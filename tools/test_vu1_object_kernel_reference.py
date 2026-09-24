#!/usr/bin/env python3
"""The VU1 object kernel (DMA CALL 0x0023C750) against the ORIGINAL microcode.

Checks src/game/em_vu1_object_kernel.h, the CPU translation of the 62
instruction program the kernel packet uploads (ELF 0x0023C780), kick for
kick against a VU1 interpreter that executes those original instructions.
Docs: docs/VU1_OBJECT_KERNEL.md. Reads the owner's pinned ELF and the
captured EE RAM under ../Extermination/build/; embeds no original bytes; the
report holds addresses, counts and hashes only.

The oracle is the shadow test's VU1 interpreter (in-order issue, VF operand
stalls, CLIP flags 4 cycles and Q 7 cycles after their producer, XGKICK
snapshots) with the VU0 lane rules of tools/ee_float_model.py for the
arithmetic (docs/EE_FLOAT_MODEL.md; VU1 is assumed to follow them). Every
compared batch: the translation and the interpreter start from the same
data memory and the same carried registers (randomised at every MSCAL, so
anything the program reads from a register it did not write shows up);
the XGKICK address, the kicked GIF packet bytes and all 16 KiB of data
memory afterwards must be equal.

A. The kernel packet: one DMA CNT whose VIF codes are exactly FLUSHA,
   STCYCL 4,4, STMASK 0, STMOD 0, BASE 0x1B0, OFFSET 0x10E and one
   62-instruction MPG to micro 0, then RET; the same bytes in every capture.
B. Captured display lists. Both lists of every AREA11 capture
   (startup-reference and route beats 00..14) are replayed as the DMAC and
   VIF1 would (CALL/RET, UNPACK V4-32 with STCYCL and the TOPS double
   buffer, MPG, MSCAL/MSCNT). Every batch the object kernel runs is
   compared; units are attributed to their owner (model REF = owner +0x44
   + 0x40). The clip program 0x002354A0 and every other program are not
   executed (their batches are counted). Every value the kernel reads must
   come from an upload the replay saw (no unknown input). Census (asserted,
   report 'census'): the fog, guard and ambient rows of every unit's MSCAL
   (the doc's section 2) and the 0x0023C480 face-program CALLs (builder
   001D3E40 and its call chain, each CALL's face resource). On every
   compared vertex the CLIP and the clip-flag test are exactly
   CLIP_FLAG_LATENCY cycles apart: the ADC window i-2..i rests on that
   interpreter latency.
C. Executed owner units. The ORIGINAL 001CAA00 runs over every drawn world
   owner of route beats 00..14 (tools/test_owner_draw_reference.py lane D:
   the 119 drawn units, including the three pickups that are not in the
   captured lists), its DMA unit is replayed on a fresh VU1 with nothing
   else in data memory, and every object-kernel batch is compared. This
   also proves each unit is self-contained (no read outside its uploads).
D. Synthetic batches: random units and TOPs (including wrap-around and
   outputs overlapping the rows), fog-off rows, ADC data bits, zero and
   negative w, denormals, +-0, overflow to MAX, FTOI saturation, MSCNT with
   changed constants in data memory, random fog-row x/y lanes (the cap and
   the ADC addend), exponent-255 words in dead lanes; an
   exponent-255 word in a live lane must fault the translation. Both
   outcomes of the program's conditional branches are reached.
E. Consumers: the port's em_lighting_vertex (src/game/em_lighting.c) and
   em_shadow_gs_object_batch (src/gfx/metal/em_shadow_gs.h) against the
   kicked RGBAQ and XYZF2 words of every compared captured batch (counted,
   reported).
F. TEX0/ST path: every kicked TEX0 qword equals the vertex's qword 0; ST x/y
   equal s * Q and t * Q with the Q the kernel put in ST z; ST w is the
   carried register. GS decode: on every compared packet (all three
   templates) and on built tags (accepted ones and ones breaking one rule
   each), em_vu1_object_kernel_decode must equal gs_decode, an independent
   decode of the PACKED layout, field by field, and the strip triangles
   must equal the enumeration of i >= 2 with ADC clear.
S. PCSX2 save state 14 (the only one with the kernel in VU1 micro memory):
   the header over its vu1Memory image must give PCSX2's output lanes.
--defects: injects defects into a copy of the header and requires the
   quick checks to catch each one (not part of the default run).
"""
from pathlib import Path
import collections
import ctypes as C
import hashlib
import json
import os
import random
import struct
import subprocess
import sys
import time

import ee_float_model as fm
import test_level_material_reference as lm
import test_shadow_original_reference as sh
from reference_mode import FULL, banner, part, pick, select, parallel_map, in_scope_beat

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
REF = DECOMP / 'build/startup-reference'
ROUTE = DECOMP / 'build/s87/route'
OUT = ROOT / 'build/vu1_object_kernel_reference'
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
KERNEL, PROGRAM, INSTRUCTIONS = 0x23C750, 0x23C780, 62
CLIP_KERNEL = 0x2354A0
FACE_KERNEL = 0x23C480                  # the face morph program (docs/VU1_OBJECT_KERNEL.md section 2)
FACE_BUILDER = (0x1D3E40, 0x1D3F50)     # 001D3E40, the function that appends its CALL
# the face resources (the decomp repo's docs/OPENING_ACTORS.md): runtime address -> name
FACES = {0x018C8740: 'Roger', 0x011749C0: 'Dennis'}
# the face draw chain: (caller range, callee) for each call on the way to 001D3E40
FACE_CHAIN = [((0x1CAA00, 0x1CB2C0), 0x1CB3C0), ((0x1CB3C0, 0x1CB500), 0x1D3F50), ((0x1D3F50, 0x1D3F60), 0x1D3E40)]
FOG_OFF_ROW = 0x2514B0                  # 001D37D0's REF 2 (docs/OWNER_DRAW.md)
MICRO_ADC, MICRO_EXIT, MICRO_RESUME = 0x031 * 8, 0x036 * 8, 0x03C * 8
MICRO_FLAG_TEST = 0x02B * 8             # the clip-flag test (CLIP issues at micro 0x027)
CLIP_FLAG_LATENCY = 4                   # the interpreter's model: visible 4 cycles after CLIP
PLAYER = 0x8102B0
BEHAVIOUR = {0x1551B0: 'crate', 0x156620: 'drum', 0x827630: 'fan', 0x823FF0: 'truck',
             0x827B10: 'elevator', 0x825940: 'husk creature', 0x827490: 'husk partner',
             0x823E80: 'parachute', 0x1BC350: 'door', 0x159210: 'panel', 0x1C4820: '001C4820',
             0x15AFA0: 'pickup', 0x18A6B0: 'player equipment', 0x8237E0: 'Roger',
             0x1C5680: 'pickup light', 0x219550: 'item', 0x1C5760: '001C5760'}
M32 = 0xFFFFFFFF
SIGN = 0x80000000
HOUSE_SHARE = 0.25                      # quick mode: units also run on the unmodified interpreter


def u32(b, a): return struct.unpack_from('<I', b, a)[0]
def e32(elf, a): return u32(elf, a - 0x100000 + 0x300)
def fail(msg): raise AssertionError(msg)
def seed_of(*parts): return int(hashlib.sha256(repr(parts).encode()).hexdigest()[:8], 16)


# ------------------------------------------------------------ A. packet ---
# The kernel packet's VIF codes in order, as (command, NUM, value): FLUSHA,
# STCYCL 4,4, STMASK 0, STMOD 0, BASE 0x1B0, OFFSET 0x10E, MPG of 62 to
# micro 0 (value = the micro address). No code carries the interrupt bit.
PACKET_CODES = [(0x13, 0, 0), (0x01, 0, 0x404), (0x20, 0, 0), (0x05, 0, 0), (0x03, 0, 0x1B0),
                (0x02, 0, 0x10E), (0x4A, INSTRUCTIONS, 0)]


def kernel_packet(elf):
    """The CALLed packet: its VIF code sequence (exactly PACKET_CODES), the
    uploaded program and the packet byte range."""
    w0 = e32(elf, KERNEL)
    if (w0 >> 28) & 7 != 1: fail('kernel packet: not a CNT tag')
    if e32(elf, KERNEL + 8) or e32(elf, KERNEL + 12): fail('kernel packet: codes in the tag qword')
    qwc = w0 & 0xFFFF
    i, end, seq, mpg = KERNEL + 16, KERNEL + 16 + 16 * qwc, [], []
    while i < end:
        v = e32(elf, i)
        if v >> 31: fail('kernel packet: a VIF code with the interrupt bit')
        cmd, num, imm = (v >> 24) & 0x7F, (v >> 16) & 0xFF, v & 0xFFFF
        if cmd == 0x4A:
            seq.append((cmd, num, imm))
            mpg.append((imm, num or 256, i + 4)); i += 4 + 8 * (num or 256); continue
        if cmd == 0x20: seq.append((cmd, num, e32(elf, i + 4))); i += 8; continue
        seq.append((cmd, num, imm)); i += 4
    if i != end: fail('kernel packet: the codes overrun the CNT')
    if (e32(elf, end) >> 28) & 7 != 6: fail('kernel packet: no RET after the CNT')
    if seq != PACKET_CODES or mpg != [(0, INSTRUCTIONS, PROGRAM)]:
        fail(f'kernel packet codes {[(hex(c), n, hex(v)) for c, n, v in seq]} {mpg}')
    code = elf[PROGRAM - 0x100000 + 0x300:PROGRAM - 0x100000 + 0x300 + 8 * INSTRUCTIONS]
    return dict(code=code, range=(KERNEL, end + 16), base=0x1B0, offset=0x10E)


# ------------------------------------------------------------ oracle ------
FIELDS = sh.FIELDS
def mul(a, b): return fm._vu_mul_raw(a, b)
def add(a, b): return fm._vu_add_raw(a, b)


class Oracle(sh.VU1):
    """sh.VU1 (the shadow test's VU1 interpreter: issue, stalls, flag and Q
    timing, VIF-loaded micro memory, XGKICK snapshots) with the VU0 lane
    rules for every arithmetic op the object kernel uses; any other upper
    op raises. ACC and Q hold binary32 words."""

    def __init__(self, elf):
        super().__init__(elf)
        self.accw = [0] * 4
        self.q = 0
        self.clip_at = None
        self.flag_gap = collections.Counter()   # cycles from the CLIP to the flag test, per vertex

    def upper(self, pc, up):
        code, fs, ft, fd, mask = up & 0x7FF, up >> 11 & 31, up >> 16 & 31, up >> 6 & 31, up >> 21 & 15
        op, x, y, acc, m = up & 63, self.v[fs], self.v[ft], self.accw, FIELDS(mask)
        if code == 0x2FF: return []
        if code == 0x1FF:                                    # CLIP fs.xyz against |ft.w|
            self.clip_at = self.cycle
            xs, w = list(map(sh.vnum, x)), abs(sh.vnum(y[3])); f = 0
            for c in range(3):
                if xs[c] > w: f |= 1 << (2 * c)
                if xs[c] < -w: f |= 2 << (2 * c)
            self.later(4, 'cf', f); return []
        if (up & 0x3C) == 0x3C:
            if code == 0x17D: return [(ft, c, fm.vu_ftoi(x[c], 4)) for c in m if ft]
            if 0x1BC <= code <= 0x1BF:
                for c in m: acc[c] = mul(x[c], y[code & 3])
                return []
            if 0x0BC <= code <= 0x0BF:
                for c in m: acc[c] = add(acc[c], mul(x[c], y[code & 3]))
                return []
            if code == 0x2BE:
                for c in m: acc[c] = mul(x[c], y[c])
                return []
            fail(('oracle upper special', hex(pc), hex(up)))
        if op < 4: res = {c: add(x[c], y[op]) for c in m}
        elif 8 <= op < 12: res = {c: add(acc[c], mul(x[c], y[op & 3])) for c in m}
        elif 16 <= op < 20: res = {c: fm.vu_max(x[c], y[op & 3]) for c in m}
        elif 20 <= op < 24: res = {c: fm.vu_min(x[c], y[op & 3]) for c in m}
        elif op == 0x1C: res = {c: mul(x[c], self.q) for c in m}
        elif op == 0x22: res = {c: add(x[c], sh.bits(self.i)) for c in m}
        else: fail(('oracle upper', hex(pc), hex(up)))
        return [(fd, c, w) for c, w in res.items()] if fd else []

    def lower(self, pc, lo):
        if pc == MICRO_FLAG_TEST and lo >> 25 == 0x12:      # the clip-flag test of the ADC decision
            self.flag_gap[self.cycle - self.clip_at] += 1
        if lo >> 25 == 0x40 and lo & 0x7FF == 0x3BC:        # DIV Q = fs.fsf / ft.ftf
            fsf, ftf = lo >> 21 & 3, lo >> 23 & 3
            self.later(7, 'q', fm.vu_div(self.v[lo >> 11 & 31][fsf], self.v[lo >> 16 & 31][ftf], fsf, ftf))
            self.q_ready = self.cycle + 7
            return None
        return super().lower(pc, lo)

    def randomise(self, rng):
        """Unknown register state at an MSCAL: every VF/VI/ACC word random
        (exponent 255 excluded: a stored register can come back as a row
        in the synthetic overlap cases)."""
        self.v = [[finite(rng) for _ in range(4)] for _ in range(32)]
        self.v[0] = [0, 0, 0, sh.bits(1.0)]
        self.vi = [0] + [rng.getrandbits(16) for _ in range(15)]
        self.accw = [rng.getrandbits(32) for _ in range(4)]
        self.cf = rng.getrandbits(24)
        self.pending, self.cycle = [], 0
        self.ready = [[0] * 4 for _ in range(32)]
        self.q_ready = 0


# ------------------------------------------------------------ native ------
class Q(C.Structure):
    _fields_ = [('w', C.c_uint32 * 4)]


class State(C.Structure):
    _fields_ = [('loaded', C.c_uint32), ('colour', C.c_uint32 * 16), ('fog', C.c_uint32 * 4),
                ('guard_scale', C.c_uint32 * 4), ('guard_offset', C.c_uint32 * 4), ('cap', C.c_uint32),
                ('tex0', Q), ('st', Q), ('rgbaq', Q)]


class Batch(C.Structure):
    _fields_ = [('fault', C.c_uint32), ('fault_vertex', C.c_uint32), ('top', C.c_uint32), ('kick', C.c_uint32),
                ('why', C.c_uint32 * 32), ('clip', C.c_uint32 * 32), ('ftoi_saturated', C.c_uint32),
                ('saturated', C.c_uint32)]


class GsVertex(C.Structure):
    _fields_ = [('tex0', C.c_uint64), ('s', C.c_uint32), ('t', C.c_uint32), ('q', C.c_uint32),
                ('r', C.c_uint8), ('g', C.c_uint8), ('b', C.c_uint8), ('a', C.c_uint8),
                ('x', C.c_uint16), ('y', C.c_uint16), ('z', C.c_uint32), ('f', C.c_uint8),
                ('adc', C.c_uint8)]


DMEM = Q * 1024
SHIM = r'''
#include "game/em_vu1_object_kernel.h"
#include "game/em_lighting.h"
#include "gfx/metal/em_shadow_gs.h"

int run(int cont, EmVu1ObjState *s, EmVu1ObjQword *m, uint32_t top, EmVu1ObjBatch *b)
{ return cont ? em_vu1_object_kernel_mscnt(s, m, top, b) : em_vu1_object_kernel_mscal(s, m, top, b); }
int decode(const EmVu1ObjQword *m, uint32_t kick, EmVu1ObjGsVertex *v, uint32_t *prim, uint32_t *regs)
{ return em_vu1_object_kernel_decode(m, kick, v, prim, regs); }
uint32_t triangles(const EmVu1ObjGsVertex *v, int n, uint32_t prim, uint8_t *last)
{ return em_vu1_object_kernel_triangles(v, n, prim, last); }
uint32_t top_of(uint32_t block) { return em_vu1_object_kernel_top(block); }
unsigned sizes(void) { return (unsigned)(sizeof(EmVu1ObjState) | sizeof(EmVu1ObjBatch) << 12 | sizeof(EmVu1ObjGsVertex) << 24); }

/* The port's CPU consumers over one batch (dmem before it; no overlap):
 * out[0] = RGBAQ qwords em_lighting_vertex gets wrong; em_shadow_gs_object_batch:
 * out[1] = XYZF2 lanes wrong on drawing vertices (ADC 0), out[2] = ADC
 * decisions that differ, out[3] = XYZF2 lanes wrong on ADC vertices,
 * out[4] = of out[1], the fog (w) lanes. */
void consumers(const EmVu1ObjQword *m, uint32_t top, const EmVu1ObjState *s,
               const EmVu1ObjQword *after, uint32_t kick, uint32_t out[5])
{
    float bones[32][16], qw3[32][4], k1021[4], k1022[4], k1023[4];
    const float *bone[32];
    memcpy(k1021, s->fog, 16); memcpy(k1022, s->guard_scale, 16); memcpy(k1023, s->guard_offset, 16);
    out[0] = out[1] = out[2] = out[3] = out[4] = 0;
    for (unsigned i = 0; i < 32; ++i) {
        const uint32_t word = m[(top + 4 * i + 3) & 1023].w[3] & 0xFFFF;
        memcpy(qw3[i], m[(top + 4 * i + 3) & 1023].w, 16);
        for (unsigned r = 0; r < 4; ++r) memcpy(&bones[i][4 * r], m[(word + r) & 1023].w, 16);
        bone[i] = bones[i];
        EmLightingMatrices mx;
        memset(&mx, 0, sizeof mx);
        for (unsigned r = 0; r < 3; ++r) memcpy(&mx.normal[4 * r], m[(word + 4 + r) & 1023].w, 16);
        memcpy(mx.color, s->colour, 64);
        float n[3];
        memcpy(n, m[(top + 4 * i + 2) & 1023].w, 12);
        uint32_t rgba[4];
        em_lighting_vertex(rgba, n, &mx);
        if (memcmp(rgba, after[(kick + 3 + 4 * i) & 1023].w, 16)) ++out[0];
    }
    EmShadowGsVertex g[32];
    em_shadow_gs_object_batch(bone, k1021, k1022, k1023, (const float (*)[4])qw3, 32, g);
    for (unsigned i = 0; i < 32; ++i) {
        const uint32_t *k = after[(kick + 4 + 4 * i) & 1023].w;
        const uint32_t adc = (k[3] >> 15) & 1u;
        for (unsigned l = 0; l < 4; ++l)
            if ((uint32_t)g[i].w[l] != k[l]) {
                ++out[adc ? 3 : 1];
                if (!adc && l == 3) ++out[4];
            }
        if (g[i].adc != adc) ++out[2];
    }
}
'''


def native_library(path_header=None, tag='native'):
    OUT.mkdir(parents=True, exist_ok=True)
    src, lib_path = OUT / f'{tag}.c', OUT / f'{tag}.dylib'
    inc = ['-I' + str(ROOT / 'src')]
    text = SHIM
    if path_header:
        text = SHIM.replace('#include "game/em_vu1_object_kernel.h"', f'#include "{path_header}"')
    src.write_text(text)
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared',
                    '-fPIC', *inc, str(src), str(ROOT / 'src/game/em_lighting.c'), '-o', str(lib_path)],
                   check=True)
    lib = C.CDLL(str(lib_path))
    lib.run.argtypes = [C.c_int, C.POINTER(State), C.POINTER(Q), C.c_uint32, C.POINTER(Batch)]
    lib.decode.argtypes = [C.POINTER(Q), C.c_uint32, C.POINTER(GsVertex), C.POINTER(C.c_uint32),
                           C.POINTER(C.c_uint32)]
    lib.decode.restype = C.c_int
    lib.triangles.argtypes = [C.POINTER(GsVertex), C.c_int, C.c_uint32, C.POINTER(C.c_uint8)]
    lib.triangles.restype = C.c_uint32
    lib.sizes.restype = C.c_uint
    lib.top_of.argtypes = [C.c_uint32]
    lib.top_of.restype = C.c_uint32
    lib.consumers.argtypes = [C.POINTER(Q), C.c_uint32, C.POINTER(State), C.POINTER(Q), C.c_uint32,
                              C.POINTER(C.c_uint32)]
    s = lib.sizes()
    if (s & 0xFFF, s >> 12 & 0xFFF, s >> 24) != (C.sizeof(State), C.sizeof(Batch), C.sizeof(GsVertex)):
        fail('ctypes layout differs from the header')
    return lib


# ------------------------------------------------------------ GS decode ---
# Independent decode of a kicked GIF packet, from the GS PACKED register
# layout: slot k of the tag's REGS may be one of these registers (else the
# header must refuse the tag). The field tuple order is FIELDS_GS.
GS_SLOTS = ({0x6: 'tex0', 0xF: None}, {0x2: 'st', 0xF: None}, {0x1: 'rgbaq', 0xF: None},
            {0x4: 'xyzf2', 0x5: 'xyz2'})
FIELDS_GS = ('tex0', 's', 't', 'q', 'r', 'g', 'b', 'a', 'x', 'y', 'z', 'f', 'adc')
GS_BUFFER = 64                           # decode output buffer (32 + a guard for defects)
SENTINEL = 0xA5


def gs_decode(mem, kick):
    """None for a tag the header must refuse; else (NLOOP, PRIM or None when
    PRE is clear, EM_VU1_OBJ_GS_* bits, [field tuples])."""
    def q(a): return struct.unpack_from('<4I', mem, 16 * (a & 1023))
    t = q(kick)
    lo, regs64 = t[0] | t[1] << 32, t[2] | t[3] << 32
    nloop, eop, pre, prim, flg, nreg = lo & 0x7FFF, lo >> 15 & 1, lo >> 46 & 1, lo >> 47 & 0x7FF, lo >> 58 & 3, lo >> 60
    slots = [regs64 >> 4 * k & 15 for k in range(4)]
    if not eop or flg != 0 or nreg != 4 or nloop > 32 or any(r not in GS_SLOTS[k] for k, r in enumerate(slots)):
        return None
    kind = [GS_SLOTS[k][r] for k, r in enumerate(slots)]
    regs = (1 if kind[0] else 0) | (2 if kind[1] else 0) | (4 if kind[2] else 0) | (8 if kind[3] == 'xyzf2' else 0)
    out = []
    for i in range(nloop):
        d0, d1, d2, d3 = (q(kick + 1 + 4 * i + k) for k in range(4))
        tex0 = d0[0] | d0[1] << 32 if kind[0] else 0             # TEX0: bits 0..63
        s_, t_, q_ = d1[:3] if kind[1] else (0, 0, 0)             # S 0..31, T 32..63, Q 64..95
        r, g, b, a = [w & 0xFF for w in d2] if kind[2] else (0, 0, 0, 0)   # 0..7, 32..39, 64..71, 96..103
        x, y = d3[0] & 0xFFFF, d3[1] & 0xFFFF                     # X 0..15, Y 32..47
        if kind[3] == 'xyzf2': z, f = d3[2] >> 4 & 0xFFFFFF, d3[3] >> 4 & 0xFF   # Z 68..91, F 100..107
        else: z, f = d3[2], 0                                     # XYZ2: Z 64..95
        out.append((tex0, s_, t_, q_, r, g, b, a, x, y, z, f, d3[3] >> 15 & 1))  # ADC: bit 111
    return nloop, (prim if pre else None), regs, out


def gs_fields(v): return tuple(getattr(v, f) for f in FIELDS_GS)


def decode_check(lib, mem, kick, where):
    """The header's decode against gs_decode on the same bytes: the return,
    PRIM, REGS and every field of every vertex; the entries past NLOOP (and
    all of them on a refusal) untouched. Returns (n, prim, regs, verts)."""
    want = gs_decode(mem, kick)
    verts = (GsVertex * GS_BUFFER)()
    C.memset(verts, SENTINEL, C.sizeof(verts))
    prim, regs = C.c_uint32(0xDEADBEEF), C.c_uint32(0xDEADBEEF)
    n = lib.decode(DMEM.from_buffer_copy(mem), kick, verts, C.byref(prim), C.byref(regs))
    raw = bytes(verts)
    if want is None:
        if n != -1: fail(f'{where}: decode accepted a tag the GS layout refuses (returned {n})')
        if raw != bytes([SENTINEL]) * len(raw): fail(f'{where}: a refused decode wrote vertices')
        return -1, None, None, None
    wn, wprim, wregs, wv = want
    if n != wn: fail(f'{where}: decode returned {n}, NLOOP is {wn}')
    if prim.value != (M32 if wprim is None else wprim) or regs.value != wregs:
        fail(f'{where}: decode PRIM {prim.value:#x} REGS {regs.value} want {wprim} {wregs}')
    for i in range(n):
        got = gs_fields(verts[i])
        if got != wv[i]:
            bad = [f for f, a, b in zip(FIELDS_GS, got, wv[i]) if a != b]
            fail(f'{where}: decode vertex {i} fields {bad} differ from the PACKED layout')
    if raw[n * C.sizeof(GsVertex):] != bytes([SENTINEL]) * (len(raw) - n * C.sizeof(GsVertex)):
        fail(f'{where}: decode wrote past NLOOP')
    return n, prim.value, regs.value, verts


def triangle_check(lib, verts, n, prim, where):
    """The header's strip triangles against the enumeration: every i >= 2
    whose ADC is clear when PRIM is a triangle strip, else ~0."""
    last = (C.c_uint8 * 32)()
    got = lib.triangles(verts, n, prim, last)
    if prim & 7 != 4:
        if got != M32: fail(f'{where}: triangles for PRIM {prim:#x} returned {got}')
        return None
    want = [i for i in range(2, n) if not verts[i].adc]
    if got != len(want) or list(last[:got]) != want:
        fail(f'{where}: triangles {list(last[:got]) if got <= 32 else got} want {want}')
    return want


def decode_cases(lib, count, seed=0x6D5):
    """Tags built at random: accepted ones (every slot choice, PRE on and off,
    any PRIM and NLOOP 0..32, reserved tag bits and slots 4..15 random, kicks
    that wrap dmem) and ones that break one rule each (NLOOP > 32, FLG != 0,
    NREG != 4, a register outside its slot's choices, EOP clear)."""
    rng = random.Random(seed)
    tally = collections.Counter()
    choices = [sorted(g) for g in GS_SLOTS]
    for k in range(count):
        mem = bytearray(struct.pack('<4096I', *[rng.getrandbits(32) for _ in range(4096)]))
        kick = rng.choice([rng.randrange(1024), rng.randrange(1000, 1024)])
        nloop = rng.choice([rng.randrange(33), 32, 0, 1, 2])
        slots = [rng.choice(c) for c in choices]
        lo = nloop | 1 << 15 | rng.getrandbits(30) << 16 | rng.getrandbits(1) << 46 | rng.getrandbits(11) << 47 | 4 << 60
        regs64 = rng.getrandbits(48) << 16 | sum(r << 4 * i for i, r in enumerate(slots))
        broken = k % 2 and rng.choice(['nloop', 'eop', 'flg', 'nreg', 'slot'])
        if broken == 'nloop': lo = lo & ~0x7FFF | rng.randrange(33, GS_BUFFER)
        if broken == 'eop': lo &= ~(1 << 15)
        if broken == 'flg': lo |= rng.randrange(1, 4) << 58
        if broken == 'nreg': lo = lo & ~(15 << 60) | rng.choice([x for x in range(16) if x != 4]) << 60
        if broken == 'slot':
            i = rng.randrange(4)
            regs64 = regs64 & ~(15 << 4 * i) | rng.choice([x for x in range(16) if x not in GS_SLOTS[i]]) << 4 * i
        struct.pack_into('<4I', mem, 16 * kick, lo & M32, lo >> 32, regs64 & M32, regs64 >> 32)
        where = f'decode case {k} ({broken or "accepted"})'
        n, prim, regs, verts = decode_check(lib, bytes(mem), kick, where)
        if (n < 0) != bool(broken): fail(f'{where}: refusal {n < 0}')
        tally['refused_' + broken if broken else 'accepted'] += 1
        if n >= 0:
            tally[f'regs_{regs}'] += 1
            for p in (prim, rng.getrandbits(11) & ~7 | 4, rng.getrandbits(11) & ~7 | rng.choice([0, 1, 2, 3, 5, 6, 7])):
                tris = triangle_check(lib, verts, n, p, where)
                tally['triangle_lists' if tris is not None else 'triangles_refused'] += 1
    return dict(tally)


# ------------------------------------------------------------ replay ------
class Stats(collections.Counter):
    pass


class Replay:
    """VIF1 + VU1 data memory for one DMA stream. Object-kernel batches run
    through the native header (always) and the oracle (when the unit is
    chosen); `known` marks data-memory qwords whose value the replay saw
    uploaded (other programs' batches are not executed, so after one of
    them nothing is known)."""

    def __init__(self, elf, lib, packet, seed, stats, name):
        self.elf, self.lib, self.program, self.stats, self.name = elf, lib, packet['code'], stats, name
        self.vu = Oracle(elf)
        self.known = bytearray(1024)
        self.base = self.offset = self.tops = self.dbf = 0
        self.cl = self.wl = 1
        self.ours = False            # the last MSCAL ran this program
        self.synced = False          # oracle registers = native state
        self.oracle_unit = False
        self.state = State()
        self.rng = random.Random(seed)
        self.want, self.label, self.stale = True, None, False
        self.house = sh.VU1(elf)
        self.house.code = self.vu.code
        self.house_unit = False
        self.house_examples = []
        self.house_on = True           # off for synthetic batches (it cannot represent overflow)
        self.by_label = collections.Counter()
        self.tex0 = collections.Counter()
        self.st_ulps = collections.Counter()
        self.census = collections.Counter()   # MSCAL constants per unit (section 2 of the doc)
        self.faces = collections.Counter()    # 0x0023C480 CALLs: model REF address

    # -- stream
    def feed(self, data, at=lambda offset: (True, None, False)):
        """Replay a whole VIF1 stream; at(offset of an MSCAL/MSCNT code)
        gives (run the oracle for this unit, owner label, the unit lies past
        the capture's DMA write cursor)."""
        i, n = 0, len(data)
        while i + 4 <= n:
            v = struct.unpack_from('<I', data, i)[0]; i += 4
            cmd, num, imm = (v >> 24) & 0x7F, (v >> 16) & 0xFF, v & 0xFFFF
            if cmd >= 0x60:
                vn, vl, cnt = (cmd >> 2) & 3, cmd & 3, num or 256
                size = ((32 >> vl) * (vn + 1) * cnt + 31) // 32 * 4
                dst = (imm & 0x3FF) + (self.tops if imm & 0x8000 else 0)
                plain = vn == 3 and vl == 0 and not cmd & 0x10 and self.wl <= self.cl
                for k in range(cnt):
                    d = (dst + ((k // self.wl) * self.cl + k % self.wl if self.wl <= self.cl else k)) & 1023
                    if plain:
                        self.vu.mem[16 * d:16 * d + 16] = data[i + 16 * k:i + 16 * k + 16]
                        self.known[d] = 1
                    else:
                        self.known[d] = 0
                        self.stats['unpack_other_format_qwords'] += 1
                i += size; continue
            if cmd == 0x4A:
                cnt = num or 256
                self.vu.code[imm * 8:imm * 8 + 8 * cnt] = data[i:i + 8 * cnt]
                i += 8 * cnt; continue
            if cmd in (0x50, 0x51): i += 16 * (imm or 65536); continue
            if cmd == 0x20: i += 4; continue
            if cmd in (0x30, 0x31): i += 16; continue
            if cmd == 0x01: self.cl, self.wl = imm & 0xFF, (imm >> 8) & 0xFF; continue
            if cmd == 0x03: self.base = imm & 0x3FF; continue
            if cmd == 0x02: self.offset, self.dbf, self.tops = imm & 0x3FF, 0, self.base; continue
            if cmd in (0x14, 0x15, 0x17):
                self.want, self.label, self.stale = at(i - 4)
                self.kick(cmd, imm); continue
            if cmd in (0x00, 0x04, 0x05, 0x06, 0x07, 0x10, 0x11, 0x13): continue
            fail(f'{self.name}: VIF code {v:#010x}')

    def kick(self, cmd, imm):
        top = self.tops
        self.dbf ^= 1
        self.tops = self.base + (self.offset if self.dbf else 0)
        loaded = bytes(self.vu.code[:8 * INSTRUCTIONS]) == self.program
        mscal = cmd != 0x17
        ours = loaded and (imm == 0 if mscal else self.ours)
        if loaded and mscal and imm: self.stats['foreign_entry_with_kernel_loaded'] += 1
        if not ours:
            self.ours = self.synced = False
            self.known[:] = bytes(1024)
            self.stats['other_program_batches'] += 1
            return
        self.ours = True
        self.batch(mscal, top)

    # -- one object-kernel batch
    def live(self, before, top, mscal):
        """The data-memory qwords whose value reaches an output."""
        out = [(top + k) & 1023 for k in range(128)] + [1020]
        if mscal: out += [1013, 1014, 1015, 1016, 1021, 1022, 1023]
        for i in range(32):
            word = u32(before, 16 * ((top + 4 * i + 3) & 1023) + 12) & 0xFFFF
            out += [(word + k) & 1023 for k in range(7)]
        return out

    def batch(self, mscal, top):
        st, vu = self.stats, self.vu
        before = bytes(vu.mem)
        self.stats_mscal = mscal
        self.block = 0 if mscal else self.block + 1
        if self.lib.top_of(self.block) != top: st['top_not_block_rule'] += 1
        if mscal:
            row = lambda q: struct.unpack_from('<4I', before, 16 * q)
            prim = (row(1020)[1] >> 15) & 0x7FF
            self.census[('fog', prim) + row(1021)] += 1
            self.census[('guard', prim) + row(1022) + row(1023)] += 1
            self.census[('ambient_w', row(1016)[3])] += 1
            self.oracle_unit = self.want
            vu.randomise(self.rng)
            self.house_unit = self.house_on and self.want and (FULL or self.rng.random() < HOUSE_SHARE)
            if self.house_unit:
                h = self.house
                h.v = [list(r) for r in vu.v]
                h.vi, h.acc, h.cf = list(vu.vi), [sh.number(w) for w in vu.accw], vu.cf
                h.pending, h.cycle, h.q, h.i, h.q_ready = [], 0, 0.0, 0.0, 0
                h.ready = [[0] * 4 for _ in range(32)]
            for field, reg in (('tex0', 16), ('st', 2), ('rgbaq', 14)):
                getattr(self.state, field).w[:] = vu.v[reg]
            self.synced = True
        unknown = sum(1 for a in set(self.live(before, top, mscal)) if not self.known[a])
        st['batches'] += 1
        st['batches_mscal' if mscal else 'batches_mscnt'] += 1
        if unknown:
            st['batches_with_unknown_input'] += 1
            if not self.stale: st['unknown_input_outside_stale_units'] += 1
        mem, b = DMEM.from_buffer_copy(before), Batch()
        state_before = bytes(self.state)
        rc = self.lib.run(0 if mscal else 1, C.byref(self.state), mem, top, C.byref(b))
        if rc or b.fault:
            fail(f'{self.name}: native fault {b.fault} at vertex {b.fault_vertex} of a {"MSCAL" if mscal else "MSCNT"} batch at top {top:#x}')
        after = bytes(mem)
        if self.oracle_unit and self.synced:
            vu.kicks, vu.events, vu.top = [], [], top
            vu.watch = {MICRO_ADC, MICRO_EXIT, MICRO_RESUME}
            vu.flag_gap.clear()
            vu.run(0 if mscal else vu.resume)
            if sum(vu.flag_gap.values()) != 32: fail(f'{self.name}: {dict(vu.flag_gap)} clip-flag tests')
            for gap, k in vu.flag_gap.items(): st[f'clip_to_flag_test_cycles_{gap}'] += k
            kicks = [e for e in vu.events if e[0] == 'kick']
            if len(kicks) != 1 or kicks[0][1] != b.kick:
                fail(f'{self.name}: kicks {[(hex(k[1]), len(k[3])) for k in kicks]} native {b.kick:#x}')
            raw = sh.gif_raw(after, b.kick)
            if raw != kicks[0][3]:
                fail(f'{self.name}: kicked packet differs at top {top:#x}')
            if bytes(vu.mem) != after:
                diff = [q for q in range(1024) if vu.mem[16 * q:16 * q + 16] != after[16 * q:16 * q + 16]]
                fail(f'{self.name}: data memory differs at qwords {[hex(q) for q in diff[:8]]}')
            st['compared_batches'] += 1
            self.by_label[self.label] += 1
            st['compared_packet_bytes'] += len(raw)
            st['compared_dmem_bytes'] += 16384
            if unknown == 0: st['compared_batches_all_inputs_uploaded'] += 1
            adc = sum(1 for e in vu.events if e[0] == 'pc' and e[1] == MICRO_ADC)
            st['branch_adc_taken'] += 32 - adc
            st['branch_adc_fallthrough'] += adc
            st['loop_exits'] += sum(1 for e in vu.events if e[0] == 'pc' and e[1] == MICRO_EXIT)
            st['resumes'] += sum(1 for e in vu.events if e[0] == 'pc' and e[1] == MICRO_RESUME)
            self.checks(before, top, after, b, state_before)
            if self.house_unit: self.house_check(before, top, after, b)
        else:
            vu.mem[:] = after
            self.synced = False
            st['native_only_batches'] += 1
        for k in range(0x81, 0x105): self.known[(top + k) & 1023] = 1 if (self.oracle_unit and self.synced) else 0
        st['ftoi_saturated_lanes'] += b.ftoi_saturated
        for i in range(32):
            if b.saturated >> i & 1:
                st['saturated_vertices'] += 1
                if not b.why[i]: st['saturated_on_drawing_vertex'] += 1
        for i in range(32):
            st['vertices'] += 1
            if b.why[i] & 1: st['adc_data'] += 1
            if b.why[i] & 2: st['adc_clip'] += 1
            if not b.why[i]: st['vertices_kicked_drawing'] += 1
        return b

    def house_check(self, before, top, after, b):
        """The shadow test's VU1 interpreter unchanged (host double
        arithmetic, FTOI wrapping) on the same batch: every word it writes
        differently must be an XYZF2 lane the VU0 rule saturated."""
        h, st = self.house, self.stats
        h.mem = bytearray(before)
        h.kicks, h.events, h.top, h.watch = [], [], top, set()
        try:
            h.run(0 if self.stats_mscal else h.resume)
        except OverflowError:
            # a product beyond binary32 (the VU0 rule clamps it to MAX)
            st['house_unrepresentable_batches_' + ('stale_unit' if self.stale else 'live_unit')] += 1
            self.house_unit = False
            return
        xyzf = {(b.kick + 4 + 4 * i) & 1023 for i in range(32)}
        for q in range(1024):
            a, o = h.mem[16 * q:16 * q + 16], after[16 * q:16 * q + 16]
            if a == o: continue
            for lane in range(4):
                wa, wo = u32(a, 4 * lane), u32(o, 4 * lane)
                if wa == wo: continue
                if q in xyzf and wo in (0x7FFFFFFF, 0x80000000): st['house_ftoi_wrap_words'] += 1
                else:
                    st['house_other_words'] += 1
                    if len(self.house_examples) < 12:
                        rel = (q - b.kick) & 1023
                        self.house_examples.append(dict(list=self.name, owner=self.label, stale=self.stale,
                                                        top=hex(top), qword=hex(q), vertex=(rel - 1) // 4,
                                                        slot=(rel - 1) % 4, lane=lane, house=hex(wa),
                                                        vu0_rule=hex(wo)))
        st['house_batches'] += 1

    # -- E and F on a compared batch
    def checks(self, before, top, after, b, state_before):
        st = self.stats
        mem_before = DMEM.from_buffer_copy(before)
        mem_after = DMEM.from_buffer_copy(after)
        out = (C.c_uint32 * 5)()
        overlap = any(((u32(before, 16 * ((top + 4 * i + 3) & 1023) + 12) & 0xFFFF) + k - top) & 1023 in
                      range(0x81, 0x105) for i in range(32) for k in range(7))
        if not overlap:
            self.lib.consumers(mem_before, top, C.byref(self.state), mem_after, b.kick, out)
            st['consumer_batches'] += 1
            st['em_lighting_vertex_rgbaq_differs'] += out[0]
            st['em_shadow_gs_xyzf2_lanes_differ_drawing'] += out[1]
            st['em_shadow_gs_adc_differs'] += out[2]
            st['em_shadow_gs_xyzf2_lanes_differ_adc'] += out[3]
            st['em_shadow_gs_fog_lanes_differ_drawing'] += out[4]
        n, prim, regs, verts = decode_check(self.lib, after, b.kick, f'{self.name} top {top:#x}')
        if n < 0: fail(f'{self.name}: the kicked tag is none the decode accepts')
        st[f'packet_prim_{prim:#05x}_regs_{regs}'] += 1
        st[f'decoded_vertices_regs_{regs}'] += n
        tris = triangle_check(self.lib, verts, n, prim, f'{self.name} top {top:#x}')
        if tris is None: fail(f'{self.name}: PRIM {prim:#x} is not a triangle strip')
        if regs != 15:                     # the silhouette template: XYZ2 only
            st['triangles_silhouette'] += len(tris)
            return
        carried_w = struct.unpack_from('<I', state_before, State.st.offset + 12)[0]
        for i in range(n):
            vin = 16 * ((top + 4 * i) & 1023)
            kq = 16 * ((b.kick + 1 + 4 * i) & 1023)
            if after[kq:kq + 16] != before[vin:vin + 16]: fail(f'{self.name}: TEX0 qword {i} not the input')
            s_in, t_in, z_in = struct.unpack_from('<3I', before, 16 * ((top + 4 * i + 1) & 1023))
            S, T, Qw, Ww = struct.unpack_from('<4I', after, 16 * ((b.kick + 2 + 4 * i) & 1023))
            if Ww != carried_w: fail(f'{self.name}: ST w lane {i} is not the carried register')
            if z_in == 0x3F800000:
                st['st_z_is_one'] += 1
                if S != mul(s_in, Qw) or T != mul(t_in, Qw): fail(f'{self.name}: ST {i} is not (s, t) x Q')
            else:
                st['st_z_not_one'] += 1
            if not b.why[i]:
                self.tex0[verts[i].tex0 & ~(7 << 61) & (2**64 - 1)] += 1
                if fm._exp(Qw) not in (0, 255) and fm._exp(s_in) not in (0, 255):
                    back = sh.bits(sh.number(S) / sh.number(Qw))
                    self.st_ulps[min(abs(back - s_in), 4) if (back ^ s_in) >> 31 == 0 else 99] += 1
        st['triangles'] += len(tris)
        for i in tris:
            t3 = {verts[i - 2].tex0, verts[i - 1].tex0, verts[i].tex0}
            if len(t3) > 1:
                st['triangles_tex0_differ_in_cld_only' if len({t & ~(7 << 61) for t in t3}) == 1
                   else 'triangles_tex0_differ_beyond_cld'] += 1


# ------------------------------------------------------------ B. lists ----
def captures():
    out = [(f'startup/{n}', REF / n) for n in lm.CAPTURES]
    if ROUTE.exists():
        out += [(f'route/{p.name}', p / 'eeMemory.bin') for p in sorted(ROUTE.iterdir())
                if in_scope_beat(p.name) and (p / 'eeMemory.bin').exists()]
    return out


def labels(ram):
    """model address -> owner label."""
    out = collections.defaultdict(set)
    a, seen = u32(ram, 0x275BC0), set()
    while a and a not in seen:
        seen.add(a)
        b = u32(ram, a + 0x10)
        out[u32(ram, a + 0x44)].add(BEHAVIOUR.get(b, f'{b:08X}'))
        a = u32(ram, a + 0x1C)
    out[u32(ram, PLAYER + 0x44)].add('player')
    return {m: '/'.join(sorted(v)) for m, v in out.items()}


def list_units(name, path, head):
    """The object-kernel units of one list: (CALL tag address, label, blocks)."""
    ram = RAMS.get(name) or path.read_bytes()
    lab = labels(ram)
    tags = list(lm.dma_walk(ram, head))
    out = []
    for k, (tag, tid, qwc, addr, data) in enumerate(tags):
        if tid == 5 and addr == KERNEL:
            ref = None
            for t in tags[k + 1:]:
                if t[1] == 5: break
                if t[1] == 3 and t[3] != FOG_OFF_ROW and t[2] >= 0x82:
                    ref = t; break
            model = ref[3] - 0x40 if ref else 0
            out.append((tag, lab.get(model, 'model %08X' % model), ref[2] // 0x82 if ref else 0))
    return out


RAMS = {}
RUN = {}


def list_job(item):
    """Replay one list; the units in `chosen` (CALL tag addresses) run the oracle."""
    name, path, head, chosen = item
    elf, lib, packet = RUN['elf'], RUN['lib'], RUN['packet']
    ram = path.read_bytes()
    lo, hi = packet['range']
    if ram[lo:hi] != elf[lo - 0x100000 + 0x300:hi - 0x100000 + 0x300]:
        fail(f'{name}: the kernel packet in RAM differs from the ELF')
    stats = Stats()
    rp = Replay(elf, lib, packet, seed_of(name, head), stats, f'{name}@{head:#x}')
    lab = labels(ram)
    cursor = u32(ram, u32(ram, 0x275670) + 0x10)
    unit, label = None, None
    stream, marks = bytearray(), []          # (stream offset, unit, label)
    face, faces = None, []                   # (CALL tag, REF targets) of each 0x0023C480 CALL
    roger = any('Roger' in v for v in lab.values())
    for tag, tid, qwc, addr, data in lm.dma_walk(ram, head):
        if tid == 5:
            if face: faces.append(face)
            face = (tag, []) if addr == FACE_KERNEL else None
            if addr == KERNEL: unit, label = tag, None
            else:
                unit = None
                if addr == CLIP_KERNEL: stats['clip_program_calls'] += 1
        # a live CALL's REFs stop at the write cursor (stale bytes follow it)
        if tid == 3 and face is not None and (cursor <= face[0] or not cursor <= tag < cursor + 0x20000):
            face[1].append(addr)
        if tid == 3 and unit and label is None and addr != FOG_OFF_ROW and qwc >= 0x82:
            label = lab.get(addr - 0x40, 'model %08X' % (addr - 0x40))
        if qwc:
            marks.append((len(stream), unit, label))
            stream += ram[data:data + 16 * qwc]
    starts = [m[0] for m in marks]

    def at(offset):
        import bisect
        _, u, l = marks[bisect.bisect_right(starts, offset) - 1]
        return u in chosen, l, u is not None and cursor <= u < cursor + 0x20000
    if face: faces.append(face)
    rp.feed(bytes(stream), at)
    by_label = rp.by_label
    return dict(stats=dict(stats), labels=dict(by_label), tex0=dict(rp.tex0), st_ulps=dict(rp.st_ulps),
                house_examples=rp.house_examples, census=list(rp.census.items()),
                faces=[(next((FACES[a - 0x40] for a in refs if a - 0x40 in FACES), None), roger,
                         cursor <= tag < cursor + 0x20000) for tag, refs in faces])


def f32(w): return sh.number(w)


def census_report(census, faces, elf, mscal_batches):
    """Section 2 of the doc, reproduced: the fog rows, the guard rows (and
    the band they give) and the ambient w lane of every unit's MSCAL, and
    the 0x0023C480 CALLs. Values are reported as counts, bands and hashes."""
    fog, guard, amb = collections.defaultdict(collections.Counter), {}, collections.Counter()
    for key, n in census:
        if key[0] == 'fog': fog[key[1]][key[2:]] += n
        elif key[0] == 'guard': guard.setdefault(key[2:], collections.Counter())[key[1]] += n
        else: amb[key[1]] += n
    units = sum(amb.values())
    if units != mscal_batches: fail(f'census: {units} units, {mscal_batches} MSCAL batches')
    if set(amb) != {0x4B000000}: fail(f'census: ambient w lanes {sorted(map(hex, amb))}')
    rows = {r for c in fog.values() for r in c}
    if any(r[0] != 0x437F0000 or r[1] != 0x45000000 for r in rows): fail('census: a fog row with x != 255 or y != 2048')
    blended = fog.get(0x07C, {})
    if set(blended) - {(0x437F0000, 0x45000000, 0x437F0000, 0)}: fail('census: a blended unit with fog')
    lit = set(fog.get(0x03C, {})) | set(fog.get(0x004, {}))
    if len(lit) != 1 or next(iter(lit))[3] == 0: fail(f'census: {len(lit)} fog rows on opaque/silhouette units')
    if set(fog) - {0x03C, 0x07C, 0x004}: fail(f'census: templates {sorted(map(hex, fog))}')
    variants = []
    for words, prims in sorted(guard.items(), key=lambda kv: -sum(kv[1].values())):
        sc, of = [f32(w) for w in words[:4]], [f32(w) for w in words[4:]]
        # g = scale * c + offset * c.w, CLIP of g.xyz against |g.w|: lane k
        # passes while c.k / c.w lies in [(-1 - offset) / scale, (1 - offset) / scale]
        # g.w = scale.w * c.w + offset.w * c.w: one of the two is 1, the other 0
        if (words[3], words[7]) not in ((0x3F800000, 0), (0, 0x3F800000)) or min(sc[:3]) <= 0:
            fail(f'census: a guard row whose g.w is not c.w {dict(prims)}')
        band = [[round((-1 - of[c]) / sc[c], 3), round((1 - of[c]) / sc[c], 3)] for c in range(3)]
        variants.append(dict(units=sum(prims.values()), by_template={f'{p:#05x}': n for p, n in prims.items()},
                             band_x=band[0], band_y=band[1], band_z=band[2],
                             rows_sha256=hashlib.sha256(struct.pack('<8I', *words)).hexdigest()[:16]))
    out = dict(units=units, ambient_w_8388608=amb[0x4B000000],
               fog_units={f'{p:#05x}': sum(c.values()) for p, c in sorted(fog.items())},
               fog_rows_distinct=len(rows),
               fog_row_opaque_sha256=hashlib.sha256(struct.pack('<4I', *next(iter(lit)))).hexdigest()[:16],
               guard_variants=variants)
    # the face morph program: 001CAA00 -> 001CB3C0 -> 001D3F50 -> 001D3E40,
    # which appends the CALL; every CALL's model REF is a face resource + 0x40
    lo, hi = FACE_BUILDER
    words = [e32(elf, a) for a in range(lo, hi, 4)]
    has_hi = any(w >> 26 == 0x0F and w & 0xFFFF == (FACE_KERNEL + 0x8000) >> 16 for w in words)
    has_lo = any(w >> 26 == 0x09 and w & 0xFFFF == FACE_KERNEL & 0xFFFF for w in words)
    if not (has_hi and has_lo): fail('001D3E40 does not build the address 0x0023C480')
    for (a, b), callee in FACE_CHAIN:
        if not any(e32(elf, x) >> 26 in (2, 3) and e32(elf, x) & 0x3FFFFFF == callee >> 2 for x in range(a, b, 4)):
            fail(f'no call to {callee:08X} in {a:08X}..{b:08X}')
    who = collections.Counter((name or 'unattributed', stale) for name, _, stale in faces)
    if who[('unattributed', False)]: fail(f'face CALLs without a face model REF: {dict(who)}')
    if not all(r for name, r, _ in faces if name == 'Roger'): fail('a Roger face CALL in a list without Roger')
    out['face_program'] = dict(calls=len(faces), builder='001D3E40',
                               by_face={f'{n} {"stale" if s else "live"}': c for (n, s), c in sorted(who.items())})
    if True:                     # every list is replayed in both modes, so the census is always whole
        want = dict(units=950, fog={0x03C: 770, 0x07C: 137, 0x004: 43}, guard=[907, 43],
                    faces={('Roger', False): 41, ('Dennis', False): 5, ('Dennis', True): 2})
        got = dict(units=units, fog={p: sum(c.values()) for p, c in fog.items()},
                   guard=[v['units'] for v in variants], faces=dict(who))
        if got != want: fail(f'census {got} != the documented {want}')
        bands = [(v['band_x'], v['band_y'], v['band_z'], v['by_template']) for v in variants]
        if bands != [([8.0, 4088.0], [8.0, 4088.0], [-1.0, 16777215.0], {'0x03c': 770, '0x07c': 137}),
                     ([8.0, 4088.0], [8.0, 4088.0], [-8355840.498, 8355840.498], {'0x004': 43})]:
            fail(f'census guard bands {bands}')
    return out


# ------------------------------------------------------------ C. units ----
def unit_stream(o, used, base):
    """The DMA unit 001CAA00 wrote at `base`, walked as the DMAC would:
    [(data bytes, (CALL target or None))]."""
    out, a, kernel = [], base, None
    while a < base + used:
        w0, addr = struct.unpack('<2I', o.read(a, 8))
        tid, qwc = (w0 >> 28) & 7, w0 & 0xFFFF
        if tid == 1:
            out.append((o.read(a + 16, 16 * qwc), kernel)); a += 16 * (qwc + 1)
        elif tid == 3:
            if qwc: out.append((o.read(addr, 16 * qwc), kernel))
            a += 16
        elif tid == 5:
            kernel = addr
            k = addr
            while True:
                kw, _ = struct.unpack('<2I', o.read(k, 8))
                kt, kq = (kw >> 28) & 7, kw & 0xFFFF
                if kt == 6: break
                if kt != 1: fail(f'kernel packet {addr:#x}: tag {kt}')
                out.append((o.read(k + 16, 16 * kq), kernel)); k += 16 * (kq + 1)
            a += 16
        else:
            fail(f'unit tag id {tid} at {a:#x}')
    return out


def owner_job(item):
    import test_owner_draw_reference as tod
    beat, owner = item
    ram, spr = RAMS[beat]
    tod.ELF, tod.CAP[beat] = RUN['elf'], (ram, spr)
    o, ctx = tod.original_draw(ram, spr, owner)
    used = o.load(ctx + 0x10) - tod.CAP_DL
    behaviour = u32(ram, owner + 0x10)
    res = dict(beat=beat, owner=owner, behaviour=behaviour, used=used, batches=0, clip=False)
    if not used: return res
    stats = Stats()
    rp = Replay(RUN['elf'], RUN['lib'], RUN['packet'], seed_of(beat, owner), stats,
                f'{beat} owner {owner:#x}')
    stream = unit_stream(o, used, tod.CAP_DL)
    res['clip'] = any(kernel == CLIP_KERNEL for _, kernel in stream)
    rp.feed(b''.join(data for data, _ in stream))
    if stats['batches_with_unknown_input']:
        fail(f'{beat} owner {owner:#x}: the kernel read data memory the unit did not upload')
    if stats['compared_batches'] != stats['batches'] or not stats['batches'] or stats['top_not_block_rule'] \
            or stats['saturated_on_drawing_vertex']:
        fail(f'{beat} owner {owner:#x}: {dict(stats)}')
    res.update(batches=stats['batches'], stats=dict(stats))
    return res


def owner_items():
    import test_owner_draw_reference as tod
    import export_world_models as ewm
    bank = {ewm.TABLE_ADDRESS + m['offset'] for m in ewm.build(DECOMP / 'extract')['models']}
    items = []
    for beat in tod.BEATS:
        p = ROUTE / beat
        if not ((p / 'eeMemory.bin').exists() and (p / 'scratchpad.bin').exists()): continue
        ram = (p / 'eeMemory.bin').read_bytes()
        RAMS[beat] = (ram, (p / 'scratchpad.bin').read_bytes())
        a, seen = u32(ram, 0x275BC0), set()
        while a and a not in seen:
            seen.add(a)
            if u32(ram, a + 0x4C) == tod.DRAW and u32(ram, a + 0x44) in bank:
                items.append((beat, a))
            a = u32(ram, a + 0x1C)
    return items


# ------------------------------------------------------------ D. synthetic
def rbits(rng, lo, hi): return sh.bits(sh.fp(rng.uniform(lo, hi)))


def finite(rng):
    w = rng.getrandbits(32)
    return w ^ 0x00800000 if (w >> 23) & 0xFF == 0xFF else w


SPECIAL = [0, 0x80000000, 0x00000001, 0x807FFFFF, 0x00800000, 0x7F7FFFFF, 0xFF7FFFFF, 0x3F800000,
           0xBF800000]


def synthetic_dmem(rng, style, top, nodes):
    mem = bytearray(struct.pack('<4096I', *[finite(rng) for _ in range(4096)]))
    def put(q, words): struct.pack_into('<4I', mem, 16 * (q & 1023), *[w & M32 for w in words])
    def f(v): return sh.bits(sh.fp(v))
    scale = {'huge': 1e30, 'tiny': 1e-30}.get(style, 1.0)
    rowbase = 0x80 if top >= 0x2F1 else 0     # outputs wrap to at most dmem 0x74
    outputs = {(top + k) & 1023 for k in range(0x81, 0x105)}
    for n in range(nodes):
        a = rowbase + 8 * n
        # screen-space rows as 001C7420 uploads them: x and y lanes about
        # 2048 x the w lane (the GS window centre), so most vertices land
        # inside the guard band and 'guard' positions leave it
        lo = 0.0 if style == 'overlap' else -1.0
        for r in range(4):
            wl = rng.uniform(50, 900) if r == 3 else rng.uniform(lo, 1)
            d = rng.uniform(-9000, 9000) if r == 3 else rng.uniform(40 * lo, 40)
            row = [f(2048 * wl + d), f(2048 * wl + rng.uniform(40 * lo, 40) * (200 if r == 3 else 1)),
                   rbits(rng, 0 if style == 'overlap' else -3e5, 3e5), f(wl)]
            if style in ('huge', 'tiny'): row = [f(sh.number(w) * scale) for w in row]
            if style == 'wzero' and r == 3: row[3] = 0
            if style == 'negw' and r == 3: row[3] = f(-abs(sh.number(row[3])))
            put(a + r, row)
        for r in range(3):
            row = [rbits(rng, -1, 1) for _ in range(4)]
            if style == 'dead255': row[3] = 0x7F800000 | rng.getrandbits(23)
            put(a + 4 + r, row)
    bias = f(8388608.0)
    for r in range(3): put(1013 + r, [rbits(rng, 0, 200) for _ in range(3)] + [rbits(rng, 0, 50)])
    put(1016, [f(8388608.0 + rng.uniform(0, 120)) for _ in range(3)] + [bias if rng.random() < .5 else f(8388608.0 + 128)])
    tag = 0x8000 | 32 | 1 << 46 | 0x03C << 47 | 4 << 60
    if style == 'nloop': tag = (tag & ~0x7FFF) | rng.randrange(1, 33)
    put(1020, [tag & M32, tag >> 32, 0x4126, 0])
    fogoff = style == 'fogoff'
    put(1021, [f(255.0), f(2048.0), f(255.0) if fogoff else rbits(rng, -600, 600), 0 if fogoff else rbits(rng, -3, 3)])
    if style == 'fogrow':
        # every capture has x = 255 and y = 2048: here the cap and the ADC
        # addend are arbitrary, so reading the row's lanes is observable
        put(1021, [rbits(rng, -100, 400), rbits(rng, -5000, 5000), rbits(rng, -600, 600), rbits(rng, -3, 3)])
    put(1022, [0x3A008081, 0x3A008081, 0x34008080, 0])
    put(1023, [0xBF808081, 0xBF808081, 0, 0x3F800000])
    if style == 'edge':
        # identity rows and guard: g = c = (p, 1), so a coordinate of exactly
        # +-1 sits on the CLIP boundary (strictly greater / less clips)
        put(1022, [0x3F800000, 0x3F800000, 0x3F800000, 0])
        put(1023, [0, 0, 0, 0x3F800000])
        for n in range(nodes):
            for r in range(4):
                put(rowbase + 8 * n + r, [0x3F800000 if k == r else 0 for k in range(4)])
    for i in range(32):
        v = top + 4 * i
        slot = rowbase + 8 * rng.randrange(nodes)
        if style == 'wild' and rng.random() < .3:
            slot = rng.randrange(1024)
            while {(slot + k) & 1023 for k in range(7)} & outputs: slot = rng.randrange(1024)
        # rows over the stale-store slots and vertex 0's outputs (TOP + 0x81..
        # 0x87): vertex 0 reads them before any store, vertex 1 after the
        # carried stores, vertices 2.. after vertex 0's outputs too
        if style == 'overlap' and rng.random() < .3: slot = (top + 0x81) & 1023
        word = slot | (0x8000 if rng.random() < .15 else 0) | (rng.getrandbits(4) << 10 if style == 'wild' else 0)
        wf = sh.bits(-1.0 if rng.random() < .5 else 1.0) & 0xFFFF0000 | word
        pos = [rbits(rng, 0 if style == 'overlap' else -60, 60) for _ in range(3)]
        if style == 'special': pos = [rng.choice(SPECIAL) if rng.random() < .3 else p for p in pos]
        if style == 'guard': pos = [rbits(rng, -6000, 6000) for _ in range(3)]
        if style == 'edge':
            one = 0x3F800000
            pos = [rng.choice([one, one | SIGN, one - 1, 0x3F000000]) if rng.random() < .97
                   else rng.choice([one + 1, (one + 1) | SIGN]) for _ in range(3)]
        put(v + 3, pos + [wf])
        nrm = [rbits(rng, -1, 1) for _ in range(3)] + [0]
        if style == 'special': nrm = [rng.choice(SPECIAL) if rng.random() < .3 else p for p in nrm]
        if style == 'dead255': nrm[3] = 0xFF800000
        put(v + 2, nrm)
        st = [rbits(rng, -4, 4), rbits(rng, -4, 4), 0x3F800000 if rng.random() < .8 else rbits(rng, -2, 2),
              0x7FC00000 if style == 'dead255' else 0]
        put(v + 1, st)
        put(v, [finite(rng) for _ in range(4)])
    return mem


STYLES = ('plain', 'fogoff', 'wzero', 'negw', 'special', 'guard', 'huge', 'tiny', 'wild', 'overlap',
          'dead255', 'nloop', 'mscnt', 'edge', 'fogrow')


def synthetic_job(item):
    style, seed = item
    rng = random.Random(seed)
    stats = Stats()
    rp = Replay(RUN['elf'], RUN['lib'], RUN['packet'], seed, stats, f'synthetic {style} {seed}')
    rp.vu.code[:8 * INSTRUCTIONS] = RUN['packet']['code']
    rp.house_on = False
    rp.want = True
    # the double buffer, anywhere below the constants, and outputs that wrap
    # over the constants and the low rows (inputs stay below dmem 1013)
    tops = [0x1B0, 0x2BE, rng.randrange(0x100, 0x2F1), rng.randrange(0x2F1, 0x371)]
    top = tops[seed % len(tops)]
    rp.vu.mem[:] = synthetic_dmem(rng, style, top, 1 + rng.randrange(20))
    rp.known[:] = b'\1' * 1024
    rp.oracle_unit = True
    rp.batch(True, top)
    if style == 'mscnt':
        for k in range(3):
            top2 = (0x1B0, 0x2BE)[(k + 1) & 1]
            fresh = synthetic_dmem(rng, 'plain', top2, 20)
            for q in list(range(top2, top2 + 128)) + list(range(0x80 + 160)) + [1013, 1014, 1015, 1016, 1020, 1021, 1022, 1023]:
                q &= 1023
                rp.vu.mem[16 * q:16 * q + 16] = fresh[16 * q:16 * q + 16]
            rp.batch(False, top2)
    return dict(stats)


def fault_cases(lib, packet, elf):
    """An exponent-255 word in a live lane faults the translation; the same
    word in a dead lane does not."""
    rng = random.Random(0x255)
    live = [('position x', lambda m, t, w: struct.pack_into('<I', m, 16 * ((t + 3) & 1023), 0x7F800000)),
            ('normal y', lambda m, t, w: struct.pack_into('<I', m, 16 * ((t + 2) & 1023) + 4, 0xFF800000)),
            ('ST s', lambda m, t, w: struct.pack_into('<I', m, 16 * ((t + 1) & 1023), 0x7FC00000)),
            ('M row 3 w', lambda m, t, w: struct.pack_into('<I', m, 16 * ((w + 3) & 1023) + 12, 0x7F800001)),
            ('L row 1 z', lambda m, t, w: struct.pack_into('<I', m, 16 * ((w + 5) & 1023) + 8, 0x7F800000)),
            ('colour ambient w', lambda m, t, w: struct.pack_into('<I', m, 16 * 1016 + 12, 0x7F800000)),
            ('fog B', lambda m, t, w: struct.pack_into('<I', m, 16 * 1021 + 12, 0xFF800000)),
            ('guard offset w', lambda m, t, w: struct.pack_into('<I', m, 16 * 1023 + 12, 0x7FFFFFFF))]
    faults = 0
    for label, poke in live:
        top = 0x1B0
        mem = synthetic_dmem(rng, 'plain', top, 4)
        word = u32(mem, 16 * (top + 3) + 12) & 0xFFFF
        poke(mem, top, word)
        st, b = State(), Batch()
        m = DMEM.from_buffer_copy(mem)
        rc = lib.run(0, C.byref(st), m, top, C.byref(b))
        if rc != -1 or b.fault != 2: fail(f'exponent-255 {label}: no fault')
        faults += 1
    st, b = State(), Batch()
    if lib.run(1, C.byref(st), DMEM(), 0x1B0, C.byref(b)) != -1 or b.fault != 3: fail('MSCNT before MSCAL')
    if lib.run(0, None, DMEM(), 0x1B0, C.byref(b)) != -1 or b.fault != 1: fail('NULL state')
    return faults + 2


# ------------------------------------------------------------ S. PCSX2 ---
# The one save state whose VU1 micro memory holds the object kernel (the
# other startup-reference and route states have other programs loaded).
STATE = REF / 'portable-data/sstates/SCUS-97112 (0AE679AF).14.p2s'
STATE_TOPS = (0x1B0, 0x2BE)


def vu1_state():
    """(vu1Memory, vu1MicroMem) of STATE, extracted once with the decomp
    venv into build/, or (None, reason)."""
    cache = OUT / 'states'
    mem, micro = cache / '14_vu1Memory.bin', cache / '14_vu1MicroMem.bin'
    if not (mem.exists() and micro.exists()):
        venv = DECOMP / '.venv/bin/python'
        if not (STATE.exists() and venv.exists()): return None, f'missing {STATE.name} or the decomp venv'
        cache.mkdir(parents=True, exist_ok=True)
        code = ('import sys; from pathlib import Path; from tools.parse_pcsx2_state import extract_zstd_entry; '
                'p = Path(sys.argv[1]); '
                '[Path(o).write_bytes(extract_zstd_entry(p, n)) for n, o in '
                '(("vu1Memory.bin", sys.argv[2]), ("vu1MicroMem.bin", sys.argv[3]))]')
        subprocess.run([str(venv), '-c', code, str(STATE), str(mem), str(micro)], cwd=DECOMP, check=True)
    return (mem.read_bytes(), micro.read_bytes()), None


def state_check(lib, packet):
    """PCSX2's VU1 ran the kernel over both double-buffer blocks of this
    image. Re-running the header over the same inputs (each block as an
    MSCAL: the constants the MSCAL loaded are still in dmem 1013..1023)
    must give PCSX2's template qword and every output lane except ST w (the
    carried register): 32 x 15 = 480 lanes per block."""
    got, reason = vu1_state()
    if got is None: return dict(skipped=reason)
    img, micro = got
    if len(img) != 16384 or micro[:8 * INSTRUCTIONS] != packet['code']:
        fail(f'{STATE.name}: the object kernel is not in VU1 micro memory')
    out = dict(state=STATE.name, micro_sha256=hashlib.sha256(micro[:8 * INSTRUCTIONS]).hexdigest()[:16], blocks=[])
    for top in STATE_TOPS:
        m, b = DMEM.from_buffer_copy(img), Batch()
        if lib.run(0, C.byref(State()), m, top, C.byref(b)) or b.fault: fail(f'{STATE.name}: native fault at {top:#x}')
        after, equal, lanes, st_w = bytes(m), 0, 0, set()
        k = (top + 0x84) & 1023
        if after[16 * k:16 * k + 16] != img[16 * k:16 * k + 16]: fail(f'{STATE.name}: template at {k:#x} differs')
        for i in range(32):
            for slot in range(4):
                q = (top + 0x85 + 4 * i + slot) & 1023
                for lane in range(4):
                    if slot == 1 and lane == 3:
                        st_w.add(img[16 * q + 12:16 * q + 16]); continue
                    lanes += 1
                    equal += after[16 * q + 4 * lane:16 * q + 4 * lane + 4] == img[16 * q + 4 * lane:16 * q + 4 * lane + 4]
        if equal != lanes or lanes != 480: fail(f'{STATE.name}: block at {top:#x}: {equal} of {lanes} lanes equal PCSX2')
        prim = (u32(img, 16 * k + 4) >> 15) & 0x7FF
        out['blocks'].append(dict(top=hex(top), prim=hex(prim), lanes_equal=equal,
                                  drawing=sum(1 for i in range(32) if not b.why[i]),
                                  adc_data=sum(1 for i in range(32) if b.why[i] & 1),
                                  adc_clip=sum(1 for i in range(32) if b.why[i] & 2),
                                  st_w_lanes_one_value=len(st_w) == 1))
    return out


# ------------------------------------------------------------ main --------
def check_capture_code(elf, packet):
    lo, hi = packet['range']
    n = 0
    for name, path in captures():
        ram = path.read_bytes()
        if ram[0x810700] != 11: fail(f'{name}: not AREA11')
        if ram[lo:hi] != elf[lo - 0x100000 + 0x300:hi - 0x100000 + 0x300]:
            fail(f'{name}: the kernel packet in RAM differs from the ELF')
        n += 1
    return n


def merge(dst, src):
    for k, v in src.items(): dst[k] = dst.get(k, 0) + v


def main(argv):
    started = time.time()
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    if hashlib.sha256(elf).hexdigest() != ELF_SHA256: fail('not the pinned SCUS-97112 ELF')
    packet = kernel_packet(elf)
    if '--defects' in argv: return defects(elf, packet)
    lib = native_library(os.environ.get('EM_VU1_OBJK_HEADER'), os.environ.get('EM_VU1_OBJK_TAG', 'native'))
    RUN.update(elf=elf, lib=lib, packet=packet)
    report = dict(elf_sha256=ELF_SHA256, program=dict(packet=hex(KERNEL), mpg=hex(PROGRAM),
                  instructions=INSTRUCTIONS, base=hex(packet['base']), offset=hex(packet['offset']),
                  program_sha256=hashlib.sha256(packet['code']).hexdigest()))
    report['captures_checked'] = check_capture_code(elf, packet)

    # B. captured lists
    units = []
    for name, path in captures():
        for head in lm.LIST_HEADS:
            for tag, label, blocks in list_units(name, path, head):
                units.append((name, head, tag, label, blocks))
    chosen = select(units, 40, 0xB0B, axes=(lambda u: u[3], lambda u: u[0]),
                    keep=lambda i, u: u[4] <= 2 and i % 7 == 0)
    by_list = collections.defaultdict(set)
    for u in chosen: by_list[(u[0], u[1])].add(u[2])
    items = [(name, path, head, by_list.get((name, head), set()))
             for name, path in captures() for head in lm.LIST_HEADS]
    results = parallel_map(list_job, items, cost=lambda i: len(i[3]))
    total, label_batches, tex0, st_ulps = {}, collections.Counter(), collections.Counter(), collections.Counter()
    for r in results:
        merge(total, r['stats']); label_batches.update(r['labels']); tex0.update(r['tex0'])
        st_ulps.update({int(k): v for k, v in r['st_ulps'].items()})
    if total.get('batches_with_unknown_input', 0) and FULL:
        # every compared batch's inputs came from uploads the replay saw
        pass
    if total.get('compared_batches', 0) == 0: fail('no captured batch compared')
    if total.get('unknown_input_outside_stale_units', 0):
        fail('an object-kernel batch read data memory no upload of its list wrote')
    house_examples = [e for r in results for e in r['house_examples']]
    for key in ('saturated_on_drawing_vertex', 'triangles_tex0_differ_beyond_cld', 'top_not_block_rule'):
        if total.get(key, 0): fail(f'captured batches: {key} = {total[key]}')
    unit_labels = collections.Counter(u[3] for u in units)
    report['census'] = census_report([kv for r in results for kv in r['census']],
                                     [f for r in results for f in r['faces']], elf, total.get('batches_mscal', 0))
    report['B_captured_lists'] = dict(house_examples=house_examples, lists=len(items), units=len(units), units_compared=len(chosen),
                                      units_by_owner=dict(unit_labels), compared_batches_by_owner=dict(label_batches),
                                      stats=total)

    # C. executed owner units
    oitems = owner_items()
    ochosen = select(oitems, 12, 0xCAA, axes=(lambda i: u32(RAMS[i[0]][0], i[1] + 0x10),))
    ores = parallel_map(owner_job, ochosen)
    drawn = [r for r in ores if r['used']]
    otally = collections.defaultdict(lambda: collections.Counter())
    ostats = {}
    for r in drawn:
        t = otally[BEHAVIOUR.get(r['behaviour'], hex(r['behaviour']))]
        t['units'] += 1; t['batches'] += r['batches']; t['clip_units'] += r['clip']
        merge(ostats, r['stats'])
    if FULL and len(drawn) != 119: fail(f'{len(drawn)} drawn owner units, docs/OWNER_DRAW.md has 119')
    report['C_executed_owner_units'] = dict(owner_frames=len(ores), owner_frames_total=len(oitems),
                                            units_drawn=len(drawn), by_owner={k: dict(v) for k, v in otally.items()},
                                            stats=ostats)

    # D. synthetic
    sitems = [(s, 1000 + 97 * k + i) for i, s in enumerate(STYLES) for k in range(pick(16, 3))]
    sres = parallel_map(synthetic_job, sitems)
    sstats = {}
    for r in sres: merge(sstats, r)
    faults = fault_cases(lib, packet, elf)
    for key in ('branch_adc_taken', 'branch_adc_fallthrough', 'loop_exits', 'resumes'):
        if not sstats.get(key): fail(f'synthetic batches never reach {key}')
    if not sstats.get('ftoi_saturated_lanes'): fail('no synthetic FTOI saturation')
    report['D_synthetic'] = dict(cases=len(sitems), stats=sstats, fault_cases=faults)

    # the ADC window i-2..i rests on the CLIP flag being visible exactly at
    # the flag test: every compared vertex has the two CLIP_FLAG_LATENCY apart
    gaps = collections.Counter()
    for part_stats in (total, ostats, sstats):
        for k, v in part_stats.items():
            if k.startswith('clip_to_flag_test_cycles_'): gaps[int(k.rsplit('_', 1)[1])] += v
    if set(gaps) != {CLIP_FLAG_LATENCY}: fail(f'CLIP to flag-test distances {dict(gaps)}')
    report['clip_flag_window'] = dict(latency_assumed=CLIP_FLAG_LATENCY, vertices_at_exactly_that_distance=gaps[CLIP_FLAG_LATENCY])

    # F. GS decode and strip triangles on built tags (the captured packets
    # are checked batch by batch in B, C and D)
    report['F_decode_cases'] = decode_cases(lib, pick(4000, 400))

    # S. PCSX2's VU1 output in save state 14
    report['S_pcsx2_state'] = state_check(lib, packet)

    # F. TEX0 census
    report['F_tex0'] = dict(distinct_tex0=len(tex0),
                            fields=sorted({(t >> 20 & 0x3F, t >> 34 & 1, t >> 35 & 3) for t in tex0}),
                            st_back_ulps={str(k): v for k, v in sorted(st_ulps.items())})
    line = banner(part(len(chosen), len(units), 'captured object-kernel units') +
                  f" ({total['compared_batches']:,} batches compared, {total['batches']:,} replayed)",
                  part(len(ochosen), len(oitems), 'executed owner-frames') + f' ({len(drawn)} drawn)',
                  f'{len(sitems)} synthetic cases + {faults} fault cases',
                  f"{sum(v for k, v in report['F_decode_cases'].items() if k.startswith(('accepted', 'refused')))} decode cases",
                  'PCSX2 state 14: ' + ('skipped' if 'skipped' in report['S_pcsx2_state'] else
                                        '/'.join(str(b['lanes_equal']) for b in report['S_pcsx2_state']['blocks']) + ' lanes equal'))
    report.update(status='PASS', mode=line, seconds=round(time.time() - started, 1))
    OUT.mkdir(parents=True, exist_ok=True)
    tag = os.environ.get('EM_VU1_OBJK_TAG', 'native')
    (OUT / ('report.json' if tag == 'native' else f'{tag}_report.json')).write_text(
        json.dumps(report, indent=1, default=str) + '\n')
    summary = {k: report[k] for k in ('status', 'mode', 'seconds')}
    summary['compared'] = dict(captured=total['compared_batches'], owner_units=ostats.get('compared_batches', 0),
                               synthetic=sstats.get('compared_batches', 0))
    summary['consumers'] = {k: total.get(k, 0) for k in total if k.startswith(('consumer', 'em_'))}
    print(json.dumps(summary))
    return 0


# ------------------------------------------------------------ defects -----
DEFECTS = [
    ('position sum order', 'acc = emvuo_madd(acc, in->m[2][k], in->pos[2], &bad);\n        c[k] = emvuo_madd(acc, in->m[3][k], EM_EE_ONE, &bad);',
     'acc = emvuo_madd(acc, in->m[3][k], EM_EE_ONE, &bad);\n        c[k] = emvuo_madd(acc, in->m[2][k], in->pos[2], &bad);'),
    ('lighting not clamped', 'light[k] = em_vu_max_bits(light[k], 0u);', 'light[k] = light[k];'),
    ('colour cap', 'EM_VU1_OBJ_COLOUR_CAP 0x4B0000FFu', 'EM_VU1_OBJ_COLOUR_CAP 0x4B0000FEu'),
    ('fog clamp order', 'fog = em_vu_min_bits(fog, s->fog[0]);', 'fog = em_vu_max_bits(fog, 0u);'),
    ('fog ADC add', 'if (w) fog = emvuo_add(fog, s->fog[1], &bad);', 'if (0) fog = emvuo_add(fog, s->fog[1], &bad);'),
    ('clip history 2 vertices', 'if (*hist & 0x03FFFFu) w |= EM_VU1_OBJ_ADC_CLIP;',
     'if (*hist & 0x000FFFu) w |= EM_VU1_OBJ_ADC_CLIP;'),
    ('data bit', 'uint32_t w = (in->word & 0x8000u)', 'uint32_t w = (in->word & 0x4000u)'),
    ('Q from z', '(void)em_vu_div_bits(EM_EE_ONE, c[3], 3, 3, &q);', '(void)em_vu_div_bits(EM_EE_ONE, c[2], 3, 3, &q);'),
    ('ST w zero', 'out[1].w[3] = st_w;', 'out[1].w[3] = st_w & 0u;'),
    ('MSCNT reloads constants', 'return emvuo_batch(s, dmem, top, out);\n}\n\n/* TOP of block',
     'memcpy(s->fog, dmem[1021].w, 16);\n    return emvuo_batch(s, dmem, top, out);\n}\n\n/* TOP of block'),
    ('first stores skipped', 'for (unsigned k = 0; k < 4; ++k)             /* 0x01B..0x01E */\n            emvuo_st',
     'for (unsigned k = 0; k < 4 && i; ++k)        /* 0x01B..0x01E */\n            emvuo_st'),
    ('ST input read early', 'memcpy(cur.st_in, emvuo_ld(dmem, v + 1u).w, sizeof cur.st_in);   /* 0x029 */',
     '(void)0;'),
    ('ftoi0', 'out[3].w[k] = emvuo_ftoi4(scr[k], saturated);', 'out[3].w[k] = em_vu_ftoi0_bits(scr[k]);'),
    ('kick offset', 'EM_VU1_OBJ_KICK 0x84u', 'EM_VU1_OBJ_KICK 0x85u'),
    ('clip >=', 'if (v > w) f |= 1u << (2 * c);', 'if (v >= w) f |= 1u << (2 * c);'),
    ('guard offset lane', 's->guard_offset[k], c[3], &bad);', 's->guard_offset[k], c[2], &bad);'),
    ('look-ahead before stores',
     '            emvuo_st(dmem, v + 0x81u + k, prev[k]);\n        memcpy(cur.st_in, emvuo_ld(dmem, v + 1u).w, sizeof cur.st_in);   /* 0x029 */\n        emvuo_fetch(dmem, v + 4u, &next);             /* look-ahead */\n',
     '            (void)0;\n        memcpy(cur.st_in, emvuo_ld(dmem, v + 1u).w, sizeof cur.st_in);\n        emvuo_fetch(dmem, v + 4u, &next);\n        for (unsigned k = 0; k < 4; ++k) emvuo_st(dmem, v + 0x81u + k, prev[k]);\n'),
    ('lighting w lane', 'for (unsigned k = 0; k < 3; ++k) light[k] = em_vu_max_bits', 'for (unsigned k = 0; k < 2; ++k) light[k] = em_vu_max_bits'),
    ('clip history kept across batches', 'uint32_t hist = 0u;', 'static uint32_t hist = 0u;'),
    ('fog A times B', 'emvuo_mul(EM_EE_ONE, s->fog[2], &bad), s->fog[3], c[3], &bad);',
     'emvuo_mul(EM_EE_ONE, s->fog[2], &bad), s->fog[3], c[2], &bad);'),
    ('fog cap constant 255', 'fog = em_vu_min_bits(fog, s->fog[0]);', 'fog = em_vu_min_bits(fog, 0x437F0000u);'),
    ('ADC addend constant 2048', 'if (w) fog = emvuo_add(fog, s->fog[1], &bad);',
     'if (w) fog = emvuo_add(fog, 0x45000000u, &bad);'),
    # the GS-side decode and triangles (the renderer's entry points)
    ('decode F shift 5', 'v->f = (uint8_t)(q3.w[3] >> 4);', 'v->f = (uint8_t)(q3.w[3] >> 5);'),
    ('decode R and B swapped', 'v->r = (uint8_t)q2.w[0]; v->g = (uint8_t)q2.w[1];\n            v->b = (uint8_t)q2.w[2];',
     'v->r = (uint8_t)q2.w[2]; v->g = (uint8_t)q2.w[1];\n            v->b = (uint8_t)q2.w[0];'),
    ('decode G from R', 'v->g = (uint8_t)q2.w[1];', 'v->g = (uint8_t)q2.w[0];'),
    ('decode A from B', 'v->a = (uint8_t)q2.w[3];', 'v->a = (uint8_t)q2.w[2];'),
    ('decode Z 16 bits', 'v->z = (q3.w[2] >> 4) & 0xFFFFFFu;', 'v->z = (q3.w[2] >> 4) & 0xFFFFu;'),
    ('decode ST Q from lane w', 'v->q = q1.w[2];', 'v->q = q1.w[3];'),
    ('decode S from T', 'v->s = q1.w[0]; v->t = q1.w[1];', 'v->s = q1.w[1]; v->t = q1.w[1];'),
    ('decode XYZ2 Z shifted', 'v->z = q3.w[2];', 'v->z = q3.w[2] >> 4;'),
    ('decode X from the Y word', 'v->x = (uint16_t)q3.w[0];', 'v->x = (uint16_t)q3.w[1];'),
    ('decode Y from the X word', 'v->y = (uint16_t)q3.w[1];', 'v->y = (uint16_t)q3.w[0];'),
    ('decode ADC bit 14', 'v->adc = (uint8_t)((q3.w[3] >> 15) & 1u);', 'v->adc = (uint8_t)((q3.w[3] >> 14) & 1u);'),
    ('decode TEX0 low word only', 'v->tex0 = (uint64_t)q0.w[0] | (uint64_t)q0.w[1] << 32;', 'v->tex0 = (uint64_t)q0.w[0];'),
    ('decode NLOOP > 32 accepted', 'nloop > EM_VU1_OBJ_VERTICES ||', 'nloop > 64u ||'),
    ('decode EOP ignored', 'if (!((lo >> 15) & 1u) ||', 'if (0 ||'),
    ('decode FLG ignored', '((lo >> 58) & 3u) != 0u ||', '0 ||'),
    ('decode NREG ignored', '((lo >> 60) & 15u) != 4u ||', '0 ||'),
    ('decode slot 0 unchecked', '(r0 != 0x6u && r0 != 0xFu) ||', '0 ||'),
    ('decode slot 3 unchecked', '(r3 != 0x4u && r3 != 0x5u))', '0)'),
    ('decode PRE ignored', '((lo >> 46) & 1u) ? (uint32_t)((lo >> 47) & 0x7FFu) : ~0u',
     '(uint32_t)((lo >> 47) & 0x7FFu)'),
    ('decode XYZ2 as XYZF2', '(r3 == 0x4u ? EM_VU1_OBJ_GS_XYZF2 : 0u)', '(r3 != 0xFu ? EM_VU1_OBJ_GS_XYZF2 : 0u)'),
    ('decode ST slot always', '(r1 == 0x2u ? EM_VU1_OBJ_GS_ST : 0u)', 'EM_VU1_OBJ_GS_ST'),
    ('triangles from i = 3', 'for (int i = 2; i < count; ++i)', 'for (int i = 3; i < count; ++i)'),
    ('triangles ignore ADC', 'if (!v[i].adc) last[n++] = (uint8_t)i;', 'if (v[i].adc || 1) last[n++] = (uint8_t)i;'),
    ('triangles any PRIM', 'if ((prim & 7u) != 4u) return ~0u;', '(void)prim;'),
]


def defects(elf, packet):
    """Each defect in a copy of the header must make the quick run fail."""
    header = (ROOT / 'src/game/em_vu1_object_kernel.h').read_text()
    OUT.mkdir(parents=True, exist_ok=True)
    caught = []
    for k, (label, old, new) in enumerate(DEFECTS):
        if header.count(old) != 1: fail(f'defect {label!r}: pattern not unique in the header')
        path = OUT / f'defect_{k}.h'
        path.write_text(header.replace(old, new))
        env = dict(os.environ, EM_VU1_OBJK_HEADER=str(path), EM_VU1_OBJK_TAG=f'defect_{k}', EM_TEST_FULL='0')
        r = subprocess.run([sys.executable, __file__], env=env, capture_output=True, text=True)
        # caught = a check failed (an AssertionError), not a build error
        hit = r.returncode != 0 and 'AssertionError' in r.stderr
        caught.append((label, hit))
        print(f"defect {k:2d} {label}: {'caught' if hit else 'NOT CAUGHT'}", flush=True)
        for leftover in (path, OUT / f'defect_{k}.c', OUT / f'defect_{k}.dylib', OUT / f'defect_{k}_report.json'):
            if leftover.exists(): leftover.unlink()
    missed = [l for l, c in caught if not c]
    print(json.dumps(dict(defects=len(caught), caught=len(caught) - len(missed), missed=missed)))
    return 1 if missed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
