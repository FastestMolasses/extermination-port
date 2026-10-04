#!/usr/bin/env python3
"""The chain page D_007635C0 consumer against the ORIGINAL (docs/CHAIN_PAGE.md).

Checks src/game/em_vu1_page_programs.h (the lane program of D_00233290, the
sprite program of table 0x231770, the snow program of D_00233800, the
streak program of table 0x230800 and the kind-2 program of table 0x232540,
translated from their VU1 microcode)
and src/game/em_chain_page.c (the DMA / VIF1 / GIF / GS walk 001CB800's kick
starts) against tools/chain_page_model.py, which walks the same pages and
executes the ORIGINAL microcode the page's MPG codes upload from the pinned
boot ELF (VuOracle: the shadow test's VU1 machine with the VU0 lane rules of
tools/ee_float_model.py). Reads the owner's ELF and the captured EE RAM under
../Extermination/build/; embeds no original bytes; prints counts only.

A. Captured pages. The page the last kick of every route capture 00..14
   spliced (its start tag at the render context's +0, where 001CB800
   stores its base) is walked. CALLs of
   producers the port does not run on the page (object units via 001CAAC0,
   001DDE10's four-sprite pass: every other CALL into the packet arena) are
   walked over by both sides and counted. The weather's 001E0D70 kick (the
   channel-3 list of 001CFFE0's 108 snow tiles, chain_page_model
   weather_lists) is walked with all its snow MSCALs on the selected beats
   (quick: one; full: all) and walked over on the others.
   - Every MSCAL: the translation and the oracle start from the same data
     memory and registers; all 16 KiB of data memory, every register (VF,
     VI, ACC, Q, I, R, the clip history) and every XGKICK (address and the
     kicked GIF packet's bytes) must be equal afterwards.
   - The whole page: the primitives em_chain_page hands the renderer (PRIM,
     the drawing state the page set, every vertex word and whether its Q
     was set in the page) must equal the model's, in order, with the same
     DMA, DIRECT, MSCAL, XGKICK and skip counts.
   - Independence: the model walks the page again from random VU1 data
     memory and registers and must draw the same primitives (the programs
     read only what the page uploads).
B. Synthetic lane batches (the route draws no active lane slot): random
   slot matrices placed around the capture's camera (some crossing the view
   planes: clipped and ADC vertices), colours, corner rows, countdowns
   (<= 0, 1..0x7FFF, halfword-signed), fog rows; compared as in A.
C. Synthetic sprite and snow batches: random descriptors over the captured rows
   (counts 1..90 so several 28-particle batches run, every flags value
   0..31 including bit 0x10's path, phases, fade intervals, seeds, gravity,
   blend and life rows), tile matrices moved so particles clip; compared as
   in A. Both outcomes of every conditional branch of both programs are
   reached (asserted); the snow batches start from the captured snow
   MSCALs' memory. The streak batches (no capture holds a page with the
   streak program: no AIM or route beat's end snapshot has a live
   0x80000060 effect) start from a captured sprite MSCAL's memory with the
   streak packet's own uploads applied the way the page orders them (its
   lookup and constant rows, then 001CFBE0's rows 80..93 and 110..124 over
   them), the GIF tag row of kind 0 (D_00251260 entry 0 or 1), and the same
   random descriptors and tile moves; the EFU (ERCPR / ERLENG) is the
   model chain_page_model documents, the same on both sides. The kind-2
   batches (the cable's hit effect node 0021AAC0; no capture holds one
   either) start the same way from the kind-2 packet's uploads, with the
   GIF tag row of kind 2 (D_00251260 entry 4 or 5).
D. The timing premise of the translation: in every A..C run the oracle logs
   which producer each Q, MAC-flag, clip-flag and (streak, kind 2) P read sees; it
   must be the one the header names (em_vu1_page_programs.h).
E. Faults: an exponent-255 word on a live lane faults both programs (the
   oracle raises); malformed pages (END tag, unknown MPG, an MSCAL before
   any program, ITOP, REGLIST, an A+D FRAME write, an unmapped REF, too
   little room) latch the documented fault.
F. The blend presets the page REFs (001D0F20's bank at D_00275674 + 0x6A0,
   em_gs_blocks_original): equal to every capture's bytes; in full mode
   also to the bank the ORIGINAL 001D0F20 writes when executed.

Quick mode (default): all of A on every beat but the weather's snow
MSCALs on one beat and the independence re-walk on three, B and C 40 cases
each (snow: 20). EM_TEST_FULL=1: the snow MSCALs and the independence on
all beats, B and C 600 cases each (snow: 300).
"""
from __future__ import annotations

import collections
import ctypes as C
import hashlib
import random
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import chain_page_model as M  # noqa: E402
from reference_mode import FULL, banner, in_scope_beat, part, pick, select  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
ROUTE = DECOMP / 'build/s87/route'
OUT = ROOT / 'build/chain_page_reference'
ELF_SHA = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
BEATS = sorted(p.name for p in ROUTE.iterdir() if (p / 'eeMemory.bin').exists() and in_scope_beat(p.name))

# The producers the header names for each non-interlocked read (micro
# address of the reader -> of the producer), per program.
PRODUCERS = {
    M.PROGRAM_LANE: {('Q', 0x067): 0x060, ('Q', 0x068): 0x060, ('CF', 0x069): 0x064},
    M.PROGRAM_SPRITE: {('Q', 0x00F): 0x008, ('Q', 0x0D5): 0x0CE, ('Q', 0x0D6): 0x0CE, ('Q', 0x0DF): 0x0D6,
                       ('Q', 0x11E): 0x117, ('Q', 0x125): 0x11E, ('MAC', 0x06A): 0x066,
                       ('MAC', 0x071): 0x06D, ('MAC', 0x09E): 0x09A, ('MAC', 0x0A5): 0x0A1,
                       ('CF', 0x123): 0x11F},
}
# The snow program's reads see the same producers (its extra instructions
# read no Q and write no flag a test reads before a later producer).
PRODUCERS[M.PROGRAM_SNOW] = dict(PRODUCERS[M.PROGRAM_SPRITE])
# The streak program: the sprite program's draw (micro 0x000..0x0C2), its
# own particle record and quad emission; P is the EFU's (ERCPR / ERLENG).
PRODUCERS[M.PROGRAM_STREAK] = {
    ('Q', 0x00F): 0x008, ('MAC', 0x06A): 0x066, ('MAC', 0x071): 0x06D, ('MAC', 0x09E): 0x09A,
    ('MAC', 0x0A5): 0x0A1, ('Q', 0x0D6): 0x0CF, ('Q', 0x0D7): 0x0CF, ('Q', 0x0E4): 0x0DB,
    ('Q', 0x12C): 0x125, ('Q', 0x12D): 0x125, ('Q', 0x12E): 0x125, ('Q', 0x161): 0x15A,
    ('CF', 0x12F): 0x129, ('P', 0x12D): 0x121, ('P', 0x147): 0x139, ('P', 0x162): 0x156}
# The kind-2 program: the streak program's micro 0x000..0x0F8 and its own
# line emission (Q of the tail's w, P = ERCPR of the head's w).
PRODUCERS[M.PROGRAM_KIND2] = {
    ('Q', 0x00F): 0x008, ('MAC', 0x06A): 0x066, ('MAC', 0x071): 0x06D, ('MAC', 0x09E): 0x09A,
    ('MAC', 0x0A5): 0x0A1, ('Q', 0x0D6): 0x0CF, ('Q', 0x0D7): 0x0CF, ('Q', 0x0E4): 0x0DB,
    ('Q', 0x125): 0x11E, ('CF', 0x127): 0x122, ('P', 0x126): 0x11A}
# Conditional branches (micro address) whose both outcomes must be reached.
BRANCHES = {
    M.PROGRAM_LANE: (0x017, 0x048, 0x07C),
    M.PROGRAM_SPRITE: (0x026, 0x02A, 0x03D, 0x045, 0x049, 0x059, 0x07E, 0x081, 0x084, 0x087,
                       0x0B2, 0x0B5, 0x0B8, 0x0BB, 0x0F7, 0x0F9, 0x126, 0x132, 0x145),
    M.PROGRAM_SNOW: (0x026, 0x02A, 0x03D, 0x045, 0x049, 0x059, 0x07E, 0x081, 0x084, 0x087,
                     0x0B2, 0x0B5, 0x0B8, 0x0BB, 0x0F7, 0x0F9, 0x126, 0x132, 0x147),
    M.PROGRAM_STREAK: (0x026, 0x02A, 0x03D, 0x045, 0x049, 0x059, 0x07E, 0x081, 0x084, 0x087,
                       0x0B2, 0x0B5, 0x0B8, 0x0BB, 0x0FD, 0x0FF, 0x142, 0x174),
    M.PROGRAM_KIND2: (0x026, 0x02A, 0x03D, 0x045, 0x049, 0x059, 0x07E, 0x081, 0x084, 0x087,
                      0x0B2, 0x0B5, 0x0B8, 0x0BB, 0x0FD, 0x0FF, 0x12B, 0x138),
}
PROGRAM_ID = {M.PROGRAM_LANE: 0, M.PROGRAM_SPRITE: 1, M.PROGRAM_SNOW: 2, M.PROGRAM_STREAK: 3,
              M.PROGRAM_KIND2: 4}
PROGRAM_NAME = {M.PROGRAM_LANE: 'lane', M.PROGRAM_SPRITE: 'sprite', M.PROGRAM_SNOW: 'snow',
                M.PROGRAM_STREAK: 'streak', M.PROGRAM_KIND2: 'kind2'}

SHIM = r'''
#include "game/em_chain_page.h"
#include "game/em_gs_blocks_original.h"
#include <stdlib.h>

int shim_presets(uint8_t *bank) { return em_gs_blocks_001D0F20_presets(bank); }

typedef struct { uint32_t n; uint32_t at[64]; EmVu1PQword mem[64][1024]; } Kicks;
static int kick_cb(void *ctx, const EmVu1PQword *dmem, uint32_t at)
{
    Kicks *k = ctx;
    if (k->n >= 64) return 1;
    k->at[k->n] = at;
    memcpy(k->mem[k->n], dmem, sizeof(EmVu1PQword) * 1024);
    k->n++;
    return 0;
}
int shim_mscal(int program, EmVu1PRegs *r, EmVu1PQword *dmem, Kicks *k)
{
    k->n = 0;
    return program == 4 ? em_vu1_kind2_program_mscal(r, dmem, kick_cb, k)
         : program == 3 ? em_vu1_streak_program_mscal(r, dmem, kick_cb, k)
         : program == 2 ? em_vu1_snow_program_mscal(r, dmem, kick_cb, k)
         : program == 1 ? em_vu1_sprite_program_mscal(r, dmem, kick_cb, k)
                        : em_vu1_lane_program_mscal(r, dmem, kick_cb, k);
}

typedef struct { uint32_t base, size; const uint8_t *bytes; } Region;
static Region *g_regions;
static unsigned g_count;
static const uint8_t *reader(void *ctx, uint32_t a, uint32_t n)
{
    (void)ctx;
    for (unsigned i = 0; i < g_count; ++i)
        if (a >= g_regions[i].base && (uint64_t)a + n <= (uint64_t)g_regions[i].base + g_regions[i].size)
            return g_regions[i].bytes + (a - g_regions[i].base);
    return NULL;
}
static EmChainPage g_page;
int shim_page(Region *regions, unsigned count, uint32_t start, const uint32_t *skip, uint32_t nskip,
              EmGfxGsPrim *prims, EmChainPageQ *q, uint32_t capacity, EmChainPageCounts *counts,
              uint32_t out[3])
{
    g_regions = regions;
    g_count = count;
    memset(&g_page, 0, sizeof g_page);
    g_page.read = reader;
    g_page.skip_calls = skip;
    g_page.skip_count = nskip;
    g_page.prims = prims;
    g_page.prim_q = q;
    g_page.prim_capacity = capacity;
    int rc = em_chain_page_run(&g_page, start);
    *counts = g_page.counts;
    out[0] = g_page.fault; out[1] = g_page.fault_address; out[2] = g_page.prim_count;
    return rc;
}
unsigned shim_sizes(void)
{
    return (unsigned)sizeof(EmGfxGsPrim) | (unsigned)sizeof(EmVu1PRegs) << 12 |
           (unsigned)sizeof(EmChainPageCounts) << 24;
}
'''


class Regs(C.Structure):
    _fields_ = [('vf', C.c_uint32 * 4 * 32), ('vi', C.c_uint16 * 16), ('acc', C.c_uint32 * 4),
                ('q', C.c_uint32), ('i', C.c_uint32), ('r', C.c_uint32), ('cf', C.c_uint32),
                ('p', C.c_uint32)]


class Kicks(C.Structure):
    _fields_ = [('n', C.c_uint32), ('at', C.c_uint32 * 64), ('mem', C.c_uint8 * (64 * 16384))]


class Region(C.Structure):
    _fields_ = [('base', C.c_uint32), ('size', C.c_uint32), ('bytes', C.c_char_p)]


class GsVertex(C.Structure):
    _fields_ = [('x', C.c_uint16), ('y', C.c_uint16), ('z', C.c_uint32), ('f', C.c_uint8),
                ('has_f', C.c_uint8), ('rgba', C.c_uint8 * 4), ('q', C.c_uint32), ('s', C.c_uint32),
                ('t', C.c_uint32), ('u', C.c_uint16), ('v', C.c_uint16)]


class GsPrim(C.Structure):
    _fields_ = [('prim', C.c_uint32), ('set', C.c_uint32), ('tex0', C.c_uint64), ('clamp', C.c_uint64),
                ('tex1', C.c_uint64), ('alpha', C.c_uint64), ('test', C.c_uint64), ('colclamp', C.c_uint64),
                ('count', C.c_uint32), ('v', GsVertex * 3)]


class PrimQ(C.Structure):
    _fields_ = [('q_known', C.c_uint8 * 3)]


class Counts(C.Structure):
    _fields_ = [('transfers', C.c_uint32), ('qwords', C.c_uint32), ('direct', C.c_uint32),
                ('mscal_lane', C.c_uint32), ('mscal_sprite', C.c_uint32), ('kicks', C.c_uint32),
                ('prims', C.c_uint32), ('prim_type', C.c_uint32 * 8), ('skipped', C.c_uint32),
                ('stale_q', C.c_uint32), ('cycle_inherited', C.c_uint32), ('mscal_snow', C.c_uint32),
                ('units', C.c_uint32), ('mscal_streak', C.c_uint32), ('streak_prims', C.c_uint32),
                ('mscal_kind2', C.c_uint32), ('kind2_prims', C.c_uint32), ('lane_strips', C.c_uint32),
                ('direct_strips', C.c_uint32), ('mscal_grid', C.c_uint32),
                ('mscal_dynamic', C.c_uint32), ('mscal_dynamic_clip', C.c_uint32)]


DMEM = C.c_uint8 * 16384
STATE_BITS = (('TEX0_1', 0x01, 'tex0'), ('CLAMP_1', 0x02, 'clamp'), ('TEX1_1', 0x04, 'tex1'),
              ('ALPHA_1', 0x08, 'alpha'), ('TEST_1', 0x10, 'test'), ('COLCLAMP', 0x20, 'colclamp'))


def fail(msg):
    raise AssertionError(msg)


def build_lib():
    OUT.mkdir(parents=True, exist_ok=True)
    src, lib_path = OUT / 'shim.c', OUT / 'chain_page.dylib'
    src.write_text(SHIM)
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared',
                    '-fPIC', '-I' + str(ROOT / 'src'), str(src), str(ROOT / 'src/game/em_chain_page.c'),
                    str(ROOT / 'src/game/em_gs_blocks_original.c'),
                    str(ROOT / 'src/game/em_load_veil_particles.c'), '-o', str(lib_path)], check=True)
    lib = C.CDLL(str(lib_path))
    P = C.POINTER
    lib.shim_mscal.argtypes = [C.c_int, P(Regs), DMEM, P(Kicks)]
    lib.shim_page.argtypes = [P(Region), C.c_uint, C.c_uint32, P(C.c_uint32), C.c_uint32, P(GsPrim), P(PrimQ),
                              C.c_uint32, P(Counts), P(C.c_uint32)]
    lib.shim_sizes.restype = C.c_uint
    lib.shim_presets.argtypes = [C.c_char_p]
    s = lib.shim_sizes()
    if (s & 0xFFF, s >> 12 & 0xFFF, s >> 24) != (C.sizeof(GsPrim), C.sizeof(Regs), C.sizeof(Counts)):
        fail('ctypes layout differs from the headers')
    return lib


# ------------------------------------------------------- the VU1 oracle ---
class Oracle(M.VuOracle):
    """VuOracle that logs, per read of Q / the MAC flags / the clip flags,
    the micro address of the producer it sees, and the outcomes of the
    conditional branches."""

    def __init__(self, elf=None):
        super().__init__(elf)
        self.seen = collections.Counter()
        self.branch = collections.Counter()
        self.cur = 0
        self.q_src = self.mac_src = self.cf_src = self.p_src = -1

    def later(self, delay, kind, value):
        self.seq += 1
        self.pending.append((self.cycle + delay, self.seq, kind, (value, self.cur)))

    def settle(self):
        due = sorted(p for p in self.pending if p[0] <= self.cycle)
        for p in due:
            _, _, kind, (value, src) = p
            if kind == 'cf':
                self.cf, self.cf_src = ((self.cf << 6) | value) & 0xFFFFFF, src
            elif kind == 'mac':
                self.mac, self.mac_src = value, src
            elif kind == 'q':
                self.q, self.q_src = value, src
            elif kind == 'p':
                self.p, self.p_src = value, src
            self.pending.remove(p)

    def upper(self, pc, up):
        self.cur = pc
        op = up & 63
        if (up & 0x3C) != 0x3C and op in (0x1C, 0x20):
            self.seen[('Q', pc // 8, self.q_src // 8)] += 1
        return super().upper(pc, up)

    def lower(self, pc, lo):
        self.cur = pc
        op = lo >> 25
        if op == 0x1A:
            self.seen[('MAC', pc // 8, self.mac_src // 8)] += 1
        if op == 0x12:
            self.seen[('CF', pc // 8, self.cf_src // 8)] += 1
        if op == 0x40 and lo & 0x7FF == 0x67C:
            self.seen[('P', pc // 8, self.p_src // 8)] += 1
        nxt = super().lower(pc, lo)
        if op in (0x28, 0x29, 0x2C, 0x2D, 0x2E, 0x2F):
            self.branch[(pc // 8, nxt is not None)] += 1
        return nxt


def regs_of(vu):
    r = Regs()
    for i in range(32):
        for c in range(4):
            r.vf[i][c] = vu.v[i][c]
    for i in range(16):
        r.vi[i] = vu.vi[i] & 0xFFFF
    for c in range(4):
        r.acc[c] = vu.accw[c]
    r.q, r.i, r.r, r.cf, r.p = vu.q, vu.iw, vu.r, vu.cf, vu.p
    return r


def same_regs(vu, r, where):
    for i in range(32):
        if list(vu.v[i]) != list(r.vf[i]):
            fail(f'{where}: VF{i} {[hex(x) for x in vu.v[i]]} native {[hex(x) for x in r.vf[i]]}')
    for i in range(16):
        if vu.vi[i] & 0xFFFF != r.vi[i]:
            fail(f'{where}: VI{i} {vu.vi[i]:#x} native {r.vi[i]:#x}')
    for c in range(4):
        if vu.accw[c] != r.acc[c]:
            fail(f'{where}: ACC lane {c} {vu.accw[c]:#x} native {r.acc[c]:#x}')
    for name, a, b in (('Q', vu.q, r.q), ('I', vu.iw, r.i), ('R', vu.r, r.r), ('clip', vu.cf, r.cf),
                       ('P', vu.p, r.p)):
        if a != b:
            fail(f'{where}: {name} {a:#x} native {b:#x}')


class Stats(collections.Counter):
    pass


def compare_mscal(lib, vu, program, where, stats, run=None):
    """Run the oracle (run()) and the translation from vu's current state;
    compare everything afterwards."""
    r, dm, k = regs_of(vu), DMEM.from_buffer_copy(vu.mem), Kicks()
    vu.kicks, vu.events, vu.watch = [], [], set()
    vu.pending, vu.cycle, vu.q_ready, vu.p_ready = [], 0, 0, 0
    vu.ready = [[0] * 4 for _ in range(32)]
    oracle_error = None
    try:
        (run or (lambda: vu.run(0)))()
    except M.ModelError as e:
        oracle_error = e
    rc = lib.shim_mscal(PROGRAM_ID[program], C.byref(r), dm, C.byref(k))
    if oracle_error is not None:
        if rc != 2:
            fail(f'{where}: the oracle refused ({oracle_error}), the translation returned {rc}')
        stats['operand_faults'] += 1
        return False
    if rc:
        fail(f'{where}: the translation faulted ({rc}), the oracle ran')
    same_regs(vu, r, where)
    if bytes(dm) != bytes(vu.mem):
        diff = [q for q in range(1024) if bytes(dm)[16 * q:16 * q + 16] != bytes(vu.mem[16 * q:16 * q + 16])]
        fail(f'{where}: data memory differs at qwords {[hex(q) for q in diff[:8]]}')
    kicks = [e for e in vu.events if e[0] == 'kick']
    if k.n != len(kicks):
        fail(f'{where}: {k.n} XGKICKs, the original {len(kicks)}')
    raw = bytes(k.mem)
    for j, e in enumerate(kicks):
        if k.at[j] != e[1] or M.sh.gif_raw(raw[16384 * j:16384 * (j + 1)], e[1]) != e[3]:
            fail(f'{where}: XGKICK {j} differs')
        stats['kicked_packet_bytes'] += len(e[3])
    stats['mscal_' + PROGRAM_NAME[program]] += 1
    stats['kicks'] += len(kicks)
    return True


def check_timing(vu, program, where):
    want = PRODUCERS[program]
    for (kind, reader, producer), n in vu.seen.items():
        if want.get((kind, reader)) != producer:
            fail(f'{where}: the {kind} read at micro {reader:#05x} sees micro {producer:#05x} '
                 f'(the header assumes {want.get((kind, reader), "none")})')


# -------------------------------------------------------- A. captured pages
class ComparedPage(M.Page):
    def __init__(self, read, lib, stats, where, skip):
        super().__init__(read, skip_calls=skip)
        self.vu = Oracle()
        self.lib, self.stats, self.where = lib, stats, where
        self.branches = collections.Counter()
        self.mscals = collections.Counter()
        self.seeds = []

    def mscal(self, imm, at):
        if self.program is None or imm != 0:
            M.fail(f'MSCAL {imm:#x} at {at:#x}')
        vu = self.vu
        self.mscals[self.program] += 1
        top = self.tops
        self.dbf ^= 1
        self.tops = self.base + (self.offset if self.dbf else 0)
        vu.seen.clear()
        vu.branch.clear()
        self.seeds.append((self.program, bytes(vu.mem)))
        compare_mscal(self.lib, vu, self.program, f'{self.where} MSCAL at {at:#x}', self.stats)
        check_timing(vu, self.program, self.where)
        for key, n in vu.branch.items():
            self.branches[(self.program,) + key] += n
        vu.top = top
        for e in vu.events:
            if e[0] == 'kick':
                self.kicks.append((self.program, e[1], e[3]))
                M.gs_feed(self.gs, e[3], f'XGKICK {e[1]:#x}')


class RandomStartPage(M.Page):
    """The model walked from random VU1 data memory and registers."""

    def __init__(self, read, skip, seed):
        super().__init__(read, skip_calls=skip)
        rng = random.Random(seed)
        vu = self.vu
        vu.mem[:] = bytes(rng.getrandbits(8) for _ in range(16384))
        for i in range(1, 32):
            vu.v[i] = [finite(rng) for _ in range(4)]
        vu.vi = [0] + [rng.getrandbits(16) for _ in range(15)]
        vu.accw = [finite(rng) for _ in range(4)]
        vu.q, vu.iw, vu.r, vu.cf = finite(rng), finite(rng), rng.getrandbits(23), rng.getrandbits(24)
        vu.p = finite(rng)


def finite(rng):
    while True:
        w = rng.getrandbits(32)
        if w & 0x7F800000 != 0x7F800000:
            return w


def page_skip(ram, start, weather):
    """The CALLs walked over: every top-level CALL into the packet arena of
    a producer the port does not run on the page, and the weather's kick
    unless `weather` (chain_page_model.arena_skips)."""
    return M.arena_skips(M.ram_reader(ram), start, weather)


def native_page(lib, regions, start, skip, capacity=4096):
    keep = [(base, bytes(data)) for base, data in regions]
    arr = (Region * len(keep))(*[Region(b, len(d), d) for b, d in keep])
    sk = (C.c_uint32 * max(1, len(skip)))(*skip)
    prims, q = (GsPrim * max(1, capacity))(), (PrimQ * max(1, capacity))()
    counts, out = Counts(), (C.c_uint32 * 3)()
    rc = lib.shim_page(arr, len(keep), start, sk, len(skip), prims, q, capacity, C.byref(counts), out)
    return rc, prims, q, counts, (out[0], out[1], out[2])


def native_tuple(p, q):
    state = tuple((getattr(p, field) if p.set & bit else None) for _name, bit, field in STATE_BITS)
    verts = []
    for k in range(p.count):
        v = p.v[k]
        verts.append((v.x, v.y, v.z, v.f if v.has_f else None, tuple(v.rgba), v.q, v.s, v.t, v.u, v.v,
                      q.q_known[k]))
    return (p.prim, state, tuple(verts))


def compare_prims(model_prims, prims, q, n, where):
    if n != len(model_prims):
        fail(f'{where}: {n} primitives, the original {len(model_prims)}')
    for i in range(n):
        a, b = model_prims[i], native_tuple(prims[i], q[i])
        if a != b:
            fail(f'{where}: primitive {i} differs:\n  original {a}\n  native   {b}')


def part_a(lib, stats, elf, seeds):
    pages = []
    indep = set(select(BEATS, 3, 0xC7A, keep=lambda i, b: i == 0))
    snowy = set(select(BEATS, 1, 0x5A0, keep=lambda i, b: b.startswith('10_')))
    branches = collections.Counter()
    for beat in BEATS:
        ram = (ROUTE / beat / 'eeMemory.bin').read_bytes()
        start = M.latest_start(ram)
        weather = beat in snowy
        skip = page_skip(ram, start, weather)
        if weather and len(set(page_skip(ram, start, False)) - set(skip)) != 1:
            fail(f'{beat}: the captured page does not hold exactly one weather kick')
        pg = ComparedPage(M.ram_reader(ram), lib, stats, beat, skip)
        pg.run(start)
        branches.update(pg.branches)
        for program, mem in pg.seeds:
            seeds.setdefault(program, []).append((mem,))
        rc, prims, q, counts, out = native_page(lib, [(0, ram)], start, skip)
        if rc or out[0]:
            fail(f'{beat}: the native page faulted ({out[0]} at {out[1]:#x})')
        compare_prims(pg.gs.prims, prims, q, out[2], beat)
        if (counts.mscal_lane, counts.mscal_sprite, counts.mscal_snow, counts.mscal_streak) != \
                (pg.mscals[M.PROGRAM_LANE], pg.mscals[M.PROGRAM_SPRITE], pg.mscals[M.PROGRAM_SNOW],
                 pg.mscals[M.PROGRAM_STREAK]):
            fail(f'{beat}: MSCALs native {counts.mscal_lane}/{counts.mscal_sprite}/{counts.mscal_snow}, '
                 f'original {dict(pg.mscals)}')
        if weather:
            if pg.mscals[M.PROGRAM_SNOW] != 108:
                fail(f'{beat}: the weather kick ran {pg.mscals[M.PROGRAM_SNOW]} snow MSCALs, not 108 tiles')
            stats['snow_pages'] += 1
        if counts.kicks != len(pg.kicks) or counts.direct != len(pg.directs) or counts.skipped != len(pg.skipped):
            fail(f'{beat}: counts differ: native kicks {counts.kicks} directs {counts.direct} skipped '
                 f'{counts.skipped}; original {len(pg.kicks)} {len(pg.directs)} {len(pg.skipped)}')
        if counts.cycle_inherited != pg.cycle_inherited:
            fail(f'{beat}: inherited-cycle UNPACKs {counts.cycle_inherited}, model {pg.cycle_inherited}')
        stats['pages'] += 1
        stats['page_prims'] += out[2]
        stats['page_kicks'] += counts.kicks
        stats['page_directs'] += counts.direct
        stats['page_skipped_calls'] += counts.skipped
        stats['page_stale_q'] += counts.stale_q
        if counts.stale_q:
            fail(f'{beat}: {counts.stale_q} vertices without their GIF tag\'s Q')
        stats['page_cycle_inherited'] += counts.cycle_inherited
        for t in range(8):
            if counts.prim_type[t]:
                stats[f'prim_type_{t}'] += counts.prim_type[t]
        if beat in indep and (FULL or not weather):
            rp = RandomStartPage(M.ram_reader(ram), skip, int(hashlib.sha256(beat.encode()).hexdigest()[:8], 16))
            rp.run(start)
            if rp.gs.prims != pg.gs.prims:
                fail(f'{beat}: the page draws differently from random VU1 contents')
            stats['independence_pages'] += 1
        pages.append(beat)
    return branches


# ------------------------------------------------ B / C. synthetic batches
def elf_code(elf, address, size):
    o = address - 0x100000 + 0x300
    return elf[o:o + size]


def load_program(vu, elf, program):
    if program == M.PROGRAM_LANE:
        vu.code[0:138 * 8] = elf_code(elf, 0x2332B8, 138 * 8)
    else:
        parts = {M.PROGRAM_SPRITE: M.SPRITE_MPG, M.PROGRAM_SNOW: M.SNOW_MPG,
                 M.PROGRAM_STREAK: M.STREAK_MPG, M.PROGRAM_KIND2: M.KIND2_MPG}[program]
        for code, count, micro in parts:
            vu.code[micro * 8:(micro + count) * 8] = elf_code(elf, code, count * 8)


def f32(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def num(w):
    return struct.unpack('<f', struct.pack('<I', w))[0]


def set_row(mem, row, words):
    struct.pack_into('<4I', mem, 16 * row, *[w & 0xFFFFFFFF for w in words])


def row(mem, r):
    return list(struct.unpack_from('<4I', mem, 16 * r))


def inverse4(m):
    """Inverse of a 4x4 (rows) by Gauss-Jordan, host floats (for placing
    synthetic slots in view; the arithmetic under test never sees it)."""
    a = [list(map(float, r)) + [1.0 if i == j else 0.0 for j in range(4)] for i, r in enumerate(m)]
    for c in range(4):
        p = max(range(c, 4), key=lambda r: abs(a[r][c]))
        a[c], a[p] = a[p], a[c]
        d = a[c][c]
        if abs(d) < 1e-30:
            return None
        a[c] = [x / d for x in a[c]]
        for r in range(4):
            if r != c:
                f = a[r][c]
                a[r] = [x - f * y for x, y in zip(a[r], a[c])]
    return [r[4:] for r in a]


def lane_case(rng, seed_state, elf):
    mem = bytearray(seed_state[0])
    # rows 0..3: the clip projection (columns as the program multiplies:
    # result = sum_k row_k * p_k): place slots so that clip = target.
    cols = [[num(w) for w in row(mem, r)] for r in range(4)]
    mat = [[cols[k][i] for k in range(4)] for i in range(4)]   # clip = mat . (x, y, z, 1)
    inv = inverse4(mat)
    fog = [255.0, 2048.0, rng.uniform(50, 400), -rng.uniform(0.05, 1.5)]
    if rng.random() < 0.2:
        fog[0] = rng.choice([100.0, 200.0, 255.0])
    set_row(mem, 8, [f32(x) for x in fog])
    for k in range(4):                                        # the corners
        set_row(mem, 0xE + k, [f32(rng.uniform(-1.5, 1.5)), f32(rng.uniform(-1.5, 1.5)),
                               f32(rng.uniform(-0.2, 0.2)), f32(1.0)])
    active = 0
    for s in range(32):
        base = 0x20 + 6 * s
        countdown = rng.choice([0, -1, -0x8000, 1, 2, 60, 119, 0x7FFF, rng.randint(-0x8000, 0x7FFF),
                                0x12345, 0x10000])
        if rng.random() < 0.5:
            countdown = rng.randint(1, 240)
        if (countdown & 0xFFFF) and not (countdown & 0x8000):
            active += 1
        target = [rng.uniform(-1.4, 1.4), rng.uniform(-1.4, 1.4), rng.uniform(-1.1, 1.1), 1.0]
        w = rng.choice([rng.uniform(5, 800), rng.uniform(0.2, 3), rng.uniform(-50, -1)])
        target = [x * w for x in target[:3]] + [w]
        pos = [sum(inv[i][j] * target[j] for j in range(4)) for i in range(4)] if inv else [0, 0, 0, 1]
        if abs(pos[3]) > 1e-12:
            pos = [x / pos[3] for x in pos[:3]]
        else:
            pos = pos[:3]
        scale = rng.choice([0.5, 5.0, 40.0, 200.0])
        rot = [[rng.uniform(-1, 1) * scale for _ in range(3)] for _ in range(3)]
        for r in range(3):
            set_row(mem, base + r, [f32(v) for v in rot[r]] + [0])
        set_row(mem, base + 3, [f32(pos[0]), f32(pos[1]), f32(pos[2]), f32(1.0)])
        set_row(mem, base + 4, [f32(rng.uniform(0, 255)) for _ in range(3)] + [f32(rng.uniform(0, 128))])
        tex0 = rng.getrandbits(64)
        set_row(mem, base + 5, [tex0 & 0xFFFFFFFF, tex0 >> 32, countdown & 0xFFFFFFFF, rng.getrandbits(32)])
    return mem, active


def sprite_case(rng, seed_state, elf):
    mem = bytearray(seed_state[0])
    d88 = row(mem, 88)
    count = rng.choice([1, 2, 27, 28, 29, 56, 57, 80, 90, rng.randint(1, 90)])
    flags = rng.randint(0, 31)
    d88 = [count, f32(rng.choice([0.0, -0.05, -0.4, 0.3])), flags, d88[3]]
    set_row(mem, 88, d88)
    d89 = row(mem, 89)
    phase = rng.choice([num(d89[0]), rng.uniform(0, 2), rng.uniform(-0.5, 5), 0.0])
    set_row(mem, 89, [f32(phase), f32(rng.choice([1.0, 0.5, 2.0])), f32(rng.choice([1e-6, 0.3, 1.0, 5.0])),
                      f32(rng.uniform(0.0001, 1.0))])
    d86 = [num(w) for w in row(mem, 86)]
    b0 = rng.uniform(0, 0.8)
    set_row(mem, 86, [f32(b0), f32(b0 + rng.choice([rng.uniform(0.05, 1.5), 0.0])),
                      f32(rng.uniform(0.1, 2.0)), f32(rng.uniform(0.2, 3.0))])
    for r in (80, 81):
        set_row(mem, r, [f32(rng.uniform(-8, 8)) for _ in range(3)] + [row(mem, r)[3]])
    for r in (82, 83):
        set_row(mem, r, [f32(rng.uniform(0, 300)) for _ in range(4)])
    for r in (84, 85):
        set_row(mem, r, [f32(rng.uniform(0, 30)) for _ in range(4)])
    d87 = row(mem, 87)
    set_row(mem, 87, [d87[0], d87[1], f32(rng.choice([0.0, 0.25, -0.1])), f32(rng.choice([0.0, 0.5]))])
    # the tile matrix: move it so some particles cross the view planes
    t = row(mem, 93)
    shift = rng.choice([0.0, 30.0, 300.0, -300.0, 3000.0])
    set_row(mem, 93, [f32(num(t[0]) + rng.uniform(-1, 1) * shift), f32(num(t[1]) + rng.uniform(-1, 1) * shift),
                      f32(num(t[2]) + rng.uniform(-1, 1) * shift), t[3]])
    return mem, count


def packet_uploads(elf, packet):
    """The UNPACK V1-32 / V4-32 rows a program packet uploads (decoded from
    the ELF's packet: {row: [4 words]}, in packet order)."""
    at = packet + 16
    qwc = struct.unpack_from('<I', elf_code(elf, packet, 4))[0] & 0xFFFF
    end, rows = packet + 16 + 16 * qwc, {}
    while at < end:
        code = struct.unpack('<I', elf_code(elf, at, 4))[0]
        cmd, num, imm = code >> 24 & 0x7F, code >> 16 & 0xFF, code & 0xFFFF
        at += 4
        if cmd == 0x4A:                                          # MPG: 8-byte aligned code
            at += (8 - at % 8) % 8 + 8 * (num or 256)
            continue
        if cmd in (0x60, 0x6C):
            comps = 1 if cmd == 0x60 else 4
            for k in range(num or 256):
                words = struct.unpack(f'<{comps}I', elf_code(elf, at + 4 * comps * k, 4 * comps))
                rows[(imm & 0x3FF) + k] = list(words) * 4 if comps == 1 else list(words)
            at += 4 * comps * (num or 256)
            continue
        if cmd == 0x20:
            at += 4
    return rows


def streak_seed(elf, sprite_state, program=M.PROGRAM_STREAK):
    """A sprite MSCAL's memory as the streak (or kind-2) program would find
    it: the program packet's uploads, then the rows 001CFBE0's packets 1, 2
    and 4 send after the CALL (80..93 and 110..124) as the captured sprite
    MSCAL had them."""
    mem = bytearray(sprite_state[0])
    rows = packet_uploads(elf, program)
    if sorted(rows) != list(range(129 if program == M.PROGRAM_STREAK else 128)):
        fail(f'the {PROGRAM_NAME[program]} packet uploads rows {min(rows)}..{max(rows)} ({len(rows)})')
    keep = {r: row(mem, r) for r in list(range(80, 94)) + list(range(110, 125))}
    for r, words in rows.items():
        set_row(mem, r, words)
    for r, words in keep.items():
        set_row(mem, r, words)
    return (bytes(mem),)


def streak_case(rng, seed_state, elf):
    mem, count = sprite_case(rng, seed_state, elf)
    entry = rng.choice([0, 1])                                   # D_00251260: kind 0, mode 1 / 2..4
    set_row(mem, 124, list(struct.unpack('<4I', elf_code(elf, 0x251260 + 16 * entry, 16))))
    return mem, count


def kind2_case(rng, seed_state, elf):
    mem, count = sprite_case(rng, seed_state, elf)
    entry = rng.choice([4, 5])                                   # D_00251260: kind 2, mode 1 / 2..4
    set_row(mem, 124, list(struct.unpack('<4I', elf_code(elf, 0x251260 + 16 * entry, 16))))
    return mem, count


def run_synthetic(lib, elf, program, cases, stats, branches, seed, seeds):
    if program in (M.PROGRAM_STREAK, M.PROGRAM_KIND2):
        seeds = [streak_seed(elf, s, program) for s in seeds.get(M.PROGRAM_SPRITE, [])]
    else:
        seeds = seeds.get(program)
    if not seeds:
        fail(f'no captured MSCAL of {program:#x}')
    rng = random.Random(seed)
    for n in range(cases):
        state = rng.choice(seeds)
        mem, extra = {M.PROGRAM_LANE: lane_case, M.PROGRAM_STREAK: streak_case,
                      M.PROGRAM_KIND2: kind2_case}.get(program, sprite_case)(
            rng, state, elf)
        vu = Oracle()
        load_program(vu, elf, program)
        vu.mem[:] = mem
        for i in range(1, 32):
            vu.v[i] = [finite(rng) for _ in range(4)]
        vu.vi = [0] + [rng.getrandbits(16) for _ in range(15)]
        vu.accw = [finite(rng) for _ in range(4)]
        vu.q, vu.iw, vu.r, vu.cf = finite(rng), finite(rng), rng.getrandbits(23), rng.getrandbits(24)
        vu.p = finite(rng)
        where = f'synthetic {program:#x} case {n}'
        if compare_mscal(lib, vu, program, where, stats):
            check_timing(vu, program, where)
            stats['synthetic_' + PROGRAM_NAME[program]] += 1
        for key, k in vu.branch.items():
            branches[(program,) + key] += k


# ---------------------------------------------------------------- E. faults
def operand_faults(lib, elf, stats, seeds):
    for program, rows in ((M.PROGRAM_LANE, [(0x20 + 3, 0)]), (M.PROGRAM_SPRITE, [(89, 0), (86, 2)]),
                          (M.PROGRAM_SNOW, [(89, 0), (86, 2)]), (M.PROGRAM_STREAK, [(89, 0), (86, 2), (127, 2)]),
                          (M.PROGRAM_KIND2, [(89, 0), (86, 2)])):
        state = streak_seed(elf, seeds[M.PROGRAM_SPRITE][0], program) \
            if program in (M.PROGRAM_STREAK, M.PROGRAM_KIND2) else seeds[program][0]
        for r, lane in rows:
            mem = bytearray(state[0])
            if program == M.PROGRAM_LANE:
                w = row(mem, 0x20 + 5)
                w[2] = 5
                set_row(mem, 0x20 + 5, w)
            w = row(mem, r)
            w[lane] = 0x7F800000
            set_row(mem, r, w)
            vu = Oracle()
            load_program(vu, elf, program)
            vu.mem[:] = mem
            if compare_mscal(lib, vu, program, f'operand fault {program:#x} row {r}', stats):
                fail(f'an exponent-255 word on a live lane of {program:#x} did not fault')
    # The EFU's unestablished operands: K's w column (rows 118..121) zero
    # makes every head's w zero (ERCPR of zero); both sides fault.
    mem = bytearray(streak_seed(elf, seeds[M.PROGRAM_SPRITE][0])[0])
    for r in range(118, 122):
        w = row(mem, r)
        w[3] = 0
        set_row(mem, r, w)
    vu = Oracle()
    load_program(vu, elf, M.PROGRAM_STREAK)
    vu.mem[:] = mem
    before = stats['operand_faults']
    if compare_mscal(lib, vu, M.PROGRAM_STREAK, 'EFU fault: ERCPR of zero', stats) or \
            stats['operand_faults'] != before + 1:
        fail('ERCPR of a zero w did not fault')
    stats['efu_faults'] += 1
    # The kind-2 program's ERCPR: the same rows (its head through 118..121).
    mem = bytearray(streak_seed(elf, seeds[M.PROGRAM_SPRITE][0], M.PROGRAM_KIND2)[0])
    for r in range(118, 122):
        w = row(mem, r)
        w[3] = 0
        set_row(mem, r, w)
    vu = Oracle()
    load_program(vu, elf, M.PROGRAM_KIND2)
    vu.mem[:] = mem
    before = stats['operand_faults']
    if compare_mscal(lib, vu, M.PROGRAM_KIND2, 'EFU fault: kind-2 ERCPR of zero', stats) or \
            stats['operand_faults'] != before + 1:
        fail('the kind-2 ERCPR of a zero w did not fault')
    stats['efu_faults'] += 1


def tag(tid, qwc, addr):
    return struct.pack('<IIII', (tid << 28) | qwc, addr, 0, 0)


def page_faults(lib, stats):
    """Malformed pages in scratch memory at 0x1000: each must latch the
    documented fault; a well-formed decal-shaped fan draws."""
    START = 0x1000
    FAULTS = {1: 'ARGS', 2: 'READ', 3: 'DMA', 4: 'VIF', 5: 'PROGRAM', 6: 'VU', 7: 'GIF', 8: 'CAPACITY'}

    def run(chain, want, capacity=64, extra=()):
        mem = bytearray(0x10000)
        mem[START:START + len(chain)] = chain
        for at, data in extra:
            mem[at:at + len(data)] = data
        rc, prims, q, counts, out = native_page(lib, [(0, mem)], START, [], capacity)
        got = FAULTS.get(out[0], 'none')
        if want != got or (want != 'none') != (rc != 0):
            fail(f'fault case {want}: got {got} (rc {rc})')
        stats['fault_cases'] += 1
        return prims, q, out[2]

    def cnt(words, link=START + 0x20):
        """A NEXT-closed CNT at START + 0x100 holding `words` (padded)."""
        body = b''.join(struct.pack('<I', w) for w in words)
        body += bytes(-len(body) % 16)
        head = tag(2, 0, START + 0x100)
        blk = tag(1, len(body) // 16, 0) + body + tag(2, 0, link)
        return head, blk

    def build(words):
        head, blk = cnt(words)
        chain = bytearray(0x400)
        chain[0:16] = head
        chain[0x100:0x100 + len(blk)] = blk
        return bytes(chain)

    # a clean fan of 3 vertices (ST, RGBAQ, XYZF2) with the mode-1 state
    gif_lo = 3 | (1 << 15) | (1 << 46) | (0x7D << 47) | (3 << 60)
    ad = [(0x17E, 0x00), (0x60, 0x14), (0x53001, 0x47), (0x44, 0x42), (0, 0x08), (1, 0x46),
          (0x2004290511322469, 0x06)]
    words = [0, 0, 0, 0x50000000 | (1 + len(ad))]
    words += [len(ad) | (1 << 15), 0x10000000, 0xE, 0]
    for data, reg in ad:
        words += [data & 0xFFFFFFFF, data >> 32, reg, 0]
    words += [0, 0, 0, 0x50000000 | (1 + 9)]
    words += [gif_lo & 0xFFFFFFFF, gif_lo >> 32, 0x412, 0]
    for k in range(3):
        words += [f32(0.5 * k), f32(0.25), f32(1.0), 0]
        words += [0x80, 0x80, 0x80, 0x80]
        words += [0x7000 + 0x100 * k, 0x7900 + 0x80 * k, 0x100000, 0x800]
    prims, q, n = run(build(words), 'none')
    if n != 1 or prims[0].prim != 0x7D or prims[0].count != 3 or prims[0].tex0 != 0x2004290511322469:
        fail('the clean fan did not draw one triangle with its state')
    run(build([0, 0, 0, 0x04000000]), 'VIF')                              # ITOP
    run(build([0, 0, 0, 0x14000000]), 'PROGRAM')                          # MSCAL, no program
    run(build([0, 0, 0x4A010000, 0, 0, 0, 0, 0]), 'PROGRAM')              # an unknown MPG
    reglist = (1 | (1 << 15) | (1 << 58) | (1 << 60))
    run(build([0, 0, 0, 0x50000002, reglist & 0xFFFFFFFF, reglist >> 32, 0x1, 0, 0, 0, 0, 0]), 'GIF')
    run(build([0, 0, 0, 0x50000002, 1 | (1 << 15), 0x10000000, 0xE, 0, 0, 0, 0x4C, 0]), 'GIF')  # FRAME_1
    chain = bytearray(0x400)
    chain[0:16] = tag(7, 0, 0)                                            # END
    run(bytes(chain), 'DMA')
    chain = bytearray(0x400)
    chain[0:16] = tag(3, 4, 0x7FFFF000)                                   # REF unmapped
    run(bytes(chain), 'READ')
    run(build(words), 'CAPACITY', capacity=0)


# ----------------------------------------------------- F. the preset bank
PRESETS, PRESETS_SIZE = 0x814220 + 0x6A0, 0x500


def presets(lib, elf, stats):
    """001D0F20's blend-preset bank (em_gs_blocks_original): the native bank
    equals every route capture's bytes (the boot builder writes them once;
    nothing writes them after) and, in full mode, the bytes the ORIGINAL
    001D0F20 writes when executed (its callees stubbed: none of them writes
    the bank)."""
    bank = C.create_string_buffer(PRESETS_SIZE)
    if lib.shim_presets(bank) != 0:
        fail('the preset bank refused a buffer')
    native = bank.raw
    for beat in BEATS:
        ram = (ROUTE / beat / 'eeMemory.bin').read_bytes()
        if ram[PRESETS:PRESETS + PRESETS_SIZE] != native:
            fail(f'{beat}: the blend-preset bank differs from 001D0F20\'s')
        stats['preset_captures'] += 1
    if FULL:
        import test_effect_manager_reference as EMR
        ram = bytearray((ROUTE / BEATS[0] / 'eeMemory.bin').read_bytes())
        ram[PRESETS:PRESETS + PRESETS_SIZE] = bytes(PRESETS_SIZE)
        spad = bytearray((ROUTE / BEATS[0] / 'scratchpad.bin').read_bytes())
        e = EMR.Oracle(elf, ram, spad)
        for a in (0x21B860, 0x1D2830, 0x1D25F0, 0x1DEDE0, 0x101898, 0x121870, 0x21B970, 0x21BA80, 0x1CB5C0):
            e.stubs[a] = lambda ee: None
        e.run(0x1D0F20, stack=0x70003F00)
        if bytes(e.ram[PRESETS:PRESETS + PRESETS_SIZE]) != native:
            fail('the ORIGINAL 001D0F20 writes another blend-preset bank')
        stats['preset_original'] += 1


# ------------------------------------------------------------------- main
def main():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    if hashlib.sha256(elf).hexdigest() != ELF_SHA:
        fail('boot ELF is not the pinned build')
    lib = build_lib()
    stats = Stats()
    seeds = {}
    branches = part_a(lib, stats, elf, seeds)
    n_syn = pick(600, 40)
    run_synthetic(lib, elf, M.PROGRAM_LANE, n_syn, stats, branches, 0x1A7E, seeds)
    run_synthetic(lib, elf, M.PROGRAM_SPRITE, n_syn, stats, branches, 0x5B21, seeds)
    n_snow = pick(300, 20)
    run_synthetic(lib, elf, M.PROGRAM_SNOW, n_snow, stats, branches, 0x5A0C, seeds)
    run_synthetic(lib, elf, M.PROGRAM_STREAK, n_syn, stats, branches, 0x57EA, seeds)
    run_synthetic(lib, elf, M.PROGRAM_KIND2, n_syn, stats, branches, 0x2540, seeds)
    for program, sites in BRANCHES.items():
        for site in sites:
            for taken in (True, False):
                if not branches[(program, site, taken)]:
                    fail(f'{program:#x}: the branch at micro {site:#05x} was never '
                         f'{"taken" if taken else "not taken"}')
    operand_faults(lib, elf, stats, seeds)
    page_faults(lib, stats)
    presets(lib, elf, stats)
    if stats['synthetic_lane'] < n_syn // 2 or stats['synthetic_sprite'] < n_syn // 2 or \
            stats['synthetic_snow'] < n_snow // 2 or stats['synthetic_streak'] < n_syn // 2 or \
            stats['synthetic_kind2'] < n_syn // 2:
        fail(f'too few synthetic cases ran: {stats}')
    banner(part(stats['independence_pages'], stats['pages'], 'pages re-walked from random VU1 state'),
           part(stats['snow_pages'], stats['pages'], 'pages with the weather kick walked'),
           f"{stats['pages']} captured pages ({stats['page_prims']} primitives, {stats['page_kicks']} XGKICKs, "
           f"{stats['page_directs']} DIRECT packets, {stats['page_skipped_calls']} non-port CALLs walked over)",
           f"{stats['mscal_lane']} lane, {stats['mscal_sprite']} sprite, {stats['mscal_snow']} snow, "
           f"{stats['mscal_streak']} streak and {stats['mscal_kind2']} kind-2 MSCALs compared "
           f"({stats['synthetic_lane']}, {stats['synthetic_sprite']}, {stats['synthetic_snow']}, "
           f"{stats['synthetic_streak']} and {stats['synthetic_kind2']} synthetic; "
           f"{stats['efu_faults']} EFU operand faults)",
           f"{stats['kicks']} XGKICKs, {stats['kicked_packet_bytes']:,} packet bytes",
           f"{stats['operand_faults']} operand faults, {stats['fault_cases']} page fault cases",
           f"the 001D0F20 blend-preset bank equal in {stats['preset_captures']} captures"
           + (" and to the executed original" if stats['preset_original'] else ""))
    print('chain page reference: PASS (primitive types ' +
          ', '.join(f'{k[10:]}: {v}' for k, v in sorted(stats.items()) if k.startswith('prim_type_')) +
          f"; every vertex with its GIF tag's Q; "
          f"{stats['page_cycle_inherited']} UNPACKs before the page's first STCYCL)")
    return 0


if __name__ == '__main__':
    sys.exit(main())
