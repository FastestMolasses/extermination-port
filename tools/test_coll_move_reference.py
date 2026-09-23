#!/usr/bin/env python3
"""Execute the original horizontal move/sweep walkers and compare
src/game/em_coll_move_original.c (docs/COLL_MOVE.md).

The original instructions run, unmodified, over captured AREA11 RAM (the PCSX2
route beats, docs/FIRST_LEVEL_ROUTE.md):

  001A4830  0x8000/0x4000 prims (with the SDK sqrt 0011E748)
  001A4D10  0x2000 face prims
  001A4030  0x1000 n-gon prims
  0019FE50  the cell walker, both passes
  0019AD00  the move probe       0019AFE0  the sweep

The interpreter is the shared EE of test_player_slide_reference.py with its
COP1 and VU0 macro replaced by tools/ee_float_model.py (docs/EE_FLOAT_MODEL.md
section 5a: the shared COP1 has no add/sub pre-trim and truncates DIV.S; the
shared file is not edited). Every other instruction is the shared one.

What is compared, bit for bit, on every case:
  - the return value;
  - every scratchpad field the walkers own (0x70003190..0x700031D8, 0x700030CA,
    0x700030D4..DC, 0x7000324E, 0x70003254, 0x70003B88, 0x70003680..8C), and
    the WHOLE 16 KB scratchpad: the original's final scratchpad must equal the
    initial one plus the native worker executions' stores plus the native
    fields;
  - the query actor's +0xB0/+0xB8 (mask bit 31), and that nothing else in RAM
    is written.

The untranslated callees the walkers reach are workers on the native side
(0019CB60 the grid pass, 001A6440 / 001A7280 the hull locks, 0011E748 sqrt).
Each native worker call runs that ORIGINAL routine as instructions over the
same RAM, with the native scratch loaded into the scratchpad, and hands its
scratch back: so the native walker is compared against the original
end to end, and the call sequence (routine, arguments, scratch at entry) must
equal the one the original makes.

Worlds: the RAM + scratchpad snapshots of route beats 04 (the 05_boxes
start: crates, elevator), 05 (the 06_hill_slide start) and 08 (the truck
crossing: the truck's n-gon hull). Route mode (EM_TEST_FULL=1 or
EM_TEST_ROUTE=1): every 0019AD00 / 0019AFE0 call the ORIGINAL player stage
makes while it climbs the two 05_boxes crates (seeded at each Cross press) and
while it replays 06_hill_slide from its source snapshot is re-run natively at
the call and compared as above; the quick run takes the first frames of one
climb and of the slide replay.

No original bytes are embedded: the ELF, RAM images and captures are the
user's own local files.
"""
import ctypes as C
import json
import os
import random
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import ee_float_model as M  # noqa: E402
import reference_mode  # noqa: E402
import test_player_slide_reference as SR  # noqa: E402
from test_player_slide_reference import EE, DECOMP, read_elf, sx32  # noqa: E402

OUT = ROOT / 'build/coll_move_reference'
ROUTE = DECOMP / 'build/s87/route'
FUNCTIONS = DECOMP / 'docs/FUNCTIONS.csv'

MOVE, SWEEP, WALK = 0x19AD00, 0x19AFE0, 0x19FE50
ROUND, FACE, NGON = 0x1A4830, 0x1A4D10, 0x1A4030
LOCK6440, LOCK7280, GRID, SQRT = 0x1A6440, 0x1A7280, 0x19CB60, 0x11E748
WORKERS = (LOCK6440, LOCK7280, GRID)
PLAYER = 0x8102B0
POOL, POOL_STRIDE, POOL_COUNT = 0x7A5640, 0x2F0, 0x100
CELL_RECORD = 0x700030B0
QUERY = 0x01FE0000      # a synthetic query actor record (zero RAM in every beat; checked)
PRIM_AT = 0x01FE1000    # synthetic prims for the prim-level cases

# The scratchpad fields the walkers own (EmCollMoveScratch).
MODELED = (set(range(0x3190, 0x31DC)) | {0x30CA, 0x30CB} | set(range(0x30D4, 0x30E0)) |
           {0x324E, 0x324F} | set(range(0x3254, 0x3258)) | {0x3B88, 0x3B89} | set(range(0x3680, 0x3690)))


# ---- The EE with the measured float model -----------------------------------

BC_OPS = {0: 'vaddbc', 1: 'vsubbc', 2: 'vmaddbc', 3: 'vmsubbc', 6: 'vmulbc'}
OUTER_S, OUTER_T = (1, 2, 0), (2, 0, 1)


class FloatEE(EE):
    """The shared interpreter with COP1 and the VU0 macro ops through
    ee_float_model (ACC, Q and the VU ACC are held as bit patterns)."""

    def __init__(self, elf, ram=None, spad=None):
        super().__init__(elf, ram, spad)
        self.acc, self.q, self.vacc = 0, 0, [0, 0, 0, 0]
        self.calls = None

    def cop1(self, word, pc):
        rs, rt = word >> 21 & 31, word >> 16 & 31
        fs, fd, fn = word >> 11 & 31, word >> 6 & 31, word & 63
        f = self.f
        if rs == 0:
            if rt: self.r[rt] = sx32(f[fs])
            return
        if rs == 4: f[fs] = self.r[rt] & 0xFFFFFFFF; return
        if rs == 2:
            if rt: self.r[rt] = 0
            return
        if rs == 6: return
        if rs == 20:
            if fn == 32: f[fd] = M.ee_cvt_s_w(f[fs]); return
            raise AssertionError(('cvt', fn, hex(pc)))
        if rs != 16: raise AssertionError(('COP1', rs, hex(pc)))
        a, b = f[fs], f[rt]
        if fn == 0: f[fd] = M.ee_add(a, b)
        elif fn == 1: f[fd] = M.ee_sub(a, b)
        elif fn == 2: f[fd] = M.ee_mul(a, b)
        elif fn == 3: f[fd] = M.ee_div(a, b)
        elif fn == 6: f[fd] = M.ee_mov(a)
        elif fn == 7: f[fd] = M.ee_neg(a)
        elif fn == 24: self.acc = M.ee_adda(a, b)
        elif fn == 25: self.acc = M.ee_suba(a, b)
        elif fn == 26: self.acc = M.ee_mula(a, b)
        elif fn == 28: f[fd] = M.ee_madd(self.acc, a, b)
        elif fn == 29: f[fd] = M.ee_msub(self.acc, a, b)
        elif fn == 36: f[fd] = M.ee_cvt_w_s(a)
        elif fn == 48: self.cond = False
        elif fn == 50: self.cond = bool(M.ee_c_eq(a, b))
        elif fn == 52: self.cond = bool(M.ee_c_lt(a, b))
        elif fn == 54: self.cond = bool(M.ee_c_le(a, b))
        else: raise AssertionError(('FPU op outside the measured model', fn, hex(pc)))

    def macro(self, word):
        op, fs, ft, fd = word & 63, word >> 11 & 31, word >> 16 & 31, word >> 6 & 31
        dest = word >> 21 & 15
        S, T = self.vf[fs], self.vf[ft]
        lanes = [k for k in range(4) if dest & (8 >> k)]
        if op < 60:
            out = list(self.vf[fd])
            if op < 16 or 24 <= op < 28:
                name, bc = BC_OPS[op >> 2], op & 3
                for k in lanes: out[k] = M.vu_lane(name, dest, bc, S[k], T[bc], self.vacc[k])
            elif op < 20:
                for k in lanes: out[k] = M.vu_max(S[k], T[op & 3])
            elif op < 24:
                for k in lanes: out[k] = M.vu_min(S[k], T[op & 3])
            elif op in (28, 32):
                name = 'vmulq' if op == 28 else 'vaddq'
                for k in lanes: out[k] = M.vu_lane(name, dest, None, S[k], self.q)
            elif op in (40, 42, 44):
                name = {40: 'vadd', 42: 'vmul', 44: 'vsub'}[op]
                for k in lanes: out[k] = M.vu_lane(name, dest, None, S[k], T[k])
            elif op == 46:
                for k in lanes:
                    out[k] = M.vu_lane('vopmsub', dest, None, S[OUTER_S[k]], T[OUTER_T[k]], self.vacc[k])
            else:
                raise AssertionError(('VU op outside the measured model', hex(word)))
            if fd: self.vf[fd] = out
            return
        special = (word >> 6 & 31) << 2 | (op & 3)
        if 0x08 <= special <= 0x0B or 0x18 <= special <= 0x1B or special == 0x2E:
            acc = list(self.vacc)
            for k in lanes:
                if special == 0x2E:
                    acc[k] = M.vu_lane('vopmula', dest, None, S[OUTER_S[k]], T[OUTER_T[k]])
                elif special <= 0x0B:
                    acc[k] = M.vu_lane('vmaddabc', dest, special & 3, S[k], T[special & 3], self.vacc[k])
                else:
                    acc[k] = M.vu_lane('vmulabc', dest, special & 3, S[k], T[special & 3])
            self.vacc = acc
            return
        if special in (0x10, 0x11, 0x14, 0x15, 0x1D, 0x30, 0x31):
            src = list(S)
            if special == 0x31: src = src[1:] + src[:1]
            out = list(self.vf[ft])
            for k in lanes:
                v = src[k]
                if special == 0x10: v = M.vu_itof(v, 0)
                elif special == 0x11: v = M.vu_itof(v, 4)
                elif special == 0x14: v = M.vu_ftoi(v, 0)
                elif special == 0x15: v = M.vu_ftoi(v, 4)
                elif special == 0x1D: v = M.vu_abs(v)
                out[k] = v
            if ft: self.vf[ft] = out
            return
        if special == 0x38:
            fsf, ftf = word >> 21 & 3, word >> 23 & 3
            self.q = M.vu_div(S[fsf], T[ftf], fsf, ftf); return
        if special == 0x39:
            self.q = M.vu_sqrt(T[word >> 23 & 3]); return
        if special in (0x3B, 0x2F):
            return
        raise AssertionError(('VU special outside the measured model', hex(word), hex(special)))

    def invoke(self, entry, ints=(), fbits=()):
        """Run an original routine with raw argument registers; the caller's
        registers are restored afterwards. Returns (v0, f0) as raw words."""
        saved = (list(self.r), list(self.rh), self.hi, self.lo, list(self.f), self.acc,
                 self.cond, [list(v) for v in self.vf], list(self.vacc), self.q)
        self.r[29] = (self.r[29] - 0x400) & ~15
        for i, value in enumerate(ints): self.r[4 + i] = sx32(value)
        for i, value in enumerate(fbits): self.f[12 + i] = value & 0xFFFFFFFF
        self.r[31] = SR.RETURN
        self.run(entry)
        result = (self.r[2] & 0xFFFFFFFF, self.f[0])
        (self.r, self.rh, self.hi, self.lo, self.f, self.acc, self.cond, self.vf,
         self.vacc, self.q) = saved
        return result


def watch_writes(ee):
    """Log every store (address, bytes) the interpreter makes from now on."""
    log = []
    save, write = ee.save, ee.write

    def saved(address, value, size=4):
        log.append((address & 0xFFFFFFFF, (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'little')))
        save(address, value, size)

    def wrote(address, data):
        log.append((address & 0xFFFFFFFF, bytes(data)))
        write(address, data)
    ee.save, ee.write = saved, wrote
    return log


def unwatch(ee):
    for name in ('save', 'write'):
        if name in ee.__dict__: del ee.__dict__[name]


# ---- Code identity ------------------------------------------------------------

def function_sizes():
    sizes = {}
    for line in FUNCTIONS.read_text().splitlines()[1:]:
        parts = line.split(',')
        sizes[int(parts[0], 16)] = int(parts[2])
    return sizes


def call_graph(elf, roots):
    """{entry: size} of the roots and every function they reach by jal."""
    sizes = function_sizes()
    phoff = struct.unpack_from('<I', elf, 28)[0]
    _, offset, vaddr = struct.unpack_from('<3I', elf, phoff)
    seen, todo = {}, list(roots)
    while todo:
        entry = todo.pop()
        if entry in seen: continue
        assert entry in sizes, ('no FUNCTIONS.csv entry', hex(entry))
        seen[entry] = sizes[entry]
        for pc in range(entry, entry + sizes[entry], 4):
            word = struct.unpack_from('<I', elf, pc - vaddr + offset)[0]
            if word >> 26 == 3:
                todo.append((word & 0x3FFFFFF) << 2)
    return seen, offset, vaddr


def check_code(elf, ram, where):
    graph, offset, vaddr = CODE_GRAPH
    for entry, size in graph.items():
        at = entry - vaddr + offset
        assert ram[entry:entry + size] == elf[at:at + size], (where, 'code differs from the ELF', hex(entry))


# ---- Native bridge -------------------------------------------------------------

BRIDGE = r"""
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "game/em_coll_move_original.h"
#include "game/em_item_sdk_math.h"

/* The named worker section 4 of docs/COLL_MOVE.md binds to .sqrt. */
uint32_t bridge_named_sqrt(uint32_t x)
{
    float f, r;
    memcpy(&f, &x, 4);
    r = em_item_sdk_sqrt(f);
    memcpy(&x, &r, 4);
    return x;
}

typedef struct {
    uint32_t start[4], end[4], point[4], delta[4], record, record_node, record_normal[3],
             record_axis[3], entity, mode, cell_class, cell_normal[3], query_class, self, kind, work[4];
} BridgeScratch;

typedef struct { uint32_t status, cls, h52, self, position[3]; } BridgeActor;

typedef int (*WorkerCb)(int which, int arg, BridgeScratch *io, uint32_t *result);
typedef int (*SqrtCb)(uint32_t x, uint32_t *out);

typedef struct {
    EmActorCellTable table;
    EmActorClassLists lists;
    EmActorCollisionWorld cells;
    EmCollMoveWorld world;
    EmCollMoveScratch s;
    EmActor rec[512];
    uint32_t addr[512];
    int count;
    uint8_t kinds[256];
    WorkerCb worker;
    SqrtCb sqrt;
    int fault;
} Bridge;

static float fb(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static uint32_t bf(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

static int find(Bridge *b, uint32_t addr)
{
    for (int i = 0; i < b->count; ++i) if (b->addr[i] == addr) return i;
    return -1;
}

static const EmActor *actor_of(Bridge *b, uint32_t addr)
{
    if (!addr) return NULL;
    int i = find(b, addr);
    if (i < 0) { b->fault = 1; return NULL; }
    return &b->rec[i];
}

static uint32_t addr_of(Bridge *b, const void *p)
{
    if (!p) return 0;
    if (p == EM_COLL_MOVE_CELL_RECORD) return 0x700030B0u;
    const EmActor *a = p;
    if (a >= b->rec && a < b->rec + b->count) return b->addr[a - b->rec];
    return (uint32_t)(uintptr_t)p;
}

static const void *self_of(Bridge *b, uint32_t addr)
{
    if (!addr) return NULL;
    int i = find(b, addr);
    return i >= 0 ? (const void *)&b->rec[i] : (const void *)(uintptr_t)addr;
}

static void load(Bridge *b, const BridgeScratch *io)
{
    EmCollMoveScratch *s = &b->s;
    for (int k = 0; k < 4; ++k) {
        s->start[k] = fb(io->start[k]); s->end[k] = fb(io->end[k]);
        s->point[k] = fb(io->point[k]); s->delta[k] = fb(io->delta[k]); s->work[k] = fb(io->work[k]);
    }
    if (!io->record) s->record = NULL;
    else if (io->record == 0x700030B0u) s->record = EM_COLL_MOVE_CELL_RECORD;
    else s->record = (const void *)(uintptr_t)io->record;
    s->record_node = (uint16_t)io->record_node;
    for (int k = 0; k < 3; ++k) {
        s->record_normal[k] = fb(io->record_normal[k]); s->record_axis[k] = fb(io->record_axis[k]);
        s->cell_normal[k] = fb(io->cell_normal[k]);
    }
    s->entity = actor_of(b, io->entity);
    s->mode = (int32_t)io->mode;
    s->cell_class = (uint16_t)io->cell_class;
    s->query_class = (int16_t)io->query_class;
    s->self = self_of(b, io->self);
    s->kind = (int16_t)io->kind;
}

static void save(Bridge *b, BridgeScratch *io)
{
    const EmCollMoveScratch *s = &b->s;
    for (int k = 0; k < 4; ++k) {
        io->start[k] = bf(s->start[k]); io->end[k] = bf(s->end[k]);
        io->point[k] = bf(s->point[k]); io->delta[k] = bf(s->delta[k]); io->work[k] = bf(s->work[k]);
    }
    io->record = addr_of(b, s->record);
    io->record_node = s->record_node;
    for (int k = 0; k < 3; ++k) {
        io->record_normal[k] = bf(s->record_normal[k]); io->record_axis[k] = bf(s->record_axis[k]);
        io->cell_normal[k] = bf(s->cell_normal[k]);
    }
    io->entity = addr_of(b, s->entity);
    io->mode = (uint32_t)s->mode;
    io->cell_class = s->cell_class;
    io->query_class = (uint16_t)s->query_class;
    io->self = addr_of(b, s->self);
    io->kind = (uint16_t)s->kind;
}

static int call_worker(Bridge *b, int which, int arg, EmCollMoveScratch *s, int *result)
{
    BridgeScratch io;
    uint32_t r = 0;
    (void)s;
    save(b, &io);
    if (b->worker(which, arg, &io, &r) < 0) return -1;
    load(b, &io);
    if (b->fault) return -1;
    *result = (int)r;
    return 0;
}

static int w6440(void *c, EmCollMoveScratch *s, int arg, int *result) { return call_worker(c, 0x6440, arg, s, result); }
static int w7280(void *c, EmCollMoveScratch *s, int *result) { return call_worker(c, 0x7280, 0, s, result); }
static int wgrid(void *c, EmCollMoveScratch *s, int *result) { return call_worker(c, 0xCB60, 0, s, result); }
static int wsqrt(void *c, float x, float *out)
{
    Bridge *b = c;
    uint32_t r;
    if (b->sqrt(bf(x), &r) < 0) return -1;
    *out = fb(r);
    return 0;
}

void *bridge_new(const uint8_t *image, uint32_t size, WorkerCb worker, SqrtCb sq)
{
    Bridge *b = calloc(1, sizeof *b);
    if (!b) return NULL;
    if (em_actor_cells_init(&b->table, image, size) != 0) { free(b); return NULL; }
    b->cells.table = &b->table;
    b->cells.lists = &b->lists;
    b->world.cells = &b->cells;
    b->world.workers.context = b;
    b->worker = worker;
    b->sqrt = sq;
    b->world.workers.lock_6440 = w6440;
    b->world.workers.lock_7280 = w7280;
    b->world.workers.grid = wgrid;
    b->world.workers.sqrt = wsqrt;
    return b;
}

void bridge_free(void *p)
{
    Bridge *b = p;
    em_actor_cells_free(&b->table);
    free(b);
}

void bridge_present(void *p, int mask)
{
    Bridge *b = p;
    b->world.workers.lock_6440 = mask & 1 ? w6440 : NULL;
    b->world.workers.lock_7280 = mask & 2 ? w7280 : NULL;
    b->world.workers.grid = mask & 4 ? wgrid : NULL;
    b->world.workers.sqrt = mask & 8 ? wsqrt : NULL;
}

void bridge_static_kinds(void *p, const uint8_t *kinds, unsigned n)
{
    Bridge *b = p;
    memcpy(b->kinds, kinds, n);
    b->cells.static_kind = n ? b->kinds : NULL;
    b->cells.static_kind_count = n;
}

int bridge_actor(void *p, uint32_t addr, uint8_t status, uint8_t cls, uint8_t model, uint16_t uid,
                 uint16_t h52, uint16_t kind, uint32_t callback)
{
    Bridge *b = p;
    int i = find(b, addr);
    if (i < 0) {
        if (b->count >= 512) return -1;
        i = b->count++;
        b->addr[i] = addr;
    }
    EmActor *a = &b->rec[i];
    memset(a, 0, sizeof *a);
    a->status = status; a->cls = cls; a->model = model; a->uid = uid; a->h52 = h52; a->kind = kind;
    a->callback = callback;
    a->self = a;
    return 0;
}

int bridge_list(void *p, const uint32_t *entries, int count)
{
    Bridge *b = p;
    EmActorClassList *l = &b->lists.list[EM_ACTOR_LIST_CLASS4];
    memset(l, 0, sizeof *l);
    if (count < 0 || count > EM_ACTOR_LIST_MAX) return -1;
    for (int j = 0; j < count; ++j) {
        int i = find(b, entries[j]);
        if (i < 0) return -1;
        l->slot[count - 1 - j] = &b->rec[i];
    }
    l->published = (int16_t)count;
    return 0;
}

static EmCollMoveActor actor_in(Bridge *b, const BridgeActor *in)
{
    EmCollMoveActor a;
    a.status = (uint8_t)in->status; a.cls = (uint8_t)in->cls; a.h52 = (uint16_t)in->h52;
    a.self = self_of(b, in->self);
    for (int k = 0; k < 3; ++k) a.position[k] = fb(in->position[k]);
    return a;
}

int bridge_move(void *p, BridgeScratch *io, BridgeActor *actor, const uint32_t *target, uint32_t flags)
{
    Bridge *b = p;
    b->fault = 0;
    load(b, io);
    EmCollMoveActor a = actor_in(b, actor);
    float t[3] = { fb(target[0]), fb(target[1]), fb(target[2]) };
    int r = em_coll_move_0019AD00(&b->world, &b->s, &a, t, flags);
    save(b, io);
    for (int k = 0; k < 3; ++k) actor->position[k] = bf(a.position[k]);
    return b->fault ? -2 : r;
}

int bridge_sweep(void *p, BridgeScratch *io, BridgeActor *actor, const uint32_t *from, const uint32_t *to,
                 uint32_t flags)
{
    Bridge *b = p;
    b->fault = 0;
    load(b, io);
    EmCollMoveActor a = actor_in(b, actor);
    float f[3] = { fb(from[0]), fb(from[1]), fb(from[2]) }, t[3] = { fb(to[0]), fb(to[1]), fb(to[2]) };
    int r = em_coll_move_sweep_0019AFE0(&b->world, &b->s, &a, f, t, flags);
    save(b, io);
    for (int k = 0; k < 3; ++k) actor->position[k] = bf(a.position[k]);
    return b->fault ? -2 : r;
}

int bridge_walk(void *p, BridgeScratch *io)
{
    Bridge *b = p;
    b->fault = 0;
    load(b, io);
    int r = em_coll_move_walk_0019FE50(&b->world, &b->s);
    save(b, io);
    return b->fault ? -2 : r;
}

int bridge_prim(void *p, BridgeScratch *io, int which, const uint8_t *prim)
{
    Bridge *b = p;
    b->fault = 0;
    load(b, io);
    int r = which == 0x4830 ? em_coll_move_prim_001A4830(&b->world, &b->s, prim)
          : which == 0x4D10 ? em_coll_move_prim_001A4D10(&b->s, prim)
          : em_coll_move_prim_001A4030(&b->s, prim);
    save(b, io);
    return b->fault ? -2 : r;
}

/* ---- The adapters ------------------------------------------------------- */

typedef struct {
    int kind;
    uint32_t node, entity_flags, entity_type, entity, owner, pickup_box;
    uint32_t point[3], delta[3], normal[3], axis[3];
} BridgeProbe;

static void probe_out(Bridge *b, const EmPlayerProbeHit *h, BridgeProbe *o)
{
    o->node = h->node; o->entity_flags = h->entity_flags; o->entity_type = h->entity_type;
    o->entity = (uint32_t)h->entity; o->owner = addr_of(b, h->owner);
    for (int k = 0; k < 3; ++k) {
        o->point[k] = bf(h->point[k]); o->delta[k] = bf(h->delta[k]);
        o->normal[k] = bf(h->normal[k]); o->axis[k] = bf(h->axis[k]);
    }
}

/* which: 0 player move, 1 player sweep, 2 climb move, 3 climb sweep, 4 slide
 * move, 5 slide sweep, 6 owner move (live = NULL; self = the owner). */
int bridge_adapter(void *p, int which, BridgeScratch *io, const uint8_t *live_bytes, uint32_t self,
                   uint32_t *position, const uint32_t *a, const uint32_t *c, uint32_t mask, BridgeProbe *o)
{
    Bridge *b = p;
    static EmPlayerLiveActor live;
    b->fault = 0;
    load(b, io);
    memset(o, 0, sizeof *o);
    float pos[3] = { fb(position[0]), fb(position[1]), fb(position[2]) };
    float va[4] = { fb(a[0]), fb(a[1]), fb(a[2]), 1.0f }, vc[4] = { fb(c[0]), fb(c[1]), fb(c[2]), 1.0f };
    int r;
    EmPlayerProbeHit hit;
    EmPlayerClimbHit climb;
    if (which == 6) {
        int i = find(b, self);
        if (i < 0) return -3;
        EmCollMoveOwner owner = { &b->world, &b->s, &b->rec[i], pos };
        r = em_coll_move_owner_move(&owner, va, mask);
    } else {
        memcpy(live.bytes, live_bytes, EM_PLAYER_ACTOR_SIZE);
        EmCollMovePlayer pl = { &b->world, &b->s, &live, self_of(b, self) };
        switch (which) {
        case 0: r = em_coll_move_player_move(&pl, pos, va, mask, &hit); break;
        case 1: r = em_coll_move_player_sweep(&pl, va, vc, mask, &hit); break;
        case 2: r = em_coll_move_climb_move(&pl, pos, va, mask, &climb); hit = climb.probe;
                o->pickup_box = climb.pickup_box; break;
        case 3: r = em_coll_move_climb_sweep(&pl, va, vc, mask, &climb); hit = climb.probe;
                o->pickup_box = climb.pickup_box; break;
        case 4: r = em_coll_move_slide_move(&pl, pos, va, mask); break;
        default: r = em_coll_move_slide_sweep(&pl, va, vc, mask, &hit); break;
        }
        if (r >= 0 && which != 4) probe_out(b, &hit, o);
    }
    o->kind = r;
    for (int k = 0; k < 3; ++k) position[k] = bf(pos[k]);
    save(b, io);
    return b->fault ? -2 : r;
}
"""


class Scratch(C.Structure):
    _fields_ = [('start', C.c_uint32 * 4), ('end', C.c_uint32 * 4), ('point', C.c_uint32 * 4),
                ('delta', C.c_uint32 * 4), ('record', C.c_uint32), ('record_node', C.c_uint32),
                ('record_normal', C.c_uint32 * 3), ('record_axis', C.c_uint32 * 3), ('entity', C.c_uint32),
                ('mode', C.c_uint32), ('cell_class', C.c_uint32), ('cell_normal', C.c_uint32 * 3),
                ('query_class', C.c_uint32), ('self', C.c_uint32), ('kind', C.c_uint32),
                ('work', C.c_uint32 * 4)]


class Actor(C.Structure):
    _fields_ = [('status', C.c_uint32), ('cls', C.c_uint32), ('h52', C.c_uint32), ('self', C.c_uint32),
                ('position', C.c_uint32 * 3)]


class Probe(C.Structure):
    _fields_ = [('kind', C.c_int), ('node', C.c_uint32), ('entity_flags', C.c_uint32),
                ('entity_type', C.c_uint32), ('entity', C.c_uint32), ('owner', C.c_uint32),
                ('pickup_box', C.c_uint32), ('point', C.c_uint32 * 3), ('delta', C.c_uint32 * 3),
                ('normal', C.c_uint32 * 3), ('axis', C.c_uint32 * 3)]


WORKER_CB = C.CFUNCTYPE(C.c_int, C.c_int, C.c_int, C.POINTER(Scratch), C.POINTER(C.c_uint32))
SQRT_CB = C.CFUNCTYPE(C.c_int, C.c_uint32, C.POINTER(C.c_uint32))
U32P = C.POINTER(C.c_uint32)


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    source = OUT / 'bridge.c'
    source.write_text(BRIDGE)
    lib = OUT / ('bridge.dylib' if sys.platform == 'darwin' else 'bridge.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared',
                    '-fPIC', '-Isrc', str(source), 'src/game/em_coll_move_original.c',
                    'src/game/em_actor_collision.c', 'src/game/em_collision.c', 'src/game/em_actor_pool.c',
                    'src/game/em_item_sdk_math.c', 'src/game/em_interaction_scan.c', '-lm', '-o', str(lib)],
                   cwd=ROOT, check=True)
    n = C.CDLL(str(lib))
    V = C.c_void_p
    n.bridge_new.restype = V
    n.bridge_new.argtypes = [C.c_char_p, C.c_uint32, WORKER_CB, SQRT_CB]
    n.bridge_free.argtypes = [V]
    n.bridge_named_sqrt.restype = C.c_uint32
    n.bridge_named_sqrt.argtypes = [C.c_uint32]
    n.bridge_present.argtypes = [V, C.c_int]
    n.bridge_static_kinds.argtypes = [V, C.c_char_p, C.c_uint]
    n.bridge_actor.argtypes = [V, C.c_uint32, C.c_uint8, C.c_uint8, C.c_uint8, C.c_uint16, C.c_uint16,
                               C.c_uint16, C.c_uint32]
    n.bridge_list.argtypes = [V, U32P, C.c_int]
    n.bridge_move.argtypes = [V, C.POINTER(Scratch), C.POINTER(Actor), U32P, C.c_uint32]
    n.bridge_sweep.argtypes = [V, C.POINTER(Scratch), C.POINTER(Actor), U32P, U32P, C.c_uint32]
    n.bridge_walk.argtypes = [V, C.POINTER(Scratch)]
    n.bridge_prim.argtypes = [V, C.POINTER(Scratch), C.c_int, C.c_char_p]
    n.bridge_adapter.argtypes = [V, C.c_int, C.POINTER(Scratch), C.c_char_p, C.c_uint32, U32P, U32P, U32P,
                                 C.c_uint32, C.POINTER(Probe)]
    return n


def u32s(values):
    return (C.c_uint32 * len(values))(*values)


# ---- Worlds -------------------------------------------------------------------

def u32(buf, at):
    return struct.unpack_from('<I', buf, at)[0]


def s16(buf, at):
    return struct.unpack_from('<h', buf, at)[0]


def f32(buf, at):
    return struct.unpack_from('<f', buf, at)[0]


def fbits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def prim_size(buf, at):
    h = struct.unpack_from('<H', buf, at)[0]
    t, n = h & 0xF000, buf[at + 2]
    if t == 0x8000: return 0x24 if h & 0x800 else 0x14
    if t == 0x4000: return 0x2C if h & 0x800 else 0x18
    if t == 0x2000: return 0x1C
    if t == 0x1000: return 0x24 + 0x30 * n if h & 0x800 else 0x14 + 0x18 * n
    return 0


def directory(buf, base):
    """(image bytes, {uid: (hull offset, [prim offsets])}) of the directory at buf[base:]."""
    count = u32(buf, base)
    hulls, size = {}, 4 + 4 * count
    for uid in range(count):
        word = u32(buf, base + 4 + 4 * uid)
        if not word:
            continue
        hull = word & 0x3FFFFFFF
        at, prims = base + hull + 0x1C, []
        for _ in range(s16(buf, base + hull + 0x18)):
            n = prim_size(buf, at)
            assert n, ('unknown prim type in a captured hull', uid)
            prims.append(at - base)
            at += n
        hulls[uid] = (hull, prims)
        size = max(size, at - base)
    return bytes(buf[base:base + size]), hulls


SPAD_FIELDS = [('start', 0x3190, 4), ('end', 0x31A0, 4), ('point', 0x31B0, 4), ('delta', 0x31C0, 4),
               ('cell_normal', 0x30D4, 3), ('work', 0x3680, 4)]
SPAD_WORDS = [('record', 0x31D0, 4), ('entity', 0x31D4, 4), ('mode', 0x31D8, 4), ('cell_class', 0x30CA, 2),
              ('query_class', 0x324E, 2), ('self', 0x3254, 4), ('kind', 0x3B88, 2)]


class World:
    """One captured snapshot: RAM, scratchpad, the directory and the owners,
    the FloatEE over them and the native bridge holding the same state."""

    def __init__(self, beat):
        self.beat = beat
        self.ram = (ROUTE / beat / 'eeMemory.bin').read_bytes()
        self.spad = (ROUTE / beat / 'scratchpad.bin').read_bytes()
        check_code(ELF, self.ram, beat)
        assert not any(self.ram[QUERY:QUERY + 0x2000]), (beat, 'the synthetic region is in use')
        self.table = u32(self.spad, 0x3250)
        assert s16(self.spad, 0x324C) == u32(self.ram, self.table), (beat, 'directory count word')
        self.image, self.hulls = directory(self.ram, self.table)
        pub, count = u32(self.ram, 0x275B7C), s16(self.ram, 0x275B84)
        self.owners = [u32(self.ram, pub + 4 * j) for j in range(count)]
        self.ee = FloatEE(ELF, self.ram, self.spad)
        self.native = self.bridge(self.image)

    def bridge(self, image, owners=None, patch=None):
        b = NATIVE.bridge_new(image, len(image), WORKER, SQRT_HOOK)
        assert b, (self.beat, 'bridge_new')
        ram = self.ee.mem
        records = [POOL + i * POOL_STRIDE for i in range(POOL_COUNT)] + [PLAYER]
        for a in records:
            r = bytes(ram[a:a + 0x60])
            for at, v in (patch or {}).items():
                if a <= at < a + 0x60: r = r[:at - a] + bytes([v]) + r[at - a + 1:]
            assert NATIVE.bridge_actor(b, a, r[0], r[2], r[3], struct.unpack_from('<H', r, 0xE)[0],
                                       struct.unpack_from('<H', r, 0x52)[0],
                                       struct.unpack_from('<H', r, 0x54)[0], u32(r, 0x10)) == 0
        owners = self.owners if owners is None else owners
        assert NATIVE.bridge_list(b, u32s(owners or [0]), len(owners)) == 0
        return b


# ---- One comparison -------------------------------------------------------------

class Mismatch(AssertionError):
    pass


def spad_scratch(spad, ram):
    """The EmCollMoveScratch view of a scratchpad image (a worker record's
    +0x1A / +0x24 / +0x34 read from RAM)."""
    s = Scratch()
    for name, at, n in SPAD_FIELDS:
        arr = getattr(s, name)
        for k in range(n): arr[k] = u32(spad, at + 4 * k)
    for name, at, n in SPAD_WORDS:
        setattr(s, name, int.from_bytes(spad[at:at + n], 'little'))
    rec = s.record
    if rec and rec != CELL_RECORD and rec < 0x2000000:
        s.record_node = struct.unpack_from('<H', ram, rec + 0x1A)[0]
        for k in range(3):
            s.record_normal[k] = u32(ram, rec + 0x24 + 4 * k)
            s.record_axis[k] = u32(ram, rec + 0x34 + 4 * k)
    return s


def scratch_into(spad, s):
    for name, at, n in SPAD_FIELDS:
        arr = getattr(s, name)
        for k in range(n): struct.pack_into('<I', spad, at + 4 * k, arr[k])
    for name, at, n in SPAD_WORDS:
        spad[at:at + n] = (getattr(s, name) & ((1 << (8 * n)) - 1)).to_bytes(n, 'little')


def fields(s):
    out = {}
    for name, _, n in SPAD_FIELDS:
        out[name] = tuple(getattr(s, name)[k] for k in range(n))
    for name, _, _ in SPAD_WORDS:
        out[name] = getattr(s, name)
    return out


class Runner:
    """Runs one original call and its native twin over a world and compares
    everything (see the module docstring)."""

    def __init__(self, world, script=None):
        self.w = world
        self.calls = None
        self.wspad = None
        self.last_calls = []
        self.sqrt_pairs = []
        # {entry: (v0, point words, entity)}: a SCRIPTED stand-in for a lock
        # routine, applied identically on both sides (the original's callee is
        # replaced by the same effect), so 0019AD00's lock handling is compared
        # where AREA11's own lists never make 001A6440 / 001A7280 succeed.
        self.script = script or {}

    # native worker callbacks (the bridge calls these during a native call)
    def worker(self, which, arg, io, result):
        ee, s = self.w.ee, io.contents
        entry = {0x6440: LOCK6440, 0x7280: LOCK7280, 0xCB60: GRID}[which]
        self.calls.append((entry, arg if entry == LOCK6440 else 0, tuple(sorted(fields(s).items()))))
        if entry in self.script:
            v0, point, entity = self.script[entry]
            for k in range(3): s.point[k] = point[k]
            s.entity = entity
            scratch_into(self.wspad, s)
            result[0] = v0
            return 0
        scratch_into(self.wspad, s)
        ee.spad = self.wspad
        log = watch_writes(ee)
        try:
            v0, _ = ee.invoke(entry, (arg,) if entry == LOCK6440 else ())
        finally:
            unwatch(ee)
        ram_writes = [hex(a) for a, _ in log if a < 0x40000000]
        if ram_writes:
            self.error = ('worker wrote RAM', hex(entry), ram_writes[:8]); return -1
        back = spad_scratch(self.wspad, ee.mem)
        C.memmove(io, C.byref(back), C.sizeof(Scratch))
        result[0] = v0
        return 0

    def sqrt(self, x, out):
        ee = self.w.ee
        self.calls.append((SQRT, x))
        log = watch_writes(ee)
        try:
            _, f0 = ee.invoke(SQRT, (), (x,))
        finally:
            unwatch(ee)
        if [a for a, _ in log if a < 0x40000000]:
            self.error = ('sqrt wrote RAM',); return -1
        out[0] = f0
        self.sqrt_pairs.append((x, f0))
        return 0

    def original_hooks(self, ee, calls):
        """Observe (not replace) the original's worker and sqrt calls."""
        def observer(entry):
            def hook(e):
                if entry == SQRT:
                    calls.append((SQRT, e.f[12]))
                    args, fl = (), (e.f[12],)
                else:
                    arg = e.arg(0) if entry == LOCK6440 else 0
                    calls.append((entry, arg, tuple(sorted(fields(spad_scratch(e.spad, e.mem)).items()))))
                    args, fl = ((arg,) if entry == LOCK6440 else ()), ()
                    if entry in self.script:
                        v0, point, entity = self.script[entry]
                        for k in range(3): e.save(0x700031B0 + 4 * k, point[k])
                        e.save(0x700031D4, entity)
                        e.r[2] = sx32(v0)
                        return
                del e.hooks[entry]
                try:
                    v0, f0 = e.invoke(entry, args, fl)
                finally:
                    e.hooks[entry] = hook
                e.r[2], e.f[0] = sx32(v0), f0
            return hook
        for entry in WORKERS + (SQRT,):
            ee.hooks[entry] = observer(entry)

    def compare(self, label, native_call, original_entry, args, setup_spad=None, setup_ram=(),
                actor_at=None, bridge=None, native_actor=None):
        """setup_ram: [(address, bytes)] written before both runs and undone
        after. native_call(bridge, io) -> native return (io in/out). With
        actor_at, the original may write only that record's +B0 / +B8, and
        they must equal native_actor.position afterwards."""
        w, ee = self.w, self.w.ee
        saved_ram = [(a, bytes(ee.mem[a:a + len(d)])) for a, d in setup_ram]
        if actor_at is not None:
            saved_ram.append((actor_at, bytes(ee.mem[actor_at:actor_at + 0xC0])))
        for a, d in setup_ram: ee.mem[a:a + len(d)] = d
        spad0 = bytearray(w.spad if setup_spad is None else setup_spad)
        try:
            # native, its workers running the originals over the same RAM
            self.calls, self.error, self.wspad = [], None, bytearray(spad0)
            io = spad_scratch(spad0, ee.mem)
            got = native_call(bridge or w.native, io)
            native_calls = self.calls
            if self.error: raise Mismatch((label, 'native worker', self.error))
            predicted = bytearray(self.wspad)
            scratch_into(predicted, io)
            native_fields = fields(io)
            # the original
            ee.spad = bytearray(spad0)
            calls = []
            self.original_hooks(ee, calls)
            outer = {e: ee.hooks.pop(e) for e in (MOVE, SWEEP) if e in ee.hooks}   # route-mode hooks
            log = watch_writes(ee)
            try:
                v0, _ = ee.invoke(original_entry, args)
            finally:
                unwatch(ee)
                for entry in WORKERS + (SQRT,): ee.hooks.pop(entry, None)
                ee.hooks.update(outer)
            want = s32(v0)
            allowed = set()
            if actor_at is not None:
                allowed = set(range(actor_at + 0xB0, actor_at + 0xB4)) | set(range(actor_at + 0xB8, actor_at + 0xBC))
            bad = [hex(a) for a, d in log if a < 0x40000000 and not set(range(a, a + len(d))) <= allowed]
            if bad: raise Mismatch((label, 'the original wrote RAM outside the actor +B0/+B8', bad[:8]))
            orig_fields = fields(spad_scratch(ee.spad, ee.mem))
            if got != want:
                raise Mismatch((label, 'return', got, want))
            for key in orig_fields:
                if orig_fields[key] != native_fields[key]:
                    raise Mismatch((label, 'field', key, [hex(v) for v in _seq(native_fields[key])],
                                    [hex(v) for v in _seq(orig_fields[key])]))
            if bytes(predicted) != bytes(ee.spad):
                diff = [hex(0x70000000 + i) for i in range(0x4000) if predicted[i] != ee.spad[i]]
                raise Mismatch((label, 'scratchpad differs outside the compared fields', diff[:12]))
            if calls != native_calls:
                raise Mismatch((label, 'worker calls', _calls(native_calls), _calls(calls)))
            self.last_calls = [(c[0],) for c in calls]
            if native_actor is not None:
                ram_pos = [u32(ee.mem, actor_at + 0xB0 + 4 * k) for k in range(3)]
                if ram_pos != list(native_actor.position):
                    raise Mismatch((label, 'actor +B0..+B8', [hex(v) for v in native_actor.position],
                                    [hex(v) for v in ram_pos]))
            return want, calls
        finally:
            for a, d in saved_ram: ee.mem[a:a + len(d)] = d
            ee.spad = bytearray(w.spad)


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def _seq(v):
    return v if isinstance(v, tuple) else (v,)


def _calls(calls):
    return [(hex(c[0]),) + tuple(hex(x) if isinstance(x, int) else '..' for x in c[1:2]) for c in calls]


# ---- Globals the callbacks reach -------------------------------------------------

ELF = NATIVE = CODE_GRAPH = None
CURRENT = None


def _worker(which, arg, io, result):
    try:
        return CURRENT.worker(which, arg, io, result)
    except Exception as exc:          # noqa: BLE001 - reported by the case
        CURRENT.error = ('worker raised', repr(exc)[:300])
        return -1


def _sqrt(x, out):
    try:
        return CURRENT.sqrt(x, out)
    except Exception as exc:          # noqa: BLE001
        CURRENT.error = ('sqrt raised', repr(exc)[:300])
        return -1


WORKER = WORKER_CB(_worker)
SQRT_HOOK = SQRT_CB(_sqrt)


def setup():
    global ELF, NATIVE, CODE_GRAPH
    ELF = read_elf()
    NATIVE = build_native()
    CODE_GRAPH = call_graph(ELF, (MOVE, SWEEP, WALK, ROUND, FACE, NGON, LOCK6440, LOCK7280, GRID, SQRT,
                                  0x1028D0, 0x102760, 0x103230, 0x1028B8, 0x1028E8, 0x102738))


# ---- Case drivers ------------------------------------------------------------------

def actor_bytes(status, cls, self_addr, h52, x, z, y=0.0):
    rec = bytearray(0xC0)
    rec[0], rec[2] = status, cls
    struct.pack_into('<I', rec, 0x14, self_addr)
    struct.pack_into('<H', rec, 0x52, h52)
    struct.pack_into('<3I', rec, 0xB0, x, fbits(y), z)
    return bytes(rec)


def run_move(runner, label, actor_at, target, flags, sweep_from=None, patch_actor=None, bridge=None,
             extra_ram=()):
    """0019AD00 (or 0019AFE0 when sweep_from is given) with the actor record
    at actor_at (its bytes from RAM, or patch_actor written there)."""
    w, ee = runner.w, runner.w.ee
    setup_ram = []
    if patch_actor is not None:
        setup_ram.append((actor_at, patch_actor))
    setup_ram += list(extra_ram)
    vec_at = QUERY + 0x800
    setup_ram.append((vec_at, struct.pack('<4I', *target, 0)))
    if sweep_from is not None:
        setup_ram.append((vec_at + 0x10, struct.pack('<4I', *sweep_from, 0)))
    rec = patch_actor if patch_actor is not None else bytes(ee.mem[actor_at:actor_at + 0xC0])
    act = Actor(rec[0], rec[2], struct.unpack_from('<H', rec, 0x52)[0], u32(rec, 0x14),
                (C.c_uint32 * 3)(u32(rec, 0xB0), u32(rec, 0xB4), u32(rec, 0xB8)))

    def native(b, io):
        if sweep_from is None:
            return NATIVE.bridge_move(b, C.byref(io), C.byref(act), u32s(target), flags)
        return NATIVE.bridge_sweep(b, C.byref(io), C.byref(act), u32s(sweep_from), u32s(target), flags)

    if sweep_from is None:
        args, entry = (actor_at, vec_at, flags), MOVE
    else:
        args, entry = (actor_at, vec_at + 0x10, vec_at, flags), SWEEP
    got, calls = runner.compare(label, native, entry, args, setup_ram=setup_ram, actor_at=actor_at,
                                bridge=bridge, native_actor=act)
    # the actor's +B0/+B8 after the original (still in RAM until compare undid it) is checked here
    return got, calls, act


# ---- Float helpers for the case generators -------------------------------------

def f32r(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def ulp(value, steps):
    """The float `steps` representable values away (toward +inf for > 0)."""
    b = fbits(value)
    if b & 0x80000000:
        mag = (b & 0x7FFFFFFF) - steps
        if mag < 0: return struct.unpack('<f', struct.pack('<I', -mag))[0]
        return struct.unpack('<f', struct.pack('<I', mag | 0x80000000))[0]
    mag = b + steps
    if mag < 0: return struct.unpack('<f', struct.pack('<I', (-mag) | 0x80000000))[0]
    return struct.unpack('<f', struct.pack('<I', mag))[0]


def near(value, rng):
    """value, or one of its nearest neighbours."""
    return ulp(value, rng.choice((-2, -1, 0, 0, 1, 2)))


def vbits(v):
    return [fbits(x) for x in v]


def spad_with_segment(w, start, end, query_class=None, self_addr=None, rng=None):
    spad = bytearray(w.spad)
    struct.pack_into('<4I', spad, 0x3190, *vbits(start), 0)
    struct.pack_into('<4I', spad, 0x31A0, *vbits(end), 0)
    if query_class is not None: struct.pack_into('<h', spad, 0x324E, query_class)
    if self_addr is not None: struct.pack_into('<I', spad, 0x3254, self_addr)
    return spad


# ---- Prim cases ------------------------------------------------------------------------

def prim_floats(buf, at, count):
    return [f32(buf, at + 4 * k) for k in range(count)]


def face_segments(prim, rng, count):
    face = prim[2]
    x0, y0, z0, ex, ey, ez = prim_floats(prim, 4, 6)
    ys = [y0, f32r(y0 + ey), f32r(y0 + ey / 2)]
    out = []
    for _ in range(count):
        y = near(rng.choice(ys), rng) if rng.random() < 0.7 else f32r(y0 + ey * rng.uniform(-0.3, 1.3))
        if face < 3 or face >= 7 or face in (3, 4):
            lo, hi = sorted((z0, f32r(z0 + ez)))
            z = near(rng.choice((lo, hi, f32r((lo + hi) / 2))), rng) if rng.random() < 0.6 else rng.uniform(lo - 2, hi + 2)
            d = rng.choice((0.0, 0.3, 2.0, 9.0))
            a, b = x0 - d - rng.uniform(0, 3), x0 + d + rng.uniform(0, 3)
            if rng.random() < 0.2: a = x0
            if rng.random() < 0.2: b = x0
            s, e = [a, y, z], [b, y, z + rng.uniform(-2, 2) * (rng.random() < 0.5)]
        else:
            lo, hi = sorted((x0, f32r(x0 + ex)))
            x = near(rng.choice((lo, hi, f32r((lo + hi) / 2))), rng) if rng.random() < 0.6 else rng.uniform(lo - 2, hi + 2)
            d = rng.choice((0.0, 0.3, 2.0, 9.0))
            a, b = z0 - d - rng.uniform(0, 3), z0 + d + rng.uniform(0, 3)
            if rng.random() < 0.2: a = z0
            if rng.random() < 0.2: b = z0
            s, e = [x, y, a], [x + rng.uniform(-2, 2) * (rng.random() < 0.5), y, b]
        if rng.random() < 0.5: s, e = e, s
        out.append((s, e))
    return out


def round_segments(prim, rng, count):
    h = struct.unpack_from('<H', prim, 0)[0]
    cx, cy, cz, r = prim_floats(prim, 4, 4)
    half = r if h & 0x8000 else f32(prim, 0x14)
    out = []
    for _ in range(count):
        y = near(rng.choice((f32r(cy - half), f32r(cy + half), cy)), rng) if rng.random() < 0.5 \
            else f32r(cy + half * rng.uniform(-1.3, 1.3))
        t = rng.uniform(0, 6.283185)
        ux, uz = math_cos(t), math_sin(t)
        d = r * rng.choice((0.0, 0.5, 1 - 1e-6, 1.0, 1 + 1e-6, 1.5, rng.random()))
        px, pz = cx - uz * d, cz + ux * d
        a = rng.choice((-3 * r - 1, -r * 0.5, -r * 0.999, -2.0))
        b = rng.choice((3 * r + 1, r * 0.5, 0.0, r * 1.001, 2.0))
        out.append(([px + ux * a, y, pz + uz * a], [px + ux * b, y, pz + uz * b]))
    return out


def ngon_points(prim):
    n = prim[2]
    verts = [prim_floats(prim, 0x14 + 12 * k, 3) for k in range(n)]
    edges = [prim_floats(prim, 0x14 + 12 * n + 12 * k, 3) for k in range(n)]
    pts = list(verts)
    for k in range(n):
        a, b = verts[k], verts[(k + 1) % n]
        pts.append([(a[j] + b[j]) / 2 for j in range(3)])
    c = [sum(v[j] for v in verts) / n for j in range(3)]
    pts.append(c)
    for k in range(n):
        for eps in (-1e-4, 1e-5, 1e-3):
            m = pts[n + k]
            pts.append([m[j] + edges[k][j] * eps for j in range(3)])
    return pts


def ngon_segments(prim, rng, count):
    nrm = prim_floats(prim, 4, 3)
    pts = ngon_points(prim)
    out = []
    horiz = (nrm[0] ** 2 + nrm[2] ** 2) ** 0.5
    for _ in range(count):
        p = rng.choice(pts)
        p = [near(v, rng) for v in p]
        if horiz > 1e-3 and rng.random() < 0.7:
            dx, dz = nrm[0] / horiz, nrm[2] / horiz
            a, b = rng.uniform(0.0, 4.0), rng.uniform(0.0, 4.0)
            if rng.random() < 0.15: a = 0.0
            if rng.random() < 0.15: b = 0.0
            s, e = [p[0] + dx * a, p[1], p[2] + dz * a], [p[0] - dx * b, p[1], p[2] - dz * b]
        else:
            a, b = rng.uniform(0.0, 4.0), rng.uniform(0.0, 4.0)
            jitter = [rng.uniform(-0.3, 0.3) for _ in range(3)]
            s = [p[j] + (nrm[j] + jitter[j]) * a for j in range(3)]
            e = [p[j] - (nrm[j] - jitter[j]) * b for j in range(3)]
        if rng.random() < 0.1: s, e = e, s
        out.append((s, e))
    return out


def math_cos(t):
    import math
    return math.cos(t)


def math_sin(t):
    import math
    return math.sin(t)


def synthetic_prims(rng):
    """Prim layouts AREA11's directory lacks: spheres, extended 0x4000,
    every face byte, negative extents, downward and vertical n-gons."""
    prims = []
    for header in (0x8000, 0x8800):
        p = bytearray(0x24 if header & 0x800 else 0x14)
        struct.pack_into('<HBB4f', p, 0, header, 0, 0, 230.0, 195.0, 280.0, 2.5)
        prims.append(bytes(p))
    for header in (0x4000, 0x4800):
        p = bytearray(0x2C if header & 0x800 else 0x18)
        struct.pack_into('<HBB5f', p, 0, header, 0, 0, 230.0, 195.0, 280.0, 3.0, 4.0)
        prims.append(bytes(p))
    for face in range(8):
        for sx, sy, sz in ((1, 1, 1), (-1, -1, -1), (1, -1, 1), (-1, 1, -1)):
            p = bytearray(0x1C)
            struct.pack_into('<HBB6f', p, 0, 0x2000, face, 0, 228.0, 190.0, 285.0, 7.0 * sx, 6.0 * sy, 5.0 * sz)
            prims.append(bytes(p))
    import math
    for ny in (0.9, 0.5, 0.2, 0.0, -0.2, -0.5, -0.9):
        nh = math.sqrt(max(0.0, 1 - ny * ny))
        n = (nh, ny, 0.0)
        # a square around (230, 195, 280) in the plane
        u = (0.0, 0.0, 1.0)
        v = (n[1] * u[2] - n[2] * u[1], n[2] * u[0] - n[0] * u[2], n[0] * u[1] - n[1] * u[0])
        c = (230.0, 195.0, 280.0)
        corners = []
        for a, b in ((-3, -3), (3, -3), (3, 3), (-3, 3)):
            corners.append([c[j] + u[j] * a + v[j] * b for j in range(3)])
        d = sum(n[j] * c[j] for j in range(3))
        edges = []
        for k in range(4):
            a, b = corners[k], corners[(k + 1) % 4]
            e = [b[j] - a[j] for j in range(3)]
            x = (e[1] * n[2] - e[2] * n[1], e[2] * n[0] - e[0] * n[2], e[0] * n[1] - e[1] * n[0])
            m = math.sqrt(sum(t * t for t in x)) or 1.0
            edges.append([t / m for t in x])
        p = bytearray(0x14 + 0x18 * 4)
        struct.pack_into('<HBB4f', p, 0, 0x1000, 4, 0, *n, d)
        for k in range(4):
            struct.pack_into('<3f', p, 0x14 + 12 * k, *corners[k])
            struct.pack_into('<3f', p, 0x14 + 48 + 12 * k, *edges[k])
        prims.append(bytes(p))
    return prims


def bf(word):
    return struct.unpack('<f', struct.pack('<I', word))[0]


def ngon_prim(normal, d, verts, edges):
    """A non-extended 0x1000 prim from explicit floats (bit-exact via f32)."""
    n = len(verts)
    p = bytearray(0x14 + 0x18 * n)
    struct.pack_into('<HBB4f', p, 0, 0x1000, n, 0, *normal, d)
    for k in range(n):
        struct.pack_into('<3f', p, 0x14 + 12 * k, *verts[k])
        struct.pack_into('<3f', p, 0x14 + 12 * n + 12 * k, *edges[k])
    return bytes(p)


def exact_cases():
    """Boundary cases the random sweep cannot reach: segments whose
    comparisons land exactly on the originals' thresholds."""
    out = []
    # 001A4830: integer circles (every intermediate exact): starts and ends on
    # the circle, tangents (hc = 0), centre lines, band edges.
    for header, half in ((0x4000, 4.0), (0x8000, 2.0), (0x4800, 4.0)):
        p = bytearray(0x2C if header & 0x800 else (0x18 if header & 0x4000 else 0x14))
        struct.pack_into('<HBB4f', p, 0, header, 0, 0, 8.0, 0.0, 16.0, 2.0)
        if header & 0x4000: struct.pack_into('<f', p, 0x14, half)
        prim = bytes(p)
        for axis in (0, 2):
            for off in (0.0, 1.0, 2.0, -2.0, bf(fbits(2.0) + 1), bf(fbits(2.0) - 1)):
                for a in (-4.0, -2.0, -1.0, 0.0):
                    for b in (4.0, 2.0, 1.0, 0.0, -2.0):
                        for y in (0.0, half, -half, bf(fbits(half) + 1)):
                            if a == b: continue
                            if axis == 0: seg = ([8.0 + a, y, 16.0 + off], [8.0 + b, y, 16.0 + off])
                            else: seg = ([8.0 + off, y, 16.0 + a], [8.0 + off, y, 16.0 + b])
                            out.append((prim, ROUND, seg[0], seg[1], 'exact round %x' % header))
                            out.append((prim, ROUND, seg[1], seg[0], 'exact round %x' % header))
    # 001A4030: the facing epsilon (dir.n exactly -1e-5 and its neighbours)
    # and the inside epsilon (1e-5 outside an edge) on an axis-aligned quad in
    # the plane x = 0 (y -3..3, z -3..0).
    quad = ngon_prim((1.0, 0.0, 0.0), 0.0, [(0, -3, -3), (0, 3, -3), (0, 3, 0), (0, -3, 0)],
                     [(0, 0, -1), (0, 1, 0), (0, 0, 1), (0, -1, 0)])
    for delta in (-2, -1, 0, 1, 2):
        dx = bf(fbits(-1e-5) + delta)
        for z in (-1.0, 0.0):
            out.append((quad, NGON, [0.0, 0.5, z], [dx, 0.5, z], 'exact ngon facing'))
    for delta in (-2, -1, 0, 1, 2):
        z = bf(fbits(1e-5) + delta)
        out.append((quad, NGON, [1.0, 0.5, z], [-1.0, 0.5, z], 'exact ngon inside'))
        out.append((quad, NGON, [1.0, bf(fbits(3.0) + delta) if delta > 0 else 3.0, -1.0], [-1.0, 3.0, -1.0],
                    'exact ngon inside'))
    # the interval test at the segment ends
    for end in (0.0, bf(fbits(0.0) + 1), -1e-3):
        out.append((quad, NGON, [1.0, 0.5, -1.0], [end, 0.5, -1.0], 'exact ngon interval'))
    # the class thresholds: ny^2 / (nx^2 + nz^2) exactly 3.0 and 0.49029058f
    # (found by searching the measured model), both signs of ny, +-1 ulp of nx
    for nxw, ny in ((0x3DCB3A30, 0.171875), (0x3EC23A67, 0.265625)):
        for dn in (-1, 0, 1):
            for sy in (1.0, -1.0):
                nx = bf(nxw + dn)
                n = (nx, ny * sy, 0.0)
                # a quad in the plane n.p = 0 around the origin: u = z axis, v = n x u
                v = (n[1], -n[0], 0.0)
                pts = [(v[0] * a, v[1] * a, b) for a, b in ((-3, -3), (3, -3), (3, 3), (-3, 3))]
                edges = [(0, 0, -1), (v[0], v[1], 0), (0, 0, 1), (-v[0], -v[1], 0)]
                prim = ngon_prim(n, 0.0, pts, edges)
                out.append((prim, NGON, [2.0, 0.0, 0.5], [-2.0, 0.0, 0.5], 'exact ngon class'))
    return out


def segments_for(prim, rng, count):
    t = struct.unpack_from('<H', prim, 0)[0] & 0xF000
    if t == 0x2000: return FACE, face_segments(prim, rng, count)
    if t in (0x4000, 0x8000): return ROUND, round_segments(prim, rng, count)
    return NGON, ngon_segments(prim, rng, count)


def prim_case(w, prim_at, prim, entry, s, e, label, general=False):
    r = Runner(w)
    global CURRENT
    CURRENT = r
    spad = spad_with_segment(w, s, e)
    which = {ROUND: 0x4830, FACE: 0x4D10, NGON: 0x4030}[entry]

    def native(b, io):
        return NATIVE.bridge_prim(b, C.byref(io), which, prim)
    setup_ram = [] if prim_at != PRIM_AT else [(PRIM_AT, prim)]
    return r.compare(label, native, entry, (prim_at,), setup_spad=spad, setup_ram=setup_ram)[0]


# ---- Case lists ---------------------------------------------------------------------

WORLD_BEATS = ('04_elevator_ride', '05_boxes', '08_truck_crossing')
WORLDS = {}


def prim_cases(rng):
    cases = []
    for beat in WORLD_BEATS:
        w = WORLDS[beat]
        for uid, (hull, prims) in sorted(w.hulls.items()):
            for off in prims:
                prim = w.image[off:off + prim_size(w.image, off)]
                entry, segs = segments_for(prim, rng, 24)
                for k, (s, e) in enumerate(segs):
                    cases.append(('prim', beat, (w.table + off, None, entry, s, e, 'uid %d +%#x #%d' % (uid, off, k))))
    for k, (prim, entry, a, b, label) in enumerate(exact_cases()):
        cases.append(('prim', WORLD_BEATS[0], (PRIM_AT, prim, entry, a, b, '%s #%d' % (label, k))))
    w = WORLDS[WORLD_BEATS[0]]
    for idx, prim in enumerate(synthetic_prims(rng)):
        entry, segs = segments_for(prim, rng, 40)
        for k, (s, e) in enumerate(segs):
            cases.append(('prim', WORLD_BEATS[0], (PRIM_AT, prim, entry, s, e, 'synthetic %d #%d' % (idx, k))))
    return cases


def owner_boxes(w):
    boxes = []
    for a in w.owners:
        uid = struct.unpack_from('<H', w.ee.mem, a + 0xE)[0] >> 8
        if uid in w.hulls:
            hull = w.hulls[uid][0]
            boxes.append((a, uid, prim_floats(w.image, hull, 6)))
    return boxes


def segment_near(box, rng):
    x0, y0, z0, x1, y1, z1 = box
    cx, cz = rng.uniform(x0 - 3, x1 + 3), rng.uniform(z0 - 3, z1 + 3)
    if rng.random() < 0.5:     # start outside the hull box
        side = rng.randrange(4)
        gap = rng.uniform(0.2, 6.0)
        if side == 0: cx = x0 - gap
        elif side == 1: cx = x1 + gap
        elif side == 2: cz = z0 - gap
        else: cz = z1 + gap
    y = rng.uniform(y0 - 1, y1 + 1) if rng.random() < 0.8 else rng.choice((y0, y1))
    if rng.random() < 0.5:     # aimed through the hull
        tx, tz = rng.uniform(x0, x1), rng.uniform(z0, z1)
        dx, dz = tx - cx, tz - cz
        m = (dx * dx + dz * dz) ** 0.5 or 1.0
        length = rng.uniform(1.0, 18.0)
        return [cx, y, cz], [cx + dx / m * length, y, cz + dz / m * length]
    import math
    t = rng.uniform(0, 2 * math.pi)
    length = rng.choice((0.0, 0.5, 4.5, 6.0, 13.5, 18.0))
    return [cx, y, cz], [cx + math.cos(t) * length, y, cz + math.sin(t) * length]


def segment_through(box, rng):
    """From outside the hull box into its middle, at mid height."""
    x0, y0, z0, x1, y1, z1 = box
    tx, tz = x0 + (x1 - x0) * rng.uniform(0.2, 0.8), z0 + (z1 - z0) * rng.uniform(0.2, 0.8)
    y = y0 + (y1 - y0) * rng.uniform(0.25, 0.75)
    side, gap = rng.randrange(4), rng.uniform(0.5, 4.0)
    sx, sz = [(x0 - gap, tz), (x1 + gap, tz), (tx, z0 - gap), (tx, z1 + gap)][side]
    return [sx, y, sz], [tx, y, tz]


def walk_cases(rng):
    cases = []
    for beat in WORLD_BEATS:
        w = WORLDS[beat]
        boxes = owner_boxes(w)
        for k in range(160):
            a, uid, box = rng.choice(boxes)
            s, e = segment_near(box, rng)
            qc = rng.choice((0, 0, 0, 2, 4, -1))
            self_addr = rng.choice((PLAYER, PLAYER, a, 0))
            cases.append(('walk', beat, ('plain', s, e, qc, self_addr, 'uid %d #%d' % (uid, k))))
        for k in range(60):
            # long segments across the crate cluster (several hulls: the
            # clamped far bound decides which later hulls the gate admits)
            a, uid, box = rng.choice([b for b in boxes if b[1] in (7, 8, 9, 10)] or boxes)
            mid = [(box[0] + box[3]) / 2, rng.uniform(box[1], box[4]), (box[2] + box[5]) / 2]
            import math
            t = rng.uniform(0, 2 * math.pi)
            half = rng.uniform(10.0, 30.0)
            s = [mid[0] - math.cos(t) * half, mid[1], mid[2] - math.sin(t) * half]
            e = [mid[0] + math.cos(t) * half, mid[1], mid[2] + math.sin(t) * half]
            cases.append(('walk', beat, ('long', s, e, 0, PLAYER, 'long uid %d #%d' % (uid, k))))
        for kind in (0x50, 0x51, 0x52, 0x53, 0x59, 0x5A):
            # every owner carries `kind`; the query class decides 0x51..0x53
            for qc in (0, 2, 1, -1):
                for k in range(6):
                    a, uid, box = rng.choice(boxes)
                    s, e = segment_through(box, rng)
                    cases.append(('walk', beat, ('kinds%02x' % kind, s, e, qc, PLAYER,
                                                 'kinds %#x qc %d uid %d #%d' % (kind, qc, uid, k))))
        for variant in ('mixed', 'drums', 'unknown', 'static'):
            pool = static_boxes(w) if variant == 'static' else boxes
            for k in range(40):
                a, uid, box = rng.choice(pool)
                s, e = segment_near(box, rng)
                cases.append(('walk', beat, (variant, s, e, rng.choice((0, 2, 1, -1)), PLAYER,
                                             '%s uid %d #%d' % (variant, uid, k))))
    return cases


FLAGS = (2, 4, 6, 7, 1, 3, 5, 0, 0x80000006, 0x80000007, 0x80000002, 0x80000004, 0x80000001)


def move_cases(rng):
    cases = []
    for beat in WORLD_BEATS:
        w = WORLDS[beat]
        boxes = owner_boxes(w)
        for k in range(120):
            a, uid, box = rng.choice(boxes)
            s, e = segment_near(box, rng)
            flags = rng.choice(FLAGS)
            who = rng.choice(('player', 'player', 'player', 'synthetic', 'owner'))
            if who == 'synthetic':
                actor = (rng.choice((0, 1, 3)), rng.choice((0, 4, 0x84, 0x20, 2)), rng.choice((0, 1, 2, 3)))
            else:
                actor = None
            sweep = rng.random() < 0.4
            cases.append(('move', beat, (who, a, actor, s, e, flags, sweep, 'uid %d #%d' % (uid, k))))
        for k in range(40):
            import math
            t = rng.uniform(0, 2 * math.pi)
            s = [rng.uniform(-0.5, 0.5), rng.uniform(-1, 1), rng.uniform(-0.5, 0.5)]
            length = rng.choice((1e-3, 0.02, 0.3, 1.0))
            e = [s[0] + math.cos(t) * length, s[1], s[2] + math.sin(t) * length]
            cases.append(('move', beat, ('synthetic', None, (1, 4, 0), s, e, rng.choice((2, 0, 0x80000002)),
                                         rng.random() < 0.5, 'origin #%d' % k)))
    return cases


def lock_cases(rng):
    """0019AD00 / 0019AFE0 lock handling (mask bit 0) with a scripted lock
    result: the player (class 0: 001A6440(0x40), the locked entity's +0x52
    bit 1 vetoes) and synthetic class-4 actors (001A7280, the actor's own
    +0x52 bit 1 is required, bit 0 vetoes the copy)."""
    cases = []
    for beat in WORLD_BEATS:
        w = WORLDS[beat]
        boxes = owner_boxes(w)
        for k in range(40):
            a, uid, box = rng.choice(boxes)
            s, e = segment_near(box, rng)
            flags = rng.choice((1, 3, 5, 7, 0x80000001, 0x80000007, 0x80000003))
            who = rng.choice(('player', 'synthetic'))
            actor = (rng.choice((1, 1, 3, 0)), rng.choice((4, 0x84, 7)), rng.choice((0, 1, 2, 3)))
            entity = rng.choice([o for o, _, _ in boxes])
            ret = rng.choice((0, 1, 1, 2))
            point = [e[0] + rng.uniform(-3, 3), e[1], e[2] + rng.uniform(-3, 3)]
            h52 = rng.choice((0, 1, 2, 3))
            cases.append(('lock', beat, (who, actor, s, e, flags, rng.random() < 0.3, ret, point, entity, h52,
                                         'lock uid %d #%d' % (uid, k))))
    return cases


def lock_case(w, who, actor, s, e, flags, sweep, ret, point, entity, h52, label):
    global CURRENT
    script = {LOCK6440: (ret, vbits(point), entity), LOCK7280: (ret, vbits(point), entity)}
    r = CURRENT = Runner(w, script)
    sb, eb = vbits(s), vbits(e)
    at = PLAYER if who == 'player' else QUERY
    rec = bytearray(w.ee.mem[at:at + 0xC0]) if who == 'player' else \
        bytearray(actor_bytes(actor[0], actor[1], QUERY, actor[2], sb[0], sb[2], s[1]))
    struct.pack_into('<3I', rec, 0xB0, *sb)
    patch = {entity + 0x52: h52 & 0xFF, entity + 0x53: h52 >> 8}
    bridge = w.bridge(w.image, patch=patch)
    try:
        return run_move(r, label, at, eb, flags, sweep_from=sb if sweep else None, patch_actor=bytes(rec),
                        bridge=bridge, extra_ram=[(entity + 0x52, struct.pack('<H', h52))])[0]
    finally:
        NATIVE.bridge_free(bridge)


def adapter_cases(rng):
    cases = []
    for beat in WORLD_BEATS:
        w = WORLDS[beat]
        boxes = owner_boxes(w)
        for k in range(12):
            a, uid, box = rng.choice(boxes)
            s, e = segment_near(box, rng)
            which = k % 7
            mask = 0x80000006 if which == 4 else (0x80000007 if which == 6 else rng.choice((6, 7)))
            owner = rng.choice([o for o, _, _ in boxes])
            cases.append(('adapter', beat, (which, owner, s, e, mask, 'adapter %d uid %d #%d' % (which, uid, k))))
    return cases


# ---- Case runners -------------------------------------------------------------------

def static_kinds(w, count):
    ram = w.ee.mem
    area, sub = ram[0x810700], ram[0x810701]
    base = u32(ram, 0x24D7C0 + 4 * area)
    view = u32(ram, base + 4 * sub)
    return bytes(ram[view + 0x28 * i + 8] for i in range(count))


def drum_records(w):
    return [POOL + i * POOL_STRIDE for i in range(POOL_COUNT)
            if u32(w.ee.mem, POOL + i * POOL_STRIDE + 0x10) == 0x156620]


def walk_case(w, variant, s, e, qc, self_addr, label):
    global CURRENT
    r = CURRENT = Runner(w)
    spad = spad_with_segment(w, s, e, qc, self_addr)
    setup_ram, bridge, owned = [], None, False
    rng = random.Random(hash(label) & 0xFFFFFFFF)
    if variant == 'long':
        pass
    elif variant.startswith('kinds') or variant == 'mixed':
        patch = {}
        for a in w.owners:
            if variant != 'mixed':
                patch[a + 0x54] = int(variant[5:], 16)
            elif rng.random() < 0.6:
                patch[a + 0x54] = rng.choice((0x50, 0x51, 0x52, 0x53, 0x59, 0x5A, 0x46))
        setup_ram = [(a, bytes([v])) for a, v in patch.items()]
        bridge, owned = w.bridge(w.image, patch=patch), True
    elif variant == 'drums':
        drums = drum_records(w)
        owners = list(w.owners) + drums
        patch = {}
        for d in drums:
            patch[d] = w.ee.mem[d] or 1
            patch[d + 2] = (w.ee.mem[d + 2] & 0xE0) | 4
        arr = b''.join(struct.pack('<I', a) for a in owners)
        setup_ram = [(a, bytes([v])) for a, v in patch.items()]
        setup_ram += [(QUERY + 0x1000, arr), (0x275B7C, struct.pack('<I', QUERY + 0x1000)),
                      (0x275B84, struct.pack('<h', len(owners)))]
        bridge, owned = w.bridge(w.image, owners=owners, patch=patch), True
    elif variant in ('unknown', 'static'):
        image = bytearray(w.image)
        if variant == 'unknown':
            for uid in (7, 8, 9, 10, 14, 4):
                if uid in w.hulls and len(w.hulls[uid][1]) > 2:
                    off = w.hulls[uid][1][rng.choice((1, 2))]
                    struct.pack_into('<H', image, off, struct.unpack_from('<H', image, off)[0] & 0x0FFF)
        else:
            for uid in (0, 1, 2):
                struct.pack_into('<I', image, 4 + 4 * uid, u32(image, 4 + 4 * uid) | 0x80000000)
        setup_ram = [(w.table, bytes(image))]
        bridge, owned = w.bridge(bytes(image)), True
        if variant == 'static':
            kinds = static_kinds(w, 3)
            NATIVE.bridge_static_kinds(bridge, kinds, 3)

    def native(b, io):
        return NATIVE.bridge_walk(b, C.byref(io))
    try:
        return r.compare(label, native, WALK, (), setup_spad=spad, setup_ram=setup_ram, bridge=bridge)[0]
    finally:
        if owned: NATIVE.bridge_free(bridge)


def static_boxes(w):
    return [(None, uid, prim_floats(w.image, w.hulls[uid][0], 6)) for uid in (0, 1, 2) if uid in w.hulls]


def move_case(w, who, owner, actor, s, e, flags, sweep, label):
    global CURRENT
    r = CURRENT = Runner(w)
    sb, eb = vbits(s), vbits(e)
    if who == 'player':
        at = PLAYER
    elif who == 'owner':
        at = owner
    else:
        at = QUERY
    rec = bytearray(w.ee.mem[at:at + 0xC0]) if who != 'synthetic' else \
        bytearray(actor_bytes(actor[0], actor[1], QUERY, actor[2], sb[0], sb[2], s[1]))
    struct.pack_into('<3I', rec, 0xB0, *sb)
    return run_move(r, label, at, eb, flags, sweep_from=sb if sweep else None, patch_actor=bytes(rec))[0]


def adapter_case(w, which, owner, s, e, mask, label):
    global CURRENT
    r = CURRENT = Runner(w)
    sb, eb = vbits(s), vbits(e)
    at = owner if which == 6 else PLAYER
    rec = bytearray(w.ee.mem[at:at + 0x320])
    struct.pack_into('<3I', rec, 0xB0, *sb)
    live = bytes(rec) if which != 6 else bytes(0x320)
    pos = u32s(sb)
    probe = Probe()
    act = Actor(0, 0, 0, 0, pos)
    vec_at = QUERY + 0x800
    setup_ram = [(at, bytes(rec[:0xC0])), (vec_at, struct.pack('<4I', *eb, 0)), (vec_at + 0x10, struct.pack('<4I', *sb, 0))]
    sweep = which in (1, 3, 5)

    def native(b, io):
        a_vec, c_vec = (u32s(sb), u32s(eb)) if sweep else (u32s(eb), u32s(eb))
        got = NATIVE.bridge_adapter(b, which, C.byref(io), live, at, pos, a_vec, c_vec, mask, C.byref(probe))
        for k in range(3): act.position[k] = pos[k]
        return got
    entry, args = (SWEEP, (at, vec_at + 0x10, vec_at, mask)) if sweep else (MOVE, (at, vec_at, mask))
    got, _ = r.compare(label, native, entry, args, setup_ram=setup_ram, actor_at=at, native_actor=act)
    # The hit view the consumers read, from the original's final state (re-run
    # to read it: compare() restored the scratchpad).
    ee = w.ee
    saved = bytes(ee.mem[at:at + 0xC0])
    for a, d in setup_ram: ee.mem[a:a + len(d)] = d
    ee.spad = bytearray(w.spad)
    try:
        ee.invoke(entry, args)
        spad, ram = ee.spad, ee.mem
        record, entity = u32(spad, 0x31D0), u32(spad, 0x31D4)
        if record == CELL_RECORD:
            node, normal = struct.unpack_from('<H', spad, 0x30CA)[0], [u32(spad, 0x30D4 + 4 * k) for k in range(3)]
        elif record:
            node, normal = struct.unpack_from('<H', ram, record + 0x1A)[0], [u32(ram, record + 0x24 + 4 * k) for k in range(3)]
        else:
            node, normal = 0, [0, 0, 0]
        want = {'kind': got, 'node': node, 'normal': normal,
                'entity': int(entity != 0), 'owner': entity,
                'entity_flags': ram[entity + 2] if entity else 0, 'entity_type': ram[entity + 3] if entity else 0,
                'point': [u32(spad, 0x31B0 + 4 * k) for k in range(3)],
                'delta': [u32(spad, 0x31C0 + 4 * k) for k in range(3)]}
        if which in (2, 3):
            want['pickup_box'] = int(bool(entity) and u32(ram, entity + 0x10) == 0x219550)
    finally:
        ee.mem[at:at + 0xC0] = saved
        ee.spad = bytearray(w.spad)
    if which not in (4, 6):
        have = {'kind': probe.kind, 'node': probe.node, 'normal': list(probe.normal), 'entity': probe.entity,
                'owner': probe.owner, 'entity_flags': probe.entity_flags, 'entity_type': probe.entity_type,
                'point': list(probe.point), 'delta': list(probe.delta)}
        if which in (2, 3): have['pickup_box'] = probe.pickup_box
        for key in want:
            if want[key] != have[key]:
                raise Mismatch((label, 'hit view', key, have[key], want[key]))
    return got


def failstop_checks(w):
    """Native only: a call that can reach a missing worker returns -1 before
    it writes anything; other faults (bit-31 owner word, bit 31 on the
    player's const-position adapters)."""
    global CURRENT
    CURRENT = Runner(w)
    CURRENT.calls, CURRENT.wspad, CURRENT.error = [], bytearray(w.spad), None
    s, e = [228.8, 195.0, 284.0], [228.8, 195.0, 296.0]
    b = w.bridge(w.image)
    checked = 0
    try:
        player = w.ee.mem[PLAYER:PLAYER + 0xC0]
        for mask, flags in ((0b1110, 7), (0b1011, 4), (0b0111, 2), (0b0011, 6)):
            NATIVE.bridge_present(b, mask)
            io = spad_scratch(w.spad, w.ee.mem)
            before = bytes(io)
            act = Actor(player[0], player[2], struct.unpack_from('<H', player, 0x52)[0], PLAYER,
                        u32s(vbits([s[0], s[1], s[2]])))
            got = NATIVE.bridge_move(b, C.byref(io), C.byref(act), u32s(vbits(e)), flags)
            assert got == -1 and bytes(io) == before, ('missing worker must fault before any write', mask, flags, got)
            checked += 1
        NATIVE.bridge_present(b, 0xF)
        # 7280 missing, reached only by a nonzero class
        NATIVE.bridge_present(b, 0b1101)
        io = spad_scratch(w.spad, w.ee.mem); before = bytes(io)
        act = Actor(1, 4, 0, QUERY, u32s(vbits(s)))
        assert NATIVE.bridge_move(b, C.byref(io), C.byref(act), u32s(vbits(e)), 1) == -1 and bytes(io) == before
        checked += 1
        NATIVE.bridge_present(b, 0xF)
        probe = Probe()
        live = bytes(w.ee.mem[PLAYER:PLAYER + 0x320])
        for which in (0, 1, 2, 3):
            io = spad_scratch(w.spad, w.ee.mem)
            got = NATIVE.bridge_adapter(b, which, C.byref(io), live, PLAYER, u32s(vbits(s)), u32s(vbits(e)),
                                        u32s(vbits(e)), 0x80000006, C.byref(probe))
            assert got == -1, ('bit 31 on a const-position adapter must fault', which, got)
            checked += 1
    finally:
        NATIVE.bridge_free(b)
    # a published owner whose offset word carries bit 31
    image = bytearray(w.image)
    uid = next(struct.unpack_from('<H', w.ee.mem, a + 0xE)[0] >> 8 for a in w.owners
               if (struct.unpack_from('<H', w.ee.mem, a + 0xE)[0] >> 8) in w.hulls)
    struct.pack_into('<I', image, 4 + 4 * uid, u32(image, 4 + 4 * uid) | 0x80000000)
    b = w.bridge(bytes(image))
    try:
        box = prim_floats(w.image, w.hulls[uid][0], 6)
        mid = [(box[0] + box[3]) / 2, (box[1] + box[4]) / 2, (box[2] + box[5]) / 2]
        io = spad_scratch(spad_with_segment(w, [mid[0] - 30, mid[1], mid[2]], [mid[0] + 30, mid[1], mid[2]], 0, PLAYER),
                          w.ee.mem)
        assert NATIVE.bridge_walk(b, C.byref(io)) == -1, 'a bit-31 owner word must fault'
        checked += 1
    finally:
        NATIVE.bridge_free(b)
    return checked


def run_one(case):
    kind, beat, args = case
    if kind == 'route':
        return run_route(case)
    w = WORLDS[beat]
    try:
        if kind == 'prim':
            at, prim, entry, s, e, label = args
            if prim is None: prim = bytes(w.ee.mem[at:at + prim_size(w.ee.mem, at)])
            got = prim_case(w, at, prim, entry, vbits_f(s), vbits_f(e), (beat,) + (label,))
            return ('ok', 'prim %x' % entry, got, (), tuple(CURRENT.sqrt_pairs))
        if kind == 'walk':
            got = walk_case(w, *args)
            return ('ok', 'walk ' + args[0], got, tuple(CURRENT.last_calls), tuple(CURRENT.sqrt_pairs))
        if kind == 'move':
            got = move_case(w, *args)
            return ('ok', 'sweep' if args[6] else 'move', got, tuple(CURRENT.last_calls),
                    tuple(CURRENT.sqrt_pairs))
        if kind == 'lock':
            got = lock_case(w, *args)
            return ('ok', 'lock ' + ('sweep' if args[5] else 'move'), got, tuple(CURRENT.last_calls),
                    tuple(CURRENT.sqrt_pairs))
        got = adapter_case(w, *args)
        return ('ok', 'adapter %d' % args[0], got, tuple(CURRENT.last_calls), tuple(CURRENT.sqrt_pairs))
    except Mismatch as m:
        return (beat, kind) + tuple(m.args)


def coverage(results):
    """What the cases exercised: return values per routine and the worker
    calls with their original results."""
    table, workers = {}, {}
    for r in results:
        if not r or r[0] != 'ok': continue
        _, name, got, calls = r[:4]
        table.setdefault(name, {}).setdefault(got, 0)
        table[name][got] += 1
        for c in calls:
            key = '%x' % c[0]
            workers[key] = workers.get(key, 0) + 1
    return table, workers


def vbits_f(v):
    return [f32r(x) for x in v]


# ---- Route mode: the original player stage's own calls ----------------------------

class LiveWorld(World):
    """The state of a running original stage at one call (no file reads)."""

    def __init__(self, label, ee):
        self.beat, self.ee = label, ee
        self.spad = bytes(ee.spad)
        self.table = u32(self.spad, 0x3250)
        self.image, self.hulls = directory(ee.mem, self.table)
        pub, count = u32(ee.mem, 0x275B7C), s16(ee.mem, 0x275B84)
        self.owners = [u32(ee.mem, pub + 4 * j) for j in range(count)]
        self.native = self.bridge(self.image)


class RouteCalls:
    """Hooks 0019AD00 / 0019AFE0 in an original stage: at each call the native
    walker and the original run on the live state and are compared (Runner),
    then the original runs for real and the stage continues."""

    def __init__(self, label):
        self.label, self.count, self.modes, self.frame = label, 0, {}, 0

    def install(self, ee):
        for entry in (MOVE, SWEEP):
            ee.hooks[entry] = self.hook(entry)

    def hook(self, entry):
        def run(ee):
            global CURRENT
            args = tuple(ee.arg(i) for i in range(4 if entry == SWEEP else 3))
            actor_at = args[0]
            live = LiveWorld(self.label, ee)
            r = CURRENT = Runner(live)
            rec = bytes(ee.mem[actor_at:actor_at + 0xC0])
            act = Actor(rec[0], rec[2], struct.unpack_from('<H', rec, 0x52)[0], u32(rec, 0x14),
                        (C.c_uint32 * 3)(u32(rec, 0xB0), u32(rec, 0xB4), u32(rec, 0xB8)))
            vec = lambda at: [ee.load(at + 4 * k) for k in range(3)]
            if entry == MOVE:
                target, flags = vec(args[1]), args[2]

                def native(b, io):
                    return NATIVE.bridge_move(b, C.byref(io), C.byref(act), u32s(target), flags)
            else:
                start, target, flags = vec(args[1]), vec(args[2]), args[3]

                def native(b, io):
                    return NATIVE.bridge_sweep(b, C.byref(io), C.byref(act), u32s(start), u32s(target), flags)
            where = (self.label, 'frame', self.frame, 'call', self.count, '%x' % entry, hex(flags))
            try:
                got, _ = r.compare(where, native, entry, args, actor_at=actor_at, native_actor=act)
            finally:
                NATIVE.bridge_free(live.native)
            self.count += 1
            self.modes[got] = self.modes.get(got, 0) + 1
            del ee.hooks[entry]
            try:
                v0, _ = ee.invoke(entry, args)
            finally:
                ee.hooks[entry] = run
            ee.r[2] = sx32(v0)
        return run


def route_climb(press_index, frames_cap):
    """One 05_boxes Use climb: the original stage seeded at the row before the
    press (as test_player_climb_reference.py's capture route does), pressing
    Use, run until the player is idle again (or frames_cap frames)."""
    beat = SR.route_beat('05_boxes')
    if isinstance(beat, str): return ('skip', beat)
    trace, ram, spad = beat
    rows = {r['counter']: r for r in trace['rows']}
    presses = [c for c in sorted(rows) if c - 1 in rows and rows[c - 1]['p5'] == 0 and rows[c]['p5'] == 2]
    assert len(presses) == 2, ('05_boxes: expected the two crate climbs', presses)
    press = presses[press_index]
    before = rows[press - 1]
    stage = SR.Stage(ELF, ram, spad, tuple(before['pos']), before['yaw'])
    assert isinstance(stage.ee, FloatEE)
    for i in range(3): stage.ee.putf(PLAYER + 0xB0 + 4 * i, before['hip'][i])
    calls = RouteCalls('05_boxes press %d' % press)
    calls.install(stage.ee)
    press_input, states = {'press': 0x40}, set()
    while True:
        calls.frame = stage.frame
        stage.step(**press_input)
        press_input = {}
        state = stage.ee.load(PLAYER + 5, 1)
        states.add(state)
        if (state == 0 and stage.frame > 1) or (frames_cap and stage.frame >= frames_cap) or stage.frame >= 200:
            break
    return ('ok', '05_boxes press %d: %d frames' % (press, stage.frame), calls.count, dict(calls.modes),
            sorted(states))


def route_slide(frames_cap):
    """06_hill_slide replayed from its source snapshot by the original stage
    (test_player_slide_reference.RouteReplay: the recorded pad, camera
    heading and counters): walk off the ledge, slide, skid out, idle."""
    beat = SR.route_beat('06_hill_slide')
    if isinstance(beat, str): return ('skip', beat)
    trace, ram, spad = beat
    replay = SR.RouteReplay(ELF, trace, ram, spad)
    assert isinstance(replay.ee, FloatEE)
    calls = RouteCalls('06_hill_slide')
    calls.install(replay.ee)
    states, last = set(), trace['rows'][-1]['counter']
    while replay.counter < last and not (frames_cap and replay.frame >= frames_cap):
        calls.frame = replay.frame
        replay.step()
        states.add(replay.ee.load(PLAYER + 5, 1))
    return ('ok', '06_hill_slide: %d frames' % replay.frame, calls.count, dict(calls.modes), sorted(states))


def route_jobs(whole):
    """Whole route: both climbs to idle and the full slide beat. Quick: the
    first climb's press and probe frames and the slide replay's first frames."""
    if whole:
        return [('route', 'climb', (0, None)), ('route', 'climb', (1, None)), ('route', 'slide', (None,))]
    return [('route', 'climb', (0, 2)), ('route', 'slide', (3,))]


def run_route(case):
    _, which, args = case
    try:
        return route_climb(*args) if which == 'climb' else route_slide(*args)
    except Mismatch as m:
        return ('route', which) + tuple(m.args)


# ---- Main ----------------------------------------------------------------------------

def main():
    setup()
    missing = [b for b in WORLD_BEATS if not (ROUTE / b / 'eeMemory.bin').exists()]
    if missing:
        print('SKIP: missing route captures', missing)
        return 0
    t0 = time.time()
    for beat in WORLD_BEATS:
        WORLDS[beat] = World(beat)
    rng = random.Random(0x19AD00)
    prims, walks, moves, adapters = prim_cases(rng), walk_cases(rng), move_cases(rng), adapter_cases(rng)
    locks = lock_cases(rng)
    sel_prims = reference_mode.select(prims, 2400, 1, axes=(lambda c: (c[1], c[2][5].split('#')[0]),),
                                      keep=lambda i, c: c[2][5].startswith('exact ngon') or
                                      (c[2][2] == ROUND and not c[2][5].startswith('exact')))
    sel_walks = reference_mode.select(walks, 360, 2, axes=(lambda c: (c[1], c[2][0]), lambda c: (c[2][0], c[2][3])))
    sel_moves = reference_mode.select(moves, 220, 3, axes=(lambda c: (c[2][5], c[2][6]), lambda c: c[2][0]),
                                      keep=lambda i, c: c[2][7].startswith('origin'))
    sel_adapters = reference_mode.select(adapters, 21, 4, axes=(lambda c: c[2][0],))
    sel_locks = reference_mode.select(locks, 60, 5, axes=(lambda c: (c[2][0], c[2][4], c[2][6]),))
    cases = sel_prims + sel_walks + sel_moves + sel_adapters + sel_locks
    whole_route = reference_mode.FULL or os.environ.get('EM_TEST_ROUTE', '') not in ('', '0')
    SR.EE = FloatEE     # the route stages build their EE through this name
    routes = route_jobs(whole_route)
    results = reference_mode.parallel_map(run_one, routes + cases, cost=lambda c: 1000 if c[0] == 'route' else 1)
    route_results, results = results[:len(routes)], results[len(routes):]
    failures = [r for r in results if r and r[0] != 'ok']
    failures += [r for r in route_results if r[0] not in ('ok', 'skip')]
    table, workers = coverage(results)
    for name in sorted(table):
        print('  %-12s returns %s' % (name, dict(sorted(table[name].items()))))
    print('  worker calls (original):', dict(sorted(workers.items())))
    # The named .sqrt worker (em_item_sdk_sqrt) against the original 0011E748
    # on every argument the cases produced: reported, not asserted (the
    # comparison above ran the original sqrt itself).
    pairs = {x: f0 for r in results if r and r[0] == 'ok' for x, f0 in r[4]}
    same = sum(1 for x, f0 in pairs.items() if NATIVE.bridge_named_sqrt(x) == f0)
    print('  named sqrt worker em_item_sdk_sqrt equals 0011E748 on %d of %d arguments' % (same, len(pairs)))
    checked = sum(failstop_checks(WORLDS[b]) for b in WORLD_BEATS)
    reference_mode.banner(reference_mode.part(len(sel_prims), len(prims), 'prim'),
                          reference_mode.part(len(sel_walks), len(walks), 'walk'),
                          reference_mode.part(len(sel_moves), len(moves), 'move/sweep'),
                          reference_mode.part(len(sel_adapters), len(adapters), 'adapter'),
                          reference_mode.part(len(sel_locks), len(locks), 'scripted-lock'),
                          '%d fail-stop checks' % checked)
    for f in failures[:20]:
        print('MISMATCH', f)
    if not whole_route:
        print('  (EM_TEST_ROUTE=1 or EM_TEST_FULL=1 replays the whole 05_boxes climbs and 06_hill_slide)')
    if failures:
        print('FAIL: %d of %d cases differ' % (len(failures), len(cases)))
        return 1
    route_calls = 0
    for r in route_results:
        if r[0] == 'skip':
            print('  SKIP route:', r[1])
        elif r[0] == 'ok':
            print('  route %s, %d walker calls identical, modes %s, player states %s' % r[1:])
            assert r[2] > 0, ('a route job made no walker call', r)
            route_calls += r[2]
    print('PASS: %d cases and %d route calls identical to the original (%.1f s)'
          % (len(cases), route_calls, time.time() - t0))
    return 0


if __name__ == '__main__':
    sys.exit(main())
