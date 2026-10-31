#!/usr/bin/env python3
"""Export complete module delivery for every recorded level-2..15 area.

The existing descriptor-driven exporter remains the serializer. This opt-in
pack lives separately from the installed first-level pack. No live selector
changes, capture bytes or original data are embedded in this tool.
"""
import argparse
from pathlib import Path
import subprocess
import sys

import export_module_loader as M
from level_asset_audit import private_assets

ROOT = Path(__file__).resolve().parents[1]
# Union of the route recordings in LEVEL_READINESS, plus the AREA11 start.
# A reload of the same area/sub uses the same complete disc delivery.
AREAS = ((0, 0), (0, 1), (1, 0), (2, 0), (2, 1), (3, 0), (4, 0),
         (6, 0), (11, 0), (13, 0), (15, 0), (15, 1), (19, 0),
         (19, 1), (22, 0), (8, 0), (8, 2), (8, 3), (7, 0), (7, 2), (7, 3))
DEFAULT = ROOT / 'assets/levels/modules.emml'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=DEFAULT)
    parser.add_argument('--iso', type=Path, default=M.DECOMP/'Extermination-rebuilt.iso')
    args = parser.parse_args()
    private_assets(ROOT)
    out = args.out.absolute()
    if not any(out.is_relative_to(ROOT/p) for p in ('assets', 'build')):
        parser.error('--out must be in this worktree assets/ or build/')
    if out.is_symlink() or any(p.is_symlink() for p in out.parents):
        parser.error('symlink output path')
    subprocess.run([sys.executable, str(ROOT/'tools/export_module_loader.py'),
                    '--iso', str(args.iso), '--areas',
                    ','.join(f'{a}:{s}' for a, s in AREAS), '--out', str(out)],
                   cwd=ROOT, check=True)


if __name__ == '__main__':
    main()
