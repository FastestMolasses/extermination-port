#!/usr/bin/env python3
"""Shared inputs of the AREA19X asset exporters (docs/AREA19X_ASSETS.md):
AREA19 sub 0 over the twelfth level's a19b captures (entry 10).

The AREA19 lane (export_area19_common, docs/AREA19_ASSETS.md) exported
AREA19 sub 0 from its first load (a13_05 and the tenth level's a19_00 ..
a19_02), through the AREA13 lane's exporters (export_area13_common /
_level / _tables / _sfx). AREA19 was loaded a second time when the twelfth
level came down the hatch [63]'s ladder (TWELFTH_LEVEL_ROUTE.md, a19b_00 at
f176); the a19b group plays in that load. Its three captures are this
lane's (../Extermination/build/s87/route_a19b/, ignored):

  a19b_00_arrival, a19b_01_ledge, a19b_02_ladder1023   (area bytes 13 00 0A)

The a13d folders (route_a13d/, AREA13) are listed and excluded. Measured,
not assumed: all three hold the AREA19 module (MWo3 id 0x10) resident,
D_00810701 = 0 and the sub-0 nested block (export_area13_common.
loaded_sub_proof: 362 differing rows for sub 0 against 186,960 for sub 1),
so entry 10 is a sub-0 entry (AREA19_ASSETS.md had placed it behind sub 1);
D_0028A73C = 0x133C1C0, 0x1B80 above the first load's 0x133A640.

The AREA13 lane's modules are imported and run UNCHANGED (and through
export_area19_common, which is imported first and re-points them at
assets/area19/); `install()` re-points their AREA19 target once more and
adds the one rule the a19b captures need:
  * export_area13_common: AREA19.out -> TREE/sub0, OUT -> TREE, SCRATCH ->
    build/area19x/assets/scratch, capture_paths -> the a13d and a19b
    folders (all_captures keeps the three a19b captures);
  * export_area13_level.PREVIOUS['area19'] -> route_a13d/a13d_07_hatch63
    (the last capture before the second load);
  * export_area13_tables.explicit_model_nodes / explicit_model_problems ->
    the functions below: [43] 0x8255D0's model after counter 0x1D bit 0
    (it sets model 0x25 without changing +0x0D or the bone count).
The full export is written to the scratch tree; export_area19x_split.py
then keeps what differs from the first load's export (assets/area19/) under
assets/area19/reload/ (OUT here).

Nothing here is original data: every byte is read from the user's own files
(the pinned ELF and overlay, the extracted chunk23 files, the disc image)
and checked against the captures.
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area19_common as A19  # noqa: E402  (first: re-points the AREA13 lane's AREA19 target)
import export_area13_level as LV  # noqa: E402
import export_area13_tables as TB  # noqa: E402

A13, C = A19.A13, A19.C
L = LV.L
ROOT, DECOMP = A13.ROOT, A13.DECOMP
A19_TREE = A19.OUT                                 # the first load's export (assets/area19), read only
OUT = ROOT / 'assets/area19/reload'                # what differs from it (export_area19x_split.py)
SCRATCH = ROOT / 'build/area19x/assets'
TREE = SCRATCH / 'tree'                            # the full export over the a19b captures
ROUTE_A13D = DECOMP / 'build/s87/route_a13d'
ROUTE_A19B = DECOMP / 'build/s87/route_a19b'
PREVIOUS = ROUTE_A13D / 'a13d_07_hatch63'
FIRST_LAST = A19.ROUTE_A19 / 'a19_02_duct_back'    # the first load's last capture (its relocation words)
TARGET = A13.AREA19
# the AREA19 captures of the second load, in route order (each checked by
# all_captures: area byte 0x13, sub 0, overlay id 0x10 resident)
CAPTURE_NAMES = ('a19b_00_arrival', 'a19b_01_ledge', 'a19b_02_ladder1023')
EXCLUDED = ('a13d_00_stair', 'a13d_01_ladder405', 'a13d_02_block890', 'a13d_03_jump', 'a13d_04_ladder426',
            'a13d_05_ladder432', 'a13d_06_door20', 'a13d_07_hatch63')
CURSOR = 0x133C1C0                                 # D_0028A73C of the second load (measured)
FIRST_CURSOR = 0x133A640                           # the first load's (AREA19_ASSETS.md)
_LANE_EXPLICIT_NODES = TB.explicit_model_nodes
_LANE_EXPLICIT_PROBLEMS = TB.explicit_model_problems
# [43] 0x8255D0 (func_overlay_AREA19_00825590.c, byte-identical): its state
# 0 with D_008107F5 bit 0 set sets +0x0D = 0x25 before the spawn binding
# (state 2); without it, state 1, and once the bit is set state 1 sets
# func_001CA6E0(self, func_001C6120(D_0028A59C, 0x25)) and func_001C62C0
# (bone_init_default_1.c) without changing +0x0D, and goes to state 2. So
# a node in state 2 whose +0x0D is not 0x25 carries model 0x25. 001CA6E0
# (func_001CA5E0: +0x44 and the +0x4C handler) leaves the bone count +0x0C,
# and 001C62C0 walks that count over the new model's 0x50-byte rest records
# (model + model[+0xC]): +0x0C stays the +8 of the spawn binding's model
# 001C6120(table, +0x0D).
SWAP_OWNER, SWAP_MODEL = 0x8255D0, 0x25


def capture_paths():
    """The a13d and a19b capture folders in route order."""
    out = []
    for route, prefix in ((ROUTE_A13D, 'a13d_*'), (ROUTE_A19B, 'a19b_*')):
        out += sorted(p for p in route.glob(prefix) if (p / 'eeMemory.bin').exists())
    order = {n: i for i, n in enumerate(EXCLUDED + CAPTURE_NAMES)}
    return sorted(out, key=lambda p: (order.get(p.name, len(order)), p.name))


def model_address(ram, ident):
    """001C6120(*D_0028A59C, ident): table + (word[1 + ident] >> 2 << 2)."""
    table = C.u32(ram, 0x28A59C)
    return table + (C.s32(ram, table + 4 + 4 * ident) >> 2 << 2)


def swap_nodes(ram):
    """[(slot, node)] of [43] nodes in state 2 with +0x0D != 0x25 (the state-1
    path), in an AREA19 sub-0 capture."""
    if ram[0x810700] != TARGET.area or ram[0x810701] != TARGET.sub:
        return []
    return [(s, a) for s, a in TB.E02._POOL_NODES(ram)
            if C.u32(ram, a + 0x10) == SWAP_OWNER and ram[a + 4] == 2 and ram[a + 0x0D] != SWAP_MODEL]


def bone_rest_problems(ram, node, model):
    """The words bone_init_default_1 writes into each of the node's +0x0C bone
    nodes from `model`'s rest records (+0x64 the record's +4 halfword,
    +0x88/+0x8A/+0x8C 0x1000, +0x70..+0x87 zero, +0x00..+0x3F the record's
    +0x10..+0x4F), compared with RAM. [] when every bone holds them."""
    out = []
    rest = model + C.u32(ram, model + 0x0C)
    for k in range(ram[node + 0x0C]):
        bone, rec = C.u32(ram, node + 0x110 + 4 * k), rest + 0x50 * k
        if not bone:
            out.append(f'bone {k}: no slot')
            continue
        if ram[bone + 0x64:bone + 0x66] != ram[rec + 4:rec + 6] or \
                ram[bone + 0x88:bone + 0x8E] != struct.pack('<3H', 0x1000, 0x1000, 0x1000) or \
                any(ram[bone + 0x70:bone + 0x88]) or ram[bone:bone + 0x40] != ram[rec + 0x10:rec + 0x50]:
            out.append(f'bone {k}: not the rest record {rec:#x}')
    return out


def explicit_model_nodes(ram):
    """export_area13_tables.explicit_model_nodes plus [43] after its swap
    (swap_nodes): the model owners whose +0x44 is not 001C6120(table,
    +0x0D)."""
    return list(_LANE_EXPLICIT_NODES(ram)) + swap_nodes(ram)      # disjoint: two behaviours


def explicit_model_problems(ram, name):
    """export_area13_tables.explicit_model_problems for the AREA13 lane's
    nodes, and for [43] after its swap: +0x44 = 001C6120(table, 0x25), +0x0C
    = the +8 of 001C6120(table, +0x0D), every bone slot set, and every bone
    holding bone_init_default_1's words from model 0x25's rest records."""
    saved, TB.explicit_model_nodes = TB.explicit_model_nodes, _LANE_EXPLICIT_NODES
    try:
        out = _LANE_EXPLICIT_PROBLEMS(ram, name)
    finally:
        TB.explicit_model_nodes = saved
    for slot, a in swap_nodes(ram):
        model = model_address(ram, SWAP_MODEL)
        if C.u32(ram, a + 0x44) != model:
            out.append(f'{name} slot {slot}: +0x44 != 001C6120(table, {SWAP_MODEL:#x})')
        elif ram[a + 0x0C] != C.u32(ram, model_address(ram, ram[a + 0x0D]) + 8):
            out.append(f'{name} slot {slot}: bone count (the spawn binding\'s)')
        else:
            out += [f'{name} slot {slot}: {p}' for p in bone_rest_problems(ram, a, model)]
    return out


def install():
    """Re-point the AREA13 lane's AREA19 target at the a19b captures and the
    scratch tree (idempotent). Returns export_area01_common configured for
    AREA19."""
    A19.install()                                  # the first lane's re-pointing, then ours over it
    TARGET.out = TREE / 'sub0'
    A13.OUT = TREE
    A13.SCRATCH = SCRATCH / 'scratch'
    A13.capture_paths = capture_paths
    LV.PREVIOUS['area19'] = PREVIOUS
    TB.explicit_model_nodes = explicit_model_nodes
    TB.explicit_model_problems = explicit_model_problems
    C_ = A13.configure(TARGET)
    LV.install('area19')
    return C_


install()
