#!/usr/bin/env python3
"""Extend the retained library export with AREA04's missing library model.

Run export_roger_banks.py, export_area00_library.py and
export_area02_library.py first. AREA04 sub 0's roster (walked as 001B6990
walks it) binds the library pickup models 0x41, 0x4D, 0x4E, 0x4F, 0x57,
0x58, 0x6C and 0x72 (0015AFA0 unless (+3 & 15) == 1; 00219550 unless class
0 / item 0x28) and the 001C4960 model 0x7D; all but 0x41 are already
exported. 0x41 follows 0x40 in the library file, whose retained span
(one model) is extended up to 0x41's end: its address stays, every
previous byte stays, and the region count (the provider's limit of 16) is
unchanged.

Every AREA04 capture (the a02_05 arrival, s87 route_a04's AREA04 beats)
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

import export_area04_common as A
import export_area11_roster as R
import export_roger_banks as B
import export_area00_library as L
import export_area02_library as E2
import reference_root as refs

ROOT = Path(__file__).resolve().parents[1]
REQUIRED = {0x41, 0x4D, 0x4E, 0x4F, 0x57, 0x58, 0x6C, 0x72}
OWNER_MODELS = {(0x1C4960, 0x7D)}
PREVIOUS, MODEL = 0x40, 0x41                  # the retained 0x40 span, extended over 0x41
OUT = ROOT / 'build/levels/area04-library-export'


def required_models():
    c = A.configure(0)
    read = c.static_reader(c.read_elf(), A.read_overlay())
    groups, _, placements = R.walk_roster(read, 4, 0)
    rows = [(B.u32(r, 0x24), r[2], r[3], r[4]) for r in placements]
    rows += [(B.u32(r, 0x28), r[6], r[7], r[8]) for _, records in groups for r in records]
    result = {model for callback, kind, item, model in rows if E2.library_pickup(callback, kind, item)}
    if result != REQUIRED:
        raise ValueError(f'AREA04 pickup model census changed: {sorted(hex(m) for m in result)}')
    owners = {(cb, m) for cb, _, _, m in rows if cb == 0x1C4960}
    if owners != OWNER_MODELS:
        raise ValueError(f'AREA04 001C4960 model census changed: {sorted(owners)}')
    return result | {m for _, m in owners}


def extend(previous, library):
    header, regions = L.parse(previous)
    base = B.u32(header, 32 + 4 * B.GLOBAL_TABLE_INDEX)
    prev = B.u32(library, 4 + 4 * PREVIOUS) & ~3
    model = B.u32(library, 4 + 4 * MODEL) & ~3
    end = model + B.block_model_size(library, model)
    if not prev < model or model - prev > 0x10000:
        raise ValueError('model 0x41 does not follow 0x40')
    matches = [i for i, r in enumerate(regions) if r[0] == base + prev]
    if len(matches) != 1:
        raise ValueError('the retained span starting at model 0x40 is missing (run export_roger_banks.py)')
    i = matches[0]
    address, writable, retained, old = regions[i]
    if writable or old != library[prev:prev + len(old)]:
        raise ValueError('the 0x40 span differs from disc or is writable')
    if prev + len(old) < end:
        regions[i] = (address, writable, retained, library[prev:end])
    return E2.rebuild(header, regions)


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
            raise ValueError(f'library model {model:#x} is not delivered exactly')
        spans[model] = address
    start = base + (B.u32(library, 4 + 4 * PREVIOUS) & ~3)
    extended = [(a, raw) for a, _, _, raw in regions if a == start]
    paths = [refs.capture('s87/route_a02') / 'a02_05_progression_exit/eeMemory.bin']
    paths += sorted(refs.capture('s87/route_a04').glob('a04_*/eeMemory.bin'))
    snapshots = live = 0
    for path in paths:
        ram = path.read_bytes()
        if ram[0x810700] != 4:
            continue                              # a04_05 ends in AREA22
        if B.u32(ram, B.TABLE + 4 * B.GLOBAL_TABLE_INDEX) != base:
            raise ValueError(f'{path.parent.name}: another library base')
        for address, raw in extended:
            if ram[address:address + len(raw)] != raw:
                raise ValueError(f'{path.parent.name}: RAM differs within the extended span')
        for slot in range(400):
            a = 0x7A5640 + slot * 0x2F0
            if not ram[a] or not E2.library_pickup(B.u32(ram, a + 0x10), ram[a + 3],
                                                   int.from_bytes(ram[a + 0x2E:a + 0x30], 'little')):
                continue
            model = ram[a + 0xD]
            if model not in spans or B.u32(ram, a + 0x44) != spans[model]:
                raise ValueError(f'{path.parent.name}: live pickup {slot} handle differs from its library model')
            live += 1
        snapshots += 1
    if not snapshots:
        raise ValueError('no AREA04 capture')
    return dict(models=len(spans), snapshots=snapshots, live_handles=live, regions=len(regions),
                extended_span_bytes=sum(len(raw) for _, raw in extended))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=ROOT / 'assets/scene_snow/roger/resources.emrs')
    args = parser.parse_args()
    output = args.out.absolute()
    if output.is_symlink():
        previous = output.read_bytes()
        output.unlink()
    else:
        previous = output.read_bytes()
    if any(p.is_symlink() for p in output.parents):
        raise ValueError('Output must use the worktree private directories')
    library = (B.DECOMP / 'extract/chunk27/f01_id37.bin').read_bytes()
    result = extend(previous, library)
    receipt = verify(result, library)
    receipt.update(bytes_added=len(result) - len(previous), sha256=hashlib.sha256(result).hexdigest())
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    output.write_bytes(result)
    print(f'AREA04 library: {receipt["models"]} models, {receipt["snapshots"]} matching snapshots, '
          f'{receipt["live_handles"]} live pickup handles; {receipt["bytes_added"]} bytes added, '
          f'{receipt["regions"]} regions retained')


if __name__ == '__main__':
    main()
