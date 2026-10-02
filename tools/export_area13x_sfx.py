#!/usr/bin/env python3
"""Export AREA13 sub 0's sound container and registry over the later
AREA13 captures (a13b_00 .. a13b_04, a13b_s0, a13c_00 .. a13c_06)
(docs/AREA13X_ASSETS.md).

export_area13_sfx.export_target (the AREA13 lane's container block, the
bindings from each capture's D_00281D50 / D_0027C6C0, the registry export)
is imported and run UNCHANGED; export_area13x_common re-points it at the 13
captures and at the scratch tree build/area13x/assets/tree/. Every capture
must bind the same slots (bindings_from_captures refuses a disagreement).

Output (ignored build/area13x/assets/tree/sub0/sfx/, disc-derived, never
committed): area13_banks.bin, banks.json, sfx_registry.emsr (+ .json)

Usage (port root, macOS arm64, pure Python): python3 tools/export_area13x_sfx.py
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area13x_common as X13  # noqa: E402  (first: re-points the AREA13 lane's AREA13 target)
import export_area13_sfx as SX  # noqa: E402


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=Path, default=X13.TREE)
    args = ap.parse_args(argv)
    X13.install()
    SX.X.Elf()                                   # pins the boot ELF
    summary = SX.export_target('area13', args.out.resolve())
    if tuple(summary['captures']) != X13.CAPTURE_NAMES:
        raise SystemExit(f'AREA13 captures {summary["captures"]}, not the pinned {X13.CAPTURE_NAMES}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
