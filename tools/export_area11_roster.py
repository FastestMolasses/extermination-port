#!/usr/bin/env python3
"""Export the AREA11 static roster (WP-3 S7) from the user's own ELF and overlay.

What the original state-0 spawners read (Extermination/src, byte-matched
001B6910/001B6660/001B6990):
- 001B6910: list = D_0024D820[D_00810700][D_00810701]; for every item of the
  list (do-while, stops at a zero word) 001B6660 walks 0x2C-byte records until
  the halfword -1.
- 001B6990: table = D_0024D7C0[D_00810700]; records = table[D_00810701]; 0x28-
  byte records until the halfword 0xFF.
New Game enters AREA11 with D_00810700 = 0x0B and D_00810701 = 0 (001AD360
step 4 writes 700 = 0x0B, 701 = 0). Pointers into 0x823500.. resolve in the
AREA11 overlay, which the loader places at its arena 0x823500 (the census test
checks that the captured RAM holds the same bytes).

The records are copied in their original byte layout into the ignored asset
assets/scene_snow/roster.emro (layout in src/game/em_actor_roster.c). Only
counts and addresses are printed.
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
OVERLAY_ARENA, OVERLAY_SIZE = 0x823500, 0x7800
D_0024D820, D_0024D7C0 = 0x24D820, 0x24D7C0
AREA, SUB = 0x0B, 0
DEFERRED_SIZE, PLACEMENT_SIZE = 0x2C, 0x28
VERSION = 1
MAX_RECORDS = 0x10000
MAX_GROUPS = 64


def elf_overlay_reader(elf: bytes, overlay: bytes):
    """read(address, size) over the ELF load segment and the AREA11 overlay."""
    if hashlib.sha256(elf).hexdigest() != ELF_SHA256:
        raise ValueError('Not the pinned SCUS-97112 boot ELF')
    if len(overlay) != OVERLAY_SIZE or overlay[:4] != b'MWo3' or \
            struct.unpack_from('<I', overlay, 8)[0] != OVERLAY_ARENA:
        raise ValueError('Not the original SCUS-97112 AREA11 overlay')

    def read(address: int, size: int) -> bytes:
        if ELF_VADDR <= address and address + size <= ELF_VADDR + ELF_FILESZ:
            at = address - ELF_VADDR + ELF_OFFSET
            return elf[at:at + size]
        if OVERLAY_ARENA <= address and address + size <= OVERLAY_ARENA + OVERLAY_SIZE:
            at = address - OVERLAY_ARENA
            return overlay[at:at + size]
        raise ValueError(f'address {address:#x} outside the ELF and the AREA11 overlay')
    return read


def walk_roster(read, area: int, sub: int):
    """Mirror the original table walks. Returns (groups, placement_address,
    placements) with groups = [(address, [record bytes...]), ...]."""
    u32 = lambda a: struct.unpack('<I', read(a, 4))[0]
    s16 = lambda a: struct.unpack('<h', read(a, 2))[0]
    groups = []
    registry = u32(D_0024D820 + 4 * area)
    if registry:                                   # 001B6910: p != 0
        q = u32(registry + 4 * sub)
        if not q:
            raise ValueError('D_0024D820 list pointer is 0 (the original would read address 0)')
        while True:                                # do { item = *q++; 001B6660(item); } while (*q)
            item = u32(q)
            if not item:
                raise ValueError('empty first item (the original would walk address 0)')
            records = []
            while s16(item + DEFERRED_SIZE * len(records)) != -1:
                records.append(read(item + DEFERRED_SIZE * len(records), DEFERRED_SIZE))
                if len(records) > MAX_RECORDS:
                    raise ValueError('unterminated deferred group')
            groups.append((item, records))
            q += 4
            if not u32(q):
                break
            if len(groups) >= MAX_GROUPS:
                raise ValueError('too many deferred groups')
    placement_address, placements = 0, []
    table = u32(D_0024D7C0 + 4 * area)
    if table:                                      # 001B6990: table == 0 -> return
        placement_address = u32(table + 4 * sub)
        if not placement_address:
            raise ValueError('placement pointer is 0 (the original would read address 0)')
        while s16(placement_address + PLACEMENT_SIZE * len(placements)) != 0xFF:
            placements.append(read(placement_address + PLACEMENT_SIZE * len(placements), PLACEMENT_SIZE))
            if len(placements) > MAX_RECORDS:
                raise ValueError('unterminated placement table')
    return groups, placement_address, placements


def encode_roster(area: int, sub: int, groups, placement_address: int, placements) -> bytes:
    out = bytearray(b'EMRO')
    out += struct.pack('<IBBHII I', VERSION, area, sub, len(groups), len(placements), placement_address, 0)
    for address, records in groups:
        out += struct.pack('<II', address, len(records))
    for _, records in groups:
        for record in records:
            out += record
    for record in placements:
        out += record
    return bytes(out)


def build_area11_roster(elf: bytes, overlay: bytes) -> bytes:
    groups, placement_address, placements = walk_roster(elf_overlay_reader(elf, overlay), AREA, SUB)
    return encode_roster(AREA, SUB, groups, placement_address, placements)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--decomp-root', type=Path, default=ROOT.parent/'Extermination')
    parser.add_argument('--out', type=Path, default=ROOT/'assets/scene_snow/roster.emro')
    args = parser.parse_args()
    elf = (args.decomp_root/'config/SCUS_971.12').read_bytes()
    overlay = (args.decomp_root/'extract/OVERLAY/AREA11.BIN').read_bytes()
    data = build_area11_roster(elf, overlay)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(data)
    groups, placement_address, placements = walk_roster(elf_overlay_reader(elf, overlay), AREA, SUB)
    print(f'{args.out}: area {AREA:#04x} sub {SUB}, {len(groups)} deferred group(s) '
          + ', '.join(f'{a:#x}x{len(r)}' for a, r in groups)
          + f', {len(placements)} placements at {placement_address:#x}, {len(data)} bytes')
    return 0


if __name__ == '__main__':
    sys.exit(main())
