#!/usr/bin/env python3
"""Place the AREA15 export under assets/area15/ (docs/AREA15_ASSETS.md).

export_area15_level / _tables / _sfx write one part per sub into
build/area15/assets/tree/part<sub>/ (the AREA13 lane's layout: the sub's
files under sub<sub>/, the side files next to it) and the loaded-sub proof
over both captures into the tree's root. This tool writes, under
assets/area15/ (ignored, disc-derived, never committed):

  sub0/<path>, sub1/<path>   every file of each part's sub<sub>/
  sub<s>/tables.json         the part's tables report (it describes that
                             sub's capture)
  <side file>                spawn_table.emsp, door_destinations.emsp,
                             scripts.emsc, overlay_data.emsc: once, as both
                             parts hold the same bytes (refused otherwise)
  loaded_sub_proof.json      both captures
  area15.json                per placed file its SHA-256 and size, the
                             captures per sub, and which sub-1 files equal
                             their sub-0 namesake

A file under assets/area15/ that the tree does not list is removed.

Usage (port root, macOS arm64, pure Python): python3 tools/export_area15_split.py
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
import export_area15_common as A15  # noqa: E402

SIDE = ('spawn_table.emsp', 'door_destinations.emsp', 'scripts.emsc', 'overlay_data.emsc')
SUBS = (0, 1)
MANIFEST = 'area15.json'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def files_under(root):
    """{relative posix path: Path} of every file under `root`."""
    return {p.relative_to(root).as_posix(): p for p in sorted(root.rglob('*')) if p.is_file()}


def tree_files(tree):
    """{published path: source Path} of the tree, or SystemExit when the
    tree is not the two parts plus the proof, or the side files differ."""
    out, extra = {}, []
    for p in sorted(tree.rglob('*')):
        if not p.is_file():
            continue
        rel = p.relative_to(tree).parts
        if rel == ('loaded_sub_proof.json',):
            out['loaded_sub_proof.json'] = p
        elif rel[0] in (f'part{s}' for s in SUBS) and len(rel) > 1:
            s = int(rel[0][4:])
            if rel[1] == f'sub{s}':
                out['/'.join(rel[1:])] = p
            elif len(rel) == 2 and rel[1] == 'tables.json':
                out[f'sub{s}/tables.json'] = p
            elif len(rel) == 2 and rel[1] in SIDE:
                if s == SUBS[0]:
                    out[rel[1]] = p
            else:
                extra.append(p.relative_to(tree).as_posix())
        else:
            extra.append(p.relative_to(tree).as_posix())
    if extra:
        raise SystemExit(f'the tree holds files outside the parts and the proof: {extra}')
    missing = [f'part{s}/{n}' for s in SUBS for n in SIDE + ('tables.json',) if not (tree / f'part{s}' / n).exists()]
    missing += [f'part{s}/sub{s}/' for s in SUBS if not (tree / f'part{s}' / f'sub{s}').is_dir()]
    if not (tree / 'loaded_sub_proof.json').exists():
        missing.append('loaded_sub_proof.json')
    if missing:
        raise SystemExit(f'the tree lacks {missing}')
    differ = [n for n in SIDE if (tree / 'part0' / n).read_bytes() != (tree / 'part1' / n).read_bytes()]
    if differ:
        raise SystemExit(f'the side files {differ} differ between the subs')
    return out


def split(tree, out):
    files = tree_files(tree)
    rel_root = lambda p: Path(os.path.relpath(p, A15.ROOT)).as_posix()
    record = dict(source_tree=rel_root(tree), captures={f'sub{s}': list(A15.CAPTURE_NAMES[s]) for s in SUBS},
                  files={}, sub1_same_as_sub0=[])
    out.mkdir(parents=True, exist_ok=True)
    for rel, p in sorted(files.items()):
        data = p.read_bytes()
        record['files'][rel] = dict(sha256=sha(data), bytes=len(data))
        dest = out / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(p, dest)
    for rel in sorted(files):
        if rel.startswith('sub1/'):
            twin = 'sub0/' + rel[5:]
            if twin in record['files'] and record['files'][twin]['sha256'] == record['files'][rel]['sha256']:
                record['sub1_same_as_sub0'].append(rel)
    for p in sorted(out.rglob('*'), reverse=True):
        rel = p.relative_to(out).as_posix()
        if p.is_file() and rel != MANIFEST and rel not in files:
            p.unlink()
        elif p.is_dir() and not any(p.iterdir()):
            p.rmdir()
    (out / MANIFEST).write_text(json.dumps(record, indent=1) + '\n')
    return record


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--tree', type=Path, default=A15.TREE)
    ap.add_argument('--out', type=Path, default=A15.OUT)
    args = ap.parse_args(argv)
    rec = split(args.tree.absolute(), args.out.absolute())
    subs = {s: sum(1 for r in rec['files'] if r.startswith(f'sub{s}/')) for s in SUBS}
    print(f"assets/area15/: sub0/ {subs[0]} files, sub1/ {subs[1]} files, side files {list(SIDE)} once, "
          f"loaded_sub_proof.json; sub-1 files equal to sub 0's: {rec['sub1_same_as_sub0']}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
