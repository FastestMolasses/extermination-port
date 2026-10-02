#!/usr/bin/env python3
"""Execute the ORIGINAL tenth-level code (the a19 and a13b census rows:
AREA19's door [27], [6], [43], [7], [9], [47], [18], [10] and the probe
0x829370; AREA13's load-time 0x8236E0; the boot functions 00118790,
001305B0, 001833F0, 001885F0 and 001E6F60) and compare em_level10_port*.c.

docs/LEVEL10_PORT.md. The census deltas of the tenth-level route (decomp
build/s87/census/a19_delta.json and a13b_delta.json, new_functions: 13 + 5
rows) name 18 functions. 17 have no verified port translation and are
translated in em_level10_port_*.c; they are the entries below (FUNCS). One
(AREA19 0x8298D0, whose instructions equal AREA01 0x828850's) is reused
(em_area01_ovl_00828850) and re-run against the AREA19 original by
reuse_checks. Calls between the translations run as original code at the
top level of the oracle (only other functions are hooks), and every
function is also an entry of its own.

The harness is tools/test_level9_port_reference.py's (lane L9T, itself the
LEVEL8 / AREA06 / AREA22 / AREA04 design), copied and owned here, with the
tenth-level captures, this module's hook table and cases. Entries that take
the original stack pointer (kind S) run the oracle with sp = STACK_TOP and
give the native entry STACK_TOP; the frame locals live in the compared
stack window.

Oracle: the shared EE interpreter with the measured float model (FallEE,
tools/test_player_fall_reference.py) runs the original code resident in the
recorded RAM images of the tenth-level route (decomp build/s87/route_a19/
<beat>/ and route_a13b/<beat>/, plus the ninth level's last image
route_a13/a13_05_shaft, the AREA19 arrival at entry 9). Before any case the
test checks that the overlay text of every image equals the user's
extract/OVERLAY/<AREA>.BIN (text size from the file header), that the boot
text below 0x241000 equals the pinned ELF and that the jump table equals
its file, so every executed instruction is original. Nothing here embeds
original bytes; reports hold counts only.

Callees (every function outside this module and the +0x4C method) are
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

EM_LEVEL10_PORT_ONLY=<label prefix> runs a subset (no coverage / contract /
reuse checks). EM_LEVEL10_PORT_SOURCE=<dir> tests another copy of the
module sources (the mutation sweep). EM_LEVEL10_PORT_MISSING=1 lists
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
OVERLAY_FILES = {5: DECOMP / 'extract/OVERLAY/AREA04.BIN', 10: DECOMP / 'extract/OVERLAY/AREA13.BIN',
                 16: DECOMP / 'extract/OVERLAY/AREA19.BIN'}
OUT = ROOT / 'build/level10/port'
MASK = 0xFFFFFFFF
MASK64 = 0xFFFFFFFFFFFFFFFF
RAM_SIZE, SPAD_SIZE = 0x2000000, 0x4000
ARENA = 0x823500
STACK_LO = STACK_TOP - 0x800
STACK_SIZE = STACK_TOP - STACK_LO
BEAT_DIRS = {'a13_05_shaft': DECOMP / 'build/s87/route_a13/a13_05_shaft'}
for _b in ('a19_00_duct', 'a19_01_pickup_g2', 'a19_02_duct_back'):
    BEAT_DIRS[_b] = DECOMP / 'build/s87/route_a19' / _b
for _b in ('a13b_00_ladder_up', 'a13b_01_door17', 'a13b_02_button15', 'a13b_03_door8', 'a13b_04_lift_call',
           'a13b_05_lift_ride', 'a13b_s0_roof_ladder'):
    BEAT_DIRS[_b] = DECOMP / 'build/s87/route_a13b' / _b
BEATS = list(BEAT_DIRS)
# The overlay each image holds (header id at 0x823504): AREA19 (16) from the
# arrival at entry 9 to the ladder's foot, AREA13 (10) after the climb, and
# AREA04 (5) at the end of the lift ride.
RESIDENT = {b: (16 if b.startswith(('a19', 'a13_05')) else 10) for b in BEATS}
RESIDENT['a13b_05_lift_ride'] = 5
A19_BEATS = [b for b in BEATS if RESIDENT[b] == 16]
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
    # AREA19 overlay (runtime addresses)
    0x823580: ('em_level10_port_00823580', 0x1EC, [U], None),
    0x8250F0: ('em_level10_port_008250F0', 0x150, [U, S], None),
    0x825240: ('em_level10_port_00825240', 0x1D4, [U, S], None),
    0x8255D0: ('em_level10_port_008255D0', 0x1EC, [U], None),
    0x8257C0: ('em_level10_port_008257C0', 0x164, [U], None),
    0x825AB0: ('em_level10_port_00825AB0', 0x1B8, [U], None),
    0x825C70: ('em_level10_port_00825C70', 0x270, [U], None),
    0x825EE0: ('em_level10_port_00825EE0', 0x21C, [U], None),
    0x826100: ('em_level10_port_00826100', 0x364, [U], None),
    0x827790: ('em_level10_port_00827790', 0x250, [U, S], None),
    0x829370: ('em_level10_port_00829370', 0x4D0, [U, U, S], I),
    # AREA13 overlay
    0x8236E0: ('em_level10_port_008236E0', 0x20, [], None),
    # boot
    0x118790: ('em_level10_port_00118790', 0x98, [U], I),
    0x1305B0: ('em_level10_port_001305B0', 0x4F8, [U, U], None),
    0x1833F0: ('em_level10_port_001833F0', 0x48, [U], None),
    0x1885F0: ('em_level10_port_001885F0', 0x1C, [U], I),
    0x1E6F60: ('em_level10_port_001E6F60', 0xEC, [I, I, I, I, I, I, I], None),
}
OVERLAY_OF = {entry: (10 if entry == 0x8236E0 else 16) for entry in FUNCS if entry >= ARENA}
# The entries whose passing cases are all contract sites (they return from
# their own body): the default run and EM_TEST_FULL=1.
RETURN_SITES = (0x118790, 0x1885F0, 0x829370)
RETURN_SITES_FULL = RETURN_SITES


def has_result(entry):
    return FUNCS[entry][3] is not None


# Hooks, in the field order of EmLevel10PortHooks (after ctx and bytes; the
# +0x4C method's w_callback follows them).
# name -> (original address, [(ctype, register)], result kind or None)
HOOKS = [
# BEGIN GENERATED HOOKS
    ('w_001026A0', 0x1026A0, [(U, 4), (U, 5), (U, 6)], None),
    ('w_00102738', 0x102738, [(U, 4), (U, 5)], F),
    ('w_001028B8', 0x1028B8, [(U, 4), (U, 5), (U, 6)], None),
    ('w_001028D0', 0x1028D0, [(U, 4), (U, 5), (U, 6)], None),
    ('w_00102948', 0x102948, [(U, 4), (U, 5)], None),
    ('w_001031E0', 0x1031E0, [(U, 4), (U, 5)], None),
    ('w_00122BB8', 0x122BB8, [], I),
    ('w_00131F20', 0x131F20, [(U, 4), (U, 5), (U, 6)], None),
    ('w_00132490', 0x132490, [(U, 4), (U, 5)], None),
    ('w_001662D0', 0x1662D0, [(U, 4)], None),
    ('w_001831F0', 0x1831F0, [(I, 4)], None),
    ('w_0019A570', 0x19A570, [(U, 4), (U, 5), (I, 6), (I, 7)], I),
    ('w_0019AA80', 0x19AA80, [(U, 4), (U, 5), (I, 6)], I),
    ('w_0019C6F0', 0x19C6F0, [(I, 4), (I, 5)], I),
    ('w_001A7B80', 0x1A7B80, [(U, 4)], I),
    ('w_001AEE10', 0x1AEE10, [(I, 4), (I, 5)], None),
    ('w_001AFC10', 0x1AFC10, [(U, 4)], None),
    ('w_001B0F60', 0x1B0F60, [(U, 4), (I, 5)], I),
    ('w_001B0FD0', 0x1B0FD0, [(U, 4)], I),
    ('w_001B10B0', 0x1B10B0, [(U, 4), (I, 5), (I, 6)], I),
    ('w_001B1240', 0x1B1240, [(U, 4), (F, 12), (F, 13)], F),
    ('w_001B12B0', 0x1B12B0, [(F, 12), (F, 13), (F, 14)], F),
    ('w_001B1560', 0x1B1560, [(U, 4), (U, 5), (F, 12)], I),
    ('w_001B17A0', 0x1B17A0, [(U, 4)], I),
    ('w_001B1E20', 0x1B1E20, [(I, 4), (I, 5)], None),
    ('w_001B1EA0', 0x1B1EA0, [(I, 4), (U, 5), (U, 6), (I, 7)], I),
    ('w_001B2B10', 0x1B2B10, [(U, 4), (U, 5), (U, 6)], None),
    ('w_001B3250', 0x1B3250, [(U, 4), (U, 5), (F, 12)], I),
    ('w_001B55E0', 0x1B55E0, [(U, 4), (I, 5)], None),
    ('w_001B6660', 0x1B6660, [(U, 4)], U),
    ('w_001BA1A0', 0x1BA1A0, [(U, 4), (U, 5)], None),
    ('w_001BA1C0', 0x1BA1C0, [(U, 4), (I, 5)], I),
    ('w_001BA1F0', 0x1BA1F0, [(U, 4)], I),
    ('w_001BBDA0', 0x1BBDA0, [(U, 4)], None),
    ('w_001BBE40', 0x1BBE40, [(U, 4), (U, 5), (I, 6)], I),
    ('w_001BC0E0', 0x1BC0E0, [(U, 4), (U, 5)], I),
    ('w_001BC240', 0x1BC240, [(U, 4), (U, 5)], None),
    ('w_001BC290', 0x1BC290, [(U, 4), (U, 5)], I),
    ('w_001BC300', 0x1BC300, [(U, 4)], None),
    ('w_001C4760', 0x1C4760, [(I, 4), (I, 5)], I),
    ('w_001C5570', 0x1C5570, [(U, 4), (U, 5), (I, 6), (I, 7)], U),
    ('w_001C6120', 0x1C6120, [(U, 4), (I, 5)], U),
    ('w_001C62C0', 0x1C62C0, [(U, 4)], None),
    ('w_001C6380', 0x1C6380, [(U, 4)], None),
    ('w_001C63E0', 0x1C63E0, [(U, 4), (I, 5)], None),
    ('w_001C64F0', 0x1C64F0, [(U, 4), (F, 12)], I),
    ('w_001C67E0', 0x1C67E0, [(U, 4), (I, 5), (F, 12), (F, 13)], None),
    ('w_001C68C0', 0x1C68C0, [(U, 4)], None),
    ('w_001CA6E0', 0x1CA6E0, [(U, 4), (U, 5)], None),
    ('w_001CA6F0', 0x1CA6F0, [(U, 4), (I, 5)], None),
    ('w_001CD520', 0x1CD520, [(I, 4), (I, 5), (U, 6), (Q, 7), (U, 8), (F, 12), (F, 13), (F, 14)], I),
    ('w_001E2BA0', 0x1E2BA0, [(U, 4), (U, 5), (U, 6), (F, 12)], None),
    ('w_001E8B40', 0x1E8B40, [(I, 4)], None),
    ('w_001EFD90', 0x1EFD90, [(I, 4), (U, 5), (U, 6)], None),
    ('w_001EFE00', 0x1EFE00, [(I, 4), (U, 5)], I),
    ('w_001F5940', 0x1F5940, [(I, 4), (U, 5), (I, 6)], None),
    ('w_001FAE70', 0x1FAE70, [(I, 4)], None),
    ('w_001FBD50', 0x1FBD50, [(U, 4), (I, 5), (I, 6), (F, 12)], I),
    ('w_001FC580', 0x1FC580, [(U, 4), (I, 5)], None),
    ('w_0021BE40', 0x21BE40, [(U, 4)], I),
    ('w_00825420', 0x825420, [(U, 4)], None),
    ('w_00825930', 0x825930, [(U, 4)], None),
# END GENERATED HOOKS
]
BY_ADDRESS = {address: (name, args, result) for name, address, args, result in HOOKS}

# Callees that run as original code inside the oracle (writes replayed to
# the native side). Pure helpers only: none of them reaches hardware; each
# is first rehearsed with the argument registers its hook does not pass
# poisoned (Oracle.rehearse).
RUN = {0x1026A0, 0x102738, 0x1028B8, 0x1028D0, 0x102948, 0x1031E0, 0x1B1240, 0x1B12B0}

# Jump tables (runtime address of the jump -> (table, words)): door [27]'s
# steps 0..5 (AREA19 0x823580, the table in the overlay's data).
JUMP_TABLES = {0x823604: (0x82F800, 6)}
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
SOURCES = ('em_level10_port_area19.c', 'em_level10_port_boot.c')


def header_checks(source_dir=None):
    """The ctypes Hooks structure above is built from HOOKS; the C side's
    EmLevel10PortHooks (em_level10_port.h) and its wrappers
    (em_level10_port_internal.h) are text. Check that the header's generated
    block lists exactly the HOOKS entries, in order, with the same argument
    and result types, and that each hook has one wrapper l10_c_<address>
    that calls it and latches its own address."""
    base = Path(source_dir) if source_dir else ROOT / 'src/game'
    ctype = {U: 'uint32_t', I: 'int32_t', F: 'float', Q: 'uint64_t'}
    header = (base / 'em_level10_port.h').read_text()
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
    internal = (base / 'em_level10_port_internal.h').read_text()
    wrappers = internal.split('/* BEGIN GENERATED WRAPPERS */')[1].split('/* END GENERATED WRAPPERS */')[0]
    for hname, address, _, _ in HOOKS:
        tag = hname[2:]
        body = re.findall(r'static inline int l10_c_%s\(.*?\n\}\n' % tag, wrappers, re.S)
        if len(body) != 1 or ('o->h->%s(' % hname) not in body[0] or ('0x%08Xu' % address) not in body[0]:
            problems.append('%s: wrapper l10_c_%s missing or not calling / latching it' % (hname, tag))
    return problems, len(found)


def build(source_dir=None):
    """Compile the module (or the copies in `source_dir`, a mutation sweep)
    into a private library under build/level10/port."""
    OUT.mkdir(parents=True, exist_ok=True)
    base = Path(source_dir) if source_dir else ROOT / 'src/game'
    stem = 'level10_port' if not source_dir else 'level10_port_%d' % os.getpid()
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
    """Every image must hold the overlay RESIDENT names, with its text equal
    to the user's extract/OVERLAY/<AREA>.BIN; every image's boot text must
    equal the pinned ELF, and AREA19's table at 0x82F800 its file: every
    instruction the oracle executes is original. The designed-record area
    must be zero."""
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
        if oid == 16:
            for table, count in JUMP_TABLES.values():
                at = table - ARENA
                assert ram[table:table + 4 * count] == data[at:at + 4 * count], (beat, 'overlay jump table differs')
        assert not any(ram[FREE:FREE + 0x40000]), (beat, 'the designed-record area is not zero')
        captures[beat] = (ram, spad)
    return captures


# the entries that reach a node's +0x4C method (directly or through a
# translated callee)
SELF_ENTRIES = {0x8250F0, 0x825240, 0x8255D0, 0x8257C0, 0x825C70, 0x825EE0, 0x826100}
SCRIBBLED_CALLBACKS = set()
HEAVY = {0x829370, 0x1305B0}


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

# AREA19 nodes (the same pool nodes in a13_05 and a19_00..02;
# tools/area_overview.py --area 19, TENTH_LEVEL_ROUTE.md section 1.1)
DOOR27, R6, R7, R9, R10, R18, R43, R47 = 0x7B1240, 0x7AD490, 0x7AD780, 0x7ADD60, 0x7AE050, 0x7AF7D0, 0x7B4140, \
    0x7B4D00
OWNERS = (0x7A96E0, 0x7A9CC0)      # 0x827DD0 (the group 0x829E00)
PARTNERS = (0x7A99D0, 0x7A9FB0)    # 0x8298D0
CREATURE = 0x7AA2A0                # a13b_05: 0012E3A0 (AREA04)
TRACKS = 0x27E0C0                  # the sequencer's 0x30 tracks, 0x78 bytes each
B19 = 'a19_00_duct'


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
    rng = random.Random(0x1EA)
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
    area19_cases(add, rng)
    probe_cases(add, rng)
    boot_cases(add, rng)
    survivor_cases(add)
    return cases, captured, len(cases) - captured


BEHAVIOUR_ENTRY = {0x823580: 0x823580, 0x8250F0: 0x8250F0, 0x8255D0: 0x8255D0, 0x8257C0: 0x8257C0,
                   0x825C70: 0x825C70, 0x825EE0: 0x825EE0, 0x826100: 0x826100, 0x827790: 0x827790}


def live_tracks(ram):
    """The sequencer tracks with a cursor (+8) set."""
    return [TRACKS + 0x78 * i for i in range(0x30) if u32(ram, TRACKS + 0x78 * i + 8)]


def capture_cases(add):
    """Every entry on the captured state of every image it applies to."""
    for beat in BEATS:
        ram, _ = CAPTURES[beat]
        if RESIDENT[beat] == 16:
            for behaviour, entry in BEHAVIOUR_ENTRY.items():
                for node in owners(ram, behaviour):
                    add('capture %s %06X @%X' % (beat, entry, node), beat, entry, [node], bones(node))
                    if entry == 0x8250F0:
                        add('capture %s 825240 @%X' % (beat, node), beat, 0x825240, [node], bones(node))
                    if entry == 0x8257C0:
                        add('capture %s 825AB0 @%X' % (beat, node), beat, 0x825AB0, [node], bones(node))
            for node in owners(ram, 0x827DD0):
                rec_b = u32(ram, node + 0x110 + 0xC)
                add('capture %s 829370 @%X' % (beat, node), beat, 0x829370, [node, rec_b + 0x90], bones(node))
        if RESIDENT[beat] == 10:
            add('capture %s 8236E0' % beat, beat, 0x8236E0, [])
        if RESIDENT[beat] == 5:
            add('capture %s 1305B0 @%X' % (beat, CREATURE), beat, 0x1305B0, [CREATURE, CREATURE + 0x1F0],
                bones(CREATURE))
        add('capture %s 1833F0' % beat, beat, 0x1833F0, [PLAYER])
        add('capture %s 1885F0' % beat, beat, 0x1885F0, [PLAYER])
        # 001E5AC0's call (decomp src/func_001E5AC0.c; LEVEL9_PORT.md section 2)
        add('capture %s 1E6F60' % beat, beat, 0x1E6F60, [3, 0x7000, 0x7900, 0x9000, 0x8700, 0xFFFFFF, 0x40])
        for p in live_tracks(ram):
            add('capture %s 118790 @%X' % (beat, p), beat, 0x118790, [p])


def area19_cases(add, rng):
    """The AREA19 overlay rows: every state and sub-state with each callee
    result on both sides of its test (designed on the captured nodes of
    a19_00, the state bytes patched)."""
    B = B19
    # door [27]: the lock byte D_00810841[D_00810700] and bit (+0x34 & 31)
    d = DOOR27
    for state in (0, 1, 2, 3, 4):
        for sub in ((0, 1, 2, 3, 4, 5, 6) if state == 1 else (0,)):
            for r in ((0, 1) if state == 1 and sub != 4 else (0,)):
                for lock, bit in (((0, 2), (4, 2), (4, 0x22), (0x80, -1), (0xFB, 2)) if state == 1 and sub == 0
                                  else ((0, 2),)):
                    for ccd in ((0, 1) if state == 1 and sub == 2 else (0,)):
                        add('a19 823580 s%d sub%d r%d l%X b%d c%d' % (state, sub, r, lock, bit, ccd), B, 0x823580, [d],
                            [(d + 4, b8(state)), (d + 5, b8(sub)), (d + 0x34, h16(bit)), (0x810700, b8(0x13)),
                             (0x810841 + 0x13, b8(lock)), (0x810CCD, b8(ccd))],
                            queues={0x1BBE40: [r], 0x1BC0E0: [r], 0x1BC290: [r]})
    # [6] 0x8250F0 and its sub-state 0x825240
    n = R6
    for state in (0, 1, 2, 3, 4):
        for r in ((0, 1) if state == 0 else (0,)):
            for f775 in ((0, 1) if state == 0 else (0,)):
                for f7f5 in ((0, 1, 3, 4, 5, 0xFF) if state == 1 else (0,)):
                    add('a19 8250F0 s%d r%d %X %X' % (state, r, f775, f7f5), B, 0x8250F0, [n],
                        bones(n) + [(n + 4, b8(state)), (0x810775, b8(f775)), (0x8107F5, b8(f7f5)), (n + 5, b8(0)),
                                    (n + 0xD, b8(0x66))],
                        queues={0x1BA1C0: [r], 0x1B1EA0: [1]})
    for r in (0, 1, 2):
        for y in (209.9, 210.0, 300.0, -210.0):
            add('a19 825240 sub0 r%d y%g' % (r, y), B, 0x825240, [n],
                bones(n) + [(n + 5, b8(0)), (0x810354, f32b(y)), (0x8106C0, w32(0x1234))], queues={0x1B1EA0: [r]})
    add('a19 825240 sub0 y up', B, 0x825240, [n], [(n + 5, b8(0)), (0x810354, bits32(up(210.0)))],
        queues={0x1B1EA0: [1]})
    add('a19 825240 sub0 y down', B, 0x825240, [n], [(n + 5, b8(0)), (0x810354, bits32(down(210.0)))],
        queues={0x1B1EA0: [1]})
    for t in (0x423, 0x424, 0x425, -1, 0x7FFF):
        for other in (0, FREE + 0x100):
            for r in (0, 1):
                for f7f5 in ((0, 0xFC) if r else (0,)):
                    add('a19 825240 sub1 t%X o%X r%d f%X' % (t & 0xFFFF, other, r, f7f5), B, 0x825240, [n],
                        [(n + 5, b8(1)), (n + 0x28, h16(t)), (0x8106C0, w32(other)), (0x8107F5, b8(f7f5))],
                        queues={0x1BA1F0: [r], 0x1EFE00: [1], 0x1B6660: [0xABCD00]})
    for sub in (2, 0x80):
        add('a19 825240 sub%X' % sub, B, 0x825240, [n], [(n + 5, b8(sub))])
    # [43] 0x8255D0
    n = R43
    for state in (0, 1, 2, 3, 4):
        for bit in ((0, 1) if state in (0, 1) else (0,)):
            for r in (0, 1):
                for child in ((0, FREE + 0x200) if state in (0, 1) else (0,)):
                    if state == 0 and bit and child:
                        continue
                    add('a19 8255D0 s%d b%d r%d c%X' % (state, bit, r, child), B, 0x8255D0, [n],
                        [(n + 4, b8(state)), (0x8107F5, b8(bit | 0x10)), (n + 0x2EC, w32(child)),
                         (FREE + 0x200 + 0xB0, fw(1.25, 2.0, -3.75)), (FREE + 0x300 + 0xB0, fw(5.5, 6.0, 7.25))],
                        queues={0x1B0FD0: [r], 0x1B17A0: [1 - r], 0x1C5570: [child],
                                0x1C6120: [0x2468]})
    # the child the spawn returns is not the one +0x2EC holds afterwards
    # (the second read of +0x2EC): a scribbling 001C5570
    add('a19 8255D0 s0 reread', B, 0x8255D0, [n], [(n + 4, b8(0)), (0x8107F5, b8(0)),
                                                   (FREE + 0x200 + 0xB0, fw(1.25, 2.0, -3.75)),
                                                   (FREE + 0x300 + 0xB0, fw(5.5, 6.0, 7.25))],
        queues={0x1C5570: [Scribble(FREE + 0x200, [(n + 0x2EC, w32(FREE + 0x200))])]})
    # [7] 0x8257C0 and its sub-state 0x825AB0
    n = R7
    for state in (0, 1, 2, 3, 4):
        for r in ((0, 1) if state == 0 else (0,)):
            for r2 in ((0, 1) if state == 0 else (0,)):
                for f7f6 in ((0, 1, 2, 3, 4, 0xFF) if state == 1 else (0,)):
                    add('a19 8257C0 s%d r%d %d f%X' % (state, r, r2, f7f6), B, 0x8257C0, [n],
                        [(n + 4, b8(state)), (0x8107F6, b8(f7f6)), (n + 5, b8(0))],
                        queues={0x1B0F60: [r], 0x1BA1C0: [r2]})
    for sub in (0, 1, 2, 3, 4, 5):
        for r in ((0, 1) if sub in (1, 2, 4) else (0,)):
            for e702 in ((9, 0xA) if sub == 0 else (0xA,)):
                for b5 in ((0, 1) if sub in (1, 3) else (0x3C,)):
                    for t in ((0, 498, 499, 500, 0x7FFF, -2) if sub == 2 else (0,)):
                        add('a19 825AB0 sub%d r%d e%X b%X t%d' % (sub, r, e702, b5, t), B, 0x825AB0, [n],
                            [(n + 5, b8(sub)), (0x810702, b8(e702)), (0x8102B5, b8(b5)), (n + 0x28, h16(t)),
                             (n + 7, b8(0x5A))], queues={0x1BA1F0: [r]})
    # [9] 0x825C70 (D_00275B40: its bone array, two parts at +4 / +8)
    n = R9
    for state in (0, 1, 2, 3, 4):
        for r in ((0, 1) if state == 0 else (0,)):
            for f776 in ((0, 1, 0xFF) if state in (0, 1) else (0,)):
                for sub in ((0, 1, 2) if state == 1 else (0,)):
                    for t in ((0, 299, 300, 301, 559, 560, 0x7FFF, -1) if state == 1 and sub == 1 else (0,)):
                        add('a19 825C70 s%d r%d f%X sub%d t%d' % (state, r, f776, sub, t), B, 0x825C70, [n],
                            bones(n) + [(n + 4, b8(state)), (n + 5, b8(sub)), (0x810776, b8(f776)), (n + 0x28, h16(t)),
                                        (n + 0x1F8, f32b(0.057692308))],
                            queues={0x1B0FD0: [r]})
    # [47] 0x825EE0
    n = R47
    for state in (0, 1, 2, 3, 4):
        for f776 in ((0, 0xFF) if state in (0, 1) else (0,)):
            for f7f6 in ((5, 6, 0x80) if state == 1 and f776 == 0 else (0,)):
                for r in (0, 1):
                    for child in ((0, FREE + 0x400) if state == 1 else (0,)):
                        add('a19 825EE0 s%d f%X %X r%d c%X' % (state, f776, f7f6, r, child), B, 0x825EE0, [n],
                            [(n + 4, b8(state)), (0x810776, b8(f776)), (0x8107F6, b8(f7f6)), (n + 0x1F0, w32(child))],
                            queues={0x1B0FD0: [r], 0x1B17A0: [1 - r], 0x1C5570: [FREE + 0x500, FREE + 0x600],
                                    0x1C6120: [0x1357]})
    # [18] 0x826100
    n = R18
    for state in (0, 2, 3, 5):
        for r in ((0, 1) if state == 0 else (0,)):
            for f777 in ((0, 0xFF) if state == 0 else (0,)):
                add('a19 826100 s%d r%d f%X' % (state, r, f777), B, 0x826100, [n],
                    [(n + 4, b8(state)), (0x810777, b8(f777))], queues={0x1B0FD0: [r]})
    for x, y, z, a in ((965.0, 300.0, 926.0, 0x2A), (962.0, 300.0, 926.0, 0x2A), (968.0, 300.0, 926.0, 0x2A),
                       (965.0, 293.0, 926.0, 0x2A), (965.0, 300.0, 923.6, 0x2A), (965.0, 300.0, 929.6, 0x2A),
                       (965.0, 300.0, 926.0, 0x29), (900.0, 300.0, 926.0, 0x2A), (965.0, 100.0, 926.0, 0x2A)):
        for r in (0, 1):
            add('a19 826100 s4 %g %g %g %X r%d' % (x, y, z, a, r), B, 0x826100, [n],
                [(n + 4, b8(4)), (0x810350, fw(x, y, z)), (0x8104A0, b8(a))], queues={0x1B17A0: [r]})
    for xb, yb, zb in ((up(962.0), up(293.0), up(923.6)), (down(968.0), up(293.0), down(929.6))):
        add('a19 826100 s4 edge %X %X %X' % (xb, yb, zb), B, 0x826100, [n],
            [(n + 4, b8(4)), (0x810350, bits32(xb)), (0x810354, bits32(yb)), (0x810358, bits32(zb)),
             (0x8104A0, b8(0x2A))])
    for z in (796.589, 797.0, 900.0, 916.321, 916.0, 810.023, 811.0, 808.0, 807.5, 1000.0, 700.0):
        for t in ((0, 30, 31, -1) if z in (797.0, 900.0, 808.0) else (31,)):
            for f777 in ((0, 0xFF) if z in (807.5, 700.0, 900.0) else (0,)):
                for r in ((0, 1) if z in (807.5, 900.0) else (0,)):
                    add('a19 826100 s1 z%g t%d f%X r%d' % (z, t, f777, r), B, 0x826100, [n],
                        [(n + 4, b8(1)), (0x810354, f32b(250.0)), (0x810358, f32b(z)), (n + 0x28, h16(t)),
                         (0x810777, b8(f777))], queues={0x1BA1F0: [r], 0x1B17A0: [1 - r]})
    for zb in (up(796.589), down(916.321), up(810.023), down(808.0)):
        add('a19 826100 s1 edge %X' % zb, B, 0x826100, [n],
            [(n + 4, b8(1)), (0x810358, bits32(zb)), (n + 0x28, h16(0))])
    # [10] 0x827790
    n = R10
    for state in (0, 2, 3, 4):
        add('a19 827790 s%d' % state, B, 0x827790, [n], [(n + 4, b8(state))])
    for k in (0, 0x80, 1):
        for sub in (0, 1, 2, 3, 4):
            for r in ((0, 1, 2) if sub in (0, 1, 2) else (0,)):
                for e702 in ((0xD, 0xC) if k == 0 and sub == 0 else (0xD,)):
                    for t in ((0, 364, 365, 366, -1) if k == 0 and sub == 1 else
                              (0, 599, 600, 601, 0x7FFF) if k == 0x80 and sub == 3 else (0,)):
                        if k == 1 and (sub or r):
                            continue
                        add('a19 827790 k%X sub%d r%d e%X t%d' % (k, sub, r, e702, t), B, 0x827790, [n],
                            [(n + 4, b8(1)), (0x81081D, b8(k)), (n + 5, b8(sub)), (0x810702, b8(e702)),
                             (n + 0x28, h16(t)), (n + 0x2A, h16(t))],
                            queues={0x1BA1F0: [r], 0x1B1EA0: [r]})


HITREC = FREE + 0x6000     # a designed probe hit polygon
HITOBJ = FREE + 0x6800     # a designed hit object (0x700031D4: +3 kind)


def probe_scribble(result, kind, obj=HITOBJ, poly=HITREC, point=(0.0, 0.0, 0.0)):
    """A 0019A570 / 0019AA80 stub that writes the probe's outputs: the hit
    point 0x700031B0, the polygon 0x700031D0, the object 0x700031D4, the
    kind 0x700031D8."""
    return Scribble(result, [(0x700031B0, fw(point[0], point[1], point[2], 1.0)),
                             (0x700031D0, w32(poly)), (0x700031D4, w32(obj)), (0x700031D8, w32(kind))])


def probe_cases(add, rng):
    """0x829370 on the group owner (+4 4 sweeps, 1 tracks): the two probes'
    outcomes, the far / near distance, the hit-object kinds, +0x204 and
    +0x200 on both sides of their tests."""
    B = B19
    ram, _ = CAPTURES[B]
    t = OWNERS[0]
    rec_b = u32(ram, t + 0x110 + 0xC)
    for state, c200, c204, (h1, k1), (h2, k2, dist) in (
            (4, 0, 0, (1, 1), (0, 1, 0.0)), (4, 0, 0, (0, 1), (1, 1, 0.0)), (4, 0, HITOBJ, (1, 2), (1, 1, 0.0)),
            (1, 13, 0, (1, 1), (1, 2, 0.0)), (1, 12, 0, (1, 1), (0, 1, 0.0)), (1, 13, 0, (0, 1), (1, 1, 200.0)),
            (4, 0, 0, (0, 1), (1, 1, 200.0)), (1, 13, HITOBJ, (1, 1), (0, 1, 0.0)), (1, 13, 0, (0, 0), (0, 0, 0.0)),
            (1, -1, 0, (1, 1), (0, 1, 0.0)), (1, 0x7FFFFFFF, 0, (1, 1), (0, 1, 0.0))):
        for k3 in (0x11, 0x14, 0x0F):
            add('probe 829370 s%d %X %X %d%d %d%d %g %X' % (state, c200 & MASK, c204, h1, k1, h2, k2, dist, k3), B,
                0x829370, [t, rec_b + 0x90],
                bones(t) + [(t + 4, b8(state)), (t + 0x200, w32(c200)), (t + 0x204, w32(c204)), (HITOBJ + 3, b8(k3)),
                            (0x810360, fw(0.0, 0.0, 0.0, 1.0))],
                queues={0x19AA80: [probe_scribble(h1, k1)],
                        0x19A570: [probe_scribble(h2, k2, point=(dist, 0.0, 0.0))]},
                default={0x122BB8: 0x7FFF0000})
    # the distance exactly at 10000 (100 units: 10000 is not above the limit)
    for dist in (100.0, 100.00001):
        add('probe 829370 dist %r' % dist, B, 0x829370, [t, rec_b + 0x90],
            bones(t) + [(t + 4, b8(1)), (t + 0x200, w32(13)), (t + 0x204, w32(0)), (0x810360, fw(0.0, 0.0, 0.0, 1.0))],
            queues={0x19AA80: [probe_scribble(0, 1)], 0x19A570: [probe_scribble(1, 2, point=(dist, 0.0, 0.0))]})
    for rnd in (0x12345678, -0x10000, 0x7FFFFFFF, 0):
        add('probe 829370 glint %X' % (rnd & MASK), B, 0x829370, [t, rec_b + 0x90],
            bones(t) + [(t + 4, b8(4)), (t + 0x204, w32(0))],
            queues={0x19AA80: [probe_scribble(1, 1)], 0x19A570: [probe_scribble(0, 1)]}, default={0x122BB8: rnd})


def boot_cases(add, rng):
    """00118790 on designed event bytes, 001305B0's states on the AREA04
    creature of a13b_05, 001833F0 / 001885F0 on the player, 001E6F60 on
    designed pool records, AREA13 0x8236E0."""
    B = B19
    # 00118790: D_00281AD4 pointed at designed bytes; the track's +0x36
    p = TRACKS + 0x78 * 2
    base = FREE + 0x1001                       # an odd base, as the captures have
    for op4, k in ((0, 0), (0, 7), (3, 3), (3, 2), (3, 0xFF03), (0xFF, 0xFF), (1, 0)):
        for op3, op2 in ((0x12, 0x34), (0, 0), (0xFF, 0xFF)):
            add('boot 118790 %X %X %X %X' % (op4, k, op3, op2), B, 0x118790, [p],
                [(0x281AD4, w32(base)), (p + 8, w32(0x40)), (p + 0x36, h16(k)),
                 (base + 0x40, bytes([0xB0, 0x60, op2, op3, op4])),
                 (base + (op3 << 8 | op2), b8(0xA5)), (base + (op3 << 8) + op2, b8(0x5A))])
    # 001305B0 (a13b_05: the AREA04 creature; ent = +0x1F0)
    B5 = 'a13b_05_lift_ride'
    c = CREATURE
    e = c + 0x1F0
    common = bones(c) + [(c + 5, b8(6))]
    for st in (0, 1):
        for e54 in (0, 1):
            for x3c in (100.0, 136.0, 137.0):
                for e58 in (0, 0x1000, 0xEFFF):
                    add('boot 1305B0 s%d e54 %d x%g e58 %X' % (st, e54, x3c, e58), B5, 0x1305B0, [c, e],
                        common + [(c + 6, b8(st)), (e + 0x54, h16(e54)), (c + 0x3C, f32b(x3c)), (e + 0x58, h16(e58)),
                                  (e + 0x69, b8(0))],
                        full=(x3c == 137.0 and e58 == 0xEFFF))
    for st in (2, 4):
        for e58 in (0, 0x1000):
            add('boot 1305B0 s%d e58 %X' % (st, e58), B5, 0x1305B0, [c, e],
                common + [(c + 6, b8(st)), (e + 0x58, h16(e58))])
    for r1, r2 in ((0, 1), (0, 0), (1, 1), (1, 0)):
        for d in ((3, 0x80, 0x83, 0x7F) if (r1, r2) == (0, 1) else (3,)):
            for f70a in ((0, 1) if (r1, r2) == (0, 1) else (0,)):
                for r3, r4 in (((1, 0), (0, 0), (0, 1)) if (r1, r2) != (0, 1) else ((0, 0),)):
                    add('boot 1305B0 s3 %d%d d%X f%d %d%d' % (r1, r2, d, f70a, r3, r4), B5, 0x1305B0, [c, e],
                        common + [(c + 6, b8(3)), (c + 0xD, b8(d)), (0x81070A, b8(f70a)), (0x8102B0, b8(0x41))],
                        queues={0x21BE40: [r1], 0x1A7B80: [r2], 0x1B3250: [r3], 0x1B1560: [r4]})
    # 001B2B10 writes the vector the commit copies to D_00810320..
    add('boot 1305B0 s3 commit scribble', B5, 0x1305B0, [c, e], common + [(c + 6, b8(3))],
        queues={0x1A7B80: [1], 0x1B2B10: [Scribble(None, [(0x700038A0, fw(1.5, -2.25, 3.0, 0.5))])]})
    for st in (2, 3, 4, 5, 0xFF):
        for e69 in (0, 1, 0xFE):
            for c50 in ((0, 1, 0xFFFF) if e69 == 1 else (0,)):
                for rnd in ((0x12345678, -0x200, 0x0E00) if c50 == 0 and e69 == 1 else (0,)):
                    add('boot 1305B0 tail s%X e69 %X c50 %X r%X' % (st, e69, c50, rnd & MASK), B5, 0x1305B0, [c, e],
                        common + [(c + 6, b8(st)), (e + 0x69, b8(e69)), (e + 0x50, h16(c50)), (e + 0x58, h16(0)),
                                  (e + 0x40, f32b(12.5))],
                        queues={0x21BE40: [1], 0x1B3250: [0], 0x1B1560: [1]}, default={0x122BB8: rnd})
    # 001833F0 / 001885F0 on the player
    for kind in (0, 1, 2, 0x81):
        add('boot 1833F0 k%X' % kind, B, 0x1833F0, [PLAYER], [(PLAYER + 4, b8(kind)), (B8D, b8(3))])
    for b in (0, 1, 2, 3, 0xFF):
        add('boot 1885F0 b%X' % b, B, 0x1885F0, [PLAYER], [(PLAYER + 0x235, b8(b)), (0x2754D4, fw(0x80017FFF))])
    # 001E6F60: a designed pool (D_00275670) with record cursors in the free area
    pool = FREE + 0x2000
    recs = [(FREE + 0x3000, 'aligned'), (FREE + 0x3108, 'quad +8')]
    for n0 in (0, 3, 5):
        for rec, what in recs:
            for args in ((0x7000, 0x7900, 0x9000, 0x8700, 0xFFFFFF, 0x40),
                         (-1, 0x7FFF, 0x8000, -0x8000, -1, -1), (0x12345, 0xFFFF, 0, 0x10000, 0x80000000, 0xFFFFFFFF)):
                add('boot 1E6F60 n%d %s %s' % (n0, what, ' '.join('%X' % (a & MASK) for a in args)), B, 0x1E6F60,
                    [n0] + list(args),
                    [(0x275670, w32(pool)), (pool + 4 * n0 + 0x10, w32(rec))] + [(rec - 0x10 + i, b8(0xEE))
                                                                              for i in range(0x90)])
    for beat in A13_BEATS:
        add('boot 8236E0 %s poisoned' % beat, beat, 0x8236E0, [],
            [(0x275C1C, w32(0x11111111)), (0x275C24, w32(0x22222222)), (0x275C28, w32(0x33333333)),
             (0x275C2C, w32(0x44444444))])


def survivor_cases(add):
    """Cases added because the mutation sweep (docs/LEVEL10_PORT.md section
    3.5) showed the default set missed them; they always run."""
    B = B19
    # 0x827790: the counts +0x2A (D_0081081D 0x80, +5 3) and +0x28 (0, +5 1)
    # are signed halfwords: 0x7FFF + 1 is negative, below the bound
    add('survivor 827790 k80 sub3 2A 7FFF', B, 0x827790, [R10],
        [(R10 + 4, b8(1)), (0x81081D, b8(0x80)), (R10 + 5, b8(3)), (R10 + 0x2A, h16(0x7FFF))])
    add('survivor 827790 k0 sub1 28 7FFF', B, 0x827790, [R10],
        [(R10 + 4, b8(1)), (0x81081D, b8(0)), (R10 + 5, b8(1)), (R10 + 0x28, h16(0x7FFF))])
    # 001885F0's halfword is signed (0x8001 at the odd slot)
    add('survivor 1885F0 b1 signed', B, 0x1885F0, [PLAYER], [(PLAYER + 0x235, b8(1)), (0x2754D4, fw(0x80017FFF))])
    # 0x825240: the player exactly at y 210 is not below it (the script starts)
    add('survivor 825240 sub0 y210', B, 0x825240, [R6], bones(R6) + [(R6 + 5, b8(0)), (0x810354, f32b(210.0))],
        queues={0x1B1EA0: [1]})
    # review round (section 3.5): 0x826100 state 1 with +0x28 = 0 (still
    # counting, so z is compared as given): z exactly at each bound
    # (916.321f = 0x4465148B is not below it; 810.023f = 0x444A8179 is not
    # above it), and y = 0, where one ulp of the y step shows
    for label, z in (('z916.321', bits32(0x4465148B)), ('z810.023', bits32(0x444A8179))):
        add('survivor 826100 s1 %s t0' % label, B, 0x826100, [R18],
            [(R18 + 4, b8(1)), (0x810354, f32b(250.0)), (0x810358, z), (R18 + 0x28, h16(0))])
    add('survivor 826100 s1 y0 step', B, 0x826100, [R18],
        [(R18 + 4, b8(1)), (0x810354, f32b(0.0)), (0x810358, f32b(900.0)), (R18 + 0x28, h16(0))])
    # 0x829370: hit-object kind exactly 0x10, the lower bound of 0x10..0x13
    # (the first probe misses, the second hits at distance 0)
    ram, _ = CAPTURES[B]
    t = OWNERS[0]
    rec_b = u32(ram, t + 0x110 + 0xC)
    add('survivor 829370 kind10', B, 0x829370, [t, rec_b + 0x90],
        bones(t) + [(t + 4, b8(1)), (t + 0x200, w32(13)), (t + 0x204, w32(0)), (HITOBJ + 3, b8(0x10)),
                    (0x810360, fw(0.0, 0.0, 0.0, 1.0))],
        queues={0x19AA80: [probe_scribble(0, 1)], 0x19A570: [probe_scribble(1, 1, point=(0.0, 0.0, 0.0))]},
        default={0x122BB8: 0x7FFF0000})


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

# Words the static walk reaches that no input can reach, and why.
DEAD_WORDS = {}


def fault_checks():
    """The native fail-stop contract: a NULL hook, a failing hook, an
    unmapped address after the calls and before any call, a latched fault;
    and for every entry a fault latched on entry, a NULL hook table, a NULL
    fault pointer and (entries with a result) a NULL result pointer."""
    ram, spad = CAPTURES[B19]
    node = R6
    ram = bytearray(ram)
    ram[node + 4] = 1
    ram[0x8107F5] = 5
    problems = []
    calls_want = ['w_001C64F0', 'w_001B17A0', 'w_001C68C0', 'w_callback']

    def attempt(null=None, failing=None, unmapped=None, latched=False, entry=0x8250F0, args=(node, STACK_TOP)):
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
    status, address, code, calls, same = attempt(failing='w_001C64F0')
    if (status, address, code) != (-1, 0x1C64F0, 2) or calls != calls_want[:1] or not same:
        problems.append(('failing hook', status, hex(address), code, calls, same))
    status, address, code, calls, _ = attempt(unmapped=node + 0x4C)
    if (status, address, code) != (-1, node + 0x4C, 5) or calls != calls_want[:3]:
        problems.append(('unmapped address', status, hex(address), code, calls))
    status, address, code, calls, same = attempt(unmapped=PLAYER + 0x235, entry=0x1885F0, args=(PLAYER,))
    if (status, address, code) != (-1, PLAYER + 0x235, 5) or calls or not same:
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
    """The fail-stop contract of the header (em_level10_port.h), on one
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
    where each wrapper's own `l10_failed` test is observable. Returns
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







QUICK_RANDOM = 0
PINNED_RANDOM = ()


# The default run's share of the designed cases: a coverage pass over the
# whole set (every case's original run, EM_TEST_FULL=1) picked these so that
# with the capture cases the default run reaches every word the whole set
# reaches (greedy, cheapest first); SURVIVORS are the mutation sweep's cases.
DEFAULT_KEEP = frozenset([
    'a19 823580 s0 sub0 r0 l0 b2 c0',
    'a19 823580 s1 sub0 r1 l4 b34 c0',
    'a19 823580 s1 sub0 r1 lFB b2 c0',
    'a19 823580 s1 sub1 r1 l0 b2 c0',
    'a19 823580 s1 sub2 r1 l0 b2 c0',
    'a19 823580 s1 sub3 r1 l0 b2 c0',
    'a19 823580 s1 sub4 r0 l0 b2 c0',
    'a19 823580 s1 sub5 r1 l0 b2 c0',
    'a19 823580 s1 sub6 r1 l0 b2 c0',
    'a19 823580 s2 sub0 r0 l0 b2 c0',
    'a19 823580 s3 sub0 r0 l0 b2 c0',
    'a19 823580 s4 sub0 r0 l0 b2 c0',
    'a19 8250F0 s0 r0 0 0',
    'a19 8250F0 s0 r0 1 0',
    'a19 8250F0 s0 r1 1 0',
    'a19 8250F0 s1 r0 0 3',
    'a19 8250F0 s1 r0 0 FF',
    'a19 8250F0 s2 r0 0 0',
    'a19 8250F0 s3 r0 0 0',
    'a19 8250F0 s4 r0 0 0',
    'a19 825240 sub0 y up',
    'a19 825240 sub1 t424 o1C00100 r1 fFC',
    'a19 825240 sub80',
    'a19 8255D0 s0 b0 r0 c0',
    'a19 8255D0 s0 b1 r0 c0',
    'a19 8255D0 s0 reread',
    'a19 8255D0 s1 b1 r0 c1C00200',
    'a19 8255D0 s2 b0 r0 c0',
    'a19 8255D0 s3 b0 r1 c0',
    'a19 8255D0 s4 b0 r1 c0',
    'a19 8257C0 s0 r0 0 f0',
    'a19 8257C0 s0 r0 1 f0',
    'a19 8257C0 s1 r0 0 fFF',
    'a19 8257C0 s2 r0 0 f0',
    'a19 8257C0 s3 r0 0 f0',
    'a19 8257C0 s4 r0 0 f0',
    'a19 825AB0 sub0 r0 eA b3C t0',
    'a19 825AB0 sub1 r1 eA b1 t0',
    'a19 825AB0 sub2 r1 eA b3C t499',
    'a19 825AB0 sub3 r0 eA b0 t0',
    'a19 825AB0 sub3 r0 eA b1 t0',
    'a19 825AB0 sub4 r1 eA b3C t0',
    'a19 825AB0 sub5 r0 eA b3C t0',
    'a19 825C70 s0 r0 f1 sub0 t0',
    'a19 825C70 s0 r0 fFF sub0 t0',
    'a19 825C70 s1 r0 f1 sub0 t0',
    'a19 825C70 s1 r0 fFF sub1 t299',
    'a19 825C70 s1 r0 fFF sub1 t300',
    'a19 825C70 s1 r0 fFF sub1 t560',
    'a19 825C70 s1 r0 fFF sub2 t0',
    'a19 825C70 s2 r0 f0 sub0 t0',
    'a19 825C70 s3 r0 f0 sub0 t0',
    'a19 825C70 s4 r0 f0 sub0 t0',
    'a19 825EE0 s0 f0 0 r0 c0',
    'a19 825EE0 s0 fFF 0 r0 c0',
    'a19 825EE0 s1 f0 5 r0 c1C00400',
    'a19 825EE0 s1 fFF 0 r1 c1C00400',
    'a19 825EE0 s2 f0 0 r0 c0',
    'a19 825EE0 s3 f0 0 r1 c0',
    'a19 825EE0 s4 f0 0 r1 c0',
    'a19 826100 s0 r0 f0',
    'a19 826100 s0 r0 fFF',
    'a19 826100 s1 z796.589 t31 f0 r0',
    'a19 826100 s1 z807.5 t31 fFF r1',
    'a19 826100 s1 z808 t30 f0 r0',
    'a19 826100 s1 z900 t31 fFF r0',
    'a19 826100 s3 r0 f0',
    'a19 826100 s4 965 300 926 2A r1',
    'a19 826100 s4 965 300 929.6 2A r0',
    'a19 826100 s5 r0 f0',
    'a19 827790 k0 sub0 r2 eD t0',
    'a19 827790 k0 sub1 r2 eD t366',
    'a19 827790 k0 sub2 r2 eD t0',
    'a19 827790 k0 sub4 r0 eD t0',
    'a19 827790 k1 sub0 r0 eD t0',
    'a19 827790 k80 sub0 r1 eD t0',
    'a19 827790 k80 sub1 r2 eD t0',
    'a19 827790 k80 sub2 r0 eD t0',
    'a19 827790 k80 sub3 r0 eD t601',
    'a19 827790 k80 sub4 r0 eD t0',
    'a19 827790 s0',
    'a19 827790 s2',
    'a19 827790 s3',
    'a19 827790 s4',
    'boot 118790 FF FF FF FF',
    'boot 1305B0 s0 e54 1 x137 e58 1000',
    'boot 1305B0 s1 e54 1 x136 e58 1000',
    'boot 1305B0 s2 e58 1000',
    'boot 1305B0 s3 01 d3 f1 00',
    'boot 1305B0 s3 01 d7F f1 00',
    'boot 1305B0 s3 01 d83 f0 00',
    'boot 1305B0 s3 01 d83 f1 00',
    'boot 1305B0 s3 11 d3 f0 00',
    'boot 1305B0 s3 11 d3 f0 10',
    'boot 1305B0 s3 commit scribble',
    'boot 1305B0 s4 e58 1000',
    'boot 1305B0 tail sFF e69 1 c50 0 rFFFFFE00',
    'boot 1305B0 tail sFF e69 1 c50 FFFF r0',
    'probe 829370 dist 100.0',
    'probe 829370 s1 FFFFFFFF 0 11 01 0 F',
    'probe 829370 s4 0 0 01 11 0 11',
    'probe 829370 s4 0 0 01 11 200 F',
    'probe 829370 s4 0 1C06800 12 11 0 14'
])
SURVIVORS = ('survivor ',)


def select_cases(elf):
    """The mode's case list (EM_TEST_FULL=1: all; the default run keeps
    every capture case, the designed cases of DEFAULT_KEEP and the mutation
    sweep's survivor cases)."""
    cases, captured, targeted = case_list(elf)
    total = len(cases)
    if not reference_mode.FULL and not os.environ.get('EM_LEVEL10_PORT_ALL'):
        cases = cases[:captured] + [c for c in cases[captured:]
                                    if c[0] in DEFAULT_KEEP or c[0].startswith(SURVIVORS)]
        return cases, captured, len(cases) - captured, total
    return cases, captured, targeted, total


def image_for(entry):
    """An image holding the entry's code (boot text is the same in all)."""
    if entry == 0x8236E0:
        return CAPTURES[A13_BEATS[0]][0]
    return CAPTURES[A19_BEATS[0]][0]


def main():
    global NATIVE
    elf = read_elf()
    CAPTURES.update(load_captures(elf))
    NATIVE = build(os.environ.get('EM_LEVEL10_PORT_SOURCE'))
    selected, captured, targeted, total = select_cases(elf)
    CASES[:] = selected
    only = os.environ.get('EM_LEVEL10_PORT_ONLY')   # debugging: a label prefix
    if only:
        CASES[:] = selected = [c for c in selected if c[0].startswith(only)]
    results = run_cases(selected)
    failures, per_fn, seen, totals = [], {}, set(), [0, 0, 0, 0]
    header_problems, header_count = header_checks(os.environ.get('EM_LEVEL10_PORT_SOURCE'))
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
    if os.environ.get('EM_LEVEL10_PORT_KEEP'):   # lane tool: the coverage pass that picks DEFAULT_KEEP
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
    if os.environ.get('EM_LEVEL10_PORT_MISSING'):
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
        if os.environ.get('EM_LEVEL10_PORT_NOCONTRACT'):   # debugging only: skips the contract
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
        if not os.environ.get('EM_LEVEL10_PORT_NOREUSE'):
            problems, report = reuse_checks(elf)
            print('  reuse checks: %s' % (report if not problems else problems[:4]))
            if problems:
                failures.append(('reuse checks', problems[:6]))
    for label, errors in failures[:int(os.environ.get("EM_LEVEL10_PORT_SHOW", "12"))]:
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
# against the ORIGINAL over the tenth-level captures with that module's own
# harness (imported, not modified; its library is built into
# build/level10/port/reuse):
#   0x8298D0  em_area01_ovl_00828850 (tools/test_area01_overlay_reference.py,
#             run_case): AREA19's 0x8298D0 is AREA01's 0x828850 word for
#             word (the same instructions; no overlay-internal address), so
#             the AREA01 translation is run as the entry at 0x8298D0 on the
#             AREA19 images: every state of the two partners [the group
#             0x829E00's 0x8298D0 nodes] with each callee result on both
#             sides of its test.
REUSE_OUT = OUT / 'reuse'
PARTNER_SIZE = 0x198


def reuse_partner(elf, smoke=False):
    import test_area01_overlay_reference as A1
    a19 = OVERLAY_FILES[16].read_bytes()
    a01 = (DECOMP / 'extract/OVERLAY/AREA01.BIN').read_bytes()
    assert a19[0x8298D0 - ARENA:0x8298D0 - ARENA + PARTNER_SIZE] == \
        a01[0x828850 - ARENA:0x828850 - ARENA + PARTNER_SIZE], 'AREA19 0x8298D0 no longer equals AREA01 0x828850'
    A1.OUT = REUSE_OUT / 'area01_ovl'
    A1.FUNCS[0x8298D0] = ('em_area01_ovl_00828850', PARTNER_SIZE, 'owner')
    A1.CAPTURES.clear()
    for beat in A19_BEATS:
        A1.CAPTURES[beat] = CAPTURES[beat]
    A1.NATIVE = A1.build()
    cases = []
    for beat in (A19_BEATS if not smoke else A19_BEATS[1:2]):
        ram, _ = CAPTURES[beat]
        for node in owners(ram, 0x8298D0):
            cases.append(('reuse 8298D0 capture %s @%X' % (beat, node), beat, 0x8298D0, (node,), [], [],
                          A1.Script(), frozenset(A1.RUN)))
    node = PARTNERS[0]
    for state in (0, 1, 2, 3, 4):
        for q in (({0x1B0FD0: [0], 0x1B11E0: [0]}, {0x1B0FD0: [0], 0x1B11E0: [1]}, {0x1B0FD0: [1]}) if state == 0
                  else ({0x1EFE00: [0]}, {0x1EFE00: [1]}) if state == 1 else ({0x1B17A0: [0]}, {0x1B17A0: [1]})):
            for raised in ((0, 1, 0x8000) if state == 1 else (0,)):
                for t28 in ((0, 8, 9, 10, 11, -1) if state == 2 else (0,)):
                    if smoke and (raised == 0x8000 or t28 in (8, 11, -1)):
                        continue
                    tag = ' '.join('%X:%d' % (a, v[0]) for a, v in sorted(q.items()))
                    cases.append(('reuse 8298D0 s%d %s %X %d' % (state, tag, raised, t28), B19, 0x8298D0, (node,),
                                  [(node + 4, b8(state)), (node + 0x36, h16(raised)), (node + 0x28, h16(t28)),
                                   (node + 0x9A, b8(0x85))], [], A1.Script(q), frozenset(A1.RUN)))
    seen, problems = set(), []
    for case in cases:
        label = case[0]
        _, _, errors, pcs, _, _ = A1.run_case(case)
        seen |= pcs
        if errors:
            problems.append((label, errors[:2]))
    words = reachable_words(CAPTURES[A19_BEATS[0]][0], 0x8298D0, PARTNER_SIZE)
    if not smoke and words - seen:
        problems.append(('8298D0 coverage', '%d of %d words not executed' % (len(words - seen), len(words))))
    if problems:
        raise AssertionError(problems[:3])
    return '0x8298D0 (em_area01_ovl_00828850) %d cases, %d/%d reachable non-branch words' % (
        len(cases), len(words & seen), len(words))


def reuse_checks(elf):
    """EM_TEST_FULL=1 runs every check in full; the default run keeps a
    smoke sample."""
    problems, parts = [], []
    for check in (reuse_partner,):
        try:
            parts.append(check(elf, smoke=not reference_mode.FULL))
        except AssertionError as error:
            problems.append((check.__name__, repr(error)[:600]))
    return problems, 'ok: ' + '; '.join(parts)


if __name__ == '__main__':
    main()
