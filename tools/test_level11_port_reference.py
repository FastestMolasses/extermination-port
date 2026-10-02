#!/usr/bin/env python3
"""Execute the ORIGINAL eleventh-level code (the a13c census rows: AREA13's
[44] steps 2..4 and their helpers, the watchers' effect spawn 0x826610,
[45]'s step functions behind 0x82D190, the thrown piece 0x828500 and the
effects 0x828C60 / 0x828E10 / 0x828F40; the boot functions of the
0x141D20 actor family and the others) and compare em_level11_port*.c.

docs/LEVEL11_PORT.md. The census delta of the eleventh-level route (decomp
build/s87/census/a13c_delta.json, new_functions: 43 rows) names the
functions. Those without a verified port translation are translated in
em_level11_port_*.c; they are the entries below (FUNCS). One (0020D930,
whose table logic is em_menu_hover_0020D930) is reused and re-run against
the original by reuse_checks. Calls between the translations run as
original code at the top level of the oracle (only other functions are
hooks), and every function is also an entry of its own.

The harness is tools/test_level10_port_reference.py's (lane L10T, itself the
LEVEL9 / LEVEL8 / AREA06 / AREA22 / AREA04 design), copied and owned here,
with the eleventh-level captures, this module's hook table and cases.
Entries that take the original stack pointer (kind S) run the oracle with
sp = STACK_TOP and give the native entry STACK_TOP; the frame locals live in
the compared stack window.

Oracle: the shared EE interpreter with the measured float model (FallEE,
tools/test_player_fall_reference.py) runs the original code resident in the
recorded RAM images of the eleventh-level route (decomp build/s87/route_a13c/
<beat>/). Before any case the test checks that the overlay text of every
image equals the user's extract/OVERLAY/AREA13.BIN (text size from the file
header), that the boot text below 0x241000 and the boot jump tables equal
the pinned ELF, so every executed instruction is original. Nothing here
embeds original bytes; reports hold counts only.

Callees (every function outside this module and the indirect calls) are
intercepted at their entry and logged with their arguments. A callee either
runs as ORIGINAL code nested inside the oracle (the pure helpers in RUN),
its writes recorded, or is stubbed with the case's scripted result. The
native module runs over a byte copy of the same RAM with hooks that must be
called in the same order with the same arguments; each hook replays the
original callee's writes and result.

Compared, per case (the AREA22 harness's list): memory at the entry of
every call before the callee's writes are replayed; the callee and its
arguments; the memory accesses between calls one for one, in order, by
address, size and changed-or-not; all memory after the last store; the
return value; the store-log self-check; stops at unmapped or misaligned
original accesses; every case again from a poisoned start image; coverage
of every reachable original word; the fail-stop contract; the table's ctx
at every call.

EM_LEVEL11_PORT_ONLY=<label prefix> runs a subset (no coverage / contract /
reuse checks). EM_LEVEL11_PORT_SOURCE=<dir> tests another copy of the
module sources (the mutation sweep). EM_LEVEL11_PORT_MISSING=1 lists
unexecuted words. At most 4 worker processes unless EM_TEST_JOBS says
otherwise.
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
OVERLAY_FILES = {10: DECOMP / 'extract/OVERLAY/AREA13.BIN'}
OUT = ROOT / 'build/level11/port'
MASK = 0xFFFFFFFF
MASK64 = 0xFFFFFFFFFFFFFFFF
RAM_SIZE, SPAD_SIZE = 0x2000000, 0x4000
ARENA = 0x823500
STACK_LO = STACK_TOP - 0x800
STACK_SIZE = STACK_TOP - STACK_LO
BEAT_DIRS = {}
for _b in ('a13c_00_recharger', 'a13c_01_to_machine', 'a13c_02_battery', 'a13c_03_blast', 'a13c_04_cure',
           'a13c_05_boom', 'a13c_06_south'):
    BEAT_DIRS[_b] = DECOMP / 'build/s87/route_a13c' / _b
BEATS = list(BEAT_DIRS)
# Every image holds AREA13 (overlay id 10 at 0x823504): the group stays in
# one AREA13 load (ELEVENTH_LEVEL_ROUTE.md).
RESIDENT = {b: 10 for b in BEATS}
A13_BEATS = list(BEATS)
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
    0x8240E0: ('em_level11_port_008240E0', 0x7C, [], I),
    0x824390: ('em_level11_port_00824390', 0x18C, [U], None),
    0x824520: ('em_level11_port_00824520', 0x1AC, [U], None),
    0x8246D0: ('em_level11_port_008246D0', 0xE0, [U, U, U], I),
    0x8248C0: ('em_level11_port_008248C0', 0x94, [U, U, U], I),
    0x824960: ('em_level11_port_00824960', 0x88, [U], None),
    0x8249F0: ('em_level11_port_008249F0', 0x84, [U], I),
    0x826610: ('em_level11_port_00826610', 0x84, [U, S], None),
    0x827C30: ('em_level11_port_00827C30', 0x19C, [U, S], None),
    0x827DD0: ('em_level11_port_00827DD0', 0x2C, [U], None),
    0x827E00: ('em_level11_port_00827E00', 0x120, [U], None),
    0x827F20: ('em_level11_port_00827F20', 0x70, [U], None),
    0x827F90: ('em_level11_port_00827F90', 0x548, [U], None),
    0x8284E0: ('em_level11_port_008284E0', 0x20, [U, U], None),
    0x828500: ('em_level11_port_00828500', 0x758, [U, S], None),
    0x828C60: ('em_level11_port_00828C60', 0x1AC, [U, S], None),
    0x828E10: ('em_level11_port_00828E10', 0x11C, [U], None),
    0x828F40: ('em_level11_port_00828F40', 0x358, [U, S], None),
    # boot
    0x100130: ('em_level11_port_00100130', 0x24, [Q, Q], I),
    0x141D20: ('em_level11_port_00141D20', 0x1D8, [U, S], None),
    0x141F00: ('em_level11_port_00141F00', 0x168, [U, U], None),
    0x142070: ('em_level11_port_00142070', 0x2C0, [U, U, S], None),
    0x142330: ('em_level11_port_00142330', 0x184, [U, U, S], None),
    0x1424C0: ('em_level11_port_001424C0', 0x510, [U, U, S], None),
    0x1429D0: ('em_level11_port_001429D0', 0xAEC, [U, U, S], None),
    0x145880: ('em_level11_port_00145880', 0x114, [U, U], None),
    0x1459A0: ('em_level11_port_001459A0', 0x770, [U, U], None),
    0x146110: ('em_level11_port_00146110', 0x394, [U, U, S], I),
    0x1464B0: ('em_level11_port_001464B0', 0x288, [U, U], I),
    0x1469B0: ('em_level11_port_001469B0', 0x140, [U, U, S], None),
    0x146CE0: ('em_level11_port_00146CE0', 0x294, [U, U], I),
    0x1471E0: ('em_level11_port_001471E0', 0x1AC, [U, U], None),
    0x118418: ('em_level11_port_00118418', 0x294, [U], I),
    0x19A6F0: ('em_level11_port_0019A6F0', 0x214, [U, U, U, I, I, S], I),
    0x1B2E50: ('em_level11_port_001B2E50', 0x120, [U, U], I),
    0x1B3F10: ('em_level11_port_001B3F10', 0x2DC, [U, F, F, S], I),
    0x1CDDC0: ('em_level11_port_001CDDC0', 0x540, [I, I, U, Q, U, S], None),
    0x1CE660: ('em_level11_port_001CE660', 0x1F8, [I, I, U, U, Q, U, S], None),
    0x1E4610: ('em_level11_port_001E4610', 0x3D4, [U, S], None),
    0x1F4190: ('em_level11_port_001F4190', 0x6A8, [U, U, U], None),
    0x1F4840: ('em_level11_port_001F4840', 0x1B8, [U], None),
    0x1F9140: ('em_level11_port_001F9140', 0x38, [U, U, U, U, F, S], None),
}
OVERLAY_OF = {entry: 10 for entry in FUNCS if entry >= ARENA}
# The entries whose passing cases are all contract sites (they return from
# their own body): the default run and EM_TEST_FULL=1.
RETURN_SITES = (0x8240E0, 0x8246D0, 0x8248C0, 0x8249F0, 0x100130, 0x118418, 0x19A6F0, 0x1B2E50, 0x1B3F10)
RETURN_SITES_FULL = RETURN_SITES


def has_result(entry):
    return FUNCS[entry][3] is not None


# Hooks, in the field order of EmLevel11PortHooks (after ctx and bytes; the
# +0x4C method's w_callback follows them).
# name -> (original address, [(ctype, register)], result kind or None)
HOOKS = [
# BEGIN GENERATED HOOKS
    ('w_001000E0', 0x1000E0, [(Q, 4), (Q, 5)], I),
    ('w_001026A0', 0x1026A0, [(U, 4), (U, 5), (U, 6)], None),
    ('w_00102850', 0x102850, [(U, 4), (U, 5), (F, 12)], None),
    ('w_001028B8', 0x1028B8, [(U, 4), (U, 5), (U, 6)], None),
    ('w_001028D0', 0x1028D0, [(U, 4), (U, 5), (U, 6)], None),
    ('w_00102900', 0x102900, [(U, 4), (U, 5), (F, 12)], None),
    ('w_00102918', 0x102918, [(U, 4), (U, 5), (U, 6)], None),
    ('w_00102948', 0x102948, [(U, 4), (U, 5)], None),
    ('w_00102958', 0x102958, [(U, 4), (U, 5)], None),
    ('w_001029C0', 0x1029C0, [(U, 4)], None),
    ('w_00102BB0', 0x102BB0, [(U, 4), (U, 5), (F, 12)], None),
    ('w_00102C58', 0x102C58, [(U, 4), (U, 5), (U, 6)], None),
    ('w_001157F0', 0x1157F0, [(I, 4), (I, 5), (I, 6), (I, 7)], I),
    ('w_001179E0', 0x1179E0, [(I, 4), (U, 5)], I),
    ('w_00117BA0', 0x117BA0, [(I, 4), (I, 5)], I),
    ('w_0011DE90', 0x11DE90, [(F, 12)], F),
    ('w_0011DF78', 0x11DF78, [(F, 12)], F),
    ('w_0011E2A8', 0x11E2A8, [(F, 12)], F),
    ('w_0011E620', 0x11E620, [(F, 12), (F, 13)], F),
    ('w_00122BB8', 0x122BB8, [], I),
    ('w_001274B0', 0x1274B0, [(Q, 4), (Q, 5)], Q),
    ('w_00128250', 0x128250, [(F, 12)], I),
    ('w_00128350', 0x128350, [(F, 12)], Q),
    ('w_00131ED0', 0x131ED0, [(U, 4)], None),
    ('w_001434C0', 0x1434C0, [(U, 4), (U, 5)], None),
    ('w_00143610', 0x143610, [(U, 4), (U, 5)], None),
    ('w_001437E0', 0x1437E0, [(U, 4), (U, 5)], None),
    ('w_00143AF0', 0x143AF0, [(U, 4), (U, 5)], None),
    ('w_00144040', 0x144040, [(U, 4), (U, 5)], None),
    ('w_00144C20', 0x144C20, [(U, 4), (U, 5)], None),
    ('w_001450B0', 0x1450B0, [(U, 4), (U, 5)], None),
    ('w_00145850', 0x145850, [(U, 4), (U, 5)], None),
    ('w_00146740', 0x146740, [(U, 4), (U, 5)], I),
    ('w_00146AF0', 0x146AF0, [(U, 4), (U, 5), (F, 12)], I),
    ('w_0019A570', 0x19A570, [(U, 4), (U, 5), (I, 6), (I, 7)], I),
    ('w_0019AB20', 0x19AB20, [(U, 4), (U, 5), (U, 6), (I, 7)], I),
    ('w_0019AD00', 0x19AD00, [(U, 4), (U, 5), (I, 6)], I),
    ('w_0019D330', 0x19D330, [], I),
    ('w_001A0B10', 0x1A0B10, [], I),
    ('w_001A6440', 0x1A6440, [(I, 4)], I),
    ('w_001A7280', 0x1A7280, [(I, 4)], I),
    ('w_001AFA90', 0x1AFA90, [(I, 4)], U),
    ('w_001AFC10', 0x1AFC10, [(U, 4)], None),
    ('w_001B0D80', 0x1B0D80, [(U, 4)], None),
    ('w_001B10B0', 0x1B10B0, [(U, 4), (I, 5), (I, 6)], I),
    ('w_001B1240', 0x1B1240, [(U, 4), (F, 12), (F, 13)], F),
    ('w_001B12B0', 0x1B12B0, [(F, 12), (F, 13), (F, 14)], F),
    ('w_001B13F0', 0x1B13F0, [(U, 4), (U, 5), (F, 12)], I),
    ('w_001B1470', 0x1B1470, [(F, 12)], F),
    ('w_001B1560', 0x1B1560, [(U, 4), (U, 5), (F, 12)], I),
    ('w_001B15D0', 0x1B15D0, [(U, 4), (U, 5)], F),
    ('w_001B17A0', 0x1B17A0, [(U, 4)], I),
    ('w_001B1B70', 0x1B1B70, [(U, 4)], None),
    ('w_001B1E20', 0x1B1E20, [(I, 4), (I, 5)], None),
    ('w_001B1EA0', 0x1B1EA0, [(I, 4), (U, 5), (U, 6), (I, 7)], I),
    ('w_001B2140', 0x1B2140, [(U, 4)], I),
    ('w_001B2B10', 0x1B2B10, [(U, 4), (U, 5), (U, 6)], None),
    ('w_001B2BF0', 0x1B2BF0, [(U, 4), (U, 5), (U, 6), (F, 12)], I),
    ('w_001B2F70', 0x1B2F70, [(U, 4), (U, 5)], I),
    ('w_001B32F0', 0x1B32F0, [(U, 4), (U, 5), (F, 12)], I),
    ('w_001B3390', 0x1B3390, [(U, 4), (U, 5), (U, 6), (F, 12)], I),
    ('w_001B3440', 0x1B3440, [(U, 4), (U, 5), (U, 6), (F, 12)], I),
    ('w_001B3580', 0x1B3580, [(U, 4), (U, 5)], F),
    ('w_001B37D0', 0x1B37D0, [(U, 4), (F, 12), (F, 13)], F),
    ('w_001B39F0', 0x1B39F0, [(U, 4), (U, 5), (U, 6)], I),
    ('w_001B4810', 0x1B4810, [(U, 4)], None),
    ('w_001B4CF0', 0x1B4CF0, [(U, 4)], None),
    ('w_001B5360', 0x1B5360, [(U, 4)], None),
    ('w_001BA1A0', 0x1BA1A0, [(U, 4), (U, 5)], None),
    ('w_001BA1F0', 0x1BA1F0, [(U, 4)], I),
    ('w_001C6120', 0x1C6120, [(U, 4), (I, 5)], U),
    ('w_001C6380', 0x1C6380, [(U, 4)], None),
    ('w_001C63E0', 0x1C63E0, [(U, 4), (I, 5)], None),
    ('w_001C64F0', 0x1C64F0, [(U, 4), (F, 12)], I),
    ('w_001C67E0', 0x1C67E0, [(U, 4), (I, 5), (F, 12), (F, 13)], None),
    ('w_001C68C0', 0x1C68C0, [(U, 4)], None),
    ('w_001C7900', 0x1C7900, [(U, 4), (U, 5), (I, 6), (I, 7)], None),
    ('w_001CA1C0', 0x1CA1C0, [(U, 4), (U, 5), (U, 6)], None),
    ('w_001CA3B0', 0x1CA3B0, [(U, 4), (F, 12), (F, 13), (F, 14)], None),
    ('w_001CA4D0', 0x1CA4D0, [(U, 4), (U, 5), (U, 6)], None),
    ('w_001CA7B0', 0x1CA7B0, [(U, 4), (F, 12)], I),
    ('w_001CA940', 0x1CA940, [(I, 4), (I, 5)], None),
    ('w_001CB5F0', 0x1CB5F0, [(U, 4), (I, 5), (I, 6)], U),
    ('w_001CB900', 0x1CB900, [(U, 4), (I, 5), (I, 6)], None),
    ('w_001CB950', 0x1CB950, [(U, 4), (I, 5), (Q, 6)], U),
    ('w_001CCF70', 0x1CCF70, [(U, 4)], I),
    ('w_001CD370', 0x1CD370, [(I, 4)], U),
    ('w_001CF470', 0x1CF470, [(U, 4), (U, 5)], I),
    ('w_001CFA60', 0x1CFA60, [(U, 4), (U, 5), (F, 12), (F, 13)], None),
    ('w_001CFB50', 0x1CFB50, [(U, 4), (I, 5), (U, 6), (F, 12), (F, 13), (F, 14), (F, 15), (F, 16)], None),
    ('w_001CFBE0', 0x1CFBE0, [(I, 4), (I, 5), (U, 6), (U, 7), (I, 8)], None),
    ('w_001D04B0', 0x1D04B0, [(U, 4), (I, 5), (U, 6), (F, 12), (F, 13)], None),
    ('w_001EFD20', 0x1EFD20, [(I, 4), (U, 5)], None),
    ('w_001EFE00', 0x1EFE00, [(I, 4), (U, 5)], I),
    ('w_001F02C0', 0x1F02C0, [(U, 4), (I, 5), (F, 12)], None),
    ('w_001F3E30', 0x1F3E30, [(U, 4), (U, 5), (I, 6), (I, 7), (I, 8)], None),
    ('w_001F8D30', 0x1F8D30, [(U, 4), (U, 5), (U, 6), (U, 7), (F, 12), (F, 13), (F, 14), (U, 8)], None),
    ('w_001FA790', 0x1FA790, [(I, 4), (I, 5)], None),
    ('w_001FABB0', 0x1FABB0, [], None),
    ('w_001FAE70', 0x1FAE70, [(I, 4)], None),
    ('w_001FBD50', 0x1FBD50, [(U, 4), (I, 5), (I, 6), (F, 12)], I),
    ('w_0021C040', 0x21C040, [(U, 4), (U, 5)], None),
    ('w_00824060', 0x824060, [], I),
# END GENERATED HOOKS
]
BY_ADDRESS = {address: (name, args, result) for name, address, args, result in HOOKS}

# Callees that run as original code inside the oracle (writes replayed to
# the native side). Pure helpers only: none of them reaches hardware; each
# is first rehearsed with the argument registers its hook does not pass
# poisoned (Oracle.rehearse).
RUN = {0x1000E0, 0x11DE90, 0x11DF78, 0x11E2A8, 0x11E620, 0x1274B0, 0x128250, 0x128350, 0x1B1240, 0x1B12B0,
       0x1B13F0, 0x1B1470, 0x1B15D0, 0x1026A0, 0x102850, 0x1028B8, 0x1028D0, 0x102900, 0x102918, 0x102948, 0x102958,
       0x1029C0, 0x102BB0, 0x102C58}

# Jump tables (runtime address of the jump -> (table, words)), in the boot
# .rodata (checked against the ELF before any case).
JUMP_TABLES = {0x1F4554: (0x26EA00, 16), 0x1F492C: (0x26EA40, 16), 0x142124: (0x26D270, 9), 0x142A08: (0x26D2A0, 6)}
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
SOURCES = ('em_level11_port_area13.c', 'em_level11_port_boot.c', 'em_level11_port_creature.c')


def header_checks(source_dir=None):
    """The ctypes Hooks structure above is built from HOOKS; the C side's
    EmLevel11PortHooks (em_level11_port.h) and its wrappers
    (em_level11_port_internal.h) are text. Check that the header's generated
    block lists exactly the HOOKS entries, in order, with the same argument
    and result types, and that each hook has one wrapper l11_c_<address>
    that calls it and latches its own address."""
    base = Path(source_dir) if source_dir else ROOT / 'src/game'
    ctype = {U: 'uint32_t', I: 'int32_t', F: 'float', Q: 'uint64_t'}
    header = (base / 'em_level11_port.h').read_text()
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
    internal = (base / 'em_level11_port_internal.h').read_text()
    wrappers = internal.split('/* BEGIN GENERATED WRAPPERS */')[1].split('/* END GENERATED WRAPPERS */')[0]
    for hname, address, _, _ in HOOKS:
        tag = hname[2:]
        body = re.findall(r'static inline int l11_c_%s\(.*?\n\}\n' % tag, wrappers, re.S)
        if len(body) != 1 or ('o->h->%s(' % hname) not in body[0] or ('0x%08Xu' % address) not in body[0]:
            problems.append('%s: wrapper l11_c_%s missing or not calling / latching it' % (hname, tag))
    return problems, len(found)


def build(source_dir=None):
    """Compile the module (or the copies in `source_dir`, a mutation sweep)
    into a private library under build/level11/port."""
    OUT.mkdir(parents=True, exist_ok=True)
    base = Path(source_dir) if source_dir else ROOT / 'src/game'
    stem = 'level11_port' if not source_dir else 'level11_port_%d' % os.getpid()
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


def fb(value):
    return fbits(value)


def up(value):
    """The next float above `value` (bits)."""
    b = fbits(value)
    return b + 1 if value > 0 or b == 0 else b - 1


def down(value):
    b = fbits(value)
    return b - 1 if value > 0 else b + 1


# ----------------------------------------------------------------------------
# this module's capture loader and indirect-call stubs
# ----------------------------------------------------------------------------
def load_captures(elf):
    """Every image must hold AREA13, with its text equal to the user's
    extract/OVERLAY/AREA13.BIN; every image's boot text must equal the
    pinned ELF, and the boot jump tables their ELF bytes: every instruction
    the oracle executes is original. The designed-record area must be
    zero."""
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
            at = table - 0x100000 + 0x300
            assert ram[table:table + 4 * count] == elf[at:at + 4 * count], (beat, 'boot jump table differs')
        assert not any(ram[FREE:FREE + 0x40000]), (beat, 'the designed-record area is not zero')
        captures[beat] = (ram, spad)
    return captures


# the entries that reach a node's +0x4C method (directly or through a
# translated callee)
SELF_ENTRIES = {0x824390, 0x824520, 0x141D20, 0x142070}
SCRIBBLED_CALLBACKS = set()
HEAVY = {0x828500, 0x827F90, 0x828F40}


def callbacks_of(entry, native_args, ram):
    """The indirect calls the oracle stubs: the entry's own node's +0x4C
    method (for the entries that reach it)."""
    out = set(SCRIBBLED_CALLBACKS)
    if entry in SELF_ENTRIES:
        out.add(u32(ram, native_args[0] + 0x4C))
    return {c for c in out if c not in BY_ADDRESS and c not in FUNCS}


# ----------------------------------------------------------------------------
# cases
# ----------------------------------------------------------------------------
FREE = 0x1C00000          # RAM zero in every capture: the designed records live here
FULL_ONLY = set()         # labels of the cases EM_TEST_FULL=1 adds
CAPTURES = {}
FRAME = 0x70003B68
B8D = 0x70003B8D

# AREA13 nodes (ELEVENTH_LEVEL_ROUTE.md section 1.1; the same pool nodes in
# every a13c image while they live)
R44, R45 = 0x7B20F0, 0x7B23E0
WATCHERS = (0x7A8540, 0x7A8B20, 0x7A9100, 0x7A96E0)   # 0x824BB0
BLOCK45 = R45 + 0x1F0                                   # D_00275CA8 in every image
B0, B2, B3 = 'a13c_00_recharger', 'a13c_02_battery', 'a13c_03_blast'
NODE_A, NODE_B = FREE + 0x1000, FREE + 0x2000           # designed nodes (0x2F0 bytes each)


def fw(*values):
    """Little-endian words (floats given as Python floats, ints as ints)."""
    return b''.join(struct.pack('<f', v) if isinstance(v, float) else struct.pack('<I', v & MASK) for v in values)


def b8(v): return bytes([v & 0xFF])
def h16(v): return struct.pack('<H', v & 0xFFFF)
def w32(v): return struct.pack('<I', v & MASK)
def f32b(v): return struct.pack('<f', v)


def bits32(b):
    return struct.pack('<I', b & MASK)


def bones(node):
    """The frame loop points D_00275B40 at the running actor's bone array
    (actor +0x110) before its behaviour runs."""
    return [(0x275B40, w32(node + 0x110))]


def case_list(elf):
    rng = random.Random(0x1EB)
    cases = []

    def add(label, beat, entry, args, patches=(), spad=(), queues=None, default=None, run_set=RUN, full=False):
        """full=True: the case runs only with EM_TEST_FULL=1."""
        if full:
            FULL_ONLY.add(label)
        args = [STACK_TOP if k == S else a for k, a in zip(FUNCS[entry][2], list(args) + [STACK_TOP] * 8)]
        # scratchpad addresses go to the scratchpad patches (patched() takes one image)
        spad = list(spad) + [(a, v) for a, v in patches if a >= 0x70000000]
        patches = [(a, v) for a, v in patches if a < 0x70000000]
        cases.append((label, beat, entry, tuple(args), list(patches), list(spad),
                      Script(queues, default), frozenset(run_set)))

    capture_cases(add)
    captured = len(cases)
    machine_cases(add, rng)
    step_cases(add, rng)
    piece_cases(add, rng)
    boot_cases(add, rng)
    creature_cases(add, rng)
    creature_cases2(add, rng)
    bound_cases(add)
    survivor_cases(add)
    return cases, captured, len(cases) - captured


def capture_cases(add):
    """Every entry on the captured state of every image it applies to."""
    for beat in BEATS:
        ram, _ = CAPTURES[beat]
        add('capture %s 8240E0' % beat, beat, 0x8240E0, [])
        if ram[R44] and u32(ram, R44 + 0x10) == 0x823E90:
            for entry in (0x824390, 0x824520, 0x824960, 0x8249F0):
                add('capture %s %06X @%X' % (beat, entry, R44), beat, entry, [R44], bones(R44))
        for node in WATCHERS:
            rec_b = u32(ram, node + 0x110 + 0xC)
            add('capture %s 826610 @%X' % (beat, node), beat, 0x826610, [rec_b + 0x90], bones(node),
                queues={0x1AFA90: [FREE + 0x3000]})
        for node in owners(ram, 0x141D20):
            for entry in sorted(e for e in FUNCS if 0x141D20 <= e < 0x147400):
                args = [node] if entry == 0x141D20 else [node, node + 0x1F0]
                add('capture %s %06X @%X' % (beat, entry, node), beat, entry, args, bones(node))
        for t in live_tracks(ram):
            add('capture %s 118418 @%X' % (beat, t), beat, 0x118418, [t])
        assert u32(ram, 0x275CA8) == BLOCK45, beat
        for entry in (0x827C30, 0x827DD0, 0x827E00, 0x827F20, 0x827F90):
            add('capture %s %06X @%X' % (beat, entry, R45), beat, entry, [R45], bones(R45))


def chain(base, items):
    """A designed +0x18 chain: items = [(b2, b3, y)] from base, 0x100 apart;
    the head's +0x18 points at the first. Returns the patches."""
    out = []
    for i, (b2, b3, y) in enumerate(items):
        at = base + 0x100 * (i + 1)
        nxt = base + 0x100 * (i + 2) if i + 1 < len(items) else 0
        out += [(at + 2, b8(b2)), (at + 3, b8(b3)), (at + 0xB4, y if isinstance(y, bytes) else f32b(y)),
                (at + 0x18, w32(nxt)), (at + 0x36, h16(0x5A5A))]
    return [(base + 0x18, w32(base + 0x100 if items else 0))] + out


def machine_cases(add, rng):
    """[44]'s steps 2..4 (designed on a13c_02's [44], the state bytes and
    the parent patched), 0x8240E0, 0x8249F0 and the script callbacks."""
    B = B2
    parent = FREE + 0x800
    # 0x8240E0: the flag, 001B1EA0 and the y bound
    for flag in (0, 1, 0xFF):
        for r in (0, 1, 2):
            for y in (bits32(0x43520000), bits32(0x43520001), bits32(0x4351FFFF), f32b(300.0), f32b(-210.0)):
                if flag == 0xFF and (r or y != f32b(300.0)):
                    continue
                add('a13 8240E0 f%X r%d y%s' % (flag, r, y.hex()), B, 0x8240E0, [],
                    [(0x810774, b8(flag)), (0x810354, y)], queues={0x1B1EA0: [r]})
    # 0x8249F0 on designed chains
    head = FREE + 0x4000
    for label, items in (('one', [(4, 0xA, 215.0)]), ('mask', [(0xE4, 0xA, 300.0), (0x24, 0xA, 216.0)]),
                         ('below', [(4, 0xA, bits32(0x4356FFFF)), (4, 0xA, -1.0)]),
                         ('kinds', [(5, 0xA, 300.0), (4, 0xB, 300.0), (0x14, 0xA, 300.0), (3, 0xA, 300.0)]),
                         ('twelve', [(4, 0xA, 300.0)] * 12), ('thirteen', [(4, 0xA, 300.0)] * 13),
                         ('eleven', [(4, 0xA, 300.0)] * 11), ('empty', [])):
        add('a13 8249F0 %s' % label, B, 0x8249F0, [head], chain(head, items))
    # 0x824390: [44] step 2
    n = R44
    base = [(n + 0x1C, w32(parent)), (parent + 0x2A, h16(7)), (n + 6, b8(9))]
    for sub in (0, 3, 0x80):
        add('a13 824390 sub%X' % sub, B, 0x824390, [n], bones(n) + base + [(n + 5, b8(sub))])
    for t in (2, 1, 0, -5, -0x8000, 0x7FFF):
        add('a13 824390 sub1 t%d' % t, B, 0x824390, [n], bones(n) + base + [(n + 5, b8(1)), (n + 0x2A, h16(t)),
                                                                          (0x810774, b8(0xFF))],
            queues={0x824060: [0]})
    for flag, r1, y, r2 in ((0, 1, 300.0, 0), (0, 1, 100.0, 1), (0, 1, 100.0, 0), (0, 0, 300.0, 1),
                            (0xFF, 1, 300.0, 1), (0xFF, 1, 300.0, 0), (1, 1, 300.0, 0)):
        add('a13 824390 sub1 go f%X r%d y%g r%d' % (flag, r1, y, r2), B, 0x824390, [n],
            bones(n) + base + [(n + 5, b8(1)), (n + 0x2A, h16(1)), (0x810774, b8(flag)), (0x810354, f32b(y)),
                               (0x8107F4, b8(0x42)), (parent + 0x2A, h16(0x7FFF))],
            queues={0x1B1EA0: [r1], 0x824060: [r2], 0x1B17A0: [1]})
    for r in (0, 1, 2):
        add('a13 824390 sub2 r%d' % r, B, 0x824390, [n],
            bones(n) + base + [(n + 5, b8(2)), (0x8107F4, b8(0xFF)), (parent + 0x2A, h16(-1))],
            queues={0x1BA1F0: [r]})
    # 0x824520: [44] step 3
    for sub in (0, 1, 2, 3, 4):
        for count in ((1, 2, 3, 4, 5, -1, 0x7FFF) if sub < 3 else (0,)):
            for six in ((0, 1, 2) if sub == 0 and count >= 2 else (0,)):
                for r in ((0, 1) if sub == 3 or (sub == 0 and six == 0 and count >= 2) else (0,)):
                    add('a13 824520 sub%d c%d s%d r%d' % (sub, count, six, r), B, 0x824520, [n],
                        bones(n) + [(n + 0x1C, w32(parent)), (parent + 0x28, h16(count)), (n + 5, b8(sub)),
                                    (n + 6, b8(six)), (0x8107F4, b8(0xFF))],
                        queues={0x1BA1F0: [r, r]})
    # 0x824960: [44] step 4
    for sub in (0, 1, 2):
        for f833 in ((0, 1, 0xFF) if sub == 0 else (0,)):
            for r in ((0, 1) if sub == 1 else (0,)):
                add('a13 824960 sub%d f%X r%d' % (sub, f833, r), B, 0x824960, [n],
                    [(n + 5, b8(sub)), (0x810833, b8(f833)), (n + 4, b8(1))], queues={0x1BA1F0: [r]})
    # the script callbacks 0x8246D0 / 0x8248C0 on a designed self / block
    s, blk = NODE_A, NODE_A + 0x1F0
    for entry in (0x8246D0, 0x8248C0):
        for st in (0, 1, 2):
            for t in ((0.0, 44.0, 45.0, 44.5, 49.0, 49.5, 50.0, -3.0) if st == 1 else (7.0,)):
                add('a13 %06X s%d t%g' % (entry, st, t), B, entry, [0x1234, s, blk],
                    [(s + 4, b8(st)), (blk + 0x10, f32b(t)), (0x810350, fw(700.0, 214.0, 1200.0))])


def step_cases(add, rng):
    """[45]'s step functions on its node and block (a13c_02, the block's
    words patched)."""
    B = B2
    n, blk = R45, BLOCK45
    for count in (0, 1, 0x63, 0x64, 0xC8, 0x12C, 0x190, 0x1DE, 0x1DF, 0x1E0, 0x1F3, 0x30C, -1):
        add('a13 827C30 c%X' % (count & MASK), B, 0x827C30, [n],
            [(blk + 4, w32(count)), (blk + 12, w32(4)), (blk + 24, w32(9)), (n + 0x28, h16(0x7FFF)),
             (0x8107F4, b8(0xD2))])
    for v in (0, 1, 0x8000, -1):
        add('a13 827DD0 v%X' % (v & 0xFFFF), B, 0x827DD0, [n], [(n + 0x2A, h16(v))])
    for count in (0, 0x5A, 0x96, 1, 0x95, -1):
        add('a13 827E00 c%X' % (count & MASK), B, 0x827E00, [n],
            [(blk + 4, w32(count)), (blk + 12, w32(0)), (n + 0x28, h16(-1))])
    for count in (0, 0xB4, 0xB5, -1, 0x7FFFFFFF):
        add('a13 827F20 c%X' % (count & MASK), B, 0x827F20, [n], [(blk + 4, w32(count))])
    for count in (0, 45, 20, 90, 60, 1, -45, -20):
        for rnd in ((0, 1, 2, 5, -1) if count == 0 else (0x12345678,)):
            for x3c, r, y in (((10.0, 1, 209.0), (bits32(0x41200001), 1, 209.0), (4.0, 1, 210.0), (3.0, 0, 0.0),
                               (20.0, 1, 0.0)) if count in (0, 45) and rnd in (0, 0x12345678) else ((20.0, 0, 0.0),)):
                xb = x3c if isinstance(x3c, bytes) else f32b(x3c)
                add('a13 827F90 p0 c%d r%X x%s %d y%g' % (count, rnd & MASK, xb.hex(), r, y), B, 0x827F90, [n],
                    bones(n) + [(blk + 16, w32(0)), (blk + 4, w32(count)), (n + 0x3C, xb), (0x810354, f32b(y)),
                                (0x8104D0, f32b(37.0)), (0x8104D4, f32b(-1.0)), (0x8102B0, b8(0x41))],
                    queues={0x1B1EA0: [r]}, default={0x122BB8: rnd})
    for b5 in (0, 1, 30, 31, -1):
        add('a13 827F90 p1 b%d' % b5, B, 0x827F90, [n], bones(n) + [(blk + 16, w32(1)), (blk + 20, w32(b5)),
                                                                     (n + 5, b8(0xFF))])
    add('a13 827F90 p2', B, 0x827F90, [n], bones(n) + [(blk + 16, w32(2))])


def piece_cases(add, rng):
    """The thrown piece 0x828500, its method 0x8284E0, the effects 0x828C60 /
    0x828E10 / 0x828F40 and the spawn 0x826610 on designed nodes."""
    B = B3
    s, blk = NODE_A, NODE_A + 0x1F0
    for other in (0, 2, 3, 0xFD):
        add('a13 8284E0 o%X' % other, B, 0x8284E0, [s, NODE_B], [(NODE_B, b8(other)), (s + 4, b8(1))])
    pos = [(s + 0xB0, fw(700.0, 170.0, 1100.0, 1.0)), (s + 0xC0, fw(0.25, -0.5, 1.0, 0.0)),
           (0x810360, fw(690.0, 160.0, 1050.0, 1.0))]
    hit = FREE + 0x6000
    probe = Scribble(1, [(0x700031B0, fw(701.0, 150.5, 1101.0, 1.0)), (0x700031D0, w32(hit))])
    for d in (6, 7, 8, 9, 10, 11):
        add('a13 828500 s0 d%d' % d, B, 0x828500, [s], pos + [(s + 4, b8(0)), (s + 0xD, b8(d)),
                                                               (hit + 0x24, fw(0.0, 1.0, 0.0))],
            queues={0x19A570: [0, probe], 0x1CA7B0: [3], 0x1C6120: [0x4400]}, default={0x122BB8: 0x40000000})
    for b8v, a9a0 in ((2, 2), (2, 1), (1, 2)):
        add('a13 828500 s1 quit %d %d' % (b8v, a9a0), B, 0x828500, [s],
            pos + [(s + 4, b8(1)), (s + 0xD, b8(10)), (0x8106B8, b8(b8v)), (0x28A9A0, h16(a9a0)),
                   (blk + 0x24, w32(10))])
    for f833 in (0, 0xFF):
        for d in ((7, 8, 9) if f833 else (7,)):
            for life, ang, t in ((10, 0.0, 0.25), (0, 177.0, 0.25), (1, 178.0, 0.25), (10, -180.0, 0.5),
                                 (10, 0.0, 0.6), (10, 180.0, 1.49), (10, 0.0, 1.4916667)):
                for p1, p2, h in (((0, 0, 3), (1, 0, 3), (0, 1, -1), (0, 1, 0)) if t > 0.5 else ((0, 0, 3), (0, 1, -1))):
                    add('a13 828500 s1 f%X d%d l%d a%g t%g %d%d h%d' % (f833, d, life, ang, t, p1, p2, h), B,
                        0x828500, [s],
                        pos + [(s + 4, b8(1)), (s + 0xD, b8(d)), (0x810833, b8(f833)), (blk + 0x24, w32(life)),
                               (blk + 0x28, f32b(ang)), (blk + 0x2C, f32b(t)), (blk, fw(700.0, 170.0, 1100.0, 1.0)),
                               (blk + 0x10, fw(650.0, 160.0, 1000.0, 1.0)), (blk + 0x20, w32(0x16)),
                               (hit + 0x24, fw(0.0, 1.0, 0.0))],
                        queues={0x19A570: [probe if p1 else 0, probe if p2 else 0], 0x1CA7B0: [h],
                                0x1C6120: [0x4400]})
    for d in (6, 7, 8, 9, 10, 11):
        add('a13 828500 s2 d%d' % d, B, 0x828500, [s], pos + [(s + 4, b8(2)), (s + 0xD, b8(d))])
    for st in (3, 4):
        add('a13 828500 s%d' % st, B, 0x828500, [s], pos + [(s + 4, b8(st))])
    # the effects
    for st in (0, 1, 2, 3, 4):
        for w0, w1 in (((0.0, 0.1), (1.79, 0.006), (1.7, 0.1), (1.795, 0.004)) if st == 1 else ((0.0, 0.0),)):
            add('a13 828C60 s%d %g %g' % (st, w0, w1), B, 0x828C60, [s],
                pos + [(s + 4, b8(st)), (s + 0x1F0, fw(w0, w1, 0.5))], queues={0x1CCF70: [0x1234]},
                default={0x122BB8: 0x4000})
        for w0 in ((0.0, 1.492, 1.5, 1.4) if st == 1 else (0.0,)):
            add('a13 828E10 s%d %g' % (st, w0), B, 0x828E10, [s], pos + [(s + 4, b8(st)), (s + 0x1F0, fw(w0, 0.25))],
                default={0x122BB8: 0x7FFFFFFF})
        for t, step, cnt in (((0.0, 0.1, 0), (1.4, 0.2, 40), (1.45, 0.02, 41), (1.49, 0.009, 7), (0.5, 0.01, -1))
                             if st == 1 else ((0.0, 0.0, 0),)):
            add('a13 828F40 s%d %g %g %d' % (st, t, step, cnt), B, 0x828F40, [s],
                pos + [(s + 4, b8(st)), (s + 0x14, w32(NODE_B)), (blk + 0x14, w32(0x9ABCDEF0)),
                       (blk + 0x18, w32(cnt)), (blk + 0x1C, f32b(t)), (blk + 0x20, f32b(step))],
                queues={0x1CCF70: [0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17]},
                default={0x122BB8: 0x13579BDF})
    # 0x826610: 001AFA90 gives no object, or one
    ram, _ = CAPTURES[B]
    node = WATCHERS[0]
    rec_b = u32(ram, node + 0x110 + 0xC)
    for obj in (0, FREE + 0x3000):
        add('a13 826610 o%X' % obj, B, 0x826610, [rec_b + 0x90], [], queues={0x1AFA90: [obj]})


TRACKS = 0x27E0C0                  # the sequencer's 0x30 tracks, 0x78 bytes each
CHANNELS = 0x27CCC0                # 48 channel records, 0x6A bytes each


def live_tracks(ram):
    """The sequencer tracks with a cursor (+8) set."""
    return [TRACKS + 0x78 * i for i in range(0x30) if u32(ram, TRACKS + 0x78 * i + 8)]


def dbl(x):
    return struct.unpack('<Q', struct.pack('<d', x))[0]


def boot_cases(add, rng):
    """The boot functions outside the actor family."""
    B = B3
    ram3, _ = CAPTURES[B]
    # 001F9140
    for f in (7.0, -0.0, 1e-30):
        add('boot 1F9140 %g' % f, B, 0x1F9140, [0x810360, 0x700031B0, 0x700038A0, FREE + 0x5000, fb(f)])
    # 00100130: the soft-double compare run as original, then stubbed results
    for x, y in ((0.8, 0.8), (0.9, 0.8), (0.7, 0.8), (-1.0, 0.8), (0.0, -0.0), (1e300, 0.8)):
        add('boot 100130 %r %r' % (x, y), B, 0x100130, [dbl(x), dbl(y)])
    for v in (0, 1, -1, 0x80000000, 0xFFFFFFFF80000000, 0x7FFFFFFF00000000, 0x8000000000000000):
        add('boot 100130 stub %X' % v, B, 0x100130, [dbl(0.5), dbl(0.8)], queues={0x1274B0: [v]},
            run_set=RUN - {0x1274B0})
    # 00118418: the note-on path on designed tracks / channels (every live
    # track as captured is a capture case)
    t = TRACKS
    stream = u32(ram3, 0x281AC0 + 0x14)
    cur = 0x200
    q = stream + cur
    for kind in (1, 0, 2):
        for match in ((0, 1, 2, 3, 4, 5) if kind == 1 else (0, 1, 2, 3, 4, 5, 6, 7)):
            ch = CHANNELS + 0x6A * 21
            pat = [(t + 0x34, h16(kind)), (t + 8, w32(cur)), (t + 0x24, h16(4)), (t + 0x18, w32(0)), (t, b8(0xB3)),
                   (q, bytes([0x90, 0x41, 0x37, 0x52, 0x08, 0x28]))]
            if kind == 1:
                c = [(ch, h16(1)), (ch + 0x1A, h16(2)), (ch + 0x22, h16(4)), (ch + 0x3E, h16(8)), (ch + 2, h16(0x28)),
                     (ch + 6, h16(0)), (ch + 0x4C, h16(0x1234))]
                bad = {1: (ch, h16(2)), 2: (ch + 0x1A, h16(1)), 3: (ch + 0x3E, h16(9)), 4: (ch + 2, h16(0x27)),
                       5: (ch + 6, h16(1))}
            else:
                c = [(ch, h16(1)), (ch + 0x1A, h16(1)), (ch + 0x22, h16(4)), (ch + 4, h16(3)), (ch + 6, h16(0)),
                     (ch + 8, h16(0)), (ch + 0xE, h16(0x44))]
                bad = {1: (ch, h16(2)), 2: (ch + 0x1A, h16(2)), 3: (ch + 4, h16(4)), 4: (ch + 8, h16(1)),
                       5: (ch + 6, h16(5)), 6: (ch + 0x22, h16(5)), 7: (t + 0x18, w32(0x10000))}
            if match:
                c = c + [bad[match]]
            add('boot 118418 k%d m%d' % (kind, match), B, 0x118418, [t], pat + c,
                queues={0x117BA0: [0x47], 0x1179E0: [0x12345678], 0x1157F0: [0]})
    add('boot 118418 k0 neg', B, 0x118418, [t],
        [(t + 0x34, h16(0)), (t + 8, w32(cur)), (t + 0x24, h16(4)), (t + 0x18, w32(0)), (t, b8(0x03)),
         (q, bytes([0x90, 0x41, 0x37])), (CHANNELS + 0x6A * 7, h16(1)), (CHANNELS + 0x6A * 7 + 0x1A, h16(1)),
         (CHANNELS + 0x6A * 7 + 0x22, h16(4)), (CHANNELS + 0x6A * 7 + 4, h16(3))],
        queues={0x117BA0: [-5], 0x1179E0: [-0x10000]})
    # 0019A6F0 on a designed object and segment
    obj, seg = FREE + 0x7000, FREE + 0x7100
    hit = FREE + 0x7200
    for flags in (0, 1, 2, 3, 4, 7, 0x100):
        for b0, b2, b52, t0 in (((1, 0, 0, 0x40), (1, 3, 2, 0x40), (1, 3, 2, 0x41), (1, 3, 0, 0x40), (1, 0, 1, 0x40),
                                 (0, 0, 0, 0x40), (1, 3, 3, 0x10040)) if flags & 1 else ((1, 0, 0, 0x40),)):
            for r1, h52 in (((0, 0), (1, 0), (1, 2)) if flags & 1 else ((0, 0),)):
                for r2, r3 in (((0, 0), (1, 0), (0, 1), (1, 1)) if flags & 6 else ((0, 0),)):
                    add('boot 19A6F0 f%X o%X%X%X t%X r%d%X %d%d' % (flags, b0, b2, b52, t0, r1, h52, r2, r3), B,
                        0x19A6F0, [obj, seg, seg + 0x10, flags, t0],
                        [(obj, b8(b0)), (obj + 2, b8(b2 | 0xE0)), (obj + 0x52, h16(b52)), (obj + 0x14, w32(0xABCD)),
                         (seg, fw(1.0, 2.0, 3.0, 9.0, 4.0, 5.0, 6.0, 9.0)), (hit + 0x52, h16(h52)),
                         (0x700031B0, fw(7.5, 8.5, 9.5, 1.0))],
                        queues={0x1A6440: [Scribble(r1, [(0x700031D4, w32(hit))])],
                                0x1A7280: [Scribble(r1, [(0x700031D4, w32(hit))])], 0x1A0B10: [r2],
                                0x19D330: [r3]})
    # 001B2E50 on designed tables
    a0 = FREE + 0x7300
    out = FREE + 0x7400
    for count, x, flags in ((0, 5.0, (1, 1, 1)), (3, 0.5, (1, 0, 0)), (3, 0.5, (0, 0, 0)), (3, 1.0, (1, 1, 1)),
                            (3, 1.5, (0, 1, 1)), (3, 2.0, (0, 0, 1)), (3, 9.0, (0, 0, 0)), (3, 9.0, (1, 0, 1)),
                            (3, 9.0, (0, 1, 0)), (3, 9.0, (0, 0, 1)), (1, 9.0, (0, 1, 1)), (1, 0.5, (1, 1, 1)),
                            (-1, 9.0, (0, 0, 0)), (-1, 0.5, (1, 0, 0)), (2, 2.0, (1, 1, 0))):
        add('boot 1B2E50 n%d x%g f%s' % (count, x, ''.join(map(str, flags))), B, 0x1B2E50, [a0, out],
            [(a0 + 4, f32b(x)), (0x700031E0, w32(count)), (0x700030EC, fw(0.25, 1.0, 2.0, 3.0)),
             (0x7000316E, h16(1) + b''.join(h16(f | 0x10) for f in flags)), (out, w32(0x5A5A5A5A))])
    # 001B3F10 on a designed actor
    me = FREE + 0x8000
    for kind in (1, 7, 6, 5, 2):
        for near in (0.0, 2.0, 1.0303768, 0.9, -2.5, 2.9):
            add('boot 1B3F10 k%d b%g' % (kind, near), B, 0x1B3F10, [me, fb(near), fb(30.0)],
                [(me + 3, b8(kind)), (me + 0xB0, fw(700.0, 160.0, 1000.0, 1.0)), (0x8102B4, b8(1)),
                 (0x81050C, b8(0x12)), (0x810350, fw(705.0, 165.0, 1003.0, 1.0)), (0x810360, fw(705.0, 165.0, 1003.0, 1.0))],
                queues={0x19D330: [0]})
    for b4, mode, dy, height, hitflag in ((0, 0x10, 5.0, 30.0, 0), (1, 0x10, 5.0, 30.0, 0), (1, 0x10, 40.0, 30.0, 0),
                                          (1, 0x10, 30.0, 30.0, 0), (1, 0x10, 5.0, 30.0, 0x2000),
                                          (1, 0x10, 5.0, 30.0, 0xDFFF)):
        hitobj = FREE + 0x8100
        add('boot 1B3F10 b%d dy%g h%g f%X' % (b4, dy, height, hitflag), B, 0x1B3F10, [me, fb(0.0), fb(height)],
            [(me + 3, b8(1)), (me, b8(0)), (me + 0xB0, fw(700.0, 160.0, 1000.0, 1.0)), (0x8102B4, b8(b4)),
             (0x81050C, b8(3)), (0x810350, fw(705.0, 160.0 + dy, 1003.0, 1.0)),
             (0x810360, fw(705.0, 160.0 + dy, 1003.0, 1.0)), (hitobj + 0x1A, h16(hitflag)), (0x700031D0, w32(hitobj))],
            queues={0x19D330: [1 if hitflag else 0]})
    add('boot 1B3F10 far', B, 0x1B3F10, [me, fb(0.0), fb(30.0)],
        [(me + 3, b8(1)), (me + 0xB0, fw(700.0, 160.0, 1000.0, 1.0)), (0x8102B4, b8(1)), (0x81050C, b8(0)),
         (0x810360, fw(900.0, 160.0, 1000.0, 1.0))])
    # 001E4610 (the ring emitter) on a designed node
    s, other = NODE_A, NODE_B
    for st in (0, 1, 2, 3, 4):
        for d in ((0, 1, 2, 0x80) if st == 0 else (0,)):
            for t in ((0.5, 1.0, 1.0000001, 1.3, 1.8, 1.99, 2.0) if st == 1 else (0.0,)):
                add('boot 1E4610 s%d d%d t%r' % (st, d, t), B, 0x1E4610, [s],
                    [(s + 4, b8(st)), (s + 0x14, w32(other)), (s + 0xD, b8(d)), (s + 0x1F0 + 0xD, b8(d ^ 1)),
                     (s + 0x1F0 + 0x18, f32b(t)),
                     (s + 0x1F0 + 0x14, f32b(25.0)), (s + 0x1F0 + 0x28, f32b(5.0)),
                     (s + 0xB0, fw(1.0, 2.0, 3.0, 1.0)), (s + 0x100, fw(10.0, 20.0, 30.0, 1.0))])
    # 001CE660 on designed bands (001CDDC0 a hook here)
    mtx, band = FREE + 0xA000, FREE + 0xA100
    for steps in (0, 1, 2, 3, 20, -1):
        for m in ((1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 700.0, 160.0, 1000.0, 1.0),
                  (0.5, -0.25, 2.0, 0.0, 1.5, 0.75, -1.0, 0.0, -0.125, 3.0, 0.5, 0.0, 10.0, -20.0, 30.0, 1.0)):
            add('boot 1CE660 n%d m%g' % (steps, m[1]), B, 0x1CE660, [0, 2, mtx, band, 0x20041A85553220D0, 0x80FFFFFF],
                [(mtx, fw(*m)), (band, fw(steps, 0.0, 360.0, 12.5, 12.5, 0.0, 17.5, 17.5, 0.0)),
                 (0x70003400, fw(*([9.0] * 16)))])
    add('boot 1CE660 arc', B, 0x1CE660, [3, 1, mtx, band, 0x1234, 0x7F102030],
        [(mtx, fw(1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 700.0, 160.0, 1000.0, 1.0)),
         (band, fw(5, -45.0, 90.0, 3.0, 4.0, 1.5, 6.0, 8.0, -2.5))])
    # 001CDDC0 on designed corners, camera rows, fog and clipper results
    corners = FREE + 0xB000
    ctxblk = FREE + 0xB800
    vp = (1.5, 0.25, 0.0, 0.0, -0.5, 2.0, 0.125, 0.0, 0.75, 0.5, 1.0, 1.0, 320.0, 224.0, 10.0, 50.0)
    clip_in = [(0x8117C0 + 0x50 * i, fw(10.0 * i, 5.0 - i, 2.5 * i, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                                         0.25 * i, 1.0 - 0.25 * i)) for i in range(5)]
    for mode in (0, 1, 2, 3, 4, 5):
        for rgba in (0x80FFFFFF, 0x10203040):
            for fogs in ((255.0, 2048.0, 151.1, -0.497), (255.0, 0.0, 300.0, 2.0), (255.0, 0.0, -50.0, 0.0),
                         (300.0, 0.0, 280.0, 0.0), (-10.0, 0.0, 5.0, 0.0), (4096.0, 0.0, 1e9, 0.0)):
                for n1, n2 in (((3, 4), (0, 3), (0, 0)) if mode in (0, 2) and rgba == 0x80FFFFFF else ((3, 4),)):
                    add('boot 1CDDC0 m%d c%X f%g n%d%d' % (mode, rgba, fogs[2], n1, n2), B, 0x1CDDC0,
                        [1, mode, corners, 0x20041A85553220D0, rgba],
                        [(corners, fw(1.0, 2.0, 3.0, 1.0, 4.0, 5.0, 6.0, 1.0, 7.0, 8.0, 9.0, 1.0, 10.0, 11.0, 12.0, 1.0)),
                         (0x275670, w32(ctxblk)), (ctxblk + 0xA0, fw(*fogs)), (0x70003AC0, fw(*vp))] + clip_in,
                        queues={0x1CCF70: [5, 9, 7, 3], 0x1CD370: [0x123450, 0x123450], 0x1CF470: [n1, n2],
                                0x1CB5F0: [FREE + 0xC000, FREE + 0xC400]})
    # 001F4840 / 001F4190 on a designed node (0x25A350's real per-kind records)
    for st in (0, 1, 2, 3, 4):
        for kind in ((0x0A, 0x0B, 0x0F, 0x10, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B) if st == 0 else
                     (0x0B, 0x0F, 0x14, 0x15) if st == 1 else (0x0B,)):
            for ticks, life in (((0, 1), (1, 1), (5, 0x3C)) if st == 1 else ((0, 0),)):
                add('boot 1F4840 s%d k%X t%d l%d' % (st, kind, ticks, life), B, 0x1F4840, [s],
                    [(s + 4, b8(st)), (s + 0xD, b8(kind)), (s + 0x1F0, w32(0x2468ACE1)), (s + 0x1F4, w32(life)),
                     (s + 0x1F8, w32(ticks)), (s + 0x100, fw(700.0, 170.0, 1100.0, 1.0)), (s + 0x104, f32b(3.0))],
                    default={0x122BB8: 0x13572468})
    cfg = FREE + 0x9000
    for kind in list(range(10, 28)):
        for count, age, life in (((1, 3, 30),) if kind not in (11, 21) else ((0, 0, 1), (2, 3, 30), (2, 15, 30), (1, 25, 30))):
            add('boot 1F4190 k%d n%d a%d l%d' % (kind, count, age, life), B, 0x1F4190, [s, s + 0x1F0, cfg],
                [(s + 0xD, b8(kind)), (s + 0x1F0, w32(0x89ABCDEF)), (s + 0x1F4, w32(life)), (s + 0x1F8, w32(age)),
                 (s + 0x100, fw(700.0, 170.0, 1100.0, 1.0)),
                 (cfg, fw(2.0, 3.0, 4.0, 0.0, 1.5, 2.5, 3.5, 0.0, 0.25, 0.5, 0.75, 0.0, 0x111, 0x222, 0x333, 0x444,
                          0.5, 0.25, -0.05, count, 0x51, 0x54, 0x58, 0x5C))])


ACTOR = 0x7AAB70        # a13c_05 / a13c_06: an 0x141D20 actor (ELEVENTH_LEVEL_ROUTE.md section 2, [7]'s group)
B5 = 'a13c_05_boom'


def bge(flags, floor, hit=None):
    """A 001B2BF0 stub result that writes the floor 0x700038D0 (and the hit
    object 0x700031D0)."""
    writes = [(0x700038D0, f32b(floor))]
    if hit is not None:
        writes.append((0x700031D0, w32(hit)))
    return Scribble(flags, writes)


def creature_cases(add, rng):
    """The 0x141D20 actor family on a13c_05's actor, its state bytes and
    behaviour block patched; the callees' results on both sides of every
    test."""
    B = B5
    e = ACTOR
    d = e + 0x1F0
    hitobj = FREE + 0xD000
    base = bones(e) + [(hitobj + 0x24, fw(701.0, 150.0, 1031.0)), (hitobj + 0x1A, b8(0x10))]
    # 00141D20: the pause byte, the state and the countdowns
    for b8d in (0, 1, 2, 3, 4, 5):
        for st in ((0, 1, 2, 3, 4) if b8d == 0 else (2,) if b8d != 1 else (0, 1)):
            for r in ((0, 1) if b8d == 1 else (1,)):
                add('actor 141D20 b%d s%d r%d' % (b8d, st, r), B, 0x141D20, [e],
                    base + [(B8D, b8(b8d)), (e + 4, b8(st)), (d + 0x64, h16(1)), (d + 0x62, h16(0)), (d + 0x73, b8(2)),
                            (d + 0x7D, b8(0)), (d + 0x72, b8(0xFF)), (d + 0x71, b8(0)), (d + 0x60, h16(-1)),
                            (d + 0x7E, b8(0))],
                    queues={0x1B2140: [r, 0]})
    for c in (0, 1):
        for area in (0xD, 0xC):
            add('actor 141D20 counters %d a%X' % (c, area), B, 0x141D20, [e],
                base + [(B8D, b8(0)), (e + 4, b8(2)), (0x810700, b8(area))]
                + [(d + o_, h16(c * 0x8000 + c) if n == 2 else b8(c * 0x80 + c))
                   for o_, n in ((0x64, 2), (0x62, 2), (0x73, 1), (0x7D, 1), (0x72, 1), (0x71, 1), (0x60, 2))]
                + [(d + 0x7E, b8(1 - c + 0x80 * c))])
    # 00141F00: the spawn step
    for f808 in (0, 0xFF):
        for dd in (0, 0x80):
            for f70a in (0, 1):
                for r in (0, 1):
                    add('actor 141F00 f%X d%X a%d r%d' % (f808, dd, f70a, r), B, 0x141F00, [e, d],
                        base + [(e + 4, b8(0xFF)), (0x810808, b8(f808)), (e + 0xD, b8(dd | 3)), (0x81070A, b8(f70a))],
                        queues={0x1B10B0: [r]})
    # 00142070: the per-frame update over every behaviour
    for r in (0, 1):
        for beh in ((3, 4, 5, 6, 7, 8, 9, 0xFF) if r else (3,)):
            for flags in ((0, 0, 9), (1, 1, 8), (0, 1, 7), (1, 0, 9)):
                d72, d78, clip = flags
                for rnd in ((0x12345678, -0x2000) if clip in (8, 9) else (0,)):
                    add('actor 142070 r%d b%d %d%d c%d %X' % (r, beh, d72, d78, clip, rnd & MASK), B, 0x142070, [e, d],
                        base + [(e + 5, b8(beh)), (d + 0x72, b8(d72)), (d + 0x78, b8(d78)), (d + 0x30, w32(clip)),
                                (d + 0x40, f32b(1.0))],
                        queues={0x1B2140: [r], 0x1C64F0: [0x5D]}, default={0x122BB8: rnd})
    # 00142330: behaviour 0
    for st in (0, 2):
        for rnd in ((0, 0x80000, 0x7FFFFFFF) if st == 0 else (0,)):
            add('actor 142330 s%d r%X' % (st, rnd), B, 0x142330, [e, d], base + [(e + 6, b8(st))],
                default={0x122BB8: rnd})
    for clip, t3c in ((1, 80.0), (0x8001, 80.0), (1, 79.0), (2, 80.0)):
        for z, d30, d64, d78, d60 in ((0x80, 0x1000, 0, 0, 0), (0x80, 0, 0, 1, 0), (0, 0x1000, 0, 0, 0),
                                     (0, 0, 1, 0, 0), (1, 0, 0, 0, 0), (0, 0, 0, 1, 0), (0, 0, 0, 1, 5), (0, 0, 0, 0, 0)):
            add('actor 142330 s1 c%X t%g z%X %X %d %d %d' % (clip, t3c, z, d30, d64, d78, d60), B, 0x142330, [e, d],
                base + [(e + 6, b8(1)), (e + 0x2C, h16(clip)), (e + 0x3C, f32b(t3c)), (d + 0x7F, b8(z)),
                        (d + 0x30, w32(d30)), (d + 0x64, h16(d64)), (d + 0x78, b8(d78)), (d + 0x60, h16(d60)),
                        (0x8102B4, b8(0))])
    # 001469B0: the alert
    for d64 in (1, 0):
        for z in (0, 1):
            for a, d5c, cnt in (((0, 50.0, 0), (0, 150.0, 0xB3), (0, 150.0, 0xB2), (0, 150.0, 0xFF))
                                if d64 else ((1, 0.0, 0), (0, 0.0, 0), (0, 0.0, 5))):
                for r3f in ((0, 1) if not d64 and not a and not z else (0,)):
                    add('actor 1469B0 a%d z%d e%d %g c%X f%d' % (d64, z, a, d5c, cnt, r3f), B, 0x1469B0, [e, d],
                        base + [(d + 0x64, h16(d64)), (d + 0x7F, b8(z)), (e + 0xA, b8(a)), (d + 0x5C, f32b(d5c)),
                                (d + 0x70, b8(cnt)), (0x8102B4, b8(r3f)), (0x81050C, b8(2)),
                                (0x810360, fw(701.0, 150.0, 1031.0, 1.0))],
                        queues={0x19D330: [0]})
    # 00146CE0: the hit counter
    for hp, hit, d72, d7b, d73, d7d, b4, d66, d74 in (
            (0, 5, 0, 0, 0, 0, 1, 0, 0), (100, 0, 0, 0, 0, 0, 1, 0, 0), (100, 5, 1, 0, 0, 0, 1, 0, 0),
            (100, 5, 0, 1, 0, 0, 1, 0, 0), (100, 5, 0, 0, 0, 0, 1, 0, 0), (100, 5, 0, 0, 3, 0, 1, 20, 0),
            (100, 0x4005, 0, 0, 0, 0, 1, 0, 0), (100, 0x4005, 0, 0, 0, 5, 1, 0, 0), (100, 0x8005, 0, 0, 0, 0, 1, 0, 2),
            (100, 0x8005, 0, 0, 0, 0, 0, 0, 0), (100, 0x2005, 0, 0, 0, 0, 1, 0, 0), (100, 0x1005, 0, 0, 0, 0, 0, 0, 0),
            (10, 5, 0, 0, 0, 0, 1, 0, 0), (25, 5, 0, 0, 0, 0, 1, 0, 2), (25, 0xA005, 0, 0, 0, 0, 1, 0, 2),
            (5, 0x2005, 0, 0, 0, 0, 1, 0, 2), (100, 5, 0, 0, 1, 0, 1, 24, 0), (100, 5, 0, 0, 1, 0, 1, 25, 0),
            (100, 0xFFF, 0, 0, 0, 0, 2, 0, 0), (0x7FFF, 0xCFFF, 0, 0, 0, 0, 2, 0, 0)):
        for f6bd in ((0, 1) if d7b else (0,)):
            add('actor 146CE0 h%X x%X %d%d%d%d s%d %d %d f%d' % (hp, hit, d72, d7b, d73, d7d, b4, d66, d74, f6bd), B,
                0x146CE0, [e, d],
                base + [(e + 0x34, h16(hp)), (e + 0x36, h16(hit)), (d + 0x72, b8(d72)), (d + 0x7B, b8(d7b)),
                        (d + 0x73, b8(d73)), (d + 0x7D, b8(d7d)), (e + 4, b8(b4)), (d + 0x66, h16(d66)),
                        (d + 0x74, b8(d74)), (0x8106BD, b8(f6bd))])
    # 001471E0: the zone
    for area, z, r1, r2, r3 in ((0xD, 980.0, 0, 0, 0), (0xD, 970.0, 0, 0, 0), (0xD, 969.0, 1, 0, 0),
                                (0xD, 900.0, 0, 1, 0), (0xD, 900.0, 0, 0, 1), (0xD, 900.0, 0, 0, 0),
                                (0xC, 1041.04, 0, 0, 1), (0xC, 1041.0, 0, 0, 0), (0xC, 2000.0, 0, 0, 0)):
        add('actor 1471E0 a%X z%g %d%d%d' % (area, z, r1, r2, r3), B, 0x1471E0, [e, d],
            base + [(0x810700, b8(area)), (0x810368, f32b(z)), (d + 0x7F, b8(0x55))],
            queues={0x1B1EA0: [r1, r2], 0x1B13F0: [r3]}, run_set=RUN - {0x1B13F0})
    # 00145880: the fall and the run
    for d75, d5c, r in ((1, 0.0, 0), (0, 0.0, 1), (2, 18.0, 1), (2, 19.0, 1), (0x82, 0.0, 1), (2, 5.0, 0), (3, 0.0, 1)):
        add('actor 145880 f%X %g r%d' % (d75, d5c, r), B, 0x145880, [e, d],
            base + [(d + 0x75, b8(d75)), (d + 0x5C, f32b(d5c)), (d + 0x48, f32b(0.5)), (d + 0x4C, f32b(1.0)),
                    (d + 0x50, f32b(-0.25))], queues={0x1B1560: [r]})
    # 001459A0: the probes
    for d4c in (1.0, -1.0):
        for lean in (0.5, 0.0, -0.1, -0.2, -1.0):
            for area in (0xD, 0xC):
                for hits in ((0,), (1,), (0, 0, 1), (0, 0, 0, 0, 0), (0, 0, 0, 1), (0, 0, 0, 0, 1)):
                    if area == 0xC and hits not in ((0,), (0, 0, 0, 0, 0)):
                        continue
                    add('actor 1459A0 s%g l%g a%X h%s' % (d4c, lean, area, ''.join(map(str, hits))), B, 0x1459A0, [e, d],
                        base + [(d + 0x4C, f32b(d4c)), (d + 0x48, f32b(lean)), (0x810700, b8(area)), (e + 4, b8(1)),
                                (d + 0x7A, b8(0))],
                        queues={0x1B32F0: list(hits), 0x1B3390: list(hits[1:]), 0x1B3440: [0, 0, 1] if lean < -0.5 else [],
                                0x1B2F70: [Scribble(1, [(0x700038A0, f32b(100.0))])], 0x19AB20: [1 if lean > 0.2 else 0]})
    for e4, d7a, ground, lean in ((1, 0, 0, -1.0), (1, 0, 1, -1.0), (1, 0, 1, -0.4), (2, 0, 1, -1.0), (1, 1, 1, -1.0),
                                  (1, 0, 2, -1.0)):
        add('actor 1459A0 floor e%d a%d g%d l%g' % (e4, d7a, ground, lean), B, 0x1459A0, [e, d],
            base + [(d + 0x4C, f32b(1.0)), (d + 0x48, f32b(lean)), (e + 4, b8(e4)), (d + 0x7A, b8(d7a))],
            queues={0x1B32F0: [0], 0x1B3390: [0, 0, 0, 0], 0x1B3440: [0] * 5,
                    0x1B2F70: [Scribble(1 if ground else 0, [(0x700038A0, f32b(100.0 if ground == 1 else 140.0))])]})
    # 001464B0 / 00146110: the forward and step probes
    for flags, floor, kind, r19, d5c, r60, r67 in (
            (0, 0.0, 0x10, 0, 50.0, 0, 7), (4, 0.0, 0x10, 0, 50.0, 0, 7), (4, 0.0, 0x10, 0, 5.0, 1, 7),
            (4, 0.0, 0x10, 0, 5.0, 0, 7), (5, 500.0, 0x10, 0, 50.0, 0, 7), (5, 0.0, 0x46, 0, 50.0, 0, 7),
            (5, 0.0, 0x10, 0, 50.0, 0, 7), (5, 0.0, 0x10, 1, 50.0, 0, 7), (1, 500.0, 0x10, 0, 50.0, 0, 7),
            (1, -500.0, 0x10, 0, 50.0, 0, 7), (5, 0.0, 0x10, 1, 10.0, 1, 7), (5, 0.0, 0x10, 1, 10.0, 0, 7)):
        pat = base + [(hitobj + 0x1A, b8(kind)), (d + 0x5C, f32b(d5c))]
        add('actor 1464B0 f%d fl%g k%X r%d %g %d' % (flags, floor, kind, r19, d5c, r60), B, 0x1464B0, [e, d], pat,
            queues={0x1B2BF0: [bge(flags, floor, hitobj)], 0x19AD00: [r19], 0x1B1560: [r60], 0x146740: [r67]})
        for rnd in ((0, 0x40, 0xC0) if flags == 5 else (0,)):
            for d7a, d74 in (((0, 1), (1, 1), (0, 0)) if flags == 1 else ((0, 0),)):
                add('actor 146110 f%d fl%g k%X r%d %X %d%d %g %d' % (flags, floor, kind, r19, rnd, d7a, d74, d5c, r60),
                    B, 0x146110,
                    [e, d], pat + [(d + 0x7A, b8(d7a)), (d + 0x74, b8(d74))],
                    queues={0x1B2BF0: [bge(flags, floor, hitobj)], 0x19AD00: [r19], 0x19A570: [r60],
                            0x1B3580: [fb(1.25)], 0x1B37D0: [fb(-0.75)]},
                    default={0x122BB8: rnd})


def creature_cases2(add, rng):
    """001424C0 / 001429D0 (behaviours 1 / 2), 00142070 over behaviours 0..2
    and the remaining branches of 001469B0 / 001459A0, on a13c_05's actor:
    the inline probes 00146110 / 001464B0 are steered by their callees'
    results (001B2BF0's flags and floor, 0019AD00, 00146740, ...)."""
    B = B5
    e = ACTOR
    d = e + 0x1F0
    hitobj = FREE + 0xD000
    here = (722.0, 159.0, 733.0)
    base = bones(e) + [(hitobj + 0x24, fw(701.0, 150.0, 1031.0)), (hitobj + 0x1A, b8(0x10)),
                       (e + 0xB0, fw(here[0], here[1], here[2], 1.0)), (e + 0xC4, f32b(0.5)), (d + 0x7A, b8(0)),
                       (0x8102B4, b8(0))]
    # the step probe 00146110 steered to its three results
    probe = {1: {0x1B2BF0: [bge(0, 0.0)], 0x1B37D0: [fb(0.75)]},
             0: {0x1B2BF0: [bge(1, 500.0)]},
             2: {0x1B2BF0: [bge(5, 0.0, hitobj)], 0x19A570: [0], 0x19AD00: [0]}}
    near = (here[0] + 3.0, here[1], here[2] + 4.0, 1.0)
    far = (here[0] + 300.0, here[1], here[2] + 400.0, 1.0)
    # 001424C0 state 0, 2, 3
    for rnd in (0, 0x12345678, 0x7FFFFFFF):
        add('chase1 1424C0 s0 r%X' % rnd, B, 0x1424C0, [e, d], base + [(e + 6, b8(0))], default={0x122BB8: rnd})
    for want in (0.5, 0.25):
        add('chase1 1424C0 s2 w%g' % want, B, 0x1424C0, [e, d], base + [(e + 6, b8(2)), (d + 0x44, f32b(want))])
    add('chase1 1424C0 s3', B, 0x1424C0, [e, d], base + [(e + 6, b8(3))])
    # state 1, the probe nonzero: the turn test and the counter
    for res in (1, 2):
        for heading in (0.5 + 3.1415927, 0.5 + 1.0, 0.5 - 3.1415927):
            for cnt in (0, 1, 2, -1):
                for timer in ((0, 3) if res == 1 and cnt == 2 else (3,)):
                    add('chase1 1424C0 s1 p%d h%.4g c%d t%d' % (res, heading, cnt, timer), B, 0x1424C0, [e, d],
                        base + [(e + 6, b8(1)), (d + 0x44, f32b(heading)), (d + 0x77, b8(cnt)), (d + 0x20, w32(timer)),
                                (d + 0x7F, b8(0))],
                        queues={**probe[res], 0x1B37D0: [fb(heading)], 0x1B3580: [fb(heading)]},
                        default={0x122BB8: 0x00ABCDEF})
    # state 1, the probe zero: alerted
    for d78, d60, d5c, rnd, d71 in ((0, 0, 50.0, 0x60000, 0), (0, 0, 50.0, 0x40000, 0), (0, 0, 40.0, 0, 0),
                                    (1, 0, 10.0, 0, 0), (0, 3, 10.0, 0, 0), (0, 3, 19.0, 0, 0), (0, 3, 19.5, 0, 0),
                                    (0, 3, 10.0, 0, 1), (0, 3, 50.0, 0x7FFF8000, 1), (0, 3, 49.0, 0, 1),
                                    (0, 3, 20.0, 0, 1), (0, 3, 20.5, 0, 1), (1, 0, 60.0, 0, 0)):
        for timer in (0, 2):
            add('chase1 1424C0 alert %d %d %g %X %d t%d' % (d78, d60, d5c, rnd, d71, timer), B, 0x1424C0, [e, d],
                base + [(e + 6, b8(1)), (d + 0x64, h16(5)), (d + 0x7F, b8(0)), (d + 0x78, b8(d78)), (d + 0x60, h16(d60)),
                        (d + 0x5C, f32b(d5c)), (d + 0x71, b8(d71)), (d + 0x20, w32(timer)), (d + 0x7A, b8(1)),
                        (0x810360, fw(here[0] + 5.0, here[1], here[2] + 5.0, 1.0))],
                queues=dict(probe[0]), default={0x122BB8: rnd})
    # state 1, the probe zero: not alerted (the zone, the wander flag and the frame counter)
    for zone, d76, frame, target, timer in ((1, 0, 0, near, 1), (1, 0, 0, far, 0), (0x81, 0, 0, near, 0),
                                            (0, 1, 0x40, near, 1), (0, 1, 0x40, far, 0), (0, 1, 0x41, near, 0),
                                            (0, 0, 0x40, near, 1), (0, 0, 0x40, far, 0), (0, 0, 0x43, far, 0),
                                            (0, 0, 0x40, near, 0)):
        add('chase1 1424C0 calm z%X w%d f%X %s t%d' % (zone, d76, frame, 'near' if target is near else 'far', timer), B,
            0x1424C0, [e, d],
            base + [(e + 6, b8(1)), (d + 0x64, h16(0)), (d + 0x7F, b8(zone)), (d + 0x76, b8(d76)), (e + 0xA, b8(0)),
                    (0x70003B68, w32(frame)), (0x70003B8A, h16(0)), (d + 0x10, fw(*target)), (d + 0x20, w32(timer)),
                    (d + 0x7A, b8(1))],
            queues=dict(probe[0]))
    # 001429D0 (behaviour 2): states 0, 1, 3, 4, 5 and the forward probe's four results
    for st in (0, 6):
        add('chase2 1429D0 s%d' % st, B, 0x1429D0, [e, d], base + [(e + 6, b8(st))])
    for t3c, d30 in ((14.0, 0), (15.0, 0), (14.0, 0x1000), (20.0, 0x1000)):
        add('chase2 1429D0 s1 %g %X' % (t3c, d30), B, 0x1429D0, [e, d],
            base + [(e + 6, b8(1)), (e + 0x3C, f32b(t3c)), (d + 0x30, w32(d30))])
    fwd = {3: {0x1B2BF0: [bge(0, 0.0)]}, 0: {0x1B2BF0: [bge(1, 500.0)]},
           2: {0x1B2BF0: [bge(5, 0.0, hitobj)], 0x19AD00: [0]}, 1: {0x1B2BF0: [bge(4, 0.0)], 0x146740: [1]}}
    for v in (1, 2, 3):
        add('chase2 1429D0 s2 v%d' % v, B, 0x1429D0, [e, d], base + [(e + 6, b8(2)), (d + 0x5C, f32b(50.0))],
            queues=dict(fwd[v]))
    for v in (4, 5, -1):   # 00146740's result passed through 001464B0
        add('chase2 1429D0 s2 v%d' % v, B, 0x1429D0, [e, d], base + [(e + 6, b8(2)), (d + 0x5C, f32b(50.0))],
            queues={0x1B2BF0: [bge(4, 0.0)], 0x146740: [v]})
    # 001459A0: the first segment probe hits
    for lean in (-0.2, -1.0):
        add('chase 1459A0 seg1 %g' % lean, B, 0x1459A0, [e, d],
            base + [(d + 0x48, f32b(lean)), (e + 4, b8(1)), (d + 0x4C, f32b(-1.0))],
            queues={0x1B32F0: [0], 0x1B3390: [1], 0x1B3440: [0, 1]})
    # 001459A0: a floor hit at each joint, both areas
    for area in (0xD, 0xC):
        for k in range(5):
            add('chase 1459A0 joint %X %d' % (area, k), B, 0x1459A0, [e, d],
                base + [(0x810700, b8(area)), (d + 0x48, f32b(-1.0)), (e + 4, b8(1)), (d + 0x4C, f32b(1.0))],
                queues={0x1B32F0: [0], 0x1B3390: [0, 0, 0, 0], 0x1B3440: [0] * k + [1]})
    for zone, t28, target in ((1, 2, near), (1, 0, near), (1, 0, far), (0x81, 2, far)):
        add('chase2 1429D0 s2 v0 z%X t%d %s' % (zone, t28, 'near' if target is near else 'far'), B, 0x1429D0, [e, d],
            base + [(e + 6, b8(2)), (d + 0x7F, b8(zone)), (d + 0x28, w32(t28)), (d + 0x10, fw(*target))],
            queues=dict(fwd[0]), default={0x122BB8: 0x2468ACE0})
    for d5c, r60, sel, yaw, r19, d62 in ((10.0, 1, 3, 0.5, 0, 1), (10.0, 1, 3, 0.5, 1, 1), (10.0, 1, 3, 2.5, 0, 1),
                                         (10.0, 1, 2, 0.5, 0, 1), (15.0, 1, 3, 0.5, 0, 1), (40.0, 1, 0, 3.6, 0, 1),
                                         (40.0, 1, 0, 0.6, 0, 1), (35.0, 1, 0, 3.6, 0, 1), (27.0, 1, 0, 0.5, 0, 1),
                                         (27.0, 1, 1, 0.5, 0, 1), (28.0, 1, 0, 0.5, 0, 1), (30.0, 0, 0, 0.5, 0, 1),
                                         (41.0, 1, 0, 0.5, 0, 1), (30.0, 0, 0, 0.5, 0, 0)):
        add('chase2 1429D0 s2 v0 near %g %d %d %g %d %d' % (d5c, r60, sel, yaw, r19, d62), B, 0x1429D0, [e, d],
            base + [(e + 6, b8(2)), (d + 0x7F, b8(0)), (d + 0x5C, f32b(d5c)), (0x81050C, b8(sel)),
                    (0x810374, f32b(yaw)), (d + 0x62, h16(d62)), (d + 0x24, w32(3)), (d + 0x28, w32(3))],
            queues={**fwd[0], 0x1B1560: [r60], 0x19AD00: [r19]})
    for d5c, same, r6a, r39, t24, t28 in ((30.0, 1, 1, 1, 0, 0), (30.0, 1, 1, 0, 0, 0), (30.0, 1, 0, 0, 2, 2),
                                          (30.0, 0, 1, 0, 0, 0), (23.0, 1, 1, 0, 0, 0), (19.0, 0, 0, 0, 0, 0),
                                          (21.0, 0, 0, 0, 2, 0)):
        add('chase2 1429D0 s2 v0 aim %g %d %d %d %d %d' % (d5c, same, r6a, r39, t24, t28), B, 0x1429D0, [e, d],
            base + [(e + 6, b8(2)), (d + 0x7F, b8(0)), (d + 0x5C, f32b(d5c)), (0x81050C, b8(5)), (d + 0x62, h16(1)),
                    (d + 0x24, w32(t24)), (d + 0x28, w32(t28)), (e + 0xC4, f32b(0.5))],
            queues={**fwd[0], 0x1B1560: [0], 0x1B1240: [fb(0.5) if same else fb(0.75), fb(0.6)],
                    0x146AF0: [r6a], 0x1B39F0: [r39]},
            default={0x122BB8: 0x13572468}, run_set=RUN - {0x1B1240})
    for t20, d79, r1, r2 in ((299, 0, 0, 0), (298, 1, 0, 1), (5, 0, 1, 0), (5, 1, 0, 0), (-1, 0, 0, 0)):
        add('chase2 1429D0 s3 t%d d%d %d%d' % (t20, d79, r1, r2), B, 0x1429D0, [e, d],
            base + [(e + 6, b8(3)), (d + 0x20, w32(t20)), (d + 0x79, b8(d79))], queues={0x19AD00: [r1, r2]})
    for d4c, d30, r60, target in ((-1.0, 0, 0, near), (1.0, 0x1000, 1, near), (1.0, 0x1000, 1, far),
                                  (1.0, 0x1000, 0, near), (0.0, 0x1000, 0, far)):
        add('chase2 1429D0 s4 %g %X %d %s' % (d4c, d30, r60, 'near' if target is near else 'far'), B, 0x1429D0, [e, d],
            base + [(e + 6, b8(4)), (d + 0x4C, f32b(d4c)), (d + 0x30, w32(d30)), (0x810360, fw(*target))],
            queues={0x1B1560: [r60]})
    for e7, d48, clip, d30, t3c, d74 in ((0, 0.0, 8, 0x1000, 40.0, 0), (0, 0.5, 8, 0x1000, 40.0, 0),
                                         (0, 0.5, 9, 0x1000, 40.0, 0), (0, 0.5, 0x8008, 0, 40.0, 0),
                                         (0, 0.5, 9, 0, 40.0, 0), (1, 0.0, 0, 0, 32.0, 2), (1, 0.0, 0, 0, 33.0, 0),
                                         (2, 0.0, 0, 0, 0.0, 0)):
        add('chase2 1429D0 s5 %d %g %X %X %g %d' % (e7, d48, clip, d30, t3c, d74), B, 0x1429D0, [e, d],
            base + [(e + 6, b8(5)), (e + 7, b8(e7)), (d + 0x48, f32b(d48)), (e + 0x2C, h16(clip)), (d + 0x30, w32(d30)),
                    (e + 0x3C, f32b(t3c)), (d + 0x74, b8(d74))])
    # 00142070 over behaviours 0, 1 and 2 (their bodies run as original code)
    for beh in (0, 1, 2):
        add('chase 142070 b%d' % beh, B, 0x142070, [e, d], base + [(e + 5, b8(beh)), (e + 6, b8(3)), (d + 0x30, w32(0))],
            queues={0x1B2140: [1], 0x1C64F0: [0x5D]})
    # the nested frames: 00141D20 -> 00142070 -> 001424C0 -> 00146110 (its
    # local vector), and -> 00142330 -> 001469B0 -> 001B3F10 -> 0019A6F0
    near_player = [(0x8102B4, b8(1)), (0x81050C, b8(2)), (0x810350, fw(here[0] + 1.0, here[1], here[2] + 1.0, 1.0)),
                   (0x810360, fw(here[0] + 1.0, here[1], here[2] + 1.0, 1.0)), (d + 0x64, h16(0)), (d + 0x7F, b8(0)),
                   (e + 0xA, b8(0)), (e, b8(1)), (e + 2, b8(0))]
    for entry, args_ in ((0x142070, [e, d]), (0x141D20, [e])):
        add('chase frames %06X b1' % entry, B, entry, args_,
            base + [(B8D, b8(0)), (e + 4, b8(1)), (e + 5, b8(1)), (e + 6, b8(1)), (d + 0x30, w32(0))],
            queues={0x1B2140: [1], 0x1C64F0: [0x5D], 0x1B2BF0: [bge(4, 0.0, hitobj)], 0x1B3580: [fb(1.0)]})
        add('chase frames %06X b0' % entry, B, entry, args_,
            base + near_player + [(B8D, b8(0)), (e + 4, b8(1)), (e + 5, b8(0)), (e + 6, b8(1)), (d + 0x30, w32(0))],
            queues={0x1B2140: [1], 0x1C64F0: [0x5D], 0x1A6440: [1], 0x19D330: [0]})
    # 001469B0: the 001B3F10 hit path (the actor next to the player) and its threshold
    for cnt in (0, 58, 59, 0xFF):
        add('chase 1469B0 hit c%d' % cnt, B, 0x1469B0, [e, d],
            base + [(d + 0x64, h16(0)), (d + 0x7F, b8(0)), (e + 0xA, b8(0)), (d + 0x70, b8(cnt)), (0x8102B4, b8(1)),
                    (0x81050C, b8(2)), (0x810350, fw(here[0] + 1.0, here[1], here[2] + 1.0, 1.0)),
                    (0x810360, fw(here[0] + 1.0, here[1], here[2] + 1.0, 1.0))],
            queues={0x19D330: [0]})
    # 001459A0: the drop to behaviour 4 (10 below, and exactly 10)
    for y, floor_, lean in ((150.0, 100.0, -0.6), (150.0, 140.0, -0.6), (150.0, 140.5, -0.6), (150.0, 100.0, -0.5)):
        add('chase 1459A0 drop %g %g %g' % (y, floor_, lean), B, 0x1459A0, [e, d],
            base + [(e + 0xB4, f32b(y)), (d + 0x48, f32b(lean)), (e + 4, b8(1)), (d + 0x4C, f32b(1.0))],
            queues={0x1B32F0: [0], 0x1B3390: [0, 0, 0, 0], 0x1B3440: [0] * 5,
                    0x1B2F70: [Scribble(1, [(0x700038A0, f32b(floor_))])]})


def bound_cases(add):
    """Float compares at their constants: each bound exactly and the next
    float on each side (a bound mutated by one unit in the last place, or a
    strict compare made non-strict, changes the outcome of one of them).
    EM_TEST_FULL=1 / EM_LEVEL11_PORT_ALL=1 run them; the default run keeps
    the ones the mutation sweep needed (survivor cases)."""
    B = B5
    e = ACTOR
    d = e + 0x1F0
    hitobj = FREE + 0xD000
    here = (722.0, 159.0, 733.0)
    base = bones(e) + [(hitobj + 0x24, fw(701.0, 150.0, 1031.0)), (hitobj + 0x1A, b8(0x10)),
                       (e + 0xB0, fw(here[0], here[1], here[2], 1.0)), (e + 0xC4, f32b(0.5)), (d + 0x7A, b8(0)),
                       (0x8102B4, b8(0))]

    def around(value):
        if value == 0.0:   # the EE compares denormals as zero: the smallest normals either side
            return ((0, 'eq'), (0x00800000, 'up'), (0x80800000, 'dn'))
        return ((fb(value), 'eq'), (up(value), 'up'), (down(value), 'dn'))
    probe0 = {0x1B2BF0: [bge(1, 500.0)]}
    # 001424C0, alerted: 40 (not within -> the random pick), 19, 50, 20
    for d78, d60, d71, bound in ((0, 0, 0, 40.0), (1, 0, 0, 19.0), (1, 0, 1, 50.0), (1, 0, 1, 20.0)):
        for bits, tag in around(bound):
            add('bound 1424C0 alert %g %s' % (bound, tag), B, 0x1424C0, [e, d],
                base + [(e + 6, b8(1)), (d + 0x64, h16(5)), (d + 0x7F, b8(0)), (d + 0x78, b8(d78)),
                        (d + 0x60, h16(d60)), (d + 0x71, b8(d71)), (d + 0x5C, bits32(bits)), (d + 0x20, w32(2)),
                        (d + 0x7A, b8(1)), (0x810360, fw(here[0] + 5.0, here[1], here[2] + 5.0, 1.0))],
                queues=dict(probe0), default={0x122BB8: 0x60000})
    # 001429D0: e +0x3C 14 (state 1) and 32 (state 5 / 1)
    for bits, tag in around(14.0):
        add('bound 1429D0 s1 14 %s' % tag, B, 0x1429D0, [e, d],
            base + [(e + 6, b8(1)), (e + 0x3C, bits32(bits)), (d + 0x30, w32(0)), (d + 0x4C, f32b(0.25))])
    for bits, tag in around(32.0):
        add('bound 1429D0 s5 32 %s' % tag, B, 0x1429D0, [e, d],
            base + [(e + 6, b8(5)), (e + 7, b8(1)), (e + 0x3C, bits32(bits)), (d + 0x40, f32b(1.0)), (d + 0x74, b8(0))])
    for value in (0.0, -0.0):
        for bits, tag in around(value) if value == 0.0 else ((0x80000000, 'neg0'),):
            add('bound 1429D0 s5 0 d48 %s' % tag, B, 0x1429D0, [e, d],
                base + [(e + 6, b8(5)), (e + 7, b8(0)), (d + 0x48, bits32(bits)), (e + 0x2C, h16(9)), (d + 0x30, w32(0))])
            add('bound 1429D0 s4 d4C %s' % tag, B, 0x1429D0, [e, d],
                base + [(e + 6, b8(4)), (d + 0x4C, bits32(bits)), (d + 0x50, f32b(0.5)), (d + 0x30, w32(0))])
    # 001429D0 state 2, the forward probe zero, near the player: 40, 15, 35, 27 (D_0081050C 0), 23
    fwd0 = {0x1B2BF0: [bge(1, 500.0)]}
    for bound, sel in ((40.0, 0), (15.0, 3), (35.0, 0), (27.0, 0), (23.0, 5)):
        for bits, tag in around(bound):
            add('bound 1429D0 s2 near %g %s' % (bound, tag), B, 0x1429D0, [e, d],
                base + [(e + 6, b8(2)), (d + 0x7F, b8(0)), (d + 0x5C, bits32(bits)), (0x81050C, b8(sel)),
                        (0x810374, f32b(3.6)), (d + 0x62, h16(1)), (d + 0x24, w32(3)), (d + 0x28, w32(3)),
                        (e + 0xC4, f32b(0.5))],
                queues={**fwd0, 0x1B1560: [1], 0x19AD00: [0], 0x1B1240: [fb(0.5), fb(0.6)], 0x146AF0: [1],
                        0x1B39F0: [0]},
                run_set=RUN - {0x1B1240})
    # 001429D0 state 2: d +0x5C < 20 for the re-pick (d +0x28 zero)
    for bits, tag in around(20.0):
        add('bound 1429D0 s2 repick 20 %s' % tag, B, 0x1429D0, [e, d],
            base + [(e + 6, b8(2)), (d + 0x7F, b8(0)), (d + 0x5C, bits32(bits)), (0x81050C, b8(5)),
                    (d + 0x62, h16(1)), (d + 0x24, w32(2)), (d + 0x28, w32(0))],
            queues={**fwd0, 0x1B1560: [0]}, default={0x122BB8: 0x13572468})
    # 001429D0 state 2: the facing test |wrap(yaw - e +0xC4)| at pi/4 exactly
    for yaw, tag in ((0x3F490FDB, 'eq'), (0x3F490FDC, 'up'), (0x3F490FDA, 'dn')):
        add('bound 1429D0 s2 facing %s' % tag, B, 0x1429D0, [e, d],
            base + [(e + 6, b8(2)), (d + 0x7F, b8(0)), (d + 0x5C, f32b(10.0)), (0x81050C, b8(3)),
                    (0x810374, bits32(yaw)), (e + 0xC4, f32b(0.0)), (d + 0x62, h16(1)), (d + 0x24, w32(3)),
                    (d + 0x28, w32(3))],
            queues={**fwd0, 0x1B1560: [1], 0x19AD00: [0]})
    # 00145880: d +0x5C <= 18
    for bits, tag in around(18.0):
        add('bound 145880 18 %s' % tag, B, 0x145880, [e, d],
            base + [(d + 0x75, b8(2)), (d + 0x5C, bits32(bits)), (d + 0x48, f32b(0.5)), (d + 0x4C, f32b(1.0)),
                    (d + 0x50, f32b(-0.25))], queues={0x1B1560: [1]})
    # 001459A0: the side probe's sign (d +0x4C < 0), the vertical speed's
    # bounds 0 / -0.1 / -0.5, the drop of 10
    for value in (0.0, -0.1, -0.5):
        for bits, tag in around(value) + (((0x80000000, 'neg0'),) if value == 0.0 else ()):
            add('bound 1459A0 lean %g %s' % (value, tag), B, 0x1459A0, [e, d],
                base + [(d + 0x48, bits32(bits)), (e + 4, b8(1)), (d + 0x4C, f32b(1.0)), (e + 0xB4, f32b(150.0))],
                queues={0x1B32F0: [0], 0x1B3390: [0, 0, 0, 0], 0x1B3440: [0] * 5,
                        0x1B2F70: [Scribble(1, [(0x700038A0, f32b(100.0))])], 0x19AB20: [0]})
    for bits, tag in around(0.0) + ((0x80000000, 'neg0'),):
        add('bound 1459A0 side %s' % tag, B, 0x1459A0, [e, d],
            base + [(d + 0x4C, bits32(bits)), (d + 0x48, f32b(0.0))], queues={0x1B32F0: [0]})
    for floor_ in (140.0, struct.unpack('<f', struct.pack('<I', fbits(140.0) + 1))[0],
                   struct.unpack('<f', struct.pack('<I', fbits(140.0) - 1))[0]):
        add('bound 1459A0 drop %r' % floor_, B, 0x1459A0, [e, d],
            base + [(e + 0xB4, f32b(150.0)), (d + 0x48, f32b(-0.6)), (e + 4, b8(1)), (d + 0x4C, f32b(1.0))],
            queues={0x1B32F0: [0], 0x1B3390: [0, 0, 0, 0], 0x1B3440: [0] * 5,
                    0x1B2F70: [Scribble(1, [(0x700038A0, f32b(floor_))])]})
    # 001464B0 / 00146110: y + 15 against the floor, y - 20 against the
    # floor, d +0x5C <= 10
    y = here[1]
    for floor_bits, tag in around(y + 15.0):
        for entry in (0x1464B0, 0x146110):
            add('bound %06X hi %s' % (entry, tag), B, entry, [e, d],
                base + [(d + 0x5C, f32b(50.0))],
                queues={0x1B2BF0: [Scribble(5, [(0x700038D0, bits32(floor_bits)), (0x700031D0, w32(hitobj))])],
                        0x19AD00: [0], 0x146740: [7], 0x19A570: [0], 0x1B3580: [fb(1.0)]},
                default={0x122BB8: 0x40})
    for floor_bits, tag in around(y - 20.0):
        for entry in (0x1464B0, 0x146110):
            add('bound %06X lo %s' % (entry, tag), B, entry, [e, d],
                base + [(d + 0x5C, f32b(50.0)), (d + 0x74, b8(1))],
                queues={0x1B2BF0: [Scribble(1, [(0x700038D0, bits32(floor_bits))])], 0x1B37D0: [fb(1.0)]})
    for bits, tag in around(10.0):
        add('bound 1464B0 d5c 10 %s' % tag, B, 0x1464B0, [e, d], base + [(d + 0x5C, bits32(bits))],
            queues={0x1B2BF0: [bge(4, 0.0)], 0x1B1560: [1], 0x146740: [7]})
    # 001469B0: d +0x5C <= 100 while alerted
    for bits, tag in around(100.0):
        add('bound 1469B0 100 %s' % tag, B, 0x1469B0, [e, d],
            base + [(d + 0x64, h16(1)), (d + 0x7F, b8(0)), (d + 0x5C, bits32(bits)), (d + 0x70, b8(3))])
    # 001471E0: the z bounds 970 and 1041.04 (one float below each)
    for area, z in ((0xD, down(970.0)), (0xC, down(1041.04)), (0xC, up(1041.04))):
        add('bound 1471E0 a%X z%X' % (area, z), B, 0x1471E0, [e, d],
            base + [(0x810700, b8(area)), (0x810368, bits32(z)), (d + 0x7F, b8(0x55))],
            queues={0x1B1EA0: [0, 0], 0x1B13F0: [0]}, run_set=RUN - {0x1B13F0})
    # 001B3F10: the yaw error at pi/4 exactly and one float either side
    # (001B1240 stubbed to that angle, the bearing 0)
    me = FREE + 0x8000
    for angle, tag in ((0x3F490FDB, 'eq'), (0x3F490FDC, 'up'), (0x3F490FDA, 'dn')):
        add('bound 1B3F10 yaw %s' % tag, B3, 0x1B3F10, [me, fb(0.0), fb(30.0)],
            [(me + 3, b8(1)), (me + 0xB0, fw(700.0, 160.0, 1000.0, 1.0)), (0x8102B4, b8(1)), (0x81050C, b8(0x12)),
             (0x810350, fw(705.0, 165.0, 1003.0, 1.0)), (0x810360, fw(705.0, 165.0, 1003.0, 1.0))],
            queues={0x1B1240: [angle], 0x19D330: [0]}, run_set=RUN - {0x1B1240})
    # 001E4610: the alpha at exactly 0 (t = 1.7) and the growth to 2
    s = NODE_A
    # t = 1.7 gives the alpha exactly 0; the other three land the growth at
    # 2 exactly and one float either side (found with the EE float model)
    for t in (fb(1.7), 0x3FFFDDDF, 0x3FFFDDE1, 0x3FFFDDDE):
        add('bound 1E4610 t%X' % t, B3, 0x1E4610, [s],
            [(s + 4, b8(1)), (s + 0x14, w32(NODE_B)), (s + 0x1F0 + 0x18, bits32(t)), (s + 0x1F0 + 0x14, f32b(25.0)),
             (s + 0x1F0 + 0x28, f32b(5.0)), (s + 0xB0, fw(1.0, 2.0, 3.0, 1.0)), (s + 0x100, fw(10.0, 20.0, 30.0, 1.0))])
    # 001B3F10: y + 15 where one unit in the last place of 15 shows (y 0)
    for y in (0.0, 1.0):
        add('bound 1B3F10 y%g' % y, B3, 0x1B3F10, [me, fb(0.0), fb(30.0)],
            [(me + 3, b8(1)), (me + 0xB0, fw(700.0, y, 1000.0, 1.0)), (0x8102B4, b8(1)), (0x81050C, b8(0x12)),
             (0x810350, fw(705.0, y + 2.0, 1003.0, 1.0)), (0x810360, fw(705.0, y + 2.0, 1003.0, 1.0))],
            queues={0x19D330: [0]})
    # 0x827F90: self +0x3C one float above 4 (the phase change) and above 10
    for x3c, tag in ((up(4.0), 'up4'), (down(4.0), 'dn4'), (down(10.0), 'dn10')):
        add('bound 827F90 x3c %s' % tag, B2, 0x827F90, [R45],
            bones(R45) + [(BLOCK45 + 16, w32(0)), (BLOCK45 + 4, w32(7)), (R45 + 0x3C, bits32(x3c)),
                          (0x810354, f32b(209.0))], queues={0x1B1EA0: [1]})


def survivor_cases(add):
    """Cases added because the mutation sweep (docs/LEVEL11_PORT.md section
    3.5) showed the case set missed them; they always run (the default run
    included)."""
    B = B5
    e = ACTOR
    d = e + 0x1F0
    hitobj = FREE + 0xD000
    here = (722.0, 159.0, 733.0)
    base = bones(e) + [(hitobj + 0x24, fw(701.0, 150.0, 1031.0)), (hitobj + 0x1A, b8(0x10)),
                       (e + 0xB0, fw(here[0], here[1], here[2], 1.0)), (e + 0xC4, f32b(0.5)), (d + 0x7A, b8(0)),
                       (0x8102B4, b8(0))]
    fwd0 = {0x1B2BF0: [bge(1, 500.0)]}
    # 001429D0: the player within 15 (d +0x5C exactly the next float above
    # 15 is not within)
    add('survivor 1429D0 s2 near up15', B, 0x1429D0, [e, d],
        base + [(e + 6, b8(2)), (d + 0x7F, b8(0)), (d + 0x5C, bits32(up(15.0))), (0x81050C, b8(3)),
                (0x810374, f32b(0.5)), (d + 0x62, h16(1)), (d + 0x24, w32(3)), (d + 0x28, w32(3))],
        queues={**fwd0, 0x1B1560: [1], 0x19AD00: [0]})
    # 001429D0 state 5 / 1: e +0x3C exactly the next float above 32
    add('survivor 1429D0 s5 1 up32', B, 0x1429D0, [e, d],
        base + [(e + 6, b8(5)), (e + 7, b8(1)), (e + 0x3C, bits32(up(32.0))), (d + 0x40, f32b(1.0)), (d + 0x74, b8(0))])
    # 001F4190: the drift with a nonzero velocity (a basis at +0xD0)
    s = NODE_A
    cfg = FREE + 0x9000
    add('survivor 1F4190 drift', B3, 0x1F4190, [s, s + 0x1F0, cfg],
        [(s + 0xD, b8(11)), (s + 0x1F0, w32(0x89ABCDEF)), (s + 0x1F4, w32(30)), (s + 0x1F8, w32(3)),
         (s + 0xD0, fw(0.6, 0.0, 0.8, 0.0, 0.0, 1.0, 0.0, 0.0, -0.8, 0.0, 0.6, 0.0, 700.0, 170.0, 1100.0, 1.0)),
         (cfg, fw(2.0, 3.0, 4.0, 0.0, 1.5, 2.5, 3.5, 0.0, 0.25, 0.5, 0.75, 0.0, 0x111, 0x222, 0x333, 0x444,
                  0.5, 0.25, -0.05, 1, 0x51, 0x54, 0x58, 0x5C))])
    # 001B3F10: y + 15 where one unit in the last place of 15 shows (y 0)
    me = FREE + 0x8000
    add('survivor 1B3F10 y0', B3, 0x1B3F10, [me, fb(0.0), fb(30.0)],
        [(me + 3, b8(1)), (me + 0xB0, fw(700.0, 0.0, 1000.0, 1.0)), (0x8102B4, b8(1)), (0x81050C, b8(0x12)),
         (0x810350, fw(705.0, 2.0, 1003.0, 1.0)), (0x810360, fw(705.0, 2.0, 1003.0, 1.0))],
        queues={0x19D330: [0]})
    # 0x827F90: self +0x3C one float above 4 (no phase change)
    add('survivor 827F90 x3c up4', B2, 0x827F90, [R45],
        bones(R45) + [(BLOCK45 + 16, w32(0)), (BLOCK45 + 4, w32(7)), (R45 + 0x3C, bits32(up(4.0))),
                      (0x810354, f32b(209.0))], queues={0x1B1EA0: [1]})
    # 001E4610: the growth landing one float above 2 (ends) and exactly at
    # 2 (does not end)
    for t in (0x3FFFDDE1, 0x3FFFDDDF):
        add('survivor 1E4610 t%X' % t, B3, 0x1E4610, [NODE_A],
            [(NODE_A + 4, b8(1)), (NODE_A + 0x14, w32(NODE_B)), (NODE_A + 0x1F0 + 0x18, bits32(t)),
             (NODE_A + 0x1F0 + 0x14, f32b(25.0)), (NODE_A + 0x1F0 + 0x28, f32b(5.0)),
             (NODE_A + 0xB0, fw(1.0, 2.0, 3.0, 1.0)), (NODE_A + 0x100, fw(10.0, 20.0, 30.0, 1.0))])
    # 00146110: the floor exactly at y + 15 (not above it: the wide probe runs)
    add('survivor 146110 floor eq', B, 0x146110, [e, d], base + [(d + 0x5C, f32b(50.0))],
        queues={0x1B2BF0: [Scribble(5, [(0x700038D0, f32b(here[1] + 15.0)), (0x700031D0, w32(hitobj))])],
                0x19AD00: [0], 0x19A570: [0], 0x1B3580: [fb(1.0)]},
        default={0x122BB8: 0x40})

    # 001464B0: the floor exactly at y - 20 (bit 0 without bit 2: 3), at a
    # height where one unit in the last place of 20 survives the EE's
    # subtraction (y 30)
    add('survivor 1464B0 floor lo eq', B, 0x1464B0, [e, d],
        base + [(d + 0x5C, f32b(50.0)), (e + 0xB4, f32b(30.0))],
        queues={0x1B2BF0: [Scribble(1, [(0x700038D0, f32b(10.0))])]})


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

# Words the static walk reaches that no input can reach, and why (each
# word's instruction class was checked, never its text reproduced).
DEAD_WORDS = {
    # 00118418: the divide-by-zero traps after the two divisions by the
    # constant 60 (the divisor is never zero)
    0x118504: 'trap after a division by the constant 60',
    0x118528: 'trap after a division by the constant 60',
    # 00146CE0: the second test of d +0x7B (its 0021C040 path): the byte
    # was tested zero just before and nothing in between writes it (the
    # stores go to e +0, d +0x64 and d +0x72; e +0 is d +0x7B only for an
    # odd e, whose halfword e +0x34 already stops the original)
    0x146DA0: 'd +0x7B tested zero just before', 0x146DA4: 'd +0x7B tested zero just before',
    0x146DA8: 'd +0x7B tested zero just before', 0x146DB0: 'd +0x7B tested zero just before',
    0x146DB4: 'd +0x7B tested zero just before', 0x146DB8: 'd +0x7B tested zero just before',
    0x146DBC: 'd +0x7B tested zero just before', 0x146DC4: 'd +0x7B tested zero just before',
    0x146DC8: 'd +0x7B tested zero just before', 0x146DCC: 'd +0x7B tested zero just before',
    # 001CDDC0: the fog byte's clamp to 0 (the lane was raised to at least
    # 0 by VMAX before VFTOI4, so (fog >> 4) is never negative), and one
    # compare of the (u, v) switch that no corner index 0..3 reaches (the
    # index is the triangle 0 / 1 plus the vertex 0..2)
    0x1CDEE0: 'fog clamp to 0 after VMAX with 0', 0x1CDEE4: 'fog clamp to 0 after VMAX with 0',
    0x1CE07C: '(u, v) switch: no corner index 0..3 reaches it',
    # 001F4190: the sign fix-up of (piece index % 2) for a negative index;
    # the index counts up from 0
    0x1F47CC: 'remainder sign fix-up for a negative index',
    0x1F47D0: 'remainder sign fix-up for a negative index',
}


def callee_checks():
    """Every call the static walk of an entry reaches (a jal target) is a
    hook (HOOKS) or another entry (FUNCS): no original function runs
    unlisted at the top level of the oracle. Returns the problems."""
    problems = []
    for entry, (_, size, _, _) in sorted(FUNCS.items()):
        ram = image_for(entry)
        for pc in sorted(reachable_words(ram, entry, size) | {p - 4 for p in reachable_words(ram, entry, size)}):
            if not entry <= pc < entry + size:
                continue
            word = u32(ram, pc)
            if word >> 26 == 3:
                target = (pc & 0xF0000000) | ((word & 0x3FFFFFF) << 2)
                if target not in BY_ADDRESS and target not in FUNCS:
                    problems.append('%06X calls %06X (neither a hook nor an entry)' % (entry, target))
    return sorted(set(problems))


def fault_checks():
    """The native fail-stop contract: a NULL hook, a failing hook, an
    unmapped address after the calls and before any call, a latched fault;
    and for every entry a fault latched on entry, a NULL hook table, a NULL
    fault pointer and (entries with a result) a NULL result pointer."""
    ram, spad = CAPTURES[B2]
    node = R44
    ram = bytearray(ram)
    ram[node + 5] = 3
    problems = []
    calls_want = ['w_001C6380', 'w_001B17A0', 'w_callback']

    def attempt(null=None, failing=None, unmapped=None, latched=False, entry=0x824390, args=(node,)):
        ram_c = (C.c_uint8 * RAM_SIZE).from_buffer_copy(ram)
        spad_c = (C.c_uint8 * SPAD_SIZE).from_buffer_copy(spad)
        stack_c = (C.c_uint8 * STACK_SIZE)()
        base, sbase, kbase, calls = C.addressof(ram_c), C.addressof(spad_c), C.addressof(stack_c), []

        def mem(_, address, size):
            if address == unmapped:
                return None
            if 0x70000000 <= address and address + size <= 0x70000000 + SPAD_SIZE:
                return sbase + address - 0x70000000
            if STACK_LO <= address and address + size <= STACK_TOP:
                return kbase + address - STACK_LO
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
    status, address, code, calls, _ = attempt(null='w_001B17A0')
    if (status, address, code) != (-1, 0x1B17A0, 1) or calls != calls_want[:1]:
        problems.append(('NULL hook', status, hex(address), code, calls))
    status, address, code, calls, same = attempt(failing='w_001C6380')
    if (status, address, code) != (-1, 0x1C6380, 2) or calls != calls_want[:1] or not same:
        problems.append(('failing hook', status, hex(address), code, calls, same))
    status, address, code, calls, _ = attempt(unmapped=node + 0x4C)
    if (status, address, code) != (-1, node + 0x4C, 5) or calls != calls_want[:2]:
        problems.append(('unmapped address', status, hex(address), code, calls))
    status, address, code, calls, same = attempt(unmapped=node + 0x18, entry=0x8249F0, args=(node,))
    if (status, address, code) != (-1, node + 0x18, 5) or calls or not same:
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


HOOK_NAMES = [name for name, _, _, _ in HOOKS] + ['w_callback']
BY_NAME = {name: address for name, address, _, _ in HOOKS}


# Hooks no passing case can reach, and why (the contract skips them; their
# wrappers are the same generated code as every other hook's).
CONTRACT_UNREACHABLE = {
    'w_0021C040': '00146CE0 calls it only on a path DEAD_WORDS lists (d +0x7B tested zero just before)',
}


def hook_sites(results):
    """The cases hook_contract_site runs on: passing cases (the original
    does not stop) that together call every hook, the +0x4C callback and
    every entry, picked greedily (most targets not yet covered per native
    run the contract makes there, i.e. per call plus memory access of the
    case, then the earliest case). Returns ([(case index, targets)], targets no case
    reaches); a target is a hook name or ('bytes', entry)."""
    want = (set(HOOK_NAMES) - set(CONTRACT_UNREACHABLE)) | {('bytes', entry) for entry in FUNCS}
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
    """The fail-stop contract of the header (em_level11_port.h), on one
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
    where each wrapper's own `l11_failed` test is observable. Returns
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
            if not flat:   # an entry that touches no memory itself: nothing to refuse
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



# The default run's share of the designed cases: a coverage pass over the
# whole set (every case's original run, EM_TEST_FULL=1) picked these so that
# with the capture cases the default run reaches every word the whole set
# reaches (greedy, cheapest first); SURVIVORS are the mutation sweep's cases.
DEFAULT_KEEP = frozenset([
    'a13 8240E0 f1 r2 y01005243',
    'a13 824390 sub1 go f1 r1 y300 r0',
    'a13 824390 sub1 go fFF r1 y300 r1',
    'a13 824390 sub1 t1',
    'a13 824390 sub2 r2',
    'a13 824390 sub80',
    'a13 824520 sub0 c5 s0 r1',
    'a13 824520 sub0 c5 s1 r0',
    'a13 824520 sub0 c5 s2 r0',
    'a13 824520 sub1 c5 s0 r0',
    'a13 824520 sub2 c5 s0 r0',
    'a13 824520 sub3 c0 s0 r1',
    'a13 824520 sub4 c0 s0 r0',
    'a13 8246D0 s0 t7',
    'a13 8246D0 s1 t49',
    'a13 8246D0 s1 t50',
    'a13 8246D0 s2 t7',
    'a13 8248C0 s0 t7',
    'a13 8248C0 s1 t44',
    'a13 8248C0 s1 t50',
    'a13 8248C0 s2 t7',
    'a13 824960 sub0 fFF r0',
    'a13 824960 sub1 f0 r1',
    'a13 824960 sub2 f0 r0',
    'a13 827C30 c1',
    'a13 827C30 c12C',
    'a13 827C30 c190',
    'a13 827C30 c1DF',
    'a13 827C30 c1F3',
    'a13 827C30 c64',
    'a13 827C30 cC8',
    'a13 827E00 c5A',
    'a13 827E00 c96',
    'a13 827F90 p0 c0 r1 x0000a041 0 y0',
    'a13 827F90 p0 c0 r5 x0000a041 0 y0',
    'a13 827F90 p0 c0 rFFFFFFFF x0000a041 0 y0',
    'a13 827F90 p1 b0',
    'a13 827F90 p2',
    'a13 8284E0 oFD',
    'a13 828500 s0 d10',
    'a13 828500 s0 d6',
    'a13 828500 s0 d7',
    'a13 828500 s0 d8',
    'a13 828500 s0 d9',
    'a13 828500 s1 f0 d7 l0 a177 t0.25 01 h-1',
    'a13 828500 s1 f0 d7 l10 a0 t1.49167 01 h-1',
    'a13 828500 s1 fFF d7 l10 a180 t1.49 10 h3',
    'a13 828500 s1 quit 2 2',
    'a13 828500 s2 d10',
    'a13 828500 s2 d6',
    'a13 828500 s2 d7',
    'a13 828500 s2 d8',
    'a13 828500 s2 d9',
    'a13 828500 s3',
    'a13 828500 s4',
    'a13 828C60 s0 0 0',
    'a13 828C60 s1 1.79 0.006',
    'a13 828C60 s1 1.795 0.004',
    'a13 828C60 s2 0 0',
    'a13 828C60 s3 0 0',
    'a13 828C60 s4 0 0',
    'a13 828E10 s0 0',
    'a13 828E10 s1 1.492',
    'a13 828E10 s1 1.5',
    'a13 828E10 s2 0',
    'a13 828E10 s3 0',
    'a13 828E10 s4 0',
    'a13 828F40 s0 0 0 0',
    'a13 828F40 s1 1.45 0.02 41',
    'a13 828F40 s1 1.49 0.009 7',
    'a13 828F40 s2 0 0 0',
    'a13 828F40 s3 0 0 0',
    'a13 828F40 s4 0 0 0',
    'actor 141D20 b0 s0 r1',
    'actor 141D20 b0 s1 r1',
    'actor 141D20 b0 s3 r1',
    'actor 141D20 b0 s4 r1',
    'actor 141D20 b1 s1 r1',
    'actor 141D20 b5 s2 r1',
    'actor 141D20 counters 1 aC',
    'actor 141F00 f0 d0 a1 r1',
    'actor 141F00 fFF d80 a1 r1',
    'actor 142070 r1 b3 01 c7 0',
    'actor 142070 r1 b4 01 c7 0',
    'actor 142070 r1 b5 01 c7 0',
    'actor 142070 r1 b6 10 c9 FFFFE000',
    'actor 142070 r1 b7 11 c8 FFFFE000',
    'actor 142070 r1 b8 01 c7 0',
    'actor 142330 s0 r0',
    'actor 142330 s0 r80000',
    'actor 142330 s1 c1 t80 z80 0 0 1 0',
    'actor 142330 s1 c8001 t80 z80 1000 0 0 0',
    'actor 142330 s2 r0',
    'actor 1459A0 s-1 l0.5 aD h001',
    'actor 1459A0 s1 l-0.1 aC h00000',
    'actor 1459A0 s1 l-1 aD h00001',
    'actor 1459A0 s1 l-1 aD h0001',
    'actor 1459A0 s1 l-1 aD h001',
    'actor 1459A0 s1 l0 aD h1',
    'actor 1459A0 s1 l0.5 aC h00000',
    'actor 146110 f1 fl500 k10 r0 0 00 50 0',
    'actor 146110 f4 fl0 k10 r0 0 00 50 0',
    'actor 146110 f5 fl0 k46 r0 C0 00 50 0',
    'actor 1464B0 f4 fl0 k10 r0 5 0',
    'actor 1464B0 f5 fl0 k10 r1 10 0',
    'actor 1464B0 f5 fl0 k10 r1 10 1',
    'actor 1469B0 a0 z0 e1 0 c0 f0',
    'actor 1469B0 a1 z1 e0 150 cB3 f0',
    'actor 146CE0 h19 xA005 0000 s1 0 2 f0',
    'actor 146CE0 h64 x2005 0000 s1 0 0 f0',
    'actor 146CE0 h64 x4005 0000 s1 0 0 f0',
    'actor 146CE0 h64 x5 0030 s1 20 0 f0',
    'actor 146CE0 h64 x5 1000 s1 0 0 f0',
    'actor 146CE0 h64 x8005 0000 s0 0 0 f0',
    'actor 146CE0 h64 x8005 0000 s1 0 2 f0',
    'actor 146CE0 hA x5 0000 s1 0 0 f0',
    'actor 1471E0 aC z1041.04 001',
    'actor 1471E0 aD z900 010',
    'actor 1471E0 aD z969 100',
    'boot 100130 stub FFFFFFFF80000000',
    'boot 118418 k1 m5',
    'boot 118418 k2 m0',
    'boot 118418 k2 m1',
    'boot 118418 k2 m2',
    'boot 118418 k2 m6',
    'boot 118418 k2 m7',
    'boot 19A6F0 f1 o101 t40 r12 00',
    'boot 19A6F0 f1 o130 t40 r12 00',
    'boot 19A6F0 f7 o100 t40 r10 11',
    'boot 1B2E50 n-1 x9 f000',
    'boot 1B2E50 n0 x5 f111',
    'boot 1B2E50 n3 x0.5 f000',
    'boot 1B2E50 n3 x1 f111',
    'boot 1B2E50 n3 x9 f000',
    'boot 1B2E50 n3 x9 f010',
    'boot 1B3F10 b1 dy40 h30 f0',
    'boot 1B3F10 b1 dy5 h30 f2000',
    'boot 1B3F10 b1 dy5 h30 fDFFF',
    'boot 1B3F10 k2 b1.03038',
    'boot 1B3F10 k2 b2.9',
    'boot 1B3F10 k5 b1.03038',
    'boot 1B3F10 k5 b2.9',
    'boot 1B3F10 k7 b1.03038',
    'boot 1B3F10 k7 b2.9',
    'boot 1CDDC0 m0 c80FFFFFF f5 n03',
    'boot 1CDDC0 m1 c80FFFFFF f5 n34',
    'boot 1CDDC0 m2 c80FFFFFF f280 n00',
    'boot 1CDDC0 m5 c80FFFFFF f5 n34',
    'boot 1CE660 n0 m0',
    'boot 1CE660 n1 m0',
    'boot 1CE660 n2 m0',
    'boot 1E4610 s0 d0 t0.0',
    'boot 1E4610 s0 d1 t0.0',
    'boot 1E4610 s0 d2 t0.0',
    'boot 1E4610 s1 d0 t1.0',
    'boot 1E4610 s2 d0 t0.0',
    'boot 1E4610 s3 d0 t0.0',
    'boot 1E4610 s4 d0 t0.0',
    'boot 1F4190 k11 n1 a25 l30',
    'boot 1F4190 k12 n1 a3 l30',
    'boot 1F4190 k19 n1 a3 l30',
    'boot 1F4190 k20 n1 a3 l30',
    'boot 1F4190 k21 n0 a0 l1',
    'boot 1F4190 k21 n1 a25 l30',
    'boot 1F4190 k27 n1 a3 l30',
    'boot 1F4840 s0 k10 t0 l0',
    'boot 1F4840 s0 k13 t0 l0',
    'boot 1F4840 s0 k14 t0 l0',
    'boot 1F4840 s0 k17 t0 l0',
    'boot 1F4840 s0 kA t0 l0',
    'boot 1F4840 s0 kF t0 l0',
    'boot 1F4840 s1 kF t1 l1',
    'boot 1F4840 s2 kB t0 l0',
    'boot 1F4840 s3 kB t0 l0',
    'boot 1F4840 s4 kB t0 l0',
    'boot 1F9140 7',
    'bound 1424C0 alert 40 up',
    'bound 1424C0 alert 50 up',
    'bound 1429D0 s2 near 35 dn',
    'bound 145880 18 eq',
    'bound 146110 hi eq',
    'bound 146110 lo eq',
    'bound 146110 lo up',
    'bound 1464B0 d5c 10 eq',
    'bound 1464B0 hi up',
    'bound 1464B0 lo eq',
    'bound 1469B0 100 up',
    'bound 1471E0 aC z44822147',
    'bound 1B3F10 yaw eq',
    'bound 1E4610 t3FFFDDDF',
    'bound 1E4610 t3FFFDDE1',
    'bound 827F90 x3c dn4',
    'chase 142070 b0',
    'chase 142070 b1',
    'chase 1459A0 drop 150 140 -0.6',
    'chase 1459A0 joint D 0',
    'chase 1459A0 seg1 -1',
    'chase 1469B0 hit c59',
    'chase1 1424C0 alert 0 0 50 40000 0 t2',
    'chase1 1424C0 alert 1 0 10 0 0 t2',
    'chase1 1424C0 calm z0 w0 f40 far t0',
    'chase1 1424C0 calm z0 w1 f40 near t1',
    'chase1 1424C0 calm z1 w0 f0 far t0',
    'chase1 1424C0 calm z81 w0 f0 near t0',
    'chase1 1424C0 s0 r7FFFFFFF',
    'chase1 1424C0 s1 p1 h3.642 c2 t3',
    'chase1 1424C0 s2 w0.5',
    'chase1 1424C0 s3',
    'chase2 1429D0 s0',
    'chase2 1429D0 s1 20 1000',
    'chase2 1429D0 s2 v0 aim 19 0 0 0 0 0',
    'chase2 1429D0 s2 v0 aim 30 1 1 1 0 0',
    'chase2 1429D0 s2 v0 near 10 1 2 0.5 0 1',
    'chase2 1429D0 s2 v0 near 15 1 3 0.5 0 1',
    'chase2 1429D0 s2 v0 near 27 1 0 0.5 0 1',
    'chase2 1429D0 s2 v0 near 30 0 0 0.5 0 0',
    'chase2 1429D0 s2 v0 near 40 1 0 3.6 0 1',
    'chase2 1429D0 s2 v0 z1 t0 near',
    'chase2 1429D0 s2 v0 z81 t2 far',
    'chase2 1429D0 s2 v1',
    'chase2 1429D0 s2 v2',
    'chase2 1429D0 s2 v3',
    'chase2 1429D0 s2 v5',
    'chase2 1429D0 s3 t298 d1 01',
    'chase2 1429D0 s3 t299 d0 00',
    'chase2 1429D0 s3 t5 d0 10',
    'chase2 1429D0 s3 t5 d1 00',
    'chase2 1429D0 s4 -1 0 0 near',
    'chase2 1429D0 s4 1 1000 0 near',
    'chase2 1429D0 s4 1 1000 1 near',
    'chase2 1429D0 s5 0 0 8 1000 40 0',
    'chase2 1429D0 s5 0 0.5 9 0 40 0',
    'chase2 1429D0 s5 0 0.5 9 1000 40 0',
    'chase2 1429D0 s5 1 0 0 0 32 2',
    'chase2 1429D0 s5 2 0 0 0 0 0',
])
SURVIVORS = ('survivor ',)


def select_cases(elf):
    """The mode's case list (EM_TEST_FULL=1: all; the default run keeps
    every capture case, the designed cases of DEFAULT_KEEP and the mutation
    sweep's survivor cases)."""
    cases, captured, targeted = case_list(elf)
    total = len(cases)
    if not reference_mode.FULL and not os.environ.get('EM_LEVEL11_PORT_ALL'):
        cases = cases[:captured] + [c for c in cases[captured:]
                                    if c[0] in DEFAULT_KEEP or c[0].startswith(SURVIVORS)]
        return cases, captured, len(cases) - captured, total
    return cases, captured, targeted, total


def image_for(entry):
    """An image holding the entry's code (every image holds AREA13; boot
    text is the same in all)."""
    return CAPTURES[A13_BEATS[0]][0]


def main():
    global NATIVE
    elf = read_elf()
    CAPTURES.update(load_captures(elf))
    NATIVE = build(os.environ.get('EM_LEVEL11_PORT_SOURCE'))
    selected, captured, targeted, total = select_cases(elf)
    CASES[:] = selected
    only = os.environ.get('EM_LEVEL11_PORT_ONLY')   # debugging: a label prefix
    if only:
        CASES[:] = selected = [c for c in selected if c[0].startswith(only)]
    results = run_cases(selected)
    failures, per_fn, seen, totals = [], {}, set(), [0, 0, 0, 0]
    header_problems, header_count = header_checks(os.environ.get('EM_LEVEL11_PORT_SOURCE'))
    print('  header / wrappers vs HOOKS: %s' % ('ok (%d hooks: names, order, argument and result types; one '
                                                'wrapper each, calling it and latching its address)' % header_count
                                                if not header_problems else header_problems[:4]))
    if header_problems:
        failures.append(('header vs HOOKS', header_problems))
    for label, entry, errors, pcs, counts, _ in results:
        totals = [a + b for a, b in zip(totals, counts)]
        per_fn[entry] = per_fn.get(entry, 0) + 1
        seen.update(pcs)
        if errors:
            failures.append((label, errors))
    if os.environ.get('EM_LEVEL11_PORT_KEEP'):   # lane tool: the coverage pass that picks DEFAULT_KEEP
        print_keep(results)
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
    print('  coverage %d/%d reachable non-branch words' % (covered_total, words_total))
    if os.environ.get('EM_LEVEL11_PORT_MISSING'):
        for entry, words in missing_words.items():
            print('   missing %06X: %s' % (entry, ' '.join('%X' % w for w in words[:400])))
    if covered_total < words_total and not only:
        failures.append(('coverage', ['%d of %d reachable original words not executed'
                                      % (words_total - covered_total, words_total)]))
    callee_problems = callee_checks()
    print('  callees: %s' % ('ok (every reachable call of the %d entries is a hook or an entry)' % len(FUNCS)
                             if not callee_problems else callee_problems[:8]))
    if callee_problems:
        failures.append(('callees', callee_problems))
    if not only:
        problems = fault_checks()
        print('  native fail-stop checks: %s' % ('ok (NULL hook, failing hook, unmapped address, unmapped address '
                                                 'before any call, latched fault; every entry refuses a latched '
                                                 'fault, a NULL hook table, a NULL fault pointer and a NULL result '
                                                 'pointer)' if not problems else problems))
        if problems:
            failures.append(('native fail-stop', problems))
        sites, missing = hook_sites(results)
        if os.environ.get('EM_LEVEL11_PORT_NOCONTRACT'):   # debugging only: skips the contract
            sites, missing = [], []
        outcome = reference_mode.parallel_map(hook_contract_site, sites)
        contract = [p for problems_, _ in outcome for p in problems_]
        if missing:
            contract.append(('no passing case reaches', missing))
        sweep = ('every call failing and every memory access refused' if reference_mode.FULL else
                 'an even sample of each case\'s calls failing (at most %d) and memory accesses refused (at most %d)'
                 % (CONTRACT_CALLS, CONTRACT_ACCESSES))
        print('  hook contract: %s' % (
            ('ok (%d native runs on %d cases: %s; each of the %d hooks and the +0x4C callback NULL, '
             'returning INT32_MIN, 1 and INT32_MAX; `bytes` NULL on all %d entries)'
             % (sum(n for _, n in outcome), len(sites), sweep, len(HOOKS), len(FUNCS))) if not contract
            else contract[:6]))
        if contract:
            failures.append(('hook contract', contract))
        if not os.environ.get('EM_LEVEL11_PORT_NOREUSE'):
            problems, report = reuse_checks(elf)
            print('  reuse checks: %s' % (report if not problems else problems[:4]))
            if problems:
                failures.append(('reuse checks', problems[:6]))
    for label, errors in failures[:int(os.environ.get("EM_LEVEL11_PORT_SHOW", "12"))]:
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


def print_keep(results):
    """The coverage pass behind DEFAULT_KEEP: the capture cases' words, then
    designed cases picked greedily (most new words per unit of cost) until
    the words of the whole set are reached."""
    captured_seen = set()
    designed = []
    for label, entry, errors, pcs, counts, _ in results:
        if label.startswith('capture '):
            captured_seen |= pcs
        elif not label.startswith(SURVIVORS):
            designed.append((label, set(pcs), 1 + counts[3]))
    want = set().union(*(p for _, p, _ in designed)) - captured_seen if designed else set()
    keep = []
    while want:
        label, pcs, cost = max(designed, key=lambda d: (len(d[1] & want) / d[2], d[0]))
        if not pcs & want:
            break
        keep.append(label)
        want -= pcs
    print('KEEP %d' % len(keep))
    for label in sorted(keep):
        print('KEEP:    %r,' % label)


# ----------------------------------------------------------------------------
# reuse_checks: the census row with an existing verified translation, re-run
# against the ORIGINAL over the eleventh-level captures:
#   0020D930  em_menu_hover_0020D930 (src/game/em_menu_hover.c, the port's
#             one translation of the status screens' stick hover; verified
#             by tools/test_menu_hover_source_reference.py). Its interface
#             takes the sampled stick (magnitude, angle) and the hover byte;
#             the original samples the stick through 001B62C0 and gates on
#             the soft-double compare (00128350 / 00100130 / 001274B0, run
#             here as original code). The check runs the original 0020D930
#             on a13c_04's image (the HEALING page's commit, census first
#             frame f2032) with 001B62C0 stubbed to the designed stick and
#             001FB9F0 logged, and compares the hover byte t +0x11 and the
#             cue calls with em_menu_hover_0020D930 (built unmodified into
#             build/level11/port/reuse).
REUSE_OUT = OUT / 'reuse'
HOVER_SIZE = 0x668
HOVER_BOUNDS = (-2.7576203, -2.670354, -2.3561945, -2.0071287, -1.5707964, -0.7853982, -0.5235988, -0.41887903,
                0.36651915, 0.5235988, 0.7853982, 1.5707964, 2.0071287, 2.3561945, 2.7576203, 3.1415927)


def reuse_hover(elf, smoke=False):
    REUSE_OUT.mkdir(parents=True, exist_ok=True)
    lib = REUSE_OUT / ('hover.dylib' if sys.platform == 'darwin' else 'hover.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared', '-fPIC',
                    '-I' + str(ROOT / 'src'), str(ROOT / 'src/game/em_menu_hover.c'), '-o', str(lib)], check=True)
    port = C.CDLL(str(lib))
    port.em_menu_hover_0020D930.argtypes = [C.POINTER(C.c_uint8), C.c_int32, C.c_float, C.c_float]
    port.em_menu_hover_0020D930.restype = C.c_int
    beat = 'a13c_04_cure'
    ram0, spad0 = CAPTURES[beat]
    t = FREE + 0xE000
    angles = {fbits(0.0), fbits(-3.14159274), fbits(3.14159274), fbits(1.0), fbits(-1.0)}
    for bound in HOVER_BOUNDS:
        angles.update(fbits(bound) + k for k in ((-1, 0, 1) if not smoke else (0, 1)))
    magnitudes = (0.0, M_08_DOWN, 0.8, 1.0) if not smoke else (M_08_DOWN, 0.8)
    modes = (0, 1, 2, -1, 3, 255) if not smoke else (0, 1, 2)
    previous = range(7) if not smoke else (0, 4)
    seen, problems, count = set(), [], 0
    ram = WatchedMemory(RAM_SIZE)
    ram[:] = ram0
    for mode in modes:
        for prev in previous:
            for mag in magnitudes:
                for ang in sorted(angles):
                    ram.touched = []
                    ram[t + 0x11] = prev
                    ram.touched = []
                    ee = Coverage(None, ram, bytearray(spad0), own=True)
                    ee.seen = seen
                    cues = []

                    def stick(e, mag=mag, ang=ang):
                        e.write(e.r[4], fw(0.0, 0.0, mag) + w32(ang))

                    def cue(e):
                        cues.append(tuple(e.r[4 + k] & MASK for k in range(4)))
                    ee.hooks[0x1B62C0] = stick
                    ee.hooks[0x1FB9F0] = cue
                    ee.r[4], ee.r[5] = sx32(t), sx32(mode & MASK)
                    ee.r[31] = RETURN
                    ee.run(0x20D930)
                    orig_hover = ram[t + 0x11]
                    hover = C.c_uint8(prev)
                    sound = port.em_menu_hover_0020D930(C.byref(hover), mode, mag,
                                                        struct.unpack('<f', struct.pack('<I', ang))[0])
                    count += 1
                    stray = [(a, b) for a, b in ram.touched if not (a >= t + 0x11 and b <= t + 0x12)]
                    if stray:
                        problems.append(('0020D930 wrote outside t +0x11', [(hex(a), hex(b)) for a, b in stray[:3]]))
                    for a, b in list(ram.touched):
                        bytearray.__setitem__(ram, slice(a, b), ram0[a:b])
                    ram.touched = []
                    want_sound = len(cues)
                    if hover.value != orig_hover or sound != want_sound or \
                            any(c != (5, 0x1000, 0x1000, 0x1000) for c in cues):
                        problems.append(('0020D930 mode %d prev %d mag %r angle %08X' % (mode, prev, mag, ang),
                                         'original', orig_hover, cues, 'em_menu_hover', hover.value, sound))
    words = reachable_words(ram0, 0x20D930, HOVER_SIZE)
    if not smoke and words - seen:
        problems.append(('0020D930 coverage', '%d of %d words not executed' % (len(words - seen), len(words))))
    if problems:
        raise AssertionError(problems[:3])
    return '0x20D930 (em_menu_hover_0020D930) %d cases, %d/%d reachable non-branch words' % (
        count, len(words & seen), len(words))


M_08_DOWN = struct.unpack('<f', struct.pack('<I', fbits(0.8) - 1))[0]


def reuse_checks(elf):
    """EM_TEST_FULL=1 runs every check in full; the default run keeps a
    smoke sample."""
    problems, parts = [], []
    for check in (reuse_hover,):
        try:
            parts.append(check(elf, smoke=not reference_mode.FULL))
        except AssertionError as error:
            problems.append((check.__name__, repr(error)[:600]))
    return problems, 'ok: ' + '; '.join(parts)


if __name__ == '__main__':
    main()
