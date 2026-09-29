#!/usr/bin/env python3
"""Execute the ORIGINAL AREA04 overlay and boot code and compare em_area04_port.c.

docs/AREA04_PORT.md. The 16 functions of the AREA04 census delta (decomp
build/s87/census/a04_delta.json, new_functions): the AREA04 overlay
functions 0x823700, 0x823B40, 0x823B90, 0x823EE0, 0x824100, 0x8241F0,
0x824490, 0x8246B0, 0x824DC0, 0x825880, 0x825B00, 0x825D60, 0x825DF0,
0x8260C0 and the boot functions 001BE5F0 and 001C1A80; plus 0x823580,
0x824830 and 0x824930, which 0x823700 and 0x8246B0 call and which have no
port translation. The calls between them (0x823700 -> 0x823580, 0x8246B0
-> 0x824830 / 0x824930, 001C1A80 -> 001BE5F0) run as original code at the
top level of the oracle (only other boot functions are hooks), and every
function is also an entry of its own.

The harness is tools/test_area02_overlay_reference.py (lane A02OVL, itself
the AREA00 / AREA01 overlay design), reused with the AREA04 captures, the
AREA04 hook table and cases, and entries with a result that take several
arguments. Stack locals: 0x824490 copies the 64 bytes at 0x828220 into its
frame and passes their address to 001B1EA0. As in the AREA02 harness, the
oracle runs with the private stack top STACK_TOP as sp at entry; the window
[STACK_LO, STACK_TOP) is compared memory: the function's own loads and
stores there are logged and compared one for one, except the frame
bookkeeping (stack_frame_op).

Oracle: the shared EE interpreter with the measured float model (FallEE,
tools/test_player_fall_reference.py) runs the original code resident in the
recorded RAM images (decomp build/s87/route_a02/a02_05_progression_exit,
the AREA04 arrival, and build/s87/route_a04/<beat>/ for a04_00..a04_04 and
a04_s0..a04_s3, all with AREA04 resident (overlay id 5 at 0x823500); the
boot functions also run on a04_05_progression_exit, which ends in AREA22
with two 001C1A80 owners). Before any case the test checks that the overlay
text of every AREA04 image equals the user's extract/OVERLAY/AREA04.BIN
(text size from its header) and that the boot text below 0x241000 of every
image equals the pinned ELF, so every executed instruction is original.
Nothing here embeds original bytes; reports hold counts only.

Callees (every boot function outside this module and the actor's +0x4C
callback) are intercepted at their entry and logged with their arguments.
A callee either runs as ORIGINAL code nested inside the oracle (the pure
helpers in RUN: cos, sin, fabs, sqrt, the vector subtract / scale / copy,
the 16-byte copy 00121870, the random generator, the angle from a point
001B1240, the angle wrap, the script start 001BA1A0 and the weight update
001D0D60), its writes recorded, or is stubbed with the case's scripted
result (everything else). The native module runs over a byte copy of the
same RAM with hooks that must be called in the same order with the same
arguments; each hook replays the original callee's writes and result.

Compared, per case (as in the AREA02 harness, docstring there):
  * at the ENTRY of every call, BEFORE the callee's writes are replayed:
    native RAM, scratchpad and stack window against the oracle's memory at
    the entry of the same original call (quick mode: the dirty set;
    EM_TEST_FULL=1: all 32 MiB + scratchpad + window at every call entry);
    then the callee and its arguments.
  * between calls: the native module's memory accesses are the original's
    own loads and stores one for one, in order, with the same address,
    size and `changed` mark (AREA04 has no jump tables, so no load is left
    out), except the frame bookkeeping.
  * after the last store: all 32 MiB + scratchpad + window, the return value
    of the entries with a result, and the store-log self-check. An original
    load or store outside the modelled memory (or misaligned) stops the
    original; the native module must stop with fault 5 at the same address
    after the same calls and stores.
  * float hook arguments and results cross as bit patterns.
  * each helper that runs as original code is rehearsed with every argument
    register its hook does not pass poisoned.
  * every case runs twice: as given, and poisoned (poison_patches).
  * the run fails unless every reachable original word was executed.
  * the fail-stop contract of em_area04_port.h (hook_contract_site and
    fault_checks), and the table's ctx at every `bytes`, hook and callback
    call.

Cases:
  capture   every owner node of every image, exactly as captured, for each
            entry that takes the node, 0x823B40 on every AREA04 image, and
            001BE5F0 / 001C1A80 on the two AREA22-side owners.
  designed  targeted_cases: every state and step, callee results 0 / 1 /
            other values at every test of a result, scribbling callees,
            story bytes and flags on both sides of every test, the float
            boundaries of every compare.
  perturbed seeded variations (EM_TEST_FULL=1 only).
Coverage: the report counts the original instruction words the oracle
executed inside each function (branch words excluded).

EM_AREA04_PORT_SOURCE=<file> tests another copy of the module source (the
mutation sweep). At most 4 worker processes unless EM_TEST_JOBS says
otherwise.
"""
import ctypes as C
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
from test_player_slide_reference import EE, read_elf, bits, number, s32, sx32, STACK_TOP  # noqa: E402
from test_player_fall_reference import FallEE  # noqa: E402
import ee_float_model as M  # noqa: E402

DECOMP = ROOT.parent / 'Extermination'
OVERLAY_FILE = DECOMP / 'extract/OVERLAY/AREA04.BIN'
OUT = ROOT / 'build/area04/port'
MASK = 0xFFFFFFFF
RAM_SIZE, SPAD_SIZE = 0x2000000, 0x4000
ARENA, TEXT_END = 0x823500, 0x826600
# The stack window: the frames of the call chains below the entry sp
# (deepest: 001C1A80 -> 001BE5F0, 0x80 bytes; 0x824490 keeps its locals at
# sp - 0x40); nested helpers run 0x400 lower (EE.nested).
STACK_LO = STACK_TOP - 0x400
STACK_SIZE = STACK_TOP - STACK_LO
# The images with AREA04 resident: the arrival (the a02_05 end, sub 0) and
# the end of every AREA04 beat but a04_05; a04_05 ends in AREA22, with the
# two 001C1A80 owners (boot entries only).
BEAT_DIRS = {'a02_05_progression_exit': DECOMP / 'build/s87/route_a02/a02_05_progression_exit'}
for _b in ('a04_00_door45_event', 'a04_01_door40', 'a04_02_console', 'a04_03_back_to_hall', 'a04_04_conveyor',
           'a04_s0_door45_locked', 'a04_s1_reel_blocks', 'a04_s2_pickup', 'a04_s3_door42_locked',
           'a04_05_progression_exit'):
    BEAT_DIRS[_b] = DECOMP / 'build/s87/route_a04' / _b
BEATS = list(BEAT_DIRS)
BOOT_BEAT = 'a04_05_progression_exit'
OVERLAY_BEATS = [b for b in BEATS if b != BOOT_BEAT]
POOL, NODE, NODES = 0x7A5640, 0x2F0, 400
PLAYER = 0x8102B0


class _Route:
    """ROUTE / beat resolves to the beat's folder (two route trees)."""

    def __truediv__(self, beat):
        return BEAT_DIRS[beat]


ROUTE = _Route()

# (entry, native symbol, byte length of the function body, kind). Kinds:
# owner (self), owner_sp (self, sp), talk (self, who -> result), void_ret
# (-> result), triple_ret (player, actor, block -> result).
FUNCS = {
    0x823580: ('em_area04_port_00823580', 0x178, 'talk'),
    0x823700: ('em_area04_port_00823700', 0x220, 'owner'),
    0x823B40: ('em_area04_port_00823B40', 0x4C, 'void_ret'),
    0x823B90: ('em_area04_port_00823B90', 0x344, 'owner'),
    0x823EE0: ('em_area04_port_00823EE0', 0x220, 'owner'),
    0x824100: ('em_area04_port_00824100', 0xE4, 'owner'),
    0x8241F0: ('em_area04_port_008241F0', 0x130, 'owner'),
    0x824490: ('em_area04_port_00824490', 0x158, 'owner_sp'),
    0x8246B0: ('em_area04_port_008246B0', 0x178, 'owner'),
    0x824830: ('em_area04_port_00824830', 0xFC, 'owner'),
    0x824930: ('em_area04_port_00824930', 0x10C, 'owner'),
    0x824DC0: ('em_area04_port_00824DC0', 0x130, 'owner'),
    0x825880: ('em_area04_port_00825880', 0x27C, 'owner'),
    0x825B00: ('em_area04_port_00825B00', 0x254, 'owner'),
    0x825D60: ('em_area04_port_00825D60', 0x90, 'owner'),
    0x825DF0: ('em_area04_port_00825DF0', 0x2D0, 'owner'),
    0x8260C0: ('em_area04_port_008260C0', 0x4F8, 'owner'),
    0x1BE5F0: ('em_area04_port_001BE5F0', 0xC4, 'triple_ret'),
    0x1C1A80: ('em_area04_port_001C1A80', 0x27C, 'owner'),
}
NARGS = {'owner': 1, 'owner_sp': 2, 'talk': 2, 'void_ret': 0, 'triple_ret': 3}
SELF_KINDS = ('owner', 'owner_sp', 'talk')
RESULT_KINDS = ('talk', 'void_ret', 'triple_ret')
SP_KINDS = ('owner_sp',)
BOOT_ENTRIES = (0x1BE5F0, 0x1C1A80)


def original_args(entry, native_args):
    """The original's argument registers: the stack-pointer entries take sp
    as a native argument only (the original gets it in $sp)."""
    return native_args[:1] if FUNCS[entry][2] in SP_KINDS else native_args


def entry_args(entry, node):
    """The native arguments of `entry` for an actor node."""
    kind = FUNCS[entry][2]
    return {'owner': [node], 'owner_sp': [node, STACK_TOP], 'talk': [node, node + 0x1F0], 'void_ret': [],
            'triple_ret': [PLAYER, node, node + 0x1F0]}[kind]


# Hooks, in the field order of EmArea04PortHooks (after ctx and bytes).
# name -> (original address, [(ctype, register)], result kind or None)
U, I, F, Q = 'u', 'i', 'f', 'q'
HOOKS = [
    ('w_0011DE90', 0x11DE90, [(F, 12)], F),
    ('w_0011DF78', 0x11DF78, [(F, 12)], F),
    ('w_0011E2A8', 0x11E2A8, [(F, 12)], F),
    ('w_0011E748', 0x11E748, [(F, 12)], F),
    ('w_001028D0', 0x1028D0, [(U, 4), (U, 5), (U, 6)], None),
    ('w_00102900', 0x102900, [(U, 4), (U, 5), (F, 12)], None),
    ('w_00102948', 0x102948, [(U, 4), (U, 5)], None),
    ('w_00121870', 0x121870, [(U, 4), (U, 5), (I, 6)], None),
    ('w_00122BB8', 0x122BB8, [], I),
    ('w_00182F90', 0x182F90, [(U, 4), (U, 5)], None),
    ('w_00183010', 0x183010, [(U, 4), (U, 5)], None),
    ('w_00187EC0', 0x187EC0, [(I, 4), (I, 5)], None),
    ('w_001A2370', 0x1A2370, [(U, 4), (U, 5)], None),
    ('w_001AF890', 0x1AF890, [(U, 4)], None),
    ('w_001AFC10', 0x1AFC10, [(U, 4)], None),
    ('w_001B0FD0', 0x1B0FD0, [(U, 4)], I),
    ('w_001B10B0', 0x1B10B0, [(U, 4), (I, 5), (I, 6)], I),
    ('w_001B1240', 0x1B1240, [(U, 4), (F, 12), (F, 13)], F),
    ('w_001B1470', 0x1B1470, [(F, 12)], F),
    ('w_001B17A0', 0x1B17A0, [(U, 4)], I),
    ('w_001B1B70', 0x1B1B70, [(U, 4)], None),
    ('w_001B1DE0', 0x1B1DE0, [(U, 4)], None),
    ('w_001B1E20', 0x1B1E20, [(I, 4), (I, 5)], None),
    ('w_001B1EA0', 0x1B1EA0, [(I, 4), (U, 5), (U, 6), (I, 7)], I),
    ('w_001B6660', 0x1B6660, [(U, 4)], U),
    ('w_001B6F80', 0x1B6F80, [(U, 4), (F, 12)], None),
    ('w_001BA1A0', 0x1BA1A0, [(U, 4), (U, 5)], None),
    ('w_001BA1C0', 0x1BA1C0, [(U, 4), (I, 5)], I),
    ('w_001BA1F0', 0x1BA1F0, [(U, 4)], I),
    ('w_001BA580', 0x1BA580, [(U, 4), (I, 5)], None),
    ('w_001BB520', 0x1BB520, [(U, 4), (U, 5)], None),
    ('w_001BB560', 0x1BB560, [(U, 4), (U, 5), (I, 6)], I),
    ('w_001BB7C0', 0x1BB7C0, [(U, 4), (U, 5)], I),
    ('w_001BB7F0', 0x1BB7F0, [(U, 4), (U, 5)], I),
    ('w_001BC150', 0x1BC150, [(U, 4), (U, 5)], None),
    ('w_001C4760', 0x1C4760, [(I, 4), (I, 5)], None),
    ('w_001C47A0', 0x1C47A0, [(I, 4), (I, 5)], None),
    ('w_001C5570', 0x1C5570, [(U, 4), (U, 5), (I, 6), (I, 7)], U),
    ('w_001C6120', 0x1C6120, [(U, 4), (I, 5)], U),
    ('w_001C62C0', 0x1C62C0, [(U, 4)], None),
    ('w_001C6380', 0x1C6380, [(U, 4)], None),
    ('w_001C63E0', 0x1C63E0, [(U, 4), (I, 5)], None),
    ('w_001C64F0', 0x1C64F0, [(U, 4), (F, 12)], I),
    ('w_001C67E0', 0x1C67E0, [(U, 4), (I, 5), (F, 12), (F, 13)], None),
    ('w_001C68C0', 0x1C68C0, [(U, 4)], None),
    ('w_001CA5E0', 0x1CA5E0, [(U, 4), (U, 5), (I, 6)], None),
    ('w_001CA6E0', 0x1CA6E0, [(U, 4), (U, 5)], None),
    ('w_001D0C80', 0x1D0C80, [(U, 4), (U, 5)], None),
    ('w_001D0D40', 0x1D0D40, [(U, 4), (U, 5), (I, 6), (I, 7)], None),
    ('w_001D0D60', 0x1D0D60, [(U, 4), (F, 12)], None),
    ('w_001EFD20', 0x1EFD20, [(I, 4), (U, 5)], U),
    ('w_001F4BF0', 0x1F4BF0, [(U, 4), (U, 5)], None),
    ('w_001FABB0', 0x1FABB0, [], None),
    ('w_001FAE70', 0x1FAE70, [(I, 4)], None),
    ('w_001FB0B0', 0x1FB0B0, [(I, 4)], None),
    ('w_001FB9F0', 0x1FB9F0, [(I, 4), (I, 5), (I, 6), (I, 7)], None),
    ('w_001FBD50', 0x1FBD50, [(U, 4), (I, 5), (I, 6), (F, 12)], None),
]
BY_ADDRESS = {address: (name, args, result) for name, address, args, result in HOOKS}

# Callees that run as original code inside the oracle (writes replayed to
# the native side). Pure helpers only: none of them reaches hardware.
RUN = {0x11DE90, 0x11DF78, 0x11E2A8, 0x11E748, 0x1028D0, 0x102900, 0x102948, 0x121870, 0x122BB8, 0x1B1240,
       0x1B1470, 0x1BA1A0, 0x1D0D60}

# AREA04 has no register-indirect jump other than the returns (decomp
# docs/AREA04_OVERLAY.md), and neither boot function has one.
JUMP_TABLES = {}
TABLE_BYTES = frozenset()


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


CT = {U: C.c_uint32, I: C.c_int32, F: C.c_float}
ARG_CT = {U: C.c_uint32, I: C.c_int32, F: FloatArg, Q: C.c_uint64}
BYTES_FN = C.CFUNCTYPE(C.c_void_p, C.c_void_p, C.c_uint32, C.c_uint32)
CALLBACK_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_uint32)


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
def build(source=None):
    """Compile the module (or `source`, a copy under test in a mutation
    sweep) into a private library under build/area04/port."""
    OUT.mkdir(parents=True, exist_ok=True)
    source = source or 'src/game/em_area04_port.c'
    stem = 'area04_port' if source == 'src/game/em_area04_port.c' else 'area04_port_%d' % os.getpid()
    lib = OUT / (stem + ('.dylib' if sys.platform == 'darwin' else '.so'))
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc', '-I' + str(ROOT / 'src/game'), str(source),
                    '-lm', '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    for entry, (symbol, _, kind) in FUNCS.items():
        fn = getattr(native, symbol)
        types = [C.POINTER(Hooks)] + [C.c_uint32] * NARGS[kind]
        if kind in RESULT_KINDS:
            types.append(C.POINTER(C.c_int32))
        fn.argtypes = types + [C.POINTER(Fault)]
        fn.restype = C.c_int
    return native


def load_captures(elf):
    """Every AREA04 image must hold the AREA04 overlay (header id 5) with its
    text equal to the user's AREA04.BIN, and every image's boot text must
    equal the pinned ELF: every instruction the oracle executes is
    original (the boot image a04_05 runs boot code only)."""
    overlay = OVERLAY_FILE.read_bytes()
    text_end = ARENA + 0x40 + struct.unpack_from('<I', overlay, 12)[0]
    assert text_end == TEXT_END, hex(text_end)
    captures = {}
    for beat in BEATS:
        folder = ROUTE / beat
        ram = (folder / 'eeMemory.bin').read_bytes()
        spad = (folder / 'scratchpad.bin').read_bytes()
        assert len(ram) == RAM_SIZE and len(spad) == SPAD_SIZE, beat
        if beat in OVERLAY_BEATS:
            assert ram[ARENA:ARENA + 8] == overlay[:8] and ram[ARENA + 4] == 5, (beat, 'AREA04 overlay not resident')
            assert ram[ARENA + 0x40:TEXT_END] == overlay[0x40:TEXT_END - ARENA], (beat, 'overlay text differs from AREA04.BIN')
        assert ram[0x100000:0x241000] == elf[0x300:0x300 + 0x141000], (beat, 'boot text differs from the ELF')
        captures[beat] = (ram, spad)
    return captures


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


def free_node(ram):
    for i in range(NODES):
        node = POOL + i * NODE
        if not any(ram[node:node + 0x20]):
            return node
    raise AssertionError('no free pool node')


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
            ee.call(entry, original_args(entry, args[0]))
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
            poisoned = (v0 & MASK, f0 & MASK, self.rehearsal)
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
            result is None or (poisoned[1] == f0 & MASK if result == F else poisoned[0] == v0 & MASK))
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
                elif result:
                    e.r[2] = sx32(value)
            outcome = None
            if result == F:
                outcome = e.f[0] & MASK
            elif result:
                outcome = e.r[2] & MASK
            self.log.append((name, values, outcome, writes))
        return hook

    def callback_hook(self, callback):
        def cb(e):   # the actor's +0x4C callback: stubbed, logged
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
            return action[1]
        return hook_proto(args, result)(fn)

    def hooks(self, null=None):
        """The hook table; `null` names one field ('bytes', a hook or
        'w_callback') left NULL (hook_contract_site)."""
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
CTX = 0xA04C7C00   # the hook table's ctx in every Replay: opaque, never dereferenced by the module


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
        if kind in RESULT_KINDS and s32(oracle.ee.r[2]) != result:
            errors.append(('return', s32(oracle.ee.r[2]), result))
    return errors


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
    symbol, _, kind = FUNCS[entry]
    fn = getattr(native, symbol)
    result = C.c_int32(0x5A5A5A5A)
    call = [C.byref(hooks)] + list(args)
    if kind in RESULT_KINDS:
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
    errors = finish(replay, oracle, FUNCS[entry][2], status, result, fault)
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
    cost = lambda i: 3 if selected[i][2] in (0x823700, 0x823580, 0x1C1A80, 0x8260C0) else 1   # noqa: E731
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


def callbacks_of(entry, native_args, ram):
    """The +0x4C callbacks the oracle stubs: the entry's own node's (for the
    entries that take a node) and the scribbled ones."""
    if FUNCS[entry][2] not in SELF_KINDS:
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
# +0x4C values the scribbling cases write mid-run: the oracle stubs them like
# the captured callback, so a callback read after the scribble shows the new one.
SCRIBBLED_CALLBACKS = frozenset({0x1234560, 0x2345670})
# Owner behaviours (+0x10) and the entries that take such a node.
OWNER_ENTRIES = {
    0x823700: (0x823700, 0x823580), 0x823B90: (0x823B90,), 0x823EE0: (0x823EE0,), 0x824100: (0x824100,),
    0x8241F0: (0x8241F0,), 0x824320: (0x824490,), 0x8246B0: (0x8246B0, 0x824830, 0x824930),
    0x824DC0: (0x824DC0,), 0x825880: (0x825880,), 0x825B00: (0x825B00,), 0x825D60: (0x825D60,),
    0x825DF0: (0x825DF0,), 0x8260C0: (0x8260C0,),
}
PLAYER_X, PLAYER_Y, PLAYER_Z = 0x810350, 0x810354, 0x810358
E4, F764, F845 = 0x8107E4, 0x810764, 0x810845
B84, B8D = 0x70003B84, 0x70003B8D


def any_owner(ram, callback):
    """Pool nodes whose +0x10 behaviour is `callback`, live or not."""
    return [POOL + i * NODE for i in range(NODES) if u32(ram, POOL + i * NODE + 0x10) == callback]


F_VALUES = [0.0, -0.0, 1e-39, -1e-39, 0.25, -0.5, 1.0, 10.0, 9.99999, 16.0, 20.0, 380.0, 424.0, 499.0,
            3.4028235e38, -3.4028235e38]


def rf(rng):
    pick = rng.random()
    if pick < 0.4:
        return rng.choice(F_VALUES)
    if pick < 0.8:
        return rng.uniform(-12.0, 12.0)
    return rng.uniform(-600.0, 600.0)


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


NAN, NEG_INF = 0x7FC00000, 0xFF800000


def owner_args(entry, node):
    return entry_args(entry, node)


def case_list(elf):
    rng = random.Random(0xA04)
    cases = []
    count = reference_mode.pick(60, 0)

    def add(label, beat, entry, args, patches=(), spad=(), queues=None, default=None, run_set=RUN):
        cases.append((label, beat, entry, tuple(args), list(patches), list(spad),
                      Script(queues, default), frozenset(run_set)))

    for beat in OVERLAY_BEATS:
        ram, spad = CAPTURES[beat]
        add('capture %s 823B40' % beat, beat, 0x823B40, [])
        for callback, entries in sorted(OWNER_ENTRIES.items()):
            for node in owners(ram, callback):
                for entry in entries:
                    add('capture %s %06X @%X' % (beat, entry, node), beat, entry, entry_args(entry, node))
        for node in any_owner(ram, 0x823B90):
            if ram[node] == 0:   # the director after it freed itself
                add('capture %s 823B90 freed @%X' % (beat, node), beat, 0x823B90, [node])
    ram, spad = CAPTURES[BOOT_BEAT]
    for node in owners(ram, 0x1C1A80):
        for entry in BOOT_ENTRIES:
            add('capture %s %06X @%X' % (BOOT_BEAT, entry, node), BOOT_BEAT, entry, entry_args(entry, node))
    captured = len(cases)
    targeted_cases(add)
    targeted = len(cases) - captured

    # ---------------------------------------------------------------- perturbed
    for i in range(count):
        beat = rng.choice(['a04_00_door45_event', 'a04_01_door40', 'a04_02_console', 'a02_05_progression_exit'])
        ram, _ = CAPTURES[beat]
        for callback, entries in sorted(OWNER_ENTRIES.items()):
            for node in owners(ram, callback):
                entry = rng.choice(entries)
                p = [(node + 4, b8(rng.choice([0, 1, 1, 1, 2, 3, 4, 4, 0x64, 0xFF]))),
                     (node + 5, b8(rng.choice([0, 1, 2, 3, 4]))), (node + 6, b8(rng.randrange(4))),
                     (node + 0xB, b8(rng.choice([0, 4, 0xFB, 0xFF]))),
                     (node + 0x28, h16(rng.choice([0, 1, 2, 0x78, 0x77, 0xFFFF]))),
                     (node + 0xB0, f32b(rf(rng))), (node + 0x1FC, f32b(rf(rng))),
                     (node + 0x1F0, w32(rng.choice([0, 1]))), (node + 0x1F4, w32(rng.choice([0, 1, 2]))),
                     (E4, b8(rng.choice([0, 1, 2, 3]))), (F764, b8(rng.choice([0, 0xFF]))),
                     (F845, b8(rng.randrange(256))), (0x8107EA, b8(rng.choice([0, 1, 2, 0x10, 0x20, 0xFF]))),
                     (0x81076A, b8(rng.choice([0, 1]))), (0x81076C, b8(rng.choice([0, 1, 0xFF]))),
                     (0x81083B, b8(rng.choice([0, 0xFF]))), (0x81083D, b8(rng.choice([0, 1]))),
                     (0x810834, b8(rng.choice([0, 1]))), (PLAYER_X, f32b(rf(rng)))]
                q = {a: [rng.choice([0, 1, 2, -1]) for _ in range(3)]
                     for a in (0x1BA1F0, 0x1B0FD0, 0x1BA1C0, 0x1B1EA0, 0x1B17A0, 0x1BB560, 0x1BB7C0, 0x1BB7F0,
                               0x1B10B0)}
                add('perturbed %06X %d' % (entry, i), beat, entry, entry_args(entry, node), p, queues=q)
    return cases, captured, targeted


def targeted_cases(add):
    """Designed cases for every state, step and test; all run by default."""
    beat = 'a04_00_door45_event'
    ram, spad = CAPTURES[beat]
    door = owners(ram, 0x823700)[0]
    r47 = owners(ram, 0x823EE0)[0]
    npc = owners(ram, 0x824320)[0]
    r3 = owners(ram, 0x8246B0)[0]
    r4 = owners(ram, 0x824DC0)[0]
    r61 = owners(ram, 0x8241F0)[0]
    m66 = owners(ram, 0x825880)[0]
    lift = owners(ram, 0x825B00)[0]
    r68 = owners(ram, 0x825D60)[0]
    console = owners(ram, 0x825DF0)[0]
    reel = owners(ram, 0x8260C0)[0]
    freed_director = any_owner(ram, 0x823B90)[0]
    spare = free_node(ram)
    a0205 = 'a02_05_progression_exit'
    ram5 = CAPTURES[a0205][0]
    director = owners(ram5, 0x823B90)[0]
    group = owners(ram5, 0x824100)[0]
    ALL = (0, 1, 2, -1) + WIDE_RESULTS

    # ---- 0x823B40 the script callback
    for v in (0, 8, 0xF7, 0xFF, 0x20, 0x28):
        add('823B40 845 %02X' % v, beat, 0x823B40, [], [(F845, b8(v))])

    # ---- 0x823580 the talk turn (door [45]; who = door + 0x1F0)
    talk = entry_args(0x823580, door)
    for use in (0, 4, 0xFB, 0xFF, 0x84):
        add('823580 use %02X' % use, beat, 0x823580, talk, [(door + 0xB, b8(use))])
    for dx, dz in ((0, 10), (10, 0), (0, -10), (-10, 0), (7, 7), (-7, 7), (7, -7), (-7, -7)):
        for yaw in (0.0, 3.1415927, 1.5707964, -3.0):
            for b3 in (0x16, 0x15):
                add('823580 at %d %d yaw %g b3 %X' % (dx, dz, yaw, b3), beat, 0x823580, talk,
                    [(door + 0xB, b8(4)), (door + 3, b8(b3)), (door + 0xC4, f32b(yaw)),
                     (PLAYER_X, f32b(440.0 + dx)), (PLAYER_Z, f32b(109.3 + dz))])
    for fab in (fbits(1.5707964), up(1.5707964), down(1.5707964), NAN, 0x80000000, NEG_INF):
        for b3 in (0x16, 0x07):
            for side in (0, 1, 0xFFFF):
                add('823580 fabs %08X b3 %X side %X' % (fab, b3, side), beat, 0x823580, talk,
                    [(door + 0xB, b8(4)), (door + 3, b8(b3)), (door + 0x2E, h16(side))],
                    queues={0x11DF78: [fab]}, run_set=RUN - {0x11DF78})
    add('823580 scribble wrap', beat, 0x823580, talk, [(door + 0xB, b8(4))],
        queues={0x1B1470: [Scribble(fb(0.5), [(door + 0xC4, f32b(2.0))]), fb(1.25)]}, run_set=RUN - {0x1B1470})
    add('823580 scribble wrap far', beat, 0x823580, talk, [(door + 0xB, b8(4))],
        queues={0x1B1470: [Scribble(fb(3.0), [(door + 0xC4, f32b(-2.0))]), fb(-1.25)]}, run_set=RUN - {0x1B1470})
    add('823580 scribble sin', beat, 0x823580, talk, [(door + 0xB, b8(4))],
        queues={0x11E2A8: [Scribble(fb(0.5), [(door + 0xB0, f32b(7.0)), (PLAYER + 0xA4, f32b(3.0)),
                                               (PLAYER + 0xC4, f32b(1.25)), (door + 0xB8, f32b(-4.0))])]},
        run_set=RUN - {0x11E2A8})
    add('823580 scribble cos', beat, 0x823580, talk, [(door + 0xB, b8(4))],
        queues={0x11DE90: [Scribble(fb(-0.5), [(door + 0xB8, f32b(9.0))])]}, run_set=RUN - {0x11DE90})
    add('823580 scribble angle', beat, 0x823580, talk, [(door + 0xB, b8(4))],
        queues={0x1B1240: [Scribble(fb(0.75), [(door + 0xC4, f32b(0.25)), (door + 3, b8(0x16))])]},
        run_set=RUN - {0x1B1240})
    add('823580 scribble move', beat, 0x823580, talk, [(door + 0xB, b8(4)), (door + 3, b8(0x16))],
        queues={0x1B1470: [Scribble(fb(0.5), [(door + 3, b8(0x15))]), Scribble(fb(0.5), [(door + 0x2E, h16(5))])]},
        run_set=RUN - {0x1B1470})
    for px, pz in ((1e-39, -1e-39), (3.4028235e38, 1.0), (440.0, 109.3), (-0.0, 0.0)):
        add('823580 player %g %g' % (px, pz), beat, 0x823580, talk,
            [(door + 0xB, b8(4)), (PLAYER_X, f32b(px)), (PLAYER_Z, f32b(pz))])
    add('823580 player NaN', beat, 0x823580, talk, [(door + 0xB, b8(4)), (PLAYER_X, bits32(NAN))])
    for result in (0, 1, -1, 0x80000000):
        add('823580 done %X' % (result & MASK), beat, 0x823580, talk, [(door + 0xB, b8(4))],
            queues={0x1BA1F0: [result]})

    # ---- 0x823700 door [45]
    for st in (0, 2, 3, 4, 0xFF):
        add('823700 s%X' % st, beat, 0x823700, [door], [(door + 4, b8(st))])
    for idx in (4, 0, 0xFF):
        for shift in (5, 0, 31, 32, 37, -1, 0x7FFF, 16, 21, 48):   # 16/21/48: (shift & 31) in 16..23, above the 8-bit lock
            bit = 1 << (shift & 31)
            for lock in sorted({0, bit & 0xFF, 0xFF ^ (bit & 0xFF), 0xFF}):
                for use in (0, 4):
                    add('823700 lock idx %X shift %d lock %02X use %d' % (idx, shift, lock, use), beat, 0x823700,
                        [door], [(door + 4, b8(1)), (door + 5, b8(0)), (0x810700, b8(idx)),
                                 (door + 0x34, h16(shift)), (0x810841 + idx, b8(lock)), (door + 0xB, b8(use))],
                        queues={0x1BB560: [1]})
    for result in ALL:
        add('823700 sub0 locked %X' % (result & MASK), beat, 0x823700, [door],
            [(door + 4, b8(1)), (door + 5, b8(0)), (F845, b8(0x20))], queues={0x1BB560: [result]})
        for sub, hook in ((1, 0x1BB7C0), (2, 0x1BB7C0), (4, 0x1BB7F0)):
            add('823700 sub%d %X' % (sub, result & MASK), beat, 0x823700, [door],
                [(door + 4, b8(1)), (door + 5, b8(sub)), (door + 0xB, b8(4))], queues={hook: [result]})
    for sub in (3, 5, 0xFF):
        add('823700 sub%X' % sub, beat, 0x823700, [door], [(door + 4, b8(1)), (door + 5, b8(sub))])
    add('823700 sub2 scribble', beat, 0x823700, [door], [(door + 4, b8(1)), (door + 5, b8(2))],
        queues={0x1BB7C0: [Scribble(1, [(door + 5, b8(7))])]})
    add('823700 sub3 scribble', beat, 0x823700, [door], [(door + 4, b8(1)), (door + 5, b8(3))],
        queues={0x1BC150: [Scribble(0, [(door + 5, b8(9))])]})
    add('823700 talk scribble', beat, 0x823700, [door],
        [(door + 4, b8(1)), (door + 5, b8(0)), (door + 0xB, b8(4)), (F845, b8(0))],
        queues={0x1BA1F0: [Scribble(1, [(door + 5, b8(6))])]})
    # review round: the lock byte is 8 bits, so a shift with (shift & 31) in 8..31 never unlocks; pinned
    # with the flag byte D_00810845 = 0x01 (the talk turn 001B1240 runs, a wrong mask would call 001BB560)
    for shift in (16, 21):
        add('823700 lock shift %d 845 01' % shift, beat, 0x823700, [door],
            [(door + 4, b8(1)), (door + 5, b8(0)), (0x810700, b8(4)), (door + 0x34, h16(shift)),
             (F845, b8(0x01)), (door + 0xB, b8(4))], queues={0x1BB560: [1]})
    for sq in (fb(20.0), up(20.0), down(20.0), NAN, 0x80000000, 0):
        for b2 in (0x85, 0x05, 0x80):
            add('823700 near %08X b2 %02X' % (sq, b2), beat, 0x823700, [door],
                [(door + 4, b8(1)), (door + 5, b8(0xFF)), (door + 2, b8(b2))],
                queues={0x11E748: [sq]}, run_set=RUN - {0x11E748})
    for dx, dy, dz in ((20.0, 0.0, 0.0), (0.0, 20.0, 0.0), (12.0, 0.0, 16.0), (0.0, 0.0, 20.000002),
                       (1e-20, 1e-20, 1e-20), (3e19, 0.0, 0.0), (0.0, -0.0, 0.0)):
        add('823700 player %g %g %g' % (dx, dy, dz), beat, 0x823700, [door],
            [(door + 4, b8(1)), (door + 5, b8(0xFF)), (PLAYER_X, f32b(440.0 + dx)),
             (PLAYER_Y, f32b(14.9 + dy)), (PLAYER_Z, f32b(109.3 + dz))])
    # review round: three non-zero axes pin the order of the three-term sum (dx*dx + dy*dy first, then dz*dz):
    # for each of these the truncating EE sum differs in the last bit under both other orders
    for bx, by, bz in ((0x43D95B85, 0x408D9D7D, 0x42E3A7F4), (0x43E043C4, 0x3F63E341, 0x42F09D0F),
                       (0x43DF7C30, 0x40699B75, 0x42E4CC12), (0x43DDCC6A, 0x41912B6B, 0x42C78683)):
        add('823700 player bits %08X %08X %08X' % (bx, by, bz), beat, 0x823700, [door],
            [(door + 4, b8(1)), (door + 5, b8(0xFF)), (PLAYER_X, bits32(bx)), (PLAYER_Y, bits32(by)),
             (PLAYER_Z, bits32(bz))])
    add('823700 scribble c6380', beat, 0x823700, [door], [(door + 4, b8(1)), (door + 5, b8(0xFF))],
        queues={0x1C6380: [Scribble(0, [(door + 0x4C, w32(0x1234560)), (PLAYER_X, f32b(441.0)),
                                        (door + 0xB4, f32b(20.0)), (door + 2, b8(0x80))])]})

    # ---- 0x823B90 the director
    for node, where in ((director, a0205), (freed_director, beat)):
        for st in (2, 3, 4, 0xFF):
            add('823B90 %s s%X' % (where, st), where, 0x823B90, [node], [(node + 4, b8(st))])
        for v845 in (0, 0x20, 0xDF, 0xFF):
            for result in ((0, 1, -1, 2, 0x80000000) if not v845 & 0x20 else (0,)):
                for e4 in ((0, 1, 2, 0xFF) if result == 0 else (0,)):
                    add('823B90 %s s0 845 %02X talk %X E4 %X' % (where, v845, result & MASK, e4), where, 0x823B90,
                        [node], [(node + 4, b8(0)), (F845, b8(v845)), (E4, b8(e4))], queues={0x1BA1C0: [result]})
    add('823B90 s0 scribble', a0205, 0x823B90, [director], [(director + 4, b8(0)), (F845, b8(0)), (E4, b8(0))],
        queues={0x1FABB0: [Scribble(0, [(director + 4, b8(6))])]})
    for b8d in (0, 1, 0xFF):
        for c in (1, 0x81, 0, 2):
            for h in (0xAA, 0xAB, 0x1AA, 0):
                if b8d == 0 and (c, h) != (1, 0xAA):
                    continue
                add('823B90 E0 8D %X c %X 84 %X' % (b8d, c, h), a0205, 0x823B90, [director],
                    [(director + 4, b8(1)), (E4, b8(0)), (director + 0x1FC, b8(c))], [(B8D, b8(b8d)), (B84, h16(h))])
    for result in ALL:
        add('823B90 E0 done %X' % (result & MASK), a0205, 0x823B90, [director], [(director + 4, b8(1)), (E4, b8(0))],
            queues={0x1BA1F0: [result]})
    for e4 in (1, 2):
        for v845 in (0, 0x20, 0xDF):
            for y in (16.0, 15.0, -0.0, 1e-39):
                for quad in ((0, 1, 2, -1, 0x80000000) if not v845 & 0x20 and y == 15.0 else (1,)):
                    add('823B90 E%d step0 845 %02X y %g quad %X' % (e4, v845, y, quad & MASK), a0205, 0x823B90,
                        [director], [(director + 4, b8(1)), (director + 5, b8(0)), (E4, b8(e4)), (F845, b8(v845)),
                                     (PLAYER_Y, f32b(y))], queues={0x1B1EA0: [quad]})
            for yb in (up(16.0), NAN, NEG_INF, 0xFF7FFFFF, fbits(3.4028235e38)):
                add('823B90 E%d step0 845 %02X y %08X' % (e4, v845, yb), a0205, 0x823B90, [director],
                    [(director + 4, b8(1)), (director + 5, b8(0)), (E4, b8(e4)), (F845, b8(v845)),
                     (PLAYER_Y, bits32(yb))], queues={0x1B1EA0: [1]})
        for result in ALL:
            add('823B90 E%d step1 %X' % (e4, result & MASK), a0205, 0x823B90, [director],
                [(director + 4, b8(1)), (director + 5, b8(1)), (E4, b8(e4))], queues={0x1BA1F0: [result]})
        for step in (2, 0xFF):
            add('823B90 E%d step%X' % (e4, step), a0205, 0x823B90, [director],
                [(director + 4, b8(1)), (director + 5, b8(step)), (E4, b8(e4))])
    for e4 in (3, 0xFF, 0x80):
        add('823B90 E%X' % e4, a0205, 0x823B90, [director], [(director + 4, b8(1)), (E4, b8(e4))])
    add('823B90 step0 scribble', a0205, 0x823B90, [director],
        [(director + 4, b8(1)), (director + 5, b8(0)), (E4, b8(1)), (F845, b8(0)), (PLAYER_Y, f32b(10.0))],
        queues={0x1B1EA0: [Scribble(1, [(director + 5, b8(4))])]})

    # ---- 0x823EE0 [47]
    for st in (3, 4, 0xFF):
        add('823EE0 s%X' % st, beat, 0x823EE0, [r47], [(r47 + 4, b8(st))])
    for v764 in (0xFF, 0, 1):
        for i701 in (0, 1, 7, 0xFF):
            for result in ((0, 1) if v764 == 0 and i701 == 0 else (0,)):
                add('823EE0 s0 764 %X 701 %X fd0 %d' % (v764, i701, result), beat, 0x823EE0, [r47],
                    [(r47 + 4, b8(0)), (F764, b8(v764)), (0x810701, b8(i701))], queues={0x1B0FD0: [result]})
    for v in (0, 0xFF):
        add('823EE0 s0 scribble 764 %X' % v, beat, 0x823EE0, [r47], [(r47 + 4, b8(0)), (F764, b8(0xFF ^ v))],
            queues={0x1CA5E0: [Scribble(0, [(F764, b8(v))])]})
    for v764 in (0xFF, 0):
        for e4 in (2, 1, 0xFF):
            add('823EE0 s1 sub0 764 %X E4 %X' % (v764, e4), beat, 0x823EE0, [r47],
                [(r47 + 4, b8(1)), (r47 + 5, b8(0)), (F764, b8(v764)), (E4, b8(e4))])
    for h in (0x55C, 0x55D, 0x55E, 0xFFFF, 0, 0x8000):
        for v764 in (0xFF, 0):
            for kind in (8, 9):
                for i701 in (0, 1):
                    add('823EE0 s1 sub1 84 %X 764 %X kind %d 701 %d' % (h, v764, kind, i701), beat, 0x823EE0,
                        [r47], [(r47 + 4, b8(1)), (r47 + 5, b8(1)), (F764, b8(v764)), (r47 + 0xD, b8(kind)),
                                (0x810701, b8(i701))], [(B84, h16(h))], queues={0x1C6120: [0x7E0000 + h]})
    for sub in (2, 0xFF):
        add('823EE0 s1 sub%X' % sub, beat, 0x823EE0, [r47], [(r47 + 4, b8(1)), (r47 + 5, b8(sub))])
    add('823EE0 s1 scribble', beat, 0x823EE0, [r47], [(r47 + 4, b8(1)), (r47 + 5, b8(1))], [(B84, h16(0x600))],
        queues={0x1C62C0: [Scribble(0, [(r47 + 0x44, w32(0x1234)), (r47 + 0x4C, w32(0x2345670))])],
                0x1C6120: [Scribble(0x7F0000, [(r47 + 0xD, b8(3))])]})
    link1 = u32(ram, r47 + 0x18)
    link = u32(ram, link1 + 0x18)
    add('823EE0 s2 holder', beat, 0x823EE0, [r47], [(0x275B40, w32(0x7A0000)), (0x7A0000, w32(door))])
    add('823EE0 s2 bone', beat, 0x823EE0, [r47], [(link + 0x110, w32(door))])
    add('823EE0 s2 scribble', beat, 0x823EE0, [r47], [], queues={0x1C6380: [Scribble(0, [(r47 + 0x4C, w32(0x1234560))])]})
    for where, patch in (('first', (r47 + 0x18, w32(0x02400000))), ('link', (link1 + 0x18, w32(0x02400000))),
                         ('bone', (link + 0x110, w32(0x02400000))), ('holder', (0x275B40, w32(0x02400000))),
                         ('object', (0x275B40, w32(0x7A0000))), ('odd', (r47 + 0x18, w32(0x7B0002)))):
        extra = [(0x7A0000, w32(0x02400000))] if where == 'object' else []
        add('823EE0 unmapped %s' % where, beat, 0x823EE0, [r47], [patch] + extra)

    # ---- 0x824100 the group node (a02_05)
    for st in (2, 3, 4, 0xFF):
        add('824100 s%X' % st, a0205, 0x824100, [group], [(group + 4, b8(st))])
    for result in ALL:
        add('824100 s0 %X' % (result & MASK), a0205, 0x824100, [group], [(group + 4, b8(0))],
            queues={0x1B10B0: [result]})
        for e4 in (2, 1):
            add('824100 s1 %X E4 %d' % (result & MASK, e4), a0205, 0x824100, [group],
                [(group + 4, b8(1)), (E4, b8(e4))], queues={0x1B17A0: [result]})
    add('824100 s0 scribble', a0205, 0x824100, [group], [(group + 4, b8(0))],
        queues={0x1C63E0: [Scribble(0, [(group + 4, b8(7))])]})
    add('824100 s1 scribble', a0205, 0x824100, [group], [(group + 4, b8(1)), (E4, b8(0))],
        queues={0x1C68C0: [Scribble(0, [(group + 0x4C, w32(0x1234560)), (E4, b8(2))])], 0x1B17A0: [1]})
    add('824100 s1 C64F0 result', a0205, 0x824100, [group], [(group + 4, b8(1))], queues={0x1C64F0: [-1]})

    # ---- 0x8241F0 [61]
    for st in (2, 3, 4, 0xFF):
        add('8241F0 s%X' % st, beat, 0x8241F0, [r61], [(r61 + 4, b8(st))])
    for q in ([0, 0], [1], [0, 1], [0, -1], [2], [0x80000000], [0, 0x100], [-1]):
        add('8241F0 s0 %s' % q, beat, 0x8241F0, [r61], [(r61 + 4, b8(0))], queues={0x1BA1C0: list(q)})
    for f83d in (0, 1, 0xFF):
        for sub in (0, 1, 2, 0xFF):
            for result in ((0, 1, -1, 2) if sub == 1 and f83d else (0,)):
                add('8241F0 s1 83D %X sub %X done %X' % (f83d, sub, result & MASK), beat, 0x8241F0, [r61],
                    [(r61 + 4, b8(1)), (r61 + 5, b8(sub)), (0x81083D, b8(f83d))], queues={0x1BA1F0: [result]})

    # ---- 0x824490 the NPC's first sub-state (sp)
    nargs = entry_args(0x824490, npc)
    for result in (0, 1, 2, -1, 0x10001, 0x80000000):
        add('824490 sub0 %X' % (result & MASK), beat, 0x824490, nargs, [(npc + 5, b8(0))],
            queues={0x1B1EA0: [result]})
    for result in (0, 1, 2, -1):
        for clip in (0x12345, -1, 0x8000, 0):
            add('824490 sub1 %X clip %X' % (result & MASK, clip & MASK), beat, 0x824490, nargs, [(npc + 5, b8(1))],
                queues={0x1BA1F0: [result], 0x1C64F0: [clip]})
    for sub in (2, 0xFF):
        add('824490 sub%X' % sub, beat, 0x824490, nargs, [(npc + 5, b8(sub))])
    add('824490 sub1 scribble', beat, 0x824490, nargs, [(npc + 5, b8(1))],
        queues={0x1BA1F0: [1], 0x1FAE70: [Scribble(0, [(npc + 0xD, b8(0x44))])]})
    add('824490 sub0 scribble', beat, 0x824490, nargs, [(npc + 5, b8(0))],
        queues={0x1B1EA0: [Scribble(1, [(npc + 0xD, b8(0x45)), (npc + 5, b8(3))])]})
    add('824490 area', beat, 0x824490, nargs, [(npc + 5, b8(0)), (0x828220, f32b(-1.0)), (0x82825C, f32b(1e-39))])

    # ---- 0x8246B0 [3] and its sub-states 0x824830 / 0x824930
    for st in (2, 3, 4, 0xFF):
        add('8246B0 s%X' % st, beat, 0x8246B0, [r3], [(r3 + 4, b8(st))])
    for result in (0, 1, -1, 0x100):
        add('8246B0 s0 %X' % (result & MASK), beat, 0x8246B0, [r3], [(r3 + 4, b8(0))],
            queues={0x1BA1C0: [result], 0x1B10B0: [result]})
    for f76a in (0, 1, 0xFF):
        for ea in (0, 0x10, 1, 2, 0x20, 0xFF, 3, 0x11):
            for sub in ((0, 1, 2) if f76a == 1 else (0,)):
                add('8246B0 s1 76A %X 7EA %X sub %d' % (f76a, ea, sub), beat, 0x8246B0, [r3],
                    [(r3 + 4, b8(1)), (0x81076A, b8(f76a)), (0x8107EA, b8(ea)), (r3 + 5, b8(sub)),
                     (r3 + 6, b8(sub)), (r3 + 0x240, w32(door if sub else 0))], queues={0x1BA1F0: [1]})
    add('8246B0 s1 scribble 7EA', beat, 0x8246B0, [r3],
        [(r3 + 4, b8(1)), (0x81076A, b8(1)), (0x8107EA, b8(1)), (r3 + 6, b8(1))],
        queues={0x1BA1F0: [Scribble(0, [(0x8107EA, b8(0))])]})
    add('8246B0 s1 scribble callback', beat, 0x8246B0, [r3],
        [(r3 + 4, b8(1)), (0x81076A, b8(1)), (0x8107EA, b8(2)), (r3 + 6, b8(3))],
        queues={0x1C68C0: [Scribble(0, [(r3 + 0x4C, w32(0x2345670))])]})
    for ea in (0, 0x10, 0xFF):
        for obj in (0x7C0000, 0, 0x7FFFFFFF):
            add('824830 sub0 7EA %X obj %X' % (ea, obj), beat, 0x824830, [r3], [(r3 + 5, b8(0)), (0x8107EA, b8(ea))],
                queues={0x1B6660: [obj, 0x7C1000]})
    for result in (0, 1, -1, 2):
        add('824830 sub1 %X' % (result & MASK), beat, 0x824830, [r3], [(r3 + 5, b8(1))],
            queues={0x1BA1F0: [result], 0x1B6660: [0x7C2000, 0]})
    for sub in (2, 3, 0xFF):
        add('824830 sub%X' % sub, beat, 0x824830, [r3], [(r3 + 5, b8(sub))])
    for obj in (0, door, 0x100, reel):
        add('824930 sub0 obj %X' % obj, beat, 0x824930, [r3], [(r3 + 6, b8(0)), (r3 + 0x240, w32(obj))],
            queues={0x1EFD20: [0x7C3000]})
    for result in (0, 1, -1, 2):
        add('824930 sub1 %X' % (result & MASK), beat, 0x824930, [r3], [(r3 + 6, b8(1))], queues={0x1BA1F0: [result]})
    for sub in (2, 3, 4, 0xFF):
        add('824930 sub%X' % sub, beat, 0x824930, [r3], [(r3 + 6, b8(sub))])
    add('824930 sub0 scribble', beat, 0x824930, [r3], [(r3 + 6, b8(0)), (r3 + 0x240, w32(0))],
        queues={0x1B6660: [Scribble(0, [(r3 + 6, b8(9))])]})

    # ---- 0x824DC0 [4]
    for st in (2, 3, 4, 0xFF):
        add('824DC0 s%X' % st, beat, 0x824DC0, [r4], [(r4 + 4, b8(st))])
    for result in (0, 1, -1, 2):
        add('824DC0 s0 %X' % (result & MASK), beat, 0x824DC0, [r4], [(r4 + 4, b8(0))], queues={0x1BA1C0: [result]})
    for f76c in (0, 1, 2, 0xFF):
        for sub in (0, 1, 2, 3):
            for result in ((0, 1, -1, 2) if f76c == 1 and sub == 1 else (0,)):
                add('824DC0 s1 76C %X sub %d done %X' % (f76c, sub, result & MASK), beat, 0x824DC0, [r4],
                    [(r4 + 4, b8(1)), (r4 + 5, b8(sub)), (0x81076C, b8(f76c))], queues={0x1BA1F0: [result]})

    # ---- 0x825880 [66]
    for st in (2, 3, 5, 0xFF):
        add('825880 s%X' % st, beat, 0x825880, [m66], [(m66 + 4, b8(st))])
    for result in (0, 1, -1, 2):
        add('825880 s0 %X' % (result & MASK), beat, 0x825880, [m66], [(m66 + 4, b8(0))], queues={0x1B0FD0: [result]})
    for use in (0, 4, 0xFB, 0xFF):
        for result in ((0, 1, -1) if use & 4 else (0,)):
            add('825880 s1 use %X done %X' % (use, result & MASK), beat, 0x825880, [m66],
                [(m66 + 4, b8(1)), (m66 + 0xB, b8(use))], queues={0x1BA1F0: [result], 0x1B17A0: [result]})
    for x, y, z in ((0.0, 0.04, -0.34), (1e-39, -0.0, 3.4028235e38), (-3.4028235e38, 0.04000001, 0.3399999),
                    (1.0, 1e-8, -1e-8)):
        for st in (1, 4):
            add('825880 s%d pos %g %g %g' % (st, x, y, z), beat, 0x825880, [m66],
                [(m66 + 4, b8(st)), (m66 + 0xB0, f32b(x)), (m66 + 0xB4, f32b(y)), (m66 + 0xB8, f32b(z))])
    add('825880 s1 pos NaN', beat, 0x825880, [m66], [(m66 + 4, b8(1)), (m66 + 0xB4, bits32(NAN))])
    for count in (0, 0x76, 0x77, 0x78, 0x79, 0x7FFF, 0x8000, 0xFFFF):
        for result in ((0, 1, -1) if count == 0x77 else (0,)):
            add('825880 s4 count %X done %X' % (count, result & MASK), beat, 0x825880, [m66],
                [(m66 + 4, b8(4)), (m66 + 0x28, h16(count)), (m66 + 0xB, b8(4))], queues={0x1BA1F0: [result]})
    add('825880 s1 scribble', beat, 0x825880, [m66], [(m66 + 4, b8(1)), (m66 + 0xB, b8(0))],
        queues={0x1B17A0: [Scribble(0, [(m66 + 0xB, b8(4))])],
                0x1BA1F0: [Scribble(0, [(m66 + 0x18, w32(door)), (m66 + 0xB0, f32b(5.0))])]})
    add('825880 s4 scribble', beat, 0x825880, [m66], [(m66 + 4, b8(4)), (m66 + 0x28, h16(0x77))],
        queues={0x1FBD50: [Scribble(0, [(m66 + 0x28, h16(3))])],
                0x1F4BF0: [Scribble(0, [(m66 + 0x4C, w32(0x1234560))])]})

    # ---- 0x825B00 [65]
    for st in (2, 3, 4, 0xFF):
        add('825B00 s%X' % st, beat, 0x825B00, [lift], [(lift + 4, b8(st))])
    for result in (0, 1, -1):
        for v834 in (0, 1, 0xFF):
            for y in (14.9, 0.0, 1e-39, 3.4028235e38):
                if result and (v834, y) != (0, 14.9):
                    continue
                add('825B00 s0 %X 834 %X y %g' % (result & MASK, v834, y), beat, 0x825B00, [lift],
                    [(lift + 4, b8(0)), (0x810834, b8(v834)), (lift + 0xB4, f32b(y))], queues={0x1B0FD0: [result]})
    add('825B00 s0 y NaN', beat, 0x825B00, [lift], [(lift + 4, b8(0)), (0x810834, b8(1)), (lift + 0xB4, bits32(NAN))])
    for count in (0, 1, 2, 0x8000, 0xFFFF):
        for moving in (0, 1):
            for direction in (0, 1, 2, -1):
                add('825B00 s1 count %X on %d dir %d' % (count, moving, direction), beat, 0x825B00, [lift],
                    [(lift + 4, b8(1)), (lift + 0x28, h16(count)), (lift + 0x1F0, w32(moving)),
                     (lift + 0x1F4, w32(direction)), (lift + 0x1FC, f32b(5.0)), (lift + 0x1F8, f32b(14.9))])
    for off in (fbits(10.0 - 0.1923077), up(10.0 - 0.1923077), down(10.0 - 0.1923077), fbits(9.8), fbits(10.0),
                0, 0x80000000, NAN, fbits(1e-39), fbits(-5.0), fbits(3.4028235e38)):
        add('825B00 s1 up off %08X' % off, beat, 0x825B00, [lift],
            [(lift + 4, b8(1)), (lift + 0x28, h16(0)), (lift + 0x1F0, w32(1)), (lift + 0x1F4, w32(0)),
             (lift + 0x1FC, bits32(off)), (0x810834, b8(0))])
    for off in (fbits(0.1923077), up(0.1923077), down(0.1923077), 0, 0x80000000, fbits(0.5), NAN, fbits(-1e-39),
                fbits(11.0)):
        add('825B00 s1 down off %08X' % off, beat, 0x825B00, [lift],
            [(lift + 4, b8(1)), (lift + 0x28, h16(0)), (lift + 0x1F0, w32(1)), (lift + 0x1F4, w32(1)),
             (lift + 0x1FC, bits32(off)), (0x810834, b8(1))])
    add('825B00 s1 scribble', beat, 0x825B00, [lift], [(lift + 4, b8(1)), (lift + 0x28, h16(1)), (lift + 0x1F4, w32(0))],
        queues={0x1FBD50: [Scribble(0, [(lift + 0x1F0, w32(0)), (lift + 0x1F4, w32(1))])]})
    add('825B00 s1 scribble 2', beat, 0x825B00, [lift], [(lift + 4, b8(1)), (lift + 0x28, h16(1)), (lift + 0x1F4, w32(1))],
        queues={0x1FBD50: [Scribble(0, [(lift + 0x1F4, w32(0)), (lift + 0x1FC, f32b(2.0)), (lift + 0x1F8, f32b(3.0))])]})
    add('825B00 s1 scribble callback', beat, 0x825B00, [lift], [(lift + 4, b8(1))],
        queues={0x1B1B70: [Scribble(0, [(lift + 0x4C, w32(0x1234560))])]})
    add('825B00 s0 scribble', beat, 0x825B00, [lift], [(lift + 4, b8(0)), (0x810834, b8(1))],
        queues={0x1C6380: [Scribble(0, [(lift + 0xD0, w32(7))])]})

    # ---- 0x825D60 [68]
    for st in (1, 2, 3, 4, 0xFF):
        add('825D60 s%X' % st, beat, 0x825D60, [r68], [(r68 + 4, b8(st))])
    for result in (0, 1, -1, 2):
        add('825D60 s0 %X' % (result & MASK), beat, 0x825D60, [r68], [(r68 + 4, b8(0))], queues={0x1B0FD0: [result]})

    # ---- 0x825DF0 the console [69]
    for st in (2, 3, 5, 0xFF):
        add('825DF0 s%X' % st, beat, 0x825DF0, [console], [(console + 4, b8(st))])
    for result in (0, 1, -1):
        for v83b in (0, 0xFF, 1):
            for spawn in ((spare, 0) if result == 0 else (spare,)):
                add('825DF0 s0 %X 83B %X spawn %X' % (result & MASK, v83b, spawn), beat, 0x825DF0, [console],
                    [(console + 4, b8(0)), (0x81083B, b8(v83b))], queues={0x1B0FD0: [result], 0x1C5570: [spawn]})
    add('825DF0 s0 spawn unmapped', beat, 0x825DF0, [console], [(console + 4, b8(0))], queues={0x1C5570: [0x02400000]})
    add('825DF0 s0 spawn floats', beat, 0x825DF0, [console], [(console + 4, b8(0)), (spare + 0xB0, f32b(-0.7)),
                                                             (spare + 0xB4, bits32(NAN))], queues={0x1C5570: [spare]})
    add('825DF0 s0 scribble', beat, 0x825DF0, [console], [(console + 4, b8(0))],
        queues={0x1C5570: [Scribble(spare, [(console + 0x2EC, w32(door))])]})
    for v702 in (4, 0, 3, 0xFF):
        for use in (0, 4, 0xFB):
            for result in ((0, 1, -1) if use & 4 and v702 == 4 else (0,)):
                add('825DF0 s1 702 %X use %X done %X' % (v702, use, result & MASK), beat, 0x825DF0, [console],
                    [(console + 4, b8(1)), (0x810702, b8(v702)), (console + 0xB, b8(use))],
                    queues={0x1BA1F0: [result], 0x1B17A0: [result]})
    add('825DF0 s1 scribble', beat, 0x825DF0, [console], [(console + 4, b8(1)), (console + 0xB, b8(0))],
        queues={0x1B17A0: [Scribble(0, [(console + 0xB, b8(4))])],
                0x1BA1F0: [Scribble(0, [(console + 0x18, w32(door))])]})
    for use in (0, 1, 4):
        for result in ((0, 1, -1, 2) if use else (0,)):
            for c28, c2a in ((0, 0), (1, 0), (2, 5), (0, 1), (0, 2), (0x8000, 0), (0, 0x8000), (0xFFFF, 1)):
                for fx in ((spare, 0) if c28 == 1 else (spare,)):
                    add('825DF0 s4 use %X done %X 28 %X 2A %X fx %X' % (use, result & MASK, c28, c2a, fx), beat,
                        0x825DF0, [console], [(console + 4, b8(4)), (console + 0xB, b8(use)),
                                              (console + 0x28, h16(c28)), (console + 0x2A, h16(c2a)),
                                              (console + 0x2EC, w32(fx))], queues={0x1BA1F0: [result]})
    add('825DF0 s4 scribble', beat, 0x825DF0, [console],
        [(console + 4, b8(4)), (console + 0x28, h16(1)), (console + 0x2EC, w32(spare))],
        queues={0x1FBD50: [Scribble(0, [(console + 0x2EC, w32(door))])]})
    add('825DF0 s4 scribble 2', beat, 0x825DF0, [console], [(console + 4, b8(4)), (console + 0x2A, h16(1))],
        queues={0x1FBD50: [Scribble(0, [(console + 0x1C, w32(door))])]})
    add('825DF0 s4 scribble 3', beat, 0x825DF0, [console], [(console + 4, b8(4)), (console + 0xB, b8(4))],
        queues={0x1BA1F0: [Scribble(1, [(console + 0x1C, w32(door)), (console + 0x28, h16(3))])]})

    # ---- 0x8260C0 the reel [70]
    for st in (2, 3, 5, 0x63, 0x65, 0xFF):
        add('8260C0 s%X' % st, beat, 0x8260C0, [reel], [(reel + 4, b8(st))])
    for result in (0, 1, -1):
        for v83b in (0, 0xFF):
            add('8260C0 s0 %X 83B %X' % (result & MASK, v83b), beat, 0x8260C0, [reel],
                [(reel + 4, b8(0)), (0x81083B, b8(v83b))], queues={0x1B0FD0: [result]})
    for v83b in (0, 0xFF, 1):
        for use in (0, 4, 0xFB):
            add('8260C0 s4 sub0 83B %X use %X' % (v83b, use), beat, 0x8260C0, [reel],
                [(reel + 4, b8(4)), (reel + 5, b8(0)), (0x81083B, b8(v83b)), (reel + 0xB, b8(use))])
    for result in (0, 1, -1, 2):
        add('8260C0 s4 sub1 %X' % (result & MASK), beat, 0x8260C0, [reel], [(reel + 4, b8(4)), (reel + 5, b8(1))],
            queues={0x1BA1F0: [result]})
    for sub in (2, 0xFF):
        add('8260C0 s4 sub%X' % sub, beat, 0x8260C0, [reel], [(reel + 4, b8(4)), (reel + 5, b8(sub))])
        add('8260C0 s1 sub%X' % sub, beat, 0x8260C0, [reel], [(reel + 4, b8(1)), (reel + 5, b8(sub))])
    add('8260C0 s4 scribble', beat, 0x8260C0, [reel], [(reel + 4, b8(4)), (reel + 5, b8(0)), (reel + 0xB, b8(4))],
        queues={0x1BA1A0: [Scribble(0, [(reel + 5, b8(6))])],
                0x1B1B70: [Scribble(0, [(reel + 0x4C, w32(0x1234560))])]}, run_set=RUN - {0x1BA1A0})
    add('8260C0 s1 sub0', beat, 0x8260C0, [reel], [(reel + 4, b8(1)), (reel + 5, b8(0))])
    for count in (1, 2, 0, 0x8000, 0x8001, 0xFFFF, 0x2BC):
        add('8260C0 s1 count %X' % count, beat, 0x8260C0, [reel],
            [(reel + 4, b8(1)), (reel + 5, b8(1)), (reel + 0x28, h16(count))])
    for limit in (499.0, 497.0, 495.0, 428.0, 426.0, 424.0, 395.0):
        for xb in (fbits(limit), up(limit), down(limit)):
            add('8260C0 s1 x %08X' % xb, beat, 0x8260C0, [reel],
                [(reel + 4, b8(1)), (reel + 5, b8(1)), (reel + 0x28, h16(100)), (reel + 0xB0, bits32(xb)),
                 (reel + 0xB4, f32b(50.0)), (reel + 0xC8, f32b(0.25))])
    for xb in (NAN, NEG_INF, fbits(3.4028235e38), fbits(380.0), fbits(-0.0), fbits(1e-39)):
        add('8260C0 s1 x %08X' % xb, beat, 0x8260C0, [reel],
            [(reel + 4, b8(1)), (reel + 5, b8(1)), (reel + 0x28, h16(100)), (reel + 0xB0, bits32(xb))])
    for y, c8 in ((1e-39, -1e-39), (3.4028235e38, -3.4028235e38), (0.08944256, 0.023182336), (-0.0, 0.0)):
        for x in (498.0, 496.0, 427.0, 425.0):
            add('8260C0 s1 x %g y %g c8 %g' % (x, y, c8), beat, 0x8260C0, [reel],
                [(reel + 4, b8(1)), (reel + 5, b8(1)), (reel + 0x28, h16(100)), (reel + 0xB0, f32b(x)),
                 (reel + 0xB4, f32b(y)), (reel + 0xC8, f32b(c8))])
    add('8260C0 s64', beat, 0x8260C0, [reel], [(reel + 4, b8(0x64))])
    add('8260C0 s64 scribble', beat, 0x8260C0, [reel], [(reel + 4, b8(0x64))],
        queues={0x1B17A0: [Scribble(0, [(reel + 0x4C, w32(0x2345670))])]})

    # ---- 001BE5F0 / 001C1A80 (the AREA22-side owners of a04_05)
    boot = BOOT_BEAT
    bram = CAPTURES[boot][0]
    n1 = owners(bram, 0x1C1A80)[0]
    record = u32(bram, n1 + 0x208)
    nx, ny, nz = struct.unpack_from('<3f', bram, n1 + 0xB0)
    radius, height = struct.unpack_from('<2f', bram, record)
    triple = entry_args(0x1BE5F0, n1)
    for b8d in (0, 1, 0xFF):
        add('1BE5F0 8D %X near' % b8d, boot, 0x1BE5F0, triple,
            [(PLAYER_X, f32b(nx)), (PLAYER_Y, f32b(ny)), (PLAYER_Z, f32b(nz))], [(B8D, b8(b8d))])
    for dx, dy, dz in ((radius, 0.0, 0.0), (0.0, height, 0.0), (0.0, -height, 0.0), (3.0, 1.0, 3.0),
                       (radius + 0.001, 0.0, 0.0), (0.0, height + 0.001, 0.0), (1e-20, 1e-39, -1e-20),
                       (3e19, 0.0, 0.0), (0.0, 0.0, -radius)):
        add('1BE5F0 d %g %g %g' % (dx, dy, dz), boot, 0x1BE5F0, triple,
            [(PLAYER_X, f32b(nx + dx)), (PLAYER_Y, f32b(ny + dy)), (PLAYER_Z, f32b(nz + dz))])
    for sq in (fbits(radius), up(radius), down(radius), NAN, 0x80000000):
        for fab in (fbits(height), up(height), down(height), NAN):
            add('1BE5F0 sqrt %08X fabs %08X' % (sq, fab), boot, 0x1BE5F0, triple, [],
                queues={0x11E748: [sq], 0x11DF78: [fab]}, run_set=RUN - {0x11E748, 0x11DF78})
    add('1BE5F0 record unmapped', boot, 0x1BE5F0, triple, [(n1 + 0x208, w32(0x02400000)), (PLAYER_X, f32b(nx)),
                                                          (PLAYER_Y, f32b(ny)), (PLAYER_Z, f32b(nz))])
    add('1BE5F0 scribble', boot, 0x1BE5F0, triple, [],
        queues={0x11E748: [Scribble(fbits(1.0), [(n1 + 0x208, w32(0x275670)), (PLAYER_Y, f32b(ny)),
                                                  (n1 + 0xB4, f32b(ny))])],
                0x11DF78: [Scribble(fbits(0.5), [(n1 + 0x208, w32(0x275668))])]},
        run_set=RUN - {0x11E748, 0x11DF78})
    for st in (2, 3, 4, 0xFF):
        add('1C1A80 s%X' % st, boot, 0x1C1A80, [n1], [(n1 + 4, b8(st))])
    add('1C1A80 s0', boot, 0x1C1A80, [n1], [(n1 + 4, b8(0))])
    for r1, r2 in ((0, 0), (0x7FFFFFFF, 0x7FFFFFFF), (-0x80000000, -0x80000000), (-1, -1), (0x10000, 0xFFFF),
                   (0x12345678, 0x40000000), (0x7FFF0000, -0x10000)):
        add('1C1A80 s0 rand %X %X' % (r1 & MASK, r2 & MASK), boot, 0x1C1A80, [n1], [(n1 + 4, b8(0))],
            queues={0x122BB8: [r1, r2]}, run_set=RUN - {0x122BB8})
    add('1C1A80 s0 scribble', boot, 0x1C1A80, [n1], [(n1 + 4, b8(0))],
        queues={0x1C62C0: [Scribble(0, [(n1 + 0x90, w32(u32(bram, owners(bram, 0x1C1A80)[1] + 0x90)))])]})
    for result in (0, 1, -1, 2):
        for count in (0, 1, 2, 0x8000, 0xFFFF, 0x7FFF):
            if result != 1 and count != 1:
                continue
            add('1C1A80 s1 %X count %X' % (result & MASK, count), boot, 0x1C1A80, [n1],
                [(n1 + 4, b8(1)), (n1 + 0x28, h16(count))], queues={0x1B17A0: [result]})
    for rnd in (0, 0x7FFFFFFF, -0x80000000, -1, 0x10000):
        add('1C1A80 s1 rand %X' % (rnd & MASK), boot, 0x1C1A80, [n1], [(n1 + 4, b8(1)), (n1 + 0x28, h16(0))],
            queues={0x1B17A0: [1], 0x122BB8: [rnd]}, run_set=RUN - {0x122BB8})
    for impact in (0, 1, 0xFF):
        for b8d in (0, 1):
            add('1C1A80 s1 contact A %X 8D %X' % (impact, b8d), boot, 0x1C1A80, [n1],
                [(n1 + 4, b8(1)), (n1 + 0xA, b8(impact)), (PLAYER_X, f32b(nx + 1.0)), (PLAYER_Y, f32b(ny)),
                 (PLAYER_Z, f32b(nz - 2.0))], [(B8D, b8(b8d))], queues={0x1B17A0: [1]})
    add('1C1A80 s1 contact scribble', boot, 0x1C1A80, [n1],
        [(n1 + 4, b8(1)), (n1 + 0xA, b8(0)), (PLAYER_X, f32b(nx)), (PLAYER_Y, f32b(ny)), (PLAYER_Z, f32b(nz))],
        queues={0x1B17A0: [1], 0x187EC0: [Scribble(0, [(n1 + 0xA, b8(1))])],
                0x183010: [Scribble(0, [(PLAYER_X, f32b(nx + 3.0))])]})
    add('1C1A80 s1 push scribble', boot, 0x1C1A80, [n1],
        [(n1 + 4, b8(1)), (n1 + 0xA, b8(1)), (PLAYER_X, f32b(nx)), (PLAYER_Y, f32b(ny)), (PLAYER_Z, f32b(nz))],
        queues={0x1B17A0: [1], 0x183010: [Scribble(0, [(PLAYER_X, f32b(nx + 3.0)), (PLAYER_Z, f32b(nz - 1.0))])],
                0x1C6380: [Scribble(0, [(n1 + 0x90, w32(u32(bram, owners(bram, 0x1C1A80)[1] + 0x90))),
                                        (n1 + 0x4C, w32(0x1234560))])]})
    add('1C1A80 s1 far', boot, 0x1C1A80, [n1], [(n1 + 4, b8(1)), (n1 + 0xA, b8(1))], queues={0x1B17A0: [1]})
    add('1C1A80 s1 scribble FBD50', boot, 0x1C1A80, [n1], [(n1 + 4, b8(1)), (n1 + 0x28, h16(0))],
        queues={0x1B17A0: [1], 0x1FBD50: [Scribble(0, [(n1 + 0x28, h16(9)), (B8D + 0, b8(0))])]})
    add('1C1A80 s23 scribble', boot, 0x1C1A80, [n1], [(n1 + 4, b8(2))],
        queues={0x1AF890: [Scribble(0, [(n1 + 4, b8(0))])]})


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


def fault_checks():
    """The native fail-stop contract: a NULL hook, a failing hook, an
    unmapped address and a fault latched before the call (on [66] 0x825880,
    state 1 with +0x0B bit 2: 001B17A0, 001BA1A0, 001BA1F0, the +0x4C
    callback, 001F4BF0), then every entry with a latched fault, a NULL hook
    table, a NULL fault pointer and (entries with a result) a NULL result
    pointer."""
    ram, spad = CAPTURES['a04_00_door45_event']
    node = owners(ram, 0x825880)[0]
    problems = []
    calls_want = ['w_001B17A0', 'w_001BA1A0', 'w_001BA1F0', 'w_callback', 'w_001F4BF0']

    def attempt(null=None, failing=None, unmapped=None, latched=False):
        ram_c = (C.c_uint8 * RAM_SIZE).from_buffer_copy(ram)
        spad_c = (C.c_uint8 * SPAD_SIZE).from_buffer_copy(spad)
        ram_c[node + 4] = 1
        ram_c[node + 0xB] = 4
        base, sbase, calls = C.addressof(ram_c), C.addressof(spad_c), []

        def mem(_, address, size):
            if address == unmapped:
                return None
            if 0x70000000 <= address and address + size <= 0x70000000 + SPAD_SIZE:
                return sbase + address - 0x70000000
            return base + address if address + size <= RAM_SIZE else None
        keep = [BYTES_FN(mem)]
        fields = {'ctx': None, 'bytes': keep[0]}
        for name, _, args, result in HOOKS:
            def fn(_ctx, *values, name=name, result=result):
                calls.append(name)
                if result:
                    values[-1][0] = 1
                return -1 if name == failing else 0
            fields[name] = hook_proto(args, result)(fn) if name != null else hook_proto(args, result)()
            keep.append(fields[name])
        fields['w_callback'] = CALLBACK_FN(lambda _c, fn, actor: calls.append('w_callback') or 0)
        keep.append(fields['w_callback'])
        hooks = Hooks(**fields)
        fault = Fault(0x1234, 7) if latched else Fault()
        before = C.string_at(base, RAM_SIZE)
        status = NATIVE.em_area04_port_00825880(C.byref(hooks), node, C.byref(fault))
        return status, fault.address, fault.code, calls, before == C.string_at(base, RAM_SIZE)

    status, address, code, calls, _ = attempt()
    if (status, address, code) != (0, 0, 0) or calls != calls_want:
        problems.append(('fault-check baseline', status, hex(address), code, calls))
    status, address, code, calls, _ = attempt(null='w_001BA1F0')
    if (status, address, code) != (-1, 0x1BA1F0, 1) or calls != calls_want[:2]:
        problems.append(('NULL hook', status, hex(address), code, calls))
    status, address, code, calls, same = attempt(failing='w_001B17A0')
    if (status, address, code) != (-1, 0x1B17A0, 2) or calls != calls_want[:1] or not same:
        problems.append(('failing hook', status, hex(address), code, calls, same))
    status, address, code, calls, _ = attempt(unmapped=node + 0x4C)
    if (status, address, code) != (-1, node + 0x4C, 5) or calls != calls_want[:3]:
        problems.append(('unmapped address', status, hex(address), code, calls))
    status, address, code, calls, _ = attempt(unmapped=node + 4)
    if (status, address, code) != (-1, node + 4, 5) or calls:
        problems.append(('unmapped address before any call', status, hex(address), code, calls))
    status, address, code, calls, same = attempt(latched=True)
    if (status, address, code) != (-1, 0x1234, 7) or calls or not same:
        problems.append(('latched fault', status, hex(address), code, calls, same))
    if NATIVE.em_area04_port_00825880(None, node, C.byref(Fault())) != -1:
        problems.append(('NULL hooks accepted',))
    # every entry: a fault latched on entry, a NULL hook table or a NULL
    # fault pointer returns -1 at once, with no call and no result
    touched = []

    def refuse(*_):
        touched.append('called')
        return -1
    bytes_fn = BYTES_FN(refuse)
    fields = {'ctx': None, 'bytes': bytes_fn}
    keep = [bytes_fn]
    for name, _, args, result in HOOKS:
        fields[name] = hook_proto(args, result)(refuse)
        keep.append(fields[name])
    fields['w_callback'] = CALLBACK_FN(refuse)
    keep.append(fields['w_callback'])
    hooks = Hooks(**fields)
    for entry, (symbol, _, kind) in sorted(FUNCS.items()):
        fn = getattr(NATIVE, symbol)
        for table, fault, with_result in ((C.byref(hooks), Fault(0x1234, 7), True), (None, Fault(), True),
                                          (C.byref(hooks), None, True), (C.byref(hooks), Fault(), False)):
            if not with_result and kind not in RESULT_KINDS:
                continue
            result = C.c_int32(0x5A5A5A5A)
            del touched[:]
            where = C.byref(fault) if fault is not None else None
            call = [table] + entry_args(entry, node)
            if kind in RESULT_KINDS:
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
    sites = []
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
    """The fail-stop contract of the header (em_area04_port.h), on one
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
    where each wrapper's own `a04_failed` test is observable. Returns
    (problems found, native runs made)."""
    index, targets = site
    label, beat, entry, native_args, patches, spad_patches, script, run_set = CASES[index]
    ram0, spad0 = CAPTURES[beat]
    ram, spad = patched(ram0, patches), patched(spad0, spad_patches)
    oracle = Oracle(ram, spad, entry, (native_args, callbacks_of(entry, native_args, ram)), copy_script(script),
                    run_set, set())
    kind = FUNCS[entry][2]
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
        unwritten = kind not in RESULT_KINDS or result == 0x5A5A5A5A
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


QUICK_RANDOM = 0   # random cases sampled into the default run (none; EM_TEST_FULL=1 runs the sweep)
PINNED_RANDOM = ()


def select_cases(elf):
    """The mode's case list: every capture and designed case, the pinned
    random cases and QUICK_RANDOM sampled ones (EM_TEST_FULL=1: all).
    Returns (cases, captured, designed, cases in the full run)."""
    cases, captured, targeted = case_list(elf)
    fixed = captured + targeted
    assert all(0 <= n < len(cases) - fixed for n in PINNED_RANDOM), 'pinned random case out of range'
    pinned = {fixed + n for n in PINNED_RANDOM}
    return reference_mode.select(cases, reference_mode.pick(len(cases), fixed + len(pinned) + QUICK_RANDOM), 0xA04,
                                 keep=lambda i, c: i < fixed or i in pinned), captured, targeted, len(cases)


def main():
    global NATIVE
    elf = read_elf()
    CAPTURES.update(load_captures(elf))
    NATIVE = build(os.environ.get('EM_AREA04_PORT_SOURCE'))
    selected, captured, targeted, total = select_cases(elf)
    CASES[:] = selected
    only = os.environ.get('EM_AREA04_PORT_ONLY')   # debugging: a label prefix
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
    for entry in sorted(FUNCS):
        _, size, _ = FUNCS[entry]
        ram0 = CAPTURES[BOOT_BEAT if entry in BOOT_ENTRIES else OVERLAY_BEATS[0]][0]
        words = reachable_words(ram0, entry, size)
        hit = len(words & seen)
        covered_total += hit
        words_total += len(words)
        print('  %06X  %4d cases  %4d/%4d reachable non-branch words executed'
              % (entry, per_fn.get(entry, 0), hit, len(words)))
    print('  coverage %d/%d reachable words' % (covered_total, words_total))
    if covered_total < words_total and not only:
        failures.append(('coverage', ['%d of %d reachable original words not executed'
                                      % (words_total - covered_total, words_total)]))
    if not only:
        problems = fault_checks()
        print('  native fail-stop checks: %s' % ('ok (NULL hook, failing hook, unmapped address inside an argument, latched fault, NULL hooks; every entry refuses a latched fault, a NULL hook table and a NULL fault pointer)'
                                                 if not problems else problems))
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
          'memory after the last store (all 32 MiB + scratchpad + stack window); the table\'s ctx at every hook, `bytes` and '
          'callback call'
          % (len(selected), 'all 32 MiB + scratchpad + stack window' if reference_mode.FULL else 'dirty set'))


if __name__ == '__main__':
    main()
