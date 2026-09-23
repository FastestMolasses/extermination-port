#!/usr/bin/env python3
"""Execute the original slide state and compare em_player_slide.c.

docs/PLAYER_CLIMB_SLIDE.md. The user's pinned ELF (and, for the whole-world
evidence mode, a captured EE RAM image) supplies every instruction and table;
none are embedded here.

This file also holds `EE`, a general bounded EE interpreter shared with
tools/test_player_climb_reference.py. Unlike the older per-routine oracles it
backs memory with the full 32 MB RAM image and the 16 KB scratchpad, so the
original collision walkers (0019AD00, 0019AFE0, 0019AB20, 0019BC40, ...) can
run over the captured AREA11 world unmodified. Arithmetic follows the model
the existing oracles and the native port share: single-precision results
truncate toward zero (EE FPU and VU0), overflow clamps to the largest finite
value and denormals flush to zero.

Hooked boundaries are recorded, never simulated as a claim about the callee.
"""
import ctypes as C
import hashlib
import zlib
import math
import os
import random
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
REFERENCE = DECOMP / 'build/startup-reference'
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
RETURN = 0x0BADF00C
STACK_TOP = 0x7F0F0000          # a private stack region outside RAM/scratchpad
FLT_MAX_BITS = 0x7F7FFFFF

_F = struct.Struct('<f')
_I = struct.Struct('<I')


def bits(value):
    return _I.unpack(_F.pack(value))[0]


def number(value):
    return _F.unpack(_I.pack(value & 0xFFFFFFFF))[0]


def s32(value):
    value &= 0xFFFFFFFF
    return value - 0x100000000 if value & 0x80000000 else value


def s64(value):
    value &= 0xFFFFFFFFFFFFFFFF
    return value - (1 << 64) if value >> 63 else value


def sx32(value):
    """Sign-extend a 32-bit result into the 64-bit register image."""
    value &= 0xFFFFFFFF
    return value | 0xFFFFFFFF00000000 if value & 0x80000000 else value


def fp(value):
    """EE single precision: truncate toward zero, clamp, flush denormals."""
    if value != value:
        raise AssertionError('NaN reached the EE model')
    magnitude = abs(value)
    if magnitude >= 3.4028234663852886e38:
        return math.copysign(number(FLT_MAX_BITS), value)
    if magnitude < 1.1754943508222875e-38:
        return math.copysign(0.0, value) if magnitude == 0 else 0.0
    rounded = _F.unpack(_F.pack(value))[0]
    if abs(rounded) > magnitude:
        rounded = number(bits(rounded) - 1)
    return rounded


def fbits(value):
    return bits(fp(value))


def flt(word):
    """Read a register image as an EE float (denormal inputs are zero)."""
    word &= 0xFFFFFFFF
    exponent = word & 0x7F800000
    if exponent == 0:
        return 0.0 if not word & 0x80000000 else -0.0
    if exponent == 0x7F800000:   # no Inf/NaN on the EE: a finite 2^128 range
        value = math.ldexp(1.0 + (word & 0x7FFFFF) / 8388608.0, 128)
        return -value if word & 0x80000000 else value
    return _F.unpack(_I.pack(word))[0]


class EE:
    """Bounded EE core: GPR (128-bit via hi halves), FPU, VU0 macro, MMI subset."""

    def __init__(self, elf, ram=None, spad=None):
        self.mem = bytearray(ram) if ram is not None else bytearray(0x2000000)
        if ram is None:
            self.load_elf(elf)
        self.spad = bytearray(spad) if spad is not None else bytearray(0x4000)
        self.stack = bytearray(0x100000)
        self.r = [0] * 32
        self.rh = [0] * 32
        self.hi = self.lo = 0
        self.f = [0] * 32
        self.acc = 0.0
        self.cond = False
        self.vf = [[0, 0, 0, 0] for _ in range(32)]
        self.vf[0][3] = bits(1.0)
        self.vacc = [0.0] * 4
        self.q = 0.0
        self.hooks = {}
        self.log = []
        self.steps = 0
        self.limit = 50_000_000
        self.r[28] = 0x27D370
        self.r[29] = STACK_TOP

    # ---- memory -------------------------------------------------------
    def load_elf(self, elf):
        phoff = struct.unpack_from('<I', elf, 28)[0]
        size, count = struct.unpack_from('<HH', elf, 42)
        for i in range(count):
            kind, off, va, _, filesz, memsz, *_ = struct.unpack_from('<8I', elf, phoff + i * size)
            if kind == 1:
                self.mem[va:va + filesz] = elf[off:off + filesz]

    def _where(self, address):
        address &= 0xFFFFFFFF
        if 0x70000000 <= address < 0x70004000:
            return self.spad, address - 0x70000000
        if 0x7F000000 <= address < 0x7F100000:
            return self.stack, address - 0x7F000000
        if address < 0x40000000:
            return self.mem, address & 0x1FFFFFF
        raise AssertionError(('address', hex(address)))

    def load(self, address, size=4):
        buf, at = self._where(address)
        return int.from_bytes(buf[at:at + size], 'little')

    def save(self, address, value, size=4):
        buf, at = self._where(address)
        buf[at:at + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'little')

    def read(self, address, size):
        buf, at = self._where(address)
        return bytes(buf[at:at + size])

    def write(self, address, data):
        buf, at = self._where(address)
        buf[at:at + len(data)] = data

    def getf(self, address):
        return flt(self.load(address))

    def putf(self, address, value):
        self.save(address, bits(value))

    def vector(self, address, count=3):
        return tuple(self.load(address + 4 * i) for i in range(count))

    # ---- helpers for hooks -------------------------------------------
    def ret_int(self, value):
        self.r[2] = sx32(value)

    def ret_float(self, value):
        self.f[0] = bits(value)

    def arg(self, n):
        return self.r[4 + n] & 0xFFFFFFFF

    def farg(self, n):
        return flt(self.f[12 + n])

    # ---- VU0 macro ----------------------------------------------------
    def macro(self, word):
        op, fs, ft, fd = word & 63, word >> 11 & 31, word >> 16 & 31, word >> 6 & 31
        mask = word >> 21 & 15
        x = [flt(v) for v in self.vf[fs]]
        y = [flt(v) for v in self.vf[ft]]
        result, destination, accumulator = None, fd, False
        if op < 4:
            result = [fp(a + y[op]) for a in x]
        elif op < 8:
            result = [fp(a - y[op - 4]) for a in x]
        elif op < 12:
            result = [fp(self.vacc[i] + fp(x[i] * y[op - 8])) for i in range(4)]
        elif op < 16:
            result = [fp(self.vacc[i] - fp(x[i] * y[op - 12])) for i in range(4)]
        elif 24 <= op < 28:
            result = [fp(a * y[op - 24]) for a in x]
        elif op == 28:
            result = [fp(a * self.q) for a in x]
        elif op == 32:
            result = [fp(a + self.q) for a in x]
        elif op == 40:
            result = [fp(a + b) for a, b in zip(x, y)]
        elif op == 41:
            result = [fp(self.vacc[i] + fp(x[i] * y[i])) for i in range(4)]
        elif op == 42:
            result = [fp(a * b) for a, b in zip(x, y)]
        elif op == 44:
            result = [fp(a - b) for a, b in zip(x, y)]
        elif op >= 60:
            special = (word >> 6 & 31) << 2 | (op & 3)
            destination = ft
            if special in (0x30, 0x31):                          # vmove / vmr32
                raw = list(self.vf[fs])
                if special == 0x31: raw = raw[1:] + raw[:1]
                for lane in range(4):
                    if mask & (8 >> lane) and ft: self.vf[ft][lane] = raw[lane]
                return
            elif special in (0x18, 0x19, 0x1A, 0x1B):            # vmula[xyzw]
                result = [fp(a * y[op & 3]) for a in x]; accumulator = True
            elif special in (0x08, 0x09, 0x0A, 0x0B):            # vmadda[xyzw]
                result = [fp(self.vacc[i] + fp(x[i] * y[op & 3])) for i in range(4)]; accumulator = True
            elif special in (0x0C, 0x0D, 0x0E, 0x0F):            # vmsuba[xyzw]
                result = [fp(self.vacc[i] - fp(x[i] * y[op & 3])) for i in range(4)]; accumulator = True
            elif special == 0x2A:                                # vmula
                result = [fp(a * b) for a, b in zip(x, y)]; accumulator = True
            elif special == 0x28:                                # vadda
                result = [fp(a + b) for a, b in zip(x, y)]; accumulator = True
            elif special == 0x2E:                                # vopmula
                result = [fp(x[1] * y[2]), fp(x[2] * y[0]), fp(x[0] * y[1]), 0.0]; accumulator = True
                mask = 0xE
            elif special == 0x39:                                # vsqrt
                self.q = fp(math.sqrt(abs(y[word >> 23 & 3]))); return
            elif special == 0x38:                                # vdiv
                den = y[word >> 23 & 3]
                self.q = fp(x[word >> 21 & 3] / den) if den else number(FLT_MAX_BITS)
                return
            elif special == 0x3B:                                # vwaitq
                return
            elif special == 0x2F:                                # vnop
                return
            else:
                raise AssertionError(('VU special', hex(word), hex(special)))
        elif op == 46:                                           # vopmsub
            result = [fp(self.vacc[0] - fp(x[1] * y[2])), fp(self.vacc[1] - fp(x[2] * y[0])),
                      fp(self.vacc[2] - fp(x[0] * y[1])), 0.0]
            mask &= 0xE
        else:
            raise AssertionError(('VU', hex(word), op))
        for lane in range(4):
            if mask & (8 >> lane):
                if accumulator:
                    self.vacc[lane] = result[lane]
                elif destination:
                    self.vf[destination][lane] = bits(result[lane])

    # ---- one instruction (non-branch) ---------------------------------
    def execute(self, word, pc):
        r = self.r
        op = word >> 26
        rs, rt, rd = word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        imm = word & 0xFFFF
        simm = imm - 0x10000 if imm & 0x8000 else imm
        if op == 0:
            fn, sa = word & 63, word >> 6 & 31
            a, b = r[rs], r[rt]
            if fn == 0: v = sx32((b & 0xFFFFFFFF) << sa)
            elif fn == 2: v = sx32((b & 0xFFFFFFFF) >> sa)
            elif fn == 3: v = sx32(s32(b) >> sa)
            elif fn == 4: v = sx32((b & 0xFFFFFFFF) << (a & 31))
            elif fn == 6: v = sx32((b & 0xFFFFFFFF) >> (a & 31))
            elif fn == 7: v = sx32(s32(b) >> (a & 31))
            elif fn == 10: v = a if b == 0 else r[rd]                    # movz
            elif fn == 11: v = a if b != 0 else r[rd]                    # movn
            elif fn == 15: return                                        # sync
            elif fn == 16: v = self.hi
            elif fn == 18: v = self.lo
            elif fn == 20: v = (b << (a & 63)) & 0xFFFFFFFFFFFFFFFF      # dsllv
            elif fn == 22: v = (b & 0xFFFFFFFFFFFFFFFF) >> (a & 63)      # dsrlv
            elif fn == 23: v = (s64(b) >> (a & 63)) & 0xFFFFFFFFFFFFFFFF
            elif fn == 24:
                p = s32(a) * s32(b); self.lo = sx32(p); self.hi = sx32(p >> 32); v = self.lo
            elif fn == 25:
                p = (a & 0xFFFFFFFF) * (b & 0xFFFFFFFF)
                self.lo = sx32(p); self.hi = sx32(p >> 32); v = self.lo
            elif fn == 26:
                x, y = s32(a), s32(b)
                if y:
                    q = abs(x) // abs(y) * (1 if (x < 0) == (y < 0) else -1)
                    self.lo, self.hi = sx32(q), sx32(x - q * y)
                return
            elif fn == 27:
                x, y = a & 0xFFFFFFFF, b & 0xFFFFFFFF
                if y: self.lo, self.hi = sx32(x // y), sx32(x % y)
                return
            elif fn in (32, 33): v = sx32(a + b)
            elif fn in (34, 35): v = sx32(a - b)
            elif fn == 36: v = a & b
            elif fn == 37: v = a | b
            elif fn == 38: v = a ^ b
            elif fn == 39: v = ~(a | b) & 0xFFFFFFFFFFFFFFFF
            elif fn == 40: v = self.hi                                   # mfsa (unused)
            elif fn == 42: v = int(s64(a) < s64(b))
            elif fn == 43: v = int((a & 0xFFFFFFFFFFFFFFFF) < (b & 0xFFFFFFFFFFFFFFFF))
            elif fn in (44, 45): v = (a + b) & 0xFFFFFFFFFFFFFFFF
            elif fn in (46, 47): v = (a - b) & 0xFFFFFFFFFFFFFFFF
            elif fn == 56: v = (b << sa) & 0xFFFFFFFFFFFFFFFF
            elif fn == 58: v = (b & 0xFFFFFFFFFFFFFFFF) >> sa
            elif fn == 59: v = (s64(b) >> sa) & 0xFFFFFFFFFFFFFFFF
            elif fn == 60: v = (b << (sa + 32)) & 0xFFFFFFFFFFFFFFFF
            elif fn == 62: v = (b & 0xFFFFFFFFFFFFFFFF) >> (sa + 32)
            elif fn == 63: v = (s64(b) >> (sa + 32)) & 0xFFFFFFFFFFFFFFFF
            else: raise AssertionError(('SPECIAL', fn, hex(pc)))
            if rd: r[rd] = v & 0xFFFFFFFFFFFFFFFF
            return
        if op in (8, 9):
            v = sx32(r[rs] + simm)
        elif op == 10: v = int(s64(r[rs]) < simm)
        elif op == 11: v = int((r[rs] & 0xFFFFFFFFFFFFFFFF) < (simm & 0xFFFFFFFFFFFFFFFF))
        elif op == 12: v = r[rs] & imm
        elif op == 13: v = r[rs] | imm
        elif op == 14: v = r[rs] ^ imm
        elif op == 15: v = sx32(imm << 16)
        elif op in (24, 25): v = (r[rs] + simm) & 0xFFFFFFFFFFFFFFFF             # daddi(u)
        elif op == 32: v = self.load(r[rs] + simm, 1); v = v | ~0xFF if v & 0x80 else v
        elif op == 33: v = self.load(r[rs] + simm, 2); v = v | ~0xFFFF if v & 0x8000 else v
        elif op == 35: v = sx32(self.load(r[rs] + simm, 4))
        elif op == 36: v = self.load(r[rs] + simm, 1)
        elif op == 37: v = self.load(r[rs] + simm, 2)
        elif op == 39: v = self.load(r[rs] + simm, 4)
        elif op == 55: v = self.load(r[rs] + simm, 8)
        elif op == 30:                                                           # lq
            address = (r[rs] + simm) & ~15
            if rt:
                r[rt] = self.load(address, 8); self.rh[rt] = self.load(address + 8, 8)
            return
        elif op == 31:                                                           # sq
            address = (r[rs] + simm) & ~15
            self.save(address, r[rt], 8); self.save(address + 8, self.rh[rt], 8)
            return
        elif op == 40: self.save(r[rs] + simm, r[rt], 1); return
        elif op == 41: self.save(r[rs] + simm, r[rt], 2); return
        elif op == 43: self.save(r[rs] + simm, r[rt], 4); return
        elif op == 63: self.save(r[rs] + simm, r[rt], 8); return
        elif op == 49: self.f[rt] = self.load(r[rs] + simm); return            # lwc1
        elif op == 57: self.save(r[rs] + simm, self.f[rt]); return             # swc1
        elif op == 54:                                                           # lqc2
            address = (r[rs] + simm) & ~15
            self.vf[rt] = [self.load(address + 4 * i) for i in range(4)]
            if rt == 0: self.vf[0] = [0, 0, 0, bits(1.0)]
            return
        elif op == 62:                                                           # sqc2
            address = (r[rs] + simm) & ~15
            for i, value in enumerate(self.vf[rt]): self.save(address + 4 * i, value)
            return
        elif op == 47: return                                                    # cache
        elif op == 51: return                                                    # pref
        elif op == 17: self.cop1(word, pc); return
        elif op == 18: self.cop2(word, pc); return
        elif op == 28: self.mmi(word, pc); return
        elif op == 16:                                                           # cop0
            if rs == 0: v = 0
            else: return
        else:
            raise AssertionError(('opcode', op, hex(word), hex(pc)))
        if rt: r[rt] = v & 0xFFFFFFFFFFFFFFFF

    def cop1(self, word, pc):
        rs, rt = word >> 21 & 31, word >> 16 & 31
        fs, fd, fn = word >> 11 & 31, word >> 6 & 31, word & 63
        f = self.f
        if rs == 0:
            if rt: self.r[rt] = sx32(f[fs])
            return
        if rs == 4: f[fs] = self.r[rt] & 0xFFFFFFFF; return
        if rs == 2:
            if rt: self.r[rt] = 0
            return
        if rs == 6: return
        if rs == 20:
            if fn == 32: f[fd] = fbits(float(s32(f[fs]))); return
            raise AssertionError(('cvt', fn, hex(pc)))
        if rs != 16: raise AssertionError(('COP1', rs, hex(pc)))
        x, y = flt(f[fs]), flt(f[rt])
        if fn == 0: f[fd] = fbits(x + y)
        elif fn == 1: f[fd] = fbits(x - y)
        elif fn == 2: f[fd] = fbits(x * y)
        elif fn == 3:
            f[fd] = fbits(x / y) if y else (FLT_MAX_BITS | ((bits(x) ^ bits(y)) & 0x80000000))
        elif fn == 4: f[fd] = fbits(math.sqrt(abs(y)))
        elif fn == 5: f[fd] = f[fs] & 0x7FFFFFFF
        elif fn == 6: f[fd] = f[fs]
        elif fn == 7: f[fd] = f[fs] ^ 0x80000000
        elif fn == 22:
            f[fd] = fbits(x / math.sqrt(abs(y))) if y else FLT_MAX_BITS
        elif fn == 24: self.acc = fp(x + y)
        elif fn == 25: self.acc = fp(x - y)
        elif fn == 26: self.acc = fp(x * y)
        elif fn == 28: f[fd] = fbits(self.acc + fp(x * y))
        elif fn == 29: f[fd] = fbits(self.acc - fp(x * y))
        elif fn == 30: self.acc = fp(self.acc + fp(x * y))
        elif fn == 31: self.acc = fp(self.acc - fp(x * y))
        elif fn == 36:
            value = x
            f[fd] = (int(value) if abs(value) < 2147483648 else (0x7FFFFFFF if value > 0 else -0x80000000)) & 0xFFFFFFFF
        elif fn == 40: f[fd] = f[fs] if x >= y else f[rt]
        elif fn == 41: f[fd] = f[fs] if x <= y else f[rt]
        elif fn == 48: self.cond = False
        elif fn == 50: self.cond = x == y
        elif fn == 52: self.cond = x < y
        elif fn == 54: self.cond = x <= y
        else: raise AssertionError(('FPU', fn, hex(pc)))

    def cop2(self, word, pc):
        rs, rt, rd = word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        if rs == 1:                                                               # qmfc2
            if rt:
                value = sum(v << (32 * i) for i, v in enumerate(self.vf[rd]))
                self.r[rt] = value & 0xFFFFFFFFFFFFFFFF; self.rh[rt] = value >> 64
            return
        if rs == 5:                                                               # qmtc2
            value = (self.r[rt] & 0xFFFFFFFFFFFFFFFF) | (self.rh[rt] << 64)
            if rd: self.vf[rd] = [(value >> (32 * i)) & 0xFFFFFFFF for i in range(4)]
            return
        if rs >= 16: self.macro(word); return
        raise AssertionError(('COP2', rs, hex(pc)))

    def mmi(self, word, pc):
        rs, rt, rd = word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        fn, sub = word & 63, word >> 6 & 31
        full = lambda n: (self.r[n] & 0xFFFFFFFFFFFFFFFF) | (self.rh[n] << 64)

        def put(value):
            if rd:
                self.r[rd] = value & 0xFFFFFFFFFFFFFFFF; self.rh[rd] = (value >> 64) & 0xFFFFFFFFFFFFFFFF
        a, b = full(rs), full(rt)
        if fn == 0x18:                                     # mult1 (the pad quantizer 001B5CC0)
            product = s32(self.r[rs]) * s32(self.r[rt])
            self.lo1, self.hi1 = sx32(product), sx32(product >> 32)
            if rd: self.r[rd] = self.lo1 & 0xFFFFFFFFFFFFFFFF
        elif fn == 0x28 and sub == 0x18:                                          # paddub
            put(sum(min(255, ((a >> 8 * i) & 255) + ((b >> 8 * i) & 255)) << 8 * i for i in range(16)))
        elif fn == 0x29 and sub == 0x12:                                          # por
            put(a | b)
        elif fn == 0x29 and sub == 0x13:                                          # pnor
            put(~(a | b) & ((1 << 128) - 1))
        elif fn == 0x09 and sub == 0x12:                                          # pand
            put(a & b)
        elif fn == 0x09 and sub == 0x13:                                          # pxor
            put(a ^ b)
        elif fn == 0x09 and sub == 0x0E:                                          # pcpyld
            put(((a & 0xFFFFFFFFFFFFFFFF) << 64) | (b & 0xFFFFFFFFFFFFFFFF))
        elif fn == 0x29 and sub == 0x0E:                                          # pcpyud
            put((a >> 64) | ((b >> 64) << 64))
        else:
            raise AssertionError(('MMI', hex(fn), hex(sub), hex(pc)))

    # ---- control flow -----------------------------------------------------
    def branch(self, word, pc):
        """None for a plain instruction, else (taken, target, likely)."""
        op = word >> 26
        rs, rt = word >> 21 & 31, word >> 16 & 31
        imm = word & 0xFFFF
        target = pc + 4 + ((imm - 0x10000 if imm & 0x8000 else imm) << 2)
        r = self.r
        if op in (4, 5, 20, 21):
            taken = (r[rs] & 0xFFFFFFFFFFFFFFFF) == (r[rt] & 0xFFFFFFFFFFFFFFFF)
            if op in (5, 21): taken = not taken
            return taken, target, op >= 20
        if op in (6, 7, 22, 23):
            value = s64(r[rs])
            taken = value <= 0 if op in (6, 22) else value > 0
            return taken, target, op >= 22
        if op == 1:
            value = s64(r[rs])
            kind = rt & 3
            taken = value < 0 if kind in (0, 2) else value >= 0
            if rt & 0x10: r[31] = pc + 8
            return taken, target, bool(rt & 2)
        if op == 17 and rs == 8:
            taken = self.cond == bool(rt & 1)
            return taken, target, bool(rt & 2)
        return None

    def nested(self, entry, args=(), floats=()):
        """Call an original routine from inside a hook; the interrupted
        context (all registers) is restored afterwards. Returns (v0, f0)."""
        saved = (list(self.r), list(self.rh), self.hi, self.lo, list(self.f), self.acc,
                 self.cond, [list(v) for v in self.vf], list(self.vacc), self.q)
        self.r[29] = (self.r[29] - 0x400) & ~15
        self.call(entry, args, floats)
        result = (self.r[2], self.f[0])
        (self.r, self.rh, self.hi, self.lo, self.f, self.acc, self.cond, self.vf,
         self.vacc, self.q) = saved
        return result

    def call(self, entry, args=(), floats=()):
        for i, value in enumerate(args): self.r[4 + i] = sx32(value)
        for i, value in enumerate(floats): self.f[12 + i] = bits(value)
        self.r[31] = RETURN
        self.run(entry)

    def run(self, pc):
        hooks = self.hooks
        load = self.load
        steps = 0
        limit = self.limit
        while True:
            if pc == RETURN:
                self.steps += steps
                return
            hook = hooks.get(pc)
            if hook is not None:
                hook(self)
                pc = self.r[31] & 0xFFFFFFFF
                continue
            word = load(pc)
            steps += 1
            if steps > limit:
                raise AssertionError(('step limit', hex(pc)))
            op = word >> 26
            if op in (2, 3):
                if op == 3: self.r[31] = pc + 8
                self.execute(load(pc + 4), pc + 4)
                pc = (pc & 0xF0000000) | ((word & 0x3FFFFFF) << 2)
                continue
            if op == 0 and word & 63 in (8, 9):
                target = self.r[word >> 21 & 31] & 0xFFFFFFFF
                if word & 63 == 9:
                    rd = word >> 11 & 31
                    if rd: self.r[rd] = pc + 8
                self.execute(load(pc + 4), pc + 4)
                pc = target
                continue
            b = self.branch(word, pc)
            if b is None:
                self.execute(word, pc)
                pc += 4
                continue
            taken, target, likely = b
            if taken:
                self.execute(load(pc + 4), pc + 4)
                pc = target
            elif likely:
                pc += 8
            else:
                self.execute(load(pc + 4), pc + 4)
                pc += 8


def record_writes(ee, base, limit=0x320):
    """Record every byte offset in [base, base + limit) that ee stores to from
    now on (instructions and hooks alike). The unit oracles assert the result
    lies inside their compared field set, so a field the original writes but
    the native mirror lacks fails instead of passing silently."""
    written = set()
    save = ee.save

    def guarded(address, value, size=4):
        at = (address & 0xFFFFFFFF) - base
        for i in range(size):
            if 0 <= at + i < limit:
                written.add(at + i)
        save(address, value, size)
    ee.save = guarded
    return written


def assert_covered(written, covered, where):
    """Every recorded actor byte must be one the test compares."""
    missing = sorted(written - covered)
    assert not missing, (where, 'original writes actor bytes the test does not compare',
                         [hex(m) for m in missing])


def read_elf():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA256, 'wrong original executable'
    return elf


# ======================================================================
# Slide state 0x1C: original instructions versus em_player_slide.c
# ======================================================================
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode  # noqa: E402

ACTOR, NODES = 0x680000, 0x6D0000
SLIDE, SIDE, RELEASE, MOTION = 0x16C6A0, 0x16C570, 0x16C520, 0x16CD70
STEER, STEER_INPUT, SWEEPS, APPROACH = 0x17F5F0, 0x174FD0, 0x1791D0, 0x1B12B0
REQUEST, ARBITER, FRAMES = 0x1749A0, 0x1749F0, 0x1C61D0
SOUND, STOP, EFFECT = 0x1FBD50, 0x11A070, 0x1EFD90
MOVE, SWEEP, FLOOR, FALL, TRANSLATE = 0x19AD00, 0x19AFE0, 0x175900, 0x1796C0, 0x178B90
DAMAGE, LAND_CHECK, LAND, STEP_SOUND, LAND_SOUND = 0x224B80, 0x224290, 0x17C580, 0x182430, 0x182870
SURFACE5D, TELEPORT = 0x21D250, 0x21D2E0
SINE, COSINE, ATAN2, FABS = 0x11E2A8, 0x11DE90, 0x11E620, 0x11DF78

LIBC = C.CDLL(None)
for _name in ('sinf', 'cosf'):
    getattr(LIBC, _name).argtypes = [C.c_float]; getattr(LIBC, _name).restype = C.c_float
LIBC.atan2f.argtypes = [C.c_float, C.c_float]; LIBC.atan2f.restype = C.c_float


class ProbeHit(C.Structure):
    _fields_ = [('kind', C.c_int), ('node', C.c_uint16), ('entity_flags', C.c_uint8),
                ('entity_type', C.c_uint8), ('entity', C.c_int), ('point', C.c_float * 3),
                ('delta', C.c_float * 3), ('normal', C.c_float * 3), ('axis', C.c_float * 3),
                ('owner', C.c_void_p)]   # EmPlayerProbeHit.owner (0x700031D4)


class SlideActor(C.Structure):
    _fields_ = [('position', C.c_float * 3), ('rotation', C.c_float * 3), ('speed', C.c_float),
                ('clock', C.c_float), ('slope', C.c_float), ('slide_yaw', C.c_float),
                ('root_prev', C.c_float), ('impact', C.c_float), ('fall_rate', C.c_float),
                ('ramp', C.c_float), ('drop', C.c_float), ('land_y', C.c_float),
                ('ramp_timer', C.c_float), ('entry_y', C.c_float), ('pad_x', C.c_float),
                ('pad_y', C.c_float), ('steer', C.c_int32), ('anim_flags', C.c_uint32),
                ('ticks', C.c_int16), ('sound_id', C.c_uint16), ('sound', C.c_int8),
                ('sound_live', C.c_uint8), ('major', C.c_uint8), ('state', C.c_uint8),
                ('walk', C.c_uint8), ('sub', C.c_uint8), ('mode', C.c_uint8),
                ('variant', C.c_uint8), ('lean', C.c_uint8), ('lock', C.c_uint8),
                ('step', C.c_uint8), ('obstruction', C.c_uint8), ('slide', C.c_uint8),
                ('surface', C.c_uint8), ('gait', C.c_uint8), ('contact', C.c_uint8)]


class SlideScene(C.Structure):
    _fields_ = [('root_forward', C.c_float), ('hip', C.c_float * 2), ('scripted', C.c_uint8),
                ('pad_gait', C.c_uint8), ('pad_x', C.c_uint8), ('pad_y', C.c_uint8)]


REQUEST_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.c_int, C.c_float)
ARBITER_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.c_float, C.c_float)
FRAMES_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.POINTER(C.c_int))
SOUND_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint, C.POINTER(C.c_int))
STOP_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int)
EFFECT_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.POINTER(C.c_float), C.POINTER(C.c_float))
MOVE_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_float), C.POINTER(C.c_float), C.c_uint)
SWEEP_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_float), C.POINTER(C.c_float), C.c_uint,
                       C.POINTER(ProbeHit))
FLOOR_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(SlideActor), C.c_int, C.POINTER(C.c_int))
ACTOR_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(SlideActor))
TRANSLATE_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(SlideActor), C.c_int)
RESULT_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(SlideActor), C.POINTER(C.c_int))
TIER_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int)
PLAIN_FN = C.CFUNCTYPE(C.c_int, C.c_void_p)
MATH1_FN = C.CFUNCTYPE(C.c_float, C.c_void_p, C.c_float)
MATH2_FN = C.CFUNCTYPE(C.c_float, C.c_void_p, C.c_float, C.c_float)


class SlideWorkers(C.Structure):
    _fields_ = [('context', C.c_void_p), ('request', REQUEST_FN), ('arbiter', ARBITER_FN),
                ('clip_frames', FRAMES_FN), ('sound', SOUND_FN), ('stop_sound', STOP_FN),
                ('effect', EFFECT_FN), ('move', MOVE_FN), ('sweep', SWEEP_FN), ('floor', FLOOR_FN),
                ('fall', ACTOR_FN), ('translate', TRANSLATE_FN), ('damage', RESULT_FN),
                ('land_check', RESULT_FN), ('land', ACTOR_FN), ('step_sound', TIER_FN),
                ('land_sound', TIER_FN), ('surface5d', PLAIN_FN), ('teleport', PLAIN_FN),
                ('sine', MATH1_FN), ('cosine', MATH1_FN), ('atan2', MATH2_FN)]


# (name, actor offset, size, kind): f float, u unsigned, s signed
FIELDS = [('speed', 0x38, 4, 'f'), ('clock', 0x3C, 4, 'f'), ('slope', 0x9C, 4, 'f'),
          ('slide_yaw', 0x218, 4, 'f'), ('root_prev', 0x21C, 4, 'f'), ('impact', 0x26C, 4, 'f'),
          ('fall_rate', 0x2E0, 4, 'f'), ('ramp', 0x2E4, 4, 'f'), ('drop', 0x2EC, 4, 'f'),
          ('land_y', 0x2F4, 4, 'f'), ('ramp_timer', 0x2F8, 4, 'f'), ('entry_y', 0x294, 4, 'f'),
          ('pad_x', 0x244, 4, 'f'), ('pad_y', 0x248, 4, 'f'), ('steer', 0x24C, 4, 's'),
          ('anim_flags', 0x200, 4, 'u'), ('ticks', 0x2A, 2, 's'), ('sound_id', 0x31C, 2, 'u'),
          ('sound', 0x31B, 1, 's'), ('sound_live', 0x31A, 1, 'u'), ('major', 4, 1, 'u'),
          ('state', 5, 1, 'u'), ('walk', 6, 1, 'u'), ('sub', 7, 1, 'u'), ('mode', 0x1F0, 1, 'u'),
          ('variant', 0x1F1, 1, 'u'), ('lean', 0x25D, 1, 'u'), ('lock', 0x25F, 1, 'u'),
          ('step', 0x302, 1, 'u'), ('obstruction', 0x314, 1, 'u'), ('slide', 0x237, 1, 'u'),
          ('surface', 0x23A, 1, 'u'), ('gait', 0x23F, 1, 'u'), ('contact', 0xA, 1, 'u')]


def raw_of(value, size, kind):
    if kind == 'f':
        return bits(value)
    return value & ((1 << (8 * size)) - 1)


class Script:
    """Scripted boundary results, consumed in call order by both sides."""

    def __init__(self, rng=None):
        self.moves, self.sweeps, self.floors, self.damages = [], [], [], []
        self.land_checks, self.translates, self.handles = [], [], []
        if rng is None:
            return
        for _ in range(4):
            self.moves.append((rng.choice([0, 0, 2, 4]), rng.uniform(-1, 1), rng.uniform(-1, 1)))
        for _ in range(40):
            hit = ProbeHit()
            hit.kind = rng.choice([0, 0, 0, 0, 2, 4, 1])
            hit.node = rng.choice([0x2000, 0x2005, 0x4005, 0x1005, 0x8000, rng.randrange(65536)])
            for i in range(3): hit.delta[i] = rng.choice([0.0, rng.uniform(-0.5, 0.5)])
            self.sweeps.append(hit)
        for _ in range(4):
            result = rng.choice([0, 1, 2])
            self.floors.append((result, rng.choice([0, 1, 0x81, 2]) if result else 0,
                                rng.choice([0, 1]), rng.choice([0.0, rng.uniform(-1, 1)])))
        self.damages = [rng.choice([0, 0, 1, 2]) for _ in range(2)]
        self.land_checks = [rng.choice([0, 1]) for _ in range(2)]
        self.translates = [rng.choice([0, 0, 1, 0x80, 3]) for _ in range(3)]
        self.handles = [rng.choice([-1, 0, 5, 0x7F, 300]) for _ in range(3)]

    def copy(self):
        other = Script()
        for name in ('moves', 'sweeps', 'floors', 'damages', 'land_checks', 'translates', 'handles'):
            setattr(other, name, list(getattr(self, name)))
        return other

    @staticmethod
    def pop(queue, default):
        return queue.pop(0) if queue else default


def clip_length(bank, clip):
    header = struct.unpack_from('<I', bank, 4 + clip * 4)[0]
    return struct.unpack_from('<H', bank, header + 2)[0]


def fvec(values, count=3):
    return tuple(bits(values[i]) for i in range(count))


class SlideOracle:
    """The original routines in EE, every boundary scripted and logged."""

    def __init__(self, elf, bank, script, scene):
        self.ee = ee = EE(elf)
        self.bank, self.script, self.log = bank, script, []
        ee.save(0x275B40, NODES)
        ee.save(NODES, NODES + 0x100); ee.save(NODES + 4, NODES + 0x200)
        ee.putf(NODES + 0x100 + 8, scene.root_forward)
        ee.putf(NODES + 0x200 + 0xC0, scene.hip[0]); ee.putf(NODES + 0x200 + 0xC8, scene.hip[1])
        ee.save(0x70003B8D, scene.scripted, 1)
        ee.save(0x810E57, scene.pad_gait, 1)
        ee.save(0x810E64, scene.pad_x, 1); ee.save(0x810E65, scene.pad_y, 1)
        ee.save(ACTOR + 0x40, 0x500000)
        h = ee.hooks
        h[REQUEST] = lambda e: self.rec('request', e.arg(1) & 0xFFFF, e.arg(2), e.f[12])
        h[ARBITER] = lambda e: self.rec('arbiter', e.arg(1) & 0xFFFF, e.f[12], e.f[13])
        h[FRAMES] = self.frames
        h[SOUND] = self.sound
        h[STOP] = lambda e: self.rec('stop', s32(e.arg(0)))
        h[EFFECT] = lambda e: self.rec('effect', e.arg(0), e.vector(e.arg(1)), e.vector(e.arg(2)))
        h[MOVE] = self.move
        h[SWEEP] = self.sweep
        h[FLOOR] = self.floor
        h[FALL] = lambda e: self.rec('fall')
        h[TRANSLATE] = self.translate
        h[DAMAGE] = lambda e: self.result('damage', self.script.damages)
        h[LAND_CHECK] = lambda e: self.result('land_check', self.script.land_checks)
        h[LAND] = lambda e: self.rec('land')
        h[STEP_SOUND] = lambda e: self.rec('step_sound', e.arg(1))
        h[LAND_SOUND] = lambda e: self.rec('land_sound', e.arg(1))
        h[SURFACE5D] = lambda e: self.rec('surface5d', e.arg(1))
        h[TELEPORT] = lambda e: self.rec('teleport', e.arg(1), e.arg(2))
        h[SINE] = lambda e: e.ret_float(LIBC.sinf(e.farg(0)))
        h[COSINE] = lambda e: e.ret_float(LIBC.cosf(e.farg(0)))
        h[ATAN2] = lambda e: e.ret_float(LIBC.atan2f(e.farg(0), e.farg(1)))
        h[FABS] = lambda e: e.ret_float(abs(e.farg(0)))

    def rec(self, *entry):
        assert self.ee.arg(0) == ACTOR or entry[0] in ('stop', 'effect'), entry
        self.log.append(entry)
        self.ee.ret_int(0)

    def result(self, name, queue):
        value = Script.pop(queue, 0)
        self.log.append((name,))
        self.ee.ret_int(value)

    def frames(self, e):
        clip = e.arg(1) & 0xFFFF
        self.log.append(('frames', clip))
        e.ret_int(clip_length(self.bank, clip))

    def sound(self, e):
        self.log.append(('sound', e.arg(1), e.arg(2), e.f[12]))
        e.ret_int(Script.pop(self.script.handles, 0))

    def move(self, e):
        kind, dx, dz = Script.pop(self.script.moves, (0, 0.0, 0.0))
        self.log.append(('move', e.vector(e.arg(1), 4), e.arg(2)))
        if kind:
            e.putf(ACTOR + 0xB0, fp(e.getf(ACTOR + 0xB0) + dx))
            e.putf(ACTOR + 0xB8, fp(e.getf(ACTOR + 0xB8) + dz))
        e.ret_int(kind)

    def sweep(self, e):
        hit = Script.pop(self.script.sweeps, ProbeHit())
        self.log.append(('sweep', e.vector(e.arg(1), 4), e.vector(e.arg(2), 4), e.arg(3)))
        e.save(0x700031D8, hit.kind)
        if hit.kind:
            e.save(0x700031D0, 0x6A0000)
            e.save(0x6A0000 + 0x1A, hit.node, 2)
            for i in range(3): e.putf(0x700031C0 + 4 * i, hit.delta[i])
        else:
            e.save(0x700031D0, 0)
        e.ret_int(hit.kind)

    def floor(self, e):
        result, contact, slide, dy = Script.pop(self.script.floors, (0, 0, 0, 0.0))
        self.log.append(('floor', e.arg(1), e.vector(ACTOR + 0xB0)))
        e.save(ACTOR + 0xA, contact, 1)
        e.save(ACTOR + 0x237, slide, 1)
        e.putf(ACTOR + 0xB4, fp(e.getf(ACTOR + 0xB4) + dy))
        e.ret_int(result)

    def translate(self, e):
        value = Script.pop(self.script.translates, 0)
        self.log.append(('translate', e.arg(1), e.load(ACTOR + 0x38)))
        e.save(ACTOR + 0x314, value, 1)
        e.ret_int(0)

    def load(self, actor):
        for name, offset, size, kind in FIELDS:
            self.ee.save(ACTOR + offset, raw_of(getattr(actor, name), size, kind), size)
        for i in range(3):
            self.ee.putf(ACTOR + 0xB0 + 4 * i, actor.position[i])
            self.ee.putf(ACTOR + 0xC0 + 4 * i, actor.rotation[i])

    def fields(self):
        out = {name: self.ee.load(ACTOR + offset, size) for name, offset, size, _ in FIELDS}
        for i in range(3):
            out['position%d' % i] = self.ee.load(ACTOR + 0xB0 + 4 * i)
            out['rotation%d' % i] = self.ee.load(ACTOR + 0xC0 + 4 * i)
        return out


# The actor bytes fields() compares: FIELDS plus position +B0..+BB and rotation +C0..+CB.
COMPARED = ({offset + i for _, offset, size, _ in FIELDS for i in range(size)} |
            set(range(0xB0, 0xBC)) | set(range(0xC0, 0xCC)))


class SlideNative:
    def __init__(self, bank, script):
        self.bank, self.script, self.log = bank, script, []
        self.workers = SlideWorkers(
            None, REQUEST_FN(self.request), ARBITER_FN(self.arbiter), FRAMES_FN(self.frames),
            SOUND_FN(self.sound), STOP_FN(self.stop), EFFECT_FN(self.effect), MOVE_FN(self.move),
            SWEEP_FN(self.sweep), FLOOR_FN(self.floor), ACTOR_FN(lambda _, a: self.plain('fall')),
            TRANSLATE_FN(self.translate), RESULT_FN(self.damage), RESULT_FN(self.land_check),
            ACTOR_FN(lambda _, a: self.plain('land')),
            TIER_FN(lambda _, t: self.plain('step_sound', t)),
            TIER_FN(lambda _, t: self.plain('land_sound', t)),
            PLAIN_FN(lambda _: self.plain('surface5d', 0)),
            PLAIN_FN(lambda _: self.plain('teleport', 0x78, 0)),
            MATH1_FN(lambda _, x: LIBC.sinf(x)), MATH1_FN(lambda _, x: LIBC.cosf(x)),
            MATH2_FN(lambda _, y, x: LIBC.atan2f(y, x)))

    def plain(self, *entry):
        self.log.append(entry); return 0

    def request(self, _, clip, force, blend):
        return self.plain('request', clip & 0xFFFF, force, bits(blend))

    def arbiter(self, _, clip, blend, frame):
        return self.plain('arbiter', clip & 0xFFFF, bits(blend), bits(frame))

    def frames(self, _, clip, out):
        self.log.append(('frames', clip)); out[0] = clip_length(self.bank, clip); return 0

    def sound(self, _, sound, out):
        self.log.append(('sound', sound, 0, bits(300.0)))
        out[0] = Script.pop(self.script.handles, 0); return 0

    def stop(self, _, handle):
        return self.plain('stop', handle)

    def effect(self, _, id, position, rotation):
        return self.plain('effect', id, fvec(position), fvec(rotation))

    def move(self, _, position, target, mask):
        kind, dx, dz = Script.pop(self.script.moves, (0, 0.0, 0.0))
        self.log.append(('move', fvec(target, 4), mask))
        if kind:
            position[0] = fp(position[0] + dx); position[2] = fp(position[2] + dz)
        return kind

    def sweep(self, _, start, end, mask, out):
        hit = Script.pop(self.script.sweeps, ProbeHit())
        self.log.append(('sweep', fvec(start, 4), fvec(end, 4), mask))
        C.memmove(out, C.byref(hit), C.sizeof(ProbeHit))
        return hit.kind

    def floor(self, _, actor, search, out):
        result, contact, slide, dy = Script.pop(self.script.floors, (0, 0, 0, 0.0))
        a = actor.contents
        self.log.append(('floor', search, fvec(a.position)))
        a.contact, a.slide = contact, slide
        a.position[1] = fp(a.position[1] + dy)
        out[0] = result; return 0

    def translate(self, _, actor, arg):
        value = Script.pop(self.script.translates, 0)
        self.log.append(('translate', arg, bits(actor.contents.speed)))
        actor.contents.obstruction = value; return 0

    def damage(self, _, actor, out):
        self.log.append(('damage',)); out[0] = Script.pop(self.script.damages, 0); return 0

    def land_check(self, _, actor, out):
        self.log.append(('land_check',)); out[0] = Script.pop(self.script.land_checks, 0); return 0


def native_fields(actor):
    out = {name: raw_of(getattr(actor, name), size, kind) for name, _, size, kind in FIELDS}
    for i in range(3):
        out['position%d' % i] = bits(actor.position[i])
        out['rotation%d' % i] = bits(actor.rotation[i])
    return out


def normal_log(log):
    """Oracle log floats are raw register words; the native log packs bits."""
    out = []
    for entry in log:
        name = entry[0]
        if name == 'request':
            out.append((name, entry[1], entry[2], entry[3] & 0xFFFFFFFF))
        elif name == 'arbiter':
            out.append((name, entry[1], entry[2] & 0xFFFFFFFF, entry[3] & 0xFFFFFFFF))
        elif name == 'sound':
            out.append((name, entry[1], entry[2], entry[3] & 0xFFFFFFFF))
        elif name in ('surface5d', 'teleport'):
            # The native workers take no arguments: the binder supplies the
            # constants the original passes (0021D250(p, 0), 0021D2E0(p, 0x78, 0)),
            # so the recorded original arguments must equal exactly those.
            assert entry == (('surface5d', 0) if name == 'surface5d' else ('teleport', 0x78, 0)), entry
            out.append(entry)
        else:
            out.append(entry)
    return out


def random_actor(rng, walk=None):
    a = SlideActor()
    for i in range(3): a.position[i] = rng.uniform(-300, 300)
    a.rotation[0] = rng.choice([0.0, rng.uniform(-1, 1)])
    a.rotation[1] = rng.uniform(-3.14159, 3.14159)
    a.rotation[2] = 0.0
    a.speed = rng.choice([0.0, 0.2, rng.uniform(0, 1.6), 1.5, number(bits(1.5) + 1)])
    a.clock = rng.choice([rng.uniform(0, 40), 24.0, 13.0, 2.0, number(bits(24.0) + 1)])
    a.slope = rng.choice([0.0, rng.uniform(0, 1.2), 0.6])
    a.slide_yaw = rng.choice([a.rotation[1], rng.uniform(-3.14159, 3.14159)])
    if rng.random() < 0.2: a.rotation[0] = a.slope
    a.root_prev = rng.uniform(-5, 5)
    a.impact = rng.choice([1.0, rng.uniform(0, 1)])
    a.fall_rate = rng.uniform(0, 0.02)
    a.ramp = rng.uniform(-0.02, 0.02)
    a.drop = rng.choice([0.0, -0.04, -0.16, number(bits(-0.19999999)), -0.2, -0.24, rng.uniform(-4.2, 0)])
    a.land_y = rng.uniform(150, 250)
    a.ramp_timer = rng.choice([0.0, 1.0, 30.0, rng.uniform(-2, 30)])
    a.entry_y = rng.uniform(150, 250)
    a.pad_x = rng.uniform(-1, 1); a.pad_y = rng.uniform(-1, 1)
    a.steer = rng.choice([-1, 0, 1, 2, 3])
    a.anim_flags = rng.choice([0, 0x1000, 0x8000, 0x9000])
    a.ticks = rng.choice([0, 7, 15, rng.randrange(-100, 100)])
    a.sound_id = rng.choice([0x12E, 0x12E, 0x5DD])
    a.sound = rng.choice([-1, -1, 3])
    a.sound_live = rng.choice([0, 1])
    a.major = rng.choice([1, 1, 4]); a.state = rng.choice([0x1C, 0x1C, 9])
    a.walk = walk if walk is not None else rng.choice([0, 1, 2, 3, 3, 3, 0xA, 0xB, 0xC, 0x14, 0x15, 0x1E, 5])
    a.sub = rng.randrange(3); a.mode = rng.choice([0x30, 0, 0xB])
    a.variant = rng.choice([0, 1, 2]); a.lean = rng.choice([0, 1, 2])
    a.lock = rng.randrange(3); a.step = rng.randrange(5)
    a.obstruction = rng.randrange(256); a.slide = rng.choice([0, 1])
    a.surface = rng.choice([0, 5, 5, 6, 7, 8, 0x5A, 0x5B, 0x5C, 0x5D, 0xD])
    a.gait = rng.randrange(4); a.contact = rng.choice([0, 1])
    return a


def random_scene(rng):
    s = SlideScene()
    s.root_forward = rng.uniform(-10, 10)
    s.hip[0] = rng.uniform(-300, 300); s.hip[1] = rng.uniform(-300, 300)
    s.scripted = rng.choice([0, 0, 0, 1])
    s.pad_gait = rng.randrange(4)
    s.pad_x = rng.choice([0, 0x80, 0xFF, rng.randrange(256)])
    s.pad_y = rng.choice([0, 0x80, 0xFF, rng.randrange(256)])
    return s


ROUTINES = {  # name -> (original entry, native function, extra args)
    'tick': (SLIDE, 'em_player_slide_tick', ()),
    'motion': (MOTION, 'em_player_slide_motion', ('hold',)),
    'steer': (STEER, 'em_player_slide_steer', ('hold',)),
    'steer_input': (STEER_INPUT, 'em_player_slide_steer_input', ()),
    'sweeps': (SWEEPS, 'em_player_slide_sweeps', ()),
    'side': (SIDE, 'em_player_slide_side_probes', ()),
    'release': (RELEASE, 'em_player_slide_release_sound', ()),
}


def build_native():
    out = ROOT / 'build/player_slide_reference'
    out.mkdir(parents=True, exist_ok=True)
    lib = out / ('slide.dylib' if sys.platform == 'darwin' else 'slide.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-shared', '-fPIC', '-Isrc', 'src/game/em_player_slide.c',
                    'src/game/em_player_floor.c', '-lm', '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    A, S, W = C.POINTER(SlideActor), C.POINTER(SlideScene), C.POINTER(SlideWorkers)
    native.em_player_slide_tick.argtypes = [A, S, W]
    native.em_player_slide_motion.argtypes = [A, S, C.c_int, W]
    native.em_player_slide_steer.argtypes = [A, S, C.c_int, W]
    native.em_player_slide_steer_input.argtypes = [A, S, W]
    native.em_player_slide_sweeps.argtypes = [A, S, W]
    native.em_player_slide_side_probes.argtypes = [A, W]
    native.em_player_slide_release_sound.argtypes = [A, W]
    native.em_player_slide_approach.argtypes = [C.c_float, C.c_float, C.c_float]
    native.em_player_slide_approach.restype = C.c_float
    return native


ELF = BANK = NATIVE = None


def run_case(case):
    routine, seed = case
    rng = random.Random(seed)
    walk = None
    if routine == 'tick' and seed % 7 == 0:
        walk = 3
    actor, scene = random_actor(rng, walk), random_scene(rng)
    hold = rng.choice([0, 0, 1])
    script = Script(rng)
    entry, name, extra = ROUTINES[routine]
    oracle = SlideOracle(ELF, BANK, script.copy(), scene)
    oracle.load(actor)
    args = (ACTOR, hold) if extra else (ACTOR,)
    written = record_writes(oracle.ee, ACTOR)
    oracle.ee.call(entry, args)
    assert_covered(written, COMPARED, (routine, seed))
    native = SlideNative(BANK, script.copy())
    fn = getattr(NATIVE, name)
    if routine in ('side', 'release'):
        result = fn(C.byref(actor), C.byref(native.workers))
    elif extra:
        result = fn(C.byref(actor), C.byref(scene), hold, C.byref(native.workers))
    else:
        result = fn(C.byref(actor), C.byref(scene), C.byref(native.workers))
    if routine == 'motion':
        assert result == s32(oracle.ee.r[2]), (routine, seed, result, oracle.ee.r[2])
    else:
        assert result == 0, (routine, seed, result)
    expected, got = normal_log(oracle.log), native.log
    assert expected == got, (routine, seed, expected, got)
    want, have = oracle.fields(), native_fields(actor)
    for key in want:
        assert want[key] == have[key], (routine, seed, key, hex(want[key]), hex(have[key]))
    return routine, len(got)


def approach_cases(rng, count):
    cases = []
    for _ in range(count):
        target = rng.choice([rng.uniform(-3.2, 3.2), 0.0, -0.0])
        current = rng.choice([target, number(bits(target) + 1) if target else 1e-7, rng.uniform(-3.2, 3.2)])
        rate = rng.choice([0.10471976, 0.06981317, rng.uniform(0, 0.5)])
        cases.append((target, current, rate))
        # the |difference| == rate boundary on both sides
        current = number(bits(rng.uniform(-3.0, 3.0)))
        rate = number(bits(rng.choice([0.10471976, 0.06981317, 0.25, 0.5])))
        for sign in (1, -1):
            target = fp(current + sign * rate)
            if fp(target - current) == sign * rate:
                cases.append((target, current, rate))
    return cases


def main():
    global ELF, BANK, NATIVE
    ELF = read_elf()
    BANK = (DECOMP / 'extract/chunk28/f01_id3c.bin').read_bytes()
    NATIVE = build_native()
    rng = random.Random(0x16C6A0)
    counts = {}

    approaches = approach_cases(rng, reference_mode.pick(20000, 1500))
    for target, current, rate in approaches:
        ee = EE(ELF)
        ee.call(APPROACH, floats=(target, current, rate))
        got = bits(NATIVE.em_player_slide_approach(target, current, rate))
        assert got == ee.f[0], (target, current, rate, hex(got), hex(ee.f[0]))
    counts['approach'] = len(approaches)

    per_routine = {'tick': (12000, 900), 'motion': (6000, 400), 'steer': (6000, 400),
                   'steer_input': (3000, 200), 'sweeps': (2000, 120), 'side': (2000, 120),
                   'release': (1000, 60)}
    cases = []
    for routine, (full, quick) in per_routine.items():
        cases += [(routine, 1000003 * i + zlib.crc32(routine.encode()) % 997) for i in range(reference_mode.pick(full, quick))]
    results = reference_mode.parallel_map(run_case, cases)
    for routine, _ in results:
        counts[routine] = counts.get(routine, 0) + 1
    # The live state 0x1C's mirror (em_player_slide_actor_*_live) against the
    # offset table every case above compares the original through.
    from test_player_floor_reference import LiveActor, live_mapping_check
    NATIVE.em_player_slide_actor_from_live.argtypes = [C.POINTER(LiveActor), C.POINTER(SlideActor)]
    NATIVE.em_player_slide_actor_to_live.argtypes = [C.POINTER(SlideActor), C.POINTER(LiveActor)]
    table = [(name, None, offset, size) for name, offset, size, _ in FIELDS] + \
            [(v, i, base + 4 * i, 4) for v, base in (('position', 0xB0), ('rotation', 0xC0)) for i in range(3)]
    counts['live_mirror_fields'] = live_mapping_check(
        NATIVE.em_player_slide_actor_from_live, NATIVE.em_player_slide_actor_to_live, SlideActor, table, rng)
    reference_mode.banner(*('%s %d' % (k, v) for k, v in counts.items()))
    print('player slide reference: PASS (original 0016C6A0/0016C570/0016C520/0016CD70/'
          '0017F5F0/00174FD0/001791D0/001B12B0 instructions)')


# ======================================================================
# Whole-world mode (EM_TEST_WORLD=1): the original player stage over the
# captured AREA11 world, with and without the native state-0x1C callback
# ======================================================================
PLAYER = 0x8102B0
WORLD_RAM = REFERENCE / 'playable_ee.bin'
# The state04 scratchpad, extracted on demand from the startup-reference save
# state (whose eeMemory.bin is byte-identical to playable_ee.bin) with the
# decomp venv's zstd reader, as test_area11_sfx_reference.py does.
WORLD_STATE = REFERENCE / 'portable-data/sstates/SCUS-97112 (0AE679AF).04.p2s'
WORLD_SPAD = ROOT / 'build/player_world_reference/state04_scratchpad.bin'


def world_inputs():
    """None when RAM and scratchpad are available (extracting the scratchpad
    if needed), else the reason world checks must be skipped."""
    if not WORLD_RAM.exists():
        return 'missing %s' % WORLD_RAM
    if WORLD_SPAD.exists():
        return None
    venv = DECOMP / '.venv/bin/python'
    if not WORLD_STATE.exists() or not venv.exists():
        return 'cannot extract the state04 scratchpad (%s, %s)' % (WORLD_STATE, venv)
    WORLD_SPAD.parent.mkdir(parents=True, exist_ok=True)
    code = ('import sys; from pathlib import Path; from tools.parse_pcsx2_state import extract_zstd_entry; '
            'Path(sys.argv[2]).write_bytes(extract_zstd_entry(Path(sys.argv[1]), "Scratchpad.bin"))')
    subprocess.run([str(venv), '-c', code, str(WORLD_STATE), str(WORLD_SPAD)], cwd=DECOMP, check=True)
    assert WORLD_SPAD.stat().st_size == 0x4000, WORLD_SPAD
    return None
SOUND_HOOKS = {0x1FBD50: 'sound3d', 0x1FB9F0: 'sound', 0x11A070: 'stopsound',
               0x1EFD90: 'effect', 0x1F0460: 'decal', 0x1E8B90: 'wade'}


class Stage:
    """The original 0015BCF0 player stage on a captured RAM image. Only the
    player's placement and the pad bytes are seeded; sound/effect submission
    calls are recorded and return 0 (they reach the IOP/renderer)."""

    def __init__(self, elf, ram, spad, position, yaw):
        self.ee = ee = EE(elf, ram, spad)
        self.frame, self.events = 0, []
        for address, name in SOUND_HOOKS.items():
            ee.hooks[address] = self.recorder(name)
        for base in (0xA0, 0xB0):
            for i, value in enumerate(position):
                ee.putf(PLAYER + base + 4 * i, value)
        ee.putf(PLAYER + 0xC4, yaw)

    def recorder(self, name):
        def hook(ee):
            # only each entry's own arguments (the rest are stale registers)
            ints, floats = {'sound3d': (3, 1), 'sound': (4, 0), 'stopsound': (1, 0),
                            'effect': (3, 0), 'decal': (2, 0), 'wade': (1, 1)}[name]
            self.events.append((self.frame, name) + tuple(ee.arg(i) for i in range(ints)) +
                               tuple(ee.f[12 + i] for i in range(floats)))
            ee.ret_int(0)
        return hook

    def step(self, gait=0, lx=128, ly=128, press=0, camera=None):
        ee = self.ee
        ee.save(0x275B40, PLAYER + 0x110)       # anim_bone_array_setup for the player
        ee.save(0x810E57, gait, 1); ee.save(0x810E64, lx, 1); ee.save(0x810E65, ly, 1)
        ee.save(0x810E70, press, 2); ee.save(0x810E74, press, 2)
        if camera is not None:
            ee.putf(0x8106A0, camera)
        ee.save(0x70003B68, ee.load(0x70003B68) + 1)
        ee.call(0x15BCF0, (PLAYER,))
        self.frame += 1

    def actor(self):
        return self.ee.read(PLAYER, 0x320)


ROUTE = DECOMP / 'build/s87/route'
PAD_READ, PAD_UNPACK = 0x110B38, 0x1B5940
PAD_LATENCY = 3


class RouteReplay(Stage):
    """The original player stage replayed over a PCSX2 route beat
    (docs/FIRST_LEVEL_ROUTE.md): it starts from the RAM/scratchpad snapshot the
    beat resumed from (trace['source']) and runs, per frame, what the real
    frame feeds the player stage:

    - the original pad unpack 001B5940 (step C) on the beat's recorded pad
      input, supplied through the libpad read 00110B38 as
      test_input_block_reference.py does. An input recorded at trace frame f
      reaches the stage PAD_LATENCY frames later (05_boxes: Cross at f172,
      climb state at row f175);
    - the camera the stage reads, from the previous row: eye 0x8105D0,
      target 0x8105E0, forward 0x810600 and the heading D_008106A0, computed
      as the byte-matched camera commit 0018C0D0 does (the original atan2
      0011E620 of (-forward.z, forward.x)). The trace prints them to 5
      decimals, so the heading is that close, not bit-exact;
    - the main-loop counter 0x70003B64 (row n's frame runs with n - 1; it is
      incremented at step W) and the gameplay counters 0x810750/0x70003B68
      (incremented by 001AE5E0 before the stage).

    Nothing else runs (no camera stage, owners or scripts), so a replay is
    only valid while the beat's player is not driven by them."""

    def __init__(self, elf, trace, ram, spad):
        self.ee = ee = EE(elf, ram, spad)
        self.frame, self.events = 0, []
        for address, name in SOUND_HOOKS.items():
            ee.hooks[address] = self.recorder(name)
        ee.hooks[PAD_READ] = self.pad_read
        self.raw = bytes(8)
        self.rows = {r['counter']: r for r in trace['rows']}
        self.first = trace['first_counter']
        self.inputs = sorted(trace['inputs'], key=lambda i: i['f'])
        self.counter = ee.load(0x70003B64)

    def pad_read(self, ee):
        ee.write(ee.r[6] & 0xFFFFFFFF, self.raw + bytes(24))
        ee.ret_int(1)

    def pad(self, f):
        current = {'buttons': 0, 'lx': 0x80, 'ly': 0x80}
        for entry in self.inputs:
            if entry['f'] <= f: current = entry
        return current

    def step(self):
        """Run the frame that produces row counter + 1; returns that row (or
        None before the trace starts)."""
        ee, n = self.ee, self.counter + 1
        pad = self.pad(n - self.first - PAD_LATENCY)
        held = pad['buttons']
        self.raw = bytes([0, 0x73, ~held & 0xFF, ~(held >> 8) & 0xFF, 0x80, 0x80, pad['lx'], pad['ly']])
        ee.save(0x70003B64, n - 1)
        ee.call(PAD_UNPACK, (0x810E70, 0x810E40, 1))
        previous = self.rows.get(n - 1)
        if previous is not None:
            for base, key in ((0x8105D0, 'eye'), (0x8105E0, 'tgt'), (0x810600, 'fwd')):
                for i in range(3): ee.putf(base + 4 * i, previous[key][i])
            ee.save(0x8106A0, ee.nested(0x11E620, (), (-previous['fwd'][2], previous['fwd'][0]))[1])
        ee.save(0x275B40, PLAYER + 0x110)
        ee.save(0x810750, ee.load(0x810750) + 1)
        ee.save(0x70003B68, ee.load(0x70003B68) + 1)
        ee.call(0x15BCF0, (PLAYER,))
        self.counter, self.frame = n, self.frame + 1
        return self.rows.get(n)


def route_row_check(ee, r, where, clock=True):
    """A replayed frame against its trace row: state +5, action +1F0 and clip
    +20C exact; feet +A0, body +B0, yaw +C4 and clip clock +3C within the
    trace's 5-decimal printing plus the replay's inputs (1e-3, 1e-3, 2e-5,
    1e-3). The walk steers by the reconstructed camera heading, so positions
    carry its rounding: 06_hill_slide measures at most 3.7e-4 (feet and
    body), 8.6e-6 (yaw) and 0 (clock)."""
    state = (ee.load(PLAYER + 5, 1), ee.load(PLAYER + 0x1F0, 1), ee.load(PLAYER + 0x20C, 2))
    assert state == (r['p5'], r['m1F0'], r['clip']), (where, 'state/action/clip', state, r)
    for base, key in ((0xA0, 'pos'), (0xB0, 'hip')):
        got = [number(ee.load(PLAYER + base + 4 * i)) for i in range(3)]
        assert all(abs(got[i] - r[key][i]) <= 1e-3 for i in range(3)), (where, key, got, r[key])
    assert abs(number(ee.load(PLAYER + 0xC4)) - r['yaw']) <= 2e-5, (where, 'yaw', r['yaw'])
    if clock:
        assert abs(number(ee.load(PLAYER + 0x3C)) - r['clock']) <= 1e-3, (where, 'clock', r['clock'])


def route_beat(beat):
    """(trace, source RAM, source scratchpad) of a route beat, or the reason
    it is unavailable."""
    import json
    trace_path = ROUTE / beat / 'trace.json'
    if not trace_path.exists():
        return 'missing %s' % trace_path
    trace = json.loads(trace_path.read_text())
    files = [ROUTE / trace['source'] / name for name in ('eeMemory.bin', 'scratchpad.bin')]
    missing = [str(f) for f in files if not f.exists()]
    if missing:
        return 'missing %s' % missing
    return trace, files[0].read_bytes(), files[1].read_bytes()


def slide_from_ee(ee, actor):
    for name, offset, size, kind in FIELDS:
        raw = ee.load(PLAYER + offset, size)
        if kind == 'f':
            setattr(actor, name, number(raw))
        elif kind == 's':
            setattr(actor, name, raw - (1 << (8 * size)) if raw >> (8 * size - 1) else raw)
        else:
            setattr(actor, name, raw)
    for i in range(3):
        actor.position[i] = number(ee.load(PLAYER + 0xB0 + 4 * i))
        actor.rotation[i] = number(ee.load(PLAYER + 0xC0 + 4 * i))


def slide_to_ee(ee, actor):
    for name, offset, size, kind in FIELDS:
        ee.save(PLAYER + offset, raw_of(getattr(actor, name), size, kind), size)
    for i in range(3):
        ee.save(PLAYER + 0xB0 + 4 * i, bits(actor.position[i]))
        ee.save(PLAYER + 0xC0 + 4 * i, bits(actor.rotation[i]))


class WorldWorkers:
    """em_player_slide.c workers bound to the ORIGINAL routines, executed in
    the same EE on the same world: 00175900, 001796C0, 00178B90, 00224B80,
    00224290, 0017C580, 0019AD00, 0019AFE0, the animation calls, the sound
    and effect entries (recorded), 00182430, 00182870 and the SDK math."""

    def __init__(self, ee, actor_ref):
        self.ee, self.actor = ee, actor_ref
        scratch = 0x7F0E0000
        self.vectors = (scratch, scratch + 0x10)
        e = ee
        sync = lambda: slide_to_ee(e, self.actor.contents)
        back = lambda: slide_from_ee(e, self.actor.contents)

        def around(entry, *args, floats=()):
            sync(); result = e.nested(entry, args, floats); back(); return result

        def vector(address, values):
            for i in range(4): e.save(address + 4 * i, bits(values[i]))
            return address

        def sweep(_, start, end, mask, out):
            r, _f = around(0x19AFE0, PLAYER, vector(self.vectors[0], start), vector(self.vectors[1], end), mask)
            kind = s32(r)
            out[0].kind = kind
            if kind:
                record = e.load(0x700031D0)
                out[0].node = e.load(record + 0x1A, 2)
                for i in range(3): out[0].delta[i] = number(e.load(0x700031C0 + 4 * i))
            return kind

        def move(_, position, target, mask):
            r, _f = around(0x19AD00, PLAYER, vector(self.vectors[0], target), mask)
            return 0 if s32(r) < 0 else s32(r)

        def floor(_, actor, search, out):
            r, _f = around(0x175900, PLAYER, search); out[0] = s32(r); return 0

        def call_actor(entry):
            return lambda _, actor: (around(entry, PLAYER), 0)[1]

        def result(entry):
            def fn(_, actor, out):
                r, _f = around(entry, PLAYER); out[0] = s32(r); return 0
            return fn

        def math1(entry):
            return lambda _, x: number(around(entry, floats=(x,))[1])

        def frames(_, clip, out):
            r, _f = around(0x1C61D0, e.load(PLAYER + 0x40), clip); out[0] = s32(r); return 0

        def sound(_, id, out):
            r, _f = around(0x1FBD50, PLAYER, id, 0, floats=(300.0,)); out[0] = s32(r); return 0

        self.workers = SlideWorkers(
            None,
            REQUEST_FN(lambda _, clip, force, blend: (around(0x1749A0, PLAYER, clip, force, floats=(blend,)), 0)[1]),
            ARBITER_FN(lambda _, clip, blend, frame: (around(0x1749F0, PLAYER, clip, floats=(blend, frame)), 0)[1]),
            FRAMES_FN(frames), SOUND_FN(sound),
            STOP_FN(lambda _, handle: (around(0x11A070, handle), 0)[1]),
            EFFECT_FN(lambda _, id, p, r: (around(0x1EFD90, id, PLAYER + 0xB0, PLAYER + 0xC0), 0)[1]),
            MOVE_FN(move), SWEEP_FN(sweep), FLOOR_FN(floor), ACTOR_FN(call_actor(0x1796C0)),
            TRANSLATE_FN(lambda _, actor, arg: (around(0x178B90, PLAYER, arg), 0)[1]),
            RESULT_FN(result(0x224B80)), RESULT_FN(result(0x224290)), ACTOR_FN(call_actor(0x17C580)),
            TIER_FN(lambda _, tier: (around(0x182430, PLAYER, tier), 0)[1]),
            TIER_FN(lambda _, tier: (around(0x182870, PLAYER, tier), 0)[1]),
            PLAIN_FN(lambda _: (around(0x21D250, PLAYER, 0), 0)[1]),
            PLAIN_FN(lambda _: (around(0x21D2E0, PLAYER, 0x78, 0), 0)[1]),
            MATH1_FN(math1(0x11E2A8)), MATH1_FN(math1(0x11DE90)),
            MATH2_FN(lambda _, y, x: number(around(0x11E620, floats=(y, x))[1])))


def native_slide_hook(native_lib, counter):
    def hook(ee):
        actor = SlideActor()
        slide_from_ee(ee, actor)
        scene = SlideScene()
        node0 = ee.load(ee.load(0x275B40))
        node1 = ee.load(ee.load(0x275B40) + 4)
        scene.root_forward = number(ee.load(node0 + 8))
        scene.hip[0] = number(ee.load(node1 + 0xC0)); scene.hip[1] = number(ee.load(node1 + 0xC8))
        scene.scripted = ee.load(0x70003B8D, 1)
        scene.pad_gait = ee.load(0x810E57, 1)
        scene.pad_x = ee.load(0x810E64, 1); scene.pad_y = ee.load(0x810E65, 1)
        workers = WorldWorkers(ee, C.pointer(actor))
        assert native_lib.em_player_slide_tick(C.byref(actor), C.byref(scene), C.byref(workers.workers)) == 0
        slide_to_ee(ee, actor)
        counter[0] += 1
    return hook


SLIDE_BEAT = '06_hill_slide'


CAPTURE = {}   # elf, native, trace, ram, spad: set before the forked replays


def slide_replay(native):
    """One RouteReplay of the capture beat, with the native 0016C6A0 hooked
    in when `native`; every trace row is checked (route_row_check), so the
    original replay proves the harness and the native one the translation."""
    c = CAPTURE
    replay = RouteReplay(c['elf'], c['trace'], c['ram'], c['spad'])
    calls = [0]
    if native:
        replay.ee.hooks[SLIDE] = native_slide_hook(c['native'], calls)
    frames, rows, slid = [], 0, 0
    while replay.counter < c['trace']['last_counter']:
        r = replay.step()
        frames.append((replay.counter, replay.actor(), len(replay.events)))
        if replay.ee.load(PLAYER + 5, 1) == 0x1C: slid += 1
        if r is not None:
            route_row_check(replay.ee, r, (SLIDE_BEAT, 'native' if native else 'original', replay.counter))
            rows += 1
    position = [round(number(replay.ee.load(PLAYER + 0xA0 + 4 * i)), 3) for i in range(3)]
    return frames, replay.events, rows, slid, calls[0], position


def world_capture(elf, native):
    """The hill slide against the real PCSX2 route capture 06_hill_slide: the
    original stage and the stage with the native 0016C6A0 replay the whole
    beat (walk off the ledge, slide, skid out, idle) from the 05_boxes
    snapshot with the recorded pad input (RouteReplay), in two forked
    workers. Both must match every trace row (route_row_check), and every
    frame their actors (0x320 bytes) and sound/effect calls must be
    identical."""
    beat = route_beat(SLIDE_BEAT)
    if isinstance(beat, str):
        raise SystemExit('capture route: %s' % beat)
    CAPTURE.update(elf=elf, native=native, trace=beat[0], ram=beat[1], spad=beat[2])
    (a_frames, a_events, rows, slid, _, position), (b_frames, b_events, _, b_slid, calls, _) = \
        reference_mode.parallel_map(slide_replay, (False, True))
    for (counter, a, a_count), (_, b, b_count) in zip(a_frames, b_frames):
        if a != b:
            diff = [hex(k) for k in range(0x320) if a[k] != b[k]]
            raise AssertionError((SLIDE_BEAT, counter, 'actor bytes differ at', diff[:24]))
        assert a_count == b_count, (SLIDE_BEAT, counter, 'sound/effect call counts differ')
    assert a_events == b_events, (SLIDE_BEAT, 'sound/effect calls differ')
    assert len(a_frames) == len(b_frames) and rows == len(beat[0]['rows']), (len(a_frames), len(b_frames), rows)
    assert slid > 0 and b_slid == slid and calls == slid, (slid, b_slid, calls)
    return rows, calls, len(a_events), position


def world_main():
    elf = read_elf()
    missing = world_inputs()
    if missing:
        raise SystemExit('world mode: %s (docs/PLAYER_CLIMB_SLIDE.md)' % missing)
    native = build_native()
    routes = os.environ.get('EM_WORLD_ROUTES', 'slide,capture').split(',')
    if 'capture' in routes:
        rows, calls, events, position = world_capture(elf, native)
        print('player slide vs PCSX2 capture %s: PASS %d rows (native = original bytes; state, +1F0, clip exact; '
              'feet, body, yaw and clock within the trace precision), %d native state-0x1C callbacks, '
              '%d identical sound/effect calls; end %s' % (SLIDE_BEAT, rows, calls, events, position))
    if 'slide' not in routes:
        return
    ram, spad = WORLD_RAM.read_bytes(), WORLD_SPAD.read_bytes()
    start, yaw = (238.0, 220.5, 308.0), 0.0
    original = Stage(elf, ram, spad, start, yaw)
    translated = Stage(elf, ram, spad, start, yaw)
    calls = [0]
    translated.ee.hooks[SLIDE] = native_slide_hook(native, calls)
    frames = int(os.environ.get('EM_WORLD_FRAMES', '175'))
    slid = 0
    for i in range(frames):
        for stage in (original, translated):
            if i < 3: stage.step()
            else: stage.step(gait=3, ly=0, camera=-math.pi / 2 if i < 40 else None)
        a, b = original.actor(), translated.actor()
        if a != b:
            diff = [hex(k) for k in range(0x320) if a[k] != b[k]]
            raise AssertionError(('frame', i, 'actor bytes differ at', diff[:24]))
        assert original.events == translated.events, ('frame', i, 'sound/effect calls differ')
        if original.ee.load(PLAYER + 5, 1) == 0x1C: slid += 1
    assert slid > 0 and calls[0] == slid, (slid, calls[0])
    position = [round(number(original.ee.load(PLAYER + 0xA0 + 4 * i)), 3) for i in range(3)]
    print('player slide world: PASS %d frames, %d native state-0x1C callbacks, identical actor '
          'bytes and %d sound/effect calls; end %s' % (frames, calls[0], len(original.events), position))


if __name__ == '__main__':
    if os.environ.get('EM_TEST_WORLD', '') not in ('', '0'):
        world_main()
    else:
        main()
