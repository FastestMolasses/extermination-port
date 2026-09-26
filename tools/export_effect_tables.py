#!/usr/bin/env python3
"""export_effect_tables.py - the boot ELF data the live effects read.

The effect originals bound live by src/game/em_effects_live.c and
src/game/em_equipment_live.c (census lanes L26 / L27 / L28 / L39;
docs/EFFECT_MANAGER.md section 8) read static .data of the boot ELF: the
effect entity tables, the subtype step / handler pairs, the effect manager's
windows, the glow-marker and point-light lists, the handlers' source blocks,
the head sprite's entries and packet rows, and the equipment routines' offset
tables. This tool copies those from the user's own pinned boot ELF
(config/SCUS_971.12, file offset = address - 0x100000 + 0x300) into an
ignored asset; nothing is embedded here. The runtime places each block at its
address in an otherwise empty ELF-sized image and hands that image to the
translations' own table loaders (em_effect_original_load_tables,
em_effect_manager_load_tables, em_effect_kinds_load_tables,
em_head_sprite_original_load_tables), which read only these windows.

Blocks (original address, bytes, what reads them):
  0x00257C90  0x2460  D_00259C70's entity records (001EF9D0), D_00259C70,
                      D_00259C74[23], and the manager's D_00259CD0 colour
                      records (001F0720) and D_00259DD0.. sprite records
                      (001F1180's draw block)
  0x00255430  0x158   D_00255430: the 0x2B {step, handler} pairs (001EA240)
  0x0025A350  0x34B0  D_0025A350 (001F40C0's records), the lists
                      0x25AD80..0x25D800 (001F5640 / 001F5CA0 / 001F6760 /
                      001F6D60) and D_0025CA40 (001F6210's keys)
  0x0026EB20  0x90    D_0026EB20 / D_0026EB60 (001F6210) and D_0026EB70
                      (001F6640's colour presets)
  0x002565E0  0x480   the handlers' source blocks D_002565E0, D_00256670,
                      D_00256700, D_002568B0, D_00256940, D_002569D0
                      (001CFBE0's a2, 0x90 bytes each)
  0x002535F0  0x110   D_002535F0 (001E23A0's entries) and D_00253670 (the
                      head sprite's source block)
  0x00251260  0x80    D_00251260: 001CFBE0's rows
  0x0024A220  0x290   D_0024A220..D_0024A4AF (00188630 / 0018A1F0's rows)
  0x00248B98  8       D_00248B98's first halfword (00188B80)
  0x00248C78  8       D_00248C78's first halfword (00188B80)

--verify-ram (default: the opening capture, the playable capture and every
AREA11 route capture 00..14) checks that each block equals captured RAM.

Output (disc-derived: git-ignored assets/ only):
  assets/effect_tables.emet, little-endian:
    0x00 'EMET', u32 version 1, u32 block count, u32 0
    0x10 count x (u32 address, u32 size, u32 file offset, u32 0)
    then the block bytes at their offsets (16-aligned)

Runs natively on arm64 macOS (pure Python).

Usage (port root):
  python3 tools/export_effect_tables.py
"""
from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
BLOCKS = ((0x00257C90, 0x2460), (0x00255430, 0x158), (0x0025A350, 0x34B0), (0x0026EB20, 0x90),
          (0x002565E0, 0x480), (0x002535F0, 0x110), (0x00251260, 0x80), (0x0024A220, 0x290),
          (0x00248B98, 8), (0x00248C78, 8))


def elf_block(elf: bytes, address: int, size: int) -> bytes:
    offset = address - 0x100000 + 0x300
    if offset < 0x300 or offset + size > 0x175E00:
        raise SystemExit(f'{address:#x}: outside the loadable section')
    return elf[offset:offset + size]


def serialize(blocks) -> bytes:
    head = struct.pack('<4s3I', b'EMET', 1, len(blocks), 0)
    table, body = b'', b''
    offset = 0x10 + 0x10 * len(blocks)
    for address, data in blocks:
        at = offset + len(body)
        table += struct.pack('<4I', address, len(data), at, 0)
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
    ap.add_argument('--out', type=Path, default=ROOT / 'assets/effect_tables.emet')
    ap.add_argument('--verify-ram', type=Path, action='append', default=None)
    ap.add_argument('--no-verify', action='store_true')
    args = ap.parse_args(argv)
    elf = args.elf.read_bytes()
    if hashlib.sha256(elf).hexdigest() != ELF_SHA256:
        raise SystemExit(f'{args.elf}: not the pinned SCUS-97112 boot ELF')
    blocks = [(a, elf_block(elf, a, n)) for a, n in BLOCKS]
    captures = [] if args.no_verify else (args.verify_ram or default_captures())
    compared = sum(verify(blocks, p.read_bytes(), p) for p in captures)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(serialize(blocks))
    print(f'wrote {args.out}: {len(blocks)} blocks, {sum(len(d) for _, d in blocks)} bytes; '
          f'{compared} bytes equal in {len(captures)} captures')
    return 0


if __name__ == '__main__':
    sys.exit(main())
