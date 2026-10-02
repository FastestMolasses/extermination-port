#!/usr/bin/env python3
"""Export AREA19 sub 1's tables over the a19c captures (a19c_06, a19c_07):
the roster, the spawn table, the door destinations, the overlay scripts
and data section, the message records + area bank and the world model
bank (docs/AREA19S1_ASSETS.md).

export_area13_tables.export_target (the AREA13 lane's AREA19 path) is
imported and run UNCHANGED; export_area19s1_common re-points it at AREA19
sub 1 (its roster and spawn pins, no run-time overlay-data writer), the two
captures and the scratch tree.

Output (ignored build/area19s1/assets/tree/, disc-derived, never committed):
  sub1/roster.emro, sub1/message_data.emmd, sub1/world_models.emwm (+ .json)
  spawn_table.emsp, door_destinations.emsp, scripts.emsc, overlay_data.emsc,
  tables.json

Usage (port root, macOS arm64, pure Python): python3 tools/export_area19s1_tables.py
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area19s1_common as S1  # noqa: E402  (first: re-points the AREA13 lane's AREA19 target at sub 1)

TB = S1.TB


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=Path, default=S1.TREE)
    args = ap.parse_args(argv)
    S1.install()
    report = TB.export_target('area19', args.out.resolve())
    if tuple(report['captures']) != S1.CAPTURE_NAMES or report['roster']['sub'] != S1.SUB:
        raise SystemExit(f'AREA19 sub {report["roster"]["sub"]} captures {report["captures"]}, not sub 1 '
                         f'{S1.CAPTURE_NAMES}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
