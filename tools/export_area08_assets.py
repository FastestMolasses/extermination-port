#!/usr/bin/env python3
"""Export AREA08 subs 0, 2, 3 and AREA07 sub 0 (docs/AREA08_PORT.md).

Runs the AREA13 lane's level, table and sound exports (with AREA15's
dynamic-list step where the load relocates id 0x45) over export_area08_common's
targets, then places each part under assets/area08/ and assets/area07/
(ignored, disc-derived, never committed): sub<s>/<path> per sub, the side
files spawn_table.emsp, door_destinations.emsp, scripts.emsc, overlay_data.emsc
once (refused when the subs' copies differ).

Usage (port root, macOS arm64, pure Python):
  python3 tools/export_area08_assets.py [--only area08:0 ...] [--kind level|tables|sfx|place]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area08_common as A  # noqa: E402  (first)
import export_area15_level as L15  # noqa: E402

SIDE = ('spawn_table.emsp', 'door_destinations.emsp', 'scripts.emsc', 'overlay_data.emsc')


def export_one(target, sub, kind, el, elf, iso):
    A.install(target, sub)
    A.A13.OUT.mkdir(parents=True, exist_ok=True)
    A.TARGETS[target].out.mkdir(parents=True, exist_ok=True)
    if kind in ('all', 'level'):
        if A.SUBS[(target, sub)]['dynamic']:
            L15.export_dynamic(el, elf, A.A13.OUT, iso, False, target)
        else:
            A.LV.export_target(target, el, elf, A.A13.OUT, iso, False)
    if kind in ('all', 'tables'):
        A.install(target, sub)
        A.TB.export_target(target, A.A13.OUT)
    if kind in ('all', 'sfx'):
        A.install(target, sub)
        A.SX.export_target(target, A.A13.OUT)


def place(target):
    tree = A.SCRATCH / 'tree' / target
    out = A.ROOT / 'assets' / target
    if out.is_symlink():
        raise SystemExit(f'{out} is a symlink: unlink it first')
    subs = sorted(s for t, s in A.SUBS if t == target)
    first = None
    for s in subs:
        part = tree / f'part{s}'
        if first is None:
            first = part
            for n in SIDE:
                (out).mkdir(parents=True, exist_ok=True)
                shutil.copyfile(part / n, out / n)
        else:
            differ = [n for n in SIDE if (part / n).read_bytes() != (first / n).read_bytes()]
            if differ:
                raise SystemExit(f'{target}: the side files {differ} differ between the subs')
        for p in sorted((part / f'sub{s}').rglob('*')):
            if p.is_file():
                dest = out / f'sub{s}' / p.relative_to(part / f'sub{s}')
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(p, dest)
        shutil.copyfile(part / 'tables.json', out / f'sub{s}' / 'tables.json')
    record = {p.relative_to(out).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in sorted(out.rglob('*')) if p.is_file() and p.name != f'{target}.json'}
    (out / f'{target}.json').write_text(json.dumps(record, indent=1) + '\n')
    print(f'assets/{target}/: {len(record)} files, subs {subs}')


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--only', action='append', help='target:sub, default every sub')
    ap.add_argument('--kind', choices=('all', 'level', 'tables', 'sfx', 'place'), default='all')
    args = ap.parse_args(argv)
    todo = [(o.split(':')[0], int(o.split(':')[1])) for o in args.only] if args.only else list(A.SUBS)
    if args.kind != 'place':
        C = A.C
        el = A.LV.L.load_export_level()
        elf = C.read_elf()
        for target, sub in todo:
            export_one(target, sub, args.kind, el, elf, C.ISO_PATH.resolve())
    if args.kind in ('all', 'place'):
        for target in sorted({t for t, _s in todo}):
            place(target)
    return 0


if __name__ == '__main__':
    sys.exit(main())
