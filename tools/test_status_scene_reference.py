#!/usr/bin/env python3
"""Compare the native status scene workers (em_status_scene_original.c) with
the ORIGINAL instructions of the user's pinned boot ELF.

Executed original functions: the static actor pool 001AFF10, 001AFF90,
001AF800, 001AFEB0, 001AFE60 and 001B0000; the status models 0020E1E0,
0020E250, 0020E3A0, 0020E460, 0020E6F0, 0020EC80 (with 001031E0) and
001F4BF0; 001FF080 + 001AB740 (load request); 001FF0D0 + 001FF830 + 001FF3F0
+ 001FEF70 + 001AB7D0 (the slot-2 module loader); and (for the capture
checks only) the model lookup 001C6120. Everything else they call is a
recorded worker. Every worker call and argument, every modelled byte, and
the set of bytes the original writes are compared. COP1 arithmetic follows
tools/ee_float_model.py (docs/EE_FLOAT_MODEL.md).

Capture checks (the user's own RAM images under
../Extermination/build/startup-reference and build/s87/route) tie the
translation to the running original: the status letter actors and the menu
player in the static pool D_0028B020, the slot-2 loader record, and the
route 01/03 ITEM-root load wait. (The status background 0020A7A0 is
em_status_background.c, checked by tools/test_status_background_reference.py.)

No original instruction bytes, disassembly or data are written by this file;
the report in build/ holds only counts.
"""
import ctypes as C
import hashlib
import itertools
import json
from pathlib import Path
import random
import struct
import subprocess
import time

import ee_float_model as M
from reference_mode import FULL, banner, pick, select
from test_interaction_scan_reference import DECOMP, ELF_SHA, ScanOracle
from test_point_light_reference import RETURN, STACK, bits, number, signed

ROOT = Path(__file__).resolve().parents[1]
MASK = 0xFFFFFFFF
REF = DECOMP / 'build/startup-reference'
ROUTE = DECOMP / 'build/s87/route'
ACTOR, MODEL = 0x910000, 0x960000
SLOT, SPAD_SLOT, SPAD_3B90 = 0x28A790, 0x70003B6C, 0x70003B90
HEADER, BG = 0x289BC0, 0x2655A0
FIXED = 0x01800000


# ------------------------------------------------------------------ oracle --

class SceneOracle(ScanOracle):
    """The shared interpreter plus the ops these functions use: EE COP1 per
    ee_float_model, div/mfhi/mflo, srlv, movn, xori, dsll32/dsra32, and the
    REGIMM/COP1 branch-likely forms."""

    def __init__(self, elf):
        super().__init__(elf)
        self.hi = self.lo = 0

    def plain(self, word):
        op, rs, rt, rd = word >> 26, word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        fn, r, f = word & 63, self.r, self.f
        if op == 17 and rs == 16:
            fs, ft, fd = rd, rt, word >> 6 & 31
            a, b = f[fs] & MASK, f[ft] & MASK
            if fn == 0: f[fd] = M.ee_add(a, b)
            elif fn == 1: f[fd] = M.ee_sub(a, b)
            elif fn == 2: f[fd] = M.ee_mul(a, b)
            elif fn == 3: f[fd] = M.ee_div(a, b)
            elif fn == 6: f[fd] = M.ee_mov(a)
            elif fn == 7: f[fd] = M.ee_neg(a)
            elif fn == 36: f[fd] = M.ee_cvt_w_s(a)
            elif fn == 50: self.condition = bool(M.ee_c_eq(a, b))
            elif fn == 52: self.condition = bool(M.ee_c_lt(a, b))
            elif fn == 54: self.condition = bool(M.ee_c_le(a, b))
            else: raise AssertionError(('COP1.S', fn))
            return
        if op == 17 and rs == 20 and fn == 32:
            f[word >> 6 & 31] = M.ee_cvt_s_w(f[rd] & MASK)
            return
        if op == 0 and fn == 24:  # mult rd, rs, rt: LO (and rd) = the low word, HI the high
            product = signed(r[rs]) * signed(r[rt])
            self.lo, self.hi = product & MASK, (product >> 32) & MASK
            if rd: r[rd] = self.lo
            return
        if op == 0 and fn == 56:  # dsll
            r[rd] = (r[rt] << (word >> 6 & 31)) & (2**64 - 1)
            return
        if op == 0 and fn in (6, 11, 16, 18, 26, 27, 39, 60, 62, 63):
            sa = word >> 6 & 31
            if fn == 6: r[rd] = (r[rt] & MASK) >> (r[rs] & 31)
            elif fn == 11:
                if r[rt] & MASK: r[rd] = r[rs]
            elif fn == 16: r[rd] = self.hi
            elif fn == 18: r[rd] = self.lo
            elif fn == 26:
                a, b = signed(r[rs]), signed(r[rt])
                if b:
                    q = abs(a) // abs(b) * (1 if (a < 0) == (b < 0) else -1)
                    self.lo, self.hi = q & MASK, (a - q * b) & MASK
            elif fn == 27:
                a, b = r[rs] & MASK, r[rt] & MASK
                if b: self.lo, self.hi = a // b, a % b
            elif fn == 39: r[rd] = ~(r[rs] | r[rt]) & MASK
            elif fn == 60: r[rd] = (r[rt] << (sa + 32)) & (2**64 - 1)
            elif fn == 62: r[rd] = ((r[rt] & (2**64 - 1)) >> (sa + 32))
            elif fn == 63: r[rd] = (signed(r[rt], 64) >> (sa + 32)) & (2**64 - 1)
            r[0] = 0
            return
        if op == 14:
            r[rt] = (r[rs] & MASK) ^ (word & 0xFFFF)
            r[0] = 0
            return
        if op == 32:  # lb
            r[rt] = signed(self.load((r[rs] + signed(word & 65535, 16)) & MASK, 1), 8) & MASK
            r[0] = 0
            return
        super().plain(word)

    def run(self, entry, args=(), floats=(), stop=RETURN, limit=3_000_000):
        self.r[31] = RETURN
        for i, value in enumerate(args): self.r[4 + i] = value
        for i, value in enumerate(floats): self.f[12 + i] = bits(value)
        pc = entry
        for _ in range(limit):
            if pc == stop: return
            word = self.load(pc)
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            offset = signed(word & 65535, 16) * 4
            indirect = op == 0 and word & 63 == 9
            if op in (2, 3) or indirect:
                target = self.r[rs] & MASK if indirect else (word & 0x3FFFFFF) * 4
                if indirect: self.r[word >> 11 & 31] = pc + 8
                elif op == 3: self.r[31] = pc + 8
                self.plain(self.load(pc + 4))
                if target in self.calls:
                    self.calls[target](self)
                    pc = pc + 8 if op == 3 or indirect else self.r[31] & MASK
                else:
                    pc = target
                continue
            branch = likely = None
            if op in (4, 5, 20, 21):
                taken = ((self.r[rs] & MASK) == (self.r[rt] & MASK)) == (op in (4, 20))
                likely = op in (20, 21)
            elif op in (6, 7, 22, 23):
                v = signed(self.r[rs])
                taken = v <= 0 if op in (6, 22) else v > 0
                likely = op in (22, 23)
            elif op == 1:
                assert rt in (0, 1, 2, 3), ('REGIMM', rt)
                v = signed(self.r[rs])
                taken = v < 0 if rt in (0, 2) else v >= 0
                likely = rt in (2, 3)
            elif op == 17 and rs == 8:
                taken = self.condition == bool(rt & 1)
                likely = bool(rt & 2)
            elif op == 0 and word & 63 == 8:
                target = self.r[rs] & MASK
                self.plain(self.load(pc + 4))
                pc = target
                continue
            else:
                try: self.plain(word)
                except AssertionError as error: raise AssertionError(hex(pc), error) from error
                pc += 4
                continue
            if likely and not taken:
                pc += 8
                continue
            self.plain(self.load(pc + 4))
            pc = pc + 4 + offset if taken else pc + 8
        raise AssertionError(('original routine did not return', hex(entry)))


def fresh(elf):
    o = SceneOracle(elf)
    o.r[28], o.r[29] = 0x27D370, STACK
    return o


def changed_bytes(o, before):
    return {a for a in set(o.mem) | set(before)
            if o.mem.get(a, 0) != before.get(a, 0) and not STACK - 0x2000 <= a < STACK + 0x10}


def span(address, size):
    return set(range(address, address + size))


# ------------------------------------------------------------------ native --

class Actor(C.Structure):
    _fields_ = [(f'b{i:02X}', C.c_uint8) for i in range(16)] + [
        ('w10', C.c_uint32), ('w14', C.c_uint32), ('h36', C.c_uint16), ('f38', C.c_float),
        ('w40', C.c_uint32), ('w44', C.c_uint32), ('w4C', C.c_uint32), ('f60', C.c_float * 4),
        ('f80', C.c_float * 4), ('w90', C.c_uint32), ('h94', C.c_int16), ('b98', C.c_uint8),
        ('b99', C.c_uint8), ('b9A', C.c_uint8), ('fB0', C.c_float * 3), ('fC0', C.c_float * 3),
        ('w110', C.c_uint32 * 120)]


class Pool(C.Structure):
    _fields_ = [('record', Actor * 24)]


class Scratch(C.Structure):
    _fields_ = [('s3400', C.c_float * 16), ('s3440', C.c_float * 16), ('s36A0', C.c_float * 16),
                ('s38A0', C.c_uint32 * 4), ('s38B0', C.c_uint32 * 4)]


class LetterGlobals(C.Structure):
    _fields_ = [('d275BCC', C.c_int16), ('d275B40', C.POINTER(C.c_uint32)), ('view', C.c_float * 12)]


class PlayerGlobals(C.Structure):
    _fields_ = [('d8104E4', C.c_uint8), ('d810C60', C.c_uint8)] + [
        (n, C.c_uint32) for n in ('d28A57C', 'd28A580', 'd28A584', 'd28A588', 'd28A58C', 'd28A590')] + [
        ('view', C.c_float * 12), ('health', C.c_float), ('infection', C.c_float)]


class Loader(C.Structure):
    _fields_ = [(n, C.c_uint8) for n in ('d282157', 'd275BD8', 'spad3B90', 'd810CA4', 'd810CA6')] + [
        (n, C.c_uint32) for n in ('d275C70', 'd275C74', 'd28A5A0', 'd28A738', 'd28A73C',
                                   'd28A744', 'd28A748')] + [
        ('d28A490', C.c_uint32 * 68), ('header', C.c_uint8 * 0x800)]


class Fault(C.Structure):
    _fields_ = [('address', C.c_uint32), ('code', C.c_int32)]


PA = C.POINTER(Actor)
I32P, U32P, FP = C.POINTER(C.c_int32), C.POINTER(C.c_uint32), C.POINTER(C.c_float)
U8P = C.POINTER(C.c_uint8)
WORKER_TYPES = [
    ('w_001AF800_slot', C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32)),
    ('w_001CB590', C.CFUNCTYPE(C.c_int, C.c_void_p, PA, C.c_int32, C.c_uint8)),
    ('w_call', C.CFUNCTYPE(C.c_int, C.c_void_p, PA, C.c_uint32)),
    ('w_001AFF10', C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(PA))),
    ('w_001C6120', C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_int32, U32P)),
    ('w_001CA6E0', C.CFUNCTYPE(C.c_int, C.c_void_p, PA, C.c_uint32)),
    ('w_001C6150', C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, U32P)),
    ('w_001AF7C0', C.CFUNCTYPE(C.c_int, C.c_void_p, U32P)),
    ('w_001CB5B0', C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32)),
    ('w_001C63E0', C.CFUNCTYPE(C.c_int, C.c_void_p, PA, C.c_int32)),
    ('w_001C62C0', C.CFUNCTYPE(C.c_int, C.c_void_p, PA)),
    ('w_001CA5F0', C.CFUNCTYPE(C.c_int, C.c_void_p, PA, C.c_int32)),
    ('w_001C67E0', C.CFUNCTYPE(C.c_int, C.c_void_p, PA, C.c_int32, C.c_float, C.c_float)),
    ('w_001C64F0', C.CFUNCTYPE(C.c_int, C.c_void_p, PA, C.c_float)),
    ('w_0020EC80', C.CFUNCTYPE(C.c_int, C.c_void_p, PA)),
    ('w_001AFF90', C.CFUNCTYPE(C.c_int, C.c_void_p, PA)),
    ('w_001C6380', C.CFUNCTYPE(C.c_int, C.c_void_p, PA)),
    ('w_001D2040', C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int32, C.c_int32)),
    ('w_001029C0', C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32)),
    ('w_00102B08', C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_uint32, C.c_uint32)),
    ('w_00102BB0', C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_uint32, C.c_uint32)),
    ('w_00102A60', C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_uint32, C.c_uint32)),
    ('w_001026D0', C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_uint32, C.c_uint32)),
    ('w_001026A0', C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_uint32, C.c_uint32)),
    ('w_001C69A0', C.CFUNCTYPE(C.c_int, C.c_void_p, PA)),
    ('w_00122BB8', C.CFUNCTYPE(C.c_int, C.c_void_p, I32P)),
    ('w_001CD520', C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int32, C.c_int32, C.c_uint32, C.c_uint64,
                               C.c_uint32, C.c_float, C.c_float, C.c_float)),
    ('w_001FFCD0', C.CFUNCTYPE(C.c_int, C.c_void_p, U8P)),
    ('w_00200360', C.CFUNCTYPE(C.c_int, C.c_void_p, U8P)),
    ('w_00200780', C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_uint32, C.c_int32, C.c_int32,
                               U8P)),
    ('w_00200730', C.CFUNCTYPE(C.c_int, C.c_void_p, I32P)),
    ('w_00200830', C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32)),
    ('w_001FB370', C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, U32P)),
]
WT = dict(WORKER_TYPES)


class Workers(C.Structure):
    _fields_ = [('ctx', C.c_void_p)] + WORKER_TYPES


def workers(**fns):
    """Workers from Python callables (kept alive on the struct) or raw
    C function addresses (ints)."""
    w = Workers()
    keep = []
    for name, fn in fns.items():
        if fn is None:
            continue
        cb = WT[name](fn)
        keep.append(cb)
        setattr(w, name, cb)
    w._keep = keep
    return w


def build():
    out = ROOT / 'build/status_scene_reference'
    out.mkdir(parents=True, exist_ok=True)
    lib_path = out / 'status_scene.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-shared', '-fPIC', '-Isrc', 'src/game/em_status_scene_original.c',
                    '-o', str(lib_path)], cwd=ROOT, check=True)
    lib = C.CDLL(str(lib_path))
    lib.em_status_scene_glyph_0020E3A0.argtypes = [C.c_int32]
    lib.em_status_scene_glyph_0020E3A0.restype = C.c_int32
    lib.em_status_scene_letter_0020E1E0.argtypes = [C.c_int32, C.c_uint32, C.POINTER(Workers),
                                                    C.POINTER(Fault)]
    lib.em_status_scene_letters_0020E250.argtypes = [C.c_uint8 * 4, C.c_uint32, C.POINTER(Workers),
                                                     C.POINTER(Fault)]
    lib.em_status_scene_player_0020E6F0.argtypes = [PA, C.POINTER(PlayerGlobals),
                                                    C.POINTER(Workers), C.POINTER(Fault)]
    lib.em_status_scene_letter_0020E460.argtypes = [PA, C.POINTER(LetterGlobals),
                                                    C.POINTER(Workers), C.POINTER(Fault)]
    lib.em_status_scene_publish_0020EC80.argtypes = [PA, C.c_uint8, C.POINTER(Scratch),
                                                     C.POINTER(Workers), C.POINTER(Fault)]
    lib.em_status_scene_glow_001F4BF0.argtypes = [C.c_uint32, C.c_uint32 * 4, C.POINTER(Workers),
                                                  C.POINTER(Fault)]
    lib.em_status_scene_alloc_001AFF10.argtypes = [C.POINTER(Pool)]
    lib.em_status_scene_alloc_001AFF10.restype = PA
    lib.em_status_scene_free_001AFF90.argtypes = [C.POINTER(Pool), PA, C.POINTER(Workers),
                                                  C.POINTER(Fault)]
    lib.em_status_scene_release_001AFEB0.argtypes = [C.POINTER(Pool), C.POINTER(Workers),
                                                     C.POINTER(Fault)]
    lib.em_status_scene_clear_001AFE60.argtypes = [C.POINTER(Pool)]
    lib.em_status_scene_clear_001AFE60.restype = None
    lib.em_status_scene_walk_001B0000.argtypes = [C.POINTER(Pool), C.POINTER(Workers),
                                                  C.POINTER(Fault)]
    lib.em_status_scene_loader_request_001FF080.argtypes = [U8P, U8P, C.c_uint8, C.c_uint8]
    lib.em_status_scene_loader_request_001FF080.restype = None
    lib.em_status_scene_loader_001FF0D0.argtypes = [U8P, U8P, C.POINTER(Loader),
                                                    C.POINTER(Workers), C.POINTER(Fault)]
    lib.em_status_scene_bank_001FEF70.argtypes = [C.c_uint8, C.c_uint8]
    return out, lib


def fn_address(lib, name):
    return C.cast(getattr(lib, name), C.c_void_p).value


def f32(value): return number(bits(value))
def up(value): return number(bits(value) + (1 if value >= 0 else -1)) if value else number(1)
def down(value): return number(bits(value) - (1 if value > 0 else -1)) if value else number(0x80000001)


# ------------------------------------------------------- A/B/C: pure parts --

def check_glyph(elf, lib):
    codes = list(range(-40, 64)) + [0x7FFFFFFF, -0x80000000, 0x7FFF, -129, 255, 256, 0x10015]
    for code in codes:
        o = fresh(elf)
        o.run(0x20E3A0, (code & MASK,))
        assert o.r[2] & MASK == lib.em_status_scene_glyph_0020E3A0(code) & MASK, ('glyph', code)
    return len(codes)


# ---------------------------------------------------- D/E: letter models ---

ACTOR_FIELDS = dict([(f'b{i:02X}', (i, 1)) for i in range(16)] + [
    ('w10', (0x10, 4)), ('w14', (0x14, 4)), ('h36', (0x36, 2)), ('f38', (0x38, 4)),
    ('w40', (0x40, 4)), ('w44', (0x44, 4)), ('w4C', (0x4C, 4)), ('w90', (0x90, 4)),
    ('h94', (0x94, 2)), ('b98', (0x98, 1)), ('b99', (0x99, 1)), ('b9A', (0x9A, 1))])
ACTOR_ARRAYS = (('f60', 0x60, 4), ('f80', 0x80, 4), ('fB0', 0xB0, 3), ('fC0', 0xC0, 3))


def field_bits(v, size):
    return bits(v) if isinstance(v, float) else v & ((1 << (8 * size)) - 1)


def actor_bits(a):
    out = [field_bits(getattr(a, name), size) for name, (off, size) in ACTOR_FIELDS.items()]
    for name, off, n in ACTOR_ARRAYS: out += [bits(x) for x in getattr(a, name)]
    return out


def oracle_actor_bits(o, base):
    out = [o.load(base + off, size) for off, size in ACTOR_FIELDS.values()]
    for name, off, n in ACTOR_ARRAYS: out += [o.load(base + off + 4 * i) for i in range(n)]
    return out


def push_actor(o, base, a):
    for name, (off, size) in ACTOR_FIELDS.items():
        o.save(base + off, field_bits(getattr(a, name), size), size)
    for name, off, n in ACTOR_ARRAYS:
        for i in range(n): o.save(base + off + 4 * i, bits(getattr(a, name)[i]))
    for i in range(120): o.save(base + 0x110 + 4 * i, a.w110[i])


ACTOR_BYTES = set()
for _off, _size in ACTOR_FIELDS.values(): ACTOR_BYTES |= span(_off, _size)
for _name, _off, _n in ACTOR_ARRAYS: ACTOR_BYTES |= span(_off, 4 * _n)
RECORD_BYTES = ACTOR_BYTES | span(0x110, 4 * 120)


class LetterRig:
    """0020E1E0/0020E250 over a pool of up to 8 records; alloc_plan lists,
    per 001AFF10 call, the record index or None (pool full)."""

    def __init__(self, elf, alloc_plan, bones=1, bank=0xBAA1C0):
        self.o = fresh(elf)
        self.plan, self.bones, self.bank = list(alloc_plan), bones, bank
        self.expected, self.actual = [], []
        self.actors = [Actor() for _ in range(8)]
        self.o_calls = 0
        self.n_calls = 0
        o = self.o
        o.save(0x28A56C, bank)
        o.calls.update({0x1AFF10: self.o_alloc, 0x1C6120: self.o_lookup,
                        0x1CA6E0: self.o_bind, 0x1C6150: self.o_bones})

    def model_of(self, code): return (MODEL + (code & 0xFFFF) * 0x40) & MASK

    def o_alloc(self, o):
        slot = self.plan[self.o_calls]; self.o_calls += 1
        self.expected.append(('alloc', slot))
        o.r[2] = 0 if slot is None else ACTOR + slot * 0x2F0

    def o_lookup(self, o):
        self.expected.append(('lookup', o.r[4] & MASK, o.r[5] & MASK))
        o.r[2] = self.model_of(o.r[5])

    def o_bind(self, o):
        slot = ((o.r[4] & MASK) - ACTOR) // 0x2F0
        self.expected.append(('bind', slot, o.r[5] & MASK))
        o.save((o.r[4] & MASK) + 0x44, o.r[5] & MASK)

    def o_bones(self, o):
        self.expected.append(('bones', o.r[4] & MASK))
        o.r[2] = self.bones

    def native_workers(self):
        def alloc(_, out):
            slot = self.plan[self.n_calls]; self.n_calls += 1
            self.actual.append(('alloc', slot))
            out[0] = PA() if slot is None else C.pointer(self.actors[slot])
            return 0

        def lookup(_, bank, code, out):
            self.actual.append(('lookup', bank, code & MASK))
            out[0] = self.model_of(code & MASK)
            return 0

        def bind(_, actor, model):
            slot = next(i for i, a in enumerate(self.actors)
                        if C.addressof(a) == C.addressof(actor.contents))
            self.actual.append(('bind', slot, model))
            actor.contents.w44 = model
            return 0

        def bones(_, word, out):
            self.actual.append(('bones', word))
            out[0] = self.bones
            return 0
        return workers(w_001AFF10=alloc, w_001C6120=lookup, w_001CA6E0=bind, w_001C6150=bones)

    def compare(self, label, before):
        allowed = set()
        for i in range(8): allowed |= {ACTOR + i * 0x2F0 + b for b in ACTOR_BYTES}
        assert changed_bytes(self.o, before) <= allowed, (label, 'unmodelled write')
        assert self.actual == self.expected, (label, self.actual, self.expected)
        for i, a in enumerate(self.actors):
            assert actor_bits(a) == oracle_actor_bits(self.o, ACTOR + i * 0x2F0), (label, i)


def check_letters(elf, lib):
    n = 0
    for code, slot, bones, bank in itertools.product(
            (0x2F, 0x30, 0x40, 0x6D, -1, 0x1FF), (0, None), (0, 1, 21, 0x1FF), (0xBAA1C0, 0)):
        rig = LetterRig(elf, [slot], bones, bank)
        before = dict(rig.o.mem)
        rig.o.run(0x20E1E0, (code & MASK,))
        fault = Fault()
        r = lib.em_status_scene_letter_0020E1E0(code, bank, C.byref(rig.native_workers()),
                                                C.byref(fault))
        assert fault.code == 0 and r == (0 if slot is None else 1), ('E1E0', code, slot, r)
        rig.compare(('E1E0', code, slot, bones), before)
        n += 1
    ca_cases = list(itertools.product((0, 1, 2, 3, 0xFF), (5, 0x10, 21, 200), (0, 1, 4, 13, 21, 22, 255),
                                      (7, 16)))
    for ca4, ca5, ca6, ca7 in select(ca_cases, 90, 0xE250, axes=(lambda c: c[0], lambda c: c[2])):
        plan = [0, 1, None, 2, 3, 4, 5, 6]
        rig = LetterRig(elf, plan)
        for a, v in zip(range(0x810CA4, 0x810CA8), (ca4, ca5, ca6, ca7)): rig.o.save(a, v, 1)
        before = dict(rig.o.mem)
        rig.o.run(0x20E250)
        fault = Fault()
        r = lib.em_status_scene_letters_0020E250((C.c_uint8 * 4)(ca4, ca5, ca6, ca7), rig.bank,
                                                 C.byref(rig.native_workers()), C.byref(fault))
        assert r == 0 and fault.code == 0, ('E250', ca4, ca5, ca6, ca7)
        before.update({a: rig.o.mem.get(a, 0) for a in range(0x810CA4, 0x810CA8)})
        rig.compare(('E250', ca4, ca5, ca6, ca7), before)
        n += 1
    return n, len(ca_cases)


# ---------------------------------------------------- F: menu player ------

PG_ADDR = {'d8104E4': (0x8104E4, 1), 'd810C60': (0x810C60, 1), 'd28A57C': (0x28A57C, 4),
           'd28A580': (0x28A580, 4), 'd28A584': (0x28A584, 4), 'd28A588': (0x28A588, 4),
           'd28A58C': (0x28A58C, 4), 'd28A590': (0x28A590, 4)}


class PlayerRig:
    def __init__(self, elf, actor, g, bone_count=21):
        self.o = fresh(elf)
        self.actor, self.g, self.bone_count = actor, g, bone_count
        self.expected, self.actual = [], []
        self.seq_o = self.seq_n = 0x5000
        o = self.o
        o.calls.update({
            0x1CA6E0: self.o_bind, 0x1C6150: self.o_bones, 0x1AF7C0: self.o_slot,
            0x1CB5B0: lambda r: self.expected.append(('array', r.r[4] & MASK)),
            0x1C63E0: lambda r: self.expected.append(('bone_init', r.r[5] & MASK)),
            0x1CA5F0: lambda r: self.expected.append(('mode', r.r[5] & MASK)),
            0x1C67E0: lambda r: self.expected.append(('clip', r.r[5] & MASK, r.f[12], r.f[13])),
            0x1C64F0: lambda r: self.expected.append(('advance', r.f[12])),
            0x20EC80: lambda r: self.expected.append(('publish',)),
            0x1AFF90: lambda r: self.expected.append(('free',))})
        self.push()

    def push(self):
        o, g = self.o, self.g
        for name, (addr, size) in PG_ADDR.items(): o.save(addr, getattr(g, name), size)
        for i in range(12): o.save(0x810610 + 4 * i, bits(g.view[i]))
        o.save(0x810858, bits(g.health)); o.save(0x81085C, bits(g.infection))
        push_actor(o, ACTOR, self.actor)

    def o_bind(self, o):
        self.expected.append(('bind', o.r[5] & MASK))
        o.save((o.r[4] & MASK) + 0x44, (o.r[5] & MASK) ^ 0x100)

    def o_bones(self, o):
        self.expected.append(('bones', o.r[4] & MASK))
        o.r[2] = self.bone_count

    def o_slot(self, o):
        self.seq_o += 1
        self.expected.append(('slot',))
        o.r[2] = self.seq_o

    def native_workers(self):
        A = self.actual

        def bind(_, a, model):
            A.append(('bind', model)); a.contents.w44 = model ^ 0x100; return 0

        def bones(_, word, out):
            A.append(('bones', word)); out[0] = self.bone_count; return 0

        def slot(_, out):
            self.seq_n += 1; A.append(('slot',)); out[0] = self.seq_n; return 0
        return workers(
            w_001CA6E0=bind, w_001C6150=bones, w_001AF7C0=slot,
            w_001CB5B0=lambda _, n: A.append(('array', n)) or 0,
            w_001C63E0=lambda _, a, clip: A.append(('bone_init', clip & MASK)) or 0,
            w_001CA5F0=lambda _, a, mode: A.append(('mode', mode & MASK)) or 0,
            w_001C67E0=lambda _, a, clip, x, y: A.append(('clip', clip & MASK, bits(x), bits(y))) or 0,
            w_001C64F0=lambda _, a, t: A.append(('advance', bits(t))) or 0,
            w_0020EC80=lambda _, a: A.append(('publish',)) or 0,
            w_001AFF90=lambda _, a: A.append(('free',)) or 0)

    def tick(self, lib, label, w=None):
        o = self.o
        o.r = [0] * 32; o.f = [0] * 32; o.r[28], o.r[29] = 0x27D370, STACK
        before = dict(o.mem)
        self.expected.clear(); self.actual.clear()
        o.run(0x20E6F0, (ACTOR,))
        fault = Fault()
        r = lib.em_status_scene_player_0020E6F0(C.pointer(self.actor), C.byref(self.g),
                                                C.byref(w or self.native_workers()), C.byref(fault))
        freed = ('free',) in self.expected
        assert fault.code == 0 and r == (0 if freed else 1), (label, r, fault.code, fault.address)
        assert self.actual == self.expected, (label, self.actual, self.expected)
        nb = self.actor.b0C
        allowed = {ACTOR + b for b in ACTOR_BYTES} | span(ACTOR + 0x110, 4 * nb)
        assert changed_bytes(o, before) <= allowed, (label, sorted(hex(a) for a in changed_bytes(o, before) - allowed)[:8])
        assert actor_bits(self.actor) == oracle_actor_bits(o, ACTOR), (label, actor_bits(self.actor),
                                                                      oracle_actor_bits(o, ACTOR))
        assert [self.actor.w110[i] for i in range(nb)] == [o.load(ACTOR + 0x110 + 4 * i) for i in range(nb)], label
        return r


def make_globals(variant=0, costume=0, health=100.0, infection=0.0, view=None):
    g = PlayerGlobals()
    g.d8104E4, g.d810C60 = variant, costume
    for i, n in enumerate(('d28A57C', 'd28A580', 'd28A584', 'd28A588', 'd28A58C', 'd28A590')):
        setattr(g, n, 0xC00000 + 0x1000 * i)
    view = view or [1.0, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0]
    for i, v in enumerate(view): g.view[i] = v
    g.health, g.infection = health, infection
    return g


def make_actor(state=0, b05=0, f38=1.0, c4=3.1415927, b0B=0, bones=0):
    a = Actor()
    a.b04, a.b05, a.f38, a.b0B, a.b0C = state, b05, f38, b0B, bones
    a.fC0[1] = c4
    return a


def check_player(elf, lib):
    n = 0
    rng = random.Random(0x20E6F0)
    PI = number(0x40490FDB)
    # State 0: every variant/costume, the 35.0 health edge and NaN, bone counts.
    healths = [35.0, up(35.0), down(35.0), 100.0, -1.0, number(0x7FC00000)]
    cases = list(itertools.product((0, 1, 2, 255), (0, 1, 2, 3), healths, (0, 1, 21, 120),
                                   (0.0, 37.5, 100.0, -3.0)))
    for variant, costume, health, bones, inf in select(cases, 120, 0xE6F0,
                                                       axes=(lambda c: c[:2], lambda c: c[2],
                                                             lambda c: c[3], lambda c: c[4])):
        view = [f32(rng.uniform(-2, 2)) for _ in range(12)]
        rig = PlayerRig(elf, make_actor(0), make_globals(variant, costume, health, inf, view), bones)
        rig.tick(lib, ('init', variant, costume, health, bones, inf))
        n += 1
    # More bones than the 0x2F0 record holds: the original stores past it.
    rig = PlayerRig(elf, make_actor(0), make_globals(), 121)
    rig.o.run(0x20E6F0, (ACTOR,))
    assert rig.expected.count(('slot',)) == 121 and \
        rig.o.load(ACTOR + 0x110 + 4 * 120) != 0, 'overflow not reproduced'
    fault = Fault()
    r = lib.em_status_scene_player_0020E6F0(C.pointer(rig.actor), C.byref(rig.g),
                                            C.byref(rig.native_workers()), C.byref(fault))
    assert r == -1 and fault.code == 4 and rig.actor.w110[119] != 0, ('overflow fault', r, fault.code)
    # State 1: breathe edges, yaw wrap edges, clip swap-back, the -127 clamp.
    scales = [1.0, up(1.0), f32(1.01), f32(1.29), down(f32(1.29)), up(f32(1.29)), f32(1.3),
              down(f32(1.3)), f32(1.31), f32(0.5), f32(2.0)]
    yaws = [PI, down(PI), f32(PI - 0.01), down(f32(PI - 0.01)), up(f32(PI - 0.01)), -PI, 0.0, f32(3.2)]
    cases = list(itertools.product((0, 1, 2), scales, yaws, ((100.0, 1), (100.0, 0), (10.0, 1), (35.0, 1)),
                                   (0.0, 50.0, 100.0, 158.0, 170.0)))
    for b05, f38, c4, (health, b0B), inf in select(cases, 160, 0x1E6F0,
                                                  axes=(lambda c: c[0], lambda c: c[1], lambda c: c[2],
                                                        lambda c: c[3], lambda c: c[4])):
        rig = PlayerRig(elf, make_actor(1, b05, f38, c4, b0B, 3), make_globals(health=health, infection=inf))
        rig.tick(lib, ('run', b05, f38, c4, health, b0B, inf))
        n += 1
    # The -127 clamp at equality and its neighbours: infections whose
    # 0.01 * (-100 * D) * scale is exactly -127 (found with ee_float_model;
    # asserted here) for the post-step scales 1.0 and 1.19.
    for f38, exact in ((number(0x3F8147AE), 0x42FE0002), (f32(1.2), 0x42D57206)):
        scale = M.ee_sub(bits(f38), 0x3C23D70A)
        assert M.ee_mul(M.ee_mul(0x3C23D70A, M.ee_mul(0xC2C80000, exact)), scale) == 0xC2FE0000
        for d in (exact - 2, exact - 1, exact, exact + 1, exact + 2):
            rig = PlayerRig(elf, make_actor(1, 1, f38, 0.0, 0, 3), make_globals(infection=number(d)))
            rig.tick(lib, ('clamp', hex(d)))
            n += 1
    for state in (2, 3, 4, 0x80, 0xFF):
        rig = PlayerRig(elf, make_actor(state), make_globals())
        assert rig.tick(lib, ('free', state)) == 0
        n += 1
    for k in range(pick(600, 60)):
        a = make_actor(rng.choice((0, 1, 1, 1, 2)), rng.randrange(3), f32(rng.uniform(0.9, 1.4)),
                       f32(rng.uniform(-3.3, 3.3)), rng.randrange(3), rng.randrange(8))
        g = make_globals(rng.randrange(3), rng.randrange(4), f32(rng.uniform(-10, 120)),
                         f32(rng.uniform(-20, 200)), [f32(rng.uniform(-3, 3)) for _ in range(12)])
        PlayerRig(elf, a, g, rng.randrange(40)).tick(lib, ('random', k))
        n += 1
    # Lockstep from state 0: the original keeps its memory between ticks.
    ticks = pick(900, 160)
    rig = PlayerRig(elf, make_actor(0), make_globals(health=20.0, infection=30.0))
    for t in range(ticks):
        if t == 70:  # health recovers: the clip swaps back once
            rig.g.health = 60.0
            rig.o.save(0x810858, bits(60.0))
        rig.tick(lib, ('lockstep', t))
    return n, ticks


# ---------------------------------------------------- G: the static pool --

POOL_BASE = 0x28B020
SLOT_STACK, SLOT_BASE = 0x9C0400, 0xA00000


def pool_address(i): return POOL_BASE + i * 0x2F0


def random_actor(rng, used=None):
    a = Actor()
    for name, (off, size) in ACTOR_FIELDS.items():
        setattr(a, name, rng.getrandbits(8 * size) if size < 4 else rng.getrandbits(32))
    a.b00 = rng.choice((0, 0, 1, 2, 0x80)) if used is None else used
    a.b09 = rng.choice((0, 0, 1, 2, 5))
    a.h94 = C.c_int16(rng.getrandbits(16) - 0x8000).value
    for name, off, n in ACTOR_ARRAYS:
        for i in range(n): getattr(a, name)[i] = f32(rng.uniform(-50, 50))
    for i in range(120): a.w110[i] = rng.getrandbits(32)
    return a


class PoolRig:
    """The 24 records at D_0028B020 (native Pool) and the oracle's copy.
    001AF800 runs as the original over D_00275BD0/D_00275BCC; the native
    worker records each slot it is given."""

    def __init__(self, elf, rng, records=None):
        self.o = fresh(elf)
        self.pool = Pool()
        self.expected, self.actual = [], []
        for i in range(24):
            a = records[i] if records else random_actor(rng)
            for k in range(min(a.b09, 120)):
                a.w110[k] = SLOT_BASE + 0xD0 * (i * 8 + k)
            C.memmove(C.addressof(self.pool.record[i]), C.addressof(a), C.sizeof(Actor))
            push_actor(self.o, pool_address(i), a)
        self.count = rng.randrange(0, 0x400)
        self.o.save(0x275BD0, SLOT_STACK)
        self.o.save(0x275BCC, self.count, 2)
        self.o.calls.update({
            0x121A28: self.o_memset,
            0x1CB590: self.o_setup,
            0x20E460: lambda r: self.expected.append(('call', r.r[4] & MASK, 0x20E460)),
            0x20E6F0: lambda r: self.expected.append(('call', r.r[4] & MASK, 0x20E6F0)),
            0x1CB580: lambda r: self.expected.append(('call', r.r[4] & MASK, 0x1CB580))})

    def o_memset(self, o):
        base, value, size = o.r[4] & MASK, o.r[5] & 0xFF, o.r[6] & MASK
        self.expected.append(('memset', base, value, size))
        for k in range(size): o.save(base + k, value, 1)

    def o_setup(self, o):  # 001CB590 stores D_00275B48 = D_00275B44 = a0 (byte-matched C)
        self.expected.append(('setup', o.r[4] & MASK, o.r[5] & MASK, o.r[6] & 0xFF))
        o.save(0x275B48, o.r[4] & MASK)
        o.save(0x275B44, o.r[4] & MASK)

    def index_of(self, actor):
        return (C.addressof(actor.contents) - C.addressof(self.pool.record[0])) // C.sizeof(Actor)

    def native_workers(self):
        A = self.actual
        return workers(
            w_001AF800_slot=lambda _, slot: A.append(('slot', slot)) or 0,
            w_001CB590=lambda _, a, stride, n: A.append(
                ('setup', pool_address(self.index_of(a)), stride & MASK, n)) or 0,
            w_call=lambda _, a, fn: A.append(('call', pool_address(self.index_of(a)), fn)) or 0)

    def pushed_slots(self, before_top):
        top = self.o.load(0x275BD0)
        return [('slot', self.o.load(a)) for a in range(before_top - 4, top - 4, -4)]

    def compare(self, label, before, extra=()):
        allowed = set()
        for i in range(24): allowed |= {pool_address(i) + b for b in RECORD_BYTES}
        allowed |= span(0x275BCC, 2) | span(0x275BD0, 4) | span(0x275B44, 4) | span(0x275B48, 4)
        allowed |= span(SLOT_STACK - 0x800, 0x800) | span(SLOT_BASE, 0xD0 * 24 * 8)
        allowed |= set(extra)
        bad = changed_bytes(self.o, before) - allowed
        assert not bad, (label, 'unmodelled write', sorted(hex(a) for a in bad)[:6])
        for i in range(24):
            assert actor_bits(self.pool.record[i]) == oracle_actor_bits(self.o, pool_address(i)), (label, i)
            assert list(self.pool.record[i].w110) == [self.o.load(pool_address(i) + 0x110 + 4 * k)
                                                      for k in range(120)], (label, i, 'slots')


def check_pool(elf, lib):
    rng = random.Random(0x1AFF10)
    n = 0
    # 001AFF10: first free record, every pattern of used bytes incl. a full pool.
    patterns = [[0] * 24, [2] * 24, [1] * 23 + [0], [0x80] * 5 + [0] * 19]
    patterns += [[rng.choice((0, 1, 2, 0x80)) for _ in range(24)] for _ in range(pick(60, 20))]
    for used in patterns:
        rig = PoolRig(elf, rng, [random_actor(rng, u) for u in used])
        before = dict(rig.o.mem)
        rig.o.run(0x1AFF10)
        got = lib.em_status_scene_alloc_001AFF10(C.byref(rig.pool))
        want = rig.o.r[2] & MASK
        assert (pool_address(rig.index_of(got)) if got else 0) == want, ('AFF10', used, hex(want))
        rig.compare(('AFF10', used), before)
        n += 1
    # 001AFEB0 (001AF800 per record in use) and 001AFE60.
    for k in range(pick(30, 10)):
        rig = PoolRig(elf, rng)
        before = dict(rig.o.mem)
        rig.o.run(0x1AFEB0)
        fault = Fault()
        assert lib.em_status_scene_release_001AFEB0(C.byref(rig.pool), C.byref(rig.native_workers()),
                                                    C.byref(fault)) == 0 and fault.code == 0
        assert rig.actual == rig.pushed_slots(SLOT_STACK), ('AFEB0 slots', k)
        rig.compare(('AFEB0', k), before)
        n += 1
    rig = PoolRig(elf, rng)
    before = dict(rig.o.mem)
    rig.o.run(0x1AFE60)
    lib.em_status_scene_clear_001AFE60(C.byref(rig.pool))
    assert rig.expected == [('memset', pool_address(i), 0, 0x2F0) for i in range(24)]
    rig.compare('AFE60', before, span(POOL_BASE, 24 * 0x2F0))
    n += 1
    # 001AFF90: +0x14 names the record freed (itself or another one).
    for k in range(pick(40, 12)):
        rig = PoolRig(elf, rng)
        me, other = rng.randrange(24), rng.randrange(24)
        target = me if k % 3 else other
        rig.pool.record[me].w14 = pool_address(target)
        rig.o.save(pool_address(me) + 0x14, pool_address(target))
        top = rig.o.load(0x275BD0)
        before = dict(rig.o.mem)
        rig.o.run(0x1AFF90, (pool_address(me),))
        fault = Fault()
        assert lib.em_status_scene_free_001AFF90(C.byref(rig.pool), C.pointer(rig.pool.record[me]),
                                                 C.byref(rig.native_workers()), C.byref(fault)) == 0
        assert rig.actual == rig.pushed_slots(top), ('AFF90 slots', k)
        rig.compare(('AFF90', k), before)
        n += 1
    rig = PoolRig(elf, rng)
    rig.pool.record[3].w14 = pool_address(3) + 4
    fault = Fault()
    assert lib.em_status_scene_free_001AFF90(C.byref(rig.pool), C.pointer(rig.pool.record[3]),
                                             C.byref(rig.native_workers()), C.byref(fault)) == -1 \
        and fault.code == 4, 'a +0x14 outside the records faults'
    # 001B0000: 001CB590 then the +0x10 call for every record in use.
    for k in range(pick(40, 12)):
        rig = PoolRig(elf, rng)
        for i in range(24):
            fn = rng.choice((0x20E460, 0x20E6F0, 0x1CB580))
            rig.pool.record[i].w10 = fn
            rig.o.save(pool_address(i) + 0x10, fn)
        before = dict(rig.o.mem)
        rig.o.run(0x1B0000)
        fault = Fault()
        assert lib.em_status_scene_walk_001B0000(C.byref(rig.pool), C.byref(rig.native_workers()),
                                                 C.byref(fault)) == 0 and fault.code == 0
        assert rig.actual == rig.expected, ('B0000', k, first_diff(rig.actual, rig.expected))
        rig.compare(('B0000', k), before)
        n += 1
    return n


# ---------------------------------------------- H: letter behaviour 0020E460

class LetterBehaviourRig:
    def __init__(self, elf, actor, cap, view, row=0xA12340, slots=0x7000):
        self.o = fresh(elf)
        self.actor = actor
        self.expected, self.actual = [], []
        self.seq_o = self.seq_n = slots
        self.row_array = (C.c_uint32 * 1)(row)
        self.g = LetterGlobals()
        self.g.d275BCC = cap
        self.g.d275B40 = C.cast(self.row_array, C.POINTER(C.c_uint32))
        for i, v in enumerate(view): self.g.view[i] = v
        o = self.o
        o.save(0x275BCC, cap & 0xFFFF, 2)
        o.save(0x275B40, 0x930000)
        o.save(0x930000, row)
        for i in range(12): o.save(0x810610 + 4 * i, bits(view[i]))
        push_actor(o, ACTOR, actor)
        o.calls.update({
            0x1AF7C0: self.o_slot,
            0x1CB5B0: lambda r: self.expected.append(('array', r.r[4] & MASK)),
            0x1C62C0: lambda r: self.expected.append(('bone_init1', r.r[4] & MASK)),
            0x1CA5F0: self.o_mode,
            0x1C6380: lambda r: self.expected.append(('place', r.r[4] & MASK)),
            0x1026A0: lambda r: self.expected.append(('apply', r.r[4] & MASK, r.r[5] & MASK, r.r[6] & MASK)),
            0x1D2040: lambda r: self.expected.append(('packet', r.r[4] & MASK, r.r[5] & MASK)),
            0x1CB580: lambda r: self.expected.append(('call', r.r[4] & MASK, 0x1CB580)),
            0x1AFF90: lambda r: self.expected.append(('free', r.r[4] & MASK))})

    def o_slot(self, o):
        self.seq_o += 1
        self.expected.append(('slot',))
        o.r[2] = self.seq_o

    def o_mode(self, o):  # the worker writes +0x4C (001CA5F0 case 0xB: 0x1CB580)
        self.expected.append(('mode', o.r[4] & MASK, o.r[5] & MASK))
        o.save((o.r[4] & MASK) + 0x4C, 0x1CB580 if o.r[5] & MASK == 0xB else 0x1CAA00)

    def native_workers(self):
        A = self.actual

        def slot(_, out):
            self.seq_n += 1; A.append(('slot',)); out[0] = self.seq_n; return 0

        def mode(_, a, m):
            A.append(('mode', ACTOR, m & MASK)); a.contents.w4C = 0x1CB580 if m == 0xB else 0x1CAA00
            return 0
        return workers(
            w_001AF7C0=slot, w_001CA5F0=mode,
            w_001CB5B0=lambda _, n: A.append(('array', n)) or 0,
            w_001C62C0=lambda _, a: A.append(('bone_init1', ACTOR)) or 0,
            w_001C6380=lambda _, a: A.append(('place', ACTOR)) or 0,
            w_001026A0=lambda _, out, m, v: A.append(('apply', out, m, v)) or 0,
            w_001D2040=lambda _, a0, a1: A.append(('packet', a0 & MASK, a1 & MASK)) or 0,
            w_call=lambda _, a, fn: A.append(('call', ACTOR, fn)) or 0,
            w_001AFF90=lambda _, a: A.append(('free', ACTOR)) or 0)

    def tick(self, lib, label):
        o = self.o
        o.r = [0] * 32; o.f = [0] * 32; o.r[28], o.r[29] = 0x27D370, STACK
        before = dict(o.mem)
        self.expected.clear(); self.actual.clear()
        o.run(0x20E460, (ACTOR,))
        fault = Fault()
        r = lib.em_status_scene_letter_0020E460(C.pointer(self.actor), C.byref(self.g),
                                                C.byref(self.native_workers()), C.byref(fault))
        freed = any(e[0] == 'free' for e in self.expected)
        assert fault.code == 0 and r == (0 if freed else 1), (label, r, fault.code, hex(fault.address))
        assert self.actual == self.expected, (label, first_diff(self.actual, self.expected))
        nb = min(self.actor.b0C, 120)
        allowed = {ACTOR + b for b in ACTOR_BYTES} | span(ACTOR + 0x110, 4 * nb)
        bad = changed_bytes(o, before) - allowed
        assert not bad, (label, sorted(hex(a) for a in bad)[:8])
        assert actor_bits(self.actor) == oracle_actor_bits(o, ACTOR), label
        assert [self.actor.w110[i] for i in range(nb)] == [o.load(ACTOR + 0x110 + 4 * i) for i in range(nb)]


def check_letter_behaviour(elf, lib):
    rng = random.Random(0x20E460)
    n = 0
    hub_view = [1.0, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0]
    for bones in (0, 1, 2, 21, 120):
        for cap in (-1, 0, bones - 1, bones, bones + 1, 0x3F0, -0x8000):
            view = hub_view if cap == 0x3F0 else [f32(rng.uniform(-3, 3)) for _ in range(12)]
            a = random_actor(rng, 2)
            a.b04, a.b0C = 0, bones
            LetterBehaviourRig(elf, a, cap, view).tick(lib, ('init', bones, cap))
            n += 1
    for glyph in (0x6D, 0x2F, 0x40, 0x30, 0x15, 0x6C, 0xED):
        a = random_actor(rng, 2)
        a.b04, a.b0D, a.w4C = 1, glyph, 0x1CB580
        LetterBehaviourRig(elf, a, 5, hub_view, row=rng.getrandbits(32) & ~3).tick(lib, ('run', hex(glyph)))
        n += 1
    for state in (2, 3, 4, 0x80, 0xFF):
        a = random_actor(rng, 2)
        a.b04 = state
        LetterBehaviourRig(elf, a, 5, hub_view).tick(lib, ('free', state))
        n += 1
    for k in range(pick(200, 40)):
        a = random_actor(rng, 2)
        a.b04 = rng.choice((0, 0, 1, 1, 2, 3))
        a.b0C = rng.randrange(0, 30)
        a.b0D = rng.choice((0x6D, 0x2F, rng.getrandbits(8)))
        a.w4C = 0x1CB580
        LetterBehaviourRig(elf, a, rng.randrange(-5, 40), [f32(rng.uniform(-100, 100)) for _ in range(12)],
                           row=rng.getrandbits(32) & ~3).tick(lib, ('random', k))
        n += 1
    # Lockstep: init, then run frames.
    a = random_actor(rng, 2)
    a.b04, a.b0C, a.b0D = 0, 1, 0x40
    rig = LetterBehaviourRig(elf, a, 0x3F0, hub_view)
    for t in range(pick(40, 8)):
        rig.tick(lib, ('lockstep', t))
    return n


# ---------------------------------------------- I: 0020EC80 and 001F4BF0 ---

SPR_FIELDS = (('s3400', 0x70003400, 16), ('s3440', 0x70003440, 16), ('s36A0', 0x700036A0, 16),
              ('s38A0', 0x700038A0, 4), ('s38B0', 0x700038B0, 4))


def scratch_bits(spr):
    out = []
    for name, _addr, n in SPR_FIELDS:
        v = getattr(spr, name)
        out += [v[i] if name in ('s38A0', 's38B0') else bits(v[i]) for i in range(n)]
    return out


def oracle_scratch_bits(o):
    return [o.load(addr + 4 * i) for _name, addr, n in SPR_FIELDS for i in range(n)]


class PublishRig:
    def __init__(self, elf, rng, actor, d8104E4, rand_values):
        self.o = fresh(elf)
        self.actor, self.d8104E4 = actor, d8104E4
        self.spr = Scratch()
        self.rand_o, self.rand_n = list(rand_values), list(rand_values)
        self.expected, self.actual = [], []
        o = self.o
        for name, addr, n in SPR_FIELDS:
            for i in range(n):
                value = rng.getrandbits(32) if name in ('s38A0', 's38B0') else bits(f32(rng.uniform(-9, 9)))
                getattr(self.spr, name)[i] = value if name in ('s38A0', 's38B0') else number(value)
                o.save(addr + 4 * i, value)
        o.save(0x8104E4, d8104E4, 1)
        push_actor(o, ACTOR, actor)
        rec = self.expected.append
        o.calls.update({
            0x1029C0: lambda r: rec(('identity', r.r[4] & MASK)),
            0x102B08: lambda r: rec(('rot_x', r.r[4] & MASK, r.r[5] & MASK, r.f[12] & MASK)),
            0x102BB0: lambda r: rec(('rot_y', r.r[4] & MASK, r.r[5] & MASK, r.f[12] & MASK)),
            0x102A60: lambda r: rec(('rot_z', r.r[4] & MASK, r.r[5] & MASK, r.f[12] & MASK)),
            0x1026D0: lambda r: rec(('mul', r.r[4] & MASK, r.r[5] & MASK, r.r[6] & MASK)),
            0x1026A0: lambda r: rec(('apply', r.r[4] & MASK, r.r[5] & MASK, r.r[6] & MASK)),
            0x102958: self.o_copy,
            0x1C69A0: lambda r: rec(('bones', r.r[4] & MASK)),
            0x122BB8: self.o_rand,
            0x1CD520: lambda r: rec(('sprite', r.r[4] & MASK, r.r[5] & MASK, r.r[6] & MASK,
                                     r.r[7] & (2**64 - 1), r.r[8] & MASK, r.f[12] & MASK,
                                     r.f[13] & MASK, r.f[14] & MASK)),
            0x1D2040: lambda r: rec(('packet', r.r[4] & MASK, r.r[5] & MASK)),
            0x1CB580: lambda r: rec(('call', r.r[4] & MASK, 0x1CB580))})

    def o_copy(self, o):  # copy_qw4: a raw 64-byte copy
        dst, src = o.r[4] & MASK, o.r[5] & MASK
        self.expected.append(('copy', dst, src))
        for k in range(64): o.save(dst + k, o.load(src + k, 1), 1)

    def o_rand(self, o):
        self.expected.append(('rand',))
        o.r[2] = self.rand_o.pop(0)

    def native_workers(self):
        A = self.actual

        def rand(_, out):
            A.append(('rand',)); out[0] = signed(self.rand_n.pop(0)); return 0

        def sprite(_, a0, a1, pos, tex0, rgb, f12, f13, f14):
            A.append(('sprite', a0 & MASK, a1 & MASK, pos, tex0, rgb, bits(f12), bits(f13), bits(f14)))
            return 0
        return workers(
            w_001029C0=lambda _, m: A.append(('identity', m)) or 0,
            w_00102B08=lambda _, d, s_, x: A.append(('rot_x', d, s_, x)) or 0,
            w_00102BB0=lambda _, d, s_, x: A.append(('rot_y', d, s_, x)) or 0,
            w_00102A60=lambda _, d, s_, x: A.append(('rot_z', d, s_, x)) or 0,
            w_001026D0=lambda _, d, a, b: A.append(('mul', d, a, b)) or 0,
            w_001026A0=lambda _, d, m, v: A.append(('apply', d, m, v)) or 0,
            w_001C69A0=lambda _, a: A.append(('bones', ACTOR)) or 0,
            w_00122BB8=rand, w_001CD520=sprite,
            w_001D2040=lambda _, a0, a1: A.append(('packet', a0 & MASK, a1 & MASK)) or 0,
            w_call=lambda _, a, fn: A.append(('call', ACTOR, fn)) or 0)

    def run(self, lib, label):
        o = self.o
        before = dict(o.mem)
        o.run(0x20EC80, (ACTOR,))
        fault = Fault()
        r = lib.em_status_scene_publish_0020EC80(C.pointer(self.actor), self.d8104E4, C.byref(self.spr),
                                                 C.byref(self.native_workers()), C.byref(fault))
        assert r == 0 and fault.code == 0, (label, r, fault.code, hex(fault.address))
        # copy_qw4 is inline natively: its ('copy', ...) event has no native worker.
        expected = [e for e in self.expected if e[0] != 'copy']
        assert ('copy', 0x700036A0, 0x70003400) in self.expected, label
        assert self.actual == expected, (label, first_diff(self.actual, expected))
        allowed = set()
        for _name, addr, n in SPR_FIELDS: allowed |= span(addr, 4 * n)
        bad = changed_bytes(o, before) - allowed
        assert not bad, (label, sorted(hex(a) for a in bad)[:8])
        assert scratch_bits(self.spr) == oracle_scratch_bits(o), (label, first_diff(scratch_bits(self.spr),
                                                                                    oracle_scratch_bits(o)))
        assert actor_bits(self.actor) == oracle_actor_bits(o, ACTOR), label


def check_publish(elf, lib):
    rng = random.Random(0x20EC80)
    n = 0
    rands = [0, 1, 0x7FFFFF, 0x800000, 0x7F800000, 0x7FFFFFFF, 0x40000000, 0x3FFFFFFF]
    for d8104E4 in (0, 1, 2, 0xFF):
        for k in range(pick(12, 4)):
            a = random_actor(rng, 2)
            a.w4C = 0x1CB580
            rv = [rng.choice(rands + [rng.getrandbits(31)])]
            PublishRig(elf, rng, a, d8104E4, rv).run(lib, ('publish', d8104E4, k))
            n += 1
    return n


def check_glow(elf, lib):
    """001F4BF0 alone over arbitrary colour words and rand values."""
    rng = random.Random(0x1F4BF0)
    n = 0
    colours = [(0x20, 0x70, 0x80, 0x80), (0, 0, 0, 0), (0xFF, 0xFF, 0xFF, 0xFF),
               (0x7FFFFFFF, 1, 0x80000000, 0xFFFFFFFF), (1, 2, 3, 0x1000000)]
    colours += [tuple(rng.getrandbits(rng.choice((8, 16, 32))) for _ in range(4)) for _ in range(pick(60, 12))]
    rands = [0, 1, 0x7FFFFF, 0x800000, 0x7F800000, 0x7FFFFFFF] + [rng.getrandbits(31) for _ in range(pick(20, 4))]
    for colour in colours:
        for r in rands[:pick(len(rands), 5)]:
            o = fresh(elf)
            got = []
            for i, c in enumerate(colour): o.save(0x700038B0 + 4 * i, c)
            o.calls.update({0x122BB8: lambda x, r=r: x.r.__setitem__(2, r),
                            0x1CD520: lambda x: got.append((x.r[4] & MASK, x.r[5] & MASK, x.r[6] & MASK,
                                                            x.r[7] & (2**64 - 1), x.r[8] & MASK,
                                                            x.f[12] & MASK, x.f[13] & MASK, x.f[14] & MASK))})
            o.run(0x1F4BF0, (0x700038A0, 0x700038B0))
            native = []

            def sprite(_, a0, a1, pos, tex0, rgb, f12, f13, f14):
                native.append((a0 & MASK, a1 & MASK, pos, tex0, rgb, bits(f12), bits(f13), bits(f14)))
                return 0
            fault = Fault()
            assert lib.em_status_scene_glow_001F4BF0(
                0x700038A0, (C.c_uint32 * 4)(*colour),
                C.byref(workers(w_00122BB8=lambda _, out: out.__setitem__(0, signed(r)) or 0, w_001CD520=sprite)),
                C.byref(fault)) == 0, (colour, r, fault.code)
            assert native == got, (colour, hex(r), native, got)
            n += 1
    # A negative rand() result is outside the original range: fault.
    fault = Fault()
    assert lib.em_status_scene_glow_001F4BF0(
        0, (C.c_uint32 * 4)(1, 2, 3, 4),
        C.byref(workers(w_00122BB8=lambda _, out: out.__setitem__(0, -1) or 0,
                        w_001CD520=lambda *a: 0)), C.byref(fault)) == -1 and fault.code == 3
    return n


def first_diff(a, b):
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y: return i, x, y
    return len(a), len(b)


# ---------------------------------------------------- H: module loader ----

U = {'state': 0, 'step': 1, 'sub': 3, 'module': 6, 'kind': 7}
LD_WORDS = [('d275C70', 0x275C70), ('d275C74', 0x275C74), ('d28A5A0', 0x28A5A0), ('d28A738', 0x28A738),
            ('d28A73C', 0x28A73C), ('d28A744', 0x28A744), ('d28A748', 0x28A748)]


def header_bytes(module, count=1, sections=0, relocs=(), payload=0x50800, entries=None, entry_base=0):
    h = bytearray(0x800)
    struct.pack_into('<IIIHH', h, 0, module, 0x0E4BC000 + module * 0x800, payload, entry_base, count)
    struct.pack_into('<II', h, 0x10, sections, payload - 0x800 * min(sections, 3))
    struct.pack_into('<I', h, 0x1C, len(relocs))
    # Entries: count chunk records (001FF3F0), then the sections (001FF830
    # step 7 walks +0xE + i), then the relocation words.
    for i in range(count + sections):
        off, size = entries[i] if entries and i < len(entries) else (0x800 * i, 0x400 + 0x10 * i)
        struct.pack_into('<II', h, 0x20 + 8 * i, off, size)
    base = 0x20 + 8 * (count + sections)
    for k, e in enumerate(relocs): struct.pack_into('<I', h, base + 4 * k, e)
    return bytes(h)


class LoaderRig:
    """One slot-2 record and loader state on both sides. polls: the queue of
    00200730 results; fb: the queue of 001FB370 results; area: what the
    001FFCD0/00200360 stubs leave in +8."""

    def __init__(self, elf, header, user=None, slot_state=2, polls=(), fb=(), area=(),
                 globals_=None, gate=0, spad=0, ca=(0xFF, 0)):
        self.o = fresh(elf)
        self.ld = Loader()
        self.slot = (C.c_uint8 * 1)(slot_state)
        self.user = (C.c_uint8 * 24)(*(user or [0] * 24))
        self.polls_o, self.polls_n = list(polls), list(polls)
        self.fb_o, self.fb_n = list(fb), list(fb)
        self.area_o, self.area_n = list(area), list(area)
        self.expected, self.actual = [], []
        g = globals_ or {}
        ld = self.ld
        ld.d282157, ld.d275BD8, ld.spad3B90 = gate, 1, spad
        ld.d810CA4, ld.d810CA6 = ca
        for name, _ in LD_WORDS: setattr(ld, name, g.get(name, 0))
        for i in range(68): ld.d28A490[i] = 0xAB000000 + i
        C.memmove(ld.header, header, 0x800)
        o = self.o
        o.calls.update({0x200780: self.o_read, 0x200730: self.o_poll, 0x200830: self.o_section,
                        0x1FB370: self.o_fb, 0x1FFCD0: self.o_area('area1'), 0x200360: self.o_area('area2')})
        self.push()

    def push(self):
        o, ld = self.o, self.ld
        o.save(SPAD_SLOT, SLOT)
        o.save(SLOT, self.slot[0], 1); o.save(SLOT + 4, 0x1FF0D0)
        for i in range(24): o.save(SLOT + 8 + i, self.user[i], 1)
        o.save(0x282157, ld.d282157, 1); o.save(0x275BD8, ld.d275BD8, 1); o.save(SPAD_3B90, ld.spad3B90, 1)
        o.save(0x810CA4, ld.d810CA4, 1); o.save(0x810CA6, ld.d810CA6, 1)
        for name, addr in LD_WORDS: o.save(addr, getattr(ld, name))
        for i in range(68): o.save(0x28A490 + 4 * i, ld.d28A490[i])
        o.write(HEADER, bytes(ld.header))

    def o_read(self, o):
        self.expected.append(('read', o.r[4] & MASK, o.r[5] & MASK, o.r[6] & MASK, o.r[7] & MASK))

    def o_poll(self, o):
        self.expected.append(('poll',)); o.r[2] = self.polls_o.pop(0) & MASK

    def o_section(self, o):
        self.expected.append(('section', o.r[4] & MASK))

    def o_fb(self, o):
        self.expected.append(('fb', o.r[4] & MASK)); o.r[2] = self.fb_o.pop(0)

    def o_area(self, name):
        def call(o):
            self.expected.append((name,))
            value = self.area_o.pop(0)
            if value is not None: o.save(SLOT + 8, value, 1)
        return call

    def native_workers(self):
        A = self.actual

        def read(_, file, buf, offset, size, header):
            assert bool(header) == (buf == HEADER), ('header pointer', buf)
            A.append(('read', file, buf, offset & MASK, size & MASK)); return 0

        def poll(_, out):
            A.append(('poll',)); out[0] = signed(self.polls_n.pop(0)); return 0

        def fb(_, address, out):
            A.append(('fb', address)); out[0] = self.fb_n.pop(0); return 0

        def area(name):
            def call(_, user):
                A.append((name,))
                value = self.area_n.pop(0)
                if value is not None: user[0] = value
                return 0
            return call
        return workers(w_00200780=read, w_00200730=poll,
                       w_00200830=lambda _, a: A.append(('section', a)) or 0, w_001FB370=fb,
                       w_001FFCD0=area('area1'), w_00200360=area('area2'))

    def state(self):
        ld = self.ld
        return ([self.slot[0]] + list(self.user) + [ld.d275BD8] + [getattr(ld, n) for n, _ in LD_WORDS]
                + list(ld.d28A490))

    def oracle_state(self):
        o = self.o
        return ([o.load(SLOT, 1)] + [o.load(SLOT + 8 + i, 1) for i in range(24)] + [o.load(0x275BD8, 1)]
                + [o.load(a) for _, a in LD_WORDS] + [o.load(0x28A490 + 4 * i) for i in range(68)])

    def allowed(self):
        a = span(SLOT, 1) | span(SLOT + 8, 24) | {0x275BD8} | span(0x28A490, 4 * 68)
        for _, addr in LD_WORDS: a |= span(addr, 4)
        return a

    def dispatch(self, lib, label, w=None):
        o = self.o
        o.r = [0] * 32; o.f = [0] * 32; o.r[28], o.r[29] = 0x27D370, STACK
        before = dict(o.mem)
        self.expected.clear(); self.actual.clear()
        o.run(0x1FF0D0)
        fault = Fault()
        r = lib.em_status_scene_loader_001FF0D0(self.slot, self.user, C.byref(self.ld),
                                                C.byref(w or self.native_workers()), C.byref(fault))
        assert r == 0 and fault.code == 0, (label, fault.address, fault.code)
        diff = changed_bytes(o, before) - self.allowed()
        assert not diff, (label, sorted(hex(a) for a in diff)[:8])
        assert self.actual == self.expected, (label, self.actual, self.expected)
        assert self.state() == self.oracle_state(), (label, first_diff(self.state(), self.oracle_state()))


MODULES = [0x21, 0x1F, 2, 3, 1, 0x27, 0x28, 0x29, 0x37, 0x1D, 0x32, 0x35, 0x36, 0x2A, 0x2B, 0x44, 0]
GLOBALS = dict(d275C70=0x289BC0, d275C74=0x19A3F40, d28A5A0=0x1516F40, d28A738=0x10E99C0,
               d28A73C=0x1335F40, d28A744=0x19A3F40, d28A748=0x19A3F40)


def run_load(elf, lib, label, module, header, polls, fb=(), spad=0, limit=120):
    """001FF080(0, module), then 001FF0D0 until the task stops; returns the
    number of dispatches."""
    rig = LoaderRig(elf, header, polls=polls, fb=fb, globals_=GLOBALS, spad=spad)
    lib.em_status_scene_loader_request_001FF080(rig.slot, rig.user, 0, module)
    rig.o.run(0x1FF080, (0, module))
    assert rig.o.load(SLOT, 1) == rig.slot[0] == 1 and rig.o.load(SLOT + 4) == 0x1FF0D0, (label, 'request')
    assert [rig.o.load(SLOT + 8 + i, 1) for i in range(24)] == list(rig.user), (label, 'request bytes')
    rig.slot[0] = 2; rig.o.save(SLOT, 2, 1)  # 001AB6A0 promotes 1 -> 2 before the first call
    for n in range(1, limit + 1):
        rig.dispatch(lib, (label, n))
        if rig.slot[0] == 0:
            assert not rig.polls_n and not rig.fb_n, (label, 'unused scripted results')
            return n, rig
    raise AssertionError((label, 'load did not finish'))


def check_loader(elf, lib, capture_header):
    n = 0
    # 001FF080 alone: record writes over a dirty record.
    for state, module in itertools.product((0, 1, 2, 0x63), (0, 0x21, 0xFF)):
        rig = LoaderRig(elf, capture_header, user=list(range(0x40, 0x58)), slot_state=0)
        lib.em_status_scene_loader_request_001FF080(rig.slot, rig.user, state, module)
        rig.o.run(0x1FF080, (state, module))
        assert [rig.o.load(SLOT, 1)] + [rig.o.load(SLOT + 8 + i, 1) for i in range(24)] == \
            [rig.slot[0]] + list(rig.user), ('request', state, module)
        n += 1
    # Whole loads: every bank-selection arm, immediate and busy I/O, the error
    # arms, chunk counts 0/1/3, sections and relocations, and the kind-3 path.
    loads = 0
    rng = random.Random(0x1FF830)
    for module in MODULES:
        for count, sections, relocs in ((1, 0, ()), (0, 0, ()), (3, 2, (0x01000010, 0x3F000020, 0x05FFFFFF))):
            header = header_bytes(module, count, sections, relocs)
            for script in ('fast', 'busy', 'errors'):
                reads = 2 + count
                if script == 'fast': polls = [1] * reads
                elif script == 'busy': polls = sum(([0] * rng.randrange(4) + [1] for _ in range(reads)), [])
                else:  # header error: back to step 0; chunk error: re-read it; payload error: step 3
                    polls = [2, 1] + ([5, 1] + [1] * (count - 1) if count else []) + [7, 0, 1]
                fb = [0, 0, 0x1234000] if module in (0x32, 0x35) else ()
                if not FULL and script != 'fast' and module not in (0x21, 0x32, 2):
                    continue
                run_load(elf, lib, (module, count, script), module, header, polls, fb)
                loads += 1
    # 0x2A/0x2B follow 0x70003B90.
    for module, spad in itertools.product((0x2A, 0x2B), (0, 1, 2)):
        run_load(elf, lib, ('spad', module, spad), module, header_bytes(module), [1, 1, 1], spad=spad)
        loads += 1
    # Single dispatches: gate, area streamers + 001FEF70 chaining, 0x63, idle states.
    for gate, state, ca4, ca6, left in itertools.product((0, 1), (1, 2), (0, 2, 0xFF), (0, 1, 2, 3, 4, 5),
                                                         (0x63, 0x62, None)):
        if state == 2 and (ca4, ca6) != (0, 0):
            continue
        user = [0] * 24; user[0] = state; user[1:6] = [3, 4, 5, 6, 7]; user[6] = 0x77
        rig = LoaderRig(elf, capture_header, user=user, area=[left], gate=gate, ca=(ca4, ca6))
        rig.dispatch(lib, ('area', gate, state, ca4, ca6, left))
        n += 1
    for state, gate in itertools.product((0x63, 3, 0x62, 0xFF), (0, 1)):
        user = [0] * 24; user[0] = state
        rig = LoaderRig(elf, capture_header, user=user, gate=gate)
        rig.dispatch(lib, ('state', state, gate))
        n += 1
    # 001FF830 steps 8..0xFF do nothing; step 3/5/7 from arbitrary cursors.
    for step in (3, 5, 7, 8, 0x20, 0xFF):
        for kind in (0, 1, 2, 3):
            user = [0] * 24; user[1] = step; user[6] = 0x21; user[7] = kind
            rig = LoaderRig(elf, header_bytes(0x21, 1, 1, (0x02000100,)), user=user, globals_=GLOBALS)
            rig.dispatch(lib, ('step', step, kind))
            n += 1
    # A relocation index past D_0028A5A0: the original writes outside the
    # modelled table; the native module faults instead.
    user = [0] * 24; user[1] = 7; user[6] = 0x21
    rig = LoaderRig(elf, header_bytes(0x21, 1, 0, (0x44000000,)), user=user, globals_=GLOBALS)
    rig.o.run(0x1FF0D0)
    assert rig.o.load(0x28A5A0) == GLOBALS['d275C74'], 'reloc 0x44 did not alias D_0028A5A0'
    fault = Fault()
    r = lib.em_status_scene_loader_001FF0D0(rig.slot, rig.user, C.byref(rig.ld),
                                            C.byref(rig.native_workers()), C.byref(fault))
    assert r == -1 and fault.code == 4 and fault.address == 0x28A5A0, ('reloc fault', r, fault.code)
    return n, loads


# ---------------------------------------------------- I: captures ---------

def ram(path):
    data = path.read_bytes()
    assert len(data) >= 0x2000000, path
    return data


def word(m, a): return struct.unpack_from('<I', m, a)[0]


class CaptureOracle(SceneOracle):
    """Reads every byte it has not written from a captured RAM image."""

    def __init__(self, elf, image):
        super().__init__(elf)
        self.image = image

    def load(self, address, size=4):
        if address in self.mem:
            return sum(self.mem.get(address + i, 0) << (8 * i) for i in range(size))
        address &= 0x1FFFFFF
        return int.from_bytes(self.image[address:address + size], 'little')


def check_capture_models(elf, lib, m):
    """status-hub: 0020CDC0's sub-state 0 over a cleared native pool
    (001AFF10 for the menu player, then 0020E250's letters), one 0020E460
    state-0 call per letter, and 0020E6F0 until the captured breathe/yaw;
    every modelled byte of pool records 0..6 equals the capture."""
    ca = m[0x810CA4:0x810CA8]
    bank = word(m, 0x28A56C)
    pool = Pool()
    lib.em_status_scene_clear_001AFE60(C.byref(pool))
    look = CaptureOracle(elf, m)

    def alloc(_, out):
        out[0] = lib.em_status_scene_alloc_001AFF10(C.byref(pool)); return 0

    def lookup(_, b, code, out):  # the ORIGINAL 001C6120 over the captured RAM
        look.r = [0] * 32; look.r[28], look.r[29] = 0x27D370, STACK
        look.mem = {}
        look.run(0x1C6120, (b, code & MASK))
        out[0] = look.r[2] & MASK
        return 0

    def bind(_, a, model):
        a.contents.w44 = model; return 0

    def bones(_, w44, out):  # 001C6150 is the byte at model + 8
        out[0] = m[w44 + 8]; return 0

    def mode(_, a, value):  # 001CA5F0: jump-table entry 0xB is 0x1CB580
        assert value == 0xB
        a.contents.w4C = 0x1CB580; return 0
    ok = lambda *_: 0
    slots = iter(range(0x5000, 0x6000))

    def slot(_, out):
        out[0] = next(slots); return 0
    player = lib.em_status_scene_alloc_001AFF10(C.byref(pool))
    assert player and C.addressof(player.contents) == C.addressof(pool.record[0])
    player.contents.w10 = 0x20E6F0
    fault = Fault()
    assert lib.em_status_scene_letters_0020E250((C.c_uint8 * 4)(*ca), bank,
                                                C.byref(workers(w_001AFF10=alloc, w_001C6120=lookup,
                                                                w_001CA6E0=bind, w_001C6150=bones)),
                                                C.byref(fault)) == 0, fault.code
    view = [number(word(m, 0x810610 + 4 * i)) for i in range(12)]
    lg = LetterGlobals()
    lg.d275BCC = 0x7FFF
    for i, v in enumerate(view): lg.view[i] = v
    lw = workers(w_001AF7C0=slot, w_001CB5B0=ok, w_001C62C0=ok, w_001CA5F0=mode)
    letters = 0
    for i in range(1, 24):
        rec = m[POOL_BASE + 0x2F0 * i:POOL_BASE + 0x2F0 * (i + 1)]
        a = pool.record[i]
        if a.w10 == 0:
            assert rec[0] == 0 or word(rec, 0x10) != 0x20E460, ('extra captured letter', i)
            continue
        assert rec[0] != 0 and word(rec, 0x10) == a.w10 == 0x20E460, ('letter callback', i)
        assert lib.em_status_scene_letter_0020E460(C.pointer(a), C.byref(lg), C.byref(lw),
                                                   C.byref(fault)) == 1, fault.code
        for name, (off, size) in ACTOR_FIELDS.items():
            assert int.from_bytes(rec[off:off + size], 'little') == field_bits(getattr(a, name), size), \
                ('letter', i, name)
        for name, off, n in ACTOR_ARRAYS:
            for k in range(n):
                assert word(rec, off + 4 * k) == bits(getattr(a, name)[k]), ('letter', i, name, k)
        letters += 1
    assert letters == 6, letters
    # Menu player: state 0 over the captured globals, then state 1 until the
    # breathe/yaw pair reaches the captured one.
    rec = m[POOL_BASE:POOL_BASE + 0x2F0]
    assert word(rec, 0x10) == 0x20E6F0 and rec[4] == 1, 'menu player record'
    g = PlayerGlobals()
    g.d8104E4, g.d810C60 = m[0x8104E4], m[0x810C60]
    for name, (addr, size) in PG_ADDR.items():
        if size == 4: setattr(g, name, word(m, addr))
    for i in range(12): g.view[i] = view[i]
    g.health, g.infection = number(word(m, 0x810858)), number(word(m, 0x81085C))
    a = pool.record[0]
    w = workers(w_001CA6E0=bind, w_001C6150=bones, w_001AF7C0=slot, w_001CB5B0=ok,
                w_001C63E0=ok, w_001CA5F0=mode, w_001C67E0=ok, w_001C64F0=ok, w_0020EC80=ok)
    target = (rec[5], word(rec, 0x38), word(rec, 0xC4))
    for t in range(4000):
        assert lib.em_status_scene_player_0020E6F0(C.pointer(a), C.byref(g), C.byref(w), C.byref(fault)) == 1
        if t and (a.b05, bits(a.f38), bits(a.fC0[1])) == target:
            break
    else:
        raise AssertionError('captured menu-player breathe/yaw not on the trajectory')
    for name, (off, size) in ACTOR_FIELDS.items():
        assert int.from_bytes(rec[off:off + size], 'little') == field_bits(getattr(a, name), size), \
            ('menu player', name)
    for name, off, n in ACTOR_ARRAYS:
        for k in range(n):
            assert word(rec, off + 4 * k) == bits(getattr(a, name)[k]), ('menu player', name, k)
    return letters, t


def item_wait_rows(trace):
    """(row of ITEM state 3 step 0, first row of state 5 after it) where the
    ITEM root (UI+4) waits in state 3 step 1 on module 0x21."""
    rows = json.loads(trace.read_text())['rows']
    start = end = None
    for i, row in enumerate(rows):
        ui = bytes.fromhex(row['ui'])
        if start is None and ui[4] == 3 and ui[5] == 0 and ui[6] == 5:
            start = i
        elif start is not None and ui[4] == 5:
            end = i
            break
        elif start is not None:
            assert ui[4] == 3 and (ui[5] == 1 or i == start), ('unexpected ITEM row', i, row['ui'])
    assert start is not None and end is not None, trace
    return start, end


def check_capture_loader(elf, lib, m, routes):
    """The captured slot-2 record, header and cursors are exactly what the
    native loader leaves after 001FF080(0, 0x21) over that header; the route
    01/03 ITEM wait spans 24 dispatches, the native minimum is 10."""
    header = m[HEADER:HEADER + 0x800]
    assert word(m, HEADER) == 0x21, 'captured header is not module 0x21'
    g = {name: word(m, addr) for name, addr in LD_WORDS}
    g_before = dict(g)
    g_before['d275C74'] = 0  # written by the load itself
    rig = LoaderRig(elf, header, polls=[1] * 3, globals_=g_before)
    lib.em_status_scene_loader_request_001FF080(rig.slot, rig.user, 0, 0x21)
    rig.slot[0] = 2
    fault = Fault()
    w = rig.native_workers()
    dispatches = 0
    while rig.slot[0]:
        dispatches += 1
        assert lib.em_status_scene_loader_001FF0D0(rig.slot, rig.user, C.byref(rig.ld), C.byref(w),
                                                   C.byref(fault)) == 0, fault.code
        assert dispatches < 100
    assert list(rig.user) == list(m[SLOT + 8:SLOT + 0x20]), ('slot bytes', bytes(rig.user).hex(),
                                                              m[SLOT + 8:SLOT + 0x20].hex())
    assert m[SLOT] == 0 and word(m, SLOT + 4) == 0x1FF0D0 and m[0x275BD8] == 0 and rig.ld.d275BD8 == 0
    for name, addr in LD_WORDS:
        assert getattr(rig.ld, name) == word(m, addr), ('cursor', name)
    waits = {}
    for route in routes:
        start, end = item_wait_rows(ROUTE / route / 'trace.json')
        waits[route] = end - start - 1  # dispatches from the 001FF080 frame to the BD8 clear
    return dispatches, waits


# ---------------------------------------------------------------- main ----

def main():
    started = time.time()
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    out, lib = build()
    counts = {}
    counts['glyph'] = check_glyph(elf, lib)
    counts['pool'] = check_pool(elf, lib)
    letters, e250_total = check_letters(elf, lib)
    counts['letters'] = letters
    counts['letter_behaviour'] = check_letter_behaviour(elf, lib)
    player, player_ticks = check_player(elf, lib)
    counts['player'] = player
    counts['player_lockstep_ticks'] = player_ticks
    counts['publish'] = check_publish(elf, lib)
    counts['glow'] = check_glow(elf, lib)
    route03 = ram(ROUTE / '03_panel_power/eeMemory.bin')
    loader_cases, loads = check_loader(elf, lib, route03[HEADER:HEADER + 0x800])
    counts['loader_dispatch_cases'] = loader_cases
    counts['loader_whole_loads'] = loads

    hub = ram(REF / 'status-hub/eeMemory.bin')
    cap_letters, player_frames = check_capture_models(elf, lib, hub)
    dispatches, waits = check_capture_loader(elf, lib, route03, ('01_battery', '03_panel_power'))
    assert dispatches == 10, dispatches
    assert set(waits.values()) == {24}, waits

    report = dict(status='PASS', mode='full' if FULL else 'quick', elf_sha256=ELF_SHA, cases=counts,
                  e250_cases_total=e250_total,
                  captured_letters=cap_letters, menu_player_frames_to_capture=player_frames,
                  loader_native_min_dispatches=dispatches, captured_item_wait_dispatches=waits,
                  original_functions=['001AFF10', '001AFF90', '001AF800', '001AFEB0', '001AFE60',
                                      '001B0000', '0020E1E0', '0020E250', '0020E3A0', '0020E460',
                                      '0020E6F0', '0020EC80', '001031E0', '001F4BF0',
                                      '001FF080', '001AB740', '001FF0D0', '001FF830', '001FF3F0',
                                      '001FEF70', '001AB7D0', '001C6120 (capture only)'])
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    banner(f"{counts['glyph']} glyph codes, {counts['pool']} pool cases",
           f"{letters} letter cases ({e250_total} 0020E250 inputs in full), "
           f"{counts['letter_behaviour']} 0020E460 cases",
           f"{player} player states + {player_ticks} lockstep ticks",
           f"{counts['publish']} 0020EC80 + {counts['glow']} 001F4BF0 cases",
           f"{loader_cases} loader dispatches + {loads} whole loads")
    print(f"Status scene workers: PASS; captures: pool records 0..6 (6 letters, menu player at "
          f"call {player_frames}), loader record; ITEM wait "
          f"{waits['03_panel_power']} dispatches in routes 01/03 vs native minimum {dispatches} "
          f"({time.time() - started:.1f} s)")


if __name__ == '__main__':
    main()
