#!/usr/bin/env python3
"""Extend the retained library export with AREA00's two missing pickup models.

Run export_roger_banks.py first. This adds disc bytes to two existing read-only
library regions, preserving the table, flags, region count and every previous
byte. The production provider's 16-region limit therefore remains sufficient.
Output is private disc-derived data, never repository content.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

import export_area00_common as A
import export_area11_roster as R
import export_roger_banks as B

SPANS = ((0x4D, 0x4F), (0x58, 0x59))
OUT = A.ROOT / 'build/levels/area00-library-export'


def parse(data):
    if len(data) < 32 + 4 * B.TABLE_WORDS:
        raise ValueError('Truncated retained library export')
    header = struct.unpack_from('<4s7I', data)
    if header[:4] != (b'EMRS', 2, B.TABLE, B.TABLE_WORDS) or not 0 < header[4] <= 16 or any(header[5:]):
        raise ValueError('Invalid retained library header')
    at = 32 + 4 * B.TABLE_WORDS
    regions = []
    for _ in range(header[4]):
        if at + 16 > len(data):
            raise ValueError('Truncated retained library region')
        address, size, writable, library = struct.unpack_from('<4I', data, at)
        at += 16
        if not size or at + size > len(data) or writable > 1 or library > 1 or writable > library:
            raise ValueError('Invalid retained library region')
        regions.append((address, writable, library, data[at:at + size]))
        at += size
    if at != len(data):
        raise ValueError('Trailing retained library bytes')
    for left, right in zip(sorted(regions), sorted(regions)[1:]):
        if left[0] + len(left[3]) > right[0]:
            raise ValueError('Overlapping retained library regions')
    return data[:32 + 4 * B.TABLE_WORDS], regions


def required_models():
    c = A.configure(0)
    read = c.static_reader(c.read_elf(), A.read_overlay())
    result = set()
    for sub in A.SUBS:
        groups, _, placements = R.walk_roster(read, 0, sub)
        rows = [(B.u32(r, 0x24), r[2], r[3], r[4]) for r in placements]
        rows += [(B.u32(r, 0x28), r[6], r[7], r[8]) for _, records in groups for r in records]
        for callback, kind, item, model in rows:
            if ((callback == 0x15AFA0 and kind & 15 != 1) or
                    (callback == 0x219550 and not (kind == 0 and item == 0x28))):
                result.add(model)
    if result != {0x4D, 0x4F, 0x58, 0x59, 0x72}:
        raise ValueError('AREA00 pickup model census changed')
    return result


def extend(previous, library):
    header, regions = parse(previous)
    base = B.u32(header, 32 + 4 * B.GLOBAL_TABLE_INDEX)
    table_size = 4 + 4 * B.u32(library, 0)
    if not any(a == base and data == library[:table_size] for a, _, _, data in regions):
        raise ValueError('Retained library table differs from disc')
    for first, last in SPANS:
        start = B.u32(library, 4 + 4 * first) & ~3
        end_at = B.u32(library, 4 + 4 * last) & ~3
        end = end_at + B.block_model_size(library, end_at)
        # The span may already start lower (export_area02_library.py
        # extends the 0x58 span down to 0x57): the region holding `start`.
        matches = [i for i, r in enumerate(regions) if r[0] <= base + start < r[0] + len(r[3])]
        if len(matches) != 1:
            raise ValueError('Expected retained pickup span is missing')
        i = matches[0]
        address, writable, retained, old = regions[i]
        lo = address - base
        if writable or retained != 1 or old != library[lo:lo + len(old)]:
            raise ValueError('Existing pickup span differs from disc or has unexpected flags')
        regions[i] = (address, writable, retained, library[lo:max(end, lo + len(old))])
    result = bytearray(header)
    for address, writable, retained, data in regions:
        result += struct.pack('<4I', address, len(data), writable, retained) + data
    parse(result)
    for old, new in zip(parse(previous)[1], regions):
        if old[:3] != new[:3] or new[3][:len(old[3])] != old[3]:
            raise ValueError('Export changed existing bytes or flags')
    return bytes(result)


def verify(data, library):
    header, regions = parse(data)
    base = B.u32(header, 32 + 4 * B.GLOBAL_TABLE_INDEX)
    models = required_models()
    spans = {}
    for model in models:
        offset = B.u32(library, 4 + 4 * model) & ~3
        size = B.block_model_size(library, offset)
        address = base + offset
        expected = library[offset:offset + size]
        if not any(a <= address and address + size <= a + len(raw) and
                   raw[address - a:address - a + size] == expected for a, _, _, raw in regions):
            raise ValueError('Required pickup model is not delivered exactly')
        spans[model] = (address, expected)
    from export_area00_runtime_tables import captures
    paths = captures(A.DECOMP)
    extended_starts = {base + (B.u32(library, 4 + 4 * first) & ~3) for first, _ in SPANS}
    extended = [(address, raw) for address, _, _, raw in regions if address in extended_starts]
    observations = 0
    for path in paths:
        ram = path.read_bytes()
        if B.u32(ram, B.TABLE + 4 * B.GLOBAL_TABLE_INDEX) != base:
            raise ValueError('Original capture uses a different library base')
        for address, expected in spans.values():
            if ram[address:address + len(expected)] != expected:
                raise ValueError('Original capture differs from exported pickup model')
        for address, expected in extended:
            if ram[address:address + len(expected)] != expected:
                raise ValueError('Original capture differs within an extended library span')
        for slot in range(256):
            actor = 0x7A5640 + slot * 0x2F0
            if not ram[actor] or B.u32(ram, actor + 0x10) != 0x15AFA0 or ram[actor + 3] & 15 == 1:
                continue
            model = ram[actor + 0xD]
            if model not in spans or B.u32(ram, actor + 0x44) != spans[model][0]:
                raise ValueError('Live pickup handle differs from roster library dependency')
            observations += 1
    return dict(models=len(models), snapshots=len(paths), live_handles=observations,
                regions=len(regions), model_bytes=sum(len(raw) for _, raw in spans.values()),
                extended_span_bytes=sum(len(raw) for _, raw in extended))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=A.ROOT / 'assets/scene_snow/roger/resources.emrs')
    args = parser.parse_args()
    output = args.out.absolute()
    if any(p.is_symlink() for p in (output, *output.parents)):
        raise ValueError('Output must use the worktree private directories')
    previous = output.read_bytes()
    library = (B.DECOMP / 'extract/chunk27/f01_id37.bin').read_bytes()
    result = extend(previous, library)
    receipt = verify(result, library)
    receipt.update(bytes_added=len(result) - len(previous), sha256=hashlib.sha256(result).hexdigest())
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    output.write_bytes(result)
    print(f'AREA00 library: {receipt["models"]} models, {receipt["snapshots"]} matching snapshots; '
          f'{receipt["bytes_added"]} bytes added, {receipt["regions"]} regions retained')


if __name__ == '__main__':
    main()
