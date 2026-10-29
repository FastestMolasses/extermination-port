#!/usr/bin/env python3
"""Export the use owners' descriptor floats from the boot ELF's .sdata.

The use owners store the address of a gp-relative descriptor in their
record's +0x30 (decomp C byte-matched: 00159620 / 00159970 D_00275458,
0015A070 D_00275460, 00158810 D_00275468, 0015AC00 D_00275470, 00159210
D_00275478 / D_00275480, 00158EC0 D_00275480, 00159B90 D_00275488); the
Use scan's candidate test 00183EF0 reads two to three floats there (its
distance, height and facing gates). No code writes them. This tool copies
D_00275458 .. D_00275490 from the user's own ELF.

Output (disc-derived: git-ignored assets/ only; nothing is embedded here):
  assets/use_descriptors.emrg: "EMRG", u32 version 1, u32 base (0x275458),
      u32 size (0x38), then the ELF's bytes base .. base + size;
      em_area01_live (src/game/em_area01_live.c) serves them read-only.
Only the span bounds and the output's SHA-256 are printed.

Runs natively on arm64 macOS (pure Python).

Usage (port root):
  python3 tools/export_use_descriptors.py
"""
import argparse
import hashlib
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
BASE, END = 0x275458, 0x275490


def elf_offset(address):
    """The boot ELF's single LOAD segment: file 0x300 -> vaddr 0x100000."""
    return address - 0x100000 + 0x300


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--decomp', type=Path, default=ROOT.parent / 'Extermination')
    ap.add_argument('--elf', type=Path, help='the boot ELF (default: <decomp>/config/SCUS_971.12)')
    ap.add_argument('--out', type=Path, default=ROOT / 'assets/use_descriptors.emrg')
    args = ap.parse_args()
    elf = (args.elf or args.decomp / 'config/SCUS_971.12').read_bytes()
    digest = hashlib.sha256(elf).hexdigest()
    if digest != ELF_SHA256:
        sys.exit(f'export_use_descriptors: ELF SHA-256 {digest} is not the pinned SCUS-97112 build')
    data = elf[elf_offset(BASE):elf_offset(END)]
    if len(data) != END - BASE:
        sys.exit(f'export_use_descriptors: {BASE:#x}..{END:#x} lies outside the ELF')
    payload = struct.pack('<4sIII', b'EMRG', 1, BASE, len(data)) + data
    args.out.parent.mkdir(parents=True, exist_ok=True)
    if args.out.is_symlink():
        args.out.unlink()   # a worktree's link to a shared tree: write a file of its own
    args.out.write_bytes(payload)
    print(f'use descriptors: {BASE:#x}..{END:#x}, {len(payload)} bytes -> {args.out} '
          f'(sha256 {hashlib.sha256(payload).hexdigest()[:16]})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
