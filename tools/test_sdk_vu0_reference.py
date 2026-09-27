#!/usr/bin/env python3
"""Execute the original SDK VU0 leaves 001026D0, 00102900 and 00102948 and
compare the one translation of each, src/game/em_sdk_vu0.h (docs/SDK_VU0.md).

The user's pinned ELF supplies every instruction; none are embedded here.
Arithmetic: tools/ee_float_model.py through test_coll_move_reference.FloatEE
(the VU0 macro ops on raw bit patterns).

Each case writes its operands into a guarded window of EE RAM, runs the
original routine, and compares the whole window (the destination and the
guard words around it) with the native leaf over the same bytes. Aliasing is
covered as the callers use it: 001026D0 with dst = a, dst = b and a separate
dst; 00102900 with dst = v; 00102948 with dst = src.

Default run (~1 s): a fixed-seed sample with every special value;
EM_TEST_FULL=1: the exhaustive random sweep.
"""
import ctypes as C
import random
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode  # noqa: E402
import test_player_slide_reference as SR  # noqa: E402
from test_coll_move_reference import FloatEE  # noqa: E402

PRODUCT, SCALE, COPY = 0x1026D0, 0x102900, 0x102948
OUT = ROOT / 'build' / 'sdk_vu0_reference'
U32 = C.c_uint32
WINDOW, SIZE = 0x01F00000, 0x200     # a (0x40), b (0x80), dst (0x100), guards between
A, B, D = WINDOW + 0x40, WINDOW + 0x80, WINDOW + 0x100

SPECIALS = (0x00000000, 0x80000000, 0x3F800000, 0xBF800000, 0x7F7FFFFF, 0xFF7FFFFF, 0x7F800000,
            0xFF800000, 0x7FC00000, 0x00000001, 0x00800000, 0x807FFFFF, 0x3F7FFFFF)


# The header is header-only (static inline); this bridge exports each leaf
# unchanged under its own name for ctypes.
BRIDGE = """#include "game/em_sdk_vu0.h"
int vu0_001026D0(uint32_t *d, const uint32_t *a, const uint32_t *b) { return em_sdk_vu0_001026D0(d, a, b); }
int vu0_00102900(uint32_t *d, const uint32_t *v, uint32_t s) { return em_sdk_vu0_00102900(d, v, s); }
void vu0_00102948(void *d, const void *s) { em_sdk_vu0_00102948(d, s); }
"""


class Leaves:
    def __init__(self, lib):
        self.em_sdk_vu0_001026D0 = lib.vu0_001026D0
        self.em_sdk_vu0_00102900 = lib.vu0_00102900
        self.em_sdk_vu0_00102948 = lib.vu0_00102948


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    source = OUT / 'bridge.c'
    source.write_text(BRIDGE)
    lib = OUT / ('sdk_vu0' + ('.dylib' if sys.platform == 'darwin' else '.so'))
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared',
                    '-fPIC', '-Isrc', str(source), '-o', str(lib)], cwd=ROOT, check=True)
    n = C.CDLL(str(lib))
    n.vu0_001026D0.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p]
    n.vu0_00102900.argtypes = [C.c_void_p, C.c_void_p, U32]
    n.vu0_00102948.argtypes = [C.c_void_p, C.c_void_p]
    n.vu0_00102948.restype = None
    return Leaves(n)


def word(rng):
    if rng.random() < 0.15:
        return rng.choice(SPECIALS)
    return struct.unpack('<I', struct.pack('<f', rng.uniform(-1e3, 1e3)))[0]


def window(rng):
    return bytearray(struct.pack(f'<{SIZE // 4}I', *(rng.getrandbits(32) for _ in range(SIZE // 4))))


def compare(label, ee, before, native_call):
    """ee has run the original over `before`; run the native over a copy."""
    want = ee.read(WINDOW, SIZE)
    buf = (C.c_ubyte * SIZE).from_buffer_copy(before)
    status = native_call(C.addressof(buf))
    assert status in (None, 0), (label, 'native refused', status)
    got = bytes(buf)
    assert got == want, (label, [(i, want[i:i + 4].hex(), got[i:i + 4].hex())
                                 for i in range(0, SIZE, 4) if want[i:i + 4] != got[i:i + 4]][:6])


def case_copy(ee, n, rng, alias):
    before = window(rng)
    ee.write(WINDOW, bytes(before))
    target = (D, A)[alias]
    ee.invoke(COPY, (target, A))
    off = lambda address: address - WINDOW
    compare(f'00102948 alias {alias}', ee, before,
            lambda base: n.em_sdk_vu0_00102948(base + off(target), base + off(A)))


def main():
    n = build()
    ee = FloatEE(SR.read_elf())
    rng = random.Random(0x1026D0)
    counts = {'001026D0': 0, '00102900': 0, '00102948': 0}
    # Every special value in every lane of a and b at least once.
    for i, special in enumerate(SPECIALS):
        for lane in range(16):
            before = window(rng)
            words = [word(rng) for _ in range(32)]
            words[lane] = special
            words[16 + (lane * 7 + i) % 16] = special
            before[0x40:0xC0] = struct.pack('<32I', *words)
            ee.write(WINDOW, bytes(before))
            ee.invoke(PRODUCT, (D, A, B))
            compare('001026D0 special', ee, before,
                    lambda base: n.em_sdk_vu0_001026D0(base + 0x100, base + 0x40, base + 0x80))
            counts['001026D0'] += 1
        for lane in range(4):
            before = window(rng)
            v = [word(rng) for _ in range(4)]
            v[lane] = special
            before[0x40:0x50] = struct.pack('<4I', *v)
            for s in (special, word(rng)):
                ee.write(WINDOW, bytes(before))
                ee.invoke(SCALE, (D, A), (s,))
                compare('00102900 special', ee, before,
                        lambda base: n.em_sdk_vu0_00102900(base + 0x100, base + 0x40, s))
                counts['00102900'] += 1
    for index in range(reference_mode.pick(3000, 300)):
        before = window(rng)
        before[0x40:0xC0] = struct.pack('<32I', *(word(rng) for _ in range(32)))
        alias = index % 3
        ee.write(WINDOW, bytes(before))
        target = (D, A, B)[alias]
        ee.invoke(PRODUCT, (target, A, B))
        compare(f'001026D0 alias {alias}', ee, before,
                lambda base: n.em_sdk_vu0_001026D0(base + target - WINDOW, base + 0x40, base + 0x80))
        counts['001026D0'] += 1
        before = window(rng)
        before[0x40:0x50] = struct.pack('<4I', *(word(rng) for _ in range(4)))
        s, alias = word(rng), index % 2
        target = (D, A)[alias]
        ee.write(WINDOW, bytes(before))
        ee.invoke(SCALE, (target, A), (s,))
        compare(f'00102900 alias {alias}', ee, before,
                lambda base: n.em_sdk_vu0_00102900(base + target - WINDOW, base + 0x40, s))
        counts['00102900'] += 1
        case_copy(ee, n, rng, index % 2)
        counts['00102948'] += 1
    reference_mode.banner(*('%s %d' % kv for kv in counts.items()))
    print('sdk vu0 reference: PASS (original 001026D0 / 00102900 / 00102948)')


if __name__ == '__main__':
    main()
