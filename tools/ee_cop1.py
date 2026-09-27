#!/usr/bin/env python3
"""One COP1 (EE FPU) instruction on the measured EE float model.

docs/EE_FLOAT_MODEL.md section 2; the arithmetic is tools/ee_float_model.py.
The older per-oracle interpreters used to compute COP1 with host floats
(`fp(x+y)`, a truncated or round-to-nearest double): that skips the EE
add/sub pre-trim, DAZ/FTZ and saturation, gets MSUB's orientation or
CVT.S.W's rounding wrong in places, and gave one-ULP errors like the one
found in 001BBE40 (docs/DOOR_ORIGINAL.md). Every interpreter now routes its
COP1 arithmetic through `cop1()` below, so the model lives in one place.

Registers and the accumulator are raw binary32 bit patterns. `cop1()`
returns what the instruction writes:

    ('fd', value)    the destination FPR fd
    ('acc', value)   the FPU accumulator (ADDA/SUBA/MULA)
    ('cond', flag)   the FCR31 C bit (C.F / C.EQ / C.LT / C.LE)
    None             not an arithmetic op (mfc1, mtc1, cfc1, ctc1, bc1x):
                     the caller's own handler deals with it

An op the original never executes (SQRT.S, RSQRT.S, ABS.S, MAX.S, MIN.S,
MADDA.S, MSUBA.S, section 2 "Not present") is unmeasured and raises
UnmeasuredCop1: a fail-stop, never a host-float guess.
"""
import ee_float_model as M

MASK = 0xFFFFFFFF


class UnmeasuredCop1(AssertionError):
    """A COP1 op outside the measured model (absent from the original)."""


_TWO = {0: M.ee_add, 1: M.ee_sub, 2: M.ee_mul, 3: M.ee_div}
_ACC = {24: M.ee_adda, 25: M.ee_suba, 26: M.ee_mula}
_CMP = {50: M.ee_c_eq, 52: M.ee_c_lt, 54: M.ee_c_le}


def cop1(word, fs_bits, ft_bits, acc_bits=0):
    """Execute the COP1 word over its operand bit patterns (fs, ft) and the
    accumulator. See the module docstring for the result."""
    rs, fn = word >> 21 & 31, word & 63
    a, b = fs_bits & MASK, ft_bits & MASK
    if rs == 20:                                   # W format
        if fn == 32: return ('fd', M.ee_cvt_s_w(a))
        raise UnmeasuredCop1(('cop1.w', fn, hex(word)))
    if rs != 16: return None                       # moves and branches
    if fn in _TWO: return ('fd', _TWO[fn](a, b))
    if fn in _ACC: return ('acc', _ACC[fn](a, b))
    if fn == 28: return ('fd', M.ee_madd(acc_bits & MASK, a, b))
    if fn == 29: return ('fd', M.ee_msub(acc_bits & MASK, a, b))
    if fn == 6: return ('fd', M.ee_mov(a))
    if fn == 7: return ('fd', M.ee_neg(a))
    if fn == 36: return ('fd', M.ee_cvt_w_s(a))
    if fn == 48: return ('cond', False)
    if fn in _CMP: return ('cond', bool(_CMP[fn](a, b)))
    raise UnmeasuredCop1(('cop1.s', fn, hex(word)))


def fields(word):
    """(fs, ft, fd) register numbers of a COP1 arithmetic word."""
    return word >> 11 & 31, word >> 16 & 31, word >> 6 & 31


def step(word, f, acc_bits=0):
    """Execute over a bit-pattern register list f. Writes fd in place and
    returns (acc_bits, cond) where cond is None unless a compare ran, or
    None when the word is not arithmetic."""
    fs, ft, fd = fields(word)
    result = cop1(word, f[fs], f[ft], acc_bits)
    if result is None: return None
    kind, value = result
    if kind == 'fd':
        f[fd] = value
        return acc_bits, None
    if kind == 'acc': return value, None
    return acc_bits, value
