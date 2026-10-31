#!/usr/bin/env python3
"""Shared inputs of the AREA08 and AREA07 asset exports (docs/AREA08_PORT.md):
the subs the sixteenth level's fork recordings load.

  AREA08 (area 8, OVERLAY/AREA08.BIN = MWo3 id 8 at 0x823500)
    sub 0  a03b_06_lift, a08_00, a08_01, a08_02  (the lift, the west rooms)
    sub 2  a08_03_slider                         (the live floor)
    sub 3  a08_04_ceiling, a08_05_terminal       (the terminal)
  AREA07 (area 7, OVERLAY/AREA07.BIN = MWo3 id 7)
    sub 0  a04c_02_lift, a07_00_switch           (the lift, the chambers)
    sub 2  a07b_01_sub2, a07b_02_duct            (the flames, the turrets)
    sub 3  a07b_03_sub3, a07b_04_terminal        (the terminal)

Like export_area15_common: the AREA13 lane's exporters model an area through
a Target and per-target tables, imported and run unchanged; `install` points
them at one (target, sub). Each load's nested list relocates id 0x45 in
AREA08 subs 0, 3 and AREA07 sub 0 (export_area15_level.export_dynamic), not in
AREA08 sub 2 and AREA07 sub 2 (the stale-pointer model of
export_area13_level.export_target).

001A2370 / 0019C6F0 rules: no AREA08 C calls either; of AREA07's C only
0x823F80 calls 001A2370 (*(node + 0x110)'s bone record, sub 2's deferred
group, not loaded in sub 0). 001BC150 is called by AREA08's door 0x823870,
which is therefore a door behaviour of export_area13_tables' door rule.

Nothing here is original data: every byte is read from the user's own files
and checked against the captures.
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area13_common as A13  # noqa: E402  (first: it keeps AREA01's nested load-map builder)
import export_area13_level as LV  # noqa: E402
import export_area13_tables as TB  # noqa: E402
import export_area13_sfx as SX  # noqa: E402

C = A13.C
ROOT, DECOMP = A13.ROOT, A13.DECOMP
SET = DECOMP / 'build/fork_refs/levels-recordings-20261030-1/s87'
SET2 = DECOMP / 'build/fork_refs/levels-recordings-20261031-1/s87'
SCRATCH = ROOT / 'build/area08/assets'
AREA08_SHA256 = 'f50d8d7cc590a448a0ed4a8c0f6ae99d096897534eb4d72dfc7569c7c498d3c6'
AREA07_SHA256 = '3233b7bf53df85452cbd00959b09725734a22afccccd9ef670d62f72f716f96e'
AREA08 = A13.Target('area08', 8, 0, 'AREA08.BIN', 0x4A00, 8, AREA08_SHA256, 'chunk12', True,
                    ROOT / 'assets/area08/sub0', 'AREA08')
AREA07 = A13.Target('area07', 7, 0, 'AREA07.BIN', 0x4B80, 7, AREA07_SHA256, 'chunk11', True,
                    ROOT / 'assets/area07/sub0', 'AREA07')
A13.TARGETS.update(area08=AREA08, area07=AREA07)
TARGETS = {'area08': AREA08, 'area07': AREA07}

# (target, sub): the capture folders in route order, the capture before the load
SUBS = {
    ('area08', 0): dict(paths=(SET / 'route_a03b/a03b_06_lift', SET / 'route_a08/a08_00_door17',
                               SET / 'route_a08/a08_01_magazine', SET / 'route_a08/a08_02_switch'),
                        previous=SET / 'route_a03b/a03b_05_slider', dynamic=True),
    ('area08', 2): dict(paths=(SET / 'route_a08/a08_03_slider',), previous=SET / 'route_a08/a08_02_switch',
                        dynamic=False),
    ('area08', 3): dict(paths=(SET / 'route_a08/a08_04_ceiling', SET / 'route_a08/a08_05_terminal'),
                        previous=SET / 'route_a08/a08_03_slider', dynamic=True),
    ('area07', 0): dict(paths=(SET / 'route_a04c/a04c_02_lift', SET / 'route_a07/a07_00_switch'),
                        previous=SET / 'route_a04c/a04c_01_door73', dynamic=True),
    ('area07', 2): dict(paths=(SET2 / 'route_a07b/a07b_01_sub2', SET2 / 'route_a07b/a07b_02_duct'),
                        previous=SET2 / 'route_a07b/a07b_00_switch', dynamic=False),
    ('area07', 3): dict(paths=(SET2 / 'route_a07b/a07b_03_sub3', SET2 / 'route_a07b/a07b_04_terminal'),
                        previous=SET2 / 'route_a07b/a07b_02_duct', dynamic=True),
}
# every func_001BA1A0 script argument of the committed overlay C (re-read by
# export_area13_tables.c_script_entries)
SCRIPT_ENTRIES = {
    'area08': (0x825880, 0x825AA0, 0x825E60, 0x826420, 0x8268E0, 0x827170, 0x827330),
    'area07': (0x8264B0, 0x826930, 0x8271A0, 0x827360),
}
DOORS = {'area08': (0x1BC350, 0x1BB860, 0x1BD9F0, 0x1BD560, 0x823870),
         'area07': (0x1BC350, 0x1BB860, 0x1BD9F0, 0x1BD560)}
DOOR_ROW = {'area08': 0x24E070, 'area07': 0x24E040}


def retained_colours(cap):
    """The two colour windows D_00251BE0..EB (001D40E0) and D_00251C20..2B
    (001D4440) hold the last class-6 draw's tint in the captures of this set:
    each draw rewrites them before its packet goes out, so they are no load
    state. The ORIGINAL routines run here over the capture with both windows
    poisoned and the neutral tint (1, 1, 1) of an owner's +0x80: their integer
    conversion gives the bytes the comparison expects (the unrelated display
    list workers are boundaries). No captured colour is copied."""
    import test_effect_manager_reference as E
    ram = bytearray(cap.ram)
    tint = 0x01F00000
    ram[tint:tint + 16] = struct.pack('<4f', 1.0, 1.0, 1.0, 1.0)
    out = {}
    for fn, lo in ((0x1D40E0, 0x251BE0), (0x1D4440, 0x251C20)):
        ram[lo:lo + 12] = bytes([0x5A]) * 12
        oracle = E.Oracle(C.read_elf(), ram, bytearray(cap.spad))

        def boundary(machine):
            machine.set32(2, 0)

        for address in (0x1D2090, 0x1D6930, 0x1D1F80, 0x1D6580, 0x1D1F20, 0x1D6BA0, 0x1D1FF0, 0x1D2910):
            oracle.stubs[address] = boundary
        oracle.run(fn, (3, tint, tint))
        out[lo] = bytes(oracle.ram[lo:lo + 12])
    return out


_CHECK_CTX_BLOCK = LV.E22.check_ctx_block


def check_ctx_block(caps, elf):
    """The shared room checks, with retained_colours in the expected ELF
    blocks of export_render_context's comparison."""
    import export_render_context as ERC
    colours = retained_colours(caps[0])
    original = ERC.verify

    def verify(blocks, ram):
        expected = []
        for address, data in blocks:
            data = bytearray(data)
            for lo, bytes_ in colours.items():
                if address <= lo and lo + 12 <= address + len(data):
                    data[lo - address:lo - address + 12] = bytes_
            expected.append((address, data))
        return original(expected, ram)

    ERC.verify = verify
    try:
        return _CHECK_CTX_BLOCK(caps, elf)
    finally:
        ERC.verify = original


def flag_calls(ram):
    """No AREA07 / AREA08 C calls 0019C6F0."""
    return ()


def pins(target, sub):
    """The roster and spawn pins of (target, sub), by the original's walks
    over the pinned ELF and overlay (the exporters compare them again)."""
    t = TARGETS[target]
    read = C.static_reader(C.read_elf(), A13.read_overlay(t))
    groups, paddr, _placements = TB.R.walk_roster(read, t.area, sub)
    _table, entries, count = TB.spawn_rows(read)
    return paddr, tuple((a, len(r)) for a, r in groups), (entries, count)


def install(target, sub, tree=None):
    """Point the AREA13 lane's modules at (target, sub); `tree` is the scratch
    tree the parts go under (default SCRATCH/tree). Returns the module C."""
    v = SUBS[(target, sub)]
    t = TARGETS[target]
    t.sub = sub
    A13.SCRATCH = SCRATCH / 'scratch'
    A13.OUT = (tree or SCRATCH / 'tree') / target / f'part{sub}'
    t.out = A13.OUT / f'sub{sub}'
    A13.capture_paths = lambda: [p for p in v['paths']]
    A13.configure(t)
    LV.PREVIOUS[target] = v['previous']
    # AREA07 sub 2's turrets 0x823F80: 001A2370 over *(node + 0x11C) + 0x90
    # (func_overlay_AREA07_00823F40.c), the AREA01 twin's rule.
    LV.CREATURES[target] = 0x823F80 if target == 'area07' else None
    LV.FLAG_OWNERS[target] = None
    LV.flag_calls = flag_calls
    LV.E22.check_ctx_block = check_ctx_block
    TB.SCRIPT_ENTRIES[target] = SCRIPT_ENTRIES[target]
    TB.WRITERS[target] = ()
    TB.DOOR_ROW[target] = DOOR_ROW[target]
    TB.DOOR_BEHAVIOURS = DOORS[target]
    TB.EXPLICIT_MODELS = {}
    TB.PLACEMENTS[target], TB.GROUPS[target], TB.SPAWN_SUB0[target] = pins(target, sub)
    LV.install(target)
    return C
