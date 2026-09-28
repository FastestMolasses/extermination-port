#!/usr/bin/env python3
"""Execute the ORIGINAL AREA01 revisit owners and compare em_area01_revisit.c.

docs/AREA01_REVISIT.md. The 17 AREA01 overlay functions that only the AREA01
revisit ran (decomp build/s87/census/a02_delta.json, new_functions, region
overlay:AREA01): 0x8237D0, 0x823900, 0x8239C0, 0x824340, 0x824770,
0x824D50, 0x824F70, 0x824FE0, 0x825040, 0x825910, 0x825950, 0x825BE0,
0x825D30, 0x825EA0, 0x825F00, 0x825FC0, 0x8282F0. They call each other
(0x825950 -> 0x825BE0 / 0x825D30 / 0x825EA0 / 0x825F00 / 0x825FC0, 0x824340
-> 0x824F70 / 0x824FE0 / 0x825040): those calls run as original code inside
the oracle and as the translated functions natively, so a case entering
0x825950 compares its callees too. Every other callee (boot functions, the
AREA01 overlay function 0x826010 and the actor's +0x4C callback) is a hook.

The harness is the design of tools/test_area00_overlay_reference.py and
tools/test_area01_overlay_reference.py (Oracle, Replay, Images, poisoning,
the hook contract), reused with the revisit captures, the revisit hook table
and the revisit cases, and extended with a STACK model (below).

Oracle: the shared EE interpreter with the measured float model (FallEE,
tools/test_player_fall_reference.py) runs the original overlay code resident
in the recorded RAM images of the revisit with AREA01 resident (decomp
build/s87/route_a01r/<beat>/eeMemory.bin + scratchpad.bin for a01r_00,
a01r_01, a01r_02, a01r_s0, a01r_s1, and build/s87/route_a00/
a00_10_progression_exit, the revisit's arrival; a01r_03 ends in AREA02).
Before any case the test checks that the overlay text in each image equals
the user's extract/OVERLAY/AREA01.BIN (text size from its header), that the
jump table equals the file and that the boot text below 0x241000 equals the
pinned ELF, so every executed instruction is original. Nothing here embeds
original bytes; reports hold counts only.

Stack locals: 0x8237D0, 0x823900, 0x825BE0 (reached from 0x825950 too) and
0x8282F0 write vectors, a matrix or a quad in their own frame and pass the
addresses to callees. The oracle enters with sp = STACK_TOP (the harness's
private stack); the native entry gets the same sp. When the oracle enters one
of those functions it registers the local range of that frame (LOCALS: the
bytes between sp - size and sp at its entry; the register saves lie below and
are left out). The function's own loads and stores in a registered range are
logged and compared like any other access; the writes callees make on the
stack (helpers running as original code) are replayed. The native `bytes`
serves the same stack region from a private buffer, and the call-entry and
final comparisons include every stack byte either side wrote. Both stacks
start filled with one byte pattern (a different one in the poisoned run), so
a dropped local store shows.

Compared, per case (as in the AREA00 / AREA01 harnesses):
  * at the ENTRY of every call, BEFORE the callee's writes are replayed:
    native RAM, scratchpad and stack against the oracle's memory at the
    entry of the same original call (quick mode: the dirty set;
    EM_TEST_FULL=1: all 32 MiB + scratchpad + the 1 MiB stack at every call
    entry); then the callee and its arguments (64-bit registers where the
    hook takes a 64-bit argument).
  * between calls: the native module's memory accesses are the original's
    own loads and stores one for one, in order, with the same address, size
    and `changed` mark. The only accesses left out are 0x824340's jump-table
    loads, which the C switch encodes (TABLE_BYTES).
  * after the last store: all memory, the return value of the entries that
    have one, and the store-log self-check. An original load or store
    outside the modelled memory (or misaligned) stops the original; the
    native module must stop with fault 5 at the same address after the same
    calls.
  * float hook arguments and results cross as bit patterns.
  * each helper that runs as original code is rehearsed with every argument
    register its hook does not pass poisoned.
  * every case runs twice: as given, and poisoned (poison_patches).
  * the run fails unless every reachable original word was executed.
  * the fail-stop contract of em_area01_revisit.h (hook_contract_site and
    fault_checks), and the table's ctx at every `bytes`, hook and callback
    call.

Cases:
  capture   every owner node of every image as captured (with the frame
            loop's D_00275B40 = actor + 0x110 publication), each function on
            the nodes whose owners call it.
  designed  targeted_cases: every state and step, callee results 0 / 1 /
            other values at every test of a result, scribbling callees,
            story bytes and flags on both sides of every test, the float
            boundaries of every compare.
  perturbed seeded variations (EM_TEST_FULL=1 only).
Coverage: the report counts the original instruction words the oracle
executed inside each function (branch words excluded).

EM_AREA01_RV_SOURCE=<file> tests another copy of the module source (the
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
OVERLAY_FILE = DECOMP / 'extract/OVERLAY/AREA01.BIN'
OUT = ROOT / 'build/area01_revisit'
MASK = 0xFFFFFFFF
RAM_SIZE, SPAD_SIZE = 0x2000000, 0x4000
STACK_BASE, STACK_SIZE = 0x7F000000, 0x100000   # the EE core's private stack region
ARENA = 0x823500
OVERLAY_ID = 2
# The images with AREA01 resident on the revisit: the end of every revisit
# beat that stays in AREA01 and the revisit's arrival (a00_10).
BEAT_DIRS = {b: DECOMP / 'build/s87/route_a01r' / b for b in (
    'a01r_00_to_train_room', 'a01r_01_event', 'a01r_02_bridge', 'a01r_s0_pickup', 'a01r_s1_door14_locked')}
BEAT_DIRS['a00_10_progression_exit'] = DECOMP / 'build/s87/route_a00/a00_10_progression_exit'
BEATS = list(BEAT_DIRS)
POOL, NODE, NODES = 0x7A5640, 0x2F0, 400


class _Route:
    """ROUTE / beat resolves to the beat's folder (two route trees)."""

    def __truediv__(self, beat):
        return BEAT_DIRS[beat]


ROUTE = _Route()

# (entry, native symbol, byte length of the function body, kind)
#   owner     (self)                 owner_sp  (self, sp)
#   pair      (self, block)          self_ret  (self) -> result
#   pair_ret  (self, block) -> result
#   op09      (a0, a1, a2) -> result probe     (self, matrix, sp) -> result
FUNCS = {
    0x8237D0: ('em_area01_revisit_008237D0', 300, 'owner_sp'),
    0x823900: ('em_area01_revisit_00823900', 188, 'owner_sp'),
    0x8239C0: ('em_area01_revisit_008239C0', 140, 'owner'),
    0x824340: ('em_area01_revisit_00824340', 1068, 'pair'),
    0x824770: ('em_area01_revisit_00824770', 1500, 'pair'),
    0x824D50: ('em_area01_revisit_00824D50', 536, 'pair'),
    0x824F70: ('em_area01_revisit_00824F70', 108, 'self_ret'),
    0x824FE0: ('em_area01_revisit_00824FE0', 88, 'owner'),
    0x825040: ('em_area01_revisit_00825040', 228, 'pair_ret'),
    0x825910: ('em_area01_revisit_00825910', 56, 'op09'),
    0x825950: ('em_area01_revisit_00825950', 644, 'owner_sp'),
    0x825BE0: ('em_area01_revisit_00825BE0', 332, 'owner_sp'),
    0x825D30: ('em_area01_revisit_00825D30', 360, 'owner'),
    0x825EA0: ('em_area01_revisit_00825EA0', 92, 'owner'),
    0x825F00: ('em_area01_revisit_00825F00', 192, 'owner'),
    0x825FC0: ('em_area01_revisit_00825FC0', 72, 'owner'),
    0x8282F0: ('em_area01_revisit_008282F0', 1232, 'probe'),
}
RESULT_KINDS = {'self_ret', 'pair_ret', 'op09', 'probe'}
# Each stack-local function's local range: [sp - size, sp) at its entry.
LOCALS = {0x8237D0: 0x60, 0x823900: 0x10, 0x825BE0: 0x40, 0x8282F0: 0x20}


def original_args(entry, native_args):
    """The original's argument registers: sp is a native argument only (the
    original gets it in $sp)."""
    kind = FUNCS[entry][2]
    if kind == 'owner_sp':
        return native_args[:1]
    if kind == 'probe':
        return native_args[:2]
    return native_args


def native_args_of(entry, *args):
    """The native argument list of an entry from its original arguments."""
    kind = FUNCS[entry][2]
    if kind in ('owner_sp', 'probe'):
        return list(args) + [STACK_TOP]
    return list(args)


# Hooks, in the field order of EmArea01RevisitHooks (after ctx and bytes).
# name -> (original address, [(ctype, register)], result kind or None)
U, I, F, Q = 'u', 'i', 'f', 'q'
HOOKS = [
    ('w_001026A0', 0x1026A0, [(U, 4), (U, 5), (U, 6)], None),
    ('w_001026D0', 0x1026D0, [(U, 4), (U, 5), (U, 6)], None),
    ('w_001028B8', 0x1028B8, [(U, 4), (U, 5), (U, 6)], None),
    ('w_001028D0', 0x1028D0, [(U, 4), (U, 5), (U, 6)], None),
    ('w_00102738', 0x102738, [(U, 4), (U, 5)], F),
    ('w_00102760', 0x102760, [(U, 4), (U, 5)], None),
    ('w_00102948', 0x102948, [(U, 4), (U, 5)], None),
    ('w_00102958', 0x102958, [(U, 4), (U, 5)], None),
    ('w_001029C0', 0x1029C0, [(U, 4)], None),
    ('w_00102A60', 0x102A60, [(U, 4), (U, 5), (F, 12)], None),
    ('w_00102B08', 0x102B08, [(U, 4), (U, 5), (F, 12)], None),
    ('w_00102BB0', 0x102BB0, [(U, 4), (U, 5), (F, 12)], None),
    ('w_001031E0', 0x1031E0, [(U, 4), (U, 5)], None),
    ('w_00103230', 0x103230, [(U, 4), (U, 5), (F, 12)], None),
    ('w_0011E2A8', 0x11E2A8, [(F, 12)], F),
    ('w_0011E748', 0x11E748, [(F, 12)], F),
    ('w_00122BB8', 0x122BB8, [], I),
    ('w_001287F0', 0x1287F0, [(U, 4), (U, 5), (I, 6), (F, 12)], None),
    ('w_00128830', 0x128830, [(U, 4), (F, 12), (F, 13), (F, 14)], None),
    ('w_0012D580', 0x12D580, [(U, 4), (U, 5), (I, 6)], None),
    ('w_0012DE90', 0x12DE90, [(U, 4)], None),
    ('w_0019A570', 0x19A570, [(U, 4), (U, 5), (I, 6), (I, 7)], I),
    ('w_0019AA80', 0x19AA80, [(U, 4), (U, 5), (I, 6)], I),
    ('w_0019C6F0', 0x19C6F0, [(I, 4), (I, 5)], I),
    ('w_001AFA90', 0x1AFA90, [(I, 4)], U),
    ('w_001AFC10', 0x1AFC10, [(U, 4)], None),
    ('w_001B10B0', 0x1B10B0, [(U, 4), (I, 5), (I, 6)], I),
    ('w_001B1240', 0x1B1240, [(U, 4), (F, 12), (F, 13)], F),
    ('w_001B12B0', 0x1B12B0, [(F, 12), (F, 13), (F, 14)], F),
    ('w_001B1470', 0x1B1470, [(F, 12)], F),
    ('w_001B17A0', 0x1B17A0, [(U, 4)], I),
    ('w_001B1EA0', 0x1B1EA0, [(I, 4), (U, 5), (U, 6), (I, 7)], I),
    ('w_001B6660', 0x1B6660, [(U, 4)], U),
    ('w_001BA1A0', 0x1BA1A0, [(U, 4), (U, 5)], None),
    ('w_001BA1F0', 0x1BA1F0, [(U, 4)], I),
    ('w_001C2770', 0x1C2770, [(U, 4), (U, 5), (I, 6)], I),
    ('w_001C3D60', 0x1C3D60, [(U, 4), (U, 5)], None),
    ('w_001C4760', 0x1C4760, [(I, 4), (I, 5)], I),
    ('w_001C47A0', 0x1C47A0, [(I, 4), (I, 5)], I),
    ('w_001C63E0', 0x1C63E0, [(U, 4), (I, 5)], None),
    ('w_001C64F0', 0x1C64F0, [(U, 4), (F, 12)], I),
    ('w_001C68C0', 0x1C68C0, [(U, 4)], None),
    ('w_001C69A0', 0x1C69A0, [(U, 4)], None),
    ('w_001CA6F0', 0x1CA6F0, [(U, 4), (I, 5)], None),
    ('w_001CD520', 0x1CD520, [(I, 4), (I, 5), (U, 6), (Q, 7), (U, 8), (F, 12), (F, 13), (F, 14)], I),
    ('w_001E2BA0', 0x1E2BA0, [(U, 4), (U, 5), (U, 6), (F, 12)], None),
    ('w_001EFD20', 0x1EFD20, [(I, 4), (U, 5)], None),
    ('w_001EFD90', 0x1EFD90, [(I, 4), (U, 5), (U, 6)], None),
    ('w_001F4010', 0x1F4010, [(I, 4), (U, 5)], None),
    ('w_001F4E20', 0x1F4E20, [(U, 4), (U, 5), (F, 12)], None),
    ('w_001FAE70', 0x1FAE70, [(I, 4)], None),
    ('w_001FB9F0', 0x1FB9F0, [(I, 4), (I, 5), (I, 6), (I, 7)], None),
    ('w_00826010', 0x826010, [(U, 4)], None),
]
BY_ADDRESS = {address: (name, args, result) for name, address, args, result in HOOKS}

# Callees that run as original code inside the oracle (writes replayed to
# the native side). Pure helpers only: none of them reaches hardware.
RUN = {0x1026A0, 0x1026D0, 0x1028B8, 0x1028D0, 0x102738, 0x102760, 0x102948, 0x102958, 0x1029C0, 0x102A60,
       0x102B08, 0x102BB0, 0x1031E0, 0x103230, 0x11E2A8, 0x11E748, 0x122BB8, 0x1B1240, 0x1B12B0, 0x1B1470,
       0x1BA1A0}

# 0x824340's step table (runtime address), keyed by the runtime address of
# its jump.
JUMP_TABLES = {0x824388: (0x82CBF0, 9)}
# The only bytes the original loads that the translation does not: that
# table, which the C switch encodes (load_captures asserts it equals
# AREA01.BIN in every image). Replay.check_access leaves them out.
TABLE_BYTES = frozenset(a for table, count in JUMP_TABLES.values() for a in range(table, table + 4 * count))



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
    sweep) into a private library under build/area01_revisit."""
    OUT.mkdir(parents=True, exist_ok=True)
    default = 'src/game/em_area01_revisit.c'
    source = source or default
    stem = 'area01_revisit' if source == default else 'area01_revisit_%d' % os.getpid()
    lib = OUT / (stem + ('.dylib' if sys.platform == 'darwin' else '.so'))
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc', '-I' + str(ROOT / 'src/game'), str(source),
                    '-lm', '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    H, W, R, FP = C.POINTER(Hooks), C.c_uint32, C.POINTER(C.c_int32), C.POINTER(Fault)
    shapes = {'owner': [H, W, FP], 'owner_sp': [H, W, W, FP], 'pair': [H, W, W, FP],
              'self_ret': [H, W, R, FP], 'pair_ret': [H, W, W, R, FP], 'op09': [H, W, W, W, R, FP],
              'probe': [H, W, W, W, R, FP]}
    for entry, (symbol, _, kind) in FUNCS.items():
        fn = getattr(native, symbol)
        fn.argtypes = shapes[kind]
        fn.restype = C.c_int
    return native


TEXT_END = None   # set by load_captures from the file header


def load_captures(elf):
    """Every image must hold the AREA01 overlay (header id 2) with its text
    equal to the user's AREA01.BIN, the jump table equal to the file, and the
    boot text equal to the pinned ELF: every instruction the oracle executes
    is original."""
    global TEXT_END
    overlay = OVERLAY_FILE.read_bytes()
    TEXT_END = ARENA + 0x40 + struct.unpack_from('<I', overlay, 12)[0]
    assert all(ARENA < e and e + FUNCS[e][1] <= TEXT_END for e in FUNCS), 'function outside the overlay text'
    captures = {}
    for beat in BEATS:
        folder = ROUTE / beat
        ram = (folder / 'eeMemory.bin').read_bytes()
        spad = (folder / 'scratchpad.bin').read_bytes()
        assert len(ram) == RAM_SIZE and len(spad) == SPAD_SIZE, beat
        assert ram[ARENA:ARENA + 8] == overlay[:8] and ram[ARENA + 4] == OVERLAY_ID, \
            (beat, 'AREA01 overlay not resident')
        assert ram[ARENA + 0x40:TEXT_END] == overlay[0x40:TEXT_END - ARENA], (beat, 'overlay text differs from AREA01.BIN')
        assert ram[0x100000:0x241000] == elf[0x300:0x300 + 0x141000], (beat, 'boot text differs from the ELF')
        for table, count in JUMP_TABLES.values():
            assert ram[table:table + 4 * count] == overlay[table - ARENA:table - ARENA + 4 * count], \
                (beat, 'jump table differs from AREA01.BIN')
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
    run loop are not counted as reads. It also registers the local range of
    every stack-local function it enters at top level (LOCALS): `frames`."""
    seen = None
    top = True
    data = False

    def __init__(self, elf, ram, spad, own=False, fill=0xA5):
        """own=True: run directly on the given bytearrays (Images' oracle
        copy, which run_case puts back afterwards) instead of copying
        32 MiB per run. The private stack starts filled with `fill`."""
        if own:
            FallEE.__init__(self, elf, b'', b'')
            self.mem, self.spad = ram, spad
        else:
            FallEE.__init__(self, elf, ram, spad)
        self.stack[:] = bytes([fill]) * STACK_SIZE
        self.frames = []

    def execute(self, word, pc):
        if self.top:
            if self.seen is not None:
                self.seen.add(pc)
            if self.frames and self.frames[-1][1] <= self.r[29] & MASK:
                # a frame ends when sp is back at (or above) its entry value:
                # the function returned, and a later frame may reuse the bytes
                sp = self.r[29] & MASK
                self.frames = [f for f in self.frames if f[1] > sp]
            size = LOCALS.get(pc)
            if size is not None:
                sp = self.r[29] & MASK
                self.frames.append((sp - size, sp))
        self.data = True
        try:
            return FallEE.execute(self, word, pc)
        finally:
            self.data = False

    def in_frame(self, key, size):
        return any(lo <= key and key + size <= hi for lo, hi in self.frames)


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
    RAM below 0x40000000), the scratchpad address or the private stack
    address; None elsewhere."""
    address &= MASK
    if 0x70000000 <= address < 0x70000000 + SPAD_SIZE:
        return address
    if STACK_BASE <= address < STACK_BASE + STACK_SIZE:
        return address
    if address < 0x40000000:
        return address & (RAM_SIZE - 1)
    return None


def is_stack(key):
    return key >= STACK_BASE


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

    def __init__(self, ram, spad, entry, args, script, run_set, seen, own=False, fill=0xA5):
        ee = self.ee = Coverage(None, ram, spad, own, fill)
        self.fill = fill
        # stack bytes whose last write was one of the function's own register
        # saves (not logged): rebuilt() leaves them out
        self.unlogged = ee.unlogged = set()
        ee.seen = seen
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
        test compares only the stop (docs/AREA01_REVISIT.md section 3). A
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
        EE.write(ee, address, data)
        if key is None:
            return
        if ee.top and is_stack(key) and not ee.in_frame(key, len(data)):
            self.unlogged.update(range(key, key + len(data)))
            return   # a register save of the function's own frame: not memory the module models
        if is_stack(key):
            self.unlogged.difference_update(range(key, key + len(data)))
        self.stores.append((key, data))
        if self.capture is not None:
            self.capture.append((key, data))
        if ee.top:
            self.access[-1].append((key, len(data), old != data, 'store'))
            if is_stack(key):
                return   # stack locals are not poisoned (both stacks start from a fill pattern)
            for i, value in enumerate(data):
                self.first.setdefault(key + i, ('w', value))
                self.written.add(key + i)

    def load(self, address, size=4):
        ee = self.ee
        if ee.data and ee.top and self.undo is None:
            self.stop_if_unmapped(address, size)
            key = key_of(address)
            if key is not None and is_stack(key):
                if ee.in_frame(key, size):
                    self.access[-1].append((key, size, False, 'load'))
            elif key is not None:
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

    def __init__(self, oracle, buffers):
        self.log, self.stores, self.marks = oracle.log, oracle.stores, oracle.marks
        self.oracle_access, self.stopped = oracle.access, oracle.stopped
        self.access = [[]]      # per call interval: every `bytes` request, in order: [address, size, changed]
        self.pending = None     # the last request: (pointer, size, bytes when handed out, its access entry)
        self.i, self.applied, self.errors = 0, 0, []
        self.ram, self.spad, self.expect_ram, self.expect_spad = buffers
        self.ram_base, self.spad_base = C.addressof(self.ram), C.addressof(self.spad)
        self.expect_ram_base = C.addressof(self.expect_ram)
        self.expect_spad_base = C.addressof(self.expect_spad)
        self.stack, self.expect_stack = stack_buffers()
        self.stack_base, self.expect_stack_base = C.addressof(self.stack), C.addressof(self.expect_stack)
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
        elif STACK_BASE <= address and address + size <= STACK_BASE + STACK_SIZE:
            pointer = self.stack_base + address - STACK_BASE
        elif 0x70000000 <= address and address + size <= 0x70000000 + SPAD_SIZE:
            pointer = self.spad_base + address - 0x70000000
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
        """The module uses the bytes of a `bytes` request (rv_at) before
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
        compares all 32 MiB of RAM, the whole scratchpad and the whole
        private stack. Otherwise only the dirty set is compared: every byte
        the oracle has stored so far plus every range the native module has
        been handed through `bytes`. That is sufficient: the module reaches
        memory only through a pointer returned by `bytes` for exactly the
        size it asked for (rv_at in em_area01_revisit_internal.h), and
        replayed callee writes are oracle stores, so every other byte still
        holds the start image (the fill pattern on the stack) on both sides.
        EM_TEST_FULL=1 compares everything at every call entry as well; the
        final comparison is always full."""
        if full is None:
            full = reference_mode.FULL
        for key, data in self.stores[self.applied:upto]:
            if is_stack(key):
                C.memmove(self.expect_stack_base + key - STACK_BASE, data, len(data))
            elif key >= 0x70000000:
                C.memmove(self.expect_spad_base + key - 0x70000000, data, len(data))
            else:
                C.memmove(self.expect_ram_base + key, data, len(data))
            if len(data) > self.dirty.get(key, 0):
                self.dirty[key] = len(data)
        self.applied = upto
        bad_ram = bad_spad = bad_stack = None
        if full:
            if LIBC.memcmp(self.expect_ram_base, self.ram_base, RAM_SIZE):
                bad_ram = first_difference_at(self.expect_ram_base, self.ram_base, RAM_SIZE)
            if LIBC.memcmp(self.expect_spad_base, self.spad_base, SPAD_SIZE):
                bad_spad = first_difference_at(self.expect_spad_base, self.spad_base, SPAD_SIZE)
            if LIBC.memcmp(self.expect_stack_base, self.stack_base, STACK_SIZE):
                bad_stack = first_difference_at(self.expect_stack_base, self.stack_base, STACK_SIZE)
        else:
            for key, size in self.dirty.items():
                if is_stack(key):
                    at = key - STACK_BASE
                    if LIBC.memcmp(self.expect_stack_base + at, self.stack_base + at, size):
                        at += first_difference_at(self.expect_stack_base + at, self.stack_base + at, size)
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
            self.errors.append(('stack differs at %s' % where, hex(STACK_BASE + bad_stack),
                                'original %02X native %02X' % (self.expect_stack[bad_stack], self.stack[bad_stack])))
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
            if is_stack(address):
                C.memmove(self.stack_base + address - STACK_BASE, data, len(data))
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
STACKS = []   # the native and the expected private stack, allocated once per worker


def stack_buffers():
    if not STACKS:
        STACKS.extend([(C.c_uint8 * STACK_SIZE)(), (C.c_uint8 * STACK_SIZE)()])
    return STACKS


def reset_stacks(fill):
    """Both stacks back to the fill pattern (every native run starts so)."""
    for buffer in stack_buffers():
        C.memset(C.addressof(buffer), fill, STACK_SIZE)


CTX = 0xA00C7C00   # the hook table's ctx in every Replay: opaque, never dereferenced by the module


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
            if not is_stack(key):   # the stacks are reset for every run
                self._mark(self.stale, key, len(data))
        for key, size in replay.dirty.items():
            if not is_stack(key):
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


def native_call(native, ram, spad, entry, args, oracle, null=None, inject=None, refuse_at=None, buffers=None):
    """One native run over the start image `ram` / `spad` against `oracle`
    (Replay). `null` leaves one hook-table field NULL, `inject` scripts
    hook statuses and `refuse_at` makes `bytes` refuse that request
    (hook_contract_site); `buffers` are already loaded with the start
    image. Both private stacks start from the oracle's fill pattern.
    Returns (replay, status, fault, result)."""
    reset_stacks(oracle.fill)
    replay = Replay(oracle, buffers or load_buffers(ram, spad))
    replay.inject = inject or {}
    replay.refuse_at = refuse_at
    hooks = replay.hooks(null)
    fault = Fault()
    status, result = call_entry(native, entry, C.byref(hooks), args, C.byref(fault))
    return replay, status, fault, result


def call_entry(native, entry, hooks, args, fault, result_ptr=True):
    """Call the native entry with its argument shape; returns (status, the
    value left in the result word, 0x5A5A5A5A when unwritten)."""
    symbol, _, kind = FUNCS[entry]
    fn = getattr(native, symbol)
    result = C.c_int32(0x5A5A5A5A)
    where = C.byref(result) if result_ptr else None
    if kind in RESULT_KINDS:
        status = fn(hooks, *args, where, fault)
    else:
        status = fn(hooks, *args, fault)
    return status, result.value


def run_native(native, ram, spad, entry, args, oracle, buffers=None, replays=None):
    """`ram` / `spad` are the start image (bytearrays); `buffers`, if
    given, already hold it (Images). Returns the errors; the Replay is
    appended to `replays` if that is given."""
    replay, status, fault, result = native_call(native, ram, spad, entry, args, oracle, buffers=buffers)
    if replays is not None:
        replays.append(replay)
    errors = finish(replay, oracle, FUNCS[entry][2], status, result, fault)
    if not replay.broken:
        # harness self-check: the store log rebuilds the oracle's final memory
        ee = oracle.ee
        if not rebuilt(replay, ee):
            errors.append(('oracle store log does not rebuild the final memory',))
    return errors


def stack_rebuilt(replay, ee, start, stop):
    """The logged stack bytes of [start, stop) (the frames' locals and the
    helpers' writes) equal the oracle's stack, except bytes a later
    function's register saves (not logged) overwrote."""
    at, size = start - STACK_BASE, stop - start
    real, expected = bytes(ee.stack[at:at + size]), C.string_at(replay.expect_stack_base + at, size)
    return all(real[i] == expected[i] or start + i in ee.unlogged for i in range(size))


def rebuilt(replay, ee):
    """The expected image (start image + the oracle's store log) equals the
    oracle's final memory: over all 32 MiB + scratchpad, or, when the
    oracle ran on WatchedMemory, over every byte any code path wrote there
    plus every byte the log wrote (the rest is the start image on both
    sides, so the answer is the same)."""
    if not isinstance(ee.mem, WatchedMemory):
        for key, data in replay.stores:
            if is_stack(key) and not stack_rebuilt(replay, ee, key, key + len(data)):
                return False
        return not (LIBC.memcmp(replay.expect_ram_base, buffer_address(ee.mem), RAM_SIZE)
                    or LIBC.memcmp(replay.expect_spad_base, buffer_address(ee.spad), SPAD_SIZE))
    if len(ee.mem) != RAM_SIZE or len(ee.spad) != SPAD_SIZE:
        return False
    spans = [(key, key + len(data)) for key, data in replay.stores]
    spans += list(ee.mem.touched) + [(0x70000000 + a, 0x70000000 + b) for a, b in ee.spad.touched]
    for start, stop in spans:
        if is_stack(start):
            if not stack_rebuilt(replay, ee, start, stop):
                return False
        elif start >= 0x70000000:
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
    cost = lambda i: 3 if selected[i][2] in (0x824770, 0x825950) else 1   # noqa: E731
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
    """The poisoned start image, as patches. (1) Every byte whose first
    access by the function is a store is preset to a value different from
    the first value stored there, so a store that is moved across a call,
    dropped or aimed at the wrong byte leaves memory that the call-entry or
    final comparison sees. (2) Every byte the function never touches that
    shares an aligned 8-byte group with a byte it stores is flipped too, so
    a store of the wrong width shows. Runs of bytes are merged."""
    first = oracle.first
    values = {k: first[k][1] ^ 0x5A for k, access in first.items() if access[0] == 'w'}
    for key in oracle.written:
        for k in range(key & ~7, (key & ~7) + 8):
            if k not in first and k not in values:
                if k >= 0x70000000:
                    if k - 0x70000000 < SPAD_SIZE:
                        values[k] = spad[k - 0x70000000] ^ 0xA5
                elif k < RAM_SIZE:
                    values[k] = ram[k] ^ 0xA5
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
    return patches


def run_one(entry, native_args, ram, spad, script, run_set, seen, images=None, fill=0xA5):
    """Oracle then native on the same start image; returns (errors, oracle).
    With `images` (Images), ram / spad are its start image and the oracle
    and native sides run on its prepared copies. `fill` is the stacks'
    starting pattern."""
    self_node = native_args[0]
    callbacks = {u32(ram, self_node + 0x4C)} | SCRIBBLED_CALLBACKS
    try:   # the oracle works on its own copy; ram / spad stay the start image
        if images is None:
            oracle = Oracle(ram, spad, entry, (native_args, callbacks), script, run_set, seen, fill=fill)
        else:
            oracle = Oracle(images.oracle_ram, images.oracle_spad, entry, (native_args, callbacks), script, run_set,
                            seen, own=True, fill=fill)
    except AssertionError as error:
        if images is not None:
            images.ran(None, None, False)
        return [('oracle failed', repr(error))], None
    replays = []
    errors = list(oracle.problems) + run_native(NATIVE, ram, spad, entry, native_args, oracle,
                                                buffers=BUFFERS if images is not None else None, replays=replays)
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
    counts = [1, 0, 0]
    calls = None   # the as-given run's callee names, for hook_sites (None: not usable there)
    if oracle is not None:
        counts[1] += len(oracle.marks)
        counts[2] += oracle.rehearsals
        if oracle.stopped is None:
            calls = tuple(e[0] for e in oracle.log)
    if errors or oracle is None:
        return label, entry, errors, seen, counts, None
    poison = poison_patches(oracle, ram, spad)
    if poison:
        ram_p, spad_p = IMAGES.more(poison)
        more, second = run_one(entry, native_args, ram_p, spad_p, script_copy, run_set, seen, IMAGES, fill=0x3C)
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
EVENT_CALLEES = (0x825BE0, 0x825D30, 0x825EA0, 0x825F00, 0x825FC0)


def world_patch(beat, node):
    """The frame loop publishes the current actor's bone table before its
    behaviour runs: D_00275B40 = actor + 0x110 (the pool loop 001AFD70 calls
    001CB590 before each behaviour; docs/AREA01_OVERLAY.md "Facts this lane
    measured"). 0x824D50 and 0x8282F0 read it here; their callers are the
    behaviours of the node they run on (0x823CD0, 0x826D40)."""
    return [(0x275B40, w32(node + 0x110))]


def record_naming(ram, function):
    """The overlay data word holding `function` (a script record's +0x04),
    as the record's address; None when not found."""
    for at in range(TEXT_END, ARENA + 0xA000, 4):
        if u32(ram, at) == function:
            return at - 4
    return None


def fb(value):
    return fbits(value)


def probe_matrix(ram, node):
    """0x826D40 passes record B's matrix: B = (D_00275B40) +0xC with
    D_00275B40 = node + 0x110, so B is the word at node + 0x11C."""
    return u32(ram, node + 0x11C) + 0x90


def case_list(elf):
    rng = random.Random(0xA01E)
    cases = []
    count = reference_mode.pick(40, 0)

    def add(label, beat, entry, args, patches=(), spad=(), queues=None, default=None, run_set=RUN):
        cases.append((label, beat, entry, tuple(args), list(patches), list(spad),
                      Script(queues, default), frozenset(run_set)))

    for beat in BEATS:
        ram, spad = CAPTURES[beat]
        for node in owners(ram, 0x825950):
            add('capture %s 825950 @%X' % (beat, node), beat, 0x825950, native_args_of(0x825950, node),
                world_patch(beat, node))
            for entry in EVENT_CALLEES:
                add('capture %s %06X @%X' % (beat, entry, node), beat, entry, native_args_of(entry, node),
                    world_patch(beat, node))
            record = record_naming(ram, 0x825910)
            add('capture %s 825910 @%X' % (beat, node), beat, 0x825910, [node, node + 0x1F0, record])
        for entry in (0x8237D0, 0x823900, 0x8239C0):
            for node in owners(ram, entry):
                add('capture %s %06X @%X' % (beat, entry, node), beat, entry, native_args_of(entry, node),
                    world_patch(beat, node))
        for node in owners(ram, 0x823CD0):
            w = world_patch(beat, node)
            for entry in (0x824340, 0x824770, 0x824D50, 0x825040):
                add('capture %s %06X @%X' % (beat, entry, node), beat, entry, [node, node + 0x1F0], w)
            for entry in (0x824F70, 0x824FE0):
                add('capture %s %06X @%X' % (beat, entry, node), beat, entry, [node], w)
        for node in owners(ram, 0x826D40):
            add('capture %s 8282F0 @%X' % (beat, node), beat, 0x8282F0,
                native_args_of(0x8282F0, node, probe_matrix(ram, node)), world_patch(beat, node))
    captured = len(cases)
    targeted_cases(add)
    targeted = len(cases) - captured

    # ---------------------------------------------------------------- perturbed
    for i in range(count):
        beat = rng.choice(['a01r_00_to_train_room', 'a00_10_progression_exit'])
        ram, _ = CAPTURES[beat]
        for node in owners(ram, 0x825950):
            p = world_patch(beat, node) + [
                (node + 4, b8(rng.choice([0, 1, 1, 1, 2, 3, 4]))), (node + 5, b8(rng.choice([0, 1, 2, 3]))),
                (node + 0xD, b8(rng.choice([0x47, 0x4B, 0x47, 0x4B, 0x46]))),
                (0x8107DF, b8(rng.choice([0, 1, 2, 0x10, 0x40, 0x80, 0xFF, 3]))),
                (0x810845, b8(rng.choice([0, 0x20, 0xDF]))), (node + 0x28, h16(rng.choice([0, 0x26B, 0x26C, 5])))]
            q = {a: [rng.choice([0, 1, 2, -1]) for _ in range(3)] for a in (0x1BA1F0, 0x1B1EA0, 0x1C64F0)}
            add('perturbed 825950 %d @%X' % (i, node), beat, 0x825950, native_args_of(0x825950, node), p, queues=q)
        for node in owners(ram, 0x823CD0):
            ev = node + 0x1F0
            p = world_patch(beat, node) + [
                (node + 6, b8(rng.choice(range(10)))), (node + 7, b8(rng.choice([0, 1, 2]))),
                (ev + 0xE2, b8(rng.randrange(5))), (ev + 0xF4, h16(rng.choice([0, 0x1000, 0x4000, 0x5000]))),
                (ev + 0xE4, w32(rng.choice([0, 1, 0x100, 0x400]))), (ev + 0xD4, f32b(rng.uniform(-0.5, 2.5))),
                (ev + 0xF0, f32b(rng.uniform(-0.1, 0.9))), (ev + 0xD0, h16(rng.choice([0, 1, 2, 0x859, 0x870]))),
                (ev + 0xD2, h16(rng.choice([0, 1, 2, 0x78])))]
            q = {0x1C2770: [rng.choice([0, 1, -1])]}
            for entry in (0x824340, 0x824770, 0x824D50, 0x825040):
                add('perturbed %06X %d @%X' % (entry, i, node), beat, entry, [node, ev], p, queues=q)
        for node in owners(ram, 0x826D40):
            rec = owners(ram, 0x823CD0)[0]
            p = world_patch(beat, node) + [
                (node + 4, b8(rng.choice([4, 1]))), (node + 0x200, w32(rng.choice([1, 13, 12]))),
                (node + 0x204, w32(rng.choice([0, rec]))), (rec + 3, b8(rng.choice([0x0F, 0x10, 0x13, 0x14])))]
            hit = Scribble(rng.choice([0, 1]), [(0x700031D8, w32(rng.choice([0, 1]))), (0x700031D4, w32(rec)),
                                                (0x700031B0, f32b(rng.uniform(-200, 200)))])
            add('perturbed 8282F0 %d @%X' % (i, node), beat, 0x8282F0,
                native_args_of(0x8282F0, node, probe_matrix(ram, node)), p,
                queues={0x19AA80: [rng.choice([0, 1])], 0x19A570: [hit]})
    return cases, captured, targeted


F_EDGES = [0.0, -0.0, 1e-39, -1e-39, 3.4028235e38, -3.4028235e38]
BIT_EDGES = [0x7F800000, 0xFF800000, 0x7FC00000, 0x7F7FFFFF, 0x00000001, 0x80000001]


def targeted_cases(add):
    """Designed cases for every state, step and test; all run by default."""
    beat = 'a01r_00_to_train_room'
    ram, spad = CAPTURES[beat]
    g5 = [n for n in owners(ram, 0x825950) if ram[n + 0xD] == 0x47][0]
    g7 = [n for n in owners(ram, 0x825950) if ram[n + 0xD] == 0x4B][0]
    cr = owners(ram, 0x823CD0)
    probes = owners(ram, 0x826D40)
    spare = free_node(ram)
    assert u32(ram, 0x8106C0) == g5, 'D_008106C0 is the 0x47 node in the a01r_00 image'
    target = u32(ram, g5 + 0x118)   # the record 0x824F70 / 0x824FE0 / 0x825040 turn toward

    def w(node):
        return world_patch(beat, node)

    def sp_args(entry, node):
        return native_args_of(entry, node)

    # ---- 0x825950 the event owner (g5, +0x0D 0x47) and the second character (g7, 0x4B)
    for node in (g5, g7):
        for state in (0, 1, 2, 3, 4, 0xFF):
            for kind in (0x47, 0x4B, 0x46, 0xCB):
                for flag in ((0, 0x20, 0xDF) if state == 0 else (0,)):
                    for df in ((0, 1, 2, 3, 0x10, 0x40, 0x80, 0xFF, 0x20) if state == 1 else (0,)):
                        add('825950 %X s%d %02X %02X %02X' % (node, state, kind, flag, df), beat, 0x825950,
                            sp_args(0x825950, node),
                            w(node) + [(node + 4, b8(state)), (node + 0xD, b8(kind)), (0x810845, b8(flag)),
                                       (0x8107DF, b8(df))])
    # re-reads: +0x0D after 001B10B0 / inside the dispatched callee, D_008107DF after the callee
    add('825950 s0 scribble kind', beat, 0x825950, sp_args(0x825950, g5), w(g5) + [(g5 + 4, b8(0))],
        queues={0x1B10B0: [Scribble(0, [(g5 + 0xD, b8(0x4B))])]})
    add('825950 s0 scribble 845', beat, 0x825950, sp_args(0x825950, g5), w(g5) + [(g5 + 4, b8(0))],
        queues={0x19C6F0: [Scribble(0, [(g5 + 0xD, b8(0x4B)), (0x810845, b8(0x20))])]})
    for df, stub in ((0x40, 0x1BA1F0), (0x80, 0x1BA1F0), (0x10, 0x826010), (0, 0x826010), (2, 0x1C68C0)):
        for kind, flag in ((0x4B, 0xFF), (0x4B, 0x80), (0x46, 0xFF)):
            add('825950 s1 %02X scribble %02X %02X' % (df, kind, flag), beat, 0x825950, sp_args(0x825950, g5),
                w(g5) + [(g5 + 4, b8(1)), (g5 + 5, b8(1)), (0x8107DF, b8(df))],
                queues={stub: [Scribble(1, [(g5 + 0xD, b8(kind)), (0x8107DF, b8(flag))])]})
    add('825950 s1 80 done then 4B', beat, 0x825950, sp_args(0x825950, g5),
        w(g5) + [(g5 + 4, b8(1)), (g5 + 5, b8(1)), (0x8107DF, b8(0x80))],
        queues={0x1BA1F0: [Scribble(1, [(g5 + 0xD, b8(0x4B))])]})
    add('825950 s2 scribble', beat, 0x825950, sp_args(0x825950, g5), w(g5) + [(g5 + 4, b8(2))],
        queues={0x1AFC10: [Scribble(0, [(g5 + 0xD, b8(0))])]})

    # ---- 0x825BE0 (stage 0/1)
    for node in (g5, g7):
        for step in (0, 1, 2, 3, 0xFF):
            for result in ((0, 1, 2, -1) + WIDE_RESULTS if step in (1, 2) else (0,)):
                add('825BE0 %X step%d %X' % (node, step, result & MASK), beat, 0x825BE0, sp_args(0x825BE0, node),
                    w(node) + [(node + 5, b8(step))], queues={0x1B1EA0: [result], 0x1BA1F0: [result]})
    add('825BE0 step2 scribble', beat, 0x825BE0, sp_args(0x825BE0, g5), w(g5) + [(g5 + 5, b8(2))],
        queues={0x1BA1F0: [Scribble(1, [(g5 + 0x18, w32(cr[2])), (g5 + 5, b8(9))])],
                0x1C68C0: [Scribble(0, [(g5 + 0x4C, w32(0x1234560))])]})
    add('825BE0 step1 scribble', beat, 0x825BE0, sp_args(0x825BE0, g5), w(g5) + [(g5 + 5, b8(1))],
        queues={0x1B1EA0: [Scribble(1, [(g5 + 5, b8(7))])],
                0x826010: [Scribble(0, [(g5 + 0x4C, w32(0x2345670)), (0x82B090, f32b(-3.5))])]})
    add('825BE0 quad changed', beat, 0x825BE0, sp_args(0x825BE0, g5),
        w(g5) + [(g5 + 5, b8(1)), (0x82B094, f32b(12.25)), (0x82B0C8, w32(0x7F800000))])

    # ---- 0x825D30 (stage 2/0x10)
    for step in (0, 1, 2, 0xFF):
        for df in ((0x10, 2, 0x11, 0, 0x90) if step == 0 else (2,)):
            for result in ((0, 1, -1) + WIDE_RESULTS if step == 1 else (0,)):
                add('825D30 step%d %02X %X' % (step, df, result & MASK), beat, 0x825D30, [g5],
                    w(g5) + [(g5 + 5, b8(step)), (0x8107DF, b8(df))], queues={0x1BA1F0: [result]})
    for counter in (0, 0x26A, 0x26B, 0x26C, 0x7FFF, 0xFFFF, 0x8000, 0x26D):
        for anim in (0, 0x12345, -1, 0x1000, 0x7FFF8000):
            add('825D30 step1 counter %X %X' % (counter, anim & MASK), beat, 0x825D30, [g5],
                w(g5) + [(g5 + 5, b8(1)), (g5 + 0x28, h16(counter))], queues={0x1C64F0: [anim]})
    add('825D30 step0 scribble', beat, 0x825D30, [g5], w(g5) + [(g5 + 5, b8(0)), (0x8107DF, b8(2))],
        queues={0x1C64F0: [Scribble(0, [(0x8107DF, b8(0x10))])],
                0x1CA6F0: [Scribble(0, [(g5 + 5, b8(0x20))])]})
    add('825D30 step0 scribble 18', beat, 0x825D30, [g5], w(g5) + [(g5 + 5, b8(0)), (0x8107DF, b8(0x10))],
        queues={0x826010: [Scribble(0, [(g5 + 0x18, w32(cr[3])), (0x8107DF, b8(0x10))])],
                0x1C68C0: [Scribble(0, [(g5 + 0x4C, w32(0x1234560))])]})
    add('825D30 step1 scribble', beat, 0x825D30, [g5], w(g5) + [(g5 + 5, b8(1)), (g5 + 0x28, h16(0x26B))],
        queues={0x1BA1F0: [Scribble(1, [(g5 + 0x1C, w32(cr[4])), (g5 + 5, b8(1))])],
                0x1C64F0: [Scribble(0x55, [(g5 + 0x28, h16(0x100))])]})

    # ---- 0x825EA0 (stage 0x40), 0x825F00 (stage 0x80), 0x825FC0 (0x4B at 0xFF)
    for result in (0, 1, 2, -1) + WIDE_RESULTS:
        add('825EA0 %X' % (result & MASK), beat, 0x825EA0, [g5], w(g5), queues={0x1BA1F0: [result]})
    add('825EA0 scribble', beat, 0x825EA0, [g5], w(g5),
        queues={0x1BA1F0: [Scribble(1, [(g5 + 0x4C, w32(0x1234560))])],
                0x1C68C0: [Scribble(0, [(g5 + 0x4C, w32(0x2345670))])]})
    for step in (0, 1, 2, 0xFF):
        for result in ((0, 1, -1) + WIDE_RESULTS if step == 1 else (0,)):
            add('825F00 step%d %X' % (step, result & MASK), beat, 0x825F00, [g5], w(g5) + [(g5 + 5, b8(step))],
                queues={0x1BA1F0: [result], 0x1B6660: [spare, 0]})
    add('825F00 step1 scribble', beat, 0x825F00, [g5], w(g5) + [(g5 + 5, b8(1))],
        queues={0x1BA1F0: [1], 0x1C47A0: [Scribble(3, [(g5 + 4, b8(9))])]})
    for node in (g5, g7):
        add('825FC0 %X' % node, beat, 0x825FC0, [node], w(node),
            queues={0x1B17A0: [Scribble(1, [(node + 0x4C, w32(0x1234560))])]})

    # ---- 0x825910 op09 (a0 +0x3C against 505.0)
    record = record_naming(ram, 0x825910)
    for value in [505.0, 504.99997, 505.00003, 0.0, -0.0, 1e-39, -505.0, 124.0, 3.4028235e38, -3.4028235e38]:
        add('825910 %r' % value, beat, 0x825910, [g5, g5 + 0x1F0, record], [(g5 + 0x3C, f32b(value))])
    for pattern in BIT_EDGES + [0x43FC8000, 0x43FC8001, 0x43FC7FFF]:
        add('825910 bits %08X' % pattern, beat, 0x825910, [g5, g5 + 0x1F0, record], [(g5 + 0x3C, w32(pattern))])

    # ---- 0x8237D0 / 0x823900 / 0x8239C0 (the group 0x82A900 behaviours; run on real nodes)
    for node in (cr[0], cr[3], g7):
        for state in (0, 1, 2, 3, 4, 0xFF):
            for entry in (0x8237D0, 0x823900, 0x8239C0):
                add('%06X %X s%d' % (entry, node, state), beat, entry, sp_args(entry, node),
                    w(node) + [(node + 4, b8(state))], queues={0x1AFA90: [spare]})
    for counter in (9, 19, -11, -1, 0, 0x7FFF, 10, -10, 29, 99, 0x7FF9, -0x7FFF, 0x7FFE):
        for spawned in (spare, 0, cr[1]):
            add('8237D0 s1 %d %X' % (counter, spawned), beat, 0x8237D0, sp_args(0x8237D0, cr[0]),
                w(cr[0]) + [(cr[0] + 4, b8(1)), (cr[0] + 0x28, h16(counter))], queues={0x1AFA90: [spawned]})
    for spawned in (0x100, 0x10000, 0x1000000):
        add('8237D0 s1 spawned %X' % spawned, beat, 0x8237D0, sp_args(0x8237D0, cr[0]),
            w(cr[0]) + [(cr[0] + 4, b8(1)), (cr[0] + 0x28, h16(9))], queues={0x1AFA90: [spawned]})
    add('8237D0 s1 data changed', beat, 0x8237D0, sp_args(0x8237D0, cr[0]),
        w(cr[0]) + [(cr[0] + 4, b8(1)), (cr[0] + 0x28, h16(9)), (0x829DA4, f32b(-7.5)), (0x829DBC, w32(0x7F800001))],
        queues={0x1AFA90: [spare]})
    for node in cr[:2]:
        for x, y, z in ((0.0, 0.0, 0.0), (1e-39, -0.0, 3.5), (-12.5, 40.0, -560.0)):
            add('823900 s1 %X %g' % (node, z), beat, 0x823900, sp_args(0x823900, node),
                w(node) + [(node + 4, b8(1)), (node + 0x100, f32b(x)), (node + 0x104, f32b(y)), (node + 0x108, f32b(z))])

    # ---- 0x824340 (0x823CD0 steps 2..5)
    node = cr[0]
    ev = node + 0x1F0
    tx, tz = struct.unpack_from('<ff', ram, target + 0xC0)[0], struct.unpack_from('<f', ram, target + 0xC8)[0]

    def near(dx, dz):
        """self +0xB0/+0xB8 at (dx, dz) from the target record's +0xC0/+0xC8."""
        return [(node + 0xB0, f32b(tx + dx)), (node + 0xB8, f32b(tz + dz))]
    for step in list(range(10)) + [0xFF]:
        for held in (0, 1, -1) + WIDE_RESULTS:
            add('824340 step%d held %X' % (step, held & MASK), beat, 0x824340, [node, ev],
                w(node) + [(node + 6, b8(step)), (ev + 0xF4, h16(0x5000)), (ev + 0xE4, w32(0x100))],
                queues={0x1C2770: [held]})
    add('824340 scribble step', beat, 0x824340, [node, ev], w(node) + [(node + 6, b8(0))],
        queues={0x1C2770: [Scribble(0, [(node + 6, b8(4)), (ev + 0xF4, h16(0x4000))])]})
    for flags in (0, 0x1000, 0xEFFF, 0x1001, 0x4000, 0x5000, 0xAFFF, 0x8000):
        for step in (3, 4, 8):
            add('824340 step%d F4 %X' % (step, flags), beat, 0x824340, [node, ev],
                w(node) + [(node + 6, b8(step)), (ev + 0xF4, h16(flags)), (node + 7, b8(1))])
    for sub in (0, 1, 2, 0xFF):
        add('824340 step3 825040 %d' % sub, beat, 0x824340, [node, ev],
            w(node) + [(node + 6, b8(3)), (ev + 0xF4, h16(0)), (node + 7, b8(sub))])
    for e4 in (0, 1, 0xF0, 0x100, 0x8, 0x101, 0x10F, 0x110, 0x200, 0x400):
        for step in (5, 6, 7):
            add('824340 step%d E4 %X' % (step, e4), beat, 0x824340, [node, ev],
                w(node) + [(node + 6, b8(step)), (ev + 0xE4, w32(e4)), (ev + 0xF0, f32b(0.5))] + near(50.0, 0.0))
    for f0 in (0.8, 0.04, 0.03, 0.0400001, 0.039999999, -0.5, 0.0, -0.0, 1e-39, 3.4028235e38):
        for dx in (0.0, 50.0):
            add('824340 step5 F0 %r %g' % (f0, dx), beat, 0x824340, [node, ev],
                w(node) + [(node + 6, b8(5)), (ev + 0xE4, w32(0)), (ev + 0xF0, f32b(f0))] + near(dx, 0.0))
    for pattern in BIT_EDGES:
        add('824340 step5 F0 bits %08X' % pattern, beat, 0x824340, [node, ev],
            w(node) + [(node + 6, b8(5)), (ev + 0xE4, w32(0)), (ev + 0xF0, w32(pattern))] + near(50.0, 0.0))
    # the operand order of +0xC0 = pi * (56.25 * -F0) shows at these F0 values
    # of the 0.04 decay (found in the review sweep); 0.1 * wave lands unrounded
    # in +0xB4 when +0xB4 is 0
    for pattern in (0x3F428F5C, 0x3F3851EB, 0x3F2E147A, 0x3F199998):
        add('824340 step5 F0 order %08X' % pattern, beat, 0x824340, [node, ev],
            w(node) + [(node + 6, b8(5)), (ev + 0xE4, w32(0)), (ev + 0xF0, w32(pattern))] + near(50.0, 0.0))
    for wave in (1.0, -0.75, 0.3):
        add('824340 step6 wave %r at B4 0' % wave, beat, 0x824340, [node, ev],
            w(node) + [(node + 6, b8(6)), (ev + 0xE4, w32(0)), (ev + 0xD0, h16(0x100)), (node + 0xB4, w32(0))]
            + near(50.0, 0.0), queues={0x11E2A8: [fb(wave)]}, run_set=RUN - {0x11E2A8})
    for dx, dz in ((8.0, 0.0), (0.0, 8.0), (8.000001, 0.0), (7.9999995, 0.0), (5.656854, 5.656854), (-8.0, 0.0),
                   (0.0, -8.0000005), (1e-39, 0.0)):
        for step in (5, 6):
            add('824340 step%d near %g %g' % (step, dx, dz), beat, 0x824340, [node, ev],
                w(node) + [(node + 6, b8(step)), (ev + 0xE4, w32(0)), (ev + 0xF0, f32b(0.5)),
                           (ev + 0xD0, h16(0x100))] + near(dx, dz))
    for d0 in (0, 0x858, 0x859, 0x870, 0x871, -0x18, 0x7FF0, -0x8000, 0x2D0, 0x5A0):
        add('824340 step6 D0 %X' % (d0 & 0xFFFF), beat, 0x824340, [node, ev],
            w(node) + [(node + 6, b8(6)), (ev + 0xE4, w32(0)), (ev + 0xD0, h16(d0))] + near(50.0, 0.0))
    for c4 in (0.0, 3.1415927, -3.1415927, 1.0, -0.0, 1e-39):
        add('824340 step8 C4 %r' % c4, beat, 0x824340, [node, ev],
            w(node) + [(node + 6, b8(8)), (ev + 0xF4, h16(0x1000)), (node + 0xC4, f32b(c4))])
    add('824340 step6 scribble', beat, 0x824340, [node, ev],
        w(node) + [(node + 6, b8(6)), (ev + 0xE4, w32(0)), (ev + 0xD0, h16(0x100))] + near(50.0, 0.0),
        queues={0x11E2A8: [Scribble(fb(0.5), [(ev + 0xD0, h16(0x858)), (node + 0xB4, f32b(2.0))])],
                0x11E748: [Scribble(fb(100.0), [(ev + 0xD0, h16(0x871))])]},
        run_set=RUN - {0x11E2A8, 0x11E748})
    add('824340 step5 scribble', beat, 0x824340, [node, ev],
        w(node) + [(node + 6, b8(5)), (ev + 0xE4, w32(0)), (ev + 0xF0, f32b(0.5))] + near(50.0, 0.0),
        queues={0x1B12B0: [Scribble(fb(0.25), [(ev + 0xE4, w32(0)), (ev + 0xF0, f32b(0.02)), (0x8106C0, w32(g7))])],
                0x11E748: [Scribble(fb(1.0), [(node + 6, b8(0x33))])]},
        run_set=RUN - {0x1B12B0, 0x11E748})
    add('824340 step3 scribble', beat, 0x824340, [node, ev],
        w(node) + [(node + 6, b8(3)), (ev + 0xF4, h16(0)), (node + 7, b8(1)), (ev + 0xD2, h16(5))],
        queues={0x1B12B0: [Scribble(fb(0.75), [(ev + 0xE8, f32b(0.75)), (node + 0xC4, f32b(0.1))])]},
        run_set=RUN - {0x1B12B0})

    # ---- 0x825040 (the turn helper)
    for sub in (0, 1, 2, 0xFF):
        for d2 in ((1, 0, 2, 0x8000, 0xFFFF, 0x78) if sub == 1 else (0x78,)):
            add('825040 +7 %d D2 %X' % (sub, d2), beat, 0x825040, [node, ev],
                w(node) + [(node + 7, b8(sub)), (ev + 0xD2, h16(d2)), (ev + 0xE8, f32b(1.5)), (node + 0xC4, f32b(-2.0))])
    for c4, e8 in ((1.0, 1.0), (0.0, -0.0), (1e-39, 0.0), (0.5, 0.52), (-3.0, 3.0), (100.0, 100.0)):
        add('825040 equal %r %r' % (c4, e8), beat, 0x825040, [node, ev],
            w(node) + [(node + 7, b8(1)), (ev + 0xD2, h16(5)), (ev + 0xE8, f32b(e8)), (node + 0xC4, f32b(c4))])
    add('825040 scribble', beat, 0x825040, [node, ev],
        w(node) + [(node + 7, b8(1)), (ev + 0xD2, h16(5)), (ev + 0xE8, f32b(1.0)), (node + 0xC4, f32b(0.0))],
        queues={0x1B12B0: [Scribble(fb(0.2), [(ev + 0xE8, f32b(0.2)), (ev + 0xD2, h16(1))])]},
        run_set=RUN - {0x1B12B0})
    add('825040 +7 0 scribble', beat, 0x825040, [node, ev], w(node) + [(node + 7, b8(0))],
        queues={0x1B1240: [Scribble(fb(-1.25), [(node + 7, b8(0x41))])]}, run_set=RUN - {0x1B1240})

    # ---- 0x824F70 / 0x824FE0
    for dx, dz in ((8.0, 0.0), (0.0, 8.0), (8.000001, 0.0), (7.9999995, 0.0), (5.656854, 5.656854), (0.0, 0.0),
                   (-0.0, 1e-39), (1e19, 0.0), (3.0e38, 3.0e38), (-8.0, -0.0)):
        add('824F70 %g %g' % (dx, dz), beat, 0x824F70, [node], w(node) + near(dx, dz))
        add('824FE0 %g %g' % (dx, dz), beat, 0x824FE0, [node], w(node) + near(dx, dz))
    add('824F70 target changed', beat, 0x824F70, [node], w(node) + [(0x8106C0, w32(g7))])
    add('824FE0 scribble', beat, 0x824FE0, [node], w(node),
        queues={0x1B1240: [Scribble(fb(0.3), [(node + 0xC4, f32b(-1.0))])]}, run_set=RUN - {0x1B1240})

    # ---- 0x824770 (0x823CD0 step 6): every spot index, every step
    for n in cr:
        e = n + 0x1F0
        for step in (0, 1, 2, 3, 0xFF):
            add('824770 %X step%d' % (n, step), beat, 0x824770, [n, e],
                w(n) + [(n + 6, b8(step)), (e + 0xD4, f32b(0.9)), (e + 0xD0, h16(3))])
    for index in range(5):
        for d4 in (0.22, 0.22000001, 0.21999999, 0.2, 0.19, 1.0, -1.0, 0.0):
            add('824770 step1 E2 %d D4 %r' % (index, d4), beat, 0x824770, [node, ev],
                w(node) + [(node + 6, b8(1)), (ev + 0xE2, b8(index)), (ev + 0xD4, f32b(d4))])
    for d0 in (1, 2, 0, 0x8000, 0xFFFF, 0x1C2):
        add('824770 step2 D0 %X' % d0, beat, 0x824770, [node, ev],
            w(node) + [(node + 6, b8(2)), (ev + 0xD0, h16(d0))])
    for index, again in ((0, 3), (2, 0)):
        add('824770 step1 E2 scribble %d %d' % (index, again), beat, 0x824770, [node, ev],
            w(node) + [(node + 6, b8(1)), (ev + 0xE2, b8(index)), (ev + 0xD4, f32b(0.1))],
            queues={0x1287F0: [Scribble(0, [(ev + 0xE2, b8(again)), (node + 6, b8(0x40))])]})
    add('824770 step1 heading scribble', beat, 0x824770, [node, ev],
        w(node) + [(node + 6, b8(1)), (ev + 0xD4, f32b(0.5))],
        queues={0x103230: [Scribble(0, [(g5 + 0xC4, f32b(0.5)), (ev + 0x40, f32b(0.25))])],
                0x1031E0: [Scribble(0, [(ev + 0xD4, f32b(0.1))]), Scribble(0, [(ev + 0xD4, f32b(0.1))])]},
        run_set=RUN - {0x103230, 0x1031E0})
    add('824770 step0 target moved', beat, 0x824770, [node, ev],
        w(node) + [(node + 6, b8(0)), (g5 + 0xC4, f32b(2.5)), (0x829DC0 + 4 * ram[ev + 0xE2], w32(1))])

    # ---- 0x824D50 (0x823CD0 step 7)
    for step in (0, 1, 2, 0xFF):
        add('824D50 step%d' % step, beat, 0x824D50, [node, ev], w(node) + [(node + 6, b8(step)), (ev + 0xD4, f32b(2.0))])
    for d4 in (2.0, 1.9999999, 2.0000002, 0.0, -0.0, 3.4028235e38, 1e-39, -2.0):
        add('824D50 step1 D4 %r' % d4, beat, 0x824D50, [node, ev], w(node) + [(node + 6, b8(1)), (ev + 0xD4, f32b(d4))])
    for pattern in BIT_EDGES:
        add('824D50 step1 D4 bits %08X' % pattern, beat, 0x824D50, [node, ev],
            w(node) + [(node + 6, b8(1)), (ev + 0xD4, w32(pattern))])
    add('824D50 step1 scribble', beat, 0x824D50, [node, ev], w(node) + [(node + 6, b8(1)), (ev + 0xD4, f32b(1.0))],
        queues={0x12DE90: [Scribble(0, [(ev + 0xD4, f32b(2.5)), (0x275B40, w32(cr[1] + 0x110))])],
                0x1C69A0: [Scribble(0, [(ev + 0xD4, f32b(0.5))])]})

    # ---- 0x8282F0 (0x826D40's probe)
    pnode = probes[0]
    pm = probe_matrix(ram, pnode)
    zero_base = [(0x810360, w32(0)), (0x810364, w32(0)), (0x810368, w32(0))]
    rec = cr[4]   # a record whose byte +3 the kind test reads

    def pw(*more):
        return w(pnode) + zero_base + list(more)

    def hit(result, d8, d4, point=(0.0, 0.0, 0.0), d0=None):
        writes = [(0x700031D8, w32(d8)), (0x700031D4, w32(d4)),
                  (0x700031B0, f32b(point[0])), (0x700031B4, f32b(point[1])), (0x700031B8, f32b(point[2]))]
        if d0 is not None:
            writes.append((0x700031D0, w32(d0)))
        return Scribble(result, writes)
    base = (0.0, 0.0, 0.0)   # D_00810360 is zeroed in these cases, so d is the point's x squared
    for first in (0, 1, -1) + WIDE_RESULTS:
        for second in (0, 1) + WIDE_RESULTS[:2]:
            add('8282F0 %X %X' % (first & MASK, second & MASK), beat, 0x8282F0, native_args_of(0x8282F0, pnode, pm),
                pw((pnode + 4, b8(4))), queues={0x19AA80: [first], 0x19A570: [second]})
    for d in (100.0, 100.00001, 99.99999, 0.0, 1e19, -100.0):
        for d8, kind in ((1, 0x10), (1, 0x0F), (1, 0x13), (1, 0x14), (0, 0x10), (2, 0x12), (1, 0xFF)):
            for state, count in ((4, 1), (1, 13), (1, 12)):
                add('8282F0 hit d %g %d %02X s%d %d' % (d, d8, kind, state, count), beat, 0x8282F0,
                    native_args_of(0x8282F0, pnode, pm),
                    pw((pnode + 4, b8(state)), (pnode + 0x200, w32(count)), (rec + 3, b8(kind))),
                    queues={0x19AA80: [0], 0x19A570: [hit(1, d8, rec, (base[0] + d, base[1], base[2]))]})
    for mine in (0, rec, cr[3]):
        for d8 in (1, 0):
            for state in (4, 1):
                add('8282F0 204 %X %d s%d' % (mine, d8, state), beat, 0x8282F0, native_args_of(0x8282F0, pnode, pm),
                    pw((pnode + 4, b8(state)), (pnode + 0x200, w32(13)), (pnode + 0x204, w32(mine)), (rec + 3, b8(0x05))),
                    queues={0x19AA80: [1], 0x19A570: [hit(1, d8, rec, (base[0] + 5.0, base[1], base[2]))]})
    for count in (12, 13, 0x7FFFFFFF, -1, 0x80000000, 14):
        add('8282F0 200 %X' % (count & MASK), beat, 0x8282F0, native_args_of(0x8282F0, pnode, pm),
            pw((pnode + 4, b8(1)), (pnode + 0x200, w32(count))), queues={0x19AA80: [1], 0x19A570: [0]})
    for word in (0, -1, 0x7FFFFFFF, 0x80000000, 0x12345678, 0xFFFF0000, 0x0000FFFF, 0x7FFF8000):
        add('8282F0 random %08X' % (word & MASK), beat, 0x8282F0, native_args_of(0x8282F0, pnode, pm),
            pw((pnode + 4, b8(4))), queues={0x19AA80: [1], 0x19A570: [0], 0x122BB8: [word]},
            run_set=RUN - {0x122BB8})
    add('8282F0 restore', beat, 0x8282F0, native_args_of(0x8282F0, pnode, pm), pw((pnode + 4, b8(4))),
        queues={0x19AA80: [Scribble(1, [(0x700031B0, f32b(3.5))])],
                0x19A570: [hit(0, 7, 0x1234, (1.0, 2.0, 3.0), 0x5678)]})
    add('8282F0 restore 2', beat, 0x8282F0, native_args_of(0x8282F0, pnode, pm), pw((pnode + 4, b8(1))),
        spad=[(0x700031D8, w32(1)), (0x700031D4, w32(rec)), (0x700031D0, w32(9))],
        queues={0x19AA80: [1], 0x19A570: [hit(0, 0, 0, (1.0, 2.0, 3.0), 0)]})
    add('8282F0 scribble tail', beat, 0x8282F0, native_args_of(0x8282F0, pnode, pm),
        pw((pnode + 4, b8(4)), (pnode + 0x204, w32(rec))),
        queues={0x19AA80: [0], 0x19A570: [hit(1, 1, rec, (base[0] + 5.0, base[1], base[2]))],
                0x1E2BA0: [Scribble(0, [(pnode + 0x204, w32(cr[0])), (0x700031D4, w32(cr[0]))])],
                0x1CD520: [Scribble(0, [(pnode + 4, b8(1))])]})
    add('8282F0 scribble table', beat, 0x8282F0, native_args_of(0x8282F0, pnode, pm),
        pw((pnode + 4, b8(1)), (pnode + 0x200, w32(13))),
        queues={0x19AA80: [1], 0x19A570: [0],
                0x1031E0: [Scribble(0, [(0x275B40, w32(probes[1] + 0x110))])]}, run_set=RUN - {0x1031E0})
    add('8282F0 data changed', beat, 0x8282F0, native_args_of(0x8282F0, pnode, pm),
        pw((pnode + 4, b8(1)), (pnode + 0x200, w32(13)), (0x82CB24, f32b(-9.5))), queues={0x19AA80: [1]})
    for other in probes[1:]:
        add('8282F0 %X hit' % other, beat, 0x8282F0, native_args_of(0x8282F0, other, probe_matrix(ram, other)),
            w(other) + [(other + 4, b8(4))],
            queues={0x19AA80: [1], 0x19A570: [hit(1, 1, rec, (base[0] + 3.0, base[1] - 2.0, base[2] + 1.0))]})


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
    unmapped address and a fault latched before the call (on 0x825D30 with
    +0x05 = 1: 001BA1F0, 001BA1A0, 001C64F0, 001C68C0, the +0x4C callback),
    then every entry with a latched fault, a NULL hook table, a NULL fault
    pointer and (entries with a result) a NULL result pointer."""
    ram, spad = CAPTURES['a01r_00_to_train_room']
    node = [n for n in owners(ram, 0x825950) if ram[n + 0xD] == 0x47][0]
    problems = []

    def attempt(null=None, failing=None, unmapped=None, latched=False):
        ram_c = (C.c_uint8 * RAM_SIZE).from_buffer_copy(ram)
        spad_c = (C.c_uint8 * SPAD_SIZE).from_buffer_copy(spad)
        ram_c[node + 5] = 1
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
        status = NATIVE.em_area01_revisit_00825D30(C.byref(hooks), node, C.byref(fault))
        return status, fault.address, fault.code, calls, before == C.string_at(base, RAM_SIZE)

    every = ['w_001BA1F0', 'w_001BA1A0', 'w_001C64F0', 'w_001C68C0', 'w_callback']
    status, address, code, calls, _ = attempt()
    if (status, address, code) != (0, 0, 0) or calls != every:
        problems.append(('fault-check baseline', status, hex(address), code, calls))
    status, address, code, calls, _ = attempt(null='w_001BA1F0')
    if (status, address, code) != (-1, 0x1BA1F0, 1) or calls:
        problems.append(('NULL hook', status, hex(address), code, calls))
    status, address, code, calls, same = attempt(failing='w_001BA1F0')
    if (status, address, code) != (-1, 0x1BA1F0, 2) or calls != ['w_001BA1F0'] or not same:
        problems.append(('failing hook', status, hex(address), code, calls, same))
    status, address, code, calls, _ = attempt(unmapped=node + 0x4C)
    if (status, address, code) != (-1, node + 0x4C, 5) or calls != every[:4]:
        problems.append(('unmapped address', status, hex(address), code, calls))
    status, address, code, calls, _ = attempt(unmapped=node + 5)
    if (status, address, code) != (-1, node + 5, 5) or calls:
        problems.append(('unmapped address before any call', status, hex(address), code, calls))
    status, address, code, calls, _ = attempt(unmapped=node + 0x1C)
    if (status, address, code) != (-1, node + 0x1C, 5) or calls != every[:1]:
        problems.append(('unmapped address inside an address argument', status, hex(address), code, calls))
    status, address, code, calls, same = attempt(latched=True)
    if (status, address, code) != (-1, 0x1234, 7) or calls or not same:
        problems.append(('latched fault', status, hex(address), code, calls, same))
    if NATIVE.em_area01_revisit_00825D30(None, node, C.byref(Fault())) != -1:
        problems.append(('NULL hooks accepted',))
    # every entry: a fault latched on entry, a NULL hook table, a NULL fault
    # pointer or a NULL result pointer returns -1 at once, with no call and
    # no result
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
        args = native_args_of(entry, *([node, node + 0x1F0, 0][:{'owner': 1, 'owner_sp': 1, 'pair': 2, 'self_ret': 1,
                                                                   'pair_ret': 2, 'op09': 3, 'probe': 2}[kind]]))
        tries = [(C.byref(hooks), Fault(0x1234, 7), True), (None, Fault(), True), (C.byref(hooks), None, True)]
        if kind in RESULT_KINDS:
            tries.append((C.byref(hooks), Fault(), False))
        for table, fault, with_result in tries:
            del touched[:]
            where = C.byref(fault) if fault is not None else None
            status, result = call_entry(NATIVE, entry, table, args, where, with_result)
            latched = fault is None or (fault.address, fault.code) == (
                (0x1234, 7) if table is not None and with_result else (0, 0))
            if status != -1 or touched or result != 0x5A5A5A5A or not latched:
                problems.append(('entry %06X refused' % entry, status, touched[:2], hex(result & MASK)))
    return problems


HOOK_NAMES = [name for name, _, _, _ in HOOKS] + ['w_callback']
BY_NAME = {name: address for name, address, _, _ in HOOKS}


def hook_sites(results):
    """The cases hook_contract_site runs on: passing cases (the original
    does not stop) that together call every hook, the +0x4C callback and
    every entry, picked greedily (most targets not yet covered, then the
    earliest case). Returns ([(case index, targets)], targets no case
    reaches); a target is a hook name or ('bytes', entry)."""
    want = set(HOOK_NAMES) | {('bytes', entry) for entry in FUNCS}
    options = [(index, (set(calls) | {('bytes', entry)}) & want)
               for index, (_, entry, errors, _, _, calls) in enumerate(results) if not errors and calls is not None]
    sites = []
    while want and options:
        index, targets = max(options, key=lambda option: (len(option[1] & want), -option[0]))
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
        if is_stack(key):   # the stacks are reset for every native run
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
    """The fail-stop contract of the header (em_area01_revisit.h), on one
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
    where each wrapper's own `rv_failed` test is observable. Returns
    (problems found, native runs made)."""
    index, targets = site
    label, beat, entry, native_args, patches, spad_patches, script, run_set = CASES[index]
    ram0, spad0 = CAPTURES[beat]
    ram, spad = patched(ram0, patches), patched(spad0, spad_patches)
    oracle = Oracle(ram, spad, entry, (native_args, {u32(ram, native_args[0] + 0x4C)} | SCRIBBLED_CALLBACKS),
                    copy_script(script),
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
    return reference_mode.select(cases, reference_mode.pick(len(cases), fixed + len(pinned) + QUICK_RANDOM), 0xA01E,
                                 keep=lambda i, c: i < fixed or i in pinned), captured, targeted, len(cases)


def main():
    global NATIVE
    elf = read_elf()
    CAPTURES.update(load_captures(elf))
    NATIVE = build(os.environ.get('EM_AREA01_RV_SOURCE'))
    selected, captured, targeted, total = select_cases(elf)
    CASES[:] = selected
    results = run_cases(selected)
    failures, per_fn, seen, totals = [], {}, set(), [0, 0, 0]
    for label, entry, errors, pcs, counts, _ in results:
        totals = [a + b for a, b in zip(totals, counts)]
        per_fn[entry] = per_fn.get(entry, 0) + 1
        seen.update(pcs)
        if errors:
            failures.append((label, errors))
    ram0 = CAPTURES[BEATS[0]][0]
    reference_mode.banner(reference_mode.part(len(selected), total, 'cases'),
                          '%d capture + %d designed cases kept' % (captured, targeted))
    covered_total = words_total = 0
    for entry in sorted(FUNCS):
        _, size, _ = FUNCS[entry]
        words = reachable_words(ram0, entry, size)
        hit = len(words & seen)
        covered_total += hit
        words_total += len(words)
        print('  %06X  %4d cases  %4d/%4d reachable non-branch words executed'
              % (entry, per_fn.get(entry, 0), hit, len(words)))
    print('  coverage %d/%d reachable words' % (covered_total, words_total))
    if covered_total < words_total:
        failures.append(('coverage', ['%d of %d reachable original words not executed'
                                      % (words_total - covered_total, words_total)]))
    problems = fault_checks()
    print('  native fail-stop checks: %s' % ('ok (NULL hook, failing hook, unmapped address before any call, inside an address argument and at the callback read, latched fault, NULL hooks; every entry refuses a latched fault, a NULL hook table, a NULL fault pointer and a NULL result pointer)'
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
             'all 32 MiB + scratchpad + stack' if reference_mode.FULL else 'dirty set', totals[2]))
    print('all %d cases identical: callee calls, arguments and results; memory at every call entry (%s); '
          'the memory accesses between calls (one for one, in order); '
          'memory after the last store (all 32 MiB + scratchpad + stack); the table\'s ctx at every hook, `bytes` and '
          'callback call'
          % (len(selected), 'all 32 MiB + scratchpad + stack' if reference_mode.FULL else 'dirty set'))


if __name__ == '__main__':
    main()
