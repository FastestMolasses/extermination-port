#!/usr/bin/env python3
"""Shared inputs of the AREA19 asset exporters (docs/AREA19_ASSETS.md).

One target, the only (area, sub) the recorded AREA19 captures load:
AREA19 (area 0x13) sub 0, OVERLAY/AREA19.BIN = MWo3 id 0x10 at 0x823500.
Its captures (../Extermination/build/s87/, ignored):
  * route_a13/a13_05_shaft: the arrival at the foot of the entry-9 ladder
    (NINTH_LEVEL_ROUTE.md; area bytes 13 00 09);
  * route_a19/a19_00_duct, a19_01_pickup_g2, a19_02_duct_back: the tenth
    level's AREA19 beats (TENTH_LEVEL_ROUTE.md; area bytes 13 00 08,
    13 00 08, 13 00 09).
Every other capture folder of the a13 / a19 / a13b groups ends in another
area (AREA13, or AREA04 for a13b_05) and is listed as excluded. a13b_00
starts in AREA19 but its recorded RAM is the AREA13 arrival.

The AREA13 lane's exporters (export_area13_common / _level / _tables /
_sfx) already model AREA19 sub 0 for a13_05 alone, including the AREA19
load layout (a top block with a group-A section and a resident offset plus
nested per-sub blocks; export_area13_common.build_load_map_both, the
resident-relative label rule of AREA06 / AREA22). They are imported and
run UNCHANGED; this module only re-points their target:
  * export_area13_common.AREA19.out -> assets/area19/sub0 and
    export_area13_common.OUT -> assets/area19 (the exporters write to
    out_root / t.out.relative_to(OUT));
  * export_area13_common.capture_paths -> the a13, a19 and a13b capture
    folders in route order (all_captures keeps those whose area byte is
    0x13 and refuses one with another sub, so the four above);
  * export_area13_common.SCRATCH -> build/area19/assets.
The AREA13 target keeps its own (old) paths but is never run by this lane;
a process that imports this module must not export or check AREA13.

Nothing here is original data: every byte is read from the user's own
files (the pinned boot ELF and overlay, the extracted chunk23 files, the
disc image) and checked against the captures.
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area13_common as A13  # noqa: E402  (first: it keeps AREA01's nested load-map builder)

C = A13.C
ROOT, DECOMP = A13.ROOT, A13.DECOMP
OUT = ROOT / 'assets/area19'
SCRATCH = ROOT / 'build/area19/assets'
ROUTE_A13, ROUTE_A19, ROUTE_A13B = A13.ROUTE_A13, DECOMP / 'build/s87/route_a19', DECOMP / 'build/s87/route_a13b'
ARRIVAL = ROUTE_A13 / 'a13_05_shaft'
# the AREA19 captures in route order (each folder's eeMemory.bin; checked by
# all_captures: area byte 0x13, sub 0, overlay id 0x10 resident)
CAPTURE_NAMES = ('a13_05_shaft', 'a19_00_duct', 'a19_01_pickup_g2', 'a19_02_duct_back')
TARGET = A13.AREA19


def capture_paths():
    """Every a13 / a19 / a13b capture folder in route order (the AREA04
    arrival of the a13 group first, as export_area13_common lists it)."""
    out = [A13.ARRIVAL]
    for route, prefix in ((ROUTE_A13, 'a13_*'), (ROUTE_A19, 'a19_*'), (ROUTE_A13B, 'a13b_*')):
        out += sorted(p for p in route.glob(prefix) if (p / 'eeMemory.bin').exists())
    return out


def install():
    """Re-point export_area13_common at this lane's tree and captures
    (idempotent). Returns export_area01_common configured for AREA19."""
    TARGET.out = OUT / 'sub0'
    A13.OUT = OUT
    A13.SCRATCH = SCRATCH
    A13.capture_paths = capture_paths
    return A13.configure(TARGET)


install()
