#!/usr/bin/env python3
"""Export the walking camera's ELF tables from the user's own boot ELF.

The live camera (src/game/em_camera_live.c, docs/CAMERA_LIVE.md) reads two
tables of the boot ELF's data through 001B1EA0's polygon argument:
  D_0024A4B0          00190F20's area-0xE quad (4 XYZW vertices);
  D_0024A5F0 + 0x40*i 00194D10's region quads (00230000 passes i = 1 in
                      AREA11); the word at +4 of each is its reference height.
One span, 0x24A4B0..0x24A6F0, holds both and is written whole.

The room camera seat reads a third table: 001B0460 (at the placement
001B07C0 makes) and 001B0300 (camera mode 1's re-seat) take the eye
D_0024A8D0 + (record word +0x10 >> 8) * 12 when the spawn record's +0x10
byte has bit 7 (camera mode 1). Its 12-byte XYZ rows run from D_0024A8D0 up
to the first spawn entry array (0x24AA50, the lowest record array
tools/export_spawn_table.py walks): 32 rows. The exporter checks that every
mode-1 record of D_0024D650 indexes a row inside that span (AREA01 room 0
entries 1 and 8 use rows 2 and 6; AREA11 has none).

Output (disc-derived: git-ignored assets/ only; nothing is embedded here):
  assets/camera_tables.emrg: "EMRG", u32 version 1, u32 base (0x24A4B0),
      u32 size (0x240), then the ELF's bytes base .. base + size.
  assets/camera_eye_rows.emrg: the same layout for D_0024A8D0 (size 0x180).
Only the span bounds and the outputs' SHA-256 are printed.

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
EYE_BASE, EYE_ROW = 0x24A8D0, 12


def elf_offset(address):
    """The boot ELF's single LOAD segment: file 0x300 -> vaddr 0x100000."""
    return address - 0x100000 + 0x300


def eye_span(elf):
    """D_0024A8D0 .. the first spawn entry array, after checking that every
    mode-1 spawn record (+0x10 bit 7) indexes a whole row inside it."""
    sys.path.insert(0, str(ROOT / 'tools'))
    import export_spawn_table as spawn
    read = spawn.elf_reader(elf)
    areas, ranges = spawn.walk(read)
    floor = min(table for table, _ in areas.values())
    starts = sorted({entries for _, rooms in areas.values() for entries in rooms})
    end = starts[0]
    if end <= EYE_BASE or (end - EYE_BASE) % EYE_ROW:
        sys.exit(f'export_camera_tables: eye rows {EYE_BASE:#x}..{end:#x} are not whole 12-byte rows')
    used = set()
    for i, entries in enumerate(starts):
        stop = starts[i + 1] if i + 1 < len(starts) else floor
        for at in range(entries, stop, 0x30):
            flags = struct.unpack('<i', read(at + 0x10, 4))[0]
            if flags & 0x80:
                row = flags >> 8                 # 001B05C4: arithmetic shift
                if not 0 <= row < (end - EYE_BASE) // EYE_ROW:
                    sys.exit(f'export_camera_tables: record {at:#x} indexes eye row {row} outside the span')
                used.add(row)
    return end, sorted(used)


def write_span(path, elf, base, end):
    data = elf[elf_offset(base):elf_offset(end)]
    if len(data) != end - base:
        sys.exit('export_camera_tables: the span lies outside the ELF')
    payload = struct.pack('<4sIII', b'EMRG', 1, base, len(data)) + data
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)
    return payload


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--decomp', type=Path, default=ROOT.parent / 'Extermination')
    ap.add_argument('--elf', type=Path, help='the boot ELF (default: <decomp>/config/SCUS_971.12)')
    ap.add_argument('--output', type=Path, default=ROOT / 'assets/camera_tables.emrg')
    ap.add_argument('--eye-output', type=Path, default=ROOT / 'assets/camera_eye_rows.emrg')
    args = ap.parse_args()
    elf = (args.elf or args.decomp / 'config/SCUS_971.12').read_bytes()
    digest = hashlib.sha256(elf).hexdigest()
    if digest != ELF_SHA256:
        sys.exit(f'export_camera_tables: ELF SHA-256 {digest} is not the pinned SCUS-97112 build')
    payload = write_span(args.output, elf, BASE, END)
    print(f'camera tables: {BASE:#x}..{END:#x}, {len(payload)} bytes -> {args.output} '
          f'(sha256 {hashlib.sha256(payload).hexdigest()[:16]})')
    eye_end, used = eye_span(elf)
    payload = write_span(args.eye_output, elf, EYE_BASE, eye_end)
    print(f'camera eye rows: {EYE_BASE:#x}..{eye_end:#x} ({(eye_end - EYE_BASE) // EYE_ROW} rows; '
          f'{len(used)} indexed by mode-1 spawn records), {len(payload)} bytes -> {args.eye_output} '
          f'(sha256 {hashlib.sha256(payload).hexdigest()[:16]})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
