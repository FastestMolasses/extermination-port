#!/usr/bin/env python3
"""Shared inputs of the AREA15 asset exporters (docs/AREA15_ASSETS.md):
AREA15 sub 0 and sub 1 as the fourteenth level's a19d / a15 captures load
them.

AREA15 (area 0x0F, OVERLAY/AREA15.BIN = MWo3 id 0x0C at 0x823500) has two
subs: INDEX.IDX sector 19 holds a top block (no sections, resident offset
0) and two nested blocks (chunk19.n0 / chunk19.n1). The fourteenth level
entered it twice (FOURTEENTH_LEVEL_ROUTE.md); one capture ends in each sub
(../Extermination/build/s87/, ignored):

  sub 0  route_a19d/a19d_20_door50  area bytes 0F 00 01  after [0]'s scene
  sub 1  route_a15/a15_01_door51    area bytes 0F 01 00  sub 1 entry 0

Each sub's capture paths are its group's folders: sub 0 the a19d group
(a19d_00 .. a19d_19 end in AREA19 and are listed as excluded), sub 1 the a15
group (a15_00 ends in AREA19 sub 1 and is excluded). The capture before
each load (D_0028A5A4's previous capture) is a19d_19_flights resp.
a15_00_door14.

The AREA13 lane's exporters (export_area13_common / _level / _tables /
_sfx) model an area through a Target object and per-target tables keyed by
its name. They are imported and run UNCHANGED; this module adds an AREA15
Target ('area15', registered in export_area13_common.TARGETS) and `install
(sub)` points every per-target table at it:
  * export_area13_common: OUT -> TREE/part<sub>, AREA15.out ->
    TREE/part<sub>/sub<sub>, SCRATCH -> build/area15/assets/scratch,
    capture_paths -> the sub's group;
  * export_area13_level: PREVIOUS / CREATURES / FLAG_OWNERS['area15'], and
    flag_calls / owner_matrix -> the AREA15 rules below (falling back to
    the AREA13 lane's outside AREA15);
  * export_area13_tables: PLACEMENTS / GROUPS / SPAWN_SUB0 / DOOR_ROW /
    SCRIPT_ENTRIES / WRITERS['area15'] (pinned per sub, each read by the
    original's table walks from the pinned ELF and overlay);
    DOOR_BEHAVIOURS without the AREA13 / AREA19 overlay door 0x823580 and
    EXPLICIT_MODELS without the AREA13 hatch 0x826850 (both behaviour
    addresses of another module: in AREA15 0x826850 is a sub-1 placement
    behaviour, and no AREA15 node behaviour or door is at 0x823580, where
    the AREA15 init 00823540 runs); uninstall() restores them.
export_area15_level.py exports sub 0's dynamic list (D_0028A5A4 = the
relocated id 0x45, which the AREA13 lane's level exporter refuses);
export_area15_split.py places the two parts under assets/area15/.

Nothing here is original data: every byte is read from the user's own files
(the pinned ELF and overlay, the extracted chunk19 files, the disc image)
and checked against the captures.
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area13_common as A13  # noqa: E402  (first: it keeps AREA01's nested load-map builder)
import export_area13_level as LV  # noqa: E402
import export_area13_tables as TB  # noqa: E402
import export_area13_sfx as SX  # noqa: E402

C = A13.C
E02L = LV.E02                                      # export_area02_level (derive_words reads flag_calls)
ROOT, DECOMP = A13.ROOT, A13.DECOMP
OUT = ROOT / 'assets/area15'
SCRATCH = ROOT / 'build/area15/assets'
TREE = SCRATCH / 'tree'
ROUTE_A19D = DECOMP / 'build/s87/route_a19d'
ROUTE_A15 = DECOMP / 'build/s87/route_a15'
# SHA-256 of the user's extract/OVERLAY/AREA15.BIN (a hash, not disc data)
OVERLAY_SHA256 = '99426fad965d75fd542d3d2266759f6479cba87eedc3f96e6c433453bd00e83b'
TARGET = A13.Target('area15', 0x0F, 0, 'AREA15.BIN', 0x6A00, 0x0C, OVERLAY_SHA256, 'chunk19', True,
                    TREE / 'part0/sub0', 'AREA15')
A13.TARGETS['area15'] = TARGET

# Per sub: the capture group, its one AREA15 capture, the capture before the
# load, and the roster / spawn pins (D_0024D7C0[0x0F][sub], D_0024D820
# [0x0F][sub], D_0024D650[0x0F][sub]: export_area11_roster.walk_roster and
# export_area13_tables.spawn_rows over the pinned ELF and overlay).
SUBS = {
    0: dict(route=ROUTE_A19D, glob='a19d_*', capture='a19d_20_door50', previous='a19d_19_flights',
            placements=0x829800, groups=((0x826980, 6),), spawn=(0x24CB80, 3)),
    1: dict(route=ROUTE_A15, glob='a15_*', capture='a15_01_door51', previous='a15_00_door14',
            placements=0x829C90, groups=((0x826AC0, 11),), spawn=(0x24CC10, 1)),
}
CAPTURE_NAMES = {s: (v['capture'],) for s, v in SUBS.items()}
DOOR_ROW = 0x24E0B8                                # D_0024E140[0x0F]
# every func_001BA1A0 script argument of the committed AREA15 overlay C
# (export_area13_tables.c_script_entries, re-read by the checker)
SCRIPT_ENTRIES = (0x826E70, 0x827130, 0x827400, 0x8277C0, 0x827D70, 0x8281F0, 0x828670, 0x8287B0, 0x828900,
                  0x828A40, 0x828CA0, 0x828FA0, 0x8290E0, 0x829330, 0x8296F0)
# No AREA15 C calls 001BC150 / 001BC240 (export_area13_tables' door rule);
# the boot door behaviours stay.
DOOR_BEHAVIOURS = (0x1BC350, 0x1BB860, 0x1BD9F0, 0x1BD560)
WRITERS = ()

# 001A2370 callers of the AREA15 module (their overlay C; every jal of
# 001A2370 in the module lies in one of them):
#   0x825320 (func_overlay_AREA15_008252E0.c, byte-identical): state 1,
#            self + 0xD0;
#   0x826600 (func_overlay_AREA15_008265C0.c, byte-identical): state 1,
#            self + 0xD0 (a sub-1 placement, [8]);
#   0x825430 (func_overlay_AREA15_008253F0.c, NEARMISS) and its mirrored
#   0x825D10 (func_overlay_AREA15_00825CD0.c, NEARMISS): state 1,
#            D_00275B40[0] + 0x90, i.e. *(node + 0x110) + 0x90 (D_00275B40 =
#            node + 0x110 while a behaviour runs: the AREA01 lane's reading
#            that export_area13_level's creature rule uses); the NEARMISS
#            bodies differ only by two scratchpad reloads (their headers),
#            not in this argument. The evidence is the ORIGINAL instructions
#            at their call sites 0x825C50 / 0x826540 (a1 = the word that
#            D_00275B40 points at, plus 0x90; the delay slot of the call),
#            which the checker's ov_arg_problems reads from the user's
#            overlay. The a19d_20 capture does NOT tell this rule from
#            node + 0xD0: both swing nodes are at rest there and the 64
#            bytes at node + 0xD0 and *(node + 0x110) + 0x90 are equal.
SELF_MATRIX_OWNERS = (0x825320, 0x826600)
BONE0_OWNERS = (0x825430, 0x825D10)
# 0019C6F0 callers of the AREA15 module: only [8] 0x826600 (sub 1;
# func_overlay_AREA15_008265C0.c, byte-identical): state 0 calls (0x22, 1)
# once and goes on (001B0FD0); states 2 / 3 call (0x22, 0) and free the node.
# So a live node in state 1 made (0x22, 1) last; one still in state 0 has
# made no call. Any other state, or two nodes: not decided (ValueError).
FLAG_OWNER = 0x826600
FLAG_KEY = 0x22
FLAG_STATES = {0: None, 1: (FLAG_KEY, 1)}

_BASE_FLAG_CALLS = LV.flag_calls
_BASE_OWNER_MATRIX = LV.owner_matrix
_BASE_DOOR_BEHAVIOURS = TB.DOOR_BEHAVIOURS
_BASE_EXPLICIT_MODELS = TB.EXPLICIT_MODELS
_CURRENT_SUB = [0]


def sub():
    return _CURRENT_SUB[0]


def capture_paths(s=None):
    """The sub's capture group in route order."""
    v = SUBS[sub() if s is None else s]
    return sorted(p for p in v['route'].glob(v['glob']) if (p / 'eeMemory.bin').exists())


def previous(s=None):
    v = SUBS[sub() if s is None else s]
    return v['route'] / v['previous']


def in_area15(ram):
    return ram[0x810700] == TARGET.area


def _nodes(ram, behaviour):
    return [a for _s, a in E02L.pool(ram) if C.u32(ram, a + 0x10) == behaviour]


def owner_matrix(ram, node):
    """export_area13_level.owner_matrix (the pickup, the drum), plus the
    AREA15 owners above: node + 0xD0, or *(node + 0x110) + 0x90. Outside
    AREA15: the AREA13 lane's."""
    if in_area15(ram):
        behaviour = C.u32(ram, node + 0x10)
        if behaviour in SELF_MATRIX_OWNERS:
            return node + 0xD0
        if behaviour in BONE0_OWNERS:
            return C.u32(ram, node + 0x110) + 0x90
    return _BASE_OWNER_MATRIX(ram, node)


def flag_calls(ram):
    """The 0019C6F0 calls of an AREA15 capture whose last effect the capture
    decides ([8]'s key 0x22, FLAG_STATES). Outside AREA15: the AREA13
    lane's flag_calls."""
    if not in_area15(ram):
        return _BASE_FLAG_CALLS(ram)
    nodes = _nodes(ram, FLAG_OWNER)
    if len(nodes) > 1:
        raise ValueError(f'{FLAG_OWNER:#x}: {len(nodes)} live nodes')
    calls = []
    for a in nodes:
        if ram[a + 4] not in FLAG_STATES:
            raise ValueError(f'[8] {FLAG_OWNER:#x} node {a:#x} in state {ram[a + 4]}: its last 0019C6F0 call (key '
                             f'{FLAG_KEY:#x}) is not decided by the capture')
        if FLAG_STATES[ram[a + 4]]:
            calls.append(FLAG_STATES[ram[a + 4]])
    return tuple(calls)


def install(s=0):
    """Point the AREA13 lane's modules at AREA15 sub `s`, its capture group
    and the scratch tree TREE/part<s> (idempotent). Returns
    export_area01_common configured for it."""
    v = SUBS[s]
    _CURRENT_SUB[0] = s
    TARGET.sub = s
    A13.OUT = TREE / f'part{s}'
    TARGET.out = A13.OUT / f'sub{s}'
    A13.SCRATCH = SCRATCH / 'scratch'
    A13.capture_paths = capture_paths
    LV.PREVIOUS['area15'] = previous(s)
    LV.CREATURES['area15'] = None                  # no outdoor-creature twin in AREA15 (owner_matrix above)
    LV.FLAG_OWNERS['area15'] = FLAG_OWNER
    LV.flag_calls = flag_calls
    LV.owner_matrix = owner_matrix
    TB.PLACEMENTS['area15'] = v['placements']
    TB.GROUPS['area15'] = v['groups']
    TB.SPAWN_SUB0['area15'] = v['spawn']
    TB.DOOR_ROW['area15'] = DOOR_ROW
    TB.SCRIPT_ENTRIES['area15'] = SCRIPT_ENTRIES
    TB.WRITERS['area15'] = WRITERS
    TB.DOOR_BEHAVIOURS = DOOR_BEHAVIOURS
    TB.EXPLICIT_MODELS = {}
    C_ = A13.configure(TARGET)
    LV.install('area15')
    return C_


def uninstall():
    """Back to the AREA13 lane's state (its rules and behaviour tables)."""
    LV.flag_calls = _BASE_FLAG_CALLS
    LV.owner_matrix = _BASE_OWNER_MATRIX
    TB.DOOR_BEHAVIOURS = _BASE_DOOR_BEHAVIOURS
    TB.EXPLICIT_MODELS = _BASE_EXPLICIT_MODELS
    LV.install('area13')


install(0)
