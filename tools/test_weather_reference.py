#!/usr/bin/env python3
"""Execute the user's original weather controller and compare readable C.

Rendering is intercepted at its two original calls. Tests compare actor state,
random consumption and render parameters; this does not validate VU rendering.
The bounded EE interpreter rounds finite arithmetic toward zero. No original
instructions, captures or tables are embedded in this tool.
"""
from __future__ import annotations
import ctypes as C
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
ENTRY, END = 0x1E55F0, 0x1E5AC0
ACTOR, RETURN = 0x600000, 0xBADF00D


def signed(value, bits=32):
    value &= (1 << bits) - 1
    return value - (1 << bits) if value >> (bits - 1) else value


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def number(value):
    return struct.unpack('<f', struct.pack('<I', value & 0xffffffff))[0]


def truncate(value):
    rounded = number(bits(value))
    return number(bits(rounded) - 1) if abs(rounded) > abs(value) else rounded


class Weather(C.Structure):
    _fields_ = [('phase', C.c_float * 6), ('drift', C.c_float * 6),
                ('intensity', C.c_float), ('target', C.c_float),
                ('rate', C.c_float), ('seed', C.c_uint32),
                ('burst_wait', C.c_int32), ('state', C.c_uint8)]


class Frame(C.Structure):
    _fields_ = [('strength', C.c_float), ('intensity', C.c_uint8),
                ('draw', C.c_uint8), ('render_flags', C.c_uint8),
                ('released', C.c_uint8)]


def oracle(elf, initial, flags, area, selector, transition, task, draws):
    memory = {}
    registers = [0] * 32
    floats = [0] * 32
    condition = False
    hi = 0
    calls = 0
    rendered = None
    released = False

    def save(address, value, size=4):
        for i in range(size):
            memory[address + i] = (value >> (i * 8)) & 255

    def load(address, size=4):
        if address in memory:
            return sum(memory.get(address + i, 0) << (8 * i) for i in range(size))
        if 0x100000 <= address and address + size <= 0x275B00:
            return int.from_bytes(elf[address - 0x100000 + 0x300:
                                       address - 0x100000 + 0x300 + size], 'little')
        return sum(memory.get(address + i, 0) << (8 * i) for i in range(size))

    def fetch(pc):
        assert (ENTRY <= pc < END or 0x128250 <= pc < 0x128300 or
                0x1278C0 <= pc < 0x127970), hex(pc)
        return load(pc)

    for i, value in enumerate(initial[:68]):
        save(ACTOR + 0x1F0 + i, value, 1)
    save(ACTOR + 4, initial[68], 1)
    save(0x70003B8D, selector, 1)
    save(0x810700, area >> 8, 1)
    save(0x810701, area & 255, 1)
    save(0x8106B8, transition, 1)
    save(0x28A9A0, task, 2)
    save(0x275670, 0x620000)
    save(0x62001C, 0x630000)
    registers[4], registers[28], registers[29], registers[31] = ACTOR, 0x27D370, 0x700000, RETURN

    def plain(word):
        nonlocal hi, condition
        op, rs, rt, rd = word >> 26, word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        imm = signed(word & 65535, 16)
        address = (registers[rs] + imm) & 0xffffffff
        if op == 0:
            fn = word & 63
            if fn == 0: registers[rd] = (registers[rt] << (word >> 6 & 31)) & 0xffffffff
            elif fn == 2: registers[rd] = (registers[rt] & 0xffffffff) >> (word >> 6 & 31)
            elif fn == 3: registers[rd] = signed(registers[rt]) >> (word >> 6 & 31) & 0xffffffff
            elif fn == 4: registers[rd] = (registers[rt] << (registers[rs] & 31)) & 0xffffffff
            elif fn == 6: registers[rd] = (registers[rt] & 0xffffffff) >> (registers[rs] & 31)
            elif fn == 7: registers[rd] = signed(registers[rt]) >> (registers[rs] & 31) & 0xffffffff
            elif fn == 16: registers[rd] = hi
            elif fn == 26:
                x, y = signed(registers[rs]), signed(registers[rt])
                hi = (x - int(x / y) * y) & 0xffffffff
            elif fn == 33: registers[rd] = (registers[rs] + registers[rt]) & 0xffffffff
            elif fn == 35: registers[rd] = (registers[rs] - registers[rt]) & 0xffffffff
            elif fn == 36: registers[rd] = registers[rs] & registers[rt]
            elif fn == 37: registers[rd] = registers[rs] | registers[rt]
            elif fn == 38: registers[rd] = registers[rs] ^ registers[rt]
            elif fn == 42: registers[rd] = int(signed(registers[rs]) < signed(registers[rt]))
            elif fn == 43: registers[rd] = int((registers[rs] & 0xffffffff) < (registers[rt] & 0xffffffff))
            elif fn == 45: registers[rd] = (registers[rs] + registers[rt]) & 0xffffffffffffffff
            else: raise AssertionError(('SPECIAL', fn))
        elif op == 9: registers[rt] = address
        elif op == 10: registers[rt] = int(signed(registers[rs]) < imm)
        elif op == 11: registers[rt] = int((registers[rs] & 0xffffffff) < (imm & 0xffffffff))
        elif op == 12: registers[rt] = registers[rs] & (word & 65535)
        elif op == 13: registers[rt] = registers[rs] | (word & 65535)
        elif op == 14: registers[rt] = registers[rs] ^ (word & 65535)
        elif op == 15: registers[rt] = (word & 65535) << 16
        elif op == 28 and word & 63 == 40:
            registers[rd] = sum((((registers[rs] >> (8*i) & 255) +
                                  (registers[rt] >> (8*i) & 255)) & 255) << (8*i)
                                for i in range(16))
        elif op in (30, 33, 35, 36, 55):
            size = {30: 16, 33: 2, 35: 4, 36: 1, 55: 8}[op]
            value = load(address, size)
            registers[rt] = signed(value, 16) & 0xffffffff if op == 33 else value
        elif op in (31, 40, 41, 43, 63): save(address, registers[rt], {31: 16, 40: 1, 41: 2, 43: 4, 63: 8}[op])
        elif op == 49: floats[rt] = load(address)
        elif op == 57: save(address, floats[rt])
        elif op == 17:
            fs, fd, fn = rd, word >> 6 & 31, word & 63
            if rs == 0: registers[rt] = floats[fs]
            elif rs == 4: floats[fs] = registers[rt] & 0xffffffff
            elif rs == 20 and fn == 32: floats[fd] = bits(truncate(float(signed(floats[fs]))))
            elif rs == 16:
                x, y = number(floats[fs]), number(floats[rt])
                if fn == 0: floats[fd] = bits(truncate(x + y))
                elif fn == 1: floats[fd] = bits(truncate(x - y))
                elif fn == 2: floats[fd] = bits(truncate(x * y))
                elif fn == 3: floats[fd] = bits(truncate(x / y))
                elif fn == 5: floats[fd] = floats[fs] & 0x7fffffff
                elif fn == 6: floats[fd] = floats[fs]
                elif fn == 7: floats[fd] = floats[fs] ^ 0x80000000
                elif fn in (13, 36): floats[fd] = int(x) & 0xffffffff
                elif fn == 52: condition = x < y
                elif fn == 54: condition = x <= y
                else: raise AssertionError(('FPU', fn))
            else: raise AssertionError(('COP1', rs, fn))
        else: raise AssertionError(('opcode', op))
        registers[0] = 0

    pc = ENTRY
    for _ in range(3000):
        if pc == RETURN:
            result = bytes(load(ACTOR + 0x1F0 + i, 1) for i in range(68))
            result += bytes([load(ACTOR + 4, 1)])
            return result, calls, rendered, released, load(0x8106BF, 1)
        word = fetch(pc)
        op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
        offset = signed(word & 65535, 16) * 4
        branch = None
        if op == 3:
            target = (word & 0x3ffffff) << 2
            plain(fetch(pc + 4))
            if target == 0x122BB8:
                registers[2] = draws[calls]
                calls += 1
            elif target == 0x1B0070: registers[2] = flags
            elif target == 0x11DF78: floats[0] = floats[12] & 0x7fffffff
            elif target in (0x128250, 0x1278C0):
                registers[31] = pc + 8
                pc = target
                continue
            elif target in (0x1E5AC0, 0x1E67C0):
                rendered = (2 if target == 0x1E5AC0 else 1, registers[5], registers[6], floats[12])
            elif target == 0x1AFC10: released = True
            elif target != 0x1D2DE0: raise AssertionError(('callee', hex(target)))
            pc += 8
            continue
        if op in (4, 5, 20, 21):
            taken = (registers[rs] == registers[rt]) == (op in (4, 20))
            if op in (20, 21) and not taken:
                pc += 8
                continue
            branch = pc + 4 + offset if taken else pc + 8
        elif op == 7: branch = pc + 4 + offset if signed(registers[rs]) > 0 else pc + 8
        elif op == 1:
            taken = signed(registers[rs]) >= 0 if rt == 1 else signed(registers[rs]) < 0
            assert rt in (0, 1)
            branch = pc + 4 + offset if taken else pc + 8
        elif op == 17 and rs == 8: branch = pc + 4 + offset if condition == bool(rt & 1) else pc + 8
        elif op == 0 and word & 63 == 8: branch = registers[rs]
        if branch is not None:
            plain(fetch(pc + 4))
            pc = branch
        else:
            plain(word)
            pc += 4
    raise AssertionError('Weather oracle failed to return')


def main():
    elf = (ROOT.parent / 'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
    output = ROOT / 'build/weather_reference'
    output.mkdir(parents=True, exist_ok=True)
    library = output / 'weather.so'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc',
                    'src/game/em_weather.c', '-o', str(library)], cwd=ROOT, check=True)
    native = C.CDLL(str(library))
    callback = C.CFUNCTYPE(C.c_uint32, C.c_void_p)
    native.em_weather_tick.argtypes = [C.POINTER(Weather), C.c_uint32, C.c_uint,
                                     C.c_uint, C.c_uint, C.c_uint, callback, C.c_void_p]
    native.em_weather_tick.restype = Frame
    rng = random.Random(0x1E55F0)
    comparisons = random_calls = 0
    flags_list = (0, 0x10, 0x20, 0x40, 0x70, 0x2000000, 0x4000000,
                  0x8000000, 0xe000000, 0x20081910)
    for index in range(2400):
        weather = Weather()
        weather.state = (index // len(flags_list)) % 5
        for i in range(6):
            weather.phase[i] = rng.uniform(1, 2)
            weather.drift[i] = rng.uniform(0, 1)
        weather.intensity = rng.choice([0, 0.1, 3, 48, 63, 90, 127])
        weather.target = weather.intensity + rng.choice([-3.01, -3, -2.99, 0, 2.99, 3, 3.01])
        weather.rate = rng.choice([0.003, 0.05])
        weather.seed = rng.randrange(0x80000000)
        weather.burst_wait = rng.choice([-1, 0, 1, 15])
        flags = flags_list[index % len(flags_list)]
        area = rng.choice([0xb00, 0xf01, 0x1500])
        selector, transition, task = rng.randrange(2), rng.randrange(3), rng.randrange(3)
        for tick in range(5):
            draws = [rng.randrange(0x80000000) for _ in range(32)]
            expected, count, rendered, released, intensity = oracle(
                elf, bytes(weather), flags, area, selector, transition, task, draws)
            called = []
            def rand(_):
                called.append(1)
                return draws[len(called)-1]
            frame = native.em_weather_tick(C.byref(weather), flags, area, selector,
                                            transition, task, callback(rand), None)
            assert bytes(weather)[:69] == expected, (index, tick, 'state')
            assert len(called) == count, (index, tick, 'random count', len(called), count)
            assert bool(frame.released) == released
            if rendered:
                assert (frame.draw, frame.render_flags, weather.seed, bits(frame.strength)) == rendered, (index, tick, 'render')
                assert frame.intensity == intensity, (index, tick, 'intensity')
            else:
                assert not frame.draw
            comparisons += 1
            random_calls += count
    result = {'state_comparisons': comparisons, 'random_calls': random_calls,
              'scope': 'EE controller state and render-call parameters; renderers intercepted'}
    (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(f'weather reference: PASS {comparisons} state comparisons, {random_calls} identical RNG calls')


if __name__ == '__main__':
    main()
