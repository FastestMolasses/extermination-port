#!/usr/bin/env python3
"""Execute the ORIGINAL twelfth-level code (the a13d / a19b census rows:
AREA19's effect 0x824BE0, the group 0x82B5A0's behaviours, the area init
0x8250C0, [6]'s second stage 0x825420 and [7]'s second sequence 0x825930;
the boot functions of the AREA19 creature family run by 001386E0, the
0x141D20 actor's behaviour 5 and side probe, the player's ledge state, the
float arc cosine and the others) and compare em_level12_port*.c.

docs/LEVEL12_PORT.md. The census deltas of the twelfth-level route (decomp
build/s87/census/a13d_delta.json and a19b_delta.json, new_functions: 9 +
30 rows) name the functions; none had a verified port translation, so all
39 are translated in em_level12_port_*.c; they are the entries below
(FUNCS). Calls between the translations run as original code at the top
level of the oracle (only other functions are hooks), and every function
is also an entry of its own.

The harness is tools/test_level11_port_reference.py's (lane L11T, itself the
LEVEL10 / LEVEL9 / LEVEL8 / AREA06 / AREA22 / AREA04 design), copied and
owned here, with the twelfth-level captures, this module's hook table and
cases. Entries that take the original stack pointer (kind S) run the
oracle with sp = STACK_TOP and give the native entry STACK_TOP; the frame
locals live in the compared stack window.

Oracle: the shared EE interpreter with the measured float model (FallEE,
tools/test_player_fall_reference.py) runs the original code resident in the
recorded RAM images of the twelfth-level route (decomp build/s87/route_a13d/
and route_a19b/<beat>/). Before any case the test checks that the overlay
text of every image equals the user's extract/OVERLAY/AREA13.BIN (a13d) or
AREA19.BIN (a19b) (text size from the file header) and that the boot text
below 0x241000 equals the pinned ELF, so every executed instruction is
original. Nothing here embeds original bytes; reports hold counts only.

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

EM_LEVEL12_PORT_ONLY=<label prefix> runs a subset (no coverage / contract
checks). EM_LEVEL12_PORT_SOURCE=<dir> tests another copy of the module
sources (the mutation sweep). EM_LEVEL12_PORT_MISSING=1 lists unexecuted
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
OUT = ROOT / 'build/level12/port'
MASK = 0xFFFFFFFF
MASK64 = 0xFFFFFFFFFFFFFFFF
RAM_SIZE, SPAD_SIZE = 0x2000000, 0x4000
ARENA = 0x823500
STACK_LO = STACK_TOP - 0x800
STACK_SIZE = STACK_TOP - STACK_LO
BEAT_DIRS = {}
for _b in ('a13d_00_stair', 'a13d_01_ladder405', 'a13d_02_block890', 'a13d_03_jump', 'a13d_04_ladder426',
           'a13d_05_ladder432', 'a13d_06_door20', 'a13d_07_hatch63'):
    BEAT_DIRS[_b] = DECOMP / 'build/s87/route_a13d' / _b
for _b in ('a19b_00_arrival', 'a19b_01_ledge', 'a19b_02_ladder1023'):
    BEAT_DIRS[_b] = DECOMP / 'build/s87/route_a19b' / _b
BEATS = list(BEAT_DIRS)
# a13d images hold AREA13 (overlay id 10), a19b images AREA19 (id 16) at
# 0x823504 (TWELFTH_LEVEL_ROUTE.md section 1).
RESIDENT = {b: (10 if b.startswith('a13d') else 16) for b in BEATS}
A13_BEATS = [b for b in BEATS if b.startswith('a13d')]
A19_BEATS = [b for b in BEATS if b.startswith('a19b')]
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
    0x824BE0: ('em_level12_port_00824BE0', 0x310, [U, S], None),
    0x824EF0: ('em_level12_port_00824EF0', 0x74, [U], None),
    0x824F70: ('em_level12_port_00824F70', 0xC0, [U], None),
    0x825030: ('em_level12_port_00825030', 0x8C, [U], None),
    0x8250C0: ('em_level12_port_008250C0', 0x30, [], None),
    0x825420: ('em_level12_port_00825420', 0x1AC, [U, S], None),
    0x825930: ('em_level12_port_00825930', 0x174, [U], None),
    # boot: the AREA19 creature family
    0x138900: ('em_level12_port_00138900', 0x314, [U, U], None),
    0x138C20: ('em_level12_port_00138C20', 0x618, [U, U, S], None),
    0x13BA20: ('em_level12_port_0013BA20', 0x18C, [U, U], None),
    0x13BBB0: ('em_level12_port_0013BBB0', 0x2B0, [U, U, S], None),
    0x13BE60: ('em_level12_port_0013BE60', 0xC0, [U, U], None),
    0x13BF20: ('em_level12_port_0013BF20', 0x2CC, [U, U], None),
    0x13C1F0: ('em_level12_port_0013C1F0', 0x2D0, [U, U], I),
    0x13C4C0: ('em_level12_port_0013C4C0', 0x3F8, [U, U], I),
    0x13C8C0: ('em_level12_port_0013C8C0', 0x488, [U, U, S], None),
    0x13CD50: ('em_level12_port_0013CD50', 0x4C8, [U, U], I),
    0x13D220: ('em_level12_port_0013D220', 0xA4, [U], I),
    # boot: the others
    0x11BCF8: ('em_level12_port_0011BCF8', 0x430, [F], F),
    0x11E420: ('em_level12_port_0011E420', 0xFC, [F, S], F),
    0x1437E0: ('em_level12_port_001437E0', 0x304, [U, U], None),
    0x146740: ('em_level12_port_00146740', 0x26C, [U, U, S], I),
    0x16EF50: ('em_level12_port_0016EF50', 0x5D4, [U], None),
    0x179560: ('em_level12_port_00179560', 0x118, [U], None),
    0x17F9E0: ('em_level12_port_0017F9E0', 0x1A4, [U], None),
    0x1821E0: ('em_level12_port_001821E0', 0x68, [U], I),
    0x1831F0: ('em_level12_port_001831F0', 0x4C, [I], None),
    0x1A96F0: ('em_level12_port_001A96F0', 0xC0, [U, U], None),
    0x1B1270: ('em_level12_port_001B1270', 0x3C, [U, F, F], F),
    0x1B2F70: ('em_level12_port_001B2F70', 0x170, [U, U], I),
    0x1B3390: ('em_level12_port_001B3390', 0xAC, [U, U, U, F, S], I),
    0x1B39F0: ('em_level12_port_001B39F0', 0x234, [U, U, U], I),
    0x1C6910: ('em_level12_port_001C6910', 0x44, [U], None),
    0x1C9570: ('em_level12_port_001C9570', 0x98, [U, U, U, U], None),
    0x1CAFA0: ('em_level12_port_001CAFA0', 0xBC, [U, U], None),
    0x1CB060: ('em_level12_port_001CB060', 0x8, [U], None),
    0x1D3F60: ('em_level12_port_001D3F60', 0x164, [I, U], None),
    0x1D40D0: ('em_level12_port_001D40D0', 0xC, [U], None),
    0x1E8B40: ('em_level12_port_001E8B40', 0x48, [I], None),
}
OVERLAY_OF = {entry: 16 for entry in FUNCS if entry >= ARENA}
# The entries whose passing cases are all contract sites (they return from
# their own body): the default run and EM_TEST_FULL=1.
RETURN_SITES = tuple(e for e, (_, _, _, r) in FUNCS.items() if r is not None)
RETURN_SITES_FULL = RETURN_SITES


def has_result(entry):
    return FUNCS[entry][3] is not None


# Hooks, in the field order of EmLevel12PortHooks (after ctx and bytes; the
# +0x4C method's w_callback follows them).
# name -> (original address, [(ctype, register)], result kind or None)
HOOKS = [
# BEGIN GENERATED HOOKS
    ('w_001026A0', 0x1026A0, [(U, 4), (U, 5), (U, 6)], None),
    ('w_00102738', 0x102738, [(U, 4), (U, 5)], F),
    ('w_00102760', 0x102760, [(U, 4), (U, 5)], None),
    ('w_001028B8', 0x1028B8, [(U, 4), (U, 5), (U, 6)], None),
    ('w_001028D0', 0x1028D0, [(U, 4), (U, 5), (U, 6)], None),
    ('w_00102918', 0x102918, [(U, 4), (U, 5), (U, 6)], None),
    ('w_00102948', 0x102948, [(U, 4), (U, 5)], None),
    ('w_00102958', 0x102958, [(U, 4), (U, 5)], None),
    ('w_001029C0', 0x1029C0, [(U, 4)], None),
    ('w_00102B08', 0x102B08, [(U, 4), (U, 5), (F, 12)], None),
    ('w_00102BB0', 0x102BB0, [(U, 4), (U, 5), (F, 12)], None),
    ('w_00102C58', 0x102C58, [(U, 4), (U, 5), (U, 6)], None),
    ('w_00103230', 0x103230, [(U, 4), (U, 5), (F, 12)], None),
    ('w_0011CB90', 0x11CB90, [(F, 12)], F),
    ('w_0011DB90', 0x11DB90, [(U, 4)], I),
    ('w_0011DE90', 0x11DE90, [(F, 12)], F),
    ('w_0011DF78', 0x11DF78, [(F, 12)], F),
    ('w_0011E080', 0x11E080, [(F, 12)], I),
    ('w_0011E0A8', 0x11E0A8, [(U, 4), (F, 12)], F),
    ('w_0011E2A8', 0x11E2A8, [(F, 12)], F),
    ('w_0011E620', 0x11E620, [(F, 12), (F, 13)], F),
    ('w_0011E748', 0x11E748, [(F, 12)], F),
    ('w_0011FD78', 0x11FD78, [], U),
    ('w_00122BB8', 0x122BB8, [], I),
    ('w_00127758', 0x127758, [(Q, 4)], F),
    ('w_001281C0', 0x1281C0, [(F, 12)], I),
    ('w_00128250', 0x128250, [(F, 12)], I),
    ('w_00128350', 0x128350, [(F, 12)], Q),
    ('w_001749A0', 0x1749A0, [(U, 4), (I, 5), (I, 6), (F, 12)], None),
    ('w_00174A50', 0x174A50, [(U, 4), (F, 12)], None),
    ('w_00174AB0', 0x174AB0, [(U, 4)], None),
    ('w_001751A0', 0x1751A0, [(U, 4)], None),
    ('w_00175900', 0x175900, [(U, 4), (I, 5)], None),
    ('w_001764E0', 0x1764E0, [(U, 4)], None),
    ('w_001796C0', 0x1796C0, [(U, 4)], None),
    ('w_00188610', 0x188610, [(U, 4)], I),
    ('w_0019A310', 0x19A310, [(U, 4)], I),
    ('w_0019A6F0', 0x19A6F0, [(U, 4), (U, 5), (U, 6), (I, 7), (I, 8)], I),
    ('w_0019AB20', 0x19AB20, [(U, 4), (U, 5), (U, 6), (I, 7)], I),
    ('w_0019AD00', 0x19AD00, [(U, 4), (U, 5), (I, 6)], I),
    ('w_0019AFE0', 0x19AFE0, [(U, 4), (U, 5), (U, 6), (I, 7)], I),
    ('w_0019B2C0', 0x19B2C0, [(U, 4), (U, 5), (I, 6)], I),
    ('w_0019B6C0', 0x19B6C0, [(U, 4), (U, 5)], I),
    ('w_0019BC40', 0x19BC40, [(U, 4)], None),
    ('w_0019F680', 0x19F680, [(U, 4), (U, 5), (I, 6)], None),
    ('w_001AFC10', 0x1AFC10, [(U, 4)], None),
    ('w_001B1240', 0x1B1240, [(U, 4), (F, 12), (F, 13)], F),
    ('w_001B12B0', 0x1B12B0, [(F, 12), (F, 13), (F, 14)], F),
    ('w_001B1380', 0x1B1380, [(U, 4), (U, 5), (F, 12)], I),
    ('w_001B13F0', 0x1B13F0, [(U, 4), (U, 5), (F, 12)], I),
    ('w_001B1470', 0x1B1470, [(F, 12)], F),
    ('w_001B15D0', 0x1B15D0, [(U, 4), (U, 5)], F),
    ('w_001B17A0', 0x1B17A0, [(U, 4)], None),
    ('w_001B1EA0', 0x1B1EA0, [(I, 4), (U, 5), (U, 6), (I, 7)], I),
    ('w_001B2B10', 0x1B2B10, [(U, 4), (U, 5), (U, 6)], None),
    ('w_001B6F00', 0x1B6F00, [(U, 4), (U, 5), (F, 12)], None),
    ('w_001BA1A0', 0x1BA1A0, [(U, 4), (U, 5)], None),
    ('w_001BA1F0', 0x1BA1F0, [(U, 4)], I),
    ('w_001C6160', 0x1C6160, [(U, 4)], I),
    ('w_001C64F0', 0x1C64F0, [(U, 4), (F, 12)], None),
    ('w_001C67E0', 0x1C67E0, [(U, 4), (I, 5), (F, 12), (F, 13)], None),
    ('w_001C68C0', 0x1C68C0, [(U, 4)], None),
    ('w_001C6DA0', 0x1C6DA0, [(U, 4)], None),
    ('w_001C7420', 0x1C7420, [(U, 4), (I, 5), (I, 6)], None),
    ('w_001C94B0', 0x1C94B0, [(U, 4), (U, 5), (U, 6), (U, 7)], None),
    ('w_001C9940', 0x1C9940, [(U, 4), (I, 5), (U, 6)], None),
    ('w_001CA5E0', 0x1CA5E0, [(U, 4), (U, 5), (I, 6)], None),
    ('w_001CB760', 0x1CB760, [(U, 4), (U, 5), (U, 6), (I, 7)], None),
    ('w_001CCF70', 0x1CCF70, [(U, 4)], I),
    ('w_001CFA60', 0x1CFA60, [(U, 4), (U, 5), (F, 12), (F, 13)], None),
    ('w_001CFBE0', 0x1CFBE0, [(I, 4), (I, 5), (U, 6), (U, 7), (I, 8)], None),
    ('w_001D1F20', 0x1D1F20, [(I, 4)], None),
    ('w_001D1F80', 0x1D1F80, [(I, 4), (I, 5), (I, 6)], None),
    ('w_001D1FF0', 0x1D1FF0, [(I, 4), (I, 5)], None),
    ('w_001D2090', 0x1D2090, [(I, 4), (U, 5)], None),
    ('w_001D2910', 0x1D2910, [(I, 4)], I),
    ('w_001D6B10', 0x1D6B10, [(I, 4), (I, 5), (I, 6), (I, 7)], None),
    ('w_001D6BA0', 0x1D6BA0, [(I, 4), (I, 5), (I, 6), (I, 7), (I, 8), (I, 9)], None),
    ('w_001D8C20', 0x1D8C20, [(I, 4)], None),
    ('w_001EFE00', 0x1EFE00, [(I, 4), (U, 5)], None),
    ('w_001EFEB0', 0x1EFEB0, [(I, 4), (U, 5)], None),
    ('w_001F4E20', 0x1F4E20, [(U, 4), (U, 5), (F, 12)], None),
    ('w_001FAE70', 0x1FAE70, [(I, 4)], None),
    ('w_001FBD50', 0x1FBD50, [(U, 4), (I, 5), (I, 6), (F, 12)], None),
    ('w_0021BE40', 0x21BE40, [(U, 4), (U, 5)], I),
# END GENERATED HOOKS
]
BY_ADDRESS = {address: (name, args, result) for name, address, args, result in HOOKS}

# Callees that run as original code inside the oracle (writes replayed to
# the native side). Pure helpers only: none of them reaches hardware; each
# is first rehearsed with the argument registers its hook does not pass
# poisoned (Oracle.rehearse).
RUN = {0x1026A0, 0x102738, 0x102760, 0x1028B8, 0x1028D0, 0x102918, 0x102948, 0x102958, 0x1029C0, 0x102B08,
       0x102BB0, 0x102C58, 0x103230, 0x11CB90, 0x11DE90, 0x11DF78, 0x11E080, 0x11E0A8, 0x11E2A8, 0x11E620,
       0x11E748, 0x1281C0, 0x128250, 0x128350, 0x1B1240, 0x1B12B0, 0x1B1380, 0x1B13F0, 0x1B1470, 0x1B15D0,
       0x1C6160}

# No jump tables in this module's entries (every dispatch is a compare chain).
JUMP_TABLES = {}
TABLE_BYTES = frozenset()
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
SOURCES = ('em_level12_port_area19.c', 'em_level12_port_boot.c', 'em_level12_port_creature.c')
def header_checks(source_dir=None):
    """The ctypes Hooks structure above is built from HOOKS; the C side's
    EmLevel12PortHooks (em_level12_port.h) and its wrappers
    (em_level12_port_internal.h) are text. Check that the header's generated
    block lists exactly the HOOKS entries, in order, with the same argument
    and result types, and that each hook has one wrapper l12_c_<address>
    that calls it and latches its own address."""
    base = Path(source_dir) if source_dir else ROOT / 'src/game'
    ctype = {U: 'uint32_t', I: 'int32_t', F: 'float', Q: 'uint64_t'}
    header = (base / 'em_level12_port.h').read_text()
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
    internal = (base / 'em_level12_port_internal.h').read_text()
    wrappers = internal.split('/* BEGIN GENERATED WRAPPERS */')[1].split('/* END GENERATED WRAPPERS */')[0]
    for hname, address, _, _ in HOOKS:
        tag = hname[2:]
        body = re.findall(r'static inline int l12_c_%s\(.*?\n\}\n' % tag, wrappers, re.S)
        if len(body) != 1 or ('o->h->%s(' % hname) not in body[0] or ('0x%08Xu' % address) not in body[0]:
            problems.append('%s: wrapper l12_c_%s missing or not calling / latching it' % (hname, tag))
    return problems, len(found)


def build(source_dir=None):
    """Compile the module (or the copies in `source_dir`, a mutation sweep)
    into a private library under build/level12/port."""
    OUT.mkdir(parents=True, exist_ok=True)
    base = Path(source_dir) if source_dir else ROOT / 'src/game'
    stem = 'level12_port' if not source_dir else 'level12_port_%d' % os.getpid()
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
    """Every image must hold its overlay (AREA13 for a13d, AREA19 for a19b)
    with its text equal to the user's extract/OVERLAY file; every image's
    boot text must equal the pinned ELF: every instruction the oracle
    executes is original. The designed-record area must be zero."""
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
SELF_ENTRIES = {0x825420}
SCRIBBLED_CALLBACKS = set()
HEAVY = {0x138C20, 0x13C8C0, 0x16EF50, 0x11BCF8, 0x11E420, 0x146740}


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

# AREA19 records from entry 10 (TWELFTH_LEVEL_ROUTE.md section 1.1; the same
# pool nodes in every a19b image)
CREATURES = (0x7A9FB0, 0x7AA2A0, 0x7AA590)    # behaviour 0x1383C0 (001386E0's family)
R6, R7 = 0x7AD1A0, 0x7AD490                    # [6] 0x8250F0, [7] 0x8257C0
ACTORS13 = (0x7AAB70, 0x7B2CB0)                # a13d: the two 0x141D20 actors
A0, A1, A2 = 'a19b_00_arrival', 'a19b_01_ledge', 'a19b_02_ladder1023'
D0 = 'a13d_00_stair'
NODE_A, NODE_B = FREE + 0x1000, FREE + 0x2000  # designed nodes (0x2F0 bytes each)


def fw(*values):
    """Little-endian words (floats given as Python floats, ints as ints)."""
    return b''.join(struct.pack('<f', v) if isinstance(v, float) else struct.pack('<I', v & MASK) for v in values)


def b8(v): return bytes([v & 0xFF])
def h16(v): return struct.pack('<H', v & 0xFFFF)
def w32(v): return struct.pack('<I', v & MASK)
def w64(v): return struct.pack('<Q', v & MASK64)
def f32b(v): return struct.pack('<f', v)


def bits32(b):
    return struct.pack('<I', b & MASK)


def bones(node):
    """The frame loop points D_00275B40 at the running actor's bone array
    (actor +0x110) before its behaviour runs."""
    return [(0x275B40, w32(node + 0x110))]


def case_list(elf):
    rng = random.Random(0x1EC)
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
        # scratchpad addresses go to the scratchpad patches (patched() takes one image)
        spad = list(spad) + [(a, v) for a, v in patches if a >= 0x70000000]
        patches = [(a, v) for a, v in patches if a < 0x70000000]
        cases.append((label, beat, entry, tuple(args), list(patches), list(spad),
                      Script(queues, default), frozenset(run_set)))

    capture_cases(add)
    captured = len(cases)
    creature_cases(add, rng)
    steer_cases(add, rng)
    area19_cases(add, rng)
    actor13_cases(add, rng)
    player_cases(add, rng)
    math_cases(add, rng)
    render_cases(add, rng)
    extra_cases(add)
    bound_cases(add)
    survivor_cases(add)
    return cases, captured, len(cases) - captured


def live_creatures(ram):
    return [n for n in CREATURES if ram[n] and u32(ram, n + 0x10) == 0x1383C0]


def capture_cases(add):
    """Every entry on the captured state of every image it applies to."""
    for beat in A19_BEATS:
        ram, _ = CAPTURES[beat]
        for n in live_creatures(ram):
            e = n + 0x1F0
            for entry in (0x138900, 0x138C20, 0x13BA20, 0x13BBB0, 0x13BE60, 0x13BF20, 0x13C1F0, 0x13C4C0, 0x13C8C0):
                add('capture %s %06X @%X' % (beat, entry, n), beat, entry, [n, e], bones(n))
            add('capture %s 13D220 @%X' % (beat, n), beat, 0x13D220, [n])
            add('capture %s 13CD50 @%X' % (beat, n), beat, 0x13CD50, [n, 0x700038A0])
            add('capture %s 1C6910 @%X' % (beat, n), beat, 0x1C6910, [n])
            add('capture %s 1CB060 @%X' % (beat, n), beat, 0x1CB060, [n])
        add('capture %s 825420' % beat, beat, 0x825420, [R6], bones(R6))
        add('capture %s 825930' % beat, beat, 0x825930, [R7], bones(R7))
        add('capture %s 8250C0' % beat, beat, 0x8250C0, [])
        for n in (0, 1, 2):
            add('capture %s 1E8B40 %d' % (beat, n), beat, 0x1E8B40, [n])
        add('capture %s 1B2F70' % beat, beat, 0x1B2F70, [PLAYER + 0xB0, FREE + 0x100])
        for entry in (0x16EF50, 0x179560, 0x1821E0, 0x1C6910):
            add('capture %s %06X player' % (beat, entry), beat, entry, [PLAYER])
    for beat in A13_BEATS:
        ram, _ = CAPTURES[beat]
        for n in ACTORS13:
            if not ram[n] or u32(ram, n + 0x10) != 0x141D20:
                continue
            add('capture %s 1437E0 @%X' % (beat, n), beat, 0x1437E0, [n, n + 0x1F0], bones(n))
            add('capture %s 146740 @%X' % (beat, n), beat, 0x146740, [n, n + 0x1F0], bones(n))
            add('capture %s 1B39F0 @%X' % (beat, n), beat, 0x1B39F0, [n, n + 0xB0, FREE + 0x100])
            add('capture %s 1A96F0 @%X' % (beat, n), beat, 0x1A96F0, [n, PLAYER])
        add('capture %s 1B2F70' % beat, beat, 0x1B2F70, [PLAYER + 0xB0, FREE + 0x100])


CR = 0x7A9FB0          # a19b_02: a creature of 001386E0's family (TWELFTH_LEVEL_ROUTE.md section 1.1)
HIT = FREE + 0x6000    # a designed hit object (0x700031D0)


def hit_obj(kind=0x5B, x=0.0, z=1.0):
    """The designed hit object: +0x1A kind, +0x24 / +0x2C a direction."""
    return [(HIT + 0x1A, b8(kind)), (HIT + 0x24, f32b(x)), (HIT + 0x2C, f32b(z)), (0x700031D0, w32(HIT))]


def table(rows):
    """A 0019BC40 result (the column table) as writes: rows = [(height,
    flag, slope)]."""
    out = [(0x700031E0, w32(len(rows)))]
    for i, (h, fl, sl) in enumerate(rows):
        out += [(0x700030F0 + 4 * i, f32b(h)), (0x70003170 + 2 * i, h16(fl)), (0x282250 + 4 * i, f32b(sl))]
    return out


def creature_cases(add, rng):
    """001386E0's family on a19b_02's creature, the state bytes and the
    behaviour block patched; the callees' results on both sides of every
    test."""
    B = A2
    n, e = CR, CR + 0x1F0
    base = bones(n)
    far = [(n + 0xB0, fw(2000.0, 300.0, 2000.0, 1.0))]
    near = [(n + 0xB0, fw(1070.0, 231.0, 982.0, 1.0))]
    # 00138900: behaviour 0
    for c20, dd, f44 in ((1, 1, 0.5), (0, 0, 0.5), (0, 1, 0.5), (0, 1, 0.0), (0, 1, -1.0)):
        add('cr 138900 s0 c%d d%d %g' % (c20, dd, f44), B, 0x138900, [n, e],
            base + [(n + 6, b8(0)), (e + 0x20, h16(c20)), (n + 0xD, b8(dd)), (e + 0x44, f32b(f44))])
    for f44 in (0.0, 0.5):
        for b81 in (0, 4, 0xFB):
            add('cr 138900 s1 %g %X' % (f44, b81), B, 0x138900, [n, e],
                base + [(n + 6, b8(1)), (e + 0x44, f32b(f44)), (e + 0x81, b8(b81))])
    for c7, pos, a in ((0, near, 0), (1, near, 0), (1, far, 1), (1, far, 0), (0, far, 1)):
        add('cr 138900 s2 %d %s %d' % (c7, 'near' if pos is near else 'far', a), B, 0x138900, [n, e],
            base + pos + [(n + 6, b8(2)), (0x8106C7, b8(c7)), (n + 0xA, b8(a | 0x10)), (e + 0x20, h16(5))])
    for f3c, f70 in ((20.0, 0), (30.0, 0x1000), (20.0, 0x1000), (30.0, 0)):
        add('cr 138900 s3 %g %X' % (f3c, f70), B, 0x138900, [n, e],
            base + [(n + 6, b8(3)), (n + 0x3C, f32b(f3c)), (e + 0x70, w32(f70))])
    for st in (4, 5, 0xFF):
        add('cr 138900 s%X' % st, B, 0x138900, [n, e], base + [(n + 6, b8(st)), (n + 5, b8(7))])
    # 0013BA20: the pose word and the lean
    for h2c in (0, 4, 1, 0x8000, 0x8004):
        for dc, c8 in ((0.01, 1.0), (0.5, -1.0), (-0.5, 1.0), (-0.01, -1.0), (0.5, 0.0), (-0.5, 0.0), (0.5, 1.0),
                       (-0.5, -1.0)):
            if h2c not in (0, 1) and dc not in (0.5, -0.5):
                continue
            add('cr 13BA20 h%X d%g c%g' % (h2c, dc, c8), B, 0x13BA20, [n, e],
                [(n + 0x2C, h16(h2c)), (n + 0xC4, f32b(0.25 + dc)), (e + 0x60, f32b(0.25)), (n + 0xC8, f32b(c8))])
    # 0013BE60: the drift gate
    for b82, b4, b5 in ((1, 1, 3), (0, 1, 3), (0, 1, 2), (0, 2, 3), (0, 0, 0)):
        add('cr 13BE60 %d %d %d' % (b82, b4, b5), B, 0x13BE60, [n, e],
            [(e + 0x82, b8(b82)), (n + 4, b8(b4)), (n + 5, b8(b5))])
    # 0013BBB0: the pose by the lean (001C6160 stubbed: the clip length)
    leans = ((0.0, 1.0), (1.0, 1.0), (0.5, 0.5), (0.0, 0.0), (0.3, 0.2), (1.2, 0.2), (-0.1, -0.5))
    for cur in (0, 4, 1, 2, 0x8000, 0x8004):
        for f5c, f50 in leans:
            ts = (44.5, 43.5, 10.0, 50.0) if cur == 0 else (22.5, 21.5, 5.0, 30.0) if cur == 4 else (3.0,)
            for t in ts:
                add('cr 13BBB0 c%X %g %g t%g' % (cur, f5c, f50, t), B, 0x13BBB0, [n, e],
                    [(n + 0x2C, h16(cur)), (e + 0x5C, f32b(f5c)), (e + 0x50, f32b(f50)), (n + 0x3C, f32b(100.0 - t))],
                    queues={0x1C6160: [100]}, run_set=RUN - {0x1C6160})
    # 0013BF20: the ground probes
    for r1 in (0, 1):
        for frame in (0, 1):
            for r2 in ((0, 1) if r1 == 0 else (0,)):
                for f50 in (0.0, 0.5, -0.5):
                    for r3 in (0, 1):
                        add('cr 13BF20 %d %d %d %g %d' % (r1, frame, r2, f50, r3), B, 0x13BF20, [n, e],
                            [(FRAME, w32(0x1E744 + frame)), (e + 0x50, f32b(f50)), (e + 0x81, b8(0x40))],
                            queues={0x19AD00: [r1, r2], 0x19AB20: [r3]})
    # 0013D220
    for r, kind in ((0, 0x5B), (1, 0x5B), (1, 0x5A)):
        add('cr 13D220 %d %X' % (r, kind), B, 0x13D220, [n], hit_obj(kind), queues={0x19B6C0: [r]})


def steer_cases(add, rng):
    """The steering family (0013C8C0 / 0013CD50 / 0013C4C0 / 0013C1F0) and
    00138C20 on a19b_02's creature."""
    B = A2
    n, e = CR, CR + 0x1F0
    base = bones(n)
    spot = [(n + 0xB0, fw(900.0, 280.0, 950.0, 1.0)), (n + 0xC4, f32b(0.5))]
    # 0013C1F0
    for c28 in (0, 5):
        for c2a in (0xB3, 0xB4):
            for b81 in (0, 2, 4, 6):
                for b80 in (0, 1, 8, 9):
                    if c2a == 0xB4 and b81 not in (0, 2):
                        continue
                    for rnd in ((0, 0x4000) if b80 in (0, 8) and (c2a == 0xB4 or b81) and not b81 & 6 else (0,)):
                        for hits in ((0, 0), (1,), (0, 1)) if c28 == 0 and b80 in (0, 8) else ((0, 0),):
                            add('st 13C1F0 %d %X %X %X %X %s' % (c28, c2a, b81, b80, rnd, hits), B, 0x13C1F0, [n, e],
                                spot + [(e + 0x28, h16(c28)), (e + 0x2A, h16(c2a)), (e + 0x81, b8(b81)),
                                        (e + 0x80, b8(b80))],
                                queues={0x19AD00: list(hits)}, default={0x122BB8: rnd})
    # 0013C4C0
    for b80 in (0, 1, 4, 5, 2, 3, 0xC):
        for b81 in (0, 2, 4, 6):
            for r1 in (0, 1):
                for d in ((3.0, 9.0) if r1 else (0.0,)):
                    for r2, q in (((0, 0), (0, 1), (1, 0)) if not r1 and not (b81 & (4 if b80 & 1 else 2)) else
                                  ((0, 0),)):
                        add('st 13C4C0 %X %X %d %g %d %d' % (b80, b81, r1, d, r2, q), B, 0x13C4C0, [n, e],
                            spot + hit_obj() + [(e + 0x80, b8(b80)), (e + 0x81, b8(b81)),
                                                (0x700031B0, fw(900.0, 280.0, 950.0 + d, 1.0))],
                            queues={0x19AFE0: [r1, r2], 0x19B6C0: [q], 0x19A6F0: [1, 0]},
                            default={0x122BB8: 0x800})
    # 0013CD50: the obstacle class (0019AFE0 results, the column tables)
    seg = FREE + 0x300
    segp = [(seg, fw(900.0, 100.0, 960.0, 1.0))]
    for r, d4 in ((0, 0), (2, 0), (2, 1), (1, 0), (3, 1), (6, 1)):
        add('st 13CD50 r%d %d' % (r, d4), B, 0x13CD50, [n, seg], spot + segp + [(0x700031D4, w32(d4))],
            queues={0x19AFE0: [r], 0x19BC40: [Scribble(None, table([]))]})
    first = {
        'lohi': [(50.0, 0, 0.0), (90.0, 1, 0.0), (95.0, 0, 2.0), (120.0, 1, 0.0), (130.0, 1, 0.0)],
        'nohi': [(80.0, 0, 0.0), (120.0, 0, 0.0)],
        'steep': [(80.0, 0, 0.0), (120.0, 1, 2.0)],
        'none': [(60.0, 1, 0.0)],
        'near': [(85.0, 0, 0.0), (110.0, 1, 0.0)],
    }
    second = {
        'm0': [], 'm1': [(99.0, 0, 0.0)], 'm2': [(101.0, 0, 0.0)], 'm3': [(99.0, 0, 0.0), (101.0, 0, 0.0)],
        'm0b': [(40.0, 0, 0.0), (500.0, 0, 0.0)],
    }
    for fk, rows in first.items():
        for sk, rows2 in second.items():
            add('st 13CD50 t %s %s' % (fk, sk), B, 0x13CD50, [n, seg], spot + segp,
                queues={0x19AFE0: [2], 0x19BC40: [Scribble(None, table(rows)), Scribble(None, table(rows2))]},
                spad=[(0x700031D4, w32(HIT))])
    add('st 13CD50 t empty', B, 0x13CD50, [n, seg], spot + segp + [(0x700031D4, w32(HIT))],
        queues={0x19AFE0: [2], 0x19BC40: [Scribble(None, table([(60.0, 0, 0.0)])), Scribble(None, table([]))]})
    # 0013C8C0
    for b80 in (1, 4, 5, 0x10, 0x30):
        add('st 13C8C0 pre %X' % b80, B, 0x13C8C0, [n, e], spot + [(e + 0x80, b8(b80))],
            queues={0x19AFE0: [0, 0, 0, 0], 0x19AD00: [1, 1]})
    for coin in (0, 1):
        for r2, q in ((0, 0), (1, 0), (0, 1)):
            add('st 13C8C0 c0 %d %d %d' % (coin, r2, q), B, 0x13C8C0, [n, e],
                spot + hit_obj() + [(e + 0x80, b8(0)), (FRAME, w32(0x1E780 + coin))],
                queues={0x19AFE0: [0, r2, 0, 0], 0x19B6C0: [q, 0, 0], 0x19AD00: [1, 1]})
    for x, z in ((0.0, 1.0), (0.0, -1.0), (1.0, 0.0)):
        for side in (0, 1):
            add('st 13C8C0 c3 %g %g %d' % (x, z, side), B, 0x13C8C0, [n, e],
                spot + hit_obj(0x10, x, z) + [(e + 0x80, b8(0)), (0x700031B0, fw(950.0, 280.0, 990.0, 1.0))],
                queues={0x19AFE0: [1, 0, 0, 0], 0x19A6F0: [side, 1 - side], 0x19AD00: [1, 1]},
                default={0x122BB8: 0x12345})
    for sk, rows2 in (('m1', second['m1']), ('m2', second['m2'])):
        add('st 13C8C0 c12 %s' % sk, B, 0x13C8C0, [n, e], spot + [(e + 0x80, b8(0)), (0x700031D4, w32(HIT))],
            queues={0x19AFE0: [2, 0, 0], 0x19BC40: [Scribble(None, table(first['lohi'])), Scribble(None, table(rows2))],
                    0x19AD00: [1, 1]})
    # 00138C20: the wander (0013C8C0 first; its probes miss)
    quiet = {0x19AFE0: [0, 0, 0, 0, 0, 0], 0x19B6C0: [0, 0]}
    for st in (0, 1, 2):
        for tick in (0, 1):
            for tgt in ((0, 1) if tick else (0,)):
                for b80 in ((0, 1, 4, 5) if st != 2 else (0,)):
                    for c20, c22 in (((0, 0), (5, 5), (0, 5)) if st != 2 else ((0, 0),)):
                        if b80 not in (0,) and (c20, c22) != (0, 0):
                            continue
                        target = (1070.0, 231.0, 982.0) if tgt else (2000.0, 300.0, 2000.0)
                        add('st 138C20 s%d t%d g%d b%X %d %d' % (st, tick, tgt, b80, c20, c22), B, 0x138C20, [n, e],
                            base + [(n + 0xB0, fw(1070.0, 231.0, 982.0, 1.0)), (n + 6, b8(st)), (e + 0x80, b8(b80)),
                                    (e + 0x20, h16(c20)), (e + 0x22, h16(c22)), (e + 0, fw(*target, 1.0)),
                                    (FRAME, w32(0x1E780 - 0x80 + (0 if tick else 1))), (e + 0x5C, f32b(-0.5))],
                            queues={**quiet, 0x19AD00: [1, 1, 1, 1]},
                            default={0x122BB8: 0x7FFF00})
    # 00138C20 state 0: the pitch floor check (001B2F70 over a designed table)
    for rnd, fl in ((0x7FFF00, 300.0), (0x7FFF00, 100.0), (0, 300.0), (0, 0.0)):
        for c22 in (0, 5):
            add('st 138C20 floor %X %g %d' % (rnd, fl, c22), B, 0x138C20, [n, e],
                base + [(n + 0xB0, fw(900.0, 280.0, 950.0, 1.0)), (n + 6, b8(0)), (e + 0x80, b8(0)),
                        (e + 0x20, h16(5)), (e + 0x22, h16(c22)), (FRAME, w32(0x1E781)), (e + 0x5C, f32b(-0.5))],
                queues={**quiet, 0x19BC40: [Scribble(None, table([(fl, 1, 0.0)]))], 0x19AD00: [1, 1]},
                default={0x122BB8: rnd})
    # 00138C20 tail: the stun count, bit 0 of +0xA, the alert count, the chirp
    for dd, c30, aa, c2e, r1, r2, r3, c2c, c86 in (
            (1, 0x95, 0, 1, 0, 0, 0, 0, 1), (1, 0x96, 0, 1, 0, 0, 0, 0, 1), (0, 0, 1, 0, 0, 0, 0, 0, 0),
            (0, 0, 0, 0, 1, 0, 0, 5, 0), (0, 0, 0, 0, 1, 1, 0, 5, 1), (0, 0, 0, 0, 1, 0, 1, 5, 1),
            (0, 0, 0, 0, 1, 0, 0, 0x77, 1), (0, 0, 0, 0, 0, 0, 0, 5, 1)):
        player = (1070.0, 231.0, 982.0) if r1 else (5000.0, 0.0, 5000.0)
        add('st 138C20 tail %d %X %d %d %d%d%d %X %d' % (dd, c30, aa, c2e, r1, r2, r3, c2c, c86), B, 0x138C20,
            [n, e],
            base + [(n + 0xB0, fw(1070.0, 231.0, 982.0, 1.0)), (n + 6, b8(2)), (e + 0x80, b8(0)),
                    (n + 0xD, b8(dd | 0x40)), (e + 0x30, h16(c30)), (n + 0xA, b8(aa)), (e + 0x2E, h16(c2e)),
                    (e + 0x2C, h16(c2c)), (e + 0x86, b8(c86)), (0x810360, fw(*player, 1.0))],
            queues={**quiet, 0x21BE40: [r2], 0x19AFE0: [0, 0, 0, 0, r3], 0x19AD00: [1, 1]},
            default={0x122BB8: 0x1234})


def area19_cases(add, rng):
    """The AREA19 overlay entries: the effect and the group behaviours on a
    designed node, [6] and [7] with their state bytes patched."""
    B = A2
    s = NODE_A
    mtx = [(s + 0xD0, fw(1.0, 0.5, -0.25, 0.0, 0.0, 2.0, 0.0, 0.0, -1.5, 0.0, 1.0, 0.0, 900.0, 200.0, 950.0, 1.0)),
           (s + 0x100, fw(900.0, 200.0, 950.0, 1.0))]
    for st in (0, 1, 2, 3, 4):
        for dd in (0, 1, 2):
            for t in ((0.5, 1.47, 1.5, 1.4700001) if st == 1 and dd < 2 else (0.5,)):
                for seed in ((0x12345678, -0x7654321) if st == 1 and t == 0.5 else (0x12345678,)):
                    add('a19 824BE0 s%d d%d t%r %X' % (st, dd, t, seed & MASK), B, 0x824BE0, [s],
                        mtx + [(s + 4, b8(st)), (s + 0xD, b8(dd)), (s + 0x1F0, f32b(t)), (s + 0x1F4, w32(seed))],
                        queues={0x1CCF70: [0x55]}, default={0x122BB8: 0x2468ACE0})
        for entry in (0x824EF0, 0x824F70, 0x825030):
            add('a19 %06X s%d' % (entry, st), B, entry, [s], mtx + [(s + 4, b8(st))])
    # 0x825420 ([6]'s second stage)
    n = R6
    for st in (0, 1, 2, 3):
        for r, y in (((0, 195.0), (2, 195.0), (1, 189.0), (1, 190.0), (1, 200.0), (1, 201.0), (1, 195.0))
                     if st == 0 else ((0, 195.0), (1, 195.0)) if st == 1 else ((0, 195.0),)):
            for b92 in ((0, 1) if (st, r) in ((0, 0), (2, 0)) else (0,)):
                add('a19 825420 s%d r%d y%g b%d' % (st, r, y, b92), B, 0x825420, [n],
                    bones(n) + [(n + 5, b8(st)), (0x810354, f32b(y)), (0x70003B92, b8(b92))],
                    queues={0x1B1EA0: [r], 0x1BA1F0: [r]})
    # 0x825930 ([7]'s second sequence)
    n = R7
    for b in (0, 4, 0xFB):
        add('a19 825930 s0 b%X' % b, B, 0x825930, [n], [(n + 5, b8(0)), (n + 0xB, b8(b))])
    for c in (0, 1, 2, 3, 4, 5, 0xFF):
        for r in ((0, 1) if c in (4, 0xFF) else (0,)):
            add('a19 825930 s1 c%X r%d' % (c, r), B, 0x825930, [n],
                [(n + 5, b8(1)), (0x8107F6, b8(c)), (0x810854, b8(0x41))], queues={0x1BA1F0: [r]})
    for st in (2, 3):
        add('a19 825930 s%d' % st, B, 0x825930, [n], [(n + 5, b8(st))])


ACT = 0x7AAB70        # a13d_00: one of the two 0x141D20 actors


def actor13_cases(add, rng):
    """The 0x141D20 actor's behaviour 5 and side probes (a13d_00), 001B3390
    and 001A96F0 on designed records."""
    B = D0
    e, d = ACT, ACT + 0x1F0
    base = bones(e)
    # 001437E0
    for f78 in (0, 1, 0x80):
        add('ac 1437E0 s0 %X' % f78, B, 0x1437E0, [e, d], base + [(e + 6, b8(0)), (d + 0x78, b8(f78))])
    for h2c in (0, 0x8000):
        for f3c in (100.0, 120.0):
            for rate, f8c in ((0.0, 2.0), (0.08, 3.95), (0.08, 3.0), (-0.08, 1.05), (-0.08, 2.0), (0.08, 4.0)):
                for dd, f78, f20 in (((0, 1, 0), (0x80, 0, 0), (0, 1, 1), (0, 0, 1)) if rate > 0 and f8c >= 3.95
                                     else ((0, 0, 0),)):
                    for f30 in ((0, 0x1000) if h2c == 0 and f3c == 100.0 else (0x1000,)):
                        add('ac 1437E0 s1 %X %g %g %g %X %d %d %X' % (h2c, f3c, rate, f8c, dd, f78, f20, f30), B,
                            0x1437E0, [e, d],
                            base + [(e + 6, b8(1)), (e + 0x2C, h16(h2c)), (e + 0x3C, f32b(f3c)),
                                    (d + 0x54, f32b(rate)), (e + 0x8C, f32b(f8c)), (e + 0xD, b8(dd | 3)),
                                    (d + 0x78, b8(f78)), (d + 0x20, w32(f20)), (d + 0x30, w32(f30))],
                            default={0x122BB8: 0x7654321})
    add('ac 1437E0 s2', B, 0x1437E0, [e, d], base + [(e + 6, b8(2))])
    # 00146740: near (within 23.5) / far
    here = [(e + 0xB0, fw(800.0, 200.0, 820.0, 1.0)), (e + 0xC4, f32b(0.3))]
    for dz in (5.0, 23.5, 30.0, 100.0):
        for x, z in ((0.0, 1.0), (0.0, -1.0), (1.0, 0.0)):
            for side, a, b in (((0, 1, 0), (1, 0, 1), (0, 0, 0), (1, 1, 1)) if dz > 23.5 else ((0, 0, 0),)):
                if dz > 23.5 and (x, z) != (0.0, 1.0):
                    continue
                add('ac 146740 %g %g %g %d%d%d' % (dz, x, z, side, a, b), B, 0x146740, [e, d],
                    base + here + hit_obj(0x10, x, z) + [(0x700031B0, fw(800.0, 200.0, 820.0 + dz, 1.0))],
                    queues={0x19A6F0: [a, b]}, default={0x122BB8: side << 11})
    # 001B39F0 (on designed probes: 0019A6F0 hits, 001028D0 distances)
    for a, b, sx in ((0, 0, 0), (1, 0, 0), (0, 1, 0), (1, 1, 0), (1, 1, 1)):
        for d31 in ((0.0, 3.0, 0.0), (0.5, 0.0, 0.0), (100.0, 0.0, 50.0)):
            add('ac 1B39F0 %d%d %d %g %g' % (a, b, sx, d31[0], d31[2]), B, 0x1B39F0, [e, e + 0xB0, FREE + 0x100],
                [(0x700031B0, fw(*d31, 1.0))], queues={0x19A6F0: [a, b]}, default={0x122BB8: sx << 11})
    # 001B3390
    a1, a2 = FREE + 0x400, FREE + 0x500
    for r, q, v, f in ((0, 0, 1.0, 0.5), (3, 0, 1.0, 0.5), (3, 1, 1.0, 0.5), (3, 1, 0.5, 0.5), (3, 1, 0.25, 0.5),
                       (-1, 1, 2.0, -1.0)):
        add('ac 1B3390 %d %d %g %g' % (r, q, v, f), B, 0x1B3390, [e, a1, a2, fb(f)],
            [(a1, fw(1.0, 2.0, 3.0, 9.0)), (a2, fw(4.0, 5.0, 6.0, 9.0)), (0x700031C0, fw(0.5, 0.25, -0.5, 0.0))],
            queues={0x19B2C0: [r], 0x19A310: [Scribble(q, [(STACK_TOP - 4, f32b(v))])]})
    # 001A96F0: the box test of a pair
    pa, pb, box = FREE + 0x600, FREE + 0x900, FREE + 0xC00
    for dx, dz, dy in ((1.0, 1.0, 1.0), (5.0, 1.0, 1.0), (1.0, 5.0, 1.0), (1.0, 1.0, 5.0), (2.0, 3.0, 4.0),
                       (2.0, 3.0, 4.5), (-2.0, -3.0, -4.0)):
        add('ac 1A96F0 %g %g %g' % (dx, dz, dy), B, 0x1A96F0, [pa, pb],
            [(pa + 0x30, w32(box)), (box, fw(2.0, 3.0, 3.0)), (pa + 0xB0, fw(10.0, 20.0, 30.0)),
             (pb + 0xB0, fw(10.0 + dx, 20.0 + dy, 30.0 + dz)), (pb + 0x54, h16(0x5555)), (0x70003B86, h16(7))])


def player_cases(add, rng):
    """The player's ledge state (a19b_01's player block 0x8102B0, the state
    bytes patched) and 001831F0."""
    B = A1
    p = PLAYER
    for st in (0, 0x14):
        for f2f1 in (0, 1):
            add('pl 16EF50 s%X f%d' % (st, f2f1), B, 0x16EF50, [p], [(p + 6, b8(st)), (p + 0x2F1, b8(f2f1))])
    for st in (1, 0x16):
        for f200 in (0, 0x1000):
            add('pl 16EF50 s%X %X' % (st, f200), B, 0x16EF50, [p], [(p + 6, b8(st)), (p + 0x200, w32(f200))],
                queues={0x188610: [0x14B]})
    for f200 in (0, 0x8000):
        add('pl 16EF50 s15 %X' % f200, B, 0x16EF50, [p], [(p + 6, b8(0x15)), (p + 0x200, w32(f200))])
    for st in (0xA, 0xB):
        for gate in ((0.0, 0.0, 0), (1.0, 0.0, 0), (0.0, 1.0, 0), (0.0, 0.0, 2)):
            add('pl 16EF50 s%X gate %g %g %d' % (st, gate[0], gate[1], gate[2]), B, 0x16EF50, [p],
                [(p + 6, b8(st)), (p + 7, b8(2)), (p + 0x224, f32b(gate[0])), (p + 0x22C, f32b(gate[1])),
                 (p + 0xF, b8(gate[2]))])
    nogate = [(p + 0x224, f32b(0.0)), (p + 0x22C, f32b(0.0)), (p + 0xF, b8(0))]
    for kind in (0, 2, 3, 5, -1):
        for f23f in ((3, 1) if kind in (2, 3) else (2,)):
            add('pl 16EF50 sB 0 k%d %d' % (kind, f23f), B, 0x16EF50, [p],
                nogate + [(p + 6, b8(0xB)), (p + 7, b8(0)), (p + 0x24C, w32(kind)), (0x810E57, b8(f23f)),
                          (p + 0x23F, b8(f23f))])
    for f200, f23b in ((0x1000, 0x39), (0x1000, 0x38), (0, 0x39)):
        add('pl 16EF50 sB 1 %X %X' % (f200, f23b), B, 0x16EF50, [p],
            nogate + [(p + 6, b8(0xB)), (p + 7, b8(1)), (p + 0x200, w32(f200)), (p + 0x23B, b8(f23b))],
            queues={0x188610: [0x14C]})
    for st in (0x17, 2):
        add('pl 16EF50 s%X' % st, B, 0x16EF50, [p], [(p + 6, b8(st))])
    # 00179560
    for f38 in (0.0, 0.5, -0.5):
        add('pl 179560 %g' % f38, B, 0x179560, [p], [(p + 0x38, f32b(f38))])
    # 0017F9E0 (the ledge record at 0x700031D0)
    rec = FREE + 0xA00
    for f3c, f34, c4 in ((1.0, 0.0, 0.0), (0.0, 1.0, 3.0), (-1.0, 0.0, 1.0), (0.0, -1.0, -2.0)):
        add('pl 17F9E0 %g %g %g' % (f3c, f34, c4), B, 0x17F9E0, [p],
            [(rec + 0x3C, f32b(f3c)), (rec + 0x34, f32b(f34)), (p + 0xC4, f32b(c4)), (0x700031D0, w32(rec)),
             (0x700038A0, fw(1.0, 2.0, 3.0)), (0x700038B0, fw(5.0, 6.0, 7.0))])
    # 001821E0
    for a, b, f in ((0.0, 0.0, 0), (0.0, 0.0, 2), (1.0, 0.0, 0), (0.0, -1.0, 0), (-0.0, 0.0, 0xFD)):
        add('pl 1821E0 %g %g %X' % (a, b, f), B, 0x1821E0, [p],
            [(p + 0x224, f32b(a)), (p + 0x22C, f32b(b)), (p + 0xF, b8(f))])
    for k in (0, 1, 2, -1):
        add('pl 1831F0 %d' % k, B, 0x1831F0, [k])


def math_cases(add, rng):
    """0011BCF8 / 0011E420 over the float classes and the error modes;
    001B1270 on a designed record."""
    B = D0
    xs = [1.0, -1.0, 0.0, -0.0, 0.25, -0.3, 0.5, -0.5, 0.75, -0.75, 0.9999, -0.9999, 1e-30]
    xb = [fbits(v) for v in xs] + [0x3F800001, 0xBF800001, 0x7F800000, 0xFF800000, 0x7FC00000, 0x23000000,
                                   0x23000001, 0xA3000000, 0x3EFFFFFF, 0xBEFFFFFF, 0x3F000000, 0xBF000000,
                                   0x40000000, 0x00000001]
    for x in xb:
        add('m 11BCF8 %08X' % x, B, 0x11BCF8, [x])
    for mode in (1, -1, 2, 0):
        for x in (fbits(0.5), fbits(2.0), fbits(-2.0), 0x7FC00000, 0x7F800000, fbits(1.0)):
            for m, err in (((0, 0), (1, 0), (0, 0x22), (1, 0x22)) if mode not in (-1,) and x in (fbits(2.0),) else
                           ((0, 0),)):
                add('m 11E420 m%d %08X %d %X' % (mode, x, m, err), B, 0x11E420, [x],
                    [(0x26C5D0, w32(mode))],
                    queues={0x11DB90: [Scribble(m, [(STACK_TOP - 0x60 + 0x20, w32(err))])],
                            0x11FD78: [FREE + 0xF00, FREE + 0xF10], 0x127758: [fbits(0.125)]})
    obj = FREE + 0xB00
    for px, py in ((1.0, 2.0), (-3.0, 0.5), (0.0, 0.0)):
        add('m 1B1270 %g %g' % (px, py), B, 0x1B1270, [obj, fb(px), fb(py)], [(obj, fw(9.0, 0.5, 1.5))])


def render_cases(add, rng):
    """The render-list helpers and 001E8B40 on the captured context, and
    001C9570 on designed matrices."""
    B = A2
    arg = FREE + 0xD00
    for r in (0, 1):
        for sel in (3, 1):
            add('r 1D3F60 %d %d' % (sel, r), B, 0x1D3F60, [sel, arg], [(arg + 4, w32(0x12345))],
                queues={0x1D2910: [r]})
        add('r 1CAFA0 %d' % r, B, 0x1CAFA0, [CR, arg], [(arg + 4, w32(7))], queues={0x1D2910: [r]})
        add('r 1D40D0 %d' % r, B, 0x1D40D0, [arg], [(arg + 4, w32(0x10007))], queues={0x1D2910: [r]})
    m = FREE + 0xE00
    for sc in ((1.0, 2.0, 3.0), (0.0, -1.0, 0.5)):
        add('r 1C9570 %g %g %g' % sc, B, 0x1C9570, [m, m + 0x40, m + 0x50, m + 0x60],
            [(m + 0x40, fw(1.0, 2.0, 3.0, 1.0)), (m + 0x50, fw(0.1, 0.2, 0.3, 0.0)), (m + 0x60, fw(*sc, 1.0))])
    for n in (0, 1, 2, -1):
        add('r 1E8B40 %d' % n, D0, 0x1E8B40, [n], [(0x275C20, w32(FREE + 0xD800)), (FREE + 0xD858, w32(FREE + 0x1E00)),
                                                   (FREE + 0xD800 + 0xA0B8, w32(FREE + 0x1E40))])


def around(value):
    """The float `value` exactly and one unit in the last place either
    side, as bit patterns."""
    return (fbits(value), up(value), down(value))


def bound_cases(add):
    """Each float compare against a constant (or between two values) with
    the compared value exactly at the bound and one float either side."""
    B = A2
    n, e = CR, CR + 0x1F0
    base = bones(n)
    # 00138900: e +0x44 <= 0, the distance <= 150, self +0x3C <= 25
    for v in (0x00000000, 0x80000000, 0x00000001, 0x80000001, fbits(1e-38)):
        add('bound 138900 s0 44 %08X' % v, B, 0x138900, [n, e],
            base + [(n + 6, b8(0)), (e + 0x20, h16(0)), (n + 0xD, b8(1)), (e + 0x44, bits32(v))])
        add('bound 138900 s1 44 %08X' % v, B, 0x138900, [n, e], base + [(n + 6, b8(1)), (e + 0x44, bits32(v))])
    for dz in around(150.0):
        add('bound 138900 s2 d %08X' % dz, B, 0x138900, [n, e],
            base + [(n + 6, b8(2)), (0x8106C7, b8(1)), (n + 0xA, b8(0)), (e + 0x20, h16(0)),
                    (n + 0xB0, fw(1070.0, 231.0, 982.0, 1.0)), (0x810360, fw(1070.0, 231.0, 982.0, 1.0)),
                    (n + 0xB8, bits32(fbits(982.0))), (0x810368, bits32(fbits(982.0)))],
            queues={0x1B15D0: [dz]}, run_set=RUN - {0x1B15D0})
    for v in around(25.0):
        add('bound 138900 s3 %08X' % v, B, 0x138900, [n, e], base + [(n + 6, b8(3)), (n + 0x3C, bits32(v))])
    # 00138C20: e +0x44 < 0.4, 20 + floor <= y, e +0x5C < 0
    quiet = {0x19AFE0: [0, 0, 0, 0, 0, 0], 0x19B6C0: [0, 0], 0x19AD00: [1, 1]}
    for v in around(0.4):
        add('bound 138C20 44 %08X' % v, B, 0x138C20, [n, e],
            base + [(n + 6, b8(2)), (e + 0x80, b8(0)), (e + 0x44, bits32(v))], queues=dict(quiet))
    for fl in around(260.0):
        add('bound 138C20 floor %08X' % fl, B, 0x138C20, [n, e],
            base + [(n + 0xB0, fw(900.0, 280.0, 950.0, 1.0)), (n + 6, b8(0)), (e + 0x80, b8(0)), (e + 0x20, h16(5)),
                    (e + 0x22, h16(5)), (FRAME, w32(0x1E781)), (e + 0x5C, f32b(-0.5))],
            queues={**quiet, 0x19BC40: [Scribble(None, [(0x700031E0, w32(1)), (0x700030F0, bits32(fl)),
                                                       (0x70003170, h16(1)), (0x282250, f32b(0.0))])]})
    for v in (0x00000000, 0x80000000, 0x80000001):
        add('bound 138C20 5c %08X' % v, B, 0x138C20, [n, e],
            base + [(n + 6, b8(0)), (e + 0x80, b8(0)), (e + 0x20, h16(5)), (e + 0x22, h16(5)), (FRAME, w32(0x1E781)),
                    (e + 0x5C, bits32(v))], queues=dict(quiet))
    # 0013BA20: |d| <= 0.034906585, d < 0, self +0xC8 < / <= 0
    for v in around(0.034906585) + around(-0.034906585):
        add('bound 13BA20 d %08X' % v, B, 0x13BA20, [n, e],
            [(n + 0x2C, h16(1)), (n + 0xC4, bits32(v)), (e + 0x60, f32b(0.0)), (n + 0xC8, f32b(0.5))])
    for c8 in (0x00000000, 0x80000000, 0x00000001, 0x80000001):
        for v in (fbits(0.01), fbits(-0.01)):
            add('bound 13BA20 c8 %08X %08X' % (c8, v), B, 0x13BA20, [n, e],
                [(n + 0x2C, h16(1)), (n + 0xC4, bits32(v)), (e + 0x60, f32b(0.0)), (n + 0xC8, bits32(c8))])
    # 0013BBB0: +0x5C < +0x50, +0x50 < pi/4, d == 0, +0x5C <= 0, d < pi/4, (1 + t) / 2 <= 23, 2 (1 + t) <= 45
    pi4 = math.pi / 4
    for f5c, f50 in ((0.3, up(0.3)), (0.3, 0.3), (up(0.3), 0.3), (1.0, fbits(pi4)), (1.0, down(pi4)),
                     (0.5, 0.5), (0.0, 0.0), (-0.0, 0.0), (up(0.0), 0.0), (0.7, 0.0), (fbits(pi4), 0.0),
                     (down(pi4), 0.0)):
        bb = lambda x: x if isinstance(x, int) else fbits(x)  # noqa: E731
        add('bound 13BBB0 %08X %08X' % (bb(f5c), bb(f50)), B, 0x13BBB0, [n, e],
            [(n + 0x2C, h16(2)), (e + 0x5C, bits32(bb(f5c))), (e + 0x50, bits32(bb(f50))), (n + 0x3C, f32b(97.0))],
            queues={0x1C6160: [100]}, run_set=RUN - {0x1C6160})
    for cur, t in ((0, 45.0), (0, up(45.0)), (0, down(45.0)), (4, 21.5), (4, up(21.5)), (4, down(21.5))):
        tb = t if isinstance(t, int) else fbits(t)
        # t = cvt(100) - self +0x3C: the frame chosen so that t lands on tb
        f3c = struct.unpack('<f', struct.pack('<I', tb))[0]
        add('bound 13BBB0 c%d t%08X' % (cur, tb), B, 0x13BBB0, [n, e],
            [(n + 0x2C, h16(cur)), (e + 0x5C, f32b(1.2)), (e + 0x50, f32b(0.2)), (n + 0x3C, f32b(100.0 - f3c))],
            queues={0x1C6160: [100]}, run_set=RUN - {0x1C6160})
    # 0013BF20: e +0x50 <= 0
    for v in (0x00000000, 0x80000000, 0x00000001, 0x80000001):
        add('bound 13BF20 %08X' % v, B, 0x13BF20, [n, e], [(e + 0x50, bits32(v))], queues={0x19AD00: [1]})
    # 0013C4C0: the hit distance <= 5
    for v in around(5.0):
        add('bound 13C4C0 %08X' % v, B, 0x13C4C0, [n, e],
            [(e + 0x80, b8(1)), (e + 0x81, b8(0))], queues={0x19AFE0: [1], 0x1B15D0: [v], 0x19A6F0: [0, 0]},
            run_set=RUN - {0x1B15D0})
    # 0013CD50: slope <= pi/3, y < h, h <= lo - 5, h < hi + 5, |..| <= 25
    seg = FREE + 0x300
    for sl in around(1.0471976):
        add('bound 13CD50 slope %08X' % sl, B, 0x13CD50, [n, seg], [(seg, fw(900.0, 100.0, 960.0, 1.0))],
            queues={0x19AFE0: [2], 0x19BC40: [Scribble(None, [(0x700031E0, w32(2)), (0x700030F0, fw(50.0, 120.0)),
                                                             (0x70003170, h16(0) + h16(1)),
                                                             (0x282250, bits32(sl) + bits32(sl))]),
                                              Scribble(None, table([(99.0, 0, 0.0)]))]},
            spad=[(0x700031D4, w32(HIT))])
    for h1 in around(100.0):
        for h2 in (around(45.0) + around(125.0) + around(75.0)):
            add('bound 13CD50 h %08X %08X' % (h1, h2), B, 0x13CD50, [n, seg], [(seg, fw(900.0, 100.0, 960.0, 1.0))],
                queues={0x19AFE0: [2], 0x19BC40: [Scribble(None, [(0x700031E0, w32(3)), (0x700030F0, fw(50.0)),
                                                                 (0x700030F4, bits32(h1)), (0x700030F8, fw(120.0)),
                                                                 (0x70003170, h16(0) + h16(0) + h16(1)),
                                                                 (0x282250, fw(0.0, 0.0, 0.0))]),
                                                  Scribble(None, [(0x700031E0, w32(1)), (0x700030F0, bits32(h2)),
                                                                  (0x70003170, h16(0))])]},
                spad=[(0x700031D4, w32(HIT))], full=h1 != fbits(100.0))
    # 0013C8C0: 0011E420(the dot) <= 2.3561945 (the dot set by 00102738, stubbed here)
    for dot in (fbits(math.cos(2.3561945)), up(math.cos(2.3561945)), down(math.cos(2.3561945)), fbits(-0.7071068)):
        add('bound 13C8C0 dot %08X' % dot, B, 0x13C8C0, [n, e],
            hit_obj(0x10) + [(e + 0x80, b8(0))],
            queues={0x19AFE0: [1, 0, 0, 0], 0x19A6F0: [0, 0], 0x19AD00: [1, 1], 0x102738: [dot]},
            run_set=RUN - {0x102738}, default={0x122BB8: 0x12345})
    # 00146740: the distance <= 23.5, the side word < 10
    e13, d13 = ACT, ACT + 0x1F0
    for v in around(23.5):
        add('bound 146740 d %08X' % v, D0, 0x146740, [e13, d13], bones(e13) + hit_obj(0x10),
            queues={0x1B15D0: [v], 0x19A6F0: [0, 0]}, run_set=RUN - {0x1B15D0})
    for side in (0, 1):
        for v in around(10.0):
            add('bound 146740 w %d %08X' % (side, v), D0, 0x146740, [e13, d13], bones(e13) + hit_obj(0x10),
                queues={0x1B15D0: [fbits(40.0)], 0x19A6F0: [1, 1], 0x11E748: [v, v]},
                run_set=RUN - {0x1B15D0, 0x11E748}, default={0x122BB8: side << 11})
    # 001437E0: +0x3C <= 110, the rate == 0 / <= 0, +0x8C < 4, +0x8C <= 1
    for v in around(110.0):
        add('bound 1437E0 3c %08X' % v, D0, 0x1437E0, [e13, d13],
            bones(e13) + [(e13 + 6, b8(1)), (e13 + 0x2C, h16(0)), (e13 + 0x3C, bits32(v)), (d13 + 0x54, f32b(0.08)),
                          (e13 + 0x8C, f32b(2.0))])
    for rate in (0x00000000, 0x80000000, 0x00000001, 0x80000001):
        add('bound 1437E0 rate %08X' % rate, D0, 0x1437E0, [e13, d13],
            bones(e13) + [(e13 + 6, b8(1)), (e13 + 0x2C, h16(0)), (e13 + 0x3C, f32b(50.0)), (d13 + 0x54, bits32(rate)),
                          (e13 + 0x8C, f32b(2.0)), (d13 + 0x30, w32(0x1000))])
    for f8c in around(4.0 - 0.08) + (fbits(4.0), up(4.0)):
        add('bound 1437E0 8c up %08X' % f8c, D0, 0x1437E0, [e13, d13],
            bones(e13) + [(e13 + 6, b8(1)), (e13 + 0x2C, h16(0)), (e13 + 0x3C, f32b(50.0)), (d13 + 0x54, f32b(0.08)),
                          (e13 + 0x8C, bits32(f8c)), (d13 + 0x78, b8(1))])
    for f8c in around(1.08):
        add('bound 1437E0 8c dn %08X' % f8c, D0, 0x1437E0, [e13, d13],
            bones(e13) + [(e13 + 6, b8(1)), (e13 + 0x2C, h16(0)), (e13 + 0x3C, f32b(50.0)), (d13 + 0x54, f32b(-0.08)),
                          (e13 + 0x8C, bits32(f8c))])
    # 001B39F0: |a - b| <= 1.5, a < b
    for a, b in ((500.0, 498.5), (500.0, up(498.5)), (500.0, down(498.5)), (1.0, 1.0), (2.0, up(2.0))):
        bb = lambda x: x if isinstance(x, int) else fbits(x)  # noqa: E731
        add('bound 1B39F0 %08X %08X' % (bb(a), bb(b)), D0, 0x1B39F0, [e13, e13 + 0xB0, FREE + 0x100],
            [], queues={0x19A6F0: [1, 1], 0x11E748: [bb(a), bb(b)]}, run_set=RUN - {0x11E748},
            default={0x122BB8: 0x800})
    # 001B3390: the queried value <= f12
    a1, a2 = FREE + 0x400, FREE + 0x500
    for v in around(0.5):
        add('bound 1B3390 %08X' % v, D0, 0x1B3390, [e13, a1, a2, fb(0.5)], [],
            queues={0x19B2C0: [3], 0x19A310: [Scribble(1, [(STACK_TOP - 4, bits32(v))])]})
    # 001A96F0: each box bound
    pa, pb, box = FREE + 0x600, FREE + 0x900, FREE + 0xC00
    for k, (dx, dz, dy) in enumerate(((2.0, 1.0, 1.0), (1.0, 3.0, 1.0), (1.0, 1.0, 4.0))):
        for which in (0, 1, -1):
            vals = [dx, dz, dy]
            vals[k] = struct.unpack('<f', struct.pack('<I', fbits(vals[k]) + which))[0]
            add('bound 1A96F0 %d %d' % (k, which), D0, 0x1A96F0, [pa, pb],
                [(pa + 0x30, w32(box)), (box, fw(2.0, 3.0, 3.0)), (pa + 0xB0, fw(0.0, 0.0, 0.0)),
                 (pb + 0xB0, fw(vals[0], vals[2], vals[1]))])
    # 00179560 +0x38 <= 0; 0017F9E0 the delta < 0; 001821E0 == 0
    for v in (0x00000000, 0x80000000, 0x00000001, 0x80000001):
        add('bound 179560 %08X' % v, A1, 0x179560, [PLAYER], [(PLAYER + 0x38, bits32(v))])
        add('bound 1821E0 %08X' % v, A1, 0x1821E0, [PLAYER],
            [(PLAYER + 0x224, bits32(v)), (PLAYER + 0x22C, bits32(v)), (PLAYER + 0xF, b8(0))])
    for v in (0x00000000, 0x80000000, 0x80000001):
        add('bound 17F9E0 %08X' % v, A1, 0x17F9E0, [PLAYER], [(0x700031D0, w32(FREE + 0xA00))],
            queues={0x1B1470: [fbits(0.5), v, fbits(0.25)]}, run_set=RUN - {0x1B1470})
    # 0011E420: 1 < |x|
    for x in around(1.0) + around(-1.0):
        add('bound 11E420 %08X' % x, D0, 0x11E420, [x], [(0x26C5D0, w32(1))],
            queues={0x11DB90: [1], 0x127758: [fbits(0.125)]})
    # 0x824BE0: the phase + 0.03 <= 1.5
    s = NODE_A
    for v in around(1.47) + (fbits(1.4699999), fbits(1.4700001), 0x3FBC28F5, 0x3FBC28F6):
        add('bound 824BE0 %08X' % v, B, 0x824BE0, [s], [(s + 4, b8(1)), (s + 0xD, b8(0)), (s + 0x1F0, bits32(v))],
            queues={0x1CCF70: [0x55]})
    # 0x825420: 190 <= y <= 200
    for y in around(190.0) + around(200.0):
        add('bound 825420 %08X' % y, B, 0x825420, [R6], bones(R6) + [(R6 + 5, b8(0)), (0x810354, bits32(y))],
            queues={0x1B1EA0: [1]})


def extra_cases(add):
    """Cases for the paths the families above leave out: a speed below 0.4
    in 00138C20, the turn bits kept through 0013C8C0 (a 0013C4C0 hit far
    away), 00146740's side word below 10, 001B2F70's empty and steep
    tables."""
    B = A2
    n, e = CR, CR + 0x1F0
    base = bones(n)
    for st in (0, 1):
        for b80 in (1, 2):
            add('x 138C20 s%d keep %d' % (st, b80), B, 0x138C20, [n, e],
                base + [(n + 0xB0, fw(900.0, 280.0, 950.0, 1.0)), (n + 6, b8(st)), (e + 0x80, b8(b80)),
                        (e + 0x81, b8(0)), (e + 0x44, f32b(0.2)), (FRAME, w32(0x1E781)),
                        (0x700031B0, fw(900.0, 280.0, 1100.0, 1.0))],
                queues={0x19AFE0: [1, 0, 0, 0], 0x19B6C0: [0, 0]})
    e13, d13 = ACT, ACT + 0x1F0
    for side in (0, 1):
        add('x 146740 near side %d' % side, D0, 0x146740, [e13, d13], bones(e13) + hit_obj(0x10),
            queues={0x1B15D0: [fbits(40.0)], 0x19A6F0: [1, 1],
                    0x1028D0: [Scribble(None, [(0x70003600, fw(1.0, 2.0, 2.0))])] * 2},
            run_set=RUN - {0x1B15D0, 0x1028D0}, default={0x122BB8: side << 11})
    for rows in ([], [(80.0, 1, 2.0), (90.0, 1, 0.0)], [(80.0, 1, 0.0), (90.0, 1, 0.0), (95.0, 0, 0.0)]):
        add('x 1B2F70 %d %s' % (len(rows), rows[0][2] if rows else ''), B, 0x1B2F70, [PLAYER + 0xB0, FREE + 0x100],
            [(PLAYER + 0xB4, f32b(85.0))], queues={0x19BC40: [Scribble(None, table(rows))]})


def survivor_cases(add):
    """Cases added because the mutation sweep (docs/LEVEL12_PORT.md section
    3.4) showed the case set missed them; they always run (the default run
    included)."""
    B = A2
    n, e = CR, CR + 0x1F0
    # 00138900 state 3: bit 0x1000 of ent +0x70 alone (other bits set, it clear)
    for f70 in (0xEFFF, 0x1001):
        add('survivor 138900 s3 %X' % f70, B, 0x138900, [n, e],
            bones(n) + [(n + 6, b8(3)), (n + 0x3C, f32b(30.0)), (e + 0x70, w32(f70))])
    # 0011BCF8: an |x| < 0.5 input where one unit in the last place of the
    # q polynomial's third constant shows (found with the EE float model)
    add('survivor 11BCF8 BEDEA086', D0, 0x11BCF8, [0xBEDEA086])
    # 0013CD50: mask 1 with |hi - y| at 25 and either side; mask 0 with |hi -
    # y| beyond 25 and |y - lo| at 25 and either side
    seg = FREE + 0x300
    for hi in around(125.0):
        add('survivor 13CD50 m1 hi %08X' % hi, B, 0x13CD50, [n, seg], [(seg, fw(900.0, 100.0, 960.0, 1.0))],
            queues={0x19AFE0: [2], 0x19BC40: [Scribble(None, [(0x700031E0, w32(2)), (0x700030F0, fw(50.0)),
                                                             (0x700030F4, bits32(hi)), (0x70003170, h16(0) + h16(1)),
                                                             (0x282250, fw(0.0, 0.0))]),
                                              Scribble(None, table([(99.0, 0, 0.0)]))]},
            spad=[(0x700031D4, w32(HIT))])
    for lo in around(75.0):
        add('survivor 13CD50 m0 lo %08X' % lo, B, 0x13CD50, [n, seg], [(seg, fw(900.0, 100.0, 960.0, 1.0))],
            queues={0x19AFE0: [2], 0x19BC40: [Scribble(None, [(0x700031E0, w32(2)), (0x700030F0, bits32(lo)),
                                                             (0x700030F4, fw(200.0)), (0x70003170, h16(0) + h16(1)),
                                                             (0x282250, fw(0.0, 0.0))]),
                                              Scribble(None, table([]))]},
            spad=[(0x700031D4, w32(HIT))])
    # 0013C8C0: 0011E420(the dot) exactly 2.3561945 (0x4016CBE4) and one
    # float either side: the dot -0.7071068 with the square root 0011CB90
    # scripted to the three results that land there (found with the module)
    for s, want in ((0x3EC3EF14, 'eq'), (0x3EC3EF10, 'up'), (0x3EC3EF18, 'dn')):
        add('survivor 13C8C0 ang %s' % want, B, 0x13C8C0, [n, e], hit_obj(0x10) + [(e + 0x80, b8(0))],
            queues={0x19AFE0: [1, 0, 0, 0], 0x19A6F0: [0, 0], 0x19AD00: [1, 1], 0x102738: [0xBF3504F3],
                    0x11CB90: [s]},
            run_set=RUN - {0x102738, 0x11CB90}, default={0x122BB8: 0x12345})

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

# Words the static walk reaches that no input can reach, and why (each
# word's instruction class was checked, never its text reproduced).
DEAD_WORDS = {
    # 0013C8C0: the fall-through after the two compares of the coin
    # ((0x70003B68 + 0x70003B8A) & 1 is 0 or 1): the slot of a branch no
    # coin reaches
    0x13C9BC: 'coin is 0 or 1',
    # 0013CD50: the switch's default (the mask holds bits 0 and 1 only)
    0x13D09C: 'mask in 0..3',
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
    fault pointer and (entries with a result) a NULL result pointer.
    0x825420 on [6] in a19b_02 with +5 = 2 and 0x70003B92 = 0 calls
    00102948, 001C64F0, 001B17A0, 001C68C0 and the +0x4C method."""
    ram, spad = CAPTURES[A2]
    node = R6
    ram = bytearray(ram)
    spad = bytearray(spad)
    ram[node + 5] = 2
    spad[0x3B92] = 0
    problems = []
    calls_want = ['w_00102948', 'w_001C64F0', 'w_001B17A0', 'w_001C68C0', 'w_callback']

    def attempt(null=None, failing=None, unmapped=None, latched=False, entry=0x825420, args=(node, STACK_TOP)):
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
    if (status, address, code) != (-1, 0x1B17A0, 1) or calls != calls_want[:2]:
        problems.append(('NULL hook', status, hex(address), code, calls))
    status, address, code, calls, same = attempt(failing='w_00102948')
    if (status, address, code) != (-1, 0x102948, 2) or calls != calls_want[:1] or not same:
        problems.append(('failing hook', status, hex(address), code, calls, same))
    status, address, code, calls, _ = attempt(unmapped=node + 0x4C)
    if (status, address, code) != (-1, node + 0x4C, 5) or calls != calls_want[:4]:
        problems.append(('unmapped address', status, hex(address), code, calls))
    status, address, code, calls, same = attempt(unmapped=node + 0xB0, entry=0x13D220, args=(node,))
    if (status, address, code) != (-1, node + 0xB0, 5) or calls or not same:
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
    """The fail-stop contract of the header (em_level12_port.h), on one
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
    where each wrapper's own `l12_failed` test is observable. Returns
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
    'a19 824BE0 s0 d0 t0.5 12345678',
    'a19 824BE0 s0 d1 t0.5 12345678',
    'a19 824BE0 s0 d2 t0.5 12345678',
    'a19 824BE0 s1 d1 t1.47 12345678',
    'a19 824BE0 s1 d1 t1.5 12345678',
    'a19 824BE0 s2 d2 t0.5 12345678',
    'a19 824BE0 s3 d2 t0.5 12345678',
    'a19 824BE0 s4 d2 t0.5 12345678',
    'a19 824EF0 s1',
    'a19 824EF0 s4',
    'a19 824F70 s1',
    'a19 824F70 s4',
    'a19 825030 s1',
    'a19 825030 s4',
    'a19 825420 s1 r1 y195 b0',
    'a19 825420 s3 r0 y195 b0',
    'a19 825930 s0 b4',
    'a19 825930 s1 c4 r1',
    'a19 825930 s1 c5 r0',
    'a19 825930 s1 cFF r0',
    'a19 825930 s3',
    'ac 1437E0 s0 0',
    'ac 1437E0 s0 80',
    'ac 1437E0 s1 0 100 -0.08 1.05 0 0 0 1000',
    'ac 1437E0 s1 0 100 0.08 4 0 1 0 0',
    'ac 1437E0 s1 0 100 0.08 4 80 0 0 1000',
    'ac 146740 5 0 -1 000',
    'ac 1B3390 0 0 1 0.5',
    'ac 1B3390 3 0 1 0.5',
    'ac 1B39F0 01 0 100 50',
    'bound 13C4C0 40A00000',
    'bound 146740 d 41BC0000',
    'bound 1A96F0 2 0',
    'bound 1B3390 3F000000',
    'bound 1B3390 3F000001',
    'bound 824BE0 3FBC28F6',
    'bound 824BE0 3FBC28F7',
    'bound 825420 43480000',
    'cr 138900 s0 c0 d0 0.5',
    'cr 138900 s0 c0 d1 0',
    'cr 138900 s0 c0 d1 0.5',
    'cr 138900 s1 0 4',
    'cr 138900 s2 0 near 0',
    'cr 138900 s2 1 near 0',
    'cr 138900 s3 20 1000',
    'cr 138900 s4',
    'cr 138900 sFF',
    'cr 13BA20 h8004 d-0.5 c-1',
    'cr 13BBB0 c0 0 1 t44.5',
    'cr 13BBB0 c0 1.2 0.2 t50',
    'cr 13BBB0 c2 1.2 0.2 t3',
    'cr 13BBB0 c4 0 1 t22.5',
    'cr 13BBB0 c4 0.5 0.5 t30',
    'cr 13BE60 0 1 3',
    'cr 13BF20 0 1 1 0 1',
    'cr 13BF20 1 1 0 0 0',
    'cr 13D220 1 5B',
    'm 11BCF8 3F7FF972',
    'm 11BCF8 A3000000',
    'm 11BCF8 BEFFFFFF',
    'm 11BCF8 BF7FF972',
    'm 11BCF8 BF800000',
    'm 11E420 m-1 C0000000 0 0',
    'm 11E420 m1 40000000 1 22',
    'm 11E420 m2 C0000000 0 0',
    'm 1B1270 1 2',
    'pl 16EF50 s0 f0',
    'pl 16EF50 s0 f1',
    'pl 16EF50 s1 1000',
    'pl 16EF50 s14 f0',
    'pl 16EF50 s14 f1',
    'pl 16EF50 s15 0',
    'pl 16EF50 s16 0',
    'pl 16EF50 s16 1000',
    'pl 16EF50 s2',
    'pl 16EF50 sA gate 0 1 0',
    'pl 16EF50 sB 0 k0 2',
    'pl 16EF50 sB 0 k2 1',
    'pl 16EF50 sB 0 k2 3',
    'pl 16EF50 sB 0 k3 1',
    'pl 16EF50 sB 0 k3 3',
    'pl 16EF50 sB 1 0 39',
    'pl 16EF50 sB 1 1000 38',
    'pl 16EF50 sB 1 1000 39',
    'pl 16EF50 sB gate 0 0 0',
    'pl 179560 0.5',
    'pl 17F9E0 0 1 3',
    'pl 17F9E0 1 0 0',
    'pl 1821E0 1 0 0',
    'pl 1831F0 0',
    'pl 1831F0 1',
    'pl 1831F0 2',
    'st 138C20 floor 0 300 0',
    'st 138C20 floor 7FFF00 300 5',
    'st 138C20 s0 t1 g0 b4 0 0',
    'st 138C20 s0 t1 g1 b0 0 0',
    'st 138C20 s1 t1 g0 b4 0 0',
    'st 138C20 s1 t1 g1 b4 0 0',
    'st 138C20 tail 0 0 0 0 100 77 1',
    'st 138C20 tail 0 0 1 0 000 0 0',
    'st 138C20 tail 1 96 0 1 000 0 1',
    'st 13C1F0 0 B3 6 8 0 (1,)',
    'st 13C1F0 0 B4 0 8 4000 (1,)',
    'st 13C1F0 0 B4 2 8 0 (0, 1)',
    'st 13C4C0 2 2 0 0 0 0',
    'st 13C4C0 5 6 1 9 0 0',
    'st 13C4C0 C 2 1 3 0 0',
    'st 13C4C0 C 4 0 0 0 1',
    'st 13C4C0 C 6 0 0 0 0',
    'st 13C8C0 c0 0 1 0',
    'st 13C8C0 c0 1 0 1',
    'st 13C8C0 c0 1 1 0',
    'st 13C8C0 c3 0 -1 1',
    'st 13C8C0 c3 1 0 1',
    'st 13CD50 r2 0',
    'st 13CD50 r6 1',
    'st 13CD50 t lohi m0',
    'st 13CD50 t near m0b',
    'st 13CD50 t near m1',
    'st 13CD50 t nohi m2',
    'st 13CD50 t none m1',
    'st 13CD50 t none m2',
    'st 13CD50 t steep m0',
    'x 138C20 s0 keep 1',
    'x 138C20 s1 keep 1',
    'x 146740 near side 1',
    'x 1B2F70 0 ',
    'x 1B2F70 2 2.0',
])
SURVIVORS = ('survivor ',)


def select_cases(elf):
    """The mode's case list (EM_TEST_FULL=1: all; the default run keeps
    every capture case, the designed cases of DEFAULT_KEEP and the mutation
    sweep's survivor cases)."""
    cases, captured, targeted = case_list(elf)
    total = len(cases)
    if not reference_mode.FULL and not os.environ.get('EM_LEVEL12_PORT_ALL'):
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
    """An image holding the entry's code (the AREA19 entries need an a19b
    image; boot text is the same in all)."""
    return CAPTURES[A19_BEATS[0]][0]


def main():
    global NATIVE
    elf = read_elf()
    CAPTURES.update(load_captures(elf))
    NATIVE = build(os.environ.get('EM_LEVEL12_PORT_SOURCE'))
    selected, captured, targeted, total = select_cases(elf)
    CASES[:] = selected
    only = os.environ.get('EM_LEVEL12_PORT_ONLY')   # debugging: a label prefix
    if only:
        CASES[:] = selected = [c for c in selected if c[0].startswith(only)]
    results = run_cases(selected)
    failures, per_fn, seen, totals = [], {}, set(), [0, 0, 0, 0]
    header_problems, header_count = header_checks(os.environ.get('EM_LEVEL12_PORT_SOURCE'))
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
    if os.environ.get('EM_LEVEL12_PORT_KEEP'):   # lane tool: the coverage pass that picks DEFAULT_KEEP
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
    if os.environ.get('EM_LEVEL12_PORT_MISSING'):
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
        if os.environ.get('EM_LEVEL12_PORT_NOCONTRACT'):   # debugging only: skips the contract
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
    for label, errors in failures[:int(os.environ.get("EM_LEVEL12_PORT_SHOW", "12"))]:
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
