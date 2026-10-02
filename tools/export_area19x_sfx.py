#!/usr/bin/env python3
"""Export AREA19 sub 0's sound container and registry over the a19b
captures (the second AREA19 load, entry 10) (docs/AREA19X_ASSETS.md).

export_area13_sfx.export_target (the AREA13 lane's AREA19 path) is imported
and run UNCHANGED; export_area19x_common re-points it at the three captures
and the scratch tree. Every capture must bind the same slots
(bindings_from_captures refuses a disagreement).

Output (ignored build/area19x/assets/tree/sub0/sfx/, disc-derived, never
committed): area19_banks.bin, banks.json, sfx_registry.emsr (+ .json)

Usage (port root, macOS arm64, pure Python): python3 tools/export_area19x_sfx.py
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area19x_common as X19  # noqa: E402  (first: re-points the AREA13 lane's AREA19 target)
import export_area13_sfx as SX  # noqa: E402


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=Path, default=X19.TREE)
    args = ap.parse_args(argv)
    X19.install()
    SX.X.Elf()                                   # pins the boot ELF
    summary = SX.export_target('area19', args.out.resolve())
    if tuple(summary['captures']) != X19.CAPTURE_NAMES:
        raise SystemExit(f'AREA19 captures {summary["captures"]}, not the pinned {X19.CAPTURE_NAMES}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
