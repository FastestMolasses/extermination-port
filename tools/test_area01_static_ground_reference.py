#!/usr/bin/env python3
"""Live placement-kind binding and AREA01 ground queries vs original code.

The native side loads the actual collision world and calls its public
placement setter. Captures initialize isolated test fixtures only.
"""
import ctypes as C
import json
import random
import struct
import subprocess
import time
from pathlib import Path
from types import SimpleNamespace

import reference_mode as mode
import test_actor_collision_reference as Q
from export_area01_common import captures, read_overlay, u32
from export_area01_level import cell_directory
from test_area01_math_reference import A01Base

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/level2/static-ground'
EMCL = ROOT / 'assets/area01/area01.emcl'
CELLS = ROOT / 'assets/area01/area01_cells.bin'
CRATE = 0x7AAB70


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    sources = ['collision_world', 'actor_collision', 'collision', 'actor_pool',
               'coll_probe_original', 'effect_original', 'coll_list_passes_walkers',
               'coll_list_passes', 'coll_segment_walkers', 'coll_move_original',
               'coll_grid_hull', 'sdk_math_original', 'sdk_soft_float', 'interaction_scan']
    path = OUT / 'ground.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc',
                    'tests/area01_static_ground_bridge.c',
                    *[f'src/game/em_{s}.c' for s in sources], '-lm', '-o', str(path)],
                   cwd=ROOT, check=True)
    n = C.CDLL(str(path)); P = C.c_void_p; U = C.c_uint32
    n.sg_load.argtypes = [C.c_char_p, C.c_char_p]
    n.sg_capture.argtypes = [P, P, U]
    n.sg_ground.argtypes = [U, C.c_uint8, P, P, U, P, C.POINTER(Q.BridgeHit)]
    n.sg_kind.argtypes = [C.c_uint]
    n.em_collision_world_bind_static_kinds.argtypes = [P, U]
    return n


def world(ram, spad, name):
    w = Q.World.__new__(Q.World)
    w.beat, w.ram, w.spad = name, bytes(ram), bytes(spad)
    w.table = u32(w.spad, 0x3250)
    w.count, hulls, w.size = cell_directory(w.ram, w.table)
    w.hulls = {i: (a, b) for i, (a, b, _) in hulls.items()}
    w.image = w.ram[w.table:w.table + w.size]
    bank = u32(w.ram, 0x28A598)
    w.node_base = bank + u32(w.ram, bank + 0x20)
    row = u32(w.ram, 0x24D7C0 + 4*w.ram[0x810700])
    w.placements = u32(w.ram, row + 4*w.ram[0x810701])
    cursor, count = u32(w.ram, 0x275B7C), Q.s16(w.ram, 0x275B84)
    w.owners = [u32(w.ram, cursor + 4*i) for i in range(count)]
    return w


class Caught(Exception):
    pass


def initializer_query(elf, capture):
    """Run the real crate initializer until its first ground-query entry.

    Only the crate state is reset to zero in this fixture. Model allocation,
    placement, publication and every preceding leaf execute original code.
    This is an initializer perturbation, not an unmodified route catch.
    """
    ee = A01Base(elf, capture.ram, capture.spad)
    assert ee.load(CRATE + 0x10) == 0x1551B0 and ee.load(CRATE + 0xE, 2) == 0x1900
    ee.save(CRATE + 4, 0, 1)
    found = []
    def catch(e):
        args = e.r[4:8]
        point = tuple(e.getf(args[1] + 4*i) for i in range(3))
        probe = tuple(e.getf(args[2] + 4*i) for i in range(3))
        found.append((world(e.mem, e.spad, capture.name), args[0], point, probe, args[3]))
        raise Caught()
    ee.hooks[Q.GROUND] = catch
    try:
        ee.call(0x1551B0, (CRATE, 0, 0))
    except Caught:
        pass
    assert len(found) == 1, (capture.name, 'initializer never reached ground')
    return found[0]


def main():
    start = time.monotonic(); n = build(); elf = Q.read_elf()
    emcl = EMCL.read_bytes(); pool = 0x30 + 12*u32(emcl, 8)
    first_grid = [emcl[pool + 24*i + 21] for i in range(u32(emcl, 12))].index(4)
    assert u32(emcl, 20) & 2, 'the AREA01 grid must carry original node classes'
    native = SimpleNamespace(bridge_ground=lambda _, *a: n.sg_ground(*a))
    records = captures(); overlay = read_overlay()
    count = hits = contracts = metadata = 0
    kinds_seen = set(); native_kinds = set()
    rng = random.Random(0x19F730)

    def setup(w):
        assert n.sg_load(str(EMCL).encode(), str(CELLS).encode()) == 0
        assert n.sg_capture(w.ram, w.spad, w.size) == 0
        assert n.em_collision_world_bind_static_kinds(w.ram[w.placements:w.placements + 40*w.count], w.count) == 0
        assert [n.sg_kind(i) for i in range(w.count)] == list(w.ram[w.placements + 8:w.placements + 40*w.count:40])

    def compare(w, actor, point, probe, mask, cls=None):
        nonlocal count, hits
        ee = Q.ee_world(elf, w)
        if cls is not None: ee.save(actor + 2, cls, 1)
        want = Q.ee_ground(ee, w, actor, point, probe, mask, first_grid)
        got = Q.native_ground(native, None, u32(w.ram, actor + 0x14),
                              ee.load(actor + 2, 1) & 31, point, probe, mask,
                              u32(w.ram, actor + 0xB4))
        assert Q.compare_ground(want, got, True), (w.beat, point, probe, mask, cls, want, got)
        count += 1; hits += bool(want['kind']); native_kinds.add(want['kind'])

    # Public failure contracts and cleanup are checked against the actual
    # owner, not a second placement projection in the test.
    n.em_collision_world_unload()
    assert n.em_collision_world_bind_static_kinds(overlay, 1) == -1; contracts += 1
    for capture in records:
        w = world(capture.ram, capture.spad, capture.name)
        Q.check_code(elf, w.ram, w.beat)
        assert w.placements == 0x82BD50 and w.count == 38
        data = w.ram[w.placements:w.placements + w.count*40]
        assert data == overlay[w.placements - 0x823500:w.placements - 0x823500 + len(data)]
        assert u32(w.image, 4) == 0xA000009C and data[8] == 0x52
        assert u32(w.image, 8) == 0xC0000448 and data[48] == 0x51
        metadata += len(data); kinds_seen.update(data[8::40])
        caught, actor, point, probe, mask = initializer_query(elf, capture)
        assert n.sg_load(str(EMCL).encode(), str(CELLS).encode()) == 0
        assert n.sg_capture(caught.ram, caught.spad, caught.size) == 0
        out = Q.BridgeHit(); feet = C.c_float(0.)
        assert n.sg_ground(actor, 4, Q.fvec(point), Q.fvec(probe), mask, C.byref(feet), C.byref(out)) == -1
        contracts += 1
        setup(caught); compare(caught, actor, point, probe, mask)
        setup(w)
        # Captured player query and real static hull's inside/outside,
        # upward/downward and class-0/2/4 gates. All queries retain both
        # original passes and use the actual AREA01 rank grid for mask 6/7.
        pos = struct.unpack_from('<3f', w.ram, Q.PLAYER + 0xB0)
        compare(w, Q.PLAYER, pos, (0., -4., 0.), 7)
        box = struct.unpack_from('<6f', w.image, w.hulls[0][0])
        queries = [(cls, mask, x, y, z, dy) for cls in (0, 2, 4) for mask in (2, 6, 7)
                   for x, z in (((box[0]+box[3])/2, (box[2]+box[5])/2),
                                (box[0]-1., box[2]-1.))
                   for y, dy in ((box[4]+2., box[1]-box[4]-4.),
                                 (box[1]-2., box[4]-box[1]+4.))]
        chosen = mode.select(queries, 6, 0x1900,
                             axes=(lambda q: q[0], lambda q: q[1], lambda q: q[-1]>0))
        for cls, mask, x, y, z, dy in chosen:
            compare(w, Q.PLAYER, (x, y, z), (0., dy, 0.), mask, cls)
        for _ in range(mode.pick(20, 1)):
            point = (rng.uniform(box[0]-2, box[3]+2), box[4]+2, rng.uniform(box[2]-2, box[5]+2))
            compare(w, Q.PLAYER, point, (0., box[1]-box[4]-4., 0.), 6, 2)
    # Refused changes retain the previously valid projection.
    saved = [n.sg_kind(i) for i in range(38)]
    for data, length in ((None, 38), (overlay, 0), (overlay, 257)):
        assert n.em_collision_world_bind_static_kinds(data, length) == -1
        assert [n.sg_kind(i) for i in range(38)] == saved; contracts += 1
    n.em_collision_world_unload(); assert n.sg_kind(0) == -1; contracts += 1
    # A later AREA11 load retains the former no-static-prefix contract.
    assert n.sg_load(str(ROOT/'assets/scene_snow/snow.emcl').encode(),
                     str(ROOT/'assets/scene_snow/area11_cells.bin').encode()) == 0
    assert n.sg_kind(0) == -1; contracts += 1
    n.em_collision_world_unload()
    result = dict(captures=len(records), ground_cases=count, hits=hits,
                  result_kinds=sorted(native_kinds), placement_kinds=sorted(kinds_seen),
                  overlay_bytes_compared=metadata, contracts=contracts,
                  seconds=round(time.monotonic()-start, 3))
    (OUT/('full.json' if mode.FULL else 'quick.json')).write_text(json.dumps(result, indent=2)+'\n')
    print('AREA01 static ground: PASS', result)


if __name__ == '__main__':
    main()
