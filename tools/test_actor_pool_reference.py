#!/usr/bin/env python3
"""Execute the original actor-pool instructions and compare src/game/em_actor_pool.c.

Original functions executed from the user's pinned ELF (never embedded here):
001AF8E0 reset, 001AFA90 alloc (with its 001AFA50 link), 001AFC10 free (with
001AF800 and 001AFBC0), 001AFD70 walk (with 001CB590 and 0x1CB5B0). Only
func_00121A28 (memset) is a boundary: the stub asserts the arguments the reset
passes (record, 0, 0x2F0) and zeroes the record.

Behaviours are synthetic: the jalr *(+0x10) of the walk lands on a fake
address whose action runs the ORIGINAL alloc/free through nested execution
(append a tail child, free itself, free another node, or free the captured
next). The same action runs against the native pool through ctypes. Compared
after every operation and walk: every record's original-layout bytes, the
globals D_00275BC0/BBC/BC4/BC8/B44, scratchpad 0x70003B8A, the ordered
(001CB590, behaviour) call list and the alloc results. Freeing the captured
next must fault natively (EM_SCENE_FAULT_FREED_NEXT at the owner's callback);
the oracle shows the original continuing into the freed record.
"""
import ctypes as C
import hashlib
import json
from pathlib import Path
import random
import subprocess
import sys

from test_point_light_reference import Oracle as Base, signed

ROOT = Path(__file__).resolve().parents[1]
POOL, SIZE, COUNT = 0x7A5640, 0x2F0, 0x100
HEAD, TAIL, FREE, FREE_COUNT, CURRENT = 0x275BC0, 0x275BBC, 0x275BC4, 0x275BC8, 0x275B44
SPAD_3B8A, D_701, D_702 = 0x70003B8A, 0x810701, 0x810702
RESET, ALLOC, FREE_FN, WALK, SELECT = 0x1AF8E0, 0x1AFA90, 0x1AFC10, 0x1AFD70, 0x1CB590
MEMSET = 0x121A28
FAKE = 0x00A00000          # synthetic behaviour addresses, outside the ELF
SENTINEL = 0x0BAD0000
FAULT_FREED_NEXT, FAULT_BAD_INDEX = 5, 4
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'


class Crash(Exception):
    """The original would jump somewhere the oracle does not model."""


class Stop(Exception):
    pass


class Original(Base):
    def __init__(self, elf):
        super().__init__(elf)
        self.arena = bytearray(COUNT*SIZE)
        self.calls[MEMSET] = self.memset
        self.behaviours = {}
        self.log = []
        self.depth = 0
        self.memsets = 0

    # Fast storage for the arena; everything else uses the base dict.
    def save(self, address, value, size=4):
        if POOL <= address and address+size <= POOL+COUNT*SIZE:
            o = address-POOL
            self.arena[o:o+size] = (value & ((1 << (8*size))-1)).to_bytes(size, 'little')
        else: super().save(address, value, size)

    def load(self, address, size=4):
        if POOL <= address and address+size <= POOL+COUNT*SIZE:
            o = address-POOL
            return int.from_bytes(self.arena[o:o+size], 'little')
        return super().load(address, size)

    def memset(self, _):
        a0, a1, a2 = self.r[4], self.r[5], self.r[6]
        assert a1 == 0 and a2 == SIZE and (a0-POOL) % SIZE == 0, (hex(a0), a1, a2)
        o = a0-POOL; self.arena[o:o+SIZE] = bytes(SIZE); self.r[2] = a0; self.memsets += 1

    def call(self, entry, args=()):
        self.depth += 1
        sentinel = SENTINEL+8*self.depth
        self.r[31] = sentinel
        for i, value in enumerate(args): self.r[4+i] = value & 0xFFFFFFFF
        pc = entry
        try:
            for _ in range(200000):
                if pc == sentinel: return self.r[2]
                if pc == SELECT: self.log.append((SELECT, self.r[4]))
                word = self.load(pc); op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
                offset = signed(word & 65535, 16)*4; branch = None
                if op in (2, 3):
                    target = (word & 0x3FFFFFF)*4
                    if op == 3: self.r[31] = pc+8
                    self.plain(self.load(pc+4))
                    if target in self.calls: self.calls[target](self); pc += 8
                    else: pc = target
                    continue
                if op == 0 and word & 63 == 9:           # jalr
                    target = self.r[rs]; link = pc+8
                    self.plain(self.load(pc+4))
                    rd = word >> 11 & 31
                    if target not in self.behaviours: raise Crash(hex(target))
                    node = self.r[4]
                    self.log.append((target, node))
                    self.behaviours[target](self, node)
                    self.r[rd] = link; pc = link
                    continue
                if op in (4, 5, 20, 21):
                    taken = (self.r[rs] == self.r[rt]) == (op in (4, 20))
                    if op in (20, 21) and not taken: pc += 8; continue
                    branch = pc+4+offset if taken else pc+8
                elif op in (6, 7):
                    taken = signed(self.r[rs]) <= 0 if op == 6 else signed(self.r[rs]) > 0
                    branch = pc+4+offset if taken else pc+8
                elif op == 1:
                    assert rt in (0, 1)
                    taken = signed(self.r[rs]) < 0 if rt == 0 else signed(self.r[rs]) >= 0
                    branch = pc+4+offset if taken else pc+8
                elif op == 0 and word & 63 == 8: branch = self.r[rs]
                if branch is not None:
                    self.plain(self.load(pc+4)); pc = branch
                else:
                    self.plain(word); pc += 4
            raise AssertionError('original routine did not return', hex(entry))
        finally:
            self.depth -= 1

    # --- driver interface shared with Native ---
    def reset(self): self.call(RESET)
    def alloc(self, cls): return self.call(ALLOC, [cls]) & 0xFFFFFFFF
    def free(self, node): self.call(FREE_FN, [node])
    def walk(self, mode):
        self.call(WALK, [mode]); return 0, 0, 0
    def next_of(self, node): return self.load(node+0x1C)
    def head(self): return self.load(HEAD)
    def bind(self, node, callback): self.save(node+0x10, callback)
    def set_drawn(self, node, value): self.save(node+1, value, 1)
    def poke(self, node, offset, value): self.save(node+offset, value, 1)
    def set_scene(self, d701, d702, spad):
        self.save(D_701, d701, 1); self.save(D_702, d702, 1); self.save(SPAD_3B8A, spad, 2)
    def spad(self): return self.load(SPAD_3B8A, 2)
    def globals(self):
        return (self.load(HEAD), self.load(TAIL), self.load(FREE), signed(self.load(FREE_COUNT, 2), 16),
                self.load(CURRENT))
    def image(self): return bytes(self.arena)


SHIM = r'''
#include "game/em_actor_pool.h"
#include <stdlib.h>
EmActorPool *shim_pool_new(void) { return calloc(1, sizeof(EmActorPool)); }
EmSceneState *shim_scene_new(void) { return calloc(1, sizeof(EmSceneState)); }
void shim_release(void *p) { free(p); }
EmActor *shim_record(EmActorPool *p, int i) { return &p->records[i]; }
void shim_bind(EmActor *a, uint32_t callback, EmActorBehavior fn) { a->callback = callback; a->behavior = fn; }
uint32_t shim_callback(const EmActor *a) { return a->callback; }
void shim_set_drawn(EmActor *a, uint8_t v) { a->drawn = v; }
void shim_scene_set(EmSceneState *s, uint8_t d701, uint8_t d702, uint16_t spad)
{ s->d810701 = d701; s->d810702 = d702; s->spad3B8A = spad; }
uint16_t shim_spad(const EmSceneState *s) { return s->spad3B8A; }
int32_t shim_fault_code(const EmSceneState *s) { return s->fault.code; }
uint32_t shim_fault_address(const EmSceneState *s) { return s->fault.address; }
void shim_fault_clear(EmSceneState *s) { s->fault.code = 0; s->fault.address = 0; }
#include <string.h>
#define FIELD(off, member) { off, sizeof(((EmActor *)0)->member), offsetof(EmActor, member) }
static const struct { unsigned off, size, at; } fields[] = {
    FIELD(0x00, status), FIELD(0x01, drawn), FIELD(0x02, cls), FIELD(0x03, model),
    FIELD(0x04, u04), FIELD(0x0A, u0A), FIELD(0x0D, param), FIELD(0x0E, uid),
    FIELD(0x2E, flags2), FIELD(0x30, w30), FIELD(0x36, h36), FIELD(0x52, h52), FIELD(0x54, kind), FIELD(0x56, link),
    FIELD(0x58, w58), FIELD(0x5C, w5C), FIELD(0x60, f60), FIELD(0x80, f80), FIELD(0x90, w90),
    FIELD(0x94, h94), FIELD(0x96, h96), FIELD(0x98, b98), FIELD(0x99, b99),
    FIELD(0x9A, table_index), FIELD(0x9C, b9C), FIELD(0x9D, b9D), FIELD(0x9E, b9E),
    FIELD(0xB0, pos), FIELD(0xC0, rot), FIELD(0x1F0, scratch)};
/* Write one original record byte into the native field that holds it. */
int shim_poke(EmActor *a, unsigned offset, uint8_t value)
{
    for (unsigned i = 0; i < sizeof fields / sizeof fields[0]; ++i)
        if (offset >= fields[i].off && offset < fields[i].off + fields[i].size) {
            memcpy((char *)a + fields[i].at + (offset - fields[i].off), &value, 1);
            return 1;
        }
    return 0;
}
'''

Behavior = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_void_p)
Trace = C.CFUNCTYPE(None, C.c_void_p, C.c_uint32, C.c_uint32, C.c_uint32, C.c_void_p)


class Globals(C.Structure):
    _fields_ = [('head', C.c_uint32), ('tail', C.c_uint32), ('free_head', C.c_uint32),
                ('free_count', C.c_int16), ('current', C.c_uint32)]


def load_native():
    out = ROOT/'build/actor_pool_reference'; out.mkdir(parents=True, exist_ok=True)
    (out/'shim.c').write_text(SHIM)
    library = out/('actor_pool.dylib' if sys.platform == 'darwin' else 'actor_pool.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC',
                    '-dynamiclib' if sys.platform == 'darwin' else '-shared', '-Isrc',
                    'src/game/em_actor_pool.c', str(out/'shim.c'), '-o', str(library)], cwd=ROOT, check=True)
    lib = C.CDLL(str(library))
    vp, u32 = C.c_void_p, C.c_uint32
    for name, res, args in [
            ('shim_pool_new', vp, []), ('shim_scene_new', vp, []), ('shim_release', None, [vp]),
            ('shim_record', vp, [vp, C.c_int]), ('shim_bind', None, [vp, u32, Behavior]),
            ('shim_callback', u32, [vp]), ('shim_set_drawn', None, [vp, C.c_uint8]),
            ('shim_scene_set', None, [vp, C.c_uint8, C.c_uint8, C.c_uint16]),
            ('shim_spad', C.c_uint16, [vp]), ('shim_fault_code', C.c_int32, [vp]),
            ('shim_fault_address', u32, [vp]), ('shim_fault_clear', None, [vp]),
            ('shim_poke', C.c_int, [vp, C.c_uint, C.c_uint8]),
            ('em_actor_pool_reset_001AF8E0', None, [vp]),
            ('em_actor_pool_alloc_001AFA90', vp, [vp, vp, C.c_uint8]),
            ('em_actor_pool_free_001AFC10', C.c_int, [vp, vp, vp]),
            ('em_actor_pool_walk_001AFD70', C.c_int, [vp, vp, C.c_int, vp, Trace, vp]),
            ('em_actor_pool_address', u32, [vp, vp]),
            ('em_actor_pool_globals', None, [vp, C.POINTER(Globals)]),
            ('em_actor_pool_record_image', None, [vp, vp, C.c_char_p])]:
        fn = getattr(lib, name); fn.restype = res; fn.argtypes = args
    return lib


class Native:
    def __init__(self, lib):
        self.lib = lib
        self.pool = lib.shim_pool_new(); self.scene = lib.shim_scene_new()
        self.behaviours = {}; self.log = []
        self.behavior = Behavior(self.dispatch)
        self.trace = Trace(self.on_trace)
        self.error = None

    def close(self):
        self.lib.shim_release(self.pool); self.lib.shim_release(self.scene)

    def ptr(self, address):
        assert (address-POOL) % SIZE == 0 and 0 <= address-POOL < COUNT*SIZE, hex(address)
        return self.lib.shim_record(self.pool, (address-POOL)//SIZE)

    def addr(self, pointer): return self.lib.em_actor_pool_address(self.pool, pointer)

    def dispatch(self, actor, _world):
        try:
            address = self.addr(actor)
            self.behaviours[self.lib.shim_callback(actor)](self, address)
            return 1
        except Exception as error:  # surface Python failures after the walk
            self.error = error
            return -1

    def on_trace(self, _ctx, caller, callee, address, _actor):
        assert caller == WALK
        self.log.append((callee, address))

    def reset(self): self.lib.em_actor_pool_reset_001AF8E0(self.pool)
    def alloc(self, cls): return self.addr(self.lib.em_actor_pool_alloc_001AFA90(self.pool, self.scene, cls))
    def free(self, node):
        assert self.lib.em_actor_pool_free_001AFC10(self.pool, self.scene, self.ptr(node)) == 0
    def walk(self, mode):
        result = self.lib.em_actor_pool_walk_001AFD70(self.pool, self.scene, mode, None, self.trace, None)
        if self.error: raise self.error
        return result, self.lib.shim_fault_code(self.scene), self.lib.shim_fault_address(self.scene)
    def next_of(self, node):
        image = C.create_string_buffer(SIZE)
        self.lib.em_actor_pool_record_image(self.pool, self.ptr(node), image)
        return int.from_bytes(image.raw[0x1C:0x20], 'little')
    def head(self): return self.globals()[0]
    def bind(self, node, callback): self.lib.shim_bind(self.ptr(node), callback, self.behavior)
    def set_drawn(self, node, value): self.lib.shim_set_drawn(self.ptr(node), value)
    def poke(self, node, offset, value): assert self.lib.shim_poke(self.ptr(node), offset, value)
    def set_scene(self, d701, d702, spad): self.lib.shim_scene_set(self.scene, d701, d702, spad)
    def spad(self): return self.lib.shim_spad(self.scene)
    def globals(self):
        g = Globals(); self.lib.em_actor_pool_globals(self.pool, C.byref(g))
        return (g.head, g.tail, g.free_head, g.free_count, g.current)
    def image(self):
        out = bytearray()
        buffer = C.create_string_buffer(SIZE)
        for i in range(COUNT):
            self.lib.em_actor_pool_record_image(self.pool, self.lib.shim_record(self.pool, i), buffer)
            out += buffer.raw
        return bytes(out)


def active(model):
    nodes, node = [], model.head()
    while node:
        nodes.append(node); node = model.next_of(node)
        assert len(nodes) <= COUNT
    return nodes


# Actions: ('none',), ('append', cls, child_callback), ('free_self',),
# ('free_other', k), ('free_next',). Implemented once for both models.
def make_behaviour(action, snapshots):
    def run(model, node):
        kind = action[0]
        if kind == 'append':
            child = model.alloc(action[1])
            if child: model.bind(child, action[2])
        elif kind == 'free_self':
            model.free(node)
        elif kind == 'free_other':
            nxt = model.next_of(node)
            eligible = [n for n in active(model) if n not in (node, nxt)]
            if eligible: model.free(eligible[action[1] % len(eligible)])
        elif kind == 'free_next':
            nxt = model.next_of(node)
            assert nxt, 'free_next scheduled on the tail'
            model.free(nxt)
            if isinstance(model, Original):
                snapshots.append((model.image(), model.globals(), model.spad(), list(model.log)))
        elif kind == 'visit_stop':
            # Reached only by the original after the freed-next hazard.
            if isinstance(model, Original):
                snapshots.append(('continued', node, model.load(node, 1)))
                raise Stop()
            raise AssertionError('native walk reached a freed next node')
    return run


def compare(original, native, where):
    a, b = original.image(), native.image()
    if a != b:
        i = next(i for i, (x, y) in enumerate(zip(a, b)) if x != y)
        raise AssertionError((where, 'record', i//SIZE, 'offset', hex(i % SIZE), a[i], b[i]))
    assert original.globals() == native.globals(), (where, original.globals(), native.globals())
    assert original.spad() == native.spad(), (where, original.spad(), native.spad())
    assert original.log == native.log, (where, original.log[:12], native.log[:12])


CLASSES = [0, 1, 1, 2, 3, 8, 9, 0x0B, 0x0C, 0x0C, 0x1F]
FLAGS = [0, 0, 0x20, 0x40, 0x80, 0xE0]


def random_class(rng): return rng.choice(CLASSES) | rng.choice(FLAGS)


# Record bytes the native struct holds, except +0x02 (class, kept as allocated),
# +0x09 (bones: nonzero needs the 001AF800 slot arena, covered by the C test)
# and +0x10..+0x1F (callback and links). Scribbling them after an alloc lets a
# later free and re-alloc show which bytes the original clears, rewrites or keeps.
SCRIBBLE = ([0, 1, 3, 4, 5, 6, 7, 8, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF, 0x2E, 0x2F] + list(range(0x30, 0x34)) +
            [0x36, 0x37] + list(range(0x52, 0x60)) + list(range(0x60, 0x90)) + list(range(0x90, 0x94)) +
            list(range(0x94, 0x9B)) + [0x9C, 0x9D, 0x9E] + list(range(0xB0, 0xD0)) +
            list(range(0x1F0, 0x2F0)))


def scribble(rng, node, *models):
    for offset in rng.sample(SCRIBBLE, 48):
        value = rng.randrange(1, 256)
        for model in models: model.poke(node, offset, value)


def scenario(rng, length, fault=False):
    """Prelude ops (alloc/free) that leave `length` live nodes, callbacks and actions."""
    ops, live, serial = [], 0, 0
    while live < length or (live and rng.random() < .15):
        if live < length and (rng.random() < .75 or live == 0):
            ops.append(('alloc', random_class(rng))); live += 1
        else:
            ops.append(('free', rng.randrange(live))); live -= 1
    kinds = ['none', 'none', 'append', 'free_self', 'free_other']
    # A fault case keeps every other behaviour inert so the captured next of
    # the owner is the next computed before the walk.
    actions = ['none']*length if fault else [rng.choice(kinds) for _ in range(length)]
    return ops, actions


def ticked(cls, mode):
    c = cls & 0x1F
    return not ((mode == 1 and c == 1) or (mode == 2 and c != 1))


def run_case(elf, lib, rng, length, modes, fault=False, stats=None):
    original, native = Original(elf), Native(lib)
    try:
        snapshots = []
        d701, d702, spad = rng.randrange(256), rng.randrange(256), rng.randrange(65536)
        for model in (original, native):
            model.set_scene(d701, d702, spad); model.reset()
        assert original.memsets == COUNT
        compare(original, native, 'reset')
        ops, actions = scenario(rng, length, fault)
        live = []
        for op in ops:
            if op[0] == 'alloc':
                results = [model.alloc(op[1]) for model in (original, native)]
                assert results[0] == results[1], ('alloc', op, results)
                if results[0]:
                    live.append(results[0])
                    scribble(rng, results[0], original, native)
            else:
                victim = live.pop(op[1] % len(live)) if live else None
                if victim:
                    for model in (original, native): model.free(victim)
        compare(original, native, 'prelude')
        # Callbacks: one per live node, plus child callbacks for appends.
        behaviours = {}
        stop = FAKE+0xFFF0
        behaviours[stop] = make_behaviour(('visit_stop',), snapshots)
        order = active(original)
        assert order == active(native)
        if fault:
            owners = [i for i, n in enumerate(order[:-1]) if ticked(original.load(n+2, 1), modes[0])]
            if not owners: return False
            actions[rng.choice(owners)] = 'free_next'
        for index, node in enumerate(order):
            kind = actions[index] if index < len(actions) else 'none'
            callback = FAKE+0x10*index
            if kind == 'append':
                child = FAKE+0x8000+0x10*index
                behaviours[child] = make_behaviour(rng.choice([('none',), ('free_self',)]), snapshots)
                action = ('append', random_class(rng), child)
            elif kind == 'free_other': action = ('free_other', rng.randrange(64))
            else: action = (kind,)
            behaviours[callback] = make_behaviour(action, snapshots)
            for model in (original, native):
                model.bind(node, callback); model.set_drawn(node, 0x5A)
        if fault:
            # A freed record keeps its +0x10; point it at the stop behaviour so
            # the original's continuation into it is observable.
            victim_owner = next(n for i, n in enumerate(order) if actions[i] == 'free_next')
            victim = original.next_of(victim_owner)
            for model in (original, native): model.bind(victim, stop)
        original.behaviours = native.behaviours = behaviours
        compare(original, native, 'bound')
        for mode in modes:
            original.log.clear(); native.log.clear()
            if fault:
                try:
                    original.walk(mode)
                    continued = False
                except Stop:
                    continued = True
                result, code, address = native.walk(mode)
                owner_callback = FAKE+0x10*order.index(victim_owner)
                assert (result, code, address) == (-1, FAULT_FREED_NEXT, owner_callback), \
                    (result, code, hex(address))
                image, globals_, spad_value, log = snapshots[0]
                assert image == native.image() and globals_ == native.globals()
                assert spad_value == native.spad() and log == native.log
                # The hazard the fault replaces: the original either calls the
                # freed record's stale +0x10 (its status byte is already 0), or
                # (mode 2 skips the cleared class) runs on down the free list.
                if continued:
                    assert snapshots[1] == ('continued', victim, 0), snapshots[1]
                    stats['fault_original_called_freed'] += 1
                else:
                    assert original.spad() > len(order), (original.spad(), len(order))
                    stats['fault_original_walked_free_list'] += 1
                stats['fault_cases'] += 1
                return True
            original.walk(mode)
            result = native.walk(mode)
            assert result == (0, 0, 0), result
            compare(original, native, ('walk', mode))
            stats['visits'] += sum(1 for callee, _ in original.log if callee != SELECT)
            stats['walks'] += 1
        stats['cases'] += 1
        return True
    finally:
        native.close()


def reserve_cases(elf, lib, stats):
    """Drain the pool with mixed classes; every result must match (0xC reserve,
    empty free list), then free in a shuffled order and refill (LIFO reuse)."""
    rng = random.Random(0xC0FFEE)
    for trial in range(4):
        original, native = Original(elf), Native(lib)
        try:
            for model in (original, native): model.set_scene(trial, 255-trial, 0); model.reset()
            live, refused = [], 0
            extra = 0
            while extra < 12:
                extra += original.globals()[3] == 0
                cls = rng.choice([0x0C, 0x2C, 0xEC, 0x0C, 1, 2, 0x22, 0x8B])
                results = [model.alloc(cls) for model in (original, native)]
                assert results[0] == results[1], ('reserve alloc', cls, results, original.globals())
                if results[0]: live.append(results[0])
                elif cls & 0x1F == 0x0C: refused += 1
                stats['reserve_allocs'] += 1
            compare(original, native, 'drained')
            assert refused and original.globals()[3] == 0, (refused, original.globals())
            rng.shuffle(live)
            for node in live[:len(live)//2]:
                for model in (original, native): model.free(node)
            compare(original, native, 'half freed')
            for _ in range(40):
                cls = rng.choice([0x0C, 3, 0x4C, 2])
                results = [model.alloc(cls) for model in (original, native)]
                assert results[0] == results[1]
            compare(original, native, 'refilled')
            stats['reserve_refused'] += refused
        finally:
            native.close()


def main():
    elf = (ROOT.parent/'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA256
    lib = load_native()
    stats = dict(cases=0, walks=0, visits=0, fault_cases=0, fault_original_called_freed=0,
                 fault_original_walked_free_list=0, reserve_allocs=0, reserve_refused=0)
    rng = random.Random(0x1AFD70)
    for length in range(0, 41):
        for modes in ([0], [1], [2], [3], [1, 2], [0, 0]):
            run_case(elf, lib, rng, length, modes, stats=stats)
    for length in range(2, 41, 2):
        for mode in (0, 1, 2, 3):
            # Retry when no node the mode ticks has a successor.
            for _ in range(20):
                if run_case(elf, lib, rng, length, [mode], fault=True, stats=stats): break
            else: raise AssertionError(('no fault owner', length, mode))
    reserve_cases(elf, lib, stats)
    report = dict(status='PASS', **stats, original_elf_sha256=ELF_SHA256,
                  executed=['001AF8E0', '001AFA90', '001AFA50', '001AFC10', '001AF800', '001AFBC0',
                            '001AFD70', '001CB590', '001CB5B0'],
                  boundary='func_00121A28 memset (argument-checked stub)')
    out = ROOT/'build/actor_pool_reference'
    (out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__': main()
