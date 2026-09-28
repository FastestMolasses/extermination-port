#!/usr/bin/env python3
"""test_static_world_draw_reference.py - the static world's channel-0 run
drawn from its original packets (docs/STATIC_WORLD.md section 7):
src/game/em_static_world_draw.c (the DMA / VIF1 / GIF / GS walk) with
src/game/em_vu1_level_kernel.h (the level kernel 0x00237180) and
src/game/em_vu1_shadow_clip.h (the guard-band clip kernel 0x00239C90),
against the ORIGINAL VU1 microcode run by the reference interpreter
(tools/test_shadow_original_reference.py VU1 / kernel_replay).

A. The ELF: the two kernel packets' VIF codes (NOP, NOP, FLUSHA, STCYCL 4,4,
   STMASK 0, STMOD 0, BASE 0x190, OFFSET 0x109 / 0x101, the MPG blocks, then
   a RET), which the walker applies without transferring the microcode; the
   background kernel packet 0x0023C990 ends the channel-3 list with STCYCL
   4,4, the cycle channel 0's first UNPACK inherits.
B. The captures (default: the opening and 14_roger_encounter; EM_TEST_FULL=1:
   the opening, route beats 00..14 and the c7cap capture): the captured
   frame's static run (from 001D4750's first packet to the first tag that is
   not one of the run's forms) replayed on one persistent VU1 with the
   original microcode, and walked by the native module. Every XGKICK must be
   equal (the program, the TOP, every byte of the packet up to EOP), and the
   triangles the GS draws from them (decoded here independently: the PACKED
   registers and the vertex queue of PRIM types 3 and 4 under ADC) must be
   equal, with their TEX0 and GS state, in order.
C. Synthetic level batches: random matrices, positions (behind the camera,
   outside the guard band, at the plane), data words (both windings, bit 15
   set or clear), colours and fog rows, run in pairs on one persistent
   interpreter and the native state (the carried registers of vertices 0 and
   1), compared over the whole 1024-qword data memory and the kick. Every
   ADC reason, both fog clamps, the ADC fog step and a -0 cull product are
   reached. A batch whose vertex 0 needs registers another program left
   faults (STALE); an exponent-255 position faults (OPERAND). Every batch
   also runs on the host lanes the live walk uses
   (em_vu1_level_kernel_batch_host, src/game/em_vu_host_lanes.h) and must
   equal the model's batch word for word (fault 0xD1FF otherwise), faults
   included; parts B and D walk with the host lanes.
D. Fail-stop: an unknown DMA tag, a CALL of another program, DIRECT outside
   the GS state REF, a GS state REF of other bytes, an unmapped address and a
   non-contiguous inherited cycle each fault with their code.

Runs natively on arm64 macOS; the shim is built into build/static_world_draw/.
"""
import ctypes as C
import os
import random
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode as RM  # noqa: E402
import test_shadow_original_reference as sh  # noqa: E402
from test_player_slide_reference import read_elf, REFERENCE, DECOMP  # noqa: E402

OUT = ROOT / 'build' / 'static_world_draw'
RAM_SIZE = 0x2000000
CTX_PTR = 0x275670
LEVEL, CLIP, BACKGROUND = 0x237180, 0x239C90, 0x23C990
GS_STATE, ARENA_Q = 0x815360, 0x814220
SKIN, SKIN_COUNT = 0x816440, 14
BANK_WORD = 0x28A5A0
FAULTS = dict(ok=0, args=1, read=2, dma=3, vif=4, program=5, vu=6, gif=7, gs_state=8, capacity=9)

SOURCES = ['src/game/em_static_world_draw.c', 'src/game/em_object_unit.c']
SHIM = r'''
#include <stdlib.h>
#include <string.h>
#include "game/em_static_world_draw.h"

static const unsigned char *g_ram;
static unsigned g_size;
static const unsigned char *rd(void *ctx, unsigned a, unsigned n)
{
    (void)ctx;
    if (a >= g_size || n > g_size - a) return NULL;
    return g_ram + a;
}

static unsigned *g_kick;      /* per kick: program, top, first word, count */
static unsigned *g_words;
static unsigned g_nkick, g_nword, g_kcap, g_wcap;
static void kick(void *ctx, unsigned program, unsigned top, const unsigned *qw, unsigned n)
{
    (void)ctx;
    if (g_nkick + 1 > g_kcap) { g_kcap = g_kcap ? 2 * g_kcap : 256; g_kick = realloc(g_kick, 16 * g_kcap); }
    if (g_nword + 4 * n > g_wcap) {
        while (g_nword + 4 * n > g_wcap) g_wcap = g_wcap ? 2 * g_wcap : 65536;
        g_words = realloc(g_words, 4 * g_wcap);
    }
    unsigned *k = g_kick + 4 * g_nkick++;
    k[0] = program; k[1] = top; k[2] = g_nword; k[3] = n;
    memcpy(g_words + g_nword, qw, 16 * n);
    g_nword += 4 * n;
}

static EmStaticWorldDraw D;

void swd_reset(void) { em_static_world_draw_free(&D); memset(&D, 0, sizeof D); }

/* The run [start, end) over the RAM image; prims out as 28 words each. */
int swd_run(const unsigned char *ram, unsigned size, unsigned start, unsigned end, unsigned cl,
            unsigned wl, unsigned *fault, unsigned *counts)
{
    g_ram = ram; g_size = size;
    g_nkick = g_nword = 0;
    D.read = rd; D.kick = kick; D.cl = (unsigned char)cl; D.wl = (unsigned char)wl;
    int rc = em_static_world_draw_run(&D, start, end);
    fault[0] = D.fault; fault[1] = D.fault_address; fault[2] = D.fault_detail;
    counts[0] = D.prim_count; counts[1] = g_nkick;
    counts[2] = D.counts.batches[0]; counts[3] = D.counts.batches[1];
    counts[4] = D.counts.triangles[0]; counts[5] = D.counts.triangles[1];
    counts[6] = D.counts.culled; counts[7] = D.counts.gs_states;
    counts[8] = D.counts.calls[0]; counts[9] = D.counts.calls[1];
    return rc;
}

void swd_prims(unsigned *out)
{
    for (unsigned i = 0; i < D.prim_count; ++i) {
        const EmGfxGsPrim *p = &D.prims[i];
        unsigned *o = out + 28 * i;
        o[0] = p->prim; o[1] = (unsigned)p->tex0; o[2] = (unsigned)(p->tex0 >> 32); o[3] = p->set;
        for (unsigned c = 0; c < 3; ++c) {
            const EmGfxGsVertex *v = &p->v[c];
            unsigned *w = o + 4 + 8 * c;
            w[0] = v->x; w[1] = v->y; w[2] = v->z; w[3] = v->f;
            w[4] = (unsigned)v->rgba[0] | (unsigned)v->rgba[1] << 8 | (unsigned)v->rgba[2] << 16 |
                   (unsigned)v->rgba[3] << 24;
            w[5] = v->s; w[6] = v->t; w[7] = v->q;
        }
    }
}

void swd_state(unsigned long long *out)
{
    const EmStaticWorldDraw *d = &D;
    (void)d;
    out[0] = D.prim_count ? D.prims[0].test : 0;
    out[1] = D.prim_count ? D.prims[0].alpha : 0;
    out[2] = D.prim_count ? D.prims[0].tex1 : 0;
    out[3] = D.prim_count ? D.prims[0].clamp : 0;
    out[4] = D.prim_count ? D.prims[0].colclamp : 0;
}

void swd_kicks(unsigned *kicks, unsigned *words)
{
    memcpy(kicks, g_kick, 16 * g_nkick);
    memcpy(words, g_words, 4 * g_nword);
}
unsigned swd_kick_words(void) { return g_nword; }

/* C: one level-kernel batch on a persistent state, run twice: on the
 * model's arithmetic (the result the test compares) and on the host lanes
 * the live walk uses (em_vu1_level_kernel_batch_host) from its own copy of
 * the state and data memory. The two must agree in every data-memory word,
 * the batch record and the carried state; otherwise fault[0] = 0xD1FF. */
static EmVu1LvlState L, LH;
static unsigned char HM[16384];
void lvl_reset(void) { em_vu1_level_kernel_reset(&L); em_vu1_level_kernel_reset(&LH); }
void lvl_forget(void) { em_vu1_level_kernel_forget(&L); em_vu1_level_kernel_forget(&LH); }
int lvl_batch(unsigned char *dmem, unsigned top, unsigned *why, unsigned *fault)
{
    EmVu1LvlBatch hb;
    memcpy(HM, dmem, sizeof HM);
    const int rh = em_vu1_level_kernel_batch_host(&LH, (EmVu1ObjQword *)(void *)HM, top, &hb);
    int rc = em_vu1_level_kernel_batch(&L, (EmVu1ObjQword *)(void *)dmem, top, (EmVu1LvlBatch *)(void *)why);
    const EmVu1LvlBatch *b = (const EmVu1LvlBatch *)(void *)why;
    fault[0] = b->fault; fault[1] = b->fault_vertex;
    if (rh != rc || memcmp(HM, dmem, sizeof HM) || memcmp(&hb, b, sizeof hb) || memcmp(&LH, &L, sizeof L)) {
        fault[0] = 0xD1FFu;
        return -1;
    }
    return rc;
}
unsigned lvl_batch_size(void) { return sizeof(EmVu1LvlBatch); }
'''


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    source = OUT / 'swd_shim.c'
    if not source.exists() or source.read_text() != SHIM:
        source.write_text(SHIM)
    lib = OUT / ('swd.dylib' if sys.platform == 'darwin' else 'swd.so')
    deps = [ROOT / s for s in SOURCES] + [ROOT / h for h in (
        'src/game/em_static_world_draw.h', 'src/game/em_vu1_level_kernel.h', 'src/game/em_vu1_object_kernel.h',
        'src/game/em_vu1_shadow_clip.h', 'src/game/em_object_unit.h', 'src/game/em_ee_float.h', 'src/em_gfx.h',
        'src/game/em_vu_host_lanes.h')]
    if not lib.exists() or lib.stat().st_mtime < max(p.stat().st_mtime for p in deps + [source]):
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                        '-shared', '-fPIC', '-Isrc', str(source), *SOURCES, '-o', str(lib)],
                       cwd=ROOT, check=True)
    n = C.CDLL(str(lib))
    P = C.POINTER
    n.swd_run.argtypes = [C.c_void_p, C.c_uint32, C.c_uint32, C.c_uint32, C.c_uint32, C.c_uint32,
                          P(C.c_uint32), P(C.c_uint32)]
    n.swd_run.restype = C.c_int
    n.swd_prims.argtypes = [P(C.c_uint32)]
    n.swd_kicks.argtypes = [P(C.c_uint32), P(C.c_uint32)]
    n.swd_kick_words.restype = C.c_uint32
    n.swd_state.argtypes = [P(C.c_uint64)]
    n.lvl_batch.argtypes = [C.c_void_p, C.c_uint32, P(C.c_uint32), P(C.c_uint32)]
    n.lvl_batch.restype = C.c_int
    n.lvl_batch_size.restype = C.c_uint32
    return n


def u32(b, a): return struct.unpack_from('<I', b, a)[0]


# ======================================================================
# A. The ELF packets
# ======================================================================

def vif_codes(elf, packet):
    w0 = u32(elf, packet - 0x100000 + 0x300)
    assert (w0 >> 28) & 7 == 1, ('kernel packet tag', hex(packet))
    i, e, codes = packet + 8, packet + 16 + 16 * (w0 & 0xFFFF), []
    while i < e:
        v = u32(elf, i - 0x100000 + 0x300)
        cmd, num, imm = (v >> 24) & 0x7F, (v >> 16) & 0xFF, v & 0xFFFF
        if cmd == 0x4A:
            codes.append(('MPG', imm, num or 256))
            i += 4 + 8 * (num or 256)
            continue
        codes.append((cmd, imm))
        i += 8 if cmd == 0x20 else 4
    after = u32(elf, e - 0x100000 + 0x300)
    return codes, (after >> 28) & 7


def check_packets(elf):
    head = [(0, 0), (0, 0), (0x13, 0), (0x01, 0x404), (0x20, 0), (0x05, 0), (0x03, 0x190)]
    lvl, ret = vif_codes(elf, LEVEL)
    assert lvl[:7] == head and lvl[7] == (0x02, 0x109) and lvl[8] == ('MPG', 0, 79) and ret == 6, lvl
    clip, ret = vif_codes(elf, CLIP)
    assert clip[:7] == head and clip[7] == (0x02, 0x101) and ret == 6, clip
    assert [c for c in clip if c[0] == 'MPG'] == [('MPG', 0, 256), ('MPG', 0x100, 256), ('MPG', 0x200, 256),
                                                 ('MPG', 0x300, 256), ('MPG', 0x400, 159)], clip
    bg, ret = vif_codes(elf, BACKGROUND)
    assert (0x01, 0x404) in bg and ret == 6 and [c for c in bg if c[0] == 0x01] == [(0x01, 0x404)], bg
    return 'kernel packets 00237180 (OFFSET 0x109, 79 instructions) and 00239C90 (OFFSET 0x101, 1183) as the walker applies them; 0023C990 leaves STCYCL 4,4'


# ======================================================================
# B. The captures
# ======================================================================

CAPTURES = [('opening', REFERENCE / 'opening_ee.bin')]
ROUTE = DECOMP / 'build/s87/route'
for _beat in sorted(p.name for p in ROUTE.iterdir() if (p / 'eeMemory.bin').exists()) if ROUTE.exists() else []:
    if RM.in_scope_beat(_beat):
        CAPTURES.append((_beat, ROUTE / _beat / 'eeMemory.bin'))
for _p in sorted((DECOMP / 'build/s87/c7cap').glob('**/eeMemory.bin')):
    if (_p.parent / 'scratchpad.bin').exists():
        CAPTURES.append(('c7cap/' + _p.parent.name, _p))


def static_run(ram):
    """The captured frame's static run: from the first 001D4750 packet (CNT
    qwc 9: FLUSH, UNPACK 8 to VU 0) in the current buffer's channel 0, to
    the first tag outside the run's forms. Returns (start, end, tags)."""
    ctx = u32(ram, CTX_PTR) & (RAM_SIZE - 1)
    cur0 = u32(ram, ctx + 0x10)
    b = 0 if cur0 < 0x28F700 + 0x60800 + 0x8000 else 1
    a = 0x28F700 + b * 0x60800 + 0x8000
    while a < cur0:
        if ram[a + 3] == 0x10 and struct.unpack_from('<H', ram, a)[0] == 9 and u32(ram, a + 0x18) == 0x11000000 \
                and u32(ram, a + 0x1C) == 0x6C080000:
            break
        a += 16
    else:
        raise AssertionError('no static run')
    bank = u32(ram, BANK_WORD)
    start, tags, prev = a, [], None
    while a < cur0:
        w0, addr = struct.unpack_from('<II', ram, a)
        tid, qwc = (w0 >> 28) & 7, w0 & 0xFFFF
        kind = None
        if tid == 1 and qwc == 9 and u32(ram, a + 0x18) == 0x11000000 and u32(ram, a + 0x1C) == 0x6C080000:
            kind = 'cnt9'
        elif tid == 1 and qwc == 5 and prev == 'cnt9' and u32(ram, a + 0x1C) == 0x6C0403F5:
            kind = 'cnt5'
        elif tid == 3 and qwc == 1 and addr == ARENA_Q and prev == 'cnt5':
            kind = 'arena'
        elif tid == 5 and qwc == 0 and addr in (LEVEL, CLIP) and prev == 'arena':
            kind = 'call'
        elif tid == 3 and qwc == 9 and addr == GS_STATE:
            kind = 'gs'
        elif tid == 3 and qwc == 8 and SKIN <= addr < SKIN + 0x80 * SKIN_COUNT and (addr - SKIN) % 0x80 == 0:
            kind = 'skin'
        elif tid == 3 and qwc and qwc % 0x82 == 0 and bank <= addr and addr + 16 * qwc <= bank + 0x2D6FE0:
            kind = 'blocks'
        if kind is None:
            break
        tags.append((a, tid, qwc, addr, kind))
        prev = kind
        a += 16 * (qwc + 1) if tid == 1 else 16
    return start, a, tags


def gs_triangles(kicks):
    """The triangles the GS draws from the kicked packets (PACKED, PRE PRIM,
    registers TEX0_1 ST RGBAQ XYZF2 NOP): the vertex queue of a strip (the
    last three at a drawing kick) or a triangle list (every third)."""
    out = []
    prim, queue, tex0, st, rgba = None, [], 0, (0, 0, 0), (0, 0, 0, 0)
    for program, top, raw in kicks:
        q = 0
        while True:
            lo, hi = struct.unpack_from('<QQ', raw, 16 * q)
            nloop, eop, pre, flg = lo & 0x7FFF, lo >> 15 & 1, lo >> 46 & 1, lo >> 58 & 3
            nreg = (lo >> 60) or 16
            assert flg == 0, 'not PACKED'
            if pre:
                prim, queue = (lo >> 47) & 0x7FF, []
            q += 1
            for _ in range(nloop):
                for g in range(nreg):
                    reg = (hi >> (4 * g)) & 15
                    w = struct.unpack_from('<4I', raw, 16 * q)
                    q += 1
                    if reg == 0xF: continue
                    if reg == 0x6: tex0 = w[0] | w[1] << 32
                    elif reg == 0x2: st = (w[0], w[1], w[2])
                    elif reg == 0x1: rgba = tuple(x & 0xFF for x in w)
                    elif reg == 0x4:
                        adc = w[3] >> 15 & 1
                        v = (w[0] & 0xFFFF, w[1] & 0xFFFF, (w[2] >> 4) & 0xFFFFFF, (w[3] >> 4) & 0xFF,
                             rgba, st[0], st[1], st[2])
                        queue.append(v)
                        if prim & 7 == 4:
                            queue = queue[-3:]
                            if len(queue) == 3 and not adc:
                                out.append((prim, tex0, tuple(queue)))
                        elif prim & 7 == 3:
                            assert not adc
                            if len(queue) == 3:
                                out.append((prim, tex0, tuple(queue)))
                                queue = []
                        else:
                            raise AssertionError(('prim', prim))
                    else:
                        raise AssertionError(('register', reg))
            if eop:
                assert 16 * q == len(raw), 'packet longer than its EOP'
                break
    return out


def native_prims(native, count):
    buf = (C.c_uint32 * (28 * max(1, count)))()
    native.swd_prims(buf)
    out = []
    for i in range(count):
        o = buf[28 * i:28 * i + 28]
        verts = []
        for c in range(3):
            w = o[4 + 8 * c:12 + 8 * c]
            verts.append((w[0], w[1], w[2], w[3], (w[4] & 0xFF, w[4] >> 8 & 0xFF, w[4] >> 16 & 0xFF, w[4] >> 24),
                          w[5], w[6], w[7]))
        out.append((o[0], o[1] | o[2] << 32, tuple(verts)))
    return out


def native_kicks(native, nkicks):
    kicks = (C.c_uint32 * (4 * max(1, nkicks)))()
    words = (C.c_uint32 * max(1, native.swd_kick_words()))()
    native.swd_kicks(kicks, words)
    out = []
    for k in range(nkicks):
        program, top, first, count = kicks[4 * k:4 * k + 4]
        out.append((program, top, struct.pack(f'<{4 * count}I', *words[first:first + 4 * count])))
    return out


def run_capture(item):
    name, path = item
    ram = path.read_bytes()
    assert len(ram) == RAM_SIZE
    start, end, tags = static_run(ram)
    kinds = [t[4] for t in tags]
    assert kinds[:6] == ['cnt9', 'cnt5', 'arena', 'call', 'gs', 'skin'], (name, kinds[:6])
    # the original: the run's VIF stream on one VU1 with the ORIGINAL microcode
    units = [(a, tid, qwc, addr) for a, tid, qwc, addr, _ in tags]
    replay = sh.kernel_replay(ELF, ram, units)
    kicks = []
    for kernel, top, _before, _k, _src, events in replay:
        for e in events:
            if e[0] == 'kick':
                kicks.append((kernel, top, e[3]))
    tris = gs_triangles(kicks)
    # the native walk
    buf = (C.c_ubyte * RAM_SIZE).from_buffer_copy(ram)
    fault, counts = (C.c_uint32 * 3)(), (C.c_uint32 * 10)()
    NATIVE.swd_reset()
    rc = NATIVE.swd_run(C.addressof(buf), RAM_SIZE, start, end, 4, 4, fault, counts)
    assert rc == 0, (name, 'native fault', list(fault))
    nk = native_kicks(NATIVE, counts[1])
    assert len(nk) == len(kicks), (name, 'kick count', len(nk), len(kicks))
    # The level kernel never writes the ST register's w lane (vf02.w): every
    # level packet carries what the previous program left there. After a
    # clip batch that is the clip kernel's value, which its translation does
    # not model; the GS ignores that lane (PACKED ST takes S, T, Q). Those
    # lanes are compared until the run's first clip batch, masked after it.
    after_clip, masked = False, 0
    for i, (a, b) in enumerate(zip(nk, kicks)):
        assert a[0] == b[0] and a[1] == b[1], (name, 'kick', i, 'program / TOP', a[:2], b[:2])
        ra, rb = bytearray(a[2]), bytearray(b[2])
        if a[0] == CLIP:
            after_clip = True
        elif after_clip and len(ra) == len(rb) == 16 * 129:
            for v in range(32):
                o = 16 * (1 + 4 * v + 1) + 12
                ra[o:o + 4] = rb[o:o + 4] = b'\0\0\0\0'
            masked += 32
        assert ra == rb, (name, 'kick', i, 'packet bytes differ', hex(a[0]), hex(a[1]),
                          next(j for j in range(min(len(ra), len(rb))) if ra[j] != rb[j])
                          if len(ra) == len(rb) else ('length', len(ra), len(rb)))
    np_ = native_prims(NATIVE, counts[0])
    assert len(np_) == len(tris), (name, 'triangle count', len(np_), len(tris))
    for i, (a, b) in enumerate(zip(np_, tris)):
        assert a == b, (name, 'triangle', i, a, b)
    state = (C.c_uint64 * 5)()
    NATIVE.swd_state(state)
    assert list(state) == [0x5000D, 0x80000000A8, 0x60, 0, 1], (name, 'GS state', [hex(x) for x in state])
    level_batches = sum(1 for r in replay if r[0] == LEVEL)
    clip_batches = sum(1 for r in replay if r[0] == CLIP)
    assert counts[2] == level_batches and counts[3] == clip_batches, (name, list(counts), level_batches, clip_batches)
    objects = sum(1 for k in kinds if k == 'blocks')
    return dict(name=name, bytes=end - start, kicks=len(kicks), triangles=len(tris), level=level_batches,
                clip=clip_batches, clip_tris=counts[5], culled=counts[6], objects=objects, masked=masked,
                calls=(counts[8], counts[9]))


# ======================================================================
# C. Synthetic level batches
# ======================================================================

def fbits(x): return struct.unpack('<I', struct.pack('<f', x))[0]


def capture_rows():
    """The opening capture's skin record 0 rows dmem 1020..1023 (the
    template, the fog row and the guard rows 001D30A0 wrote)."""
    ram = (REFERENCE / 'opening_ee.bin').read_bytes()
    ctx = u32(ram, CTX_PTR) & (RAM_SIZE - 1)
    rec = SKIN + (u32(ram, ctx + 0x9C) << 7)
    return ram[rec + 0x40:rec + 0x80]


def synthetic_batch(rng, style, rows):
    """A level batch: TEX0, (s, t, 1, 0), colour / 128, position + data word,
    under a view-projection whose c.x / c.w and c.y / c.w are GS window
    coordinates (2048 at the centre), as the frame head's D_70003AC0 is."""
    import math
    mem = bytearray(16384)
    ang = rng.uniform(0, 2 * math.pi)
    ca, sa = math.cos(ang), math.sin(ang)
    f = rng.uniform(200, 600)
    dist = rng.uniform(-40, 5) if style == 'behind' else rng.uniform(40, 400)
    m = [[f * ca - 2048 * sa, -2048 * sa, -100 * sa, -sa],
         [0.0, -f, 0.0, 0.0],
         [f * sa + 2048 * ca, 2048 * ca, 100 * ca, ca],
         [2048 * dist, 2048 * dist, 100 * dist + 1000, dist]]
    for r in range(4):
        struct.pack_into('<4f', mem, 16 * r, *m[r])
    mem[16 * 1020:16 * 1024] = rows
    a_fog = rng.choice([None, rng.uniform(-300, 400)])
    if a_fog is not None:
        struct.pack_into('<4f', mem, 16 * 1021, 255.0, 2048.0, a_fog, rng.uniform(-3, 3))
    top = rng.choice((0x190, 0x299))
    for i in range(32):
        v = top + 4 * i
        struct.pack_into('<QQ', mem, 16 * v, rng.getrandbits(62), 0)
        struct.pack_into('<4f', mem, 16 * (v + 1), rng.uniform(-4, 4), rng.uniform(-4, 4), 1.0, 0.0)
        struct.pack_into('<4f', mem, 16 * (v + 2), *(rng.choice([0.0, 1.0, rng.uniform(0, 2)]) for _ in range(4)))
        spread = {'wide': 3000, 'near': 20}.get(style, 80)
        x, y, z = (rng.uniform(-spread, spread) for _ in range(3))
        if style == 'plane' and i % 5 == 0:
            y = 0.0
        if style == 'flat' and i % 3 == 0:
            x, y, z = 0.0, 0.0, 0.0            # repeated points: a zero cross product
        word = fbits(rng.choice((1.0, -1.0)))
        forced = i < 2 if style not in ('open', 'open1') else (i == 0 and style == 'open1')
        flag = 0x8000 if forced or rng.random() < 0.15 else 0
        word = (word & ~0xFFFF) | flag | (rng.getrandbits(15) if style == 'lowbits' else 0)
        struct.pack_into('<3fI', mem, 16 * (v + 3), x, y, z, word)
    return mem, top


def interp_batch(vu1, mem, top):
    vu1.mem[:] = mem
    vu1.top = top
    vu1.kicks, vu1.events = [], []
    vu1.run(0)
    kick = [e for e in vu1.events if e[0] == 'kick']
    return bytes(vu1.mem), kick


def synthetic(item):
    seed, pairs = item
    rng = random.Random(seed)
    rows = capture_rows()
    why_t = C.c_uint32 * (NATIVE.lvl_batch_size() // 4)
    seen = set()
    styles = ('near', 'wide', 'behind', 'plane', 'lowbits', 'flat', 'open')
    for p in range(pairs):
        NATIVE.lvl_reset()
        # a fresh interpreter each pair: the registers of both start zero
        vu1 = sh.VU1(ELF)
        sh.load_program(vu1, ELF, LEVEL)
        # vertex 1's cull reads the previous batch's s_31 and e_31; vertex
        # 0's also a w the batch before that stored: the third batch may
        # leave both without bit 15, the second only vertex 1
        for k in range(3):
            style = styles[(p + k) % len(styles)]
            if style == 'open' and k < 2:
                style = 'open1' if k == 1 else 'near'
            mem, top = synthetic_batch(rng, style, rows)
            before = bytes(vu1.mem) if k else bytes(16384)
            # the interpreter's memory persists across the pair, as VU1's does
            if k:
                merged = bytearray(before)
                for r in list(range(0, 4)) + list(range(1017, 1024)) + list(range(top, top + 128)):
                    merged[16 * r:16 * r + 16] = mem[16 * r:16 * r + 16]
                mem = merged
            nat = (C.c_ubyte * 16384).from_buffer_copy(mem)
            why = why_t()
            fault = (C.c_uint32 * 2)()
            rc = NATIVE.lvl_batch(C.addressof(nat), top, why, fault)
            after, kick = interp_batch(vu1, mem, top)
            assert rc == 0, ('synthetic', seed, p, k, style, 'native fault', list(fault))
            assert bytes(nat) == after, ('synthetic', seed, p, k, style, 'dmem differs at qword',
                                         next(i // 16 for i in range(16384) if bytes(nat)[i] != after[i]))
            assert len(kick) == 1 and kick[0][1] == (top + 0x84) & 1023, ('synthetic kick', kick)
            whys = list(why)[4:4 + 32]
            for i in range(32):
                seen |= {('adc', b) for b in (1, 2, 4) if whys[i] & b}
                if whys[i] == 4: seen.add(('cull only',))
                word = struct.unpack_from('<4I', after, 16 * ((top + 0x88 + 4 * i) & 1023))[3]
                seen.add(('fog', 'adc' if word & 0x8000 else 'plain'))
                f = (word >> 4) & 0xFF
                if f == 255: seen.add(('fog', 255))
                if f == 0: seen.add(('fog', 0))
            if style == 'open' and not (whys[0] & 1):
                seen.add(('carried', 'vertex 0'))
            if style in ('open', 'open1') and not (whys[1] & 1):
                seen.add(('carried', 'vertex 1'))
    return seen


def stale_and_operand():
    """A fresh state (another program ran before) with vertex 0 lacking bit
    15 and at the window centre: STALE; an exponent-255 position: OPERAND."""
    rng = random.Random(0x57A1E)
    rows = capture_rows()
    why = (C.c_uint32 * (NATIVE.lvl_batch_size() // 4))()
    fault = (C.c_uint32 * 2)()
    mem, top = synthetic_batch(rng, 'near', rows)
    struct.pack_into('<3fI', mem, 16 * (top + 3), 0.0, 0.0, 0.0, fbits(1.0))
    NATIVE.lvl_reset()
    nat = (C.c_ubyte * 16384).from_buffer_copy(mem)
    rc = NATIVE.lvl_batch(C.addressof(nat), top, why, fault)
    stale = rc < 0 and fault[0] == 3 and fault[1] == 0
    mem, top = synthetic_batch(rng, 'near', rows)
    struct.pack_into('<I', mem, 16 * (top + 4 * 5 + 3), 0x7F800000)
    NATIVE.lvl_reset()
    nat = (C.c_ubyte * 16384).from_buffer_copy(mem)
    rc2 = NATIVE.lvl_batch(C.addressof(nat), top, why, fault)
    operand = rc2 < 0 and fault[0] == 2
    assert stale, ('a fresh state with vertex 0 needing the cull must fault STALE', rc, list(fault))
    assert operand, ('an exponent-255 position must fault OPERAND', rc2, list(fault))
    # a zero cross product (every vertex at one point): the product with a
    # negative w is -0, whose sign the S flag takes: the kernel culls it
    mem, top = synthetic_batch(rng, 'near', rows)
    for i in range(32):
        word = (fbits(-1.0 if i % 2 else 1.0) & ~0xFFFF) | (0x8000 if i < 2 else 0)
        struct.pack_into('<3fI', mem, 16 * (top + 4 * i + 3), 0.0, 0.0, 0.0, word)
    NATIVE.lvl_reset()
    nat = (C.c_ubyte * 16384).from_buffer_copy(mem)
    rc3 = NATIVE.lvl_batch(C.addressof(nat), top, why, fault)
    vu1 = sh.VU1(ELF)
    sh.load_program(vu1, ELF, LEVEL)
    after, _ = interp_batch(vu1, mem, top)
    assert rc3 == 0 and bytes(nat) == after, ('zero cross product', rc3)
    whys = list(why)[4:36]
    assert all(whys[i] == (4 if i % 2 else 0) for i in range(2, 32)), ('zero cross product culls', whys)
    return ('STALE on a fresh state (vertex 0 without bit 15), OPERAND on an exponent-255 position; a zero '
            'cross product culls exactly the negative-w vertices (-0), equal to the original')


# ======================================================================
# D. Fail-stop
# ======================================================================

def failstop():
    path = next(p for n, p in CAPTURES if n == 'opening')
    ram = bytearray(path.read_bytes())
    start, end, tags = static_run(ram)
    lines = []

    def expect(label, image, s, e, code, cl=4, wl=4):
        buf = (C.c_ubyte * len(image)).from_buffer_copy(image)
        fault, counts = (C.c_uint32 * 3)(), (C.c_uint32 * 10)()
        NATIVE.swd_reset()
        rc = NATIVE.swd_run(C.addressof(buf), len(image), s, e, cl, wl, fault, counts)
        assert rc < 0 and fault[0] == FAULTS[code], (label, rc, list(fault))
        lines.append(f'{label}: {code}')

    img = bytearray(ram)
    struct.pack_into('<I', img, start, (struct.unpack_from('<I', img, start)[0] & 0x8FFFFFFF) | 7 << 28)
    expect('an END tag', img, start, end, 'dma')
    call = next(t for t in tags if t[4] == 'call')
    img = bytearray(ram)
    struct.pack_into('<I', img, call[0] + 4, 0x23C750)
    expect('a CALL of the object kernel', img, start, end, 'program')
    img = bytearray(ram)
    struct.pack_into('<I', img, start + 0x18, 0x50000001)
    expect('DIRECT in 001D4750\'s packet', img, start, end, 'vif')
    gs = next(t for t in tags if t[4] == 'gs')
    img = bytearray(ram)
    img[GS_STATE + 0x30] ^= 0x01            # a different TEX1 / TEST byte
    expect('a GS state REF of other bytes', img, start, end, 'gs_state')
    img = bytearray(ram)
    blocks = next(t for t in tags if t[4] == 'blocks')
    struct.pack_into('<I', img, blocks[0] + 4, 0x3000000)
    expect('a REF outside RAM', img, start, end, 'read')
    expect('an inherited cycle with CL != WL', ram, start, end, 'vif', cl=4, wl=2)
    return lines


# ======================================================================

ELF = NATIVE = None


def main():
    global ELF, NATIVE
    t0 = time.time()
    ELF = read_elf()
    NATIVE = build_native()
    packets = check_packets(ELF)
    names = [c[0] for c in CAPTURES]
    beats = RM.select(names, 2, 0x5D, keep=lambda i, n: n in ('opening', '14_roger_encounter'))
    items = [c for c in CAPTURES if c[0] in beats]
    pairs = RM.pick(24, 6)
    synth = [(0x51A7 + k, pairs) for k in range(RM.pick(8, 2))]
    work = [('capture', i) for i in items] + [('synthetic', s) for s in synth]
    results = RM.parallel_map(dispatch, work, cost=lambda w: 10 if w[0] == 'capture' else 1)
    caps = [r for (kind, _), r in zip(work, results) if kind == 'capture']
    seen = set()
    for (kind, _), r in zip(work, results):
        if kind == 'synthetic':
            seen |= r
    for need in [('adc', 1), ('adc', 2), ('adc', 4), ('cull only',), ('fog', 'adc'), ('fog', 'plain'),
                 ('fog', 255), ('fog', 0), ('carried', 'vertex 0'), ('carried', 'vertex 1')]:
        assert need in seen, ('synthetic batches never reached', need)
    so = stale_and_operand()
    fs = failstop()
    RM.banner(RM.part(len(items), len(CAPTURES), 'captures'), f'{len(synth) * pairs * 3} synthetic batches')
    print('A:', packets)
    for c in caps:
        print(f'B: {c["name"]}: run {c["bytes"]:#x} bytes, {c["objects"]} object REFs, {c["level"]} level + '
              f'{c["clip"]} clip batches, {c["kicks"]} kicks equal to the original microcode byte for byte '
              f'({c["masked"]} ST w lanes after a clip batch masked); '
              f'{c["triangles"]} triangles equal ({c["clip_tris"]} clipped, {c["culled"]} vertices culled)')
    print(f'C: {len(synth) * pairs * 3} batches equal over the whole data memory and the kick; reached: '
          f'{", ".join(sorted(" ".join(map(str, s)) for s in seen))}; {so}')
    for line in fs:
        print('D:', line)
    print(f'test_static_world_draw_reference: PASS ({time.time() - t0:.1f} s)')
    return 0


def dispatch(work):
    kind, item = work
    return run_capture(item) if kind == 'capture' else synthetic(item)


if __name__ == '__main__':
    sys.exit(main())
