#!/usr/bin/env python3
"""Export AREA19 sub 1's level over the a19c captures (a19c_06, a19c_07):
geometry + textures + GS state codes (one zone EMDL, the static bank id
0x44), the static bank image, the grid collision, the cell directory with
its per-capture derivation, the render-context block check and the
loaded-sub proof (docs/AREA19S1_ASSETS.md).

export_area13_level.export_target (the AREA13 lane's AREA19 path) and
export_area19_level.sub_proof are imported and run UNCHANGED;
export_area19s1_common re-points them at AREA19 sub 1, the two captures,
sub 1's 0019C6F0 callers / 001A2370 owners and the scratch tree
build/area19s1/assets/tree/ (D_0028A5A4's previous capture: a19c_05).
export_area19s1_split.py then places the tree under assets/area19s1/.

Output (ignored build/area19s1/assets/tree/, disc-derived, never committed):
  sub1/level/00_id44.emdl (+ .gsmat.json), sub1/level/static_bank.emsc,
  sub1/level/level.json, sub1/area19.emcl (+ scene.txt),
  sub1/area19_cells.bin, sub1/cells.json, loaded_sub_proof.json

Usage (port root, macOS arm64, pure Python):
  python3 tools/export_area19s1_level.py [--skip-collision]
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area19s1_common as S1  # noqa: E402  (first: re-points the AREA13 lane's AREA19 target at sub 1)
import export_area19_level as A19L  # noqa: E402  (sub_proof)

A13, C, LV = S1.A13, S1.C, S1.LV


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=Path, default=S1.TREE)
    ap.add_argument('--iso', type=Path, default=C.ISO_PATH,
                    help="the user's disc image (the level-load GS upload is replayed from it)")
    ap.add_argument('--skip-collision', action='store_true')
    args = ap.parse_args(argv)
    out = args.out.resolve()
    S1.install()
    el = LV.L.load_export_level()
    elf = C.read_elf()
    A13.read_overlay(S1.TARGET)                  # pins the overlay
    S1.install()
    caps = C.captures()
    names = tuple(c.name for c in caps)
    if names != S1.CAPTURE_NAMES:
        raise SystemExit(f'AREA19 sub-1 captures {names}, not the pinned {S1.CAPTURE_NAMES}')
    out.mkdir(parents=True, exist_ok=True)
    (out / 'loaded_sub_proof.json').write_text(json.dumps(A19L.sub_proof(caps), indent=1) + '\n')
    LV.export_target('area19', el, elf, out, args.iso.resolve(), args.skip_collision)
    return 0


if __name__ == '__main__':
    sys.exit(main())
