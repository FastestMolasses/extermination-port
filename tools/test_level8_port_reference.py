#!/usr/bin/env python3
"""Execute the ORIGINAL eighth-level code (the a06b / a01v / a04b census
rows: the lift, the area-change segment, AREA01's gap node, the creature at
the lift and its helpers, AREA04's overlay rows) and compare
em_level8_port*.c.

docs/LEVEL8_PORT.md. The census deltas of the eighth-level route (decomp
build/s87/census/a06b_delta.json, a01v_delta.json and a04b_delta.json,
new_functions: 5, 3 and 45 rows; a22b_delta.json lists none) name 53
functions. The ones without a verified port translation are translated in
em_level8_port_*.c and are the entries below (FUNCS). Calls between them
run as original code at the top level of the oracle (only other boot
functions are hooks), and every function is also an entry of its own.

The harness is tools/test_area06_port_reference.py's (lane A06T, itself the
AREA22 / AREA04 design), copied and owned here, with the eighth-level
captures, this module's hook table and cases. Entries that take the
original stack pointer (kind S) run the oracle with sp = STACK_TOP and give
the native entry STACK_TOP; the frame locals live in the compared stack
window.

Oracle: the shared EE interpreter with the measured float model (FallEE,
tools/test_player_fall_reference.py) runs the original code resident in the
recorded RAM images of the eighth-level route (decomp
build/s87/route_a06b/, route_a01v/, route_a22b/ and route_a04b/ <beat>/).
Before any case the test checks that the overlay text of every image equals
the user's extract/OVERLAY/<AREA>.BIN (text size from the file header) and
that the boot text below 0x241000 equals the pinned ELF, so every executed
instruction is original. Nothing here embeds original bytes; reports hold
counts only.

Callees (every boot function outside this module and the actor's +0x4C
method) are intercepted at their entry and logged with their arguments. A
callee either runs as ORIGINAL code nested inside the oracle (the pure
helpers in RUN), its writes recorded, or is stubbed with the case's
scripted result. The native module runs over a byte copy of the same RAM
with hooks that must be called in the same order with the same arguments;
each hook replays the original callee's writes and result.

Compared, per case (the AREA22 harness's list): memory at the entry of
every call before the callee's writes are replayed; the callee and its
arguments; the memory accesses between calls one for one, in order, by
address, size and changed-or-not; all memory after the last store; the
return value; the store-log self-check; stops at unmapped or misaligned
original accesses; every case again from a poisoned start image; coverage
of every reachable original word; the fail-stop contract; the table's ctx
at every call.

Cases:
  capture   the entries on the captured state of every image they apply to.
  designed  every state and step, callee results, the float boundaries of
            every compare, scribbling callees.
  perturbed seeded variations (EM_TEST_FULL=1 only).

EM_LEVEL8_PORT_ONLY=<label prefix> runs a subset (no coverage / contract /
reuse checks). EM_LEVEL8_PORT_SOURCE=<dir> tests another copy of the module
sources (the mutation sweep). EM_LEVEL8_PORT_MISSING=1 lists unexecuted
words. At most 4 worker processes unless EM_TEST_JOBS says otherwise.
"""
import ctypes as C
import math
import os
import random
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
OVERLAY_FILES = {2: DECOMP / 'extract/OVERLAY/AREA01.BIN', 5: DECOMP / 'extract/OVERLAY/AREA04.BIN',
                 6: DECOMP / 'extract/OVERLAY/AREA06.BIN', 10: DECOMP / 'extract/OVERLAY/AREA13.BIN',
                 19: DECOMP / 'extract/OVERLAY/AREA22.BIN'}
OUT = ROOT / 'build/level8/port'
MASK = 0xFFFFFFFF
MASK64 = 0xFFFFFFFFFFFFFFFF
RAM_SIZE, SPAD_SIZE = 0x2000000, 0x4000
ARENA = 0x823500
STACK_LO = STACK_TOP - 0x800
STACK_SIZE = STACK_TOP - STACK_LO
BEAT_DIRS = {}
for _group, _beats in (('a06b', ('a06b_00_trigger_jump', 'a06b_01_door1')),
                       ('a01v', ('a01v_00_catwalk_east', 'a01v_01_gap_jump', 'a01v_02_door18')),
                       ('a22b', ('a22b_00_to_door7', 'a22b_01_door6')),
                       ('a04b', ('a04b_00_door45', 'a04b_01_npc', 'a04b_02_door45_out', 'a04b_03_reader',
                                 'a04b_04_lift'))):
    for _b in _beats:
        BEAT_DIRS[_b] = DECOMP / 'build/s87' / ('route_' + _group) / _b
BEATS = list(BEAT_DIRS)
# The overlay each image holds (header id at 0x823504).
RESIDENT = {'a06b_00_trigger_jump': 6, 'a06b_01_door1': 2, 'a01v_00_catwalk_east': 2, 'a01v_01_gap_jump': 2,
            'a01v_02_door18': 19, 'a22b_00_to_door7': 19, 'a22b_01_door6': 5, 'a04b_00_door45': 5,
            'a04b_01_npc': 5, 'a04b_02_door45_out': 5, 'a04b_03_reader': 5, 'a04b_04_lift': 10}
AREA04_BEATS = [b for b in BEATS if RESIDENT[b] == 5]
POOL, NODE, NODES = 0x7A5640, 0x2F0, 400
PLAYER = 0x8102B0
CAM = 0x8101E0
class _Route:
    """ROUTE / beat resolves to the beat's folder (three route trees)."""

    def __truediv__(self, beat):
        return BEAT_DIRS[beat]


ROUTE = _Route()

U, I, F, Q, S = 'u', 'i', 'f', 'q', 's'
# (entry, native symbol, byte length of the function body, argument kinds,
# result kind or None). Word arguments go to a0.., floats to f12.., S is the
# entry stack pointer (native only; the oracle's sp is STACK_TOP).
FUNCS = {
    # the lift, its buttons and the area-change segment
    0x1BBD20: ('em_level8_port_001BBD20', 0x38, [U, I], I),
    0x1BC560: ('em_level8_port_001BC560', 0x16C, [], I),
    0x1BC6D0: ('em_level8_port_001BC6D0', 0x70, [U, U], I),
    0x1BC740: ('em_level8_port_001BC740', 0x11C, [U, U], I),
    0x1BC860: ('em_level8_port_001BC860', 0xF4, [U, U], I),
    0x1BD180: ('em_level8_port_001BD180', 0xE8, [U], I),
    0x1BD270: ('em_level8_port_001BD270', 0xF8, [U], I),
    0x1BD9F0: ('em_level8_port_001BD9F0', 0x2AC, [U], None),
    0x1BDE60: ('em_level8_port_001BDE60', 0x158, [U], None),
    # AREA01's gap node
    0x1284E0: ('em_level8_port_001284E0', 0x114, [U, U, I, U], I),
    0x12B850: ('em_level8_port_0012B850', 0x118, [U, U], None),
    0x1BE6C0: ('em_level8_port_001BE6C0', 0x3F4, [U], None),
    0x1BEAC0: ('em_level8_port_001BEAC0', 0xA8, [U, U, I, I], I),
    0x1BF5B0: ('em_level8_port_001BF5B0', 0x80, [U, U, I], None),
    0x1BF6B0: ('em_level8_port_001BF6B0', 0x8DC, [U], None),
    0x1BFF90: ('em_level8_port_001BFF90', 0x40, [U, U, I], None),
    # the creature at the lift and its helpers
    0x11E0A8: ('em_level8_port_0011E0A8', 0x9C, [U, F], F),
    0x12E3A0: ('em_level8_port_0012E3A0', 0x1B8, [U, S], None),
    0x12E560: ('em_level8_port_0012E560', 0x2DC, [U, U], None),
    0x12E840: ('em_level8_port_0012E840', 0x318, [U, U, S], None),
    0x12EB60: ('em_level8_port_0012EB60', 0x59C, [U, U, S], None),
    0x12F100: ('em_level8_port_0012F100', 0x5BC, [U, U, S], None),
    0x12FC10: ('em_level8_port_0012FC10', 0x9A0, [U, U, S], None),
    0x131ED0: ('em_level8_port_00131ED0', 0x48, [U], None),
    0x131F90: ('em_level8_port_00131F90', 0x500, [U, U, S], None),
    0x132490: ('em_level8_port_00132490', 0x43C, [U, U, S], None),
    0x1328D0: ('em_level8_port_001328D0', 0x6D8, [U, U, S], None),
    0x132FB0: ('em_level8_port_00132FB0', 0x434, [U, U, S], I),
    0x1333F0: ('em_level8_port_001333F0', 0x244, [U, U], I),
    0x133640: ('em_level8_port_00133640', 0x394, [U, U], I),
    0x133A20: ('em_level8_port_00133A20', 0x2A4, [U, U], I),
    0x133DB0: ('em_level8_port_00133DB0', 0x78, [U, U], None),
    0x21BE40: ('em_level8_port_0021BE40', 0x90, [U], I),
    0x1B4810: ('em_level8_port_001B4810', 0x4D8, [U], None),
    # the probes
    0x1A7B80: ('em_level8_port_001A7B80', 0x14, [U, S], I),
    0x1A7BA0: ('em_level8_port_001A7BA0', 0xABC, [U, U, U, U, S], I),
    0x1B1560: ('em_level8_port_001B1560', 0x6C, [U, U, F], I),
    0x1B2B10: ('em_level8_port_001B2B10', 0x70, [U, U, U], None),
    0x1B2BF0: ('em_level8_port_001B2BF0', 0x104, [U, U, U, F, S], I),
    0x1B30E0: ('em_level8_port_001B30E0', 0x168, [U, U], I),
    0x1B32F0: ('em_level8_port_001B32F0', 0xA0, [U, U, F, S], I),
    0x1B3440: ('em_level8_port_001B3440', 0xA4, [U, U, U, F, S], I),
    0x1B3580: ('em_level8_port_001B3580', 0xE4, [U, U], F),
    0x1B55E0: ('em_level8_port_001B55E0', 0x1A8, [U, I], None),
    # packets and effects
    0x1EAD70: ('em_level8_port_001EAD70', 0x184, [I, I], None),
    0x1ED100: ('em_level8_port_001ED100', 0x348, [I, I], None),
    0x1F91C0: ('em_level8_port_001F91C0', 0x494, [U, S], None),
    # AREA04 overlay (runtime addresses)
    0x823920: ('em_level8_port_00823920', 0x7C, [U], None),
    0x8239A0: ('em_level8_port_008239A0', 0xF0, [U], None),
    0x823A90: ('em_level8_port_00823A90', 0x80, [U], None),
    0x8245F0: ('em_level8_port_008245F0', 0xBC, [U], None),
}
OVERLAY_OF = {0x823920: 5, 0x8239A0: 5, 0x823A90: 5, 0x8245F0: 5}
# The entries whose passing cases are all contract sites (they return from
# their own body): the default run and EM_TEST_FULL=1.
RETURN_SITES = (0x1BC6D0, 0x1284E0, 0x21BE40, 0x1B30E0)
RETURN_SITES_FULL = (0x1BC6D0, 0x1BD180, 0x1BD270, 0x1284E0, 0x1BEAC0, 0x21BE40, 0x1B30E0, 0x1333F0, 0x11E0A8)
def has_result(entry):
    return FUNCS[entry][3] is not None




# Hooks, in the field order of EmLevel8PortHooks (after ctx and bytes; the
# +0x4C callback w_callback follows them).
# name -> (original address, [(ctype, register)], result kind or None)
HOOKS = [
# BEGIN GENERATED HOOKS
    ('w_001000E0', 0x1000E0, [(Q, 4), (Q, 5)], I),
    ('w_001026A0', 0x1026A0, [(U, 4), (U, 5), (U, 6)], None),
    ('w_001026D0', 0x1026D0, [(U, 4), (U, 5), (U, 6)], None),
    ('w_00102718', 0x102718, [(U, 4), (U, 5), (U, 6)], None),
    ('w_00102738', 0x102738, [(U, 4), (U, 5)], F),
    ('w_00102760', 0x102760, [(U, 4), (U, 5)], None),
    ('w_001028B8', 0x1028B8, [(U, 4), (U, 5), (U, 6)], None),
    ('w_001028D0', 0x1028D0, [(U, 4), (U, 5), (U, 6)], None),
    ('w_00102948', 0x102948, [(U, 4), (U, 5)], None),
    ('w_00102958', 0x102958, [(U, 4), (U, 5)], None),
    ('w_001029C0', 0x1029C0, [(U, 4)], None),
    ('w_00102B08', 0x102B08, [(U, 4), (U, 5), (F, 12)], None),
    ('w_00102BB0', 0x102BB0, [(U, 4), (U, 5), (F, 12)], None),
    ('w_00102C58', 0x102C58, [(U, 4), (U, 5), (U, 6)], None),
    ('w_0011DE90', 0x11DE90, [(F, 12)], F),
    ('w_0011DF78', 0x11DF78, [(F, 12)], F),
    ('w_0011E2A8', 0x11E2A8, [(F, 12)], F),
    ('w_0011E620', 0x11E620, [(F, 12), (F, 13)], F),
    ('w_0011E748', 0x11E748, [(F, 12)], F),
    ('w_00122BB8', 0x122BB8, [], I),
    ('w_001281C0', 0x1281C0, [(F, 12)], I),
    ('w_00128250', 0x128250, [(F, 12)], I),
    ('w_00128350', 0x128350, [(F, 12)], Q),
    ('w_00128640', 0x128640, [(U, 4)], I),
    ('w_001287F0', 0x1287F0, [(U, 4), (U, 5), (I, 6), (F, 12)], None),
    ('w_0012F6C0', 0x12F6C0, [(U, 4), (U, 5)], None),
    ('w_0012F980', 0x12F980, [(U, 4), (U, 5)], None),
    ('w_0012FA50', 0x12FA50, [(U, 4), (U, 5)], None),
    ('w_001305B0', 0x1305B0, [(U, 4), (U, 5)], None),
    ('w_00130AB0', 0x130AB0, [(U, 4), (U, 5)], None),
    ('w_00131210', 0x131210, [(U, 4), (U, 5)], None),
    ('w_00131510', 0x131510, [(U, 4), (U, 5)], None),
    ('w_00131650', 0x131650, [(U, 4), (U, 5)], None),
    ('w_00131E80', 0x131E80, [(U, 4)], None),
    ('w_00131F20', 0x131F20, [(U, 4), (U, 5), (U, 6)], None),
    ('w_001339E0', 0x1339E0, [(U, 4), (U, 5)], None),
    ('w_00158590', 0x158590, [(U, 4), (I, 5), (I, 6)], None),
    ('w_00182F90', 0x182F90, [(U, 4), (U, 5)], None),
    ('w_0018D7B0', 0x18D7B0, [(U, 4), (I, 5)], None),
    ('w_0019A310', 0x19A310, [(U, 4)], I),
    ('w_0019A570', 0x19A570, [(U, 4), (U, 5), (I, 6), (I, 7)], I),
    ('w_0019AB20', 0x19AB20, [(U, 4), (U, 5), (U, 6), (I, 7)], I),
    ('w_0019AD00', 0x19AD00, [(U, 4), (U, 5), (I, 6)], I),
    ('w_0019B6C0', 0x19B6C0, [(U, 4), (U, 5)], I),
    ('w_0019BC40', 0x19BC40, [(U, 4)], None),
    ('w_001AEBA0', 0x1AEBA0, [(I, 4)], None),
    ('w_001AF890', 0x1AF890, [(U, 4)], None),
    ('w_001AFA90', 0x1AFA90, [(I, 4)], U),
    ('w_001AFC10', 0x1AFC10, [(U, 4)], None),
    ('w_001B0D80', 0x1B0D80, [(U, 4)], None),
    ('w_001B0FD0', 0x1B0FD0, [(U, 4)], I),
    ('w_001B1020', 0x1B1020, [(U, 4), (I, 5), (I, 6), (I, 7)], None),
    ('w_001B10B0', 0x1B10B0, [(U, 4), (I, 5), (I, 6)], I),
    ('w_001B1190', 0x1B1190, [(I, 4)], None),
    ('w_001B1240', 0x1B1240, [(U, 4), (F, 12), (F, 13)], F),
    ('w_001B12B0', 0x1B12B0, [(F, 12), (F, 13), (F, 14)], F),
    ('w_001B13F0', 0x1B13F0, [(U, 4), (U, 5), (F, 12)], I),
    ('w_001B1470', 0x1B1470, [(F, 12)], F),
    ('w_001B15D0', 0x1B15D0, [(U, 4), (U, 5)], F),
    ('w_001B17A0', 0x1B17A0, [(U, 4)], None),
    ('w_001B1D20', 0x1B1D20, [(U, 4)], None),
    ('w_001B1EA0', 0x1B1EA0, [(I, 4), (U, 5), (U, 6), (I, 7)], I),
    ('w_001B2140', 0x1B2140, [(U, 4)], I),
    ('w_001B2E50', 0x1B2E50, [(U, 4), (U, 5)], I),
    ('w_001B2F70', 0x1B2F70, [(U, 4), (U, 5)], I),
    ('w_001B3250', 0x1B3250, [(U, 4), (U, 5), (F, 12)], I),
    ('w_001B3390', 0x1B3390, [(U, 4), (U, 5), (U, 6), (F, 12)], I),
    ('w_001B37D0', 0x1B37D0, [(U, 4), (F, 12), (F, 13)], F),
    ('w_001B3F10', 0x1B3F10, [(U, 4), (F, 12), (F, 13)], I),
    ('w_001B4CF0', 0x1B4CF0, [(U, 4)], None),
    ('w_001B5360', 0x1B5360, [(U, 4)], None),
    ('w_001BA1A0', 0x1BA1A0, [(U, 4), (U, 5)], None),
    ('w_001BA1F0', 0x1BA1F0, [(U, 4)], I),
    ('w_001BA580', 0x1BA580, [(U, 4), (I, 5)], None),
    ('w_001BC150', 0x1BC150, [(U, 4)], None),
    ('w_001BDCA0', 0x1BDCA0, [(U, 4)], I),
    ('w_001BDD70', 0x1BDD70, [(U, 4)], I),
    ('w_001BF630', 0x1BF630, [(U, 4), (U, 5), (U, 6)], I),
    ('w_001C1500', 0x1C1500, [(U, 4), (I, 5), (F, 12), (F, 13), (F, 14)], None),
    ('w_001C1570', 0x1C1570, [(U, 4)], I),
    ('w_001C2770', 0x1C2770, [(U, 4), (U, 5), (I, 6)], I),
    ('w_001C3D60', 0x1C3D60, [(U, 4), (U, 5)], None),
    ('w_001C6160', 0x1C6160, [(U, 4)], I),
    ('w_001C62C0', 0x1C62C0, [(U, 4)], None),
    ('w_001C6380', 0x1C6380, [(U, 4)], None),
    ('w_001C63D0', 0x1C63D0, [(U, 4)], None),
    ('w_001C63E0', 0x1C63E0, [(U, 4), (I, 5)], None),
    ('w_001C64F0', 0x1C64F0, [(U, 4), (F, 12)], I),
    ('w_001C67E0', 0x1C67E0, [(U, 4), (I, 5), (F, 12), (F, 13)], None),
    ('w_001C68C0', 0x1C68C0, [(U, 4)], None),
    ('w_001CFB50', 0x1CFB50, [(U, 4), (I, 5), (I, 6), (F, 12), (F, 13), (F, 14), (F, 15), (F, 16)], None),
    ('w_001CFBE0', 0x1CFBE0, [(I, 4), (I, 5), (U, 6), (U, 7), (I, 8)], None),
    ('w_001D04B0', 0x1D04B0, [(U, 4), (I, 5), (U, 6), (F, 12), (F, 13)], None),
    ('w_001D0C80', 0x1D0C80, [(U, 4), (U, 5)], None),
    ('w_001D0D40', 0x1D0D40, [(U, 4), (U, 5), (I, 6), (I, 7)], None),
    ('w_001D0D60', 0x1D0D60, [(U, 4), (F, 12)], I),
    ('w_001EFD20', 0x1EFD20, [(I, 4), (U, 5)], None),
    ('w_001EFD90', 0x1EFD90, [(I, 4), (U, 5), (U, 6)], None),
    ('w_001EFE00', 0x1EFE00, [(I, 4), (U, 5)], None),
    ('w_001EFEB0', 0x1EFEB0, [(I, 4), (U, 5)], None),
    ('w_001EFF10', 0x1EFF10, [(I, 4), (U, 5), (U, 6), (U, 7), (U, 8), (U, 9), (F, 12)], U),
    ('w_001EFFD0', 0x1EFFD0, [(I, 4), (U, 5), (U, 6), (I, 7), (F, 12)], None),
    ('w_001F4A00', 0x1F4A00, [(U, 4), (U, 5)], None),
    ('w_001F4E20', 0x1F4E20, [(U, 4), (U, 5), (F, 12)], None),
    ('w_001F8D30', 0x1F8D30, [(U, 4), (U, 5), (U, 6), (U, 7), (F, 12), (F, 13), (F, 14), (U, 8)], None),
    ('w_001FBD50', 0x1FBD50, [(U, 4), (I, 5), (I, 6), (F, 12)], I),
    ('w_0021BB00', 0x21BB00, [(U, 4)], I),
    ('w_0021BED0', 0x21BED0, [(U, 4)], I),
# END GENERATED HOOKS
]
BY_ADDRESS = {address: (name, args, result) for name, address, args, result in HOOKS}

# Callees that run as original code inside the oracle (writes replayed to
# the native side). Pure helpers only: none of them reaches hardware.
RUN = {0x1000E0, 0x1026A0, 0x1026D0, 0x102718, 0x102738, 0x102760, 0x1028B8, 0x1028D0, 0x102948, 0x102958,
       0x1029C0, 0x102B08, 0x102BB0, 0x102C58, 0x11DE90, 0x11DF78, 0x11E2A8, 0x11E620, 0x11E748, 0x1281C0,
       0x128250, 0x128350, 0x1B1240, 0x1B12B0, 0x1B13F0, 0x1B1470, 0x1B15D0, 0x1C6160}

# Boot jump tables (runtime address of the jump -> (table, words)): 001BF6B0's
# sub-states 0..5, 0012E840's behaviours 0..9, 0012EB60's sub-states 0..7,
# 001B4810's types 0..11.
JUMP_TABLES = {0x1BF9D8: (0x26E260, 6), 0x12E8E0: (0x26D0D0, 10), 0x12EBA4: (0x26D100, 8),
               0x1B4848: (0x26DE40, 12)}
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
SOURCES = ('em_level8_port_lift.c', 'em_level8_port_overlay.c', 'em_level8_port_gap.c', 'em_level8_port_fx.c',
           'em_level8_port_probe.c', 'em_level8_port_creature.c')


def build(source_dir=None):
    """Compile the module (or the copies in `source_dir`, a mutation sweep)
    into a private library under build/level8/port."""
    OUT.mkdir(parents=True, exist_ok=True)
    base = Path(source_dir) if source_dir else ROOT / 'src/game'
    stem = 'level8_port' if not source_dir else 'level8_port_%d' % os.getpid()
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
        0x08, sub 0x01 / 0x09: lane-wise subtract, wrapping), which the
        string compare 00123020 uses on its 16-byte chunks."""
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


NAN, SNAN, NEG_INF, POS_INF, FMAX = 0x7FC00000, 0x7F800001, 0xFF800000, 0x7F800000, 0x7F7FFFFF
F_0_1, F_0_2, F_0_3 = 0x3DCCCCCD, 0x3E4CCCCD, 0x3E99999A



# The default run's share of the designed grids and the random creature states: a
# coverage pass over the whole set (every case's original run) picked these so that the
# default run reaches every word the whole set reaches (EM_TEST_FULL=1 runs all).
GRID = ('target ', 'gap 1BF6B0 step', 'gap 1BE6C0 l', 'gap 1BF6B0 hit', 'creature 21BE40', 'creature 133A20', 'probe 1B2BF0', 'overlay 8239A0', 'lift 1BD9F0', 'gap 1BF5B0', 'gap 12B850', 'overlay 8239', 'overlay 8238', 'overlay 823A', 'lift 1BC740', 'lift 1BC860', 'probe 1B30E0', 'probe 1B32F0', 'probe 1B3440', 'creature 1B4810', 'probe 1A7BA0 random', 'gap 1284E0', 'gap 1BE6C0 state', 'gap 1BF6B0 aim', 'gap 1BF6B0 cooldown', 'gap 1BF6B0 fade')
GRID_KEEP = frozenset([
    'creature 133A20 8003 200 0',
    'creature 133A20 A001 200 0',
    'creature 1B4810 type 10 flags 0',
    'creature 1B4810 type 11 flags 0',
    'creature 1B4810 type 3 flags 0',
    'creature 1B4810 type 4 flags 0',
    'creature 1B4810 type 5 flags 0',
    'creature 1B4810 type 6 flags 0',
    'creature 1B4810 type 6 flags 80',
    'creature 1B4810 type 7 flags 0',
    'creature 1B4810 type 7 flags 80',
    'creature 1B4810 type 9 flags 0',
    'gap 1284E0 B 0 0',
    'gap 12B850 0 h 0 r 0 t 1',
    'gap 12B850 1 h 0 r 0 t 1',
    'gap 1BE6C0 l 1 k 0 hit 1000 p 1 g (0, 0)',
    'gap 1BE6C0 l 1 k 0 hit 20 p 1 g (2, 0)',
    'gap 1BE6C0 state 0 parent 0',
    'gap 1BE6C0 state 0 parent 3',
    'gap 1BE6C0 state 1 parent 2',
    'gap 1BE6C0 state 3 parent 0',
    'gap 1BE6C0 state 4 parent 0',
    'gap 1BF5B0 1000 1 0',
    'gap 1BF6B0 aim 0.8 0',
    'gap 1BF6B0 aim 0.8 1C03000',
    'gap 1BF6B0 fade 0 0.4',
    'gap 1BF6B0 hit 1005 hp 80 g 2',
    'gap 1BF6B0 hit 4020 hp 16 g 1',
    'gap 1BF6B0 step 0 g 0 t 0 f 0 c 0',
    'gap 1BF6B0 step 0 g 0 t 1 f 0 c 0',
    'gap 1BF6B0 step 1 g 0 t 1 f 0 c 3C',
    'gap 1BF6B0 step 2 g 0 t 0 f 1000 c 0',
    'gap 1BF6B0 step 3 g 0 t 0 f 0 c 14',
    'gap 1BF6B0 step 4 g 0 t 0 f 1000 c 0',
    'gap 1BF6B0 step 5 g 0 t 0 f 0 c 1',
    'gap 1BF6B0 step2 count B',
    'lift 1BC740 7B3870 04 0',
    'lift 1BC740 7B3870 04 1',
    'lift 1BD9F0 7ACEB0 state 0 kind 3',
    'lift 1BD9F0 7ACEB0 state 0 kind 4',
    'lift 1BD9F0 7ACEB0 state 2 kind 3',
    'lift 1BD9F0 7ACEB0 state 3 kind 3',
    'lift 1BD9F0 7ACEB0 state 4 kind 3',
    'lift 1BD9F0 7ACEB0 sub 1 r 1 kind 4',
    'lift 1BD9F0 7ACEB0 sub 3 r 0 kind 3',
    'lift 1BD9F0 7ACEB0 sub 4 r 0 kind 3',
    'lift 1BD9F0 7ACEB0 sub 5 r 0 kind 3',
    'lift 1BD9F0 7ACEB0 sub0 1 1 4',
    'lift 1BD9F0 7ACEB0 sub2 1',
    'overlay 823920 state 1 frame 0',
    'overlay 823920 state 4 frame 0',
    'overlay 8239A0 state 0 t 0 r 0',
    'overlay 8239A0 state 1 t 2 r 0',
    'overlay 8239A0 state 2 t 0 r 0',
    'overlay 8239A0 state 3 t 0 r 0',
    'overlay 8239A0 state 4 t 0 r 0',
    'overlay 823A90 state 1 frame 0',
    'overlay 823A90 state 4 frame 0',
    'probe 1B2BF0 1 0 2000 0.5',
    'probe 1B2BF0 1 0 800 0.5',
    'probe 1B30E0 1 4',
    'probe 1B32F0 3 0 0.5',
    'probe 1B3440 3 0 0.5',
    'target 12E3A0 state 4',
    'target 12E560 0 0 0',
    'target 12E560 1 0 0',
    'target 12E560 3 1 0',
    'target 12EB60 pick 3 80 0 0',
    'target 12EB60 pick 3 80 1 0',
    'target 12EB60 sound 1 95',
    'target 12F100 chase 0 10 0 0',
    'target 12F100 chase 1 50 1 0',
    'target 12F100 chase 1 50 1 1',
    'target 12F100 wander 0 40',
    'target 12F100 wander 1 5',
    'target 12FC10 bite 0 0 0',
    'target 12FC10 bite 0 0 1',
    'target 12FC10 bite 0 1 0',
    'target 12FC10 bite 0 1 1',
    'target 12FC10 bite 0 3 0',
    'target 12FC10 bite 0 3 1',
    'target 12FC10 bite 0 80 0',
    'target 12FC10 bite 0 81 0',
    'target 12FC10 bite 0 81 1',
    'target 12FC10 bite 1 0 0',
    'target 12FC10 bite 1 0 1',
    'target 12FC10 bite 1 1 0',
    'target 12FC10 bite 1 1 1',
    'target 12FC10 bite 1 3 0',
    'target 12FC10 bite 1 80 0',
    'target 12FC10 bite 1 81 0',
    'target 12FC10 bite 1 81 1',
    'target 12FC10 spit 0',
    'target 12FC10 spit 1',
    'target 12FC10 spit 2',
    'target 12FC10 spit 80',
    'target 12FC10 spit 81',
    'target 131F90 0 -0.01 0',
    'target 131F90 0 -0.3 0',
    'target 131F90 25 0.01 0',
    'target 131F90 25 0.3 0',
    'target 132490 clip 10',
    'target 132490 clip 11',
    'target 132490 clip 13',
    'target 132490 clip 14',
    'target 132490 clip 17',
    'target 132490 clip 18',
    'target 132490 clip 19',
    'target 132490 clip 1A',
    'target 132490 clip 1B',
    'target 132490 clip 1D',
    'target 132490 clip 20',
    'target 132490 clip 8',
    'target 132490 clip 9',
    'target 132490 clip A',
    'target 132490 clip F',
    'target 1328D0 corners [0, 0, 0, 1]',
    'target 1328D0 ledge 5B',
    'target 132FB0 bits 4',
    'target 132FB0 climb 1 -40 [0, 0]',
    'target 1333F0 count 77',
    'target 1333F0 facing 4 3',
    'target 133640 10 13.1 1 1',
    'target 133640 10 13.1 3 1',
    'target 133640 far 100000 0',
    'target 133640 far 100000 1',
    'target 133640 roll A0 0 1',
    'target 133640 roll A0 0 3',
    'target 1A7BA0 clamp',
    'target 1A7BA0 shifted 4',
    'target 1B30E0 higher 20',
    'target 1B30E0 higher 60',
    'target 1F91C0 widths close',
])
PINNED = {0x12E3A0: (0, 2, 4, 10, 17, 26, 47, 55, 75, 82, 93, 139, 145, 149), 0x12E560: (15, 75, 110, 136, 148, 197), 0x12E840: (7, 32, 37, 43, 77, 102, 111, 155, 157, 159, 188, 214, 216, 221, 241, 273, 279, 284), 0x12EB60: (1, 29, 50, 85, 103, 104, 128, 130, 158, 271, 352, 378), 0x12F100: (3, 28, 57, 202, 247), 0x12FC10: (218,), 0x131F90: (75,), 0x1328D0: (67,), 0x133A20: (102,), 0x133DB0: (11,)}

FREE = 0x1C00000          # RAM zero in every capture: the designed records live here
FULL_ONLY = set()         # labels of the cases EM_TEST_FULL=1 adds
A04_LIFT, A04_BUTTON53, A04_BUTTON54, A04_LIFT56 = 0x7B3290, 0x7B3870, 0x7B3B60, 0x7B4140
A04_NPC = 0x7AA2A0
A13_DOOR, A13_MOVE_A, A13_MOVE_B = 0x7ACBC0, 0x7ACEB0, 0x7AD1A0   # a04b_04 (AREA13): 001BDE60, 001BD9F0 x2
SPARKS = 0x7BEE90         # a04b_03: the first of the 16 0x8239A0 nodes
FRAME = 0x70003B68
GAP, GAP_MATE, GAP_FFD0 = 0x7AAB70, 0x7A6AD0, 0x7A6200   # AREA01 g[36] (001BF6B0), its 001BE6C0 / 001BFFD0 nodes
BUG = 0x7B8AB0            # a01v_01: a 0012A5D0 bug (+5 = 13)
COUNT = 0x700031F4        # the live bug count


def fw(*values):
    """Little-endian words (floats given as Python floats, ints as ints)."""
    return b''.join(struct.pack('<f', v) if isinstance(v, float) else struct.pack('<I', v & MASK) for v in values)


def owner_nodes(ram, callback):
    return owners(ram, callback)


def case_list(elf):
    rng = random.Random(0x1E8)
    cases = []

    def add(label, beat, entry, args, patches=(), spad=(), queues=None, default=None, run_set=RUN, full=False):
        """full=True: the case runs only with EM_TEST_FULL=1, and so does a case
        of a designed grid (GRID); the coverage pass's picks (GRID_KEEP) always
        run."""
        if label not in GRID_KEEP and (full or label.startswith(GRID)):
            FULL_ONLY.add(label)
        args = [STACK_TOP if k == S else a for k, a in zip(FUNCS[entry][2], list(args) + [STACK_TOP] * 4)]
        cases.append((label, beat, entry, tuple(args), list(patches), list(spad),
                      Script(queues, default), frozenset(run_set)))

    for beat in BEATS:
        ram, spad = CAPTURES[beat]
        for callback, entry in ((0x1BD9F0, 0x1BD9F0), (0x1BDE60, 0x1BDE60), (0x8239A0, 0x8239A0),
                                (0x1BF6B0, 0x1BF6B0), (0x1BE6C0, 0x1BE6C0)):
            for node in owners(ram, callback):
                add('capture %s %06X @%X' % (beat, entry, node), beat, entry, [node])
        for node in owners(ram, 0x1BD560):
            add('capture %s 1BD180 @%X' % (beat, node), beat, 0x1BD180, [node + 0x1F0])
            add('capture %s 1BD270 @%X' % (beat, node), beat, 0x1BD270, [node + 0x1F0])
            add('capture %s 1BBD20 @%X' % (beat, node), beat, 0x1BBD20, [node, 0])
        for node in owners(ram, 0x1BC960):
            add('capture %s 1BC740 @%X' % (beat, node), beat, 0x1BC740, [node, node + 0x1F0])
            add('capture %s 1BC860 @%X' % (beat, node), beat, 0x1BC860, [node, node + 0x1F0])
        for node in owners(ram, 0x824320):
            add('capture %s 8245F0 @%X' % (beat, node), beat, 0x8245F0, [node])
        add('capture %s 1BC560' % beat, beat, 0x1BC560, [])
        if RESIDENT[beat] == 2:
            for node in owners(ram, 0x12A5D0):
                add('capture %s 12B850 @%X' % (beat, node), beat, 0x12B850, [node, node + 0x1F0])
    captured = len(cases)
    targeted_cases(add)
    targeted = len(cases) - captured
    perturbed_cases(add, rng)
    return cases, captured, targeted


def perturbed_cases(add, rng):
    """EM_TEST_FULL=1: seeded random states, counters, floats and results."""
    for k in range(reference_mode.pick(0, 30)):
        state, sub = rng.choice((0, 1, 1, 1, 2, 3, 5)), rng.randrange(7)
        node = rng.choice((A13_MOVE_A, A13_MOVE_B))
        add('perturbed %d 1BD9F0' % k, 'a04b_04_lift', 0x1BD9F0, [node],
            [(node + 4, bytes([state, sub])), (node + 3, b8(rng.choice((3, 4, 5)))),
             (node + 0x0B, b8(rng.randrange(256))), (node + 0x28, h16(rng.choice((0, 1, 2, 0x8000, 0xFFFF)))),
             (node + 0x2E, h16(rng.randrange(3))), (0x810774, b8(rng.randrange(3))),
             (0x8106B8, b8(rng.randrange(2)))],
            default={0x1BA1F0: rng.randrange(2), 0x1B0FD0: rng.randrange(2)})
        state, sub = rng.choice((0, 1, 1, 1, 2, 3, 6)), rng.randrange(4)
        add('perturbed %d 1BDE60' % k, 'a04b_04_lift', 0x1BDE60, [A13_DOOR],
            [(A13_DOOR + 4, bytes([state, sub])), (A13_DOOR + 0x0B, b8(rng.randrange(3))),
             (A13_DOOR + 0x56, h16(rng.randrange(0x10000)))],
            default={0x1BDCA0: rng.randrange(2), 0x1BDD70: rng.randrange(2), 0x1FBD50: rng.randrange(-2, 3)})
        rec = A04_LIFT + 0x1F0
        a, b = rng.uniform(-1.0, 20.0), rng.uniform(-1.0, 20.0)
        add('perturbed %d 1BD180' % k, 'a04b_03_reader', 0x1BD180, [rec], [(rec + 0x10, fw(a, b))])
        add('perturbed %d 1BD270' % k, 'a04b_03_reader', 0x1BD270, [rec], [(rec + 0x10, fw(a, b))])


def targeted_cases(add):
    lift_cases(add)
    overlay_cases(add)
    gap_cases(add)
    creature_cases(add)
    targeted_creature_cases(add)
    survivor_cases(add)
    probe_cases(add)
    fx_cases(add)


def gap_cases(add):
    beat = 'a01v_01_gap_jump'
    node, sub = GAP, GAP + 0x1F0
    spawn = {0x1AFA90: [FREE + 0x1000, FREE + 0x2000]}
    # 001BF6B0 state 0: both spawns, either failing
    for q in ([FREE + 0x1000, FREE + 0x2000], [0], [FREE + 0x1000, 0]):
        add('gap 1BF6B0 init %d' % len(q), beat, 0x1BF6B0, [node], [(node + 4, b8(0))], queues={0x1AFA90: list(q)})
    for state in (2, 3, 4, 0xFF):
        for result in (0, 1):
            add('gap 1BF6B0 state %d r %d' % (state, result), beat, 0x1BF6B0, [node], [(node + 4, b8(state))],
                default={0x1C1570: result})
    for alpha in (0.01, 0.0100001, 0.02, 0.0, 1.0):
        for tint in (0.4, 0.39, 0.5, 0.0):
            add('gap 1BF6B0 fade %g %g' % (alpha, tint), beat, 0x1BF6B0, [node],
                [(node + 4, b8(2)), (node + 0x64, fw(alpha)), (node + 0x38, fw(tint))])
    # state 1: the room test, every sub-state and its tests
    add('gap 1BF6B0 away', beat, 0x1BF6B0, [node], [(node + 4, bytes([1, 1]))], default={0x1B2140: 0})
    for step in range(8):
        for gate in (0, 1):
            for track in (0, 1):
                for flag in (0, 0x1000):
                    for timer in (0, 0x14, 0x3C, 1):
                        add('gap 1BF6B0 step %d g %d t %d f %X c %X' % (step, gate, track, flag, timer), beat, 0x1BF6B0,
                            [node], [(node + 4, bytes([1, step])), (node + 0x28, h16(timer)), (sub, h16(flag | 0x20))],
                            [(0x70003B64, w32(0x100 * 7 + (0 if not gate else 3))), (0x70003B8A, h16(4))],
                            default={0x1B2140: 1, 0x1BF630: track, 0x1AFA90: FREE + 0x3000, 0x122BB8: 0x12345678,
                                     0x1D0D60: 0x1234}, full=timer == 1 and step not in (3, 5))
    for count in (10, 11, -1, 0x7FFFFFFF):
        add('gap 1BF6B0 step2 count %X' % (count & MASK), beat, 0x1BF6B0, [node],
            [(node + 4, bytes([1, 2])), (sub, h16(0x1000))], [(COUNT, w32(count))], default={0x1B2140: 1})
    for dot in (0.8, 0.79999995, 0.0, -1.0, 1.0):
        for result in (0, FREE + 0x3000):
            add('gap 1BF6B0 aim %g %X' % (dot, result), beat, 0x1BF6B0, [node],
                [(node + 4, bytes([1, 3])), (node + 0x28, h16(0x14)), (sub + 8, fw(dot)),
                 (sub + 0xC, fw(0.3, -0.5, 0.7))], [(COUNT, w32(3))],
                default={0x1B2140: 1, 0x1AFA90: result})
    for r in (0, 0x7FFFFFFF, -0x80000000, -1, 0x10000, 0x5A5A5A5A):
        add('gap 1BF6B0 cooldown %X' % (r & MASK), beat, 0x1BF6B0, [node],
            [(node + 4, bytes([1, 4])), (sub, h16(0x1000))], default={0x1B2140: 1, 0x122BB8: r})
    for t in (1, 2, 0, 0x8000):
        add('gap 1BF6B0 step5 %X' % t, beat, 0x1BF6B0, [node], [(node + 4, bytes([1, 5])), (node + 0x28, h16(t))],
            default={0x1B2140: 1})
    # the hit and the hit points
    for hit in (0, 0x0010, 0x1005, 0x4020, 0x5000, 0x0FFF, 0x8001, 0xFFFF):
        for hp in (0x50, 0x10, 1, 0, -5):
            for guard in (0, 1, 2):
                add('gap 1BF6B0 hit %X hp %d g %d' % (hit, hp, guard), beat, 0x1BF6B0, [node],
                    [(node + 4, bytes([1, 0xFF])), (node + 0x36, h16(hit)), (node + 0x34, h16(hp)),
                     (sub + 0x1C, w32(guard))], default={0x1B2140: 1},
                    full=hp not in (0x10, 0) and guard == 2)
    # 001BE6C0: the companion, every state, parent states, the hit, +0x0D
    mate = GAP_MATE
    for state in (0, 1, 2, 3, 4):
        for pstate in (0, 1, 2, 3):
            add('gap 1BE6C0 state %d parent %d' % (state, pstate), beat, 0x1BE6C0, [mate],
                [(mate + 4, b8(state)), (GAP + 4, b8(pstate))])
    for alpha in (0.05, 0.0500001, 0.1, 0.0, -1.0):
        add('gap 1BE6C0 fade %g' % alpha, beat, 0x1BE6C0, [mate], [(mate + 4, b8(2)), (mate + 0x64, fw(alpha))])
    for live in (0, 1):
        for kind in (0, 1, 2):
            for hit in (0, 0x0020, 0x1000, 0x4000, 0x5001):
                for p0 in (1, 2):
                    for guards in ((0, 0), (2, 0), (0, 5), (1, 1)):
                        add('gap 1BE6C0 l %d k %d hit %X p %d g %s' % (live, kind, hit, p0, guards), beat, 0x1BE6C0,
                            [mate], [(mate + 4, b8(1)), (GAP + 4, b8(1)), (GAP + 1, b8(live)), (mate + 0x0D, b8(kind)),
                                     (mate + 0x36, h16(hit)), (GAP, b8(p0)), (mate + 0x20C, w32(guards[0])),
                                     (GAP + 0x20C, w32(guards[1]))],
                            full=live == 0 or (kind == 2 and hit not in (0, 0x5001)))
    # 001BEAC0, 001284E0: the spawns
    for result in (0, FREE + 0x1000):
        for a2, a3 in ((0x64, 1), (-1, 0x1FF), (0x12345, -7)):
            add('gap 1BEAC0 %X %d %d' % (result, a2, a3), beat, 0x1BEAC0, [node, 0x700038A0, a2, a3],
                default={0x1AFA90: result})
    for count in (10, 11, 0, -1, 0x7FFFFFFF, -0x80000000):
        for result in (0, FREE + 0x1000):
            for kind in (0, 1, 2, -1):
                add('gap 1284E0 %X %X %d' % (count & MASK, result, kind), beat, 0x1284E0,
                    [node, node + 0xB0, kind, node + 0xC0], [], [(COUNT, w32(count))], default={0x1AFA90: result},
                    full=kind == 2 or (count & MASK) in (0, 0x80000000))
    # 001BF5B0, 001BFF90
    for flags in (0, 0x1000, 0xF000, 0xEFFF):
        for now in (0, 1, 2, 0xFFFF):
            for mode in (0, 1, 2, 0x10000, -1, 0x10001):
                add('gap 1BF5B0 %X %X %X' % (flags, now, mode & MASK), beat, 0x1BF5B0, [node, sub, mode],
                    [(sub, h16(flags)), (sub + 2, h16(now))], full=flags in (0, 0xEFFF))
    for now in (0, 1, 5, 0xFFFF):
        for clip in (0, 1, 5, 0x10005, -1, 0x7FFF):
            add('gap 1BFF90 %X %X' % (now, clip & MASK), beat, 0x1BFF90, [node, sub, clip], [(sub + 2, h16(now))])
    # 0012B850: the bug's grab state
    bug, bsub = BUG, BUG + 0x1F0
    for step in (0, 1, 2, 3):
        for held in (0, 1):
            for r in (0, 1):
                for t in (1, 2, 0, 0x8000):
                    add('gap 12B850 %d h %d r %d t %X' % (step, held, r, t), beat, 0x12B850, [bug, bsub],
                        [(bug + 6, b8(step)), (bsub + 0xD0, h16(t))], default={0x1C2770: held, 0x128640: r},
                        full=t == 0x8000 and step != 1)


def lift_cases(add):
    beat = 'a04b_03_reader'
    rec = A04_LIFT + 0x1F0
    # 001BD180 / 001BD270: the two counters around every bound
    for a in (0.0, 5.7, 5.8, 5.8000001, 6.0, 13.8, 13.9, 14.0, 20.0, -1.0):
        for b in (0.0, 8.0, 8.2, 15.8, 15.9, 16.0, 16.2, -3.0):
            add('lift 1BD180 %g %g' % (a, b), beat, 0x1BD180, [rec], [(rec + 0x10, fw(a, b))])
            add('lift 1BD270 %g %g' % (a, b), beat, 0x1BD270, [rec], [(rec + 0x10, fw(b, a))])
    for bits_a in (fbits(5.8) - 1, fbits(5.8) + 1, 0x80000000, 0x00000001):
        add('lift 1BD180 bits %08X' % bits_a, beat, 0x1BD180, [rec], [(rec + 0x10, fw(bits_a, 0))])
        add('lift 1BD270 bits %08X' % bits_a, beat, 0x1BD270, [rec], [(rec + 0x10, fw(bits_a, bits_a))])
    add('lift 1BD270 0.2', beat, 0x1BD270, [rec], [(rec + 0x10, fw(0.2, 0.2))])
    add('lift 1BD270 -0', beat, 0x1BD270, [rec], [(rec + 0x10, fw(0x80000000, 0.2))])
    # 001BBD20: the sound row by the high byte of +0x56 and the index
    for hw in (0x0000, 0x0B00, 0x0D7F, 0x7FFF, 0x8000, 0xFFFF, 0x2A55):
        for index in (0, 1, 3):
            add('lift 1BBD20 %04X %d' % (hw, index), beat, 0x1BBD20, [A04_LIFT, index],
                [(A04_LIFT + 0x56, h16(hw))], default={0x1FBD50: -1})
    add('lift 1BBD20 result', beat, 0x1BBD20, [A04_LIFT, 1], default={0x1FBD50: 0x1234})
    # 001BC740 / 001BC860: the Use bit, +3 zero or not
    for node in (A04_BUTTON53, A04_BUTTON54):
        for use in (0, 4, 0xFB, 0xFF):
            for kind in (0, 1, 7):
                patches = [(node + 0x0B, b8(use)), (node + 3, b8(kind))]
                add('lift 1BC740 %X %02X %d' % (node, use, kind), beat, 0x1BC740, [node, node + 0x1F0], patches)
                add('lift 1BC860 %X %02X %d' % (node, use, kind), beat, 0x1BC860, [node, node + 0x1F0], patches)
    add('lift 1BC740 yaw', beat, 0x1BC740, [A04_BUTTON54, A04_BUTTON54 + 0x1F0],
        [(A04_BUTTON54 + 0x0B, b8(4)), (A04_BUTTON54 + 0xC4, fw(3.0))])
    # 001BC6D0: a designed chain self -> +0x18 -> +0x18 -> +0x18
    chain = [(FREE + 0x18, w32(FREE + 0x100)), (FREE + 0x118, w32(FREE + 0x200)), (FREE + 0x218, w32(FREE + 0x300))]
    st = FREE + 0x400
    for step in (0, 1, 2, 0xFF):
        for flag in (0, 1, 2, 3, 0x83):
            add('lift 1BC6D0 %d %d' % (step, flag), beat, 0x1BC6D0, [FREE, st],
                chain + [(st + 4, b8(step)), (FREE + 0x30B, b8(flag))])
    # 001BC560: the camera step
    for beat_ in ('a04b_03_reader', 'a04b_04_lift'):
        add('lift 1BC560 %s' % beat_, beat_, 0x1BC560, [], default={0x18D7B0: 0})
    # 001BD9F0: every state and sub-state on both AREA13 room moves
    beat = 'a04b_04_lift'
    for node in (A13_MOVE_A, A13_MOVE_B):
        for state in (0, 1, 2, 3, 4):
            for kind in (3, 4):
                add('lift 1BD9F0 %X state %d kind %d' % (node, state, kind), beat, 0x1BD9F0, [node],
                    [(node + 4, b8(state)), (node + 3, b8(kind))])
        for sub in range(6):
            for result in (0, 1):
                for kind in (3, 4):
                    base = [(node + 4, bytes([1, sub])), (node + 3, b8(kind))]
                    add('lift 1BD9F0 %X sub %d r %d kind %d' % (node, sub, result, kind), beat, 0x1BD9F0, [node],
                        base, default={0x1BA1F0: result})
        # sub 0: the halfword +0x2E, D_00810774, the Use bit
        for hw in (0, 1, 2, 0x101):
            for flag in (0, 1, 2):
                for use in (0, 4):
                    add('lift 1BD9F0 %X sub0 %X %d %d' % (node, hw, flag, use), beat, 0x1BD9F0, [node],
                        [(node + 4, bytes([1, 0])), (node + 0x2E, h16(hw)), (0x810774, b8(flag)),
                         (node + 0x0B, b8(use))])
        # sub 2: the countdown
        for t in (0, 1, 2, 0x8000, 0xFFFF, 0x10001 & 0xFFFF):
            add('lift 1BD9F0 %X sub2 %X' % (node, t), beat, 0x1BD9F0, [node],
                [(node + 4, bytes([1, 2])), (node + 0x28, h16(t))])
        # sub 4: D_008106B8
        for v in (0, 1, 2, 0x80):
            add('lift 1BD9F0 %X sub4 %X' % (node, v), beat, 0x1BD9F0, [node],
                [(node + 4, bytes([1, 4])), (0x8106B8, b8(v))])
        # state 0: +0x2E byte and +3
        for byte, kind in ((0x85, 3), (0x7F, 4), (0, 3)):
            add('lift 1BD9F0 %X init %X %d' % (node, byte, kind), beat, 0x1BD9F0, [node],
                [(node + 4, b8(0)), (node + 0x2E, h16(byte | 0x5500)), (node + 3, b8(kind))])
    # 001BDE60: every state and sub-state, the callee results
    node = A13_DOOR
    for state in (0, 2, 3, 4):
        add('lift 1BDE60 state %d' % state, beat, 0x1BDE60, [node], [(node + 4, b8(state))])
    for sub in range(5):
        for result in (0, 1, 2):
            for flag in (0, 1):
                add('lift 1BDE60 sub %d r %d f %d' % (sub, result, flag), beat, 0x1BDE60, [node],
                    [(node + 4, bytes([1, sub])), (node + 0x0B, b8(flag))],
                    default={0x1BDCA0: result, 0x1BDD70: result})
    add('lift 1BDE60 init', beat, 0x1BDE60, [node], [(node + 4, b8(0)), (node + 0x2E, h16(0xAA93))])


def overlay_cases(add):
    beat = 'a04b_03_reader'
    node = SPARKS
    for callback in (0x823920, 0x823A90):
        for state in (0, 1, 2, 3, 4):
            for frame in (0, 40, 50, 200, 199, 1, -40, -50, -1, 0x7FFFFFF8, -0x80000000, 1000, 2000):
                add('overlay %06X state %d frame %d' % (callback, state, frame), beat, callback, [node],
                    [(node + 4, b8(state))], [(FRAME, w32(frame))])
    for state in (0, 1, 2, 3, 4):
        for t in (0.0, 1.98, 1.99, 1.9900001, 2.0, 5.0):
            for r in (0, 1, 0x7FFFFFFF, -0x80000000, 0x40000000, -5):
                if state != 0 and r:
                    continue
                add('overlay 8239A0 state %d t %g r %X' % (state, t, r & MASK), beat, 0x8239A0, [node],
                    [(node + 4, b8(state)), (node + 0x1F0, fw(t, 0.25))], default={0x122BB8: r})
    npc = A04_NPC
    for sub in (0, 1, 2, 5):
        for use in (0, 4, 0xFB):
            for result in (0, 1):
                add('overlay 8245F0 sub %d use %X r %d' % (sub, use, result), beat, 0x8245F0, [npc],
                    [(npc + 5, b8(sub)), (npc + 0x0B, b8(use)), (npc + 0x0D, b8(0x93))],
                    default={0x1BA1F0: result})


CR = 0x7BF180             # a04b_03: the creature (behaviour 0012E3A0)
CENT = CR + 0x1F0
CBEAT = 'a04b_03_reader'
MODE = 0x70003B8D


def scribble_float(value):
    """A stub that writes a float to the address in a0 (0019A310's out)."""
    def make(ee, value=value):
        return Scribble(1, [(ee.r[4] & MASK, f32b(value))])
    return make


def scribble_at(result, address, value):
    def make(ee, result=result, address=address, value=value):
        return Scribble(result, [(address, f32b(value))])
    return make


def creature_state(rng):
    """A random creature state (bytes and floats around every test)."""
    pick = rng.choice
    p = [(CR, b8(pick((1, 2, 3)))), (CR + 4, b8(pick((0, 1, 1, 1, 2, 3)))), (CR + 5, b8(pick(range(11)))),
         (CR + 6, b8(pick(range(9)))), (CR + 0x0A, b8(pick((0, 1)))),
         (CR + 0x0D, b8(pick((0, 1, 2, 3, 0x80, 0x81, 0x82, 0x83)))),
         (CR + 0x2C, h16(pick((1, 2, 3, 4, 7, 0x12, 0x28, 0x1C, 0x8001, 0x0E, 0x21)))),
         (CR + 0x34, h16(pick((0, 5, 200, 1)))), (CR + 0x36, h16(pick((0, 0, 0x10, 0x1005, 0x4020, 0x5000, 0x8003, 0x2010)))),
         (CR + 0x3C, fw(pick((0.0, 22.0, 24.0, 35.0, 43.0, 46.0, 95.0, 3.5)))),
         (CR + 0x54, h16(pick((0, 1)))), (CR + 0x56, h16(pick((0, 5, 0)))), (CR + 0x5D, b8(0)),
         (CENT + 0x30, fw(pick((0.0, 1.0, -2.5, 3.1)))), (CENT + 0x34, fw(pick((1.0, 2.0, 0.5)))),
         (CENT + 0x38, fw(pick((-1.0, -0.2, 0.0, 0.5, -0.05)))), (CENT + 0x3C, fw(pick((0.0, -0.5, 0.07, 1.2)))),
         (CENT + 0x40, fw(12.5)), (CENT + 0x44, fw(pick((5.0, 11.5, 22.0, 60.0, 70.0)))),
         (CENT + 0x50, h16(pick((0, 1, 2)))), (CENT + 0x54, h16(pick((0, 1)))), (CENT + 0x56, h16(pick((0, 1, 5)))),
         (CENT + 0x58, h16(pick((0, 0x1000, 0x5000, 0x8000, 0x3009, 0x1009)))), (CENT + 0x5A, h16(pick((0, 1)))),
         (CENT + 0x5C, h16(pick((0, 1))))]
    for off in range(0x60, 0x70):
        p.append((CENT + off, b8(pick((0, 0, 1, 2, 3)))))
    p += [(0x81070A, b8(pick((0, 1)))), (0x810808, b8(pick((0, 0xFF))))]
    if rng.random() < 0.7:   # the creature placed around the player (the RUN helpers' distances and bearings)
        dist = pick((3.0, 8.0, 12.0, 14.9, 15.1, 20.0, 39.0, 41.0, 49.0, 55.0, 70.0))
        ang = rng.uniform(-3.14159, 3.14159)
        p.append((CR + 0xB0, fw(545.0 + dist * math.sin(ang), 54.9 + pick((0.0, 0.0, 5.0, -12.0, 14.0)),
                                282.2 + dist * math.cos(ang), 1.0)))
        yaw = ang + math.pi + pick((0.0, 0.0, 0.05, 0.5, 1.2, -1.2, 3.0))
        p.append((CR + 0xC4, fw(yaw)))
        p.append((CENT + 0x30, fw(pick((yaw, ang + math.pi, -ang, 0.3)))))
    p.append((CENT + 0x68, b8(pick((0, 1)))))
    p.append((CENT + 0x62, b8(pick((0, 0, 1, 3)))))
    p.append((CENT, fw(545.0 + rng.uniform(-40, 40), 55.0, 282.0 + rng.uniform(-40, 40), 1.0)))
    spad = [(MODE, b8(pick((0, 0, 0, 1, 2, 3, 4, 5)))), (0x700031F4, w32(pick((0, 5, 11))))]
    return p, spad


def creature_queues(rng):
    pick = rng.choice
    q = {}
    for address in (0x1B2140, 0x1B10B0, 0x1B3250, 0x1B3390, 0x19A570, 0x19AB20, 0x19AD00, 0x19B6C0, 0x1B2E50,
                    0x21BB00, 0x21BED0, 0x1B1EA0, 0x1B3F10):
        q[address] = [pick((0, 1, 1, 2, 3, 4, 5, 7)) for _ in range(12)]
    q[0x122BB8] = [rng.randrange(-0x80000000, 0x80000000) for _ in range(12)]
    q[0x1C64F0] = [pick((0, 9, 0x19, 0x1000, 0x5009, 0x3000)) for _ in range(4)]
    q[0x1B37D0] = [fbits(pick((0.5, -1.0, 2.0))) for _ in range(4)]
    q[0x19A310] = [scribble_float(pick((2.0, 0.5, 0.7853982, 0.78539824))) for _ in range(8)]
    q[0x1B2F70] = [scribble_at(pick((0, 1)), 0x700038A0, pick((-50.0, 0.0, 40.0))) for _ in range(4)]
    return q


RANDOM_COUNTS = {0x12E3A0: 200, 0x12E840: 300, 0x12EB60: 400, 0x12F100: 400, 0x12FC10: 400, 0x131F90: 300,
                 0x132490: 200, 0x1328D0: 200, 0x132FB0: 300, 0x1333F0: 200, 0x133640: 400, 0x133A20: 200,
                 0x133DB0: 200, 0x12E560: 200}
def add_random(add, entry, k):
    rng = random.Random((entry << 12) ^ (k * 0x9E3779B1) ^ 0xC8EA)
    patches, spad = creature_state(rng)
    queues = creature_queues(rng)
    args = [CR] if entry == 0x12E3A0 else [CR, CENT]
    add('creature random %06X %d' % (entry, k), CBEAT, entry, args, patches, spad, queues=queues,
        full=k not in PINNED.get(entry, ()))


def creature_cases(add):
    rng = random.Random(0xC8EA)
    beat = CBEAT
    # the captured state of every entry that takes the creature
    for entry in (0x12E3A0, 0x12E840, 0x12EB60, 0x12F100, 0x12FC10, 0x131F90, 0x132490, 0x1328D0, 0x132FB0,
                  0x1333F0, 0x133640, 0x133A20, 0x133DB0, 0x131ED0, 0x21BE40, 0x1B4810, 0x1A7B80, 0x12E560):
        args = [CR] if FUNCS[entry][2][:2] != [U, U] else [CR, CENT]
        add('creature capture %06X' % entry, beat, entry, args)
    add('creature capture 1F91C0', beat, 0x1F91C0, [CR])
    # random states for every state function (the default run keeps a share)
    for entry, n in RANDOM_COUNTS.items():
        for k in range(n):
            if not reference_mode.FULL and k not in PINNED.get(entry, ()):
                continue
            add_random(add, entry, k)
    # 0011E0A8 (modf): every exponent class and sign
    for x in (0.0, -0.0, 0.5, -0.75, 1.0, 1.5, -2.25, 3.0, 1e-40, 8388607.5, 8388608.0, 16777217.0, 1e30, -1e30,
              123.456, -0.000001, 65535.99):
        add('creature 11E0A8 %r' % x, beat, 0x11E0A8, [FREE, fbits(x)])
    for bits_ in (0x7F800000, 0xFF800001, 0x3F800001, 0x4B000001, 0x4AFFFFFF, 0x00000001, 0x80000000):
        add('creature 11E0A8 bits %08X' % bits_, beat, 0x11E0A8, [FREE, bits_])
    # 0021BE40: every exit
    for mode in (0, 1):
        for hp in (90.0, 0.0, -1.0, 1e-38):
            for st in ((1, 1), (2, 1), (1, 0)):
                for r in (0, 1):
                    for held in (0, 1, -1):
                        add('creature 21BE40 %d %g %s %d %d' % (mode, hp, st, r, held), beat, 0x21BE40, [PLAYER],
                            [(PLAYER + 0x220, fw(hp)), (PLAYER, b8(st[0])), (PLAYER + 4, b8(st[1])),
                             (PLAYER + 0x20E, h16(held))], [(MODE, b8(mode))], default={0x21BB00: r},
                            full=mode == 1 or hp in (-1.0, 1e-38))
    # 001B4810: every defined type with +0x0D bits 0 / 7
    pose = u32(CAPTURES[beat][0], 0x275B40)
    bone_of = {1: 4, 3: 2, 4: 2, 5: 0x0D, 6: 5, 7: 0x0B, 9: 0x1C, 10: 0x10, 11: 2}
    for type_ in (1, 3, 4, 5, 6, 7, 9, 10, 11):
        for flags in (0, 1, 0x80, 0x81):
            add('creature 1B4810 type %d flags %X' % (type_, flags), beat, 0x1B4810, [CR],
                [(CR + 3, b8(type_)), (CR + 0x0D, b8(flags)), (pose + 4 * bone_of[type_], w32(FREE + 0x1000)),
                 (FREE + 0x1090, fw(1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 5.0, 6.0, 7.0, 1.0))])
    # 00133A20: the hit bits, hit points, the accumulator and the burst test
    for hit in (0x0010, 0x0FFF, 0x8003, 0x2010, 0x4020, 0x5000, 0x1005, 0x8FFF, 0xA001):
        for hp in (200, 16, 15, 1, 0):
            for acc in (0, 24, 25):
                add('creature 133A20 %X %d %d' % (hit, hp, acc), beat, 0x133A20, [CR, CENT],
                    [(CR + 0x36, h16(hit)), (CR + 0x34, h16(hp)), (CENT + 0x67, b8(acc)), (CENT + 0x66, b8(acc & 1)),
                     (CENT + 0x61, b8(acc == 24)), (CR + 0x2C, h16(4)), (CENT + 0x60, b8(2 if hp == 1 else 0))],
                    full=acc == 24 or hp in (16, 0))


POLY = FREE + 0x800       # a designed hit polygon (0x700031D0 points here)


def probe_cases(add):
    rng = random.Random(0x1A7B)
    beat = CBEAT
    # 001A7BA0: the creature against the player, against itself (boxes overlap)
    for m1, m2 in ((0x10, 0x20), (0x30, 0x30), (0x20, 0x10), (0, 0x30), (0x30, 0)):
        add('probe 1A7BA0 player %X %X' % (m1, m2), beat, 0x1A7BA0, [CR, PLAYER, m1, m2])
        add('probe 1A7BA0 self %X %X' % (m1, m2), beat, 0x1A7BA0, [CR, CR, m1, m2])
        add('probe 1A7BA0 player-self %X %X' % (m1, m2), beat, 0x1A7BA0, [PLAYER, PLAYER, m1, m2])
    add('probe 1A7B80', beat, 0x1A7B80, [CR])
    add('probe 1A7BA0 no list a', beat, 0x1A7BA0, [CR, PLAYER, 0x30, 0x30], [(CR + 0x58, w32(0))])
    add('probe 1A7BA0 no list b', beat, 0x1A7BA0, [CR, PLAYER, 0x30, 0x30], [(PLAYER + 0x58, w32(0))])
    # the player moved onto the creature (both at the same place) and nearby
    for k in range(12):
        add('probe 1A7BA0 random %d' % k, beat, 0x1A7BA0,
            [rng.choice((CR, PLAYER)), rng.choice((CR, PLAYER)), rng.choice((0x10, 0x20, 0x30)),
             rng.choice((0x10, 0x20, 0x30))], full=k >= 4)
    # 001B1560: the facing test at the limit's bounds
    for limit in (0.0, 0.1, 0.7853982, 3.2, -1.0):
        for yaw in (0.0, 1.0, -2.0, 3.1415927):
            add('probe 1B1560 %g %g' % (limit, yaw), beat, 0x1B1560, [CR, 0x810360, fbits(limit)],
                [(CR + 0xC4, fw(yaw))], full=limit in (0.1, -1.0))
    # 001B2B10, 001B3580
    for yaw in (0.0, 1.5, -3.0):
        add('probe 1B2B10 %g' % yaw, beat, 0x1B2B10, [CR, 0x700038A0, 0x810360], [(CR + 0xC4, fw(yaw))])
        for r in (0, 0x7FFFFFFF, -0x80000000, 0x12345678):
            add('probe 1B3580 %g %X' % (yaw, r & MASK), beat, 0x1B3580, [CR, 0x810360], [(CR + 0xC4, fw(yaw))],
                default={0x122BB8: r})
    add('probe 1B3580 far', beat, 0x1B3580, [CR, FREE], [(FREE, fw(-3000.0, 5.0, 4000.0))])
    # 001B30E0: designed hit lists
    for n in range(0, 5):
        for k in range(6):
            flags = [rng.choice((0, 1, 1, 3)) for _ in range(n)]
            slopes = [rng.choice((0.0, 0.5, -1.0, 1.0471976, 2.0)) for _ in range(n)]
            heights = [rng.choice((-5.0, 0.0, 10.0, 55.0, 60.0)) for _ in range(n)]
            spad = [(0x700031E0, w32(n))]
            for i in range(n):
                spad += [(0x70003170 + 2 * i, h16(flags[i])), (0x700030F0 + 4 * i, fw(heights[i]))]
            patches = [(0x282250 + 4 * i, fw(slopes[i])) for i in range(n)]
            y = rng.choice((0.0, 20.0, 57.5))
            add('probe 1B30E0 %d %d' % (n, k), beat, 0x1B30E0, [FREE + 0x100, FREE + 0x200],
                patches + [(FREE + 0x100, fw(1.0, y, 2.0)), (FREE + 0x200, fw(rng.choice((0.0, 30.0))))], spad,
                full=k >= 3)
    # 001B2BF0: every hit class
    for hit in (0, 1, 2, 3, 4, 6):
        for d4 in (0, 1):
            for attr in (0, 0x0800, 0x2000, 0x1000, 0x2001):
                for local in (0.5, 0.7853982, 2.0):
                    add('probe 1B2BF0 %d %d %X %g' % (hit, d4, attr, local), beat, 0x1B2BF0,
                        [CR, 0x700038A0, 0x700038D0, fbits(0.7853982)], [(POLY + 0x1A, h16(attr))],
                        [(0x700031D4, w32(d4)), (0x700031D0, w32(POLY)), (0x700031E0, w32(0))],
                        queues={0x19AD00: [hit], 0x19A310: [scribble_float(local)], 0x1B2E50: [5]},
                        full=local == 2.0 or attr == 0x1000)
    # 001B32F0 / 001B3440
    for hit in (0, 3):
        for got in (0, 1):
            for local in (0.5, 0.7853982, 0.78539824, 2.0):
                q = {0x19AD00: [hit], 0x19AB20: [hit], 0x19A310: [lambda ee, got=got, local=local:
                                                                 Scribble(got, [(ee.r[4] & MASK, f32b(local))])]}
                add('probe 1B32F0 %d %d %g' % (hit, got, local), beat, 0x1B32F0,
                    [CR, 0x700038A0, fbits(0.7853982)], queues=q)
                add('probe 1B3440 %d %d %g' % (hit, got, local), beat, 0x1B3440,
                    [CR, 0x700038A0, 0x700038B0, fbits(0.7853982)], queues=dict(q))
    # 001B55E0: the three classes
    for kind in (0, 1, 2, 0x100, 0x101):
        for r in (0, -1, 0x7FFFFFFF):
            add('probe 1B55E0 %X %X' % (kind, r & MASK), beat, 0x1B55E0, [CR, kind], default={0x122BB8: r})


def fx_cases(add):
    for beat in ('a06b_00_trigger_jump', 'a04b_03_reader'):
        for a0, a1 in ((3, 7), (0, 0), (-1, 0x7FFF)):
            add('fx 1EAD70 %s %d %d' % (beat, a0, a1), beat, 0x1EAD70, [a0, a1])
            add('fx 1ED100 %s %d %d' % (beat, a0, a1), beat, 0x1ED100, [a0, a1])
    view = u32(CAPTURES['a04b_03_reader'][0], 0x275C34)
    for seed in (0, 0xFFFFFFFF, 0x80000000, 0x12345678):
        for level in (0.0, 0.02, 0.019, 1.0, -1.0):
            add('fx 1ED100 seed %X %g' % (seed, level), 'a04b_03_reader', 0x1ED100, [1, 2],
                [(view + 4, w32(seed)), (view + 8, fw(level))], full=level in (0.019, -1.0))
        add('fx 1EAD70 seed %X' % seed, 'a04b_03_reader', 0x1EAD70, [1, 2], [(view + 4, w32(seed))])
    # 001F91C0: every type on the creature (its bones), type 10's probe and radius test
    for type_ in (2, 9, 10, 11, 0, 1, 12):
        add('fx 1F91C0 type %d' % type_, CBEAT, 0x1F91C0, [CR], [(CR + 3, b8(type_))])
    for hit in (0, 1):
        for tall in (5.0, 10.0, 10.000001, 30.0):
            add('fx 1F91C0 10 %d %g' % (hit, tall), CBEAT, 0x1F91C0, [CR], [(CR + 3, b8(10))] +
                [(u32(CAPTURES[CBEAT][0], CR + 0x110 + 4 * i) + 0xC4, fw(tall)) for i in range(0x20)
                 if 0x100000 <= u32(CAPTURES[CBEAT][0], CR + 0x110 + 4 * i) < 0x2000000],
                default={0x19A570: hit})
    # type changes to another between rows (the whole function ends)
    add('fx 1F91C0 type change', CBEAT, 0x1F91C0, [CR], [(CR + 3, b8(10))],
        queues={0x19A570: [Scribble(0, [(CR + 3, b8(5))])]})


# ---------------------------------------------------------------------------
# targeted creature cases: the branches the random states reach rarely. Table
# contents are read from the capture at run time (never embedded).
# ---------------------------------------------------------------------------
def rand_where(table, pred, shift, mask=0xF, width=1):
    """A 00122BB8 value whose index (r >> shift) & mask picks an entry of the
    table (bytes or halfwords, in the capture) that satisfies pred."""
    ram = CAPTURES[CBEAT][0]
    for i in range(mask + 1):
        value = ram[table + i] if width == 1 else struct.unpack_from('<H', ram, table + 2 * i)[0]
        if pred(value):
            return i << shift
    return None


def player_ready():
    """0021BE40(player) is 0: +0 and +4 are 1, +0x220 above 0, +0x20E 0."""
    return [(PLAYER, b8(1)), (PLAYER + 4, b8(1)), (PLAYER + 0x220, fw(90.0)), (PLAYER + 0x20E, h16(0))]


def player_as_creature():
    """The player's box list and bones made the creature's: 001A7BA0 finds an overlap."""
    ram = CAPTURES[CBEAT][0]
    return ([(PLAYER + 0x58, w32(u32(ram, CR + 0x58))), (PLAYER + 0x5C, b8(ram[CR + 0x5C])),
             (PLAYER + 0x5D, b8(ram[CR + 0x5D]))] +
            [(PLAYER + 0x110 + 4 * i, w32(u32(ram, CR + 0x110 + 4 * i))) for i in range(0x30)])


def floor_at(height, slope=0.0, flags=1):
    """One probe hit (0x700031E0.. ) with the given height, slope and flags:
    (RAM patches, scratchpad patches)."""
    return [(0x282250, fw(slope))], [(0x700031E0, w32(1)), (0x70003170, h16(flags)), (0x700030F0, fw(height))]


def placed(dist, face=True, dy=0.0, off=0.0):
    """The creature at `dist` from the player (0x810350), facing it or not."""
    ang = 0.6
    x, z = 545.0 + dist * math.sin(ang), 282.2 + dist * math.cos(ang)
    yaw = ang + math.pi + off if face else ang + off
    return [(CR + 0xB0, fw(x, 54.9 + dy, z, 1.0)), (CR + 0xC4, fw(yaw))]


def clean(sub5, sub6, flags=1):
    return [(CR, b8(1)), (CR + 4, b8(1)), (CR + 5, b8(sub5)), (CR + 6, b8(sub6)), (CR + 0x0D, b8(flags)),
            (CR + 0x0A, b8(0)), (CENT + 0x56, h16(0)), (CENT + 0x5A, h16(0)), (CENT + 0x61, b8(0)),
            (CENT + 0x62, b8(0)), (CENT + 0x60, b8(0)), (CENT + 0x69, b8(0))]


def targeted_creature_cases(add):
    beat = CBEAT
    # 0012E3A0: an unlisted state, modes 2 / 3 with +0x0D == 3
    for state in (4, 5):
        add('target 12E3A0 state %d' % state, beat, 0x12E3A0, [CR], [(CR + 4, b8(state))], [(MODE, b8(0))])
    for mode in (2, 3):
        for state in (0, 1, 3):
            add('target 12E3A0 mode %d state %d' % (mode, state), beat, 0x12E3A0, [CR],
                [(CR + 4, b8(state)), (CR + 0x0D, b8(3))], [(MODE, b8(mode))])
    # 0012E560: every kind, both difficulties, the model result, the +0x56 clip
    for flags in (0, 1, 3, 0x80, 0x81, 0x83, 2):
        for level in (0, 1):
            for model in (0, 1):
                add('target 12E560 %X %d %d' % (flags, level, model), beat, 0x12E560, [CR, CENT],
                    [(CR + 0x0D, b8(flags)), (0x81070A, b8(level)), (CR + 0x56, h16(level)), (0x810808, b8(0))],
                    default={0x1B10B0: model})
    add('target 12E560 810808', beat, 0x12E560, [CR, CENT], [(CR + 0x0D, b8(1)), (0x810808, b8(0xFF))])
    # 0012EB60 sub-state 0: each pick, the Use test far / near, +0x69, +0x61
    for pick in (0, 1, 3):
        r = rand_where(0x244F10, lambda v, pick=pick: v == pick, 8)
        for dist in (20.0, 80.0):
            for bit in (0, 1):
                for eat in (0, 1):
                    add('target 12EB60 pick %d %g %d %d' % (pick, dist, bit, eat), beat, 0x12EB60, [CR, CENT],
                        clean(0, 0) + placed(dist) + [(CENT + 0x68, b8(0)), (CENT + 0x69, b8(bit)),
                                                      (CENT + 0x61, b8(eat))],
                        queues={0x122BB8: [r, 0x1234]})
    for clip, frame in ((1, 95.0), (0x8001, 95.0), (1, 94.0)):
        add('target 12EB60 sound %X %g' % (clip, frame), beat, 0x12EB60, [CR, CENT],
            clean(0, 5) + [(CR + 0x2C, h16(clip)), (CR + 0x3C, fw(frame)), (CENT + 0x50, h16(5)),
                           (CENT + 0x58, h16(0x3000))], queues={0x122BB8: [1]})
    # 0012F100 sub-state 2: the path test finds a floor it cannot climb (0), then
    # the chase or the wander
    fr, fs = floor_at(45.0)
    for chase in (0, 1):
        for dist in (10.0, 30.0, 50.0):
            for face in (True, False):
                for blocked in (0, 1):
                    add('target 12F100 chase %d %g %d %d' % (chase, dist, face, blocked), beat, 0x12F100,
                        [CR, CENT], clean(1, 2) + placed(dist, face) + fr + [(CENT + 0x56, h16(chase)),
                                                                            (CENT + 0x50, h16(9))],
                        fs, queues={0x19AD00: [0], 0x1B3250: [blocked]})
    for reach in (0, 1):
        for d in (5.0, 20.0, 40.0):
            add('target 12F100 wander %d %g' % (reach, d), beat, 0x12F100, [CR, CENT],
                clean(1, 2) + placed(d) + fr + [(CENT + 0x68, b8(reach)), (CENT, fw(545.0 + d, 55.0, 282.2, 1.0)),
                                                 (CENT + 0x50, h16(9))],
                fs + [(0x70003B68, w32(64)), (0x70003B8A, h16(0))], queues={0x19AD00: [0]})
    # 0012FC10 sub-state 2: the bite lands (the player given the creature's boxes)
    for kind in (0, 1):
        for flags in (0x81, 0x80, 3, 1, 0):
            for level in (0, 1):
                add('target 12FC10 bite %d %X %d' % (kind, flags, level), beat, 0x12FC10, [CR, CENT],
                    clean(5, 2, flags) + player_ready() + player_as_creature() +
                    [(CENT + 0x64, b8(kind)), (0x81070A, b8(level)), (CENT + 0x54, h16(1)),
                     (CENT + 0x58, h16(0x1000))], [(MODE, b8(0))])
    for flags in (0, 1, 2, 3, 0x80, 0x81):
        add('target 12FC10 spit %X' % flags, beat, 0x12FC10, [CR, CENT],
            clean(5, 2, flags) + [(CENT + 0x64, b8(2)), (CR + 0x3C, fw(24.0)), (CENT + 0x54, h16(1))],
            queues={0x122BB8: [0x5A5A5A5A]})
    # 00131F90: the side step (clip 7, frames left in each range, both turns)
    for frame in (0.0, 5.0, 10.0, 12.0, 20.0, 25.0, 30.0, 33.5, 40.0, 47.0, 50.0):
        for turn in (0.3, -0.3, 0.01, -0.01):
            for side in (0, 1, 2):
                add('target 131F90 %g %g %d' % (frame, turn, side), beat, 0x131F90, [CR, CENT],
                    clean(1, 2) + [(CR + 0x2C, h16(7)), (CR + 0x3C, fw(frame)), (CR + 0xC4, fw(1.0)),
                                   (CENT + 0x30, fw(1.0 + turn)), (CENT + 0x63, b8(side))],
                    full=side == 0 and frame not in (10.0, 30.0))
    # 00132490: every speed table clip
    for clip in (0x28, 0x21, 0x20, 0x1D, 0x1C, 0x1B, 0x1A, 0x19, 0x18, 0x17, 0x14, 0x13, 0x12, 0x11, 0x10, 0x0F,
                 0x0E, 0x0A, 9, 8, 7, 2, 3):
        add('target 132490 clip %X' % clip, beat, 0x132490, [CR, CENT],
            [(CR + 0x2C, h16(clip)), (CENT + 0x44, fw(30.0)), (CR + 0x3C, fw(2.5))])
    # 001328D0: the second corner pair and the ledge
    for results in ([0, 0, 1], [0, 0, 0, 1], [0, 0, 0, 0], [1]):
        add('target 1328D0 corners %s' % results, beat, 0x1328D0, [CR, CENT],
            [(CENT + 0x38, fw(-0.5)), (CENT + 0x3C, fw(0.5))], queues={0x1B3390: list(results), 0x1B32F0: [0]})
    for attr in (0x5B, 0x5A):
        add('target 1328D0 ledge %X' % attr, beat, 0x1328D0, [CR, CENT],
            [(CENT + 0x38, fw(-0.05)), (POLY + 0x1A, b8(attr))], [(0x700031D0, w32(POLY)), (0x700031B4, fw(33.0))],
            default={0x19B6C0: 1})
    # 00132FB0: bit 2 alone (the turn away), the climb (return 2), the floor above (0)
    for hit, d4 in ((4, 0), (5, 0)):
        add('target 132FB0 bits %d' % hit, beat, 0x132FB0, [CR, CENT], [(POLY + 0x1A, h16(0))],
            [(0x700031D4, w32(d4)), (0x700031D0, w32(POLY)), (0x700031E0, w32(0))], queues={0x19AD00: [hit]})
    for area in (7, 1):
        for floor in (-40.0, 80.0):
            for probe in ([0, 0], [1], [0, 1]):
                add('target 132FB0 climb %d %g %s' % (area, floor, probe), beat, 0x132FB0, [CR, CENT],
                    clean(1, 2, 1) + [(POLY + 0x1A, h16(0)), (0x810700, b8(area))],
                    [(0x700031D4, w32(1)), (0x700031D0, w32(POLY)), (0x700031E0, w32(0)),
                     (0x700038D0, fw(floor))],
                    queues={0x19AD00: [2] + [probe[-1]], 0x19A570: [probe[0]], 0x122BB8: [0x7FFFFFFF],
                            0x19A310: [scribble_float(2.0)], 0x1B2E50: [1]})
    # 001333F0: the retry count's end, the facing count's limit
    for count in (0x76, 0x77, 0x78):
        add('target 1333F0 count %X' % count, beat, 0x1333F0, [CR, CENT],
            clean(1, 1, 1) + [(CENT + 0x56, h16(3)), (CENT + 0x44, fw(70.0)), (CENT + 0x6F, b8(count))])
    for count in (0, 3, 4, 0x3B, 0xB3):
        for level in (0, 3):
            add('target 1333F0 facing %X %d' % (count, level), beat, 0x1333F0, [CR, CENT],
                clean(1, 1, 1) + [(CENT + 0x6F, b8(count)), (0x81050C, b8(level))], default={0x1B3F10: 1})
    # 00133640: the lunge (near, facing, clear, dy)
    for dist in (10.0, 20.0, 30.0, 51.0):
        for dy in (0.0, 12.9, 13.1):
            for flags in (1, 3, 2):
                for face in (True, False):
                    add('target 133640 %g %g %X %d' % (dist, dy, flags, face), beat, 0x133640, [CR, CENT],
                        clean(1, 1, flags) + player_ready() + placed(dist, face, dy),
                        [(MODE, b8(0))], queues={0x19A570: [0]},
                        full=face is False or dy == 12.9)
    near_r = rand_where(0x245120, lambda v: v != 0, 4)
    zero_r = rand_where(0x245120, lambda v: v == 0, 4)
    far_rolls = [rand_where(0x245130, lambda v, t=t: v == t, 17) for t in (0, 1, 2)]
    for r in (near_r, zero_r):
        for busy in (0, 1):
            for flags in (1, 3):
                add('target 133640 roll %X %d %X' % (r, busy, flags), beat, 0x133640, [CR, CENT],
                    clean(1, 1, flags) + player_ready() + placed(10.0, True, 5.0), [(MODE, b8(0))],
                    queues={0x19A570: [0], 0x122BB8: [r, 0x800], 0x21BED0: [busy]})
    for r in far_rolls:
        for flags in (0, 1):
            add('target 133640 far %X %X' % (r, flags), beat, 0x133640, [CR, CENT],
                clean(1, 1, flags) + player_ready() + placed(40.0, True, 0.0), [(MODE, b8(0))],
                queues={0x19A570: [0], 0x122BB8: [r]})
    # 001B30E0: a later, higher floor (above or below the point)
    for y in (20.0, 60.0):
        spad = [(0x700031E0, w32(2)), (0x70003170, h16(1)), (0x70003172, h16(1)), (0x700030F0, fw(10.0)),
                (0x700030F4, fw(50.0))]
        add('target 1B30E0 higher %g' % y, beat, 0x1B30E0, [FREE + 0x100, FREE + 0x200],
            [(0x282250, fw(0.0, 0.0)), (FREE + 0x100, fw(1.0, y, 2.0))], spad)
    # 001F91C0 type 2: the other width order (a patched part row)
    ram = CAPTURES[beat][0]
    row = 0x25DB20
    a, b = struct.unpack_from('<ff', ram, row + 4)
    add('target 1F91C0 widths', beat, 0x1F91C0, [CR], [(CR + 3, b8(2)), (row + 4, fw(b + 1.0, a))])
    add('target 1F91C0 widths close', beat, 0x1F91C0, [CR], [(CR + 3, b8(2)), (row + 4, fw(b * 1.001, b))])
    # 001A7BA0: more than 4 points in a group (clamped), and boxes that overlap
    # without enclosing the origin (the player's bones shifted against the creature's)
    lst = u32(ram, CR + 0x58)
    g = lst + 4 + struct.unpack_from('<H', ram, lst + 4 + 4)[0]
    add('target 1A7BA0 clamp', beat, 0x1A7BA0, [CR, CR, 0x30, 0x30], [(g + 6, h16(6))])
    for shift in (1, 4, 7):
        pat = [(PLAYER + 0x58, w32(lst)), (PLAYER + 0x5C, b8(ram[CR + 0x5C])), (PLAYER + 0x5D, b8(ram[CR + 0x5D]))]
        pat += [(PLAYER + 0x110 + 4 * i, w32(u32(ram, CR + 0x110 + 4 * ((i + shift) % 0x30)))) for i in range(0x30)]
        add('target 1A7BA0 shifted %d' % shift, beat, 0x1A7BA0, [CR, PLAYER, 0x30, 0x30], pat)


def clip_frames(clip):
    """001C6160(creature) with the clip set, run as original code on the
    capture (the clip's frame count the side step compares against)."""
    ram, spad = CAPTURES[CBEAT]
    ee = Coverage(None, patched(ram, [(CR + 0x2C, h16(clip))]), bytearray(spad))
    ee.r[4] = CR
    ee.r[31] = RETURN
    ee.run(0x1C6160)
    return s32(ee.r[2])


def survivor_cases(add):
    """Cases the bounded mutation sweep (section 3.5) showed the default run
    needed: the side step's frame ranges at their edges (00128250 of the
    frames left at 10, 11, 12 / 22, 23, 24 / 47, 48 / 55, 56), 00132FB0's
    three random fields with a rand word that tells their shifts apart, and
    the second round's edges (survivor_cases_2)."""
    beat = CBEAT
    total = clip_frames(7)
    for left in (10, 11, 12, 22, 23, 24, 47, 48, 55, 56):
        frame = float(total - left)
        for turn in (0.3, -0.3):
            add('survivor 131F90 left %d %g' % (left, turn), beat, 0x131F90, [CR, CENT],
                clean(1, 2) + [(CR + 0x2C, h16(7)), (CR + 0x3C, fw(frame)), (CR + 0xC4, fw(1.0)),
                               (CENT + 0x30, fw(1.0 + turn)), (CENT + 0x63, b8(0))])
    fr, fs = floor_at(45.0)
    for r in (1 << 5, 1 << 6, 1 << 8, 1 << 9, 1 << 10, 1 << 13):
        add('survivor 132FB0 wander5 %X' % r, beat, 0x132FB0, [CR, CENT], [], [(0x700031E0, w32(0))],
            queues={0x19AD00: [0], 0x122BB8: [r]})
        add('survivor 132FB0 wander9 %X' % r, beat, 0x132FB0, [CR, CENT], fr + [(CR + 0xB4, fw(20.0))], fs,
            queues={0x19AD00: [0], 0x122BB8: [r]})
        add('survivor 132FB0 turn %X' % r, beat, 0x132FB0, [CR, CENT], [(POLY + 0x1A, h16(0))],
            [(0x700031D4, w32(0)), (0x700031D0, w32(POLY)), (0x700031E0, w32(0))],
            queues={0x19AD00: [4], 0x122BB8: [r]})
    survivor_cases_2(add)


def box_object(at, lo, hi, n, mask=1):
    """A designed collision object for 001A7BA0: +0x58 the group list (one
    group: masks 1 / 1, bone 0, n points at group +0x18, stride 12, the
    first n corners of the box lo..hi), +0x5C / +0x5D masks, bone 0's node
    with the identity matrix at +0x90."""
    lst, node = at + 0x400, at + 0x800
    corners = [(x, y, z) for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])]
    pts = [corners[k] for k in (0, 7, 3, 4, 5, 6, 1, 2)][:n]
    p = [(at + 0x58, w32(lst)), (at + 0x5C, b8(mask)), (at + 0x5D, b8(mask)), (at + 0x110, w32(node)),
         (node + 0x90, fw(1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0)),
         (lst, w32(1)), (lst + 4, b8(1)), (lst + 5, b8(1)), (lst + 7, b8(0)), (lst + 8, h16(0x18 + 12 * n)),
         (lst + 10, h16(n))]
    for k, (x, y, z) in enumerate(pts):
        p.append((lst + 4 + 0x18 + 12 * k, fw(x, y, z)))
    return p


def survivor_cases_2(add):
    """Round two (section 3.5): 001ED100's clamp at the bits next to 0.02;
    001A7BA0 on designed boxes that touch on each face and with five points
    a group (the clamp to four); 0012FC10's spit speed with high rand bytes
    and its stage 3 with +0x58 bit 0 alone; 001328D0's stage 3; 001B30E0's
    slope at pi/3 and the next float."""
    view = u32(CAPTURES['a04b_03_reader'][0], 0x275C34)
    for bits_ in (0x3CA3D709, 0x3CA3D70A, 0x3CA3D70B, 0x3CA3D70C, 0x3CA3D70D, 0x3CA3D70E):
        add('survivor 1ED100 level %08X' % bits_, 'a04b_03_reader', 0x1ED100, [1, 2], [(view + 8, w32(bits_))])
    A, B = FREE + 0x4000, FREE + 0x5000
    for axis in range(3):
        for side in (0, 1):
            lo_a, hi_a, lo_b, hi_b = [0.0, 0.0, 0.0], [1.0, 1.0, 1.0], [0.5, 0.5, 0.5], [1.5, 1.5, 1.5]
            if side == 0:     # a's low face on b's high face
                lo_a[axis], hi_a[axis], lo_b[axis], hi_b[axis] = 1.0, 2.0, 0.0, 1.0
            else:             # a's high face on b's low face
                lo_a[axis], hi_a[axis], lo_b[axis], hi_b[axis] = 0.0, 1.0, 1.0, 2.0
            add('survivor 1A7BA0 touch %d %d' % (axis, side), CBEAT, 0x1A7BA0, [A, B, 0x10, 0x10],
                box_object(A, lo_a, hi_a, 4) + box_object(B, lo_b, hi_b, 4))
    for na, nb in ((4, 4), (5, 4), (4, 5), (5, 5), (8, 8)):
        add('survivor 1A7BA0 overlap %d %d' % (na, nb), CBEAT, 0x1A7BA0, [A, B, 0x10, 0x20],
            box_object(A, (0.0, 0.0, 0.0), (1.0, 1.0, 1.0), na) +
            box_object(B, (0.25, 0.25, 0.25), (1.25, 1.25, 1.25), nb))
    for r in (0xFF << 14, 0x80 << 14, 0x7F << 14, 1 << 14, -1):
        for flags in (3, 0x83):
            add('survivor 12FC10 spit %X %X' % (r & MASK, flags), CBEAT, 0x12FC10, [CR, CENT],
                [(CR + 6, b8(2)), (CR + 0x0D, b8(flags)), (CR + 0x3C, fw(24.0)), (CENT + 0x5A, h16(1)),
                 (CENT + 0x64, b8(2)), (CENT + 0x54, h16(5))], queues={0x122BB8: [r]})
    for hit in (0x0001, 0x1000, 0x1001, 0x0FFF):
        add('survivor 12FC10 stage3 %X' % hit, CBEAT, 0x12FC10, [CR, CENT],
            [(CR + 6, b8(3)), (CENT + 0x58, h16(hit))])
    for st in (2, 3, 4, 5):
        add('survivor 1328D0 stage %d' % st, CBEAT, 0x1328D0, [CR, CENT],
            [(CR + 4, b8(1)), (CR + 5, b8(st)), (CENT + 0x60, b8(0))])
    for slope in (0x3F860A91, 0x3F860A92, 0x3F860A93, 0xBF860A92, 0xBF860A93):
        add('survivor 1B30E0 slope %08X' % slope, CBEAT, 0x1B30E0, [FREE + 0x100, FREE + 0x200],
            [(0x282250, w32(slope)), (FREE + 0x100, fw(1.0, 20.0, 2.0)), (FREE + 0x200, fw(0.0))],
            [(0x700031E0, w32(1)), (0x70003170, h16(1)), (0x700030F0, fw(10.0))])
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

# Words the static walk reaches that no input can (none so far).
DEAD_WORDS = {}




def fault_checks():
    """The native fail-stop contract: a NULL hook, a failing hook, an
    unmapped address and a fault latched before the call (001BC740 on
    button [54] with the Use bit: 001B1470, 001026A0, 00182F90, 001BA1A0,
    001BA1F0; and 001BD180 for an unmapped address before any call), then
    every entry with a latched fault, a NULL hook table, a NULL fault
    pointer and (entries with a result) a NULL result pointer."""
    ram, spad = CAPTURES['a04b_03_reader']
    node = A04_BUTTON54
    ram = bytearray(ram)
    ram[node + 0x0B] |= 4
    problems = []
    calls_want = ['w_001B1470', 'w_001026A0', 'w_00182F90', 'w_001BA1A0', 'w_001BA1F0']

    def attempt(null=None, failing=None, unmapped=None, latched=False, entry=0x1BC740, args=(node, node + 0x1F0)):
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
    status, address, code, calls, _ = attempt(null='w_00182F90')
    if (status, address, code) != (-1, 0x182F90, 1) or calls != calls_want[:2]:
        problems.append(('NULL hook', status, hex(address), code, calls))
    status, address, code, calls, same = attempt(failing='w_001B1470')
    if (status, address, code) != (-1, 0x1B1470, 2) or calls != calls_want[:1] or not same:
        problems.append(('failing hook', status, hex(address), code, calls, same))
    status, address, code, calls, _ = attempt(unmapped=D_PLAYER_A4)
    if (status, address, code) != (-1, D_PLAYER_A4, 5) or calls != calls_want[:2]:
        problems.append(('unmapped address', status, hex(address), code, calls))
    status, address, code, calls, same = attempt(unmapped=A04_LIFT + 0x200, entry=0x1BD180, args=(A04_LIFT + 0x1F0,))
    if (status, address, code) != (-1, A04_LIFT + 0x200, 5) or calls or not same:
        problems.append(('unmapped address before any call', status, hex(address), code, calls, same))
    status, address, code, calls, same = attempt(latched=True)
    if (status, address, code) != (-1, 0x1234, 7) or calls or not same:
        problems.append(('latched fault', status, hex(address), code, calls, same))
    # every entry: a fault latched on entry, a NULL hook table, a NULL fault
    # pointer or a NULL result pointer returns -1 at once, with no call
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


D_PLAYER_A4 = PLAYER + 0xA4


# Words the static walk reaches that no input can reach, and why.
DEAD_WORDS = {
    # 001A7BA0's two corner picks test c.le.s of a register against itself;
    # the EE's FPU orders every bit pattern (no unordered result), so the
    # test is always true and the corner stays 0.
    0x1A8404: 'self-compare', 0x1A8408: 'self-compare', 0x1A840C: 'self-compare', 0x1A845C: 'self-compare',
}
# 001A7BA0's d4 = dot(normalize(v), v) (00102760 then 00102738): each lane
# product is v_i (v_i q) with q = 1 / sqrt(sum of squares) >= 0, so every
# term's sign bit is clear and the sum is not below 0; the negation branch
# after its c.lt.s against 0 cannot run.
for _pc in range(0x1A850C, 0x1A854C, 4):
    DEAD_WORDS[_pc] = 'dot of a vector with its own normal'


import types  # noqa: E402


def leftover_checks():
    """The paths where the original reads a register its caller left: 001B4810
    with a type that sets neither the bone index nor the radius (0, 2, 8, 12,
    0xFF: the caller's s0 and f20) and 001B55E0 with a kind other than 0, 1, 2
    (the caller's s0 as the class). The translation faults 7
    (EM_LEVEL8_PORT_FAULT_REGISTER) instead: 001B4810 right after reading the
    type (no call, nothing written), 001B55E0 where the original makes the
    call that takes the register (every call and memory access before it the
    original's). Returns (problems, the original pcs these runs executed)."""
    problems, pcs = [], set()
    ram0, spad0 = CAPTURES[CBEAT]
    nothing = types.SimpleNamespace(log=[], stores=[], marks=[], access=[[]], stopped=None)
    for type_ in (0, 2, 8, 12, 0xFF):
        ram, spad = patched(ram0, [(CR + 3, b8(type_))]), patched(spad0, [])
        seen = set()
        try:
            Oracle(ram, spad, 0x1B4810, ([CR], set()), Script(), RUN, seen)
            pcs |= seen
        except AssertionError:
            pass
        ram, spad = patched(ram0, [(CR + 3, b8(type_))]), patched(spad0, [])
        replay, status, fault, _ = native_call(NATIVE, ram, spad, 0x1B4810, [CR], nothing)
        replay.settle()
        if (status, fault.address, fault.code) != (-1, 0x1B4810, 7) or replay.i or \
                [a for interval in replay.access for a in interval] != [[CR + 3, 1, False]]:
            problems.append(('1B4810 type %d' % type_, status, hex(fault.address), fault.code, replay.i))
    for kind in (3, -1, 0x80, 0x7F):
        ram, spad = patched(ram0, []), patched(spad0, [])
        seen = set()
        oracle = Oracle(ram, spad, 0x1B55E0, ([CR, kind], set()), Script(), RUN, seen)
        pcs |= seen
        names = [e[0] for e in oracle.log]
        replay, status, fault, _ = native_call(NATIVE, ram, spad, 0x1B55E0, [CR, kind], oracle)
        k = len(names) - 1
        ok = (names[-1] == 'w_001EFD90' and (status, fault.address, fault.code) == (-1, 0x1B55E0, 7)
              and replay.i == k and not replay.errors and replay.check_access(k)
              and replay.compare(oracle.marks[k], 'the leftover fault', full=True))
        if not ok:
            problems.append(('1B55E0 kind %d' % kind, status, hex(fault.address), fault.code, replay.i,
                             replay.errors[:1]))
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


def hook_contract_site(site):
    """The fail-stop contract of the header (em_level8_port.h), on one
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

    for k in range(len(names)):   # every call fails
        replay, status, fault, result = run(inject={k: ('fail', -1)})
        expect('call %d (%s) fails' % (k, names[k]), replay, status, fault, result, (-1, callee[k] & MASK, 2, k + 1),
               len(replay.access) == k + 2 and not replay.access[k + 1]
               and replay.compare(oracle.marks[k], 'the fault', full=False))
        restore(buffers, ram, spad, replay, oracle)
    for n, (j, address) in enumerate(flat):   # every access refused
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


def select_cases(elf):
    """The mode's case list (EM_TEST_FULL=1: all; the default run keeps
    every capture and designed case not marked full-only)."""
    cases, captured, targeted = case_list(elf)
    fixed = captured + targeted
    total = len(cases)
    if not reference_mode.FULL:
        cases = [c for c in cases[:fixed] if c[0] not in FULL_ONLY]
        return cases, captured, len(cases) - captured, total
    return cases, captured, targeted, total


def image_for(entry):
    """An image holding the entry's code (boot text is the same in all)."""
    oid = OVERLAY_OF.get(entry)
    if oid == 5:
        return CAPTURES[AREA04_BEATS[0]][0]
    return CAPTURES[BEATS[0]][0]
def main():
    global NATIVE
    elf = read_elf()
    CAPTURES.update(load_captures(elf))
    NATIVE = build(os.environ.get('EM_LEVEL8_PORT_SOURCE'))
    selected, captured, targeted, total = select_cases(elf)
    CASES[:] = selected
    only = os.environ.get('EM_LEVEL8_PORT_ONLY')   # debugging: a label prefix
    if only:
        CASES[:] = selected = [c for c in selected if c[0].startswith(only)]
    results = run_cases(selected)
    failures, per_fn, seen, totals = [], {}, set(), [0, 0, 0, 0]
    if not only:
        problems, pcs = leftover_checks()
        seen.update(pcs)
        print('  register-leftover paths: %s' % ('ok (001B4810 types 0, 2, 8, 12, 0xFF fault 7 after the type; '
                                                 '001B55E0 kinds 3, -1, 0x80, 0x7F fault 7 at the call that takes '
                                                 'the register, after the original calls and accesses)'
                                                 if not problems else problems))
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
    if os.environ.get('EM_LEVEL8_PORT_MISSING'):
        for entry, words in missing_words.items():
            print('   missing %06X: %s' % (entry, ' '.join('%X' % w for w in words[:40])))
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
        outcome = reference_mode.parallel_map(hook_contract_site, sites)
        contract = [p for problems_, _ in outcome for p in problems_]
        if missing:
            contract.append(('no passing case reaches', missing))
        print('  hook contract: %s' % (
            'ok (%d native runs on %d cases: every call failing and every memory access refused; each of the %d hooks '
            'and the +0x4C callback NULL, returning INT32_MIN, 1 and INT32_MAX; `bytes` NULL on all %d entries)'
            % (sum(n for _, n in outcome), len(sites), len(HOOKS), len(FUNCS)) if not contract else contract[:6]))
        if contract:
            failures.append(('hook contract', contract))
        if reuse_checks is not None:
            problems, report = reuse_checks(elf)
            print('  reuse checks: %s' % (report if not problems else problems[:4]))
            if problems:
                failures.append(('reuse checks', problems[:6]))
    for label, errors in failures[:12]:
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
# reuse_checks: the census rows (and the lift pair the task names) with an
# existing verified translation, re-run against the ORIGINAL over the
# eighth-level captures with each module's own harness (imported, not
# modified; the libraries are built into build/level8/port/reuse):
#   0x824830  em_area04_port_00824830 (tools/test_area04_port_reference.py,
#             run_case): [3] (0x7AA590) as captured in the a22b_01 and
#             a04b_00..03 images, and its sub-states 0 (D_008107EA 0 / 0x10
#             / 0xFF, 001B6660 results), 1 (001BA1F0 results) and others;
#   00215870  em_status_pages_00215870 (tools/test_status_pages_reference.py,
#             item_case): the harness's own EVENT page inputs over the
#             a04b_01 image (the NPC's item page frame);
#   001BC960 / 001BD560  em_area02_math_001BC960 / _001BD560 (not a04b rows:
#             both ran on earlier levels; tools/test_area02_math_reference.py,
#             run_case): the lift [51] / [56] and its buttons [53] / [54] as
#             captured in the a04b images and at a04b_04's AREA13 arrival
#             (001BD560 [0x7AC000], 001BC960 [0x7AC5E0, 0x7AC8D0]).
REUSE_OUT = OUT / 'reuse'
A04B_BEATS = ['a22b_01_door6', 'a04b_00_door45', 'a04b_01_npc', 'a04b_02_door45_out', 'a04b_03_reader']


def reuse_area04(elf, smoke=False):
    import test_area04_port_reference as A4
    A4.OUT = REUSE_OUT / 'area04'
    beats = A04B_BEATS[-1:] if smoke else A04B_BEATS
    A4.BEAT_DIRS.clear()
    A4.BEAT_DIRS.update({b: BEAT_DIRS[b] for b in beats})
    A4.BEATS[:] = beats
    A4.OVERLAY_BEATS[:] = beats
    A4.CAPTURES.clear()
    A4.CAPTURES.update(A4.load_captures(elf))
    A4.NATIVE = A4.build()
    r3 = 0x7AA590
    cases = []

    def add(label, beat, args, patches=(), queues=None):
        cases.append((label, beat, 0x824830, tuple(args), list(patches), [], A4.Script(queues, None),
                      frozenset(A4.RUN)))
    for beat in beats:
        add('capture %s' % beat, beat, [r3])
        if smoke:
            continue
        for ea in (0, 0x10, 0xFF):
            for obj in (0x7C0000, 0):
                add('%s sub0 7EA %X obj %X' % (beat, ea, obj), beat, [r3], [(r3 + 5, b8(0)), (0x8107EA, b8(ea))],
                    queues={0x1B6660: [obj, 0x7C1000]})
        for result in (0, 1):
            add('%s sub1 %d' % (beat, result), beat, [r3], [(r3 + 5, b8(1))],
                queues={0x1BA1F0: [result], 0x1B6660: [0x7C2000, 0]})
        add('%s sub2' % beat, beat, [r3], [(r3 + 5, b8(2))])
    A4.CASES[:] = cases
    bad = []
    for case in cases:
        label, _, errors, *_ = A4.run_case(case)
        if errors:
            bad.append((label, errors[:1]))
    assert not bad, ('0x824830', bad[:3])
    return '0x824830 %d cases over %s' % (len(cases), ', '.join(beats))


def reuse_status(elf, smoke=False):
    import test_status_pages_reference as SP
    SP.IMAGES['a04b01'] = BEAT_DIRS['a04b_01_npc']
    if 'a04b01' not in SP.BEATS:
        SP.BEATS.append('a04b01')
    SP.setup()
    items = [('00215870', 'a04b01', k) for kind, beat, k in SP.item_items() if kind == '00215870' and beat == 'hub']
    if smoke:
        items = items[::8]
    done = refused = 0
    for item in items:
        r = SP.item_case(item)
        if r == 'unmeasured' or r == 'skipped':
            refused += 1
        else:
            done += 1
    return '00215870 %d EVENT page cases over the a04b_01 image (%d refused on both sides)' % (done, refused)


def reuse_lift(elf, smoke=False):
    import test_area02_math_reference as A2
    A2.OUT = REUSE_OUT / 'area02_math'
    A2.ELF = elf
    A2.NATIVE = A2.build_native()
    A2.BRANCH_PCS = A2.branch_pcs(A2.ExEE(elf).mem)
    for beat in ('a04b_00_door45', 'a04b_03_reader', 'a04b_04_lift'):
        A2.BEATS[beat] = BEAT_DIRS[beat]
    nodes = {'a04b_00_door45': (0x7B3290, 0x7B4140, 0x7B3870, 0x7B3B60),
             'a04b_03_reader': (0x7B3290, 0x7B4140, 0x7B3870, 0x7B3B60),
             'a04b_04_lift': (0x7AC000, None, 0x7AC5E0, 0x7AC8D0)}
    cases = []
    for beat, (lift, lift2, b1, b2) in nodes.items():
        if smoke and beat != 'a04b_03_reader':
            continue
        for node in (lift, lift2):
            if node:
                cases.append(A2.case('reuse 1BD560 %s %X' % (beat, node), 0x1BD560, [node], beat=beat))
        for node in (b1, b2):
            cases.append(A2.case('reuse 1BC960 %s %X' % (beat, node), 0x1BC960, [node], beat=beat))
        if smoke:
            continue
        # the Use at button [54] taken (001BC740 1) and not, its state 2 sub-state 1 as a04b_04 found it
        for r in (0, 1):
            cases.append(A2.case('reuse 1BC960 %s use %d' % (beat, r), 0x1BC960, [b2], beat=beat,
                                 script={0x1BC740: [r]}))
    bad = [r[1] for r in map(A2.run_case_safe, cases) if r[0] == 'fail']
    assert not bad, ('lift', bad[:2])
    return '001BD560 / 001BC960 %d cases (a04b images and the AREA13 arrival)' % len(cases)


def reuse_checks(elf):
    """EM_TEST_FULL=1 runs every check in full; the default run keeps one
    smoke sample of each."""
    problems, parts = [], []
    for check in (reuse_area04, reuse_status, reuse_lift):
        try:
            parts.append(check(elf, smoke=not reference_mode.FULL))
        except AssertionError as error:
            problems.append((check.__name__, repr(error)[:600]))
    return problems, 'ok: ' + '; '.join(parts)


if __name__ == '__main__':
    main()
