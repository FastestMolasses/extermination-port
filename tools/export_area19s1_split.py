#!/usr/bin/env python3
"""Place the AREA19 sub-1 export under assets/area19s1/
(docs/AREA19S1_ASSETS.md).

export_area19s1_level / _tables / _sfx write the full export over the two
a19c sub-1 captures into build/area19s1/assets/tree/: the sub's own files
under sub1/ and the area's side files (spawn_table.emsp,
door_destinations.emsp, scripts.emsc, overlay_data.emsc, tables.json,
loaded_sub_proof.json) next to it, where the AREA13 lane's exporters write
them. assets/area19/ already holds the sub-0 export's side files, so the
sub-1 side files are not written there. Nor is anything written inside
assets/area19/: the AREA19X lane's split and checker
(export_area19x_split.tree_files, test_area19x_assets_reference.
split_problems) require every file of assets/area19/ outside reload/ to be
a sub-0 file listed in its reload.json, so a sub1/ folder there would fail
them. This tool writes, under assets/area19s1/ (ignored, disc-derived,
never committed), in assets/area19/'s layout:

  sub1/<path>            every file of the tree's sub1/
  <side file name>       every side file whose bytes differ from
                         assets/area19/'s (the reports: tables.json and
                         loaded_sub_proof.json describe the a19c captures)
  sub1.json              per sub1/ file: its SHA-256; per side file: placed here
                         (both SHA-256s) or identical to assets/area19/'s
                         (its SHA-256, not copied); and which sub-1 files are
                         byte-identical to a sub-0 file (assets/area19/sub0/
                         or reload/sub0/), named, not removed

A file under assets/area19s1/ that the tree does not list is removed.

Usage (port root, macOS arm64, pure Python): python3 tools/export_area19s1_split.py
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area19s1_common as S1  # noqa: E402

SIDE = ('spawn_table.emsp', 'door_destinations.emsp', 'scripts.emsc', 'overlay_data.emsc', 'tables.json',
        'loaded_sub_proof.json')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def files_under(root):
    """{relative posix path: Path} of every file under `root`."""
    return {p.relative_to(root).as_posix(): p for p in sorted(root.rglob('*')) if p.is_file()}


def split(tree, area, out):
    """The sub1.json record (and the copies under `out`)."""
    own = files_under(tree / 'sub1')
    extra = sorted(p.relative_to(tree).as_posix() for p in tree.rglob('*')
                   if p.is_file() and p.relative_to(tree).parts[0] != 'sub1' and p.name not in SIDE)
    if extra:
        raise SystemExit(f'the tree holds files outside sub1/ and the side files: {extra}')
    missing = [n for n in SIDE if not (tree / n).exists()]
    if missing or not own:
        raise SystemExit(f'the tree lacks {missing or "sub1/"}')
    rel_root = lambda p: Path(os.path.relpath(p, S1.ROOT)).as_posix()
    record = dict(source_tree=rel_root(tree), area_tree=rel_root(area),
                  captures=list(S1.CAPTURE_NAMES), sub=S1.SUB, files={}, side_placed={}, side_identical={},
                  same_as_sub0={})
    out.mkdir(parents=True, exist_ok=True)
    keep = set()
    sub0 = {**{f'reload/sub0/{k}': v for k, v in files_under(area / 'reload/sub0').items()},
            **{f'sub0/{k}': v for k, v in files_under(area / 'sub0').items()}} if (area / 'sub0').exists() else {}
    for rel, p in own.items():
        data = p.read_bytes()
        record['files'][rel] = dict(sha256=sha(data), bytes=len(data))
        twins = sorted(k for k, q in sub0.items() if q.name == p.name and q.stat().st_size == len(data)
                       and q.read_bytes() == data)
        if twins:
            record['same_as_sub0'][rel] = twins
        dest = out / 'sub1' / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(p, dest)
        keep.add(f'sub1/{rel}')
    for name in SIDE:
        data = (tree / name).read_bytes()
        first = area / name
        old = first.read_bytes() if first.exists() else None
        if old == data:
            record['side_identical'][name] = sha(data)
            continue
        record['side_placed'][name] = dict(sha256=sha(data), area_sha256=None if old is None else sha(old))
        shutil.copyfile(tree / name, out / name)
        keep.add(name)
    for p in sorted(out.rglob('*'), reverse=True):
        rel = p.relative_to(out).as_posix()
        if p.is_file() and rel != 'sub1.json' and rel not in keep:
            p.unlink()
        elif p.is_dir() and not any(p.iterdir()):
            p.rmdir()
    (out / 'sub1.json').write_text(json.dumps(record, indent=1) + '\n')
    return record


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--tree', type=Path, default=S1.TREE)
    ap.add_argument('--area', type=Path, default=S1.A19_TREE)
    ap.add_argument('--out', type=Path, default=S1.OUT)
    args = ap.parse_args(argv)
    rec = split(args.tree.absolute(), args.area.absolute(), args.out.absolute())
    print(f"assets/area19s1/sub1/: {len(rec['files'])} files; side files placed {sorted(rec['side_placed'])}, identical to "
          f"assets/area19/ {sorted(rec['side_identical'])}; byte-identical to a sub-0 file "
          f"{ {k: v for k, v in rec['same_as_sub0'].items()} }")
    return 0


if __name__ == '__main__':
    sys.exit(main())
