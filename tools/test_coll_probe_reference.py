#!/usr/bin/env python3
"""Execute the original surface and object probes and compare
src/game/em_coll_probe_original.c (docs/COLL_PROBES.md).

The user's pinned ELF and the captured AREA11 RAM supply every instruction
and every table; none are embedded here. The interpreter is the shared
bounded EE core (tools/test_player_slide_reference.EE), wrapped so that
every COP1 and VU0 macro instruction goes through tools/ee_float_model.py
(docs/EE_FLOAT_MODEL.md; the shared core's own float model is known wrong
there, section 5a). The shared file is not edited.

Original code executed (from the captured RAM, checked against the ELF):
  0019B6C0 surface record    001A2AE0 / 0019DF10 its walkers
  0019B8C0 object probe      001A32C0 / 0019E640 its walkers
  0019F1A0 ranks  0019ED80 grid node test  001A4030 / 001A4650 / 001A44B0
  prim tests  00102738 / 001028B8 / 001028D0 / 00103230 SDK vectors
  0019AB20 (beat 06's slide-entry class check only)
Hooked boundaries (scripted, recorded, compared call by call): 001A50A0 and
001A5C30, the surface walker's pass-2 tests of 0x2000 / 0x4000 prims (the
native side binds them as workers; no AREA11 owner reaches them).

Every case compares the whole scratchpad state the routines use
(0x70003190..0x700031D8, the cell record D_700030B0 +0x1A/+0x24,
0x70003680, 0x7000324E, 0x70003254, the ranks 0x70003240..0x7000324A,
0x70003B86/88) and the return value, and asserts that the original wrote
no other scratchpad or RAM byte.

The EMCL comes from the decomp exporter (tools/export_collision.py), run
here into build/coll_probe_reference/ with --verify-ram against the captured
RAM, so every run re-checks the exported grid byte for byte. When the
installed assets/scene_snow/snow.emcl carries the rank section it must be
byte-identical to that export.

Default run: a covering sample (about 10 s). EM_TEST_FULL=1: every route
row of every beat and the full random sweeps.
"""
import ctypes as C
import hashlib
import json
import random
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from test_player_slide_reference import EE, read_elf, bits, number  # noqa: E402
import ee_float_model as M  # noqa: E402
import reference_mode  # noqa: E402

DECOMP = ROOT.parent / 'Extermination'
ROUTE = DECOMP / 'build/s87/route'
PLAYABLE = DECOMP / 'build/startup-reference/playable_ee.bin'
EXPORTER = DECOMP / 'tools/export_collision.py'
CHUNK15 = ('f07_id52.bin', 'f08_id4d.bin', 'f09_id53.bin', 'f10_id5b.bin', 'f11_id4a.bin', 'f12_id44.bin')
OUT = ROOT / 'build/coll_probe_reference'
ASSET = ROOT / 'assets/scene_snow/snow.emcl'
BEATS = ('00_panel_no_battery', '01_battery', '02_elevator_refusal', '03_panel_power', '04_elevator_ride',
         '05_boxes', '06_hill_slide', '07_truck_preview', '08_truck_crossing', '09_fence_door',
         '10_cage_roof_roger', '11_crevice_prompt', '12_crevice_jump', '13_east_tower',
         '14_roger_encounter')
VERIFY_BEATS = ('05_boxes', '06_hill_slide', '08_truck_crossing', '14_roger_encounter')

HEAD, OBJECT, SURFACE_CELLS, SURFACE_GRID = 0x19B6C0, 0x19B8C0, 0x1A2AE0, 0x19DF10
OBJECT_CELLS, OBJECT_GRID, RANKS, NODE_TEST = 0x1A32C0, 0x19E640, 0x19F1A0, 0x19ED80
NGON, FACE, ROUND, FACE_SEG, ROUND_SEG, GROUND = 0x1A4030, 0x1A4650, 0x1A44B0, 0x1A50A0, 0x1A5C30, 0x19AB20
CODE = [(HEAD, 0x104), (OBJECT, 0x1B4), (SURFACE_CELLS, 0x7DC), (SURFACE_GRID, 0x370),
        (OBJECT_CELLS, 0x6B4), (OBJECT_GRID, 0x2EC), (RANKS, 0x184), (NODE_TEST, 0x418),
        (NGON, 0x480), (FACE, 0x1E0), (ROUND, 0x19C), (0x102738, 0x24), (0x1028B8, 0x14),
        (0x1028D0, 0x14), (0x103230, 0x18), (GROUND, 0x1E0), (0x19F730, 0x718), (0x19C830, 0x32C)]
PLAYER = 0x8102B0
ARGS = 0x7F0E0000           # private argument area inside the interpreter's stack region
PRIMS = 0x7F0D0000          # synthetic prims for the direct prim cases
CELL_RECORD = 0x700030B0    # D_700030B0
SLIDE_CLASS = 0x10          # node +0x1B of the hill's slide nodes

# The scratchpad words the probes read or write (the native state); every
# other scratchpad byte the original writes fails the case.
STATE_SPAN = [(0x70003190, 0x70003190 + 0x4C), (0x700030CA, 0x700030CC), (0x700030D4, 0x700030E0),
              (0x70003680, 0x70003684), (0x7000324E, 0x70003250), (0x70003254, 0x70003258),
              (0x70003240, 0x7000324C), (0x70003B86, 0x70003B8A)]


# ---------------------------------------------------------------------------
# The interpreter with the measured float model

class ProbeEE(EE):
    """EE with COP1 / VU0 macro arithmetic from ee_float_model (bit exact)."""

    def __init__(self, elf, ram=None, spad=None):
        super().__init__(elf, ram, spad)
        self.accb = 0

    def cop1(self, word, pc):
        rs = word >> 21 & 31
        if rs != 16:
            if rs == 20 and (word & 63) == 32:
                self.f[word >> 6 & 31] = M.ee_cvt_s_w(self.f[word >> 11 & 31] & 0xFFFFFFFF)
                return
            return super().cop1(word, pc)
        ft, fs, fd, fn = word >> 16 & 31, word >> 11 & 31, word >> 6 & 31, word & 63
        f = self.f
        a, b = f[fs] & 0xFFFFFFFF, f[ft] & 0xFFFFFFFF
        if fn == 0: f[fd] = M.ee_add(a, b)
        elif fn == 1: f[fd] = M.ee_sub(a, b)
        elif fn == 2: f[fd] = M.ee_mul(a, b)
        elif fn == 3: f[fd] = M.ee_div(a, b)
        elif fn == 6: f[fd] = M.ee_mov(a)
        elif fn == 7: f[fd] = M.ee_neg(a)
        elif fn == 24: self.accb = M.ee_add(a, b)
        elif fn == 25: self.accb = M.ee_sub(a, b)
        elif fn == 26: self.accb = M.ee_mul(a, b)
        elif fn == 28: f[fd] = M.ee_madd(self.accb, a, b)
        elif fn == 29: f[fd] = M.ee_msub(self.accb, a, b)
        elif fn == 36: f[fd] = M.ee_cvt_w_s(a)
        elif fn == 48: self.cond = False
        elif fn == 50: self.cond = bool(M.ee_c_eq(a, b))
        elif fn == 52: self.cond = bool(M.ee_c_lt(a, b))
        elif fn == 54: self.cond = bool(M.ee_c_le(a, b))
        else: raise AssertionError(('COP1 op outside the model', fn, hex(pc)))

    def macro(self, word):
        op, fs, ft, fd = word & 63, word >> 11 & 31, word >> 16 & 31, word >> 6 & 31
        dest = word >> 21 & 15
        if op < 4: name, bc = 'vaddbc', op
        elif 24 <= op < 28: name, bc = 'vmulbc', op - 24
        elif op == 40: name, bc = 'vadd', None
        elif op == 42: name, bc = 'vmul', None
        elif op == 44: name, bc = 'vsub', None
        else: raise AssertionError(('VU0 op these routines do not use', hex(word)))
        x, y = list(self.vf[fs]), list(self.vf[ft])
        out = list(self.vf[fd])
        for lane in range(4):
            if dest & (8 >> lane):
                t = y[bc] if bc is not None else y[lane]
                out[lane] = M.vu_lane(name, dest, bc, x[lane], t)
        if fd:
            self.vf[fd] = out


# ---------------------------------------------------------------------------
# The native side

BRIDGE = r"""
#include <stdlib.h>
#include <string.h>
#include "game/em_coll_probe_original.h"

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

typedef struct { int32_t prim; int32_t which; } BCall;

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
    EmCollProbeWorkers workers;
    int script_hit;
    uint32_t script_point[3];
    uint16_t script_class;
    BCall calls[64];
    int ncalls;
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

static int worker(Bridge *b, int which, const uint8_t *prim, EmCollProbeState *s, int *hit)
{
    if (b->ncalls < 64) {
        b->calls[b->ncalls].prim = (int32_t)(prim - b->table.bytes);
        b->calls[b->ncalls].which = which;
    }
    b->ncalls++;
    *hit = b->script_hit;
    if (b->script_hit) {
        memcpy(s->point, b->script_point, 12);
        s->cell_class = b->script_class;
    }
    return 0;
}
static int face_seg(void *c, const uint8_t *p, EmCollProbeState *s, int *hit) { return worker(c, 0, p, s, hit); }
static int round_seg(void *c, const uint8_t *p, EmCollProbeState *s, int *hit) { return worker(c, 1, p, s, hit); }

Bridge *bridge_new(const uint8_t *table, uint32_t size, const char *emcl)
{
    Bridge *b = calloc(1, sizeof *b);
    if (!b) return NULL;
    /* The directory image as the walkers see it (not through
     * em_actor_cells_init, which refuses the synthetic static words with
     * 0x20000000 this test stages; the walkers bound-check every read). */
    b->table.bytes = malloc(size);
    if (!b->table.bytes || size < 4) { free(b->table.bytes); free(b); return NULL; }
    memcpy(b->table.bytes, table, size);
    b->table.size = size;
    b->table.count = (int16_t)(table[0] | table[1] << 8);
    if (em_collision_load(&b->emcl, emcl) || em_coll_probe_grid_load(&b->grid, &b->emcl, emcl)) {
        em_actor_cells_free(&b->table); free(b); return NULL;
    }
    b->cells.table = &b->table;
    b->cells.lists = &b->lists;
    b->world.cells = &b->cells;
    b->world.grid = &b->grid;
    b->workers.context = b;
    b->workers.face_segment = face_seg;
    b->workers.round_segment = round_seg;
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

void bridge_script(Bridge *b, int hit, const uint32_t *point, uint16_t cls)
{
    b->script_hit = hit; memcpy(b->script_point, point, 12); b->script_class = cls; b->ncalls = 0;
}
int bridge_calls(Bridge *b, BCall *out) { memcpy(out, b->calls, sizeof b->calls); return b->ncalls; }

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

/* which: 0 head, 1 object, 2 ranks, 3 node test, 4 n-gon, 5 face, 6 round,
 * 7 surface cells, 8 surface grid, 9 object cells, 10 object grid. */
int bridge_run(Bridge *b, int which, BState *st, const float *a, const float *c, uint32_t self,
               uint8_t cls, uint32_t mask, const uint8_t *prim, int use_workers)
{
    EmCollProbeState s;
    to_native(b, st, &s);
    EmCollProbeState before = s;
    int r;
    b->ncalls = 0;
    switch (which) {
    case 0: r = em_coll_probe_0019B6C0(&b->world, use_workers ? &b->workers : NULL, &s, a, c); break;
    case 1: r = em_coll_probe_0019B8C0(&b->world, &s, ptr_of(b, self), cls, a, c, mask); break;
    case 2: r = em_coll_probe_0019F1A0(&b->grid, &s, a, mask); break;
    case 3: r = em_coll_probe_0019ED80(&b->grid, &s, (int)mask); break;
    case 4: r = em_coll_probe_001A4030(prim, &s); break;
    case 5: r = em_coll_probe_001A4650(prim, &s); break;
    case 6: r = em_coll_probe_001A44B0(prim, &s); break;
    case 7: r = em_coll_probe_001A2AE0(&b->world, use_workers ? &b->workers : NULL, &s); break;
    case 8: r = em_coll_probe_0019DF10(&b->world, &s); break;
    case 9: r = em_coll_probe_001A32C0(&b->world, &s); break;
    case 10: r = em_coll_probe_0019E640(&b->world, &s); break;
    default: return -9;
    }
    if (r < 0 && (which == 0 || which == 1) && memcmp(&before, &s, sizeof s)) return -8;
    from_native(b, &s, st);
    return r;
}

/* The floor-service adapters (EmPlayerFloorWorkers.head / .object). */
typedef struct {
    int kind; uint16_t node; uint8_t flags, type; int entity;
    float point[3], delta[3], normal[3], axis[3]; uint32_t owner;
} BHit;

int bridge_player(Bridge *b, int object, BState *st, const float *a, const float *c, uint32_t self,
                  uint8_t cls, uint32_t mask, BHit *out)
{
    EmCollProbeState s;
    to_native(b, st, &s);
    EmCollProbePlayer p = { &b->world, NULL, &s, { ptr_of(b, self), cls, NULL } };
    EmPlayerProbeHit h;
    int r = object ? em_coll_probe_player_object(&p, a, c, mask, &h) : em_coll_probe_player_head(&p, a, c, &h);
    if (r < 0) return r;
    out->kind = h.kind; out->node = h.node; out->flags = h.entity_flags; out->type = h.entity_type;
    out->entity = h.entity;
    memcpy(out->point, h.point, 12); memcpy(out->delta, h.delta, 12);
    memcpy(out->normal, h.normal, 12); memcpy(out->axis, h.axis, 12);
    out->owner = addr_of(b, h.owner);
    from_native(b, &s, st);
    return r;
}

/* Synthetic worlds: a grid node's attr byte (the RAM +0x1A the original
 * reads is patched to the same value). */
void bridge_set_attr(Bridge *b, int node, uint8_t attr) { b->emcl.polys[b->grid.first + node].attr = attr; }

uint32_t bridge_grid_first(Bridge *b) { return b->grid.first; }
uint32_t bridge_grid_count(Bridge *b) { return b->grid.count; }
int bridge_node_class(Bridge *b, int node) { return b->emcl.polys[b->grid.first + node].pad; }
"""


class BState(C.Structure):
    _fields_ = [('start', C.c_uint32 * 4), ('end', C.c_uint32 * 4), ('point', C.c_uint32 * 3),
                ('delta', C.c_uint32 * 3), ('record', C.c_int32), ('node', C.c_int32),
                ('entity', C.c_uint32), ('kind', C.c_int32), ('cell_class', C.c_uint16),
                ('cell_normal', C.c_uint32 * 3), ('ratio', C.c_uint32), ('query_class', C.c_int16),
                ('self', C.c_uint32), ('rank', C.c_int16 * 6), ('span_lo', C.c_int16),
                ('span_hi', C.c_int16)]


class BCall(C.Structure):
    _fields_ = [('prim', C.c_int32), ('which', C.c_int32)]


class BHit(C.Structure):
    _fields_ = [('kind', C.c_int), ('node', C.c_uint16), ('flags', C.c_uint8), ('type', C.c_uint8),
                ('entity', C.c_int), ('point', C.c_float * 3), ('delta', C.c_float * 3),
                ('normal', C.c_float * 3), ('axis', C.c_float * 3), ('owner', C.c_uint32)]


SOURCES = ['src/game/em_coll_probe_original.c', 'src/game/em_actor_collision.c',
           'src/game/em_collision.c', 'src/game/em_actor_pool.c']


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    source = OUT / 'bridge.c'
    source.write_text(BRIDGE)
    lib = OUT / ('bridge.dylib' if sys.platform == 'darwin' else 'bridge.so')
    command = ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared',
               '-fPIC', '-Isrc', str(source)] + SOURCES + ['-lm', '-o', str(lib)]
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
    n.bridge_new.restype = V; n.bridge_new.argtypes = [P, U32, P]
    n.bridge_static_kinds.argtypes = [V, P, C.c_uint]
    n.bridge_free.argtypes = [V]
    n.bridge_actor.argtypes = [V, U32, U8, U8, U8, U16, U16]
    n.bridge_list.argtypes = [V, C.POINTER(U32), I, I]
    n.bridge_script.argtypes = [V, I, C.POINTER(U32), U16]
    n.bridge_calls.argtypes = [V, C.POINTER(BCall)]
    n.bridge_run.argtypes = [V, I, C.POINTER(BState), PF, PF, U32, U8, U32, P, I]
    n.bridge_player.argtypes = [V, I, C.POINTER(BState), PF, PF, U32, U8, U32, C.POINTER(BHit)]
    n.bridge_grid_first.restype = n.bridge_grid_count.restype = U32
    n.bridge_grid_first.argtypes = n.bridge_grid_count.argtypes = [V]
    n.bridge_node_class.argtypes = [V, I]
    n.bridge_set_attr.argtypes = [V, I, U8]
    return n


# ---------------------------------------------------------------------------
# The EMCL export (decomp exporter, verified against captured RAM)

def export_emcl():
    OUT.mkdir(parents=True, exist_ok=True)
    out = OUT / 'snow.emcl'
    command = [sys.executable, str(EXPORTER)] + [str(DECOMP / 'extract/chunk15' / f) for f in CHUNK15]
    command += ['-o', str(out), '--at', '218.592,201.789', '--node-class']
    for beat in VERIFY_BEATS:
        command += ['--verify-ram', str(ROUTE / beat / 'eeMemory.bin')]
    command += ['--verify-ram', str(PLAYABLE)]
    run = subprocess.run(command, cwd=DECOMP, capture_output=True, text=True)
    assert run.returncode == 0, ('exporter', run.stdout, run.stderr)
    verified = [line for line in run.stdout.splitlines() if line.startswith('verified against')]
    assert len(verified) == len(VERIFY_BEATS) + 1, run.stdout
    data = out.read_bytes()
    assert struct.unpack_from('<I', data, 20)[0] == 7, 'flags: grid, node class, ranks'
    installed = 'not installed'
    if ASSET.exists() and struct.unpack_from('<I', ASSET.read_bytes(), 20)[0] & 4:
        assert ASSET.read_bytes() == data, 'assets/scene_snow/snow.emcl is stale (re-run the exporter)'
        installed = 'installed asset identical'
    return out, len(verified), installed


# ---------------------------------------------------------------------------
# Worlds

def u32(buf, at): return struct.unpack_from('<I', buf, at)[0]
def s16(buf, at): return struct.unpack_from('<h', buf, at)[0]
def u16(buf, at): return struct.unpack_from('<H', buf, at)[0]


def prim_size(buf, at):
    h = u16(buf, at)
    t, n = h & 0xF000, buf[at + 2]
    if t == 0x8000: return 0x24 if h & 0x800 else 0x14
    if t == 0x4000: return 0x2C if h & 0x800 else 0x18
    if t == 0x2000: return 0x1C
    if t == 0x1000: return 0x24 + 0x30 * n if h & 0x800 else 0x14 + 0x18 * n
    raise AssertionError(('unknown prim', hex(h)))


def prim_addresses(buf, hull):
    at, out = hull + 0x1C, []
    for _ in range(s16(buf, hull + 0x18)):
        out.append(at)
        at += prim_size(buf, at)
    return out


def directory_size(buf, base):
    count, size = u32(buf, base), 4 + 4 * u32(buf, base)
    for uid in range(count):
        word = u32(buf, base + 4 + 4 * uid)
        if not word:
            continue
        at = base + (word & 0x3FFFFFFF) + 0x1C
        for _ in range(s16(buf, base + (word & 0x3FFFFFFF) + 0x18)):
            at += prim_size(buf, at)
        size = max(size, at - base)
    return size


class World:
    def __init__(self, beat, elf):
        self.beat = beat
        self.ram = bytearray((ROUTE / beat / 'eeMemory.bin').read_bytes())
        self.spad = bytearray((ROUTE / beat / 'scratchpad.bin').read_bytes())
        check_code(elf, self.ram, beat)
        self.table = u32(self.spad, 0x3250)
        self.count = s16(self.spad, 0x324C)
        assert self.count == u32(self.ram, self.table)
        self.size = directory_size(self.ram, self.table)
        self.nodes = u32(self.spad, 0x3208)
        self.node_count = u32(self.spad, 0x320C)
        area, sub = self.ram[0x810700], self.ram[0x810701]
        self.kind_view = u32(self.ram, u32(self.ram, 0x24D7C0 + 4 * area) + 4 * sub)

    def image(self, ram=None):
        ram = self.ram if ram is None else ram
        return bytes(ram[self.table:self.table + self.size])

    def owners(self, ram=None):
        ram = self.ram if ram is None else ram
        pub, count = u32(ram, 0x275B7C), s16(ram, 0x275B84)
        return [u32(ram, pub + 4 * j) for j in range(count)]

    def kinds(self, ram=None):
        ram = self.ram if ram is None else ram
        return bytes(ram[self.kind_view + 0x28 * i + 8] for i in range(self.count))


def check_code(elf, ram, where):
    phoff = u32(elf, 28)
    kind, offset, vaddr = struct.unpack_from('<3I', elf, phoff)
    assert kind == 1 and vaddr == 0x100000
    for start, size in CODE:
        at = start - vaddr + offset
        assert bytes(ram[start:start + size]) == elf[at:at + size], (where, 'code differs from the ELF', hex(start))


def native_world(native, world, emcl, ram=None):
    ram = world.ram if ram is None else ram
    image = world.image(ram)
    b = native.bridge_new(image, len(image), str(emcl).encode())
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




# ---------------------------------------------------------------------------
# The shared initial state

def fvec(values):
    return (C.c_float * len(values))(*values)


def sx16(v):
    return v - 0x10000 if v & 0x8000 else v


def sx32(v):
    return v - 0x100000000 if v & 0x80000000 else v


class Case:
    """The scratchpad state both sides start from. The persistent words come
    from the capture (or are randomized); the per-call words start at values
    the native state can represent."""

    def __init__(self, world, rng=None, words=None):
        sp = world.spad
        w = {}
        for a in range(0x70003190, 0x700031D0, 4):
            w[a] = (u32(sp, a - 0x70000000), 4)
        w[0x700031D0] = (0, 4)
        w[0x700031D4] = (0, 4)
        w[0x700031D8] = (u32(sp, 0x31D8), 4)
        w[0x700030CA] = (u16(sp, 0x30CA), 2)
        for a in (0x700030D4, 0x700030D8, 0x700030DC, 0x70003680):
            w[a] = (u32(sp, a - 0x70000000), 4)
        w[0x7000324E] = (u16(sp, 0x324E), 2)
        w[0x70003254] = (0, 4)
        for i in range(6):
            w[0x70003240 + 2 * i] = (u16(sp, 0x3240 + 2 * i), 2)
        w[0x70003B86] = (u16(sp, 0x3B86), 2)
        w[0x70003B88] = (u16(sp, 0x3B88), 2)
        if rng is not None:
            for i in range(6):
                w[0x70003240 + 2 * i] = (rng.randrange(0, world.node_count), 2)
            w[0x700030CA] = (rng.choice((0, 0x1000, 0x2000, 0x4000, 0x8000, 0x0800)) | rng.randrange(256), 2)
        for a, v in (words or {}).items():
            w[a] = (v, w[a][1])
        self.words = w

    def native(self, world):
        w = {a: v for a, (v, _) in self.words.items()}
        st = BState()
        for k in range(4):
            st.start[k], st.end[k] = w[0x70003190 + 4 * k], w[0x700031A0 + 4 * k]
        for k in range(3):
            st.point[k], st.delta[k] = w[0x700031B0 + 4 * k], w[0x700031C0 + 4 * k]
            st.cell_normal[k] = w[0x700030D4 + 4 * k]
        st.record, st.node = record_of(world, w[0x700031D0])
        st.entity, st.kind = w[0x700031D4], sx32(w[0x700031D8])
        st.cell_class, st.ratio = w[0x700030CA], w[0x70003680]
        st.query_class, st.self = sx16(w[0x7000324E]), w[0x70003254]
        for i in range(6):
            st.rank[i] = sx16(w[0x70003240 + 2 * i])
        st.span_lo, st.span_hi = sx16(w[0x70003B86]), sx16(w[0x70003B88])
        return st

    def load(self, ee):
        for a, (v, size) in self.words.items():
            ee.save(a, v, size)


def record_of(world, value):
    if value == 0:
        return 0, -1
    if value == CELL_RECORD:
        return 1, -1
    off = value - world.nodes
    assert 0 <= off < 64 * world.node_count and off % 64 == 0, ('0x700031D0 names no record', hex(value))
    return 2, off // 64


def ee_state(ee, world):
    L = ee.load
    record, node = record_of(world, L(0x700031D0))
    return {
        'start': tuple(L(0x70003190 + 4 * k) for k in range(4)),
        'end': tuple(L(0x700031A0 + 4 * k) for k in range(4)),
        'point': tuple(L(0x700031B0 + 4 * k) for k in range(3)),
        'delta': tuple(L(0x700031C0 + 4 * k) for k in range(3)),
        'record': record, 'node': node, 'entity': L(0x700031D4), 'kind': sx32(L(0x700031D8)),
        'cell_class': L(0x700030CA, 2), 'cell_normal': tuple(L(0x700030D4 + 4 * k) for k in range(3)),
        'ratio': L(0x70003680), 'query_class': sx16(L(0x7000324E, 2)), 'self': L(0x70003254),
        'rank': tuple(sx16(L(0x70003240 + 2 * i, 2)) for i in range(6)),
        'span_lo': sx16(L(0x70003B86, 2)), 'span_hi': sx16(L(0x70003B88, 2)),
    }


def native_state(st):
    return {
        'start': tuple(st.start), 'end': tuple(st.end), 'point': tuple(st.point), 'delta': tuple(st.delta),
        'record': st.record, 'node': st.node, 'entity': st.entity, 'kind': st.kind,
        'cell_class': st.cell_class, 'cell_normal': tuple(st.cell_normal), 'ratio': st.ratio,
        'query_class': st.query_class, 'self': st.self, 'rank': tuple(st.rank),
        'span_lo': st.span_lo, 'span_hi': st.span_hi,
    }


def assert_in_span(written, where):
    for a, size in written:
        assert any(lo <= a and a + size <= hi for lo, hi in STATE_SPAN), \
            (where, 'the original wrote outside the modelled state', hex(a), size)


# ---------------------------------------------------------------------------
# One world on both sides

class Pair:
    """The original (interpreter over the captured RAM) and the native module
    over the same world."""

    def __init__(self, elf, native, emcl, world):
        self.world, self.native, self.emcl = world, native, emcl
        self.ee = ProbeEE(elf, world.ram, world.spad)
        self.ram = self.ee.mem          # the original's RAM (never written by these routines)
        self.spad0 = bytes(self.ee.spad)
        self.b = native_world(native, world, emcl, self.ram)
        self.calls, self.script = [], (0, (0, 0, 0), 0)
        self.ee.hooks[FACE_SEG] = lambda e: self._seg(e, 0)
        self.ee.hooks[ROUND_SEG] = lambda e: self._seg(e, 1)
        self.first = native.bridge_grid_first(self.b)

    def rebind(self):
        """A native world over the (patched) RAM."""
        self.native.bridge_free(self.b)
        self.b = native_world(self.native, self.world, self.emcl, self.ram)

    def _seg(self, e, which):
        self.calls.append((e.arg(0) - self.world.table, which))
        hit, point, cls = self.script
        if hit:
            for k in range(3):
                e.save(0x700031B0 + 4 * k, point[k])
            e.save(0x700030CA, cls, 2)
        e.ret_int(hit)

    def set_script(self, hit, point, cls):
        self.script = (hit, point, cls)
        self.native.bridge_script(self.b, hit, (C.c_uint32 * 3)(*point), cls)

    def original(self, case, entry, args, stage=()):
        """Run `entry` from the case's state; `stage` = [(address, bytes)]
        written before the call. Returns (r, state)."""
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
        assert_in_span(written, hex(entry))
        return sx32(ee.r[2] & 0xFFFFFFFF), ee_state(ee, self.world)

    def both(self, where, case, which, entry, args, a=(0.0, 0.0, 0.0), c=(0.0, 0.0, 0.0), self_addr=0,
             cls=0, mask=0, prim=None, stage=(), workers=1):
        want_r, want = self.original(case, entry, args, stage)
        st = case.native(self.world)
        got_r = self.native.bridge_run(self.b, which, C.byref(st), fvec(a), fvec(c), self_addr, cls, mask,
                                       prim, workers)
        got = native_state(st)
        assert want_r == got_r, (self.world.beat, where, 'return', want_r, got_r, want, got)
        for key, value in want.items():
            assert got[key] == value, (self.world.beat, where, key, value, got[key])
        calls = (BCall * 64)()
        n = self.native.bridge_calls(self.b, calls)
        assert [(calls[i].prim, calls[i].which) for i in range(min(n, 64))] == self.calls, \
            (self.world.beat, where, 'worker calls', self.calls)
        return want_r, want

    def args(self, *vectors):
        data = b''.join(struct.pack('<4f', *(list(v) + [0.0] * (4 - len(v)))) for v in vectors)
        return [(ARGS, data)]

    # The two probes, with the floor service's arguments.
    def head(self, case, where, top, bottom, workers=1):
        return self.both(where, case, 0, HEAD, (ARGS, ARGS + 0x10), top, bottom,
                         stage=self.args(top, bottom), workers=workers)

    def object(self, case, where, at, probe, mask, self_addr=PLAYER):
        cls = self.ram[self_addr + 2]
        return self.both(where, case, 1, OBJECT, (self_addr, ARGS, ARGS + 0x10, mask), at, probe,
                         self_addr=self_addr, cls=cls, mask=mask, stage=self.args(at, probe))

    def adapter(self, case, where, object, a, c, mask=7):
        """The EmPlayerFloorWorkers adapters: the fields 00175900 reads come
        from the original's record bytes."""
        entry = OBJECT if object else HEAD
        args = (PLAYER, ARGS, ARGS + 0x10, mask) if object else (ARGS, ARGS + 0x10)
        r, _ = self.original(case, entry, args, self.args(a, c))
        ee = self.ee
        st = case.native(self.world)
        hit = BHit()
        got = self.native.bridge_player(self.b, object, C.byref(st), fvec(a), fvec(c), PLAYER,
                                        self.ram[PLAYER + 2], mask, C.byref(hit))
        assert got == r and hit.kind == r, (where, r, got)
        if not r:
            return r
        record = ee.load(0x700031D0)
        assert hit.node == ee.load(record + 0x1A, 2), (where, 'record +0x1A', hex(hit.node))
        assert [bits(v) for v in hit.normal] == [ee.load(record + 0x24 + 4 * k) for k in range(3)], where
        assert [bits(v) for v in hit.point] == [ee.load(0x700031B0 + 4 * k) for k in range(3)], where
        if object:
            assert [bits(v) for v in hit.delta] == [ee.load(0x700031C0 + 4 * k) for k in range(3)], where
        owner = ee.load(0x700031D4)
        assert hit.owner == owner and hit.entity == (owner != 0), (where, hex(owner), hex(hit.owner))
        if owner:
            assert (hit.flags, hit.type) == (ee.load(owner + 2, 1), ee.load(owner + 3, 1)), where
        return r


# ---------------------------------------------------------------------------
# Case generators

def node_ring(world, ram, i):
    """(plane, [ring vertices]) of grid node i, read from the captured RAM."""
    base = u32(ram, 0x28A598)
    h = struct.unpack_from('<9I', ram, base)
    o = world.nodes + 64 * i
    cnt = ram[o + 0x18]
    il = base + h[4] + u32(ram, o + 0x1C)
    verts = [struct.unpack_from('<3f', ram, base + h[0] + 12 * s16(ram, il + 2 * k)) for k in range(cnt)]
    return struct.unpack_from('<4f', ram, o + 0x24), verts, ram[o + 0x1A]


def inside(rng, verts):
    """A point inside the ring (a random convex combination)."""
    w = [rng.random() + 0.05 for _ in verts]
    t = sum(w)
    return [sum(wk * v[a] for wk, v in zip(w, verts)) / t for a in range(3)]


def f32(v):
    return number(bits(v))


def feet_rows(beat):
    rows = json.loads((ROUTE / beat / 'trace.json').read_text())['rows']
    return [(r['counter'], tuple(f32(x) for x in r['pos']), r['p5']) for r in rows]


def up(v, dy):
    """(x, y + dy, z) with the EE add the floor service uses (00175900: +18)."""
    return (v[0], number(M.ee_add(bits(v[1]), bits(dy))), v[2])


def down(v, dy):
    return (v[0], number(M.ee_sub(bits(v[1]), bits(dy))), v[2])


PROBE = (0.0, -13.8, 0.0)   # +280, 0015C420's floor-probe vector


# ---------------------------------------------------------------------------
# Work items (one per world; forked workers)

G = {}


def run_beat(item):
    beat, rows = item
    elf, native, emcl = G['elf'], G['native'], G['emcl']
    world = World(beat, elf)
    pair = Pair(elf, native, emcl, world)
    out = {'head': [0, 0, 0], 'object': [0, 0, 0], 'records': set(), 'surfaces': set(), 'entities': set(),
           'cases': 0}
    base_case = Case(world)

    def tally(kind, r, state):
        out[kind][{0: 0, 2: 1, 4: 2}[r]] += 1
        out['cases'] += 1
        if r:
            out['records'].add((kind, state['record']))
            if state['entity']:
                out['entities'].add(state['entity'])

    for counter, feet, _ in rows:
        where = f'row {counter}'
        r, s = pair.head(base_case, where + ' head', up(feet, 18.0), feet)
        tally('head', r, s)
        if r:
            out['surfaces'].add(pair.ee.load(pair.ee.load(0x700031D0) + 0x1A, 1))
        at = down(feet, 0.2)
        r, s = pair.object(base_case, where + ' object', at, PROBE, 7)
        tally('object', r, s)
    # The capture: the snapshot's player bytes. The last stage was idle, so
    # the floor service ran at the feet +A0 (after the idle tail's 0.2
    # lowering and the ground delta): +250 is the surface record's point y
    # or, on a miss, the feet y; +23A is the surface record's byte, or in
    # contact the object probe's (00175900; its miss rule is not checked).
    ram = world.ram
    p = PLAYER
    assert (ram[p + 4], ram[p + 5]) == (1, 0), (beat, 'the snapshot stage is not idle')
    feet = struct.unpack_from('<3f', ram, p + 0xA0)
    r, s = pair.head(base_case, 'capture head', up(feet, 18.0), feet)
    surface_y = number(s['point'][1]) if r else feet[1]
    assert bits(surface_y) == u32(ram, p + 0x250), (beat, 'capture +250', surface_y)
    checked = None
    if r:
        byte = pair.ee.load(pair.ee.load(0x700031D0) + 0x1A, 1)
        assert byte == ram[p + 0x23A], (beat, 'capture +23A (surface record)', byte)
        checked = 'surface record'
    elif ram[p + 0xA]:
        ro, _ = pair.object(base_case, 'capture object', down(feet, 0.2), PROBE, 7)
        if ro:
            byte = pair.ee.load(pair.ee.load(0x700031D0) + 0x1A, 1)
            assert byte == ram[p + 0x23A], (beat, 'capture +23A (object probe)', byte, ram[p + 0x23A])
            checked = 'object probe'
    out['capture'] = checked
    # Adapters on a few rows.
    for counter, feet, _ in rows[:: max(1, len(rows) // 3)]:
        pair.adapter(base_case, f'adapter head {counter}', 0, up(feet, 18.0), feet)
        pair.adapter(base_case, f'adapter object {counter}', 1, down(feet, 0.2), PROBE)
    return out


def run_surface_nodes(item):
    """Points on the surface nodes (attr 0x5A..0x77) and on the object
    probe's low-attr nodes, from above, below and on the plane."""
    beat, count, seed = item
    elf, native, emcl = G['elf'], G['native'], G['emcl']
    world = World(beat, elf)
    pair = Pair(elf, native, emcl, world)
    rng = random.Random(seed)
    ram = world.ram
    surface = [i for i in range(world.node_count) if 0x5A <= ram[world.nodes + 64 * i + 0x1A] < 0x78]
    low = [i for i in range(world.node_count) if ram[world.nodes + 64 * i + 0x1A] < 0x1E]
    out = {'head': [0, 0, 0], 'object': [0, 0, 0], 'rank_sentinel': 0}
    for k in range(count):
        i = rng.choice(surface) if k % 2 == 0 else rng.choice(low)
        plane, verts, attr = node_ring(world, ram, i)
        pt = inside(rng, verts)
        if rng.random() < 0.3:
            pt = [pt[0] + rng.uniform(-3, 3), pt[1], pt[2] + rng.uniform(-3, 3)]
        feet = tuple(f32(v) for v in (pt[0], pt[1] + rng.choice((-1.0, -0.2, 0.0, 0.3, 2.0, 12.0)), pt[2]))
        case = Case(world, rng if rng.random() < 0.5 else None)
        r, _ = pair.head(case, f'surface node {i} head', up(feet, 18.0), feet)
        out['head'][{0: 0, 2: 1, 4: 2}[r]] += 1
        mask = rng.choice((7, 7, 6, 4, 2, 0, 3, 5))
        probe = (0.0, rng.choice((-13.8, -13.8, 4.0, -0.5)), 0.0)
        words = {}
        if rng.random() < 0.1:
            words[0x70003244] = 0xFFFF          # 0019E640's -1 rank-y sentinel
            out['rank_sentinel'] += 1
        case = Case(world, None, words)
        r, _ = pair.object(case, f'node {i} object mask {mask}', feet, probe, mask)
        out['object'][{0: 0, 2: 1, 4: 2}[r]] += 1
    return out


def run_units(item):
    """0019F1A0, 0019ED80, the prim tests and the four walkers on their own,
    over staged segment states (oblique ones included)."""
    beat, count, seed = item
    elf, native, emcl = G['elf'], G['native'], G['emcl']
    world = World(beat, elf)
    pair = Pair(elf, native, emcl, world)
    rng = random.Random(seed)
    ram = world.ram
    verts_base = u32(ram, 0x28A598) + struct.unpack_from('<9I', ram, u32(ram, 0x28A598))[0]
    nverts = struct.unpack_from('<9I', ram, u32(ram, 0x28A598))[1]
    out = {'ranks': 0, 'node': [0, 0], 'prims': {}, 'walkers': {}}
    # 0019F1A0: random points and exact vertex keys.
    for k in range(count):
        if k % 3 == 0:
            v = struct.unpack_from('<3f', ram, verts_base + 12 * rng.randrange(nverts))
            point = tuple(f32(x) for x in v)
        else:
            point = (f32(rng.uniform(-80, 590)), f32(rng.uniform(-90, 460)), f32(rng.uniform(-200, 670)))
        mask = rng.randrange(64)
        case = Case(world, rng)
        pair.both(f'0019F1A0 {point} {mask:#x}', case, 2, RANKS, (ARGS, mask), point, stage=pair.args(point),
                  mask=mask)
        out['ranks'] += 1
    # 0019ED80: segments through random nodes.
    for k in range(count):
        i = rng.randrange(world.node_count)
        plane, verts, _ = node_ring(world, ram, i)
        pt = inside(rng, verts)
        if rng.random() < 0.3:
            pt = [pt[j] + rng.uniform(-2, 2) for j in range(3)]
        n = plane[:3]
        span = rng.uniform(0.5, 20)
        side = rng.choice((1, -1, 1))
        a = [pt[j] + n[j] * span * side + rng.uniform(-1, 1) * (k % 2) for j in range(3)]
        e = [pt[j] - n[j] * span * rng.uniform(0.0, 1.2) for j in range(3)]
        words = {}
        for j in range(3):
            words[0x70003190 + 4 * j] = bits(f32(a[j]))
            words[0x700031A0 + 4 * j] = bits(f32(e[j]))
        case = Case(world, None, words)
        r, _ = pair.both(f'0019ED80 node {i}', case, 3, NODE_TEST, (0x70003190, world.nodes + 64 * i), mask=i)
        out['node'][r] += 1
    # The prim tests over the directory's own prims and synthetic round prims.
    prims = []
    table = world.table
    for uid in range(world.count):
        word = u32(ram, table + 4 + 4 * uid)
        if not word:
            continue
        at = table + (word & 0x3FFFFFFF) + 0x1C
        for _ in range(s16(ram, table + (word & 0x3FFFFFFF) + 0x18)):
            prims.append(at)
            at += prim_size(ram, at)
    for k in range(count):
        if rng.random() < 0.6:
            family = rng.choice(sorted({u16(ram, a) & 0xF000 for a in prims}))
            address = rng.choice([a for a in prims if u16(ram, a) & 0xF000 == family])
            data = bytes(ram[address:address + prim_size(ram, address)])
        else:
            header = rng.choice((0x4000, 0x8000)) | rng.choice((0, 0x800))
            centre = (rng.uniform(200, 300), rng.uniform(180, 260), rng.uniform(200, 400))
            data = struct.pack('<HBB5f', header, 0, 0, *centre, rng.uniform(0.5, 6), rng.uniform(0.5, 6))
            data = (data + bytes(0x40))[:prim_size(data + bytes(0x40), 0)]
        h = u16(data, 0)
        kind = h & 0xF000
        # A segment near the prim: vertical through its reference point, or oblique.
        ref = struct.unpack_from('<3f', data, 0x14 if kind == 0x1000 else 4)
        if kind == 0x1000 and rng.random() < 0.7:
            cnt = data[2]
            ref = inside(rng, [struct.unpack_from('<3f', data, 0x14 + 12 * j) for j in range(cnt)])
        if kind == 0x2000 and rng.random() < 0.7:
            o = struct.unpack_from('<6f', data, 4)
            ref = (o[0] + o[3] * rng.random(), o[1], o[2] + o[5] * rng.random())
        dy = rng.uniform(1, 30)
        x, z = ref[0] + rng.uniform(-1, 1) * (k % 3 == 0), ref[2] + rng.uniform(-1, 1) * (k % 3 == 0)
        sign = rng.choice((1, -1))
        if kind == 0x2000 and data[2] in (3, 4) and rng.random() < 0.7:
            sign = 1 if data[2] == 3 else -1      # across the top face downward, the bottom upward
        a = (x, ref[1] + dy * sign, z)
        e = (x + rng.uniform(-3, 3) * (k % 4 == 0), ref[1] - sign * dy * rng.uniform(-0.5, 1.5),
             z + rng.uniform(-3, 3) * (k % 4 == 0))
        words = {}
        for j in range(3):
            words[0x70003190 + 4 * j] = bits(f32(a[j]))
            words[0x700031A0 + 4 * j] = bits(f32(e[j]))
        case = Case(world, rng, words)
        entry, which = {0x1000: (NGON, 4), 0x2000: (FACE, 5), 0x4000: (ROUND, 6), 0x8000: (ROUND, 6)}[kind]
        r, _ = pair.both(f'prim {h:#x}', case, which, entry, (PRIMS,), prim=data, stage=[(PRIMS, data)])
        key = (hex(h), r)
        out['prims'][key] = out['prims'].get(key, 0) + 1
    # The walkers on staged segments (oblique too): 001A2AE0, 0019DF10,
    # 001A32C0, 0019E640.
    owner_hulls = []
    for a in world.owners():
        uid = u16(ram, a + 0xE) >> 8
        word = u32(ram, table + 4 + 4 * uid) if uid < world.count else 0
        if word:
            owner_hulls.append(table + word)
    surface = [i for i in range(world.node_count) if 0x5A <= ram[world.nodes + 64 * i + 0x1A] < 0x78]
    walkers = ((SURFACE_CELLS, 7), (SURFACE_GRID, 8), (OBJECT_CELLS, 9), (OBJECT_GRID, 10))
    for k in range(count):
        entry, which = walkers[k % 4]
        i = rng.choice(surface) if entry == SURFACE_GRID and k % 8 < 6 else rng.randrange(world.node_count)
        _, verts, _ = node_ring(world, ram, i)
        pt = inside(rng, verts)
        if owner_hulls and entry in (SURFACE_CELLS, OBJECT_CELLS) and k % 8 < 6:   # an owner cell's box
            hull = rng.choice(owner_hulls)
            lo, hi = struct.unpack_from('<3f', ram, hull), struct.unpack_from('<3f', ram, hull + 0xC)
            pt = [rng.uniform(lo[j], hi[j]) for j in range(3)]
            pt[1] = hi[1] - rng.uniform(0, 2)
        a = (pt[0] + rng.uniform(-4, 4) * (k % 2), pt[1] + rng.uniform(-5, 25), pt[2] + rng.uniform(-4, 4) * (k % 2))
        e = (pt[0] + rng.uniform(-4, 4) * (k % 2), pt[1] - rng.uniform(-2, 10), pt[2] + rng.uniform(-4, 4) * (k % 2))
        words = {}
        for j in range(3):
            words[0x70003190 + 4 * j] = bits(f32(a[j]))
            words[0x700031A0 + 4 * j] = bits(f32(e[j]))
        if rng.random() < 0.2:
            words[0x70003254] = rng.choice(world.owners() or [0])
        case = Case(world, rng, words)
        r, _ = pair.both(f'walker {entry:#x} node {i}', case, which, entry, ())
        key = (hex(entry), r)
        out['walkers'][key] = out['walkers'].get(key, 0) + 1
    return out


def run_synthetic(item):
    """Worlds the capture does not hold, made from it by patching RAM (and
    the native side's view of the same bytes): static cells in pass 1, owner
    kinds that reach the surface walker's pass 2 (001A50A0 / 001A5C30
    worker calls, scripted), the self skip, a NULL list entry."""
    beat, count, seed = item
    elf, native, emcl = G['elf'], G['native'], G['emcl']
    base_world = World(beat, elf)
    rng = random.Random(seed)
    out = {'static': 0, 'pass2': 0, 'worker_calls': 0, 'self_skip': 0, 'faults': 0, 'cell_head': 0,
           'object': [0, 0, 0], 'round': 0}
    ram0 = base_world.ram
    table = base_world.table
    owners = base_world.owners()
    words = [u32(ram0, table + 4 + 4 * uid) for uid in range(base_world.count)]
    hulls = [w for w in words if w]
    used = {u16(ram0, a + 0xE) >> 8 for a in owners}
    free_uids = []
    for uid in range(base_world.count):
        if uid in used:
            break
        free_uids.append(uid)
    assert len(free_uids) >= 2, ('no owner-free leading uids for static cells', used)
    pair = Pair(elf, native, G['emcl'], base_world)
    mem = pair.ram
    world = base_world
    # Each node's x/z box (to patch the attrs of the nodes under a point).
    boxes = []
    for i in range(world.node_count):
        _, verts, _ = node_ring(world, mem, i)
        boxes.append((min(v[0] for v in verts), max(v[0] for v in verts),
                      min(v[2] for v in verts), max(v[2] for v in verts)))
    owner_hulls = [(a, table + u32(mem, table + 4 + 4 * (u16(mem, a + 0xE) >> 8))) for a in owners
                   if (u16(mem, a + 0xE) >> 8) < world.count and u32(mem, table + 4 + 4 * (u16(mem, a + 0xE) >> 8))]
    kinds_pool = (0x04, 0x0D, 0x1D, 0x1E, 0x1D, 0x1E, 0x46, 0x59, 0x5A, 0x5B, 0x77, 0x78)
    attr_pool = (0x00, 0x05, 0x1D, 0x1E, 0x59, 0x5A, 0x77, 0x78)
    for k in range(count):
        saved = []

        def patch(address, data):
            saved.append((address, bytes(mem[address:address + len(data)])))
            mem[address:address + len(data)] = data
        # Pass 1: the first few directory words (uids no owner uses) become
        # static cells (bit 31; some 0x40000000-disabled, some
        # 0x20000000-gated) over existing hulls.
        nstatic = rng.randrange(1, len(free_uids) + 1) if rng.random() < 0.6 else 0
        statics = []
        for i in range(nstatic):
            hull = rng.choice(hulls)
            statics.append((i, table + hull))
            flag = 0x80000000 | rng.choice((0, 0, 0x40000000, 0x20000000, 0x20000000))
            patch(table + 4 + 4 * i, struct.pack('<I', flag | hull))
        for i in range(world.count):
            patch(world.kind_view + 0x28 * i + 8, bytes([rng.choice(kinds_pool)]))
        # Owner kinds (+0x54) around both walkers' gates.
        for a in owners:
            if rng.random() < 0.6:
                patch(a + 0x54, bytes([rng.choice(kinds_pool)]))
        # A point on top of an owner cell, a static cell, or a grid node.
        target = rng.random()
        node = None
        if target < 0.4 or (target < 0.7 and not statics):
            owner, hull = rng.choice(owner_hulls)
            patch(owner + 0x54, bytes([rng.choice((0x04, 0x1D, 0x1E, 0x59, 0x5A, 0x77, 0x78))]))
        elif target < 0.7:
            uid, hull = rng.choice(statics)
            patch(world.kind_view + 0x28 * uid + 8, bytes([rng.choice((0x1D, 0x1E, 0x59, 0x5A, 0x77, 0x78))]))
        else:
            hull = None
        if hull is not None:
            lo = struct.unpack_from('<3f', mem, hull)
            hi = struct.unpack_from('<3f', mem, hull + 0xC)
            if rng.random() < 0.35:
                # Round prims (none in AREA11): 0x8000 / 0x4000, narrow or
                # wide, centred in the box, replacing the hull's prims.
                centre = [rng.uniform(lo[j], hi[j]) for j in range(3)]
                prims = b''
                for header in (0x8000 | rng.choice((0, 0x800)), 0x4000 | rng.choice((0, 0x800))):
                    body = struct.pack('<HBB5f', header, 0, 0, centre[0], hi[1] - rng.uniform(0, 3), centre[2],
                                       rng.uniform(1, 8), rng.uniform(0.5, 3))
                    size = prim_size(body + bytes(0x40), 0)
                    prims += (body + bytes(0x40))[:size]
                old_size = sum(prim_size(mem, a) for a in prim_addresses(mem, hull))
                if len(prims) <= old_size:
                    patch(hull + 0x18, struct.pack('<h', 2))
                    patch(hull + 0x1C, prims)
                    out['round'] += 1
            feet = (f32(rng.uniform(lo[0], hi[0])), f32(hi[1] + rng.uniform(-1.5, 1.0)),
                    f32(rng.uniform(lo[2], hi[2])))
        else:
            node = rng.randrange(world.node_count)
            _, verts, _ = node_ring(world, mem, node)
            pt = inside(rng, verts)
            feet = (f32(pt[0]), f32(pt[1] + rng.uniform(-1.0, 1.0)), f32(pt[2]))
        pair.rebind()
        # The grid nodes under the point get attrs around the walkers' gates.
        for i, (x0, x1, z0, z1) in enumerate(boxes):
            if i == node or (x0 <= feet[0] <= x1 and z0 <= feet[2] <= z1 and rng.random() < 0.5):
                attr = rng.choice(attr_pool)
                patch(world.nodes + 64 * i + 0x1A, bytes([attr]))
                native.bridge_set_attr(pair.b, i, attr)
        hit = rng.random() < 0.5
        pair.set_script(hit, tuple(bits(f32(feet[j] + (rng.uniform(-0.5, 0.5), rng.uniform(-3, 3),
                                                       rng.uniform(-0.5, 0.5))[j])) for j in range(3)),
                        rng.choice((0x2000, 0x4000, 0x8000)) | rng.randrange(256))
        qc = rng.choice((0, 0, 1, 2))
        case = Case(world, rng, {0x7000324E: qc})
        r, st = pair.head(case, f'synthetic head {k}', up(feet, 18.0), feet)
        out['worker_calls'] += len(pair.calls)
        out['cell_head'] += st['record'] == 1 and r != 0
        out['static'] += nstatic
        self_addr = rng.choice(owners + [PLAYER, PLAYER])
        if self_addr != PLAYER:
            out['self_skip'] += 1
        r, st = pair.object(case, f'synthetic object {k}', down(feet, 0.2), PROBE, rng.choice((7, 7, 2, 6)),
                            self_addr)
        out['object'][{0: 0, 2: 1, 4: 2}[r]] += 1
        out['pass2'] += 1
        for address, data in reversed(saved):
            mem[address:address + len(data)] = data
    pair.rebind()
    # Fail-stop: the surface walker's pass 2 reaching a 0x2000 prim with no
    # worker bound faults, and the caller's state is untouched (the bridge
    # returns -8 if it changed).
    crate = next(a for a in owners if (u16(mem, a + 0xE) >> 8) in (7, 8, 9, 10))
    saved_kind = mem[crate + 0x54]
    mem[crate + 0x54] = 0x5A
    pair.rebind()
    uid = u16(mem, crate + 0xE) >> 8
    hull = table + u32(mem, table + 4 + 4 * uid)
    lo, hi = struct.unpack_from('<3f', mem, hull), struct.unpack_from('<3f', mem, hull + 0xC)
    feet = (f32((lo[0] + hi[0]) / 2), f32(hi[1] - 0.5), f32((lo[2] + hi[2]) / 2))
    st = Case(base_world).native(base_world)
    r = native.bridge_run(pair.b, 0, C.byref(st), fvec(up(feet, 18.0)), fvec(feet), 0, 0, 0, None, 0)
    assert r == -1, ('missing 001A50A0 worker must fault', r)
    out['faults'] += 1
    mem[crate + 0x54] = saved_kind
    # A static cell without the D_0024D7C0 kind view faults the same way.
    saved_word = bytes(mem[table + 4:table + 8])
    struct.pack_into('<I', mem, table + 4, 0x80000000 | hulls[0])
    pair.rebind()
    native.bridge_static_kinds(pair.b, None, 0)
    st = Case(base_world).native(base_world)
    r = native.bridge_run(pair.b, 1, C.byref(st), fvec(feet), fvec(PROBE), PLAYER, 0, 7, None, 1)
    assert r == -1, ('a static cell without its kind view must fault', r)
    out['faults'] += 1
    mem[table + 4:table + 8] = saved_word
    return out


def run_boundaries(item):
    """Deterministic cases on every gate boundary the walkers test: grid
    attrs 0x1D/0x1E (0019E640) and 0x59/0x5A/0x77/0x78 (0019DF10), static
    cell flags x query class x kinds on both walkers' pass 1, and owner kinds
    on both pass 2s."""
    beat = item
    elf, native = G['elf'], G['native']
    world = World(beat, elf)
    pair = Pair(elf, native, G['emcl'], world)
    mem = pair.ram
    table = world.table
    out = {'attr': 0, 'static': 0, 'owner': 0, 'hits': 0}
    case = Case(world)
    # A flat ground node (attr 5, normal up) and a point in it.
    rng = random.Random(7)
    ground = next(i for i in range(world.node_count)
                  if mem[world.nodes + 64 * i + 0x1A] == 5 and node_ring(world, mem, i)[0][1] > 0.99)
    _, verts, _ = node_ring(world, mem, ground)
    pt = inside(rng, verts)
    feet = (f32(pt[0]), f32(pt[1] - 1.0), f32(pt[2]))
    at = world.nodes + 64 * ground + 0x1A
    for attr in (0x1D, 0x1E, 0x59, 0x5A, 0x77, 0x78):
        mem[at] = attr
        native.bridge_set_attr(pair.b, ground, attr)
        for q in (0, 1):
            c = Case(world, None, {0x7000324E: q})
            r1, _ = pair.head(c, f'attr {attr:#x} head', up(feet, 18.0), feet)
            r2, _ = pair.object(c, f'attr {attr:#x} object', down(feet, 0.2), PROBE, 7)
            out['attr'] += 1
            out['hits'] += bool(r1) + bool(r2)
    mem[at] = 5
    pair.rebind()
    # Static cells over a crate's hull (the crate owner itself neutral: kind
    # 0x46 passes neither walker).
    owners = world.owners()
    crate = next(a for a in owners if (u16(mem, a + 0xE) >> 8) in (7, 8, 9, 10))
    uid = u16(mem, crate + 0xE) >> 8
    word = u32(mem, table + 4 + 4 * uid)
    hull = table + word
    lo, hi = struct.unpack_from('<3f', mem, hull), struct.unpack_from('<3f', mem, hull + 0xC)
    top = (f32((lo[0] + hi[0]) / 2 + 1.25), f32(hi[1] - 0.5), f32((lo[2] + hi[2]) / 2 - 2.5))
    saved_kind, saved_word0 = mem[crate + 0x54], bytes(mem[table + 4:table + 8])
    saved_view = mem[world.kind_view + 8]
    mem[crate + 0x54] = 0x46
    for flag in (0x80000000, 0xA0000000, 0xC0000000):
        struct.pack_into('<I', mem, table + 4, flag | word)
        for kind in (0x1D, 0x1E, 0x59, 0x5A):
            mem[world.kind_view + 8] = kind
            pair.rebind()
            for q in (0, 2):
                c = Case(world, None, {0x7000324E: q})
                pair.set_script(0, (0, 0, 0), 0)
                r1, _ = pair.head(c, f'static {flag:#x} kind {kind:#x} class {q} head', up(top, 18.0), top)
                r2, _ = pair.object(c, f'static {flag:#x} kind {kind:#x} class {q} object', down(top, 0.2),
                                    PROBE, 2, PLAYER)
                out['static'] += 1
                out['hits'] += bool(r1) + bool(r2)
    mem[table + 4:table + 8] = saved_word0
    mem[world.kind_view + 8] = saved_view
    # Owner kinds on both pass 2s (001A50A0 scripted as a hit for 0x5A+).
    for kind in (0x04, 0x1D, 0x1E, 0x59, 0x5A, 0x77, 0x78):
        mem[crate + 0x54] = kind
        pair.rebind()
        pair.set_script(1, (bits(top[0]), bits(f32(top[1] + 0.25)), bits(top[2])), 0x4000)
        r1, _ = pair.head(case, f'owner kind {kind:#x} head', up(top, 18.0), top)
        r2, _ = pair.object(case, f'owner kind {kind:#x} object', down(top, 0.2), PROBE, 2, PLAYER)
        out['owner'] += 1
        out['hits'] += bool(r1) + bool(r2)
    mem[crate + 0x54] = saved_kind
    pair.rebind()
    return out


def run_slide_class(item):
    """Beat 06: every sliding row's floor (the original 0019AB20 mask 6 from
    13.8 above the feet to 0.4 below) and the slide-entry row: the hit grid
    node's class byte in RAM (+0x1B) equals the EMCL's, and the entry row's
    is the slide class 0x10."""
    beat = item
    elf, native, emcl = G['elf'], G['native'], G['emcl']
    world = World(beat, elf)
    ee = ProbeEE(elf, world.ram, world.spad)
    spad0 = bytes(ee.spad)
    data = emcl.read_bytes()
    pool = 0x30 + 12 * u32(data, 8)
    rows = feet_rows(beat)
    entry = next(k for k in range(1, len(rows)) if rows[k][2] == 0x1C and rows[k - 1][2] != 0x1C)
    checked, slide = 0, 0
    for k, (counter, feet, p5) in enumerate(rows):
        if p5 != 0x1C and k != entry - 1:
            continue
        ee.spad[:] = spad0
        at = down(feet, 0.4)
        for j, v in enumerate(list(at) + [1.0]): ee.save(ARGS + 4 * j, bits(v))
        for j, v in enumerate(PROBE + (0.0,)): ee.save(ARGS + 0x10 + 4 * j, bits(v))
        ee.call(GROUND, (PLAYER, ARGS, ARGS + 0x10, 6))
        if ee.r[2] & 0xFFFFFFFF != 4:
            continue
        node = (ee.load(0x700031D0) - world.nodes) // 64
        cls = ee.load(world.nodes + 64 * node + 0x1B, 1)
        assert data[pool + 24 * node + 23] == cls, (beat, counter, 'EMCL class byte', node, cls)
        checked += 1
        slide += cls == SLIDE_CLASS
        if k == entry - 1:
            assert cls == SLIDE_CLASS, (beat, counter, 'the slide entry floor is not class 0x10', cls)
    assert slide, 'no sliding row stood on a class-0x10 node'
    return {'rows': checked, 'slide': slide, 'entry': rows[entry][0]}


# ---------------------------------------------------------------------------
# Main

def main():
    import time
    t0 = time.time()
    missing = [p for p in [PLAYABLE, EXPORTER] + [ROUTE / b / 'eeMemory.bin' for b in BEATS]
               + [DECOMP / 'extract/chunk15' / f for f in CHUNK15] if not p.exists()]
    if missing:
        print('SKIP: the user-local inputs are missing:', [str(p) for p in missing])
        return 0
    emcl, verified, installed = export_emcl()
    G['elf'] = read_elf()
    G['native'] = build_native()
    G['emcl'] = emcl

    row_items = []
    total_rows = 0
    for beat in BEATS:
        rows = feet_rows(beat)
        total_rows += len(rows)
        picked = reference_mode.select(rows, reference_mode.pick(len(rows), 4), seed=len(rows),
                                       axes=(lambda r: r[2],))
        row_items.append((beat, picked))
    picked_rows = sum(len(r) for _, r in row_items)
    node_count = reference_mode.pick(400, 60)
    unit_count = reference_mode.pick(1500, 90)
    synth_count = reference_mode.pick(400, 40)
    items = ([('rows', x) for x in row_items]
             + [('nodes', ('06_hill_slide', node_count, 11)), ('nodes', ('10_cage_roof_roger', node_count, 12))]
             + [('units', ('06_hill_slide', unit_count, 21)), ('units', ('08_truck_crossing', unit_count, 22))]
             + [('synth', ('05_boxes', synth_count, 31)), ('synth', ('08_truck_crossing', synth_count, 32))]
             + [('slide', '06_hill_slide'), ('bounds', '05_boxes')])
    results = reference_mode.parallel_map(run_item, items, cost=lambda it: 3 if it[0] == 'units' else 1)
    head, obj = [0, 0, 0], [0, 0, 0]
    records, surfaces, entities, captures = set(), set(), set(), {}
    nodes = {'head': [0, 0, 0], 'object': [0, 0, 0], 'rank_sentinel': 0}
    units = {'ranks': 0, 'node': [0, 0], 'prims': {}, 'walkers': {}}
    synth = {'static': 0, 'pass2': 0, 'worker_calls': 0, 'self_skip': 0, 'faults': 0, 'cell_head': 0,
             'object': [0, 0, 0], 'round': 0}
    slide = None
    cases = 0
    for (kind, arg), res in zip(items, results):
        if kind == 'rows':
            head = [a + b for a, b in zip(head, res['head'])]
            obj = [a + b for a, b in zip(obj, res['object'])]
            records |= res['records']; surfaces |= res['surfaces']; entities |= res['entities']
            captures[arg[0]] = res.get('capture')
            cases += res['cases']
        elif kind == 'nodes':
            for key in ('head', 'object'):
                nodes[key] = [a + b for a, b in zip(nodes[key], res[key])]
            nodes['rank_sentinel'] += res['rank_sentinel']
        elif kind == 'units':
            units['ranks'] += res['ranks']
            units['node'] = [a + b for a, b in zip(units['node'], res['node'])]
            for key in ('prims', 'walkers'):
                for k, v in res[key].items():
                    units[key][k] = units[key].get(k, 0) + v
        elif kind == 'synth':
            for key in synth:
                synth[key] = [a + b for a, b in zip(synth[key], res[key])] if key == 'object' else synth[key] + res[key]
        elif kind == 'bounds':
            bounds = res
        else:
            slide = res
    # Coverage the default run must reach (every path class).
    assert head[0] and head[2], ('surface record misses and grid hits on the route', head)
    assert obj[0] and obj[1] and obj[2], ('object probe: none, cell and grid results on the route', obj)
    assert ('object', 1) in records and ('object', 2) in records, records
    assert entities, 'no owner cell was hit on the route'
    assert nodes['head'][2] and nodes['object'][2] and nodes['rank_sentinel'], nodes
    assert units['node'][0] and units['node'][1], units['node']
    prim_hits = {int(k[0], 16) & 0xF000 for k in units['prims'] if k[1] == 1}
    assert prim_hits == {0x1000, 0x2000, 0x4000, 0x8000}, units['prims']
    # A hit is 1 for the surface walkers and 0 for the object walkers; the
    # surface cell walker hits only owners of kind 0x5A+ (synthetic worlds).
    hit_value = {'0x1a2ae0': 1, '0x19df10': 1, '0x1a32c0': 0, '0x19e640': 0}
    walker_hits = {w for (w, r) in units['walkers'] if r == hit_value[w]}
    assert walker_hits >= {'0x19df10', '0x1a32c0', '0x19e640'}, units['walkers']
    assert synth['static'] and synth['worker_calls'] and synth['self_skip'] and synth['cell_head'], synth
    assert synth['faults'] == 4 and all(synth['object']) and synth['round'], synth
    assert len(captures) == len(BEATS), captures
    assert bounds['hits'] and bounds['attr'] == 12 and bounds['static'] == 24 and bounds['owner'] == 7, bounds
    elapsed = time.time() - t0
    reference_mode.banner(
        reference_mode.part(picked_rows, total_rows, 'route rows (head + object each)'),
        f'{2 * node_count} surface/low-attr node points x 2 probes',
        f'{2 * unit_count} x 4 unit cases (0019F1A0, 0019ED80, prim tests, walkers)',
        f'{2 * synth_count} synthetic-world cases')
    print(f'exporter: {verified} RAM images verified byte for byte ({installed})')
    print(f'route: surface record none/cells/grid {head}, object probe {obj}, surfaces hit '
          f'{sorted(hex(x) for x in surfaces)}, owners hit {sorted(hex(x) for x in entities)}')
    byte_checked = {b: c for b, c in captures.items() if c}
    print(f'captures: +250 agrees on all {len(captures)} beat snapshots; +23A on {len(byte_checked)} '
          f'({", ".join(sorted(set(byte_checked.values())))}); not checkable on '
          f'{", ".join(sorted(b[:2] for b in captures if not captures[b])) or "none"} '
          f'(no contact, or an object-probe miss)')
    print(f'nodes: head {nodes["head"]}, object {nodes["object"]}, 0x70003244 = -1 cases {nodes["rank_sentinel"]}')
    print(f'units: {units["ranks"]} rank queries, 0019ED80 miss/hit {units["node"]}, '
          f'prims {dict(sorted(units["prims"].items()))}')
    print(f'walkers {dict(sorted(units["walkers"].items()))}')
    print(f'synthetic: {synth}')
    print(f'gate boundaries: {bounds}')
    print(f'beat 06: {slide["rows"]} sliding-row floors carry the RAM class byte in the EMCL, {slide["slide"]} '
          f'on class 0x10 nodes; slide entry at counter {slide["entry"]}')
    print(f'PASS ({elapsed:.1f}s)')
    return 0


def run_item(item):
    kind, arg = item
    return {'rows': run_beat, 'nodes': run_surface_nodes, 'units': run_units, 'synth': run_synthetic,
            'slide': run_slide_class, 'bounds': run_boundaries}[kind](arg)


if __name__ == '__main__':
    sys.exit(main())
