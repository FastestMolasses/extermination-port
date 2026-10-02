#!/usr/bin/env python3
"""Export AREA13 sub 0's tables over the later AREA13 captures (a13b_00 ..
a13b_04, a13b_s0, a13c_00 .. a13c_06): the roster, the spawn table, the
door destinations, the overlay scripts and data section, the message
records + area bank and the world model bank (docs/AREA13X_ASSETS.md).

export_area13_tables.export_target (the AREA13 lane's walks and rules) is
imported and run UNCHANGED; export_area13x_common re-points it at the 13
captures and at the scratch tree build/area13x/assets/tree/.

Output (ignored build/area13x/assets/tree/, disc-derived, never committed):
  sub0/roster.emro, sub0/message_data.emmd, sub0/world_models.emwm (+ .json)
  spawn_table.emsp, door_destinations.emsp, scripts.emsc, overlay_data.emsc,
  tables.json

Usage (port root, macOS arm64, pure Python): python3 tools/export_area13x_tables.py
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area13x_common as X13  # noqa: E402  (first: re-points the AREA13 lane's AREA13 target)
import export_area13_tables as TB  # noqa: E402


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=Path, default=X13.TREE)
    args = ap.parse_args(argv)
    X13.install()
    report = TB.export_target('area13', args.out.resolve())
    if tuple(report['captures']) != X13.CAPTURE_NAMES:
        raise SystemExit(f'AREA13 captures {report["captures"]}, not the pinned {X13.CAPTURE_NAMES}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
