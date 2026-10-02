#!/usr/bin/env python3
"""Execute the ORIGINAL thirteenth-level code (the a19c census rows: the
AREA19 sub-1 placements [34]..[40], [46], [53] and the 0x827B60 objects,
the effect 0x824A90 and the spawner 0x829840; 001386E0's behaviours 2 / 3
/ 4 of the AREA19 creatures, the 001549C0 tendril pieces, the bar-pad test
00194240, the sub-change camera set-up 00197390, the probe chain 001B2B80 /
001B3250 / 001B37D0, the box test 001B34F0, a draw method and its DMA tags,
two light-record helpers) and compare em_level13_port*.c.

docs/LEVEL13_PORT.md. The census delta of the thirteenth-level route
(decomp build/s87/census/a19c_delta.json, new_functions: 36 rows) names
the functions; five of them (0016A4B0, 0016A8B0, 001818D0, 00181950,
00181A70) have the verified translation em_player_closure_10_12_19.c and are
reused, not re-translated; the other 31 are translated in
em_level13_port_*.c and are the entries below (FUNCS). Calls between the
translations run as original code at the top level of the oracle (only
other functions are hooks), and every function is also an entry of its
own.

The harness is tools/test_level12_port_reference.py's (lane L12T, itself
the LEVEL11 / LEVEL10 / LEVEL9 / LEVEL8 / AREA06 / AREA22 / AREA04 design),
copied and owned here, with the thirteenth-level captures, this module's
hook table and cases. Entries that take the original stack pointer (kind
S) run the oracle with sp = STACK_TOP and give the native entry STACK_TOP;
the frame locals live in the compared stack window.

Oracle: the shared EE interpreter with the measured float model (FallEE,
tools/test_player_fall_reference.py) runs the original code resident in the
recorded RAM images of the thirteenth-level route (decomp
build/s87/route_a19c/<beat>/, AREA19 resident in all eight). Before any
case the test checks that the overlay text of every image equals the user's
extract/OVERLAY/AREA19.BIN (text size from the file header) and that the
boot text below 0x241000 equals the pinned ELF, so every executed
instruction is original. Nothing here embeds original bytes; reports hold
counts only.

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

EM_LEVEL13_PORT_ONLY=<label prefix> runs a subset (no coverage / contract
checks). EM_LEVEL13_PORT_SOURCE=<dir> tests another copy of the module
sources (the mutation sweep). EM_LEVEL13_PORT_MISSING=1 lists unexecuted
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
OVERLAY_FILES = {16: DECOMP / 'extract/OVERLAY/AREA19.BIN'}
OUT = ROOT / 'build/level13/port'
MASK = 0xFFFFFFFF
MASK64 = 0xFFFFFFFFFFFFFFFF
RAM_SIZE, SPAD_SIZE = 0x2000000, 0x4000
ARENA = 0x823500
STACK_LO = STACK_TOP - 0x800
STACK_SIZE = STACK_TOP - STACK_LO
BEAT_DIRS = {}
for _b in ('a19c_00_bar840', 'a19c_01_bar841', 'a19c_02_use7', 'a19c_03_slide', 'a19c_04_walkway',
           'a19c_05_door27', 'a19c_06_ladder959', 'a19c_07_door52'):
    BEAT_DIRS[_b] = DECOMP / 'build/s87/route_a19c' / _b
BEATS = list(BEAT_DIRS)
# every a19c image holds AREA19 (overlay id 16) at 0x823504 (sub 0 in
# a19c_00..05, sub 1 in a19c_06 / 07: the same overlay)
RESIDENT = {b: 16 for b in BEATS}
SUB0_BEATS = BEATS[:6]
SUB1_BEATS = BEATS[6:]
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
    # AREA19 overlay (runtime addresses)
    0x823780: ('em_level13_port_00823780', 0x520, [U, S], None),
    0x823D10: ('em_level13_port_00823D10', 0x97C, [U, S], None),
    0x824A90: ('em_level13_port_00824A90', 0x148, [U, S], None),
    0x826570: ('em_level13_port_00826570', 0x2CC, [U], None),
    0x826840: ('em_level13_port_00826840', 0x2EC, [U], None),
    0x826C10: ('em_level13_port_00826C10', 0x818, [U], None),
    0x827430: ('em_level13_port_00827430', 0x104, [U], None),
    0x827550: ('em_level13_port_00827550', 0x234, [U], None),
    0x8279E0: ('em_level13_port_008279E0', 0x124, [U], None),
    0x827B10: ('em_level13_port_00827B10', 0x10, [], I),
    0x827B20: ('em_level13_port_00827B20', 0x38, [], I),
    0x827B60: ('em_level13_port_00827B60', 0x264, [U], None),
    0x829840: ('em_level13_port_00829840', 0x84, [U, S], None),
    0x829A70: ('em_level13_port_00829A70', 0x320, [U], None),
    # boot: 001386E0's behaviours 2 / 3 / 4 and the tendrils
    0x139240: ('em_level13_port_00139240', 0x7A8, [U, U], None),
    0x139E00: ('em_level13_port_00139E00', 0x5A4, [U, U], None),
    0x13A3B0: ('em_level13_port_0013A3B0', 0xF98, [U, U], None),
    0x1545B0: ('em_level13_port_001545B0', 0x108, [U, F, F], I),
    0x154F00: ('em_level13_port_00154F00', 0x2A4, [U, S], None),
    # boot: the others
    0x194240: ('em_level13_port_00194240', 0x26C, [U], I),
    0x197390: ('em_level13_port_00197390', 0xF4, [U, U], None),
    0x1B2B80: ('em_level13_port_001B2B80', 0x64, [U, U, U], I),
    0x1B3250: ('em_level13_port_001B3250', 0x9C, [U, U, F, S], I),
    0x1B34F0: ('em_level13_port_001B34F0', 0x8C, [U, U, U], I),
    0x1B37D0: ('em_level13_port_001B37D0', 0x218, [U, F, F, S], F),
    0x1CB140: ('em_level13_port_001CB140', 0xA4, [U, U], None),
    0x1CB1F0: ('em_level13_port_001CB1F0', 0x8, [U], None),
    0x1D42E0: ('em_level13_port_001D42E0', 0x14C, [I, U], None),
    0x1D4430: ('em_level13_port_001D4430', 0x10, [U, U], None),
    0x1F6AC0: ('em_level13_port_001F6AC0', 0x10, [U], I),
    0x1F6B90: ('em_level13_port_001F6B90', 0xC, [], None),
}
OVERLAY_OF = {entry: 16 for entry in FUNCS if entry >= ARENA}
# The entries whose passing cases are all contract sites (they return from
# their own body): the default run and EM_TEST_FULL=1.
RETURN_SITES = tuple(e for e, (_, _, _, r) in FUNCS.items() if r is not None)
RETURN_SITES_FULL = RETURN_SITES


def has_result(entry):
    return FUNCS[entry][3] is not None


# Hooks, in the field order of EmLevel13PortHooks (after ctx and bytes; the
# +0x4C method's w_callback follows them).
# name -> (original address, [(ctype, register)], result kind or None)
HOOKS = [
# BEGIN GENERATED HOOKS
    ('w_001026A0', 0x1026A0, [(U, 4), (U, 5), (U, 6)], None),
    ('w_00102760', 0x102760, [(U, 4), (U, 5)], None),
    ('w_001028B8', 0x1028B8, [(U, 4), (U, 5), (U, 6)], None),
    ('w_00102918', 0x102918, [(U, 4), (U, 5), (U, 6)], None),
    ('w_00102948', 0x102948, [(U, 4), (U, 5)], None),
    ('w_00102958', 0x102958, [(U, 4), (U, 5)], None),
    ('w_001029C0', 0x1029C0, [(U, 4)], None),
    ('w_00102B08', 0x102B08, [(U, 4), (U, 5), (F, 12)], None),
    ('w_00102BB0', 0x102BB0, [(U, 4), (U, 5), (F, 12)], None),
    ('w_00102C58', 0x102C58, [(U, 4), (U, 5), (U, 6)], None),
    ('w_00103230', 0x103230, [(U, 4), (U, 5), (F, 12)], None),
    ('w_0011DE90', 0x11DE90, [(F, 12)], F),
    ('w_0011DF78', 0x11DF78, [(F, 12)], F),
    ('w_0011E2A8', 0x11E2A8, [(F, 12)], F),
    ('w_00122BB8', 0x122BB8, [], I),
    ('w_001281C0', 0x1281C0, [(F, 12)], I),
    ('w_0013BA20', 0x13BA20, [(U, 4), (U, 5)], None),
    ('w_0013BBB0', 0x13BBB0, [(U, 4), (U, 5)], None),
    ('w_0013C8C0', 0x13C8C0, [(U, 4), (U, 5)], None),
    ('w_0018D7B0', 0x18D7B0, [(U, 4), (I, 5)], None),
    ('w_001916C0', 0x1916C0, [(U, 4), (U, 5), (I, 6)], None),
    ('w_0019A570', 0x19A570, [(U, 4), (U, 5), (I, 6), (I, 7)], I),
    ('w_0019AD00', 0x19AD00, [(U, 4), (U, 5), (I, 6)], I),
    ('w_0019C6F0', 0x19C6F0, [(I, 4), (I, 5)], None),
    ('w_001A2370', 0x1A2370, [(U, 4), (U, 5)], None),
    ('w_001A7B80', 0x1A7B80, [(U, 4)], I),
    ('w_001AFA90', 0x1AFA90, [(I, 4)], U),
    ('w_001AFC10', 0x1AFC10, [(U, 4)], None),
    ('w_001B0FD0', 0x1B0FD0, [(U, 4)], I),
    ('w_001B10B0', 0x1B10B0, [(U, 4), (I, 5), (I, 6)], None),
    ('w_001B1240', 0x1B1240, [(U, 4), (F, 12), (F, 13)], F),
    ('w_001B1270', 0x1B1270, [(U, 4), (F, 12), (F, 13)], F),
    ('w_001B12B0', 0x1B12B0, [(F, 12), (F, 13), (F, 14)], F),
    ('w_001B1470', 0x1B1470, [(F, 12)], F),
    ('w_001B1560', 0x1B1560, [(U, 4), (U, 5), (F, 12)], I),
    ('w_001B15D0', 0x1B15D0, [(U, 4), (U, 5)], F),
    ('w_001B17A0', 0x1B17A0, [(U, 4)], I),
    ('w_001B1B70', 0x1B1B70, [(U, 4)], None),
    ('w_001B1E20', 0x1B1E20, [(I, 4), (I, 5)], None),
    ('w_001B1EA0', 0x1B1EA0, [(I, 4), (U, 5), (U, 6), (I, 7)], I),
    ('w_001B2B10', 0x1B2B10, [(U, 4), (U, 5), (U, 6)], None),
    ('w_001B2D00', 0x1B2D00, [(U, 4), (U, 5)], I),
    ('w_001B2F70', 0x1B2F70, [(U, 4), (U, 5)], I),
    ('w_001B6660', 0x1B6660, [(U, 4)], None),
    ('w_001B6F00', 0x1B6F00, [(U, 4), (U, 5), (F, 12)], None),
    ('w_001BA1A0', 0x1BA1A0, [(U, 4), (U, 5)], None),
    ('w_001BA1C0', 0x1BA1C0, [(U, 4), (I, 5)], I),
    ('w_001BA1F0', 0x1BA1F0, [(U, 4)], I),
    ('w_001C4760', 0x1C4760, [(I, 4), (I, 5)], None),
    ('w_001C47A0', 0x1C47A0, [(I, 4), (I, 5)], None),
    ('w_001C47E0', 0x1C47E0, [(I, 4), (I, 5)], None),
    ('w_001C6380', 0x1C6380, [(U, 4)], None),
    ('w_001C63E0', 0x1C63E0, [(U, 4), (I, 5)], None),
    ('w_001C64F0', 0x1C64F0, [(U, 4), (F, 12)], None),
    ('w_001C67E0', 0x1C67E0, [(U, 4), (I, 5), (F, 12), (F, 13)], None),
    ('w_001C68C0', 0x1C68C0, [(U, 4)], None),
    ('w_001C7420', 0x1C7420, [(U, 4), (I, 5), (I, 6)], None),
    ('w_001CA770', 0x1CA770, [(U, 4)], None),
    ('w_001CAAC0', 0x1CAAC0, [(U, 4), (U, 5)], None),
    ('w_001CCF70', 0x1CCF70, [(U, 4)], I),
    ('w_001CFA60', 0x1CFA60, [(U, 4), (U, 5), (F, 12), (F, 13)], None),
    ('w_001CFB50', 0x1CFB50, [(U, 4), (I, 5), (U, 6), (F, 12), (F, 13), (F, 14), (F, 15), (F, 16)], None),
    ('w_001CFBE0', 0x1CFBE0, [(I, 4), (I, 5), (U, 6), (U, 7), (I, 8)], None),
    ('w_001D1F80', 0x1D1F80, [(I, 4), (I, 5), (I, 6)], None),
    ('w_001D1FF0', 0x1D1FF0, [(I, 4), (I, 5)], None),
    ('w_001D2090', 0x1D2090, [(I, 4), (U, 5)], None),
    ('w_001D2910', 0x1D2910, [(I, 4)], I),
    ('w_001D6F60', 0x1D6F60, [(I, 4), (Q, 5), (I, 6)], None),
    ('w_001D8C20', 0x1D8C20, [(I, 4)], None),
    ('w_001EFD90', 0x1EFD90, [(I, 4), (U, 5), (U, 6)], None),
    ('w_001EFEB0', 0x1EFEB0, [(I, 4), (U, 5)], None),
    ('w_001F02C0', 0x1F02C0, [(U, 4), (I, 5), (F, 12)], None),
    ('w_001F0460', 0x1F0460, [(I, 4), (U, 5)], None),
    ('w_001F1110', 0x1F1110, [(U, 4), (I, 5)], None),
    ('w_001F1180', 0x1F1180, [(U, 4)], None),
    ('w_001F4BF0', 0x1F4BF0, [(U, 4), (U, 5)], None),
    ('w_001F5940', 0x1F5940, [(I, 4), (U, 5), (I, 6)], None),
    ('w_001F6640', 0x1F6640, [(U, 4)], None),
    ('w_001F6BA0', 0x1F6BA0, [], None),
    ('w_001FB0B0', 0x1FB0B0, [(I, 4)], None),
    ('w_001FBD50', 0x1FBD50, [(U, 4), (I, 5), (I, 6), (F, 12)], None),
    ('w_001FC3C0', 0x1FC3C0, [(U, 4), (U, 5), (I, 6), (F, 12), (F, 13)], None),
    ('w_001FC520', 0x1FC520, [(U, 4)], None),
    ('w_0021B9A0', 0x21B9A0, [(I, 4), (F, 12), (F, 13)], None),
    ('w_0021BE40', 0x21BE40, [(U, 4), (U, 5)], I),
    ('w_0021BED0', 0x21BED0, [(U, 4)], I),
    ('w_0021BF90', 0x21BF90, [(U, 4), (U, 5)], None),
    ('w_0021C040', 0x21C040, [(U, 4), (U, 5)], None),
# END GENERATED HOOKS
]
BY_ADDRESS = {address: (name, args, result) for name, address, args, result in HOOKS}

# Callees that run as original code inside the oracle (writes replayed to
# the native side). Pure helpers only: none of them reaches hardware; each
# is first rehearsed with the argument registers its hook does not pass
# poisoned (Oracle.rehearse).
RUN = {0x1026A0, 0x102760, 0x1028B8, 0x102918, 0x102948, 0x102958, 0x1029C0, 0x102B08, 0x102BB0, 0x102C58,
       0x103230, 0x11DE90, 0x11DF78, 0x11E2A8, 0x1281C0, 0x1B1240, 0x1B1270, 0x1B12B0, 0x1B1470, 0x1B1560,
       0x1B15D0}

# 0013A3B0 dispatches its state byte through a 10-entry table at 0x26D1E0
# (the jr at 0x13A408); the translation is a switch, so the table's loads
# are left out of the access comparison (TABLE_BYTES).
JUMP_TABLES = {0x13A408: (0x26D1E0, 10)}
TABLE_BYTES = frozenset(range(0x26D1E0, 0x26D1E0 + 40))

CALLBACK_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_uint32)
# Stores of a callee-saved register at an $sp offset that are frame locals,
# not bookkeeping: 001B37D0 keeps its distance argument (in $f22) in the
# frame vector at sp + 0x58.
LOCAL_STORES = {0x1B3848}


def stack_frame_op(word, pc=None):
    """A save or restore of a callee-saved register ($s0..$s7, $fp, $ra;
    $f20..$f31) at an $sp offset: the frame bookkeeping, which the
    translation has no counterpart for (LOCAL_STORES excepted)."""
    if pc in LOCAL_STORES:
        return False
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
SOURCES = ('em_level13_port_area19.c', 'em_level13_port_boot.c', 'em_level13_port_creature.c')
def header_checks(source_dir=None):
    """The ctypes Hooks structure above is built from HOOKS; the C side's
    EmLevel13PortHooks (em_level13_port.h) and its wrappers
    (em_level13_port_internal.h) are text. Check that the header's generated
    block lists exactly the HOOKS entries, in order, with the same argument
    and result types, and that each hook has one wrapper l13_c_<address>
    that calls it and latches its own address."""
    base = Path(source_dir) if source_dir else ROOT / 'src/game'
    ctype = {U: 'uint32_t', I: 'int32_t', F: 'float', Q: 'uint64_t'}
    header = (base / 'em_level13_port.h').read_text()
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
    internal = (base / 'em_level13_port_internal.h').read_text()
    wrappers = internal.split('/* BEGIN GENERATED WRAPPERS */')[1].split('/* END GENERATED WRAPPERS */')[0]
    for hname, address, _, _ in HOOKS:
        tag = hname[2:]
        body = re.findall(r'static inline int l13_c_%s\(.*?\n\}\n' % tag, wrappers, re.S)
        if len(body) != 1 or ('o->h->%s(' % hname) not in body[0] or ('0x%08Xu' % address) not in body[0]:
            problems.append('%s: wrapper l13_c_%s missing or not calling / latching it' % (hname, tag))
    return problems, len(found)


def build(source_dir=None):
    """Compile the module (or the copies in `source_dir`, a mutation sweep)
    into a private library under build/level13/port."""
    OUT.mkdir(parents=True, exist_ok=True)
    base = Path(source_dir) if source_dir else ROOT / 'src/game'
    stem = 'level13_port' if not source_dir else 'level13_port_%d' % os.getpid()
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
        self.pc = pc
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
        ee.pc = None
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
            if key is not None and in_window(key) and stack_frame_op(ee.word, ee.pc):
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
            if key is not None and in_window(key) and stack_frame_op(ee.word, ee.pc):
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
    """Every image must hold AREA19 with its text equal to the user's
    extract/OVERLAY file; every image's boot text must equal the pinned ELF:
    every instruction the oracle executes is original. The designed-record
    area must be zero."""
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
        assert not any(ram[FREE:FREE + 0x40000]), (beat, 'the designed-record area is not zero')
        captures[beat] = (ram, spad)
    return captures


# the entries that reach a node's +0x4C method
SELF_ENTRIES = {0x826570, 0x826840, 0x826C10, 0x827430, 0x827550, 0x827B60, 0x829A70, 0x154F00}
SCRIBBLED_CALLBACKS = set()
HEAVY = {0x823D10, 0x823780, 0x13A3B0, 0x139240, 0x154F00, 0x1B37D0}


def callbacks_of(entry, native_args, ram):
    """The indirect calls the oracle stubs: the entry's own node's +0x4C
    method (for the entries that reach it)."""
    out = set(SCRIBBLED_CALLBACKS)
    if entry in SELF_ENTRIES:
        out.add(u32(ram, native_args[0] + 0x4C))
    return {c for c in out if c not in BY_ADDRESS}


# ----------------------------------------------------------------------------
# cases
# ----------------------------------------------------------------------------
FREE = 0x1C00000          # RAM zero in every capture: the designed records live here
FULL_ONLY = set()         # labels of the cases EM_TEST_FULL=1 adds
CAPTURES = {}
FRAME = 0x70003B68
B0, B1, B2, B3, B4, B5, B6, B7 = BEATS
NODE_A = FREE + 0x1000    # a designed node (0x2F0 bytes)
NODE_B = FREE + 0x2000
# AREA19 records (section 1 of docs/LEVEL13_PORT.md; the same pool nodes in
# the images that hold them)
CREATURES = (0x7A9FB0, 0x7AA2A0, 0x7AA590)    # behaviour 0x1383C0 (001386E0's family), a19c_00..05
TENDRILS = (0x7BBCA0, 0x7BBF90, 0x7BC280, 0x7BC570, 0x7BC860, 0x7BCB50)   # 0x1546C0, a19c_00..05
SUB1 = {0x823780: 0x7AE920, 0x823D10: 0x7AE630, 0x826570: 0x7AE340, 0x826840: 0x7AE050,
        0x826C10: 0x7ADD60, 0x827430: 0x7ADA70, 0x829A70: 0x7AFAC0}
N34 = 0x7AD780                                 # [34] 0x8279E0, a19c_06
OBJS = (0x7A8540, 0x7A8830, 0x7A8B20, 0x7AAE60, 0x7AC000, 0x7AC5E0, 0x7AC8D0, 0x7ACBC0, 0x7ACEB0, 0x7AD1A0,
        0x7AD490)                              # the 0x827B60 objects, a19c_06
CR = 0x7A9FB0
TEN = 0x7BBCA0


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


def around(value):
    """The float `value` exactly and one unit in the last place either
    side, as bit patterns."""
    return (fbits(value), up(value), down(value))


def ff(b):
    """A float from its bits."""
    return struct.unpack('<f', struct.pack('<I', b & MASK))[0]


def case_list(elf):
    rng = random.Random(0x13C)
    cases = []
    labels = set()

    def add(label, beat, entry, args, patches=(), spad=(), queues=None, default=None, run_set=RUN, full=False):
        """full=True: the case runs only with EM_TEST_FULL=1. A label seen
        before names the same inputs (overlapping bound lists): skipped."""
        if label in labels:
            return
        labels.add(label)
        if full:
            FULL_ONLY.add(label)
        args = [STACK_TOP if k == S else a for k, a in zip(FUNCS[entry][2], list(args) + [STACK_TOP] * 8)]
        spad = list(spad) + [(a, v) for a, v in patches if a >= 0x70000000]
        patches = [(a, v) for a, v in patches if a < 0x70000000]
        cases.append((label, beat, entry, tuple(args), list(patches), list(spad),
                      Script(queues, default), frozenset(run_set)))

    capture_cases(add)
    captured = len(cases)
    overlay_cases(add, rng)
    lift_cases(add, rng)
    creature_cases(add, rng)
    grab_cases(add, rng)
    tendril_cases(add, rng)
    boot_cases(add, rng)
    bound_cases(add)
    survivor_cases(add)
    return cases, captured, len(cases) - captured


def live(ram, node, callback):
    return ram[node] != 0 and u32(ram, node + 0x10) == callback


def capture_cases(add):
    """Every entry on the captured state of every image it applies to."""
    for beat in SUB0_BEATS:
        ram, _ = CAPTURES[beat]
        for n in CREATURES:
            if not live(ram, n, 0x1383C0):
                continue
            e = n + 0x1F0
            for entry in (0x139240, 0x139E00, 0x13A3B0):
                add('capture %s %06X @%X' % (beat, entry, n), beat, entry, [n, e], bones(n))
            add('capture %s 1B2B80 @%X' % (beat, n), beat, 0x1B2B80, [n, FREE + 0x100, FREE + 0x200])
            add('capture %s 1B3250 @%X' % (beat, n), beat, 0x1B3250, [n, FREE + 0x100, fb(15.0)])
            add('capture %s 1B37D0 @%X' % (beat, n), beat, 0x1B37D0, [n, fb(10.0), fb(15.0)])
            add('capture %s 1B34F0 @%X' % (beat, n), beat, 0x1B34F0, [0x810360, FREE + 0x300, n + 0xB0],
                [(FREE + 0x300, fw(4.0, 9.0, 0.0, 1.0))])
            add('capture %s 1CB1F0 @%X' % (beat, n), beat, 0x1CB1F0, [n])
        for n in TENDRILS:
            if not live(ram, n, 0x1546C0):
                continue
            add('capture %s 154F00 @%X' % (beat, n), beat, 0x154F00, [n])
            add('capture %s 1545B0 @%X' % (beat, n), beat, 0x1545B0, [n, u32(ram, 0x810360), u32(ram, 0x810368)])
        add('capture %s 194240' % beat, beat, 0x194240, [PLAYER])
        add('capture %s 197390' % beat, beat, 0x197390, [CAM, PLAYER])
    for beat in BEATS:
        add('capture %s 1F6AC0 D270' % beat, beat, 0x1F6AC0, [0x25D270])
        add('capture %s 1F6AC0 D2C0' % beat, beat, 0x1F6AC0, [0x25D2C0])
        add('capture %s 1F6B90' % beat, beat, 0x1F6B90, [])
        add('capture %s 827B10' % beat, beat, 0x827B10, [])
        add('capture %s 827B20' % beat, beat, 0x827B20, [])
    for beat in SUB1_BEATS:
        ram, _ = CAPTURES[beat]
        for entry, n in SUB1.items():
            if live(ram, n, entry):
                add('capture %s %06X' % (beat, entry), beat, entry, [n], bones(n))
        if live(ram, N34, 0x8279E0):
            add('capture %s 8279E0' % beat, beat, 0x8279E0, [N34], bones(N34))
        for n in OBJS:
            if live(ram, n, 0x827B60):
                add('capture %s 827B60 @%X' % (beat, n), beat, 0x827B60, [n], bones(n))
        add('capture %s 194240' % beat, beat, 0x194240, [PLAYER])
        add('capture %s 197390' % beat, beat, 0x197390, [CAM, PLAYER])


def overlay_cases(add, rng):
    """The AREA19 overlay entries, their state bytes patched on the captured
    sub-1 nodes (a19c_07; [34] and the 0x827B60 objects a19c_06) and on
    designed nodes."""
    B = B7
    # 0x823780 ([40]): the states, the area and story bytes, the columns
    n = SUB1[0x823780]
    blk = n + 0x1F0
    for st in (2, 3, 5):
        add('ov 823780 s%d' % st, B, 0x823780, [n], [(n + 4, b8(st))])
    for b7b, b778, b7f8 in ((0xFF, 0xFF, 0), (0xFF, 0, 0), (0, 0xFF, 1), (0, 0, 0), (0, 0, 1)):
        for area in (1, 8):
            add('ov 823780 s0 %X %X %d a%d' % (b7b, b778, b7f8, area), B, 0x823780, [n],
                [(n + 4, b8(0)), (0x81077B, b8(b7b)), (0x810778, b8(b778)), (0x8107F8, b8(b7f8)), (0x810702, b8(area))],
                queues={0x122BB8: [0x40000000, 0x1234567, -0x40000000, 0x7654321]})
    for area in (8, 5, 4, 2, 1, 3):
        for sub, b7f8 in ((0, 0), (0, 1), (1, 0), (2, 0)):
            if area != 1 and (sub, b7f8) != (0, 0):
                continue
            add('ov 823780 s1 a%d +5 %d %d' % (area, sub, b7f8), B, 0x823780, [n],
                [(n + 4, b8(1)), (n + 5, b8(sub)), (0x810702, b8(area)), (0x8107F8, b8(b7f8))])
    for p0, p1, d0, s0, s1 in ((0.5, 1.2, 0.008, 0x12345678, -0x76543210), (1.99, 1.8, 0.005, 0, 0x7FFFFFFF),
                               (2.5, 0.1, 0.3, -1, 0x10000), (1.0, 1.75, 0.01, 0x55555555, 0x2AAAAAAA)):
        add('ov 823780 cols %g %g %g' % (p0, p1, d0), B, 0x823780, [n],
            [(n + 4, b8(1)), (n + 5, b8(1)), (0x810702, b8(1)), (blk, fw(d0, 0.5, p0, p1, s0, s1))])
    # 0x823D10 ([39])
    n = SUB1[0x823D10]
    blk = n + 0x1F0
    for st in (3, 4):
        add('ov 823D10 s%d' % st, B, 0x823D10, [n], [(n + 4, b8(st))])
    for b7fb, b7f8, area in ((0xFF, 0, 1), (0, 1, 1), (0, 0, 1), (0, 0, 5)):
        add('ov 823D10 s0 %X %d a%d' % (b7fb, b7f8, area), B, 0x823D10, [n],
            [(n + 4, b8(0)), (0x8107FB, b8(b7fb)), (0x8107F8, b8(b7f8)), (0x810702, b8(area))],
            queues={0x122BB8: [0x3456789A], 0x1CCF70: [0x123]})
    for area in (5, 4, 2, 3):
        add('ov 823D10 s1 a%d' % area, B, 0x823D10, [n], [(n + 4, b8(1)), (0x810702, b8(area))])
    for st in (1, 2):
        for b7f8 in (0, 1):
            for size, b7f9 in ((1.0, 0), (0.0, 0), (0.5, 0x80), (0.75, 0x7F)):
                for h in ((0xF4240, 0xF4241) if size == 1.0 and b7f8 == 0 else (0x10,)):
                    for cd in ((0, 3) if size == 1.0 and h == 0x10 else (0,)):
                        add('ov 823D10 s%d f%d sz%g %X h%X cd%d' % (st, b7f8, size, b7f9, h, cd), B, 0x823D10, [n],
                            [(n + 4, b8(st)), (0x810702, b8(1)), (0x8107F8, b8(b7f8)), (0x8107F9, b8(b7f9)),
                             (blk + 0x14, w32(cd)), (blk + 0x1C, w32(0x13579BDF)), (blk + 0x24, f32b(size)),
                             (blk + 0x28, f32b(1.0)), (blk + 0x2C, f32b(1.99)), (blk + 0x30, f32b(0.5))],
                            queues={0x1CCF70: [h]})
    for c18, size, t28 in ((59, 0.5, 1.0), (60, 0.5, 1.0), (60, 0.002, 1.0), (70, 0.5, 4.995), (70, 0.002, 4.995)):
        add('ov 823D10 s2 c%d %g %g' % (c18, size, t28), B, 0x823D10, [n],
            [(n + 4, b8(2)), (0x810702, b8(1)), (blk + 0x18, w32(c18)), (blk + 0x24, f32b(size)),
             (blk + 0x28, f32b(t28)), (blk + 0x2C, f32b(2.5)), (blk + 0x30, f32b(1.995))])
    # 0x824A90 (the effect, on a designed node in a19c_02)
    s = NODE_A
    mtx = [(s + 0xB0, fw(900.0, 200.0, 950.0, 1.0)), (s + 0xC0, fw(0.1, 0.5, 0.0, 0.0))]
    for st in (0, 1, 2, 3, 4):
        for t in ((0.5, 1.47, 1.5) if st == 1 else (0.5,)):
            add('ov 824A90 s%d %g' % (st, t), B2, 0x824A90, [s], mtx + [(s + 4, b8(st)), (s + 0x1F0, f32b(t))],
                queues={0x1CCF70: [0x55], 0x122BB8: [0x2468ACE0]})
    # 0x826570 ([38]) and 0x826840 ([37]), the valves
    for entry in (0x826570, 0x826840):
        n = SUB1[entry]
        for st in (2, 3, 4):
            for r in (0, 1):
                add('ov %06X s%d r%d' % (entry, st, r), B, entry, [n], [(n + 4, b8(st))], queues={0x1B17A0: [r]})
        for r0, a, b in ((1, 0, 0), (0, 1, 0), (0, 0, 1), (0, 0, 0)):
            for b779 in ((0, 0xFF) if entry == 0x826840 else (0,)):
                add('ov %06X s0 %d %d %d %X' % (entry, r0, a, b, b779), B, entry, [n],
                    [(n + 4, b8(0)), (0x810779, b8(b779))], queues={0x1B0FD0: [r0], 0x1BA1C0: [a, b]})
        for b779 in ((0, 0xFF) if entry == 0x826840 else (0,)):
            for bb in (4, 0xFB):
                for f9 in ((0, 3, 0x80) if entry == 0x826840 else (0,)):
                    add('ov %06X s1 +5 0 %X %X %X' % (entry, b779, bb, f9), B, entry, [n],
                        [(n + 4, b8(1)), (n + 5, b8(0)), (n + 0xB, b8(bb)), (0x810779, b8(b779)), (0x8107F9, b8(f9))],
                        queues={0x1B17A0: [1]})
            for c28 in (0x76, 0x77, 0x78, -5):
                for done, b77b in (((0, 0), (1, 0xFF), (1, 0)) if c28 == 0x77 else ((0, 0),)):
                    add('ov %06X s1 +5 1 %X %d %X %X' % (entry, c28 & 0xFFFF, done, b77b, b779), B, entry, [n],
                        [(n + 4, b8(1)), (n + 5, b8(1)), (n + 0x28, h16(c28)), (0x81077B, b8(b77b)),
                         (0x810779, b8(b779)), (0x8107F9, b8(0xF3))],
                        queues={0x1BA1F0: [done], 0x1B17A0: [done]})
            add('ov %06X s1 +5 2 %X' % (entry, b779), B, entry, [n],
                [(n + 4, b8(1)), (n + 5, b8(2)), (0x810779, b8(b779))], queues={0x1B17A0: [1]})
    # 0x827430 ([35])
    n = SUB1[0x827430]
    for st in (2, 3, 5):
        add('ov 827430 s%d' % st, B, 0x827430, [n], [(n + 4, b8(st))])
    for r in (0, 1):
        add('ov 827430 s0 r%d' % r, B, 0x827430, [n], [(n + 4, b8(0))], queues={0x1B0FD0: [r]})
    for mine in (370.0, 380.0, -0.0):
        add('ov 827430 s1 %g' % mine, B, 0x827430, [n],
            [(n + 4, b8(1)), (n + 0x2E4, f32b(mine)), (0x7ADD60 + 0xB4, f32b(370.0 if mine != -0.0 else 0.0))])
    # 0x827550 ([53], a designed node in a19c_07)
    s = NODE_A
    give = [(s + 0xD, b8(5)), (s + 0x4C, w32(0x8277C0))]
    for st in (2, 3, 4):
        add('ov 827550 s%d' % st, B, 0x827550, [s], give + [(s + 4, b8(st))])
    for a, b in ((1, 1), (0, 0), (0, 1)):
        add('ov 827550 s0 %d %d' % (a, b), B, 0x827550, [s], give + [(s + 4, b8(0))], queues={0x1BA1C0: [a, b]})
    for y in (444.0, 445.0, 452.0, 460.0, 461.0):
        for r in ((0, 1) if y == 452.0 else (0,)):
            add('ov 827550 s1 +5 0 y%g r%d' % (y, r), B, 0x827550, [s],
                give + [(s + 4, b8(1)), (s + 5, b8(0)), (0x810354, f32b(y))], queues={0x1B1EA0: [r]})
    for c28 in (0x12A, 0x12B, 0x7FFF):
        for done in (0, 1):
            add('ov 827550 s1 +5 1 %X %d' % (c28, done), B, 0x827550, [s],
                give + [(s + 4, b8(1)), (s + 5, b8(1)), (s + 0x28, h16(c28))], queues={0x1BA1F0: [done]})
    for sub in (2, 3):
        add('ov 827550 s1 +5 %d' % sub, B, 0x827550, [s], give + [(s + 4, b8(1)), (s + 5, b8(sub))])
    # 0x8279E0 ([34], a19c_06)
    B = B6
    n = N34
    for st in (2, 3, 4):
        add('ov 8279E0 s%d' % st, B, 0x8279E0, [n], [(n + 4, b8(st))])
    for r in (0, 1):
        add('ov 8279E0 s0 r%d' % r, B, 0x8279E0, [n], [(n + 4, b8(0))], queues={0x1BA1C0: [r]})
    for area, b8d in ((1, 0), (1, 4), (2, 0)):
        add('ov 8279E0 s1 +5 0 a%d %d' % (area, b8d), B, 0x8279E0, [n],
            [(n + 4, b8(1)), (n + 5, b8(0)), (0x810702, b8(area)), (0x70003B8D, b8(b8d))])
    for done in (0, 1):
        add('ov 8279E0 s1 +5 1 %d' % done, B, 0x8279E0, [n], [(n + 4, b8(1)), (n + 5, b8(1))],
            queues={0x1BA1F0: [done]})
    add('ov 8279E0 s1 +5 2', B, 0x8279E0, [n], [(n + 4, b8(1)), (n + 5, b8(2))])
    # 0x827B60 (a19c_06's first object)
    n = OBJS[0]
    for st in (2, 3, 4):
        add('ov 827B60 s%d' % st, B, 0x827B60, [n], [(n + 4, b8(st))])
    for b79e, r in ((0xFF, 0), (0, 0), (0, 1)):
        add('ov 827B60 s0 %X %d' % (b79e, r), B, 0x827B60, [n], [(n + 4, b8(0)), (0x81079E, b8(b79e))],
            queues={0x1B0FD0: [r]})
    for b81e in (1, 0, 2):
        add('ov 827B60 s1 +5 0 %d' % b81e, B, 0x827B60, [n],
            [(n + 4, b8(1)), (n + 5, b8(0)), (0x81081E, b8(b81e)), (n + 0x9A, b8(0x2A))])
    for c28, hit in ((1, 0), (1, 1), (2, 0), (0, 0)):
        add('ov 827B60 s1 +5 1 %d %d' % (c28, hit), B, 0x827B60, [n],
            [(n + 4, b8(1)), (n + 5, b8(1)), (n + 0x28, h16(c28))], queues={0x19A570: [hit]})
    add('ov 827B60 s1 +5 2', B, 0x827B60, [n], [(n + 4, b8(1)), (n + 5, b8(2))])
    # 0x829840 (the spawner): the spawned object designed
    for obj in (0, NODE_B):
        add('ov 829840 %X' % obj, B, 0x829840, [NODE_A], [(NODE_A, fw(*[float(i) for i in range(16)]))],
            queues={0x1AFA90: [obj]})
    # 0x829A70 ([46], a19c_07)
    B = B7
    n = SUB1[0x829A70]
    turn = [(0x275B40, w32(FREE + 0x800)), (FREE + 0x808, w32(NODE_B)), (FREE + 0x80C, w32(NODE_B + 0x100))]
    for st in (2, 3, 4):
        add('ov 829A70 s%d' % st, B, 0x829A70, [n], [(n + 4, b8(st))])
    for r, b838, child, again in ((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 0, 0), (0, 0, NODE_A, 0), (0, 0, NODE_A, 1)):
        q = {0x1B0FD0: [r], 0x1AFA90: [child]}
        if again:
            q[0x1C6380] = [Scribble(None, [(0x810838, b8(1))])]
        add('ov 829A70 s0 %d %d %X %d' % (r, b838, child, again), B, 0x829A70, [n],
            turn + [(n + 4, b8(0)), (0x810838, b8(b838)), (n + 0xE, h16(0x1234))], queues=q)
    for bb, child, b8f in ((5, NODE_A, 2), (5, 0, 1), (4, 0, 2), (0, 0, 2), (0xFF, NODE_A, 0)):
        add('ov 829A70 s1 +5 0 %X %X %d' % (bb, child, b8f), B, 0x829A70, [n],
            turn + [(n + 4, b8(1)), (n + 5, b8(0)), (n + 0xB, b8(bb)), (n + 0x2E8, w32(child)),
                    (n + 0x2EC, w32(0xABCD5678)), (0x70003B8F, b8(b8f))])
    for done in (0, 1):
        add('ov 829A70 s1 +5 1 %d' % done, B, 0x829A70, [n], [(n + 4, b8(1)), (n + 5, b8(1))],
            queues={0x1BA1F0: [done]})
    add('ov 829A70 s1 +5 2', B, 0x829A70, [n], [(n + 4, b8(1)), (n + 5, b8(2))])


def lift_cases(add, rng):
    """0x826C10 ([36], the lift) on its a19c_07 node, every state and the
    player box."""
    B = B7
    n = SUB1[0x826C10]
    for st in (2, 3, 5):
        add('lift s%d' % st, B, 0x826C10, [n], [(n + 4, b8(st))])
    for r, b779, f9 in ((1, 0, 0), (0, 0xFF, 0), (0, 0, 0), (0, 0, 1)):
        add('lift s0 %d %X %d' % (r, b779, f9), B, 0x826C10, [n], [(n + 4, b8(0)), (0x810779, b8(b779)),
                                                                     (0x8107F9, b8(f9))], queues={0x1B0FD0: [r]})
    for cd, b779, f9 in ((1, 0, 0), (1, 0, 0x21), (2, 0, 0), (5, 0xFF, 0)):
        add('lift s4 +5 0 cd%d %X %X' % (cd, b779, f9), B, 0x826C10, [n],
            [(n + 4, b8(4)), (n + 5, b8(0)), (n + 0x2EC, w32(cd)), (0x810779, b8(b779)), (0x8107F9, b8(f9))])
    for x, z in ((855.0, 853.0), (850.0, 853.0), (859.6, 853.0), (855.0, 850.5), (855.0, 855.0), (849.0, 853.0)):
        add('lift s4 box %g %g' % (x, z), B, 0x826C10, [n],
            [(n + 4, b8(4)), (n + 5, b8(0)), (n + 0x2EC, w32(0)), (0x810350, f32b(x)), (0x810358, f32b(z))])
    for c28, f9 in ((0xB2, 0x30), (0xB3, 0x30), (0xB3, 0x31), (0xB3, 0x3F)):
        add('lift s4 +5 1 %X %X' % (c28, f9), B, 0x826C10, [n],
            [(n + 4, b8(4)), (n + 5, b8(1)), (n + 0x28, h16(c28)), (n + 0x2E4, f32b(0.083333336)),
             (n + 0x2E8, f32b(370.0)), (0x8107F9, b8(f9))])
    add('lift s4 +5 2', B, 0x826C10, [n], [(n + 4, b8(4)), (n + 5, b8(2))])
    add('lift s1 done', B, 0x826C10, [n], [(n + 4, b8(1))], queues={0x1BA1F0: [1]})
    for c28 in (0, 0x44, 0x45, -3):
        add('lift s1 count %X' % (c28 & 0xFFFF), B, 0x826C10, [n], [(n + 4, b8(1)), (n + 0x28, h16(c28))])
    for y, rest, f2e0 in ((380.0, 370.0, 0), (370.0, 370.0, 0), (369.0, 370.0, 1), (370.1, 370.0, 0)):
        add('lift s1 drop %g %g %d' % (y, rest, f2e0), B, 0x826C10, [n],
            [(n + 4, b8(1)), (n + 0x28, h16(0x46)), (n + 0xB4, f32b(y)), (n + 0x2E8, f32b(rest)),
             (n + 0x2E4, f32b(0.4)), (n + 0x2E0, w32(f2e0))])


def creature_cases(add, rng):
    """001386E0's behaviours 2 (00139240) and 3 (00139E00) on a19c_02's
    creature, the state bytes and the behaviour block patched; the distance
    (001B15D0) and the facing test (001B1560) scripted."""
    B = B2
    n, e = CR, CR + 0x1F0
    base = bones(n)
    ram, _ = CAPTURES[B]
    side = (ram[n + 0xD] & 0x80) >> 7
    t1 = ram[0x2451A0 + 16 * side:0x2451A0 + 16 * side + 16]
    t2 = ram[0x2451C0 + 16 * side:0x2451C0 + 16 * side + 16]
    pick1 = {v: t1.index(v) for v in set(t1)}
    pick2 = {v: t2.index(v) for v in set(t2)}
    ns = RUN - {0x1B15D0, 0x1B1560}
    # 00139240 state 0
    for dist in (40.0, 60.0):
        for v in sorted(pick1):
            if dist == 40.0 and v != sorted(pick1)[0]:
                continue
            add('cr 139240 s0 %g t%d' % (dist, v), B, 0x139240, [n, e], base + [(n + 6, b8(0))],
                queues={0x1B15D0: [fb(dist), fb(100.0)], 0x122BB8: [pick1[v] << 11]}, run_set=ns)
    # state 1, the near branch
    for f80, dist, r21, face, v, d2 in ((0, 40.0, 0, 1, 5, 30.0), (0, 40.0, 0, 1, 5, 10.0), (0, 40.0, 0, 1, 4, 0.0),
                                       (0, 40.0, 1, 1, 5, 0.0), (0, 40.0, 0, 0, 5, 0.0), (1, 40.0, 0, 1, 5, 0.0)):
        if v not in pick2:
            continue
        add('cr 139240 s1 near %d %g %d %d %d %g' % (f80, dist, r21, face, v, d2), B, 0x139240, [n, e],
            base + [(n + 6, b8(1)), (e + 0x80, b8(f80))],
            queues={0x1B15D0: [fb(dist), fb(d2), fb(100.0)], 0x21BE40: [r21], 0x1B1560: [face, 1, 1],
                    0x122BB8: [pick2[v] << 15]}, run_set=ns)
    # state 1, the far branch
    for dist, c22 in ((120.0, 0xEF), (120.0, 0xF0), (120.0, 0xFFFF)):
        add('cr 139240 s1 far %g %X' % (dist, c22), B, 0x139240, [n, e],
            base + [(n + 6, b8(1)), (e + 0x80, b8(1)), (e + 0x22, h16(c22))],
            queues={0x1B15D0: [fb(dist), fb(100.0)]}, run_set=ns)
    for dist, fa, c32, face in ((70.0, 0x80, 0, 1), (70.0, 0x80, 0, 0), (70.0, 0x80, 1, 1), (70.0, 0, 0, 1),
                                (45.0, 0x80, 0, 1)):
        add('cr 139240 s1 mid %g %X %d %d' % (dist, fa, c32, face), B, 0x139240, [n, e],
            base + [(n + 6, b8(1)), (e + 0x80, b8(1)), (n + 0xA, b8(fa)), (e + 0x32, h16(c32))],
            queues={0x1B15D0: [fb(dist), fb(100.0)], 0x1B1560: [face, 1]}, run_set=ns)
    add('cr 139240 s2', B, 0x139240, [n, e], base + [(n + 6, b8(2))], queues={0x1B15D0: [fb(50.0), fb(100.0)]},
        run_set=ns)
    # the tail: speed, re-aim, wander, mirror, life count
    for f80, s44, s40 in ((1, 0.3, 0.8), (1, 0.5, 0.8), (0, 0.3, 0.8), (0, 0.9, 0.8)):
        add('cr 139240 tail speed %d %g %g' % (f80, s44, s40), B, 0x139240, [n, e],
            base + [(n + 6, b8(2)), (e + 0x80, b8(f80)), (e + 0x44, f32b(s44)), (e + 0x40, f32b(s40))],
            queues={0x1B15D0: [fb(50.0), fb(100.0)]}, run_set=ns)
    for c24, f80, d, face in ((3, 0, 100.0, 1), (0, 4, 100.0, 1), (0, 0, 100.0, 1), (0, 0, 10.0, 1), (0, 0, 10.0, 0)):
        add('cr 139240 tail aim %d %X %g %d' % (c24, f80, d, face), B, 0x139240, [n, e],
            base + [(n + 6, b8(2)), (e + 0x80, b8(f80)), (e + 0x24, h16(c24))],
            queues={0x1B15D0: [fb(50.0), fb(d)], 0x1B1560: [face], 0x122BB8: [0x1234567]}, run_set=ns)
    for f80, c20, w, hit, hgt, py in ((2, 0, -0.5, 0, 0.0, 0.0), (0, 4, -0.5, 0, 0.0, 0.0), (0, 0, -0.5, 1, 100.0, 0.0),
                                       (0, 4, -0.5, 1, 300.0, 0.0), (0, 4, -0.5, 1, 100.0, 500.0),
                                       (0, 4, 0.5, 0, 0.0, 0.0), (0, 4, 0.5, 0, 0.0, 500.0), (0, 4, 0.0, 0, 0.0, 0.0)):
        y = 231.0
        add('cr 139240 tail wander %d %d %g %d %g %g' % (f80, c20, w, hit, hgt, py), B, 0x139240, [n, e],
            base + [(n + 6, b8(2)), (e + 0x80, b8(f80)), (e + 0x20, h16(c20)), (e + 0x5C, f32b(w)),
                    (e + 0x24, h16(3)), (n + 0xB4, f32b(y)), (0x810364, f32b(py))],
            queues={0x1B15D0: [fb(50.0)], 0x1B2F70: [Scribble(hit, [(0x700038A0, f32b(hgt))])],
                    0x122BB8: [0x12345678, 0x7FFFFFFF]}, run_set=ns)
    for c2c in (0x12B, 0x12C, 0xFFFF):
        add('cr 139240 tail life %X' % c2c, B, 0x139240, [n, e],
            base + [(n + 6, b8(2)), (e + 0x2C, h16(c2c)), (e + 0x24, h16(3))],
            queues={0x1B15D0: [fb(50.0)], 0x122BB8: [0x4000, 0x7FFFFFFF, -0x80000000]}, run_set=ns)
    # 00139E00
    for st in (0, 1):
        for f81, c20, face in ((1, 5, 1), (0, 1, 1), (0, 5, 0), (0, 5, 1)):
            for s44, s50, r21, grab, fd, b70a in (((0.9, 0.5, 0, 1, 0x80, 1), (0.5, -0.5, 0, 1, 0, 0),
                                                   (0.95, 0.5, 0, 1, 0x80, 0), (0.5, 0.5, 0, 1, 0, 1),
                                                   (0.5, 0.5, 1, 0, 0, 0), (0.5, 0.5, 0, 0, 0, 0))
                                                  if (f81, c20, face) == (0, 5, 1) else ((0.5, 0.5, 0, 0, 0, 0),)):
                if st == 0 and (f81, c20, face) != (0, 5, 1):
                    continue
                add('cr 139E00 s%d %d %d %d %g %g %d %d %X %d' % (st, f81, c20, face, s44, s50, r21, grab, fd, b70a), B,
                    0x139E00, [n, e],
                    base + [(n + 6, b8(st)), (e + 0x81, b8(f81)), (e + 0x20, h16(c20)), (e + 0x44, f32b(s44)),
                            (e + 0x50, f32b(s50)), (n + 0xD, b8(fd)), (0x81070A, b8(b70a))],
                    queues={0x1B1560: [face], 0x21BE40: [r21], 0x1A7B80: [grab]}, run_set=ns)
    for f70 in (0x1000, 0xEFFF):
        add('cr 139E00 s2 %X' % f70, B, 0x139E00, [n, e], base + [(n + 6, b8(2)), (e + 0x70, w32(f70))])
    for f81, c20, s44 in ((1, 5, 0.5), (0, 1, 0.3), (0, 5, 0.4), (0, 5, 0.3)):
        add('cr 139E00 s3 %d %d %g' % (f81, c20, s44), B, 0x139E00, [n, e],
            base + [(n + 6, b8(3)), (e + 0x81, b8(f81)), (e + 0x20, h16(c20)), (e + 0x44, f32b(s44))],
            queues={0x122BB8: [0x7FFFFFFF]})
    add('cr 139E00 s4', B, 0x139E00, [n, e], base + [(n + 6, b8(4))])


def grab_cases(add, rng):
    """0013A3B0 (behaviour 4, the grab) on a19c_02's creature: every state
    of the switch, each test on both sides."""
    B = B2
    n, e = CR, CR + 0x1F0
    base = bones(n)
    ns = RUN - {0x1B1560}
    ppos = (1000.0, 200.0, 950.0)
    near = [(0x810360, fw(*ppos, 1.0))]
    for st in (8, 10, 0xFF):
        add('gr s%d' % st, B, 0x13A3B0, [n, e], base + [(n + 6, b8(st))])
    for st in (0, 1):
        for s44, s5c, f81, y, s50, pose in ((0.5, 0.0, 0, 0.0, 0.0, 7), (0.0, 0.0, 0, 0.0, 0.0, 7),
                                            (0.0, 0.0, 0, 0.0, 0.0, 0x8007), (0.0, 0.0, 0, 0.0, 0.0, 6),
                                            (0.0, 0.0, 0, 0.0, 0.5, 7), (-0.1, 0.5, 4, 0.0, 0.0, 7),
                                            (0.0, 0.5, 0, 210.0, 0.0, 7), (0.0, 0.5, 0, 230.0, 0.0, 7)):
            if st == 0 and (s44, s5c) != (0.5, 0.0):
                continue
            add('gr s%d %g %g %d %g %g %X' % (st, s44, s5c, f81, y, s50, pose), B, 0x13A3B0, [n, e],
                base + near + [(n + 6, b8(st)), (e + 0x44, f32b(s44)), (e + 0x5C, f32b(s5c)), (e + 0x81, b8(f81)),
                               (n + 0xB4, f32b(y)), (e + 0x50, f32b(s50)), (n + 0x2C, h16(pose))])
    # state 2: the countdown, the facing test, the approach and the box
    for f81, c20, face in ((1, 5, 1), (0, 1, 1), (0, 5, 0)):
        add('gr s2 leave %d %d %d' % (f81, c20, face), B, 0x13A3B0, [n, e],
            base + [(n + 6, b8(2)), (e + 0x81, b8(f81)), (e + 0x20, h16(c20))], queues={0x1B1560: [face]},
            run_set=ns)
    for s44, s50, dx, dy, r_ed in ((0.5, -0.5, 1.0, 0.0, 0), (0.9, 0.5, 1.0, 0.0, 0), (0.5, 0.5, 1.0, 0.0, 1),
                                   (0.5, 0.5, 50.0, 0.0, 0), (0.5, 0.5, 1.0, 20.0, 0)):
        # the creature placed so that its point 3.5 ahead lies near / far from the player
        add('gr s2 track %g %g %g %g %d' % (s44, s50, dx, dy, r_ed), B, 0x13A3B0, [n, e],
            base + near + [(n + 6, b8(2)), (e + 0x81, b8(0)), (e + 0x20, h16(5)), (e + 0x44, f32b(s44)),
                           (e + 0x50, f32b(s50)), (n + 0xB0, fw(ppos[0] + dx, ppos[1] + dy, ppos[2] - 3.5, 1.0)),
                           (n + 0xC4, f32b(0.0))],
            queues={0x1B1560: [1], 0x21BED0: [r_ed]}, run_set=ns)
    # states 3 and 4
    for bd, y, f70 in ((1, 0.0, 0), (0, 300.0, 0), (0, 100.0, 0x1000), (0, 300.0, 0x1000)):
        add('gr s3 %d %g %X' % (bd, y, f70), B, 0x13A3B0, [n, e],
            base + near + [(n + 6, b8(3)), (0x8106BD, b8(bd)), (n + 0xB4, f32b(y)), (e + 0x70, w32(f70))])
    add('gr s4 cancel', B, 0x13A3B0, [n, e], base + [(n + 6, b8(4)), (0x8106BD, b8(1))])
    for c20, c22 in ((0, 0), (5, 3), (5, 0)):
        for fd, b70a, hp in (((0x80, 1, 10.0), (0x80, 0, 12.0), (0x80, 0, 13.0), (0, 1, 30.0), (0, 0, 10.0),
                              (0, 0, 11.0)) if (c20, c22) == (5, 0) else ((0, 0, 50.0),)):
            add('gr s4 %d %d %X %d %g' % (c20, c22, fd, b70a, hp), B, 0x13A3B0, [n, e],
                base + near + [(n + 6, b8(4)), (0x8106BD, b8(0)), (e + 0x20, h16(c20)), (e + 0x22, h16(c22)),
                               (n + 0xD, b8(fd)), (0x81070A, b8(b70a)), (0x8104D0, f32b(hp)),
                               (0x810374, f32b(0.7))])
    # state 5
    for f70, c20, f3c in ((0x1000, 1, 10.0), (0, 0, 20.0), (0, 0, 17.0)):
        add('gr s5 %X %d %g' % (f70, c20, f3c), B, 0x13A3B0, [n, e],
            base + [(n + 6, b8(5)), (e + 0x70, w32(f70)), (e + 0x20, h16(c20)), (n + 0x3C, f32b(f3c))])
    for fd, b70a, bd in ((0x80, 1, 1), (0x80, 0, 0), (0, 1, 0), (0, 0, 1)):
        add('gr s5 throw %X %d %d' % (fd, b70a, bd), B, 0x13A3B0, [n, e],
            base + [(n + 6, b8(5)), (e + 0x70, w32(0)), (e + 0x20, h16(0)), (n + 0x3C, f32b(5.0)), (n + 0xD, b8(fd)),
                    (0x81070A, b8(b70a)), (0x8106BD, b8(bd))])
    add('gr s6', B, 0x13A3B0, [n, e], base + [(n + 6, b8(6))])
    for f70, s44 in ((0x1000, -0.5), (0, 0.6), (0, -0.2), (0, 0.2)):
        add('gr s7 %X %g' % (f70, s44), B, 0x13A3B0, [n, e],
            base + [(n + 6, b8(7)), (e + 0x70, w32(f70)), (e + 0x44, f32b(s44))],
            queues={0x122BB8: [0x3FFFFFFF]})
    for f70, s5c, f81, y, s44, s40 in ((0x1000, 0.0, 0, 0.0, -0.3, -0.005), (0, 0.5, 4, 0.0, 0.3, -0.005),
                                       (0, 0.5, 0, 210.0, 0.3, -0.005), (0, 0.5, 0, 230.0, 0.0, -0.005),
                                       (0x1000, 0.5, 0, 230.0, 0.6, -0.005), (0x1000, 0.0, 0, 0.0, 0.1, -0.5)):
        add('gr s9 %X %g %d %g %g %g' % (f70, s5c, f81, y, s44, s40), B, 0x13A3B0, [n, e],
            base + near + [(n + 6, b8(9)), (e + 0x70, w32(f70)), (e + 0x5C, f32b(s5c)), (e + 0x81, b8(f81)),
                           (n + 0xB4, f32b(y)), (e + 0x44, f32b(s44)), (e + 0x40, f32b(s40)),
                           (e + 0x60, f32b(0.3))],
            queues={0x122BB8: [0x2AAAAAAA]})


def tendril_cases(add, rng):
    """The tendril pieces (00154F00) and the ellipse test (001545B0) on
    a19c_02's first 0x1546C0 actor, the pieces designed."""
    B = B2
    n = TEN
    blk = n + 0x1F0
    ram, _ = CAPTURES[B]
    tgt = FREE + 0x3000
    for tgt_word, t80 in ((0, 0.0), (tgt, 0.25), (tgt, 0.75), (tgt, 0.5)):
        pieces = []
        for i in range(12):
            on = i % 4 != 3
            pos = (90, 99, 100, 110, 127, 128, 200, -5, 115, 105, 30, 99)[i]
            vel = (3, -2, 9, 20, 7, 8, -12, 50, 1, 0, 9, 2)[i]
            p80 = (4, 16, 15, 0, 40, 3, 8, 1, 17, 2, 9, 12)[i]
            p82 = (100, -50, 3, 256, 0, 7, 1000, -1, 5, 9, 11, 13)[i]
            pieces.append((blk + 0x7C + 10 * i, h16(pos) + h16(vel) + h16(p80) + h16(p82) + h16(1 if on else 0)))
            pieces.append((blk + 0x1C + 8 * i, fw(900.0 + i, 300.0 + i)))
        add('tn 154F00 %X %g' % (tgt_word, t80), B, 0x154F00, [n],
            pieces + [(n + 0x20, w32(tgt_word)), (tgt + 0x80, f32b(t80)), (blk + 0xC, f32b(2.5))],
            queues={0x122BB8: [0x7FFF0000, -0x10000, 0x12345678, 0x40000000, 0x7FFFFFFF, -1] * 3})
    px, pz = struct.unpack_from('<ff', ram, n + 0xB0)[0], struct.unpack_from('<ff', ram, n + 0xB8)[0]
    for k, (dx, dz) in enumerate(((0.5, 0.5), (3.0, 0.0), (0.0, 3.0), (30.0, 30.0), (0.0, 0.0), (-2.0, 1.0))):
        for d in (0, 3):
            add('tn 1545B0 %d d%d' % (k, d), B, 0x1545B0, [n, fb(px + dx), fb(pz + dz)], [(n + 0xD, b8(d))])


def boot_cases(add, rng):
    """The other boot entries on designed inputs."""
    B = B2
    n = CR
    # 00194240: the four points, inside, beyond 15 and beyond 4 in y
    pts = ((1017.9, 223.0, 1030.9), (997.2, 198.0, 929.8), (896.5, 197.0, 930.2), (760.7, 268.0, 883.1))
    for k, (x, y, z) in enumerate(pts):
        for dx, dy in ((1.0, 1.0), (16.0, 0.0), (0.0, 5.0), (0.0, -3.9)):
            add('bt 194240 %d %g %g' % (k, dx, dy), B, 0x194240, [PLAYER], [(PLAYER + 0xA0, fw(x + dx, y + dy, z))])
    add('bt 194240 none', B, 0x194240, [PLAYER], [(PLAYER + 0xA0, fw(0.0, 0.0, 0.0))])
    # 00197390
    for s230 in (9, 8, 0x2C, 0, 7):
        for area in (1, 0):
            add('bt 197390 %X a%d' % (s230, area), B, 0x197390, [CAM, PLAYER],
                [(PLAYER + 0x230, w32(s230)), (0x810702, b8(area))])
    # 001B2B80 / 001B3250: the probe results and the frame word
    p = FREE + 0x100
    obj = FREE + 0x500
    for r19, r2d, r2f in ((0, 0, 0), (0, 0, 1), (1, 0, 0), (1, 1, 0), (1, 5, 0)):
        add('bt 1B2B80 %d %d %d' % (r19, r2d, r2f), B, 0x1B2B80, [n, p, FREE + 0x200],
            queues={0x19AD00: [r19], 0x1B2D00: [r2d], 0x1B2F70: [r2f]})
    for r19, r2d, r2f, flag, h in ((0, 0, 0, 0, 0.0), (0, 0, 1, 0, 0.0), (0, 0, 1, 0, 1000.0), (1, 1, 0, 0x2000, 0.0),
                                   (1, 1, 0, 0xDFFF, 1000.0), (1, 1, 0, 0xDFFF, 0.0), (1, 0, 0, 0, 0.0)):
        add('bt 1B3250 %d %d %d %X %g' % (r19, r2d, r2f, flag, h), B, 0x1B3250, [n, p, fb(15.0)],
            [(0x700031D0, w32(obj)), (obj + 0x1A, h16(flag))],
            queues={0x19AD00: [r19], 0x1B2D00: [Scribble(r2d, [(STACK_TOP - 0x10, f32b(h))])],
                    0x1B2F70: [Scribble(r2f, [(STACK_TOP - 0x10, f32b(h))])]})
    # 001B37D0: the first probe clear, a later one, none
    for first, later in ((0, 0), (1, 3), (1, 10), (1, 99)):
        seq19, seq2f = [], []
        for k in range(15):
            ok = (k == 0 and first == 0) or (k == later)
            seq19.append(0)
            seq2f.append(Scribble(1, [(STACK_TOP - 0x70, f32b(1000.0 if ok else -1000.0))]))
        add('bt 1B37D0 %d %d' % (first, later), B, 0x1B37D0, [n, fb(10.0), fb(15.0)],
            queues={0x19AD00: seq19, 0x1B2F70: seq2f})
    # 001B34F0: inside, beyond the radius, beyond the height
    for dx, dy in ((1.0, 1.0), (5.0, 0.0), (0.0, 10.0), (3.0, -8.9)):
        add('bt 1B34F0 %g %g' % (dx, dy), B, 0x1B34F0, [FREE + 0x400, FREE + 0x300, FREE + 0x480],
            [(FREE + 0x400, fw(10.0, 20.0, 30.0)), (FREE + 0x300, fw(4.0, 9.0)), (FREE + 0x480, fw(10.0 + dx, 20.0 + dy,
                                                                                                   30.0 + dx))])
    # the draw method and its DMA tags (a designed object and model)
    o = FREE + 0x600
    model = FREE + 0x700
    for r in (0, 1):
        add('bt 1CB1F0 %d' % r, B, 0x1CB1F0, [o], [(o + 0x44, w32(model)), (model + 4, w32(0x12345))],
            queues={0x1D2910: [r]})
        add('bt 1CB140 %d' % r, B, 0x1CB140, [o, model], [(model + 4, w32(7))], queues={0x1D2910: [r]})
        add('bt 1D42E0 %d' % r, B, 0x1D42E0, [1, model], [(model + 4, w32(0x10007))], queues={0x1D2910: [r]})
        add('bt 1D4430 %d' % r, B, 0x1D4430, [model, o], [(model + 4, w32(3))], queues={0x1D2910: [r]})
    for w in (0xFFFFFFFF, 0, 0xFFFFFFFE):
        add('bt 1F6AC0 %X' % w, B, 0x1F6AC0, [FREE + 0x800], [(FREE + 0x824, w32(w))])


def bound_cases(add):
    """The float compares listed in docs/LEVEL13_PORT.md section 3.2, each
    with the compared value exactly at the bound and one float either side
    (not every float compare of the module: see that section)."""
    B = B7
    # 0x823780: the phase wrap at 2 (phase + k/4, k = 0)
    n = SUB1[0x823780]
    for v in around(2.0):
        add('bound 823780 wrap %08X' % v, B, 0x823780, [n],
            [(n + 4, b8(1)), (n + 5, b8(1)), (0x810702, b8(1)), (n + 0x1F0, fw(0.0, 0.5)),
             (n + 0x1F8, bits32(v) + f32b(0.25))], full=v != fbits(2.0))
    # 0x823D10: the phase wraps at 2, the size floor 0, the end past 5
    n = SUB1[0x823D10]
    blk = n + 0x1F0
    for v in around(2.0 - 0.015):
        add('bound 823D10 wrap %08X' % v, B, 0x823D10, [n],
            [(n + 4, b8(1)), (0x810702, b8(1)), (blk + 0x24, f32b(0.0)), (blk + 0x2C, bits32(v))])
    for v in around(0.004166667):
        add('bound 823D10 size %08X' % v, B, 0x823D10, [n],
            [(n + 4, b8(2)), (0x810702, b8(1)), (blk + 0x18, w32(70)), (blk + 0x24, bits32(v)), (blk + 0x28, f32b(1.0))])
    for v in around(4.99):
        add('bound 823D10 end %08X' % v, B, 0x823D10, [n],
            [(n + 4, b8(2)), (0x810702, b8(1)), (blk + 0x18, w32(70)), (blk + 0x24, f32b(0.0)), (blk + 0x28, bits32(v))])
    # 0x824A90: the end past 1.5
    s = NODE_A
    for v in around(1.48) + (0x3FBD70A4, 0x3FBD70A3):
        add('bound 824A90 %08X' % v, B2, 0x824A90, [s], [(s + 4, b8(1)), (s + 0x1F0, bits32(v))],
            queues={0x1CCF70: [0x55]})
    # 0x826C10: the player box and the rest height
    n = SUB1[0x826C10]
    for k, (lo, hi) in enumerate(((850.0, 859.6), (850.5, 855.0))):
        for v in around(lo) + around(hi):
            x, z = (ff(v), 853.0) if k == 0 else (855.0, ff(v))
            add('bound 826C10 box %d %08X' % (k, v), B, 0x826C10, [n],
                [(n + 4, b8(4)), (n + 5, b8(0)), (n + 0x2EC, w32(0)), (0x810350, f32b(x)), (0x810358, f32b(z))])
    for v in around(370.0):
        add('bound 826C10 rest %08X' % v, B, 0x826C10, [n],
            [(n + 4, b8(1)), (n + 0x28, h16(0x46)), (n + 0xB4, bits32(v)), (n + 0x2E8, f32b(370.0)),
             (n + 0x2E4, f32b(0.4))])
    # 0x827430: the equality of the heights
    n = SUB1[0x827430]
    for v in around(370.0):
        add('bound 827430 %08X' % v, B, 0x827430, [n],
            [(n + 4, b8(1)), (n + 0x2E4, bits32(v)), (0x7ADD60 + 0xB4, f32b(370.0))])
    # 0x827550: 445 <= y <= 460
    for v in around(445.0) + around(460.0):
        add('bound 827550 %08X' % v, B, 0x827550, [NODE_A],
            [(NODE_A + 4, b8(1)), (NODE_A + 5, b8(0)), (NODE_A + 0x4C, w32(0x8277C0)), (0x810354, bits32(v))])
    # 00139240: 50 / 100 / 20 / 15, the speed 0.4, the heights
    B = B2
    n, e = CR, CR + 0x1F0
    ns = RUN - {0x1B15D0, 0x1B1560}
    for v in around(50.0):
        add('bound 139240 s0 %08X' % v, B, 0x139240, [n, e], bones(n) + [(n + 6, b8(0))],
            queues={0x1B15D0: [v, fb(100.0)], 0x122BB8: [4 << 11]}, run_set=ns)
        add('bound 139240 s1 %08X' % v, B, 0x139240, [n, e], bones(n) + [(n + 6, b8(1)), (e + 0x80, b8(0))],
            queues={0x1B15D0: [v, fb(100.0), fb(100.0)], 0x1B1560: [1, 1, 1], 0x122BB8: [0]}, run_set=ns)
    for v in around(100.0):
        add('bound 139240 far %08X' % v, B, 0x139240, [n, e], bones(n) + [(n + 6, b8(1)), (e + 0x80, b8(1))],
            queues={0x1B15D0: [v, fb(100.0)]}, run_set=ns)
    for v in around(20.0):
        add('bound 139240 d20 %08X' % v, B, 0x139240, [n, e], bones(n) + [(n + 6, b8(1)), (e + 0x80, b8(0))],
            queues={0x1B15D0: [fb(10.0), v, fb(100.0)], 0x1B1560: [1, 1], 0x122BB8: [10 << 15]}, run_set=ns)
    for v in around(15.0):
        add('bound 139240 d15 %08X' % v, B, 0x139240, [n, e], bones(n) + [(n + 6, b8(2)), (e + 0x24, h16(0)),
                                                                          (e + 0x80, b8(0))],
            queues={0x1B15D0: [fb(10.0), v], 0x1B1560: [0]}, run_set=ns)
    for v in around(0.4):
        add('bound 139240 speed %08X' % v, B, 0x139240, [n, e], bones(n) + [(n + 6, b8(2)), (e + 0x80, b8(1)),
                                                                            (e + 0x44, bits32(v))],
            queues={0x1B15D0: [fb(10.0), fb(100.0)]}, run_set=ns)
    for v in around(221.0):
        add('bound 139240 hgt %08X' % v, B, 0x139240, [n, e],
            bones(n) + [(n + 6, b8(2)), (e + 0x20, h16(4)), (e + 0x5C, f32b(-0.5)), (e + 0x24, h16(3)),
                        (n + 0xB4, f32b(231.0)), (0x810364, f32b(0.0))],
            queues={0x1B15D0: [fb(50.0)], 0x1B2F70: [Scribble(1, [(0x700038A0, bits32(v))])]}, run_set=ns)
    for v in around(216.0):
        add('bound 139240 hgt2 %08X' % v, B, 0x139240, [n, e],
            bones(n) + [(n + 6, b8(2)), (e + 0x20, h16(4)), (e + 0x5C, f32b(0.5)), (e + 0x24, h16(3)),
                        (n + 0xB4, f32b(231.0)), (0x810364, bits32(v))],
            queues={0x1B15D0: [fb(50.0)]}, run_set=ns)
    # 00139E00: 0.8 / 1.0 / 0.4
    for v in around(0.8) + around(1.0):
        add('bound 139E00 s1 %08X' % v, B, 0x139E00, [n, e], bones(n) + [(n + 6, b8(1)), (e + 0x81, b8(0)),
                                                                         (e + 0x20, h16(5)), (e + 0x44, bits32(v))],
            queues={0x1B1560: [1], 0x21BE40: [1]}, run_set=ns)
    for v in around(0.4):
        add('bound 139E00 s3 %08X' % v, B, 0x139E00, [n, e], bones(n) + [(n + 6, b8(3)), (e + 0x81, b8(0)),
                                                                         (e + 0x20, h16(5)), (e + 0x44, bits32(v))])
    # 0013A3B0: 0.8, 17, 0.4, the heights
    for v in around(0.8):
        add('bound 13A3B0 s2 %08X' % v, B, 0x13A3B0, [n, e],
            bones(n) + [(n + 6, b8(2)), (e + 0x81, b8(0)), (e + 0x20, h16(5)), (e + 0x44, bits32(v))],
            queues={0x1B1560: [1]}, run_set=RUN - {0x1B1560})
    for v in around(17.0):
        add('bound 13A3B0 s5 %08X' % v, B, 0x13A3B0, [n, e],
            bones(n) + [(n + 6, b8(5)), (e + 0x70, w32(0)), (e + 0x20, h16(0)), (n + 0x3C, bits32(v))])
    for v in around(0.4):
        add('bound 13A3B0 s7 %08X' % v, B, 0x13A3B0, [n, e], bones(n) + [(n + 6, b8(7)), (e + 0x70, w32(0)),
                                                                         (e + 0x44, bits32(v))])
    for v in around(208.0):
        add('bound 13A3B0 s3 %08X' % v, B, 0x13A3B0, [n, e],
            bones(n) + [(n + 6, b8(3)), (0x8106BD, b8(0)), (0x810364, f32b(200.0)), (n + 0xB4, bits32(v))])
    for v in around(215.0):
        add('bound 13A3B0 s9 %08X' % v, B, 0x13A3B0, [n, e],
            bones(n) + [(n + 6, b8(9)), (e + 0x5C, f32b(0.5)), (e + 0x81, b8(0)), (0x810364, f32b(200.0)),
                        (n + 0xB4, bits32(v))])
    # 00194240: 225 and 4
    for v in around(15.0):
        add('bound 194240 r %08X' % v, B, 0x194240, [PLAYER], [(PLAYER + 0xA0, fw(1017.9 + ff(v), 223.0, 1030.9))])
    for v in around(4.0):
        add('bound 194240 y %08X' % v, B, 0x194240, [PLAYER],
            [(PLAYER + 0xA0, fw(1017.9, 223.0, 1030.9)), (PLAYER + 0xA4, bits32(fbits(223.0 + ff(v))))])
    # 001B3250: B4 - lim against the frame word
    p = FREE + 0x100
    ram, _ = CAPTURES[B]
    y = struct.unpack_from('<f', ram, n + 0xB4)[0]
    for v in around(y - 15.0):
        add('bound 1B3250 %08X' % v, B, 0x1B3250, [n, p, fb(15.0)],
            queues={0x19AD00: [0], 0x1B2F70: [Scribble(1, [(STACK_TOP - 0x10, bits32(v))])]})
    # 001B34F0: the radius and the height
    for v in around(4.0):
        add('bound 1B34F0 r %08X' % v, B, 0x1B34F0, [FREE + 0x400, FREE + 0x300, FREE + 0x480],
            [(FREE + 0x400, fw(10.0, 20.0, 30.0)), (FREE + 0x300, bits32(v) + f32b(9.0)), (FREE + 0x480, fw(14.0, 20.0,
                                                                                                         30.0))])
    for v in around(9.0):
        add('bound 1B34F0 y %08X' % v, B, 0x1B34F0, [FREE + 0x400, FREE + 0x300, FREE + 0x480],
            [(FREE + 0x400, fw(10.0, 20.0, 30.0)), (FREE + 0x300, f32b(4.0) + bits32(v)), (FREE + 0x480, fw(10.0, 29.0,
                                                                                                         30.0))])
    # 00154F00: the target's +0x80 against 0.5
    n = TEN
    for v in around(0.5):
        add('bound 154F00 %08X' % v, B, 0x154F00, [n], [(n + 0x20, w32(FREE + 0x3000)), (FREE + 0x3080, bits32(v))])


def survivor_cases(add):
    """Cases added because the mutation sweep (docs/LEVEL13_PORT.md section
    3.4) showed the case set missed them; they always run (the default run
    included)."""
    B = B2
    n, e = CR, CR + 0x1F0
    # 00139E00: the pitch target's constants 7 and 5 added to a zero height
    # and z, where one unit in their last place shows (at the captured
    # heights the sums round it away)
    add('survivor 139E00 pitch 0', B, 0x139E00, [n, e],
        bones(n) + [(n + 6, b8(1)), (e + 0x81, b8(0)), (e + 0x20, h16(5)), (n + 0xB8, f32b(0.0)),
                    (0x810364, f32b(0.0))],
        queues={0x1B1560: [1], 0x21BE40: [1]}, run_set=RUN - {0x1B1560})
    # 00154F00: the target's +0x80 one unit above 0.5 with live pieces (the
    # bound cases had idle pieces)
    t = TEN
    blk = t + 0x1F0
    for v in (0x3F000001, 0x3F000004, 0x3F000005):
        pieces = [(blk + 0x7C + 10 * i, h16(110) + h16(20) + h16(4) + h16(9) + h16(1)) for i in range(3)]
        add('survivor 154F00 fast %08X' % v, B, 0x154F00, [t],
            pieces + [(t + 0x20, w32(FREE + 0x3000)), (FREE + 0x3080, bits32(v)), (blk + 0xC, f32b(2.5))],
            queues={0x122BB8: [0x12345678] * 6})
    # 0x827B60: the probe's lower point y - 4 at a zero height, where one
    # unit in the last place of the 4 shows
    o = OBJS[0]
    add('survivor 827B60 low point', B6, 0x827B60, [o],
        [(o + 4, b8(1)), (o + 5, b8(1)), (o + 0x28, h16(1)), (o + 0xB0, fw(1.0, 0.0, 2.0, 1.0))],
        queues={0x19A570: [0]})
    # the low nibble of D_008107F9 with bit 2 alone (4): every nibble test
    # of the lift and its valve (the other cases' nibbles were 0, 1, 3 and F)
    n = SUB1[0x826C10]
    add('survivor lift s0 nibble 4', B7, 0x826C10, [n], [(n + 4, b8(0)), (0x810779, b8(0)), (0x8107F9, b8(0x24))],
        queues={0x1B0FD0: [0]})
    add('survivor lift countdown nibble 4', B7, 0x826C10, [n],
        [(n + 4, b8(4)), (n + 5, b8(0)), (n + 0x2EC, w32(1)), (0x810779, b8(0)), (0x8107F9, b8(0x24))])
    add('survivor lift toggle nibble 4', B7, 0x826C10, [n],
        [(n + 4, b8(4)), (n + 5, b8(1)), (n + 0x28, h16(0xB3)), (n + 0x2E8, f32b(370.0)), (0x8107F9, b8(0x24))])
    v = SUB1[0x826840]
    add('survivor 826840 nibble 4', B7, 0x826840, [v],
        [(v + 4, b8(1)), (v + 5, b8(0)), (v + 0xB, b8(4)), (0x810779, b8(0)), (0x8107F9, b8(0x24))],
        queues={0x1B17A0: [1]})
    # Review round (docs/LEVEL13_PORT.md section 3.4): four compares whose
    # bound no earlier case reached.
    # 0013A3B0 state 2: the +0x4C pick tests +0x50 < 0; +0x50 exactly 0 (the
    # tracking cases used +-0.5)
    n, e = CR, CR + 0x1F0
    ns = RUN - {0x1B1560}
    ppos = (1000.0, 200.0, 950.0)
    add('survivor 13A3B0 s2 +50 zero', B, 0x13A3B0, [n, e],
        bones(n) + [(0x810360, fw(*ppos, 1.0)), (n + 6, b8(2)), (e + 0x81, b8(0)), (e + 0x20, h16(5)),
                    (e + 0x44, f32b(0.5)), (e + 0x50, f32b(0.0)),
                    (n + 0xB0, fw(ppos[0] + 1.0, ppos[1], ppos[2] - 3.5, 1.0)), (n + 0xC4, f32b(0.0))],
        queues={0x1B1560: [1], 0x21BED0: [0]}, run_set=ns)
    # 0013A3B0 state 7: the push tests +0x44 < 0; +0x44 exactly 0
    add('survivor 13A3B0 s7 +44 zero', B, 0x13A3B0, [n, e],
        bones(n) + [(n + 6, b8(7)), (e + 0x70, w32(0)), (e + 0x44, f32b(0.0))], queues={0x122BB8: [0x3FFFFFFF]})
    # 00194240: pad 3 with dz exactly at the radius (the earlier bound case
    # was pad 0 along x); the sum is exact in single precision
    x = ff(0x443E2CCD)
    z = ff(0x445CC666) + 15.0
    assert struct.unpack('<f', struct.pack('<f', z))[0] == z
    add('survivor 194240 pad 3 z edge', B, 0x194240, [PLAYER], [(PLAYER + 0xA0, fw(x, 268.0, z))])
    # 00197390: the row byte D_00810702 is an unsigned load; 0x80 tells it
    # from a signed one (every other case used a row below 0x80)
    add('survivor 197390 row 0x80', B, 0x197390, [CAM, PLAYER], [(PLAYER + 0x230, w32(0)), (0x810702, b8(0x80))])


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
    # 0x823780: the delay slot of the column switch's exit for a k & 1
    # other than 0 and 1 (k & 1 is 0 or 1)
    0x8239C8: 'k & 1 is 0 or 1',
    # 0x823780: the store of 1.0 to blk +4 and the next word, reached only
    # when blk +4 (stored as 1.0 two words before, no call between) is
    # above 1.0
    0x823C2C: 'blk +4 was just stored as 1.0',
    0x823C30: 'blk +4 was just stored as 1.0',
    # 0x823D10: the delay slots of the two column switches' exits for an
    # index outside 0..2 (the loops run 0..2)
    0x823EC4: 'the flame index is 0..2',
    0x824104: 'the smoke index is 0..2',
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
            if word >> 26 in (2, 3):
                target = (pc & 0xF0000000) | ((word & 0x3FFFFFF) << 2)
                if word >> 26 == 2 and entry <= target < entry + size:
                    continue
                if target not in BY_ADDRESS and target not in FUNCS:
                    problems.append('%06X calls %06X (neither a hook nor an entry)' % (entry, target))
    return sorted(set(problems))


def fault_checks():
    """The native fail-stop contract: a NULL hook, a failing hook, an
    unmapped address after the calls and before any call, a latched fault;
    and for every entry a fault latched on entry, a NULL hook table, a NULL
    fault pointer and (entries with a result) a NULL result pointer.
    0x826570 on [38] in a19c_07 with +4 = 1 and +5 = 2 calls 001B17A0, the
    +0x4C method and 001F4BF0."""
    ram, spad = CAPTURES[B7]
    node = SUB1[0x826570]
    ram = bytearray(ram)
    spad = bytearray(spad)
    ram[node + 4] = 1
    ram[node + 5] = 2
    problems = []
    calls_want = ['w_001B17A0', 'w_callback', 'w_001F4BF0']

    def attempt(null=None, failing=None, unmapped=None, latched=False, entry=0x826570, args=(node,)):
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
    status, address, code, calls, _ = attempt(null='w_001F4BF0')
    if (status, address, code) != (-1, 0x1F4BF0, 1) or calls != calls_want[:2]:
        problems.append(('NULL hook', status, hex(address), code, calls))
    status, address, code, calls, same = attempt(failing='w_001B17A0')
    if (status, address, code) != (-1, 0x1B17A0, 2) or calls != calls_want[:1] or not same:
        problems.append(('failing hook', status, hex(address), code, calls, same))
    status, address, code, calls, _ = attempt(unmapped=node + 0x4C)
    if (status, address, code) != (-1, node + 0x4C, 5) or calls != calls_want[:1]:
        problems.append(('unmapped address', status, hex(address), code, calls))
    status, address, code, calls, same = attempt(unmapped=FREE + 0x24, entry=0x1F6AC0, args=(FREE,))
    if (status, address, code) != (-1, FREE + 0x24, 5) or calls or not same:
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
}


def hook_sites(results):
    """The cases hook_contract_site runs on: passing cases (the original
    does not stop) that together call every hook, the +0x4C callback and
    every entry, picked greedily (most targets not yet covered per native
    run the contract makes there, i.e. per call plus memory access of the
    case, then the earliest case). Returns ([(case index, targets)], targets no case
    reaches); a target is a hook name, ('bytes', entry) or ('pair', entry,
    hook): every hook an entry calls on any passing case fails at least once
    inside that entry (hook_contract_site fails the first call of every
    callee of a site), so each entry's own return after a failed call is
    contracted, not only one entry's per hook."""
    pairs = {('pair', entry, name) for (_, entry, errors, _, _, calls) in results
             if not errors and calls is not None for name in calls}
    want = (set(HOOK_NAMES) - set(CONTRACT_UNREACHABLE)) | {('bytes', entry) for entry in FUNCS} | pairs
    options = [(index, (set(calls) | {('bytes', entry)} | {('pair', entry, name) for name in calls}) & want,
                1 + counts[3])
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
    """The fail-stop contract of the header (em_level13_port.h), on one
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
    where each wrapper's own `l13_failed` test is observable. Returns
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
    call_ks = sorted(set(call_ks) | {names.index(name) for name in set(names)})   # each callee fails once
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
        if isinstance(target, tuple) and target[0] == 'pair':   # covered by call_ks
            continue
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
   'bound 13A3B0 s2 3F4CCCCE',
   'bound 823D10 end 409FAE15',
   'bt 194240 0 1 1',
   'bt 194240 1 1 1',
   'bt 194240 2 1 1',
   'bt 194240 3 0 5',
   'bt 194240 3 1 1',
   'bt 1B3250 1 1 0 2000 0',
   'bt 1B3250 1 1 0 DFFF 0',
   'bt 1B34F0 0 10',
   'bt 1B37D0 0 0',
   'bt 1B37D0 1 3',
   'cr 139240 s0 60 t1',
   'cr 139240 s0 60 t2',
   'cr 139240 s1 far 120 F0',
   'cr 139240 s1 mid 70 80 0 1',
   'cr 139240 s1 near 0 40 0 1 5 10',
   'cr 139240 s1 near 0 40 0 1 5 30',
   'cr 139240 tail aim 0 0 10 0',
   'cr 139240 tail speed 1 0.5 0.8',
   'cr 139240 tail wander 0 4 -0.5 1 100 500',
   'cr 139E00 s1 0 1 1 0.5 0.5 0 0 0 0',
   'cr 139E00 s1 0 5 1 0.5 -0.5 0 1 0 0',
   'cr 139E00 s1 0 5 1 0.5 0.5 0 1 0 1',
   'cr 139E00 s1 0 5 1 0.9 0.5 0 1 80 1',
   'cr 139E00 s1 0 5 1 0.95 0.5 0 1 80 0',
   'cr 139E00 s2 1000',
   'cr 139E00 s3 0 1 0.3',
   'cr 139E00 s3 0 5 0.3',
   'cr 139E00 s3 1 5 0.5',
   'cr 139E00 s4',
   'gr s1 0 0 0 0 0 7',
   'gr s2 leave 0 1 1',
   'gr s2 leave 1 5 1',
   'gr s2 track 0.5 -0.5 1 0 0',
   'gr s3 0 100 1000',
   'gr s3 0 300 0',
   'gr s3 1 0 0',
   'gr s4 0 0 0 0 50',
   'gr s4 5 0 0 0 11',
   'gr s4 5 0 0 1 30',
   'gr s4 5 0 80 0 13',
   'gr s4 5 0 80 1 10',
   'gr s4 5 3 0 0 50',
   'gr s4 cancel',
   'gr s5 0 0 17',
   'gr s5 1000 1 10',
   'gr s5 throw 0 1 0',
   'gr s5 throw 80 0 0',
   'gr s5 throw 80 1 1',
   'gr s6',
   'gr s7 0 -0.2',
   'gr s7 0 0.6',
   'gr s7 1000 -0.5',
   'gr s9 0 0.5 0 230 0 -0.005',
   'gr s9 0 0.5 4 0 0.3 -0.005',
   'gr s9 1000 0 0 0 0.1 -0.5',
   'gr s9 1000 0.5 0 230 0.6 -0.005',
   'lift s0 0 0 0',
   'lift s0 0 0 1',
   'lift s0 0 FF 0',
   'lift s1 count 45',
   'lift s1 done',
   'lift s1 drop 370 370 0',
   'lift s1 drop 380 370 0',
   'lift s2',
   'lift s3',
   'lift s4 +5 0 cd1 0 0',
   'lift s4 +5 0 cd1 0 21',
   'lift s4 +5 0 cd5 FF 0',
   'lift s4 +5 1 B2 30',
   'lift s4 +5 1 B3 30',
   'lift s4 +5 1 B3 3F',
   'lift s4 +5 2',
   'lift s4 box 855 853',
   'lift s5',
   'ov 823780 cols 2.5 0.1 0.3',
   'ov 823780 s0 0 FF 1 a8',
   'ov 823780 s0 FF 0 0 a8',
   'ov 823780 s0 FF FF 0 a8',
   'ov 823780 s1 a1 +5 0 1',
   'ov 823780 s1 a1 +5 2 0',
   'ov 823780 s1 a5 +5 0 0',
   'ov 823780 s2',
   'ov 823780 s3',
   'ov 823780 s5',
   'ov 823D10 s0 0 1 a1',
   'ov 823D10 s0 FF 0 a1',
   'ov 823D10 s1 f0 sz1 0 hF4241 cd0',
   'ov 823D10 s2 c70 0.5 4.995',
   'ov 823D10 s2 f1 sz0 0 h10 cd0',
   'ov 823D10 s2 f1 sz1 0 h10 cd3',
   'ov 823D10 s3',
   'ov 823D10 s4',
   'ov 824A90 s0 0.5',
   'ov 824A90 s1 1.47',
   'ov 824A90 s1 1.5',
   'ov 824A90 s2 0.5',
   'ov 824A90 s3 0.5',
   'ov 824A90 s4 0.5',
   'ov 826570 s0 0 0 0 0',
   'ov 826570 s0 0 0 1 0',
   'ov 826570 s1 +5 0 0 4 0',
   'ov 826570 s1 +5 1 77 1 FF 0',
   'ov 826570 s1 +5 1 78 0 0 0',
   'ov 826570 s1 +5 2 0',
   'ov 826570 s2 r1',
   'ov 826570 s3 r1',
   'ov 826570 s4 r1',
   'ov 826840 s0 0 1 0 0',
   'ov 826840 s0 0 1 0 FF',
   'ov 826840 s1 +5 0 0 4 3',
   'ov 826840 s1 +5 0 0 4 80',
   'ov 826840 s1 +5 1 77 1 FF 0',
   'ov 826840 s1 +5 2 0',
   'ov 826840 s1 +5 2 FF',
   'ov 826840 s2 r1',
   'ov 826840 s3 r1',
   'ov 826840 s4 r1',
   'ov 827430 s0 r0',
   'ov 827430 s1 380',
   'ov 827430 s3',
   'ov 827430 s5',
   'ov 827550 s0 0 0',
   'ov 827550 s0 0 1',
   'ov 827550 s0 1 1',
   'ov 827550 s1 +5 0 y452 r1',
   'ov 827550 s1 +5 0 y460 r0',
   'ov 827550 s1 +5 1 12B 0',
   'ov 827550 s1 +5 1 7FFF 1',
   'ov 827550 s1 +5 2',
   'ov 827550 s1 +5 3',
   'ov 827550 s2',
   'ov 827550 s3',
   'ov 827550 s4',
   'ov 8279E0 s0 r0',
   'ov 8279E0 s0 r1',
   'ov 8279E0 s1 +5 0 a1 0',
   'ov 8279E0 s1 +5 1 1',
   'ov 8279E0 s1 +5 2',
   'ov 8279E0 s2',
   'ov 8279E0 s3',
   'ov 8279E0 s4',
   'ov 827B60 s0 0 0',
   'ov 827B60 s0 FF 0',
   'ov 827B60 s1 +5 0 1',
   'ov 827B60 s1 +5 1 1 1',
   'ov 827B60 s1 +5 2',
   'ov 827B60 s2',
   'ov 827B60 s3',
   'ov 827B60 s4',
   'ov 829840 0',
   'ov 829840 1C02000',
   'ov 829A70 s0 0 0 0 0',
   'ov 829A70 s0 0 0 1C01000 0',
   'ov 829A70 s0 0 1 0 0',
   'ov 829A70 s1 +5 0 4 0 2',
   'ov 829A70 s1 +5 0 5 1C01000 2',
   'ov 829A70 s1 +5 1 1',
   'ov 829A70 s1 +5 2',
   'ov 829A70 s2',
   'ov 829A70 s3',
   'ov 829A70 s4',
   'tn 154F00 0 0',
   'tn 154F00 1C03000 0.75',
])
SURVIVORS = ('survivor ',)


def select_cases(elf):
    """The mode's case list (EM_TEST_FULL=1: all; the default run keeps
    every capture case, the designed cases of DEFAULT_KEEP and the mutation
    sweep's survivor cases)."""
    cases, captured, targeted = case_list(elf)
    total = len(cases)
    if not reference_mode.FULL and not os.environ.get('EM_LEVEL13_PORT_ALL'):
        kept = cases[:captured] + [c for c in cases[captured:]
                                   if c[0] in DEFAULT_KEEP or c[0].startswith(SURVIVORS)]
        # every entry keeps at least one case of its own (the hook
        # contract's `bytes` target): the first designed one
        have = {c[2] for c in kept}
        for c in cases[captured:]:
            if c[2] not in have:
                kept.append(c)
                have.add(c[2])
        return kept, captured, len(kept) - captured, total
    return cases, captured, targeted, total


def image_for(entry):
    """An image holding the entry's code (every a19c image holds AREA19;
    boot text is the same in all)."""
    return CAPTURES[B6][0]


def main():
    global NATIVE
    elf = read_elf()
    CAPTURES.update(load_captures(elf))
    NATIVE = build(os.environ.get('EM_LEVEL13_PORT_SOURCE'))
    selected, captured, targeted, total = select_cases(elf)
    CASES[:] = selected
    only = os.environ.get('EM_LEVEL13_PORT_ONLY')   # debugging: a label prefix
    if only:
        CASES[:] = selected = [c for c in selected if c[0].startswith(only)]
    results = run_cases(selected)
    failures, per_fn, seen, totals = [], {}, set(), [0, 0, 0, 0]
    header_problems, header_count = header_checks(os.environ.get('EM_LEVEL13_PORT_SOURCE'))
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
    if os.environ.get('EM_LEVEL13_PORT_KEEP'):   # lane tool: the coverage pass that picks DEFAULT_KEEP
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
    if os.environ.get('EM_LEVEL13_PORT_MISSING'):
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
        if os.environ.get('EM_LEVEL13_PORT_NOCONTRACT'):   # debugging only: skips the contract
            sites, missing = [], []
        outcome = reference_mode.parallel_map(hook_contract_site, sites)
        contract = [p for problems_, _ in outcome for p in problems_]
        if missing:
            contract.append(('no passing case reaches', missing))
        sweep = ('every call failing and every memory access refused' if reference_mode.FULL else
                 'an even sample of each case\'s calls failing (at most %d, plus the first call of each callee) and '
                 'memory accesses refused (at most %d)' % (CONTRACT_CALLS, CONTRACT_ACCESSES))
        print('  hook contract: %s' % (
            ('ok (%d native runs on %d cases: %s; each of the %d hooks and the +0x4C callback NULL, '
             'returning INT32_MIN, 1 and INT32_MAX; `bytes` NULL on all %d entries)'
             % (sum(n for _, n in outcome), len(sites), sweep, len(HOOKS), len(FUNCS))) if not contract
            else contract[:6]))
        if contract:
            failures.append(('hook contract', contract))
    for label, errors in failures[:int(os.environ.get("EM_LEVEL13_PORT_SHOW", "12"))]:
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



if __name__ == '__main__':
    main()
