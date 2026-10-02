#!/usr/bin/env python3
"""Export AREA13 sub 0's level over the later AREA13 captures (the second
load: a13b_00 .. a13b_04, a13b_s0 and the eleventh level's a13c_00 ..
a13c_06): geometry + textures + GS state codes (one zone EMDL, the static
bank id 0x44), the static bank image, the background, the grid collision,
the cell directory with its per-capture derivation and the render-context
block check (docs/AREA13X_ASSETS.md).

export_area13_level.export_target (the AREA13 lane's pipeline) is imported
and run UNCHANGED; export_area13x_common re-points it at the 13 captures
and at the scratch tree build/area13x/assets/tree/ and installs the two
rules the later captures need ([47]'s 0019C6F0 pairs with D_008107F4 bit
0x40 set, the knocked-drum orphan proof). export_area13x_split.py then
keeps what differs from assets/area13/ under assets/area13/reload/.

Output (ignored build/area13x/assets/tree/, disc-derived, never committed):
  sub0/level/00_id44.emdl (+ .gsmat.json), sub0/level/static_bank.emsc,
  sub0/level/level.json, sub0/background.embg, sub0/area13.emcl
  (+ scene.txt), sub0/area13_cells.bin, sub0/cells.json

Usage (port root, macOS arm64, pure Python):
  python3 tools/export_area13x_level.py [--skip-collision]
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area13x_common as X13  # noqa: E402  (first: re-points the AREA13 lane's AREA13 target)

A13, C, LV = X13.A13, X13.C, X13.LV


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=Path, default=X13.TREE)
    ap.add_argument('--iso', type=Path, default=C.ISO_PATH,
                    help="the user's disc image (the level-load GS upload is replayed from it)")
    ap.add_argument('--skip-collision', action='store_true')
    args = ap.parse_args(argv)
    out = args.out.resolve()
    X13.install()
    el = LV.L.load_export_level()
    elf = C.read_elf()
    A13.read_overlay(A13.AREA13)                 # pins the overlay
    X13.install()
    names = tuple(c.name for c in C.captures())
    if names != X13.CAPTURE_NAMES:
        raise SystemExit(f'AREA13 captures {names}, not the pinned {X13.CAPTURE_NAMES}')
    out.mkdir(parents=True, exist_ok=True)
    LV.export_target('area13', el, elf, out, args.iso.resolve(), args.skip_collision)
    return 0


if __name__ == '__main__':
    sys.exit(main())
