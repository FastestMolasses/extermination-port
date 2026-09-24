#!/usr/bin/env python3
"""Export the AREA11 box owners' ELF tables from the user's own boot ELF.

001551B0 (the crates) reads D_002468B0, the rattle table: 7 rows of 0x30
bytes, [row][column][B0, B4, B8] floats (docs/CRATES_DRUMS_ORIGINAL.md,
state 4). 00156620 (the drums) reads D_00246A00 (speed) and D_00246A10
(lift), four floats each, in its break state. The three tables lie in one
span, 0x2468B0..0x246A20, which is written whole.

Output (disc-derived: git-ignored assets/ only; nothing is embedded here):
  assets/scene_snow/box_tables.emrg: "EMRG", u32 version 1, u32 base
      (0x2468B0), u32 size (0x170), then the ELF's bytes base .. base + size.
      em_area11_boxes (src/game/em_area11_boxes.c) reads it.
Only the span bounds and the output's SHA-256 are printed.

Runs natively on arm64 macOS (pure Python).

Usage (port root):
  python3 tools/export_box_tables.py
"""
import argparse
import hashlib
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
BASE, END = 0x2468B0, 0x246A20      # D_002468B0 (7 x 0x30) .. D_00246A10 + 0x10


def elf_offset(address):
    """The boot ELF's single LOAD segment: file 0x300 -> vaddr 0x100000."""
    return address - 0x100000 + 0x300


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--decomp', type=Path, default=ROOT.parent / 'Extermination')
    ap.add_argument('--elf', type=Path, help='the boot ELF (default: <decomp>/config/SCUS_971.12)')
    ap.add_argument('--output', type=Path, default=ROOT / 'assets/scene_snow/box_tables.emrg')
    args = ap.parse_args()
    elf = (args.elf or args.decomp / 'config/SCUS_971.12').read_bytes()
    digest = hashlib.sha256(elf).hexdigest()
    if digest != ELF_SHA256:
        sys.exit(f'export_box_tables: ELF SHA-256 {digest} is not the pinned SCUS-97112 build')
    data = elf[elf_offset(BASE):elf_offset(END)]
    if len(data) != END - BASE:
        sys.exit('export_box_tables: the span lies outside the ELF')
    payload = struct.pack('<4sIII', b'EMRG', 1, BASE, len(data)) + data
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(payload)
    print(f'box tables: {BASE:#x}..{END:#x}, {len(payload)} bytes -> {args.output} '
          f'(sha256 {hashlib.sha256(payload).hexdigest()[:16]})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
