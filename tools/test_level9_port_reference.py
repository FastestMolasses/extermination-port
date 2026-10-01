#!/usr/bin/env python3
"""Execute the ORIGINAL ninth-level code (the a13 census rows: AREA13's
overlay owners, the outdoor watchers, the hatches, the panel; the boot
functions of the outdoor load, the lift door's wobble and the AREA19 load
at entry 9) and compare em_level9_port*.c.

docs/LEVEL9_PORT.md. The census delta of the ninth-level route (decomp
build/s87/census/a13_delta.json, new_functions: 44 rows) names 44
functions. 42 have no verified port translation and are translated in
em_level9_port_*.c; they are the entries below (FUNCS). Two are reused
(001E7C60, 00214570) and re-run against the original by reuse_checks.
Calls between the translations run as original code at the top level of
the oracle (only other functions are hooks), and every function is also an
entry of its own.

The harness is tools/test_level8_port_reference.py's (lane E8T, itself the
AREA06 / AREA22 / AREA04 design), copied and owned here, with the
ninth-level captures, this module's hook table and cases. Entries that take
the original stack pointer (kind S) run the oracle with sp = STACK_TOP and
give the native entry STACK_TOP; the frame locals live in the compared
stack window.

Oracle: the shared EE interpreter with the measured float model (FallEE,
tools/test_player_fall_reference.py) runs the original code resident in the
recorded RAM images of the ninth-level route (decomp build/s87/route_a13/
<beat>/ and route_a04b/a04b_04_lift/, the AREA13 arrival). Before any case
the test checks that the overlay text of every image equals the user's
extract/OVERLAY/<AREA>.BIN (text size from the file header), that the boot
text below 0x241000 equals the pinned ELF and that the jump tables equal
their files, so every executed instruction is original. Nothing here embeds
original bytes; reports hold counts only.

Callees (every function outside this module and the indirect calls through
a function word) are intercepted at their entry and logged with their
arguments. A callee either runs as ORIGINAL code nested inside the oracle
(the pure helpers in RUN), its writes recorded, or is stubbed with the
case's scripted result. The native module runs over a byte copy of the same
RAM with hooks that must be called in the same order with the same
arguments; each hook replays the original callee's writes and result.

Compared, per case (the AREA22 harness's list): memory at the entry of
every call before the callee's writes are replayed; the callee and its
arguments; the memory accesses between calls one for one, in order, by
address, size and changed-or-not; all memory after the last store; the
return value; the store-log self-check; stops at unmapped or misaligned
original accesses; every case again from a poisoned start image; coverage
of every reachable original word; the fail-stop contract; the table's ctx
at every call.

EM_LEVEL9_PORT_ONLY=<label prefix> runs a subset (no coverage / contract /
reuse checks). EM_LEVEL9_PORT_SOURCE=<dir> tests another copy of the module
sources (the mutation sweep). EM_LEVEL9_PORT_MISSING=1 lists unexecuted
words. At most 4 worker processes unless EM_TEST_JOBS says otherwise.
"""
import ctypes as C
import math
import os
import random
import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
os.environ.setdefault('EM_TEST_JOBS', '4')
import reference_mode  # noqa: E402
from test_player_slide_reference import EE, read_elf, bits, number, s32, s64, sx32, STACK_TOP, RETURN  # noqa: E402
from test_player_fall_reference import FallEE  # noqa: E402
import ee_float_model as M  # noqa: E402

DECOMP = ROOT.parent / 'Extermination'
OVERLAY_FILES = {10: DECOMP / 'extract/OVERLAY/AREA13.BIN', 16: DECOMP / 'extract/OVERLAY/AREA19.BIN'}
OUT = ROOT / 'build/level9/port'
MASK = 0xFFFFFFFF
MASK64 = 0xFFFFFFFFFFFFFFFF
RAM_SIZE, SPAD_SIZE = 0x2000000, 0x4000
ARENA = 0x823500
STACK_LO = STACK_TOP - 0x800
STACK_SIZE = STACK_TOP - STACK_LO
BEAT_DIRS = {'a04b_04_lift': DECOMP / 'build/s87/route_a04b/a04b_04_lift'}
for _b in ('a13_00_door8', 'a13_01_door14', 'a13_02_door17', 'a13_03_item27', 'a13_04_hatch', 'a13_05_shaft'):
    BEAT_DIRS[_b] = DECOMP / 'build/s87/route_a13' / _b
BEATS = list(BEAT_DIRS)
# The overlay each image holds (header id at 0x823504): AREA13 (10) from the
# lift's arrival to the hatch's ladder, AREA19 (16) after the fall.
RESIDENT = {b: 10 for b in BEATS}
RESIDENT['a13_05_shaft'] = 16
A13_BEATS = [b for b in BEATS if RESIDENT[b] == 10]
POOL, NODE, NODES = 0x7A5640, 0x2F0, 400
PLAYER = 0x8102B0
CAM = 0x8101E0


class _Route:
    """ROUTE / beat resolves to the beat's folder."""

    def __truediv__(self, beat):
        return BEAT_DIRS[beat]


ROUTE = _Route()

U, I, F, Q, S = 'u', 'i', 'f', 'q', 's'
# (entry, native symbol, byte length of the function body, argument kinds,
# result kind or None). Word arguments go to a0.., floats to f12.., S is the
# entry stack pointer (native only; the oracle's sp is STACK_TOP).
FUNCS = {
    # AREA13 overlay (runtime addresses)
    0x823700: ('em_level9_port_00823700', 0x12C, [U], None),
    0x823940: ('em_level9_port_00823940', 0xD0, [U], None),
    0x823A10: ('em_level9_port_00823A10', 0x24, [], I),
    0x823BC0: ('em_level9_port_00823BC0', 0x4C, [U], None),
    0x823C10: ('em_level9_port_00823C10', 0x138, [U], None),
    0x823D50: ('em_level9_port_00823D50', 0x138, [U], None),
    0x823E90: ('em_level9_port_00823E90', 0x148, [U], None),
    0x823FE0: ('em_level9_port_00823FE0', 0x7C, [], I),
    0x824180: ('em_level9_port_00824180', 0x208, [U], None),
    0x824BB0: ('em_level9_port_00824BB0', 0x1590, [U, S], None),
    0x826140: ('em_level9_port_00826140', 0x4D0, [U, U, S], I),
    0x8266A0: ('em_level9_port_008266A0', 0x1AC, [U], None),
    0x826850: ('em_level9_port_00826850', 0x760, [U], None),
    0x826FB0: ('em_level9_port_00826FB0', 0x10, [U], I),
    0x826FC0: ('em_level9_port_00826FC0', 0x2C, [U], I),
    0x826FF0: ('em_level9_port_00826FF0', 0x158, [U], None),
    0x827150: ('em_level9_port_00827150', 0xADC, [U], None),
    0x8292A0: ('em_level9_port_008292A0', 0xF8, [U], None),
    0x8293A0: ('em_level9_port_008293A0', 0x63C, [U], None),
    0x8299E0: ('em_level9_port_008299E0', 0xB8, [U], None),
    0x829AA0: ('em_level9_port_00829AA0', 0x1F8, [U], None),
    # the AREA19 load
    0x1383C0: ('em_level9_port_001383C0', 0x178, [U], None),
    0x138540: ('em_level9_port_00138540', 0x198, [U, U], None),
    0x1386E0: ('em_level9_port_001386E0', 0x214, [U, U], None),
    0x154460: ('em_level9_port_00154460', 0x144, [U], I),
    0x1546C0: ('em_level9_port_001546C0', 0x7C, [U], None),
    0x154740: ('em_level9_port_00154740', 0x274, [U, U], None),
    0x1549C0: ('em_level9_port_001549C0', 0x540, [U, U], None),
    0x15A200: ('em_level9_port_0015A200', 0xB8, [U, I, I], I),
    0x183440: ('em_level9_port_00183440', 0x94, [U], None),
    0x1838B0: ('em_level9_port_001838B0', 0x5C, [U], None),
    0x196970: ('em_level9_port_00196970', 0x370, [U, U], None),
    0x196CE0: ('em_level9_port_00196CE0', 0x6B0, [U, U], None),
    # pose, animation and AREA19 actors
    0x1BA7F0: ('em_level9_port_001BA7F0', 0xEC, [U], None),
    0x1BDCA0: ('em_level9_port_001BDCA0', 0xD0, [U], I),
    0x1BDD70: ('em_level9_port_001BDD70', 0xE4, [U], I),
    0x1C06E0: ('em_level9_port_001C06E0', 0x94C, [U], None),
    0x1C1030: ('em_level9_port_001C1030', 0x4D0, [U], None),
    0x1C4BA0: ('em_level9_port_001C4BA0', 0x108, [U], None),
    # effects
    0x1DE920: ('em_level9_port_001DE920', 0x488, [S], None),
    0x1E5AC0: ('em_level9_port_001E5AC0', 0xD00, [U, I, I, F, S], None),
    0x1E7050: ('em_level9_port_001E7050', 0x2B4, [U, S], None),
}
OVERLAY_OF = {entry: 10 for entry in FUNCS if entry >= ARENA}
# The entries whose passing cases are all contract sites (they return from
# their own body): the default run and EM_TEST_FULL=1.
RETURN_SITES = (0x823A10, 0x823FE0, 0x826FB0, 0x826FC0, 0x154460, 0x15A200)
RETURN_SITES_FULL = RETURN_SITES + (0x1BDCA0, 0x1BDD70, 0x826140)


def has_result(entry):
    return FUNCS[entry][3] is not None


# Hooks, in the field order of EmLevel9PortHooks (after ctx and bytes; the
# indirect-call w_callback follows them).
# name -> (original address, [(ctype, register)], result kind or None)
HOOKS = [
# BEGIN GENERATED HOOKS
    ('w_001026A0', 0x1026A0, [(U, 4), (U, 5), (U, 6)], None),
    ('w_00102738', 0x102738, [(U, 4), (U, 5)], F),
    ('w_00102760', 0x102760, [(U, 4), (U, 5)], None),
    ('w_00102798', 0x102798, [(U, 4), (U, 5)], None),
    ('w_001028B8', 0x1028B8, [(U, 4), (U, 5), (U, 6)], None),
    ('w_001028D0', 0x1028D0, [(U, 4), (U, 5), (U, 6)], None),
    ('w_00102900', 0x102900, [(U, 4), (U, 5), (F, 12)], None),
    ('w_00102918', 0x102918, [(U, 4), (U, 5), (U, 6)], None),
    ('w_00102948', 0x102948, [(U, 4), (U, 5)], None),
    ('w_00102958', 0x102958, [(U, 4), (U, 5)], None),
    ('w_001029C0', 0x1029C0, [(U, 4)], None),
    ('w_00102A60', 0x102A60, [(U, 4), (U, 5), (F, 12)], None),
    ('w_00102B08', 0x102B08, [(U, 4), (U, 5), (F, 12)], None),
    ('w_00102BB0', 0x102BB0, [(U, 4), (U, 5), (F, 12)], None),
    ('w_001031E0', 0x1031E0, [(U, 4), (U, 5)], None),
    ('w_00103230', 0x103230, [(U, 4), (U, 5), (F, 12)], None),
    ('w_0011DBB8', 0x11DBB8, [(F, 12)], F),
    ('w_0011DE90', 0x11DE90, [(F, 12)], F),
    ('w_0011DF78', 0x11DF78, [(F, 12)], F),
    ('w_0011E2A8', 0x11E2A8, [(F, 12)], F),
    ('w_0011E520', 0x11E520, [(F, 12)], F),
    ('w_0011E748', 0x11E748, [(F, 12)], F),
    ('w_00122BB8', 0x122BB8, [], I),
    ('w_001281C0', 0x1281C0, [(F, 12)], I),
    ('w_00128250', 0x128250, [(F, 12)], I),
    ('w_00131ED0', 0x131ED0, [(U, 4)], None),
    ('w_00138900', 0x138900, [(U, 4), (U, 5)], None),
    ('w_00138C20', 0x138C20, [(U, 4), (U, 5)], None),
    ('w_00139240', 0x139240, [(U, 4), (U, 5)], None),
    ('w_001399F0', 0x1399F0, [(U, 4), (U, 5)], None),
    ('w_00139E00', 0x139E00, [(U, 4), (U, 5)], None),
    ('w_0013A3B0', 0x13A3B0, [(U, 4), (U, 5)], None),
    ('w_0013B350', 0x13B350, [(U, 4), (U, 5)], None),
    ('w_0013B9A0', 0x13B9A0, [(U, 4), (U, 5)], None),
    ('w_0013BE60', 0x13BE60, [(U, 4), (U, 5)], None),
    ('w_0013BF20', 0x13BF20, [(U, 4), (U, 5)], None),
    ('w_001545B0', 0x1545B0, [(U, 4), (F, 12), (F, 13)], I),
    ('w_00154F00', 0x154F00, [(U, 4)], None),
    ('w_0015AC00', 0x15AC00, [(U, 4), (U, 5)], I),
    ('w_0015AE20', 0x15AE20, [(U, 4), (U, 5)], None),
    ('w_001662D0', 0x1662D0, [(U, 4)], None),
    ('w_00174A50', 0x174A50, [(U, 4), (F, 12)], None),
    ('w_0018C0C0', 0x18C0C0, [(U, 4)], None),
    ('w_0018C4B0', 0x18C4B0, [(U, 4), (F, 12), (F, 13)], None),
    ('w_0018C6A0', 0x18C6A0, [(U, 4), (U, 5), (F, 12)], None),
    ('w_0018D7B0', 0x18D7B0, [(U, 4), (I, 5)], None),
    ('w_001916C0', 0x1916C0, [(U, 4), (U, 5), (I, 6)], None),
    ('w_00192010', 0x192010, [(U, 4), (F, 12), (F, 13), (F, 14)], None),
    ('w_0019A570', 0x19A570, [(U, 4), (U, 5), (I, 6), (I, 7)], I),
    ('w_0019AA80', 0x19AA80, [(U, 4), (U, 5), (I, 6)], I),
    ('w_0019B6C0', 0x19B6C0, [(U, 4), (U, 5), (U, 6)], I),
    ('w_0019C6F0', 0x19C6F0, [(I, 4), (I, 5)], I),
    ('w_001A2370', 0x1A2370, [(U, 4), (U, 5)], None),
    ('w_001AEE10', 0x1AEE10, [(I, 4), (I, 5)], None),
    ('w_001AF780', 0x1AF780, [], U),
    ('w_001AF800', 0x1AF800, [(U, 4)], None),
    ('w_001AFA90', 0x1AFA90, [(I, 4)], U),
    ('w_001AFC10', 0x1AFC10, [(U, 4)], None),
    ('w_001B0C60', 0x1B0C60, [(I, 4), (I, 5), (I, 6)], None),
    ('w_001B0D80', 0x1B0D80, [(U, 4)], None),
    ('w_001B0F60', 0x1B0F60, [(U, 4), (I, 5)], I),
    ('w_001B0FD0', 0x1B0FD0, [(U, 4)], I),
    ('w_001B1020', 0x1B1020, [(U, 4), (I, 5), (I, 6), (I, 7)], None),
    ('w_001B10B0', 0x1B10B0, [(U, 4), (I, 5), (I, 6)], I),
    ('w_001B1190', 0x1B1190, [(I, 4)], None),
    ('w_001B11E0', 0x1B11E0, [(I, 4)], I),
    ('w_001B1240', 0x1B1240, [(U, 4), (F, 12), (F, 13)], F),
    ('w_001B12B0', 0x1B12B0, [(F, 12), (F, 13), (F, 14)], F),
    ('w_001B1470', 0x1B1470, [(F, 12)], F),
    ('w_001B1630', 0x1B1630, [(F, 12), (F, 13), (F, 14)], I),
    ('w_001B17A0', 0x1B17A0, [(U, 4)], I),
    ('w_001B1B70', 0x1B1B70, [(U, 4)], None),
    ('w_001B1EA0', 0x1B1EA0, [(I, 4), (U, 5), (U, 6), (I, 7)], I),
    ('w_001B2140', 0x1B2140, [(U, 4)], I),
    ('w_001B4810', 0x1B4810, [(U, 4)], None),
    ('w_001B5360', 0x1B5360, [(U, 4)], None),
    ('w_001BA1A0', 0x1BA1A0, [(U, 4), (U, 5)], None),
    ('w_001BA1C0', 0x1BA1C0, [(U, 4), (I, 5)], I),
    ('w_001BA1F0', 0x1BA1F0, [(U, 4)], I),
    ('w_001BA540', 0x1BA540, [(U, 4)], None),
    ('w_001BA580', 0x1BA580, [(U, 4), (I, 5)], None),
    ('w_001BA8E0', 0x1BA8E0, [(U, 4), (I, 5)], None),
    ('w_001BE5F0', 0x1BE5F0, [(U, 4), (U, 5), (U, 6)], I),
    ('w_001C47A0', 0x1C47A0, [(I, 4), (I, 5)], I),
    ('w_001C6120', 0x1C6120, [(U, 4), (I, 5)], U),
    ('w_001C6150', 0x1C6150, [(U, 4)], I),
    ('w_001C6160', 0x1C6160, [(U, 4)], I),
    ('w_001C62C0', 0x1C62C0, [(U, 4)], None),
    ('w_001C6380', 0x1C6380, [(U, 4)], None),
    ('w_001C63E0', 0x1C63E0, [(U, 4), (I, 5)], None),
    ('w_001C64F0', 0x1C64F0, [(U, 4), (F, 12)], I),
    ('w_001C67E0', 0x1C67E0, [(U, 4), (I, 5), (F, 12), (F, 13)], None),
    ('w_001C68C0', 0x1C68C0, [(U, 4)], None),
    ('w_001C6910', 0x1C6910, [(U, 4)], None),
    ('w_001C7900', 0x1C7900, [(U, 4), (U, 5), (I, 6), (I, 7)], None),
    ('w_001CA5E0', 0x1CA5E0, [(U, 4), (U, 5), (I, 6)], None),
    ('w_001CA6E0', 0x1CA6E0, [(U, 4), (U, 5)], None),
    ('w_001CA6F0', 0x1CA6F0, [(U, 4), (I, 5)], None),
    ('w_001CA7B0', 0x1CA7B0, [(U, 4), (F, 12)], I),
    ('w_001CA940', 0x1CA940, [(I, 4), (I, 5)], None),
    ('w_001CB5B0', 0x1CB5B0, [(I, 4)], None),
    ('w_001CB5F0', 0x1CB5F0, [(U, 4), (U, 5), (I, 6)], U),
    ('w_001CB760', 0x1CB760, [(U, 4), (U, 5), (U, 6), (I, 7)], None),
    ('w_001CD070', 0x1CD070, [(U, 4), (I, 5)], I),
    ('w_001CD2B0', 0x1CD2B0, [(F, 12), (F, 13), (F, 14), (F, 15)], F),
    ('w_001CD520', 0x1CD520, [(I, 4), (I, 5), (U, 6), (Q, 7), (U, 8), (F, 12), (F, 13), (F, 14)], I),
    ('w_001CFAE0', 0x1CFAE0, [(U, 4), (I, 5), (U, 6), (F, 12), (F, 13), (F, 14), (F, 15)], None),
    ('w_001CFBE0', 0x1CFBE0, [(I, 4), (I, 5), (U, 6), (U, 7), (I, 8)], None),
    ('w_001CFFE0', 0x1CFFE0, [(I, 4), (I, 5), (U, 6), (U, 7), (I, 8)], None),
    ('w_001D1F20', 0x1D1F20, [(I, 4)], None),
    ('w_001D1FF0', 0x1D1FF0, [(I, 4), (I, 5)], None),
    ('w_001D2040', 0x1D2040, [(I, 4), (I, 5)], None),
    ('w_001D6B10', 0x1D6B10, [(I, 4), (I, 5), (I, 6), (I, 7)], None),
    ('w_001D6BA0', 0x1D6BA0, [(I, 4), (I, 5), (I, 6), (I, 7), (I, 8), (I, 9)], None),
    ('w_001D6C90', 0x1D6C90, [(I, 4), (I, 5), (I, 6), (I, 7), (I, 8), (I, 9), (I, 10), (I, 11)], None),
    ('w_001D8BF0', 0x1D8BF0, [(U, 4), (I, 5)], None),
    ('w_001E2BA0', 0x1E2BA0, [(U, 4), (U, 5), (U, 6), (F, 12)], None),
    ('w_001E6F60', 0x1E6F60, [(I, 4), (I, 5), (I, 6), (I, 7), (I, 8), (I, 9), (I, 10)], None),
    ('w_001EFD20', 0x1EFD20, [(I, 4), (U, 5)], None),
    ('w_001EFD90', 0x1EFD90, [(I, 4), (U, 5), (U, 6)], None),
    ('w_001EFE00', 0x1EFE00, [(I, 4), (U, 5)], I),
    ('w_001EFFD0', 0x1EFFD0, [(I, 4), (U, 5), (U, 6), (I, 7), (F, 12)], None),
    ('w_001F02C0', 0x1F02C0, [(U, 4), (I, 5), (F, 12)], None),
    ('w_001F4A00', 0x1F4A00, [(U, 4), (U, 5)], None),
    ('w_001F4BF0', 0x1F4BF0, [(U, 4), (U, 5)], None),
    ('w_001F4E20', 0x1F4E20, [(U, 4), (U, 5), (F, 12)], None),
    ('w_001F5940', 0x1F5940, [(I, 4), (U, 5), (I, 6)], None),
    ('w_001F9100', 0x1F9100, [(U, 4), (U, 5), (U, 6), (F, 12)], None),
    ('w_001FB9F0', 0x1FB9F0, [(I, 4), (I, 5), (I, 6), (I, 7)], None),
    ('w_001FBD50', 0x1FBD50, [(U, 4), (I, 5), (I, 6), (F, 12)], I),
    ('w_0021B9A0', 0x21B9A0, [(I, 4), (F, 12), (F, 13)], None),
    ('w_00823830', 0x823830, [(U, 4)], None),
    ('w_00824160', 0x824160, [(U, 4)], None),
    ('w_00824390', 0x824390, [(U, 4)], None),
    ('w_00824520', 0x824520, [(U, 4)], None),
    ('w_00824960', 0x824960, [(U, 4)], None),
    ('w_00826610', 0x826610, [(U, 4)], None),
# END GENERATED HOOKS
]
BY_ADDRESS = {address: (name, args, result) for name, address, args, result in HOOKS}

# Callees that run as original code inside the oracle (writes replayed to
# the native side). Pure helpers only: none of them reaches hardware; each
# is first rehearsed with the argument registers its hook does not pass
# poisoned (Oracle.rehearse).
RUN = {0x1026A0, 0x102738, 0x102760, 0x102798, 0x1028B8, 0x1028D0, 0x102900, 0x102918, 0x102948, 0x102958,
       0x1029C0, 0x102A60, 0x102B08, 0x102BB0, 0x1031E0, 0x103230, 0x11DBB8, 0x11DE90, 0x11DF78, 0x11E2A8,
       0x11E520, 0x11E748, 0x1281C0, 0x128250, 0x1B1240, 0x1B12B0, 0x1B1470, 0x1C6160}

# Jump tables (runtime address of the jump -> (table, words)): 001386E0's
# sub-states 0..5, 001C06E0's attack steps 0..5, AREA13 0x824180's steps
# 0..6 (in the overlay's data).
JUMP_TABLES = {0x138728: (0x26D1C0, 6), 0x1C0AD0: (0x26E280, 6), 0x8241B4: (0x82E200, 7)}
TABLE_BYTES = frozenset(a for table, count in JUMP_TABLES.values() for a in range(table, table + 4 * count))
CALLBACK_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_uint32)


def stack_frame_op(word):
    """A save or restore of a callee-saved register ($s0..$s7, $fp, $ra;
    $f20..$f31) at an $sp offset: the frame bookkeeping, which the
    translation has no counterpart for."""
    op, base, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
    if base != 29:
        return False
    if op in (30, 31, 55, 63, 35, 43):   # lq sq ld sd lw sw
        return 16 <= rt <= 23 or rt in (30, 31)
    if op in (49, 57):                   # lwc1 swc1
        return rt >= 20
    return False


def in_window(key):
    return STACK_LO <= key < STACK_TOP


class FloatArg(C.c_float):
    """A float hook argument as its raw bits. ctypes converts a plain c_float
    callback argument to a Python float (a float-to-double conversion that
    quiets a signalling NaN: 0x7F800001 would arrive as 0x7FC00001); a
    subclass of c_float arrives as an instance holding a copy of the
    argument's bytes. Float results are written back as bits as well
    (Replay.hook), so every float crosses the harness bit-exactly."""


def float_arg_bits(value):
    return struct.unpack('<I', bytes(value))[0]


CT = {U: C.c_uint32, I: C.c_int32, F: C.c_float, Q: C.c_uint64}
ARG_CT = {U: C.c_uint32, I: C.c_int32, F: FloatArg, Q: C.c_uint64}
BYTES_FN = C.CFUNCTYPE(C.c_void_p, C.c_void_p, C.c_uint32, C.c_uint32)


def hook_proto(args, result):
    types = [C.c_void_p] + [ARG_CT[k] for k, _ in args]
    if result:
        types.append(C.POINTER(CT[result]))
    return C.CFUNCTYPE(C.c_int, *types)


class Hooks(C.Structure):
    _fields_ = ([('ctx', C.c_void_p), ('bytes', BYTES_FN)]
                + [(name, hook_proto(args, result)) for name, _, args, result in HOOKS]
                + [('w_callback', CALLBACK_FN)])


class Fault(C.Structure):
    _fields_ = [('address', C.c_uint32), ('code', C.c_int32)]


def fbits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]



# ----------------------------------------------------------------------------
# build + inputs
# ----------------------------------------------------------------------------
# ----------------------------------------------------------------------------
# build + inputs
# ----------------------------------------------------------------------------
SOURCES = ('em_level9_port_area13.c', 'em_level9_port_turret.c', 'em_level9_port_exit.c', 'em_level9_port_anim.c',
           'em_level9_port_fx.c')


def header_checks(source_dir=None):
    """The ctypes Hooks structure above is built from HOOKS; the C side's
    EmLevel9PortHooks (em_level9_port.h) and its wrappers
    (em_level9_port_internal.h) are text. Check that the header's generated
    block lists exactly the HOOKS entries, in order, with the same argument
    and result types, and that each hook has one wrapper l9_c_<address>
    that calls it and latches its own address."""
    base = Path(source_dir) if source_dir else ROOT / 'src/game'
    ctype = {U: 'uint32_t', I: 'int32_t', F: 'float', Q: 'uint64_t'}
    header = (base / 'em_level9_port.h').read_text()
    block = header.split('/* BEGIN GENERATED HOOKS */')[1].split('/* END GENERATED HOOKS */')[0]
    found = re.findall(r'int \(\*(w_[0-9A-F]{8})\)\(void \*ctx((?:, [^,)]+)*)\);', block)
    problems = []
    if [n for n, _ in found] != [h[0] for h in HOOKS]:
        problems.append('header hook names / order differ from HOOKS')
    for (name, params), (hname, address, args, result) in zip(found, HOOKS):
        types = [re.sub(r'\s*\b\w+$', '', p.strip()).replace(' ', '') for p in params.split(',')[1:]]
        want = [ctype[k] for k, _ in args] + ([ctype[result] + '*'] if result else [])
        if types != want or name != hname:
            problems.append('%s: header %s, HOOKS %s' % (hname, types, want))
    internal = (base / 'em_level9_port_internal.h').read_text()
    wrappers = internal.split('/* BEGIN GENERATED WRAPPERS */')[1].split('/* END GENERATED WRAPPERS */')[0]
    for hname, address, _, _ in HOOKS:
        tag = hname[2:]
        body = re.findall(r'static inline int l9_c_%s\(.*?\n\}\n' % tag, wrappers, re.S)
        if len(body) != 1 or ('o->h->%s(' % hname) not in body[0] or ('0x%08Xu' % address) not in body[0]:
            problems.append('%s: wrapper l9_c_%s missing or not calling / latching it' % (hname, tag))
    return problems, len(found)


def build(source_dir=None):
    """Compile the module (or the copies in `source_dir`, a mutation sweep)
    into a private library under build/level9/port."""
    OUT.mkdir(parents=True, exist_ok=True)
    base = Path(source_dir) if source_dir else ROOT / 'src/game'
    stem = 'level9_port' if not source_dir else 'level9_port_%d' % os.getpid()
    lib = OUT / (stem + ('.dylib' if sys.platform == 'darwin' else '.so'))
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                    '-ffp-contract=off', '-shared', '-fPIC', '-I' + str(base), '-Isrc', '-I' + str(ROOT / 'src/game')]
                   + [str(base / name) for name in SOURCES] + ['-lm', '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    for entry, (symbol, _, kinds, result) in FUNCS.items():
        fn = getattr(native, symbol)
        types = [C.POINTER(Hooks)] + [{U: C.c_uint32, I: C.c_int32, F: FloatArg, Q: C.c_uint64, S: C.c_uint32}[k]
                                      for k in kinds]
        if result is not None:
            types.append(C.POINTER(C.c_int32))
        fn.argtypes = types + [C.POINTER(Fault)]
        fn.restype = C.c_int
    return native


def load_captures(elf):
    """Every image must hold the overlay RESIDENT names, with its text equal
    to the user's extract/OVERLAY/<AREA>.BIN; every image's boot text and
    the boot jump tables must equal the pinned ELF: every instruction the
    oracle executes is original."""
    overlays = {}
    for oid, path in OVERLAY_FILES.items():
        data = path.read_bytes()
        overlays[oid] = (data, ARENA + 0x40 + struct.unpack_from('<I', data, 12)[0])
    captures = {}
    for beat in BEATS:
        folder = ROUTE / beat
        ram = (folder / 'eeMemory.bin').read_bytes()
        spad = (folder / 'scratchpad.bin').read_bytes()
        assert len(ram) == RAM_SIZE and len(spad) == SPAD_SIZE, beat
        oid = RESIDENT[beat]
        assert ram[ARENA + 4] == oid, (beat, 'resident overlay', ram[ARENA + 4])
        data, end = overlays[oid]
        assert ram[ARENA:ARENA + 8] == data[:8], (beat, 'overlay header')
        assert ram[ARENA + 0x40:end] == data[0x40:end - ARENA], (beat, 'overlay text differs')
        assert ram[0x100000:0x241000] == elf[0x300:0x300 + 0x141000], (beat, 'boot text differs from the ELF')
        for table, count in JUMP_TABLES.values():
            at = 0x300 + table - 0x100000
            assert ram[table:table + 4 * count] == elf[at:at + 4 * count], (beat, 'jump table differs')
        captures[beat] = (ram, spad)
    return captures


def text_end(oid):
    data = OVERLAY_FILES[oid].read_bytes()
    return ARENA + 0x40 + struct.unpack_from('<I', data, 12)[0]
def u32(ram, address):
    return struct.unpack_from('<I', ram, address)[0]


def owners(ram, callback):
    """Pool nodes whose +0x10 behaviour is `callback` and whose header is live."""
    found = []
    for i in range(NODES):
        node = POOL + i * NODE
        if u32(ram, node + 0x10) == callback and ram[node] != 0:
            found.append(node)
    return found


# ----------------------------------------------------------------------------
# the original side
# ----------------------------------------------------------------------------
class Coverage(FallEE):
    """FallEE that remembers every executed non-branch pc of the function
    under test (top level, not inside a callee) and flags data accesses:
    `data` is set only while an instruction executes, so the fetches of the
    run loop are not counted as reads."""
    seen = None
    top = True
    data = False

    def __init__(self, elf, ram, spad, own=False):
        """own=True: run directly on the given bytearrays (Images' oracle
        copy, which run_case puts back afterwards) instead of copying
        32 MiB per run."""
        if own:
            FallEE.__init__(self, elf, b'', b'')
            self.mem, self.spad = ram, spad
        else:
            FallEE.__init__(self, elf, ram, spad)

    def mmi(self, word, pc):
        """The shared core's MMI subset plus psubw / psubb (MMI0 function
        0x08, sub 0x01 / 0x09: lane-wise subtract, wrapping) and pextlw /
        pextuw (sub 0x12 of MMI0 / MMI1)."""
        fn, sub = word & 63, word >> 6 & 31
        if fn == 0x08 and sub in (0x01, 0x09):
            rs, rt, rd = word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
            a = (self.r[rs] & MASK64) | (self.rh[rs] << 64)
            b = (self.r[rt] & MASK64) | (self.rh[rt] << 64)
            lanes, width = (4, 32) if sub == 0x01 else (16, 8)
            m = (1 << width) - 1
            v = sum(((((a >> width * i) & m) - ((b >> width * i) & m)) & m) << width * i for i in range(lanes))
            if rd:
                self.r[rd], self.rh[rd] = v & MASK64, (v >> 64) & MASK64
            return
        if fn in (0x08, 0x28) and sub == 0x12:
            # pextlw (MMI0) / pextuw (MMI1): interleave the low / high word
            # pairs of rt and rs (rt.w0, rs.w0, rt.w1, rs.w1), which the SDK
            # matrix transpose 00102798 uses (test_area01_math_reference's model)
            rs, rt, rd = word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
            if fn == 0x08:
                a, b = self.r[rs] & MASK64, self.r[rt] & MASK64
            else:
                a, b = self.rh[rs] & MASK64, self.rh[rt] & MASK64
            lo = (b & MASK) | ((a & MASK) << 32)
            hi = (b >> 32) | ((a >> 32) << 32)
            if rd:
                self.r[rd], self.rh[rd] = lo, hi
            return
        return FallEE.mmi(self, word, pc)

    clip = 0

    def macro(self, word):
        """The shared core's VU0 macro set plus what 001CE860 uses beyond it,
        as the other VU oracles model them (test_shadow_actor_route_reference,
        test_area01_render_reference): VMAXbc / VMINIbc lanes, VFTOI4 and the
        clip test vclipw.xyz (x, y, z against |w|, denormals as zero; a lane
        with exponent 255 is not measured and refused)."""
        op, fs, ft, fd = word & 63, word >> 11 & 31, word >> 16 & 31, word >> 6 & 31
        mask = word >> 21 & 15
        x, y = [v & MASK for v in self.vf[fs]], [v & MASK for v in self.vf[ft]]
        lanes = [i for i in range(4) if mask & (8 >> i)]
        if 16 <= op < 24:
            fn = M.vu_max if op < 20 else M.vu_min
            for i in lanes:
                if fd:
                    self.vf[fd][i] = fn(x[i], y[(op - 16) & 3])
            return
        if op >= 60:
            special = (word >> 6 & 31) << 2 | (op & 3)
            if special == 0x15:                                               # vftoi4
                for i in lanes:
                    if ft:
                        self.vf[ft][i] = M.vu_ftoi(x[i], 4)
                return
            if special == 0x1F:                                               # vclipw.xyz
                assert mask == 0xE, hex(word)
                v = x[:3] + [y[3]]
                if any((e >> 23) & 0xFF == 0xFF for e in v):
                    raise AssertionError(('clip lane with exponent 255 (not measured)',))
                daz = lambda b: b & 0x80000000 if (b >> 23) & 0xFF == 0 else b  # noqa: E731
                w = daz(v[3]) & 0x7FFFFFFF
                f = 0
                for k in range(3):
                    e = daz(v[k])
                    if (e & 0x7FFFFFFF) > w:
                        f |= (2 if e >> 31 else 1) << (2 * k)
                self.clip = ((self.clip << 6) | f) & 0xFFFFFF
                return
        return FallEE.macro(self, word)

    def cop2(self, word, pc):
        rs, rt, rd = word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        if rs == 2:                                                           # cfc2 (the clip register)
            assert rd == 18, ('control register', rd, hex(pc))
            if rt:
                self.r[rt] = self.clip
            return
        return FallEE.cop2(self, word, pc)

    def execute(self, word, pc):
        if self.top and self.seen is not None:
            self.seen.add(pc)
        self.data = True
        self.word = word
        try:
            return FallEE.execute(self, word, pc)
        finally:
            self.data = False


def arg_values(ee, args):
    values = []
    for kind, reg in args:
        if kind == F:
            values.append(ee.f[reg] & MASK)
        elif kind == Q:   # a 64-bit argument register (001CD520's GIF tag)
            values.append(ee.r[reg] & 0xFFFFFFFFFFFFFFFF)
        else:
            values.append(ee.r[reg] & MASK)
    return tuple(values)


class Scribble:
    """A stub result that also writes memory (as a callee may), so the
    case checks that the translation re-reads what the original re-reads."""

    def __init__(self, result, writes):
        self.result, self.writes = result, list(writes)


class Script:
    """Stub results: per callee address a queue of values (ints, or float
    bit patterns for float results, callables of the oracle, or Scribble)."""

    def __init__(self, queues=None, default=None):
        self.queues = {a: list(v) for a, v in (queues or {}).items()}
        self.default = default or {}

    def next(self, address, ee):
        queue = self.queues.get(address)
        value = queue.pop(0) if queue else self.default.get(address, 0)
        return value(ee) if callable(value) else value


def key_of(address):
    """Canonical key of an original address: the RAM offset (the EE mirrors
    RAM below 0x40000000), the scratchpad address or the stack-window
    address; None elsewhere (the rest of the oracle's private stack)."""
    address &= MASK
    if 0x70000000 <= address < 0x70000000 + SPAD_SIZE:
        return address
    if STACK_LO <= address < STACK_TOP:
        return address
    if address < 0x40000000:
        return address & (RAM_SIZE - 1)
    return None


# Argument registers of the EE calling convention: a0-a3 and t0-t3 (the
# EABI's a4-a7) for words, f12-f19 for floats.
INT_ARGS, FLOAT_ARGS = range(4, 12), range(12, 20)


def start(ee, entry, values):
    """Set the entry's argument registers (words sign-extended into a0..,
    floats as bits into f12.., Q as the whole 64-bit register) and run it."""
    words = floats = 0
    for kind, value in zip(FUNCS[entry][2], values):
        if kind == S:   # the entry sp: the oracle runs with sp = STACK_TOP
            assert value == STACK_TOP
            continue
        if kind == F:
            ee.f[12 + floats] = value & MASK
            floats += 1
        elif kind == Q:
            ee.r[4 + words] = value & MASK64
            words += 1
        else:
            ee.r[4 + words] = sx32(value)
            words += 1
    ee.r[31] = RETURN
    ee.run(entry)


class Oracle:
    """One run of the original function. Everything that changes RAM or the
    scratchpad (the function's own stores, the stores of callees that run as
    original code, the writes of scribbling stubs) is logged in order in
    `stores`; `marks[i]` is the length of that log when call i was entered,
    so the oracle's full memory at the entry of any call can be rebuilt as
    the start image plus stores[:marks[i]]. `first` maps each byte the
    function itself touched to its first access ('r', or ('w', value))."""

    def __init__(self, ram, spad, entry, args, script, run_set, seen, own=False, stack=None):
        ee = self.ee = Coverage(None, ram, spad, own)
        ee.seen = seen
        ee.word = 0
        ee.stack[STACK_LO - 0x7F000000:STACK_TOP - 0x7F000000] = stack or bytes(STACK_SIZE)
        self.window = bytearray(stack or bytes(STACK_SIZE))
        self.log, self.stores, self.marks, self.first, self.problems = [], [], [], {}, []
        self.written = set()    # every byte the function itself stored
        self.access = [[]]      # per call interval: the function's own loads and stores, in order (Oracle.load/write)
        self.stopped = None     # the address of a top-level access outside the memory map
        self.rehearsals = 0
        self.capture = None     # writes of the running callee (replayed on the native side)
        self.undo = None        # the register rehearsal's undo log
        self.rehearsal = None
        self.script, self.run_set = script, run_set
        ee.save, ee.write, ee.load = self.save, self.write, self.load
        for address in BY_ADDRESS:
            ee.hooks[address] = self.handler(address)
        for callback in args[1]:
            assert callback not in ee.hooks, hex(callback)
            ee.hooks[callback] = self.callback_hook(callback)
        try:
            start(ee, entry, args[0])
        except AssertionError:
            if self.stopped is None:
                raise

    # ---- memory with logging -------------------------------------------
    def stop_if_unmapped(self, address, size):
        """The function's own load or store outside the modelled memory
        (main RAM 0x00000000-0x01FFFFFF and the scratchpad; the oracle's
        private stack is also allowed) stops the run there; the native
        module must stop at the same address (fault code 5) after the same
        calls and stores (finish). The map is the one Replay.bytes serves.
        On the EE such an address is either TLB-unmapped (the access
        faults) or a window the harness does not model (the uncached RAM
        mirrors at 0x20000000 / 0x30100000, I/O, the BIOS); for those the
        test compares only the stop (docs/AREA04_PORT.md section 3). A
        halfword or word access at an address that is not a multiple of
        its size stops too: the EE raises an address error there."""
        address &= MASK
        if address % size:
            pass
        elif address + size <= RAM_SIZE or 0x70000000 <= address and address + size <= 0x70000000 + SPAD_SIZE:
            return
        if 0x7F000000 <= address < 0x7F100000:
            return
        self.stopped = address
        raise AssertionError(('address', hex(address)))

    def save(self, address, value, size=4):
        self.write(address, (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'little'))

    def write(self, address, data):
        ee, data = self.ee, bytes(data)
        key = key_of(address)
        if self.undo is not None:
            self.undo.append((address, EE.read(ee, address, len(data))))
            EE.write(ee, address, data)
            if key is not None:
                self.rehearsal.append((key, data))
            return
        old = None
        if ee.top:
            self.stop_if_unmapped(address, len(data))
            old = bytes(EE.read(ee, address, len(data)))
            if key is not None and in_window(key) and stack_frame_op(ee.word):
                key = None   # frame bookkeeping (stack_frame_op)
        EE.write(ee, address, data)
        if key is None:
            return
        if in_window(key):
            # The window as the logged stores leave it: the frame
            # bookkeeping of nested translated functions (their saved
            # registers) also lands in the window but is not logged, and the
            # native side does not make it; a store's `changed` mark is
            # taken against this view, the one the native side has.
            at = key - STACK_LO
            if ee.top:
                old = bytes(self.window[at:at + len(data)])
            self.window[at:at + len(data)] = data
        self.stores.append((key, data))
        if self.capture is not None:
            self.capture.append((key, data))
        if ee.top:
            self.access[-1].append((key, len(data), old != data, 'store'))
            for i, value in enumerate(data):
                self.first.setdefault(key + i, ('w', value))
                self.written.add(key + i)

    def load(self, address, size=4):
        ee = self.ee
        if ee.data and ee.top and self.undo is None:
            self.stop_if_unmapped(address, size)
            key = key_of(address)
            if key is not None and in_window(key) and stack_frame_op(ee.word):
                key = None   # frame bookkeeping (stack_frame_op)
            if key is not None:
                self.access[-1].append((key, size, False, 'load'))
                for i in range(size):
                    self.first.setdefault(key + i, ('r',))
        return EE.load(ee, address, size)

    # ---- callees ----------------------------------------------------------
    def nested(self, address):
        ee = self.ee
        saved_hooks, saved_seen = ee.hooks, ee.seen
        ee.hooks, ee.top = {}, False
        try:
            return ee.nested(address)
        finally:
            ee.hooks, ee.top, ee.seen = saved_hooks, True, saved_seen

    def rehearse(self, address, arg_spec, result):
        """Run the helper once with every argument register it is NOT
        given by its hook poisoned, undo its writes, then run it for real:
        the two runs must write the same bytes and return the same result,
        so the hook's argument list is everything the helper reads."""
        ee = self.ee
        saved = (list(ee.r), list(ee.rh), list(ee.f))
        words = {reg for kind, reg in arg_spec if kind != F}
        floats = {reg for kind, reg in arg_spec if kind == F}
        for reg in INT_ARGS:
            ee.rh[reg] = 0x0123456789ABCDEF
            if reg not in words:
                ee.r[reg] = sx32(0x00A5A5A0 + 4 * reg)
        for reg in FLOAT_ARGS:
            if reg not in floats:
                ee.f[reg] = 0x3F9E0419 ^ (reg << 4)
        self.undo, self.rehearsal = [], []
        try:
            v0, f0 = self.nested(address)
            poisoned = (v0 & (MASK64 if result == Q else MASK), f0 & MASK, self.rehearsal)
        except AssertionError as error:
            poisoned = ('fault', repr(error))
        finally:
            for where, old in reversed(self.undo):
                EE.write(ee, where, old)
            self.undo = self.rehearsal = None
            ee.r, ee.rh, ee.f = list(saved[0]), list(saved[1]), list(saved[2])
        return poisoned

    def run_callee(self, address, arg_spec, result):
        poisoned = self.rehearse(address, arg_spec, result)
        self.rehearsals += 1
        self.capture = writes = []
        try:
            v0, f0 = self.nested(address)
        finally:
            self.capture = None
        same = poisoned[0] != 'fault' and poisoned[2] == writes and (
            result is None or (poisoned[1] == f0 & MASK if result == F else
                                  poisoned[0] == v0 & (MASK64 if result == Q else MASK)))
        if not same:
            self.problems.append(('callee %06X reads a register outside its hook arguments' % address,
                                  poisoned if poisoned[0] == 'fault' else 'result or writes differ'))
        return v0, f0, writes

    def handler(self, address):
        name, arg_spec, result = BY_ADDRESS[address]

        def hook(e):
            self.marks.append(len(self.stores))
            self.access.append([])
            values = arg_values(e, arg_spec)
            if address in self.run_set:
                v0, f0, writes = self.run_callee(address, arg_spec, result)
                e.r[2], e.f[0] = v0, f0
            else:
                writes = []
                value = self.script.next(address, e)
                if isinstance(value, Scribble):
                    e.top = False
                    self.capture = writes
                    try:
                        for where, data in value.writes:
                            e.write(where, data)
                    finally:
                        e.top, self.capture = True, None
                    value = value.result
                if result == F:
                    e.f[0] = value & MASK
                elif result == Q:
                    e.r[2] = value & MASK64
                elif result:
                    e.r[2] = sx32(value)
            outcome = None
            if result == F:
                outcome = e.f[0] & MASK
            elif result == Q:
                outcome = e.r[2] & MASK64
            elif result:
                outcome = e.r[2] & MASK
            self.log.append((name, values, outcome, writes))
        return hook


    def callback_hook(self, callback):
        def cb(e):   # the actor's +0x4C method: stubbed, logged
            self.marks.append(len(self.stores))
            self.access.append([])
            self.log.append(('w_callback', (callback, e.r[4] & MASK), None, []))
        return cb


# ----------------------------------------------------------------------------
# the native side
# ----------------------------------------------------------------------------
LIBC = C.CDLL(None)
LIBC.memcmp.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t]
LIBC.memcmp.restype = C.c_int


def buffer_address(data):
    return C.addressof((C.c_uint8 * len(data)).from_buffer(data))


def first_difference_at(a, b, size):
    """First differing offset of two buffers given by address."""
    lo, hi, step = 0, size, 0x10000
    for start in range(lo, hi, step):
        n = min(step, hi - start)
        if LIBC.memcmp(a + start, b + start, n):
            x, y = C.string_at(a + start, n), C.string_at(b + start, n)
            for i in range(n):
                if x[i] != y[i]:
                    return start + i
    return None


class Replay:
    """The native side's hooks. At the entry of EVERY call (hook or the +0x4C
    callback) it first rebuilds the oracle's memory at the entry of the same
    original call (start image + stores[:marks[i]]) and compares ALL of RAM
    and the scratchpad with the native memory, then compares the callee and
    its arguments, and only then replays the callee's writes and result."""

    def __init__(self, oracle, buffers, stack=None):
        self.log, self.stores, self.marks = oracle.log, oracle.stores, oracle.marks
        self.oracle_access, self.stopped = oracle.access, oracle.stopped
        self.access = [[]]      # per call interval: every `bytes` request, in order: [address, size, changed]
        self.pending = None     # the last request: (pointer, size, bytes when handed out, its access entry)
        self.i, self.applied, self.errors = 0, 0, []
        self.ram, self.spad, self.expect_ram, self.expect_spad = buffers
        self.ram_base, self.spad_base = C.addressof(self.ram), C.addressof(self.spad)
        self.expect_ram_base = C.addressof(self.expect_ram)
        self.expect_spad_base = C.addressof(self.expect_spad)
        # the stack window (native and expected), loaded with the run's
        # start image: zeros, or the poisoned run's patches
        if not STACKS:
            STACKS.extend([(C.c_uint8 * STACK_SIZE)(), (C.c_uint8 * STACK_SIZE)()])
        for buffer in STACKS:
            C.memmove(C.addressof(buffer), stack or bytes(STACK_SIZE), STACK_SIZE)
        self.stack_base, self.expect_stack_base = (C.addressof(b) for b in STACKS)
        self.broken = False
        self.dirty = {}   # key -> size: bytes either side may have changed
        # hook_contract_site: call index -> ('fail', status) makes that hook
        # return the negative status without the callee's writes or result;
        # ('ok', status) returns the positive status after a normal replay
        self.inject = {}
        self.refuse_at, self.requests = None, 0   # hook_contract_site: `bytes` refuses request number refuse_at

    def check_ctx(self, ctx, name):
        """Every hook, `bytes` and the callback get the table's ctx."""
        if ctx != CTX:
            self.errors.append(('%s called with ctx %r, not the table\'s' % (name, ctx),))
            self.broken = True

    def bytes(self, ctx, address, size):
        self.check_ctx(ctx, 'bytes')
        self.settle()
        self.requests += 1
        if self.requests - 1 == self.refuse_at:
            return None
        if address % size:   # the EE raises an address error (Oracle.stop_if_unmapped)
            pointer = None
        elif 0x70000000 <= address and address + size <= 0x70000000 + SPAD_SIZE:
            pointer = self.spad_base + address - 0x70000000
        elif STACK_LO <= address and address + size <= STACK_TOP:
            pointer = self.stack_base + address - STACK_LO
        elif address + size <= RAM_SIZE:
            pointer = self.ram_base + address
        else:
            pointer = None
        if pointer is None:
            if self.stopped is None:
                self.errors.append(('unmapped address', hex(address)))
            return None
        if size > self.dirty.get(address, 0):
            self.dirty[address] = size
        entry = [address, size, False]
        self.access[-1].append(entry)
        self.pending = (pointer, size, C.string_at(pointer, size), entry)
        return pointer

    def settle(self):
        """The module uses the bytes of a `bytes` request (a04_at) before
        it makes its next request or call, so by then a store through the
        last pointer has happened: mark the request `changed` when its
        bytes differ from what they were when handed out. The oracle marks
        its stores the same way (a store of the value already there counts
        as unchanged on both sides)."""
        if self.pending is not None:
            pointer, size, before, entry = self.pending
            self.pending = None
            if C.string_at(pointer, size) != before:
                entry[2] = True

    def check_access(self, k):
        """Call interval k (entry to call 0, between calls k-1 and k, or
        after the last call): the native module's memory accesses there
        must be the original's, one for one and in the same order, each
        with the same address and size and the same `changed` mark (a
        store that changed memory). A read moved across a call or across a
        store, a value kept instead of read again, a read of another width,
        or an access the original does not make, shows here. The kind
        (load or store) of an access that changed nothing is not observed:
        `bytes` does not say. The door's jump-table loads are left out
        (TABLE_BYTES)."""
        self.settle()
        if k >= len(self.oracle_access) or k >= len(self.access):
            return True
        want = [(a, n, changed) for a, n, changed, _ in self.oracle_access[k] if a not in TABLE_BYTES]
        kinds = [kind for a, n, changed, kind in self.oracle_access[k] if a not in TABLE_BYTES]
        got = [tuple(e) for e in self.access[k]]
        if want == got:
            return True
        at = next((i for i, (w, g) in enumerate(zip(want, got)) if w != g), min(len(want), len(got)))
        where = 'before call %d (%s)' % (k, self.log[k][0]) if k < len(self.log) else 'after the last call'

        def show(items, marks=None):
            return ['%s%s %x/%d' % ('*' if e[2] else '', marks[i] if marks else '', e[0], e[1])
                    for i, e in enumerate(items)][max(0, at - 1):at + 3]
        self.errors.append(('memory accesses %s differ at access %d' % (where, at),
                            'original', show(want, kinds), 'native', show(got)))
        return False

    def compare(self, upto, where, full=None):
        """Bring the expected image to stores[:upto] and compare. `full`
        compares all 32 MiB of RAM and the whole scratchpad. Otherwise only
        the dirty set is compared: every byte the oracle has stored so far
        plus every range the native module has been handed through
        `bytes`. That is sufficient: the module reaches memory only through
        a pointer returned by `bytes` for exactly the size it asked for
        (a04_at in em_area04_port_internal.h), and replayed callee
        writes are oracle stores, so every other byte still holds the start
        image on both sides. EM_TEST_FULL=1 compares everything at every
        call entry as well; the final comparison is always full."""
        if full is None:
            full = reference_mode.FULL
        for key, data in self.stores[self.applied:upto]:
            if in_window(key):
                C.memmove(self.expect_stack_base + key - STACK_LO, data, len(data))
            elif key >= 0x70000000:
                C.memmove(self.expect_spad_base + key - 0x70000000, data, len(data))
            else:
                C.memmove(self.expect_ram_base + key, data, len(data))
            if len(data) > self.dirty.get(key, 0):
                self.dirty[key] = len(data)
        self.applied = upto
        bad_ram = bad_spad = bad_stack = None
        if full:
            if LIBC.memcmp(self.expect_stack_base, self.stack_base, STACK_SIZE):
                bad_stack = STACK_LO + first_difference_at(self.expect_stack_base, self.stack_base, STACK_SIZE)
            if LIBC.memcmp(self.expect_ram_base, self.ram_base, RAM_SIZE):
                bad_ram = first_difference_at(self.expect_ram_base, self.ram_base, RAM_SIZE)
            if LIBC.memcmp(self.expect_spad_base, self.spad_base, SPAD_SIZE):
                bad_spad = first_difference_at(self.expect_spad_base, self.spad_base, SPAD_SIZE)
        else:
            for key, size in self.dirty.items():
                if in_window(key):
                    at = key - STACK_LO
                    if LIBC.memcmp(self.expect_stack_base + at, self.stack_base + at, size):
                        at = key + first_difference_at(self.expect_stack_base + at, self.stack_base + at, size)
                        bad_stack = at if bad_stack is None else min(bad_stack, at)
                elif key >= 0x70000000:
                    at = key - 0x70000000
                    if LIBC.memcmp(self.expect_spad_base + at, self.spad_base + at, size):
                        at += first_difference_at(self.expect_spad_base + at, self.spad_base + at, size)
                        bad_spad = at if bad_spad is None else min(bad_spad, at)
                elif LIBC.memcmp(self.expect_ram_base + key, self.ram_base + key, size):
                    at = key + first_difference_at(self.expect_ram_base + key, self.ram_base + key, size)
                    bad_ram = at if bad_ram is None else min(bad_ram, at)
        if bad_ram is not None:
            self.errors.append(('RAM differs at %s' % where, hex(bad_ram),
                                'original %02X native %02X' % (self.expect_ram[bad_ram], self.ram[bad_ram])))
        if bad_spad is not None:
            self.errors.append(('scratchpad differs at %s' % where, hex(0x70000000 + bad_spad),
                                'original %02X native %02X' % (self.expect_spad[bad_spad], self.spad[bad_spad])))
        if bad_stack is not None:
            at = bad_stack - STACK_LO
            self.errors.append(('stack window differs at %s' % where, hex(bad_stack),
                                'original %02X native %02X' % (STACKS[1][at], STACKS[0][at])))
        return bad_ram is None and bad_spad is None and bad_stack is None

    def take(self, name, values):
        if self.broken:
            return None
        if self.i >= len(self.log):
            self.errors.append(('extra native call', name, [hex(v) for v in values]))
            self.broken = True
            return None
        if not self.compare(self.marks[self.i], 'entry of call %d (%s)' % (self.i, self.log[self.i][0])):
            self.broken = True
            return None
        entry = self.log[self.i]
        self.i += 1
        if entry[0] != name or entry[1] != values:
            self.errors.append(('call %d differs' % (self.i - 1), 'original', entry[0], [hex(v) for v in entry[1]],
                                'native', name, [hex(v) for v in values]))
            self.broken = True
            return None
        if not self.check_access(self.i - 1):
            self.broken = True
            return None
        self.access.append([])
        if self.inject.get(self.i - 1, ('ok',))[0] == 'fail':
            return entry
        for address, data in entry[3]:
            if in_window(address):
                C.memmove(self.stack_base + address - STACK_LO, data, len(data))
            elif address >= 0x70000000:
                C.memmove(self.spad_base + address - 0x70000000, data, len(data))
            else:
                C.memmove(self.ram_base + address, data, len(data))
        return entry

    def hook(self, name, args, result):
        def fn(ctx, *values):
            self.check_ctx(ctx, name)
            if result:
                values, out = values[:-1], values[-1]
            normal = tuple(float_arg_bits(v) if k == F else v & 0xFFFFFFFFFFFFFFFF if k == Q else v & MASK
                           for (k, _), v in zip(args, values))
            k = self.i
            entry = self.take(name, normal)
            if entry is None:
                return -1
            action = self.inject.get(k, ('ok', 0))
            if action[0] == 'fail':
                return action[1]
            if result == F:   # the bits, not a Python float (see FloatArg)
                C.cast(out, C.POINTER(C.c_uint32))[0] = entry[2] & MASK
            elif result == I:
                out[0] = s32(entry[2])
            elif result == U:
                out[0] = entry[2] & MASK
            elif result == Q:
                out[0] = entry[2] & MASK64
            return action[1]
        return hook_proto(args, result)(fn)

    def hooks(self, null=None):
        """The hook table; `null` names one field ('bytes', a hook or
        ) left NULL (hook_contract_site)."""
        self.keep = [BYTES_FN(self.bytes)]
        fields = {'ctx': CTX, 'bytes': self.keep[0]}
        for name, _, args, result in HOOKS:
            fields[name] = self.hook(name, args, result)
            self.keep.append(fields[name])

        def callback(ctx, fn, actor):
            self.check_ctx(ctx, 'w_callback')
            k = self.i
            if self.take('w_callback', (fn & MASK, actor & MASK)) is None:
                return -1
            return self.inject.get(k, ('ok', 0))[1]
        fields['w_callback'] = CALLBACK_FN(callback)
        self.keep.append(fields['w_callback'])
        if null is not None:
            del fields[null]
        return Hooks(**fields)


BUFFERS = []
STACKS = []   # the stack window: native, expected (Replay)
CTX = 0xA06C7C00   # the hook table's ctx in every Replay: opaque, never dereferenced by the module


def load_buffers(ram, spad):
    """The native RAM / scratchpad and the expected (rebuilt original)
    images, allocated once per worker process and loaded with the case's
    start image (native RAM, scratchpad, expected RAM, expected scratchpad)."""
    if not BUFFERS:
        BUFFERS.extend([(C.c_uint8 * RAM_SIZE)(), (C.c_uint8 * SPAD_SIZE)(),
                        (C.c_uint8 * RAM_SIZE)(), (C.c_uint8 * SPAD_SIZE)()])
    if IMAGES is not None:
        IMAGES.valid = False   # BUFFERS no longer hold what Images recorded
    for buffer, image in zip(BUFFERS, (ram, spad, ram, spad)):
        C.memmove(C.addressof(buffer), buffer_address(image), len(image))
    return BUFFERS


IMAGES = None   # this worker process's Images (run_case)


class WatchedMemory(bytearray):
    """A bytearray that records every item or slice assigned to it
    (`touched`, as (start, stop) offsets). The EE core writes memory only by
    slice assignment (EE.save / EE.write), so the recorded ranges hold
    every byte of the oracle's memory that any code path changed; run_native
    compares exactly those (plus the store log's) with the rebuilt image,
    which gives the same answer as comparing all 32 MiB."""

    def __init__(self, size):
        bytearray.__init__(self, size)
        self.touched = []

    def __setitem__(self, key, value):
        bytearray.__setitem__(self, key, value)
        if isinstance(key, slice):
            start, stop, step = key.indices(len(self))
            assert step == 1, 'WatchedMemory: extended slice'
            self.touched.append((start, max(start, stop)))
        else:
            key = key % len(self)
            self.touched.append((key, key + 1))


class Images:
    """A worker's four 32 MiB images, kept from case to case instead of
    copied for every run (the copies were most of the default run's CPU):
    the case's start image (`ram` / `spad`: a capture plus the case's
    patches), the oracle's working copy (`oracle_ram` / `oracle_spad`, the
    Oracle runs on it with own=True) and BUFFERS (native and expected).
    Every run starts from exactly the start image in all of them, as with
    fresh copies: after a run that passed, the bytes it can have changed are
    copied back from the start image. Those are the oracle's stores (its
    store log, which the self-check in run_native proves complete: the
    oracle's final memory is the start image plus that log, compared over
    all 32 MiB) and the native side's `bytes` ranges; the final comparison
    (all 32 MiB + scratchpad) has shown that native and expected memory are
    equal everywhere. After a failed run, a change of capture, or any other
    load of BUFFERS, the next case copies all four images again."""

    def __init__(self):
        self.beat, self.valid = None, False
        self.ram, self.spad = bytearray(RAM_SIZE), bytearray(SPAD_SIZE)
        self.oracle_ram, self.oracle_spad = WatchedMemory(RAM_SIZE), WatchedMemory(SPAD_SIZE)
        self.patched = {}   # key -> size where the start image differs from the capture
        self.stale = {}     # key -> size where the other three may differ from the start image

    @staticmethod
    def _mark(spans, key, size):
        if size > spans.get(key, 0):
            spans[key] = size

    def _apply(self, patches):
        for address, value in patches:
            if address >= 0x70000000:
                at = address - 0x70000000
                self.spad[at:at + len(value)] = value
            else:
                self.ram[address:address + len(value)] = value
            self._mark(self.patched, address, len(value))
            self._mark(self.stale, address, len(value))

    def start(self, beat, patches):
        """Make `beat` + patches (RAM and scratchpad addresses) the start
        image of all four; returns (ram, spad)."""
        ram0, spad0 = CAPTURES[beat]
        if not BUFFERS:
            BUFFERS.extend([(C.c_uint8 * RAM_SIZE)(), (C.c_uint8 * SPAD_SIZE)(),
                            (C.c_uint8 * RAM_SIZE)(), (C.c_uint8 * SPAD_SIZE)()])
            self.valid = False
        if not self.valid or beat != self.beat:
            self.ram[:], self.spad[:] = ram0, spad0
            self.patched, self.stale = {}, {}
            self._apply(patches)
            self.oracle_ram[:], self.oracle_spad[:] = self.ram, self.spad
            self.oracle_ram.touched, self.oracle_spad.touched = [], []
            for buffer, image in zip(BUFFERS, (self.ram, self.spad, self.ram, self.spad)):
                C.memmove(C.addressof(buffer), buffer_address(image), len(image))
            self.beat, self.valid, self.stale = beat, True, {}
            return self.ram, self.spad
        for key, size in self.patched.items():
            if key >= 0x70000000:
                at = key - 0x70000000
                self.spad[at:at + size] = spad0[at:at + size]
            else:
                self.ram[key:key + size] = ram0[key:key + size]
            self._mark(self.stale, key, size)
        self.patched = {}
        return self.more(patches)

    def more(self, patches):
        """Add patches to the current start image (the poisoned run)."""
        self._apply(patches)
        nat_ram, nat_spad, exp_ram, exp_spad = BUFFERS
        for key, size in self.stale.items():
            if key >= 0x70000000:
                at = key - 0x70000000
                chunk = bytes(self.spad[at:at + size])
                self.oracle_spad[at:at + size] = chunk
                C.memmove(C.addressof(nat_spad) + at, chunk, len(chunk))
                C.memmove(C.addressof(exp_spad) + at, chunk, len(chunk))
            else:
                chunk = bytes(self.ram[key:key + size])
                self.oracle_ram[key:key + size] = chunk
                C.memmove(C.addressof(nat_ram) + key, chunk, len(chunk))
                C.memmove(C.addressof(exp_ram) + key, chunk, len(chunk))
        self.stale = {}
        self.oracle_ram.touched, self.oracle_spad.touched = [], []
        return self.ram, self.spad

    def ran(self, oracle, replay, passed):
        """After a run: record what it may have changed, or (failed run, no
        replay) give up and copy everything before the next case."""
        if not passed or oracle is None or replay is None:
            self.valid = False
            return
        for key, data in oracle.stores:
            if not in_window(key):   # the window is reloaded by every Replay
                self._mark(self.stale, key, len(data))
        for key, size in replay.dirty.items():
            if not in_window(key):
                self._mark(self.stale, key, size)
        for memory, base in ((self.oracle_ram, 0), (self.oracle_spad, 0x70000000)):
            for start, stop in memory.touched:
                self._mark(self.stale, base + start, stop - start)


def finish(replay, oracle, kind, status, result, fault, full=True):
    """Everything compared once the native entry has returned: no original
    call skipped, the bytes accessed after the last call, memory after the
    last store (all 32 MiB + scratchpad unless `full` is False), and the
    outcome. When the original stops at a load or store outside the memory
    map (Oracle.stop_if_unmapped), the native module must have latched fault
    5 at that same address, after the same calls and the same stores."""
    errors = replay.errors
    if not replay.broken:
        if replay.i != len(replay.log):
            errors.append(('native skipped original calls', [e[0] for e in replay.log[replay.i:replay.i + 4]]))
        replay.check_access(replay.i)
        replay.compare(len(replay.stores), 'return', full=full)
    if oracle.stopped is not None:
        if (status, fault.code, fault.address) != (-1, 5, oracle.stopped):
            errors.append(('original stops at unmapped %s' % hex(oracle.stopped),
                           'native', status, hex(fault.address), fault.code))
    else:
        if status != 0 or fault.code:
            errors.append(('native fault', status, hex(fault.address), fault.code))
        if kind == F:
            if oracle.ee.f[0] & MASK != result & MASK:
                errors.append(('return f0', hex(oracle.ee.f[0] & MASK), hex(result & MASK)))
        elif kind is not None and s32(oracle.ee.r[2]) != result:
            errors.append(('return', s32(oracle.ee.r[2]), result))
    return errors


def native_args(entry, values):
    """The native entry's arguments: words as uint32, floats as FloatArg
    holding the bits (so a signalling NaN crosses unchanged), Q as uint64."""
    out = []
    for kind, value in zip(FUNCS[entry][2], values):
        if kind == F:
            out.append(FloatArg.from_buffer_copy(struct.pack('<I', value & MASK)))
        elif kind == Q:
            out.append(C.c_uint64(value & MASK64))
        elif kind == I:
            out.append(C.c_int32(s32(value)))
        else:
            out.append(C.c_uint32(value & MASK))
    return out


def native_call(native, ram, spad, entry, args, oracle, null=None, inject=None, refuse_at=None, buffers=None,
                stack=None):
    """One native run over the start image `ram` / `spad` (and the stack
    window image `stack`) against `oracle` (Replay). `null` leaves one
    hook-table field NULL, `inject` scripts hook statuses and `refuse_at`
    makes `bytes` refuse that request (hook_contract_site); `buffers` are
    already loaded with the start image. Returns (replay, status, fault,
    result)."""
    replay = Replay(oracle, buffers or load_buffers(ram, spad), stack)
    replay.inject = inject or {}
    replay.refuse_at = refuse_at
    hooks = replay.hooks(null)
    fault = Fault()
    symbol, _, kinds, kind = FUNCS[entry]
    fn = getattr(native, symbol)
    result = C.c_int32(0x5A5A5A5A)
    call = [C.byref(hooks)] + native_args(entry, args)
    if kind is not None:
        call.append(C.byref(result))
    status = fn(*call, C.byref(fault))
    return replay, status, fault, result.value


def run_native(native, ram, spad, entry, args, oracle, buffers=None, replays=None, stack=None):
    """`ram` / `spad` are the start image (bytearrays); `buffers`, if
    given, already hold it (Images). Returns the errors; the Replay is
    appended to `replays` if that is given."""
    replay, status, fault, result = native_call(native, ram, spad, entry, args, oracle, buffers=buffers,
                                                stack=stack)
    if replays is not None:
        replays.append(replay)
    errors = finish(replay, oracle, FUNCS[entry][3], status, result, fault)
    if not replay.broken:
        # harness self-check: the store log rebuilds the oracle's final memory
        ee = oracle.ee
        if not rebuilt(replay, ee):
            errors.append(('oracle store log does not rebuild the final memory',))
    return errors


def rebuilt(replay, ee):
    """The expected image (start image + the oracle's store log) equals the
    oracle's final memory: over all 32 MiB + scratchpad, or, when the
    oracle ran on WatchedMemory, over every byte any code path wrote there
    plus every byte the log wrote (the rest is the start image on both
    sides, so the answer is the same)."""
    if not isinstance(ee.mem, WatchedMemory):
        return not (LIBC.memcmp(replay.expect_ram_base, buffer_address(ee.mem), RAM_SIZE)
                    or LIBC.memcmp(replay.expect_spad_base, buffer_address(ee.spad), SPAD_SIZE))
    if len(ee.mem) != RAM_SIZE or len(ee.spad) != SPAD_SIZE:
        return False
    spans = [(key, key + len(data)) for key, data in replay.stores if not in_window(key)]
    spans += list(ee.mem.touched) + [(0x70000000 + a, 0x70000000 + b) for a, b in ee.spad.touched]
    for start, stop in spans:
        if start >= 0x70000000:
            at, size = start - 0x70000000, stop - start
            if bytes(ee.spad[at:at + size]) != C.string_at(replay.expect_spad_base + at, size):
                return False
        elif bytes(ee.mem[start:stop]) != C.string_at(replay.expect_ram_base + start, stop - start):
            return False
    return True


# ----------------------------------------------------------------------------
# cases
# ----------------------------------------------------------------------------
def patched(data, patches):
    out = bytearray(data)
    for address, value in patches:
        at = address - 0x70000000 if address >= 0x70000000 else address
        out[at:at + len(value)] = value
    return out


def b8(v): return bytes([v & 0xFF])
def h16(v): return struct.pack('<H', v & 0xFFFF)
def w32(v): return struct.pack('<I', v & MASK)
def f32b(v): return struct.pack('<I', fbits(v))


NATIVE = None
CASES = []


def run_case_index(index):
    """Forked workers get an index (the scripts hold closures); CASES is
    inherited through fork."""
    return run_case(CASES[index])


def run_case_chunk(indices):
    return [run_case(CASES[index]) for index in indices]


def run_cases(selected, chunk=16):
    """run_case over every case (CASES = selected), in the worker processes,
    results in case order. Cases go out in chunks of the same capture, so a
    worker's Images changes capture (a full copy) rarely."""
    cost = lambda i: 3 if selected[i][2] in HEAVY else 1   # noqa: E731
    order = sorted(range(len(selected)), key=lambda i: (-cost(i), selected[i][1], i))
    chunks = []
    for i in order:
        if chunks and len(chunks[-1]) < chunk and selected[chunks[-1][0]][1] == selected[i][1] \
                and cost(chunks[-1][0]) == cost(i):
            chunks[-1].append(i)
        else:
            chunks.append([i])
    out = [None] * len(selected)
    for indices, results in zip(chunks, reference_mode.parallel_map(
            run_case_chunk, chunks, cost=lambda c: sum(cost(i) for i in c))):
        for i, result in zip(indices, results):
            out[i] = result
    return out


def poison_patches(oracle, ram, spad):
    """The poisoned start image, as (patches, stack window image). (1) Every
    byte whose first access by the function is a store is preset to a
    value different from the first value stored there, so a store that is
    moved across a call, dropped or aimed at the wrong byte leaves memory
    that the call-entry or final comparison sees. (2) Every byte the
    function never touches that shares an aligned 8-byte group with a byte
    it stores is flipped too, so a store of the wrong width shows. Runs of
    bytes are merged. Stack-window bytes go into the window image (which
    otherwise starts zeroed)."""
    first = oracle.first
    values = {k: first[k][1] ^ 0x5A for k, access in first.items() if access[0] == 'w'}
    for key in oracle.written:
        for k in range(key & ~7, (key & ~7) + 8):
            if k not in first and k not in values:
                if in_window(k):
                    values[k] = 0xA5
                elif k >= 0x70000000:
                    if k - 0x70000000 < SPAD_SIZE:
                        values[k] = spad[k - 0x70000000] ^ 0xA5
                elif k < RAM_SIZE:
                    values[k] = ram[k] ^ 0xA5
    stack = bytearray(STACK_SIZE)
    for k in [k for k in values if in_window(k)]:
        stack[k - STACK_LO] = values.pop(k)
    patches, run = [], []
    for key in sorted(values):
        if run and key == run[-1] + 1 and (key >= 0x70000000) == (run[0] >= 0x70000000):
            run.append(key)
        else:
            if run:
                patches.append((run[0], bytes(values[k] for k in run)))
            run = [key]
    if run:
        patches.append((run[0], bytes(values[k] for k in run)))
    return patches, (bytes(stack) if any(stack) else None)


SELF_ENTRIES = {0x1BD9F0, 0x1BDE60, 0x1BE6C0, 0x1BF6B0, 0x12E3A0, 0x12E840}   # the entries that reach their node's +0x4C method
SCRIBBLED_CALLBACKS = set()
HEAVY = {0x219870, 0x824560, 0x207350, 0x1944B0, 0x1CE860}


def callbacks_of(entry, native_args, ram):
    """The +0x4C callbacks the oracle stubs: the entry's own node's (for the
    entries that reach it) and the scribbled ones."""
    if entry not in SELF_ENTRIES:
        return set(SCRIBBLED_CALLBACKS)
    return {u32(ram, native_args[0] + 0x4C)} | SCRIBBLED_CALLBACKS


def run_one(entry, native_args, ram, spad, script, run_set, seen, images=None, stack=None):
    """Oracle then native on the same start image; returns (errors, oracle).
    With `images` (Images), ram / spad are its start image and the oracle
    and native sides run on its prepared copies. `stack`: the stack
    window's start image (None: zeros)."""
    callbacks = callbacks_of(entry, native_args, ram)
    try:   # the oracle works on its own copy; ram / spad stay the start image
        if images is None:
            oracle = Oracle(ram, spad, entry, (native_args, callbacks), script, run_set, seen, stack=stack)
        else:
            oracle = Oracle(images.oracle_ram, images.oracle_spad, entry, (native_args, callbacks), script, run_set,
                            seen, own=True, stack=stack)
    except AssertionError as error:
        if images is not None:
            images.ran(None, None, False)
        return [('oracle failed', repr(error))], None
    replays = []
    errors = list(oracle.problems) + run_native(NATIVE, ram, spad, entry, native_args, oracle,
                                                buffers=BUFFERS if images is not None else None, replays=replays,
                                                stack=stack)
    if images is not None:
        images.ran(oracle, replays[0] if replays else None, not errors)
    return errors, oracle


def copy_script(script):
    """A fresh copy of a case's Script (an oracle run consumes its queues)."""
    copy = Script()
    copy.queues = {a: list(q) for a, q in script.queues.items()}
    copy.default = script.default
    return copy


def run_case(case):
    """One case: (label, beat, entry, args, patches, spad_patches, script,
    run_set), run twice: as given, then from the poisoned start image
    (poison_patches). Returns (label, entry, errors, seen pcs, counts, calls)
    with counts = (runs, call entries compared, helper register rehearsals)
    and calls the callee names of the as-given run (None when the case
    failed or the original stopped)."""
    global IMAGES
    label, beat, entry, native_args, patches, spad_patches, script, run_set = case
    if IMAGES is None:
        IMAGES = Images()
    ram, spad = IMAGES.start(beat, list(patches) + list(spad_patches))
    seen = set()
    script_copy = copy_script(script)
    errors, oracle = run_one(entry, native_args, ram, spad, copy_script(script), run_set, seen, IMAGES)
    counts = [1, 0, 0, 0]
    calls = None   # the as-given run's callee names, for hook_sites (None: not usable there)
    if oracle is not None:
        counts[1] += len(oracle.marks)
        counts[2] += oracle.rehearsals
        counts[3] = len(oracle.log) + sum(len(interval) for interval in oracle.access)   # hook_sites' cost
        if oracle.stopped is None:
            calls = tuple(e[0] for e in oracle.log)
    if errors or oracle is None:
        return label, entry, errors, seen, counts, None
    poison, stack = poison_patches(oracle, ram, spad)
    if poison or stack:
        ram_p, spad_p = IMAGES.more(poison)
        more, second = run_one(entry, native_args, ram_p, spad_p, script_copy, run_set, seen, IMAGES, stack)
        errors += [('poisoned run',) + tuple(e) for e in more]
        counts[0] += 1
        if second is not None:
            counts[1] += len(second.marks)
            counts[2] += second.rehearsals
    return label, entry, errors, seen, counts, calls


HEAVY = {0x12E3A0, 0x12E840, 0x12EB60, 0x12F100, 0x12FC10, 0x1A7BA0, 0x1A7B80, 0x1BF6B0, 0x1328D0, 0x132FB0}
CAPTURES = {}
WIDE_RESULTS = (0x100, 0x10000, 0x80000000, 0x7FFFFFFF)


def fb(value):
    return fbits(value)


def up(value):
    """The next float above `value` (bits)."""
    b = fbits(value)
    return b + 1 if value > 0 or b == 0 else b - 1


def down(value):
    b = fbits(value)
    return b - 1 if value > 0 else b + 1


def bits32(b):
    return struct.pack('<I', b & MASK)


# ----------------------------------------------------------------------------
# overrides of the copied harness for this module
# ----------------------------------------------------------------------------
def load_captures(elf):
    """Every image must hold the overlay RESIDENT names, with its text equal
    to the user's extract/OVERLAY/<AREA>.BIN; every image's boot text and
    the boot jump tables must equal the pinned ELF, and AREA13's table at
    0x82E200 its file: every instruction the oracle executes is original."""
    overlays = {}
    for oid, path in OVERLAY_FILES.items():
        data = path.read_bytes()
        overlays[oid] = (data, ARENA + 0x40 + struct.unpack_from('<I', data, 12)[0])
    captures = {}
    for beat in BEATS:
        folder = ROUTE / beat
        ram = (folder / 'eeMemory.bin').read_bytes()
        spad = (folder / 'scratchpad.bin').read_bytes()
        assert len(ram) == RAM_SIZE and len(spad) == SPAD_SIZE, beat
        oid = RESIDENT[beat]
        assert ram[ARENA + 4] == oid, (beat, 'resident overlay', ram[ARENA + 4])
        data, end = overlays[oid]
        assert ram[ARENA:ARENA + 8] == data[:8], (beat, 'overlay header')
        assert ram[ARENA + 0x40:end] == data[0x40:end - ARENA], (beat, 'overlay text differs')
        assert ram[0x100000:0x241000] == elf[0x300:0x300 + 0x141000], (beat, 'boot text differs from the ELF')
        for table, count in JUMP_TABLES.values():
            if table < ARENA:
                at = 0x300 + table - 0x100000
                assert ram[table:table + 4 * count] == elf[at:at + 4 * count], (beat, 'jump table differs')
            elif oid == 10:
                at = table - ARENA
                assert ram[table:table + 4 * count] == data[at:at + 4 * count], (beat, 'overlay jump table differs')
        assert not any(ram[FREE:FREE + 0x40000]), (beat, 'the designed-record area is not zero')
        captures[beat] = (ram, spad)
    return captures


# the entries that reach a node's +0x4C method (directly or through a
# translated callee), and 0x827150's step table
SELF_ENTRIES = {0x823700, 0x823940, 0x823E90, 0x824180, 0x824BB0, 0x8266A0, 0x826850, 0x826FF0, 0x827150,
                0x8292A0, 0x8293A0, 0x1383C0, 0x1386E0, 0x1C06E0, 0x1C1030, 0x1C4BA0}
STEP_TABLE = 0x82D190
SCRIBBLED_CALLBACKS = set()
HEAVY = {0x824BB0, 0x827150, 0x1E5AC0, 0x1DE920, 0x1C06E0, 0x1549C0}


def callbacks_of(entry, native_args, ram):
    """The indirect calls the oracle stubs: the entry's own node's +0x4C
    method (for the entries that reach it) and, for 0x827150, the five step
    functions of its table."""
    out = set(SCRIBBLED_CALLBACKS)
    if entry in SELF_ENTRIES:
        out.add(u32(ram, native_args[0] + 0x4C))
    if entry == 0x827150:
        out |= {u32(ram, STEP_TABLE + 4 * i) for i in range(5)}
    return {c for c in out if c not in BY_ADDRESS and c not in FUNCS}


# ----------------------------------------------------------------------------
# cases
# ----------------------------------------------------------------------------
FREE = 0x1C00000          # RAM zero in every capture: the designed records live here
FULL_ONLY = set()         # labels of the cases EM_TEST_FULL=1 adds
CAPTURES = {}
FRAME = 0x70003B68
B8D = 0x70003B8D
WIDE = (0, 1, 2, 0x7FFFFFFF, -1)

# AREA13 nodes (a04b_04 and the a13 images; tools/area_overview.py --area 13)
R3, R4, HOLE5, HOLE6, R7 = 0x7AAB70, 0x7AAE60, 0x7AB150, 0x7AB440, 0x7AB730
DOOR14 = 0x7ACBC0
R44, PANEL, R47 = 0x7B23E0, 0x7B26D0, 0x7B29C0
R49 = (0x7B2CB0, 0x7B2FA0, 0x7B3290, 0x7B3580, 0x7B3870, 0x7B3B60)
R58, R59 = 0x7B4720, 0x7B4A10
HATCH62, HATCH63 = 0x7B52E0, 0x7B55D0
G11 = 0x7A7690
WATCHERS = (0x7A8830, 0x7A8E10, 0x7A93F0, 0x7A99D0)
PARTNERS = (0x7A8B20, 0x7A9100, 0x7A96E0, 0x7A9CC0)
EMITTER = 0x7B6D50       # a13_01: 001E7050 (its +0x24: the player)
WEATHER = 0x7BC570       # a13_01: 001E55F0's node
# AREA19 nodes (a13_05)
CREATURES = (0x7AA2A0, 0x7AA590, 0x7AA880)
KNOCK, PULSE, PART = 0x7ACEB0, 0x7AD1A0, 0x7B1820
FIELDS = (0x7BC280, 0x7BC570, 0x7BC860, 0x7BCB50, 0x7BCE40, 0x7BD130)
PADS19 = (0x7B4FF0, 0x7B52E0, 0x7B55D0)


def fw(*values):
    """Little-endian words (floats given as Python floats, ints as ints)."""
    return b''.join(struct.pack('<f', v) if isinstance(v, float) else struct.pack('<I', v & MASK) for v in values)


def scribble_at(result, address, data):
    return Scribble(result, [(address, data)])


def case_list(elf):
    rng = random.Random(0x1E9)
    cases = []

    def add(label, beat, entry, args, patches=(), spad=(), queues=None, default=None, run_set=RUN, full=False):
        """full=True: the case runs only with EM_TEST_FULL=1."""
        if full:
            FULL_ONLY.add(label)
        args = [STACK_TOP if k == S else a for k, a in zip(FUNCS[entry][2], list(args) + [STACK_TOP] * 5)]
        # scratchpad addresses go to the scratchpad patches (patched() takes one image)
        spad = list(spad) + [(a, v) for a, v in patches if a >= 0x70000000]
        patches = [(a, v) for a, v in patches if a < 0x70000000]
        cases.append((label, beat, entry, tuple(args), list(patches), list(spad),
                      Script(queues, default), frozenset(run_set)))

    capture_cases(add)
    captured = len(cases)
    area13_cases(add, rng)
    turret_cases(add, rng)
    exit_cases(add, rng)
    anim_cases(add, rng)
    fx_cases(add, rng)
    survivor_cases(add)
    return cases, captured, len(cases) - captured


BEHAVIOUR_ENTRY = {0x823700: 0x823700, 0x823BC0: 0x823BC0, 0x823E90: 0x823E90, 0x824BB0: 0x824BB0,
                   0x8266A0: 0x8266A0, 0x826850: 0x826850, 0x826FF0: 0x826FF0, 0x827150: 0x827150,
                   0x8292A0: 0x8292A0, 0x8293A0: 0x8293A0, 0x8299E0: 0x8299E0, 0x1383C0: 0x1383C0,
                   0x1546C0: 0x1546C0, 0x1C06E0: 0x1C06E0, 0x1C1030: 0x1C1030, 0x1C4BA0: 0x1C4BA0,
                   0x1E7050: 0x1E7050}


def capture_cases(add):
    """Every entry on the captured state of every image it applies to."""
    for beat in BEATS:
        ram, _ = CAPTURES[beat]
        for behaviour, entry in BEHAVIOUR_ENTRY.items():
            if (behaviour >= ARENA) != (RESIDENT[beat] == 10) and behaviour >= ARENA:
                continue
            for node in owners(ram, behaviour):
                # the frame loop points D_00275B40 at the running actor's bone
                # array (+0x110) before its behaviour runs
                bones = [(0x275B40, w32(node + 0x110))]
                add('capture %s %06X @%X' % (beat, entry, node), beat, entry, [node], bones)
                if entry == 0x823BC0:
                    sub = 0x823C10 if ram[node + 0xD] == 0 else 0x823D50
                    add('capture %s %06X @%X' % (beat, sub, node), beat, sub, [node])
                if entry == 0x823700:
                    add('capture %s 823940 @%X' % (beat, node), beat, 0x823940, [node])
                if entry == 0x823E90:
                    add('capture %s 824180 @%X' % (beat, node), beat, 0x824180, [node])
                if entry == 0x824BB0:
                    rec_b = u32(ram, node + 0x110 + 0xC)
                    add('capture %s 826140 @%X' % (beat, node), beat, 0x826140, [node, rec_b + 0x90], bones)
                if entry == 0x1383C0:
                    add('capture %s 138540 @%X' % (beat, node), beat, 0x138540, [node, node + 0x1F0])
                    add('capture %s 1386E0 @%X' % (beat, node), beat, 0x1386E0, [node, node + 0x1F0])
                if entry == 0x1546C0:
                    add('capture %s 154460 @%X' % (beat, node), beat, 0x154460, [node])
                    add('capture %s 154740 @%X' % (beat, node), beat, 0x154740, [node, node + 0x1F0])
                    add('capture %s 1549C0 @%X' % (beat, node), beat, 0x1549C0, [node, node + 0x1F0])
        add('capture %s 183440' % beat, beat, 0x183440, [PLAYER])
        add('capture %s 1838B0' % beat, beat, 0x1838B0, [PLAYER])
        add('capture %s 196970' % beat, beat, 0x196970, [CAM, PLAYER])
        add('capture %s 196CE0' % beat, beat, 0x196CE0, [CAM, PLAYER])
        if RESIDENT[beat] == 10:
            bones = [(0x275B40, w32(DOOR14 + 0x110))]
            add('capture %s 1BDCA0' % beat, beat, 0x1BDCA0, [DOOR14 + 0x1F0], bones)
            add('capture %s 1BDD70' % beat, beat, 0x1BDD70, [DOOR14 + 0x1F0], bones)
            add('capture %s 1BA7F0 @%X' % (beat, R3), beat, 0x1BA7F0, [R3])
    for beat in ('a13_00_door8', 'a13_02_door17', 'a13_04_hatch'):
        add('capture %s 823A10' % beat, beat, 0x823A10, [])
        add('capture %s 823FE0' % beat, beat, 0x823FE0, [])
        add('capture %s 826FB0' % beat, beat, 0x826FB0, [HATCH62 + 0x1F0])
        add('capture %s 826FC0' % beat, beat, 0x826FC0, [HATCH62])
    add('capture a13_01 1DE920', 'a13_01_door14', 0x1DE920, [])
    add('capture a13_01 1E5AC0', 'a13_01_door14', 0x1E5AC0, [WEATHER, 0, 0x12345678, fbits(1.0)])
    for parent in PADS19:
        add('capture a13_05 15A200 @%X' % parent, 'a13_05_shaft', 0x15A200, [parent, 0xE, 0],
            queues={0x1AFA90: [FREE + 0x1000]})


def b8(v): return bytes([v & 0xFF])
def h16(v): return struct.pack('<H', v & 0xFFFF)
def w32(v): return struct.pack('<I', v & MASK)
def f32b(v): return struct.pack('<f', v)


def area13_cases(add, rng):
    """The AREA13 overlay rows: every state and sub-state with each callee
    result on both sides of its test (designed on the captured nodes, the
    state bytes patched)."""
    B = 'a13_00_door8'
    # 0x823700 ([3])
    for state in (0, 1, 2, 3, 4):
        for r in (0, 1):
            for c in (0, 1, 2):
                if state != 1 and (r or c):
                    continue
                add('a13 823700 s%d r%d c%d' % (state, r, c), B, 0x823700, [R3],
                    [(R3 + 4, b8(state)), (0x8107F1, b8(c))], queues={0x1BA1C0: [r]})
    for sub, b, r in ((0, 4, 0), (0, 0, 0), (0, 0xFB, 0), (1, 0, 0), (1, 0, 1), (2, 0, 0)):
        add('a13 823940 sub%d b%X r%d' % (sub, b, r), B, 0x823940, [R3],
            [(R3 + 5, b8(sub)), (R3 + 0xB, b8(b))], queues={0x1BA1F0: [r]})
    add('a13 823A10', B, 0x823A10, [])
    # 0x823BC0 and its handlers ([5] / [6])
    for d in (0, 1, 2, 0xFF):
        add('a13 823BC0 d%d' % d, B, 0x823BC0, [HOLE5], [(HOLE5 + 0xD, b8(d))])
    for entry, node in ((0x823C10, HOLE5), (0x823D50, HOLE6)):
        for state in (0, 1, 2, 3, 4):
            for r in (0, 1):
                for item in (0, 1):
                    for sub, b in ((0, 4), (0, 0), (1, 0), (2, 0)):
                        if state == 0 and (item or sub or b):
                            continue
                        if state != 1 and state != 0 and (r or item or sub or b):
                            continue
                        if state == 1 and item and (r or sub or b):
                            continue
                        add('a13 %06X s%d r%d i%d sub%d b%d' % (entry, state, r, item, sub, b), B, entry, [node],
                            [(node + 4, b8(state)), (node + 5, b8(sub)), (node + 0xB, b8(b)), (0x810C8B, b8(item))],
                            queues={0x1BA1C0: [r], 0x1BA1F0: [r]})
    # 0x823E90 ([44]) and its step 0x824180
    for state in (0, 1, 2, 3, 4):
        for step in ((0, 1, 2, 3, 4, 5, 0x31) if state == 1 else (0,)):
            for r in ((0, 1) if state == 0 else (0,)):
                add('a13 823E90 s%d step%X r%d' % (state, step, r), B, 0x823E90, [R44],
                    [(R44 + 4, b8(state)), (0x8107F4, b8(step)), (R44 + 5, b8(6))], queues={0x1BA1C0: [r]})
    for sub in range(8):
        for b in ((5, 4, 0, 1) if sub == 0 else (0,)):
            for a in ((0, 1) if sub == 1 else (0,)):
                for r in ((0, 1) if sub in (2, 3, 4) else (0,)):
                    add('a13 824180 sub%d b%d a%d r%d' % (sub, b, a, r), B, 0x824180, [R44],
                        [(R44 + 5, b8(sub)), (R44 + 0xB, b8(b)), (R44 + 0xA, b8(a)), (R44 + 0x34, b8(0x91))],
                        queues={0x1BA1F0: [r]})
    for player_y in (100.0, 210.0, 211.0, 300.0):
        for flag in (0, 0xFF, 1):
            for r in (0, 1):
                add('a13 823FE0 y%g f%X r%d' % (player_y, flag, r), B, 0x823FE0, [],
                    [(0x810354, f32b(player_y)), (0x810774, b8(flag))], queues={0x1B1EA0: [r]})
    add('a13 823FE0 y up', B, 0x823FE0, [], [(0x810354, bits32(up(210.0)))], queues={0x1B1EA0: [1]})
    # 0x8266A0 (the partners)
    p = PARTNERS[0]
    for state in (0, 1, 2, 3, 4):
        for r in (0, 1):
            for rr in (0, 1):
                for e702 in ((7, 8, 9) if state == 1 else (8,)):
                    for hit in ((0, 1) if state == 1 else (0,)):
                        for count in ((5, 9, 10, 11) if state == 2 else (0,)):
                            add('a13 8266A0 s%d r%d rr%d e%d h%d c%d' % (state, r, rr, e702, hit, count), B,
                                0x8266A0, [p], [(p + 4, b8(state)), (0x810702, b8(e702)), (p + 0x36, h16(hit)),
                                                (p + 0x28, h16(count))],
                                queues={0x1B0FD0: [r], 0x1B11E0: [rr], 0x1EFE00: [rr]})
    # 0x826850 (the hatches)
    for node in (HATCH62, HATCH63):
        for state in (0, 1, 2, 3, 5):
            for item in (0, 1):
                for bits in ((0, 1, 2, 3) if state == 0 else (0,)):
                    for r in (0, 1):
                        for sub, b, count in (((0, 4, 0), (0, 0, 0), (1, 0, 3), (1, 0, 0), (2, 0, 0))
                                              if state == 1 else ((0, 0, 0),)):
                            if state == 2 and item:
                                continue
                            add('a13 826850 @%X s%d i%d bits%d r%d sub%d b%d c%d' % (node, state, item, bits, r, sub,
                                                                                    b, count), B, 0x826850, [node],
                                [(node + 4, b8(state)), (node + 5, b8(sub)), (node + 0xB, b8(b)),
                                 (node + 0x28, h16(count)), (0x810C8B, b8(item)), (0x810839, b8(bits))],
                                queues={0x1B0FD0: [r], 0x1B0F60: [r], 0x1BA1F0: [r], 0x1B17A0: [1 - r],
                                        0x1B1630: [r], 0x1C64F0: [0x1234], 0x1C6120: [0x4567]})
    add('a13 826FB0', B, 0x826FB0, [HATCH62 + 0x1F0])
    add('a13 826FC0', B, 0x826FC0, [HATCH62])
    # 0x826FF0 ([58] / [59])
    for state in (0, 1, 2, 3, 4):
        for r in (0, 1):
            for sub, b, x in (((0, 4, 700.0), (0, 4, 800.0), (0, 4, 900.0), (0, 0, 700.0), (1, 0, 0.0), (2, 0, 0.0))
                              if state == 1 else ((0, 0, 0.0),)):
                add('a13 826FF0 s%d r%d sub%d b%d x%g' % (state, r, sub, b, x), B, 0x826FF0, [R58],
                    [(R58 + 4, b8(state)), (R58 + 5, b8(sub)), (R58 + 0xB, b8(b)), (0x810350, f32b(x))],
                    queues={0x1B0FD0: [r], 0x1BA1F0: [r]})
    # 0x8292A0 ([49]..)
    n = R49[0]
    for state in (0, 0x64, 1, 3, 2):
        for flag in (0, 0x20):
            for r in (0, 1):
                for count in ((0, 1, -1) if state == 0x64 else (0,)):
                    add('a13 8292A0 s%X f%X r%d c%d' % (state, flag, r, count), B, 0x8292A0, [n],
                        [(n + 4, b8(state)), (0x8107F4, b8(flag)), (n + 0x28, h16(count))], queues={0x1B0FD0: [r]})
    # 0x8293A0 ([47])
    for state in (0, 1, 3, 2):
        for flag in (0, 0x40):
            for r in (0, 1):
                for model, count in (((0x13, 0), (0x13, 1), (0x12, 0), (0x13, 5)) if state == 1 else ((0, 0),)):
                    add('a13 8293A0 s%d f%X r%d m%X c%d' % (state, flag, r, model, count), B, 0x8293A0, [R47],
                        [(R47 + 4, b8(state)), (0x8107F4, b8(flag)), (R47 + 0xD, b8(model)), (R47 + 0x28, h16(count))],
                        queues={0x1B0FD0: [r]})
    # 0x8299E0 ([11])
    for state in (0, 1, 2, 3, 4):
        for r in (0, 1):
            for flag in ((0, 1) if state == 1 else (0,)):
                add('a13 8299E0 s%d r%d f%d' % (state, r, flag), B, 0x8299E0, [G11],
                    [(G11 + 4, b8(state)), (0x810774, b8(flag))], queues={0x15AC00: [r]})
    # 0x829AA0 (no live node in the end-of-beat images: a free record)
    node = FREE + 0x2000
    for state, count in ((0, 0), (1, 24), (1, 2), (1, 1), (1, 0), (2, 0), (3, 0), (4, 0)):
        add('a13 829AA0 s%d c%d' % (state, count), B, 0x829AA0, [node], [(node + 4, bytes([state, count]))],
            queues={0x1281C0: [0x12, 0x34, 0x56, 0x80], 0x1CB5F0: [FREE + 0x3000]}, run_set=RUN - {0x1281C0})
    # 0x827150 (the panel)
    for state in (0, 1, 2, 3, 4):
        for flag in (0, 0xFF):
            for model in ((0x11, 0x12, 0x10) if state == 1 else (0x11,)):
                for sub in ((0, 1, 2, 3, 4, 5) if state == 1 and model == 0x11 else (0,)):
                    add('a13 827150 s%d f%X m%X sub%d' % (state, flag, model, sub), B, 0x827150, [PANEL],
                        [(PANEL + 4, b8(state)), (0x810774, b8(flag)), (PANEL + 0xD, b8(model)), (PANEL + 5, b8(sub)),
                         (0x8107F4, b8(0x02 if sub == 1 else 0))],
                        queues={0x1CA7B0: [3, -1, 0, 5], 0x1C6120: [0x777]})
    blk = PANEL + 0x1F0
    for step, count, limit, shown, blink, latch, frame in (
            (0, 0, 1, 4, -1, 0, 0), (1, 2, 3, 3, 0, 0, 1), (2, 0, 1, 2, 3, 1, 8), (3, 4, 4, 1, 3, 0, 0),
            (4, 0, 1, 0, 5, 0, 0x20), (0, 0, 2, 4, 2, 1, 0x10), (2, 1, 1, 4, 31, 0, 0x80000000)):
        for sub, f7f4 in ((2, 0), (1, 2), (1, 0), (0, 0xFF)):
            add('a13 827150 blk %d %d %d %d %d %d %X sub%d' % (step, count, limit, shown, blink, latch, frame, sub), B,
                0x827150, [PANEL], [(PANEL + 4, b8(1)), (PANEL + 0xD, b8(0x11)), (PANEL + 5, b8(sub)),
                                     (0x8107F4, b8(f7f4)),
                                     (blk, fw(step, count, limit, shown, 0, 0, blink, latch)), (FRAME, w32(frame))],
                queues={0x1CA7B0: [-1, 2, 3, 4]})


def bits32(b):
    return struct.pack('<I', b & MASK)


TARGET = FREE + 0x5000     # a designed record the watcher tracks (+0xB0 position)
HITREC = FREE + 0x6000     # a designed probe hit polygon (+0x24.. point, +0x1A attribute)
HITOBJ = FREE + 0x6800     # a designed hit object (0x700031D4: +0 flags, +2, +3 kind, +0x36)
COMP = FREE + 0x7000       # a designed companion record


def probe_scribble(result, kind, obj=HITOBJ, poly=HITREC, point=(0.0, 0.0, 0.0)):
    """A 0019A570 / 0019AA80 stub that writes the probe's outputs: the hit
    point 0x700031B0, the polygon 0x700031D0, the object 0x700031D4, the
    kind 0x700031D8."""
    return Scribble(result, [(0x700031B0, fw(point[0], point[1], point[2], 1.0)),
                             (0x700031D0, w32(poly)), (0x700031D4, w32(obj)), (0x700031D8, w32(kind))])


def turret_cases(add_plain, rng):
    """0x824BB0 (the watchers) and 0x826140 (its probe). In the game the
    frame loop points D_00275B40 at the running actor's bone array (actor
    +0x110) before its behaviour runs; the designed cases do the same."""
    B = 'a13_01_door14'
    t = WATCHERS[0]
    ram, _ = CAPTURES[B]
    block = t + 0x110

    def add(label, beat, entry, args, patches=(), **kw):
        add_plain(label, beat, entry, args, [(0x275B40, w32(block))] + list(patches), **kw)
    rec_a, rec_b = u32(ram, block + 8), u32(ram, block + 0xC)
    comp = u32(ram, t + 0x220)
    hitrec = [(HITREC + 0x24, fw(1.0, 2.0, 3.0)), (HITREC + 0x1A, b8(0x5A)),
              (HITOBJ, b8(0)), (HITOBJ + 2, b8(0)), (HITOBJ + 3, b8(0x11))]
    # state 0
    for r in (0, 1):
        for c in (0, COMP):
            add('turret s0 r%d c%X' % (r, c), B, 0x824BB0, [t], [(t + 4, b8(0))],
                queues={0x1B0FD0: [r], 0x1AFA90: [c], 0x00122BB8: [0x40000000]})
    # states 4 / 1, the entry gate and the countdown
    for state in (4, 1):
        for e702 in (7, 8):
            add('turret s%d gate %d' % (state, e702), B, 0x824BB0, [t], [(t + 4, b8(state)), (0x810702, b8(e702))])
        for left in (1, 0x1E, 0x1F, 0x20, 0x100):
            for c20c, c214 in ((1, 1), (2, 2), (1, 2), (2, 1)):
                for rnd in (0x00040000, 0x00080000, 0x7FFF0000):
                    add('turret s%d count %X %d %d %X' % (state, left, c20c, c214, rnd), B, 0x824BB0, [t],
                        [(t + 4, b8(state)), (0x810702, b8(8)), (t + 0x208, w32(left)), (t + 0x20C, w32(c20c)),
                         (t + 0x214, w32(c214))], default={0x122BB8: rnd})
        for a74 in (1.2, -1.2, 0.0, 1.13, -1.13, -1.3):
            for phase in (3.1, 3.2):
                add('turret s%d sweep %g %g' % (state, a74, phase), B, 0x824BB0, [t],
                    [(t + 4, b8(state)), (0x810702, b8(8)), (t + 0x208, w32(0x40)), (t + 0x20C, w32(5)),
                     (t + 0x214, w32(5)), (rec_a + 0x74, f32b(a74)), (t + 0x1FC, f32b(phase)),
                     (t + 0x210, f32b(0.1)), (t + 0x218, f32b(0.2))])
    # state 4 idle
    for c28 in (0, 3, 5, 0x30):
        for flag in (0, 1):
            for step in (0.016, -0.016):
                for a74 in (1.2, -1.2, 0.0):
                    for c36, c208 in ((0, 0), (1, 0)):
                        for c204 in (0, TARGET):
                            for frame in (0, 1):
                                full = not (c28 in (0, 2) and a74 == 0.0) and not (frame == 0 and c204)
                                add('turret idle %d %d %g %g %d %X %d' % (c28, flag, step, a74, c36, c204, frame), B,
                                    0x824BB0, [t],
                                    [(t + 4, b8(4)), (0x810702, b8(8)), (t + 0x208, w32(c208)), (t + 0x28, h16(c28)),
                                     (t + 0x200, w32(flag)), (t + 0x1F4, f32b(step)), (rec_a + 0x74, f32b(a74)),
                                     (t + 0x36, h16(c36)), (t + 0x204, w32(c204)), (t + 0x1FC, f32b(3.13)),
                                     (FRAME, w32(frame))],
                                    default={0x122BB8: 0x12345678}, full=full)
    # the re-arm's two random sign flips (bits 8 and 4 of the scaled draws)
    for state in (4, 1):
        for rnd in (0x30000000, 0x7FFF0000, 0x10000000):
            add('turret rearm s%d %X' % (state, rnd), B, 0x824BB0, [t],
                [(t + 4, b8(state)), (0x810702, b8(8)), (t + 0x208, w32(0)), (t + 0x36, h16(1)),
                 (t + 0x204, w32(TARGET)), (t + 0x200, w32(0)), (t + 0x28, h16(5)), (FRAME, w32(0))],
                default={0x122BB8: rnd})
    # state 1 track: the target placed around record A's +0xC0
    ra_pos = [struct.unpack_from('<f', ram, rec_a + 0xC0 + 4 * i)[0] for i in range(3)]
    for dx, dy, dz in ((10.0, 0.0, 0.0), (-10.0, 0.0, 0.0), (0.0, 0.0, 10.0), (0.0, 0.0, -10.0), (0.0, 5.0, 10.0),
                       (0.0, -5.0, -10.0), (0.0005, 0.0, 10.0), (3.0, 20.0, 1.0), (0.0, 0.0, 0.0)):
        for a74, b78 in ((0.0, -0.8), (1.2, -1.2), (-1.2, -0.4)):
            for flat, frame in ((0, 1), (1, 0)):
                # flat: record A's +0xB0 / +0xB8 zeroed, so the side term is 0 and the
                # heading comes from the atan of the raw difference
                add('turret track %g %g %g %g %g f%d' % (dx, dy, dz, a74, b78, flat), B, 0x824BB0, [t],
                    [(t + 4, b8(1)), (0x810702, b8(8)), (t + 0x208, w32(0)), (t + 0x204, w32(TARGET)),
                     (TARGET + 0xB0, fw(ra_pos[0] + dx, ra_pos[1] + dy, ra_pos[2] + dz, 1.0)),
                     (rec_a + 0x74, f32b(a74)), (rec_b + 0x78, f32b(b78)), (FRAME, w32(frame))]
                    + ([(rec_a + 0xB0, fw(0.0)), (rec_a + 0xB8, fw(0.0))] if flat else []))
    for c200, hit, kind, obj2, obj0, att, r6c0 in (
            (13, 2, 1, 0, 0, 0x5A, 0), (13, 2, 1, 1, 0, 0x5A, 0), (13, 2, 1, 0, 2, 0x5A, 0), (13, 2, 2, 0, 0, 0x5A, 0),
            (13, 2, 2, 0, 0, 0x5A, 1), (13, 2, 2, 0, 0, 0x5B, 1), (13, 2, 2, 0, 0, 0x5C, 1), (13, 2, 2, 0, 0, 0x10, 1),
            (13, 2, 2, 0, 0, 5, 0), (12, 2, 1, 0, 0, 0, 0), (0, 2, 1, 0, 0, 0, 0), (13, 0, 0, 0, 0, 0, 0),
            (13, 1, 1, 0, 0, 0, 0)):
        for c28, c2a, c36 in ((-5, 10, 0), (3, 0, 1), (0, 1, 0)):
            queues = {0x19AA80: [probe_scribble(1 if hit else 0, kind)],
                      0x19A570: [probe_scribble(1 if hit == 1 else 0, kind)], 0x19B6C0: [r6c0]}
            add('turret period %d %d %d %d %d %X %d %d %d %d' % (c200, hit, kind, obj2, obj0, att, r6c0, c28, c2a,
                                                               c36), B, 0x824BB0, [t],
                [(t + 4, b8(1)), (0x810702, b8(8)), (t + 0x208, w32(0)), (t + 0x204, w32(HITOBJ)),
                 (t + 0x200, w32(c200)), (t + 0x28, h16(c28)), (t + 0x2A, h16(c2a)), (t + 0x36, h16(c36))] + hitrec
                + [(HITOBJ + 2, b8(obj2)), (HITOBJ, b8(obj0)), (HITREC + 0x1A, b8(att))],
                queues=queues, default={0x122BB8: 0x2345678})
    # state 2
    for phase in (-1.5707964, -2.0, 0.0, -1.6, -1.55):
        for c21c in (0, 1, 10):
            for lane in (0, 1):
                add('turret s2 %g %d %d' % (phase, c21c, lane), B, 0x824BB0, [t],
                    [(t + 4, b8(2)), (t + 0x1FC, f32b(phase)), (t + 0x21C, w32(c21c)), (t + 0x224, w32(lane))])
    for state in (3, 0x64, 5):
        add('turret s%X' % state, B, 0x824BB0, [t], [(t + 4, b8(state))])
    # 0x826140 directly: the probe's three outcomes and its two draws
    for state, c200, c204, (h1, k1), (h2, k2, d) in (
            (4, 0, 0, (1, 1), (0, 1, 0.0)), (4, 0, 0, (0, 1), (1, 1, 0.0)), (4, 0, HITOBJ, (1, 2), (1, 1, 0.0)),
            (1, 13, 0, (1, 1), (1, 2, 0.0)), (1, 12, 0, (1, 1), (0, 1, 0.0)), (1, 13, 0, (0, 1), (1, 1, 200.0)),
            (4, 0, 0, (0, 1), (1, 1, 200.0)), (1, 13, HITOBJ, (1, 1), (0, 1, 0.0))):
        for k3 in (0x11, 0x14, 0x0F):
            px = d
            add('probe 826140 s%d %d %X %d%d %d%d %g %X' % (state, c200, c204, h1, k1, h2, k2, d, k3), B, 0x826140,
                [t, rec_b + 0x90],
                [(t + 4, b8(state)), (t + 0x200, w32(c200)), (t + 0x204, w32(c204)), (HITOBJ + 3, b8(k3)),
                 (0x810360, fw(0.0, 0.0, 0.0, 1.0))],
                queues={0x19AA80: [probe_scribble(h1, k1)],
                        0x19A570: [probe_scribble(h2, k2, point=(px, 0.0, 0.0))]},
                default={0x122BB8: 0x7FFF0000})


def exit_cases(add, rng):
    """The AREA19 load's boot rows on the a13_05 image (and the camera and
    player workers on every image)."""
    B = 'a13_05_shaft'
    ram, _ = CAPTURES[B]
    c = CREATURES[0]
    ent = c + 0x1F0
    # 001383C0
    for mode in (0, 1, 2, 3, 4, 5):
        for state in ((0, 1, 2, 3, 4) if mode in (0, 4, 5) else (0, 1)):
            for r in ((0, 1) if mode == 1 else (1,)):
                for c85, c34, c87, area, sub in (((0, 0, 0, 0x13, 0), (3, 2, 1, 0x13, 0), (1, 1, 1, 0x13, 1),
                                                  (0x80, 0x8000, 1, 0x12, 0)) if mode == 0 and state == 2 else
                                                 ((2, 2, 1, 0x13, 1),)):
                    add('exit 1383C0 m%d s%d r%d %X %X %d %X %d' % (mode, state, r, c85, c34, c87, area, sub), B,
                        0x1383C0, [c],
                        [(B8D, b8(mode)), (c + 4, b8(state)), (ent + 0x85, b8(c85)), (ent + 0x34, h16(c34)),
                         (ent + 0x87, b8(c87)), (0x810700, bytes([area, sub]))],
                        queues={0x1B2140: [r], 0x1B10B0: [1]})
    # 00138540
    for e808 in (0, 0xFF):
        for d in (0, 0x80, 0x7F):
            for e70a in (0, 1):
                for r in (0, 1):
                    add('exit 138540 %X %X %d %d' % (e808, d, e70a, r), B, 0x138540, [c, ent],
                        [(0x810808, b8(e808)), (c + 0xD, b8(d)), (0x81070A, b8(e70a)), (0x70003B8A, h16(0x7FF0))],
                        queues={0x1B10B0: [r], 0x122BB8: [0x7FFFF800 if r == 0 else -0x800]})
    # 001386E0
    for r in (0, 1):
        for sub in ((0, 1, 2, 3, 4, 5, 6) if r else (0,)):
            add('exit 1386E0 r%d sub%d' % (r, sub), B, 0x1386E0, [c, ent], [(c + 5, b8(sub))],
                queues={0x1B2140: [r]})
    for c86, c32, c2e, c84, c36, c34, c83, c70, rnd in (
            (0, 0, 0, 0, 0, 0, 0, 0, 0), (1, 1, 1, 1, 1, 1, 0, 8, 0x7FFFFFFF), (0x80, 0xFFFF, 2, 0, 1, 0, 1, 8, -1),
            (2, 0, 0, 0x80, 1, 0, 0, 8, 0x12345678), (0, 0, 0, 0, 0x8000, 0, 0, 7, 0), (0, 0, 0, 0, 1, 0, 0, 8, -0x40000)):
        add('exit 1386E0 tail %X %X %X %X %X %X %X %X %X' % (c86, c32, c2e, c84, c36, c34, c83, c70, rnd), B,
            0x1386E0, [c, ent],
            [(c + 5, b8(6)), (ent + 0x86, b8(c86)), (ent + 0x32, h16(c32)), (ent + 0x2E, h16(c2e)),
             (ent + 0x84, b8(c84)), (c + 0x36, h16(c36)), (ent + 0x34, h16(c34)), (ent + 0x83, b8(c83)),
             (ent + 0x70, w32(c70))],
            queues={0x1B2140: [1], 0x122BB8: [rnd], 0x1C64F0: [0x55]})
    # the field: 00154460 / 001546C0 / 00154740 / 001549C0 / 0015A200
    f = FIELDS[0]
    scr = f + 0x1F0
    fx_, fy_, fz_ = (struct.unpack_from('<f', ram, f + 0xB0 + 4 * i)[0] for i in range(3))
    row = ram[f + 0xD] * 20
    wx = struct.unpack_from('<f', ram, 0x248120 + row)[0]
    wy = struct.unpack_from('<f', ram, 0x248124 + row)[0]
    wz = struct.unpack_from('<f', ram, 0x248128 + row)[0]
    for mode in (0, 1):
        for ox, oy, oz in ((0.0, 0.0, 0.0), (3 * wx + 1, 0.0, 0.0), (0.0, 0.0, 3 * wz + 1), (0.0, 3 + wy + 1, 0.0),
                           (3 * wx, 3 + wy, 3 * wz), (-3 * wx, -(3 + wy), -3 * wz)):
            add('exit 154460 m%d %g %g %g' % (mode, ox, oy, oz), B, 0x154460, [f],
                [(B8D, b8(mode)), (PLAYER + 0xA0, fw(fx_ + ox, fy_ + oy, fz_ + oz))])
    for state in (0, 1, 2, 3, 4):
        add('exit 1546C0 s%d' % state, B, 0x1546C0, [f], [(f + 4, b8(state)), (f + 5, b8(4))],
            queues={0x1C6150: [5], 0x1AF780: [FREE + 0x100 * i for i in range(12)]})
    for count, limit in ((5, 20), (12, 12), (13, 12), (0, 3)):
        for area, sub in ((0x13, 0), (0x13, 9), (0xD, 0)):
            add('exit 154740 %d %d %X %d' % (count, limit, area, sub), B, 0x154740, [f, scr],
                [(0x275BCC, h16(limit)), (0x810700, bytes([area, sub]))],
                queues={0x1C6150: [count], 0x1AF780: [FREE + 0x100 * i for i in range(20)]})
    near = [(PLAYER + 0xA0, fw(fx_, fy_, fz_)), (B8D, b8(0))]
    for wide in (0, 1):
        for sub in (0, 1, 2, 3, 4, 5):
            for parent in (0, PADS19[0]):
                for hits in ((0, 1) if sub == 0 else (0,)):
                    add('exit 1549C0 sub%d w%d p%X h%d' % (sub, wide, parent, hits), B, 0x1549C0, [f, scr],
                        near + [(f + 5, b8(sub)), (f + 0x2E, h16(wide)), (f + 0x20, w32(parent)), (f + 0x28, h16(1))],
                        queues={0x1545B0: [hits, 0, hits]},
                        default={0x122BB8: 0x5A5A5A5A})
    for sub in (1, 3):
        for c28 in (2, 1, 0, -3):
            for v in (0, 0x100, 0x12C, 0x12D, 0x24, 0x25, -5):
                add('exit 1549C0 step%d %d %d' % (sub, c28, v), B, 0x1549C0, [f, scr],
                    [(f + 5, b8(sub)), (f + 0x28, h16(c28))] + [(scr + 0x80 + 10 * i, h16(v)) for i in range(12)])
    for mode in (0, 1):
        for dy in (0.0, 3 + wy + 0.5):
            for dx in (0.0, 1.9, 2.1, 3.9, 4.1):
                for wide in (0, 1):
                    add('exit 1549C0 hold m%d %g %g w%d' % (mode, dy, dx, wide), B, 0x1549C0, [f, scr],
                        [(f + 5, b8(2)), (B8D, b8(mode)), (f + 0x2E, h16(wide)),
                         (PLAYER + 0xA0, fw(fx_ + dx, fy_ + dy, fz_)), (scr + 0x10, fw(fx_, fy_, fz_))])
    for parent in PADS19[:1]:
        for kind in (0xD, 0xE):
            for node in (0, FREE + 0x1000):
                add('exit 15A200 %X %X' % (kind, node), B, 0x15A200, [parent, kind, 3], queues={0x1AFA90: [node]})
    # 00183440 / 001838B0 (the player)
    for beat in ('a13_04_hatch', B):
        for bit in (0, 0x1000):
            for t268 in (0.0, 2.9, 3.0, 2.99999, 5.0):
                add('exit 183440 %s %X %g' % (beat, bit, t268), beat, 0x183440, [PLAYER],
                    [(PLAYER + 0x200, w32(bit)), (PLAYER + 0x268, f32b(t268))])
        for state in (0, 1, 4):
            add('exit 1838B0 %s s%d' % (beat, state), beat, 0x1838B0, [PLAYER], [(PLAYER + 4, b8(state))])
    # 00196970: each region's centre, inside / outside, high / low
    regions = ((710.0, 1266.1, 268.1, 284.5), (907.0, 960.2, 306.1, 314.0), (925.1, 859.7, 308.0, 314.0),
               (1077.2, 845.0, 242.5, 252.0), (855.1, 851.3, 309.0, 314.0))
    for flag in (0, 1):
        for cx, cz, ceil, thr in regions:
            for off in (0.0, 7.99, 8.0):
                for y, p14 in ((thr, ceil + 5), (thr - 1, ceil - 5)):
                    if flag and (off or y != thr):
                        continue
                    add('exit 196970 %d %g %g %g %g' % (flag, cx, off, y, p14), B, 0x196970, [CAM, PLAYER],
                        [(0x8106B8, b8(flag)), (PLAYER + 0xA0, fw(cx + off, y, cz)), (CAM + 0x14, f32b(p14))])
    # 00196CE0 (the camera)
    for state in (0, 1, 2, 3, 4, 5):
        for code in (0x2D, 7, 6, 8, 9, 0x2C, 0, 0x30):
            for px, py, pz, region in ((848.6, 260.0, 882.1, 0), (848.6, 300.0, 882.1, 3), (915.0, 240.0, 939.1, 4),
                                       (915.0, 270.0, 939.1, 1), (700.0, 250.0, 700.0, 5), (700.0, 250.0, 700.0, 2)):
                if state not in (0, 1, 2) and (region or px != 848.6 or code not in (0x2D, 0)):
                    continue
                add('exit 196CE0 s%d c%X %g %g %d' % (state, code, px, py, region), B, 0x196CE0, [CAM, PLAYER],
                    [(CAM + 1, b8(state)), (PLAYER + 0x230, w32(code)), (PLAYER + 0xA0, fw(px, py, pz)),
                     (CAM + 2, b8(region)), (PLAYER + 0xB4, f32b(py)), (0x8106B8, b8(1))])
    # state 0 with the classifier live: regions 3 / 4 take their point, region
    # 5 outside the second window goes to state 2
    for px, pz in ((1077.2, 845.0), (855.1, 851.3), (700.0, 700.0), (907.0, 960.2)):
        for code in (0, 0x2D):
            add('exit 196CE0 s0 live %g %g %X' % (px, pz, code), B, 0x196CE0, [CAM, PLAYER],
                [(CAM + 1, b8(0)), (PLAYER + 0x230, w32(code)), (PLAYER + 0xA0, fw(px, 200.0, pz)),
                 (0x8106B8, b8(0))])
    for idx in range(6):
        for y in (100.0, 400.0):
            for state in (1, 2):
                add('exit 196CE0 idle s%d r%d %g' % (state, idx, y), B, 0x196CE0, [CAM, PLAYER],
                    [(CAM + 1, b8(state)), (PLAYER + 0x230, w32(0x2C)), (CAM + 2, b8(idx)),
                     (PLAYER + 0xB4, f32b(y)), (0x8106B8, b8(1))])


def survivor_cases(add):
    """Cases added because the mutation sweep (docs/LEVEL9_PORT.md section
    3.5) showed the default set missed them; they always run."""
    B, B5, B1 = 'a13_00_door8', 'a13_05_shaft', 'a13_01_door14'
    # 0x823700 passes +0x0D (zero-extended) to 001B10B0 / 001BA8E0
    add('survivor 823700 d90', B, 0x823700, [R3], [(R3 + 4, b8(0)), (R3 + 0xD, b8(0x90))])
    # 001386E0's sound index (00122BB8() >> 17) % 5 with draws that tell the shift apart
    c = CREATURES[0]
    for rnd in (0x7FFFFFFF, 0x12340000, -0x40000):
        add('survivor 1386E0 rnd %X' % (rnd & MASK), B5, 0x1386E0, [c, c + 0x1F0],
            [(c + 5, b8(6)), (c + 0x1F0 + 0x70, w32(8))], queues={0x1B2140: [1], 0x122BB8: [rnd]})
    # 00138540: bit 7 of +0x0D alone decides (D_00810808 clear, the other bits set)
    for d in (0x7F, 0x84):
        add('survivor 138540 d%X' % d, B5, 0x138540, [c, c + 0x1F0], [(0x810808, b8(0)), (c + 0xD, b8(d))],
            queues={0x1B10B0: [0]})
    # 0x8292A0 state 0x64: only bit 5 of D_008107F4 counts
    add('survivor 8292A0 f1F', B, 0x8292A0, [R49[0]], [(R49[0] + 4, b8(0x64)), (0x8107F4, b8(0x1F)),
                                                       (R49[0] + 0x28, h16(0))])
    add('survivor 8292A0 s0 f1F', B, 0x8292A0, [R49[0]], [(R49[0] + 4, b8(0)), (0x8107F4, b8(0x1F))])
    # 0x826850: z exactly 1000 is the south hatch's side (not > 1000)
    for state, sub, r in ((0, 0, 0), (1, 0, 0), (1, 1, 1), (1, 1, 0)):
        add('survivor 826850 z1000 s%d sub%d r%d' % (state, sub, r), B, 0x826850, [HATCH63],
            [(HATCH63 + 4, b8(state)), (HATCH63 + 5, b8(sub)), (HATCH63 + 0xB, b8(4)), (HATCH63 + 0xB8, f32b(1000.0)),
             (HATCH63 + 0x28, h16(3)), (0x810839, b8(1))], queues={0x1BA1F0: [r]})
    # 001C06E0: +0x2A landing exactly on 0x1000 (attack step 5, dying)
    e = PULSE
    add('survivor 1C06E0 attack5 102A', B5, 0x1C06E0, [e],
        [(e + 4, b8(1)), (e + 5, b8(3)), (e + 6, b8(5)), (e + 0x2A, h16(0x102A))], queues={0x1B2140: [1], 0x1BE5F0: [1]})
    add('survivor 1C06E0 dying 1040', B5, 0x1C06E0, [e], [(e + 4, b8(2)), (e + 5, b8(2)), (e + 0x2A, h16(0x1040))])
    # 00196CE0 state 2: the player exactly at the region's height
    ram, _ = CAPTURES[B5]
    for idx in (0, 2):
        y = struct.unpack_from('<f', ram, 0x24A6BC + 16 * idx)[0]
        add('survivor 196CE0 s2 y=table r%d' % idx, B5, 0x196CE0, [CAM, PLAYER],
            [(CAM + 1, b8(2)), (PLAYER + 0x230, w32(0)), (CAM + 2, b8(idx)), (PLAYER + 0xB4, f32b(y)),
             (0x8106B8, b8(1))])
    # 0x824BB0: record A's yaw exactly at the limit (no clamp, no sign flip)
    t = WATCHERS[0]
    ram1, _ = CAPTURES[B1]
    rec_a = u32(ram1, t + 0x118)
    bones = [(0x275B40, w32(t + 0x110))]
    for state in (4, 1):
        add('survivor 824BB0 sweep at limit s%d' % state, B1, 0x824BB0, [t], bones +
            [(t + 4, b8(state)), (0x810702, b8(8)), (t + 0x208, w32(0x40)), (t + 0x20C, w32(5)), (t + 0x214, w32(5)),
             (rec_a + 0x74, bits32(0x3F91361E)), (t + 0x210, f32b(0.0))])
    add('survivor 824BB0 track at limit', B1, 0x824BB0, [t], bones +
        [(t + 4, b8(1)), (0x810702, b8(8)), (t + 0x208, w32(0)), (t + 0x204, w32(TARGET)),
         (TARGET + 0xB0, fw(0.0, 0.0, 0.0, 1.0)), (rec_a + 0xC0, fw(0.0, 0.0, 0.0, 1.0)),
         (rec_a + 0xB0, fw(0.0)), (rec_a + 0xB8, fw(0.0)), (rec_a + 0x74, bits32(0x3F91361E))])
    add('survivor 824BB0 idle at limit', B1, 0x824BB0, [t], bones +
        [(t + 4, b8(4)), (0x810702, b8(8)), (t + 0x208, w32(0)), (t + 0x28, h16(5)), (t + 0x200, w32(1)),
         (t + 0x1F4, f32b(0.0)), (rec_a + 0x74, bits32(0x3F91361E))])
    # --- the review's named survivors (fix round): each case below tells the
    # original's comparison or operand apart from the named mutant ---
    # 0x824BB0 sweep: record A's yaw exactly on the LOWER limit (-1.134464).
    # The original's test is strict, so +0x210 keeps +0.0 (a <= test would
    # flip it to -0.0).
    for state in (4, 1):
        add('survivor 824BB0 sweep at -limit s%d' % state, B1, 0x824BB0, [t], bones +
            [(t + 4, b8(state)), (0x810702, b8(8)), (t + 0x208, w32(0x40)), (t + 0x20C, w32(5)), (t + 0x214, w32(5)),
             (rec_a + 0x74, bits32(0xBF91361E)), (t + 0x210, f32b(0.0))])
    # 0x826140: hit-object kinds 0x10 and 0x13, the two ends of the 0x10..0x13
    # range the original counts as hit 1
    rec_b = u32(ram1, t + 0x110 + 0xC)
    for k3 in (0x10, 0x13):
        add('survivor 826140 kind %X' % k3, B1, 0x826140, [t, rec_b + 0x90], bones +
            [(t + 4, b8(1)), (t + 0x200, w32(13)), (t + 0x204, w32(0)), (HITOBJ + 3, b8(k3)),
             (0x810360, fw(0.0, 0.0, 0.0, 1.0))],
            queues={0x19AA80: [probe_scribble(1, 1)], 0x19A570: [probe_scribble(1, 1, point=(0.0, 0.0, 0.0))]},
            default={0x122BB8: 0x7FFF0000})
    # 001549C0 state 2: the player 3.0 above / below the field, inside the
    # vertical limit 3 + h (h = D_00248124[row], 1.0 for FIELDS[0]) but
    # outside 3 - h: the original keeps waiting
    ram5, _ = CAPTURES[B5]
    f = FIELDS[0]
    scr = f + 0x1F0
    fx_, fy_, fz_ = (struct.unpack_from('<f', ram5, f + 0xB0 + 4 * i)[0] for i in range(3))
    for dy in (3.0, -3.0):
        add('survivor 1549C0 hold dy %g' % dy, B5, 0x1549C0, [f, scr],
            [(f + 5, b8(2)), (B8D, b8(0)), (f + 0x2E, h16(0)),
             (PLAYER + 0xA0, fw(fx_, fy_ + dy, fz_)), (scr + 0x10, fw(fx_, fy_, fz_))])
    # 001E7050 state 1: slot phases 0x3F99999A + 0.2 land exactly on 1.4
    # (0x3FB33333) under the EE model; the original's c.le.s does not wrap them
    em = EMITTER
    src = u32(ram1, em + 0x24)
    add('survivor 1E7050 phase to 1.4', B1, 0x1E7050, [em],
        [(em + 4, b8(1)), (src + 4, b8(1)), (src + 0xC, b8(4))]
        + [(em + 0x1F0 + 4 * j, w32(j & 3)) for j in range(8)]
        + [(em + 0x1F0 + 0x40 + 4 * j, bits32(0x3F99999A)) for j in range(8)],
        queues={0x1CD070: [5], 0x1CD2B0: [fbits(2.0)]}, default={0x122BB8: 0x13579BDF})


def anim_cases(add, rng):
    """001BA7F0, the lift door's wobble pair, and the AREA19 actors
    001C06E0 / 001C1030 / 001C4BA0 (a13_05)."""
    B13 = 'a13_01_door14'
    for r in (0, 1):
        for node in (R3, DOOR14):
            add('anim 1BA7F0 @%X r%d' % (node, r), B13, 0x1BA7F0, [node],
                queues={0x19A570: [probe_scribble(r, 1, point=(5.0, 6.0, 7.0))]},
                spad=[(0x700038A0, fw(1.0, 2.0, 3.0, 4.0))])
    blk = DOOR14 + 0x1F0
    bones = [(0x275B40, w32(DOOR14 + 0x110))]   # the door's bone array, as the frame loop sets it
    for v in (0.0, 0.1, 0.2, 2.3, 2.5, 2.6, 24.8, 24.9, 25.0, 25.1, 40.0, -1.0):
        for entry in (0x1BDCA0, 0x1BDD70):
            add('anim %06X %g' % (entry, v), B13, entry, [blk], bones + [(blk + 0x10, f32b(v))])
    for entry in (0x1BDCA0, 0x1BDD70):
        add('anim %06X up 2.3' % entry, B13, entry, [blk], bones + [(blk + 0x10, bits32(up(2.3)))])
        add('anim %06X 0.2 exact' % entry, B13, entry, [blk], bones + [(blk + 0x10, bits32(0x3E4CCCCD))])
    B = 'a13_05_shaft'
    ram, _ = CAPTURES[B]
    e = PULSE
    tail = e + 0x1F0
    sub118 = u32(ram, e + 0x118)
    # 001C06E0 states
    for state in (0, 2, 3, 4):
        for sub in ((0, 1, 2, 3) if state == 2 else (0,)):
            for r in ((0, 1) if state == 2 and sub == 1 else (0,)):
                for a70 in ((0.0, 0.86, 0.87266463, 1.0) if state == 2 and sub == 0 else (0.0,)):
                    for c2a in ((0x1000, 0x1040, 0x1041, 0x2000) if state == 2 and sub == 2 else (0x1000,)):
                        add('anim 1C06E0 s%d sub%d r%d %g %X' % (state, sub, r, a70, c2a), B, 0x1C06E0, [e],
                            [(e + 4, b8(state)), (e + 5, b8(sub)), (u32(ram, e + 0x11C) + 0x70, f32b(a70)),
                             (e + 0x2A, h16(c2a))],
                            queues={0x1EFE00: [r]})
    for r2140 in (0, 1):
        for sub in (0, 1, 2, 3, 4):
            for r in (0, 1):
                for c28 in ((0, 0x3B, 0x3C, 0x3D) if sub in (0, 1) else (0,)):
                    for frame in ((0, 0x40, 0x80, 1) if sub in (0, 1) else (0,)):
                        add('anim 1C06E0 alive %d sub%d r%d %X %X' % (r2140, sub, r, c28, frame), B, 0x1C06E0, [e],
                            [(e + 4, b8(1)), (e + 5, b8(sub)), (e + 0x28, h16(c28)), (FRAME, w32(frame))],
                            queues={0x1B2140: [r2140], 0x1BE5F0: [r]}, default={0x122BB8: 0x40000000})
    for want, cur, r in ((0.5, 0.5, 0), (0.5, 0.5, 1), (0.5, 0.4, 0), (0.0, 0.0, 1)):
        add('anim 1C06E0 aim %g %g %d' % (want, cur, r), B, 0x1C06E0, [e],
            [(e + 4, b8(1)), (e + 5, b8(2)), (tail + 4, f32b(want)), (sub118 + 0x74, f32b(cur))],
            queues={0x1B2140: [1], 0x19A570: [r]})
    for step in range(7):
        for c2a, c28, r in ((0x1000, 1, 0), (0x1FE0, 2, 1), (0x1FF0, 0, 0), (0x1300, 1, 1), (0x1200, 3, 0),
                            (0x102A, 0, 1), (0x1029, 0, 0), (0x1029, 0, 1)):
            add('anim 1C06E0 attack %d %X %d %d' % (step, c2a, c28, r), B, 0x1C06E0, [e],
                [(e + 4, b8(1)), (e + 5, b8(3)), (e + 6, b8(step)), (e + 0x2A, h16(c2a)), (e + 0x28, h16(c28))],
                queues={0x1B2140: [1], 0x1BE5F0: [r]}, default={0x122BB8: 0x7FFFFFFF})
    for hit in (0, 0x8005, 0x5003, 0x4010, 0x1004, 0x0050, 0x4FFF):
        for c1c in (0, 1, 5):
            for pool in (0x50, 0x10, 0x5):
                add('anim 1C06E0 hit %X %d %X' % (hit, c1c, pool), B, 0x1C06E0, [e],
                    [(e + 4, b8(1)), (e + 5, b8(5)), (e + 0x36, h16(hit)), (tail + 0x1C, w32(c1c)),
                     (e + 0x34, h16(pool))], queues={0x1B2140: [1], 0x1EFE00: [1]})
    # 001C1030
    k = KNOCK
    for state in (0, 1, 2, 3, 4):
        for r in (0, 1):
            for r1, r2 in (((0, 0), (1, 0), (0, 1), (1, 1)) if state == 0 else ((0, 0),)):
                add('anim 1C1030 s%d r%d %d%d' % (state, r, r1, r2), B, 0x1C1030, [k], [(k + 4, b8(state))],
                    queues={0x1B10B0: [r], 0x19A570: [probe_scribble(r1, 1, point=(30.0, 40.0, 50.0)),
                                                       probe_scribble(r2, 1, point=(31.0, 41.0, 51.0))],
                            0x1C6160: [7]}, run_set=RUN - {0x1C6160})
    for sub in (0, 1, 2, 3):
        for v36, c34 in ((0, 5), (0x1003, 5), (0x1005, 5), (0x1006, 5), (0x0003, 5), (-0x1000 + 3, 5), (0x1FFF, 0x1000)):
            for c28 in ((1, 2, 0) if sub == 2 else (1,)):
                add('anim 1C1030 s1 sub%d %X %X %d' % (sub, v36 & 0xFFFF, c34, c28), B, 0x1C1030, [k],
                    [(k + 4, b8(1)), (k + 5, b8(sub)), (k + 0x36, h16(v36)), (k + 0x34, h16(c34)),
                     (k + 0x28, h16(c28))])
    for sub in (0, 1, 2):
        for flag in (0, 0x1000, 0x0FFF):
            add('anim 1C1030 s2 sub%d %X' % (sub, flag), B, 0x1C1030, [k],
                [(k + 4, b8(2)), (k + 5, b8(sub)), (k + 0x1F0, h16(flag))])
    # 001C4BA0
    p = PART
    for state in (0, 1, 2, 3, 4):
        for r in (0, 1):
            for kind in ((4, 3) if state == 1 else (4,)):
                add('anim 1C4BA0 s%d r%d k%d' % (state, r, kind), B, 0x1C4BA0, [p],
                    [(p + 4, b8(state)), (p + 3, b8(kind))], queues={0x1B0FD0: [r]})


def fx_cases(add, rng):
    """001DE920, 001E5AC0 and 001E7050 (a13_01: the outdoor load)."""
    B = 'a13_01_door14'
    for spread in (0, 0x7F, 0xFF, 0x40):
        for rnd in (0x12345678, 0x7FFFFFFF, 0, -0x10000):
            add('fx 1DE920 %X %X' % (spread, rnd & MASK), B, 0x1DE920, [], [(0x8106BF, b8(spread))],
                default={0x122BB8: rnd}, full=rnd not in (0x12345678,))
    for flags in (0, 2, 3):
        for seed in (0x12345678, -1, 0):
            for dt in (1.0, 0.5, 3.0):
                for timer in (0.0, 75.0, 76.0, 200.0):
                    for pos in ((0.0, 0.0), (1.9, 0.5), (0.9, 2.1)):
                        full = not (seed == 0x12345678 and dt == 1.0 and pos == (0.0, 0.0)) and timer != 76.0
                        add('fx 1E5AC0 %d %X %g %g %s' % (flags, seed & MASK, dt, timer, pos), B, 0x1E5AC0,
                            [WEATHER, flags, seed, fbits(dt)],
                            [(WEATHER + 0x1F0 + 0x30, f32b(timer))]
                            + [(WEATHER + 0x1F0 + 4 * i, f32b(pos[i % 2])) for i in range(6)], full=full)
    for cam in ((-250.5, 12.25, 99999.0), (0.0, 0.0, 0.0), (1234.75, -5.5, 160.25)):
        add('fx 1E5AC0 cam %s' % (cam,), B, 0x1E5AC0, [WEATHER, 0, 7, fbits(1.0)],
            [(0x8105D0, fw(*cam)), (WEATHER + 0x1F0 + 0x30, f32b(100.0))])
    em = EMITTER
    ram, _ = CAPTURES[B]
    src = u32(ram, em + 0x24)
    for state in (0, 1, 2, 3, 4):
        for s4 in ((3, 1) if state == 1 else (1,)):
            for h in ((0xFFFFFF, 5) if state == 1 else (5,)):
                for n in ((0.0, 2.0) if state == 1 else (2.0,)):
                    for ph in ((0.0, 1.2, 1.25, 1.3) if state == 1 else (0.0,)):
                        add('fx 1E7050 s%d %d %X %g %g' % (state, s4, h, n, ph), B, 0x1E7050, [em],
                            [(em + 4, b8(state)), (src + 4, b8(s4)), (src + 0xC, b8(4))]
                            + [(em + 0x1F0 + 4 * j, w32(j & 3)) for j in range(8)]
                            + [(em + 0x1F0 + 0x40 + 4 * j, f32b(ph)) for j in range(8)],
                            queues={0x1CD070: [h], 0x1CD2B0: [fbits(n)]}, default={0x122BB8: 0x13579BDF})


def u32f(ram, address):
    return struct.unpack_from('<f', ram, address)[0]


def reachable_words(ram, entry, size):
    """Non-branch words of [entry, entry + size) that control flow from the
    entry can reach (branch words themselves never reach execute). The
    words left out are the dead copies the compiler places after an
    unconditional branch's delay slot."""
    end, todo, seen, words = entry + size, [entry], set(), set()

    def delay(pc):  # a delay slot runs, but its fall-through is the branch's
        if entry <= pc < end:
            words.add(pc)
    while todo:
        pc = todo.pop()
        if pc in seen or not entry <= pc < end:
            continue
        seen.add(pc)
        word = struct.unpack_from('<I', ram, pc)[0]
        op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
        target = pc + 4 + (((word & 0xFFFF) ^ 0x8000) - 0x8000) * 4
        if op in (4, 5, 6, 7, 20, 21, 22, 23, 1) or (op == 17 and rs == 8):
            delay(pc + 4)
            always = op == 4 and rs == 0 and rt == 0
            todo += [target] + ([] if always else [pc + 8])
        elif op == 2:
            delay(pc + 4)
            todo.append((word & 0x3FFFFFF) << 2)
        elif op == 3 or (op == 0 and word & 63 == 9):
            delay(pc + 4)
            todo.append(pc + 8)
        elif op == 0 and word & 63 == 8:
            delay(pc + 4)
            if rs != 31:
                table, count = JUMP_TABLES[pc]
                todo += [u32(ram, table + 4 * i) for i in range(count)]
        else:
            words.add(pc)
            todo.append(pc + 4)
    return words



DEFAULT_ARGS = {entry: [STACK_TOP if k == S else (fb(1.0) if k == F else FREE) for k in kinds]
                for entry, (_, _, kinds, _) in FUNCS.items()}

# Words the static walk reaches that no input can reach, and why: the
# unsigned-int-to-float fix-up after a bltz on a byte that lbu loaded (the
# value is 0..255, never negative) in 00154740 (the four tint bytes),
# 001DE920 (D_008106BF) and 0x829AA0 (+5).
DEAD_WORDS = {}
# 0x824BB0's elevation step stores the target angle itself only when it is
# both <= B+0x78 - k and >= k + B+0x78 (k = 0.011635528): with the EE's
# truncating add / sub, cur - k < cur + k for every finite cur, and the
# angle is an atan result: never both.
DEAD_WORDS[0x825A0C] = 'elevation exact-store branch'
for _lo, _hi in ((0x154858, 0x154870), (0x154890, 0x1548A8), (0x1548C8, 0x1548E0), (0x154900, 0x154918),
                 (0x1DEA08, 0x1DEA20), (0x829B14, 0x829B2C)):
    for _pc in range(_lo, _hi, 4):
        DEAD_WORDS[_pc] = 'unsigned fix-up of a byte'





def fault_checks():
    """The native fail-stop contract: a NULL hook, a failing hook, an
    unmapped address after a call and before any call, a latched fault; and
    for every entry a fault latched on entry, a NULL hook table, a NULL
    fault pointer and (entries with a result) a NULL result pointer."""
    ram, spad = CAPTURES['a13_00_door8']
    node = R3
    ram = bytearray(ram)
    ram[node + 5] = 0
    ram[node + 0x0B] |= 4
    problems = []
    calls_want = ['w_001BA1A0', 'w_001BA580', 'w_001C64F0', 'w_001B17A0', 'w_001C68C0', 'w_callback']

    def attempt(null=None, failing=None, unmapped=None, latched=False, entry=0x823940, args=(node,)):
        ram_c = (C.c_uint8 * RAM_SIZE).from_buffer_copy(ram)
        spad_c = (C.c_uint8 * SPAD_SIZE).from_buffer_copy(spad)
        base, sbase, calls = C.addressof(ram_c), C.addressof(spad_c), []

        def mem(_, address, size):
            if address == unmapped:
                return None
            if 0x70000000 <= address and address + size <= 0x70000000 + SPAD_SIZE:
                return sbase + address - 0x70000000
            return base + address if address + size <= RAM_SIZE else None
        keep = [BYTES_FN(mem)]
        fields = {'ctx': None, 'bytes': keep[0]}
        for name, _, hargs, result in HOOKS:
            def fn(_ctx, *values, name=name, result=result):
                calls.append(name)
                if result:
                    values[-1][0] = 1
                return -1 if name == failing else 0
            fields[name] = hook_proto(hargs, result)(fn) if name != null else hook_proto(hargs, result)()
            keep.append(fields[name])
        fields['w_callback'] = CALLBACK_FN(lambda _c, fn, actor: calls.append('w_callback') or 0)
        keep.append(fields['w_callback'])
        hooks = Hooks(**fields)
        fault = Fault(0x1234, 7) if latched else Fault()
        before = C.string_at(base, RAM_SIZE) + C.string_at(sbase, SPAD_SIZE)
        call = [C.byref(hooks)] + native_args(entry, args)
        result = C.c_int32(0)
        if FUNCS[entry][3] is not None:
            call.append(C.byref(result))
        status = getattr(NATIVE, FUNCS[entry][0])(*call, C.byref(fault))
        same = before == C.string_at(base, RAM_SIZE) + C.string_at(sbase, SPAD_SIZE)
        return status, fault.address, fault.code, calls, same

    status, address, code, calls, _ = attempt()
    if (status, address, code) != (0, 0, 0) or calls != calls_want:
        problems.append(('fault-check baseline', status, hex(address), code, calls))
    status, address, code, calls, _ = attempt(null='w_001C64F0')
    if (status, address, code) != (-1, 0x1C64F0, 1) or calls != calls_want[:2]:
        problems.append(('NULL hook', status, hex(address), code, calls))
    status, address, code, calls, same = attempt(failing='w_001BA1A0')
    if (status, address, code) != (-1, 0x1BA1A0, 2) or calls != calls_want[:1] or not same:
        problems.append(('failing hook', status, hex(address), code, calls, same))
    status, address, code, calls, _ = attempt(unmapped=node + 0x0D)
    if (status, address, code) != (-1, node + 0x0D, 5) or calls != calls_want[:1]:
        problems.append(('unmapped address', status, hex(address), code, calls))
    blk = DOOR14 + 0x1F0
    status, address, code, calls, same = attempt(unmapped=blk + 0x10, entry=0x1BDCA0, args=(blk,))
    if (status, address, code) != (-1, blk + 0x10, 5) or calls or not same:
        problems.append(('unmapped address before any call', status, hex(address), code, calls, same))
    status, address, code, calls, same = attempt(latched=True)
    if (status, address, code) != (-1, 0x1234, 7) or calls or not same:
        problems.append(('latched fault', status, hex(address), code, calls, same))
    touched = []

    def refuse(*_):
        touched.append('called')
        return -1
    bytes_fn = BYTES_FN(refuse)
    fields = {'ctx': None, 'bytes': bytes_fn}
    keep = [bytes_fn]
    for name, _, hargs, result in HOOKS:
        fields[name] = hook_proto(hargs, result)(refuse)
        keep.append(fields[name])
    fields['w_callback'] = CALLBACK_FN(refuse)
    keep.append(fields['w_callback'])
    hooks = Hooks(**fields)
    for entry, (symbol, _, kinds, kind) in sorted(FUNCS.items()):
        fn = getattr(NATIVE, symbol)
        for table, fault, with_result in ((C.byref(hooks), Fault(0x1234, 7), True), (None, Fault(), True),
                                          (C.byref(hooks), None, True), (C.byref(hooks), Fault(), False)):
            if not with_result and kind is None:
                continue
            result = C.c_int32(0x5A5A5A5A)
            del touched[:]
            where = C.byref(fault) if fault is not None else None
            call = [table] + native_args(entry, DEFAULT_ARGS[entry])
            if kind is not None:
                call.append(C.byref(result) if with_result else None)
            status = fn(*call, where)
            latched = fault is None or (fault.address, fault.code) == (
                (0x1234, 7) if table is not None and with_result else (0, 0))
            if status != -1 or touched or result.value != 0x5A5A5A5A or not latched:
                problems.append(('entry %06X refused' % entry, status, touched[:2], hex(result.value & MASK)))
    return problems


import types  # noqa: E402


def leftover_checks():
    """The path where the original uses a value its own code leaves
    undefined: 001E7050 dividing by its source's bone count +0x0C when that
    is 0 (the EE's div then leaves HI as it was; the original stores it as
    the slot's bone index). The translation faults 7
    (EM_LEVEL9_PORT_FAULT_REGISTER) at 0x1E7050 right after the load of
    the count: the calls and accesses before it are the original's, nothing
    after. Returns (problems, the original pcs these runs executed)."""
    problems, pcs = [], set()
    beat = 'a13_01_door14'
    ram0, spad0 = CAPTURES[beat]
    em = EMITTER
    src = u32(ram0, em + 0x24)
    for state, extra in ((0, []), (1, [(em + 0x1F0 + 0x40 + 4 * j, f32b(1.3)) for j in range(8)])):
        patches = [(em + 4, b8(state)), (src + 0xC, b8(0)), (src + 4, b8(1))] + extra
        ram, spad = patched(ram0, patches), patched(spad0, [])
        seen = set()
        script = Script({0x1CD070: [5], 0x1CD2B0: [fbits(2.0)]}, {0x122BB8: 0x13579BDF})
        try:
            oracle = Oracle(ram, spad, 0x1E7050, ([em, STACK_TOP], {u32(ram, em + 0x4C)}), script, RUN, seen)
            pcs |= seen
        except AssertionError:
            oracle = None
        if oracle is None:
            problems.append(('1E7050 leftover: the original did not run', state))
            continue
        names = [e[0] for e in oracle.log]
        k = names.index('w_00122BB8')     # the first draw: the remainder that follows is the undefined one
        replay, status, fault, _ = native_call(NATIVE, ram, spad, 0x1E7050, [em, STACK_TOP], oracle)
        replay.settle()
        tail = replay.access[k + 1] if len(replay.access) > k + 1 else None
        ok = ((status, fault.address, fault.code) == (-1, 0x1E7050, 7) and replay.i == k + 1
              and not replay.errors and tail == [[src + 0xC, 1, False]])
        if not ok:
            problems.append(('1E7050 leftover state %d' % state, status, hex(fault.address), fault.code, replay.i,
                             replay.errors[:1], tail))
    return problems, pcs


HOOK_NAMES = [name for name, _, _, _ in HOOKS] + ['w_callback']
BY_NAME = {name: address for name, address, _, _ in HOOKS}


def hook_sites(results):
    """The cases hook_contract_site runs on: passing cases (the original
    does not stop) that together call every hook, the +0x4C callback and
    every entry, picked greedily (most targets not yet covered per native
    run the contract makes there, i.e. per call plus memory access of the
    case, then the earliest case). Returns ([(case index, targets)], targets no case
    reaches); a target is a hook name or ('bytes', entry)."""
    want = set(HOOK_NAMES) | {('bytes', entry) for entry in FUNCS}
    options = [(index, (set(calls) | {('bytes', entry)}) & want, 1 + counts[3])
               for index, (_, entry, errors, _, counts, calls) in enumerate(results)
               if not errors and calls is not None]
    # Every passing case of the entries that return from their own body
    # (not only through the shared end), so each return path is contracted.
    sites = [(index, sorted(targets, key=str)) for index, targets, _ in options
             if results[index][1] in (RETURN_SITES_FULL if reference_mode.FULL else RETURN_SITES)]
    for _, targets in sites:
        want -= set(targets)
    while want and options:
        index, targets, _ = max(options, key=lambda option: (len(option[1] & want) / option[2], -option[0]))
        new = targets & want
        if not new:
            break
        sites.append((index, sorted(new, key=str)))
        want -= new
    return sites, sorted(want, key=str)


def restore(buffers, ram, spad, replay, oracle):
    """Put the start image back into every byte a native run or its
    comparison touched (cheaper than reloading 2 x 32 MiB)."""
    spans = dict(replay.dirty)
    for key, data in oracle.stores:
        if len(data) > spans.get(key, 0):
            spans[key] = len(data)
    nat_ram, nat_spad, exp_ram, exp_spad = buffers
    for key, size in spans.items():
        if in_window(key):   # the window is reloaded by every Replay
            continue
        if key >= 0x70000000:
            at = key - 0x70000000
            chunk = bytes(spad[at:at + size])
            C.memmove(C.addressof(nat_spad) + at, chunk, len(chunk))
            C.memmove(C.addressof(exp_spad) + at, chunk, len(chunk))
        else:
            chunk = bytes(ram[key:key + size])
            C.memmove(C.addressof(nat_ram) + key, chunk, len(chunk))
            C.memmove(C.addressof(exp_ram) + key, chunk, len(chunk))


CONTRACT_CALLS, CONTRACT_ACCESSES = 8, 16


def sample(items, cap):
    """All of `items`, or at most `cap` of them evenly spaced (the first and
    the last always included)."""
    items = list(items)
    if cap is None or len(items) <= cap:
        return items
    step = (len(items) - 1) / (cap - 1)
    return sorted({items[round(i * step)] for i in range(cap)})


def hook_contract_site(site):
    """The fail-stop contract of the header (em_level9_port.h), on one
    case (CASES[index]), against the original's run of that case:
      * at EVERY call k of the case, the hook (or the +0x4C callback)
        returning -1, and at the first call of each target hook also
        INT32_MIN: the entry returns -1 with fault (the original callee's
        address, 2; for the callback the function read from +0x4C), calls
        0..k were made and no other, no `bytes` request follows the failed
        call, memory is the original's at the entry of call k (the failed
        callee's writes are not replayed) and a result is not written;
      * at EVERY memory access n of the case, `bytes` refusing that request:
        -1 with fault (that address, 5), exactly the original's calls before
        that access, no later request and no result;
      * each target hook NULL in the table at its first call k: -1 with
        fault (callee, 1) after exactly the original's calls before k, the
        accesses since call k-1 exactly the original's (none after), memory
        as at the entry of call k, no result;
      * each target hook returning 1 or INT32_MAX at its first call (a
        success): the whole case compares as usual (finish);
      * target ('bytes', entry): `bytes` NULL in the table: -1 with fault
        (the original's first access, 5) after the original's calls before
        it.
    After a fault the module's reads come from a zeroed sink, so its
    control flow runs on; the every-call and every-access sweeps therefore
    reach later hook wrappers with the fault already latched, which is
    where each wrapper's own `a6_failed` test is observable. Returns
    (problems found, native runs made)."""
    index, targets = site
    label, beat, entry, native_args, patches, spad_patches, script, run_set = CASES[index]
    ram0, spad0 = CAPTURES[beat]
    ram, spad = patched(ram0, patches), patched(spad0, spad_patches)
    oracle = Oracle(ram, spad, entry, (native_args, callbacks_of(entry, native_args, ram)), copy_script(script),
                    run_set, set())
    kind = FUNCS[entry][3]
    names = [e[0] for e in oracle.log]
    callee = [BY_NAME[e[0]] if e[0] in BY_NAME else e[1][0] for e in oracle.log]
    flat = [(j, a[0]) for j, interval in enumerate(oracle.access) for a in interval if a[0] not in TABLE_BYTES]
    buffers = load_buffers(ram, spad)
    problems, runs = [], [0]

    def run(**how):
        runs[0] += 1
        replay, status, fault, result = native_call(NATIVE, ram, spad, entry, native_args, oracle,
                                                    buffers=buffers, **how)
        replay.settle()
        return replay, status, fault, result

    def expect(what, replay, status, fault, result, want, extra=True):
        got = (status, fault.address, fault.code, replay.i)
        unwritten = kind is None or result == 0x5A5A5A5A
        if got != want or replay.errors or replay.broken or not extra or not unwritten:
            problems.append((label, what, 'want', (want[0], hex(want[1]), want[2], want[3]),
                             'got', (got[0], hex(got[1]), got[2], got[3]), 'result written' if not unwritten
                             else '', replay.errors[:1]))

    # EM_TEST_FULL=1: every call and every access; the default run an even
    # sample of at most CONTRACT_CALLS calls and CONTRACT_ACCESSES accesses
    # per site (always the first and the last)
    call_ks = sample(range(len(names)), None if reference_mode.FULL else CONTRACT_CALLS)
    access_ns = set(sample(range(len(flat)), None if reference_mode.FULL else CONTRACT_ACCESSES))
    for k in call_ks:   # the sampled calls fail
        replay, status, fault, result = run(inject={k: ('fail', -1)})
        expect('call %d (%s) fails' % (k, names[k]), replay, status, fault, result, (-1, callee[k] & MASK, 2, k + 1),
               len(replay.access) == k + 2 and not replay.access[k + 1]
               and replay.compare(oracle.marks[k], 'the fault', full=False))
        restore(buffers, ram, spad, replay, oracle)
    for n, (j, address) in enumerate(flat):   # the sampled accesses refused
        if n not in access_ns:
            continue
        replay, status, fault, result = run(refuse_at=n)
        expect('access %d refused' % n, replay, status, fault, result, (-1, address, 5, j), replay.requests == n + 1)
        restore(buffers, ram, spad, replay, oracle)
    for target in targets:
        if isinstance(target, tuple):   # ('bytes', entry)
            replay, status, fault, result = run(null='bytes')
            if not flat:   # an entry that touches no memory itself (0x824C40): nothing to refuse
                errors = finish(replay, oracle, kind, status, result, fault, full=False)
                if errors:
                    problems.append((label, 'bytes NULL, no access', errors[:1]))
            else:
                expect('bytes NULL', replay, status, fault, result, (-1, flat[0][1], 5, flat[0][0]))
            restore(buffers, ram, spad, replay, oracle)
            continue
        k = names.index(target)
        replay, status, fault, result = run(null=target)
        expect('%s NULL' % target, replay, status, fault, result, (-1, callee[k] & MASK, 1, k),
               replay.check_access(k) and replay.compare(oracle.marks[k], 'the fault', full=False))
        restore(buffers, ram, spad, replay, oracle)
        replay, status, fault, result = run(inject={k: ('fail', -0x80000000)})
        expect('%s returns INT32_MIN' % target, replay, status, fault, result, (-1, callee[k] & MASK, 2, k + 1),
               len(replay.access) == k + 2 and not replay.access[k + 1])
        restore(buffers, ram, spad, replay, oracle)
        for value in (1, 0x7FFFFFFF):
            replay, status, fault, result = run(inject={k: ('ok', value)})
            errors = finish(replay, oracle, kind, status, result, fault, full=False)
            if errors:
                problems.append((label, '%s returns %d' % (target, value), errors[:1]))
            restore(buffers, ram, spad, replay, oracle)
    return problems, runs[0]





QUICK_RANDOM = 0
PINNED_RANDOM = ()


# The default run's share of the designed cases: a coverage pass over the
# whole set (every case's original run, EM_TEST_FULL=1) picked these so that
# with the capture cases the default run reaches every word the whole set
# reaches (greedy, cheapest first); SURVIVORS are the mutation sweep's cases.
DEFAULT_KEEP = frozenset([
    'a13 823700 s0 r0 c0',
    'a13 823700 s1 r0 c2',
    'a13 823700 s1 r1 c0',
    'a13 823700 s2 r0 c0',
    'a13 823700 s3 r0 c0',
    'a13 823700 s4 r0 c0',
    'a13 823940 sub0 b4 r0',
    'a13 823940 sub1 b0 r1',
    'a13 823940 sub2 b0 r0',
    'a13 823BC0 d2',
    'a13 823C10 s0 r0 i0 sub0 b0',
    'a13 823C10 s0 r1 i0 sub0 b0',
    'a13 823C10 s1 r0 i0 sub0 b4',
    'a13 823C10 s1 r0 i0 sub2 b0',
    'a13 823C10 s1 r0 i1 sub0 b0',
    'a13 823C10 s1 r1 i0 sub1 b0',
    'a13 823C10 s2 r0 i0 sub0 b0',
    'a13 823C10 s3 r0 i0 sub0 b0',
    'a13 823C10 s4 r0 i0 sub0 b0',
    'a13 823D50 s0 r0 i0 sub0 b0',
    'a13 823D50 s0 r1 i0 sub0 b0',
    'a13 823D50 s1 r0 i0 sub0 b4',
    'a13 823D50 s1 r0 i0 sub2 b0',
    'a13 823D50 s1 r0 i1 sub0 b0',
    'a13 823D50 s1 r1 i0 sub1 b0',
    'a13 823D50 s2 r0 i0 sub0 b0',
    'a13 823D50 s3 r0 i0 sub0 b0',
    'a13 823D50 s4 r0 i0 sub0 b0',
    'a13 823E90 s0 step0 r0',
    'a13 823E90 s0 step0 r1',
    'a13 823E90 s1 step0 r0',
    'a13 823E90 s1 step2 r0',
    'a13 823E90 s1 step3 r0',
    'a13 823E90 s1 step4 r0',
    'a13 823E90 s1 step5 r0',
    'a13 823E90 s2 step0 r0',
    'a13 823E90 s3 step0 r0',
    'a13 823E90 s4 step0 r0',
    'a13 823FE0 y211 f0 r1',
    'a13 824180 sub0 b4 a0 r0',
    'a13 824180 sub0 b5 a0 r0',
    'a13 824180 sub1 b0 a0 r0',
    'a13 824180 sub1 b0 a1 r0',
    'a13 824180 sub2 b0 a0 r1',
    'a13 824180 sub3 b0 a0 r1',
    'a13 824180 sub4 b0 a0 r1',
    'a13 824180 sub5 b0 a0 r0',
    'a13 8266A0 s0 r0 rr1 e8 h0 c0',
    'a13 8266A0 s1 r0 rr0 e8 h1 c0',
    'a13 8266A0 s1 r0 rr1 e8 h1 c0',
    'a13 8266A0 s2 r0 rr0 e8 h0 c9',
    'a13 8266A0 s3 r0 rr0 e8 h0 c0',
    'a13 8266A0 s4 r0 rr0 e8 h0 c0',
    'a13 826850 @7B52E0 s0 i0 bits1 r0 sub0 b0 c0',
    'a13 826850 @7B52E0 s0 i0 bits1 r1 sub0 b0 c0',
    'a13 826850 @7B52E0 s0 i1 bits0 r0 sub0 b0 c0',
    'a13 826850 @7B52E0 s1 i0 bits0 r0 sub0 b4 c0',
    'a13 826850 @7B52E0 s1 i0 bits0 r0 sub1 b0 c3',
    'a13 826850 @7B52E0 s1 i0 bits0 r1 sub1 b0 c3',
    'a13 826850 @7B52E0 s1 i0 bits0 r1 sub2 b0 c0',
    'a13 826850 @7B52E0 s2 i0 bits0 r1 sub0 b0 c0',
    'a13 826850 @7B52E0 s3 i0 bits0 r0 sub0 b0 c0',
    'a13 826850 @7B52E0 s5 i0 bits0 r0 sub0 b0 c0',
    'a13 826850 @7B55D0 s0 i0 bits0 r0 sub0 b0 c0',
    'a13 826850 @7B55D0 s0 i0 bits0 r1 sub0 b0 c0',
    'a13 826850 @7B55D0 s0 i0 bits2 r0 sub0 b0 c0',
    'a13 826850 @7B55D0 s0 i0 bits2 r1 sub0 b0 c0',
    'a13 826850 @7B55D0 s1 i0 bits0 r0 sub1 b0 c3',
    'a13 826850 @7B55D0 s1 i0 bits0 r1 sub0 b4 c0',
    'a13 826850 @7B55D0 s1 i0 bits0 r1 sub1 b0 c3',
    'a13 826FF0 s0 r0 sub0 b0 x0',
    'a13 826FF0 s1 r0 sub0 b4 x700',
    'a13 826FF0 s1 r0 sub0 b4 x800',
    'a13 826FF0 s1 r0 sub2 b0 x0',
    'a13 826FF0 s1 r1 sub1 b0 x0',
    'a13 826FF0 s2 r0 sub0 b0 x0',
    'a13 826FF0 s3 r0 sub0 b0 x0',
    'a13 826FF0 s4 r0 sub0 b0 x0',
    'a13 827150 blk 0 0 2 4 2 1 10 sub2',
    'a13 827150 blk 2 0 1 2 3 1 8 sub1',
    'a13 827150 blk 2 1 1 4 31 0 80000000 sub1',
    'a13 827150 blk 3 4 4 1 3 0 0 sub1',
    'a13 827150 blk 4 0 1 0 5 0 20 sub0',
    'a13 827150 blk 4 0 1 0 5 0 20 sub1',
    'a13 827150 blk 4 0 1 0 5 0 20 sub2',
    'a13 827150 s0 fFF m11 sub0',
    'a13 827150 s1 f0 m10 sub0',
    'a13 827150 s1 f0 m11 sub0',
    'a13 827150 s1 f0 m11 sub4',
    'a13 827150 s1 f0 m11 sub5',
    'a13 827150 s1 fFF m12 sub0',
    'a13 827150 s2 f0 m11 sub0',
    'a13 827150 s3 f0 m11 sub0',
    'a13 827150 s4 f0 m11 sub0',
    'a13 8292A0 s0 f0 r0 c0',
    'a13 8292A0 s0 f0 r1 c0',
    'a13 8292A0 s0 f20 r0 c0',
    'a13 8292A0 s1 f0 r0 c0',
    'a13 8292A0 s2 f0 r0 c0',
    'a13 8292A0 s3 f0 r0 c0',
    'a13 8292A0 s64 f20 r0 c0',
    'a13 8293A0 s0 f0 r1 m0 c0',
    'a13 8293A0 s0 f40 r0 m0 c0',
    'a13 8293A0 s1 f40 r0 m13 c0',
    'a13 8293A0 s1 f40 r0 m13 c5',
    'a13 8293A0 s2 f0 r0 m0 c0',
    'a13 8293A0 s3 f0 r0 m0 c0',
    'a13 8299E0 s0 r0 f0',
    'a13 8299E0 s2 r0 f0',
    'a13 8299E0 s4 r0 f0',
    'a13 829AA0 s0 c0',
    'a13 829AA0 s1 c1',
    'a13 829AA0 s1 c24',
    'a13 829AA0 s2 c0',
    'a13 829AA0 s3 c0',
    'a13 829AA0 s4 c0',
    'anim 1BA7F0 @7AAB70 r1',
    'anim 1BDCA0 24.9',
    'anim 1C06E0 aim 0.5 0.5 1',
    'anim 1C06E0 alive 1 sub0 r1 0 0',
    'anim 1C06E0 alive 1 sub1 r0 0 0',
    'anim 1C06E0 alive 1 sub1 r1 3C 0',
    'anim 1C06E0 alive 1 sub2 r0 0 0',
    'anim 1C06E0 alive 1 sub3 r0 0 0',
    'anim 1C06E0 attack 1 1FF0 0 0',
    'anim 1C06E0 attack 2 1000 1 0',
    'anim 1C06E0 attack 3 1000 1 0',
    'anim 1C06E0 attack 4 1000 1 0',
    'anim 1C06E0 attack 5 1000 1 0',
    'anim 1C06E0 attack 5 1029 0 1',
    'anim 1C06E0 hit 4010 1 10',
    'anim 1C06E0 hit 5003 5 50',
    'anim 1C06E0 s0 sub0 r0 0 1000',
    'anim 1C06E0 s2 sub0 r0 0 1000',
    'anim 1C06E0 s2 sub0 r0 0.86 1000',
    'anim 1C06E0 s2 sub1 r0 0 1000',
    'anim 1C06E0 s2 sub1 r1 0 1000',
    'anim 1C06E0 s2 sub3 r0 0 1000',
    'anim 1C06E0 s3 sub0 r0 0 1000',
    'anim 1C06E0 s4 sub0 r0 0 1000',
    'anim 1C1030 s0 r0 00',
    'anim 1C1030 s0 r0 11',
    'anim 1C1030 s1 sub0 1003 5 1',
    'anim 1C1030 s1 sub0 1005 5 1',
    'anim 1C1030 s1 sub0 3 5 1',
    'anim 1C1030 s1 sub1 0 5 1',
    'anim 1C1030 s1 sub2 0 5 1',
    'anim 1C1030 s1 sub2 0 5 2',
    'anim 1C1030 s1 sub3 0 5 1',
    'anim 1C1030 s2 r0 00',
    'anim 1C1030 s2 sub1 1000',
    'anim 1C1030 s2 sub2 0',
    'anim 1C1030 s3 r0 00',
    'anim 1C1030 s4 r0 00',
    'anim 1C4BA0 s0 r0 k4',
    'anim 1C4BA0 s1 r0 k3',
    'anim 1C4BA0 s2 r0 k4',
    'anim 1C4BA0 s3 r0 k4',
    'anim 1C4BA0 s4 r0 k4',
    'exit 1383C0 m0 s0 r1 2 2 1 13 1',
    'exit 1383C0 m0 s2 r1 80 8000 1 12 0',
    'exit 1383C0 m0 s4 r1 2 2 1 13 1',
    'exit 1383C0 m1 s1 r1 2 2 1 13 1',
    'exit 1383C0 m5 s3 r1 2 2 1 13 1',
    'exit 138540 0 0 1 1',
    'exit 138540 0 80 0 1',
    'exit 138540 FF 0 1 1',
    'exit 1386E0 r1 sub0',
    'exit 1386E0 r1 sub1',
    'exit 1386E0 r1 sub2',
    'exit 1386E0 r1 sub3',
    'exit 1386E0 r1 sub4',
    'exit 1386E0 r1 sub5',
    'exit 1386E0 tail 2 0 0 80 1 0 0 8 12345678',
    'exit 1386E0 tail 80 FFFF 2 0 1 0 1 8 -1',
    'exit 154460 m0 0 0 46',
    'exit 154460 m0 0 5 0',
    'exit 154460 m1 0 0 0',
    'exit 1546C0 s0',
    'exit 1546C0 s2',
    'exit 1546C0 s3',
    'exit 1546C0 s4',
    'exit 154740 13 12 13 0',
    'exit 154740 5 20 13 9',
    'exit 1549C0 hold m0 4.5 0 w0',
    'exit 1549C0 hold m1 0 0 w0',
    'exit 1549C0 step1 2 300',
    'exit 1549C0 sub0 w0 p0 h0',
    'exit 1549C0 sub0 w1 p0 h1',
    'exit 1549C0 sub1 w0 p0 h0',
    'exit 1549C0 sub2 w0 p0 h0',
    'exit 1549C0 sub2 w1 p0 h0',
    'exit 1549C0 sub3 w0 p0 h0',
    'exit 1549C0 sub4 w0 p0 h0',
    'exit 1549C0 sub5 w0 p0 h0',
    'exit 15A200 D 1C01000',
    'exit 183440 a13_04_hatch 1000 3',
    'exit 196970 0 1077.2 0 251 237.5',
    'exit 196970 0 1077.2 0 252 247.5',
    'exit 196970 0 710 0 283.5 263.1',
    'exit 196970 0 710 0 284.5 273.1',
    'exit 196970 0 855.1 0 313 304',
    'exit 196970 0 855.1 0 314 314',
    'exit 196970 0 907 0 313 301.1',
    'exit 196970 0 907 0 314 311.1',
    'exit 196970 0 925.1 0 313 303',
    'exit 196970 0 925.1 0 314 313',
    'exit 196CE0 s0 c2D 848.6 260 0',
    'exit 196CE0 s0 c7 848.6 300 3',
    'exit 196CE0 s0 c7 915 240 4',
    'exit 196CE0 s0 live 700 700 0',
    'exit 196CE0 s0 live 855.1 851.3 0',
    'exit 196CE0 s1 c2D 848.6 260 0',
    'exit 196CE0 s1 c7 848.6 260 0',
    'exit 196CE0 s2 c2D 848.6 260 0',
    'exit 196CE0 s2 c2D 848.6 300 3',
    'exit 196CE0 s2 c7 848.6 260 0',
    'exit 196CE0 s3 c2D 848.6 260 0',
    'exit 196CE0 s4 c2D 848.6 260 0',
    'exit 196CE0 s5 c2D 848.6 260 0',
    'fx 1E5AC0 0 12345678 3 0 (1.9, 0.5)',
    'fx 1E5AC0 2 12345678 1 76 (1.9, 0.5)',
    'fx 1E7050 s0 1 5 2 0',
    'fx 1E7050 s1 1 5 2 0',
    'fx 1E7050 s1 1 5 2 1.25',
    'fx 1E7050 s1 3 FFFFFF 0 0',
    'fx 1E7050 s2 1 5 2 0',
    'fx 1E7050 s3 1 5 2 0',
    'fx 1E7050 s4 1 5 2 0',
    'probe 826140 s1 12 0 11 01 0 11',
    'probe 826140 s1 13 0 01 11 200 11',
    'probe 826140 s1 13 0 11 12 0 11',
    'probe 826140 s4 0 0 01 11 0 11',
    'probe 826140 s4 0 1C06800 12 11 0 14',
    'turret idle 0 0 -0.016 -1.2 0 0 0',
    'turret idle 0 1 0.016 1.2 1 1C05000 0',
    'turret idle 3 1 0.016 1.2 0 0 0',
    'turret period 13 2 1 0 0 5A 0 -5 10 0',
    'turret period 13 2 1 1 0 5A 0 -5 10 0',
    'turret period 13 2 2 0 0 10 1 3 0 1',
    'turret period 13 2 2 0 0 5 0 -5 10 0',
    'turret period 13 2 2 0 0 5A 0 -5 10 0',
    'turret period 13 2 2 0 0 5A 1 -5 10 0',
    'turret period 13 2 2 0 0 5B 1 -5 10 0',
    'turret period 13 2 2 0 0 5C 1 -5 10 0',
    'turret rearm s1 7FFF0000',
    'turret rearm s4 30000000',
    'turret s0 r0 c0',
    'turret s0 r0 c1C07000',
    'turret s1 count 1 1 1 40000',
    'turret s1 count 20 1 1 7FFF0000',
    'turret s1 sweep -1.2 3.1',
    'turret s1 sweep -1.3 3.1',
    'turret s1 sweep 1.2 3.1',
    'turret s2 -1.55 0 0',
    'turret s2 -1.5708 1 0',
    'turret s2 -1.5708 1 1',
    'turret s2 -1.6 0 0',
    'turret s2 -2 0 0',
    'turret s3',
    'turret s4 count 1 1 1 40000',
    'turret s4 count 20 1 1 7FFF0000',
    'turret s4 sweep -1.2 3.1',
    'turret s4 sweep -1.3 3.1',
    'turret s4 sweep 1.2 3.1',
    'turret s64',
    'turret track -10 0 0 0 -0.8 f1',
    'turret track 0 -5 -10 -1.2 -0.4 f0',
    'turret track 10 0 0 -1.2 -0.4 f1',
    'turret track 10 0 0 1.2 -1.2 f0'
])
SURVIVORS = ('survivor ',)


def select_cases(elf):
    """The mode's case list (EM_TEST_FULL=1: all; the default run keeps
    every capture case, the designed cases of DEFAULT_KEEP and the mutation
    sweep's survivor cases)."""
    cases, captured, targeted = case_list(elf)
    total = len(cases)
    if not reference_mode.FULL:
        cases = cases[:captured] + [c for c in cases[captured:]
                                    if c[0] in DEFAULT_KEEP or c[0].startswith(SURVIVORS)]
        return cases, captured, len(cases) - captured, total
    return cases, captured, targeted, total


def image_for(entry):
    """An image holding the entry's code (boot text is the same in all)."""
    return CAPTURES[A13_BEATS[0]][0]
def main():
    global NATIVE
    elf = read_elf()
    CAPTURES.update(load_captures(elf))
    NATIVE = build(os.environ.get('EM_LEVEL9_PORT_SOURCE'))
    selected, captured, targeted, total = select_cases(elf)
    CASES[:] = selected
    only = os.environ.get('EM_LEVEL9_PORT_ONLY')   # debugging: a label prefix
    if only:
        CASES[:] = selected = [c for c in selected if c[0].startswith(only)]
    results = run_cases(selected)
    failures, per_fn, seen, totals = [], {}, set(), [0, 0, 0, 0]
    header_problems, header_count = header_checks(os.environ.get('EM_LEVEL9_PORT_SOURCE'))
    print('  header / wrappers vs HOOKS: %s' % ('ok (%d hooks: names, order, argument and result types; one '
                                                'wrapper each, calling it and latching its address)' % header_count
                                                if not header_problems else header_problems[:4]))
    if header_problems:
        failures.append(('header vs HOOKS', header_problems))
    if not only:
        problems, pcs = leftover_checks()
        seen.update(pcs)
        print('  undefined-value paths: %s' % ('ok (001E7050 with a zero bone count, states 0 and 1: fault 7 '
                                               'right after the count is read, after the original calls and '
                                               'accesses)' if not problems else problems))
        if problems:
            failures.append(('register leftover', problems))
    for label, entry, errors, pcs, counts, _ in results:
        totals = [a + b for a, b in zip(totals, counts)]
        per_fn[entry] = per_fn.get(entry, 0) + 1
        seen.update(pcs)
        if errors:
            failures.append((label, errors))
    reference_mode.banner(reference_mode.part(len(selected), total, 'cases'),
                          '%d capture + %d designed cases kept' % (captured, targeted))
    covered_total = words_total = 0
    missing_words = {}
    for entry in sorted(FUNCS):
        _, size, _, _ = FUNCS[entry]
        words = reachable_words(image_for(entry), entry, size) - set(DEAD_WORDS)
        hit = len(words & seen)
        covered_total += hit
        words_total += len(words)
        if hit < len(words):
            missing_words[entry] = sorted(words - seen)
        print('  %06X  %4d cases  %4d/%4d reachable non-branch words executed'
              % (entry, per_fn.get(entry, 0), hit, len(words)))
    print('  coverage %d/%d reachable words' % (covered_total, words_total))
    if os.environ.get('EM_LEVEL9_PORT_MISSING'):
        for entry, words in missing_words.items():
            print('   missing %06X: %s' % (entry, ' '.join('%X' % w for w in words[:400])))
    if covered_total < words_total and not only:
        failures.append(('coverage', ['%d of %d reachable original words not executed'
                                      % (words_total - covered_total, words_total)]))
    if not only:
        problems = fault_checks()
        print('  native fail-stop checks: %s' % ('ok (NULL hook, failing hook, unmapped address, unmapped address '
                                                 'before any call, latched fault; every entry refuses a latched '
                                                 'fault, a NULL hook table, a NULL fault pointer and a NULL result '
                                                 'pointer)' if not problems else problems))
        if problems:
            failures.append(('native fail-stop', problems))
        sites, missing = hook_sites(results)
        if os.environ.get('EM_LEVEL9_PORT_NOCONTRACT'):   # debugging only: skips the contract
            sites, missing = [], []
        outcome = reference_mode.parallel_map(hook_contract_site, sites)
        contract = [p for problems_, _ in outcome for p in problems_]
        if missing:
            contract.append(('no passing case reaches', missing))
        sweep = ('every call failing and every memory access refused' if reference_mode.FULL else
                 'an even sample of each case\'s calls failing (at most %d) and memory accesses refused (at most %d)'
                 % (CONTRACT_CALLS, CONTRACT_ACCESSES))
        print('  hook contract: %s' % (
            ('ok (%d native runs on %d cases: %s; each of the %d hooks and the indirect-call callback NULL, '
             'returning INT32_MIN, 1 and INT32_MAX; `bytes` NULL on all %d entries)'
             % (sum(n for _, n in outcome), len(sites), sweep, len(HOOKS), len(FUNCS))) if not contract
            else contract[:6]))
        if contract:
            failures.append(('hook contract', contract))
        if reuse_checks is not None:
            problems, report = reuse_checks(elf)
            print('  reuse checks: %s' % (report if not problems else problems[:4]))
            if problems:
                failures.append(('reuse checks', problems[:6]))
    for label, errors in failures[:int(os.environ.get("EM_LEVEL9_PORT_SHOW", "12"))]:
        print('FAIL', label)
        for error in errors[:4]:
            print('   ', error)
    if failures:
        print('%d of %d cases FAILED' % (len(failures), len(selected)))
        sys.exit(1)
    print('  %d runs (%d as given + %d poisoned; a case whose function stores nothing has no poisoned run), '
          '%d call entries compared (%s), %d helper register rehearsals'
          % (totals[0], len(selected), totals[0] - len(selected), totals[1],
             'all 32 MiB + scratchpad + stack window' if reference_mode.FULL else 'dirty set', totals[2]))
    print('all %d cases identical: callee calls, arguments and results; memory at every call entry (%s); '
          'the memory accesses between calls (one for one, in order); '
          'memory after the last store (all 32 MiB + scratchpad + stack window); the table\'s ctx at every hook and '
          '`bytes` call'
          % (len(selected), 'all 32 MiB + scratchpad + stack window' if reference_mode.FULL else 'dirty set'))



# ----------------------------------------------------------------------------
# reuse_checks: the census rows with an existing verified translation,
# re-run against the ORIGINAL over the ninth-level captures with each
# module's own harness (imported, not modified; libraries are built into
# build/level9/port/reuse):
#   001E7C60  em_area01_sys_001E7C60 (tools/test_area01_sys_reference.py,
#             run_case): the level fill of the three AREA19 grids that
#             001E7D20 owns at the a13_05 arrival (s = D_00275C20 + byte
#             +0x0D * 0xA060), with its height and edge values;
#   00214570  em_status_pages_00214570 (tools/test_status_pages_reference.py,
#             item_case): the harness's own EQUIPMENT page inputs over the
#             a13_02 image (the item 0x1A page of [4]'s scene) and a13_03.
REUSE_OUT = OUT / 'reuse'


def reuse_fill(elf, smoke=False):
    import test_area01_sys_reference as A1
    A1.OUT = REUSE_OUT / 'area01_sys'
    A1.ROUTE = DECOMP / 'build/s87/route_a13'
    A1.ELF = elf
    A1.FUNC_RANGES = tuple((fn, fn + size) for fn, size in sorted(A1.FUNCS.items()))
    base = FallEE(elf)
    A1.ELF_IMAGE = bytes(base.mem[:A1.ELF_END])
    A1.BRANCH_PCS.update(A1.branch_pcs(base.mem, 0x1E7C60, A1.FUNCS[0x1E7C60]))
    A1.NATIVE = A1.build_native()
    beat = 'a13_05_shaft'
    ram, _ = CAPTURES[beat]
    grids = sorted({u32(ram, 0x275C20) + ram[n + 0xD] * 0xA060 for n in owners(ram, 0x1E7D20)})
    values = (132.0, 160.0, 131.9, -0.0) if not smoke else (132.0,)
    cases = []
    for s in (grids if not smoke else grids[:1]):
        for v in values:
            cases.append(A1.case('reuse 1E7C60 %X %g' % (s, v), 0x1E7C60, beat, [s], fargs=[A1.FB(v)]))
    for c in cases:
        A1.run_case(c)
    return '001E7C60 %d cases over the %d AREA19 grids of a13_05' % (len(cases), len(grids))


def reuse_status(elf, smoke=False):
    import test_status_pages_reference as SP
    SP.IMAGES['a13_02'] = BEAT_DIRS['a13_02_door17']
    SP.IMAGES['a13_03'] = BEAT_DIRS['a13_03_item27']
    for name in ('a13_02', 'a13_03'):
        if name not in SP.BEATS:
            SP.BEATS.append(name)
    SP.setup()
    base = [k for kind, beat, k in SP.item_items() if kind == '00214570' and beat == 'hub']
    items = [('00214570', b, k) for b in ('a13_02', 'a13_03') for k in base]
    if smoke:
        items = items[::8]
    done = refused = 0
    for item in items:
        r = SP.item_case(item)
        if r == 'unmeasured' or r == 'skipped':
            refused += 1
        else:
            done += 1
    return '00214570 %d EQUIPMENT page cases over the a13_02 / a13_03 images (%d refused on both sides)' % (
        done, refused)


def reuse_checks(elf):
    """EM_TEST_FULL=1 runs every check in full; the default run keeps one
    smoke sample of each."""
    problems, parts = [], []
    for check in (reuse_fill, reuse_status):
        try:
            parts.append(check(elf, smoke=not reference_mode.FULL))
        except AssertionError as error:
            problems.append((check.__name__, repr(error)[:600]))
    return problems, 'ok: ' + '; '.join(parts)


if __name__ == '__main__':
    main()
