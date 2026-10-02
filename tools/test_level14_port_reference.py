#!/usr/bin/env python3
"""Execute the ORIGINAL fourteenth-level code (the a19d / a15 census rows:
the AREA15 overlay's sub-0 / sub-1 placements and their sub-handlers, the
overlay's init, two AREA19 script callbacks, the 0012E3A0 creatures' state 2
/ 3 handlers and their pieces, the AREA15 enemy 00153950 and its states,
the player's ceiling-hang entry state 0016AC50, the ledge-top test
001782A0, the running jump's target helpers 001AA2A0 / 001AA410, the probe
height scan 001B2D00, the hit-effect spawner 001B4CF0, the effect set-up
001EDE40 and the light helper 001F6BA0) and compare em_level14_port*.c.

docs/LEVEL14_PORT.md. The census deltas of the fourteenth-level route
(decomp build/s87/census/a19d_delta.json and a15_delta.json,
new_functions; FOURTEENTH_LEVEL_ROUTE.md section 5) name 59 functions that
ran in no earlier pass; 19 of them have verified translations elsewhere in
the port and are reused, not re-translated (LEVEL14_PORT.md section 0); the
other 40 are translated in em_level14_port_*.c and are the entries below
(FUNCS). Calls between the translations run as original code at the top
level of the oracle (only other functions are hooks), and every function is
also an entry of its own.

The harness is tools/test_level13_port_reference.py's (lane L13T, itself
the LEVEL12 / LEVEL11 / LEVEL10 / LEVEL9 / LEVEL8 / AREA06 / AREA22 /
AREA04 design), copied and owned here, with the fourteenth-level captures,
this module's hook table and cases. Entries that take the original stack
pointer (kind S) run the oracle with sp = STACK_TOP and give the native
entry STACK_TOP; the frame locals live in the compared stack window. The
argument registers an entry does not take are poisoned at its start (so an
entry that read one would differ from the native side).

Oracle: the shared EE interpreter with the measured float model (FallEE,
tools/test_player_fall_reference.py) runs the original code resident in the
recorded RAM images of the fourteenth-level route (decomp
build/s87/route_a19d/<beat>/ and route_a15/<beat>/: AREA19 resident in the
a19d images but a19d_20, AREA15 in a19d_20 and a15_01). Before any case the
test checks that the overlay text of every image equals the user's
extract/OVERLAY/AREA19.BIN or AREA15.BIN (text size from the file header)
and that the boot text below 0x241000 equals the pinned ELF, so every
executed instruction is original. Nothing here embeds original bytes;
reports hold counts only.

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

EM_LEVEL14_PORT_ONLY=<label prefix> runs a subset (no coverage / contract
checks). EM_LEVEL14_PORT_SOURCE=<dir> tests another copy of the module
sources (the mutation sweep). EM_LEVEL14_PORT_MISSING=1 lists unexecuted
words. EM_LEVEL14_PORT_TRACE=<label prefix> prints, per case run, the
original's calls (callee, arguments, result) and its memory accesses
between them (address, size, load / store, the stored value): a behaviour
trace, no instruction text. At most 4 worker processes unless EM_TEST_JOBS
says otherwise.
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
OVERLAY_FILES = {16: DECOMP / 'extract/OVERLAY/AREA19.BIN', 12: DECOMP / 'extract/OVERLAY/AREA15.BIN'}
OUT = ROOT / 'build/level14/port'
MASK = 0xFFFFFFFF
MASK64 = 0xFFFFFFFFFFFFFFFF
RAM_SIZE, SPAD_SIZE = 0x2000000, 0x4000
ARENA = 0x823500
STACK_LO = STACK_TOP - 0x800
STACK_SIZE = STACK_TOP - STACK_LO
BEAT_DIRS = {}
for _b in ('a19d_00_beastA', 'a19d_01_ceiling', 'a19d_03_beastB', 'a19d_06_g3', 'a19d_07_cage', 'a19d_08_west',
           'a19d_12_post', 'a19d_20_door50'):
    BEAT_DIRS[_b] = DECOMP / 'build/s87/route_a19d' / _b
BEAT_DIRS['a15_01_door51'] = DECOMP / 'build/s87/route_a15/a15_01_door51'
BEATS = list(BEAT_DIRS)
# the resident overlay of each image (AREA19 = 16, AREA15 = 12)
RESIDENT = {b: (12 if b in ('a19d_20_door50', 'a15_01_door51') else 16) for b in BEATS}
POOL, NODE, NODES = 0x7A5640, 0x2F0, 400
PLAYER = 0x8102B0


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
    # AREA15 overlay (runtime addresses; overlay id 12)
    0x823580: ('em_level14_port_00823580', 0x20, [], None),
    0x8235A0: ('em_level14_port_008235A0', 0x110, [U], None),
    0x8236B0: ('em_level14_port_008236B0', 0xC8, [U], None),
    0x823780: ('em_level14_port_00823780', 0xC4, [U], None),
    0x823850: ('em_level14_port_00823850', 0x1A0, [U], None),
    0x8239F0: ('em_level14_port_008239F0', 0x144, [U], None),
    0x823E40: ('em_level14_port_00823E40', 0x120, [U], None),
    0x824070: ('em_level14_port_00824070', 0x1D0, [U], None),
    0x824510: ('em_level14_port_00824510', 0x4C, [U], None),
    0x824560: ('em_level14_port_00824560', 0x244, [U], None),
    0x8247B0: ('em_level14_port_008247B0', 0x1D4, [U], None),
    0x824990: ('em_level14_port_00824990', 0x1B0, [U], None),
    0x824E00: ('em_level14_port_00824E00', 0x4C, [U], None),
    0x824E50: ('em_level14_port_00824E50', 0x1E0, [U], None),
    0x825030: ('em_level14_port_00825030', 0x294, [U], None),
    0x8252D0: ('em_level14_port_008252D0', 0x44, [U], None),
    0x825320: ('em_level14_port_00825320', 0x110, [U], None),
    0x825430: ('em_level14_port_00825430', 0x8D4, [U], None),
    0x825D10: ('em_level14_port_00825D10', 0x8E4, [U], None),
    0x826600: ('em_level14_port_00826600', 0x244, [U], None),
    0x826850: ('em_level14_port_00826850', 0x120, [U], None),
    # AREA19 overlay (overlay id 16): two script callbacks
    0x826B30: ('em_level14_port_00826B30', 0xE0, [U], I),
    0x827540: ('em_level14_port_00827540', 0xC, [U], I),
    # boot: the 0012E3A0 creatures' state handlers and pieces
    0x131650: ('em_level14_port_00131650', 0xEC, [U, U], None),
    0x131740: ('em_level14_port_00131740', 0x1F4, [U, U], None),
    0x131B10: ('em_level14_port_00131B10', 0x368, [U, U], None),
    0x131E80: ('em_level14_port_00131E80', 0x44, [U], None),
    0x131F20: ('em_level14_port_00131F20', 0x70, [U, U, U], None),
    0x1339E0: ('em_level14_port_001339E0', 0x34, [U, U], None),
    # boot: the AREA15 enemy 00153950 and its states
    0x153950: ('em_level14_port_00153950', 0xC0, [U], None),
    0x153A10: ('em_level14_port_00153A10', 0x74, [U, U], None),
    0x153A90: ('em_level14_port_00153A90', 0xBC, [U, U], None),
    # boot: the others
    0x16AC50: ('em_level14_port_0016AC50', 0x184, [U], None),
    0x1782A0: ('em_level14_port_001782A0', 0xF0, [U, S], I),
    0x1AA2A0: ('em_level14_port_001AA2A0', 0x164, [U, U, F], I),
    0x1AA410: ('em_level14_port_001AA410', 0xC8, [U], F),
    0x1B2D00: ('em_level14_port_001B2D00', 0x144, [U, U], I),
    0x1B4CF0: ('em_level14_port_001B4CF0', 0x668, [U], None),
    0x1EDE40: ('em_level14_port_001EDE40', 0x34C, [U, U], None),
    0x1F6BA0: ('em_level14_port_001F6BA0', 0xC, [], None),
}
OVERLAY_OF = {entry: (16 if entry in (0x826B30, 0x827540) else 12) for entry in FUNCS if entry >= ARENA}
# The entries whose passing cases are all contract sites (they return from
# their own body): the default run and EM_TEST_FULL=1.
RETURN_SITES = tuple(e for e, (_, _, _, r) in FUNCS.items() if r is not None)
RETURN_SITES_FULL = RETURN_SITES


def has_result(entry):
    return FUNCS[entry][3] is not None


# Hooks, in the field order of EmLevel14PortHooks (after ctx and bytes; the
# +0x4C method's w_callback follows them).
# name -> (original address, [(ctype, register)], result kind or None)
HOOKS = [
# BEGIN GENERATED HOOKS
    ('w_001026A0', 0x1026A0, [(U, 4), (U, 5), (U, 6)], None),
    ('w_00102948', 0x102948, [(U, 4), (U, 5)], None),
    ('w_001029C0', 0x1029C0, [(U, 4)], None),
    ('w_00102C58', 0x102C58, [(U, 4), (U, 5), (U, 6)], None),
    ('w_00103230', 0x103230, [(U, 4), (U, 5), (F, 12)], None),
    ('w_0011DF78', 0x11DF78, [(F, 12)], F),
    ('w_0011E2A8', 0x11E2A8, [(F, 12)], F),
    ('w_0011E748', 0x11E748, [(F, 12)], F),
    ('w_00122BB8', 0x122BB8, [], I),
    ('w_001281C0', 0x1281C0, [(F, 12)], I),
    ('w_00131940', 0x131940, [(U, 4), (U, 5)], None),
    ('w_00131ED0', 0x131ED0, [(U, 4)], None),
    ('w_00132490', 0x132490, [(U, 4), (U, 5)], None),
    ('w_001328D0', 0x1328D0, [(U, 4), (U, 5)], None),
    ('w_00133A20', 0x133A20, [(U, 4), (U, 5)], None),
    ('w_00153B50', 0x153B50, [(U, 4), (U, 5)], None),
    ('w_00153EA0', 0x153EA0, [(U, 4), (U, 5)], None),
    ('w_001749A0', 0x1749A0, [(U, 4), (I, 5), (I, 6), (F, 12)], None),
    ('w_001885B0', 0x1885B0, [(U, 4)], I),
    ('w_00199DB0', 0x199DB0, [(U, 4)], None),
    ('w_00199FA0', 0x199FA0, [(U, 4), (U, 5)], I),
    ('w_0019BC40', 0x19BC40, [(U, 4)], None),
    ('w_0019C6F0', 0x19C6F0, [(I, 4), (I, 5)], None),
    ('w_001A2370', 0x1A2370, [(U, 4), (U, 5)], None),
    ('w_001AEE10', 0x1AEE10, [(I, 4), (I, 5)], None),
    ('w_001AFC10', 0x1AFC10, [(U, 4)], None),
    ('w_001B0CD0', 0x1B0CD0, [(U, 4), (I, 5)], None),
    ('w_001B0FD0', 0x1B0FD0, [(U, 4)], I),
    ('w_001B10B0', 0x1B10B0, [(U, 4), (I, 5), (I, 6)], I),
    ('w_001B1190', 0x1B1190, [(I, 4)], None),
    ('w_001B1470', 0x1B1470, [(F, 12)], F),
    ('w_001B1630', 0x1B1630, [(F, 12), (F, 13), (F, 14)], I),
    ('w_001B17A0', 0x1B17A0, [(U, 4)], I),
    ('w_001B1B70', 0x1B1B70, [(U, 4)], None),
    ('w_001B1EA0', 0x1B1EA0, [(I, 4), (U, 5), (U, 6), (I, 7)], I),
    ('w_001B5360', 0x1B5360, [(U, 4)], None),
    ('w_001B6660', 0x1B6660, [(U, 4)], U),
    ('w_001BA1A0', 0x1BA1A0, [(U, 4), (U, 5)], None),
    ('w_001BA1C0', 0x1BA1C0, [(U, 4), (I, 5)], I),
    ('w_001BA1F0', 0x1BA1F0, [(U, 4)], I),
    ('w_001BA540', 0x1BA540, [(U, 4)], None),
    ('w_001BA580', 0x1BA580, [(U, 4), (I, 5)], None),
    ('w_001BA8E0', 0x1BA8E0, [(U, 4), (I, 5)], None),
    ('w_001C4760', 0x1C4760, [(I, 4), (I, 5)], None),
    ('w_001C4820', 0x1C4820, [(U, 4)], None),
    ('w_001C5C90', 0x1C5C90, [(U, 4)], None),
    ('w_001C6160', 0x1C6160, [(U, 4)], I),
    ('w_001C6380', 0x1C6380, [(U, 4)], None),
    ('w_001C63E0', 0x1C63E0, [(U, 4), (I, 5)], None),
    ('w_001C64F0', 0x1C64F0, [(U, 4), (F, 12)], I),
    ('w_001C67E0', 0x1C67E0, [(U, 4), (I, 5), (F, 12), (F, 13)], None),
    ('w_001C68C0', 0x1C68C0, [(U, 4)], None),
    ('w_001CA5F0', 0x1CA5F0, [(U, 4), (I, 5)], None),
    ('w_001CA6F0', 0x1CA6F0, [(U, 4), (I, 5)], None),
    ('w_001CFB50', 0x1CFB50, [(U, 4), (I, 5), (U, 6), (F, 12), (F, 13), (F, 14), (F, 15), (F, 16)], None),
    ('w_001CFBE0', 0x1CFBE0, [(I, 4), (I, 5), (U, 6), (U, 7), (I, 8)], None),
    ('w_001EFD90', 0x1EFD90, [(I, 4), (U, 5), (U, 6)], None),
    ('w_001EFE00', 0x1EFE00, [(I, 4), (U, 5)], U),
    ('w_001F5940', 0x1F5940, [(I, 4), (U, 5), (I, 6)], None),
    ('w_001F66F0', 0x1F66F0, [(U, 4)], None),
    ('w_001FAE70', 0x1FAE70, [(I, 4)], None),
    ('w_001FBD50', 0x1FBD50, [(U, 4), (I, 5), (I, 6), (F, 12)], None),
    ('w_001FC3C0', 0x1FC3C0, [(U, 4), (U, 5), (I, 6), (F, 12), (F, 13)], None),
    ('w_001FC520', 0x1FC520, [(U, 4)], None),
    ('w_0021C040', 0x21C040, [(U, 4), (U, 5)], None),
    ('w_00823B40', 0x823B40, [(U, 4)], None),
    ('w_00823C80', 0x823C80, [(U, 4)], None),
    ('w_00824240', 0x824240, [(U, 4)], None),
    ('w_00824350', 0x824350, [(U, 4)], None),
    ('w_008243E0', 0x8243E0, [(U, 4)], None),
    ('w_00824B40', 0x824B40, [(U, 4)], None),
    ('w_00824C90', 0x824C90, [(U, 4)], None),
# END GENERATED HOOKS
]
BY_ADDRESS = {address: (name, args, result) for name, address, args, result in HOOKS}

# Callees that run as original code inside the oracle (writes replayed to
# the native side). Pure helpers only: none of them reaches hardware; each
# is first rehearsed with the argument registers its hook does not pass
# poisoned (Oracle.rehearse).
RUN = {0x1026A0, 0x102948, 0x1029C0, 0x102C58, 0x103230, 0x11DF78, 0x11E2A8, 0x11E748, 0x1281C0, 0x1B1470}

# 001B4CF0 dispatches its kind byte +3 through a 12-entry table at 0x26DE70
# (the register jump at 0x1B4D28); the translation is a switch, so the
# table's loads are left out of the access comparison (TABLE_BYTES).
JUMP_TABLES = {0x1B4D28: (0x26DE70, 12)}
TABLE_BYTES = frozenset(range(0x26DE70, 0x26DE70 + 48))

CALLBACK_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_uint32)
# Accesses of a callee-saved register at an $sp offset that are frame
# locals, not bookkeeping: 001782A0 loads the first frame vector's y (the
# vector 00199FA0 wrote) into $f20.
LOCAL_STORES = {0x1782D4}


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
SOURCES = ('em_level14_port_area15.c', 'em_level14_port_area19.c', 'em_level14_port_creature.c',
           'em_level14_port_boot.c')
def header_checks(source_dir=None):
    """The ctypes Hooks structure above is built from HOOKS; the C side's
    EmLevel14PortHooks (em_level14_port.h) and its wrappers
    (em_level14_port_internal.h) are text. Check that the header's generated
    block lists exactly the HOOKS entries, in order, with the same argument
    and result types, and that each hook has one wrapper l14_c_<address>
    that calls it and latches its own address."""
    base = Path(source_dir) if source_dir else ROOT / 'src/game'
    ctype = {U: 'uint32_t', I: 'int32_t', F: 'float', Q: 'uint64_t'}
    header = (base / 'em_level14_port.h').read_text()
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
    internal = (base / 'em_level14_port_internal.h').read_text()
    wrappers = internal.split('/* BEGIN GENERATED WRAPPERS */')[1].split('/* END GENERATED WRAPPERS */')[0]
    for hname, address, _, _ in HOOKS:
        tag = hname[2:]
        body = re.findall(r'static inline int l14_c_%s\(.*?\n\}\n' % tag, wrappers, re.S)
        if len(body) != 1 or ('o->h->%s(' % hname) not in body[0] or ('0x%08Xu' % address) not in body[0]:
            problems.append('%s: wrapper l14_c_%s missing or not calling / latching it' % (hname, tag))
    return problems, len(found)


def build(source_dir=None):
    """Compile the module (or the copies in `source_dir`, a mutation sweep)
    into a private library under build/level14/port."""
    OUT.mkdir(parents=True, exist_ok=True)
    base = Path(source_dir) if source_dir else ROOT / 'src/game'
    stem = 'level14_port' if not source_dir else 'level14_port_%d' % os.getpid()
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

    def branch(self, word, pc):
        """The core's branch decision; at the top level the outcome is also
        recorded (('b', pc, taken) in `seen`) for the branch report."""
        b = FallEE.branch(self, word, pc)
        if b is not None and self.top and self.seen is not None and not (word >> 26 == 4 and not word >> 16 & 0x3FF):
            self.seen.add(('b', pc, bool(b[0])))
        return b

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
    floats as bits into f12.., Q as the whole 64-bit register), poison the
    argument registers the entry does not take, and run it."""
    words = floats = 0
    for reg in INT_ARGS:
        ee.r[reg] = sx32(0x0BAD0000 + 4 * reg)
    for reg in FLOAT_ARGS:
        ee.f[reg] = 0x3F9BAD00 + 4 * reg
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
        self.trace = []         # EM_LEVEL14_PORT_TRACE: ('L' / 'S', address, size, value) and ('C', callee, args, result)
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
            self.trace.append(('S', address & MASK, len(data), int.from_bytes(data, 'little')))
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
            value = EE.load(ee, address, size)
            self.trace.append(('L', address & MASK, size, value) if key is not None
                              else ('l', address & MASK, size, ee.pc))   # frame bookkeeping (and its pc)
            return value
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
            self.trace.append(('C', name, values, outcome))
        return hook


    def callback_hook(self, callback):
        def cb(e):   # the actor's +0x4C method: stubbed, logged
            self.marks.append(len(self.stores))
            self.access.append([])
            self.log.append(('w_callback', (callback, e.r[4] & MASK), None, []))
            self.trace.append(('C', 'w_callback', (callback, e.r[4] & MASK), None))
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
        if os.environ.get('EM_LEVEL14_PORT_DEBUG'):
            a = first_difference_at(replay.expect_ram_base, buffer_address(ee.mem), RAM_SIZE)
            print('rebuild (full) ram diff at', None if a is None else hex(a))
        return not (LIBC.memcmp(replay.expect_ram_base, buffer_address(ee.mem), RAM_SIZE)
                    or LIBC.memcmp(replay.expect_spad_base, buffer_address(ee.spad), SPAD_SIZE))
    if len(ee.mem) != RAM_SIZE or len(ee.spad) != SPAD_SIZE:
        if os.environ.get('EM_LEVEL14_PORT_DEBUG'):
            print('rebuild: memory resized', hex(len(ee.mem)), hex(len(ee.spad)))
        return False
    spans = [(key, key + len(data)) for key, data in replay.stores if not in_window(key)]
    spans += list(ee.mem.touched) + [(0x70000000 + a, 0x70000000 + b) for a, b in ee.spad.touched]
    for start, stop in spans:
        if start >= 0x70000000:
            at, size = start - 0x70000000, stop - start
            if bytes(ee.spad[at:at + size]) != C.string_at(replay.expect_spad_base + at, size):
                if os.environ.get('EM_LEVEL14_PORT_DEBUG'):
                    print('rebuild differs in spad %X..+%X' % (start, size), bytes(ee.spad[at:at + size]).hex()[:64],
                          C.string_at(replay.expect_spad_base + at, size).hex()[:64])
                return False
        elif bytes(ee.mem[start:stop]) != C.string_at(replay.expect_ram_base + start, stop - start):
            if os.environ.get('EM_LEVEL14_PORT_DEBUG'):
                print('rebuild differs in %X..%X' % (start, stop), bytes(ee.mem[start:stop]).hex()[:64],
                      C.string_at(replay.expect_ram_base + start, stop - start).hex()[:64])
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
    """Every image must hold its resident overlay (AREA19 or AREA15) with
    its text equal to the user's extract/OVERLAY file; every image's boot
    text must equal the pinned ELF: every instruction the oracle executes is
    original. The designed-record area must be zero."""
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


# the entries that reach a node's +0x4C method (directly or through a
# translated callee); its function word is stubbed as the callback
SELF_ENTRIES = {0x8235A0, 0x8236B0, 0x823780, 0x823850, 0x8239F0, 0x824070, 0x824510, 0x824560, 0x8247B0,
                0x824E00, 0x824E50, 0x825030, 0x825320, 0x825430, 0x825D10, 0x826600, 0x826850, 0x131650,
                0x153950, 0x153A90}
SCRIBBLED_CALLBACKS = set()
HEAVY = {0x825430, 0x825D10, 0x1EDE40, 0x1B4CF0}


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
FREE = 0x1E40000          # RAM zero in every capture: the designed records live here
FULL_ONLY = set()         # labels of the cases EM_TEST_FULL=1 adds
CAPTURES = {}
B00, B01, B03, B06, B07, B08, B12, B20, A151 = BEATS
NODE_A = FREE + 0x1000    # a designed node (0x2F0 bytes)
NODE_B = FREE + 0x2000
# The images' records (LEVEL14_PORT.md section 3.2)
CREATURES = {B00: (0x7A7980, 0x7A7C70), B01: (0x7A7C70,), B03: (0x7A7C70,), B07: (0x7ACEB0,), B08: (0x7ACEB0,),
             B12: (0x7ACEB0,)}                 # behaviour 0x12E3A0 (0012E3A0's creatures)
A15S0 = {0x8235A0: (0x7A67E0,), 0x825320: (0x7A9CC0, 0x7A9FB0, 0x7AA2A0), 0x825D10: (0x7AA590,),
         0x825430: (0x7AA880,)}               # a19d_20 (AREA15 sub 0)
A15S1 = {0x826850: (0x7A7690, 0x7A7980, 0x7A7C70), 0x823850: (0x7A7F60, 0x7A8540), 0x8252D0: (0x7A8250, 0x7A8830),
         0x826600: (0x7A8B20,), 0x153950: (0x7AD1A0,)}   # a15_01 (AREA15 sub 1)
LIFT = 0x7ADD60                                # AREA19 [36] 0x826C10, a19d_00..06


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


def node_copy(ram, src, dst, behaviour=None):
    """A designed node at dst: the 0x2F0 bytes of the captured node src
    (its +0x4C method, matrices and pointers stay valid), optionally with
    another behaviour word +0x10."""
    patches = [(dst, bytes(ram[src:src + NODE]))]
    if behaviour is not None:
        patches.append((dst + 0x10, w32(behaviour)))
    return patches


def case_list(elf):
    rng = random.Random(0x14C)
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
    area15_cases(add, rng)
    area19_cases(add, rng)
    creature_cases(add, rng)
    boot_cases(add, rng)
    bound_cases(add)
    survivor_cases(add)
    return cases, captured, len(cases) - captured


def live(ram, node, callback):
    return ram[node] != 0 and u32(ram, node + 0x10) == callback


def capture_cases(add):
    """Every entry on the captured state of every image it applies to."""
    ram, _ = CAPTURES[B20]
    for entry, nodes in A15S0.items():
        for n in nodes:
            if live(ram, n, entry):
                add('capture %s %06X @%X' % (B20, entry, n), B20, entry, [n], bones(n))
    ram, _ = CAPTURES[A151]
    for entry, nodes in A15S1.items():
        for n in nodes:
            if live(ram, n, entry):
                add('capture %s %06X @%X' % (A151, entry, n), A151, entry, [n], bones(n))
    for beat in (B20, A151):
        add('capture %s 823580' % beat, beat, 0x823580, [])
    for beat in (B00, B01, B03, B06):
        add('capture %s 826B30' % beat, beat, 0x826B30, [LIFT])
        add('capture %s 827540' % beat, beat, 0x827540, [LIFT])
    for beat, nodes in CREATURES.items():
        ram, _ = CAPTURES[beat]
        for n in nodes:
            if not live(ram, n, 0x12E3A0):
                continue
            e = n + 0x1F0
            for entry in (0x131650, 0x131740, 0x131B10, 0x1339E0):
                add('capture %s %06X @%X' % (beat, entry, n), beat, entry, [n, e], bones(n))
            add('capture %s 131E80 @%X' % (beat, n), beat, 0x131E80, [n], bones(n))
            add('capture %s 131F20 @%X' % (beat, n), beat, 0x131F20, [n, 0x700038A0, 0x700038A0], bones(n))
            add('capture %s 1B4CF0 @%X' % (beat, n), beat, 0x1B4CF0, [n], bones(n))
            add('capture %s 1B2D00 @%X' % (beat, n), beat, 0x1B2D00, [n + 0xB0, FREE + 0x100])
    for beat in (B01, B08, B12):
        add('capture %s 0016AC50' % beat, beat, 0x16AC50, [PLAYER])
        add('capture %s 001782A0' % beat, beat, 0x1782A0, [PLAYER])
        add('capture %s 1F6BA0' % beat, beat, 0x1F6BA0, [])
    for beat in (B00, B07):
        add('capture %s 1EDE40' % beat, beat, 0x1EDE40, [FREE + 0x100, FREE + 0x200])


def area15_cases(add, rng):
    """The AREA15 entries, their state bytes patched on the captured nodes
    (a19d_20: [0] 0x8235A0, the 0x825320 objects, 0x825430 / 0x825D10;
    a15_01: 0x823850, 0x8252D0, 0x826600, 0x826850) and on designed nodes
    (a copy of a19d_20's [0] at NODE_A with the entry's +0xD) for the
    placements that end at once on the route."""
    B = B20
    ram, _ = CAPTURES[B]
    # 0x8235A0 ([0]) and its sub-handlers 0x8236B0 / 0x823780
    n = A15S0[0x8235A0][0]
    base = bones(n)
    for bit in (1, 0):
        add('a15 8235A0 s0 %d' % bit, B, 0x8235A0, [n], base + [(n + 4, b8(0))], queues={0x1BA1C0: [bit]})
    for st in (2, 3, 4):
        add('a15 8235A0 s%d' % st, B, 0x8235A0, [n], base + [(n + 4, b8(st))])
    for fa in (0, 1, 2):
        for sub in (0, 1, 2):
            for done in ((0, 1) if sub == 1 else (0,)):
                for bb in ((4, 0xFB) if fa == 1 and sub == 0 else (0,)):
                    add('a15 8235A0 s1 %d +5 %d %d %X' % (fa, sub, done, bb), B, 0x8235A0, [n],
                        base + [(n + 4, b8(1)), (n + 5, b8(sub)), (0x8107FA, b8(fa)), (n + 0xB, b8(bb))],
                        queues={0x1BA1F0: [done]})
    for entry in (0x8236B0, 0x823780):
        for sub in (0, 1, 2, 0xFF):
            for done in ((0, 1) if sub == 1 else (0,)):
                for bb in ((4, 0xFB, 0xFF) if entry == 0x823780 and sub == 0 else (0,)):
                    add('a15 %06X +5 %X %d %X' % (entry, sub, done, bb), B, entry, [n],
                        base + [(n + 5, b8(sub)), (n + 0xB, b8(bb))], queues={0x1BA1F0: [done]})
    # 0x823850 (sub 1, +0xD 0x54 and 0x5D) with 0x8239F0 / 0x823E40
    B = A151
    for n in A15S1[0x823850]:
        base = bones(n)
        for d in (None, 0x10):
            more = [] if d is None else [(n + 0xD, b8(d))]
            for q in ([1], [0, 0], [0, 1]):
                add('a15 823850 s0 @%X %s %s' % (n, d, q), B, 0x823850, [n], base + more + [(n + 4, b8(0))],
                    queues={0x1BA1C0: q, 0x1B6660: [0x12345678]})
        for st in (2, 3, 4):
            add('a15 823850 s%d @%X' % (st, n), B, 0x823850, [n], base + [(n + 4, b8(st))])
        for fb_ in (0, 1, 2, 3, 4, 5):
            add('a15 823850 s1 @%X FB %d' % (n, fb_), B, 0x823850, [n],
                base + [(n + 4, b8(1)), (0x8107FB, b8(fb_)), (0x70003B92, b8(0))])
        # 0x8239F0
        for sub in (0, 1, 2):
            for y, r in (((309.0, 1), (310.0, 1), (400.0, 1), (400.0, 0), (400.0, 2)) if sub == 0 else ((400.0, 1),)):
                for done in ((0, 1) if sub == 1 else (0,)):
                    for b92 in ((0, 1) if (sub, y, r) == (0, 400.0, 1) else (0,)):
                        add('a15 8239F0 @%X +5 %d %g %d %d %d' % (n, sub, y, r, done, b92), B, 0x8239F0, [n],
                            base + [(n + 5, b8(sub)), (0x810354, f32b(y)), (0x70003B92, b8(b92))],
                            queues={0x1B1EA0: [r], 0x1BA1F0: [done], 0x122BB8: [0x12345678]})
        # 0x823E40: the counter, the re-arm and the countdown
        for c, k240, r in ((7, 0, 0), (0, 0, 0), (239, 0, 0), (247, 0, 0x7FFFFFFF), (247, 0, -0x80000000),
                           (247, 0, 0x12345678), (15, 3, 0), (15, 0, 0), (-9, 0, 0), (0x7FFF, 0, 0), (0xFFF7, 1, 0),
                           (-4, 0, 0), (0xFFF0, 0, 0), (0xFF00, 0xFF, 0), (7, 0x80, 0)):
            add('a15 823E40 @%X %X %d %X' % (n, c & 0xFFFF, k240, r & MASK), B, 0x823E40, [n],
                base + [(n + 0x2A, h16(c)), (n + 0x240, b8(k240))], queues={0x122BB8: [r]})
    # 0x8252D0
    for n in A15S1[0x8252D0]:
        for k in (1, 0, 2, 0xFF):
            add('a15 8252D0 @%X +3 %X' % (n, k), B, 0x8252D0, [n], [(n + 3, b8(k))])
    # 0x826600
    n = A15S1[0x826600][0]
    base = bones(n)
    for q in ([0], [1]):
        add('a15 826600 s0 %s' % q, B, 0x826600, [n], base + [(n + 4, b8(0))], queues={0x1BA1C0: q})
    for q, b92 in (([1], 0), ([0, 1], 0), ([0, 0], 0), ([0], 1), ([0], 0xFF)):
        add('a15 826600 s1 %s %X' % (q, b92), B, 0x826600, [n], base + [(n + 4, b8(1)), (0x70003B92, b8(b92))],
            queues={0x1BA1C0: q})
    for st in (2, 3, 4):
        add('a15 826600 s%d' % st, B, 0x826600, [n], base + [(n + 4, b8(st))])
    # 0x826850
    for n in A15S1[0x826850]:
        base = bones(n)
        for d in (0xD, 0xC, 0xE):
            add('a15 826850 s0 @%X %X' % (n, d), B, 0x826850, [n], base + [(n + 4, b8(0)), (n + 0xD, b8(d))])
            for fb_ in (2, 3, 4, 0x80):
                add('a15 826850 s1 @%X %X %X' % (n, d, fb_), B, 0x826850, [n],
                    base + [(n + 4, b8(1)), (n + 0xD, b8(d)), (0x8107FB, b8(fb_))])
        for st in (2, 3, 4):
            add('a15 826850 s%d @%X' % (st, n), B, 0x826850, [n], base + [(n + 4, b8(st))])
    # 0x825320 (sub 0's spinning objects)
    B = B20
    for n in A15S0[0x825320][:1]:
        base = bones(n)
        for r in (0, 1, 0x7FFFFFFF, -0x80000000, 0x12345678, -5):
            add('a15 825320 s0 %X' % (r & MASK), B, 0x825320, [n], base + [(n + 4, b8(0))],
                queues={0x122BB8: [r], 0x1B0FD0: [1]})
        for st in (1, 2, 3, 4):
            add('a15 825320 s%d' % st, B, 0x825320, [n], base + [(n + 4, b8(st))])
    # 0x825430 / 0x825D10 (the swinging gates): the player's offsets inside /
    # outside the box, the side, the amplitude / phase / step and the angle
    for entry in (0x825430, 0x825D10):
        n = A15S0[entry][0]
        base = bones(n)
        bone0 = u32(ram, n + 0x110)
        mir = entry == 0x825D10

        def at(xo, zo):
            """The player position giving the offsets (xo, zo)."""
            return ((869.0 - xo) if mir else (869.0 + xo)), ((966.5 - zo) if mir else (951.5 + zo))
        for st in (0, 2, 3, 4):
            add('a15 %06X s%d' % (entry, st), B, entry, [n], base + [(n + 4, b8(st))], queues={0x1B0FD0: [1]})
        for xo, zo, side, ang in ((0.0, -1.0, 1, 0.3), (-3.0, -1.0, 1, 0.3), (3.0, -1.0, -1, 0.3), (0.0, -1.0, -1, 0.3),
                                  (-3.0, -1.0, -1, 0.3), (3.0, -1.0, 1, 0.3), (0.0, -1.0, 0, -0.7), (1.9, -1.5, 1, 0.2),
                                  (-1.9, -0.5, -1, 0.2)):
            x, z = at(xo, zo)
            add('a15 %06X pin %g %g %d %g' % (entry, xo, zo, side, ang), B, entry, [n],
                base + [(n + 4, b8(1)), (0x810350, f32b(x)), (0x810358, f32b(z)), (n + 0x1F0, w32(side)),
                        (bone0 + 0x74, f32b(ang)), (n + 0x1F4, f32b(0.5))])
        for xo, zo in ((7.8, 3.0), (-7.8, 3.0), (0.0, -2.5), (0.0, 9.6), (0.0, -2.0), (0.0, 9.5), (7.7, 3.0)):
            x, z = at(xo, zo)
            for amp, p3, step in ((0.5, 0.1, 0.08), (0.01, 0.1, 0.08), (0.5, -0.05, 0.08), (0.5, 0.05, -0.08),
                                  (-0.4, 3.1, 0.08), (0.3, -3.1, -0.08), (0.5, 0.0, 0.0)):
                add('a15 %06X out %g %g %g %g %g' % (entry, xo, zo, amp, p3, step), B, entry, [n],
                    base + [(n + 4, b8(1)), (0x810350, f32b(x)), (0x810358, f32b(z)), (n + 0x1F0, w32(1)),
                            (n + 0x1F4, f32b(amp)), (n + 0x1FC, f32b(p3)), (n + 0x200, f32b(step)),
                            (n + 0x204, f32b(1.0))], queues={0x1B1630: [1]})
        for xo, zo, side, v9, f204, ang, amp, p3, step, vis in (
                (2.0, 3.0, 0, 1.0, 0.0, 0.6, 0.2, 0.5, 0.08, 1), (2.0, 3.0, 0, -1.0, 0.0, 0.3, 0.2, 0.5, 0.08, 0),
                (2.0, 3.0, 0, 0.0, 1.0, 0.6, 0.2, 0.5, 0.08, 1), (-6.0, 3.0, 1, 1.0, 0.0, 0.6, 0.2, 3.1, 0.08, 1),
                (3.0, 0.0, 1, 1.0, 0.0, 0.1, 0.2, 0.5, 0.08, 1), (3.0, 8.0, 1, 1.0, 0.0, 0.1, 1.2, 0.5, 0.08, 1),
                (3.0, 8.0, 1, 1.0, 0.0, 0.1, 0.1, 1.0, 0.08, 1), (3.0, 8.0, 1, 1.0, 0.0, 0.1, 0.3, -1.0, 0.08, 1),
                (-3.0, 3.0, -1, 1.0, 0.0, 0.1, -0.2, 0.5, 0.08, 1), (3.0, 3.0, -1, 1.0, 0.0, 0.1, -0.2, -3.1, -0.08, 1),
                (-6.0, 0.5, -1, 1.0, 0.0, 0.1, -1.2, 0.5, 0.08, 1), (-6.0, 8.0, -1, 1.0, 0.0, 0.1, -0.1, 1.0, 0.08, 1),
                (-6.0, 8.0, -1, 1.0, 0.0, 0.1, -0.5, -1.0, 0.08, 1), (6.0, 3.0, -1, 1.0, 0.0, 1.4, 0.1, 0.5, 0.08, 1),
                (-3.0, 3.0, 1, 1.0, 0.0, -1.4, 0.1, 0.5, 0.08, 1), (7.0, 1.0, 1, 1.0, 0.0, 0.1, 0.5, 0.5, 0.08, 1),
                (-7.0, 1.0, -1, 1.0, 0.0, 0.1, -0.5, 0.5, 0.08, 1), (0.0, 5.0, 1, 1.0, 0.0, 0.1, 0.0, 0.5, 0.08, 1),
                (3.0, 3.0, 1, 1.0, 0.0, 0.1, 1.2, 0.05, 0.0, 1), (-6.0, 3.0, -1, 1.0, 0.0, 0.1, -1.2, 0.05, 0.0, 1),
                (-7.0, 0.5, -1, 1.0, 0.0, 0.1, -0.1, 0.5, 0.08, 1), (7.0, 0.5, 1, 1.0, 0.0, 0.1, 0.1, 0.5, 0.08, 1),
                (2.0, 3.0, -1, 1.0, 0.0, 0.1, -1.5, 0.3, 0.08, 1), (-2.0, 3.0, 1, 1.0, 0.0, 0.1, 1.5, 0.3, 0.08, 1),
                (-7.5, -0.5, 1, 1.0, 0.0, 0.1, 0.5, 0.3, 0.08, 1), (7.5, 9.0, -1, 1.0, 0.0, 0.1, -0.5, -0.3, 0.08, 1),
                (-4.5, 8.0, 1, 1.0, 0.0, 0.1, -1.2, 0.01, 0.0, 1), (-4.5, 8.0, -1, 1.0, 0.0, 0.1, -1.2, 0.01, 0.0, 1),
                (4.5, 8.0, 1, 1.0, 0.0, 0.1, 1.2, 0.01, 0.0, 1), (4.5, 8.0, -1, 1.0, 0.0, 0.1, 1.2, 0.01, 0.0, 1),
                (4.5, 8.0, -1, 1.0, 0.0, 0.1, 1.2, 1.0, 0.0, 1), (-4.5, 8.0, 1, 1.0, 0.0, 0.1, -1.2, 1.0, 0.0, 1),
                (4.5, 8.0, 1, 1.0, 0.0, 0.1, 1.2, 1.0, 0.0, 1), (-4.5, 8.0, -1, 1.0, 0.0, 0.1, -1.2, 1.0, 0.0, 1)):
            x, z = at(xo, zo)
            add('a15 %06X push %g %g %d %g %g %g %g %g %g %d' % (entry, xo, zo, side, v9, f204, ang, amp, p3, step, vis),
                B, entry, [n], base + [(n + 4, b8(1)), (0x810350, f32b(x)), (0x810358, f32b(z)), (0x810374, f32b(v9)),
                                       (n + 0x1F0, w32(side)), (n + 0x204, f32b(f204)), (bone0 + 0x74, f32b(ang)),
                                       (n + 0x1F4, f32b(amp)), (n + 0x1FC, f32b(p3)), (n + 0x200, f32b(step))],
                queues={0x1B1630: [vis]})
    # a seeded sweep of the swing's inputs (EM_TEST_FULL=1)
    for entry in (0x825430, 0x825D10):
        n = A15S0[entry][0]
        bone0 = u32(ram, n + 0x110)
        for k in range(120):
            x = rng.uniform(855.0, 883.0)
            z = rng.uniform(944.0, 974.0)
            side = rng.choice((-1, 0, 1))
            vals = [rng.uniform(-1.6, 1.6), rng.uniform(-1.6, 1.6), rng.uniform(-3.3, 3.3), rng.uniform(-0.1, 0.1)]
            add('a15 %06X sweep %d' % (entry, k), B, entry, [n],
                bones(n) + [(n + 4, b8(1)), (0x810350, f32b(x)), (0x810358, f32b(z)),
                            (0x810374, f32b(rng.uniform(-1, 1))), (n + 0x1F0, w32(side)),
                            (n + 0x204, f32b(rng.choice((0.0, 1.0)))), (bone0 + 0x74, f32b(vals[0])),
                            (n + 0x1F4, f32b(vals[1])), (n + 0x1FC, f32b(vals[2])), (n + 0x200, f32b(vals[3]))],
                queues={0x1B1630: [rng.choice((0, 1))]}, full=True)
    # the placements that end at once on the route: a designed node at NODE_A
    src = A15S0[0x8235A0][0]
    for entry, ids in ((0x824070, (0x50, 0x64, 0x10)), (0x824510, (0x64, 0x50, 0x10)), (0x824560, (0x64,)),
                       (0x8247B0, (0x50,)), (0x824990, (0x50, 0x5A, 0x10)), (0x824E00, (0x59, 0x50, 0x10)),
                       (0x824E50, (0x59,)), (0x825030, (0x50,))):
        for d in ids:
            node = node_copy(ram, src, NODE_A, entry) + bones(NODE_A) + [(NODE_A + 0xD, b8(d))]
            for st in (2, 3, 4):
                add('a15 %06X %X s%d' % (entry, d, st), B, entry, [NODE_A], node + [(NODE_A + 4, b8(st))])
            gates = {0x824070: ([0], [1, 1], [1, 0, 1], [1, 0, 0]), 0x824990: ([0], [1])}.get(
                entry, ([1], [0, 0, 0], [0, 1], [0, 0, 1]) if entry in (0x824510, 0x824560, 0x8247B0) else
                ([1], [0, 0], [0, 1]))
            for q in gates:
                for b804 in ((0, 1) if entry in (0x824E00, 0x825030) else (0,)):
                    add('a15 %06X %X s0 %s %d' % (entry, d, q, b804), B, entry, [NODE_A],
                        node + [(NODE_A + 4, b8(0)), (0x810804, b8(b804))], queues={0x1BA1C0: q})
            if entry == 0x824070:
                for fc in (0, 1, 2):
                    add('a15 824070 %X s1 FC %d' % (d, fc), B, entry, [NODE_A],
                        node + [(NODE_A + 4, b8(1)), (0x8107FC, b8(fc))])
            elif entry == 0x824990:
                for q, ff_ in (([1], 0), ([0], 0), ([0], 1), ([0], 2)):
                    add('a15 824990 %X s1 %s FF %d' % (d, q, ff_), B, entry, [NODE_A],
                        node + [(NODE_A + 4, b8(1)), (0x8107FF, b8(ff_))], queues={0x1BA1C0: q})
            elif entry in (0x824510, 0x824560, 0x8247B0):
                for sub in (0, 1, 2):
                    for bb in ((4, 0xFB) if sub == 0 else (0,)):
                        for c28, done in (((0x206, 0), (0x207, 0), (0x207, 1), (0x208, 1), (-1, 0)) if sub == 1
                                          else ((0, 0),)):
                            add('a15 %06X %X s1 +5 %d %X %X %d' % (entry, d, sub, bb, c28 & 0xFFFF, done), B, entry,
                                [NODE_A], node + [(NODE_A + 4, b8(1)), (NODE_A + 5, b8(sub)), (NODE_A + 0xB, b8(bb)),
                                                  (NODE_A + 0x28, h16(c28))], queues={0x1BA1F0: [done]})
            else:   # 0x824E00 / 0x824E50 / 0x825030
                for sub in (0, 1, 2, 3):
                    for a702, b784, b804, bb in (((1, 0, 0, 0), (1, 0, 1, 0), (0, 0, 0, 0), (1, 1, 0, 0), (2, 0, 1, 0),
                                                  (1, 1, 1, 0)) if sub == 0 else ((1, 0, 0, 4), (1, 0, 1, 4),
                                                                                  (1, 0, 1, 0xFB))):
                        for done in ((0, 1) if sub in (1, 2) else (0,)):
                            add('a15 %06X %X s1 +5 %d %d %d %d %X %d' % (entry, d, sub, a702, b784, b804, bb, done),
                                B, entry, [NODE_A], node + [(NODE_A + 4, b8(1)), (NODE_A + 5, b8(sub)),
                                                            (0x810702, b8(a702)), (0x810784, b8(b784)),
                                                            (0x810804, b8(b804)), (NODE_A + 0xB, b8(bb))],
                                queues={0x1BA1F0: [done]})


def area19_cases(add, rng):
    """The two script callbacks on the lift node and on a designed record."""
    for beat in (B07, B03):
        add('a19 826B30 %s free' % beat, beat, 0x826B30, [NODE_A])
        add('a19 827540 %s free' % beat, beat, 0x827540, [NODE_A], [(NODE_A + 0x28, h16(0xFFFF))])


def creature_cases(add, rng):
    """The 0012E3A0 creatures' handlers on their captured nodes with the
    state bytes patched (a19d_00 / 03 / 07), and the AREA15 enemy
    00153950 / 00153A10 / 00153A90 on a15_01's node."""
    B = B00
    n = CREATURES[B][1]
    e = n + 0x1F0
    base = bones(n)
    # 00131650 (state 2): every sub-state +5, the +0x20 object and ent +0x65
    for sub in range(0, 7):
        for p20, f65 in ((0, 0), (NODE_A, 0), (0, 1), (NODE_A, 1)):
            add('cr 131650 +5 %d %X %d' % (sub, p20, f65), B, 0x131650, [n, e],
                base + [(n + 5, b8(sub)), (n + 6, b8(4)), (n + 0x20, w32(p20)), (e + 0x65, b8(f65)),
                        (NODE_A + 4, b8(1))], queues={0x1C64F0: [0x2468]})
    add('cr 131650 +5 80', B, 0x131650, [n, e], base + [(n + 5, b8(0x80)), (n + 6, b8(4))])
    # 001B4CF0: every kind with both bits of +0xD
    for kind in range(0, 13):
        for d in ((0, 1, 0x80, 0x81) if kind in (1, 4, 5, 6, 7) else (0, 0x81)):
            add('cr 1B4CF0 k%d %X' % (kind, d), B, 0x1B4CF0, [n], base + [(n + 3, b8(kind)), (n + 0xD, b8(d))])
    add('cr 1B4CF0 kFF', B, 0x1B4CF0, [n], base + [(n + 3, b8(0xFF))])
    # 00131740
    for f61, c2c, t, f3c, s34, s36, r in ((0, 4, 0, 0.0, 0, 0, 0), (0, 4, 0, 0.0, 1, 0x8000, 0),
                                          (0, 4, 0, 0.0, 0, 0, 0x1000), (0, 4, 0, 0.0, 0, 0, 0x7FFFFFFF),
                                          (0, 4, 0, 0.0, 0, 0, -0x80000000), (0, 4, 0, 0.0, 1, 0x7FFF, -0x12345),
                                          (1, 4, 100, 20.0, 0, 0, 0), (1, 4, 100, 40.0, 0, 0, 0),
                                          (1, 4, 85, 20.0, 0, 0, 0), (1, 5, 100, 20.0, 0, 0, 0),
                                          (1, 0x104, 100, 20.0, 0, 0, 0), (1, 4, -100, 20.0, 1, 0x8000, 0)):
        add('cr 131740 s0 %d %X %d %g %d %X %X' % (f61, c2c, t, f3c, s34, s36, r & MASK), B, 0x131740, [n, e],
            base + [(n + 6, b8(0)), (e + 0x61, b8(f61)), (n + 0x2C, h16(c2c)), (n + 0x3C, f32b(f3c)),
                    (n + 0x34, h16(s34)), (n + 0x36, h16(s36))],
            queues={0x1C6160: [t], 0x122BB8: [r]})
    for f58, s34, f61 in ((0x1000, 1, 0), (0x1000, 0, 0), (0xEFFF, 1, 0), (0xEFFF, 1, 1), (0x11000, 0, 1)):
        add('cr 131740 s1 %X %d %d' % (f58, s34, f61), B, 0x131740, [n, e],
            base + [(n + 6, b8(1)), (e + 0x58, h16(f58)), (n + 0x34, h16(s34)), (e + 0x61, b8(f61))])
    for st in (2, 0xFF):
        add('cr 131740 s%d' % st, B, 0x131740, [n, e], base + [(n + 6, b8(st))])
    # 00131B10
    for dd, f61, r in ((2, 0, 0), (2, 1, 0), (0, 1, 0), (0, 0, 0x200), (0, 0, 0x1FF), (0xFD, 0, -0x200),
                       (0xFF, 1, 0)):
        add('cr 131B10 s0 %X %d %X' % (dd, f61, r & MASK), B, 0x131B10, [n, e],
            base + [(n + 6, b8(0)), (n + 0xD, b8(dd)), (e + 0x61, b8(f61))], queues={0x122BB8: [r]})
    for clip, f3c, f58, dd, got in ((0x2A, 34.0, 0, 0, 0), (0x802A, 34.0, 0, 0, 0), (0x2B, 21.0, 0, 0, 0),
                                    (0x2B, 34.0, 0, 0, 0), (0x2A, 21.0, 0, 0, 0), (0x12A, 34.0, 0, 0, 0),
                                    (0x2C, 34.0, 0x1000, 0, 0), (0x2C, 34.0, 0x1000, 0, 0x7AF000),
                                    (0x2C, 34.0, 0x1000, 2, 0), (0x2A, -0.0, 0xEFFF, 0, 0)):
        add('cr 131B10 s1 %X %g %X %X %X' % (clip, f3c, f58, dd, got), B, 0x131B10, [n, e],
            base + [(n + 6, b8(1)), (n + 0x2C, h16(clip)), (n + 0x3C, f32b(f3c)), (e + 0x58, h16(f58)),
                    (n + 0xD, b8(dd))], queues={0x1EFE00: [got]})
    for dd, a2, a0, link in ((2, 1, 2, NODE_A), (2, 1, 2, 0), (2, 0, 2, NODE_A), (2, 1, 3, NODE_A), (0, 1, 2, NODE_A),
                             (2, 0xFF, 0x10002, NODE_A), (2, 0x80, 0x8002, NODE_A), (2, 1, -2, NODE_A)):
        add('cr 131B10 s2 %X %X %X %X' % (dd, a2, a0, link), B, 0x131B10, [n, e],
            base + [(n + 6, b8(2)), (n + 0xD, b8(dd)), (0x28A9A2, b8(a2)), (0x28A9A0, h16(a0)),
                    (n + 0x24, w32(link)), (NODE_A + 4, b8(1))])
    for ea, r in ((2, 0x200), (2, 0), (1, 0), (0x102, 0)):
        add('cr 131B10 s3 %X %X' % (ea, r), B, 0x131B10, [n, e], base + [(n + 6, b8(3)), (0x8107EA, b8(ea))],
            queues={0x122BB8: [r], 0x1EFE00: [0x7A9000]})
    add('cr 131B10 s4', B, 0x131B10, [n, e], base + [(n + 6, b8(4))])
    # 001339E0, 00131E80
    for f65 in (0, 1, 0xFF):
        add('cr 1339E0 %X' % f65, B, 0x1339E0, [n, e], [(e + 0x65, b8(f65))])
    for p20 in (0, NODE_A):
        add('cr 131E80 %X' % p20, B, 0x131E80, [n], [(n + 0x20, w32(p20)), (NODE_A + 4, b8(1))])
    add('cr 131F20', B, 0x131F20, [n, FREE + 0x100, FREE + 0x200], [(FREE + 0x100, fw(1.0, 2.0, 3.0, 1.0))])
    # 00153A10 / 00153A90 (a15_01's enemy node)
    B = A151
    n = A15S1[0x153950][0]
    e = n + 0x1F0
    base = bones(n)
    for st in range(0, 7):
        for f10 in (0, 1, 0xFF):
            add('cr 153950 s%d %X' % (st, f10), B, 0x153950, [n], base + [(n + 4, b8(st)), (e + 0x10, b8(f10))],
                queues={0x1B10B0: [1]})
    add('cr 153950 s80', B, 0x153950, [n], base + [(n + 4, b8(0x80))])
    for r in (0, 1, 5):
        add('cr 153A10 %d' % r, B, 0x153A10, [n, e], base, queues={0x1B10B0: [r]})
    for s36, b7b, bfb in ((0, 0, 0), (5, 0, 0), (5, 1, 0), (5, 1, 1), (0, 1, 0), (0, 1, 1), (0, 2, 0), (-1, 0x81, 0)):
        add('cr 153A90 %X %X %X' % (s36 & 0xFFFF, b7b, bfb), B, 0x153A90, [n, FREE + 0x100],
            base + [(n + 0x36, h16(s36)), (0x81077B, b8(b7b)), (0x8107FB, b8(bfb))], queues={0x1C64F0: [0x1234]})


def boot_cases(add, rng):
    """0016AC50 / 001782A0 on the player record (a19d_01 / 08 / 12), the
    probe-height scan 001B2D00 with designed column tables, 001EDE40 with
    designed generator states, 001F6BA0."""
    B = B01
    P = PLAYER
    for st, f200, s28 in ((0, 0, 0), (1, 0x1000, 0), (1, 0xEFFF, 0), (2, 0, 0), (2, 0, 3), (2, 0, -1), (3, 0x1000, 0),
                          (3, 0x0FFF, 0), (4, 0x1000, 0), (0xFF, 0x1000, 0)):
        add('bt 16AC50 s%d %X %d' % (st, f200, s28), B, 0x16AC50, [P],
            [(P + 6, b8(st)), (P + 0x200, w32(f200)), (P + 0x28, h16(s28)), (P + 0x254, f32b(231.5)),
             (P + 0xB4, f32b(219.25)), (P + 0x2E4, f32b(1.5))], queues={0x1885B0: [0x77]})
    for y, yb, r, k in ((219.0, 200.0, 1, 7), (219.0, 230.0, 1, 0), (219.0, 222.0, 1, 1), (219.0, 200.0, 0, 0),
                        (219.0, 222.1, 1, 0), (-5.0, -8.0, 1, -3)):
        for beat in (B12, B08):
            add('bt 1782A0 %s %g %g %d %d' % (beat, y, yb, r, k), beat, 0x1782A0, [P],
                [(P + 0xB4, f32b(y)), (P + 0xC4, f32b(1.25))],
                queues={0x199FA0: [Scribble(r, [(STACK_TOP - 0x20, fw(1.0, yb, 3.0, 1.0)),
                                                (STACK_TOP - 0x10, fw(0.0, 1.0, 0.0, 0.0))])],
                        0x199DB0: [Scribble(None, [(0x700038A0, fw(10.5, 11.5, 12.5, 1.0))])]},
                run_set=RUN - {0x1281C0} if k else RUN, )
    # 001B2D00: the column table (heights 0x700030F0, flags 0x70003170)
    B = B07
    for n_, hs, fl, y in ((0, (), (), 10.0), (1, (5.0,), (1,), 10.0), (1, (15.0,), (1,), 10.0), (1, (15.0,), (0,), 10.0),
                          (3, (1.0, 5.0, 9.0), (0, 1, 0), 10.0), (3, (1.0, 5.0, 9.0), (0, 1, 1), 10.0),
                          (3, (1.0, 5.0, 9.0), (1, 0, 0), 10.0), (3, (1.0, 5.0, 12.0), (1, 0, 1), 10.0),
                          (3, (1.0, 5.0, 12.0), (1, 0, 0), 10.0), (3, (1.0, 10.0, 12.0), (0, 1, 0), 10.0),
                          (2, (1.0, 5.0), (1, 0), 10.0), (2, (1.0, 5.0), (0, 0), 10.0), (-1, (1.0,), (1,), 10.0),
                          (1, (5.0,), (0,), 10.0), (2, (1.0, 5.0), (0, 2), 10.0), (2, (1.0, 5.0), (3, 0), 10.0)):
        add('bt 1B2D00 %d %s %s %g' % (n_, hs, fl, y), B, 0x1B2D00, [FREE + 0x100, FREE + 0x200],
            [(FREE + 0x100, fw(1.0, y, 2.0, 1.0)), (0x700031E0, w32(n_))]
            + [(0x700030F0 + 4 * i, f32b(h)) for i, h in enumerate(hs)]
            + [(0x70003170 + 2 * i, h16(f)) for i, f in enumerate(fl)])
    # 001EDE40: the generator state at a designed record
    for r, f8 in ((0, 0.5), (0x12345678, 0.021), (-1, 0.02), (0x7FFF0000, 0.01), (0xFFFF0000, -1.0)):
        add('bt 1EDE40 %X %g' % (r & MASK, f8), B, 0x1EDE40, [FREE + 0x100, FREE + 0x200],
            [(0x275C34, w32(FREE + 0x400)), (FREE + 0x404, w32(r)), (FREE + 0x408, f32b(f8)),
             (FREE + 0x454, f32b(2.5))])
    # 001AA410 / 001AA2A0 (the running jump's target helpers) on a19d_08's
    # listed target 0x7AA2A0 (an AREA19 creature) and on designed objects
    B = B08
    T = 0x7AA2A0
    ram, _ = CAPTURES[B]
    add('bt 1AA410 target', B, 0x1AA410, [T])
    for kind in range(0, 16):
        add('bt 1AA410 kind %d' % kind, B, 0x1AA410, [T], [(T + 3, b8(kind))])
    add('bt 1AA2A0 target', B, 0x1AA2A0, [PLAYER, T, fb(3.0)])
    for kind in (0x10, 0x80, 0xFF):
        add('bt 1AA410 kind %d' % kind, B, 0x1AA410, [T], [(T + 3, b8(kind))])
    px, pz = struct.unpack_from('<f', ram, PLAYER + 0xA0)[0], struct.unpack_from('<f', ram, PLAYER + 0xA8)[0]
    py = struct.unpack_from('<f', ram, PLAYER + 0xA4)[0]
    for kind in (4, 1, 6, 8, 0, 3, 0xC):
        for dy in (-40.0, -20.0, -5.0, 0.0, 5.0, 20.0, 29.0, 31.0, 40.0):
            add('bt 1AA2A0 dy %d %g' % (kind, dy), B, 0x1AA2A0, [PLAYER, T, fb(20.0)],
                [(T + 0xB0, f32b(px + 3.0)), (T + 0xB8, f32b(pz + 4.0)), (T + 0xB4, f32b(py + dy)), (T + 3, b8(kind))])
    for kind in list(range(0, 16)) + [0x10, 0x44, 0x80, 0x84, 0xFF]:
        for dy in (-10.0, 20.0):
            add('bt 1AA2A0 kind %X %g' % (kind, dy), B, 0x1AA2A0, [PLAYER, T, fb(20.0)],
                [(T + 0xB0, f32b(px + 3.0)), (T + 0xB8, f32b(pz + 4.0)), (T + 0xB4, f32b(py + dy)), (T + 3, b8(kind))])
    for kind in (4, 1):
        for dy in (-8.0, -13.0, 30.0, 10.0):
            add('bt 1AA2A0 edge %X %g' % (kind, dy), B, 0x1AA2A0, [PLAYER, T, fb(20.0)],
                [(T + 0xB0, f32b(px + 3.0)), (T + 0xB8, f32b(pz + 4.0)), (T + 0xB4, f32b(py + dy)), (T + 3, b8(kind))])
    for rad in (5.0, 4.999999, 5.000001):
        add('bt 1AA2A0 rad %r' % rad, B, 0x1AA2A0, [PLAYER, T, fb(rad)],
            [(T + 0xB0, f32b(px + 3.0)), (T + 0xB8, f32b(pz + 4.0)), (T + 0xB4, f32b(py)), (T + 3, b8(4))])
    for dx, dz, rad in ((0.0, 10.0, 3.0), (10.0, 0.0, 3.0), (-10.0, 0.0, 3.0), (0.0, -10.0, 3.0), (5.0, 5.0, 20.0),
                        (30.0, 0.0, 20.0), (0.0, 50.0, 0.0), (1.0, 1.0, 0.0), (0.0, 0.0, 3.0), (60.0, 60.0, 35.0)):
        add('bt 1AA2A0 at %g %g %g' % (dx, dz, rad), B, 0x1AA2A0, [PLAYER, T, fb(rad)],
            [(T + 0xB0, f32b(px + dx)), (T + 0xB8, f32b(pz + dz))])
    add('bt 1F6BA0', B, 0x1F6BA0, [])


def bound_cases(add):
    """The float compares with the compared value exactly at the bound and
    one float either side, where an input reaches the bound exactly (the
    others straddle it with the nearest inputs; LEVEL14_PORT.md section
    3.2). EM_TEST_FULL=1 only beyond DEFAULT_KEEP."""
    # 0x8239F0: the player's y against 310
    n = A15S1[0x823850][0]
    for v in around(310.0):
        add('bound 8239F0 y %08X' % v, A151, 0x8239F0, [n],
            bones(n) + [(n + 5, b8(0)), (0x810354, bits32(v))], queues={0x1B1EA0: [1]}, full=True)
    # 0x825430 / 0x825D10
    ram, _ = CAPTURES[B20]
    for entry in (0x825430, 0x825D10):
        n = A15S0[entry][0]
        bone0 = u32(ram, n + 0x110)
        mir = entry == 0x825D10
        base = bones(n) + [(n + 4, b8(1))]

        def at(xo, zo):
            return ((869.0 - xo) if mir else (869.0 + xo)), ((966.5 - zo) if mir else (951.5 + zo))
        x0, z0 = at(0.0, 3.0)
        # z offset bounds -2 / 0 / 9.5 (exact: the player's z one float either side)
        for zo in (-2.0, 0.0, 9.5):
            x, z = at(0.0, zo)
            for zb in around(z):
                add('bound %06X z %g %08X' % (entry, zo, zb), B20, entry, [n],
                    base + [(0x810350, f32b(x)), (0x810358, bits32(zb)), (n + 0x1F0, w32(1)),
                            (n + 0x1F4, f32b(0.3)), (n + 0x1FC, f32b(0.5)), (n + 0x200, f32b(0.08))], full=True)
        # |x| against 7.7 (straddled: the player's x one float either side of 869 +- 7.7)
        for xo in (7.7, -7.7):
            x, z = at(xo, 3.0)
            for xb in around(x):
                add('bound %06X x %g %08X' % (entry, xo, xb), B20, entry, [n],
                    base + [(0x810350, bits32(xb)), (0x810358, f32b(z)), (n + 0x1F0, w32(1)),
                            (n + 0x1F4, f32b(0.3)), (n + 0x1FC, f32b(0.5)), (n + 0x200, f32b(0.08))], full=True)
        # the pin's x bounds -2 / 2
        for xo, side in ((-2.0, 1), (2.0, -1), (2.0, 1), (-2.0, -1)):
            x, z = at(xo, -1.0)
            for xb in around(x):
                add('bound %06X pin %g %d %08X' % (entry, xo, side, xb), B20, entry, [n],
                    base + [(0x810350, bits32(xb)), (0x810358, f32b(z)), (n + 0x1F0, w32(side)),
                            (bone0 + 0x74, f32b(0.25))], full=True)
        # the angle 0.5235988 (sound 0x404), the phase wraps at +-pi, the tail's 1.3613569
        for sign in (1.0, -1.0):
            for v in around(sign * 0.5235988):
                add('bound %06X 404 %08X' % (entry, v), B20, entry, [n],
                    base + [(0x810350, f32b(x0)), (0x810358, f32b(z0)), (n + 0x1F0, w32(1)),
                            (n + 0x204, f32b(0.0)), (bone0 + 0x74, bits32(v)), (n + 0x200, f32b(0.0))], full=True)
            for v in around(sign * 3.1415927):
                add('bound %06X wrap %08X' % (entry, v), B20, entry, [n],
                    base + [(0x810350, f32b(x0)), (0x810358, f32b(z0)), (n + 0x1F0, w32(1)),
                            (n + 0x1FC, bits32(v)), (n + 0x200, f32b(0.0)), (n + 0x1F4, f32b(0.3))], full=True)
                xf, zf = at(9.0, 3.0)
                add('bound %06X free wrap %08X' % (entry, v), B20, entry, [n],
                    base + [(0x810350, f32b(xf)), (0x810358, f32b(zf)), (n + 0x1FC, bits32(v)),
                            (n + 0x200, f32b(0.0)), (n + 0x1F4, f32b(0.3))], full=True)
            for v in around(sign * 1.3613569):
                xp, zp = at(0.0, -1.0)
                add('bound %06X tail %08X' % (entry, v), B20, entry, [n],
                    base + [(0x810350, f32b(xp)), (0x810358, f32b(zp)), (n + 0x1F0, w32(0)),
                            (bone0 + 0x74, bits32(v))], queues={0x1B1630: [1]}, full=True)
            for v in around(sign * 0.02):
                xf, zf = at(9.0, 3.0)
                add('bound %06X free amp %08X' % (entry, v), B20, entry, [n],
                    base + [(0x810350, f32b(xf)), (0x810358, f32b(zf)), (n + 0x1F4, bits32(v)),
                            (n + 0x1FC, f32b(0.5)), (n + 0x200, f32b(0.08))], full=True)
            # the free swing's zero crossings: the old or the new phase exactly 0
            if sign > 0:
                xf, zf = at(9.0, 3.0)
                for before, step in ((0.0, 0.08), (0.0, -0.08), (0.08, -0.08), (-0.08, 0.08)):
                    add('bound %06X free zero %g %g' % (entry, before, step), B20, entry, [n],
                        base + [(0x810350, f32b(xf)), (0x810358, f32b(zf)), (n + 0x1F4, f32b(0.3)),
                                (n + 0x1FC, f32b(before)), (n + 0x200, f32b(step)), (n + 0x1F0, w32(0))], full=True)
    # 00131740: (float)t - +0x3C against 65
    B = B00
    n = CREATURES[B][1]
    e = n + 0x1F0
    for v in around(35.0):
        add('bound 131740 65 %08X' % v, B, 0x131740, [n, e],
            bones(n) + [(n + 6, b8(0)), (e + 0x61, b8(1)), (n + 0x2C, h16(4)), (n + 0x3C, bits32(v))],
            queues={0x1C6160: [100]}, full=True)
    # 00131B10: +0x3C against 34 / 21 (equality)
    for clip, f in ((0x2A, 34.0), (0x2B, 21.0)):
        for v in around(f):
            add('bound 131B10 %X %08X' % (clip, v), B, 0x131B10, [n, e],
                bones(n) + [(n + 6, b8(1)), (n + 0x2C, h16(clip)), (n + 0x3C, bits32(v))], full=True)
    # 001782A0: t against 0 (y = the probed y - 3 exactly, and one float either side)
    for v in around(197.0):
        add('bound 1782A0 t %08X' % v, B12, 0x1782A0, [PLAYER],
            [(PLAYER + 0xB4, bits32(v)), (PLAYER + 0xC4, f32b(1.25))],
            queues={0x199FA0: [Scribble(1, [(STACK_TOP - 0x20, fw(1.0, 200.0, 3.0, 1.0)),
                                            (STACK_TOP - 0x10, fw(0.0, 1.0, 0.0, 0.0))])]}, full=True)
    # 001AA2A0: the radius (d = 5 exactly) and the dy band ends one float either side
    ram, _ = CAPTURES[B08]
    T = 0x7AA2A0
    px, py, pz = struct.unpack_from('<fff', ram, PLAYER + 0xA0)
    for rad in around(5.0):
        add('bound 1AA2A0 rad %08X' % rad, B08, 0x1AA2A0, [PLAYER, T, rad],
            [(T + 0xB0, f32b(px + 3.0)), (T + 0xB8, f32b(pz + 4.0)), (T + 0xB4, f32b(py)), (T + 3, b8(4))], full=True)
    for kind, dy in ((4, -8.0), (4, 30.0), (1, -13.0), (1, 10.0)):
        for oy in around(struct.unpack('<f', struct.pack('<f', py + dy))[0]):
            add('bound 1AA2A0 dy %X %g %08X' % (kind, dy, oy), B08, 0x1AA2A0, [PLAYER, T, fb(20.0)],
                [(T + 0xB0, f32b(px + 3.0)), (T + 0xB8, f32b(pz + 4.0)), (T + 0xB4, bits32(oy)), (T + 3, b8(kind))],
                full=True)
    # 001B2D00: a height exactly at the probe's y and one float either side
    for v in around(10.0):
        add('bound 1B2D00 %08X' % v, B07, 0x1B2D00, [FREE + 0x100, FREE + 0x200],
            [(FREE + 0x100, fw(1.0, 10.0, 2.0, 1.0)), (0x700031E0, w32(2)), (0x700030F0, f32b(1.0)),
             (0x700030F4, bits32(v)), (0x70003170, h16(0)), (0x70003172, h16(1)), (0x7000316E, h16(1))], full=True)
    # 001EDE40: the eased value against its floor 0.02
    for v in around(0.02):
        add('bound 1EDE40 %08X' % v, B07, 0x1EDE40, [FREE + 0x100, FREE + 0x200],
            [(0x275C34, w32(FREE + 0x400)), (FREE + 0x404, w32(7)), (FREE + 0x408, bits32(v)),
             (FREE + 0x454, f32b(2.5))], full=True)


def survivor_cases(add):
    """The mutation sweep's survivor cases (LEVEL14_PORT.md section 3.4):
    the swinging gates' amplitude * sin(phase) exactly equal to the target
    (the amplitude found with the EE model so that the product is the
    target's bits), in both branches of both gates."""
    ram, _ = CAPTURES[B20]
    for entry in (0x825430, 0x825D10):
        n = A15S0[entry][0]
        mir = entry == 0x825D10
        for xo, side_pos, amp in ((3.0, 1, 0x3F320F30), (-3.0, -1, 0xBF320F30)):
            side = -side_pos if mir else side_pos
            x = (869.0 - xo) if mir else (869.0 + xo)
            z = (966.5 - 8.0) if mir else (951.5 + 8.0)
            add('survivor %06X a == t %g' % (entry, xo), B20, entry, [n],
                bones(n) + [(n + 4, b8(1)), (0x810350, f32b(x)), (0x810358, f32b(z)), (n + 0x1F0, w32(side)),
                            (n + 0x204, f32b(1.0)), (n + 0x1F4, bits32(amp)), (n + 0x1FC, f32b(0.5)),
                            (n + 0x200, f32b(0.08))], queues={0x1B1630: [1]})


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
    # 0x825430 / 0x825D10: the clamps of the target after its product with
    # (9.5 - z): in the positive branch the target was clamped to >= +0 and
    # 9.5 - z > 0 inside the box, so the product is never below 0; in the
    # negative branch it was clamped to <= 0 (or +0), so the product is never
    # above 0 (the EE has no negative zero from x + (-x) or a +0 product).
    # Each is the branch's two not-taken words (located by the cases that
    # run the neighbouring words: the preceding 79-degree clamp, the join).
    0x8257E0: 'pos product >= 0', 0x8257E4: 'pos product >= 0',
    0x825968: 'neg product <= 0', 0x82596C: 'neg product <= 0',
    0x8260D0: 'neg product <= 0 (mirrored)', 0x8260D4: 'neg product <= 0 (mirrored)',
    0x826258: 'pos product >= 0 (mirrored)', 0x82625C: 'pos product >= 0 (mirrored)',
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
    0x826850 on a15_01's 0x7A7690 (+0xD 0xC) in state 1 with D_008107FB 0
    calls 001C6380, 001B17A0 and the +0x4C method."""
    ram, spad = CAPTURES[A151]
    node = 0x7A7690
    ram = bytearray(ram)
    spad = bytearray(spad)
    ram[node + 4] = 1
    ram[0x8107FB] = 0
    problems = []
    calls_want = ['w_001C6380', 'w_001B17A0', 'w_callback']

    def attempt(null=None, failing=None, unmapped=None, latched=False, entry=0x826850, args=(node,)):
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
    status, address, code, calls, same = attempt(unmapped=FREE + 0x28, entry=0x827540, args=(FREE,))
    if (status, address, code) != (-1, FREE + 0x28, 5) or calls or not same:
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
    """The fail-stop contract of the header (em_level14_port.h), on one
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
    where each wrapper's own `l14_failed` test is observable. Returns
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
   'a15 8235A0 s0 0',
   'a15 8235A0 s0 1',
   'a15 8235A0 s1 0 +5 1 1 0',
   'a15 8235A0 s1 2 +5 2 0 0',
   'a15 8235A0 s2',
   'a15 8235A0 s3',
   'a15 8235A0 s4',
   'a15 8236B0 +5 0 0 0',
   'a15 8236B0 +5 1 0 0',
   'a15 8236B0 +5 FF 0 0',
   'a15 823780 +5 0 0 FF',
   'a15 823780 +5 1 0 0',
   'a15 823780 +5 1 1 0',
   'a15 823780 +5 FF 0 0',
   'a15 823850 s0 @7A7F60 None [0, 1]',
   'a15 823850 s0 @7A8540 None [0, 0]',
   'a15 823850 s0 @7A8540 None [0, 1]',
   'a15 823850 s0 @7A8540 None [1]',
   'a15 823850 s1 @7A8540 FB 1',
   'a15 823850 s1 @7A8540 FB 2',
   'a15 823850 s1 @7A8540 FB 3',
   'a15 823850 s1 @7A8540 FB 4',
   'a15 823850 s1 @7A8540 FB 5',
   'a15 823850 s2 @7A8540',
   'a15 823850 s3 @7A8540',
   'a15 823850 s4 @7A8540',
   'a15 8239F0 @7A7F60 +5 0 400 1 0 1',
   'a15 8239F0 @7A7F60 +5 0 400 2 0 0',
   'a15 8239F0 @7A7F60 +5 1 400 1 0 0',
   'a15 8239F0 @7A7F60 +5 1 400 1 1 0',
   'a15 8239F0 @7A7F60 +5 2 400 1 0 0',
   'a15 823E40 @7A8540 F7 0 80000000',
   'a15 823E40 @7A8540 FFF7 0 0',
   'a15 823E40 @7A8540 FFFC 0 0',
   'a15 824070 10 s0 [1, 0, 0] 0',
   'a15 824070 50 s0 [1, 0, 0] 0',
   'a15 824070 50 s1 FC 0',
   'a15 824070 50 s1 FC 1',
   'a15 824070 64 s0 [0] 0',
   'a15 824070 64 s0 [1, 0, 0] 0',
   'a15 824070 64 s0 [1, 0, 1] 0',
   'a15 824070 64 s0 [1, 1] 0',
   'a15 824070 64 s1 FC 0',
   'a15 824070 64 s1 FC 1',
   'a15 824070 64 s1 FC 2',
   'a15 824070 64 s2',
   'a15 824070 64 s3',
   'a15 824070 64 s4',
   'a15 824510 10 s4',
   'a15 824510 50 s1 +5 1 0 208 1',
   'a15 824510 64 s1 +5 0 4 0 0',
   'a15 824560 64 s0 [0, 0, 0] 0',
   'a15 824560 64 s0 [0, 0, 1] 0',
   'a15 824560 64 s0 [0, 1] 0',
   'a15 824560 64 s0 [1] 0',
   'a15 824560 64 s1 +5 0 FB 0 0',
   'a15 824560 64 s1 +5 1 0 207 1',
   'a15 824560 64 s1 +5 1 0 FFFF 0',
   'a15 824560 64 s1 +5 2 0 0 0',
   'a15 824560 64 s2',
   'a15 824560 64 s3',
   'a15 824560 64 s4',
   'a15 8247B0 50 s0 [0, 0, 0] 0',
   'a15 8247B0 50 s0 [0, 0, 1] 0',
   'a15 8247B0 50 s0 [0, 1] 0',
   'a15 8247B0 50 s0 [1] 0',
   'a15 8247B0 50 s1 +5 0 4 0 0',
   'a15 8247B0 50 s1 +5 0 FB 0 0',
   'a15 8247B0 50 s1 +5 1 0 FFFF 0',
   'a15 8247B0 50 s1 +5 2 0 0 0',
   'a15 8247B0 50 s2',
   'a15 8247B0 50 s3',
   'a15 8247B0 50 s4',
   'a15 824990 50 s0 [1] 0',
   'a15 824990 5A s0 [0] 0',
   'a15 824990 5A s0 [1] 0',
   'a15 824990 5A s1 [0] FF 0',
   'a15 824990 5A s1 [0] FF 1',
   'a15 824990 5A s1 [0] FF 2',
   'a15 824990 5A s1 [1] FF 0',
   'a15 824990 5A s2',
   'a15 824990 5A s3',
   'a15 824990 5A s4',
   'a15 824E00 50 s1 +5 3 1 0 1 FB 0',
   'a15 824E00 59 s0 [0, 1] 1',
   'a15 824E50 59 s0 [0, 0] 0',
   'a15 824E50 59 s0 [1] 0',
   'a15 824E50 59 s1 +5 0 0 0 0 0 0',
   'a15 824E50 59 s1 +5 0 1 0 0 0 0',
   'a15 824E50 59 s1 +5 0 1 1 0 0 0',
   'a15 824E50 59 s1 +5 1 1 0 0 4 0',
   'a15 824E50 59 s1 +5 1 1 0 1 FB 1',
   'a15 824E50 59 s1 +5 2 1 0 0 4 1',
   'a15 824E50 59 s1 +5 3 1 0 0 4 0',
   'a15 824E50 59 s2',
   'a15 824E50 59 s3',
   'a15 824E50 59 s4',
   'a15 825030 50 s0 [0, 0] 1',
   'a15 825030 50 s0 [0, 1] 0',
   'a15 825030 50 s0 [0, 1] 1',
   'a15 825030 50 s0 [1] 1',
   'a15 825030 50 s1 +5 0 1 0 0 0 0',
   'a15 825030 50 s1 +5 0 1 1 1 0 0',
   'a15 825030 50 s1 +5 1 1 0 1 4 1',
   'a15 825030 50 s1 +5 1 1 0 1 FB 1',
   'a15 825030 50 s1 +5 2 1 0 1 FB 0',
   'a15 825030 50 s1 +5 2 1 0 1 FB 1',
   'a15 825030 50 s2',
   'a15 825030 50 s3',
   'a15 825030 50 s4',
   'a15 8252D0 @7A8830 +3 FF',
   'a15 825320 s0 FFFFFFFB',
   'a15 825320 s2',
   'a15 825320 s3',
   'a15 825320 s4',
   'a15 825430 out 0 -2.5 0.5 0.05 -0.08',
   'a15 825430 out 0 9.6 -0.4 3.1 0.08',
   'a15 825430 out 7.8 3 0.5 -0.05 0.08',
   'a15 825430 pin -3 -1 1 0.3',
   'a15 825430 pin 0 -1 -1 0.3',
   'a15 825430 pin 3 -1 -1 0.3',
   'a15 825430 push -6 3 1 1 0 0.6 0.2 3.1 0.08 1',
   'a15 825430 push 2 3 0 -1 0 0.3 0.2 0.5 0.08 0',
   'a15 825430 push 3 3 -1 1 0 0.1 -0.2 -3.1 -0.08 1',
   'a15 825430 push 7.5 9 -1 1 0 0.1 -0.5 -0.3 0.08 1',
   'a15 825430 s0',
   'a15 825430 s2',
   'a15 825430 s3',
   'a15 825430 s4',
   'a15 825430 sweep 15',
   'a15 825430 sweep 28',
   'a15 825430 sweep 33',
   'a15 825430 sweep 45',
   'a15 825430 sweep 63',
   'a15 825430 sweep 78',
   'a15 825430 sweep 96',
   'a15 825D10 out 0 -2.5 0.5 0.05 -0.08',
   'a15 825D10 out 0 9.6 0.3 -3.1 -0.08',
   'a15 825D10 out 7.8 3 0.5 -0.05 0.08',
   'a15 825D10 pin -3 -1 -1 0.3',
   'a15 825D10 pin 3 -1 -1 0.3',
   'a15 825D10 push -6 3 1 1 0 0.6 0.2 3.1 0.08 1',
   'a15 825D10 push 2 3 0 0 1 0.6 0.2 0.5 0.08 1',
   'a15 825D10 push 3 3 -1 1 0 0.1 -0.2 -3.1 -0.08 1',
   'a15 825D10 push 4.5 8 -1 1 0 0.1 1.2 1 0 1',
   'a15 825D10 s0',
   'a15 825D10 s2',
   'a15 825D10 s3',
   'a15 825D10 s4',
   'a15 825D10 sweep 107',
   'a15 825D10 sweep 109',
   'a15 825D10 sweep 25',
   'a15 825D10 sweep 3',
   'a15 825D10 sweep 4',
   'a15 825D10 sweep 65',
   'a15 825D10 sweep 89',
   'a15 825D10 sweep 92',
   'a15 825D10 sweep 97',
   'a15 826600 s0 [0]',
   'a15 826600 s0 [1]',
   'a15 826600 s1 [0, 1] 0',
   'a15 826600 s1 [0] FF',
   'a15 826600 s1 [1] 0',
   'a15 826600 s2',
   'a15 826600 s3',
   'a15 826600 s4',
   'a15 826850 s0 @7A7C70 D',
   'a15 826850 s0 @7A7C70 E',
   'a15 826850 s1 @7A7C70 C 80',
   'a15 826850 s1 @7A7C70 E 80',
   'a15 826850 s2 @7A7C70',
   'a15 826850 s3 @7A7C70',
   'a15 826850 s4 @7A7C70',
   'bt 16AC50 s0 0 0',
   'bt 16AC50 s1 1000 0',
   'bt 16AC50 s1 EFFF 0',
   'bt 16AC50 s2 0 0',
   'bt 16AC50 s2 0 3',
   'bt 16AC50 s3 1000 0',
   'bt 16AC50 s3 FFF 0',
   'bt 1782A0 a19d_12_post 219 222 1 1',
   'bt 1782A0 a19d_12_post 219 230 1 0',
   'bt 1AA2A0 dy 6 -40',
   'bt 1AA2A0 dy 8 -5',
   'bt 1AA2A0 dy 8 40',
   'bt 1AA2A0 edge 1 10',
   'bt 1AA2A0 kind 4 -10',
   'bt 1AA2A0 kind FF -10',
   'bt 1AA2A0 kind FF 20',
   'bt 1AA2A0 rad 5.000001',
   'bt 1AA2A0 target',
   'bt 1AA410 kind 1',
   'bt 1AA410 kind 12',
   'bt 1AA410 kind 2',
   'bt 1AA410 kind 5',
   'bt 1AA410 kind 6',
   'bt 1AA410 kind 7',
   'bt 1AA410 kind 8',
   'bt 1AA410 kind 9',
   'bt 1AA410 target',
   'bt 1B2D00 -1 (1.0,) (1,) 10',
   'bt 1B2D00 0 () () 10',
   'bt 1B2D00 1 (5.0,) (0,) 10',
   'bt 1B2D00 1 (5.0,) (1,) 10',
   'bt 1B2D00 2 (1.0, 5.0) (3, 0) 10',
   'cr 131650 +5 2 0 0',
   'cr 131650 +5 6 1E41000 0',
   'cr 131740 s0 0 4 0 0 1 8000 0',
   'cr 131740 s0 1 4 100 40 0 0 0',
   'cr 131740 s0 1 4 85 20 0 0 0',
   'cr 131740 s0 1 5 100 20 0 0 0',
   'cr 131740 s1 1000 1 0',
   'cr 131740 s1 11000 0 1',
   'cr 131740 s1 EFFF 1 1',
   'cr 131B10 s0 0 0 1FF',
   'cr 131B10 s0 0 1 0',
   'cr 131B10 s0 2 0 0',
   'cr 131B10 s0 FD 0 FFFFFE00',
   'cr 131B10 s0 FF 1 0',
   'cr 131B10 s1 2A 21 0 0 0',
   'cr 131B10 s1 2A 34 0 0 0',
   'cr 131B10 s1 2B 21 0 0 0',
   'cr 131B10 s1 2B 34 0 0 0',
   'cr 131B10 s1 2C 34 1000 0 0',
   'cr 131B10 s1 2C 34 1000 0 7AF000',
   'cr 131B10 s1 2C 34 1000 2 0',
   'cr 131B10 s2 2 0 2 1E41000',
   'cr 131B10 s2 2 1 2 0',
   'cr 131B10 s2 2 80 8002 1E41000',
   'cr 131B10 s2 2 FF 10002 1E41000',
   'cr 131B10 s3 1 0',
   'cr 131B10 s3 2 0',
   'cr 131B10 s3 2 200',
   'cr 131E80 1E41000',
   'cr 1339E0 FF',
   'cr 153950 s0 FF',
   'cr 153950 s2 0',
   'cr 153950 s3 FF',
   'cr 153950 s6 0',
   'cr 153A10 0',
   'cr 153A10 5',
   'cr 153A90 0 1 1',
   'cr 153A90 5 0 0',
   'cr 153A90 5 1 0',
   'cr 1B4CF0 k1 0',
   'cr 1B4CF0 k1 80',
   'cr 1B4CF0 k10 81',
   'cr 1B4CF0 k11 81',
   'cr 1B4CF0 k3 81',
   'cr 1B4CF0 k4 1',
   'cr 1B4CF0 k4 81',
   'cr 1B4CF0 k5 1',
   'cr 1B4CF0 k5 81',
   'cr 1B4CF0 k6 1',
   'cr 1B4CF0 k6 81',
   'cr 1B4CF0 k7 1',
   'cr 1B4CF0 k7 81',
   'cr 1B4CF0 k8 81',
   'cr 1B4CF0 k9 81',
   'cr 1B4CF0 kFF',
])
SURVIVORS = ('survivor ', 'bound ')


def select_cases(elf):
    """The mode's case list (EM_TEST_FULL=1: all; the default run keeps
    every capture case, the designed cases of DEFAULT_KEEP and the mutation
    sweep's survivor cases)."""
    cases, captured, targeted = case_list(elf)
    total = len(cases)
    if not reference_mode.FULL and not os.environ.get('EM_LEVEL14_PORT_ALL'):
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
    """An image holding the entry's code (AREA15 entries: a15_01; AREA19
    entries: a19d_07; boot text is the same in all)."""
    if entry >= ARENA:
        return CAPTURES[A151 if OVERLAY_OF[entry] == 12 else B07][0]
    return CAPTURES[B07][0]


def main():
    global NATIVE
    elf = read_elf()
    CAPTURES.update(load_captures(elf))
    NATIVE = build(os.environ.get('EM_LEVEL14_PORT_SOURCE'))
    selected, captured, targeted, total = select_cases(elf)
    CASES[:] = selected
    only = os.environ.get('EM_LEVEL14_PORT_ONLY')   # debugging: a label prefix
    if only:
        CASES[:] = selected = [c for c in selected if c[0].startswith(only)]
    trace = os.environ.get('EM_LEVEL14_PORT_TRACE')
    if trace:   # lane tool: the original's behaviour trace of the matching cases, then stop
        for case in selected:
            if case[0].startswith(trace):
                print_trace(case)
        return
    results = run_cases(selected)
    failures, per_fn, seen, totals = [], {}, set(), [0, 0, 0, 0]
    header_problems, header_count = header_checks(os.environ.get('EM_LEVEL14_PORT_SOURCE'))
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
    if os.environ.get('EM_LEVEL14_PORT_PCS'):   # lane tool: which cases executed these original words
        for pc in [int(x, 16) for x in os.environ['EM_LEVEL14_PORT_PCS'].split(',')]:
            hits = [label for label, _, _, pcs, _, _ in results if pc in pcs]
            print('  pc %X: %d cases %s' % (pc, len(hits), hits[:6]))
    if os.environ.get('EM_LEVEL14_PORT_KEEP'):   # lane tool: the coverage pass that picks DEFAULT_KEEP
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
    outcomes = {}
    for item in seen:
        if isinstance(item, tuple):
            outcomes.setdefault(item[1], set()).add(item[2])
    one_way = sorted(pc for pc, ways in outcomes.items() if len(ways) == 1)
    print('  branch outcomes: %d conditional branches executed, %d both ways%s'
          % (len(outcomes), len(outcomes) - len(one_way),
             (', one way only: ' + ' '.join('%X%s' % (pc, '+' if True in outcomes[pc] else '-') for pc in one_way))
             if one_way and (reference_mode.FULL or os.environ.get('EM_LEVEL14_PORT_ALL')) else ''))
    if os.environ.get('EM_LEVEL14_PORT_MISSING'):
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
        if os.environ.get('EM_LEVEL14_PORT_NOCONTRACT'):   # debugging only: skips the contract
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
    for label, errors in failures[:int(os.environ.get("EM_LEVEL14_PORT_SHOW", "12"))]:
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


def print_trace(case):
    """The original's run of one case as a behaviour trace: per call
    interval its loads (address, size, value) and stores (address, size,
    value), then the call (callee, arguments, result). No instruction text."""
    label, beat, entry, native_args, patches, spad_patches, script, run_set = case
    ram0, spad0 = CAPTURES[beat]
    ram, spad = patched(ram0, patches), patched(spad0, spad_patches)
    try:
        oracle = Oracle(ram, spad, entry, (native_args, callbacks_of(entry, native_args, ram)), copy_script(script),
                        run_set, set())
    except AssertionError as error:
        print('TRACE', label, 'oracle failed', error)
        return
    print('TRACE %s (%s %06X %s)%s' % (label, beat, entry, ' '.join('%X' % a for a in native_args),
                                      ' stopped at %X' % oracle.stopped if oracle.stopped is not None else ''))
    for item in oracle.trace:
        if item[0] == 'C':
            print('   call %s(%s) -> %s' % (item[1], ', '.join('%X' % v for v in item[2]),
                                           'none' if item[3] is None else '%X' % item[3]))
        else:
            print('   %s %X/%d %X' % item if item[0] != 'l' else '   frame load %X/%d at %X' % item[1:])
    print('   return v0 %X f0 %X' % (oracle.ee.r[2] & MASK, oracle.ee.f[0] & MASK))


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
