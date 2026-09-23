#!/usr/bin/env python3
"""The boot ELF's SDK float math: em_sdk_math_original vs the original instructions.

The oracle is a bounded EE interpreter written for this test. It executes
the user's own boot-ELF code (every function it enters is checked byte for
byte against ../Extermination/config/SCUS_971.12, with the ranges taken from
../Extermination/docs/FUNCTIONS.csv) over the captured first-control RAM
(../Extermination/build/startup-reference/playable_ee.bin, which supplies
the .data the code reads: D_0026C5D0 = 1 and the errno pointer
D_0024295C). Every COP1 instruction is computed by tools/ee_float_model.py,
the EE FPU model measured in PCSX2 (docs/EE_FLOAT_MODEL.md).

Parts (every part runs in both modes; quick mode samples the bulk sweeps,
EM_TEST_FULL=1 runs them whole):
 1. Leaves: 0011DF78 fabsf, 0011E080 isnanf, 0011DE60 copysignf,
    0011DF98 floorf, 0011E148 scalbnf, 0011CB90 the sqrt kernel.
 2. Kernels: 0011D770 (sine), 0011CCC8 (cosine), 0011D878 (tangent) on
    their own arguments (x, y, iy).
 3. Reduction: 0011C7B0 (y[0], y[1], n) over every branch, and 0011CE20
    directly for prec 0..3.
 4. sinf 0011E2A8, cosf 0011DE90, tanf 0011E398, atanf 0011DBB8, the atan2
    kernel 0011C4C8: branch edges and their float neighbours, signed zeros,
    denormals, infinities, NaNs, multiples of pi/2 (the cancellation paths),
    the script ease arguments, random words and random finite values.
 5. Wrappers 0011E620 (atan2f) and 0011E748 (sqrtf) with their callees
    00128350 / 0011DB90 / 0011FD78 / 00127758 recorded on both sides (the
    ordered calls, the exception record 0011DB90 receives, the errno stores,
    the result), for D_0026C5D0 = -1, 0, 1, 2, 5; and once with every
    callee executed as original instructions, the native workers bound to
    original executions of the same callees.
 6. Fail-stop: every missing table, world cell, worker, failing worker and
    NULL output faults at its original address.
 7. Cross-checks (reported; the ones marked asserted must hold):
    em_area_script_sin_0011E2A8 on its domain (asserted), the director's
    0011DBB8/0011C4C8/0011E620 translations, em_item_sdk_sine/cosine/sqrt
    and em_interaction_sdk_atan2 (reported: host models to be replaced).
 8. Route: the AREA11 script host (test_area_script_reference.capture_case)
    replayed over route beats 07 (truck preview) and 10 (Roger + director)
    with its sine worker bound to em_sdk_math_original_w_0011E2A8; every
    camera eye/target the eases produce must equal the captured frame, and
    every sine argument is also checked against the oracle.

Only addresses and values are printed; no original bytes are embedded.
"""
import csv
import ctypes as C
import hashlib
import math
from pathlib import Path
import random
import struct
import subprocess
import sys

import ee_float_model as M
from reference_mode import FULL, banner, parallel_map, pick

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
REF = DECOMP / 'build/startup-reference'
BUILD = ROOT / 'build/sdk_math_original_reference'
ELF_SHA = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
RETURN = 0x0BADF00C
STACK = 0x01F00000
SCRATCH = 0x01E00000        # exception copies for original callee runs
ERRNO_CELL = 0x01E10000     # the recorded 0011FD78's cell
MASK32, MASK64 = 0xFFFFFFFF, (1 << 64) - 1

SIN, COS, TAN, ATAN, ATAN2 = 0x11E2A8, 0x11DE90, 0x11E398, 0x11DBB8, 0x11C4C8
REM, CE20, KSIN, KCOS, KTAN = 0x11C7B0, 0x11CE20, 0x11D770, 0x11CCC8, 0x11D878
FABS, ISNAN, COPYSIGN, FLOOR, SCALBN, SQRTK = 0x11DF78, 0x11E080, 0x11DE60, 0x11DF98, 0x11E148, 0x11CB90
W_ATAN2, W_SQRT = 0x11E620, 0x11E748
X_TODOUBLE, X_MATHERR, X_ERRNO, X_TOFLOAT = 0x128350, 0x11DB90, 0x11FD78, 0x127758


def s32(v):
    v &= MASK32
    return v - (1 << 32) if v & 0x80000000 else v


def s64(v):
    v &= MASK64
    return v - (1 << 64) if v >> 63 else v


def f2b(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def b2f(b):
    return struct.unpack('<f', struct.pack('<I', b & MASK32))[0]


# ------------------------------------------------------------------ oracle --

class Unsupported(AssertionError):
    pass


class Oracle:
    """Executes original EE code. COP1 goes through ee_float_model; each
    entered function is checked against the ELF before it runs."""

    def __init__(self, mem, elf, sizes):
        self.mem, self.elf, self.sizes = mem, elf, sizes
        self.r, self.f = [0] * 32, [0] * 32
        self.cond = [False]
        self.cache, self.checked = {}, set()
        self.hooks = {}

    # memory
    def load(self, address, size, signed=False):
        address &= MASK32
        assert address + size <= len(self.mem), ('load outside RAM', hex(address))
        return int.from_bytes(self.mem[address:address + size], 'little', signed=signed)

    def store(self, address, value, size):
        address &= MASK32
        assert address + size <= len(self.mem), ('store outside RAM', hex(address))
        self.mem[address:address + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'little')

    def check_function(self, entry):
        if entry in self.checked:
            return
        size = self.sizes.get(entry)
        assert size, ('entered a function missing from FUNCTIONS.csv', hex(entry))
        offset = entry - 0x100000 + 0x300
        assert bytes(self.mem[entry:entry + size]) == self.elf[offset:offset + size], \
            ('RAM code differs from the ELF', hex(entry))
        self.checked.add(entry)

    # decode ---------------------------------------------------------------
    def decode(self, pc):
        w = self.load(pc, 4)
        r, f, cond = self.r, self.f, self.cond
        op, rs, rt, rd, sa, fn = w >> 26, w >> 21 & 31, w >> 16 & 31, w >> 11 & 31, w >> 6 & 31, w & 63
        imm, uimm = s32(w & 0xFFFF if not w & 0x8000 else (w & 0xFFFF) - 0x10000), w & 0xFFFF
        mem = self

        def setr(fnc):
            if rt == 0 and op != 0:
                return ('s', lambda: None)
            return ('s', fnc)

        if op == 0:
            if fn == 8:
                return ('jr', rs)
            if rd == 0 and fn not in (8,):
                return ('s', lambda: None)
            table = {
                0: lambda: r.__setitem__(rd, s32((r[rt] & MASK32) << sa)),
                2: lambda: r.__setitem__(rd, s32((r[rt] & MASK32) >> sa)),
                3: lambda: r.__setitem__(rd, s32(s32(r[rt]) >> sa)),
                4: lambda: r.__setitem__(rd, s32((r[rt] & MASK32) << (r[rs] & 31))),
                6: lambda: r.__setitem__(rd, s32((r[rt] & MASK32) >> (r[rs] & 31))),
                7: lambda: r.__setitem__(rd, s32(s32(r[rt]) >> (r[rs] & 31))),
                10: lambda: r.__setitem__(rd, r[rs]) if r[rt] == 0 else None,
                11: lambda: r.__setitem__(rd, r[rs]) if r[rt] != 0 else None,
                20: lambda: r.__setitem__(rd, s64(r[rt] << (r[rs] & 63))),
                22: lambda: r.__setitem__(rd, s64((r[rt] & MASK64) >> (r[rs] & 63))),
                23: lambda: r.__setitem__(rd, r[rt] >> (r[rs] & 63)),
                33: lambda: r.__setitem__(rd, s32(r[rs] + r[rt])),
                35: lambda: r.__setitem__(rd, s32(r[rs] - r[rt])),
                36: lambda: r.__setitem__(rd, r[rs] & r[rt]),
                37: lambda: r.__setitem__(rd, r[rs] | r[rt]),
                38: lambda: r.__setitem__(rd, r[rs] ^ r[rt]),
                39: lambda: r.__setitem__(rd, s64(~(r[rs] | r[rt]))),
                42: lambda: r.__setitem__(rd, int(r[rs] < r[rt])),
                43: lambda: r.__setitem__(rd, int((r[rs] & MASK64) < (r[rt] & MASK64))),
                45: lambda: r.__setitem__(rd, s64(r[rs] + r[rt])),
                47: lambda: r.__setitem__(rd, s64(r[rs] - r[rt])),
                56: lambda: r.__setitem__(rd, s64(r[rt] << sa)),
                58: lambda: r.__setitem__(rd, s64((r[rt] & MASK64) >> sa)),
                59: lambda: r.__setitem__(rd, r[rt] >> sa),
                60: lambda: r.__setitem__(rd, s64(r[rt] << (sa + 32))),
                62: lambda: r.__setitem__(rd, s64((r[rt] & MASK64) >> (sa + 32))),
                63: lambda: r.__setitem__(rd, r[rt] >> (sa + 32)),
            }
            if fn not in table:
                raise Unsupported(('SPECIAL', fn, hex(pc)))
            return ('s', table[fn])
        if op == 1:
            if rt not in (0, 1, 2, 3):
                raise Unsupported(('REGIMM', rt, hex(pc)))
            return ('b', (lambda: r[rs] < 0) if rt in (0, 2) else (lambda: r[rs] >= 0),
                    pc + 4 + imm * 4, rt >= 2)
        if op in (2, 3):
            return ('j', ((pc + 4) & 0xF0000000) | (w & 0x3FFFFFF) << 2, op == 3)
        if op in (4, 5, 6, 7, 20, 21, 22, 23):
            base = op & 3 | 4
            test = {4: lambda: r[rs] == r[rt], 5: lambda: r[rs] != r[rt],
                    6: lambda: r[rs] <= 0, 7: lambda: r[rs] > 0}[base]
            return ('b', test, pc + 4 + imm * 4, op >= 20)
        if op == 9:
            return setr(lambda: r.__setitem__(rt, s32(r[rs] + imm)))
        if op == 10:
            return setr(lambda: r.__setitem__(rt, int(r[rs] < imm)))
        if op == 11:
            return setr(lambda: r.__setitem__(rt, int((r[rs] & MASK64) < (imm & MASK64))))
        if op == 12:
            return setr(lambda: r.__setitem__(rt, r[rs] & uimm))
        if op == 13:
            return setr(lambda: r.__setitem__(rt, r[rs] | uimm))
        if op == 14:
            return setr(lambda: r.__setitem__(rt, r[rs] ^ uimm))
        if op == 15:
            return setr(lambda: r.__setitem__(rt, s32(uimm << 16)))
        if op == 25:
            return setr(lambda: r.__setitem__(rt, s64(r[rs] + imm)))
        loads = {32: (1, True), 33: (2, True), 35: (4, True), 36: (1, False), 37: (2, False),
                 39: (4, False), 55: (8, True)}
        if op in loads:
            size, signed = loads[op]
            return setr(lambda: r.__setitem__(rt, mem.load(r[rs] + imm, size, signed)))
        stores = {40: 1, 41: 2, 43: 4, 63: 8}
        if op in stores:
            size = stores[op]
            return ('s', lambda: mem.store(r[rs] + imm, r[rt], size))
        if op == 49:
            return ('s', lambda: f.__setitem__(rt, mem.load(r[rs] + imm, 4)))
        if op == 57:
            return ('s', lambda: mem.store(r[rs] + imm, f[rt], 4))
        if op == 17:
            fs, fd = rd, sa
            if rs == 0:
                return setr(lambda: r.__setitem__(rt, s32(f[fs])))
            if rs == 4:
                return ('s', lambda: f.__setitem__(fs, r[rt] & MASK32))
            if rs == 8:
                likely, want = bool(rt & 2), bool(rt & 1)
                return ('b', lambda: cond[0] == want, pc + 4 + imm * 4, likely)
            if rs == 16:
                two = {0: M.ee_add, 1: M.ee_sub, 2: M.ee_mul, 3: M.ee_div}
                if fn in two:
                    g = two[fn]
                    return ('s', lambda: f.__setitem__(fd, g(f[fs], f[rt])))
                if fn == 6:
                    return ('s', lambda: f.__setitem__(fd, M.ee_mov(f[fs])))
                if fn == 7:
                    return ('s', lambda: f.__setitem__(fd, M.ee_neg(f[fs])))
                if fn == 36:
                    return ('s', lambda: f.__setitem__(fd, M.ee_cvt_w_s(f[fs])))
                cmp = {50: M.ee_c_eq, 52: M.ee_c_lt, 54: M.ee_c_le}
                if fn in cmp:
                    g = cmp[fn]
                    return ('s', lambda: cond.__setitem__(0, bool(g(f[fs], f[rt]))))
            if rs == 20 and fn == 32:
                return ('s', lambda: f.__setitem__(fd, M.ee_cvt_s_w(f[fs])))
            raise Unsupported(('COP1', rs, fn, hex(pc)))
        raise Unsupported(('opcode', op, hex(pc)))

    def step_simple(self, pc):
        ins = self.cache.get(pc)
        if ins is None:
            ins = self.cache[pc] = self.decode(pc)
        assert ins[0] == 's', ('control transfer in a delay slot', hex(pc))
        ins[1]()

    # run ------------------------------------------------------------------
    def run(self, entry, args=(), floats=(), limit=4_000_000):
        r, f, cache = self.r, self.f, self.cache
        for i in range(32):
            r[i] = 0
        r[28], r[29], r[31] = 0x27D370, STACK, RETURN
        for i, value in enumerate(args):
            r[4 + i] = value
        for i, value in enumerate(floats):
            f[12 + i] = value & MASK32
        self.check_function(entry)
        pc, steps = entry, 0
        while pc != RETURN:
            steps += 1
            assert steps < limit, ('original routine did not return', hex(entry))
            ins = cache.get(pc)
            if ins is None:
                ins = cache[pc] = self.decode(pc)
            kind = ins[0]
            if kind == 's':
                ins[1]()
                pc += 4
            elif kind == 'b':
                taken = ins[1]()
                if taken:
                    self.step_simple(pc + 4)
                    pc = ins[2]
                elif ins[3]:
                    pc += 8
                else:
                    self.step_simple(pc + 4)
                    pc += 8
            elif kind == 'j':
                target, link = ins[1], ins[2]
                if link:
                    r[31] = pc + 8
                self.step_simple(pc + 4)
                if link and target in self.hooks:
                    self.hooks[target](self)
                    pc += 8
                else:
                    if link:
                        self.check_function(target)
                    pc = target
            else:
                target = r[ins[1]] & MASK32
                self.step_simple(pc + 4)
                pc = target
            r[0] = 0
        return self

    def call(self, entry, args=(), floats=()):
        self.run(entry, args, floats)
        return self


# Globals set in main() before any fork (parallel_map).
ELF = RAM = SIZES = NATIVE = None


def oracle():
    return Oracle(bytearray(RAM), ELF, SIZES)


_ORACLE = None


def shared_oracle():
    """One oracle (and one RAM copy) per worker process."""
    global _ORACLE
    if _ORACLE is None:
        _ORACLE = oracle()
    _ORACLE.hooks = {}
    return _ORACLE


# ------------------------------------------------------------------ native --

SHIM = r'''
#include <string.h>
#include "game/em_sdk_math_original.h"
#include "game/em_director_original.h"
#include "game/em_item_sdk_math.h"
#include "game/em_interaction_scan.h"
#include "game/em_area_script.h"

static EmSdkMathTables T;
static EmDirectorAtanTables D;
static EmInteractionMath I;
static uint32_t fb(float f) { uint32_t b; memcpy(&b, &f, 4); return b; }
static float bf(uint32_t b) { float f; memcpy(&f, &b, 4); return f; }

int shim_load(const uint8_t *elf, size_t size)
{
    if (em_sdk_math_original_load_tables(elf, size, &T) < 0) return -1;
    if (em_director_original_load_atan_tables(elf, size, &D) < 0) return -2;
    for (int i = 0; i < 4; ++i) { I.atan_high[i] = D.hi[i]; I.atan_low[i] = D.lo[i]; }
    for (int i = 0; i < 11; ++i) I.atan_coefficients[i] = D.aT[i];
    return 0;
}

/* which: 0 sin 1 cos 2 tan 3 atan 4 fabs 5 floor 6 sqrt kernel 7 isnan */
int shim_unary(int which, uint32_t x, uint32_t *out, uint32_t *fault, int no_tables)
{
    const EmSdkMathTables *t = no_tables ? NULL : &T;
    float r = 0.0f;
    int rc = 0;
    *fault = 0;
    switch (which) {
    case 0: rc = em_sdk_math_original_0011E2A8(t, bf(x), &r, fault); break;
    case 1: rc = em_sdk_math_original_0011DE90(t, bf(x), &r, fault); break;
    case 2: rc = em_sdk_math_original_0011E398(t, bf(x), &r, fault); break;
    case 3: rc = em_sdk_math_original_0011DBB8(t, bf(x), &r, fault); break;
    case 4: r = em_sdk_math_original_0011DF78(bf(x)); break;
    case 5: r = em_sdk_math_original_0011DF98(bf(x)); break;
    case 6: r = em_sdk_math_original_0011CB90(bf(x)); break;
    default: *out = (uint32_t)em_sdk_math_original_0011E080(bf(x)); return 0;
    }
    *out = fb(r);
    return rc;
}

/* which: 0 atan2 kernel (a=y, b=x) 1 sine kernel 2 cosine kernel 3 tangent
 * kernel 4 scalbnf (a, i) 5 copysignf */
int shim_binary(int which, uint32_t a, uint32_t b, int32_t i, uint32_t *out, uint32_t *fault,
                int no_tables)
{
    const EmSdkMathTables *t = no_tables ? NULL : &T;
    float r = 0.0f;
    int rc = 0;
    *fault = 0;
    switch (which) {
    case 0: rc = em_sdk_math_original_0011C4C8(t, bf(a), bf(b), &r, fault); break;
    case 1: r = em_sdk_math_original_0011D770(bf(a), bf(b), i); break;
    case 2: r = em_sdk_math_original_0011CCC8(bf(a), bf(b)); break;
    case 3: rc = em_sdk_math_original_0011D878(t, bf(a), bf(b), i, &r, fault); break;
    case 4: r = em_sdk_math_original_0011E148(bf(a), i); break;
    default: r = em_sdk_math_original_0011DE60(bf(a), bf(b)); break;
    }
    *out = fb(r);
    return rc;
}

int shim_rem(uint32_t x, uint32_t *y, int32_t *n, uint32_t *fault, int no_tables)
{
    float v[2] = {0, 0};
    *fault = 0;
    int rc = em_sdk_math_original_0011C7B0(no_tables ? NULL : &T, bf(x), v, n, fault);
    y[0] = fb(v[0]); y[1] = fb(v[1]);
    return rc;
}

/* Replaces init_jk[3] (D_0026C538 + 12) in the native tables; returns the old
 * value. Only the patched 0011CE20 cases use it, and they restore it. */
int32_t shim_set_init_jk3(int32_t value)
{
    const int32_t old = T.init_jk[3];
    T.init_jk[3] = value;
    return old;
}

int shim_ce20(const uint32_t *x, uint32_t *y, int32_t e0, int32_t nx, int32_t prec,
              int32_t *n, uint32_t *fault)
{
    float xs[20], ys[3] = {0, 0, 0};
    for (int i = 0; i < nx && i < 20; ++i) xs[i] = bf(x[i]);
    *fault = 0;
    int rc = em_sdk_math_original_0011CE20(&T, xs, ys, e0, nx, prec, T.two_over_pi,
                                           EM_SDK_MATH_TWO_OVER_PI_COUNT, n, fault);
    for (int i = 0; i < 3; ++i) y[i] = fb(ys[i]);
    return rc;
}

/* Wrapper workers as uint32/uint64 trampolines (no host float conversion). */
typedef int (*Pd)(uint32_t x, uint64_t *out);
typedef int (*Pm)(EmSdkMathException *e, int32_t *r);
typedef int (*Pe)(int32_t **cell);
typedef int (*Pf)(uint64_t v, uint32_t *out);
static Pd pd; static Pm pm; static Pe pe; static Pf pf;
static int t_d(void *c, float x, uint64_t *o) { (void)c; return pd(fb(x), o); }
static int t_m(void *c, EmSdkMathException *e, int32_t *r) { (void)c; return pm(e, r); }
static int t_e(void *c, int32_t **cell) { (void)c; return pe(cell); }
static int t_f(void *c, uint64_t v, float *o) { (void)c; uint32_t b; int rc = pf(v, &b); *o = bf(b); return rc; }

/* drop: 1 tables, 2 world, 4 world cell, 8 w_00128350, 16 w_0011DB90,
 * 32 w_0011FD78, 64 w_00127758, 128 workers struct, 256 result */
int shim_wrap(int which, uint32_t a, uint32_t b, const int32_t *mode, Pd d, Pm m, Pe e, Pf f,
              unsigned drop, uint32_t *out, uint32_t *fault)
{
    pd = d; pm = m; pe = e; pf = f;
    EmSdkMathWorld world = { drop & 4 ? NULL : mode };
    EmSdkMathWorkers k = { NULL, drop & 8 ? NULL : t_d, drop & 16 ? NULL : t_m,
                           drop & 32 ? NULL : t_e, drop & 64 ? NULL : t_f };
    const EmSdkMathTables *t = drop & 1 ? NULL : &T;
    const EmSdkMathWorld *w = drop & 2 ? NULL : &world;
    const EmSdkMathWorkers *kk = drop & 128 ? NULL : &k;
    float r = 0.0f;
    float *rp = drop & 256 ? NULL : &r;
    *fault = 0;
    int rc = which == 0 ? em_sdk_math_original_0011E620(t, w, kk, bf(a), bf(b), rp, fault)
                        : em_sdk_math_original_0011E748(t, w, kk, bf(a), rp, fault);
    *out = fb(r);
    return rc;
}

/* Adapter forms (EmSdkMathContext). which: 0 sin 1 cos 2 tan 3 atan 4 atan2 5 sqrt 6 w-sin 7 w-cos */
int shim_adapter(int which, uint32_t a, uint32_t b, int no_tables, const int32_t *mode, uint32_t *out,
                 uint32_t *fault)
{
    EmSdkMathContext c;
    memset(&c, 0, sizeof c);
    c.tables = no_tables ? NULL : &T;
    c.world.d26C5D0 = mode;
    float r = 0.0f;
    int rc = 0;
    switch (which) {
    case 0: r = em_sdk_math_original_float_0011E2A8(&c, bf(a)); break;
    case 1: r = em_sdk_math_original_float_0011DE90(&c, bf(a)); break;
    case 2: r = em_sdk_math_original_float_0011E398(&c, bf(a)); break;
    case 3: r = em_sdk_math_original_float_0011DBB8(&c, bf(a)); break;
    case 4: r = em_sdk_math_original_float_0011E620(&c, bf(a), bf(b)); break;
    case 5: r = em_sdk_math_original_float_0011E748(&c, bf(a)); break;
    case 6: rc = em_sdk_math_original_w_0011E2A8(&c, bf(a), &r); break;
    default: rc = em_sdk_math_original_w_0011DE90(&c, bf(a), &r); break;
    }
    *out = fb(r);
    *fault = c.fault;
    return rc;
}

/* Cross-checks: 0 area-script sine, 1 director atan, 2 director atan2
 * kernel, 3 director atan2 wrapper, 4 item sine, 5 item cosine, 6 item sqrt,
 * 7 interaction atan2. Returns the host model's rc. */
int shim_cross(int which, uint32_t a, uint32_t b, uint32_t *out)
{
    float r = 0.0f;
    int rc = 0;
    switch (which) {
    case 0: rc = em_area_script_sin_0011E2A8(bf(a), &r); break;
    case 1: rc = em_director_original_0011DBB8(&D, bf(a), &r); break;
    case 2: rc = em_director_original_0011C4C8(&D, bf(a), bf(b), &r); break;
    case 3: rc = em_director_original_0011E620(&D, bf(a), bf(b), &r); break;
    case 4: r = em_item_sdk_sine(bf(a)); break;
    case 5: r = em_item_sdk_cosine(bf(a)); break;
    case 6: r = em_item_sdk_sqrt(bf(a)); break;
    default: r = em_interaction_sdk_atan2(&I, bf(a), bf(b)); break;
    }
    *out = fb(r);
    return rc;
}

/* The worker the route replay binds (float API, EmSdkMathContext). */
static EmSdkMathContext route_ctx;
int shim_route_sine(float x, float *out)
{
    route_ctx.tables = &T;
    return em_sdk_math_original_w_0011E2A8(&route_ctx, x, out);
}
'''

CROSS_SOURCES = ['src/game/em_director_original.c', 'src/game/em_item_sdk_math.c',
                 'src/game/em_interaction_scan.c', 'src/game/em_item_trail.c',
                 'src/game/em_area_script.c', 'src/game/em_script.c',
                 'src/game/em_message_service.c', 'src/game/em_interaction_frame.c',
                 'src/game/em_interaction_cinematic.c', 'src/game/em_cinematic_playback.c',
                 'src/game/em_cinematic_camera.c', 'src/game/em_camera_rotation.c',
                 'src/game/em_fan_original.c']

U32P, I32P = C.POINTER(C.c_uint32), C.POINTER(C.c_int32)


class Exception_(C.Structure):
    _fields_ = [('type', C.c_int32), ('name', C.c_uint32), ('arg1', C.c_uint64),
                ('arg2', C.c_uint64), ('retval', C.c_uint64), ('err', C.c_int32)]


PD = C.CFUNCTYPE(C.c_int, C.c_uint32, C.POINTER(C.c_uint64))
PM = C.CFUNCTYPE(C.c_int, C.POINTER(Exception_), I32P)
PE = C.CFUNCTYPE(C.c_int, C.POINTER(I32P))
PF = C.CFUNCTYPE(C.c_int, C.c_uint64, U32P)


def build_native():
    BUILD.mkdir(parents=True, exist_ok=True)
    source = BUILD / 'shim.c'
    source.write_text(SHIM)
    lib = BUILD / ('shim.dylib' if sys.platform == 'darwin' else 'shim.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                    '-ffp-contract=off', '-Isrc', str(source), 'src/game/em_sdk_math_original.c',
                    *CROSS_SOURCES, '-lm', '-o', str(lib)], cwd=ROOT, check=True)
    n = C.CDLL(str(lib))
    n.shim_load.argtypes = [C.c_char_p, C.c_size_t]
    n.shim_unary.argtypes = [C.c_int, C.c_uint32, U32P, U32P, C.c_int]
    n.shim_binary.argtypes = [C.c_int, C.c_uint32, C.c_uint32, C.c_int32, U32P, U32P, C.c_int]
    n.shim_rem.argtypes = [C.c_uint32, U32P, I32P, U32P, C.c_int]
    n.shim_ce20.argtypes = [U32P, U32P, C.c_int32, C.c_int32, C.c_int32, I32P, U32P]
    n.shim_set_init_jk3.argtypes = [C.c_int32]
    n.shim_set_init_jk3.restype = C.c_int32
    n.shim_wrap.argtypes = [C.c_int, C.c_uint32, C.c_uint32, I32P, PD, PM, PE, PF, C.c_uint, U32P, U32P]
    n.shim_adapter.argtypes = [C.c_int, C.c_uint32, C.c_uint32, C.c_int, I32P, U32P, U32P]
    n.shim_cross.argtypes = [C.c_int, C.c_uint32, C.c_uint32, U32P]
    n.shim_route_sine.argtypes = [C.c_float, C.POINTER(C.c_float)]
    return n


def native_unary(which, x, no_tables=0):
    out, fault = C.c_uint32(), C.c_uint32()
    rc = NATIVE.shim_unary(which, x, C.byref(out), C.byref(fault), no_tables)
    return rc, out.value, fault.value


def native_binary(which, a, b, i=0, no_tables=0):
    out, fault = C.c_uint32(), C.c_uint32()
    rc = NATIVE.shim_binary(which, a, b, i, C.byref(out), C.byref(fault), no_tables)
    return rc, out.value, fault.value


def native_rem(x, no_tables=0):
    y, n, fault = (C.c_uint32 * 2)(), C.c_int32(), C.c_uint32()
    rc = NATIVE.shim_rem(x, y, C.byref(n), C.byref(fault), no_tables)
    return rc, (y[0], y[1], n.value), fault.value


# ------------------------------------------------------------ the cases ---

UNARY_NATIVE = {SIN: 0, COS: 1, TAN: 2, ATAN: 3, FABS: 4, FLOOR: 5, SQRTK: 6, ISNAN: 7}


def special_words():
    out = [0, 1, 2, 0x7FFFFF, 0x400000, 0x800000, 0x800001, 0x7F7FFFFF, 0x7F7FFFFE,
           0x7F800000, 0x7F800001, 0x7FC00000, 0x7FFFFFFF, 0x3F800000, 0x3F7FFFFF, 0x3F800001]
    return sorted({w | s for w in out for s in (0, 0x80000000)})


def neighbourhood(words, radius=2):
    out = set()
    for w in words:
        for d in range(-radius, radius + 1):
            out.add((w + d) & 0x7FFFFFFF)
    return sorted({w | s for w in out for s in (0, 0x80000000)})


TRIG_EDGES = [0x31FFFFFF, 0x32000000, 0x317FFFFF, 0x31800000, 0x3E999999, 0x3E99999A,
              0x3F2CA13F, 0x3F2CA140, 0x3F480000, 0x3F480001, 0x3F490FD8, 0x3F490FDA,
              0x3FC90FD0, 0x3FC90FDB, 0x3FC90FE0, 0x4016CBE3, 0x4016CBE4, 0x43490F80,
              0x43490F81, 0x7F7FFFFF]
ATAN_EDGES = [0x30FFFFFF, 0x31000000, 0x3EDFFFFF, 0x3EE00000, 0x3F2FFFFF, 0x3F300000,
              0x3F97FFFF, 0x3F980000, 0x401BFFFF, 0x401C0000, 0x507FFFFF, 0x50800000,
              0x7F800000]


def pi_multiples(count, rng):
    """Words at and next to k*pi/2 (the reduction's cancellation paths)."""
    out = set()
    for k in range(1, 130):
        w = f2b(k * math.pi / 2)
        out.update((w - 1, w, w + 1))
    for _ in range(count):
        k = rng.randrange(1, 1 << rng.randrange(8, 60))
        w = f2b(min(k * math.pi / 2, 3.0e38))
        out.update((w - 1, w, w + 1))
    return sorted({w | s for w in out for s in (0, 0x80000000)})


def ease_words():
    """The script ease arguments (em_area_script: pi*t - pi/2 for t = k/d)."""
    out = set()
    pi, half = f2b(math.pi), f2b(math.pi / 2)
    for d in (range(1, 401) if FULL else (5, 30, 60, 90, 120, 180)):
        for k in range(d + 1):
            out.add(M.ee_sub(M.ee_mul(pi, f2b(k / d)), half))
    return sorted(out)


def random_words(rng, count):
    return [rng.getrandbits(32) for _ in range(count)]


def random_finite(rng, count, lo, hi):
    return [f2b(rng.uniform(lo, hi)) for _ in range(count)]


def trig_arguments(rng):
    words = special_words() + neighbourhood(TRIG_EDGES) + pi_multiples(pick(4000, 60), rng)
    words += ease_words()
    words += random_words(rng, pick(60000, 500))
    words += random_finite(rng, pick(40000, 400), -10.0, 10.0)
    words += random_finite(rng, pick(20000, 200), -2000.0, 2000.0)
    words += random_finite(rng, pick(10000, 150), -1e6, 1e6)
    return sorted(set(words))


def atan_arguments(rng):
    words = special_words() + neighbourhood(ATAN_EDGES)
    words += random_words(rng, pick(60000, 600))
    words += random_finite(rng, pick(40000, 400), -30.0, 30.0)
    return sorted(set(words))


def atan2_pairs(rng):
    classes = [0, 0x80000000, 1, 0x80000001, 0x3F800000, 0xBF800000, 0x40000000, 0x41200000,
               0xC2C80000, 0x7F7FFFFF, 0xFF7FFFFF, 0x7F800000, 0xFF800000, 0x7FC00000, 0x7F800001,
               0x00800000, 0x1E800000, 0x5F000000, 0x3F7FFFFF]
    pairs = [(a, b) for a in classes for b in classes]
    # exponent differences at the +-60 / 61 edges (0x11C6BC / 0x11C6DC)
    for k in (-62, -61, -60, -59, 59, 60, 61, 62):
        for s in (0, 0x80000000):
            for t in (0, 0x80000000):
                ex = 0x3F800000
                ey = ex + (k << 23)
                pairs.append((ey | s, ex | t))
                pairs.append((ey | s | 0x1234, ex | t | 0x777))
    pairs += [(rng.getrandbits(32), rng.getrandbits(32)) for _ in range(pick(40000, 500))]
    pairs += [(f2b(rng.uniform(-400, 400)), f2b(rng.uniform(-400, 400))) for _ in range(pick(40000, 500))]
    pairs += [(f2b(rng.uniform(-1, 1)), f2b(rng.uniform(-1, 1))) for _ in range(pick(10000, 150))]
    return pairs


def sqrt_arguments(rng):
    words = special_words() + [1 << b for b in range(23)] + [(1 << b) - 1 for b in range(1, 24)]
    words += random_words(rng, pick(60000, 600))
    words += random_finite(rng, pick(20000, 200), 0.0, 1e5)
    return sorted({w & MASK32 for w in words} | {w | 0x80000000 for w in words})


def floor_arguments(rng):
    words = special_words()
    for j0 in range(-3, 25):
        e = (j0 + 127) << 23
        words += [e, e | 1, e | 0x400000, e | 0x7FFFFF, e | 0x155555]
    words += [0x4B000000 | 0x7FFFFF, 0x7F800000, 0x7F800001]
    words += random_words(rng, pick(20000, 300))
    words += random_finite(rng, pick(20000, 300), -1000.0, 1000.0)
    return sorted({w & MASK32 for w in words} | {(w | 0x80000000) & MASK32 for w in words})


def scalbn_cases(rng):
    xs = special_words() + [0x3F800000, 0x3FC00000, 0x00000005, 0x80400000, 0x7F000000, 0x0C000000]
    ns = [-50001, -50000, -49999, -300, -200, -151, -150, -149, -127, -126, -25, -24, -1, 0, 1,
          24, 25, 126, 127, 128, 200, 254, 49999, 50000, 50001, 0x7FFFFFFF, -0x80000000]
    cases = [(x, n) for x in xs for n in ns]
    cases += [(rng.getrandbits(32), rng.randrange(-300, 300)) for _ in range(pick(20000, 300))]
    return cases


def kernel_cases(rng):
    cases = []
    for which in (1, 2, 3):
        for x in neighbourhood([0x31FFFFFF, 0x32000000, 0x317FFFFF, 0x31800000, 0x3E999999,
                                0x3F2CA13F, 0x3F480000, 0x3F490FDA, 0]):
            for iy in ((0, 1) if which == 1 else (1, -1) if which == 3 else (0,)):
                for y in (0, 0x80000000, 0x32000000, 0xB1800000):
                    cases.append((which, x, y, iy))
        for _ in range(pick(20000, 250)):
            x = f2b(rng.uniform(-0.7854, 0.7854))
            y = f2b(rng.uniform(-1e-8, 1e-8))
            iy = rng.choice((0, 1) if which == 1 else (1, -1) if which == 3 else (0,))
            cases.append((which, x, y, iy))
    return cases


INIT_JK3 = 0x26C538 + 12       # D_0026C538[3], read by 0011CE20 at 0x11CEA0


def ce20_cases(rng):
    """(x, e0, nx, prec, jk3). e0 <= -5 makes jv negative, so the clamp at
    0x11CE64 runs. jk3 None keeps the ELF's init_jk[3] = 0, where prec 3
    reaches at most jz = 1. The patched cases store another init_jk[3] in
    both the oracle RAM and the native tables, so the prec-3 second
    compensation loop (0x11D680) and the tail sum (0x11D6D8/0x11D6EC) run
    with jz >= 2 on the original instructions. The game never takes them."""
    cases = []
    for prec in (0, 1, 2, 3):
        for e0 in list(range(-12, 12)) + [20, 50, 90, 120]:
            for nx in (1, 2, 3):
                x = [rng.randrange(128, 256)] + [rng.randrange(0, 256) for _ in range(nx - 1)]
                cases.append(([f2b(float(v)) for v in x], e0, nx, prec, None))
        for _ in range(pick(3000, 40)):
            nx = rng.randrange(1, 4)
            x = [rng.randrange(128, 256)] + [rng.randrange(0, 256) for _ in range(nx - 1)]
            cases.append(([f2b(float(v)) for v in x], rng.randrange(-12, 121), nx, prec, None))
    for jk3 in (2, 5, 9):
        for e0 in (-12, -5, 0, 3, 7, 11, 20, 50, 90, 120):
            for nx in (1, 2, 3):
                x = [rng.randrange(128, 256)] + [rng.randrange(0, 256) for _ in range(nx - 1)]
                cases.append(([f2b(float(v)) for v in x], e0, nx, 3, jk3))
        for _ in range(pick(1000, 10)):
            nx = rng.randrange(1, 4)
            x = [rng.randrange(128, 256)] + [rng.randrange(0, 256) for _ in range(nx - 1)]
            cases.append(([f2b(float(v)) for v in x], rng.randrange(-12, 121), nx, 3, jk3))
    return cases


# ------------------------------------------------------------ comparisons ---

def run_unary(entry, words):
    o = shared_oracle()
    which = UNARY_NATIVE[entry]
    misses = []
    for x in words:
        o.run(entry, floats=(x,))
        want = (o.r[2] & MASK32) if entry == ISNAN else o.f[0]
        rc, got, fault = native_unary(which, x)
        if rc != 0 or got != want:
            misses.append((hex(entry), hex(x), hex(want), rc, hex(got), hex(fault)))
    return misses


def run_atan2(pairs):
    o = shared_oracle()
    misses = []
    for y, x in pairs:
        o.run(ATAN2, floats=(y, x))
        rc, got, fault = native_binary(0, y, x)
        if rc != 0 or got != o.f[0]:
            misses.append(('atan2', hex(y), hex(x), hex(o.f[0]), rc, hex(got)))
    return misses


def run_kernels(cases):
    o = shared_oracle()
    misses = []
    entry = {1: KSIN, 2: KCOS, 3: KTAN}
    for which, x, y, iy in cases:
        o.run(entry[which], args=(iy,), floats=(x, y))
        rc, got, fault = native_binary(which, x, y, iy)
        if rc != 0 or got != o.f[0]:
            misses.append((hex(entry[which]), hex(x), hex(y), iy, hex(o.f[0]), rc, hex(got)))
    return misses


def run_scalbn(cases):
    o = shared_oracle()
    misses = []
    for x, n in cases:
        o.run(SCALBN, args=(n,), floats=(x,))
        rc, got, _ = native_binary(4, x, 0, n)
        if got != o.f[0]:
            misses.append(('scalbn', hex(x), n, hex(o.f[0]), hex(got)))
    for x in special_words()[:12]:
        for y in (0, 0x80000000, 0x3F800000, 0xBF800000):
            o.run(COPYSIGN, floats=(x, y))
            _, got, _ = native_binary(5, x, y)
            if got != o.f[0]:
                misses.append(('copysign', hex(x), hex(y), hex(o.f[0]), hex(got)))
    return misses


def run_rem(words):
    o = shared_oracle()
    misses = []
    y = SCRATCH + 0x100          # outside every frame below STACK
    for x in words:
        o.run(REM, args=(y,), floats=(x,))
        want = (o.load(y, 4), o.load(y + 4, 4), s32(o.r[2]))
        rc, got, fault = native_rem(x)
        if rc != 0 or got != want:
            misses.append(('rem', hex(x), [hex(v & MASK32) for v in want], rc,
                           [hex(v & MASK32) for v in got], hex(fault)))
    return misses


POISON = struct.pack('<I', 0xDEADBEEF)


def run_ce20(cases):
    o = shared_oracle()
    misses, stale, patched = [], 0, 0
    xa, ya = SCRATCH + 0x200, SCRATCH + 0x300
    elf_jk3 = o.load(INIT_JK3, 4, signed=True)
    assert elf_jk3 == 0 and NATIVE.shim_set_init_jk3(0) == 0, 'init_jk[3] is not the ELF value 0'
    for x, e0, nx, prec, jk3 in cases:
        o.store(INIT_JK3, elf_jk3 if jk3 is None else jk3, 4)
        NATIVE.shim_set_init_jk3(elf_jk3 if jk3 is None else jk3)
        for i, v in enumerate(x):
            o.store(xa + 4 * i, v, 4)
        # Run twice over two different fills of the stack. A result that
        # depends on the fill read memory nothing wrote (prec 3 reads fq[1]
        # at 0x11D700 when jz = 0, and iq[jk-k] below the frame at 0x11D21C
        # when jk = 0); the native refuses those (fault 0x0011CE20).
        outs = []
        for fill in (POISON, bytes(4)):
            o.mem[STACK - 0x400:STACK] = fill * 0x100
            for i in range(3):
                o.store(ya + 4 * i, 0, 4)
            o.run(CE20, args=(xa, ya, e0, nx, prec, 0x26C178))   # t0 = prec, t1 = ipio2
            outs.append(([o.load(ya + 4 * i, 4) for i in range(3)], s32(o.r[2])))
        want_y = outs[0][0]
        words = 1 if prec == 0 else 3 if prec == 3 else 2
        xs = (C.c_uint32 * 20)(*x)
        ys, n, fault = (C.c_uint32 * 3)(), C.c_int32(), C.c_uint32()
        rc = NATIVE.shim_ce20(xs, ys, e0, nx, prec, C.byref(n), C.byref(fault))
        if (outs[0][0][:words], outs[0][1]) != (outs[1][0][:words], outs[1][1]):
            stale += 1
            if rc != -1 or fault.value != CE20:
                misses.append(('ce20 unwritten read not refused', [hex(v) for v in x], e0, nx, prec))
            continue
        want_n = outs[0][1]
        got = (list(ys)[:words], n.value)
        want = (want_y[:words], want_n)
        if rc != 0 or got != want:
            misses.append(("ce20", [hex(v) for v in x], e0, nx, prec, jk3, rc, hex(fault.value), [[hex(v) for v in want[0]], want[1]], [[hex(v) for v in got[0]], got[1]]))
        elif jk3 is not None:
            patched += 1          # prec 3 with jz >= jk3 >= 2, compared bit for bit
    o.store(INIT_JK3, elf_jk3, 4)
    NATIVE.shim_set_init_jk3(elf_jk3)
    return misses, stale, patched


# ------------------------------------------------------------ wrappers ---

def exc_tuple(o, address):
    return (o.load(address, 4, True), o.load(address + 4, 4), o.load(address + 8, 8),
            o.load(address + 16, 8), o.load(address + 24, 8), o.load(address + 32, 4, True))


def fake_double(x):
    return ((x * 0x9E3779B1) ^ (x << 29) ^ 0x0123456789ABCDEF) & MASK64


def fake_float(v):
    return (v ^ (v >> 32) ^ 0x5A5A5A5A) & MASK32


def recorded_hooks(events, script):
    """0x128350 / 0x11DB90 / 0x11FD78 / 0x127758 recorded in the oracle.
    script: (matherr result, err written by matherr or None, retval written
    or None, D_0026C5D0 written by 00128350 or None)."""
    ret, err, retval, flip = script

    def to_double(o):
        events.append(('d', o.f[12]))
        o.r[2] = s64(fake_double(o.f[12]))
        if flip is not None:
            o.store(0x26C5D0, flip, 4)

    def matherr(o):
        events.append(('m', exc_tuple(o, o.r[4] & MASK32)))
        if err is not None:
            o.store((o.r[4] & MASK32) + 32, err, 4)
        if retval is not None:
            o.store((o.r[4] & MASK32) + 24, retval, 8)
        o.r[2] = ret

    def errno(o):
        events.append(('e',))
        o.r[2] = ERRNO_CELL

    def to_float(o):
        events.append(('f', o.r[4] & MASK64))
        o.f[0] = fake_float(o.r[4] & MASK64)
    return {X_TODOUBLE: to_double, X_MATHERR: matherr, X_ERRNO: errno, X_TOFLOAT: to_float}


def native_wrapper(which, a, b, mode, script, drop=0, fail=0):
    """fail: bit 8/16/32/64 makes that worker return -1."""
    events, cell = [], C.c_int32(0x5EED)
    ret, err, retval, flip = script
    mode_cell = C.c_int32(mode)

    @PD
    def d(x, out):
        events.append(('d', x))
        out[0] = fake_double(x)
        if flip is not None:
            mode_cell.value = flip
        return -1 if fail & 8 else 0

    @PM
    def m(e, r):
        ex = e.contents
        events.append(('m', (ex.type, ex.name, ex.arg1, ex.arg2, ex.retval, ex.err)))
        if err is not None:
            ex.err = err
        if retval is not None:
            ex.retval = retval
        r[0] = ret
        return -1 if fail & 16 else 0

    @PE
    def e(cellp):
        events.append(('e',))
        cellp[0] = C.pointer(cell)
        return -1 if fail & 32 else 0

    @PF
    def f(v, out):
        events.append(('f', v))
        out[0] = fake_float(v)
        return -1 if fail & 64 else 0

    out, fault = C.c_uint32(), C.c_uint32()
    rc = NATIVE.shim_wrap(which, a, b, C.byref(mode_cell), d, m, e, f, drop, C.byref(out),
                          C.byref(fault))
    return rc, out.value, fault.value, events, cell.value


# The last two change D_0026C5D0 inside 00128350: 0011E748 reads it again
# at 0x11E7E0, 0011E620 does not.
WRAP_SCRIPTS = [(0, None, None, None), (1, None, None, None), (0, 7, None, None),
                (1, 0x22, 0x3FF0000000000000, None), (0, None, None, 2), (0, None, None, 1)]


def wrapper_cases():
    zeros = [0, 0x80000000, 1, 0x80000001, 0x007FFFFF]
    others = [0x3F800000, 0xBF800000, 0x7FC00000, 0x7F800001, 0xFF800000, 0x7F800000]
    cases = []
    for mode in (-1, 0, 1, 2, 5):
        for script in WRAP_SCRIPTS:
            for y in zeros + others:
                for x in zeros + others:
                    cases.append((0, y, x, mode, script))
            for x in zeros + others + [0xBF800000, 0xC2C80000, 0x80800000, 0xFF7FFFFF, 0x40800000]:
                cases.append((1, x, 0, mode, script))
    return cases


def run_wrappers(cases):
    o = shared_oracle()
    misses = []
    for which, a, b, mode, script in cases:
        o.store(0x26C5D0, mode, 4)
        o.store(ERRNO_CELL, 0x5EED, 4)
        events = []
        o.hooks = recorded_hooks(events, script)
        if which == 0:
            o.run(W_ATAN2, floats=(a, b))
        else:
            o.run(W_SQRT, floats=(a,))
        want = (o.f[0], events, o.load(ERRNO_CELL, 4, True))
        rc, got, fault, nevents, cell = native_wrapper(which, a, b, mode, script)
        if rc != 0 or (got, nevents, cell) != want:
            misses.append(('wrapper', which, hex(a), hex(b), mode, script, rc, hex(fault), want,
                           (got, nevents, cell)))
    o.store(0x26C5D0, 1, 4)
    o.hooks = {}
    return misses


def full_original_wrappers():
    """Every callee as original instructions (D_0026C5D0 = 1 as captured).
    The native workers run the same original callees one by one."""
    o = shared_oracle()
    errno_pointer = o.load(0x24295C, 4)
    saved_errno = o.load(errno_pointer, 4)
    results, misses = [], []
    for which, a, b in ((0, 0, 0), (0, 0x80000000, 0x80000000), (0, 0, 0x80000000),
                        (0, 1, 0x80000001), (1, 0xBF800000, 0), (1, 0x80000001, 0),
                        (1, 0xC2C80000, 0)):
        o.hooks = {}
        o.store(errno_pointer, 0x5EED, 4)
        o.run(W_ATAN2 if which == 0 else W_SQRT, floats=(a, b) if which == 0 else (a,))
        want = (o.f[0], o.load(errno_pointer, 4, True))
        # native with workers = original executions of the callees
        cell = C.c_int32(0x5EED)
        calls = []
        w = shared_oracle_scratch()

        @PD
        def d(x, out):
            w.run(X_TODOUBLE, floats=(x,))
            out[0] = w.r[2] & MASK64
            calls.append('00128350')
            return 0

        @PM
        def m(e, r):
            ex = e.contents
            base = SCRATCH
            w.store(base, ex.type, 4)
            w.store(base + 4, ex.name, 4)
            w.store(base + 8, ex.arg1, 8)
            w.store(base + 16, ex.arg2, 8)
            w.store(base + 24, ex.retval, 8)
            w.store(base + 32, ex.err, 4)
            w.run(X_MATHERR, args=(base,))
            ex.type, ex.name, ex.arg1, ex.arg2, ex.retval, ex.err = exc_tuple(w, base)
            r[0] = s32(w.r[2])
            calls.append('0011DB90')
            return 0

        @PE
        def e(cellp):
            w.run(X_ERRNO)
            assert w.r[2] & MASK32 == errno_pointer
            cellp[0] = C.pointer(cell)
            calls.append('0011FD78')
            return 0

        @PF
        def f(v, out):
            w.run(X_TOFLOAT, args=(s64(v),))
            out[0] = w.f[0]
            calls.append('00127758')
            return 0

        mode_cell = C.c_int32(o.load(0x26C5D0, 4, True))
        out, fault = C.c_uint32(), C.c_uint32()
        rc = NATIVE.shim_wrap(which, a, b, C.byref(mode_cell), d, m, e, f, 0, C.byref(out),
                              C.byref(fault))
        got = (out.value, cell.value)
        if rc != 0 or got != want:
            misses.append(('full', which, hex(a), hex(b), want, rc, got))
        results.append((('atan2f' if which == 0 else 'sqrtf'), a, b, want, calls))
    o.store(errno_pointer, saved_errno, 4)
    return results, misses


_SCRATCH = None


def shared_oracle_scratch():
    global _SCRATCH
    if _SCRATCH is None:
        _SCRATCH = oracle()
    return _SCRATCH


# ------------------------------------------------------------ fail-stop ---

def fail_stop():
    checks = 0

    def expect(label, rc, fault, address):
        nonlocal checks
        checks += 1
        assert rc == -1 and fault == address, (label, rc, hex(fault), hex(address))

    # tables NULL where the path reads them; fine where it does not
    rc, v, fault = native_unary(0, f2b(0.5), no_tables=1)
    assert rc == 0, 'sinf below pi/4 reads no table'
    rc, v, fault = native_unary(0, f2b(2.0), no_tables=1)
    assert rc == 0, 'sinf n = +-1 reads no table'
    rc, _, fault = native_unary(0, f2b(10.0), no_tables=1)
    expect('sin medium: npio2_hw', rc, fault, 0x0011C98C)
    rc, _, fault = native_unary(1, f2b(1e6), no_tables=1)
    expect('cos huge: init_jk', rc, fault, 0x0011CEA0)
    rc, _, fault = native_unary(2, f2b(0.3), no_tables=1)
    expect('tan: T', rc, fault, 0x0011D974)
    rc, _, fault = native_unary(3, f2b(0.3), no_tables=1)
    expect('atan: aT', rc, fault, 0x0011DD78)
    rc, _, fault = native_unary(3, f2b(1e20), no_tables=1)
    expect('atan: hi[3]', rc, fault, 0x0011DC14)
    rc, _, fault = native_binary(0, f2b(-1.0), 0x7F800000, no_tables=1)
    expect('atan2: D_0026C170', rc, fault, 0x0011C684)
    rc, _, fault = native_rem(f2b(10.0), no_tables=1)
    expect('rem medium', rc, fault, 0x0011C98C)
    # wrappers
    for which, a, b in ((0, 0, 0), (1, 0xBF800000, 0)):
        entry = W_ATAN2 if which == 0 else W_SQRT
        rc, _, fault, _, _ = native_wrapper(which, a, b, 1, (0, None, None, None), drop=256)
        expect('result NULL', rc, fault, entry)
        rc, _, fault, _, _ = native_wrapper(which, a, b, 1, (0, None, None, None), drop=2)
        expect('world NULL', rc, fault, 0x0011E648 if which == 0 else 0x0011E76C)
        rc, _, fault, _, _ = native_wrapper(which, a, b, 1, (0, None, None, None), drop=4)
        expect('world cell NULL', rc, fault, 0x0011E648 if which == 0 else 0x0011E76C)
        for bit, address in ((8, X_TODOUBLE), (16, X_MATHERR), (32, X_ERRNO), (64, X_TOFLOAT)):
            rc, _, fault, _, _ = native_wrapper(which, a, b, 1, (0, None, None, None), drop=bit)
            expect(f'worker {address:#x} NULL', rc, fault, address)
            rc, _, fault, _, _ = native_wrapper(which, a, b, 1, (0, None, None, None), fail=bit)
            expect(f'worker {address:#x} fails', rc, fault, address)
        rc, _, fault, _, _ = native_wrapper(which, a, b, 1, (0, None, None, None), drop=128)
        expect('workers NULL', rc, fault, X_TODOUBLE)
    rc, _, fault, _, _ = native_wrapper(1, 0xBF800000, 0, 1, (0, None, None, None), drop=1)
    expect('sqrtf mode 1: D_0026C650', rc, fault, 0x0011E7D8)
    # the worker paths not reached need nothing
    rc, v, fault, ev, _ = native_wrapper(0, f2b(1.0), f2b(2.0), 1, (0, None, None, None), drop=8 | 16 | 32 | 64)
    assert rc == 0 and not ev, 'atan2f off the domain path needs no worker'
    rc, v, fault, ev, _ = native_wrapper(1, f2b(4.0), 0, 1, (0, None, None, None), drop=1 | 8 | 16 | 32 | 64)
    assert rc == 0 and v == f2b(2.0), 'sqrtf off the domain path needs no worker or table'
    # 0011CE20 bounds (not reachable from 0011C7B0; the original would leave its frame)
    ys, n, fault = (C.c_uint32 * 3)(), C.c_int32(), C.c_uint32()
    xs = (C.c_uint32 * 20)(*([f2b(200.0)] * 20))
    rc = NATIVE.shim_ce20(xs, ys, 100000, 3, 2, C.byref(n), C.byref(fault))
    expect('ce20 ipio2 index', rc, fault.value, CE20)
    rc = NATIVE.shim_ce20(xs, ys, 20, 12, 2, C.byref(n), C.byref(fault))
    expect('ce20 f[] index', rc, fault.value, CE20)
    rc = NATIVE.shim_ce20(xs, ys, 20, 3, 4, C.byref(n), C.byref(fault))
    expect('ce20 prec', rc, fault.value, CE20)
    # adapters record the fault and return +0
    out, f = C.c_uint32(), C.c_uint32()
    rc = NATIVE.shim_adapter(2, f2b(0.3), 0, 1, None, C.byref(out), C.byref(f))
    assert out.value == 0 and f.value == 0x0011D974, ('adapter fault', hex(out.value), hex(f.value))
    rc = NATIVE.shim_adapter(6, f2b(10.0), 0, 1, None, C.byref(out), C.byref(f))
    assert rc == -1 and f.value == 0x0011C98C, 'w_0011E2A8 adapter fault'
    rc = NATIVE.shim_adapter(4, 0, 0, 0, None, C.byref(out), C.byref(f))
    assert out.value == 0 and f.value == 0x0011E648, 'atan2 adapter without D_0026C5D0'
    checks += 3
    return checks


def adapters(rng):
    """The adapter forms return what the entry points return."""
    mode = C.c_int32(1)
    count = 0
    for _ in range(pick(2000, 100)):
        a, b = f2b(rng.uniform(-50, 50)), f2b(rng.uniform(-50, 50))
        for which, ref in ((0, lambda: native_unary(0, a)), (1, lambda: native_unary(1, a)),
                           (2, lambda: native_unary(2, a)), (3, lambda: native_unary(3, a)),
                           (6, lambda: native_unary(0, a)), (7, lambda: native_unary(1, a))):
            out, f = C.c_uint32(), C.c_uint32()
            NATIVE.shim_adapter(which, a, b, 0, C.byref(mode), C.byref(out), C.byref(f))
            assert out.value == ref()[1] and f.value == 0, ('adapter', which, hex(a))
            count += 1
        out, f = C.c_uint32(), C.c_uint32()
        NATIVE.shim_adapter(4, a, b, 0, C.byref(mode), C.byref(out), C.byref(f))
        assert out.value == native_binary(0, a, b)[1] and f.value == 0, ('atan2 adapter', hex(a), hex(b))
        s = f2b(abs(b2f(a)))
        NATIVE.shim_adapter(5, s, 0, 0, C.byref(mode), C.byref(out), C.byref(f))
        assert out.value == native_unary(6, s)[1] and f.value == 0, ('sqrt adapter', hex(s))
        count += 2
    return count


# ------------------------------------------------------------ cross-checks ---

def cross_checks(rng, zero_result):
    """Host models against the verified translation (which equals the
    original on every argument above)."""
    report, asserted = {}, []

    def cross(which, a, b=0):
        out = C.c_uint32()
        rc = NATIVE.shim_cross(which, a, b, C.byref(out))
        return rc, out.value

    # em_area_script_sin_0011E2A8 on its domain |x| <= 0x4016CBE3 (asserted)
    words = sorted(set(ease_words() + neighbourhood([0x3FC90FD0, 0x3FC90FDB, 0x3F490FD8, 0x4016CBE3,
                                                    0x31FFFFFF, 0x3E999999, 0x3F480000], 4)
                       + [rng.randrange(0, 0x4016CBE4) | rng.choice((0, 0x80000000))
                          for _ in range(pick(20000, 2000))]))
    same = 0
    for x in words:
        if (x & 0x7FFFFFFF) > 0x4016CBE3:
            continue
        rc, v = cross(0, x)
        mine = native_unary(0, x)[1]
        assert rc == 0 and v == mine, ('em_area_script_sin_0011E2A8 differs', hex(x), hex(v), hex(mine))
        same += 1
    report['em_area_script_sin_0011E2A8'] = f'= on {same:,} arguments (asserted)'

    def tally(label, which, args, mine_fn, domain=lambda *a: True):
        total = diff = refused = plain = other = 0
        examples = []
        for args_ in args:
            if not domain(*args_):
                continue
            rc, v = cross(which, *args_)
            total += 1
            if rc != 0:
                refused += 1
                continue
            m = mine_fn(*args_)
            if v != m:
                diff += 1
                # no denormal operand (exponent field 0 with a nonzero fraction)
                clean = all((a >> 23) & 0xFF or not a & 0x7FFFFF for a in args_)
                plain += clean
                # ... and a normal (or zero) host result: not a DAZ/FTZ case
                other += clean and bool((v >> 23) & 0xFF or not v & 0x7FFFFF)
                if len(examples) < 3:
                    examples.append(tuple(hex(a) for a in args_) + (hex(m), hex(v)))
        report[label] = (f'differs on {diff:,} of {total - refused:,}'
                         + (f' ({diff - plain:,} with a denormal operand, {plain - other:,} with a '
                            f'denormal host result, {other:,} other)' if diff else '')
                         + (f', refuses {refused:,}' if refused else '')
                         + (f'; e.g. (args, original, host) {examples}' if examples else ''))
        return diff, other

    finite = lambda *a: all((w & 0x7F800000) != 0x7F800000 for w in a)
    atan_words = atan_arguments(rng)
    pairs = atan2_pairs(rng)
    # The director's translations use em_pose_math.h (pre-trim add/sub,
    # truncating mul, nearest div, no DAZ/FTZ): asserted to differ only
    # where a denormal operand or result is involved.
    d = tally('em_director_original_0011DBB8', 1, [(w,) for w in atan_words],
              lambda x: native_unary(3, x)[1], finite)
    assert d == (0, 0), ('em_director_original_0011DBB8 differs from 0011DBB8', report)
    d = tally('em_director_original_0011C4C8', 2, pairs, lambda y, x: native_binary(0, y, x)[1], finite)
    assert d[1] == 0, ('em_director_original_0011C4C8 differs beyond DAZ/FTZ', report)

    def my_atan2_wrapper(y, x):
        # 0011E620 with D_0026C5D0 = 1: the kernel, except that an EE-zero
        # pair (c.eq.s, so denormals too) returns 00127758(0.0), measured
        # in part 5 with the original callees (`zero_result`).
        if M.ee_c_eq(x, 0) and M.ee_c_eq(y, 0):
            return zero_result
        return native_binary(0, y, x)[1]
    d = tally('em_director_original_0011E620', 3, pairs, my_atan2_wrapper, finite)
    assert d[1] == 0, ('em_director_original_0011E620 differs beyond DAZ/FTZ', report)
    trig = trig_arguments(rng)
    item_domain = lambda x: finite(x) and abs(b2f(x)) <= 12.566371
    tally('em_item_sdk_sine', 4, [(w,) for w in trig], lambda x: native_unary(0, x)[1], item_domain)
    tally('em_item_sdk_cosine', 5, [(w,) for w in trig], lambda x: native_unary(1, x)[1], item_domain)
    tally('em_item_sdk_sqrt', 6, [(w,) for w in sqrt_arguments(rng)],
          lambda x: native_unary(6, x)[1], lambda x: finite(x) and not x & 0x80000000)
    tally('em_interaction_sdk_atan2', 7, pairs, lambda y, x: native_binary(0, y, x)[1], finite)
    return report


# ------------------------------------------------------------ route ---

ROUTE_LABELS = ('truck preview 8292C0', 'Roger 828990 + director 8294C0')


class RouteLib:
    """The area-script library with its sine binding replaced by this
    module's em_sdk_math_original_w_0011E2A8 (everything else forwarded)."""

    def __init__(self, lib):
        self._lib, self.args = lib, []

    def __getattr__(self, name):
        return getattr(self._lib, name)

    def em_area_script_sin_0011E2A8(self, x, ref):
        out = C.c_float()
        rc = NATIVE.shim_route_sine(x, C.byref(out))
        self.args.append((f2b(x), f2b(out.value)))
        ref._obj.value = out.value
        return rc


def route_case(case):
    import test_area_script_reference as asr
    label, frames, checks = asr.capture_case(case)
    lib = asr.CONTEXT['lib']
    return label, frames, checks, list(lib.args)


# ------------------------------------------------------------ main ---

def chunks(items, parts=8):
    return [items[i::parts] for i in range(parts) if items[i::parts]]


def sweep(fn, items):
    return [m for part in parallel_map(fn, chunks(items)) for m in part]


def unary_job(job):
    entry, words = job
    return run_unary(entry, words)


def main():
    global ELF, RAM, SIZES, NATIVE
    ELF = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(ELF).hexdigest() == ELF_SHA, 'unexpected boot ELF'
    RAM = (REF / 'playable_ee.bin').read_bytes()
    with open(DECOMP / 'docs/FUNCTIONS.csv') as fh:
        SIZES = {int(row['vram'], 16): int(row['size_bytes']) for row in csv.DictReader(fh)}
    assert struct.unpack_from('<i', RAM, 0x26C5D0)[0] == 1, 'D_0026C5D0 is not 1 in the capture'
    data = slice(0x26C170, 0x26C658)
    assert RAM[data] == ELF[data.start - 0x100000 + 0x300:data.stop - 0x100000 + 0x300], \
        'captured SDK tables differ from the ELF'
    NATIVE = build_native()
    assert NATIVE.shim_load(ELF, len(ELF)) == 0, 'table load'
    rng = random.Random(0x11E2A8)
    counts = {}

    trig = trig_arguments(rng)
    atan_words = atan_arguments(rng)
    jobs = []
    for entry, words in ((SIN, trig), (COS, trig), (TAN, trig), (ATAN, atan_words),
                         (SQRTK, sqrt_arguments(rng)), (FLOOR, floor_arguments(rng)),
                         (FABS, special_words() + random_words(rng, 200)),
                         (ISNAN, special_words() + neighbourhood([0x7F800000], 3))):
        for part in chunks(words, 4):
            jobs.append((entry, part))
        counts[f'{entry:08X}'] = len(words)
    misses = [m for part in parallel_map(unary_job, jobs, cost=lambda j: len(j[1])) for m in part]
    assert not misses, ('unary', len(misses), misses[:6])

    pairs = atan2_pairs(rng)
    misses = sweep(run_atan2, pairs)
    assert not misses, ('0011C4C8', len(misses), misses[:6])
    counts['0011C4C8'] = len(pairs)

    kernels = kernel_cases(rng)
    misses = sweep(run_kernels, kernels)
    assert not misses, ('kernels', len(misses), misses[:6])
    counts['0011D770/0011CCC8/0011D878'] = len(kernels)

    sc = scalbn_cases(rng)
    misses = sweep(run_scalbn, sc)
    assert not misses, ('scalbn', len(misses), misses[:6])
    counts['0011E148/0011DE60'] = len(sc)

    misses = sweep(run_rem, trig)
    assert not misses, ('0011C7B0', len(misses), misses[:6])
    counts['0011C7B0'] = len(trig)

    ce = ce20_cases(rng)
    parts = parallel_map(run_ce20, chunks(ce))
    misses = [m for part, _, _ in parts for m in part]
    stale = sum(n for _, n, _ in parts)
    patched = sum(n for _, _, n in parts)
    assert not misses, ('0011CE20', len(misses), misses[:6])
    assert stale, 'no prec-3 case reached the unwritten fq[1]'
    assert any(e0 <= -5 for _, e0, _, _, _ in ce), 'no 0011CE20 case clamps jv'
    assert patched >= sum(1 for c in ce if c[4] is not None) // 2, \
        ('too few patched prec-3 cases compared', patched)
    counts['0011CE20'] = len(ce)
    print(f'0011CE20: {patched:,} of the {len(ce):,} cases are prec 3 with init_jk[3] patched to 2/5/9 '
          f'(jz >= 2), {sum(1 for c in ce if c[1] <= -5):,} clamp jv (e0 <= -5), {stale:,} stale reads refused')

    wc = wrapper_cases()
    misses = sweep(run_wrappers, wc)
    assert not misses, ('wrappers', len(misses), misses[:4])
    counts['0011E620/0011E748 recorded'] = len(wc)
    results, misses = full_original_wrappers()
    assert not misses, ('wrappers, original callees', misses)
    counts['wrappers with original callees'] = len(results)

    counts['fail-stop'] = fail_stop()
    counts['adapters'] = adapters(rng)

    zero = [value for name, a, b, (value, _), _ in results if name == 'atan2f' and a == 0 and b == 0]
    report = cross_checks(rng, zero[0])

    # Route beats 07 and 10: the script host with this module's sine.
    import test_area_script_reference as asr
    asr.BUILD = BUILD / 'area_script'
    asr.CONTEXT.update(elf=ELF, lib=RouteLib(asr.build()))
    cases = [c for c in asr.ROUTE_CASES if c[0] in ROUTE_LABELS]
    assert len(cases) == 2, 'route cases missing'
    for case in cases:
        assert (asr.ROUTE / case[1] / 'trace.json').is_file(), ('route capture missing', case[1])
    routed = parallel_map(route_case, cases)
    route_args = sorted({a for *_, args in routed for a, _ in args})
    o = shared_oracle()
    for label, frames, checks, args in routed:
        for x, v in args:
            o.run(SIN, floats=(x,))
            assert o.f[0] == v, ('route sine differs from 0011E2A8', label, hex(x), hex(v), hex(o.f[0]))
        print(f'route {label}: {frames} frames, 0 differences (camera {checks.get("camera", 0)}, '
              f'block {checks.get("block", 0)}); {len(args)} sine calls', flush=True)
    counts['route sine arguments'] = len(route_args)

    for label, text in report.items():
        print(f'cross-check {label}: {text}')
    for name, a, b, (value, errno), calls in results:
        args = f'{a:#010x}' if name == 'sqrtf' else f'{a:#010x}, {b:#010x}'
        print(f'original {name}({args}) = {value:#010x}, errno {errno:#x} (callees {", ".join(calls)})')
    total = sum(counts.values())
    banner(', '.join(f'{k} {v:,}' for k, v in counts.items()),
           f'{total:,} cases equal to the original instructions')


if __name__ == '__main__':
    main()
