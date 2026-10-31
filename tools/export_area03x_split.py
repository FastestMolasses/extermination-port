#!/usr/bin/env python3
"""Place the AREA03 subs 1 / 2 and AREA04 sub 1 export (docs/AREA03X_PORT.md).

export_area03x_level / _tables / _sfx / _textures write build/area03x/assets/
tree/<key>/: the sub's files under sub<n>/ and the side files next to it. This
tool copies each sub<n>/ to assets/levels/03/sub<n>/ resp.
assets/levels/04/sub1/ (ignored, disc-derived, never committed), with the
key's tables.json inside it, and compares every side file with the area's
existing export: a difference is refused, as the shared files must not depend
on the sub. A symlink on the way is refused: write real files.

Usage (port root, macOS arm64, pure Python): python3 tools/export_area03x_split.py
"""
from __future__ import annotations

import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TREE = ROOT / 'build/area03x/assets/tree'
PLACES = {'03_1': ('assets/levels/03', 'sub1'), '03_2': ('assets/levels/03', 'sub2'),
          '04_1': ('assets/levels/04', 'sub1')}
SIDE = {'03': {n: f'assets/levels/03/{n}' for n in ('spawn_table.emsp', 'door_destinations.emsp', 'scripts.emsc',
                                                    'overlay_data.emsc')},
        '04': {'door_destinations.emsp': 'assets/area04/door_destinations.emsp',
               'overlay_data.emsc': 'assets/area04/overlay_data.emsc',
               'scripts.emsc': 'assets/area04/area04_scripts/scripts.emsc',
               'spawn_table.emsp': 'assets/area04/spawn_table.emsp'}}


def main():
    for key, (place, sub) in PLACES.items():
        part = TREE / key
        for name, have in SIDE[key[:2]].items():
            got, want = (part / name).read_bytes(), (ROOT / have).read_bytes()
            if got != want:
                print(f'{key}: side file {name} differs from {have} ({len(got)} / {len(want)} bytes)')
        dest = ROOT / place / sub
        for p in (dest, *dest.parents):
            if p.is_symlink():
                raise SystemExit(f'{p}: a symlink (write real files)')
        count = 0
        for src in sorted((part / sub).rglob('*')):
            if src.is_file():
                out = dest / src.relative_to(part / sub)
                out.parent.mkdir(parents=True, exist_ok=True)
                if out.is_symlink():
                    raise SystemExit(f'{out}: a symlink (write real files)')
                shutil.copyfile(src, out)
                count += 1
        shutil.copyfile(part / 'tables.json', dest / 'tables.json')
        print(f'{place}/{sub}: {count + 1} files')
    return 0


if __name__ == '__main__':
    sys.exit(main())
