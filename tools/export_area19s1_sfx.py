#!/usr/bin/env python3
"""Export AREA19 sub 1's sound container and registry over the a19c
captures (a19c_06, a19c_07) (docs/AREA19S1_ASSETS.md).

export_area13_sfx.export_target (the AREA13 lane's AREA19 path) is imported
and run UNCHANGED; export_area19s1_common re-points it at AREA19 sub 1 (its
container is table entry 0 of the nested block chunk23.n1, read by
container_block through the capture's own sub), the two captures and the
scratch tree. Every capture must bind the same slots
(bindings_from_captures refuses a disagreement).

Output (ignored build/area19s1/assets/tree/sub1/sfx/, disc-derived, never
committed): area19_banks.bin, banks.json, sfx_registry.emsr (+ .json)

Usage (port root, macOS arm64, pure Python): python3 tools/export_area19s1_sfx.py
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area19s1_common as S1  # noqa: E402  (first: re-points the AREA13 lane's AREA19 target at sub 1)
import export_area13_sfx as SX  # noqa: E402


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=Path, default=S1.TREE)
    args = ap.parse_args(argv)
    S1.install()
    SX.X.Elf()                                   # pins the boot ELF
    summary = SX.export_target('area19', args.out.resolve())
    if tuple(summary['captures']) != S1.CAPTURE_NAMES or summary['sub'] != S1.SUB:
        raise SystemExit(f'AREA19 sub {summary["sub"]} captures {summary["captures"]}, not sub 1 {S1.CAPTURE_NAMES}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
