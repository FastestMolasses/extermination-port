#!/usr/bin/env python3
"""Player drop shadow (001DA6A0 chain) against the original instructions.

Reads the owner's pinned ELF and the captured EE RAM under
../Extermination/build/startup-reference; embeds no original bytes. The report
holds addresses, counts and differences only. Docs: docs/SHADOW_ORIGINAL.md.

A. Original execution. 001CB590(player) then 001DA6A0(player) run as original
   instructions (every callee, including 001D89D0 lighting and the VU0 macro
   code, is executed; nothing is stubbed) over every capture of a world
   frame. The DMA cursor is set to the chain in the list being built (the
   greatest chain start at or below ctx+0x10), so the rebuilt chain must
   equal the captured bytes: playable, panel/animation, elevator/completed
   and elevator/clip47 byte-identical; roger-encounter and opening
   (cutscenes, selector 2) identical except inside the silhouette pass's
   bone palette (the pose was advanced after the list was built); handoff
   identical up to the receiver section, differing in the palette and the
   receiver section (reported: captured vs executed receiver passes). The
   other display list is reported only.
B. Native module. em_shadow_original_001DA6A0 over the same record bytes
   and render-context views; every computed value is compared bit for bit
   with the executed original: the light globals D_00817F20..D_00817FFF and
   ctx+0x24B0, the silhouette D_70003AC0 (snapshot at the 001C7420 entry),
   each box pass's uploads (colour rows, the two matrices, RGBAQ, the
   (W x V) x P matrix both kernels receive), the UV matrix upload, the
   receiver object sequence and its clip classes (0023E8A0 passes), and the
   worker call order. The capture-state D_00817FF0 is the previous light.
C. Receiver kernel. The 0023C200 VU1 program (executed by the snow test's VU
   interpreter, plus xtop/fcset/iaddi/mula) runs on every receiver batch of
   the executed chain after a VIF replay (UNPACK/MSCAL, BASE 0x190 OFFSET
   0x101 double buffer). Every vertex's RGBAQ and ST must equal
   em_shadow_original_receiver_vertex scaled by the kernel's Q.
D. Gates. Synthetic kinds/areas/variant through both sides. 0015C160 is
   executed as original instructions with its three gate bytes patched
   (36 cases; 001DA6A0, 0015BF90 and the +0x4C method recorded and
   skipped) and must name the callee the native route names. status-hub
   and panel: executing 001DA6A0 draws nothing, and the chain nearest the
   DMA cursor lies above it (left from an earlier frame).
E. GS. The 128x128 target at GS byte 0x258000 in every GS dump is decoded
   (PSMCT32, 2 pages wide) and reported; the receiver TEX0, the target setup
   packet (D_00817E20) and the state blocks are decoded from RAM and must
   equal the header constants.
F. GS side (src/gfx/metal/em_shadow_gs.h, the Metal em_gfx_shadow_*). Each
   executed chain is replayed from the head of its display list as the
   DMAC/VIF1/GIF would; the GS registers in force at every draw of the chain
   must be the header's pass state. Every program the chain CALLs runs as
   original VU1 instructions on one persistent VU1 (the kernel packets' MPG
   blocks loaded into micro memory, their STCYCL/BASE/OFFSET applied,
   MSCAL and MSCNT; in-order issue with VF operand stalls, flags 4 cycles
   after the FMAC op, Q 7 after DIV): the box (00237180), silhouette
   (0023C750) and receiver (0023C200) kernels, every kicked vertex word
   equal to the header's batch functions including each ADC and its
   reason; the templates' dmem 1021..1023 and 001C7420's bone rows equal
   to the header's. The clip kernels 00239C90 and 0023E8A0 run too: each
   clip batch holds its kernel batch's vertices, matrix and template rows,
   the triangles its loop sends to the clipping code are exactly the
   header's em_shadow_gs_needs_clip set, and it draws nothing else. The
   proxy EMDL's triangles must be the silhouette kernel's kicked
   triangles. Then, in a headless window, the Metal backend's silhouette
   target must equal those triangles rasterized at the GS sample points,
   texel for texel, and its receiver pixel pipeline must give
   em_shadow_gs_receiver_pixel's result.
G. Clip kernels (src/game/em_vu1_shadow_clip.h, the translation). Every
   clip batch of the seven captures and of all 15 AREA11 route beats
   (../Extermination/build/s87/route; per beat the native 001DA6A0 must
   draw without a fault and the executed chain must carry the plan's
   boxes, receivers, classes and worker order) runs through the
   translation, which must XGKICK what the executed kernel kicks: dmem
   address, order and every packet byte. The backend's own dmem image
   (em_shadow_gs_clip_dmem: plan matrices, template, fog row, qword 3
   only) must give the same GS vertices, which must unproject within 1/16
   pixel. Synthetic batches (quick 32, full 192) must match too and reach
   both outcomes of every conditional branch of both programs but the two
   CLIP_UNREACHABLE_TAKEN names; an FTOI result outside int32 must fault
   the translation at exactly that kick. The receiver asset
   (assets/scene_snow/shadow_receivers.emsr, em_shadow_receivers_load)
   must equal each capture's grid, bounds and uploaded batches. In Metal a
   class-2 receiver and a box with a clipped triangle must draw it; class 0
   must not.
--capture BEAT (not part of the default run): a headless native frame of a
   route beat (../Extermination/build/s87/route/BEAT) with and without the
   shadow, written to build/captures/shadow/, and the shadow-region metric
   against the beat's original.png; it FAILS on a worker fault or outside
   the bounds in capture_bounds (IoU >= 0.80, luminance ratio within 0.05).
"""
from pathlib import Path
import ctypes as C
import hashlib
import json
import math
import random
import struct
import subprocess
import sys

import test_actor_lighting_reference as al
from reference_mode import FULL, banner, part, pick, select, parallel_map, in_scope_beat
import test_level_material_reference as lm
import test_snow_particles_reference as vu
from test_point_light_reference import signed, bits, number, fp, RETURN

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent/'Extermination'
REF = DECOMP/'build/startup-reference'
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
PLAYER, CONTEXT_PTR = 0x8102B0, 0x275670
BEGIN_CTX, SHADOW, C7420 = 0x1CB590, 0x1DA6A0, 0x1C7420
RECEIVER_KERNEL, RECEIVER_PROGRAM, CLIP_KERNEL = 0x23C200, 0x23C230, 0x23E8A0
BOX_KERNEL, BOX_CLIP_KERNEL = 0x237180, 0x239C90
STATE_2_9 = 0x815E10
MASK64, MASK128 = (1 << 64)-1, (1 << 128)-1
CAPTURES = [  # (name, ee, scratchpad or None, required status)
    # exact: the current list's chain equals the executed chain byte for byte.
    # palette: differences only inside the silhouette bone palette (cutscene
    #   poses advance after the list is built).
    # receivers: differences only inside the palette and the receiver section
    #   (the receiver object set/clip classes of the captured list differ from
    #   what the capture-time RAM produces; the prefix is identical).
    ('playable', 'playable_ee.bin', None, 'exact'),
    ('panel-animation', 'panel/animation_ee.bin', 'panel/animation_scratchpad.bin', 'exact'),
    ('elevator-completed', 'elevator/completed_ee.bin', None, 'exact'),
    ('elevator-clip47', 'elevator/clip47_ee.bin', None, 'exact'),
    ('roger-encounter', 'roger-encounter/eeMemory.bin', 'roger-encounter/scratchpad.bin', 'palette'),
    ('opening', 'opening_ee.bin', 'opening_scratchpad.bin', 'palette'),
    ('handoff', 'handoff_ee.bin', None, 'receivers'),
]
# Captures where executing 001DA6A0 over the RAM draws nothing (clip reject).
# Their lists still hold chains, but none in the list being built: the chain
# nearest the DMA cursor ctx+0x10 lies above it (left from an earlier frame).
NOT_DRAWN = [('status-hub', 'status-hub/eeMemory.bin', 'status-hub/scratchpad.bin'),
             ('panel', 'panel/eeMemory.bin', None)]


def u32(b, a): return struct.unpack_from('<I', b, a)[0]
def f4(b, a): return list(struct.unpack_from('<4f', b, a))


# ---------------------------------------------------------------- oracle ----
class ShadowRam(al.Ram):
    """al.Ram (full-RAM backing, overlay writes) plus the scratchpad, the
    EE/MMI/COP1/VU0-macro ops this chain executes and call watching."""

    def __init__(self, elf, ram, scratch=None):
        super().__init__(elf, ram)
        self.scratch = scratch
        self.clip = 0
        self.lo = self.hi = 0
        self.facc = 0.0
        self.watch = {}
        self.calls_seen = []
        self.stub = set()      # callees recorded and skipped (not executed)
        self.stubbed = []

    def load(self, address, size=4):
        value = 0
        for i in range(size):
            a = address+i
            b = self.mem.get(a)
            if b is None:
                if a >= 0x70000000:
                    o = a-0x70000000
                    b = self.scratch[o] if self.scratch is not None and o < len(self.scratch) else 0
                else:
                    b = self.ram[a] if a < len(self.ram) else 0
            value |= b << (8*i)
        return value

    def macro(self, word):
        op, fs, ft, fd, mask = word & 63, word >> 11 & 31, word >> 16 & 31, word >> 6 & 31, word >> 21 & 15
        if op == 0x3F and fd == 11: return                       # vnop
        if op == 0x3F and fd == 7:                               # vclipw.xyz
            x = list(map(number, self.v[fs])); w = abs(number(self.v[ft][3]))
            f = (x[0] > w) | (x[0] < -w) << 1 | (x[1] > w) << 2 | (x[1] < -w) << 3 | \
                (x[2] > w) << 4 | (x[2] < -w) << 5
            self.clip = ((self.clip << 6) | f) & 0xFFFFFF
            return
        if op == 0x3E and fd == 11:                              # vopmula.xyz
            x = list(map(number, self.v[fs])); y = list(map(number, self.v[ft]))
            r = [fp(x[1]*y[2]), fp(x[2]*y[0]), fp(x[0]*y[1])]
            for lane in range(3):
                if mask & (8 >> lane): self.acc[lane] = r[lane]
            return
        if op == 0x2E:                                           # vopmsub.xyz
            x = list(map(number, self.v[fs])); y = list(map(number, self.v[ft]))
            r = [fp(self.acc[0]-fp(x[1]*y[2])), fp(self.acc[1]-fp(x[2]*y[0])),
                 fp(self.acc[2]-fp(x[0]*y[1]))]
            for lane in range(3):
                if mask & (8 >> lane) and fd: self.v[fd][lane] = bits(r[lane])
            return
        super().macro(word)

    def plain(self, word):
        r, f = self.r, self.f
        op, rs, rt, rd = word >> 26, word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        fn, sa, imm = word & 63, word >> 6 & 31, signed(word & 65535, 16)
        if op == 0:
            if fn in (56, 60): r[rd] = (r[rt] << (sa+(32 if fn == 60 else 0))) & MASK64; r[0] = 0; return
            if fn in (58, 62): r[rd] = (r[rt] & MASK64) >> (sa+(32 if fn == 62 else 0)); r[0] = 0; return
            if fn in (59, 63):
                r[rd] = (signed(r[rt] & MASK64, 64) >> (sa+(32 if fn == 63 else 0))) & MASK64; r[0] = 0; return
            if fn == 24:
                p = signed(r[rs]) * signed(r[rt])
                self.lo = p & 0xFFFFFFFF; self.hi = (p >> 32) & 0xFFFFFFFF
                if rd: r[rd] = self.lo
                r[0] = 0; return
            if fn == 18: r[rd] = self.lo; r[0] = 0; return
            if fn == 16: r[rd] = self.hi; r[0] = 0; return
            if fn in (32, 34): r[rd] = (r[rs]+r[rt] if fn == 32 else r[rs]-r[rt]) & 0xFFFFFFFF; r[0] = 0; return
            if fn in (45, 47): r[rd] = (r[rs]+r[rt] if fn == 45 else r[rs]-r[rt]) & MASK64; r[0] = 0; return
        if op == 63: self.save((r[rs]+imm) & 0xFFFFFFFF, r[rt] & MASK64, 8); return
        if op == 55: r[rt] = self.load((r[rs]+imm) & 0xFFFFFFFF, 8); r[0] = 0; return
        if op == 28:
            w = lambda v, i: (v >> (32*i)) & 0xFFFFFFFF
            a, b = r[rs], r[rt]
            if fn == 8 and sa == 0x12: r[rd] = w(b, 0) | w(a, 0) << 32 | w(b, 1) << 64 | w(a, 1) << 96; r[0] = 0; return
            if fn == 40 and sa == 0x12: r[rd] = w(b, 2) | w(a, 2) << 32 | w(b, 3) << 64 | w(a, 3) << 96; r[0] = 0; return
            if fn == 9 and sa == 0x0E: r[rd] = (b & MASK64) | (a & MASK64) << 64; r[0] = 0; return
            if fn == 41 and sa == 0x0E: r[rd] = (a >> 64) & MASK64 | ((b >> 64) & MASK64) << 64; r[0] = 0; return
        if op == 18 and rs in (2, 6):
            if rd != 18: raise AssertionError(('cfc2/ctc2', rd))
            if rs == 2: r[rt] = self.clip; r[0] = 0
            else: self.clip = r[rt] & 0xFFFFFF
            return
        if op == 17 and rs == 16:
            fs, fd = rd, sa
            x, y = number(f[fs]), number(f[rt])
            if fn == 7: f[fd] = bits(-x); return
            if fn == 24: self.facc = fp(x+y); return
            if fn == 26: self.facc = fp(x*y); return
            if fn == 28: f[fd] = bits(fp(self.facc+fp(x*y))); return
            if fn == 29: f[fd] = bits(fp(self.facc-fp(x*y))); return
            if fn == 36:
                iv = int(x) if math.isfinite(x) else (0x7FFFFFFF if x > 0 else -0x80000000)
                f[fd] = max(-0x80000000, min(0x7FFFFFFF, iv)) & 0xFFFFFFFF; return
        super().plain(word)

    def run(self, entry, args=(), floats=(), stop=RETURN, budget=6_000_000):
        self.r[31] = RETURN
        for i, value in enumerate(args): self.r[4+i] = value
        for i, value in enumerate(floats): self.f[12+i] = bits(value)
        pc = entry
        for _ in range(budget):
            if pc == stop: return True
            if pc == RETURN: return False
            word = self.load(pc); op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            offset = signed(word & 65535, 16)*4; branch = None
            if op in (2, 3):
                target = (pc & 0xF0000000) | (word & 0x3FFFFFF)*4
                if op == 3: self.r[31] = pc+8
                self.plain(self.load(pc+4))
                if op == 3 and target in self.watch: self.calls_seen.append((target, self.r[4], self.r[5]))
                if op == 3 and target in self.stub:
                    self.stubbed.append((target, self.r[4] & 0xFFFFFFFF)); pc += 8; continue
                pc = target
                continue
            if op == 0 and word & 63 == 9:
                target = self.r[rs] & 0xFFFFFFFF; self.r[word >> 11 & 31] = pc+8
                self.plain(self.load(pc+4))
                if target in self.stub:
                    self.stubbed.append((target, self.r[4] & 0xFFFFFFFF)); pc += 8; continue
                pc = target; continue
            if op in (4, 5, 20, 21):
                taken = (self.r[rs] & 0xFFFFFFFF) == (self.r[rt] & 0xFFFFFFFF)
                taken = taken if op in (4, 20) else not taken
                if op in (20, 21) and not taken: pc += 8; continue
                branch = pc+4+offset if taken else pc+8
            elif op in (6, 7, 22, 23):
                v = signed(self.r[rs] & 0xFFFFFFFF)
                taken = v <= 0 if op in (6, 22) else v > 0
                if op in (22, 23) and not taken: pc += 8; continue
                branch = pc+4+offset if taken else pc+8
            elif op == 1:
                if rt not in (0, 1, 2, 3): raise AssertionError(('REGIMM', rt))
                v = signed(self.r[rs] & 0xFFFFFFFF)
                taken = v < 0 if rt in (0, 2) else v >= 0
                if rt in (2, 3) and not taken: pc += 8; continue
                branch = pc+4+offset if taken else pc+8
            elif op == 17 and rs == 8:
                taken = self.condition == bool(rt & 1)
                if rt & 2 and not taken: pc += 8; continue
                branch = pc+4+offset if taken else pc+8
            elif op == 0 and word & 63 == 8: branch = self.r[rs] & 0xFFFFFFFF
            if branch is not None:
                self.plain(self.load(pc+4)); pc = branch
            else:
                try: self.plain(word)
                except AssertionError as error: raise AssertionError(hex(pc), error) from error
                pc += 4
        raise AssertionError('instruction budget exhausted')

    def read(self, address, size): return bytes(self.load(address+i, 1) for i in range(size))


def validate_ops(elf):
    """The added ops on synthetic words before any original code runs."""
    o = ShadowRam(elf, bytes(16))
    o.r[9] = 0x11112222_33334444_55556666_77778888; o.r[8] = 0xAAAABBBB_CCCCDDDD_EEEEFFFF_00001111
    o.plain(0x70000000 | 9 << 21 | 8 << 16 | 12 << 11 | 0x12 << 6 | 8)   # r12 = low words of r8/r9 interleaved
    w = lambda v, i: (v >> (32*i)) & 0xFFFFFFFF
    assert o.r[12] == (w(o.r[8], 0) | w(o.r[9], 0) << 32 | w(o.r[8], 1) << 64 | w(o.r[9], 1) << 96)
    o.r[5] = 0x8000_0000_0000_0000
    o.plain(0x0005283F)       # r5 = r5 >> 32, arithmetic
    assert o.r[5] == 0xFFFF_FFFF_8000_0000
    o.f[1], o.f[2] = bits(3.0), bits(0.1)
    o.plain(0x46020818)       # FPU accumulator = f1 + f2
    assert o.facc == fp(3.0+number(bits(0.1)))
    o.v[1] = [bits(2.0), bits(-3.0), bits(0.5), bits(1.0)]
    o.macro(0x4BC109FF)       # clip test of the x/y/z lanes of vf1 against its |w|
    assert o.clip == (1 | 8)


# --------------------------------------------------------------- captures ---
def chain_starts(ram):
    """Every REF(qwc 9, state block (2,9)) + CNT(7) + DIRECT alpha-clear
    sprite = the start of a 001DA290 chain. Tag bits 16..27 are masked: the
    tag writers store only the QWC halfword and the ID byte, so bits 16..23
    keep whatever the buffer held before (e.g. 0x30880009 in handoff_ee.bin).
    An earlier version of this detector required them to be 0 and so missed
    the chain in opening, handoff and elevator/clip47."""
    out = []
    pat = struct.pack('<I', STATE_2_9)
    i = ram.find(pat)
    while i >= 0:
        a = i-4
        if a % 16 == 0 and u32(ram, a) & 0x7000FFFF == 0x30000009 \
                and u32(ram, a+16) & 0x7000FFFF == 0x10000007 and u32(ram, a+0x2C) == 0x50000006 \
                and struct.unpack_from('<Q', ram, a+0x30)[0] == 0x5022400000008001:
            out.append(a)
        i = ram.find(pat, i+1)
    return out


def walk(buf, a, end):
    """Linear DMA walk of a built chain: (address, id, qwc, addr)."""
    out = []
    while a < end:
        w0, w1 = struct.unpack_from('<2I', buf, a)
        tid, qwc = (w0 >> 28) & 7, w0 & 0xFFFF
        out.append((a, tid, qwc, w1))
        a += 16*(qwc+1) if tid == 1 else 16
    return out


def execute(elf, ram, scratch, start, stop=RETURN):
    o = ShadowRam(elf, ram, scratch)
    ctx = u32(ram, CONTEXT_PTR)
    if scratch is None:          # the scratch VP is the frame camera (checked
        o.write(0x70003AC0, ram[ctx+0x23C0:ctx+0x2400])  # on every scratch dump)
    o.save(ctx+0x10, start)
    o.run(BEGIN_CTX, [PLAYER, 0x320, ram[PLAYER+9]])
    o.watch = {0x1D4FB0: 1, 0x1D4B50: 1}
    stopped = o.run(SHADOW, [PLAYER], stop=stop)
    return o, u32(bytes(o.read(ctx+0x10, 4)), 0), stopped


# ----------------------------------------------------------------- native ---
F16 = C.c_float*16
F4 = C.c_float*4


class Scene(C.Structure):
    _fields_ = [('clip_2240', F16), ('proj_2340', F16), ('view_2380', F16), ('camera_3AC0', F16),
                ('view_810610', F16), ('zoom_2468', C.c_float), ('area_700', C.c_uint8),
                ('sub_701', C.c_uint8), ('grid_140', C.POINTER(C.c_int32)), ('grid_words', C.c_uint32),
                ('stride_148', C.c_int32), ('cell_x_150', C.c_float), ('cell_z_154', C.c_float),
                ('origin_x_158', C.c_float), ('origin_z_15C', C.c_float)]


class State(C.Structure):
    _fields_ = [('d817FF0', F4)]


class Box(C.Structure):
    _fields_ = [('model', C.c_int32), ('world', F16), ('camera', F16), ('normal', F16),
                ('color_row', F4), ('rgbaq', C.c_uint32), ('clip_pass', F16)]


class Receiver(C.Structure):
    _fields_ = [('id', C.c_int32), ('cls', C.c_uint8), ('clip_lo', C.c_uint32), ('clip_hi', C.c_uint32)]


class Plan(C.Structure):
    _fields_ = [('drawn', C.c_int32), ('kind', C.c_int32), ('anchor', F4), ('probe', F4*3),
                ('probe_clip', C.c_uint32*3), ('variant', C.c_int32), ('spread', C.c_float),
                ('box_size', C.c_float), ('unit', C.c_float), ('light_817F70', F4),
                ('right_817F80', F4), ('up_817F90', F4), ('cross_817FC0', F4), ('eye_817F60', F4),
                ('view_817F20', F16), ('uv_24B0', F16), ('alpha_matrix', C.c_int32),
                ('near_index', C.c_int32), ('far_index', C.c_int32), ('near_817FB0', F4),
                ('far_817FA0', F4), ('box', Box*2), ('silhouette_vp', F16), ('cell_cx', C.c_int32),
                ('cell_cz', C.c_int32), ('cell_x0', C.c_int32), ('cell_x1', C.c_int32),
                ('cell_z0', C.c_int32), ('cell_z1', C.c_int32), ('screen_3400', F16),
                ('guard_3440', F16), ('receiver_count', C.c_uint32), ('receiver', Receiver*512)]


BOUNDS = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int32, C.POINTER(C.c_float), C.POINTER(C.c_float))
PLAIN = C.CFUNCTYPE(C.c_int, C.c_void_p)
BOXFN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(Box))
SILFN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int32, C.POINTER(C.c_float))
UVFN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_float))
RECFN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(Receiver))


class Workers(C.Structure):
    _fields_ = [('ctx', C.c_void_p), ('w_object_bounds', BOUNDS), ('w_alpha_clear', PLAIN),
                ('w_box', BOXFN), ('w_silhouette', SILFN), ('w_receiver_begin', UVFN),
                ('w_receiver', RECFN), ('w_receiver_end', PLAIN)]


class Fault(C.Structure):
    _fields_ = [('address', C.c_uint32), ('code', C.c_int32)]


class ReceiverObject(C.Structure):
    _fields_ = [('id', C.c_int32), ('batches', C.c_uint32), ('bmin', C.c_float*3), ('bmax', C.c_float*3),
                ('qw3', C.POINTER(C.c_float)), ('qwords', C.POINTER(C.c_uint32))]


class Receivers(C.Structure):
    """EmShadowReceivers (em_shadow_original.h)."""
    _fields_ = [('rows_144', C.c_int32), ('stride_148', C.c_int32), ('f150', C.c_float*6),
                ('grid', C.POINTER(C.c_int32)), ('grid_words', C.c_uint32), ('slots', C.c_uint32),
                ('object', C.POINTER(ReceiverObject)), ('box', ReceiverObject*2), ('blob', C.c_void_p)]


RECEIVERS_ASSET = ROOT/'assets/scene_snow/shadow_receivers.emsr'
RECEIVERS = [None]


def receivers_asset(lib):
    """The loaded asset (once per process), or None when it is missing."""
    if RECEIVERS[0] is None and RECEIVERS_ASSET.exists():
        r = Receivers()
        assert lib.em_shadow_receivers_load(C.byref(r), str(RECEIVERS_ASSET).encode()) == 0, \
            'em_shadow_receivers_load refused the asset'
        RECEIVERS[0] = r
    return RECEIVERS[0]


def asset_checks(lib, name, ram, scene, plan, batches, stats):
    """The exported receiver data (export_shadow_receivers.py, loaded by
    em_shadow_receivers_load) against the capture: the grid and ctx+0x144..
    +0x164 equal RAM; the native 001DA6A0 with the asset's grid and
    w_object_bounds (em_shadow_receivers_scene / _bounds) gives the same
    receivers and classes; every receiver and box batch the chain uploads
    equals the asset object's batch, all 128 qwords."""
    r = receivers_asset(lib)
    if r is None:
        stats['asset_skipped'] = stats.get('asset_skipped', 0)+1
        return
    ctx = u32(ram, CONTEXT_PTR)
    words = r.grid_words
    assert (r.rows_144, r.stride_148) == (u32(ram, ctx+0x144), u32(ram, ctx+0x148)), (name, 'grid shape')
    assert bytes(C.string_at(r.grid, 4*words)) == ram[u32(ram, ctx+0x140):u32(ram, ctx+0x140)+4*words], \
        (name, 'grid ids')
    assert fbytes(r.f150) == ram[ctx+0x150:ctx+0x168], (name, 'ctx+0x150..+0x164')
    s2 = Scene.from_buffer_copy(scene)
    lib.em_shadow_receivers_scene(C.byref(r), C.byref(s2))
    nat = NativeRun(lib, ram, s2, bounds=BOUNDS(lambda _, i, lo, hi: lib.em_shadow_receivers_bounds(
        C.addressof(r), i, lo, hi)))
    p = nat.plan
    assert nat.result == 1 and nat.fault.code == 0, (name, 'asset plan', nat.result, nat.fault.code)
    assert [(p.receiver[i].id, p.receiver[i].cls, p.receiver[i].clip_lo, p.receiver[i].clip_hi)
            for i in range(p.receiver_count)] == \
           [(plan.receiver[i].id, plan.receiver[i].cls, plan.receiver[i].clip_lo, plan.receiver[i].clip_hi)
            for i in range(plan.receiver_count)], (name, 'asset plan receivers')
    by_src = {}
    for kernel, top, before, _, src, _ in batches:
        if kernel in (RECEIVER_KERNEL, BOX_KERNEL):
            by_src.setdefault((kernel, src), []).append(bytes(before[top*16:(top+128)*16]))
    checked = 0
    for i in range(plan.receiver_count):
        ident = plan.receiver[i].id
        o = lib.em_shadow_receivers_object(C.byref(r), ident)
        assert o, (name, 'no asset object', ident)
        o = o.contents
        got = by_src[(RECEIVER_KERNEL, object_address(ram, ident)+0x40)]
        assert o.batches == len(got), (name, 'asset batches', ident, o.batches, len(got))
        for b in range(o.batches):
            assert C.string_at(C.addressof(o.qwords.contents)+2048*b, 2048) == got[b], (name, ident, b)
            checked += 1
        lo = [struct.unpack_from('<f', ram, object_address(ram, ident)+0x14+4*k)[0] for k in range(3)]
        assert list(o.bmin) == lo, (name, 'AABB', ident)
    library = u32(ram, 0x28A56C)
    for k in range(2):
        model = plan.box[k].model
        o = lib.em_shadow_receivers_box(C.byref(r), model).contents
        addr = (library + (signed(u32(ram, library+4*model+4)) >> 2 << 2)) & 0xFFFFFFFF
        got = by_src[(BOX_KERNEL, addr+0x40)]
        assert o.batches == len(got) and C.string_at(C.addressof(o.qwords.contents), 2048) == got[0], \
            (name, 'asset box model', hex(model))
        checked += 1
    stats['asset_batches_checked'] = stats.get('asset_batches_checked', 0)+checked
    stats['asset_captures'] = stats.get('asset_captures', 0)+1


def native_library(out):
    lib_path = out/'shadow.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-shared', '-fPIC', '-I'+str(ROOT/'src'), str(ROOT/'src/game/em_shadow_original.c'),
                    '-o', str(lib_path)], check=True)
    lib = C.CDLL(str(lib_path))
    lib.em_shadow_original_001DA6A0.argtypes = [C.c_char_p, C.POINTER(C.c_char_p), C.c_uint32,
                                                C.POINTER(Scene), C.POINTER(State), C.POINTER(Plan),
                                                C.POINTER(Workers), C.POINTER(Fault)]
    lib.em_shadow_original_receiver_vertex.argtypes = [C.POINTER(C.c_float), C.POINTER(C.c_float),
                                                       C.POINTER(C.c_float), C.POINTER(C.c_uint32)]
    lib.em_shadow_original_route_0015C160.argtypes = [C.c_uint8, C.c_uint8, C.c_uint32, C.POINTER(Fault)]
    lib.em_shadow_receivers_load.argtypes = [C.POINTER(Receivers), C.c_char_p]
    lib.em_shadow_receivers_free.argtypes = [C.POINTER(Receivers)]
    lib.em_shadow_receivers_scene.argtypes = [C.POINTER(Receivers), C.POINTER(Scene)]
    lib.em_shadow_receivers_object.argtypes = [C.POINTER(Receivers), C.c_int32]
    lib.em_shadow_receivers_object.restype = C.POINTER(ReceiverObject)
    lib.em_shadow_receivers_box.argtypes = [C.POINTER(Receivers), C.c_int32]
    lib.em_shadow_receivers_box.restype = C.POINTER(ReceiverObject)
    lib.em_shadow_receivers_bounds.argtypes = [C.c_void_p, C.c_int32, C.POINTER(C.c_float),
                                               C.POINTER(C.c_float)]
    return lib


def object_address(ram, ident):
    """001C6120(*D_0028A5A0, id) over the capture bytes."""
    bank = u32(ram, 0x28A5A0)
    a1 = ident & 0xFFFF & 0xFFFF7FFF
    return (bank + (signed(u32(ram, bank+4*a1+4)) >> 2 << 2)) & 0xFFFFFFFF


def f16(ram, a): return F16(*struct.unpack_from('<16f', ram, a))


def scene_view(ram, scratch):
    ctx = u32(ram, CONTEXT_PTR)
    cam = scratch[0x3AC0:0x3B00] if scratch is not None else ram[ctx+0x23C0:ctx+0x2400]
    grid_ptr, stride = u32(ram, ctx+0x140), signed(u32(ram, ctx+0x148))
    words = 32*stride*4
    grid = (C.c_int32*words)(*struct.unpack_from(f'<{words}i', ram, grid_ptr))
    s = Scene(f16(ram, ctx+0x2240), f16(ram, ctx+0x2340), f16(ram, ctx+0x2380),
              F16(*struct.unpack('<16f', cam)), f16(ram, 0x810610),
              struct.unpack_from('<f', ram, ctx+0x2468)[0], ram[0x810700], ram[0x810701],
              C.cast(grid, C.POINTER(C.c_int32)), words, stride,
              *struct.unpack_from('<4f', ram, ctx+0x150))
    s._grid = grid
    return s


class NativeRun:
    def __init__(self, lib, ram, scene, actor=None, nodes=None, ff0=None, fail_at=None, bounds=None):
        self.log = []
        self.ram = ram
        actor = actor if actor is not None else ram[PLAYER:PLAYER+0x320]
        count = actor[9]
        if nodes is None:
            nodes = []
            for i in range(max(count, 4)):
                p = u32(actor, 0x110+4*i) if 0x110+4*i+4 <= len(actor) else 0
                nodes.append(ram[p:p+0xD0] if p else None)
        self.node_bufs = [C.create_string_buffer(n, len(n)) if n is not None else None for n in nodes]
        arr = (C.c_char_p*len(nodes))(*[C.cast(b, C.c_char_p) if b is not None else None
                                        for b in self.node_bufs])
        self.actor = C.create_string_buffer(bytes(actor), len(actor))
        self.state = State(F4(*struct.unpack_from('<4f', ram, 0x817FF0))) if ff0 is None else State(F4(*ff0))
        self.plan = Plan(); self.fault = Fault()
        self.fail_at = fail_at

        def rec(name, value=0):
            self.log.append(name)
            return -1 if self.fail_at == name else value

        def ram_bounds(_, ident, lo, hi):
            self.log.append(('bounds', ident))
            obj = object_address(ram, ident)
            for i in range(3):
                lo[i] = struct.unpack_from('<f', ram, obj+0x14+4*i)[0]
                hi[i] = struct.unpack_from('<f', ram, obj+0x24+4*i)[0]
            return -1 if self.fail_at == 'bounds' else 0
        self.cb = [bounds if bounds is not None else BOUNDS(ram_bounds), PLAIN(lambda _: rec('alpha_clear')),
                   BOXFN(lambda _, b: rec(('box', b.contents.model))),
                   SILFN(lambda _, k, vp: rec(('silhouette', k))),
                   UVFN(lambda _, uv: rec('receiver_begin')),
                   RECFN(lambda _, r: rec(('receiver', r.contents.id, r.contents.cls))),
                   PLAIN(lambda _: rec('receiver_end'))]
        self.workers = Workers(None, *self.cb)
        self.result = lib.em_shadow_original_001DA6A0(self.actor, arr, len(nodes), C.byref(scene),
                                                      C.byref(self.state), C.byref(self.plan),
                                                      C.byref(self.workers), C.byref(self.fault))


def fbytes(values): return struct.pack(f'<{len(values)}f', *values)


# ----------------------------------------------------- receiver VU replay ---
def vif_units(buf, start, qwc):
    """Parse a VIF stream: yields ('unpack', addr, flg, data qwords) and
    ('mscal', addr) and ('direct', qwc)."""
    a, end = start, start+16*qwc
    out = []
    while a < end:
        v = u32(buf, a); cmd = (v >> 24) & 0x7F; imm = v & 0xFFFF; num = (v >> 16) & 0xFF
        if cmd >= 0x60:
            if cmd & 0x0F != 0x0C: raise AssertionError(('unpack format', hex(v)))
            n = num or 256
            out.append(('unpack', imm & 0x3FF, bool(imm & 0x8000), buf[a+4:a+4+16*n]))
            a += 4+16*n; continue
        if cmd == 0x50:
            a += 4; a = (a+15)//16*16; out.append(('direct', imm)); a += 16*imm; continue
        if cmd == 0x14 or cmd == 0x15: out.append(('mscal', imm)); a += 4; continue
        if cmd == 0x17: out.append(('mscnt', 0)); a += 4; continue
        if cmd == 0x20: a += 8; continue
        if cmd in (0x30, 0x31): a += 20; continue
        if cmd in (0, 1, 2, 3, 4, 5, 6, 7, 0x10, 0x11, 0x13): a += 4; continue
        raise AssertionError(('vif', hex(v), hex(a)))
    return out


class MiniVU:
    """A VU1 interpreter for exactly the instructions of the 70-instruction
    0023C200 program (MPG at ELF 0x23C230). Same conventions as the snow
    test's interpreter: binary32 results truncated, one cycle per
    instruction, Q written 7 cycles after DIV and the CLIP flag register 4
    cycles after CLIP (the program's own nop scheduling relies on these).
    Anything else raises."""

    def __init__(self, elf, dmem, top):
        self.elf = elf
        self.mem = bytearray(dmem)
        self.v = [[0, 0, 0, 0] for _ in range(32)]; self.v[0] = [0, 0, 0, bits(1.0)]
        self.vi = [0]*16
        self.acc = [0.0]*4
        self.q = 0.0; self.i = 0.0; self.cf = 0; self.cycle = 0
        self.pending = []
        self.top = top
        self.kicks = []

    def rd(self, a): return list(struct.unpack_from('<4I', self.mem, (a & 1023)*16))
    def wr(self, a, x): struct.pack_into('<4I', self.mem, (a & 1023)*16, *x)

    def upper(self, pc, up):
        code, fs, ft, fd, mask = up & 0x7FF, up >> 11 & 31, up >> 16 & 31, up >> 6 & 31, up >> 21 & 15
        op = up & 63
        x = list(map(number, self.v[fs])); y = list(map(number, self.v[ft]))
        res, dest, accw, intw = None, fd, False, False
        if code == 0x2FF: return
        if code == 0x1FF:                                  # clip fs.xyz, ft.w
            w = abs(y[3]); f = 0
            for c in range(3):
                if x[c] > w: f |= 1 << (2*c)
                if x[c] < -w: f |= 2 << (2*c)
            self.pending.append((self.cycle+4, 'cf', ((self.cf << 6) | f) & 0xFFFFFF)); return
        if code == 0x17D:                                  # ftoi4
            res = [int(v*16) & 0xFFFFFFFF for v in x]; dest = ft; intw = True
        elif code in range(0x1BC, 0x1C0):                  # mulA bc
            res = [fp(a*y[code & 3]) for a in x]; accw = True
        elif code in range(0x0BC, 0x0C0):                  # maddA bc
            res = [fp(self.acc[c]+fp(x[c]*y[code & 3])) for c in range(4)]; accw = True
        elif code == 0x2BE:                                # mulA
            res = [fp(x[c]*y[c]) for c in range(4)]; accw = True
        elif op < 4: res = [fp(a+y[op]) for a in x]
        elif 8 <= op < 12: res = [fp(self.acc[c]+fp(x[c]*y[op & 3])) for c in range(4)]
        elif 16 <= op < 20: res = [max(a, y[op & 3]) for a in x]
        elif 20 <= op < 24: res = [min(a, y[op & 3]) for a in x]
        elif op == 0x1C: res = [fp(a*self.q) for a in x]
        elif op == 0x22: res = [fp(a+self.i) for a in x]
        elif op == 0x2C: res = [fp(a-b) for a, b in zip(x, y)]
        else: raise AssertionError(('VU upper', hex(pc), hex(up)))
        for c in range(4):
            if mask & (8 >> c):
                if accw: self.acc[c] = res[c]
                elif dest: self.v[dest][c] = res[c] if intw else bits(res[c])

    def lower(self, pc, lo):
        op, it, iss, idd = lo >> 25, lo >> 16 & 31, lo >> 11 & 31, lo >> 6 & 31
        mask, imm11 = lo >> 21 & 15, signed(lo & 0x7FF, 11)
        nxt = None
        if lo == 0x8000033C: return None
        if op == 0x00: self.v[it] = self.rd(self.vi[iss]+imm11)
        elif op == 0x01:
            a = self.vi[it]+imm11; q = self.rd(a)
            for c in range(4):
                if mask & (8 >> c): q[c] = self.v[iss][c]
            self.wr(a, q)
        elif op == 0x04:
            q = self.rd(self.vi[iss]+imm11)
            for c in range(4):
                if mask & (8 >> c): self.vi[it] = q[c] & 0xFFFF
        elif op == 0x08: self.vi[it] = (self.vi[iss]+((lo & 0x7FF) | (lo >> 21 & 15) << 11)) & 0xFFFF
        elif op == 0x11: self.cf = lo & 0xFFFFFF
        elif op == 0x12: self.vi[1] = int(bool(self.cf & lo & 0xFFFFFF))
        elif op == 0x20: nxt = pc+8+imm11*8
        elif op in (0x28, 0x29):
            if (self.vi[iss] == self.vi[it]) == (op == 0x28): nxt = pc+8+imm11*8
        elif op == 0x40:
            fn = lo & 0x7FF
            if lo & 63 == 0x32: self.vi[it] = (self.vi[iss]+signed(idd, 5)) & 0xFFFF
            elif lo & 63 == 0x34: self.vi[idd] = self.vi[iss] & self.vi[it]
            elif lo & 63 == 0x35: self.vi[idd] = self.vi[iss] | self.vi[it]
            elif fn == 0x33C:
                for c in range(4):
                    if mask & (8 >> c) and it: self.v[it][c] = self.v[iss][c]
            elif fn == 0x3BC:
                a = number(self.v[iss][lo >> 21 & 3]); b = number(self.v[it][lo >> 23 & 3])
                self.pending.append((self.cycle+7, 'q', fp(a/b) if b else number(0x7F7FFFFF)))
            elif fn == 0x6BC: self.vi[it] = self.top
            elif fn == 0x6FC: self.kicks.append(self.vi[iss])
            else: raise AssertionError(('VU lower special', hex(pc), hex(lo)))
        else: raise AssertionError(('VU lower', hex(pc), hex(lo)))
        return nxt

    def run(self):
        pc, branch, end_after = RECEIVER_PROGRAM, None, None
        for _ in range(100000):
            for t, k, value in self.pending[:]:
                if t <= self.cycle: setattr(self, k, value); self.pending.remove((t, k, value))
            o = pc-0x100000+0x300
            lo, up = struct.unpack_from('<II', self.elf, o)
            if not RECEIVER_PROGRAM <= pc < RECEIVER_PROGRAM+70*8: raise AssertionError(('VU pc', hex(pc)))
            nxt = branch if branch is not None else pc+8
            branch = None
            self.upper(pc, up)
            if up >> 31: self.i = number(lo)
            else:
                b = self.lower(pc, lo)
                if b is not None: branch = b
            self.vi[0] = 0; self.v[0] = [0, 0, 0, bits(1.0)]
            self.cycle += 1
            if end_after is not None: return
            if up >> 30 & 1: end_after = True
            pc = nxt
        raise AssertionError('VU runaway')


def receiver_replay(elf, buf, units):
    """VIF replay of the executed receiver passes. Yields per 0023C200 batch
    (top, dmem-in, dmem-out)."""
    dmem = bytearray(16384)
    base, offset, tops = 0x190, 0x101, 0x190
    program = None
    batches, clip_batches = [], 0
    for a, tid, qwc, addr in units:
        if tid == 5: program = addr; continue
        if tid == 1: stream = vif_units(buf, a+16, qwc)
        elif tid == 3: stream = vif_units(buf, addr, qwc)
        else: continue
        for item in stream:
            if item[0] == 'unpack':
                _, dst, flg, data = item
                dst = dst + (tops if flg else 0)
                dmem[dst*16:dst*16+len(data)] = data
            elif item[0] == 'mscal':
                top = tops
                tops = base+offset if tops == base else base
                if program == RECEIVER_KERNEL:
                    machine = MiniVU(elf, dmem, top)
                    before = bytes(dmem)
                    machine.run()
                    dmem[:] = machine.mem
                    batches.append((top, before, bytes(dmem)))
                elif program == CLIP_KERNEL:
                    clip_batches += 1
                else:
                    raise AssertionError(('unexpected kernel in receiver pass', hex(program or 0)))
    return batches, clip_batches


# ------------------------------------------------------------ comparisons ---
def unit_data(buf, unit):
    a, tid, qwc, addr = unit
    return buf[a+32:a+16+16*qwc] if tid == 1 else buf[addr:addr+16*qwc]


def eq(label, native, original, stats, key):
    if bytes(native) != bytes(original):
        raise AssertionError((label, bytes(native).hex(), bytes(original).hex()))
    stats[key] = stats.get(key, 0) + len(bytes(original))


def box_checks(buf, units, plan, ram, stats):
    calls = [j for j, u in enumerate(units) if u[1] == 5 and u[3] == BOX_KERNEL]
    assert len(calls) == 2, ('box passes', len(calls))
    library = u32(ram, 0x28A56C)
    for k, j in enumerate(calls):
        box = plan.box[k]
        colour, first, state, second, const_colour, ref = (units[j-6], units[j-5], units[j-4],
                                                           units[j-3], units[j-2], units[j-1])
        assert state[1] == 3 and state[3] == STATE_2_9 and ref[3] == 0x814220
        cd = unit_data(buf, colour)
        eq(f'box{k} colour rows 0..2', bytes(48), cd[:48], stats, 'box_bytes')
        eq(f'box{k} colour row', fbytes(box.color_row), cd[48:64], stats, 'box_bytes')
        fd = unit_data(buf, first)
        eq(f'box{k} camera', fbytes(box.camera), fd[:64], stats, 'box_bytes')
        eq(f'box{k} normal', fbytes(box.normal), fd[64:128], stats, 'box_bytes')
        sd = unit_data(buf, second)
        eq(f'box{k} clip-pass matrix (0023 7180)', fbytes(box.clip_pass), sd[:64], stats, 'box_bytes')
        rgba = unit_data(buf, units[j+2])
        assert u32(rgba, 8) == 0x0E and u32(rgba, 24) == 0x01, ('box RGBAQ A+D', k)
        eq(f'box{k} RGBAQ', struct.pack('<I', box.rgbaq), rgba[16:20], stats, 'box_bytes')
        model = (library + (signed(u32(ram, library+4*box.model+4)) >> 2 << 2)) & 0xFFFFFFFF
        assert units[j+3][1] == 3 and units[j+3][3] == model+0x40, ('box model', k)
        # the clip pass (001D4C20): same matrix into the 00239C90 kernel
        assert units[j+7][1] == 5 and units[j+7][3] == BOX_CLIP_KERNEL
        eq(f'box{k} clip-pass matrix (0023 9C90)', fbytes(box.clip_pass), unit_data(buf, units[j+4])[:64],
           stats, 'box_bytes')
        assert units[j+9][3] == model+0x40
    return calls


def receiver_sequence(units, start_index):
    """(object address, kernel) in chain order after the receiver setup."""
    program, out = None, []
    state_area = range(0x814220, 0x818000)   # D_00275674 blocks and templates
    for a, tid, qwc, addr in units[start_index:]:
        if tid == 5: program = addr
        elif tid == 3 and addr not in state_area:
            if not out or out[-1] != (addr, program): out.append((addr, program))
    return out


def run_capture(elf, lib, name, ee, sp, status, stats, report):
    ram = (REF/ee).read_bytes()
    scratch = (REF/sp).read_bytes() if sp else None
    ctx = u32(ram, CONTEXT_PTR)
    if scratch is not None:
        assert scratch[0x3AC0:0x3B00] == ram[ctx+0x23C0:ctx+0x2400], (name, 'D_70003AC0 != ctx+0x23C0')
        stats['scratch_vp_is_camera'] = stats.get('scratch_vp_is_camera', 0) + 1
    starts = chain_starts(ram)
    cursor = u32(ram, ctx+0x10)
    current = max(s for s in starts if s <= cursor)   # the chain of the list being built
    entry = {'capture': name, 'status': status, 'cursor': hex(cursor), 'chain_starts': [hex(s) for s in starts],
             'current_start': hex(current), 'lists': []}
    matched = None
    for start in starts:
        o, end, _ = execute(elf, ram, scratch, start)
        written = [a for a in o.mem if start <= a < end]
        diff = sorted(a for a in written if o.mem[a] != ram[a])
        entry['lists'].append({'start': hex(start), 'qwords': (end-start)//16, 'written_bytes': len(written),
                               'differing_bytes': len(diff)})
        if start == current:
            matched = (start, end, o, diff)
    start, end, o, diff = matched
    assert status != 'exact' or not diff, (name, 'current list differs from the executed chain', entry)
    buf = bytearray(ram)
    for a in range(start, end): buf[a] = o.load(a, 1)
    units = walk(buf, start, end)
    if diff:
        sil = [j for j, u in enumerate(units) if u[1] == 3 and u[3] == 0x817E20][0]
        palette = units[sil+2]
        lo, hi = palette[0]+32, palette[0]+16+16*palette[2]
        outside = [a for a in diff if not lo <= a < hi]
        entry['palette_difference_bytes'] = len(diff)-len(outside)
        if status == 'receivers':
            first = [j for j, u in enumerate(units) if u[1] == 5 and u[3] == RECEIVER_KERNEL][0]
            section = units[first-3][0]
            assert walk(ram, start, section) == walk(buf, start, section), (name, 'prefix tags differ')
            assert all(a >= section for a in outside), (name, 'differences before the receiver section',
                                                         [hex(a) for a in outside if a < section][:8])
            cap_units = walk(ram, start, end+0x4000)
            cap_first = [j for j, u in enumerate(cap_units) if u[1] == 5 and u[3] == RECEIVER_KERNEL][0]
            stop = next(j for j in range(cap_first, len(cap_units)) if cap_units[j][1] == 5
                        and cap_units[j][3] not in (RECEIVER_KERNEL, CLIP_KERNEL))
            cap_seq = receiver_sequence(cap_units[:stop], cap_first-3)
            exe_seq = receiver_sequence(units, first-3)
            entry['receiver_difference_bytes'] = len(outside)
            entry['captured_receiver_passes'] = [[hex(a), hex(k)] for a, k in cap_seq]
            entry['executed_receiver_passes'] = [[hex(a), hex(k)] for a, k in exe_seq]
        else:
            assert status == 'palette' and not outside, (name, 'differences outside the silhouette palette',
                                                         [hex(a) for a in outside[:8]])
        entry['palette_only_difference_bytes'] = len(diff) if not outside else None
    stats['original_chain_bytes'] = stats.get('original_chain_bytes', 0) + (end-start)

    # snapshot of D_70003AC0 when 001D9EE0 enters 001C7420
    snap, _, stopped = execute(elf, ram, scratch, start, stop=C7420)
    assert stopped, 'no 001C7420 call'
    silhouette_vp = snap.read(0x70003AC0, 64)

    scene = scene_view(ram, scratch)
    nat = NativeRun(lib, ram, scene)
    p = nat.plan
    assert nat.result == 1 and p.drawn == 1 and nat.fault.code == 0, (name, nat.result, nat.fault.code)
    g = lambda a, n=16: o.read(a, n)
    eq('D_00817F20', fbytes(p.view_817F20), g(0x817F20, 64), stats, 'light_bytes')
    eq('D_00817F60', fbytes(p.eye_817F60), g(0x817F60), stats, 'light_bytes')
    eq('D_00817F70', fbytes(p.light_817F70), g(0x817F70), stats, 'light_bytes')
    eq('D_00817F80', fbytes(p.right_817F80), g(0x817F80), stats, 'light_bytes')
    eq('D_00817F90', fbytes(p.up_817F90), g(0x817F90), stats, 'light_bytes')
    eq('D_00817FA0', fbytes(p.far_817FA0), g(0x817FA0), stats, 'light_bytes')
    eq('D_00817FB0', fbytes(p.near_817FB0), g(0x817FB0), stats, 'light_bytes')
    eq('D_00817FC0', fbytes(p.cross_817FC0), g(0x817FC0), stats, 'light_bytes')
    eq('D_00817FF0', fbytes(nat.state.d817FF0), g(0x817FF0), stats, 'light_bytes')
    eq('ctx+0x24B0', fbytes(p.uv_24B0), g(ctx+0x24B0, 64), stats, 'light_bytes')
    eq('silhouette D_70003AC0', fbytes(p.silhouette_vp), silhouette_vp, stats, 'silhouette_bytes')
    box_checks(buf, units, p, ram, stats)
    if status == 'exact':
        # 001C7420's palette under the silhouette VP: node(+0x90) x VP per bone
        # (the port's skinned draw of the proxy mesh with the player palette).
        sil = [j for j, u in enumerate(units) if u[1] == 3 and u[3] == 0x817E20][0]
        pal = unit_data(buf, units[sil+2])
        assert ram[PLAYER+0x0C]*128 == len(pal) and signed(struct.unpack_from('<h', ram, PLAYER+0x94)[0], 16) < 0
        for b in range(ram[PLAYER+0x0C]):
            node = struct.unpack_from('<16f', ram, u32(ram, PLAYER+0x110+4*b)+0x90)
            rows = []
            for r in range(4):
                v = node[4*r:4*r+4]
                for lane in range(4):
                    m = p.silhouette_vp
                    acc = fp(m[lane]*v[0]); acc = fp(acc+fp(m[4+lane]*v[1]))
                    acc = fp(acc+fp(m[8+lane]*v[2])); rows.append(fp(acc+fp(m[12+lane]*v[3])))
            eq(f'silhouette bone {b}', struct.pack('<16f', *rows), pal[128*b:128*b+64], stats,
               'silhouette_palette_bytes')

    first = [j for j, u in enumerate(units) if u[1] == 5 and u[3] == RECEIVER_KERNEL][0]
    kd = unit_data(buf, units[first-3])
    eq('receiver camera (dmem 0)', fbytes(scene.camera_3AC0), kd[:64], stats, 'receiver_bytes')
    uv_unit = units[first+5]
    assert uv_unit[1] == 1 and u32(buf, uv_unit[0]+28) == 0x6C040008, 'UV upload'
    eq('receiver UV upload (dmem 8)', fbytes(p.uv_24B0), unit_data(buf, uv_unit), stats, 'receiver_bytes')
    tex = unit_data(buf, units[first+4])
    tex0 = struct.unpack_from('<Q', tex, 0x20)[0]
    assert u32(tex, 0x28) == 6 and u32(tex, 0x18) == 0x3F and tex0 == (0x2580 | 2 << 14 | 0 << 20 | 7 << 26 | 7 << 30 | 1 << 34 | 0 << 35), \
        hex(tex0)
    uploads = [u for u in units[first-3:] if u[1] == 1 and u32(buf, u[0]+28) in (0x6C040008, 0x6C040004)]
    for u in uploads:   # 001D4CD0 (dmem 8, 0023C200) and 001D49D0 (dmem 4, 0023E8A0)
        eq('receiver UV upload', fbytes(p.uv_24B0), unit_data(buf, u), stats, 'receiver_bytes')
    seq = receiver_sequence(units, first-3)
    expected = []
    for i in range(p.receiver_count):
        r = p.receiver[i]
        addr = object_address(ram, r.id)+0x40
        expected.append((addr, RECEIVER_KERNEL))
        if r.cls == 2: expected.append((addr, CLIP_KERNEL))
    assert seq == expected, (name, 'receiver sequence', [(hex(a), hex(k)) for a, k in seq][:12],
                             [(hex(a), hex(k)) for a, k in expected][:12])
    watched = [c[1] for c in o.calls_seen if c[0] == 0x1D4FB0]
    library = u32(ram, 0x28A56C)
    assert watched[:2] == [(library + (signed(u32(ram, library+4*m+4)) >> 2 << 2)) & 0xFFFFFFFF
                           for m in (0x14, 0x15)], 'box model draws'
    assert watched[2:] == [object_address(ram, p.receiver[i].id) for i in range(p.receiver_count)]
    classes = [p.receiver[i].cls for i in range(p.receiver_count)]
    log = [x for x in nat.log if not (isinstance(x, tuple) and x[0] == 'bounds')]
    want = ['alpha_clear', ('box', 0x14), ('box', 0x15), ('silhouette', p.kind), 'receiver_begin'] + \
           [('receiver', p.receiver[i].id, p.receiver[i].cls) for i in range(p.receiver_count)] + ['receiver_end']
    assert log == want, (name, log[:8])

    batches, clip_batches = receiver_replay(elf, buf, units[first-3:])
    vertices = 0
    for top, before, after in batches:
        for i in range(32):
            base = top+4*i
            pos = struct.unpack_from('<3f', before, (base+3)*16)
            st = after[(base+130)*16:(base+130)*16+16]
            rgba = after[(base+131)*16:(base+131)*16+16]
            out, alpha = (C.c_float*4)(), C.c_uint32()
            lib.em_shadow_original_receiver_vertex(p.uv_24B0, (C.c_float*3)(*pos), out, C.byref(alpha))
            q = struct.unpack_from('<f', st, 8)[0]
            assert out[2] == 1.0, 'uv z lane'
            want_st = struct.pack('<3f', fp(out[0]*q), fp(out[1]*q), fp(out[2]*q))
            eq('receiver ST', want_st, st[:12], stats, 'receiver_vertex_bytes')
            eq('receiver RGBAQ', struct.pack('<3If', 0, 0, 0, 8388608.0+alpha.value), rgba, stats,
               'receiver_vertex_bytes')
            stats['alpha_histogram'][alpha.value] = stats['alpha_histogram'].get(alpha.value, 0)+1
            vertices += 1
    stats['receiver_vertices'] = stats.get('receiver_vertices', 0)+vertices
    # F. GS side: kernels and GS state of the executed chain
    batches = kernel_replay(elf, buf, units)
    asset_checks(lib, name, ram, scene, p, batches, stats)
    whys = kernel_checks(GSLIB, name, status, ram, batches, p, stats, entry)
    clip_kernel_checks(GSLIB, name, batches, whys, p, ram, stats, entry, scene.camera_3AC0)
    gs_state_checks(GSLIB, name, buf, start, end, stats, entry)
    if status == 'exact' and PROXY_EMDL.exists():
        tris, xy = kicked_silhouette(batches)
        verts, idx, _ = load_emdl(PROXY_EMDL)
        assert proxy_triangles(verts, idx) == tris, (name, 'proxy EMDL triangles != kicked triangles')
        stats['gs_proxy_triangles'] = stats.get('gs_proxy_triangles', 0)+len(tris)
        METAL_CASES.append((name, ram, list(p.silhouette_vp), xy))
    entry.update({'matched_start': hex(start), 'receivers': p.receiver_count, 'classes': classes,
                  'cells': [p.cell_x0, p.cell_x1, p.cell_z0, p.cell_z1], 'batches': len(batches),
                  'clip_batches': clip_batches, 'vertices': vertices, 'alpha_matrix': p.alpha_matrix,
                  'anchor': list(p.anchor), 'near_index': p.near_index, 'far_index': p.far_index})
    report['captures'].append(entry)
    return ram, scratch


def roger_records(ram):
    """Actor records with 001BA580 code 0x47 (+0x0D), +0x56 != 0 and kind
    0x29 (+0x96): Roger's, reached through the AREA11 overlay's
    001BA580(actor, actor+0x0D) calls."""
    return [a for a in range(0x700000, 0x800000, 16)
            if ram[a+0x0D] == 0x47 and struct.unpack_from('<h', ram, a+0x56)[0] != 0
            and struct.unpack_from('<h', ram, a+0x96)[0] == 0x29]


def roger_checks(elf, report, stats):
    """001CB590(Roger) + 001DA6A0(Roger) executed over every capture."""
    rows = []
    for name, ee, sp in [(n, e, s) for n, e, s, _ in CAPTURES] + NOT_DRAWN:
        ram = (REF/ee).read_bytes()
        scratch = (REF/sp).read_bytes() if sp else None
        found = roger_records(ram)
        assert found, (name, 'no Roger record')
        for roger in found:
            o = ShadowRam(elf, ram, scratch)
            ctx = u32(ram, CONTEXT_PTR)
            if scratch is None: o.write(0x70003AC0, ram[ctx+0x23C0:ctx+0x2400])
            o.save(ctx+0x10, 0x1F00000)
            o.run(BEGIN_CTX, [roger, 0x320, ram[roger+9]])
            o.run(SHADOW, [roger])
            emitted = (u32(bytes(o.read(ctx+0x10, 4)), 0)-0x1F00000)//16
            rows.append({'capture': name, 'record': hex(roger), 'emitted_qwords': emitted,
                         'probe_clip': hex(o.clip)})
    report['roger'] = rows
    stats['roger_executions'] = len(rows)
    stats['roger_drawn'] = sum(r['emitted_qwords'] > 0 for r in rows)


def route_0015C160(elf, lib, report, stats):
    """0015C160 executed as original instructions over playable_ee.bin with
    its gate bytes D_008102B1, D_00810771 and player+0x214 patched. 001CB590
    runs (it publishes D_00275B44/D_00275B48 = its argument); 001DA6A0,
    0015BF90 and the player's +0x4C method are recorded with their a0 and
    skipped. The native route must name the same callee."""
    ram = (REF/'playable_ee.bin').read_bytes()
    method = u32(ram, PLAYER+0x4C)
    ROUTE_ALT = 0x15BF90
    cases = []
    for b1 in (0, 1, 2):
        for d771 in (0, 1, 2, 0xFF):
            for w214 in (0, 1, 0x80000000):
                mod = bytearray(ram)
                mod[0x8102B1] = b1; mod[0x810771] = d771
                struct.pack_into('<I', mod, PLAYER+0x214, w214)
                o = ShadowRam(elf, bytes(mod))
                o.stub = {SHADOW, ROUTE_ALT, method}
                assert o.run(0x15C160) is True, 'no return from 0015C160'
                calls = [t for t, _ in o.stubbed]
                executed = 1 if SHADOW in calls else (2 if ROUTE_ALT in calls else 0)
                fault = Fault()
                r = lib.em_shadow_original_route_0015C160(b1, d771, w214, C.byref(fault))
                native = 2 if (r == -1 and fault.code == 5 and fault.address == ROUTE_ALT) else r
                assert native == executed, (b1, d771, w214, native, executed, calls)
                assert r != -1 or native == 2, (b1, d771, w214, 'unexpected fault', fault.code)
                want = ([] if not b1 else ([SHADOW] if executed == 1 else [ROUTE_ALT] if executed == 2 else [])
                        + [method])
                assert calls == want, (b1, d771, w214, [hex(c) for c in calls])
                assert all(a0 == PLAYER for _, a0 in o.stubbed), [(hex(t), hex(a)) for t, a in o.stubbed]
                cases.append({'D_008102B1': b1, 'D_00810771': d771, 'player_214': hex(w214),
                              'calls': [hex(c) for c in calls], 'native': r})
    report['route_0015C160'] = cases
    stats['route_cases'] = len(cases)


def gate_cases(elf, lib, ram, scratch, stats, report):
    """Synthetic record/global variations executed on both sides."""
    ctx = u32(ram, CONTEXT_PTR)
    rng = random.Random(0x5AD0)
    cases = []
    node1 = u32(ram, PLAYER+0x114)
    far = struct.pack('<4f', 0.0, -5000.0, 0.0, 1.0)
    variations = [('kind 0', {PLAYER+0x96: struct.pack('<h', 0)}),
                  ('variant', {PLAYER+0x23C: b'\x01'}),
                  ('clip reject', {node1+0xC0: far})]
    for kind in (0x29, 0x2A, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x34, 0x35, 0x10):
        variations.append((f'kind {kind:#x}', {PLAYER+0x96: struct.pack('<h', kind)}))
    for key in (0x601, 0x000, 0x1100, 0x0B00, 0x0C01, 0x1700):
        variations.append((f'area {key:#06x}', {0x810700: bytes((key >> 8, key & 255))}))
    variations.append(('sub 0xFF anchor', {PLAYER+0x98: b'\xff'}))
    for n in range(4):
        v = [rng.uniform(-1, 1) for _ in range(3)]
        variations.append((f'previous light {n}', {0x817FF0: struct.pack('<4f', *v, 0.0)}))
    global GATE_BASE
    GATE_BASE = (elf, lib, ram, scratch)
    for label, case, delta in parallel_map(gate_case, variations):
        cases.append(case)
        merge_stats(stats, delta)
    report['gate_cases'] = cases
    stats['gate_cases'] = len(cases)


GATE_BASE = None


def gate_case(item):
    """One gate variation on both sides (a parallel_map worker)."""
    label, patch = item
    elf, lib, ram, scratch = GATE_BASE
    ctx = u32(ram, CONTEXT_PTR)
    stats = {}
    if True:
        mod = bytearray(ram)
        for a, b in patch.items(): mod[a:a+len(b)] = b
        mod = bytes(mod)
        o, end, _ = execute(elf, mod, scratch, 0x1F00000)
        scene = scene_view(mod, scratch)
        nat = NativeRun(lib, mod, scene)
        p = nat.plan
        emitted = (end-0x1F00000)//16
        assert nat.fault.code == 0, (label, nat.fault.code, hex(nat.fault.address))
        assert bool(p.drawn) == (emitted > 0), (label, p.drawn, emitted)
        if p.drawn:
            eq(label+' F20', fbytes(p.view_817F20), o.read(0x817F20, 64), stats, 'gate_bytes')
            eq(label+' 24B0', fbytes(p.uv_24B0), o.read(ctx+0x24B0, 64), stats, 'gate_bytes')
            eq(label+' FA0', fbytes(p.far_817FA0), o.read(0x817FA0, 16), stats, 'gate_bytes')
            eq(label+' FB0', fbytes(p.near_817FB0), o.read(0x817FB0, 16), stats, 'gate_bytes')
            eq(label+' FC0', fbytes(p.cross_817FC0), o.read(0x817FC0, 16), stats, 'gate_bytes')
            watched = [c[1] for c in o.calls_seen if c[0] == 0x1D4FB0][2:]
            assert watched == [object_address(mod, p.receiver[i].id) for i in range(p.receiver_count)], label
        case = {'case': label, 'drawn': p.drawn, 'emitted_qwords': emitted,
                'alpha_matrix': p.alpha_matrix, 'far_index': p.far_index, 'near_index': p.near_index,
                'receivers': p.receiver_count}
    return label, case, stats


def area_switch(elf, lib, ram, scratch, stats):
    """Every key of 001D98A0's area switch (0x1D9C44..0x1D9E78), read from
    the ELF's key-load / compare-branch pairs, through the native module;
    plus every other key 0x0000..0x17FF, which must select no alpha matrix."""
    word = lambda a: u32(elf, a-0x100000+0x300)
    targets = {0x1D9E98: 2, 0x1D9E7C: 1}
    keys = {}
    for b in range(0x1D9C4C, 0x1D9E78, 4):
        if word(b) >> 16 != 0x1043: continue                 # branch if r2 == r3
        a = max(x for x in range(b-24, b, 4) if word(x) >> 16 == 0x2403)   # r3 = imm (the key)
        keys[word(a) & 0xFFFF] = targets[b+4+(signed(word(b) & 0xFFFF, 16) << 2)]
    assert word(0x1D9CBC) >> 26 == 4 and word(0x1D9CBC) >> 16 & 31 == 0   # branch-if-zero -> key 0
    keys[0] = targets[0x1D9CC0+(signed(word(0x1D9CBC) & 0xFFFF, 16) << 2)]
    assert len(keys) == 45, len(keys)
    scene = scene_view(ram, scratch)
    checked = 0
    # quick mode: every listed key, its neighbours and both ends of the
    # range, plus a fixed-seed sample of the other keys (EM_TEST_FULL=1:
    # all 6,144)
    edges = {0, 0x17FF} | {k+d for k in keys for d in (-1, 1) if 0 <= k+d < 0x1800}
    sweep = select(range(0x1800), 256, 0x1D98A0, keep=lambda i, k: k in keys or k in edges)
    for key in sweep:
        mod = bytearray(ram); mod[0x810700] = key >> 8; mod[0x810701] = key & 255
        scene.area_700, scene.sub_701 = key >> 8, key & 255
        nat = NativeRun(lib, bytes(mod), scene)
        assert nat.plan.alpha_matrix == keys.get(key, 0), (hex(key), nat.plan.alpha_matrix)
        checked += 1
    stats['area_keys_checked'] = checked
    stats['area_keys_total'] = 0x1800
    stats['area_keys_listed'] = len(keys)


def rotation_probe(elf, stats):
    """00102A60 / 00102BB0 / 00102B08 with angle 0 on a signed-zero probe
    matrix equal a VU0 product with the exact identity."""
    probe = struct.pack('<16f', -0.0, 1.5, -2.0, 0.0, 0.0, -0.0, 3.0, -1.0,
                        -4.0, 0.0, -0.0, 2.0, 5.0, -6.0, 7.0, 1.0)
    image = bytearray(0x800000)
    image[0x100000:0x100000+len(elf)-0x300] = elf[0x300:]
    image = bytes(image)
    for fn in (0x102A60, 0x102BB0, 0x102B08):
        o = ShadowRam(elf, image)
        o.write(0x700000, probe)
        o.run(fn, [0x700000, 0x700000], floats=[0.0])
        got = o.read(0x700000, 64)
        m = list(struct.unpack('<16f', probe))
        want = []
        for row in range(4):
            v = m[4*row:4*row+4]
            ident = [1.0 if i == j else 0.0 for i in range(4) for j in range(4)]
            for lane in range(4):
                acc = fp(ident[lane]*v[0]); acc = fp(acc+fp(ident[4+lane]*v[1]))
                acc = fp(acc+fp(ident[8+lane]*v[2])); want.append(fp(acc+fp(ident[12+lane]*v[3])))
        assert got == struct.pack('<16f', *want), (hex(fn), got.hex())
        stats['rotation_probe_bytes'] = stats.get('rotation_probe_bytes', 0)+64


def gs_checks(report, stats):
    sys.path.insert(0, str(DECOMP/'tools'))
    from gs_vram import read_localmem
    import importlib.util
    spec = importlib.util.spec_from_file_location('est', DECOMP/'tools/extract_subtextures.py')
    est = importlib.util.module_from_spec(spec); spec.loader.exec_module(est)
    out = []
    for name in ('roger-encounter', 'status-hub', 'panel'):
        path = REF/name/'gs.bin'
        if not path.exists(): continue
        _, lm = read_localmem(path)
        hist = {}
        for y in range(128):
            for x in range(128):
                a = 0x258000+est.psmct32_word(x, y, 2)*4
                word = u32(lm, a)
                hist[word] = hist.get(word, 0)+1
        out.append({'gs_dump': name, 'target_texels': {hex(k): v for k, v in sorted(hist.items())}})
        stats['gs_target_texels'] = stats.get('gs_target_texels', 0)+16384
    report['gs_target'] = out


def state_blocks(ram, stats):
    """The fixed GS packets the chain references, decoded from the capture."""
    def ad(block, count, skip=2):
        regs = {}
        for i in range(count):
            lo, hi = struct.unpack_from('<QQ', ram, block+16*(skip+i))
            regs.setdefault(hi & 0xFF, []).append(lo)
        return regs
    target = ad(0x817E20, 14)
    assert target[0x4C] == [0x2012C] and target[0x18] == [0x7C0000007C00] and \
        target[0x40] == [0x7F0000007F0000] and target[0x01] == [0x3F80000000808080] and \
        target[0x05] == [0x7C007C00, 0x84008400] and target[0x00] == [6], target
    assert (0x2012C & 0x1FF) * 8192 == 0x258000 and (0x2012C >> 16 & 0x3F) * 64 == 128
    alpha_only = ad(STATE_2_9, 7)
    assert alpha_only[0x47] == [0x51001] and alpha_only[0x42] == [0x80000000A9] and \
        alpha_only[0x4E] == [0x101000070], alpha_only
    receiver = ad(0x815C60, 7)
    assert receiver[0x47] == [0x5C00D] and receiver[0x42] == [0x44] and receiver[0x14] == [0x60] and \
        receiver[0x4E] == [0x101000070], receiver
    clamp = ad(0x8146C0, 2)
    assert clamp[0x08] == [5], clamp
    silhouette = ad(0x814DC0, 7)
    assert silhouette[0x47] == [0x3000D] and silhouette[0x42] == [0x80000000A8], silhouette
    stats['state_registers_checked'] = 14+7+7+2+7


# ------------------------------------------------------------- GS side ----
# F. The GS side (src/gfx/metal/em_shadow_gs.h, the Metal backend's
#    em_gfx_shadow_*): every executed chain is replayed as the DMAC/VIF1/GIF
#    would, from the head of its display list, and the GS state in force at
#    each draw of the chain must be the header's; the box, silhouette and
#    receiver kernels run as original VU1 instructions on every batch and
#    every kicked vertex word must equal the header's functions.
GS_SHIM = r'''
#include "gfx/metal/em_shadow_gs.h"
#include <stdint.h>
int pass_state(int pass, uint64_t *o)
{
    EmShadowGsState s;
    int r = em_shadow_gs_pass_state(pass, &s);
    uint64_t v[13] = { s.prim, s.regs, s.nloop, s.frame, s.zbuf, s.xyoffset,
                       s.scissor, s.test, s.alpha, s.tex0, s.tex1, s.clamp, s.rgbaq };
    for (int i = 0; i < 13; ++i) o[i] = v[i];
    return r;
}
const char *unsupported(int pass, const uint64_t *v)
{
    EmShadowGsState s = { v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8],
                          v[9], v[10], v[11], v[12] };
    const char *w = em_shadow_gs_unsupported(pass, &s);
    return w ? w : "";
}
void rows(int object, float *k1022, float *k1023)
{
    if (object) em_shadow_gs_object_rows(k1022, k1023);
    else em_shadow_gs_level_rows(k1022, k1023);
}
void coefficients(float n, float f, float *o) { em_fog_gs_coefficients(n, f, o); }
static void put(const EmShadowGsVertex *v, int32_t *w, uint32_t *why)
{
    for (int k = 0; k < 4; ++k) w[k] = v->w[k];
    *why = v->why;
}
unsigned level_batch(const float *m, const float *k1021, const float *k1022,
                     const float *k1023, const float *qw3, unsigned n,
                     int32_t *w, uint32_t *why)
{
    EmShadowGsVertex out[32];
    unsigned stale = em_shadow_gs_level_batch(m, k1021, k1022, k1023,
                                              (const float (*)[4])qw3, n, out);
    for (unsigned i = 0; i < n; ++i) put(&out[i], w + 4 * i, why + i);
    return stale;
}
void object_batch(const float *bones, const float *k1021, const float *k1022,
                  const float *k1023, const float *qw3, unsigned n,
                  int32_t *w, uint32_t *why)
{
    const float *b[32];
    EmShadowGsVertex out[32];
    for (unsigned i = 0; i < n; ++i) b[i] = bones + 16 * i;
    em_shadow_gs_object_batch(b, k1021, k1022, k1023, (const float (*)[4])qw3, n, out);
    for (unsigned i = 0; i < n; ++i) put(&out[i], w + 4 * i, why + i);
}
void receiver_batch(const float *camera, const float *uv, const float *k1021,
                    const float *k1022, const float *k1023, const float *qw3,
                    unsigned n, float *stq, uint32_t *a, int32_t *w, uint32_t *why)
{
    EmShadowGsReceiverVertex out[32];
    em_shadow_gs_receiver_batch(camera, uv, k1021, k1022, k1023,
                                (const float (*)[4])qw3, n, out);
    for (unsigned i = 0; i < n; ++i) {
        stq[3 * i] = out[i].s; stq[3 * i + 1] = out[i].t; stq[3 * i + 2] = out[i].q;
        a[i] = out[i].a;
        put(&out[i].xyzf, w + 4 * i, why + i);
    }
}
void bone(const float *node, const float *vp, float *out) { em_shadow_gs_bone(node, vp, out); }
unsigned needs_clip(unsigned why, unsigned i) { return em_shadow_gs_needs_clip(why, i); }
/* src/game/em_vu1_shadow_clip.h (through em_shadow_gs.h) */
int clip_run(int kernel, EmVu1Qword *dmem, unsigned top, EmVu1ClipResult *out)
{ return em_vu1_shadow_clip_run(kernel, dmem, top, out); }
void clip_template(int kernel, EmVu1Qword *rows) { em_shadow_gs_clip_template(kernel, rows); }
int clip_dmem(int kernel, const float *camera, const float *st, const float *k1021,
              const float *qw3, unsigned top, EmVu1Qword *dmem)
{ return em_shadow_gs_clip_dmem(kernel, camera, st, k1021, (const float (*)[4])qw3, top, dmem); }
int clip_vertices(int kernel, const EmVu1ClipResult *r, EmShadowGsClipVertex *out, unsigned cap)
{ return em_shadow_gs_clip_vertices(kernel, r, out, cap); }
int clip_unproject(const float *cam, float x, float y, float w, float *p)
{ return em_shadow_gs_clip_unproject(cam, x, y, w, p); }
unsigned clip_top(void) { return EM_SHADOW_GS_CLIP_TOP; }
unsigned bilinear(float u, float v, const uint8_t *alpha)
{ return em_shadow_gs_bilinear_alpha(u, v, alpha); }
int pixel(unsigned at, unsigned a, unsigned f, const unsigned *fogcol,
          const uint8_t *dst, uint8_t *out)
{ return em_shadow_gs_receiver_pixel(at, a, f, fogcol, dst, out); }
'''

PASSES = ('alpha clear', 'box', 'target clear', 'silhouette', 'receiver')
STATE_FIELDS = ('prim', 'regs', 'nloop', 'frame', 'zbuf', 'xyoffset', 'scissor', 'test',
                'alpha', 'tex0', 'tex1', 'clamp', 'rgbaq')
SIL_KERNEL = 0x23C750
# CALLed kernel packet -> (MPG program start, instruction count); the VIF MPG
# code in front of each program is checked by check_mpg.
KERNEL_MPG = {BOX_KERNEL: (0x2371B0, 79), SIL_KERNEL: (0x23C780, 62),
              RECEIVER_KERNEL: (0x23C230, 70)}
# The clip kernels' micro address reached only for a triangle that passed
# every skip test of the loop (data ADC, the 18-bit clip-history AND test, the six clip-flag OR
# plane tests, i >= 2): 0023E8A0 xtop at 0x070, 00239C90 xtop at 0x06E.
CLIP_ENTRY = {CLIP_KERNEL: 0x070*8, BOX_CLIP_KERNEL: 0x06E*8}
PROXY_EMDL = ROOT/'assets/player_shadow.emdl'
GSLIB = None
METAL_CASES = []   # (capture, ram, silhouette vp, kicked XY triangles)


class ClipKick(C.Structure):
    _fields_ = [('addr', C.c_uint32), ('vertex', C.c_uint32), ('first', C.c_uint32), ('count', C.c_uint32)]


CLIP_MAX_QWORDS = 30*(1+84)+1


class ClipResult(C.Structure):
    """EmVu1ClipResult (src/game/em_vu1_shadow_clip.h)."""
    _fields_ = [('fault', C.c_uint32), ('entries', C.c_uint32), ('entry', C.c_uint8*32),
                ('kicks', C.c_uint32), ('qwords', C.c_uint32), ('kick', ClipKick*61),
                ('qw', C.c_uint32*(4*CLIP_MAX_QWORDS))]

    def kicked(self):
        """[(dmem address, packet bytes)] in kick order."""
        raw = bytes(self.qw)
        return [(self.kick[i].addr, raw[16*self.kick[i].first:16*(self.kick[i].first+self.kick[i].count)])
                for i in range(self.kicks)]


class ClipVertex(C.Structure):
    _fields_ = [('x', C.c_float), ('y', C.c_float), ('z', C.c_uint32), ('f', C.c_uint32),
                ('s', C.c_float), ('t', C.c_float), ('q', C.c_float), ('a', C.c_uint32),
                ('kq', C.c_float)]


def gs_library(out):
    src, lib_path = out/'shadow_gs.c', out/'shadow_gs.dylib'
    src.write_text(GS_SHIM)
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-shared', '-fPIC', '-I'+str(ROOT/'src'), str(src),
                    str(ROOT/'src/game/em_packet_chain_original.c'),
                    str(ROOT/'src/game/em_status_ui_leftovers.c'), '-o', str(lib_path)], check=True)
    lib = C.CDLL(str(lib_path))
    lib.unsupported.restype = C.c_char_p
    lib.bilinear.argtypes = [C.c_float, C.c_float, C.c_char_p]
    lib.coefficients.argtypes = [C.c_float, C.c_float, C.POINTER(C.c_float)]
    lib.clip_run.argtypes = [C.c_int, C.c_char_p, C.c_uint, C.POINTER(ClipResult)]
    lib.clip_template.argtypes = [C.c_int, C.c_char_p]
    lib.clip_dmem.argtypes = [C.c_int, C.POINTER(C.c_float), C.POINTER(C.c_float),
                              C.POINTER(C.c_float), C.POINTER(C.c_float), C.c_uint, C.c_char_p]
    lib.clip_vertices.argtypes = [C.c_int, C.POINTER(ClipResult), C.POINTER(ClipVertex), C.c_uint]
    lib.clip_unproject.argtypes = [C.POINTER(C.c_float), C.c_float, C.c_float, C.c_float,
                                   C.POINTER(C.c_float)]
    return lib


def check_mpg(elf):
    """The VIF MPG code in front of each program: load address 0, the
    instruction count KERNEL_MPG names."""
    for kernel, (start, count) in KERNEL_MPG.items():
        code = u32(elf, start-4-0x100000+0x300)
        assert (code >> 24) & 0x7F == 0x4A and (code >> 16) & 0xFF == count and code & 0xFFFF == 0, \
            (hex(kernel), hex(code))


def fbits(values): return struct.pack(f'<{len(values)}f', *values)


MAXF = 0x7F7FFFFF


def vfp(value):
    """A VU FMAC result: truncated binary32, denormals flushed to (signed)
    zero, overflow clamped to +-max. Returns (value, overflow, underflow)."""
    if value != value: raise AssertionError('VU NaN')
    r = fp(value) if abs(value) < 3.5e38 else value
    b = bits(r) if abs(r) < float('inf') else (0x7F800000 | (0x80000000 if r < 0 else 0))
    if b & 0x7F800000 == 0x7F800000:
        return number((b & 0x80000000) | MAXF), 1, 0
    if b & 0x7F800000 == 0 and b & 0x7FFFFF:
        return number(b & 0x80000000), 0, 1
    return r, 0, 0


def vnum(word):
    """A VU register word as a number (denormals read as zero, the
    IEEE inf/NaN encodings as the largest magnitude)."""
    if word & 0x7F800000 == 0: return number(word & 0x80000000)
    if word & 0x7F800000 == 0x7F800000: return number((word & 0x80000000) | MAXF)
    return number(word)


FIELDS = lambda mask: [c for c in range(4) if mask & (8 >> c)]


class VU1:
    """VU1 interpreter for the programs the shadow chain CALLs: the box
    (00237180), silhouette (0023C750) and receiver (0023C200) kernels and
    the two guard-band clip kernels (00239C90, 0023E8A0; 1183 and 1235
    instructions in five MPG blocks each). One persistent machine: the
    VIF's MPG uploads fill a 16 KiB micro memory and MSCAL/MSCNT run from
    it. Model (in-order issue, one pair per cycle): a VF operand whose
    field is still in the 4-cycle FMAC/load pipeline stalls issue; the MAC,
    status and clip flags an FMAC/CLIP op produces are visible to flag
    instructions issued 4 cycles later; Q 7 cycles after DIV (WAITQ and a
    second DIV stall for it); a pair's lower op reads the registers before
    its upper op writes them; binary32 results truncated, denormals
    flushed, overflow clamped with the O flag. XGKICK snapshots the GIF
    packet at the kick. Any instruction outside the implemented set
    raises."""

    def __init__(self, elf):
        self.elf = elf
        self.code = bytearray(16384)
        self.mem = bytearray(16384)
        self.v = [[0, 0, 0, 0] for _ in range(32)]; self.v[0] = [0, 0, 0, bits(1.0)]
        self.vi = [0]*16
        self.acc = [0.0]*4
        self.q = 0.0; self.i = 0.0; self.cf = 0; self.status = 0; self.mac = 0; self.cycle = 0
        self.pending = []; self.seq = 0
        self.ready = [[0]*4 for _ in range(32)]; self.q_ready = 0
        self.top = 0; self.kicks = []; self.resume = None
        self.executed = 0
        self.watch = set(); self.events = []   # micro addresses -> ('pc', pc, vi11)
        self.ftoi_out = 0   # FTOI results outside int32 (wrapped here; not established)

    def rd(self, a): return list(struct.unpack_from('<4I', self.mem, (a & 1023)*16))
    def wr(self, a, x): struct.pack_into('<4I', self.mem, (a & 1023)*16, *x)

    def later(self, delay, kind, value):
        self.seq += 1
        self.pending.append((self.cycle+delay, self.seq, kind, value))

    def settle(self):
        due = sorted(p for p in self.pending if p[0] <= self.cycle)
        for p in due:
            _, _, kind, value = p
            if kind == 'cf': self.cf = ((self.cf << 6) | value) & 0xFFFFFF
            elif kind == 'flags':
                self.mac, st = value
                self.status = (self.status & 0xFC0) | st | (st & 0xF) << 6
            elif kind == 'q': self.q = value
            self.pending.remove(p)

    # --- operand fields each op reads (for the stall model)
    @staticmethod
    def upper_reads(up):
        code, fs, ft, mask, op = up & 0x7FF, up >> 11 & 31, up >> 16 & 31, up >> 21 & 15, up & 63
        m = FIELDS(mask)
        if code == 0x2FF: return []
        if code == 0x1FF: return [(fs, c) for c in range(3)] + [(ft, 3)]
        if (up & 0x3C) == 0x3C:
            hi = code >> 6
            if hi in (0x00, 0x01, 0x02, 0x03, 0x06):             # ADDA/SUBA/MADDA/MSUBA/MULA bc
                return [(fs, c) for c in m] + [(ft, code & 3)]
            if code in (0x2FE,) or code == 0x2FE: return [(fs, c) for c in range(3)] + [(ft, c) for c in range(3)]
            if code in (0x2BC, 0x2BD, 0x2BE, 0x2FC, 0x2FD):      # ADDA MADDA MULA SUBA MSUBA
                return [(fs, c) for c in m] + [(ft, c) for c in m]
            return [(fs, c) for c in m]                           # ftoi/itof/abs, q/i acc ops
        if op < 0x1C: return [(fs, c) for c in m] + [(ft, op & 3)]
        if op < 0x28: return [(fs, c) for c in m]
        if op == 0x2E: return [(fs, c) for c in range(3)] + [(ft, c) for c in range(3)]
        return [(fs, c) for c in m] + [(ft, c) for c in m]

    @staticmethod
    def lower_reads(lo):
        op, it, iss, mask = lo >> 25, lo >> 16 & 31, lo >> 11 & 31, lo >> 21 & 15
        if op == 0x01: return [(iss, c) for c in FIELDS(mask)]
        if op != 0x40: return []
        fn = lo & 0x7FF
        if fn in (0x33C, 0x37D, 0x37F): return [(iss, c) for c in FIELDS(mask)]
        if fn == 0x33D: return [(iss, (c+1) & 3) for c in FIELDS(mask)]
        if fn in (0x3BC, 0x3BE): return [(iss, lo >> 21 & 3), (it, lo >> 23 & 3)]
        if fn == 0x3BD: return [(it, lo >> 23 & 3)]
        if fn == 0x3FC: return [(iss, lo >> 21 & 3)]
        return []

    def upper(self, pc, up):
        """Returns the deferred register writes [(reg, field, word)]."""
        code, fs, ft, fd, mask = up & 0x7FF, up >> 11 & 31, up >> 16 & 31, up >> 6 & 31, up >> 21 & 15
        op = up & 63
        x = list(map(vnum, self.v[fs])); y = list(map(vnum, self.v[ft]))
        res, dest, accw, intw, flags = None, fd, False, False, True
        if code == 0x2FF: return []
        if code == 0x1FF:                                  # clip fs.xyz, ft.w
            w = abs(y[3]); f = 0
            for c in range(3):
                if x[c] > w: f |= 1 << (2*c)
                if x[c] < -w: f |= 2 << (2*c)
            self.later(4, 'cf', f); return []
        acc = self.acc
        if (up & 0x3C) == 0x3C:
            if code in (0x17C, 0x17D, 0x17E, 0x17F):      # ftoi0/4/12/15
                scale = (1, 16, 4096, 32768)[code & 3]
                # out-of-range results wrap here (the header saturates;
                # the hardware result is not established, compare_words)
                res = [math.trunc(v*scale) & 0xFFFFFFFF for v in x]
                self.ftoi_out += sum(1 for c in FIELDS(mask) if not -2**31 <= math.trunc(x[c]*scale) < 2**31)
                dest = ft; intw = True; flags = False
            elif code in (0x13C, 0x13D, 0x13E, 0x13F):    # itof0/4/12/15
                scale = (1, 16, 4096, 32768)[code & 3]
                res = [fp(signed(w)/scale) for w in self.v[fs]]; dest = ft; flags = False
            elif code == 0x1FD:                            # abs
                res = [abs(a) for a in x]; dest = ft; flags = False
            elif 0x03C <= code <= 0x03F: res = [a+y[code & 3] for a in x]; accw = True
            elif 0x07C <= code <= 0x07F: res = [a-y[code & 3] for a in x]; accw = True
            elif 0x0BC <= code <= 0x0BF: res = [acc[c]+fp(x[c]*y[code & 3]) for c in range(4)]; accw = True
            elif 0x0FC <= code <= 0x0FF: res = [acc[c]-fp(x[c]*y[code & 3]) for c in range(4)]; accw = True
            elif 0x1BC <= code <= 0x1BF: res = [a*y[code & 3] for a in x]; accw = True
            elif code == 0x1FC: res = [a*self.q for a in x]; accw = True
            elif code == 0x1FE: res = [a*self.i for a in x]; accw = True
            elif code == 0x2BC: res = [a+b for a, b in zip(x, y)]; accw = True
            elif code == 0x2BD: res = [acc[c]+fp(x[c]*y[c]) for c in range(4)]; accw = True
            elif code == 0x2BE: res = [a*b for a, b in zip(x, y)]; accw = True
            elif code == 0x2FC: res = [a-b for a, b in zip(x, y)]; accw = True
            elif code == 0x2FD: res = [acc[c]-fp(x[c]*y[c]) for c in range(4)]; accw = True
            elif code == 0x2FE:                            # opmula
                res = [x[1]*y[2], x[2]*y[0], x[0]*y[1], 0.0]; accw = True
            else: raise AssertionError(('VU upper special', hex(pc), hex(up)))
        elif op < 4: res = [a+y[op] for a in x]
        elif op < 8: res = [a-y[op & 3] for a in x]
        elif op < 12: res = [acc[c]+fp(x[c]*y[op & 3]) for c in range(4)]
        elif op < 16: res = [acc[c]-fp(x[c]*y[op & 3]) for c in range(4)]
        elif op < 20: res = [max(a, y[op & 3]) for a in x]; flags = False
        elif op < 24: res = [min(a, y[op & 3]) for a in x]; flags = False
        elif op < 28: res = [a*y[op & 3] for a in x]
        elif op == 0x1C: res = [a*self.q for a in x]
        elif op == 0x1D: res = [max(a, self.i) for a in x]; flags = False
        elif op == 0x1E: res = [a*self.i for a in x]
        elif op == 0x1F: res = [min(a, self.i) for a in x]; flags = False
        elif op == 0x20: res = [a+self.q for a in x]
        elif op == 0x21: res = [acc[c]+fp(x[c]*self.q) for c in range(4)]
        elif op == 0x22: res = [a+self.i for a in x]
        elif op == 0x23: res = [acc[c]+fp(x[c]*self.i) for c in range(4)]
        elif op == 0x24: res = [a-self.q for a in x]
        elif op == 0x25: res = [acc[c]-fp(x[c]*self.q) for c in range(4)]
        elif op == 0x26: res = [a-self.i for a in x]
        elif op == 0x27: res = [acc[c]-fp(x[c]*self.i) for c in range(4)]
        elif op == 0x28: res = [a+b for a, b in zip(x, y)]
        elif op == 0x29: res = [acc[c]+fp(x[c]*y[c]) for c in range(4)]
        elif op == 0x2A: res = [a*b for a, b in zip(x, y)]
        elif op == 0x2B: res = [max(a, b) for a, b in zip(x, y)]; flags = False
        elif op == 0x2C: res = [a-b for a, b in zip(x, y)]
        elif op == 0x2D: res = [acc[c]-fp(x[c]*y[c]) for c in range(4)]
        elif op == 0x2E:                                   # opmsub
            res = [acc[0]-fp(x[1]*y[2]), acc[1]-fp(x[2]*y[0]), acc[2]-fp(x[0]*y[1]), 0.0]
        elif op == 0x2F: res = [min(a, b) for a, b in zip(x, y)]; flags = False
        else: raise AssertionError(('VU upper', hex(pc), hex(up)))
        mac, writes = 0, []
        for c in FIELDS(mask):
            if intw: word = res[c]
            else:
                value, o, u = vfp(res[c]) if flags else (res[c], 0, 0)
                word = bits(value)
                if flags:
                    if value == 0: mac |= 1 << (3-c)
                    if word >> 31: mac |= 1 << (7-c)
                    if u: mac |= 1 << (11-c)
                    if o: mac |= 1 << (15-c)
            if accw: self.acc[c] = number(word)
            elif dest: writes.append((dest, c, word))
        if flags:
            st = int(bool(mac & 0xF)) | int(bool(mac & 0xF0)) << 1 | int(bool(mac & 0xF00)) << 2 | \
                int(bool(mac & 0xF000)) << 3
            self.later(4, 'flags', (mac, st))
        return writes

    def branch_if(self, cond, pc, imm11):
        return pc+8+imm11*8 if cond else None

    def lower(self, pc, lo):
        op, it, iss, idd = lo >> 25, lo >> 16 & 31, lo >> 11 & 31, lo >> 6 & 31
        mask, imm11 = lo >> 21 & 15, signed(lo & 0x7FF, 11)
        imm15 = (lo & 0x7FF) | (lo >> 21 & 15) << 11
        vi, s16 = self.vi, lambda r: signed(self.vi[r], 16)
        it &= 15 if op != 0x00 else 31
        nxt = None
        if lo == 0x8000033C: return None
        if op == 0x00:
            q = self.rd(vi[iss & 15]+imm11)
            for c in FIELDS(mask):
                if it: self.v[it][c] = q[c]; self.ready[it][c] = self.cycle+4
        elif op == 0x01:
            a = vi[lo >> 16 & 15]+imm11; q = self.rd(a)
            for c in FIELDS(mask): q[c] = self.v[iss][c]
            self.wr(a, q)
        elif op == 0x04:
            q = self.rd(vi[iss & 15]+imm11)
            for c in FIELDS(mask): vi[it] = q[c] & 0xFFFF
        elif op == 0x05:
            a = vi[iss & 15]+imm11; q = self.rd(a)
            for c in FIELDS(mask): q[c] = vi[it] & 0xFFFF
            self.wr(a, q)
        elif op == 0x08: vi[it] = (vi[iss & 15]+imm15) & 0xFFFF
        elif op == 0x09: vi[it] = (vi[iss & 15]-imm15) & 0xFFFF
        elif op == 0x10: vi[1] = int(self.cf == lo & 0xFFFFFF)
        elif op == 0x11: self.cf = lo & 0xFFFFFF
        elif op == 0x12: vi[1] = int(bool(self.cf & lo & 0xFFFFFF))
        elif op == 0x13: vi[1] = int((self.cf | lo) & 0xFFFFFF == 0xFFFFFF)
        elif op == 0x1C: vi[it] = self.cf & 0xFFF
        elif op == 0x16: vi[it] = self.status & ((lo & 0x7FF) | (lo >> 21 & 1) << 11)
        elif op == 0x18: vi[it] = int(vi[iss & 15] == self.mac)
        elif op == 0x1A: vi[it] = vi[iss & 15] & self.mac
        elif op == 0x1B: vi[it] = vi[iss & 15] | self.mac
        elif op == 0x20: nxt = pc+8+imm11*8
        elif op == 0x21: vi[it] = (pc+16)//8; nxt = pc+8+imm11*8
        elif op == 0x24: nxt = vi[iss & 15]*8
        elif op == 0x25: t = vi[iss & 15]*8; vi[it] = (pc+16)//8; nxt = t
        elif op == 0x28: nxt = self.branch_if(vi[iss & 15] == vi[it], pc, imm11)
        elif op == 0x29: nxt = self.branch_if(vi[iss & 15] != vi[it], pc, imm11)
        elif op == 0x2C: nxt = self.branch_if(s16(iss & 15) < 0, pc, imm11)
        elif op == 0x2D: nxt = self.branch_if(s16(iss & 15) > 0, pc, imm11)
        elif op == 0x2E: nxt = self.branch_if(s16(iss & 15) <= 0, pc, imm11)
        elif op == 0x2F: nxt = self.branch_if(s16(iss & 15) >= 0, pc, imm11)
        elif op == 0x40:
            fn, sub = lo & 0x7FF, lo & 63
            s, t, d = iss & 15, it & 15, idd & 15
            if sub == 0x30: vi[d] = (vi[s]+vi[t]) & 0xFFFF
            elif sub == 0x31: vi[d] = (vi[s]-vi[t]) & 0xFFFF
            elif sub == 0x32: vi[t] = (vi[s]+signed(idd, 5)) & 0xFFFF
            elif sub == 0x34: vi[d] = vi[s] & vi[t]
            elif sub == 0x35: vi[d] = vi[s] | vi[t]
            elif fn == 0x33C:
                full = lo >> 16 & 31
                for c in FIELDS(mask):
                    if full: self.v[full][c] = self.v[iss][c]; self.ready[full][c] = self.cycle+4
            elif fn == 0x33D:
                full = lo >> 16 & 31; src = self.v[iss][:]
                for c in FIELDS(mask):
                    if full: self.v[full][c] = src[(c+1) & 3]; self.ready[full][c] = self.cycle+4
            elif fn in (0x3BC, 0x3BD, 0x3BE):
                a = vnum(self.v[iss][lo >> 21 & 3]); b = vnum(self.v[lo >> 16 & 31][lo >> 23 & 3])
                if fn == 0x3BD: r = fp(math.sqrt(abs(b)))
                else:
                    den = math.sqrt(abs(b)) if fn == 0x3BE else b
                    if den == 0:
                        neg = (math.copysign(1, a) < 0) != (math.copysign(1, b) < 0 and fn == 0x3BC)
                        r = number((0x80000000 if neg else 0) | MAXF)
                    else: r = vfp(a/den)[0]
                self.later(7 if fn == 0x3BC else (7 if fn == 0x3BD else 13), 'q', r)
                self.q_ready = self.cycle+(13 if fn == 0x3BE else 7)
            elif fn == 0x3BF: pass
            elif fn == 0x3FC: vi[t] = self.v[iss][lo >> 21 & 3] & 0xFFFF
            elif fn == 0x3FD:
                full = lo >> 16 & 31
                for c in FIELDS(mask):
                    if full: self.v[full][c] = signed(vi[s], 16) & 0xFFFFFFFF; self.ready[full][c] = self.cycle+4
            elif fn == 0x6BC: vi[t] = self.top
            elif fn == 0x6FC:
                self.kicks.append((vi[s], gif_packets(self.mem, vi[s])))
                self.events.append(('kick', vi[s], self.kicks[-1][1], gif_raw(self.mem, vi[s])))
            else: raise AssertionError(('VU lower special', hex(pc), hex(lo)))
        else: raise AssertionError(('VU lower', hex(pc), hex(lo)))
        return nxt

    def run(self, entry):
        pc, branch, end_after = entry & 0x3FFF, None, None
        for _ in range(400000):
            lo, up = struct.unpack_from('<II', self.code, pc & 0x3FFF)
            if pc in self.watch: self.events.append(('pc', pc, self.vi[11]))
            # stalls: VF operands still in the pipeline, WAITQ / DIV on Q
            reads = self.upper_reads(up) + ([] if up >> 31 else self.lower_reads(lo))
            wait = max([self.ready[r][c] for r, c in reads if r] + [self.cycle])
            if not up >> 31 and lo >> 25 == 0x40 and (lo & 0x7FF) in (0x3BF, 0x3BC, 0x3BD, 0x3BE):
                wait = max(wait, self.q_ready)
            self.cycle = wait
            self.settle()
            nxt = branch if branch is not None else pc+8
            branch = None
            writes = self.upper(pc, up)
            if up >> 31: self.i = vnum(lo)
            else:
                b = self.lower(pc, lo)
                if b is not None: branch = b
            for r, c, word in writes:
                self.v[r][c] = word; self.ready[r][c] = self.cycle+4
            self.vi[0] = 0; self.v[0] = [0, 0, 0, bits(1.0)]
            self.executed += 1
            self.cycle += 1
            if end_after:
                self.resume = nxt
                return
            if up >> 30 & 1: end_after = True
            pc = nxt & 0x3FFF
        raise AssertionError('VU runaway')


ELF_BYTES = [None]


def load_program(vu1, elf, kernel):
    """The MPG blocks of a CALLed kernel packet into vu1.code."""
    w0 = u32(elf, kernel-0x100000+0x300)
    i, e = kernel+16, kernel+16+16*(w0 & 0xFFFF)
    while i < e:
        v = u32(elf, i-0x100000+0x300); cmd = (v >> 24) & 0x7F; num = (v >> 16) & 0xFF
        if cmd == 0x4A:
            n = num or 256; a = i+4-0x100000+0x300
            vu1.code[(v & 0xFFFF)*8:(v & 0xFFFF)*8+8*n] = elf[a:a+8*n]
            i += 4+8*n; continue
        i += 4


def kernel_replay(elf, buf, units):
    """The chain's VIF1 stream replayed on one persistent VU1: a CALLed
    kernel packet's VIF codes are applied (its MPG blocks into micro
    memory, STCYCL, BASE, OFFSET), UNPACK V4-32 with the TOPS double
    buffer, MSCAL/MSCNT run the loaded program. Returns (kernel, top,
    dmem before, kicks [(address, GIF packets)], source, events) per
    MSCAL/MSCNT: source = the REF'd address whose VIF stream held the
    MSCAL (an object's +0x40 data), events = the CLIP_ENTRY hits and the
    kicks in execution order."""
    ELF_BYTES[0] = elf
    vu1 = VU1(elf)
    st = dict(base=0, offset=0, tops=0, dbf=0, cl=1, wl=1)
    kernel, out = None, []

    def vif(i, e):
        while i < e:
            v = struct.unpack_from('<I', src[0], i - src[1])[0]
            cmd = (v >> 24) & 0x7F; imm = v & 0xFFFF; num = (v >> 16) & 0xFF
            if cmd >= 0x60:
                assert cmd & 0x0F == 0x0C and st['wl'] <= st['cl'], ('unpack', hex(v))
                cnt = num or 256
                dst = (imm & 0x3FF) + (st['tops'] if imm & 0x8000 else 0)
                for k in range(cnt):
                    d = (dst + (k // st['wl'])*st['cl'] + k % st['wl']) & 1023
                    a = i+4+16*k - src[1]
                    vu1.mem[d*16:d*16+16] = src[0][a:a+16]
                i += 4+16*cnt; continue
            if cmd == 0x4A:
                n = num or 256
                a = i+4-src[1]
                vu1.code[imm*8:imm*8+8*n] = src[0][a:a+8*n]
                i += 4+8*n; continue
            if cmd in (0x50, 0x51): i = (i+4+15)//16*16+16*imm; continue
            if cmd in (0x14, 0x15, 0x17):
                top = st['tops']; st['dbf'] ^= 1
                st['tops'] = st['base']+(st['offset'] if st['dbf'] else 0)
                entry = vu1.resume if cmd == 0x17 else 8*imm
                before = bytes(vu1.mem)
                vu1.top = top; vu1.kicks = []; vu1.events = []
                vu1.watch = {CLIP_ENTRY[kernel]} if kernel in CLIP_ENTRY else set()
                vu1.run(entry)
                out.append((kernel, top, before, vu1.kicks, source[0], vu1.events))
                i += 4; continue
            if cmd == 0x01: st['cl'], st['wl'] = imm & 0xFF, (imm >> 8) & 0xFF; i += 4; continue
            if cmd == 0x03: st['base'] = imm & 0x3FF; i += 4; continue
            if cmd == 0x02:
                st['offset'] = imm & 0x3FF; st['dbf'] = 0; st['tops'] = st['base']; i += 4; continue
            if cmd == 0x20: i += 8; continue
            if cmd in (0x30, 0x31): i += 20; continue
            if cmd in (0, 4, 5, 6, 7, 0x10, 0x11, 0x13): i += 4; continue
            raise AssertionError(('vif', hex(v), hex(i)))

    src = [buf, 0]
    source = [None]
    for a, tid, qwc, addr in units:
        source[0] = addr if tid == 3 else a
        if tid == 5:
            kernel = addr
            w0 = u32(elf, addr-0x100000+0x300)
            assert (w0 >> 28) & 7 == 1, ('kernel packet tag', hex(addr))
            src[:] = [elf, 0x100000-0x300]
            vif(addr+16, addr+16+16*(w0 & 0xFFFF))
            src[:] = [buf, 0]
            continue
        if tid == 1: vif(a+16, a+16+16*qwc)
        elif tid == 3: vif(addr, addr+16*qwc)
    return out


def gif_raw(mem, at):
    """The bytes of the GIF packet an XGKICK of dmem qword `at` sends: its
    tags and data up to the EOP tag (PACKED NLOOP x NREG, REGLIST (NLOOP x
    NREG + 1) / 2, IMAGE NLOOP qwords)."""
    q = at
    for _ in range(64):
        lo = struct.unpack_from('<Q', mem, (q & 1023)*16)[0]
        nloop, nreg, flg, eop = lo & 0x7FFF, (lo >> 60) or 16, (lo >> 58) & 3, lo >> 15 & 1
        q += 1 + (nloop*nreg if flg == 0 else (nloop*nreg+1)//2 if flg == 1 else nloop)
        if eop:
            return b''.join(bytes(mem[(a & 1023)*16:(a & 1023)*16+16]) for a in range(at, q))
    raise AssertionError('GIF packet without EOP')


def gif_packets(mem, at):
    """The GIF packets of one XGKICK from dmem qword `at`, up to EOP:
    [(prim or None, flg, [ {reg: 16 bytes} per loop ], regs)] (PACKED;
    REGLIST and IMAGE bodies are skipped)."""
    out = []
    for _ in range(64):
        lo, hi = struct.unpack_from('<QQ', mem, (at & 1023)*16)
        nloop, nreg, flg, eop = lo & 0x7FFF, (lo >> 60) or 16, (lo >> 58) & 3, lo >> 15 & 1
        regs = [(hi >> (4*r)) & 15 for r in range(nreg)]
        prim = (lo >> 47) & 0x7FF if (lo >> 46) & 1 else None
        loops, q = [], at+1
        if flg == 0:
            for _ in range(nloop):
                d = {}
                for r in regs:
                    d[r] = bytes(mem[(q & 1023)*16:(q & 1023)*16+16]); q += 1
                loops.append(d)
        elif flg == 1: q += (nloop*nreg+1)//2
        else: q += nloop
        out.append((prim, flg, loops, regs))
        at = q
        if eop: return out
    raise AssertionError('GIF packet without EOP')


def f4a(values): return (C.c_float*len(values))(*values)


def dmem_f(mem, q, n=1): return list(struct.unpack_from(f'<{4*n}f', mem, (q & 1023)*16))


def batch_qw3(mem, top):
    return [dmem_f(mem, top+4*i+3) for i in range(32)]


def compare_words(label, native, kicked, stats, key, why=0):
    """The four kicked XYZF2/XYZ2 words. For a vertex outside the guard band
    (ADC from CLIP, never drawn by the kernel) a screen word beyond the
    int32 range is ftoi4's overflow, which the two models treat differently
    (the header saturates, the interpreter wraps; the hardware result is
    not established): such words are counted, every other word must be
    equal."""
    got = struct.pack('<4i', *native)
    if got != kicked:
        ok = why & 2 and all(g == k or n in (0x7FFFFFFF, -0x80000000)
                             for n, g, k in zip(native[:3], struct.unpack('<3I', got[:12]),
                                                struct.unpack('<3I', kicked[:12]))) \
            and got[12:] == kicked[12:]
        if not ok:
            raise AssertionError((label, got.hex(), kicked.hex()))
        stats['gs_overflow_words_clipped'] = stats.get('gs_overflow_words_clipped', 0) + \
            sum(g != k for g, k in zip(struct.unpack('<3I', got[:12]), struct.unpack('<3I', kicked[:12])))
    stats[key] = stats.get(key, 0)+16


def template_rows(gslib, before, object_rows, name, stats):
    """dmem 1022/1023 of a batch equal the header's rows; 1021 is (255,
    2048, A, B) with AREA11's fog (near -209, far 304)."""
    k1022, k1023 = F4(), F4()
    gslib.rows(1 if object_rows else 0, k1022, k1023)
    eq(f'{name} dmem 1022', fbits(k1022), before[1022*16:1023*16], stats, 'gs_template_bytes')
    eq(f'{name} dmem 1023', fbits(k1023), before[1023*16:1024*16], stats, 'gs_template_bytes')
    coef = (C.c_float*2)()
    gslib.coefficients(-209.0, 304.0, coef)
    eq(f'{name} dmem 1021', fbits([255.0, 2048.0, coef[0], coef[1]]), before[1021*16:1022*16],
       stats, 'gs_template_bytes')
    return k1022, k1023


def kernel_checks(gslib, name, status, ram, batches, plan, stats, entry):
    """Every kicked vertex of the box, silhouette and receiver batches
    against em_shadow_gs.h."""
    counts = {'box': 0, 'box_drawn': 0, 'box_cull': 0, 'box_clip': 0, 'silhouette': 0,
              'silhouette_adc': 0, 'receiver': 0, 'receiver_clip': 0, 'receiver_data_adc': 0}
    whys = {}
    for index, (kernel, top, before, kicks, _, _) in enumerate(batches):
        if kernel not in KERNEL_MPG: continue
        assert len(kicks) == 1 and len(kicks[0][1]) == 1 and len(kicks[0][1][0][2]) == 32, \
            (name, hex(kernel), 'kick')
        loops = kicks[0][1][0][2]
        qw3 = batch_qw3(before, top)
        flat = [x for q in qw3 for x in q]
        words, why = (C.c_int32*128)(), (C.c_uint32*32)()
        k1021 = f4a(dmem_f(before, 1021))
        if kernel == BOX_KERNEL:
            k1022, k1023 = template_rows(gslib, before, False, name+' box', stats)
            stale = gslib.level_batch(f4a(dmem_f(before, 0, 4)), k1021, k1022, k1023,
                                      f4a(flat), 32, words, why)
            assert stale == 0, (name, 'box strip starts without ADC')
            for i in range(32):
                compare_words(f'{name} box XYZF2 {i}', words[4*i:4*i+4], loops[i][4], stats,
                              'gs_box_vertex_bytes', why[i])
                counts['box'] += 1
                if i >= 2 and not why[i]: counts['box_drawn'] += 1
                if why[i] & 4 and not why[i] & 1: counts['box_cull'] += 1
                if why[i] & 2 and not why[i] & 1: counts['box_clip'] += 1
            whys[index] = list(why)
        elif kernel == SIL_KERNEL:
            k1022, k1023 = template_rows(gslib, before, True, name+' silhouette', stats)
            bones = []
            for q in qw3:
                w = struct.unpack('<I', struct.pack('<f', q[3]))[0] & 0xFFFF
                bones += dmem_f(before, w, 4)
            gslib.object_batch(f4a(bones), k1021, k1022, k1023, f4a(flat), 32, words, why)
            for i in range(32):
                compare_words(f'{name} silhouette XYZ2 {i}', words[4*i:4*i+4], loops[i][5], stats,
                              'gs_silhouette_vertex_bytes', why[i])
                counts['silhouette'] += 1
                counts['silhouette_adc'] += bool(why[i] & 2)
        elif kernel == RECEIVER_KERNEL:
            k1022, k1023 = template_rows(gslib, before, False, name+' receiver', stats)
            stq, alpha = (C.c_float*96)(), (C.c_uint32*32)()
            gslib.receiver_batch(f4a(dmem_f(before, 0, 4)), f4a(dmem_f(before, 8, 4)), k1021, k1022,
                                 k1023, f4a(flat), 32, stq, alpha, words, why)
            for i in range(32):
                eq(f'{name} receiver ST {i}', fbits(stq[3*i:3*i+3]), loops[i][2][:12], stats,
                   'gs_receiver_vertex_bytes')
                eq(f'{name} receiver RGBAQ {i}', struct.pack('<3If', 0, 0, 0, 8388608.0+alpha[i]),
                   loops[i][1], stats, 'gs_receiver_vertex_bytes')
                compare_words(f'{name} receiver XYZF2 {i}', words[4*i:4*i+4], loops[i][4], stats,
                              'gs_receiver_vertex_bytes', why[i])
                counts['receiver'] += 1
                counts['receiver_clip'] += bool(why[i] & 2 and not why[i] & 1)
                counts['receiver_data_adc'] += bool(why[i] & 1)
            whys[index] = list(why)
    assert counts['box'] == 64 and counts['silhouette'] > 0 and counts['receiver'] > 0, (name, counts)
    entry['kernel_vertices'] = counts
    for k, v in counts.items(): stats['gs_'+k] = stats.get('gs_'+k, 0)+v
    if status == 'exact':
        # 001C7420's rows (8 qwords per bone from dmem 0) =
        # em_shadow_gs_bone(node + 0x90, silhouette vp)
        sil = [b for b in batches if b[0] == SIL_KERNEL][0][2]
        for b in range(ram[PLAYER+0x0C]):
            node = f4a(struct.unpack_from('<16f', ram, u32(ram, PLAYER+0x110+4*b)+0x90))
            out = F16()
            gslib.bone(node, plan.silhouette_vp, out)
            eq(f'{name} bone {b}', fbits(out), sil[(8*b)*16:(8*b+4)*16], stats, 'gs_bone_bytes')
    return whys


CLIP_ID = {CLIP_KERNEL: 0, BOX_CLIP_KERNEL: 1}          # EM_VU1_CLIP_RECEIVER / _BOX
CLIP_TEMPLATE_ELF = {CLIP_KERNEL: 0x251750, BOX_CLIP_KERNEL: 0x251550}   # +0x10..+0x40


def native_clip(gslib, kernel, mem, top):
    """em_vu1_shadow_clip_run over a copy of `mem`: (rc, result)."""
    buf = C.create_string_buffer(bytes(mem), 16384)
    res = ClipResult()
    rc = gslib.clip_run(CLIP_ID[kernel], buf, top, C.byref(res))
    return rc, res


def clip_drawn(gslib, kernel, res):
    """em_shadow_gs_clip_vertices of a result: the GS vertices, 3 per
    triangle, as tuples (x, y, z, f, s, t, q, a, kq)."""
    out = (ClipVertex*(30*27))()
    n = gslib.clip_vertices(CLIP_ID[kernel], C.byref(res), out, 30*27)
    assert n >= 0, ('clip packet the GS decode refuses', hex(kernel))
    return [(v.x, v.y, v.z, v.f, v.s, v.t, v.q, v.a, v.kq) for v in out[:n]]


def clip_native_checks(gslib, name, kernel, top, before, events, camera, st, stats):
    """The translation (src/game/em_vu1_shadow_clip.h) against the executed
    kernel on one clip batch: the same XGKICKs (dmem address, order, every
    packet byte) and the same clip entries. Then the backend's own dmem
    image (em_shadow_gs_clip_dmem: the plan's matrices, the template, the
    fog row of em_gfx_fog(-209, 304), qword 3 only) must give the same GS
    vertices (em_shadow_gs_clip_vertices): all fields for the receivers,
    X, Y, Z and the kernel's Q for the box (its ST and RGBAQ slots go to NOP
    registers and PRIM 0x043 has FGE 0). Every vertex must unproject
    (em_shadow_gs_clip_unproject) to a point that projects back onto it."""
    label = f'{name} {hex(kernel)} top {top}'
    want = [(ev[1], ev[3]) for ev in events if ev[0] == 'kick']
    entries = [32-ev[2] for ev in events if ev[0] == 'pc']
    rc, res = native_clip(gslib, kernel, before, top)
    assert rc == 0 and res.fault == 0, (label, 'native clip fault', res.fault)
    got = res.kicked()
    if got != want:
        for k, ((ga, gr), (wa, wr)) in enumerate(zip(got, want)):
            if (ga, gr) != (wa, wr):
                raise AssertionError((label, 'kick', k, ga, wa, gr.hex()[:96], wr.hex()[:96]))
        raise AssertionError((label, 'kick count', len(got), len(want)))
    assert list(res.entry[:res.entries]) == entries, (label, 'clip entries', entries)
    rows = C.create_string_buffer(64)
    gslib.clip_template(CLIP_ID[kernel], rows)
    elf_rows = ELF_BYTES[0][CLIP_TEMPLATE_ELF[kernel]+0x10-0x100000+0x300:][:64]
    assert rows.raw == elf_rows == bytes(before[1017*16:1021*16]), (label, 'template rows 1017..1020')
    real = clip_drawn(gslib, kernel, res)
    # the backend's image
    assert bytes(before[:64]) == fbytes(camera), (label, 'dmem 0..3 is not the plan matrix')
    if st is not None:
        assert bytes(before[64:128]) == fbytes(st), (label, 'dmem 4..7 is not ctx+0x24B0')
    coef = (C.c_float*2)()
    gslib.coefficients(-209.0, 304.0, coef)
    k1021 = [255.0, 2048.0, coef[0], coef[1]] if kernel == CLIP_KERNEL else [255.0, 2048.0, 0.0, 0.0]
    qw3 = [x for q in batch_qw3(before, top) for x in q]
    image = C.create_string_buffer(16384)
    assert gslib.clip_dmem(CLIP_ID[kernel], f4a(camera), f4a(st) if st is not None else None, f4a(k1021),
                           f4a(qw3), gslib.clip_top(), image) == 0, (label, 'backend dmem image refused')
    rc, bres = native_clip(gslib, kernel, image.raw, gslib.clip_top())
    assert rc == 0, (label, 'backend image clip fault', bres.fault)
    backend = clip_drawn(gslib, kernel, bres)
    key = (lambda v: v) if kernel == CLIP_KERNEL else (lambda v: v[:3]+v[8:])
    assert [key(v) for v in backend] == [key(v) for v in real], (label, 'backend image draws differently')
    for v in backend:
        pnt = (C.c_float*3)()
        w = 1.0/v[8]
        assert gslib.clip_unproject(f4a(camera), v[0], v[1], w, pnt) == 0, (label, 'unproject', v)
        c = [sum(pnt[r]*camera[r*4+k] for r in range(3))+camera[12+k] for k in range(4)]
        # within the GS 12.4 grid (the point is binary32)
        assert abs(c[3]-w) <= 1e-3*max(1.0, abs(w)) and abs(c[0]/c[3]-v[0]) <= 1/16 and \
            abs(c[1]/c[3]-v[1]) <= 1/16, (label, 'unprojected point', v, c)
    for k in ('batches', 'kicks', 'packets', 'packet_bytes', 'vertices'):
        stats.setdefault('clip_native_'+k, 0)
    stats['clip_native_batches'] += 1
    stats['clip_native_kicks'] += len(want)
    stats['clip_native_packets'] += sum(1 for a, _ in want if a != 1019)
    stats['clip_native_packet_bytes'] += sum(len(r) for _, r in want)
    stats['clip_native_vertices'] += len(real)


def clip_kernel_checks(gslib, name, batches, whys, plan, ram, stats, entry, camera):
    """The guard-band clip kernels 00239C90 (box) and 0023E8A0 (receivers)
    executed as VU1 instructions on every batch the chain gives them.
    Each clip batch must hold the same 32 vertex qwords, the same dmem 0..3
    matrix and the same template rows 1021..1023 as the kernel batch
    before it (pairs: a run of n clip batches follows the object's n
    kernel batches); the triangles its loop sends on to the clipping code
    (CLIP_ENTRY, vertex i = 32 - vi11) must be exactly the header's
    em_shadow_gs_needs_clip set of the paired batch; every drawing kick
    must come after such an entry; every other kick must be the empty
    packet at dmem 1019 (NLOOP 0, EOP). So the two passes never draw one
    triangle twice, and a batch without such a triangle draws nothing in
    the clip pass. Every clip batch is then run through the translation
    (clip_native_checks), kick for kick."""
    main_of = {BOX_CLIP_KERNEL: BOX_KERNEL, CLIP_KERNEL: RECEIVER_KERNEL}
    box_index = 0
    counts = {'batches': 0, 'needs_clip': 0, 'rejected': 0, 'polygons': 0, 'polygon_vertices': 0,
              'empty_kicks': 0}
    polygon_prims = set()
    j = 0
    while j < len(batches):
        kernel = batches[j][0]
        if kernel not in main_of: j += 1; continue
        e = j
        while e < len(batches) and batches[e][0] == kernel: e += 1
        n = e-j
        assert j >= n and all(batches[k][0] == main_of[kernel] for k in range(j-n, j)), \
            (name, hex(kernel), 'clip batches without their kernel batches')
        for c in range(n):
            mk, mtop, mbefore, _, msrc, _ = batches[j-n+c]
            ck, ctop, cbefore, ckicks, csrc, events = batches[j+c]
            label = f'{name} {hex(ck)} batch {c}'
            assert msrc == csrc, (label, 'different source', hex(msrc), hex(csrc))
            assert batch_qw3(mbefore, mtop) == batch_qw3(cbefore, ctop), (label, 'vertex qwords differ')
            assert mbefore[:64] == cbefore[:64], (label, 'dmem 0..3 matrix differs')
            assert mbefore[1021*16:1024*16] == cbefore[1021*16:1024*16], (label, 'template rows differ')
            why = whys[j-n+c]
            want = {i for i in range(32) if gslib.needs_clip(why[i], i)}
            counts['rejected'] += sum(1 for i in range(2, 32) if why[i] & 18 == 18 and not why[i] & 1)
            got, current = [], None
            for ev in events:
                if ev[0] == 'pc':
                    current = 32-ev[2]; got.append(current)
                    continue
                _, at, packets = ev[:3]
                drawn = [pk for pk in packets if pk[0] is not None or pk[2]]
                if at == 1019 and all(pk[0] is None and not pk[2] for pk in packets):
                    counts['empty_kicks'] += 1
                    continue
                assert current is not None, (label, 'drawing kick before any clipped triangle', hex(at))
                for prim, flg, loops, regs in packets:
                    if prim is not None:
                        polygon_prims.add(prim)
                        counts['polygons'] += 1
                        counts['polygon_vertices'] += len(loops)
            assert sorted(got) == sorted(want) and len(got) == len(set(got)), \
                (label, 'clipped triangles', sorted(got), sorted(want))
            counts['needs_clip'] += len(want)
            counts['batches'] += 1
            if ck == BOX_CLIP_KERNEL:
                clip_native_checks(gslib, name, ck, ctop, cbefore, events,
                                   list(plan.box[box_index].clip_pass), None, stats)
            else:
                clip_native_checks(gslib, name, ck, ctop, cbefore, events, list(camera),
                                   list(plan.uv_24B0), stats)
        if kernel == BOX_CLIP_KERNEL: box_index += 1
        j = e
    # boundary: the strip's first two vertices. The captures have no
    # CLIP-only triangle there, so the first clip batch of each kernel is
    # run again (fresh VU1, its program loaded from the kernel packet)
    # with vertices 0 and 1 moved outside the guard band and the data ADC
    # of vertices 0..2 cleared: the loop must send exactly the header's set
    # on, starting at i = 2.
    for kernel in main_of:
        first = next((k for k, b in enumerate(batches) if b[0] == kernel), None)
        if first is None: continue
        ck, ctop, cbefore, _, _, _ = batches[first]

        def header_why(mem):
            flat = [x for q in batch_qw3(bytes(mem), ctop) for x in q]
            words, why = (C.c_int32*128)(), (C.c_uint32*32)()
            k1021 = f4a(dmem_f(mem, 1021)); k1022, k1023 = F4(), F4()
            gslib.rows(0, k1022, k1023)
            if kernel == BOX_CLIP_KERNEL:
                gslib.level_batch(f4a(dmem_f(mem, 0, 4)), k1021, k1022, k1023, f4a(flat), 32, words, why)
            else:
                stq, alpha = (C.c_float*96)(), (C.c_uint32*32)()
                gslib.receiver_batch(f4a(dmem_f(mem, 0, 4)), f4a(dmem_f(mem, 8, 4)), k1021, k1022, k1023,
                                     f4a(flat), 32, stq, alpha, words, why)
            return list(why)
        # vertices 0 and 1 moved by an offset that puts both outside the
        # guard band while vertex 2 stays (first offset that does)
        chosen = None
        for axis in range(3):
            for d in (1e3, -1e3, 1e4, -1e4):
                mem = bytearray(cbefore)
                for i in range(3):
                    at = ((ctop+4*i+3) & 1023)*16
                    v = list(struct.unpack_from('<3fI', mem, at))
                    if i < 2: v[axis] += d
                    v[3] &= ~0xA000
                    struct.pack_into('<3fI', mem, at, *v)
                why = header_why(mem)
                if why[0] & 2 and why[1] & 2 and gslib.needs_clip(why[2], 2):
                    chosen = (mem, why); break
            if chosen: break
        assert chosen, (name, hex(kernel), 'no synthetic offset puts vertices 0 and 1 outside')
        mem, why = chosen
        vu1 = VU1(ELF_BYTES[0])
        load_program(vu1, ELF_BYTES[0], kernel)
        vu1.mem[:] = mem; vu1.top = ctop; vu1.watch = {CLIP_ENTRY[kernel]}
        vu1.run(0)
        got = sorted(32-ev[2] for ev in vu1.events if ev[0] == 'pc')
        want = sorted(i for i in range(32) if gslib.needs_clip(why[i], i))
        assert got == want and min(got) == 2, \
            (name, hex(kernel), 'synthetic first-vertices batch', got, want, why[:6])
        rc, res = native_clip(gslib, kernel, mem, ctop)
        assert rc == 0 and res.kicked() == [(ev[1], ev[3]) for ev in vu1.events if ev[0] == 'kick'], \
            (name, hex(kernel), 'synthetic first-vertices batch: translation kicks')
        counts['synthetic_boundary_batches'] = counts.get('synthetic_boundary_batches', 0)+1
    # class 0/1 receivers get no clip pass: their triangles 0023C200 left
    # undrawn for CLIP are drawn by nothing (counted)
    cls = {object_address(ram, plan.receiver[i].id)+0x40: plan.receiver[i].cls
           for i in range(plan.receiver_count)}
    counts['class01_clip_triangles'] = 0
    for index, (kernel, top, before, kicks, src, _) in enumerate(batches):
        if kernel == RECEIVER_KERNEL:
            assert src in cls, (name, 'receiver batch from an unknown object', hex(src))
        if kernel == RECEIVER_KERNEL and cls[src] != 2:
            counts['class01_clip_triangles'] += sum(bool(gslib.needs_clip(w, i))
                                                    for i, w in enumerate(whys[index]))
    entry['clip_kernels'] = dict(counts, prims=sorted(hex(p) for p in polygon_prims))
    for k, v in counts.items(): stats['gs_clip_'+k] = stats.get('gs_clip_'+k, 0)+v


class ShadowFrame(lm.Frame):
    """lm.Frame (DMAC/VIF1/GIF replay with the GS registers at every kick)
    that also records every GIF vertex kick of a DIRECT packet (PACKED
    XYZF2/XYZ2 or A+D XYZ2) with the registers in force."""

    def __init__(self, ram, head):
        self.draws = []
        super().__init__(ram, head)

    def gif(self, pos, end):
        s = self.stream
        while pos + 16 <= end:
            lo, hi = struct.unpack_from('<QQ', s, pos)
            tag_at = self.where(pos)[1]; pos += 16
            nloop, flg, nreg = lo & 0x7FFF, (lo >> 58) & 3, (lo >> 60) or 16
            if (lo >> 46) & 1: self.write(lm.GS['PRIM'], (lo >> 47) & 0x7FF, tag_at)
            regs = [(hi >> (4 * i)) & 15 for i in range(nreg)]
            if flg == 0:
                for _ in range(nloop):
                    for r in regs:
                        d0, d1 = struct.unpack_from('<QQ', s, pos)
                        seg, at = self.where(pos); pos += 16
                        if r == 0xE:
                            self.write(d1 & 0xFF, d0, at)
                            if d1 & 0xFF in (0x04, 0x05):
                                self.draws.append(dict(at=at, dma=seg[3], tag=tag_at, gs=dict(self.gs),
                                                       regs=0, nloop=0))
                        elif r == 0: self.write(0, d0 & 0x7FF, at)
                        elif r == 1:
                            self.write(1, (d0 & 0xFF) | ((d0 >> 32) & 0xFF) << 8 | (d1 & 0xFF) << 16 |
                                       ((d1 >> 32) & 0xFF) << 24, at)
                        elif r in (4, 5):
                            self.draws.append(dict(at=at, dma=seg[3], tag=tag_at, gs=dict(self.gs),
                                                   regs=hi & ((1 << (4*nreg))-1), nloop=nloop))
                        elif r in (6, 7, 8, 9): self.write(r, d0, at)
            elif flg == 1:
                pos = (pos + 8 * nloop * nreg + 15) & ~15
            else:
                pos += 16 * nloop


def state_of(gs, prim, regs, nloop):
    g = lambda r: gs.get(r, 0)
    return [prim, regs, nloop, g(0x4C), g(0x4E), g(0x18), g(0x40), g(0x47), g(0x42), g(0x06),
            g(0x14), g(0x08), g(0x01)]


def gs_state_checks(gslib, name, buf, start, end, stats, entry):
    """The GS state in force at every draw of the executed chain, replayed
    from the head of its display list (an END tag written at the chain's
    end in a copy), against em_shadow_gs_pass_state."""
    image = bytearray(buf)
    struct.pack_into('<II', image, end, 0x70000000, 0)
    image = bytes(image)
    frame = None
    for head in lm.LIST_HEADS:
        try:
            tags = [a for a, *_ in lm.dma_walk(image, head)]
        except SystemExit:
            continue
        if start in tags:
            frame = ShadowFrame(image, head)
            break
    assert frame is not None, (name, 'no display list reaches the chain')
    # a draw belongs to the chain when the DMA tag that moved its data does
    # (REF'd packets and models live outside the chain's own bytes)
    inside = lambda a: start <= a < end
    draws = [d for d in frame.draws if inside(d['dma'])]
    kicks = [k for k in frame.kicks if inside(k['tag'])]
    found = {p: [] for p in range(len(PASSES))}
    # the 001DA290 strip (PACKED, 4 vertices) is the chain's first DIRECT draw
    strip = [d for d in draws if d['regs']]
    assert len(strip) == 4 and len({d['tag'] for d in strip}) == 1, (name, 'alpha clear strip')
    found[0].append(state_of(strip[-1]['gs'], strip[-1]['gs'][0], strip[-1]['regs'], strip[-1]['nloop']))
    sprite = [d for d in draws if not d['regs'] and 0x817E20 <= d['at'] < 0x817E20+16*16]
    assert len(sprite) == 2, (name, 'target clear sprite')
    found[2].append(state_of(sprite[-1]['gs'], sprite[-1]['gs'][0], 0, 0))
    extra = {}
    for k in kicks:
        tpl = k['tpl'].get(0x3FC)
        lo, hi = struct.unpack_from('<QQ', tpl) if tpl else (0, 0)
        prim, regs, nloop = (lo >> 47) & 0x7FF, hi & ((1 << (4*((lo >> 60) or 16)))-1), lo & 0x7FFF
        if k['kernel'] == BOX_KERNEL: p = 1
        elif k['kernel'] == SIL_KERNEL: p = 3
        elif k['kernel'] == RECEIVER_KERNEL: p = 4
        elif k['kernel'] in (BOX_CLIP_KERNEL, CLIP_KERNEL):
            # clip kernels: the GS registers of their pass (their GIF tags
            # come from other dmem slots, LEVEL_MATERIALS.md)
            p = 1 if k['kernel'] == BOX_CLIP_KERNEL else 4
            want = state_of(k['gs'], 0, 0, 0)
            found.setdefault(('clip', p), []).append(want)
            continue
        else:
            raise AssertionError((name, 'unexpected kernel in chain', hex(k['kernel'])))
        found[p].append(state_of(k['gs'], prim, regs, nloop))
        for reg, bit, want in ((0x46, 1, 1), (0x45, 1, 0), (0x49, 1, 0), (0x4A, 1, 0), (0x1A, 1, 1)):
            got = k['gs'].get(reg, None)
            assert got is not None and got & bit == want, (name, PASSES[p], hex(reg), got)
        if p == 4: extra['fogcol'] = hex(k['gs'].get(0x3D, -1))
    env = set()
    for p in range(len(PASSES)):
        want = (C.c_uint64*13)()
        assert gslib.pass_state(p, want) == 0
        assert found[p], (name, PASSES[p], 'no draw')
        for got in found[p]:
            if p in (1, 4): got = got[:12] + [want[12]]        # per-call / per-vertex RGBAQ
            if p in (0, 1, 2, 3) and not want[0] & 0x10:
                got = got[:9] + list(want)[9:12] + got[12:]    # untextured: TEX regs unused
            if p == 2: got = got[:8] + [want[8]] + got[9:]     # sprite without ABE: ALPHA unused
            why = gslib.unsupported(p, (C.c_uint64*13)(*got)).decode()
            assert not why, (name, PASSES[p], why, [hex(x) for x in got], [hex(x) for x in want])
            main = want[3] == 0x80000
            for f, a, b in zip(STATE_FIELDS, got, want):
                if f == 'alpha' and not want[0] & 0x40: continue
                if main and f == 'frame': a &= ~0x1FF        # the buffer of this frame
                if main and f == 'xyoffset': a &= ~(0xF << 32)   # the field's half line
                assert a == b, (name, PASSES[p], f, hex(a), hex(b))
            if main:
                env.add((got[3], got[5]))
        stats['gs_state_draws'] = stats.get('gs_state_draws', 0)+len(found[p])
    for (tag, p), rows in [(k, v) for k, v in found.items() if isinstance(k, tuple)]:
        want = (C.c_uint64*13)()
        gslib.pass_state(p, want)
        for got in rows:
            env.add((got[3], got[5]))
            for f in ('zbuf', 'scissor', 'test', 'alpha', 'tex0', 'tex1', 'clamp'):
                i = STATE_FIELDS.index(f)
                if f in ('tex0', 'tex1', 'clamp') and p == 1: continue
                assert got[i] == want[i], (name, 'clip kernel', PASSES[p], f, hex(got[i]))
        stats['gs_state_clip_kicks'] = stats.get('gs_state_clip_kicks', 0)+len(rows)
    # every main-frame draw of the chain uses the one environment of its frame
    assert len(env) == 1, (name, 'main-frame FRAME/XYOFFSET differ inside the chain', env)
    entry['gs_state'] = {PASSES[p]: len(found[p]) for p in range(len(PASSES))}
    entry['gs_state']['frame_env'] = [hex(x) for x in sorted(env)[0]]
    entry['gs_state'].update(extra)


# ----------------------------------------------------------- Metal side ---
def load_emdl(path):
    """(verts [10 words per vertex, raw bytes], indices, bone_count)."""
    b = path.read_bytes()
    magic = b[:4]
    bc, vc, ic, fc = struct.unpack_from('<4I', b, 4)
    tc = struct.unpack_from('<I', b, 24)[0]
    o = 32
    cc = 1
    if magic == b'EMD3':
        cc = struct.unpack_from('<I', b, 32)[0]; o = 36
    o += bc*4+tc*16+(cc*16 if magic == b'EMD3' else 0)
    verts = b[o:o+40*vc]; o += 40*vc
    idx = list(struct.unpack_from(f'<{ic}I', b, o))
    return verts, idx, bc


def proxy_triangles(verts, idx):
    """EMDL triangles as frozensets of (bone slot, x, y, z)."""
    out = set()
    for t in range(0, len(idx), 3):
        tri = []
        for k in idx[t:t+3]:
            x, y, z = struct.unpack_from('<3f', verts, 40*k)
            bone = struct.unpack_from('<I', verts, 40*k+32)[0] & 0xFFFFFF
            tri.append((bone, x, y, z))
        out.add(frozenset(tri))
    return out


def kicked_silhouette(batches):
    """The silhouette kernel's drawn triangles: (bone slot, x, y, z)
    vertex sets and the kicked 12.4 XY triples."""
    tris, xy = set(), []
    for kernel, top, before, kicks, _, _ in batches:
        if kernel != SIL_KERNEL: continue
        loops = kicks[0][1][0][2]
        verts = []
        for i in range(32):
            x, y, z, w = struct.unpack('<3fI', before[(top+4*i+3)*16:(top+4*i+4)*16])
            verts.append((((w & 0x3FF)//8), x, y, z))   # VU dmem addresses are 10 bits
        for i in range(2, 32):
            word = struct.unpack_from('<4I', loops[i][5])
            if word[3] & 0x8000: continue
            tris.add(frozenset(verts[i-2:i+1]))
            xy.append([struct.unpack_from('<4I', loops[j][5])[:2] for j in (i-2, i-1, i)])
    return tris, xy


def gs_raster(tris):
    """128x128 coverage of GS 12.4 triangles in the target (window pixel
    (x, y) sampled at (1984 + x, 1984 + y)), top-left fill."""
    cover = bytearray(128*128)
    org = 1984*16
    for t in tris:
        p = [((x & 0xFFFF)-org, (y & 0xFFFF)-org) for x, y in t]
        area = (p[1][0]-p[0][0])*(p[2][1]-p[0][1])-(p[1][1]-p[0][1])*(p[2][0]-p[0][0])
        if area == 0: continue
        if area < 0: p = [p[0], p[2], p[1]]
        edges = []
        for k in range(3):
            (x0, y0), (x1, y1) = p[k], p[(k+1) % 3]
            a, b = y0-y1, x1-x0
            c = -(a*x0+b*y0)
            # interior where a*x + b*y + c > 0 (y down). A pixel on an edge
            # is drawn for a left edge (a > 0: the edge runs up, interior to
            # its right) or a top edge (a == 0, b > 0: interior below).
            bias = 0 if (a > 0 or (a == 0 and b > 0)) else -1
            edges.append((a, b, c, bias))
        xs = [q[0] for q in p]; ys = [q[1] for q in p]
        for py in range(max(0, min(ys)//16), min(127, max(ys)//16)+1):
            for px in range(max(0, min(xs)//16), min(127, max(xs)//16)+1):
                sx, sy = px*16, py*16
                if all(a*sx+b*sy+c+bias >= 0 for a, b, c, bias in edges):
                    cover[py*128+px] = 1
    return cover


class Metal:
    """The Metal backend (em_gfx_metal.m) loaded through ctypes in a headless
    window, for the silhouette target and the receiver pixel pipeline."""

    def __init__(self, out, size=(160, 120)):
        import os
        os.environ['EM_HEADLESS'] = '1'
        lib_path = out/'gfx.dylib'
        subprocess.run(['clang', '-O2', '-Wall', '-Wextra', '-Werror', '-I'+str(ROOT/'src'), '-shared',
                        '-fPIC', str(ROOT/'src/gfx/metal/em_gfx_metal.m'), str(ROOT/'src/game/em_lighting.c'),
                        str(ROOT/'src/platform/mac/em_platform_mac.m'), str(ROOT/'src/em_model.c'),
                        str(ROOT/'src/game/em_packet_chain_original.c'),
                        str(ROOT/'src/game/em_status_ui_leftovers.c'),
                        '-framework', 'Cocoa', '-framework', 'Metal', '-framework', 'QuartzCore',
                        '-o', str(lib_path)], check=True)
        lib = self.lib = C.CDLL(str(lib_path))
        lib.em_window_create.restype = C.c_void_p
        lib.em_window_create.argtypes = [C.c_char_p, C.c_int, C.c_int]
        lib.em_gfx_create.restype = C.c_void_p
        lib.em_gfx_create.argtypes = [C.c_void_p]
        for fn in ('em_gfx_begin_frame',):
            getattr(lib, fn).argtypes = [C.c_void_p, C.c_float, C.c_float, C.c_float, C.c_float]
        for fn in ('em_gfx_end_frame', 'em_gfx_fog_off'):
            getattr(lib, fn).argtypes = [C.c_void_p]
        lib.em_gfx_request_capture.argtypes = [C.c_void_p, C.c_char_p]
        lib.em_gfx_fog.argtypes = [C.c_void_p, C.c_float, C.c_float, C.POINTER(C.c_float)]
        lib.em_gfx_shadow_target_read.argtypes = [C.c_void_p, C.c_char_p]
        lib.em_gfx_shadow_silhouette.argtypes = [C.c_void_p, C.c_char_p, C.c_uint32, C.POINTER(C.c_uint32),
                                                 C.c_uint32, C.POINTER(C.c_float), C.c_uint32,
                                                 C.POINTER(C.c_float)]
        self.win = lib.em_window_create(b'shadow-gs test', *size)
        assert self.win, 'no window'
        self.gfx = lib.em_gfx_create(self.win)
        assert self.gfx, 'no Metal device'


class Strips(C.Structure):
    _fields_ = [('qw3', C.POINTER(C.c_float)), ('vertex_count', C.c_uint32)]


def metal_silhouette(metal, captures, stats, report):
    """Draw each exact capture's silhouette through em_gfx_shadow_silhouette
    and compare the 128x128 target with the original kernel's kicked
    triangles rasterized with the GS sampling rule: every texel must be
    (128,255,255,255) inside and the clear (128,128,128,0) outside."""
    verts, idx, _ = load_emdl(PROXY_EMDL)
    ia = (C.c_uint32*len(idx))(*idx)
    rows = []
    for name, ram, vp, xy in captures:
        count = ram[PLAYER+0x0C]
        nodes = []
        for b in range(count):
            nodes += struct.unpack_from('<16f', ram, u32(ram, PLAYER+0x110+4*b)+0x90)
        lib, gfx = metal.lib, metal.gfx
        lib.em_gfx_begin_frame(gfx, 0.0, 0.0, 0.0, 1.0)
        r = lib.em_gfx_shadow_silhouette(gfx, verts, len(verts)//40, ia, len(idx), f4a(nodes), count,
                                         f4a(vp))
        lib.em_gfx_end_frame(gfx)
        assert r == 0, (name, 'em_gfx_shadow_silhouette failed')
        target = C.create_string_buffer(128*128*4)
        assert lib.em_gfx_shadow_target_read(gfx, target) == 0
        cover = gs_raster(xy)
        texels = target.raw
        wrong_colour = mismatch = 0
        for p in range(128*128):
            px = texels[4*p:4*p+4]
            inside = px == b'\x80\xff\xff\xff'
            if not inside and px != b'\x80\x80\x80\x00': wrong_colour += 1
            if inside != bool(cover[p]): mismatch += 1
        assert wrong_colour == 0, (name, 'target texels other than the two original values', wrong_colour)
        rows.append({'capture': name, 'covered_gs': sum(cover), 'covered_metal':
                     sum(texels[4*p+3] == 255 for p in range(128*128)), 'mismatch_texels': mismatch})
        stats['metal_target_texels'] = stats.get('metal_target_texels', 0)+128*128
        stats['metal_target_mismatch'] = stats.get('metal_target_mismatch', 0)+mismatch
    report['metal_silhouette'] = rows


def metal_receiver_pixels(metal, gslib, stats, report):
    """The receiver pipeline's pixel arithmetic against
    em_shadow_gs_receiver_pixel: a frame of Cd (91,106,106) with destination
    alpha 255, a full silhouette target (At 255), one receiver quad at
    constant depth drawn once and twice (the second draw passes the
    destination-alpha test only when the first wrote As >= 128)."""
    import os, tempfile
    lib, gfx = metal.lib, metal.gfx
    lib.em_gfx_shadow_receiver_begin.argtypes = [C.c_void_p, C.POINTER(C.c_float), C.POINTER(C.c_float),
                                                 C.POINTER(C.c_float)]
    lib.em_gfx_shadow_receiver.argtypes = [C.c_void_p, C.POINTER(Strips), C.c_uint32]
    lib.em_gfx_shadow_receiver_end.argtypes = [C.c_void_p]
    # a proxy of two triangles covering the whole target: vertex pos
    # (+-64, +-64, 0) under an identity bone and vp = GS centre 2048
    quad = [(-100.0, -100.0), (100.0, -100.0), (-100.0, 100.0), (100.0, 100.0)]
    verts = b''.join(struct.pack('<8fII', x, y, 0.0, 0, 0, 0, 0, 0, 0, 0xFFFFFFFF) for x, y in quad)
    idx = (C.c_uint32*6)(0, 1, 2, 1, 3, 2)
    ident = [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0]
    vp = [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 2048.0, 2048.0, 0, 1.0]
    cd = (91, 106, 106)
    fogcol = (48, 48, 48)
    fog_rgb = (C.c_float*3)(*[float(c) for c in fogcol])
    rows = []
    tmp = Path(tempfile.mkdtemp(prefix='shadow_gs_'))
    for a in (36, 90, 200):
        for passes in (1, 2):
            # camera: clip = (x, y, 0, w) with w = 20 -> F = floor(A + B*20)
            w = 20.0
            camera = [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 0, 0, 2048.0*w, 2048.0*w, 0, w]
            uv = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.5, 0.5, 1.0, 8388608.0+a]
            # a world quad covering the frame at NDC depth 0.5 (viewproj = identity)
            strip = []
            for x, y in ((-1.0, -1.0), (1.0, -1.0), (-1.0, 1.0), (1.0, 1.0)):
                strip.append((x, y, 0.5, struct.unpack('<f', struct.pack('<I', 0x3F800000))[0]))
            flags = [0x3F808000, 0xBF80C000, 0x3F800000, 0xBF800000]
            q = []
            for (x, y, z, _), fl in zip(strip, flags):
                q += [x, y, z, struct.unpack('<f', struct.pack('<I', fl))[0]]
            pad = [0.0, 0.0, 0.0, struct.unpack('<f', struct.pack('<I', 0xBF80C000))[0]]
            q += pad*28
            strips = Strips(f4a(q), 32)
            lib.em_gfx_begin_frame(gfx, cd[0]/255.0, cd[1]/255.0, cd[2]/255.0, 1.0)
            lib.em_gfx_fog(gfx, -209.0, 304.0, fog_rgb)
            assert lib.em_gfx_shadow_silhouette(gfx, verts, 4, idx, 6, f4a(ident), 1, f4a(vp)) == 0
            assert lib.em_gfx_shadow_receiver_begin(gfx, f4a(uv), f4a(camera), f4a(ident)) == 0
            for _ in range(passes):
                assert lib.em_gfx_shadow_receiver(gfx, C.byref(strips), 0) == 0
            assert lib.em_gfx_shadow_receiver_end(gfx) == 0
            path = tmp/f'r{a}_{passes}.bmp'
            lib.em_gfx_request_capture(gfx, str(path).encode())
            lib.em_gfx_end_frame(gfx)
            got = read_bmp_centre(path)
            # expected: the C model once or twice
            coef = (C.c_float*2)(); gslib.coefficients(-209.0, 304.0, coef)
            acc = fp(fp(1.0*coef[0])+fp(coef[1]*w))
            f = int(max(min(acc, 255.0), 0.0))
            dst = bytes(cd)+b'\xff'
            for _ in range(passes):
                out = C.create_string_buffer(4)
                if gslib.pixel(255, a, f, (C.c_uint*3)(*fogcol), dst, out):
                    dst = out.raw
            assert got == tuple(dst[:3]), (a, passes, got, tuple(dst))
            rows.append({'a': a, 'passes': passes, 'F': f, 'pixel': list(got), 'alpha_after': dst[3]})
            stats['metal_receiver_pixels'] = stats.get('metal_receiver_pixels', 0)+1
    report['metal_receiver_pixels'] = rows

    # The clip kernels in the backend: a strip whose second triangle has a
    # vertex outside the guard band (GS X 7168: +x only, so not rejected; its data
    # word +1.0 keeps the clip kernels' back-face test from dropping it).
    # The camera is w = 40 z (row 2) and GS X = 256 x + 2048, so the plane
    # z = 0.5 sits at w = 20 as above, the quad spans 512 GS pixels and the
    # x, y, w columns are regular (em_shadow_gs_clip_unproject). NDC
    # (0.5, 0.5) lies only in that
    # triangle. Receivers: class 2 (0023E8A0 re-pass) shadows it, class 0
    # (no re-pass) leaves the frame. Box: 00239C90's part of the box writes
    # destination alpha 128 there, so a following receiver over the whole
    # frame passes DATE there; without the box it does not.
    lib.em_gfx_shadow_box.argtypes = [C.c_void_p, C.POINTER(Strips), C.POINTER(C.c_float),
                                      C.POINTER(C.c_float), C.c_uint32, C.POINTER(C.c_float)]
    lib.em_gfx_shadow_alpha_clear.argtypes = [C.c_void_p]
    w = 20.0
    camera = [256.0*w, 0, 0, 0, 0, 256.0*w, 0, 0, 2048.0*40, 2048.0*40, 0, 40.0, 0, 0, 0, 0]
    uv = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.5, 0.5, 1.0, 8388608.0+200]
    word = lambda u: struct.unpack('<f', struct.pack('<I', u))[0]

    def strip_of(pos, flags):
        q = []
        for (x, y), fl in zip(pos, flags):
            q += [x, y, 0.5, word(fl)]
        q += [0.0, 0.0, 0.0, word(0xBF80C000)]*28
        return Strips(f4a(q), 32)
    clipped = strip_of([(-1.0, -1.0), (1.0, -1.0), (-1.0, 1.0), (20.0, 1.0)],
                       [0x3F808000, 0xBF80C000, 0x3F800000, 0x3F800000])
    whole = strip_of([(-1.0, -1.0), (1.0, -1.0), (-1.0, 1.0), (1.0, 1.0)],
                     [0x3F808000, 0xBF80C000, 0x3F800000, 0xBF800000])
    coef = (C.c_float*2)(); gslib.coefficients(-209.0, 304.0, coef)
    f = int(max(min(fp(fp(1.0*coef[0])+fp(coef[1]*w)), 255.0), 0.0))
    out = C.create_string_buffer(4)
    assert gslib.pixel(255, 200, f, (C.c_uint*3)(*fogcol), bytes(cd)+b'\xff', out)
    shadowed = tuple(out.raw[:3])
    results = {}
    for case in ('receiver cls 2', 'receiver cls 0', 'box', 'no box'):
        lib.em_gfx_begin_frame(gfx, cd[0]/255.0, cd[1]/255.0, cd[2]/255.0, 1.0)
        lib.em_gfx_fog(gfx, -209.0, 304.0, fog_rgb)
        rets = []
        if case in ('box', 'no box'):
            rets.append(lib.em_gfx_shadow_alpha_clear(gfx))
            if case == 'box':
                rets.append(lib.em_gfx_shadow_box(gfx, C.byref(clipped), f4a(ident), f4a(camera),
                                                  0x80000000, f4a(ident)))
        rets.append(lib.em_gfx_shadow_silhouette(gfx, verts, 4, idx, 6, f4a(ident), 1, f4a(vp)))
        rets.append(lib.em_gfx_shadow_receiver_begin(gfx, f4a(uv), f4a(camera), f4a(ident)))
        if case.startswith('receiver'):
            rets.append(lib.em_gfx_shadow_receiver(gfx, C.byref(clipped), 2 if case.endswith('2') else 0))
        else:
            rets.append(lib.em_gfx_shadow_receiver(gfx, C.byref(whole), 0))
        rets.append(lib.em_gfx_shadow_receiver_end(gfx))
        path = tmp/'clip.bmp'
        lib.em_gfx_request_capture(gfx, str(path).encode())
        lib.em_gfx_end_frame(gfx)
        results[case] = (rets, read_bmp_at(path, 0.75, 0.25))
    assert all(r == 0 for rets, _ in results.values() for r in rets), results
    assert results['receiver cls 2'][1] == shadowed, (results, shadowed)
    assert results['receiver cls 0'][1] == cd, results
    assert results['box'][1] == shadowed, (results, shadowed)
    assert results['no box'][1] == cd, results
    report['metal_clip'] = {k: list(v[1]) for k, v in results.items()}
    stats['metal_clip_cases'] = len(results)
    for p in tmp.iterdir(): p.unlink()
    tmp.rmdir()


def read_bmp_at(path, fx, fy):
    """The pixel at (fx * width, fy * height) from the top-left."""
    d = path.read_bytes()
    off, w, h, bpp = u32(d, 10), u32(d, 18), struct.unpack_from('<i', d, 22)[0], d[28]
    stride = (w*bpp//8+3) & ~3
    x, y = int(fx*w), int(fy*abs(h))
    yy = abs(h)-1-y if h > 0 else y
    o = off+yy*stride+x*bpp//8
    return (d[o+2], d[o+1], d[o])


def read_bmp_centre(path):
    d = path.read_bytes()
    off, w, h, bpp = u32(d, 10), u32(d, 18), struct.unpack_from('<i', d, 22)[0], d[28]
    stride = (w*bpp//8+3) & ~3
    y = abs(h)//2
    yy = abs(h)-1-y if h > 0 else y
    o = off+yy*stride+(w//2)*bpp//8
    return (d[o+2], d[o+1], d[o])


# ------------------------------------------------------ capture metric ----
# --capture BEAT: a headless native frame of a route beat
# (../Extermination/build/s87/route/BEAT/, FIRST_LEVEL_ROUTE.md) against
# its original.png. The harness draws what the port's render stage will:
# the background, the six AREA11 zone meshes with the area fog, then the
# shadow chain through em_gfx_shadow_* with the inputs the native
# em_shadow_original computes over the beat's RAM (the box and receiver
# strips are the original objects' VU1 vertex lists, taken from the chain
# the original 001DA6A0 builds over that RAM). It writes three frames to
# build/captures/shadow/ (without shadow, with shadow, with the player's
# mask) and reports the shadow region against the original screenshot.
ROUTE = DECOMP/'build/s87/route'
CAPTURE_IOU_MIN, CAPTURE_RATIO_TOL = 0.80, 0.05
ZONES = ('00_zone_main', '01_zone_e1', '02_zone_e2', '03_zone_e3', '04_zone_e4', '05_movables')


class EmModel(C.Structure):
    _fields_ = [('bone_count', C.c_uint32), ('vert_count', C.c_uint32), ('index_count', C.c_uint32),
                ('frame_count', C.c_uint32), ('fps', C.c_float), ('tex_count', C.c_uint32),
                ('flags', C.c_uint32), ('clip_count', C.c_uint32), ('parents', C.c_void_p),
                ('texs', C.c_void_p), ('clips', C.c_void_p), ('verts', C.c_void_p),
                ('indices', C.c_void_p), ('palette', C.POINTER(C.c_float)), ('texels', C.c_void_p),
                ('texel_bytes', C.c_uint32)]


def native_viewproj(ram):
    """The frame's native P*V (column-major) from the original view copy
    ctx+0x2380 (rows 1 and 2 negated: em_cs_view_to_native's convention,
    tools/test_census_standins_reference.py) and em_mat4_perspective_gs(ctx+0x2468)."""
    ctx = u32(ram, CONTEXT_PTR)
    orig = struct.unpack_from('<16f', ram, ctx+0x2380)
    view = [(-v if (i % 4) in (1, 2) else v) for i, v in enumerate(orig)]
    zoom = struct.unpack_from('<f', ram, ctx+0x2468)[0]
    near, far = 0.1, 16711680.0
    proj = [0.0]*16
    proj[0] = number(bits(zoom/320.0)); proj[5] = number(bits(zoom/224.0))
    proj[10] = number(bits(far/(near-far))); proj[11] = -1.0
    proj[14] = number(bits((near*far)/(near-far)))
    vp = [0.0]*16
    for c in range(4):
        for r in range(4):
            vp[c*4+r] = sum(proj[k*4+r]*view[c*4+k] for k in range(4))
    return view, zoom, vp


def region_metric(orig_px, a_px, b_px, c_px, w, h):
    """S = pixels the shadow changed (B != A), minus the player's pixels
    (C != B); ring = the bounding box of S grown by 16 px, minus S and the
    player. Means of the original and native frames over S and the ring,
    and the IoU of S with the original's dark pixels in the box."""
    lum = lambda c: 0.299*c[0]+0.587*c[1]+0.114*c[2]
    S, P = set(), set()
    for y in range(h):
        for x in range(w):
            if b_px(x, y) != a_px(x, y): S.add((x, y))
            if c_px(x, y) != b_px(x, y): P.add((x, y))
    S -= P
    if not S: return {'shadow_pixels': 0}
    xs = [p[0] for p in S]; ys = [p[1] for p in S]
    x0, x1, y0, y1 = max(0, min(xs)-16), min(w-1, max(xs)+16), max(0, min(ys)-16), min(h-1, max(ys)+16)
    box = {(x, y) for y in range(y0, y1+1) for x in range(x0, x1+1)} - P
    ring = box - S
    mean = lambda px, pts: [round(sum(px(*p)[k] for p in pts)/len(pts), 1) for k in range(3)]
    o_s, o_r = mean(orig_px, S), mean(orig_px, ring)
    cut = (lum(o_s)+lum(o_r))/2
    dark = {p for p in box if lum(orig_px(*p)) < cut}
    return {'shadow_pixels': len(S), 'player_pixels': len(P), 'box': [x0, y0, x1, y1],
            'original_shadow_mean': o_s, 'original_ring_mean': o_r,
            'native_shadow_mean': mean(b_px, S), 'native_unshadowed_mean': mean(a_px, S),
            'native_ring_mean': mean(b_px, ring),
            'original_ratio': round(lum(o_s)/lum(o_r), 3),
            'native_ratio': round(lum(mean(b_px, S))/lum(mean(a_px, S)), 3),
            'original_dark_pixels': len(dark), 'iou_shadow_vs_original_dark':
                round(len(dark & S)/len(dark | S), 3)}


def capture_metric(elf, lib, gslib, beat, report):
    import test_background_reference as bgref
    folder = ROUTE/beat
    ram = (folder/'eeMemory.bin').read_bytes()
    scratch = (folder/'scratchpad.bin').read_bytes()
    need = [ROOT/'assets/scene_snow'/(z+'.emdl') for z in ZONES] + \
           [PROXY_EMDL, ROOT/'assets/player.emdl', ROOT/'assets/scene_snow/background.embg']
    missing = [str(p) for p in need if not p.exists()]
    assert not missing, ('capture needs the exported assets', missing)
    scene = scene_view(ram, scratch)
    nat = NativeRun(lib, ram, scene)
    plan = nat.plan
    assert nat.result == 1 and plan.drawn == 1, (beat, 'the native 001DA6A0 does not draw here', nat.result)
    # the original chain over the same RAM gives the objects' vertex lists
    o, end, _ = execute(elf, ram, scratch, 0x1F00000)
    buf = bytearray(ram)
    for a in range(0x1F00000, end): buf[a] = o.load(a, 1)
    units = walk(buf, 0x1F00000, end)
    batches = kernel_replay(elf, buf, units)
    box_strips = [b[2] for b in batches if b[0] == BOX_KERNEL]
    recv = [(b[1], b[2], b[4]) for b in batches if b[0] == RECEIVER_KERNEL]
    assert len(box_strips) == 2 and recv, (beat, 'chain', len(box_strips), len(recv))
    # the receiver objects in plan order, each with its class and batches
    objects = []
    for i in range(plan.receiver_count):
        addr = object_address(ram, plan.receiver[i].id)+0x40
        q = [x for top, before, src in recv if src == addr for qq in batch_qw3(before, top) for x in qq]
        assert q, (beat, 'receiver object without batches', hex(addr))
        objects.append((addr, plan.receiver[i].cls, q))
    assert sum(len(q) for _, _, q in objects) == 128*len(recv), (beat, 'receiver batches outside the plan')
    out = ROOT/'build/captures/shadow'; out.mkdir(parents=True, exist_ok=True)
    metal = Metal(ROOT/'build/shadow_original_reference', size=(320, 240))
    lib_g, gfx = metal.lib, metal.gfx
    lib_g.em_model_load.argtypes = [C.POINTER(EmModel), C.c_char_p]
    lib_g.em_gfx_mesh_create.restype = C.c_void_p
    lib_g.em_gfx_mesh_create.argtypes = [C.c_void_p, C.c_void_p, C.c_uint32, C.c_void_p, C.c_uint32,
                                         C.c_void_p, C.c_uint32, C.c_void_p, C.c_uint32]
    lib_g.em_gfx_draw_skinned.argtypes = [C.c_void_p, C.c_void_p, C.POINTER(C.c_float),
                                          C.POINTER(C.c_float), C.c_uint32]
    lib_g.em_gfx_draw_skinned_tinted.argtypes = [C.c_void_p, C.c_void_p, C.POINTER(C.c_float),
                                                 C.POINTER(C.c_float), C.c_uint32, C.POINTER(C.c_float)]
    lib_g.em_gfx_background_load.argtypes = [C.c_void_p, C.c_char_p]
    lib_g.em_gfx_background_draw.argtypes = [C.c_void_p, C.POINTER(C.c_float), C.c_float]
    lib_g.em_gfx_char_rig.argtypes = [C.c_void_p, C.c_void_p]
    lib_g.em_gfx_shadow_alpha_clear.argtypes = [C.c_void_p]
    lib_g.em_gfx_shadow_box.argtypes = [C.c_void_p, C.POINTER(Strips), C.POINTER(C.c_float),
                                        C.POINTER(C.c_float), C.c_uint32, C.POINTER(C.c_float)]
    lib_g.em_gfx_shadow_receiver_begin.argtypes = [C.c_void_p, C.POINTER(C.c_float), C.POINTER(C.c_float),
                                                   C.POINTER(C.c_float)]
    lib_g.em_gfx_shadow_receiver.argtypes = [C.c_void_p, C.POINTER(Strips), C.c_uint32]
    lib_g.em_gfx_shadow_receiver_end.argtypes = [C.c_void_p]
    meshes = []
    for z in ZONES:
        m = EmModel()
        assert lib_g.em_model_load(C.byref(m), str(ROOT/'assets/scene_snow'/(z+'.emdl')).encode()) == 0
        mesh = lib_g.em_gfx_mesh_create(gfx, m.verts, m.vert_count, m.indices, m.index_count, m.texs,
                                        m.tex_count, m.texels, m.flags)
        assert mesh, z
        meshes.append((mesh, m.palette, m.bone_count, m))
    player = EmModel()
    assert lib_g.em_model_load(C.byref(player), str(ROOT/'assets/player.emdl').encode()) == 0
    pmesh = lib_g.em_gfx_mesh_create(gfx, player.verts, player.vert_count, player.indices, player.index_count,
                                     player.texs, player.tex_count, player.texels, player.flags)
    assert lib_g.em_gfx_background_load(gfx, str(ROOT/'assets/scene_snow/background.embg').encode()) == 0
    view, zoom, vp = native_viewproj(ram)
    vpa, viewa = f4a(vp), f4a(view)
    count = ram[PLAYER+0x0C]
    nodes = []
    for b in range(count):
        nodes += struct.unpack_from('<16f', ram, u32(ram, PLAYER+0x110+4*b)+0x90)
    pverts, pidx, _ = load_emdl(PROXY_EMDL)
    ia = (C.c_uint32*len(pidx))(*pidx)
    rig = (C.c_float*28)(*([0.0]*24+[128.0, 128.0, 128.0, 0.0]))
    fog_rgb = (C.c_float*3)(48.0, 48.0, 48.0)
    paths, faults = {}, []
    for mode in ('noshadow', 'shadow', 'player'):
        lib_g.em_gfx_begin_frame(gfx, 0.0, 0.0, 0.0, 1.0)
        lib_g.em_gfx_background_draw(gfx, viewa, zoom)
        lib_g.em_gfx_fog(gfx, -209.0, 304.0, fog_rgb)
        lib_g.em_gfx_char_rig(gfx, None)
        for mesh, pal, bc, _ in meshes:
            lib_g.em_gfx_draw_skinned(gfx, mesh, vpa, pal, bc)
        if mode != 'noshadow':
            assert lib_g.em_gfx_shadow_alpha_clear(gfx) == 0
            for k in range(2):
                box = plan.box[k]
                top = [b[1] for b in batches if b[0] == BOX_KERNEL][k]
                q = [x for qq in batch_qw3(box_strips[k], top) for x in qq]
                if lib_g.em_gfx_shadow_box(gfx, C.byref(Strips(f4a(q), 32)), box.world, box.clip_pass,
                                           box.rgbaq, vpa):
                    faults.append((mode, f'box {k}'))
            assert lib_g.em_gfx_shadow_silhouette(gfx, pverts, len(pverts)//40, ia, len(pidx), f4a(nodes),
                                                  count, plan.silhouette_vp) == 0
            assert lib_g.em_gfx_shadow_receiver_begin(gfx, plan.uv_24B0, scene.camera_3AC0, vpa) == 0
            for addr, cls, q in objects:
                if lib_g.em_gfx_shadow_receiver(gfx, C.byref(Strips(f4a(q), len(q)//4)), cls):
                    faults.append((mode, f'receiver {hex(addr)} class {cls}'))
            assert lib_g.em_gfx_shadow_receiver_end(gfx) == 0
        if mode == 'player':
            lib_g.em_gfx_char_rig(gfx, rig)
            lib_g.em_gfx_draw_skinned_tinted(gfx, pmesh, vpa, f4a(nodes), count, f4a([0.0, 0.0, 0.0, 1.0]))
            lib_g.em_gfx_char_rig(gfx, None)
        paths[mode] = out/f'native_{mode}.bmp'
        lib_g.em_gfx_request_capture(gfx, str(paths[mode]).encode())
        lib_g.em_gfx_end_frame(gfx)
    w, h, a_px = bgref.read_bmp(paths['noshadow'])
    _, _, b_px = bgref.read_bmp(paths['shadow'])
    _, _, c_px = bgref.read_bmp(paths['player'])
    ow, oh, o_px = bgref.read_png(folder/'original.png')
    assert (w, h) == (ow, oh), ('frame sizes', w, h, ow, oh)
    metric = region_metric(o_px, a_px, b_px, c_px, w, h)
    metric.update({'beat': beat, 'receiver_batches': len(recv), 'receivers': plan.receiver_count,
                   'classes': [c for _, c, _ in objects], 'faults': faults,
                   'frames': [str(p.relative_to(ROOT)) for p in paths.values()]})
    report['capture_metric'] = metric
    print('capture metric:', json.dumps(metric))


def capture_bounds(metric):
    """The --capture pass/fail: no worker fault (a fault stops the live
    chain: the untranslated clip kernels), IoU of the native shadow with
    the original's dark pixels >= CAPTURE_IOU_MIN and the shadowed/lit
    luminance ratios within CAPTURE_RATIO_TOL. Measured (lane shadow-gs):
    01_battery 0.841 / 0.601 vs 0.600, 08_truck_crossing 0.837 / 0.585 vs
    0.590, 12_crevice_jump 0.861 / 0.605 vs 0.593. The frame is the
    harness's render (zone meshes + fog, no level lighting), not the live
    port."""
    beat = metric['beat']
    assert not metric['faults'], (beat, 'the shadow chain faults (untranslated clip kernel)',
                                  metric['faults'])
    assert metric['iou_shadow_vs_original_dark'] >= CAPTURE_IOU_MIN, (beat, metric)
    assert abs(metric['native_ratio']-metric['original_ratio']) <= CAPTURE_RATIO_TOL, (beat, metric)


# ------------------------------------------ G. clip kernels on the route ----
def route_clip_beat(elf, lib, beat, stats):
    """One AREA11 route beat (FIRST_LEVEL_ROUTE.md): the native 001DA6A0
    over the beat's RAM must draw without a fault; 001CB590 + 001DA6A0 run
    as original instructions over the same RAM must build the chain whose
    box uploads, receiver objects, clip classes and worker order are the
    plan's; its VU1 kernels are executed (kernel_checks) and every clip
    batch goes through clip_kernel_checks (the translation kick for kick,
    and the backend's own dmem image)."""
    folder = ROUTE/beat
    ram = (folder/'eeMemory.bin').read_bytes()
    scratch = (folder/'scratchpad.bin').read_bytes()
    scene = scene_view(ram, scratch)
    nat = NativeRun(lib, ram, scene)
    p = nat.plan
    assert nat.result == 1 and p.drawn == 1 and nat.fault.code == 0, (beat, nat.result, nat.fault.code)
    o, end, _ = execute(elf, ram, scratch, 0x1F00000)
    buf = bytearray(ram)
    for a in range(0x1F00000, end): buf[a] = o.load(a, 1)
    units = walk(buf, 0x1F00000, end)
    box_checks(buf, units, p, ram, stats)
    first = [j for j, u in enumerate(units) if u[1] == 5 and u[3] == RECEIVER_KERNEL][0]
    expected = []
    for i in range(p.receiver_count):
        addr = object_address(ram, p.receiver[i].id)+0x40
        expected.append((addr, RECEIVER_KERNEL))
        if p.receiver[i].cls == 2: expected.append((addr, CLIP_KERNEL))
    assert receiver_sequence(units, first-3) == expected, (beat, 'receiver sequence')
    log = [x for x in nat.log if not (isinstance(x, tuple) and x[0] == 'bounds')]
    want = ['alpha_clear', ('box', 0x14), ('box', 0x15), ('silhouette', p.kind), 'receiver_begin'] + \
           [('receiver', p.receiver[i].id, p.receiver[i].cls) for i in range(p.receiver_count)] + ['receiver_end']
    assert log == want, (beat, 'worker order', log[:8])
    batches = kernel_replay(elf, buf, units)
    entry = {'beat': beat, 'receivers': p.receiver_count,
             'classes': [p.receiver[i].cls for i in range(p.receiver_count)]}
    asset_checks(lib, beat, ram, scene, p, batches, stats)
    whys = kernel_checks(GSLIB, beat, 'route', ram, batches, p, stats, entry)
    before = dict(stats)
    clip_kernel_checks(GSLIB, beat, batches, whys, p, ram, stats, entry, scene.camera_3AC0)
    entry['clip_batches'] = stats.get('clip_native_batches', 0)-before.get('clip_native_batches', 0)
    entry['clip_packets'] = stats.get('clip_native_packets', 0)-before.get('clip_native_packets', 0)
    entry['clip_vertices'] = stats.get('clip_native_vertices', 0)-before.get('clip_native_vertices', 0)
    return entry


# Synthetic clip batches: the translation against the executed kernel on
# batches built to reach every branch of both programs. Positions are
# chosen in GS space (X, Y, w) and solved back through the playable
# capture's camera / box clip matrix; qwords 0..2 are random. Styles:
#   near    w in [-3, 3] around the screen (plane w = 0.1 cases, back faces)
#   wide    X, Y far outside the guard band (screen planes, split triangles)
#   mix     both
#   behind  w in (0, 0.1) with the camera's z column zeroed (otherwise the
#           +z guard plane rejects every triangle behind w = 0.1 before the
#           clip entry): the all-behind abort
#   huge    triangles around all four sides (more than 9 triangles: abort)
#   far     w up to 2e8
#   negz    the z column negated (the -z guard plane reject)
#   ftoi    receivers: z column x 1e6; box: qword 2 x 1e8 (FTOI results
#           outside int32: the translation must fault exactly there)
CLIP_STYLES = ('near', 'wide', 'mix', 'behind', 'huge', 'far', 'negz', 'ftoi')
# Conditional branches whose taken side no batch can reach: the triangle
# cap test inside the x planes' one-vertex-out case (R 0x2E0, B 0x2E6).
# The w plane leaves at most 2 triangles and each plane at most doubles
# them, so the x planes end with at most 8; only the y planes can pass 9.
CLIP_UNREACHABLE_TAKEN = {CLIP_KERNEL: {0x2E0}, BOX_CLIP_KERNEL: {0x2E6}}
SYNTH_BASE = {}


def solve_gs(m, x, y, w):
    """p with [p, 1] x m = (x w, y w, ., w) (x, y, w columns; Cramer)."""
    a = [[m[r*4+c] for r in range(3)] for c in (0, 1, 3)]
    b = [x*w-m[12], y*w-m[13], w-m[15]]
    det = lambda t: t[0][0]*(t[1][1]*t[2][2]-t[1][2]*t[2][1])-t[0][1]*(t[1][0]*t[2][2]-t[1][2]*t[2][0]) + \
        t[0][2]*(t[1][0]*t[2][1]-t[1][1]*t[2][0])
    d = det(a)
    out = []
    for j in range(3):
        t = [row[:] for row in a]
        for i in range(3): t[i][j] = b[i]
        out.append(det(t)/d)
    return out


def synthetic_batch(kernel, style, seed):
    """(top, dmem bytes) of one synthetic clip batch."""
    rng = random.Random(seed)
    camera, uv, clip_pass, k1021 = SYNTH_BASE['camera'], SYNTH_BASE['uv'], SYNTH_BASE['clip'], SYNTH_BASE['k1021']
    m = list(camera if kernel == CLIP_KERNEL else clip_pass)
    if style == 'behind':
        for r in range(4): m[4*r+2] = 0.0
    elif style == 'negz':
        for r in range(4): m[4*r+2] = -m[4*r+2]
    elif style == 'ftoi' and kernel == CLIP_KERNEL:
        for r in range(4): m[4*r+2] = m[4*r+2]*1e6
    top = GSLIB.clip_top()
    pos = []
    for i in range(32):
        if style == 'near':
            w, x, y = rng.uniform(-3, 3), rng.uniform(1500, 2600), rng.uniform(1500, 2600)
        elif style == 'wide':
            w, x, y = rng.uniform(0.5, 40), rng.uniform(-6000, 10000), rng.uniform(-6000, 10000)
        elif style == 'behind':
            w = rng.choice([rng.uniform(0.001, 0.099), rng.uniform(0.001, 0.099), rng.uniform(0.1, 3)])
            x, y = rng.choice([-6000.0, 2048.0, 10000.0])+rng.uniform(-500, 500), rng.uniform(1000, 3000)
        elif style == 'huge':
            w = rng.uniform(0.2, 30)
            x = rng.choice([-20000.0, 25000.0, 2048.0])+rng.uniform(-3000, 3000)
            y = rng.choice([-20000.0, 25000.0, 2048.0])+rng.uniform(-3000, 3000)
        elif style == 'far':
            w, x, y = rng.choice([rng.uniform(2e7, 2e8), rng.uniform(1, 100)]), rng.uniform(-3000, 7000), \
                rng.uniform(-3000, 7000)
        elif style in ('negz', 'ftoi'):
            w, x, y = rng.uniform(0.5, 50), rng.uniform(-3000, 7000), rng.uniform(-3000, 7000)
        else:
            w = rng.choice([rng.uniform(-10, 0.3), rng.uniform(0.05, 60)])
            x, y = rng.uniform(-3000, 7000), rng.uniform(-3000, 7000)
        word = rng.choice([0x3F800000, 0xBF800000, 0x3F800000, 0xBF800000, 0x3F808000, 0x3F802000])
        pos.append(solve_gs(m, x, y, w) + [struct.unpack('<f', struct.pack('<I', word))[0]])
    image = C.create_string_buffer(16384)
    assert GSLIB.clip_dmem(CLIP_ID[kernel], f4a(m), f4a(uv) if kernel == CLIP_KERNEL else None, f4a(k1021),
                           f4a([v for q in pos for v in q]), top, image) == 0
    mem = bytearray(image.raw)
    for i in range(32):
        at = (top+4*i)*16
        struct.pack_into('<2I', mem, at, rng.getrandbits(32), rng.getrandbits(32))
        scale = 1e8 if style == 'ftoi' and kernel == BOX_CLIP_KERNEL else 1.0
        struct.pack_into('<8f', mem, at+16, *[rng.uniform(-2, 2)*scale for _ in range(8)])
    return top, bytes(mem)


def clip_branches(kernel):
    """Every conditional branch of the loaded program: [micro pc]."""
    out = []
    for q in range(2048):
        lo, up = struct.unpack_from('<II', ELF_PROGRAM[kernel], 8*q)
        if not up >> 31 and lo >> 25 in (0x28, 0x29, 0x2C, 0x2D, 0x2E, 0x2F):
            out.append(q)
    return out


def synthetic_clip_worker(item):
    """One synthetic batch through the executed kernel (fresh VU1, every
    micro address watched) and the translation: equal kicks, or, when an
    FTOI result lies outside int32, a translation fault at exactly that
    kick. Returns (kernel, taken/not-taken branch edges, stats)."""
    kernel, style, seed = item
    top, mem = synthetic_batch(kernel, style, seed)
    vu1 = VU1(ELF_BYTES[0])
    load_program(vu1, ELF_BYTES[0], kernel)
    vu1.mem[:] = mem; vu1.top = top; vu1.watch = set(range(0, 16384, 8))
    vu1.run(0)
    pcs = [e[1]//8 for e in vu1.events if e[0] == 'pc']
    want = [(e[1], e[3]) for e in vu1.events if e[0] == 'kick']
    rc, res = native_clip(GSLIB, kernel, mem, top)
    got = res.kicked()
    label = (hex(kernel), style, seed)
    st = {'clip_synthetic_batches': 1, 'clip_synthetic_kicks': len(want),
          'clip_synthetic_packets': sum(1 for a, _ in want if a != 1019)}
    if vu1.ftoi_out:
        assert rc == -1 and res.fault == 2, (label, 'FTOI outside int32 without a translation fault')
        assert got == want[:len(got)] and len(got) < len(want), (label, 'kicks before the FTOI fault')
        st['clip_synthetic_ftoi_faults'] = 1
    else:
        assert rc == 0, (label, 'translation fault', res.fault)
        assert got == want, (label, 'kicks', len(got), len(want),
                             next((k for k, (g, w) in enumerate(zip(got, want)) if g != w), None))
        assert list(res.entry[:res.entries]) == [32-ev[2] for ev in vu1.events
                                                 if ev[0] == 'pc' and ev[1] == CLIP_ENTRY[kernel]], label
    return kernel, set(zip(pcs, pcs[1:])), st


def clip_coverage(edges, stats, report):
    """Both outcomes of every conditional branch of both clip programs are
    reached by the synthetic batches, except the taken side of the two
    branches CLIP_UNREACHABLE_TAKEN names."""
    rows = {}
    for kernel in (CLIP_KERNEL, BOX_CLIP_KERNEL):
        missing = set()
        branches = clip_branches(kernel)
        for q in branches:
            lo = struct.unpack_from('<I', ELF_PROGRAM[kernel], 8*q)[0]
            target = q+1+signed(lo & 0x7FF, 11)
            if (q+1, target) not in edges[kernel]: missing.add(('taken', q))
            if (q+1, q+2) not in edges[kernel]: missing.add(('not taken', q))
        want = {('taken', q) for q in CLIP_UNREACHABLE_TAKEN[kernel]}
        assert missing == want, (hex(kernel), 'branch outcomes not reached',
                                 sorted((k, hex(q)) for k, q in missing - want))
        rows[hex(kernel)] = {'branches': len(branches), 'unreached_taken': sorted(hex(q) for _, q in want)}
        stats['clip_branches_covered'] = stats.get('clip_branches_covered', 0)+len(branches)
    report['clip_coverage'] = rows


ELF_PROGRAM = {}


def merge_stats(dst, src):
    for k, v in src.items():
        if isinstance(v, dict):
            d = dst.setdefault(k, {})
            for kk, vv in v.items(): d[kk] = d.get(kk, 0)+vv
        else:
            dst[k] = dst.get(k, 0)+v


RUN = None   # (elf, lib) for the forked capture workers


def capture_worker(item):
    """run_capture for one capture in a forked worker: its report entry, its
    stats and its Metal silhouette case (the RAM is reloaded by the parent)."""
    name, ee, sp, status = item
    stats, report = {'alpha_histogram': {}}, {'captures': []}
    del METAL_CASES[:]
    run_capture(*RUN, name, ee, sp, status, stats, report)
    return report['captures'], stats, [(n, ee, vp, xy) for n, _, vp, xy in METAL_CASES]


def section_g_worker(item):
    """A route beat or a synthetic clip batch, in a forked worker."""
    if item[0] == 'beat':
        stats = {'alpha_histogram': {}}
        entry = route_clip_beat(*RUN, item[1], stats)
        return 'beat', entry, stats
    return ('synthetic',) + synthetic_clip_worker(item[1:])


def section_g_items(elf, lib):
    """G. The clip kernels 00239C90 / 0023E8A0: all 15 route beats
    (route_clip_beat) and the synthetic batches (quick: two per style and
    kernel; EM_TEST_FULL=1: twelve). Run in the same worker pool as the
    captures; section_g_finish takes their results."""
    beats = sorted(p.name for p in ROUTE.iterdir() if p.is_dir() and in_scope_beat(p.name))
    assert len(beats) == 15, ('route beats', beats)
    ram = (REF/'playable_ee.bin').read_bytes()
    scene = scene_view(ram, None)
    nat = NativeRun(lib, ram, scene)
    coef = (C.c_float*2)()
    GSLIB.coefficients(-209.0, 304.0, coef)
    SYNTH_BASE.update(camera=list(scene.camera_3AC0), uv=list(nat.plan.uv_24B0),
                      clip=list(nat.plan.box[0].clip_pass), k1021=[255.0, 2048.0, coef[0], coef[1]])
    for kernel in (CLIP_KERNEL, BOX_CLIP_KERNEL):
        vu1 = VU1(elf)
        load_program(vu1, elf, kernel)
        ELF_PROGRAM[kernel] = bytes(vu1.code)
    per_style = pick(12, 2)
    return [('beat', b) for b in beats] + \
        [('synthetic', k, style, 1000*n+j) for k in (CLIP_KERNEL, BOX_CLIP_KERNEL)
         for j, style in enumerate(CLIP_STYLES) for n in range(per_style)]


def section_g_finish(results, report, stats):
    edges = {CLIP_KERNEL: set(), BOX_CLIP_KERNEL: set()}
    rows = []
    for result in results:
        if result[0] == 'beat':
            rows.append(result[1])
            merge_stats(stats, result[2])
        else:
            _, kernel, e, st = result
            edges[kernel] |= e
            merge_stats(stats, st)
    report['route_clip'] = rows
    clip_coverage(edges, stats, report)
    return len(rows), stats.get('clip_synthetic_batches', 0)


def main_worker(item):
    """A capture (capture_worker) or a section G item, in a forked worker."""
    if item[0] == 'capture':
        return capture_worker(item[1])
    return section_g_worker(item)


def main():
    import argparse
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--capture', metavar='BEAT', help='only the headless capture metric of a route beat '
                    '(e.g. 06_hill_slide; needs the exported AREA11 assets)')
    args = ap.parse_args()
    elf = (DECOMP/'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA256, 'not the pinned SCUS-97112 ELF'
    vu.ELF = elf
    ELF_BYTES[0] = elf
    out = ROOT/'build/shadow_original_reference'; out.mkdir(parents=True, exist_ok=True)
    validate_ops(elf)
    check_mpg(elf)
    lib = native_library(out)
    global GSLIB
    GSLIB = gs_library(out)
    if args.capture:
        report = {}
        capture_metric(elf, lib, GSLIB, args.capture, report)
        (out/'capture_metric.json').write_text(json.dumps(report, indent=1))
        capture_bounds(report['capture_metric'])
        print('PASS capture', args.capture)
        return
    stats = {'alpha_histogram': {}}
    report = {'captures': []}
    rotation_probe(elf, stats)
    global RUN
    RUN = (elf, lib)
    cost = {'roger-encounter': 6, 'opening': 6, 'handoff': 4}
    metal_cases = []
    items = [('capture', c) for c in CAPTURES] + section_g_items(elf, lib)
    weight = lambda it: cost.get(it[1][0], 2) if it[0] == 'capture' else 2 if it[0] == 'beat' else 1
    results = parallel_map(main_worker, items, cost=weight)
    for entries, delta, cases in results[:len(CAPTURES)]:
        report['captures'] += entries
        merge_stats(stats, delta)
        metal_cases += [(n, (REF/ee).read_bytes(), vp, xy) for n, ee, vp, xy in cases]
    beats, synthetic = section_g_finish(results[len(CAPTURES):], report, stats)
    base = ((REF/'playable_ee.bin').read_bytes(), None)
    state_blocks(base[0], stats)
    for name, ee, sp in NOT_DRAWN:
        ram = (REF/ee).read_bytes()
        scratch = (REF/sp).read_bytes() if sp else None
        o, end, _ = execute(elf, ram, scratch, 0x1F00000)
        nat = NativeRun(lib, ram, scene_view(ram, scratch))
        emitted = (end-0x1F00000)//16
        assert emitted == 0 and nat.plan.drawn == 0 and nat.fault.code == 0, name
        starts = chain_starts(ram)
        cursor = u32(ram, u32(ram, CONTEXT_PTR)+0x10)
        nearest = min(starts, key=lambda s: abs(s-cursor))
        assert nearest > cursor, (name, 'a chain lies in the list being built', hex(nearest), hex(cursor))
        report.setdefault('not_drawn_captures', []).append(
            {'capture': name, 'chain_starts': [hex(s) for s in starts], 'cursor': hex(cursor),
             'nearest_chain_above_cursor': True, 'executed_001DA6A0_emits_qwords': emitted,
             'native_drawn': nat.plan.drawn, 'probe_clip': [hex(c) for c in nat.plan.probe_clip]})
    route_0015C160(elf, lib, report, stats)
    roger_checks(elf, report, stats)
    gate_cases(elf, lib, *base, stats, report)
    area_switch(elf, lib, *base, stats)
    gs_checks(report, stats)
    # F. Metal backend (after every forked section: Cocoa is not fork-safe)
    metal = Metal(out)
    if metal_cases:
        metal_silhouette(metal, metal_cases, stats, report)
    else:
        report['metal_silhouette'] = 'skipped: no assets/player_shadow.emdl'
    metal_receiver_pixels(metal, GSLIB, stats, report)
    stats['alpha_histogram'] = {str(k): v for k, v in sorted(stats['alpha_histogram'].items())}
    report['stats'] = stats
    (out/'report.json').write_text(json.dumps(report, indent=1))
    print(json.dumps(stats))
    for c in report['captures']:
        print(c['capture'], 'lists', c['lists'], 'receivers', c['receivers'], 'vertices', c['vertices'],
              'clip batches', c['clip_batches'])
    for c in report.get('not_drawn_captures', []):
        print('not drawn:', c)
    for r in report['route_clip']:
        print('route', r['beat'], 'receivers', r['receivers'], 'clip batches', r['clip_batches'],
              'drawing clip packets', r['clip_packets'], 'clip vertices', r['clip_vertices'])
    if stats.get('asset_skipped'):
        print(f"receiver asset missing: {stats['asset_skipped']} asset checks skipped "
              "(run tools/export_shadow_receivers.py)")
    banner(part(stats['area_keys_checked'], stats['area_keys_total'], 'area keys'),
           part(synthetic, 12*len(CLIP_STYLES)*2, 'synthetic clip batches'), f'{beats} route beats')
    print('PASS test_shadow_original_reference')


if __name__ == '__main__':
    main()
