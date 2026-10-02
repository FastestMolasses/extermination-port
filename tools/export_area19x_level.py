#!/usr/bin/env python3
"""Export AREA19 sub 0's level over the a19b captures (the second AREA19
load, entry 10: a19b_00 .. a19b_02): geometry + textures + GS state codes
(one zone EMDL, the static bank id 0x44), the static bank image, the grid
collision, the cell directory with its per-capture derivation, the
render-context block check and the loaded-sub proof (docs/AREA19X_ASSETS.md).

export_area13_level.export_target (the AREA13 lane's AREA19 path) and
export_area19_level.sub_proof are imported and run UNCHANGED;
export_area19x_common re-points them at the three captures and at the
scratch tree build/area19x/assets/tree/ (D_0028A5A4's previous capture:
a13d_07). export_area19x_split.py then keeps what differs from
assets/area19/ under assets/area19/reload/.

Output (ignored build/area19x/assets/tree/, disc-derived, never committed):
  sub0/level/00_id44.emdl (+ .gsmat.json), sub0/level/static_bank.emsc,
  sub0/level/level.json, sub0/area19.emcl (+ scene.txt),
  sub0/area19_cells.bin, sub0/cells.json, loaded_sub_proof.json

Usage (port root, macOS arm64, pure Python):
  python3 tools/export_area19x_level.py [--skip-collision]
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area19x_common as X19  # noqa: E402  (first: re-points the AREA13 lane's AREA19 target)
import export_area19_level as A19L  # noqa: E402  (sub_proof)

A13, C, LV = X19.A13, X19.C, X19.LV


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=Path, default=X19.TREE)
    ap.add_argument('--iso', type=Path, default=C.ISO_PATH,
                    help="the user's disc image (the level-load GS upload is replayed from it)")
    ap.add_argument('--skip-collision', action='store_true')
    args = ap.parse_args(argv)
    out = args.out.resolve()
    X19.install()
    el = LV.L.load_export_level()
    elf = C.read_elf()
    A13.read_overlay(X19.TARGET)                 # pins the overlay
    X19.install()
    caps = C.captures()
    names = tuple(c.name for c in caps)
    if names != X19.CAPTURE_NAMES:
        raise SystemExit(f'AREA19 captures {names}, not the pinned {X19.CAPTURE_NAMES}')
    out.mkdir(parents=True, exist_ok=True)
    (out / 'loaded_sub_proof.json').write_text(json.dumps(A19L.sub_proof(caps), indent=1) + '\n')
    LV.export_target('area19', el, elf, out, args.iso.resolve(), args.skip_collision)
    return 0


if __name__ == '__main__':
    sys.exit(main())
