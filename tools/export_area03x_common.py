#!/usr/bin/env python3
"""AREA03 subs 1 and 2 and AREA04 sub 1 as the sixteenth level loads them
(docs/SIXTEENTH_LEVEL_ROUTE.md, docs/AREA03X_PORT.md).

The AREA13 lane's exporters (export_area13_common / _level / _tables / _sfx)
model an area through a Target object and per-target tables keyed by its
name. They are imported and run UNCHANGED; `install(key)` registers a Target
for the sub and points every per-target table at it, as export_area15_common
does for AREA15. Each key's capture list is the fork recording's folders that
end in that sub, in route order, all loaded with one load map:

  03_1  AREA03 sub 1: a03b_00 .. a03b_04 and the second visit a03c_00 .. a03c_02
  03_2  AREA03 sub 2: a03b_05 (a08_06's second load holds another stale
        D_0028A5A4 word and the same map: see STALE)
  04_1  AREA04 sub 1: a03c_03, a04c_00, a04c_01

Rules, each read from the code:
  * cell-hull owners (001A2370's callers): the pickup and the drum
    (export_area13_level.owner_matrix), AREA03's 0x823930 with
    *(node + 0x11C) + 0x90 (func_overlay_AREA03_008238F0.c: only sub 0 places
    it), and AREA04's three 0x825510, 0x825B00, 0x8260C0 with node + 0xD0
    (export_area04_level.HULL_OWNERS);
  * no AREA03 C calls 0019C6F0 (export_area03_common.source_rules); AREA04's
    0019C6F0 caller is its door [45] 0x823700 in sub 0's placements only, not
    in sub 1's table;
  * the door behaviours: the four boot doors; AREA04's 0x823700 has no sub-1
    placement.

Output: export_area03x_level / _tables / _sfx write into
build/area03x/tree/<key>/; export_area03x_split.py places sub<n>/ under
assets/levels/03/ and assets/area04/ and refuses a side file (spawn table,
door row, scripts, overlay data) that differs from the existing export.

Nothing here is original data: every byte is read from the user's own files
and checked against the captures.
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area15_level as A15L  # noqa: E402  (first: export_area13_common keeps AREA01's nested builder)
import reference_root as refs  # noqa: E402

A15 = A15L.A15
A13, LV, TB, SX = A15.A13, A15.LV, A15.TB, A15.SX
C, L = A13.C, LV.L
ROOT, DECOMP = A13.ROOT, A13.DECOMP
SCRATCH = ROOT / 'build/area03x/assets'
TREE = SCRATCH / 'tree'
SET = 'levels-recordings-20261030-1/s87'
# the module's own tables, pinned by export_area13_tables' walks
AREA03 = dict(name='area03', area=3, overlay='AREA03.BIN', size=0x5C00, id=4, chunk='chunk07', label='AREA03',
              sha256='ea5cba26e5ea53bd0809177fb04d0ea549fa02c2bb01d86108a1ab1ce02ca361',
              door_row=0x24DFD0, scripts=(0x827170, 0x827630, 0x8282D0, 0x828BD0, 0x828CD0))
AREA04 = dict(name='area04', area=4, overlay='AREA04.BIN', size=0x9400, id=5, chunk='chunk08', label='AREA04',
              sha256='49cd7be912804c00bf11241ee3cf020690677fac2451d990fb30687b5d15f771',
              door_row=0x24E000, scripts=(0x8272A0, 0x8275A0, 0x8278D0, 0x827D10, 0x827D90, 0x8280D0, 0x828450,
                                          0x8286D0, 0x828BE0, 0x829020, 0x8293A0, 0x829570, 0x829930, 0x829CF0,
                                          0x82BD60, 0x82C060, 0x82C120, 0x82C1F0, 0x82C3B0, 0x82C650))
KEYS = {
    '03_1': dict(AREA03, sub=1, placements=0x827B60, groups=((0x826BC0, 23),), spawn=(0x24BA10, 8),
                 captures=(('route_a03b', 'a03b_00_door13'), ('route_a03b', 'a03b_01_duct_west'),
                           ('route_a03b', 'a03b_02_duct_east'), ('route_a03b', 'a03b_03_item2a'),
                           ('route_a03b', 'a03b_04_back'), ('route_a03c', 'a03c_00_slider'),
                           ('route_a03c', 'a03c_01_duct'), ('route_a03c', 'a03c_02_seal')),
                 previous=None, dynamic=True),
    '03_2': dict(AREA03, sub=2, placements=0x828090, groups=((0x8270F0, 1),), spawn=(0x24BB90, 2),
                 captures=(('route_a03b', 'a03b_05_slider'),),
                 previous=('route_a03b', 'a03b_04_back'), dynamic=False),
    '04_1': dict(AREA04, sub=1, placements=0x82AF70, groups=((0x826DE0, 24),), spawn=(0x24BE60, 13),
                 captures=(('route_a03c', 'a03c_03_door24'), ('route_a04c', 'a04c_00_event'),
                           ('route_a04c', 'a04c_01_door73')),
                 previous=None, dynamic=True),
}
TURRET = 0x823930                                  # AREA03 sub 0's, *(node + 0x11C) + 0x90
A04_HULLS = (0x825510, 0x825B00, 0x8260C0)         # node + 0xD0
DOORS = (0x1BC350, 0x1BB860, 0x1BD9F0, 0x1BD560)
_CURRENT = [None]


def key():
    return _CURRENT[0]


def folder(pair):
    return refs.capture(f'{SET}/{pair[0]}/{pair[1]}')


def capture_paths(k=None):
    return [folder(p) for p in KEYS[k or key()]['captures']]


def owner_matrix(ram, node):
    behaviour = C.u32(ram, node + 0x10)
    if behaviour in (LV.PICKUP, LV.DRUM):
        return node + 0xD0
    v = KEYS[key()]
    if v['area'] == 3 and behaviour == TURRET:
        return C.u32(ram, node + 0x11C) + 0x90
    if v['area'] == 4 and behaviour in A04_HULLS:
        return node + 0xD0
    return None


def flag_calls(ram):
    return ()


# 001D40E0 and 001D4440 store float_to_int(128 * colour) into these three
# words and read them back at once in the same call (src/func_001D40E0.c,
# src/func_001D4440.c): they hold the last draw's colour, 1.0 in every capture
# of this set, and no reader of the exported block sees them before a store.
COLOUR_WORDS = (0x251BE0, 0x251C20)
_CHECK_CTX_BLOCK = LV.E22.check_ctx_block


def check_ctx_block(caps, elf):
    """The shared room checks, with the two colour windows' run-time words."""
    import export_render_context as ERC
    original = ERC.verify

    def verify(blocks, ram):
        expected = []
        for address, data in blocks:
            data = bytearray(data)
            for w in COLOUR_WORDS:
                if address <= w and w + 12 <= address + len(data):
                    if ram[w:w + 12] != bytes(data[w - address:w - address + 12]) and \
                            ram[w:w + 12] != (0x80).to_bytes(4, 'little') * 3:
                        raise ValueError(f'{w:#x}: neither the boot bytes nor 001D40E0 / 001D4440 of colour 1.0')
                    data[w - address:w - address + 12] = ram[w:w + 12]
            expected.append((address, data))
        return original(expected, ram)

    ERC.verify = verify
    try:
        return _CHECK_CTX_BLOCK(caps, elf)
    finally:
        ERC.verify = original


def install(k):
    """Point the AREA13 lane's modules at sub `k` and its part of the tree.
    Returns export_area01_common configured for it."""
    v = KEYS[k]
    _CURRENT[0] = k
    t = A13.Target(v['name'], v['area'], v['sub'], v['overlay'], v['size'], v['id'], v['sha256'], v['chunk'], True,
                   TREE / k / f"sub{v['sub']}", v['label'])
    A13.TARGETS[v['name']] = t
    A13.OUT = TREE / k
    A13.SCRATCH = SCRATCH / 'scratch' / k
    A13.capture_paths = capture_paths
    LV.PREVIOUS[v['name']] = folder(v['previous']) if v['previous'] else ROOT / 'no-previous-capture'
    LV.FLAG_OWNERS[v['name']] = None
    LV.flag_calls = flag_calls
    LV.owner_matrix = owner_matrix
    LV.E22.check_ctx_block = check_ctx_block
    TB.PLACEMENTS[v['name']] = v['placements']
    TB.GROUPS[v['name']] = v['groups']
    TB.SPAWN_SUB0[v['name']] = v['spawn']
    TB.DOOR_ROW[v['name']] = v['door_row']
    TB.SCRIPT_ENTRIES[v['name']] = v['scripts']
    TB.WRITERS[v['name']] = ()
    TB.DOOR_BEHAVIOURS = DOORS
    TB.EXPLICIT_MODELS = {}
    common = A13.configure(t)
    LV.install(t)
    return common
