#!/usr/bin/env python3
"""Original1B62C0/20AC70/1D66A0 arithmetic, ring and fixed-point packet proof.

SDK sin/cos/atan2/sqrt and float-to-int are explicit worker boundaries.
The same finite libm values enter both runners; this is not a claim that
host libm reproduces every original SDK bit or its errno side effects.
"""
import ctypes as C
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

from test_camera_rotation_reference import Original as Base
from test_point_light_reference import bits, number, fp

ROOT = Path(__file__).resolve().parents[1]
BASE, OUTPUT, CONTEXT, PACKET = 0x900000, 0x901000, 0x902000, 0xA00000
HOST = C.CDLL(None)
for name, arity in [('sinf', 1), ('cosf', 1), ('sqrtf', 1), ('atan2f', 2)]:
    function = getattr(HOST, name)
    function.argtypes = [C.c_float] * arity
    function.restype = C.c_float


class Stick(C.Structure):
    _fields_ = [(name, C.c_float) for name in ('x', 'y', 'magnitude', 'angle')]


class Trail(C.Structure):
    _fields_ = [('slots', Stick * 16), ('cursor', C.c_uint32)]


Unary = C.CFUNCTYPE(C.c_float, C.c_void_p, C.c_float)
Atan = C.CFUNCTYPE(C.c_float, C.c_void_p, C.c_float, C.c_float)
Emit = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_int32), C.c_uint)


class Math(C.Structure):
    _fields_ = [('context', C.c_void_p), ('sine', Unary), ('cosine', Unary),
                ('atan2', Atan), ('sqrt', Unary)]


class Original(Base):
    def __init__(self, elf):
        super().__init__(elf)
        self.fpu_acc = 0.0
        self.calls = {
            0x11E2A8: lambda o: o.f.__setitem__(0, bits(HOST.sinf(number(o.f[12])))),
            0x11DE90: lambda o: o.f.__setitem__(0, bits(HOST.cosf(number(o.f[12])))),
            0x11E748: lambda o: o.f.__setitem__(0, bits(HOST.sqrtf(number(o.f[12])))),
            0x11E620: lambda o: o.f.__setitem__(0, bits(HOST.atan2f(number(o.f[12]), number(o.f[13])))),
            0x1281C0: lambda o: o.r.__setitem__(2, int(number(o.f[12])) & 0xFFFFFFFF),
            0x207D00: lambda o: self.check_mode(o),
        }
        self.save(0x275C90, 0)

    @staticmethod
    def check_mode(original):
        assert original.r[4:6] == [1, 1]

    def plain(self, word):
        op, rs = word >> 26, word >> 21 & 31
        if op == 0 and word & 63 == 60:
            self.r[word >> 11 & 31] = self.r[word >> 16 & 31] << ((word >> 6 & 31) + 32) & 0xFFFFFFFFFFFFFFFF
        elif op == 17 and rs == 16 and word & 63 in (26, 28, 29):
            x, y = number(self.f[word >> 11 & 31]), number(self.f[word >> 16 & 31])
            operation = word & 63
            if operation == 26:
                self.fpu_acc = fp(x * y)
            else:
                value = fp(self.fpu_acc + fp(x * y)) if operation == 28 else fp(fp(x * y) - self.fpu_acc)
                self.f[word >> 6 & 31] = bits(value)
        else:
            super().plain(word)

    def axes(self, x, y):
        self.save(0x810E64, x, 1)
        self.save(0x810E65, y, 1)

    def stick(self, x, y):
        self.axes(x, y)
        self.run(0x1B62C0, (OUTPUT,))
        return self.read(OUTPUT, 16)

    def tick(self, x, y):
        self.axes(x, y)
        self.write(BASE, struct.pack('<2f', 248, 208))
        self.save(0x275670, CONTEXT)
        self.save(CONTEXT + 0x14, PACKET)
        self.run(0x20AC70, (0, BASE, 0))
        assert self.load(CONTEXT + 0x14) == PACKET + 16 * 0x870
        triangles = []
        for slot in range(16):
            packet = PACKET + slot * 0x870
            tag = self.load(packet + 0x20, 8)
            assert (tag >> 47 & 0x7FF) == 0x4C  # untextured additive Gouraud strip
            previous = None
            for vertex in range(33):
                record = packet + 0x30 + vertex * 0x40
                color = [self.load(record + i * 4) for i in range(3)]
                assert color[0] == color[1] == color[2]
                assert self.read(record + 0x20, 12) == bytes(12)
                center = [self.load(record + 0x10), self.load(record + 0x14)]
                outer = [self.load(record + 0x30), self.load(record + 0x34)]
                if previous is not None:
                    triangles.append((center + previous + outer, color[0]))
                previous = outer
        state = self.read(0x821300, 256) + self.read(0x275C90, 4)
        return state, triangles


def main():
    elf = (ROOT.parent / 'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
    out = ROOT / 'build/item_trail_reference'
    out.mkdir(parents=True, exist_ok=True)
    library = out / ('trail.dylib' if sys.platform == 'darwin' else 'trail.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
        '-fPIC', '-dynamiclib' if sys.platform == 'darwin' else '-shared', '-Isrc',
        'src/game/em_item_trail.c', '-lm', '-o', str(library)], cwd=ROOT, check=True)
    native = C.CDLL(str(library))
    native.em_item_stick_sample.argtypes = [C.POINTER(Stick), C.c_uint8, C.c_uint8, C.POINTER(Math)]
    native.em_item_trail_step.argtypes = [C.POINTER(Trail), C.POINTER(Stick), C.c_float,
                                        C.c_float, C.POINTER(Math), Emit, C.c_void_p]
    math = Math(None, Unary(lambda _, x: HOST.sinf(x)), Unary(lambda _, x: HOST.cosf(x)),
                Atan(lambda _, y, x: HOST.atan2f(y, x)), Unary(lambda _, x: HOST.sqrtf(x)))
    axes = [(x, y) for x in (0, 1, 32, 64, 96, 127, 128, 129, 160, 192, 224, 254, 255)
                   for y in (0, 1, 32, 64, 96, 127, 128, 129, 160, 192, 224, 254, 255)]
    for x, y in axes:
        expected = Original(elf).stick(x, y)
        got = Stick()
        assert native.em_item_stick_sample(C.byref(got), x, y, C.byref(math))
        assert bytes(got) == expected, (x, y, struct.unpack('<4f', bytes(got)), struct.unpack('<4f', expected))
    original, trail = Original(elf), Trail()
    frames = [(128, 128)] * 3 + [(0, 0), (255, 0), (255, 255), (0, 255)] * 5 + [(128, 128)] * 20
    checks = 0
    for x, y in frames:
        expected_state, expected_triangles = original.tick(x, y)
        stick = Stick()
        assert native.em_item_stick_sample(C.byref(stick), x, y, C.byref(math))
        actual = []
        emit = Emit(lambda _, xy, intensity: (actual.append(([xy[i] for i in range(6)], intensity)), 1)[1])
        assert native.em_item_trail_step(C.byref(trail), C.byref(stick), 248, 208, C.byref(math), emit, None)
        assert bytes(trail) == expected_state
        assert actual == expected_triangles, next((i, a, b) for i, (a, b) in enumerate(zip(actual, expected_triangles)) if a != b)
        checks += len(actual)
    report = {'normalized_stick_cases': len(axes), 'ring_frames': len(frames),
              'original_fixed16_triangles': checks, 'state_and_packets': 'PASS',
              'boundaries': 'SDK transcendental values and float-to-int; final GS/Metal pixels'}
    (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
