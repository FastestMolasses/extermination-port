#!/usr/bin/env python3
"""Export the walking camera's ELF tables from the user's own boot ELF.

The live camera (src/game/em_camera_live.c, docs/CAMERA_LIVE.md) reads two
tables of the boot ELF's data through 001B1EA0's polygon argument:
  D_0024A4B0          00190F20's area-0xE quad (4 XYZW vertices);
  D_0024A5F0 + 0x40*i 00194D10's region quads (00230000 passes i = 1 in
                      AREA11); the word at +4 of each is its reference height.
One span, 0x24A4B0..0x24A6F0, holds both and is written whole.

Output (disc-derived: git-ignored assets/ only; nothing is embedded here):
  assets/camera_tables.emrg: "EMRG", u32 version 1, u32 base (0x24A4B0),
      u32 size (0x240), then the ELF's bytes base .. base + size.
Only the span bounds and the output's SHA-256 are printed.

Runs natively on arm64 macOS (pure Python).

Usage (port root):
  python3 tools/export_camera_tables.py
"""
import argparse
import hashlib
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
BASE, END = 0x24A4B0, 0x24A6F0


def elf_offset(address):
    """The boot ELF's single LOAD segment: file 0x300 -> vaddr 0x100000."""
    return address - 0x100000 + 0x300


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--decomp', type=Path, default=ROOT.parent / 'Extermination')
    ap.add_argument('--elf', type=Path, help='the boot ELF (default: <decomp>/config/SCUS_971.12)')
    ap.add_argument('--output', type=Path, default=ROOT / 'assets/camera_tables.emrg')
    args = ap.parse_args()
    elf = (args.elf or args.decomp / 'config/SCUS_971.12').read_bytes()
    digest = hashlib.sha256(elf).hexdigest()
    if digest != ELF_SHA256:
        sys.exit(f'export_camera_tables: ELF SHA-256 {digest} is not the pinned SCUS-97112 build')
    data = elf[elf_offset(BASE):elf_offset(END)]
    if len(data) != END - BASE:
        sys.exit('export_camera_tables: the span lies outside the ELF')
    payload = struct.pack('<4sIII', b'EMRG', 1, BASE, len(data)) + data
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(payload)
    print(f'camera tables: {BASE:#x}..{END:#x}, {len(payload)} bytes -> {args.output} '
          f'(sha256 {hashlib.sha256(payload).hexdigest()[:16]})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
