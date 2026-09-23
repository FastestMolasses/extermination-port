#!/usr/bin/env python3
"""Compare the native effect chain (em_effect_original.c) with the original code.

The oracle below is our own bounded EE interpreter. It executes the ORIGINAL
instructions of the pinned boot ELF (the user's config/SCUS_971.12):
001EFD90, 001EFD20, 001EF9D0, 001EF940, 001D80E0, 001D8100, 001F0460,
001EA240, 001CCF70, 001CD370, 001CD390 and every SDK leaf they reach
(001029C0, 00102918, 001026A0, 00102760, 00102718, 001031E0, 00102948,
copy_qw4, 001029E8, 00102A60, 00102BB0, 00102B08, 00102C58, 001B1470,
0011E860, float_to_int 001281C0, 001278C0). COP1 and VU0 arithmetic come
from tools/ee_float_model.py (docs/EE_FLOAT_MODEL.md), which reproduces all
recorded PCSX2 results; VCLIPw is outside that model and runs on finite
operands only. Memory is a captured route snapshot
(../Extermination/build/s87/route/<beat>/eeMemory.bin + scratchpad.bin).

Workers (recorded as calls, scripted results): 001AFA90 alloc, 00122BB8
rand, 001D7FA0 point light, 001FBF50 / 001FB9F0 sound, 0021B9A0 rumble,
the D_00255434 handlers, 001AFC10 free. Every byte the original changes
must be a byte the native module models (or stack), and every modelled byte,
worker call and return value is compared.

Capture evidence: the live 001EA240 nodes in the route snapshots (beat 05
and 12: subtype 5 footsteps; beat 08: eight subtype-0x20 truck puffs) are
(1) run in lockstep original vs native from their captured state until they
free, and (2) checked against the native state-0 recomputation: their
captured +0xD0 matrix and accumulator must be what the translation computes.

No original instruction bytes, disassembly or data are written by this file;
build/effect_original_reference/report.json holds only counts.
"""
import ctypes as C
import hashlib
import json
import random
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ee_float_model as FM  # noqa: E402
from reference_mode import banner, part, pick, select  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
ELF_SHA = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
ROUTE = DECOMP / 'build/s87/route'
M32, M64, M128 = (1 << 32) - 1, (1 << 64) - 1, (1 << 128) - 1
ONE = 0x3F800000
RET = 0x0BADF00C
STACK = 0x01F80000
TABLE, TABLE_END = 0x257C90, 0x259C70
RING, RING_INDEX = 0x28F700 + 0x4DBEC0, 0x81F950
SCRATCH = 0x01E00000          # oracle-only vectors and synthetic nodes
POOL_HEAD = 0x275BC0

W_ALLOC, W_RAND, W_LIGHT, W_SFXPOS, W_SFX, W_RUMBLE, W_FREE = (
    0x1AFA90, 0x122BB8, 0x1D7FA0, 0x1FBF50, 0x1FB9F0, 0x21B9A0, 0x1AFC10)


def sx(v, bits=32):
    v &= (1 << bits) - 1
    return v - (1 << bits) if v >> (bits - 1) else v


def fbits(x): return struct.unpack('<I', struct.pack('<f', x))[0]


# --------------------------------------------------------------- oracle ---

class Unsupported(AssertionError):
    pass


class EE:
    """Bounded EE + VU0-macro interpreter over a captured RAM image."""

    def __init__(self, elf, ram, spad):
        self.elf, self.ram, self.spad = elf, ram, spad
        self.journal = []
        self.r = [0] * 32
        self.f = [0] * 32
        self.cond = 0
        self.vf = [[0, 0, 0, 0] for _ in range(32)]
        self.vf[0] = [0, 0, 0, ONE]
        self.acc = [0, 0, 0, 0]
        self.q = 0
        self.clip = 0
        self.stubs = {}
        self.steps = 0

    # memory
    def where(self, a):
        a &= M32
        if 0x70000000 <= a < 0x70004000:
            return self.spad, a - 0x70000000
        if a < 0x02000000:
            return self.ram, a
        raise Unsupported(('address', hex(a)))

    def load(self, a, n=4):
        buf, o = self.where(a)
        return int.from_bytes(buf[o:o + n], 'little')

    def store(self, a, v, n=4):
        buf, o = self.where(a)
        self.journal.append((buf, o, bytes(buf[o:o + n])))
        buf[o:o + n] = (v & ((1 << (8 * n)) - 1)).to_bytes(n, 'little')

    def write(self, a, data):
        buf, o = self.where(a)
        self.journal.append((buf, o, bytes(buf[o:o + len(data)])))
        buf[o:o + len(data)] = data

    def restore(self):
        while self.journal:
            buf, o, old = self.journal.pop()
            buf[o:o + len(old)] = old

    def fetch(self, pc):
        if 0x100000 <= pc < 0x275B00:
            o = pc - 0x100000 + 0x300
            return int.from_bytes(self.elf[o:o + 4], 'little')
        raise Unsupported(('fetch', hex(pc)))

    # registers
    def g32(self, i): return sx(self.r[i], 32)
    def u32(self, i): return self.r[i] & M32

    def set32(self, i, v):
        if i:
            self.r[i] = (self.r[i] & ~M64 & M128) | (sx(v, 32) & M64)

    def set64(self, i, v):
        if i:
            self.r[i] = (self.r[i] & ~M64 & M128) | (v & M64)

    def set128(self, i, v):
        if i:
            self.r[i] = v & M128

    # COP2 macro
    def vlane(self, name, dest, bc, fs, ft, lanes, t_of, acc_of=None, out='vf', fd=0):
        res = list(self.acc if out == 'acc' else self.vf[fd])
        for k in range(4):
            if (dest >> (3 - k)) & 1:
                s = fs[lanes[k]] if lanes else fs[k]
                res[k] = FM.vu_lane(name, dest, bc, s, t_of(k), acc_of(k) if acc_of else None)
        if out == 'acc':
            self.acc = res
        elif fd:
            self.vf[fd] = res

    def cop2(self, w, pc):
        if not (w >> 25) & 1:
            rs, rt, rd = w >> 21 & 31, w >> 16 & 31, w >> 11 & 31
            if rs == 5:            # qmtc2
                v = self.r[rt]
                if rd:
                    self.vf[rd] = [v >> (32 * i) & M32 for i in range(4)]
            elif rs == 1:          # qmfc2
                self.set128(rt, sum(x << (32 * i) for i, x in enumerate(self.vf[rd])))
            elif rs == 2:          # cfc2
                if rd != 18:
                    raise Unsupported(('cfc2', rd))
                self.set32(rt, self.clip)
            else:
                raise Unsupported(('cop2', hex(pc), rs))
            return
        dest, ft, fs, fd, fn = w >> 21 & 15, w >> 16 & 31, w >> 11 & 31, w >> 6 & 31, w & 63
        S, T = list(self.vf[fs]), list(self.vf[ft])
        if fn < 0x3C:
            bc = fn & 3
            if fn < 4:
                self.vlane('vaddbc', dest, bc, S, T, None, lambda k: T[bc], fd=fd)
            elif fn < 8:
                self.vlane('vsubbc', dest, bc, S, T, None, lambda k: T[bc], fd=fd)
            elif fn < 0xC:
                self.vlane('vmaddbc', dest, bc, S, T, None, lambda k: T[bc], lambda k: self.acc[k], fd=fd)
            elif 0x10 <= fn < 0x18:
                op = FM.vu_max if fn < 0x14 else FM.vu_min
                res = list(self.vf[fd])
                for k in range(4):
                    if (dest >> (3 - k)) & 1:
                        res[k] = op(S[k], T[bc])
                if fd:
                    self.vf[fd] = res
            elif fn < 0x1C:
                self.vlane('vmulbc', dest, bc, S, T, None, lambda k: T[bc], fd=fd)
            elif fn == 0x1C:
                self.vlane('vmulq', dest, None, S, T, None, lambda k: self.q, fd=fd)
            elif fn == 0x20:
                self.vlane('vaddq', dest, None, S, T, None, lambda k: self.q, fd=fd)
            elif fn == 0x28:
                self.vlane('vadd', dest, None, S, T, None, lambda k: T[k], fd=fd)
            elif fn == 0x2A:
                self.vlane('vmul', dest, None, S, T, None, lambda k: T[k], fd=fd)
            elif fn == 0x2C:
                self.vlane('vsub', dest, None, S, T, None, lambda k: T[k], fd=fd)
            elif fn == 0x2E:       # vopmsub: fs.yzx * ft.zxy
                acc = list(self.acc)
                self.vlane('vopmsub', dest, None, S, T, (1, 2, 0, 3), lambda k: T[(2, 0, 1, 3)[k]],
                           lambda k: acc[k], fd=fd)
            else:
                raise Unsupported(('vu upper', hex(pc), hex(fn)))
            return
        idx = fd << 2 | (fn & 3)
        bc = fn & 3
        fsf, ftf = w >> 21 & 3, w >> 23 & 3
        if 0x08 <= idx < 0x0C:
            acc = list(self.acc)
            self.vlane('vmaddabc', dest, bc, S, T, None, lambda k: T[bc], lambda k: acc[k], out='acc')
        elif 0x18 <= idx < 0x1C:
            self.vlane('vmulabc', dest, bc, S, T, None, lambda k: T[bc], out='acc')
        elif idx == 0x2E:
            self.vlane('vopmula', dest, None, S, T, (1, 2, 0, 3), lambda k: T[(2, 0, 1, 3)[k]], out='acc')
        elif idx == 0x15:          # vftoi4
            res = list(self.vf[ft])
            for k in range(4):
                if (dest >> (3 - k)) & 1:
                    res[k] = FM.vu_ftoi(S[k], 4)
            if ft:
                self.vf[ft] = res
        elif idx == 0x1F:          # vclipw.xyz fs, ft.w (finite operands only)
            vals = S[:3] + [T[3]]
            if any((v >> 23 & 0xFF) == 0xFF for v in vals):
                raise Unsupported(('vclipw non-finite', hex(pc)))
            w_ = abs(FM.b2f(FM._daz(T[3])))
            flags = 0
            for k in range(3):
                c = FM.b2f(FM._daz(S[k]))
                flags |= (c > w_) << (2 * k) | (c < -w_) << (2 * k + 1)
            self.clip = (self.clip << 6 | flags) & 0xFFFFFF
        elif idx == 0x2F or idx == 0x3B:
            pass                   # vnop, vwaitq
        elif idx == 0x30:          # vmove
            res = list(self.vf[ft])
            for k in range(4):
                if (dest >> (3 - k)) & 1:
                    res[k] = S[k]
            if ft:
                self.vf[ft] = res
        elif idx == 0x31:          # vmr32
            rot = S[1:] + S[:1]
            res = list(self.vf[ft])
            for k in range(4):
                if (dest >> (3 - k)) & 1:
                    res[k] = rot[k]
            if ft:
                self.vf[ft] = res
        elif idx == 0x38:
            self.q = FM.vu_div(S[fsf], T[ftf], fsf, ftf)
        elif idx == 0x39:
            self.q = FM.vu_sqrt(T[ftf])
        else:
            raise Unsupported(('vu lower', hex(pc), hex(idx)))

    def cop1(self, w, pc):
        rs, ft, fs, fd, fn = w >> 21 & 31, w >> 16 & 31, w >> 11 & 31, w >> 6 & 31, w & 63
        f = self.f
        if rs == 0:
            self.set32(ft, f[fs])
        elif rs == 4:
            f[fs] = self.u32(ft)
        elif rs == 16:
            a, b = f[fs], f[ft]
            if fn == 0: f[fd] = FM.ee_add(a, b)
            elif fn == 1: f[fd] = FM.ee_sub(a, b)
            elif fn == 2: f[fd] = FM.ee_mul(a, b)
            elif fn == 3: f[fd] = FM.ee_div(a, b)
            elif fn == 6: f[fd] = a
            elif fn == 7: f[fd] = FM.ee_neg(a)
            elif fn == 0x24: f[fd] = FM.ee_cvt_w_s(a)
            elif fn == 0x32: self.cond = FM.ee_c_eq(a, b)
            elif fn == 0x34: self.cond = FM.ee_c_lt(a, b)
            elif fn == 0x36: self.cond = FM.ee_c_le(a, b)
            else: raise Unsupported(('fpu', hex(pc), fn))
        elif rs == 20 and fn == 0x20:
            f[fd] = FM.ee_cvt_s_w(f[fs])
        else:
            raise Unsupported(('cop1', hex(pc), rs, fn))

    def execute(self, w, pc):
        op, rs, rt, rd, sa, fn = w >> 26, w >> 21 & 31, w >> 16 & 31, w >> 11 & 31, w >> 6 & 31, w & 63
        imm = sx(w & 0xFFFF, 16)
        r = self.r
        if op == 0:
            if fn == 0: self.set32(rd, self.u32(rt) << sa)
            elif fn == 2: self.set32(rd, self.u32(rt) >> sa)
            elif fn == 3: self.set32(rd, self.g32(rt) >> sa)
            elif fn == 6: self.set32(rd, self.u32(rt) >> (self.u32(rs) & 31))
            elif fn == 4: self.set32(rd, self.u32(rt) << (self.u32(rs) & 31))
            elif fn == 0x0A:
                if not r[rt] & M64: self.set64(rd, r[rs])
            elif fn == 0x0B:
                if r[rt] & M64: self.set64(rd, r[rs])
            elif fn in (0x20, 0x21): self.set32(rd, self.u32(rs) + self.u32(rt))
            elif fn in (0x22, 0x23): self.set32(rd, self.u32(rs) - self.u32(rt))
            elif fn == 0x24: self.set64(rd, r[rs] & r[rt])
            elif fn == 0x25: self.set64(rd, r[rs] | r[rt])
            elif fn == 0x26: self.set64(rd, r[rs] ^ r[rt])
            elif fn == 0x2A: self.set64(rd, int(sx(r[rs], 64) < sx(r[rt], 64)))
            elif fn == 0x2B: self.set64(rd, int((r[rs] & M64) < (r[rt] & M64)))
            elif fn == 0x2D: self.set64(rd, r[rs] + r[rt])
            elif fn == 0x2F: self.set64(rd, r[rs] - r[rt])
            elif fn == 0x3C: self.set64(rd, (r[rt] & M64) << (sa + 32))
            elif fn == 0x3E: self.set64(rd, (r[rt] & M64) >> (sa + 32))
            elif fn == 0x3F: self.set64(rd, sx(r[rt], 64) >> (sa + 32))
            elif fn == 0x38: self.set64(rd, (r[rt] & M64) << sa)
            elif fn == 0x3A: self.set64(rd, (r[rt] & M64) >> sa)
            else: raise Unsupported(('special', hex(pc), hex(fn)))
        elif op in (8, 9): self.set32(rt, self.u32(rs) + imm)
        elif op == 0x19: self.set64(rt, r[rs] + imm)
        elif op == 0x0A: self.set64(rt, int(sx(r[rs], 64) < imm))
        elif op == 0x0B: self.set64(rt, int((r[rs] & M64) < (imm & M64)))
        elif op == 0x0C: self.set64(rt, r[rs] & (w & 0xFFFF))
        elif op == 0x0D: self.set64(rt, (r[rs] & M64) | (w & 0xFFFF))
        elif op == 0x0E: self.set64(rt, (r[rs] & M64) ^ (w & 0xFFFF))
        elif op == 0x0F: self.set32(rt, (w & 0xFFFF) << 16)
        elif op == 0x1C:
            if fn == 0x28 and sa == 0x18:   # paddub
                a, b = r[rs], r[rt]
                v = sum((((a >> (8 * i)) + (b >> (8 * i))) & 0xFF) << (8 * i) for i in range(16))
                self.set128(rd, v)
            else:
                raise Unsupported(('mmi', hex(pc), hex(fn), sa))
        elif op in (0x20, 0x21, 0x23, 0x24, 0x25, 0x27, 0x37, 0x1E):
            a = (self.u32(rs) + imm) & M32
            n = {0x20: 1, 0x24: 1, 0x21: 2, 0x25: 2, 0x23: 4, 0x27: 4, 0x37: 8, 0x1E: 16}[op]
            if op == 0x1E:
                a &= ~15
            v = self.load(a, n)
            if op == 0x20: self.set64(rt, sx(v, 8))
            elif op == 0x21: self.set64(rt, sx(v, 16))
            elif op == 0x23: self.set32(rt, v)
            elif op == 0x37: self.set64(rt, v)
            elif op == 0x1E: self.set128(rt, v)
            else: self.set64(rt, v)
        elif op in (0x28, 0x29, 0x2B, 0x3F, 0x1F):
            a = (self.u32(rs) + imm) & M32
            n = {0x28: 1, 0x29: 2, 0x2B: 4, 0x3F: 8, 0x1F: 16}[op]
            if op == 0x1F:
                a &= ~15
            self.store(a, r[rt], n)
        elif op == 0x31: self.f[rt] = self.load((self.u32(rs) + imm) & M32)
        elif op == 0x39: self.store((self.u32(rs) + imm) & M32, self.f[rt])
        elif op == 0x36:
            a = (self.u32(rs) + imm) & M32 & ~15
            if rt:
                self.vf[rt] = [self.load(a + 4 * i) for i in range(4)]
        elif op == 0x3E:
            a = (self.u32(rs) + imm) & M32 & ~15
            for i in range(4):
                self.store(a + 4 * i, self.vf[rt][i])
        elif op == 0x11: self.cop1(w, pc)
        elif op == 0x12: self.cop2(w, pc)
        else:
            raise Unsupported(('opcode', hex(pc), hex(op)))
        r[0] = 0

    def call(self, entry, args=(), floats=()):
        self.r = [0] * 32
        self.r[28], self.r[29], self.r[31] = 0x27D370, STACK, RET
        for i, v in enumerate(args):
            self.set32(4 + i, v)
        for i, v in enumerate(floats):
            self.f[12 + i] = v
        pc = entry
        while pc != RET:
            self.steps += 1
            if self.steps > 5_000_000:
                raise AssertionError('runaway')
            w = self.fetch(pc)
            op, rs, rt = w >> 26, w >> 21 & 31, w >> 16 & 31
            target = None
            likely = False
            if op in (2, 3):
                target = (pc & 0xF0000000) | (w & 0x3FFFFFF) << 2
                if op == 3:
                    self.set64(31, pc + 8)
            elif op == 0 and (w & 63) in (8, 9):
                target = self.u32(rs)
                if w & 63 == 9:
                    self.set64(w >> 11 & 31, pc + 8)
            elif op in (4, 5, 0x14, 0x15):
                taken = ((self.r[rs] & M64) == (self.r[rt] & M64)) == (op in (4, 0x14))
                likely = op >= 0x14
                target = pc + 4 + (sx(w & 0xFFFF, 16) << 2) if taken else None
            elif op in (6, 7):
                v = sx(self.r[rs], 64)
                taken = v <= 0 if op == 6 else v > 0
                target = pc + 4 + (sx(w & 0xFFFF, 16) << 2) if taken else None
            elif op == 1:
                v = sx(self.r[rs], 64)
                taken = v < 0 if rt in (0, 2) else v >= 0
                likely = rt in (2, 3)
                if rt > 3:
                    raise Unsupported(('regimm', hex(pc), rt))
                target = pc + 4 + (sx(w & 0xFFFF, 16) << 2) if taken else None
            elif op == 0x11 and rs == 8:
                taken = self.cond == (rt & 1)
                likely = bool(rt & 2)
                target = pc + 4 + (sx(w & 0xFFFF, 16) << 2) if taken else None
            else:
                self.execute(w, pc)
                pc += 4
                continue
            # control transfer with delay slot
            if target is None:
                if likely:
                    pc += 8
                else:
                    self.execute(self.fetch(pc + 4), pc + 4)
                    pc += 8
                continue
            self.execute(self.fetch(pc + 4), pc + 4)
            if target in self.stubs:
                self.stubs[target](self)
                links = op == 3 or (op == 0 and w & 63 == 9)
                pc = pc + 8 if links else self.r[31] & M32
                continue
            pc = target


# ------------------------------------------------------------ native side ---

class Work(C.Structure):
    _fields_ = [('seed', C.c_int32), ('seed_copy', C.c_int32), ('step', C.c_uint32),
                ('limit', C.c_uint32), ('accumulator', C.c_uint32), ('fraction', C.c_uint32)]


class Node(C.Structure):
    _fields_ = [('b03', C.c_uint8), ('state', C.c_uint8), ('b09', C.c_uint8), ('b0C', C.c_uint8),
                ('subtype', C.c_uint8), ('callback', C.c_uint32), ('live38', C.c_int32),
                ('pos', C.c_uint32 * 4), ('rot', C.c_uint32 * 4), ('matrix', C.c_uint32 * 16),
                ('work', Work), ('freed', C.c_uint8)]


class Tables(C.Structure):
    _fields_ = [('bytes', C.c_uint8 * (TABLE_END - TABLE)), ('glob', C.c_uint32),
                ('area', C.c_uint32 * 23), ('step', C.c_uint32 * 43), ('handler', C.c_uint32 * 43)]


class Globals(C.Structure):
    _fields_ = [('d8101E4', C.c_uint8), ('d810700', C.c_uint8), ('d275C38', C.c_int32),
                ('spad3B68', C.c_int32), ('d8102E8', C.c_uint32), ('d275C30', C.POINTER(Node)),
                ('d275C34', C.POINTER(Work)), ('d275C04', C.c_int32),
                ('spad3600', C.c_uint32 * 16), ('spad38A0', C.c_uint32 * 4)]


class View(C.Structure):
    _fields_ = [('clip', C.c_uint32 * 16), ('fog', C.c_uint32 * 4), ('camera', C.c_uint32 * 16)]


class Slot(C.Structure):
    _fields_ = [('source', C.c_uint32 * 16), ('params', C.c_uint32 * 4), ('tag', C.c_uint64),
                ('life', C.c_int32), ('w5C', C.c_uint32)]


class Decals(C.Structure):
    _fields_ = [('index', C.c_int32 * 7), ('slot', (Slot * 32) * 7)]


FP = C.POINTER(C.c_uint32)
ALLOC = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint8, C.POINTER(C.POINTER(Node)))
RAND = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_int32))
LIGHT = C.CFUNCTYPE(C.c_int, C.c_void_p, FP, FP, C.c_int32, C.c_float, C.c_float)
SFXPOS = C.CFUNCTYPE(C.c_int, C.c_void_p, FP, C.c_float, C.c_float, C.POINTER(C.c_int32),
                     C.POINTER(C.c_int32), C.POINTER(C.c_int32))
SFX = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int32, C.c_int32, C.c_int32, C.c_int32)
RUMBLE = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int32, C.c_float, C.c_float)
HANDLER = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.POINTER(Node), C.c_int32, C.POINTER(Work))
FREE = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(Node))


class Workers(C.Structure):
    _fields_ = [('ctx', C.c_void_p), ('alloc', ALLOC), ('rand', RAND), ('light', LIGHT),
                ('sfxpos', SFXPOS), ('sfx', SFX), ('rumble', RUMBLE), ('handler', HANDLER),
                ('free', FREE)]


class Fault(C.Structure):
    _fields_ = [('address', C.c_uint32), ('code', C.c_int32)]


class Effect(C.Structure):
    _fields_ = [('tables', C.POINTER(Tables)), ('globals', C.POINTER(Globals)),
                ('decals', C.POINTER(Decals)), ('view', C.POINTER(View)),
                ('workers', C.POINTER(Workers)), ('fault', Fault)]


def build_lib():
    out = ROOT / 'build/effect_original_reference'
    out.mkdir(parents=True, exist_ok=True)
    lib_path = out / 'effect.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-shared', '-fPIC', '-Isrc', 'src/game/em_effect_original.c',
                    'src/game/em_point_light.c', '-o', str(lib_path)], cwd=ROOT, check=True)
    lib = C.CDLL(str(lib_path))
    P = C.POINTER
    lib.em_effect_original_load_tables.argtypes = [C.c_char_p, C.c_size_t, P(Tables)]
    lib.em_effect_original_001EFD90.argtypes = [P(Effect), C.c_uint32, FP, FP, P(P(Node))]
    lib.em_effect_original_001EFD20.argtypes = [P(Effect), C.c_uint32, FP, P(P(Node))]
    lib.em_effect_original_001EF9D0.argtypes = [P(Effect), C.c_uint32, FP, C.c_float, P(P(Node))]
    lib.em_effect_original_001F0460.argtypes = [P(Effect), C.c_int32, FP]
    lib.em_effect_original_001EA240.argtypes = [P(Effect), P(Node)]
    lib.em_effect_original_001CCF70.argtypes = [P(Effect), FP, P(C.c_int32)]
    lib.em_effect_original_001CD390.argtypes = [P(Effect), FP, FP]
    lib.em_effect_original_fp.argtypes = [C.c_int, C.c_uint, C.c_uint32, C.c_uint32, C.c_uint32]
    lib.em_effect_original_fp.restype = C.c_uint32
    return lib


# --------------------------------------------------------------- the world ---

NODE_FIELDS = [('b03', 3, 1), ('state', 4, 1), ('b09', 9, 1), ('b0C', 0xC, 1), ('subtype', 0xD, 1),
               ('callback', 0x10, 4), ('live38', 0x38, 4)]
NODE_ARRAYS = [('pos', 0xB0, 4), ('rot', 0xC0, 4), ('matrix', 0xD0, 16)]
WORK_FIELDS = [('seed', 0x1F0), ('seed_copy', 0x1F4), ('step', 0x1F8), ('limit', 0x1FC),
               ('accumulator', 0x244), ('fraction', 0x24C)]


def node_bytes_allowed(a):
    out = set()
    for _, off, n in NODE_FIELDS:
        out.update(range(a + off, a + off + n))
    for _, off, n in NODE_ARRAYS:
        out.update(range(a + off, a + off + 4 * n))
    for _, off in WORK_FIELDS:
        out.update(range(a + off, a + off + 4))
    return out


class World:
    """One original memory image and its native twin, with worker scripts."""

    def __init__(self, lib, elf, ram, spad, rands=(), allocs=(), sfxpos=()):
        self.lib, self.elf = lib, elf
        self.ee = EE(elf, ram, spad)
        self.rands = list(rands)
        self.allocs = list(allocs)
        self.sfxpos_script = list(sfxpos)
        self.o_log, self.n_log = [], []
        self.o_rand, self.n_rand = 0, 0
        self.o_alloc, self.n_alloc = 0, 0
        self.o_sfx, self.n_sfx = 0, 0
        self.nodes = {}                 # original address -> native Node
        self.addr_of = {}               # id(native node address) -> original address
        self.allowed = set()
        self.n_handlers = []
        # native state mirrors the RAM
        self.tables = Tables()
        blob = C.create_string_buffer(bytes(elf), len(elf))
        assert lib.em_effect_original_load_tables(blob, len(elf), C.byref(self.tables)) == 0
        self.glob = Globals()
        self.view = View()
        self.decals = Decals()
        self.pull_globals()
        self.pull_view()
        self.pull_decals()
        self.workers = Workers(None, ALLOC(self.n_alloc_fn), RAND(self.n_rand_fn), LIGHT(self.n_light_fn),
                               SFXPOS(self.n_sfxpos_fn), SFX(self.n_sfx_fn), RUMBLE(self.n_rumble_fn),
                               HANDLER(self.n_handler_fn), FREE(self.n_free_fn))
        self.effect = Effect(C.pointer(self.tables), C.pointer(self.glob), C.pointer(self.decals),
                             C.pointer(self.view), C.pointer(self.workers), Fault())
        e = self.ee
        e.stubs.update({W_ALLOC: self.o_alloc_fn, W_RAND: self.o_rand_fn, W_LIGHT: self.o_light_fn,
                        W_SFXPOS: self.o_sfxpos_fn, W_SFX: self.o_sfx_fn, W_RUMBLE: self.o_rumble_fn,
                        W_FREE: self.o_free_fn})
        for h in set(self.tables.handler):
            e.stubs[h] = (lambda ee, h=h: self.o_handler_fn(ee, h))
        self.allowed.update(range(TABLE, TABLE_END))
        for a in (0x275C38, 0x275C30, 0x275C34, 0x275C04):
            self.allowed.update(range(a, a + 4))
        self.allowed.update(range(0x70003600, 0x70003640))
        self.allowed.update(range(0x700038A0, 0x700038B0))
        self.allowed.update(range(RING_INDEX, RING_INDEX + 28))
        self.allowed.update(range(RING, RING + 7 * 0xC00))

    # ---- RAM <-> native
    def u32(self, a): return self.ee.load(a, 4)

    def pull_globals(self):
        e, g = self.ee, self.glob
        g.d8101E4 = e.load(0x8101E4, 1)
        g.d810700 = e.load(0x810700, 1)
        g.d275C38 = sx(e.load(0x275C38))
        g.spad3B68 = sx(e.load(0x70003B68))
        g.d8102E8 = e.load(0x8102E8)
        g.d275C04 = sx(e.load(0x275C04))
        for i in range(16):
            g.spad3600[i] = e.load(0x70003600 + 4 * i)
        for i in range(4):
            g.spad38A0[i] = e.load(0x700038A0 + 4 * i)

    def pull_view(self):
        ctx = self.u32(0x275670)
        for i in range(16):
            self.view.clip[i] = self.u32(ctx + 0x2240 + 4 * i)
            self.view.camera[i] = self.u32(0x70003AC0 + 4 * i)
        for i in range(4):
            self.view.fog[i] = self.u32(ctx + 0xA0 + 4 * i)

    def pull_decals(self):
        for n in range(7):
            self.decals.index[n] = sx(self.u32(RING_INDEX + 4 * n))
        blob = self.ee.ram[RING:RING + 7 * 0xC00]
        C.memmove(C.addressof(self.decals.slot), bytes(blob), len(blob))

    def node_at(self, a):
        """The native node for original address a (created from RAM)."""
        if a in self.nodes:
            return self.nodes[a]
        n = Node()
        e = self.ee
        for name, off, size in NODE_FIELDS:
            v = e.load(a + off, size)
            setattr(n, name, sx(v) if name == 'live38' else v)
        for name, off, cnt in NODE_ARRAYS:
            arr = getattr(n, name)
            for i in range(cnt):
                arr[i] = e.load(a + off + 4 * i)
        for name, off in WORK_FIELDS:
            v = e.load(a + off)
            setattr(n.work, name, sx(v) if name in ('seed', 'seed_copy') else v)
        self.nodes[a] = n
        self.addr_of[C.addressof(n)] = a
        self.allowed |= node_bytes_allowed(a)
        return n

    def addr(self, ptr):
        if not ptr:
            return 0
        return self.addr_of[C.addressof(ptr.contents)]

    # ---- original workers
    def o_alloc_fn(self, e):
        cls = e.u32(4) & 0xFF
        a = self.allocs[self.o_alloc] if self.o_alloc < len(self.allocs) else 0
        self.o_alloc += 1
        self.o_log.append(('alloc', cls, a))
        if a:
            self.node_at(a)
        e.set32(2, a)

    def o_rand_fn(self, e):
        v = self.rands[self.o_rand]
        self.o_rand += 1
        self.o_log.append(('rand', v))
        e.set32(2, v)

    def o_light_fn(self, e):
        pos = tuple(e.load(e.u32(4) + 4 * i) for i in range(4))
        col = tuple(e.load(e.u32(5) + 4 * i) for i in range(4))
        self.o_log.append(('light', pos, col, e.g32(6), e.f[12], e.f[13]))
        e.set32(2, 0)

    def o_sfxpos_fn(self, e):
        a0 = e.u32(4)
        pos = tuple(e.load(a0 + 0xB0 + 4 * i) for i in range(4))
        a, b, v0 = self.sfxpos_script[self.o_sfx] if self.o_sfx < len(self.sfxpos_script) else (0, 0, 0)
        self.o_sfx += 1
        self.o_log.append(('sfxpos', pos, e.f[12], e.f[13], e.g32(7)))
        e.store(e.u32(5), a)
        e.store(e.u32(6), b)
        e.set32(2, v0)

    def o_sfx_fn(self, e):
        self.o_log.append(('sfx', e.g32(4), e.g32(5), e.g32(6), e.g32(7)))

    def o_rumble_fn(self, e):
        self.o_log.append(('rumble', e.g32(4), e.f[12], e.f[13]))

    def o_handler_fn(self, e, h):
        self.o_log.append(('handler', h, e.u32(4) - 0xD0, e.g32(5), e.u32(6) - 0x1F0))

    def o_free_fn(self, e):
        self.o_log.append(('free', e.u32(4)))

    # ---- native workers
    def n_alloc_fn(self, _, cls, out):
        a = self.allocs[self.n_alloc] if self.n_alloc < len(self.allocs) else 0
        self.n_alloc += 1
        self.n_log.append(('alloc', cls, a))
        out[0] = C.pointer(self.node_at(a)) if a else C.POINTER(Node)()
        return 0

    def n_rand_fn(self, _, out):
        v = self.rands[self.n_rand]
        self.n_rand += 1
        self.n_log.append(('rand', v))
        out[0] = v
        return 0

    def n_light_fn(self, _, pos, col, typ, fa, fb):
        self.n_log.append(('light', tuple(pos[i] for i in range(4)), tuple(col[i] for i in range(4)),
                           typ, fbits(fa), fbits(fb)))
        return 0

    def n_sfxpos_fn(self, _, pos, f12, f13, a, b, res):
        s = self.sfxpos_script[self.n_sfx] if self.n_sfx < len(self.sfxpos_script) else (0, 0, 0)
        self.n_sfx += 1
        self.n_log.append(('sfxpos', tuple(pos[i] for i in range(4)), fbits(f12), fbits(f13), 0))
        a[0], b[0], res[0] = sx(s[0]), sx(s[1]), sx(s[2])
        return 0

    def n_sfx_fn(self, _, i, a1, a2, a3):
        self.n_log.append(('sfx', i, a1, a2, a3))
        return 0

    def n_rumble_fn(self, _, ch, f12, f13):
        self.n_log.append(('rumble', ch, fbits(f12), fbits(f13)))
        return 0

    def n_handler_fn(self, _, handler, node, depth, work):
        a = self.addr(node)
        assert C.addressof(work.contents) == C.addressof(node.contents.work)
        self.n_log.append(('handler', handler, a, depth, a))
        return 0

    def n_free_fn(self, _, node):
        self.n_log.append(('free', self.addr(node)))
        return 0

    # ---- comparison
    def compare(self, label, before, mark):
        e = self.ee
        changed = set()
        for buf, o, old in e.journal[mark:]:
            base = 0x70000000 if buf is e.spad else 0
            for i in range(len(old)):
                a = base + o + i
                if not STACK - 0x2000 <= a < STACK + 0x100 and not SCRATCH <= a < SCRATCH + 0x100:
                    changed.add(a)
        changed = {a for a in changed if self.ee.load(a, 1) != before(a)}
        stray = sorted(changed - self.allowed)
        assert not stray, (label, 'original wrote unmodelled bytes', [hex(a) for a in stray[:12]])
        assert self.effect.fault.code == 0, (label, 'native fault', hex(self.effect.fault.address),
                                             self.effect.fault.code)
        # nodes
        for a, n in self.nodes.items():
            for name, off, size in NODE_FIELDS:
                o = e.load(a + off, size)
                v = getattr(n, name) & ((1 << (8 * size)) - 1)
                assert o == v, (label, hex(a), name, hex(o), hex(v))
            for name, off, cnt in NODE_ARRAYS:
                arr = getattr(n, name)
                got = [arr[i] for i in range(cnt)]
                want = [e.load(a + off + 4 * i) for i in range(cnt)]
                assert got == want, (label, hex(a), name, [hex(x) for x in want], [hex(x) for x in got])
            for name, off in WORK_FIELDS:
                assert e.load(a + off) == getattr(n.work, name) & M32, (label, hex(a), name)
        # tables
        assert bytes(self.tables.bytes) == bytes(e.ram[TABLE:TABLE_END]), (label, 'table bytes')
        # globals
        g = self.glob
        assert e.load(0x275C38) == g.d275C38 & M32, (label, 'D_00275C38')
        assert e.load(0x275C04) == g.d275C04 & M32, (label, 'D_00275C04')
        for i in range(16):
            assert e.load(0x70003600 + 4 * i) == g.spad3600[i], (label, 'spad 3600', i)
        for i in range(4):
            assert e.load(0x700038A0 + 4 * i) == g.spad38A0[i], (label, 'spad 38A0', i)
        if g.d275C30:
            assert e.load(0x275C30) == self.addr(g.d275C30), (label, 'D_00275C30')
            assert e.load(0x275C34) == self.addr(g.d275C30) + 0x1F0, (label, 'D_00275C34')
            assert C.addressof(g.d275C34.contents) == C.addressof(g.d275C30.contents.work)
        # decals
        for n in range(7):
            assert e.load(RING_INDEX + 4 * n) == self.decals.index[n] & M32, (label, 'ring index', n)
        assert bytes(C.string_at(C.addressof(self.decals.slot), 7 * 0xC00)) == bytes(e.ram[RING:RING + 7 * 0xC00]), \
            (label, 'ring slots')
        # calls
        assert self.o_log == self.n_log, dict(case=label, original=self.o_log, native=self.n_log)

    def run(self, label, original, native):
        """Run one original call and its native twin, then compare."""
        e = self.ee
        mark = len(e.journal)
        self.o_log.clear()
        self.n_log.clear()
        result = original(e)
        first = {}
        for buf, o, old in e.journal[mark:]:
            base = 0x70000000 if buf is e.spad else 0
            for i, b in enumerate(old):
                first.setdefault(base + o + i, b)
        nres = native()
        self.compare(label, lambda a: first.get(a, e.load(a, 1)), mark)
        return result, nres


# ----------------------------------------------------------------- scenarios ---

def load_beat(name):
    d = ROUTE / name
    return bytearray((d / 'eeMemory.bin').read_bytes()), bytearray((d / 'scratchpad.bin').read_bytes())


def live_driver_nodes(ram):
    out, node, n = [], struct.unpack_from('<I', ram, POOL_HEAD)[0], 0
    while node and n < 0x100:
        if struct.unpack_from('<I', ram, node + 0x10)[0] == 0x1EA240:
            out.append(node)
        node = struct.unpack_from('<I', ram, node + 0x1C)[0]
        n += 1
    return out


def vec_ptr(values):
    arr = (C.c_uint32 * len(values))(*values)
    return arr, C.cast(arr, FP)


def put_vec(world, a, values):
    world.ee.write(a, struct.pack('<%dI' % len(values), *values))


def spawn_case(lib, elf, beat, label, fn, id_, pos, rot, rands, allocs, sfx=(), glob=None):
    """001EFD90 (fn 'D90'), 001EFD20 ('D20') or 001EF9D0 ('9D0', pos may be None)."""
    ram, spad = beat
    w = World(lib, elf, ram, spad, rands=rands, allocs=allocs, sfxpos=sfx)
    try:
        e = w.ee
        if glob:
            for a, v, n in glob:
                e.store(a, v, n)
            w.pull_globals()
        P, R = SCRATCH, SCRATCH + 0x10
        put_vec(w, P, pos or [0, 0, 0, 0])
        put_vec(w, R, rot or [0, 0, 0, 0])
        out = C.POINTER(Node)()
        pa, pp = vec_ptr(pos or [0, 0, 0, 0])
        ra, rp = vec_ptr(rot or [0, 0, 0, 0])
        if fn == 'D90':
            orig = lambda ee: (ee.call(0x1EFD90, (id_, P, R)), ee.u32(2))[1]
            nat = lambda: lib.em_effect_original_001EFD90(C.byref(w.effect), id_, pp, rp, C.byref(out))
        elif fn == 'D20':
            orig = lambda ee: (ee.call(0x1EFD20, (id_, P)), ee.u32(2))[1]
            nat = lambda: lib.em_effect_original_001EFD20(C.byref(w.effect), id_, pp, C.byref(out))
        else:
            f12 = rot[3] if rot else ONE
            a1 = P if pos is not None else 0
            orig = lambda ee: (ee.call(0x1EF9D0, (id_, a1), (f12,)), ee.u32(2))[1]
            nat = lambda: lib.em_effect_original_001EF9D0(C.byref(w.effect), id_, pp if pos is not None else None,
                                                          FM.b2f(f12), C.byref(out))
        o, n = w.run(label, orig, nat)
        assert n == 0, (label, 'native result', n)
        assert o == w.addr(out), (label, 'returned node', hex(o), hex(w.addr(out)))
        assert w.o_rand == w.n_rand and w.o_alloc == w.n_alloc
        return w, o
    except Exception:
        w.ee.restore()
        raise


def drive(world, a, label, max_ticks=400):
    """Tick 001EA240 on node a (original and native) until it frees."""
    lib = world.lib
    node = world.node_at(a)
    ticks = 0
    while True:
        o, n = world.run((label, ticks), lambda ee: ee.call(0x1EA240, (a,)),
                         lambda: lib.em_effect_original_001EA240(C.byref(world.effect), C.byref(node)))
        ticks += 1
        if ('free', a) in world.o_log:
            assert n == 0, (label, ticks, 'native did not free')
            return ticks
        assert n == 1, (label, ticks, n)
        assert ticks < max_ticks, (label, 'never freed')


# ------------------------------------------------------------------ checks ---

def fp_against_vectors(lib, counts):
    """The native float helpers against the recorded PCSX2 vectors."""
    vec_dir = FM.VECTORS
    if not vec_dir.exists():
        raise SystemExit(f'no recorded vectors under {vec_dir}')
    ops = {'add.s': 0, 'sub.s': 1, 'mul.s': 2, 'div.s': 3, 'cvt.w.s': 4, 'cvt.s.w': 5,
           'c.eq.s': 6, 'c.lt.s': 7, 'c.le.s': 8}
    kinds = {'vadd': 9, 'vaddbc': 9, 'vaddq': 9, 'vsub': 10, 'vsubbc': 10, 'vmul': 11, 'vmulbc': 11,
             'vmulq': 11, 'vmulabc': 11, 'vopmula': 11, 'vmaddbc': 12, 'vmaddabc': 12, 'vopmsub': 13}
    recs = []
    for path in sorted(vec_dir.glob('*.jsonl')):
        with open(path) as fh:
            recs.extend(json.loads(line) for line in fh)
    recs = select(recs, pick(len(recs), 6000), 0x1EA240, axes=(lambda r: r['op'],))
    fp = lib.em_effect_original_fp
    done = 0
    for rec in recs:
        op = rec['op']
        if op in ops:
            got = fp(ops[op], 0, rec['fs'], rec.get('ft', 0), 0)
            want = rec['c'] if 'c' in rec else rec['fd']
            assert got == want, (op, rec, hex(got))
            done += 1
        elif op in kinds or op in ('vdiv', 'vsqrt', 'vftoi4', 'vmaxbc', 'vminibc'):
            want = FM.measured(rec)
            if op == 'vdiv':
                fsf, ftf = rec.get('fsf'), rec.get('ftf')
                if fsf is None:
                    inst = FM._instance(op)
                    fsf, ftf = inst['fsf'], inst['ftf']
                got = fp(14, int(FM.VU_DIV_FORMS[(fsf, ftf)]), rec['vs'][fsf], rec['vt'][ftf], 0)
                assert got == want, (rec, hex(got))
                done += 1
                continue
            if op == 'vsqrt':
                ftf = rec.get('ftf')
                if ftf is None:
                    ftf = FM._instance(op)['ftf']
                got = fp(15, 0, 0, rec['vt'][ftf], 0)
                assert got == want, (rec, hex(got))
                done += 1
                continue
            predicted = FM.predict(rec)
            assert predicted == want
            # recompute each written lane natively
            if 'sig' in rec:
                dest, bc = rec.get('dest'), rec.get('bc')
                s = rec['vs']
            else:
                inst = FM._instance(op)
                dest, bc = inst['dest'], inst['bc']
                s = FM.VF0 if op == 'vaddq' else rec['vs']
            for k in range(4):
                if not (dest >> (3 - k)) & 1:
                    continue
                if op == 'vftoi4':
                    got = fp(16, 0, s[k], 0, 0)
                elif op in ('vmaxbc', 'vminibc'):
                    got = fp(18 if op == 'vmaxbc' else 17, 0, s[k], rec['vt'][bc], 0)
                else:
                    form = FM.vu_form(op, dest, bc if op.endswith('bc') else None)
                    flags = form[0] | form[1] << 1 | form[2] << 2 | form[3] << 3
                    if op in ('vopmula', 'vopmsub'):
                        sk, tk = s[(1, 2, 0)[k]], rec['vt'][(2, 0, 1)[k]]
                    elif op in ('vmulq', 'vaddq'):
                        sk, tk = s[k], rec['q_in']
                    elif op.endswith('bc'):
                        sk, tk = s[k], rec['vt'][bc]
                    else:
                        sk, tk = s[k], rec['vt'][k]
                    acc = rec['acc_in'][k] if rec.get('acc_in') else 0
                    got = fp(kinds[op], flags, sk, tk, acc)
                assert got == predicted[k], (op, k, rec, hex(got))
            done += 1
    counts['fp_vectors'] = done
    return len(recs)


def fp_random(lib, counts):
    """Native helpers vs the model on random and boundary bit patterns."""
    rng = random.Random(0xEEF1)
    special = [0, FM.SIGN, 1, 0x807FFFFF, 0x00800000, FM.MAX, FM.MAX | FM.SIGN, FM.INF, FM.INF | FM.SIGN,
               0x7FC00000, 0x7F800001, ONE, 0xBF800000, 0x3F7FFFFF, 0x4B000000, 0x4F000000, 0xCF000000]
    vals = special + [rng.getrandbits(32) for _ in range(pick(4000, 200))]
    vals += [fbits(rng.uniform(-400, 400)) for _ in range(pick(4000, 200))]
    fp = lib.em_effect_original_fp
    n = 0
    for i, a in enumerate(vals):
        b = vals[(i * 7 + 3) % len(vals)]
        c = vals[(i * 13 + 5) % len(vals)]
        for op, fn in ((0, FM.ee_add), (1, FM.ee_sub), (2, FM.ee_mul), (3, FM.ee_div)):
            assert fp(op, 0, a, b, 0) == fn(a, b), (op, hex(a), hex(b))
        assert fp(4, 0, a, 0, 0) == FM.ee_cvt_w_s(a)
        assert fp(5, 0, a, 0, 0) == FM.ee_cvt_s_w(a)
        for op, fn in ((6, FM.ee_c_eq), (7, FM.ee_c_lt), (8, FM.ee_c_le)):
            assert fp(op, 0, a, b, 0) == fn(a, b)
        for flags in range(16):
            form = (flags & 1, flags >> 1 & 1, flags >> 2 & 1, flags >> 3 & 1)
            for kind in (9, 10, 11, 12, 13):
                cs, ct, ca, order = form
                s, t = (FM._saturate(a) if cs else a), (FM._saturate(b) if ct else b)
                if kind == 9: want = FM._vu_add_raw(s, t)
                elif kind == 10: want = FM._vu_sub_raw(s, t)
                elif kind == 11: want = FM._vu_mul_raw(s, t)
                else:
                    p = FM._vu_mul_raw(s, t)
                    acc = FM._saturate(c) if ca else c
                    want = (FM._vu_add_raw(p, acc) if order else FM._vu_add_raw(acc, p)) if kind == 12 \
                        else FM._vu_sub_raw(acc, p)
                assert fp(kind, flags, a, b, c) == want, (kind, flags, hex(a), hex(b), hex(c))
        assert fp(14, 1, a, b, 0) == FM.vu_div(a, b, 3, 0)
        assert fp(14, 0, a, b, 0) == FM.vu_div(a, b, 0, 0)
        assert fp(15, 0, 0, b, 0) == FM.vu_sqrt(b)
        assert fp(16, 0, a, 0, 0) == FM.vu_ftoi(a, 4)
        assert fp(17, 0, a, b, 0) == FM.vu_min(a, b)
        assert fp(18, 0, a, b, 0) == FM.vu_max(a, b)
        n += 1
    counts['fp_random'] = n


def leaf_checks(lib, elf, beat, counts):
    """001B1470, float_to_int, 001CD390 and 001CCF70 alone."""
    ram, spad = beat
    rng = random.Random(0x1B1470)
    w = World(lib, elf, ram, spad)
    e = w.ee
    # 001B1470
    angles = [0x40490FDB, 0xC0490FDB, 0x40490FDC, 0xC0490FDC, 0, FM.SIGN, fbits(9.5), fbits(-9.5),
              fbits(12.9), fbits(-12.9), fbits(6.2831855), fbits(-6.2831855)]
    angles += [fbits(rng.uniform(-13, 13)) for _ in range(pick(600, 60))]
    for a in angles:
        e.call(0x1B1470, (), (a,))
        assert lib.em_effect_original_fp(20, 0, a, 0, 0) == e.f[0], ('001B1470', hex(a))
    counts['wrap'] = len(angles)
    # float_to_int
    vals = [0, FM.SIGN, 1, ONE, 0xBF800000, 0x3F7FFFFF, 0x4EFFFFFF, 0x4F000000, 0xCF000000, 0xCF000001,
            FM.INF, FM.INF | FM.SIGN, 0x7FC00000, 0x7F800001, 0x7F900000]
    vals += [rng.getrandbits(32) for _ in range(pick(1500, 150))]
    for v in vals:
        e.call(0x1281C0, (), (v,))
        assert lib.em_effect_original_fp(19, 0, v, 0, 0) == e.u32(2), ('float_to_int', hex(v))
    e.restore()
    counts['float_to_int'] = len(vals)
    # 001CD390 (both guard arms, zeros of both signs, random)
    OUT, VEC = SCRATCH + 0x40, SCRATCH + 0x20
    cases = [[0, fbits(1.0), 0, ONE], [FM.SIGN, fbits(2.0), FM.SIGN, 0], [0, 0, 0, 0], [1, fbits(3.0), 0x80000001, 0],
             [fbits(1.0), 0, 0, 0], [0, 0, fbits(-1.0), 0]]
    cases += [[fbits(rng.uniform(-2, 2)) for _ in range(3)] + [rng.getrandbits(32)] for _ in range(pick(400, 40))]
    for v in cases:
        put_vec(w, VEC, v)
        w.glob.spad3600[:] = [e.load(0x70003600 + 4 * i) for i in range(16)]
        va, vp = vec_ptr(v)
        out = (C.c_uint32 * 16)()
        o, n = w.run(('001CD390', v), lambda ee: ee.call(0x1CD390, (OUT, VEC)),
                     lambda: lib.em_effect_original_001CD390(C.byref(w.effect), out, vp))
        assert n == 0
        assert [e.load(OUT + 4 * i) for i in range(16)] == list(out), ('001CD390 out', v)
    counts['look_at'] = len(cases)
    # 001CCF70 over the captured view: points around the camera target
    P = SCRATCH + 0x20
    tgt = [FM.b2f(e.load(0x8105E0 + 4 * i)) for i in range(3)]
    pts = [[fbits(t + rng.uniform(-60, 60)) for t in tgt] + [rng.getrandbits(32)] for _ in range(pick(600, 80))]
    pts += [[fbits(rng.uniform(-3000, 3000)) for _ in range(3)] + [ONE] for _ in range(pick(200, 20))]
    clipped = inside = 0
    for p in pts:
        put_vec(w, P, p)
        pa, pp = vec_ptr(p)
        res = C.c_int32()
        o, n = w.run(('001CCF70', p), lambda ee: (ee.call(0x1CCF70, (P,)), ee.u32(2))[1],
                     lambda: lib.em_effect_original_001CCF70(C.byref(w.effect), pp, C.byref(res)))
        assert n == 0 and res.value & M32 == o, ('001CCF70', p, hex(o), res.value)
        clipped += o == 0xFFFFFF
        inside += o != 0xFFFFFF
    assert clipped and inside, ('001CCF70 needs both arms', clipped, inside)
    # the captured fog quad never reaches its 255 / 0 bounds in view: vary it
    ctx = w.u32(0x275670)
    fogs = [[0x437F0000, 0x45000000, fbits(900.0), 0xBEFE80C0], [0x437F0000, 0x45000000, fbits(-900.0), 0xBEFE80C0],
            [fbits(40.0), fbits(10.0), fbits(151.1), fbits(0.25)], [0, 0, 0, 0]]
    bounded = 0
    for fog in fogs:
        put_vec(w, ctx + 0xA0, fog)
        w.pull_view()
        for p in pts[:pick(len(pts), 30)]:
            put_vec(w, P, p)
            pa, pp = vec_ptr(p)
            res = C.c_int32()
            o, n = w.run(('001CCF70 fog', fog, p), lambda ee: (ee.call(0x1CCF70, (P,)), ee.u32(2))[1],
                         lambda: lib.em_effect_original_001CCF70(C.byref(w.effect), pp, C.byref(res)))
            assert n == 0 and res.value & M32 == o
            bounded += o != 0xFFFFFF and e.load(0x7000360C) in (40 * 16, 255 * 16, 0)
    assert bounded, 'fog bounds never reached'
    counts['project_fog_bounded'] = bounded
    counts['project'] = len(pts)
    counts['project_clipped'] = clipped
    e.restore()


ENTITY_IDS = None


def entity(elf, i):
    o = TABLE - 0x100000 + 0x300 + 0x30 * i
    return elf[o:o + 0x30]


def spawn_sweep(lib, elf, beat, counts):
    """001EF9D0 / 001EFD90 / 001EFD20 over every sign-bit id with a record."""
    rng = random.Random(0x1EF9D0)
    ram, spad = beat
    player = [struct.unpack_from('<I', ram, 0x8102B0 + 0xB0 + 4 * i)[0] for i in range(4)]
    prot = [struct.unpack_from('<I', ram, 0x8102B0 + 0xC0 + 4 * i)[0] for i in range(4)]
    ids = [0x80000000 | i for i in range(0xAA)]
    cases = []
    for id_ in ids:
        for fn in ('D90', 'D20', '9D0'):
            cases.append((id_, fn))
    for id_ in (0x80000026, 0x8000002C, 0x80000067):
        for fn in ('D90', 'D20', '9D0'):
            for _ in range(3):
                cases.append((id_, fn))
    cases += [(i, fn) for i in (0, 1, 5, 0x20) for fn in ('D90', 'D20')]  # area table ids (D_00810700 = 11)
    total = len(cases)
    cases = select(cases, pick(len(cases), 180), 0x1EFD90, axes=(lambda c: c[0] & 0xFF, lambda c: c[1]))
    n = 0
    for id_, fn in cases:
        pos = [fbits(rng.uniform(-50, 50)) for _ in range(3)] + [rng.choice((ONE, 0))]
        rot = [fbits(rng.uniform(-3.2, 3.2)) for _ in range(3)] + [rng.choice((ONE, ONE, 0, fbits(0.5)))]
        if rng.random() < 0.3:
            pos, rot = list(player), list(prot)
        rands = [sx(rng.getrandbits(32)) for _ in range(4)]
        allocs = [rng.choice((SCRATCH + 0x1000, SCRATCH + 0x1000, 0))]
        sfx = [(rng.getrandbits(32), rng.getrandbits(32), rng.choice((0, 1)))]
        glob = [(0x8101E4, rng.choice((0, 3, 4)), 1), (0x70003B68, rng.getrandbits(32), 4),
                (0x275C38, rng.choice((0, rng.getrandbits(32))), 4)]
        if fn == '9D0' and rng.random() < 0.3:
            pos = None
        w, _ = spawn_case(lib, elf, beat, ('spawn', hex(id_), fn), fn, id_, pos, rot, rands, allocs, sfx, glob)
        w.ee.restore()
        n += 1
    counts['spawn'] = n
    # the table entries whose sound is live (0x27, 0x44) with 001FBF50 results 0 and 1, gate byte 3
    for id_ in (0x80000027, 0x80000044):
        for v0 in (0, 1, 7):
            for gate in (0, 3):
                pos = [fbits(rng.uniform(-50, 50)) for _ in range(3)] + [ONE]
                w, _ = spawn_case(lib, elf, beat, ('sound', hex(id_), v0, gate), '9D0', id_, pos, [0, 0, 0, ONE],
                                  [], [SCRATCH + 0x1000], [(123, sx(0xFFFFFF85), v0)],
                                  [(0x8101E4, gate, 1), (0x70003B68, 100, 4), (0x275C38, 0, 4)])
                w.ee.restore()
                counts['spawn_sound'] = counts.get('spawn_sound', 0) + 1
    return total


def decal_sweep(lib, elf, beat, counts):
    rng = random.Random(0x1F0460)
    ram, spad = beat
    n = 0
    for preset in range(-1, 8):
        for start in (0, 5, 0x13, 0x14, 0x1F, 0x20, 0x40):
            if preset not in range(7) and start:
                continue
            w = World(lib, elf, ram, spad, allocs=[SCRATCH + 0x1000, 0][rng.randrange(2):],
                      rands=[sx(rng.getrandbits(32)) for _ in range(4)])
            e = w.ee
            if preset in range(7):
                e.store(RING_INDEX + 4 * preset, start)
            src = [rng.getrandbits(32) for _ in range(16)]
            put_vec(w, SCRATCH + 0x40, src)
            w.pull_decals()
            sa, sp_ = vec_ptr(src)
            w.run(('decal', preset, start), lambda ee: ee.call(0x1F0460, (preset & M32, SCRATCH + 0x40)),
                  lambda: lib.em_effect_original_001F0460(C.byref(w.effect), preset, sp_))
            e.restore()
            n += 1
    counts['decal'] = n


def driver_synthetic(lib, elf, beat, counts):
    """Spawn every 001EA240 subtype through 001EFD90 and drive it to its free."""
    rng = random.Random(0x1EA240)
    ram, spad = beat
    subs = {}
    for i in range(0xAA):
        rec = entity(elf, i)
        if struct.unpack_from('<I', rec, 0xC)[0] == 0x1EA240:
            subs.setdefault(rec[8], 0x80000000 | i)
    ids = sorted(subs.values())
    route = [0x80000028, 0x80000065, 0x80000049, 0x80000033, 0x80000005, 0x80000011, 0x80000015,
             0x8000001D, 0x8000005F, 0x80000066, 0x80000067, 0x80000068, 0x80000003]
    ids = select(ids, pick(len(ids), 16), 0x28, keep=lambda _, i: i in route)
    ticks = 0
    for id_ in ids:
        for variant in range(pick(3, 1)):
            pos = [fbits(rng.uniform(-80, 80)) for _ in range(3)] + [ONE]
            rot = [fbits(rng.uniform(-3.2, 3.2)) for _ in range(3)] + [rng.choice((0, ONE))]
            if variant == 1:
                rot[0] = rot[2] = 0
            node_a = SCRATCH + 0x1000
            rands = [sx(rng.getrandbits(32)) for _ in range(12)]
            glob = [(0x8101E4, 0, 1), (0x8102E8, fbits(rng.uniform(0.5, 1.5)), 4)]
            w = World(lib, elf, ram, spad, rands=rands, allocs=[node_a, SCRATCH + 0x1400])
            e = w.ee
            for a, v, n in glob:
                e.store(a, v, n)
            e.store(node_a + 4, 0, 1)       # 001AFA90 hands out a record in state 0
            e.store(SCRATCH + 0x1404, 0, 1)
            w.pull_globals()
            put_vec(w, SCRATCH, pos)
            put_vec(w, SCRATCH + 0x10, rot)
            out = C.POINTER(Node)()
            pa, pp = vec_ptr(pos)
            ra, rp = vec_ptr(rot)
            o, n = w.run(('drv-spawn', hex(id_)), lambda ee: (ee.call(0x1EFD90, (id_, SCRATCH, SCRATCH + 0x10)), ee.u32(2))[1],
                         lambda: lib.em_effect_original_001EFD90(C.byref(w.effect), id_, pp, rp, C.byref(out)))
            assert o == node_a and w.addr(out) == node_a, (hex(id_), hex(o))
            ticks += drive(w, node_a, ('drive', hex(id_), variant))
            e.restore()
    counts['driver_synthetic_nodes'] = len(ids)
    counts['driver_synthetic_ticks'] = ticks


def driver_boundaries(lib, elf, counts):
    """One 001EA240 tick from crafted states: the limit comparison at its
    edges, the endless (limit 0) decay clamp, the free states and the
    rumble subtypes."""
    ram, spad = load_beat('08_truck_crossing')
    a = live_driver_nodes(ram)[0]
    rng = random.Random(0x1EAA00)
    lim15, lim2 = 0x3FC00000, 0x40000000
    cases = []
    for sub in (5, 0x20, 0x24, 1, 0x28, 0x1D, 0x15, 3, 6, 0x19, 0x26, 0x13):
        for acc, limit, step in ((lim15, lim15, 0), (0x3FBFFFFF, lim15, 1), (lim15, lim15, 1), (0x3FB851EB, lim15, 0x3CF5C28F),
                                 (lim2, 0, 0), (0x40000001, 0, 0), (0x40800000, 0, 0x3D4CCCCD), (0x3F000000, FM.SIGN, 0),
                                 (0x3F000000, 1, 0), (0x3F000000, 0x80000001, 0)):
            cases.append((1, sub, acc, limit, step))
    for state in (2, 3, 4, 0xFF):
        cases.append((state, 5, 0, lim15, 0))
    cases = select(cases, pick(len(cases), 60), 0x1EAA00, axes=(lambda c: c[0], lambda c: c[1], lambda c: (c[2], c[3])))
    for state, sub, acc, limit, step in cases:
        w = World(lib, elf, ram, spad)
        e = w.ee
        e.store(a + 4, state, 1)
        e.store(a + 0xD, sub, 1)
        e.store(a + 0x244, acc)
        e.store(a + 0x1FC, limit)
        e.store(a + 0x1F8, step)
        e.store(a + 0x1F0, rng.getrandbits(32))
        node = w.node_at(a)
        o, n = w.run(('bound', state, sub, hex(acc), hex(limit)), lambda ee: ee.call(0x1EA240, (a,)),
                     lambda: lib.em_effect_original_001EA240(C.byref(w.effect), C.byref(node)))
        assert n == (0 if state in (2, 3) else 1), (state, n)
        e.restore()
    counts['driver_boundaries'] = len(cases)
    # non-finite translations: vmaddw.xyzw (form (15,3)) clamps ACC, so VCLIP
    # (outside the measured model) only ever receives finite lanes
    nonfinite = 0
    for lane in (0, 1, 2):
        for v in (0x7FC00000, 0xFFC00001, 0x7F800000, 0xFF800000, 0x7F7FFFFF, 0x00000001):
            w = World(lib, elf, ram, spad)
            e = w.ee
            e.store(a + 4, 1, 1)
            e.store(a + 0xD, 5, 1)
            e.store(a + 0x100 + 4 * lane, v)
            node = w.node_at(a)
            w.run(('nonfinite', lane, hex(v)), lambda ee: ee.call(0x1EA240, (a,)),
                  lambda: lib.em_effect_original_001EA240(C.byref(w.effect), C.byref(node)))
            e.restore()
            nonfinite += 1
    counts['driver_nonfinite'] = nonfinite
    # 001EF9D0 kind 4 (record 0x39): the 13-tick throttle at its edges
    beat = load_beat('05_boxes')
    n = 0
    for stamp, clock in ((0, 12), (0, 13), (13, 0), (12, 0), (0x7FFFFFF8, 0x80000004), (0x80000000, 0),
                         (0, 0x80000000), (100, 100), (5, 0xFFFFFFF8)):
        w, _ = spawn_case(lib, elf, beat, ('kind4', stamp, clock), '9D0', 0x80000039,
                          [fbits(1.0), fbits(2.0), fbits(3.0), ONE], [0, 0, 0, ONE], [], [SCRATCH + 0x1000], [],
                          [(0x8101E4, 0, 1), (0x70003B68, clock, 4), (0x275C38, stamp, 4)])
        w.ee.restore()
        n += 1
    counts['kind4_throttle'] = n


def driver_captured(lib, elf, counts):
    """Lockstep from every live 001EA240 node of the route snapshots, and the
    capture check of each node's matrix and accumulator."""
    ticks = nodes = 0
    captured = []
    for beat_dir in sorted(ROUTE.iterdir()):
        if not (beat_dir / 'eeMemory.bin').exists():
            continue
        ram, spad = load_beat(beat_dir.name)
        live = live_driver_nodes(ram)
        for a in live:
            captured.append((beat_dir.name, a, ram[a + 0xD]))
            # (2) capture check: recompute state 0 natively from +B0/+C0
            w = World(lib, elf, ram, spad, rands=[0, 0, 0, 0, 0, 0])
            n = w.node_at(a)
            probe = Node()
            C.pointer(probe)[0] = n
            probe.state = 0
            w.addr_of[C.addressof(probe)] = a
            assert lib.em_effect_original_001EA240(C.byref(w.effect), C.byref(probe)) == 1
            assert w.effect.fault.code == 0
            assert list(probe.matrix) == list(n.matrix), (beat_dir.name, hex(a), 'captured matrix')
            step, acc = n.work.step, n.work.accumulator
            assert n.subtype in (5, 0x20), ('unexpected captured subtype', n.subtype)
            v, k = 0, 0          # both subtypes seed the accumulator with 0.0
            while v != acc:
                v = FM.ee_add(v, step)
                k += 1
                assert k < 1000, (beat_dir.name, hex(a), 'accumulator off the EE-add trajectory')
            assert k >= 1 and n.state == 1 and n.b09 == 0 and n.b0C == 0
            assert n.work.seed == n.work.seed_copy or n.subtype == 0x20
            # (1) lockstep from the captured state until the free
            w = World(lib, elf, ram, spad, rands=[])
            ticks += drive(w, a, (beat_dir.name, hex(a)))
            w.ee.restore()
            nodes += 1
    assert nodes, 'no live 001EA240 nodes in the route snapshots'
    counts['captured_nodes'] = nodes
    counts['captured_ticks'] = ticks
    return captured


def point_light_binding(lib, elf, counts):
    """The kind 1/2/4 argument sets through em_point_light_register against the
    original 001D7FA0 (the binding the header names)."""
    beat = load_beat('08_truck_crossing')
    ram, spad = beat
    w = World(lib, elf, ram, spad)
    e = w.ee
    del e.stubs[W_LIGHT]
    ctx = w.u32(0x275670)

    class Light(C.Structure):
        _fields_ = [('m', C.c_float), ('a', C.c_float), ('type', C.c_int32), ('handle', C.c_int32),
                    ('position', C.c_float * 4), ('color', C.c_float * 4), ('angle', C.c_float * 4),
                    ('matrix', C.c_float * 16)]

    class Pool(C.Structure):
        _fields_ = [('next', C.c_uint32), ('pending', C.c_int32), ('active', Light * 32), ('staged', Light * 32)]

    lib.em_point_light_register.argtypes = [C.POINTER(Pool), FP, FP, C.c_int32, C.c_float, C.c_float]
    lib.em_point_light_register.restype = C.c_int32
    rng = random.Random(0x1D7FA0)
    n = 0
    for typ, fa, fb in ((0, 0x3F19999A, 0xBF800000), (0, 0x3F733333, 0xBD4CCCCD), (1, 0x3F733333, 0xBD4CCCCD)):
        for count in (0, 5, 31, 32):
            pool = Pool()
            C.memmove(C.addressof(pool), bytes(e.ram[ctx + 0x210:ctx + 0x210 + 8]) + bytes(e.ram[ctx + 0x220:ctx + 0x2220]), 8 + 0x2000)
            pool.pending = count
            e.store(ctx + 0x214, count)
            pos = [fbits(rng.uniform(-9, 9)) for _ in range(4)]
            col = [fbits(rng.uniform(0, 64)) for _ in range(4)]
            put_vec(w, SCRATCH, pos)
            put_vec(w, SCRATCH + 0x10, col)
            e.call(0x1D7FA0, (SCRATCH, SCRATCH + 0x10, typ), (fa, fb))
            pa, pp = vec_ptr(pos)
            ca, cp = vec_ptr(col)
            got = lib.em_point_light_register(C.byref(pool), pp, cp, typ, FM.b2f(fa), FM.b2f(fb))
            assert got & M32 == e.u32(2), ('001D7FA0', typ, count)
            want = bytes(e.ram[ctx + 0x210:ctx + 0x218]) + bytes(e.ram[ctx + 0x220:ctx + 0x2220])
            assert bytes(pool)[:len(want)] == want, ('001D7FA0 pool', typ, count)
            e.restore()
            n += 1
    counts['point_light_binding'] = n


def main():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA, 'not the pinned boot ELF'
    lib = build_lib()
    counts = {}
    nrec = fp_against_vectors(lib, counts)
    fp_random(lib, counts)
    beat05 = load_beat('05_boxes')
    beat08 = load_beat('08_truck_crossing')
    leaf_checks(lib, elf, beat08, counts)
    nspawn = spawn_sweep(lib, elf, beat05, counts)
    decal_sweep(lib, elf, beat05, counts)
    driver_synthetic(lib, elf, beat08, counts)
    driver_boundaries(lib, elf, counts)
    captured = driver_captured(lib, elf, counts)
    point_light_binding(lib, elf, counts)
    out = ROOT / 'build/effect_original_reference'
    report = dict(status='PASS', elf_sha256=ELF_SHA, cases=counts,
                  captured=[(b, hex(a), s) for b, a, s in captured],
                  original_functions=['001EFD90', '001EFD20', '001EF9D0', '001EF940', '001D80E0', '001D8100',
                                      '001D7FA0 (via em_point_light_register)', '001F0460', '001EA240',
                                      '001CCF70', '001CD370', '001CD390', '001029C0', '00102918', '001026A0',
                                      '00102760', '00102718', '001031E0', '00102948', 'copy_qw4', '001029E8',
                                      '00102A60', '00102BB0', '00102B08', '00102C58', '001B1470', '0011E860',
                                      '001281C0', '001278C0'])
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    banner(part(counts['fp_vectors'], nrec, 'recorded float vectors'),
           part(counts['spawn'], nspawn, 'spawn cases'),
           f"{counts['driver_synthetic_ticks']} synthetic driver ticks",
           f"{counts['captured_nodes']} captured nodes / {counts['captured_ticks']} lockstep ticks")
    print('Original effect chain: PASS', json.dumps(counts))


if __name__ == '__main__':
    main()
