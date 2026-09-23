#!/usr/bin/env python3
"""Export the player's clip-rate column D_00248C98 from the user's ELF.

0015BA50 sets the player's source-advance step +34 to
D_00248C98[*(short *)(p + 0x20C) * 3] * +204: the float at +8 of row +20C
of D_00248C90, a table of 12-byte rows indexed by the player's clip id.
It has one row per clip of the player's clip bank (the bank's count word,
459; tools/test_player_footstep_reference.py walks the same 459 rows). The
row after the last one is all zeros.

The output is written into the port's ignored assets/ (never committed):
  "EMCR", u32 version 1, u32 count (459), then count little-endian floats.
em_player_clip_rates_load (src/game/em_player_stage_workers.c) reads it.
A report (source SHA-256, the output's SHA-256, the distinct-value count)
goes to the ignored build/player_tables/export.json (--report). No original data is
embedded in this script.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
TABLE, STRIDE, RATE_AT, ROWS = 0x248C90, 12, 8, 459
BANK = 'extract/chunk28/f01_id3c.bin'   # the player's clip bank (count word at +0)


def elf_offset(address):
    """The boot ELF's single LOAD segment: file 0x300 -> vaddr 0x100000."""
    return address - 0x100000 + 0x300


def rates(elf, rows=ROWS):
    return [struct.unpack_from('<f', elf, elf_offset(TABLE) + STRIDE * row + RATE_AT)[0]
            for row in range(rows)]


def encode(values):
    return struct.pack('<4sII', b'EMCR', 1, len(values)) + struct.pack(f'<{len(values)}f', *values)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--decomp', type=Path, default=ROOT.parent / 'Extermination')
    ap.add_argument('--elf', type=Path, help='the boot ELF (default: <decomp>/config/SCUS_971.12)')
    ap.add_argument('--output', type=Path, default=ROOT / 'assets/player_clip_rates.emcr')
    ap.add_argument('--report', type=Path, default=ROOT / 'build/player_tables/export.json')
    args = ap.parse_args()
    elf = (args.elf or args.decomp / 'config/SCUS_971.12').read_bytes()
    digest = hashlib.sha256(elf).hexdigest()
    if digest != ELF_SHA256:
        sys.exit(f'export_player_tables: ELF SHA-256 {digest} is not the pinned SCUS-97112 build')
    bank = args.decomp / BANK
    if bank.exists():
        count = struct.unpack_from('<I', bank.read_bytes(), 0)[0]
        if count != ROWS:
            sys.exit(f'export_player_tables: the player clip bank holds {count} clips, not {ROWS}')
    terminator = elf[elf_offset(TABLE) + STRIDE * ROWS:elf_offset(TABLE) + STRIDE * (ROWS + 1)]
    if terminator != bytes(STRIDE):
        sys.exit('export_player_tables: the row after the table is not the zero row')
    values = rates(elf)
    payload = encode(values)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(payload)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    histogram = collections.Counter(struct.pack('<f', v).hex() for v in values)
    args.report.write_text(json.dumps({
        'source_sha256': digest, 'table': hex(TABLE), 'rows': ROWS,
        'bank_checked': bank.exists(), 'output': str(args.output),
        'output_sha256': hashlib.sha256(payload).hexdigest(), 'bytes': len(payload),
        'distinct_rates': len(histogram)}, indent=2) + '\n')
    print(f'player clip rates: {ROWS} rows ({len(histogram)} distinct), {len(payload)} bytes -> {args.output}')


if __name__ == '__main__':
    main()
