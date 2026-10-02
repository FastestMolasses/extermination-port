#!/usr/bin/env python3
"""Export AREA15's sound container and registry for sub 0 (a19d_20) and
sub 1 (a15_01) (docs/AREA15_ASSETS.md).

export_area13_sfx.export_target (the AREA13 lane's path: table entry 0 of
the descriptor whose +0x0C is 1, here each sub's nested block, which
001FFCD0 state 8 hands to the IOP stream loader; the bindings from the
capture's D_00281D50 / D_0027C6C0; the AREA01 registry export) is imported
and run UNCHANGED, once per sub, with export_area15_common's AREA15 target
installed.

Output (ignored build/area15/assets/tree/part<sub>/sub<sub>/sfx/,
disc-derived, never committed):
  area15_banks.bin, banks.json, sfx_registry.emsr (+ .json)

Usage (port root, macOS arm64, pure Python): python3 tools/export_area15_sfx.py [--sub 0|1]
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
    A15.SX.X.Elf()                               # pins the boot ELF
    for s in args.sub or (0, 1):
        A15.install(s)
        A15.TARGET.out = tree / f'part{s}' / f'sub{s}'
        A15.A13.OUT = tree / f'part{s}'
        summary = A15.SX.export_target('area15', A15.A13.OUT)
        if tuple(summary['captures']) != A15.CAPTURE_NAMES[s]:
            raise SystemExit(f'AREA15 sub-{s} captures {summary["captures"]}, not the pinned {A15.CAPTURE_NAMES[s]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
