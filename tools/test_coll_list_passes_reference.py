#!/usr/bin/env python3
"""Execute the original lane-L08 routines and compare
src/game/em_coll_list_passes.c and src/game/em_coll_list_passes_walkers.c
(docs/COLL_LIST_PASSES.md).

The user's pinned ELF and the captured AREA11 RAM supply every instruction
and every table; none are embedded here. The interpreter is the fall lane's
FallEE (tools/test_player_fall_reference.py: the shared bounded core with
every COP1 and VU0 macro instruction routed through tools/ee_float_model.py).
No shared file is edited.

Original code executed (from the captured RAM, checked against the ELF):
  walkers   0019B7D0 / 0019E280, 0019BA80 / 001A3980 / 0019E930, 0019F330,
            with 0019F1A0, 0019ED80, 001A4030 / 001A4650 / 001A44B0 and the
            SDK 001028B8 / 001028D0 / 00102738 / 00103230, 0011E748 (with
            0011CB90 / 0011E080), 0011DF78, 0011DBB8
  passes    001A9D20, 001A8DA0, 001A9F60, 001AA140, 001A7870, 001A8BE0,
            001A8660, 001A9000, 001A97B0, 001A9B10, with 0011E748,
            00128350 (001278C0 / 00127728 / 00126AB8), 001000C0 (001274B0 /
            00126BE8 / 00127398), 001028D0 and 00102760
Hooked boundaries (recorded, scripted, compared call by call with the
native workers): 001A8840, 001A8970, 001A8CE0, 001A8E80, 001A8F40,
001A9360, 001A96F0, 001A9480, 001A99E0, 001A9C40, 001A9E00, 001AA000,
0021BD10 and the entry's +0x34 behaviour of 001A8660. None of them ran on
the census route; the scripts make some of them shorten the scratchpad walks
(0x70003B86 / 0x70003B88), as 001A9360 and 001A99E0 do.

Walkers: every case compares the whole scratchpad state (0x70003190..
0x700031D8, 0x700030CA, 0x700030D4.., 0x70003680, 0x70003684, 0x7000324E,
0x70003254, the ranks, 0x70003B86/88), 0019F330's four output words and the
return value, and asserts the original wrote nothing else.
Passes: every case compares every byte of the pool arena, the player record,
the list arrays, the synthetic records and the globals, the scratchpad words
0x70003B86/88 and 0x700038A0..AC, the worker call log and the return value,
and asserts the original wrote nothing else; each world ends with a
whole-RAM comparison.

Route beats (census): each beat's published class lists are replayed as the
live lists (the snapshot follows 001AAD00's swap, so they are the lists the
passes last walked), the nine hooks run in order, and 001A8660 runs on the
beats whose class-0xD list holds a type-1 entry; the walkers run at the
player's rows of each beat's trace.

Stale state: a share of the queries start from a stale 0x700031D0 record
(a grid node or the cell record) and a nonzero 0x700031D4, so the stores of
0 there are visible (every staged 001A3980 case included). Boundary cases
(both modes): 0019E280's and 0019E930's rank bounds at equality,
0019E280's attribute test (0x77 / 0x78 / 0x79), 0019BA80's nudge at
box.y = +-0.0 and its class mask (the player patched to class bytes 0x30 /
0x14), 0019E930's and 001A3980's attribute / kind windows at their edges,
001A3980's 0x40000000 / 0x20000000 / 0x60000000 static words (query class
0 and nonzero, the hull read through the uncached mirror), pass 1 stopping
at the first unflagged word, its pass-2 owner gates (status 0 / 1 / 2,
class, zero word, uid = count, uid 0xFF with the count at 27 and 256), its
hull gate's y tests at equality and x / z tests on the segment start
(oblique segments), staged prim lists in a static and an owner hull
(0x8000 / 0x4000 round prims with and without 0x800, unknown type
nibbles, each ahead of a prim the segment hits), both passes' y re-split
on stacked hulls (static / static, static / owner, owner / owner),
0019F330's flat limit at h = 1e-4 exactly, and staged list cases for each
gate (001A7870's capsule y extents, its halfword -2 tag and +0x52 bit 0,
001A8660's circle / height tests at equality, 001A9000's status tests and
001AAD00's hook order among them; docs/COLL_LIST_PASSES.md section 3).

Default run: a covering sample (about 6 s). EM_TEST_FULL=1: every route row
of every beat and the full random sweeps.
"""
import ctypes as C
import hashlib
import os
import random
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from test_player_slide_reference import read_elf, bits, number  # noqa: E402
from test_player_fall_reference import FallEE  # noqa: E402
import test_coll_probe_reference as cp  # noqa: E402
import ee_float_model as M  # noqa: E402
import reference_mode  # noqa: E402

OUT = ROOT / 'build/coll_list_passes_reference'
ROUTE, BEATS = cp.ROUTE, cp.BEATS
u32, s16, u16 = cp.u32, cp.s16, cp.u16

GROUND, GRID78, LADDER, ATTR_CELLS, ATTR_GRID, COLUMN = 0x19B7D0, 0x19E280, 0x19BA80, 0x1A3980, 0x19E930, 0x19F330
P9D20, P8DA0, P9F60, PA140, P7870, P8BE0, P8660, P9000, P97B0, P9B10 = (
    0x1A9D20, 0x1A8DA0, 0x1A9F60, 0x1AA140, 0x1A7870, 0x1A8BE0, 0x1A8660, 0x1A9000, 0x1A97B0, 0x1A9B10)
HOOK_ORDER = (P9D20, P8DA0, P9F60, PA140, P7870, P8BE0, P9000, P97B0, P9B10)   # 001AAD00
WHICH = {P9D20: 0, P8DA0: 1, P9F60: 2, PA140: 3, P7870: 4, P8BE0: 5, P9000: 6, P97B0: 7, P9B10: 8}
PAIR_WORKERS = (0x1A8840, 0x1A8970, 0x1A8CE0, 0x1A8E80, 0x1A8F40, 0x1A9360, 0x1A96F0, 0x1A9480,
                0x1A99E0, 0x1A9C40, 0x1A9E00)
QUAD_WORKER, BD10 = 0x1AA000, 0x21BD10
PLAYER = 0x8102B0
ARGS = cp.ARGS
QOUT = ARGS + 0x40
POOL, POOL_END = 0x7A5640, 0x7A5640 + 0x100 * 0x2F0
SYN = 0x1F00000                 # synthetic records (capsules, radii, nodes): restored after each case
FAKE_FN = 0x1F10000             # a behaviour address no code occupies (hooked)
# RAM the passes may write, restored after every case on both sides.
REGIONS = ((POOL, POOL_END), (PLAYER, PLAYER + 0x320), (0x28AAB0, 0x28B024), (0x275B50, 0x275BC0),
           (0x28A9A0, 0x28A9A4), (0x810700, 0x810710), (SYN, SYN + 0x10000))
WRITABLE = ((POOL, POOL_END), (PLAYER, PLAYER + 0x320), (SYN, SYN + 0x10000))
SPAD_WORDS = ((0x70003B86, 4), (0x700038A0, 16))
WALK_SPAN = cp.STATE_SPAN + [(0x70003684, 0x70003688)]
CODE = [(0x100000, 0x130000)] + [(0x26DAE0, 0x90), (0x24A740, 0x80), (0x26C5D8, 0x80)]
EXTRA_SOURCES = ['src/game/em_coll_list_passes.c', 'src/game/em_coll_list_passes_walkers.c',
                 'src/game/em_coll_probe_original.c', 'src/game/em_actor_collision.c',
                 'src/game/em_collision.c', 'src/game/em_actor_pool.c', 'src/game/em_sdk_math_original.c',
                 'src/game/em_sdk_soft_float.c', 'src/game/em_effect_original.c']


def f32(v):
    return number(bits(v))


def check_code(elf, ram, where):
    phoff = u32(elf, 28)
    kind, offset, vaddr = struct.unpack_from('<3I', elf, phoff)
    assert kind == 1 and vaddr == 0x100000
    for start, size in CODE:
        at = start - vaddr + offset
        assert bytes(ram[start:start + size]) == elf[at:at + size], (where, 'differs from the ELF', hex(start))


# ---------------------------------------------------------------------------
# The native side

BRIDGE = r"""
#include <stdlib.h>
#include <string.h>
#include "game/em_coll_list_passes.h"
#include "game/em_coll_list_passes_walkers.h"

/* ---- walkers (the state layout of tools/test_coll_probe_reference.py) ---- */
typedef struct {
    uint32_t start[4], end[4], point[3], delta[3];
    int32_t record, node;
    uint32_t entity;
    int32_t kind;
    uint16_t cell_class;
    uint32_t cell_normal[3], ratio;
    int16_t query_class;
    uint32_t self;
    int16_t rank[6];
    int16_t span_lo, span_hi;
} BState;

typedef struct {
    EmActorCellTable table;
    EmCollision emcl;
    EmCollProbeGrid grid;
    EmActorClassLists lists;
    EmActor rec[64];
    uint32_t addr[64];
    int count;
    EmActor stranger;
    uint32_t stranger_addr;
    uint8_t kinds[256];
    EmActorCollisionWorld cells;
    EmCollProbeWorld world;
    EmSdkMathTables tables;
    int32_t mode;
    EmSdkMathContext math;
} Bridge;

static int find(Bridge *b, uint32_t addr)
{
    for (int i = 0; i < b->count; ++i) if (b->addr[i] == addr) return i;
    return -1;
}
static uint32_t addr_of(Bridge *b, const void *p)
{
    if (!p) return 0;
    if (p == (const void *)&b->stranger) return b->stranger_addr;
    for (int i = 0; i < b->count; ++i) if ((const void *)&b->rec[i] == p) return b->addr[i];
    return 0xFFFFFFFFu;
}
static const void *ptr_of(Bridge *b, uint32_t addr)
{
    if (!addr) return NULL;
    int i = find(b, addr);
    if (i >= 0) return &b->rec[i];
    b->stranger_addr = addr;
    return &b->stranger;
}

Bridge *bridge_new(const uint8_t *table, uint32_t size, const char *emcl, const uint8_t *elf,
                   uint32_t elf_size, int32_t mode)
{
    Bridge *b = calloc(1, sizeof *b);
    if (!b) return NULL;
    b->table.bytes = malloc(size);
    if (!b->table.bytes || size < 4) { free(b->table.bytes); free(b); return NULL; }
    memcpy(b->table.bytes, table, size);
    b->table.size = size;
    b->table.count = (int16_t)(table[0] | table[1] << 8);
    if (em_collision_load(&b->emcl, emcl) || em_coll_probe_grid_load(&b->grid, &b->emcl, emcl) ||
        em_sdk_math_original_load_tables(elf, elf_size, &b->tables)) {
        free(b->table.bytes); free(b); return NULL;
    }
    b->mode = mode;
    b->math.tables = &b->tables;
    b->math.world.d26C5D0 = &b->mode;
    b->cells.table = &b->table;
    b->cells.lists = &b->lists;
    b->world.cells = &b->cells;
    b->world.grid = &b->grid;
    b->stranger.self = &b->stranger;
    return b;
}
void bridge_free(Bridge *b)
{
    if (!b) return;
    em_coll_probe_grid_free(&b->grid);
    em_collision_free(&b->emcl);
    free(b->table.bytes);
    free(b);
}
void bridge_static_kinds(Bridge *b, const uint8_t *kinds, unsigned count)
{
    memcpy(b->kinds, kinds, count < 256 ? count : 256);
    b->cells.static_kind = count ? b->kinds : NULL;
    b->cells.static_kind_count = count;
}
int bridge_actor(Bridge *b, uint32_t addr, uint8_t status, uint8_t cls, uint8_t model, uint16_t uid,
                 uint16_t kind)
{
    int i = find(b, addr);
    if (i < 0) {
        if (b->count == 64) return -1;
        i = b->count++;
        b->addr[i] = addr;
    }
    EmActor *a = &b->rec[i];
    a->status = status; a->cls = cls; a->model = model; a->uid = uid; a->kind = kind;
    a->self = a;
    return i;
}
/* A grid node's attribute byte (node +0x1A), patched with the RAM copy. */
int bridge_node_attr(Bridge *b, int node, uint8_t attr)
{
    if (node < 0 || (uint32_t)node >= b->grid.count) return -1;
    b->emcl.polys[b->grid.first + (uint32_t)node].attr = attr;
    return 0;
}
/* A grid node's plane (node +0x24..+0x33), patched with the RAM copy. */
int bridge_node_plane(Bridge *b, int node, const uint32_t *words)
{
    if (node < 0 || (uint32_t)node >= b->grid.count) return -1;
    memcpy(b->emcl.polys[b->grid.first + (uint32_t)node].plane, words, 16);
    return 0;
}
int bridge_list(Bridge *b, const uint32_t *slots, int nslots, int published)
{
    EmActorClassList *l = &b->lists.list[EM_ACTOR_LIST_CLASS4];
    memset(l->slot, 0, sizeof l->slot);
    for (int i = 0; i < nslots; ++i) {
        if (!slots[i]) { l->slot[i] = NULL; continue; }
        int k = find(b, slots[i]);
        if (k < 0) return -1;
        l->slot[i] = &b->rec[k];
    }
    l->live = 0; l->published = (int16_t)published;
    return 0;
}
static void to_native(Bridge *b, const BState *in, EmCollProbeState *s)
{
    memset(s, 0, sizeof *s);
    memcpy(s->start, in->start, 16); memcpy(s->end, in->end, 16);
    memcpy(s->point, in->point, 12); memcpy(s->delta, in->delta, 12);
    s->record = in->record; s->node = in->node;
    s->entity = ptr_of(b, in->entity);
    s->kind = in->kind; s->cell_class = in->cell_class;
    memcpy(s->cell_normal, in->cell_normal, 12); memcpy(&s->ratio, &in->ratio, 4);
    s->query_class = in->query_class;
    s->self = ptr_of(b, in->self);
    memcpy(s->rank, in->rank, sizeof s->rank);
    s->span_lo = in->span_lo; s->span_hi = in->span_hi;
}
static void from_native(Bridge *b, const EmCollProbeState *s, BState *out)
{
    memcpy(out->start, s->start, 16); memcpy(out->end, s->end, 16);
    memcpy(out->point, s->point, 12); memcpy(out->delta, s->delta, 12);
    out->record = s->record; out->node = s->node;
    out->entity = addr_of(b, s->entity);
    out->kind = s->kind; out->cell_class = s->cell_class;
    memcpy(out->cell_normal, s->cell_normal, 12); memcpy(&out->ratio, &s->ratio, 4);
    out->query_class = s->query_class;
    out->self = addr_of(b, s->self);
    memcpy(out->rank, s->rank, sizeof s->rank);
    out->span_lo = s->span_lo; out->span_hi = s->span_hi;
}

/* which: 0 0019B7D0, 1 0019BA80, 2 0019E280, 3 0019E930, 4 001A3980,
 * 5 0019F330 (node = mask), 6 the camera-ground adapter. `math_off` drops
 * the SDK context (a fault case). */
int bridge_run(Bridge *b, int which, BState *st, const float *a, const float *c, uint32_t self,
               uint8_t cls, uint32_t mask, float *q, uint32_t *s3684, int math_off)
{
    EmCollProbeState s;
    to_native(b, st, &s);
    EmCollProbeState before = s;
    int r;
    switch (which) {
    case 0: r = em_coll_list_passes_0019B7D0(&b->grid, &s, a, c); break;
    case 1: r = em_coll_list_passes_0019BA80(&b->world, &s, ptr_of(b, self), cls, a, c, mask); break;
    case 2: r = em_coll_list_passes_0019E280(&b->grid, &s); break;
    case 3: r = em_coll_list_passes_0019E930(&b->grid, &s); break;
    case 4: r = em_coll_list_passes_001A3980(&b->world, &s); break;
    case 5: {
        float f84;
        memcpy(&f84, s3684, 4);
        r = em_coll_list_passes_0019F330(&b->grid, math_off ? NULL : &b->math, &s, &f84, a, c, (int)mask, q);
        memcpy(s3684, &f84, 4);
        break;
    }
    case 6: {
        EmCollListPassesGround g = { &b->grid, &s };
        uint32_t fa[4] = { 0 }, fc[4] = { 0 };
        int res = -9;
        memcpy(fa, a, 12); memcpy(fc, c, 12);
        r = em_coll_list_passes_camera_ground(&g, fa, fc, &res);
        if (r == 0) r = res;
        break;
    }
    default: return -9;
    }
    if (r < 0 && memcmp(&before, &s, sizeof s) && (which == 0 || which == 1 || which == 6 || which == 5))
        return -8;
    from_native(b, &s, st);
    return r;
}

/* ---- list passes ---- */
typedef struct { int32_t id; uint32_t a, b, c, d; } LCall;

typedef struct {
    uint8_t *ram;
    uint32_t ram_size;
    uint8_t d24A740[0x440];
    EmCollListData data;
    EmSdkMathTables tables;
    int32_t mode;
    EmSdkMathContext math;
    LCall calls[512];
    int ncalls;
    int script_at[16], script_which[16];
    int16_t script_value[16];
    int nscript;
    int bd10;
    EmCollListPasses passes;
} LBridge;

static uint8_t *lp_bytes(void *context, uint32_t address, uint32_t size)
{
    LBridge *b = context;
    if ((uint64_t)address + size > b->ram_size) return NULL;
    return b->ram + address;
}
static int lp_rec(LBridge *b, EmCollListPasses *p, int32_t id, uint32_t x, uint32_t y, uint32_t z,
                  uint32_t w)
{
    int k = b->ncalls;
    if (k < 512) { b->calls[k].id = id; b->calls[k].a = x; b->calls[k].b = y; b->calls[k].c = z; b->calls[k].d = w; }
    b->ncalls++;
    for (int i = 0; i < b->nscript && id != 0x1AA000; ++i)
        if (b->script_at[i] == k) {
            if (b->script_which[i] == 0) p->globals->s3B86 = b->script_value[i];
            else p->globals->s3B88 = b->script_value[i];
        }
    return 0;
}
#define PAIR(addr) static int w_##addr(void *c, EmCollListPasses *p, uint32_t x, uint32_t y) \
    { return lp_rec(c, p, 0x##addr, x, y, 0, 0); }
PAIR(1A8840) PAIR(1A8970) PAIR(1A8CE0) PAIR(1A8E80) PAIR(1A8F40) PAIR(1A9360) PAIR(1A96F0)
PAIR(1A9480) PAIR(1A99E0) PAIR(1A9C40) PAIR(1A9E00)
static int w_1AA000(void *c, EmCollListPasses *p, uint32_t x, uint32_t y, uint32_t z, uint32_t w)
{ return lp_rec(c, p, 0x1AA000, x, y, z, w); }
static int w_21BD10(void *c, EmCollListPasses *p, int *result)
{ LBridge *b = c; lp_rec(b, p, 0x21BD10, 0, 0, 0, 0); *result = b->bd10; return 0; }
static int w_behaviour(void *c, EmCollListPasses *p, uint32_t fn, uint32_t e, uint32_t pl, uint32_t b0)
{ return lp_rec(c, p, -1, fn, e, pl, b0); }

LBridge *lp_new(const uint8_t *elf, uint32_t elf_size, int32_t mode, const uint8_t *table, uint32_t size)
{
    LBridge *b = calloc(1, sizeof *b);
    if (!b) return NULL;
    if (em_sdk_math_original_load_tables(elf, elf_size, &b->tables) || size > sizeof b->d24A740) {
        free(b); return NULL;
    }
    memcpy(b->d24A740, table, size);
    b->data.d24A740 = b->d24A740;
    b->data.d24A740_size = size;
    b->mode = mode;
    b->math.tables = &b->tables;
    b->math.world.d26C5D0 = &b->mode;
    EmCollListPasses *p = &b->passes;
    p->memory.context = b;
    p->memory.bytes = lp_bytes;
    p->data = &b->data;
    p->math = &b->math;
    EmCollListWorkers *w = &p->workers;
    w->context = b;
    w->w_001A8840 = w_1A8840; w->w_001A8970 = w_1A8970; w->w_001A8CE0 = w_1A8CE0;
    w->w_001A8E80 = w_1A8E80; w->w_001A8F40 = w_1A8F40; w->w_001A9360 = w_1A9360;
    w->w_001A96F0 = w_1A96F0; w->w_001A9480 = w_1A9480; w->w_001A99E0 = w_1A99E0;
    w->w_001A9C40 = w_1A9C40; w->w_001A9E00 = w_1A9E00; w->w_001AA000 = w_1AA000;
    w->w_0021BD10 = w_21BD10; w->behaviour = w_behaviour;
    w->normalize = em_coll_list_passes_normalize;
    return b;
}
void lp_free(LBridge *b) { free(b); }
void lp_ram(LBridge *b, uint8_t *ram, uint32_t size) { b->ram = ram; b->ram_size = size; }
void lp_script(LBridge *b, int n, const int *at, const int *which, const int16_t *value, int bd10)
{
    b->nscript = n < 16 ? n : 16;
    for (int i = 0; i < b->nscript; ++i) { b->script_at[i] = at[i]; b->script_which[i] = which[i]; b->script_value[i] = value[i]; }
    b->bd10 = bd10;
}
int lp_calls(LBridge *b, LCall *out) { memcpy(out, b->calls, sizeof b->calls); return b->ncalls; }
uint32_t lp_fault(LBridge *b) { return b->passes.fault; }
int lp_bound(LBridge *b) { return em_coll_list_passes_bound(&b->passes); }

/* which: 0..8 the nine hooks (001AAD00 order), 9 001A8660(player, entry),
 * 10 the whole hook sequence; drop (fault cases): 1 unbinds 001A9C40, 2 the
 * normalize worker; 3..6 bind the unported variants for 001A8840, 001AA000,
 * 0021BD10 and the +0x34 behaviour. */
int lp_run(LBridge *b, int which, EmCollListGlobals *g, uint32_t player, uint32_t entry, int drop)
{
    EmCollListPasses *p = &b->passes;
    EmCollListWorkers saved = p->workers;
    p->globals = g;
    p->fault = 0;
    b->ncalls = 0;
    if (drop == 1) p->workers.w_001A9C40 = NULL;
    if (drop == 2) p->workers.normalize = NULL;
    if (drop == 3) p->workers.w_001A8840 = em_coll_list_passes_unported;
    if (drop == 4) p->workers.w_001AA000 = em_coll_list_passes_unported_001AA000;
    if (drop == 5) p->workers.w_0021BD10 = em_coll_list_passes_unported_0021BD10;
    if (drop == 6) p->workers.behaviour = em_coll_list_passes_unported_behaviour;
    int r;
    switch (which) {
    case 0: r = em_coll_list_001A9D20(p); break;
    case 1: r = em_coll_list_001A8DA0(p); break;
    case 2: r = em_coll_list_001A9F60(p, player); break;
    case 3: r = em_coll_list_001AA140(p); break;
    case 4: r = em_coll_list_001A7870(p); break;
    case 5: r = em_coll_list_001A8BE0(p, player); break;
    case 6: r = em_coll_list_001A9000(p); break;
    case 7: r = em_coll_list_001A97B0(p); break;
    case 8: r = em_coll_list_001A9B10(p); break;
    case 9: r = em_coll_list_001A8660(p, player, entry); break;
    case 10: r = em_coll_list_passes_001AAD00_hooks(p, player); break;
    default: r = -9;
    }
    p->workers = saved;
    return r;
}
"""


class LCall(C.Structure):
    _fields_ = [('id', C.c_int32), ('a', C.c_uint32), ('b', C.c_uint32), ('c', C.c_uint32), ('d', C.c_uint32)]


class Globals(C.Structure):
    _fields_ = [('d275BB0', C.c_uint32), ('d275BB8', C.c_int16), ('d275BA0', C.c_uint32), ('d275BA8', C.c_int16),
                ('d275B90', C.c_uint32), ('d275B98', C.c_int16), ('d275B80', C.c_uint32), ('d275B88', C.c_int16),
                ('s3B86', C.c_int16), ('s3B88', C.c_int16), ('s3B8D', C.c_uint8), ('d28A9A0', C.c_int16),
                ('d810700', C.c_uint8), ('d810702', C.c_uint8), ('d81070A', C.c_uint8), ('s38A0', C.c_uint32 * 4)]


LISTS = (('d275BB0', 'd275BB8', 0x275BB0, 0x275BB8), ('d275BA0', 'd275BA8', 0x275BA0, 0x275BA8),
         ('d275B90', 'd275B98', 0x275B90, 0x275B98), ('d275B80', 'd275B88', 0x275B80, 0x275B88))
PUBLISHED = {0x275BB0: (0x275BAC, 0x275BB4), 0x275BA0: (0x275B9C, 0x275BA4), 0x275B90: (0x275B8C, 0x275B94),
             0x275B80: (0x275B7C, 0x275B84)}
LIST_BASE = {0x275BB0: 0x28B020, 0x275BA0: 0x28AFF0, 0x275B90: 0x28AF30, 0x275B80: 0x28AE30}


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    source = OUT / 'bridge.c'
    source.write_text(BRIDGE)
    lib = OUT / ('bridge.dylib' if sys.platform == 'darwin' else 'bridge.so')
    command = ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared', '-fPIC',
               '-Isrc', str(source)] + EXTRA_SOURCES + ['-lm', '-o', str(lib)]
    digest = hashlib.sha256(' '.join(command).encode() + BRIDGE.encode())
    for path in EXTRA_SOURCES:
        digest.update((ROOT / path).read_bytes())
    for header in sorted((ROOT / 'src').rglob('*.h')):
        digest.update(header.read_bytes())
    stamp = Path(str(lib) + '.sha256')
    if not (lib.exists() and stamp.exists() and stamp.read_text() == digest.hexdigest()):
        subprocess.run(command, cwd=ROOT, check=True)
        stamp.write_text(digest.hexdigest())
    n = C.CDLL(str(lib))
    V, P, U8, U16, U32, I = C.c_void_p, C.c_char_p, C.c_uint8, C.c_uint16, C.c_uint32, C.c_int
    PF = C.POINTER(C.c_float)
    n.bridge_new.restype = V
    n.bridge_new.argtypes = [P, U32, P, P, U32, C.c_int32]
    n.bridge_free.argtypes = [V]
    n.bridge_static_kinds.argtypes = [V, P, C.c_uint]
    n.bridge_actor.argtypes = [V, U32, U8, U8, U8, U16, U16]
    n.bridge_list.argtypes = [V, C.POINTER(U32), I, I]
    n.bridge_node_attr.argtypes = [V, I, U8]
    n.bridge_node_plane.argtypes = [V, I, C.POINTER(U32)]
    n.bridge_run.argtypes = [V, I, C.POINTER(cp.BState), PF, PF, U32, U8, U32, PF, C.POINTER(U32), I]
    n.lp_new.restype = V
    n.lp_new.argtypes = [P, U32, C.c_int32, P, U32]
    n.lp_free.argtypes = [V]
    n.lp_ram.argtypes = [V, C.c_void_p, U32]
    n.lp_script.argtypes = [V, I, C.POINTER(I), C.POINTER(I), C.POINTER(C.c_int16), I]
    n.lp_calls.argtypes = [V, C.POINTER(LCall)]
    n.lp_fault.restype = U32
    n.lp_fault.argtypes = [V]
    n.lp_bound.argtypes = [V]
    n.lp_run.argtypes = [V, I, C.POINTER(Globals), U32, U32, I]
    return n


def export_emcl():
    cp.OUT = OUT              # the probe lane's exporter run, into this lane's build folder
    return cp.export_emcl()


G = {}


# ---------------------------------------------------------------------------
# The walkers: one world on both sides

class WalkPair:
    def __init__(self, beat):
        elf, native, emcl = G['elf'], G['native'], G['emcl']
        self.world = world = cp.World(beat, elf)
        check_code(elf, world.ram, beat)
        self.native = native
        self.ee = FallEE(elf, world.ram, world.spad)
        self.ram = self.ee.mem
        self.spad0 = bytes(self.ee.spad)
        image = world.image(self.ram)
        mode = u32(self.ram, 0x26C5D0)
        self.b = native.bridge_new(image, len(image), str(emcl).encode(), elf, len(elf), mode)
        assert self.b, 'bridge_new'
        owners = world.owners(self.ram)
        for a in set(o for o in owners if o):
            native.bridge_actor(self.b, a, self.ram[a], self.ram[a + 2], self.ram[a + 3], u16(self.ram, a + 0xE),
                                u16(self.ram, a + 0x54))
        slots = list(reversed(owners))
        arr = (C.c_uint32 * max(1, len(slots)))(*slots)
        assert native.bridge_list(self.b, arr, len(slots), len(slots)) == 0
        kinds = world.kinds(self.ram)
        native.bridge_static_kinds(self.b, kinds, len(kinds))

    def rebind(self, kinds=None):
        self.native.bridge_free(self.b)
        world, native = self.world, self.native
        image = world.image(self.ram)
        self.b = native.bridge_new(image, len(image), str(G['emcl']).encode(), G['elf'], len(G['elf']),
                                   u32(self.ram, 0x26C5D0))
        for a in set(o for o in world.owners(self.ram) if o):
            native.bridge_actor(self.b, a, self.ram[a], self.ram[a + 2], self.ram[a + 3], u16(self.ram, a + 0xE),
                                u16(self.ram, a + 0x54))
        slots = list(reversed(world.owners(self.ram)))
        arr = (C.c_uint32 * max(1, len(slots)))(*slots)
        assert native.bridge_list(self.b, arr, len(slots), len(slots)) == 0
        kinds = world.kinds(self.ram) if kinds is None else kinds
        native.bridge_static_kinds(self.b, kinds, len(kinds))

    def original(self, case, entry, args, stage, s3684):
        ee = self.ee
        ee.spad[:] = self.spad0
        for address, data in stage:
            ee.write(address, data)
        case.load(ee)
        ee.save(0x70003684, s3684)
        written = []
        save = ee.save

        def guarded(address, value, size=4):
            a = address & 0xFFFFFFFF
            if not 0x7F000000 <= a < 0x7F100000:
                written.append((a, size))
            save(address, value, size)
        ee.save = guarded
        try:
            ee.call(entry, args)
        finally:
            del ee.save
        for a, size in written:
            assert any(lo <= a and a + size <= hi for lo, hi in WALK_SPAN), \
                (self.world.beat, hex(entry), 'the original wrote outside the modelled state', hex(a), size)
        r = ee.r[2] & 0xFFFFFFFF
        return (r - (1 << 32) if r & 0x80000000 else r), cp.ee_state(ee, self.world)

    def both(self, where, case, which, entry, args, a=(0.0, 0.0, 0.0), c=(0.0, 0.0, 0.0), self_addr=0, cls=0,
             mask=0, stage=(), q0=(0, 0, 0, 0), s3684=0):
        want_r, want = self.original(case, entry, args, stage, s3684)
        want_q = tuple(self.ee.load(QOUT + 4 * k) for k in range(4))
        want_84 = self.ee.load(0x70003684)
        st = case.native(self.world)
        q = (C.c_float * 4)(*[number(v) for v in q0])
        w84 = C.c_uint32(s3684)
        got_r = self.native.bridge_run(self.b, which, C.byref(st), cp.fvec(a), cp.fvec(c), self_addr, cls, mask,
                                       q, C.byref(w84), 0)
        got = cp.native_state(st)
        assert want_r == got_r, (self.world.beat, where, 'return', want_r, got_r, want, got)
        for key, value in want.items():
            assert got[key] == value, (self.world.beat, where, key, value, got[key])
        if which == 5:
            assert tuple(bits(v) for v in q) == want_q, (self.world.beat, where, 'q', want_q)
            assert w84.value == want_84, (self.world.beat, where, '0x70003684', hex(want_84), hex(w84.value))
        return want_r, want

    def args(self, *vectors):
        data = b''.join(struct.pack('<4f', *(list(v) + [0.0] * (4 - len(v)))) for v in vectors)
        return [(ARGS, data)]

    # The queries with their callers' arguments.
    def ground(self, case, where, a, c, adapter=False):
        return self.both(where, case, 6 if adapter else 0, GROUND, (ARGS, ARGS + 0x10), a, c,
                         stage=self.args(a, c))

    def ladder(self, case, where, point, box, mask, actor=PLAYER, stage=()):
        self_word = u32(self.ram, actor + 0x14)
        return self.both(where, case, 1, LADDER, (actor, ARGS, ARGS + 0x10, mask), point, box, self_addr=self_word,
                         cls=self.ram[actor + 2], mask=mask, stage=self.args(point, box) + list(stage))

    def column(self, case, where, a, c, node, q0, s3684):
        stage = self.args(a, c) + [(QOUT, struct.pack('<4I', *q0))]
        return self.both(where, case, 5, COLUMN, (ARGS, ARGS + 0x10, QOUT, self.world.nodes + 64 * node), a, c,
                         mask=node, stage=stage, q0=q0, s3684=s3684)


def up(v, dy):
    return (v[0], number(M.ee_add(bits(v[1]), bits(dy))), v[2])


def ulps(v, k):
    """The float k ulps above v (toward +inf)."""
    b = bits(f32(v))
    if b & 0x80000000:
        return number((b - k) & 0xFFFFFFFF) if b != 0x80000000 else number(k)
    return number(b + k)


EPS_EDGE = number(0x3727C5AC)      # 0019ED80's ring-edge tolerance (+1e-5)
FLAT_LIMIT = 0x38D1B717            # 0019F330's flat limit (1e-4), as bits


def odd_bound_margin(world, ram, n78):
    """The smallest largest-edge-dot a point can have in 0019ED80's ring test
    when it lies at least one ulp beyond a node's maximum x (or z): the
    in-plane distance (>= that ulp) times cos(half the vertex's exterior
    angle), minimised over the ring's vertex regions and edges. Above
    EPS_EDGE, an equality on 0019E280's +0x0E / +0x16 bounds (the query's
    min x / z strictly beyond the node's max) can never reach an accepted
    node, so the strictness of those two tests cannot change a result."""
    import math

    def ulp(x):
        return 2.0 ** (math.frexp(abs(x))[1] - 24)
    worst = float('inf')
    for i in n78:
        _, verts, _ = cp.node_ring(world, ram, i)
        n = len(verts)
        for axis in (0, 2):
            top = max(v[axis] for v in verts)
            u = ulp(top)
            worst = min(worst, u)                      # an edge region: the distance itself
            for k in range(n):
                v, a, b = verts[k], verts[k - 1], verts[(k + 1) % n]
                e1 = [a[j] - v[j] for j in range(3)]
                e2 = [b[j] - v[j] for j in range(3)]
                c = sum(x * y for x, y in zip(e1, e2)) / math.sqrt(sum(x * x for x in e1) * sum(x * x for x in e2))
                half = (math.pi - math.acos(max(-1.0, min(1.0, c)))) / 2
                worst = min(worst, max(u, top + u - v[axis]) * math.cos(half))
    return worst


def run_walk_beat(item):
    beat, rows, count, seed = item
    pair = WalkPair(beat)
    world, ram = pair.world, pair.ram
    rng = random.Random(seed)
    out = {'ground': [0, 0], 'ladder': {}, 'e280': [0, 0], 'e930': [0, 0], 'a3980': [0, 0], 'column': [0, 0],
           'cases': 0, 'static_hits': 0, 'kind_edge_hits': 0}
    base = cp.Case(world)
    box = struct.unpack_from('<4f', ram, PLAYER + 0x280)
    attr = [ram[world.nodes + 64 * i + 0x1A] for i in range(world.node_count)]
    n78 = [i for i, a in enumerate(attr) if a == 0x78]
    nmid = [i for i, a in enumerate(attr) if 0x1E <= a < 0x5A]
    slot = BEATS.index(beat)
    full = reference_mode.FULL
    for key in ('edge', 'nudge0', 'attr_edge'):
        out[key] = [0, 0]
    out['attr78'] = [0, 0, 0]      # ground hits with a floor node's attribute at 0x77 / 0x78 / 0x79
    owners_now = [a for a in world.owners(ram) if a]
    assert PLAYER not in owners_now      # the patched player class bytes below need no rebind
    stale_entity = owners_now[0] if owners_now else PLAYER

    def stale(k):
        """0x700031D0 / 0x700031D4 as an earlier query left them: a grid node
        record or the cell record D_700030B0, and a nonzero entity. 0019B7D0
        and 0019BA80 must overwrite or clear both."""
        record = cp.CELL_RECORD if k % 2 else world.nodes + 64 * ((k * 7919 + 13) % world.node_count)
        return {0x700031D0: record, 0x700031D4: stale_entity}

    def plane_y(plane, x, z):
        return (plane[3] - plane[0] * x - plane[2] * z) / plane[1]

    def tally(key, r):
        if isinstance(out[key], dict):
            out[key][r] = out[key].get(r, 0) + 1
        else:
            out[key][1 if r in (1, 4) else 0] += 1
        out['cases'] += 1

    # Route rows: the ground query under the hip and the ladder probe at the
    # feet. Every other row starts from a stale record / entity pair, and
    # every third row probes with mask 4 alone (0019E930 only, so a miss
    # leaves 0x19BC08's 0x700031D0 clear as the only record store).
    for n_row, (counter, feet, _) in enumerate(rows):
        case = cp.Case(world, None, stale(n_row)) if n_row % 2 else base
        r, _ = pair.ground(case, f'row {counter} ground', up(feet, 18.0), up(feet, -40.0))
        tally('ground', r)
        mask = 4 if n_row % 3 == 1 else 7
        r, _ = pair.ladder(case, f'row {counter} ladder mask {mask}', up(feet, 0.2), box, mask)
        out['ladder'][r] = out['ladder'].get(r, 0) + 1
        out['cases'] += 1
    # The camera adapter on the first row (clean and stale state).
    if rows:
        feet = rows[0][1]
        pair.ground(base, 'adapter', up(feet, 18.0), up(feet, -40.0), adapter=True)
        pair.ground(cp.Case(world, None, stale(2)), 'adapter stale', up(feet, 18.0), up(feet, -40.0), adapter=True)
        pair.ground(cp.Case(world, None, stale(3)), 'adapter stale cell', up(feet, 18.0), up(feet, -40.0),
                    adapter=True)
        out['cases'] += 3
    # Points on the attr-0x78 nodes (0019E280) and the 0x1E..0x59 nodes (0019E930).
    for k in range(count):
        i = rng.choice(n78) if k % 2 == 0 else rng.randrange(world.node_count)
        _, verts, _ = cp.node_ring(world, ram, i)
        pt = cp.inside(rng, verts)
        a = (f32(pt[0] + rng.uniform(-2, 2) * (k % 3 == 0)), f32(pt[1] + rng.uniform(1, 30)),
             f32(pt[2] + rng.uniform(-2, 2) * (k % 3 == 0)))
        c = (f32(pt[0] + rng.uniform(-3, 3) * (k % 4 == 0)), f32(pt[1] - rng.uniform(-2, 30)),
             f32(pt[2] + rng.uniform(-3, 3) * (k % 4 == 0)))
        r, _ = pair.ground(cp.Case(world, rng if k % 2 else None, stale(k) if k % 3 == 0 else None),
                           f'ground node {i}', a, c)
        tally('ground', r)
    owner_hulls = []
    for a in world.owners(ram):
        uid = u16(ram, a + 0xE) >> 8
        word = u32(ram, world.table + 4 + 4 * uid) if uid < world.count else 0
        if word:
            owner_hulls.append(world.table + word)
    # Every attr-0x78 node near each ring vertex (the rank bounds are equal
    # there), straight down: 0019E280's six rank gates at their edges.
    if count >= 8 or reference_mode.FULL:
        for i in n78:
            _, verts, _ = cp.node_ring(world, ram, i)
            centre = [sum(v[j] for v in verts) / len(verts) for j in range(3)]
            for v in verts:
                pt = [v[j] * 0.97 + centre[j] * 0.03 for j in range(3)]
                a = (f32(pt[0]), f32(pt[1] + 10.0), f32(pt[2]))
                c = (f32(pt[0]), f32(pt[1] - 10.0), f32(pt[2]))
                r, _ = pair.ground(base, f'ground node {i} edge', a, c)
                tally('ground', r)
    # 0019E280's rank bounds at equality: vertical segments through each
    # attr-0x78 ring vertex (and one ulp beside it in x or z) whose top, or
    # bottom, sits one ulp above the vertex. The query's rank then equals
    # the node's +0x0C / +0x10 / +0x14 bound (the top) or its +0x12 bound
    # (the bottom) while 0019ED80 still accepts the node. The quick run
    # spreads the vertices over the beats (every beat has the same grid).
    edge_jobs = [(i, v) for i in n78 for v in cp.node_ring(world, ram, i)[1]]
    if not full:
        edge_jobs = edge_jobs[slot::len(BEATS)]
    for i, v in edge_jobs:
        for dx, dz in ((0, 0), (1, 0), (0, 1)):
            x, z = ulps(v[0], dx), ulps(v[2], dz)
            for end in ('top', 'bottom'):
                if end == 'top':
                    a, c = (x, ulps(v[1], 1), z), (x, f32(v[1] - 10.0), z)
                else:
                    a, c = (x, f32(v[1] + 10.0), z), (x, ulps(v[1], 1), z)
                r, _ = pair.ground(cp.Case(world, None, stale(dx + 2 * dz)), f'ground node {i} rank edge {end}',
                                   a, c)
                tally('ground', r)
                out['edge'][int(r == 4)] += 1
    # 0019E930's rank bounds at equality, as above: vertical segments
    # through the ring vertices of 0x1E..0x59 nodes (one ulp beside them in
    # x or z), top or bottom one ulp above the vertex, the walker alone.
    # Two floor nodes per beat by default, every tenth in the full run.
    mid_floors = [i for i in nmid if cp.node_ring(world, ram, i)[0][1] > 0.5]
    mid_jobs = mid_floors[slot * 2 % len(mid_floors):][:2] if not full else mid_floors[::10]
    out['edge930'] = [0, 0]
    for i in mid_jobs:
        for v in cp.node_ring(world, ram, i)[1]:
            for dx, dz in ((0, 0), (1, 0), (0, 1)):
                x, z = ulps(v[0], dx), ulps(v[2], dz)
                for end in ('top', 'bottom'):
                    if end == 'top':
                        a, c = (x, ulps(v[1], 1), z), (x, f32(v[1] - 10.0), z)
                    else:
                        a, c = (x, f32(v[1] + 10.0), z), (x, ulps(v[1], 1), z)
                    words = stale(dx + 2 * dz)
                    for j in range(3):
                        words[0x70003190 + 4 * j] = bits(a[j])
                        words[0x700031A0 + 4 * j] = bits(c[j])
                    r, _ = pair.both(f'0019E930 node {i} rank edge {end} {dx}{dz}', cp.Case(world, None, words), 3,
                                     ATTR_GRID, ())
                    out['edge930'][int(r == 0)] += 1
                    out['cases'] += 1
    for k in range(count):
        i = rng.choice(nmid) if k % 3 else rng.randrange(world.node_count)
        _, verts, _ = cp.node_ring(world, ram, i)
        pt = cp.inside(rng, verts)
        if k % 4 == 0:
            pt = [pt[0] + rng.uniform(-3, 3), pt[1], pt[2] + rng.uniform(-3, 3)]
        if owner_hulls and k % 3 == 1:     # on top of an owner cell (001A3980 pass 2)
            hull = rng.choice(owner_hulls)
            lo, hi = struct.unpack_from('<3f', ram, hull), struct.unpack_from('<3f', ram, hull + 0xC)
            pt = [rng.uniform(lo[j], hi[j]) for j in range(3)]
            pt[1] = hi[1]
        point = (f32(pt[0]), f32(pt[1] + rng.choice((-1.0, 0.0, 0.3, 2.0, 12.0))), f32(pt[2]))
        probe = (0.0, rng.choice((box[1], box[1], 4.0, -0.5, 30.0, 0.0)), 0.0)
        mask = rng.choice((7, 7, 6, 4, 2, 0))
        words = stale(k) if k % 2 else {}
        if rng.random() < 0.1:
            words[0x70003244] = 0xFFFF
        r, _ = pair.ladder(cp.Case(world, None, words), f'ladder node {i} mask {mask}', point, probe, mask)
        out['ladder'][r] = out['ladder'].get(r, 0) + 1
        out['cases'] += 1
    # 0019BA80's nudge at box.y = +0.0 and -0.0: c.lt.s is false for both, so
    # both take -0.001. The point sits 0.0003 below a mid-attribute floor
    # node: the original's segment rises to the point and misses, a +0.001
    # nudge would cross the plane downward and hit.
    floors = [i for i in nmid if cp.node_ring(world, ram, i)[0][1] > 0.5]
    for i in (floors if full else floors[slot % len(floors):][:1] if floors else []):
        plane, verts, _ = cp.node_ring(world, ram, i)
        centre = [sum(v[j] for v in verts) / len(verts) for j in range(3)]
        x, z = f32(centre[0]), f32(centre[2])
        point = (x, f32(plane_y(plane, x, z) - 0.0003), z)
        for y0 in (0.0, -0.0):
            for mask in (4, 7):
                r, _ = pair.ladder(cp.Case(world, None, stale(mask)), f'ladder node {i} box.y {y0!r} mask {mask}',
                                   point, (0.0, y0, 0.0), mask)
                out['ladder'][r] = out['ladder'].get(r, 0) + 1
                out['nudge0'][int(r != 0)] += 1
                out['cases'] += 1
    # 0019E930's attribute window at its edges: a floor node's +0x1A patched
    # to 0x1D / 0x1E / 0x59 / 0x5A on both sides (the RAM byte and the
    # native grid's attr), then the walker alone on a vertical segment
    # through its centre and the ladder probe from above it.
    for i in (floors if full else floors[(slot * 7 + 3) % len(floors):][:1] if floors else []):
        plane, verts, _ = cp.node_ring(world, ram, i)
        centre = [sum(v[j] for v in verts) / len(verts) for j in range(3)]
        x, z = f32(centre[0]), f32(centre[2])
        y = plane_y(plane, x, z)
        at = world.nodes + 64 * i + 0x1A
        saved_attr = ram[at]
        for value in (0x1D, 0x1E, 0x59, 0x5A):
            ram[at] = value
            assert pair.native.bridge_node_attr(pair.b, i, value) == 0
            words = stale(value)
            for j in range(3):
                words[0x70003190 + 4 * j] = bits((x, f32(y + 2.0), z)[j])
                words[0x700031A0 + 4 * j] = bits((x, f32(y - 2.0), z)[j])
            r, _ = pair.both(f'0019E930 node {i} attr {value:#x}', cp.Case(world, None, words), 3, ATTR_GRID, ())
            out['attr_edge'][int(r == 0)] += 1
            r, _ = pair.ladder(cp.Case(world, None, stale(value + 1)), f'ladder node {i} attr {value:#x}',
                               (x, f32(y - 0.5), z), (0.0, -3.0, 0.0), 4)
            out['ladder'][r] = out['ladder'].get(r, 0) + 1
            out['attr_edge'][int(r == 4)] += 1
            out['cases'] += 2
        # 0019E280's attribute test is == 0x78 (AREA11's grid has no
        # attribute above 0x78): the same node at 0x77, 0x78 and 0x79, the
        # ground query straight down through its centre.
        for value in (0x77, 0x78, 0x79):
            ram[at] = value
            assert pair.native.bridge_node_attr(pair.b, i, value) == 0
            r, _ = pair.ground(cp.Case(world, None, stale(value)), f'ground node {i} attr {value:#x}',
                               (x, f32(y + 2.0), z), (x, f32(y - 2.0), z))
            out['attr78'][value - 0x77] += r == 4
            out['cases'] += 1
        ram[at] = saved_attr
        assert pair.native.bridge_node_attr(pair.b, i, saved_attr) == 0
    # The three walkers on their own, over staged (oblique) segments.
    for k in range(count):
        entry, which, key = ((GRID78, 2, 'e280'), (ATTR_GRID, 3, 'e930'), (ATTR_CELLS, 4, 'a3980'))[k % 3]
        pool = n78 if entry == GRID78 and k % 2 else nmid if entry == ATTR_GRID and k % 2 else None
        i = rng.choice(pool) if pool else rng.randrange(world.node_count)
        _, verts, _ = cp.node_ring(world, ram, i)
        pt = cp.inside(rng, verts)
        if entry == ATTR_CELLS and owner_hulls and k % 4 < 3:
            hull = rng.choice(owner_hulls)
            lo, hi = struct.unpack_from('<3f', ram, hull), struct.unpack_from('<3f', ram, hull + 0xC)
            pt = [rng.uniform(lo[j], hi[j]) for j in range(3)]
            pt[1] = hi[1] - rng.uniform(0, 2)
        a = (pt[0] + rng.uniform(-4, 4) * (k % 2), pt[1] + rng.uniform(-5, 25), pt[2] + rng.uniform(-4, 4) * (k % 2))
        c = (pt[0] + rng.uniform(-4, 4) * (k % 2), pt[1] - rng.uniform(-2, 10), pt[2] + rng.uniform(-4, 4) * (k % 2))
        if entry != ATTR_CELLS and k % 2:      # against the node's normal (walls included)
            n = struct.unpack_from('<3f', ram, world.nodes + 64 * i + 0x24)
            span = rng.uniform(0.5, 15)
            a = tuple(pt[j] + n[j] * span for j in range(3))
            c = tuple(pt[j] - n[j] * span * rng.uniform(0.0, 1.2) for j in range(3))
        words = {}
        for j in range(3):
            words[0x70003190 + 4 * j] = bits(f32(a[j]))
            words[0x700031A0 + 4 * j] = bits(f32(c[j]))
        if rng.random() < 0.2:
            words[0x70003254] = rng.choice(world.owners(ram) or [0])
        if entry == ATTR_GRID and rng.random() < 0.1:
            words[0x70003244] = 0xFFFF
        r, _ = pair.both(f'walker {entry:#x} node {i}', cp.Case(world, rng, words), which, entry, ())
        out[key][int(r == (1 if entry == GRID78 else 0))] += 1
        out['cases'] += 1
    # 0019F330: 0019BC40's line (pos -> pos + (0, 1, 0)) through random nodes,
    # and oblique lines.
    for k in range(count):
        i = rng.randrange(world.node_count)
        _, verts, _ = cp.node_ring(world, ram, i)
        pt = cp.inside(rng, verts)
        if k % 3 == 0:
            pt = [pt[0] + rng.uniform(-2, 2), pt[1], pt[2] + rng.uniform(-2, 2)]
        a = (f32(pt[0]), f32(pt[1] + rng.uniform(-20, 20)), f32(pt[2]))
        c = up(a, 1.0) if k % 4 else (f32(a[0] + rng.uniform(-1, 1)), f32(a[1] + rng.uniform(-5, 5)),
                                       f32(a[2] + rng.uniform(-1, 1)))
        q0 = tuple(rng.getrandbits(32) & 0x7F7FFFFF for _ in range(4))
        r, _ = pair.column(cp.Case(world, rng), f'0019F330 node {i}', a, c, i, q0, rng.getrandbits(32) & 0x7F7FFFFF)
        out['column'][r] += 1
        out['cases'] += 1
    # 0019F330 without its SDK context faults and leaves the state.
    st = base.native(world)
    q = (C.c_float * 4)()
    w84 = C.c_uint32(0)
    assert pair.native.bridge_run(pair.b, 5, C.byref(st), cp.fvec((0.0, 0.0, 0.0)), cp.fvec((0.0, 1.0, 0.0)), 0,
                                  0, 0, q, C.byref(w84), 1) == -1
    # Static cells in 001A3980's pass 1: the leading owner-free uids become
    # static cells with attribute kinds (patched RAM on both sides). A word
    # may carry 0x40000000 (skipped) or 0x20000000 (skipped while the query
    # class 0x7000324E is 0; otherwise its hull is read at tbl + (word &
    # 0x3FFFFFFF), which lies in the EE's uncached main-RAM mirror at
    # 0x20000000 and so names the same bytes as tbl + (word & 0x1FFFFFFF)).
    # 0019BA80 sets the query class from the actor's class byte & 0x1F: the
    # player's is 0x20 (class 0), an owner's 4 or 0x84 (class 4).
    table = world.table
    used = {u16(ram, a + 0xE) >> 8 for a in world.owners(ram)}
    free = []
    for uid in range(world.count):
        if uid in used:
            break
        if u32(ram, table + 4 + 4 * uid):
            free.append(uid)
    saved = bytes(ram[table:table + world.size])
    kv = world.kind_view
    saved_kinds = bytes(ram[kv:kv + 0x28 * world.count])
    class_actor = next((a for a in owners_now if ram[a + 2] & 0x1F), None)
    static_cases = 0
    for key in ('flag_hits', 'flag_gated', 'flag_ladder', 'flag_ladder_gated', 'flag_ladder_cls'):
        out[key] = 0

    def static_hull(uid):
        """The hull pass 1 reads for a static word, as a RAM address."""
        return (table + (u32(ram, table + 4 + 4 * uid) & 0x3FFFFFFF)) & 0x1FFFFFF

    if free:
        for k in range(max(2, count // 4)):
            for uid in free:
                flag = rng.choice((0, 0, 0, 0x40000000, 0x20000000, 0x20000000, 0x60000000))
                word = u32(saved, 4 + 4 * uid) | 0x80000000 | flag
                struct.pack_into('<I', ram, table + 4 + 4 * uid, word)
                ram[kv + 0x28 * uid + 8] = rng.choice((0x1D, 0x1E, 0x32, 0x46, 0x59, 0x5A, 0x04))
            pair.rebind()
            uid = rng.choice(free)
            hull = static_hull(uid)
            lo, hi = struct.unpack_from('<3f', ram, hull), struct.unpack_from('<3f', ram, hull + 0xC)
            pt = [rng.uniform(lo[j], hi[j]) for j in range(3)]
            point = (f32(pt[0]), f32(hi[1] + rng.uniform(-1, 3)), f32(pt[2]))
            probe = (0.0, rng.choice((box[1], 4.0, -30.0)), 0.0)
            actor = class_actor if class_actor and k % 2 else PLAYER
            r, _ = pair.ladder(cp.Case(world, None, stale(k) if k % 3 == 0 else {}), f'static uid {uid} actor {actor:#x}',
                               point, probe, rng.choice((2, 6, 7)), actor=actor)
            out['static_hits'] += r == 2
            # The walker itself on an oblique segment from inside the hull
            # (pass 1 gates on the segment start's x/z).
            start = (f32(pt[0]), f32(rng.uniform(lo[1], hi[1])), f32(pt[2]))
            d = [rng.uniform(-12, 12), rng.uniform(-6, 6), rng.uniform(-12, 12)]
            # A stale nonzero 0x700031D4: pass 1 clears it on a hit (0x1A3CAC).
            words = {0x7000324E: rng.choice((0, 1, 0x100, 0x8000)), 0x700031D4: stale_entity if k % 2 else 0}
            for j in range(3):
                words[0x70003190 + 4 * j] = bits(start[j])
                words[0x700031A0 + 4 * j] = bits(f32(start[j] + d[j]))
            r, _ = pair.both(f'static uid {uid} walker', cp.Case(world, rng, words), 4, ATTR_CELLS, ())
            out['static_hits'] += r == 0
            static_cases += 2
            out['cases'] += 2
        # Segments through each static prim along both directions of its
        # normal (n-gons) or across its face (0x2000), starting inside the
        # hull box: the prim tests of pass 1 with every static cell at one
        # kind, at the edges of the 0x1E..0x59 window (and inside it and
        # beyond both edges in the full run). The 0x20000000 sweeps run each
        # segment with the query class 0 and nonzero (1 or 0x100), and probe
        # 0019BA80 down onto each hull's top from the player (class 0) and
        # from a class-4 owner.
        # A word with both 0x40000000 and 0x20000000 (0x60000000) at query
        # class 1: the 0x40000000 test comes first and skips it, so it must
        # hit exactly as often as the 0x40000000 sweep. Every other prim
        # segment starts from a stale nonzero 0x700031D4.
        sweeps = ((0x1E, 0), (0x59, 0), (0x1D if slot % 2 else 0x5A, 0), (0x32, 0x20000000), (0x32, 0x40000000),
                  (0x32, 0x60000000))
        if reference_mode.FULL:
            sweeps = ((0x1D, 0), (0x1E, 0), (0x32, 0), (0x59, 0), (0x5A, 0), (0x1E, 0x20000000),
                      (0x32, 0x20000000), (0x59, 0x20000000), (0x32, 0x40000000), (0x32, 0x60000000))
        sweep_hits = {}
        for sweep_kind, flag in sweeps:
            for uid in free:
                word = u32(saved, 4 + 4 * uid) | 0x80000000 | flag
                struct.pack_into('<I', ram, table + 4 + 4 * uid, word)
                ram[kv + 0x28 * uid + 8] = sweep_kind
            pair.rebind()
            for uid in free:
                hull = static_hull(uid)
                lo, hi = struct.unpack_from('<3f', ram, hull), struct.unpack_from('<3f', ram, hull + 0xC)
                for n_prim, prim in enumerate(cp.prim_addresses(ram, hull)[:4]):
                    h = u16(ram, prim)
                    if h & 0xF000 == 0x1000:
                        n = struct.unpack_from('<3f', ram, prim + 4)
                        ring = [struct.unpack_from('<3f', ram, prim + 0x14 + 12 * j) for j in range(ram[prim + 2])]
                        pt = [sum(v[j] for v in ring) / len(ring) for j in range(3)]
                    elif h & 0xF000 == 0x2000:
                        o = struct.unpack_from('<6f', ram, prim + 4)
                        pt = [o[j] + o[3 + j] * 0.5 for j in range(3)]
                        n = (0.0, 0.0, 1.0) if o[5] == 0 else (1.0, 0.0, 0.0) if o[3] == 0 else (0.0, 1.0, 0.0)
                    else:
                        continue
                    for sign in (1, -1):
                        start = [min(max(pt[j] + sign * n[j] * 0.5, lo[j]), hi[j]) for j in range(3)]
                        end = [pt[j] - sign * n[j] * 2.0 for j in range(3)]
                        results = {}
                        for qclass in ((0, 0x100 if n_prim % 2 else 1) if flag == 0x20000000 else (1,)):
                            words = {0x7000324E: qclass, 0x700031D4: stale_entity if sign == 1 else 0}
                            for j in range(3):
                                words[0x70003190 + 4 * j] = bits(f32(start[j]))
                                words[0x700031A0 + 4 * j] = bits(f32(end[j]))
                            r, _ = pair.both(f'static uid {uid} kind {sweep_kind:#x} flag {flag:#x} prim '
                                             f'{prim - hull:#x} {sign} class {qclass:#x}',
                                             cp.Case(world, None, words), 4, ATTR_CELLS, ())
                            results[qclass] = r
                            out['static_hits'] += r == 0
                            if qclass:
                                sweep_hits[flag] = sweep_hits.get(flag, 0) + (r == 0)
                            if sweep_kind in (0x1E, 0x59) and not flag:
                                out['kind_edge_hits'] += r == 0
                            static_cases += 1
                            out['cases'] += 1
                        if flag == 0x20000000:
                            q = 0x100 if n_prim % 2 else 1
                            out['flag_hits'] += results[q] == 0
                            out['flag_gated'] += results[q] != results[0]
                if flag == 0x20000000:
                    x, z = f32((lo[0] + hi[0]) / 2), f32((lo[2] + hi[2]) / 2)
                    got = {}
                    # The player as captured (class byte 0x20), a class-4
                    # owner, and the player with its class byte patched to
                    # 0x30 and 0x14 (0019BA80's & 0x1F keeps bit 4: classes
                    # 0x10 and 0x14, both nonzero).
                    for actor, cls_byte in ((PLAYER, None), (class_actor, None), (PLAYER, 0x30), (PLAYER, 0x14)):
                        if not actor:
                            continue
                        saved_cls = ram[actor + 2]
                        if cls_byte is not None:
                            ram[actor + 2] = cls_byte
                        r, _ = pair.ladder(cp.Case(world, None, stale(uid)), f'static uid {uid} flag ladder '
                                           f'actor {actor:#x} class {ram[actor + 2]:#x}', (x, f32(hi[1] - 0.5), z),
                                           (0.0, -3.0, 0.0), 2, actor=actor)
                        ram[actor + 2] = saved_cls
                        got[(actor, cls_byte)] = r
                        static_cases += 1
                        out['cases'] += 1
                    if class_actor:
                        out['flag_ladder'] += got[(class_actor, None)] == 2
                        out['flag_ladder_gated'] += got[(class_actor, None)] != got[(PLAYER, None)]
                    # A patched player class sees the flagged hull exactly as
                    # the class-4 owner does.
                    for cls_byte in (0x30, 0x14):
                        assert (got[(PLAYER, cls_byte)] == 2) == (got[(PLAYER, None)] == 2 or
                                                                    got.get((class_actor, None)) == 2), \
                            (world.beat, 'patched player class', hex(cls_byte), got)
                        out['flag_ladder_cls'] += got[(PLAYER, cls_byte)] == 2
        assert sweep_hits.get(0x60000000) == sweep_hits.get(0x40000000), \
            (world.beat, '0x60000000 words must be skipped like 0x40000000 words', sweep_hits)
        ram[table:table + world.size] = saved
        ram[kv:kv + 0x28 * world.count] = saved_kinds
        pair.rebind()
    # 001A3980 pass 2 skips a published owner whose +0xE high byte is 0xFF
    # (0x1A3D70), before it reads that directory word. With the captured
    # count (27) the later uid < count test (0x1A3DA0) would also reject it;
    # with the count at 256 (0x7000324C and the directory's count word) the
    # 0xFF test is the only guard: the word at directory +0x400 is not a hull
    # offset. A vertical segment through the owner's hull top, the walker
    # alone and the ladder probe from the player, first unpatched, then with
    # +0xF = 0xFF (both sides), then with the count at 256 as well.
    out['uid_ff'] = [0, 0]
    out['owner_gates'] = [0, 0]
    out['gate_edge'] = 0

    def uid_ff_segment(a):
        hull = table + u32(ram, table + 4 + 4 * (u16(ram, a + 0xE) >> 8))
        lo, hi = struct.unpack_from('<3f', ram, hull), struct.unpack_from('<3f', ram, hull + 0xC)
        x, z = f32((lo[0] + hi[0]) / 2), f32((lo[2] + hi[2]) / 2)
        seg = {}
        for j, (s_v, e_v) in enumerate(zip((x, f32(hi[1] + 2.0), z), (x, f32(hi[1] - 1.0), z))):
            seg[0x70003190 + 4 * j] = bits(s_v)
            seg[0x700031A0 + 4 * j] = bits(e_v)
        return seg, (x, f32(hi[1] - 0.5), z)

    # The first class-4 owner the plain segment hits (the plain runs are
    # compared too).
    target = None
    for a in owners_now:
        uid = u16(ram, a + 0xE) >> 8
        if not (ram[a] and ram[a + 2] & 0x1F == 4 and uid < world.count and u32(ram, table + 4 + 4 * uid)):
            continue
        seg, point = uid_ff_segment(a)
        r1, _ = pair.both(f'uid 0xFF candidate {a:#x} walker plain', cp.Case(world, None, dict(seg)), 4,
                          ATTR_CELLS, ())
        out['cases'] += 1
        if r1 == 0:
            target = a
            break
    if target:
        a_ff = target
        seg, point = uid_ff_segment(a_ff)
        saved_rec, saved_count = bytes(ram[a_ff:a_ff + 0x10]), bytes(ram[table:table + 4])
        zero_uid = next((u for u in range(world.count) if not u32(ram, table + 4 + 4 * u)), None)
        results = {}
        # Pass 2's owner gates on the owner the plain segment hits: +0xE
        # high byte 0xFF (with the count at 27 and at 256), the uid equal to
        # the count (0x1A3DA0), a uid whose directory word is 0 (0x1A3D8C),
        # status 0 (0x1A3D34) and class bytes 0x14 / 0x24 (& 0x1F, 0x1A3D48).
        for variant in ('plain', 'uid 0xff', 'uid 0xff count 256', 'uid count', 'uid zero word', 'status 0',
                        'status 1', 'status 2', 'class 0x14', 'class 0x24'):
            stage = []
            ram[a_ff:a_ff + 0x10] = saved_rec
            if variant.startswith('uid 0xff'):
                ram[a_ff + 0xF] = 0xFF
            if variant.endswith('256'):
                struct.pack_into('<I', ram, table, 256)
                stage = [(0x7000324C, struct.pack('<h', 256))]
            if variant == 'uid count':
                ram[a_ff + 0xF] = world.count
            if variant == 'uid zero word':
                if zero_uid is None:
                    continue
                ram[a_ff + 0xF] = zero_uid
            if variant.startswith('status'):
                ram[a_ff] = int(variant[-1])
            if variant.startswith('class'):
                ram[a_ff + 2] = int(variant[-4:], 16)
            pair.rebind()
            r1, st1 = pair.both(f'owner {a_ff:#x} walker {variant}', cp.Case(world, None, dict(seg)), 4,
                                ATTR_CELLS, (), stage=stage)
            r2, st2 = pair.ladder(cp.Case(world, None, stale(7)), f'owner {a_ff:#x} ladder {variant}',
                                  point, (0.0, -3.0, 0.0), 2, stage=stage)
            results[variant] = (r1, sorted(st1.items()), r2, sorted(st2.items()))
            out['cases'] += 2
            ram[a_ff:a_ff + 0x10] = saved_rec
            ram[table:table + 4] = saved_count
        pair.rebind()
        out['uid_ff'] = [1, int(results['plain'] != results['uid 0xff'])]
        for variant in ('status 0', 'class 0x14', 'uid count'):
            out['owner_gates'][int(results['plain'] != results[variant])] += 1
        # Pass 2 tests the status byte against 0 only (0x1A3D34): status 1
        # and 2 (both occur in captured class-4 owners) hit alike.
        for variant in ('status 1', 'status 2'):
            assert results[variant] == results['plain'], (world.beat, 'owner', variant, results[variant])
        # The hull gate (0x1A3DB4..) and the y re-split (0x1A3FD8) at
        # equality: vertical segments through the hit owner's hull centre
        # that end or start exactly on the hull's top or bottom y.
        hull = table + u32(ram, table + 4 + 4 * (u16(ram, a_ff + 0xE) >> 8))
        lo, hi = struct.unpack_from('<3f', ram, hull), struct.unpack_from('<3f', ram, hull + 0xC)
        x, z = f32((lo[0] + hi[0]) / 2), f32((lo[2] + hi[2]) / 2)
        for name, y0, y1 in (('down to top', hi[1] + 2.0, hi[1]), ('up from top', hi[1], hi[1] + 2.0),
                             ('down from top', hi[1], hi[1] - 1.0), ('up to bottom', lo[1] - 2.0, lo[1]),
                             ('down from bottom', lo[1], lo[1] - 2.0), ('up from bottom', lo[1], lo[1] + 1.0)):
            words = {}
            for j, (s_v, e_v) in enumerate(zip((x, f32(y0), z), (x, f32(y1), z))):
                words[0x70003190 + 4 * j] = bits(s_v)
                words[0x700031A0 + 4 * j] = bits(e_v)
            pair.both(f'owner {a_ff:#x} gate edge {name}', cp.Case(world, None, words), 4, ATTR_CELLS, ())
            out['cases'] += 1
        # The gate's y tests at equality where the result depends on them:
        # the hull's AABB min y (+4) patched to equal a falling segment's
        # top, or its max y (+0x10) to equal the segment's bottom, while the
        # segment still crosses the hull's top face (both sides patched).
        top = [x, f32(hi[1] + 2.0), z]
        bottom = [x, f32(hi[1] - 1.0), z]
        words = {}
        for j in range(3):
            words[0x70003190 + 4 * j] = bits(top[j])
            words[0x700031A0 + 4 * j] = bits(bottom[j])
        saved_box = bytes(ram[hull:hull + 0x18])
        for name, off, value in (('min y = segment top', 4, top[1]), ('max y = segment bottom', 0x10, bottom[1])):
            struct.pack_into('<f', ram, hull + off, value)
            pair.rebind()
            r, st = pair.both(f'owner {a_ff:#x} aabb {name}', cp.Case(world, None, dict(words)), 4, ATTR_CELLS, ())
            out['gate_edge'] += st['entity'] == a_ff
            out['cases'] += 1
            ram[hull:hull + 0x18] = saved_box
        pair.rebind()
    # Staged prim lists in a static hull (pass 1) and in the hit owner's hull
    # (pass 2), patched on both sides. AREA11's directory holds 0x4000 prims
    # without 0x800 (uids 5 and 6) and no 0x8000 prims, so the test writes
    # them: round prims of each header (0x8000 / 0x4000, with and without
    # 0x800; 001A44B0 reads only +0..+0x17, the 0x800 tail is zeros) and
    # unknown type nibbles, each followed by a 0x4000 prim F that the
    # segment hits. The first prim sits either on the segment (x offset 0)
    # or 100 to the side (a miss, so the walk steps to F). Expected, and
    # asserted per case:
    #   a 0x8000 prim on the segment: pass 2 hits it (0x1A3E94), pass 1
    #     steps over it (0x1A3B90) and hits nothing;
    #   a missed round prim then F: F is hit in both passes, which needs the
    #     size step of that header (0x1A3B90 / 0x1A3BC0 / 0x1A3EA4 /
    #     0x1A3ED4) to land on F; a wrong 0x800 size lands on the zero tail;
    #   an unknown type nibble (0 or 3) then F: the walk neither tests nor
    #     steps over it (0x1A3B84 / 0x1A3E88), so it reads the same header
    #     again and F is never reached (a 4-byte and a 0x14-byte unknown
    #     prim, so a step of either size would reach F).
    # Every case starts from a stale nonzero 0x700031D4 (the player's
    # address): pass 1 must clear it on a hit, pass 2 must store the owner.
    out['prim_stage'] = [0, 0]      # cases, cases with the expected outcome
    out['round'] = [0, 0]           # pass-1 hits on a 0x8000 / 0x4000 prim on the segment
    out['round2'] = 0               # pass-2 hits on a 0x8000 prim on the segment
    out['prim_names'] = set()       # (case, pass) staged at least once

    def prim_room(hull):
        prims = cp.prim_addresses(ram, hull)
        return prims[-1] + cp.prim_size(ram, prims[-1]) - (hull + 0x1C) if prims else 0

    def put_prims(hull, blobs):
        data = b''.join(blobs)
        assert len(data) <= prim_room(hull), (world.beat, 'staged prims overrun the hull', hex(hull))
        ram[hull + 0x1C:hull + 0x1C + len(data)] = data
        struct.pack_into('<h', ram, hull + 0x18, len(blobs))

    def round_prim(header, x, y, z, radius=1.0, half=1.0):
        body = struct.pack('<HBB5f', header, 0, 0, x, y, z, radius, half) + bytes(0x20)
        return body[:cp.prim_size(body, 0)]

    def vwords(x, y0, y1, z, extra=()):
        words = dict(extra)
        for j, (s_v, e_v) in enumerate(zip((x, y0, z), (x, y1, z))):
            words[0x70003190 + 4 * j] = bits(f32(s_v))
            words[0x700031A0 + 4 * j] = bits(f32(e_v))
        return words

    def static_only(uids, kind=0x32):
        """The captured table with `uids` as the only static cells (they
        must be the leading uids: pass 1 stops at the first word without
        0x80000000)."""
        ram[table:table + world.size] = saved
        ram[kv:kv + 0x28 * world.count] = saved_kinds
        for u in uids:
            struct.pack_into('<I', ram, table + 4 + 4 * u, u32(saved, 4 + 4 * u) | 0x80000000)
            ram[kv + 0x28 * u + 8] = kind

    def prim_lists(hull, owner):
        lo, hi = struct.unpack_from('<3f', ram, hull), struct.unpack_from('<3f', ram, hull + 0xC)
        x, z = f32((lo[0] + hi[0]) / 2), f32((lo[2] + hi[2]) / 2)
        cy = float(round((lo[1] + hi[1]) / 2))          # an integer: cy +- 1 and +- 3 are exact
        side = f32(x + 100.0)
        hit_f = round_prim(0x4000, x, cy, z)
        cases = [('0x8000 on the segment', [round_prim(0x8000, x, cy, z)], owner != 0),
                 ('0x8800 on the segment', [round_prim(0x8800, x, cy, z)], owner != 0),
                 ('0x4000 on the segment', [round_prim(0x4000, x, cy, z)], True),
                 ('0x4800 on the segment', [round_prim(0x4800, x, cy, z)], True)]
        for header in (0x8000, 0x8800, 0x4000, 0x4800):
            cases.append((f'{header:#x} beside then F', [round_prim(header, side, cy, z), hit_f], True))
        cases += [('type 3 (4 bytes) then F', [struct.pack('<HH', 0x3000, 0), hit_f], False),
                  ('type 0 (0x14 bytes) then F', [bytes(0x14), hit_f], False),
                  ('type 3 (0x14 bytes) then F', [struct.pack('<H', 0x3000) + bytes(0x12), hit_f], False)]
        saved_hull = bytes(ram[hull:hull + 0x1C + prim_room(hull)])
        for name, blobs, expect in cases:
            if len(b''.join(blobs)) > prim_room(hull):
                continue
            put_prims(hull, blobs)
            pair.rebind()
            for y0, y1 in ((cy + 3.0, cy - 3.0), (cy - 3.0, cy + 3.0)):
                face = cy + 1.0 if y0 > y1 else cy - 1.0
                r, st = pair.both(f'{"owner" if owner else "static"} hull {hull:#x} prims: {name} '
                                  f'y {y0} -> {y1}', cp.Case(world, None, vwords(x, y0, y1, z, {0x700031D4: PLAYER})),
                                  4, ATTR_CELLS, ())
                hit = st['end'][1] == bits(face) and st['entity'] == owner
                assert hit == expect and (r == 0) == expect, \
                    (world.beat, 'staged prims', name, owner, y0, r, st['end'], st['entity'])
                out['prim_stage'][0] += 1
                out['prim_stage'][1] += hit == expect
                out['prim_names'].add((name, 2 if owner else 1))
                if name == '0x8000 on the segment':
                    if owner:
                        out['round2'] += hit
                    else:
                        out['round'][0] += hit
                if name == '0x4000 on the segment' and not owner:
                    out['round'][1] += hit
                out['cases'] += 1
            ram[hull:hull + len(saved_hull)] = saved_hull
        # The hull gate's x / z tests read the segment start (0x1A3AD4..,
        # 0x1A3DD8..), and 001A44B0's circle test does too. Oblique segments
        # with the start inside the AABB and the end 5 beyond it in x or z,
        # past the minimum or the maximum (F under the start: hit), and with
        # the start 5 beyond it and the end at the centre (F under the start,
        # outside the AABB: gated out).
        for axis, out_v in ((0, f32(hi[0] + 5.0)), (0, f32(lo[0] - 5.0)), (2, f32(hi[2] + 5.0)),
                            (2, f32(lo[2] - 5.0))):
            for start_out in (False, True):
                sx, sz = (out_v if axis == 0 and start_out else x), (out_v if axis == 2 and start_out else z)
                ex, ez = (out_v if axis == 0 and not start_out else x), (out_v if axis == 2 and not start_out else z)
                put_prims(hull, [round_prim(0x4000, sx, cy, sz)])
                pair.rebind()
                words = {0x700031D4: PLAYER}
                for j, (s_v, e_v) in enumerate(zip((sx, cy + 3.0, sz), (ex, cy - 3.0, ez))):
                    words[0x70003190 + 4 * j] = bits(f32(s_v))
                    words[0x700031A0 + 4 * j] = bits(f32(e_v))
                r, st = pair.both(f'{"owner" if owner else "static"} hull {hull:#x} oblique axis {axis} to {out_v} start '
                                  f'{"outside" if start_out else "inside"}', cp.Case(world, None, words), 4,
                                  ATTR_CELLS, ())
                hit = st['end'][1] == bits(cy + 1.0) and st['entity'] == owner
                assert hit != start_out, (world.beat, 'hull gate on the start x / z', axis, start_out, owner,
                                          st['end'], st['entity'])
                out['prim_stage'][0] += 1
                out['prim_stage'][1] += 1
                out['prim_names'].add((f'oblique axis {axis} {out_v > x if axis == 0 else out_v > z} start {start_out}',
                                       2 if owner else 1))
                out['cases'] += 1
                ram[hull:hull + len(saved_hull)] = saved_hull

    if free and free[0] == 0:
        static_only([0])
        prim_lists(static_hull(0), 0)
    # Pass 1 stops at the first directory word without 0x80000000 (0x1A3A18
    # leaves the loop; the NEARMISS C continues instead). Static uid 1 with
    # a 0x4000 prim on the segment: behind a static uid 0 it is hit, behind
    # an unflagged uid 0 it is never tested.
    out['p1_break'] = 0
    if free[:2] == [0, 1]:
        for uids, expect in (([0, 1], True), ([1], False)):
            static_only(uids)
            hull = static_hull(1)
            lo, hi = struct.unpack_from('<3f', ram, hull), struct.unpack_from('<3f', ram, hull + 0xC)
            x, z = f32((lo[0] + hi[0]) / 2), f32((lo[2] + hi[2]) / 2)
            cy = float(round((lo[1] + hi[1]) / 2))
            put_prims(hull, [round_prim(0x4000, x, cy, z)])
            pair.rebind()
            r, st = pair.both(f'pass 1 stop: static uids {uids}', cp.Case(world, None, vwords(x, cy + 3.0, cy - 3.0, z,
                                                                                            {0x700031D4: PLAYER})),
                              4, ATTR_CELLS, ())
            hit = st['end'][1] == bits(cy + 1.0) and st['entity'] == 0
            assert hit == expect, (world.beat, 'pass 1 stops at an unflagged word', uids, r, st['end'], st['entity'])
            out['p1_break'] += 1
            out['cases'] += 1
    if target:
        static_only([])
        prim_lists(table + u32(ram, table + 4 + 4 * (u16(ram, a_ff + 0xE) >> 8)), a_ff)
    # Pass 1's y re-split after a hit (0x1A3CF0 / 0x1A3CF4). Two hulls A
    # and B are stacked in y at B's x/z (AABBs 2 high, a 0x4000 prim of
    # radius 1 and half height 0.5 in each), and a vertical segment 20 long
    # crosses both. A is static uid 0, hit first by pass 1; B is either
    # static uid 1 (pass 1 again) or the hit owner's hull (pass 2). Falling,
    # A lies below B: after the hit on A's top the range is [A's top,
    # start], which still overlaps B, and B's top is the final hit. Rising,
    # A lies above B and the range after A's bottom is [start, A's bottom].
    # With the two range ends exchanged, B would be gated out.
    out['resplit'] = 0

    def stacked(b_hull, b_owner, a_hull, a_owner=0):
        lo, hi = struct.unpack_from('<3f', ram, b_hull), struct.unpack_from('<3f', ram, b_hull + 0xC)
        x, z = f32((lo[0] + hi[0]) / 2), f32((lo[2] + hi[2]) / 2)
        y0 = float(round(lo[1]))
        backup = {h: bytes(ram[h:h + 0x1C + prim_room(h)]) for h in (a_hull, b_hull)}
        # 'beyond': B's prim stays between the start and A, but its AABB
        # moves past A's hit (y0 - 15 .. y0 - 13 falling, y0 - 2 .. y0
        # rising). The narrowed range gates B out, so A's face is the final
        # hit; a range left at the whole segment would test B's prim.
        for falling, beyond in ((True, False), (False, False), (True, True), (False, True)):
            ya, yb = (y0 - 9.0, y0 - 5.0) if falling else (y0 - 5.0, y0 - 9.0)
            for h, yc in ((a_hull, ya), (b_hull, yb)):
                box_y = (y0 - 14.0 if falling else y0 - 1.0) if (beyond and h == b_hull) else yc
                struct.pack_into('<6f', ram, h, f32(x - 1.0), box_y - 1.0, f32(z - 1.0), f32(x + 1.0), box_y + 1.0,
                                 f32(z + 1.0))
                put_prims(h, [round_prim(0x4000, x, yc, z, 1.0, 0.5)])
            pair.rebind()
            s_y, e_y = (y0, y0 - 20.0) if falling else (y0 - 20.0, y0)
            r, st = pair.both(f'y re-split {"falling" if falling else "rising"} B {"owner" if b_owner else "static"}'
                              f'{" beyond" if beyond else ""}',
                              cp.Case(world, None, vwords(x, s_y, e_y, z, {0x700031D4: PLAYER})), 4, ATTR_CELLS, ())
            if beyond:
                want_y, want_e = (ya + 0.5 if falling else ya - 0.5), a_owner
            else:
                want_y, want_e = (yb + 0.5 if falling else yb - 0.5), b_owner
            assert r == 0 and st['end'][1] == bits(want_y) and st['entity'] == want_e, \
                (world.beat, 'y re-split', falling, beyond, b_owner, st['end'], st['entity'])
            out['resplit'] += 1
            out['cases'] += 1
            for h, data in backup.items():
                ram[h:h + len(data)] = data

    if free[:2] == [0, 1]:
        static_only([0, 1])
        stacked(static_hull(1), 0, static_hull(0))
    if free[:1] == [0] and target:
        static_only([0])
        stacked(table + u32(ram, table + 4 + 4 * (u16(ram, a_ff + 0xE) >> 8)), a_ff, static_hull(0))
    # Pass 2's re-split (0x1A3FD0 / 0x1A3FD8): A and B both owner hulls, A
    # the one pass 2 reaches first (the native list is the published slots
    # in order), no static cells.
    usable = []
    for a in owners_now:
        uid = u16(ram, a + 0xE) >> 8
        if (ram[a] and ram[a + 2] & 0x1F == 4 and uid < world.count and u32(ram, table + 4 + 4 * uid)
                and a not in usable and all(u16(ram, b + 0xE) >> 8 != uid for b in usable)):
            usable.append(a)
    out['resplit2'] = 0
    if len(usable) >= 2:
        static_only([])
        a_first, b_second = usable[0], usable[1]
        before = out['resplit']
        stacked(table + u32(ram, table + 4 + 4 * (u16(ram, b_second + 0xE) >> 8)), b_second,
                table + u32(ram, table + 4 + 4 * (u16(ram, a_first + 0xE) >> 8)), a_first)
        out['resplit2'] = out['resplit'] - before
    ram[table:table + world.size] = saved
    ram[kv:kv + 0x28 * world.count] = saved_kinds
    pair.rebind()
    # 0019F330's flat test at h == 1e-4 exactly (0x19F594: h < 1e-4 takes the
    # flat ratio). A floor node's plane is patched on both sides to n =
    # (hx, 1, 0), d through the node's centre, for hx a few ulps around
    # 1e-4; the line is 0019BC40's (pos -> pos + (0, 1, 0)) through that
    # centre. At least one hx must give 0x70003680 == 1e-4 exactly.
    out['flat_edge'] = [0, 0]
    for i in ([floors[(slot * 5 + 1) % len(floors)]] if floors else []):
        plane, verts, _ = cp.node_ring(world, ram, i)
        centre = [sum(v[j] for v in verts) / len(verts) for j in range(3)]
        x, z = f32(centre[0]), f32(centre[2])
        y = plane_y(plane, x, z)
        at = world.nodes + 64 * i + 0x24
        saved_plane = bytes(ram[at:at + 16])
        for dbits in range(-3, 4):
            hx = number(FLAT_LIMIT + dbits)
            words = (bits(hx), bits(1.0), bits(0.0), bits(f32(y + hx * x)))
            struct.pack_into('<4I', ram, at, *words)
            assert pair.native.bridge_node_plane(pair.b, i, (C.c_uint32 * 4)(*words)) == 0
            a = (x, f32(y - 5.0), z)
            _, want = pair.column(cp.Case(world), f'0019F330 node {i} flat edge hx {hx!r}', a, up(a, 1.0), i,
                                  (0, 0, 0, 0), 0)
            out['flat_edge'][int(want['ratio'] == FLAT_LIMIT)] += 1
            out['cases'] += 1
        ram[at:at + 16] = saved_plane
        assert pair.native.bridge_node_plane(pair.b, i, (C.c_uint32 * 4)(*struct.unpack('<4I', saved_plane))) == 0
    out['static'] = static_cases
    pair.native.bridge_free(pair.b)
    return out


# ---------------------------------------------------------------------------
# The list passes: one world on both sides

class ListWorld:
    def __init__(self, beat):
        elf, native = G['elf'], G['native']
        self.beat = beat
        ram = (ROUTE / beat / 'eeMemory.bin').read_bytes()
        spad = (ROUTE / beat / 'scratchpad.bin').read_bytes()
        check_code(elf, ram, beat)
        self.ram0, self.spad0 = ram, spad
        self.ee = FallEE(elf, ram, spad)
        self.nram = bytearray(ram)
        self.nbuf = (C.c_uint8 * len(self.nram)).from_buffer(self.nram)
        self.native = native
        table = bytes(ram[0x24A740:0x24A740 + 0x440])
        self.b = native.lp_new(elf, len(elf), u32(ram, 0x26C5D0), table, len(table))
        assert self.b, 'lp_new'
        native.lp_ram(self.b, C.addressof(self.nbuf), len(self.nram))
        assert native.lp_bound(self.b)
        self.calls = []
        self.script = {}
        self.bd10 = 0
        self.pushes = 0      # 001A7870's +0xB0 / +0xB8 writes (both paths)
        self.knock = 0       # 001A8660's state byte writes (the knock-back path)
        self.tiny = 0        # 001A7870's |len| < 0.001 pushes (+0xB0 alone)
        self.seen = {}       # worker calls by id over every run
        self.nruns = 0       # compared runs

    def close(self):
        self.restore()
        assert bytes(self.ee.mem) == bytes(self.nram) == self.ram0, (self.beat, 'a stray RAM write')
        self.native.lp_free(self.b)
        del self.nbuf

    def restore(self):
        for lo, hi in REGIONS:
            self.ee.mem[lo:hi] = self.ram0[lo:hi]
            self.nram[lo:hi] = self.ram0[lo:hi]
        self.ee.spad[:] = self.spad0

    def poke(self, address, data):
        self.ee.mem[address:address + len(data)] = data
        self.nram[address:address + len(data)] = data

    def _call(self, e, id_, fn=0):
        a = [e.arg(k) for k in range(4)]
        if id_ == -1:
            rec = (-1, fn, a[0], a[1], a[2])
        elif id_ == QUAD_WORKER:
            rec = (id_, a[0], a[1], a[2], a[3])
        elif id_ == BD10:
            rec = (id_, 0, 0, 0, 0)
        else:
            rec = (id_, a[0], a[1], 0, 0)
        k = len(self.calls)
        self.calls.append(rec)
        # No script inside 001AA140's pair callee: 001AA140 decrements its
        # outer counter again after the inner walk, so a zeroed 0x70003B88
        # would send the original past the end of the list.
        for which, value in (() if id_ == QUAD_WORKER else self.script.get(k, ())):
            e.save(0x70003B86 if which == 0 else 0x70003B88, value & 0xFFFF, 2)
        e.ret_int(self.bd10 if id_ == BD10 else 0)

    def hooks(self, fns):
        h = self.ee.hooks
        h.clear()
        for address in PAIR_WORKERS + (QUAD_WORKER, BD10):
            h[address] = lambda e, a=address: self._call(e, a)
        for fn in fns:
            h[fn] = lambda e, f=fn: self._call(e, -1, f)

    def globals(self):
        ee, g = self.ee, Globals()
        for cursor, count, ca, cc in LISTS:
            setattr(g, cursor, ee.load(ca))
            setattr(g, count, cp.sx16(ee.load(cc, 2)))
        g.s3B86, g.s3B88 = cp.sx16(ee.load(0x70003B86, 2)), cp.sx16(ee.load(0x70003B88, 2))
        g.s3B8D = ee.load(0x70003B8D, 1)
        g.d28A9A0 = cp.sx16(ee.load(0x28A9A0, 2))
        g.d810700, g.d810702, g.d81070A = ee.load(0x810700, 1), ee.load(0x810702, 1), ee.load(0x81070A, 1)
        for k in range(4):
            g.s38A0[k] = ee.load(0x700038A0 + 4 * k)
        return g

    def behaviour_fns(self):
        """Every +0x34 word of the class-0xD list's entries (001A8660's jalr)."""
        ee = self.ee
        cursor, count = ee.load(0x275BA0), cp.sx16(ee.load(0x275BA8, 2))
        return {ee.load(ee.load(cursor + 4 * j) + 0x34) for j in range(max(0, count))} | {FAKE_FN}

    def run(self, where, which, player=PLAYER, entry=0, script=None, bd10=0, drop=0):
        """Run `which` (a pass address, P8660 or 'hooks') on both sides from
        the current (patched) RAM; compare; restore."""
        ee = self.ee
        self.script, self.bd10, self.calls = dict(script or {}), bd10, []
        self.hooks(self.behaviour_fns())
        g = self.globals()
        if drop:     # a fail-stop case: the native side alone
            code = 10 if which == 'hooks' else 9 if which == P8660 else WHICH[which]
            self.native.lp_script(self.b, 0, None, None, None, 0)
            return self.native.lp_run(self.b, code, C.byref(g), player, entry, drop), self.native.lp_fault(self.b)
        spad_before = bytes(ee.spad)
        written = []
        save = ee.save

        def guarded(address, value, size=4):
            a = address & 0xFFFFFFFF
            if not 0x7F000000 <= a < 0x7F100000:
                written.append((a, size))
            save(address, value, size)
        ee.save = guarded
        try:
            if which == 'hooks':
                for entry_addr in HOOK_ORDER:
                    ee.call(entry_addr, (player,) if entry_addr in (P9F60, P8BE0) else ())
            elif which == P8660:
                ee.call(P8660, (player, entry))
            else:
                ee.call(which, (player,) if which in (P9F60, P8BE0) else ())
        finally:
            del ee.save
        self.pushes += sum(1 for a, size in written
                           if POOL <= a < POOL_END and (a - POOL) % 0x2F0 in (0xB0, 0xB8))
        self.knock += sum(1 for a, size in written if a == PLAYER and size == 1)
        self.tiny += sum(1 for a, size in written if POOL <= a < POOL_END and (a - POOL) % 0x2F0 == 0xB0) - \
            sum(1 for a, size in written if POOL <= a < POOL_END and (a - POOL) % 0x2F0 == 0xB8)
        for a, size in written:
            ok = any(lo <= a and a + size <= hi for lo, hi in WRITABLE)
            ok = ok or any(lo <= a and a + size <= lo + n for lo, n in SPAD_WORDS)
            assert ok, (self.beat, where, 'the original wrote outside the compared state', hex(a), size)
        # Native.
        flat = [(k, w, v) for k, acts in self.script.items() for w, v in acts]
        n = len(flat)
        self.native.lp_script(self.b, n, (C.c_int * max(1, n))(*[k for k, _, _ in flat]),
                              (C.c_int * max(1, n))(*[w for _, w, _ in flat]),
                              (C.c_int16 * max(1, n))(*[cp.sx16(v & 0xFFFF) for _, _, v in flat]), bd10)
        code = 10 if which == 'hooks' else 9 if which == P8660 else WHICH[which]
        r = self.native.lp_run(self.b, code, C.byref(g), player, entry, 0)
        assert r == 0, (self.beat, where, 'native fault', r, hex(self.native.lp_fault(self.b)))
        calls = (LCall * 512)()
        count = self.native.lp_calls(self.b, calls)
        got_calls = [(calls[i].id, calls[i].a, calls[i].b, calls[i].c, calls[i].d) for i in range(min(count, 512))]
        assert got_calls == self.calls, (self.beat, where, 'worker calls', self.calls[:8], got_calls[:8])
        for lo, hi in REGIONS:
            if ee.mem[lo:hi] != self.nram[lo:hi]:
                diff = next(i for i in range(lo, hi) if ee.mem[i] != self.nram[i])
                raise AssertionError((self.beat, where, 'RAM differs', hex(diff), ee.mem[diff], self.nram[diff]))
        assert (cp.sx16(ee.load(0x70003B86, 2)), cp.sx16(ee.load(0x70003B88, 2))) == (g.s3B86, g.s3B88), \
            (self.beat, where, 'counters', ee.load(0x70003B86, 2), ee.load(0x70003B88, 2), g.s3B86, g.s3B88)
        assert [ee.load(0x700038A0 + 4 * k) for k in range(4)] == list(g.s38A0), (self.beat, where, '0x700038A0')
        spad_after = bytes(ee.spad)
        for i in range(len(spad_after)):
            if spad_after[i] != spad_before[i]:
                a = 0x70000000 + i
                assert any(lo <= a < lo + n for lo, n in SPAD_WORDS), (self.beat, where, 'spad byte', hex(a))
        for cursor, count_name, ca, cc in LISTS:
            assert (getattr(g, cursor), getattr(g, count_name)) == (ee.load(ca), cp.sx16(ee.load(cc, 2))), \
                (self.beat, where, 'list globals')
        out = len(self.calls)
        self.nruns += 1
        for c in self.calls:
            self.seen[c[0]] = self.seen.get(c[0], 0) + 1
        self.restore()
        return 0, out

    # ---- staging ----
    def set_list(self, cursor_addr, entries):
        base = LIST_BASE[cursor_addr]
        cur = base - 4 * len(entries)
        self.poke(cur, struct.pack(f'<{len(entries)}I', *entries) if entries else b'')
        self.poke(cursor_addr, struct.pack('<I', cur))
        self.poke(cursor_addr + 8, struct.pack('<h', len(entries)))

    def live_from_published(self):
        for cursor_addr, (pc, pn) in PUBLISHED.items():
            self.poke(cursor_addr, self.ram0[pc:pc + 4])
            self.poke(cursor_addr + 8, self.ram0[pn:pn + 2])


def pool_records(ram):
    return [POOL + 0x2F0 * i for i in range(0x100) if ram[POOL + 0x2F0 * i] != 0]


def run_route_lists(beat):
    """The beat's published lists as the live lists: the nine hooks in order,
    each hook on its own, and 001A8660 for each class-0xD type-1 entry."""
    w = ListWorld(beat)
    out = {'runs': 0, 'calls': 0, 'pairs_8660': 0}
    w.live_from_published()
    _, n = w.run('route hooks', 'hooks')
    out['runs'] += 1
    out['calls'] += n
    for entry in HOOK_ORDER:
        w.live_from_published()
        _, n = w.run(f'route {entry:#x}', entry)
        out['runs'] += 1
        out['calls'] += n
    ram = w.ram0
    pc, pn = PUBLISHED[0x275BA0]
    cursor, count = u32(ram, pc), s16(ram, pn)
    for j in range(max(0, count)):
        e = u32(ram, cursor + 4 * j)
        if ram[e + 3] == 1:
            w.live_from_published()
            w.run(f'route 001A8660 {e:#x}', P8660, entry=e)
            out['pairs_8660'] += 1
            out['runs'] += 1
    w.close()
    return out


def f4(v):
    return struct.pack('<f', v)


def run_synthetic_lists(item):
    """Synthetic live lists over the beat's pool records, gating bytes and
    globals randomized, capsules and radii staged in SYN, worker scripts that
    shorten the walks. Each case runs the nine hooks in order (and a random
    single pass or 001A8660)."""
    beat, count, seed = item
    w = ListWorld(beat)
    rng = random.Random(seed)
    records = pool_records(w.ram0)
    player_pos = struct.unpack_from('<3f', w.ram0, PLAYER + 0xA0)
    out = {'runs': 0, 'calls': 0, 'hits_8660': 0, 'faults': 0, 'by_id': {}, 'pushes': 0, 'knock': 0}
    types = (0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xA, 0xC, 0x10, 0x13, 0x14, 0x18, 0x1C, 0x1E, 0x1F, 0x2A, 0x50)
    for k in range(count):
        chosen = rng.sample(records, min(len(records), rng.randrange(2, 9)))
        # Per-record gating bytes.
        syn = SYN
        for i, e in enumerate(chosen):
            w.poke(e, bytes([rng.choice((0, 1, 1, 1, 2, 3))]))
            w.poke(e + 2, bytes([rng.choice((2, 2, 0xA, 0x82, 4, 0xD, 1))]))
            w.poke(e + 3, bytes([rng.choice(types)]))
            w.poke(e + 5, bytes([rng.choice((0, 9))]))
            w.poke(e + 0xD, bytes([rng.choice((0, 0, 2, 3, 4, 0xB, 1, 5, 20))]))
            w.poke(e + 0x56, struct.pack('<h', rng.choice((0, 1))))
            w.poke(e + 0x2D4, struct.pack('<I', rng.choice((0, 0x100, 0x200, 0x300, 0x400, 0x101, 0x10, 0x2, 0x4,
                                                             0x8, 0x108))))
            w.poke(e + 0x38, f4(rng.choice((0.0, 1.0, -0.0))))
            w.poke(e + 0x52, struct.pack('<H', rng.choice((0, 0, 1))))
            w.poke(e + 0x5E, bytes([rng.choice((0, 1, 3))]))
            # The 001A7870 capsule: +0x58 -> record, +0x110[slot] -> node.
            # Some records share the previous capsule and node (a zero
            # distance: 001A7870's soft-float |len| < 0.001 path).
            if i and rng.random() < 0.25:
                w.poke(e + 0x58, struct.pack('<I', cap))
                w.poke(e + 0x110 + 4 * slot, struct.pack('<I', node))
                w.poke(e, bytes([1]))
                w.poke(e + 0x5E, bytes([3]))
            else:
                cap, node = syn, syn + 0x40
                syn += 0x100
                slot = rng.choice((0, 1, 2, -1))
                w.poke(e + 0x58, struct.pack('<I', rng.choice((cap, cap, cap, 0))))
                w.poke(cap, struct.pack('<I', rng.choice((1, 1, 0))))
                w.poke(cap + 6, bytes([rng.choice((1, 2, 3))]))
                w.poke(cap + 7, bytes([slot & 0xFF]))
                w.poke(cap + 0xA, struct.pack('<h', rng.choice((-2, -2, -2, 0, 1, -1))))
                w.poke(cap + 0xC, struct.pack('<3f', *[rng.uniform(-1, 1) for _ in range(3)]))
                w.poke(cap + 0x18, struct.pack('<2f', rng.uniform(0.5, 6), rng.uniform(0.5, 6)))
                w.poke(e + 0x110 + 4 * slot, struct.pack('<I', node))
                centre = (rng.uniform(0, 4), rng.uniform(0, 4), rng.uniform(0, 4))
                w.poke(node + 0xC0, struct.pack('<3f', *centre))
            # The 001A8660 radii and position near the player.
            radius = syn
            syn += 0x10
            w.poke(radius, struct.pack('<2f', rng.uniform(1, 8), rng.uniform(4, 30)))
            w.poke(e + 0x30, struct.pack('<I', radius))
            w.poke(e + 0x34, struct.pack('<I', FAKE_FN))
            near = [player_pos[j] + rng.uniform(-8, 8) for j in range(3)]
            w.poke(e + 0xB0, struct.pack('<3f', *near))
        player_radius = syn
        w.poke(player_radius, struct.pack('<2f', rng.uniform(1, 5), rng.uniform(10, 25)))
        w.poke(PLAYER + 0x30, struct.pack('<I', player_radius))
        w.poke(PLAYER, bytes([rng.choice((1, 1, 0, 2))]))
        w.poke(PLAYER + 0xA4, f4(rng.choice((49.5, 50.0, 60.0, player_pos[1], 49.9999))))
        # Globals.
        w.poke(0x28A9A0, struct.pack('<h', rng.choice((0, 0, 0, 1))))
        w.ee.spad[0x3B8D] = rng.choice((0, 0, 0, 1))
        w.poke(0x810700, bytes([rng.choice((0, 11))]))
        w.poke(0x810702, bytes([rng.choice((5, 0))]))
        w.poke(0x81070A, bytes([rng.choice((0, 1))]))
        # Each list gets entries shaped for the passes that read it (most of
        # the time; the rest keep the random bytes above).
        for cursor in LIST_BASE:
            size = rng.randrange(0, 5)
            entries = [rng.choice(chosen) for _ in range(size)]
            for e in entries:
                if rng.random() < 0.3:
                    continue
                w.poke(e, bytes([1]))
                if cursor == 0x275BA0:        # class 0xD: 001A8BE0, 001A8DA0, 001A9000, 001A97B0
                    w.poke(e + 3, bytes([rng.choice((1, 3, 5, 5, 6, 3))]))
                    w.poke(e + 0xD, bytes([rng.choice((0, 0, 1, 2, 0xB, 3))]))
                    w.poke(e + 0x56, struct.pack('<h', rng.choice((0, 1, 1))))
                elif cursor == 0x275B90:      # class 2: 001A9D20, 001A9F60, 001AA140, 001A7870, 001A97B0, 001A9B10
                    w.poke(e + 2, bytes([rng.choice((2, 2, 0xA, 0x22))]))
                    w.poke(e + 3, bytes([rng.choice((0, 0, 0, 0, 0, 1, 1, 3, 4, 8, 13, 19, 20))]))
                    w.poke(e + 5, bytes([rng.choice((0, 0, 9))]))
                    w.poke(e + 0xD, bytes([rng.choice((3, 3, 0, 2))]))
                    w.poke(e + 0x2D4, struct.pack('<I', rng.choice((0, 0x100, 0x200, 0x300, 0x400, 0x2, 0x4, 0x8,
                                                                     0x108))))
                elif cursor == 0x275B80:      # class 4: 001A9000, 001A9B10
                    w.poke(e + 3, bytes([rng.choice((7, 7, 0xA, 0x1C, 0x1E, 0x6, 0x2A, 0x50, 0x1F))]))
                    w.poke(e + 0x38, f4(rng.choice((0.0, 2.5, 2.5))))
            w.set_list(cursor, entries)
        script = {}
        for _ in range(rng.choice((0, 0, 1, 2))):
            script.setdefault(rng.randrange(0, 6), []).append((rng.choice((0, 1)), 0))   # the callees only zero them
        bd10 = rng.choice((0, 1))
        # Snapshot the staged RAM so a second run starts from the same state.
        staged = {lo: bytes(w.ee.mem[lo:hi]) for lo, hi in REGIONS}
        spad3B8D = w.ee.spad[0x3B8D]
        _, n = w.run(f'case {k} hooks', 'hooks', script=script, bd10=bd10)
        out['runs'] += 1
        out['calls'] += n
        # Second run from the same staging: one pass alone, or 001A8660 on a
        # record (overlap forced for some).
        for lo, hi in REGIONS:
            w.poke(lo, staged[lo])
        w.ee.spad[0x3B8D] = spad3B8D
        if rng.random() < 0.4:
            e = rng.choice(chosen)
            if rng.random() < 0.6:
                w.poke(e + 0xB0, struct.pack('<3f', *[player_pos[j] + rng.uniform(-0.5, 0.5) for j in range(3)]))
            _, n = w.run(f'case {k} 001A8660', P8660, entry=e, bd10=bd10)
            out['hits_8660'] += int(any(c[0] == -1 for c in w.calls))
        else:
            which = rng.choice(HOOK_ORDER)
            _, n = w.run(f'case {k} {which:#x}', which, script=script, bd10=bd10)
        out['runs'] += 1
        out['calls'] += n
    # Staged cases every world runs: the knock-back path of 001A8660 (the
    # 0021BD10 gate at +0xD = 0xB, both speed tables, both stores), 001A9000's
    # 001A8F40 branch, 001A97B0's 50.0 gate and handlers, 001AA140's status.
    e1, e2 = records[0], records[-1]
    radius = SYN
    staged0 = w.nruns
    for param, pick in ((0xB, 0), (3, 1), (4, 0), (0x14, 1)):   # both speed tables, both stores
        w.poke(radius, struct.pack('<2f', 4.0, 20.0))
        w.poke(e1 + 0x30, struct.pack('<I', radius))
        w.poke(PLAYER + 0x30, struct.pack('<I', radius))
        w.poke(e1 + 0x34, struct.pack('<I', FAKE_FN))
        w.poke(e1 + 0xB0, struct.pack('<3f', *[player_pos[j] + 0.25 for j in range(3)]))
        w.poke(e1 + 0xD, bytes([param]))
        w.poke(PLAYER, bytes([1]))
        w.poke(0x81070A, bytes([pick]))
        w.run(f'staged 001A8660 +0xD {param:#x} table {pick}', P8660, entry=e1, bd10=1)
    # 001A8660's circle and height tests at equality (0x1A86AC: dist <= ra +
    # rb; 0x1A8718: |gap| <= reach), every value exact: the player at x/z
    # (10, 20), radius 2, height 4; the entry radius 3, height 6. dist = 5
    # (dx 3, dz 4) with the heights level; then dist 0 with the entry's
    # +0xB4 at 7 or -3 (gap +-5 = reach); and just beyond (the entry's x at
    # 6.99, its +0xB4 one ulp above 7).
    calls_8660 = []
    for name, ex, ez, ey in (('dist == ra + rb', 7.0, 16.0, 0.0), ('dist beyond', 6.99, 16.0, 0.0),
                             ('gap == reach', 10.0, 20.0, 7.0), ('gap == -reach', 10.0, 20.0, -3.0),
                             ('gap beyond', 10.0, 20.0, ulps(7.0, 1))):
        w.poke(radius, struct.pack('<2f', 2.0, 4.0))
        w.poke(radius + 8, struct.pack('<2f', 3.0, 6.0))
        w.poke(PLAYER + 0x30, struct.pack('<I', radius))
        w.poke(e1 + 0x30, struct.pack('<I', radius + 8))
        w.poke(e1 + 0x34, struct.pack('<I', FAKE_FN))
        w.poke(PLAYER + 0xA0, struct.pack('<3f', 10.0, 0.0, 20.0))
        w.poke(e1 + 0xB0, struct.pack('<3f', ex, ey, ez))
        w.poke(PLAYER, bytes([0]))
        w.run(f'staged 001A8660 {name}', P8660, entry=e1)
        calls_8660.append(len(w.calls))
    assert calls_8660 == [1, 0, 1, 1, 0], ('001A8660 equality staging', calls_8660)
    # 001A9000's status tests: the outer entry must have status 1, and so
    # must the inner one (0x1A90F0 tests == 1, so 2 and 3 are skipped).
    calls_9000 = []
    inner_type = rng.choice((0xA, 0xC, 0x18, 0x2A))
    for outer_status, inner_status in ((1, 1), (1, 2), (1, 0), (1, 3), (2, 1), (0, 1)):
        w.poke(e1, bytes([outer_status])); w.poke(e1 + 3, bytes([5])); w.poke(e1 + 0xD, bytes([0]))
        w.poke(e2, bytes([inner_status])); w.poke(e2 + 3, bytes([inner_type]))
        w.set_list(0x275BA0, [e1])
        w.set_list(0x275B80, [e2])
        w.run(f'staged 001A9000 status {outer_status} inner status {inner_status}', P9000)
        calls_9000.append(len(w.calls))
    assert calls_9000 == [1, 0, 0, 0, 0, 0], ('001A9000 status staging', calls_9000)
    w.poke(e1, bytes([1])); w.poke(e1 + 3, bytes([5])); w.poke(e1 + 0xD, bytes([0]))
    w.poke(e2, bytes([1])); w.poke(e2 + 3, bytes([inner_type]))
    w.set_list(0x275BA0, [e1])
    w.set_list(0x275B80, [e2, e1])
    w.run('staged 001A9000', P9000)
    # 001A97B0's class-1 inner gate: the player's +0xA4 = 50.0 edge, the
    # inner +5 == 9 exclusion (only with +0xD 3), and its three handlers
    # (outer type 5, 3 with +0x56 set, 6 with +0xD 2).
    for y, outer, b5, bparam in ((49.5, 5, 0, 3), (50.0, 5, 0, 3), (50.0, 3, 0, 3), (50.0, 6, 0, 3),
                                 (60.0, 5, 9, 3), (60.0, 5, 9, 0), (49.5, 5, 9, 0)):
        w.poke(e1, bytes([1])); w.poke(e1 + 3, bytes([outer]))
        w.poke(e1 + 0xD, bytes([2 if outer == 6 else 0])); w.poke(e1 + 0x56, struct.pack('<h', 1))
        w.poke(e2, bytes([1])); w.poke(e2 + 2, bytes([2])); w.poke(e2 + 3, bytes([1]))
        w.poke(e2 + 5, bytes([b5])); w.poke(e2 + 0xD, bytes([bparam]))
        w.poke(PLAYER + 0xA4, f4(y))
        w.set_list(0x275BA0, [e1])
        w.set_list(0x275B90, [e2])
        w.run(f'staged 001A97B0 y {y} outer {outer} +5 {b5} +0xD {bparam}', P97B0)
    # 001A97B0's inner type switch, every type 0..21 (outer type 5).
    for btype in range(22):
        w.poke(e1, bytes([1])); w.poke(e1 + 3, bytes([5])); w.poke(e1 + 0xD, bytes([0]))
        w.poke(e2, bytes([1])); w.poke(e2 + 2, bytes([2])); w.poke(e2 + 3, bytes([btype]))
        w.poke(e2 + 5, bytes([0])); w.poke(e2 + 0xD, bytes([0]))
        w.set_list(0x275BA0, [e1])
        w.set_list(0x275B90, [e2])
        w.run(f'staged 001A97B0 inner type {btype}', P97B0)
    # 001AA140's pair test on the +0 status byte (2 excludes an entry), and
    # on each bit of the +0x2D4 low nibble (and bits above it).
    for status, word in ((1, 0), (2, 0), (3, 0), (1, 0x1), (1, 0x2), (1, 0x4), (1, 0x8), (1, 0x10), (1, 0x108),
                         (1, 0x100)):
        for e in (e1, e2):
            w.poke(e + 2, bytes([2])); w.poke(e + 3, bytes([0])); w.poke(e + 0x2D4, struct.pack('<I', 0))
        w.poke(e1, bytes([status])); w.poke(e2, bytes([1]))
        w.poke(e1 + 0x2D4, struct.pack('<I', word))
        w.set_list(0x275B90, [e2, e1, e2])
        w.run(f'staged 001AA140 status {status} +0x2D4 {word:#x}', PA140)
    # 001A8660's 0x70003B86 = 0 ends 001A8BE0's walk: an overlapping type-1
    # entry followed by an active type-1 / 3 / 5 entry (the player's state
    # byte 1 takes the knock-back path first, 0 skips it).
    for follow, state in ((1, 1), (3, 0), (5, 1), (3, 1)):
        for e, kind in ((e1, 1), (e2, follow)):
            w.poke(radius, struct.pack('<2f', 4.0, 20.0))
            w.poke(e + 0x30, struct.pack('<I', radius))
            w.poke(e + 0x34, struct.pack('<I', FAKE_FN))
            w.poke(e + 0xB0, struct.pack('<3f', *[player_pos[j] + 0.25 for j in range(3)]))
            w.poke(e, bytes([1])); w.poke(e + 3, bytes([kind])); w.poke(e + 0xD, bytes([0]))
        w.poke(PLAYER + 0x30, struct.pack('<I', radius))
        w.poke(PLAYER, bytes([state]))
        w.poke(0x28A9A0, struct.pack('<h', 0))
        w.ee.spad[0x3B8D] = 0
        w.set_list(0x275BA0, [e1, e2])
        _, n = w.run(f'staged 001A8BE0 overlap then type {follow} state {state}', P8BE0)
        assert n == 1, ('the overlap must end the walk before the second entry', follow, w.calls)
    # 001A7870 at an overlap of exactly 0 (radii sum = distance) on an inner
    # +0xB0 of -0.0: the push still runs (c.lt.s over, 0) and -0.0 + 0.0
    # stores +0.0. Both paths: len 2 (the division path) and 2^-11 (< 0.001,
    # the +0xB0-only path).
    for radius_v, dist in ((1.0, 2.0), (2.0 ** -12, 2.0 ** -11)):
        for e, (cap, node, cx) in ((e1, (SYN + 0x800, SYN + 0x900, 0.0)), (e2, (SYN + 0xA00, SYN + 0xB00, dist))):
            w.poke(e, bytes([1])); w.poke(e + 0x5E, bytes([1])); w.poke(e + 0x52, struct.pack('<H', 0))
            w.poke(e + 0x58, struct.pack('<I', cap)); w.poke(e + 0x110, struct.pack('<I', node))
            w.poke(cap, struct.pack('<I', 1)); w.poke(cap + 6, bytes([1, 0]))
            w.poke(cap + 0xA, struct.pack('<h', -2))
            w.poke(cap + 0xC, struct.pack('<5f', 0.0, 0.0, 0.0, radius_v, 5.0))
            w.poke(node + 0xC0, struct.pack('<3f', cx, 0.0, 0.0))
            w.poke(e + 0xB0, struct.pack('<3f', -0.0, 0.0, -0.0))
        w.set_list(0x275B90, [e1, e2])
        w.run(f'staged 001A7870 zero overlap len {dist}', P7870)
    # 001A7870's capsule y-extent tests at equality: the outer capsule spans
    # y -0.5..2.5 (node +0xC4 0.25, offset 0.75, half 1.5); the inner one
    # (offset 0, half 1.5) sits at +0xC4 4.0, so its bottom equals the outer
    # top (0x1A7A34, hi < glo is false), or at -2.0, so its top equals the
    # outer bottom (0x1A7A44, lo <= ghi holds); every sum is exact.
    # Both push; one ulp further out neither does.
    # Then, on the pushing 'hi == glo' pair: pass 1's -2 tag test is a
    # halfword (tags 0x00FE and 0x01FE on either record mark nothing), and
    # the inner +0x52 test is bit 0 alone (2 pushes, 1 and 3 do not).
    y_edges = []
    for name, c4, tag1, tag2, h52 in (('hi == glo', 4.0, -2, -2, 0), ('lo == ghi', -2.0, -2, -2, 0),
                                      ('hi < glo', ulps(4.0, 1), -2, -2, 0), ('ghi < lo', -ulps(2.0, 1), -2, -2, 0),
                                      ('inner tag 0x00FE', 4.0, -2, 0xFE, 0), ('inner tag 0x01FE', 4.0, -2, 0x1FE, 0),
                                      ('outer tag 0x00FE', 4.0, 0xFE, -2, 0), ('inner +0x52 = 2', 4.0, -2, -2, 2),
                                      ('inner +0x52 = 1', 4.0, -2, -2, 1), ('inner +0x52 = 3', 4.0, -2, -2, 3)):
        for e, (cap, node, cx, cy, off), tag, h in ((e1, (SYN + 0xC00, SYN + 0xD00, 0.0, 0.25, 0.75), tag1, 0),
                                                    (e2, (SYN + 0xE00, SYN + 0xF00, 0.5, c4, 0.0), tag2, h52)):
            w.poke(e, bytes([1])); w.poke(e + 0x5E, bytes([1])); w.poke(e + 0x52, struct.pack('<H', h))
            w.poke(e + 0x58, struct.pack('<I', cap)); w.poke(e + 0x110, struct.pack('<I', node))
            w.poke(cap, struct.pack('<I', 1)); w.poke(cap + 6, bytes([1, 0]))
            w.poke(cap + 0xA, struct.pack('<h', tag))
            w.poke(cap + 0xC, struct.pack('<5f', 0.0, off, 0.0, 1.0, 1.5))
            w.poke(node + 0xC0, struct.pack('<3f', cx, cy, 0.0))
            w.poke(e + 0xB0, struct.pack('<3f', 1.0, 0.0, 1.0))
        w.set_list(0x275B90, [e1, e2])
        before = w.pushes
        w.run(f'staged 001A7870 y extent {name}', P7870)
        y_edges.append((name, w.pushes > before))
    assert y_edges == [('hi == glo', True), ('lo == ghi', True), ('hi < glo', False), ('ghi < lo', False),
                       ('inner tag 0x00FE', False), ('inner tag 0x01FE', False), ('outer tag 0x00FE', False),
                       ('inner +0x52 = 2', True), ('inner +0x52 = 1', False), ('inner +0x52 = 3', False)], \
        ('001A7870 y-extent / tag / +0x52 staging', y_edges)
    # 001AAD00's hook order: 001A7870 runs before 001A8BE0, so 001A8660
    # reads an entry's +0xB0 after the capsule push. e2 is in the class-2
    # list behind e1 (capsules 0.5 apart in x, radii 1: e2's +0xB0 is pushed
    # by +1.5) and in the class-0xD list as an active type-1 entry. The
    # player sits at x/z (10, 20) with radius 2, e2 at (4, 20) with radius
    # 3: 6 apart before the push, 4.5 after, so the behaviour runs only in
    # the original order (asserted).
    w.poke(radius, struct.pack('<2f', 2.0, 4.0))
    w.poke(radius + 8, struct.pack('<2f', 3.0, 6.0))
    for e, (cap, node, cx) in ((e1, (SYN + 0xC00, SYN + 0xD00, 0.0)), (e2, (SYN + 0xE00, SYN + 0xF00, 0.5))):
        w.poke(e, bytes([1])); w.poke(e + 0x5E, bytes([1])); w.poke(e + 0x52, struct.pack('<H', 0))
        w.poke(e + 0x58, struct.pack('<I', cap)); w.poke(e + 0x110, struct.pack('<I', node))
        w.poke(cap, struct.pack('<I', 1)); w.poke(cap + 6, bytes([1, 0]))
        w.poke(cap + 0xA, struct.pack('<h', -2))
        w.poke(cap + 0xC, struct.pack('<5f', 0.0, 0.0, 0.0, 1.0, 1.5))
        w.poke(node + 0xC0, struct.pack('<3f', cx, 0.0, 0.0))
    w.poke(e2 + 3, bytes([1])); w.poke(e2 + 0xD, bytes([0]))
    w.poke(e2 + 0xB0, struct.pack('<3f', 4.0, 0.0, 20.0))
    w.poke(e2 + 0x30, struct.pack('<I', radius + 8))
    w.poke(e2 + 0x34, struct.pack('<I', FAKE_FN))
    w.poke(PLAYER, bytes([0]))
    w.poke(PLAYER + 0x30, struct.pack('<I', radius))
    w.poke(PLAYER + 0xA0, struct.pack('<3f', 10.0, 0.0, 20.0))
    w.poke(0x28A9A0, struct.pack('<h', 0))
    w.ee.spad[0x3B8D] = 0
    w.set_list(0x275B90, [e1, e2])
    w.set_list(0x275BA0, [e2])
    w.run('staged hook order: 001A7870 push then 001A8660', 'hooks')
    assert sum(1 for c in w.calls if c[0] == -1) == 1, ('hook order staging', w.calls)
    # 001A9B10's +0x2D4 >> 8 window (1..3), against one class-4 type-7 entry.
    for word in (0, 0x100, 0x200, 0x300, 0x400, 0xFFFFFF00, 0x3FF):
        w.poke(e1, bytes([1])); w.poke(e1 + 3, bytes([0])); w.poke(e1 + 0x2D4, struct.pack('<I', word))
        w.poke(e2, bytes([1])); w.poke(e2 + 3, bytes([7])); w.poke(e2 + 0x38, f4(2.5))
        w.set_list(0x275B90, [e1])
        w.set_list(0x275B80, [e2])
        w.run(f'staged 001A9B10 +0x2D4 {word:#x}', P9B10)
    # The class-byte masks (& 0x1F) of 001A9F60, 001AA140 and 001A97B0.
    for cls in (2, 0x12, 0x22, 0x42, 0x0A, 0x1A, 0x2A, 0x4A, 0xE2):
        w.poke(e1, bytes([1])); w.poke(e1 + 2, bytes([cls])); w.poke(e1 + 3, bytes([0]))
        w.poke(e1 + 0x2D4, struct.pack('<I', 0))
        w.poke(e2, bytes([1])); w.poke(e2 + 2, bytes([2])); w.poke(e2 + 3, bytes([0]))
        w.poke(e2 + 0x2D4, struct.pack('<I', 0))
        w.poke(0x28A9A0, struct.pack('<h', 0))
        w.ee.spad[0x3B8D] = 0
        w.set_list(0x275B90, [e1, e2])
        w.run(f'staged 001A9F60 class {cls:#x}', P9F60)
        w.poke(e1, bytes([1])); w.poke(e1 + 2, bytes([cls])); w.poke(e1 + 3, bytes([0]))
        w.poke(e1 + 0x2D4, struct.pack('<I', 0))
        w.poke(e2, bytes([1])); w.poke(e2 + 2, bytes([2])); w.poke(e2 + 3, bytes([0]))
        w.poke(e2 + 0x2D4, struct.pack('<I', 0))
        w.set_list(0x275B90, [e2, e1])
        w.run(f'staged 001AA140 class {cls:#x}', PA140)
        w.poke(e1, bytes([1])); w.poke(e1 + 2, bytes([cls])); w.poke(e1 + 3, bytes([0]))
        w.poke(e2, bytes([1])); w.poke(e2 + 3, bytes([5])); w.poke(e2 + 0xD, bytes([0]))
        w.set_list(0x275BA0, [e2])
        w.set_list(0x275B90, [e1])
        w.run(f'staged 001A97B0 class {cls:#x}', P97B0)
    out['runs'] += w.nruns - staged0
    # Fail-stop: an unbound worker faults before any write; an explicitly
    # unported worker faults when reached.
    r, fault = w.run('drop 001A9C40', P9D20, drop=1)
    assert r == -1 and fault == P9D20, ('unbound 001A9C40 must fault at entry', r, hex(fault))
    assert bytes(w.nram) == w.ram0, 'an entry fault wrote'
    r, fault = w.run('drop normalize', P8BE0, drop=2)
    assert r == -1 and fault == P8BE0, ('unbound 00102760 must fault at entry', r, hex(fault))
    # A reached unported callee faults with the address of its call site.
    w.poke(e1, bytes([1])); w.poke(e1 + 3, bytes([3])); w.poke(e1 + 0xD, bytes([0]))
    w.poke(0x28A9A0, struct.pack('<h', 0))
    w.ee.spad[0x3B8D] = 0
    w.set_list(0x275BA0, [e1])
    r, fault = w.run('unported 001A8840', P8BE0, drop=3)
    assert r == -1 and fault == 0x1A8C94, ('unported 001A8840 must fault at its call', r, hex(fault))
    w.restore()
    for e in (e1, e2):
        w.poke(e, bytes([1])); w.poke(e + 2, bytes([2])); w.poke(e + 3, bytes([0]))
        w.poke(e + 0x2D4, struct.pack('<I', 0))
    w.set_list(0x275B90, [e2, e1])
    r, fault = w.run('unported 001AA000', PA140, drop=4)
    assert r == -1 and fault == 0x1AA23C, ('unported 001AA000 must fault at its call', r, hex(fault))
    w.restore()
    for drop, site in ((5, 0x1A875C), (6, 0x1A8734)):
        w.poke(radius, struct.pack('<2f', 4.0, 20.0))
        w.poke(e1 + 0x30, struct.pack('<I', radius))
        w.poke(PLAYER + 0x30, struct.pack('<I', radius))
        w.poke(e1 + 0x34, struct.pack('<I', FAKE_FN))
        w.poke(e1 + 0xB0, struct.pack('<3f', *[player_pos[j] + 0.25 for j in range(3)]))
        w.poke(e1 + 0xD, bytes([0xB]))
        w.poke(PLAYER, bytes([1]))
        r, fault = w.run(f'unported drop {drop}', P8660, entry=e1, drop=drop)
        assert r == -1 and fault == site, ('an unported 001A8660 callee must fault at its call', drop, r, hex(fault))
        w.restore()
    out['faults'] += 6
    out['pushes'], out['knock'], out['tiny'], out['by_id'] = w.pushes, w.knock, w.tiny, dict(w.seen)
    w.restore()
    w.close()
    return out


# ---------------------------------------------------------------------------

def main():
    elf = read_elf()
    G['elf'] = elf
    G['native'] = build_native()
    emcl, verified, installed = export_emcl()
    G['emcl'] = emcl
    full = reference_mode.FULL
    rows_all = {beat: cp.feet_rows(beat) for beat in BEATS}
    walk_items = []
    total_rows = sum(len(r) for r in rows_all.values())
    picked_rows = 0
    for n, beat in enumerate(BEATS):
        rows = rows_all[beat]
        chosen = rows if full else rows[:: max(1, len(rows) // 2)][:2]
        picked_rows += len(chosen)
        walk_items.append((beat, chosen, reference_mode.pick(60, 8 if n % 5 == 0 else 4), 1000 + n))
    walk = reference_mode.parallel_map(run_walk_beat, walk_items, cost=lambda it: len(it[1]) + 4 * it[2])
    route = reference_mode.parallel_map(run_route_lists, BEATS)
    syn_items = [(beat, reference_mode.pick(80, 8 if n % 3 == 0 else 4), 2000 + n) for n, beat in enumerate(BEATS)]
    syn = reference_mode.parallel_map(run_synthetic_lists, syn_items, cost=lambda it: it[1])

    wcases = sum(o['cases'] for o in walk)
    ground = [sum(o['ground'][i] for o in walk) for i in range(2)]
    column = [sum(o['column'][i] for o in walk) for i in range(2)]
    ladder = {}
    for o in walk:
        for key, v in o['ladder'].items():
            ladder[key] = ladder.get(key, 0) + v
    static = sum(o['static'] for o in walk)
    static_hits = sum(o['static_hits'] for o in walk)
    direct = {key: [sum(o[key][i] for o in walk) for i in range(2)] for key in ('e280', 'e930', 'a3980')}
    assert all(v[0] and v[1] for v in direct.values()) and static_hits, ('coverage: walker hits', direct, static_hits)
    assert ground[1] > 0 and column[1] > 0 and column[0] > 0, ('coverage: hits and misses', ground, column)
    assert all(ladder.get(r, 0) > 0 for r in (0, 2, 4)), ('coverage: ladder results', ladder)
    edge, nudge0, attr_edge = ([sum(o[key][i] for o in walk) for i in range(2)] for key in ('edge', 'nudge0', 'attr_edge'))
    kind_edge_hits = sum(o['kind_edge_hits'] for o in walk)
    assert edge[1] > 0 and nudge0[0] > 0 and attr_edge[0] > 0 and attr_edge[1] > 0 and kind_edge_hits > 0, \
        ('coverage: boundary cases', edge, nudge0, attr_edge, kind_edge_hits)
    flag = {key: sum(o[key] for o in walk) for key in ('flag_hits', 'flag_gated', 'flag_ladder', 'flag_ladder_gated',
                                                       'flag_ladder_cls')}
    assert all(flag.values()), ('coverage: 0x20000000 static words with a nonzero query class', flag)
    uid_ff = [sum(o['uid_ff'][i] for o in walk) for i in range(2)]
    flat_edge = [sum(o['flat_edge'][i] for o in walk) for i in range(2)]
    assert uid_ff[0] > 0 and uid_ff[1] == uid_ff[0], ('coverage: a uid 0xFF owner skipped where it would hit', uid_ff)
    round_hits = [sum(o['round'][i] for o in walk) for i in range(2)]
    gate_edge = sum(o['gate_edge'] for o in walk)
    assert round_hits[0] == 0 and round_hits[1] > 0, ('pass 1 tests 0x4000 prims, steps over 0x8000', round_hits)
    round2 = sum(o['round2'] for o in walk)
    prim_stage = [sum(o['prim_stage'][i] for o in walk) for i in range(2)]
    resplit = sum(o['resplit'] for o in walk)
    assert round2 > 0, ('coverage: pass 2 hits a 0x8000 prim', round2)
    assert prim_stage[0] > 0 and prim_stage[1] == prim_stage[0], ('staged prim lists', prim_stage)
    assert resplit > 0, ('coverage: pass 1 y re-split gating a later hull', resplit)
    p1_break = sum(o['p1_break'] for o in walk)
    resplit2 = sum(o['resplit2'] for o in walk)
    assert resplit2 > 0, ('coverage: pass 2 y re-split between two owners', resplit2)
    assert p1_break > 0, ('coverage: pass 1 stopping at an unflagged word', p1_break)
    prim_names = set().union(*(o['prim_names'] for o in walk))
    assert len(prim_names) == 38, ('coverage: every staged prim list in both passes', sorted(prim_names))
    assert gate_edge > 0, ('coverage: 001A3980 hull gate y tests at equality', gate_edge)
    owner_gates = [sum(o['owner_gates'][i] for o in walk) for i in range(2)]
    assert owner_gates[1] > 0 and owner_gates[0] == 0, ('coverage: pass-2 owner gates change the hit', owner_gates)
    assert flat_edge[1] > 0, ('coverage: 0019F330 with h exactly 1e-4', flat_edge)
    attr78 = [sum(o['attr78'][i] for o in walk) for i in range(3)]
    edge930 = [sum(o['edge930'][i] for o in walk) for i in range(2)]
    assert edge930[1] > 0 and edge930[0] > 0, ('coverage: 0019E930 rank-edge segments', edge930)
    assert attr78[1] > attr78[0] and attr78[1] > attr78[2], ('0019E280 hits only attr 0x78 nodes', attr78)
    # The x / z maximum rank bounds of 0019E280 (+0x0E, +0x16): see
    # odd_bound_margin. The claim in docs/COLL_LIST_PASSES.md holds only
    # while this margin stays above the ring tolerance.
    world0 = cp.World(BEATS[0], elf)
    n78 = [i for i in range(world0.node_count) if world0.ram[world0.nodes + 64 * i + 0x1A] == 0x78]
    margin = odd_bound_margin(world0, world0.ram, n78)
    assert margin > EPS_EDGE, ('an x / z maximum rank bound at equality could reach an accepted node', margin)
    rruns = sum(o['runs'] for o in route)
    pairs = sum(o['pairs_8660'] for o in route)
    assert pairs > 0, 'no route beat reached 001A8660'
    sruns = sum(o['runs'] for o in syn)
    by_id = {}
    for o in syn:
        for key, v in o['by_id'].items():
            by_id[key] = by_id.get(key, 0) + v
    hits = by_id.get(-1, 0)          # 001A8660 overlaps: its +0x34 behaviour ran
    pushes, tiny, knock = (sum(o[k] for o in syn) for k in ('pushes', 'tiny', 'knock'))
    wanted = set(PAIR_WORKERS) | {QUAD_WORKER, BD10, -1}
    assert set(by_id) == wanted, ('coverage: workers never reached', sorted(hex(x) for x in wanted - set(by_id)))
    assert hits > 0 and knock > 0, ('coverage: 001A8660 overlaps', hits, knock)
    assert pushes > tiny > 0, ('coverage: 001A7870 push paths', pushes, tiny)
    print(f'EMCL export verified against {verified} captured RAM images ({installed})')
    print(f'walkers: {wcases} cases over {len(BEATS)} beats ({picked_rows} route rows); ground hits/misses '
          f'{ground[1]}/{ground[0]}, ladder results {dict(sorted(ladder.items()))}, 0019F330 crossings/rejects '
          f'{column[1]}/{column[0]}, direct walker hits/misses '
          f'{", ".join(f"{k} {v[1]}/{v[0]}" for k, v in direct.items())}, {static} static-cell cases '
          f'({static_hits} hits, {kind_edge_hits} at kinds 0x1E / 0x59) PASS')
    print(f'boundaries: 0019E280 rank-edge segments hits/misses {edge[1]}/{edge[0]}, attr 0x77/0x78/0x79 ground hits {"/".join(map(str, attr78))}, '
          f'0019E930 rank-edge segments hits/misses {edge930[1]}/{edge930[0]}, '
          f'box.y = +-0.0 probes '
          f'{nudge0[0] + nudge0[1]}, 0019E930 attr-edge hits/misses {attr_edge[1]}/{attr_edge[0]}, '
          f'x / z max-bound margin {margin:.3e} > 1e-5 PASS')
    print(f'001A3980: 0x20000000 static words hit with a nonzero query class {flag["flag_hits"]} times '
          f'({flag["flag_gated"]} gated by class 0), 0019BA80 through them from a class-4 owner '
          f'{flag["flag_ladder"]} hits ({flag["flag_ladder_gated"]} differ from the player); uid-0xFF owner '
          f'staged in {uid_ff[0]} worlds (the skip changed the result in {uid_ff[1]}), status / class / uid-count '
          f'gates changed it {owner_gates[1]} times, AABB y edges hit {gate_edge} times; staged prim lists '
          f'{prim_stage[1]}/{prim_stage[0]} as expected (on-segment 0x8000/0x4000 hit in pass 1 '
          f'{round_hits[0]}/{round_hits[1]}, 0x8000 in pass 2 {round2}), stacked-hull re-splits {resplit} ({resplit2} owner / owner), '
          f'pass-1 stop cases {p1_break}, '
          f'patched player class hits {flag["flag_ladder_cls"]}; 0019F330 at h == 1e-4 '
          f'{flat_edge[1]} of {sum(flat_edge)} flat-edge planes PASS')
    print(f'route lists: {rruns} runs over {len(BEATS)} beats (published lists as live lists), '
          f'{pairs} 001A8660 route pairs PASS')
    print(f'synthetic lists: {sruns} runs, all {len(by_id)} worker kinds reached ({sum(by_id.values())} calls), '
          f'{hits} 001A8660 overlaps ({knock} knock-backs), 001A7870 pushes {pushes} ({tiny} |len| < 0.001), '
          f'fail-stop checks PASS')
    reference_mode.banner(reference_mode.part(picked_rows, total_rows, 'route rows'),
                          f'{wcases} walker cases', f'{rruns + sruns} list-pass runs')


if __name__ == '__main__':
    main()
