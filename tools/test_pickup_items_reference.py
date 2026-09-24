#!/usr/bin/env python3
"""Compare em_pickup_items_original.c with the original instructions.

The oracle is the EE interpreter of test_owner_services_reference.py (COP1
and VU0 macro mode on tools/ee_float_model.py), extended with div/mfhi. It
executes the ORIGINAL instructions of the pinned SCUS-97112 boot ELF:

  001C40B0                  the inventory worker, over a random image of
                            D_00810C00..D_00810DFF (every byte after the call
                            is compared, so aliasing counts and meters are
                            checked as the original lays them out);
  001F1110, 001F1180 with   the class-7 aura; 001F1180's SDK leaves 001028D0,
  001028D0/00102760/        00102760 and 00102738 run as original code.
  00102738

Calls leaving that set are answered by scripts the native workers answer
identically: 00122BB8 (rand) returns scripted values; the draw block's
0011E2A8, 001281C0, 001026A0 and 001F0A60 are recorded. Distinct marker
words planted at every candidate sprite record +0x18 identify the record
the original selected; the recorded 0011E2A8 and 001F0A60 float arguments
must equal pi * timer and pi * angle / 180 of the values the native draw
worker received.

No original instruction bytes, data or disassembly are written by this file.
"""
import ctypes as C
import hashlib
import itertools
import random
import struct
import subprocess
import sys
from pathlib import Path

import ee_float_model as fm
from reference_mode import FULL, banner, part, select
from test_owner_services_reference import EE, M32, ROOT, DECOMP, ELF_SHA

OUT = ROOT / 'build/pickup_items_reference'
REGION, REGION_SIZE = 0x810C00, 0x200
ACTOR = 0x7B0000
PI, F180 = 0x40490FDB, 0x43340000


class Oracle(EE):
    """div/mfhi on top of the shared interpreter."""

    def __init__(self, elf):
        super().__init__(elf)
        self.hi = 0

    def plain(self, w, pc):
        if w >> 26 == 0 and w & 63 == 26:        # div (signed, 32-bit)
            rs, rt = w >> 21 & 31, w >> 16 & 31
            a, b = s32(self.g(rs)), s32(self.g(rt))
            q = abs(a) // abs(b)
            q = q if (a < 0) == (b < 0) else -q
            self.hi = a - q * b
            return
        if w >> 26 == 0 and w & 63 == 16:        # mfhi
            self.set32(w >> 11 & 31, self.hi)
            self.r[0] = 0
            return
        super().plain(w, pc)


def s32(v):
    v &= M32
    return v - (1 << 32) if v >> 31 else v


# ---------------------------------------------------------------- native

AT = C.CFUNCTYPE(C.c_void_p, C.c_void_p, C.c_uint32, C.c_uint32)
RAND = C.CFUNCTYPE(C.c_int32, C.c_void_p)
DRAW = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_uint32, C.c_uint32)


class Aura(C.Structure):
    _fields_ = [('angle', C.c_uint32), ('timer', C.c_uint32), ('variant', C.c_int16),
                ('index', C.c_int16), ('state', C.c_int32)]


class Workers(C.Structure):
    _fields_ = [('ctx', C.c_void_p), ('rand', RAND), ('draw', DRAW)]


def build_library():
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / 'pickup_items.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-shared', '-fPIC', '-I' + str(ROOT / 'src'),
                    str(ROOT / 'src/game/em_pickup_items_original.c'), '-o', str(path)], check=True)
    lib = C.CDLL(str(path))
    lib.em_pickup_items_001C40B0.argtypes = [AT, C.c_void_p, C.c_int32, C.c_int32]
    lib.em_pickup_items_001C40B0.restype = C.c_int
    lib.em_pickup_aura_001F1110.argtypes = [C.POINTER(Aura), C.c_int16, C.POINTER(Workers)]
    lib.em_pickup_aura_001F1110.restype = C.c_int
    lib.em_pickup_aura_001F1180.argtypes = [C.POINTER(Aura), C.POINTER(C.c_float), C.POINTER(C.c_float),
                                           C.c_uint8, C.POINTER(Workers)]
    lib.em_pickup_aura_001F1180.restype = C.c_int
    return lib


# ---------------------------------------------------------------- 001C40B0

def items_case(elf, lib, a0, a1, image):
    o = Oracle(elf)
    o.put(REGION, image)
    o.run(0x1C40B0, (a0 & M32, a1 & M32))
    assert o.g(2) & M32 == 0
    written = {a for a in o.written if not (REGION <= a < REGION + REGION_SIZE)}
    assert not written, ('001C40B0 wrote outside the modelled block', sorted(hex(a) for a in written)[:4])
    expected = o.read(REGION, REGION_SIZE)
    buf = C.create_string_buffer(bytes(image), REGION_SIZE)
    base = C.addressof(buf)

    def at(_, address, size):
        if REGION <= address and address + size <= REGION + REGION_SIZE:
            return base + address - REGION
        return None
    result = lib.em_pickup_items_001C40B0(AT(at), None, a0, a1)
    actual = buf.raw
    assert result == 0 and actual == expected, dict(
        a0=hex(a0), a1=a1, result=result,
        diff=[(hex(REGION + i), expected[i], actual[i]) for i in range(REGION_SIZE) if expected[i] != actual[i]][:8])


def items_cases(rng):
    types = list(range(0x21)) + [0x3F, 0x40, 0x43, 0x4E, 0x50, 0x51, 0x54, 0x5C, 0x5F, 0x91, 0xDB, 0xFF]
    amounts = [1, 2, 0x7F, -1, 200]
    cases = []
    for t, n in itertools.product(types, amounts):
        for shape in range(3):
            img = bytearray(rng.randrange(256) for _ in range(REGION_SIZE))
            c64 = 0x810C64 - REGION
            if shape == 1:          # boundaries: counts and packs at the clamps, meters near 99
                img[c64 + t] = rng.choice((0x62, 0x63, 0xFF, 0))
                img[0x63] = rng.choice((0x61, 0x62, 0x63, 0xFF))
                for m in (0xA8, 0xAA, 0xAC, 0xB0):
                    img[m:m + 2] = struct.pack('<h', rng.choice((0x5E, 0x62, 0x63, -3, 0x7FFF)))
                img[0xB2:0xB4] = struct.pack('<h', rng.choice((0, 11, 12, 0x30, -5, 0x7FF0)))
                img[0xB7] = rng.choice((0, 0xB, 0xC, 0x24, 0x30, 0xFF))
                img[0x62] = rng.choice((0, 1))
            elif shape == 2:        # the gate flags of 0xC..0xE
                for f in (0x70, 0x71, 0x72):
                    img[f] = rng.choice((0, 1))
            cases.append((t, n, bytes(img)))
    return cases


def items_null_storage(lib):
    """A byte without storage stops the routine (fail-stop) after the stores
    before it: case 0x10 with no D_00810CB4 keeps the count and pack stores."""
    buf = C.create_string_buffer(REGION_SIZE)
    base = C.addressof(buf)

    def at(_, address, size):
        if address in (0x810CB4, 0x810CB5):
            return None
        return base + address - REGION if REGION <= address < REGION + REGION_SIZE else None
    assert lib.em_pickup_items_001C40B0(AT(at), None, 0x10, 1) == -1
    assert buf.raw[0x810C74 - REGION] == 1 and buf.raw[0x810C63 - REGION] == 1


# ---------------------------------------------------------------- aura

def records():
    out = [0x259DD0 + v * 0x2C for v in range(-1, 8)]
    for base in (0x259EE0, 0x259F90, 0x25A040):
        out += [base + i * 0x2C for i in range(-3, 4)]
    return out


MARK = {r: 0x5A000000 | (r & 0xFFFFF) for r in records()}


def aura_bytes(a):
    return struct.pack('<IIhhi', a.angle, a.timer, a.variant, a.index, a.state)


def aura_case(elf, lib, entry, aura, world, eye, area, rands):
    o = Oracle(elf)
    o.put(ACTOR + 0x2D0, aura_bytes(aura))
    o.put(ACTOR + 0xD0, struct.pack('<16I', *world))
    o.put(0x8105D0, struct.pack('<4I', *eye))
    o.put(0x810700, bytes([area]))
    for r, mark in MARK.items():
        o.put(r + 0x18, struct.pack('<I', mark))
    queue = list(rands)
    expected = []

    def rand(r):
        expected.append(('rand',))
        r.set32(2, queue.pop(0))
    state = {}

    def sine(r):
        state['sin_f12'] = r.f[12]
        r.f[0] = 0x3F000000

    def to_int(r): r.set32(2, 0)

    def mat_vec(r):
        state['record_mark'] = r.load(r.g(6) & M32)   # a2: the record's +0x18 vector

    def draw(r):
        rec = next(k for k, v in MARK.items() if v == state['record_mark'])
        expected.append(('draw', rec, state['sin_f12'], r.f[12]))
    o.calls.update({0x122BB8: rand, 0x11E2A8: sine, 0x1281C0: to_int, 0x1026A0: mat_vec, 0x1F0A60: draw})
    args = (ACTOR, aura.variant & 0xFFFF) if entry == 0x1F1110 else (ACTOR,)
    o.run(entry, args)
    exp_bytes = o.read(ACTOR + 0x2D0, 16)

    queue2 = list(rands)
    actual = []

    def nrand(_):
        actual.append(('rand',))
        return s32(queue2.pop(0))

    def ndraw(_, record, angle, timer):
        actual.append(('draw', record, fm.ee_mul(PI, timer), fm.ee_div(fm.ee_mul(PI, angle), F180)))
        return 0
    workers = Workers(None, RAND(nrand), DRAW(ndraw))
    native = Aura(aura.angle, aura.timer, aura.variant, aura.index, aura.state)
    if entry == 0x1F1110:
        result = lib.em_pickup_aura_001F1110(C.byref(native), aura.variant, C.byref(workers))
    else:
        fw = (C.c_float * 16)(*[struct.unpack('<f', struct.pack('<I', x))[0] for x in world])
        fe = (C.c_float * 4)(*[struct.unpack('<f', struct.pack('<I', x))[0] for x in eye])
        result = lib.em_pickup_aura_001F1180(C.byref(native), fw, fe, area, C.byref(workers))
    assert result == 0 and (aura_bytes(native), actual) == (exp_bytes, expected), dict(
        entry=hex(entry), aura=aura_bytes(aura).hex(), area=area, native=aura_bytes(native).hex(),
        original=exp_bytes.hex(), actual=actual, expected=expected)


def fbits(x): return struct.unpack('<I', struct.pack('<f', x))[0]


def aura_cases(rng):
    timers = [49.0, 1.0, 0.99999994, 1.0000001, 0.95, 0.5, 0.0, -0.5, 0.05, 159.0, 0.9500000476837158]
    angles = [0.0, 126.0, 174.0, 175.0, 176.0, 180.0, -180.0, 179.99998]
    matrices = [
        (-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 231.1, 200.9, 428.0, 1),     # the AREA11 map owner
        (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 231.1, 200.9, 428.0, 1),
        (0, 0, 1, 0, 0, 1, 0, 0, -1, 0, 0, 0, 100.0, 5.0, -40.0, 1),
    ]
    eyes = [(268.2, 258.47372, 182.8, 1), (231.1, 210.0, 500.0, 1), (231.1, 200.9, 428.0, 1)]
    cases = []
    for state, timer, angle, variant, index, m, e, area in itertools.product(
            (0, 1, 2), timers, angles, (0, 1, 2, 3, 4, 5), (0, 3), range(3), range(3), (0x0B, 1, 2)):
        rands = [rng.choice((rng.randrange(1 << 31), 0, 3, 119, 120, 0x7FFFFFFF)) for _ in range(2)]
        world = tuple(fbits(x) for x in matrices[m])
        eye = tuple(fbits(x) for x in eyes[e])
        cases.append((0x1F1180, Aura(fbits(angle), fbits(timer), variant, index, state), world, eye, area, rands))
    for variant, r in itertools.product((0, 1, 2, 4, 5), (0, 1, 119, 120, 239, 0x7FFFFFFF, 12345678)):
        cases.append((0x1F1110, Aura(0x12345678, 0x9ABCDEF0, variant, 7, 3), (0,) * 16, (0,) * 4, 0x0B, [r]))
    return cases


def main():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    lib = build_library()
    rng = random.Random(0x1C40B0)
    items = items_cases(rng)
    run_items = select(items, 120, 1, axes=(lambda c: c[0], lambda c: c[1]))
    for t, n, img in run_items:
        items_case(elf, lib, t, n, img)
    items_null_storage(lib)
    auras = aura_cases(random.Random(0x1F1180))
    run_auras = select(auras, 400, 2, axes=(lambda c: c[0], lambda c: (c[1].state, c[1].variant),
                                             lambda c: c[1].timer, lambda c: c[1].angle, lambda c: c[4]))
    for case in run_auras:
        aura_case(elf, lib, *case)
    banner(part(len(run_items), len(items), '001C40B0 cases'), part(len(run_auras), len(auras), 'aura cases'))
    print('pickup items reference: 001C40B0, 001F1110 and 001F1180 match the original instructions PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
