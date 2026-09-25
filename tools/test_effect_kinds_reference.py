#!/usr/bin/env python3
"""Compare the native effect kinds (em_effect_kinds.c) with the original code.

docs/EFFECT_KINDS.md. The oracle is our own bounded EE interpreter (the EE
class of tools/test_effect_original_reference.py, extended here with MULT).
It executes the ORIGINAL instructions of the pinned boot ELF (the user's
config/SCUS_971.12) for

  001EC1F0 001EC3F0 001EC470 001EBF10   the per-subtype draw handlers
  001CFB50 001D0540                     their transform block and depth scale
  001F54E0                              the effect colour
  001F5640 001F5CA0 001F6760 001F6D60   the room list selectors
  001F5940 001F5C20                     the glow markers
  001F0310 001F03D0 001F3FA0            the effect-pool resets
  001F6640 001F66F0 001F6850 001F68B0 001F6E40   the room point-light lists
  001029C0                              the identity leaf they reach

COP1 and VU0 arithmetic come from tools/ee_float_model.py (the imported EE
routes every float op through it). Memory is the ELF image (vram 0x100000..)
or a captured route snapshot (../Extermination/build/s87/route/<beat>/) or
the captured opening (../Extermination/build/startup-reference/opening_ee.bin).

Workers (recorded as calls, scripted results; the native module gets the
same script): 001D7FA0, 001D80B0, 001F4D40, 0011DF78, float_to_int
001281C0, 0021B9A0, 00122BB8, the 001F54E0 indirect call, 001CFB50,
001CFBE0. After every call: every byte the original changed must be a byte
the native module models (or stack), every modelled byte must equal the
native value, and the worker call sequences (with argument bits and the
bytes the pointer arguments point at) must be equal. The test also asserts
that every instruction of every translated function executed at least once.

Capture evidence:
- every route beat: the room glow markers 001F5C20 over the captured RAM and
  clock (AREA11 key 0x0B00: 11 markers);
- beat 08: the eight live subtype-0x20 truck puffs through 001EBF10, beats
  05 and 12: the live subtype-5 puffs through 001EC3F0, from their captured
  work blocks; the D_00255434 handler words for subtypes 5/0xA/0x20/0x24 are
  read from the captured RAM;
- every beat: 001F54E0 over the captured pickup indicators (001C5680 and
  001C5760 nodes) with their captured colour;
- the opening capture: 001F68B0 then 001F6E40 (001D7BB0's two calls) over
  its point-light lists;
- every beat: 001CFB50 (with 001D0540, 001CD370 and 0011DF78 executed)
  over the captured camera matrix 0x70003AC0 and D_00275670; beats 05, 08
  and 12: the captured transform block D_0081F8F0 equals the native
  001CFB50 of the frame's last puff draw (the source words, +0x40, +0x48,
  +0x4C, +0x50 and the 001D0540 depth scale +0x54; +0x44 is the
  accumulator before the draw's step, checked to be the EE pre-image).

It also measures the live header src/game/em_effect_color.h (em_effect_delta,
the port's existing 001F54E0 stand-in) against the original and reports the
agreement count (informational; the header is not this lane's file).

Default run ~10 s; EM_TEST_FULL=1 runs the exhaustive sweeps (all 65536
selector keys, larger random sweeps). No original instruction bytes,
disassembly or data are written by this file; build/effect_kinds_reference/
report.json holds only counts.
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

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ee_float_model as FM  # noqa: E402
from reference_mode import banner, part, pick, select, in_scope_beat  # noqa: E402
import test_effect_original_reference as base  # noqa: E402
from test_effect_original_reference import Decals, Work  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
ELF_SHA = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
ROUTE = DECOMP / 'build/s87/route'
OPENING = DECOMP / 'build/startup-reference/opening_ee.bin'
OUT = ROOT / 'build/effect_kinds_reference'
M32 = (1 << 32) - 1
ONE = 0x3F800000
STACK = base.STACK
LISTS, LISTS_END = 0x25AD80, 0x25D800
TEMPLATES = 0x26EB70
RING, RING_INDEX = 0x76B5C0, 0x81F950
PARTICLES = 0x7709C0
SPAD36A0 = 0x700036A0
XF = 0x81F8F0
SCRATCH = 0x01E00000
CALLBACK = 0x01FFF000          # oracle-only address for the 001F54E0 indirect call

FUNCS = {  # address: size in bytes (FUNCTIONS.csv / the split listing)
    0x1EC1F0: 0x7C, 0x1EC3F0: 0x7C, 0x1EC470: 0x180, 0x1EBF10: 0x2D4, 0x1F54E0: 0x15C,
    0x1F5640: 0x2F8, 0x1F5940: 0x2DC, 0x1F5C20: 0x80, 0x1F5CA0: 0x2BC, 0x1F0310: 0x4C,
    0x1F03D0: 0x90, 0x1F3FA0: 0x64, 0x1F6640: 0xA4, 0x1F66F0: 0x64, 0x1F6760: 0xE8,
    0x1F6850: 0x60, 0x1F68B0: 0x208, 0x1F6D60: 0xD4, 0x1F6E40: 0x3C, 0x1029C0: 0x28,
    0x1CFB50: 0x8C, 0x1D0540: 0x11C}
SPAD3660, SPAD3AC0 = 0x70003660, 0x70003AC0

W_LIGHT, W_UNLIGHT, W_EMIT, W_SIN, W_FTOI, W_RANGE, W_RAND, W_XF, W_DRAW = (
    0x1D7FA0, 0x1D80B0, 0x1F4D40, 0x11DF78, 0x1281C0, 0x21B9A0, 0x122BB8, 0x1CFB50, 0x1CFBE0)


def sx(v, bits=32):
    v &= (1 << bits) - 1
    return v - (1 << bits) if v >> (bits - 1) else v


def fbits(x): return struct.unpack('<I', struct.pack('<f', x))[0]


# --------------------------------------------------------------- oracle ---

class KEE(base.EE):
    """The shared bounded interpreter plus the R5900 three-operand MULT
    (rd = LO), and a record of every executed instruction address."""

    def __init__(self, elf, ram, spad):
        super().__init__(elf, ram, spad)
        self.seen = set()

    def call(self, entry, args=(), floats=()):
        self.steps = 0          # the runaway bound applies per call
        return super().call(entry, args, floats)

    def fetch(self, pc):
        self.seen.add(pc)
        return super().fetch(pc)

    def execute(self, w, pc):
        if w >> 26 == 0 and w & 63 == 0x18:
            rs, rt, rd = w >> 21 & 31, w >> 16 & 31, w >> 11 & 31
            product = sx(self.r[rs], 32) * sx(self.r[rt], 32)
            self.lo, self.hi = sx(product & M32), sx(product >> 32 & M32)
            self.set32(rd, self.lo)
            return
        super().execute(w, pc)


def elf_ram(elf):
    ram = bytearray(0x2000000)
    ram[0x100000:0x100000 + 0x175B00] = elf[0x300:0x300 + 0x175B00]
    return ram


# ------------------------------------------------------------ native side ---

u8, i32, u32 = C.c_uint8, C.c_int32, C.c_uint32
FP = C.POINTER(C.c_uint32)
IP = C.POINTER(C.c_int32)


class Tables(C.Structure):
    _fields_ = [('lists', u8 * (LISTS_END - LISTS)), ('templates', (u32 * 4) * 4)]


class Globals(C.Structure):
    _fields_ = [('d810700', u8), ('d810701', u8), ('d81075D', u8), ('d81075E', u8), ('d810761', u8),
                ('d810778', u8), ('d81077B', u8), ('d810784', u8), ('d810785', u8), ('d81079E', u8),
                ('spad3B68', i32), ('spad36A0', u32 * 16), ('d275C40', i32), ('d275C44', i32)]


Particles = u8 * (0x80 * 0x90)

LIGHT = C.CFUNCTYPE(C.c_int, C.c_void_p, FP, u32, FP, i32, u32, u32, IP)
UNLIGHT = C.CFUNCTYPE(C.c_int, C.c_void_p, i32)
EMIT = C.CFUNCTYPE(C.c_int, C.c_void_p, FP, IP, u32, u32)
SIN = C.CFUNCTYPE(C.c_int, C.c_void_p, u32, FP)
FTOI = C.CFUNCTYPE(C.c_int, C.c_void_p, u32, IP)
RANGE = C.CFUNCTYPE(C.c_int, C.c_void_p, i32, u32, u32)
RAND = C.CFUNCTYPE(C.c_int, C.c_void_p, IP)
INDIRECT = C.CFUNCTYPE(C.c_int, C.c_void_p, u32, C.c_void_p)
XFW = C.CFUNCTYPE(C.c_int, C.c_void_p, u32, u32, FP, u32, u32, u32, u32, u32)
DRAW = C.CFUNCTYPE(C.c_int, C.c_void_p, i32, i32, u32, u32, i32)


class Workers(C.Structure):
    _fields_ = [('ctx', C.c_void_p), ('light', LIGHT), ('unlight', UNLIGHT), ('emit', EMIT),
                ('sin', SIN), ('ftoi', FTOI), ('range', RANGE), ('rand', RAND),
                ('indirect', INDIRECT), ('xf', XFW), ('draw', DRAW)]


class Fault(C.Structure):
    _fields_ = [('address', u32), ('code', i32)]


class XfState(C.Structure):
    _fields_ = [('d275670', u32), ('spad3AC0', u32 * 16), ('spad3660', u32 * 4), ('spad3670', u32 * 4)]


Xf = u32 * (0x58 // 4)


class Kinds(C.Structure):
    _fields_ = [('tables', C.POINTER(Tables)), ('globals', C.POINTER(Globals)),
                ('decals', C.POINTER(Decals)), ('particles', C.POINTER(Particles)),
                ('workers', C.POINTER(Workers)), ('fault', Fault)]


SHIM = r'''
#include "game/em_effect_color.h"
void shim_effect_delta(uint32_t random_value, const float color[4], float delta[3])
{
    em_effect_delta(random_value, color, delta);
}
'''


def build_lib():
    OUT.mkdir(parents=True, exist_ok=True)
    shim = OUT / 'color_header_shim.c'
    shim.write_text(SHIM)
    lib_path = OUT / 'effect_kinds.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-shared', '-fPIC', '-Isrc', 'src/game/em_effect_kinds.c', str(shim),
                    '-o', str(lib_path)], cwd=ROOT, check=True)
    lib = C.CDLL(str(lib_path))
    P = C.POINTER
    K = P(Kinds)
    lib.em_effect_kinds_load_tables.argtypes = [C.c_char_p, C.c_size_t, P(Tables)]
    for name in ('001EC1F0', '001EC3F0', '001EC470', '001EBF10'):
        getattr(lib, 'em_effect_kinds_' + name).argtypes = [K, FP, i32, P(Work)]
    lib.em_effect_kinds_handler.argtypes = [K, u32, FP, i32, P(Work)]
    lib.em_effect_kinds_001F54E0.argtypes = [K, C.c_void_p, FP, u32, FP]
    for name in ('001F5640', '001F5CA0', '001F6760', '001F6D60'):
        fn = getattr(lib, 'em_effect_kinds_' + name)
        fn.argtypes = [u8, u8]
        fn.restype = u32
    lib.em_effect_kinds_001F5940.argtypes = [K, u32, FP, i32]
    lib.em_effect_kinds_001F03D0.argtypes = [K, i32]
    lib.em_effect_kinds_001F6640.argtypes = [K, u32]
    lib.em_effect_kinds_001F66F0.argtypes = [K, u32]
    for name in ('001F5C20', '001F0310', '001F3FA0', '001F6850', '001F68B0', '001F6E40'):
        getattr(lib, 'em_effect_kinds_' + name).argtypes = [K]
    lib.shim_effect_delta.argtypes = [u32, FP, FP]
    lib.em_effect_kinds_001CFB50.argtypes = [K, P(XfState), P(Xf), i32, FP, u32, u32, u32, u32, u32]
    lib.em_effect_kinds_001D0540.argtypes = [K, P(XfState), FP, FP, u32, FP]
    return lib


# --------------------------------------------------------------- the world ---

class World:
    """One original memory image (journaled) and its native twin.

    `script` maps a worker to the list of results it hands out, in order,
    to BOTH sides; each side keeps its own cursor."""

    def __init__(self, lib, elf, ee, script=None):
        self.lib, self.elf, self.ee = lib, elf, ee
        ee.journal.clear()
        self.script = {k: list(v) for k, v in (script or {}).items()}
        self.cursor = {'o': {}, 'n': {}}
        self.o_log, self.n_log = [], []
        self.tables = Tables()
        blob = C.create_string_buffer(bytes(elf), len(elf))
        assert lib.em_effect_kinds_load_tables(blob, len(elf), C.byref(self.tables)) == 0
        self.glob = Globals()
        self.decals = Decals()
        self.particles = Particles()
        self.node_matrix = None          # (native array, original vram) of the handler a0
        self.obj = None                  # (native token, original vram) of the 001F54E0 obj
        self.workers = Workers(None, LIGHT(self.n_light), UNLIGHT(self.n_unlight), EMIT(self.n_emit),
                               SIN(self.n_sin), FTOI(self.n_ftoi), RANGE(self.n_range),
                               RAND(self.n_rand), INDIRECT(self.n_indirect), XFW(self.n_xf),
                               DRAW(self.n_draw))
        self.kinds = Kinds(C.pointer(self.tables), C.pointer(self.glob), C.pointer(self.decals),
                           C.pointer(self.particles), C.pointer(self.workers), Fault())
        ee.stubs = {W_LIGHT: self.o_light, W_UNLIGHT: self.o_unlight, W_EMIT: self.o_emit,
                    W_SIN: self.o_sin, W_FTOI: self.o_ftoi, W_RANGE: self.o_range, W_RAND: self.o_rand,
                    CALLBACK: self.o_indirect, W_XF: self.o_xf, W_DRAW: self.o_draw}
        self.allowed = set(range(LISTS, LISTS_END))
        self.allowed.update(range(RING, RING + 7 * 0xC00))
        self.allowed.update(range(RING_INDEX, RING_INDEX + 28))
        self.allowed.update(range(PARTICLES, PARTICLES + 0x80 * 0x90))
        self.allowed.update(range(0x275C40, 0x275C48))
        self.allowed.update(range(SPAD36A0, SPAD36A0 + 0x40))

    def take(self, side, name):
        c = self.cursor[side].get(name, 0)
        self.cursor[side][name] = c + 1
        values = self.script.get(name, [])
        assert c < len(values), ('script ran out', side, name)
        return values[c]

    # ---- RAM <-> native
    def pull(self):
        """Native state := the original's RAM."""
        e, g = self.ee, self.glob
        C.memmove(self.tables.lists, bytes(e.ram[LISTS:LISTS_END]), LISTS_END - LISTS)
        for name, a in (('d810700', 0x810700), ('d810701', 0x810701), ('d81075D', 0x81075D),
                        ('d81075E', 0x81075E), ('d810761', 0x810761), ('d810778', 0x810778),
                        ('d81077B', 0x81077B), ('d810784', 0x810784), ('d810785', 0x810785),
                        ('d81079E', 0x81079E)):
            setattr(g, name, e.load(a, 1))
        g.spad3B68 = sx(e.load(0x70003B68))
        for i in range(16):
            g.spad36A0[i] = e.load(SPAD36A0 + 4 * i)
        g.d275C40, g.d275C44 = sx(e.load(0x275C40)), sx(e.load(0x275C44))
        for n in range(7):
            self.decals.index[n] = sx(e.load(RING_INDEX + 4 * n))
        C.memmove(C.addressof(self.decals.slot), bytes(e.ram[RING:RING + 7 * 0xC00]), 7 * 0xC00)
        C.memmove(self.particles, bytes(e.ram[PARTICLES:PARTICLES + 0x4800]), 0x4800)

    def compare(self, label, extra=(), mark=0):
        """Every byte the original changed (journal entries from `mark` on) is
        modelled; every modelled byte is equal."""
        e, g = self.ee, self.glob
        for buf, off, _old in e.journal[mark:]:
            a = off + (0x70000000 if buf is e.spad else 0)
            for i in range(len(_old)):
                if STACK - 0x2000 <= a + i < STACK:
                    continue
                assert a + i in self.allowed or any(lo <= a + i < hi for lo, hi in extra), \
                    (label, 'unmodelled write', hex(a + i))
        assert bytes(self.tables.lists) == bytes(e.ram[LISTS:LISTS_END]), (label, 'lists')
        assert bytes(C.string_at(C.addressof(self.decals.slot), 7 * 0xC00)) == bytes(e.ram[RING:RING + 7 * 0xC00]), \
            (label, 'ring slots')
        assert [sx(e.load(RING_INDEX + 4 * n)) for n in range(7)] == list(self.decals.index), (label, 'ring index')
        assert bytes(self.particles) == bytes(e.ram[PARTICLES:PARTICLES + 0x4800]), (label, 'particles')
        assert (g.d275C40, g.d275C44) == (sx(e.load(0x275C40)), sx(e.load(0x275C44))), (label, '275C40/44')
        assert list(g.spad36A0) == [e.load(SPAD36A0 + 4 * i) for i in range(16)], (label, 'spad 36A0')
        assert self.o_log == self.n_log, (label, 'calls', self.o_log[:6], self.n_log[:6])
        assert self.cursor['o'] == self.cursor['n'], (label, 'script use')

    # ---- original workers
    def o_light(self, ee):
        a0, a1 = ee.u32(4), ee.u32(5)
        self.o_log.append(('1D7FA0', mem_hex(ee, a0, 4), a1, mem_hex(ee, a1, 4),
                           sx(ee.r[6]), ee.f[12], ee.f[13]))
        ee.set32(2, self.take('o', 'light'))

    def o_unlight(self, ee):
        self.o_log.append(('1D80B0', sx(ee.r[4])))

    def o_emit(self, ee):
        a0, a1 = ee.u32(4), ee.u32(5)
        pos = mem_hex(ee, a0, 4)
        col = tuple(sx(ee.load(a1 + 4 * i)) for i in range(4))
        self.o_log.append(('1F4D40', pos, col, ee.f[12], ee.f[13]))

    def o_sin(self, ee):
        self.o_log.append(('11DF78', ee.f[12]))
        ee.f[0] = self.take('o', 'sin')

    def o_ftoi(self, ee):
        self.o_log.append(('1281C0', ee.f[12]))
        ee.set32(2, self.take('o', 'ftoi'))

    def o_range(self, ee):
        self.o_log.append(('21B9A0', sx(ee.r[4]), ee.f[12], ee.f[13]))

    def o_rand(self, ee):
        self.o_log.append(('122BB8',))
        ee.set32(2, self.take('o', 'rand'))

    def o_indirect(self, ee):
        a0 = ee.u32(4)
        self.o_log.append(('indirect', CALLBACK, 'obj' if self.obj and a0 == self.obj[1] else a0))

    def o_xf(self, ee):
        a2 = ee.u32(6)
        src = mem_hex(ee, a2, 16)
        who = 'node' if self.node_matrix and a2 == self.node_matrix[1] else a2
        self.o_log.append(('1CFB50', ee.u32(4), ee.u32(5), who, src) + tuple(ee.f[12:17]))

    def o_draw(self, ee):
        self.o_log.append(('1CFBE0', sx(ee.r[4]), sx(ee.r[5]), ee.u32(6), ee.u32(7), sx(ee.r[8])))

    # ---- native workers
    def n_light(self, _ctx, pos, tvram, color, typ, f12, f13, handle):
        self.n_log.append(('1D7FA0', b''.join(pos[i].to_bytes(4, 'little') for i in range(4)).hex(),
                           tvram, b''.join(color[i].to_bytes(4, 'little') for i in range(4)).hex(),
                           typ, f12, f13))
        handle[0] = self.take('n', 'light')
        return 0

    def n_unlight(self, _ctx, handle):
        self.n_log.append(('1D80B0', handle))
        return 0

    def n_emit(self, _ctx, pos, col, f12, f13):
        self.n_log.append(('1F4D40', b''.join(pos[i].to_bytes(4, 'little') for i in range(4)).hex(),
                           tuple(col[i] for i in range(4)), f12, f13))
        return 0

    def n_sin(self, _ctx, f12, out):
        self.n_log.append(('11DF78', f12))
        out[0] = self.take('n', 'sin')
        return 0

    def n_ftoi(self, _ctx, f12, out):
        self.n_log.append(('1281C0', f12))
        out[0] = self.take('n', 'ftoi')
        return 0

    def n_range(self, _ctx, a0, f12, f13):
        self.n_log.append(('21B9A0', a0, f12, f13))
        return 0

    def n_rand(self, _ctx, out):
        self.n_log.append(('122BB8',))
        out[0] = self.take('n', 'rand')
        return 0

    def n_indirect(self, _ctx, fn, obj):
        self.n_log.append(('indirect', fn, 'obj' if self.obj and obj == self.obj[0] else obj))
        return 0

    def n_xf(self, _ctx, dst, a1, src, f12, f13, f14, f15, f16):
        addr = C.cast(src, C.c_void_p).value
        if self.node_matrix and addr == C.addressof(self.node_matrix[0]):
            who = 'node'
        else:
            who = 'other'
        data = b''.join(src[i].to_bytes(4, 'little') for i in range(16)).hex()
        if who == 'other':
            who = SPAD36A0 if data == b''.join(self.glob.spad36A0[i].to_bytes(4, 'little')
                                               for i in range(16)).hex() else addr
        self.n_log.append(('1CFB50', dst, a1, who, data, f12, f13, f14, f15, f16))
        return 0

    def n_draw(self, _ctx, a0, a1, a2, a3, t0):
        self.n_log.append(('1CFBE0', a0, a1, a2, a3, t0))
        return 0


def mem_hex(ee, a, words):
    return b''.join(ee.load(a + 4 * i).to_bytes(4, 'little') for i in range(words)).hex()


def run_both(w, label, original, native, extra=()):
    """original(ee) -> value, native() -> value; then the full comparison
    over the bytes the original wrote during this call."""
    mark = len(w.ee.journal)
    o = original(w.ee)
    n = native()
    assert w.kinds.fault.code == 0, (label, 'native fault', hex(w.kinds.fault.address), w.kinds.fault.code)
    assert n == 0, (label, 'native result', n)
    w.compare(label, extra, mark)
    return o


# ----------------------------------------------------------------- scenarios ---

KEYS = {
    0x1F5640: [0x0, 0x1, 0x700, 0x703, 0xB00, 0xD00, 0xE00, 0xF00, 0xF01, 0x1100, 0x600, 0x601, 0x1300,
               0x1400, 0x1500, 0x802],
    0x1F5CA0: [0x301, 0x302, 0x400, 0x401, 0x700, 0x800, 0x803, 0xD00, 0xF00, 0x1500],
    0x1F6760: [0x0, 0x1, 0x2, 0x100, 0x200, 0xE00, 0x1100, 0x1301],
    0x1F6D60: [0x100, 0x700, 0x702, 0xB00, 0x1000, 0x1200, 0x1300],
}


def selector_cases(lib, elf, ee, counts):
    """All four selectors on every key in full mode; in quick mode every
    case key, its neighbours, area 0..0x17 x sub 0..7 and a random sample."""
    keys = set()
    for ks in KEYS.values():
        for k in ks:
            keys.update(((k - 1) & 0xFFFF, k, (k + 1) & 0xFFFF))
    keys.update((a << 8) | s for a in range(0x18) for s in range(8))
    keys.update({0xFFFF, 0x8000, 0x7FFF, 0xFF00, 0x00FF})
    rng = random.Random(0x1F5640)
    keys.update(rng.randrange(0x10000) for _ in range(300))
    keys = sorted(keys)
    if pick(True, False):
        keys = list(range(0x10000))
    n = 0
    for key in keys:
        ee.store(0x810700, key >> 8, 1)
        ee.store(0x810701, key & 0xFF, 1)
        for fn in KEYS:
            ee.call(fn)
            o = ee.u32(2)
            nat = getattr(lib, 'em_effect_kinds_%08X' % fn)(key >> 8, key & 0xFF)
            assert o == nat, ('selector', hex(fn), hex(key), hex(o), hex(nat))
            n += 1
        ee.restore()
    counts['selector_calls'] = n
    return len(keys)


def marker_cases(lib, elf, ee, counts):
    """001F5940 on every kind (and out-of-range kinds), with random t,
    clock and scripted 0011DF78 / float_to_int results."""
    rng = random.Random(0x1F5940)
    kinds = list(range(12)) + [0xFFFFFFFF, 0x80000000, 0xFFFF, 0x7FFFFFFF, 0xFFFFFFFE]
    cases = []
    for kind in kinds:
        for _ in range(pick(150, 4)):
            cases.append((kind, rng.choice([0, 1, -1, 0x7FFFFFFF, -0x80000000, rng.getrandbits(32),
                                            rng.randrange(40)]),
                          rng.choice([0, -1, rng.getrandbits(32), rng.randrange(20000)])))
    n = 0
    for kind, t, clock in cases:
        pos = [fbits(rng.uniform(-900, 900)) for _ in range(3)] + [ONE]
        script = {'sin': [rng.choice([fbits(rng.uniform(-1, 1)), rng.getrandbits(32)])],
                  'ftoi': [sx(rng.getrandbits(32))]}
        w = World(lib, elf, ee, script)
        ee.write(SCRATCH, struct.pack('<4I', *pos))
        ee.store(0x70003B68, clock & M32)
        w.pull()
        pa = (C.c_uint32 * 4)(*pos)
        run_both(w, ('1F5940', kind, t), lambda e: e.call(0x1F5940, (kind & M32, SCRATCH, t & M32)),
                 lambda: lib.em_effect_kinds_001F5940(C.byref(w.kinds), kind, pa, sx(t)))
        ee.restore()
        n += 1
    counts['marker_cases'] = n


def walker_cases(lib, elf, ee, counts):
    """001F5C20 for every key that selects a marker list (ELF image)."""
    rng = random.Random(0x1F5C20)
    n = records = 0
    for key in KEYS[0x1F5640] + [0x0002, 0x0B01]:
        for _ in range(pick(4, 1)):
            clock = rng.getrandbits(32)
            script = {'sin': [rng.getrandbits(32) for _ in range(64)],
                      'ftoi': [sx(rng.getrandbits(32)) for _ in range(64)]}
            w = World(lib, elf, ee, script)
            ee.store(0x810700, key >> 8, 1)
            ee.store(0x810701, key & 0xFF, 1)
            ee.store(0x70003B68, clock)
            w.pull()
            run_both(w, ('1F5C20', hex(key)), lambda e: e.call(0x1F5C20),
                     lambda: lib.em_effect_kinds_001F5C20(C.byref(w.kinds)))
            records += sum(1 for c in w.o_log if c[0] == '1F4D40')
            ee.restore()
            n += 1
    counts['walker_cases'] = n
    counts['walker_markers'] = records


def reset_cases(lib, elf, ee, counts):
    """001F0310, 001F03D0(0..6) and 001F3FA0 over randomised pools."""
    rng = random.Random(0x1F0310)
    n = 0
    for fn, args in [(0x1F0310, ())] * pick(6, 2) + [(0x1F03D0, (lane,)) for lane in range(7)] + \
            [(0x1F3FA0, ())] * pick(4, 1):
        w = World(lib, elf, ee)
        ee.write(RING, rng.randbytes(7 * 0xC00))
        ee.write(RING_INDEX, rng.randbytes(28))
        ee.write(PARTICLES, rng.randbytes(0x4800))
        ee.write(0x275C40, rng.randbytes(8))
        w.pull()
        if fn == 0x1F0310:
            native = lambda: lib.em_effect_kinds_001F0310(C.byref(w.kinds))
        elif fn == 0x1F03D0:
            native = lambda: lib.em_effect_kinds_001F03D0(C.byref(w.kinds), args[0])
        else:
            native = lambda: lib.em_effect_kinds_001F3FA0(C.byref(w.kinds))
        run_both(w, ('reset', hex(fn), args), lambda e: e.call(fn, args), native)
        ee.restore()
        n += 1
    counts['reset_cases'] = n


def light_list_vrams():
    return [0x25CF10, 0x25CF90, 0x25CFE0, 0x25D030, 0x25D080, 0x25D1F0, 0x25D270, 0x25D2C0, 0x25D340,
            0x25D3C0, 0x25D500, 0x25D550, 0x25D5A0, 0x25D5F0, 0x25D6C0, 0x25D760]


def randomise_handles(ee, rng):
    for base_ in light_list_vrams():
        at = base_
        while sx(ee.load(at, 2), 16) >= 0:
            ee.store(at + 0x24, rng.choice([M32, M32, 0, rng.randrange(64), rng.getrandbits(32)]))
            at += 0x28


LATCHES = (0x81075D, 0x81075E, 0x810761, 0x810778, 0x81077B, 0x810784, 0x810785, 0x81079E)


def light_cases(lib, elf, ee, counts):
    """The room point-light chain: 001F68B0, 001F6E40, 001F6850 over every
    case key plus others, latch bytes 0xFF / not, randomised handles and
    scripted 001D7FA0 results (-1 included); 001F6640 / 001F66F0 on every
    list directly."""
    rng = random.Random(0x1F68B0)
    keys = sorted(set(KEYS[0x1F6760] + KEYS[0x1F6D60] + [0x0B00, 0x0B01, 0x0300, 0x1301, 0x1302, 0x0F00]))
    cases = []
    for key in keys:
        for fn in (0x1F68B0, 0x1F6E40, 0x1F6850):
            for variant in range(pick(24, 3)):
                cases.append((fn, key, variant))
    cases = select(cases, pick(len(cases), 150), 0x1F68B0, axes=(lambda c: (c[0], c[1]),),
                   keep=lambda _, c: c[2] < 2)
    for lst in light_list_vrams():
        for fn in (0x1F6640, 0x1F66F0):
            for variant in range(pick(8, 1)):
                cases.append((fn, lst, variant))
    cases.append((0x1F6640, 0, 0))
    cases.append((0x1F66F0, 0, 0))
    n = calls = 0
    for fn, arg, variant in cases:
        script = {'light': [rng.choice([-1, rng.randrange(0x40), sx(rng.getrandbits(32))]) for _ in range(40)]}
        w = World(lib, elf, ee, script)
        randomise_handles(ee, rng)
        for a in LATCHES:   # variant 0: every latch 0xFF, variant 1: none, else random
            ee.store(a, 0xFF if variant == 0 else 0 if variant == 1 else
                     rng.choice([0xFF, 0xFF, 0, rng.getrandbits(8)]), 1)
        if fn in (0x1F68B0, 0x1F6E40, 0x1F6850):
            ee.store(0x810700, arg >> 8, 1)
            ee.store(0x810701, arg & 0xFF, 1)
        w.pull()
        K = C.byref(w.kinds)
        native = {0x1F68B0: lambda: lib.em_effect_kinds_001F68B0(K),
                  0x1F6E40: lambda: lib.em_effect_kinds_001F6E40(K),
                  0x1F6850: lambda: lib.em_effect_kinds_001F6850(K),
                  0x1F6640: lambda: lib.em_effect_kinds_001F6640(K, arg),
                  0x1F66F0: lambda: lib.em_effect_kinds_001F66F0(K, arg)}[fn]
        args = (arg,) if fn in (0x1F6640, 0x1F66F0) else ()
        run_both(w, ('light', hex(fn), hex(arg), variant), lambda e: e.call(fn, args), native)
        calls += len(w.o_log)
        ee.restore()
        n += 1
    counts['light_cases'] = n
    counts['light_worker_calls'] = calls


def handler_case(lib, elf, ee, fn, matrix_vram, work_vram, depth, label, w=None):
    """One handler call: the original with D_00275C34 = work_vram and
    a0 = matrix_vram, against the native on the same bytes."""
    w = w or World(lib, elf, ee)
    ee.store(0x275C34, work_vram)
    w.pull()
    mat = (C.c_uint32 * 16)(*[ee.load(matrix_vram + 4 * i) for i in range(16)])
    w.node_matrix = (mat, matrix_vram)
    work = Work()
    work.seed = sx(ee.load(work_vram))
    work.seed_copy = sx(ee.load(work_vram + 4))
    work.step, work.limit = ee.load(work_vram + 8), ee.load(work_vram + 0xC)
    work.accumulator, work.fraction = ee.load(work_vram + 0x54), ee.load(work_vram + 0x5C)
    extra = [(work_vram + 4, work_vram + 8)]
    run_both(w, label, lambda e: e.call(fn, (matrix_vram, depth & M32)),
             lambda: lib.em_effect_kinds_handler(C.byref(w.kinds), fn, C.cast(mat, FP), sx(depth),
                                                 C.byref(work)), extra)
    assert sx(ee.load(work_vram + 4)) == work.seed_copy, (label, 'work +4')
    assert list(mat) == [ee.load(matrix_vram + 4 * i) for i in range(16)], (label, 'matrix unchanged')
    return w


def handler_cases(lib, elf, ee, counts):
    rng = random.Random(0x1EBF10)
    n = 0
    M, Wk = SCRATCH + 0x100, SCRATCH + 0x200
    for fn in (0x1EC1F0, 0x1EC3F0, 0x1EC470, 0x1EBF10):
        for _ in range(pick(600, 40)):
            vals = [fbits(rng.uniform(-600, 600)) for _ in range(16)]
            if rng.random() < 0.2:
                vals = [rng.getrandbits(32) for _ in range(16)]
            ee.write(M, struct.pack('<16I', *vals))
            work = [rng.getrandbits(32) for _ in range(0x60 // 4)]
            work[1] = rng.choice([rng.getrandbits(32), 0, M32, 0x7FFFFFFF, 0x80000000, 0xFFFF0000, 0x0000FFFF])
            work[0x54 // 4] = rng.choice([fbits(rng.uniform(0, 2)), rng.getrandbits(32)])
            work[0x5C // 4] = rng.choice([fbits(rng.random()), rng.getrandbits(32)])
            ee.write(Wk, struct.pack('<%dI' % len(work), *work))
            handler_case(lib, elf, ee, fn, M, Wk, sx(rng.getrandbits(32)), ('handler', hex(fn), n))
            ee.restore()
            n += 1
    counts['handler_cases'] = n


def color_cases(lib, elf, ee, counts, header):
    """001F54E0 on random colours (aliased and not) and scripted rand."""
    rng = random.Random(0x1F54E0)
    special = [0, FM.SIGN, ONE, 0xBF800000, fbits(127.0), fbits(-127.0), fbits(1.0 / 254), 0x00000001,
               0x7F7FFFFF, 0xFF7FFFFF, 0x7F800000, 0x3F7FFFFF, fbits(128.0), fbits(0.5)]
    n = 0
    OBJ = SCRATCH + 0x400
    for i in range(pick(4000, 250)):
        if i % 5 == 0:
            color = [rng.choice(special) for _ in range(4)]
        elif i % 5 == 1:
            color = [rng.getrandbits(32) for _ in range(4)]
        elif i % 5 == 2:
            # clamp edges: channel * brightness within a few ulps of 0 or 254,
            # so channel - 127 lands on either side of -127 / +127
            one_ulp = [fbits(1.0) + d for d in range(-3, 4)]
            color = [rng.choice([fbits(rng.uniform(-2e-5, 2e-5)), rng.choice(one_ulp)]) for _ in range(3)]
            color.append(rng.choice([ONE, 0xBF800000] + one_ulp))
        else:
            color = [fbits(rng.uniform(-3, 3)) for _ in range(3)] + [fbits(rng.uniform(-2, 2))]
        rand = rng.choice([0, 0x7FFFFFFF, 0x7FFFFFFE, 0x40000000, 1, rng.randrange(0x80000000),
                           rng.randrange(0x80000000)])
        alias = i % 3 == 0
        color_case(lib, elf, ee, OBJ, color, rand, alias, ('color', i), header)
        n += 1
    counts['color_cases'] = n


def color_case(lib, elf, ee, obj, color, rand, alias, label, header):
    w = World(lib, elf, ee, {'rand': [rand]})
    ee.store(obj + 0x4C, CALLBACK)
    src = obj + 0x80 if alias else SCRATCH + 0x600
    ee.write(src, struct.pack('<4I', *color))
    w.pull()
    token = C.c_void_p(0x5EED)
    w.obj = (token.value, obj)
    out = (C.c_uint32 * 4)(*[ee.load(obj + 0x80 + 4 * i) for i in range(4)])
    col = out if alias else (C.c_uint32 * 4)(*color)
    run_both(w, label, lambda e: e.call(0x1F54E0, (obj, src)),
             lambda: lib.em_effect_kinds_001F54E0(C.byref(w.kinds), token, C.cast(out, FP), CALLBACK,
                                                   C.cast(col, FP)), [(obj + 0x80, obj + 0x90)])
    got = [ee.load(obj + 0x80 + 4 * i) for i in range(4)]
    assert list(out) == got, (label, 'obj +0x80', [hex(x) for x in out], [hex(x) for x in got])
    # the live header's delta (informational)
    hc = (C.c_uint32 * 4)(*color)
    hd = (C.c_uint32 * 3)()
    lib.shim_effect_delta(rand, C.cast(hc, FP), C.cast(hd, FP))
    header['cases'] += 1
    kind = 'captured' if isinstance(label[0], str) and label[0][:2].isdigit() else 'swept'
    header[kind + '_cases'] = header.get(kind + '_cases', 0) + 1
    if list(hd) == got[:3]:
        header['equal'] += 1
        header[kind + '_equal'] = header.get(kind + '_equal', 0) + 1
    elif len(header['examples']) < 5:
        header['examples'].append({'rand': rand, 'color': [hex(c) for c in color],
                                   'original': [hex(x) for x in got[:3]], 'header': [hex(x) for x in hd]})
    ee.restore()


# ------------------------------------------- 001CFB50 / 001D0540 (the xf block) ---

class XfWorld(World):
    """001CFB50 runs with its callees 001D0540, 001CD370 and 0011DF78
    executed on the original side; the native 0011DF78 worker is the leaf's
    whole effect (clear the sign bit), and its argument reaches +0x54."""

    def __init__(self, lib, elf, ee):
        super().__init__(lib, elf, ee)
        del ee.stubs[W_SIN]
        del ee.stubs[W_XF]

    def n_sin(self, _ctx, f12, out):
        out[0] = f12 & 0x7FFFFFFF
        return 0


def xf_state(ee):
    st = XfState()
    st.d275670 = ee.load(0x275670)
    for i in range(16):
        st.spad3AC0[i] = ee.load(SPAD3AC0 + 4 * i)
    for i in range(4):
        st.spad3660[i] = ee.load(SPAD3660 + 4 * i)
        st.spad3670[i] = ee.load(SPAD3660 + 0x10 + 4 * i)
    return st


def xf_native(lib, w, st, a1, src_words, floats, dst_words=None):
    dst = Xf(*(dst_words or [0] * (0x58 // 4)))
    src = (C.c_uint32 * 16)(*src_words)
    rc = lib.em_effect_kinds_001CFB50(C.byref(w.kinds), C.byref(st), C.byref(dst), a1,
                                      C.cast(src, FP), *floats)
    return rc, list(dst)


def xf_case(lib, elf, ee, dst_vram, a1, src_words, floats, label):
    """001CFB50(dst, a1, src, f12..f16) on both sides over the image's
    D_00275670 and 0x70003AC0; dst, 0x70003660..0x7000367F and the
    returned state must be equal, and nothing else may change."""
    w = XfWorld(lib, elf, ee)
    w.pull()
    SRC = SCRATCH + 0x800
    ee.write(SRC, struct.pack('<16I', *src_words))
    before = [ee.load(dst_vram + 4 * i) for i in range(0x58 // 4)]
    st = xf_state(ee)
    mark = len(ee.journal)
    ee.call(0x1CFB50, (dst_vram, a1 & M32, SRC), floats)
    rc, got = xf_native(lib, w, st, a1, src_words, floats, before)
    assert rc == 0 and w.kinds.fault.code == 0, (label, rc, hex(w.kinds.fault.address), w.kinds.fault.code)
    want = [ee.load(dst_vram + 4 * i) for i in range(0x58 // 4)]
    assert got == want, (label, 'xf block', [hex(x) for x in got], [hex(x) for x in want])
    assert list(st.spad3660) + list(st.spad3670) == [ee.load(SPAD3660 + 4 * i) for i in range(8)], \
        (label, 'spad 3660..367F')
    w.compare(label, [(dst_vram, dst_vram + 0x58), (SPAD3660, SPAD3660 + 0x20)], mark)
    ee.restore()


def xf_cases(lib, elf, counts):
    """Every in-scope beat (quick: three) supplies the camera matrix and
    context address; sources are the beat's live effect-node matrices and
    random or special matrices; a1, f12..f15 and the depth offset f16 are
    swept (f16 over the handlers' 0, 5 and 15 and specials). Then the
    capture check of the transform block."""
    beats = sorted(p.name for p in ROUTE.iterdir() if (p / 'eeMemory.bin').exists() and in_scope_beat(p.name))
    rng = random.Random(0x1CFB50)
    special = [0, FM.SIGN, ONE, 0xBF800000, 0x00000001, 0x80000001, 0x7F7FFFFF, 0xFF7FFFFF,
               0x7F800000, 0xFF800000, 0x7FC00000, fbits(1e-6), fbits(5.0), fbits(15.0)]
    n = synthetic_views = captured = 0
    seen = set()
    wanted = ('05_boxes', '08_truck_crossing', '12_crevice_jump')
    for beat in select(beats, 3, 0x1CFB50, keep=lambda _i, name: name in wanted):
        ram, spad = load_ram(ROUTE / beat / 'eeMemory.bin', ROUTE / beat / 'scratchpad.bin')
        ee = KEE(elf, ram, spad)
        camera = bytes(spad[0x3AC0:0x3B00])      # the captured 0x70003AC0
        sources = [[ee.load(node + 0xD0 + 4 * i) for i in range(16)]
                   for cb in (0x1EA240, 0x1E2560, 0x8235F0) for node in pool_nodes(ram, cb)]
        for i in range(pick(160, 24)):
            if i < len(sources):
                src = sources[i]
            elif i % 4 == 0:
                src = [rng.choice(special) for _ in range(16)]
            else:
                src = [fbits(rng.uniform(-700, 700)) for _ in range(16)]
            f16 = rng.choice([0, fbits(5.0), fbits(15.0), rng.choice(special), fbits(rng.uniform(-50, 50))])
            floats = [rng.getrandbits(32) for _ in range(4)] + [f16]
            a1 = rng.choice([0, 1, 2, 3, -1, 0x7FFFFFFF, sx(rng.getrandbits(32))])
            dst = rng.choice([XF, SCRATCH + 0xA00])
            if i % 8 == 7:          # a synthetic camera: w = 0, specials, huge rows
                synthetic_views += 1
                cam = [rng.choice(special + [fbits(rng.uniform(-2, 2))]) for _ in range(16)]
                ee.write(SPAD3AC0, struct.pack('<16I', *cam))
                ee.journal.clear()
            xf_case(lib, elf, ee, dst, a1, src, floats, (beat, 'xf', i))
            if i % 8 == 7:
                ee.write(SPAD3AC0, camera)
                ee.journal.clear()
            n += 1
        captured += xf_capture(lib, elf, ee, ram, beat)
        seen.update(ee.seen)
        del ee, ram, spad
    counts['xf_cases'] = n
    counts['xf_synthetic_cameras'] = synthetic_views
    counts['xf_captured_blocks'] = captured
    return seen


def ee_add_preimages(total, step):
    """Every a with EE a + step == total, searched around total - step."""
    guess = FM.ee_sub(total, step)
    out = []
    for d in range(-64, 65):
        a = (guess + d) & M32
        if FM.ee_add(a, step) == total:
            out.append(a)
    return out


def xf_capture(lib, elf, ee, ram, beat):
    """The captured D_0081F8F0 is the frame's last 001CFB50 call. On the
    route that is the last live puff node's last draw (no other writer runs
    after the effect walk). Reproduce it natively from the node's captured
    matrix, work block and the captured camera; the accumulator word +0x44
    is the value before the draw's step, so it must be an EE pre-image of
    the captured accumulator."""
    nodes = pool_nodes(ram, 0x1EA240)
    if not nodes:
        return 0
    node = nodes[-1]
    sub = ram[node + 0xD]
    work = node + 0x1F0
    acc, frac, step = ee.load(work + 0x54), ee.load(work + 0x5C), ee.load(work + 8)
    seed = ee.load(work + 4)
    matrix = [ee.load(node + 0xD0 + 4 * i) for i in range(16)]
    if sub == 5:                                          # 001EC3F0: one draw
        src, f13, f16 = matrix, frac, fbits(5.0)
    elif sub == 0x20:                                     # 001EBF10: the third draw
        inv = pow(0x25, -1, 1 << 32)
        before = ((seed - 0xB) * inv) & M32               # work +4 before that draw
        f13 = FM.ee_add(FM.ee_div(FM.ee_cvt_s_w((before >> 16) & 0xFFFF), 0x477FFF00), 0x38D1B717)
        src = [ONE, 0, 0, 0, 0, ONE, 0, 0, 0, 0, ONE, 0, fbits(395.0), matrix[13], fbits(390.0), ONE]
        f16 = 0
    else:
        raise AssertionError((beat, 'captured last puff of an unexpected subtype', hex(sub)))
    w = XfWorld(lib, elf, ee)
    st = xf_state(ee)
    captured = [ee.load(XF + 4 * i) for i in range(0x58 // 4)]
    rc, got = xf_native(lib, w, st, 0, src, [captured[0x44 // 4], f13, ONE, fbits(1e-6), f16])
    assert rc == 0, (beat, 'native xf fault', hex(w.kinds.fault.address))
    assert got == captured, (beat, hex(node), 'captured xf block',
                             [(hex(4 * i), hex(a), hex(b)) for i, (a, b) in enumerate(zip(got, captured)) if a != b])
    assert captured[0x44 // 4] in ee_add_preimages(acc, step), (beat, 'accumulator before the step')
    return 1


# ------------------------------------------------------------ capture checks ---

def load_ram(path_ram, path_spad=None):
    ram = bytearray(Path(path_ram).read_bytes())
    spad = bytearray(Path(path_spad).read_bytes()) if path_spad else bytearray(0x4000)
    return ram, spad


def pool_nodes(ram, callback):
    out, node, n = [], struct.unpack_from('<I', ram, 0x275BC0)[0], 0
    while node and n < 0x100:
        if struct.unpack_from('<I', ram, node + 0x10)[0] == callback:
            out.append(node)
        node = struct.unpack_from('<I', ram, node + 0x1C)[0]
        n += 1
    return out


def route_cases(lib, elf, counts, header):
    beats = sorted(p.name for p in ROUTE.iterdir() if (p / 'eeMemory.bin').exists() and in_scope_beat(p.name))
    assert len(beats) == 15, beats
    rng = random.Random(0xB00)
    markers = puffs = colors = 0
    for beat in beats:
        ram, spad = load_ram(ROUTE / beat / 'eeMemory.bin', ROUTE / beat / 'scratchpad.bin')
        ee = KEE(elf, ram, spad)
        assert (ram[0x810700], ram[0x810701]) == (0x0B, 0x00), beat
        # 1. the glow markers over the captured RAM and clock
        w = World(lib, elf, ee, {'sin': [], 'ftoi': []})
        w.pull()
        assert bytes(w.tables.lists[0x25B590 - LISTS:0x25B770 - LISTS]) == bytes(ram[0x25B590:0x25B770]), \
            (beat, 'captured AREA11 marker list differs from the ELF')
        run_both(w, (beat, '1F5C20'), lambda e: e.call(0x1F5C20),
                 lambda: lib.em_effect_kinds_001F5C20(C.byref(w.kinds)))
        got = sum(1 for c in w.o_log if c[0] == '1F4D40')
        assert got == 11, (beat, got)
        markers += got
        ee.restore()
        # 2. the live puffs through their handlers, from the captured work blocks
        table = {s: struct.unpack_from('<I', ram, 0x255434 + 8 * s)[0] for s in (5, 0xA, 0x20, 0x24)}
        assert table == {5: 0x1EC3F0, 0xA: 0x1EC1F0, 0x20: 0x1EBF10, 0x24: 0x1EC470}, (beat, table)
        for node in pool_nodes(ram, 0x1EA240):
            sub = ram[node + 0xD]
            fn = table.get(sub)
            assert fn, (beat, 'live puff with an untranslated subtype', hex(sub))
            handler_case(lib, elf, ee, fn, node + 0xD0, node + 0x1F0, sx(rng.getrandbits(24)),
                         (beat, 'puff', hex(node)))
            ee.restore()
            puffs += 1
        # 3. the pickup indicators' colour, from their captured colour
        for cb in (0x1C5680, 0x1C5760):
            for node in pool_nodes(ram, cb):
                color = [struct.unpack_from('<I', ram, node + 0x80 + 4 * i)[0] for i in range(4)]
                color_case(lib, elf, ee, node, color, rng.randrange(0x80000000), True,
                           (beat, 'pickup', hex(node)), header)
                colors += 1
        del ee, ram, spad
    counts['route_beats'] = len(beats)
    counts['route_markers'] = markers
    counts['route_puffs'] = puffs
    counts['route_pickup_colors'] = colors


def opening_light_case(lib, elf, counts):
    """001D7BB0's calls 001F68B0 then 001F6E40 over the captured opening."""
    ram, spad = load_ram(OPENING)
    ee = KEE(elf, ram, spad)
    assert (ram[0x810700], ram[0x810701]) == (0x0B, 0x00)
    w = World(lib, elf, ee, {'light': [0]})
    w.pull()
    o_calls = []
    run_both(w, ('opening', '1F68B0'), lambda e: e.call(0x1F68B0),
             lambda: lib.em_effect_kinds_001F68B0(C.byref(w.kinds)))
    o_calls += w.o_log
    w.o_log.clear(), w.n_log.clear()
    run_both(w, ('opening', '1F6E40'), lambda e: e.call(0x1F6E40),
             lambda: lib.em_effect_kinds_001F6E40(C.byref(w.kinds)))
    o_calls += w.o_log
    assert [c[0] for c in o_calls] == ['1D80B0', '1D7FA0'], o_calls
    assert o_calls[1][2] == 0x26EB90, 'auxiliary light preset 2'
    counts['opening_light_calls'] = len(o_calls)


def fault_cases(lib, elf, counts):
    """Fail-stop paths: untranslated handler, NULL workers, lane and
    template out of range, latched faults."""
    tables = Tables()
    blob = C.create_string_buffer(bytes(elf), len(elf))
    assert lib.em_effect_kinds_load_tables(blob, len(elf), C.byref(tables)) == 0
    assert lib.em_effect_kinds_load_tables(blob, len(elf) - 1, C.byref(tables)) == -1
    g, d, p = Globals(), Decals(), Particles()
    none = Workers()
    mat = (C.c_uint32 * 16)()
    work = Work()
    k = Kinds(C.pointer(tables), C.pointer(g), C.pointer(d), C.pointer(p), C.pointer(none), Fault())
    assert lib.em_effect_kinds_handler(C.byref(k), 0x1EAD70, C.cast(mat, FP), 0, C.byref(work)) == -1
    assert (k.fault.address, k.fault.code) == (0x1EAD70, 6)
    assert lib.em_effect_kinds_001F3FA0(C.byref(k)) == -1, 'latched'
    k.fault = Fault()
    assert lib.em_effect_kinds_001EC3F0(C.byref(k), C.cast(mat, FP), 0, C.byref(work)) == -1
    assert (k.fault.address, k.fault.code) == (0x1CFB50, 1)
    k.fault = Fault()
    assert lib.em_effect_kinds_001F03D0(C.byref(k), 7) == -1 and k.fault.code == 4
    k.fault = Fault()
    g.d810700, g.d810701 = 0x0B, 0
    assert lib.em_effect_kinds_001F5C20(C.byref(k)) == -1 and k.fault.address == 0x1F4D40
    k.fault = Fault()
    struct.pack_into('<h', tables.lists, 0x25D5A0 - LISTS + 4, 4)       # preset outside 0..3
    struct.pack_into('<i', tables.lists, 0x25D5A0 - LISTS + 0x24, -1)
    assert lib.em_effect_kinds_001F6640(C.byref(k), 0x25D5A0) == -1 and k.fault.code == 4
    k.fault = Fault()
    assert lib.em_effect_kinds_001F6640(C.byref(k), LISTS_END - 0x10) == -1 and k.fault.code == 4
    k.fault = Fault()
    assert lib.em_effect_kinds_001F66F0(C.byref(k), 0) == 0 and k.fault.code == 0
    counts['fault_cases'] = 8


# ------------------------------------------------------------------ coverage ---

def word(elf, pc):
    return struct.unpack_from('<I', elf, pc - 0x100000 + 0x300)[0]


def branch_target(w, pc):
    op, rt = w >> 26, w >> 16 & 31
    if op in (4, 5, 6, 7, 0x14, 0x15, 0x16, 0x17) or (op == 1 and rt in (0, 1, 2, 3)) or \
            (op == 0x11 and (w >> 21 & 31) == 8):
        return pc + 4 + (sx(w & 0xFFFF, 16) << 2)
    return None


def statically_dead(elf, fn, size, pc):
    """pc follows the delay slot of an unconditional transfer (b = beq
    zero, zero; j; jr) and no branch or jump of the function targets it; or
    it is a trailing zero alignment word after the last return."""
    if pc == fn + size - 4 and word(elf, pc) == 0:
        return True
    if pc - 8 < fn:
        return False
    w = word(elf, pc - 8)
    op = w >> 26
    uncond = (op == 4 and (w >> 21 & 31) == 0 and (w >> 16 & 31) == 0) or op == 2 or \
        (op == 0 and w & 63 == 8)
    if not uncond:
        return False
    for q in range(fn, fn + size, 4):
        wq = word(elf, q)
        if branch_target(wq, q) == pc or (wq >> 26 in (2, 3) and ((q & 0xF0000000) | (wq & 0x3FFFFFF) << 2) == pc):
            return False
    return True


# ---------------------------------------------------------------------- main ---

def main():
    t0 = time.time()
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA, 'not the pinned boot ELF'
    lib = build_lib()
    ee = KEE(elf, elf_ram(elf), bytearray(0x4000))
    counts, header = {}, {'cases': 0, 'equal': 0, 'examples': []}
    nkeys = selector_cases(lib, elf, ee, counts)
    marker_cases(lib, elf, ee, counts)
    walker_cases(lib, elf, ee, counts)
    reset_cases(lib, elf, ee, counts)
    light_cases(lib, elf, ee, counts)
    handler_cases(lib, elf, ee, counts)
    color_cases(lib, elf, ee, counts, header)
    seen = set(ee.seen)
    route_cases(lib, elf, counts, header)
    seen.update(xf_cases(lib, elf, counts))
    opening_light_case(lib, elf, counts)
    fault_cases(lib, elf, counts)
    # every instruction of every translated function executed at least once,
    # except words proven statically unreachable
    # 001F68B0's default arm releases obj only when obj != 0, but every key
    # for which 001F6760 returns a list is one of 001F68B0's case keys (all
    # 65536 keys checked here on the verified selector), so the release
    # call (the jal and its delay slot) cannot run.
    case_keys = {0x0, 0x1, 0x2, 0x100, 0x200, 0xE00, 0x1100, 0x1301}
    assert all(key in case_keys for key in range(0x10000)
               if lib.em_effect_kinds_001F6760(key >> 8, key & 0xFF)), '001F68B0 default arm reachable'
    seen.update((0x1F6AA0, 0x1F6AA4))
    counts['dynamically_unreachable_words'] = 2
    missing, dead = {}, 0
    for fn, size in FUNCS.items():
        gap = [pc for pc in range(fn, fn + size, 4) if pc not in seen]
        unreachable = [pc for pc in gap if statically_dead(elf, fn, size, pc)]
        dead += len(unreachable)
        gap = [pc for pc in gap if pc not in unreachable]
        if gap:
            missing[hex(fn)] = [hex(pc) for pc in gap[:6]]
    assert not missing, ('instructions never executed', missing)
    counts['functions_fully_executed'] = len(FUNCS)
    counts['statically_unreachable_words'] = dead
    counts['header_em_effect_delta_equal'] = f"{header['equal']}/{header['cases']}"
    counts['header_equal_on_captured_pickups'] = f"{header.get('captured_equal', 0)}/{header.get('captured_cases', 0)}"
    report = dict(status='PASS', elf_sha256=ELF_SHA, cases=counts,
                  header_mismatch_examples=header['examples'], seconds=round(time.time() - t0, 1))
    (OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    banner(part(nkeys, 0x10000, 'selector keys'),
           part(counts['marker_cases'], 17 * 150, '001F5940 cases'),
           part(counts['handler_cases'], 4 * 600, 'handler cases'),
           part(counts['color_cases'], 4000, '001F54E0 cases'))
    print('header em_effect_color.h em_effect_delta vs original: %s equal' % counts['header_em_effect_delta_equal'])
    print('Original effect kinds: PASS', json.dumps(counts))


if __name__ == '__main__':
    main()
