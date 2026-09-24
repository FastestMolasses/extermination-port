#!/usr/bin/env python3
"""Export the player's clip-row columns of D_00248C90 from the user's ELF.

0015BA50 sets the player's source-advance step +34 to
D_00248C98[*(short *)(p + 0x20C) * 3] * +204: the float at +8 of row +20C
of D_00248C90, a table of 12-byte rows indexed by the player's clip id.
It has one row per clip of the player's clip bank (the bank's count word,
459; tools/test_player_footstep_reference.py walks the same 459 rows). The
row after the last one is all zeros.

The +0 halfword of the same rows is read by 0015BCF0 (a nonzero row
evaluates the skeleton with anim_eval_skeleton, a zero row with 001C68C0) and
by 00182DF0 (a zero row requests 00174AB0 before 00174A50); it is exported as
its own column.

The outputs are written into the port's ignored assets/ (never committed):
  assets/player_clip_rates.emcr: "EMCR", u32 version 1, u32 count (459), then
      count little-endian floats (the +8 column). em_player_clip_rates_load
      (src/game/em_player_stage_workers.c) reads it.
  assets/player_clip_row0.emch: "EMCH", u32 version 1, u32 count (459), then
      count little-endian signed halfwords (the +0 column).
      em_player_record_pose_load (src/game/em_player_record_pose.c) reads it.
  assets/player_loco_tables.emrg: "EMRG", u32 version 1, u32 base, u32 size,
      then the ELF's bytes base .. base + size: the span 0x248740..0x248ACC
      holding D_00248740 (0017B5C0's entry frame offsets), D_00248870
      (0017C440's tier speeds), the seven D_00248AB0 row pointers and the
      rows they name (0017B460 / 0017B490's clip tables). The script checks
      that every row pointer lies inside the span.
      em_player_record_pose_load_tables maps it read-only at its base.
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
TABLE, STRIDE, RATE_AT, ROW0_AT, ROWS = 0x248C90, 12, 8, 0, 459
LOCO_BASE, LOCO_END, LOCO_ROWS = 0x248740, 0x248ACC, 0x248AB0   # D_00248740 .. D_00248AB0[7]
BANK = 'extract/chunk28/f01_id3c.bin'   # the player's clip bank (count word at +0)


def elf_offset(address):
    """The boot ELF's single LOAD segment: file 0x300 -> vaddr 0x100000."""
    return address - 0x100000 + 0x300


def rates(elf, rows=ROWS):
    return [struct.unpack_from('<f', elf, elf_offset(TABLE) + STRIDE * row + RATE_AT)[0]
            for row in range(rows)]


def row0(elf, rows=ROWS):
    return [struct.unpack_from('<h', elf, elf_offset(TABLE) + STRIDE * row + ROW0_AT)[0]
            for row in range(rows)]


def encode(values):
    return struct.pack('<4sII', b'EMCR', 1, len(values)) + struct.pack(f'<{len(values)}f', *values)


def encode_row0(values):
    return struct.pack('<4sII', b'EMCH', 1, len(values)) + struct.pack(f'<{len(values)}h', *values)


def loco_tables(elf):
    """The span LOCO_BASE..LOCO_END; every D_00248AB0 row pointer must name a
    row inside it (rows end at the pointer table)."""
    pointers = [struct.unpack_from('<I', elf, elf_offset(LOCO_ROWS) + 4 * i)[0] for i in range(7)]
    for p in pointers:
        if not LOCO_BASE <= p < LOCO_ROWS:
            sys.exit(f'export_player_tables: D_00248AB0 row pointer {p:#x} lies outside the span')
    data = elf[elf_offset(LOCO_BASE):elf_offset(LOCO_END)]
    return struct.pack('<4sIII', b'EMRG', 1, LOCO_BASE, len(data)) + data, pointers


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--decomp', type=Path, default=ROOT.parent / 'Extermination')
    ap.add_argument('--elf', type=Path, help='the boot ELF (default: <decomp>/config/SCUS_971.12)')
    ap.add_argument('--output', type=Path, default=ROOT / 'assets/player_clip_rates.emcr')
    ap.add_argument('--row0-output', type=Path, default=ROOT / 'assets/player_clip_row0.emch')
    ap.add_argument('--loco-output', type=Path, default=ROOT / 'assets/player_loco_tables.emrg')
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
    column = row0(elf)
    row0_payload = encode_row0(column)
    args.row0_output.parent.mkdir(parents=True, exist_ok=True)
    args.row0_output.write_bytes(row0_payload)
    loco_payload, loco_rows = loco_tables(elf)
    args.loco_output.parent.mkdir(parents=True, exist_ok=True)
    args.loco_output.write_bytes(loco_payload)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    histogram = collections.Counter(struct.pack('<f', v).hex() for v in values)
    args.report.write_text(json.dumps({
        'source_sha256': digest, 'table': hex(TABLE), 'rows': ROWS,
        'bank_checked': bank.exists(), 'output': str(args.output),
        'output_sha256': hashlib.sha256(payload).hexdigest(), 'bytes': len(payload),
        'distinct_rates': len(histogram),
        'row0_output': str(args.row0_output),
        'row0_sha256': hashlib.sha256(row0_payload).hexdigest(),
        'row0_values': dict(collections.Counter(column)),
        'loco_output': str(args.loco_output),
        'loco_sha256': hashlib.sha256(loco_payload).hexdigest(),
        'loco_rows': [hex(p) for p in loco_rows]}, indent=2) + '\n')
    print(f'player clip rates: {ROWS} rows ({len(histogram)} distinct), {len(payload)} bytes -> {args.output}')
    print(f'player clip row +0: {ROWS} rows, {len(row0_payload)} bytes -> {args.row0_output}')
    print(f'player locomotion tables: {LOCO_BASE:#x}..{LOCO_END:#x}, {len(loco_payload)} bytes -> {args.loco_output}')


if __name__ == '__main__':
    main()
