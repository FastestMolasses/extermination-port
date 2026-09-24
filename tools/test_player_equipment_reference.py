#!/usr/bin/env python3
"""Execute the original equipment routines and compare em_player_equipment*.c.

docs/PLAYER_EQUIPMENT.md. The user's pinned ELF (and the route captures under
../Extermination/build/s87/route/) supply every instruction, table and
record; none are embedded here. Routines executed unmodified:

  0018A6B0  node behaviour        0018A8D0  node init
  00188630  flavour 0             00188A50 / 00188AC0 / 00188B80  flavour 1
  00188DF0 / 00188ED0  flavour 2  0018A1F0 / 00189D30  flavour 4
  0015D2F0  camera-mode code      001CD520  depth-faded sprite
  copy_qw4 (00102958), 00102948, 001031E0  (the plain copies, not hooked)

Every other callee is hooked and recorded. The SDK VU0 routines (001026A0,
001026D0, 001028B8, 001028D0, 00102760, 001029C0, 00102BB0) are hooked on
both sides and evaluated by running the ORIGINAL routine in a separate
interpreter on the recorded inputs, so both sides store the original's
values. The other workers get scripted effects, identical on both sides.
The test asserts that the hooked set is exactly the set of jal targets of
the translated routines that are not translated here.

Arithmetic: COP1 and VU0 macro instructions go through
tools/ee_float_model.py (docs/EE_FLOAT_MODEL.md). VCLIPw is outside that
model; like tools/test_effect_original_reference.py it runs on DAZ'd finite
operands only (a non-finite operand is 'unsupported' and the native side
must fault).

Default run (~10 s): unit cases with every conditional branch outcome in the
translated routines asserted (except the listed impossible ones), the seven
live nodes of every route capture ticked over the captured RAM, and a
sample of the sprite and 0015D2F0 sweeps. EM_TEST_FULL=1: the exhaustive
sweeps.

No original instruction bytes, disassembly or data are written by this file;
build/<lane>/report.json holds only counts.
"""
import ctypes as C
import hashlib
import json
import os
import random
import struct
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ee_float_model as FM  # noqa: E402
import reference_mode as RM  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
ROUTE = DECOMP / 'build/s87/route'
ELF_SHA = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
LANE = os.environ.get('EM_LANE', 'player_equipment_reference')
OUT = ROOT / 'build' / LANE

M32, M64, M128 = (1 << 32) - 1, (1 << 64) - 1, (1 << 128) - 1
ONE = 0x3F800000
RET = 0x0BADF00C
STACK = 0x01F80000
STACK_LOW = 0x01F70000


def sx(v, bits=32):
    v &= (1 << bits) - 1
    return v - (1 << bits) if v >> (bits - 1) else v


def fb(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


class Unsupported(AssertionError):
    pass


# ======================================================================
# The interpreter
# ======================================================================

class EE:
    """Bounded EE + VU0-macro interpreter over a RAM image."""

    def __init__(self, elf, ram, spad):
        self.elf, self.ram, self.spad = elf, ram, spad
        self.r = [0] * 32
        self.f = [0] * 32
        self.lo = self.hi = self.lo1 = self.hi1 = 0
        self.cond = 0
        self.vf = [[0, 0, 0, 0] for _ in range(32)]
        self.vf[0] = [0, 0, 0, ONE]
        self.acc = [0, 0, 0, 0]
        self.q = 0
        self.clip = 0
        self.stubs = {}
        self.steps = 0
        self.writes = None        # list of (address, size) while recording
        self.ranges = ()          # (start, end) of routines whose branches are recorded
        self.outcomes = None
        self.undo = None          # (buffer, offset, old bytes) of every write, for restore()

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
        if self.writes is not None:
            self.writes.append((a & M32, n))
        if self.undo is not None:
            self.undo.append((buf, o, bytes(buf[o:o + n])))
        buf[o:o + n] = (v & ((1 << (8 * n)) - 1)).to_bytes(n, 'little')

    def poke(self, a, v, n=4):
        """A write made by a hook or the harness (not recorded as the
        original's; still undone after the case)."""
        buf, o = self.where(a)
        if self.undo is not None:
            self.undo.append((buf, o, bytes(buf[o:o + n])))
        buf[o:o + n] = (v & ((1 << (8 * n)) - 1)).to_bytes(n, 'little')

    def words(self, a, n):
        return tuple(self.load(a + 4 * i) for i in range(n))

    def put_words(self, a, values):
        for i, v in enumerate(values):
            self.poke(a + 4 * i, v)

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

    def arg(self, n):
        return self.r[4 + n] & M32

    # COP2 macro
    def vlane(self, name, dest, bc, fs, t_of, acc_of=None, out='vf', fd=0, swz=None):
        res = list(self.acc if out == 'acc' else self.vf[fd])
        for k in range(4):
            if (dest >> (3 - k)) & 1:
                s = fs[swz[k]] if swz else fs[k]
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
            elif rs == 2:          # cfc2 (clip flags only)
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
                self.vlane('vaddbc', dest, bc, S, lambda k: T[bc], fd=fd)
            elif fn < 8:
                self.vlane('vsubbc', dest, bc, S, lambda k: T[bc], fd=fd)
            elif fn < 0xC:
                acc = list(self.acc)
                self.vlane('vmaddbc', dest, bc, S, lambda k: T[bc], lambda k: acc[k], fd=fd)
            elif 0x10 <= fn < 0x18:
                op = FM.vu_max if fn < 0x14 else FM.vu_min
                res = list(self.vf[fd])
                for k in range(4):
                    if (dest >> (3 - k)) & 1:
                        res[k] = op(S[k], T[bc])
                if fd:
                    self.vf[fd] = res
            elif fn < 0x1C:
                self.vlane('vmulbc', dest, bc, S, lambda k: T[bc], fd=fd)
            elif fn == 0x1C:
                self.vlane('vmulq', dest, None, S, lambda k: self.q, fd=fd)
            elif fn == 0x20:
                self.vlane('vaddq', dest, None, S, lambda k: self.q, fd=fd)
            elif fn == 0x28:
                self.vlane('vadd', dest, None, S, lambda k: T[k], fd=fd)
            elif fn == 0x2A:
                self.vlane('vmul', dest, None, S, lambda k: T[k], fd=fd)
            elif fn == 0x2C:
                self.vlane('vsub', dest, None, S, lambda k: T[k], fd=fd)
            elif fn == 0x2E:
                acc = list(self.acc)
                self.vlane('vopmsub', dest, None, S, lambda k: T[(2, 0, 1, 3)[k]], lambda k: acc[k],
                           fd=fd, swz=(1, 2, 0, 3))
            else:
                raise Unsupported(('vu upper', hex(pc), hex(fn)))
            return
        idx = fd << 2 | (fn & 3)
        bc = fn & 3
        fsf, ftf = w >> 21 & 3, w >> 23 & 3
        if 0x08 <= idx < 0x0C:
            acc = list(self.acc)
            self.vlane('vmaddabc', dest, bc, S, lambda k: T[bc], lambda k: acc[k], out='acc')
        elif 0x18 <= idx < 0x1C:
            self.vlane('vmulabc', dest, bc, S, lambda k: T[bc], out='acc')
        elif idx == 0x2E:
            self.vlane('vopmula', dest, None, S, lambda k: T[(2, 0, 1, 3)[k]], out='acc', swz=(1, 2, 0, 3))
        elif idx == 0x15:          # float to 28.4
            res = list(self.vf[ft])
            for k in range(4):
                if (dest >> (3 - k)) & 1:
                    res[k] = FM.vu_ftoi(S[k], 4)
            if ft:
                self.vf[ft] = res
        elif idx == 0x14:          # float to integer
            res = list(self.vf[ft])
            for k in range(4):
                if (dest >> (3 - k)) & 1:
                    res[k] = FM.vu_ftoi(S[k], 0)
            if ft:
                self.vf[ft] = res
        elif idx == 0x1F:          # clip test of fs.xyz against |ft.w| (finite operands only)
            vals = S[:3] + [T[3]]
            if any((v >> 23 & 0xFF) == 0xFF for v in vals):
                raise Unsupported(('clip non-finite', hex(pc)))
            w_ = abs(FM.b2f(FM._daz(T[3])))
            flags = 0
            for k in range(3):
                c = FM.b2f(FM._daz(S[k]))
                flags |= (c > w_) << (2 * k) | (c < -w_) << (2 * k + 1)
            self.clip = (self.clip << 6 | flags) & 0xFFFFFF
        elif idx in (0x2F, 0x3B):
            pass                   # no-op and Q wait
        elif idx == 0x30:          # move
            res = list(self.vf[ft])
            for k in range(4):
                if (dest >> (3 - k)) & 1:
                    res[k] = S[k]
            if ft:
                self.vf[ft] = res
        elif idx == 0x31:          # rotate lanes
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
            elif fn == 0x30: self.cond = 0
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
            elif fn == 4: self.set32(rd, self.u32(rt) << (self.u32(rs) & 31))
            elif fn == 6: self.set32(rd, self.u32(rt) >> (self.u32(rs) & 31))
            elif fn == 7: self.set32(rd, self.g32(rt) >> (self.u32(rs) & 31))
            elif fn == 0x0A:
                if not r[rt] & M64: self.set64(rd, r[rs])
            elif fn == 0x0B:
                if r[rt] & M64: self.set64(rd, r[rs])
            elif fn == 0x10: self.set64(rd, self.hi)
            elif fn == 0x12: self.set64(rd, self.lo)
            elif fn == 0x18:                                   # 32-bit multiply into LO/HI (and rd)
                p = self.g32(rs) * self.g32(rt)
                self.lo, self.hi = sx(p, 32) & M64, sx(p >> 32, 32) & M64
                self.set64(rd, self.lo)
            elif fn == 0x19:
                p = self.u32(rs) * self.u32(rt)
                self.lo, self.hi = sx(p, 32) & M64, sx(p >> 32, 32) & M64
                self.set64(rd, self.lo)
            elif fn in (0x20, 0x21): self.set32(rd, self.u32(rs) + self.u32(rt))
            elif fn in (0x22, 0x23): self.set32(rd, self.u32(rs) - self.u32(rt))
            elif fn == 0x24: self.set64(rd, r[rs] & r[rt])
            elif fn == 0x25: self.set64(rd, r[rs] | r[rt])
            elif fn == 0x26: self.set64(rd, r[rs] ^ r[rt])
            elif fn == 0x27: self.set64(rd, ~(r[rs] | r[rt]))
            elif fn == 0x2A: self.set64(rd, int(sx(r[rs], 64) < sx(r[rt], 64)))
            elif fn == 0x2B: self.set64(rd, int((r[rs] & M64) < (r[rt] & M64)))
            elif fn == 0x2D: self.set64(rd, r[rs] + r[rt])
            elif fn == 0x2F: self.set64(rd, r[rs] - r[rt])
            elif fn == 0x38: self.set64(rd, (r[rt] & M64) << sa)
            elif fn == 0x3A: self.set64(rd, (r[rt] & M64) >> sa)
            elif fn == 0x3B: self.set64(rd, sx(r[rt], 64) >> sa)
            elif fn == 0x3C: self.set64(rd, (r[rt] & M64) << (sa + 32))
            elif fn == 0x3E: self.set64(rd, (r[rt] & M64) >> (sa + 32))
            elif fn == 0x3F: self.set64(rd, sx(r[rt], 64) >> (sa + 32))
            elif fn == 0x0F: pass                                  # sync
            else: raise Unsupported(('special', hex(pc), hex(fn)))
        elif op in (8, 9): self.set32(rt, self.u32(rs) + imm)
        elif op in (0x18, 0x19): self.set64(rt, r[rs] + imm)
        elif op == 0x0A: self.set64(rt, int(sx(r[rs], 64) < imm))
        elif op == 0x0B: self.set64(rt, int((r[rs] & M64) < (imm & M64)))
        elif op == 0x0C: self.set64(rt, r[rs] & (w & 0xFFFF))
        elif op == 0x0D: self.set64(rt, (r[rs] & M64) | (w & 0xFFFF))
        elif op == 0x0E: self.set64(rt, (r[rs] & M64) ^ (w & 0xFFFF))
        elif op == 0x0F: self.set32(rt, (w & 0xFFFF) << 16)
        elif op == 0x1C:
            if fn == 0x28 and sa == 0x18:   # parallel byte add (the compiler's register move)
                a, b = r[rs], r[rt]
                self.set128(rd, sum((((a >> (8 * i)) + (b >> (8 * i))) & 0xFF) << (8 * i) for i in range(16)))
            elif fn == 0x18:                # pipeline-1 multiply into LO1/HI1 (and rd)
                p = self.g32(rs) * self.g32(rt)
                self.lo1, self.hi1 = sx(p, 32) & M64, sx(p >> 32, 32) & M64
                self.set64(rd, self.lo1)
            elif fn == 0x12: self.set64(rd, self.lo1)
            elif fn == 0x10: self.set64(rd, self.hi1)
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
        elif op == 0x2F: pass                                      # cache
        else:
            raise Unsupported(('opcode', hex(pc), hex(op)))
        r[0] = 0

    def call(self, entry, args=(), floats=(), wide=None):
        self.r = [0] * 32
        self.r[28], self.r[29], self.r[31] = 0x27D370, STACK, RET
        for i, v in enumerate(args):
            self.set32(4 + i, v)
        for i, v in (wide or {}).items():
            self.set64(i, v)
        for i, v in enumerate(floats):
            self.f[12 + i] = v
        self.run(entry)
        return self.r[2]

    def in_ranges(self, pc):
        for start, end in self.ranges:
            if start <= pc < end:
                return True
        return False

    def run(self, pc):
        while pc != RET:
            self.steps += 1
            if self.steps > 20_000_000:
                raise AssertionError('runaway')
            w = self.fetch(pc)
            op, rs, rt = w >> 26, w >> 21 & 31, w >> 16 & 31
            target = None
            likely = False
            conditional = False
            if op in (2, 3):
                target = (pc & 0xF0000000) | (w & 0x3FFFFFF) << 2
                if op == 3:
                    self.set64(31, pc + 8)
            elif op == 0 and (w & 63) in (8, 9):
                target = self.u32(rs)
                if w & 63 == 9:
                    self.set64(w >> 11 & 31, pc + 8)
            elif op in (4, 5, 0x14, 0x15):
                conditional = True
                taken = ((self.r[rs] & M64) == (self.r[rt] & M64)) == (op in (4, 0x14))
                likely = op >= 0x14
                target = pc + 4 + (sx(w & 0xFFFF, 16) << 2) if taken else None
            elif op in (6, 7, 0x16, 0x17):
                conditional = True
                v = sx(self.r[rs], 64)
                taken = v <= 0 if op in (6, 0x16) else v > 0
                likely = op >= 0x16
                target = pc + 4 + (sx(w & 0xFFFF, 16) << 2) if taken else None
            elif op == 1:
                conditional = True
                v = sx(self.r[rs], 64)
                if rt > 3:
                    raise Unsupported(('regimm', hex(pc), rt))
                taken = v < 0 if rt in (0, 2) else v >= 0
                likely = rt in (2, 3)
                target = pc + 4 + (sx(w & 0xFFFF, 16) << 2) if taken else None
            elif op == 0x11 and rs == 8:
                conditional = True
                taken = self.cond == (rt & 1)
                likely = bool(rt & 2)
                target = pc + 4 + (sx(w & 0xFFFF, 16) << 2) if taken else None
            else:
                self.execute(w, pc)
                pc += 4
                continue
            if conditional and self.outcomes is not None and self.in_ranges(pc):
                self.outcomes.add((pc, target is not None))
            if target is None:
                if not likely:
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


def restore(undo):
    while undo:
        buf, o, old = undo.pop()
        buf[o:o + len(old)] = old


def read_elf():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA, 'wrong original executable'
    return elf


def elf_ram(elf):
    ram = bytearray(0x2000000)
    phoff = struct.unpack_from('<I', elf, 28)[0]
    size, count = struct.unpack_from('<HH', elf, 42)
    for i in range(count):
        kind, off, va, _, filesz, _memsz, *_ = struct.unpack_from('<8I', elf, phoff + i * size)
        if kind == 1:
            ram[va:va + filesz] = elf[off:off + filesz]
    return ram


# ======================================================================
# The routines
# ======================================================================

TICK, INIT = 0x18A6B0, 0x18A8D0
F0, F1, F1V0, F1V10, F2, F2V0, F4, F4FX = 0x188630, 0x188A50, 0x188AC0, 0x188B80, 0x188DF0, 0x188ED0, 0x18A1F0, 0x189D30
MODEMAP, SPRITE = 0x15D2F0, 0x1CD520
SIZES = {TICK: 0x1CC, INIT: 0x230, F0: 0x41C, F1: 0x68, F1V0: 0xBC, F1V10: 0xE4, F2: 0xD4, F2V0: 0x1BC,
         F4: 0x4B8, F4FX: 0x18C, MODEMAP: 0x170, SPRITE: 0x420}
COPIES = {0x102958, 0x102948, 0x1031E0}          # executed, translated inline
SDK = {0x1026A0: '001026A0', 0x1026D0: '001026D0', 0x1028B8: '001028B8', 0x1028D0: '001028D0',
       0x102760: '00102760', 0x1029C0: '001029C0', 0x102BB0: '00102BB0'}
NODE_WORKERS = {0x1AFC10: '001AFC10', 0x1C62C0: 'bone_init_default_1', 0x1854E0: '001854E0',
                0x185760: '00185760', 0x1861C0: '001861C0', 0x1869A0: '001869A0', 0x186A60: '00186A60',
                0x1872C0: '001872C0', 0x187CC0: '00187CC0', 0x188C70: '00188C70', 0x189090: '00189090',
                0x189330: '00189330', 0x1899C0: '001899C0', 0x189A20: '00189A20', 0x1AA840: '001AA840',
                0x18A180: '0018A180'}
OTHER = {0x1C6120: '001C6120', 0x1CA6E0: '001CA6E0', 0x1C6150: '001C6150', 0x1AF780: '001AF780',
         0x1CB5B0: 'anim_bone_array_setup', 0x1B61C0: '001B61C0', 0x1EFEB0: '001EFEB0',
         0x1F4010: '001F4010', 0x15C310: '0015C310', 0x1C9610: '001C9610', 0x1B0070: '001B0070',
         0x187780: '00187780', 0x19B2C0: '0019B2C0', 0x189EC0: '00189EC0', 0x1F00A0: '001F00A0',
         0x19A570: '0019A570', 0x189FE0: '00189FE0', 0x1EFF10: '001EFF10',
         0x1CD370: '001CD370', 0x1CB5F0: '001CB5F0', 0x1CB6B0: '001CB6B0', 0x1CB900: '001CB900'}
HOOKED = {**SDK, **NODE_WORKERS, **OTHER}


def check_callee_set(elf):
    """Every jal target inside the translated routines is translated here,
    one of the inline copies, or hooked; and every hook is such a target."""
    targets = set()
    for start, size in SIZES.items():
        for pc in range(start, start + size, 4):
            o = pc - 0x100000 + 0x300
            word = int.from_bytes(elf[o:o + 4], 'little')
            if word >> 26 == 3:
                targets.add((word & 0x3FFFFFF) << 2)
    missing = sorted(t for t in targets if t not in HOOKED and t not in SIZES and t not in COPIES)
    assert not missing, ('callees neither hooked nor translated', [hex(t) for t in missing])
    unused = sorted(t for t in HOOKED if t not in targets)
    assert not unused, ('hooks no translated routine calls', [hex(t) for t in unused])
    return len(targets)


# ======================================================================
# The SDK VU0 routines, evaluated by running the original
# ======================================================================

class SdkEval:
    """Runs one original SDK VU0 routine on given inputs in its own
    interpreter (a private RAM image), returning the words it stores."""
    A, B, OUTP = 0x01E00000, 0x01E00100, 0x01E00200

    def __init__(self, elf, ram):
        self.ee = EE(elf, ram, bytearray(0x4000))

    def run(self, entry, a=None, b=None, out_words=4, angle=None):
        ee = self.ee
        if a is not None:
            ee.put_words(self.A, a)
        if b is not None:
            ee.put_words(self.B, b)
        ee.put_words(self.OUTP, [0] * out_words)
        args = {0x1026A0: (self.OUTP, self.A, self.B), 0x1026D0: (self.OUTP, self.A, self.B),
                0x1028B8: (self.OUTP, self.A, self.B), 0x1028D0: (self.OUTP, self.A, self.B),
                0x102760: (self.OUTP, self.A), 0x1029C0: (self.OUTP,),
                0x102BB0: (self.OUTP, self.A)}[entry]
        ee.call(entry, args, floats=(angle,) if angle is not None else ())
        return ee.words(self.OUTP, out_words)


SDK_SHAPE = {  # entry: (a words, b words, out words)
    0x1026A0: (16, 4, 4), 0x1026D0: (16, 16, 16), 0x1028B8: (4, 4, 4), 0x1028D0: (4, 4, 4),
    0x102760: (4, 0, 4), 0x1029C0: (0, 0, 16), 0x102BB0: (16, 0, 16)}


# ======================================================================
# Native side (ctypes)
# ======================================================================

P = C.POINTER
I, U8, U16, I16, U32, I32, VP = C.c_int, C.c_uint8, C.c_uint16, C.c_int16, C.c_uint32, C.c_int32, C.c_void_p
U32P = P(U32)


class Bone(C.Structure):
    _fields_ = [('bind', C.c_float * 16), ('parent', I16), ('rot', C.c_float * 3), ('trans', C.c_float * 3),
                ('scale', I16 * 3), ('world', U32 * 16)]


BoneP = P(Bone)


class Node(C.Structure):
    pass


Node._fields_ = [('status', U8), ('drawn', U8), ('flavour', U8), ('state', U8), ('b05', U8), ('b07', U8),
                 ('bones_held', U8), ('bone_count', U8), ('variant', U8), ('self', P(Node)),
                 ('effect', P(U8)), ('h28', U16), ('h2E', U16), ('model', VP), ('method', U32),
                 ('vA0', U32 * 4), ('vB0', U32 * 4), ('vC0', U32 * 4), ('mD0', U32 * 16),
                 ('bone', BoneP * 56), ('v1F0', U32 * 4), ('w210', U32), ('w214', U32)]
NP = P(Node)


class Hit(C.Structure):
    _fields_ = [('point', U32 * 4), ('record', VP), ('entity', VP)]


class World(C.Structure):
    _fields_ = [('player', P(U8)), ('player_bones', P(BoneP)), ('player_bone_count', U32),
                ('d00275B40', P(BoneP)), ('d00275B40_count', U32),
                ('d008106C6', P(U8)), ('d008106C7', P(U8)), ('d008106CC', P(U8)),
                ('d00810CA4', P(U8)), ('d00810CA6', P(U8)), ('d00275BCC', P(I16)), ('d0028A56C', U32P),
                ('d00248B98', P(I16)), ('d00248C78', P(I16)), ('table', U32P), ('table_words', U32),
                ('spad3600', U32P), ('spad38A0', U32P), ('hit', P(Hit))]


def fn(*args):
    return C.CFUNCTYPE(I, VP, *args)


WORKER_TYPES = [
    ('w_001026A0', fn(U32P, U32P, U32P)), ('w_001026D0', fn(U32P, U32P, U32P)),
    ('w_001028B8', fn(U32P, U32P, U32P)), ('w_001028D0', fn(U32P, U32P, U32P)),
    ('w_00102760', fn(U32P, U32P)), ('w_001029C0', fn(U32P)), ('w_00102BB0', fn(U32P, U32, U32P)),
    ('w_001AFC10', fn(NP)), ('w_method', fn(NP, U32)),
    ('w_001C6120', fn(U32, U32, U32P)), ('w_001CA6E0', fn(NP, U32)), ('w_001C6150', fn(VP, P(U8))),
    ('w_001AF780', fn(P(BoneP))), ('w_anim_bone_array_setup', fn(U8)), ('w_bone_init_default_1', fn(NP)),
    ('w_001854E0', fn(NP)), ('w_00185760', fn(NP)), ('w_001861C0', fn(NP)), ('w_001869A0', fn(NP)),
    ('w_00186A60', fn(NP)), ('w_001872C0', fn(NP)), ('w_00187CC0', fn(NP)),
    ('w_001B61C0', fn(I32, I32, I32, I32)), ('w_001EFEB0', fn(U32, U32P)), ('w_001F4010', fn(I32, U32P)),
    ('w_00188C70', fn(NP)), ('w_0015C310', fn(I32)),
    ('w_00189090', fn(NP)), ('w_00189330', fn(NP)), ('w_001899C0', fn(NP)), ('w_00189A20', fn(NP)),
    ('w_001C9610', fn(P(BoneP), U32, I32, U32P)), ('w_001B0070', fn(U32P)), ('w_00187780', fn(NP, I32, I32)),
    ('w_001AA840', fn(NP)), ('w_0019B2C0', fn(U32P, U32P, I32, P(I32))), ('w_00189EC0', fn(VP, P(I32))),
    ('w_face_record', fn(VP, P(U16), U32P)), ('w_001F00A0', fn(U32, U32P, U32P, I32)),
    ('w_0018A180', fn(NP)), ('w_0019A570', fn(U32P, U32P, I32, I32, P(I32))),
    ('w_00189FE0', fn(NP, U32P, U32P)),
    ('w_001EFF10', fn(U32, BoneP, U32P, U32P, U32P, U32P, U32, P(P(U8)))),
]


class Workers(C.Structure):
    _fields_ = [('ctx', VP)] + WORKER_TYPES


class Fault(C.Structure):
    _fields_ = [('address', U32), ('code', I32)]


class Equip(C.Structure):
    _fields_ = [('workers', P(Workers)), ('world', P(World)), ('fault', P(Fault))]


class SpriteWorld(C.Structure):
    _fields_ = [('fog', U32P), ('s3A40', U32P), ('s3AC0', U32P), ('s3600', U32P)]


SPRITE_TYPES = [('w_001CD370', fn(I32, U32P)), ('w_001CB5F0', fn(U32, I32, I32, P(P(U8)))),
                ('w_001CB6B0', fn(U32, I32, I32, U32)), ('w_001CB900', fn(U32, I32, I32))]


class SpriteWorkers(C.Structure):
    _fields_ = [('ctx', VP)] + SPRITE_TYPES


class Sprite(C.Structure):
    _fields_ = [('workers', P(SpriteWorkers)), ('world', P(SpriteWorld)), ('fault', P(Fault))]


LAYOUT_PROBE = r'''
#include <stddef.h>
#include <stdio.h>
#include "game/em_player_equipment_sprite.h"
#define O(T, f) printf(#T " " #f " %zu\n", offsetof(T, f))
int main(void) {
  O(EmPlayerEquipmentNode, variant); O(EmPlayerEquipmentNode, self); O(EmPlayerEquipmentNode, model);
  O(EmPlayerEquipmentNode, method); O(EmPlayerEquipmentNode, bone); O(EmPlayerEquipmentNode, w214);
  O(EmOwnerBone, world); O(EmPlayerEquipmentHitState, entity);
  O(EmPlayerEquipmentWorld, hit); O(EmPlayerEquipmentWorld, table_words);
  O(EmPlayerEquipmentWorkers, w_001EFF10); O(EmPlayerEquipmentWorkers, w_face_record);
  O(EmPlayerEquipmentWorkers, w_001C9610); O(EmPlayerEquipmentSpriteWorkers, w_001CB900);
  O(EmPlayerEquipmentSpriteWorld, s3600);
  printf("sizes %zu %zu %zu %zu\n", sizeof(EmPlayerEquipmentNode), sizeof(EmPlayerEquipmentWorld),
         sizeof(EmPlayerEquipmentWorkers), sizeof(EmOwnerBone));
  return 0;
}
'''
LAYOUT_NAMES = {'EmPlayerEquipmentNode': Node, 'EmOwnerBone': Bone, 'EmPlayerEquipmentHitState': Hit,
                'EmPlayerEquipmentWorld': World, 'EmPlayerEquipmentWorkers': Workers,
                'EmPlayerEquipmentSpriteWorkers': SpriteWorkers, 'EmPlayerEquipmentSpriteWorld': SpriteWorld}


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / ('equipment.dylib' if sys.platform == 'darwin' else 'equipment.so')
    flags = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-ffp-contract=off', '-Isrc']
    subprocess.run(['cc', *flags, '-shared', '-fPIC', 'src/game/em_player_equipment.c',
                    'src/game/em_player_equipment_sprite.c', '-o', str(lib)], cwd=ROOT, check=True)
    probe_c, probe = OUT / 'layout.c', OUT / 'layout'
    probe_c.write_text(LAYOUT_PROBE)
    subprocess.run(['cc', *flags, str(probe_c), '-o', str(probe)], cwd=ROOT, check=True)
    lines = subprocess.run([str(probe)], check=True, capture_output=True, text=True).stdout.split('\n')
    for line in lines:
        parts = line.split()
        if len(parts) == 3 and parts[0] in LAYOUT_NAMES:
            got = getattr(LAYOUT_NAMES[parts[0]], parts[1]).offset
            assert got == int(parts[2]), ('ctypes layout', parts, got)
        elif parts and parts[0] == 'sizes':
            want = [C.sizeof(Node), C.sizeof(World), C.sizeof(Workers), C.sizeof(Bone)]
            assert [int(x) for x in parts[1:]] == want, ('ctypes sizes', parts, want)
    native = C.CDLL(str(lib))
    E = P(Equip)
    for name in ('em_player_equipment_tick', 'em_player_equipment_00188630', 'em_player_equipment_00188A50',
                 'em_player_equipment_00188AC0', 'em_player_equipment_00188DF0', 'em_player_equipment_00188ED0',
                 'em_player_equipment_0018A1F0', 'em_player_equipment_00189D30'):
        getattr(native, name).argtypes = [E, NP]
    native.em_player_equipment_00188B80.argtypes = [E]
    native.em_player_equipment_0018A8D0.argtypes = [E, NP, I32, P(I32)]
    native.em_player_equipment_0015D2F0.argtypes = [P(U8), P(I32)]
    native.em_player_equipment_001CD520.argtypes = [P(Sprite), I32, I32, U32P, C.c_uint64, U32, U32, U32,
                                                    C.c_uint64, P(I32)]
    return native


# ======================================================================
# Scripted worker effects (identical on both sides)
# ======================================================================

NODE = 0x00680000
PLAYER = 0x008102B0
METHOD = 0x001CAA00
EFFECT_BASE = 0x006A0000
BONE_BASE = 0x006C0000
RECORD_A, RECORD_B, RECORD_C = 0x006E0000, 0x006E0100, 0x006E0200
ENTITY_A = 0x006E8000
MODEL_BASE = 0x006F0000
TABLE_BASE, TABLE_WORDS = 0x24A220, (0x24A4B0 - 0x24A220) // 4
G_C6, G_C7, G_CC, G_CA4, G_CA6 = 0x8106C6, 0x8106C7, 0x8106CC, 0x810CA4, 0x810CA6
G_B40, G_BCC, G_56C, G_B98, G_C78 = 0x275B40, 0x275BCC, 0x28A56C, 0x248B98, 0x248C78
NODE_SLOTS = 56
ALT_ARRAY = 0x006B0000                 # a second bone work array (unit cases)


class Script:
    """Per-call effects keyed on (seed, call index, worker)."""

    def __init__(self, seed, knobs=None):
        self.seed, self.count, self.knobs = seed, 0, knobs or {}

    def next(self, name):
        rng = random.Random('%s:%d:%s' % (self.seed, self.count, name))
        self.count += 1
        k = self.knobs
        e = {'ret': 0}
        if name == '001C6120':
            e['ret'] = 0x00900000 + 4 * rng.randrange(0x4000)
        elif name == '001CA6E0':
            e['model'] = MODEL_BASE + 0x100 * rng.randrange(8) if rng.random() < 0.95 else 0
            e['method'] = METHOD
        elif name == '001C6150':
            e['ret'] = k.get('bones', rng.choice((0, 1, 1, 2, 3, 5)))
        elif name == '001AF780':
            e['ret'] = 0 if rng.random() < k.get('slot0', 0.1) else BONE_BASE + 0x100 * rng.randrange(0x40)
        elif name in ('001854E0', '00185760'):
            if rng.random() < 0.3:
                e['player'] = [(0x275, rng.choice((0, 1, 2, 3, 4, 5)), 1)]
        elif name in ('001861C0', '00187CC0'):
            if k.get('republish') and rng.random() < 0.5:
                e['republish'] = True            # the callee publishes another bone work array
        elif name == '001B0070':
            e['ret'] = rng.choice((0, 0x80, 0x20081910, 0x81, 0x7F))
        elif name == '0019B2C0':
            e['ret'] = rng.choice((0, 1, 1, 2))
            e['hit'] = ([fb(rng.uniform(-500, 500)) for _ in range(3)] + [rng.choice((ONE, 0))],
                        rng.choice((RECORD_A, RECORD_A, RECORD_B, RECORD_C)),
                        rng.choice((0, ENTITY_A)))
        elif name == '00189EC0':
            e['ret'] = rng.choice((0, 0, 1, 2))
        elif name == '0018A180':
            e['status'] = rng.choice((2, 3, 1))
        elif name == '0019A570':
            e['ret'] = rng.choice((0, 1, 2, 3, -1, 0))
        elif name == '00189FE0':
            if rng.random() < 0.4:
                e['spad38'] = [(0xC + i, fb(rng.uniform(-9, 9))) for i in range(4)]
        elif name == '001EFF10':
            e['ret'] = EFFECT_BASE + 0x40 * rng.randrange(8) if rng.random() < 0.9 else 0
        return e


# ======================================================================
# One equipment case: original instructions versus the native module
# ======================================================================

class EquipCase:
    """Runs one entry point on both sides from the same RAM image and
    compares every modelled byte, the worker calls and the result."""

    def __init__(self, native, elf, sdk, ram, spad, undo, seed, knobs=None, node=NODE, outcomes=None):
        self.native, self.elf, self.sdk = native, elf, sdk
        self.ram, self.spad, self.undo = ram, spad, undo
        self.seed, self.knobs, self.node_addr = seed, knobs or {}, node
        self.outcomes = outcomes

    # ---- the original side -------------------------------------------
    def run_original(self, entry, args):
        ee = EE(self.elf, self.ram, self.spad)
        ee.undo = self.undo
        ee.writes = []
        ee.ranges = tuple((s, s + n) for s, n in SIZES.items())
        ee.outcomes = self.outcomes
        log, script = [], Script(self.seed, self.knobs)
        node = self.node_addr

        def nf(address):
            return 'node' if address == node else hex(address)

        def sdk_hook(entry_):
            a_n, b_n, out_n = SDK_SHAPE[entry_]

            def hook(e):
                a = e.words(e.arg(1), a_n) if a_n else None
                b = e.words(e.arg(2), b_n) if b_n else None
                angle = e.f[12] if entry_ == 0x102BB0 else None
                log.append(tuple(x for x in (SDK[entry_], a, b, angle) if x is not None))
                out = self.sdk.run(entry_, a, b, out_n, angle)
                e.put_words(e.arg(0), out)
            return hook
        for entry_ in SDK:
            ee.stubs[entry_] = sdk_hook(entry_)

        def node_hook(name):
            def hook(e):
                log.append((name, nf(e.arg(0))))
                eff = script.next(name)
                for off, v, n in eff.get('player', ()):
                    e.poke(PLAYER + off, v, n)
                if 'status' in eff:
                    e.poke(e.arg(0), eff['status'], 1)
                if eff.get('republish'):
                    e.poke(G_B40, ALT_ARRAY)
                e.r[2] = 0
            return hook
        for a, name in NODE_WORKERS.items():
            ee.stubs[a] = node_hook(name)

        def ret(e, value):
            e.set32(2, value)

        def h_method(e):
            log.append(('method', nf(e.arg(0)), METHOD))
            script.next('method')
        ee.stubs[METHOD] = h_method

        def h_6120(e):
            log.append(('001C6120', e.arg(0), e.arg(1)))
            ret(e, script.next('001C6120')['ret'])

        def h_A6E0(e):
            log.append(('001CA6E0', nf(e.arg(0)), e.arg(1)))
            eff = script.next('001CA6E0')
            e.poke(e.arg(0) + 0x44, eff['model'])
            e.poke(e.arg(0) + 0x4C, eff['method'])

        def h_6150(e):
            log.append(('001C6150', e.arg(0)))
            ret(e, script.next('001C6150')['ret'])

        def h_F780(e):
            log.append(('001AF780',))
            ret(e, script.next('001AF780')['ret'])

        def h_setup(e):
            log.append(('anim_bone_array_setup', e.arg(0)))
            script.next('anim_bone_array_setup')

        def h_61C0(e):
            log.append(('001B61C0', e.g32(4), e.g32(5), e.g32(6), e.g32(7)))
            script.next('001B61C0')

        def h_where16(name):
            def hook(e):
                log.append((name, e.arg(0), ('at', e.arg(1)), e.words(e.arg(1), 16)))
                script.next(name)
            return hook

        def h_C310(e):
            log.append(('0015C310', e.arg(0) == PLAYER, e.g32(5)))
            script.next('0015C310')

        def h_9610(e):
            log.append(('001C9610', e.arg(0) == e.load(G_B40), e.g32(5), e.words(e.arg(2), 16)))
            script.next('001C9610')

        def h_0070(e):
            log.append(('001B0070',))
            ret(e, script.next('001B0070')['ret'])

        def h_7780(e):
            log.append(('00187780', nf(e.arg(0)), e.g32(5), e.g32(6)))
            script.next('00187780')

        def h_B2C0(e):
            log.append(('0019B2C0', e.words(e.arg(0), 4), e.words(e.arg(1), 4), e.g32(6)))
            eff = script.next('0019B2C0')
            point, record, entity = eff['hit']
            e.put_words(0x700031B0, point)
            e.poke(0x700031D0, record)
            e.poke(0x700031D4, entity)
            ret(e, eff['ret'])

        def h_9EC0(e):
            log.append(('00189EC0', e.arg(0)))
            ret(e, script.next('00189EC0')['ret'])

        def h_00A0(e):
            log.append(('001F00A0', e.arg(0), ('at', e.arg(1)), e.words(e.arg(1), 4), ('at', e.arg(2)),
                        e.words(e.arg(2), 4), e.g32(7)))
            script.next('001F00A0')

        def h_A570(e):
            log.append(('0019A570', e.words(e.arg(0), 4), e.words(e.arg(1), 4), e.g32(6), e.g32(7)))
            ret(e, script.next('0019A570')['ret'])

        def h_9FE0(e):
            log.append(('00189FE0', nf(e.arg(0)), e.words(e.arg(1), 4), e.words(e.arg(2), 4)))
            eff = script.next('00189FE0')
            for i, v in eff.get('spad38', ()):
                e.poke(0x700038A0 + 4 * i, v)

        def h_FF10(e):
            ptrs = [e.arg(2), e.arg(3), e.r[8] & M32, e.r[9] & M32]
            log.append(('001EFF10', e.arg(0), e.arg(1) - 0x90,
                        tuple((('at', p), e.words(p, 4)) for p in ptrs), e.f[12]))
            ret(e, script.next('001EFF10')['ret'])

        ee.stubs.update({0x1C6120: h_6120, 0x1CA6E0: h_A6E0, 0x1C6150: h_6150, 0x1AF780: h_F780,
                         0x1CB5B0: h_setup, 0x1B61C0: h_61C0, 0x1EFEB0: h_where16('001EFEB0'),
                         0x1F4010: h_where16('001F4010'), 0x15C310: h_C310, 0x1C9610: h_9610,
                         0x1B0070: h_0070, 0x187780: h_7780, 0x19B2C0: h_B2C0, 0x189EC0: h_9EC0,
                         0x1F00A0: h_00A0, 0x19A570: h_A570, 0x189FE0: h_9FE0, 0x1EFF10: h_FF10})
        v0 = ee.call(entry, args)
        return v0 & M32, log, ee.writes

    # ---- the native side ---------------------------------------------
    def build_native_side(self):
        ram, spad = self.ram, self.spad
        rd = lambda a, n=4: int.from_bytes(ram[a:a + n], 'little')  # noqa: E731
        self.bones, self.bone_addr = {}, {}
        self.effects, self.effect_addr = {}, {}

        def bone(address):
            if address == 0:
                return None
            if address not in self.bones:
                b = Bone()
                for i in range(16):
                    b.world[i] = rd(address + 0x90 + 4 * i)
                self.bones[address] = b
                self.bone_addr[C.addressof(b)] = address
            return self.bones[address]

        def bone_ptr(address):
            b = bone(address)
            return C.pointer(b) if b is not None else BoneP()

        def effect(address):
            if address == 0:
                return P(U8)()
            if address not in self.effects:
                buf = (U8 * 8)(*ram[address:address + 8])
                self.effects[address] = buf
                self.effect_addr[C.addressof(buf)] = address
            return C.cast(self.effects[address], P(U8))
        self.bone_ptr, self.effect_ptr = bone_ptr, effect

        n = Node()
        a = self.node_addr
        for field, off in (('status', 0), ('drawn', 1), ('flavour', 3), ('state', 4), ('b05', 5), ('b07', 7),
                           ('bones_held', 9), ('bone_count', 0xC), ('variant', 0xD)):
            setattr(n, field, ram[a + off])
        assert rd(a + 0x14) == a, 'node +0x14 is not the record itself'
        n.self = C.pointer(n)
        n.effect = effect(rd(a + 0x20))
        n.h28, n.h2E = rd(a + 0x28, 2), rd(a + 0x2E, 2)
        n.model = rd(a + 0x44) or None
        n.method = rd(a + 0x4C)
        for name, off, count in (('vA0', 0xA0, 4), ('vB0', 0xB0, 4), ('vC0', 0xC0, 4), ('mD0', 0xD0, 16),
                                 ('v1F0', 0x1F0, 4)):
            arr = getattr(n, name)
            for i in range(count):
                arr[i] = rd(a + off + 4 * i)
        for i in range(NODE_SLOTS):
            n.bone[i] = bone_ptr(rd(a + 0x110 + 4 * i))
        n.w210, n.w214 = rd(a + 0x210), rd(a + 0x214)
        self.n = n

        self.player = (U8 * 0x320)(*ram[PLAYER:PLAYER + 0x320])
        self.player_bones = (BoneP * NODE_SLOTS)(*[bone_ptr(rd(PLAYER + 0x110 + 4 * i)) for i in range(NODE_SLOTS)])
        array = rd(G_B40)
        self.view = (BoneP * NODE_SLOTS)(*[bone_ptr(rd(array + 4 * i)) for i in range(NODE_SLOTS)])
        if self.knobs.get('republish'):
            self.view_alt = (BoneP * NODE_SLOTS)(*[bone_ptr(rd(ALT_ARRAY + 4 * i)) for i in range(NODE_SLOTS)])
        self.g = {k: U8(ram[k]) for k in (G_C6, G_C7, G_CC, G_CA4, G_CA6)}
        self.bcc = I16(sx(rd(G_BCC, 2), 16))
        self.bank = U32(rd(G_56C))
        self.b98, self.c78 = I16(sx(rd(G_B98, 2), 16)), I16(sx(rd(G_C78, 2), 16))
        self.table = (U32 * TABLE_WORDS)(*[rd(TABLE_BASE + 4 * i) for i in range(TABLE_WORDS)])
        sw = lambda at, k: [int.from_bytes(spad[at + 4 * i:at + 4 * i + 4], 'little') for i in range(k)]  # noqa: E731
        self.s3600 = (U32 * 0x48)(*sw(0x3600, 0x48))
        self.s38A0 = (U32 * 0x10)(*sw(0x38A0, 0x10))
        self.hit = Hit()
        for i, v in enumerate(sw(0x31B0, 4)):
            self.hit.point[i] = v
        self.hit.record = sw(0x31D0, 1)[0] or None
        self.hit.entity = sw(0x31D4, 1)[0] or None
        w = World()
        w.player = C.cast(self.player, P(U8))
        w.player_bones, w.player_bone_count = C.cast(self.player_bones, P(BoneP)), NODE_SLOTS
        w.d00275B40, w.d00275B40_count = C.cast(self.view, P(BoneP)), NODE_SLOTS
        w.d008106C6, w.d008106C7, w.d008106CC = (C.pointer(self.g[k]) for k in (G_C6, G_C7, G_CC))
        w.d00810CA4, w.d00810CA6 = C.pointer(self.g[G_CA4]), C.pointer(self.g[G_CA6])
        w.d00275BCC, w.d0028A56C = C.pointer(self.bcc), C.pointer(self.bank)
        w.d00248B98, w.d00248C78 = C.pointer(self.b98), C.pointer(self.c78)
        w.table, w.table_words = C.cast(self.table, U32P), TABLE_WORDS
        w.spad3600, w.spad38A0 = C.cast(self.s3600, U32P), C.cast(self.s38A0, U32P)
        w.hit = C.pointer(self.hit)
        self.world = w

    def where(self, ptr):
        """A native pointer handed to a worker, as the original address."""
        at = C.cast(ptr, VP).value
        for base, arr, words in ((0x70003600, self.s3600, 0x48), (0x700038A0, self.s38A0, 0x10)):
            start = C.addressof(arr)
            if start <= at < start + 4 * words:
                return base + (at - start)
        raise AssertionError(('pointer outside the owned storage', hex(at)))

    def run_native(self, entry, args):
        self.build_native_side()
        log, script = [], Script(self.seed, self.knobs)
        ram = self.ram
        n = self.n
        keep = []

        def words(ptr, k):
            return tuple(ptr[i] for i in range(k))

        def nflag(p):
            return 'node' if C.cast(p, VP).value == C.addressof(n) else 'other'

        def sdk(entry_):
            a_n, b_n, out_n = SDK_SHAPE[entry_]
            name = SDK[entry_]
            if entry_ in (0x102760,):
                def f(_, a, out):
                    va = words(a, a_n)
                    log.append((name, va))
                    for i, v in enumerate(self.sdk.run(entry_, va, None, out_n)):
                        out[i] = v
                    return 0
            elif entry_ == 0x1029C0:
                def f(_, out):
                    log.append((name,))
                    for i, v in enumerate(self.sdk.run(entry_, None, None, out_n)):
                        out[i] = v
                    return 0
            elif entry_ == 0x102BB0:
                def f(_, a, angle, out):
                    va = words(a, a_n)
                    log.append((name, va, angle))
                    for i, v in enumerate(self.sdk.run(entry_, va, None, out_n, angle)):
                        out[i] = v
                    return 0
            else:
                def f(_, a, b, out):
                    va, vb = words(a, a_n), words(b, b_n)
                    log.append((name, va, vb))
                    for i, v in enumerate(self.sdk.run(entry_, va, vb, out_n)):
                        out[i] = v
                    return 0
            return f

        def node_worker(name):
            def f(_, p):
                log.append((name, nflag(p)))
                eff = script.next(name)
                for off, v, k in eff.get('player', ()):
                    for i in range(k):
                        self.player[off + i] = (v >> (8 * i)) & 0xFF
                if 'status' in eff:
                    p[0].status = eff['status']
                if eff.get('republish'):
                    self.world.d00275B40 = C.cast(self.view_alt, P(BoneP))
                return 0
            return f

        def method(_, p, m):
            log.append(('method', nflag(p), m))
            script.next('method')
            return 0

        def w6120(_, bank, ident, out):
            log.append(('001C6120', bank, ident))
            out[0] = script.next('001C6120')['ret'] & M32
            return 0

        def wA6E0(_, p, handle):
            log.append(('001CA6E0', nflag(p), handle))
            eff = script.next('001CA6E0')
            p[0].model = eff['model'] or None
            p[0].method = eff['method']
            return 0

        def w6150(_, model, out):
            log.append(('001C6150', model or 0))
            out[0] = script.next('001C6150')['ret'] & 0xFF
            return 0

        def wF780(_, out):
            log.append(('001AF780',))
            address = script.next('001AF780')['ret']
            out[0] = self.bone_ptr(address)
            return 0

        def wsetup(_, count):
            log.append(('anim_bone_array_setup', count))
            script.next('anim_bone_array_setup')
            return 0

        def w61C0(_, a, b, c, d):
            log.append(('001B61C0', a, b, c, d))
            script.next('001B61C0')
            return 0

        def where16(name):
            def f(_, first, at):
                log.append((name, first & M32, ('at', self.where(at)), words(at, 16)))
                script.next(name)
                return 0
            return f

        def wC310(_, arg1):
            log.append(('0015C310', True, arg1))
            script.next('0015C310')
            return 0

        def w9610(_, bones, count_view, count, m):
            current = C.cast(self.world.d00275B40, VP).value
            same = C.cast(bones, VP).value == current and count_view == NODE_SLOTS
            log.append(('001C9610', same, count, words(m, 16)))
            script.next('001C9610')
            return 0

        def w0070(_, out):
            log.append(('001B0070',))
            out[0] = script.next('001B0070')['ret'] & M32
            return 0

        def w7780(_, p, a1, a2):
            log.append(('00187780', nflag(p), a1, a2))
            script.next('00187780')
            return 0

        def wB2C0(_, a, b, mask, out):
            log.append(('0019B2C0', words(a, 4), words(b, 4), mask))
            eff = script.next('0019B2C0')
            point, record, entity = eff['hit']
            for i in range(4):
                self.hit.point[i] = point[i]
            self.hit.record = record or None
            self.hit.entity = entity or None
            out[0] = eff['ret']
            return 0

        def w9EC0(_, entity, out):
            log.append(('00189EC0', entity or 0))
            out[0] = script.next('00189EC0')['ret']
            return 0

        def wface(_, record, h1A, normal):
            address = record or 0
            h1A[0] = int.from_bytes(ram[address + 0x1A:address + 0x1C], 'little')
            for i in range(3):
                normal[i] = int.from_bytes(ram[address + 0x24 + 4 * i:address + 0x28 + 4 * i], 'little')
            return 0

        def w00A0(_, ident, a, b, a3):
            log.append(('001F00A0', ident, ('at', self.where(a)), words(a, 4), ('at', self.where(b)),
                        words(b, 4), a3))
            script.next('001F00A0')
            return 0

        def wA570(_, a, b, mask, ident, out):
            log.append(('0019A570', words(a, 4), words(b, 4), mask, ident))
            out[0] = script.next('0019A570')['ret']
            return 0

        def w9FE0(_, p, a, b):
            log.append(('00189FE0', nflag(p), words(a, 4), words(b, 4)))
            eff = script.next('00189FE0')
            for i, v in eff.get('spad38', ()):
                self.s38A0[i] = v
            return 0

        def wFF10(_, ident, bone_p, a, b, c, d, f12, out):
            address = self.bone_addr[C.cast(bone_p, VP).value]
            log.append(('001EFF10', ident, address,
                        tuple((('at', self.where(p)), words(p, 4)) for p in (a, b, c, d)), f12))
            out[0] = self.effect_ptr(script.next('001EFF10')['ret'])
            return 0

        impl = {'w_001AFC10': node_worker('001AFC10'), 'w_method': method, 'w_001C6120': w6120,
                'w_001CA6E0': wA6E0, 'w_001C6150': w6150, 'w_001AF780': wF780,
                'w_anim_bone_array_setup': wsetup, 'w_bone_init_default_1': node_worker('bone_init_default_1'),
                'w_001B61C0': w61C0, 'w_001EFEB0': where16('001EFEB0'), 'w_001F4010': where16('001F4010'),
                'w_0015C310': wC310, 'w_001C9610': w9610, 'w_001B0070': w0070, 'w_00187780': w7780,
                'w_0019B2C0': wB2C0, 'w_00189EC0': w9EC0, 'w_face_record': wface, 'w_001F00A0': w00A0,
                'w_0019A570': wA570, 'w_00189FE0': w9FE0, 'w_001EFF10': wFF10}
        for name in NODE_WORKERS.values():
            impl.setdefault('w_' + name, node_worker(name))
        for entry_, name in SDK.items():
            impl['w_' + name] = sdk(entry_)
        errors = self.errors = []

        def guard(f):
            def g(*a):
                try:
                    return f(*a)
                except BaseException as exc:            # a callback must never fail silently
                    errors.append(repr(exc))
                    return -1
            return g
        workers = Workers()
        for field, kind in WORKER_TYPES:
            cb = kind(guard(impl[field]))
            keep.append(cb)
            setattr(workers, field, cb)
        self.keep = keep
        fault = Fault()
        equip = Equip(C.pointer(workers), C.pointer(self.world), C.pointer(fault))
        result = I32(-7)
        if entry == TICK:
            rc = self.native.em_player_equipment_tick(C.byref(equip), C.byref(n))
        elif entry == INIT:
            rc = self.native.em_player_equipment_0018A8D0(C.byref(equip), C.byref(n), args[1], C.byref(result))
        elif entry == F1V10:
            rc = self.native.em_player_equipment_00188B80(C.byref(equip))
        else:
            name = {F0: '00188630', F1: '00188A50', F1V0: '00188AC0', F2: '00188DF0', F2V0: '00188ED0',
                    F4: '0018A1F0', F4FX: '00189D30'}[entry]
            rc = getattr(self.native, 'em_player_equipment_' + name)(C.byref(equip), C.byref(n))
        return rc, result.value, log, fault

    # ---- comparison ----------------------------------------------------
    def compare(self, entry, args, where):
        """Runs the native side (it only reads the RAM image), then the
        original over the same image, and compares. The caller restores
        the image from self.undo. Returns 'ok' or 'unsupported'."""
        rc, result, nlog, fault = self.run_native(entry, args)
        assert not self.errors, (where, 'native-side worker raised', self.errors)
        try:
            v0, olog, writes = self.run_original(entry, args)
        except Unsupported:
            return 'unsupported'
        assert rc == 0 and fault.code == 0, (where, 'native faulted', rc, hex(fault.address), fault.code)
        assert nlog == olog, (where, 'worker calls differ', first_diff(nlog, olog))
        if entry == INIT:
            assert result == sx(v0, 32), (where, 'return', result, v0)
        self.check_state(where, writes)
        return 'ok'

    def check_state(self, where, writes):
        ram, spad, n, a = self.ram, self.spad, self.n, self.node_addr
        rd = lambda x, k=4: int.from_bytes(ram[x:x + k], 'little')  # noqa: E731
        covered = set()

        def cover(start, size):
            covered.update(range(start, start + size))
        for field, off in (('status', 0), ('drawn', 1), ('flavour', 3), ('state', 4), ('b05', 5), ('b07', 7),
                           ('bones_held', 9), ('bone_count', 0xC), ('variant', 0xD)):
            assert getattr(n, field) == ram[a + off], (where, field, getattr(n, field), ram[a + off])
            cover(a + off, 1)
        eff = C.cast(n.effect, VP).value
        assert (self.effect_addr[eff] if eff else 0) == rd(a + 0x20), (where, 'effect pointer')
        assert n.h28 == rd(a + 0x28, 2) and n.h2E == rd(a + 0x2E, 2), (where, 'h28/h2E', n.h28, n.h2E)
        cover(a + 0x20, 4), cover(a + 0x28, 2), cover(a + 0x2E, 2)
        assert (n.model or 0) == rd(a + 0x44) and n.method == rd(a + 0x4C), (where, 'model/method')
        for name, off, count in (('vA0', 0xA0, 4), ('vB0', 0xB0, 4), ('vC0', 0xC0, 4), ('mD0', 0xD0, 16),
                                 ('v1F0', 0x1F0, 4)):
            got = list(getattr(n, name))
            want = [rd(a + off + 4 * i) for i in range(count)]
            assert got == want, (where, name, [hex(x) for x in got], [hex(x) for x in want])
            cover(a + off, 4 * count)
        assert (n.w210, n.w214) == (rd(a + 0x210), rd(a + 0x214)), (where, 'w210/w214')
        cover(a + 0x210, 8)
        for i in range(NODE_SLOTS):
            p = C.cast(n.bone[i], VP).value
            assert (self.bone_addr[p] if p else 0) == rd(a + 0x110 + 4 * i), (where, 'bone slot', i)
        cover(a + 0x110, 4 * NODE_SLOTS)
        for address, b in self.bones.items():
            want = [rd(address + 0x90 + 4 * i) for i in range(16)]
            assert list(b.world) == want, (where, 'bone world', hex(address))
            cover(address + 0x90, 64)
        for address, buf in self.effects.items():
            assert bytes(buf) == bytes(ram[address:address + 8]), (where, 'effect record', hex(address))
            cover(address + 4, 1)
        for k, v in self.g.items():
            assert v.value == ram[k], (where, 'global', hex(k), v.value, ram[k])
        cover(G_C6, 1), cover(G_C7, 1), cover(G_CC, 1)
        assert bytes(self.player) == bytes(ram[PLAYER:PLAYER + 0x320]), (where, 'player record')
        for base, arr, k in ((0x3600, self.s3600, 0x48), (0x38A0, self.s38A0, 0x10)):
            want = [int.from_bytes(spad[base + 4 * i:base + 4 * i + 4], 'little') for i in range(k)]
            assert list(arr) == want, (where, 'spad', hex(base), first_diff(list(arr), want))
            cover(0x70000000 + base, 4 * k)
        assert list(self.hit.point) == [int.from_bytes(spad[0x31B0 + 4 * i:0x31B4 + 4 * i], 'little')
                                        for i in range(4)], (where, 'hit point')
        for address, size in writes:
            if STACK_LOW <= address < STACK:
                continue
            for x in range(address, address + size):
                assert x in covered, (where, 'original writes a byte the test does not compare', hex(x))


def first_diff(a, b):
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return i, x, y
    return len(a), len(b)


# ======================================================================
# Unit cases (the ELF's RAM image plus synthetic records)
# ======================================================================

class Setup:
    """Journaled writes into the shared RAM image and scratchpad."""

    def __init__(self, ram, spad, undo):
        self.ee = EE(None, ram, spad)
        self.ee.undo = undo

    def put(self, a, v, n=4):
        self.ee.poke(a, v, n)

    def words(self, a, values):
        for i, v in enumerate(values):
            self.ee.poke(a + 4 * i, v)


def rfloat(rng, lo=-200.0, hi=200.0):
    return fb(rng.uniform(lo, hi))


def unit_setup(rng, st, entry, elf_ram):
    """Randomised node, player, globals and scratchpad for one case. The
    rare conditions of each entry are weighted up so every branch outcome
    is reached in the quick sample."""
    flavour = rng.choice((0, 1, 2, 4, 0, 1, 2, 4, 3, 5))
    if entry in (F1, F1V0, F1V10):
        flavour = 1
    elif entry in (F2, F2V0):
        flavour = 2
    elif entry in (F4, F4FX):
        flavour = 4
    elif entry == F0:
        flavour = 0
    variant = {1: rng.choice((0, 0x10, 0x15, 3)),
               2: rng.choice((0, 0, 1, 2, 3, 5, 7, 8, 9, 10, 11, 12, 0xC, 13, 0x20))}.get(
        flavour, rng.choice((0, 5, 0x10)))
    if entry == F1V10:
        variant = 0x10
    elif entry == F2V0:
        variant = 0
    for a in range(NODE, NODE + 0x2F0, 4):
        st.put(a, rng.getrandbits(32))
    count = rng.choice((0, 1, 1, 2, 3, 5))
    st.put(NODE + 0x0, rng.choice((1, 3, 1, 2)) if flavour == 4 else rng.choice((2, 2, 1, 3, 0)), 1)
    st.put(NODE + 0x1, rng.choice((0, 1)), 1)
    st.put(NODE + 0x3, flavour, 1)
    st.put(NODE + 0x4, rng.choice((0, 1, 1, 1, 1, 2, 3, 4)), 1)
    st.put(NODE + 0x5, rng.choice((0, 1, 2)), 1)
    st.put(NODE + 0x7, rng.choice((0, 1, 1, 2)), 1)
    st.put(NODE + 0xC, count, 1)
    st.put(NODE + 0xD, variant, 1)
    st.put(NODE + 0x14, NODE)
    st.put(NODE + 0x20, EFFECT_BASE + 0x40 * rng.randrange(8))   # 0 only in run_faults
    st.put(NODE + 0x2E, rng.choice((0, 1, 2)), 2)
    st.put(NODE + 0x44, MODEL_BASE + 0x100 * rng.randrange(8))
    st.put(NODE + 0x4C, METHOD)
    for i in range(NODE_SLOTS):
        valid = i < max(count, 1) or rng.random() < 0.3
        st.put(NODE + 0x110 + 4 * i, BONE_BASE + 0x100 * rng.randrange(0x20) if valid else 0)
    for b in range(0x40):
        base = BONE_BASE + 0x100 * b
        st.words(base + 0x90, [rfloat(rng) for _ in range(16)])
    st.put(G_B40, NODE + 0x110 if rng.random() < 0.9 else ALT_ARRAY)
    st.words(ALT_ARRAY, [BONE_BASE + 0x100 * rng.randrange(0x20) for _ in range(NODE_SLOTS)])
    # player record
    for a in range(PLAYER, PLAYER + 0x320, 4):
        st.put(a, rng.getrandbits(32))
    b98 = int.from_bytes(elf_ram[G_B98:G_B98 + 2], 'little')
    c78 = int.from_bytes(elf_ram[G_C78:G_C78 + 2], 'little')
    focus_b80 = flavour == 1 and variant == 0x10 and rng.random() < 0.7
    st.put(PLAYER + 0x1, rng.choice((0, 0, 1)), 1)
    st.put(PLAYER + 0x4, 1 if focus_b80 else rng.choice((1, 1, 2, 0)), 1)
    st.put(PLAYER + 0x5, rng.choice((0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x17, 0x18, 0x19, 0x1C)), 1)
    st.put(PLAYER + 0x1F0, 0x33 if focus_b80 else rng.choice((0x33, 0x33, 0x31, 0x32, 0x34, 0x35, 0x36, 0x37, 0)), 1)
    st.put(PLAYER + 0x1F1, rng.choice((0, 1, 1, 2)), 1)
    st.put(PLAYER + 0x20C, rng.choice((b98, c78, 0x1234)), 2)
    st.put(PLAYER + 0x3C, rng.choice((fb(11.99), fb(12.0), fb(30.0), fb(51.0), fb(51.01), fb(-1.0),
                                      0x7F800000, 0xFF800000, 0x7FC00000)))
    st.put(PLAYER + 0x230, rng.choice((0xC, 0xC, 0, 0xD)))
    st.put(PLAYER + 0x275, 0 if focus_b80 else rng.choice((0, 0, 1, 2, 3, 4, 5, 6, 7)), 1)
    st.put(PLAYER + 0x2F2, rng.choice((0, 1)), 1)
    st.put(PLAYER + 0x318, rng.choice((0, 1, 2, 3)), 1)
    if flavour == 0 and rng.random() < 0.4:          # the 001854E0 / 00185760 selector
        st.put(PLAYER + 0x1F0, rng.choice((0x31, 0x32, 0x34, 0x35)), 1)
        st.put(PLAYER + 0x1F1, 1, 1)
        st.put(PLAYER + 0x2F2, rng.choice((0, 1, 1)), 1)
    if flavour == 2 and rng.random() < 0.5:          # the 00188ED0 lamp gate
        st.put(PLAYER + 0x4, rng.choice((1, 2, 2)), 1)
        st.put(PLAYER + 0x5, rng.choice((0x17, 0x18, 0x19, 0x1D, 0x1E, 0x20, 0x21)), 1)
        st.put(PLAYER + 0x1F0, rng.choice((0x31, 0x34, 0x33)), 1)
    if flavour == 0 and rng.random() < 0.5:          # every mode's one-shot, pending or not
        st.put(PLAYER + 0x275, rng.choice((0, 1, 2, 3, 4, 5)), 1)
        st.put(NODE + 0x2E, rng.choice((0, 1, 2)), 2)
    st.words(PLAYER + 0xB0, [rfloat(rng) for _ in range(3)] + [ONE])
    st.words(PLAYER + 0x2A0, [rfloat(rng, -2, 2) for _ in range(16)])
    for i in range(NODE_SLOTS):
        st.put(PLAYER + 0x110 + 4 * i, BONE_BASE + 0x100 * (0x20 + i) if i < 20 else 0)
    # globals
    st.put(G_C6, rng.choice((0, 1, 3)), 1)
    st.put(G_C7, rng.choice((0, 1)), 1)
    st.put(G_CC, rng.choice((0, 1)) if flavour != 2 else rng.choice((0, 1, 1)), 1)
    st.put(G_CA4, rng.choice((0, 1, 2, 0xFF)), 1)
    st.put(G_CA6, rng.choice((0, 4, 7)), 1)
    if flavour == 2 and rng.random() < 0.3:          # 0018A6B0's equipment-change arm
        st.put(G_CC, 1, 1)
        st.put(G_C7, rng.choice((0, 1, 1)), 1)
        st.put(NODE + 0xD, 0, 1)
    st.put(G_BCC, rng.choice((0, 2, 40, 200)), 2)
    st.put(G_56C, rng.getrandbits(32))
    # scratchpad windows and hit records
    st.words(0x70003600, [rng.getrandbits(32) for _ in range(0x48)])
    st.words(0x700038A0, [rng.getrandbits(32) for _ in range(0x10)])
    st.words(0x700031B0, [rfloat(rng) for _ in range(4)])
    st.put(0x700031D0, rng.choice((RECORD_A, RECORD_B, RECORD_C)))
    st.put(0x700031D4, rng.choice((0, ENTITY_A)))
    for rec, kind in ((RECORD_A, 0x2000 | rng.randrange(256)), (RECORD_B, 0x8020), (RECORD_C, 0x1F00)):
        st.put(rec + 0x1A, kind, 2)
        st.words(rec + 0x24, [rfloat(rng, -1, 1) for _ in range(3)])
    for k in range(8):
        st.words(EFFECT_BASE + 0x40 * k, [rng.getrandbits(32) for _ in range(2)])
    args = (NODE,)
    if entry == INIT:
        args = (NODE, rng.choice((0, 0x44, 0x7FFF)))
    return args


ENTRIES = (TICK, TICK, TICK, TICK, INIT, F0, F1, F1V0, F1V10, F2, F2V0, F4, F4FX)


def run_unit(native, elf, sdk, ram, spad, seeds, outcomes):
    counts = {'ok': 0, 'unsupported': 0}
    elf_ram = ram
    for seed in seeds:
        rng = random.Random(seed)
        entry = ENTRIES[seed % len(ENTRIES)]
        undo = []
        st = Setup(ram, spad, undo)
        args = unit_setup(rng, st, entry, elf_ram)
        knobs = {}
        if rng.random() < 0.3:
            knobs['bones'] = rng.choice((0, 1, 4, 30))
        if rng.random() < 0.3:
            knobs['republish'] = True
        case = EquipCase(native, elf, sdk, ram, spad, undo, seed, knobs, outcomes=outcomes)
        try:
            counts[case.compare(entry, args, ('unit', seed, hex(entry)))] += 1
        finally:
            restore(undo)
    return counts


def run_faults(native, elf, sdk, ram, spad):
    """The fail-stop contract: a missing worker faults before any write;
    an index outside a view faults."""
    rng = random.Random(99)
    undo = []
    st = Setup(ram, spad, undo)
    unit_setup(rng, st, TICK, ram)
    case = EquipCase(native, elf, sdk, ram, spad, undo, 99)
    case.build_native_side()
    before = bytes(case.n)
    fault = Fault()
    empty = Workers()
    equip = Equip(C.pointer(empty), C.pointer(case.world), C.pointer(fault))
    rc = native.em_player_equipment_tick(C.byref(equip), C.byref(case.n))
    assert rc == -1 and fault.code == 1 and bytes(case.n) == before, ('missing workers', rc, fault.code)
    rc = native.em_player_equipment_tick(C.byref(equip), C.byref(case.n))
    assert rc == -1, 'a latched fault must refuse later calls'
    # mode byte 255 reads past the table window: the native faults (BAD_INDEX)
    st.put(NODE + 0x3, 0, 1)
    st.put(NODE + 0x4, 1, 1)
    st.put(PLAYER + 0x275, 255, 1)
    case = EquipCase(native, elf, sdk, ram, spad, undo, 98)
    rc, _, _, fault = case.run_native(TICK, (NODE,))
    assert rc == -1 and fault.code == 4, ('table window', rc, fault.code)
    # 00189D30 step 1 with no effect record: the original writes address 4;
    # the native faults (BAD_INDEX) instead
    st.put(NODE + 0x3, 4, 1)
    st.put(NODE + 0x0, 2, 1)
    st.put(NODE + 0x7, 1, 1)
    st.put(NODE + 0x20, 0)
    case = EquipCase(native, elf, sdk, ram, spad, undo, 97)
    rc, _, _, fault = case.run_native(TICK, (NODE,))
    assert rc == -1 and fault.code == 4 and fault.address == 0x189D30, ('effect record', rc, fault.code)
    restore(undo)
    return 4


# ======================================================================
# Route cases: the seven live nodes of every capture
# ======================================================================

def route_beats():
    return sorted(p for p in ROUTE.iterdir() if (p / 'eeMemory.bin').exists() and RM.in_scope_beat(p.name)) if ROUTE.exists() else []


def run_route_beat(beat):
    elf = read_elf()
    native = build_native_loaded()
    ram = bytearray((beat / 'eeMemory.bin').read_bytes())
    spad = bytearray((beat / 'scratchpad.bin').read_bytes())
    sdk = SdkEval(elf, elf_ram(elf))
    counts = {'ok': 0, 'unsupported': 0, 'nodes': 0, 'flavours': []}
    for i in range(0x100):
        a = 0x7A5640 + i * 0x2F0
        if int.from_bytes(ram[a + 0x10:a + 0x14], 'little') != TICK or ram[a] == 0:
            continue
        counts['nodes'] += 1
        counts['flavours'].append((ram[a + 3], ram[a + 0xD], ram[a + 4]))
        assert int.from_bytes(ram[a + 0x4C:a + 0x50], 'little') == METHOD, (beat.name, hex(a), 'method')
        for variant in ('tick', 'init'):
            undo = []
            st = Setup(ram, spad, undo)
            st.put(G_B40, a + 0x110)            # 001AFD70 publishes the node's slots before the call
            if variant == 'init':
                st.put(a + 4, 0, 1)
            case = EquipCase(native, elf, sdk, ram, spad, undo, '%s:%x:%s' % (beat.name, a, variant), node=a)
            try:
                counts[case.compare(TICK, (a,), (beat.name, hex(a), variant))] += 1
            finally:
                restore(undo)
    return beat.name, counts


_NATIVE = None


def build_native_loaded():
    global _NATIVE
    if _NATIVE is None:
        _NATIVE = build_native()
    return _NATIVE


# ======================================================================
# 001CD520: the sprite
# ======================================================================

CTX_UNIT = 0x01E10000
POINT = 0x01E20000
PKT = 0x01E30000


def sprite_case(native, elf, ram, spad, undo, params, outcomes, where):
    """params: bucket, mode, point(4), giftag, w, h, zbias, rgba (64-bit)."""
    bucket, mode, point, giftag, w, h, zbias, rgba = params
    st = Setup(ram, spad, undo)
    st.words(POINT, point)
    fill = random.Random(str(where)).getrandbits(8 * 0x60).to_bytes(0x60, 'little')
    for i in range(0, 0x60, 4):
        st.put(PKT + i, int.from_bytes(fill[i:i + 4], 'little'))
    ctx = int.from_bytes(ram[0x275670:0x275674], 'little')
    rw = lambda a, k: [int.from_bytes(ram[a + 4 * i:a + 4 * i + 4], 'little') for i in range(k)]  # noqa: E731
    sw = lambda a, k: [int.from_bytes(spad[a + 4 * i:a + 4 * i + 4], 'little') for i in range(k)]  # noqa: E731
    fog = (U32 * 4)(*rw(ctx + 0xA0, 4))
    s3A40 = (U32 * 16)(*sw(0x3A40, 16))
    s3AC0 = (U32 * 16)(*sw(0x3AC0, 16))
    s3600 = (U32 * 8)(*sw(0x3600, 8))
    cull = rw(ctx + 0x2240, 16)
    packet = (U8 * 0x60)(*fill)
    nlog = []

    def w370(_, a0, out):
        nlog.append(('001CD370', a0))
        for i in range(16):
            out[i] = cull[i]
        return 0

    def w5F0(_, chain, z, count, out):
        nlog.append(('001CB5F0', chain, z, count))
        out[0] = C.cast(packet, P(U8))
        return 0

    def w6B0(_, chain, z, kind, address):
        nlog.append(('001CB6B0', chain, z, kind, address))
        return 0

    def w900(_, chain, z, m):
        nlog.append(('001CB900', chain, z, m))
        return 0
    errors = []

    def guard(f):
        def g(*a):
            try:
                return f(*a)
            except BaseException as exc:
                errors.append(repr(exc))
                return -1
        return g
    cbs = [kind(guard(f)) for (_, kind), f in zip(SPRITE_TYPES, (w370, w5F0, w6B0, w900))]
    workers = SpriteWorkers(None, *cbs)
    world = SpriteWorld(C.cast(fog, U32P), C.cast(s3A40, U32P), C.cast(s3AC0, U32P), C.cast(s3600, U32P))
    fault = Fault()
    sprite = Sprite(C.pointer(workers), C.pointer(world), C.pointer(fault))
    result = I32(-7)
    pt = (U32 * 4)(*point)
    rc = native.em_player_equipment_001CD520(C.byref(sprite), bucket, mode, pt, giftag, w, h, zbias, rgba,
                                             C.byref(result))
    assert not errors, (where, 'native-side worker raised', errors)

    ee = EE(elf, ram, spad)
    ee.undo = undo
    ee.writes = []
    ee.ranges = ((SPRITE, SPRITE + SIZES[SPRITE]),)
    ee.outcomes = outcomes
    olog = []

    def h370(e):
        olog.append(('001CD370', e.g32(4)))
        e.set32(2, ctx + 0x2240)

    def h5F0(e):
        olog.append(('001CB5F0', e.arg(0), e.g32(5), e.g32(6)))
        e.set32(2, PKT)

    def h6B0(e):
        olog.append(('001CB6B0', e.arg(0), e.g32(5), e.g32(6), e.arg(3)))

    def h900(e):
        olog.append(('001CB900', e.arg(0), e.g32(5), e.g32(6)))
    ee.stubs.update({0x1CD370: h370, 0x1CB5F0: h5F0, 0x1CB6B0: h6B0, 0x1CB900: h900})
    try:
        v0 = ee.call(SPRITE, (bucket, mode, POINT, 0), floats=(w, h, zbias), wide={7: giftag, 8: rgba})
    except Unsupported:
        assert rc == -1 and fault.code == 6, (where, 'native must refuse the unmeasured clip', rc, fault.code)
        return 'unsupported'
    assert rc == 0 and fault.code == 0, (where, 'native faulted', rc, hex(fault.address), fault.code)
    assert result.value == sx(v0, 32), (where, 'return', result.value, sx(v0, 32))
    assert nlog == olog, (where, 'calls', nlog, olog)
    assert list(s3600) == sw(0x3600, 8), (where, 'spad 3600', [hex(x) for x in s3600],
                                          [hex(x) for x in sw(0x3600, 8)])
    assert bytes(packet) == bytes(ram[PKT:PKT + 0x60]), (where, 'packet')
    for address, size in ee.writes:
        if STACK_LOW <= address < STACK:
            continue
        ok = 0x70003600 <= address and address + size <= 0x70003620 or PKT <= address and address + size <= PKT + 0x60
        assert ok, (where, 'original writes outside the compared set', hex(address))
    return 'culled' if result.value == 0x00FFFFFF else 'drawn'


def sprite_params(rng, spad, ram, player_view=True):
    """Parameters around the captured camera: points ahead of it (drawn) and
    random ones (often culled)."""
    base = [int.from_bytes(ram[PLAYER + 0xB0 + 4 * i:PLAYER + 0xB4 + 4 * i], 'little') for i in range(3)]
    if player_view and rng.random() < 0.7:
        point = [fb(FM.b2f(base[i]) + rng.uniform(-30, 30) + (20 if i == 1 else 0)) for i in range(3)]
    else:
        point = [rfloat(rng, -3000, 3000) for _ in range(3)]
    point.append(rng.getrandbits(32))
    mode = rng.choice((0, 1, 2, 3, 4, 5, -1))
    return (rng.choice((0, 1, 2, 3)), mode, point, rng.getrandbits(64),
            fb(rng.uniform(0.1, 40)), fb(rng.uniform(0.1, 40)), fb(rng.choice((0.0, 0.5, 5.0, -2.0))),
            rng.getrandbits(64))


def run_sprites(native, elf, beats, count, outcomes):
    counts = {'drawn': 0, 'culled': 0, 'unsupported': 0}
    for beat in beats:
        ram = bytearray((beat / 'eeMemory.bin').read_bytes())
        spad = bytearray((beat / 'scratchpad.bin').read_bytes())
        ctx = int.from_bytes(ram[0x275670:0x275674], 'little')
        rng = random.Random(beat.name)
        for k in range(count):
            undo = []
            if k % 5 == 4:          # a fog quad whose clamp reaches past 255
                st = Setup(ram, spad, undo)
                st.put(ctx + 0xA0, fb(300.0))
                st.put(ctx + 0xA8, fb(400.0))
            try:
                counts[sprite_case(native, elf, ram, spad, undo, sprite_params(rng, spad, ram), outcomes,
                                   (beat.name, k))] += 1
            finally:
                restore(undo)
        # a non-finite point: the oracle cannot run the clip; the native must fault
        undo = []
        try:
            p = list(sprite_params(rng, spad, ram))
            p[2] = [0x7F800000, 0, 0, ONE]
            counts[sprite_case(native, elf, ram, spad, undo, tuple(p), outcomes, (beat.name, 'inf'))] += 1
        finally:
            restore(undo)
    return counts


# ======================================================================
# 0015D2F0
# ======================================================================

def modemap_cases():
    cases = []
    for p4 in (0, 1, 2, 255):
        for p5 in range(256):
            for f1 in (0, 1, 2):
                for r318 in (0, 1, 2, 3, 4):
                    cases.append((p4, p5, f1, r318))
    return cases


def run_modemap(native, elf, ram, spad, cases, outcomes):
    ee = EE(elf, ram, spad)
    ee.ranges = ((MODEMAP, MODEMAP + SIZES[MODEMAP]),)
    ee.outcomes = outcomes
    undo = []
    ee.undo = undo
    buf = (U8 * 0x320)(*ram[PLAYER:PLAYER + 0x320])
    for p4, p5, f1, r318 in cases:
        for off, v in ((4, p4), (5, p5), (0x1F1, f1), (0x318, r318)):
            ee.poke(PLAYER + off, v, 1)
            buf[off] = v
        v0 = ee.call(MODEMAP)
        out = I32(-7)
        assert native.em_player_equipment_0015D2F0(buf, C.byref(out)) == 0
        assert out.value == sx(v0, 32), ('0015D2F0', (p4, p5, f1, r318), out.value, v0)
    restore(undo)
    return len(cases)


# ======================================================================
# Branch coverage
# ======================================================================

def conditional_branches(elf):
    pcs = []
    for start, size in SIZES.items():
        for pc in range(start, start + size, 4):
            o = pc - 0x100000 + 0x300
            w = int.from_bytes(elf[o:o + 4], 'little')
            op, rt = w >> 26, w >> 16 & 31
            if op == 4 and w >> 21 & 31 == rt:          # unconditional (b)
                continue
            if op in (4, 5, 6, 7, 0x14, 0x15, 0x16, 0x17) or (op == 1 and rt < 4) or (op == 0x11 and w >> 21 & 31 == 8):
                pcs.append(pc)
    return pcs


# Outcomes no input can produce (each explained in docs/PLAYER_EQUIPMENT.md
# section 6): (pc, taken).
IMPOSSIBLE = {
    (0x189064, True),    # 00188ED0 re-tests D_008106C7 != 0 after its entry test; nothing clears it between
    (0x1CD6E4, False),   # 001CD520: the fog word is clamped at +0 before the 28.4 conversion, so fog >> 4 >= 0
}


# ======================================================================
# Main
# ======================================================================

def main():
    elf = read_elf()
    targets = check_callee_set(elf)
    native = build_native_loaded()
    ram = elf_ram(elf)
    spad = bytearray(0x4000)
    sdk = SdkEval(elf, elf_ram(elf))
    outcomes = set()
    report = {}

    t0 = time.time()
    branches = conditional_branches(elf)

    def uncovered():
        return sorted((pc, t) for pc in branches for t in (False, True)
                      if (pc, t) not in outcomes and (pc, t) not in IMPOSSIBLE)
    # Full: 6000 seeds. Quick: blocks of 250 seeds in fixed order until every
    # possible branch outcome of the equipment routines is reached (at least
    # 750, at most 4000 seeds).
    report['unit'] = {'ok': 0, 'unsupported': 0}
    unit_total, block = 0, 250
    limit = RM.pick(6000, 4000)
    while unit_total < limit:
        counts = run_unit(native, elf, sdk, ram, spad, range(unit_total, unit_total + block), outcomes)
        for k, v in counts.items():
            report['unit'][k] += v
        unit_total += block
        if not RM.FULL and unit_total >= 750 and not [m for m in uncovered() if m[0] < 0x1CD520 and
                                                      not MODEMAP <= m[0] < MODEMAP + SIZES[MODEMAP]]:
            break
    report['faults'] = run_faults(native, elf, sdk, ram, spad)
    timing = {'unit': round(time.time() - t0, 2)}

    beats = route_beats()
    assert len(beats) == 15, ('route captures', len(beats))
    sprite_beats = beats if RM.FULL else [beats[0], beats[5], beats[10], beats[14]]
    t0 = time.time()
    report['sprite'] = run_sprites(native, elf, sprite_beats, RM.pick(60, 25), outcomes)
    timing['sprite'] = round(time.time() - t0, 2)

    mm = modemap_cases()
    picked = RM.select(mm, 900, 7, axes=(lambda c: c[0], lambda c: c[2], lambda c: c[3]),
                       keep=lambda i, c: c[1] in (25, 29, 30, 31, 32, 35))
    report['modemap'] = run_modemap(native, elf, ram, spad, picked, outcomes)

    t0 = time.time()
    route = RM.parallel_map(run_route_beat, beats)
    timing['route'] = round(time.time() - t0, 2)
    report['timing'] = timing
    report['route'] = {name: {k: v for k, v in c.items() if k != 'flavours'} for name, c in route}
    for name, c in route:
        assert c['nodes'] == 7 and c['unsupported'] == 0, (name, c)
    flavours = sorted({f for _, c in route for f in c['flavours']})

    missing = uncovered()
    seen_impossible = sorted(o for o in IMPOSSIBLE if o in outcomes)
    assert not seen_impossible, ('an outcome listed as impossible occurred', seen_impossible)
    conditional = {(pc, t) for pc, t in outcomes if pc in set(branches)}
    report['branch_outcomes'] = '%d of %d (%d impossible)' % (len(conditional), 2 * len(branches), len(IMPOSSIBLE))
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'report.json').write_text(json.dumps(report, indent=1, default=str))
    assert not missing, ('branch outcomes never exercised', [(hex(pc), t) for pc, t in missing])
    unit_ok = report['unit']['ok']
    RM.banner(RM.part(unit_ok, 6000, 'unit cases'),
                    RM.part(len(picked), len(mm), '0015D2F0 cases'),
                    '%d sprite cases (%d beats)' % (sum(report['sprite'].values()), len(sprite_beats)),
              '%d route nodes x2' % sum(c['nodes'] for _, c in route))
    print('route node (flavour, variant, state):', flavours)
    print('timing (s):', timing)
    print('jal targets checked: %d; conditional branch outcomes: %s; unsupported unit cases: %d'
          % (targets, report['branch_outcomes'], report['unit']['unsupported']))
    print('PASS test_player_equipment_reference')


if __name__ == '__main__':
    main()
