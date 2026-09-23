#!/usr/bin/env python3
"""Compare em_drum_original with the owner's original 00156620 instructions.

The oracle (test_crate_original_reference.Oracle) executes 00156620 and the
pure helpers it calls from the user's pinned ELF (001B0FD0, 00102948,
001029C0, 001029E8, 00102B08, 00102918, 001028D0, 00102738, 001B1470,
0011E2A8/0011DE90 sin/cos with their reduction kernels, 001B0D80) over the
two AREA11 records in playable_ee.bin/handoff_ee.bin (real fields, real
player mirror D_00810350, real D_00246A00/D_00246A10 tables) and over
synthetic states, including the model 0xA/0xC flight arm that AREA11 never
places. Model, collision, effect, sound, list-publish and draw functions
are explicit worker stubs. Each tick compares every modelled field
bit-for-bit, the ordered worker log with arguments, and asserts the
original wrote nothing outside the modelled fields, the stack and the
scratchpad. No instruction bytes are copied here.
"""
import ctypes as C
import hashlib
import json
from pathlib import Path
import random
import struct
import sys

from test_crate_original_reference import (Oracle, Script, gen_matrix, build, list_nodes,
    check_code, branch_targets, dead_after_branch, load_fields, store_fields, image,
    oracle_image, field_ranges, probe_result, CAPTURES, DRAW, SCRATCH, Probe,
    CALL, MATRIX, PROBE, RANDOM, SOUND, SOUND3D)
from test_interaction_scan_reference import ELF_SHA, DECOMP
from test_point_light_reference import STACK, bits, number, signed, fp

ROOT = Path(__file__).resolve().parents[1]
DRUM, DRUM_END = 0x156620, 0x156F30
SYN_ACTOR = 0x940000


class Drum(C.Structure):
    _fields_ = [(n, C.c_uint8) for n in ('status', 'visible', 'model', 'state', 'phase')] + [
        ('timer', C.c_int16), ('health', C.c_int16), ('damage', C.c_int16), ('speed', C.c_float),
        ('position', C.c_float*4), ('rotation', C.c_float*4), ('world', C.c_float*16),
        ('origin', C.c_float*4), ('origin_rotation', C.c_float*4), ('heading', C.c_float),
        ('lift', C.c_float)]


class Input(C.Structure):
    _fields_ = [('area', C.c_uint8), ('player', C.c_float*4), ('frame', C.c_int32),
                ('dispatch_index', C.c_int16), ('speed_table', C.c_float*4),
                ('lift_table', C.c_float*4)]


SEGMENT = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_float), C.POINTER(C.c_float), C.c_int32, C.c_int32)
EFFECT_MATRIX = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int32, C.POINTER(C.c_float))
VISIBLE = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_uint8))
EFFECT = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.POINTER(C.c_float))
SWEEP = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_float), C.c_uint32)

HOOK_NAMES = ['allocate_model', 'bone_init', 'place', 'segment', 'effect_matrix', 'draw', 'contact',
              'visibility', 'effect', 'sound', 'random', 'sweep', 'probe', 'sound3d', 'hull', 'free']
HOOK_TYPES = [CALL, CALL, MATRIX, SEGMENT, EFFECT_MATRIX, CALL, CALL, VISIBLE, EFFECT, SOUND, RANDOM,
              SWEEP, PROBE, SOUND3D, MATRIX, CALL]


class Hooks(C.Structure):
    _fields_ = [('context', C.c_void_p)] + list(zip(HOOK_NAMES, HOOK_TYPES))


FIELDS = [('status', 0, 'B'), ('visible', 1, 'B'), ('model', 3, 'B'), ('state', 4, 'B'),
          ('phase', 5, 'B'), ('timer', 0x28, 'h'), ('health', 0x34, 'h'), ('damage', 0x36, 'h'),
          ('speed', 0x38, 1), ('position', 0xB0, 4), ('rotation', 0xC0, 4), ('world', 0xD0, 16),
          ('origin', 0x200, 4), ('origin_rotation', 0x210, 4), ('heading', 0x264, 1),
          ('lift', 0x268, 1)]


def get(obj, name):
    v = getattr(obj, name)
    return [v] if isinstance(v, float) else v


def drum_image(drum):
    out = {}
    for name, offset, kind in FIELDS:
        if isinstance(kind, int): out[name] = tuple(bits(v) for v in get(drum, name))
        else: out[name] = getattr(drum, name) & (0xff if kind == 'B' else 0xffff)
    return out


def drum_store(o, base, drum):
    for name, offset, kind in FIELDS:
        if isinstance(kind, int):
            for i, v in enumerate(get(drum, name)): o.save(base + offset + 4*i, bits(v))
        else:
            size = 1 if kind == 'B' else 2
            o.save(base + offset, getattr(drum, name) & (0xff if size == 1 else 0xffff), size)


def drum_load(o, base):
    d = Drum()
    for name, offset, kind in FIELDS:
        if isinstance(kind, int):
            if kind == 1: setattr(d, name, number(o.load(base + offset)))
            else:
                arr = getattr(d, name)
                for i in range(kind): arr[i] = number(o.load(base + offset + 4*i))
        else:
            size = 1 if kind == 'B' else 2
            v = o.load(base + offset, size)
            setattr(d, name, signed(v, 16) if kind == 'h' else v)
    return d


def oracle_workers(o, script, log, base):
    def me(o): assert o.arg(0) == base, hex(o.arg(0))
    def allocate(o): me(o); log.append(('allocate_model',)); o.ret(script.take('allocate', lambda r: 0))
    def bone(o): me(o); log.append(('bone_init',))
    def place(o):
        me(o); m = script.take('place', gen_matrix)
        for i, v in enumerate(m): o.save(base + 0xD0 + 4*i, bits(v))
        log.append(('place',))
    def segment(o):
        log.append(('segment', o.words(o.arg(0), 3), o.words(o.arg(1), 3), signed(o.arg(2)), signed(o.arg(3))))
        o.ret(script.take('segment', lambda r: r.choice([0, 1, 1, 3])))
    def effect_matrix(o): log.append(('effect_matrix', signed(o.arg(0)), o.words(o.arg(1), 16)))
    def draw(o): me(o); log.append(('draw',))
    def contact(o): me(o); log.append(('contact',))
    def visibility(o):
        me(o); v = script.take('visible', lambda r: r.choice([0, 1, 1, 2]))
        o.save(base + 1, v, 1); log.append(('visibility', v))
    def effect(o): log.append(('effect', o.arg(0), o.words(o.arg(1), 4)))
    def sound(o): me(o); log.append(('sound', o.arg(1) & 0xffff))
    def rnd(o):
        v = script.take('random', lambda r: r.randrange(0x80000000)); log.append(('random', v)); o.ret(v)
    def sweep(o):
        me(o); log.append(('sweep', o.words(o.arg(1), 3), o.arg(2)))
        o.ret(script.take('sweep', lambda r: r.choice([0, 0, 0, 1])))
    def probe(o):
        me(o)
        result, actor, snap = script.take('probe', probe_result)
        log.append(('probe', o.words(o.arg(1), 3), o.load(o.arg(2) + 4), o.arg(3)))
        o.save(0x700031D4, actor)
        if o.arg(3) & 0x80000000 and result:
            o.save(base + 0xB4, bits(fp(number(o.load(base + 0xB4)) + snap)))
        o.ret(result)
    def sound3d(o): me(o); log.append(('sound3d', o.arg(1) & 0xffff, signed(o.arg(2)), o.f[12]))
    def hull(o):
        me(o); assert o.arg(1) == base + 0xD0; log.append(('hull', o.words(o.arg(1), 16)))
    def free(o): me(o); log.append(('free',))
    o.calls.update({0x1b0ea0: allocate, 0x1c62c0: bone, 0x1c6380: place, 0x19a570: segment,
                    0x1f0460: effect_matrix, DRAW: draw, 0x1b1d20: contact, 0x1b17a0: visibility,
                    0x1efd20: effect, 0x1fc580: sound, 0x122bb8: rnd, 0x19ad00: sweep,
                    0x19ab20: probe, 0x1fbd50: sound3d, 0x1a2370: hull, 0x1afc10: free})


def native_workers(script, log):
    def words(p, n): return tuple(bits(p[i]) for i in range(n))
    def allocate(_): log.append(('allocate_model',)); return script.take('allocate', lambda r: 0)
    def bone(_): log.append(('bone_init',)); return 0
    def place(_, world):
        for i, v in enumerate(script.take('place', gen_matrix)): world[i] = v
        log.append(('place',)); return 0
    def segment(_, a, b, mask, excl):
        log.append(('segment', words(a, 3), words(b, 3), mask, excl))
        return script.take('segment', lambda r: r.choice([0, 1, 1, 3]))
    def effect_matrix(_, kind, m): log.append(('effect_matrix', kind, words(m, 16))); return 0
    def draw(_): log.append(('draw',)); return 0
    def contact(_): log.append(('contact',)); return 0
    def visibility(_, out):
        v = script.take('visible', lambda r: r.choice([0, 1, 1, 2])); out[0] = v
        log.append(('visibility', v)); return 0
    def effect(_, i, p): log.append(('effect', i, words(p, 4))); return 0
    def sound(_, i): log.append(('sound', i)); return 0
    def rnd(_, out):
        v = script.take('random', lambda r: r.randrange(0x80000000)); log.append(('random', v))
        out[0] = v; return 0
    def sweep(_, p, mode):
        log.append(('sweep', words(p, 3), mode)); return script.take('sweep', lambda r: r.choice([0, 0, 0, 1]))
    def probe(_, position, frm, dy, mode, out):
        result, actor, snap = script.take('probe', probe_result)
        log.append(('probe', words(frm, 3), bits(dy), mode))
        out[0].result, out[0].actor = result, actor
        if mode & 0x80000000 and result: position[1] = fp(position[1] + snap)
        return 0
    def sound3d(_, i, mode, radius): log.append(('sound3d', i, mode, bits(radius))); return 0
    def hull(_, m): log.append(('hull', words(m, 16))); return 0
    def free(_): log.append(('free',)); return 0
    fns = [allocate, bone, place, segment, effect_matrix, draw, contact, visibility, effect, sound, rnd,
           sweep, probe, sound3d, hull, free]
    keep = [t(f) for t, f in zip(HOOK_TYPES, fns)]
    return Hooks(None, *keep), keep


class World:
    def __init__(self, elf, ram, base, area, player, frame, index):
        self.o = Oracle(elf, ram)
        self.base, self.area, self.player, self.frame, self.index = base, area, player, frame, index


def tick(native, world, drum, seed, fixed, stats, tag):
    o, base = world.o, world.base
    drum_store(o, base, drum)
    o.save(base + 0x4C, DRAW)
    o.save(0x810700, world.area, 1)
    for i, v in enumerate(world.player): o.save(0x810350 + 4*i, bits(v))
    o.save(0x70003B68, world.frame & 0xffffffff)
    o.save(0x70003B8A, world.index & 0xffff, 2)
    o.writes.clear()
    log_o = []
    o.calls.clear(); o.watch.clear()
    oracle_workers(o, Script(seed, **fixed), log_o, base)
    o.run(DRUM, (base,))
    log_n = []
    hooks, keep = native_workers(Script(seed, **fixed), log_n)
    speed = [number(o.load(0x246A00 + 4*i)) for i in range(4)]
    lift = [number(o.load(0x246A10 + 4*i)) for i in range(4)]
    inp = Input(world.area, (C.c_float*4)(*world.player), world.frame, world.index,
                (C.c_float*4)(*speed), (C.c_float*4)(*lift))
    result = native.em_drum_original_tick(C.byref(drum), C.byref(inp), C.byref(hooks))
    assert result >= 0, (tag, 'native fault', log_n, log_o)
    expected = {}
    for name, offset, kind in FIELDS:
        if isinstance(kind, int): expected[name] = o.words(base + offset, kind)
        else: expected[name] = o.load(base + offset, 1 if kind == 'B' else 2)
    actual = drum_image(drum)
    diff = {k: (actual[k], expected[k]) for k in expected if actual[k] != expected[k]}
    assert log_n == log_o and not diff, dict(tag=tag, diff=diff, native=log_n, original=log_o)
    assert (result == 0) == (('free',) in log_o), tag
    allowed = {base + off for off in field_ranges(FIELDS)}
    stray = sorted(a for a in o.writes if a not in allowed and not SCRATCH[0] <= a < SCRATCH[1]
                   and not STACK - 0x2000 <= a < STACK)
    assert not stray, (tag, [hex(a) for a in stray[:16]])
    stats['ticks'] += 1; stats['calls'] += len(log_o)
    return result, log_o


def captured(elf, native, stats):
    names = {}
    for capture in ('playable_ee.bin', 'handoff_ee.bin'):
        ram = (CAPTURES/capture).read_bytes()
        names[capture] = hashlib.sha256(ram).hexdigest()
        probe = Oracle(elf, ram)
        nodes = list_nodes(probe)
        drums = [n for n in nodes if probe.load(n + 0x10) == DRUM]
        assert len(drums) == 2, capture
        player = [number(probe.load(0x810350 + 4*i)) for i in range(4)]
        for base in drums:
            d0 = drum_load(probe, base)
            assert (d0.model, d0.state, d0.status, d0.health) == (0x18, 1, 1, 1), capture
            px, py, pz = d0.position[0], d0.position[1], d0.position[2]
            scenarios = [
                ('far', player, [dict()] * 3, {}),
                ('near', [px + 10.0, py + 3.0, pz - 5.0, 1.0], [dict()] * 2, {}),
                ('edge-in', [px + 30.0, py, pz + 40.0, 1.0], [dict()], {}),
                ('edge-out', [px + 30.0, py, pz + 40.05, 1.0], [dict()], {}),
                ('shot-decal', player, [dict(damage=1)] + [dict()] * 5, dict(segment=[1])),
                ('shot-plain', player, [dict(damage=1)] + [dict()] * 5, dict(segment=[0])),
                ('init', player, [dict(state=0)] + [dict()] * 2, {}),
                ('flight-0xA', player, [dict(damage=1, model=0xA)] + [dict()] * 40,
                 dict(probe=[(0, 0, 0.0)] * 6 + [(4, 0, 0.5)])),
                ('flight-0xC', player, [dict(damage=1, model=0xC)] + [dict()] * 8 + [dict(damage=1)] + [dict()] * 4,
                 dict(probe=[(0, 0, 0.0)])),
            ]
            for name, where, steps, fixed in scenarios:
                # 0x70003B8A is 001AFD70's 1-based walk count at this node.
                world = World(elf, ram, base, 11, where, 0x1234, nodes.index(base) + 1)
                drum = drum_load(world.o, base)
                fixed = dict(fixed, place=[list(drum.world)])
                for k, overrides in enumerate(steps):
                    for field, value in overrides.items(): setattr(drum, field, value)
                    seed = base ^ (len(name) << 20) ^ (k << 8)
                    result, log = tick(native, world, drum, seed, fixed, stats,
                                       (capture, hex(base), name, k))
                    stats['captured_ticks'] += 1
                    if name in ('near', 'edge-in', 'edge-out', 'far') and k == 0:
                        stats['publish'][f'{name}'] = 'contact' if ('contact',) in log else 'visibility'
                    if result == 0: break
                check_code(elf, ram, world.o.pcs)
                stats['pcs'] |= world.o.pcs
    return names


def synthetic(elf, native, stats, count):
    rng = random.Random(0x156620)
    for case in range(count):
        base = SYN_ACTOR
        player = [rng.uniform(-400, 400) for _ in range(3)] + [1.0]
        world = World(elf, None, base, rng.choice([11, 0x15, 0x15, 2]), player,
                      rng.choice([0, 0x40, 0x3F, 0x1234, -5]), rng.choice([0, 1, 23, -1]))
        d = Drum()
        d.model = rng.choice([0x18, 0x2A, 0xA, 0xC, 0x0B])
        d.state = rng.choice([0, 1, 1, 2, 2, 2, 3, 4])
        d.phase = rng.choice([0, 1, 2, 2, 3, 3, 4])
        d.status = rng.choice([0, 1, 2])
        d.visible = rng.choice([0, 1])
        d.timer = rng.choice([0, 1, 2, 8, -1])
        d.health = 1
        d.damage = rng.choice([0, 0, 1, 0x2000, 0x2001, -3])
        d.speed = rng.choice([0.0, rng.uniform(0, 3)])
        for i in range(3):
            d.position[i] = rng.choice([rng.uniform(-400, 400), rng.uniform(-260, 10)])
            d.rotation[i] = rng.uniform(-3.1, 3.1)
        d.position[3] = 1.0
        if rng.random() < 0.3:
            for i in range(3): player[i] = d.position[i] + rng.uniform(-40, 40)
        m = gen_matrix(rng)
        for i in range(16): d.world[i] = m[i]
        d.heading = rng.uniform(-3.1, 3.1)
        d.lift = rng.choice([rng.uniform(-4.5, 2), -4.0, 0.0])
        fixed = {}
        if rng.random() < 0.3: fixed['allocate'] = [rng.choice([0, 1])]
        for k in range(rng.randrange(1, 10)):
            result, _ = tick(native, world, d, rng.randrange(1 << 30), fixed, stats, ('synthetic', case, k))
            if result == 0: break
            if rng.random() < 0.1: d.damage = rng.choice([1, 0x2000])
        stats['synthetic_sequences'] += 1
        stats['pcs'] |= world.o.pcs


def main():
    elf = (DECOMP/'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    native, out = build()
    native.em_drum_original_tick.argtypes = [C.POINTER(Drum), C.POINTER(Input), C.POINTER(Hooks)]
    stats = dict(ticks=0, calls=0, captured_ticks=0, synthetic_sequences=0, pcs=set(), publish={})
    captures = captured(elf, native, stats)
    synthetic(elf, native, stats, int(sys.argv[1]) if len(sys.argv) > 1 else 1500)
    owner = {pc for pc in stats['pcs'] if DRUM <= pc < DRUM_END}
    total = (DRUM_END - DRUM) // 4
    missing = sorted(pc for pc in range(DRUM, DRUM_END, 4) if pc not in owner)
    targets = branch_targets(elf, DRUM, DRUM_END)
    reachable_missing = [hex(pc) for pc in missing if not dead_after_branch(elf, pc, targets)]
    assert not reachable_missing, reachable_missing
    report = dict(status='PASS', ticks=stats['ticks'], captured_ticks=stats['captured_ticks'],
                  synthetic_sequences=stats['synthetic_sequences'], worker_calls=stats['calls'],
                  owner_instructions=total, executed=len(owner),
                  unexecuted_dead=[hex(pc) for pc in missing], captured_publish=stats['publish'],
                  captures=captures, elf_sha256=ELF_SHA)
    (out/'drum_report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f"Original drum owner 00156620: PASS {stats['ticks']} ticks "
          f"({stats['captured_ticks']} captured, {stats['synthetic_sequences']} synthetic sequences), "
          f"{stats['calls']} worker calls, {len(owner)}/{total} owner instructions executed")


if __name__ == '__main__': main()
