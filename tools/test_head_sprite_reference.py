#!/usr/bin/env python3
"""Compare em_head_sprite_original with the original instructions.

The oracle executes, from the user's pinned boot ELF, the ORIGINAL code of
001E2560 (the effect-0x10 node behaviour), 001E23A0, 001B0070, 001CFA60,
001CFBE0, 001F0120 and 001E2290. Their callees are explicit worker stubs
(00122BB8 RNG, 001EF9D0 allocation, 001AFC10 free, the SDK matrix helpers
001026A0/001029C0/00102C58/00102918, 001CCF70 projection, 001CD370 and the
chain builders 001CB5F0/001CB6B0/001CB760/001CB900) that record every call
with its arguments and return the same scripted results to both sides.
After every call the test compares every modelled record byte, every
emitted packet byte and the ordered worker log, and asserts the original
wrote no byte outside the modelled record fields, the packet buffers and
the stack.

Capture check: in every original RAM image (build/s87/route/*, build/
startup-reference) each type-0x10 node's +0x244 lies on the 0.02 ramp from
0 under the EE add model, and for every node captured mid-ramp the
original 001E2560 (with its real callees, over the captured RAM and
scratchpad, from the previous ramp value) rewrites +0xB0 and +0xD0..+0x10F
and emits a packet chain byte-identical to the captured one. The native
module, with the SDK helpers bound to em_crate_original's exported
translations, reproduces the same bytes.

Arithmetic: tools/ee_float_model.py (docs/EE_FLOAT_MODEL.md) for add.s,
div.s, cvt.s.w and the compares.

No original instruction bytes, disassembly or data are written by this
file; the report in build/ holds only counts.
"""
import ctypes as C
import glob
import hashlib
import itertools
import json
import zlib
from pathlib import Path
import random
import struct
import subprocess
import sys

from test_crate_original_reference import Oracle as CrateOracle, branch_targets, dead_after_branch
from test_interaction_scan_reference import DECOMP, ELF_SHA
from test_point_light_reference import STACK, bits, number, signed
import ee_float_model as M
from reference_mode import FULL, banner, pick, select

ROOT = Path(__file__).resolve().parents[1]
ENTRY, SPAWN, GATE = 0x1E2560, 0x1F0120, 0x1E2290
REC, OWNER, XF, ST = 0x910000, 0x920000, 0x930000, 0x940000
BONES = 0x950000                   # slot addresses (matrices at slot + 0x90)
PKT = 0x980000                     # packet buffers returned by the 001CB5F0 stub
CTX = 0x600000                     # D_00275670 in synthetic runs
M40 = CTX + 0x2240                 # 001CD370(0)
POOL, RECORD = 0x7A5640, 0x2F0
SLOTS = 24
STEP, END = 0x3CA3D70A, 0x3FC00000
# The translated functions (start, size in bytes, from the decomp's FUNCTIONS.csv).
TRANSLATED = {0x1E2560: 0x294, 0x1E23A0: 0x1B8, 0x1B0070: 0xC, 0x1CFA60: 0x78, 0x1CFBE0: 0x400,
              0x1F0120: 0x68, 0x1E2290: 0x110}
PCS = set()                        # every original pc the synthetic groups executed
REC_FIELDS = {'lifecycle': (4, 1), 'sub': (5, 1), 'b09': (9, 1), 'b0C': (0xC, 1), 'key': (0xD, 1),
              'owner': (0x24, 4), 'bone': (0x28, 4), 'local': (0xA0, 16), 'pos': (0xB0, 16),
              'matrix': (0xD0, 64), 'timer': (0x1F0, 4), 'ramp': (0x244, 4), 'scalar': (0x24C, 4)}


class HeadOracle(CrateOracle):
    """The crate oracle (64-bit EE scalars, VU0 macro, captured RAM) with
    div/mfhi/mflo, 16-byte aligned lq/sq and the measured EE COP1 model."""
    hi = lo = 0

    def run(self, *args, **kw):
        super().run(*args, **kw)
        PCS.update(self.pcs)

    def plain(self, word):
        op, rs, rt, rd, fn = word >> 26, word >> 21 & 31, word >> 16 & 31, word >> 11 & 31, word & 63
        if op == 0 and fn == 26:
            a, b = signed(self.r[rs]), signed(self.r[rt])
            assert b, 'div by zero'
            q = abs(a) // abs(b) * (1 if (a < 0) == (b < 0) else -1)
            self.lo, self.hi = q, a - q * b
            return
        if op == 0 and fn in (16, 18):
            self.r[rd] = signed((self.hi if fn == 16 else self.lo) & 0xffffffff) & 0xffffffffffffffff
            return
        if op in (30, 31):
            address = (self.r[rs] + signed(word & 0xffff, 16)) & 0xfffffff0
            if op == 30: self.r[rt] = self.load(address, 16)
            else: self.save(address, self.r[rt] & ((1 << 128) - 1), 16)
            return
        if op == 17 and rs == 16 and fn in (0, 1, 3, 50, 52, 54):
            a, b, d = self.f[rd], self.f[rt], word >> 6 & 31
            if fn == 0: self.f[d] = M.ee_add(a, b)
            elif fn == 1: self.f[d] = M.ee_sub(a, b)
            elif fn == 3: self.f[d] = M.ee_div(a, b)
            else: self.condition = bool({50: M.ee_c_eq, 52: M.ee_c_lt, 54: M.ee_c_le}[fn](a, b))
            return
        if op == 17 and rs == 20 and fn == 32:
            self.f[word >> 6 & 31] = M.ee_cvt_s_w(self.f[rd])
            return
        super().plain(word)


# ------------------------------------------------------------------ ctypes

class Rec(C.Structure):
    _fields_ = [('self', C.c_uint32), ('lifecycle', C.c_uint8), ('sub', C.c_uint8),
                ('b09', C.c_uint8), ('b0C', C.c_uint8), ('key', C.c_uint8),
                ('owner', C.c_uint32), ('bone', C.c_int32), ('local', C.c_float * 4),
                ('pos', C.c_float * 4), ('matrix', C.c_float * 16), ('timer', C.c_int32),
                ('ramp', C.c_float), ('scalar', C.c_float), ('freed', C.c_uint8)]


class Owner(C.Structure):
    _fields_ = [('address', C.c_uint32), ('b01', C.c_uint8), ('b02', C.c_uint8),
                ('b04', C.c_uint8), ('f220', C.c_float), ('slots', C.POINTER(C.c_uint32)),
                ('slot_count', C.c_uint32), ('rot', C.POINTER(C.c_float))]


class Tables(C.Structure):
    _fields_ = [('d2535F0', C.c_uint8 * 128), ('d253670', C.c_uint8 * 0x90),
                ('d251260', C.c_uint8 * 128)]


class World(C.Structure):
    _fields_ = [('d8106C8', C.c_int32), ('d810E80', C.c_int16), ('cursor', C.c_uint32),
                ('scratch_3A40', C.POINTER(C.c_uint8)), ('scratch_3AC0', C.POINTER(C.c_uint8)),
                ('ctx_A0', C.POINTER(C.c_uint8)), ('tables', C.POINTER(Tables))]


class Xf(C.Structure):
    _fields_ = [('q', C.c_uint8 * 64), ('m40', C.c_uint32), ('m40_bytes', C.POINTER(C.c_uint8)),
                ('w44', C.c_uint32), ('w48', C.c_uint32), ('w4C', C.c_uint32),
                ('w50', C.c_uint32), ('w54', C.c_uint32)]


class Source(C.Structure):
    _fields_ = [('address', C.c_uint32), ('bytes', C.POINTER(C.c_uint8))]


class Fault(C.Structure):
    _fields_ = [('address', C.c_uint32), ('code', C.c_int32)]


FP, U8P = C.POINTER(C.c_float), C.POINTER(C.c_uint8)
W_RNG = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_int32))
W_ALLOC = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_uint32, C.c_float, C.POINTER(C.POINTER(Rec)))
W_FREE = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32)
W_APPLY = C.CFUNCTYPE(C.c_int, C.c_void_p, FP, C.c_uint32, FP)
W_IDENT = C.CFUNCTYPE(C.c_int, C.c_void_p, FP)
W_EULER = C.CFUNCTYPE(C.c_int, C.c_void_p, FP, FP)
W_TRANS = C.CFUNCTYPE(C.c_int, C.c_void_p, FP, FP, FP)
W_PROJ = C.CFUNCTYPE(C.c_int, C.c_void_p, FP, C.POINTER(C.c_int32))
W_M40 = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int32, C.POINTER(C.c_uint32), C.POINTER(U8P))
W_OPEN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_int32, C.c_int32, C.POINTER(U8P))
W_REF = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_int32, C.c_int32, C.c_uint32)
W_TBL = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_int32, C.c_uint32, C.c_uint32)
W_MODE = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_int32, C.c_int32)


class Workers(C.Structure):
    _fields_ = [('ctx', C.c_void_p), ('w_00122BB8', W_RNG), ('w_001EF9D0', W_ALLOC),
                ('w_001AFC10', W_FREE), ('w_001026A0', W_APPLY), ('w_001029C0', W_IDENT),
                ('w_00102C58', W_EULER), ('w_00102918', W_TRANS), ('w_001CCF70', W_PROJ),
                ('w_001CD370', W_M40), ('w_001CB5F0', W_OPEN), ('w_001CB6B0', W_REF),
                ('w_001CB760', W_TBL), ('w_001CB900', W_MODE)]


def build():
    out = ROOT/'build/head_sprite_reference'; out.mkdir(parents=True, exist_ok=True)
    lib_path = out/('head_sprite.dylib' if sys.platform == 'darwin' else 'head_sprite.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-shared', '-fPIC', '-Isrc', 'src/game/em_head_sprite_original.c',
                    'src/game/em_crate_original.c', '-lm', '-o', str(lib_path)], cwd=ROOT, check=True)
    lib = C.CDLL(str(lib_path))
    lib.em_head_sprite_original_tick.argtypes = [C.POINTER(Rec), C.POINTER(Owner), C.POINTER(World),
                                                 C.POINTER(Workers), C.POINTER(Fault)]
    lib.em_head_sprite_original_spawn_001F0120.argtypes = [C.c_uint32, C.c_int32, C.POINTER(Workers),
                                                           C.POINTER(C.POINTER(Rec)), C.POINTER(Fault)]
    lib.em_head_sprite_original_001CFBE0.argtypes = [C.c_int32, C.c_uint32, C.POINTER(Source),
                                                     C.POINTER(Xf), C.c_int32, C.POINTER(World),
                                                     C.POINTER(Workers), C.POINTER(Fault)]
    lib.em_head_sprite_original_load_tables.argtypes = [C.c_char_p, C.c_size_t, C.POINTER(Tables)]
    lib.em_head_sprite_original_001E2290.argtypes = [C.c_int32]
    lib.em_head_sprite_original_001E23A0.argtypes = [C.POINTER(Rec), C.POINTER(Tables), C.POINTER(Fault)]
    lib.em_head_sprite_original_001CFA60.argtypes = [C.POINTER(Xf), FP, C.c_uint32, C.c_uint32,
                                                     C.POINTER(Workers), C.POINTER(Fault)]
    for name in ('add', 'div'):
        getattr(lib, 'em_head_sprite_ee_' + name).argtypes = [C.c_uint32, C.c_uint32]
        getattr(lib, 'em_head_sprite_ee_' + name).restype = C.c_uint32
    lib.em_head_sprite_ee_c_le.argtypes = [C.c_uint32, C.c_uint32]
    lib.em_head_sprite_ee_cvt_s_w.argtypes = [C.c_int32]
    lib.em_head_sprite_ee_cvt_s_w.restype = C.c_uint32
    lib.em_crate_sdk_apply.argtypes = [FP, FP, FP]
    lib.em_crate_sdk_identity.argtypes = [FP]
    lib.em_crate_sdk_euler.argtypes = [FP, FP]
    lib.em_crate_sdk_translate.argtypes = [FP, FP, FP]
    return lib, out


def pbits(pointer, count): return struct.unpack('<%dI' % count, C.string_at(pointer, 4 * count))
def set_bits(obj, name, value):
    C.memmove(C.addressof(obj) + getattr(type(obj), name).offset, struct.pack('<I', value & 0xffffffff), 4)
def get_raw(obj, name, size):
    return C.string_at(C.addressof(obj) + getattr(type(obj), name).offset, size)
def f32(value): return number(bits(value))
def up(value): return number(bits(value) + (1 if value >= 0 else -1)) if value else number(1)
def down(value): return number(bits(value) - (1 if value > 0 else -1))
def u8p(buffer): return C.cast(buffer, U8P)
def raw(words): return b''.join(struct.pack('<I', w & 0xffffffff) for w in words)


# ------------------------------------------------------------------ one case

class Script:
    """Scripted worker results: the same per-worker sequence for both sides."""

    def __init__(self, seed, rng_values=None, alloc=True, handle=None):
        g = random.Random(seed)
        self.rng_values = list(rng_values) if rng_values is not None else \
            [g.choice((0, 1, 39, 40, 0x7fffffff, g.randrange(1 << 31))) for _ in range(4)]
        self.vec = [raw(bits(f32(g.uniform(-400, 400))) for _ in range(4)) for _ in range(4)]
        self.mat = [raw(bits(f32(g.uniform(-2, 2))) for _ in range(16)) for _ in range(12)]
        self.handle = handle if handle is not None else g.choice((0, 0x1234, 0xFFFFFF, -5, 0x7FFFFF0))
        self.alloc = alloc

    def rng(self, n): return self.rng_values[n % len(self.rng_values)]


class Case:
    def __init__(self, elf, lib, tables, script, owner, world, rec=None):
        self.lib, self.script = lib, script
        self.o = o = HeadOracle(elf)
        self.rec = rec if rec is not None else Rec()
        self.rec.self = REC
        self.owner_values = owner
        self.world_values = world
        # Native views.
        self.slots = (C.c_uint32 * SLOTS)(*[BONES + 0x100 * i for i in range(SLOTS)])
        self.rot = (C.c_float * 3)(*owner['rot'])
        self.owner = Owner(OWNER, owner['b01'], owner['b02'], owner['b04'], 0.0, self.slots, SLOTS, self.rot)
        set_bits(self.owner, 'f220', owner['f220'])
        self.s3A40 = (C.c_uint8 * 64)(*world['s3A40']); self.s3AC0 = (C.c_uint8 * 64)(*world['s3AC0'])
        self.ctxA0 = (C.c_uint8 * 16)(*world['ctxA0']); self.m40 = (C.c_uint8 * 64)(*world['m40'])
        self.tables = tables
        self.world = World(world['d8106C8'], world['d810E80'], world['cursor'], u8p(self.s3A40),
                           u8p(self.s3AC0), u8p(self.ctxA0), C.pointer(tables))
        # Oracle memory.
        o.save(OWNER + 1, owner['b01'], 1); o.save(OWNER + 2, owner['b02'], 1)
        o.save(OWNER + 4, owner['b04'], 1); o.save(OWNER + 0x220, owner['f220'])
        o.save(OWNER + 0x14, owner['w14'])
        for i in range(SLOTS): o.save(OWNER + 0x110 + 4*i, BONES + 0x100 * i)
        for i, v in enumerate(owner['rot']): o.save(OWNER + 0xC0 + 4*i, bits(v))
        o.save(0x8106C8, world['d8106C8'] & 0xffffffff); o.save(0x810E80, world['d810E80'] & 0xffff, 2)
        o.save(CTX + 0x18, world['cursor'])
        o.write(0x70003A40, bytes(world['s3A40'])); o.write(0x70003AC0, bytes(world['s3AC0']))
        o.write(CTX + 0xA0, bytes(world['ctxA0'])); o.write(M40, bytes(world['m40']))
        self.push()
        self.expected, self.actual = [], []
        self.o_packets, self.n_packets = [], []
        self.o_count, self.n_count = {}, {}
        o.calls.update({0x122BB8: self.o_rng, 0x1EF9D0: self.o_alloc, 0x1AFC10: self.o_free,
                        0x1026A0: self.o_apply, 0x1029C0: self.o_ident, 0x102C58: self.o_euler,
                        0x102918: self.o_trans, 0x1CCF70: self.o_proj, 0x1CD370: self.o_m40,
                        0x1CB5F0: self.o_open, 0x1CB6B0: self.o_log('001CB6B0', 4),
                        0x1CB760: self.o_log('001CB760', 4), 0x1CB900: self.o_log('001CB900', 3)})
        self.keep = []
        self.workers = Workers(None, W_RNG(self.n_rng), W_ALLOC(self.n_alloc), W_FREE(self.n_free),
                               W_APPLY(self.n_apply), W_IDENT(self.n_ident), W_EULER(self.n_euler),
                               W_TRANS(self.n_trans), W_PROJ(self.n_proj), W_M40(self.n_m40),
                               W_OPEN(self.n_open), W_REF(self.n_log('001CB6B0')),
                               W_TBL(self.n_log('001CB760')), W_MODE(self.n_log('001CB900')))

    # ---- shared script
    def take(self, counts, name):
        k = counts.get(name, 0); counts[name] = k + 1
        return k

    def field(self, address):
        """Native pointer -> original record offset name for the log."""
        for name, off in (('local', 0xA0), ('pos', 0xB0), ('matrix', 0xD0)):
            if address == C.addressof(getattr(self.rec, name)): return off
        if address == C.addressof(self.rot): return 'rot'
        return hex(address)

    def ofield(self, address):
        if address == OWNER + 0xC0: return 'rot'
        return address - REC if REC <= address < REC + RECORD else hex(address)

    # ---- original side
    def o_rng(self, r):
        self.expected.append(('00122BB8',))
        r.ret(self.script.rng(self.take(self.o_count, 'rng')))

    def o_alloc(self, r):
        self.expected.append(('001EF9D0', r.arg(0), r.arg(1), r.f[12]))
        r.ret(REC if self.script.alloc else 0)

    def o_free(self, r): self.expected.append(('001AFC10', r.arg(0)))

    def o_apply(self, r):
        self.expected.append(('001026A0', self.ofield(r.arg(0)), r.arg(1), r.words(r.arg(2), 4)))
        r.write(r.arg(0), self.script.vec[self.take(self.o_count, 'vec') % 4])

    def o_ident(self, r):
        self.expected.append(('001029C0', self.ofield(r.arg(0))))
        r.write(r.arg(0), self.script.mat[self.take(self.o_count, 'mat') % 12])

    def o_euler(self, r):
        assert r.arg(0) == r.arg(1)
        self.expected.append(('00102C58', self.ofield(r.arg(0)), r.words(r.arg(0), 16), r.words(r.arg(2), 3)))
        r.write(r.arg(0), self.script.mat[self.take(self.o_count, 'mat') % 12])

    def o_trans(self, r):
        self.expected.append(('00102918', self.ofield(r.arg(0)), self.ofield(r.arg(1)), self.ofield(r.arg(2)),
                              r.words(r.arg(1), 16), r.words(r.arg(2), 4)))
        r.write(r.arg(0), self.script.mat[self.take(self.o_count, 'mat') % 12])

    def o_proj(self, r):
        self.expected.append(('001CCF70', self.ofield(r.arg(0)), r.words(r.arg(0), 4)))
        r.ret(self.script.handle)

    def o_m40(self, r):
        self.expected.append(('001CD370', r.arg(0)))
        r.ret(M40)

    def o_open(self, r):
        n = self.take(self.o_count, 'open')
        self.expected.append(('001CB5F0', r.arg(0), signed(r.arg(1)), signed(r.arg(2))))
        self.o_packets.append((PKT + 0x800 * n, signed(r.arg(2))))
        r.ret(PKT + 0x800 * n)

    def o_log(self, name, argc):
        def call(r): self.expected.append((name,) + tuple(signed(r.arg(i)) for i in range(argc)))
        return call

    # ---- native side
    def n_rng(self, _, value):
        self.actual.append(('00122BB8',))
        value[0] = signed(self.script.rng(self.take(self.n_count, 'rng')))
        return 0

    def n_alloc(self, _, handle, a1, weight, out):
        self.actual.append(('001EF9D0', handle, a1, bits(weight)))
        out[0] = C.pointer(self.rec) if self.script.alloc else C.POINTER(Rec)()
        return 0

    def n_free(self, _, address):
        self.actual.append(('001AFC10', address)); return 0

    def n_apply(self, _, out, matrix, v):
        self.actual.append(('001026A0', self.field(C.addressof(out.contents)), matrix, pbits(v, 4)))
        C.memmove(out, self.script.vec[self.take(self.n_count, 'vec') % 4], 16); return 0

    def n_ident(self, _, m):
        self.actual.append(('001029C0', self.field(C.addressof(m.contents))))
        C.memmove(m, self.script.mat[self.take(self.n_count, 'mat') % 12], 64); return 0

    def n_euler(self, _, m, v):
        self.actual.append(('00102C58', self.field(C.addressof(m.contents)), pbits(m, 16), pbits(v, 3)))
        C.memmove(m, self.script.mat[self.take(self.n_count, 'mat') % 12], 64); return 0

    def n_trans(self, _, out, src, v):
        self.actual.append(('00102918', self.field(C.addressof(out.contents)),
                            self.field(C.addressof(src.contents)), self.field(C.addressof(v.contents)),
                            pbits(src, 16), pbits(v, 4)))
        C.memmove(out, self.script.mat[self.take(self.n_count, 'mat') % 12], 64); return 0

    def n_proj(self, _, pos, handle):
        self.actual.append(('001CCF70', self.field(C.addressof(pos.contents)), pbits(pos, 4)))
        handle[0] = self.script.handle; return 0

    def n_m40(self, _, a0, address, data):
        self.actual.append(('001CD370', a0 & 0xffffffff))
        address[0] = M40; data[0] = u8p(self.m40); return 0

    def n_open(self, _, a0, ident, count, out):
        self.actual.append(('001CB5F0', a0, ident, count))
        buf = (C.c_uint8 * (16 * max(count, 0)))(*([0xA5] * 16 * max(count, 0)))
        self.keep.append(buf); self.n_packets.append(buf)
        out[0] = u8p(buf); return 0

    def n_log(self, name):
        def call(_, *args):
            self.actual.append((name,) + tuple(signed(a & 0xffffffff) for a in args)); return 0
        return call

    # ---- state transfer
    def push(self):
        o, e = self.o, self.rec
        for name, (off, size) in REC_FIELDS.items():
            value = getattr(e, name)
            if size == 1 or name in ('owner',): o.save(REC + off, value, size)
            elif name in ('bone', 'timer'): o.save(REC + off, value & 0xffffffff)
            else: o.write(REC + off, get_raw(e, name, size))

    def original_record(self):
        return {name: self.o.read(REC + off, size) for name, (off, size) in REC_FIELDS.items()}

    def native_record(self):
        e = self.rec
        out = {}
        for name, (off, size) in REC_FIELDS.items():
            value = getattr(e, name)
            if size == 1: out[name] = bytes([value])
            elif name == 'owner': out[name] = struct.pack('<I', value)
            elif name in ('bone', 'timer'): out[name] = struct.pack('<i', value)
            else: out[name] = get_raw(e, name, size)
        return out

    def allowed(self):
        allowed = set()
        for off, size in REC_FIELDS.values(): allowed.update(range(REC + off, REC + off + size))
        for base, n in self.o_packets: allowed.update(range(base, base + 16 * n))
        return allowed

    def check_writes(self, label):
        changed = {a for a in self.o.writes if not STACK - 0x1000 <= a < STACK + 0x10}
        extra = changed - self.allowed()
        assert not extra, (label, sorted(hex(a) for a in extra)[:12])

    def check_packets(self, label):
        assert len(self.o_packets) == len(self.n_packets), label
        for (base, n), buf in zip(self.o_packets, self.n_packets):
            assert self.o.read(base, 16 * n) == bytes(buf), (label, 'packet', hex(base), n)

    def reset_logs(self):
        o = self.o
        o.r = [0] * 32; o.f = [0] * 32; o.r[28], o.r[29] = 0x27d370, STACK
        o.writes.clear()
        self.expected.clear(); self.actual.clear(); self.o_packets.clear(); self.n_packets.clear()

    def tick(self, label, expect_fault=0):
        self.reset_logs()
        self.o.run(ENTRY, (REC,))
        fault = Fault()
        result = self.lib.em_head_sprite_original_tick(C.byref(self.rec), C.byref(self.owner),
                                                        C.byref(self.world), C.byref(self.workers),
                                                        C.byref(fault))
        assert fault.code == expect_fault, (label, hex(fault.address), fault.code)
        self.check_writes(label)
        freed = any(c[0] == '001AFC10' for c in self.expected)
        assert result == (0 if freed else 1), (label, result)
        assert self.actual == self.expected, dict(case=label, actual=self.actual, expected=self.expected)
        self.check_packets(label)
        assert self.native_record() == self.original_record(), dict(
            case=label, native=self.native_record(), original=self.original_record())
        if freed: self.rec.freed = 0
        return result


def owner_values(g, **kw):
    v = dict(b01=1, b02=0, b04=0, f220=bits(10.0), w14=OWNER,
             rot=tuple(f32(g.uniform(-3.2, 3.2)) for _ in range(3)))
    v.update(kw)
    return v


def world_values(g, **kw):
    v = dict(d8106C8=0, d810E80=0, cursor=0x480000,
             s3A40=[g.randrange(256) for _ in range(64)], s3AC0=[g.randrange(256) for _ in range(64)],
             ctxA0=[g.randrange(256) for _ in range(16)], m40=[g.randrange(256) for _ in range(64)])
    v.update(kw)
    return v


def make_rec(lifecycle=1, sub=0, key=0x3B, bone=7, timer=5, ramp=0.0, scalar=0.0, g=None):
    g = g or random.Random(0)
    e = Rec()
    e.lifecycle, e.sub, e.key, e.bone, e.timer = lifecycle, sub, key, bone, timer
    e.b09, e.b0C, e.owner = 0x55, 0x66, OWNER
    set_bits(e, 'ramp', ramp if isinstance(ramp, int) else bits(ramp))
    set_bits(e, 'scalar', bits(scalar))
    for i in range(4): e.local[i] = f32(g.uniform(-2, 2)); e.pos[i] = f32(g.uniform(-9, 9))
    for i in range(16): e.matrix[i] = f32(g.uniform(-1, 1))
    return e


# ------------------------------------------------------------------ groups

def run_tick_cases(elf, lib, tables, cases, group):
    n = 0
    for label, rec_kw, owner_kw, world_kw, script_kw in cases:
        g = random.Random(zlib.crc32(repr(label).encode()))
        case = Case(elf, lib, tables, Script(n + 17, **script_kw), owner_values(g, **owner_kw),
                    world_values(g, **world_kw), make_rec(g=g, **rec_kw))
        case.tick((group,) + label)
        n += 1
    return n


def lifecycle0_cases():
    out = []
    keys = list(range(256))
    for key, flags, value in itertools.product(keys, (0, 8, 7, -1, 0x10), (0, 39, 40, 0x7fffffff, 12345)):
        out.append(((key, flags, value), dict(lifecycle=0, key=key), {}, dict(d8106C8=flags),
                    dict(rng_values=[value])))
    valid = {k for k in keys if k in (0x3B, 0x3D, 0x3E, 0x3F, 0x40, 0x47, 0x48, 0x49, 0x4E, 0x4F, 0x50,
                                      0x51, 0x54, 0x55, 0x58, 0x59, 0x5A, 0x61, 0x68, 0x6A)}
    return select(out, 300, 0xE23A0, axes=(lambda c: c[0][0], lambda c: c[0][1], lambda c: c[0][2]),
                  keep=lambda i, c: c[0][0] in valid and c[0][2] == 0)


def gate_cases():
    out = []
    healths = (0, 0x80000000, 1, 0x80000001, bits(1.0), bits(-1.0), 0x7FC00000, 0xFFC00000,
               0x7F800000, 0xFF800000, 0x7F7FFFFF)
    for b02, health, b04, b01 in itertools.product((0, 0x20, 1, 0x1F, 0xE0), healths, (0, 1, 2, 255), (0, 1)):
        out.append(((b02, health, b04, b01), dict(sub=0, timer=5), dict(b01=b01, b02=b02, b04=b04, f220=health),
                    {}, {}))
    return select(out, 160, 0xE2600, axes=(lambda c: c[0][0], lambda c: c[0][1], lambda c: c[0][2],
                                            lambda c: c[0][3]))


def sub_cases():
    out = []
    ramps = [bits(v) for v in (0.0, -0.0, f32(0.02), f32(1.46), f32(1.48), down(f32(1.48)), up(f32(1.48)),
                               f32(1.49), down(1.5), 1.5, up(1.5), f32(-1.0), f32(3e38), f32(1e-3))]
    ramps += [0x7FC00000, 0xFFC00000, 0x7F800000, 0xFF800000, 1, 0x80000001, 0x7F7FFFFF]
    for sub, timer, ramp, value in itertools.product((0, 1, 2, 255), (0, 1, -1, 100, -2**31, 2**31 - 1),
                                                     ramps, (0, 1, 0x7fffffff, 12345678, 40)):
        out.append(((sub, timer, ramp, value), dict(sub=sub, timer=timer, ramp=ramp),
                    {}, {}, dict(rng_values=[value])))
    return select(out, 220, 0xE26EC, axes=(lambda c: c[0][0], lambda c: c[0][1], lambda c: c[0][2],
                                            lambda c: c[0][3]))


def edge_cases():
    out = []
    for lifecycle in (2, 3, 4, 0x80, 255):
        out.append(((lifecycle,), dict(lifecycle=lifecycle), {}, {}, {}))
    for bone in (0, 7, SLOTS - 1):  # slot selection
        out.append((('bone', bone), dict(sub=1, bone=bone, ramp=0.5), {}, {}, {}))
    for e80, cursor in ((0, 0x4F35C0 - 0x8000), (0, 0x4F35C0 - 0x7FFF), (1, 0x5635C0 - 0x8000),
                        (1, 0x5635C0 - 0x7FFF), (0, 0x4F35C0 + 0x10), (-1, 0x480000),
                        (1, 0x4F35C0 - 0x8000), (0, 0xFFFFFFF0)):
        out.append((('guard', e80, cursor), dict(sub=1, ramp=0.5), {}, dict(d810E80=e80, cursor=cursor), {}))
    return out


def random_cases(count):
    g = random.Random(0x1E2560)
    out = []
    for n in range(count):
        out.append(((n,), dict(lifecycle=g.choice((0, 1, 1, 1, 1, 2, 3, 5)), sub=g.choice((0, 1, 1, 2)),
                               key=g.choice((0x3B, 0x47, 0x6A, 0x3C, g.randrange(256))),
                               bone=g.randrange(SLOTS), timer=g.randrange(-3, 100),
                               ramp=f32(g.choice((g.uniform(0, 1.6), round(g.uniform(0, 1.5) / 0.02) * 0.02))),
                               scalar=f32(g.random())),
                    dict(b01=g.choice((0, 1, 1)), b02=g.choice((0, 0, 0x21, 0x40)), b04=g.choice((0, 1, 2)),
                         f220=bits(f32(g.choice((g.uniform(-5, 50), 0.0))))),
                    dict(d8106C8=g.choice((0, 8, 0x10)), d810E80=g.choice((0, 1)),
                         cursor=g.choice((0x480000, 0x4F0000, 0x560000))),
                    dict(rng_values=[g.randrange(1 << 31) for _ in range(3)])))
    return out


def spawn_cases(elf, lib, tables):
    """001F0120 (+ 001E2290) against the native spawner."""
    keys = list(range(-2, 0x102)) + [0x13B, 0x7FFFFFFF, -0x80000000]
    g = random.Random(0x1F0120)
    n = 0
    for key, alloc in itertools.product(select(keys, 60, 0x1E2290, keep=lambda i, k: k in (0x3B, 0x47, 1)),
                                        (True, False)):
        w14 = g.randrange(1 << 32)
        case = Case(elf, lib, tables, Script(n, alloc=alloc), owner_values(g, w14=w14), world_values(g),
                    make_rec(g=g))
        case.reset_logs()
        o = case.o
        o.run(SPAWN, (OWNER, key & 0xffffffff))
        out = C.POINTER(Rec)(); fault = Fault()
        assert lib.em_head_sprite_original_spawn_001F0120(w14, key, C.byref(case.workers), C.byref(out),
                                                          C.byref(fault)) == 0
        assert fault.code == 0
        assert case.actual == case.expected, (key, case.actual, case.expected)
        gate = key in (0x3B, 0x3D, 0x3E, 0x3F, 0x40, 0x47, 0x48, 0x49, 0x4E, 0x4F, 0x50, 0x51, 0x54, 0x55,
                       0x58, 0x59, 0x5A, 0x61, 0x68, 0x6A)
        assert bool(out) == (gate and alloc) and signed(o.r[2]) == (REC if gate and alloc else 0), key
        case.check_writes(('spawn', key))
        assert case.native_record() == case.original_record(), ('spawn', key)
        # 001E2290 alone.
        o2 = HeadOracle(elf); o2.run(GATE, (key & 0xffffffff,))
        assert lib.em_head_sprite_original_001E2290(key) == signed(o2.r[2]) == int(gate), key
        n += 2
    return n


def cfbe0_cases(elf, lib, tables):
    """001CFBE0 alone: every mode x kind x copy, the guard and the undefined paths."""
    g = random.Random(0x1CFBE0)
    items = [(mode, kind, copy, False) for mode in (1, 2, 3, 4) for kind in range(7) for copy in (0, 1)]
    items = select(items, 40, 0xCFBE, axes=(lambda c: c[0], lambda c: c[1], lambda c: c[2]))
    n = 0
    for mode, kind, copy, real in items + [(1, 1, 0, True)]:   # True: the ELF's D_00253670
        st_bytes = bytes(tables.d253670) if real else bytes(g.randrange(256) for _ in range(0x8C)) + struct.pack('<i', mode)
        st_addr = 0x253670 if real else ST
        xf_bytes = bytes(g.randrange(256) for _ in range(0x58))
        xf_bytes = xf_bytes[:0x40] + struct.pack('<I', M40) + xf_bytes[0x44:]
        ident = g.choice((0, 0x1000, 0x7FFFFF, -1, 0xFFF000))
        case = Case(elf, lib, tables, Script(n), owner_values(g), world_values(g))
        o = case.o
        if not real: o.write(ST, st_bytes)
        o.write(XF, xf_bytes)
        case.reset_logs()
        o.run(0x1CFBE0, (ident & 0xffffffff, kind, st_addr, XF, copy))
        xf = Xf(); C.memmove(xf.q, xf_bytes, 64)
        xf.m40 = M40; xf.m40_bytes = u8p(case.m40)
        xf.w44, xf.w48, xf.w4C, xf.w50, xf.w54 = struct.unpack_from('<5I', xf_bytes, 0x44)
        sbuf = (C.c_uint8 * 0x90)(*st_bytes)
        st = Source(st_addr, u8p(sbuf)); fault = Fault()
        result = lib.em_head_sprite_original_001CFBE0(ident, kind, C.byref(st), C.byref(xf), copy,
                                                      C.byref(case.world), C.byref(case.workers), C.byref(fault))
        assert fault.code == 0 and result == 1, (mode, kind, copy, fault.code, result)
        assert case.actual == case.expected, (mode, kind, copy, case.actual, case.expected)
        case.check_packets(('1CFBE0', mode, kind, copy))
        case.check_writes(('1CFBE0', mode, kind, copy))
        n += 1
    # Guard: signed (end - cursor) < 0x8000 skips everything.
    for e80, cursor in ((0, 0x4F35C0 - 0x7FFF), (1, 0x5635C0 - 0x7FFF), (0, 0x4F35C0 + 4), (1, 0x5635C0),
                        (0, 0x4F35C0 - 0x8000), (7, 0x5635C0 - 0x8000), (0, 0x80000000)):
        case = Case(elf, lib, tables, Script(n), owner_values(g), world_values(g, d810E80=e80, cursor=cursor))
        o = case.o
        xf_bytes = bytes(64) + struct.pack('<I', M40) + bytes(20)
        o.write(XF, xf_bytes)
        case.reset_logs()
        o.run(0x1CFBE0, (0, 1, 0x253670, XF, 0))
        xf = Xf(); xf.m40 = M40; xf.m40_bytes = u8p(case.m40)
        st = Source(0x253670, u8p(tables.d253670)); fault = Fault()
        result = lib.em_head_sprite_original_001CFBE0(0, 1, C.byref(st), C.byref(xf), 0, C.byref(case.world),
                                                      C.byref(case.workers), C.byref(fault))
        emitted = bool(case.expected)
        assert fault.code == 0 and result == int(emitted) and case.actual == case.expected, (e80, cursor)
        case.check_packets(('guard', e80, cursor))
        n += 1
    # Undefined: mode outside 1..4 or kind >= 7 -> the original reads stale
    # s0/s1; the native faults before any worker call.
    for mode, kind in ((0, 1), (5, 1), (-1, 0), (1, 7), (2, 7), (1, 0xFFFFFFFF)):
        case = Case(elf, lib, tables, Script(n), owner_values(g), world_values(g))
        # The original does not skip: it emits the chain with whatever s0/s1
        # held (here the oracle's zeroed registers).
        case.o.write(ST, bytes(0x8C) + struct.pack('<i', mode))
        case.o.write(XF, bytes(64) + struct.pack('<I', M40) + bytes(20))
        case.reset_logs()
        case.o.run(0x1CFBE0, (0, kind, ST, XF, 0))
        assert case.expected[0][0] == '001CB5F0' and case.expected[-1][0] == '001CB900', (mode, kind)
        case.reset_logs()
        sbuf = (C.c_uint8 * 0x90)(*(bytes(0x8C) + struct.pack('<i', mode)))
        st = Source(ST, u8p(sbuf)); xf = Xf(); xf.m40_bytes = u8p(case.m40); fault = Fault()
        result = lib.em_head_sprite_original_001CFBE0(0, kind, C.byref(st), C.byref(xf), 0, C.byref(case.world),
                                                      C.byref(case.workers), C.byref(fault))
        assert result == -1 and fault.code == 6 and fault.address == 0x1CFBE0 and not case.actual, (mode, kind)
        n += 1
    return n


def lockstep(elf, lib, tables, ticks):
    """Spawn with 001F0120, then tick both sides from lifecycle 0 through
    whole wait/ramp cycles; the owner dies at the end (lifecycle 3, free)."""
    g = random.Random(0x7AC8D0)
    total = 0
    for key in (0x3B, 0x47):
        values = [g.randrange(1 << 31) for _ in range(64)]
        case = Case(elf, lib, tables, Script(key, rng_values=values), owner_values(g), world_values(g), Rec())
        case.reset_logs()
        case.o.run(SPAWN, (OWNER, key))
        out = C.POINTER(Rec)(); fault = Fault()
        lib.em_head_sprite_original_spawn_001F0120(OWNER, key, C.byref(case.workers), C.byref(out), C.byref(fault))
        assert out and case.actual == case.expected and case.native_record() == case.original_record()
        ramps = set()
        for t in range(ticks):
            if t == ticks - 2:  # the owner's +0x220 drops to 0: state 3, then the free
                case.owner.f220 = 0.0; case.o.save(OWNER + 0x220, 0)
            result = case.tick(('lockstep', key, t))
            total += 1
            if case.rec.sub == 1: ramps.add(bits(case.rec.ramp))
            if result == 0: break
        # sub 1 holds 0 (set at the wait's end) and every ramp value <= 1.5;
        # the overshoot is replaced by the 1.5 clamp in the same tick (sub 0).
        on_ramp = {k for k in ramp_trajectory() if M.ee_c_le(k, END)}
        assert result == 0 and ramps == on_ramp, (key, len(ramps), len(on_ramp))
    return total


# ------------------------------------------------------------------ captures

def ramp_trajectory():
    """{bits: steps} of the EE-add ramp from 0 (0.02 per tick up to 1.5)."""
    out, x, n = {0: 0}, 0, 0
    while True:
        x = M.ee_add(x, STEP); n += 1
        out[x] = n
        if not M.ee_c_le(x, END): return out


def capture_files():
    files = sorted(glob.glob(str(DECOMP/'build/s87/route/*/eeMemory.bin')))
    files += sorted({str(p) for p in (DECOMP/'build/startup-reference').rglob('*.bin')
                     if p.name in ('eeMemory.bin',) or p.name.endswith('_ee.bin')})
    return files


# The three ELF data blocks the module loads once from the ELF (address,
# size, Tables field). The doc states they never change at run time.
DATA_BLOCKS = ((0x2535F0, 128, 'd2535F0'), (0x253670, 0x90, 'd253670'), (0x251260, 128, 'd251260'))


def elf_block(elf, address, size):
    return elf[address - 0x100000 + 0x300:address - 0x100000 + 0x300 + size]


def capture_check(elf, lib, tables):
    trajectory = ramp_trajectory()
    stats = dict(images=0, nodes=0, ramping=0, replayed=0)
    for address, size, field in DATA_BLOCKS:
        assert bytes(getattr(tables, field)) == elf_block(elf, address, size), ('loader', field)
    for path in capture_files():
        ram = Path(path).read_bytes()
        if len(ram) != 0x2000000: continue
        stats['images'] += 1
        # D_002535F0, D_00253670 and D_00251260 are identical in the ELF and
        # in every captured RAM image.
        for address, size, _ in DATA_BLOCKS:
            assert ram[address:address + size] == elf_block(elf, address, size), (path, hex(address))
        scratch_path = Path(path).parent/'scratchpad.bin'
        for i in range(0x100):
            a = POOL + i * RECORD
            if struct.unpack_from('<I', ram, a + 0x10)[0] != ENTRY or ram[a + 4] != 1: continue
            stats['nodes'] += 1
            ramp = struct.unpack_from('<I', ram, a + 0x244)[0]
            assert ramp in trajectory or ramp == END, (path, hex(a), hex(ramp))  # END: the clamp
            if ram[a + 5] == 0: assert ramp in (0, END), (path, hex(a))
            if ram[a + 5] != 1: continue
            stats['ramping'] += 1
            # Replay only the route captures: they are taken at the frame
            # breakpoint 0x001AAF28 with no seeded writes. (startup-reference/
            # roger-encounter seeds player writes; clip47 has no scratchpad and
            # its owner bone moved after the node's tick in that image.)
            if '/s87/route/' not in path or not scratch_path.exists(): continue
            replay(elf, lib, tables, ram, scratch_path.read_bytes(), a, ramp, trajectory, (path, hex(a)))
            stats['replayed'] += 1
    assert stats['images'] and stats['replayed'] >= 10, stats
    return stats


def replay(elf, lib, tables, ram, scratch, a, ramp, trajectory, label):
    """Original (real callees) and native (crate SDK helpers) from the
    previous ramp value over the captured RAM; both must write the captured
    +0xB0/+0xD0 bytes and the captured packet chain."""
    previous = next(k for k, v in trajectory.items() if v == trajectory[ramp] - 1)
    ctx = struct.unpack_from('<I', ram, 0x275670)[0]
    key = struct.pack('<I', 0x6C050059) + ram[a + 0x244:a + 0x248] + struct.pack('<I', 0x3F800000) + \
        struct.pack('<I', 0x358637BD) + ram[a + 0x24C:a + 0x250] + ram[a + 0xD0:a + 0x110]
    p1 = ram.find(key) - 0xC
    assert p1 > 0 and ram.find(key, p1 + 0xD) < 0, label
    cursor = p1 - 0x20 - 0x100
    e80 = 0 if cursor < 0x4F35C0 else 1   # the image is taken after the buffer flip
    handle = 0x1000
    o = HeadOracle(elf, ram)
    for i in range(0, 0x4000): o.mem[0x70000000 + i] = scratch[i]
    o.save(0x275670, ctx); o.save(ctx + 0x18, cursor); o.save(0x810E80, e80, 2)
    o.save(a + 0x244, previous)
    o.calls[0x1CCF70] = lambda r: r.ret(handle)
    o.writes.clear()
    o.run(ENTRY, (a,))
    assert o.read(a + 0xB0, 16) == ram[a + 0xB0:a + 0xC0] and o.read(a + 0xD0, 64) == ram[a + 0xD0:a + 0x110], label
    span = [w for w in range(cursor + 0x100, p1 + 0x400) if w in o.writes]
    assert len(span) >= 0x1C0 and all(o.mem[w] == ram[w] for w in span), label

    # Native, with the SDK helpers bound to em_crate_original's translations.
    owner_addr = struct.unpack_from('<I', ram, a + 0x24)[0]
    rec = Rec(); rec.self = a; rec.lifecycle, rec.sub = ram[a + 4], ram[a + 5]
    rec.b09, rec.b0C, rec.key = ram[a + 9], ram[a + 0xC], ram[a + 0xD]
    rec.owner = owner_addr; rec.bone = struct.unpack_from('<i', ram, a + 0x28)[0]
    C.memmove(rec.local, ram[a + 0xA0:a + 0xB0], 16)
    rec.timer = struct.unpack_from('<i', ram, a + 0x1F0)[0]
    set_bits(rec, 'ramp', previous); set_bits(rec, 'scalar', struct.unpack_from('<I', ram, a + 0x24C)[0])
    slots = (C.c_uint32 * SLOTS).from_buffer_copy(ram, owner_addr + 0x110)
    rot = (C.c_float * 3).from_buffer_copy(ram, owner_addr + 0xC0)
    owner = Owner(owner_addr, ram[owner_addr + 1], ram[owner_addr + 2], ram[owner_addr + 4], 0.0, slots, SLOTS, rot)
    set_bits(owner, 'f220', struct.unpack_from('<I', ram, owner_addr + 0x220)[0])
    s40 = (C.c_uint8 * 64)(*scratch[0x3A40:0x3A80]); sC0 = (C.c_uint8 * 64)(*scratch[0x3AC0:0x3B00])
    cA0 = (C.c_uint8 * 16)(*ram[ctx + 0xA0:ctx + 0xB0]); m40 = (C.c_uint8 * 64)(*ram[ctx + 0x2240:ctx + 0x2280])
    world = World(0, e80, cursor, u8p(s40), u8p(sC0), u8p(cA0), C.pointer(tables))
    packets, keep, calls = [], [], []

    def apply(_, out, matrix, v):
        m = (C.c_float * 16).from_buffer_copy(ram, matrix); keep.append(m)
        lib.em_crate_sdk_apply(out, m, v); return 0

    def ident(_, m): lib.em_crate_sdk_identity(m); return 0
    def euler(_, m, v): lib.em_crate_sdk_euler(m, v); return 0

    def trans(_, out, src, v):
        tmp = (C.c_float * 16).from_buffer_copy(C.string_at(src, 64)); lib.em_crate_sdk_translate(out, tmp, v)
        return 0

    def proj(_, pos, h): h[0] = handle; return 0
    def m40w(_, a0, address, data): address[0] = ctx + 0x2240; data[0] = u8p(m40); return 0

    def open_(_, a0, ident_, count, out):
        buf = (C.c_uint8 * (16 * count))(); keep.append(buf); packets.append(buf)
        calls.append(('open', count)); out[0] = u8p(buf); return 0

    def log(name):
        def call(_, *args): calls.append((name,) + tuple(args)); return 0
        return call

    def fail(*_): raise AssertionError(('unexpected worker', label))
    workers = Workers(None, W_RNG(fail), W_ALLOC(fail), W_FREE(fail), W_APPLY(apply), W_IDENT(ident),
                      W_EULER(euler), W_TRANS(trans), W_PROJ(proj), W_M40(m40w), W_OPEN(open_),
                      W_REF(log('ref')), W_TBL(log('tbl')), W_MODE(log('mode')))
    fault = Fault()
    assert lib.em_head_sprite_original_tick(C.byref(rec), C.byref(owner), C.byref(world), C.byref(workers),
                                            C.byref(fault)) == 1 and fault.code == 0, (label, fault.code)
    assert bytes(rec.pos) == ram[a + 0xB0:a + 0xC0] and bytes(rec.matrix) == ram[a + 0xD0:a + 0x110], label
    assert get_raw(rec, 'ramp', 4) == ram[a + 0x244:a + 0x248], label
    # Chain layout of 001CB5F0/001CB6B0 (byte-matched C): a packet of n
    # quadwords lands at cursor + 0x120 and advances the cursor by (n + 2) * 16;
    # a reference/call tag advances it by 0x20.
    p, n = cursor, 0
    for call in calls:
        if call[0] == 'open':
            assert bytes(packets[n]) == ram[p + 0x120:p + 0x120 + 16 * call[1]], (label, n)
            p += (call[1] + 2) * 16; n += 1
        else:
            p += 0x20
    assert n == 3 and calls[1] == ('ref', 0x7635C0, handle, 9, 0x253670) and \
        calls[4] == ('tbl', 0x7635C0, handle, 0x231770, 0x251280) and calls[5] == ('mode', 0x7635C0, handle, 1), \
        (label, calls)


def latched_fault(lib, tables):
    """A fault already latched on entry: every entry point that takes the
    fault block returns -1 with no worker call and no write."""
    called = []   # a ctypes callback cannot raise into the caller: record instead

    def fail(*_): called.append(1); return 0
    workers = Workers(None, W_RNG(fail), W_ALLOC(fail), W_FREE(fail), W_APPLY(fail), W_IDENT(fail),
                      W_EULER(fail), W_TRANS(fail), W_PROJ(fail), W_M40(fail), W_OPEN(fail),
                      W_REF(fail), W_TBL(fail), W_MODE(fail))
    s40, sC0, cA0, m40 = (C.c_uint8 * 64)(), (C.c_uint8 * 64)(), (C.c_uint8 * 16)(), (C.c_uint8 * 64)()
    world = World(0, 0, 0, u8p(s40), u8p(sC0), u8p(cA0), C.pointer(tables))
    slots = (C.c_uint32 * SLOTS)(); rot = (C.c_float * 3)()
    n = 0
    for code in (1, 2, 3, 4, 6):
        for lifecycle, sub in ((0, 0), (1, 0), (1, 1), (3, 0)):
            rec = Rec(); rec.self = REC; rec.lifecycle, rec.sub = lifecycle, sub
            rec.key = 0x3B; rec.owner = OWNER; rec.bone = 7
            owner = Owner(OWNER, 1, 0, 0, 1.0, slots, SLOTS, rot)
            before = bytes(rec); fault = Fault(0x123456, code)
            assert lib.em_head_sprite_original_tick(C.byref(rec), C.byref(owner), C.byref(world),
                                                    C.byref(workers), C.byref(fault)) == -1
            assert bytes(rec) == before and (fault.address, fault.code) == (0x123456, code)
            n += 1
        out = C.POINTER(Rec)(Rec()); fault = Fault(0x123456, code)
        assert lib.em_head_sprite_original_spawn_001F0120(OWNER, 0x3B, C.byref(workers), C.byref(out),
                                                          C.byref(fault)) == -1
        assert (fault.address, fault.code) == (0x123456, code)
        rec = Rec(); rec.key = 0x3B; before = bytes(rec); fault = Fault(0x123456, code)
        assert lib.em_head_sprite_original_001E23A0(C.byref(rec), C.byref(tables), C.byref(fault)) == -1
        assert bytes(rec) == before and (fault.address, fault.code) == (0x123456, code)
        xf = Xf(); before = bytes(xf); src = (C.c_float * 16)(); fault = Fault(0x123456, code)
        assert lib.em_head_sprite_original_001CFA60(C.byref(xf), src, STEP, END, C.byref(workers),
                                                    C.byref(fault)) == -1
        assert bytes(xf) == before and (fault.address, fault.code) == (0x123456, code)
        st = Source(0x253670, u8p(tables.d253670)); xf = Xf(); xf.m40_bytes = u8p(m40)
        fault = Fault(0x123456, code)
        assert lib.em_head_sprite_original_001CFBE0(0, 1, C.byref(st), C.byref(xf), 0, C.byref(world),
                                                    C.byref(workers), C.byref(fault)) == -1
        assert (fault.address, fault.code) == (0x123456, code)
        n += 4
    assert not called, ('worker called with a latched fault', len(called))
    return n


# ------------------------------------------------------------------ main

def coverage(elf):
    """Every reachable instruction of the seven translated functions ran.
    Quick mode keeps every case class, so the same holds there."""
    missed = []
    for start, size in TRANSLATED.items():
        targets = branch_targets(elf, start, start + size)
        missed += [hex(pc) for pc in range(start, start + size, 4)
                   if pc not in PCS and not dead_after_branch(elf, pc, targets)]
    assert not missed, ('unexecuted original instructions', missed)
    return missed

def ee_helpers(lib):
    """The module's EE helpers against tools/ee_float_model.py."""
    g = random.Random(0xEEF)
    specials = [0, 0x80000000, 1, 0x80000001, 0x7F7FFFFF, 0xFF7FFFFF, 0x7F800000, 0xFF800000, 0x7FC00000,
                0xFFC00000, STEP, END, bits(1.49), bits(-1.0), 0x4F000000, 0x00800000, 0x80800000, 0x33800000]
    values = specials + [g.randrange(1 << 32) for _ in range(pick(20000, 1500))] + \
        [bits(f32(g.uniform(-2, 2))) for _ in range(pick(20000, 1500))]
    pairs = [(a, b) for a in specials for b in specials] + list(zip(values, reversed(values)))
    for a, b in pairs:
        assert lib.em_head_sprite_ee_add(a, b) == M.ee_add(a, b), ('add', hex(a), hex(b))
        assert lib.em_head_sprite_ee_div(a, b) == M.ee_div(a, b), ('div', hex(a), hex(b))
        assert lib.em_head_sprite_ee_c_le(a, b) == M.ee_c_le(a, b), ('c.le', hex(a), hex(b))
    ints = [0, 1, -1, 2**31 - 1, -2**31, 2**24 + 1, -(2**24 + 3), 0x7fffffc0] + \
        [g.randrange(-2**31, 2**31) for _ in range(pick(20000, 1000))]
    for i in ints:
        assert lib.em_head_sprite_ee_cvt_s_w(i) == M.ee_cvt_s_w(i & 0xffffffff), ('cvt', i)
    return len(pairs) + len(ints)


def main():
    elf = (DECOMP/'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    lib, out = build()
    tables = Tables()
    assert lib.em_head_sprite_original_load_tables(elf, len(elf), C.byref(tables)) == 0
    assert struct.unpack_from('<i', bytes(tables.d253670), 0x8C)[0] == 1   # D_00253670 mode word
    counts = {}
    counts['ee_helpers'] = ee_helpers(lib)
    counts['lifecycle0'] = run_tick_cases(elf, lib, tables, lifecycle0_cases(), 'L0')
    counts['gates'] = run_tick_cases(elf, lib, tables, gate_cases(), 'G')
    counts['sub_states'] = run_tick_cases(elf, lib, tables, sub_cases(), 'S')
    counts['edges'] = run_tick_cases(elf, lib, tables, edge_cases(), 'E')
    counts['random'] = run_tick_cases(elf, lib, tables, random_cases(pick(2000, 120)), 'R')
    counts['spawn'] = spawn_cases(elf, lib, tables)
    counts['cfbe0'] = cfbe0_cases(elf, lib, tables)
    counts['latched_fault'] = latched_fault(lib, tables)
    counts['lockstep_ticks'] = lockstep(elf, lib, tables, pick(900, 420))
    unexecuted = coverage(elf)
    captures = capture_check(elf, lib, tables)
    report = dict(status='PASS', mode='full' if FULL else 'quick', elf_sha256=ELF_SHA, cases=counts,
                  captures=captures, unexecuted_instructions=len(unexecuted),
                  original_functions=['001E2560', '001E23A0', '001B0070', '001CFA60', '001CFBE0',
                                      '001F0120', '001E2290'],
                  workers=['00122BB8', '001EF9D0', '001AFC10', '001026A0', '001029C0', '00102C58',
                           '00102918', '001CCF70', '001CD370', '001CB5F0', '001CB6B0', '001CB760',
                           '001CB900'])
    (out/'report.json').write_text(json.dumps(report, indent=2) + '\n')
    total = sum(v for k, v in counts.items() if k not in ('lockstep_ticks', 'ee_helpers'))
    banner(f'{total} oracle cases', f"{counts['lockstep_ticks']} lockstep ticks",
           f"{counts['ee_helpers']} EE helper vectors")
    print(f"Original head sprite 001E2560: PASS; {captures['nodes']} captured nodes on the ramp from "
          f"{captures['images']} RAM images, {captures['replayed']} mid-ramp ticks replayed byte-identical "
          f"(record + packet chain), native with the crate SDK helpers")


if __name__ == '__main__':
    main()
