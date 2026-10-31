#!/usr/bin/env python3
"""Export the sound banks and the registry of AREA03 subs 1 and 2 and AREA04
sub 1 (export_area03x_common.py): export_area13_sfx.export_target, unchanged.

Usage (port root, macOS arm64, pure Python):
  python3 tools/export_area03x_sfx.py [--key 03_1|03_2|04_1]
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
    args = ap.parse_args(argv)
    X.SX.X.Elf()                                  # pins the boot ELF
    for k in args.key or sorted(X.KEYS):
        X.install(k)
        X.SX.export_target(X.KEYS[k]['name'], X.A13.OUT)
    return 0


if __name__ == '__main__':
    sys.exit(main())
