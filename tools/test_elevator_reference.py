#!/usr/bin/env python3
"""Compare the active elevator owner with original AREA11 instructions.

Model creation, script handlers and drawing are intercepted boundaries. The
instruction runner checks phase/arm/sound counters, live power selection,
height patching, indicator level and observable call order. No original
instruction bytes or asset data are embedded in this source.
"""
import ctypes as C
import hashlib
import itertools
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
ACTOR, CHILD, MODEL, CHILD_MODEL = 0x900000, 0x901000, 0x902000, 0x903000
RETURN, VIRTUAL = 0xbadf00d, 0xbad0000


def signed(value, width=32):
    value &= (1 << width)-1
    return value-(1 << width) if value >> (width-1) else value


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def number(value):
    return struct.unpack('<f', struct.pack('<I', value & 0xffffffff))[0]


def guard_add(a, b):
    """EE add.s: the operand with the smaller exponent keeps one guard bit
    below the other's precision, then the sum truncates (em_pose_math.h
    pose_add; route capture 04_elevator_ride's 150 carry values)."""
    difference = (a >> 23 & 255) - (b >> 23 & 255)
    def trim(value, shift):
        return value & (0x80000000 if shift >= 25 else (0xffffffff << (shift - 1)) & 0xffffffff)
    if difference > 0: b = trim(b, difference)
    elif difference < 0: a = trim(a, -difference)
    value = number(a)+number(b)
    result = bits(value)
    if abs(number(result)) > abs(value): result -= 1
    return result


def oracle(overlay, case, motion=None):
    phase, armed, lower, timer, level, powered, done = case
    memory = {}; registers = [0]*32; floats = [0]*32; events = []
    def save(address, value, size=4):
        for i in range(size): memory[address+i] = value >> (8*i) & 255
    def load(address, size=4):
        def byte(at):
            if at in memory: return memory[at]
            return overlay[at-0x823500] if 0x823500 <= at < 0x82ad00 else 0
        return sum(byte(address+i) << (8*i) for i in range(size))
    save(ACTOR+4, 1, 1); save(ACTOR+5, phase, 1); save(ACTOR+11, armed, 1)
    save(ACTOR+0x28, level, 2); save(ACTOR+0x2a, timer, 2)
    save(ACTOR+0x2e, 7, 2); save(ACTOR+0x4c, VIRTUAL)
    height = 190.0 if lower else 230.0
    for address in (ACTOR+0xb4, 0x82a7c4, 0x82ab14): save(address, bits(height))
    save(0x82a844, bits(205.0 if lower else 245.0))
    save(0x82a944, bits(245.0 if lower else 205.0))
    save(ACTOR+0x110, MODEL); save(CHILD+0x110, CHILD_MODEL)
    save(ACTOR+0x2e4, CHILD)
    save(0x810700, 11, 1); save(0x81084c, 128 if powered else 0, 1)
    save(0x81083a, lower, 1)
    registers[4], registers[29], registers[31] = ACTOR, 0x2000000, RETURN
    if motion is not None:
        ticks, rate, owner_y, player_y, target_y = motion
        registers[5] = 0x920000
        save(registers[5]+4, phase, 1)
        save(ACTOR+0x2ec, ticks); save(ACTOR+0x2e8, bits(rate))
        save(ACTOR+0xb4, bits(owner_y)); save(0x810354, bits(player_y))
        save(0x8105e4, bits(target_y))
    def execute(word):
        op, rs, rt, rd = word >> 26, word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        immediate = signed(word & 65535, 16)
        address = (registers[rs]+immediate) & 0xffffffff
        if op == 0:
            fn = word & 63
            if fn == 0: registers[rd] = registers[rt] << (word >> 6 & 31) & 0xffffffff
            elif fn == 4: registers[rd] = registers[rt] << (registers[rs] & 31) & 0xffffffff
            elif fn in (33, 45): registers[rd] = (registers[rs]+registers[rt]) & 0xffffffff
            elif fn == 35: registers[rd] = (registers[rs]-registers[rt]) & 0xffffffff
            elif fn == 36: registers[rd] = registers[rs] & registers[rt]
            elif fn == 37: registers[rd] = registers[rs] | registers[rt]
            elif fn == 43: registers[rd] = int(registers[rs] < registers[rt])
            else: raise AssertionError(('SPECIAL', hex(word)))
        elif op == 9: registers[rt] = address
        elif op == 10: registers[rt] = int(signed(registers[rs]) < immediate)
        elif op == 11: registers[rt] = int(registers[rs] < (immediate & 0xffffffff))
        elif op == 12: registers[rt] = registers[rs] & (word & 65535)
        elif op == 13: registers[rt] = registers[rs] | (word & 65535)
        elif op == 14: registers[rt] = registers[rs] ^ (word & 65535)
        elif op == 15: registers[rt] = (word & 65535) << 16
        elif op == 17:
            if rs == 4: floats[rd] = registers[rt]
            elif rs == 20 and word & 63 == 32: floats[word >> 6 & 31] = bits(float(signed(floats[rd])))
            elif rs == 16 and word & 63 == 3: floats[word >> 6 & 31] = bits(number(floats[rd])/number(floats[rt]))
            elif rs == 16 and word & 63 == 0:
                floats[word >> 6 & 31] = guard_add(floats[rd], floats[rt])
            else: raise AssertionError(('COP1', hex(word)))
        elif op == 28 and word & 63 == 40:
            assert registers[rs] == 0 or registers[rt] == 0
            registers[rd] = registers[rs] | registers[rt]
        elif op in (30, 32, 33, 35, 36, 37):
            size = {30: 16, 32: 1, 33: 2, 35: 4, 36: 1, 37: 2}[op]
            value = load(address, size)
            registers[rt] = signed(value, size*8) & 0xffffffff if op in (32, 33) else value
        elif op in (31, 40, 41, 43): save(address, registers[rt], {31: 16, 40: 1, 41: 2, 43: 4}[op])
        elif op == 49: floats[rt] = load(address)
        elif op == 57: save(address, floats[rt])
        else: raise AssertionError(('opcode', op, hex(word)))
        registers[0] = 0
    pc = 0x827b10 if motion is None else 0x828050
    external = (0x1ba1a0, 0x1ba1f0, 0x1fbd50, 0x1c6380,
                0x1a2370, 0x102958, 0x1b17a0, VIRTUAL)
    for _ in range(2000):
        if pc == RETURN:
            if motion is not None:
                state = (load(0x920004, 1), signed(load(ACTOR+0x2ec)),
                         *(load(a) for a in (ACTOR+0x2e8, ACTOR+0xb4, 0x810354, 0x8105e4)))
                return state, events, registers[2]
            state = (load(ACTOR+5, 1), load(ACTOR+11, 1), load(0x81083a, 1),
                     signed(load(ACTOR+0x2a, 2), 16), signed(load(ACTOR+0x28, 2), 16),
                     load(ACTOR+0xb4), *(load(a) for a in (0x82a7c4, 0x82a844, 0x82a944)))
            return state, events
        if pc in external:
            if pc == 0x1ba1a0: events.append(('start', registers[5]))
            elif pc == 0x1ba1f0: events.append(('tick',)); registers[2] = done
            elif pc == 0x1fbd50: events.append(('sound', registers[5], floats[12]))
            elif pc == 0x1c6380: events.append(('pose', load(ACTOR+0xb4)))
            elif pc == 0x102958: events.append(('copy',))
            elif pc == VIRTUAL: events.append(('actor',))
            pc = registers[31] & 0xffffffff
            continue
        assert 0x827b10 <= pc < (0x828050 if motion is None else 0x8281e0), hex(pc)
        word = load(pc); op = word >> 26; rs = word >> 21 & 31; rt = word >> 16 & 31
        if op in (1, 4, 5, 6, 7, 20, 21):
            if op == 1:
                assert rt in (0, 1)
                taken = signed(registers[rs]) >= 0 if rt else signed(registers[rs]) < 0
            elif op in (4, 20): taken = registers[rs] == registers[rt]
            elif op in (5, 21): taken = registers[rs] != registers[rt]
            elif op == 6: taken = signed(registers[rs]) <= 0
            else: taken = signed(registers[rs]) > 0
            target = pc+4+signed(word & 65535, 16)*4 if taken else pc+8
            if op < 20 or taken: execute(load(pc+4))
            pc = target
        elif op in (2, 3):
            if op == 3: registers[31] = pc+8
            target = (word & 0x3ffffff) << 2
            execute(load(pc+4)); pc = target
        elif op == 0 and word & 63 in (8, 9):
            target = registers[rs] & 0xffffffff
            if word & 63 == 9: registers[word >> 11 & 31] = pc+8
            execute(load(pc+4)); pc = target
        else: execute(word); pc += 4
    raise AssertionError('Original elevator owner did not return')


class Elevator(C.Structure):
    _fields_ = [('phase', C.c_uint8), ('armed', C.c_uint8), ('lower', C.c_uint8),
                ('timer', C.c_int16), ('level', C.c_int16), ('height', C.c_float),
                ('heights', C.c_float*3)]
class Motion(C.Structure):
    _fields_ = [('phase', C.c_uint8), ('ticks', C.c_int32), ('rate', C.c_float)]
START = C.CFUNCTYPE(None, C.c_void_p, C.c_uint32)
TICK = C.CFUNCTYPE(C.c_int, C.c_void_p)
SOUND = C.CFUNCTYPE(None, C.c_void_p, C.c_uint, C.c_float)
POSE = C.CFUNCTYPE(None, C.c_void_p, C.c_float)
EVENT = C.CFUNCTYPE(None, C.c_void_p)
class Hooks(C.Structure):
    _fields_ = [('context', C.c_void_p), ('start', START), ('tick', TICK),
                ('sound', SOUND), ('pose', POSE), ('copy', EVENT), ('actor', EVENT)]


def main():
    overlay = (ROOT.parent/'Extermination/extract/OVERLAY/AREA11.BIN').read_bytes()
    assert len(overlay) == 0x7800 and overlay[:4] == b'MWo3'
    assert struct.unpack_from('<I', overlay, 8)[0] == 0x823500
    output = ROOT/'build/elevator_reference'; output.mkdir(parents=True, exist_ok=True)
    library = output/('owner.dylib' if sys.platform == 'darwin' else 'owner.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC',
        '-dynamiclib' if sys.platform == 'darwin' else '-shared', '-Isrc',
        'src/game/em_elevator.c', '-o', str(library)], cwd=ROOT, check=True)
    native = C.CDLL(str(library))
    native.em_elevator_init.argtypes = [C.POINTER(Elevator), C.c_int]
    native.em_elevator_tick.argtypes = [C.POINTER(Elevator), C.c_int, C.POINTER(Hooks)]
    native.em_elevator_motion_tick.argtypes = [C.POINTER(Motion), C.c_int,
        C.POINTER(C.c_float), C.POINTER(C.c_float), C.POINTER(C.c_float), C.POINTER(Hooks)]
    count = 0
    for case in itertools.product((0,1,2),(0,4,5),(0,1),(-3,0,119,120,300),
                                  (-3,0,1,120,127,128,160),(0,1),(0,1)):
        phase, armed, lower, timer, level, powered, done = case
        owner = Elevator(); native.em_elevator_init(C.byref(owner), lower)
        owner.phase, owner.armed, owner.timer, owner.level = phase, armed, timer, level
        events = []
        def tick(_): events.append(('tick',)); return done
        hooks = Hooks(None, START(lambda _, address: events.append(('start', address))),
            TICK(tick), SOUND(lambda _, cue, radius: events.append(('sound', cue, bits(radius)))),
            POSE(lambda _, height: events.append(('pose', bits(height)))),
            EVENT(lambda _: events.append(('copy',))), EVENT(lambda _: events.append(('actor',))))
        assert native.em_elevator_tick(C.byref(owner), powered, C.byref(hooks)) == 0
        actual = ((owner.phase,owner.armed,owner.lower,owner.timer,owner.level,
                   bits(owner.height), *(bits(v) for v in owner.heights)), events)
        expected = oracle(overlay, case)
        assert actual == expected, dict(case=case, actual=actual, expected=expected)
        count += 1
    print(f'Original00827B10 active owner: {count} state/call-order cases PASS')
    motion_count = 0
    for phase, lower, ticks, rate, position in itertools.product(
            (0,1,2), (0,1), (-1,0,149,150,0x7fffffff),
            (-0.26666668,0.26666668), (190,230,-0.1,16777216)):
        state = Motion(phase, ticks, rate)
        y = [C.c_float(position), C.c_float(position+10.8904), C.c_float(position+15)]
        events = []
        hooks = Hooks(None, START(), TICK(),
            SOUND(lambda _, cue, radius: events.append(('sound', cue, bits(radius)))),
            POSE(lambda _, height: events.append(('pose', bits(height)))), EVENT(), EVENT())
        result = native.em_elevator_motion_tick(C.byref(state), lower,
            *(C.byref(value) for value in y), C.byref(hooks))
        actual = ((state.phase, state.ticks, bits(state.rate), *(bits(v.value) for v in y)), events, result)
        expected = oracle(overlay, (phase,0,lower,0,0,0,0),
            (ticks, number(bits(rate)), position, number(bits(position+10.8904)), number(bits(position+15))))
        assert actual == expected, dict(case=(phase,lower,ticks,rate,position),actual=actual,expected=expected)
        motion_count += 1
    print(f'Original00828050 elevator carry: {motion_count} state/float/call-order cases PASS')
    # The played original ride (FIRST_LEVEL_ROUTE.md beat 04): its trace
    # rows hold the player Y (0x810354, printed to 5 decimals) of all 150
    # carry calls. Native and oracle carries from the captured start must
    # reproduce every row; a plain truncating add ends at 189.99832, the
    # capture at 190.00061.
    route = ROOT.parent/'Extermination/build/s87/route/04_elevator_ride/trace.json'
    assert route.exists(), f'route capture missing: {route} (docs/FIRST_LEVEL_ROUTE.md)'
    rows = json.loads(route.read_text())['rows']
    first = next(i for i in range(1, len(rows)) if rows[i-1]['pos'][1] == 230.0 and rows[i]['pos'][1] < 230.0)
    captured = [row['pos'][1] for row in rows[first:first+150]]
    state = Motion(0, 0, 0.0)
    y = [C.c_float(230.0), C.c_float(230.0), C.c_float(245.0)]
    hooks = Hooks(None, START(), TICK(), SOUND(lambda *_: None), POSE(lambda *_: None), EVENT(), EVENT())
    carried, oracle_y = [], (0, 0.0, 230.0, 230.0, 245.0)
    oracle_phase = 0
    for call in range(151):
        result = native.em_elevator_motion_tick(C.byref(state), 0, *(C.byref(v) for v in y), C.byref(hooks))
        expected = oracle(overlay, (oracle_phase,0,0,0,0,0,0), oracle_y)
        o_phase, o_ticks, o_rate, o_owner, o_player, o_target = expected[0]
        assert bits(y[1].value) == o_player, ('carry call', call, y[1].value, number(o_player))
        oracle_phase = o_phase
        oracle_y = (signed(o_ticks), number(o_rate), number(o_owner), number(o_player), number(o_target))
        if call: carried.append(y[1].value)
        if result: break
    assert len(carried) == 150 and [round(v, 5) for v in carried] == captured, \
        ('carry vs route 04', [(i, v, c) for i, (v, c) in enumerate(zip(carried, captured))
                               if round(v, 5) != c][:5])
    print(f'Original00828050 descent: 150 carried player Y equal route 04_elevator_ride '
          f'f{rows[first]["f"]}..f{rows[first+149]["f"]} (ends {carried[-1]:.8g})')
    (output/'owner_validation.json').write_text(json.dumps({
        'cases': count, 'motion_cases': motion_count, 'overlay_sha256': hashlib.sha256(overlay).hexdigest(),
        'entry': '00827B10', 'scope': 'active owner state1; external script/graphics hooks'
    }, indent=2)+'\n')


if __name__ == '__main__':
    main()
