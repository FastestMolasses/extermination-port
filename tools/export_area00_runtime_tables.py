#!/usr/bin/env python3
"""Add AREA00's immutable surface and crate roots to the effect export.

0015AB00 indexes D_00248290 with signed actor +54 and a 20-byte stride.
The retained fork AREA00 placements use rows 0 and 1. This exports those
two records from the pinned local ELF and verifies them in every retained
AREA00 snapshot. The existing effect blocks and runtime provider are reused.
Output is disc-derived and must stay in ignored assets/ or build/.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import struct

import export_effect_tables as E
from export_area01_water_effects import EXTRA as AREA01_EFFECT_WINDOWS

SURFACE_BASE, SURFACE_SIZE = 0x00248290, 40
SURFACE_CALLBACK = 0x0015AB00
AREA00_WINDOWS = ((SURFACE_BASE, SURFACE_SIZE), (0x0024A850, 2), (0x0024D820, 4))
# 0022DCD0 (decomp NEARMISS C) in AREA00 (key (D_00810700 << 8) + D_00810701
# 0 or 1; the route visits subareas 0 and 1 only) fills its tables from the
# 64-byte records at D_00269400 up to the word -1 (six records and the
# terminator word), and every frame scales the 0x90-byte record D_0026A900
# through 001D0400. Kept apart from AREA00_WINDOWS, whose read-view test
# pins the surface/root layout.
LIGHT_TABLE, LIGHT_RECORDS = 0x00269400, 6
LIGHT_WINDOWS = ((LIGHT_TABLE, LIGHT_RECORDS * 0x40 + 4), (0x0026A900, 0x90))
# 0015A2C0 (decomp NEARMISS C) state 0 points +0x30 at the 20-byte config
# row D_00248120 + 20 * (short)+0x54. The AREA00 placements use rows 1, 3,
# 4 and 5 (every retained AREA00 snapshot); rows 0, 1 and 5 are already
# exported for AREA01 (export_effect_tables.py), so rows 3 and 4 are added.
FIELD_ROWS = (0x00248120, 20, (3, 4))
FIELD_WINDOWS = ((FIELD_ROWS[0] + FIELD_ROWS[1] * FIELD_ROWS[2][0], FIELD_ROWS[1] * len(FIELD_ROWS[2])),)

# The class-2 owners' immutable tables up to D_00242F20 (already exported):
# 00128C10's halfword clip tables D_00242EB0 / D_00242EB6 / D_00242EBC
# (indexed by 00128600(0)) and 00128600's byte table D_00242ED0, 16 bytes
# per row (rows 0, 1 and 4 named by its callers; the AREA00 arrival's
# 0012A5D0 -> 0012AFC0 reaches 00128600(0)). Both are byte-matched decomp C.
CLASS2_WINDOWS = ((0x00242EB0, 0x70),)

# The footstep handler 001EB980 (effect subtype 0x27, byte-matched decomp C;
# AREA00's a00_00 reaches it) passes the two 0x90-byte 001CFBE0 source
# blocks D_00255FB0 and D_00256040 (adjacent: one window). Only read.
FOOTSTEP_WINDOWS = ((0x00255FB0, 0x120),)


def captures(decomp):
    root = decomp / 'build/fork_refs/s87'
    return [root / 'route_a01/a01_07_level_exit/eeMemory.bin',
            *sorted((root / 'route_a00').glob('*/eeMemory.bin'))]


def verify_surface(data, paths):
    observed, owners = set(), 0
    for path in paths:
        ram = path.read_bytes()
        if ram[SURFACE_BASE:SURFACE_BASE + SURFACE_SIZE] != data:
            raise ValueError('Surface table differs from pinned ELF: ' + str(path))
        for slot in range(256):
            address = 0x7A5640 + slot * 0x2F0
            if not ram[address] or struct.unpack_from('<I', ram, address + 0x10)[0] != SURFACE_CALLBACK:
                continue
            index = struct.unpack_from('<h', ram, address + 0x54)[0]
            if index not in (0, 1):
                raise ValueError('Retained surface owner requires an unexported row')
            observed.add(index)
            owners += 1
    if observed != {0, 1}:
        raise ValueError('Both surface rows require original capture evidence')
    return owners


def blocks(elf):
    if hashlib.sha256(elf).hexdigest() != E.ELF_SHA256:
        raise ValueError('Not the pinned SCUS-97112 boot ELF')
    common = (*E.BLOCKS, *AREA01_EFFECT_WINDOWS)
    windows = (*common, *AREA00_WINDOWS, *LIGHT_WINDOWS, *FIELD_WINDOWS, *CLASS2_WINDOWS, *FOOTSTEP_WINDOWS)
    ordered = sorted(windows)
    if any(a + n > b for (a, n), (b, _) in zip(ordered, ordered[1:])):
        raise ValueError('AREA00 window overlaps an existing effect export')
    return [(a, E.elf_block(elf, a, n)) for a, n in windows]


def verify_light_table(data):
    """The 0022DCD0 AREA00 list: LIGHT_RECORDS records, then the word -1."""
    table = data[LIGHT_TABLE]
    for index in range(LIGHT_RECORDS):
        if struct.unpack_from('<i', table, index * 0x40)[0] == -1:
            raise ValueError('0022DCD0 AREA00 list ends early')
    if struct.unpack_from('<i', table, LIGHT_RECORDS * 0x40)[0] != -1:
        raise ValueError('0022DCD0 AREA00 list does not end where exported')


def field_rows(ram):
    """The config rows the 0015A2C0 owners of one snapshot name."""
    rows = set()
    for slot in range(256):
        address = 0x7A5640 + slot * 0x2F0
        if ram[address] and struct.unpack_from('<I', ram, address + 0x10)[0] == 0x0015A2C0:
            rows.add(struct.unpack_from('<h', ram, address + 0x54)[0])
    return rows


def verify_field_rows(paths):
    """Every 0015A2C0 owner of the retained snapshots names an exported row."""
    exported = {0, 1, 5, *FIELD_ROWS[2]}
    for path in paths:
        if not field_rows(path.read_bytes()) <= exported:
            raise ValueError('A retained 0015A2C0 owner requires an unexported config row')


def verify_windows(data, paths):
    wanted = dict(data)
    verify_light_table(wanted)
    for path in paths:
        ram = path.read_bytes()
        for address, size in (*AREA00_WINDOWS, *LIGHT_WINDOWS, *FIELD_WINDOWS, *CLASS2_WINDOWS,
                              *FOOTSTEP_WINDOWS):
            if ram[address:address + size] != wanted[address]:
                raise ValueError('AREA00 immutable table differs from pinned ELF')
    verify_field_rows(paths)
    return verify_surface(wanted[SURFACE_BASE], paths)


def verify_preserved(previous, data):
    """Refuse to discard or change an installed effect window."""
    if len(previous) < 16:
        raise ValueError('Truncated existing effect export')
    magic, version, count, _ = struct.unpack_from('<4s3I', previous)
    if magic != b'EMET' or version != 1 or not 0 < count <= 128 or 16 + 16 * count > len(previous):
        raise ValueError('Invalid existing effect export')
    expected, seen = dict(data), set()
    for index in range(count):
        address, size, offset, _ = struct.unpack_from('<4I', previous, 16 + 16 * index)
        if address in seen or offset < 16 + 16 * count or offset + size > len(previous):
            raise ValueError('Invalid existing effect window')
        if address not in expected or previous[offset:offset + size] != expected[address]:
            raise ValueError('Export would discard or change an existing effect window')
        seen.add(address)
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, default=E.DECOMP / 'config/SCUS_971.12')
    parser.add_argument('--decomp', type=Path, default=E.DECOMP)
    parser.add_argument('--out', type=Path, default=E.ROOT / 'assets/effect_tables.emet')
    args = parser.parse_args()
    data = blocks(args.elf.read_bytes())
    paths = captures(args.decomp)
    count = verify_windows(data, paths)
    output = args.out.absolute()
    if output.is_symlink() or any(parent.is_symlink() for parent in output.parents):
        raise ValueError('Output must use the worktree\'s private directories')
    if output.exists():
        verify_preserved(output.read_bytes(), data)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(E.serialize(data))
    print(f'Exported {len(data)} effect windows; surface table: {SURFACE_SIZE} bytes, '
          f'{len(paths)} matching fork snapshots, {count} owner observations')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
