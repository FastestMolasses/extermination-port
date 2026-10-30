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
CLASS2_WINDOWS = ((0x00242EB0, 0x70),
                  # 0012CAA0's grab table (the bug on the player, a00_09 row 462;
                  # byte-matched decomp C): the bone indices D_00242DD0[8] and the
                  # eight 0x18-byte rows D_00242DF0.. (offset xyz, rotation xyz).
                  (0x00242DD0, 0xE0))

# The footstep handler 001EB980 (effect subtype 0x27, byte-matched decomp C;
# AREA00's a00_00 reaches it) passes the two 0x90-byte 001CFBE0 source
# blocks D_00255FB0 and D_00256040 (adjacent: one window). Only read.
FOOTSTEP_WINDOWS = ((0x00255FB0, 0x120),)

# The box break's lift handler 001EBC30 (effect subtype 8, NEARMISS decomp
# C; AREA00 a00_09's barricade crate reaches it) passes the 0x90-byte
# 001CFBE0 source blocks D_00256280 and D_00256310 (adjacent: one window).
# Only read.
BOX_BREAK_WINDOWS = ((0x00256280, 0x120),)

# The spat hazard 001E2E80 (pool owner of entity 0x80000008, subtype 3;
# byte-matched decomp C; the AREA00 creatures' spray spawns it through
# 001EFFD0, a00_04 on) draws through 001D04B0 with the six adjacent 0x90-byte
# source blocks D_00253740, D_002537D0, D_00253860 (kind 3) and D_002538F0,
# D_00253980, D_00253A10 (kind 4): one window. Only read.
HAZARD_WINDOWS = ((0x00253740, 6 * 0x90),)

# The spat hazard's two effects: 0x80000029 (subtype 0x19, handler 001EEBA0,
# on launch) and 0x8000002A (subtype 0x1A, handler 001EEEB0, on a hit), both
# byte-matched decomp C. 001EEBA0 passes D_00257360 / D_00257870 /
# D_00257480 to 001CFBE0 (the first and last in the exported 0x257360
# window), 001EEEB0 D_00257510 / D_00257900 / D_00257630 and rewrites the
# parameter rows D_00257530 / D_00257650 inside them: the 0x257510 window
# (three blocks and their rows) and the 0x257870 window (its blocks
# D_00257870 / D_00257900, and the box-break spray 001EF510's tables
# D_00257990 / D_00257A20 / D_00257AB0, adjacent: 0x2D0 bytes).
HAZARD_EFFECT_WINDOWS = ((0x00257510, 0x1B0), (0x00257870, 0x2D0))
# 001EEEB0's stores into its window (D_00257530 = (8, 24, 8, 0 x5),
# D_00257650 = (24, 112, 24, 128, 24, 112, 24, 0)): a capture holds either
# the ELF's words there or these (the handler ran); the ELF's are exported.
# The subtype-3 handler 001ECFB0 (byte-matched decomp C; AREA00 a00_09)
# passes the three descriptor blocks D_002571B0 + 0x90 k (k = 0..2), and the
# creatures' glow node 001F6FB0 (em_area00_fx_glow.c) the block D_0025D800,
# whose +0x10..+0x18 it rewrites before each 001CFBE0: one window each.
CREATURE_EFFECT_WINDOWS = ((0x002571B0, 0x1B0), (0x0025D800, 0x90),
                           # the flash node 001E7310's ramp (a00_09): 001DEE80's three
                           # words D_0026E970 and 001DF180's 001D6B60 colour D_0026E860
                           (0x0026E860, 0x10), (0x0026E970, 0x10))
# The AREA01 revisit's effect handler 001ECEF0 (byte-matched decomp C; after
# the event, a01r_01 / a01r_02) submits D_00257090 or D_00257120 by its node's
# +0x38: one window, only read.
REVISIT_EFFECT_WINDOWS = ((0x00257090, 0x120),)
# AREA02 (a02_01 / a02_02, the car's run after switch [31]): the effect
# handler 001EC9A0's source block D_00256D30 (one 0x90-byte record); the
# burst emitter 001E4CE0 / 001E4A00 the car spawns: the frame doublewords
# D_00253C50[10], the colour ramps D_002541F0 / D_00254210, its parameter
# rows D_00254230..D_002542BC and mesh sets D_002542D0..D_00254D80 (the
# last 0x1B0 bytes long).
# The effect handler 001EC5F0 (a02_03, over the wreck): its three source
# records D_00256A60 / D_00256AF0 / D_00256B80.
# The effect handler 001EC820 (the wreck's fire, a02_03): D_00256C10 /
# D_00256CA0, just below 001EC9A0's record.
AREA02_EFFECT_WINDOWS = ((0x00256D30, 0x90), (0x00256C10, 0x120), (0x00253C50, 0x50), (0x002541F0, 0xD40), (0x00256A60, 0x1B0))
# AREA02 sub 0 (key 0x200): 0022DCD0 state 0 fills its tables from
# D_00269680, six 64-byte records and the word -1 (decomp NEARMISS C; the
# light owner of a02_00 onwards). Sub 1 (0x201) has no list (state 3).
AREA02_LIGHT_TABLE, AREA02_LIGHT_RECORDS = 0x00269680, 6
AREA02_LIGHT_WINDOWS = ((AREA02_LIGHT_TABLE, AREA02_LIGHT_RECORDS * 0x40 + 4),)
HAZARD_EFFECT_ROWS = {0x00257530: struct.pack('<8f', 8, 24, 8, 0, 0, 0, 0, 0),
                      0x00257650: struct.pack('<8f', 24, 112, 24, 128, 24, 112, 24, 0)}


def verify_hazard_effects(wanted, ram):
    for address, size in HAZARD_EFFECT_WINDOWS:
        got, want = bytearray(ram[address:address + size]), bytearray(wanted[address])
        for row, stored in HAZARD_EFFECT_ROWS.items():
            if address <= row < address + size:
                at = row - address
                if got[at:at + 32] not in (want[at:at + 32], stored):
                    raise ValueError('001EEEB0 parameter row holds neither the ELF nor its stores')
                got[at:at + 32] = want[at:at + 32]
        if got != want:
            raise ValueError('AREA00 hazard effect window differs from pinned ELF')


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
    windows = (*common, *AREA00_WINDOWS, *LIGHT_WINDOWS, *FIELD_WINDOWS, *CLASS2_WINDOWS, *FOOTSTEP_WINDOWS,
               *BOX_BREAK_WINDOWS, *HAZARD_WINDOWS, *HAZARD_EFFECT_WINDOWS, *CREATURE_EFFECT_WINDOWS,
               *REVISIT_EFFECT_WINDOWS, *AREA02_LIGHT_WINDOWS, *AREA02_EFFECT_WINDOWS)
    ordered = sorted(windows)
    if any(a + n > b for (a, n), (b, _) in zip(ordered, ordered[1:])):
        raise ValueError('AREA00 window overlaps an existing effect export')
    return [(a, E.elf_block(elf, a, n)) for a, n in windows]


def verify_light_table(data):
    """The 0022DCD0 lists (AREA00, AREA02 sub 0): their records, then the
    word -1."""
    for base, records in ((LIGHT_TABLE, LIGHT_RECORDS), (AREA02_LIGHT_TABLE, AREA02_LIGHT_RECORDS)):
        table = data[base]
        for index in range(records):
            if struct.unpack_from('<i', table, index * 0x40)[0] == -1:
                raise ValueError(f'0022DCD0 list {base:#x} ends early')
        if struct.unpack_from('<i', table, records * 0x40)[0] != -1:
            raise ValueError(f'0022DCD0 list {base:#x} does not end where exported')


def verify_area02_windows(wanted, decomp):
    """The AREA02 windows equal every AREA02 capture's bytes."""
    root = decomp / 'build/fork_refs/s87'
    paths = [root / 'route_a01r/a01r_03_door16/eeMemory.bin', *sorted((root / 'route_a02').glob('a02_*/eeMemory.bin'))]
    seen = 0
    for path in paths:
        ram = path.read_bytes()
        if ram[0x810700] != 2:
            continue
        for address, size in AREA02_LIGHT_WINDOWS:
            if ram[address:address + size] != wanted[address]:
                raise ValueError('AREA02 immutable table differs from pinned ELF')
        seen += 1
    if not seen:
        raise ValueError('no AREA02 capture')
    return seen


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
                              *FOOTSTEP_WINDOWS, *BOX_BREAK_WINDOWS, *HAZARD_WINDOWS, *CREATURE_EFFECT_WINDOWS,
                              *REVISIT_EFFECT_WINDOWS, *AREA02_EFFECT_WINDOWS):
            if ram[address:address + size] != wanted[address]:
                raise ValueError('AREA00 immutable table differs from pinned ELF')
        verify_hazard_effects(wanted, ram)
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
    verify_area02_windows(dict(data), args.decomp)
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
