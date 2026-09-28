#!/usr/bin/env python3
"""Prove src/game/em_vu_host_lanes.h equal to em_ee_float.h's VU lane
arithmetic for every finite operand pair it is used on.

em_vu_host_lanes.h computes a VU multiply, add, subtract and the VDIV (3, 3)
reciprocal on the host FPU in the environment em_vu_host_enter sets (round
toward zero, flush-to-zero, denormals-are-zero). The model is
em_ee_float.h's integer arithmetic (em_eei_vu_mul_raw, em_eei_vu_add_raw,
em_eei_vu_sub_raw, em_vu_div_bits (3, 3)). This test compares them bit for
bit over:

1. boundary classes: signed zeros and denormals on either side; products and
   sums whose truncated magnitude lands just below, on and just above the
   smallest normal (FTZ); products and sums at the overflow edge (+-MAX);
   exact cancellation (x - x, the sign of the zero); sums at every exponent
   distance 0..40 (the model's exactness bound is 39) with same and opposite
   signs, including a power-of-two larger operand (the borrow into the next
   binade); reciprocals of zero, denormals, MAX and the smallest normal;
2. a seeded random sweep of finite operands (uniform bit patterns, near
   exponents, cancellation-prone pairs). EM_TEST_FULL=1 runs 40x more.

It also checks that the environment is really in effect: a call OUTSIDE
em_vu_host_enter must differ from the model on a sum where round-to-nearest
and truncation differ, so the test cannot pass vacuously.
"""
import random
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode as RM  # noqa: E402

OUT = ROOT / 'build' / 'vu_host_lanes'

SHIM = r'''
#include <stdio.h>
#include <stdlib.h>
#include "game/em_vu_host_lanes.h"

enum { MUL, ADD, SUB, RCP };

static __attribute__((noinline)) void host_run(const uint32_t *a, const uint32_t *b, const uint8_t *op,
                                               uint32_t *out, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        switch (op[i]) {
        case MUL: out[i] = emvuh_mul(a[i], b[i]); break;
        case ADD: out[i] = emvuh_add(a[i], b[i]); break;
        case SUB: out[i] = emvuh_sub(a[i], b[i]); break;
        default: out[i] = emvuh_rcp(b[i]); break;
        }
    }
}

static uint32_t model(uint8_t op, uint32_t a, uint32_t b)
{
    uint32_t q = 0;
    switch (op) {
    case MUL: return em_eei_vu_mul_raw(a, b);
    case ADD: return em_eei_vu_add_raw(a, b);
    case SUB: return em_eei_vu_sub_raw(a, b);
    default: (void)em_vu_div_bits(EM_EE_ONE, b, 3, 3, &q); return q;
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END);
    const size_t n = (size_t)ftell(f) / 9u;
    fseek(f, 0, SEEK_SET);
    uint32_t *a = malloc(4 * n), *b = malloc(4 * n), *out = malloc(4 * n);
    uint8_t *op = malloc(n);
    for (size_t i = 0; i < n; ++i) {
        unsigned char r[9];
        if (fread(r, 1, 9, f) != 9) return 2;
        memcpy(&a[i], r, 4);
        memcpy(&b[i], r + 4, 4);
        op[i] = r[8];
    }
    fclose(f);
    EmVuHostEnv env;
    em_vu_host_enter(&env);
    host_run(a, b, op, out, n);
    em_vu_host_leave(&env);
    size_t bad = 0;
    for (size_t i = 0; i < n; ++i) {
        const uint32_t m = model(op[i], a[i], b[i]);
        if (m != out[i] && bad++ < 20)
            printf("MISMATCH op %u a %08X b %08X host %08X model %08X\n", op[i], (unsigned)a[i],
                   (unsigned)b[i], (unsigned)out[i], (unsigned)m);
    }
    /* Outside the environment: 1 + 1.5 * 2**-24 rounds up to nearest but
     * truncates to 1. */
    const uint32_t probe_a = 0x3F800000u, probe_b = 0x33C00000u;
    host_run(&probe_a, &probe_b, (const uint8_t[]){ADD}, out, 1);
    const int env_matters = out[0] != model(ADD, probe_a, probe_b);
    printf("cases %zu mismatches %zu host_env %d fallback %d\n", n, bad, env_matters, !EM_VU_HOST_LANES);
    return bad ? 1 : 0;
}
'''

MUL, ADD, SUB, RCP = range(4)
SIGN, MAXF = 0x80000000, 0x7F7FFFFF


def f32(sign, exp, mant):
    return (sign << 31) | (exp << 23) | (mant & 0x7FFFFF)


def boundary_cases():
    cases = []
    zeros = [0, SIGN, 1, SIGN | 1, 0x7FFFFF, SIGN | 0x7FFFFF]      # +-0 and denormals
    normals = [0x3F800000, 0xBF800000, 0x00800000, 0x80800000, MAXF, SIGN | MAXF, 0x3FFFFFFF, 0x4B000001]
    for x in zeros + normals:
        for y in zeros + normals:
            for op in (MUL, ADD, SUB):
                cases.append((x, y, op))
    # products around the smallest normal and the overflow edge
    for ea in range(1, 255):
        for eb in (1, 2, 126, 127, 128, 253, 254):
            if not 100 <= ea + eb <= 160 and ea + eb not in range(120, 135) and not ea + eb >= 250:
                continue
            for ma, mb in ((0, 0), (0x7FFFFF, 0x7FFFFF), (0x400000, 0x7FFFFF), (1, 0x7FFFFE)):
                for sa in (0, 1):
                    cases.append((f32(sa, ea, ma), f32(0, eb, mb), MUL))
    for e in range(1, 255):                    # a * 1 and a * (1 - ulp) at every exponent
        cases.append((f32(0, e, 0x7FFFFF), 0x3F800000, MUL))
        cases.append((f32(1, e, 0), 0x3F7FFFFF, MUL))
    # sums at every exponent distance, both signs, power-of-two larger operand
    for ea in (1, 2, 24, 25, 40, 100, 127, 150, 200, 253, 254):
        for d in range(0, 42):
            eb = ea - d
            if eb < 1:
                continue
            for ma in (0, 1, 0x7FFFFF, 0x400000):
                for mb in (0, 1, 0x7FFFFF, 0x2AAAAA):
                    for sb in (0, 1):
                        a, b = f32(0, ea, ma), f32(sb, eb, mb)
                        cases += [(a, b, ADD), (b, a, ADD), (a, b, SUB), (b | SIGN, a, SUB)]
    for x in (0x3F800000, 0x00800000, 0x00800001, MAXF, 0x40490FDB):
        cases += [(x, x, SUB), (x, x ^ SIGN, ADD), (x ^ SIGN, x ^ SIGN, SUB)]
    # reciprocals
    for b in zeros + normals + [0x7E800000, 0x7EFFFFFF, 0x00800001, 0x3F800001, 0x3FFFFFFF, 0xC0000000]:
        cases.append((0x3F800000, b, RCP))
    for e in range(1, 255):
        for m in (0, 1, 0x7FFFFF, 0x555555):
            cases.append((0x3F800000, f32(e & 1, e, m), RCP))
    return cases


def random_cases(count, seed):
    rng = random.Random(seed)
    out = []

    def finite():
        k = rng.random()
        if k < 0.1:
            return rng.choice([0, SIGN, 1, SIGN | 0x7FFFFF, 0x00800000, MAXF, SIGN | MAXF])
        if k < 0.6:
            return f32(rng.getrandbits(1), rng.randint(0x60, 0x9F), rng.getrandbits(23))
        return f32(rng.getrandbits(1), rng.randint(0, 254), rng.getrandbits(23))
    for _ in range(count):
        op = rng.randrange(4)
        a = finite()
        if rng.random() < 0.4:                 # near exponents / cancellation
            b = f32(rng.getrandbits(1), max(0, min(254, ((a >> 23) & 0xFF) + rng.randint(-3, 3))),
                    (a + rng.randint(-8, 8)) & 0x7FFFFF if rng.random() < 0.5 else rng.getrandbits(23))
        else:
            b = finite()
        out.append((a, b, op))
    return out


def main():
    t0 = time.time()
    OUT.mkdir(parents=True, exist_ok=True)
    src, exe = OUT / 'shim.c', OUT / 'shim'
    src.write_text(SHIM)
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Isrc', str(src), '-o', str(exe)],
                   cwd=ROOT, check=True)
    bound = boundary_cases()
    rand = random_cases(RM.pick(8_000_000, 400_000), 0x5EED)
    cases = bound + rand
    data = OUT / 'cases.bin'
    with open(data, 'wb') as f:
        for a, b, op in cases:
            f.write(struct.pack('<IIB', a, b, op))
    r = subprocess.run([str(exe), str(data)], capture_output=True, text=True)
    print(r.stdout.strip())
    assert r.returncode == 0, 'host lanes differ from em_ee_float.h'
    words = r.stdout.strip().split('\n')[-1].split()
    fields = dict(zip(words[0::2], words[1::2]))
    if fields.get('fallback') == '0':
        assert fields.get('host_env') == '1', 'the host environment did not change a result: not in effect'
    RM.banner(f'{len(bound):,} boundary + {len(rand):,} random lane operations')
    print(f'PASS: em_vu_host_lanes.h equals em_ee_float.h on every case ({time.time() - t0:.1f}s)')
    data.unlink()


if __name__ == '__main__':
    main()
