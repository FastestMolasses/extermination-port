#!/usr/bin/env python3
"""Extend the retained library export with AREA02's missing library models.

Run export_roger_banks.py and export_area00_library.py first. AREA02's
static rosters (sub 0 and 1, walked as 001B6990 walks them) bind the
library pickup models 0x4D, 0x4F, 0x57, 0x58 and 0x72 (0015AFA0 unless
(+3 & 15) == 1; 00219550 unless class 0 / item 0x28); all but 0x57 are
already exported. 0x57 is the block model right before 0x58 in the
library file, so the read-only span that starts at 0x58 is extended down
to 0x57's header: its address moves, every previous byte stays, and the
region count (the provider's limit of 16) is unchanged.

AREA02 sub 0's 001C4960 owner binds the library model 0x7D through
001B1020(node, +0x0D, -1, 0). 0x7B..0x7D follow 0x7A, the end of the
equipment span 0x67..0x7A, in file order: that span is extended up to
0x7D's end (0x7B / 0x7C are exported and checked too).

Every AREA02 capture (s87 route_a02's AREA02 beats, a01r_03's arrival)
must hold the extended span's bytes at their addresses, and each live
library pickup's +0x44 must be its model's address.

Output is private disc-derived data (assets/scene_snow/roger/resources.emrs),
never repository content; the receipt holds counts only.
Runs natively on arm64 macOS (pure Python).
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

import export_area02_common as A
import export_area11_roster as R
import export_roger_banks as B
import export_area00_library as L
import reference_root as refs

ROOT = Path(__file__).resolve().parents[1]
REQUIRED = {0x4D, 0x4F, 0x57, 0x58, 0x72}
FIRST, NEXT = 0x57, 0x58
SPAN_FIRST, SPAN_LAST, TAIL = 0x67, 0x7A, 0x7D       # the equipment span, extended to 0x7D
OWNER_MODELS = {(0x1C4960, 0x7D)}                    # (callback, +0x0D) of AREA02's library owners
OUT = ROOT / 'build/levels/area02-library-export'


def library_pickup(callback, kind, item):
    return (callback == 0x15AFA0 and kind & 15 != 1) or (callback == 0x219550 and not (kind == 0 and item == 0x28))


def required_models():
    c = A.configure(0)
    read = c.static_reader(c.read_elf(), A.read_overlay())
    result = set()
    for sub in A.SUBS:
        groups, _, placements = R.walk_roster(read, 2, sub)
        rows = [(B.u32(r, 0x24), r[2], r[3], r[4]) for r in placements]
        rows += [(B.u32(r, 0x28), r[6], r[7], r[8]) for _, records in groups for r in records]
        result |= {model for callback, kind, item, model in rows if library_pickup(callback, kind, item)}
    if result != REQUIRED:
        raise ValueError(f'AREA02 pickup model census changed: {sorted(hex(m) for m in result)}')
    owners = set()
    for sub in A.SUBS:
        groups, _, placements = R.walk_roster(read, 2, sub)
        rows = [(B.u32(r, 0x24), r[4]) for r in placements]
        rows += [(B.u32(r, 0x28), r[8]) for _, records in groups for r in records]
        owners |= {(cb, m) for cb, m in rows if cb == 0x1C4960}
    if owners != OWNER_MODELS:
        raise ValueError(f'AREA02 001C4960 model census changed: {sorted(owners)}')
    return result | {m for _, m in owners}


def extend(previous, library):
    header, regions = L.parse(previous)
    base = B.u32(header, 32 + 4 * B.GLOBAL_TABLE_INDEX)
    first = B.u32(library, 4 + 4 * FIRST) & ~3
    nxt = B.u32(library, 4 + 4 * NEXT) & ~3
    if not first < first + B.block_model_size(library, first) <= nxt or nxt - first > 0x10000:
        raise ValueError('model 0x57 does not lie right before 0x58')
    span = B.u32(library, 4 + 4 * SPAN_FIRST) & ~3
    tail = B.u32(library, 4 + 4 * TAIL) & ~3
    tail_end = tail + B.block_model_size(library, tail)
    ordered = [B.u32(library, 4 + 4 * i) & ~3 for i in range(SPAN_LAST, TAIL + 1)]
    if ordered != sorted(ordered):
        raise ValueError('models 0x7A..0x7D are not in file order')
    spans = [i for i, r in enumerate(regions) if r[0] == base + span]
    if len(spans) != 1:
        raise ValueError('the equipment span 0x67.. is missing (run export_roger_banks.py)')
    i = spans[0]
    address, writable, retained, old = regions[i]
    if writable or old != library[span:span + len(old)]:
        raise ValueError('the equipment span differs from disc or is writable')
    if span + len(old) < tail_end:
        regions[i] = (address, writable, retained, library[span:tail_end])
    if any(a == base + first for a, *_ in regions):
        return rebuild(header, regions)
    matches = [i for i, r in enumerate(regions) if r[0] == base + nxt]
    if len(matches) != 1:
        raise ValueError('the retained span starting at model 0x58 is missing (run export_area00_library.py)')
    i = matches[0]
    address, writable, retained, old = regions[i]
    if writable or retained != 1 or old != library[nxt:nxt + len(old)]:
        raise ValueError('the 0x58 span differs from disc or has unexpected flags')
    regions[i] = (base + first, writable, retained, library[first:nxt + len(old)])
    return rebuild(header, regions)


def rebuild(header, regions):
    result = bytearray(header)
    for address, writable, retained, data in regions:
        result += struct.pack('<4I', address, len(data), writable, retained) + data
    L.parse(bytes(result))
    return bytes(result)


def verify(data, library):
    header, regions = L.parse(data)
    base = B.u32(header, 32 + 4 * B.GLOBAL_TABLE_INDEX)
    spans = {}
    for model in required_models():
        offset = B.u32(library, 4 + 4 * model) & ~3
        size = B.block_model_size(library, offset)
        address, expected = base + offset, library[offset:offset + size]
        if not any(a <= address and address + size <= a + len(raw) and raw[address - a:address - a + size] == expected
                   for a, _, _, raw in regions):
            raise ValueError(f'pickup model {model:#x} is not delivered exactly')
        spans[model] = address
    starts = {base + (B.u32(library, 4 + 4 * k) & ~3) for k in (FIRST, SPAN_FIRST)}
    extended = [(a, raw) for a, _, _, raw in regions if a in starts]
    paths = [refs.capture('s87/route_a01r') / 'a01r_03_door16/eeMemory.bin']
    paths += sorted(refs.capture('s87/route_a02').glob('a02_*/eeMemory.bin'))
    snapshots = live = 0
    for path in paths:
        ram = path.read_bytes()
        if ram[0x810700] != 2:
            continue                              # a02_05 ends in AREA04
        if B.u32(ram, B.TABLE + 4 * B.GLOBAL_TABLE_INDEX) != base:
            raise ValueError(f'{path.parent.name}: another library base')
        for address, raw in extended:
            if ram[address:address + len(raw)] != raw:
                raise ValueError(f'{path.parent.name}: RAM differs within the extended span')
        for slot in range(400):
            a = 0x7A5640 + slot * 0x2F0
            if not ram[a] or not library_pickup(B.u32(ram, a + 0x10), ram[a + 3], int.from_bytes(ram[a + 0x2E:a + 0x30], 'little')):
                continue
            model = ram[a + 0xD]
            if model not in spans or B.u32(ram, a + 0x44) != spans[model]:
                raise ValueError(f'{path.parent.name}: live pickup {slot} handle differs from its library model')
            live += 1
        snapshots += 1
    if not snapshots:
        raise ValueError('no AREA02 capture')
    return dict(models=len(spans), snapshots=snapshots, live_handles=live, regions=len(regions),
                extended_span_bytes=sum(len(raw) for _, raw in extended))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=ROOT / 'assets/scene_snow/roger/resources.emrs')
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
    if result != previous:
        output.write_bytes(result)
    print(f'AREA02 library: {receipt["models"]} models, {receipt["snapshots"]} matching snapshots, '
          f'{receipt["live_handles"]} live pickup handles; {receipt["bytes_added"]} bytes added, '
          f'{receipt["regions"]} regions retained')


if __name__ == '__main__':
    main()
