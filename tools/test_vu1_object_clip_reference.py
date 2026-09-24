#!/usr/bin/env python3
"""The object kernel's guard-band clip pass (the VU1 program the kernel
packet 0x002354A0 uploads) against its original microcode.

Reads the owner's pinned ELF and the captured EE RAM under
../Extermination/build/s87/route and ../Extermination/build/startup-reference;
embeds no original bytes. The report holds addresses, counts and hashes only.
Docs: docs/VU1_OBJECT_CLIP.md. Translation: src/game/em_vu1_object_clip.h.

The oracle is the shadow lane's VU1 interpreter
(tools/test_shadow_original_reference.py: in-order issue, VF operand stalls,
MAC/clip flags 4 cycles after the op, Q after DIV/WAITQ, truncated binary32,
XGKICK snapshots). It runs the ORIGINAL program loaded from the kernel
packet's MPG blocks in the ELF.

P. The kernel packet: CNT with 459 qwords, BASE 0x1B0, OFFSET 0x84, four MPG
   blocks at micro 0x000/0x100/0x200/0x300 (910 instructions); the object
   kernel packet 0x0023C750 sets the same BASE with OFFSET 0x10E.
A. The 14 captured clip units (docs/OWNER_DRAW.md section 5): for every
   owner with draw method 001CAA00 and a bank model in the route beats
   00..14, the ORIGINAL 001CA7B0 classifies it (the owner-draw oracle); every
   'clip' owner's unit is built by the ORIGINAL 001CAA00 and located in the
   captured display list. The captured unit bytes are replayed through
   DMAC/VIF1 on one VU1 (CNT payloads from the list, REF targets from RAM,
   the CALLed kernel packets from the ELF): the object kernel runs, then the
   clip program on every batch of the second model REF. On every clip batch:
   - the translation over the interpreter's data memory at the MSCAL/MSCNT
     kicks what the interpreter kicks (dmem address, order, every packet
     byte), reaches the same clip entries (micro 0x04B, vertex i), tags
     each kick with its entry's vertex i (32 for the kick after the loop)
     and leaves the same data memory (all 1024 qwords);
   - the native image: em_vu1_object_clip_image from the unit's own pieces
     (the colour CNT, the node CNT, skin record 1; no captured clip unit
     carries the fog-off REF 2, whose row section C covers)
     and em_vu1_object_clip_batch for each model block, run in order on one
     image, kicks the same as the replay (so the program reads nothing the
     unit does not upload, and the object pass before it changes nothing it
     reads); the image built over a buffer of random bytes is the same (every
     qword the pieces do not fill is zeroed);
   - the interpreter re-run on that image with every VF/VI register, ACC,
     Q, I and the clip flags filled with random words kicks the same (no
     state is carried in from an earlier program);
   - em_vu1_object_clip_triangles equals an independent Python decode of the
     interpreter's GIF packets (TEX0, PRIM, every ST/Q/RGBA/X/Y/Z/F), and
     every captured PRIM is 0x03B;
   - the clipping cases each batch reaches are counted (micro addresses of
     the shared case code, CASES);
   - relation to the object pass over the same block: the object kernel's
     ADC on vertex i is exactly "data word bit 15, or a non-zero guard-band
     clip history" (the clip flags the interpreter holds at micro 0x01E),
     and vertex i is entered exactly when that ADC is set, bit 15 is clear
     and the three vertices are not all outside one guard plane.
B. Every other intact clip unit found in the captured memory (both display
   lists of each route beat and of the startup-reference captures,
   including two 21-node units), deduplicated by content: the same checks.
   Quick mode runs a covering sample (one per node count); EM_TEST_FULL=1
   runs all.
C. Synthetic batches on the husk-partner unit's image (2 nodes), solved back
   through its node matrices from GS-space targets: near (w around 0.1),
   wide, huge (many triangles), mix, behind, far (A + B w below 0), flags
   (random data-word bits 10..15, entries at i = 0/1 reading below TOP),
   slots (both nodes), fog-off row, FTOI overflow (the translation must
   fault exactly at the kick the interpreter reaches with a wrapped FTOI),
   fogmax (fog row A 400, B -10: A + B w above 255, the min(F, 255) clamp;
   the pre-clamp value is read at FOG_PC and must exceed 255 somewhere) and
   boundary (node 0's position matrix exact, so vertices sit exactly on
   w = 0.1, x = 4088 / 4, y = 4088 / 4, where the difference the program
   tests is +0; the same batch with those vertices one ulp outside must
   kick something else, so the tie-break decides the output).
   Random registers before every batch. Every micro address is traced: both
   outcomes of every conditional branch of the program must be reached
   except those listed in UNREACHED (with the reason).

The shim is built with UBSan (recoverable, logged to build/...); any report
fails the run. EM_VU1_OBJECT_CLIP_SRC=<dir> builds against
<dir>/game/em_vu1_object_clip.h instead (defect injection).

No original instruction bytes, disassembly or data are written by this file.
"""
from pathlib import Path
import ctypes as C
import hashlib
import json
import os
import random
import shutil
import struct
import subprocess
import sys
import time

from reference_mode import FULL, banner, part, pick, select, parallel_map
import test_owner_draw_reference as od
from test_shadow_original_reference import VU1, signed

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
ROUTE = DECOMP / 'build/s87/route'
STARTUP = DECOMP / 'build/startup-reference'
OUT = ROOT / 'build/vu1_object_clip_reference'
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'

CLIP_KERNEL, OBJECT_KERNEL = 0x2354A0, 0x23C750
ENTRY_PC, FLAGS_PC = 0x04B, 0x01E           # micro addresses (instruction index)
FOG_PC = 0x1BE                               # F = min(A + B w, 255): the fog row's x lane
# the clipping cases' shared code (micro address -> name), counted per batch
CASES = {0x220: 'w_two_behind', 0x25B: 'w_one_behind', 0x2A1: 'x_two_out', 0x2D9: 'x_one_out',
         0x319: 'y_two_out', 0x351: 'y_one_out', 0x382: 'collapsed'}
DRAW = 0x1CAA00
# docs/OWNER_DRAW.md section 5: the 14 captured clip units (beat, behaviour)
CLIP_UNITS = [('00_panel_no_battery', 0x823E80), ('01_battery', 0x823E80), ('02_elevator_refusal', 0x823E80),
              ('02_elevator_refusal', 0x159210), ('02_elevator_refusal', 0x827B10), ('03_panel_power', 0x823E80),
              ('04_elevator_ride', 0x823E80), ('04_elevator_ride', 0x823FF0), ('05_boxes', 0x827B10),
              ('05_boxes', 0x1C4820), ('08_truck_crossing', 0x1551B0), ('09_fence_door', 0x827490),
              ('09_fence_door', 0x1BC350), ('11_crevice_prompt', 0x827490)]
# conditional branches whose one outcome no input reaches (micro address ->
# 'taken' / 'not taken'): none; the sweep reaches both outcomes of all 50
UNREACHED = {}

ELF = None
CAP = {}          # capture name -> RAM bytes
LIB = None
BASE_UNIT = None  # synthetic base: (nodes words, node count, colour words, record words)


def u32(b, a): return struct.unpack_from('<I', b, a)[0]
def e32(a): return u32(ELF, a - 0x100000 + 0x300)
def fbits(f): return struct.unpack('<I', struct.pack('<f', f))[0]


# ------------------------------------------------------------ native shim

SHIM = r'''
#include <stddef.h>
#include "game/em_vu1_object_clip.h"
int run(uint8_t *dmem, unsigned top, EmVu1ObjectClipResult *r)
{ return em_vu1_object_clip_run((EmVu1Qword *)dmem, top, r); }
int triangles(const EmVu1ObjectClipResult *r, EmVu1ObjectClipTriangle *t, unsigned cap)
{ return em_vu1_object_clip_triangles(r, t, cap); }
int image(uint8_t *dmem, const uint32_t *nodes, unsigned n, const uint32_t *color, const uint32_t *record)
{ return em_vu1_object_clip_image((EmVu1Qword *)dmem, nodes, n, color, record); }
int batch(uint8_t *dmem, unsigned index, const uint8_t *block)
{ return em_vu1_object_clip_batch((EmVu1Qword *)dmem, index, block); }
unsigned layout(unsigned k)
{
    switch (k) {
    case 0: return sizeof(EmVu1ObjectClipResult);
    case 1: return offsetof(EmVu1ObjectClipResult, kicks);
    case 2: return offsetof(EmVu1ObjectClipResult, qwords);
    case 3: return offsetof(EmVu1ObjectClipResult, qw);
    case 4: return sizeof(EmVu1ObjectClipTriangle);
    case 5: return offsetof(EmVu1ObjectClipTriangle, v);
    case 6: return sizeof(EmVu1ObjectClipVertex);
    case 7: return EM_VU1_OBJECT_CLIP_MAX_QWORDS;
    default: return 0;
    }
}
'''


class Kick(C.Structure):
    _fields_ = [('addr', C.c_uint32), ('vertex', C.c_uint32), ('first', C.c_uint32), ('count', C.c_uint32)]


class Result(C.Structure):
    """EmVu1ObjectClipResult."""
    _fields_ = [('fault', C.c_uint32), ('entries', C.c_uint32), ('entry', C.c_uint8 * 32),
                ('kicks', C.c_uint32), ('kick', Kick * 65), ('qwords', C.c_uint32),
                ('capacity', C.c_uint32), ('qw', C.c_void_p)]


class Vertex(C.Structure):
    _fields_ = [('s', C.c_float), ('t', C.c_float), ('q', C.c_float), ('rgba', C.c_uint8 * 4),
                ('x', C.c_uint16), ('y', C.c_uint16), ('z', C.c_uint32), ('f', C.c_uint8)]


class Triangle(C.Structure):
    _fields_ = [('tex0', C.c_uint64), ('prim', C.c_uint32), ('vertex', C.c_uint32), ('v', Vertex * 3)]


MAX_QWORDS = 32 * (3 + 9 * 32) + 2


def build_library():
    """The shim over src/game/em_vu1_object_clip.h (EM_VU1_OBJECT_CLIP_SRC names
    another source root holding game/em_vu1_object_clip.h, for defect injection)."""
    OUT.mkdir(parents=True, exist_ok=True)
    alt = os.environ.get('EM_VU1_OBJECT_CLIP_SRC')
    tag = f'_{os.getpid()}' if alt else ''
    src, path = OUT / f'vu1_object_clip{tag}.c', OUT / f'vu1_object_clip{tag}.dylib'
    src.write_text(SHIM)
    inc = ['-I' + alt] if alt else []
    # UBSan in recoverable mode: a report goes to build/.../ubsan.<pid> and
    # main() fails on any such file (an abort would hang the worker pool)
    for old in OUT.glob('ubsan.*'):
        old.unlink()
    os.environ['UBSAN_OPTIONS'] = f'log_path={OUT / "ubsan"}:print_stacktrace=1'
    try:
        subprocess.run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                        '-fsanitize=undefined', '-shared', '-fPIC', *inc, '-I' + str(ROOT / 'src'), str(src),
                        '-o', str(path)], check=True)
        lib = C.CDLL(str(path))
    finally:
        if alt:                         # a defect-injection build is not kept, compiled or not
            src.unlink(missing_ok=True); path.unlink(missing_ok=True)
            shutil.rmtree(str(path) + '.dSYM', ignore_errors=True)
    lib.run.argtypes = [C.c_char_p, C.c_uint, C.POINTER(Result)]
    lib.triangles.argtypes = [C.POINTER(Result), C.POINTER(Triangle), C.c_uint]
    lib.image.argtypes = [C.c_char_p, C.POINTER(C.c_uint32), C.c_uint, C.POINTER(C.c_uint32),
                          C.POINTER(C.c_uint32)]
    lib.batch.argtypes = [C.c_char_p, C.c_uint, C.c_char_p]
    lib.layout.restype = C.c_uint
    want = [C.sizeof(Result), Result.kicks.offset, Result.qwords.offset, Result.qw.offset,
            C.sizeof(Triangle), Triangle.v.offset, C.sizeof(Vertex), MAX_QWORDS]
    assert [lib.layout(k) for k in range(8)] == want, ('ctypes layout', [lib.layout(k) for k in range(8)], want)
    return lib


class Native:
    """One translation run: result + its qword storage."""

    def __init__(self, dmem, top):
        self.buf = C.create_string_buffer(bytes(dmem), 16384)
        self.store = (C.c_uint32 * (4 * MAX_QWORDS))()
        self.res = Result()
        self.res.capacity, self.res.qw = MAX_QWORDS, C.addressof(self.store)
        self.rc = LIB.run(self.buf, top, C.byref(self.res))

    def kicks(self):
        raw = bytes(self.store)
        r = self.res
        return [(r.kick[i].addr, raw[16 * r.kick[i].first:16 * (r.kick[i].first + r.kick[i].count)])
                for i in range(r.kicks)]

    def entries(self): return list(self.res.entry[:self.res.entries])

    def kick_vertices(self): return [self.res.kick[i].vertex for i in range(self.res.kicks)]

    def triangles(self):
        out = (Triangle * 1024)()
        n = LIB.triangles(C.byref(self.res), out, 1024)
        assert n >= 0, 'em_vu1_object_clip_triangles refused a kicked batch'
        return [(t.tex0, t.prim, t.vertex,
                 [(fbits(v.s), fbits(v.t), fbits(v.q), tuple(v.rgba), v.x, v.y, v.z, v.f) for v in t.v])
                for t in out[:n]]


def kick_vertices(kicks, entries):
    """EmVu1ObjectClipKick.vertex of each of the interpreter's kicks: a 1018
    kick inside the loop opens the next entry (its vertex i), a 696 kick
    belongs to the entry in progress, the last kick (after the loop) is 32."""
    out, e = [], 0
    for k, (addr, _) in enumerate(kicks):
        if k == len(kicks) - 1:
            assert addr == 1018, 'the kick after the loop is not dmem 1018'
            out.append(32)
        elif addr == 1018:
            out.append(entries[e]); e += 1
        else:
            assert addr == 696 and e, ('packet kick outside an entry', addr)
            out.append(entries[e - 1])
    assert e == len(entries), ('entries without their TEX0 kick', e, len(entries))
    return out


# ------------------------------------------------------------ interpreter

class Trace(set):
    """The watched micro addresses (bytes). At the entry the vertex base vi14
    is recorded, after the loop's clip test the clip flags and data word.
    Every issued micro address passes through here (the interpreter asks
    `pc in watch` per instruction): the shared case code is counted, and at
    FOG_PC (the upper clamp of F) the pre-clamp A + B w in vf1.w is kept."""

    def __init__(self, vu, pcs, top):
        super().__init__(pcs)
        self.vu, self.top, self.entries, self.flags, self.cases = vu, top, [], [], {}
        self.fog_above = 0            # output vertices whose A + B w exceeds 255

    def __contains__(self, pc):
        if pc // 8 in CASES and not pc % 8:
            self.cases[CASES[pc // 8]] = self.cases.get(CASES[pc // 8], 0) + 1
        if pc == 8 * FOG_PC and struct.unpack('<f', struct.pack('<I', self.vu.v[1][3]))[0] > 255.0:
            self.fog_above += 1
        if pc == 8 * ENTRY_PC:
            self.entries.append(((self.vu.vi[14] - self.top) & 0xFFFF) // 4)
        elif pc == 8 * FLAGS_PC:
            self.flags.append((((self.vu.vi[14] - self.top) & 0xFFFF) // 4, self.vu.cf, self.vu.vi[10]))
        return set.__contains__(self, pc)


def load_kernel(vu, kernel):
    """Apply a CALLed kernel packet's VIF codes from the ELF: its MPG blocks
    into micro memory; returns the STCYCL/BASE/OFFSET values it sets."""
    w0 = e32(kernel)
    assert (w0 >> 28) & 7 == 1, ('kernel packet tag', hex(kernel))
    i, e, st, mpg = kernel + 16, kernel + 16 + 16 * (w0 & 0xFFFF), {}, []
    while i < e:
        v = e32(i); cmd, num, imm = (v >> 24) & 0x7F, (v >> 16) & 0xFF, v & 0xFFFF
        if cmd == 0x4A:
            n = num or 256; a = i + 4 - 0x100000 + 0x300
            vu.code[imm * 8:imm * 8 + 8 * n] = ELF[a:a + 8 * n]
            mpg.append((imm, n)); i += 4 + 8 * n; continue
        if cmd == 0x01: st['cl'], st['wl'] = imm & 0xFF, (imm >> 8) & 0xFF
        elif cmd == 0x03: st['base'] = imm & 0x3FF
        elif cmd == 0x02: st['offset'] = imm & 0x3FF
        else: assert cmd in (0x00, 0x05, 0x10, 0x11, 0x13, 0x20), ('kernel VIF code', hex(v))
        i += 4 + (4 if cmd == 0x20 else 0)
    return st, mpg


def check_packets():
    """P: the two kernel packets' shape."""
    vu = VU1(ELF)
    st, mpg = load_kernel(vu, CLIP_KERNEL)
    assert e32(CLIP_KERNEL) & 0xFFFF == 459 and st == {'cl': 4, 'wl': 4, 'base': 0x1B0, 'offset': 0x84}, st
    assert mpg == [(0, 256), (0x100, 256), (0x200, 256), (0x300, 142)], mpg
    st2, mpg2 = load_kernel(VU1(ELF), OBJECT_KERNEL)
    assert st2 == {'cl': 4, 'wl': 4, 'base': 0x1B0, 'offset': 0x10E} and mpg2 == [(0, 62)], (st2, mpg2)
    return sum(n for _, n in mpg)


def replay(ram, tags):
    """The unit's DMA tags through VIF1 on one fresh VU1. Returns per
    MSCAL/MSCNT: dict(kernel, top, block, before, after, kicks, entries,
    flags); kicks = [(dmem address, packet bytes)]."""
    vu = VU1(ELF)
    st = dict(base=0, offset=0, tops=0, dbf=0, cl=1, wl=1)
    kernel, out = None, []

    def vif(src, i, e, block_base):
        while i < e:
            v = struct.unpack_from('<I', src, i)[0]
            cmd, imm, num = (v >> 24) & 0x7F, v & 0xFFFF, (v >> 16) & 0xFF
            if cmd >= 0x60:
                assert cmd & 0x0F == 0x0C and st['wl'] <= st['cl'], ('unpack', hex(v))
                cnt = num or 256
                dst = (imm & 0x3FF) + (st['tops'] if imm & 0x8000 else 0)
                for k in range(cnt):
                    d = (dst + (k // st['wl']) * st['cl'] + k % st['wl']) & 1023
                    vu.mem[d * 16:d * 16 + 16] = src[i + 4 + 16 * k:i + 20 + 16 * k]
                i += 4 + 16 * cnt; continue
            if cmd in (0x50, 0x51):                    # DIRECT / DIRECTHL: GIF data, not VU
                i = (i + 4 + 15) // 16 * 16 + 16 * imm; continue
            if cmd in (0x14, 0x15, 0x17):
                top = st['tops']; st['dbf'] ^= 1
                st['tops'] = st['base'] + (st['offset'] if st['dbf'] else 0)
                entry = vu.resume if cmd == 0x17 else 8 * imm
                before = bytes(vu.mem)
                trace = Trace(vu, {8 * ENTRY_PC, 8 * FLAGS_PC}, top)
                vu.top, vu.kicks, vu.events, vu.watch = top, [], [], trace
                wrapped = vu.ftoi_out                 # cumulative in the interpreter
                vu.run(entry)
                block = None if block_base is None else (i - block_base) // (16 * 0x82)
                out.append(dict(kernel=kernel, top=top, block=block, before=before, after=bytes(vu.mem),
                                kicks=[(ev[1], ev[3]) for ev in vu.events if ev[0] == 'kick'],
                                entries=trace.entries, flags=trace.flags, cases=trace.cases,
                                fog_above=trace.fog_above, ftoi=vu.ftoi_out - wrapped))
                i += 4; continue
            if cmd == 0x01: st['cl'], st['wl'] = imm & 0xFF, (imm >> 8) & 0xFF; i += 4; continue
            if cmd == 0x03: st['base'] = imm & 0x3FF; i += 4; continue
            if cmd == 0x02:
                st['offset'] = imm & 0x3FF; st['dbf'] = 0; st['tops'] = st['base']; i += 4; continue
            if cmd == 0x20: i += 8; continue
            if cmd in (0, 0x10, 0x11, 0x13): i += 4; continue
            raise AssertionError(('vif', hex(v)))

    for a, tid, qwc, addr in tags:
        if tid == 5:
            kernel = addr
            st.update({k: v for k, v in load_kernel(vu, addr)[0].items()})
            st['dbf'], st['tops'] = 0, st['base']
        elif tid == 1:
            vif(ram, a + 16, a + 16 + 16 * qwc, None)
        elif tid == 3:
            vif(ram, addr, addr + 16 * qwc, addr)
        else:
            raise AssertionError(('tag', tid))
    return out


def randomize(vu, rng):
    for r in range(1, 32):
        vu.v[r] = [rng.getrandbits(32) for _ in range(4)]
    for r in range(1, 16):
        vu.vi[r] = rng.getrandbits(16)
    vu.acc = [rng.uniform(-1e6, 1e6) for _ in range(4)]
    vu.q, vu.i, vu.cf = rng.uniform(-1e6, 1e6), rng.uniform(-1e6, 1e6), rng.getrandbits(24)


def run_program(mem, top, rng=None, watch_all=False):
    """The clip program on one batch image (fresh VU1, optionally random
    registers). Returns (kicks, entries, flags, after, pcs, ftoi, fog_above)."""
    vu = VU1(ELF)
    load_kernel(vu, CLIP_KERNEL)
    if rng is not None:
        randomize(vu, rng)
    vu.mem[:] = mem
    pcs = set(range(0, 8 * 0x38E, 8)) if watch_all else {8 * ENTRY_PC, 8 * FLAGS_PC}
    trace = Trace(vu, pcs, top)
    vu.top, vu.kicks, vu.events, vu.watch = top, [], [], trace
    vu.run(0)
    path = [ev[1] // 8 for ev in vu.events if ev[0] == 'pc'] if watch_all else []
    return ([(ev[1], ev[3]) for ev in vu.events if ev[0] == 'kick'], trace.entries, trace.flags,
            bytes(vu.mem), path, vu.ftoi_out, trace.fog_above)


# ------------------------------------------------------------ GIF decode (Python, independent)

def py_triangles(kicks, entries):
    """[(tex0, prim, entry vertex, [(s, t, q bits, rgba, x, y, z, f)] * 3)]."""
    out, tex0, e = [], None, 0
    for addr, raw in kicks:
        q, entry = 0, (entries[e - 1] if e else None)
        if addr == 1018:
            e += 1
        while q < len(raw):
            lo, hi = struct.unpack_from('<QQ', raw, q)
            q += 16
            nloop, flg, nreg = lo & 0x7FFF, (lo >> 58) & 3, (lo >> 60) or 16
            prim = (lo >> 47) & 0x7FF if (lo >> 46) & 1 else None
            assert flg == 0
            regs = [(hi >> (4 * r)) & 15 for r in range(nreg)]
            verts, st = [], None
            for _ in range(nloop):
                for r in regs:
                    w = struct.unpack_from('<4I', raw, q); q += 16
                    if r == 6: tex0 = w[0] | w[1] << 32
                    elif r == 2: st = w[:3]
                    elif r == 1: rgba = tuple(x & 0xFF for x in w)
                    elif r == 4:
                        assert not (w[3] >> 15) & 1
                        verts.append((st[0], st[1], st[2], rgba, w[0] & 0xFFFF, w[1] & 0xFFFF,
                                      (w[2] >> 4) & 0xFFFFFF, (w[3] >> 4) & 0xFF))
                    else: raise AssertionError(('register', r))
            for k in range(0, len(verts), 3):
                out.append((tex0, prim, entries[e - 1] if addr == 696 else entry, verts[k:k + 3]))
    return out


# ------------------------------------------------------------ units

def unit_tags(ram, at, end):
    out, o = [], at
    while o < end:
        w0 = u32(ram, o); qwc, tid = w0 & 0xFFFF, (w0 >> 28) & 7
        out.append((o, tid, qwc, u32(ram, o + 4)))
        o += 16 + (16 * qwc if tid in (0, 1) else 0)
    assert o == end, 'unit tags overrun'
    return out


def unit_pieces(ram, tags):
    """(nodes words, node count, colour words, record words, model address,
    blocks) of a unit in the 001CA990 form, or None."""
    vifw = lambda a: struct.unpack_from('<4I', ram, a)
    t = tags
    if len(t) < 10 or t[0][1] != 1 or t[0][2] != 5 or t[1][1] != 1:
        return None
    if vifw(t[0][0] + 16) != (0, 0x11000000, 0x01000101, 0x6C0403F5):
        return None
    n8 = t[1][2] - 1
    if n8 % 8 or n8 > 248 or vifw(t[1][0] + 16)[2:] != (0x01000101, 0x6C000000 | (n8 & 0xFF) << 16):
        return None
    calls = [i for i, x in enumerate(t) if x[1] == 5 and x[3] == CLIP_KERNEL]
    if len(calls) != 1:
        return None
    c = calls[0]
    skin, arena = t[c - 2], t[c - 1]
    if skin[1] != 3 or skin[2] != 8 or arena[1] != 3 or arena[2] != 1:
        return None
    if vifw(skin[3]) != (0, 0, 0x01000404, 0x6C0703F9):
        return None
    record = list(struct.unpack_from('<28I', ram, skin[3] + 16))
    rest = t[c + 1:]
    if rest and rest[0][1] == 3 and rest[0][3] == 0x2514B0:
        return None   # the fog-off REF 2: no captured clip unit carries it (section C covers the row)
    if len(rest) != 1 or rest[0][1] != 3 or rest[0][2] % 0x82:
        return None
    nodes = list(struct.unpack_from(f'<{4 * n8}I', ram, t[1][0] + 32))
    color = list(struct.unpack_from('<16I', ram, t[0][0] + 32))
    return nodes, n8 // 8, color, record, rest[0][3], rest[0][2] // 0x82


def owner_units():
    """A: the captured clip units of the owner draw (docs/OWNER_DRAW.md)."""
    items = []
    for beat in od.BEATS:
        ram = CAP[beat]
        table = u32(ram, 0x28A59C)
        bank = {table + (u32(ram, table + 4 + 4 * i) >> 2 << 2) for i in range(u32(ram, table))}
        a, seen = u32(ram, 0x275BC0), set()
        while a and a not in seen:
            seen.add(a)
            if u32(ram, a + 0x4C) == DRAW and u32(ram, a + 0x44) in bank:
                items.append((beat, a))
            a = u32(ram, a + 0x1C)
    kinds = parallel_map(od.flag_class, items)
    clip = [it for it, k in zip(items, kinds) if k == 'clip']
    units = parallel_map(owner_unit, clip)
    got = sorted((b, u32(CAP[b], o + 0x10)) for b, o, *_ in units)
    assert got == sorted(CLIP_UNITS), ('captured clip units', got)
    return units, len(items)


def owner_unit(item):
    beat, owner = item
    ram = CAP[beat]
    o, ctx = od.original_draw(ram, od.CAP[beat][1], owner)
    used = o.load(ctx + 0x10) - od.CAP_DL
    unit = o.read(od.CAP_DL, used)
    hits = od.captured_unit(ram, unit)
    assert len(hits) == 1, ('clip unit not found once in the captured list', beat, hex(owner), hits)
    return beat, owner, hits[0], hits[0] + used


def scanned_units(names):
    """B: every intact clip unit in the captured memory: a CALL 0x2354A0 tag,
    the unit head (colour CNT) found by walking forward to it, the tail (the
    optional REF 2 and the model REF). Returns [(capture, start, end)]."""
    out = []
    pat = struct.pack('<I', CLIP_KERNEL)
    for name in names:
        ram, s = CAP[name], 0
        while True:
            i = ram.find(pat, s)
            if i < 0: break
            s = i + 1
            x = i - 4
            if x % 16 or (ram[x + 3] >> 4) & 7 != 5 or u32(ram, x) & 0xFFFF: continue
            start = unit_head(ram, x)
            if start is None: continue
            end = x + 16
            if (ram[end + 3] >> 4) & 7 == 3 and u32(ram, end + 4) == 0x2514B0: end += 16
            if (ram[end + 3] >> 4) & 7 != 3: continue
            out.append((name, start, end + 16))
    return out


def unit_head(ram, x):
    for s in range(x - 16, max(0, x - 0x4000), -16):
        w0 = u32(ram, s)
        if (w0 >> 28) & 7 != 1 or w0 & 0xFFFF != 5 or u32(ram, s + 28) != 0x6C0403F5: continue
        a = s
        for _ in range(24):
            w = u32(ram, a); tid = (w >> 28) & 7
            if tid not in (1, 3, 5): break
            if a == x: return s
            a += 16 + (16 * (w & 0xFFFF) if tid == 1 else 0)
            if a > x: break
    return None


def unit_key(ram, tags):
    h = hashlib.sha256()
    for a, tid, qwc, addr in tags:
        h.update(struct.pack('<3I', tid, qwc, addr if tid != 1 else 0))
        if tid == 1: h.update(ram[a + 16:a + 16 + 16 * qwc])
        if tid == 3: h.update(hashlib.sha256(ram[addr:addr + 16 * qwc]).digest())
    return h.hexdigest()


def check_unit(item):
    """Every clip batch of one unit (A/B checks). Returns stats."""
    label, name, start, end, full = item
    ram = CAP[name]
    tags = unit_tags(ram, start, end)
    runs = replay(ram, tags)
    clip = [r for r in runs if r['kernel'] == CLIP_KERNEL]
    obj = {r['block']: r for r in runs if r['kernel'] == OBJECT_KERNEL}
    assert clip, (label, 'no clip batch')
    st = dict(units=1, batches=0, kicks=0, packets=0, packet_bytes=0, triangles=0, entries=0, aborted=0,
              image_batches=0, random_register_batches=0, rejected=0, adc_vertices=0)
    pieces = unit_pieces(ram, tags)
    image = None
    if pieces:
        nodes, n, color, record, model, blocks = pieces
        assert blocks == len(clip), (label, 'blocks', blocks, len(clip))
        image = C.create_string_buffer(16384)
        arr = lambda w: (C.c_uint32 * len(w))(*w)
        assert LIB.image(image, arr(nodes) if n else None, n, arr(color), arr(record)) == 0, label
        # the image does not depend on what the buffer held: a buffer full of
        # garbage gives the same 1024 qwords (every other qword zero)
        dirty = C.create_string_buffer(random.Random(start).randbytes(16384), 16384)
        assert LIB.image(dirty, arr(nodes) if n else None, n, arr(color), arr(record)) == 0 and \
            dirty.raw == image.raw, (label, 'image over a dirty buffer')
        st['image_units'] = 1
    rng = random.Random(start ^ 0x2354A0)
    for k, r in enumerate(clip):
        where = (label, 'batch', k, 'top', r['top'])
        assert r['ftoi'] == 0, (where, 'captured FTOI outside int32')
        nat = Native(r['before'], r['top'])
        assert nat.rc == 0 and nat.res.fault == 0, (where, 'translation fault', nat.res.fault)
        got, want = nat.kicks(), r['kicks']
        if got != want:
            first = next((i for i, (g, w) in enumerate(zip(got, want)) if g != w), min(len(got), len(want)))
            raise AssertionError((where, 'kicks differ at', first, len(got), len(want)))
        assert nat.entries() == r['entries'], (where, 'entries', nat.entries(), r['entries'])
        want_vertex = kick_vertices(want, r['entries'])
        assert nat.kick_vertices() == want_vertex, (where, 'kick vertex', nat.kick_vertices(), want_vertex)
        assert bytes(nat.buf.raw) == r['after'], (where, 'data memory after the batch')
        tris = nat.triangles()
        assert tris == py_triangles(want, r['entries']), (where, 'triangle decode')
        assert all(t[1] == 0x03B for t in tris), (where, 'PRIM other than 0x03B')
        for case, hits in r['cases'].items():
            st[case] = st.get(case, 0) + hits
        # the native image: one image across the unit's batches
        if image is not None:
            blk = ram[model + 0x820 * k:model + 0x820 * (k + 1)]
            top = LIB.batch(image, k, blk)
            assert top == r['top'], (where, 'image TOP', top)
            before_image = image.raw
            img = Native(image.raw, top)
            assert img.rc == 0 and img.kicks() == want and img.entries() == r['entries'] and \
                img.kick_vertices() == want_vertex, (where, 'native image')
            C.memmove(image, img.buf.raw, 16384)
            st['image_batches'] += 1
            if full or k < 3:
                rk, re_, _, rafter, _, _, _ = run_program(before_image, top, rng)
                assert rk == want and re_ == r['entries'] and rafter == img.buf.raw, \
                    (where, 'interpreter on the image with random registers')
                st['random_register_batches'] += 1
        # relation to the object kernel's pass over the same block
        o = obj.get(r['block'])
        assert o is not None and len(o['kicks']) == 1, (where, 'object batch')
        raw = o['kicks'][0][1]
        lo, hi = struct.unpack_from('<QQ', raw, 0)
        nreg = (lo >> 60) or 16
        regs = [(hi >> (4 * g)) & 15 for g in range(nreg)]
        assert lo & 0x7FFF == 32 and regs.index(4) >= 0, (where, 'object packet')
        adc = [(struct.unpack_from('<I', raw, 16 + 16 * (nreg * i + regs.index(4)) + 12)[0] >> 15) & 1
               for i in range(32)]
        flags = {i: (cf, word) for i, cf, word in r['flags']}
        rejected = [i for i, (cf, word) in flags.items() if not word & 0x8000 and cf & 0x3FFFF and
                    any(all(cf >> (6 * v + b) & 1 for v in range(3)) for b in range(6))]
        expect = [i for i in range(32) if adc[i] and not flags[i][1] & 0x8000 and i not in rejected]
        assert r['entries'] == expect, (where, 'entries vs the object pass', r['entries'], expect)
        assert all(adc[i] == int(bool(flags[i][1] & 0x8000 or flags[i][0] & 0x3FFFF)) for i in range(32)), \
            (where, 'object ADC is not data flag or guard history')
        st['batches'] += 1
        st['kicks'] += len(want)
        st['packets'] += sum(1 for a, _ in want if a == 696)
        st['packet_bytes'] += sum(len(b) for _, b in want)
        st['triangles'] += len(tris)
        st['entries'] += len(r['entries'])
        st['aborted'] += len(r['entries']) - sum(1 for a, _ in want if a == 696)
        st['rejected'] += len(rejected)
        st['fog_above_255'] = st.get('fog_above_255', 0) + r['fog_above']
        st['adc_vertices'] += sum(adc)
    return label, st, len(clip), clip[0]['top'], sorted({r['top'] for r in clip})


# ------------------------------------------------------------ C. synthetic

STYLES = ('near', 'wide', 'huge', 'mix', 'behind', 'far', 'flags', 'slots', 'fogoff', 'ftoi', 'fogmax',
          'boundary')
# boundary style: node 0's position matrix is exact, c = (p.x, p.y, 1000, p.z),
# so w = p.z and, at w = 1 (Q = 1 exactly), the screen X, Y are p.x, p.y.
# Kinds of boundary vertex: (lane, value, the one-ulp step that puts it out)
EXACT = (1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1000.0, 0.0)
W_PLANE = struct.unpack('<f', struct.pack('<I', 0x3DCCCCCD))[0]   # the program's 0.1
BOUNDARY = {'x_hi': (0, 4088.0, 1), 'x_lo': (0, 4.0, -1), 'y_hi': (1, 4088.0, 1), 'y_lo': (1, 4.0, -1),
            'w': (2, W_PLANE, -1)}


def solve_gs(m, x, y, w):
    """p with [p, 1] x m = (x w, y w, ., w) (x, y, w columns; Cramer)."""
    a = [[m[r * 4 + c] for r in range(3)] for c in (0, 1, 3)]
    b = [x * w - m[12], y * w - m[13], w - m[15]]
    det = lambda t: t[0][0] * (t[1][1] * t[2][2] - t[1][2] * t[2][1]) - t[0][1] * (t[1][0] * t[2][2] - t[1][2] * t[2][0]) \
        + t[0][2] * (t[1][0] * t[2][1] - t[1][1] * t[2][0])
    d = det(a)
    out = []
    for j in range(3):
        t = [row[:] for row in a]
        for i in range(3): t[i][j] = b[i]
        out.append(det(t) / d)
    return out


def ulp_step(x, d):
    """The binary32 next to x in direction d (+1 / -1), x positive."""
    return struct.unpack('<f', struct.pack('<I', fbits(x) + d))[0]


def synthetic_batch(style, seed):
    """(top, dmem image, nudged image) of one synthetic batch on the base
    unit's image; the nudged image (boundary style only, else None) has every
    boundary vertex moved one ulp to the outside of its plane."""
    rng = random.Random(seed)
    nodes, n, color, record = [list(x) if isinstance(x, list) else x for x in BASE_UNIT]
    f = lambda w: struct.unpack('<f', struct.pack('<I', w))[0]
    record = list(record)
    if style == 'fogoff':
        record[16:20] = [fbits(255.0), fbits(2048.0), fbits(255.0), 0]
    if style == 'fogmax':   # A + B w above 255 for w below about 14.5: the min(F, 255) clamp
        record[18:20] = [fbits(400.0), fbits(-10.0)]
    if style == 'boundary':
        nodes = list(nodes)
        nodes[0:16] = [fbits(x) for x in EXACT]
    if style == 'ftoi':
        if seed & 1:   # z column x -1e7: z / w below -2^27 (FTOI4 of Z outside int32)
            for s in range(n):
                for r in range(4): nodes[32 * s + 4 * r + 2] = fbits(f(nodes[32 * s + 4 * r + 2]) * -1e7)
        else:          # colour rows x -1e12: RGBAQ below -2^31
            color = [fbits(f(c) * -1e12) for c in color]
    image = C.create_string_buffer(16384)
    arr = lambda w: (C.c_uint32 * len(w))(*w)
    assert LIB.image(image, arr(nodes), n, arr(color), arr(record)) == 0
    mem = bytearray(image.raw)
    nudged = bytearray(mem) if style == 'boundary' else None
    top = 0x1B0 + (0x84 if seed & 2 else 0)
    phase = rng.randrange(3) if style == 'boundary' else 0
    for i in range(32):
        at = (top + 4 * i) * 16
        if style == 'boundary':
            # every three consecutive vertices hold one boundary vertex, one
            # inside the screen and one far outside x = 4088 (so the guard
            # history is non-zero and the entry is taken); at w = 1 exactly
            # except the w kind, which sits at w = 0.1 exactly
            role = (i + phase) % 3
            nudge = None
            if role == 0:
                kind = rng.choice(sorted(BOUNDARY))
                lane, value, d = BOUNDARY[kind]
                gs = [rng.uniform(1000, 3000), rng.uniform(1000, 3000), 1.0]
                gs[lane] = value
                w = gs[2]
                p = [struct.unpack('<f', struct.pack('<f', gs[0] * w))[0],
                     struct.unpack('<f', struct.pack('<f', gs[1] * w))[0], w]
                nudge = list(p)
                nudge[lane] = ulp_step(p[lane], d)
            elif role == 1:
                p = [rng.uniform(1000, 3000), rng.uniform(1000, 3000), 1.0]
            else:
                p = [rng.uniform(20000, 40000), rng.uniform(1000, 3000), 1.0]
            word = (rng.getrandbits(16) << 16) | (0x8000 if i < 2 else 0)
            normal = [rng.uniform(-1, 1) for _ in range(3)]
            for out, q in ((mem, p), (nudged, nudge or p)):
                struct.pack_into('<2I', out, at, 0x1234 + i, 0x5678)
                struct.pack_into('<4f', out, at + 16, 0.5, 0.25, 1.0, 0.0)
                struct.pack_into('<3fI', out, at + 32, *normal, 0)
                struct.pack_into('<3fI', out, at + 48, *q, word)
            continue
        slot = rng.randrange(n) if style == 'slots' else 0
        m = [f(nodes[32 * slot + k]) for k in range(16)]
        if style == 'near':
            w, x, y = rng.uniform(-3, 3), rng.uniform(1500, 2600), rng.uniform(1500, 2600)
        elif style == 'wide':
            w, x, y = rng.uniform(0.5, 40), rng.uniform(-6000, 10000), rng.uniform(-6000, 10000)
        elif style == 'huge':
            w = rng.uniform(0.2, 30)
            x = rng.choice([-20000.0, 25000.0, 2048.0]) + rng.uniform(-3000, 3000)
            y = rng.choice([-20000.0, 25000.0, 2048.0]) + rng.uniform(-3000, 3000)
        elif style == 'far':   # fog A + B w below 0 (the max(F, 0) clamp)
            w = rng.choice([rng.uniform(100, 20000), rng.uniform(-50, 0.3)])
            x, y = rng.uniform(-3000, 7000), rng.uniform(-3000, 7000)
        elif style == 'behind':
            w = rng.choice([rng.uniform(-0.2, 0.099), rng.uniform(-0.2, 0.099), rng.uniform(0.1, 3)])
            x, y = rng.choice([-6000.0, 2048.0, 10000.0]) + rng.uniform(-500, 500), rng.uniform(1000, 3000)
        else:
            w = rng.choice([rng.uniform(-10, 0.3), rng.uniform(0.05, 60)])
            x, y = rng.uniform(-3000, 7000), rng.uniform(-3000, 7000)
        p = solve_gs(m, x, y, w)
        flag = 0x8000 if (style == 'flags' and rng.random() < 0.3) or \
            (style not in ('flags',) and i < 2) else 0
        # bits 10..14 of the data word: not a flag here (only bit 15 is), and
        # outside the 10-bit dmem address, so the node address wraps past them
        high = rng.getrandbits(5) << 10 if style == 'flags' else 0
        word = (rng.getrandbits(16) << 16) | flag | high | (8 * slot)
        normal = [rng.uniform(-1, 1) for _ in range(3)]
        struct.pack_into('<2I', mem, at, rng.getrandbits(32), rng.getrandbits(32))
        struct.pack_into('<4f', mem, at + 16, rng.uniform(-4, 4), rng.uniform(-4, 4), 1.0, 0.0)
        struct.pack_into('<3fI', mem, at + 32, *normal, 0)
        struct.pack_into('<3fI', mem, at + 48, *p, word)
    return top, bytes(mem), (bytes(nudged) if nudged is not None else None)


def branches():
    """Every conditional branch of the program: {micro pc: target}."""
    out = {}
    code = VU1(ELF)
    load_kernel(code, CLIP_KERNEL)
    for q in range(0x38E):
        lo, up = struct.unpack_from('<II', code.code, 8 * q)
        if not up >> 31 and lo >> 25 in (0x28, 0x29, 0x2C, 0x2D, 0x2E, 0x2F):
            out[q] = q + 1 + signed(lo & 0x7FF, 11)
    return out


def synthetic_worker(item):
    style, seed = item
    top, mem, nudged = synthetic_batch(style, seed)
    kicks, entries, _, after, path, ftoi, fog_above = run_program(mem, top, random.Random(seed ^ 0x51),
                                                                  watch_all=True)
    nat = Native(mem, top)
    got = nat.kicks()
    label = (style, seed)
    want_vertex = kick_vertices(kicks, entries)
    assert nat.kick_vertices() == want_vertex[:len(got)], (label, 'kick vertex')
    st = {'synthetic_batches': 1, 'synthetic_kicks': len(kicks), 'synthetic_fog_above_255': fog_above,
          f'synthetic_{style}_batches': 1,
          'synthetic_packets': sum(1 for a, _ in kicks if a == 696),
          'synthetic_triangles': sum((u32(b, 0) & 0x7FFF) for a, b in kicks if a == 696)}
    if ftoi:
        assert nat.rc == -1 and nat.res.fault == 2, (label, 'FTOI outside int32 without a translation fault')
        assert got == kicks[:len(got)] and len(got) < len(kicks), (label, 'kicks before the FTOI fault')
        st['synthetic_ftoi_faults'] = 1
    else:
        assert nat.rc == 0, (label, 'translation fault', nat.res.fault)
        if got != kicks:
            first = next((i for i, (g, w) in enumerate(zip(got, kicks)) if g != w), None)
            raise AssertionError((label, 'kicks', len(got), len(kicks), first))
        assert nat.entries() == entries and nat.buf.raw == after, (label, 'entries / data memory')
        assert nat.triangles() == py_triangles(kicks, entries), (label, 'triangle decode')
        st['synthetic_max_triangles'] = max([u32(b, 0) & 0x7FFF for a, b in kicks if a == 696] + [0])
    if style == 'fogmax':
        st['synthetic_fogmax_above_255'] = fog_above
    if nudged is not None:
        # the exact-boundary vertices decide the output: one ulp outside, the
        # original kicks something else (so the tie-break above is exercised)
        assert entries, (label, 'boundary batch without entries')
        nk = run_program(nudged, top)[0]
        st['synthetic_boundary_decisive'] = int(nk != kicks)
    return set(zip(path, path[1:])), st


def coverage(edges, report):
    br = branches()
    missing = []
    for q, target in sorted(br.items()):
        # a VU branch's delay slot (q + 1) always executes: the next address
        # after it is the target when taken, q + 2 otherwise
        if (q + 1, target) not in edges: missing.append(('taken', q))
        if (q + 1, q + 2) not in edges: missing.append(('not taken', q))
    want = sorted(UNREACHED.items())
    assert sorted((k, q) for k, q in missing) == sorted((v, q) for q, v in want), \
        ('branch outcomes not reached', [(k, hex(q)) for k, q in missing])
    report['coverage'] = {'branches': len(br), 'unreached': [(hex(q), v) for q, v in want]}
    return len(br)


# ------------------------------------------------------------ main

def merge(dst, src):
    for k, v in src.items():
        if k.endswith('max_triangles'): dst[k] = max(dst.get(k, 0), v)
        else: dst[k] = dst.get(k, 0) + v


def main():
    global ELF, LIB, BASE_UNIT
    started = time.time()
    ELF = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(ELF).hexdigest() == ELF_SHA256, 'not the pinned SCUS-97112 ELF'
    od.ELF = ELF
    LIB = build_library()
    instructions = check_packets()
    for beat in od.BEATS:
        p = ROUTE / beat
        assert (p / 'eeMemory.bin').exists(), ('missing route capture', beat)
        ram = (p / 'eeMemory.bin').read_bytes()
        CAP[beat] = ram
        od.CAP[beat] = (ram, (p / 'scratchpad.bin').read_bytes())
    extra = []
    for p in sorted(STARTUP.rglob('eeMemory.bin')):
        name = str(p.parent.relative_to(STARTUP))
        CAP[name] = p.read_bytes()
        extra.append(name)
    for p in sorted(ROUTE.glob('15_*/eeMemory.bin')):
        CAP[p.parent.name] = p.read_bytes()
        extra.append(p.parent.name)
    report, stats = {}, {}

    # A. the 14 owner units
    owners, classified = owner_units()
    a_items = [(f'{b} {hex(u32(CAP[b], o + 0x10))}', b, s, e, True) for b, o, s, e in owners]
    a_keys = {unit_key(CAP[b], unit_tags(CAP[b], s, e)) for _, b, s, e, _ in a_items}
    # B. every other intact clip unit, deduplicated by content
    found, b_items, seen = scanned_units(list(od.BEATS) + extra), [], set(a_keys)
    for name, s, e in found:
        k = unit_key(CAP[name], unit_tags(CAP[name], s, e))
        if k in seen: continue
        seen.add(k)
        b_items.append((f'{name} {hex(s)}', name, s, e, FULL))
    b_run = select(b_items, 3, 0xB0, axes=(lambda i: u32(CAP[i[1]], i[2] + 96) & 0xFFFF,))

    # C. synthetic, on the husk-partner unit (2 nodes)
    husk = next(i for i in a_items if i[0].endswith('0x827490'))
    BASE_UNIT = unit_pieces(CAP[husk[1]], unit_tags(CAP[husk[1]], husk[2], husk[3]))[:4]
    assert BASE_UNIT[1] == 2
    synth = [(s, 1000 * k + j) for k, s in enumerate(STYLES) for j in range(pick(48, 6))]

    results = parallel_map(dispatch, [('unit', i) for i in a_items + b_run] + [('synth', s) for s in synth],
                           cost=lambda it: unit_cost(it[1]) if it[0] == 'unit' else 1)
    units = results[:len(a_items) + len(b_run)]
    report['units'] = []
    for label, st, batches, _, tops in units:
        merge(stats, {('A_' if any(label == a[0] for a in a_items) else 'B_') + k: v for k, v in st.items()})
        report['units'].append({'unit': label, 'batches': batches, 'tops': [hex(t) for t in tops], **st})
    edges = set()
    for e, st in results[len(units):]:
        edges |= e
        merge(stats, st)
    stats['branches_covered'] = coverage(edges, report)
    # the styles reach what they exist for
    assert stats.get('synthetic_fogmax_above_255', 0) > 0, 'fogmax style never drives A + B w above 255'
    assert stats.get('synthetic_boundary_decisive', 0) == stats['synthetic_boundary_batches'], \
        ('boundary batches whose exact-plane vertices do not decide the output',
         stats.get('synthetic_boundary_decisive'), stats['synthetic_boundary_batches'])
    line = banner(f'{len(a_items)} captured owner clip units ({classified} owner-frames classified)',
                  part(len(b_run), len(b_items), 'other captured clip units'),
                  part(len(synth), len(STYLES) * 48, 'synthetic batches'))
    report.update(status='PASS', mode=line, elf_sha256=ELF_SHA256, program_instructions=instructions,
                  counts=stats, other_units_found=len(found), seconds=round(time.time() - started, 1))
    ubsan = sorted(p.name for p in OUT.glob('ubsan.*'))
    assert not ubsan, ('UBSan reports', ubsan)
    if not os.environ.get('EM_VU1_OBJECT_CLIP_SRC'):   # a defect-injection run keeps the real report
        (OUT / 'report.json').write_text(json.dumps(report, indent=1) + '\n')
    print(json.dumps({k: report[k] for k in ('status', 'mode', 'counts', 'seconds')}))


def unit_cost(item):
    """The unit's model qwords (it is sent twice): the dearest units start first."""
    _, name, start, end, _ = item
    return sum(qwc for _, tid, qwc, _ in unit_tags(CAP[name], start, end) if tid == 3)


def dispatch(item):
    kind, x = item
    return check_unit(x) if kind == 'unit' else synthetic_worker(x)


if __name__ == '__main__':
    sys.exit(main())
