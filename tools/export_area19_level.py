#!/usr/bin/env python3
"""Export AREA19 sub 0's level over every recorded AREA19 capture (a13_05
and the tenth level's a19_00 .. a19_02): geometry + textures + GS state
codes (one zone EMDL, the static bank id 0x44), the static bank image, the
grid collision, the cell directory with its per-capture derivation, the
render-context block check and the loaded-sub proof (docs/AREA19_ASSETS.md).

export_area13_level.export_target (the AREA13 lane's AREA19 path, which is
export_area01_level's pipeline plus the AREA02 / AREA06 / AREA22 original-
code derivations) is imported and run UNCHANGED; export_area19_common
re-points its target at assets/area19/ and at the four captures. What it
checks per capture is listed in AREA13_ASSETS.md ("How each asset is
derived and checked"); every check runs over all four captures here.

Output (ignored assets/area19/, disc-derived, never committed):
  sub0/level/00_id44.emdl (+ .gsmat.json)   geometry, textures, codes
  sub0/level/static_bank.emsc               *D_0028A5A0
  sub0/level/level.json                     counts, hashes, verification
  sub0/area19.emcl (+ scene.txt)            grid collision
  sub0/area19_cells.bin                     the cell directory (disc bytes)
  sub0/cells.json                           the directory verification
  loaded_sub_proof.json                     per capture, rows of each nested map

Usage (port root, macOS arm64, pure Python):
  python3 tools/export_area19_level.py [--skip-collision]
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area19_common as A19  # noqa: E402  (first: re-points the AREA13 lane's AREA19 target)
import export_area13_level as LV  # noqa: E402

A13, C = A19.A13, A19.C


def install():
    A19.install()
    LV.install('area19')
    return C


def sub_proof(caps):
    """export_area13_common.loaded_sub_proof, required to favour each
    capture's own sub by a factor over 100 (the AREA13 lane's rule)."""
    proof = A13.loaded_sub_proof(caps)
    for cap in caps:
        rows, sub = proof[cap.name], cap.ram[0x810701]
        if not all(rows[sub] * 100 < rows[s] for s in rows if s != sub):
            raise SystemExit(f'{cap.name}: its sub-{sub} map does not fit RAM far better ({rows})')
    return {k: {f'sub{s}': v for s, v in r.items()} for k, r in proof.items()}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=Path, default=A19.OUT)
    ap.add_argument('--iso', type=Path, default=C.ISO_PATH,
                    help="the user's disc image (the level-load GS upload is replayed from it)")
    ap.add_argument('--skip-collision', action='store_true')
    args = ap.parse_args(argv)
    out = args.out.resolve()
    install()
    el = LV.L.load_export_level()
    elf = C.read_elf()
    A13.read_overlay(A19.TARGET)                 # pins the overlay
    install()
    caps = C.captures()
    names = tuple(c.name for c in caps)
    if names != A19.CAPTURE_NAMES:
        raise SystemExit(f'AREA19 captures {names}, not the pinned {A19.CAPTURE_NAMES}')
    out.mkdir(parents=True, exist_ok=True)
    (out / 'loaded_sub_proof.json').write_text(json.dumps(sub_proof(caps), indent=1) + '\n')
    LV.export_target('area19', el, elf, out, args.iso.resolve(), args.skip_collision)
    return 0


if __name__ == '__main__':
    sys.exit(main())
