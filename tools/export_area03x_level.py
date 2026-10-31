#!/usr/bin/env python3
"""Export the level of AREA03 subs 1 and 2 and AREA04 sub 1
(export_area03x_common.py): export_area13_level.export_target, or
export_area15_level.export_dynamic for a load whose nested list relocates id
0x45.

Usage (port root, macOS arm64, pure Python):
  python3 tools/export_area03x_level.py [--key 03_1|03_2|04_1] [--skip-collision]
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area03x_common as X  # noqa: E402


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--key', choices=sorted(X.KEYS), action='append', help='default all')
    ap.add_argument('--skip-collision', action='store_true')
    args = ap.parse_args(argv)
    el = X.L.load_export_level()
    elf = X.C.read_elf()
    iso = X.C.ISO_PATH.resolve()
    for k in args.key or sorted(X.KEYS):
        X.install(k)
        v = X.KEYS[k]
        X.A13.read_overlay(X.A13.current())          # pins the overlay
        X.A13.OUT.mkdir(parents=True, exist_ok=True)
        if v['dynamic']:
            X.A15L.export_dynamic(el, elf, X.A13.OUT, iso, args.skip_collision, target=v['name'])
        else:
            X.LV.export_target(v['name'], el, elf, X.A13.OUT, iso, args.skip_collision)
    return 0


if __name__ == '__main__':
    sys.exit(main())
