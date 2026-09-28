#!/usr/bin/env python3
"""Execute the ORIGINAL AREA02 overlay code and compare em_area02_overlay.c.

docs/AREA02_OVERLAY_PORT.md. The 15 AREA02 overlay functions of the AREA02
census delta (decomp build/s87/census/a02_delta.json, new_functions, region
overlay:AREA02): 0x823580, 0x823900, 0x823930, 0x823980, 0x823D70,
0x824020, 0x8242F0, 0x824800, 0x824910, 0x824AC0, 0x824C40, 0x824CD0,
0x824D50, 0x824FA0, 0x825100, plus 0x825520, which 0x825100 calls and the
route did not run. Unlike AREA00's, these call each other (0x823930 ->
0x823980 / 0x824020, 0x823980 -> 0x824D50, 0x824020 -> 0x824800 /
0x8242F0, 0x8242F0 -> 0x824CD0 / 0x824910 / 0x824AC0, 0x824CD0 -> 0x824C40,
0x825100 -> 0x825520): the oracle runs those nested calls as original code
at the top level (only boot functions are hooks), and every function is
also an entry of its own.

The harness is tools/test_area00_overlay_reference.py (lane A00OVL, itself
the AREA01 overlay design), reused with the AREA02 captures, the AREA02
hook table and cases, entries with a result, and one addition: STACK
LOCALS. 0x824910 and 0x824CD0 write locals in their own stack frames and
pass their addresses to callees. The oracle runs with the private stack
top STACK_TOP as sp at entry; the window [STACK_LO, STACK_TOP) below it
(the frames of the overlay's own call chain; nested helpers get frames
0x400 lower, EE.nested) is compared memory: the function's own loads and
stores there are logged and compared one for one like any other access,
except the frame bookkeeping (a callee-saved register, $ra or $f20..$f31
saved or restored at an $sp offset; stack_frame_op), which the
translation has no counterpart for. Stores of callees that run as
original code into the window (001026A0 writing the transformed points)
are replayed like their RAM writes. The window starts zeroed (plus the
poison of the poisoned run) on both sides, and the native `bytes` maps it.

Oracle: the shared EE interpreter with the measured float model (FallEE,
tools/test_player_fall_reference.py) runs the original overlay code
resident in the recorded RAM images (decomp build/s87/route_a02/<beat>/
eeMemory.bin + scratchpad.bin for a02_00..a02_04 and a02_s0, and
build/s87/route_a01r/a01r_03_door16, the AREA02 arrival; overlay id 3 at
0x823500). Before any case the test checks that the overlay text in each
image equals the user's extract/OVERLAY/AREA02.BIN (text size from its
header), that the jump table equals the file and that the boot text below
0x241000 equals the pinned ELF, so every executed instruction is original.
Nothing here embeds original bytes; reports hold counts only.

Callees (every boot function and the actor's +0x4C callback) are
intercepted at their entry and logged with their arguments. A callee either
runs as ORIGINAL code nested inside the oracle (the pure helpers in RUN:
matrix x vector, the random generator, cos, sin, the angle wrap,
the turn step 001B12B0 and the script start 001BA1A0), its writes recorded,
or is stubbed with the case's scripted result (everything else). The native
module runs over a byte copy of the same RAM with hooks that must be called
in the same order with the same arguments; each hook replays the original
callee's writes and result.

Compared, per case (as in the AREA00 harness, docstring there):
  * at the ENTRY of every call, BEFORE the callee's writes are replayed:
    native RAM, scratchpad and stack window against the oracle's memory at
    the entry of the same original call (quick mode: the dirty set;
    EM_TEST_FULL=1: all 32 MiB + scratchpad + window at every call entry);
    then the callee and its arguments (64-bit registers where the hook
    takes a 64-bit argument).
  * between calls: the native module's memory accesses are the original's
    own loads and stores one for one, in order, with the same address,
    size and `changed` mark. The only accesses left out are the jump-table
    loads of 0x8242F0, which the C switch encodes (TABLE_BYTES), and the
    frame bookkeeping above.
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
  * the fail-stop contract of em_area02_overlay.h (hook_contract_site and
    fault_checks), and the table's ctx at every `bytes`, hook and callback
    call.

Cases:
  capture   every owner node of every image, exactly as captured, for each
            entry that takes the node (the dispatcher and its callees, the
            gates, the examine owner, 0x825100, the freed 0x823580 node),
            the area init and 0x825520 on every image.
  designed  targeted_cases: every state and step, callee results 0 / 1 /
            other values at every test of a result, scribbling callees,
            story bytes and flags on both sides of every test, the float
            boundaries of every compare.
  perturbed seeded variations (EM_TEST_FULL=1 only).
Coverage: the report counts the original instruction words the oracle
executed inside each function (branch words excluded).

EM_AREA02_OVL_SOURCE=<file> tests another copy of the module source (the
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
OVERLAY_FILE = DECOMP / 'extract/OVERLAY/AREA02.BIN'
OUT = ROOT / 'build/area02/ovl'
MASK = 0xFFFFFFFF
RAM_SIZE, SPAD_SIZE = 0x2000000, 0x4000
ARENA, TEXT_END = 0x823500, 0x825680
# The stack window: the frames of the overlay's own call chain below the
# entry sp (deepest: 0x823930 -> 0x824020 -> 0x8242F0 -> 0x824CD0 ->
# 0x824C40, 0x150 bytes); nested helpers run 0x400 lower (EE.nested).
STACK_LO = STACK_TOP - 0x400
STACK_SIZE = STACK_TOP - STACK_LO
# The images with AREA02 resident: the arrival (a01r_03, sub 1), the end of
# every AREA02 beat up to a02_04 (sub 0; a02_05 ends in AREA04) and a02_s0
# (sub 1).
BEAT_DIRS = {'a01r_03_door16': DECOMP / 'build/s87/route_a01r/a01r_03_door16'}
for _b in ('a02_00_duct', 'a02_01_switch', 'a02_02_ladder_escape', 'a02_03_over_wreck', 'a02_04_panel',
           'a02_s0_mts_bed'):
    BEAT_DIRS[_b] = DECOMP / 'build/s87/route_a02' / _b
BEATS = list(BEAT_DIRS)
POOL, NODE, NODES = 0x7A5640, 0x2F0, 400


class _Route:
    """ROUTE / beat resolves to the beat's folder (two route trees)."""

    def __truediv__(self, beat):
        return BEAT_DIRS[beat]


ROUTE = _Route()

# (entry, native symbol, byte length of the function body, kind). Kinds:
# owner (self), owner_sp (self, sp), ret (self -> result), ret_sp (self, sp
# -> result), void (no argument), point (a0 = a point).
FUNCS = {
    0x823580: ('em_area02_ovl_00823580', 0x380, 'owner'),
    0x823900: ('em_area02_ovl_00823900', 0x2C, 'void'),
    0x823930: ('em_area02_ovl_00823930', 0x50, 'owner_sp'),
    0x823980: ('em_area02_ovl_00823980', 0x3EC, 'owner'),
    0x823D70: ('em_area02_ovl_00823D70', 0x2B0, 'owner'),
    0x824020: ('em_area02_ovl_00824020', 0x2C8, 'owner_sp'),
    0x8242F0: ('em_area02_ovl_008242F0', 0x510, 'ret_sp'),
    0x824800: ('em_area02_ovl_00824800', 0x104, 'ret'),
    0x824910: ('em_area02_ovl_00824910', 0x1A8, 'owner_sp'),
    0x824AC0: ('em_area02_ovl_00824AC0', 0x178, 'owner'),
    0x824C40: ('em_area02_ovl_00824C40', 0x88, 'point'),
    0x824CD0: ('em_area02_ovl_00824CD0', 0x80, 'owner_sp'),
    0x824D50: ('em_area02_ovl_00824D50', 0x244, 'ret'),
    0x824FA0: ('em_area02_ovl_00824FA0', 0x154, 'owner'),
    0x825100: ('em_area02_ovl_00825100', 0x420, 'owner'),
    0x825520: ('em_area02_ovl_00825520', 0x130, 'void'),
}
SELF_KINDS = ('owner', 'owner_sp', 'ret', 'ret_sp')
RESULT_KINDS = ('ret', 'ret_sp')
SP_KINDS = ('owner_sp', 'ret_sp')


def original_args(entry, native_args):
    """The original's argument registers: the stack-pointer entries take sp
    as a native argument only (the original gets it in $sp)."""
    return native_args[:1] if FUNCS[entry][2] in SP_KINDS else native_args


# Hooks, in the field order of EmArea02OvlHooks (after ctx and bytes).
# name -> (original address, [(ctype, register)], result kind or None)
U, I, F, Q = 'u', 'i', 'f', 'q'
HOOKS = [
    ('w_0011DE90', 0x11DE90, [(F, 12)], F),
    ('w_0011E2A8', 0x11E2A8, [(F, 12)], F),
    ('w_001026A0', 0x1026A0, [(U, 4), (U, 5), (U, 6)], None),
    ('w_00121A28', 0x121A28, [(U, 4), (I, 5), (I, 6)], None),
    ('w_00122BB8', 0x122BB8, [], I),
    ('w_0019C6F0', 0x19C6F0, [(I, 4), (I, 5)], None),
    ('w_001A2370', 0x1A2370, [(U, 4), (U, 5)], None),
    ('w_001AA700', 0x1AA700, [(U, 4)], None),
    ('w_001AF780', 0x1AF780, [], U),
    ('w_001AFC10', 0x1AFC10, [(U, 4)], None),
    ('w_001B0FD0', 0x1B0FD0, [(U, 4)], I),
    ('w_001B12B0', 0x1B12B0, [(F, 12), (F, 13), (F, 14)], F),
    ('w_001B1470', 0x1B1470, [(F, 12)], F),
    ('w_001B17A0', 0x1B17A0, [(U, 4)], None),
    ('w_001B18F0', 0x1B18F0, [(U, 4), (U, 5), (U, 6)], I),
    ('w_001B1B70', 0x1B1B70, [(U, 4)], None),
    ('w_001B1EA0', 0x1B1EA0, [(I, 4), (U, 5), (U, 6), (I, 7)], I),
    ('w_001B6660', 0x1B6660, [(U, 4)], None),
    ('w_001B6F00', 0x1B6F00, [(U, 4), (U, 5), (F, 12)], None),
    ('w_001BA1A0', 0x1BA1A0, [(U, 4), (U, 5)], None),
    ('w_001BA1C0', 0x1BA1C0, [(U, 4), (I, 5)], I),
    ('w_001BA1F0', 0x1BA1F0, [(U, 4)], I),
    ('w_001C6120', 0x1C6120, [(U, 4), (I, 5)], U),
    ('w_001C6150', 0x1C6150, [(U, 4)], I),
    ('w_001C6380', 0x1C6380, [(U, 4)], None),
    ('w_001C63E0', 0x1C63E0, [(U, 4), (I, 5)], None),
    ('w_001C64F0', 0x1C64F0, [(U, 4), (F, 12)], None),
    ('w_001C68C0', 0x1C68C0, [(U, 4)], None),
    ('w_001CA6E0', 0x1CA6E0, [(U, 4), (U, 5)], None),
    ('w_001CB5B0', 0x1CB5B0, [(I, 4)], None),
    ('w_001CD520', 0x1CD520, [(I, 4), (I, 5), (U, 6), (Q, 7), (F, 12), (F, 13), (F, 14), (U, 8)], None),
    ('w_001EFD20', 0x1EFD20, [(I, 4), (U, 5)], U),
    ('w_001EFD90', 0x1EFD90, [(I, 4), (U, 5), (U, 6)], I),
    ('w_001F02C0', 0x1F02C0, [(U, 4), (I, 5), (F, 12)], None),
    ('w_001F4BF0', 0x1F4BF0, [(U, 4), (U, 5)], None),
    ('w_001F6B30', 0x1F6B30, [], None),
    ('w_001FA790', 0x1FA790, [(I, 4), (I, 5)], None),
    ('w_001FABB0', 0x1FABB0, [], None),
    ('w_001FAE70', 0x1FAE70, [(I, 4)], None),
]
BY_ADDRESS = {address: (name, args, result) for name, address, args, result in HOOKS}

# Callees that run as original code inside the oracle (writes replayed to
# the native side). Pure helpers only: none of them reaches hardware.
RUN = {0x11DE90, 0x11E2A8, 0x1026A0, 0x122BB8, 0x1B12B0, 0x1B1470, 0x1BA1A0}

# 0x8242F0's +0x06 step table (runtime address), keyed by the runtime
# address of its jump.
JUMP_TABLES = {0x8243D0: (0x829200, 8)}
# The only bytes the original loads that the translation does not: that
# table, which the C switch encodes (load_captures asserts it equals
# AREA02.BIN in every image). Replay.check_access leaves them out.
TABLE_BYTES = frozenset(a for table, count in JUMP_TABLES.values() for a in range(table, table + 4 * count))


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
    sweep) into a private library under build/area02/ovl."""
    OUT.mkdir(parents=True, exist_ok=True)
    source = source or 'src/game/em_area02_overlay.c'
    stem = 'area02_overlay' if source == 'src/game/em_area02_overlay.c' else 'area02_overlay_%d' % os.getpid()
    lib = OUT / (stem + ('.dylib' if sys.platform == 'darwin' else '.so'))
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc', '-I' + str(ROOT / 'src/game'), str(source),
                    '-lm', '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    for entry, (symbol, _, kind) in FUNCS.items():
        fn = getattr(native, symbol)
        types = [C.POINTER(Hooks)]
        if kind != 'void':
            types.append(C.c_uint32)
        if kind in SP_KINDS:
            types.append(C.c_uint32)
        if kind in RESULT_KINDS:
            types.append(C.POINTER(C.c_int32))
        fn.argtypes = types + [C.POINTER(Fault)]
        fn.restype = C.c_int
    return native


def load_captures(elf):
    """Every image must hold the AREA02 overlay (header id 3) with its text
    equal to the user's AREA02.BIN, the jump table equal to the file, and
    the boot text equal to the pinned ELF: every instruction the oracle
    executes is original."""
    overlay = OVERLAY_FILE.read_bytes()
    text_end = ARENA + 0x40 + struct.unpack_from('<I', overlay, 12)[0]
    assert text_end == TEXT_END, hex(text_end)
    captures = {}
    for beat in BEATS:
        folder = ROUTE / beat
        ram = (folder / 'eeMemory.bin').read_bytes()
        spad = (folder / 'scratchpad.bin').read_bytes()
        assert len(ram) == RAM_SIZE and len(spad) == SPAD_SIZE, beat
        assert ram[ARENA:ARENA + 8] == overlay[:8] and ram[ARENA + 4] == 3, (beat, 'AREA02 overlay not resident')
        assert ram[ARENA + 0x40:TEXT_END] == overlay[0x40:TEXT_END - ARENA], (beat, 'overlay text differs from AREA02.BIN')
        assert ram[0x100000:0x241000] == elf[0x300:0x300 + 0x141000], (beat, 'boot text differs from the ELF')
        for table, count in JUMP_TABLES.values():
            assert ram[table:table + 4 * count] == overlay[table - ARENA:table - ARENA + 4 * count], \
                (beat, 'jump table differs from AREA02.BIN')
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
        test compares only the stop (docs/AREA02_OVERLAY_PORT.md section 3). A
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
        """The module uses the bytes of a `bytes` request (a02_at) before
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
        (a02_at in em_area02_overlay_internal.h), and replayed callee
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
CTX = 0xA02C7C00   # the hook table's ctx in every Replay: opaque, never dereferenced by the module


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
    cost = lambda i: 3 if selected[i][2] in (0x823930, 0x824020, 0x8242F0, 0x825100) else 1   # noqa: E731
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
OWNER_BEHAVIOURS = (0x823930, 0x823D70, 0x824FA0, 0x825100)
PLAYER_X, PLAYER_Z, PLAYER_YAW = 0x810350, 0x810358, 0x810374


def any_owner(ram, callback):
    """Pool nodes whose +0x10 behaviour is `callback`, live or not."""
    return [POOL + i * NODE for i in range(NODES) if u32(ram, POOL + i * NODE + 0x10) == callback]


def kind4(ram, node):
    return u32(ram, node + 0x10) == 0x823930 and ram[node + 2] & 0x1F == 4


F_VALUES = [0.0, -0.0, 1e-39, -1e-39, 0.25, -0.5, 1.0, -25.0, -25.000002, -24.999998, -66.0, -65.99999,
            -270.0, -280.0, -279.99997, 140.0, 140.00002, 170.0, 170.00002, 3.4028235e38, -3.4028235e38]


def rf(rng):
    pick = rng.random()
    if pick < 0.4:
        return rng.choice(F_VALUES)
    if pick < 0.8:
        return rng.uniform(-3.5, 3.5)
    return rng.uniform(-600.0, 600.0)


def fb(value):
    return fbits(value)


def owner_args(entry, node):
    return [node, STACK_TOP] if FUNCS[entry][2] in SP_KINDS else [node]


def case_list(elf):
    rng = random.Random(0xA02)
    cases = []
    count = reference_mode.pick(60, 0)

    def add(label, beat, entry, args, patches=(), spad=(), queues=None, default=None, run_set=RUN):
        cases.append((label, beat, entry, tuple(args), list(patches), list(spad),
                      Script(queues, default), frozenset(run_set)))

    for beat in BEATS:
        ram, spad = CAPTURES[beat]
        add('capture %s 823900' % beat, beat, 0x823900, [])
        add('capture %s 825520' % beat, beat, 0x825520, [])
        for callback in OWNER_BEHAVIOURS:
            for node in owners(ram, callback):
                add('capture %s %06X @%X' % (beat, callback, node), beat, callback, owner_args(callback, node))
        for node in any_owner(ram, 0x823580):
            add('capture %s 823580 @%X' % (beat, node), beat, 0x823580, [node])
        for node in owners(ram, 0x823930):
            if ram[node + 2] & 0x1F == 9:
                add('capture %s 823980 @%X' % (beat, node), beat, 0x823980, [node])
                add('capture %s 824D50 @%X' % (beat, node), beat, 0x824D50, [node])
            if kind4(ram, node):
                add('capture %s 824020 @%X' % (beat, node), beat, 0x824020, owner_args(0x824020, node))
                add('capture %s 824800 @%X' % (beat, node), beat, 0x824800, [node])
                if ram[node + 0xD] in (7, 8):
                    add('capture %s 8242F0 @%X' % (beat, node), beat, 0x8242F0, owner_args(0x8242F0, node))
                if ram[node + 0xD] == 7:
                    for entry in (0x824910, 0x824AC0, 0x824CD0):
                        add('capture %s %06X @%X' % (beat, entry, node), beat, entry, owner_args(entry, node))
                    add('capture %s 824C40 @%X' % (beat, node), beat, 0x824C40, [node + 0xB0])
    captured = len(cases)
    targeted_cases(add)
    targeted = len(cases) - captured

    # ---------------------------------------------------------------- perturbed
    for i in range(count):
        beat = rng.choice(['a02_00_duct', 'a02_01_switch', 'a02_02_ladder_escape'])
        ram, _ = CAPTURES[beat]
        for callback in OWNER_BEHAVIOURS:
            for node in owners(ram, callback):
                p = [(node + 4, b8(rng.choice([0, 1, 1, 1, 2, 3, 4, 0xFF]))),
                     (node + 5, b8(rng.choice([0, 1, 2, 3, 8, 9, 10]))),
                     (node + 6, b8(rng.randrange(9))), (node + 7, b8(rng.randrange(4))),
                     (node + 0xB, b8(rng.choice([0, 4, 0xFB, 0xFF]))),
                     (node + 0xB0, f32b(rf(rng))),
                     (0x810761, b8(rng.choice([0, 1, 0xFF]))), (0x8107E1, b8(rng.randrange(256))),
                     (0x81083F, b8(rng.randrange(4))), (PLAYER_X, f32b(rf(rng)))]
                q = {a: [rng.choice([0, 1, 2, -1]) for _ in range(3)]
                     for a in (0x1BA1F0, 0x1B0FD0, 0x1BA1C0, 0x1B18F0, 0x1B1EA0)}
                add('perturbed %06X %d' % (callback, i), beat, callback, owner_args(callback, node), p, queues=q)
    return cases, captured, targeted


def targeted_cases(add):
    """Designed cases for every state, step and test; all run by default."""
    beat = 'a02_01_switch'
    ram, spad = CAPTURES[beat]
    nodes = owners(ram, 0x823930)
    switch = [n for n in nodes if ram[n + 2] & 0x1F == 9][0]
    car = [n for n in nodes if kind4(ram, n) and ram[n + 0xD] == 7][0]
    k8 = [n for n in nodes if kind4(ram, n) and ram[n + 0xD] == 8][0]
    k10 = [n for n in nodes if kind4(ram, n) and ram[n + 0xD] == 0xA][0]
    k11 = [n for n in nodes if kind4(ram, n) and ram[n + 0xD] == 0xB][0]
    gates = {ram[n + 0xD]: n for n in owners(ram, 0x823D70)}
    gate14 = [n for n in owners(ram, 0x823D70) if ram[n + 0xD] == 0xE]
    exam = owners(ram, 0x824FA0)[0]
    t4 = [n for n in owners(ram, 0x825100) if ram[n + 3] == 4][0]
    t7 = [n for n in owners(ram, 0x825100) if ram[n + 3] == 7][0]
    spare = free_node(ram)
    assert len(gate14) == 2 and 0xF in gates and 0x10 in gates, 'gate layout'
    E1, F761, F83F = 0x8107E1, 0x810761, 0x81083F

    # ---- 0x823900 the area init (writes only)
    add('823900 a02_01', beat, 0x823900, [])

    # ---- 0x823580 (the spawned owner): a free node
    for state in (0, 2, 3, 4, 0xFF):
        add('823580 s%d' % state, beat, 0x823580, [spare], [(spare + 4, b8(state)), (spare + 0x200, w32(60))])
    for n in (0, 1, 9, 10, 11, 59, 60, 61, 64, 65, 66, 69, 70, 71, 74, 75, 76, 79, 80, 81, 0x7FFFFFFF, -1, -60):
        add('823580 s1 n%d' % n, beat, 0x823580, [spare], [(spare + 4, b8(1)), (spare + 0x200, w32(n))],
            queues={0x1EFD90: [0x7C0000 + n], 0x1EFD20: [0x7B0000]})
    for first in (0, car, 0x02400000):   # the first spawn read at 70 (0: none; unmapped: stops)
        for result in (0, 1, -1):
            add('823580 s1 n70 first %X %d' % (first, result), beat, 0x823580, [spare],
                [(spare + 4, b8(1)), (spare + 0x200, w32(70)), (spare + 0x1F0, w32(first))],
                queues={0x1EFD90: [result]})
    add('823580 s1 n70 scribble', beat, 0x823580, [spare], [(spare + 4, b8(1)), (spare + 0x200, w32(70))],
        queues={0x1EFD90: [Scribble(5, [(spare + 0x1F0, w32(spare)), (spare + 0x200, w32(3))])]})
    add('823580 s1 n0 scribble', beat, 0x823580, [spare], [(spare + 4, b8(1)), (spare + 0x200, w32(0))],
        queues={0x1EFD20: [Scribble(0, [(spare + 0x200, w32(0x50))])]})

    # ---- 0x823930 dispatch by +0x02 & 0x1F
    for raw in (0x09, 0x89, 0x29, 0xE9, 0x04, 0x84, 0xE4, 0x00, 0x08, 0x19, 0x14, 0xFF, 0x05):
        add('823930 raw %02X switch' % raw, beat, 0x823930, owner_args(0x823930, switch), [(switch + 2, b8(raw))])
        add('823930 raw %02X car' % raw, beat, 0x823930, owner_args(0x823930, car), [(car + 2, b8(raw))])

    # ---- 0x823980 the switch
    for state in (0, 2, 3, 4, 0xFF):
        for flag in ((0, 1, 0xFF) if state == 0 else (0,)):
            add('823980 s%d %02X' % (state, flag), beat, 0x823980, [switch],
                [(switch + 4, b8(state)), (F761, b8(flag))])
    for use in (0, 4, 0xFB, 0xFF):
        add('823980 E0 use %02X' % use, beat, 0x823980, [switch],
            [(switch + 4, b8(1)), (E1, b8(0)), (switch + 0xB, b8(use))])
    for x, y, z in ((0.0, -0.0, 3.5), (1e-39, -15.0, -3.5), (140.0, 0.0, -25.0), (-1.0, -14.999999, 1e-7),
                    (3.4028235e38, 3.4028235e38, -3.4028235e38), (0.00012, 3e-7, 0.0002), (5e-8, 1e-6, 2e-7)):
        add('823980 E0 pos %g %g %g' % (x, y, z), beat, 0x823980, [switch],
            [(switch + 4, b8(1)), (E1, b8(0)), (switch + 0xB0, f32b(x)), (switch + 0xB4, f32b(y)),
             (switch + 0xB8, f32b(z))])
    for result in (0, 1, 2, -1) + WIDE_RESULTS:
        add('823980 E1 %X' % (result & MASK), beat, 0x823980, [switch], [(switch + 4, b8(1)), (E1, b8(1))],
            queues={0x1BA1F0: [result]})
    for e1 in (0x40, 0x43):   # E1 re-read after the calls, bit 1 clear and set
        add('823980 E1 scribble %02X' % e1, beat, 0x823980, [switch], [(switch + 4, b8(1)), (E1, b8(1))],
            queues={0x1BA1F0: [1], 0x1FA790: [Scribble(0, [(E1, b8(e1))])]})
    for six in (0, 1, 2, 0xFF):
        for gate in (0, 1, 0x80, 0xFF):
            add('823980 EFF six %d %02X' % (six, gate), beat, 0x823980, [switch],
                [(switch + 4, b8(1)), (E1, b8(0xFF)), (switch + 6, b8(six)), (0x282154, b8(gate))])
    for e1 in (2, 3, 0x80, 0xFE):
        add('823980 E%02X' % e1, beat, 0x823980, [switch], [(switch + 4, b8(1)), (E1, b8(e1))])
    for step in (1, 3, 4, 7, 8, 10, 11, 0xFF):
        for e1 in (3, 0xFF):
            add('823980 step %d E%02X' % (step, e1), beat, 0x823980, [switch],
                [(switch + 4, b8(1)), (E1, b8(e1)), (switch + 5, b8(step))])
    add('823980 step0 E0 after scribble', beat, 0x823980, [switch],
        [(switch + 4, b8(1)), (E1, b8(0)), (switch + 5, b8(1))],
        queues={0x1B17A0: [Scribble(0, [(E1, b8(3))])]})
    add('823980 step1 E3 scribble', beat, 0x823980, [switch],
        [(switch + 4, b8(1)), (E1, b8(3)), (switch + 5, b8(1))],
        queues={0x1B17A0: [Scribble(0, [(E1, b8(0))])]})
    for step in (2, 9):
        for result in (2, -1, 0x80000000):
            add('823980 step%d result %X' % (step, result & MASK), beat, 0x823980, [switch],
                [(switch + 4, b8(1)), (E1, b8(3)), (switch + 5, b8(step))], queues={0x1BA1F0: [result]})
        for result in (0, 1):
            for x in (-25.0, -25.000002, -24.999998, -100.0, 0.0, -0.0, -3.4028235e38):
                for z in ((-66.0, -65.99999, -66.00001, 10.0) if step == 2 and x == -100.0 else (-70.0,)):
                    add('823980 step%d %d x %g z %g' % (step, result, x, z), beat, 0x823980, [switch],
                        [(switch + 4, b8(1)), (E1, b8(3)), (switch + 5, b8(step)), (PLAYER_X, f32b(x)),
                         (PLAYER_Z, f32b(z))], queues={0x1BA1F0: [result]})
        add('823980 step%d scribble' % step, beat, 0x823980, [switch],
            [(switch + 4, b8(1)), (E1, b8(3)), (switch + 5, b8(step)), (PLAYER_X, f32b(0.0))],
            queues={0x1BA1F0: [Scribble(1, [(PLAYER_X, f32b(-30.0)), (PLAYER_Z, f32b(-70.0))])]})

    # ---- 0x824D50 the zone test (and 0x823980 step 0 through it): the four
    # transformed points are scribbled by a stubbed 001026A0
    owner = u32(ram, switch + 0x1C)
    v = switch + 0x2B0
    ox, oz = 100.0, 20.0

    def zone(points, px, pz, x, yaw, c50c, quad=None, player=None, dist=None):
        pts = [(v + 0x10 * i, struct.pack('<4f', *pt)) for i, pt in enumerate(points)]
        writes = [Scribble(0, [pt]) for pt in pts]
        patches = [(owner + 0xB0, f32b(ox)), (owner + 0xB8, f32b(oz)),
                   (0x810360, f32b(px)), (0x810368, f32b(pz)), (PLAYER_X, f32b(x)), (PLAYER_YAW, f32b(yaw)),
                   (0x81050C, b8(c50c))]
        return patches, {0x1026A0: writes, 0x1B1EA0: list(quad or [1, 1])}
    square = [(10.0, 0, 0, 1), (20.0, 0, 0, 1), (30.0, 0, 0, 1), (5.0, 0, 0, 1)]
    zone_cases = []
    for x in (15.0, 10.0, 20.0, 10.000001, 19.999998, 25.0, 30.0, 5.0, 29.999998, 5.0000005, -0.0, 50.0):
        for yaw in (-0.5, -1.5707964, -1.5707963, -1.5707965, 0.0, -0.0, -1e-39, -2.0, -3.1415927, -3.1415925,
                    -3.1415930, 1.0):
            zone_cases.append(('x %g yaw %g' % (x, yaw), square, ox + 30.0, oz, x, yaw, 3, [1, 1]))
    for dx, dz in ((70.0, 0.0), (69.99999, 0.0), (70.00001, 0.0), (0.0, -70.0), (49.4974, 49.4974),
                   (49.49748, 49.49748), (0.0, 0.0), (3e19, 0.0), (1e-20, 1e-20)):
        zone_cases.append(('dist %g %g' % (dx, dz), square, ox + dx, oz + dz, 15.0, -0.5, 3, [1, 1]))
    for q in ([0, 0], [2, 2], [-1, -1], [0x10001, 1], [1, 0], [0, 1]):
        for x, yaw in ((15.0, -0.5), (25.0, -2.0)):
            zone_cases.append(('quad %s x %g' % (q, x), square, ox, oz, x, yaw, 3, q))
    for c50c in (0, 2, 4, 0x83, 0xFF):
        for x, yaw in ((15.0, -0.5), (25.0, -2.0)):
            zone_cases.append(('50C %X x %g' % (c50c, x), square, ox, oz, x, yaw, c50c, [1, 1]))
    for label, points, px, pz, x, yaw, c50c, q in zone_cases:
        patches, queues = zone(points, px, pz, x, yaw, c50c, q)
        add('824D50 %s' % label, beat, 0x824D50, [switch], patches, queues=queues, run_set=RUN - {0x1026A0})
    for x, yaw in ((15.0, -0.5), (25.0, -2.0), (40.0, -0.5)):
        patches, queues = zone(square, ox, oz, x, yaw, 3)
        add('823980 step0 zone x %g' % x, beat, 0x823980, [switch],
            patches + [(switch + 4, b8(1)), (E1, b8(3)), (switch + 5, b8(0))], queues=queues,
            run_set=RUN - {0x1026A0})
    patches, queues = zone(square, ox, oz, 15.0, -0.5, 3)
    queues[0x1B1EA0] = [Scribble(1, [(PLAYER_X, f32b(25.0)), (PLAYER_YAW, f32b(-2.0))])]
    add('824D50 scribble player', beat, 0x824D50, [switch], patches, queues=queues, run_set=RUN - {0x1026A0})
    patches, queues = zone(square, ox, oz, 15.0, -0.5, 3)
    queues[0x1026A0][0] = Scribble(0, [(v, struct.pack('<4f', 10.0, 0, 0, 1)), (switch + 0x1C, w32(car))])
    add('824D50 scribble owner', beat, 0x824D50, [switch], patches, queues=queues, run_set=RUN - {0x1026A0})
    add('824D50 real points', beat, 0x824D50, [switch],
        [(owner + 0xB0, f32b(u32f(ram, 0x810360))), (owner + 0xB8, f32b(u32f(ram, 0x810368)))])

    # ---- 0x823D70 the gates
    for kind, node in sorted(gates.items()) + [(0xE, gate14[0]), (0xE, gate14[1])]:
        for state in (0, 2, 3, 4, 0xFF):
            for flag in ((0, 1, 0xFF) if state == 0 else (0,)):
                add('823D70 %X@%X s%d %02X' % (kind, node, state, flag), beat, 0x823D70, [node],
                    [(node + 4, b8(state)), (F761, b8(flag))], queues={0x1B0FD0: [1]})
    for kind in (0, 0xD, 0x11, 0xFF):
        add('823D70 kind %X s1' % kind, beat, 0x823D70, [gate14[0]], [(gate14[0] + 4, b8(1)), (gate14[0] + 0xD, b8(kind))])
        add('823D70 kind %X s0' % kind, beat, 0x823D70, [gate14[0]],
            [(gate14[0] + 4, b8(0)), (gate14[0] + 0xD, b8(kind)), (F761, b8(1))])
    for x in (-270.0, -270.00003, -269.99997, -280.0, 170.0, 170.00002, 169.99998, 185.0, 0.0, -0.0, 1e-39,
              3.4028235e38, -3.4028235e38):
        for e1 in (0, 4, 8, 0xFF, 0xFB, 0xF7):
            for step in (0, 1, 2):
                add('823D70 k14 x %g E%02X step %d' % (x, e1, step), beat, 0x823D70, [gate14[0]],
                    [(gate14[0] + 4, b8(1)), (gate14[0] + 0xB0, f32b(x)), (E1, b8(e1)), (gate14[0] + 5, b8(step)),
                     (gate14[0] + 0x28, h16(3))])
    for count in (0, 1, 2, 5, 0x8000, 0xFFFF, 0x10):
        add('823D70 k14 count %X' % count, beat, 0x823D70, [gate14[0]],
            [(gate14[0] + 4, b8(1)), (gate14[0] + 0xB0, f32b(185.0)), (gate14[0] + 5, b8(1)),
             (gate14[0] + 0x28, h16(count))])
    for kind in (0xF, 0x10):
        node = gates[kind]
        for step in (0, 1, 2, 0xFF):
            for e1 in (0, 4, 8, 0xC, 0xF3, 0xFB, 0xF7):
                add('823D70 k%X step %d E%02X' % (kind, step, e1), beat, 0x823D70, [node],
                    [(node + 4, b8(1)), (node + 5, b8(step)), (E1, b8(e1))])
    add('823D70 k14 scribble', beat, 0x823D70, [gate14[0]],
        [(gate14[0] + 4, b8(1)), (gate14[0] + 0xB0, f32b(-300.0))],
        queues={0x1C6380: [Scribble(0, [(gate14[0] + 0x4C, w32(0x1234560))])]})
    add('823D70 k14 far scribble', beat, 0x823D70, [gate14[0]],
        [(gate14[0] + 4, b8(1)), (gate14[0] + 0xB0, f32b(185.0)), (gate14[0] + 5, b8(0))],
        queues={0x1B1B70: [Scribble(0, [(gate14[0] + 0x4C, w32(0x2345670))])]})
    add('823D70 s0 scribble', beat, 0x823D70, [gates[0xF]], [(gates[0xF] + 4, b8(0)), (F761, b8(0))],
        queues={0x1B0FD0: [Scribble(0, [(gates[0xF] + 4, b8(9))])]})

    # ---- 0x824020 the kind-4 owners
    for node in (car, k8, k10, k11):
        for state in (0, 2, 3, 4, 0xFF):
            for flag in ((0, 1, 0xFF) if state == 0 else (0,)):
                add('824020 %X s%d %02X' % (node, state, flag), beat, 0x824020, owner_args(0x824020, node),
                    [(node + 4, b8(state)), (F761, b8(flag))])
    for kind in (0, 7, 8, 9, 0xC, 0xFF):
        for count, cap in ((0, 0), (3, 3), (4, 3), (9, 0x340), (0x100, 5), (0xFF, -1), (0x80, 0x7FFF), (0x1FF, 0x1FE)):
            add('824020 s0 kind %X count %X cap %d' % (kind, count, cap), beat, 0x824020, owner_args(0x824020, car),
                [(car + 4, b8(0)), (F761, b8(0)), (car + 0xD, b8(kind)), (0x275BCC, h16(cap))],
                queues={0x1C6150: [count], 0x1AF780: [0x7E0000 + 0x10 * i for i in range(256)]})
    for node in (car, k8):
        for step in (0, 1, 2, 0xFF):
            for e1 in (0, 2, 0xFD, 0xFF):
                add('824020 %X s1 step %d E%02X' % (node, step, e1), beat, 0x824020, owner_args(0x824020, node),
                    [(node + 4, b8(1)), (node + 5, b8(step)), (E1, b8(e1))])
    for six, result in ((7, 1), (6, 0), (3, 1)):
        for node in (car, k8):
            add('824020 %X s1 step1 six %d' % (node, six), beat, 0x824020, owner_args(0x824020, node),
                [(node + 4, b8(1)), (node + 5, b8(1)), (node + 6, b8(six))], queues={0x1BA1F0: [result]})
    for node, bit in ((k10, 0x80), (k11, 0x40)):
        for step in (0, 1, 2, 0xFF):
            for e1 in (0, bit, 0xFF ^ bit, 0xFF):
                add('824020 %X s1 step %d E%02X' % (node, step, e1), beat, 0x824020, owner_args(0x824020, node),
                    [(node + 4, b8(1)), (node + 5, b8(step)), (E1, b8(e1))])
    add('824020 k10 scribble kind', beat, 0x824020, owner_args(0x824020, k10),
        [(k10 + 4, b8(1)), (k10 + 5, b8(0)), (E1, b8(0xFF))],
        queues={0x1B6660: [Scribble(0, [(k10 + 0xD, b8(0xB)), (k10 + 5, b8(0))])]})
    add('824020 k7 scribble kind', beat, 0x824020, owner_args(0x824020, car),
        [(car + 4, b8(1)), (car + 5, b8(1)), (car + 6, b8(2))],
        queues={0x1AA700: [Scribble(0, [(car + 0xD, b8(0xA)), (car + 5, b8(1))])]})
    add('824020 s0 761 scribble', beat, 0x824020, owner_args(0x824020, k11),
        [(k11 + 4, b8(0)), (F761, b8(1))], queues={0x1B0FD0: [Scribble(0, [(k11 + 0xD, b8(7))])]})
    add('824020 s0 memset scribble', beat, 0x824020, owner_args(0x824020, k10), [(k10 + 4, b8(0)), (F761, b8(0))],
        queues={0x1B0FD0: [Scribble(0, [(k10 + 0x1F0, w32(7))])]}, run_set=RUN)

    # ---- 0x824800 model setup
    for node in (car, k8, k10, switch):
        for count, cap in ((0, 0), (1, 1), (2, 1), (9, 0x340), (0x100, 5), (0xFF, -1), (0xFF, 0xFF), (0x80, 0x7FFF),
                           (-1, 0x100), (0x1FF, 0xFF)):
            add('824800 %X count %X cap %d' % (node, count & MASK, cap), beat, 0x824800, [node],
                [(0x275BCC, h16(cap))], queues={0x1C6150: [count], 0x1C6120: [0x1600000 + count],
                                              0x1AF780: [0x7E0000 + 0x10 * i for i in range(256)]})
    add('824800 scribble count', beat, 0x824800, [car], [],
        queues={0x1C6150: [5], 0x1AF780: [0x100, Scribble(0x200, [(car + 0xC, b8(2))]), 0x300, 0x400]})
    add('824800 scribble table', beat, 0x824800, [car], [],
        queues={0x1CA6E0: [Scribble(0, [(0x28A6E8, w32(0x1234)), (car + 0x44, w32(0x5678))])],
                0x1C6150: [Scribble(3, [(car + 0xD, b8(8)), (0x275BCC, h16(2))])]})
    add('824800 scribble kind', beat, 0x824800, [car], [],
        queues={0x1C6150: [1], 0x1CB5B0: [Scribble(0, [(car + 0xD, b8(8)), (car + 0xC, b8(0x33))])]})

    # ---- 0x8242F0 one step of kinds 7 / 8
    for six in range(10):
        for result in ((0, 1) if 1 <= six <= 5 else (0,)):
            add('8242F0 k7 six %d %d' % (six, result), beat, 0x8242F0, owner_args(0x8242F0, car),
                [(car + 6, b8(six)), (car + 7, b8(3))], queues={0x1BA1F0: [result]})
    for six in (0xFF, 0x80):
        add('8242F0 k7 six %X' % six, beat, 0x8242F0, owner_args(0x8242F0, car), [(car + 6, b8(six))])
    for seven in (0, 1, 2, 3, 0xFF):
        for x in (-280.0, -279.99997, -280.00003, 140.0, 140.00002, 139.99998, 0.0, -0.0, 3.4028235e38, -300.0):
            add('8242F0 k7 seven %d x %g' % (seven, x), beat, 0x8242F0, owner_args(0x8242F0, car),
                [(car + 6, b8(6)), (car + 7, b8(seven)), (car + 0xB0, f32b(x)), (E1, b8(0x33))])
    for e1 in (0x00, 0xFE, 0x7B, 0xF7, 0x7F):   # every bit the steps set, already set and clear
        add('8242F0 k7 six 6 E%02X' % e1, beat, 0x8242F0, owner_args(0x8242F0, car),
            [(car + 6, b8(6)), (car + 7, b8(3)), (E1, b8(e1))])
        add('8242F0 k7 seven 0 E%02X' % e1, beat, 0x8242F0, owner_args(0x8242F0, car),
            [(car + 6, b8(1)), (car + 7, b8(0)), (car + 0xB0, f32b(0.0)), (E1, b8(e1))])
        add('8242F0 k7 seven 1 E%02X' % e1, beat, 0x8242F0, owner_args(0x8242F0, car),
            [(car + 6, b8(1)), (car + 7, b8(1)), (car + 0xB0, f32b(150.0)), (E1, b8(e1))])
        add('8242F0 k8 six 2 E%02X' % e1, beat, 0x8242F0, owner_args(0x8242F0, k8), [(k8 + 6, b8(2)), (E1, b8(e1))])
    for count in (0, 1, 0x7FFFFFFF, -1):
        add('8242F0 k7 seven 2 count %d' % count, beat, 0x8242F0, owner_args(0x8242F0, car),
            [(car + 6, b8(6)), (car + 7, b8(2)), (car + 0x2E0, w32(count))])
    for c28, c2a in ((0x7FFF, 0xFFFF), (0xFFFF, 0x7FFF), (0x8000, 0)):
        add('8242F0 k7 counters %X %X' % (c28, c2a), beat, 0x8242F0, owner_args(0x8242F0, car),
            [(car + 0x28, h16(c28)), (car + 0x2A, h16(c2a))])
    for six in range(6):
        for result in ((0, 1, 2) if six == 1 else (0,)):
            add('8242F0 k8 six %d %d' % (six, result), beat, 0x8242F0, owner_args(0x8242F0, k8),
                [(k8 + 6, b8(six)), (E1, b8(0x33))], queues={0x1BA1F0: [result]})
    for kind in (0, 6, 9, 0xFF):
        add('8242F0 kind %X' % kind, beat, 0x8242F0, owner_args(0x8242F0, car), [(car + 0xD, b8(kind))])
    # (no huge angles: 001B12B0's wrap 001B1470 would loop without end on the
    # original too, the step of 2 pi being below one ulp there)
    for yaw in (0.0, -0.0, 3.0, -3.0, 0.3141, 0.31416, 1e-39, 9.5, -9.5):
        add('8242F0 k7 six 1 yaw %g' % yaw, beat, 0x8242F0, owner_args(0x8242F0, car),
            [(car + 6, b8(1)), (car + 0xC4, f32b(yaw))])
        add('8242F0 k8 six 1 yaw %g' % yaw, beat, 0x8242F0, owner_args(0x8242F0, k8),
            [(k8 + 6, b8(1)), (k8 + 0xC4, f32b(yaw))])
    add('8242F0 k7 scribble kind after callback', beat, 0x8242F0, owner_args(0x8242F0, car), [(car + 6, b8(3))],
        queues={0x1C68C0: [Scribble(0, [(car + 0xD, b8(8)), (car + 0x4C, w32(0x1234560))])]})
    add('8242F0 k8 scribble kind after callback', beat, 0x8242F0, owner_args(0x8242F0, k8), [(k8 + 6, b8(3))],
        queues={0x1C68C0: [Scribble(0, [(k8 + 0xD, b8(7))])]})
    add('8242F0 k7 scribble bone', beat, 0x8242F0, owner_args(0x8242F0, car), [(car + 6, b8(3))],
        queues={0x1B1B70: [Scribble(0, [(car + 0x110, w32(u32(ram, k8 + 0x110)))])]})
    add('8242F0 k7 scribble seven', beat, 0x8242F0, owner_args(0x8242F0, car),
        [(car + 6, b8(5)), (car + 7, b8(0)), (car + 0xB0, f32b(0.0))],
        queues={0x1BA1F0: [Scribble(1, [(car + 7, b8(1)), (car + 0xB0, f32b(150.0))])]})
    add('8242F0 k7 scribble E1', beat, 0x8242F0, owner_args(0x8242F0, car),
        [(car + 6, b8(6)), (car + 7, b8(1)), (car + 0xB0, f32b(150.0))],
        queues={0x1F6B30: [Scribble(0, [(E1, b8(0x11)), (car + 0xB0, f32b(-500.0))])]})
    add('8242F0 k8 scribble counter', beat, 0x8242F0, owner_args(0x8242F0, k8), [(k8 + 6, b8(1))],
        queues={0x1BA1F0: [Scribble(1, [(k8 + 0x2E0, w32(77)), (k8 + 0xC4, f32b(2.0))])]})

    # ---- 0x824910 the trigger records (the car's x against each record's x)
    trig = 0x827350
    for x in (-500.0, -24.6, -24.599998, -24.600002, 0.0, 100.0, 118.4, 118.40001, 300.0, 3.4028235e38):
        add('824910 x %g' % x, beat, 0x824910, owner_args(0x824910, car), [(car + 0xB0, f32b(x))])
    rnd = [0, 1, 2, -1, -2, 3, -0x80000000, 0x7FFFFFFF, 5, 4, -3, 6, 7, 8, 9, 10, 11, 12]
    add('824910 all fire, scripted rand', beat, 0x824910, owner_args(0x824910, car), [(car + 0xB0, f32b(1000.0))],
        queues={0x122BB8: list(rnd)}, run_set=RUN - {0x122BB8})
    ids = [0x80000001, 0x80000002, 0x80000003, 0, 0x7FFFFFFF, 0x80000001]
    add('824910 ids', beat, 0x824910, owner_args(0x824910, car),
        [(car + 0xB0, f32b(1000.0))] + [(trig + 0x20 * i, w32(ids[i % len(ids)])) for i in range(18)]
        + [(trig + 0x20 * i + 4, w32([0, 1, 0, 2, 0, 0xFFFFFFFF][i % 6])) for i in range(18)])
    add('824910 small points', beat, 0x824910, owner_args(0x824910, car),
        [(car + 0xB0, f32b(1000.0)), (trig + 0x14, f32b(1e-7)), (trig + 0x20 * 7 + 0x14, f32b(-10.0)),
         (trig + 0x20 * 8 + 0x14, f32b(3e-7)), (trig + 0x20 * 1 + 0x14, f32b(-1e-39))])
    add('824910 scribble records', beat, 0x824910, owner_args(0x824910, car), [(car + 0xB0, f32b(1000.0))],
        queues={0x1EFD20: [Scribble(0, [(trig + 0x20 * 1 + 4, w32(1)), (trig + 0, w32(0x80000001)),
                                        (trig + 0x10, f32b(-30.0)), (car + 0xB0, f32b(120.0))])]})
    add('824910 scribble id', beat, 0x824910, owner_args(0x824910, car),
        [(car + 0xB0, f32b(1000.0)), (trig, w32(0x80000001))],
        queues={0x1EFD20: [Scribble(0, [(trig, w32(0x80000002)), (trig + 0x14, f32b(7.0))]),
                           Scribble(0, [(trig, w32(0x1234))])]})

    # ---- 0x824AC0 the spawns every 8th frame
    for frame in (0, 8, 1, 7, -8, -1, -9, 0x7FFFFFF8, -0x80000000, 53086, 53088):
        add('824AC0 frame %d' % frame, beat, 0x824AC0, [car], [], [(0x70003B68, w32(frame))],
            queues={0x1EFD20: [spare, 0, spare + NODE, spare]})
    for rands in ([0, 0, 0], [79, 149, 359], [-79, -149, -359], [-0x80000000, 0x7FFFFFFF, -1], [80, 150, 360],
                  [-81, -15, -1], [0x7FFFFFFF, -0x80000000, 0x7FFFFFFF]):
        add('824AC0 rand %s' % rands, beat, 0x824AC0, [car], [], [(0x70003B68, w32(0))],
            queues={0x1EFD20: [spare, 0, 0, 0], 0x122BB8: list(rands)}, run_set=RUN - {0x122BB8})
    add('824AC0 scribble', beat, 0x824AC0, [car], [], [(0x70003B68, w32(0))],
        queues={0x1EFD20: [spare, spare], 0x1026A0: [Scribble(0, [(car + 0x110, w32(u32(ram, k8 + 0x110))),
                                                                  (spare + 0xB0, f32b(3e-7))])]},
        run_set=RUN - {0x1026A0})
    add('824AC0 spawn unmapped', beat, 0x824AC0, [car], [], [(0x70003B68, w32(0))],
        queues={0x1EFD20: [0x02400000]})

    # ---- 0x824C40 / 0x824CD0 the sprites
    for r in (0, 15, 16, 0xF0, -1, -0x80000000, 0x7FFFFFFF, 7):
        add('824C40 rand %X' % (r & MASK), beat, 0x824C40, [car + 0xB0], queues={0x122BB8: [r]},
            run_set=RUN - {0x122BB8})
    add('824C40 point spad', beat, 0x824C40, [0x700038A0])
    add('824CD0 car', beat, 0x824CD0, owner_args(0x824CD0, car))
    add('824CD0 k8', beat, 0x824CD0, owner_args(0x824CD0, k8))
    add('824CD0 scribble bone', beat, 0x824CD0, owner_args(0x824CD0, car), [],
        queues={0x1026A0: [Scribble(0, [(car + 0x110, w32(u32(ram, k8 + 0x110))), (0x827598, w32(9))])]},
        run_set=RUN - {0x1026A0})
    add('824CD0 bone unmapped', beat, 0x824CD0, owner_args(0x824CD0, car), [(car + 0x110, w32(0x02400000))])

    # ---- 0x824FA0 the examine owner
    for state in (0, 2, 3, 4, 0xFF):
        for result in ((0, 1, 2, -1) + WIDE_RESULTS if state == 0 else (0,)):
            add('824FA0 s%d %X' % (state, result & MASK), beat, 0x824FA0, [exam], [(exam + 4, b8(state))],
                queues={0x1B0FD0: [result]})
    for step in (0, 1, 2, 0xFF):
        for use in (0, 4, 0xFB):
            for talk in (0, 1, 2) + WIDE_RESULTS:
                for done in ((0, 1, 2) + WIDE_RESULTS if step == 1 and talk == 0 and use == 0 else (0,)):
                    if use and step != 0:
                        continue
                    add('824FA0 s1 step %d use %02X talk %X done %X' % (step, use, talk, done), beat, 0x824FA0,
                        [exam], [(exam + 4, b8(1)), (exam + 5, b8(step)), (exam + 0xB, b8(use))],
                        queues={0x1BA1C0: [talk], 0x1BA1F0: [done]})
    add('824FA0 scribble', beat, 0x824FA0, [exam], [(exam + 4, b8(1)), (exam + 5, b8(0)), (exam + 0xB, b8(4))],
        queues={0x1BA1C0: [Scribble(0, [(exam + 5, b8(7)), (exam + 0xB, b8(0))])],
                0x1B17A0: [Scribble(0, [(exam + 0x4C, w32(0x1234560))])]})
    add('824FA0 s0 scribble', beat, 0x824FA0, [exam], [(exam + 4, b8(0))],
        queues={0x1C6380: [Scribble(0, [(exam + 4, b8(9))])]})

    # ---- 0x825100 by +0x03
    for node in (t4, t7):
        for state in (0, 2, 3, 4, 0xFF):
            add('825100 %X s%d' % (node, state), beat, 0x825100, [node], [(node + 4, b8(state)), (F83F, b8(5))])
    for mode in (0, 5, 0x87, 0xFF):
        add('825100 mode %X' % mode, beat, 0x825100, [t7], [(t7 + 4, b8(1)), (t7 + 3, b8(mode))])
    stand = [(0x8104C4, w32(t7)), (0x8102BA, b8(1)), (PLAYER_X, f32b(20.0)), (PLAYER_Z, f32b(30.0)),
             (PLAYER_YAW, f32b(3.14))]
    for step in (0, 1, 2, 0xFF):
        for turn in (0, 1, 2, 3, 0xFF):
            for done in ((0, 1) if step == 1 and turn in (1, 2) else (0,)):
                for tests in ([0, 0], [1], [0, 1], [2], [-1, 0]):
                    if (turn, done) != (1, 0) and tests != [0, 0] and step == 1:
                        continue
                    add('825100 t7 step %d turn %d done %d tests %s' % (step, turn, done, tests), beat, 0x825100,
                        [t7], stand + [(t7 + 4, b8(1)), (t7 + 5, b8(step)), (F83F, b8(turn))],
                        queues={0x1BA1F0: [done], 0x1B18F0: list(tests)})
    add('825100 t7 turn scribble', beat, 0x825100, [t7], stand + [(t7 + 4, b8(1)), (t7 + 5, b8(1)), (F83F, b8(2))],
        queues={0x1BA1F0: [Scribble(1, [(F83F, b8(2)), (t7 + 0x38, w32(5))])],
                0x1C6380: [Scribble(0, [(t7 + 0x4C, w32(0x1234560))]), 0]})
    add('825100 t7 turn scribble 2', beat, 0x825100, [t7], stand + [(t7 + 4, b8(1)), (t7 + 5, b8(1)), (F83F, b8(1))],
        queues={0x1BA1F0: [0], 0x1A2370: [Scribble(0, [(F83F, b8(2))])]})
    for step in (0, 1, 2, 3, 0xFF):
        for use in ((0, 4, 0xFB) if step == 0 else (0,)):
            for done in ((0, 1, 2, -1) if step == 1 else (0,)):
                for turn in ((0, 1, 2, 0xFF) if step == 2 else (0,)):
                    for flag in (0, 1, 0xFF):
                        add('825100 t4 step %d use %02X done %d turn %d 761 %02X' % (step, use, done, turn, flag),
                            beat, 0x825100, [t4],
                            [(t4 + 4, b8(1)), (t4 + 5, b8(step)), (t4 + 0xB, b8(use)), (F83F, b8(turn)),
                             (F761, b8(flag))], queues={0x1BA1F0: [done]})
    for x, y, z in ((0.0, -0.0, 3.5), (1e-39, -15.0, -3.5), (-1.0, -14.999999, 1e-7), (3.4028235e38, 1.0, 2.0),
                    (0.00012, 3e-7, 0.0002)):
        add('825100 t4 pos %g %g %g' % (x, y, z), beat, 0x825100, [t4],
            [(t4 + 4, b8(1)), (t4 + 5, b8(3)), (F761, b8(0)), (t4 + 0xB0, f32b(x)), (t4 + 0xB4, f32b(y)),
             (t4 + 0xB8, f32b(z))])
    add('825100 t4 scribble', beat, 0x825100, [t4], [(t4 + 4, b8(1)), (t4 + 5, b8(2)), (F83F, b8(0)), (F761, b8(0))],
        queues={0x1F4BF0: [Scribble(0, [(t4 + 0x4C, w32(0x2345670))])]})
    add('825100 t4 scribble 761', beat, 0x825100, [t4], [(t4 + 4, b8(1)), (t4 + 5, b8(1)), (F761, b8(1))],
        queues={0x1BA1F0: [Scribble(1, [(F761, b8(0)), (t4 + 0xB0, f32b(7.0))])]})

    # ---- 0x825520 the turn about a +0x0D-9 object
    for obj, flag, kind in ((0, 1, 9), (t7, 0, 9), (t7, 1, 8), (t7, 1, 9), (t7, 0x80, 9), (0x02400000, 1, 9)):
        add('825520 obj %X flag %X kind %X' % (obj, flag, kind), beat, 0x825520, [],
            [(0x8104C4, w32(obj)), (0x8102BA, b8(flag))] + ([(t7 + 0xD, b8(kind))] if obj == t7 else []))
    for px, pz, yaw in ((20.0, 30.0, 3.14), (-0.2, 10.0, 0.0), (0.0, 0.0, 3.1415925), (100.0, -50.0, -3.1415927),
                        (1e-39, -1e-39, 3.1415927), (3.4028235e38, 1.0, -3.14), (-60.0, 58.0, 6.3),
                        (5.0, 5.0, -6.3), (-0.0, -0.0, -0.0)):
        add('825520 player %g %g yaw %g' % (px, pz, yaw), beat, 0x825520, [],
            [(0x8104C4, w32(t7)), (0x8102BA, b8(1)), (PLAYER_X, f32b(px)), (PLAYER_Z, f32b(pz)),
             (PLAYER_YAW, f32b(yaw))])
    add('825520 scribble', beat, 0x825520, [], stand,
        queues={0x11E2A8: [Scribble(0x3E800000, [(t7 + 0xB0, f32b(3.0)), (PLAYER_X, f32b(9.0))]), 0x3E800000]},
        run_set=RUN - {0x11E2A8})
    add('825520 stubbed trig', beat, 0x825520, [], stand,
        queues={0x11DE90: [0x7F800000, 0x80000001], 0x11E2A8: [0xFF7FFFFF, 0x00000001],
                0x1B1470: [Scribble(0x40000000, [(PLAYER_YAW, f32b(1.0))])]},
        run_set=RUN - {0x11DE90, 0x11E2A8, 0x1B1470})


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
    unmapped address and a fault latched before the call (on the examine
    owner, state 1 step 1: 001BA1C0, 001BA1F0, 001B17A0, the +0x4C
    callback), then every entry with a latched fault, a NULL hook table, a
    NULL fault pointer and (entries with a result) a NULL result pointer."""
    ram, spad = CAPTURES['a02_01_switch']
    node = owners(ram, 0x824FA0)[0]
    problems = []
    calls_want = ['w_001BA1C0', 'w_001BA1F0', 'w_001B17A0', 'w_callback']

    def attempt(null=None, failing=None, unmapped=None, latched=False):
        ram_c = (C.c_uint8 * RAM_SIZE).from_buffer_copy(ram)
        spad_c = (C.c_uint8 * SPAD_SIZE).from_buffer_copy(spad)
        ram_c[node + 4] = 1
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
        status = NATIVE.em_area02_ovl_00824FA0(C.byref(hooks), node, C.byref(fault))
        return status, fault.address, fault.code, calls, before == C.string_at(base, RAM_SIZE)

    status, address, code, calls, _ = attempt()
    if (status, address, code) != (0, 0, 0) or calls != calls_want:
        problems.append(('fault-check baseline', status, hex(address), code, calls))
    status, address, code, calls, _ = attempt(null='w_001BA1F0')
    if (status, address, code) != (-1, 0x1BA1F0, 1) or calls != calls_want[:1]:
        problems.append(('NULL hook', status, hex(address), code, calls))
    status, address, code, calls, same = attempt(failing='w_001BA1C0')
    if (status, address, code) != (-1, 0x1BA1C0, 2) or calls != calls_want[:1] or not same:
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
    if NATIVE.em_area02_ovl_00824FA0(None, node, C.byref(Fault())) != -1:
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
            call = [table] + ([] if kind == 'void' else [node]) + ([STACK_TOP] if kind in SP_KINDS else [])
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
    """The fail-stop contract of the header (em_area02_overlay.h), on one
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
    where each wrapper's own `a02_failed` test is observable. Returns
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
    return reference_mode.select(cases, reference_mode.pick(len(cases), fixed + len(pinned) + QUICK_RANDOM), 0xA02,
                                 keep=lambda i, c: i < fixed or i in pinned), captured, targeted, len(cases)


def main():
    global NATIVE
    elf = read_elf()
    CAPTURES.update(load_captures(elf))
    NATIVE = build(os.environ.get('EM_AREA02_OVL_SOURCE'))
    selected, captured, targeted, total = select_cases(elf)
    CASES[:] = selected
    results = run_cases(selected)
    failures, per_fn, seen, totals = [], {}, set(), [0, 0, 0, 0]
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
