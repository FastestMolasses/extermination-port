#!/usr/bin/env python3
"""Export AREA15's tables for sub 0 (a19d_20) and sub 1 (a15_01): the
roster, the spawn table, the door destinations, the overlay scripts and
data section, the message records + area bank and the world model bank
(docs/AREA15_ASSETS.md).

export_area13_tables.export_target (the AREA13 lane's path: the AREA01 /
AREA02 / AREA11 walks, the chains the committed overlay C starts, the door
commit set, the spawn rule D_008106C8 = +0x1C of record D_00810702) is
imported and run UNCHANGED, once per sub, with export_area15_common's
AREA15 target and per-sub pins installed. Sub 0's capture holds four
rewritten overlay-data words (0x827060 .. 0x82706C), all inside records a
chain reaches; AREA15 has no other run-time writer window (WRITERS empty).

Output (ignored build/area15/assets/tree/part<sub>/, disc-derived, never
committed; export_area15_split.py places it under assets/area15/):
  sub<s>/roster.emro, sub<s>/message_data.emmd, sub<s>/world_models.emwm (+ .json)
  spawn_table.emsp, door_destinations.emsp, scripts.emsc, overlay_data.emsc, tables.json

Usage (port root, macOS arm64, pure Python): python3 tools/export_area15_tables.py [--sub 0|1]
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area15_common as A15  # noqa: E402  (first: registers the AREA15 target)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--tree', type=Path, default=A15.TREE)
    ap.add_argument('--sub', type=int, choices=(0, 1), action='append', help='default both')
    args = ap.parse_args(argv)
    tree = args.tree.resolve()
    for s in args.sub or (0, 1):
        A15.install(s)
        A15.TARGET.out = tree / f'part{s}' / f'sub{s}'
        A15.A13.OUT = tree / f'part{s}'
        report = A15.TB.export_target('area15', A15.A13.OUT)
        if tuple(report['captures']) != A15.CAPTURE_NAMES[s]:
            raise SystemExit(f'AREA15 sub-{s} captures {report["captures"]}, not the pinned {A15.CAPTURE_NAMES[s]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
