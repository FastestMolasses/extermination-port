#!/usr/bin/env python3
"""Compare the shared owner model services (em_owner_services_original.c)
with the original code.

The oracle below is a small EE interpreter (128-bit GPRs, COP1, VU0 macro
mode) whose float arithmetic is tools/ee_float_model.py, i.e. the model
measured in PCSX2 (docs/EE_FLOAT_MODEL.md). It executes the ORIGINAL
instructions of the pinned SCUS-97112 boot ELF:

  001B0FD0, 001B0EA0, 001C6150, 001C62C0 (bone_init_default_1), 001B1020,
  001B0DC0, 001C6380, build_trs_matrix, 001C9610 and the SDK routines
  001029C0/001029E8/00102A60/00102B08/00102BB0/00102C58/00102918,
  00102958, 001B17A0, 001CAA00, 001CA990, 001C7420, 001B1E20, 001B5B70.

Every call leaving that set (001C6120, 001CA6E0, 001AF780, 001CB5B0,
bone_init_default_2, 001B1CE0, 001B1630, 001B1B70, 001CA7B0, 001D8C20,
001D89D0, 001D1F80, 001CA940, 001CB3C0, 001B61C0, 001B6250) is recorded with
its arguments and answered from a script that the native workers answer
identically. Every modelled owner, node, scratchpad, display-list and global
byte is compared, and every byte the original writes outside the stack must
be one the native module models.

Capture checks run the same comparison over owners captured in the user's
own RAM images (build/startup-reference/playable_ee.bin and the s87 route
snapshots 04/05/07/08), and report how many captured matrices and nodes the
translation reproduces.

No original instruction bytes, disassembly or data are written by this file;
the report in build/ holds only counts and hashes.
"""
import ctypes as C
import hashlib
import json
import random
import struct
import subprocess
import sys
import time
from pathlib import Path

import ee_float_model as fm
from reference_mode import FULL, banner, part, pick, select, parallel_map

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
ELF_SHA = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
OUT = ROOT / 'build/owner_services_reference'

RETURN, STACK = 0x0BADF000, 0x01F00000
M32, M64, M128 = (1 << 32) - 1, (1 << 64) - 1, (1 << 128) - 1
ONE = 0x3F800000
SPR = 0x70000000

# Synthetic layout (outside every original structure the routines touch).
ACTOR, MODEL, NODES, NODE = 0x7B0000, 0x900000, 0x960000, 0xD0
CONTEXT, DL, DL_SIZE = 0x600000, 0xA00000, 0x9000
SKELETON = 0x100

CAPTURES = ['startup-reference/playable_ee.bin', 's87/route/04_elevator_ride/eeMemory.bin',
            's87/route/05_boxes/eeMemory.bin', 's87/route/07_truck_preview/eeMemory.bin',
            's87/route/08_truck_crossing/eeMemory.bin']


def sx32(v):
    v &= M32
    return v | (M64 ^ M32) if v & 0x80000000 else v


def s64(v):
    v &= M64
    return v - (1 << 64) if v >> 63 else v


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def bits(x): return struct.unpack('<I', struct.pack('<f', x))[0]
def number(b): return struct.unpack('<f', struct.pack('<I', b & M32))[0]


class Unmodelled(AssertionError):
    pass


class EE:
    """Bounded EE interpreter over sparse pages backed by a capture or the ELF."""

    def __init__(self, elf, ram=None, spr=None):
        self.elf, self.ram = elf, ram
        self.spr = bytes(spr) if spr is not None else bytes(0x4000)
        self.pages = {}
        self.written = set()
        self.r = [0] * 32
        self.f = [0] * 32
        self.facc, self.cond = 0, False
        self.v = [[0, 0, 0, 0] for _ in range(32)]
        self.v[0] = [0, 0, 0, ONE]
        self.acc, self.q = [0, 0, 0, 0], 0
        self.calls = {}
        self.r[28], self.r[29] = 0x27D370, STACK

    # ---- memory
    def backing(self, base):
        if SPR <= base < SPR + 0x4000:
            return bytearray(self.spr[base - SPR:base - SPR + 4096])
        if self.ram is not None and base + 4096 <= len(self.ram):
            return bytearray(self.ram[base:base + 4096])
        if 0x100000 <= base < 0x100000 + len(self.elf) - 0x300:
            o = base - 0x100000 + 0x300
            return bytearray(self.elf[o:o + 4096].ljust(4096, b'\0'))
        return bytearray(4096)

    def page(self, a):
        p = self.pages.get(a >> 12)
        if p is None:
            p = self.pages[a >> 12] = self.backing(a & ~0xFFF)
        return p

    def read(self, a, n):
        out = bytearray()
        while n:
            p, o = self.page(a), a & 0xFFF
            k = min(n, 4096 - o)
            out += p[o:o + k]
            a, n = a + k, n - k
        return bytes(out)

    def write(self, a, data, track=True):
        data = bytes(data)
        if track and not (STACK - 0x10000 <= a < STACK + 0x100):
            self.written.update(range(a, a + len(data)))
        i = 0
        while i < len(data):
            p, o = self.page(a + i), (a + i) & 0xFFF
            k = min(len(data) - i, 4096 - o)
            p[o:o + k] = data[i:i + k]
            i += k

    def load(self, a, n=4): return int.from_bytes(self.read(a & M32, n), 'little')
    def store(self, a, v, n=4): self.write(a & M32, (v & ((1 << 8 * n) - 1)).to_bytes(n, 'little'))
    def put(self, a, data): self.write(a, data, track=False)
    def put32(self, a, v): self.put(a, (v & M32).to_bytes(4, 'little'))

    # ---- registers
    def g(self, i): return self.r[i] & M64
    def set64(self, i, v):
        if i: self.r[i] = (self.r[i] & (M128 ^ M64)) | (v & M64)
    def set32(self, i, v): self.set64(i, sx32(v))
    def set128(self, i, v):
        if i: self.r[i] = v & M128

    # ---- execution
    def run(self, entry, args=(), floats=(), limit=3_000_000):
        self.set64(31, RETURN)
        for i, x in enumerate(args): self.set64(4 + i, x)
        for i, x in enumerate(floats): self.f[12 + i] = x & M32
        pc, steps = entry, 0
        while pc != RETURN:
            steps += 1
            if steps > limit: raise AssertionError(('runaway', hex(entry)))
            call = self.calls.get(pc)
            if call is not None:
                call(self)
                pc = self.g(31) & M32
                continue
            pc = self.step(pc)
        return steps

    def delay(self, pc):
        self.plain(self.load(pc + 4), pc + 4)

    def step(self, pc):
        w = self.load(pc)
        op, rs, rt = w >> 26, w >> 21 & 31, w >> 16 & 31
        off = (s16(w) << 2)
        br = pc + 4 + off
        if op == 0 and (w & 63) in (8, 9):
            target = self.g(rs) & M32
            if w & 63 == 9: self.set64(w >> 11 & 31, pc + 8)
            self.delay(pc)
            return target
        if op in (2, 3):
            target = ((pc + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
            if op == 3: self.set64(31, pc + 8)
            self.delay(pc)
            return target
        likely = False
        if op in (4, 5, 20, 21):
            taken = (self.g(rs) == self.g(rt)) == (op in (4, 20)); likely = op >= 20
        elif op in (6, 7, 22, 23):
            v = s64(self.g(rs)); taken = v <= 0 if op in (6, 22) else v > 0; likely = op >= 22
        elif op == 1:
            v = s64(self.g(rs))
            if rt & 0xF not in (0, 1, 2, 3): raise Unmodelled(('regimm', hex(pc), rt))
            taken = v < 0 if rt & 1 == 0 else v >= 0; likely = bool(rt & 2)
            if rt & 16: self.set64(31, pc + 8)
        elif op == 17 and rs == 8:
            taken = self.cond == bool(rt & 1); likely = bool(rt & 2)
        else:
            self.plain(w, pc)
            return pc + 4
        if likely and not taken: return pc + 8
        self.delay(pc)
        return br if taken else pc + 8

    def plain(self, w, pc):
        op, rs, rt, rd = w >> 26, w >> 21 & 31, w >> 16 & 31, w >> 11 & 31
        imm = s16(w)
        a = (self.g(rs) + imm) & M32
        if op == 0:
            fn, sa = w & 63, w >> 6 & 31
            t32 = self.g(rt) & M32
            if fn == 0: self.set32(rd, t32 << sa)
            elif fn == 2: self.set32(rd, t32 >> sa)
            elif fn == 3: self.set32(rd, (s64(sx32(t32)) >> sa))
            elif fn == 4: self.set32(rd, t32 << (self.g(rs) & 31))
            elif fn == 6: self.set32(rd, t32 >> (self.g(rs) & 31))
            elif fn == 7: self.set32(rd, s64(sx32(t32)) >> (self.g(rs) & 31))
            elif fn == 10:
                if self.g(rt) == 0: self.set64(rd, self.g(rs))
            elif fn == 11:
                if self.g(rt) != 0: self.set64(rd, self.g(rs))
            elif fn == 33: self.set32(rd, self.g(rs) + self.g(rt))
            elif fn == 35: self.set32(rd, self.g(rs) - self.g(rt))
            elif fn == 36: self.set64(rd, self.g(rs) & self.g(rt))
            elif fn == 37: self.set64(rd, self.g(rs) | self.g(rt))
            elif fn == 38: self.set64(rd, self.g(rs) ^ self.g(rt))
            elif fn == 39: self.set64(rd, ~(self.g(rs) | self.g(rt)))
            elif fn == 42: self.set64(rd, int(s64(self.g(rs)) < s64(self.g(rt))))
            elif fn == 43: self.set64(rd, int(self.g(rs) < self.g(rt)))
            elif fn == 45: self.set64(rd, self.g(rs) + self.g(rt))
            elif fn == 47: self.set64(rd, self.g(rs) - self.g(rt))
            elif fn == 56: self.set64(rd, self.g(rt) << sa)
            elif fn == 58: self.set64(rd, self.g(rt) >> sa)
            elif fn == 59: self.set64(rd, s64(self.g(rt)) >> sa)
            elif fn == 60: self.set64(rd, self.g(rt) << (sa + 32))
            elif fn == 62: self.set64(rd, self.g(rt) >> (sa + 32))
            elif fn == 63: self.set64(rd, s64(self.g(rt)) >> (sa + 32))
            else: raise Unmodelled(('special', hex(pc), fn))
        elif op in (8, 9): self.set32(rt, self.g(rs) + imm)
        elif op == 10: self.set64(rt, int(s64(self.g(rs)) < imm))
        elif op == 11: self.set64(rt, int(self.g(rs) < (imm & M64)))
        elif op == 12: self.set64(rt, self.g(rs) & (w & 0xFFFF))
        elif op == 13: self.set64(rt, self.g(rs) | (w & 0xFFFF))
        elif op == 14: self.set64(rt, self.g(rs) ^ (w & 0xFFFF))
        elif op == 15: self.set32(rt, (w & 0xFFFF) << 16)
        elif op in (24, 25): self.set64(rt, self.g(rs) + imm)
        elif op == 28:
            if w & 63 == 40 and (w >> 6 & 31) == 0x18:   # paddub
                x, y = self.r[rs], self.r[rt]
                self.set128(rd, sum(((x >> 8 * i & 255) + (y >> 8 * i & 255) & 255) << 8 * i
                                    for i in range(16)))
            else: raise Unmodelled(('mmi', hex(pc), hex(w)))
        elif op == 30: self.set128(rt, self.load(a & ~15, 16))
        elif op == 31: self.store(a & ~15, self.r[rt], 16)
        elif op == 32: self.set64(rt, (self.load(a, 1) ^ 0x80) - 0x80)
        elif op == 33: self.set64(rt, s16(self.load(a, 2)))
        elif op == 35: self.set32(rt, self.load(a, 4))
        elif op == 36: self.set64(rt, self.load(a, 1))
        elif op == 37: self.set64(rt, self.load(a, 2))
        elif op == 39: self.set64(rt, self.load(a, 4))
        elif op == 55: self.set64(rt, self.load(a & ~7, 8))
        elif op == 40: self.store(a, self.g(rt), 1)
        elif op == 41: self.store(a, self.g(rt), 2)
        elif op == 43: self.store(a, self.g(rt), 4)
        elif op == 63: self.store(a & ~7, self.g(rt), 8)
        elif op == 49: self.f[rt] = self.load(a, 4)
        elif op == 57: self.store(a, self.f[rt], 4)
        elif op == 54:
            if rt: self.v[rt] = [self.load((a & ~15) + 4 * i) for i in range(4)]
        elif op == 62:
            for i in range(4): self.store((a & ~15) + 4 * i, self.v[rt][i])
        elif op == 17: self.cop1(w, pc)
        elif op == 18: self.cop2(w, pc)
        else: raise Unmodelled(('opcode', hex(pc), op))
        self.r[0] = 0

    def cop1(self, w, pc):
        rs, ft, fs, fd, fn = w >> 21 & 31, w >> 16 & 31, w >> 11 & 31, w >> 6 & 31, w & 63
        f = self.f
        if rs == 0: self.set64(ft, sx32(f[fs]))
        elif rs == 4: f[fs] = self.g(ft) & M32
        elif rs == 16:
            x, y = f[fs], f[ft]
            if fn == 0: f[fd] = fm.ee_add(x, y)
            elif fn == 1: f[fd] = fm.ee_sub(x, y)
            elif fn == 2: f[fd] = fm.ee_mul(x, y)
            elif fn == 3: f[fd] = fm.ee_div(x, y)
            elif fn == 6: f[fd] = fm.ee_mov(x)
            elif fn == 7: f[fd] = fm.ee_neg(x)
            elif fn == 24: self.facc = fm.ee_adda(x, y)
            elif fn == 25: self.facc = fm.ee_suba(x, y)
            elif fn == 26: self.facc = fm.ee_mula(x, y)
            elif fn == 28: f[fd] = fm.ee_madd(self.facc, x, y)
            elif fn == 29: f[fd] = fm.ee_msub(self.facc, x, y)
            elif fn == 36: f[fd] = fm.ee_cvt_w_s(x)
            elif fn == 50: self.cond = bool(fm.ee_c_eq(x, y))
            elif fn == 52: self.cond = bool(fm.ee_c_lt(x, y))
            elif fn == 54: self.cond = bool(fm.ee_c_le(x, y))
            else: raise Unmodelled(('cop1.s', hex(pc), fn))
        elif rs == 20 and fn == 32: f[fd] = fm.ee_cvt_s_w(f[fs])
        else: raise Unmodelled(('cop1', hex(pc), rs, fn))

    def cop2(self, w, pc):
        rs = w >> 21 & 31
        ft, fs, fd, fn = w >> 16 & 31, w >> 11 & 31, w >> 6 & 31, w & 63
        if rs == 1:
            self.set128(ft, sum(x << 32 * i for i, x in enumerate(self.v[fs])))
            return
        if rs == 5:
            if fs: self.v[fs] = [(self.r[ft] >> 32 * i) & M32 for i in range(4)]
            return
        if rs < 16: raise Unmodelled(('cop2', hex(pc), rs))
        dest = w >> 21 & 15
        lanes = [i for i in range(4) if dest & (8 >> i)]
        S, T = list(self.v[fs]), list(self.v[ft])

        def write(reg, values):
            if reg:
                for i in lanes: self.v[reg][i] = values[i]

        if fn < 0x3C:
            bc = fn & 3
            if fn < 4: write(fd, {i: fm.vu_lane('vaddbc', dest, bc, S[i], T[bc]) for i in lanes})
            elif fn < 8: write(fd, {i: fm.vu_lane('vsubbc', dest, bc, S[i], T[bc]) for i in lanes})
            elif fn < 12: write(fd, {i: fm.vu_lane('vmaddbc', dest, bc, S[i], T[bc], self.acc[i]) for i in lanes})
            elif 0x18 <= fn < 0x1C: write(fd, {i: fm.vu_lane('vmulbc', dest, bc, S[i], T[bc]) for i in lanes})
            elif fn == 0x1C: write(fd, {i: fm.vu_lane('vmulq', dest, None, S[i], self.q) for i in lanes})
            elif fn == 0x20: write(fd, {i: fm.vu_lane('vaddq', dest, None, S[i], self.q) for i in lanes})
            elif fn == 0x28: write(fd, {i: fm.vu_lane('vadd', dest, None, S[i], T[i]) for i in lanes})
            elif fn == 0x2A: write(fd, {i: fm.vu_lane('vmul', dest, None, S[i], T[i]) for i in lanes})
            elif fn == 0x2C: write(fd, {i: fm.vu_lane('vsub', dest, None, S[i], T[i]) for i in lanes})
            else: raise Unmodelled(('vu', hex(pc), hex(fn)))
            return
        op2 = (w >> 6 & 31) << 2 | (fn & 3)
        bc = fn & 3
        if 0x08 <= op2 < 0x0C:
            new = {i: fm.vu_lane('vmaddabc', dest, bc, S[i], T[bc], self.acc[i]) for i in lanes}
            for i in lanes: self.acc[i] = new[i]
        elif 0x18 <= op2 < 0x1C:
            new = {i: fm.vu_lane('vmulabc', dest, bc, S[i], T[bc]) for i in lanes}
            for i in lanes: self.acc[i] = new[i]
        elif op2 == 0x30: write(ft, {i: S[i] for i in lanes})                       # vmove
        elif op2 == 0x31: write(ft, {i: [S[1], S[2], S[3], S[0]][i] for i in lanes})  # vmr32
        elif op2 == 0x38: self.q = fm.vu_div(S[w >> 21 & 3], T[w >> 23 & 3], w >> 21 & 3, w >> 23 & 3)
        elif op2 == 0x39: self.q = fm.vu_sqrt(T[w >> 23 & 3])
        elif op2 in (0x3B, 0x2F): pass                                                # vwaitq, vnop
        else: raise Unmodelled(('vu2', hex(pc), hex(op2)))


# ---------------------------------------------------------------- native side

class Bone(C.Structure):
    _fields_ = [('bind', C.c_float * 16), ('parent', C.c_int16), ('rot', C.c_float * 3),
                ('trans', C.c_float * 3), ('scale', C.c_int16 * 3), ('world', C.c_float * 16)]


class Record(C.Structure):
    _fields_ = [('parent', C.c_int16), ('bind', C.c_float * 16)]


class Model(C.Structure):
    _fields_ = [('bone_count', C.c_uint8), ('radius', C.c_float),
                ('skeleton', C.POINTER(Record)), ('skeleton_records', C.c_uint32)]


MAX_BONES = 56


class Owner(C.Structure):
    _fields_ = [('drawn', C.c_uint8), ('cls', C.c_uint8), ('kind', C.c_uint8),
                ('lifecycle', C.c_uint8), ('bones_held', C.c_uint8), ('bone_count', C.c_uint8),
                ('model_id', C.c_uint8), ('flags2', C.c_uint16), ('anim', C.c_uint32),
                ('model', C.POINTER(Model)), ('scale', C.c_float * 4), ('attachment', C.c_uint32),
                ('collapsed_bone', C.c_int16), ('pose_bone', C.c_uint8), ('pos', C.c_float * 4),
                ('rot', C.c_float * 4), ('world', C.c_float * 16),
                ('bone', C.POINTER(Bone) * MAX_BONES)]


class Scratch(C.Structure):
    _fields_ = [('s3400', C.c_float * 16), ('s3440', C.c_float * 16),
                ('s3480', C.c_float * 16), ('s3AC0', C.c_float * 16)]


class Channel(C.Structure):
    _fields_ = [('cursor', C.c_void_p), ('end', C.c_void_p)]


class World(C.Structure):
    _fields_ = [('d0028A59C', C.POINTER(C.c_uint32)), ('d0028A56C', C.POINTER(C.c_uint32)),
                ('d0028A490', C.POINTER(C.c_uint32)), ('d0028A490_count', C.c_uint32),
                ('d00275BCC', C.POINTER(C.c_int16)), ('d00810CA5', C.POINTER(C.c_uint8)),
                ('d00275B40', C.POINTER(C.POINTER(Bone))), ('d00275B40_count', C.c_uint32),
                ('scratch', C.POINTER(Scratch)), ('channel', C.POINTER(Channel)),
                ('channel_count', C.c_uint32), ('d0024D6F0', C.POINTER(C.c_uint8)),
                ('d0024D6F0_count', C.c_uint32), ('d00810E56', C.POINTER(C.c_uint8)),
                ('d00810E68', C.POINTER(C.c_int16))]


W = C.CFUNCTYPE
PO, PB, PM, PF = C.POINTER(Owner), C.POINTER(Bone), C.POINTER(Model), C.POINTER(C.c_float)
WORKERS = [
    ('w_001C6120', W(C.c_int, C.c_void_p, C.c_uint32, C.c_uint32, C.POINTER(C.c_uint32))),
    ('w_001CA6E0', W(C.c_int, C.c_void_p, PO, C.c_uint32)),
    ('w_001AF780', W(C.c_int, C.c_void_p, C.POINTER(PB))),
    ('w_anim_bone_array_setup', W(C.c_int, C.c_void_p, C.c_uint8)),
    ('w_bone_init_default_2', W(C.c_int, C.c_void_p, PO, C.c_int16)),
    ('w_001B1CE0', W(C.c_int, C.c_void_p, PO)),
    ('w_001B1630', W(C.c_int, C.c_void_p, C.c_uint32, C.c_uint32, C.c_uint32, C.POINTER(C.c_uint8))),
    ('w_001B1B70', W(C.c_int, C.c_void_p, PO)),
    ('w_001CA7B0', W(C.c_int, C.c_void_p, PF, C.c_uint32, C.POINTER(C.c_int32))),
    ('w_001D8C20', W(C.c_int, C.c_void_p, C.c_int32)),
    ('w_001D89D0', W(C.c_int, C.c_void_p, PO, PF, PF)),
    ('w_001D1F80', W(C.c_int, C.c_void_p, C.c_int32, C.c_int32, C.c_int32)),
    ('w_001CA940', W(C.c_int, C.c_void_p, C.c_int32, PM)),
    ('w_001CB3C0', W(C.c_int, C.c_void_p, PO)),
    ('w_001B61C0', W(C.c_int, C.c_void_p, C.c_uint8, C.c_uint8, C.c_int64, C.c_int32)),
    ('w_001B6250', W(C.c_int, C.c_void_p)),
]
WORKER_TYPES = dict(WORKERS)


class Workers(C.Structure):
    _fields_ = [('ctx', C.c_void_p)] + WORKERS


class Fault(C.Structure):
    _fields_ = [('address', C.c_uint32), ('code', C.c_int32)]


class Services(C.Structure):
    _fields_ = [('world', World), ('workers', Workers), ('fault', Fault)]


def build_library():
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / 'owner_services.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-shared', '-fPIC', '-I' + str(ROOT / 'src'),
                    str(ROOT / 'src/game/em_owner_services_original.c'), '-o', str(path)], check=True)
    lib = C.CDLL(str(path))
    P = C.POINTER(Services)
    for name, extra in (('001B0FD0', []), ('001B0EA0', []), ('001C62C0', []), ('001C6380', []),
                        ('001B17A0', []), ('001CAA00', []), ('001B5B70', []),
                        ('001B1020', [C.c_uint32, C.c_int32, C.c_int32]),
                        ('001B0DC0', [C.c_uint32, C.c_int32])):
        fn = getattr(lib, 'em_owner_services_' + name)
        fn.argtypes = [P] + ([PO] if name != '001B5B70' else []) + extra
        fn.restype = C.c_int
    lib.em_owner_services_001C7420.argtypes = [P, PO, C.c_int32, C.c_int32, C.POINTER(C.c_void_p)]
    lib.em_owner_services_001B1E20.argtypes = [P, C.c_int32, C.c_int64]
    for name in ('rotate_x_00102B08', 'rotate_y_00102BB0', 'rotate_z_00102A60'):
        getattr(lib, 'em_owner_services_' + name).argtypes = [PF, PF, C.c_uint32]   # raw f12 bits
        getattr(lib, 'em_owner_services_' + name).restype = C.c_int
    lib.em_owner_services_euler_00102C58.argtypes = [PF, PF, PF]
    lib.em_owner_services_translate_00102918.argtypes = [PF, PF, PF]
    for name in ('euler_00102C58', 'translate_00102918'):
        getattr(lib, 'em_owner_services_' + name).restype = C.c_int
    lib.em_owner_services_copy_qw4_00102958.argtypes = [PF, PF]
    return lib


def fbytes(arr): return bytes(arr)


# ---------------------------------------------------------------- fixture

class Fixture:
    """Native state + the oracle's original memory for one case."""

    def __init__(self, lib, elf, ram=None, spr=None, actor=ACTOR):
        self.lib, self.elf = lib, elf
        self.o = EE(elf, ram, spr)
        self.actor = actor
        self.owner = Owner()
        self.bones = (Bone * 64)()
        self.node_addr = [NODES + NODE * i for i in range(64)]
        self.model = Model()
        self.records = (Record * 64)()
        self.model_addr = MODEL
        self.scratch = Scratch()
        self.dl = (C.c_uint8 * DL_SIZE)()
        self.channels = (Channel * 2)()
        self.s = Services()
        self.exp, self.act = [], []
        self.slot_script, self.slot_i, self.slot_n = [], 0, 0
        self.keep = []
        self.results = {}
        self.cap = C.c_int16(0x400)
        self.mode = C.c_uint8(6)
        self.bank59c, self.bank56c = C.c_uint32(0x12340000), C.c_uint32(0x23450000)
        self.anim_table = (C.c_uint32 * 4)(0x111, 0x222, 0x333, 0x444)
        self.work = (PB * 64)()
        self.e56, self.e68 = C.c_uint8(0), C.c_int16(0)
        w = self.s.world
        w.d0028A59C = C.pointer(self.bank59c); w.d0028A56C = C.pointer(self.bank56c)
        w.d0028A490 = C.cast(self.anim_table, C.POINTER(C.c_uint32)); w.d0028A490_count = 4
        w.d00275BCC = C.pointer(self.cap); w.d00810CA5 = C.pointer(self.mode)
        w.d00275B40 = C.cast(self.work, C.POINTER(PB)); w.d00275B40_count = 64
        w.scratch = C.pointer(self.scratch)
        w.channel = C.cast(self.channels, C.POINTER(Channel)); w.channel_count = 2
        w.d00810E56 = C.pointer(self.e56); w.d00810E68 = C.pointer(self.e68)
        self.install_workers()

    # ---- addresses
    def bone_index(self, ptr):
        a = C.cast(ptr, C.c_void_p).value
        if not a: return None
        return (a - C.addressof(self.bones)) // C.sizeof(Bone)

    def bone_ptr(self, i): return C.pointer(self.bones[i])

    def addr_of_bone(self, ptr):
        i = self.bone_index(ptr)
        return 0 if i is None else self.node_addr[i]

    # ---- workers (both sides answer from the same script)
    def install_workers(self):
        s, o = self.s, self.o
        R = self.results

        def rec(entry, *args): self.act.append((entry,) + args)

        def n_6120(_, bank, ident, out):
            rec('6120', bank, ident); out[0] = R.get('handle', 0x5000); return 0
        def n_a6e0(_, owner, handle):
            rec('a6e0', handle); owner[0].model = C.pointer(self.model) if R.get('bind', True) else None
            return 0
        def n_f780(_, out):
            v = self.slot_script[self.slot_i] if self.slot_i < len(self.slot_script) else None
            self.slot_i += 1
            rec('f780')
            out[0] = self.bone_ptr(v) if v is not None else PB()
            return 0
        def n_anim(_, count): rec('anim', count); return 0
        def n_bone2(_, owner, clip): rec('bone2', clip & 0xFFFF); return 0
        def n_1ce0(_, owner): rec('1ce0'); return 0
        def n_1630(_, x, y, z, out):
            rec('1630', x, y, z); out[0] = R.get('visible', 0) & 255; return 0
        def n_1b70(_, owner): rec('1b70'); return 0
        def n_a7b0(_, pos, radius, out):
            raw = C.cast(pos, C.POINTER(C.c_uint32))
            rec('a7b0', tuple(raw[i] for i in range(3)), radius)
            out[0] = R.get('cull', 0); return 0
        def n_8c20(_, mode): rec('8c20', mode); return 0
        def n_89d0(_, owner, a, b):
            rec('89d0')
            C.memmove(a, struct.pack('<16I', *R['light']), 64)
            C.memmove(b, struct.pack('<16I', *R['color']), 64)
            return 0
        def n_1f80(_, a0, a1, a2): rec('1f80', a0, a1, a2); return 0
        def n_a940(_, flags, model):
            m = C.cast(model, C.c_void_p).value
            rec('a940', flags, self.model_addr if m == C.addressof(self.model) else (0 if not m else -1))
            return 0
        def n_b3c0(_, owner): rec('b3c0'); return 0
        def n_61c0(_, big, small, dur, force): rec('61c0', big, small, dur & M64, force); return 0
        def n_6250(_): rec('6250'); return 0

        natives = dict(w_001C6120=n_6120, w_001CA6E0=n_a6e0, w_001AF780=n_f780,
                       w_anim_bone_array_setup=n_anim, w_bone_init_default_2=n_bone2,
                       w_001B1CE0=n_1ce0, w_001B1630=n_1630, w_001B1B70=n_1b70,
                       w_001CA7B0=n_a7b0, w_001D8C20=n_8c20, w_001D89D0=n_89d0,
                       w_001D1F80=n_1f80, w_001CA940=n_a940, w_001CB3C0=n_b3c0,
                       w_001B61C0=n_61c0, w_001B6250=n_6250)
        for name, fn in natives.items():
            cf = WORKER_TYPES[name](fn)
            self.keep.append(cf)
            setattr(s.workers, name, cf)

        def e(entry, *args): self.exp.append((entry,) + args)
        a = lambda m, i: m.g(4 + i) & M32

        def o_6120(m):
            e('6120', a(m, 0), a(m, 1)); m.set32(2, R.get('handle', 0x5000))
        def o_a6e0(m):
            assert a(m, 0) == self.actor
            e('a6e0', a(m, 1)); m.store(self.actor + 0x44, self.model_addr if R.get('bind', True) else 0)
        def o_f780(m):
            v = self.slot_script[self.slot_n] if self.slot_n < len(self.slot_script) else None
            self.slot_n += 1
            e('f780'); m.set32(2, self.node_addr[v] if v is not None else 0)
        def o_anim(m): e('anim', a(m, 0) & 255)
        def o_bone2(m):
            assert a(m, 0) == self.actor
            e('bone2', a(m, 1) & 0xFFFF)
        def o_owner(tag):
            def f(m):
                assert a(m, 0) == self.actor, (tag, hex(a(m, 0)))
                e(tag)
            return f
        def o_1630(m):
            e('1630', m.f[12], m.f[13], m.f[14]); m.set32(2, R.get('visible', 0) & 255)
        def o_a7b0(m):
            p = a(m, 0)
            e('a7b0', tuple(m.load(p + 4 * i) for i in range(3)), m.f[12])
            m.set32(2, R.get('cull', 0))
        def o_8c20(m): e('8c20', s64(sx32(a(m, 0))))
        def o_89d0(m):
            assert a(m, 0) == self.actor and a(m, 1) == 0x70003400 and a(m, 2) == 0x70003440
            assert a(m, 3) == self.actor + 0x80
            e('89d0')
            for i in range(16):
                m.store(0x70003400 + 4 * i, R['light'][i]); m.store(0x70003440 + 4 * i, R['color'][i])
        def o_1f80(m): e('1f80', *(s64(sx32(a(m, i))) for i in range(3)))
        def o_a940(m): e('a940', s64(sx32(a(m, 0))), a(m, 1))
        def o_61c0(m): e('61c0', m.g(4) & 255, m.g(5) & 255, m.g(6), s64(sx32(m.g(7))))
        def o_6250(m):
            assert a(m, 0) == 0x810E40
            e('6250')
        o.calls.update({0x1C6120: o_6120, 0x1CA6E0: o_a6e0, 0x1AF780: o_f780, 0x1CB5B0: o_anim,
                        0x1C63E0: o_bone2, 0x1B1CE0: o_owner('1ce0'), 0x1B1630: o_1630,
                        0x1B1B70: o_owner('1b70'), 0x1CA7B0: o_a7b0, 0x1D8C20: o_8c20,
                        0x1D89D0: o_89d0, 0x1D1F80: o_1f80, 0x1CA940: o_a940,
                        0x1CB3C0: o_owner('b3c0'), 0x1B61C0: o_61c0, 0x1B6250: o_6250})

    def normalise_calls(self):
        """Native worker args in the oracle's representation."""
        out = []
        for c in self.act:
            if c[0] == '61c0': c = ('61c0', c[1], c[2], c[3] & M64, c[4])
            out.append(c)
        return out

    # ---- state transfer
    def push_model(self, count, radius, records):
        o = self.o
        o.put(self.model_addr + 8, bytes([count]))
        o.put32(self.model_addr + 0xC, SKELETON)
        o.put32(self.model_addr + 0x20, radius)
        self.model.bone_count = count
        C.memmove(C.addressof(self.model) + Model.radius.offset, struct.pack('<I', radius), 4)
        self.model.skeleton = C.cast(self.records, C.POINTER(Record))
        self.model.skeleton_records = len(records)
        for i, (parent, bind) in enumerate(records):
            base = self.model_addr + SKELETON + 0x50 * i
            o.put(base, bytes(4) + struct.pack('<h', parent) + bytes(10))
            o.put(base + 0x10, struct.pack('<16I', *bind))
            self.records[i].parent = parent
            C.memmove(C.addressof(self.records[i].bind), struct.pack('<16I', *bind), 64)

    def push_bone(self, i, parent, rot, trans, scale, bind, world):
        b, a, o = self.bones[i], self.node_addr[i], self.o
        C.memmove(C.addressof(b.bind), struct.pack('<16I', *bind), 64)
        b.parent = parent
        C.memmove(C.addressof(b.rot), struct.pack('<3I', *rot), 12)
        C.memmove(C.addressof(b.trans), struct.pack('<3I', *trans), 12)
        C.memmove(C.addressof(b.scale), struct.pack('<3h', *scale), 6)
        C.memmove(C.addressof(b.world), struct.pack('<16I', *world), 64)
        o.put(a, struct.pack('<16I', *bind))
        o.put(a + 0x64, struct.pack('<h', parent))
        o.put(a + 0x70, struct.pack('<3I', *rot) + struct.pack('<3I', *trans) + struct.pack('<3h', *scale))
        o.put(a + 0x90, struct.pack('<16I', *world))

    def push_owner(self, **f):
        ow, o, A = self.owner, self.o, self.actor
        for name, off, size in OWNER_BYTES:
            if name in f:
                setattr(ow, name, f[name])
                o.put(A + off, (f[name] & ((1 << 8 * size) - 1)).to_bytes(size, 'little'))
        for name, off, n in OWNER_FLOATS:
            if name in f:
                vals = f[name]
                C.memmove(C.addressof(getattr(ow, name)), struct.pack(f'<{n}I', *vals), 4 * n)
                o.put(A + off, struct.pack(f'<{n}I', *vals))
        if 'model' in f:
            ow.model = C.pointer(self.model) if f['model'] else None
            o.put32(A + 0x44, self.model_addr if f['model'] else 0)
        if 'slots' in f:
            for i, v in enumerate(f['slots']):
                ow.bone[i] = self.bone_ptr(v) if v is not None else PB()
                o.put32(A + 0x110 + 4 * i, self.node_addr[v] if v is not None else 0)

    # ---- comparison
    def owner_images(self, slots):
        ow, o, A = self.owner, self.o, self.actor
        nat, org = {}, {}
        for name, off, size in OWNER_BYTES:
            nat[name] = getattr(ow, name) & ((1 << 8 * size) - 1)
            org[name] = o.load(A + off, size)
        for name, off, n in OWNER_FLOATS:
            nat[name] = bytes(getattr(ow, name))
            org[name] = o.read(A + off, 4 * n)
        m = C.cast(ow.model, C.c_void_p).value
        nat['model'] = 0 if not m else (self.model_addr if m == C.addressof(self.model) else -1)
        org['model'] = o.load(A + 0x44)
        nat['slots'] = [self.addr_of_bone(ow.bone[i]) for i in range(slots)]
        org['slots'] = [o.load(A + 0x110 + 4 * i) for i in range(slots)]
        return nat, org

    def bone_images(self, i):
        b, a, o = self.bones[i], self.node_addr[i], self.o
        nat = (bytes(b.bind), b.parent & 0xFFFF, bytes(b.rot), bytes(b.trans), bytes(b.scale), bytes(b.world))
        org = (o.read(a, 64), o.load(a + 0x64, 2), o.read(a + 0x70, 12), o.read(a + 0x7C, 12),
               o.read(a + 0x88, 6), o.read(a + 0x90, 64))
        return nat, org

    def compare(self, tag, slots=0, bones=(), scratch=False, dl=None):
        nat, org = self.owner_images(slots)
        for k in nat:
            assert nat[k] == org[k], (tag, 'owner', k, nat[k], org[k])
        for i in bones:
            n, g = self.bone_images(i)
            for j, name in enumerate(('bind', 'parent', 'rot', 'trans', 'scale', 'world')):
                assert n[j] == g[j], (tag, 'bone', i, name, n[j].hex() if isinstance(n[j], bytes) else n[j],
                                      g[j].hex() if isinstance(g[j], bytes) else g[j])
        if scratch:
            for name, off in (('s3400', 0x3400), ('s3440', 0x3440), ('s3480', 0x3480), ('s3AC0', 0x3AC0)):
                assert bytes(getattr(self.scratch, name)) == self.o.read(SPR + off, 64), (tag, name)
        calls = self.normalise_calls()
        assert calls == self.exp, (tag, 'calls', calls, self.exp)
        assert self.s.fault.code == 0, (tag, 'fault', hex(self.s.fault.address), self.s.fault.code)

    def check_writes(self, tag, allowed):
        extra = sorted(a for a in self.o.written if not any(lo <= a < hi for lo, hi in allowed))
        assert not extra, (tag, 'unmodelled original writes', [hex(a) for a in extra[:8]], len(extra))


OWNER_BYTES = [('drawn', 1, 1), ('cls', 2, 1), ('kind', 3, 1), ('lifecycle', 4, 1),
               ('bones_held', 9, 1), ('bone_count', 0xC, 1), ('model_id', 0xD, 1),
               ('flags2', 0x2E, 2), ('anim', 0x40, 4), ('attachment', 0x90, 4),
               ('collapsed_bone', 0x94, 2), ('pose_bone', 0x98, 1)]
OWNER_FLOATS = [('scale', 0x60, 4), ('pos', 0xB0, 4), ('rot', 0xC0, 4), ('world', 0xD0, 16)]


def owner_allowed(actor, slots):
    return [(actor + 1, actor + 2), (actor + 4, actor + 5), (actor + 9, actor + 10),
            (actor + 0xC, actor + 0xD), (actor + 0x40, actor + 0x48), (actor + 0xD0, actor + 0x110),
            (actor + 0x110, actor + 0x110 + 4 * slots)]


def node_allowed(addrs):
    out = []
    for a in addrs:
        out += [(a, a + 0x40), (a + 0x64, a + 0x66), (a + 0x70, a + 0x8E), (a + 0x90, a + 0xD0)]
    return out


SPR_ALLOWED = [(SPR + 0x3400, SPR + 0x34C0)]


# ---------------------------------------------------------------- random values

def rf(rng, lo, hi): return bits(number(bits(rng.uniform(lo, hi))))


def special(rng):
    return rng.choice([0, 0x80000000, ONE, 0xBF800000, 0x00000001, 0x80400000, 0x7F7FFFFF, 0xFF7FFFFF,
                       0x7F800000, 0xFF800000, 0x7FC00000, 0x7F800001, 0x00800000, 0x3FC90FDB,
                       0x40490FDB, 0xC0490FDB, 0x4B800000, 0x33800000,
                       0xFFC00000, 0xFF800001])   # negative NaNs take the add.s side of the SDK prologue


def angle(rng):
    if rng.random() < 0.12: return special(rng)
    return rf(rng, -7.0, 7.0) if rng.random() < 0.8 else rf(rng, -1e4, 1e4)


def matrix(rng, lo=-2.0, hi=2.0, specials=0.0):
    return [special(rng) if rng.random() < specials else rf(rng, lo, hi) for _ in range(16)]


# ---------------------------------------------------------------- A. binding

def case_bind(args):
    """001B0FD0 / 001B0EA0 / 001B1020 / 001B0DC0 with bone_init_default_1."""
    seed, entry = args
    rng = random.Random(seed)
    fx = Fixture(LIB, ELF)
    count = rng.choice([0, 1, 2, 3, 4, 21, 31, 56, rng.randrange(0, 57)])
    cap = rng.choice([count, count - 1, count + 1, 0x40, 0x3FF, -1, 0, 0x7FFF])
    fx.cap.value = cap
    fx.o.put(0x275BCC, struct.pack('<h', cap))
    fx.o.put32(0x28A59C, fx.bank59c.value)
    fx.o.put32(0x28A56C, fx.bank56c.value)
    for i in range(4): fx.o.put32(0x28A490 + 4 * i, fx.anim_table[i])
    records = [(rng.choice([-1, 0, 1, rng.randrange(-5, 60)]), matrix(rng, -50, 50, 0.05))
               for _ in range(max(count, 1))]
    fx.push_model(count, rf(rng, 1, 40), records)
    fx.results.update(handle=rng.randrange(1 << 32), bind=True)
    fx.slot_script = list(range(64))
    rng.shuffle(fx.slot_script)
    for i in range(64):
        fx.push_bone(i, rng.randrange(-3, 3), matrix(rng)[:3], matrix(rng)[:3],
                     [rng.randrange(-0x8000, 0x8000) for _ in range(3)], matrix(rng), matrix(rng))
    life = rng.choice([0, 1, 2, 0xFF])
    fx.push_owner(lifecycle=life, model_id=rng.randrange(256), bones_held=rng.randrange(256),
                  bone_count=rng.randrange(256), anim=rng.randrange(1 << 32), model=False)
    a1 = rng.choice([rng.randrange(1 << 32), 0x13, 0x8013])
    a2 = rng.choice([-1, 0, 1, 3])
    a3 = rng.choice([0, 1, -1, 0x12345, 0x7FFF, 0x8000])
    lib, P = LIB, C.byref(fx.s)
    if entry == 0x1B0FD0: r = lib.em_owner_services_001B0FD0(P, C.byref(fx.owner)); args_o = (fx.actor,)
    elif entry == 0x1B0EA0: r = lib.em_owner_services_001B0EA0(P, C.byref(fx.owner)); args_o = (fx.actor,)
    elif entry == 0x1B0DC0:
        r = lib.em_owner_services_001B0DC0(P, C.byref(fx.owner), a1, a2); args_o = (fx.actor, a1, a2 & M64)
    else:
        r = lib.em_owner_services_001B1020(P, C.byref(fx.owner), a1, a2, a3)
        args_o = (fx.actor, a1, a2 & M64, a3 & M64)
    fx.o.run(entry, args_o)
    over = count > 56 and cap >= count
    if over:
        assert r == -1 and fx.s.fault.code == 4, ('bind cap', seed, r)
        return ('bind-over56', 1)
    ro = s64(sx32(fx.o.g(2)))
    assert r == ro, (hex(entry), seed, r, ro)
    held = count if r == 0 else 0
    fx.compare((hex(entry), seed), slots=held, bones=range(64))
    fx.check_writes((hex(entry), seed), owner_allowed(fx.actor, held) + node_allowed(fx.node_addr))
    return (hex(entry), 1)


def case_bone_init(args):
    """bone_init_default_1 alone, including NULL/short-record faults."""
    seed = args
    rng = random.Random(seed)
    fx = Fixture(LIB, ELF)
    count = rng.choice([0, 1, 3, 21, 56])
    records = [(rng.randrange(-0x8000, 0x8000), matrix(rng, -1e3, 1e3, 0.1)) for _ in range(count)]
    fx.push_model(count, ONE, records)
    for i in range(64):
        fx.push_bone(i, 7, matrix(rng)[:3], matrix(rng)[:3], [5, 6, 7], matrix(rng), matrix(rng))
    slots = list(range(count)); rng.shuffle(slots)
    fx.push_owner(bone_count=count, model=True, slots=slots)
    r = LIB.em_owner_services_001C62C0(C.byref(fx.s), C.byref(fx.owner))
    fx.o.run(0x1C62C0, (fx.actor,))
    assert r == 0
    fx.compare(('1C62C0', seed), slots=count, bones=range(64))
    fx.check_writes(('1C62C0', seed), node_allowed(fx.node_addr))
    return ('bone_init', 1)


# ---------------------------------------------------------------- B. placement

def case_place(args):
    seed, count = args
    rng = random.Random(seed)
    fx = Fixture(LIB, ELF)
    sp = 0.08 if seed % 3 == 0 else 0.0
    for i in range(64):
        parent = -1 if i == 0 or rng.random() < 0.3 else rng.randrange(0, max(1, count))
        fx.push_bone(i, parent, [angle(rng) for _ in range(3)],
                     [rf(rng, -100, 100) for _ in range(3)],
                     [rng.choice([0x1000, 0x1000, 0x800, rng.randrange(-0x8000, 0x8000)]) for _ in range(3)],
                     matrix(rng, -3, 3, sp), matrix(rng))
    fx.push_owner(bone_count=count, pos=[rf(rng, -500, 500) for _ in range(3)] + [ONE],
                  rot=[angle(rng) for _ in range(3)] + [0],
                  scale=[rng.choice([ONE, rf(rng, 0.1, 4), special(rng)]) for _ in range(3)] + [ONE],
                  world=matrix(rng), slots=list(range(count)))
    spr = matrix(rng)
    fx.o.put(SPR + 0x3400, struct.pack('<16I', *spr))
    C.memmove(C.addressof(fx.scratch.s3400), struct.pack('<16I', *spr), 64)
    r = LIB.em_owner_services_001C6380(C.byref(fx.s), C.byref(fx.owner))
    fx.o.run(0x1C6380, (fx.actor,))
    assert r == 0, (seed, r)
    fx.compare(('1C6380', seed), slots=count, bones=range(64), scratch=True)
    fx.check_writes(('1C6380', seed), owner_allowed(fx.actor, 0) + node_allowed(fx.node_addr) + SPR_ALLOWED)
    return ('place', 1)


def case_sdk(args):
    """The SDK routines alone: 00102B08/BB0/A60, 00102C58, 00102918, 00102958."""
    seed = args
    rng = random.Random(seed)
    o = EE(ELF)
    src = matrix(rng, -10, 10, 0.05 if seed % 2 else 0.0)
    if seed % 5 == 4:   # tiny rows: products flush to signed zeros (FTZ sign)
        src = [bits(number(v) * 1e-37) if rng.random() < 0.7 else v for v in src]
    if seed % 7 == 3:   # row 3 w: -0 / denormal / signalling NaN (the dest mask of 00102918's vadd.xyz)
        src[15] = rng.choice([0x80000000, 0x00000001, 0x80400000, 0x7F800001])
    ang = angle(rng)
    vec = [angle(rng) for _ in range(3)]
    o.put(0x500000, struct.pack('<16I', *src))
    o.put(0x500100, struct.pack('<3I', *vec))
    n_src = (C.c_float * 16).from_buffer_copy(struct.pack('<16I', *src))
    n_vec = (C.c_float * 3).from_buffer_copy(struct.pack('<3I', *vec))
    for entry, name in ((0x102B08, 'rotate_x_00102B08'), (0x102BB0, 'rotate_y_00102BB0'),
                        (0x102A60, 'rotate_z_00102A60')):
        o.put(0x500040, bytes(64))
        o.run(entry, (0x500040, 0x500000), (ang,))
        dst = (C.c_float * 16)()
        assert getattr(LIB, 'em_owner_services_' + name)(dst, n_src, ang) == 0, (name, seed)
        assert bytes(dst) == o.read(0x500040, 64), (name, seed, hex(ang))
    o.put(0x500040, bytes(64))
    o.run(0x102C58, (0x500040, 0x500000, 0x500100))
    dst = (C.c_float * 16)()
    assert LIB.em_owner_services_euler_00102C58(dst, n_src, n_vec) == 0, ('euler', seed)
    assert bytes(dst) == o.read(0x500040, 64), ('euler', seed)
    o.run(0x102918, (0x500040, 0x500000, 0x500100))
    assert LIB.em_owner_services_translate_00102918(dst, n_src, n_vec) == 0, ('translate', seed)
    assert bytes(dst) == o.read(0x500040, 64), ('translate', seed)
    o.run(0x102958, (0x500080, 0x500000))
    LIB.em_owner_services_copy_qw4_00102958(dst, n_src)
    assert bytes(dst) == o.read(0x500080, 64), ('copy_qw4', seed)
    return ('sdk', 1)


# ---------------------------------------------------------------- C. publication

# +0xB0/+0xB4/+0xB8 reach 001B1630 as f12..f14 raw bits. Entry 0 is the finite
# position of the gate matrix; the rest rotate specials through every lane:
# signalling NaNs (+ and -), a quiet NaN, -0, +/- denormals and +/-Inf. The
# fourth word (+0xBC, not an argument) carries a special too.
PUBLISH_SPECIALS = [0x7F800001, 0x80000000, 0x00000001, 0x7F800000, 0xFF800000, 0xFF800001,
                    0x807FFFFF, 0x7FC00000]
PUBLISH_POS = [[bits(12.5), bits(-3.25), bits(400.0), ONE]] + [
    [PUBLISH_SPECIALS[(i + k) % 8] for k in range(4)] for i in range(8)]


def case_publish(args):
    mode, cls, kind, model_id, flags2, visible, posi = args
    fx = Fixture(LIB, ELF)
    fx.mode.value = mode
    fx.o.put(0x810CA5, bytes([mode]))
    fx.results['visible'] = visible
    pos = PUBLISH_POS[posi]
    fx.push_owner(cls=cls, kind=kind, model_id=model_id, flags2=flags2, drawn=0x55, pos=pos)
    r = LIB.em_owner_services_001B17A0(C.byref(fx.s), C.byref(fx.owner))
    fx.o.run(0x1B17A0, (fx.actor,))
    assert r == fx.o.g(2) & 255, (args, r)
    # The original's f12..f14 must be the stored bits themselves (no host float
    # round trip on either side); compare() then checks native == original.
    assert [c for c in fx.exp if c[0] == '1630'] == [('1630', pos[0], pos[1], pos[2])], (args, fx.exp)
    fx.compare(('1B17A0', args))
    fx.check_writes(('1B17A0', args), owner_allowed(fx.actor, 0))
    return ('publish' if posi == 0 else 'publish_specials', 1)


# ---------------------------------------------------------------- D. draw

def case_draw(args):
    seed, count = args
    rng = random.Random(seed)
    fx = Fixture(LIB, ELF)
    sp = 0.06 if seed % 4 == 0 else 0.0
    for i in range(64):
        fx.push_bone(i, -1, [0, 0, 0], [0, 0, 0], [0x1000] * 3, matrix(rng),
                     matrix(rng, -300, 300, sp) if rng.random() < 0.9 else [0] * 16)
    radius = rng.choice([bits(19.999998), bits(20.0), bits(5.84), bits(47.75), special(rng), rf(rng, 0, 60)])
    fx.push_model(count, radius, [(-1, [0] * 16)])
    has_model = rng.random() < 0.85
    pose = rng.choice([0xFF, 0xFF, rng.randrange(0, count or 1)])
    collapsed = rng.choice([-1, -1, rng.randrange(0, count or 1), 60])
    fx.push_owner(bone_count=count, model=has_model, pose_bone=pose, collapsed_bone=collapsed,
                  attachment=rng.choice([0, 0, 0x7D9530]), pos=[rf(rng, -500, 500) for _ in range(3)] + [ONE],
                  slots=list(range(count)))
    for i in range(64): fx.work[i] = fx.bone_ptr(i)
    work = 0x9F0000                             # D_00275B40 -> this slot array
    fx.o.put32(0x275B40, work)
    for i in range(64): fx.o.put32(work + 4 * i, fx.node_addr[i])
    fx.results.update(cull=rng.choice([-1, 0, 1, 5, 0x1F]), light=matrix(rng, -1, 1, sp),
                      color=matrix(rng, 0, 128, sp))
    vp = matrix(rng, -500, 500, sp)
    old = matrix(rng)
    for name, off, vals in (('s3AC0', 0x3AC0, vp), ('s3440', 0x3440, old), ('s3480', 0x3480, old),
                            ('s3400', 0x3400, old)):
        fx.o.put(SPR + off, struct.pack('<16I', *vals))
        C.memmove(C.addressof(getattr(fx.scratch, name)), struct.pack('<16I', *vals), 64)
    pattern = bytes((i * 7 + 3) & 255 for i in range(DL_SIZE))
    C.memmove(fx.dl, pattern, DL_SIZE)
    fx.o.put(DL, pattern)
    fx.o.put32(0x275670, CONTEXT)
    fx.o.put32(CONTEXT + 0x10, DL)
    fx.channels[0].cursor = C.addressof(fx.dl)
    fx.channels[0].end = C.addressof(fx.dl) + DL_SIZE
    r = LIB.em_owner_services_001CAA00(C.byref(fx.s), C.byref(fx.owner))
    fx.o.run(0x1CAA00, (fx.actor,))
    assert r == 0, (seed, r, hex(fx.s.fault.address), fx.s.fault.code)
    fx.compare(('1CAA00', seed), slots=count, bones=range(64), scratch=True)
    assert bytes(fx.dl) == fx.o.read(DL, DL_SIZE), ('dl bytes', seed)
    advanced = (fx.channels[0].cursor or 0) - C.addressof(fx.dl)
    assert fx.o.load(CONTEXT + 0x10) == DL + advanced, ('cursor', seed)
    fx.check_writes(('1CAA00', seed), SPR_ALLOWED + [(DL, DL + DL_SIZE), (CONTEXT + 0x10, CONTEXT + 0x14)])
    return ('draw', 1)


# ---------------------------------------------------------------- E. rumble

def case_rumble(args):
    effect, duration = args
    fx = Fixture(LIB, ELF)
    table = ELF_TABLE
    buf = (C.c_uint8 * len(table)).from_buffer_copy(table)
    fx.s.world.d0024D6F0 = C.cast(buf, C.POINTER(C.c_uint8))
    fx.s.world.d0024D6F0_count = len(table) // 4
    r = LIB.em_owner_services_001B1E20(C.byref(fx.s), effect, duration)
    fx.o.run(0x1B1E20, (effect & M64, duration & M64))
    assert r == 0
    fx.compare(('1B1E20', args))
    fx.check_writes(('1B1E20', args), [])
    return ('rumble', 1)


def case_countdown(args):
    gate, timer = args
    fx = Fixture(LIB, ELF)
    fx.e56.value, fx.e68.value = gate, timer
    fx.o.put(0x810E56, bytes([gate]))
    fx.o.put(0x810E68, struct.pack('<h', timer))
    r = LIB.em_owner_services_001B5B70(C.byref(fx.s))
    fx.o.run(0x1B5B70)
    assert r == 0
    assert struct.pack('<h', fx.e68.value) == fx.o.read(0x810E68, 2), (args, fx.e68.value)
    fx.compare(('1B5B70', args))
    fx.check_writes(('1B5B70', args), [(0x810E68, 0x810E6A)])
    return ('countdown', 1)


# ---------------------------------------------------------------- F. captures

def u32(ram, a): return struct.unpack_from('<I', ram, a)[0]


def captured_owners(ram):
    out, a, seen = [], u32(ram, 0x275BC0), set()
    while a and a not in seen:
        seen.add(a)
        if u32(ram, a + 0x44) and 0 < ram[a + 0xC] <= MAX_BONES: out.append(a)
        a = u32(ram, a + 0x1C)
    return out


def load_captured(fx, ram, actor):
    """Native twin of a captured owner, its model and its nodes."""
    ow, count = fx.owner, ram[actor + 0xC]
    model = u32(ram, actor + 0x44)
    fx.model_addr = model
    for name, off, size in OWNER_BYTES:
        setattr(ow, name, int.from_bytes(ram[actor + off:actor + off + size], 'little'))
    for name, off, n in OWNER_FLOATS:
        C.memmove(C.addressof(getattr(ow, name)), ram[actor + off:actor + off + 4 * n], 4 * n)
    skel = model + u32(ram, model + 0xC)
    fx.model.bone_count = ram[model + 8]
    C.memmove(C.addressof(fx.model) + Model.radius.offset, ram[model + 0x20:model + 0x24], 4)   # raw bits
    fx.model.skeleton = C.cast(fx.records, C.POINTER(Record))
    fx.model.skeleton_records = count
    for i in range(count):
        fx.records[i].parent = struct.unpack_from('<h', ram, skel + 0x50 * i + 4)[0]
        C.memmove(C.addressof(fx.records[i].bind), ram[skel + 0x50 * i + 0x10:skel + 0x50 * i + 0x50], 64)
    ow.model = C.pointer(fx.model)
    for i in range(count):
        node = u32(ram, actor + 0x110 + 4 * i)
        fx.node_addr[i] = node
        b = fx.bones[i]
        C.memmove(C.addressof(b.bind), ram[node:node + 64], 64)
        b.parent = struct.unpack_from('<h', ram, node + 0x64)[0]
        C.memmove(C.addressof(b.rot), ram[node + 0x70:node + 0x7C], 12)
        C.memmove(C.addressof(b.trans), ram[node + 0x7C:node + 0x88], 12)
        C.memmove(C.addressof(b.scale), ram[node + 0x88:node + 0x8E], 6)
        C.memmove(C.addressof(b.world), ram[node + 0x90:node + 0xD0], 64)
        ow.bone[i] = fx.bone_ptr(i)
    return count


def case_capture(args):
    name, actor = args
    ram = CAPTURE_RAM[name]
    spr = CAPTURE_SPR.get(name)
    fx = Fixture(LIB, ELF, ram, spr, actor=actor)
    count = load_captured(fx, ram, actor)
    stats = {}
    # 1. bone_init_default_1 over the captured model records.
    fx.o.run(0x1C62C0, (actor,))
    LIB.em_owner_services_001C62C0(C.byref(fx.s), C.byref(fx.owner))
    fx.compare(('capture bone_init', name, hex(actor)), slots=count, bones=range(count))
    # 2. 001C6380 over the (re-initialised) owner: native == executed original.
    fx2 = Fixture(LIB, ELF, ram, spr, actor=actor)
    load_captured(fx2, ram, actor)
    C.memmove(C.addressof(fx2.scratch.s3400), fx2.o.read(SPR + 0x3400, 64), 64)
    fx2.o.run(0x1C6380, (actor,))
    LIB.em_owner_services_001C6380(C.byref(fx2.s), C.byref(fx2.owner))
    fx2.compare(('capture place', name, hex(actor)), slots=count, bones=range(count))
    assert bytes(fx2.scratch.s3400) == fx2.o.read(SPR + 0x3400, 64)
    stats['world_reproduced'] = int(bytes(fx2.owner.world) == ram[actor + 0xD0:actor + 0x110])
    stats['nodes_reproduced'] = sum(bytes(fx2.bones[i].world) == ram[fx2.node_addr[i] + 0x90:fx2.node_addr[i] + 0xD0]
                                    for i in range(count))
    stats['nodes'] = count
    stats['bind_default'] = sum(
        bytes(fx.bones[i].bind) == ram[fx.node_addr[i]:fx.node_addr[i] + 64] and
        ram[fx.node_addr[i] + 0x70:fx.node_addr[i] + 0x88] == bytes(24) and
        ram[fx.node_addr[i] + 0x88:fx.node_addr[i] + 0x8E] == struct.pack('<3h', 0x1000, 0x1000, 0x1000)
        for i in range(count))
    return (name, hex(actor), u32(ram, actor + 0x10), stats)


# Owners whose captured +0xD0 and node +0x90 must be reproduced by running
# 001C6380 over their captured +0xB0/+0xC0/+0x60 and node locals (every one
# calls 001C6380 as its last placement in the captured states; measured over
# all five images). The truck only while wedged (+0x04 == 4): at rest (state
# 2) its +0xD0 is the fall result, not a 001C6380 placement.
REPRODUCED = {0x1551B0: 'crate', 0x156620: 'drum', 0x159210: 'panel', 0x15AFA0: 'pickup 0B',
              0x1C4820: 'prop 1C4820', 0x1C5680: 'indicator', 0x219550: 'pickup', 0x823E80: 'parachute',
              0x825940: 'husk creature', 0x827490: 'husk partner', 0x827630: 'fan',
              0x827B10: 'elevator terminal'}
PLAYER = 0x8102B0


def palette_candidates(ram, count):
    """Colour packet addresses of every captured count-bone 001C7420 upload."""
    chunk = min(8 * count, 0xF8)
    unpack = struct.pack('<I', 0x6C000000 | chunk << 16)
    out, i = [], ram.find(unpack)
    while i != -1:
        tag = i - 0x1C
        if tag % 16 == 0 and u32(ram, tag) & 0x7000FFFF == 0x10000000 | (chunk + 1):
            ct = tag - 0x60
            if (u32(ram, ct) & 0x7000FFFF == 0x10000005 and u32(ram, ct + 0x14) == 0x11000000
                    and u32(ram, ct + 0x1C) == 0x6C0403F5):
                out.append(ct)
        i = ram.find(unpack, i + 1)
    return out


def case_capture_palette(name):
    """001C7420 over the captured player (the frame's last draw): native ==
    executed original, and one captured DL upload reproduced byte for byte,
    with SPR 0x70003480 (C) and the zeroed 0x70003440 (B) as captured."""
    ram, spr = CAPTURE_RAM[name], CAPTURE_SPR[name]
    count = ram[PLAYER + 0xC]
    total = 8 * count
    size = 0x60 + (total + 2 * ((total + 0xF7) // 0xF8)) * 16
    reproduced = oracle_equal = 0
    for ct in palette_candidates(ram, count):
        fx = Fixture(LIB, ELF, ram, spr, actor=PLAYER)
        load_captured(fx, ram, PLAYER)
        fx.results['light'] = list(struct.unpack_from('<16I', spr, 0x3400))
        fx.results['color'] = list(struct.unpack_from('<16I', ram, ct + 0x20))
        for field, off in (('s3400', 0x3400), ('s3440', 0x3440), ('s3480', 0x3480), ('s3AC0', 0x3AC0)):
            C.memmove(C.addressof(getattr(fx.scratch, field)), spr[off:off + 64], 64)
        buf = (C.c_uint8 * size).from_buffer_copy(ram[ct:ct + size])
        fx.channels[0].cursor, fx.channels[0].end = C.addressof(buf), C.addressof(buf) + size
        first = C.c_void_p()
        r = LIB.em_owner_services_001C7420(C.byref(fx.s), C.byref(fx.owner), 0x3F5, 0, C.byref(first))
        assert r == 0 and first.value == C.addressof(buf), (name, hex(ct), r)
        context = u32(ram, 0x275670)
        fx.o.put32(context + 0x10, ct)
        fx.o.run(0x1C7420, (PLAYER, 0x3F5, 0))
        assert fx.o.g(2) & M32 == ct and fx.o.load(context + 0x10) == ct + size, (name, hex(ct))
        assert bytes(buf) == fx.o.read(ct, size), ('capture palette oracle', name, hex(ct))
        fx.compare(('capture palette', name, hex(ct)), scratch=True)
        fx.check_writes(('capture palette', name), SPR_ALLOWED + [(ct, ct + size), (context + 0x10, context + 0x14)])
        oracle_equal += 1
        if bytes(buf) == ram[ct:ct + size]:
            assert bytes(fx.scratch.s3480) == spr[0x3480:0x34C0], (name, 'C differs from the capture')
            assert bytes(fx.scratch.s3440) == spr[0x3440:0x3480], (name, 'B differs from the capture')
            reproduced += 1
    assert reproduced >= 1, ('no captured player upload reproduced', name)
    return (name, oracle_equal, reproduced, size)


# ---------------------------------------------------------------- main

LIB = ELF = ELF_TABLE = None
CAPTURE_RAM, CAPTURE_SPR = {}, {}


def main():
    global LIB, ELF, ELF_TABLE
    started = time.time()
    ELF = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(ELF).hexdigest() == ELF_SHA, 'not the pinned SCUS-97112 ELF'
    LIB = build_library()
    # D_00241100 must equal the module's coefficients (bytes compared, not stored).
    coeff = ELF[0x241100 - 0x100000 + 0x300:0x241110 - 0x100000 + 0x300]
    native = struct.pack('<4I', 0x362E9C14, 0xB94FB21F, 0x3C08873E, 0xBE2AAAA4)
    assert coeff == native, 'D_00241100 differs from the translated coefficients'
    # D_0024D6F0: 16 four-byte records read from the user's ELF at run time
    # (record 11 is all zero; 12..15 follow it). The original has no bound.
    t0 = 0x24D6F0 - 0x100000 + 0x300
    ELF_TABLE = ELF[t0:t0 + 4 * 16]

    counts = {}

    def tally(results):
        for r in results:
            counts[r[0]] = counts.get(r[0], 0) + r[1]

    # A. binding
    bind_cases = [(s, e) for e in (0x1B0FD0, 0x1B0EA0, 0x1B1020, 0x1B0DC0) for s in range(pick(400, 30))]
    tally(parallel_map(case_bind, bind_cases))
    tally(parallel_map(case_bone_init, range(pick(200, 12))))
    # B. placement and the SDK
    place = [(s, c) for s in range(pick(240, 16)) for c in ((0, 1, 2, 3, 21, 56)[s % 6],)]
    tally(parallel_map(case_place, place, cost=lambda x: x[1]))
    tally(parallel_map(case_sdk, range(pick(3000, 200))))
    # C. publication: the whole gate matrix is small enough to run in full.
    pubs = [(mode, cls, kind, mid, f2, vis, posi) for mode in (5, 6)
            for cls in (0x02, 0x82, 0x08, 0x0A, 0x07, 0x04, 0xE2, 0x27, 0x0D)
            for kind in (0, 1, 7, 2) for mid in (0, 1) for f2 in (0x2C, 0x2D, 0xFFFF)
            for vis in (0, 1, 0xFF) for posi in (0,)]
    pubs = select(pubs, 400, 0x1B17A0, axes=(lambda p: p[:2], lambda p: p[2:4], lambda p: p[4:6]))
    # Special f12..f14 bits, every position in both modes (mode 6 also reaches
    # the 001B1CE0 gate with class 0x0A / kind 1), with 001B1B70 on and off.
    pubs += [(mode, 0x0A, 1, 0, 0x2C, (0, 1)[i % 2], i) for mode in (5, 6) for i in range(1, len(PUBLISH_POS))]
    tally(parallel_map(case_publish, pubs))
    # D. draw
    draws = [(s, c) for s in range(pick(300, 24)) for c in ((0, 1, 3, 21, 31, 32, 56)[s % 7],)]
    tally(parallel_map(case_draw, draws, cost=lambda x: x[1]))
    # E. rumble
    rows = len(ELF_TABLE) // 4
    durations = [-1, -0x8000_0000, -(1 << 40), 0, 1, 0x7FFF, 0x8000, 0xFFFF, 0x12345, 1 << 40, (1 << 40) | 0x8001]
    tally(parallel_map(case_rumble, [(e, d) for e in range(rows) for d in durations]))
    tally(parallel_map(case_countdown, [(g, t) for g in (0, 1, 0xFF) for t in (0, 1, -1, 5, 0x7FFF, -0x8000)]))

    # F. captures
    base = DECOMP / 'build'
    capture_items = []
    for name in CAPTURES:
        path = base / name
        if not path.exists(): continue
        ram = CAPTURE_RAM[name] = path.read_bytes()
        spr_path = path.parent / ('scratchpad.bin' if 's87' in name else 'opening_scratchpad.bin')
        if 's87' in name and spr_path.exists(): CAPTURE_SPR[name] = spr_path.read_bytes()
        for lo, hi in ((0x1B0DC0, 0x1B1100), (0x1C62C0, 0x1C6440), (0x1C9610, 0x1C9940),
                       (0x102918, 0x102CA8), (0x1C7420, 0x1C7900)):
            assert ram[lo:hi] == ELF[lo - 0x100000 + 0x300:hi - 0x100000 + 0x300], ('code differs', name, hex(lo))
        capture_items += [(name, a) for a in captured_owners(ram)]
    capture_items = select(capture_items, 40, 0xC0DE, axes=(lambda i: i[0], lambda i: u32(CAPTURE_RAM[i[0]], i[1] + 0x10)))
    cap_results = parallel_map(case_capture, capture_items, cost=lambda i: CAPTURE_RAM[i[0]][i[1] + 0xC])
    reproduced = {}
    for name, actor, behaviour, st in cap_results:
        key = hex(behaviour)
        agg = reproduced.setdefault(key, dict(owners=0, world=0, nodes=0, node_total=0, bind_default=0))
        agg['owners'] += 1; agg['world'] += st['world_reproduced']
        agg['nodes'] += st['nodes_reproduced']; agg['node_total'] += st['nodes']
        agg['bind_default'] += st['bind_default']
    counts['capture_owners'] = len(cap_results)
    for name, actor, behaviour, st in cap_results:
        wedged_truck = behaviour == 0x823FF0 and CAPTURE_RAM[name][int(actor, 16) + 4] == 4
        if behaviour in REPRODUCED or wedged_truck:
            assert st['world_reproduced'] and st['nodes_reproduced'] == st['nodes'], \
                ('captured placement not reproduced', name, actor, hex(behaviour), st)
    palettes = parallel_map(case_capture_palette, [n for n in CAPTURES if n in CAPTURE_SPR])
    counts['capture_palettes'] = sum(r[2] for r in palettes)
    counts['capture_palette_candidates'] = sum(r[1] for r in palettes)

    line = banner(part(counts.get('0x1b0fd0', 0) + counts.get('0x1b0ea0', 0) + counts.get('0x1b1020', 0)
                       + counts.get('0x1b0dc0', 0), 1600, 'bind cases'),
                  part(counts.get('place', 0), 240, 'placements'), part(counts.get('sdk', 0), 3000, 'SDK sets'),
                  part(counts.get('draw', 0) + 0, 300, 'draws'), f"{counts['capture_owners']} captured owners",
                  f"{counts['capture_palettes']} captured palette uploads")
    report = dict(status='PASS', mode=line, elf_sha256=ELF_SHA, counts=counts,
                  capture_reproduction=reproduced, seconds=round(time.time() - started, 1))
    (OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    sys.exit(main())
