#!/usr/bin/env python3
"""AREA11 static roster: execute the original spawners, then census the captures.

WP-3 step S7 (SCENE_COORDINATOR_DESIGN.md sections 4.2-4.4 and the S7 row).

A. Original execution. From the user's pinned ELF (never embedded here) the
   oracle executes 001AF8E0 reset, then 001B6990 (with 001B6910, 001B65C0,
   001B64F0, 001B6660, 001B11E0, 001AFA90 and 001AFA50 as the original calls
   them) and 001C5C50. The only boundary is func_00121A28 (memset), the
   argument-checked stub of test_actor_pool_reference.py. The native side runs
   em_actor_pool_reset_001AF8E0, em_actor_roster_spawn_001B6990 and
   em_actor_roster_spawn_001C5C50 over the exported roster. Compared: all 256
   original-layout records (the +0x2E halfword against the spawn log, because
   EmActor has no +0x2E field yet), the pool globals, the progress bytes
   D_00810758..D_00810B5F after the call, and the list order.
   - AREA11: the real tables (ELF + AREA11 overlay), with the progress bytes
     and D_00810702 of each capture.
   - Synthetic: random tables at synthetic addresses behind D_0024D820 and
     D_0024D7C0 for random areas/subs, every condition id, class-2 records,
     the first-visit prime pass (D_00810788 == 0xFF), 0x0B skips, and pools
     prefilled/freed through the S4-verified 001AFA90/001AFC10 so allocations
     fail mid-table (record skipped, index still advances) and hit the class
     0xC reserve.
B. Census (read-only). Walks D_00275BC0 in the opening, handoff and playable
   captures (and any other capture present) and prints only addresses and
   counts. The native static spawn minus record 13 must equal the captured
   prefix #0-28 exactly (node address, callback, class, table index, uid);
   record 13 must be spawned and flagged (EM_ROSTER_FLAG_SELF_FREEING) and
   absent from every captured prefix; the captured 001C5930 node must match
   the native 001C5C50 node; and every callback after the prefix must have a
   registry entry (a binding or a named UNBOUND row) whose origin is runtime.
C. Fail-stop: an area/sub mismatch, a failing bind, a progress byte outside
   the view and a walk over unbound nodes all fault; malformed files are
   rejected.

Oracle ISA extensions (sllv, srav, srlv, movz, movn, sltu, xor, nor, xori, sltiu,
lb, lhu) are validated on synthetic words before any original code runs, and
the S4-verified 001AFA90/001AFC10 run through the extended oracle in every
prefill.
"""
import ctypes as C
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import sys

from test_point_light_reference import signed, bits
from test_actor_pool_reference import (Original as PoolOriginal, POOL, SIZE, COUNT, HEAD,
                                       RESET, ALLOC, FREE_FN, WALK, ELF_SHA256)
from export_area11_roster import (build_area11_roster, walk_roster, encode_roster, elf_overlay_reader,
                                  OVERLAY_ARENA, OVERLAY_SIZE, D_0024D820, D_0024D7C0)

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent/'Extermination'
SPAWN, TITLE = 0x1B6990, 0x1C5C50
PROGRESS_BASE, PROGRESS_END = 0x810758, 0x810B60
D_700, D_701, D_702 = 0x810700, 0x810701, 0x810702
FAULT_NULL_WORKER, FAULT_WORKER_FAILED, FAULT_BAD_INDEX = 1, 2, 4
RECORD13_CALLBACK, RECORD13_SLOT = 0x8257A0, 0x7A96E0
PREFIX = 29  # captured nodes #0-28 (ORIGINAL_FRAME_ORDER.md section 4)
REQUIRED = ['opening_ee.bin', 'handoff_ee.bin', 'playable_ee.bin']
EXTRA = ['status-hub/eeMemory.bin', 'roger-encounter/eeMemory.bin', 'panel/eeMemory.bin',
         'panel/animation_ee.bin', 'elevator/clip47_ee.bin', 'elevator/completed_ee.bin']
SYN_BASE = 0x00900000       # synthetic tables, outside the ELF, overlay and pool
DEFERRED_SIZE, PLACEMENT_SIZE = 0x2C, 0x28


class RosterOracle(PoolOriginal):
    """S4 pool oracle (arena fast path, memset stub, nested jal) + overlay
    memory + the ISA extensions the spawners need."""

    def __init__(self, elf, overlay=None):
        self.overlay = bytearray(overlay) if overlay is not None else None
        super().__init__(elf)

    def save(self, address, value, size=4):
        if self.overlay is not None and OVERLAY_ARENA <= address and address+size <= OVERLAY_ARENA+OVERLAY_SIZE:
            o = address-OVERLAY_ARENA
            self.overlay[o:o+size] = (value & ((1 << (8*size))-1)).to_bytes(size, 'little')
        else: super().save(address, value, size)

    def load(self, address, size=4):
        if self.overlay is not None and OVERLAY_ARENA <= address and address+size <= OVERLAY_ARENA+OVERLAY_SIZE:
            o = address-OVERLAY_ARENA
            return int.from_bytes(self.overlay[o:o+size], 'little')
        return super().load(address, size)

    def plain(self, word):
        r = self.r
        op, rs, rt, rd = word >> 26, word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        fn, imm = word & 63, signed(word & 65535, 16)
        if op == 0 and fn in (4, 6, 7, 10, 11, 38, 39, 43):
            s = r[rs] & 31
            if fn == 4: r[rd] = (r[rt] << s) & 0xFFFFFFFF                 # sllv
            elif fn == 6: r[rd] = (r[rt] & 0xFFFFFFFF) >> s               # srlv
            elif fn == 7: r[rd] = (signed(r[rt]) >> s) & 0xFFFFFFFF       # srav
            elif fn == 10:                                                 # movz
                if r[rt] == 0: r[rd] = r[rs]
            elif fn == 11:                                                 # movn
                if r[rt] != 0: r[rd] = r[rs]
            elif fn == 38: r[rd] = r[rs] ^ r[rt]                          # xor
            elif fn == 39: r[rd] = ~(r[rs] | r[rt]) & 0xFFFFFFFF          # nor
            elif fn == 43: r[rd] = int((r[rs] & 0xFFFFFFFF) < (r[rt] & 0xFFFFFFFF))  # sltu
        elif op == 11: r[rt] = int((r[rs] & 0xFFFFFFFF) < (imm & 0xFFFFFFFF))     # sltiu
        elif op == 14: r[rt] = r[rs] ^ (word & 65535)                              # xori
        elif op == 32: r[rt] = signed(self.load((r[rs]+imm) & 0xFFFFFFFF, 1), 8) & 0xFFFFFFFF  # lb
        elif op == 37: r[rt] = self.load((r[rs]+imm) & 0xFFFFFFFF, 2)             # lhu
        else:
            super().plain(word); return
        r[0] = 0

    def read(self, address, size): return bytes(self.load(address+i, 1) for i in range(size))
    def byte(self, address, value): self.save(address, value, 1)
    def progress(self): return bytes(self.load(a, 1) for a in range(PROGRESS_BASE, PROGRESS_END))
    def set_progress(self, data):
        for i, value in enumerate(data): self.save(PROGRESS_BASE+i, value, 1)


def validate_extensions(elf):
    """Each extension against its MIPS definition on synthetic operands."""
    o = RosterOracle(elf); rng = random.Random(0x5EED); checks = 0
    special = lambda fn, rs, rt, rd: (rs << 21) | (rt << 16) | (rd << 11) | fn
    for _ in range(400):
        a, b = rng.getrandbits(32), rng.getrandbits(32)
        b = rng.choice([b, 0, 31, 32, 33, 0xFFFFFFFF])
        for fn, want in ((4, (b << (a & 31)) & 0xFFFFFFFF), (6, b >> (a & 31)),
                         (7, (signed(b) >> (a & 31)) & 0xFFFFFFFF), (38, a ^ b), (39, ~(a | b) & 0xFFFFFFFF),
                         (43, int(a < b))):
            o.r[4], o.r[5], o.r[2] = a, b, 0xDEAD
            o.plain(special(fn, 4, 5, 2)); assert o.r[2] == want, ('ext', fn, hex(a), hex(b)); checks += 1
        for fn, cond in ((10, b == 0), (11, b != 0)):
            o.r[4], o.r[5], o.r[2] = a, b, 0x1234
            o.plain(special(fn, 4, 5, 2)); assert o.r[2] == (a if cond else 0x1234), ('mov', fn); checks += 1
        imm = rng.getrandbits(16)
        o.r[4] = a; o.plain((11 << 26) | (4 << 21) | (2 << 16) | imm)
        assert o.r[2] == int(a < (signed(imm, 16) & 0xFFFFFFFF)), 'sltiu'; checks += 1
        o.plain((14 << 26) | (4 << 21) | (2 << 16) | imm); assert o.r[2] == a ^ imm, 'xori'; checks += 1
        address = SYN_BASE + 0x100 + rng.randrange(64)*2
        o.save(address, b & 0xFFFF, 2); o.r[4] = address - 4
        o.plain((37 << 26) | (4 << 21) | (2 << 16) | 4); assert o.r[2] == b & 0xFFFF, 'lhu'; checks += 1
        o.plain((32 << 26) | (4 << 21) | (2 << 16) | 4)
        assert o.r[2] == signed(b & 0xFF, 8) & 0xFFFFFFFF, 'lb'; checks += 1
        o.plain((33 << 26) | (4 << 21) | (2 << 16) | 4)  # lh (base) stays sign-extending
        assert o.r[2] == signed(b & 0xFFFF, 16) & 0xFFFFFFFF, 'lh'; checks += 1
    o.r[0] = 5; o.plain(special(4, 4, 5, 0)); assert o.r[0] == 0
    return checks


SHIM = r'''
#include "game/em_actor_roster.h"
#include <stdlib.h>
#include <string.h>
void *shim_new(size_t n) { return calloc(1, n); }
size_t shim_sizes(int which)
{
    switch (which) {
    case 0: return sizeof(EmActorPool);
    case 1: return sizeof(EmSceneState);
    case 2: return sizeof(EmActorRosterProgress);
    case 3: return sizeof(EmActorRosterSpawnLog);
    default: return sizeof(EmActorRoster);
    }
}
void shim_release(void *p) { free(p); }
EmActor *shim_record(EmActorPool *p, int i) { return &p->records[i]; }
void shim_scene_set(EmSceneState *s, uint8_t area, uint8_t sub, uint8_t d702)
{ s->d810700 = area; s->d810701 = sub; s->d810702 = d702; }
int32_t shim_fault_code(const EmSceneState *s) { return s->fault.code; }
uint32_t shim_fault_address(const EmSceneState *s) { return s->fault.address; }
void shim_fault_clear(EmSceneState *s) { s->fault.code = 0; s->fault.address = 0; }
uint8_t *shim_progress_bytes(EmActorRosterProgress *p) { return p->bytes; }
uint32_t shim_log_count(const EmActorRosterSpawnLog *l) { return l->count; }
void shim_log_counters(const EmActorRosterSpawnLog *l, uint32_t out[3])
{ out[0] = l->skipped_condition; out[1] = l->skipped_alloc; out[2] = l->skipped_class_0b; }
void shim_log_entry(const EmActorPool *pool, const EmActorRosterSpawnLog *l, uint32_t i, uint32_t out[10])
{
    const EmActorRosterSpawned *e = &l->entries[i];
    out[0] = em_actor_pool_address(pool, e->actor);
    out[1] = e->record_address; out[2] = e->source; out[3] = e->group; out[4] = e->index;
    out[5] = e->flags2; out[6] = e->wrote_flags2; out[7] = e->flags;
    out[8] = e->actor ? e->actor->source_id : 0;
    out[9] = e->actor ? (e->actor->behavior != NULL) : 0;
}
size_t shim_registry(uint32_t *callbacks, uint32_t *origins, uint32_t *bound, size_t max)
{
    size_t n = 0;
    const EmActorRosterCallback *rows = em_actor_roster_area11_callbacks(&n);
    for (size_t i = 0; i < n && i < max; ++i) {
        callbacks[i] = rows[i].callback; origins[i] = rows[i].origin;
        bound[i] = rows[i].binding != NULL;
    }
    return n;
}
const char *shim_registry_name(uint32_t callback)
{
    const EmActorRosterCallback *row = em_actor_roster_area11_callback(callback);
    return row ? row->name : NULL;
}
'''

vp, u32 = C.c_void_p, C.c_uint32
Bind = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_void_p, C.c_void_p)


class Globals(C.Structure):
    _fields_ = [('head', C.c_uint32), ('tail', C.c_uint32), ('free_head', C.c_uint32),
                ('free_count', C.c_int16), ('current', C.c_uint32)]


def load_native():
    out = ROOT/'build/actor_census_reference'; out.mkdir(parents=True, exist_ok=True)
    (out/'shim.c').write_text(SHIM)
    library = out/('roster.dylib' if sys.platform == 'darwin' else 'roster.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC',
                    '-dynamiclib' if sys.platform == 'darwin' else '-shared', '-Isrc',
                    'src/game/em_actor_pool.c', 'src/game/em_actor_roster.c', str(out/'shim.c'),
                    '-o', str(library)], cwd=ROOT, check=True)
    lib = C.CDLL(str(library))
    for name, res, args in [
            ('shim_new', vp, [C.c_size_t]), ('shim_sizes', C.c_size_t, [C.c_int]), ('shim_release', None, [vp]),
            ('shim_record', vp, [vp, C.c_int]), ('shim_scene_set', None, [vp, C.c_uint8, C.c_uint8, C.c_uint8]),
            ('shim_fault_code', C.c_int32, [vp]), ('shim_fault_address', u32, [vp]),
            ('shim_fault_clear', None, [vp]), ('shim_progress_bytes', C.POINTER(C.c_uint8), [vp]),
            ('shim_log_count', u32, [vp]), ('shim_log_counters', None, [vp, C.POINTER(u32)]),
            ('shim_log_entry', None, [vp, vp, u32, C.POINTER(u32)]),
            ('shim_registry', C.c_size_t, [C.POINTER(u32), C.POINTER(u32), C.POINTER(u32), C.c_size_t]),
            ('shim_registry_name', C.c_char_p, [u32]),
            ('em_actor_roster_parse', C.c_int, [vp, C.c_char_p, C.c_size_t]),
            ('em_actor_roster_load', C.c_int, [vp, C.c_char_p]),
            ('em_actor_roster_free', None, [vp]),
            ('em_actor_roster_spawn_001B6990', C.c_int, [vp, vp, vp, vp, Bind, vp, vp]),
            ('em_actor_roster_spawn_001C5C50', C.c_int, [vp, vp, Bind, vp, vp]),
            ('em_actor_pool_reset_001AF8E0', None, [vp]),
            ('em_actor_pool_alloc_001AFA90', vp, [vp, vp, C.c_uint8]),
            ('em_actor_pool_free_001AFC10', C.c_int, [vp, vp, vp]),
            ('em_actor_pool_walk_001AFD70', C.c_int, [vp, vp, C.c_int, vp, vp, vp]),
            ('em_actor_pool_address', u32, [vp, vp]),
            ('em_actor_pool_globals', None, [vp, C.POINTER(Globals)]),
            ('em_actor_pool_record_image', None, [vp, vp, C.c_char_p])]:
        fn = getattr(lib, name); fn.restype = res; fn.argtypes = args
    return lib


NO_BIND = C.cast(None, Bind)


class Native:
    def __init__(self, lib):
        self.lib = lib
        self.pool = lib.shim_new(lib.shim_sizes(0)); self.scene = lib.shim_new(lib.shim_sizes(1))
        self.progress_ptr = lib.shim_new(lib.shim_sizes(2)); self.log = lib.shim_new(lib.shim_sizes(3))
        self.roster = lib.shim_new(lib.shim_sizes(4)); self.parsed = False

    def close(self):
        if self.parsed: self.lib.em_actor_roster_free(self.roster)
        for p in (self.pool, self.scene, self.progress_ptr, self.log, self.roster): self.lib.shim_release(p)

    def parse(self, data):
        if self.parsed: self.lib.em_actor_roster_free(self.roster)
        result = self.lib.em_actor_roster_parse(self.roster, data, len(data)); self.parsed = result == 0
        return result

    def ptr(self, address):
        assert (address-POOL) % SIZE == 0 and 0 <= address-POOL < COUNT*SIZE, hex(address)
        return self.lib.shim_record(self.pool, (address-POOL)//SIZE)

    def addr(self, pointer): return self.lib.em_actor_pool_address(self.pool, pointer)
    def set_scene(self, area, sub, d702): self.lib.shim_scene_set(self.scene, area, sub, d702)
    def reset(self): self.lib.em_actor_pool_reset_001AF8E0(self.pool)
    def alloc(self, cls): return self.addr(self.lib.em_actor_pool_alloc_001AFA90(self.pool, self.scene, cls))
    def free(self, node): assert self.lib.em_actor_pool_free_001AFC10(self.pool, self.scene, self.ptr(node)) == 0
    def set_progress(self, data):
        p = self.lib.shim_progress_bytes(self.progress_ptr)
        for i, value in enumerate(data): p[i] = value
    def progress(self):
        p = self.lib.shim_progress_bytes(self.progress_ptr)
        return bytes(p[i] for i in range(PROGRESS_END-PROGRESS_BASE))
    def spawn(self, bind=NO_BIND):
        return self.lib.em_actor_roster_spawn_001B6990(self.roster, self.pool, self.scene, self.progress_ptr,
                                                       bind, None, self.log)
    def spawn_title(self, bind=NO_BIND, log=True):
        return self.lib.em_actor_roster_spawn_001C5C50(self.pool, self.scene, bind, None,
                                                       self.log if log else None)
    def fault(self): return self.lib.shim_fault_code(self.scene), self.lib.shim_fault_address(self.scene)
    def globals(self):
        g = Globals(); self.lib.em_actor_pool_globals(self.pool, C.byref(g))
        return (g.head, g.tail, g.free_head, g.free_count, g.current)
    def entries(self):
        out, buf = [], (u32*10)()
        for i in range(self.lib.shim_log_count(self.log)):
            self.lib.shim_log_entry(self.pool, self.log, i, buf)
            out.append(dict(node=buf[0], record=buf[1], source=buf[2], group=buf[3], index=buf[4],
                            flags2=buf[5], wrote_flags2=buf[6], flags=buf[7], source_id=buf[8], bound=buf[9]))
        return out
    def counters(self):
        buf = (u32*3)(); self.lib.shim_log_counters(self.log, buf); return tuple(buf)
    def image(self):
        out, buffer = bytearray(), C.create_string_buffer(SIZE)
        for i in range(COUNT):
            self.lib.em_actor_pool_record_image(self.pool, self.lib.shim_record(self.pool, i), buffer)
            out += buffer.raw
        return out
    def order(self):
        nodes, node, image = [], self.globals()[0], self.image()
        while node:
            nodes.append(node); o = node-POOL
            node = int.from_bytes(image[o+0x1C:o+0x20], 'little')
            assert len(nodes) <= COUNT
        return nodes


def oracle_order(o):
    nodes, node = [], o.load(HEAD)
    while node:
        nodes.append(node); node = o.load(node+0x1C); assert len(nodes) <= COUNT
    return nodes


def compare(o, n, entries, where):
    """Whole arena, pool globals, progress bytes and list order."""
    expected = n.image()
    for e in entries:  # +0x2E is not an EmActor field yet: the log carries it
        if e['wrote_flags2']:
            at = e['node']-POOL+0x2E
            expected[at:at+2] = e['flags2'].to_bytes(2, 'little')
    actual = bytes(o.arena)
    if actual != bytes(expected):
        i = next(i for i, (x, y) in enumerate(zip(actual, expected)) if x != y)
        raise AssertionError((where, 'record', i//SIZE, 'offset', hex(i % SIZE), actual[i], expected[i]))
    og = (o.load(HEAD), o.load(0x275BBC), o.load(0x275BC4), signed(o.load(0x275BC8, 2), 16), o.load(0x275B44))
    assert og == n.globals(), (where, og, n.globals())
    assert o.progress() == n.progress(), (where, 'progress')
    assert oracle_order(o) == n.order(), (where, 'order')
    for e in entries:
        assert e['source_id'] == e['record'], (where, 'source_id', e)
        assert not e['bound'], (where, 'bound without a binder', e)


def u32_at(ram, address): return struct.unpack_from('<I', ram, address)[0]


def run_area11(elf, overlay, lib, roster_bytes, ram, stats):
    """Original vs native state-0 spawn with one capture's progress bytes."""
    o, n = RosterOracle(elf, overlay), Native(lib)
    try:
        assert n.parse(roster_bytes) == 0
        progress = ram[PROGRESS_BASE:PROGRESS_END]
        area, sub, d702 = ram[D_700], ram[D_701], ram[D_702]
        assert (area, sub) == (0x0B, 0), (area, sub)
        o.byte(D_700, area); o.byte(D_701, sub); o.byte(D_702, d702); o.set_progress(progress)
        n.set_scene(area, sub, d702); n.set_progress(progress)
        o.call(RESET); n.reset()
        o.call(SPAWN)
        assert n.spawn() == 0, n.fault()
        static_entries = n.entries()
        compare(o, n, static_entries, 'area11 001B6990')
        o.call(TITLE)
        assert n.spawn_title(log=False) == 0
        compare(o, n, static_entries, 'area11 001C5C50')
        order = n.order()
        title = order[-1]
        stats['area11_spawn_cases'] += 1
        return static_entries, n.counters(), title, n.image()
    finally:
        n.close()


# ---------------------------------------------------------------- synthetic

COND_IDS = [0, 1, 1, 2, 3, 4, 4, 5, 6, 6, 7, 9, -2, -300, 0x7FFF]
DEFERRED_CLASSES = [2, 0x22, 0x82, 0xE2, 0x102, 0xFF02, 4, 0x84, 0x87, 0x0C, 0x8C, 1, 0x0B, 0x6B, 9, 0x1F]
PLACEMENT_CLASSES = [0x0B, 0x10B, 0x8B, 2, 0x22, 0x82, 0x102, 0x1FF, 0xAA, 0x0C, 0x2C, 4, 0x84, 1, 8, 0x0D]
STATES = [0, 1, 2, 4, 5, 7, 3]


def finite_float(rng):
    return bits(rng.choice([0.0, -0.0, 1.0, rng.uniform(-5000, 5000), rng.uniform(-1, 1)]))


def synthetic_record(rng, deferred):
    b = bytearray(DEFERRED_SIZE if deferred else PLACEMENT_SIZE)
    if deferred:
        cid = rng.choice(COND_IDS)
        struct.pack_into('<h', b, 0, cid)
        struct.pack_into('<H', b, 2, rng.choice([rng.getrandbits(16), rng.randrange(256), rng.randrange(256) << 8,
                                               rng.randrange(0x80, 0x100) << 8 | rng.randrange(256)]))
        struct.pack_into('<H', b, 4, rng.choice(DEFERRED_CLASSES))
        struct.pack_into('<H', b, 6, rng.choice(STATES) | rng.choice([0, 0, 0x100, 0x7F00, 0xFF00]))
        struct.pack_into('<H', b, 8, rng.getrandbits(16) if rng.random() < .5 else rng.choice([0x40, 0xC0, 0x80, 0]))
        for off in (0xA, 0xC, 0xE): struct.pack_into('<H', b, off, rng.getrandbits(16))
        for off in range(0x10, 0x28, 4): struct.pack_into('<I', b, off, finite_float(rng))
        struct.pack_into('<I', b, 0x28, rng.getrandbits(32))
        if rng.random() < .2:  # a record the 001B64F0 sweep acts on (or skips for key byte 0)
            b[2] = rng.choice([0, 0, 1, 31, 32, 0xFF, rng.randrange(256)])
            struct.pack_into('<H', b, 4, rng.choice([2, 0x22, 0x82, 0xE2]))
            struct.pack_into('<H', b, 6, rng.choice([1, 4, 5, 7]))
            struct.pack_into('<H', b, 8, rng.getrandbits(16) | 0x40)
    else:
        struct.pack_into('<H', b, 0, rng.choice(PLACEMENT_CLASSES))
        for off in (2, 4, 6, 8, 0xA): struct.pack_into('<H', b, off, rng.getrandbits(16))
        for off in range(0xC, 0x24, 4): struct.pack_into('<I', b, off, finite_float(rng))
        struct.pack_into('<I', b, 0x24, rng.getrandbits(32))
    return bytes(b)


def write_synthetic_tables(o, rng, area, sub):
    """Tables behind D_0024D820[area] / D_0024D7C0[area] in oracle memory."""
    reg, lst, tab, place = SYN_BASE, SYN_BASE+0x100, SYN_BASE+0x200, SYN_BASE+0x8000
    o.save(D_0024D820+4*area, 0 if rng.random() < .1 else reg)
    o.save(D_0024D7C0+4*area, 0 if rng.random() < .1 else tab)
    o.save(reg+4*sub, lst); o.save(tab+4*sub, place)
    groups = rng.choice([1, 1, 2, 3])
    for g in range(groups):
        item = SYN_BASE+0x1000+g*0x1000
        o.save(lst+4*g, item)
        records = rng.choice([0, 1, 3, 6, 12])
        for k in range(records):
            for i, value in enumerate(synthetic_record(rng, True)): o.save(item+DEFERRED_SIZE*k+i, value, 1)
        o.save(item+DEFERRED_SIZE*records, 0xFFFF, 2)
    o.save(lst+4*groups, 0)
    count = rng.choice([0, 1, 5, 12, 30])
    for k in range(count):
        for i, value in enumerate(synthetic_record(rng, False)): o.save(place+PLACEMENT_SIZE*k+i, value, 1)
    o.save(place+PLACEMENT_SIZE*count, 0x00FF, 2)


def synthetic_progress(rng):
    data = bytearray(rng.choice([0, 0, 0xFF, 1, rng.getrandbits(8)]) if rng.random() < .5 else rng.getrandbits(8)
                     for _ in range(PROGRESS_END-PROGRESS_BASE))
    data[0x788-0x758] = rng.choice([0xFF, 0xFF, 0, 1])   # D_00810788
    data[0x778-0x758] = rng.choice([0xFF, 0, 3])         # D_00810778
    return bytes(data)


def run_synthetic(elf, lib, rng, case, stats):
    o, n = RosterOracle(elf), Native(lib)
    try:
        area = rng.randrange(24)            # D_00810860[area] and D_00810B40[area] inside the view
        sub = rng.choice([0, 1, 2, 7, 8, 9, 33])
        d702 = rng.randrange(256)
        write_synthetic_tables(o, rng, area, sub)
        groups, placement_address, placements = walk_roster(o.read, area, sub)
        roster = encode_roster(area, sub, groups, placement_address, placements)
        assert n.parse(roster) == 0
        progress = synthetic_progress(rng)
        o.byte(D_700, area); o.byte(D_701, sub); o.byte(D_702, d702); o.set_progress(progress)
        n.set_scene(area, sub, d702); n.set_progress(progress)
        o.call(RESET); n.reset()
        # Prefill / free through the S4-verified pool so allocations fail mid-table.
        fill = rng.choice([0, 0, 3, 40, 200, 230, 240, 246, 250, 254, 256])
        live = []
        for _ in range(fill):
            cls = rng.choice([4, 4, 0x0C, 1])
            results = (o.call(ALLOC, [cls]) & 0xFFFFFFFF, n.alloc(cls))
            assert results[0] == results[1], ('prefill', results)
            if results[0]: live.append(results[0])
        for node in rng.sample(live, min(len(live), rng.choice([0, 0, 2, 6]))):
            o.call(FREE_FN, [node]); n.free(node)
        compare(o, n, [], ('synthetic prefill', case))
        o.call(SPAWN)
        assert n.spawn() == 0, (case, n.fault())
        entries = n.entries()
        compare(o, n, entries, ('synthetic 001B6990', case))
        o.call(TITLE); assert n.spawn_title(log=False) == 0
        compare(o, n, entries, ('synthetic 001C5C50', case))
        cond, alloc_fail, skipped_0b = n.counters()
        stats['synthetic_cases'] += 1; stats['synthetic_spawned'] += len(entries)
        stats['synthetic_condition_skips'] += cond; stats['synthetic_alloc_failures'] += alloc_fail
        stats['synthetic_class_0b_skips'] += skipped_0b
        prime = progress[0x30] == 0xFF and bool(groups) and not (progress[0xB40-0x758+area] & (1 << (sub & 31)))
        stats['synthetic_prime_runs'] += int(prime)
        # 001B64F0 skips a record whose key byte is 0 (beqz): count primes where
        # that skip protects a set bit 0 of the area's first word.
        stats['synthetic_prime_zero_key'] += int(prime and progress[0x860-0x758+32*area] & 1 and any(
            (struct.unpack_from('<h', r, 4)[0] & ~0xE0) == 2 and struct.unpack_from('<h', r, 6)[0] in (1, 4, 5, 7)
            and struct.unpack_from('<h', r, 8)[0] & 0x40 and r[2] == 0 for g in groups for r in g[1]))
        stats['synthetic_progress_changed'] += int(n.progress() != progress)
        stats['synthetic_class2'] += sum(1 for g in groups for r in g[1] if (struct.unpack_from('<h', r, 4)[0] & ~0xE0) == 2)
    finally:
        n.close()


# ------------------------------------------------------------------ census

def walk_capture(ram):
    nodes, node = [], u32_at(ram, 0x275BC0)
    while node:
        nodes.append(dict(node=node, cls=ram[node+2], model=ram[node+3], param=ram[node+0xD],
                          uid=struct.unpack_from('<H', ram, node+0xE)[0], callback=u32_at(ram, node+0x10),
                          index=ram[node+0x9A]))
        node = u32_at(ram, node+0x1C)
        assert len(nodes) <= COUNT, 'list does not terminate'
    return nodes


def census(name, ram, overlay, roster_bytes, native_static, native_image, title_node, registry, stats):
    nodes = walk_capture(ram)
    # The captured overlay holds the exported table bytes.
    groups, placement_address, placements = walk_roster(
        lambda a, s: bytes(ram[a:a+s]), ram[D_700], ram[D_701])
    assert encode_roster(ram[D_700], ram[D_701], groups, placement_address, placements) == roster_bytes, \
        (name, 'captured tables differ from the export')
    static = [e for e in native_static if e['source'] in (1, 2)]
    kept = [e for e in static if e['record'] != 0x82A3C0+13*0x28]
    r13 = [e for e in static if e['record'] == 0x82A3C0+13*0x28]
    assert len(r13) == 1 and r13[0]['node'] == RECORD13_SLOT and r13[0]['flags'] & 1, (name, 'record 13')
    assert len(kept) == PREFIX, (name, len(kept))
    assert len(nodes) >= PREFIX, (name, len(nodes))
    for i, e in enumerate(kept):
        o = e['node']-POOL
        want = dict(node=e['node'], cls=native_image[o+2], callback=u32_at(native_image, o+0x10),
                    index=native_image[o+0x9A], uid=struct.unpack_from('<H', native_image, o+0xE)[0])
        got = {k: nodes[i][k] for k in want}
        assert got == want, (name, '#%d' % i, {k: hex(v) for k, v in got.items()}, {k: hex(v) for k, v in want.items()})
    assert all(n['node'] != RECORD13_SLOT for n in nodes[:PREFIX]), (name, 'record 13 slot in the prefix')
    # The 001C5C50 node: one 001C5930 node in the capture, same class/model/param/index/uid.
    titles = [n for n in nodes if n['callback'] == 0x1C5930]
    assert len(titles) == 1, (name, 'area-title nodes', len(titles))
    o = title_node-POOL
    want = dict(cls=native_image[o+2], model=native_image[o+3], param=native_image[o+0xD],
                index=native_image[o+0x9A], uid=struct.unpack_from('<H', native_image, o+0xE)[0])
    assert {k: titles[0][k] for k in want} == want, (name, 'area title')
    # Every callback after the prefix: a registry row whose origin is runtime.
    tail = nodes[PREFIX:]
    unknown = sorted({hex(n['callback']) for n in tail if n['callback'] not in registry})
    assert not unknown, (name, 'tail callbacks without a registry row', unknown)
    not_runtime = sorted({hex(n['callback']) for n in tail if registry[n['callback']][0] != 3})
    assert not not_runtime, (name, 'tail callbacks registered as roster output', not_runtime)
    static_callbacks = {n['callback'] for n in nodes[:PREFIX]}
    assert all(c in registry and registry[c][0] in (1, 2) for c in static_callbacks), name
    unbound = sorted({n['callback'] for n in tail if not registry[n['callback']][1]})
    print(f'{name}: {len(nodes)} nodes, prefix {PREFIX} equal, tail {len(tail)} '
          f'(record-13 slot {RECORD13_SLOT:#x} '
          + ('reused in the tail' if any(n['node'] == RECORD13_SLOT for n in tail) else 'free') + '), '
          f'unbound tail callbacks: {" ".join(f"{c:#08x}" for c in unbound)}')
    stats['census_captures'] += 1
    return dict(nodes=len(nodes), tail=len(tail), tail_callbacks=sorted(f'{c:#08x}' for c in {n['callback'] for n in tail}))


# --------------------------------------------------------------- fail-stop

def fail_stop(elf, overlay, lib, roster_bytes, stats):
    n = Native(lib)
    try:
        assert n.parse(roster_bytes) == 0
        n.reset(); before = n.globals()
        n.set_scene(0x0C, 0, 0)                               # area mismatch
        assert n.spawn() == -1 and n.fault() == (FAULT_BAD_INDEX, SPAWN), n.fault()
        assert n.globals() == before
        n.lib.shim_fault_clear(n.scene); n.set_scene(0x0B, 1, 0)   # sub mismatch
        assert n.spawn() == -1 and n.fault() == (FAULT_BAD_INDEX, SPAWN)
        # A failing bind faults at that node's callback and stops the spawn.
        n.lib.shim_fault_clear(n.scene); n.set_scene(0x0B, 0, 0); n.reset()
        calls = []

        @Bind
        def failing(_ctx, actor, _spawned):
            calls.append(actor); return -1 if len(calls) == 3 else 0
        assert n.spawn(failing) == -1
        assert n.fault() == (FAULT_WORKER_FAILED, 0x219550) and len(calls) == 3, n.fault()
        # Unbound nodes: the walk faults at the first node's callback.
        n.lib.shim_fault_clear(n.scene); n.reset()
        assert n.spawn() == 0
        assert n.lib.em_actor_pool_walk_001AFD70(n.pool, n.scene, 0, None, None, None) == -1
        assert n.fault() == (FAULT_NULL_WORKER, 0x219550), n.fault()
        # A spawn after a latched fault does nothing.
        g = n.globals(); assert n.spawn() == -1 and n.globals() == g
        stats['fail_stop_checks'] += 6
    finally:
        n.close()
    # A progress read outside the view (area 24: D_00810860[24] > D_00810B5F).
    n = Native(lib)
    try:
        rec = bytearray(DEFERRED_SIZE); struct.pack_into('<hHH', rec, 0, 1, 5, 4)
        data = encode_roster(24, 0, [(SYN_BASE, [bytes(rec)])], 0, [])
        assert n.parse(data) == 0
        n.set_scene(24, 0, 0); n.reset()
        assert n.spawn() == -1 and n.fault() == (FAULT_BAD_INDEX, 0x1B11E0), n.fault()
        stats['fail_stop_checks'] += 1
        # Malformed images are rejected.
        for bad in (roster_bytes[:-1], roster_bytes+b'\0', b'EMRX'+roster_bytes[4:],
                    roster_bytes[:4]+struct.pack('<I', 2)+roster_bytes[8:], b''):
            assert n.parse(bad) == -1
            stats['fail_stop_checks'] += 1
        assert n.lib.em_actor_roster_load(n.roster, str(ROOT/'build/actor_census_reference/missing.emro').encode()) == -1
        stats['fail_stop_checks'] += 1
    finally:
        n.close()


def main():
    elf = (DECOMP/'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA256
    overlay = (DECOMP/'extract/OVERLAY/AREA11.BIN').read_bytes()
    stats = dict(extension_checks=validate_extensions(elf), area11_spawn_cases=0, synthetic_cases=0,
                 synthetic_spawned=0, synthetic_condition_skips=0, synthetic_alloc_failures=0,
                 synthetic_class_0b_skips=0, synthetic_prime_runs=0, synthetic_prime_zero_key=0, synthetic_progress_changed=0,
                 synthetic_class2=0, census_captures=0, fail_stop_checks=0)
    lib = load_native()
    roster_bytes = build_area11_roster(elf, overlay)
    out = ROOT/'build/actor_census_reference'
    (out/'roster.emro').write_bytes(roster_bytes)
    asset = ROOT/'assets/scene_snow/roster.emro'
    asset_state = 'absent (run tools/export_area11_roster.py)'
    if asset.exists():
        assert asset.read_bytes() == roster_bytes, 'assets/scene_snow/roster.emro is stale'
        asset_state = 'identical to a fresh export'
        n = Native(lib)
        try: assert n.lib.em_actor_roster_load(n.roster, str(asset).encode()) == 0; n.parsed = True
        finally: n.close()

    reference = DECOMP/'build/startup-reference'
    captures = {}
    for name in REQUIRED:
        captures[name] = (reference/name).read_bytes()
    for name in EXTRA:
        if (reference/name).exists(): captures[name] = (reference/name).read_bytes()

    registry_c, registry_o, registry_b = (u32*64)(), (u32*64)(), (u32*64)()
    count = lib.shim_registry(registry_c, registry_o, registry_b, 64)
    assert count <= 64
    registry = {registry_c[i]: (registry_o[i], registry_b[i]) for i in range(count)}
    assert all(registry_b[i] or lib.shim_registry_name(registry_c[i]) for i in range(count)), 'unnamed UNBOUND row'
    assert registry[RECORD13_CALLBACK] == (2, 0)

    per_capture = {}
    for name, ram in captures.items():
        entries, counters, title, image = run_area11(elf, overlay, lib, roster_bytes, ram, stats)
        if name == REQUIRED[0]:
            summary = dict(spawned=len(entries), skipped_condition=counters[0], skipped_alloc=counters[1],
                           skipped_class_0b=counters[2], title_node=f'{title:#x}')
            assert (len(entries), counters) == (30, (0, 0, 0)), (len(entries), counters)
            assert [e['source'] for e in entries] == [1]*9 + [2]*21
            # Every static callback is registered with its origin.
            for e in entries:
                cb = u32_at(image, e['node']-POOL+0x10)
                assert cb in registry and registry[cb][0] == e['source'], hex(cb)
        per_capture[name] = census(name, ram, overlay, roster_bytes, entries, image, title, registry, stats)

    rng = random.Random(0x1B6990)
    for case in range(400):
        run_synthetic(elf, lib, rng, case, stats)
    for key in ('synthetic_condition_skips', 'synthetic_alloc_failures', 'synthetic_class_0b_skips',
                'synthetic_prime_runs', 'synthetic_prime_zero_key', 'synthetic_progress_changed', 'synthetic_class2'):
        assert stats[key] > 0, ('synthetic coverage', key)
    fail_stop(elf, overlay, lib, roster_bytes, stats)

    report = dict(status='PASS', **stats, area11=summary, asset=asset_state,
                  captures=per_capture, registry_rows=count,
                  executed=['001AF8E0', '001B6990', '001B6910', '001B65C0', '001B64F0', '001B6660', '001B11E0',
                            '001AFA90', '001AFA50', '001AFC10', '001AF800', '001AFBC0', '001C5C50'],
                  boundary='func_00121A28 memset (argument-checked stub)',
                  not_compared='+0x2E is compared through the spawn log (EmActor lacks the field); '
                               'D_00275BE4/D_00275BE8 (written only by 001B65C0, read by no main-ELF code)',
                  original_elf_sha256=ELF_SHA256)
    (out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__': main()
