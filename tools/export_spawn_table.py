#!/usr/bin/env python3
"""Export the area spawn table D_0024D650 (WP-3 S12a) from the user's own ELF.

What the originals read (Extermination/src, byte-matched 001B07C0/001B0250,
and 001B0460):
  table   = D_0024D650[D_00810700]     (lw, area byte, no bound)
  entries = table[D_00810701]          (lw, room byte, no bound)
  record  = entries + D_00810702 * 0x30 (entry byte, no bound)
The asset is an address-mapped window of the ELF data holding exactly what
that walk reaches for the areas the array names: D_0024D650[0..0x16] (entry
0x17 is the first zero word; the words after it are floats of other data),
each non-null area's room pointer array (the arrays lie 0x10 bytes apart in
both of their groups, 0x24D600.. and 0x275500.., so at most four words: up to
and including the first zero word, or all four), and each room's entry
array, bounded by the next entry array (or by the first room pointer array)
in address order. A room pointer that repeats an earlier one (area 8 rooms 0
and 1) adds nothing. Every range is checked to be a whole number of 0x30-byte
records. The native walk faults on any read
outside the window (src/game/em_spawn_table.c).

Output: assets/spawn/spawn_table.emsp (ignored; layout in
src/game/em_spawn_table.c). Only counts and addresses are printed.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
ELF_VADDR, ELF_OFFSET, ELF_FILESZ = 0x100000, 0x300, 0x175B00
D_0024D650 = 0x24D650
AREAS = 0x17          # D_0024D650[0x17] is the first zero word
RECORD = 0x30
VERSION = 1
ROOM_WORDS = 4        # room pointer arrays are 0x10 apart (checked below)


def elf_reader(elf: bytes):
    if hashlib.sha256(elf).hexdigest() != ELF_SHA256:
        raise ValueError('Not the pinned SCUS-97112 boot ELF')

    def read(address: int, size: int) -> bytes:
        if ELF_VADDR <= address and address + size <= ELF_VADDR + ELF_FILESZ:
            at = address - ELF_VADDR + ELF_OFFSET
            return elf[at:at + size]
        raise ValueError(f'address {address:#x} outside the ELF load segment')
    return read


def walk(read):
    """Returns (areas, ranges): areas[area] = (table, [entry array per room]);
    ranges = sorted disjoint (address, size)."""
    u32 = lambda a: struct.unpack('<I', read(a, 4))[0]
    if u32(D_0024D650 + 4 * AREAS) != 0:
        raise ValueError('D_0024D650[0x17] is not the zero word the export stops at')
    areas, tables, entry_starts = {}, [], set()
    for area in range(AREAS):
        table = u32(D_0024D650 + 4 * area)
        if not table:
            continue
        rooms = []
        while len(rooms) < ROOM_WORDS:
            entries = u32(table + 4 * len(rooms))
            if not entries:
                break
            rooms.append(entries)
        tables.append((table, 4 * min(ROOM_WORDS, len(rooms) + 1)))
        entry_starts.update(rooms)
        areas[area] = (table, rooms)
    ordered = sorted(address for address, _ in tables)
    for a, b in zip(ordered, ordered[1:]):
        if b - a < 4 * ROOM_WORDS and b >> 16 == a >> 16:
            raise ValueError(f'room pointer arrays {a:#x} and {b:#x} are closer than 0x10')
    table_floor = min(address for address, _ in tables)
    starts = sorted(entry_starts)
    ranges = [(D_0024D650, 4 * AREAS)] + tables
    for i, start in enumerate(starts):
        end = starts[i + 1] if i + 1 < len(starts) else min(table_floor, D_0024D650)
        if end <= start or (end - start) % RECORD:
            raise ValueError(f'entry array {start:#x}..{end:#x} is not whole 0x30-byte records')
        ranges.append((start, end - start))
    ranges.sort()
    merged = []
    for address, size in ranges:
        if merged and address < merged[-1][0] + merged[-1][1]:
            raise ValueError(f'overlapping ranges at {address:#x}')
        if merged and address == merged[-1][0] + merged[-1][1]:
            merged[-1] = (merged[-1][0], merged[-1][1] + size)
        else:
            merged.append((address, size))
    return areas, merged


def encode(read, ranges) -> bytes:
    out = bytearray(b'EMSP')
    out += struct.pack('<III', VERSION, len(ranges), 0)
    for address, size in ranges:
        out += struct.pack('<II', address, size)
    for address, size in ranges:
        out += read(address, size)
    return bytes(out)


def build_spawn_table(elf: bytes) -> bytes:
    read = elf_reader(elf)
    _, ranges = walk(read)
    return encode(read, ranges)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--decomp-root', type=Path, default=ROOT.parent / 'Extermination')
    parser.add_argument('--out', type=Path, default=ROOT / 'assets/spawn/spawn_table.emsp')
    args = parser.parse_args()
    elf = (args.decomp_root / 'config/SCUS_971.12').read_bytes()
    read = elf_reader(elf)
    areas, ranges = walk(read)
    data = encode(read, ranges)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(data)
    rooms = sum(len(r) for _, r in areas.values())
    print(f'{args.out}: {len(areas)} area(s), {rooms} room pointer(s), {len(ranges)} range(s), '
          f'{sum(s for _, s in ranges)} table bytes, {len(data)} bytes')
    return 0


if __name__ == '__main__':
    sys.exit(main())
