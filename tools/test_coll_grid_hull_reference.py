#!/usr/bin/env python3
"""Execute the original grid pass 0019CB60 and the hull locks 001A6440,
001A6AD0 and 001A7280, and compare src/game/em_coll_grid_hull.c
(docs/COLL_GRID_HULL.md).

The original instructions run, unmodified, over captured AREA11 RAM (the
PCSX2 route beats, docs/FIRST_LEVEL_ROUTE.md) in the move lane's FloatEE
(tools/test_coll_move_reference.py: the shared EE with every COP1 and VU0
macro instruction through tools/ee_float_model.py). The code executed is
checked against the pinned ELF: 0019CB60, 0019F1A0, 0019ED80, the three
locks, and the SDK leaves 001026A0, 001028B8, 001028D0, 00102738,
00103230 and copy_qw4.

Every case compares the return value and the WHOLE 16 KB scratchpad: the
original's final scratchpad must equal the initial one with the native
state written into the words it models (0x70003190..0x700031D8,
0x700030CA..0x700030DC, the ranks 0x70003240..0x7000324A, 0x7000324E,
0x70003B86/88). The original must write no RAM.

Worlds:
  - route beats 05, 06, 10, 12, 13 and 14 (0019CB60 on segments at the
    player's recorded positions, with the persistent words from the
    capture or randomized; every attribute gate value with every query
    class on nodes the segments really cross);
  - beats 13 and 14 hold Roger in the published class-2 list (D_00275B8C):
    001A6440 / 001A6AD0 walk his real +0x58 chain with his real bone
    matrices, and 001A7280 walks the player's own chain in every beat;
  - synthetic class-2 entities and player chains written into free RAM on
    both sides: several entities and records, bone-slot switches, the -2
    first record, negative vertex counts, every mask byte against every
    argument bit, rotated and translated bone matrices, entity status and
    class filters, and exact-threshold cases (the facing and edge
    epsilons, strict interval ends moving -x and +x (planes facing either
    way, the facing test on and off), the surface-class ratios 3.0 and
    0.49029058 with both signs of ny);
  - the y and z arms of the interval test: segments along y alone or z
    alone (so only that axis's clauses can accept), in both directions,
    through exact y- and z-plane records (identity and spun frames, strict
    ends on the plane), synthetic worlds and the real Roger / player
    chains; records with vertex count 0 beside count -1. The run fails
    unless every lock accepts some case through each x, y and z clause.

The native chains come from the same RAM through a resolver
(CHAIN_C, shared with the move and segment tests): the chain bytes at
+0x58 and the 0x40 bytes at *(+0x110 + 4m) + 0x90 of each bone slot m.

Default run: a covering sample (about 10 s). EM_TEST_FULL=1 runs every case.
No original bytes are embedded: the ELF, RAM images and captures are the
user's own local files.
"""
import ctypes as C
import hashlib
import math
import os
import random
import struct
import subprocess
import sys
import time
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))

# ---------------------------------------------------------------------------
# The RAM-backed chain resolver (the native world's EmCollHullWorld.chain).
# Defined before the imports below: test_coll_move_reference imports this
# module back for it.

CHAIN_C = r"""
/* ---- RAM-backed hull chains (tools/test_coll_grid_hull_reference.py) ----
 * The test's world: an entity's chain is the RAM its +0x58 names, bone slot
 * m's matrix the 0x40 bytes at *(+0x110 + 4m) + 0x90, read 16-byte aligned
 * as copy_qw4's quadword loads do. */
typedef struct {
    const uint8_t *ram;
    size_t size;
    uint32_t (*address_of)(void *owner, const EmActor *e);
    void *owner;
    int calls;
    float slots[256 * 16];
} HullRam;

static uint32_t hull_ram_u32(const HullRam *h, uint32_t a)
{
    uint32_t v;
    memcpy(&v, h->ram + a, 4);
    return v;
}

static int hull_ram_chain(void *context, const EmActor *body, EmCollHullChain *out)
{
    HullRam *h = context;
    if (!h || !h->ram || !body || !h->address_of) return -1;
    const uint32_t a = h->address_of(h->owner, body) & 0x1FFFFFFu;
    if (!a || (size_t)a + 0x110 + 4 * 256 > h->size) return -1;
    const uint32_t chain = hull_ram_u32(h, a + 0x58);
    if (chain >= 0x40000000u || (chain & 0x1FFFFFFu) >= h->size) return -1;
    const unsigned bones = h->ram[a + 9];
    for (unsigned m = 0; m < bones; ++m) {
        const uint32_t p = hull_ram_u32(h, a + 0x110 + 4 * m);
        const uint32_t at = ((p & 0x1FFFFFFu) + 0x90) & ~15u;
        if (p >= 0x40000000u || (size_t)at + 64 > h->size) return -1;
        memcpy(h->slots + 16 * m, h->ram + at, 64);
    }
    out->bytes = h->ram + (chain & 0x1FFFFFFu);
    out->size = h->size - (chain & 0x1FFFFFFu);
    out->slots = h->slots;
    out->slot_count = bones;
    h->calls++;
    return 0;
}
"""

import reference_mode  # noqa: E402
import test_coll_move_reference as MV  # noqa: E402
import test_coll_probe_reference as cp  # noqa: E402
from test_player_slide_reference import read_elf, sx32, DECOMP  # noqa: E402

OUT = ROOT / 'build/coll_grid_hull_reference'
ROUTE = DECOMP / 'build/s87/route'

GRID, LOCK6440, LOCK6AD0, LOCK7280 = 0x19CB60, 0x1A6440, 0x1A6AD0, 0x1A7280
RANKS, NODE_TEST = 0x19F1A0, 0x19ED80
PLAYER = 0x8102B0
CLASS2_LIST, CLASS2_COUNT = 0x275B8C, 0x275B94
CELL_RECORD = 0x700030B0
SYN = 0x01FE8000          # synthetic entities, chains and matrices (zero RAM in every beat; checked)
SYN_SIZE = 0x8000
ARGS = 0x7F0E0000         # the interpreter's private stack region

GRID_BEATS = ('05_boxes', '06_hill_slide', '10_cage_roof_roger', '12_crevice_jump', '13_east_tower')
ROGER_BEATS = ('13_east_tower', '14_roger_encounter')
PLAYER_BEATS = ('06_hill_slide', '10_cage_roof_roger', '14_roger_encounter')



def u32(buf, at):
    return struct.unpack_from('<I', buf, at)[0]


def s16(buf, at):
    return struct.unpack_from('<h', buf, at)[0]


def f32r(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def fbits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def bitsf(word):
    return struct.unpack('<f', struct.pack('<I', word & 0xFFFFFFFF))[0]


BRIDGE = r"""
#include <stdlib.h>
#include <string.h>
#include "game/em_coll_grid_hull.h"
""" + CHAIN_C + r"""
typedef struct {
    uint32_t start[4], end[4], point[3];
    uint32_t record, entity;
    uint32_t cell_class, word_1c, word_20, cell_normal[3];
    int32_t query_class, rank[6], span_lo, span_hi;
} GState;

static EmCollision g_emcl;
static EmCollProbeGrid g_grid;
static int g_loaded;

int bridge_grid_load(const char *path)
{
    if (g_loaded) return 0;
    if (em_collision_load(&g_emcl, path) || em_coll_probe_grid_load(&g_grid, &g_emcl, path)) return -1;
    g_loaded = 1;
    return 0;
}

uint8_t bridge_set_attr(int node, uint8_t attr)
{
    EmCollPoly *p = &g_emcl.polys[g_grid.first + (uint32_t)node];
    uint8_t old = p->attr;
    p->attr = attr;
    return old;
}

typedef struct {
    EmActor rec[64];
    uint32_t addr[64];
    int count;
    EmActorClassLists lists;
    HullRam ram;
    EmCollHullWorld hulls;
    uint32_t node_base;
} Bridge;

static uint32_t address_of(void *owner, const EmActor *e)
{
    Bridge *b = owner;
    for (int i = 0; i < b->count; ++i) if (&b->rec[i] == e) return b->addr[i];
    return 0;
}

static const EmActor *actor_at(Bridge *b, uint32_t addr)
{
    for (int i = 0; i < b->count; ++i) if (b->addr[i] == addr) return &b->rec[i];
    return NULL;
}

Bridge *bridge_new(const uint8_t *ram, uint32_t size, uint32_t node_base)
{
    Bridge *b = calloc(1, sizeof *b);
    if (!b) return NULL;
    b->ram.ram = ram;
    b->ram.size = size;
    b->ram.address_of = address_of;
    b->ram.owner = b;
    b->hulls.context = &b->ram;
    b->hulls.chain = hull_ram_chain;
    b->node_base = node_base;
    return b;
}

void bridge_free(Bridge *b) { free(b); }

int bridge_actor(Bridge *b, uint32_t addr, uint8_t status, uint8_t cls, uint8_t bones, uint32_t w58,
                 uint32_t w5C, uint16_t h52)
{
    int i = -1;
    for (int k = 0; k < b->count; ++k) if (b->addr[k] == addr) i = k;
    if (i < 0) {
        if (b->count == 64) return -1;
        i = b->count++;
        b->addr[i] = addr;
    }
    EmActor *a = &b->rec[i];
    memset(a, 0, sizeof *a);
    a->status = status; a->cls = cls; a->bones = bones; a->w58 = w58; a->w5C = w5C; a->h52 = h52;
    a->self = a;
    return 0;
}

/* The published class-2 list: entry j (what D_00275B8C[j] holds). */
int bridge_class2(Bridge *b, const uint32_t *entries, int count)
{
    EmActorClassList *l = &b->lists.list[EM_ACTOR_LIST_CLASS2];
    memset(l, 0, sizeof *l);
    if (count < 0 || count > EM_ACTOR_LIST_MAX) return -1;
    for (int j = 0; j < count; ++j) {
        const EmActor *a = actor_at(b, entries[j]);
        if (!a) return -1;
        l->slot[count - 1 - j] = a;
    }
    l->published = (int16_t)count;
    return 0;
}

void bridge_player(Bridge *b, uint32_t addr) { b->hulls.player = addr ? actor_at(b, addr) : NULL; }
void bridge_no_chains(Bridge *b, int none) { b->hulls.chain = none ? NULL : hull_ram_chain; }
void bridge_ram_size(Bridge *b, uint32_t size) { b->ram.size = size; }
int bridge_chain_calls(Bridge *b) { return b->ram.calls; }

static float fb(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static uint32_t bf(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

static uint32_t entity_addr(Bridge *b, const EmActor *e) { return e ? address_of(b, e) : 0; }

/* 0019CB60 over the probe state view. */
int bridge_grid(Bridge *b, GState *g)
{
    EmCollProbeState s;
    memset(&s, 0, sizeof s);
    for (int k = 0; k < 4; ++k) { s.start[k] = fb(g->start[k]); s.end[k] = fb(g->end[k]); }
    for (int k = 0; k < 3; ++k) s.point[k] = fb(g->point[k]);
    s.record = EM_COLL_PROBE_RECORD_NONE; s.node = -1;
    s.query_class = (int16_t)g->query_class;
    for (int k = 0; k < 6; ++k) s.rank[k] = (int16_t)g->rank[k];
    s.span_lo = (int16_t)g->span_lo; s.span_hi = (int16_t)g->span_hi;
    int r = em_coll_grid_hull_0019CB60(&g_grid, &s);
    if (r < 0) return r;
    for (int k = 0; k < 4; ++k) { g->start[k] = bf(s.start[k]); g->end[k] = bf(s.end[k]); }
    for (int k = 0; k < 3; ++k) g->point[k] = bf(s.point[k]);
    if (s.record == EM_COLL_PROBE_RECORD_GRID) {
        const void *rec = em_coll_grid_hull_node_record(&g_grid, s.node);
        if (em_coll_grid_hull_node_index(&g_grid, rec) != s.node) return -7;
        g->record = b->node_base + 0x40u * (uint32_t)s.node;
    }
    for (int k = 0; k < 6; ++k) g->rank[k] = s.rank[k];
    g->span_lo = s.span_lo; g->span_hi = s.span_hi;
    return r;
}

/* which: 0 001A6440, 1 001A6AD0, 2 001A7280. */
int bridge_lock(Bridge *b, int which, GState *g, uint32_t arg)
{
    EmCollHullScratch h;
    memset(&h, 0, sizeof h);
    for (int k = 0; k < 3; ++k) {
        h.start[k] = fb(g->start[k]); h.end[k] = fb(g->end[k]); h.point[k] = fb(g->point[k]);
        h.cell_normal[k] = fb(g->cell_normal[k]);
    }
    h.cell_class = (uint16_t)g->cell_class; h.word_1c = g->word_1c; h.word_20 = g->word_20;
    h.entity = g->entity ? actor_at(b, g->entity) : NULL;
    if (g->entity && !h.entity) return -9;
    int r = which == 0 ? em_coll_grid_hull_001A6440(&b->lists, &b->hulls, &h, arg)
          : which == 1 ? em_coll_grid_hull_001A6AD0(&b->lists, &b->hulls, &h, arg)
          : em_coll_grid_hull_001A7280(&b->hulls, &h, arg);
    if (r < 0) return r;
    for (int k = 0; k < 3; ++k) { g->point[k] = bf(h.point[k]); g->cell_normal[k] = bf(h.cell_normal[k]); }
    g->cell_class = h.cell_class; g->word_1c = h.word_1c; g->word_20 = h.word_20;
    g->entity = entity_addr(b, h.entity);
    if (h.record_cell) g->record = 0x700030B0u;
    return r;
}
"""


class GState(C.Structure):
    _fields_ = [('start', C.c_uint32 * 4), ('end', C.c_uint32 * 4), ('point', C.c_uint32 * 3),
                ('record', C.c_uint32), ('entity', C.c_uint32), ('cell_class', C.c_uint32),
                ('word_1c', C.c_uint32), ('word_20', C.c_uint32), ('cell_normal', C.c_uint32 * 3),
                ('query_class', C.c_int32), ('rank', C.c_int32 * 6), ('span_lo', C.c_int32),
                ('span_hi', C.c_int32)]


# (field, scratchpad offset, element size, count, signed)
STATE_WORDS = [('start', 0x3190, 4, 4, False), ('end', 0x31A0, 4, 4, False), ('point', 0x31B0, 4, 3, False),
               ('record', 0x31D0, 4, 1, False), ('entity', 0x31D4, 4, 1, False),
               ('cell_class', 0x30CA, 2, 1, False), ('word_1c', 0x30CC, 4, 1, False),
               ('word_20', 0x30D0, 4, 1, False), ('cell_normal', 0x30D4, 4, 3, False),
               ('query_class', 0x324E, 2, 1, True), ('rank', 0x3240, 2, 6, True),
               ('span_lo', 0x3B86, 2, 1, True), ('span_hi', 0x3B88, 2, 1, True)]


def state_of(spad):
    g = GState()
    for name, at, size, count, signed in STATE_WORDS:
        values = [int.from_bytes(spad[at + size * k:at + size * (k + 1)], 'little', signed=signed)
                  for k in range(count)]
        if count == 1:
            setattr(g, name, values[0])
        else:
            arr = getattr(g, name)
            for k in range(count): arr[k] = values[k]
    return g


def state_into(spad, g):
    for name, at, size, count, signed in STATE_WORDS:
        values = [getattr(g, name)] if count == 1 else list(getattr(g, name))
        for k, v in enumerate(values):
            spad[at + size * k:at + size * (k + 1)] = (v & ((1 << (8 * size)) - 1)).to_bytes(size, 'little')


SOURCES = ['src/game/em_coll_grid_hull.c', 'src/game/em_coll_probe_original.c', 'src/game/em_actor_collision.c',
           'src/game/em_collision.c', 'src/game/em_actor_pool.c', 'src/game/em_effect_original.c']


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    source = OUT / 'bridge.c'
    source.write_text(BRIDGE)
    lib = OUT / ('bridge.dylib' if sys.platform == 'darwin' else 'bridge.so')
    command = ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared', '-fPIC',
               '-Isrc', str(source)] + SOURCES + ['-lm', '-o', str(lib)]
    if os.environ.get('EM_TEST_UBSAN', '') not in ('', '0'):
        command[1:1] = ['-fsanitize=undefined', '-fno-sanitize-recover=all']
        lib = OUT / ('bridge_ubsan' + lib.suffix)
        command[-1] = str(lib)
    digest = hashlib.sha256(' '.join(command).encode() + BRIDGE.encode())
    for path in SOURCES:
        digest.update((ROOT / path).read_bytes())
    for header in sorted((ROOT / 'src').rglob('*.h')):
        digest.update(header.read_bytes())
    stamp = Path(str(lib) + '.sha256')
    if not (lib.exists() and stamp.exists() and stamp.read_text() == digest.hexdigest()):
        subprocess.run(command, cwd=ROOT, check=True)
        stamp.write_text(digest.hexdigest())
    n = C.CDLL(str(lib))
    V, U8, U16, U32, I = C.c_void_p, C.c_uint8, C.c_uint16, C.c_uint32, C.c_int
    n.bridge_grid_load.argtypes = [C.c_char_p]
    n.bridge_set_attr.restype = U8
    n.bridge_set_attr.argtypes = [I, U8]
    n.bridge_new.restype = V
    n.bridge_new.argtypes = [C.c_void_p, U32, U32]
    n.bridge_free.argtypes = [V]
    n.bridge_actor.argtypes = [V, U32, U8, U8, U8, U32, U32, U16]
    n.bridge_class2.argtypes = [V, C.POINTER(U32), I]
    n.bridge_player.argtypes = [V, U32]
    n.bridge_no_chains.argtypes = [V, I]
    n.bridge_ram_size.argtypes = [V, U32]
    n.bridge_chain_calls.argtypes = [V]
    n.bridge_grid.argtypes = [V, C.POINTER(GState)]
    n.bridge_lock.argtypes = [V, I, C.POINTER(GState), U32]
    return n


# ---------------------------------------------------------------------------
# Chains in RAM (both sides read the same bytes)

def class2_entries(ram):
    base, count = u32(ram, CLASS2_LIST), s16(ram, CLASS2_COUNT)
    return [u32(ram, base + 4 * j) for j in range(max(0, count))]


def actor_fields(ram, a):
    return (ram[a], ram[a + 2], ram[a + 9], u32(ram, a + 0x58), u32(ram, a + 0x5C),
            struct.unpack_from('<H', ram, a + 0x52)[0])


def slot_matrix(ram, a, m):
    p = (u32(ram, a + 0x110 + 4 * m) & 0x1FFFFFF) + 0x90 & ~15
    return [struct.unpack_from('<4f', ram, p + 16 * k) for k in range(4)]


def apply(mat, v, w):
    return [mat[0][i] * v[0] + mat[1][i] * v[1] + mat[2][i] * v[2] + mat[3][i] * w for i in range(3)]


def chain_polys(ram, a):
    """World-space polygons of an entity's chain (host floats, for aiming
    segments only): [(verts, normal, record offset)]."""
    chain = u32(ram, a + 0x58) & 0x1FFFFFF
    if not chain:
        return []
    count, rec = u32(ram, chain), chain + 4
    if s16(ram, chain + 0xA) == -2:
        rec += struct.unpack_from('<H', ram, rec + 4)[0]
        count -= 1
    out = []
    for _ in range(min(count, 64)):
        n = s16(ram, rec + 6)
        if n > 0 and ram[rec + 3] < ram[a + 9]:
            mat = slot_matrix(ram, a, ram[rec + 3])
            normal = apply(mat, struct.unpack_from('<3f', ram, rec + 8), 0.0)
            verts = [apply(mat, struct.unpack_from('<3f', ram, rec + 0x18 + 12 * k), 1.0) for k in range(n)]
            out.append((verts, normal, rec - chain))
        rec += struct.unpack_from('<H', ram, rec + 4)[0]
    return out


# ---------------------------------------------------------------------------
# Worlds

class World:
    def __init__(self, beat):
        self.beat = beat
        self.ram = bytearray((ROUTE / beat / 'eeMemory.bin').read_bytes())
        self.spad = bytes((ROUTE / beat / 'scratchpad.bin').read_bytes())
        MV.check_code(ELF, self.ram, beat)
        assert not any(self.ram[SYN:SYN + SYN_SIZE]), (beat, 'the synthetic region is in use')
        self.node_base = u32(self.spad, 0x3208)
        self.node_count = u32(self.spad, 0x320C)
        assert self.node_count == G_COUNT[0], (beat, 'grid node count differs from the EMCL')
        self.ee = MV.FloatEE(ELF, self.ram, self.spad)
        self.ram = self.ee.mem

    def bridge(self):
        """A native world over the current RAM: the class-2 entities, the
        player, and the RAM view the chain resolver reads."""
        buf = (C.c_char * len(self.ram)).from_buffer(self.ram)
        b = NATIVE.bridge_new(C.addressof(buf), len(self.ram), self.node_base)
        assert b
        self._keep = buf
        entries = class2_entries(self.ram)
        for a in set(entries) | {PLAYER}:
            assert NATIVE.bridge_actor(b, a, *actor_fields(self.ram, a)) == 0
        arr = (C.c_uint32 * max(1, len(entries)))(*entries)
        assert NATIVE.bridge_class2(b, arr, len(entries)) == 0
        NATIVE.bridge_player(b, PLAYER)
        return b


class Mismatch(AssertionError):
    pass


def observe(ee, entry, calls):
    """Count calls of `entry` without changing what runs: the original
    routine executes in place (only its return address is borrowed to
    record v0)."""
    from test_player_slide_reference import RETURN

    def hook(e):
        ra = e.r[31]
        del e.hooks[entry]
        e.r[31] = RETURN
        try:
            e.run(entry)
        finally:
            e.hooks[entry] = hook
        calls.append((entry, e.r[2] & 0xFFFFFFFF))
        e.r[31] = ra
    ee.hooks[entry] = hook


def compare(w, label, entry, arg, spad0, setup_ram=(), native_setup=None):
    """One call on both sides from the scratchpad spad0; setup_ram is written
    into RAM before both and undone after. Returns (v0, state words)."""
    ee = w.ee
    saved = [(a, bytes(ee.mem[a:a + len(d)])) for a, d in setup_ram]
    for a, d in setup_ram:
        ee.mem[a:a + len(d)] = d
    b = w.bridge()
    try:
        if native_setup:
            native_setup(b)
        st = state_of(spad0)
        if entry == GRID:
            got = NATIVE.bridge_grid(b, C.byref(st))
        else:
            got = NATIVE.bridge_lock(b, {LOCK6440: 0, LOCK6AD0: 1, LOCK7280: 2}[entry], C.byref(st), arg)
        predicted = bytearray(spad0)
        state_into(predicted, st)
        ee.spad = bytearray(spad0)
        log = MV.watch_writes(ee)
        try:
            v0, _ = ee.invoke(entry, (arg,) if entry != GRID else ())
        finally:
            MV.unwatch(ee)
        want = sx32(v0)
        ram_writes = [hex(a) for a, _ in log if a < 0x40000000]
        if ram_writes:
            raise Mismatch((w.beat, label, 'the original wrote RAM', ram_writes[:8]))
        if got != want:
            raise Mismatch((w.beat, label, 'return', got, want))
        orig = state_of(ee.spad)
        for name, _, _, count, _ in STATE_WORDS:
            a, n = getattr(st, name), getattr(orig, name)
            if count > 1: a, n = list(a), list(n)
            if a != n:
                raise Mismatch((w.beat, label, 'field', name, a, n))
        if bytes(predicted) != bytes(ee.spad):
            diff = [hex(0x70000000 + i) for i in range(0x4000) if predicted[i] != ee.spad[i]]
            raise Mismatch((w.beat, label, 'scratchpad differs outside the modelled words', diff[:12]))
        return want, orig
    finally:
        NATIVE.bridge_free(b)
        for a, d in saved:
            ee.mem[a:a + len(d)] = d
        ee.spad = bytearray(w.spad)


def spad_with(w, start, end, words=None):
    spad = bytearray(w.spad)
    struct.pack_into('<4I', spad, 0x3190, *[fbits(x) for x in start], 0)
    struct.pack_into('<4I', spad, 0x31A0, *[fbits(x) for x in end], 0)
    for at, (value, size) in (words or {}).items():
        spad[at:at + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'little')
    return spad


# ---------------------------------------------------------------------------
# 0019CB60 cases

def trace_rows(beat):
    import json
    return json.loads((ROUTE / beat / 'trace.json').read_text())['rows']


def grid_cases(rng):
    cases = []
    for beat in GRID_BEATS:
        rows = trace_rows(beat)
        for k in range(120):
            r = rng.choice(rows)
            hip = [f32r(v) for v in r['hip']]
            y = f32r(hip[1] + rng.choice((0.0, 0.0, 0.5, -0.5, 2.0, -2.0, 6.0)))
            t = rng.uniform(0, 2 * math.pi)
            length = rng.choice((0.3, 1.0, 3.0, 8.0, 20.0, 40.0))
            s = [f32r(hip[0] + rng.uniform(-1, 1)), y, f32r(hip[2] + rng.uniform(-1, 1))]
            axis = rng.random()
            if axis < 0.1:
                e = [s[0], y, f32r(s[2] + length * rng.choice((1, -1)))]      # start.x == end.x
            elif axis < 0.2:
                e = [f32r(s[0] + length * rng.choice((1, -1))), y, s[2]]      # start.z == end.z
            else:
                e = [f32r(s[0] + math.cos(t) * length), y, f32r(s[2] + math.sin(t) * length)]
            if rng.random() < 0.15:
                e[1] = f32r(y + rng.uniform(-3.0, 3.0))                      # sloped (the callers pass level segments)
            qc = rng.choice((0, 0, 0, 2, 4, -1, 1))
            persist = rng.random() < 0.3
            cases.append(('grid', beat, (s, e, qc, persist, rng.randrange(1 << 30), 'row %d #%d' % (r['counter'], k))))
        ram = (ROUTE / beat / 'eeMemory.bin').read_bytes()
        spad = (ROUTE / beat / 'scratchpad.bin').read_bytes()
        for k, (s, e, label) in enumerate(vertex_segments(ram, spad, rows, rng, 40)):
            cases.append(('grid', beat, (s, e, rng.choice((0, 0, 2, -1)), False, 0, '%s #%d' % (label, k))))
        for d in (0, 1, 4, 5):
            for k, (s, e, label) in enumerate(bound_segments(ram, spad, d, rng, 12)):
                cases.append(('grid', beat, (s, e, 0, False, 0, '%s #%d' % (label, k))))
        for k in range(12):
            r = rng.choice(rows)
            hip = [f32r(v) for v in r['hip']]
            s = [hip[0], f32r(hip[1] + 1.0), hip[2]]
            t = rng.uniform(0, 2 * math.pi)
            e = [f32r(s[0] + math.cos(t) * 25.0), s[1], f32r(s[2] + math.sin(t) * 25.0)]
            cases.append(('gate', beat, (s, e, 'gate row %d #%d' % (r['counter'], k))))
    return cases


def vertex_segments(ram, spad, rows, rng, count):
    """Segments that end exactly on (or one ulp beside) the x or z of a
    nearby node's extreme vertex (node +0x00..+0x0A indexes the grid vertex
    pool *0x700031FC): the query ranks then equal the node's rank bounds
    +0x0C..+0x16, the edge of 0019CB60's four bound tests."""
    base, n = u32(spad, 0x3208), u32(spad, 0x320C)
    pool = u32(spad, 0x31FC)
    vert = lambda i: struct.unpack_from('<3f', ram, pool + 12 * i)
    out = []
    tries = 0
    while len(out) < count and tries < count * 200:
        tries += 1
        r = rng.choice(rows)
        hip = r['hip']
        node = rng.randrange(n)
        at = base + 0x40 * node
        if ram[at + 0x1A] >= 0x5A:
            continue
        idx = struct.unpack_from('<6h', ram, at)
        vs = [vert(i) for i in idx]
        c = [sum(v[j] for v in vs) / 6 for j in range(3)]
        if abs(c[0] - hip[0]) > 30 or abs(c[2] - hip[2]) > 30:
            continue
        normal = struct.unpack_from('<3f', ram, at + 0x24)
        h = math.hypot(normal[0], normal[2])
        if h < 0.2:
            continue
        d = rng.choice((0, 1, 4, 5))
        v = [f32r(x) for x in vs[d]]
        a = rng.choice((0.5, 2.0, 7.0))
        roll = rng.random()
        outward = {0: (-1.0, 0.0), 1: (1.0, 0.0), 4: (0.0, -1.0), 5: (0.0, 1.0)}[d]
        into = [-normal[0] / h + outward[0] * 0.7, -normal[2] / h + outward[1] * 0.7]
        if roll < 0.35 and into[0] * outward[0] + into[1] * outward[1] > 0.05:
            # starts one ulp beyond the node's extreme vertex on the bound's
            # axis and runs into the face's back, away from the bound: the
            # crossing lies within the edge epsilon, just outside the bound
            axis = 0 if d < 2 else 2
            s = list(v)
            s[axis] = bitsf(fbits(v[axis]) + (1 if (outward[0] + outward[1]) * v[axis] >= 0 else -1))
            m = math.hypot(*into)
            e = [f32r(s[0] + into[0] / m * a), s[1], f32r(s[2] + into[1] / m * a)]
            out.append((s, e, 'vertex bound node %d dir %d' % (node, d)))
            continue
        if roll < 0.45:
            # starts on the extreme vertex (+- 1 ulp on its axis) and leaves
            # in any level direction: the crossing lies within the edge
            # epsilon beyond the node's bound
            s = list(v)
            axis = 0 if d < 2 else 2
            s[axis] = bitsf(fbits(v[axis]) + rng.choice((0, 1, 1, -1)))
            ang = rng.uniform(0, 2 * math.pi)
            e = [f32r(s[0] + math.cos(ang) * a), s[1], f32r(s[2] + math.sin(ang) * a)]
            out.append((s, e, 'vertex node %d dir %d' % (node, d)))
            continue
        if roll < 0.75:
            # ends exactly on the extreme vertex, from the front: the
            # segment's extreme on that axis touches the node's bound
            e = list(v)
            s = [f32r(v[0] + normal[0] / h * a), v[1], f32r(v[2] + normal[2] / h * a)]
        else:
            y = f32r(v[1] + rng.uniform(-0.2, 0.2))
            s = [f32r(v[0] + normal[0] / h * a), y, f32r(v[2] + normal[2] / h * a)]
            e = [f32r(v[0] - normal[0] / h * 0.5), y, f32r(v[2] - normal[2] / h * 0.5)]
            axis = 0 if d < 2 else 2
            e[axis] = bitsf(fbits(v[axis]) + rng.choice((0, 0, 1, -1)))
        if rng.random() < 0.2:
            s, e = e, s
        out.append((s, e, 'vertex node %d dir %d' % (node, d)))
    return out


def bound_segments(ram, spad, d, rng, count):
    """Level segments that start one ulp beyond a node's extreme vertex d
    (0/1 the x minimum/maximum, 4/5 the z minimum/maximum) on that axis and
    run into the face's back away from the bound, from any node of the
    grid: 0019ED80 accepts such a crossing within its edge epsilon, and the
    node's rank bound equals the start's rank (the bound tests' equality)."""
    base, n = u32(spad, 0x3208), u32(spad, 0x320C)
    pool = u32(spad, 0x31FC)
    o = {0: (-1.0, 0.0), 1: (1.0, 0.0), 4: (0.0, -1.0), 5: (0.0, 1.0)}[d]
    axis = 0 if d < 2 else 2
    out, tries = [], 0
    while len(out) < count and tries < count * 400:
        tries += 1
        node = rng.randrange(n)
        at = base + 0x40 * node
        if ram[at + 0x1A] >= 0x5A:
            continue
        normal = struct.unpack_from('<3f', ram, at + 0x24)
        h = math.hypot(normal[0], normal[2])
        if h < 0.2:
            continue
        into = [-normal[0] / h + o[0] * 0.7, -normal[2] / h + o[1] * 0.7]
        if into[0] * o[0] + into[1] * o[1] <= 0.05:
            continue
        v = [f32r(x) for x in struct.unpack_from('<3f', ram, pool + 12 * struct.unpack_from('<h', ram, at + 2 * d)[0])]
        s = list(v)
        s[axis] = bitsf(fbits(v[axis]) + (1 if (o[0] + o[1]) * v[axis] >= 0 else -1))
        m = math.hypot(*into)
        length = rng.choice((0.5, 2.0, 6.0))
        e = [f32r(s[0] + into[0] / m * length), s[1], f32r(s[2] + into[1] / m * length)]
        out.append((s, e, 'bound node %d dir %d' % (node, d)))
    return out


def grid_words(w, qc, persist, seed):
    words = {0x324E: (qc, 2)}
    if persist:
        rng = random.Random(seed)
        for i in range(6):
            words[0x3240 + 2 * i] = (rng.randrange(0, w.node_count), 2)
        words[0x3B86] = (rng.randrange(-5, 3000), 2)
        words[0x3B88] = (rng.randrange(0, 0x100), 2)
        words[0x31B0] = (fbits(rng.uniform(-300, 300)), 4)
        words[0x31D0] = (rng.choice((0, CELL_RECORD, w.node_base + 0x40 * rng.randrange(w.node_count))), 4)
    return words


def run_grid(w, s, e, qc, persist, seed, label):
    spad = spad_with(w, s, e, grid_words(w, qc, persist, seed))
    return compare(w, label, GRID, 0, spad)


GATE_ATTRS = (0x00, 0x50, 0x51, 0x52, 0x53, 0x54, 0x59, 0x5A, 0x78)


def run_gate(w, s, e, label):
    """The attribute gate on nodes the segment really crosses: find the
    node the unpatched pass ends on, then give it every gate value under
    every query class (node +0x1A patched in RAM and in the EMCL)."""
    spad = spad_with(w, s, e, {0x324E: (0, 2)})
    v0, st = compare(w, label + ' plain', GRID, 0, spad)
    if v0 != 0:
        return [v0]
    node = (st.record - w.node_base) // 0x40
    at = st.record + 0x1A
    out = [v0]
    for attr in GATE_ATTRS:
        for qc in (0, 2, -1, 1):
            old = NATIVE.bridge_set_attr(node, attr)
            try:
                spad = spad_with(w, s, e, {0x324E: (qc, 2)})
                v, _ = compare(w, '%s attr %#x qc %d' % (label, attr, qc), GRID, 0, spad,
                               setup_ram=[(at, bytes([attr]))])
                out.append(v)
            finally:
                NATIVE.bridge_set_attr(node, old)
    return out


# ---------------------------------------------------------------------------
# Lock cases over the real chains

def aimed_segments(polys, rng, count, horizontal):
    out = []
    for _ in range(count):
        verts, normal, _ = rng.choice(polys)
        c = [sum(v[j] for v in verts) / len(verts) for j in range(3)]
        if rng.random() < 0.3:     # toward a vertex or an edge midpoint (edge epsilon region)
            k = rng.randrange(len(verts))
            a, b = verts[k], verts[(k + 1) % len(verts)]
            f = rng.choice((0.0, 0.5, 1.0))
            c = [a[j] + (b[j] - a[j]) * f for j in range(3)]
        if horizontal:
            h = math.hypot(normal[0], normal[2]) or 1.0
            d = [normal[0] / h, 0.0, normal[2] / h]
        else:
            d = [normal[j] + rng.uniform(-0.4, 0.4) for j in range(3)]
        a, b = rng.uniform(0.05, 3.0), rng.uniform(0.05, 3.0)
        if rng.random() < 0.15: b = -rng.uniform(0.0, 0.3)     # the end short of the plane
        s = [f32r(c[j] + d[j] * a) for j in range(3)]
        e = [f32r(c[j] - d[j] * b) for j in range(3)]
        if rng.random() < 0.2: s, e = e, s                    # from behind
        out.append((s, e))
    return out


LOCK_ARGS = (0x40, 0x40, 0x40, 0x10, 0x20, 0x30, 0x70, 0x60, 0)


def lock_cases(rng):
    cases = []
    for beat in ROGER_BEATS:
        ram = (ROUTE / beat / 'eeMemory.bin').read_bytes()
        roger = class2_entries(ram)
        assert roger, (beat, 'no class-2 entity to lock on')
        polys = [p for a in roger for p in chain_polys(ram, a)]
        for k, (s, e) in enumerate(aimed_segments(polys, rng, 60, False) + aimed_segments(polys, rng, 30, True)):
            entry = rng.choice((LOCK6440, LOCK6440, LOCK6AD0))
            arg = rng.choice(LOCK_ARGS) if entry == LOCK6440 else rng.choice((0x40, 0x40, 0x70, 0x10))
            if entry == LOCK6440 and rng.random() < 0.2: arg = rng.randrange(0x10000)   # 0019A570: id & 0xFFFF
            cases.append(('lock', beat, (entry, arg, s, e, 'roger #%d' % k)))
    for beat in PLAYER_BEATS:
        ram = (ROUTE / beat / 'eeMemory.bin').read_bytes()
        polys = chain_polys(ram, PLAYER)
        for k, (s, e) in enumerate(aimed_segments(polys, rng, 40, rng.random() < 0.5)):
            cases.append(('lock', beat, (LOCK7280, rng.choice((0x40, 0x40, 0x20, 0x60, 0x10)), s, e, 'player #%d' % k)))
    return cases


def run_lock(w, entry, arg, s, e, label, rng_seed=0):
    rng = random.Random(rng_seed)
    words = {0x30CA: (rng.choice((0, 0x1000, 0x2034)), 2), 0x30CC: (rng.randrange(1 << 32), 4),
             0x30D0: (rng.randrange(1 << 32), 4), 0x31D0: (rng.choice((0, CELL_RECORD)), 4),
             0x31D4: (0, 4)}
    spad = spad_with(w, s, e, words)
    return compare(w, label, entry, arg, spad)


# ---------------------------------------------------------------------------
# Synthetic chains

def unit(v):
    m = math.sqrt(sum(x * x for x in v)) or 1.0
    return [x / m for x in v]


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def polygon(centre, normal, radius, n, spin=0.0):
    """A convex n-gon around `centre` in the plane of `normal`: vertices and
    outward edge normals (host floats)."""
    normal = unit(normal)
    helper = [0.0, 1.0, 0.0] if abs(normal[1]) < 0.9 else [1.0, 0.0, 0.0]
    u = unit(cross(helper, normal))
    v = cross(normal, u)
    verts = []
    for k in range(n):
        t = spin + 2 * math.pi * k / n
        verts.append([centre[j] + radius * (math.cos(t) * u[j] + math.sin(t) * v[j]) for j in range(3)])
    edges = []
    for k in range(n):
        a, b = verts[k], verts[(k + 1) % n]
        edges.append(unit(cross([b[j] - a[j] for j in range(3)], normal)))
    return verts, edges


def record_bytes(masks, slot, normal, verts, edges, n=None, pad=0):
    n = len(verts) if n is None else n
    body = bytearray(0x18 + 24 * len(verts))
    body[0:4] = bytes(masks) + bytes([slot])
    struct.pack_into('<Hh', body, 4, len(body) + pad, n)
    struct.pack_into('<3f', body, 8, *normal)
    for k, vtx in enumerate(verts):
        struct.pack_into('<3f', body, 0x18 + 12 * k, *vtx)
        struct.pack_into('<3f', body, 0x18 + 12 * len(verts) + 12 * k, *edges[k])
    return bytes(body) + bytes(pad)


def chain_bytes(records, count=None):
    body = b''.join(records)
    return struct.pack('<I', len(records) if count is None else count) + body


def matrix_bytes(rows):
    return b''.join(struct.pack('<4f', *r) for r in rows)


def rotation(rng, translate):
    ax, ay = rng.uniform(-math.pi, math.pi), rng.uniform(-0.6, 0.6)
    cy, sy, cx, sx = math.cos(ax), math.sin(ax), math.cos(ay), math.sin(ay)
    # rows: images of the x, y and z axes, then the translation (v * M)
    r0 = [cy, 0.0, -sy, 0.0]
    r1 = [sy * sx, cx, cy * sx, 0.0]
    r2 = [sy * cx, -sx, cy * cx, 0.0]
    return [r0, r1, r2, list(translate) + [1.0]]


IDENTITY = [[1.0, 0, 0, 0], [0, 1.0, 0, 0], [0, 0, 1.0, 0], [0, 0, 0, 1.0]]


def inverse_rows(rows):
    """Local coordinates of a world point under v * M (rotation rows are
    orthonormal)."""
    r, t = [row[:3] for row in rows[:3]], rows[3][:3]

    def local(p):
        d = [p[j] - t[j] for j in range(3)]
        return [sum(d[j] * r[i][j] for j in range(3)) for i in range(3)]

    def local_dir(v):
        return [sum(v[j] * r[i][j] for j in range(3)) for i in range(3)]
    return local, local_dir


class Synthetic:
    """Entities, chains and bone matrices written into SYN (both sides read
    the same RAM); returns the RAM patch and the class-2 entries."""

    def __init__(self):
        self.patch, self.cursor = [], SYN + 0x100

    def alloc(self, data, align=16):
        self.cursor = (self.cursor + align - 1) & ~(align - 1)
        at = self.cursor
        self.patch.append((at, bytes(data)))
        self.cursor += len(data)
        assert self.cursor < SYN + SYN_SIZE
        return at

    def record(self, status, cls, masks, chain, matrices, h52=0):
        """An entity record (bytes) whose matrices and chain are allocated
        after the entity itself (a chain last, so a cut RAM view can end
        inside it)."""
        mats = [self.alloc(bytes(0x90) + matrix_bytes(m)) for m in matrices]
        rec = bytearray(0x2F0)
        rec[0], rec[2], rec[9] = status, cls, len(matrices)
        struct.pack_into('<H', rec, 0x52, h52)
        struct.pack_into('<I', rec, 0x58, self.alloc(chain) if chain is not None else 0)
        struct.pack_into('<I', rec, 0x5C, masks[0] | masks[1] << 8 | masks[2] << 16)
        for m, p in enumerate(mats):
            struct.pack_into('<I', rec, 0x110 + 4 * m, p)
        return rec

    def entity(self, status, cls, masks, chain, matrices, h52=0):
        at = self.alloc(bytes(0x2F0))
        index = len(self.patch) - 1
        self.patch[index] = (at, bytes(self.record(status, cls, masks, chain, matrices, h52)))
        return at

    def list_patch(self, entries):
        arr = self.alloc(b''.join(struct.pack('<I', a) for a in entries) or bytes(4))
        return [(CLASS2_LIST, struct.pack('<I', arr)), (CLASS2_COUNT, struct.pack('<h', len(entries)))]


def synthetic_case(rng, k):
    """A synthetic world and a segment through it: 1..3 class-2 entities
    (or the player's chain for 001A7280)."""
    entry = rng.choice((LOCK6440, LOCK6440, LOCK6AD0, LOCK7280))
    centre = [rng.uniform(-50, 50), rng.uniform(-10, 10), rng.uniform(-50, 50)]
    t = rng.uniform(0, 2 * math.pi)
    horizontal = rng.random() < 0.5
    dirv = unit([math.cos(t), 0.0 if horizontal else rng.uniform(-0.8, 0.8), math.sin(t)])
    length = rng.uniform(2.0, 12.0)
    s = [centre[j] - dirv[j] * length * 0.5 for j in range(3)]
    e = [centre[j] + dirv[j] * length * 0.5 for j in range(3)]
    ents = synthetic_entities(rng, entry, s, e)
    arg = rng.choice((0x40, 0x40, 0x10, 0x20, 0x70, 0x30, 0x50, 0xFFFF, rng.randrange(0x10000)))
    return ('synthetic', 'world', (entry, arg, [f32r(x) for x in s], [f32r(x) for x in e], ents,
                                   'synthetic #%d' % k))


def synthetic_entities(rng, entry, s, e, veto=False, z_bias=False):
    """1..3 class-2 entities (one player chain for 001A7280) whose records
    lie across the segment s -> e, in rotated/translated bone frames; with
    veto, each entity also gets a random +0x52 (0019AD00's veto bit 1); with
    z_bias, most entities and records carry mask bit 0 of their z byte (the
    one 0019AD00's argument 0x40 tests), so more records are tested."""
    dirv = unit([e[j] - s[j] for j in range(3)])
    centre = [(s[j] + e[j]) / 2 for j in range(3)]
    ents = []
    n_ent = 1 if entry == LOCK7280 else rng.choice((1, 1, 2, 3))
    for x in range(n_ent):
        status = rng.choice((1, 1, 1, 3, 0, 2))
        cls = rng.choice((0x0A, 0xAA, 0x04, 0x02, 0x00, 0x22, 0x0B))
        masks = [rng.choice((0, 1, 1, 3, 0xFF)) for _ in range(3)]
        if z_bias and rng.random() < 0.85: masks[2] |= 1
        nslots = rng.choice((1, 2, 3))
        mats = [rotation(rng, [centre[j] + rng.uniform(-1, 1) for j in range(3)]) if rng.random() < 0.7
                else [r[:] for r in IDENTITY] for _ in range(nslots)]
        records = []
        nrec = rng.choice((1, 2, 3, 4, 6))
        first_skip = rng.random() < 0.2
        for r in range(nrec):
            slot = rng.randrange(nslots)
            local, local_dir = inverse_rows(mats[slot])
            f = rng.uniform(0.15, 0.85)
            p = [s[j] + (e[j] - s[j]) * f + rng.uniform(-0.3, 0.3) for j in range(3)]
            nrm = unit([-dirv[j] + rng.uniform(-0.7, 0.7) for j in range(3)])
            if rng.random() < 0.2: nrm = [-x for x in nrm]            # back facing
            nv = rng.choice((3, 4, 5))
            verts, edges = polygon(local(p), local_dir(nrm), rng.uniform(0.2, 2.5), nv, rng.uniform(0, 6))
            rec_masks = [rng.choice((0, 1, 2, 3, 0x80)) for _ in range(3)]
            if z_bias and rng.random() < 0.85: rec_masks[2] |= 1
            count = nv
            if rng.random() < 0.1: count = (-1, 0)[r % 2]              # -1 masks the record, 0 skips the edge test
            records.append(record_bytes(rec_masks, slot, local_dir(nrm), verts, edges, n=count))
        if first_skip:
            v, ed = polygon([0, 0, 0], [0, 1, 0], 1.0, 3)
            records.insert(0, record_bytes([0xFF, 0xFF, 0xFF], 0, [0, 1, 0], v, ed, n=-2))
        chain = chain_bytes(records) if rng.random() < 0.95 else None
        ents.append((status, cls, masks, chain, mats) + ((rng.choice((0, 1, 2, 3)),) if veto else ()))
    return ents


def exact_worlds():
    """Threshold cases on identity matrices: the facing epsilon (dir.n exactly
    -1e-5 and neighbours), the edge epsilon, strict interval ends, and the
    001A6AD0 surface-class ratios exactly 3.0 and 0.49029058 (normals from
    the move test's search of the measured model), both signs of ny."""
    out = []
    square = [[0.0, -3.0, -3.0], [0.0, 3.0, -3.0], [0.0, 3.0, 3.0], [0.0, -3.0, 3.0]]
    edges = [[0.0, 0.0, -1.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0], [0.0, -1.0, 0.0]]
    plane_x = record_bytes([1, 1, 1], 0, [1.0, 0.0, 0.0], square, edges)
    for entry in (LOCK6440, LOCK6AD0, LOCK7280):
        ent = [(1, 0x0A, [1, 1, 1], chain_bytes([plane_x]), [IDENTITY])]
        for delta in (-2, -1, 0, 1, 2):
            dx = bitsf(fbits(-1e-5) + delta)
            for z in (-1.0, 0.0):
                out.append((entry, 0x40, [0.0, 0.5, z], [dx, 0.5, z], ent, 'facing %+d' % delta))
        for delta in (-2, -1, 0, 1, 2):
            z = bitsf(fbits(3.0) + delta)
            out.append((entry, 0x40, [1.0, 0.5, z], [-1.0, 0.5, z], ent, 'edge z %+d' % delta))
            y = bitsf(fbits(3.0 + 1e-5) + delta)
            out.append((entry, 0x40, [1.0, y, 0.5], [-1.0, y, 0.5], ent, 'edge y %+d' % delta))
        for end in (0.0, bitsf(1), -1e-3, bitsf(0x80000001)):
            out.append((entry, 0x40, [1.0, 0.5, -1.0], [end, 0.5, -1.0], ent, 'interval end %r' % end))
            out.append((entry, 0x40, [end, 0.5, -1.0], [1.0, 0.5, -1.0], ent, 'interval start %r' % end))
        out.append((entry, 0x40, [1.0, 0.5, -1.0], [-1.0, 0.5, -1.0], ent, 'through'))
        # The same ends moving +x, so the x clause whose qa lies below the
        # hit decides (its strict end: qb.x == hit.x must not accept). A
        # plane facing -x (the facing test passes for every lock), and the
        # +x plane crossed from behind with the facing test off (argument
        # 0x30 has no z bit; 001A7280 never tests facing; 001A6AD0 always
        # does, so there the back crossing must miss).
        plane_xn = record_bytes([1, 1, 1], 0, [-1.0, 0.0, 0.0], square, edges)
        ent_n = [(1, 0x0A, [1, 1, 1], chain_bytes([plane_xn]), [IDENTITY])]
        for world_ents, arg, side in ((ent_n, 0x40, 'n-x'), (ent, 0x30, 'n+x back')):
            for end in (0.0, -0.0, bitsf(1), bitsf(0x80000001), 1e-3, -1e-3):
                out.append((entry, arg, [-1.0, 0.5, -1.0], [end, 0.5, -1.0], world_ents,
                            'interval end +x %r %s' % (end, side)))
                out.append((entry, arg, [end, 0.5, -1.0], [1.0, 0.5, -1.0], world_ents,
                            'interval start +x %r %s' % (end, side)))
            out.append((entry, arg, [-1.0, 0.5, -1.0], [1.0, 0.5, -1.0], world_ents, 'through +x %s' % side))
        # the edge epsilon exactly: a square spanning y, z in [-3, 0] (edges at
        # y = 0 and z = 0), crossings 1e-5 +- ulps outside them
        corner = [[0.0, -3.0, -3.0], [0.0, 0.0, -3.0], [0.0, 0.0, 0.0], [0.0, -3.0, 0.0]]
        cedges = [[0.0, 0.0, -1.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0], [0.0, -1.0, 0.0]]
        cent = [(1, 0x0A, [1, 1, 1], chain_bytes([record_bytes([1, 1, 1], 0, [1.0, 0.0, 0.0], corner, cedges)]),
                 [IDENTITY])]
        for delta in (-2, -1, 0, 1, 2):
            q = bitsf(fbits(1e-5) + delta)
            out.append((entry, 0x40, [1.0, q, -1.0], [-1.0, q, -1.0], cent, 'edge eps y %+d' % delta))
            out.append((entry, 0x40, [1.0, -1.0, q], [-1.0, -1.0, q], cent, 'edge eps z %+d' % delta))
        # the facing epsilon with a strict crossing: qa.x = 2^-17 and
        # qb.x - qa.x exactly -1e-5 +- ulps (both exact in single precision)
        from fractions import Fraction
        qa = 2.0 ** -17
        for delta in (-2, -1, 0, 1, 2):
            dx = bitsf(fbits(-1e-5) + delta)
            qb = f32r(qa + dx)
            if Fraction(qb) - Fraction(qa) != Fraction(dx):
                continue
            out.append((entry, 0x40, [qa, -1.0, -1.0], [qb, -1.0, -1.0], cent, 'facing strict %+d' % delta))
        out.append((entry, 0x40, [1.0, 0.5, -1.0], [1.0, 0.5, -1.0], ent, 'zero length'))
    for nxw, ny in ((0x3DCB3A30, 0.171875), (0x3EC23A67, 0.265625)):
        for dn in (-1, 0, 1):
            for sy in (1.0, -1.0):
                nx = bitsf(nxw + dn)
                n = [nx, ny * sy, 0.0]
                v = [n[1], -n[0], 0.0]
                pts = [[v[0] * a, v[1] * a, b] for a, b in ((-3, -3), (3, -3), (3, 3), (-3, 3))]
                ed = [[0, 0, -1], [v[0], v[1], 0], [0, 0, 1], [-v[0], -v[1], 0]]
                rec = record_bytes([1, 1, 1], 0, n, pts, ed)
                ent = [(1, 0x0A, [1, 1, 1], chain_bytes([rec]), [IDENTITY])]
                out.append((LOCK6AD0, 0x40, [2.0, 0.0, 0.5], [-2.0, 0.0, 0.5], ent,
                            'class nx %+d ny %+g' % (dn, ny * sy)))
    return [('synthetic', 'world', case) for case in out]


# ---------------------------------------------------------------------------
# The y and z arms of the interval test
#
# The locks accept a record only when the hit lies strictly between qa and qb
# on some axis, tried x, then y, then z. A segment that moves along y alone
# (x and z identical at both ends, so the hit's x and z equal qa's exactly)
# can only be accepted through a y clause, one along z alone only through a
# z clause; the direction picks the clause (qa above the hit: the first,
# below: the second).

def axis_square(axis, c0, half=3.0):
    """A square in the plane coordinate[axis] == c0: its vertices and the
    outward edge normals, vertex k paired with edge k (the other two axes in
    order, as the x-plane square above)."""
    b1, b2 = [j for j in range(3) if j != axis]

    def pt(u, v):
        p = [0.0, 0.0, 0.0]
        p[axis], p[b1], p[b2] = c0, u, v
        return p

    def unit_axis(j, s):
        p = [0.0, 0.0, 0.0]
        p[j] = s
        return p
    verts = [pt(-half, -half), pt(half, -half), pt(half, half), pt(-half, half)]
    edges = [unit_axis(b2, -1.0), unit_axis(b1, 1.0), unit_axis(b2, 1.0), unit_axis(b1, -1.0)]
    return verts, edges


def spin_about(axis, angle, translate):
    """A bone matrix (v * M rows) turning about `axis` and translating: the
    plane of an axis square stays perpendicular to that axis."""
    c, s = math.cos(angle), math.sin(angle)
    rows = [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0], [0.0, 0.0, 1.0, 0.0]]
    b1, b2 = [j for j in range(3) if j != axis]
    rows[b1][b1], rows[b1][b2] = c, s
    rows[b2][b1], rows[b2][b2] = -s, c
    return rows + [list(translate) + [1.0]]


def axis_worlds():
    """Exact y- and z-plane records on identity and spun/translated frames:
    segments along the plane's axis alone in both directions (normal on the
    facing side and on the back, the facing test on (0x40) and off (0x30)),
    segments that start or end exactly on the plane or ulps from it, and
    records with vertex count 0 (no edge test: any strict crossing accepts)
    beside records with count -1 (masked)."""
    out = []
    for axis, name in ((1, 'y'), (2, 'z')):
        on = [0.5, -1.0, 0.75]
        on[axis] = 0.0
        frames = [('identity', [r[:] for r in IDENTITY], [0.0, 0.0, 0.0])]
        for angle, move in ((0.7, [3.0, 2.0, -5.0]), (-2.2, [-40.0, 12.5, 7.25])):
            shift = list(move)
            shift[axis] = 0.0                                   # the plane stays at axis == 0
            frames.append(('spun %+g' % angle, spin_about(axis, angle, shift), shift))
        for entry in (LOCK6440, LOCK6AD0, LOCK7280):
            for frame, mat, shift in frames:
                local, local_dir = inverse_rows(mat)
                point = [on[j] + shift[j] for j in range(3)]
                for sign in (1.0, -1.0):
                    n = [0.0, 0.0, 0.0]
                    n[axis] = sign
                    verts, edges = axis_square(axis, 0.0)
                    lverts = [local([v[j] + shift[j] for j in range(3)]) for v in verts]
                    ledges = [local_dir(ed) for ed in edges]
                    ent = [(1, 0x0A, [1, 1, 1], chain_bytes([record_bytes([1, 1, 1], 0, local_dir(n), lverts, ledges)]),
                            [mat])]
                    for down in (True, False):
                        s, e = list(point), list(point)
                        s[axis], e[axis] = (1.0, -1.0) if down else (-1.0, 1.0)
                        for arg in (0x40, 0x30):
                            out.append((entry, arg, [f32r(x) for x in s], [f32r(x) for x in e], ent,
                                        'axis %s %s %s n%+d %#x' % (name, frame, 'down' if down else 'up', sign, arg)))
                    if frame != 'identity':
                        continue
                    # strict ends: toward the plane from the normal's side
                    # (facing), the end on the plane or ulps around it; and
                    # from the plane (or ulps off it) away through the back
                    for end in (0.0, bitsf(1), 1e-3, -1e-3, bitsf(0x80000001), bitsf(fbits(1.0) - 1)):
                        s, e = list(point), list(point)
                        s[axis], e[axis] = sign, f32r(end * sign)
                        out.append((entry, 0x40, s, e, ent, 'axis %s end %r n%+d' % (name, end, sign)))
                        s, e = list(point), list(point)
                        s[axis], e[axis] = f32r(end * sign), -sign
                        out.append((entry, 0x40, s, e, ent, 'axis %s start %r n%+d' % (name, end, sign)))
    # Vertex counts 0 and -1 (x planes, so the x clauses decide): the
    # crossing lies outside the square, so only a count-0 record (no edge
    # loop) accepts it; the count -1 record beside it is masked.
    square = [[0.0, -3.0, -3.0], [0.0, 3.0, -3.0], [0.0, 3.0, 3.0], [0.0, -3.0, 3.0]]
    edges = [[0.0, 0.0, -1.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0], [0.0, -1.0, 0.0]]

    def at(x):
        return [[x, v[1], v[2]] for v in square]
    for entry in (LOCK6440, LOCK6AD0, LOCK7280):
        for label, counts in (('0 then -1', (0, -1)), ('-1 then 0', (-1, 0)), ('0 alone', (0,)),
                              ('-1 alone', (-1,)), ('4 then 0', (4, 0))):
            recs = [record_bytes([1, 1, 1], 0, [1.0, 0.0, 0.0], at(0.5 - 0.25 * k), edges, n=c)
                    for k, c in enumerate(counts)]
            ent = [(1, 0x0A, [1, 1, 1], chain_bytes(recs), [IDENTITY])]
            for y in (10.0, 0.5):                               # outside / inside the square
                out.append((entry, 0x40, [1.0, y, -1.0], [-1.0, y, -1.0], ent, 'count %s y %g' % (label, y)))
            out.append((entry, 0x40, [1.0, 10.0, -1.0], [0.75, 10.0, -1.0], ent, 'count %s short' % label))
    return [('synthetic', 'world', case) for case in out]


def synthetic_axis_case(rng, k):
    """A synthetic world (synthetic_entities: rotated frames, several
    records) crossed by a segment along y or z alone, or along y with z
    drifting (y decides first)."""
    entry = rng.choice((LOCK6440, LOCK6440, LOCK6AD0, LOCK7280))
    axis = rng.choice((1, 2))
    centre = [rng.uniform(-50, 50), rng.uniform(-10, 10), rng.uniform(-50, 50)]
    half = rng.uniform(1.0, 6.0) * rng.choice((1.0, -1.0))
    s, e = list(centre), list(centre)
    s[axis], e[axis] = centre[axis] - half, centre[axis] + half
    if axis == 1 and rng.random() < 0.2:
        s[2], e[2] = centre[2] - rng.uniform(-1, 1), centre[2] + rng.uniform(-1, 1)
    s, e = [f32r(x) for x in s], [f32r(x) for x in e]
    ents = synthetic_entities(rng, entry, s, e)
    arg = rng.choice((0x40, 0x40, 0x70, 0x30, 0x20, 0x10))
    return ('synthetic', 'world', (entry, arg, s, e, ents, 'synthetic axis %s #%d' % ('xyz'[axis], k)))


def aimed_axis_segments(polys, rng, count, axis):
    """Segments along `axis` alone through real records (their centre, a
    vertex or an edge midpoint), toward the facing side mostly, from both
    sides, sometimes ending short of the plane."""
    polys = [p for p in polys if abs(p[1][axis]) > 0.15 * math.sqrt(sum(x * x for x in p[1]))]
    out = []
    for _ in range(count if polys else 0):
        verts, normal, _ = rng.choice(polys)
        c = [sum(v[j] for v in verts) / len(verts) for j in range(3)]
        if rng.random() < 0.3:
            k = rng.randrange(len(verts))
            a, b = verts[k], verts[(k + 1) % len(verts)]
            f = rng.choice((0.0, 0.5, 1.0))
            c = [a[j] + (b[j] - a[j]) * f for j in range(3)]
        toward = -1.0 if normal[axis] > 0 else 1.0              # dir . n < 0: the facing side
        if rng.random() < 0.2: toward = -toward
        a, b = rng.uniform(0.05, 3.0), rng.uniform(0.05, 3.0)
        if rng.random() < 0.15: b = -rng.uniform(0.0, 0.3)
        s, e = [f32r(x) for x in c], [f32r(x) for x in c]
        s[axis], e[axis] = f32r(c[axis] - toward * a), f32r(c[axis] + toward * b)
        out.append((s, e))
    return out


def axis_lock_cases(rng):
    """Real chains: Roger's (001A6440 / 001A6AD0) and the player's
    (001A7280), crossed along y alone and along z alone."""
    cases = []
    for beat in ROGER_BEATS:
        ram = (ROUTE / beat / 'eeMemory.bin').read_bytes()
        polys = [p for a in class2_entries(ram) for p in chain_polys(ram, a)]
        for axis in (1, 2):
            for k, (s, e) in enumerate(aimed_axis_segments(polys, rng, 40, axis)):
                entry = rng.choice((LOCK6440, LOCK6440, LOCK6AD0))
                arg = rng.choice((0x40, 0x40, 0x70, 0x30)) if entry == LOCK6440 else rng.choice((0x40, 0x70, 0x10))
                cases.append(('lock', beat, (entry, arg, s, e, 'roger axis %s #%d' % ('xyz'[axis], k))))
    for beat in PLAYER_BEATS:
        ram = (ROUTE / beat / 'eeMemory.bin').read_bytes()
        polys = chain_polys(ram, PLAYER)
        for axis in (1, 2):
            for k, (s, e) in enumerate(aimed_axis_segments(polys, rng, 25, axis)):
                cases.append(('lock', beat, (LOCK7280, rng.choice((0x40, 0x40, 0x20, 0x60, 0x10)), s, e,
                                             'player axis %s #%d' % ('xyz'[axis], k))))
    return cases


def pure_axis(s, e):
    """The one axis a segment moves along (0 = x, 1 = y, 2 = z), else None."""
    moving = [j for j in range(3) if fbits(s[j]) != fbits(e[j])]
    return moving[0] if len(moving) == 1 else None


def build_world(w, entry, ents):
    syn = Synthetic()
    if entry == LOCK7280:
        # the player's own record: +0x00, +0x09, +0x58..+0x5F and the +0x110 slots
        status, cls, masks, chain, mats = ents[0][:5]
        body = syn.record(status, cls, masks, chain, mats)
        rec = bytearray(w.ee.mem[PLAYER:PLAYER + 0x2F0])
        rec[0], rec[9] = body[0], body[9]
        rec[0x58:0x60] = body[0x58:0x60]
        rec[0x110:0x110 + 4 * len(mats)] = body[0x110:0x110 + 4 * len(mats)]
        syn.patch.append((PLAYER, bytes(rec)))
        return syn.patch
    entries = [syn.entity(*ent) for ent in ents]
    return syn.patch + syn.list_patch(entries)


def run_synthetic(w, entry, arg, s, e, ents, label):
    patch = build_world(w, entry, ents)
    spad = spad_with(w, s, e, {0x31D4: (0, 4)})
    return compare(w, label, entry, arg, spad, setup_ram=patch)


# ---------------------------------------------------------------------------
# Fail-stop (native only)

def failstop_checks(w):
    """Each returns -1: no chain resolver for an entity that needs one, a
    chain cut short, a bone slot outside the matrices, an odd stride, and
    001A7280 without the player view."""
    checked = 0
    square = [[0.0, -3.0, -3.0], [0.0, 3.0, -3.0], [0.0, 3.0, 3.0], [0.0, -3.0, 3.0]]
    edges = [[0.0, 0.0, -1.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0], [0.0, -1.0, 0.0]]
    good = record_bytes([1, 1, 1], 0, [1.0, 0.0, 0.0], square, edges)
    variants = {
        'no resolver': ([(1, 0x0A, [1, 1, 1], chain_bytes([good]), [IDENTITY])], lambda b: NATIVE.bridge_no_chains(b, 1)),
        'slot outside': ([(1, 0x0A, [1, 1, 1], chain_bytes([record_bytes([1, 1, 1], 1, [1.0, 0, 0], square, edges)]),
                           [IDENTITY])], None),
        'odd stride': ([(1, 0x0A, [1, 1, 1], chain_bytes([good[:4] + struct.pack('<H', 0x79) + good[6:], good]),
                         [IDENTITY])], None),
    }
    for name, (ents, setup) in variants.items():
        for entry in (LOCK6440, LOCK6AD0):
            patch = build_world(w, entry, ents)
            saved = [(a, bytes(w.ram[a:a + len(d)])) for a, d in patch]
            for a, d in patch: w.ram[a:a + len(d)] = d
            b = w.bridge()
            try:
                if setup: setup(b)
                st = state_of(spad_with(w, [1.0, 0.5, -1.0], [-1.0, 0.5, -1.0]))
                got = NATIVE.bridge_lock(b, {LOCK6440: 0, LOCK6AD0: 1}[entry], C.byref(st), 0x40)
                assert got == -1, (name, hex(entry), got)
                checked += 1
            finally:
                NATIVE.bridge_free(b)
                for a, d in saved: w.ram[a:a + len(d)] = d
    # a chain cut short: the readable size ends inside the record
    patch = build_world(w, LOCK6440, [(1, 0x0A, [1, 1, 1], chain_bytes([good]), [IDENTITY])])
    saved = [(a, bytes(w.ram[a:a + len(d)])) for a, d in patch]
    for a, d in patch: w.ram[a:a + len(d)] = d
    b = w.bridge()
    try:
        chain = max(a for a, d in patch if len(d) == len(chain_bytes([good])))
        NATIVE.bridge_ram_size(b, chain + 0x20)
        st = state_of(spad_with(w, [1.0, 0.5, -1.0], [-1.0, 0.5, -1.0]))
        assert NATIVE.bridge_lock(b, 0, C.byref(st), 0x40) == -1, 'a chain cut short must fault'
        checked += 1
    finally:
        NATIVE.bridge_free(b)
        for a, d in saved: w.ram[a:a + len(d)] = d
    # 001A7280 without the player view
    b = w.bridge()
    try:
        NATIVE.bridge_player(b, 0)
        st = state_of(spad_with(w, [1.0, 0.5, -1.0], [-1.0, 0.5, -1.0]))
        assert NATIVE.bridge_lock(b, 2, C.byref(st), 0x40) == -1, '001A7280 without the player must fault'
        checked += 1
    finally:
        NATIVE.bridge_free(b)
    return checked


# ---------------------------------------------------------------------------
# Driver

ELF = NATIVE = None
EMCL = None
G_COUNT = [0]
WORLDS = {}


def world(beat):
    if beat not in WORLDS:
        WORLDS[beat] = World(beat)
    return WORLDS[beat]


def run_one(case):
    kind, beat, args = case
    try:
        if kind == 'synthetic':
            entry, arg, s, e, ents, label = args
            w = world(PLAYER_BEATS[0] if beat == 'world' else beat)
            v, st = run_synthetic(w, entry, arg, s, e, ents, label)
            return ('ok', '%x' % entry, v, st.cell_class if entry == LOCK6AD0 and v else None)
        w = world(beat)
        if kind == 'grid':
            v, _ = run_grid(w, *args)
            return ('ok', 'grid', v, None)
        if kind == 'gate':
            vs = run_gate(w, *args)
            return ('ok', 'gate', vs[0], len(vs))
        entry, arg, s, e, label = args
        v, st = run_lock(w, entry, arg, s, e, label, zlib.crc32(label.encode()))
        return ('ok', '%x real' % entry, v, st.cell_class if entry == LOCK6AD0 and v else None)
    except Mismatch as m:
        return ('fail',) + tuple(m.args)
    except AssertionError as a:
        return ('fail', beat, kind, 'assert', repr(a)[:400])


def setup():
    global ELF, NATIVE, EMCL
    ELF = read_elf()
    NATIVE = build_native()
    cp.OUT = OUT
    EMCL, verified, installed = cp.export_emcl()
    assert NATIVE.bridge_grid_load(str(EMCL).encode()) == 0, 'EMCL grid load'
    G_COUNT[0] = struct.unpack_from('<I', (ROUTE / GRID_BEATS[0] / 'scratchpad.bin').read_bytes(), 0x320C)[0]
    MV.ELF = ELF
    MV.CODE_GRAPH = MV.call_graph(ELF, (GRID, LOCK6440, LOCK6AD0, LOCK7280, RANKS, NODE_TEST))
    return verified, installed


def main():
    t0 = time.time()
    missing = [b for b in set(GRID_BEATS + ROGER_BEATS + PLAYER_BEATS) if not (ROUTE / b / 'eeMemory.bin').exists()]
    if missing:
        print('SKIP: missing route captures', sorted(missing))
        return 0
    verified, installed = setup()
    rng = random.Random(0x19CB60)
    grids = grid_cases(rng)
    locks = lock_cases(rng)
    synth = [synthetic_case(rng, k) for k in range(400)]
    exact = exact_worlds()
    # The y / z interval arms and the vertex counts 0 / -1 (their own stream,
    # so the cases above keep theirs).
    rng2 = random.Random(0x1A67B8)
    axis_exact = axis_worlds()
    axis_real = axis_lock_cases(rng2)
    axis_synth = [synthetic_axis_case(rng2, k) for k in range(240)]
    sel_grids = reference_mode.select([c for c in grids if c[0] == 'grid'], 200, 1,
                                      axes=(lambda c: c[1], lambda c: c[2][2], lambda c: c[2][3]),
                                      keep=lambda i, c: c[2][5].startswith('bound') or
                                      (c[2][5].startswith('vertex') and int(c[2][5].split('#')[1]) < 16))
    sel_gates = reference_mode.select([c for c in grids if c[0] == 'gate'], 10, 2, axes=(lambda c: c[1],))
    sel_locks = reference_mode.select(locks, 90, 3, axes=(lambda c: (c[1], c[2][0]), lambda c: c[2][1]))
    sel_synth = reference_mode.select(synth, 120, 4, axes=(lambda c: c[2][0], lambda c: c[2][1]))
    sel_axis_real = reference_mode.select(axis_real, 60, 6, axes=(lambda c: (c[1], c[2][0], c[2][4].split(' #')[0]),
                                                                  lambda c: c[2][1]))
    sel_axis_synth = reference_mode.select(axis_synth, 70, 7, axes=(lambda c: (c[2][0], c[2][5].split(' #')[0]),
                                                                    lambda c: c[2][1]))
    cases = sel_grids + sel_gates + sel_locks + sel_synth + exact + axis_exact + sel_axis_real + sel_axis_synth
    results = reference_mode.parallel_map(run_one, cases, cost=lambda c: 30 if c[0] == 'gate' else 1)
    failures = [(c, r) for c, r in zip(cases, results) if r[0] != 'ok']
    table = {}
    classes = {}
    for r in results:
        if r[0] != 'ok': continue
        table.setdefault(r[1], {}).setdefault(r[2], 0)
        table[r[1]][r[2]] += 1
        if r[3] is not None and r[1].startswith('1a6ad0'):
            classes[r[3]] = classes.get(r[3], 0) + 1
    checked = failstop_checks(world(PLAYER_BEATS[0]))
    # Every x, y and z clause of each lock must decide at least one accepted
    # case: a segment along that axis alone, in each direction.
    decided = {}
    for c, r in zip(cases, results):
        if r[0] != 'ok' or c[0] not in ('synthetic', 'lock') or r[2] != 1: continue
        entry, s, e = c[2][0], c[2][2], c[2][3]
        axis = pure_axis(s, e)
        if axis is None: continue
        key = ('%x' % entry, 'xyz'[axis], 'up' if e[axis] > s[axis] else 'down',
               'real' if c[0] == 'lock' else 'synthetic')
        decided[key] = decided.get(key, 0) + 1
    need = {('%x' % entry, a, d) for entry in (LOCK6440, LOCK6AD0, LOCK7280) for a in 'xyz' for d in ('up', 'down')}
    have = {k[:3] for k in decided}
    real = {k[:3] for k in decided if k[3] == 'real'}
    print('  EMCL export verified against %d RAM images (%s)' % (verified, installed))
    for name in sorted(table):
        print('  %-14s returns %s' % (name, dict(sorted(table[name].items()))))
    print('  001A6AD0 surface classes on hits:', {hex(k): v for k, v in sorted(classes.items())})
    reference_mode.banner(reference_mode.part(len(sel_grids), len([c for c in grids if c[0] == 'grid']), '0019CB60'),
                          reference_mode.part(len(sel_gates), len([c for c in grids if c[0] == 'gate']), 'gate sweeps'),
                          reference_mode.part(len(sel_locks), len(locks), 'real-chain lock'),
                          reference_mode.part(len(sel_synth), len(synth), 'synthetic-chain lock'),
                          '%d exact-threshold' % len(exact), '%d y/z-interval and vertex-count' % len(axis_exact),
                          reference_mode.part(len(sel_axis_real), len(axis_real), 'real-chain y/z'),
                          reference_mode.part(len(sel_axis_synth), len(axis_synth), 'synthetic y/z'),
                          '%d fail-stop checks' % checked)
    print('  accepted along x / y / z alone:', ', '.join('%s %s %s %s %d' % (k + (v,)) for k, v in sorted(decided.items())))
    for c, r in failures[:20]:
        print('MISMATCH', r[1:], c[1], c[0])
    if failures:
        print('FAIL: %d of %d cases differ' % (len(failures), len(cases)))
        return 1
    if need - have:
        print('FAIL: no accepted case decided by these interval clauses:', sorted(need - have))
        return 1
    if not {('1a6440', 'y'), ('1a6440', 'z'), ('1a7280', 'y'), ('1a7280', 'z')} <= {k[:2] for k in real}:
        print('FAIL: the real chains accept no segment along y / z alone', sorted(real))
        return 1
    print('PASS: %d cases identical to the original (%.1f s)' % (len(cases), time.time() - t0))
    return 0


if __name__ == '__main__':
    sys.exit(main())
