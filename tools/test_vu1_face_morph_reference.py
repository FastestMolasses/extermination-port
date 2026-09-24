#!/usr/bin/env python3
"""The VU1 face morph program (DMA CALL 0x0023C480) against the ORIGINAL microcode.

Checks src/game/em_vu1_face_morph.h, the CPU translation of the 80
instruction program the face packet uploads (ELF 0x0023C4B0), kick for kick
against a VU1 interpreter that executes those original instructions.
Docs: docs/VU1_FACE_MORPH.md. Reads the owner's pinned ELF and the captured
EE RAM under ../Extermination/build/; embeds no original bytes; the report
holds addresses, counts and hashes only.

The oracle is the object-kernel test's (tools/test_vu1_object_kernel_
reference.py): the shadow test's VU1 interpreter (in-order issue, VF operand
stalls, CLIP flags 4 cycles and Q 7 cycles after their producer, XGKICK
snapshots) with the VU0 lane rules of tools/ee_float_model.py. Every
compared batch: the translation and the interpreter start from the same
data memory and the same carried registers (every other VF/VI/ACC word is
randomised at every compared batch, so anything the program reads from a
register it did not write shows up); the XGKICK address, the kicked GIF
packet bytes, all 16 KiB of data memory, the four carried registers
afterwards and every vertex's morphed position (the interpreter's register
right after the morph) must be equal, and so must the per-vertex record:
why[i] != 0 exactly when the program reaches the ADC add of vertex i, its
CLIP bit is the program's clip-flag test, clip[i] the CLIP's six bits. On
every replayed batch why[i]'s data bit is bit 15 of the data word and (fog
row x/y = 255/2048) why[i] != 0 exactly when the kicked XYZF2 carries ADC.

A. The face packet: one DMA CNT whose VIF codes are exactly FLUSHA,
   STCYCL 4,4, STMASK 0, STMOD 0, BASE 0x20, OFFSET 0x1E5 and one
   80-instruction MPG to micro 0, then RET; the same bytes in every capture.
B. Captured display lists: both lists of every AREA11 capture
   (startup-reference, the status-hub capture and route beats 00..14) are
   replayed as the DMAC and VIF1 would (CALL/RET, UNPACK V4-32 with STCYCL
   and the TOPS double buffer, MPG, MSCAL/MSCNT). Every batch the face
   program runs goes through the native header; the oracle compares every
   batch in the full run and blocks 0, 1, 2 and every sixteenth from 9 of
   every unit in the quick run. Other programs are not executed (their
   batches are counted and clear what the replay knows of data memory).
   Census (asserted): the face CALLs and their faces, the upload each
   input comes from (the unit's own weights, colour, node-row and
   skin-record uploads and the face resource), the template, fog, guard
   and weight rows, the data words and the TOP rule. Every value the
   program reads must come from an upload of its own unit (stale units
   past a list's write cursor are compared and counted apart). Drawing
   vertices per face unit (report B_units) must follow the documented
   grouping (unit_drawing_want). On every
   compared vertex the CLIP and the clip-flag test are exactly 4 cycles
   apart, and the division and the Q multiply of the position exactly 7:
   both windows rest on those interpreter latencies.
C. Synthetic batches: random units and TOPs, fog-off rows, ADC data bits,
   zero and negative w, special bit patterns, guard-band exits, overflow,
   underflow, wild row addresses (mod 1024), rows over the store slots and
   outputs, exponent-255 words in dead lanes, short NLOOP, MSCNT with new
   constants (they DO reach an MSCNT batch here), CLIP-boundary coordinates,
   random fog-row x/y lanes, random weights, random guard rows (all eight
   lanes of dmem 1022/1023: x != y, scale.w != 0, offset.w != 1.0, so
   g.w != c.w; both CLIP outcomes asserted), signed-zero morphs (zero
   deltas, negative/-0/+0 weights, a +-0 base: -0 positions asserted) and
   signed-zero colour and ambient rows (-0 and +0 RGBAQ lanes asserted).
   Both outcomes of the ADC
   branch, the loop exit and the MSCNT resume are reached, and FTOI
   saturates. An exponent-255 word in a live lane must fault the
   translation, as must MSCNT without a batch before it and a NULL state.
D. GS decode: every captured and synthetic packet goes through
   em_vu1_object_kernel_decode and _triangles against the object test's
   independent PACKED decode (gs_decode) and strip enumeration.
E. TEX0/ST path and the port's CPU consumer: every kicked TEX0 qword is the
   vertex's qword 0; ST x/y equal s * Q and t * Q with the Q in ST z; ST w
   is the carried register. em_opening_face_position (the port's host-float
   morph, src/game/em_opening_face.c) against the exact morph on every
   compared captured vertex (counted, reported).
--defects: injects defects into a copy of the header and requires the
   quick checks to catch each one (not part of the default run).
"""
from pathlib import Path
import bisect
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
import test_vu1_object_kernel_reference as ok
from reference_mode import FULL, banner, part, pick, parallel_map, in_scope_beat

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
REF = DECOMP / 'build/startup-reference'
ROUTE = DECOMP / 'build/s87/route'
OUT = ROOT / 'build/vu1_face_morph_reference'
ELF_SHA256 = ok.ELF_SHA256
FACE_KERNEL, PROGRAM, INSTRUCTIONS = 0x23C480, 0x23C4B0, 80
BASE, OFFSET, VERTEX_QWORDS, STORE, KICK = 0x20, 0x1E5, 11, 0x160, 0x163
FACES = ok.FACES                              # face resource runtime address -> name
SKIN_RECORDS = 0x816440                       # 001D3E40's REF 8: + 0x80 * slot
MICRO_POSITION = 0x021 * 8                    # the first read of the morphed position
MICRO_DIV, MICRO_MULQ = 0x028 * 8, 0x02F * 8  # the division and the Q multiply of the position
MICRO_FLAG_TEST = 0x031 * 8                   # the clip-flag test (CLIP issues at micro 0x02D)
MICRO_CLIP = 0x02D * 8
MICRO_ADC, MICRO_EXIT, MICRO_RESUME = 0x038 * 8, 0x03D * 8, 0x04E * 8
CLIP_FLAG_LATENCY, Q_LATENCY = 4, 7           # the interpreter's model
CARRIED = (('tex0', 15), ('st', 2), ('rgbaq', 14), ('xyzf', 8))   # state field, VF register
M32 = 0xFFFFFFFF
SIGN = 0x80000000
ONE = 0x3F800000
ADC_DATA, ADC_CLIP = 1, 2                     # EM_VU1_OBJ_ADC_* (the why[] bits)
FOG_X_255, FOG_Y_2048 = 0x437F0000, 0x45000000

u32, e32, fail, seed_of = ok.u32, ok.e32, ok.fail, ok.seed_of


# ------------------------------------------------------------ A. packet ---
PACKET_CODES = [(0x13, 0, 0), (0x01, 0, 0x404), (0x20, 0, 0), (0x05, 0, 0), (0x03, 0, BASE),
                (0x02, 0, OFFSET), (0x4A, INSTRUCTIONS, 0)]


def face_packet(elf):
    """The CALLed packet: its VIF code sequence (exactly PACKET_CODES), the
    uploaded program and the packet byte range."""
    w0 = e32(elf, FACE_KERNEL)
    if (w0 >> 28) & 7 != 1: fail('face packet: not a CNT tag')
    if e32(elf, FACE_KERNEL + 8) or e32(elf, FACE_KERNEL + 12): fail('face packet: codes in the tag qword')
    qwc = w0 & 0xFFFF
    i, end, seq, mpg = FACE_KERNEL + 16, FACE_KERNEL + 16 + 16 * qwc, [], []
    while i < end:
        v = e32(elf, i)
        if v >> 31: fail('face packet: a VIF code with the interrupt bit')
        cmd, num, imm = (v >> 24) & 0x7F, (v >> 16) & 0xFF, v & 0xFFFF
        if cmd == 0x4A:
            seq.append((cmd, num, imm))
            mpg.append((imm, num or 256, i + 4)); i += 4 + 8 * (num or 256); continue
        if cmd == 0x20: seq.append((cmd, num, e32(elf, i + 4))); i += 8; continue
        seq.append((cmd, num, imm)); i += 4
    if i != end: fail('face packet: the codes overrun the CNT')
    if (e32(elf, end) >> 28) & 7 != 6: fail('face packet: no RET after the CNT')
    if seq != PACKET_CODES or mpg != [(0, INSTRUCTIONS, PROGRAM)]:
        fail(f'face packet codes {[(hex(c), n, hex(v)) for c, n, v in seq]} {mpg}')
    code = elf[PROGRAM - 0x100000 + 0x300:PROGRAM - 0x100000 + 0x300 + 8 * INSTRUCTIONS]
    return dict(code=code, range=(FACE_KERNEL, end + 16))


# ------------------------------------------------------------ oracle ------
class FaceOracle(ok.Oracle):
    """The object test's oracle; it also records, per vertex, the cycles
    from the CLIP to the clip-flag test and from the division to the Q
    multiply of the position, the morphed position as the next instruction
    reads it, the six clip bits the CLIP produces (`clips`) and the result
    of the clip-flag test over the flag register as the program's own
    instruction sees it (`flag_tests`)."""

    def __init__(self, elf):
        super().__init__(elf)
        self.div_at = None
        self.q_gap = collections.Counter()
        self.positions = []
        self.clips = []
        self.flag_tests = []

    def upper(self, pc, up):
        if pc == MICRO_MULQ and up & 63 == 0x1C: self.q_gap[self.cycle - self.div_at] += 1
        if pc == MICRO_POSITION: self.positions.append(tuple(self.v[3][:3]))
        out = super().upper(pc, up)
        if pc == MICRO_CLIP and up & 0x7FF == 0x1FF: self.clips.append(self.pending[-1][3])
        return out

    def lower(self, pc, lo):
        if pc == MICRO_FLAG_TEST and lo >> 25 == 0x12:
            self.flag_gap[self.cycle - self.clip_at] += 1
            self.flag_tests.append(bool(self.cf & lo & 0xFFFFFF))   # settled before lower()
        if pc == MICRO_DIV and lo >> 25 == 0x40 and lo & 0x7FF == 0x3BC: self.div_at = self.cycle
        return super().lower(pc, lo)


# ------------------------------------------------------------ native ------
Q, DMEM, Batch, GsVertex = ok.Q, ok.DMEM, ok.Batch, ok.GsVertex


class State(C.Structure):
    _fields_ = [('loaded', C.c_uint32), ('tex0', Q), ('st', Q), ('rgbaq', Q), ('xyzf', Q)]


SHIM = r'''
#include "game/em_vu1_face_morph.h"
#include "game/em_opening_face.h"

int run(int cont, EmVu1FaceState *s, EmVu1ObjQword *m, uint32_t top, EmVu1FaceBatch *b)
{ return cont ? em_vu1_face_morph_mscnt(s, m, top, b) : em_vu1_face_morph_mscal(s, m, top, b); }
int decode(const EmVu1ObjQword *m, uint32_t kick, EmVu1ObjGsVertex *v, uint32_t *prim, uint32_t *regs)
{ return em_vu1_object_kernel_decode(m, kick, v, prim, regs); }
uint32_t triangles(const EmVu1ObjGsVertex *v, int n, uint32_t prim, uint8_t *last)
{ return em_vu1_object_kernel_triangles(v, n, prim, last); }
uint32_t top_of(uint32_t block) { return em_vu1_face_morph_top(block); }
unsigned sizes(void) { return (unsigned)(sizeof(EmVu1FaceState) | sizeof(EmVu1FaceBatch) << 12 | sizeof(EmVu1ObjGsVertex) << 24); }

/* The exact morph of one vertex (w = dmem 1011.xyzw, 1012.xyz). */
uint32_t position(const uint32_t *w, const uint32_t *base, const uint32_t *delta, uint32_t *out)
{ return em_vu1_face_morph_position(w, base, (const uint32_t (*)[3])delta, out); }
/* The port's host-float morph (em_opening_face.c) over the same words. */
void host_position(const float *w, const float *base, const float *delta, float *out)
{ em_opening_face_position(out, base, delta, w); }
'''


def native_library(path_header=None, tag='native'):
    OUT.mkdir(parents=True, exist_ok=True)
    src, lib_path = OUT / f'{tag}.c', OUT / f'{tag}.dylib'
    text = SHIM
    if path_header:
        text = SHIM.replace('#include "game/em_vu1_face_morph.h"', f'#include "{path_header}"')
    src.write_text(text)
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared',
                    '-fPIC', '-I' + str(ROOT / 'src'), str(src), str(ROOT / 'src/game/em_opening_face.c'),
                    '-o', str(lib_path)], check=True)
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
    lib.position.argtypes = [C.POINTER(C.c_uint32)] * 4
    lib.position.restype = C.c_uint32
    lib.host_position.argtypes = [C.POINTER(C.c_float)] * 4
    s = lib.sizes()
    if (s & 0xFFF, s >> 12 & 0xFFF, s >> 24) != (C.sizeof(State), C.sizeof(Batch), C.sizeof(GsVertex)):
        fail('ctypes layout differs from the header')
    return lib


def words(mem, q, n=4): return struct.unpack_from(f'<{n}I', mem, 16 * (q & 1023))


def exact_position(lib, w7, base, delta21):
    out = (C.c_uint32 * 3)()
    bad = lib.position((C.c_uint32 * 7)(*w7), (C.c_uint32 * 3)(*base), (C.c_uint32 * 21)(*delta21), out)
    return tuple(out), bad


def host_position(lib, w8, base, delta21):
    """em_opening_face_position over the same words, as bit patterns."""
    f = lambda ws: struct.unpack(f'<{len(ws)}f', struct.pack(f'<{len(ws)}I', *ws))
    out = (C.c_float * 3)()
    lib.host_position((C.c_float * 8)(*f(w8)), (C.c_float * 3)(*f(base)), (C.c_float * 21)(*f(delta21)), out)
    return struct.unpack('<3I', struct.pack('<3f', *out))


def vertex_inputs(mem, top, i):
    v = top + VERTEX_QWORDS * i
    base = words(mem, v + 3)[:3]
    delta = [x for j in range(7) for x in words(mem, v + 4 + j)[:3]]
    return base, delta


# ------------------------------------------------------------ replay ------
class Mark(tuple):
    """(stream offset, face unit CALL tag or None, stale, upload kind)."""


class Replay:
    """VIF1 + VU1 data memory for one DMA stream. Face-program batches run
    through the native header (always) and the oracle (chosen batches).
    `known[q]`: 1 = the value came from an upload the replay saw (src[q]
    names it), 2 = this program wrote it, 0 = unknown (after any other
    program's batch nothing is known)."""

    def __init__(self, elf, lib, program, seed, stats, name, choose):
        self.lib, self.program, self.stats, self.name, self.choose = lib, program, stats, name, choose
        self.vu = FaceOracle(elf)
        self.known = bytearray(1024)
        self.src = [None] * 1024
        self.base = self.offset = self.tops = self.dbf = 0
        self.cl = self.wl = 1
        self.ours = False                  # the last batch ran this program
        self.block = 0
        self.state = State()
        self.rng = random.Random(seed)
        self.census = collections.Counter()
        self.recipes = collections.Counter()
        self.tex0 = collections.Counter()
        self.consumer = collections.Counter()
        self.units = collections.defaultdict(collections.Counter)   # CALL tag -> vertices, drawing, ...
        self.synthetic = False
        self.tame = False

    def feed(self, data, where):
        """Replay a whole VIF1 stream; where(offset) gives its Mark."""
        i, n = 0, len(data)
        while i + 4 <= n:
            v = struct.unpack_from('<I', data, i)[0]; i += 4
            cmd, num, imm = (v >> 24) & 0x7F, (v >> 16) & 0xFF, v & 0xFFFF
            if cmd >= 0x60:
                vn, vl, cnt = (cmd >> 2) & 3, cmd & 3, num or 256
                size = ((32 >> vl) * (vn + 1) * cnt + 31) // 32 * 4
                dst = (imm & 0x3FF) + (self.tops if imm & 0x8000 else 0)
                plain = vn == 3 and vl == 0 and not cmd & 0x10 and self.wl <= self.cl
                kind = f'{where(i - 4)[3]} UNPACK {cnt}@{"TOPS+" if imm & 0x8000 else ""}{imm & 0x3FF:#x}'
                for k in range(cnt):
                    d = (dst + ((k // self.wl) * self.cl + k % self.wl if self.wl <= self.cl else k)) & 1023
                    if plain:
                        self.vu.mem[16 * d:16 * d + 16] = data[i + 16 * k:i + 16 * k + 16]
                        self.known[d], self.src[d] = 1, kind
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
            if cmd in (0x14, 0x15, 0x17): self.kick(cmd, imm, where(i - 4)); continue
            if cmd in (0x00, 0x04, 0x05, 0x06, 0x07, 0x10, 0x11, 0x13): continue
            fail(f'{self.name}: VIF code {v:#010x}')

    def kick(self, cmd, imm, mark):
        top = self.tops
        self.dbf ^= 1
        self.tops = self.base + (self.offset if self.dbf else 0)
        loaded = bytes(self.vu.code[:8 * INSTRUCTIONS]) == self.program
        mscal = cmd != 0x17
        ours = loaded and (imm == 0 if mscal else self.ours)
        if loaded and mscal and imm: self.stats['foreign_entry_with_face_loaded'] += 1
        if not ours:
            if mark[1] is not None: self.stats['face_unit_batch_not_face_program'] += 1
            self.ours = False
            self.known[:] = bytes(1024)
            self.stats['other_program_batches'] += 1
            return
        if mark[1] is None and not self.synthetic: self.stats['face_program_outside_face_unit'] += 1
        self.ours = True
        self.batch(mscal, top, mark)

    def live(self, before, top):
        """The data-memory qwords whose value the program reads live."""
        out = [(top + k) & 1023 for k in range(VERTEX_QWORDS * 32)] + [1011, 1012, 1013, 1014, 1015, 1016,
                                                                     1020, 1021, 1022, 1023]
        rows = {u32(before, 16 * ((top + VERTEX_QWORDS * i + 3) & 1023) + 12) & 0xFFFF for i in range(32)}
        return out + [(w + k) & 1023 for w in rows for k in range(7)], rows

    def recipe(self, before, top, rows):
        """Which upload each group of live inputs came from."""
        def kinds(qs): return '|'.join(sorted({str(self.src[q & 1023]) if self.known[q & 1023] == 1
                                               else 'unknown' for q in qs}))
        return (('weights 1011..1012', kinds([1011, 1012])), ('colour 1013..1016', kinds(range(1013, 1017))),
                ('rows', kinds([w + k for w in rows for k in range(7)])),
                ('template/fog/guard 1020..1023', kinds(range(1020, 1024))),
                ('vertices', kinds(range(top, top + VERTEX_QWORDS * 32))))

    def batch(self, mscal, top, mark):
        st, vu = self.stats, self.vu
        unit, stale = mark[1], mark[2]
        before = bytes(vu.mem)
        self.block = 0 if mscal else self.block + 1
        live, rows = self.live(before, top)
        unknown = 0 if self.synthetic else sum(1 for a in set(live) if self.known[a] != 1)
        if not self.synthetic and self.lib.top_of(self.block) != top: st['top_not_block_rule'] += 1
        st['batches'] += 1
        st['batches_mscal' if mscal else 'batches_mscnt'] += 1
        if unknown:
            st['batches_with_input_not_uploaded'] += 1
            if not stale: st['input_not_uploaded_outside_stale_units'] += 1
        if not self.synthetic:
            row = lambda q: words(before, q)
            tag = row(1020)
            self.census[('template', tag[0] | tag[1] << 32, tag[2] | tag[3] << 32)] += 1
            self.census[('fog',) + row(1021)] += 1
            self.census[('guard',) + row(1022) + row(1023)] += 1
            self.census[('ambient_w', row(1016)[3])] += 1
            self.census[('weights',) + row(1011) + row(1012)] += 1
            for w in rows: self.census[('row_word', w & 0x7FFF)] += 1
            self.census[('adc_data_bits', sum(1 for i in range(32) if words(before, top + 11 * i + 3)[3] & 0x8000))] += 1
            if mscal: self.recipes[(('stale', stale),) + self.recipe(before, top, rows)] += 1
        compare = self.choose(unit, self.block)
        if mscal or compare:
            vu.randomise(self.rng)
            if self.tame:  # the overlap style reads these back as rows: keep them small
                for _, reg in CARRIED: vu.v[reg] = [rbits(self.rng, 0.5, 1) for _ in range(4)]
            if mscal:      # unknown registers at an MSCAL: the same random words for both
                for field, reg in CARRIED: getattr(self.state, field).w[:] = vu.v[reg]
            else:          # a sampled MSCNT: the carried registers are the native state's
                for field, reg in CARRIED: vu.v[reg] = list(getattr(self.state, field).w)
        state_before = bytes(self.state)
        mem, b = DMEM.from_buffer_copy(before), Batch()
        rc = self.lib.run(0 if mscal else 1, C.byref(self.state), mem, top, C.byref(b))
        if rc or b.fault:
            fail(f'{self.name}: native fault {b.fault} at vertex {b.fault_vertex} of a '
                 f'{"MSCAL" if mscal else "MSCNT"} batch at top {top:#x}')
        after = bytes(mem)
        if compare:
            vu.kicks, vu.events, vu.top, vu.positions, vu.clips, vu.flag_tests = [], [], top, [], [], []
            vu.watch = {MICRO_FLAG_TEST, MICRO_ADC, MICRO_EXIT, MICRO_RESUME}
            vu.flag_gap.clear(); vu.q_gap.clear()
            vu.run(0 if mscal else MICRO_RESUME)
            if vu.resume != MICRO_RESUME: fail(f'{self.name}: the program ended to resume at {vu.resume:#x}')
            if sum(vu.flag_gap.values()) != 32 or sum(vu.q_gap.values()) != 32:
                fail(f'{self.name}: {dict(vu.flag_gap)} clip-flag tests, {dict(vu.q_gap)} Q multiplies')
            for gap, k in vu.flag_gap.items(): st[f'clip_to_flag_test_cycles_{gap}'] += k
            for gap, k in vu.q_gap.items(): st[f'div_to_q_multiply_cycles_{gap}'] += k
            kicks = [e for e in vu.events if e[0] == 'kick']
            if len(kicks) != 1 or kicks[0][1] != b.kick:
                fail(f'{self.name}: kicks {[(hex(k[1]), len(k[3])) for k in kicks]} native {b.kick:#x}')
            raw = sh.gif_raw(after, b.kick)
            if raw != kicks[0][3]: fail(f'{self.name}: kicked packet differs at top {top:#x}')
            if bytes(vu.mem) != after:
                diff = [q for q in range(1024) if vu.mem[16 * q:16 * q + 16] != after[16 * q:16 * q + 16]]
                fail(f'{self.name}: data memory differs at qwords {[hex(q) for q in diff[:8]]} (top {top:#x})')
            for field, reg in CARRIED:
                if list(getattr(self.state, field).w) != vu.v[reg]:
                    fail(f'{self.name}: carried register {field} differs after the batch')
            w7 = words(before, 1011) + words(before, 1012)[:3]
            if len(vu.positions) != 32: fail(f'{self.name}: {len(vu.positions)} morphs')
            for i in range(32):
                base, delta = vertex_inputs(before, top, i)
                p, bad = exact_position(self.lib, w7, base, delta)
                if bad or p != vu.positions[i]:
                    fail(f'{self.name}: morphed position of vertex {i} differs (top {top:#x})')
            st['compared_batches'] += 1
            st['compared_packet_bytes'] += len(raw)
            st['compared_dmem_bytes'] += 16384
            st['compared_positions'] += 32
            st['positions_negative_zero_lanes'] += sum(1 for q in vu.positions for x in q if x == SIGN)
            for i in range(32):
                rgbaq = words(after, b.kick + 3 + 4 * i)
                st['rgbaq_negative_zero_lanes'] += sum(1 for x in rgbaq if x == SIGN)
                st['rgbaq_positive_zero_lanes'] += sum(1 for x in rgbaq if x == 0)
            if stale: st['compared_batches_stale_units'] += 1
            if unknown == 0: st['compared_batches_all_inputs_uploaded'] += 1
            adc = sum(1 for e in vu.events if e[0] == 'pc' and e[1] == MICRO_ADC)
            st['branch_adc_add_reached'] += adc
            st['branch_adc_add_skipped'] += 32 - adc
            st['loop_exits'] += sum(1 for e in vu.events if e[0] == 'pc' and e[1] == MICRO_EXIT)
            st['resumes'] += sum(1 for e in vu.events if e[0] == 'pc' and e[1] == MICRO_RESUME)
            self.why_against_oracle(b)
            if not self.synthetic: self.consumers(before, top, w7)
        else:
            vu.mem[:] = after
            st['native_only_batches'] += 1
        for k in range(STORE, KICK + 1 + 128): self.known[(top + k) & 1023] = 2
        st['ftoi_saturated_lanes'] += b.ftoi_saturated
        self.why_against_packet(before, after, top, b)
        u = self.units[unit]
        for i in range(32):
            u['vertices'] += 1
            if not b.why[i]: u['drawing'] += 1
            if b.why[i] & ADC_CLIP: u['adc_clip'] += 1
            if b.why[i] & ADC_DATA: u['adc_data'] += 1
            st['vertices'] += 1
            if b.saturated >> i & 1:
                st['saturated_vertices'] += 1
                if not b.why[i]: st['saturated_on_drawing_vertex'] += 1
            if b.why[i] & 1: st['adc_data'] += 1
            if b.why[i] & 2: st['adc_clip'] += 1
            if not b.why[i]: st['vertices_kicked_drawing'] += 1
        if compare or FULL or self.synthetic: self.checks(before, top, after, b, state_before)
        return b

    def why_against_oracle(self, b):
        """The per-vertex record against the original program (compared
        batches): why[i] != 0 exactly when the program reaches the ADC add
        for vertex i; the CLIP bit of why[i] is the result of the program's
        clip-flag test; clip[i] is the CLIP's six bits."""
        st = self.stats
        reached = []
        for e in self.vu.events:
            if e[0] != 'pc': continue
            if e[1] == MICRO_FLAG_TEST: reached.append(False)
            elif e[1] == MICRO_ADC:
                if not reached or reached[-1]: fail(f'{self.name}: an ADC add outside a vertex')
                reached[-1] = True
        if len(reached) != 32 or len(self.vu.flag_tests) != 32 or len(self.vu.clips) != 32:
            fail(f'{self.name}: {len(reached)} flag tests, {len(self.vu.clips)} CLIPs')
        for i in range(32):
            if bool(b.why[i]) != reached[i]:
                fail(f'{self.name}: why[{i}] = {b.why[i]}, the program {"adds" if reached[i] else "skips"} the ADC')
            if bool(b.why[i] & ADC_CLIP) != self.vu.flag_tests[i]:
                fail(f'{self.name}: why[{i}] clip bit differs from the clip-flag test')
            if b.clip[i] != self.vu.clips[i]:
                fail(f'{self.name}: clip[{i}] = {b.clip[i]:#x}, the CLIP gives {self.vu.clips[i]:#x}')
        st['why_checked_against_oracle'] += 32

    def why_against_packet(self, before, after, top, b):
        """The per-vertex record on every replayed batch: only the two
        EM_VU1_OBJ_ADC_* bits; the data bit is bit 15 of the vertex's data
        word; and, with the fog row's x = 255 and y = 2048 (every capture),
        why[i] != 0 exactly when the kicked XYZF2 of vertex i carries the
        ADC bit (bit 111; fog is 0..255 before the ADC add, 2048..2303
        after it)."""
        st = self.stats
        fog = words(before, 1021)
        standard = fog[0] == FOG_X_255 and fog[1] == FOG_Y_2048
        for i in range(32):
            w = b.why[i]
            if w & ~(ADC_DATA | ADC_CLIP): fail(f'{self.name}: why[{i}] = {w:#x}')
            if bool(w & ADC_DATA) != bool(words(before, top + VERTEX_QWORDS * i + 3)[3] & 0x8000):
                fail(f'{self.name}: why[{i}] data bit differs from the data word (top {top:#x})')
            if standard:
                if bool(w) != bool(words(after, b.kick + 4 + 4 * i)[3] >> 15 & 1):
                    fail(f'{self.name}: why[{i}] = {w} but the kicked ADC bit differs (top {top:#x})')
                st['why_checked_against_kicked_adc'] += 1
        st['why_checked_against_data_word'] += 32

    def consumers(self, before, top, w7):
        """em_opening_face_position against the exact morph (E)."""
        w8 = words(before, 1011) + words(before, 1012)
        for i in range(32):
            base, delta = vertex_inputs(before, top, i)
            p, _ = exact_position(self.lib, w7, base, delta)
            h = host_position(self.lib, w8, base, delta)
            self.consumer['vertices'] += 1
            lanes = sum(1 for a, c in zip(p, h) if a != c)
            self.consumer['lanes_differ'] += lanes
            if lanes: self.consumer['vertices_differ'] += 1
            for a, c in zip(p, h):
                if a != c and (a ^ c) >> 31 == 0: self.consumer[f'ulp_{min(abs(a - c), 3)}'] += 1
                elif a != c: self.consumer['sign_differs'] += 1

    def checks(self, before, top, after, b, state_before):
        """D (GS decode, triangles) and E (TEX0/ST) on one batch."""
        st = self.stats
        where = f'{self.name} top {top:#x}'
        n, prim, regs, verts = ok.decode_check(self.lib, after, b.kick, where)
        if n < 0:
            if self.synthetic: st['packets_refused'] += 1; return
            fail(f'{self.name}: the kicked tag is none the decode accepts')
        st[f'packet_prim_{prim:#05x}_regs_{regs}_nloop_{n}'] += 1
        st['decoded_vertices'] += n
        tris = ok.triangle_check(self.lib, verts, n, prim, where)
        if tris is None:
            if self.synthetic: return
            fail(f'{self.name}: PRIM {prim:#x} is not a triangle strip')
        st['triangles'] += len(tris)
        if regs != 15: return
        carried_w = struct.unpack_from('<I', state_before, State.st.offset + 12)[0]
        for i in range(n):
            vin = 16 * ((top + VERTEX_QWORDS * i) & 1023)
            kq = 16 * ((b.kick + 1 + 4 * i) & 1023)
            if after[kq:kq + 16] != before[vin:vin + 16]: fail(f'{where}: TEX0 qword {i} not the input')
            s_in, t_in, z_in = words(before, top + VERTEX_QWORDS * i + 1)[:3]
            S, T, Qw, Ww = words(after, b.kick + 2 + 4 * i)
            if Ww != carried_w: fail(f'{where}: ST w lane {i} is not the carried register')
            if z_in == ONE:
                st['st_z_is_one'] += 1
                if S != ok.mul(s_in, Qw) or T != ok.mul(t_in, Qw): fail(f'{where}: ST {i} is not (s, t) x Q')
            else:
                st['st_z_not_one'] += 1
            if not b.why[i] and not self.synthetic: self.tex0[verts[i].tex0 & ~(7 << 61) & (2**64 - 1)] += 1
        for i in tris if not self.synthetic else ():
            t3 = {verts[i - 2].tex0, verts[i - 1].tex0, verts[i].tex0}
            if len(t3) > 1:
                st['triangles_tex0_differ_in_cld_only' if len({t & ~(7 << 61) for t in t3}) == 1
                   else 'triangles_tex0_differ_beyond_cld'] += 1


# ------------------------------------------------------------ B. lists ----
def captures():
    out = [(f'startup/{n}', REF / n) for n in lm.CAPTURES]
    if (REF / 'status-hub/eeMemory.bin').exists(): out.append(('startup/status-hub', REF / 'status-hub/eeMemory.bin'))
    if ROUTE.exists():
        out += [(f'route/{p.name}', p / 'eeMemory.bin') for p in sorted(ROUTE.iterdir())
                if in_scope_beat(p.name) and (p / 'eeMemory.bin').exists()]
    return out


RUN = {}


def quick_block(block): return block in (0, 1, 2) or block % 16 == 9


def upload_kind(tid, addr):
    if tid != 3: return {1: 'CNT', 2: 'NEXT', 0: 'REFE', 4: 'REFS'}.get(tid, f'tag{tid}')
    if SKIN_RECORDS <= addr < SKIN_RECORDS + 0x80 * 64 and (addr - SKIN_RECORDS) % 0x80 == 0: return 'REF skin record'
    if addr - 0x40 in FACES: return f'REF {FACES[addr - 0x40]} face'
    return 'REF other'


def list_job(item):
    """Replay one list; returns its stats, census and face CALLs."""
    name, path, head = item
    elf, lib, packet = RUN['elf'], RUN['lib'], RUN['packet']
    ram = path.read_bytes()
    lo, hi = packet['range']
    if ram[lo:hi] != elf[lo - 0x100000 + 0x300:hi - 0x100000 + 0x300]:
        fail(f'{name}: the face packet in RAM differs from the ELF')
    stats = collections.Counter()
    cursor = u32(ram, u32(ram, 0x275670) + 0x10)
    stale = lambda tag: cursor <= tag < cursor + 0x20000
    stream, marks, calls = bytearray(), [], []   # calls: [CALL tag, face name, stale]
    unit = None
    after_face = collections.Counter()      # the CALL that follows each face unit
    for tag, tid, qwc, addr, data in lm.dma_walk(ram, head):
        if tid == 5:
            if unit: after_face[f'{addr:#010x}'] += 1
            unit = tag if addr == FACE_KERNEL else None
            if unit: calls.append([tag, None, stale(tag)])
        if tid == 3 and unit and calls[-1][1] is None and addr - 0x40 in FACES:
            calls[-1][1] = FACES[addr - 0x40]
        if qwc:
            marks.append((len(stream), unit, unit is not None and stale(unit), upload_kind(tid, addr)))
            stream += ram[data:data + 16 * qwc]
    starts = [m[0] for m in marks]

    def where(offset): return marks[bisect.bisect_right(starts, offset) - 1]
    choose = (lambda unit, block: unit is not None) if FULL else \
        (lambda unit, block: unit is not None and quick_block(block))
    rp = Replay(elf, lib, packet['code'], seed_of(name, head), stats, f'{name}@{head:#x}', choose)
    rp.feed(bytes(stream), where)
    if set(rp.units) - {c[0] for c in calls}: fail(f'{name}@{head:#x}: a face batch outside a face CALL')
    units = [dict(call=f'{tag:#010x}', face=face, stale=s, **{k: rp.units[tag][k] for k in
                                                              ('vertices', 'drawing', 'adc_clip', 'adc_data')})
             for tag, face, s in calls]
    return dict(stats=dict(stats), census=list(rp.census.items()), recipes=list(rp.recipes.items()),
                tex0=dict(rp.tex0), consumer=dict(rp.consumer), calls=[tuple(c[1:]) for c in calls],
                units=units, name=name, head=head, after_face=dict(after_face))


# ------------------------------------------------------------ C. synthetic
def rbits(rng, lo, hi): return sh.bits(sh.fp(rng.uniform(lo, hi)))
finite = ok.finite
SPECIAL = ok.SPECIAL


def synthetic_dmem(rng, style, top):
    mem = bytearray(struct.pack('<4096I', *[finite(rng) for _ in range(4096)]))
    def put(q, ws): struct.pack_into('<4I', mem, 16 * (q & 1023), *[w & M32 for w in ws])
    def f(v): return sh.bits(sh.fp(v))
    used = {(top + k) & 1023 for k in range(KICK + 1 + 128)} | set(range(1011, 1024))
    nodes = 1 + rng.randrange(6)
    rowbase = None
    for _ in range(4000):
        r = rng.randrange(1024 - 8 * nodes)
        if not {r + k for k in range(8 * nodes)} & used: rowbase = r; break
    if rowbase is None: nodes, rowbase = 1, next(r for r in range(1017) if not {r + k for k in range(8)} & used)
    scale = {'huge': 1e30, 'tiny': 1e-30}.get(style, 1.0)
    lo = 0.0 if style == 'overlap' else -1.0
    for n in range(nodes):
        a = rowbase + 8 * n
        for r in range(4):       # screen-space rows: x and y lanes about 2048 x w
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
        put(a + 7, [finite(rng) for _ in range(4)])
    for r in range(3): put(1013 + r, [rbits(rng, 0, 200) for _ in range(3)] + [rbits(rng, 0, 50)])
    put(1016, [f(8388608.0 + rng.uniform(0, 120)) for _ in range(3)] + [f(8388608.0) if rng.random() < .5 else f(8388608.0 + 128)])
    wts = [rbits(rng, 0, 1) if rng.random() < .6 else 0 for _ in range(8)]
    if style == 'weights': wts = [rng.choice([rbits(rng, -40, 40), 0, 0x80000000, 1, ONE, SIGN | ONE, 0x7F7FFFFF])
                                  for _ in range(8)]
    if style == 'special': wts = [rng.choice(SPECIAL) if rng.random() < .3 else w for w in wts]
    if style == 'dead255': wts[7] = 0x7FC00000
    if style == 'signzero':     # negative, -0, +0 and positive weights
        wts = [rng.choice([rbits(rng, -2, -1e-3), SIGN, 0, rbits(rng, 1e-3, 2)]) for _ in range(8)]
    put(1011, wts[:4]); put(1012, wts[4:])
    tag = 0x8000 | 32 | 1 << 46 | 0x03C << 47 | 4 << 60
    if style == 'nloop': tag = (tag & ~0x7FFF) | rng.randrange(1, 33)
    put(1020, [tag & M32, tag >> 32, 0x4126, 0])
    fogoff = style == 'fogoff'
    put(1021, [f(255.0), f(2048.0), f(255.0) if fogoff else rbits(rng, -600, 600), 0 if fogoff else rbits(rng, -3, 3)])
    if style == 'fogrow':
        put(1021, [rbits(rng, -100, 400), rbits(rng, -5000, 5000), rbits(rng, -600, 600), rbits(rng, -3, 3)])
    put(1022, [0x3A008081, 0x3A008081, 0x34008080, 0])
    put(1023, [0xBF808081, 0xBF808081, 0, ONE])
    if style == 'guardrow':
        # all eight guard lanes random (the captures hold one pair, with
        # x = y, scale.w = 0 and offset.w = 1.0). g.w/c.w = sw + ow = Kw;
        # g.x/c.w and g.y/c.w sit about +-Kw (independent x and y lanes),
        # moved per vertex by the scale times the row offsets, so both CLIP
        # outcomes occur within a batch; and g.w != c.w
        sw = rng.choice((1, -1)) * rng.uniform(0.05, 0.6)
        sign = -1 if rng.random() < .25 else 1
        ow = sign * rng.uniform(0.4, 1.6)
        while abs(ow - 1.0) < 0.02 or abs(sw + ow) < 0.2: ow = sign * rng.uniform(0.4, 1.6)
        kw = abs(sw + ow)
        sc, of = [], []
        for _ in range(2):      # x, y
            s = rng.choice((1, -1)) * kw * rng.uniform(0.002, 0.02)
            sc.append(s); of.append(rng.choice((1, -1)) * kw * rng.uniform(0.4, 1.0) - 2048 * s)
        sc.append(rng.choice((1, -1)) * kw * rng.uniform(1e-7, 1e-4)); of.append(rng.uniform(-0.5, 0.5) * kw)
        sc.append(sw); of.append(ow)
        # a fog row that leaves A + B * w mostly inside 0..255 (B * c.w and
        # B * g.w then differ in the kicked fog)
        put(1021, [f(255.0), f(2048.0), rbits(rng, 60, 190), rbits(rng, -0.15, 0.15)])
        put(1022, [f(v) for v in sc]); put(1023, [f(v) for v in of])
    if style == 'zerocolour':   # colour and ambient rows all signed zeros
        # per lane: all -0, all +0 or mixed (a sum of zeros is -0 only when
        # every term is)
        mode = [rng.randrange(3) for _ in range(4)]
        for r in range(4):
            put(1013 + r, [(SIGN, 0, rng.choice((0, SIGN)))[mode[x]] for x in range(4)])
    if style == 'edge':         # identity rows and guard: g = c = (p, 1)
        put(1022, [ONE, ONE, ONE, 0]); put(1023, [0, 0, 0, ONE])
        for n in range(nodes):
            for r in range(4): put(rowbase + 8 * n + r, [ONE if k == r else 0 for k in range(4)])
    outputs = {(top + k) & 1023 for k in range(STORE, KICK + 1 + 128)}
    for i in range(32):
        v = top + VERTEX_QWORDS * i
        slot = rowbase + 8 * rng.randrange(nodes)
        if style == 'wild' and rng.random() < .3:
            slot = rng.randrange(1024)
            while {(slot + k) & 1023 for k in range(7)} & outputs: slot = rng.randrange(1024)
        # rows over the store slots (TOP + 0x160..0x166): vertex 0 reads them
        # before any store, vertex 1 after the carried stores (its L rows still
        # the old values), vertices 2.. after vertex 0's outputs too
        if style == 'overlap' and (i < 3 or rng.random() < .3): slot = (top + STORE) & 1023
        word = slot | (0x8000 if rng.random() < .15 else 0) | (rng.getrandbits(4) << 10 if style == 'wild' else 0)
        wf = sh.bits(-1.0 if rng.random() < .5 else 1.0) & 0xFFFF0000 | word
        pos = [rbits(rng, 0 if style == 'overlap' else -60, 60) for _ in range(3)]
        if style == 'special': pos = [rng.choice(SPECIAL) if rng.random() < .3 else p for p in pos]
        if style == 'guard': pos = [rbits(rng, -6000, 6000) for _ in range(3)]
        if style == 'edge':
            pos = [rng.choice([ONE, ONE | SIGN, ONE - 1, 0x3F000000]) if rng.random() < .97
                   else rng.choice([ONE + 1, (ONE + 1) | SIGN]) for _ in range(3)]
        if style == 'signzero': pos = [rng.choice((0, SIGN)) for _ in range(3)]
        put(v + 3, pos + [wf])
        for j in range(7):
            dl = [rbits(rng, 0 if style == 'overlap' else -3, 3) for _ in range(3)] + [finite(rng)]
            if style == 'edge': dl = [0, 0, 0, dl[3]]
            if style == 'signzero':
                # zero deltas signed mostly so that D x w is -0: with a -0
                # base the whole sum is -0 (the first product is not added
                # to a +0)
                neg = [(wts[j] ^ (0 if rng.random() < .1 else SIGN)) & SIGN for _ in range(3)]
                dl = neg + [dl[3]]
            if style == 'special': dl = [rng.choice(SPECIAL) if rng.random() < .2 else x for x in dl]
            if style == 'dead255': dl[3] = 0x7F800000 | rng.getrandbits(23) | (SIGN if rng.random() < .5 else 0)
            put(v + 4 + j, dl)
        nrm = [rbits(rng, -1, 1) for _ in range(3)] + [finite(rng)]
        if style == 'special': nrm = [rng.choice(SPECIAL) if rng.random() < .3 else p for p in nrm]
        if style == 'dead255': nrm[3] = 0xFF800000
        put(v + 2, nrm)
        stq = [rbits(rng, -4, 4), rbits(rng, -4, 4), ONE if rng.random() < .8 else rbits(rng, -2, 2),
               0x7FC00000 if style == 'dead255' else 0]
        put(v + 1, stq)
        put(v, [rbits(rng, 0.5, 1) for _ in range(4)] if style == 'overlap' else [finite(rng) for _ in range(4)])
    if style == 'tail':
        # TOP + 0x1E0 = dmem 1020: vertex 31's TEX0 output lands on the
        # template slot after the constants were read, so the kicked tag is
        # that qword (a valid tag with NLOOP 31 here): the template must be
        # read after the last vertex's stores
        t31 = 0x8000 | 31 | 1 << 46 | 0x03C << 47 | 4 << 60
        put(top + VERTEX_QWORDS * 31, [t31 & M32, t31 >> 32, 0x4126, 0])
    return mem


TAIL_TOP = 1020 - STORE - 4 * 32            # the last vertex's TEX0 store lands on dmem 1020
STYLES = ('plain', 'fogoff', 'wzero', 'negw', 'special', 'guard', 'huge', 'tiny', 'wild', 'overlap',
          'dead255', 'nloop', 'mscnt', 'edge', 'fogrow', 'weights', 'tail', 'guardrow', 'signzero',
          'zerocolour')


def synthetic_job(item):
    style, seed = item
    rng = random.Random(seed)
    stats = collections.Counter()
    rp = Replay(RUN['elf'], RUN['lib'], RUN['packet']['code'], seed, stats, f'synthetic {style} {seed}',
                lambda unit, block: True)
    rp.synthetic = True
    rp.tame = style == 'overlap'
    rp.vu.code[:8 * INSTRUCTIONS] = RUN['packet']['code']
    # the double buffer, anywhere below the constants (inputs and outputs
    # never wrap onto dmem 1011..1023, so the template stays a template)
    tops = [BASE, BASE + OFFSET, 0, 0x20F, rng.randrange(0, 0x210)]
    top = TAIL_TOP if style == 'tail' else tops[seed % len(tops)]
    rp.vu.mem[:] = synthetic_dmem(rng, style, top)
    rp.known[:] = b'\1' * 1024
    mark = (0, 'synthetic', False, 'synthetic')
    rp.batch(True, top, mark)
    if style == 'mscnt':
        # MSCNT restarts the program: new weights, colour, fog and guard rows
        # in data memory DO reach the batch
        for k in range(3):
            top2 = (BASE, BASE + OFFSET)[(k + 1) & 1]
            fresh = synthetic_dmem(rng, 'plain', top2)
            for q in list(range(top2, top2 + VERTEX_QWORDS * 32)) + list(range(1011, 1024)):
                q &= 1023
                rp.vu.mem[16 * q:16 * q + 16] = fresh[16 * q:16 * q + 16]
            rows = {u32(rp.vu.mem, 16 * (top2 + 11 * i + 3) + 12) & 0x3FF for i in range(32)}
            for w in rows:
                for kq in range(7): rp.vu.mem[16 * ((w + kq) & 1023):16 * ((w + kq) & 1023) + 16] = \
                    fresh[16 * ((w + kq) & 1023):16 * ((w + kq) & 1023) + 16]
            rp.batch(False, top2, mark)
    stats[f'style_{style}_vertices'] = stats['vertices']
    stats[f'style_{style}_adc_clip'] = stats['adc_clip']
    return dict(stats)


def fault_cases(lib):
    """An exponent-255 word in a live lane faults the translation; the same
    word in a dead lane does not (synthetic style dead255)."""
    rng = random.Random(0x255)
    t = BASE
    live = [('base x', lambda m, w: struct.pack_into('<I', m, 16 * (t + 3), 0x7F800000)),
            ('delta 3 y', lambda m, w: struct.pack_into('<I', m, 16 * (t + 7) + 4, 0xFF800000)),
            ('delta 6 z', lambda m, w: struct.pack_into('<I', m, 16 * (t + 10) + 8, 0x7FC00000)),
            ('weight 1011 w', lambda m, w: struct.pack_into('<I', m, 16 * 1011 + 12, 0x7F800000)),
            ('weight 1012 z', lambda m, w: struct.pack_into('<I', m, 16 * 1012 + 8, 0x7F800001)),
            ('normal y', lambda m, w: struct.pack_into('<I', m, 16 * (t + 2) + 4, 0xFF800000)),
            ('ST s', lambda m, w: struct.pack_into('<I', m, 16 * (t + 1), 0x7FC00000)),
            ('M row 3 w', lambda m, w: struct.pack_into('<I', m, 16 * ((w + 3) & 1023) + 12, 0x7F800001)),
            ('L row 1 z', lambda m, w: struct.pack_into('<I', m, 16 * ((w + 5) & 1023) + 8, 0x7F800000)),
            ('colour ambient w', lambda m, w: struct.pack_into('<I', m, 16 * 1016 + 12, 0x7F800000)),
            ('fog B', lambda m, w: struct.pack_into('<I', m, 16 * 1021 + 12, 0xFF800000)),
            ('guard offset w', lambda m, w: struct.pack_into('<I', m, 16 * 1023 + 12, 0x7FFFFFFF))]
    faults = 0
    for label, poke in live:
        mem = synthetic_dmem(rng, 'plain', t)
        word = u32(mem, 16 * (t + 3) + 12) & 0xFFFF
        poke(mem, word)
        st, b = State(), Batch()
        rc = lib.run(0, C.byref(st), DMEM.from_buffer_copy(mem), t, C.byref(b))
        if rc != -1 or b.fault != 2 or b.fault_vertex != 0: fail(f'exponent-255 {label}: no fault at vertex 0')
        faults += 1
    st, b = State(), Batch()
    if lib.run(1, C.byref(st), DMEM(), BASE, C.byref(b)) != -1 or b.fault != 3: fail('MSCNT before any batch')
    if lib.run(0, None, DMEM(), BASE, C.byref(b)) != -1 or b.fault != 1: fail('NULL state')
    p, bad = exact_position(lib, [0x7F800000] + [0] * 6, [0, 0, 0], [0] * 21)
    if not bad: fail('exponent-255 weight: the position helper does not report it')
    return faults + 3


# ------------------------------------------------------------ main --------
def merge(dst, src):
    for k, v in src.items(): dst[k] = dst.get(k, 0) + v


def census_report(census, recipes, calls, mscal_batches):
    """Section 2 of the doc: the face CALLs, the upload recipe, the rows."""
    got = collections.defaultdict(collections.Counter)
    for key, n in census: got[key[0]][key[1:]] += n
    who = collections.Counter((face or 'unattributed', 'stale' if s else 'live') for face, s in calls)
    templates = {(f'{lo & 0x7FFF}', f'{lo >> 46 & 1}', f'{lo >> 47 & 0x7FF:#05x}', f'{lo >> 58 & 3}', f'{lo >> 60}',
                  f'{hi & 0xFFFF:#06x}', f'{lo >> 15 & 1}'): n for (lo, hi), n in got['template'].items()}
    live_recipes = collections.Counter()
    for key, n in recipes:
        live_recipes[(key[0][1],) + tuple(v for _, v in key[1:])] += n
    rec = [dict(stale=k[0], weights=k[1], colour=k[2], rows=k[3], template_fog_guard=k[4], vertices=k[5], units=n)
           for k, n in sorted(live_recipes.items(), key=lambda kv: -kv[1])]
    weights = got['weights']
    out = dict(face_calls=len(calls), by_face={f'{a} {b}': n for (a, b), n in sorted(who.items())},
               templates=[dict(nloop=k[0], pre=k[1], prim=k[2], flg=k[3], nreg=k[4], regs=k[5], eop=k[6], batches=n)
                          for k, n in templates.items()],
               fog_rows={hashlib.sha256(struct.pack('<4I', *k)).hexdigest()[:16]:
                         dict(x_255=k[0] == 0x437F0000, y_2048=k[1] == 0x45000000, b_zero=k[3] == 0, batches=n)
                         for k, n in got['fog'].items()},
               guard_rows={hashlib.sha256(struct.pack('<8I', *k)).hexdigest()[:16]: n for k, n in got['guard'].items()},
               ambient_w={hex(k[0]): n for k, n in got['ambient_w'].items()},
               weight_rows_distinct=len(weights),
               weight_rows_all_zero=sum(n for k, n in weights.items() if not any(x & 0x7FFFFFFF for x in k)),
               weight_lane_1012_w_nonzero=sum(n for k, n in weights.items() if k[7]),
               row_words={str(k[0]): n for k, n in got['row_word'].items()},
               adc_data_bits_per_batch={str(k[0]): n for k, n in sorted(got['adc_data_bits'].items())},
               recipes=rec, mscal_batches=mscal_batches)
    want = {('Roger', 'live'): 41, ('Dennis', 'live'): 5, ('Dennis', 'stale'): 2}
    if dict(who) != want: fail(f'face CALLs {dict(who)} != the documented {want}')
    return out


# Drawing vertices per face unit (why[i] == 0), by capture: the grouping
# docs/VU1_FACE_MORPH.md section 2 states. Roger's unit draws 1,048 of its
# 1,600 vertices only in the opening, the handoff and route beats 08, 09 and
# 11..14; everywhere else (roger-encounter included) the guard band drops
# every one of them. Dennis's units (live or stale) draw 1,124 of 1,760.
ROGER_DRAWS = ('startup/opening_ee.bin', 'startup/handoff_ee.bin', 'route/08_', 'route/09_', 'route/11_',
               'route/12_', 'route/13_', 'route/14_')
UNIT_VERTICES = {'Roger': 1600, 'Dennis': 1760}


def unit_drawing_want(capture, face):
    if face == 'Dennis': return 1124
    return 1048 if any(capture.startswith(c) for c in ROGER_DRAWS) else 0


def units_report(results):
    """Per face unit: capture, list, CALL tag, face, stale, vertices,
    drawing, clip ADC and data ADC (asserted against unit_drawing_want)."""
    out, groups = [], collections.defaultdict(set)
    for r in results:
        for u in r['units']:
            if u['vertices'] != UNIT_VERTICES[u['face']]:
                fail(f"{r['name']} {u['call']}: {u['vertices']} vertices for {u['face']}")
            want = unit_drawing_want(r['name'], u['face'])
            if u['drawing'] != want:
                fail(f"{r['name']}@{r['head']:#x} {u['face']} unit {u['call']}: {u['drawing']} drawing "
                     f"vertices, the documented grouping says {want}")
            out.append(dict(capture=r['name'], list=f"{r['head']:#x}", **u))
            groups[(u['face'], u['drawing'])].add(r['name'])
    return dict(units=out, captures_by_face_and_drawing={f'{f} {d}': sorted(c) for (f, d), c in sorted(groups.items())})


def main(argv):
    started = time.time()
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    if hashlib.sha256(elf).hexdigest() != ELF_SHA256: fail('not the pinned SCUS-97112 ELF')
    packet = face_packet(elf)
    if '--defects' in argv: return defects()
    lib = native_library(os.environ.get('EM_VU1_FACE_HEADER'), os.environ.get('EM_VU1_FACE_TAG', 'native'))
    RUN.update(elf=elf, lib=lib, packet=packet)
    report = dict(elf_sha256=ELF_SHA256, program=dict(packet=hex(FACE_KERNEL), mpg=hex(PROGRAM),
                  instructions=INSTRUCTIONS, base=hex(BASE), offset=hex(OFFSET),
                  program_sha256=hashlib.sha256(packet['code']).hexdigest()))
    # 001D3E40 builds the address 0x0023C480, reached from 001CAA00 (the
    # object test's FACE_BUILDER / FACE_CHAIN)
    lo, hi = ok.FACE_BUILDER
    ws = [e32(elf, a) for a in range(lo, hi, 4)]
    if not (any(w >> 26 == 0x0F and w & 0xFFFF == (FACE_KERNEL + 0x8000) >> 16 for w in ws) and
            any(w >> 26 == 0x09 and w & 0xFFFF == FACE_KERNEL & 0xFFFF for w in ws)):
        fail('001D3E40 does not build the address 0x0023C480')
    for (a, b), callee in ok.FACE_CHAIN:
        if not any(e32(elf, x) >> 26 in (2, 3) and e32(elf, x) & 0x3FFFFFF == callee >> 2 for x in range(a, b, 4)):
            fail(f'no call to {callee:08X} in {a:08X}..{b:08X}')
    report['builder'] = dict(appends_call='001D3E40', chain='001CAA00 -> 001CB3C0 -> 001D3F50 -> 001D3E40')
    caps = captures()
    for name, path in caps:
        ram = path.read_bytes()
        if ram[0x810700] != 11: fail(f'{name}: not AREA11')
    report['captures_checked'] = len(caps)

    # B. captured lists (every list in both modes: the census is whole)
    items = [(name, path, head) for name, path in caps for head in lm.LIST_HEADS]
    results = parallel_map(list_job, items, cost=lambda i: 1)
    total, tex0, consumer = {}, collections.Counter(), {}
    calls, census, recipes = [], [], []
    for r in results:
        merge(total, r['stats']); tex0.update(r['tex0']); merge(consumer, r['consumer'])
        calls += r['calls']; census += r['census']; recipes += r['recipes']
    if not total.get('compared_batches'): fail('no captured batch compared')
    for key in ('input_not_uploaded_outside_stale_units', 'face_unit_batch_not_face_program',
                'face_program_outside_face_unit', 'foreign_entry_with_face_loaded', 'top_not_block_rule',
                'saturated_on_drawing_vertex', 'triangles_tex0_differ_beyond_cld'):
        if total.get(key, 0): fail(f'captured batches: {key} = {total[key]}')
    report['census'] = census_report(census, recipes, calls, total.get('batches_mscal', 0))
    want_batches = 41 * 50 + 7 * 55
    if total.get('batches') != want_batches:
        fail(f'{total.get("batches")} face batches replayed, the 48 CALLs hold {want_batches}')
    per_capture = collections.defaultdict(list)
    for r in results:
        per_capture[r['name']] += [f'{r["head"]:#x} {u["call"]} {u["face"]}{" stale" if u["stale"] else ""}: '
                                   f'drawing vertices {u["drawing"]} of {u["vertices"]}' for u in r['units']]
    report['B_units'] = units_report(results)
    after_face = collections.Counter()
    for r in results: after_face.update(r['after_face'])
    if f'{ok.CLIP_KERNEL:#010x}' in after_face: fail('a face unit is followed by the clip pass 0x002354A0')
    report['B_captured_lists'] = dict(lists=len(items), face_calls_by_capture={k: v for k, v in per_capture.items()},
                                      next_call_after_face_unit=dict(after_face), stats=total)

    # C. synthetic
    sitems = [(s, 2000 + 97 * k + i) for i, s in enumerate(STYLES) for k in range(pick(16, 3))]
    sres = parallel_map(synthetic_job, sitems)
    sstats = {}
    for r in sres: merge(sstats, r)
    faults = fault_cases(lib)
    for key in ('branch_adc_add_reached', 'branch_adc_add_skipped', 'loop_exits', 'resumes',
                'ftoi_saturated_lanes', 'adc_clip', 'adc_data', 'positions_negative_zero_lanes',
                'rgbaq_negative_zero_lanes', 'rgbaq_positive_zero_lanes'):
        if not sstats.get(key): fail(f'synthetic batches never reach {key}')
    # random guard rows: both CLIP outcomes
    if not 0 < sstats.get('style_guardrow_adc_clip', 0) < sstats.get('style_guardrow_vertices', 0):
        fail(f"guardrow: {sstats.get('style_guardrow_adc_clip')} of {sstats.get('style_guardrow_vertices')} vertices clipped")
    report['C_synthetic'] = dict(cases=len(sitems), stats=sstats, fault_cases=faults)

    # the ADC window i-2..i and the position's Q rest on the interpreter's
    # latencies: every compared vertex has the CLIP and the flag test exactly
    # CLIP_FLAG_LATENCY apart, and the division and the Q multiply Q_LATENCY
    gaps = collections.defaultdict(collections.Counter)
    for part_stats in (total, sstats):
        for k, v in part_stats.items():
            for kind in ('clip_to_flag_test_cycles_', 'div_to_q_multiply_cycles_'):
                if k.startswith(kind): gaps[kind][int(k[len(kind):])] += v
    if set(gaps['clip_to_flag_test_cycles_']) != {CLIP_FLAG_LATENCY}:
        fail(f'CLIP to flag-test distances {dict(gaps["clip_to_flag_test_cycles_"])}')
    if set(gaps['div_to_q_multiply_cycles_']) != {Q_LATENCY}:
        fail(f'division to Q multiply distances {dict(gaps["div_to_q_multiply_cycles_"])}')
    report['latency_windows'] = dict(
        clip_flag_latency_assumed=CLIP_FLAG_LATENCY,
        vertices_clip_to_flag_test_at_exactly_that=gaps['clip_to_flag_test_cycles_'][CLIP_FLAG_LATENCY],
        q_latency_assumed=Q_LATENCY,
        vertices_div_to_q_multiply_at_exactly_that=gaps['div_to_q_multiply_cycles_'][Q_LATENCY])

    # E. consumers and TEX0
    report['E_consumers'] = dict(em_opening_face_position=consumer,
                                 distinct_tex0=len(tex0),
                                 tex0_fields=sorted({(t >> 20 & 0x3F, t >> 34 & 1, t >> 35 & 3) for t in tex0}))
    line = banner(f"{len(calls)} face CALLs, {total['batches']:,} batches replayed, "
                  + part(total['compared_batches'], total['batches'], 'batches compared'),
                  f'{len(sitems)} synthetic cases + {faults} fault cases')
    report.update(status='PASS', mode=line, seconds=round(time.time() - started, 1))
    OUT.mkdir(parents=True, exist_ok=True)
    tag = os.environ.get('EM_VU1_FACE_TAG', 'native')
    (OUT / ('report.json' if tag == 'native' else f'{tag}_report.json')).write_text(
        json.dumps(report, indent=1, default=str) + '\n')
    summary = {k: report[k] for k in ('status', 'mode', 'seconds')}
    summary['compared'] = dict(captured=total['compared_batches'], synthetic=sstats.get('compared_batches', 0))
    summary['em_opening_face_position'] = consumer
    print(json.dumps(summary))
    return 0


# ------------------------------------------------------------ defects -----
DEFECTS = [
    ('delta order', 'acc = emvuo_madd(acc, delta[j][x], weight[j], &bad);',
     'acc = emvuo_madd(acc, delta[EM_VU1_FACE_DELTAS - j][x], weight[EM_VU1_FACE_DELTAS - j], &bad);'),
    ('base added first', 'uint32_t acc = emvuo_mul(delta[0][x], weight[0], &bad);',
     'uint32_t acc = emvuo_madd(base[x], delta[0][x], weight[0], &bad);'),
    ('six deltas', 'for (unsigned j = 1; j < EM_VU1_FACE_DELTAS; ++j)\n            acc',
     'for (unsigned j = 1; j < EM_VU1_FACE_DELTAS - 1; ++j)\n            acc'),
    ('weight 1012 w lane', 'memcpy(&k.weight[4], dmem[EM_VU1_FACE_WEIGHTS + 1u].w, 12);',
     'memcpy(&k.weight[4], &dmem[EM_VU1_FACE_WEIGHTS + 1u].w[1], 12);'),
    ('position sum order', 'acc = emvuo_madd(acc, in->m[2][x], p[2], &bad);\n        c[x] = emvuo_madd(acc, in->m[3][x], EM_EE_ONE, &bad);',
     'acc = emvuo_madd(acc, in->m[3][x], EM_EE_ONE, &bad);\n        c[x] = emvuo_madd(acc, in->m[2][x], p[2], &bad);'),
    ('lighting not clamped', 'light[x] = em_vu_max_bits(light[x], 0u);', 'light[x] = light[x];'),
    ('colour cap', 'out[2].w[x] = em_vu_min_bits(col[x], k->cap);', 'out[2].w[x] = em_vu_min_bits(col[x], k->cap - 1u);'),
    ('fog clamp order', 'fog = em_vu_min_bits(fog, k->fog[0]);', 'fog = em_vu_max_bits(fog, 0u);'),
    ('fog ADC add', 'if (w) fog = emvuo_add(fog, k->fog[1], &bad);', 'if (0) fog = emvuo_add(fog, k->fog[1], &bad);'),
    ('clip history 2 vertices', 'if (*hist & 0x03FFFFu) w |= EM_VU1_OBJ_ADC_CLIP;',
     'if (*hist & 0x000FFFu) w |= EM_VU1_OBJ_ADC_CLIP;'),
    ('data bit', 'uint32_t w = (in->word & 0x8000u)', 'uint32_t w = (in->word & 0x4000u)'),
    ('Q from z', '(void)em_vu_div_bits(EM_EE_ONE, c[3], 3, 3, &q);', '(void)em_vu_div_bits(EM_EE_ONE, c[2], 3, 3, &q);'),
    ('ST w zero', 'out[1].w[3] = st_w;', 'out[1].w[3] = st_w & 0u;'),
    ('MSCNT keeps MSCAL constants', 'memcpy(k.weight, dmem[EM_VU1_FACE_WEIGHTS].w, 16);',
     'static EmVu1FaceConst keep; static int kept; if (kept) { k = keep; goto have; }\n    memcpy(k.weight, dmem[EM_VU1_FACE_WEIGHTS].w, 16);'),
    ('first stores skipped', 'emvuo_st(dmem, o + 2u, prev[2]);                        /* 0x024..0x027 */',
     'if (i) emvuo_st(dmem, o + 2u, prev[2]);                 /* 0x024..0x027 */'),
    ('rows read after the stores',
     '        for (unsigned r = 0; r < 4; ++r)                        /* 0x01D..0x020 */\n            memcpy(cur.m[r], emvuo_ld(dmem, cur.word + r).w, 16);\n        for (unsigned r = 0; r < 3; ++r)                        /* 0x021..0x023 */\n            memcpy(cur.l[r], emvuo_ld(dmem, cur.word + 4u + r).w, 12);\n        emvuo_st(dmem, o + 2u, prev[2]);                        /* 0x024..0x027 */\n        emvuo_st(dmem, o + 3u, prev[3]);\n        emvuo_st(dmem, o + 1u, prev[1]);\n        emvuo_st(dmem, o + 0u, prev[0]);\n',
     '        emvuo_st(dmem, o + 2u, prev[2]);\n        emvuo_st(dmem, o + 3u, prev[3]);\n        emvuo_st(dmem, o + 1u, prev[1]);\n        emvuo_st(dmem, o + 0u, prev[0]);\n        for (unsigned r = 0; r < 4; ++r)\n            memcpy(cur.m[r], emvuo_ld(dmem, cur.word + r).w, 16);\n        for (unsigned r = 0; r < 3; ++r)\n            memcpy(cur.l[r], emvuo_ld(dmem, cur.word + 4u + r).w, 12);\n'),
    ('ftoi0', 'out[3].w[x] = emvuo_ftoi4(scr[x], saturated);', 'out[3].w[x] = em_vu_ftoi0_bits(scr[x]);'),
    ('kick offset', 'EM_VU1_FACE_KICK 0x163u', 'EM_VU1_FACE_KICK 0x164u'),
    ('store slot', 'EM_VU1_FACE_STORE 0x160u', 'EM_VU1_FACE_STORE 0x161u'),
    ('guard offset lane', 'k->guard_offset[x], c[3], &bad);', 'k->guard_offset[x], c[2], &bad);'),
    ('clip history kept across batches', 'uint32_t hist = 0u;', 'static uint32_t hist = 0u;'),
    ('fog times B lane', 'emvuo_mul(EM_EE_ONE, k->fog[2], &bad), k->fog[3], c[3], &bad);',
     'emvuo_mul(EM_EE_ONE, k->fog[2], &bad), k->fog[3], c[2], &bad);'),
    ('fog cap constant 255', 'fog = em_vu_min_bits(fog, k->fog[0]);', 'fog = em_vu_min_bits(fog, 0x437F0000u);'),
    ('ADC addend constant 2048', 'if (w) fog = emvuo_add(fog, k->fog[1], &bad);',
     'if (w) fog = emvuo_add(fog, 0x45000000u, &bad);'),
    ('ST from the next vertex', 'memcpy(cur.st_in, emvuo_ld(dmem, v + 1u).w, sizeof cur.st_in);',
     'memcpy(cur.st_in, emvuo_ld(dmem, v + 12u).w, sizeof cur.st_in);'),
    ('normal from qword 1', 'memcpy(in->normal, emvuo_ld(m, v + 2u).w, sizeof in->normal);',
     'memcpy(in->normal, emvuo_ld(m, v + 1u).w, sizeof in->normal);'),
    ('template read before the last stores', '    emvuo_st(dmem, out->kick, emvuo_ld(dmem, 1020u));\n    return 0;',
     '    return 0;'),
    ('XYZF2 not carried', 'EmVu1ObjQword prev[4] = { s->tex0, s->st, s->rgbaq, s->xyzf };',
     'EmVu1ObjQword prev[4] = { s->tex0, s->st, s->rgbaq, s->rgbaq };'),
    ('previous vertex Q', '(void)em_vu_div_bits(EM_EE_ONE, c[3], 3, 3, &q);',
     'static uint32_t q_prev; (void)em_vu_div_bits(EM_EE_ONE, c[3], 3, 3, &q); { uint32_t t = q; q = q_prev; q_prev = t; }'),
    ('TOP rule', 'EM_VU1_FACE_OFFSET 0x1E5u', 'EM_VU1_FACE_OFFSET 0x1E4u'),
    ('weights 0 and 1 swapped', 'memcpy(k.weight, dmem[EM_VU1_FACE_WEIGHTS].w, 16);',
     'memcpy(k.weight, dmem[EM_VU1_FACE_WEIGHTS].w, 16);\n    { uint32_t t = k.weight[0]; k.weight[0] = k.weight[1]; k.weight[1] = t; }'),
    ('last vertex not stored', '    emvuo_st(dmem, o + 2u, prev[2]);\n    emvuo_st(dmem, o + 3u, prev[3]);\n    emvuo_st(dmem, o + 1u, prev[1]);\n    emvuo_st(dmem, o + 0u, prev[0]);\n    s->tex0',
     '    (void)o;\n    s->tex0'),
    ('lighting 2 lanes', 'for (unsigned x = 0; x < 3; ++x) light[x] = em_vu_max_bits(light[x], 0u);',
     'for (unsigned x = 0; x < 2; ++x) light[x] = em_vu_max_bits(light[x], 0u);'),
    ('MSCNT state unchecked', 'if (!s->loaded) {', 'if (0) {'),
    # guard rows (synthetic style guardrow: the captures hold one pair)
    ('guard w lane skipped', '    /* 0x02A, 0x02B: fog = 1 * A + B * c.w */',
     '    g[3] = c[3];\n    /* 0x02A, 0x02B: fog = 1 * A + B * c.w */'),
    ('fog times guard w', 'emvuo_mul(EM_EE_ONE, k->fog[2], &bad), k->fog[3], c[3], &bad);',
     'emvuo_mul(EM_EE_ONE, k->fog[2], &bad), k->fog[3], g[3], &bad);'),
    ('Q from guard w',
     '    (void)em_vu_div_bits(EM_EE_ONE, c[3], 3, 3, &q);\n    for (unsigned x = 0; x < 4; ++x)\n'
     '        g[x] = emvuo_madd(emvuo_mul(k->guard_scale[x], c[x], &bad), k->guard_offset[x], c[3], &bad);',
     '    for (unsigned x = 0; x < 4; ++x)\n'
     '        g[x] = emvuo_madd(emvuo_mul(k->guard_scale[x], c[x], &bad), k->guard_offset[x], c[3], &bad);\n'
     '    (void)em_vu_div_bits(EM_EE_ONE, g[3], 3, 3, &q);'),
    ('guard y from lane x', 'g[x] = emvuo_madd(emvuo_mul(k->guard_scale[x], c[x], &bad), k->guard_offset[x], c[3], &bad);',
     'g[x] = emvuo_madd(emvuo_mul(k->guard_scale[x == 1u ? 0u : x], c[x], &bad), k->guard_offset[x == 1u ? 0u : x], c[3], &bad);'),
    # signed zeros (synthetic styles signzero and zerocolour)
    ('morph sum starts from +0', 'uint32_t acc = emvuo_mul(delta[0][x], weight[0], &bad);',
     'uint32_t acc = emvuo_madd(0u, delta[0][x], weight[0], &bad);'),
    ('lighting clamped at -0', 'for (unsigned x = 0; x < 3; ++x) light[x] = em_vu_max_bits(light[x], 0u);',
     'for (unsigned x = 0; x < 3; ++x) light[x] = em_vu_max_bits(light[x], 0x80000000u);'),
    # the per-vertex record (why[], clip[]): the kicked packet is unchanged
    ('why drops the data bit under CLIP', 'if (*hist & 0x03FFFFu) w |= EM_VU1_OBJ_ADC_CLIP;',
     'if (*hist & 0x03FFFFu) w = EM_VU1_OBJ_ADC_CLIP;'),
    ('why zeroed', '    *why = w;\n', '    *why = 0u * w;\n'),
    ('why clip bit from the current vertex only', '    *why = w;\n',
     '    *why = (w & EM_VU1_OBJ_ADC_DATA) | (cf ? EM_VU1_OBJ_ADC_CLIP : 0u);\n'),
    ('clip record zeroed', '    *clip = cf;\n', '    *clip = 0u * cf;\n'),
]


def defects():
    """Each defect in a copy of the header must make the quick run fail."""
    header = (ROOT / 'src/game/em_vu1_face_morph.h').read_text()
    OUT.mkdir(parents=True, exist_ok=True)
    caught = []
    for k, (label, old, new) in enumerate(DEFECTS):
        if header.count(old) != 1: fail(f'defect {label!r}: pattern not unique in the header')
        text = header.replace(old, new)
        if label == 'MSCNT keeps MSCAL constants':
            text = text.replace('    k.cap = em_eei_vu_add_raw(0u, EM_VU1_OBJ_COLOUR_CAP);',
                                '    k.cap = em_eei_vu_add_raw(0u, EM_VU1_OBJ_COLOUR_CAP);\n    keep = k; kept = 1;\nhave:')
        if label == 'template read before the last stores':
            text = text.replace('    const uint32_t o = top + EM_VU1_FACE_STORE + 4u * EM_VU1_FACE_VERTICES;',
                                '    emvuo_st(dmem, out->kick, emvuo_ld(dmem, 1020u));\n'
                                '    const uint32_t o = top + EM_VU1_FACE_STORE + 4u * EM_VU1_FACE_VERTICES;')
        path = OUT / f'defect_{k}.h'
        path.write_text(text)
        env = dict(os.environ, EM_VU1_FACE_HEADER=str(path), EM_VU1_FACE_TAG=f'defect_{k}', EM_TEST_FULL='0')
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
