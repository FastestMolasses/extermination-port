#!/usr/bin/env python3
"""export_area_title.py - the boot ELF data the area-title node reads.

001C5930 (the area-title node, em_sul_001C5930; docs/STATUS_UI_LEFTOVERS.md
2.6, bound live by src/game/em_area_title.c) and its boot-time table builder
001B0BA0 read static .data of the boot ELF:
  D_0024A850  the 23 halfword counts 001B0BA0 builds D_00289B40 from
  D_002671C0  the title string pointers (D_00289B40[area].base + sub)
  D_0026726C  the band line pointers (001C5860's band 0..5; entry 0 is 0)
  the strings those pointers name (one span)
  D_00265520  the text style record of the band-5 line (001CC1E0's style)
This tool copies them from the user's own pinned boot ELF
(config/SCUS_971.12, file offset = address - 0x100000 + 0x300) into an
ignored asset; nothing is embedded here. Each block is checked against the
route captures' RAM (the same bytes at the same addresses). The layout is
EMET's (tools/export_effect_tables.py) with the magic 'EMAT': a 16-byte
header (magic, version 1, block count, 0), 16-byte block records (address,
size, file offset, 0), the blocks padded to 16 bytes."""
from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
COUNTS, AREAS = 0x0024A850, 23
TITLES, BANDS, BAND_COUNT = 0x002671C0, 0x0026726C, 6
STYLE, STYLE_SIZE = 0x00265520, 8


def at(address: int) -> int:
    offset = address - 0x100000 + 0x300
    if offset < 0x300 or offset >= 0x175E00:
        raise SystemExit(f'{address:#x}: outside the loadable section')
    return offset


def elf_block(elf: bytes, address: int, size: int) -> bytes:
    at(address + size - 1)
    return elf[at(address):at(address) + size]


def string_end(elf: bytes, address: int) -> int:
    o = at(address)
    n = elf.index(b'\0', o)
    return address + (n - o) + 1


def blocks_of(elf: bytes):
    counts = struct.unpack_from(f'<{AREAS}h', elf, at(COUNTS))
    titles = sum(c if c else 1 for c in counts)          # 001B0BA0's last base
    words = struct.unpack_from(f'<{titles}I', elf, at(TITLES))
    bands = struct.unpack_from(f'<{BAND_COUNT}I', elf, at(BANDS))
    if BANDS < TITLES + 4 * titles:
        raise SystemExit('the band table overlaps the title table')
    pointers = [p for p in words + bands if p]
    lo, hi = min(pointers), max(string_end(elf, p) for p in pointers)
    return [(COUNTS, elf_block(elf, COUNTS, 2 * AREAS)),
            (TITLES, elf_block(elf, TITLES, BANDS + 4 * BAND_COUNT - TITLES)),
            (lo, elf_block(elf, lo, hi - lo)),
            (STYLE, elf_block(elf, STYLE, STYLE_SIZE))]


def serialize(blocks) -> bytes:
    head = struct.pack('<4s3I', b'EMAT', 1, len(blocks), 0)
    table, body = b'', b''
    offset = 0x10 + 0x10 * len(blocks)
    for address, data in blocks:
        table += struct.pack('<4I', address, len(data), offset + len(body), 0)
        body += data + bytes(-len(data) % 16)
    return head + table + body


def verify(blocks, ram: bytes, path: Path) -> int:
    for address, data in blocks:
        if ram[address:address + len(data)] != data:
            diff = next(i for i in range(len(data)) if ram[address + i] != data[i])
            raise SystemExit(f'{path}: {address + diff:#x} differs from the ELF')
    return sum(len(d) for _a, d in blocks)


def default_captures():
    ref = DECOMP / 'build/startup-reference'
    route = DECOMP / 'build/s87/route'
    fixed = [ref / 'opening_ee.bin', ref / 'playable_ee.bin']
    return [p for p in fixed if p.exists()] + sorted(
        p for p in route.glob('*/eeMemory.bin') if not p.parent.name.startswith('15'))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--elf', type=Path, default=DECOMP / 'config/SCUS_971.12')
    ap.add_argument('--out', type=Path, default=ROOT / 'assets/area_title.emat')
    ap.add_argument('--verify-ram', type=Path, action='append', default=None)
    ap.add_argument('--no-verify', action='store_true')
    args = ap.parse_args(argv)
    elf = args.elf.read_bytes()
    if hashlib.sha256(elf).hexdigest() != ELF_SHA256:
        raise SystemExit(f'{args.elf}: not the pinned SCUS-97112 boot ELF')
    blocks = blocks_of(elf)
    captures = [] if args.no_verify else (args.verify_ram or default_captures())
    compared = sum(verify(blocks, p.read_bytes(), p) for p in captures)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(serialize(blocks))
    print(f'wrote {args.out}: {len(blocks)} blocks, {sum(len(d) for _, d in blocks)} bytes; '
          f'{compared} bytes equal in {len(captures)} captures')
    return 0


if __name__ == '__main__':
    sys.exit(main())
