#!/usr/bin/env python3
"""The boot ELF's soft-float workers: em_sdk_soft_float vs the original instructions.

docs/SDK_SOFT_FLOAT.md. The oracle is the bounded EE interpreter of
tools/test_sdk_math_original_reference.py (imported, not edited), subclassed
here to record every store outside the running function's stack. It executes
the user's own boot-ELF code over the captured first-control RAM
(../Extermination/build/startup-reference/playable_ee.bin); each function it
enters is checked byte for byte against ../Extermination/config/SCUS_971.12
(ranges from ../Extermination/docs/FUNCTIONS.csv). These routines contain no
COP1 arithmetic; the float argument and result are register moves.

Parts (quick mode samples the random sweeps; EM_TEST_FULL=1 runs them whole):
 1. 00128350 (float -> double) over every exponent with edge fractions, the
    NaN classes, and random words. Each case runs over a random stack fill
    and a random stale a2; a subset runs a second time over another fill and
    must give the same double (the unwritten record words do not matter).
 2. 00127758 (double -> float): every class, the float-denormal and overflow
    edges, the rounding ties / sticky bits / carries, NaN payloads, random
    words; the same two-fill check.
 3. 001274B0 (the double compare, via 00126BE8 and 00127398): special x
    special pairs, equal and one-ulp neighbours, random pairs.
 4. Leaves on arbitrary records: 001278C0 and 00126BE8 (all record bytes,
    including the words they leave alone), 00126AB8 and 001277B0 (any class,
    sign word, exponent and fraction, and a random incoming a2), 00127398
    (arbitrary record pairs), 00127728 and 00128320 (their four arguments).
 5. 0011DB90 over exception records: returns 0, the record is unchanged and
    nothing outside its stack is written. 0011FD78 with the captured
    D_0024295C and with other values.
 6. The wrappers 0011E620 (atan2f) and 0011E748 (sqrtf) executed whole as
    original instructions vs em_sdk_math_original with its four workers bound
    to this module (em_sdk_soft_float_bind): result and errno word, for
    D_0026C5D0 = -1, 0, 1, 2, 5, on EE-zero vectors, negative normal and
    denormal square roots, NaNs, infinities and ordinary values.
 7. Route: every AREA11 route snapshot (../Extermination/build/s87/route)
    holds D_0024295C = 0x00242670 and D_0026C5D0 = 1; the errno word there
    is reported per beat and must first read 0x21 at the end of beat 03;
    part 6's zero-vector / negative-root cases are replayed over the beat 03
    RAM (every beat in full mode).
 8. Fail-stop: every adapter's NULL output, the 0011FD78 storage checks, the
    loader rejections, and a bound wrapper whose errno cell is missing.
 9. The runtime's export (assets/sdk_soft_float.emsf, written by
    tools/export_sdk_math_tables.py from the user's ELF): it must hold
    D_0024295C and the errno cell exactly as the ELF and the captured
    first-control RAM do (the route snapshots' D_0024295C was checked in
    part 7), and em_sdk_soft_float_load_export must read it back. The
    zero-vector / negative-root wrapper cases then run over a context built
    from the loaded export and must equal the original (as in part 6).

EM_SDK_SOFT_FLOAT_SOURCE replaces the translation source (the negative
controls of the doc were run with it). Only addresses and values are printed;
no original bytes are embedded.
"""
import csv
import ctypes as C
import hashlib
import os
from pathlib import Path
import random
import struct
import subprocess
import sys

import test_sdk_math_original_reference as SM
from reference_mode import FULL, banner, parallel_map, pick

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
REF = DECOMP / 'build/startup-reference'
ROUTE = DECOMP / 'build/s87/route'
BUILD = ROOT / 'build/sdk_soft_float_reference'
SOURCE = os.environ.get('EM_SDK_SOFT_FLOAT_SOURCE', 'src/game/em_sdk_soft_float.c')
MASK32, MASK64 = 0xFFFFFFFF, (1 << 64) - 1
STACK = SM.STACK
FRAME_LOW = STACK - 0x200          # every frame on these paths fits below STACK
REC_A, REC_B, WORD = 0x01E20000, 0x01E20040, 0x01E20080

TO_DOUBLE, UNPACK_F, WIDEN, PACK_D = 0x128350, 0x1278C0, 0x127728, 0x126AB8
MATHERR, COMPARE, UNPACK_D, CMP_PARTS = 0x11DB90, 0x1274B0, 0x126BE8, 0x127398
ERRNO, TO_FLOAT, NARROW, PACK_F = 0x11FD78, 0x127758, 0x128320, 0x1277B0
W_ATAN2, W_SQRT = 0x11E620, 0x11E748
D_ERRNO_PTR, D_MODE = 0x24295C, 0x26C5D0

s32, s64 = SM.s32, SM.s64


# ------------------------------------------------------------------ oracle --

class Oracle(SM.Oracle):
    """The shared interpreter, recording stores outside the stack frames."""

    def __init__(self, mem, elf, sizes):
        super().__init__(mem, elf, sizes)
        self.allowed = []
        self.foreign = []

    def store(self, address, value, size):
        a = address & MASK32
        if not (FRAME_LOW <= a < STACK) and not any(lo <= a < hi for lo, hi in self.allowed):
            self.foreign.append(a)
        super().store(address, value, size)

    def fill_stack(self, rng):
        self.mem[FRAME_LOW:STACK] = rng.getrandbits(8 * (STACK - FRAME_LOW)).to_bytes(STACK - FRAME_LOW, 'little')

    def go(self, entry, args=(), floats=(), a2=None, allowed=()):
        self.allowed, self.foreign = list(allowed), []
        if a2 is None:
            self.run(entry, args, floats)
        else:
            # run() clears the registers; the stale a2 goes in as the third argument.
            regs = list(args) + [0] * (3 - len(args))
            regs[2] = s64(a2)
            self.run(entry, regs, floats)
        assert not self.foreign, ('stores outside the frame', hex(entry), [hex(a) for a in self.foreign[:4]])
        return self


ELF = RAM = SIZES = NATIVE = None
_ORACLE = None


def shared():
    global _ORACLE
    if _ORACLE is None:
        _ORACLE = Oracle(bytearray(RAM), ELF, SIZES)
    _ORACLE.hooks = {}
    return _ORACLE


# ------------------------------------------------------------------ native --

SHIM = r'''
#include <assert.h>
#include <string.h>
#include "game/em_sdk_soft_float.h"

_Static_assert(sizeof(EmSdkSoftFloatPartsF) == 16, "float record");
_Static_assert(sizeof(EmSdkSoftFloatPartsD) == 24, "double record");

uint64_t s_to_double(uint32_t x) { return em_sdk_soft_float_00128350(x); }
uint32_t s_to_float(uint64_t v) { return em_sdk_soft_float_00127758(v); }
int32_t s_compare(uint64_t a, uint64_t b) { return em_sdk_soft_float_001274B0(a, b); }
void s_unpack_f(uint32_t w, uint8_t *rec) { EmSdkSoftFloatPartsF p; memcpy(&p, rec, 16); em_sdk_soft_float_001278C0(w, &p); memcpy(rec, &p, 16); }
void s_unpack_d(uint64_t v, uint8_t *rec) { EmSdkSoftFloatPartsD p; memcpy(&p, rec, 24); em_sdk_soft_float_00126BE8(v, &p); memcpy(rec, &p, 24); }
uint64_t s_pack_d(const uint8_t *rec) { EmSdkSoftFloatPartsD p; memcpy(&p, rec, 24); return em_sdk_soft_float_00126AB8(&p); }
uint32_t s_pack_f(const uint8_t *rec) { EmSdkSoftFloatPartsF p; memcpy(&p, rec, 16); return em_sdk_soft_float_001277B0(&p); }
int32_t s_cmp_parts(const uint8_t *a, const uint8_t *b)
{
    EmSdkSoftFloatPartsD p, q;
    memcpy(&p, a, 24); memcpy(&q, b, 24);
    return em_sdk_soft_float_00127398(&p, &q);
}
uint64_t s_widen(uint32_t c, uint32_t s, int32_t e, uint64_t f) { return em_sdk_soft_float_00127728(c, s, e, f); }
uint32_t s_narrow(uint32_t c, uint32_t s, int32_t e, uint32_t f) { return em_sdk_soft_float_00128320(c, s, e, f); }

/* The EE record layout: +0 type, +4 name, +8 arg1, +0x10 arg2, +0x18 retval, +0x20 err. */
static void from_ee(const uint8_t *b, EmSdkMathException *e)
{
    memcpy(&e->type, b, 4); memcpy(&e->name, b + 4, 4); memcpy(&e->arg1, b + 8, 8);
    memcpy(&e->arg2, b + 16, 8); memcpy(&e->retval, b + 24, 8); memcpy(&e->err, b + 32, 4);
}
static void to_ee(const EmSdkMathException *e, uint8_t *b)
{
    memcpy(b, &e->type, 4); memcpy(b + 4, &e->name, 4); memcpy(b + 8, &e->arg1, 8);
    memcpy(b + 16, &e->arg2, 8); memcpy(b + 24, &e->retval, 8); memcpy(b + 32, &e->err, 4);
}

/* 0011DB90 through the worker adapter; the record comes back in EE layout. */
int s_matherr(uint8_t *rec, int32_t *result, uint32_t *fault)
{
    EmSdkSoftFloatContext c;
    memset(&c, 0, sizeof c);
    EmSdkMathException e;
    from_ee(rec, &e);
    int rc = em_sdk_soft_float_w_0011DB90(&c, &e, result);
    to_ee(&e, rec);
    *fault = c.fault;
    return rc;
}

/* 0011FD78 through the adapter: *value is the address of the cell handed
 * out (0 when none), matched against the context's errno address. */
int s_errno(uint32_t d24295C, uint32_t errno_address, int with_cell, uint32_t *handed, uint32_t *fault)
{
    static int32_t cell;
    EmSdkSoftFloatContext c = { &d24295C, errno_address, with_cell ? &cell : NULL, 0 };
    int32_t *out = NULL;
    int rc = em_sdk_soft_float_w_0011FD78(&c, &out);
    *handed = out == &cell ? errno_address : 0;
    *fault = c.fault;
    return rc;
}

/* The wrappers with this module bound: which 0 atan2f(a, b), 1 sqrtf(a). */
int s_wrap(int which, uint32_t a, uint32_t b, int32_t mode, uint32_t d24295C, uint32_t errno_address,
           int32_t *errno_cell, const EmSdkMathTables *tables, uint32_t *out, uint32_t *fault,
           uint32_t *context_fault)
{
    EmSdkSoftFloatContext c = { &d24295C, errno_address, errno_cell, 0 };
    EmSdkMathWorkers k;
    memset(&k, 0, sizeof k);
    if (em_sdk_soft_float_bind(&k, &c) < 0) return -2;
    EmSdkMathWorld world = { &mode };
    float x, y = 0.0f, r = 0.0f;
    memcpy(&x, &a, 4); memcpy(&y, &b, 4);
    *fault = 0;
    int rc = which == 0 ? em_sdk_math_original_0011E620(tables, &world, &k, x, y, &r, fault)
                        : em_sdk_math_original_0011E748(tables, &world, &k, x, &r, fault);
    memcpy(out, &r, 4);
    *context_fault = c.fault;
    return rc;
}

int s_load_export(const char *path, uint32_t *pointer, int32_t *word)
{
    return em_sdk_soft_float_load_export(path, pointer, word);
}

static EmSdkMathTables T;
const EmSdkMathTables *s_tables(const uint8_t *elf, size_t size)
{
    return em_sdk_math_original_load_tables(elf, size, &T) == 0 ? &T : NULL;
}

/* Fail-stop: returns the number of checks, or -(the failing check). */
int s_fail_stop(const uint8_t *elf, size_t size)
{
    int n = 0;
    EmSdkSoftFloatContext c;
    uint64_t d; float f; int32_t r, *cell; uint32_t w;
    EmSdkMathException e;
    memset(&e, 0, sizeof e);
#define CHECK(cond) do { ++n; if (!(cond)) return -n; } while (0)
    memset(&c, 0, sizeof c);
    CHECK(em_sdk_soft_float_w_00128350(&c, 1.0f, NULL) == -1 && c.fault == 0x00128350u);
    CHECK(em_sdk_soft_float_w_00128350(NULL, 1.0f, NULL) == -1);
    CHECK(em_sdk_soft_float_w_00128350(NULL, 1.0f, &d) == 0 && d == UINT64_C(0x3FF0000000000000));
    memset(&c, 0, sizeof c);
    CHECK(em_sdk_soft_float_w_0011DB90(&c, NULL, &r) == -1 && c.fault == 0x0011DB90u);
    memset(&c, 0, sizeof c);
    CHECK(em_sdk_soft_float_w_0011DB90(&c, &e, NULL) == -1 && c.fault == 0x0011DB90u);
    r = 7;
    CHECK(em_sdk_soft_float_w_0011DB90(NULL, &e, &r) == 0 && r == 0);
    memset(&c, 0, sizeof c);
    CHECK(em_sdk_soft_float_w_00127758(&c, 0, NULL) == -1 && c.fault == 0x00127758u);
    CHECK(em_sdk_soft_float_w_00127758(NULL, 0, &f) == 0);
    /* 0011FD78 */
    static int32_t errno_cell;
    uint32_t ptr = 0x00242670u;
    CHECK(em_sdk_soft_float_w_0011FD78(NULL, &cell) == -1);
    c = (EmSdkSoftFloatContext){ &ptr, 0x00242670u, &errno_cell, 0 };
    CHECK(em_sdk_soft_float_w_0011FD78(&c, NULL) == -1 && c.fault == 0x0011FD78u);
    c = (EmSdkSoftFloatContext){ NULL, 0x00242670u, &errno_cell, 0 };
    CHECK(em_sdk_soft_float_w_0011FD78(&c, &cell) == -1 && c.fault == 0x0011FD78u);
    c = (EmSdkSoftFloatContext){ &ptr, 0x00242670u, NULL, 0 };
    CHECK(em_sdk_soft_float_w_0011FD78(&c, &cell) == -1 && c.fault == 0x0011FD78u);
    c = (EmSdkSoftFloatContext){ &ptr, 0x00242674u, &errno_cell, 0 };
    CHECK(em_sdk_soft_float_w_0011FD78(&c, &cell) == -1 && c.fault == 0x0011FD78u);
    c = (EmSdkSoftFloatContext){ &ptr, 0x00242670u, &errno_cell, 0 };
    cell = NULL;
    CHECK(em_sdk_soft_float_w_0011FD78(&c, &cell) == 0 && cell == &errno_cell && c.fault == 0);
    /* the first fault is kept */
    memset(&c, 0, sizeof c);
    CHECK(em_sdk_soft_float_w_00127758(&c, 0, NULL) == -1 && em_sdk_soft_float_w_00128350(&c, 0, NULL) == -1 &&
          c.fault == 0x00127758u);
    /* bind */
    EmSdkMathWorkers k;
    CHECK(em_sdk_soft_float_bind(NULL, &c) == -1 && em_sdk_soft_float_bind(&k, NULL) == -1);
    CHECK(em_sdk_soft_float_bind(&k, &c) == 0 && k.context == &c && k.w_00128350 == em_sdk_soft_float_w_00128350 &&
          k.w_0011DB90 == em_sdk_soft_float_w_0011DB90 && k.w_0011FD78 == em_sdk_soft_float_w_0011FD78 &&
          k.w_00127758 == em_sdk_soft_float_w_00127758);
    /* loader */
    CHECK(em_sdk_soft_float_load_d24295C(NULL, size, &w) == -1);
    CHECK(em_sdk_soft_float_load_d24295C(elf, size - 1, &w) == -1);
    CHECK(em_sdk_soft_float_load_d24295C(elf, size, NULL) == -1);
    static uint8_t bad[EM_SDK_MATH_ELF_SIZE];
    memcpy(bad, elf, size);
    bad[1] = 'X';
    CHECK(em_sdk_soft_float_load_d24295C(bad, size, &w) == -1);
    CHECK(em_sdk_soft_float_load_d24295C(elf, size, &w) == 0 && w == 0x00242670u);
    /* a bound wrapper whose errno cell is missing faults at 0011FD78 */
    const EmSdkMathTables *t = s_tables(elf, size);
    CHECK(t != NULL);
    int32_t mode = 1;
    EmSdkMathWorld world = { &mode };
    c = (EmSdkSoftFloatContext){ &ptr, 0x00242670u, NULL, 0 };
    CHECK(em_sdk_soft_float_bind(&k, &c) == 0);
    uint32_t fault = 0;
    CHECK(em_sdk_math_original_0011E620(t, &world, &k, 0.0f, 0.0f, &f, &fault) == -1 && fault == 0x0011FD78u &&
          c.fault == 0x0011FD78u);
    c = (EmSdkSoftFloatContext){ &ptr, 0x00242670u, &errno_cell, 0 };
    errno_cell = 0;
    fault = 0;
    CHECK(em_sdk_math_original_0011E748(t, &world, &k, -1.0f, &f, &fault) == 0 && fault == 0 &&
          errno_cell == 0x21);
#undef CHECK
    return n;
}
'''

U8P = C.POINTER(C.c_uint8)


def build_native():
    BUILD.mkdir(parents=True, exist_ok=True)
    source = BUILD / 'shim.c'
    source.write_text(SHIM)
    lib = BUILD / ('shim.dylib' if sys.platform == 'darwin' else 'shim.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                    '-ffp-contract=off', '-Isrc', str(source), SOURCE,
                    'src/game/em_sdk_math_original.c', '-o', str(lib)], cwd=ROOT, check=True)
    n = C.CDLL(str(lib))
    sig = {
        's_to_double': ([C.c_uint32], C.c_uint64),
        's_to_float': ([C.c_uint64], C.c_uint32),
        's_compare': ([C.c_uint64, C.c_uint64], C.c_int32),
        's_unpack_f': ([C.c_uint32, C.c_char_p], None),
        's_unpack_d': ([C.c_uint64, C.c_char_p], None),
        's_pack_d': ([C.c_char_p], C.c_uint64),
        's_pack_f': ([C.c_char_p], C.c_uint32),
        's_cmp_parts': ([C.c_char_p, C.c_char_p], C.c_int32),
        's_widen': ([C.c_uint32, C.c_uint32, C.c_int32, C.c_uint64], C.c_uint64),
        's_narrow': ([C.c_uint32, C.c_uint32, C.c_int32, C.c_uint32], C.c_uint32),
        's_matherr': ([C.c_char_p, C.POINTER(C.c_int32), C.POINTER(C.c_uint32)], C.c_int),
        's_errno': ([C.c_uint32, C.c_uint32, C.c_int, C.POINTER(C.c_uint32), C.POINTER(C.c_uint32)], C.c_int),
        's_wrap': ([C.c_int, C.c_uint32, C.c_uint32, C.c_int32, C.c_uint32, C.c_uint32,
                    C.POINTER(C.c_int32), C.c_void_p, C.POINTER(C.c_uint32), C.POINTER(C.c_uint32),
                    C.POINTER(C.c_uint32)], C.c_int),
        's_tables': ([C.c_char_p, C.c_size_t], C.c_void_p),
        's_fail_stop': ([C.c_char_p, C.c_size_t], C.c_int),
        's_load_export': ([C.c_char_p, C.POINTER(C.c_uint32), C.POINTER(C.c_int32)], C.c_int),
    }
    for name, (args, res) in sig.items():
        fn = getattr(n, name)
        fn.argtypes, fn.restype = args, res
    return n


# ------------------------------------------------------------------ cases --

F_SPECIAL = [0x00000000, 0x80000000, 0x00000001, 0x80000001, 0x007FFFFF, 0x807FFFFF, 0x00400000,
             0x00800000, 0x80800000, 0x00800001, 0x3F800000, 0xBF800000, 0x3F800001, 0x3FFFFFFF,
             0x7F7FFFFF, 0xFF7FFFFF, 0x7F800000, 0xFF800000, 0x7F800001, 0xFF800001, 0x7FC00000,
             0xFFC00000, 0x7FA00000, 0x7F900000, 0x7F8FFFFF, 0x7F9FFFFF, 0x7FEFFFFF, 0x7FFFFFFF,
             0xFFFFFFFF, 0x7F800100, 0xFF8FFFFF]


def float_words(rng):
    words = list(F_SPECIAL)
    for sign in (0, 0x80000000):
        for biased in range(256):
            for frac in (0, 1, 0x100000, 0x400000, 0x7FFFFF):
                words.append(sign | biased << 23 | frac)
    words += [rng.getrandbits(32) for _ in range(pick(1_000_000, 5_000))]
    return words


def double_edges(rng):
    """Doubles around every narrowing edge."""
    out = []
    lows = (0, 1, 0x200000, 0x3FFFFF)                        # the sticky bits (m bits 0..21)
    mids = (0, 1, 0x20, 0x3F)                                # m bits 22..27
    for sign in (0, 1):
        for biased in (0, 1, 0x3FF - 152, 0x3FF - 151, 0x3FF - 150, 0x3FF - 149, 0x3FF - 128,
                       0x3FF - 127, 0x3FF - 126, 0x3FF - 125, 0x3FF, 0x3FF + 126, 0x3FF + 127,
                       0x3FF + 128, 0x7FE, 0x7FF):
            for top in (0, (1 << 22) - 1, rng.getrandbits(22)):   # m bits 30..51
                for b29 in (0, 1):
                    for b28 in (0, 1):
                        for mid in mids:
                            for low in lows:
                                if not FULL and rng.random() < 0.85:
                                    continue
                                m = top << 30 | b29 << 29 | b28 << 28 | mid << 22 | low
                                out.append(sign << 63 | biased << 52 | m)
    return out


D_SPECIAL = [0, 1 << 63, 1, (1 << 63) | 1, 0x000FFFFFFFFFFFFF, 0x0010000000000000, 0x3FF0000000000000,
             0xBFF0000000000000, 0x3FF0000000000001, 0x7FEFFFFFFFFFFFFF, 0xFFEFFFFFFFFFFFFF,
             0x7FF0000000000000, 0xFFF0000000000000, 0x7FF8000000000000, 0xFFF8000000000000,
             0x7FF0000000000001, 0x7FF4000000000000, 0x7FF7FFFFFFFFFFFF, 0x7FFFFFFFFFFFFFFF,
             0xFFFFFFFFFFFFFFFF, 0x7FF0000040000000, 0x7FF000003FFFFFFF, 0x47EFFFFFE0000000,
             0x47EFFFFFF0000000, 0x47EFFFFFEFFFFFFF, 0x47F0000000000000, 0x3810000000000000,
             0x380FFFFFF0000000, 0x36A0000000000000, 0x3690000000000000, 0x36A0000000000001,
             0x3E999999A0000000, 0x400921FB54442D18]


def double_words(rng):
    words = list(D_SPECIAL) + double_edges(rng)
    words += [rng.getrandbits(64) for _ in range(pick(1_000_000, 5_000))]
    for _ in range(pick(200_000, 1_000)):                    # random values inside the float range
        words.append(rng.getrandbits(1) << 63 | rng.randrange(0x3FF - 160, 0x3FF + 130) << 52
                     | rng.getrandbits(52))
    return words


def compare_pairs(rng):
    base = D_SPECIAL + [0x3FF0000000000000 + i for i in (-1, 1, 2)] + [0x4000000000000000, 0xC000000000000000]
    pairs = [(a, b) for a in base for b in base]
    for _ in range(pick(400_000, 3_000)):
        a = rng.getrandbits(64)
        kind = rng.randrange(4)
        b = (a if kind == 0 else a ^ 1 if kind == 1 else a ^ (1 << 63) if kind == 2 else rng.getrandbits(64))
        pairs.append((a, b))
    return pairs


CLASSES = [0, 1, 2, 3, 4, 5, 7, 0xFFFFFFFF, 0x80000003]
SIGNS = [0, 1, 2, 3, 0x80000000, 0xFFFFFFFF]
EXPS_D = [-0x7FFFFFFF - 1, -0x7FFFFFFF, -1100, -1080, -1079, -1078, -1077, -1023, -1022, -1021, -1,
          0, 1, 1022, 1023, 1024, 1025, 0x7FFFFFFF]
EXPS_F = [-0x7FFFFFFF - 1, -200, -153, -152, -151, -150, -127, -126, -125, 0, 1, 126, 127, 128, 129,
          0x7FFFFFFF]


def frac64(rng):
    k = rng.randrange(8)
    if k == 0:
        return (1 << 60) | rng.getrandbits(52) << 8 | rng.choice((0, 0x80, 0x7F, 0x81, 0x180, 0xFF))
    if k == 1:
        return 0x1FFFFFFFFFFFFF00 | rng.choice((0x7F, 0x80, 0x81, 0xFF, 0x00))
    if k == 2:
        return rng.choice((0, 1, 0x80, 0x180, 1 << 63, MASK64, 0x2000000000000000, 1 << 51))
    return rng.getrandbits(64)


def frac32(rng):
    k = rng.randrange(8)
    if k == 0:
        return (1 << 30) | rng.getrandbits(23) << 7 | rng.choice((0, 0x40, 0x3F, 0x41, 0xC0, 0x7F))
    if k == 1:
        return 0x7FFFFF80 | rng.choice((0x3F, 0x40, 0x41, 0x7F, 0))
    if k == 2:
        return rng.choice((0, 1, 0x40, 0xC0, 0x80000000, MASK32, 0x100000, 0x7FFFFFFF))
    return rng.getrandbits(32)


def record_d(rng):
    cls = rng.choice(CLASSES) if rng.random() < 0.8 else rng.getrandbits(32)
    sign = rng.choice(SIGNS) if rng.random() < 0.8 else rng.getrandbits(32)
    exp = rng.choice(EXPS_D) if rng.random() < 0.6 else s32(rng.getrandbits(32)) if rng.random() < 0.3 \
        else rng.randrange(-1100, 1100)
    return struct.pack('<IIiIQ', cls, sign, exp, rng.getrandbits(32), frac64(rng))


def record_f(rng):
    cls = rng.choice(CLASSES) if rng.random() < 0.8 else rng.getrandbits(32)
    sign = rng.choice(SIGNS) if rng.random() < 0.8 else rng.getrandbits(32)
    exp = rng.choice(EXPS_F) if rng.random() < 0.6 else s32(rng.getrandbits(32)) if rng.random() < 0.3 \
        else rng.randrange(-200, 200)
    return struct.pack('<IIiI', cls, sign, exp, frac32(rng))


def chunks(items, parts=8):
    size = max(1, (len(items) + parts - 1) // parts)
    return [(i, items[i:i + size]) for i in range(0, len(items), size)]


# ------------------------------------------------------------------- jobs --

def conversions_job(job):
    """Parts 1 and 2: (kind, seed, words)."""
    kind, seed, words = job
    o, rng, misses, twice = shared(), random.Random(seed), [], 0
    for index, w in enumerate(words):
        o.fill_stack(rng)
        a2 = rng.getrandbits(64)
        if kind == 'd':
            want = o.go(TO_DOUBLE, floats=(w,), a2=a2).r[2] & MASK64
            got = NATIVE.s_to_double(w)
        else:
            want = o.go(TO_FLOAT, args=(s64(w),), a2=a2).f[0] & MASK32
            got = NATIVE.s_to_float(w)
        if want != got:
            misses.append((kind, hex(w), hex(want), hex(got)))
        if index % 4 == 0:                                   # a second stack fill must agree
            o.mem[FRAME_LOW:STACK] = bytes(b ^ 0xA5 for b in o.mem[FRAME_LOW:STACK])
            again = (o.go(TO_DOUBLE, floats=(w,), a2=~a2).r[2] & MASK64 if kind == 'd'
                     else o.go(TO_FLOAT, args=(s64(w),), a2=~a2).f[0] & MASK32)
            twice += 1
            if again != want:
                misses.append(('stack-dependent', kind, hex(w), hex(want), hex(again)))
    return misses, twice


def compare_job(job):
    _, pairs = job
    o, misses = shared(), []
    for a, b in pairs:
        want = s32(o.go(COMPARE, args=(s64(a), s64(b))).r[2])
        got = NATIVE.s_compare(a, b)
        if want != got:
            misses.append((hex(a), hex(b), want, got))
    return misses


def leaves_job(job):
    """Part 4 on one seed: every leaf on arbitrary inputs."""
    seed, count = job
    o, rng, misses = shared(), random.Random(seed), []
    o.fill_stack(rng)
    for _ in range(count):
        # 001278C0 / 00126BE8: every record byte, including the untouched ones.
        w = rng.choice(F_SPECIAL) if rng.random() < 0.2 else rng.getrandbits(32)
        fill = rng.getrandbits(128).to_bytes(16, 'little')
        o.store(WORD, w, 4)
        o.mem[REC_A:REC_A + 16] = fill
        o.go(UNPACK_F, args=(WORD, REC_A), allowed=[(REC_A, REC_A + 16)])
        mine = C.create_string_buffer(fill, 16)
        NATIVE.s_unpack_f(w, mine)
        if bytes(o.mem[REC_A:REC_A + 16]) != mine.raw[:16]:
            misses.append(('001278C0', hex(w)))
        v = rng.choice(D_SPECIAL) if rng.random() < 0.2 else rng.getrandbits(64)
        fill = rng.getrandbits(192).to_bytes(24, 'little')
        o.store(WORD, v, 8)
        o.mem[REC_A:REC_A + 24] = fill
        o.go(UNPACK_D, args=(WORD, REC_A), allowed=[(REC_A, REC_A + 24)])
        mine = C.create_string_buffer(fill, 24)
        NATIVE.s_unpack_d(v, mine)
        if bytes(o.mem[REC_A:REC_A + 24]) != mine.raw[:24]:
            misses.append(('00126BE8', hex(v)))
        # 00126AB8 / 001277B0 on arbitrary records, with a random stale a2.
        rd = record_d(rng)
        o.mem[REC_A:REC_A + 24] = rd
        want = o.go(PACK_D, args=(REC_A,), a2=rng.getrandbits(64)).r[2] & MASK64
        got = NATIVE.s_pack_d(rd)
        if want != got:
            misses.append(('00126AB8', rd.hex(), hex(want), hex(got)))
        rf = record_f(rng)
        o.mem[REC_A:REC_A + 16] = rf
        want = o.go(PACK_F, args=(REC_A,), a2=rng.getrandbits(64)).f[0] & MASK32
        got = NATIVE.s_pack_f(rf)
        if want != got:
            misses.append(('001277B0', rf.hex(), hex(want), hex(got)))
        # 00127398 on arbitrary record pairs (sometimes sharing fields).
        ra, rb = record_d(rng), record_d(rng)
        if rng.random() < 0.5:
            rb = ra[:4] + rb[4:] if rng.random() < 0.5 else ra[:8] + rb[8:]
            if rng.random() < 0.5:
                rb = rb[:16] + ra[16:]
        o.mem[REC_A:REC_A + 24] = ra
        o.mem[REC_B:REC_B + 24] = rb
        want = s32(o.go(CMP_PARTS, args=(REC_A, REC_B)).r[2])
        got = NATIVE.s_cmp_parts(ra, rb)
        if want != got:
            misses.append(('00127398', ra.hex(), rb.hex(), want, got))
        # 00127728 / 00128320 on their four arguments.
        c, s, e, _, f = struct.unpack('<IIiIQ', record_d(rng))
        want = o.go(WIDEN, args=(s32(c), s32(s), e, s64(f))).r[2] & MASK64
        got = NATIVE.s_widen(c, s, e, f)
        if want != got:
            misses.append(('00127728', c, s, e, hex(f), hex(want), hex(got)))
        c, s, e, f = struct.unpack('<IIiI', record_f(rng))
        want = o.go(NARROW, args=(s32(c), s32(s), e, s32(f))).f[0] & MASK32
        got = NATIVE.s_narrow(c, s, e, f)
        if want != got:
            misses.append(('00128320', c, s, e, hex(f), hex(want), hex(got)))
    return misses


def matherr_cases(rng):
    out = []
    for arg in D_SPECIAL + [rng.getrandbits(64) for _ in range(pick(20_000, 150))]:
        out.append(struct.pack('<iIQQQi', 1, rng.choice((0x26C640, 0x26C648, rng.getrandbits(32))), arg,
                               rng.getrandbits(64), rng.choice((0, 0x7FF8000000000000, rng.getrandbits(64))),
                               rng.choice((0, 0, 1, -1))))
    return out


def matherr_job(job):
    _, records = job
    o, misses = shared(), []
    for rec in records:
        o.mem[REC_A:REC_A + 0x24] = rec
        want = s32(o.go(MATHERR, args=(REC_A,)).r[2])
        after = bytes(o.mem[REC_A:REC_A + 0x24])
        buf = C.create_string_buffer(rec, 0x24)
        result, fault = C.c_int32(99), C.c_uint32()
        rc = NATIVE.s_matherr(buf, C.byref(result), C.byref(fault))
        if want != 0 or after != rec or rc != 0 or result.value != 0 or buf.raw[:0x24] != rec or fault.value:
            misses.append(('0011DB90', rec.hex(), want, rc, result.value))
    return misses


def errno_checks():
    o = shared()
    saved = o.load(D_ERRNO_PTR, 4)
    checks = 0
    for value in (saved, 0x00242674, 0, 0x01E20000, 0xFFFFFFFC):
        o.store(D_ERRNO_PTR, value, 4)
        want = o.go(ERRNO).r[2] & MASK32
        assert want == value, ('0011FD78 does not return D_0024295C', hex(value), hex(want))
        handed, fault = C.c_uint32(), C.c_uint32()
        rc = NATIVE.s_errno(value, saved, 1, C.byref(handed), C.byref(fault))
        if value == saved:
            assert rc == 0 and handed.value == want and fault.value == 0, ('0011FD78 adapter', hex(value))
        else:   # no host cell stands for that address: fail-stop
            assert rc == -1 and fault.value == 0x0011FD78, ('0011FD78 adapter', hex(value), rc)
        checks += 1
    o.store(D_ERRNO_PTR, saved, 4)
    return checks


# ------------------------------------------------------------- wrappers --

EE_ZERO = [0x00000000, 0x80000000, 0x00000001, 0x80000001, 0x007FFFFF, 0x807FFFFF, 0x00400000]
MODES = (-1, 0, 1, 2, 5)


def wrapper_cases(rng):
    cases = []
    for y in EE_ZERO:
        for x in EE_ZERO:
            cases.append((0, y, x))
    others = [0x3F800000, 0xBF800000, 0x00800000, 0x80800000, 0x7F800000, 0xFF800000, 0x7FC00000,
              0xFFC00000, 0x7F800001, 0x7F7FFFFF]
    for y in others:
        for x in EE_ZERO[:3] + others[:4]:
            cases.append((0, y, x))
            cases.append((0, x, y))
    roots = [0xBF800000, 0x80800000, 0xFF7FFFFF, 0xFF800000, 0xC2C80000, 0x80000001, 0x807FFFFF,
             0x80000000, 0x00000000, 0x3F800000, 0x7F800000, 0x7FC00000, 0xFFC00000, 0xFF800001,
             0x40490FDB, 0x00000001]
    roots += [0x80000000 | rng.randrange(0x00800000, 0x7F800000) for _ in range(pick(400, 12))]
    for x in roots:
        cases.append((1, x, 0))
    return [(which, a, b, mode) for which, a, b in cases for mode in MODES]


def wrappers_job(job):
    """(label, ram path or None, cases)."""
    _, ram_path, cases = job
    o = shared() if ram_path is None else Oracle(bytearray(Path(ram_path).read_bytes()), ELF, SIZES)
    tables = NATIVE.s_tables(ELF, len(ELF))
    assert tables, 'SDK tables'
    pointer = o.load(D_ERRNO_PTR, 4)
    saved_mode, saved_errno = o.load(D_MODE, 4), o.load(pointer, 4)
    misses, edom = [], 0
    for which, a, b, mode in cases:
        o.store(D_MODE, mode, 4)
        o.store(pointer, 0x5EED, 4)
        o.go(W_ATAN2 if which == 0 else W_SQRT, floats=(a, b) if which == 0 else (a,),
             allowed=[(pointer, pointer + 4)])
        want = (o.f[0] & MASK32, o.load(pointer, 4, True))
        cell = C.c_int32(0x5EED)
        out, fault, cfault = C.c_uint32(), C.c_uint32(), C.c_uint32()
        rc = NATIVE.s_wrap(which, a, b, mode, pointer, pointer, C.byref(cell), tables, C.byref(out),
                           C.byref(fault), C.byref(cfault))
        got = (out.value, cell.value)
        if rc != 0 or fault.value or cfault.value or got != want:
            misses.append((which, hex(a), hex(b), mode, want, rc, hex(fault.value), got))
        edom += want[1] == 0x21
    o.store(D_MODE, saved_mode, 4)
    o.store(pointer, saved_errno, 4)
    return misses, edom


# ------------------------------------------------------------------ main --

def main():
    global ELF, RAM, SIZES, NATIVE
    ELF = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(ELF).hexdigest() == SM.ELF_SHA, 'unexpected boot ELF'
    RAM = (REF / 'playable_ee.bin').read_bytes()
    with open(DECOMP / 'docs/FUNCTIONS.csv') as fh:
        SIZES = {int(row['vram'], 16): int(row['size_bytes']) for row in csv.DictReader(fh)}
    assert struct.unpack_from('<I', RAM, D_ERRNO_PTR)[0] == 0x242670, 'D_0024295C differs in the capture'
    assert struct.unpack_from('<i', RAM, D_MODE)[0] == 1, 'D_0026C5D0 is not 1 in the capture'
    NATIVE = build_native()
    rng = random.Random(0x128350)
    counts = {}

    checks = NATIVE.s_fail_stop(ELF, len(ELF))
    assert checks > 0, ('fail-stop check failed', -checks)
    counts['fail-stop'] = checks

    jobs = []
    fwords, dwords = float_words(rng), double_words(rng)
    for kind, words in (('d', fwords), ('f', dwords)):
        for i, part in chunks(words):
            jobs.append((kind, rng.getrandbits(32), part))
    parts = parallel_map(conversions_job, jobs, cost=lambda j: len(j[2]))
    misses = [m for part, _ in parts for m in part]
    assert not misses, ('conversions', len(misses), misses[:6])
    twice = sum(n for _, n in parts)
    counts['00128350'] = len(fwords)
    counts['00127758'] = len(dwords)
    counts['second stack fill'] = twice

    pairs = compare_pairs(rng)
    misses = [m for part in parallel_map(compare_job, chunks(pairs)) for m in part]
    assert not misses, ('001274B0', len(misses), misses[:6])
    counts['001274B0'] = len(pairs)

    per = pick(40_000, 400)
    leaf_jobs = [(rng.getrandbits(32), per) for _ in range(8)]
    misses = [m for part in parallel_map(leaves_job, leaf_jobs) for m in part]
    assert not misses, ('leaves', len(misses), misses[:6])
    counts['leaves (7 functions each)'] = per * len(leaf_jobs)

    records = matherr_cases(rng)
    misses = [m for part in parallel_map(matherr_job, chunks(records)) for m in part]
    assert not misses, ('0011DB90', len(misses), misses[:4])
    counts['0011DB90'] = len(records)
    counts['0011FD78'] = errno_checks()

    wc = wrapper_cases(rng)
    parts = parallel_map(wrappers_job, [('capture', None, part) for _, part in chunks(wc)])
    misses = [m for part, _ in parts for m in part]
    assert not misses, ('wrappers with this module bound', len(misses), misses[:4])
    edom = sum(n for _, n in parts)
    assert edom, 'no wrapper case reached the domain-error tail'
    counts['wrappers bound'] = len(wc)

    # Route snapshots.
    beats = sorted(p for p in ROUTE.iterdir() if (p / 'eeMemory.bin').is_file()) if ROUTE.is_dir() else []
    assert beats, ('route captures missing', str(ROUTE))
    errnos = []
    for beat in beats:
        with open(beat / 'eeMemory.bin', 'rb') as fh:
            fh.seek(D_ERRNO_PTR)
            pointer = struct.unpack('<I', fh.read(4))[0]
            fh.seek(D_MODE)
            mode = struct.unpack('<i', fh.read(4))[0]
            fh.seek(pointer)
            value = struct.unpack('<i', fh.read(4))[0]
        assert pointer == 0x242670 and mode == 1, ('route data', beat.name, hex(pointer), mode)
        assert value in (0, 0x21), ('errno word outside {0, EDOM}', beat.name, hex(value))
        errnos.append((beat.name, value))
    first = next((name for name, value in errnos if value == 0x21), None)
    assert first is not None and first.startswith('03_'), ('the first EDOM beat moved', first)
    assert all(v == 0x21 for name, v in errnos if name >= first), 'errno cleared after beat 03'
    replay = [c for c in wc if c[3] == 1 and ((c[0] == 0 and c[1] in EE_ZERO and c[2] in EE_ZERO)
                                            or (c[0] == 1 and c[1] >> 31))]
    route_beats = [b for b in beats if FULL or b.name.startswith('03_')]
    parts = parallel_map(wrappers_job, [(b.name, str(b / 'eeMemory.bin'), replay) for b in route_beats])
    misses = [m for part, _ in parts for m in part]
    assert not misses, ('route RAM wrappers', len(misses), misses[:4])
    counts['route RAM wrapper cases'] = len(replay) * len(route_beats)

    # The runtime's export.
    asset = ROOT / 'assets/sdk_soft_float.emsf'
    assert asset.is_file(), f'{asset} missing (run tools/export_sdk_math_tables.py)'
    elf_word = lambda a: struct.unpack_from('<I', ELF, a - 0x100000 + 0x300)[0]
    pointer = elf_word(D_ERRNO_PTR)
    cell = elf_word(pointer)
    assert (pointer, cell) == (struct.unpack_from('<I', RAM, D_ERRNO_PTR)[0],
                               struct.unpack_from('<I', RAM, pointer)[0]), 'ELF and first-control RAM differ'
    assert asset.read_bytes() == struct.pack('<4s5I', b'EMSF', 1, D_ERRNO_PTR, pointer, pointer, cell), \
        f'{asset} is not the ELF data (run tools/export_sdk_math_tables.py)'
    loaded_pointer, loaded_word = C.c_uint32(), C.c_int32()
    assert NATIVE.s_load_export(str(asset).encode(), C.byref(loaded_pointer), C.byref(loaded_word)) == 0
    assert (loaded_pointer.value, loaded_word.value) == (pointer, cell), 'export loader'
    assert all(errno == 0 for name, errno in errnos if name < '03_'), 'errno before beat 03'
    tables = NATIVE.s_tables(ELF, len(ELF))
    o = shared()
    export_cases = export_edom = 0
    for which, a, b, mode in replay:
        o.store(D_MODE, mode, 4)
        o.store(pointer, cell, 4)
        o.go(W_ATAN2 if which == 0 else W_SQRT, floats=(a, b) if which == 0 else (a,),
             allowed=[(pointer, pointer + 4)])
        want = (o.f[0] & MASK32, o.load(pointer, 4, True))
        word = C.c_int32(loaded_word.value)
        out, fault, cfault = C.c_uint32(), C.c_uint32(), C.c_uint32()
        rc = NATIVE.s_wrap(which, a, b, mode, loaded_pointer.value, loaded_pointer.value, C.byref(word),
                           tables, C.byref(out), C.byref(fault), C.byref(cfault))
        assert rc == 0 and not fault.value and not cfault.value and (out.value, word.value) == want, \
            ('export-bound wrapper', which, hex(a), hex(b), mode, want, rc, out.value, word.value)
        export_cases += 1
        export_edom += want[1] == 0x21
    assert export_edom, 'no export-bound case reached the domain-error tail'
    o.store(D_MODE, 1, 4)
    o.store(pointer, cell, 4)
    counts['export (data + loader + bound wrappers)'] = 2 + export_cases

    print('route errno word (0x242670) at the end of each beat: ' +
          ', '.join(f'{name[:2]} {value:#x}' for name, value in errnos))
    # Which wrapper: the route census (decomp tools/route_census.py, one-shot
    # breakpoints on every boot function per label) never hit 0011E420 or
    # 0011E520, so the tail was 0011E620's or 0011E748's; its workers first
    # ran in beat 03 (and again in 05).
    census = DECOMP / 'build/s87/census/per_beat.json'
    tails = '0011E420/0011E520/0011E620/0011E748'
    if census.is_file():
        import json
        ran = {b['label']: {int(f, 16) for f in b['functions']}
               for b in json.loads(census.read_text())['beats']}
        assert not any(0x11E420 in f or 0x11E520 in f for f in ran.values()), 'census: 0011E420/0011E520 ran'
        workers = (MATHERR, ERRNO, TO_FLOAT)
        with_workers = [label for label, f in ran.items() if all(w in f for w in workers)]
        assert with_workers and with_workers[0].startswith('03_'), ('census: worker beats', with_workers)
        assert all(W_ATAN2 in f and W_SQRT in f for label, f in ran.items() if label in with_workers)
        tails = '0011E620 atan2f or 0011E748 sqrtf; the census never hit 0011E420/0011E520'
        print('census: 0011DB90 + 0011FD78 + 00127758 ran in ' + ', '.join(with_workers))
    else:
        print(f'census {census} missing: the wrapper attribution is not checked')
    print(f'first EDOM at the end of beat {first}: a domain-error tail ({tails}) ran during that beat')
    banner(', '.join(f'{k} {v:,}' for k, v in counts.items()),
           f'{sum(counts.values()):,} cases equal to the original instructions')


if __name__ == '__main__':
    main()
