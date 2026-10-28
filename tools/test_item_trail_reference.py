#!/usr/bin/env python3
"""Original1B62C0/20AC70/1D66A0 arithmetic, ring and fixed-point packet proof.

SDK sin/cos/atan2/sqrt and float-to-int are explicit worker boundaries.
COP1 (MULA/MADD/MSUB included) runs on the measured EE model in the base
Oracle (tools/ee_cop1.py; MSUB.S is ACC - fs*ft).
The same finite libm values enter both runners; this is not a claim that
host libm reproduces every original SDK bit or its errno side effects.

The GS packets (docs/STATUS_FIELD.md section 3e): every tick's sixteen
001D66A0 packets the original instructions write are also built natively
(em_item_trail_step_fans' fan sink into em_uip_001D66A0, the colour
quadword {c, c, c, 0} and D_00273580's zeros as em_status_gs_page passes
them) over the same fill, and every byte of the area and the cursor must
be equal. Then 001D66A0 alone: random positions, colour words, slots,
cursor offsets and worker results (the sin / cos hooks return the same
random bits to both), every byte of a filled area, the cursor, and no
store of the original outside the area, the cursor and its stack.
"""
import ctypes as C
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

import random

from test_camera_rotation_reference import Original as Base
from test_point_light_reference import STACK, bits, number
import test_ui_packet_reference as U

ROOT = Path(__file__).resolve().parents[1]
BASE, OUTPUT, CONTEXT, PACKET = 0x900000, 0x901000, 0x902000, 0xA00000
FANS = 16 * 0x870                  # one tick's sixteen 001D66A0 packets
FILL = 0xA5
A1, A2, A3 = 0x903000, 0x903010, 0x903020   # 001D66A0's vector arguments (the direct cases)
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
Fan = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_float), C.c_int32, C.c_float, C.POINTER(C.c_float))


class Math(C.Structure):
    _fields_ = [('context', C.c_void_p), ('sine', Unary), ('cosine', Unary),
                ('atan2', Atan), ('sqrt', Unary)]


class Original(Base):
    def __init__(self, elf):
        super().__init__(elf)
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
        self.write(PACKET, bytes([FILL]) * FANS)
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
        self.packets = self.read(PACKET, FANS)
        return state, triangles


class Direct(Original):
    """001D66A0 alone: its sin / cos calls return queued bits."""

    def __init__(self, elf, trig):
        super().__init__(elf)
        queue = list(trig)
        self.asked = []

        def worker(o, which):
            self.asked.append((which, o.f[12]))
            o.f[0] = queue.pop(0)
        self.calls = {0x11E2A8: lambda o: worker(o, 's'), 0x11DE90: lambda o: worker(o, 'c'),
                      0x1281C0: lambda o: o.r.__setitem__(2, U.float_to_int(o.f[12]) & 0xFFFFFFFF)}


def native_fan(lib, area: bytearray, area_at: int, cell: bytearray, slot: int, a1, a2, a3, trig):
    """em_uip_001D66A0 over (the cursor word, the area) by original address."""
    regions = [(CONTEXT + 0x10 + 4 * slot, cell), (area_at, area)]
    keep = [(C.c_char * len(b)).from_buffer(b) for _, b in regions]
    addr = (C.c_uint32 * 2)(*[a for a, _ in regions])
    size = (C.c_uint32 * 2)(*[len(b) for _, b in regions])
    ptrs = (C.c_void_p * 2)(*[C.addressof(k) for k in keep])
    why = C.create_string_buffer(128)
    vec = [(C.c_uint32 * 4)(*v) for v in (a1, a2, a3, trig)]
    rc = lib.uipb_fan(addr, size, ptrs, 2, CONTEXT, U.FAN_TO_INT, slot, *vec, why, 128)
    del keep, ptrs
    return rc, why.value.decode()


def direct_cases(elf, lib, count):
    """001D66A0 against the original instructions: (cases, bytes compared)."""
    r = random.Random(0x1D66A0)
    compared = 0
    for case in range(count):
        slot = r.choice((1, 1, 0, 2, 3))
        off = r.randrange(0, 0x100, 16)
        fl = lambda lo, hi: bits(r.uniform(lo, hi))
        a1 = [fl(-2000, 4000), fl(-2000, 4000), fl(-64, 128), fl(-64, 128)]
        if case % 5 == 0:
            a1 = [bits(v) for v in (2040.0, 2032.0, 28.0 + case % 16, 28.0)]
        a2 = [r.getrandbits(32) for _ in range(4)]
        a3 = [r.getrandbits(32) for _ in range(4)]
        angle = fl(-12.5, 12.5)
        trig = [fl(-1, 1) for _ in range(4)] if case % 3 else [bits(v) for v in (
            HOST.sinf(number(angle)), HOST.cosf(number(angle)), HOST.sinf(0.09817477), HOST.cosf(0.09817477))]
        o = Direct(elf, trig)
        for i, v in enumerate(a1 + a2 + a3):
            o.save(A1 + 4 * i, v)
        o.save(0x275670, CONTEXT)
        o.save(CONTEXT + 0x10 + 4 * slot, PACKET + off)
        o.write(PACKET, bytes([FILL]) * (0x870 + 0x200))
        before = dict(o.mem)
        o.f[12] = angle
        o.run(0x1D66A0, (slot, A1, A2, A3))
        assert [w for w, _ in o.asked] == ['s', 'c', 's', 'c'], o.asked
        assert [f for _, f in o.asked] == [angle, angle, 0x3DC90FDB, 0x3DC90FDB], o.asked
        stray = [a for a in o.mem if o.mem[a] != before.get(a) and not (PACKET <= a < PACKET + 0x870 + 0x200)
                 and not (CONTEXT + 0x10 + 4 * slot <= a < CONTEXT + 0x14 + 4 * slot)
                 and not STACK - 0x100 <= a < STACK]
        assert not stray, ('001D66A0 stored outside its packet, cursor and stack', [hex(a) for a in stray[:4]])
        area = bytearray([FILL]) * (0x870 + 0x200)
        cell = bytearray(struct.pack('<I', PACKET + off))
        rc, why = native_fan(lib, area, PACKET, cell, slot, a1, a2, a3, trig)
        assert rc == 0, (case, why)
        want = o.read(PACKET, len(area))
        assert bytes(area) == want, (case, next(i for i in range(len(area)) if area[i] != want[i]))
        assert struct.unpack('<I', cell)[0] == o.load(CONTEXT + 0x10 + 4 * slot) == PACKET + off + 0x870
        compared += len(area)
    # fail-stop: an unaligned cursor and a failing float_to_int write nothing
    area, cell = bytearray([FILL]) * 0x900, bytearray(struct.pack('<I', PACKET + 8))
    rc, why = native_fan(lib, area, PACKET, cell, 1, [0] * 4, [0] * 4, [0] * 4, [0] * 4)
    assert rc == -1 and 'aligned' in why and area == bytearray([FILL]) * 0x900, why
    cell = bytearray(struct.pack('<I', PACKET))
    rc, why = native_fan(lib, area, PACKET, cell, 1, [0x7F800000] * 4, [0] * 4, [0] * 4, [0] * 4)
    assert rc == -1 and '001281C0' in why and area == bytearray([FILL]) * 0x900 and cell == struct.pack('<I', PACKET)
    return count, compared


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
    native.em_item_trail_step_fans.argtypes = [C.POINTER(Trail), C.POINTER(Stick), C.c_float,
                                             C.c_float, C.POINTER(Math), Emit, Fan, C.c_void_p]
    lib = U.build()
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
    packet_bytes = 0
    for x, y in frames:
        expected_state, expected_triangles = original.tick(x, y)
        stick = Stick()
        assert native.em_item_stick_sample(C.byref(stick), x, y, C.byref(math))
        actual = []
        emit = Emit(lambda _, xy, intensity: (actual.append(([xy[i] for i in range(6)], intensity)), 1)[1])
        # the fans as em_status_gs_page builds them: 001D66A0(1, pos, {c, c, c, 0}, D_00273580, angle)
        area, cell = bytearray([FILL]) * FANS, bytearray(struct.pack('<I', PACKET))
        refusals = []

        def fan(_, pos, colour, angle, trig):
            rc, why = native_fan(lib, area, PACKET, cell, 1, [bits(pos[i]) for i in range(4)],
                                 [colour & 0xFFFFFFFF] * 3 + [0], [0] * 4, [bits(trig[i]) for i in range(4)])
            if rc:
                refusals.append(why)
            return 0 if rc else 1
        assert native.em_item_trail_step_fans(C.byref(trail), C.byref(stick), 248, 208, C.byref(math), emit,
                                              Fan(fan), None), refusals
        assert bytes(area) == original.packets, (x, y, next(i for i in range(FANS) if area[i] != original.packets[i]))
        assert struct.unpack('<I', cell)[0] == PACKET + FANS
        packet_bytes += FANS
        assert bytes(trail) == expected_state
        assert actual == expected_triangles, next((i, a, b) for i, (a, b) in enumerate(zip(actual, expected_triangles)) if a != b)
        checks += len(actual)
    cases, compared = direct_cases(elf, lib, 400)
    report = {'normalized_stick_cases': len(axes), 'ring_frames': len(frames),
              'original_fixed16_triangles': checks, 'state_and_packets': 'PASS',
              'fan_packet_bytes_equal': packet_bytes, 'direct_001D66A0_cases': cases,
              'direct_001D66A0_bytes_equal': compared,
              'boundaries': 'SDK transcendental values and float-to-int; final GS/Metal pixels'}
    (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
