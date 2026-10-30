#!/usr/bin/env python3
"""Extend the retained library export with the middle route's library models.

Run export_roger_banks.py, export_area00_library.py and
export_area02_library.py first. The static rosters of AREA22 and AREA06
(sub 0, walked as 001B6990 walks them) bind these library pickup models
(0015AFA0 unless (+3 & 15) == 1; 00219550 unless class 0 / item 0x28):
AREA22 0x41 and 0x72, AREA06 0x4D, 0x4F, 0x57, 0x58, 0x6C and 0x72. All but
0x41 are already exported. 0x41 is the block model right after 0x40 in the
library file: the read-only span that holds 0x40 (export_roger_banks's
equipment span 0x40..0x40) is extended up to 0x41's end. Its address
stays, every previous byte stays, and the region count (the provider's
limit of 16) is unchanged. AREA06's crate (a box of model 0x1E) breaks to
the husk 0x29: the break span 0x1F..0x22 is extended to 0x29's end the same
way, and the shot span 0x06..0x11 down to 0x04 (001BE6C0's model, the
companion AREA01's 001BF6B0 spawns on the way back).

Every recorded AREA22 snapshot (s87 route_a22 / route_a22b beats that end
in AREA22, and AREA04's a04_05 arrival) must hold the extended span's bytes
at their addresses, and each live library pickup's +0x44 must be its
model's address.

Output is private disc-derived data (assets/scene_snow/roger/resources.emrs),
never repository content; the receipt holds counts only.
Runs natively on arm64 macOS (pure Python).
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import export_area06_common as A6    # first: it keeps AREA01's nested load-map builder
import export_area22_common as A22
import export_area11_roster as R
import export_roger_banks as B
import export_area00_library as L
import export_area02_library as L2
import reference_root as refs

ROOT = Path(__file__).resolve().parents[1]
REQUIRED = {0x16: {0x41, 0x72}, 6: {0x4D, 0x4F, 0x57, 0x58, 0x6C, 0x72}}
SPAN, LAST = 0x40, 0x41                   # the equipment span 0x40, extended to 0x41
# AREA06's crate (a box of model 0x1E) breaks to the husk 0x29: 001551B0's
# 001CA6E0(self, 001C6120(*D_0028A56C, 0x29)). 0x23..0x29 follow 0x22, the
# end of the break span 0x1F..0x22 (export_roger_banks BREAK_SPANS), in
# file order: that span is extended up to 0x29's end.
BREAK_FIRST, BREAK_TAIL = 0x1F, 0x29
# AREA01's 001C02E0 after the AREA06 event (D_00810845 bit 5, the way back,
# a01v): 001BF6B0's companion 001BE6C0 binds the library model 0x04, which
# lies right before 0x05 / 0x06, the start of the shot span 0x06..0x11
# (export_roger_banks SHOT_SPANS): that span is extended down to 0x04.
SHOT_FIRST, COMPANION = 0x06, 0x04
OUT = ROOT / 'build/levels/mid-library-export'


def required_models():
    result = set()
    for area, mod in ((0x16, A22), (6, A6)):
        c = mod.configure() if mod is A22 else mod.configure(0)
        read = c.static_reader(c.read_elf(), c.read_overlay())
        groups, _, placements = R.walk_roster(read, area, 0)
        rows = [(B.u32(r, 0x24), r[2], r[3], r[4]) for r in placements]
        rows += [(B.u32(r, 0x28), r[6], r[7], r[8]) for _, records in groups for r in records]
        models = {model for callback, kind, item, model in rows if L2.library_pickup(callback, kind, item)}
        if models != REQUIRED[area]:
            raise ValueError(f'area {area:#x} pickup model census changed: {sorted(hex(m) for m in models)}')
        result |= models
    return result


def extend(previous, library):
    header, regions = L.parse(previous)
    base = B.u32(header, 32 + 4 * B.GLOBAL_TABLE_INDEX)
    span = B.u32(library, 4 + 4 * SPAN) & ~3
    last = B.u32(library, 4 + 4 * LAST) & ~3
    end = last + B.block_model_size(library, last)
    if not span < last or last - (span + B.block_model_size(library, span)) > 0x100:
        raise ValueError('model 0x41 does not follow 0x40')
    matches = [i for i, r in enumerate(regions) if r[0] == base + span]
    if len(matches) != 1:
        raise ValueError('the span starting at model 0x40 is missing (run export_roger_banks.py)')
    i = matches[0]
    address, writable, retained, old = regions[i]
    if writable or old != library[span:span + len(old)]:
        raise ValueError('the 0x40 span differs from disc or is writable')
    if span + len(old) < end:
        regions[i] = (address, writable, retained, library[span:end])
    first = B.u32(library, 4 + 4 * BREAK_FIRST) & ~3
    order = [B.u32(library, 4 + 4 * k) & ~3 for k in range(BREAK_FIRST, BREAK_TAIL + 1)]
    if order != sorted(order):
        raise ValueError('models 0x1F..0x29 are not in file order')
    tail = order[-1] + B.block_model_size(library, order[-1])
    matches = [i for i, r in enumerate(regions) if r[0] == base + first]
    if len(matches) != 1:
        raise ValueError('the break span starting at model 0x1F is missing (run export_roger_banks.py)')
    i = matches[0]
    address, writable, retained, old = regions[i]
    if writable or old != library[first:first + len(old)]:
        raise ValueError('the 0x1F span differs from disc or is writable')
    if first + len(old) < tail:
        regions[i] = (address, writable, retained, library[first:tail])
    lo = B.u32(library, 4 + 4 * COMPANION) & ~3
    shot = B.u32(library, 4 + 4 * SHOT_FIRST) & ~3
    order = [B.u32(library, 4 + 4 * k) & ~3 for k in range(COMPANION, SHOT_FIRST + 1)]
    if order != sorted(order) or lo + B.block_model_size(library, lo) > shot:
        raise ValueError('models 0x04..0x06 are not in file order')
    if not any(a == base + lo for a, *_ in regions):
        matches = [i for i, r in enumerate(regions) if r[0] == base + shot]
        if len(matches) != 1:
            raise ValueError('the shot span starting at model 0x06 is missing (run export_roger_banks.py)')
        i = matches[0]
        address, writable, retained, old = regions[i]
        if writable or old != library[shot:shot + len(old)]:
            raise ValueError('the 0x06 span differs from disc or is writable')
        regions[i] = (base + lo, writable, retained, library[lo:shot + len(old)])
    return L2.rebuild(header, regions)


def snapshots():
    paths = [refs.capture('s87/route_a04') / 'a04_05_progression_exit/eeMemory.bin']
    for route in ('s87/route_a22', 's87/route_a22b', 's87/route_a01u', 's87/route_a01v', 's87/route_a06',
                  's87/route_a06b'):
        paths += sorted(refs.capture(route).glob('*/eeMemory.bin'))
    return paths


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
    husk = B.u32(library, 4 + 4 * BREAK_TAIL) & ~3
    raw_husk = library[husk:husk + B.block_model_size(library, husk)]
    if not any(a <= base + husk and base + husk + len(raw_husk) <= a + len(raw) and
               raw[base + husk - a:base + husk - a + len(raw_husk)] == raw_husk for a, _, _, raw in regions):
        raise ValueError('the crate husk 0x29 is not delivered exactly')
    comp = B.u32(library, 4 + 4 * COMPANION) & ~3
    raw_comp = library[comp:comp + B.block_model_size(library, comp)]
    if not any(a <= base + comp and base + comp + len(raw_comp) <= a + len(raw) and
               raw[base + comp - a:base + comp - a + len(raw_comp)] == raw_comp for a, _, _, raw in regions):
        raise ValueError('the companion model 0x04 is not delivered exactly')
    starts = {base + (B.u32(library, 4 + 4 * k) & ~3) for k in (SPAN, BREAK_FIRST, COMPANION)}
    extended = [(a, raw) for a, _, _, raw in regions if a in starts]
    count = live = 0
    husks = 0
    for path in snapshots():
        ram = path.read_bytes()
        if ram[0x810700] in (1, 6):
            # AREA06: a broken crate's +0x44 is the husk's library address
            for slot in range(400):
                a = 0x7A5640 + slot * 0x2F0
                if ram[a] and B.u32(ram, a + 0x10) == 0x1551B0 and B.u32(ram, a + 0x44) == base + husk:
                    husks += 1
            for address, raw in extended:
                if ram[address:address + len(raw)] != raw:
                    raise ValueError(f'{path.parent.name}: RAM differs within an extended span')
            continue
        if ram[0x810700] != 0x16:
            continue
        count += 1
        if B.u32(ram, B.TABLE + 4 * B.GLOBAL_TABLE_INDEX) != base:
            raise ValueError(f'{path.parent.name}: another library base')
        for address, raw in extended:
            if ram[address:address + len(raw)] != raw:
                raise ValueError(f'{path.parent.name}: RAM differs within the extended span')
        for slot in range(400):
            a = 0x7A5640 + slot * 0x2F0
            if not ram[a] or not L2.library_pickup(B.u32(ram, a + 0x10), ram[a + 3],
                                                    int.from_bytes(ram[a + 0x2E:a + 0x30], 'little')):
                continue
            model = ram[a + 0xD]
            if model not in spans or B.u32(ram, a + 0x44) != spans[model]:
                raise ValueError(f'{path.parent.name}: live pickup {slot} handle differs from its library model')
            live += 1
    if not count or not live or not husks:
        raise ValueError('no AREA22 snapshot with a live library pickup, or no AREA06 crate husk')
    return dict(models=len(spans), snapshots=count, live_handles=live, crate_husks=husks, regions=len(regions),
                extended_span_bytes=sum(len(raw) for _, raw in extended))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=ROOT / 'assets/scene_snow/roger/resources.emrs')
    args = parser.parse_args()
    output = args.out.absolute()
    if any(p.is_symlink() for p in (output, *output.parents)):
        raise ValueError('Output must use the worktree private directories (not a symlink)')
    previous = output.read_bytes()
    library = (B.DECOMP / 'extract/chunk27/f01_id37.bin').read_bytes()
    result = extend(previous, library)
    receipt = verify(result, library)
    receipt.update(bytes_added=len(result) - len(previous), sha256=hashlib.sha256(result).hexdigest())
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    output.write_bytes(result)
    print(f'middle route library: {receipt["models"]} models, {receipt["snapshots"]} AREA22 snapshots, '
          f'{receipt["live_handles"]} live handles; {receipt["bytes_added"]} bytes added, '
          f'{receipt["regions"]} regions')


if __name__ == '__main__':
    main()
