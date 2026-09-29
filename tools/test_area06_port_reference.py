#!/usr/bin/env python3
"""Execute the ORIGINAL level-7 code (the AREA01 upper floor and AREA06) and
compare em_area06_port*.c.

docs/AREA06_PORT.md. The census deltas of the a01u and a06 routes (decomp
build/s87/census/a01u_delta.json and a06_delta.json, new_functions: 7 and
32 rows) list 39 functions. The ones without a verified port translation
are translated in em_area06_port.c / em_area06_port_node.c /
em_area06_port_overlay.c and are the entries below (FUNCS): 25 census rows
and 00219F50 / 0021A180 / 0021A440 / 002079F0 / 00207BB0 / 00207CA0 /
00207CD0 / 002072A0 / 00123020 / 0019F680 / 001885B0 / 001EA210, which are
also census rows and are called by the others. The calls between them
(00219870 -> 00219F50 / 0021A440 / 0021A180, 00207350 -> 002079F0 /
00207BB0 / 002072A0 / 00123020 / 00207CA0 / 00207CD0, 00169250 ->
0019F680 / 001885B0, 0x823580 -> 001EA210) run as original code at the top
level of the oracle (only other boot functions are hooks), and every
function is also an entry of its own.

The harness is tools/test_area22_port_reference.py (lane A22T, itself the
AREA04 / AREA02 / AREA00 / AREA01 overlay design), reused with the a01u /
a06 captures, this module's hook table and cases, the +0x4C callback of
the AREA04 harness, entries that take the original stack pointer (kind S:
the oracle runs with sp = STACK_TOP, the native entry gets STACK_TOP; the
frame locals live in the compared stack window), 64-bit register
arguments (Q) and the two boot jump tables (001944B0, 00207350), whose
table words the translation encodes as switches (TABLE_BYTES, left out of
the access comparison; load_captures asserts they equal the ELF).

Oracle: the shared EE interpreter with the measured float model (FallEE,
tools/test_player_fall_reference.py) runs the original code resident in the
recorded RAM images of the level-7 route (decomp build/s87/route_a01u/ and
route_a06/ <beat>/, and route_a22/a22_02_progression_exit, the AREA01
upper-floor arrival). AREA06 (overlay id 6) is resident in a01u_02 and
a06_00..a06_05 / a06_s1; AREA01 (id 2) in a22_02, a01u_00, a01u_01,
a01u_s0 and a06_s0; a01u_s1 ends in AREA22 (boot entries only). Before any
case the test checks that the overlay text of every image equals the
user's extract/OVERLAY/AREA06.BIN or AREA01.BIN (text size from the file
header), that the boot text below 0x241000 and the two jump tables equal
the pinned ELF, so every executed instruction is original. Nothing here
embeds original bytes; reports hold counts only.

Callees (every boot function outside this module and the actor's +0x4C
method) are intercepted at their entry and logged with their arguments. A
callee either runs as ORIGINAL code nested inside the oracle (the pure
helpers in RUN), its writes recorded, or is stubbed with the case's
scripted result. The native module runs over a byte copy of the same RAM
with hooks that must be called in the same order with the same arguments;
each hook replays the original callee's writes and result.

Compared, per case (as in the AREA22 harness, docstring there): memory at
the entry of every call before the callee's writes are replayed; the callee
and its arguments; the memory accesses between calls one for one, in order,
by address, size and changed-or-not (the jump-table loads left out); all
memory after the last store; the return value; the store-log self-check;
stops at unmapped or misaligned original accesses; every case again from a
poisoned start image; coverage of every reachable original word; the
fail-stop contract; the table's ctx at every call.

Cases:
  capture   the entries on the captured state of every image they apply to.
  designed  targeted_cases: every state and step, callee results, the
            float boundaries of every compare, scribbling callees.
  perturbed seeded variations (EM_TEST_FULL=1 only).

EM_AREA06_PORT_ONLY=<label prefix> runs a subset (no coverage / contract /
reuse checks). The reuse checks run in full only with EM_TEST_FULL=1; the
default run keeps one smoke sample of them. EM_AREA06_PORT_SOURCE=<dir> tests another copy of the module
sources (the mutation sweep). At most 4 worker processes unless
EM_TEST_JOBS says otherwise.
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
OVERLAY_FILES = {6: DECOMP / 'extract/OVERLAY/AREA06.BIN', 2: DECOMP / 'extract/OVERLAY/AREA01.BIN'}
OUT = ROOT / 'build/area06/port'
MASK = 0xFFFFFFFF
MASK64 = 0xFFFFFFFFFFFFFFFF
RAM_SIZE, SPAD_SIZE = 0x2000000, 0x4000
ARENA = 0x823500
STACK_LO = STACK_TOP - 0x400
STACK_SIZE = STACK_TOP - STACK_LO
BEAT_DIRS = {'a22_02_progression_exit': DECOMP / 'build/s87/route_a22/a22_02_progression_exit'}
for _b in ('a01u_00_gap_jump', 'a01u_01_catwalk_event', 'a01u_02_progression_exit', 'a01u_s0_pickup',
           'a01u_s1_door18_back'):
    BEAT_DIRS[_b] = DECOMP / 'build/s87/route_a01u' / _b
for _b in ('a06_00_beam', 'a06_01_crate_door2', 'a06_02_keypad', 'a06_03_room_out', 'a06_04_beam_collapse',
           'a06_05_door3_locked', 'a06_s0_door1_back', 'a06_s1_bar'):
    BEAT_DIRS[_b] = DECOMP / 'build/s87/route_a06' / _b
BEATS = list(BEAT_DIRS)
# The overlay each image holds (header id at 0x823504).
RESIDENT = {'a22_02_progression_exit': 2, 'a01u_00_gap_jump': 2, 'a01u_01_catwalk_event': 2,
            'a01u_02_progression_exit': 6, 'a01u_s0_pickup': 2, 'a01u_s1_door18_back': 0x13,
            'a06_00_beam': 6, 'a06_01_crate_door2': 6, 'a06_02_keypad': 6, 'a06_03_room_out': 6,
            'a06_04_beam_collapse': 6, 'a06_05_door3_locked': 6, 'a06_s0_door1_back': 2, 'a06_s1_bar': 6}
AREA06_BEATS = [b for b in BEATS if RESIDENT[b] == 6]
AREA01_BEATS = [b for b in BEATS if RESIDENT[b] == 2]
POOL, NODE, NODES = 0x7A5640, 0x2F0, 400
PLAYER = 0x8102B0
CAM = 0x8101E0
EYE = 0x8105D0


class _Route:
    """ROUTE / beat resolves to the beat's folder (three route trees)."""

    def __truediv__(self, beat):
        return BEAT_DIRS[beat]


ROUTE = _Route()

U, I, F, Q, S = 'u', 'i', 'f', 'q', 's'
# (entry, native symbol, byte length of the function body, argument kinds,
# result kind or None). Word arguments go to a0.., floats to f12.., Q is a
# whole 64-bit register, S is the entry stack pointer (native only; the
# oracle's sp is STACK_TOP).
FUNCS = {
    0x123020: ('em_area06_port_00123020', 0x144, [U, U], I),
    0x169250: ('em_area06_port_00169250', 0x450, [U], None),
    0x176180: ('em_area06_port_00176180', 0x158, [U, U, U, S], None),
    0x1885B0: ('em_area06_port_001885B0', 0x1C, [U], I),
    0x1944B0: ('em_area06_port_001944B0', 0x85C, [U, U, I], I),
    0x19F680: ('em_area06_port_0019F680', 0xA8, [U, U, I], I),
    0x1A8F40: ('em_area06_port_001A8F40', 0xBC, [U, U], None),
    0x1EA210: ('em_area06_port_001EA210', 0x28, [F], None),
    0x1EBE10: ('em_area06_port_001EBE10', 0x7C, [U, U], None),
    0x1ECA20: ('em_area06_port_001ECA20', 0xDC, [U, U], None),
    0x2072A0: ('em_area06_port_002072A0', 0x14, [], None),
    0x2072C0: ('em_area06_port_002072C0', 0x88, [U], None),
    0x207350: ('em_area06_port_00207350', 0x69C, [U, S], None),
    0x2079F0: ('em_area06_port_002079F0', 0x1B4, [U, U], None),
    0x207BB0: ('em_area06_port_00207BB0', 0xF0, [U, U, S], None),
    0x207CA0: ('em_area06_port_00207CA0', 0x24, [U], None),
    0x207CD0: ('em_area06_port_00207CD0', 0x24, [U], None),
    0x219870: ('em_area06_port_00219870', 0x6D4, [U, S], None),
    0x219F50: ('em_area06_port_00219F50', 0x224, [U], None),
    0x21A180: ('em_area06_port_0021A180', 0x2BC, [U, S], I),
    0x21A440: ('em_area06_port_0021A440', 0xB8, [U, U], None),
    0x21AE90: ('em_area06_port_0021AE90', 0x2F0, [U], None),
    0x1CE860: ('em_area06_port_001CE860', 0x678, [I, I, U, U, I, Q, F, S], None),
    0x823580: ('em_area06_port_00823580', 0x58C, [U], None),
    0x8242C0: ('em_area06_port_008242C0', 0x80, [U, U], I),
    0x824560: ('em_area06_port_00824560', 0x18BC, [U, S], None),
    0x826950: ('em_area06_port_00826950', 0x244, [U, U, U], I),
}
OVERLAY_OF = {0x823580: 6, 0x8242C0: 6, 0x824560: 6, 0x826950: 2}


def has_result(entry):
    return FUNCS[entry][3] is not None


# Hooks, in the field order of EmArea06PortHooks (after ctx and bytes; the
# +0x4C callback w_callback follows them).
# name -> (original address, [(ctype, register)], result kind or None)
HOOKS = [
    ('w_0011DE90', 0x11DE90, [(F, 12)], F),
    ('w_0011DF78', 0x11DF78, [(F, 12)], F),
    ('w_0011E2A8', 0x11E2A8, [(F, 12)], F),
    ('w_0011E748', 0x11E748, [(F, 12)], F),
    ('w_001026A0', 0x1026A0, [(U, 4), (U, 5), (U, 6)], None),
    ('w_00102738', 0x102738, [(U, 4), (U, 5)], F),
    ('w_00102760', 0x102760, [(U, 4), (U, 5)], None),
    ('w_001028B8', 0x1028B8, [(U, 4), (U, 5), (U, 6)], None),
    ('w_001028D0', 0x1028D0, [(U, 4), (U, 5), (U, 6)], None),
    ('w_00102900', 0x102900, [(U, 4), (U, 5), (F, 12)], None),
    ('w_00102918', 0x102918, [(U, 4), (U, 5), (U, 6)], None),
    ('w_00102948', 0x102948, [(U, 4), (U, 5)], None),
    ('w_00102958', 0x102958, [(U, 4), (U, 5)], None),
    ('w_001029C0', 0x1029C0, [(U, 4)], None),
    ('w_00102BB0', 0x102BB0, [(U, 4), (U, 5), (F, 12)], None),
    ('w_00122BB8', 0x122BB8, [], I),
    ('w_001281C0', 0x1281C0, [(F, 12)], I),
    ('w_001749A0', 0x1749A0, [(U, 4), (I, 5), (I, 6), (F, 12)], None),
    ('w_00182F90', 0x182F90, [(U, 4), (U, 5)], None),
    ('w_0018C4B0', 0x18C4B0, [(U, 4), (F, 12), (F, 13)], None),
    ('w_0018C6A0', 0x18C6A0, [(U, 4), (U, 5), (F, 12)], None),
    ('w_0018D7B0', 0x18D7B0, [(U, 4), (I, 5)], None),
    ('w_00191120', 0x191120, [(I, 4), (F, 12), (F, 13), (F, 14), (F, 15)], F),
    ('w_00194240', 0x194240, [(U, 4)], None),
    ('w_0019A570', 0x19A570, [(U, 4), (U, 5), (I, 6), (I, 7)], I),
    ('w_0019AA80', 0x19AA80, [(U, 4), (U, 5), (I, 6)], I),
    ('w_0019AFE0', 0x19AFE0, [(U, 4), (U, 5), (U, 6), (I, 7)], I),
    ('w_0019C6F0', 0x19C6F0, [(I, 4), (I, 5)], None),
    ('w_001A2370', 0x1A2370, [(U, 4), (U, 5)], None),
    ('w_001AED80', 0x1AED80, [(I, 4)], None),
    ('w_001AEDB0', 0x1AEDB0, [(I, 4), (I, 5), (U, 6)], None),
    ('w_001AF800', 0x1AF800, [(U, 4)], None),
    ('w_001AFC10', 0x1AFC10, [(U, 4)], None),
    ('w_001AFF10', 0x1AFF10, [], U),
    ('w_001AFF90', 0x1AFF90, [(U, 4)], None),
    ('w_001B0000', 0x1B0000, [], None),
    ('w_001B0FD0', 0x1B0FD0, [(U, 4)], I),
    ('w_001B1190', 0x1B1190, [(I, 4)], None),
    ('w_001B11E0', 0x1B11E0, [(I, 4)], I),
    ('w_001B1240', 0x1B1240, [(U, 4), (F, 12), (F, 13)], F),
    ('w_001B12B0', 0x1B12B0, [(F, 12), (F, 13), (F, 14)], F),
    ('w_001B1470', 0x1B1470, [(F, 12)], F),
    ('w_001B1630', 0x1B1630, [(F, 12), (F, 13), (F, 14)], I),
    ('w_001B1B70', 0x1B1B70, [(U, 4)], None),
    ('w_001B1EA0', 0x1B1EA0, [(I, 4), (U, 5), (U, 6), (I, 7)], I),
    ('w_001BA1A0', 0x1BA1A0, [(U, 4), (U, 5)], None),
    ('w_001BA1C0', 0x1BA1C0, [(U, 4), (I, 5)], I),
    ('w_001BA1F0', 0x1BA1F0, [(U, 4)], None),
    ('w_001C5570', 0x1C5570, [(U, 4), (U, 5), (I, 6), (I, 7)], U),
    ('w_001C6380', 0x1C6380, [(U, 4)], None),
    ('w_001CB5B0', 0x1CB5B0, [(I, 4)], None),
    ('w_001CB5F0', 0x1CB5F0, [(U, 4), (I, 5), (I, 6)], U),
    ('w_001CB900', 0x1CB900, [(U, 4), (I, 5), (I, 6)], None),
    ('w_001CC1E0', 0x1CC1E0, [(I, 4), (I, 5), (I, 6), (I, 7), (I, 8), (U, 9), (I, 10)], None),
    ('w_001CCF70', 0x1CCF70, [(U, 4)], I),
    ('w_001CD370', 0x1CD370, [(I, 4)], U),
    ('w_001CD390', 0x1CD390, [(U, 4), (U, 5)], None),
    ('w_001CFA60', 0x1CFA60, [(U, 4), (U, 5), (F, 12), (F, 13)], None),
    ('w_001CFB50', 0x1CFB50, [(U, 4), (I, 5), (I, 6), (F, 12), (F, 13), (F, 14), (F, 15), (F, 16)], None),
    ('w_001CFBE0', 0x1CFBE0, [(I, 4), (I, 5), (U, 6), (U, 7), (I, 8)], None),
    ('w_001D2DE0', 0x1D2DE0, [(I, 4), (I, 5)], None),
    ('w_001EFD20', 0x1EFD20, [(I, 4), (U, 5)], None),
    ('w_001EFEB0', 0x1EFEB0, [(I, 4), (U, 5)], U),
    ('w_001F02C0', 0x1F02C0, [(U, 4), (I, 5), (F, 12)], None),
    ('w_001FABB0', 0x1FABB0, [], None),
    ('w_001FB0B0', 0x1FB0B0, [(I, 4)], None),
    ('w_001FB9F0', 0x1FB9F0, [(I, 4), (I, 5), (I, 6), (I, 7)], None),
    ('w_001FBD50', 0x1FBD50, [(U, 4), (I, 5), (I, 6), (F, 12)], None),
    ('w_00207D00', 0x207D00, [(I, 4), (I, 5)], None),
    ('w_00207E40', 0x207E40, [(I, 4), (I, 5), (I, 6), (I, 7), (I, 8), (U, 9), (Q, 10)], None),
    ('w_0020CD60', 0x20CD60, [], None),
]
BY_ADDRESS = {address: (name, args, result) for name, address, args, result in HOOKS}

# Callees that run as original code inside the oracle (writes replayed to
# the native side). Pure helpers only: none of them reaches hardware.
RUN = {0x11DE90, 0x11DF78, 0x11E2A8, 0x11E748, 0x1026A0, 0x102738, 0x102760, 0x1028B8, 0x1028D0, 0x102900,
       0x102918, 0x102948, 0x102958, 0x1029C0, 0x102BB0, 0x122BB8, 0x1281C0, 0x18C4B0, 0x18C6A0, 0x191120,
       0x1B1240, 0x1B12B0, 0x1B1470, 0x1B1EA0, 0x1BA1A0, 0x1CD370, 0x1CD390, 0x1CFA60}

# The two boot jump tables (runtime address of the jump -> (table, words)):
# 001944B0's camera states 29..37 and 00207350's +5 = 0..8.
JUMP_TABLES = {0x1944F0: (0x26DA50, 9), 0x207430: (0x273530, 9)}
# The only bytes the original loads that the translation does not: those
# tables, which the C switches encode (load_captures asserts they equal the
# ELF in every image). Replay.check_access leaves them out.
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
SOURCES = ('em_area06_port.c', 'em_area06_port_node.c', 'em_area06_port_overlay.c', 'em_area06_port_strip.c')


def build(source_dir=None):
    """Compile the module (or the copies in `source_dir`, a mutation sweep)
    into a private library under build/area06/port."""
    OUT.mkdir(parents=True, exist_ok=True)
    base = Path(source_dir) if source_dir else ROOT / 'src/game'
    stem = 'area06_port' if not source_dir else 'area06_port_%d' % os.getpid()
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
    """Every image must hold the overlay RESIDENT names, with the AREA06 /
    AREA01 text equal to the user's AREA06.BIN / AREA01.BIN; every image's
    boot text and the two jump tables must equal the pinned ELF: every
    instruction the oracle executes is original."""
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
        if oid in overlays:
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
        if kind is not None and s32(oracle.ee.r[2]) != result:
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


SELF_ENTRIES = {0x219870, 0x824560}   # the entries that reach their node's +0x4C method
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

FREE = 0x1C00000          # RAM zero in every capture: the designed records live here
FULL_ONLY = set()         # labels of the cases EM_TEST_FULL=1 adds
G11, BEAM, R9 = 0x7A7690, 0x7AD490, 0x7ACEB0   # AREA06 g[11] (00219870), the beam [11], [9]
PAGE = 0x28B020           # the keypad page record (00207350's; free at every capture end)
D_2821C0 = 0x2821C0
PAD = 0x810E74
F_ONE_B = 0x3F800000


def fw(*values):
    """Little-endian words (floats given as Python floats, ints as ints)."""
    return b''.join(struct.pack('<f', v) if isinstance(v, float) else struct.pack('<I', v & MASK) for v in values)


def owner_nodes(ram, callback):
    return owners(ram, callback)


def case_list(elf):
    rng = random.Random(0xA06)
    cases = []
    count = reference_mode.pick(40, 0)

    def add(label, beat, entry, args, patches=(), spad=(), queues=None, default=None, run_set=RUN, full=False):
        """full=True: the case runs only with EM_TEST_FULL=1 (the default run
        keeps a subset that still reaches every reachable original word)."""
        if full:
            FULL_ONLY.add(label)
        args = [STACK_TOP if k == S else a for k, a in zip(FUNCS[entry][2], list(args) + [STACK_TOP] * 4)]
        cases.append((label, beat, entry, tuple(args), list(patches), list(spad),
                      Script(queues, default), frozenset(run_set)))

    for beat in BEATS:
        ram, spad = CAPTURES[beat]
        if RESIDENT[beat] == 6:
            for node in owners(ram, 0x219870):
                add('capture %s 219870 @%X' % (beat, node), beat, 0x219870, [node])
                add('capture %s 219F50 @%X' % (beat, node), beat, 0x219F50, [node])
                add('capture %s 21A180 @%X' % (beat, node), beat, 0x21A180, [node])
                add('capture %s 21A440 @%X' % (beat, node), beat, 0x21A440, [node, node + 0x1F0])
            for node in owners(ram, 0x823580):
                add('capture %s 823580 @%X' % (beat, node), beat, 0x823580, [node])
            for node in owners(ram, 0x824560):
                add('capture %s 824560 @%X' % (beat, node), beat, 0x824560, [node])
        add('capture %s 169250' % beat, beat, 0x169250, [PLAYER])
        add('capture %s 1944B0' % beat, beat, 0x1944B0, [CAM, PLAYER, 0])
        add('capture %s 1885B0' % beat, beat, 0x1885B0, [PLAYER])
        add('capture %s 176180' % beat, beat, 0x176180, [PLAYER, 0, PLAYER + 0xA0])
        add('capture %s 1EBE10' % beat, beat, 0x1EBE10, [3, 7])
        add('capture %s 1ECA20' % beat, beat, 0x1ECA20, [3, 7])
        add('capture %s 2072A0' % beat, beat, 0x2072A0, [])
        add('capture %s 1EA210' % beat, beat, 0x1EA210, [fb(2.0)])
    captured = len(cases)
    targeted_cases(add)
    targeted = len(cases) - captured
    perturbed_cases(add, rng)
    return cases, captured, targeted


def perturbed_cases(add, rng):
    """Seeded variations (EM_TEST_FULL=1 only): 40 rounds over the node
    g[11], the beam, [9], the keypad page, the player states and the
    camera states, with random states, counters, floats and callee results."""
    rf = lambda lo, hi: struct.pack('<f', rng.uniform(lo, hi))   # noqa: E731
    for i in range(40):
        beat = rng.choice(AREA06_BEATS)
        add('perturbed 219870 %d' % i, beat, 0x219870, [G11],
            [(G11 + 4, b8(rng.choice([0, 1, 2]))), (G11 + 5, b8(rng.choice([0, 1, 2]))),
             (G11 + 0x28, h16(rng.randrange(-2, 12))), (G11 + 0x2A, h16(rng.randrange(0x200))),
             (G11 + 0x2C8, rf(-10, 60)), (G11 + 0x2D8, w32(rng.choice([0, 1]))), (G11 + 0x36, h16(rng.choice([0, 1])))],
            queues={a: [rng.choice([0, 1, -1])] for a in (0x1B0FD0, 0x1B11E0, 0x19AA80, 0x1B1630, 0x19A570)},
            default={0x1C5570: 0x7A5640 + 0x2F0 * 3}, full=True)
        add('perturbed 824560 %d' % i, beat, 0x824560, [BEAM],
            [(BEAM + 4, b8(rng.choice([0, 1, 2, 4]))), (BEAM + 5, b8(rng.choice([0, 1]))),
             (BEAM + 0x2E8, w32(rng.randrange(-1, 7))), (BEAM + 0x2EC, w32(rng.randrange(-52, 32))),
             (BEAM + 0x2E4, w32(rng.randrange(-2, 0x90))), (BEAM + 0x2DC, rf(-3.2, 3.2)),
             (0x810354, rf(40, 70)), (0x8102B5, b8(rng.choice([0, 1, 2, 30, 36]))), (0x810845, b8(rng.choice([0, 0x20])))],
            queues={0x1B1EA0: [rng.choice([0, 1])], 0x1BA1C0: [rng.choice([0, 1])],
                    0x122BB8: [rng.randrange(-0x80000000, 0x80000000) for _ in range(4)]},
            run_set=RUN - {0x122BB8, 0x1B1EA0}, full=True)
        add('perturbed 823580 %d' % i, beat, 0x823580, [R9],
            [(R9 + 4, b8(rng.choice([0, 1]))), (R9 + 5, b8(rng.choice([0, 1]))),
             (R9 + 0x1F0, w32(rng.randrange(-1, 5))), (R9 + 0x1F4, w32(rng.randrange(-1, 3))),
             (R9 + 0x1F8, w32(rng.randrange(-1, 3)))], [(0x70003B68, w32(rng.choice([0, 100, 7])))],
            queues={0x122BB8: [rng.randrange(-0x80000000, 0x80000000) for _ in range(8)]},
            run_set=RUN - {0x122BB8}, full=True)
        add('perturbed 207350 %d' % i, 'a06_02_keypad', 0x207350, [PAGE],
            [(PAGE + 4, bytes([1, rng.choice([0, 0, 1, 4, 5, 8]), rng.choice([0, 1, 2, 2, 2])])),
             (PAGE + 0x28, h16(rng.randrange(0, 11))), (PAGE + 0x2A, h16(rng.randrange(0, 4))),
             (PAGE + 0xA, bytes([rng.randrange(0, 9), rng.choice([0, 1])])),
             (PAD, h16(rng.choice([0x1000, 0x2000, 0x4000, 0x8000, 0x40, 0x20, 0x810, 0x3000]))),
             (D_2821C0, w32(rng.choice([0, 1])))], full=True)
        add('perturbed 1944B0 %d' % i, 'a06_s1_bar', 0x1944B0, [CAM, PLAYER, rng.randrange(0, 8)],
            [(PLAYER + 0x230, w32(rng.randrange(29, 38))), (0x810690, rf(0, 60)), (CAM + 0xC, rf(-50, 50)),
             (CAM + 0x44, rf(-3.1, 3.1)), (PLAYER + 0xC4, rf(-3.1, 3.1)), (0x810700, bytes([rng.choice([6, 8, 0x13]), 1]))],
            full=True)


def targeted_cases(add):
    """Designed cases for every state, step and test; all run by default."""
    b6 = 'a06_02_keypad'
    b1 = 'a01u_01_catwalk_event'
    ram6, spad6 = CAPTURES[b6]
    # ---- 00123020 the string compare (designed strings, no game text)
    a, b = FREE + 0x1000, FREE + 0x1100

    def strings(sa, sb, oa=0, ob=0, label=''):
        add('123020 %s a+%d b+%d' % (label, oa, ob), b6, 0x123020, [a + oa, b + ob],
            [(a + oa, sa), (b + ob, sb)], full=(oa, ob) not in ((0, 0), (8, 8), (0, 8), (1, 1)))
    base = bytes(range(0x31, 0x31 + 40))
    for oa, ob in ((0, 0), (8, 8), (0, 8), (8, 0), (1, 1), (3, 0), (0, 5), (4, 4), (2, 6)):
        strings(b'\0', b'\0', oa, ob, 'empty')
        strings(b'1234\0', b'1234\0', oa, ob, 'same')
        strings(b'1234\0', b'1235\0', oa, ob, 'differ3')
        strings(b'12\0', b'1234\0', oa, ob, 'shorter')
        strings(b'1234\0', b'12\0', oa, ob, 'longer')
        for n in (7, 8, 15, 16, 17, 23, 31, 32, 33, 39):
            strings(base[:n] + b'\0', base[:n] + b'\0', oa, ob, 'same%d' % n)
            for k in (0, n // 2, n - 1):
                other = bytearray(base[:n]); other[k] ^= 0x08
                strings(base[:n] + b'\0', bytes(other) + b'\0', oa, ob, 'diff%d@%d' % (n, k))
        strings(b'\x80a\0', b'\x7fa\0', oa, ob, 'high')
        strings(b'\x7fa\0', b'\xffa\0', oa, ob, 'high2')
        strings(b'ab\0xyz\0', b'ab\0qrs\0', oa, ob, 'tail')
        strings(base[:12] + b'\0' + b'ABC', base[:12] + b'\0' + b'XYZ', oa, ob, 'tail16')
        strings(base[:20] + b'\0' + b'ABC', base[:20] + b'\0' + b'XYZ', oa, ob, 'tail24')
        for n in (15, 16, 17, 31, 32):   # the terminator alone in its chunk, equal non-zero bytes after it
            tail = bytes(range(0x41, 0x41 + 40))
            strings(base[:n] + b'\0' + tail, base[:n] + b'\0' + tail, oa, ob, 'full%d' % n)

    # ---- 001885B0
    for v in (0, 1, 2, 3, 0xFF):
        add('1885B0 +235=%X' % v, b6, 0x1885B0, [PLAYER], [(PLAYER + 0x235, b8(v))])

    # ---- 0019F680 (out, rec, index): the list / table pointers in the scratchpad
    rec, lst, tab = FREE + 0x2000, FREE + 0x2100, FREE + 0x2200
    for n, idx in ((3, 0), (3, 2), (3, 3), (3, -1), (0, 0), (0xFF, 7), (1, 0)):
        p = [(rec + 0x18, b8(n)), (rec + 0x1C, w32(0x40)), (lst - 0x40 + 0, w32(0))]
        p += [(lst + 2 * i, h16([2, 0, 1, 5, -1, 3, 4, 2][i])) for i in range(8)]
        p += [(tab - 12, fw(-1.5, -2.5, -3.5))] + [(tab + 12 * i, fw(1.0 + i, 2.0 + i, 3.0 + i)) for i in range(6)]
        add('19F680 n=%d i=%d' % (n, idx), b6, 0x19F680, [0x700038A0, rec, idx], p,
            [(0x70003204, w32(lst - 0x40)), (0x700031FC, w32(tab))])

    # ---- 001EA210
    for v in (0.0, -1.5, 1e10, -1e10, 2.5, 123456.7):
        add('1EA210 %r' % v, b6, 0x1EA210, [fb(v)])
    add('1EA210 nan', b6, 0x1EA210, [NAN])
    # ---- 002072C0
    rec2 = FREE + 0x2400
    for s in (0, 1, 2, 0xFF):
        add('2072C0 +3=%d' % s, b6, 0x2072C0, [rec2], [(rec2 + 3, b8(s))], queues={0x1AFF10: [FREE + 0x2800]})
    # ---- 00207CA0 / 00207CD0 / 002079F0 / 00207BB0
    tex = FREE + 0x3000
    tp = [(tex, bytes((i * 7 + 3) & 0xFF for i in range(0x100)))]
    add('207CA0', b6, 0x207CA0, [tex], tp)
    add('207CD0', b6, 0x207CD0, [tex], tp)
    for cell in list(range(0, 11)) + [-1, 11]:
        add('2079F0 cell %d' % cell, b6, 0x2079F0, [PAGE, tex], tp + [(PAGE + 0x28, h16(cell))])
    for n in (0, 1, 4, 8, 9, 0xFF):
        add('207BB0 count %d' % n, b6, 0x207BB0, [PAGE, tex], tp + [(PAGE + 0xA, b8(n))])
    for x, y in ((-5, -7), (-0x8000, 0x7FFF), (0x7FFF, -0x8000)):   # designed table entries (signed halfwords)
        add('2079F0 cell 2 at %d,%d' % (x, y), b6, 0x2079F0, [PAGE, tex],
            tp + [(PAGE + 0x28, h16(2)), (0x264FE0 + 8, h16(x) + h16(y))])
    keypad_cases(add, b6)
    node_cases(add)
    strip_cases(add)
    player_cases(add)
    camera_cases(add)
    overlay_cases(add)
    area01_cases(add, b1)
    survivor_cases(add)


def keypad_cases(add, beat):
    ram, _ = CAPTURES[beat]
    info = u32(ram, PAGE + 0x20)
    text = [u32(ram, 0x275858), u32(ram, 0x27585C)]

    def page(label, state, sub=0, step=0, pad=0, cell=0, count=0, slot=0, timer=5, flag=0, more=(),
             typed=None, bit=0, full=False):
        p = [(PAGE + 4, bytes([state, sub, step])), (PAGE + 0x28, h16(cell)), (PAGE + 0x2A, h16(timer)),
             (PAGE + 0xA, bytes([count, slot])), (PAD, h16(pad)), (D_2821C0, w32(flag)), (PAGE + 0x20, w32(info))]
        if typed is not None:
            p.append((PAGE + 0x60, typed.ljust(16, b'\0')))
        add('207350 ' + label, beat, 0x207350, [PAGE], p + list(more), [(0x70003B64, w32(bit))], full=full)
    for info_slot in (4, 5):
        page('init slot %d' % (info_slot - 4), 0, more=[(info + 0x10, b8(info_slot))])
    page('state2', 2)
    page('state3', 3)
    page('state4', 4)
    page('step0', 1, 0, 0)
    page('step1 flag0', 1, 0, 1)
    page('step1 flag1', 1, 0, 1, flag=1)
    page('step3', 1, 0, 3)
    for cell in range(0, 11):
        for pad in (0x1000, 0x4000, 0x2000, 0x8000, 0x1000 | 0x2000, 0x4000 | 0x8000, 0x40, 0x20, 0x810, 0x10, 0x800,
                    0):
            page('pad %04X cell %d' % (pad, cell), 1, 0, 2, pad=pad, cell=cell, count=cell % 9,
                 full=cell not in (0, 2, 3, 5, 6, 8, 9, 10))
    for count in (0, 7, 8, 9):
        page('type count %d' % count, 1, 0, 2, pad=0x40, cell=3, count=count)
        page('delete count %d' % count, 1, 0, 2, pad=0x20, cell=3, count=count)
    page('enter', 1, 0, 2, pad=0x40, cell=10, count=4)
    for cell in (-1, -3, 0x7FFF, -0x8000):   # the cursor halfword as signed
        for pad in (0x1000, 0x4000, 0x2000, 0x8000):
            page('pad %04X cell %d' % (pad, cell), 1, 0, 2, pad=pad, cell=cell)
    # the compare: the typed buffer equal to the string (copied from RAM at run time) or not
    for slot in (0, 1):
        s = bytes(ram[text[slot]:text[slot] + 16]).split(b'\0')[0]
        page('compare right slot %d' % slot, 1, 1, slot=slot, typed=s, count=len(s))
        page('compare wrong slot %d' % slot, 1, 1, slot=slot, typed=s[:-1] + b'0' if s else b'1', count=len(s))
        page('compare short slot %d' % slot, 1, 1, slot=slot, typed=s[:1], count=1)
        page('compare right bit set slot %d' % slot, 1, 1, slot=slot, typed=s, count=len(s),
             more=[(0x810845, b8(0x20))])
    for sub in (2, 3, 6):
        for flag in (0, 1):
            page('sub %d flag %d' % (sub, flag), 1, sub, flag=flag)
    for sub in (4, 5, 7, 8):
        for timer in (1, 2, 0, 0x8000):
            for bit in (0, 0x20):
                page('sub %d timer %d bit %X' % (sub, timer, bit), 1, sub, timer=timer, bit=bit)
    page('sub 9', 1, 9)
    page('sub 0xFF', 1, 0xFF)


def node_cases(add):
    for beat in AREA06_BEATS:
        ram, _ = CAPTURES[beat]
        if G11 not in owners(ram, 0x219870):
            continue
        node = G11
        break
    s = node + 0x1F0
    other = 0x7A5640 + 0x2F0 * 3   # a pool node for the +0x2CC object when it is cleared
    pptr = u32(CAPTURES[beat][0], 0x8102C8)
    # state 0
    for r0, r1 in ((0, 0), (0, 1), (1, 0), (-1, 1)):
        add('219870 s0 %d/%d' % (r0, r1), beat, 0x219870, [node], [(node + 4, b8(0))],
            queues={0x1B0FD0: [r0], 0x1B11E0: [r1], 0x19A570: [0], 0x1C5570: [other]})
    add('219870 s0 hit', beat, 0x219870, [node], [(node + 4, b8(0))],
        queues={0x1B0FD0: [0], 0x1B11E0: [1], 0x19A570: [1], 0x1C5570: [other]})
    # state 1 sub 0
    for c, hit, s36, vis, s5 in ((0, 0, 0, 0, 0), (0, 1, 0, 1, 0), (1, 1, 0, 1, 0), (0, 0, 1, 1, 0), (0, 1, 1, 0, 0),
                                 (1, 0, 0, 1, 0)):
        for t in (0, 0x3F, 0x40, 0x41, 0x7F, 0x80, 0x1C5):
            add('219870 s1/0 c%d hit%d 36=%d vis%d t%X' % (c, hit, s36, vis, t), beat, 0x219870, [node],
                [(node + 4, b8(1)), (node + 5, b8(0)), (pptr, b8(c)), (node + 0x36, h16(s36)), (node + 0x2A, h16(t)),
                 (node + 0x2D8, w32(t & 1))],
                queues={0x19AA80: [hit], 0x1B1630: [vis]})
    for c in (0, 1, 2):
        add('219870 s1/0 visible player %d' % c, beat, 0x219870, [node],
            [(node + 4, b8(1)), (node + 5, b8(0)), (pptr, b8(c))], queues={0x19AA80: [0], 0x1B1630: [1]})
    # state 1 sub 1
    for lim, cnt, head, t in ((20, 6, 2, 0), (20, 7, 2, 8), (20, 6, 1, 8), (0x7FFFFFF0, 2, 2, 0), (-8, 0, 2, 8),
                              (20, 5, 2, 0)):
        for vis, s5 in ((0, 1), (1, 1)):
            add('219870 s1/1 lim %d cnt %d head %d t %d vis %d' % (lim, cnt, head, t, vis), beat, 0x219870, [node],
                [(node + 4, b8(1)), (node + 5, b8(s5)), (node + 0x2C4, w32(lim)), (node + 0x28, h16(cnt)),
                 (node, b8(head)), (node + 0x2A, h16(t)), (node + 0x2D8, w32(1))],
                queues={0x1B1630: [vis]})
    add('219870 s1/5', beat, 0x219870, [node], [(node + 4, b8(1)), (node + 5, b8(5))], queues={0x1B1630: [1]})
    # state 2
    for cnt, k, lim in ((0, 0, 100.0), (2, 0, 100.0), (3, 0, 100.0), (4, 1, 100.0), (6, 7, 100.0), (6, 3, 10.0),
                        (6, 3, 23.99), (6, 3, 24.0), (6, 3, -1.0), (6, 8, 100.0)):
        for obj in (other, 0):
            add('219870 s2 cnt %d k %d lim %r obj %X' % (cnt, k, lim, obj), beat, 0x219870, [node],
                [(node + 4, b8(2)), (node + 0x28, h16(cnt)), (node + 0x2A, h16(k)), (node + 0x2C8, fw(lim)),
                 (node + 0x2CC, w32(obj)), (node + 0x2D4, fw(1.5)), (node + 0x2D0, fw(-2.25))])
    for cnt, k in ((-2, 0), (6, -1), (6, -9), (-0x8000, 3)):   # the counters as signed halfwords
        add('219870 s2 cnt %d k %d' % (cnt, k), beat, 0x219870, [node],
            [(node + 4, b8(2)), (node + 0x28, h16(cnt)), (node + 0x2A, h16(k)), (node + 0x2C8, fw(-100.0)),
             (node + 0x2CC, w32(other))])
    add('219870 s2 cnt 0 k -1 lim -3', beat, 0x219870, [node],
        [(node + 4, b8(2)), (node + 0x28, h16(0)), (node + 0x2A, h16(-1)), (node + 0x2C8, fw(-3.0)),
         (node + 0x2CC, w32(other))])
    for cnt in (-5, -0x8000):
        add('219870 s1/1 cnt %d' % cnt, beat, 0x219870, [node],
            [(node + 4, b8(1)), (node + 5, b8(1)), (node + 0x2C4, w32(20)), (node + 0x28, h16(cnt)), (node, b8(2))],
            queues={0x1B1630: [0]})
    add('219870 s3', beat, 0x219870, [node], [(node + 4, b8(3))])
    add('219870 s7', beat, 0x219870, [node], [(node + 4, b8(7))])
    # 00219F50 / 0021A180 / 0021A440 directly
    for hit in (0, 1):
        add('219F50 hit %d' % hit, beat, 0x219F50, [node], queues={0x19A570: [hit]})
    for st in (0, 1, 2, 3, 0xFFFFFFFF):
        for n, v in ((20, 0.5), (20, 0.05), (20, 0.0), (0, 0.5), (-4, 0.1), (1, 1.0)):
            add('21A180 st %d n %d v %r' % (st & 0xFF, n, v), beat, 0x21A180, [node],
                [(node + 0x22C, w32(st)), (node + 0x2C4, w32(n)), (s + 0x38, fw(v))])
    add('21A440 src free', beat, 0x21A440, [node, FREE + 0x100], [(FREE + 0x100, fw(1.0, 2.0, 3.0, 1.0))])
    # 0021AE90 (a designed node and owner)
    self_, own, bones = FREE + 0x4000, FREE + 0x5000, FREE + 0x6000
    bone_p = [(own + 0x110 + 4 * i, w32(bones + 0x100 * i)) for i in range(40)]
    bone_p += [(bones + 0x100 * i + 0xC0, fw(1.0 * i, -2.0 * i, 0.5 * i, 1.0)) for i in range(40)]
    base = [(self_ + 0x24, w32(own)), (own + 4, b8(1))] + bone_p
    table = [(self_ + 0x1F0 + 8 * i, w32([0, 3, 7, 11, 2, 5][i % 6])) for i in range(16)]
    table += [(self_ + 0x1F4 + 8 * i, w32([1, 4, 9, 12, 6, 0][i % 6])) for i in range(16)]
    add('21AE90 s0', 'a06_05_door3_locked', 0x21AE90, [self_], base + [(self_ + 4, b8(0))],
        queues={0x1EFEB0: [FREE + 0x7000] * 2})
    for k in (0, 1, 4, 8, 12, 15, 16, 17):
        for spawn in (FREE + 0x7000, 0):
            add('21AE90 s1 k %d spawn %X' % (k, spawn), 'a06_05_door3_locked', 0x21AE90, [self_],
                base + table + [(self_ + 4, b8(1)), (self_ + 0x1F0 + 0x80, w32(k)), (self_ + 0x1F0 + 0x84, w32(k))],
                queues={0x1EFEB0: [spawn] * 2})
    add('21AE90 s1 owner gone', 'a06_05_door3_locked', 0x21AE90, [self_],
        base + [(self_ + 4, b8(1)), (own + 4, b8(3))])
    for st in (2, 3, 4):
        add('21AE90 s%d' % st, 'a06_05_door3_locked', 0x21AE90, [self_], base + [(self_ + 4, b8(st))])
    # 001A8F40 (a pair): a +0xB0 / +0x30 -> radius, b +0xB0
    pa, pb, rad = FREE + 0x8000, FREE + 0x8400, FREE + 0x8800
    for d, r in ((0.0, 1.0), (3.9, 1.0), (4.0, 1.0), (4.1, 1.0), (10.0, 7.0), (10.0, 6.99), (-5.0, -8.0)):
        add('1A8F40 d %r r %r' % (d, r), 'a06_s1_bar', 0x1A8F40, [pa, pb],
            [(pa + 0xB0, fw(1.0, 2.0, 3.0, 1.0)), (pb + 0xB0, fw(1.0 + d, 2.0, 3.0, 1.0)), (pa + 0x30, w32(rad)),
             (rad, fw(r))])
    # 00176180
    for beat in ('a06_s1_bar', 'a01u_s0_pickup'):
        for c3, hit, s4, s5 in ((0x3C, 0, 1, 0x1D), (0, 0, 1, 0x1D), (0, 1, 1, 0x1D), (0, 1, 1, 0x1C), (0, 0, 2, 0x1D),
                                (0x3B, 1, 0, 0)):
            add('176180 %s 1F0=%X hit %d %d/%X' % (beat, c3, hit, s4, s5), beat, 0x176180,
                [PLAYER, 0, FREE + 0x100],
                [(PLAYER + 0x1F0, b8(c3)), (PLAYER + 4, bytes([s4, s5])), (FREE + 0x100, fw(5.0, 60.5, -7.0, 1.0))],
                queues={0x19AFE0: [hit]})


def strip_cases(add):
    """001CE860 on designed strips (the point and colour blocks in FREE,
    the packet 001CB5F0 returns in FREE), over the a06 images' matrices."""
    pts, col, pkt = FREE + 0xD000, FREE + 0xD400, FREE + 0xE000
    colour = [(col, fw(128.5, 64.25, -3.0, 255.9))]
    tag = 0x20045D8555422188
    shapes = {
        'line': [(-300.0 + 5 * i, 60.0 + i, -650.0 - 3 * i) for i in range(6)],
        'column': [(-300.0, 50.0 + 10 * i, -650.0) for i in range(6)],
        'far': [(-300.0 + 400 * i, 60.0, -650.0 + 900 * i) for i in range(6)],
        'behind': [(-300.0, 60.0, -300.0 + 200 * i) for i in range(6)],
        'same': [(-300.0, 60.0, -650.0)] * 6,
        'wide': [(-2000.0 + 900 * i, -500.0 + 300 * i, -650.0) for i in range(6)],
        'tall': [(-300.0 + 50 * i, 400.0 + 900 * i, -700.0) for i in range(6)],
        'low': [(-300.0 + 50 * i, -400.0 - 900 * i, -700.0) for i in range(6)],
        'above': [(-880.0, 1000.0, -1200.0), (-830.0, 1000.0, -1200.0), (-300.0, 60.0, -650.0),
                  (-880.0, 1000.0, -1150.0), (-180.0, -200.0, -1200.0), (270.0, -1000.0, -1200.0)],
    }
    for beat in ('a06_00_beam', 'a06_s1_bar'):
        for name, points in shapes.items():
            p = colour + [(pts + 16 * i, fw(x, y, z, 1.0)) for i, (x, y, z) in enumerate(points)]
            for n in (0, 1, 2, 3, 6):
                if n in (0, 1) and name != 'line':
                    continue
                for width in (1.5, 0.25):
                    add('1CE860 %s %s n %d w %r' % (beat, name, n, width), beat, 0x1CE860,
                        [n & 1, 3, pts, col, n, tag, fb(width)], p, default={0x1CB5F0: pkt, 0x1CCF70: 0x1234},
                        full=beat != 'a06_00_beam' or width != 1.5)
    add('1CE860 negative count', 'a06_00_beam', 0x1CE860, [0, 0, pts, col, -1, tag, fb(1.0)], colour,
        default={0x1CB5F0: pkt})


def player_cases(add):
    beat = 'a06_s1_bar'
    bar, lst, tab = FREE + 0x9000, FREE + 0x9100, FREE + 0x9200
    barp = [(bar + 0x18, b8(2)), (bar + 0x1C, w32(0x40)), (lst, h16(0) + h16(1)),
            (tab, fw(-300.0, 70.0, -600.0, -250.0, 70.0, -600.0))]
    spad = [(0x70003204, w32(lst - 0x40)), (0x700031FC, w32(tab))]

    def p(label, st, more=(), area=(6, 0), queues=None):
        add('169250 ' + label, beat, 0x169250, [PLAYER],
            [(PLAYER + 6, b8(st)), (PLAYER + 0x30C, w32(bar)), (0x810700, bytes(area))] + barp + list(more), spad,
            queues=queues)
    for area in ((6, 0), (0x13, 0), (0x13, 1)):
        p('s0 area %X/%d' % area, 0, [(PLAYER + 0x218, fw(1.0)), (PLAYER + 0xC4, fw(1.0))], area)
    for goal, cur in ((1.0, 1.0), (1.0, 0.5), (1.0, 1.3), (-3.0, 3.0), (0.2, 0.0)):
        for tx, tz in ((-290.0, -600.0), (-280.0, -600.0), (-277.75, -600.0), (-270.0, -603.0), (-260.0, -610.0)):
            p('s1 %r/%r t %r,%r' % (goal, cur, tx, tz), 1,
              [(PLAYER + 0x218, fw(goal)), (PLAYER + 0xC4, fw(cur)), (PLAYER + 0x290, fw(tx)), (PLAYER + 0x298, fw(tz)),
               (PLAYER + 0xB0, fw(-275.0)), (PLAYER + 0xB8, fw(-600.0)), (PLAYER + 0x254, fw(75.0)),
               (PLAYER + 0xB4, fw(60.0)), (PLAYER + 0x2E0, fw(0.5)), (PLAYER + 0x2E8, fw(-0.25))])
    for n in (1, 2, 3, 4, 5):   # the bar length decides n's parity
        p('s1 bar %d' % n, 1, [(PLAYER + 0x218, fw(1.0)), (PLAYER + 0xC4, fw(1.0)),
                               (tab + 12, fw(-300.0 + 4.5 * n, 70.0, -600.0))])
    for bit in (0, 0x1000, 0xFFFF):
        p('s2 %X' % bit, 2, [(PLAYER + 0x200, w32(bit))])
        p('s4 %X' % bit, 4, [(PLAYER + 0x200, w32(bit)), (PLAYER + 0x235, b8(bit & 1))])
    for cnt in (0, 1, 8, -1):
        p('s3 %d' % cnt, 3, [(PLAYER + 0x28, h16(cnt))])
    p('s5', 5)


def camera_cases(add):
    beat = 'a06_s1_bar'
    for state in range(27, 40):
        for idx in (0, 3, 6, 7, 2, 8):
            for area in ((6, 0), (8, 0), (0x13, 1), (0x13, 0)):
                if state not in (36, 37) and area != (6, 0):
                    continue
                add('1944B0 st %d idx %d area %X/%d' % (state, idx, area[0], area[1]), beat, 0x1944B0,
                    [CAM, PLAYER, idx], [(PLAYER + 0x230, w32(state)), (0x810700, bytes(area))],
                    full=idx in (2, 8))
    for dist, c, m64 in ((100.0, 40.0, -46.8), (10.0, 40.0, -46.8), (10.0, 35.0, -46.8), (10.0, 29.0, 0.0),
                         (10.0, 31.0, 0.0), (10.0, 29.9, -46.8), (10.0, 40.0, 0.0), (10.0, -45.0, 0.0),
                         (10.0, 10.0, 0.0)):
        add('1944B0 approach %r %r %r' % (dist, c, m64), beat, 0x1944B0, [CAM, PLAYER, 1],
            [(PLAYER + 0x230, w32(32)), (0x810690, fw(dist)), (CAM + 0xC, fw(c)), (CAM + 0x64, fw(m64))])
    for state, area in ((36, (8, 0)), (37, (0x13, 1))):
        for dist, c, m64 in ((100.0, 40.0, -46.8), (10.0, 35.0, -46.8), (10.0, 31.0, 0.0), (10.0, 29.0, 0.0),
                             (10.0, 40.0, 0.0)):
            add('1944B0 approach %d %r %r %r' % (state, dist, c, m64), beat, 0x1944B0, [CAM, PLAYER, 2],
                [(PLAYER + 0x230, w32(state)), (0x810700, bytes(area)), (0x810690, fw(dist)), (CAM + 0xC, fw(c)),
                 (CAM + 0x64, fw(m64))])
    for facing in (0.0, 1.0, 1.5707964, 1.5707963, 2.0, -2.5, 3.1):
        for yaw in (0.0, 0.3, -1.0):
            add('1944B0 st30 facing %r yaw %r' % (facing, yaw), beat, 0x1944B0, [CAM, PLAYER, 1],
                [(PLAYER + 0x230, w32(30)), (PLAYER + 0xC4, fw(facing)), (CAM + 0x44, fw(yaw))])
    for x, z, idx in ((-400.0, 0.0, 0), (-300.0, 0.0, 0), (700.0, 0.0, 3), (800.0, 0.0, 3), (0.0, 900.0, 6),
                      (0.0, 1100.0, 6)):
        add('1944B0 clamp %r %r %d' % (x, z, idx), beat, 0x1944B0, [CAM, PLAYER, idx],
            [(PLAYER + 0x230, w32(31)), (CAM + 0x44, fw(0.0)), (0x8105E0, fw(x)), (0x8105E8, fw(z)),
             (CAM + 0xC, fw(1.0)), (CAM + 0x94, fw(0.0))], queues={0x191120: [fb(0.5)]},
            run_set=RUN - {0x191120})
    for yaw in (0.25, 0.5):
        add('1944B0 st31 same %r' % yaw, beat, 0x1944B0, [CAM, PLAYER, 1],
            [(PLAYER + 0x230, w32(31)), (CAM + 0x44, fw(yaw))], queues={0x191120: [fb(0.5)]},
            run_set=RUN - {0x191120})


def overlay_cases(add):
    beat = 'a06_01_crate_door2'
    ram, _ = CAPTURES[beat]
    node = R9
    w = node + 0x1F0
    nr = RUN - {0x122BB8}
    for st in (0, 2, 3, 4, 0xFF):
        add('823580 st %d' % st, beat, 0x823580, [node], [(node + 4, b8(st))],
            queues={0x122BB8: [7, -3, 11, 5, 2, 9, 4, 6]}, run_set=nr)
    for r in (-3, -2, 3, 0x7FFFFFFF, -0x80000000):   # rand % 2 and % 5 on negative and extreme values
        add('823580 st 0 r %d' % r, beat, 0x823580, [node], [(node + 4, b8(0))],
            queues={0x122BB8: [r, r, r, r]}, run_set=nr)
        add('823580 sub 1 w1 r %d' % r, beat, 0x823580, [node],
            [(node + 4, b8(1)), (node + 5, b8(1)), (w, w32(5)), (w + 4, w32(0)), (w + 8, w32(5))],
            [(0x70003B68, w32(1))], queues={0x122BB8: [r] * 6}, run_set=nr)
    for sub in (0, 1, 2):
        for w0, w1, w2 in ((5, 5, 5), (0, 5, 0), (5, 0, 5), (0, 0, 0), (3, 0, 3), (6, 0, 6)):
            for tick in (0, 100, 37, -200):
                for r in ((0, 1, 2, 3), (5, -7, 9, -4), (-8, 6, 1, 2)):
                    add('823580 sub %d w %d/%d/%d tick %d r %s' % (sub, w0, w1, w2, tick, r), beat, 0x823580, [node],
                        [(node + 4, b8(1)), (node + 5, b8(sub)), (w, w32(w0)), (w + 4, w32(w1)), (w + 8, w32(w2))],
                        [(0x70003B68, w32(tick))], queues={0x122BB8: list(r) * 3}, run_set=nr,
                        default={0x1EFEB0: FREE + 0xA000}, full=tick not in (0, 37))
    for spawn in (0, FREE + 0xA000):
        add('823580 spawn %X' % spawn, beat, 0x823580, [node],
            [(node + 4, b8(1)), (node + 5, b8(1)), (w, w32(3)), (w + 4, w32(0)), (w + 8, w32(0))],
            [(0x70003B68, w32(200))], queues={0x1EFEB0: [spawn, spawn]})
    # 0x8242C0 (self, st)
    st = FREE + 0xB000
    for s in (0, 1, 2):
        for bit in (0, 0x20, 0xDF):
            add('8242C0 st %d bit %X' % (s, bit), beat, 0x8242C0, [node, st], [(st + 4, b8(s)), (0x810845, b8(bit))])
    beam_cases(add)


def beam_cases(add_case):
    node = BEAM
    for beat in ('a01u_02_progression_exit', 'a06_00_beam', 'a06_02_keypad', 'a06_04_beam_collapse', 'a06_s1_bar'):
        ram, _ = CAPTURES[beat]
        quick = beat == 'a06_00_beam'

        def add(*a, _full=not quick, **k):
            add_case(*a, full=_full, **k)
        nr = RUN - {0x122BB8, 0x1B1EA0}
        # state 0
        for hit in (0, 1):
            for bit in (0, 0x20):
                add('824560 %s s0 hit %d bit %X' % (beat, hit, bit), beat, 0x824560, [node],
                    [(node + 4, b8(0)), (0x810845, b8(bit))], queues={0x1BA1C0: [hit]})
        # state 4
        for run_, ph in ((1, 0.0), (1, 3.1), (1, 3.1415927), (1, 3.08), (0, 0.0)):
            for y, inside in ((60.0, 1), (54.9, 1), (55.0, 1), (60.0, 0), (60.0, 2)):
                for tmr in (1, 2, 0x40, 0x41, 0x80):
                    for r in (0, 0x7FFF0000, 0x40000000, -0x40000000):
                        if (y, inside) != (60.0, 1) and (tmr, r) != (0x40, 0x40000000):
                            continue
                        add('824560 %s s4 run %d ph %r y %r in %d tmr %X r %X' % (beat, run_, ph, y, inside, tmr, r & MASK),
                            beat, 0x824560, [node],
                            [(node + 4, b8(4)), (node + 0x2EC, w32(run_)), (node + 0x2DC, fw(ph)),
                             (node + 0x2E4, w32(tmr)), (0x810354, fw(y)), (0x810845, b8(0))],
                            queues={0x1B1EA0: [inside], 0x122BB8: [r, 0x12345678]}, run_set=nr)
        add('824560 %s s4 bit' % beat, beat, 0x824560, [node], [(node + 4, b8(4)), (0x810845, b8(0x20))])
        # state 1 sub 0
        for y, inside, b9, mode, yaw in ((60.0, 1, 0, 0, 1.0), (60.0, 1, 0, 1, -1.0), (60.0, 1, 0, 2, 1.0),
                                         (60.0, 1, 0, 29, 0.0), (60.0, 1, 0, 34, -0.0), (60.0, 1, 0, 35, 1.0),
                                         (60.0, 1, 0, 28, 1.0), (60.0, 1, 1, 0, 1.0), (60.0, 0, 0, 0, 1.0),
                                         (54.0, 1, 0, 0, 1.0), (55.0, 1, 0, 0, 1.0), (60.0, 1, 0, 0xFF, 1.0)):
            add('824560 %s s1/0 y %r in %d b9 %d mode %d yaw %r' % (beat, y, inside, b9, mode, yaw), beat, 0x824560,
                [node], [(node + 4, b8(1)), (node + 5, b8(0)), (0x810354, fw(y)), (0x8106B9, b8(b9)),
                         (0x8102B5, b8(mode)), (0x810374, fw(yaw))], queues={0x1B1EA0: [inside]}, run_set=nr)
        # state 1 sub 1: the shake
        for cnt, tmr, ph in ((6, 18, 0.0), (6, 1, 3.1), (1, 1, 0.5), (2, 1, 0.5), (2, 0x41, 0.5), (3, 0x41, 0.5),
                             (1, 0x41, 3.05), (6, 0x40, 1.0), (0, 1, 0.0), (-1, 0x41, 0.0)):
            add('824560 %s s1/1 shake cnt %d tmr %X ph %r' % (beat, cnt, tmr, ph), beat, 0x824560, [node],
                [(node + 4, b8(1)), (node + 5, b8(1)), (node + 0x2E8, w32(cnt)), (node + 0x2E4, w32(tmr)),
                 (node + 0x2DC, fw(ph)), (node + 0x2E0, fw(0.006)), (node + 0x2D8, fw(60.0))],
                queues={0x122BB8: [0x7FFF1234, -0x10000]}, run_set=nr)
        # the lift and the fall
        for v in (30, 1, 2, 31, 0, -50, -31, -30, -26, -25, -24, -21, -20, -15, -14, -4, -3, -2, -1):
            add('824560 %s s1/1 lift %d' % (beat, v), beat, 0x824560, [node],
                [(node + 4, b8(1)), (node + 5, b8(1)), (node + 0x2E8, w32(0)), (node + 0x2EC, w32(v)),
                 (node + 0x2E0, fw(0.6)), (node + 0x2D8, fw(60.0))])
        add('824560 %s s1/2' % beat, beat, 0x824560, [node], [(node + 4, b8(1)), (node + 5, b8(2))])
        rec = u32(ram, u32(ram, 0x24D7C0 + 4 * ram[0x810700])) + ram[node + 0x9A] * 0x28
        for v in (30, 5, -40, -26, -4, -31, -21, -15):   # a tilted record, small positions and speeds
            for small in (0.0, 0.04, -0.001):
                add('824560 %s s1/1 lift %d rec20 small %r' % (beat, v, small), beat, 0x824560, [node],
                    [(node + 4, b8(1)), (node + 5, b8(1)), (node + 0x2E8, w32(0)), (node + 0x2EC, w32(v)),
                     (node + 0x2E0, fw(small)), (node + 0xB0, fw(35.0 + small)), (node + 0xB4, fw(small)),
                     (node + 0xC8, fw(small)), (rec + 0x20, fw(0.1 + small))])
        add('824560 %s s4 bit0' % beat, beat, 0x824560, [node], [(node + 4, b8(4)), (0x810845, b8(0x01))])
        add('824560 %s s0 sub 1' % beat, beat, 0x824560, [node], [(node + 4, b8(0)), (0x810701, b8(1))],
            queues={0x1BA1C0: [1]})
        # state 2
        for sub in (0, 1, 2):
            for d0 in (0, 1):
                add('824560 %s s2/%d 2D0=%d' % (beat, sub, d0), beat, 0x824560, [node],
                    [(node + 4, b8(2)), (node + 5, b8(sub)), (node + 0x2D0, w32(d0))])
        for st in (3, 5, 0xFF):
            add('824560 %s s%d' % (beat, st), beat, 0x824560, [node], [(node + 4, b8(st))])


def area01_cases(add, beat):
    self_, st, prm = 0x7A5640, FREE + 0xC000, FREE + 0xC100
    base = [(prm + 0x30, fw(12.0, 60.0, -665.0, 1.0)), (prm + 0xC, fw(60.0))]
    for s in (0, 3):
        add('826950 st %d' % s, beat, 0x826950, [self_, st, prm], base + [(st + 4, b8(s))])
    for yaw in (0.0, 1.0, 3.0):
        add('826950 st 1 yaw %r' % yaw, beat, 0x826950, [self_, st, prm],
            base + [(st + 4, b8(1)), (PLAYER + 0xC4, fw(yaw)), (prm + 0x10, fw(5.0))])
    add('826950 st 1 same', beat, 0x826950, [self_, st, prm],
        base + [(st + 4, b8(1)), (prm + 0x10, fw(5.0))], queues={0x1B12B0: [fb(0.75)], 0x1B1240: [fb(0.75)]},
        run_set=RUN - {0x1B12B0, 0x1B1240})
    for n in (0.0, 19.0, 20.0, 39.0, 40.0, 55.0, 58.0, 59.0, 60.0, 61.0):
        add('826950 st 2 n %r' % n, beat, 0x826950, [self_, st, prm],
            base + [(st + 4, b8(2)), (prm + 0x10, fw(n)), (prm + 0x20, fw(1.0, 2.0, 3.0, 1.0))])


def survivor_cases(add):
    """Designed killing inputs for the value-domain survivors of the round-7
    review's bounded sweep (its probes P1-P3, P5-P8, P10) and for 001CE860's
    clip compare. Each runs against the original like every other case and
    in the default run; docs/AREA06_PORT.md section 3.5 names the mutant
    each one kills."""
    b6 = 'a06_02_keypad'
    # 0019F680: index-list entry -1, so the row halfword (signed) reads the
    # table 12 bytes below its base.
    rec, lst, tab = FREE + 0x2000, FREE + 0x2100, FREE + 0x2200
    p = [(rec + 0x18, b8(0xFF)), (rec + 0x1C, w32(0x40))]
    p += [(lst + 2 * i, h16([2, 0, 1, 5, -1, 3, 4, 2][i])) for i in range(8)]
    p += [(tab - 12, fw(-1.5, -2.5, -3.5))] + [(tab + 12 * i, fw(1.0 + i, 2.0 + i, 3.0 + i)) for i in range(6)]
    add('19F680 survivor row -1', b6, 0x19F680, [0x700038A0, rec, 4], p,
        [(0x70003204, w32(lst - 0x40)), (0x700031FC, w32(tab))])
    # 00123020: a byte >= 0x80 in b where a ends (the difference is unsigned).
    a, b = FREE + 0x1000, FREE + 0x1100
    for oa, ob in ((1, 1), (0, 0), (0, 8)):
        add('123020 survivor empty vs high a+%d b+%d' % (oa, ob), b6, 0x123020, [a + oa, b + ob],
            [(a + oa, b'\0'), (b + ob, b'\x80\0')])
        add('123020 survivor a vs a-high a+%d b+%d' % (oa, ob), b6, 0x123020, [a + oa, b + ob],
            [(a + oa, b'a\0'), (b + ob, b'a\xf0\0')])
    # 00207350: typed count 0x80 (the count byte is compared unsigned).
    info = u32(CAPTURES[b6][0], PAGE + 0x20)
    add('207350 survivor typed count 0x80', b6, 0x207350, [PAGE],
        [(PAGE + 4, bytes([1, 0, 2])), (PAGE + 0x28, h16(3)), (PAGE + 0x2A, h16(5)), (PAGE + 0xA, bytes([0x80, 0])),
         (PAD, h16(0x40)), (D_2821C0, w32(0)), (PAGE + 0x20, w32(info))], [(0x70003B64, w32(0))])
    # 00219870 state 1 sub 1: +0x2C4 = -13, so (lim + 10) >> 2 is -1 (an
    # arithmetic shift, not a division), with the counter at -2.
    beat = next(bb for bb in AREA06_BEATS if G11 in owners(CAPTURES[bb][0], 0x219870))
    add('219870 survivor s1/1 lim -13 cnt -2', beat, 0x219870, [G11],
        [(G11 + 4, b8(1)), (G11 + 5, b8(1)), (G11 + 0x2C4, w32(-13)), (G11 + 0x28, h16(-2)), (G11, b8(2)),
         (G11 + 0x2A, h16(0)), (G11 + 0x2D8, w32(1))], queues={0x1B1630: [0]})
    # 0021AE90 state 0: negative rand results (the bone index is taken from
    # the signed word).
    self_, own, bones = FREE + 0x4000, FREE + 0x5000, FREE + 0x6000
    bone_p = [(own + 0x110 + 4 * i, w32(bones + 0x100 * i)) for i in range(40)]
    bone_p += [(bones + 0x100 * i + 0xC0, fw(1.0 * i, -2.0 * i, 0.5 * i, 1.0)) for i in range(40)]
    add('21AE90 survivor s0 rand negative', 'a06_05_door3_locked', 0x21AE90, [self_],
        [(self_ + 0x24, w32(own)), (own + 4, b8(1))] + bone_p + [(self_ + 4, b8(0))],
        queues={0x122BB8: [-0x10000 * (k + 1) for k in range(32)], 0x1EFEB0: [FREE + 0x7000] * 2},
        run_set=RUN - {0x122BB8})
    # 00169250 state 1: goal -0.0, the turn step returns +0.0 (the EE float
    # compare calls them equal; a bitwise compare would not).
    bar, lst2, tab2 = FREE + 0x9000, FREE + 0x9100, FREE + 0x9200
    barp = [(bar + 0x18, b8(2)), (bar + 0x1C, w32(0x40)), (lst2, h16(0) + h16(1)),
            (tab2, fw(-300.0, 70.0, -600.0, -250.0, 70.0, -600.0))]
    add('169250 survivor s1 goal -0 turn +0', 'a06_s1_bar', 0x169250, [PLAYER],
        [(PLAYER + 6, b8(1)), (PLAYER + 0x30C, w32(bar)), (0x810700, bytes((6, 0))),
         (PLAYER + 0x218, w32(0x80000000)), (PLAYER + 0xC4, fw(0.0))] + barp,
        [(0x70003204, w32(lst2 - 0x40)), (0x700031FC, w32(tab2))],
        queues={0x1B12B0: [0]}, run_set=RUN - {0x1B12B0})
    # 0x823580 sub 1: w0 = -3 (w0 % 3 == 0 taken signed) with an even rand.
    w = R9 + 0x1F0
    add('823580 survivor sub 1 w0 -3', 'a06_01_crate_door2', 0x823580, [R9],
        [(R9 + 4, b8(1)), (R9 + 5, b8(1)), (w, w32(-3)), (w + 4, w32(5)), (w + 8, w32(5))],
        [(0x70003B68, w32(1))], queues={0x122BB8: [2, 2, 2, 2]}, run_set=RUN - {0x122BB8})
    # 0x824560 state 4: dust timer 33 (32 after the decrement; & 0x3F is
    # nonzero, & 0x1F would be zero).
    add('824560 survivor s4 timer 33', 'a06_00_beam', 0x824560, [BEAM],
        [(BEAM + 4, b8(4)), (BEAM + 0x2EC, w32(1)), (BEAM + 0x2E4, w32(33)), (BEAM + 0x2DC, fw(0.5)),
         (0x810845, b8(0))], queues={0x122BB8: [0x40000000, 0x40000000]}, run_set=RUN - {0x122BB8})
    # 001CE860: clip-space x, y or z exactly equal to |w|, with w's lowest
    # mantissa bit set. The cull matrix 001CD370(0) (the render context +
    # 0x2240; 001CD370 still runs as original) is patched to the identity
    # with row 3 = (0, 0, 0, 1.0000001), so the clip coordinates are the
    # point's x, y, z and w = 0x3F800001 exactly. The original sets no clip
    # flag at equality; a >= compare, or w with its low bit cleared, would.
    beat = 'a06_00_beam'
    ram = CAPTURES[beat][0]
    kmat = u32(ram, 0x275670) + 0x2240
    one, one_up, one_up2, neg_up = 0x3F800000, 0x3F800001, 0x3F800002, 0xBF800001
    matrix = fw(one, 0, 0, 0, 0, one, 0, 0, 0, 0, one, 0, 0, 0, 0, one_up)
    pts, col, pkt = FREE + 0xD000, FREE + 0xD400, FREE + 0xE000
    points = [(one_up, 0.5, 0.25), (0.5, neg_up, 0.25), (0.25, 0.5, one_up), (one_up2, 0.5, 0.25),
              (0.5, 0.5, 0.5), (one_up, neg_up, one_up)]
    p = [(kmat, matrix), (col, fw(128.5, 64.25, -3.0, 255.9))]
    p += [(pts + 16 * i, fw(x, y, z, 1.0)) for i, (x, y, z) in enumerate(points)]
    add('1CE860 survivor clip |x| == |w|', beat, 0x1CE860, [0, 3, pts, col, 6, 0x20045D8555422188, fb(1.5)], p,
        default={0x1CB5F0: pkt, 0x1CCF70: 0x1234})


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
    unmapped address and a fault latched before the call (0021A440 on
    AREA06's g[11]: 00102948, 001028D0, 0011E748, 001028D0, 0011E748; and
    002072C0 for an unmapped address before any call), then every entry
    with a latched fault, a NULL hook table, a NULL fault pointer and
    (entries with a result) a NULL result pointer."""
    ram, spad = CAPTURES['a06_02_keypad']
    node = G11
    problems = []
    calls_want = ['w_00102948', 'w_001028D0', 'w_0011E748', 'w_001028D0', 'w_0011E748']

    def attempt(null=None, failing=None, unmapped=None, latched=False, entry=0x21A440, args=(node, node + 0x1F0)):
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
        status = getattr(NATIVE, FUNCS[entry][0])(C.byref(hooks), *native_args(entry, args), C.byref(fault))
        same = before == C.string_at(base, RAM_SIZE) + C.string_at(sbase, SPAD_SIZE)
        return status, fault.address, fault.code, calls, same

    status, address, code, calls, _ = attempt()
    if (status, address, code) != (0, 0, 0) or calls != calls_want:
        problems.append(('fault-check baseline', status, hex(address), code, calls))
    status, address, code, calls, _ = attempt(null='w_0011E748')
    if (status, address, code) != (-1, 0x11E748, 1) or calls != calls_want[:2]:
        problems.append(('NULL hook', status, hex(address), code, calls))
    status, address, code, calls, same = attempt(failing='w_00102948')
    if (status, address, code) != (-1, 0x102948, 2) or calls != calls_want[:1] or not same:
        problems.append(('failing hook', status, hex(address), code, calls, same))
    status, address, code, calls, _ = attempt(unmapped=node + 0x1F0 + 0x30)
    if (status, address, code) != (-1, node + 0x220, 5) or calls != calls_want[:3]:
        problems.append(('unmapped address', status, hex(address), code, calls))
    status, address, code, calls, same = attempt(unmapped=FREE + 3, entry=0x2072C0, args=(FREE,))
    if (status, address, code) != (-1, FREE + 3, 5) or calls or not same:
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
             if results[index][1] in ((0x826950, 0x176180, 0x1A8F40, 0x8242C0, 0x2072C0) if reference_mode.FULL
                                      else (0x826950,))]
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
    """The fail-stop contract of the header (em_area06_port.h), on one
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
    every capture and designed case)."""
    cases, captured, targeted = case_list(elf)
    fixed = captured + targeted
    total = len(cases)
    if not reference_mode.FULL:
        cases = [c for c in cases[:fixed] if c[0] not in FULL_ONLY]
        return cases, captured, len(cases) - captured, total
    return cases, captured, targeted, total


def image_for(entry):
    oid = OVERLAY_OF.get(entry)
    if oid == 2:
        return CAPTURES[AREA01_BEATS[0]][0]
    return CAPTURES[AREA06_BEATS[0]][0]


def main():
    global NATIVE
    elf = read_elf()
    CAPTURES.update(load_captures(elf))
    NATIVE = build(os.environ.get('EM_AREA06_PORT_SOURCE'))
    selected, captured, targeted, total = select_cases(elf)
    CASES[:] = selected
    only = os.environ.get('EM_AREA06_PORT_ONLY')   # debugging: a label prefix
    if only:
        CASES[:] = selected = [c for c in selected if c[0].startswith(only)]
    results = run_cases(selected)
    failures, per_fn, seen, totals = [], {}, set(), [0, 0, 0, 0]
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
    if os.environ.get('EM_AREA06_PORT_MISSING'):
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
# reuse_checks: the census rows with an existing verified translation
# ----------------------------------------------------------------------------
# Each check drives the existing translation against the original
# instructions with that module's own oracle harness (imported, not
# modified), over the a06 captures instead of the AREA11 ones; the libraries
# are built into build/area06/port/reuse (the harnesses' own build folders
# are not touched):
#   0021A500  em_gun_rest_0021A500 (tools/test_security_gun_rest_reference.py
#             compare_run): every live 0x8000003B strip node of the AREA06
#             images, as captured, ticked to its end (freed) or 40 ticks;
#   00169730, 001696A0, 001811F0, 00181430, 001814E0, 00181B80, 00181BA0,
#             00181D70  em_player_closure1019_* (tools/test_player_closure_
#             10_12_19_reference.py run_batch): its seeded state cases (the
#             +5 = 0x10 / 0x12 / 0x19 / 0x1A routines) over the a06_s1 and
#             a06_00 RAM, the player record taken from each capture;
#   001FD0E0, 001FDDB0  em_cs_001FD0E0 with em_mpr_001FDDB0 (tools/test_
#             message_presenter_rest_reference.py section_c): the real cue
#             lines 0 / 2 / 4 / 6 of the message block 0x2821B0 (the keypad
#             page's dialog lines) frame by frame over the a06_02 RAM;
#   001AFF90  em_status_scene_free_001AFF90 (tools/test_status_scene_
#             reference.py PoolRig): the status pool 0x28B020 as captured at
#             the a06_02 / a06_03 ends, freeing each record through itself.
REUSE_OUT = OUT / 'reuse'


def _build_lib(name, sources, flags=()):
    REUSE_OUT.mkdir(parents=True, exist_ok=True)
    lib = REUSE_OUT / (name + ('.dylib' if sys.platform == 'darwin' else '.so'))
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared', '-fPIC',
                    '-Isrc', *flags, *sources, '-o', str(lib)], cwd=ROOT, check=True)
    return C.CDLL(str(lib))


def reuse_gun(elf):
    import test_security_gun_rest_reference as gr
    gr.OUT = REUSE_OUT / 'security_gun_rest'
    gr.sdf.CONTEXT['elf'] = gr.CONTEXT['elf'] = elf
    gr.CONTEXT['lib'] = gr.build()
    nodes = ticks = 0
    for beat in AREA06_BEATS:
        ram, _ = CAPTURES[beat]
        live = owners(ram, 0x21A500)
        if not reference_mode.FULL:
            live = live[:1]
        gr.sdf.CONTEXT['images'] = gr.CONTEXT['images'] = {beat: ram}
        gr.sdf.CONTEXT['base'] = gr.CONTEXT['base'] = ram
        for node in live:
            answers = dict(w_00122BB8=lambda k, t: (0x2545F491 * (k + 7)) & 0x7FFFFFFF,
                           w_001CCF70=lambda k, t: k + 5,
                           w_001CFB50=lambda k, t: bytes((k * 11 + i * 3) & 255 for i in range(0x60)))
            results, _, _, _ = gr.compare_run(ram, [], answers, 40, 'reuse 0021A500 %s %X' % (beat, node), node,
                                              entry=0x21A500)
            nodes += 1
            ticks += len(results)
    return '0021A500 %d live strip nodes, %d ticks' % (nodes, ticks)


def reuse_closure(elf):
    import test_player_closure_10_12_19_reference as pc
    lib = _build_lib('closure', ['src/game/em_player_closure_10_12_19.c', 'src/game/em_player_fall.c',
                                 'src/game/em_player_major2.c'])
    for name in pc.NATIVE_ENTRY.values():
        getattr(lib, name).argtypes = [C.c_void_p, pc.A]
    pc.ELF, pc.NATIVE = elf, lib
    count = reference_mode.pick(3000, 300)
    report = []
    for beat in ('a06_s1_bar', 'a06_00_beam'):
        pc.RAM = CAPTURES[beat][0]
        pc._EE = None
        for address, size in tuple(pc.EXECUTED.items()) + pc.TABLES:
            at = address - 0x100000 + 0x300
            assert pc.RAM[address:address + size] == elf[at:at + size], ('closure code differs', hex(address))
        pc.CAPTURED = pc.RAM[pc.PLAYER:pc.PLAYER + 0x320]
        tags, branches, faults = pc.run_batch([0xA06 * 100000 + i for i in range(count)])
        report.append('%s %d cases (%d state pairs)' % (beat, count, len([t for t in tags if t[0] == 'state'])))
    return '00169730 family ' + ', '.join(report)


def reuse_messages(elf):
    import test_message_presenter_rest_reference as mp
    mp.OUT = REUSE_OUT / 'message_presenter_rest'
    lib = mp.build_native()
    _, image = mp.section_a(lib)
    ram = CAPTURES['a06_02_keypad'][0]
    advances = mp.md.original_advances(elf, ram)
    outcomes = set()
    lines = mp.section_c(elf, lib, ram, advances, image, outcomes)['lines']
    return '001FD0E0 / 001FDDB0 cue lines %s' % ' '.join('%d:%d%s' % (r['line'], r['frames'], '*' if r['done'] else '')
                                                            for r in lines)


def reuse_status(elf, smoke=False):
    """smoke=True (the default run's one reuse sample): four records of the
    a06_02 pool instead of all 24 records of both images."""
    import test_status_scene_reference as ss
    lib = _build_lib('status_scene', ['src/game/em_status_scene_original.c'])
    lib.em_status_scene_free_001AFF90.argtypes = [C.POINTER(ss.Pool), ss.PA, C.POINTER(ss.Workers),
                                                  C.POINTER(ss.Fault)]
    rng = random.Random(0x1AFF90)
    runs = 0
    for beat in ('a06_02_keypad',) if smoke else ('a06_02_keypad', 'a06_03_room_out'):
        ram = CAPTURES[beat][0]
        records = [ss.Actor.from_buffer_copy(ram[ss.pool_address(i):ss.pool_address(i) + 0x2F0]) for i in range(24)]
        for me in range(0, 24, 6) if smoke else range(24):
            rig = ss.PoolRig(elf, rng, [ss.Actor.from_buffer_copy(bytes(r)) for r in records])
            rig.pool.record[me].w14 = ss.pool_address(me)
            rig.o.save(ss.pool_address(me) + 0x14, ss.pool_address(me))
            top = rig.o.load(0x275BD0)
            before = dict(rig.o.mem)
            rig.o.run(0x1AFF90, (ss.pool_address(me),))
            fault = ss.Fault()
            assert lib.em_status_scene_free_001AFF90(C.byref(rig.pool), C.pointer(rig.pool.record[me]),
                                                     C.byref(rig.native_workers()), C.byref(fault)) == 0, (beat, me)
            assert rig.actual == rig.pushed_slots(top), ('AFF90 slots', beat, me)
            rig.compare(('AFF90', beat, me), before)
            runs += 1
    return '001AFF90 %d frees over the captured status pool' % runs


def reuse_status_smoke(elf):
    return reuse_status(elf, smoke=True) + ' (smoke sample)'


def reuse_checks(elf):
    """EM_TEST_FULL=1 runs all four checks. They re-check translations that
    have their own tests and run serially, so the default run keeps one small
    smoke sample (001AFF90, four frees of the a06_02 pool)."""
    problems, parts = [], []
    checks = (reuse_gun, reuse_closure, reuse_messages, reuse_status) if reference_mode.FULL else (reuse_status_smoke,)
    for check in checks:
        try:
            parts.append(check(elf))
        except AssertionError as error:
            problems.append((check.__name__, repr(error)[:600]))
    return problems, 'ok: ' + '; '.join(parts)


if __name__ == '__main__':
    main()
