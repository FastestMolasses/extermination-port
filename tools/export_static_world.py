#!/usr/bin/env python3
"""export_static_world.py - the AREA11 static-object bank and the .data the
background channel uploads, for em_static_world (docs/STATIC_WORLD.md).

The bank. 001D52E0 and 001D5370 read the static-object bank through the word
D_0028A5A0 (resource 0x44 of AREA11; the loader leaves it at 0x01516F40 in
every AREA11 capture). Its bytes are the user's own extracted disc files: the
chunk15 files concatenated in index order (f00, f01, ... f18) hold the bank
from concatenation offset 0x304000 (f12_id44.bin + 0x123000; the bank runs on
through f13..f17). Its extent is read from the bank itself, not assumed:
  word 0 = the entry count n; words 1..n = entry offsets (001C6120: bank +
  (word[1 + id] >> 2 << 2));
  entry 0 = the grid header 001D52E0 publishes: rows (+0), stride (+4), six
  floats, then rows x stride x 4 id words from +0x20;
  entries 1..n-1 = objects: count at +0, the AABB at +0x10 / +0x20, and
  count blocks of 0x820 bytes from +0x40 (001D4F30 / 001D4A90 REF them).
The exported span ends at the last entry's end, rounded up to 16 bytes.

The .data block. 001E1E60 uploads D_00253560 (16 bytes) and D_00253570 (0x80
bytes) and writes D_00253570..D_002535AF and D_002535B8 every frame; the
block 0x00253560..0x002535EF is copied from the user's pinned boot ELF.

--verify-ram (default: the opening, playable and handoff captures, every
AREA11 route beat and every c7cap capture) checks, per capture: the area key
is 0x0B00, D_0028A5A0 equals the bank address, the whole bank span equals RAM
byte for byte, every entry lies inside the span, and the .data block equals
RAM except the run-time words above. A capture of another area (the route's
15_level_exit) is listed and skipped.

Output (disc-derived: git-ignored assets/ only):
  assets/scene_snow/static_world.emsw, little-endian:
    0x00 'EMSW', u32 version 1, u32 block count, u32 the bank address
    0x10 count x (u32 address, u32 size, u32 file offset, u32 0)
    then the block bytes at their offsets (16-aligned)
  block 0 is the bank (at the bank address), block 1 the .data block.

Runs natively on arm64 macOS (pure Python).

Usage (port root):
  python3 tools/export_static_world.py
"""
from __future__ import annotations

import argparse
import hashlib
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
BANK_WORD = 0x0028A5A0
BANK_ADDRESS = 0x01516F40
CONCAT_OFFSET = 0x304000          # f12_id44.bin + 0x123000
AREA_KEY = 0x0B00
DATA_BLOCK = (0x00253560, 0x90)
# written by 001E1E60 every frame (address, bytes): not compared
DATA_RUNTIME = ((0x00253570, 0x40), (0x002535B8, 4))
BLOCK_BYTES = 0x820


def u32(buf, at):
    return struct.unpack_from('<I', buf, at)[0]


def concatenation(folder: Path) -> bytes:
    files = []
    for p in folder.glob('f*_id*.bin'):
        m = re.match(r'f(\d+)_id[0-9a-fA-F]+\.bin$', p.name)
        if m:
            files.append((int(m.group(1)), p))
    files.sort()
    if [i for i, _ in files] != list(range(len(files))):
        raise SystemExit(f'{folder}: the chunk files are not f00..f{len(files) - 1:02d}')
    return b''.join(p.read_bytes() for _, p in files)


def bank_extent(cat: bytes, base: int) -> tuple[int, list[tuple[int, int]]]:
    """(span, [(entry offset, entry end)]) from the bank's own table."""
    n = u32(cat, base)
    if not 1 < n < 0x10000:
        raise SystemExit(f'bank entry count {n:#x}: not a bank')
    entries = []
    for i in range(n):
        off = (u32(cat, base + 4 + 4 * i) >> 2) << 2           # 001C6120 (sra, then sll)
        if off & 0x80000000 or base + off + 0x40 > len(cat):
            raise SystemExit(f'bank entry {i}: offset {off:#x} outside the files')
        o = base + off
        if i == 0:
            rows, stride = u32(cat, o), u32(cat, o + 4)
            end = off + 0x20 + rows * stride * 16
        else:
            count = u32(cat, o)
            if count & 0x80000000 or count > 0x10000:
                raise SystemExit(f'bank entry {i}: block count {count:#x}')
            end = off + 0x40 + count * BLOCK_BYTES
        entries.append((off, end))
    span = (max(e for _, e in entries) + 15) & ~15
    if base + span > len(cat):
        raise SystemExit(f'bank span {span:#x} runs past the files')
    return span, entries


def elf_block(elf: bytes, address: int, size: int) -> bytes:
    offset = address - 0x100000 + 0x300
    if offset < 0x300 or offset + size > 0x175E00:
        raise SystemExit(f'{address:#x}: outside the loadable section')
    return elf[offset:offset + size]


def serialize(bank_address: int, blocks) -> bytes:
    head = struct.pack('<4s3I', b'EMSW', 1, len(blocks), bank_address)
    table, body = b'', b''
    offset = 0x10 + 0x10 * len(blocks)
    for address, data in blocks:
        at = offset + len(body)
        table += struct.pack('<4I', address, len(data), at, 0)
        body += data + bytes(-len(data) % 16)
    return head + table + body


def default_captures():
    ref = DECOMP / 'build/startup-reference'
    out = [ref / n for n in ('opening_ee.bin', 'playable_ee.bin', 'handoff_ee.bin') if (ref / n).exists()]
    out += sorted((DECOMP / 'build/s87/route').glob('*/eeMemory.bin'))
    out += sorted((DECOMP / 'build/s87/c7cap').glob('**/eeMemory.bin'))
    return out


def verify(path: Path, bank_address: int, bank: bytes, entries, data: bytes):
    ram = path.read_bytes()
    key = ram[0x810700] << 8 | ram[0x810701]
    word = u32(ram, BANK_WORD)
    if key != AREA_KEY:
        return f'skipped: area key {key:#06x} (not AREA11)'
    if word != bank_address:
        raise SystemExit(f'{path}: D_0028A5A0 = {word:#x}, bank address {bank_address:#x}')
    at = bank_address & 0x1FFFFFF
    got = ram[at:at + len(bank)]
    if got != bank:
        diff = next(i for i in range(len(bank)) if got[i] != bank[i])
        raise SystemExit(f'{path}: bank byte {diff:#x} (RAM {bank_address + diff:#x}) differs')
    for i, (off, end) in enumerate(entries):
        if end > len(bank):
            raise SystemExit(f'{path}: entry {i} ends at {end:#x}, past the span')
    a0, n0 = DATA_BLOCK
    compared = 0
    for i, b in enumerate(data):
        a = a0 + i
        if any(r <= a < r + n for r, n in DATA_RUNTIME):
            continue
        if ram[a] != b:
            raise SystemExit(f'{path}: {a:#x}: ELF {b:#04x}, captured {ram[a]:#04x}')
        compared += 1
    return f'{len(bank)} bank bytes + {compared} .data bytes equal'


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--chunks', type=Path, default=DECOMP / 'extract/chunk15')
    ap.add_argument('--elf', type=Path, default=DECOMP / 'config/SCUS_971.12')
    ap.add_argument('--bank-address', type=lambda s: int(s, 0), default=BANK_ADDRESS)
    ap.add_argument('--out', type=Path, default=ROOT / 'assets/scene_snow/static_world.emsw')
    ap.add_argument('--verify-ram', type=Path, action='append', default=None)
    ap.add_argument('--no-verify', action='store_true')
    args = ap.parse_args(argv)
    elf = args.elf.read_bytes()
    if hashlib.sha256(elf).hexdigest() != ELF_SHA256:
        raise SystemExit(f'{args.elf}: not the pinned SCUS-97112 boot ELF')
    cat = concatenation(args.chunks)
    span, entries = bank_extent(cat, CONCAT_OFFSET)
    bank = cat[CONCAT_OFFSET:CONCAT_OFFSET + span]
    data = elf_block(elf, *DATA_BLOCK)
    captures = [] if args.no_verify else (args.verify_ram or default_captures())
    checked = 0
    for p in captures:
        line = verify(p, args.bank_address, bank, entries, data)
        if not line.startswith('skipped'):
            checked += 1
        print(f'  {p.relative_to(DECOMP) if DECOMP in p.parents else p}: {line}')
    if captures and not checked:
        raise SystemExit('no AREA11 capture was checked')
    blocks = [(args.bank_address, bank), (DATA_BLOCK[0], data)]
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(serialize(args.bank_address, blocks))
    objects = len(entries) - 1
    blocks_total = sum((e - o - 0x40) // BLOCK_BYTES for o, e in entries[1:])
    print(f'wrote {args.out}: bank {args.bank_address:#x} + {span:#x} bytes ({objects} objects, '
          f'{blocks_total} blocks), .data {DATA_BLOCK[0]:#x} + {DATA_BLOCK[1]:#x}; '
          f'{checked} AREA11 captures equal byte for byte')
    return 0


if __name__ == '__main__':
    sys.exit(main())
