#!/usr/bin/env python3
"""Deliver the whole model library D_0028A56C (extract/chunk27/f01_id37.bin).

Run after export_roger_banks.py, export_area00_library.py,
export_area02_library.py and export_mid_library.py. Those extend read-only
spans model by model, as each area's owners came to need them. From AREA19 on
the owners bind library models by number from their code (001C06E0's model 3,
the creature families' 001C6120), so the gaps between the spans are filled: every read-only
library region (word +0x0C = 1) grows to the bytes up to the next region, or
the file's end. A region that already holds library bytes (the table head,
the spans mapped with word +0x0C = 0, the writable model 0x16) keeps its
place and its flags, and every previous byte stays: the output is checked
against the input and against the disc's file. The region count only falls.

Output is private disc-derived data (assets/scene_snow/roger/resources.emrs),
never repository content.
Runs natively on arm64 macOS (pure Python).
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import export_area00_library as L
import export_area02_library as L2
import export_roger_banks as B

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/levels/later-library-export'


def fill(previous, library):
    header, regions = L.parse(previous)
    base = B.u32(header, 32 + 4 * B.GLOBAL_TABLE_INDEX)
    end = base + len(library)
    inside = sorted(r for r in regions if base <= r[0] < end)
    outside = [r for r in regions if not base <= r[0] < end]
    for a, _w, _lib, raw in inside:
        if raw != library[a - base:a - base + len(raw)]:
            raise ValueError(f'the region at {a:#x} differs from the disc\'s library')
    result, cursor = [], inside[0][0] + len(inside[0][3])
    result.append(inside[0])
    for r in inside[1:]:
        a, writable, flag, raw = r
        if a > cursor:
            # the gap, as one read-only library span up to this region
            if flag == 1 and not writable:
                result.append((cursor, 0, 1, library[cursor - base:a + len(raw) - base]))
                cursor = a + len(raw)
                continue
            result.append((cursor, 0, 1, library[cursor - base:a - base]))
        result.append(r)
        cursor = a + len(raw)
    if cursor < end:
        result.append((cursor, 0, 1, library[cursor - base:]))
    merged = []
    for r in result:
        if merged and merged[-1][1:3] == r[1:3] == (0, 1) and merged[-1][0] + len(merged[-1][3]) == r[0]:
            merged[-1] = (merged[-1][0], 0, 1, merged[-1][3] + r[3])
        else:
            merged.append(r)
    regions = outside + merged
    if len(regions) > 16:
        raise ValueError(f'{len(regions)} regions: the provider holds 16')
    return L2.rebuild(header[:16] + len(regions).to_bytes(4, 'little') + header[20:], regions), base


def verify(data, previous, library, base):
    _h, regions = L.parse(data)
    _p, before = L.parse(previous)
    for a, _w, _f, raw in before:
        got = b''.join(r[3][max(0, a - r[0]):max(0, a + len(raw) - r[0])] for r in sorted(regions)
                       if r[0] < a + len(raw) and a < r[0] + len(r[3]))
        if got != raw:
            raise ValueError(f'the bytes of the region at {a:#x} changed')
    spans = sorted((a, len(raw)) for a, _w, _f, raw in regions if base <= a < base + len(library))
    cursor = spans[0][0]
    for a, n in spans:
        if a != cursor:
            raise ValueError(f'a gap at {cursor:#x}')
        cursor = a + n
    if cursor != base + len(library):
        raise ValueError(f'the regions end at {cursor:#x}')
    return dict(regions=len(regions), library_bytes=len(library))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=ROOT / 'assets/scene_snow/roger/resources.emrs')
    args = parser.parse_args()
    output = args.out.absolute()
    if any(p.is_symlink() for p in (output, *output.parents)):
        raise ValueError('Output must use the worktree private directories (not a symlink)')
    previous = output.read_bytes()
    library = (B.DECOMP / 'extract/chunk27/f01_id37.bin').read_bytes()
    result, base = fill(previous, library)
    receipt = verify(result, previous, library, base)
    receipt.update(bytes_added=len(result) - len(previous), sha256=hashlib.sha256(result).hexdigest())
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    output.write_bytes(result)
    print(f'later library: {receipt["regions"]} regions, the {receipt["library_bytes"]} library bytes whole, '
          f'{receipt["bytes_added"]} bytes added')


if __name__ == '__main__':
    main()
