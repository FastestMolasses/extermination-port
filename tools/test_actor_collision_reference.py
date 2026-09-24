#!/usr/bin/env python3
"""Execute the original actor-collision publication and queries and compare
src/game/em_actor_collision.c (docs/ACTOR_COLLISION.md).

The EE interpreter of tools/test_player_slide_reference.py runs, unmodified,
over captured AREA11 RAM (the PCSX2 route beats, docs/FIRST_LEVEL_ROUTE.md):

  001A2370  hull re-transform       001B1B70 / 001B1D20 ... class-list pushes
  001AAD00  list publish (its nine hooks recorded, not simulated)
  0019AB20  vertical probe, with 0019F730 (001A44B0, 001A4650, 001A4030),
            0019C830, 0019F1A0 and 0019ED80 (the natives: em_actor_collision
            over em_coll_probe_original's prim tests and grid walk)
  0019BC40  column table, with 001A56A0, 001A5760, 001A58B0 and 0019F330,
            and the SDK 0011E748 (sqrt) / 0011DBB8 (atan) they call: nothing
            is hooked; the native column's math workers call back into the
            same original executions, and the named native workers
            (em_item_sdk_sqrt, em_director_original_0011DBB8) are compared
            against them and reported

Checks (every value compared bit for bit):
  directory  the user's disc directory (chunk15/f12_id44.bin +0x39800) equals
             every captured RAM directory outside the re-transformed hulls,
             and re-transforming the disc hulls natively with each owner's
             captured world matrix +0xD0 reproduces the captured RAM hulls
  retransform original 001A2370 against the native over the captured hulls
             with the owners' matrices and random matrices
  lists      original 001B1B70/001B1D20/001AAD00 against the native lists
  ground     original 0019AB20 against em_actor_collision_ground_0019AB20 at
             points over the truck, crates, elevator and pickup boxes (every
             result field), plus injected drum owners (0x4000 cells) and a
             static-cell directory (pass 1)
  column     original 0019BC40 against em_actor_collision_column_0019BC40
  truck      route beat 08: the frames the trace marks the player on the truck
             (+0x214 = the truck) against the frames before

`--export [PATH]` writes the directory for the runtime loader
(default assets/scene_snow/area11_cells.bin; the user's disc bytes, ignored).

No original bytes are embedded: the ELF, RAM images and disc data are the
user's own local files. The interpreter is test_coll_move_reference's
FloatEE: every COP1 and VU0 macro instruction through tools/ee_float_model.py,
the model em_ee_float.h implements. 0019AB20's grid pass (0019C830) is the
translation over the EMCL rank section and is compared exactly. KNOWN
INEXACT (docs/ACTOR_COLLISION.md) remains only for 0019BC40's pass 2, whose
native walk visits every grid node instead of the rank span: a column that
differs counts as KNOWN INEXACT (printed) only when dropping the native
entries outside the original's rank bounds gives the original's table.
"""
import ctypes as C
import random
import struct
import subprocess
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from test_player_slide_reference import read_elf, bits, number, fp, DECOMP  # noqa: E402
# COP1 and VU0 macro arithmetic through tools/ee_float_model.py (the measured
# EE model, docs/EE_FLOAT_MODEL.md), as the native side uses em_ee_float.h.
from test_coll_move_reference import FloatEE as EE  # noqa: E402
import reference_mode  # noqa: E402


ROUTE = DECOMP / 'build/s87/route'
DISC_DIRECTORY = (DECOMP / 'extract/chunk15/f12_id44.bin', 0x39800)
EMCL = ROOT / 'assets/scene_snow/snow.emcl'
OUT = ROOT / 'build/actor_collision_reference'

GROUND, VERTICAL, GRID, TABLE, RANKS = 0x19AB20, 0x19F730, 0x19C830, 0x19BC40, 0x19F1A0
RETRANSFORM, PUBLISH, PUSH4, SWAP, RESET = 0x1A2370, 0x1B1B70, 0x1B1D20, 0x1AAD00, 0x1AF8E0
SQRT, ATAN, FABS = 0x11E748, 0x11DBB8, 0x11DF78
SWAP_HOOKS = (0x1A9D20, 0x1A8DA0, 0x1A9F60, 0x1AA140, 0x1A7870, 0x1A8BE0, 0x1A9000, 0x1A97B0, 0x1A9B10)
# Code ranges executed from captured RAM; each must equal the pinned ELF.
CODE = [(0x19AB20, 0x1E0), (0x19F730, 0x718), (0x19C830, 0x32C), (0x19ED80, 0x418), (0x19F1A0, 0x190),
        (0x1A44B0, 0x1A0), (0x1A4650, 0x1E0), (0x1A4030, 0x480), (0x19BC40, 0xAB0), (0x19F330, 0x400),
        (0x1A56A0, 0xC0), (0x1A5760, 0x150), (0x1A58B0, 0x380), (0x1A2370, 0x768), (0x102738, 0x24),
        (0x1026A0, 0x2C), (0x1028B8, 0x14), (0x1028D0, 0x14), (0x103230, 0x18),
        # The SDK 0011E748 (sqrt) / 0011DBB8 (atan) the column executes, with
        # their whole static call graph (error paths included).
        (0x11E748, 0x114), (0x11CB90, 0x138), (0x11E080, 0x24), (0x128350, 0x40), (0x127728, 0x2C),
        (0x1278C0, 0x90), (0x126AB8, 0x12C), (0x126BE8, 0x9C), (0x127398, 0x114), (0x1274B0, 0x4C),
        (0x127758, 0x54), (0x1277B0, 0x10C), (0x128320, 0x2C), (0x11DB90, 0x24), (0x11FD78, 0xC),
        (0x11DBB8, 0x2A8), (0x11DF78, 0x1C)]
PLAYER = 0x8102B0
SCRATCH = 0x7F0E0000
LIST_BASE = {0: 0x28B020, 1: 0x28AFF0, 2: 0x28AF30, 3: 0x28AE30, 4: 0x28AC30, 5: 0x28AB30}
LIST_LIVE = {0: (0x275BB0, 0x275BB8), 1: (0x275BA0, 0x275BA8), 2: (0x275B90, 0x275B98),
             3: (0x275B80, 0x275B88), 4: (0x275B70, 0x275B78), 5: (0x275B60, 0x275B68)}
LIST_PUB = {0: (0x275BAC, 0x275BB4), 1: (0x275B9C, 0x275BA4), 2: (0x275B8C, 0x275B94),
            3: (0x275B7C, 0x275B84), 4: (0x275B6C, 0x275B74), 5: (0x275B5C, 0x275B64)}
LIST_CAP = {0: 0xC, 1: 0x30, 2: 0x40, 3: 0x80, 4: 0x40, 5: 0x20}

BRIDGE = r"""
#include <stdlib.h>
#include <string.h>
#include "game/em_actor_collision.h"
#include "game/em_coll_probe_original.h"
#include "game/em_director_original.h"
#include "game/em_item_sdk_math.h"

typedef struct {
    int kind, record, poly;
    uint32_t entity;
    uint16_t node;
    uint8_t class_known;
    float point[3], delta[3], normal[3], grid_start[3], grid_end[3];
} BridgeHit;

typedef struct {
    EmActorCellTable table;
    EmCollision grid;
    EmCollProbeGrid ranks;          /* the EMCL rank section (0019C830) */
    EmActorClassLists lists;
    EmActor rec[256];
    uint32_t addr[256];
    int count;
    EmActor stranger;               /* a query actor that is no owner */
    EmActorCollisionWorld world;
    uint8_t kinds[256];
    EmDirectorAtanTables atan;      /* 0011DBB8's tables, from the user's ELF */
} Bridge;

static int find(Bridge *b, uint32_t addr)
{
    for (int i = 0; i < b->count; ++i) if (b->addr[i] == addr) return i;
    return -1;
}

static uint32_t addr_of(Bridge *b, const void *p)
{
    for (int i = 0; i < b->count; ++i) if ((const void *)&b->rec[i] == p) return b->addr[i];
    return p ? 0xFFFFFFFFu : 0;
}

Bridge *bridge_new(const uint8_t *table, uint32_t size, const char *emcl)
{
    Bridge *b = calloc(1, sizeof *b);
    if (!b) return NULL;
    if (em_actor_cells_init(&b->table, table, size)) { free(b); return NULL; }
    if (emcl && (em_collision_load(&b->grid, emcl) || em_coll_probe_grid_load(&b->ranks, &b->grid, emcl))) {
        em_actor_cells_free(&b->table); free(b); return NULL;
    }
    b->world.table = &b->table;
    b->world.lists = &b->lists;
    b->world.grid = emcl ? &b->grid : NULL;
    b->world.ranks = emcl ? &b->ranks : NULL;
    b->stranger.self = &b->stranger;
    return b;
}

void bridge_static_kinds(Bridge *b, const uint8_t *kinds, unsigned count)
{
    memcpy(b->kinds, kinds, count < 256 ? count : 256);
    b->world.static_kind = count ? b->kinds : NULL;
    b->world.static_kind_count = count;
}

int bridge_actor(Bridge *b, uint32_t addr, uint8_t status, uint8_t cls, uint16_t uid,
                 uint16_t kind, uint8_t param)
{
    int i = find(b, addr);
    if (i < 0) {
        if (b->count == 256) return -1;
        i = b->count++;
        b->addr[i] = addr;
    }
    EmActor *a = &b->rec[i];
    a->status = status; a->cls = cls; a->uid = uid; a->kind = kind; a->param = param;
    a->self = a;
    return i;
}

int bridge_list(Bridge *b, int which, const uint32_t *slots, int nslots, int live, int published)
{
    EmActorClassList *l = &b->lists.list[which];
    memset(l->slot, 0, sizeof l->slot);
    for (int i = 0; i < nslots; ++i) {
        int k = find(b, slots[i]);
        if (k < 0) return -1;
        l->slot[i] = &b->rec[k];
    }
    l->live = (int16_t)live; l->published = (int16_t)published;
    return 0;
}

void bridge_list_state(Bridge *b, int which, uint32_t *slots, int *live, int *published)
{
    EmActorClassList *l = &b->lists.list[which];
    for (int i = 0; i < EM_ACTOR_LIST_MAX; ++i) slots[i] = addr_of(b, l->slot[i]);
    *live = l->live; *published = l->published;
}

int bridge_publish(Bridge *b, uint32_t addr) { int i = find(b, addr); return i < 0 ? -2 : em_actor_class_publish_001B1B70(&b->lists, &b->rec[i]); }
int bridge_push4(Bridge *b, uint32_t addr) { int i = find(b, addr); return i < 0 ? -2 : em_actor_class_push4_001B1D20(&b->lists, &b->rec[i]); }
void bridge_swap(Bridge *b) { em_actor_class_lists_swap_001AAD00(&b->lists); }
void bridge_reset(Bridge *b) { em_actor_class_lists_reset(&b->lists); }
int bridge_b58(Bridge *b) { return b->lists.count_b58; }

int bridge_retransform(Bridge *b, uint16_t uid_halfword, const float *matrix)
{
    return em_actor_cells_retransform_001A2370(&b->table, uid_halfword, matrix);
}

const uint8_t *bridge_table(Bridge *b) { return b->table.bytes; }

int bridge_ground(Bridge *b, uint32_t self_addr, uint8_t cls, const float *pos, const float *probe,
                  uint32_t mask, float *feet_y, BridgeHit *out)
{
    int i = find(b, self_addr);
    EmActorCollisionQuery q = { i >= 0 ? (const void *)&b->rec[i] : (const void *)&b->stranger,
                                cls, feet_y };
    EmActorCollisionHit hit;
    int r = em_actor_collision_ground_0019AB20(&b->world, &q, pos, probe, mask, &hit);
    if (r < 0) return r;
    out->kind = hit.kind; out->record = hit.record; out->poly = hit.poly;
    out->entity = addr_of(b, hit.entity); out->node = hit.node; out->class_known = hit.node_class_known;
    memcpy(out->point, hit.point, 12); memcpy(out->delta, hit.delta, 12); memcpy(out->normal, hit.normal, 12);
    memcpy(out->grid_start, hit.grid_start, 12); memcpy(out->grid_end, hit.grid_end, 12);
    return r;
}

int bridge_math(Bridge *b, const uint8_t *elf, uint32_t size)
{
    return em_director_original_load_atan_tables(elf, size, &b->atan);
}

/* The column comparison binds the SDK calls to the ORIGINAL 0011E748 /
 * 0011DBB8 instructions, executed by the oracle (sdk_hook), so both sides
 * see the same SDK results and every entry, aux included, is compared
 * against original instructions end to end. */
typedef float (*BridgeSdk)(int which, float x);
static BridgeSdk sdk_hook;
void bridge_sdk_hook(BridgeSdk f) { sdk_hook = f; }
static float hook_sqrt(void *c, float x) { (void)c; return sdk_hook(0, x); }
static float hook_atan(void *c, float x) { (void)c; return sdk_hook(1, x); }

/* The native workers docs/ACTOR_COLLISION.md section 7 names for the live
 * binding: 0011E748 -> em_item_sdk_sqrt (its nonnegative 0011CB90 path; the
 * argument is nx*nx + nz*nz), 0011DBB8 -> em_director_original_0011DBB8
 * (finite x). Compared separately against the same original executions. */
float bridge_named_sqrt(float x) { return em_item_sdk_sqrt(x); }
float bridge_named_atan(Bridge *b, float x)
{
    float r;
    return em_director_original_0011DBB8(&b->atan, x, &r) < 0 ? NAN : r;
}

int bridge_column(Bridge *b, const float *pos, EmCollColumn *out, uint32_t *owner_addr)
{
    if (!sdk_hook) return -2;
    EmCollColumnMath m = { hook_sqrt, hook_atan, NULL };
    int r = em_actor_collision_column_0019BC40(&b->world, pos, &m, out);
    for (int i = 0; i < out->count; ++i)
        owner_addr[i] = out->owner[i] >= 0
            ? addr_of(b, em_actor_class_list_entry(&b->lists, EM_ACTOR_LIST_CLASS4, out->owner[i])) : 0;
    return r;
}
"""


class BridgeHit(C.Structure):
    _fields_ = [('kind', C.c_int), ('record', C.c_int), ('poly', C.c_int), ('entity', C.c_uint32),
                ('node', C.c_uint16), ('class_known', C.c_uint8), ('point', C.c_float * 3),
                ('delta', C.c_float * 3), ('normal', C.c_float * 3), ('grid_start', C.c_float * 3),
                ('grid_end', C.c_float * 3)]


SDK_HOOK = C.CFUNCTYPE(C.c_float, C.c_int, C.c_float)


class Column(C.Structure):
    _fields_ = [('count', C.c_int), ('flags', C.c_uint16 * 20), ('height', C.c_float * 20),
                ('aux', C.c_float * 20), ('owner', C.c_int * 20), ('poly', C.c_int * 20),
                ('object_node', C.c_int16 * 20), ('object_kind', C.c_uint8 * 20)]


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    source = OUT / 'bridge.c'
    source.write_text(BRIDGE.replace('#include <string.h>', '#include <string.h>\n#include <math.h>'))
    lib = OUT / ('bridge.dylib' if sys.platform == 'darwin' else 'bridge.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared',
                    '-fPIC', '-Isrc', str(source), 'src/game/em_actor_collision.c', 'src/game/em_collision.c', 'src/game/em_actor_pool.c',
                    'src/game/em_coll_probe_original.c', 'src/game/em_effect_original.c',
                    'src/game/em_director_original.c', 'src/game/em_item_sdk_math.c', 'src/game/em_interaction_scan.c',
                    '-lm', '-o', str(lib)], cwd=ROOT, check=True)
    n = C.CDLL(str(lib))
    V, P, U8, U16, U32, I = C.c_void_p, C.c_char_p, C.c_uint8, C.c_uint16, C.c_uint32, C.c_int
    PF = C.POINTER(C.c_float)
    n.bridge_new.restype = V; n.bridge_new.argtypes = [P, U32, P]
    n.bridge_static_kinds.argtypes = [V, P, C.c_uint]
    n.bridge_actor.argtypes = [V, U32, U8, U8, U16, U16, U8]
    n.bridge_list.argtypes = [V, I, C.POINTER(U32), I, I, I]
    n.bridge_list_state.argtypes = [V, I, C.POINTER(U32), C.POINTER(I), C.POINTER(I)]
    n.bridge_publish.argtypes = n.bridge_push4.argtypes = [V, U32]
    n.bridge_swap.argtypes = n.bridge_reset.argtypes = n.bridge_b58.argtypes = [V]
    n.bridge_retransform.argtypes = [V, U16, PF]
    n.bridge_table.restype = C.POINTER(C.c_uint8); n.bridge_table.argtypes = [V]
    n.bridge_ground.argtypes = [V, U32, U8, PF, PF, U32, PF, C.POINTER(BridgeHit)]
    n.bridge_column.argtypes = [V, PF, C.POINTER(Column), C.POINTER(U32)]
    n.bridge_math.argtypes = [V, P, U32]
    n.bridge_named_sqrt.restype = C.c_float; n.bridge_named_sqrt.argtypes = [C.c_float]
    n.bridge_named_atan.restype = C.c_float; n.bridge_named_atan.argtypes = [V, C.c_float]
    n.bridge_sdk_hook.argtypes = [SDK_HOOK]
    return n


def fvec(values):
    return (C.c_float * len(values))(*values)


def u32(buf, at):
    return struct.unpack_from('<I', buf, at)[0]


def s16(buf, at):
    return struct.unpack_from('<h', buf, at)[0]


# ---- Directory helpers -------------------------------------------------------

def prim_size(buf, at):
    h = struct.unpack_from('<H', buf, at)[0]
    t, n = h & 0xF000, buf[at + 2]
    if t == 0x8000: return 0x24 if h & 0x800 else 0x14
    if t == 0x4000: return 0x2C if h & 0x800 else 0x18
    if t == 0x2000: return 0x1C
    if t == 0x1000: return 0x24 + 0x30 * n if h & 0x800 else 0x14 + 0x18 * n
    raise AssertionError(('unknown prim', hex(h)))


def directory_extent(buf, base=0):
    """(count, {uid: (start, end)}, size) of a directory at buf[base:]."""
    count = u32(buf, base)
    hulls, size = {}, 4 + 4 * count
    for uid in range(count):
        word = u32(buf, base + 4 + 4 * uid)
        if not word:
            continue
        at = base + (word & 0x3FFFFFFF) + 0x1C
        for _ in range(s16(buf, base + (word & 0x3FFFFFFF) + 0x18)):
            at += prim_size(buf, at)
        hulls[uid] = (word & 0x3FFFFFFF, at - base)
        size = max(size, at - base)
    return count, hulls, size


def disc_directory():
    path, base = DISC_DIRECTORY
    data = path.read_bytes()
    count, hulls, size = directory_extent(data, base)
    assert count == 27, ('AREA11 cell directory count', count)
    return data[base:base + size], hulls


class World:
    """One captured beat: RAM, scratchpad, the directory and the owners."""

    def __init__(self, beat):
        self.beat = beat
        self.ram = (ROUTE / beat / 'eeMemory.bin').read_bytes()
        self.spad = (ROUTE / beat / 'scratchpad.bin').read_bytes()
        self.table = u32(self.spad, 0x3250)
        assert s16(self.spad, 0x324C) == u32(self.ram, self.table), 'directory count word'
        _, self.hulls, self.size = directory_extent(self.ram, self.table)
        self.image = self.ram[self.table:self.table + self.size]
        pub, count = u32(self.ram, 0x275B7C), s16(self.ram, 0x275B84)
        self.owners = [u32(self.ram, pub + 4 * j) for j in range(count)]
        self.node_base = u32(self.ram, u32(self.ram, 0x28A598) + 0x20) + u32(self.ram, 0x28A598)

    def actor_fields(self, a, patch=None):
        r = bytearray(self.ram[a:a + 0x60])
        for at, v in (patch or {}).items():
            if a <= at < a + 0x60:
                r[at - a] = v
        return r[0], r[2], struct.unpack_from('<H', r, 0xE)[0], struct.unpack_from('<H', r, 0x54)[0], r[0xD]

    def matrix(self, a):
        return struct.unpack_from('<16f', self.ram, a + 0xD0)


def check_code(elf, ram, where):
    """The captured RAM runs the pinned ELF's instructions."""
    phoff = u32(elf, 28)
    kind, offset, vaddr = struct.unpack_from('<3I', elf, phoff)
    assert kind == 1 and vaddr == 0x100000, 'the boot LOAD segment'
    for start, size in CODE:
        at = start - vaddr + offset
        assert ram[start:start + size] == elf[at:at + size], (where, 'code differs from the ELF', hex(start))


def native_world(native, world, image=None, owners=None, patch=None):
    """A bridge holding the captured directory, every class-4 list slot the
    RAM holds and those owners' fields."""
    image = world.image if image is None else image
    b = native.bridge_new(image, len(image), str(EMCL).encode())
    assert b, 'bridge_new'
    r = world.ram
    live_cursor, live = u32(r, 0x275B80), s16(r, 0x275B88)
    pub_cursor, pub = u32(r, 0x275B7C), s16(r, 0x275B84)
    assert live_cursor == 0x28AE30 - 4 * live and pub_cursor == 0x28AE30 - 4 * pub, 'class-4 list cursors'
    slots = [u32(r, 0x28AE30 - 4 * (i + 1)) for i in range(max(live, pub))]
    if owners is not None:
        slots = list(reversed(owners))
        live, pub = 0, len(owners)
    for a in set(slots):
        native.bridge_actor(b, a, *world.actor_fields(a, patch))
    arr = (C.c_uint32 * max(1, len(slots)))(*slots)
    assert native.bridge_list(b, 3, arr, len(slots), live, pub) == 0
    return b, slots, live, pub


def ee_world(elf, world, image=None, owners=None, patch=None):
    """The captured world in the interpreter; `owners` replaces the class-4
    list by a published list whose entry j is owners[j] (no live pushes).
    Nothing is hooked: the SDK sqrt/atan/fabs the column table calls
    (0011E748, 0011DBB8, 0011DF78) run as original instructions."""
    ee = EE(elf, world.ram, world.spad)
    if image is not None:
        ee.write(world.table, image)
    if owners is not None:
        for k, a in enumerate(reversed(owners)):   # slot k lives at base - 4 (k + 1)
            ee.save(0x28AE30 - 4 * (k + 1), a)
        ee.save(0x275B7C, 0x28AE30 - 4 * len(owners)); ee.save(0x275B84, len(owners), 2)
        ee.save(0x275B80, 0x28AE30); ee.save(0x275B88, 0, 2)
    for at, v in (patch or {}).items():
        ee.save(at, v, 1)
    return ee


# ---- directory / retransform ------------------------------------------------

def check_directory(native, worlds):
    disc, disc_hulls = disc_directory()
    moved_total = 0
    orphans = {}
    for w in worlds:
        assert w.size == len(disc), (w.beat, 'directory size', w.size, len(disc))
        assert set(w.hulls) == set(disc_hulls) and all(w.hulls[u] == disc_hulls[u] for u in w.hulls)
        moved = {u for u, (s, e) in w.hulls.items() if w.image[s:e] != disc[s:e]}
        outside = bytearray(w.image); inside = bytearray(disc)
        for u in moved:
            s, e = w.hulls[u]
            outside[s:e] = b'\0' * (e - s); inside[s:e] = b'\0' * (e - s)
        assert bytes(outside) == bytes(inside), (w.beat, 'directory differs outside the moved hulls')
        # Every moved hull is an extended cell a live pool owner re-transforms:
        # re-transform the disc hull natively with the matrix that owner's
        # code passes and require the captured bytes. 0x825940 (AREA11 r?,
        # uid 15) passes *(D_00275B40 + 0xC) + 0x90, its bone-3 matrix
        # (runtime 0x825A54..0x825A64 and three more call sites); the truck
        # 0x823FF0, the elevator 0x827B10 and the pickups 0x219550 pass +0xD0.
        owner_of = {}
        for rec in range(0x100):
            a = 0x7A5640 + rec * 0x2F0
            if w.ram[a] and (struct.unpack_from('<H', w.ram, a + 0xE)[0] >> 8) in moved:
                owner_of.setdefault(struct.unpack_from('<H', w.ram, a + 0xE)[0] >> 8, []).append(a)
        for u in sorted(moved):
            if u not in owner_of:
                # A freed owner's hull keeps its last transform (uid 19: the
                # battery pickup, freed in beat 01): nothing may touch it.
                s, e = w.hulls[u]
                orphans.setdefault(u, set()).add(w.image[s:e])
                continue
            ok = []
            for a in owner_of[u]:
                if u32(w.ram, a + 0x10) == 0x825940:
                    matrix = struct.unpack_from('<16f', w.ram, u32(w.ram, a + 0x110 + 0xC) + 0x90)
                else:
                    matrix = w.matrix(a)
                b = native.bridge_new(disc, len(disc), None)
                native.bridge_retransform(b, struct.unpack_from('<H', w.ram, a + 0xE)[0], fvec(matrix))
                s, e = w.hulls[u]
                got = C.string_at(native.bridge_table(b), len(disc))[s:e]
                ok.append((hex(a), hex(u32(w.ram, a + 0x10)), got == w.image[s:e]))
            assert any(m for _, _, m in ok), (w.beat, 'uid', u, 'captured hull is not disc x owner matrix', ok)
        moved_total += len(moved) - len([u for u in moved if u in orphans])
    for u, images in orphans.items():
        assert len(images) == 1, ('orphaned hull changed between beats', u)
    return moved_total


def rotation(rng):
    """Rows 0..2 of a random Euler rotation (x, y, z), row-major with w 0."""
    import math
    ax, ay, az = (rng.uniform(-3.2, 3.2) for _ in range(3))
    cx, sx, cy, sy, cz, sz = math.cos(ax), math.sin(ax), math.cos(ay), math.sin(ay), math.cos(az), math.sin(az)
    return [cy * cz, cy * sz, -sy, 0.0, sx * sy * cz - cx * sz, sx * sy * sz + cx * cz, sx * cy, 0.0,
            cx * sy * cz + sx * sz, cx * sy * sz - sx * cz, cx * cy, 0.0]


def check_retransform(elf, native, worlds, rng, extra):
    cases = 0
    for w in worlds:
        ee = ee_world(elf, w)
        spad0, ram0 = bytes(ee.spad), bytes(ee.mem)
        uids = [u for u, (s, e) in w.hulls.items()]
        mats = []
        for rec in range(0x100):
            a = 0x7A5640 + rec * 0x2F0
            if w.ram[a]:
                uid_hw = struct.unpack_from('<H', w.ram, a + 0xE)[0]
                if (uid_hw >> 8) in w.hulls:
                    mats.append((a, uid_hw, w.matrix(a)))
        for _ in range(extra):
            u = rng.choice(uids)
            m = rotation(rng) + [rng.uniform(-500, 500), rng.uniform(-200, 400), rng.uniform(-500, 500), 1.0]
            m[3] = m[7] = m[11] = 0.0
            mats.append((0x7A5640, (u << 8) | rng.randrange(256), tuple(number(bits(v)) for v in m)))
        for a, uid_hw, m in mats:
            ee.mem[:] = ram0; ee.spad[:] = spad0
            ee.save(a + 0xE, uid_hw, 2)
            for i, v in enumerate(m): ee.putf(SCRATCH + 4 * i, v)
            ee.call(RETRANSFORM, (a, SCRATCH))
            want = ee.read(w.table, w.size)
            b = native.bridge_new(w.image, len(w.image), None)
            assert native.bridge_retransform(b, uid_hw, fvec(m)) == 1
            got = C.string_at(native.bridge_table(b), w.size)
            if got != want:
                diff = [i for i in range(w.size) if got[i] != want[i]]
                raise AssertionError((w.beat, '001A2370', hex(uid_hw), 'bytes differ at', [hex(d) for d in diff[:8]]))
            cases += 1
    return cases


def synthetic_directory(rng):
    """A small directory exercising every 001A2370 branch AREA11's data does
    not: extended 0x8000 and 0x4000 prims, an unknown type nibble (index
    advances, pointer stays), an extended n-gon, an empty hull and a hull
    whose first prim is not extended (no-op)."""
    r = lambda lo, hi: number(bits(rng.uniform(lo, hi)))
    def ngon(n):
        body = struct.pack('<HBB', 0x1800, n, 0) + struct.pack('<4f', *(r(-2, 2) for _ in range(4)))
        body += b''.join(struct.pack('<3f', *(r(-50, 50) for _ in range(3))) for _ in range(2 * n))
        body += struct.pack('<4f', r(-1, 1), r(-1, 1), r(-1, 1), 0.0)
        body += b''.join(struct.pack('<3f', *(r(-50, 50) for _ in range(3))) for _ in range(2 * n))
        return body
    box = struct.pack('<HBB', 0x8800, 0, 0) + struct.pack('<8f', *(r(-9, 9) for _ in range(3)), r(0, 6),
                                                         *(r(-40, 40) for _ in range(3)), 0.0)
    cyl = struct.pack('<HBB', 0x4800, 0, 0) + struct.pack('<10f', *(r(-9, 9) for _ in range(3)), r(0, 6), r(0, 9),
                                                         *(r(-40, 40) for _ in range(3)), 0.0, 0.0)
    odd = struct.pack('<HBB', 0x0800, 0, 0)
    prims = [box, cyl, ngon(rng.randint(3, 6)), box, ngon(3)]
    rng.shuffle(prims)
    if rng.random() < 0.3:
        prims.insert(rng.randrange(1, len(prims)), odd)
    hulls = [struct.pack('<6f', *(r(-1e3, 1e3) for _ in range(6))) + struct.pack('<hh', len(prims), 0) + b''.join(prims),
             struct.pack('<6f', *(r(-1, 1) for _ in range(6))) + struct.pack('<hh', 0, 0),
             struct.pack('<6f', *(r(-1, 1) for _ in range(6))) + struct.pack('<hh', 1, 0) +
             struct.pack('<HBB', 0x1000, 3, 0) + bytes(0x10 + 0x48)]
    image, offsets = bytearray(struct.pack('<I', 3) + bytes(12)), []
    for h in hulls:
        offsets.append(len(image)); image += h
    for i, o in enumerate(offsets):
        struct.pack_into('<I', image, 4 + 4 * i, o)
    return bytes(image)


def check_synthetic_retransform(elf, native, world, rng, cases):
    ee = ee_world(elf, world)
    ram0, spad0 = bytes(ee.mem), bytes(ee.spad)
    for _ in range(cases):
        image = synthetic_directory(rng)
        m = rotation(rng) + [rng.uniform(-500, 500), rng.uniform(-200, 400), rng.uniform(-500, 500), 1.0]
        m = [number(bits(v)) for v in m]
        uid = rng.choice([0, 0, 0, 1, 2, 3, 0xFF])
        ee.mem[:] = ram0; ee.spad[:] = spad0
        ee.write(world.table, image); ee.save(0x7000324C, 3, 2)
        ee.save(0x7A5640 + 0xE, (uid << 8) | 0x5A, 2)
        for i, v in enumerate(m): ee.putf(SCRATCH + 4 * i, v)
        ee.call(RETRANSFORM, (0x7A5640, SCRATCH))
        want = ee.read(world.table, len(image))
        b = native.bridge_new(image + bytes(0x400), len(image) + 0x400, None)
        assert b, 'synthetic directory'
        assert native.bridge_retransform(b, (uid << 8) | 0x5A, fvec(m)) == 1
        got = C.string_at(native.bridge_table(b), len(image))
        assert got == want, ('synthetic 001A2370', uid, [hex(i) for i in range(len(image)) if got[i] != want[i]][:8])
    return cases


# ---- class lists --------------------------------------------------------------

def list_state_ee(ee, which):
    cursor, count = LIST_LIVE[which]
    pcursor, pcount = LIST_PUB[which]
    live = ee.load(count, 2); pub = ee.load(pcount, 2)
    base = LIST_BASE[which]
    assert ee.load(cursor) == base - 4 * live, ('live cursor', which)
    assert ee.load(pcursor) == base - 4 * pub, ('published cursor', which)
    slots = [ee.load(base - 4 * (i + 1)) for i in range(LIST_CAP[which])]
    return live, pub, slots


def check_lists(elf, native, rng, steps):
    ee = EE(elf)
    for h in SWAP_HOOKS:
        ee.hooks[h] = lambda e: None
    ee.hooks[0x121A28] = lambda e: e.write(e.arg(0), bytes([e.arg(1) & 0xFF]) * e.arg(2))  # memset
    ee.call(RESET)
    b = native.bridge_new(struct.pack('<I', 0), 4, None)
    native.bridge_reset(b)
    actors = [0x7A5640 + 0x2F0 * i for i in range(0x90)]
    classes = [0, 1, 2, 3, 4, 5, 7, 0xA, 0xD, 0x1F, 0x21, 0x24, 0x42, 0x81, 0x84, 0x8D, 0xA4, 0xC7, 0xE4, 0x8A]
    for a in actors:
        c = rng.choice(classes)
        ee.save(a + 2, c, 1); ee.save(a + 0x14, a)
        native.bridge_actor(b, a, 1, c, 0, 0, 0)
    ops = 0
    for step in range(steps):
        op = rng.random()
        if op < 0.05:
            ee.call(SWAP); native.bridge_swap(b)
        else:
            a = rng.choice(actors)
            if op < 0.2:
                ee.call(PUSH4, (a,)); assert native.bridge_push4(b, a) == 1
            else:
                ee.call(PUBLISH, (a,)); assert native.bridge_publish(b, a) == 1
        ops += 1
        if step % 7 == 0 or step == steps - 1:
            for which in range(6):
                live, pub, slots = list_state_ee(ee, which)
                arr = (C.c_uint32 * 0x80)(); nl, np_ = C.c_int(), C.c_int()
                native.bridge_list_state(b, which, arr, C.byref(nl), C.byref(np_))
                n_slots = list(arr)[:LIST_CAP[which]]
                used = max(live, pub, max((i + 1 for i, v in enumerate(n_slots) if v), default=0))
                assert (live, pub) == (nl.value, np_.value), ('list counts', which, step, (live, pub), (nl.value, np_.value))
                assert slots[:used] == n_slots[:used], ('list slots', which, step)
            assert ee.load(0x275B58, 2) == native.bridge_b58(b)
    return ops


# ---- ground (0019AB20) ------------------------------------------------------

def passthrough(ee, address, before=None, after=None):
    """Run the original routine at `address` unchanged, observing its entry
    and exit (a nested call; the caller's return address is restored)."""
    def hook(e):
        ra, args = e.r[31], tuple(e.arg(i) for i in range(4))
        if before:
            before(e, args)
        del e.hooks[address]
        try:
            e.call(address, args)
        finally:
            e.hooks[address] = hook
        if after:
            after(e, args)
        e.r[31] = ra
    ee.hooks[address] = hook


def observe_grid(ee, world, first_grid, log):
    """Record each 0019C830 pass: its segment, query class, the nodes it
    hands to 0019ED80 in order, and its outcome."""
    vec = lambda at: tuple(ee.load(at + 4 * i) for i in range(3))
    def enter(e, _):
        log.append({'start': vec(0x70003190), 'end': vec(0x700031A0), 'entity': e.load(0x700031D4),
                    'cls': e.load(0x7000324E, 2) - (0x10000 if e.load(0x7000324E, 2) & 0x8000 else 0),
                    'nodes': []})
    def leave(e, _):
        log[-1]['hit'] = (e.r[2] & 0xFFFFFFFF) == 0
        log[-1]['point'] = vec(0x700031B0)
        log[-1]['node'] = e.load(0x700031D0)
    def node(e, args):
        log[-1]['nodes'].append(first_grid + (args[1] - world.node_base) // 64)
    passthrough(ee, GRID, enter, leave)
    passthrough(ee, 0x19ED80, node)


def ee_ground(ee, world, actor, pos, probe, mask, first_grid):
    for i, v in enumerate(pos): ee.putf(SCRATCH + 4 * i, v)
    for i, v in enumerate(probe): ee.putf(SCRATCH + 0x10 + 4 * i, v)
    ee.call(GROUND, (actor, SCRATCH, SCRATCH + 0x10, mask))
    kind = ee.r[2] & 0xFFFFFFFF
    rec = ee.load(0x700031D0)
    out = {'kind': kind, 'entity': ee.load(0x700031D4), 'feet': ee.load(actor + 0xB4)}
    if kind:
        out['point'] = tuple(ee.load(0x700031B0 + 4 * i) for i in range(3))
        out['delta'] = tuple(ee.load(0x700031C0 + 4 * i) for i in range(3))
        if rec == 0x700030B0:
            out['record'], out['poly'] = 1, -1
        else:
            assert (rec - world.node_base) % 64 == 0, 'grid record'
            out['record'], out['poly'] = 2, first_grid + (rec - world.node_base) // 64
        out['node'] = ee.load(rec + 0x1A, 2)
        out['normal'] = tuple(ee.load(rec + 0x24 + 4 * i) for i in range(3))
    else:
        out['record'] = 1 if rec else 0
        assert rec == 0, 'no hit leaves 0x700031D0 = 0'
    return out


def native_ground(native, b, actor, cls, pos, probe, mask, feet):
    hit = BridgeHit()
    f = C.c_float(number(feet))
    r = native.bridge_ground(b, actor, cls, fvec(pos), fvec(probe), mask, C.byref(f), C.byref(hit))
    assert r >= 0, ('native ground fault', pos, probe, hex(mask))
    out = {'kind': hit.kind, 'entity': hit.entity, 'feet': bits(f.value)}
    # The segment the native grid pass received (not a result field: kept
    # out of the comparison dict, used to classify grid-order differences).
    native_ground.entry = (tuple(bits(v) for v in hit.grid_start), tuple(bits(v) for v in hit.grid_end))
    if hit.kind:
        out['point'] = tuple(bits(v) for v in hit.point)
        out['delta'] = tuple(bits(v) for v in hit.delta)
        out['record'], out['poly'] = hit.record, hit.poly
        out['node'] = hit.node if hit.class_known else ('attr', hit.node & 0xFF)
        out['normal'] = tuple(bits(v) for v in hit.normal)
    else:
        out['record'] = hit.record
    return out


def compare_ground(want, got, emcl_class):
    w, g = dict(want), dict(got)
    if 'node' in w and isinstance(g.get('node'), tuple):
        w['node'] = ('attr', w['node'] & 0xFF) if w.get('record') == 2 and not emcl_class else w['node']
    return w == g


def ground_points(world, rng, count, uids=None):
    """Query points over every published owner hull (or the given uids)."""
    points = []
    hull_boxes = []
    for u in uids or ():
        s, _ = world.hulls[u]
        hull_boxes.append((0, struct.unpack_from('<6f', world.image, s)))
    for a in (world.owners if not uids else ()):
        uid = struct.unpack_from('<H', world.ram, a + 0xE)[0] >> 8
        if uid in world.hulls:
            s, _ = world.hulls[uid]
            hull_boxes.append((a, struct.unpack_from('<6f', world.image, s)))
    for i in range(count):
        a, box = hull_boxes[i % len(hull_boxes)] if hull_boxes else (0, (0, 0, 0, 1, 1, 1))
        x = rng.uniform(box[0] - 1.5, box[3] + 1.5)
        z = rng.uniform(box[2] - 1.5, box[5] + 1.5)
        kind = rng.random()
        if kind < 0.45:      # floor-service shape: 13.8 above down to the point
            y = rng.uniform(box[1] - 2, box[4] + 12)
            probe = (0.0, -13.8, 0.0)
        elif kind < 0.6:     # upward
            y = rng.uniform(box[1] - 12, box[4] + 2)
            probe = (0.0, rng.choice([2.0, 13.8, 18.0]), 0.0)
        else:                # crate/probe shapes
            y = rng.uniform(box[1] - 4, box[4] + 4)
            probe = (0.0, rng.choice([-3.0, -2.0, -13.8, -30.0, 5.0, 0.0, -0.0]), 0.0)
        mask = rng.choice([6, 6, 6, 2, 4, 7, 0x80000006, 0x80000002])
        querier = rng.choice([PLAYER, PLAYER, a]) if a else PLAYER
        points.append(((x, y, z), probe, mask, querier))
    return [(tuple(number(bits(v)) for v in p), tuple(number(bits(v)) for v in q), m, s) for p, q, m, s in points]


def up(v):
    return number(bits(v) + 1) if v > 0 else number(bits(v) - 1) if v < 0 else number(1)


def down(v):
    return number(bits(v) - 1) if v > 0 else number(bits(v) + 1) if v < 0 else -number(1)


def exact_circle(cx, cz, r, want=6):
    """(x, z) pairs with fp(fp(dx*dx) + fp(dz*dz)) == fp(r*r) (mula/madd as
    001A44B0/001A56A0 compute them), plus each pushed one ulp outward."""
    import math
    rng = random.Random(zlib.crc32(struct.pack('<3f', cx, cz, r)))
    r2, found = fp(r * r), []
    for _ in range(20000):
        t = rng.uniform(0, 2 * math.pi)
        x, z = number(bits(cx + r * math.cos(t))), number(bits(cz + r * math.sin(t)))
        dx, dz = fp(x - cx), fp(z - cz)
        if fp(fp(dx * dx) + fp(dz * dz)) == r2:
            found.append((x, z))
            found.append((up(x) if dx > 0 else down(x), z))
            if len(found) >= 2 * want:
                break
    assert found, ('no exact circle point', cx, cz, r)
    return found


def start_for(pos_y, target):
    """A probe.y whose 0019AB20 start height is exactly `target`, or None:
    start = (pos.y - probe.y) + (probe.y < 0 ? 0.001 : -0.001), EE rounding."""
    guess = number(bits(pos_y - target + (0.001 if target > pos_y else -0.001)))
    for _ in range(64):
        nudge = 0.001 if guess < 0 else -0.001
        start = fp(fp(pos_y - guess) + number(bits(nudge)))
        if start == target:
            return guess
        guess = down(guess) if start < target else up(guess)
    return None


def prims_of(image, hull):
    at = hull + 0x1C
    for _ in range(s16(image, hull + 0x18)):
        yield at
        at += prim_size(image, at)


def ngon_margin(image, p, x, z, pos_y, probe_y):
    """The largest inside-test dot 001A4030 computes for a vertical 0019AB20
    segment at (x, z), in the EE model (test-side case selection only), or
    None when the facing test rejects."""
    q = lambda at: struct.unpack_from('<f', image, at)[0]
    nx, ny, nz, d = q(p + 4), q(p + 8), q(p + 0xC), q(p + 0x10)
    nudge = number(bits(0.001 if probe_y < 0 else -0.001))
    qa = (x, fp(fp(pos_y - probe_y) + nudge), z)
    dy = fp(pos_y - qa[1])
    dot = lambda a, b: fp(fp(fp(a[0] * b[0]) + fp(a[1] * b[1])) + fp(a[2] * b[2]))
    along = dot((0.0, dy, 0.0), (nx, ny, nz))
    if not along <= -1e-5 or along == 0:
        return None
    t = fp(fp(d - dot((nx, ny, nz), qa)) / along)
    hy = fp(qa[1] + fp(dy * t))
    if not (min(qa[1], pos_y) <= hy <= max(qa[1], pos_y)):
        return None
    n = image[p + 2]
    worst = None
    for k in range(n):
        v = struct.unpack_from('<3f', image, p + 0x14 + 12 * k)
        e = struct.unpack_from('<3f', image, p + 0x14 + 12 * n + 12 * k)
        m = dot((fp(x - v[0]), fp(hy - v[1]), fp(z - v[2])), e)
        worst = m if worst is None else max(worst, m)
    return worst


def boundary_points(world, image, owners):
    """Exact edges of every prim test, built from the captured cells: face
    x/z bounds and plane heights (001A4650), round radii and caps
    (001A44B0), n-gon vertices, edge midpoints, plane heights and the
    facing threshold dot(dir, n) <= -1e-5 (001A4030)."""
    f = lambda v: number(bits(v))
    cases = []
    for a in owners:
        uid = struct.unpack_from('<H', world.ram, a + 0xE)[0] >> 8
        word = u32(image, 4 + 4 * uid) if uid != 0xFF else 0
        if not word or word & 0x80000000:
            continue
        for p in prims_of(image, word):
            h = struct.unpack_from('<H', image, p)[0]
            t = h & 0xF000
            q = lambda k: struct.unpack_from('<f', image, p + 4 + 4 * k)[0]
            if t == 0x2000 and image[p + 2] in (3, 4):
                ox, oy, oz, ex, ez = q(0), q(1), q(2), q(3), q(5)
                xlo, xhi = (fp(ox + ex), ox) if ex < 0 else (ox, fp(ox + ex))
                zlo, zhi = (fp(oz + ez), oz) if ez < 0 else (oz, fp(oz + ez))
                xm, zm = f((xlo + xhi) / 2), f((zlo + zhi) / 2)
                xz = [(xlo, zm), (xhi, zm), (down(xlo), zm), (up(xhi), zm),
                      (xm, zlo), (xm, zhi), (xm, down(zlo)), (xm, up(zhi))]
                top = image[p + 2] == 3
                for x, z in xz:
                    cases.append(((x, down(oy) if top else up(oy), z), (0.0, -2.0 if top else 2.0, 0.0), 2, a, ('face-xz', top)))
                # a zero probe: the 0.001 nudge alone spans the face (the
                # nudge sign is taken from probe.y < 0, so +0 and -0 go up)
                for zero in (0.0, -0.0):
                    cases.append(((xm, f(oy - 0.0005), zm), (0.0, zero, 0.0), 2, a, ('face-zero', top, zero)))
                    cases.append(((xm, f(oy + 0.0005), zm), (0.0, zero, 0.0), 2, a, ('face-zero', top, zero, 1)))
                # the plane exactly at each end of the segment
                cases.append(((xm, oy, zm), (0.0, -2.0 if top else 2.0, 0.0), 2, a, ('face-end', top)))
                pos_y = f(oy - 2.0) if top else f(oy + 2.0)
                probe = start_for(pos_y, oy)
                if probe is not None:
                    cases.append(((xm, pos_y, zm), (0.0, probe, 0.0), 2, a, ('face-start', top)))
                    cases.append(((xm, pos_y, zm), (0.0, up(probe) if top else down(probe), 0.0), 2, a, ('face-start+', top)))
            elif t in (0x4000, 0x8000):
                cx, cy, cz, r = q(0), q(1), q(2), q(3)
                half = r if h & 0x8000 else q(4)
                # Points exactly on the circle in EE arithmetic (d2 == r2),
                # found by a seeded search, and one just outside each.
                edge_x = exact_circle(cx, cz, r)
                for x, z in edge_x:
                    ytop, ybot = fp(cy + half), fp(cy - half)
                    cases.append(((x, down(ytop), z), (0.0, -3.0, 0.0), 2, a, ('round-circle', t)))
                    cases.append(((cx, ytop, cz), (0.0, -3.0, 0.0), 2, a, ('round-top', t)))
                    cases.append(((cx, ybot, cz), (0.0, 3.0, 0.0), 2, a, ('round-bottom', t)))
                    cases.append(((cx, up(ybot), cz), (0.0, 3.0, 0.0), 2, a, ('round-bottom+', t)))
            elif t == 0x1000:
                n = image[p + 2]
                nx, ny, nz, d = q(0), q(1), q(2), q(3)
                pts = [struct.unpack_from('<3f', image, p + 0x14 + 12 * k) for k in range(n)]
                cx = f(sum(v[0] for v in pts) / n); cz = f(sum(v[2] for v in pts) / n)
                spots = [(cx, cz)] + [(f(v[0]), f(v[2])) for v in pts] + \
                        [(f((pts[k][0] + pts[(k + 1) % n][0]) / 2), f((pts[k][2] + pts[(k + 1) % n][2]) / 2))
                         for k in range(n)]
                if abs(ny) < 1e-9:
                    continue
                for index, (x, z) in enumerate(spots):
                    where = 'centre' if index == 0 else 'vertex' if index <= n else 'edge'
                    y0 = f((d - nx * x - nz * z) / ny)
                    sign = -1.0 if ny > 0 else 1.0        # the facing direction of travel
                    for length in (4.0, 0.25):
                        pos_y = f(y0 + sign * 0.5 * length)
                        cases.append(((x, pos_y, z), (0.0, sign * length, 0.0), 2, a, ('ngon-span', length, where)))
                    cases.append(((x, y0, z), (0.0, sign * 3.0, 0.0), 2, a, ('ngon-plane', where)))
                    # the inside test's +1e-5 margin: ulp-nudged points
                    # near vertices and edge midpoints whose worst edge dot
                    # lands just inside (0, 1e-5] or just outside it
                    if where != 'centre':
                        pos_y = f(y0 + sign * 2.0)
                        probe_y = f(sign * 4.0)
                        bands = {}
                        for ix in range(-3, 4):
                            for iz in range(-3, 4):
                                xx, zz = x, z
                                for _ in range(abs(ix)): xx = up(xx) if ix > 0 else down(xx)
                                for _ in range(abs(iz)): zz = up(zz) if iz > 0 else down(zz)
                                m = ngon_margin(image, p, xx, zz, pos_y, probe_y)
                                if m is None:
                                    continue
                                band = 'in' if 0 < m <= 1e-5 else 'out' if 1e-5 < m <= 4e-5 else None
                                if band and band not in bands:
                                    bands[band] = (xx, zz)
                        for band, (xx, zz) in bands.items():
                            cases.append(((xx, pos_y, zz), (0.0, probe_y, 0.0), 2, a, ('ngon-margin', band)))
                    # dot(dir, n) close to -1e-5 on both sides
                    for target in (0.6e-5, 1.0e-5, 1.6e-5, 3e-5, 9e-5):
                        length = f(target / abs(ny))
                        if length >= 0.0011:
                            probe_y = f(sign * (length - 0.001))
                            cases.append(((x, f(y0 + sign * 0.5 * length), z), (0.0, probe_y, 0.0), 2, a, ('ngon-facing', target, where == 'centre')))
    return [((tuple(f(v) for v in pos), tuple(f(v) for v in probe), mask, PLAYER), tag)
            for pos, probe, mask, _, tag in cases]


GROUND_STATE = {}


def ground_case(item):
    beat, cases, variant = item
    elf, native = GROUND_STATE['elf'], GROUND_STATE['native']
    world = GROUND_STATE['worlds'][beat]
    image, owners, kinds, patch = GROUND_STATE['variants'][(beat, variant)]
    ee = ee_world(elf, world, image, owners, patch)
    b, _, _, _ = native_world(native, world, image, owners, patch)
    if kinds is not None:
        native.bridge_static_kinds(b, kinds, len(kinds))
    ram0, spad0 = bytes(ee.mem), bytes(ee.spad)
    emcl = EMCL.read_bytes()
    polys = u32(emcl, 12)
    pool = 0x30 + 12 * u32(emcl, 8)
    first_grid = [emcl[pool + 24 * i + 21] for i in range(polys)].index(4)
    emcl_class = bool(u32(emcl, 20) & 2)
    results = {'cases': 0, 'hits': {0: 0, 2: 0, 4: 0}, 'entities': set(), 'mismatch': [], 'grid': 0}
    grid_log = []
    observe_grid(ee, world, first_grid, grid_log)
    for pos, probe, mask, querier in cases:
        ee.mem[:] = ram0; ee.spad[:] = spad0
        feet = ee.load(querier + 0xB4)
        grid_log.clear()
        want = ee_ground(ee, world, querier, pos, probe, mask, first_grid)
        cls = ee.load(querier + 2, 1) & 0x1F
        self_word = ee.load(querier + 0x14)
        got = native_ground(native, b, self_word, cls, pos, probe, mask, feet)
        results['cases'] += 1
        results['hits'][want['kind']] += 1
        if want['entity']:
            results['entities'].add(want['entity'])
        if grid_log:
            # The grid arithmetic itself, over the original's visit order,
            # is compared on every case that reached 0019C830.
            assert len(grid_log) == 1
            results['grid'] += 1
            # The native grid pass starts from the original's entry state.
            if (native_ground.entry != (grid_log[0]['start'], grid_log[0]['end'])
                    or got['entity'] != grid_log[0]['entity']):
                results['mismatch'].append((beat, variant, pos, probe, hex(mask), '0019C830 entry state',
                                            grid_log[0], native_ground.entry, got['entity']))
                continue
        if not compare_ground(want, got, emcl_class):
            # 0019C830 is translated over the EMCL rank section (em_coll_probe
            # _0019C830): the grid pass is compared exactly, like the rest.
            results['mismatch'].append((beat, variant, pos, probe, hex(mask), hex(querier), want, got))
    return results


# ---- column (0019BC40) --------------------------------------------------------

def column_case(item):
    beat, points, variant = item
    elf, native = GROUND_STATE['elf'], GROUND_STATE['native']
    world = GROUND_STATE['worlds'][beat]
    image, owners, _, patch = GROUND_STATE['variants'][(beat, variant)]
    ee = ee_world(elf, world, image, owners, patch)
    b, _, _, _ = native_world(native, world, image, owners, patch)
    assert native.bridge_math(b, elf, len(elf)) == 0, '0011DBB8 tables'
    # The native column's SDK calls run the original 0011E748 / 0011DBB8 on
    # a separate interpreter over the same RAM; each distinct argument is
    # also given to the named native workers (reported, see main()).
    sdk_ee, sdk_seen = ee_world(elf, world), {}
    def sdk(which, x):
        key = (which, bits(x))
        if key not in sdk_seen:
            sdk_ee.call((SQRT, ATAN)[which], (), (x,))
            sdk_seen[key] = sdk_ee.f[0]
        return number(sdk_seen[key])
    hook = SDK_HOOK(sdk)
    native.bridge_sdk_hook(hook)
    ram0, spad0 = bytes(ee.mem), bytes(ee.spad)
    emcl = EMCL.read_bytes()
    polys = u32(emcl, 12)
    pool = 0x30 + 12 * u32(emcl, 8)
    first_grid = [emcl[pool + 24 * i + 21] for i in range(polys)].index(4)
    out = {'cases': 0, 'entries': 0, 'owner_entries': 0, 'mismatch': [], 'rank': [], 'sdk': {}}
    sx = lambda v: v - 0x10000 if v & 0x8000 else v
    for pos in points:
        ee.mem[:] = ram0; ee.spad[:] = spad0
        for i, v in enumerate(pos + (1.0,)): ee.putf(SCRATCH + 4 * i, v)
        ee.call(TABLE, (SCRATCH,))
        want = []
        for i in range(ee.load(0x700031E0)):
            obj = ee.load(0x70003130 + 4 * i)
            flags = ee.load(0x70003170 + 2 * i, 2)
            is_owner = bool(flags & 0x8000)
            want.append((flags, ee.load(0x700030F0 + 4 * i), ee.load(0x282250 + 4 * i),
                         obj if is_owner else None, ee.load(obj + 0x1A, 1) if not is_owner else None))
        col = Column(); owner_addr = (C.c_uint32 * 20)()
        assert native.bridge_column(b, fvec(pos), C.byref(col), owner_addr) >= 0
        got = []
        for i in range(col.count):
            is_owner = col.owner[i] >= 0
            got.append((col.flags[i], bits(col.height[i]), bits(col.aux[i]),
                        owner_addr[i] if is_owner else None, col.object_node[i] & 0xFF if not is_owner else None))
        out['cases'] += 1
        out['entries'] += len(want)
        out['owner_entries'] += sum(1 for e in want if e[3])
        if want != got:
            ee.mem[:] = ram0; ee.spad[:] = spad0
            ee.call(RANKS, (SCRATCH, 0x33))
            ranks = [sx(ee.load(0x70003240 + 2 * i, 2)) for i in range(6)]
            kept = []
            for i in range(col.count):
                if col.poly[i] >= 0:
                    node = world.node_base + 64 * (col.poly[i] - first_grid)
                    bounds = [sx(ee.load(node + 0xC + 2 * k, 2)) for k in range(6)]
                    if (ranks[0] < bounds[0] or bounds[1] < ranks[1] or bounds[4] > ranks[4]
                            or bounds[5] < ranks[5]):
                        continue
                kept.append(got[i])
            if kept == want:
                out['rank'].append((beat, pos))
            else:
                out['mismatch'].append((beat, variant, pos, want, got))
    for (which, a), want_bits in sdk_seen.items():
        named = (native.bridge_named_sqrt(number(a)) if which == 0
                 else native.bridge_named_atan(b, number(a)))
        out['sdk'][(which, a)] = (want_bits, bits(named))
    return out


def column_edges(world, image, owners):
    """Exact edges of the column prim tests: 001A5760 face bounds (strict),
    001A56A0 circle points, 001A58B0 vertices, edge midpoints and centroid."""
    f = lambda v: number(bits(v))
    pts = []
    for a in owners:
        uid = struct.unpack_from('<H', world.ram, a + 0xE)[0] >> 8
        word = u32(image, 4 + 4 * uid) if uid != 0xFF else 0
        if not word or word & 0x80000000:
            continue
        for p in prims_of(image, word):
            h = struct.unpack_from('<H', image, p)[0]
            q = lambda k: struct.unpack_from('<f', image, p + 4 + 4 * k)[0]
            t = h & 0xF000
            if t == 0x2000 and image[p + 2] in (3, 4):
                ox, oy, oz, ex, ez = q(0), q(1), q(2), q(3), q(5)
                xlo, xhi = (fp(ox + ex), ox) if ex < 0 else (ox, fp(ox + ex))
                zlo, zhi = (fp(oz + ez), oz) if ez < 0 else (oz, fp(oz + ez))
                xm, zm = f((xlo + xhi) / 2), f((zlo + zhi) / 2)
                for x, z in ((xlo, zm), (up(xlo), zm), (xhi, zm), (down(xhi), zm),
                             (xm, zlo), (xm, up(zlo)), (xm, zhi), (xm, down(zhi))):
                    pts.append(((x, f(oy + 1.5), z), ('face', image[p + 2], x in (xlo, xhi), z in (zlo, zhi))))
            elif t in (0x4000, 0x8000):
                for x, z in exact_circle(q(0), q(2), q(3), 3):
                    pts.append(((x, q(1), z), ('round',)))
            elif t == 0x1000:
                n = image[p + 2]
                vs = [struct.unpack_from('<3f', image, p + 0x14 + 12 * k) for k in range(n)]
                pts.append(((f(sum(v[0] for v in vs) / n), f(vs[0][1]), f(sum(v[2] for v in vs) / n)), ('ngon-centre', vs[0][1] > 0)))
                for k in range(n):
                    pts.append(((f(vs[k][0]), f(vs[k][1]), f(vs[k][2])), ('ngon-vertex',)))
                    pts.append(((f((vs[k][0] + vs[(k + 1) % n][0]) / 2), f(vs[k][1]),
                                 f((vs[k][2] + vs[(k + 1) % n][2]) / 2)), ('ngon-edge',)))
    return pts


def column_points(world, rng, count):
    pts = []
    boxes = []
    for a in world.owners:
        uid = struct.unpack_from('<H', world.ram, a + 0xE)[0] >> 8
        if uid in world.hulls:
            s, _ = world.hulls[uid]
            boxes.append(struct.unpack_from('<6f', world.image, s))
    for i in range(count):
        box = boxes[i % len(boxes)]
        pts.append((rng.uniform(box[0] - 1, box[3] + 1), rng.uniform(box[1] - 20, box[4] + 20),
                    rng.uniform(box[2] - 1, box[5] + 1)))
    return [tuple(number(bits(v)) for v in p) for p in pts]


# ---- the captured truck crossing --------------------------------------------

def truck_route(native, elf, worlds):
    """Beat 08 starts from beat 07's snapshot (truck idle at rest). The
    trace's +0x214 turns to the truck at f43; the truck arms on that tick and
    moves from f44 on, so only the frames up to f43 met the captured rest
    world. At each such row the floor probe 0019AB20(player, +0xB0 x/z,
    +0x280, 6) is run over the rest world from 13.8 above the trace's feet
    down to them (the frame's own +0xB4 before the snap is not traced);
    the original and native must agree, and the truck must be the entity
    exactly on the rows the trace marks."""
    import json
    trace = json.loads((ROUTE / '08_truck_crossing/trace.json').read_text())
    assert trace['source'] == '07_truck_preview'
    world = worlds['07_truck_preview']
    truck = 0x7A9FB0
    ee = ee_world(elf, world)
    b, _, _, _ = native_world(native, world)
    ram0, spad0 = bytes(ee.mem), bytes(ee.spad)
    emcl = EMCL.read_bytes()
    polys = u32(emcl, 12)
    pool = 0x30 + 12 * u32(emcl, 8)
    first_grid = [emcl[pool + 24 * i + 21] for i in range(polys)].index(4)
    probe = struct.unpack_from('<3f', world.ram, PLAYER + 0x280)
    rows = [r for r in trace['rows'] if 30 <= r['f'] <= 43]
    marked = []
    for r in rows:
        on = int(r['ground'], 16) == truck
        pos = (number(bits(r['hip'][0])), number(bits(r['pos'][1])), number(bits(r['hip'][2])))
        ee.mem[:] = ram0; ee.spad[:] = spad0
        want = ee_ground(ee, world, PLAYER, pos, probe, 6, first_grid)
        got = native_ground(native, b, u32(world.ram, PLAYER + 0x14), world.ram[PLAYER + 2] & 0x1F,
                            pos, probe, 6, ee.load(PLAYER + 0xB4))
        assert compare_ground(want, got, bool(u32(emcl, 20) & 2)), ('truck row', r['f'], want, got)
        marked.append((r['f'], on, want['entity'] == truck and want['kind'] == 2))
    return marked


# ---- main ------------------------------------------------------------------------

EXPORT = ROOT / 'assets/scene_snow/area11_cells.bin'


def export(path):
    """Write the user's AREA11 cell directory (disc bytes, ignored assets/)
    for em_actor_cells_load. A stand-in until a dedicated exporter exists."""
    image, hulls = disc_directory()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(image)
    print(f'AREA11 cell directory: {path} ({len(image)} bytes, {len(hulls)} hulls)')
    return 0


def main():
    if '--export' in sys.argv:
        at = sys.argv.index('--export')
        return export(Path(sys.argv[at + 1]) if len(sys.argv) > at + 1 else EXPORT)
    elf = read_elf()   # pins ELF_SHA256
    beats = ['04_elevator_ride', '05_boxes', '07_truck_preview', '08_truck_crossing']
    missing = [b for b in beats if not (ROUTE / b / 'eeMemory.bin').exists()]
    if missing or not DISC_DIRECTORY[0].exists() or not EMCL.exists():
        print('actor collision reference: SKIPPED, missing local inputs:',
              missing, DISC_DIRECTORY[0], EMCL)
        return 0
    native = build_native()
    worlds = {b: World(b) for b in beats}
    for w in worlds.values():
        check_code(elf, w.ram, w.beat)
    rng = random.Random(0xAC011)

    moved = check_directory(native, worlds.values())
    retrans = check_retransform(elf, native, worlds.values(), rng, reference_mode.pick(60, 8))
    retrans += check_synthetic_retransform(elf, native, worlds['05_boxes'], rng, reference_mode.pick(400, 60))
    list_ops = check_lists(elf, native, rng, reference_mode.pick(3000, 400))

    # Variants: the captured lists; the drums injected (0x4000 cells); a
    # directory whose crate cells are static (pass 1, with the RAM kinds).
    variants = {}
    for beat, w in worlds.items():
        variants[(beat, 'captured')] = (w.image, None, None, None)
    w5 = worlds['05_boxes']
    drums = [a for a in (0x7A99D0, 0x7A9CC0) if w5.ram[a] and (w5.ram[a + 2] & 0x1F) == 4]
    assert drums, 'drum owners in beat 05'
    variants[('05_boxes', 'drums')] = (w5.image, drums + w5.owners, None, None)
    # The same crate owners listed eleven times: 22 face candidates per
    # column, past 0019BC40's 20-candidate cap (a list may repeat a pointer).
    variants[('05_boxes', 'crowd')] = (w5.image, [0x7A7C70] * 6 + [0x7A7F60, 0x7A7980] * 3 + w5.owners, None, None)
    # The +0x54 gates of pass 2: kind 0x50 still collides, 0x51 never.
    variants[('05_boxes', 'kind50')] = (w5.image, None, None, {0x7A7C70 + 0x54: 0x50, 0x7A7980 + 0x54: 0x51})
    static = bytearray(w5.image)
    # Pass 1 walks words from uid 0 while bit 31 is set: uid 0 (a plain
    # 0x1000 n-gon cell) and uids 1..2 (0x2000 faces) become static cells.
    for uid in (0, 1, 2):
        struct.pack_into('<I', static, 4 + 4 * uid, u32(static, 4 + 4 * uid) | 0x80000000)
    area, sub = w5.ram[0x810700], w5.ram[0x810701]
    rec = u32(w5.ram, u32(w5.ram, 0x24D7C0 + 4 * area) + 4 * sub)
    kinds = bytes(w5.ram[rec + 0x28 * i + 8] for i in range(27))
    variants[('05_boxes', 'static')] = (bytes(static), None, kinds, None)
    GROUND_STATE.update(elf=elf, native=native, worlds=worlds, variants=variants)

    per_world = reference_mode.pick(700, 40)
    jobs, boundary_total, boundary_run = [], 0, 0
    for (beat, variant), (image, owners, _, _) in variants.items():
        n = per_world if variant == 'captured' else per_world // 2
        seed = zlib.crc32(f'{beat}/{variant}'.encode())
        pts = ground_points(worlds[beat], random.Random(seed), n, (0, 1, 2) if variant == 'static' else None)
        if variant == 'drums':
            pts = [((number(bits(rng.uniform(288, 304))), number(bits(rng.uniform(245, 266))),
                     number(bits(rng.uniform(322, 332)))), q, m, s) for _, q, m, s in pts]
        if variant == 'captured' and beat == '07_truck_preview':
            # The grid pass (0019C830 / 0019ED80) level-wide: floor probes at
            # random AREA11 points, down from well above the ground.
            grng = random.Random(0x19C830)
            for _ in range(reference_mode.pick(600, 60)):
                gx, gz = grng.uniform(100, 520), grng.uniform(80, 520)
                pts.append(((number(bits(gx)), number(bits(grng.uniform(170, 360))), number(bits(gz))),
                            (0.0, number(bits(grng.choice([-13.8, -60.0, -200.0, 13.8]))), 0.0),
                            grng.choice([4, 6, 0x80000004]), PLAYER))
        edge_owners = owners if owners is not None else worlds[beat].owners
        if variant == 'static':
            edge_owners = []
        edges = boundary_points(worlds[beat], image, edge_owners)
        boundary_total += len(edges)
        # Quick mode: a fixed-seed sample of the edges that still covers every
        # prim type, owner and mask (the full sweep runs them all).
        edges = reference_mode.select(edges, reference_mode.pick(len(edges), 220), seed,
                                      axes=(lambda e: e[1],))
        boundary_run += len(edges)
        pts = pts + [case for case, _ in edges]
        chunk = max(1, len(pts) // 4)
        for i in range(0, len(pts), chunk):
            jobs.append((beat, pts[i:i + chunk], variant))
    ground = reference_mode.parallel_map(ground_case, jobs, cost=lambda j: len(j[1]))
    g_cases = sum(r['cases'] for r in ground)
    g_mismatch = [m for r in ground for m in r['mismatch']]
    g_grid = sum(r['grid'] for r in ground)
    hits = {k: sum(r['hits'][k] for r in ground) for k in (0, 2, 4)}
    entities = set().union(*(r['entities'] for r in ground))

    col_jobs, col_edge_total, col_edge_run = [], 0, 0
    for (beat, variant), (image, owners, _, _) in variants.items():
        if variant == 'static':
            continue
        seed = zlib.crc32(f'c/{beat}/{variant}'.encode())
        pts = column_points(worlds[beat], random.Random(seed), reference_mode.pick(400, 24))
        if variant == 'drums':
            pts = [(number(bits(rng.uniform(288, 304))), p[1], number(bits(rng.uniform(322, 332)))) for p in pts]
        edges = column_edges(worlds[beat], image, owners if owners is not None else worlds[beat].owners)
        col_edge_total += len(edges)
        edges = reference_mode.select(edges, reference_mode.pick(len(edges), 60), seed, axes=(lambda e: e[1],))
        col_edge_run += len(edges)
        pts = pts + [point for point, _ in edges]
        chunk = max(1, len(pts) // 4)
        for i in range(0, len(pts), chunk):
            col_jobs.append((beat, pts[i:i + chunk], variant))
    column = reference_mode.parallel_map(column_case, col_jobs, cost=lambda j: len(j[1]))
    c_cases = sum(r['cases'] for r in column)
    c_entries = sum(r['entries'] for r in column)
    c_owner = sum(r['owner_entries'] for r in column)
    c_mismatch = [m for r in column for m in r['mismatch']]
    c_rank = [m for r in column for m in r['rank']]
    sdk = {}
    for r in column:
        sdk.update(r['sdk'])
    sdk_differ = {k: v for k, v in sdk.items() if v[0] != v[1]}

    marked = truck_route(native, elf, worlds)

    reference_mode.banner(f'{moved} captured hulls reproduced from disc x owner +0xD0',
                          f'{retrans} 001A2370 cases', f'{list_ops} list operations',
                          f'{g_cases} 0019AB20 cases ({boundary_run} of {boundary_total} prim-edge cases; '
                          f'none {hits[0]}, cell {hits[2]}, grid {hits[4]}; {g_grid} grid passes from the '
                          f'original entry state; '
                          f'{len(entities)} owners hit)',
                          f'{c_cases} 0019BC40 columns ({col_edge_run} of {col_edge_total} prim-edge columns; '
                          f'{c_entries} entries, {c_owner} owner entries)',
                          f'{len(marked)} truck route rows')
    for which, name in ((0, '0011E748 -> em_item_sdk_sqrt'), (1, '0011DBB8 -> em_director_original_0011DBB8')):
        total = sum(1 for k in sdk if k[0] == which)
        differ = sorted((hex(a), hex(o), hex(n)) for (w, a), (o, n) in sdk_differ.items() if w == which)
        # Reported, not asserted: the column above ran on the original SDK
        # instructions; this is the separate question of which native
        # worker the binder should use (docs/ACTOR_COLLISION.md section 4).
        print(f'  named SDK worker {name}: {total - len(differ)} of {total} distinct arguments '
              f'equal the original under this oracle' + (f'; differ (arg, original, native): {differ[:4]}'
                                                        if differ else ''))
    for m in c_rank:
        print('  grid visit order / rank span (KNOWN INEXACT, docs/ACTOR_COLLISION.md):', m)
    for m in g_mismatch[:6]:
        print('  0019AB20 MISMATCH', m)
    for m in c_mismatch[:6]:
        print('  0019BC40 MISMATCH', m)
    route_bad = [(f, on, found) for f, on, found in marked if on != found]
    for m in route_bad:
        print('  truck route row disagrees with the trace (+0x214):', m)
    ok = not g_mismatch and not c_mismatch and not route_bad
    assert hits[2] and hits[4] and hits[0], 'every result class must occur'
    print('actor collision reference:', 'PASS' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
