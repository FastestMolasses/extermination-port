#!/usr/bin/env python3
"""The aim/fire binding's soft-float leaves vs the original instructions.

em_aim_fire_soft_float_call (src/game/em_aim_fire_soft_float.c) is the case
dispatch the binding (em_aim_fire_binding.c) uses for the knife's facing test,
00189EC0 type 0x0E: 001000C0(00128350(t), 0x3FF921FB60000000), pi/2 widened
from single precision. This checks the dispatch itself, through the same
EmAimFireTargetCall the binding receives: f12 in f[0], the two 64-bit double
words in a[0] / a[1], the result in v0.

The oracle is the bounded EE interpreter of
tools/test_sdk_math_original_reference.py (imported, not edited), executing the
user's own boot-ELF code over the captured first-control RAM; each function it
enters is checked byte for byte against ../Extermination/config/SCUS_971.12.

Parts (quick mode samples the random words; EM_TEST_FULL=1 runs more):
 1. 00128350 through the dispatch: the whole 64-bit v0 equals the original's
    for the facing-test edges (below, at and above pi/2, its neighbours,
    zeros, infinities, NaNs) and random float words.
 2. 001000C0 through the dispatch: v0 equals the original's for double-word
    pairs (equal, one-ulp neighbours, opposite signs, NaNs, pairs that differ
    only in the high or only in the low word, random pairs).
 3. The facing test chain, both ways: original 001000C0(original
    00128350(t), pi/2) against the dispatch's, for every part-1 word; the
    edge results (below 1, at 0, above 0) are asserted explicitly.
 4. Argument checks: a call without f12, or without a0 and a1, returns -1;
    any other function returns 1 (left to the binding's other cases).
 5. Negative controls: copies of the dispatch with the comparison flipped,
    with a[0] / a[1] swapped, and with a[0] truncated to its low 32 bits are
    each built and must fail parts 1-3.

Only addresses and values are printed; no original bytes are embedded.
"""
import csv
import ctypes as C
import hashlib
from pathlib import Path
import random
import struct
import subprocess
import sys

import test_sdk_math_original_reference as SM
from reference_mode import banner, pick

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
REF = DECOMP / 'build/startup-reference'
BUILD = ROOT / 'build/aim_fire_soft_float_reference'
SOURCE = ROOT / 'src/game/em_aim_fire_soft_float.c'
MASK32, MASK64 = 0xFFFFFFFF, (1 << 64) - 1
TO_DOUBLE, FACING_COMPARE = 0x128350, 0x1000C0
HALF_PI_FLOAT = 0x3FC90FDB
HALF_PI_DOUBLE = 0x3FF921FB60000000
s64 = SM.s64

class Oracle(SM.Oracle):
    """The shared interpreter plus the quadword save / restore (SQ / LQ)
    that 001000C0's frame uses for its return address. GPRs are modelled to
    64 bits here: SQ writes the low doubleword and a zero high doubleword, LQ
    reads the low doubleword back (only the saved return address goes
    through them on this path)."""

    def decode(self, pc):
        w = self.load(pc, 4)
        op, rs, rt = w >> 26, (w >> 21) & 31, (w >> 16) & 31
        imm = (w & 0xFFFF) - ((w & 0x8000) << 1)
        r = self.r
        if op == 31:
            def sq():
                a = (r[rs] + imm) & ~15
                self.store(a, r[rt], 8)
                self.store(a + 8, 0, 8)
            return ('s', sq)
        if op == 30:
            def lq():
                if rt:
                    r[rt] = s64(self.load((r[rs] + imm) & ~15, 8))
            return ('s', lq)
        return super().decode(pc)


SHIM = r'''
#include <string.h>
#include "game/em_aim_fire_soft_float.h"

int s_call(uint32_t fn, uint64_t a0, uint64_t a1, uint32_t na, uint32_t f12, uint32_t nf, uint64_t *v0)
{
    EmAimFireTargetCall f;
    memset(&f, 0, sizeof f);
    f.function = fn; f.a[0] = a0; f.a[1] = a1; f.na = na; f.f[0] = f12; f.nf = nf;
    f.v0 = UINT64_C(0xDEADBEEFDEADBEEF);
    int rc = em_aim_fire_soft_float_call(&f);
    *v0 = f.v0;
    return rc;
}
int s_null(void) { return em_aim_fire_soft_float_call(NULL); }
'''

MUTATIONS = {
    'flipped comparison': ('em_sdk_soft_float_001274B0(f->a[0], f->a[1]) < 0',
                           'em_sdk_soft_float_001274B0(f->a[0], f->a[1]) >= 0'),
    'swapped a0/a1': ('em_sdk_soft_float_001274B0(f->a[0], f->a[1]) < 0',
                      'em_sdk_soft_float_001274B0(f->a[1], f->a[0]) < 0'),
    'a0 truncated to 32 bits': ('em_sdk_soft_float_001274B0(f->a[0], f->a[1]) < 0',
                                'em_sdk_soft_float_001274B0((uint32_t)f->a[0], f->a[1]) < 0'),
}


def build(tag, source_text):
    BUILD.mkdir(parents=True, exist_ok=True)
    shim, src = BUILD / 'shim.c', BUILD / f'dispatch_{tag}.c'
    shim.write_text(SHIM)
    src.write_text(source_text)
    lib = BUILD / (f'shim_{tag}.dylib' if sys.platform == 'darwin' else f'shim_{tag}.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                    '-ffp-contract=off', '-Isrc', str(shim), str(src), 'src/game/em_sdk_soft_float.c',
                    'src/game/em_sdk_math_original.c', '-o', str(lib)], cwd=ROOT, check=True)
    n = C.CDLL(str(lib))
    n.s_call.argtypes = [C.c_uint32, C.c_uint64, C.c_uint64, C.c_uint32, C.c_uint32, C.c_uint32,
                         C.POINTER(C.c_uint64)]
    n.s_call.restype = C.c_int
    n.s_null.restype = C.c_int
    return n


def native(n, fn, a0=0, a1=0, na=0, f12=0, nf=0):
    v0 = C.c_uint64()
    rc = n.s_call(fn, a0 & MASK64, a1 & MASK64, na, f12 & MASK32, nf, C.byref(v0))
    return rc, v0.value


def float_words(rng):
    words = [HALF_PI_FLOAT - 1, HALF_PI_FLOAT, HALF_PI_FLOAT + 1, 0x00000000, 0x80000000, 0x3F800000,
             0xBF800000, 0x40490FDB, 0xC0490FDB, 0x40C90FDB, 0xBFC90FDB, 0x00000001, 0x007FFFFF,
             0x7F7FFFFF, 0xFF7FFFFF, 0x7F800000, 0xFF800000, 0x7FC00000, 0xFFC00000, 0x7F800001]
    for biased in range(0x70, 0x90):
        for frac in (0, 1, 0x490FDA, 0x490FDB, 0x490FDC, 0x7FFFFF):
            words += [biased << 23 | frac, 0x80000000 | biased << 23 | frac]
    words += [rng.getrandbits(32) for _ in range(pick(20_000, 2_000))]
    return words


def double_pairs(rng):
    base = [0, 1 << 63, HALF_PI_DOUBLE, HALF_PI_DOUBLE + 1, HALF_PI_DOUBLE - 1, 0x3FF0000000000000,
            0xBFF0000000000000, 0x7FF0000000000000, 0xFFF0000000000000, 0x7FF8000000000000,
            0x7FF0000000000001, 1, 0x000FFFFFFFFFFFFF]
    pairs = [(a, b) for a in base for b in base]
    # the high word decides vs only the low word differs (a 32-bit truncation shows here)
    pairs += [(HALF_PI_DOUBLE + (k << 32), HALF_PI_DOUBLE) for k in (-1, 1)]
    pairs += [(0x3FF921FB00000000 | k, HALF_PI_DOUBLE) for k in (0, 0x5FFFFFFF, 0x60000001, 0xFFFFFFFF)]
    pairs += [(rng.getrandbits(64), rng.getrandbits(64)) for _ in range(pick(20_000, 2_000))]
    pairs += [(a, a ^ (1 << rng.randrange(64))) for a in (rng.getrandbits(64) for _ in range(pick(4_000, 400)))]
    return pairs


def check(n, oracle, words, pairs):
    """Returns the list of mismatches (empty when the dispatch equals the original)."""
    misses = []
    widened = {}
    for w in words:
        oracle.run(TO_DOUBLE, floats=(w,))
        want = oracle.r[2] & MASK64
        rc, got = native(n, TO_DOUBLE, f12=w, nf=1)
        widened[w] = want
        if rc != 0 or got != want:
            misses.append(('00128350', hex(w), hex(want), rc, hex(got)))
    for a, b in pairs:
        oracle.run(FACING_COMPARE, args=(s64(a), s64(b)))
        want = oracle.r[2] & MASK64
        rc, got = native(n, FACING_COMPARE, a, b, na=2)
        if rc != 0 or got != want:
            misses.append(('001000C0', hex(a), hex(b), hex(want), rc, hex(got)))
    edges = {HALF_PI_FLOAT - 1: 1, HALF_PI_FLOAT: 0, HALF_PI_FLOAT + 1: 0}
    for w, d in widened.items():
        oracle.run(FACING_COMPARE, args=(s64(d), s64(HALF_PI_DOUBLE)))
        want = oracle.r[2] & MASK64
        rc1, nd = native(n, TO_DOUBLE, f12=w, nf=1)
        rc2, got = native(n, FACING_COMPARE, nd, HALF_PI_DOUBLE, na=2)
        if rc1 or rc2 or got != want or (w in edges and want != edges[w]):
            misses.append(('facing', hex(w), hex(want), hex(got), edges.get(w)))
    return misses


def main():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == SM.ELF_SHA, 'unexpected boot ELF'
    ram = (REF / 'playable_ee.bin').read_bytes()
    with open(DECOMP / 'docs/FUNCTIONS.csv') as fh:
        sizes = {int(row['vram'], 16): int(row['size_bytes']) for row in csv.DictReader(fh)}
    oracle = Oracle(bytearray(ram), elf, sizes)
    rng = random.Random(0x189EC0)
    words, pairs = float_words(rng), double_pairs(rng)
    text = SOURCE.read_text()
    n = build('live', text)

    misses = check(n, oracle, words, pairs)
    assert not misses, ('dispatch differs from the original', len(misses), misses[:6])

    # Part 4: argument checks.
    assert native(n, TO_DOUBLE, f12=HALF_PI_FLOAT, nf=0)[0] == -1
    assert native(n, FACING_COMPARE, HALF_PI_DOUBLE, 0, na=1)[0] == -1
    assert native(n, 0x11DF78, f12=HALF_PI_FLOAT, nf=1) == (1, 0xDEADBEEFDEADBEEF)
    assert native(n, 0x1274B0, 1, 2, na=2)[0] == 1
    assert n.s_null() == -1

    # Part 5: negative controls.
    caught = []
    for tag, (old, new) in MUTATIONS.items():
        assert text.count(old) == 1, ('mutation anchor', tag)
        m = build(tag.replace(' ', '_').replace('/', '_'), text.replace(old, new))
        bad = check(m, oracle, words, pairs)
        assert bad, ('negative control passed', tag)
        caught.append(f'{tag} ({len(bad)} mismatches)')

    banner(f'00128350 {len(words):,} words', f'001000C0 {len(pairs):,} pairs')
    print(f'aim/fire soft-float dispatch: PASS: 00128350 {len(words)} words, 001000C0 {len(pairs)} pairs, facing chain {len(words)} '
          f'words equal to the original instructions (edges: below pi/2 -> 1, at -> 0, above -> 0); '
          f'argument checks 5; negative controls caught: {"; ".join(caught)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
