#!/usr/bin/env python3
"""Execute the original 00174AC0 and compare em_player_heading_record.c.

docs/PLAYER_HEADING_RECORD.md. The user's pinned ELF (and the captured RAM)
supplies every instruction and table; none are embedded here. 00174AC0 runs
unmodified with its whole original call tree: 0011DE90 cosf, 0011E620
atan2f, 0011DF78 fabsf, 001B1470 wrap and 001B12B0 turn all execute as
original code. Only 00128350 (the atan2f error path, both operands zero) is
hooked, and reaching it fails the test: two float cosines of byte angles are
never both zero.

Arithmetic: the interpreter is test_script_host_workers_reference.ScriptEE
(imported, not edited), whose COP1 is tools/ee_float_model.py.

Every case compares all 0x320 record bytes, the 0x70003A20 word and the
return value, and asserts that the original wrote nothing else (outside its
own stack frame).

Parts (every mode):
  1. synthetic records (random bytes, then the fields the routine reads set
     to boundary and random values; errors aimed at the 3pi/4 and 0.3pi
     edges and at ang == +C4), with both outcomes of every conditional
     branch asserted except the two sign tests of a zero-extended byte;
  2. captured RAM: every startup-reference capture and every route beat
     00..14 (player record D_008102B0, the pad bytes, the camera yaw and the
     scratchpad): as captured with arg 0 / 1 / 2, with 0x70003B8D cleared,
     and with the stick values the beat's route inputs used;
  3. the worker adapters and fail-stop (every unbound pointer, the 001B1470
     domain bound with the original's writes before it).
Quick mode samples the bulk sweeps; EM_TEST_FULL=1 runs them whole.
"""
import ctypes as C
import json
import math
import os
import random
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode as rm  # noqa: E402
import test_script_host_workers_reference as S  # noqa: E402

MASK = 0xFFFFFFFF
DECOMP = ROOT.parent / 'Extermination'
REF = DECOMP / 'build/startup-reference'
ROUTE = DECOMP / 'build/s87/route'
LANE = os.environ.get('EM_LANE', 'player_heading_record_reference')
OUT = ROOT / 'build' / LANE

HEADING, SIZE = 0x174AC0, 0x508
PLAYER = 0x8102B0
SPAD_3B8D, SPAD_3A20 = 0x70003B8D, 0x70003A20
PAD_GAIT, PAD_X, PAD_Y, CAMERA_YAW = 0x810E57, 0x810E64, 0x810E65, 0x8106A0
ERROR_PATH = 0x128350
F = S.F


def flt(word):
    return struct.unpack('<f', struct.pack('<I', word & MASK))[0]


# ======================================================================
# Oracle
# ======================================================================

class ErrorPath(Exception):
    pass


class HeadingEE(S.ScriptEE):
    """ScriptEE with branch outcomes recorded inside 00174AC0 only."""

    def branch(self, word, pc):
        b = S.EE.branch(self, word, pc)
        if b is not None and self.outcomes is not None and HEADING <= pc < HEADING + SIZE:
            self.outcomes.add((pc, b[0]))
        return b


def conditional_branches(elf):
    """Every conditional branch of 00174AC0 (the always-taken form excluded)."""
    ee = S.EE(elf)
    found = set()
    for pc in range(HEADING, HEADING + SIZE, 4):
        word = ee.load(pc)
        op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
        if op in (4, 20) and rs == 0 and rt == 0:
            continue
        if op in (4, 5, 6, 7, 20, 21, 22, 23, 1) or (op == 17 and rs == 8):
            found.add(pc)
    return found


def never_taken_byte_sign_tests(elf):
    """The two sign tests 00174AC0 applies to a zero-extended stick byte:
    their taken outcome cannot occur (the original's unsigned conversion
    idiom). Found by decoding, not by address."""
    ee = S.EE(elf)
    out = set()
    for pc in range(HEADING, HEADING + SIZE, 4):
        word = ee.load(pc)
        # a regimm "less than zero" branch on a register that one of the
        # three instructions before it loaded with an unsigned byte load (op 36)
        if word >> 26 != 1 or (word >> 16 & 31) != 0:
            continue
        reg = word >> 21 & 31
        for back in (4, 8, 12):
            prev = ee.load(pc - back)
            if prev >> 26 == 36 and (prev >> 16 & 31) == reg:
                out.add((pc, True))
                break
    return out


class Oracle:
    def __init__(self, elf, ram=None, spad=None):
        self.ee = HeadingEE(elf, ram)
        if spad is not None:
            self.ee.spad = bytearray(spad)
        self.ee.hooks[ERROR_PATH] = self.error_path

    def error_path(self, ee):
        raise ErrorPath()

    def load_state(self, state):
        ee = self.ee
        ee.write(PLAYER, state['record'])
        ee.save(SPAD_3B8D, state['s3B8D'], 1)
        ee.save(SPAD_3A20, state['s3A20'])
        ee.save(PAD_GAIT, state['gait'], 1)
        ee.save(PAD_X, state['x'], 1)
        ee.save(PAD_Y, state['y'], 1)
        ee.save(CAMERA_YAW, state['camera'])

    def run(self, state, arg):
        """(record bytes, 0x70003A20, v0, stray writes)."""
        self.load_state(state)
        ee = self.ee
        ee.written = set()
        v0, _ = ee.call_bits(HEADING, (PLAYER, arg & MASK))
        allowed = set(range(PLAYER, PLAYER + 0x320)) | set(range(SPAD_3A20, SPAD_3A20 + 4))
        stray = sorted(a for a in ee.written if a not in allowed)
        return ee.read(PLAYER, 0x320), ee.load(SPAD_3A20), v0, stray


# ======================================================================
# Native side (ctypes)
# ======================================================================

P = C.POINTER
U8, U32, I32, VP = C.c_uint8, C.c_uint32, C.c_int32, C.c_void_p


class LiveActor(C.Structure):
    _fields_ = [('bytes', U8 * 0x320), ('link_owner', VP), ('link_prev', VP),
                ('link_flags', U8), ('link_type', U8)]


class SdkWorld(C.Structure):
    _fields_ = [('d26C5D0', P(I32))]


class World(C.Structure):
    _fields_ = [('spad3B8D', P(U8)), ('d810E57', P(U8)), ('d810E64', P(U8)), ('d810E65', P(U8)),
                ('d8106A0', P(U32)), ('spad3A20', P(U32)), ('sdk_tables', VP),
                ('sdk_world', P(SdkWorld)), ('sdk_workers', VP)]


class Heading(C.Structure):
    _fields_ = [('world', World), ('fault_address', U32)]


SHIM = r'''
#include <stddef.h>
#include "game/em_player_heading_record.h"
size_t hr_layout(int i) {
    switch (i) {
    case 0: return sizeof(EmPlayerHeadingRecord);
    case 1: return offsetof(EmPlayerHeadingRecord, fault_address);
    case 2: return offsetof(EmPlayerHeadingRecordWorld, spad3A20);
    case 3: return offsetof(EmPlayerHeadingRecordWorld, sdk_workers);
    case 4: return sizeof(EmSdkMathTables);
    case 5: return sizeof(EmPlayerLiveActor);
    default: return 0;
    }
}
'''
SOURCES = ['src/game/em_player_heading_record.c', 'src/game/em_script_host_workers.c',
           'src/game/em_player_stage_workers.c', 'src/game/em_sdk_math_original.c',
           'src/game/em_script.c']


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    shim = OUT / 'layout_shim.c'
    shim.write_text(SHIM)
    lib = OUT / ('heading_record.dylib' if sys.platform == 'darwin' else 'heading_record.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc', *SOURCES, str(shim),
                    '-o', str(lib)], cwd=ROOT, check=True)
    n = C.CDLL(str(lib))
    n.hr_layout.restype = C.c_size_t
    n.hr_layout.argtypes = [C.c_int]
    mine = (C.sizeof(Heading), Heading.fault_address.offset, World.spad3A20.offset,
            World.sdk_workers.offset, None, C.sizeof(LiveActor))
    for i, value in enumerate(mine):
        if value is not None:
            assert n.hr_layout(i) == value, ('ctypes layout differs from C', i, n.hr_layout(i), value)
    n.em_player_heading_record_00174AC0.argtypes = [P(Heading), P(LiveActor), I32, P(I32)]
    n.em_player_heading_record_worker_result.argtypes = [VP, P(LiveActor), C.c_int, P(C.c_int)]
    n.em_player_heading_record_worker.argtypes = [VP, P(LiveActor), C.c_int]
    n.em_sdk_math_original_load_tables.argtypes = [P(C.c_ubyte), C.c_size_t, VP]
    return n


class Native:
    def __init__(self, lib, elf):
        self.lib = lib
        self.actor = LiveActor()
        self.cells = {name: U8() for name in ('s3B8D', 'gait', 'x', 'y')}
        self.camera, self.s3A20 = U32(), U32()
        self.tables = (C.c_ubyte * lib.hr_layout(4))()
        buf = (C.c_ubyte * len(elf)).from_buffer_copy(elf)
        assert lib.em_sdk_math_original_load_tables(buf, len(elf), C.cast(self.tables, VP)) == 0
        self.mode = I32(struct.unpack_from('<i', elf, 0x26C5D0 - 0x100000 + 0x300)[0])
        self.sdk_world = SdkWorld(C.pointer(self.mode))
        self.h = Heading()
        self.bind()

    def bind(self):
        w = self.h.world
        w.spad3B8D, w.d810E57, w.d810E64, w.d810E65 = (
            C.pointer(self.cells[k]) for k in ('s3B8D', 'gait', 'x', 'y'))
        w.d8106A0, w.spad3A20 = C.pointer(self.camera), C.pointer(self.s3A20)
        w.sdk_tables, w.sdk_world, w.sdk_workers = C.cast(self.tables, VP), C.pointer(self.sdk_world), None
        self.h.fault_address = 0

    def load_state(self, state):
        C.memmove(self.actor.bytes, state['record'], 0x320)
        for k in ('s3B8D', 'gait', 'x', 'y'):
            self.cells[k].value = state[k]
        self.camera.value, self.s3A20.value = state['camera'], state['s3A20']

    def run(self, state, arg):
        self.bind()
        self.load_state(state)
        out = I32(-7)
        rc = self.lib.em_player_heading_record_00174AC0(C.byref(self.h), C.byref(self.actor), arg,
                                                        C.byref(out))
        return rc, bytes(self.actor.bytes), self.s3A20.value, out.value & MASK


def compare(oracle, native, state, arg, where):
    o_rec, o_3a20, o_v0, stray = oracle.run(state, arg)
    assert not stray, (where, 'the original wrote outside the record', [hex(a) for a in stray[:8]])
    rc, n_rec, n_3a20, n_v0 = native.run(state, arg)
    assert rc == 0, (where, 'native faulted', hex(native.h.fault_address))
    if n_rec != o_rec:
        diff = [hex(i) for i in range(0x320) if n_rec[i] != o_rec[i]]
        raise AssertionError((where, arg, 'record differs at', diff[:12]))
    assert n_3a20 == o_3a20, (where, arg, '0x70003A20', hex(n_3a20), hex(o_3a20))
    assert n_v0 == o_v0, (where, arg, 'v0', n_v0, o_v0)
    before = state['record']
    return {
        'turned': o_rec[0xC4:0xC8] != before[0xC4:0xC8],
        'skid': o_rec[0x1F0] == 7 and before[0x1F0] != 7,
        'flag25D': o_rec[0x25D] == 1 and before[0x25D] != 1,
        'recorded': o_rec[0x218:0x21C] != before[0x218:0x21C],
        'wrote3A20': o_3a20 != state['s3A20'],
    }


# ======================================================================
# Part 1: synthetic records
# ======================================================================

SPEEDS = [0x00000000, 0x80000000, 0x00000001, 0x3DCCCCCD, 0x3DCCCCCE, 0x3DCCCCCC, 0x3E99999A,
          0x3E99999B, 0x3E999999, 0x3F000000, 0x3F000001, 0x3EFFFFFF, 0x3F4CCCCD, 0xBF000000]
YAWS = [0x00000000, 0x80000000, 0x40490FDB, 0xC0490FDB, 0x40490FDC, 0x3F800000, 0xC1200000,
        0x41200000]
CAMERAS = [0x00000000, 0x40490FDB, 0xC0490FDB, 0x40C90FDB, 0xC0E00000, 0x3F000000]
STICK = [0, 1, 64, 127, 128, 129, 192, 255]
EDGES = [0x4016CBE4, 0x3F71463B]   # 3pi/4 and 0.3pi


def base_state(rng):
    record = bytearray(rng.randbytes(0x320))
    s = dict(record=record, s3B8D=0, s3A20=rng.getrandbits(32), gait=rng.choice([0, 1, 2, 3, 3, 2]),
             x=rng.choice(STICK) if rng.random() < 0.3 else rng.randrange(256),
             y=rng.choice(STICK) if rng.random() < 0.3 else rng.randrange(256),
             camera=rng.choice(CAMERAS) if rng.random() < 0.2 else F(rng.uniform(-math.pi, math.pi)))
    r = rng.random()
    if r < 0.03: s['s3B8D'] = rng.choice([1, 3, 0x80])
    elif r < 0.06: s['gait'] = rng.choice([4, 7, 0xFF])
    put = lambda at, word: struct.pack_into('<I', record, at, word)
    record[0x5] = rng.choice([1, 1, 1, 1, 0, 2, 0x10])
    record[0x1F0] = rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 6, 7, rng.randrange(256)])
    put(0x38, rng.choice(SPEEDS) if rng.random() < 0.6 else F(rng.uniform(0, 1)))
    put(0xC4, rng.choice(YAWS) if rng.random() < 0.15 else F(rng.uniform(-math.pi, math.pi) *
                                                              rng.choice([1, 1, 1, 3])))
    return s


def unit_cases(seed, count):
    rng = random.Random(seed)
    cases = []
    for i in range(count):
        state = base_state(rng)
        arg = rng.choice([1, 1, 1, 1, 0, 2, 2, 3, -1])
        aim = None
        if i % 4 == 0:
            aim = (rng.choice(EDGES + [0]), rng.randrange(-3, 4), rng.choice([1, -1]))
        cases.append((state, arg, aim))
    return cases


def aimed(oracle, state, aim):
    """Set +C4 so that ang - +C4 lands on (or a few ulps from) an edge, or
    equals ang: the routine's own heading comes from an arg 2 probe."""
    edge, step, sign = aim
    # ang does not depend on the gait; the probe must not take an early return
    probe = dict(state, record=bytearray(state['record']), s3B8D=0, gait=1)
    probe['record'][0x5] = 0
    rec, *_ = oracle.run(probe, 2)
    ang = struct.unpack_from('<I', rec, 0x218)[0]
    if edge == 0:
        yaw = ang
    else:
        target = F(flt(edge + step) * sign)
        yaw = S.M.ee_sub(ang, target)
    struct.pack_into('<I', state['record'], 0xC4, yaw)
    if edge == 0x4016CBE4 and state['record'][0x5] == 1:
        state['record'][0x1F0] = state['record'][0x1F0] % 6
        struct.pack_into('<I', state['record'], 0x38, 0x3F4CCCCD)
        state['gait'] = 3


def unit_job(cases):
    elf, lib = CONTEXT['elf'], CONTEXT['lib']
    oracle = Oracle(elf)
    oracle.ee.outcomes = set()
    native = Native(lib, elf)
    tags = {}
    for index, (state, arg, aim) in enumerate(cases):
        if aim is not None:
            aimed(oracle, state, aim)
        state['record'] = bytes(state['record'])
        seen = compare(oracle, native, state, arg, ('unit', index))
        for k, v in seen.items():
            if v: tags[k] = tags.get(k, 0) + 1
    return tags, oracle.ee.outcomes


# ======================================================================
# Part 2: captured RAM
# ======================================================================

def captures():
    out = [(name, REF / f'{name}.bin', REF / 'opening_scratchpad.bin' if name == 'opening_ee' else None)
           for name in ('opening_ee', 'handoff_ee', 'playable_ee')]
    out += [(p.parent.name, p, p.parent / 'scratchpad.bin') for p in sorted(ROUTE.glob('*/eeMemory.bin'))
            if rm.in_scope_beat(p.parent.name)]
    return [(n, p, s) for n, p, s in out if p.is_file()]


def route_sticks(beat_dir):
    trace = beat_dir / 'trace.json'
    if not trace.is_file():
        return []
    inputs = json.loads(trace.read_text()).get('inputs', [])
    return sorted({(i['lx'] & 0xFF, i['ly'] & 0xFF) for i in inputs if 'lx' in i and 'ly' in i})


def capture_job(item):
    name, path, spad_path = item
    elf, lib = CONTEXT['elf'], CONTEXT['lib']
    ram = path.read_bytes()
    spad = spad_path.read_bytes() if spad_path is not None and spad_path.is_file() else bytes(0x4000)
    oracle = Oracle(elf, ram, spad)
    native = Native(lib, elf)
    word = lambda address: struct.unpack_from('<I', ram, address)[0]
    captured = dict(record=ram[PLAYER:PLAYER + 0x320], s3B8D=spad[0x3B8D],
                    s3A20=struct.unpack_from('<I', spad, 0x3A20)[0], gait=ram[PAD_GAIT],
                    x=ram[PAD_X], y=ram[PAD_Y], camera=word(CAMERA_YAW))
    states = [('captured', captured)]
    if captured['s3B8D']:
        states.append(('3B8D cleared', dict(captured, s3B8D=0)))
    sticks = route_sticks(path.parent) if path.parent.parent == ROUTE else []
    sticks = rm.select(sticks, 6, len(name))
    for x, y in sticks:
        for gait in (1, 2, 3):
            states.append((f'stick {x},{y} gait {gait}', dict(captured, s3B8D=0, gait=gait, x=x, y=y)))
    runs, tags = 0, {}
    for label, state in states:
        for arg in (0, 1, 2):
            seen = compare(oracle, native, state, arg, (name, label))
            for k, v in seen.items():
                if v: tags[k] = tags.get(k, 0) + 1
            runs += 1
    return name, runs, len(sticks), captured['s3B8D'], captured['record'][0x5], tags


# ======================================================================
# Part 3: adapters and fail-stop
# ======================================================================

def fault_part(elf, lib):
    rng = random.Random(0x174AC0)
    native = Native(lib, elf)
    oracle = Oracle(elf)
    checks = 0
    state = base_state(rng)
    state.update(s3B8D=0, gait=3)
    state['record'] = bytes(state['record'])
    # every unbound pointer faults at 0x00174AC0 before any write
    for field in ('spad3B8D', 'd810E57', 'd810E64', 'd810E65', 'd8106A0', 'spad3A20',
                  'sdk_tables', 'sdk_world'):
        native.bind()
        native.load_state(state)
        setattr(native.h.world, field, None)
        out = I32(-7)
        rc = lib.em_player_heading_record_00174AC0(C.byref(native.h), C.byref(native.actor), 1, C.byref(out))
        assert rc == -1 and native.h.fault_address == 0x174AC0, (field, rc, hex(native.h.fault_address))
        assert bytes(native.actor.bytes) == state['record'] and native.s3A20.value == state['s3A20'], field
        checks += 1
    native.bind(); native.load_state(state)
    assert lib.em_player_heading_record_00174AC0(C.byref(native.h), C.byref(native.actor), 1, None) == -1
    assert native.h.fault_address == 0x174AC0 and bytes(native.actor.bytes) == state['record']
    native.bind(); native.load_state(state)
    assert lib.em_player_heading_record_00174AC0(C.byref(native.h), None, 1, C.byref(I32())) == -1
    assert native.h.fault_address == 0x174AC0
    checks += 2
    # the 001B1470 domain bound: camera yaw 4096.0 faults at 0x001B1470; the
    # writes before the call equal the original's (which loops through).
    big = dict(state, camera=0x45800000)
    o_rec, *_ = oracle.run(big, 1)
    native.bind(); native.load_state(big)
    out = I32(-7)
    rc = lib.em_player_heading_record_00174AC0(C.byref(native.h), C.byref(native.actor), 1, C.byref(out))
    assert rc == -1 and native.h.fault_address == 0x1B1470, (rc, hex(native.h.fault_address))
    n_rec = bytes(native.actor.bytes)
    for at, size in ((0x23F, 1), (0x240, 4), (0x244, 4), (0x248, 4), (0x24C, 4)):
        assert n_rec[at:at + size] == o_rec[at:at + size], ('prefix write', hex(at))
    assert n_rec[0xC4:0xC8] == state['record'][0xC4:0xC8]
    checks += 1
    # the adapters: same record, result and scratch as the routine
    for arg in (0, 1, 2):
        expected = native.run(state, arg)
        native.bind(); native.load_state(state)
        r = C.c_int(-7)
        assert lib.em_player_heading_record_worker_result(C.byref(native.h), C.byref(native.actor), arg,
                                                          C.byref(r)) == 0
        assert (bytes(native.actor.bytes), native.s3A20.value, r.value & MASK) == expected[1:], arg
        native.bind(); native.load_state(state)
        assert lib.em_player_heading_record_worker(C.byref(native.h), C.byref(native.actor), arg) == 0
        assert (bytes(native.actor.bytes), native.s3A20.value) == expected[1:3], arg
        checks += 2
    native.bind(); native.load_state(state)
    assert lib.em_player_heading_record_worker_result(C.byref(native.h), C.byref(native.actor), 1, None) == -1
    assert native.h.fault_address == 0x174AC0
    assert lib.em_player_heading_record_worker(None, C.byref(native.actor), 1) == -1
    checks += 2
    return checks


# ======================================================================

CONTEXT = {}


def job(item):
    kind, payload = item
    if kind == 'unit': return kind, unit_job(payload)
    return kind, capture_job(payload)


def main():
    start = time.time()
    elf = S.read_elf()
    lib = build_native()
    CONTEXT.update(elf=elf, lib=lib)
    total = 200000
    units = unit_cases(0x174AC0, rm.pick(total, 6000))
    slices = 8
    items = [('unit', units[i::slices]) for i in range(slices)]
    items += [('capture', c) for c in captures()]
    results = rm.parallel_map(job, items, cost=lambda item: 3 if item[0] == 'capture' else 2)
    by = lambda kind: [r for k, r in results if k == kind]

    tags, outcomes = {}, set()
    for t, o in by('unit'):
        for k, v in t.items(): tags[k] = tags.get(k, 0) + v
        outcomes |= o
    branches = conditional_branches(elf)
    impossible = never_taken_byte_sign_tests(elf)
    assert len(impossible) == 2, impossible
    missing = {(pc, taken) for pc in branches for taken in (False, True)} - outcomes - impossible
    assert not missing, ('branch outcomes never exercised', sorted((hex(p), t) for p, t in missing))
    for k in ('turned', 'skid', 'flag25D', 'recorded', 'wrote3A20'):
        assert tags.get(k, 0) > 0, ('never exercised', k)
    print(f'unit: {len(units):,} synthetic records ({", ".join(f"{k} {v:,}" for k, v in sorted(tags.items()))}); '
          f'{len(branches)} conditional branches, both outcomes each (the 2 byte sign tests: not-taken only)')

    captured = by('capture')
    assert len(captured) >= 18, ('captures missing', [c[0] for c in captured])
    ctags = {}
    for *_, t in captured:
        for k, v in t.items(): ctags[k] = ctags.get(k, 0) + v
    assert ctags.get('turned', 0) > 0 and ctags.get('recorded', 0) > 0, ctags
    print(f'captured RAM: {len(captured)} captures, {sum(c[1] for c in captured):,} runs '
          f'({sum(c[2] for c in captured)} route stick values x gait 1..3 x arg 0/1/2; '
          f'{sum(1 for c in captured if c[3])} captures with 0x70003B8D set, also run cleared); '
          + ', '.join(f'{k} {v:,}' for k, v in sorted(ctags.items())))
    checks = fault_part(elf, lib)
    print(f'fail-stop and adapters: {checks} checks')
    rm.banner(rm.part(len(units), total, 'synthetic records'), f'{len(captured)} captures')
    print(f'00174AC0 record translation matches the original instructions ({time.time() - start:.1f} s)')


if __name__ == '__main__':
    sys.exit(main())
