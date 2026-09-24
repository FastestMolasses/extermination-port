#!/usr/bin/env python3
"""Check the native header src/game/em_ee_float.h against the recorded
ORIGINAL results and against tools/ee_float_model.py.

1. Hygiene: the header alone compiles with -std=c11 -Wall -Wextra -Wpedantic
   -Wconversion -Wsign-conversion -Wshadow -Werror (twice included, and with
   -ffp-contract=fast), and the compiled shim contains no host floating-point
   instruction that rounds (arithmetic, FMA, conversion, min/max; arm64 and
   x86-64), so no host rounding mode, FTZ/DAZ state or FMA contraction can
   reach a result. Compares are allowed: clang turns the raw-bit NaN tests
   into an unordered compare, which is exact in every mode.
2. Every recorded original result (ee_float_model.load_vectors: all 33,800,
   in both the _bits and the float API) and every free-running block run
   (blockcheck.json) is reproduced by the header. A missing recording is a
   hard failure, never a skip.
3. The VU form table and the VDIV forms equal ee_float_model.VU_FORMS /
   VU_DIV_FORMS for every (op, dest, bc) / (fsf, ftf); unmeasured forms and
   missing operands are refused without writing anything.
4. A seeded random differential against the Python model for every op and
   every VU form (special values, near exponents, cancellations, FTZ and
   saturation boundaries). EM_TEST_FULL=1 runs a sweep 40x larger.

The shim is generated and built under build/ee_float_header/ with the
undefined-behaviour sanitizer in trap mode when the compiler supports it.
"""
import ctypes as C
import json
import os
import platform
import random
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import ee_float_model as model  # noqa: E402
from reference_mode import banner, parallel_map, pick  # noqa: E402

HEADER = ROOT / 'src/game/em_ee_float.h'
OUT = ROOT / 'build/ee_float_header'
CC = os.environ.get('CC', 'cc')
RECORDINGS = ('fpu_acc', 'fpu_arith', 'fpu_misc', 'vu', 'vu_signatures')
MASK = 0xFFFFFFFF
ONE = 0x3F800000
UNMEASURED, NO_OPERAND = -1, -2

# Shim op codes (scalar functions of one to three words).
SCALAR = {
    'add.s': 0, 'sub.s': 1, 'mul.s': 2, 'div.s': 3, 'madd.s': 4, 'msub.s': 5,
    'adda.s': 6, 'suba.s': 7, 'mula.s': 8, 'neg.s': 9, 'mov.s': 10,
    'cvt.w.s': 11, 'cvt.s.w': 12, 'c.eq.s': 13, 'c.lt.s': 14, 'c.le.s': 15,
    'vsqrt': 16, 'vftoi0': 17, 'vftoi4': 18, 'vitof0': 19, 'vitof4': 20,
    'vmax': 21, 'vmini': 22, 'vabs': 23,
}
# The header's em_vu_op order.
VU_OPS = ('vadd', 'vaddbc', 'vaddq', 'vsub', 'vsubbc', 'vmul', 'vmulbc', 'vmulq',
          'vmulabc', 'vmaddbc', 'vmaddabc', 'vopmula', 'vopmsub')
VU_CODE = {name: i for i, name in enumerate(VU_OPS)}
ACC_OUT = ('vmulabc', 'vmaddabc', 'vopmula')
VF0 = [0, 0, 0, ONE]

SHIM = r'''
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "game/em_ee_float.h"

#define SHIM_EXPORT __attribute__((visibility("default")))
static float F(uint32_t b) { return em_ee_float(b); }
static uint32_t B(float f) { return em_ee_bits(f); }
static uint32_t W(int32_t i) { return em_ee_int_word(i); }
static int32_t I(uint32_t w) { return em_ee_word_int(w); }

static uint32_t scalar(int op, int via_float, uint32_t x, uint32_t y, uint32_t z)
{
    if (!via_float) {
        switch (op) {
        case 0: return em_ee_add_bits(x, y);
        case 1: return em_ee_sub_bits(x, y);
        case 2: return em_ee_mul_bits(x, y);
        case 3: return em_ee_div_bits(x, y);
        case 4: return em_ee_madd_bits(x, y, z);
        case 5: return em_ee_msub_bits(x, y, z);
        case 6: return em_ee_adda_bits(x, y);
        case 7: return em_ee_suba_bits(x, y);
        case 8: return em_ee_mula_bits(x, y);
        case 9: return em_ee_neg_bits(x);
        case 10: return em_ee_mov_bits(x);
        case 11: return em_ee_cvt_w_s_bits(x);
        case 12: return em_ee_cvt_s_w_bits(x);
        case 13: return (uint32_t)em_ee_c_eq_bits(x, y);
        case 14: return (uint32_t)em_ee_c_lt_bits(x, y);
        case 15: return (uint32_t)em_ee_c_le_bits(x, y);
        case 16: return em_vu_sqrt_bits(x);
        case 17: return em_vu_ftoi0_bits(x);
        case 18: return em_vu_ftoi4_bits(x);
        case 19: return em_vu_itof0_bits(x);
        case 20: return em_vu_itof4_bits(x);
        case 21: return em_vu_max_bits(x, y);
        case 22: return em_vu_min_bits(x, y);
        case 23: return em_vu_abs_bits(x);
        default: return 0xDEADBEEFu;
        }
    }
    switch (op) {
    case 0: return B(em_ee_add(F(x), F(y)));
    case 1: return B(em_ee_sub(F(x), F(y)));
    case 2: return B(em_ee_mul(F(x), F(y)));
    case 3: return B(em_ee_div(F(x), F(y)));
    case 4: return B(em_ee_madd(F(x), F(y), F(z)));
    case 5: return B(em_ee_msub(F(x), F(y), F(z)));
    case 6: return B(em_ee_adda(F(x), F(y)));
    case 7: return B(em_ee_suba(F(x), F(y)));
    case 8: return B(em_ee_mula(F(x), F(y)));
    case 9: return B(em_ee_neg(F(x)));
    case 10: return B(em_ee_mov(F(x)));
    case 11: return W(em_ee_cvt_w_s(F(x)));
    case 12: return B(em_ee_cvt_s_w(I(x)));
    case 13: return (uint32_t)em_ee_c_eq(F(x), F(y));
    case 14: return (uint32_t)em_ee_c_lt(F(x), F(y));
    case 15: return (uint32_t)em_ee_c_le(F(x), F(y));
    case 16: return B(em_vu_sqrt(F(x)));
    case 17: return W(em_vu_ftoi0(F(x)));
    case 18: return W(em_vu_ftoi4(F(x)));
    case 19: return B(em_vu_itof0(I(x)));
    case 20: return B(em_vu_itof4(I(x)));
    case 21: return B(em_vu_max(F(x), F(y)));
    case 22: return B(em_vu_min(F(x), F(y)));
    case 23: return B(em_vu_abs(F(x)));
    default: return 0xDEADBEEFu;
    }
}

SHIM_EXPORT void shim_scalar(int op, int via_float, size_t n, const uint32_t *x, const uint32_t *y,
                             const uint32_t *z, uint32_t *out)
{
    for (size_t i = 0; i < n; i++)
        out[i] = scalar(op, via_float, x[i], y ? y[i] : 0, z ? z[i] : 0);
}

/* via 0: em_vu_lane_bits, 1: em_vu_lane (float), 2: em_vu_form_lookup + em_vu_form_lane_bits */
SHIM_EXPORT void shim_vu_lane(int op, unsigned dest, int bc, int via, size_t n, const uint32_t *s,
                              const uint32_t *t, const uint32_t *acc, uint32_t *out, int *status)
{
    for (size_t i = 0; i < n; i++) {
        if (via == 0) {
            status[i] = em_vu_lane_bits((em_vu_op)op, dest, bc, s[i], t[i], acc[i], &out[i]);
        } else if (via == 1) {
            float r;
            memcpy(&r, &out[i], sizeof r);
            status[i] = em_vu_lane((em_vu_op)op, dest, bc, F(s[i]), F(t[i]), F(acc[i]), &r);
            memcpy(&out[i], &r, sizeof r);
        } else {
            em_vu_form form;
            status[i] = em_vu_form_lookup((em_vu_op)op, dest, bc, &form);
            if (status[i] == EM_EE_FLOAT_OK) out[i] = em_vu_form_lane_bits(&form, s[i], t[i], acc[i]);
        }
    }
}

SHIM_EXPORT int shim_vu_vec(int op, unsigned dest, int bc, int via_float, const uint32_t *fs,
                            const uint32_t *ft, uint32_t q, const uint32_t *acc, uint32_t *dst)
{
    if (!via_float) return em_vu_vec_bits((em_vu_op)op, dest, bc, fs, ft, q, acc, dst);
    float s[4], t[4], a[4], d[4];
    if (fs) memcpy(s, fs, sizeof s);
    if (ft) memcpy(t, ft, sizeof t);
    if (acc) memcpy(a, acc, sizeof a);
    if (dst) memcpy(d, dst, sizeof d);
    int status = em_vu_vec((em_vu_op)op, dest, bc, fs ? s : NULL, ft ? t : NULL, F(q), acc ? a : NULL,
                           dst ? d : NULL);
    if (dst) memcpy(dst, d, sizeof d);
    return status;
}

SHIM_EXPORT int shim_vu_form(int op, unsigned dest, int bc, int *flags)
{
    em_vu_form form;
    memset(&form, 0xA5, sizeof form);
    int status = em_vu_form_lookup((em_vu_op)op, dest, bc, &form);
    if (status == EM_EE_FLOAT_OK) {
        flags[0] = form.clamp_fs;
        flags[1] = form.clamp_ft;
        flags[2] = form.clamp_acc;
        flags[3] = form.product_nan_first;
        flags[4] = (int)form.op;
    }
    return status;
}

SHIM_EXPORT void shim_vu_div(int fsf, int ftf, int via_float, size_t n, const uint32_t *a,
                             const uint32_t *b, uint32_t *q, int *status)
{
    for (size_t i = 0; i < n; i++) {
        if (!via_float) {
            status[i] = em_vu_div_bits(a[i], b[i], fsf, ftf, &q[i]);
        } else {
            float r;
            memcpy(&r, &q[i], sizeof r);
            status[i] = em_vu_div(F(a[i]), F(b[i]), fsf, ftf, &r);
            memcpy(&q[i], &r, sizeof r);
        }
    }
}
'''

HYGIENE = ['-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-Wsign-conversion',
           '-Wshadow', '-Wcast-qual', '-Wstrict-prototypes', '-Werror']
# Host floating-point instructions that round or flush (moves, sign-bit
# operations and compares are exact in every mode and allowed).
FP_ARITH = {
    'arm64': re.compile(r'\s(fadd|fsub|fmul|fnmul|fdiv|fsqrt|f[n]?madd|f[n]?msub|fmla|fmls|fcvt\w*|[su]cvtf|'
                        r'frint\w*|fmax\w*|fmin\w*|fabd|faddp|frecp\w*|frsqrt\w*)\b'),
    'x86_64': re.compile(r'\s(v?(add|sub|mul|div|sqrt|max|min)(ss|sd|ps|pd)|v?cvt\w+|v?round(ss|sd|ps|pd)|'
                         r'vf[n]?m(add|sub)\w*|f(add|sub|mul|div|sqrt|ld|stp|ild|istp?)\w*)\b'),
}


def run(cmd):
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode:
        raise SystemExit(f'FAIL: {" ".join(map(str, cmd))}\n{proc.stdout}{proc.stderr}')
    return proc.stdout


def build():
    """Hygiene compiles, then the ctypes shim; returns (lib, notes)."""
    if not HEADER.exists():
        raise SystemExit(f'FAIL: missing {HEADER}')
    OUT.mkdir(parents=True, exist_ok=True)
    notes = []
    twice = OUT / 'hygiene.c'
    twice.write_text('#include "game/em_ee_float.h"\n#include "game/em_ee_float.h"\n')
    for extra in ([], ['-O2', '-ffp-contract=fast']):
        run([CC, *HYGIENE, *extra, '-fsyntax-only', '-I', str(ROOT / 'src'), str(twice)])
    shim = OUT / 'shim.c'
    shim.write_text(SHIM)
    lib_path = OUT / ('shim.dylib' if sys.platform == 'darwin' else 'shim.so')
    base = [CC, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC',
            '-dynamiclib' if sys.platform == 'darwin' else '-shared', '-I', str(ROOT / 'src'),
            str(shim), '-o', str(lib_path)]
    trap = ['-fsanitize=undefined', '-fsanitize-trap=undefined']
    if subprocess.run(base[:1] + trap + base[1:], capture_output=True).returncode:
        run(base)
        notes.append('UB sanitizer unavailable')
    else:
        notes.append('UB-trap shim')
    arch = {'aarch64': 'arm64', 'amd64': 'x86_64'}.get(platform.machine().lower(), platform.machine().lower())
    objdump = shutil.which('objdump') or shutil.which('llvm-objdump')
    if arch in FP_ARITH and objdump:
        listing = run([objdump, '-d', '--no-show-raw-insn', str(lib_path)])
        hits = sorted({m.group(1) for m in FP_ARITH[arch].finditer(listing)})
        if hits:
            raise SystemExit(f'FAIL: rounding host floating-point instructions in the compiled header: {hits}')
        notes.append('no rounding host FP instruction')
    else:
        notes.append('FP-instruction scan skipped (no objdump or unknown arch)')
    lib = C.CDLL(str(lib_path))
    u32p, intp = C.POINTER(C.c_uint32), C.POINTER(C.c_int)
    lib.shim_scalar.argtypes = [C.c_int, C.c_int, C.c_size_t, u32p, u32p, u32p, u32p]
    lib.shim_vu_lane.argtypes = [C.c_int, C.c_uint, C.c_int, C.c_int, C.c_size_t, u32p, u32p, u32p, u32p, intp]
    lib.shim_vu_vec.argtypes = [C.c_int, C.c_uint, C.c_int, C.c_int, u32p, u32p, C.c_uint32, u32p, u32p]
    lib.shim_vu_vec.restype = C.c_int
    lib.shim_vu_form.argtypes = [C.c_int, C.c_uint, C.c_int, intp]
    lib.shim_vu_form.restype = C.c_int
    lib.shim_vu_div.argtypes = [C.c_int, C.c_int, C.c_int, C.c_size_t, u32p, u32p, u32p, intp]
    return lib, notes


LIB = None


def arr(values, ctype=C.c_uint32):
    return (ctype * len(values))(*values) if values is not None else None


def c_scalar(op, xs, ys=None, zs=None, via=0):
    n = len(xs)
    out = (C.c_uint32 * n)()
    LIB.shim_scalar(SCALAR[op], via, n, arr(xs), arr(ys), arr(zs), out)
    return list(out)


def c_lanes(op, dest, bc, ss, ts, accs, via=0, sentinel=0x5A5A5A5A):
    n = len(ss)
    out = (C.c_uint32 * n)(*([sentinel] * n))
    status = (C.c_int * n)()
    LIB.shim_vu_lane(VU_CODE[op] if isinstance(op, str) else op, dest, -1 if bc is None else bc, via, n,
                     arr(ss), arr(ts), arr(accs), out, status)
    return list(out), list(status)


def c_vec(op, dest, bc, fs, ft, q, acc, dst, via=0):
    d = arr(list(dst)) if dst is not None else None
    status = LIB.shim_vu_vec(VU_CODE[op] if isinstance(op, str) else op, dest, -1 if bc is None else bc, via,
                             arr(fs), arr(ft), q or 0, arr(acc), d)
    return status, (list(d) if d is not None else None)


def c_div(fsf, ftf, a, b, via=0, sentinel=0x5A5A5A5A):
    n = len(a)
    q = (C.c_uint32 * n)(*([sentinel] * n))
    status = (C.c_int * n)()
    LIB.shim_vu_div(fsf, ftf, via, n, arr(a), arr(b), q, status)
    return list(q), list(status)


# ------------------------------------------------ recorded original vectors --

EE_BINARY = ('add.s', 'sub.s', 'mul.s', 'div.s', 'c.eq.s', 'c.lt.s', 'c.le.s')
EE_UNARY = ('cvt.w.s', 'cvt.s.w', 'neg.s', 'mov.s')
EE_ACC = ('adda.s', 'suba.s', 'mula.s')
EE_MACC = ('madd.s', 'msub.s')
UNARY_VU = ('vabs', 'vftoi0', 'vftoi4', 'vitof0', 'vitof4')


def c_predict(rec, via):
    """The header's result for one recorded vector, in the record's own shape
    (the same dispatch as ee_float_model.predict)."""
    op = rec['op']
    one = lambda name, *words: c_scalar(name, *[[w] for w in words], via=via)[0]  # noqa: E731
    if op in EE_BINARY:
        return one(op, rec['fs'], rec['ft'])
    if op in EE_UNARY:
        return one(op, rec['fs'])
    if op in EE_MACC:      # ACC <- MULA.S(acc, 1.0); fd <- op(ACC, fs, ft)
        return one(op, one('mula.s', rec['acc'], ONE), rec['fs'], rec['ft'])
    if op in EE_ACC:       # ACC <- op(fs, ft); fd <- MSUB.S(ACC, +0, +0)
        return one('msub.s', one(op, rec['fs'], rec['ft']), 0, 0)
    if 'sig' in rec:
        dest, bc, fsf, ftf = rec.get('dest'), rec.get('bc'), rec.get('fsf'), rec.get('ftf')
        s = rec['vs']
    else:
        inst = model._instance(op)
        dest, bc, fsf, ftf = inst['dest'], inst['bc'], inst['fsf'], inst['ftf']
        s = VF0 if op == 'vaddq' else rec['vs']
    if op == 'vdiv':
        q, status = c_div(fsf, ftf, [rec['vs'][fsf]], [rec['vt'][ftf]], via)
        return q[0] if status[0] == 0 else ('status', status[0])
    if op == 'vsqrt':
        return one('vsqrt', rec['vt'][ftf])
    fill = rec['acc_in'] if op in ACC_OUT else (rec['prior'] if rec['prior'] is not None else rec['vd'])
    if op in VU_CODE:
        status, out = c_vec(op, dest, bc if op.endswith('bc') else None, s, rec['vt'], rec['q_in'],
                            rec['acc_in'], fill, via)
        return out if status == 0 else ('status', status)
    out = []
    for k in range(4):
        if not (dest >> (3 - k)) & 1:
            out.append(fill[k])
        elif op in UNARY_VU:
            out.append(one(op, s[k]))
        elif op in ('vmaxbc', 'vminibc'):
            out.append(one('vmax' if op == 'vmaxbc' else 'vmini', s[k], rec['vt'][bc]))
        else:
            raise SystemExit(f'FAIL: recorded op {op} has no header function')
    return out


def check_recordings(failures):
    for name in RECORDINGS:
        if not (model.VECTORS / f'{name}.jsonl').exists():
            raise SystemExit(f'FAIL: missing recording {model.VECTORS / name}.jsonl '
                             '(record with ../Extermination/tools/ee_float/battery.py and vusig.py)')
    recs = model.load_vectors()
    ops = {r['op'] for r in recs}
    expected = set(EE_BINARY + EE_UNARY + EE_ACC + EE_MACC + UNARY_VU) | set(VU_CODE) | {
        'vdiv', 'vsqrt', 'vmaxbc', 'vminibc'}
    if ops != expected:
        raise SystemExit(f'FAIL: recorded instructions differ from the expected set: '
                         f'missing {sorted(expected - ops)}, unknown {sorted(ops - expected)}')
    for rec in recs:
        want = model.measured(rec)
        for via in (0, 1):
            got = c_predict(rec, via)
            if got != want:
                failures.append((f'recorded {rec["op"]} via {"float" if via else "bits"}', rec, got, want))
    return len(recs)


def check_blocks(failures):
    path = model.VECTORS.parent / 'blockcheck.json'
    if not path.exists():
        raise SystemExit(f'FAIL: missing {path} (record it with ../Extermination/tools/ee_float/blockcheck.py)')
    runs = json.loads(path.read_text())
    for rec in runs['vu_apply_matrix']:     # 001026A0: M[0]*v.x + M[1]*v.y + M[2]*v.z in ACC, then + M[3]*v.w
        m, v = rec['M'], rec['v']
        acc, statuses = [0x5A5A5A5A] * 4, []
        for op, bc in (('vmulabc', 0), ('vmaddabc', 1), ('vmaddabc', 2)):
            status, acc = c_vec(op, 15, bc, m[4 * bc:4 * bc + 4], v, 0, acc, acc)
            statuses.append(status)
        status, out = c_vec('vmaddbc', 15, 3, m[12:16], v, 0, acc, [0x5A5A5A5A] * 4)
        if any(statuses) or status or out != rec['out']:
            failures.append(('block 001026A0', rec, out, rec['out']))
    for rec in runs['ee_prefix']:           # 00102EA8..00102F20
        # The recording keys the block's inputs and results by FPU register
        # number (the recorder's format). Below is the block's value flow over
        # named inputs, every EE rounding step and operand order kept.
        f = {int(k): v for k, v in rec['f'].items()}
        op = lambda name, *w: c_scalar(name, *[[x] for x in w])[0]  # noqa: E731
        x, y, z, m = f[17], f[18], f[19], rec['mem']
        span = op('add.s', op('neg.s', x), y)                     # y - x
        denom = op('add.s', op('neg.s', z), m)                    # m - z
        xm = op('mul.s', x, m)                                    # x * m
        num_a = op('mul.s', op('mul.s', m, z), span)              # m * z * (y - x)
        num_b = op('add.s', op('mul.s', op('neg.s', y), z), xm)   # x * m - y * z
        got = {'0': span, '1': m, '17': xm, '19': denom,
               '20': op('div.s', num_b, denom), '21': op('div.s', num_a, denom)}
        if got != rec['out']:
            failures.append(('block 00102EA8', rec, got, rec['out']))
    return len(runs['vu_apply_matrix']) + len(runs['ee_prefix'])


# --------------------------------------------------- form tables & refusal --

def check_forms(failures):
    count = 0
    flags = (C.c_int * 5)()
    for code in range(-1, len(VU_OPS) + 2):
        name = VU_OPS[code] if 0 <= code < len(VU_OPS) else None
        for dest in range(0, 17):
            for bc in (None, 0, 1, 2, 3, 4):
                want = model.VU_FORMS.get((name, dest, bc)) if name else None
                status = LIB.shim_vu_form(code, dest, -1 if bc is None else bc, flags)
                count += 1
                if want is None:
                    if status != UNMEASURED:
                        failures.append(('form refusal', (name, code, dest, bc), status, UNMEASURED))
                        continue
                    lanes, st = c_lanes(code, dest, bc, [ONE], [ONE], [ONE])
                    lanes_f, st_f = c_lanes(code, dest, bc, [ONE], [ONE], [ONE], via=1)
                    vs, vd = c_vec(code, dest, bc, VF0, VF0, ONE, VF0, [7, 7, 7, 7])
                    vs_f, vd_f = c_vec(code, dest, bc, VF0, VF0, ONE, VF0, [7, 7, 7, 7], via=1)
                    if (st, st_f, vs, vs_f) != ([UNMEASURED],) * 2 + (UNMEASURED,) * 2 or \
                            lanes != [0x5A5A5A5A] or lanes_f != [0x5A5A5A5A] or vd != [7] * 4 or vd_f != [7] * 4:
                        failures.append(('unmeasured form written', (name, dest, bc), (st, st_f, vs, vs_f), None))
                elif status != 0 or tuple(flags[:4]) != tuple(want) or flags[4] != code:
                    failures.append(('form table', (name, dest, bc), (status, tuple(flags)), want))
    for fsf in range(-1, 5):
        for ftf in range(-1, 5):
            q, status = c_div(fsf, ftf, [ONE], [ONE])
            q_f, status_f = c_div(fsf, ftf, [ONE], [ONE], via=1)
            count += 1
            if (fsf, ftf) in model.VU_DIV_FORMS:
                if status != [0] or status_f != [0] or q != [ONE] or q_f != [ONE]:
                    failures.append(('vdiv form', (fsf, ftf), (status, q), 'accepted, 1.0'))
            elif status != [UNMEASURED] or status_f != [UNMEASURED] or q != [0x5A5A5A5A] or q_f != [0x5A5A5A5A]:
                failures.append(('vdiv refusal', (fsf, ftf), (status, q), UNMEASURED))
    # Missing operand arrays: refused, destination untouched.
    for op, dest, bc, fs, ft, acc in (('vadd', 15, None, VF0, None, None), ('vmaddbc', 15, 0, VF0, VF0, None),
                                      ('vopmsub', 14, None, VF0, VF0, None), ('vmulbc', 15, 0, None, VF0, None)):
        for via in (0, 1):
            status, out = c_vec(op, dest, bc, fs, ft, 0, acc, [7, 7, 7, 7], via)
            count += 1
            if status != NO_OPERAND or out != [7] * 4:
                failures.append(('missing operand', (op, via), (status, out), NO_OPERAND))
    status, _ = c_vec('vadd', 15, None, VF0, VF0, 0, None, None)
    if status != NO_OPERAND:
        failures.append(('missing destination', 'vadd', status, NO_OPERAND))
    # A Q form needs no ft; ops that do not read ACC need no acc.
    for op, dest, bc, ft in (('vmulq', 15, None, None), ('vmulbc', 15, 0, VF0)):
        status, _ = c_vec(op, dest, bc, VF0, ft, ONE, None, [0] * 4)
        count += 1
        if status != 0:
            failures.append(('optional operand refused', op, status, 0))
    return count


# ----------------------------------------------------- random differential --

SPECIAL = [0x00000000, 0x80000000, 0x00000001, 0x80000001, 0x007FFFFF, 0x807FFFFF, 0x00800000, 0x80800000,
           0x00800001, 0x00FFFFFF, 0x01000000, 0x3F800000, 0xBF800000, 0x3F7FFFFF, 0x3F800001, 0x40000000,
           0x7F7FFFFF, 0xFF7FFFFF, 0x7F7FFFFE, 0x7F000000, 0x7F800000, 0xFF800000, 0x7F800001, 0xFF800001,
           0x7FBFFFFF, 0x7FC00000, 0xFFC00000, 0x7FFFFFFF, 0xFFFFFFFF, 0x4B000000, 0x4B7FFFFF, 0x4EFFFFFF,
           0x4F000000, 0xCF000000, 0xCF000001, 0x4F800000, 0x5F000000, 0x34000000, 0x0C800000, 0x1F800000]
INT_SPECIAL = [0, 1, 2, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 0x80000001, 0x00FFFFFF, 0x01000000, 0x01000001,
               0x01000003, 0xFF000000, 0xFEFFFFFF, 16, 0xFFFFFFF0, 0x7FFFFF80, 0x7FFFFFC0]


def f_word(rng, near=None, target=None):
    """A binary32 pattern: special values, raw bits, a partner of `near`
    (small exponent distance, near-cancelling significand), a partner whose
    product/quotient exponent lands on `target`, or a random normal."""
    r = rng.random()
    if r < 0.10:
        return rng.choice(SPECIAL)
    if r < 0.22:
        return rng.getrandbits(32)
    sign = rng.getrandbits(1) << 31
    mant = rng.choice((rng.getrandbits(23), rng.getrandbits(23), 0, 0x7FFFFF, rng.getrandbits(4),
                       0x7FFFFF ^ rng.getrandbits(4)))
    if near is not None and r < 0.80:
        e = (near >> 23) & 0xFF
        d = rng.choice((0, 0, 1, 1, 2, 3, 23, 24, 25, 26, rng.randint(0, 30), rng.randint(20, 80)))
        if target is not None and rng.random() < 0.5:
            e = target(e) + rng.randint(-2, 2)
            d = 0
        e = min(255, max(0, e + rng.choice((-d, d))))
        if rng.random() < 0.3:
            mant = ((near & 0x7FFFFF) + rng.randint(-3, 3)) & 0x7FFFFF
        return sign | (e << 23) | mant
    e = rng.choice((rng.randint(1, 254), rng.randint(100, 160), rng.randint(0, 255)))
    return sign | (e << 23) | mant


def i_word(rng):
    r = rng.random()
    if r < 0.15:
        return rng.choice(INT_SPECIAL)
    if r < 0.5:
        return rng.getrandbits(32)
    v = rng.getrandbits(rng.randint(1, 32))
    return v if rng.random() < 0.5 else (-v) & MASK


def near_midpoint_quotient(rng):
    """A (dividend, divisor) pair whose exact quotient lies within 2**-16 of
    an ulp above or below a rounding midpoint: solve Q*mb + delta = ma*2**25
    for an odd 25-bit Q and a small odd delta. Nearest-even must then follow
    the sign of delta (the sticky remainder), never the parity."""
    while True:
        mb = rng.randrange(1 << 23, 1 << 24) | 1
        delta = (2 * rng.randint(0, mb >> 16) + 1) * rng.choice((1, -1))
        q = (-delta * pow(mb, -1, 1 << 25)) % (1 << 25)
        ma = (q * mb + delta) >> 25
        if q >> 24 and (1 << 23) <= ma < (1 << 24):
            break
    ea = rng.randint(1, 254)
    eb = min(254, max(1, ea + rng.choice((0, 0, 1, -1, rng.randint(-140, 140)))))
    return ((rng.getrandbits(1) << 31) | (ea << 23) | (ma & 0x7FFFFF),
            (rng.getrandbits(1) << 31) | (eb << 23) | (mb & 0x7FFFFF))


def near_square(rng):
    """A binary32 whose square root lies on or just beside a representable
    value: significand k*k (+-1) for a 12-bit k, either exponent parity."""
    k = rng.randrange(1 << 11, 1 << 12)
    m = min(max(k * k + rng.choice((-1, 0, 1)), 1 << 22), (1 << 24) - 1)
    if m < (1 << 23):
        m <<= 1
    return (rng.getrandbits(1) << 31) | (rng.randint(1, 254) << 23) | (m & 0x7FFFFF)


def pairs(rng, n, kind='sum'):
    """Operand pairs; equal and sign-flipped pairs; for mul/div the partner
    often puts the result exponent at the smallest normal, 1.0 or the largest
    finite (FTZ / saturation); for div also near-midpoint quotients."""
    xs, ys = [], []
    for _ in range(n):
        x = f_word(rng)
        r = rng.random()
        if r < 0.05:
            y = x
        elif r < 0.10:
            y = x ^ 0x80000000
        elif kind == 'div' and r < 0.25:
            x, y = near_midpoint_quotient(rng)
        elif kind == 'mul':
            y = f_word(rng, near=x, target=lambda e: rng.choice((1, 127, 254)) + 127 - e)
        elif kind == 'div':
            y = f_word(rng, near=x, target=lambda e: e + 127 - rng.choice((1, 127, 254)))
        else:
            y = f_word(rng, near=x)
        xs.append(x)
        ys.append(y)
    return xs, ys


def job_ee(args):
    """(op, seed, n) -> mismatches of the header vs the Python model."""
    op, seed, n = args
    rng = random.Random(seed)
    fn = {'add.s': model.ee_add, 'sub.s': model.ee_sub, 'mul.s': model.ee_mul, 'div.s': model.ee_div,
          'adda.s': model.ee_adda, 'suba.s': model.ee_suba, 'mula.s': model.ee_mula,
          'c.eq.s': model.ee_c_eq, 'c.lt.s': model.ee_c_lt, 'c.le.s': model.ee_c_le,
          'madd.s': model.ee_madd, 'msub.s': model.ee_msub, 'neg.s': model.ee_neg, 'mov.s': model.ee_mov,
          'cvt.w.s': model.ee_cvt_w_s, 'cvt.s.w': model.ee_cvt_s_w}[op]
    if op in ('madd.s', 'msub.s'):
        fs, ft = pairs(rng, n, 'mul')
        # ACC near the product so the pre-trim and cancellation paths run.
        acc = [f_word(rng, near=model.ee_mul(a, b)) for a, b in zip(fs, ft)]
        args3 = (acc, fs, ft)
    elif op in ('neg.s', 'mov.s'):
        args3 = ([f_word(rng) for _ in range(n)],)
    elif op == 'cvt.w.s':
        args3 = ([f_word(rng, near=rng.randint(120, 165) << 23) for _ in range(n)],)
    elif op == 'cvt.s.w':
        args3 = ([i_word(rng) for _ in range(n)],)
    else:
        kind = 'mul' if op in ('mul.s', 'mula.s') else 'div' if op == 'div.s' else 'sum'
        args3 = pairs(rng, n, kind)
    bad = []
    want = [fn(*w) for w in zip(*args3)]
    for via in (0, 1):
        got = c_scalar(op, *args3, via=via)
        bad += [(f'random {op} via {via}', w, g, m) for w, g, m in zip(zip(*args3), got, want) if g != m]
    return bad


def job_vu_scalar(args):
    op, seed, n = args
    rng = random.Random(seed)
    if op == 'vsqrt':
        xs, ys, fn = [near_square(rng) if i % 4 == 0 else f_word(rng) for i in range(n)], None, model.vu_sqrt
    elif op in ('vftoi0', 'vftoi4'):
        xs, ys = [f_word(rng, near=rng.randint(115, 165) << 23) for _ in range(n)], None
        fn = (lambda a: model.vu_ftoi(a, 0)) if op == 'vftoi0' else (lambda a: model.vu_ftoi(a, 4))
    elif op in ('vitof0', 'vitof4'):
        xs, ys = [i_word(rng) for _ in range(n)], None
        fn = (lambda a: model.vu_itof(a, 0)) if op == 'vitof0' else (lambda a: model.vu_itof(a, 4))
    elif op == 'vabs':
        xs, ys, fn = [f_word(rng) for _ in range(n)], None, model.vu_abs
    else:
        xs, ys = pairs(rng, n)
        for i in range(0, n, 8):     # signed zeros and denormals: -0 orders below +0
            xs[i], ys[i] = (rng.choice((0, 0x80000000, 1, 0x80000001)) for _ in range(2))
        fn = model.vu_max if op == 'vmax' else model.vu_min
    want = [fn(*w) for w in zip(xs, ys)] if ys else [fn(x) for x in xs]
    bad = []
    for via in (0, 1):
        got = c_scalar(op, xs, ys, via=via)
        bad += [(f'random {op} via {via}', w, g, m) for w, g, m in zip(zip(xs, ys or xs), got, want) if g != m]
    return bad


def job_vu_div(args):
    (fsf, ftf), seed, n = args
    rng = random.Random(seed)
    a, b = pairs(rng, n, 'div')
    want = [model.vu_div(x, y, fsf, ftf) for x, y in zip(a, b)]
    bad = []
    for via in (0, 1):
        got, status = c_div(fsf, ftf, a, b, via)
        bad += [(f'random vdiv{(fsf, ftf)} via {via}', w, (g, s), m)
                for w, g, s, m in zip(zip(a, b), got, status, want) if s or g != m]
    return bad


def job_vu_form(args):
    """Every lane API over one original form, plus whole-register records
    through ee_float_model.predict (broadcast, Q, swizzle, dest mask)."""
    (op, dest, bc), seed, n = args
    rng = random.Random(seed)
    ss, ts = pairs(rng, n, 'mul' if 'mul' in op or 'madd' in op or 'msub' in op else 'sum')
    accs = [f_word(rng, near=t) for t in ts]
    want = [model.vu_lane(op, dest, bc, s, t, a) for s, t, a in zip(ss, ts, accs)]
    bad = []
    for via in (0, 1, 2):
        got, status = c_lanes(op, dest, bc, ss, ts, accs, via)
        bad += [(f'random {op} dest={dest} bc={bc} lane via {via}', w, (g, s), m)
                for w, g, s, m in zip(zip(ss, ts, accs), got, status, want) if s or g != m]
    for _ in range(max(4, n // 16)):
        rec = {'op': op, 'sig': [dest, bc], 'dest': dest, 'bc': 0 if bc is None else bc, 'fsf': 0, 'ftf': 0,
               'vs': [f_word(rng) for _ in range(4)], 'vt': [f_word(rng) for _ in range(4)],
               'acc_in': [f_word(rng) for _ in range(4)], 'q_in': f_word(rng),
               'prior': [rng.getrandbits(32) for _ in range(4)]}
        want_vec = model.predict(rec)
        for via in (0, 1):
            got_vec = c_predict(rec, via)
            if got_vec != want_vec:
                bad.append((f'random {op} dest={dest} bc={bc} register via {via}', rec, got_vec, want_vec))
    return bad


def run_job(job):
    kind, args = job
    return {'ee': job_ee, 'vus': job_vu_scalar, 'div': job_vu_div, 'form': job_vu_form}[kind](args)


def random_jobs(scale):
    jobs = []
    for i, op in enumerate(SCALAR):
        if op.startswith('v'):
            jobs.append(('vus', (op, 0xEE00 + i, 3000 * scale)))
        else:
            jobs.append(('ee', (op, 0xEE00 + i, 3000 * scale)))
    for i, form in enumerate(sorted(model.VU_DIV_FORMS)):
        jobs.append(('div', (form, 0xD100 + i, 3000 * scale)))
    for i, (op, dest, bc) in enumerate(sorted(model.VU_FORMS, key=str)):
        jobs.append(('form', ((op, dest, bc), 0xF000 + i, 400 * scale)))
    return jobs


def count_cases(jobs):
    return sum(args[2] for _, args in jobs)


def main() -> int:
    global LIB
    start = time.monotonic()
    LIB, notes = build()
    failures = []
    recorded = check_recordings(failures)
    blocks = check_blocks(failures)
    forms = check_forms(failures)
    jobs = random_jobs(pick(40, 1))
    for bad in parallel_map(run_job, jobs, cost=lambda job: job[1][2]):
        failures.extend(bad)
    banner(f'{recorded:,} recorded original results (bits + float API)', f'{blocks} free-running block runs',
           f'{forms:,} form/refusal checks', f'{count_cases(jobs):,} random model cases', ', '.join(notes))
    for what, inputs, got, want in failures[:20]:
        print('MISMATCH', what, inputs, 'header', got, 'expected', want)
    elapsed = time.monotonic() - start
    if failures:
        print(f'FAIL: {len(failures)} header results differ ({elapsed:.1f}s)')
        return 1
    print(f'PASS: em_ee_float.h reproduces every recorded original result and the model ({elapsed:.1f}s)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
