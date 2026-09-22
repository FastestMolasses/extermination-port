#!/usr/bin/env python3
"""Compare finite ITEM SDK functions with the original local ELF instructions.

Full sin/cos/reduction/kernels and nonnegative sqrt execute independently.
No host libm replacement is used inside the original numerical oracle.
"""
import ctypes as C
import hashlib
import json
import random
import struct
import subprocess
import sys

from test_interaction_scan_reference import ScanOracle, ELF_SHA
from test_interaction_pickup_reference import Math as AtanMath
from test_item_trail_reference import Math, Stick, Trail, Emit, Original as TrailOriginal
from test_point_light_reference import bits, number, signed, RETURN
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class Original(ScanOracle):
    def run(self, entry, args=(), floats=(), stop=RETURN):
        self.r[31] = RETURN
        for i, value in enumerate(args):
            self.r[4 + i] = value
        for i, value in enumerate(floats):
            self.f[12 + i] = bits(value)
        pc = entry
        # A full trail executes 1,056 software float-to-int conversions.
        for _ in range(400000):
            if pc == stop:
                return
            word = self.load(pc)
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            offset = signed(word & 65535, 16) * 4
            branch = None
            if op in (2, 3):
                target = (word & 0x3ffffff) * 4
                if op == 3:
                    self.r[31] = pc + 8
                self.plain(self.load(pc + 4))
                if target in self.calls:
                    self.calls[target](self)
                    pc = self.r[31]
                else:
                    pc = target
                continue
            if op in (4, 5, 20, 21):
                taken = (self.r[rs] == self.r[rt]) == (op in (4, 20))
                if op in (20, 21) and not taken:
                    pc += 8
                    continue
                branch = pc + 4 + offset if taken else pc + 8
            elif op in (6, 7):
                taken = signed(self.r[rs], 64) <= 0 if op == 6 else signed(self.r[rs], 64) > 0
                branch = pc + 4 + offset if taken else pc + 8
            elif op == 1:
                assert rt in (0, 1)
                taken = signed(self.r[rs], 64) < 0 if rt == 0 else signed(self.r[rs], 64) >= 0
                branch = pc + 4 + offset if taken else pc + 8
            elif op == 17 and rs == 8:
                taken = self.condition == bool(rt & 1)
                if rt & 2 and not taken:
                    pc += 8
                    continue
                branch = pc + 4 + offset if taken else pc + 8
            elif op == 0 and word & 63 == 8:
                branch = self.r[rs]
            if branch is not None:
                self.plain(self.load(pc + 4))
                pc = branch
            else:
                try:
                    self.plain(word)
                except AssertionError as error:
                    raise AssertionError(hex(pc), error) from error
                pc += 4
        raise AssertionError(('Original SDK/trail exceeded bounded instruction limit', hex(pc)))

    def plain(self, word):
        op, rs, rt, rd = word >> 26, word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        fn = word & 63
        # EE word instructions sign-extend into64-bit scalar registers.
        # The SDK soft-double packer additionally needs64-bit SLTU masks.
        def word_result(value):
            return signed(value) & 0xffffffffffffffff
        if op == 0 and fn in (0, 2, 3, 4, 6, 7, 33, 35):
            shift = self.r[rs] & 31 if fn in (4, 6, 7) else word >> 6 & 31
            if fn in (0, 4):
                value = self.r[rt] << shift
            elif fn in (2, 6):
                value = (self.r[rt] & 0xffffffff) >> shift
            elif fn in (3, 7):
                value = signed(self.r[rt]) >> shift
            elif fn == 33:
                value = self.r[rs] + self.r[rt]
            else:
                value = self.r[rs] - self.r[rt]
            self.r[rd] = word_result(value)
        elif op == 0 and fn in (42, 43):
            a = signed(self.r[rs], 64) if fn == 42 else self.r[rs] & 0xffffffffffffffff
            b = signed(self.r[rt], 64) if fn == 42 else self.r[rt] & 0xffffffffffffffff
            self.r[rd] = int(a < b)
        elif op in (8, 9):
            self.r[rt] = word_result(self.r[rs] + signed(word & 65535, 16))
        elif op == 10:
            self.r[rt] = int(signed(self.r[rs], 64) < signed(word & 65535, 16))
        elif op == 11:
            self.r[rt] = int((self.r[rs] & 0xffffffffffffffff) <
                             (signed(word & 65535, 16) & 0xffffffffffffffff))
        elif op == 15:
            self.r[rt] = word_result((word & 65535) << 16)
        elif op in (32, 33, 35):
            size = {32: 1, 33: 2, 35: 4}[op]
            address = (self.r[rs] + signed(word & 65535, 16)) & 0xffffffff
            self.r[rt] = signed(self.load(address, size), size * 8) & 0xffffffffffffffff
        elif op == 17 and rs == 0:
            self.r[rt] = word_result(self.f[rd])
        elif op == 0 and fn in (45, 47):
            self.r[rd] = (self.r[rs] + self.r[rt] if fn == 45 else self.r[rs] - self.r[rt]) & 0xffffffffffffffff
        elif op == 0 and fn in (10, 11):
            if (self.r[rt] == 0) == (fn == 10):
                self.r[rd] = self.r[rs]
        elif op == 14:
            self.r[rt] = self.r[rs] ^ (word & 65535)
        elif op == 25:
            self.r[rt] = (self.r[rs] + signed(word & 65535, 16)) & 0xffffffffffffffff
        elif op == 0 and fn in (56, 58, 59, 60, 62, 63):
            shift = (word >> 6 & 31) + (32 if fn >= 60 else 0)
            if fn in (56, 60):
                value = self.r[rt] << shift
            elif fn in (58, 62):
                value = (self.r[rt] & 0xffffffffffffffff) >> shift
            else:
                value = signed(self.r[rt], 64) >> shift
            self.r[rd] = value & 0xffffffffffffffff
        elif op == 17 and rs == 16 and fn in (13, 36):
            # Original scalar configuration truncates cvt.w.s toward zero.
            value = number(self.f[rd])
            self.f[word >> 6 & 31] = int(value) & 0xffffffff
        elif op == 17 and rs == 16 and fn == 29:
            from test_point_light_reference import fp
            product = fp(number(self.f[rd]) * number(self.f[rt]))
            self.f[word >> 6 & 31] = bits(fp(product - self.scalar_accumulator))
        else:
            super().plain(word)
        self.r[0] = 0

    def unary(self, entry, value):
        self.run(entry, floats=(value,))
        return self.f[0]

    def stick(self, x, y):
        self.axes(x, y)
        self.run(0x1b62c0, (0x960000,))
        return self.read(0x960000, 16)

    axes = TrailOriginal.axes
    tick = TrailOriginal.tick


class State(C.Structure):
    _fields_ = [('atan', AtanMath), ('error', C.c_int)]


def build():
    output = ROOT / 'build/item_sdk_math'
    output.mkdir(parents=True, exist_ok=True)
    library = output / ('math.dylib' if sys.platform == 'darwin' else 'math.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
        '-ffp-contract=off', '-fPIC', '-dynamiclib' if sys.platform == 'darwin' else '-shared',
        '-Isrc', 'src/game/em_item_sdk_math.c', 'src/game/em_interaction_scan.c',
        'src/game/em_item_trail.c', '-lm', '-o', str(library)], cwd=ROOT, check=True)
    native = C.CDLL(str(library))
    for name in ('sine', 'cosine', 'sqrt'):
        function = getattr(native, 'em_item_sdk_' + name)
        function.argtypes = [C.c_float]
        function.restype = C.c_float
    native.em_item_sdk_math_bind.argtypes = [C.POINTER(State), C.POINTER(AtanMath), C.POINTER(Math)]
    native.em_item_stick_sample.argtypes = [C.POINTER(Stick), C.c_uint8, C.c_uint8, C.POINTER(Math)]
    native.em_item_trail_step.argtypes = [C.POINTER(Trail), C.POINTER(Stick), C.c_float,
                                        C.c_float, C.POINTER(Math), Emit, C.c_void_p]
    return native, output


def main():
    elf = (ROOT.parent / 'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    assert struct.unpack_from('<I', elf, 0x26c494 - 0x100000 + 0x300)[0] == 0x40490f00
    native, output = build()
    rng = random.Random(0x11e2a8)
    # Kernel boundaries, reduction cancellation and exact signed zero.
    angle_bits = {0, 0x80000000, bits(0.09817477)}
    for threshold in (0x31ffffff, 0x3e999999, 0x3f480000, 0x3f490fd8,
                      0x3fc90fd0, 0x3fc90fdb, 0x4016cbe4, 0x40490f00, 0x40490fdb):
        for delta in range(-8, 9):
            magnitude = threshold + delta
            if magnitude <= 0x40490fdb:
                angle_bits.update((magnitude, magnitude | 0x80000000))
    angle_bits.update(bits(rng.uniform(-3.14159265, 3.14159265)) for _ in range(1200))
    for encoded in sorted(angle_bits):
        value = number(encoded)
        for name, entry in (('sine', 0x11e2a8), ('cosine', 0x11de90)):
            expected = Original(elf).unary(entry, value)
            actual = bits(getattr(native, 'em_item_sdk_' + name)(value))
            assert actual == expected, (name, hex(encoded), value, hex(actual), hex(expected))
    square_bits = {0, 0x80000000, 1, 0x007fffff, 0x00800000, 0x3f800000,
                   0x47000000, 0x7f7fffff}
    square_bits.update(bits(float(x * x + y * y)) for x in range(-128, 128, 13)
                       for y in range(-128, 128, 17))
    square_bits.update(rng.randrange(1, 0x7f800000) for _ in range(600))
    for encoded in sorted(square_bits):
        value = number(encoded)
        expected = Original(elf).unary(0x11e748, value)
        actual = bits(native.em_item_sdk_sqrt(value))
        assert actual == expected, ('sqrt', hex(encoded), hex(actual), hex(expected))
    coefficients = AtanMath.from_buffer_copy(elf[0x26c5d8 - 0x100000 + 0x300:][:76])
    state, workers = State(), Math()
    assert native.em_item_sdk_math_bind(C.byref(state), C.byref(coefficients), C.byref(workers))
    for x in (0.0, -0.0):
        for y in (0.0, -0.0):
            original = Original(elf)
            original.save(0x24295c, 0x950000)
            original.run(0x11e620, floats=(y, x))
            assert original.f[0] == bits(workers.atan2(workers.context, y, x)) == 0
            assert original.load(0x950000) == state.error == 0x21
    axes = {(x, y) for x in (0, 1, 32, 64, 96, 127, 128, 129, 160, 192, 224, 254, 255)
                   for y in (0, 1, 32, 64, 96, 127, 128, 129, 160, 192, 224, 254, 255)}
    axes.update((rng.randrange(256), rng.randrange(256)) for _ in range(1024))
    for x, y in sorted(axes):
        original = Original(elf)
        original.save(0x24295c, 0x950000)
        expected = original.stick(x, y)
        actual = Stick()
        assert native.em_item_stick_sample(C.byref(actual), x, y, C.byref(workers))
        assert bytes(actual) == expected, ('stick', x, y, struct.unpack('<4I', bytes(actual)),
                                           struct.unpack('<4I', expected))
    original, trail = Original(elf), Trail()
    original.save(0x24295c, 0x950000)
    original.save(0x275c90, 0)
    original.calls[0x207d00] = TrailOriginal.check_mode
    frames = [(128, 128)] * 3 + [(0, 0), (255, 0), (255, 255), (0, 255)] * 5 + [(128, 128)] * 20
    triangles = 0
    for x, y in frames:
        expected_state, expected_triangles = original.tick(x, y)
        stick = Stick()
        assert native.em_item_stick_sample(C.byref(stick), x, y, C.byref(workers))
        actual_triangles = []
        emit = Emit(lambda _, xy, intensity: (actual_triangles.append(
            ([xy[i] for i in range(6)], intensity)), 1)[1])
        assert native.em_item_trail_step(C.byref(trail), C.byref(stick), 248, 208,
                                         C.byref(workers), emit, None)
        assert bytes(trail) == expected_state
        assert actual_triangles == expected_triangles
        triangles += len(actual_triangles)
    projection_cases = 0
    for mode in (0, 0x11):
        for flags in (0, 0x80):
            for far in (300.0, 450.0, 600.0):
                original = Original(elf)
                original.save(0x275670, 0x970000)
                original.save(0x9700f8, bits(20))
                original.save(0x9700fc, bits(far))
                original.save(0x810700, mode, 1)
                calls = []
                original.calls[0x1b0070] = lambda o: o.r.__setitem__(2, flags)
                original.calls[0x1d25f0] = lambda o: calls.append(('distance', o.f[12]))
                original.calls[0x21b970] = lambda o: calls.append(('fog', o.f[12], o.f[13]))
                original.run(0x1d2610, floats=(0,))
                assert calls == [('distance', 0x43f02f4f), ('fog', bits(20), bits(far))], calls
                projection_cases += 1
    report = {'sin_cos_values': len(angle_bits), 'sin_cos_exact_results': 2 * len(angle_bits),
              'sqrt_exact_results': len(square_bits), 'numerical_original_bodies': 'PASS',
              'neutral_wrapper_errno_cases': 4, 'full_sdk_stick_cases': len(axes),
              'full_sdk_ring_frames': len(frames), 'full_sdk_triangles': triangles,
              'scope_zero_cases': projection_cases,
              'scope_zero_projection_bits': '43F02F4F',
              'domain': 'finite ITEM angles in [-float(pi),float(pi)]; nonnegative binary32 sqrt',
              'boundaries': 'bounded EE arithmetic model; errno copied into explicit UI context, not process-global state; GS/Metal rasterization'}
    (output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
