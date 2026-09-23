#!/usr/bin/env python3
"""Execute the original segment and camera queries and compare
src/game/em_coll_segment_walkers.c (docs/COLL_SEGMENT_WALKERS.md).

The user's pinned ELF and the captured AREA11 RAM supply every instruction
and every table; none are embedded here. The interpreter is the probe
lane's EE (tools/test_coll_probe_reference.ProbeEE: the shared bounded core
with every COP1 and VU0 macro instruction routed through
tools/ee_float_model.py). No shared file is edited.

Original code executed (from the captured RAM, checked against the ELF):
  0019A570 segment query      001A0B10 / 0019D330 its walkers
  0019A910 camera query       001A1390 / 0019D770 its walkers
  001A50A0 box face  001A5C30 round prim  001A4030 n-gon  0019F1A0 ranks
  0019ED80 grid node test  00102738 / 001028B8 / 001028D0 / 001028E8 /
  00103230 SDK vectors  0011E748 / 0011CB90 / 0011E080 sqrtf  0011DF78 fabsf
  00128350 / 001278C0 / 00127728 / 00126AB8 float -> double
  001000C0 / 00100110 / 001274B0 / 00126BE8 / 00127398 double compares
  and, for the binding check, 0019B6C0 with 001A2AE0 / 0019DF10.
Hooked boundaries (scripted, recorded, compared call by call): 001A6440 and
001A6AD0, the mask-bit-0 hull locks (workers on the native side).

Every case compares the whole scratchpad state the routines use
(0x70003190..0x700031D8, the cell record D_700030B0 +0x1A/+0x24, 0x70003680,
0x7000324E, 0x70003254, the ranks 0x70003240..0x7000324A, 0x70003B86/88 and
001A50A0's 0x70003600..0x70003638 / 0x70003684..0x70003688) and the return
value, and asserts that the original wrote no other scratchpad or RAM byte.

The EMCL (with the grid node class and the rank section) comes from the
decomp exporter, run into build/coll_segment_walkers_reference/ with
--verify-ram against the captured RAM.

Default run: a covering sample (about 10 s). EM_TEST_FULL=1: every route
row of every beat and the full random sweeps. EM_TEST_UBSAN=1 builds the
native bridge with -fsanitize=undefined (no recovery).
"""
import ctypes as C
import hashlib
import math
import os
import random
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from test_player_slide_reference import read_elf, bits, number  # noqa: E402
import test_coll_probe_reference as cp  # noqa: E402
import ee_float_model as M  # noqa: E402
import reference_mode  # noqa: E402

OUT = ROOT / 'build/coll_segment_walkers_reference'
ROUTE, BEATS = cp.ROUTE, cp.BEATS

SEG, CAM = 0x19A570, 0x19A910
CELLS_SEG, CELLS_CAM, GRID_SEG, GRID_CAM = 0x1A0B10, 0x1A1390, 0x19D330, 0x19D770
FACE, ROUND, LOCK_SEG, LOCK_CAM = 0x1A50A0, 0x1A5C30, 0x1A6440, 0x1A6AD0
HEAD = 0x19B6C0
JUMP_TABLE = 0x26DA80        # 001A50A0's face-code table (7 words)
MODE_WORD = 0x26C5D0         # D_0026C5D0, the SDK math error mode 0011E748 reads
CODE = cp.CODE + [(SEG, 0x178), (CAM, 0x16C), (CELLS_SEG, 0x878), (CELLS_CAM, 0x7F0), (GRID_SEG, 0x440),
                  (GRID_CAM, 0x3D8), (FACE, 0x600), (ROUND, 0x808), (0x1028E8, 0x14), (0x11E748, 0x114),
                  (0x11CB90, 0x138), (0x11E080, 0x24), (0x11DF78, 0x1C), (0x128350, 0x40), (0x1278C0, 0x90),
                  (0x127728, 0x2C), (0x126AB8, 0x12C), (0x1000C0, 0x20), (0x100110, 0x20), (0x1274B0, 0x4C),
                  (0x126BE8, 0x9C), (0x127398, 0x114), (JUMP_TABLE, 0x1C)]
ARGS, PRIMS, PLAYER, CELL_RECORD = cp.ARGS, cp.PRIMS, cp.PLAYER, cp.CELL_RECORD

FACE_WORDS = ([0x70003600 + 4 * k for k in range(3)] + [0x70003610 + 4 * k for k in range(3)]
              + [0x70003620 + 4 * k for k in range(3)] + [0x70003630 + 4 * k for k in range(3)]
              + [0x70003684, 0x70003688])
STATE_SPAN = cp.STATE_SPAN + [(0x70003600, 0x7000360C), (0x70003610, 0x7000361C), (0x70003620, 0x7000362C),
                              (0x70003630, 0x7000363C), (0x70003684, 0x7000368C)]
WHICH = {SEG: 0, CAM: 1, CELLS_SEG: 2, CELLS_CAM: 3, GRID_SEG: 4, GRID_CAM: 5, FACE: 6, ROUND: 7}

u32, s16, u16, f32 = cp.u32, cp.s16, cp.u16, cp.f32


# ---------------------------------------------------------------------------
# The native side

BRIDGE = r"""
#include <stdlib.h>
#include <string.h>
#include "game/em_coll_segment_walkers.h"

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
    uint32_t face[14];   /* box_min, box_max, delta, rel (3 each), cross (2) */
} BState;

typedef struct { int32_t which; int32_t arg; } BCall;

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
    EmCollSegmentWorkers workers;
    EmCollProbeWorkers probe_workers;
    EmCollSegment seg;
    int script_hit;
    uint32_t script_point[3];
    BCall calls[64];
    int ncalls;
    int face_calls, round_calls;
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

static int lock(Bridge *b, int which, EmCollProbeState *s, int arg, int *result)
{
    if (b->ncalls < 64) { b->calls[b->ncalls].which = which; b->calls[b->ncalls].arg = arg; }
    b->ncalls++;
    *result = b->script_hit;
    if (b->script_hit) memcpy(s->point, b->script_point, 12);
    return 0;
}
static int lock_seg(void *c, EmCollProbeState *s, int arg, int *r) { return lock(c, 0, s, arg, r); }
/* The probe module's pass-2 worker slots, counted, then this module's adapters. */
static int face_counted(void *c, const uint8_t *p, EmCollProbeState *s, int *hit)
{
    Bridge *b = c;
    b->face_calls++;
    return em_coll_segment_face_worker(&b->seg, p, s, hit);
}
static int round_counted(void *c, const uint8_t *p, EmCollProbeState *s, int *hit)
{
    Bridge *b = c;
    b->round_calls++;
    return em_coll_segment_round_worker(&b->seg, p, s, hit);
}
static int lock_cam(void *c, EmCollProbeState *s, int arg, int *r) { return lock(c, 1, s, arg, r); }

Bridge *bridge_new(const uint8_t *table, uint32_t size, const char *emcl, const uint8_t *elf, uint32_t elf_size,
                   int32_t mode)
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
    b->workers.context = b;
    b->workers.lock_6440 = lock_seg;
    b->workers.lock_6AD0 = lock_cam;
    b->seg.world = &b->world;
    b->seg.math = &b->math;
    b->seg.workers = &b->workers;
    b->probe_workers.context = b;
    b->probe_workers.face_segment = face_counted;
    b->probe_workers.round_segment = round_counted;
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

void bridge_script(Bridge *b, int hit, const uint32_t *point)
{
    b->script_hit = hit; memcpy(b->script_point, point, 12); b->ncalls = 0;
}
int bridge_calls(Bridge *b, BCall *out) { memcpy(out, b->calls, sizeof b->calls); return b->ncalls; }
int bridge_worker_calls(Bridge *b, int round) { return round ? b->round_calls : b->face_calls; }
void bridge_set_attr(Bridge *b, int node, uint8_t attr) { b->emcl.polys[b->grid.first + node].attr = attr; }
void bridge_no_workers(Bridge *b, int none) { b->seg.workers = none ? NULL : &b->workers; }
void bridge_no_math(Bridge *b, int none) { b->seg.math = none ? NULL : &b->math; }

static void to_native(Bridge *b, const BState *in, EmCollProbeState *s, EmCollSegmentFaceScratch *x)
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
    memcpy(x->box_min, in->face + 0, 12); memcpy(x->box_max, in->face + 3, 12);
    memcpy(x->delta, in->face + 6, 12); memcpy(x->rel, in->face + 9, 12); memcpy(x->cross, in->face + 12, 8);
}

static void from_native(Bridge *b, const EmCollProbeState *s, const EmCollSegmentFaceScratch *x, BState *out)
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
    memcpy(out->face + 0, x->box_min, 12); memcpy(out->face + 3, x->box_max, 12);
    memcpy(out->face + 6, x->delta, 12); memcpy(out->face + 9, x->rel, 12); memcpy(out->face + 12, x->cross, 8);
}

/* which: 0 0019A570, 1 0019A910, 2 001A0B10, 3 001A1390, 4 0019D330,
 * 5 0019D770, 6 001A50A0, 7 001A5C30, 8 0019B6C0 with this module's
 * 001A50A0 / 001A5C30 bound as its workers. */
int bridge_run(Bridge *b, int which, BState *st, const float *a, const float *c, uint32_t mask, int32_t id,
               const uint8_t *prim)
{
    EmCollProbeState s;
    EmCollSegmentFaceScratch x;
    to_native(b, st, &s, &x);
    EmCollProbeState before = s;
    EmCollSegmentFaceScratch xbefore = x;
    int r;
    b->ncalls = 0;
    b->seg.state = &s;
    b->seg.face = &x;
    switch (which) {
    case 0: r = em_coll_segment_0019A570(&b->seg, a, c, mask, id); break;
    case 1: r = em_coll_segment_0019A910(&b->seg, a, c, mask); break;
    case 2: r = em_coll_segment_001A0B10(&b->world, b->seg.math, &s, &x); break;
    case 3: r = em_coll_segment_001A1390(&b->world, b->seg.math, &s, &x); break;
    case 4: r = em_coll_segment_0019D330(&b->grid, &s); break;
    case 5: r = em_coll_segment_0019D770(&b->grid, &s); break;
    case 6: r = em_coll_segment_001A50A0(prim, &s, &x); break;
    case 7: r = em_coll_segment_001A5C30(b->seg.math, prim, &s); break;
    case 8: r = em_coll_probe_0019B6C0(&b->world, &b->probe_workers, &s, a, c); break;
    default: return -9;
    }
    if (r < 0 && (which <= 1 || which == 8) &&
        (memcmp(&before, &s, sizeof s) || memcmp(&xbefore, &x, sizeof x))) return -8;
    from_native(b, &s, &x, st);
    return r;
}

/* em_coll_segment_hit over the state a query left. */
typedef struct {
    int kind; float point[3]; uint16_t node; float normal[3]; uint32_t entity; uint8_t flags, type;
} BHit;

int bridge_hit(Bridge *b, BState *st, BHit *out)
{
    EmCollProbeState s;
    EmCollSegmentFaceScratch x;
    to_native(b, st, &s, &x);
    b->seg.state = &s;
    b->seg.face = &x;
    EmCollSegmentHit h;
    int r = em_coll_segment_hit(&b->seg, &h);
    if (r < 0) return r;
    out->kind = h.kind; memcpy(out->point, h.point, 12); out->node = h.record_node;
    memcpy(out->normal, h.record_normal, 12); out->entity = addr_of(b, h.entity);
    out->flags = h.entity_flags; out->type = h.entity_type;
    return 0;
}
"""


class BState(C.Structure):
    _fields_ = [('start', C.c_uint32 * 4), ('end', C.c_uint32 * 4), ('point', C.c_uint32 * 3),
                ('delta', C.c_uint32 * 3), ('record', C.c_int32), ('node', C.c_int32),
                ('entity', C.c_uint32), ('kind', C.c_int32), ('cell_class', C.c_uint16),
                ('cell_normal', C.c_uint32 * 3), ('ratio', C.c_uint32), ('query_class', C.c_int16),
                ('self', C.c_uint32), ('rank', C.c_int16 * 6), ('span_lo', C.c_int16),
                ('span_hi', C.c_int16), ('face', C.c_uint32 * 14)]


class BCall(C.Structure):
    _fields_ = [('which', C.c_int32), ('arg', C.c_int32)]


class BHit(C.Structure):
    _fields_ = [('kind', C.c_int), ('point', C.c_uint32 * 3), ('node', C.c_uint16), ('normal', C.c_uint32 * 3),
                ('entity', C.c_uint32), ('flags', C.c_uint8), ('type', C.c_uint8)]


SOURCES = ['src/game/em_coll_segment_walkers.c', 'src/game/em_coll_probe_original.c',
           'src/game/em_sdk_math_original.c', 'src/game/em_actor_collision.c', 'src/game/em_collision.c',
           'src/game/em_actor_pool.c']


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    source = OUT / 'bridge.c'
    source.write_text(BRIDGE)
    lib = OUT / ('bridge.dylib' if sys.platform == 'darwin' else 'bridge.so')
    command = ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared',
               '-fPIC', '-Isrc', str(source)] + SOURCES + ['-lm', '-o', str(lib)]
    if os.environ.get('EM_TEST_UBSAN', '') not in ('', '0'):      # a one-off sanitizer build of the bridge
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
    V, P, U8, U16, U32, I = C.c_void_p, C.c_char_p, C.c_uint8, C.c_uint16, C.c_uint32, C.c_int
    PF = C.POINTER(C.c_float)
    n.bridge_new.restype = V; n.bridge_new.argtypes = [P, U32, P, P, U32, C.c_int32]
    n.bridge_static_kinds.argtypes = [V, P, C.c_uint]
    n.bridge_free.argtypes = [V]
    n.bridge_actor.argtypes = [V, U32, U8, U8, U8, U16, U16]
    n.bridge_list.argtypes = [V, C.POINTER(U32), I, I]
    n.bridge_script.argtypes = [V, I, C.POINTER(U32)]
    n.bridge_calls.argtypes = [V, C.POINTER(BCall)]
    n.bridge_set_attr.argtypes = [V, I, U8]
    n.bridge_worker_calls.argtypes = [V, I]
    n.bridge_no_workers.argtypes = [V, I]
    n.bridge_no_math.argtypes = [V, I]
    n.bridge_run.argtypes = [V, I, C.POINTER(BState), PF, PF, U32, C.c_int32, P]
    n.bridge_hit.argtypes = [V, C.POINTER(BState), C.POINTER(BHit)]
    return n


# ---------------------------------------------------------------------------
# Worlds and state

def check_code(elf, ram, where):
    phoff = u32(elf, 28)
    kind, offset, vaddr = struct.unpack_from('<3I', elf, phoff)
    assert kind == 1 and vaddr == 0x100000
    for start, size in CODE:
        at = start - vaddr + offset
        assert bytes(ram[start:start + size]) == elf[at:at + size], (where, 'code differs from the ELF', hex(start))
    assert u32(ram, MODE_WORD) == 1, (where, 'D_0026C5D0 is not 1')


class World(cp.World):
    def __init__(self, beat, elf):
        super().__init__(beat, elf)
        check_code(elf, self.ram, beat)


def native_world(native, world, emcl, ram=None):
    ram = world.ram if ram is None else ram
    image = world.image(ram)
    elf = G['elf']
    b = native.bridge_new(image, len(image), str(emcl).encode(), elf, len(elf), struct.unpack_from('<i', ram, MODE_WORD)[0])
    assert b, 'bridge_new'
    owners = world.owners(ram)
    for a in set(o for o in owners if o):
        native.bridge_actor(b, a, ram[a], ram[a + 2], ram[a + 3], u16(ram, a + 0xE), u16(ram, a + 0x54))
    slots = list(reversed(owners))
    arr = (C.c_uint32 * max(1, len(slots)))(*slots)
    assert native.bridge_list(b, arr, len(slots), len(slots)) == 0
    kinds = world.kinds(ram)
    native.bridge_static_kinds(b, kinds, len(kinds))
    return b


class Case(cp.Case):
    """The probe lane's scratchpad state plus 001A50A0's words."""

    def __init__(self, world, rng=None, words=None):
        words = dict(words or {})
        super().__init__(world, rng, {a: v for a, v in words.items() if a not in FACE_WORDS})
        for a in FACE_WORDS:
            self.words[a] = (words.get(a, u32(world.spad, a - 0x70000000)), 4)

    def native(self, world):
        base = super().native(world)
        st = BState()
        for name, _ in cp.BState._fields_:
            setattr(st, name, getattr(base, name))
        for k, a in enumerate(FACE_WORDS):
            st.face[k] = self.words[a][0]
        return st


def ee_state(ee, world):
    out = cp.ee_state(ee, world)
    out['face'] = tuple(ee.load(a) for a in FACE_WORDS)
    return out


def native_state(st):
    out = cp.native_state(st)
    out['face'] = tuple(st.face)
    return out


def fvec(values):
    return (C.c_float * len(values))(*values)


# ---------------------------------------------------------------------------
# One world on both sides

class Pair:
    def __init__(self, elf, native, emcl, world):
        self.world, self.native, self.emcl = world, native, emcl
        self.ee = cp.ProbeEE(elf, world.ram, world.spad)
        self.ram = self.ee.mem
        self.spad0 = bytes(self.ee.spad)
        self.b = native_world(native, world, emcl, self.ram)
        self.calls, self.script = [], (0, (0, 0, 0))
        self.ee.hooks[LOCK_SEG] = lambda e: self._lock(e, 0)
        self.ee.hooks[LOCK_CAM] = lambda e: self._lock(e, 1)

    def rebind(self):
        self.native.bridge_free(self.b)
        self.b = native_world(self.native, self.world, self.emcl, self.ram)

    def _lock(self, e, which):
        self.calls.append((which, cp.sx32(e.arg(0))))
        hit, point = self.script
        if hit:
            for k in range(3):
                e.save(0x700031B0 + 4 * k, point[k])
        e.ret_int(hit)

    def set_script(self, hit, point):
        self.script = (hit, tuple(point))
        self.native.bridge_script(self.b, hit, (C.c_uint32 * 3)(*point))

    def original(self, case, entry, args, stage=()):
        ee = self.ee
        ee.spad[:] = self.spad0
        for address, data in stage:
            ee.write(address, data)
        case.load(ee)
        self.calls = []
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
            assert any(lo <= a and a + size <= hi for lo, hi in STATE_SPAN), \
                (hex(entry), 'the original wrote outside the modelled state', hex(a), size)
        return cp.sx32(ee.r[2] & 0xFFFFFFFF), ee_state(ee, self.world)

    def both(self, where, case, entry, args, a=(0.0, 0.0, 0.0), c=(0.0, 0.0, 0.0), mask=0, id=0, prim=None,
             stage=(), which=None):
        want_r, want = self.original(case, entry, args, stage)
        st = case.native(self.world)
        which = WHICH[entry] if which is None else which
        got_r = self.native.bridge_run(self.b, which, C.byref(st), fvec(a), fvec(c), mask, id, prim)
        got = native_state(st)
        assert want_r == got_r, (self.world.beat, where, 'return', want_r, got_r, want, got)
        for key, value in want.items():
            assert got[key] == value, (self.world.beat, where, key, value, got[key])
        calls = (BCall * 64)()
        n = self.native.bridge_calls(self.b, calls)
        assert [(calls[i].which, calls[i].arg) for i in range(min(n, 64))] == self.calls, \
            (self.world.beat, where, 'worker calls', self.calls)
        return want_r, want

    def args(self, *vectors):
        data = b''.join(struct.pack('<4f', *(list(v) + [0.0] * (4 - len(v)))) for v in vectors)
        return [(ARGS, data)]

    def segment(self, case, where, a, c, mask, id=0):
        return self.both(where, case, SEG, (ARGS, ARGS + 0x10, mask, id), a, c, mask=mask, id=id,
                         stage=self.args(a, c))

    def camera(self, case, where, a, c, mask):
        return self.both(where, case, CAM, (ARGS, ARGS + 0x10, mask), a, c, mask=mask, stage=self.args(a, c))

    def head(self, case, where, top, bottom):
        """0019B6C0 unhooked (the original 001A50A0 / 001A5C30 run) against
        the probe module with this module's workers bound."""
        return self.both(where, case, HEAD, (ARGS, ARGS + 0x10), top, bottom, stage=self.args(top, bottom),
                         which=8)

    def hit_view(self, where, want):
        """em_coll_segment_hit against the bytes a caller reads through the
        original's own record pointer."""
        ee = self.ee
        st = BState()
        record = ee.load(0x700031D0)
        c = Case(self.world, None, {a: ee.load(a, size) for a, (_, size) in Case(self.world).words.items()})
        st = c.native(self.world)
        h = BHit()
        r = self.native.bridge_hit(self.b, C.byref(st), C.byref(h))
        if not record:
            assert r == -1, (where, 'a hit view without a record must fault')
            return 0
        assert r == 0, where
        assert h.kind == cp.sx32(ee.load(0x700031D8)), where
        assert list(h.point) == [ee.load(0x700031B0 + 4 * k) for k in range(3)], where
        assert h.node == ee.load(record + 0x1A, 2), (where, 'record +0x1A', hex(h.node))
        assert list(h.normal) == [ee.load(record + 0x24 + 4 * k) for k in range(3)], (where, 'record +0x24')
        owner = ee.load(0x700031D4)
        assert h.entity == owner, where
        if owner:
            assert (h.flags, h.type) == (ee.load(owner + 2, 1), ee.load(owner + 3, 1)), where
        return 1


# ---------------------------------------------------------------------------
# Case material

G = {}


def rows_of(beat):
    import json
    rows = json.loads((ROUTE / beat / 'trace.json').read_text())['rows']
    out = []
    for r in rows:
        v = {k: tuple(f32(x) for x in r[k]) for k in ('pos', 'hip', 'eye', 'tgt', 'cam_eye', 'cam_tgt')}
        out.append((r['counter'], r['p5'], v))
    return out


def dy(v, d):
    return (v[0], number(M.ee_add(bits(v[1]), bits(d))), v[2])


def box_prim(face, origin, extent):
    return struct.pack('<HBB6f', 0x2000, face, 0, *origin, *extent)


def round_prim(centre, radius, half, wide=False):
    body = struct.pack('<HBB5f', 0x4000 | (0x800 if wide else 0), 0, 0, *centre, radius, half)
    return body + bytes((0x2C if wide else 0x18) - len(body))


def filler_prim(wide=False, cylinder=None):
    """A 0x8000 prim; `cylinder` = (centre, radius, half) writes bytes that a
    001A5C30 call would hit (the walkers must not call it)."""
    head = struct.pack('<HBB', 0x8000 | (0x800 if wide else 0), 0, 0)
    if cylinder:
        head += struct.pack('<5f', *cylinder[0], cylinder[1], cylinder[2])
    size = 0x24 if wide else 0x14
    return (head + bytes(size))[:size]


def seg_words(a, e):
    words = {}
    for j in range(3):
        words[0x70003190 + 4 * j] = bits(f32(a[j]))
        words[0x700031A0 + 4 * j] = bits(f32(e[j]))
    return words


# ---------------------------------------------------------------------------
# Work items

def run_rows(item):
    """Route rows: the shadow's segment form (0015BF90: 100 straight down),
    the camera's line of sight and ceiling/floor forms (00197490, 0018DD20:
    200 up and down), and mask 7 of both queries with the scripted lock."""
    beat, rows = item
    elf, native, emcl = G['elf'], G['native'], G['emcl']
    world = World(beat, elf)
    pair = Pair(elf, native, emcl, world)
    out = {'seg': [0, 0, 0, 0], 'cam': [0, 0, 0, 0], 'entities': set(), 'locks': 0, 'views': 0}
    base = Case(world)
    slot = {0: 0, 1: 1, 2: 2, 4: 3}
    for n, (counter, p5, v) in enumerate(rows):
        w = f'row {counter}'
        r, s = pair.segment(base, w + ' shadow', v['hip'], dy(v['hip'], -100.0), 6)
        out['seg'][slot[r]] += 1
        out['views'] += pair.hit_view(w + ' shadow view', s)
        r, s = pair.camera(base, w + ' line of sight', v['cam_tgt'], v['cam_eye'], 6)
        out['cam'][slot[r]] += 1
        if s['entity']:
            out['entities'].add(s['entity'])
        out['views'] += pair.hit_view(w + ' camera view', s)
        for d in (200.0, -200.0):
            r, _ = pair.camera(base, f'{w} ceiling {d}', v['eye'], dy(v['eye'], d), 6)
            out['cam'][slot[r]] += 1
        pair.set_script(n & 1, (bits(v['pos'][0]), bits(v['pos'][1]), bits(v['pos'][2])))
        r, _ = pair.segment(base, w + ' mask 7', v['tgt'], v['pos'], 7, id=0x1234 + n)
        out['seg'][slot[r]] += 1
        r, _ = pair.camera(base, w + ' mask 7', v['eye'], v['tgt'], 7)
        out['cam'][slot[r]] += 1
        out['locks'] += len(pair.calls)
        pair.set_script(0, (0, 0, 0))
        r, s = pair.segment(base, w + ' low', dy(v['pos'], 3.0), dy(v['pos'], -3.0), 6)
        out['seg'][slot[r]] += 1
        if s['entity']:
            out['entities'].add(s['entity'])
    return out


def run_capture(item):
    """The snapshot frame's last collision query is the camera's ceiling test
    (0018DD20: 0019A910(D_700038F0, D_700038F0 + 200 up, 6)). Re-run from the
    captured segment over the captured RAM, the original and the native must
    write back exactly the captured scratchpad words."""
    beat = item
    elf, native, emcl = G['elf'], G['native'], G['emcl']
    world = World(beat, elf)
    pair = Pair(elf, native, emcl, world)
    sp = world.spad
    start = tuple(number(u32(sp, 0x3190 + 4 * k)) for k in range(3))
    end = tuple(number(u32(sp, 0x31A0 + 4 * k)) for k in range(3))
    assert end[0] == start[0] and end[2] == start[2] and bits(end[1]) == M.ee_add(bits(start[1]), bits(200.0)), \
        (beat, 'the captured segment is not the ceiling test')
    assert start == tuple(number(u32(sp, 0x38F0 + 4 * k)) for k in range(3)), (beat, 'D_700038F0')
    r, s = pair.camera(Case(world), 'capture ceiling', start, end, 6)
    captured = ee_state_from_spad(world)
    # 0x70003B86/88 are not compared: code after the query reuses them (the
    # captures hold 0 or 1 there on every beat, where the query leaves span
    # and attribute values).
    for key in ('start', 'end', 'point', 'record', 'node', 'entity', 'kind', 'self', 'rank', 'query_class',
                'cell_class', 'cell_normal', 'ratio', 'face'):
        assert s[key] == captured[key], (beat, 'capture', key, s[key], captured[key])
    # 0018DD20 then stores the hit's y - 1 (a 0x8800 record) at 0x70003A3C.
    lid = None
    if r:
        record = pair.ee.load(0x700031D0)
        if pair.ee.load(record + 0x1A, 2) & 0x8800:
            lid = M.ee_sub(s['point'][1], bits(1.0))
            assert u32(sp, 0x3A3C) == lid, (beat, 'capture 0x70003A3C', hex(u32(sp, 0x3A3C)), hex(lid))
    return {'hit': r, 'lid': lid is not None}


def ee_state_from_spad(world):
    class View:
        def load(self, a, size=4):
            return int.from_bytes(world.spad[a - 0x70000000:a - 0x70000000 + size], 'little')
    return ee_state(View(), world)


def run_units(item):
    """001A50A0 and 001A5C30 on their own, and the four walkers on staged
    segments."""
    beat, count, seed = item
    elf, native, emcl = G['elf'], G['native'], G['emcl']
    world = World(beat, elf)
    pair = Pair(elf, native, emcl, world)
    rng = random.Random(seed)
    ram = world.ram
    out = {'face': {}, 'round': {}, 'walkers': {}}
    table = world.table
    boxes = []
    for uid in range(world.count):
        word = u32(ram, table + 4 + 4 * uid)
        if not word:
            continue
        for at in cp.prim_addresses(ram, table + (word & 0x3FFFFFFF)):
            if u16(ram, at) & 0xF000 == 0x2000:
                boxes.append(bytes(ram[at:at + 0x1C]))
    # 001A50A0: the directory's own faces and synthetic ones (every code 0..8,
    # extents of both signs), crossed by segments from both sides.
    for k in range(count):
        if boxes and rng.random() < 0.5:
            data = rng.choice(boxes)
            if rng.random() < 0.3:
                data = data[:2] + bytes([rng.randrange(9)]) + data[3:]
        else:
            origin = [rng.uniform(-50, 50) for _ in range(3)]
            extent = [rng.choice((1, -1)) * rng.uniform(0.5, 12) for _ in range(3)]
            if rng.random() < 0.1:
                extent[rng.randrange(3)] = 0.0
            data = box_prim(rng.randrange(9), origin, extent)
        face = data[2]
        o = struct.unpack_from('<3f', data, 4)
        e = struct.unpack_from('<3f', data, 0x10)
        inside = [o[j] + e[j] * rng.uniform(-0.2, 1.2) for j in range(3)]
        axis = (face - 1) // 2 if 1 <= face <= 6 else rng.randrange(3)
        span = rng.uniform(0.5, 20)
        a, b = list(inside), list(inside)
        a[axis] = o[axis] + span * rng.choice((1, -1))
        b[axis] = o[axis] - (a[axis] - o[axis]) * rng.uniform(-0.3, 1.5)
        for j in range(3):
            if j != axis and k % 3 == 0:
                b[j] += rng.uniform(-3, 3)
        case = Case(world, rng, seg_words(a, b))
        r, _ = pair.both(f'001A50A0 face {face}', case, FACE, (PRIMS,), prim=data, stage=[(PRIMS, data)])
        key = (face, r)
        out['face'][key] = out['face'].get(key, 0) + 1
    # 001A5C30: cylinders crossed through the side, the caps, vertically, and
    # near the 1e-5 vertical threshold on either side.
    tiny = (0x3727C5AC, 0x3727C5AD, 0x3727C5AB, 0x3727C5AE, 0x00000000, 0x00400000, 0x7F800001)
    for k in range(count):
        centre = [rng.uniform(-40, 40), rng.uniform(-20, 20), rng.uniform(-40, 40)]
        radius, half = rng.uniform(0.3, 6), rng.uniform(0.3, 6)
        data = round_prim(centre, radius, half, wide=rng.random() < 0.3)
        kind = k % 6
        if kind == 0:          # vertical (or nearly) through the disc
            x, z = centre[0] + rng.uniform(-1.2, 1.2) * radius, centre[2] + rng.uniform(-1.2, 1.2) * radius
            side = rng.choice((1, -1))
            y0 = centre[1] + side * (half + rng.uniform(0.05, 3))
            y1 = centre[1] - side * rng.uniform(-half, half + 3) if rng.random() < 0.8 else \
                y0 + side * rng.uniform(0, 3)
            a, b = [x, y0, z], [x, y1, z]
            words = seg_words(a, b)
            ends = [words[0x700031A0], words[0x700031A8]]
            for j, at in enumerate((0x700031A0, 0x700031A8)):
                if rng.random() < 0.5:     # end = start +- a threshold-sized step (bit exact)
                    s0 = words[at - 0x10]
                    step = rng.choice(tiny)
                    ends[j] = M.ee_add(s0, step | (0x80000000 if rng.random() < 0.5 else 0))
                    if step == 0x7F800001:
                        ends[j] = step
                        words[at - 0x10] = 0
            words[0x700031A0], words[0x700031A8] = ends
            if rng.random() < 0.15:
                words[0x700031A4] = words[0x70003194] if rng.random() < 0.5 else \
                    M.ee_add(words[0x70003194], rng.choice(tiny))
        else:                  # oblique: through the side or across a cap
            ang = rng.uniform(0, 6.283)
            d = rng.uniform(0, 1.3) * radius
            px, pz = centre[0] + d * math.cos(ang), centre[2] + d * math.sin(ang)
            direction = [rng.uniform(-1, 1), rng.uniform(-1, 1) * (3 if kind == 1 else 1), rng.uniform(-1, 1)]
            if kind == 2:
                direction[0] *= 0.01
            if kind == 3:
                direction[2] *= 0.01
            L = rng.uniform(radius, 4 * radius + 10)
            py = centre[1] + rng.uniform(-1.5, 1.5) * half
            a = [px - direction[0] * L * 0.5, py - direction[1] * L * 0.5, pz - direction[2] * L * 0.5]
            b = [px + direction[0] * L * 0.5, py + direction[1] * L * 0.5, pz + direction[2] * L * 0.5]
            words = seg_words(a, b)
        case = Case(world, rng, words)
        r, _ = pair.both(f'001A5C30 {kind}', case, ROUND, (PRIMS,), prim=data, stage=[(PRIMS, data)])
        key = (kind, r, pair.ee.load(0x700030CA, 2) if r else 0)
        out['round'][key] = out['round'].get(key, 0) + 1
    # The walkers on staged segments: through an owner's hull, through a grid
    # node, and random, under random persistent state.
    owner_hulls = []
    for a in world.owners():
        uid = u16(ram, a + 0xE) >> 8
        word = u32(ram, table + 4 + 4 * uid) if uid < world.count else 0
        if word:
            owner_hulls.append(table + word)
    walkers = (CELLS_SEG, CELLS_CAM, GRID_SEG, GRID_CAM)
    for k in range(count):
        entry = walkers[k % 4]
        if owner_hulls and entry in (CELLS_SEG, CELLS_CAM) and k % 8 < 6:
            hull = rng.choice(owner_hulls)
            lo, hi = struct.unpack_from('<3f', ram, hull), struct.unpack_from('<3f', ram, hull + 0xC)
            pt = [rng.uniform(lo[j], hi[j]) for j in range(3)]
        else:
            i = rng.randrange(world.node_count)
            _, verts, _ = cp.node_ring(world, ram, i)
            pt = cp.inside(rng, verts)
        a = [pt[j] + rng.uniform(-6, 6) for j in range(3)]
        e = [pt[j] - (a[j] - pt[j]) * rng.uniform(0.2, 2.0) for j in range(3)]
        words = seg_words(a, e)
        if rng.random() < 0.2:
            words[0x70003254] = rng.choice(world.owners() or [0])
        words[0x7000324E] = rng.choice((0xFFFF, 0, 1, 2))
        case = Case(world, rng, words)
        r, _ = pair.both(f'walker {entry:#x}', case, entry, ())
        key = (hex(entry), r)
        out['walkers'][key] = out['walkers'].get(key, 0) + 1
    return out


def run_synthetic(item):
    """Worlds the capture does not hold, made from it by patching RAM on both
    sides: static cells (bit 31 with 0x40000000 / 0x20000000), static and
    owner kinds around every gate, box / round / filler / unknown prims in the
    hulls, grid attributes around both grid gates, the self skip, and the
    probe module's surface walker reaching this module's 001A50A0 / 001A5C30
    as its bound workers."""
    beat, count, seed = item
    elf, native = G['elf'], G['native']
    world = World(beat, elf)
    rng = random.Random(seed)
    pair = Pair(elf, native, G['emcl'], world)
    mem = pair.ram
    table = world.table
    owners = world.owners()
    words = [u32(mem, table + 4 + 4 * uid) for uid in range(world.count)]
    hulls = [w & 0x3FFFFFFF for w in words if w]
    used = {u16(mem, a + 0xE) >> 8 for a in owners}
    free_uids = []
    for uid in range(world.count):
        if uid in used:
            break
        free_uids.append(uid)
    assert len(free_uids) >= 2, ('no owner-free leading uids for static cells', used)
    boxes = []
    for i in range(world.node_count):
        _, verts, _ = cp.node_ring(world, mem, i)
        boxes.append((min(v[0] for v in verts), max(v[0] for v in verts),
                      min(v[2] for v in verts), max(v[2] for v in verts)))
    kinds_pool = (0x00, 0x04, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x54, 0x59, 0x5A, 0x5B, 0x77)
    attr_pool = (0x05, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x54, 0x59, 0x5A, 0x77)
    out = {'static': 0, 'static_hit': 0, 'prims': 0, 'seg': [0, 0, 0, 0], 'cam': [0, 0, 0, 0], 'walkers': {},
           'head': [0, 0, 0], 'self_skip': 0, 'face_calls': 0, 'round_calls': 0}
    slot = {0: 0, 1: 1, 2: 2, 4: 3}
    for k in range(count):
        saved = []

        def patch(address, data):
            saved.append((address, bytes(mem[address:address + len(data)])))
            mem[address:address + len(data)] = data
        statics = []
        if rng.random() < 0.6:
            for i in range(rng.randrange(1, len(free_uids) + 1)):
                hull = rng.choice(hulls)
                statics.append((i, table + hull))
                out['static'] += 1
                flag = 0x80000000 | rng.choice((0, 0, 0, 0x40000000, 0x20000000))
                patch(table + 4 + 4 * i, struct.pack('<I', flag | hull))
        for i in range(world.count):
            patch(world.kind_view + 0x28 * i + 8, bytes([rng.choice(kinds_pool)]))
        for a in owners:
            if rng.random() < 0.6:
                patch(a + 0x54, bytes([rng.choice(kinds_pool)]))
        # The hull the segment aims at, with synthetic prims when they fit.
        if statics and rng.random() < 0.4:
            _, hull = rng.choice(statics)
        else:
            owner = rng.choice(owners)
            uid = u16(mem, owner + 0xE) >> 8
            word = u32(mem, table + 4 + 4 * uid) if uid < world.count else 0
            hull = table + word if word else table + rng.choice(hulls)
        lo, hi = struct.unpack_from('<3f', mem, hull), struct.unpack_from('<3f', mem, hull + 0xC)
        centre = [(lo[j] + hi[j]) / 2 for j in range(3)]
        if rng.random() < 0.6:
            prims, n = b'', 0
            for _ in range(rng.randrange(1, 5)):
                pick = rng.random()
                if pick < 0.35:
                    ext = [hi[j] - lo[j] for j in range(3)]
                    org = list(lo)
                    if rng.random() < 0.5:           # the negative-extent form
                        org = list(hi)
                        ext = [-x for x in ext]
                    prims += box_prim(rng.randrange(0, 8), org, ext)
                elif pick < 0.7:
                    prims += round_prim([centre[0] + rng.uniform(-1, 1), centre[1], centre[2] + rng.uniform(-1, 1)],
                                        rng.uniform(0.5, 4), rng.uniform(0.5, 3), wide=rng.random() < 0.3)
                else:
                    prims += filler_prim(wide=rng.random() < 0.5)
                n += 1
            if rng.random() < 0.1:
                prims += struct.pack('<HBB', 0x3000, 0, 0)   # an unknown type: no call, no advance
                n += 1
            old_size = sum(cp.prim_size(mem, a) for a in cp.prim_addresses(mem, hull))
            if len(prims) <= old_size:
                patch(hull + 0x18, struct.pack('<h', n))
                patch(hull + 0x1C, prims)
                out['prims'] += 1
        pair.rebind()
        # The segment: through the hull's box, or a grid node's ring.
        attrs = []
        if rng.random() < 0.7:
            pt = [rng.uniform(lo[j], hi[j]) for j in range(3)]
        else:
            i = rng.randrange(world.node_count)
            _, verts, _ = cp.node_ring(world, mem, i)
            pt = cp.inside(rng, verts)
        a = [pt[j] + rng.uniform(-6, 6) for j in range(3)]
        e = [pt[j] - (a[j] - pt[j]) * rng.uniform(0.3, 2.0) for j in range(3)]
        if rng.random() < 0.3:
            a, e = [pt[0], pt[1] + rng.uniform(1, 10), pt[2]], [pt[0], pt[1] - rng.uniform(1, 10), pt[2]]
        for i, (x0, x1, z0, z1) in enumerate(boxes):
            if (min(a[0], e[0]) <= x1 and x0 <= max(a[0], e[0]) and min(a[2], e[2]) <= z1 and
                    z0 <= max(a[2], e[2]) and rng.random() < 0.5):
                attr = rng.choice(attr_pool)
                patch(world.nodes + 64 * i + 0x1A, bytes([attr]))
                native.bridge_set_attr(pair.b, i, attr)
                attrs.append((i, attr))
        words = seg_words(a, e)
        words[0x7000324E] = rng.choice((0xFFFF, 0, 1, 2))
        if rng.random() < 0.3:
            words[0x70003254] = rng.choice(owners)
            out['self_skip'] += 1
        case = Case(world, rng, words)
        a32, e32 = tuple(f32(x) for x in a), tuple(f32(x) for x in e)
        for entry in (CELLS_SEG, CELLS_CAM, GRID_SEG, GRID_CAM):
            r, st = pair.both(f'synthetic {k} {entry:#x}', case, entry, ())
            if r and entry in (CELLS_SEG, CELLS_CAM) and st['entity'] == 0:
                out['static_hit'] += 1
            key = (hex(entry), r)
            out['walkers'][key] = out['walkers'].get(key, 0) + 1
        r, _ = pair.segment(case, f'synthetic {k} segment', a32, e32, rng.choice((6, 6, 2, 4, 3)))
        out['seg'][slot[r]] += 1
        r, _ = pair.camera(case, f'synthetic {k} camera', a32, e32, rng.choice((6, 6, 2, 4)))
        out['cam'][slot[r]] += 1
        # The probe module's 0019B6C0 with this module bound as its pass-2
        # workers: owners of kind 0x5A+ reach 001A50A0 / 001A5C30.
        for o in owners:
            if rng.random() < 0.5:
                patch(o + 0x54, bytes([rng.choice((0x5A, 0x5B, 0x77))]))
        pair.rebind()
        for i, attr in attrs:
            native.bridge_set_attr(pair.b, i, attr)
        top = (f32(pt[0]), f32(pt[1] + rng.uniform(0.5, 18)), f32(pt[2]))
        bottom = (f32(pt[0] + rng.uniform(-0.5, 0.5) * (k % 2)), f32(pt[1] - rng.uniform(0, 3)),
                  f32(pt[2] + rng.uniform(-0.5, 0.5) * (k % 2)))
        r, _ = pair.head(Case(world, rng), f'synthetic {k} 0019B6C0', top, bottom)
        out['head'][{0: 0, 2: 1, 4: 2}[r]] += 1
        out['face_calls'] += native.bridge_worker_calls(pair.b, 0)
        out['round_calls'] += native.bridge_worker_calls(pair.b, 1)
        for address, data in reversed(saved):
            mem[address:address + len(data)] = data
    pair.rebind()
    return out


def run_boundaries(item):
    """Deterministic cases on every gate: static kinds 0x4F..0x5A x the query
    class -1/0/1/2 x flags 0x80/0xA0/0xC0 on both cell walkers' pass 1, owner
    kinds 0x4F..0x52 on both pass 2s, grid attributes 0x4F..0x5A on both grid
    walkers, the repeated hit store after a hit (0x8000 prims), and the
    fail-stops."""
    beat = item
    elf, native = G['elf'], G['native']
    world = World(beat, elf)
    pair = Pair(elf, native, G['emcl'], world)
    mem = pair.ram
    table = world.table
    out = {'static': 0, 'owner': 0, 'attr': 0, 'hits': 0, 'repeat': 0, 'faults': 0, 'threshold': [], 'stop': 0,
           'reclamp': 0, 'plane': 0}
    owners = world.owners()
    crate = next(a for a in owners if (u16(mem, a + 0xE) >> 8) in (7, 8, 9, 10))
    uid = u16(mem, crate + 0xE) >> 8
    word = u32(mem, table + 4 + 4 * uid)
    hull = table + word
    lo, hi = struct.unpack_from('<3f', mem, hull), struct.unpack_from('<3f', mem, hull + 0xC)
    mid = [(lo[j] + hi[j]) / 2 for j in range(3)]
    top, bottom = (f32(mid[0]), f32(hi[1] + 2.0), f32(mid[2])), (f32(mid[0]), f32(lo[1] - 2.0), f32(mid[2]))
    saved_kind, saved_word0, saved_view = mem[crate + 0x54], bytes(mem[table + 4:table + 8]), mem[world.kind_view + 8]
    saved_free_1, saved_view_1 = bytes(mem[table + 8:table + 12]), mem[world.kind_view + 0x28 + 8]
    saved_kinds = {a: mem[a + 0x54] for a in owners}
    # Static cells over the crate's hull; the crate itself neutral (0x5A
    # passes neither pass 2).
    mem[crate + 0x54] = 0x5A
    for flag in (0x80000000, 0xA0000000, 0xC0000000):
        struct.pack_into('<I', mem, table + 4, flag | word)
        for kind in range(0x4F, 0x5B):
            mem[world.kind_view + 8] = kind
            pair.rebind()
            for q in (0xFFFF, 0, 1, 2):
                c = Case(world, None, {**seg_words(top, bottom), 0x7000324E: q})
                for entry in (CELLS_SEG, CELLS_CAM):
                    r, _ = pair.both(f'static {flag:#x} kind {kind:#x} class {q:#x} {entry:#x}', c, entry, ())
                    out['static'] += 1
                    out['hits'] += r
    mem[table + 4:table + 8] = saved_word0
    mem[world.kind_view + 8] = saved_view
    # Owner kinds on both pass 2s.
    for kind in (0x4F, 0x50, 0x51, 0x52):
        mem[crate + 0x54] = kind
        pair.rebind()
        c = Case(world, None, seg_words(top, bottom))
        for entry in (CELLS_SEG, CELLS_CAM):
            r, _ = pair.both(f'owner kind {kind:#x} {entry:#x}', c, entry, ())
            out['owner'] += 1
            out['hits'] += r
    # 0x8000 prims around a hit: before it they are skipped (their bytes
    # would hit as a round prim), after it in pass 2 they repeat the hit
    # stores. Pass 2 (the crate as owner) and pass 1 (a static cell over the
    # same hull, the crate excluded by kind 0x5A).
    tall = (tuple(mid), 3.0, 2.0)
    prims = filler_prim(False, tall) + round_prim(mid, 0.5, 0.5) + filler_prim(True, tall)
    old = bytes(mem[hull + 0x18:hull + 0x1C + len(prims)])
    struct.pack_into('<h', mem, hull + 0x18, 3)
    mem[hull + 0x1C:hull + 0x1C + len(prims)] = prims
    for kind, static in ((4, False), (0x5A, True)):
        mem[crate + 0x54] = kind
        if static:
            struct.pack_into('<I', mem, table + 4, 0x80000000 | word)
            mem[world.kind_view + 8] = 0x10
        pair.rebind()
        for entry in (CELLS_SEG, CELLS_CAM):
            r, st = pair.both(f'repeat {entry:#x} static {static}', Case(world, None, seg_words(top, bottom)),
                              entry, ())
            assert r == 1 and st['entity'] == (0 if static else crate), ('the round prim must hit', r, st['entity'])
            out['repeat'] += 1
    mem[table + 4:table + 8] = saved_word0
    mem[world.kind_view + 8] = saved_view
    mem[hull + 0x18:hull + 0x1C + len(prims)] = old
    # Pass 1 ends at the first word without bit 31, even when a later word
    # has it.
    free = next(u for u in range(1, world.count)
                if u not in {u16(mem, a + 0xE) >> 8 for a in owners} and not u32(mem, table + 4) & 0x80000000)
    saved_free = bytes(mem[table + 4 + 4 * free:table + 8 + 4 * free])
    struct.pack_into('<I', mem, table + 4 + 4 * free, 0x80000000 | word)
    saved_free_view = mem[world.kind_view + 0x28 * free + 8]
    mem[world.kind_view + 0x28 * free + 8] = 0x10
    mem[crate + 0x54] = 0x5A
    pair.rebind()
    for entry in (CELLS_SEG, CELLS_CAM):
        r, _ = pair.both(f'stop word {entry:#x}', Case(world, None, seg_words(top, bottom)), entry, ())
        assert r == 0, ('pass 1 must stop at word 0', r)
        out['stop'] += 1
    mem[table + 4 + 4 * free:table + 8 + 4 * free] = saved_free
    mem[world.kind_view + 0x28 * free + 8] = saved_free_view
    # The box re-clamp: two hulls moved far from the level along +x; the
    # first one's face hit shortens the segment, so the second one's box no
    # longer overlaps and its prim (which writes 0x70003600.. even when it
    # misses) is not tested. Pass 2 (owners in list order) and pass 1.
    crates = [a for a in owners if (u16(mem, a + 0xE) >> 8) in (7, 8, 9, 10)]
    first, second = crates[0], crates[1]
    moved = []
    for owner, x0 in ((first, 1004.0), (second, 1020.0)):
        h = table + u32(mem, table + 4 + 4 * (u16(mem, owner + 0xE) >> 8))
        moved.append((h, bytes(mem[h:h + 0x1C + 0x1C])))
        mem[h:h + 0x18] = struct.pack('<6f', x0, 0.0, 1000.0, x0 + 6.0, 10.0, 1010.0)
        struct.pack_into('<h', mem, h + 0x18, 1)
        mem[h + 0x1C:h + 0x38] = box_prim(2, (x0 + 1.0, 0.0, 1000.0), (5.0, 10.0, 10.0))
    far = seg_words((1000.0, 5.0, 1005.0), (1030.0, 5.0, 1005.0))
    for static in (False, True):
        for a in (first, second):
            mem[a + 0x54] = 0x5A if static else 4
        if static:
            for i, (h, _) in enumerate(moved):
                struct.pack_into('<I', mem, table + 4 + 4 * i, 0x80000000 | (h - table))
                mem[world.kind_view + 0x28 * i + 8] = 0x10
        pair.rebind()
        for entry in (CELLS_SEG, CELLS_CAM):
            r, st = pair.both(f're-clamp {entry:#x} static {static}', Case(world, None, far), entry, ())
            assert r == 1 and number(st['end'][0]) == 1005.0, ('the first hull must clamp the end', r, st['end'])
            assert number(st['face'][0]) != 1020.0, 'the second hull was tested'
            out['reclamp'] += 1
    for i in range(2):
        mem[table + 4 + 4 * i:table + 8 + 4 * i] = saved_word0 if i == 0 else saved_free_1
    for h, data in moved:
        mem[h:h + len(data)] = data
    for a in (first, second):
        mem[a + 0x54] = saved_kinds[a]
    mem[world.kind_view + 8] = saved_view
    mem[world.kind_view + 0x28 + 8] = saved_view_1
    mem[crate + 0x54] = saved_kind
    pair.rebind()
    # 001A50A0 with the segment ending (or starting) exactly on the face's
    # plane: rel * (min - end) is 0, which misses.
    for face, axis in ((2, 0), (4, 1), (6, 2)):
        for on_start in (False, True):
            a, e = [2.0, 2.0, 2.0], [2.0, 2.0, 2.0]
            a[axis], e[axis] = (0.0, 5.0) if on_start else (-5.0, 0.0)
            prim = box_prim(face, (0.0, 0.0, 0.0), (5.0, 5.0, 5.0))
            r, _ = pair.both(f'on plane face {face} start {on_start}', Case(world, None, seg_words(a, e)), FACE,
                             (PRIMS,), prim=prim, stage=[(PRIMS, prim)])
            assert r == 0, ('a segment ending on the plane misses', face, r)
            out['plane'] += 1
    # Grid attributes on a flat ground node under a vertical segment.
    rng = random.Random(9)
    ground = next(i for i in range(world.node_count)
                  if mem[world.nodes + 64 * i + 0x1A] == 5 and cp.node_ring(world, mem, i)[0][1] > 0.99)
    _, verts, _ = cp.node_ring(world, mem, ground)
    pt = cp.inside(rng, verts)
    a, e = (f32(pt[0]), f32(pt[1] + 4.0), f32(pt[2])), (f32(pt[0]), f32(pt[1] - 4.0), f32(pt[2]))
    at = world.nodes + 64 * ground + 0x1A
    for attr in range(0x4F, 0x5B):
        mem[at] = attr
        native.bridge_set_attr(pair.b, ground, attr)
        for q in (0xFFFF, 0, 1, 2):
            c = Case(world, None, {**seg_words(a, e), 0x7000324E: q})
            for entry in (GRID_SEG, GRID_CAM):
                r, _ = pair.both(f'attr {attr:#x} class {q:#x} {entry:#x}', c, entry, ())
                out['attr'] += 1
                out['hits'] += r
    mem[at] = 5
    pair.rebind()
    # 001A5C30's vertical test at the 1e-5 thresholds (bit exact). dx or dz
    # of the float just below / above the double 1e-5 decides the vertical
    # branch: a segment wholly below the cap misses there, while the
    # non-vertical branch tests the cap on the whole line (a hit). dy decides
    # whether the vertical branch tests the cap at all (a cap inside (0, dy)).
    near = (0x3727C5AB, 0x3727C5AC, 0x3727C5AD, 0x3727C5AE)
    prim = round_prim((0.5, 1.0 + 5e-6, 0.5), 2.0, 1.0)
    for lane in (0, 2, 1):
        for step in near:
            for sign in (0, 0x80000000):
                words = {0x70003190: 0, 0x70003194: bits(-5.0), 0x70003198: 0,
                         0x700031A0: 0, 0x700031A4: bits(-4.0), 0x700031A8: 0}
                if lane == 1:
                    words[0x70003194] = 0
                    words[0x700031A4] = step | sign
                else:
                    words[0x700031A0 + 4 * lane] = step | sign
                r, st = pair.both(f'001A5C30 threshold lane {lane} {step | sign:#x}', Case(world, None, words),
                                  ROUND, (PRIMS,), prim=prim, stage=[(PRIMS, prim)])
                out['threshold'].append((lane, step | sign, r, st['point'] if r else None))
    # Fail-stops: mask bit 0 without the lock workers; a static cell without
    # its kind view; a round prim without the SDK math. The queries leave the
    # state untouched (the bridge returns -8 otherwise).
    st = Case(world).native(world)
    native.bridge_no_workers(pair.b, 1)
    for which, m in ((0, 7), (1, 1)):
        r = native.bridge_run(pair.b, which, C.byref(st), fvec(top), fvec(bottom), m, 0, None)
        assert r == -1, ('a missing lock worker must fault', which, r)
        out['faults'] += 1
    native.bridge_no_workers(pair.b, 0)
    struct.pack_into('<I', mem, table + 4, 0x80000000 | word)
    pair.rebind()
    native.bridge_static_kinds(pair.b, None, 0)
    for which in (0, 1):
        r = native.bridge_run(pair.b, which, C.byref(st), fvec(top), fvec(bottom), 2, 0, None)
        assert r == -1, ('a static cell without its kind view must fault', which, r)
        out['faults'] += 1
    mem[table + 4:table + 8] = saved_word0
    mem[crate + 0x54] = 4
    struct.pack_into('<h', mem, hull + 0x18, 3)
    mem[hull + 0x1C:hull + 0x1C + len(prims)] = prims
    pair.rebind()
    native.bridge_no_math(pair.b, 1)
    r = native.bridge_run(pair.b, 0, C.byref(st), fvec(top), fvec(bottom), 2, 0, None)
    assert r == -1, ('a round prim without 0011E748 must fault', r)
    out['faults'] += 1
    native.bridge_no_math(pair.b, 0)
    mem[hull + 0x18:hull + 0x1C + len(prims)] = old
    mem[crate + 0x54] = saved_kind
    return out


# ---------------------------------------------------------------------------
# Main

def run_item(item):
    kind, arg = item
    return {'rows': run_rows, 'capture': run_capture, 'units': run_units, 'synth': run_synthetic,
            'bounds': run_boundaries}[kind](arg)


def main():
    import time
    t0 = time.time()
    missing = [p for p in [cp.PLAYABLE, cp.EXPORTER] + [ROUTE / b / 'eeMemory.bin' for b in BEATS]
               + [cp.DECOMP / 'extract/chunk15' / f for f in cp.CHUNK15] if not p.exists()]
    if missing:
        print('SKIP: the user-local inputs are missing:', [str(p) for p in missing])
        return 0
    cp.OUT = OUT
    emcl, verified, installed = cp.export_emcl()
    G['elf'] = read_elf()
    G['native'] = build_native()
    G['emcl'] = emcl
    row_items, total_rows = [], 0
    for beat in BEATS:
        rows = rows_of(beat)
        total_rows += len(rows)
        picked = reference_mode.select(rows, reference_mode.pick(len(rows), 3), seed=len(rows) + 5,
                                       axes=(lambda r: r[1],))
        row_items.append((beat, picked))
    picked_rows = sum(len(r) for _, r in row_items)
    unit_count = reference_mode.pick(1500, 90)
    synth_count = reference_mode.pick(300, 24)
    items = ([('rows', x) for x in row_items] + [('capture', b) for b in BEATS]
             + [('units', ('06_hill_slide', unit_count, 41)), ('units', ('08_truck_crossing', unit_count, 42))]
             + [('synth', ('05_boxes', synth_count, 51)), ('synth', ('08_truck_crossing', synth_count, 52))]
             + [('bounds', '05_boxes')])
    results = reference_mode.parallel_map(run_item, items,
                                          cost=lambda it: {'units': 4, 'synth': 4, 'bounds': 5}.get(it[0], 1))
    seg, cam, entities, locks, views = [0] * 4, [0] * 4, set(), 0, 0
    captures = {}
    units = {'face': {}, 'round': {}, 'walkers': {}}
    synth = {'static': 0, 'static_hit': 0, 'prims': 0, 'seg': [0] * 4, 'cam': [0] * 4, 'walkers': {},
             'head': [0, 0, 0], 'self_skip': 0, 'face_calls': 0, 'round_calls': 0}
    bounds = None
    for (kind, arg), res in zip(items, results):
        if kind == 'rows':
            seg = [a + b for a, b in zip(seg, res['seg'])]
            cam = [a + b for a, b in zip(cam, res['cam'])]
            entities |= res['entities']
            locks += res['locks']
            views += res['views']
        elif kind == 'capture':
            captures[arg] = res
        elif kind == 'units':
            for key in units:
                for k, v in res[key].items():
                    units[key][k] = units[key].get(k, 0) + v
        elif kind == 'synth':
            for key in ('static', 'static_hit', 'prims', 'self_skip', 'face_calls', 'round_calls'):
                synth[key] += res[key]
            for key in ('seg', 'cam', 'head'):
                synth[key] = [a + b for a, b in zip(synth[key], res[key])]
            for k, v in res['walkers'].items():
                synth['walkers'][k] = synth['walkers'].get(k, 0) + v
        else:
            bounds = res
    # Coverage the default run must reach.
    assert seg[0] and seg[3] and cam[0] and cam[3] and locks, (seg, cam, locks)
    assert views, 'no hit view compared'
    faces_hit = {k[0] for k, v in units['face'].items() if k[1] == 1}
    assert faces_hit == {1, 2, 3, 4, 5, 6}, units['face']
    round_classes = {k[2] for k in units['round'] if k[1] == 1}
    assert round_classes == {0x2000, 0x4000, 0x8000}, units['round']
    walker_hits = {k[0] for k, v in list(units['walkers'].items()) + list(synth['walkers'].items()) if k[1] == 1}
    assert walker_hits == {hex(CELLS_SEG), hex(CELLS_CAM), hex(GRID_SEG), hex(GRID_CAM)}, (units, synth)
    assert synth['prims'] and synth['self_skip'] and synth['head'][1] and synth['static_hit'], synth
    assert synth['face_calls'] and synth['round_calls'], ('the probe module never reached the bound workers', synth)
    assert bounds['faults'] == 5 and bounds['repeat'] == 4 and bounds['hits'], bounds
    assert bounds['stop'] == 2 and bounds['reclamp'] == 4 and bounds['plane'] == 6, bounds
    # Each threshold must change the outcome between its two sides (else the
    # cases would not pin it).
    outcome = {(lane, step): (r, p) for lane, step, r, p in bounds['threshold']}
    # (Negative steps are compared too, but do not pin: the non-vertical
    # branch then divides by the zero lane and misses, like the vertical one.)
    for lane in (0, 1, 2):
        below, above = outcome[(lane, 0x3727C5AC)], outcome[(lane, 0x3727C5AD)]
        assert below != above, ('threshold not pinned', lane, below, above)
    bounds['threshold'] = len(bounds['threshold'])
    elapsed = time.time() - t0
    reference_mode.banner(
        reference_mode.part(picked_rows, total_rows, 'route rows (7 queries each)'),
        f'{len(captures)} capture re-runs', f'{2 * unit_count} x 3 unit cases (001A50A0, 001A5C30, walkers)',
        f'{2 * synth_count} synthetic-world cases (4 walkers + 2 queries + 0019B6C0 each)')
    print(f'exporter: {verified} RAM images verified byte for byte ({installed})')
    print(f'route: 0019A570 none/lock/cells/grid {seg}, 0019A910 {cam}, owners hit '
          f'{sorted(hex(x) for x in entities)}, {locks} scripted lock calls, {views} hit views')
    lids = sum(1 for c in captures.values() if c['lid'])
    print(f'captures: the ceiling test reproduces the captured scratchpad on all {len(captures)} beats '
          f'({sum(1 for c in captures.values() if c["hit"])} hits, 0x70003A3C checked on {lids})')
    print(f'units: 001A50A0 {dict(sorted(units["face"].items()))}')
    print(f'       001A5C30 {dict(sorted(units["round"].items()))}')
    print(f'       walkers {dict(sorted(units["walkers"].items()))}')
    print(f'synthetic: {synth}')
    print(f'gate boundaries: {bounds}')
    print(f'PASS ({elapsed:.1f}s)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
