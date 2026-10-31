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
# The middle of the route to the first boss (lane C1): the crate registry
# rows 001553E4 / 00155FE0 read for AREA06 and AREA22 (D_0024A850[area],
# D_0024D820[area]) and AREA22's group table D_00275A80 (its first word is
# not read: D_0024A850[0x16] is 0, so the first index is 1; the nest
# groups the table names live in the AREA22 overlay's data). AREA06's
# table 0x826A60 is in its overlay's data (the loader's).
AREA06_LIGHT_TABLE, AREA06_LIGHT_RECORDS = 0x00269D40, 9
AREA13_LIGHT_TABLE, AREA13_LIGHT_RECORDS = 0x0026A540, 1
MID_WINDOWS = ((0x0024A85C, 2), (0x0024A87C, 2), (0x0024D838, 4), (0x0024D878, 4), (0x00275A84, 8),
               # 001C1A80 (the model-0x52 owners of AREA22 / AREA06): its
               # 0x28 seven-float sampler rows D_0024F8F0 (001D0D40 / 001D0D60)
               # and its +0x208 / +0x30 reach pair D_00275660 (001BE5F0).
               (0x0024F8F0, 0x28 * 7 * 4), (0x00275660, 8),
               # the area change's flash node 001E7440's ramp row
               # (001DEE80(9, D_0026E980)), 001E7310's D_0026E970 twin
               (0x0026E980, 0x10),
               # the bugs' (0012A5D0, 00128C10) countdown halfwords D_00275380
               (0x00275380, 8),
               # the third ramp twin 001E7570's (0x80000063): 001DEE80's row
               # D_0026E990 and 001DF600's 001D6B60 colour D_0026E870
               (0x0026E990, 0x10), (0x0026E870, 0x10),
               # the infection node 001F8350's (effect 0x80000048, a06_05)
               # 001CFBE0 source block D_0025D890, only read
               (0x0025D890, 0x90),
               # the debris spawner 0021AE90's (effect 0x80000044, a06_05)
               # bone index rows D_00266AE0 / D_00266B00 (six words each)
               (0x00266AE0, 0x18), (0x00266B00, 0x18),
               # 001C50B0's AREA06 (keys 0x600 / 0x601) light offset vector
               # D_0026E2E0 (001028B8 / 001028D0 around its 001C5050)
               (0x0026E2E0, 0x10),
               # AREA06's 0022DCD0 light list D_00269D40 (its owner 0x7AD1A0):
               # nine 0x40-byte records, then the word -1
               (AREA06_LIGHT_TABLE, AREA06_LIGHT_RECORDS * 0x40 + 4),
               # 0015A2C0's floor-field config row 6 (AREA06's owners; rows
               # 0, 1, 3 and 5 are exported above): one 20-byte record
               (0x00248198, 0x14),
               # 0021A180's 001CFBE0 source block D_002668A0 (the strip owner
               # 00219870's; the port's mutable copy starts from these bytes)
               (0x002668A0, 0x90),
               # AREA13: D_0024D7C0[0x0D] (0x8293A0's record); one window
               # from the descriptors D_002759B8 / D_002759C0 (00183EF0 reads
               # the Use descriptors [44] 0x823E90 and [58] 0x826FF0 publish;
               # a candidate's floats may cross into the next word) through
               # the sub table D_002759D0
               (0x0024D7F4, 4), (0x002759B8, 0x1C),
               # AREA13's 0022DCD0 light list D_0026A540, then the word -1
               (AREA13_LIGHT_TABLE, AREA13_LIGHT_RECORDS * 0x40 + 4),
               # AREA06's effect handlers' 001CFBE0 sources: 001ECA20's
               # D_00256DC0 / D_00256E50 and 001EBE10's D_002564C0
               (0x00256DC0, 0x120), (0x002564C0, 0x90),
               # 0019C6F0's per-area room-list words D_0024D7C0[6] / [0x16]
               # and AREA06's per-sub list pointers D_00275948 (sub 0, 1: the
               # lists lie in the overlay's data); AREA22's D_00275A88 is in
               # the D_00275A84 window above. One window from D_00275940:
               # 00183EF0 reads AREA06's pickup descriptor there (a
               # candidate's floats may cross into the next word)
               (0x0024D7D8, 4), (0x0024D818, 4), (0x00275940, 0x10),
               # AREA04's panel [66] (0x825880) publishes the descriptor
               # D_00275928 at its +0x30; the Use scan's 00183EF0 reads its
               # floats (selector 0: three words) whenever [66] is listed
               # (a04_03's press at door [40]); it ends where D_00275940's
               # window starts
               (0x00275928, 0x10),
               # the creature family's tables (em_level8_port_creature.c;
               # AREA04's lift creature 0012E3A0, a04b_03): the side-step /
               # bite clip rows, its arena polygon D_00245020 (0x10 quadword
               # points) and roll tables D_00245120 / D_00245130, all one
               # stretch after the D_00242F20 window; the descriptor
               # D_00275388 / limit bytes D_00275398 (its +0x30); the hit
               # effects' sources D_0025DB20..D_0025DCF0 (001ED100)
               (0x00243320, 0x245140 - 0x243320), (0x00275388, 0x14), (0x0025DB00, 0x280),
               # the first boss's spores 001F88C0 (00131650's effect): its
               # 001D04B0 record D_0025D920 and bone lists D_0025DA40.. (-1
               # terminated, kinds 1, 2, 9, 3)
               (0x0025D920, 0x120), (0x0025DA40, 0xA0),
               # the lift's inner call button [54]'s descriptor D_00275610
               # (001BC960 stores it at +0x30; the Use scan reads it)
               (0x00275610, 0x18),
               # the strip node 0021A500's (AREA06 0x823580's sparks) 001CFBE0
               # sources D_00266930 (kind 1) and D_002669C0 (kind 2)
               (0x00266930, 0x120),
               # 001BF5B0's second sampler table D_00250750 (0x41 seven-float
               # rows; 001BF6B0 on AREA01's way back, a01v)
               (0x00250750, 0x41 * 7 * 4))
# AREA19 (lane J1): the area's rows of the registries D_0024D7C0, D_0024A850
# and D_0024D820 and the per-sub words they name (D_00275A40..57: sub 1's
# group list, the group-list pair, the placement-table pair).
AREA19_LIGHT_TABLE, AREA19_LIGHT_RECORDS = 0x0026A640, 10
AREA19_WINDOWS = ((0x0024D80C, 4), (0x0024A89C, 4), (0x0024D86C, 4), (0x00275A40, 0x18),
                  # 0015A2C0's random-mode rows D_002481B0 / D_002481D0 (eight
                  # bytes a row) and heights D_002481F0
                  (0x002481B0, 0x40 + 0x1C),
                  # 0022DCD0's list for key 0x1300 (its owner, placement [12])
                  (AREA19_LIGHT_TABLE, AREA19_LIGHT_RECORDS * 0x40 + 4))
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


# AREA13's outdoor strips (001E5AC0): the descriptors D_00254F30 / D_00254FC0
# (their +0x20..+0x4F edited at run time: only the ELF's bytes here) and the
# row tables D_00255050 / D_002550E0, through D_002551BF.
# The breath 001E7050's 001CFBE0 source D_00255320.
AREA13_DATA_WINDOWS = ((0x00254F30, 0x290), (0x00255320, 0x90))
# AREA15 / AREA03 (lane K1): the areas' rows of the registries D_0024A850
# (halfwords), D_0024D7C0, D_0024D820 and AREA15's per-sub words they name
# (the list pointers D_002759E8..F7, the pairs D_002759F8 / D_00275A00).
AREA15_WINDOWS = ((0x0024A86E, 2), (0x0024D7FC, 4), (0x0024D85C, 4), (0x002759E8, 0x20),
                  (0x0024A856, 2), (0x0024D7CC, 4), (0x0024D82C, 4))
# AREA08 / AREA07 (lane P1): their rows, the list pointers D_00275950..7F (AREA07
# subs 0..3, its slider lists), D_00275980..9F (AREA08's subs, its slider lists).
AREA08_WINDOWS = ((0x0024A85E, 4), (0x0024D7DC, 8), (0x0024D83C, 8), (0x00275950, 0x50))


def blocks(elf):
    if hashlib.sha256(elf).hexdigest() != E.ELF_SHA256:
        raise ValueError('Not the pinned SCUS-97112 boot ELF')
    common = (*E.BLOCKS, *AREA01_EFFECT_WINDOWS)
    windows = (*common, *AREA00_WINDOWS, *LIGHT_WINDOWS, *FIELD_WINDOWS, *CLASS2_WINDOWS, *FOOTSTEP_WINDOWS,
               *BOX_BREAK_WINDOWS, *HAZARD_WINDOWS, *HAZARD_EFFECT_WINDOWS, *CREATURE_EFFECT_WINDOWS,
               *REVISIT_EFFECT_WINDOWS, *AREA02_LIGHT_WINDOWS, *AREA02_EFFECT_WINDOWS, *MID_WINDOWS,
               *AREA13_DATA_WINDOWS, *AREA15_WINDOWS, *AREA19_WINDOWS, *AREA08_WINDOWS)
    ordered = sorted(windows)
    if any(a + n > b for (a, n), (b, _) in zip(ordered, ordered[1:])):
        raise ValueError('AREA00 window overlaps an existing effect export')
    return [(a, E.elf_block(elf, a, n)) for a, n in windows]


def verify_light_table(data):
    """The 0022DCD0 lists (AREA00, AREA02 sub 0, AREA06): their records,
    then the word -1."""
    for base, records in ((LIGHT_TABLE, LIGHT_RECORDS), (AREA02_LIGHT_TABLE, AREA02_LIGHT_RECORDS),
                          (AREA06_LIGHT_TABLE, AREA06_LIGHT_RECORDS), (AREA13_LIGHT_TABLE, AREA13_LIGHT_RECORDS),
                          (AREA19_LIGHT_TABLE, AREA19_LIGHT_RECORDS)):
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
    if magic != b'EMET' or version != 1 or not 0 < count <= 192 or 16 + 16 * count > len(previous):
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


def verify_mid_windows(wanted, decomp):
    """The middle route's crate registry windows equal every AREA22 /
    AREA06 capture's bytes."""
    root = decomp / 'build/fork_refs/s87'
    paths = [root / 'route_a04/a04_05_progression_exit/eeMemory.bin']
    for route in ('route_a22', 'route_a22b', 'route_a01u', 'route_a01v', 'route_a06', 'route_a06b'):
        paths += sorted((root / route).glob('*/eeMemory.bin'))
    seen = 0
    for path in paths:
        ram = path.read_bytes()
        if ram[0x810700] not in (6, 0x16):
            continue
        for address, size in MID_WINDOWS:
            got, want = bytearray(ram[address:address + size]), bytearray(wanted[address])
            if address == 0x002668A0:
                # .data 0021A180 stores into (+0x08 the strip's length,
                # +0x80 = 0x40): the rest must be the ELF's
                for at in (*range(0x08, 0x0C), *range(0x80, 0x84)):
                    got[at] = want[at]
            if got != want:
                raise ValueError(f'{path.parent.name}: crate registry window {address:#x} differs from the ELF')
        seen += 1
    if not seen:
        raise ValueError('no AREA22 / AREA06 capture')
    return seen


def verify_area08_windows(wanted, decomp):
    """The AREA08 / AREA07 windows equal the bytes of their fork snapshots."""
    root = decomp / 'build/fork_refs/levels-recordings-20261030-1/s87'
    paths = sorted((root / 'route_a08').glob('*/eeMemory.bin')) + sorted((root / 'route_a07').glob('*/eeMemory.bin'))
    for path in paths:
        ram = path.read_bytes()
        for address, size in AREA08_WINDOWS:
            if ram[address:address + size] != bytes(wanted[address]):
                raise ValueError(f'{path.parent.name}: AREA08 window {address:#x} differs from the ELF')
    if not paths:
        raise ValueError('no AREA08 capture')
    return len(paths)


def verify_area15_windows(wanted, decomp):
    """The AREA15 / AREA03 windows equal the bytes of their areas' fork snapshots."""
    root = decomp / 'build/fork_refs'
    paths = []
    for group in ('levels-recordings-20261018-19/s87/route_a15', 'levels-recordings-20261018-19/s87/route_a15b',
                  'levels-recordings-20261018-25/s87/route_a15b', 'levels-recordings-20261018-26/s87/route_a03'):
        paths += sorted((root / group).glob('*/eeMemory.bin'))
    seen = 0
    for path in paths:
        ram = path.read_bytes()
        for address, size in AREA15_WINDOWS:
            if ram[address:address + size] != bytes(wanted[address]):
                raise ValueError(f'{path.parent.name}: AREA15 window {address:#x} differs from the ELF')
        seen += 1
    if not seen:
        raise ValueError('no AREA15 capture')
    return seen


def verify_area19_windows(wanted, decomp):
    """AREA19's windows equal every AREA19 fork snapshot's bytes."""
    root = decomp / 'build/fork_refs'
    paths = []
    for group in ('levels-recordings-20261018-10/s87/route_a19', 'levels-recordings-20261018-13/s87/route_a19b',
                  'levels-recordings-20261018-14/s87/route_a19c', 'levels-recordings-20261018-17/s87/route_a19c',
                  'levels-recordings-20261018-19/s87/route_a19d'):
        paths += sorted((root / group).glob('*/eeMemory.bin'))
    seen = 0
    for path in paths:
        ram = path.read_bytes()
        if ram[0x810700] != 0x13:
            continue
        for address, size in AREA19_WINDOWS:
            if ram[address:address + size] != bytes(wanted[address]):
                raise ValueError(f'{path.parent.name}: AREA19 window {address:#x} differs from the ELF')
        seen += 1
    if not seen:
        raise ValueError('no AREA19 capture')
    return seen


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
    verify_mid_windows(dict(data), args.decomp)
    verify_area15_windows(dict(data), args.decomp)
    verify_area08_windows(dict(data), args.decomp)
    verify_area19_windows(dict(data), args.decomp)
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
