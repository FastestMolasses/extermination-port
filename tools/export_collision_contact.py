#!/usr/bin/env python3
"""Export the ELF data the collision world's contact pass reads.

001A8660 (the player x class-0xD contact of 001A8BE0, em_coll_list_passes.c)
reads the player's radius and height through the word 0015C420 stores at the
player record's +0x30: the address of D_00275490 (byte-matched decomp
src/func_0015C420.c). The two floats lie in the boot ELF's .data; this tool
copies those 8 bytes from the user's own ELF.

After a contact the same routine reads its knock-back amount from one of two
.data tables, D_0024A740 or D_0024A780 (= D_0024A740 + 0x40, picked by
D_0081070A), at +4 * the class-0xD entry's +0x0D byte; the second file spans
every byte value of both tables (0x24A740 .. 0x24A740 + 0x40 + 4 * 0xFF + 4).

Output (disc-derived: git-ignored assets/ only; nothing is embedded here):
  assets/collision_contact.emrg: "EMRG", u32 version 1, u32 base
      (0x275490), u32 size (8), then the ELF's bytes base .. base + size.
  assets/collision_knockback.emrg: the same layout, base 0x24A740, size
      0x440.
      em_collision_world (src/game/em_collision_world.c) reads both.
Only the span bounds and the output's SHA-256 are printed.

Runs natively on arm64 macOS (pure Python).

Usage (port root):
  python3 tools/export_collision_contact.py
"""
import argparse
import hashlib
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
SPANS = (
    ('collision_contact.emrg', 0x275490, 0x275498),     # D_00275490: two floats
    ('collision_knockback.emrg', 0x24A740, 0x24AB80),   # D_0024A740 / D_0024A780, every d
)


def elf_offset(address):
    """The boot ELF's single LOAD segment: file 0x300 -> vaddr 0x100000."""
    return address - 0x100000 + 0x300


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--decomp', type=Path, default=ROOT.parent / 'Extermination')
    ap.add_argument('--elf', type=Path, help='the boot ELF (default: <decomp>/config/SCUS_971.12)')
    ap.add_argument('--out-dir', type=Path, default=ROOT / 'assets')
    args = ap.parse_args()
    elf = (args.elf or args.decomp / 'config/SCUS_971.12').read_bytes()
    digest = hashlib.sha256(elf).hexdigest()
    if digest != ELF_SHA256:
        sys.exit(f'export_collision_contact: ELF SHA-256 {digest} is not the pinned SCUS-97112 build')
    for name, base, end in SPANS:
        data = elf[elf_offset(base):elf_offset(end)]
        if len(data) != end - base:
            sys.exit(f'export_collision_contact: {base:#x}..{end:#x} lies outside the ELF')
        payload = struct.pack('<4sIII', b'EMRG', 1, base, len(data)) + data
        output = args.out_dir / name
        output.parent.mkdir(parents=True, exist_ok=True)
        if output.is_symlink():
            output.unlink()   # a worktree's link to a shared tree: write a file of its own
        output.write_bytes(payload)
        print(f'collision data: {base:#x}..{end:#x}, {len(payload)} bytes -> {output} '
              f'(sha256 {hashlib.sha256(payload).hexdigest()[:16]})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
