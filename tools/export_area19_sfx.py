#!/usr/bin/env python3
"""Export AREA19 sub 0's sound container and registry over every recorded
AREA19 capture (a13_05 and the tenth level's a19_00 .. a19_02)
(docs/AREA19_ASSETS.md).

export_area13_sfx.export_target (the AREA13 lane's AREA19 path: table
entry 0 of the nested block, which 001FFCD0 state 8 hands to the IOP
stream loader, the bindings from each capture's D_00281D50 / D_0027C6C0,
and the AREA01 registry export) is imported and run UNCHANGED;
export_area19_common re-points it at assets/area19/ and at the four
captures. Every capture must bind the same slots (bindings_from_captures
refuses a disagreement).

Output (ignored assets/area19/sub0/sfx/, disc-derived, never committed):
  area19_banks.bin, banks.json, sfx_registry.emsr (+ .json)

Usage (port root, macOS arm64, pure Python): python3 tools/export_area19_sfx.py
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area19_common as A19  # noqa: E402  (first: re-points the AREA13 lane's AREA19 target)
import export_area13_sfx as SX  # noqa: E402


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=Path, default=A19.OUT)
    args = ap.parse_args(argv)
    A19.install()
    SX.X.Elf()                                   # pins the boot ELF
    summary = SX.export_target('area19', args.out.resolve())
    if tuple(summary['captures']) != A19.CAPTURE_NAMES:
        raise SystemExit(f'AREA19 captures {summary["captures"]}, not the pinned {A19.CAPTURE_NAMES}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
