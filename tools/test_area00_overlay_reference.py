#!/usr/bin/env python3
"""Execute the ORIGINAL AREA00 overlay owners and compare em_area00_overlay.c.

docs/AREA00_OVERLAY_PORT.md. The 13 AREA00 overlay functions of the AREA00
census delta (decomp build/s87/census/a00_delta.json, new_functions, region
overlay:AREA00): 0x823580, 0x823820, 0x824EA0, 0x825170, 0x8253E0,
0x825480, 0x825600, 0x825920, 0x825C80, 0x8261E0, 0x8262D0, 0x8263C0,
0x8266A0. They call no other AREA00 overlay function (every callee is a
boot function or the actor's +0x4C callback).

The harness is the design of tools/test_area01_overlay_reference.py (lane
OVL of the level-2 side track), reused here with the AREA00 captures, the
AREA00 hook table and the AREA00 cases; the generic part (Oracle, Replay,
Images, poisoning, the hook contract) is that design unchanged except for
64-bit hook arguments (001CD520's GIF tag) and the stack-pointer entries.

Oracle: the shared EE interpreter with the measured float model (FallEE,
tools/test_player_fall_reference.py) runs the original overlay code resident
in the recorded RAM images (decomp build/s87/route_a00/<beat>/eeMemory.bin +
scratchpad.bin for a00_00..a00_09, and build/s87/route_a01/a01_07_level_exit,
the AREA00 arrival; overlay id 1 at 0x823500). Before any case the test
checks that the overlay text in each image equals the user's
extract/OVERLAY/AREA00.BIN (text size from its header), that the two jump
tables equal the file and that the boot text below 0x241000 equals the
pinned ELF, so every executed instruction is original. Nothing here embeds
original bytes; reports hold counts only.

Callees (every boot function and the actor's +0x4C callback) are
intercepted at their entry and logged with their arguments. A callee either
runs as ORIGINAL code nested inside the oracle (the pure helpers in RUN:
vector and matrix copies, add/sub, matrix x vector, identity, the random
generator, float_to_int, the script start 001BA1A0 and the packet header
001CFA60), its writes recorded, or is stubbed with the case's scripted result
(everything else). The native module runs over a byte copy of the same RAM
with hooks that must be called in the same order with the same arguments;
each hook replays the original callee's writes and result.

Stack locals: 0x823820 and 0x825600 pass the address of a local of their
frame (sp - 0x60, sp - 0x10) to callees. The oracle runs with the private
stack top STACK_TOP as sp at entry, the native entry gets the same sp, and
the hook arguments are compared as addresses. Only callees read or write
those locals (001CFA60 / 001026A0 run as original code and write them;
001CFBE0 / 001F5940 are stubbed), so there is no native memory to compare
there; the private stack is outside the compared memory, as in the AREA01
harness.

Compared, per case (as in the AREA01 harness, docstring there):
  * at the ENTRY of every call, BEFORE the callee's writes are replayed:
    native RAM and scratchpad against the oracle's memory at the entry of
    the same original call (quick mode: the dirty set; EM_TEST_FULL=1: all
    32 MiB + scratchpad at every call entry); then the callee and its
    arguments (64-bit registers where the hook takes a 64-bit argument).
  * between calls: the native module's memory accesses are the original's
    own loads and stores one for one, in order, with the same address,
    size and `changed` mark. The only accesses left out are the two
    dispatchers' jump-table loads, which the C switch encodes (TABLE_BYTES).
  * after the last store: all 32 MiB + scratchpad, the op09 return value,
    and the store-log self-check. An original load or store outside the
    modelled memory (or misaligned) stops the original; the native module
    must stop with fault 5 at the same address after the same calls.
  * float hook arguments and results cross as bit patterns.
  * each helper that runs as original code is rehearsed with every argument
    register its hook does not pass poisoned.
  * every case runs twice: as given, and poisoned (poison_patches).
  * the run fails unless every reachable original word was executed.
  * the fail-stop contract of em_area00_overlay.h (hook_contract_site and
    fault_checks), and the table's ctx at every `bytes`, hook and callback
    call.

Cases:
  capture   every owner node of every image, exactly as captured (with the
            frame loop's D_00275B40 = actor + 0x110 publication), plus the
            op09 callback on the terminal's script block.
  designed  targeted_cases: every state and step, callee results 0 / 1 /
            other values at every test of a result, scribbling callees,
            story bytes and flags on both sides of every test, the float
            boundaries of every compare and clamp.
  perturbed seeded variations (EM_TEST_FULL=1 only).
Coverage: the report counts the original instruction words the oracle
executed inside each function (branch words excluded).

EM_AREA00_OVL_SOURCE=<file> tests another copy of the module source (the
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
OVERLAY_FILE = DECOMP / 'extract/OVERLAY/AREA00.BIN'
OUT = ROOT / 'build/area00/ovl'
MASK = 0xFFFFFFFF
RAM_SIZE, SPAD_SIZE = 0x2000000, 0x4000
ARENA, TEXT_END = 0x823500, 0x826F80
# The images with AREA00 resident: the end of every AREA00 beat up to a00_09
# (a00_10 and a00_s0 end back in AREA01) and the arrival (a01_07).
BEAT_DIRS = {b: DECOMP / 'build/s87/route_a00' / b for b in (
    'a00_00_descend', 'a00_01_door51_locked', 'a00_02_south_route', 'a00_03_padlock', 'a00_04_cage_terminal',
    'a00_05_ferry_deck', 'a00_06_cab_roof', 'a00_07_duct_to_ne_room', 'a00_08_switch', 'a00_09_ne_room_out')}
BEAT_DIRS['a01_07_level_exit'] = DECOMP / 'build/s87/route_a01/a01_07_level_exit'
BEATS = list(BEAT_DIRS)
POOL, NODE, NODES = 0x7A5640, 0x2F0, 400


class _Route:
    """ROUTE / beat resolves to the beat's folder (two route trees)."""

    def __truediv__(self, beat):
        return BEAT_DIRS[beat]


ROUTE = _Route()

# (entry, native symbol, byte length of the function body, kind)
FUNCS = {
    0x823580: ('em_area00_ovl_00823580', 660, 'owner'),
    0x823820: ('em_area00_ovl_00823820', 1072, 'owner_sp'),
    0x824EA0: ('em_area00_ovl_00824EA0', 720, 'owner'),
    0x825170: ('em_area00_ovl_00825170', 624, 'owner'),
    0x8253E0: ('em_area00_ovl_008253E0', 148, 'op09'),
    0x825480: ('em_area00_ovl_00825480', 372, 'owner'),
    0x825600: ('em_area00_ovl_00825600', 792, 'owner_sp'),
    0x825920: ('em_area00_ovl_00825920', 860, 'owner'),
    0x825C80: ('em_area00_ovl_00825C80', 236, 'owner'),
    0x8261E0: ('em_area00_ovl_008261E0', 236, 'owner'),
    0x8262D0: ('em_area00_ovl_008262D0', 236, 'owner'),
    0x8263C0: ('em_area00_ovl_008263C0', 724, 'owner'),
    0x8266A0: ('em_area00_ovl_008266A0', 232, 'owner'),
}
OWNER_ENTRIES = [e for e, (_, _, kind) in FUNCS.items() if kind != 'op09']


def original_args(entry, native_args):
    """The original's argument registers: the stack-pointer entries take sp
    as a native argument only (the original gets it in $sp)."""
    return native_args[:1] if FUNCS[entry][2] == 'owner_sp' else native_args


# Hooks, in the field order of EmArea00OvlHooks (after ctx and bytes).
# name -> (original address, [(ctype, register)], result kind or None)
U, I, F, Q = 'u', 'i', 'f', 'q'
HOOKS = [
    ('w_001026A0', 0x1026A0, [(U, 4), (U, 5), (U, 6)], None),
    ('w_001028B8', 0x1028B8, [(U, 4), (U, 5), (U, 6)], None),
    ('w_001028D0', 0x1028D0, [(U, 4), (U, 5), (U, 6)], None),
    ('w_00102948', 0x102948, [(U, 4), (U, 5)], None),
    ('w_00102958', 0x102958, [(U, 4), (U, 5)], None),
    ('w_001029C0', 0x1029C0, [(U, 4)], None),
    ('w_00122BB8', 0x122BB8, [], I),
    ('w_001281C0', 0x1281C0, [(F, 12)], I),
    ('w_00129780', 0x129780, [(U, 4), (U, 5), (I, 6)], I),
    ('w_00182F90', 0x182F90, [(U, 4), (U, 5)], None),
    ('w_0019C6F0', 0x19C6F0, [(I, 4), (I, 5)], I),
    ('w_001A2370', 0x1A2370, [(U, 4), (U, 5)], None),
    ('w_001AF800', 0x1AF800, [(U, 4)], None),
    ('w_001AFC10', 0x1AFC10, [(U, 4)], None),
    ('w_001B0C60', 0x1B0C60, [(I, 4), (I, 5), (I, 6)], None),
    ('w_001B0F60', 0x1B0F60, [(U, 4), (I, 5)], I),
    ('w_001B0FD0', 0x1B0FD0, [(U, 4)], I),
    ('w_001B10B0', 0x1B10B0, [(U, 4), (I, 5), (I, 6)], I),
    ('w_001B17A0', 0x1B17A0, [(U, 4)], I),
    ('w_001B1B70', 0x1B1B70, [(U, 4)], None),
    ('w_001B1E20', 0x1B1E20, [(I, 4), (I, 5)], None),
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
    ('w_001C2770', 0x1C2770, [(U, 4), (U, 5), (I, 6)], I),
    ('w_001C3D60', 0x1C3D60, [(U, 4), (U, 5)], None),
    ('w_001C4760', 0x1C4760, [(I, 4), (I, 5)], I),
    ('w_001C5570', 0x1C5570, [(U, 4), (U, 5), (I, 6), (I, 7)], None),
    ('w_001C6380', 0x1C6380, [(U, 4)], None),
    ('w_001C63E0', 0x1C63E0, [(U, 4), (I, 5)], None),
    ('w_001C64F0', 0x1C64F0, [(U, 4), (F, 12)], I),
    ('w_001C67E0', 0x1C67E0, [(U, 4), (I, 5), (F, 12), (F, 13)], None),
    ('w_001C68C0', 0x1C68C0, [(U, 4)], None),
    ('w_001C69A0', 0x1C69A0, [(U, 4)], None),
    ('w_001CB5B0', 0x1CB5B0, [(I, 4)], None),
    ('w_001CCF70', 0x1CCF70, [(U, 4)], I),
    ('w_001CD520', 0x1CD520, [(I, 4), (I, 5), (U, 6), (Q, 7), (U, 8), (F, 12), (F, 13), (F, 14)], I),
    ('w_001CFA60', 0x1CFA60, [(U, 4), (U, 5), (F, 12), (F, 13)], None),
    ('w_001CFBE0', 0x1CFBE0, [(I, 4), (I, 5), (U, 6), (U, 7), (I, 8)], None),
    ('w_001EFD20', 0x1EFD20, [(I, 4), (U, 5)], None),
    ('w_001F4E20', 0x1F4E20, [(U, 4), (U, 5), (F, 12)], None),
    ('w_001F5940', 0x1F5940, [(I, 4), (U, 5), (I, 6)], None),
    ('w_001FB9F0', 0x1FB9F0, [(I, 4), (I, 5), (I, 6), (I, 7)], None),
    ('w_001FC3C0', 0x1FC3C0, [(U, 4), (U, 5), (I, 6), (F, 12), (F, 13)], None),
]
BY_ADDRESS = {address: (name, args, result) for name, address, args, result in HOOKS}

# Callees that run as original code inside the oracle (writes replayed to
# the native side). Pure helpers only: none of them reaches hardware.
RUN = {0x1026A0, 0x1028B8, 0x1028D0, 0x102948, 0x102958, 0x1029C0, 0x122BB8, 0x1281C0, 0x1BA1A0, 0x1CFA60}

# The two dispatchers' step tables (runtime addresses), keyed by the
# runtime address of their jump.
JUMP_TABLES = {0x823640: (0x82D400, 8), 0x825218: (0x82D420, 7)}
# The only bytes the original loads that the translation does not: those
# tables, which the C switches encode (load_captures asserts they equal
# AREA00.BIN in every image). Replay.check_access leaves them out.
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
    sweep) into a private library under build/area00/ovl."""
    OUT.mkdir(parents=True, exist_ok=True)
    source = source or 'src/game/em_area00_overlay.c'
    stem = 'area00_overlay' if source == 'src/game/em_area00_overlay.c' else 'area00_overlay_%d' % os.getpid()
    lib = OUT / (stem + ('.dylib' if sys.platform == 'darwin' else '.so'))
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc', '-I' + str(ROOT / 'src/game'), str(source),
                    '-lm', '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    for entry, (symbol, _, kind) in FUNCS.items():
        fn = getattr(native, symbol)
        if kind == 'op09':
            fn.argtypes = [C.POINTER(Hooks), C.c_uint32, C.c_uint32, C.c_uint32,
                           C.POINTER(C.c_int32), C.POINTER(Fault)]
        elif kind == 'owner_sp':
            fn.argtypes = [C.POINTER(Hooks), C.c_uint32, C.c_uint32, C.POINTER(Fault)]
        else:
            fn.argtypes = [C.POINTER(Hooks), C.c_uint32, C.POINTER(Fault)]
        fn.restype = C.c_int
    return native


def load_captures(elf):
    """Every image must hold the AREA00 overlay (header id 1) with its text
    equal to the user's AREA00.BIN, the two jump tables equal to the file,
    and the boot text equal to the pinned ELF: every instruction the oracle
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
        assert ram[ARENA:ARENA + 8] == overlay[:8] and ram[ARENA + 4] == 1, (beat, 'AREA00 overlay not resident')
        assert ram[ARENA + 0x40:TEXT_END] == overlay[0x40:TEXT_END - ARENA], (beat, 'overlay text differs from AREA00.BIN')
        assert ram[0x100000:0x241000] == elf[0x300:0x300 + 0x141000], (beat, 'boot text differs from the ELF')
        for table, count in JUMP_TABLES.values():
            assert ram[table:table + 4 * count] == overlay[table - ARENA:table - ARENA + 4 * count], \
                (beat, 'jump table differs from AREA00.BIN')
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
    RAM below 0x40000000) or the scratchpad address; None elsewhere (the
    oracle's private stack)."""
    address &= MASK
    if 0x70000000 <= address < 0x70000000 + SPAD_SIZE:
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

    def __init__(self, ram, spad, entry, args, script, run_set, seen, own=False):
        ee = self.ee = Coverage(None, ram, spad, own)
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
        test compares only the stop (docs/AREA00_OVERLAY_PORT.md section 4). A
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
        """The module uses the bytes of a `bytes` request (a00_at) before
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
        (a00_at in em_area00_overlay_internal.h), and replayed callee
        writes are oracle stores, so every other byte still holds the start
        image on both sides. EM_TEST_FULL=1 compares everything at every
        call entry as well; the final comparison is always full."""
        if full is None:
            full = reference_mode.FULL
        for key, data in self.stores[self.applied:upto]:
            if key >= 0x70000000:
                C.memmove(self.expect_spad_base + key - 0x70000000, data, len(data))
            else:
                C.memmove(self.expect_ram_base + key, data, len(data))
            if len(data) > self.dirty.get(key, 0):
                self.dirty[key] = len(data)
        self.applied = upto
        bad_ram = bad_spad = None
        if full:
            if LIBC.memcmp(self.expect_ram_base, self.ram_base, RAM_SIZE):
                bad_ram = first_difference_at(self.expect_ram_base, self.ram_base, RAM_SIZE)
            if LIBC.memcmp(self.expect_spad_base, self.spad_base, SPAD_SIZE):
                bad_spad = first_difference_at(self.expect_spad_base, self.spad_base, SPAD_SIZE)
        else:
            for key, size in self.dirty.items():
                if key >= 0x70000000:
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
        return bad_ram is None and bad_spad is None

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
            if address >= 0x70000000:
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
            self._mark(self.stale, key, len(data))
        for key, size in replay.dirty.items():
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
        if kind == 'op09' and s32(oracle.ee.r[2]) != result:
            errors.append(('return', s32(oracle.ee.r[2]), result))
    return errors


def native_call(native, ram, spad, entry, args, oracle, null=None, inject=None, refuse_at=None, buffers=None):
    """One native run over the start image `ram` / `spad` against `oracle`
    (Replay). `null` leaves one hook-table field NULL, `inject` scripts
    hook statuses and `refuse_at` makes `bytes` refuse that request
    (hook_contract_site); `buffers` are already loaded with the start
    image. Returns (replay, status, fault, op09 result)."""
    replay = Replay(oracle, buffers or load_buffers(ram, spad))
    replay.inject = inject or {}
    replay.refuse_at = refuse_at
    hooks = replay.hooks(null)
    fault = Fault()
    symbol, _, kind = FUNCS[entry]
    fn = getattr(native, symbol)
    result = C.c_int32(0x5A5A5A5A)
    if kind == 'op09':
        status = fn(C.byref(hooks), *args, C.byref(result), C.byref(fault))
    elif kind == 'owner_sp':
        status = fn(C.byref(hooks), args[0], args[1], C.byref(fault))
    else:
        status = fn(C.byref(hooks), args[0], C.byref(fault))
    return replay, status, fault, result.value


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
    spans = [(key, key + len(data)) for key, data in replay.stores]
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
    cost = lambda i: 3 if selected[i][2] == 0x823820 else 1   # noqa: E731
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


def run_one(entry, native_args, ram, spad, script, run_set, seen, images=None):
    """Oracle then native on the same start image; returns (errors, oracle).
    With `images` (Images), ram / spad are its start image and the oracle
    and native sides run on its prepared copies."""
    self_node = native_args[0]
    callbacks = {u32(ram, self_node + 0x4C)} | SCRIBBLED_CALLBACKS
    try:   # the oracle works on its own copy; ram / spad stay the start image
        if images is None:
            oracle = Oracle(ram, spad, entry, (native_args, callbacks), script, run_set, seen)
        else:
            oracle = Oracle(images.oracle_ram, images.oracle_spad, entry, (native_args, callbacks), script, run_set,
                            seen, own=True)
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
        more, second = run_one(entry, native_args, ram_p, spad_p, script_copy, run_set, seen, IMAGES)
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


def world_patch(beat, node):
    """The frame loop publishes the current actor's bone table before its
    behaviour runs: D_00275B40 = actor + 0x110 (the pool loop 001AFD70 calls
    001CB590 before each behaviour; docs/AREA01_OVERLAY.md "Facts this lane
    measured"). Only 0x825600 (kind 7) reads it here."""
    return [(0x275B40, w32(node + 0x110))]


def any_owner(ram, callback):
    """Pool nodes whose +0x10 behaviour is `callback`, live or not."""
    return [POOL + i * NODE for i in range(NODES) if u32(ram, POOL + i * NODE + 0x10) == callback]


def op09_record(ram):
    """The script record that names 0x8253E0 (its +0x04 word), searched in
    the overlay's data; None when not found."""
    for at in range(TEXT_END, ARENA + 0xA000, 4):
        if u32(ram, at) == 0x8253E0:
            return at - 4
    return None


F_VALUES = [0.0, -0.0, 1e-39, -1e-39, 0.25, -0.5, 1.0, 0.3, 0.29999998, 0.30000001, 2.0, 1.9999999, 2.0000002,
            1.91, 1.5, 1.49, 1.4999999, 110.0, 110.00001, -60.0, -48.0, 155.0, -1590.0, 3.4028235e38]


def rf(rng):
    pick = rng.random()
    if pick < 0.4:
        return rng.choice(F_VALUES)
    if pick < 0.8:
        return rng.uniform(-3.5, 3.5)
    return rng.uniform(-2000.0, 2000.0)


def fb(value):
    return fbits(value)


def owner_args(entry, node):
    return [node, STACK_TOP] if FUNCS[entry][2] == 'owner_sp' else [node]


def case_list(elf):
    rng = random.Random(0xA00)
    cases = []
    count = reference_mode.pick(60, 0)

    def add(label, beat, entry, args, patches=(), spad=(), queues=None, default=None, run_set=RUN):
        cases.append((label, beat, entry, tuple(args), list(patches), list(spad),
                      Script(queues, default), frozenset(run_set)))

    for beat in BEATS:
        ram, spad = CAPTURES[beat]
        for entry in OWNER_ENTRIES:
            nodes = owners(ram, entry) or ([n for n in any_owner(ram, entry)] if entry == 0x824EA0 else [])
            for node in nodes:
                add('capture %s %06X @%X' % (beat, entry, node), beat, entry, owner_args(entry, node),
                    world_patch(beat, node))
        terminal = owners(ram, 0x825480)
        record = op09_record(ram)
        if terminal and record:
            add('capture %s 8253E0 @%X' % (beat, terminal[0]), beat, 0x8253E0,
                [terminal[0], terminal[0] + 0x1F0, record])
    captured = len(cases)
    targeted_cases(add)
    targeted = len(cases) - captured

    # ---------------------------------------------------------------- perturbed
    for i in range(count):
        beat = rng.choice(BEATS[:10])
        ram, _ = CAPTURES[beat]
        for entry in OWNER_ENTRIES:
            for node in owners(ram, entry):
                p = world_patch(beat, node) + [
                    (node + 4, b8(rng.choice([0, 1, 1, 1, 2, 3, 4, 0xFF]))),
                    (node + 5, b8(rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 8]))),
                    (node + 0xB, b8(rng.choice([0, 4, 0xFB, 0xFF]))),
                    (node + 2, b8(rng.choice([0, 0x80, 0x7F]))),
                    (0x81075A, b8(rng.choice([0, 1, 0xFF]))), (0x81075B, b8(rng.choice([0, 1, 0xFF]))),
                    (0x81075D, b8(rng.choice([0, 0xFF]))), (0x81075E, b8(rng.choice([0, 0xFF]))),
                    (0x810702, b8(rng.choice([0, 4, 5, 6, 7]))), (0x8107DC, b8(rng.randrange(4))),
                    (0x8104E6, b8(rng.choice([0, 1]))), (0x810701, b8(rng.choice([0, 1])))]
                q = {a: [rng.choice([0, 1, 2, -1]) for _ in range(3)]
                     for a in (0x1BBE40, 0x1BC0E0, 0x1BC290, 0x1BA1F0, 0x1B0FD0, 0x1B0F60, 0x1B17A0)}
                add('perturbed %06X %d' % (entry, i), beat, entry, owner_args(entry, node), p, queues=q)
    return cases, captured, targeted


def targeted_cases(add):
    """Designed cases for every state, step and test; all run by default."""
    beat = 'a00_04_cage_terminal'
    ram, _ = CAPTURES[beat]

    def one(callback):
        return owners(ram, callback)[0]
    door = one(0x823580)
    door51 = one(0x825170)
    terminal = one(0x825480)
    ferry, rider, low = sorted(owners(ram, 0x825600))
    s43 = [n for n in owners(ram, 0x825920) if ram[n + 2] & 0x80]
    plain = [n for n in owners(ram, 0x825920) if not ram[n + 2] & 0x80]
    cab = one(0x8263C0)
    beam = one(0x8266A0)
    spare = free_node(ram)
    assert ram[ferry + 0xD] == 2 and ram[rider + 0xD] == 0x15 and ram[low + 0xD] == 7, 'ferry group layout'
    assert s43 and plain, '0x825920 nodes'
    s43, plain = s43[0], plain[0]

    def w(node):
        return world_patch(beat, node)

    # ---- 0x823580 the shaft door
    for state, flag in ((0, 0), (0, 1), (0, 0xFF), (2, 0), (3, 0), (4, 0), (0xFF, 0)):
        add('door52 s%d %02X' % (state, flag), beat, 0x823580, [door],
            w(door) + [(door + 4, b8(state)), (0x81075A, b8(flag))])
    for e, d in ((0xFF, 0), (0xFF, 0xFF), (0, 0xFF), (0xFE, 0xFF), (0, 0), (0, 0xFE)):
        for result in (0, 1, 2, -1):
            add('door52 step0 %02X %02X %d' % (e, d, result), beat, 0x823580, [door],
                w(door) + [(door + 4, b8(1)), (door + 5, b8(0)), (0x81075E, b8(e)), (0x81075D, b8(d))],
                queues={0x1BBE40: [result], 0x19C6F0: [result, 3]})
    for step in range(1, 10):
        for result in (0, 1, 2, -1):
            for use in (0, 4, 0xFB):
                if use and step != 6:
                    continue
                add('door52 step%d %d %02X' % (step, result, use), beat, 0x823580, [door],
                    w(door) + [(door + 4, b8(1)), (door + 5, b8(step)), (door + 0xB, b8(use))],
                    queues={a: [result] for a in (0x1BBE40, 0x1BC0E0, 0x1BC290, 0x1BA1F0)})
    for step in (1, 2, 3, 7):   # the step is read again after the call
        add('door52 step%d scribble' % step, beat, 0x823580, [door],
            w(door) + [(door + 4, b8(1)), (door + 5, b8(step))],
            queues={a: [Scribble(1, [(door + 5, b8(0x40 + step))])]
                    for a in (0x1BBE40, 0x1BC0E0, 0x1BC240, 0x1BA1F0, 0x1B0C60)})
    add('door52 step6 +5 at dispatch', beat, 0x823580, [door],
        w(door) + [(door + 4, b8(1)), (door + 5, b8(6)), (door + 0xB, b8(4))])
    add('door52 s0 scribble', beat, 0x823580, [door], w(door) + [(door + 4, b8(0)), (0x81075A, b8(0))],
        queues={0x1BBDA0: [Scribble(0, [(0x81075A, b8(0xFF))])]})

    # ---- 0x825170 door [51]
    for state, flag in ((0, 0), (0, 1), (0, 0xFF), (2, 0), (3, 0), (4, 0), (0x80, 0)):
        add('door51 s%d %02X' % (state, flag), beat, 0x825170, [door51],
            w(door51) + [(door51 + 4, b8(state)), (0x81075B, b8(flag))])
    for entry in (5, 4, 6, 0x85):
        for flag in (0, 1, 0xFF):
            for use in (0, 4, 0xFB):
                for result in (0, 1, 2):
                    if entry != 5 and flag != 0xFF and result:
                        continue
                    add('door51 step0 e%d %02X %02X %d' % (entry, flag, use, result), beat, 0x825170, [door51],
                        w(door51) + [(door51 + 4, b8(1)), (door51 + 5, b8(0)), (0x810702, b8(entry)),
                                     (0x81075B, b8(flag)), (door51 + 0xB, b8(use))],
                        queues={0x1BBE40: [result]})
    for step in range(1, 9):
        for result in (0, 1, 2, -1):
            for flag in ((0, 1, 2, 0xFF) if step == 6 else (0,)):
                add('door51 step%d %d %02X' % (step, result, flag), beat, 0x825170, [door51],
                    w(door51) + [(door51 + 4, b8(1)), (door51 + 5, b8(step)), (0x81075B, b8(flag))],
                    queues={a: [result] for a in (0x1BC0E0, 0x1BC290, 0x1BA1F0, 0x1C4760)})
    for step in (1, 3, 4):
        add('door51 step%d scribble' % step, beat, 0x825170, [door51],
            w(door51) + [(door51 + 4, b8(1)), (door51 + 5, b8(step))],
            queues={a: [Scribble(1, [(door51 + 5, b8(0x30 + step))])] for a in (0x1BC0E0, 0x1BC240)})
    add('door51 step6 scribble', beat, 0x825170, [door51],
        w(door51) + [(door51 + 4, b8(1)), (door51 + 5, b8(6)), (0x81075B, b8(0))],
        queues={0x1BA1F0: [Scribble(1, [(0x81075B, b8(0))])]})
    add('door51 step0 e5 scribble', beat, 0x825170, [door51],
        w(door51) + [(door51 + 4, b8(1)), (door51 + 5, b8(0)), (0x810702, b8(5))],
        queues={0x1BBE40: [Scribble(1, [(0x81075B, b8(0x11))])]})

    # ---- 0x8253E0 op09 callback (the terminal's script block)
    block = terminal + 0x1F0
    for phase in (0, 1, 2, 0xFF):
        for count in (0, 0x0E, 0x0F, 0x10, 0x1F, 0x2F, 0x30, 0x31, 0x7F, 0xFE, 0xFF):
            if phase not in (0, 1) and count:
                continue
            add('8253E0 %d %02X' % (phase, count), beat, 0x8253E0, [terminal, block, 0],
                [(block + 4, b8(phase)), (block + 5, b8(count))])
    add('8253E0 scribble', beat, 0x8253E0, [terminal, block, 0], [(block + 4, b8(1)), (block + 5, b8(0x0F))],
        queues={0x1FB9F0: [Scribble(0, [(block + 5, b8(0x40))])]})

    # ---- 0x825480 the terminal
    for state in (0, 2, 3, 4, 0xFF):
        for result in ((0, 1, 2) if state == 0 else (0,)):
            add('terminal s%d %d' % (state, result), beat, 0x825480, [terminal],
                w(terminal) + [(terminal + 4, b8(state))], queues={0x1B0FD0: [result]})
    for step in (0, 1, 2, 0xFF):
        for use in (0, 4, 0xFB):
            for result in (0, 1, 2):
                for bits_ in ((0, 1, 0xFE) if step == 1 and result else (0,)):
                    add('terminal s1 step%d %02X %d %02X' % (step, use, result, bits_), beat, 0x825480, [terminal],
                        w(terminal) + [(terminal + 4, b8(1)), (terminal + 5, b8(step)), (terminal + 0xB, b8(use)),
                                       (0x8107DC, b8(bits_))],
                        queues={0x1BA1F0: [result]})
    add('terminal s1 scribble', beat, 0x825480, [terminal], w(terminal) + [(terminal + 4, b8(1)), (terminal + 5, b8(1))],
        queues={0x1BA1F0: [Scribble(1, [(0x8107DC, b8(0x10))])], 0x1B17A0: [Scribble(0, [(terminal + 0x4C, w32(0x1234560))])]})

    # ---- 0x825600 the ferry group
    for node in (ferry, rider, low):
        for state in (0, 2, 3, 4, 0xFF):
            for result in ((0, 1) if state == 0 else (0,)):
                for bits_ in ((0, 1, 2, 3) if state == 0 and node == ferry and not result else (0,)):
                    add('ferry %X s%d %d %d' % (node, state, result, bits_), beat, 0x825600, owner_args(0x825600, node),
                        w(node) + [(node + 4, b8(state)), (0x8107DC, b8(bits_))], queues={0x1B0FD0: [result]})
    add('ferry kind other s0', beat, 0x825600, owner_args(0x825600, rider),
        w(rider) + [(rider + 4, b8(0)), (rider + 0xD, b8(0x16))])
    for step in (0, 1, 2, 0xFF):
        for bits_ in (0, 1, 2, 3):
            for result in ((0, 1, 2) if step == 1 else (0,)):
                if step == 1 and bits_:
                    continue
                add('ferry s1 step%d %d %d' % (step, bits_, result), beat, 0x825600, owner_args(0x825600, ferry),
                    w(ferry) + [(ferry + 4, b8(1)), (ferry + 5, b8(step)), (0x8107DC, b8(bits_))],
                    queues={0x1BA1F0: [result]})
    add('ferry s1 step0 scribble', beat, 0x825600, owner_args(0x825600, ferry),
        w(ferry) + [(ferry + 4, b8(1)), (ferry + 5, b8(1))],
        queues={0x1BA1F0: [Scribble(1, [(ferry + 0x4C, w32(0x1234560))])],
                0x1B1B70: [Scribble(0, [(ferry + 0x4C, w32(0x2345670))])]})
    link = u32(ram, low + 0x18)
    top = u32(ram, link + 0x18)
    # small y too: there the operand pre-trim keeps the low bits of the constant 25
    for y in (110.0, 110.00001, 109.99999, -85.0, 0.0, -0.0, 25.0, 1e-39, 3.4028235e38, -3.4028235e38,
              0.0011, -0.0004, 0.00523, 1.4275e-06):
        add('ferry kind7 y %g' % y, beat, 0x825600, owner_args(0x825600, low),
            w(low) + [(low + 4, b8(1)), (top + 0xB4, f32b(y))])
    for yb in (0x42DC0000, 0x7F800000, 0xFF800000, 0x7FC00000, 0x00000001):   # 110.0 and non-finite patterns
        add('ferry kind7 bits %08X' % yb, beat, 0x825600, owner_args(0x825600, low),
            w(low) + [(low + 4, b8(1)), (top + 0xB4, w32(yb))])
    add('ferry kind7 scribble', beat, 0x825600, owner_args(0x825600, low), w(low) + [(low + 4, b8(1))],
        queues={0x1C6380: [Scribble(0, [(low + 0x4C, w32(0x1234560))])]})
    for shown in (0, 1, 0x80):
        add('ferry rider s1 %02X' % shown, beat, 0x825600, owner_args(0x825600, rider),
            w(rider) + [(rider + 4, b8(1)), (ferry + 1, b8(shown))])
        add('ferry other s1 %02X' % shown, beat, 0x825600, owner_args(0x825600, rider),
            w(rider) + [(rider + 4, b8(1)), (rider + 0xD, b8(0)), (ferry + 1, b8(shown))])
    add('ferry rider scribble', beat, 0x825600, owner_args(0x825600, rider), w(rider) + [(rider + 4, b8(1))],
        queues={0x1C6380: [Scribble(0, [(rider + 0x18, w32(rider)), (ferry + 1, b8(0))])]})
    add('ferry rider s1 bad link', beat, 0x825600, owner_args(0x825600, rider),
        w(rider) + [(rider + 4, b8(1)), (rider + 0x18, w32(0x02400000))])

    # ---- 0x825920 [43] and the plain group nodes
    for node in (s43, plain):
        for d in (0, 0xFF, 0xFE):
            for e in (0, 0xFF):
                for result in (0, 1, 2):
                    add('825920 %X s0 %02X %02X %d' % (node, d, e, result), beat, 0x825920, [node],
                        w(node) + [(node + 4, b8(0)), (0x81075D, b8(d)), (0x81075E, b8(e))],
                        queues={0x1B0F60: [result]})
        for state in (2, 3, 4, 0xFF):
            add('825920 %X s%d' % (node, state), beat, 0x825920, [node], w(node) + [(node + 4, b8(state))])
        for step in (0, 1, 2, 3):
            for entry in (5, 6, 4, 7):
                for d in (0, 0xFF):
                    for result in ((0, 1, 2) if step == 1 else (0,)):
                        for flag, use in (((0, 0), (1, 4), (0, 0xFB), (0x10, 4)) if step == 0 else ((0, 0),)):
                            add('825920 %X s1 step%d e%d %02X %d %d %02X' % (node, step, entry, d, result, flag, use),
                                beat, 0x825920, [node],
                                w(node) + [(node + 4, b8(1)), (node + 5, b8(step)), (0x810702, b8(entry)),
                                           (0x81075D, b8(d)), (0x8104E6, b8(flag)), (node + 0xB, b8(use))],
                                queues={0x1BA1F0: [result]})
    # +0x02 with only low bits set (bit 7 is the one tested)
    for flags in (0x01, 0x7F):
        for d in (0, 0xFF):
            add('825920 +2 %02X s0 %02X' % (flags, d), beat, 0x825920, [plain],
                w(plain) + [(plain + 4, b8(0)), (plain + 2, b8(flags)), (0x81075D, b8(d))])
        add('825920 +2 %02X s1' % flags, beat, 0x825920, [plain],
            w(plain) + [(plain + 4, b8(1)), (plain + 5, b8(2)), (plain + 2, b8(flags)), (0x810702, b8(5))])
    for x, y, z in ((1.0, -2.0, 5.0), (-0.0, 3.5, -1391.0), (149.4, -60.0, -1410.9), (1e-39, 1e-39, 5.0000005)):
        add('825920 step1 point %g' % z, beat, 0x825920, [s43],
            w(s43) + [(s43 + 4, b8(1)), (s43 + 5, b8(1)), (s43 + 0xB0, f32b(x)), (s43 + 0xB8, f32b(z)),
                      (0x810354, f32b(y))], queues={0x1BA1F0: [1]})
    add('825920 step1 scribble', beat, 0x825920, [s43], w(s43) + [(s43 + 4, b8(1)), (s43 + 5, b8(1)), (0x810702, b8(5))],
        queues={0x1BA1F0: [1], 0x1C64F0: [Scribble(0, [(s43 + 0xB0, f32b(7.25)), (0x810354, f32b(-3.5))])],
                0x182F90: [Scribble(0, [(0x810702, b8(6))])],
                0x1C68C0: [Scribble(0, [(s43 + 2, b8(0))])]})
    add('825920 tail scribble', beat, 0x825920, [s43], w(s43) + [(s43 + 4, b8(1)), (s43 + 5, b8(2)), (0x810702, b8(5)),
                                                                 (0x81075D, b8(0))],
        queues={0x1026A0: [Scribble(0, [(0x81075D, b8(0xFF))])]}, run_set=RUN - {0x1026A0})

    # ---- 0x825C80 / 0x8261E0 / 0x8262D0 the three examine owners
    for entry in (0x825C80, 0x8261E0, 0x8262D0):
        node = one(entry)
        for state in (0, 2, 3, 4, 0xFF):
            add('%06X s%d' % (entry, state), beat, entry, [node], w(node) + [(node + 4, b8(state))])
        for step in (0, 1, 2, 0xFF):
            for use in (0, 4, 0xFB):
                for result in ((0, 1, 2) if step == 1 else (0,)):
                    add('%06X s1 step%d %02X %d' % (entry, step, use, result), beat, entry, [node],
                        w(node) + [(node + 4, b8(1)), (node + 5, b8(step)), (node + 0xB, b8(use))],
                        queues={0x1BA1F0: [result]})

    # ---- 0x8263C0 the ferry cab
    for result, spawned in ((1, 0), (0, 0), (0, spare), (2, spare)):
        add('cab s0 %d %X' % (result, spawned), beat, 0x8263C0, [cab], w(cab) + [(cab + 4, b8(0))],
            queues={0x1B0FD0: [result], 0x1B6660: [spawned]})
    add('cab s0 scribble', beat, 0x8263C0, [cab], w(cab) + [(cab + 4, b8(0))],
        queues={0x1B6660: [spare], 0x1028D0: [Scribble(0, [(cab + 0x14, w32(0x13572468))])]},
        run_set=RUN - {0x1028D0})
    for state in (2, 3, 4, 0xFF):
        add('cab s%d' % state, beat, 0x8263C0, [cab], w(cab) + [(cab + 4, b8(state))])
    other = u32(ram, cab + 0x1C)
    for step in (0, 1, 2, 0xFF):
        for use in (0, 4, 0xFB):
            for result in ((0, 1, 2) if step == 1 else (0,)):
                add('cab s1 step%d %02X %d' % (step, use, result), beat, 0x8263C0, [cab],
                    w(cab) + [(cab + 4, b8(1)), (cab + 5, b8(step)), (cab + 0xB, b8(use))],
                    queues={0x1BA1F0: [result]})
    add('cab s1 step0 scribble', beat, 0x8263C0, [cab], w(cab) + [(cab + 4, b8(1)), (cab + 5, b8(0)), (cab + 0xB, b8(4))],
        queues={0x1BA1A0: [Scribble(0, [(cab + 5, b8(0x20))])]}, run_set=RUN - {0x1BA1A0})
    # the follow test: x equal / not, y equal / not (EE compare: -0 == +0, denormals are zero)
    for sx, ox, sy, oy in ((50.0, 50.0, -18.0, -19.0), (50.0, 50.0, -18.0, -18.5), (50.0, 51.0, -18.0, -19.0),
                           (0.0, -0.0, 1.0, 0.0), (1e-39, 0.0, 1.0, -1e-39), (155.0, 155.0, -47.0, -48.0),
                           (155.0, 155.00002, -47.0, -48.0), (3.4028235e38, 3.4028235e38, 1.0, 0.0),
                           (-5.0, -5.0, 3.4028235e38, 3.4028235e38)):
        add('cab follow %g %g %g %g' % (sx, ox, sy, oy), beat, 0x8263C0, [cab],
            w(cab) + [(cab + 4, b8(1)), (cab + 5, b8(2)), (cab + 0xB0, f32b(sx)), (other + 0xB0, f32b(ox)),
                      (cab + 0xB4, f32b(sy)), (other + 0xB4, f32b(oy))])
    # small positions: there the operand pre-trim keeps every bit of the offsets
    for x, y, z in ((12.762753, 0.0011, -0.0004), (0.00523, 12.762753, 1.4275e-06), (0.0011, -0.0004, 12.762753)):
        add('cab small position %g %g %g' % (x, y, z), beat, 0x8263C0, [cab],
            w(cab) + [(cab + 4, b8(1)), (cab + 5, b8(2)), (cab + 0xB0, f32b(x)), (cab + 0xB4, f32b(y)),
                      (cab + 0xB8, f32b(z)), (other + 0xB0, f32b(x)), (other + 0xB4, f32b(y - 1.0))])
    for sxb, oxb in ((0x7F800000, 0x7F800000), (0x7FC00000, 0x7FC00000), (0xFF800000, 0x7F800000),
                     (0x00000001, 0x80000000)):
        add('cab follow bits %08X %08X' % (sxb, oxb), beat, 0x8263C0, [cab],
            w(cab) + [(cab + 4, b8(1)), (cab + 5, b8(2)), (cab + 0xB0, w32(sxb)), (other + 0xB0, w32(oxb))])
    add('cab follow scribble', beat, 0x8263C0, [cab], w(cab) + [(cab + 4, b8(1)), (cab + 5, b8(2)),
                                                               (cab + 0xB0, f32b(1.0)), (other + 0xB0, f32b(2.0))],
        queues={0x1C6380: [Scribble(0, [(cab + 0xB4, f32b(-7.5)), (cab + 0xB8, f32b(3.25))])],
                0x1A2370: [Scribble(0, [(cab + 0x4C, w32(0x1234560))])],
                0x1F5940: [Scribble(0, [(cab + 0xB8, f32b(-100.5))])]})

    # ---- 0x8266A0 [60]
    for state in (0, 2, 3, 4, 0xFF):
        add('beam s%d' % state, beat, 0x8266A0, [beam], w(beam) + [(beam + 4, b8(state))])
    for e in (0, 0xFF, 0xFE):
        for kind in (0x19, 0x1A, 0x18):
            add('beam s1 %02X %02X' % (e, kind), beat, 0x8266A0, [beam],
                w(beam) + [(beam + 4, b8(1)), (0x81075E, b8(e)), (beam + 0xD, b8(kind))])
    add('beam s1 scribble', beat, 0x8266A0, [beam],
        w(beam) + [(beam + 4, b8(1)), (0x81075E, b8(0xFF)), (beam + 0xD, b8(0x19)), (beam + 9, b8(3))],
        queues={0x1AF800: [Scribble(0, [(beam + 9, b8(0x77))])], 0x1C6380: [Scribble(0, [(beam + 4, b8(9))])]})

    # ---- result widths: at every test of a callee's result, values whose low
    # byte or halfword is zero and values with bit 31 set, so the form of each
    # test (non-zero, not == 1, not a narrowed or signed compare) is pinned
    for width in WIDE_RESULTS:
        tag = '%X' % width
        add('door52 step0 wide %s' % tag, beat, 0x823580, [door],
            w(door) + [(door + 4, b8(1)), (door + 5, b8(0)), (0x81075E, b8(0)), (0x81075D, b8(0))],
            queues={0x1BBE40: [width]})
        for step in (1, 2, 4, 5, 7):
            add('door52 step%d wide %s' % (step, tag), beat, 0x823580, [door],
                w(door) + [(door + 4, b8(1)), (door + 5, b8(step))],
                queues={a: [width] for a in (0x1BBE40, 0x1BC0E0, 0x1BC290, 0x1BA1F0)})
        for entry, flag in ((5, 0), (4, 0xFF)):
            add('door51 step0 e%d wide %s' % (entry, tag), beat, 0x825170, [door51],
                w(door51) + [(door51 + 4, b8(1)), (door51 + 5, b8(0)), (0x810702, b8(entry)), (0x81075B, b8(flag))],
                queues={0x1BBE40: [width]})
        for step in (1, 2, 3, 5, 6):
            add('door51 step%d wide %s' % (step, tag), beat, 0x825170, [door51],
                w(door51) + [(door51 + 4, b8(1)), (door51 + 5, b8(step)), (0x81075B, b8(0))],
                queues={a: [width] for a in (0x1BC0E0, 0x1BC290, 0x1BA1F0)})
        add('terminal s0 wide %s' % tag, beat, 0x825480, [terminal], w(terminal) + [(terminal + 4, b8(0))],
            queues={0x1B0FD0: [width]})
        add('terminal s1 wide %s' % tag, beat, 0x825480, [terminal],
            w(terminal) + [(terminal + 4, b8(1)), (terminal + 5, b8(1))], queues={0x1BA1F0: [width]})
        add('ferry s0 wide %s' % tag, beat, 0x825600, owner_args(0x825600, ferry), w(ferry) + [(ferry + 4, b8(0))],
            queues={0x1B0FD0: [width]})
        add('ferry s1 wide %s' % tag, beat, 0x825600, owner_args(0x825600, ferry),
            w(ferry) + [(ferry + 4, b8(1)), (ferry + 5, b8(1))], queues={0x1BA1F0: [width]})
        for d, node in ((0xFF, plain), (0, s43), (0, plain)):
            add('825920 %X s0 %02X wide %s' % (node, d, tag), beat, 0x825920, [node],
                w(node) + [(node + 4, b8(0)), (0x81075D, b8(d))], queues={0x1B0F60: [width]})
        add('825920 s1 wide %s' % tag, beat, 0x825920, [s43], w(s43) + [(s43 + 4, b8(1)), (s43 + 5, b8(1))],
            queues={0x1BA1F0: [width]})
        add('825C80 s1 wide %s' % tag, beat, 0x825C80, [one(0x825C80)],
            w(one(0x825C80)) + [(one(0x825C80) + 4, b8(1)), (one(0x825C80) + 5, b8(1))], queues={0x1BA1F0: [width]})
        add('cab s0 wide %s' % tag, beat, 0x8263C0, [cab], w(cab) + [(cab + 4, b8(0))],
            queues={0x1B0FD0: [width], 0x1B6660: [0]})
        add('cab s1 wide %s' % tag, beat, 0x8263C0, [cab], w(cab) + [(cab + 4, b8(1)), (cab + 5, b8(1))],
            queues={0x1BA1F0: [width]})
    for spawned in (0x100, 0x10000, 0x1000000):   # a spawned-node pointer whose low bits are zero
        add('cab s0 spawned %X' % spawned, beat, 0x8263C0, [cab], w(cab) + [(cab + 4, b8(0))],
            queues={0x1B0FD0: [0], 0x1B6660: [spawned]})

    # ---- 0x823820 the puffs (the node spawned in a00_08)
    pbeat = 'a00_08_switch'
    pram, _ = CAPTURES[pbeat]
    puff = owners(pram, 0x823820)[0]
    fx = puff + 0x1F0

    def pw(node):
        return world_patch(pbeat, node)
    for state in (0, 2, 3, 4, 0xFF):
        add('puffs s%d' % state, pbeat, 0x823820, owner_args(0x823820, puff), pw(puff) + [(puff + 4, b8(state))])
    add('puffs s0 negative random', pbeat, 0x823820, owner_args(0x823820, puff), pw(puff) + [(puff + 4, b8(0))],
        queues={0x122BB8: [-5, -0x7FFFFFFF - 1, 0x7FFFFFFF, -119, 119, 120, -120, -1] * 3},
        run_set=RUN - {0x122BB8})
    # state 1: every puff live, a values around every compare and clamp
    a_values = [0.0, 0.1, 0.29999998, 0.3, 0.30000001, 1.91, 1.9100001, 1.9099999, -0.0, 1e-39]
    b_values = [0.0, 1.49, 1.4899999, 1.4900001, 1.5, 0.25, -0.0, 1e-39]
    for k in range(3):
        patches = pw(puff) + [(puff + 4, b8(1))]
        for i in range(8):
            patches += [(fx + 8 + 4 * i, w32([0, -1, 0, 5, 1, 0, -100, 0][i] if k else 0)),
                        (fx + 0x28 + 0x10 * i, f32b(a_values[(i + 3 * k) % len(a_values)])),
                        (fx + 0x2C + 0x10 * i, f32b(b_values[(i + 5 * k) % len(b_values)])),
                        (fx + 0x30 + 0x10 * i, f32b(0.125 * i)), (fx + 0x34 + 0x10 * i, f32b(-0.0625 * i))]
        add('puffs s1 set %d' % k, pbeat, 0x823820, owner_args(0x823820, puff), patches,
            queues={0x1CCF70: [[0xFFFFFF, 7, -3][k]]})
    patches = pw(puff) + [(puff + 4, b8(1))]
    for i in range(8):
        patches += [(fx + 8 + 4 * i, w32(0)), (fx + 0x28 + 0x10 * i, f32b(0.05 * i)), (fx + 0x2C + 0x10 * i, f32b(1.495))]
    add('puffs s1 scripted colour', pbeat, 0x823820, owner_args(0x823820, puff), patches,
        queues={0x1281C0: [0x12345678, -1, 0x7FFF, 0x80, 0xFFFFFF01, 3] * 8, 0x1CD520: [5, -1, 0] * 3},
        run_set=RUN - {0x1281C0})
    for abits in (0x7F800000, 0xFF800000, 0x7FC00000, 0x00000001, 0x3E999999, 0x3E99999B, 0x3FF47AE1, 0x3FF47AE2):
        patches = pw(puff) + [(puff + 4, b8(1))]
        for i in range(8):
            patches += [(fx + 8 + 4 * i, w32(0 if i < 2 else 3))]
        patches += [(fx + 0x28, w32(abits)), (fx + 0x2C, w32(abits)), (fx + 0x38, w32(abits ^ 0x80000000))]
        add('puffs s1 bits %08X' % abits, pbeat, 0x823820, owner_args(0x823820, puff), patches)
    add('puffs s1 scribble', pbeat, 0x823820, owner_args(0x823820, puff),
        pw(puff) + [(puff + 4, b8(1)), (fx + 8, w32(0)), (fx + 0x28, f32b(0.1)), (fx + 0x2C, f32b(1.495))]
        + [(fx + 8 + 4 * i, w32(9)) for i in range(1, 8)],
        queues={0x1CD520: [Scribble(0, [(fx + 0x28, f32b(1.95))])],
                0x1CFBE0: [Scribble(0, [(fx + 0x28, f32b(0.5)), (fx + 0x2C, f32b(1.2))]),
                           Scribble(0, [(fx + 0x2C, f32b(1.6))])],
                0x1281C0: [Scribble(3, [(0x70003600, w32(0x11))]), Scribble(4, [(0x70003604, w32(0x22))]),
                           Scribble(5, [(0x70003608, w32(0x33))])]},
        run_set=RUN - {0x1281C0})
    add('puffs s1 timer scribble', pbeat, 0x823820, owner_args(0x823820, puff),
        pw(puff) + [(puff + 4, b8(1))] + [(fx + 8 + 4 * i, w32(1)) for i in range(8)],
        queues={0x1CCF70: [Scribble(0, [(fx + 8, w32(0)), (fx + 0xC, w32(0x80000000))])]})

    # ---- 0x824EA0 the arrival owner (its record in the arrival image)
    abeat = 'a01_07_level_exit'
    aram, _ = CAPTURES[abeat]
    arrival = any_owner(aram, 0x824EA0)[0]

    def aw(node):
        return world_patch(abeat, node) + [(node, b8(1))]
    for state in (2, 3, 4, 0xFF):
        add('arrival s%d' % state, abeat, 0x824EA0, [arrival], aw(arrival) + [(arrival + 4, b8(state))],
            queues={0x1C4760: [Scribble(0, [(arrival + 4, b8(0x21))])]})
    for alarm, sub, walk in ((1, 0, 0), (0, 1, 0), (0, 0, 1), (0, 0, 0), (2, 0, 0), (0, 0x80, 2)):
        add('arrival s0 step0 %d %d %d' % (alarm, sub, walk), abeat, 0x824EA0, [arrival],
            aw(arrival) + [(arrival + 4, b8(0)), (arrival + 5, b8(0)), (0x810701, b8(sub))],
            queues={0x1BA1C0: [alarm], 0x1B10B0: [walk]})
    add('arrival s0 step0 scribble', abeat, 0x824EA0, [arrival],
        aw(arrival) + [(arrival + 4, b8(0)), (arrival + 5, b8(0)), (0x810701, b8(0))],
        queues={0x1029C0: [Scribble(0, [(arrival + 5, b8(0x44))])]}, run_set=RUN - {0x1029C0})
    for result in (0, 1, 2):
        add('arrival s0 step1 %d' % result, abeat, 0x824EA0, [arrival],
            aw(arrival) + [(arrival + 4, b8(0)), (arrival + 5, b8(1))], queues={0x129780: [result]})
    add('arrival s0 step2', abeat, 0x824EA0, [arrival], aw(arrival) + [(arrival + 4, b8(0)), (arrival + 5, b8(2))])
    for step in (0, 1, 2, 3, 0xFF):
        for counter in (0, 0x149, 0x14A, 0x4B0, 0xFFFF):
            for flag in ((0, 1) if step == 2 else (0,)):
                for walk, done in (((0, 0), (1, 1), (2, 0)) if step != 2 else ((0, 0), (0, 1))):
                    add('arrival s1 step%d %X %d %d %d' % (step, counter, flag, walk, done), abeat, 0x824EA0,
                        [arrival], aw(arrival) + [(arrival + 4, b8(1)), (arrival + 5, b8(step))],
                        [(0x70003B84, h16(counter)), (0x70003B8D, b8(flag))],
                        queues={0x1C2770: [walk], 0x1BA1F0: [done]})
    add('arrival s1 scribble', abeat, 0x824EA0, [arrival], aw(arrival) + [(arrival + 4, b8(1)), (arrival + 5, b8(1))],
        [(0x70003B84, h16(0x100))],
        queues={0x1C3D60: [Scribble(0, [(arrival + 0x1F0 + 0xEC, f32b(0.5))])],
                0x1C69A0: [Scribble(0, [(arrival + 0x4C, w32(0x1234560))])],
                0x1C64F0: [Scribble(0, [(0x70003B84, h16(0x14A))])],
                0x1BA1F0: [Scribble(1, [(arrival + 4, b8(0x31)), (arrival + 5, b8(0x32))])]})
    add('arrival s1 step2 scribble', abeat, 0x824EA0, [arrival], aw(arrival) + [(arrival + 4, b8(1)), (arrival + 5, b8(2))],
        [(0x70003B84, h16(0x4B0)), (0x70003B8D, b8(0))],
        queues={0x1BA1F0: [Scribble(1, [(0x70003B8D, b8(1)), (arrival + 4, b8(0x40))])]})
    add('arrival s1 scribble counter', abeat, 0x824EA0, [arrival], aw(arrival) + [(arrival + 4, b8(1)), (arrival + 5, b8(1))],
        [(0x70003B84, h16(0x200))],
        queues={0x1C2770: [Scribble(1, [(arrival + 5, b8(0x55))])],
                0x1029C0: [Scribble(0, [(arrival + 5, b8(0))])]}, run_set=RUN - {0x1029C0})
    for width in WIDE_RESULTS:
        tag = '%X' % width
        add('arrival s0 wide %s' % tag, abeat, 0x824EA0, [arrival],
            aw(arrival) + [(arrival + 4, b8(0)), (arrival + 5, b8(0)), (0x810701, b8(0))],
            queues={0x1BA1C0: [width]})
        add('arrival s0 walk wide %s' % tag, abeat, 0x824EA0, [arrival],
            aw(arrival) + [(arrival + 4, b8(0)), (arrival + 5, b8(0)), (0x810701, b8(0))],
            queues={0x1BA1C0: [0], 0x1B10B0: [width]})
        add('arrival s0 step1 wide %s' % tag, abeat, 0x824EA0, [arrival],
            aw(arrival) + [(arrival + 4, b8(0)), (arrival + 5, b8(1))], queues={0x129780: [width]})
        for step in (1, 2):
            add('arrival s1 step%d wide %s' % (step, tag), abeat, 0x824EA0, [arrival],
                aw(arrival) + [(arrival + 4, b8(1)), (arrival + 5, b8(step))], [(0x70003B84, h16(0x10))],
                queues={0x1C2770: [width], 0x1BA1F0: [width]})


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
    unmapped address and a fault latched before the call (on the terminal,
    state 1 step 1: 001BA1F0, 001B17A0, the +0x4C callback), then every
    entry with a latched fault, a NULL hook table and a NULL fault pointer."""
    ram, spad = CAPTURES['a00_04_cage_terminal']
    node = owners(ram, 0x825480)[0]
    problems = []

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
        status = NATIVE.em_area00_ovl_00825480(C.byref(hooks), node, C.byref(fault))
        return status, fault.address, fault.code, calls, before == C.string_at(base, RAM_SIZE)

    status, address, code, calls, _ = attempt()
    if (status, address, code) != (0, 0, 0) or calls != ['w_001BA1F0', 'w_001B17A0', 'w_callback']:
        problems.append(('fault-check baseline', status, hex(address), code, calls))
    status, address, code, calls, _ = attempt(null='w_001BA1F0')
    if (status, address, code) != (-1, 0x1BA1F0, 1) or calls:
        problems.append(('NULL hook', status, hex(address), code, calls))
    status, address, code, calls, same = attempt(failing='w_001BA1F0')
    if (status, address, code) != (-1, 0x1BA1F0, 2) or calls != ['w_001BA1F0'] or not same:
        problems.append(('failing hook', status, hex(address), code, calls, same))
    status, address, code, calls, _ = attempt(unmapped=node + 0x4C)
    if (status, address, code) != (-1, node + 0x4C, 5) or calls != ['w_001BA1F0', 'w_001B17A0']:
        problems.append(('unmapped address', status, hex(address), code, calls))
    status, address, code, calls, _ = attempt(unmapped=node + 5)
    if (status, address, code) != (-1, node + 5, 5) or calls:
        problems.append(('unmapped address before any call', status, hex(address), code, calls))
    status, address, code, calls, same = attempt(latched=True)
    if (status, address, code) != (-1, 0x1234, 7) or calls or not same:
        problems.append(('latched fault', status, hex(address), code, calls, same))
    if NATIVE.em_area00_ovl_00825480(None, node, C.byref(Fault())) != -1:
        problems.append(('NULL hooks accepted',))
    # every entry: a fault latched on entry, a NULL hook table or a NULL
    # fault pointer returns -1 at once, with no call and (op09) no result
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
        for table, fault in ((C.byref(hooks), Fault(0x1234, 7)), (None, Fault()), (C.byref(hooks), None)):
            result = C.c_int32(0x5A5A5A5A)
            del touched[:]
            where = C.byref(fault) if fault is not None else None
            if kind == 'op09':
                status = fn(table, node, node + 0x1F0, 0, C.byref(result), where)
            elif kind == 'owner_sp':
                status = fn(table, node, STACK_TOP, where)
            else:
                status = fn(table, node, where)
            latched = fault is None or (fault.address, fault.code) == ((0x1234, 7) if table is not None else (0, 0))
            if status != -1 or touched or result.value != 0x5A5A5A5A or not latched:
                problems.append(('entry %06X refused' % entry, status, touched[:2], hex(result.value & MASK)))
    # the op09 entry with a NULL result pointer
    del touched[:]
    if NATIVE.em_area00_ovl_008253E0(C.byref(hooks), node, node + 0x1F0, 0, None, C.byref(Fault())) != -1 or touched:
        problems.append(('op09 NULL result accepted',))
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
    """The fail-stop contract of the header (em_area00_overlay.h), on one
    case (CASES[index]), against the original's run of that case:
      * at EVERY call k of the case, the hook (or the +0x4C callback)
        returning -1, and at the first call of each target hook also
        INT32_MIN: the entry returns -1 with fault (the original callee's
        address, 2; for the callback the function read from +0x4C), calls
        0..k were made and no other, no `bytes` request follows the failed
        call, memory is the original's at the entry of call k (the failed
        callee's writes are not replayed) and an op09 result is not written;
      * at EVERY memory access n of the case, `bytes` refusing that request:
        -1 with fault (that address, 5), exactly the original's calls before
        that access, no later request and no op09 result;
      * each target hook NULL in the table at its first call k: -1 with
        fault (callee, 1) after exactly the original's calls before k, the
        accesses since call k-1 exactly the original's (none after), memory
        as at the entry of call k, no op09 result;
      * each target hook returning 1 or INT32_MAX at its first call (a
        success): the whole case compares as usual (finish);
      * target ('bytes', entry): `bytes` NULL in the table: -1 with fault
        (the original's first access, 5) after the original's calls before
        it.
    After a fault the module's reads come from a zeroed sink, so its
    control flow runs on; the every-call and every-access sweeps therefore
    reach later hook wrappers with the fault already latched, which is
    where each wrapper's own `a00_failed` test is observable. Returns
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
        unwritten = kind != 'op09' or result == 0x5A5A5A5A
        if got != want or replay.errors or replay.broken or not extra or not unwritten:
            problems.append((label, what, 'want', (want[0], hex(want[1]), want[2], want[3]),
                             'got', (got[0], hex(got[1]), got[2], got[3]), 'op09 result written' if not unwritten
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
    return reference_mode.select(cases, reference_mode.pick(len(cases), fixed + len(pinned) + QUICK_RANDOM), 0xA00,
                                 keep=lambda i, c: i < fixed or i in pinned), captured, targeted, len(cases)


def main():
    global NATIVE
    elf = read_elf()
    CAPTURES.update(load_captures(elf))
    NATIVE = build(os.environ.get('EM_AREA00_OVL_SOURCE'))
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
             'all 32 MiB + scratchpad' if reference_mode.FULL else 'dirty set', totals[2]))
    print('all %d cases identical: callee calls, arguments and results; memory at every call entry (%s); '
          'the memory accesses between calls (one for one, in order); '
          'memory after the last store (all 32 MiB + scratchpad); the table\'s ctx at every hook, `bytes` and '
          'callback call'
          % (len(selected), 'all 32 MiB + scratchpad' if reference_mode.FULL else 'dirty set'))


if __name__ == '__main__':
    main()
