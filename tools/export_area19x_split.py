#!/usr/bin/env python3
"""Keep what the second AREA19 load changes or adds (docs/AREA19X_ASSETS.md).

export_area19x_level / _tables / _sfx write the full AREA19 sub-0 export
over the three a19b captures into build/area19x/assets/tree/. This tool
compares every file of that tree with the first load's export
(assets/area19/, the AREA19 lane's; its reload/ folder is not compared)
and writes, under assets/area19/reload/ (ignored, disc-derived, never
committed):

  <same relative path>   every file whose bytes differ (the binary assets
                         whose base address moved with the cursor, and the
                         report files, which describe the a19b captures),
                         and every file the first load's export does not
                         have (the creature resources)
  reload.json            per file: identical (with its SHA-256, read from
                         assets/area19/), differing (both SHA-256s, and for
                         a binary file of equal length every differing
                         32-bit word with both values: addresses, not data)
                         or added (its SHA-256)

A file in reload/ that the new tree does not list as differing or added is
removed. Every file of the first load's export must be in the new tree.

Usage (port root, macOS arm64, pure Python): python3 tools/export_area19x_split.py
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area19x_common as X19  # noqa: E402

REPORTS = ('.json', '.txt')                       # descriptive files: compared, never word-diffed


def sha(data):
    return hashlib.sha256(data).hexdigest()


def tree_files(root, skip=('reload',)):
    """{relative posix path: Path} of every file under `root`, outside the
    `skip` top-level folders."""
    out = {}
    for p in sorted(root.rglob('*')):
        rel = p.relative_to(root)
        if p.is_file() and rel.parts[0] not in skip:
            out[rel.as_posix()] = p
    return out


def word_diffs(new, old):
    """[[offset, old word, new word]] (hex) of two equal-length blobs."""
    if len(new) != len(old):
        return None
    out = []
    for k in range(0, len(new) - len(new) % 4, 4):
        if new[k:k + 4] != old[k:k + 4]:
            out.append([hex(k), hex(struct.unpack_from('<I', old, k)[0]), hex(struct.unpack_from('<I', new, k)[0])])
    if len(new) % 4 and new[-(len(new) % 4):] != old[-(len(old) % 4):]:
        out.append(['tail', None, None])
    return out


def split(tree, first, out):
    """The reload.json record (and the copies under `out`)."""
    new, old = tree_files(tree), tree_files(first)
    if set(old) - set(new):
        raise SystemExit(f'the new tree lacks {sorted(set(old) - set(new))}')
    record = dict(source_tree=str(tree.relative_to(X19.ROOT)), first_tree=str(first.relative_to(X19.ROOT)),
                  captures=list(X19.CAPTURE_NAMES), identical={}, differing={}, added={})
    out.mkdir(parents=True, exist_ok=True)
    keep = set()
    for rel, p in new.items():
        nb = p.read_bytes()
        if rel not in old:
            record['added'][rel] = dict(sha256=sha(nb), bytes=len(nb))
        else:
            ob = old[rel].read_bytes()
            if nb == ob:
                record['identical'][rel] = sha(nb)
                continue
            row = dict(sha256=sha(nb), first_sha256=sha(ob), bytes=len(nb), first_bytes=len(ob))
            if not rel.endswith(REPORTS):
                row['words'] = word_diffs(nb, ob)
            record['differing'][rel] = row
        dest = out / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(p, dest)
        keep.add(rel)
    for p in sorted(out.rglob('*'), reverse=True):
        rel = p.relative_to(out).as_posix()
        if p.is_file() and rel != 'reload.json' and rel not in keep:
            p.unlink()
        elif p.is_dir() and not any(p.iterdir()):
            p.rmdir()
    (out / 'reload.json').write_text(json.dumps(record, indent=1) + '\n')
    return record


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--tree', type=Path, default=X19.TREE)
    ap.add_argument('--first', type=Path, default=X19.A19_TREE)
    ap.add_argument('--out', type=Path, default=X19.OUT)
    args = ap.parse_args(argv)
    rec = split(args.tree.resolve(), args.first.resolve(), args.out.resolve())
    print(f"identical: {len(rec['identical'])} files; differing: "
          + '; '.join(f"{k} ({len(v['words'])} words)" if v.get('words') is not None else k
                      for k, v in rec['differing'].items())
          + f"; added: {', '.join(rec['added'])}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
