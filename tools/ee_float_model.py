#!/usr/bin/env python3
"""Bit-exact reference model of the EE FPU (COP1) and VU0 macro (COP2)
arithmetic that the ORIGINAL executes under the user's saved PCSX2
configuration (../Extermination/build/startup-reference/portable-data/inis/
PCSX2.ini). docs/EE_FLOAT_MODEL.md states each rule with its evidence.

Every rule was fitted to, and is checked against, results recorded by
single-stepping instruction instances of the original boot ELF inside PCSX2
through the DebugServer (write operands, step one instruction, read the
result). The recordings are machine-derived and stay in the ignored
../Extermination/build/startup-reference/ee_float/vectors/. This file is our own model of
those measurements; it is not derived from emulator source.

All values are raw binary32 bit patterns (ints 0..2**32-1).

Self-test:  python3 tools/ee_float_model.py              (every vector)
Test:       python3 tools/test_ee_float_model.py         (quick sample)
"""
from __future__ import annotations

import json
import struct
import sys
from pathlib import Path

MASK = 0xFFFFFFFF
SIGN = 0x80000000
MAX = 0x7F7FFFFF          # largest finite magnitude; every saturation lands here
INF = 0x7F800000
QNAN = 0x7FC00000         # EE MADD/MSUB internal invalid product (then saturated to +MAX)
INDEFINITE = 0xFFC00000   # VU0 invalid result (0*Inf, Inf-Inf) when nothing clamps it


class UnmeasuredCase(ValueError):
    """The requested instruction form was never observed in the original."""


# ---------------------------------------------------------------- helpers --

def f2b(value: float) -> int:
    """Host float -> nearest binary32 bits (a convenience for callers)."""
    return struct.unpack('<I', struct.pack('<f', value))[0]


def b2f(bits: int) -> float:
    return struct.unpack('<f', struct.pack('<I', bits & MASK))[0]


def _exp(bits: int) -> int:
    return (bits >> 23) & 0xFF


def _is_nan(bits: int) -> bool:
    return _exp(bits) == 0xFF and bool(bits & 0x7FFFFF)


def _is_inf(bits: int) -> bool:
    return (bits & 0x7FFFFFFF) == INF


def _is_zero(bits: int) -> bool:
    """Zero after DenormalsAreZero (exponent field 0)."""
    return _exp(bits) == 0


def _daz(bits: int) -> int:
    """A denormal operand reads as a zero of the same sign."""
    return bits & SIGN if _exp(bits) == 0 else bits


def _unpack(bits: int):
    """Finite DAZ-applied operand -> (sign, significand, exponent) with
    value = significand * 2**exponent (significand 0 for zeros)."""
    e = _exp(bits)
    if e == 0:
        return bits >> 31, 0, 0
    return bits >> 31, (bits & 0x7FFFFF) | 0x800000, e - 150


def _pack(sign: int, mag: int, exp: int, nearest: bool, inexact_below: bool = False) -> int:
    """Round sign * mag * 2**exp (mag > 0; `inexact_below` marks a nonzero
    remainder below mag's last bit) to binary32.

    nearest=False truncates toward zero, nearest=True rounds to nearest-even.
    A rounded magnitude below the smallest normal flushes to a signed zero;
    one above the largest finite value saturates to it."""
    shift = mag.bit_length() - 24
    if shift > 0:
        kept, rest = mag >> shift, mag & ((1 << shift) - 1)
        if nearest:
            half = 1 << (shift - 1)
            if rest > half or (rest == half and (inexact_below or kept & 1)):
                kept += 1
    else:
        # No bits dropped here; any remainder is below half an ulp.
        kept = mag << -shift
    exp += shift
    if kept >> 24:
        kept >>= 1
        exp += 1
    biased = exp + 150
    if biased <= 0:
        return sign << 31
    if biased >= 0xFF:
        return (sign << 31) | MAX
    return (sign << 31) | (biased << 23) | (kept & 0x7FFFFF)


def _saturate(bits: int) -> int:
    """Instruction-result saturation: any NaN -> +MAX, +-Inf -> +-MAX."""
    if _is_nan(bits):
        return MAX
    if _is_inf(bits):
        return (bits & SIGN) | MAX
    return bits


def _saturate_signed(bits: int) -> int:
    """Sign-keeping saturation used by NEG.S, the compares and VSQRT:
    every exponent-255 pattern -> +-MAX by its sign bit."""
    return (bits & SIGN) | MAX if _exp(bits) == 0xFF else bits


def _exact_sum(a: int, b: int) -> int:
    """Finite DAZ-applied a + b, truncated."""
    sa, ma, ea = _unpack(a)
    sb, mb, eb = _unpack(b)
    if ma == 0 and mb == 0:
        # Signed-zero sum under truncation: -0 only for (-0) + (-0).
        return SIGN if (sa and sb) else 0
    if ma == 0:
        return b
    if mb == 0:
        return a
    base = min(ea, eb)
    total = (-1) ** sa * (ma << (ea - base)) + (-1) ** sb * (mb << (eb - base))
    if total == 0:
        return 0
    return _pack(1 if total < 0 else 0, abs(total), base, False)


def _exact_product(a: int, b: int) -> int:
    """Finite DAZ-applied a * b, truncated."""
    sa, ma, ea = _unpack(a)
    sb, mb, eb = _unpack(b)
    if ma == 0 or mb == 0:
        return (sa ^ sb) << 31
    return _pack(sa ^ sb, ma * mb, ea + eb, False)


def _quotient(a: int, b: int, nearest: bool) -> int:
    """Finite DAZ-applied a / b with b nonzero."""
    sa, ma, ea = _unpack(a)
    sb, mb, eb = _unpack(b)
    if ma == 0:
        return (sa ^ sb) << 31
    q, r = divmod(ma << 50, mb)
    return _pack(sa ^ sb, q, ea - eb - 50, nearest, inexact_below=r != 0)


# ------------------------------------------------------------- EE COP1 ---

def _trim(bits: int, distance: int) -> int:
    """ADD/SUB pre-trim of the operand with the smaller exponent field:
    keep one bit below the larger operand's last bit, i.e. clear the low
    (distance - 1) bits; a distance of 25 or more leaves a signed zero."""
    if distance >= 25:
        return bits & SIGN
    return bits & ((MASK << (distance - 1)) & MASK)


def _ee_sum(a: int, b: int) -> int:
    """a + b for DAZ-applied operands (Inf/NaN may occur): pre-trim,
    truncation, then result saturation."""
    if _exp(a) == 0xFF or _exp(b) == 0xFF:
        if _is_nan(a) or _is_nan(b):
            return MAX
        if _is_inf(a) and _is_inf(b):
            return MAX if (a ^ b) & SIGN else (a & SIGN) | MAX
        return ((a if _is_inf(a) else b) & SIGN) | MAX
    d = _exp(a) - _exp(b)
    if d > 0:
        b = _trim(b, d)
    elif d < 0:
        a = _trim(a, -d)
    return _exact_sum(a, b)


def ee_add(a: int, b: int) -> int:
    """ADD.S / ADDA.S: fs + ft."""
    return _ee_sum(_daz(a), _daz(b))


def ee_sub(a: int, b: int) -> int:
    """SUB.S / SUBA.S: fs - ft."""
    return _ee_sum(_daz(a), _daz(b) ^ SIGN)


def _ee_raw_product(a: int, b: int) -> int:
    """fs * ft truncated but not saturated (Inf / QNAN pass through)."""
    a, b = _daz(a), _daz(b)
    if _is_nan(a) or _is_nan(b):
        return QNAN
    if _is_inf(a) or _is_inf(b):
        if _is_zero(a) or _is_zero(b):
            return QNAN
        return ((a ^ b) & SIGN) | INF
    return _exact_product(a, b)


def ee_mul(a: int, b: int) -> int:
    """MUL.S / MULA.S: fs * ft, truncated, saturated."""
    return _saturate(_ee_raw_product(a, b))


def ee_div(a: int, b: int) -> int:
    """DIV.S: fs / ft rounded to nearest-even (the separate FPUDiv round
    mode). A zero divisor gives +-MAX by the XOR of the signs, even for a
    zero or NaN dividend."""
    a, b = _daz(a), _daz(b)
    sign = (a ^ b) >> 31
    if _is_zero(b):
        return (sign << 31) | MAX
    if _exp(a) == 0xFF or _exp(b) == 0xFF:
        if _is_nan(a) or _is_nan(b) or (_is_inf(a) and _is_inf(b)):
            return MAX
        return (sign << 31) | MAX if _is_inf(a) else sign << 31
    return _quotient(a, b, True)


def ee_madd(acc: int, a: int, b: int) -> int:
    """MADD.S: ACC + fs*ft. The product is truncated but NOT saturated;
    the sum then follows ADD.S (pre-trim, truncation, saturation)."""
    return _ee_sum(_daz(acc), _ee_raw_product(a, b))


def ee_msub(acc: int, a: int, b: int) -> int:
    """MSUB.S: ACC - fs*ft (the accumulator is the minuend)."""
    return _ee_sum(_daz(acc), _ee_raw_product(a, b) ^ SIGN)


# ADDA/SUBA/MULA write ACC with exactly the ADD/SUB/MUL result (saturated).
ee_adda, ee_suba, ee_mula = ee_add, ee_sub, ee_mul


def ee_neg(a: int) -> int:
    """NEG.S: exponent-255 inputs saturate by sign first, then the sign
    flips. Denormals are negated as raw bits (no DAZ)."""
    return _saturate_signed(a) ^ SIGN


def ee_mov(a: int) -> int:
    """MOV.S: raw copy."""
    return a & MASK


def ee_cvt_w_s(a: int) -> int:
    """CVT.W.S: truncate toward zero; |x| >= 2**31, Inf and NaN saturate
    to 0x7FFFFFFF / 0x80000000 by the sign bit; denormals give 0."""
    if _exp(a) >= 158:
        return 0x80000000 if a & SIGN else 0x7FFFFFFF
    s, m, x = _unpack(a)
    v = m << x if x >= 0 else m >> -x
    return (-v if s else v) & MASK


def ee_cvt_s_w(i: int) -> int:
    """CVT.S.W: signed word to float, truncated."""
    i &= MASK
    if i == 0:
        return 0
    sign = i >> 31
    return _pack(sign, (1 << 32) - i if sign else i, 0, False)


def _compare_key(bits: int) -> int:
    bits = _saturate_signed(_daz(bits))
    return -(bits & 0x7FFFFFFF) if bits & SIGN else bits


def ee_c_eq(a: int, b: int) -> int:
    """C.EQ.S condition: DAZ, sign-keeping saturation, then compare
    (so -0 == +0 and a denormal equals zero)."""
    return int(_compare_key(a) == _compare_key(b))


def ee_c_lt(a: int, b: int) -> int:
    return int(_compare_key(a) < _compare_key(b))


def ee_c_le(a: int, b: int) -> int:
    return int(_compare_key(a) <= _compare_key(b))


# ------------------------------------------------------------ VU0 macro ---
#
# Lane arithmetic: DAZ inputs, truncation, flush-to-zero, finite overflow
# saturates to +-MAX, NO add/sub pre-trim, and NO result saturation: an
# Inf/NaN operand that is not clamped propagates (NaN quieted by setting bit
# 22; a NaN in the first operand wins; 0*Inf and Inf-Inf give INDEFINITE).
# Whether an operand is clamped first (NaN -> +MAX, +-Inf -> +-MAX) depends
# on the instruction form, so it is looked up per form below.

def _quiet(bits: int) -> int:
    return bits | 0x00400000


def _vu_add_raw(x: int, y: int) -> int:
    x, y = _daz(x), _daz(y)
    if _is_nan(x):
        return _quiet(x)
    if _is_nan(y):
        return _quiet(y)
    if _is_inf(x) and _is_inf(y):
        return INDEFINITE if (x ^ y) & SIGN else x
    if _is_inf(x):
        return x
    if _is_inf(y):
        return y
    return _exact_sum(x, y)


def _vu_sub_raw(x: int, y: int) -> int:
    x, y = _daz(x), _daz(y)
    if _is_nan(x):
        return _quiet(x)
    if _is_nan(y):
        return _quiet(y)
    return _vu_add_raw(x, y ^ SIGN)


def _vu_mul_raw(x: int, y: int) -> int:
    x, y = _daz(x), _daz(y)
    if _is_nan(x):
        return _quiet(x)
    if _is_nan(y):
        return _quiet(y)
    if _is_inf(x) or _is_inf(y):
        if _is_zero(x) or _is_zero(y):
            return INDEFINITE
        return ((x ^ y) & SIGN) | INF
    return _exact_product(x, y)


# (op, dest mask, broadcast lane or None) -> (clamp fs, clamp ft/Q/bc lane,
# clamp ACC, NaN order). NaN order 1 = the product's NaN wins over ACC's in
# VMADD. One entry per form that occurs in the original boot ELF's code
# (dest bits: x=8 y=4 z=2 w=1); every form was recorded on a real instance.
# "free" marks a flag the recorded instance could not separate because it
# reads vf0 or the same register twice; every other original instance of
# that form has the same register pattern, so the choice cannot change any
# original result.
VU_FORMS = {
    ('vadd', 1, None): (0, 0, 0, 0),      # ft = vf0: ft clamp free
    ('vadd', 14, None): (0, 0, 0, 0),
    ('vadd', 15, None): (0, 0, 0, 0),
    ('vaddbc', 1, 3): (0, 0, 0, 0),       # ft = vf0: ft clamp free
    ('vaddbc', 2, 0): (0, 0, 0, 0),
    ('vaddbc', 2, 1): (0, 0, 0, 0),
    ('vaddbc', 4, 0): (0, 0, 0, 0),
    ('vaddbc', 4, 1): (0, 0, 0, 0),
    ('vaddbc', 4, 3): (0, 0, 0, 0),       # ft = vf0: ft clamp free
    ('vaddbc', 8, 0): (0, 0, 0, 0),
    ('vaddbc', 8, 1): (0, 0, 0, 0),
    ('vaddbc', 8, 2): (0, 0, 0, 0),
    ('vaddbc', 8, 3): (0, 0, 0, 0),
    ('vaddbc', 12, 0): (0, 0, 0, 0),
    ('vaddq', 8, None): (0, 0, 0, 0),     # fs = vf0: fs clamp free
    ('vaddq', 15, None): (0, 0, 0, 0),    # fs = vf0: fs clamp free
    ('vsub', 1, None): (1, 1, 0, 0),      # fs == ft on every instance
    ('vsub', 3, None): (1, 1, 0, 0),      # fs == ft on every instance
    ('vsub', 8, None): (0, 0, 0, 0),
    ('vsub', 12, None): (0, 0, 0, 0),
    ('vsub', 13, None): (0, 0, 0, 0),
    ('vsub', 14, None): (0, 0, 0, 0),
    ('vsub', 15, None): (1, 1, 0, 0),
    ('vsubbc', 1, 0): (0, 0, 0, 0),
    ('vsubbc', 2, 0): (0, 0, 0, 0),
    ('vsubbc', 4, 0): (0, 0, 0, 0),
    ('vsubbc', 8, 0): (0, 0, 0, 0),
    ('vsubbc', 15, 3): (1, 1, 0, 0),
    ('vmul', 8, None): (1, 0, 0, 0),      # fs == ft: only "not both" is determined
    ('vmul', 14, None): (1, 0, 0, 0),
    ('vmul', 15, None): (1, 1, 0, 0),
    ('vmulbc', 1, 0): (1, 0, 0, 0),
    ('vmulbc', 7, 0): (1, 0, 0, 0),       # ft = vf0: ft clamp free
    ('vmulbc', 8, 0): (1, 0, 0, 0),
    ('vmulbc', 12, 0): (1, 0, 0, 0),
    ('vmulbc', 14, 0): (1, 0, 0, 0),
    ('vmulbc', 14, 1): (1, 0, 0, 0),
    ('vmulbc', 14, 2): (1, 0, 0, 0),
    ('vmulbc', 14, 3): (1, 0, 0, 0),
    ('vmulbc', 15, 0): (1, 1, 0, 0),
    ('vmulbc', 15, 3): (1, 1, 0, 0),
    ('vmulq', 2, None): (1, 0, 0, 0),
    ('vmulq', 12, None): (1, 0, 0, 0),
    ('vmulq', 14, None): (1, 0, 0, 0),
    ('vmulq', 15, None): (1, 1, 0, 0),
    ('vmulabc', 1, 2): (1, 0, 0, 0),      # fs = vf0: fs clamp free
    ('vmulabc', 14, 0): (1, 0, 0, 0),
    ('vmulabc', 15, 0): (1, 0, 0, 0),
    ('vmulabc', 15, 1): (1, 0, 0, 0),
    ('vmaddbc', 1, 3): (1, 1, 1, 0),      # all clamped: NaN order free
    ('vmaddbc', 14, 0): (1, 0, 0, 1),
    ('vmaddbc', 14, 2): (1, 0, 0, 1),
    ('vmaddbc', 15, 0): (1, 0, 0, 1),
    ('vmaddbc', 15, 3): (1, 1, 1, 0),     # all clamped: NaN order free
    ('vmaddabc', 14, 1): (1, 0, 0, 0),
    ('vmaddabc', 15, 1): (1, 0, 0, 0),
    ('vmaddabc', 15, 2): (1, 0, 0, 0),
    ('vmaddabc', 15, 3): (1, 0, 0, 0),
    ('vopmula', 14, None): (0, 0, 0, 0),
    ('vopmsub', 14, None): (0, 0, 0, 0),
}


def vu_form(op: str, dest: int, bc: int | None = None):
    try:
        return VU_FORMS[(op, dest, bc)]
    except KeyError:
        raise UnmeasuredCase(f'{op} dest={dest:#x} bc={bc} does not occur in the original') from None


def _c(flag: int, bits: int) -> int:
    return _saturate(bits) if flag else bits


def vu_lane(op: str, dest: int, bc: int | None, s: int, t: int, acc: int | None = None) -> int:
    """One written lane of VADD/VSUB/VMUL(bc/q), VMULAbc, VMADDbc,
    VMADDAbc, VOPMULA, VOPMSUB in the given original form. `t` is the
    already-selected ft lane (broadcast lane, Q, or the swizzled lane for
    VOPMULA/VOPMSUB); `acc` is the ACC lane for the accumulate forms."""
    cs, ct, ca, order = vu_form(op, dest, bc)
    s, t = _c(cs, s), _c(ct, t)
    if op in ('vadd', 'vaddbc', 'vaddq'):
        return _vu_add_raw(s, t)
    if op in ('vsub', 'vsubbc'):
        return _vu_sub_raw(s, t)
    if op in ('vmul', 'vmulbc', 'vmulq', 'vmulabc', 'vopmula'):
        return _vu_mul_raw(s, t)
    product = _vu_mul_raw(s, t)
    acc = _c(ca, acc)
    if op in ('vmaddbc', 'vmaddabc'):
        return _vu_add_raw(product, acc) if order else _vu_add_raw(acc, product)
    if op == 'vopmsub':
        return _vu_sub_raw(acc, product)
    raise UnmeasuredCase(op)


# VDIV forms in the original: (fsf, ftf) -> whether a NaN divisor comes back
# as that NaN (quieted) instead of +MAX. The (3, x) forms are the 51
# reciprocal instances (fs = vf0, dividend vf0.w = 1.0); (0, 0) is the one
# general division (0x1CFA04, fs and ft both real registers).
VU_DIV_FORMS = {(0, 0): False, (3, 0): True, (3, 3): True}


def vu_div(a: int, b: int, fsf: int = 0, ftf: int = 0) -> int:
    """VDIV Q = fs.fsf / ft.ftf, truncated. Zero divisor: +-MAX by the XOR
    of the signs (whatever the dividend); Inf/Inf: +MAX; a NaN operand:
    +MAX, except that the reciprocal forms return a NaN divisor quieted;
    Inf/x: +-MAX; x/Inf: signed zero."""
    try:
        nan_passes = VU_DIV_FORMS[(fsf, ftf)]
    except KeyError:
        raise UnmeasuredCase(f'vdiv fsf={fsf} ftf={ftf} does not occur in the original') from None
    a, b = _daz(a), _daz(b)
    sign = (a ^ b) >> 31
    if _is_zero(b):
        return (sign << 31) | MAX
    if nan_passes and _is_nan(b) and not _is_nan(a):
        return _quiet(b)
    if _exp(a) == 0xFF or _exp(b) == 0xFF:
        if _is_nan(a) or _is_nan(b) or (_is_inf(a) and _is_inf(b)):
            return MAX
        return (sign << 31) | MAX if _is_inf(a) else sign << 31
    return _quotient(a, b, False)


def _isqrt(n: int) -> int:
    x = 1 << ((n.bit_length() + 1) // 2)
    while True:
        y = (x + n // x) // 2
        if y >= x:
            return x
        x = y


def vu_sqrt(b: int) -> int:
    """VSQRT Q = sqrt(|ft.ftf|), truncated; exponent-255 inputs read as MAX."""
    b = _saturate_signed(_daz(b)) & 0x7FFFFFFF
    _, m, e = _unpack(b)
    if m == 0:
        return 0
    if e & 1:
        m <<= 1
        e -= 1
    return _pack(0, _isqrt(m << 60), (e - 60) // 2, False)


def vu_ftoi(a: int, frac: int) -> int:
    """VFTOI0 / VFTOI4: truncate a * 2**frac toward zero; out of range,
    Inf and NaN saturate by sign; denormals give 0."""
    if _exp(a) == 0:
        return 0
    if _exp(a) + frac >= 158:
        return 0x80000000 if a & SIGN else 0x7FFFFFFF
    s, m, x = _unpack(a)
    x += frac
    v = m << x if x >= 0 else m >> -x
    return (-v if s else v) & MASK


def vu_itof(i: int, frac: int) -> int:
    """VITOF0 / VITOF4: int32 * 2**-frac to float, truncated."""
    i &= MASK
    if i == 0:
        return 0
    sign = i >> 31
    return _pack(sign, (1 << 32) - i if sign else i, -frac, False)


def _vu_order_key(bits: int) -> int:
    """VMAX/VMINI order: raw sign-magnitude bits, with -0 below +0."""
    return bits if not bits & SIGN else -(bits & 0x7FFFFFFF) - 1


def vu_max(a: int, b: int) -> int:
    """VMAX: the larger raw operand (no DAZ, no clamp)."""
    return a if _vu_order_key(a) >= _vu_order_key(b) else b


def vu_min(a: int, b: int) -> int:
    """VMINI: the smaller raw operand (no DAZ, no clamp)."""
    return a if _vu_order_key(a) <= _vu_order_key(b) else b


def vu_abs(a: int) -> int:
    """VABS: clear the sign bit, raw."""
    return a & 0x7FFFFFFF


# -------------------------------------------------------------- self-test ---

ROOT = Path(__file__).resolve().parents[1]
VECTORS = ROOT.parent / 'Extermination/build/startup-reference/ee_float/vectors'

EE_BINARY = {'add.s': ee_add, 'sub.s': ee_sub, 'mul.s': ee_mul, 'div.s': ee_div}
EE_UNARY = {'cvt.w.s': ee_cvt_w_s, 'cvt.s.w': ee_cvt_s_w, 'neg.s': ee_neg, 'mov.s': ee_mov}
EE_COMPARE = {'c.eq.s': ee_c_eq, 'c.lt.s': ee_c_lt, 'c.le.s': ee_c_le}
EE_ACC = {'adda.s': ee_adda, 'suba.s': ee_suba, 'mula.s': ee_mula}
EE_MACC = {'madd.s': ee_madd, 'msub.s': ee_msub}
VF0 = [0, 0, 0, 0x3F800000]
_OP_S, _OP_T = (1, 2, 0), (2, 0, 1)     # VOPMULA/VOPMSUB: fs.yzx * ft.zxy
_ACC_OUT = ('vmulabc', 'vmaddabc', 'vopmula')
_UNARY_VU = {'vabs': vu_abs, 'vftoi0': lambda a: vu_ftoi(a, 0), 'vftoi4': lambda a: vu_ftoi(a, 4),
             'vitof0': lambda a: vu_itof(a, 0), 'vitof4': lambda a: vu_itof(a, 4)}
_INSTANCES: dict | None = None


def _instance(op: str) -> dict:
    global _INSTANCES
    if _INSTANCES is None:
        _INSTANCES = json.loads((VECTORS / 'instances.json').read_text())
    return _INSTANCES[op]


def _vu_record(rec: dict):
    """Prediction for a recorded VU vector (both recorder layouts: the
    per-op phase names its instance in instances.json; the per-form phase
    carries dest/bc/fsf/ftf and the effective operands itself)."""
    op = rec['op']
    if 'sig' in rec:
        dest, bc = rec.get('dest'), rec.get('bc')
        fsf, ftf = rec.get('fsf'), rec.get('ftf')
        s = rec['vs']
    else:
        inst = _instance(op)
        dest, bc, fsf, ftf = inst['dest'], inst['bc'], inst['fsf'], inst['ftf']
        s = VF0 if op == 'vaddq' else rec['vs']   # that instance reads vf0
    if op == 'vdiv':
        return vu_div(rec['vs'][fsf], rec['vt'][ftf], fsf, ftf)
    if op == 'vsqrt':
        return vu_sqrt(rec['vt'][ftf])
    if op in _ACC_OUT:
        fill = rec['acc_in']
    else:
        fill = rec['prior'] if rec['prior'] is not None else rec['vd']
    out = []
    for k in range(4):
        if not (dest >> (3 - k)) & 1:
            out.append(fill[k])
            continue
        if op in _UNARY_VU:
            out.append(_UNARY_VU[op](s[k]))
        elif op in ('vmaxbc', 'vminibc'):
            out.append((vu_max if op == 'vmaxbc' else vu_min)(s[k], rec['vt'][bc]))
        else:
            if op in ('vopmula', 'vopmsub'):
                sk, tk = s[_OP_S[k]], rec['vt'][_OP_T[k]]
            elif op in ('vmulq', 'vaddq'):
                sk, tk = s[k], rec['q_in']
            elif op.endswith('bc'):
                sk, tk = s[k], rec['vt'][bc]
            else:
                sk, tk = s[k], rec['vt'][k]
            acc = rec['acc_in'][k] if rec.get('acc_in') else None
            out.append(vu_lane(op, dest, bc if op.endswith('bc') else None, sk, tk, acc))
    return out


def predict(rec: dict):
    """Model prediction for one recorded emulator vector, in the record's
    own output shape (see ../Extermination/tools/ee_float/*.py)."""
    op = rec['op']
    if op in EE_BINARY:
        return EE_BINARY[op](rec['fs'], rec['ft'])
    if op in EE_UNARY:
        return EE_UNARY[op](rec['fs'])
    if op in EE_COMPARE:
        return EE_COMPARE[op](rec['fs'], rec['ft'])
    if op in EE_MACC:      # ACC <- MULA.S(acc, 1.0); fd <- op(ACC, fs, ft)
        return EE_MACC[op](ee_mula(rec['acc'], 0x3F800000), rec['fs'], rec['ft'])
    if op in EE_ACC:       # ACC <- op(fs, ft); fd <- MSUB.S(ACC, +0, +0)
        return ee_msub(EE_ACC[op](rec['fs'], rec['ft']), 0, 0)
    return _vu_record(rec)


def measured(rec: dict):
    if 'c' in rec:
        return rec['c']
    if 'fd' in rec:
        return rec['fd']
    if rec['op'] in ('vdiv', 'vsqrt'):
        return rec['q']
    if rec['op'] in _ACC_OUT:
        return rec['acc']
    return rec['vd']


def load_vectors() -> list[dict]:
    files = sorted(VECTORS.glob('*.jsonl'))
    if not files:
        raise SystemExit(f'ee_float_model: no recorded emulator vectors under {VECTORS} '
                         '(record them with ../Extermination/tools/ee_float/battery.py '
                         'and vusig.py)')
    out = []
    for path in files:
        with open(path) as fh:
            out.extend(json.loads(line) for line in fh)
    return out


def self_test(verbose: bool = True) -> int:
    by_op: dict[str, list] = {}
    for rec in load_vectors():
        by_op.setdefault(rec['op'], []).append(rec)
    failures = 0
    total = 0
    for op, group in sorted(by_op.items()):
        bad = [r for r in group if predict(r) != measured(r)]
        failures += len(bad)
        total += len(group)
        if verbose:
            print(f'  {op:9s} {len(group):6d} vectors  mismatches {len(bad)}')
            for r in bad[:3]:
                print('    MISMATCH', json.dumps(r), 'model', predict(r))
    if verbose:
        print(f'{total} recorded original results, {failures} mismatches')
    return failures


if __name__ == '__main__':
    sys.exit(1 if self_test() else 0)
