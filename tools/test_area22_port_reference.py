#!/usr/bin/env python3
"""Execute the ORIGINAL AREA22-route boot code and the AREA22 area init and
compare em_area22_port.c.

docs/AREA22_PORT.md. The census delta of the AREA22 route (decomp
build/s87/census/a22_delta.json, new_functions) has 11 boot rows. Eight had
no port translation and are translated in em_area22_port.c: 00100110,
001028E8, 0012D940, 00183010, 0018C850, 0018C920, 001963A0, 001BB310; so is
the AREA22 area init at runtime 0x823580 (OVERLAY/AREA22.BIN, link
overlay_AREA22_func_00823540). 001963A0 calls 0018C920 and 0018C850; those
calls run as original code at the top level of the oracle (only other boot
functions are hooks), and both are also entries of their own. The other
three rows, 001A44B0, 001A4830 and 001A5C30, have verified translations in
other modules (em_coll_probe_original.c, em_coll_move_original.c,
em_coll_segment_walkers.c); `reuse_checks` runs those translations against
the original instructions on the AREA22 drum prims (section below).

The harness is tools/test_area04_port_reference.py (lane A04T, itself the
AREA02 / AREA00 / AREA01 overlay design), reused with the AREA22 captures,
the AREA22 hook table and cases, entries whose arguments are words, floats
(f12..) or whole 64-bit registers, and no +0x4C callback. As there, the
oracle runs with the private stack top STACK_TOP as sp at entry; the window
[STACK_LO, STACK_TOP) is compared memory, the frame bookkeeping excepted
(stack_frame_op).

Oracle: the shared EE interpreter with the measured float model (FallEE,
tools/test_player_fall_reference.py) runs the original code resident in the
recorded RAM images of the AREA22 route (decomp build/s87/route_a22/<beat>/
for a22_00, a22_01, a22_s0, a22_s1, a22_s2, with AREA22 resident, overlay
id 0x13; a22_02 and a22_s3, which end in AREA01 and AREA04, for the boot
entries only) and of the AREA22 arrival (build/s87/route_a04/
a04_05_progression_exit, AREA22 resident). Before any case the test checks
that the overlay text of every AREA22 image equals the user's
extract/OVERLAY/AREA22.BIN (text size from its header) and that the boot
text below 0x241000 of every image equals the pinned ELF, so every executed
instruction is original. Nothing here embeds original bytes; reports hold
counts only.

Callees (every boot function outside this module) are intercepted at their
entry and logged with their arguments. A callee either runs as ORIGINAL
code nested inside the oracle (the pure helpers in RUN: cos, sin, fabs, the
VU0 add and copy, the random generator, the soft-float compare 001274B0,
the angle wrap, 00128830, 0012E070, 0018C0C0 and the two camera chases
0018C4B0 / 0018C6A0), its writes recorded, or is stubbed with the case's
scripted result (everything else). The native module runs over a byte copy
of the same RAM with hooks that must be called in the same order with the
same arguments; each hook replays the original callee's writes and result.

Compared, per case (as in the AREA04 harness, docstring there): memory at
the entry of every call before the callee's writes are replayed; the callee
and its arguments; the memory accesses between calls one for one, in order,
by address, size and changed-or-not (no jump tables here, so no load is
left out); all memory after the last store; the return value; the
store-log self-check; stops at unmapped or misaligned original accesses;
every case again from a poisoned start image; coverage of every reachable
original word; the fail-stop contract; the table's ctx at every call.

Cases:
  capture   the entries on the captured state of every image they apply to
            (bug nodes, the camera block and the player, the doors, the
            eye / target vectors, the AREA22 init on every AREA22 image).
  designed  targeted_cases: every state and step, callee results, the
            float boundaries of every compare, scribbling callees.
  perturbed seeded variations (EM_TEST_FULL=1 only).

EM_AREA22_PORT_SOURCE=<file> tests another copy of the module source (the
mutation sweep). EM_AREA22_PORT_ONLY=<label prefix> runs a subset. At most 4
worker processes unless EM_TEST_JOBS says otherwise.
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
OVERLAY_FILE = DECOMP / 'extract/OVERLAY/AREA22.BIN'
OUT = ROOT / 'build/area22/port'
MASK = 0xFFFFFFFF
MASK64 = 0xFFFFFFFFFFFFFFFF
RAM_SIZE, SPAD_SIZE = 0x2000000, 0x4000
ARENA, TEXT_END = 0x823500, 0x823600
OVERLAY_ID = 0x13
STACK_LO = STACK_TOP - 0x400
STACK_SIZE = STACK_TOP - STACK_LO
BEAT_DIRS = {'a04_05_progression_exit': DECOMP / 'build/s87/route_a04/a04_05_progression_exit'}
for _b in ('a22_00_door7', 'a22_01_corridor', 'a22_02_progression_exit', 'a22_s0_pickup', 'a22_s1_ladder_reader',
           'a22_s2_door10_locked', 'a22_s3_door6_back'):
    BEAT_DIRS[_b] = DECOMP / 'build/s87/route_a22' / _b
BEATS = list(BEAT_DIRS)
# The images with AREA22 resident (the others end in AREA01 / AREA04).
AREA22_BEATS = [b for b in BEATS if b not in ('a22_02_progression_exit', 'a22_s3_door6_back')]
POOL, NODE, NODES = 0x7A5640, 0x2F0, 400
PLAYER = 0x8102B0
CAM = 0x8101E0
EYE, TARGET = 0x8105D0, 0x8105E0


class _Route:
    """ROUTE / beat resolves to the beat's folder (two route trees)."""

    def __truediv__(self, beat):
        return BEAT_DIRS[beat]


ROUTE = _Route()

U, I, F, Q = 'u', 'i', 'f', 'q'
# (entry, native symbol, byte length of the function body, argument kinds,
# result kind or None). Word arguments go to a0.., floats to f12.., Q is a
# whole 64-bit register.
FUNCS = {
    0x100110: ('em_area22_port_00100110', 0x20, [Q, Q, Q, Q], I),
    0x1028E8: ('em_area22_port_001028E8', 0x14, [U, U, U], None),
    0x12D940: ('em_area22_port_0012D940', 0x428, [U, U], None),
    0x183010: ('em_area22_port_00183010', 0x74, [U, U], None),
    0x18C850: ('em_area22_port_0018C850', 0xCC, [U, F, F], I),
    0x18C920: ('em_area22_port_0018C920', 0x168, [U, U, F], I),
    0x1963A0: ('em_area22_port_001963A0', 0x5C4, [U, U], None),
    0x1BB310: ('em_area22_port_001BB310', 0xE8, [U], I),
    0x823580: ('em_area22_port_00823580', 0x20, [], None),
}
OVERLAY_ENTRIES = (0x823580,)


def has_result(entry):
    return FUNCS[entry][3] is not None


# Hooks, in the field order of EmArea22PortHooks (after ctx and bytes).
# name -> (original address, [(ctype, register)], result kind or None)
HOOKS = [
    ('w_0011DE90', 0x11DE90, [(F, 12)], F),
    ('w_0011DF78', 0x11DF78, [(F, 12)], F),
    ('w_0011E2A8', 0x11E2A8, [(F, 12)], F),
    ('w_001028B8', 0x1028B8, [(U, 4), (U, 5), (U, 6)], None),
    ('w_00102948', 0x102948, [(U, 4), (U, 5)], None),
    ('w_00122BB8', 0x122BB8, [], I),
    ('w_0012E070', 0x12E070, [(U, 4)], None),
    ('w_001274B0', 0x1274B0, [(Q, 4), (Q, 5), (Q, 6), (Q, 7)], Q),
    ('w_00128640', 0x128640, [(U, 4)], I),
    ('w_001287F0', 0x1287F0, [(U, 4), (U, 5), (I, 6), (F, 12)], None),
    ('w_00128830', 0x128830, [(U, 4), (F, 12), (F, 13), (F, 14)], None),
    ('w_0018C0C0', 0x18C0C0, [(U, 4)], None),
    ('w_0018C4B0', 0x18C4B0, [(U, 4), (F, 12), (F, 13)], None),
    ('w_0018C6A0', 0x18C6A0, [(U, 4), (U, 5), (F, 12)], None),
    ('w_0018D7B0', 0x18D7B0, [(U, 4), (I, 5)], None),
    ('w_001B1470', 0x1B1470, [(F, 12)], F),
    ('w_001B5360', 0x1B5360, [(U, 4)], None),
    ('w_001C2770', 0x1C2770, [(U, 4), (U, 5), (I, 6)], I),
    ('w_001C3D60', 0x1C3D60, [(U, 4), (U, 5)], None),
]
BY_ADDRESS = {address: (name, args, result) for name, address, args, result in HOOKS}

# Callees that run as original code inside the oracle (writes replayed to
# the native side). Pure helpers only: none of them reaches hardware.
RUN = {0x11DE90, 0x11DF78, 0x11E2A8, 0x1028B8, 0x102948, 0x122BB8, 0x12E070, 0x1274B0, 0x128830, 0x18C0C0,
       0x18C4B0, 0x18C6A0, 0x1B1470}

# No register-indirect jump other than the returns in any of these
# functions (the dispatches are compare chains).
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
                + [(name, hook_proto(args, result)) for name, _, args, result in HOOKS])


class Fault(C.Structure):
    _fields_ = [('address', C.c_uint32), ('code', C.c_int32)]


def fbits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


# ----------------------------------------------------------------------------
# build + inputs
# ----------------------------------------------------------------------------
def build(source=None):
    """Compile the module (or `source`, a copy under test in a mutation
    sweep) into a private library under build/area22/port."""
    OUT.mkdir(parents=True, exist_ok=True)
    source = source or 'src/game/em_area22_port.c'
    stem = 'area22_port' if source == 'src/game/em_area22_port.c' else 'area22_port_%d' % os.getpid()
    lib = OUT / (stem + ('.dylib' if sys.platform == 'darwin' else '.so'))
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc', '-I' + str(ROOT / 'src/game'), str(source),
                    '-lm', '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    for entry, (symbol, _, kinds, result) in FUNCS.items():
        fn = getattr(native, symbol)
        types = [C.POINTER(Hooks)] + [{U: C.c_uint32, F: FloatArg, Q: C.c_uint64}[k] for k in kinds]
        if result is not None:
            types.append(C.POINTER(C.c_int32))
        fn.argtypes = types + [C.POINTER(Fault)]
        fn.restype = C.c_int
    return native


def load_captures(elf):
    """Every AREA22 image must hold the AREA22 overlay (header id 0x13) with
    its text equal to the user's AREA22.BIN, and every image's boot text
    must equal the pinned ELF: every instruction the oracle executes is
    original (the two images that end in AREA01 / AREA04 run boot code
    only)."""
    overlay = OVERLAY_FILE.read_bytes()
    text_end = ARENA + 0x40 + struct.unpack_from('<I', overlay, 12)[0]
    assert text_end == TEXT_END, hex(text_end)
    captures = {}
    for beat in BEATS:
        folder = ROUTE / beat
        ram = (folder / 'eeMemory.bin').read_bytes()
        spad = (folder / 'scratchpad.bin').read_bytes()
        assert len(ram) == RAM_SIZE and len(spad) == SPAD_SIZE, beat
        if beat in AREA22_BEATS:
            assert ram[ARENA:ARENA + 8] == overlay[:8] and ram[ARENA + 4] == OVERLAY_ID, (beat, 'AREA22 not resident')
            assert ram[ARENA + 0x40:TEXT_END] == overlay[0x40:TEXT_END - ARENA], (beat, 'overlay text differs')
        else:
            assert ram[ARENA + 4] != OVERLAY_ID, (beat, 'expected another overlay')
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


def start(ee, entry, values):
    """Set the entry's argument registers (words sign-extended into a0..,
    floats as bits into f12.., Q as the whole 64-bit register) and run it."""
    words = floats = 0
    for kind, value in zip(FUNCS[entry][2], values):
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
        if null is not None:
            del fields[null]
        return Hooks(**fields)


BUFFERS = []
STACKS = []   # the stack window: native, expected (Replay)
CTX = 0xA22C7C00   # the hook table's ctx in every Replay: opaque, never dereferenced by the module


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
    cost = lambda i: 3 if selected[i][2] in (0x1963A0, 0x12D940) else 1   # noqa: E731
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
    """No +0x4C callback is reached by these functions."""
    return set()


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
F_176_9 = 0x4330E666
SPECIAL = (0, 0x80000000, 1, 0x80000001, 0x007FFFFF, NAN, SNAN, 0xFFC00000, POS_INF, NEG_INF, FMAX, FMAX | 0x80000000)
A22_BUGS = 0x12A5D0
DOORS = (0x1BB860, 0x1BC350)
D702 = 0x810702


def dbl(value):
    """A double's bit pattern (the soft-float argument registers)."""
    return struct.unpack('<Q', struct.pack('<d', value))[0]


def case_list(elf):
    rng = random.Random(0xA22)
    cases = []
    count = reference_mode.pick(40, 0)

    def add(label, beat, entry, args, patches=(), spad=(), queues=None, default=None, run_set=RUN):
        cases.append((label, beat, entry, tuple(args), list(patches), list(spad),
                      Script(queues, default), frozenset(run_set)))

    for beat in BEATS:
        ram, spad = CAPTURES[beat]
        if beat in AREA22_BEATS:
            add('capture %s 823580' % beat, beat, 0x823580, [])
        for node in owners(ram, A22_BUGS):
            add('capture %s 12D940 @%X' % (beat, node), beat, 0x12D940, [node, node + 0x1F0])
        add('capture %s 1963A0' % beat, beat, 0x1963A0, [CAM, PLAYER])
        add('capture %s 18C850' % beat, beat, 0x18C850, [EYE, u32(ram, CAM + 0x14), F_0_2])
        add('capture %s 18C920 eye' % beat, beat, 0x18C920, [CAM + 0x10, EYE, F_0_2])
        add('capture %s 18C920 cam20' % beat, beat, 0x18C920, [PLAYER + 0xA0, CAM + 0x20, F_0_1])
        add('capture %s 183010' % beat, beat, 0x183010, [PLAYER, 0x700038A0])
        for callback in DOORS:
            for node in owners(ram, callback):
                add('capture %s 1BB310 @%X' % (beat, node), beat, 0x1BB310, [node])
        add('capture %s 1028E8' % beat, beat, 0x1028E8, [0x700038A0, EYE, TARGET])
    captured = len(cases)
    targeted_cases(add)
    targeted = len(cases) - captured

    # ---------------------------------------------------------------- perturbed
    for i in range(count):
        beat = rng.choice(BEATS)
        ram, _ = CAPTURES[beat]
        rf = lambda: rng.choice([rng.uniform(-400.0, 400.0), rng.uniform(150.0, 200.0), 176.9, 0.0])  # noqa: E731
        p = [(CAM + 1, b8(rng.choice([0, 1, 2, 3, 4, 5]))), (PLAYER + 0x230, w32(rng.choice([1, 6, 7, 8, 9, 0x15]))),
             (PLAYER + 0xA4, f32b(rf())), (CAM + 0x14, f32b(rf())), (CAM + 0x10, f32b(rf())),
             (PLAYER + 0xA0, f32b(rf())), (EYE, f32b(rf())), (EYE + 4, f32b(rf())), (CAM + 0x8C, f32b(rf()))]
        add('perturbed 1963A0 %d' % i, beat, 0x1963A0, [CAM, PLAYER], p)
        bugs = owners(ram, A22_BUGS)
        if bugs:
            node = rng.choice(bugs)
            sub = node + 0x1F0
            p = [(node + 6, b8(rng.choice([0, 1, 2, 3, 4]))), (sub + 0xE4, w32(rng.choice([0x100, 0x200, 0x101, 0x2]))),
                 (sub + 0xF4, h16(rng.choice([0, 0x1000, 0xFFFF]))), (sub + 0xF6, h16(rng.randrange(0x10000))),
                 (node + 0xC4, f32b(rng.uniform(-7, 7))), (node + 0x38, f32b(rng.uniform(-2, 2)))]
            q = {a: [rng.choice([0, 1, -1, 0x40, 0x80, 0xC0, 0x1F]) for _ in range(3)]
                 for a in (0x1C2770, 0x128640)}
            add('perturbed 12D940 %d' % i, beat, 0x12D940, [node, sub], p, queues=q)
    return cases, captured, targeted


def targeted_cases(add):
    """Designed cases for every state, step and test; all run by default."""
    beat = 'a22_00_door7'
    ram, spad = CAPTURES[beat]
    bug = owners(ram, A22_BUGS)[0]
    sub = bug + 0x1F0
    door10 = 0x7AAB70
    ALL = (0, 1, 2, -1) + WIDE_RESULTS
    no = lambda *addresses: RUN - set(addresses)   # noqa: E731

    # ---- 0x823580 the area init
    for fill in (0, 0xFFFFFFFF, 0x823E00):
        add('823580 fill %X' % fill, beat, 0x823580, [],
            [(0x275C18 + 4 * i, w32(fill)) for i in range(6)])

    # ---- 00100110 the soft-float sign (001274B0 runs as original code)
    tiny = dbl(1e-05)
    for value in (0.0, -0.0, 1e-05, 1.0000001e-05, 9.999999e-06, 1.0, -1.0, 3.4e38, -3.4e38, 1e-40,
                  float('inf'), float('-inf'), float('nan')):
        for extra in ((0, 0), (0xA5A5A5A5A5A5A5A5, 0x0123456789ABCDEF)):
            add('100110 %r %X' % (value, extra[0]), beat, 0x100110, [dbl(value), tiny, extra[0], extra[1]])
            add('100110 swapped %r %X' % (value, extra[0]), beat, 0x100110, [tiny, dbl(value), extra[0], extra[1]])
    add('100110 snan', beat, 0x100110, [0x7FF0000000000001, tiny, 0, 0])
    for result in (0, 1, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFF, 0x100000000, 0x8000000000000000, 0x7FFFFFFFFFFFFFFF,
                   0x80000000):
        add('100110 result %X' % result, beat, 0x100110, [dbl(1.0), tiny, 0, 0], queues={0x1274B0: [result]},
            run_set=no(0x1274B0))

    # ---- 001028E8 the VU0 multiply
    vec_a, vec_b, vec_o = 0x700038A0, 0x700038B0, 0x70003900
    lanes = [(1.5, -2.0, 0.25, 3.0), (1e30, 1e-30, -1e30, 7.0), (1e-20, 1e-20, 3e38, -3e38)]
    for i, (la, lb) in enumerate([(lanes[0], lanes[1]), (lanes[1], lanes[2]), (lanes[2], lanes[2])]):
        p = [(vec_a + 4 * k, f32b(v)) for k, v in enumerate(la)] + [(vec_b + 4 * k, f32b(v)) for k, v in enumerate(lb)]
        add('1028E8 lanes %d' % i, beat, 0x1028E8, [vec_o, vec_a, vec_b], spad=p)
    for j in range(0, len(SPECIAL), 4):
        for k in range(0, len(SPECIAL), 4):
            p = [(vec_a + 4 * n, bits32(SPECIAL[j + n])) for n in range(4)]
            p += [(vec_b + 4 * n, bits32(SPECIAL[k + n])) for n in range(4)]
            add('1028E8 special %d %d' % (j, k), beat, 0x1028E8, [vec_o, vec_a, vec_b], spad=p)
            p = [(vec_a + 4 * n, bits32(SPECIAL[j + n])) for n in range(4)]
            p += [(vec_b + 4 * n, bits32([0x3F800000, 0x7F7FFFFF, 0x00800000, 0xBF800000][n])) for n in range(4)]
            add('1028E8 special x consts %d %d' % (j, k), beat, 0x1028E8, [vec_o, vec_a, vec_b], spad=p)
    add('1028E8 alias out=a', beat, 0x1028E8, [vec_a, vec_a, vec_b])
    add('1028E8 alias out=b', beat, 0x1028E8, [vec_b, vec_a, vec_b])
    add('1028E8 alias all', beat, 0x1028E8, [vec_a, vec_a, vec_a])
    add('1028E8 unaligned', beat, 0x1028E8, [vec_o + 0xC, vec_a + 4, vec_b + 9])
    add('1028E8 ram', beat, 0x1028E8, [PLAYER + 0x100, PLAYER + 0xA0, PLAYER + 0xB0])
    add('1028E8 unmapped a', beat, 0x1028E8, [vec_o, 0x02000000, vec_b])
    add('1028E8 unmapped b', beat, 0x1028E8, [vec_o, vec_a, 0x40000010])
    add('1028E8 unmapped out', beat, 0x1028E8, [0x02000000, vec_a, vec_b])

    # ---- 00183010 the move by a vector (001028B8 / 00102948 run as original code)
    for d in ((0.7, 0.0, -0.7, 0.0), (-1e-3, 5.0, 2.5, 1.0), (3e38, 3e38, -3e38, 0.0)):
        add('183010 delta %r' % (d,), beat, 0x183010, [PLAYER, 0x700038A0],
            spad=[(0x700038A0 + 4 * k, f32b(v)) for k, v in enumerate(d)])
    add('183010 other actor', beat, 0x183010, [bug, 0x700038A0])
    add('183010 delta in actor', beat, 0x183010, [PLAYER, PLAYER + 0xB0])
    for result in (0, 1):
        add('183010 stub %d' % result, beat, 0x183010, [PLAYER, 0x700038A0],
            queues={0x1028B8: [Scribble(0, [(PLAYER + 0xC0, f32b(9.0)), (0x70003B40, f32b(1.0))])]},
            run_set=no(0x1028B8))

    # ---- 0018C850 the y chase (0011DF78 runs as original code unless stubbed)
    for cur, target, step in ((10.0, 10.2, 0.2), (10.0, 10.3, 0.2), (10.0, 9.7, 0.2), (10.0, 10.0, 0.2),
                              (10.0, 200.0, 0.2), (200.0, -200.0, 0.3), (0.0, 1.0, 0.0), (0.0, -1.0, -0.0),
                              (5.0, 6.0, -0.5), (1.0, 3.0, 2.0), (1.0, 2.0, 4.0), (1.0, 1e-40, 0.2)):
        add('18C850 %r %r %r' % (cur, target, step), beat, 0x18C850, [EYE, fb(target), fb(step)],
            [(EYE + 4, f32b(cur))])
    for tb in (NAN, SNAN, POS_INF, NEG_INF, FMAX, 0x80000000):
        add('18C850 target %08X' % tb, beat, 0x18C850, [EYE, tb, F_0_2])
        add('18C850 cur %08X' % tb, beat, 0x18C850, [EYE, fb(1.0), F_0_2], [(EYE + 4, bits32(tb))])
        add('18C850 step %08X' % tb, beat, 0x18C850, [EYE, fb(1.0), tb], [(EYE + 4, f32b(0.0))])
    for fab in (F_0_2, up(0.2), down(0.2), NAN, 0x80000000, POS_INF, 0, fb(1000.0), fb(0.4), up(0.4), down(0.4)):
        for d in (1.0, -1.0):
            add('18C850 fabs %08X d %g' % (fab, d), beat, 0x18C850, [EYE, fb(d), F_0_2],
                [(EYE + 4, f32b(0.0))], queues={0x11DF78: [fab]}, run_set=no(0x11DF78))
    add('18C850 scribble fabs', beat, 0x18C850, [EYE, fb(5.0), F_0_2], [(EYE + 4, f32b(0.0))],
        queues={0x11DF78: [Scribble(fb(5.0), [(EYE + 4, f32b(2.0))])]}, run_set=no(0x11DF78))
    add('18C850 scribble fabs snap', beat, 0x18C850, [EYE, fb(0.1), F_0_2], [(EYE + 4, f32b(0.0))],
        queues={0x11DF78: [Scribble(fb(0.1), [(EYE + 4, f32b(7.0))])]}, run_set=no(0x11DF78))
    for cur, target in ((0.0, 0.0), (-0.0, -0.0), (0.0, -0.0), (-0.0, 0.0)):   # d = +-0 with a negative step
        add('18C850 zero d %r %r' % (cur, target), beat, 0x18C850, [EYE, fb(target), fb(-0.5)],
            [(EYE + 4, f32b(cur))])
    for step in (1, 0x80000001, 0x80000000):   # a step whose compare key is 0 (the v <-> step tie)
        for cur in (0.0, -0.0, 3.0):
            add('18C850 tiny step %08X cur %r' % (step, cur), beat, 0x18C850, [EYE, fb(1.0), step],
                [(EYE + 4, f32b(cur))])
            add('18C850 tiny step %08X cur %r down' % (step, cur), beat, 0x18C850, [EYE, fb(-1.0), step],
                [(EYE + 4, f32b(cur))])
    add('18C850 unmapped', beat, 0x18C850, [0x02000000, fb(1.0), F_0_2])
    add('18C850 misaligned', beat, 0x18C850, [EYE + 2, fb(1.0), F_0_2])

    # ---- 0018C920 the x / z chase
    for gx, gz, cx, cz, step in ((1.0, 1.0, 1.1, 0.9, 0.2), (1.0, 5.0, 1.0, 1.0, 0.2), (5.0, 1.0, 1.0, 1.0, 0.2),
                                 (-5.0, -5.0, 5.0, 5.0, 0.1), (0.0, 0.0, 0.0, 0.0, 0.0), (1.0, 2.0, 1.2, 1.8, 0.2),
                                 (1.0, 1.0, 3.0, 3.0, 4.0), (1.0, 1.0, 3.0, 3.0, -1.0)):
        add('18C920 %r' % ((gx, gz, cx, cz, step),), beat, 0x18C920, [CAM + 0x10, EYE, fb(step)],
            [(CAM + 0x10, f32b(gx)), (CAM + 0x18, f32b(gz)), (EYE, f32b(cx)), (EYE + 8, f32b(cz))])
    for tb in (NAN, POS_INF, NEG_INF, FMAX, 0x80000000):
        add('18C920 goal %08X' % tb, beat, 0x18C920, [CAM + 0x10, EYE, F_0_2], [(CAM + 0x10, bits32(tb))])
        add('18C920 cur z %08X' % tb, beat, 0x18C920, [CAM + 0x10, EYE, F_0_2], [(EYE + 8, bits32(tb))])
    for fab in (F_0_2, up(0.2), down(0.2), NAN, 0x80000000):
        add('18C920 fabs %08X' % fab, beat, 0x18C920, [CAM + 0x10, EYE, F_0_2],
            [(CAM + 0x10, f32b(3.0)), (EYE, f32b(1.0)), (CAM + 0x18, f32b(-3.0)), (EYE + 8, f32b(1.0))],
            queues={0x11DF78: [fab, fab]}, run_set=no(0x11DF78))
    add('18C920 scribble goal', beat, 0x18C920, [CAM + 0x10, EYE, F_0_2],
        [(CAM + 0x10, f32b(1.0)), (EYE, f32b(1.0)), (CAM + 0x18, f32b(1.0)), (EYE + 8, f32b(1.0))],
        queues={0x11DF78: [Scribble(0, [(CAM + 0x10, f32b(9.0)), (CAM + 0x18, f32b(8.0))]),
                           Scribble(0, [(CAM + 0x18, f32b(-8.0))])]}, run_set=no(0x11DF78))
    add('18C920 scribble cur', beat, 0x18C920, [CAM + 0x10, EYE, F_0_2],
        [(CAM + 0x10, f32b(5.0)), (EYE, f32b(1.0)), (CAM + 0x18, f32b(5.0)), (EYE + 8, f32b(1.0))],
        queues={0x11DF78: [Scribble(fb(4.0), [(EYE, f32b(2.0)), (EYE + 8, f32b(3.0))]), fb(4.0)]},
        run_set=no(0x11DF78))
    add('18C920 unmapped z', beat, 0x18C920, [0x1FFFFFC, EYE, F_0_2])
    add('18C920 unmapped vec', beat, 0x18C920, [CAM + 0x10, 0x40000000, F_0_2])

    # ---- 001963A0 camera action 12
    heights = (fb(176.9), up(176.9), down(176.9), fb(155.0), fb(239.0), NAN, NEG_INF, POS_INF, fb(300.0))
    for st in (0, 1, 2, 3, 4, 5, 0xFF):
        for y in heights:
            for mode in (1, 6, 7, 8, 9, 0x109, 0xFFFFFFFF, 12):
                if y not in (fb(176.9), up(176.9), fb(155.0), fb(239.0)) and mode not in (1, 9):
                    continue
                add('1963A0 s%X y %08X mode %X' % (st, y, mode), beat, 0x1963A0, [CAM, PLAYER],
                    [(CAM + 1, b8(st)), (PLAYER + 0xA4, bits32(y)), (PLAYER + 0x230, w32(mode))])
    for cur in (0.0, 266.0, 265.99998, 1e30, -1e30, 3e38):
        for y in (223.99998, 224.0, 224.00002, 239.0, 1e30, -3e38):
            add('1963A0 raise %r %r' % (cur, y), beat, 0x1963A0, [CAM, PLAYER],
                [(CAM + 1, b8(4)), (CAM + 0x14, f32b(cur)), (PLAYER + 0xA4, f32b(y)), (PLAYER + 0x230, w32(1))])
    for st in (1, 3):   # the eye's x / z chase toward +0x10 at distances around its 0.2 step
        for dx, dz in ((0.25, -0.25), (0.2, 0.1), (-5.0, 3.0), (0.15, 0.35)):
            add('1963A0 s%d eye chase %r %r' % (st, dx, dz), beat, 0x1963A0, [CAM, PLAYER],
                [(CAM + 1, b8(st)), (PLAYER + 0xA4, f32b(239.0 if st == 3 else 155.0)),
                 (EYE, f32b(335.1 + dx)), (EYE + 8, f32b(359.97 + dz)),
                 (CAM + 0x10, f32b(335.1)), (CAM + 0x18, f32b(359.97))])
    for y in (0.0, -42.0, 1.0, -41.99999):   # 42 + y exact: the constant's last bit shows
        add('1963A0 raise small %r' % y, beat, 0x1963A0, [CAM, PLAYER],
            [(CAM + 1, b8(4)), (CAM + 0x14, f32b(0.0)), (PLAYER + 0xA4, f32b(y)), (PLAYER + 0x230, w32(1))])
    for mode in (8, 1):   # |d| exactly 0.1: the 0.1 step's last bit shows (snap against step)
        for d in (0.1, -0.1, 0.2, 0.05):
            add('1963A0 chase 0.1 mode %d d %r' % (mode, d), beat, 0x1963A0, [CAM, PLAYER],
                [(CAM + 1, b8(0xFF)), (PLAYER + 0x230, w32(mode)), (PLAYER + 0xA0, f32b(d)), (CAM + 0x20, f32b(0.0)),
                 (PLAYER + 0xA8, f32b(-d)), (CAM + 0x28, f32b(0.0))])
    add('1963A0 raise NaN', beat, 0x1963A0, [CAM, PLAYER],
        [(CAM + 1, b8(3)), (CAM + 0x14, bits32(NAN)), (PLAYER + 0xA4, f32b(239.0))])
    for b4, l8c in ((165.9, 6.0), (NAN, 2.0), (3e38, 3e38), (-0.0, 0.0)):
        add('1963A0 tail %r %r' % (b4, l8c), beat, 0x1963A0, [CAM, PLAYER],
            [(PLAYER + 0xB4, f32b(b4) if isinstance(b4, float) else bits32(b4)), (CAM + 0x8C, f32b(l8c))])
    # re-reads after calls (the callees scribble what the original reads again)
    add('1963A0 s1 scribble a4 in 18C850', beat, 0x1963A0, [CAM, PLAYER],
        [(CAM + 1, b8(1)), (PLAYER + 0xA4, f32b(155.0))],
        queues={0x11DF78: [fb(0.0), fb(0.0), Scribble(fb(9.0), [(PLAYER + 0xA4, f32b(239.0))])]},
        run_set=no(0x11DF78))
    add('1963A0 s1 scribble 14 in 18C920', beat, 0x1963A0, [CAM, PLAYER],
        [(CAM + 1, b8(1)), (PLAYER + 0xA4, f32b(155.0))],
        queues={0x11DF78: [fb(0.0), Scribble(fb(0.0), [(CAM + 0x14, f32b(50.0))]), fb(9.0)]},
        run_set=no(0x11DF78))
    add('1963A0 s2 scribble a4', beat, 0x1963A0, [CAM, PLAYER],
        [(CAM + 1, b8(2)), (PLAYER + 0xA4, f32b(155.0))],
        queues={0x102948: [Scribble(0, [(PLAYER + 0xA4, f32b(239.0)), (CAM + 0x14, f32b(1.0))])]},
        run_set=no(0x102948))
    add('1963A0 s3 scribble', beat, 0x1963A0, [CAM, PLAYER],
        [(CAM + 1, b8(3)), (PLAYER + 0xA4, f32b(239.0))],
        queues={0x11DF78: [fb(0.0), Scribble(fb(0.0), [(CAM + 0x14, f32b(7.0))]),
                           Scribble(fb(9.0), [(PLAYER + 0xA4, f32b(100.0))])]}, run_set=no(0x11DF78))
    add('1963A0 mode9 scribble 24', beat, 0x1963A0, [CAM, PLAYER],
        [(PLAYER + 0xA4, f32b(239.0)), (PLAYER + 0x230, w32(9))],
        queues={0x18C6A0: [Scribble(0, [(CAM + 0x24, f32b(12.0))])]}, run_set=no(0x18C6A0))
    add('1963A0 tail scribble', beat, 0x1963A0, [CAM, PLAYER],
        [(PLAYER + 0x230, w32(6))],
        queues={0x18C6A0: [Scribble(0, [(PLAYER + 0xB4, f32b(1.0)), (CAM + 0x8C, f32b(2.0))])]},
        run_set=no(0x18C6A0))
    add('1963A0 tail scribble 4B0', beat, 0x1963A0, [CAM, PLAYER], [(PLAYER + 0x230, w32(6))],
        queues={0x18C4B0: [Scribble(0, [(CAM + 0x20, f32b(1.0))])]}, run_set=no(0x18C4B0))
    add('1963A0 mode scribble in state', beat, 0x1963A0, [CAM, PLAYER], [(CAM + 1, b8(2))],
        queues={0x102948: [Scribble(0, [(PLAYER + 0x230, w32(8))])]}, run_set=no(0x102948))
    add('1963A0 unmapped player', beat, 0x1963A0, [CAM, 0x1FFFFF80], [(CAM + 1, b8(0))])
    add('1963A0 unmapped cam', beat, 0x1963A0, [0x02000000, PLAYER])
    add('1963A0 unmapped 230', beat, 0x1963A0, [CAM, 0x1FFFFF00], [(CAM + 1, b8(0xFF))])

    # ---- 001BB310 the door program's camera callback
    for yaw in (0.0, 3.1415927, -1.5707964, 1e20, -0.0):
        add('1BB310 yaw %r' % yaw, 'a22_s2_door10_locked', 0x1BB310, [door10], [(0x810374, f32b(yaw))])
    for yb in (NAN, SNAN, POS_INF, FMAX):
        add('1BB310 yaw %08X' % yb, 'a22_s2_door10_locked', 0x1BB310, [door10], [(0x810374, bits32(yb))])
    for xyz in ((3e38, -3e38, 3e38), (-3e38, 3e38, -3e38), (0.0, -5.0, 0.0)):
        add('1BB310 player %r' % (xyz,), 'a22_s2_door10_locked', 0x1BB310, [door10],
            [(0x810360 + 4 * k, f32b(v)) for k, v in enumerate(xyz)])
    add('1BB310 link other', 'a22_s2_door10_locked', 0x1BB310, [door10], [(door10 + 0x1C, w32(PLAYER))])
    add('1BB310 link unmapped', 'a22_s2_door10_locked', 0x1BB310, [door10], [(door10 + 0x1C, w32(0x02000000))])
    add('1BB310 link misaligned', 'a22_s2_door10_locked', 0x1BB310, [door10], [(door10 + 0x1C, w32(PLAYER + 2))])
    add('1BB310 scribble copy', 'a22_s2_door10_locked', 0x1BB310, [door10],
        queues={0x102948: [Scribble(0, [(0x810374, f32b(1.0)), (0x810360, f32b(2.0)), (0x810364, f32b(3.0)),
                                        (0x810368, f32b(4.0))])]}, run_set=no(0x102948))
    add('1BB310 scribble sin', 'a22_s2_door10_locked', 0x1BB310, [door10],
        queues={0x11E2A8: [Scribble(fb(0.5), [(0x810374, f32b(1.0)), (0x810360, f32b(2.0)), (0x810364, f32b(3.0)),
                                              (0x810368, f32b(4.0)), (0x700038A0, f32b(5.0))])]},
        run_set=no(0x11E2A8))
    add('1BB310 scribble cos', 'a22_s2_door10_locked', 0x1BB310, [door10],
        queues={0x11DE90: [Scribble(fb(0.5), [(0x810368, f32b(4.0)), (0x700038A4, f32b(6.0))])]},
        run_set=no(0x11DE90))

    # ---- 0012D940 the bug node's +5 = 10 / 11 step
    def bug_case(label, patches, queues=None, run_set=RUN):
        add('12D940 ' + label, beat, 0x12D940, [bug, sub], patches, queues=queues, run_set=run_set)
    for st in (0, 1, 2, 3, 4, 5, 0xFF):
        for chk in (0, 1, -1, 0x80000000):
            bug_case('s%X chk %X' % (st, chk & MASK), [(bug + 6, b8(st)), (sub + 0xF4, h16(0x1000))],
                     queues={0x1C2770: [chk]})
    for f6 in (0, 3, 4, 7, 0x84, 0xFFFF, 0xFFF8, 0x8003):
        for r in (0, 0x10, 0x1F, 0x0F, 0x7FFFFFE0, -1):
            bug_case('s0 f6 %X rand %X' % (f6, r & MASK), [(bug + 6, b8(0)), (sub + 0xF6, h16(f6))],
                     queues={0x122BB8: [r]}, run_set=no(0x122BB8))
    for yaw in (3.1415927, -3.1415927, 0.0, 6.0, -9.5):
        bug_case('s0 yaw %r' % yaw, [(bug + 6, b8(0)), (0x810374, f32b(yaw)), (sub + 0xF6, h16(4))])
    # (the original's angle wrap 001B1470 loops for ever on 1e20 or a NaN
    # pattern, which the EE compares as a huge number: stubbed there)
    for yb in (fb(1e20), NAN):
        bug_case('s0 yaw %08X' % yb, [(bug + 6, b8(0)), (0x810374, bits32(yb))],
                 queues={0x1B1470: [fb(0.25)]}, run_set=no(0x1B1470))
    bug_case('s0 scribble rand', [(bug + 6, b8(0))],
             queues={0x122BB8: [Scribble(5, [(bug + 0xC4, f32b(1.0)), (bug + 6, b8(9))])]}, run_set=no(0x122BB8))
    bug_case('s0 scribble 12E070', [(bug + 6, b8(0))],
             queues={0x12E070: [Scribble(0, [(bug + 6, b8(7)), (sub + 0xD8, f32b(2.0))])]}, run_set=no(0x12E070))
    bug_case('s0 scribble 1287F0', [(bug + 6, b8(0))],
             queues={0x1287F0: [Scribble(0, [(bug + 6, b8(0x20))])]})
    bug_case('s0 scribble wrap', [(bug + 6, b8(0))],
             queues={0x1B1470: [Scribble(fb(0.5), [(bug + 0xC4, f32b(2.0))])]}, run_set=no(0x1B1470))
    for e4 in (0x100, 0x200, 0x101, 0x10F, 0x2, 0x1100, 0x80000100):
        for r in (0x00, 0x40, 0x80, 0xC0, 0xFFFFFF3F, 0x7FFFFFFF):
            if e4 != 0x100 and r:
                continue
            for res in (0, 1, -1):
                if e4 == 0x100 and res:
                    continue
                bug_case('s1 e4 %X rand %X r %d' % (e4, r, res), [(bug + 6, b8(1)), (sub + 0xE4, w32(e4))],
                         queues={0x122BB8: [r], 0x128640: [res]}, run_set=no(0x122BB8))
    for speed, yaw in ((0.3, 0.0), (-2.0, 1.5), (3e38, 1.0), (0.0, 3.0)):
        bug_case('s1 speed %r yaw %r' % (speed, yaw),
                 [(bug + 6, b8(1)), (bug + 0x38, f32b(speed)), (bug + 0xC4, f32b(yaw))])
    bug_case('s1 scribble sin', [(bug + 6, b8(1))],
             queues={0x11E2A8: [Scribble(fb(0.5), [(bug + 0x38, f32b(2.0)), (bug + 0xB0, f32b(3.0)),
                                                   (bug + 0xC4, f32b(1.0))])]}, run_set=no(0x11E2A8))
    bug_case('s1 scribble cos', [(bug + 6, b8(1))],
             queues={0x11DE90: [Scribble(fb(0.5), [(bug + 0x38, f32b(2.0)), (bug + 0xB8, f32b(3.0))])]},
             run_set=no(0x11DE90))
    bug_case('s1 scribble 1B5360', [(bug + 6, b8(1)), (sub + 0xE4, w32(0x200))],
             queues={0x1B5360: [Scribble(0, [(sub + 0xE4, w32(0x100))])], 0x122BB8: [0]}, run_set=no(0x122BB8))
    bug_case('s1 scribble clip', [(bug + 6, b8(1)), (sub + 0xE4, w32(0x100))],
             queues={0x122BB8: [0], 0x1287F0: [Scribble(0, [(bug + 6, b8(0x30))])]}, run_set=no(0x122BB8))
    bug_case('s1 scribble rand', [(bug + 6, b8(1)), (sub + 0xE4, w32(0x100))],
             queues={0x122BB8: [Scribble(0x80, [(bug + 6, b8(0x31))])]}, run_set=no(0x122BB8))
    for st in (2, 3):
        for f4 in (0x1000, 0xEFFF, 0xF000, 0x0FFF, 0):
            for res in (0, 1, -1):
                bug_case('s%d f4 %X r %d' % (st, f4, res), [(bug + 6, b8(st)), (sub + 0xF4, h16(f4))],
                         queues={0x128640: [res]})
        for yaw in (0.0, 3.1415927, -3.1415927, 9.0):
            bug_case('s%d yaw %r' % (st, yaw), [(bug + 6, b8(st)), (sub + 0xF4, h16(0x1000)),
                                                (bug + 0xC4, f32b(yaw))])
        bug_case('s%d scribble land' % st, [(bug + 6, b8(st)), (sub + 0xF4, h16(0x1000))],
                 queues={0x128830: [Scribble(0, [(bug + 0xC4, f32b(2.0)), (bug + 5, b8(9))])]},
                 run_set=no(0x128830))
        bug_case('s%d scribble clip' % st, [(bug + 6, b8(st)), (sub + 0xF4, h16(0x1000))],
                 queues={0x1287F0: [0, Scribble(0, [(bug + 0xC4, f32b(-2.0))])]})
    bug_case('scribble state', [(bug + 6, b8(1))], queues={0x1C2770: [Scribble(1, [(bug + 6, b8(3)),
                                                                                    (sub + 0xF4, h16(0x1000))])]})
    bug_case('unmapped sub', [(bug + 6, b8(3))])
    add('12D940 unmapped sub', beat, 0x12D940, [bug, 0x02000000], [(bug + 6, b8(3))])
    add('12D940 unmapped self', beat, 0x12D940, [0x02000000, sub])


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


DEFAULT_ARGS = {
    0x100110: [dbl(1.0), dbl(1e-05), 0, 0],
    0x1028E8: [0x70003900, 0x700038A0, 0x700038B0],
    0x12D940: [0x7A6AD0, 0x7A6AD0 + 0x1F0],
    0x183010: [PLAYER, 0x700038A0],
    0x18C850: [EYE, fbits(1.0), F_0_2],
    0x18C920: [CAM + 0x10, EYE, F_0_2],
    0x1963A0: [CAM, PLAYER],
    0x1BB310: [0x7AAB70],
    0x823580: [],
}


# Words the static walk reaches that no input can: the delay slot of the
# default branch of 0012D940's (rand & 0xC0) >> 6 switch (the value is 0..3).
DEAD_WORDS = {0x12DB6C: '0012D940 kind switch default'}


def fault_checks():
    """The native fail-stop contract: a NULL hook, a failing hook, an
    unmapped address and a fault latched before the call (on door [10]
    0x7AAB70 through 001BB310: 00102948, 0011E2A8, 0011DE90, 00102948),
    then every entry with a latched fault, a NULL hook table, a NULL fault
    pointer and (entries with a result) a NULL result pointer."""
    ram, spad = CAPTURES['a22_s2_door10_locked']
    node = 0x7AAB70
    problems = []
    calls_want = ['w_00102948', 'w_0011E2A8', 'w_0011DE90', 'w_00102948']

    def attempt(null=None, failing=None, unmapped=None, latched=False):
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
        for name, _, args, result in HOOKS:
            def fn(_ctx, *values, name=name, result=result):
                calls.append(name)
                if result:
                    values[-1][0] = 1
                return -1 if name == failing else 0
            fields[name] = hook_proto(args, result)(fn) if name != null else hook_proto(args, result)()
            keep.append(fields[name])
        hooks = Hooks(**fields)
        fault = Fault(0x1234, 7) if latched else Fault()
        out = C.c_int32(0x5A5A5A5A)
        before = C.string_at(base, RAM_SIZE) + C.string_at(sbase, SPAD_SIZE)
        status = NATIVE.em_area22_port_001BB310(C.byref(hooks), node, C.byref(out), C.byref(fault))
        same = before == C.string_at(base, RAM_SIZE) + C.string_at(sbase, SPAD_SIZE)
        return status, fault.address, fault.code, calls, same, out.value

    status, address, code, calls, _, out = attempt()
    if (status, address, code, out) != (0, 0, 0, 1) or calls != calls_want:
        problems.append(('fault-check baseline', status, hex(address), code, calls, out))
    status, address, code, calls, _, out = attempt(null='w_0011DE90')
    if (status, address, code) != (-1, 0x11DE90, 1) or calls != calls_want[:2] or out != 0x5A5A5A5A:
        problems.append(('NULL hook', status, hex(address), code, calls))
    status, address, code, calls, same, out = attempt(failing='w_00102948')
    if (status, address, code) != (-1, 0x102948, 2) or calls != calls_want[:1] or out != 0x5A5A5A5A:
        problems.append(('failing hook', status, hex(address), code, calls, same))
    status, address, code, calls, _, out = attempt(unmapped=0x810368)
    if (status, address, code) != (-1, 0x810368, 5) or calls != calls_want[:3]:
        problems.append(('unmapped address', status, hex(address), code, calls))
    status, address, code, calls, same, _ = attempt(unmapped=node + 0x1C)
    if (status, address, code) != (-1, node + 0x1C, 5) or calls or not same:
        problems.append(('unmapped address before any call', status, hex(address), code, calls, same))
    status, address, code, calls, same, out = attempt(latched=True)
    if (status, address, code) != (-1, 0x1234, 7) or calls or not same or out != 0x5A5A5A5A:
        problems.append(('latched fault', status, hex(address), code, calls, same))
    if NATIVE.em_area22_port_001BB310(None, node, C.byref(C.c_int32()), C.byref(Fault())) != -1:
        problems.append(('NULL hooks accepted',))
    # every entry: a fault latched on entry, a NULL hook table, a NULL fault
    # pointer or a NULL result pointer returns -1 at once, with no call
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


# ----------------------------------------------------------------------------
# reuse_checks: the three census rows with an existing translation
# ----------------------------------------------------------------------------
# 001A44B0 (em_coll_probe_001A44B0), 001A5C30 (em_coll_segment_001A5C30) and
# 001A4830 (em_coll_move_prim_001A4830) are the round-prim tests of the
# collision walkers; the AREA22 drums (00156620 placements [14]..[16]) are
# the first round prims the route meets. Each check puts a segment into the
# captured scratchpad (0x70003190 start, 0x700031A0 end), runs the ORIGINAL
# function on the drum's prim in the captured RAM, and runs the existing
# translation on the same prim bytes with its state loaded from the same
# scratchpad; the translation's state is written back to a scratchpad copy
# and the whole scratchpad and the return value must equal the original's,
# and the original must not have written RAM. The bridge is generated into
# build/area22/port (a test artifact, not port code).
REUSE_BRIDGE = r"""
#include <string.h>
#include "game/em_coll_probe_original.h"
#include "game/em_coll_move_original.h"
#include "game/em_coll_segment_walkers.h"
#include "game/em_sdk_math_original.h"

static EmSdkMathTables tables;
static int32_t mode;
static EmSdkMathContext math;

int reuse_init(const uint8_t *elf, uint32_t size, int32_t mode_word)
{
    if (em_sdk_math_original_load_tables(elf, size, &tables)) return -1;
    mode = mode_word;
    math.tables = &tables;
    math.world.d26C5D0 = &mode;
    return 0;
}

static void rd(const uint8_t *spad, uint32_t a, void *out, size_t n) { memcpy(out, spad + (a - 0x70000000u), n); }
static void wr(uint8_t *spad, uint32_t a, const void *in, size_t n) { memcpy(spad + (a - 0x70000000u), in, n); }

/* which: 0 = 001A44B0, 1 = 001A5C30, 2 = 001A4830. Returns the translation's
 * result (1 / 0 / -1); `spad` is updated with the state it wrote. */
int reuse_run(int which, const uint8_t *prim, uint8_t *spad)
{
    if (which < 2) {
        EmCollProbeState s;
        memset(&s, 0, sizeof s);
        rd(spad, 0x70003190u, s.start, 16);
        rd(spad, 0x700031A0u, s.end, 16);
        rd(spad, 0x700031B0u, s.point, 12);
        rd(spad, 0x700031C0u, s.delta, 12);
        rd(spad, 0x700030CAu, &s.cell_class, 2);
        rd(spad, 0x700030D4u, s.cell_normal, 12);
        rd(spad, 0x70003680u, &s.ratio, 4);
        int r = which == 0 ? em_coll_probe_001A44B0(prim, &s) : em_coll_segment_001A5C30(&math, prim, &s);
        wr(spad, 0x70003190u, s.start, 16);
        wr(spad, 0x700031A0u, s.end, 16);
        wr(spad, 0x700031B0u, s.point, 12);
        wr(spad, 0x700031C0u, s.delta, 12);
        wr(spad, 0x700030CAu, &s.cell_class, 2);
        wr(spad, 0x700030D4u, s.cell_normal, 12);
        wr(spad, 0x70003680u, &s.ratio, 4);
        return r;
    }
    EmCollMoveScratch s;
    EmCollMoveWorld w;
    memset(&s, 0, sizeof s);
    memset(&w, 0, sizeof w);
    w.math = &math;
    rd(spad, 0x70003190u, s.start, 16);
    rd(spad, 0x700031A0u, s.end, 16);
    rd(spad, 0x700031B0u, s.point, 16);
    rd(spad, 0x700031C0u, s.delta, 16);
    rd(spad, 0x700030CAu, &s.cell_class, 2);
    rd(spad, 0x700030CCu, &s.cell_word_1c, 4);
    rd(spad, 0x700030D0u, &s.cell_word_20, 4);
    rd(spad, 0x700030D4u, s.cell_normal, 12);
    rd(spad, 0x70003680u, s.work, 16);
    int r = em_coll_move_prim_001A4830(&w, &s, prim);
    wr(spad, 0x70003190u, s.start, 16);
    wr(spad, 0x700031A0u, s.end, 16);
    wr(spad, 0x700031B0u, s.point, 16);
    wr(spad, 0x700031C0u, s.delta, 16);
    wr(spad, 0x700030CAu, &s.cell_class, 2);
    wr(spad, 0x700030CCu, &s.cell_word_1c, 4);
    wr(spad, 0x700030D0u, &s.cell_word_20, 4);
    wr(spad, 0x700030D4u, s.cell_normal, 12);
    wr(spad, 0x70003680u, s.work, 16);
    return r;
}
"""
REUSE_SOURCES = ['src/game/em_coll_probe_original.c', 'src/game/em_coll_segment_walkers.c',
                 'src/game/em_coll_move_original.c', 'src/game/em_coll_grid_hull.c', 'src/game/em_sdk_math_original.c',
                 'src/game/em_actor_collision.c', 'src/game/em_collision.c', 'src/game/em_actor_pool.c',
                 'src/game/em_effect_original.c']
REUSE = {0: (0x1A44B0, 'em_coll_probe_original.c'), 1: (0x1A5C30, 'em_coll_segment_walkers.c'),
         2: (0x1A4830, 'em_coll_move_original.c')}
MODE_WORD = 0x26C5D0   # D_0026C5D0, the SDK math error mode 0011E748 reads
REUSE_NATIVE = None


def build_reuse(elf):
    source = OUT / 'reuse_bridge.c'
    source.write_text(REUSE_BRIDGE)
    lib = OUT / ('reuse_bridge.dylib' if sys.platform == 'darwin' else 'reuse_bridge.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared', '-fPIC',
                    '-Isrc', str(source)] + REUSE_SOURCES + ['-lm', '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    native.reuse_init.argtypes = [C.c_char_p, C.c_uint32, C.c_int32]
    native.reuse_run.argtypes = [C.c_int, C.c_char_p, C.c_void_p]
    return native


def prim_size(buf, at):
    h = struct.unpack_from('<H', buf, at)[0]
    t, n = h & 0xF000, buf[at + 2]
    if t == 0x8000: return 0x24 if h & 0x800 else 0x14   # noqa: E701
    if t == 0x4000: return 0x2C if h & 0x800 else 0x18   # noqa: E701
    if t == 0x2000: return 0x1C                          # noqa: E701
    if t == 0x1000: return 0x24 + 0x30 * n if h & 0x800 else 0x14 + 0x18 * n   # noqa: E701
    raise AssertionError(('unknown prim', hex(h)))


def round_prims(ram, spad):
    """The 0x4000 / 0x8000 prims of the captured cell directory (*0x70003250)."""
    base = u32(spad, 0x3250)
    found = []
    for uid in range(u32(ram, base)):
        word = u32(ram, base + 4 + 4 * uid)
        if not word:
            continue
        hull = base + (word & 0x3FFFFFFF)
        at = hull + 0x1C
        for _ in range(struct.unpack_from('<h', ram, hull + 0x18)[0]):
            if struct.unpack_from('<H', ram, at)[0] & 0xF000 in (0x4000, 0x8000):
                found.append(at)
            at += prim_size(ram, at)
    return found


def reuse_segments(ram, prim, rng):
    """Segments around one round prim: horizontal crossings at heights
    across the drum's span and its caps, at offsets across the rim and in
    eight directions (long and one-frame short), vertical segments through
    the caps at radii across the rim and at the 1e-5 thresholds of
    001A5C30, and random ones nearby."""
    cx, cy, cz, r = struct.unpack_from('<4f', ram, prim + 4)
    half = struct.unpack_from('<f', ram, prim + 0x14)[0] if struct.unpack_from('<H', ram, prim)[0] & 0x4000 else r
    segs = []
    for dy in (-half - 0.5, -half, -half + 0.01, 0.0, half - 0.01, half, half + 0.5):
        for k in range(8):
            a = k * math.pi / 4 + 0.1
            ux, uz = math.cos(a), math.sin(a)
            for off in (0.0, r * 0.5, r - 0.01, r, r + 0.01):
                px, pz = cx - uz * off, cz + ux * off
                segs.append(((px - ux * (r + 3), cy + dy, pz - uz * (r + 3)), (px + ux * (r + 3), cy + dy, pz + uz * (r + 3))))
                segs.append(((px - ux * (r + 0.1), cy + dy, pz - uz * (r + 0.1)), (px - ux * (r - 0.2), cy + dy, pz - uz * (r - 0.2))))
    for rad in (0.0, r * 0.5, r - 0.001, r, r + 0.001):
        for d in (0.0, 1e-6, 1e-5, 2e-5):
            for top in (True, False):
                x, z = cx + rad, cz
                y0, y1 = (cy + half + 2, cy + half - 1) if top else (cy - half - 2, cy - half + 1)
                segs.append(((x, y0, z), (x + d, y1, z + d)))
                segs.append(((x, y1, z), (x - d, y0, z)))
    for _ in range(40):
        s = (cx + rng.uniform(-8, 8), cy + rng.uniform(-8, 8), cz + rng.uniform(-8, 8))
        e = (cx + rng.uniform(-8, 8), cy + rng.uniform(-8, 8), cz + rng.uniform(-8, 8))
        segs.append((s, e))
    return segs


REUSE_RAM = {}   # per worker process: beat -> WatchedMemory copy of the capture (never written: checked)


def reuse_case(item):
    """One segment against one prim through one function: ('ok', the
    return value, the class the original wrote at 0x700030CA on a hit), or
    the difference."""
    beat, which, prim, seg = item
    ram, spad = CAPTURES[beat]
    spad = bytearray(spad)
    if seg is not None:
        struct.pack_into('<4f', spad, 0x3190, *seg[0], 1.0)
        struct.pack_into('<4f', spad, 0x31A0, *seg[1], 1.0)
    if beat not in REUSE_RAM:
        REUSE_RAM[beat] = WatchedMemory(RAM_SIZE)
        REUSE_RAM[beat][:] = ram
        REUSE_RAM[beat].touched = []
    ee = FallEE(None, b'', b'')
    ee.mem, ee.spad = REUSE_RAM[beat], spad
    ee.call(REUSE[which][0], [prim])
    want_r, want_spad = s32(ee.r[2]), bytes(ee.spad)
    if ee.mem.touched:
        del REUSE_RAM[beat]
        return (beat, which, hex(prim), seg, 'the original wrote RAM')
    got_spad = (C.c_uint8 * SPAD_SIZE).from_buffer_copy(spad)
    prim_bytes = bytes(ram[prim:prim + prim_size(ram, prim)])
    got_r = REUSE_NATIVE.reuse_run(which, prim_bytes, got_spad)
    if got_r != want_r or bytes(got_spad) != want_spad:
        at = next((i for i in range(SPAD_SIZE) if got_spad[i] != want_spad[i]), None)
        return (beat, which, hex(prim), seg, 'return', want_r, got_r,
                'scratchpad at %s' % (hex(0x70000000 + at) if at is not None else 'same'))
    return ('ok', want_r, struct.unpack_from('<H', want_spad, 0x30CA)[0] if want_r == 1 else None)


def reuse_checks(elf):
    """Returns (problems, per-function case counts, cases run, cases in the
    full run, distinct prims)."""
    global REUSE_NATIVE
    REUSE_NATIVE = build_reuse(elf)
    mode_word = struct.unpack_from('<i', CAPTURES['a22_00_door7'][0], MODE_WORD)[0]
    assert REUSE_NATIVE.reuse_init(elf, len(elf), mode_word) == 0, 'SDK math tables'
    rng = random.Random(0xA22D)
    items, prims_seen = [], set()
    for beat in AREA22_BEATS:
        ram, spad = CAPTURES[beat]
        for prim in round_prims(ram, spad):
            prims_seen.add((prim, bytes(ram[prim:prim + 0x18])))
            for which in REUSE:
                items.append((beat, which, prim, None))   # the captured segment as it is
                if beat == 'a22_00_door7':
                    items += [(beat, which, prim, seg) for seg in reuse_segments(ram, prim, rng)]
    picked = reference_mode.select(items, reference_mode.pick(len(items), 900), 0xA22E,
                                   keep=lambda i, item: item[3] is None)
    results = reference_mode.parallel_map(reuse_case, picked)
    problems = [r for r in results if r[0] != 'ok']
    counts = {}
    for item, r in zip(picked, results):
        runs, hits, classes = counts.get(item[1], (0, 0, set()))
        if r[0] == 'ok' and r[1] == 1:
            hits += 1
            classes.add(r[2])
        counts[item[1]] = (runs + 1, hits, classes)
    return problems, counts, len(picked), len(items), len(prims_seen)


HOOK_NAMES = [name for name, _, _, _ in HOOKS]
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
    """The fail-stop contract of the header (em_area22_port.h), on one
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
    where each wrapper's own `a22_failed` test is observable. Returns
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
    return reference_mode.select(cases, reference_mode.pick(len(cases), fixed + len(pinned) + QUICK_RANDOM), 0xA22,
                                 keep=lambda i, c: i < fixed or i in pinned), captured, targeted, len(cases)


def main():
    global NATIVE
    elf = read_elf()
    CAPTURES.update(load_captures(elf))
    NATIVE = build(os.environ.get('EM_AREA22_PORT_SOURCE'))
    selected, captured, targeted, total = select_cases(elf)
    CASES[:] = selected
    only = os.environ.get('EM_AREA22_PORT_ONLY')   # debugging: a label prefix
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
        _, size, _, _ = FUNCS[entry]
        ram0 = CAPTURES[AREA22_BEATS[0]][0]
        words = reachable_words(ram0, entry, size) - set(DEAD_WORDS)
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
        print('  native fail-stop checks: %s' % ('ok (NULL hook, failing hook, unmapped address, unmapped address '
                                                 'before any call, latched fault, NULL hooks; every entry refuses a '
                                                 'latched fault, a NULL hook table, a NULL fault pointer and a NULL '
                                                 'result pointer)' if not problems else problems))
        if problems:
            failures.append(('native fail-stop', problems))
        sites, missing = hook_sites(results)
        outcome = reference_mode.parallel_map(hook_contract_site, sites)
        contract = [p for problems_, _ in outcome for p in problems_]
        if missing:
            contract.append(('no passing case reaches', missing))
        print('  hook contract: %s' % (
            'ok (%d native runs on %d cases: every call failing and every memory access refused; each of the %d hooks '
            'NULL, returning INT32_MIN, 1 and INT32_MAX; `bytes` NULL on all %d entries)'
            % (sum(n for _, n in outcome), len(sites), len(HOOKS), len(FUNCS)) if not contract else contract[:6]))
        if contract:
            failures.append(('hook contract', contract))
        problems, counts, ran, reuse_total, prims = reuse_checks(elf)
        print('  reuse checks (existing translations against the original on the AREA22 round prims): %s'
              % ('ok, %s of %d segment runs over %d distinct prims: %s'
                 % (ran if not reference_mode.FULL else 'all', reuse_total, prims,
                    ', '.join('%06X (%s) %d runs, %d hits (classes %s)'
                              % (REUSE[w][0], REUSE[w][1], n, h, '/'.join('%X' % c for c in sorted(k)))
                              for w, (n, h, k) in sorted(counts.items())))
                 if not problems else problems[:4]))
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


if __name__ == '__main__':
    main()

