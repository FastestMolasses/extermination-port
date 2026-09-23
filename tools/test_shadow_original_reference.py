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
    o.plain(0x70000000 | 9 << 21 | 8 << 16 | 12 << 11 | 0x12 << 6 | 8)   # pextlw $12, $9, $8
    w = lambda v, i: (v >> (32*i)) & 0xFFFFFFFF
    assert o.r[12] == (w(o.r[8], 0) | w(o.r[9], 0) << 32 | w(o.r[8], 1) << 64 | w(o.r[9], 1) << 96)
    o.r[5] = 0x8000_0000_0000_0000
    o.plain(0x0005283F)       # dsra32 $5, $5, 0
    assert o.r[5] == 0xFFFF_FFFF_8000_0000
    o.f[1], o.f[2] = bits(3.0), bits(0.1)
    o.plain(0x46020818)       # adda.s ACC = f1 + f2
    assert o.facc == fp(3.0+number(bits(0.1)))
    o.v[1] = [bits(2.0), bits(-3.0), bits(0.5), bits(1.0)]
    o.macro(0x4BC109FF)       # vclipw.xyz vf1, vf1w
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
    def __init__(self, lib, ram, scene, actor=None, nodes=None, ff0=None, fail_at=None):
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

        def bounds(_, ident, lo, hi):
            self.log.append(('bounds', ident))
            obj = object_address(ram, ident)
            for i in range(3):
                lo[i] = struct.unpack_from('<f', ram, obj+0x14+4*i)[0]
                hi[i] = struct.unpack_from('<f', ram, obj+0x24+4*i)[0]
            return -1 if self.fail_at == 'bounds' else 0
        self.cb = [BOUNDS(bounds), PLAIN(lambda _: rec('alpha_clear')),
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
    for label, patch in variations:
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
        cases.append({'case': label, 'drawn': p.drawn, 'emitted_qwords': emitted,
                      'alpha_matrix': p.alpha_matrix, 'far_index': p.far_index, 'near_index': p.near_index,
                      'receivers': p.receiver_count})
    report['gate_cases'] = cases
    stats['gate_cases'] = len(cases)


def area_switch(elf, lib, ram, scratch, stats):
    """Every key of 001D98A0's area switch (0x1D9C44..0x1D9E78), read from
    the ELF's addiu/beq pairs, through the native module; plus every other
    key 0x0000..0x17FF, which must select no alpha matrix."""
    word = lambda a: u32(elf, a-0x100000+0x300)
    targets = {0x1D9E98: 2, 0x1D9E7C: 1}
    keys = {}
    for b in range(0x1D9C4C, 0x1D9E78, 4):
        if word(b) >> 16 != 0x1043: continue                 # beq v0, v1, target
        a = max(x for x in range(b-24, b, 4) if word(x) >> 16 == 0x2403)   # addiu v1, zero, imm
        keys[word(a) & 0xFFFF] = targets[b+4+(signed(word(b) & 0xFFFF, 16) << 2)]
    assert word(0x1D9CBC) >> 26 == 4 and word(0x1D9CBC) >> 16 & 31 == 0   # beqz v0 -> key 0
    keys[0] = targets[0x1D9CC0+(signed(word(0x1D9CBC) & 0xFFFF, 16) << 2)]
    assert len(keys) == 45, len(keys)
    scene = scene_view(ram, scratch)
    checked = 0
    for key in range(0x1800):
        mod = bytearray(ram); mod[0x810700] = key >> 8; mod[0x810701] = key & 255
        scene.area_700, scene.sub_701 = key >> 8, key & 255
        nat = NativeRun(lib, bytes(mod), scene)
        assert nat.plan.alpha_matrix == keys.get(key, 0), (hex(key), nat.plan.alpha_matrix)
        checked += 1
    stats['area_keys_checked'] = checked
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


def main():
    elf = (DECOMP/'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA256, 'not the pinned SCUS-97112 ELF'
    vu.ELF = elf
    out = ROOT/'build/shadow_original_reference'; out.mkdir(parents=True, exist_ok=True)
    validate_ops(elf)
    lib = native_library(out)
    stats = {'alpha_histogram': {}}
    report = {'captures': []}
    rotation_probe(elf, stats)
    base = None
    for name, ee, sp, status in CAPTURES:
        ram, scratch = run_capture(elf, lib, name, ee, sp, status, stats, report)
        if name == 'playable':
            base = (ram, scratch)
            state_blocks(ram, stats)
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
    stats['alpha_histogram'] = {str(k): v for k, v in sorted(stats['alpha_histogram'].items())}
    report['stats'] = stats
    (out/'report.json').write_text(json.dumps(report, indent=1))
    print(json.dumps(stats))
    for c in report['captures']:
        print(c['capture'], 'lists', c['lists'], 'receivers', c['receivers'], 'vertices', c['vertices'],
              'clip batches', c['clip_batches'])
    for c in report.get('not_drawn_captures', []):
        print('not drawn:', c)
    print('PASS test_shadow_original_reference')


if __name__ == '__main__':
    main()
