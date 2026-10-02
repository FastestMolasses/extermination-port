#!/usr/bin/env python3
"""Export AREA19 sub 0's tables over every recorded AREA19 capture (a13_05
and the tenth level's a19_00 .. a19_02): the roster, the spawn table, the
door destinations, the overlay scripts and data section, the message
records + area bank and the world model bank (docs/AREA19_ASSETS.md).

export_area13_tables.export_target (the AREA13 lane's AREA19 path: the
AREA01 / AREA02 / AREA11 walks, the chains the committed AREA19 overlay C
starts, the flame [11]'s overlay-data word by its C, the door commit set,
the spawn rule D_008106C8 = +0x1C of record D_00810702) is imported and run
UNCHANGED; export_area19_common re-points it at assets/area19/ and at the
four captures. a19_00 and a19_01 hold spawn entry 8 (the duct moves
D_00810702 from 9 to 8); records 7, 8 and 9 carry the same +0x1C word.

Output (ignored assets/area19/, disc-derived, never committed):
  sub0/roster.emro, sub0/message_data.emmd, sub0/world_models.emwm (+ .json)
  spawn_table.emsp, door_destinations.emsp, scripts.emsc, overlay_data.emsc,
  tables.json

Usage (port root, macOS arm64, pure Python): python3 tools/export_area19_tables.py
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area19_common as A19  # noqa: E402  (first: re-points the AREA13 lane's AREA19 target)
import export_area13_tables as TB  # noqa: E402


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=Path, default=A19.OUT)
    args = ap.parse_args(argv)
    A19.install()
    report = TB.export_target('area19', args.out.resolve())
    if tuple(report['captures']) != A19.CAPTURE_NAMES:
        raise SystemExit(f'AREA19 captures {report["captures"]}, not the pinned {A19.CAPTURE_NAMES}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
