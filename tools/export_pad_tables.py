#!/usr/bin/env python3
"""Export the pad rumble records from the user's own boot ELF.

001B1E20 (the rumble dispatch) indexes D_0024D6F0 with its effect id: 16
records of 4 bytes {big motor, small motor, default duration, pad}
(docs/OWNER_SERVICES.md; em_owner_services_001B1E20). em_pad_actuator
binds it on the live path (docs/TRUCK_ORIGINAL.md "Binding": the truck's
arm and fall rumbles).

Output (disc-derived: git-ignored assets/ only; nothing is embedded here):
  assets/pad_rumble.emrg: "EMRG", u32 version 1, u32 base (0x24D6F0),
      u32 size (0x40), then the ELF's bytes base .. base + size.
Only the span bounds and the output's SHA-256 are printed.

Runs natively on arm64 macOS (pure Python).

Usage (port root):
  python3 tools/export_pad_tables.py
"""
import argparse
import hashlib
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
BASE, END = 0x24D6F0, 0x24D730      # D_0024D6F0: 16 records x 4 bytes


def elf_offset(address):
    """The boot ELF's single LOAD segment: file 0x300 -> vaddr 0x100000."""
    return address - 0x100000 + 0x300


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--decomp', type=Path, default=ROOT.parent / 'Extermination')
    ap.add_argument('--elf', type=Path, help='the boot ELF (default: <decomp>/config/SCUS_971.12)')
    ap.add_argument('--output', type=Path, default=ROOT / 'assets/pad_rumble.emrg')
    args = ap.parse_args()
    elf = (args.elf or args.decomp / 'config/SCUS_971.12').read_bytes()
    digest = hashlib.sha256(elf).hexdigest()
    if digest != ELF_SHA256:
        sys.exit(f'export_pad_tables: ELF SHA-256 {digest} is not the pinned SCUS-97112 build')
    data = elf[elf_offset(BASE):elf_offset(END)]
    if len(data) != END - BASE:
        sys.exit('export_pad_tables: the span lies outside the ELF')
    payload = struct.pack('<4sIII', b'EMRG', 1, BASE, len(data)) + data
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(payload)
    print(f'pad tables: {BASE:#x}..{END:#x}, {len(payload)} bytes -> {args.output} '
          f'(sha256 {hashlib.sha256(payload).hexdigest()[:16]})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
