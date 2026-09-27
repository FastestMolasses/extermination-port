#!/usr/bin/env python3
"""Execute the ORIGINAL AREA01 overlay owners and compare em_area01_overlay*.c.

docs/AREA01_OVERLAY.md. The 14 AREA01 overlay functions of the AREA01 census
delta (decomp build/s87/census/a01_delta.json, region overlay:AREA01):
0x823580, 0x825130, 0x825240, 0x825350, 0x8254B0, 0x825590, 0x825670,
0x8261A0, 0x826200, 0x826440, 0x8267C0, 0x826CF0, 0x826D40, 0x828850.

Oracle: the shared EE interpreter with the measured float model (FallEE,
tools/test_player_fall_reference.py) runs the original overlay code resident
in the recorded AREA01 RAM images (decomp build/s87/route_a01/<beat>/
eeMemory.bin + scratchpad.bin, overlay id 2 at 0x823500). Before any case
the test checks that the overlay text in each image equals the user's
extract/OVERLAY/AREA01.BIN and that the boot text below 0x241000 equals the
pinned ELF, so every executed instruction is original. Nothing here embeds
original bytes; reports hold counts only.

Callees (every boot function, 0x8282F0 / 0x8287C0 and the actor's +0x4C
callback) are intercepted at their entry and logged with their arguments.
A callee either runs as ORIGINAL code nested inside the oracle (the pure
helpers in RUN: vector copy/sub/normalise, matrix copy, sin/atan/sqrt/fabs/
asin, the angle wrap and turn helpers, the random generator, the script
start 001BA1A0), its writes recorded, or is stubbed with the case's
scripted result (everything else: sound, effects, script ticks, model and
pool calls, collision/quad tests, draw callbacks). The native module runs
over a byte copy of the same RAM with hooks that must be called in the same
order with the same arguments; each hook replays the original callee's
writes and result.

Compared, per case:
  * at the ENTRY of every call (hook or +0x4C callback), BEFORE the callee's
    writes are replayed: the native RAM and scratchpad against the oracle's
    memory at the entry of the same original call, rebuilt from the start
    image and the oracle's ordered store log (Oracle.stores / marks). Quick
    mode compares the dirty set (every byte the oracle stored so far plus
    every range the module was handed through `bytes`, which is sufficient
    because the module reaches memory only through those pointers);
    EM_TEST_FULL=1 compares all 32 MiB and the whole scratchpad at every
    call entry. Then the callee and its arguments.
  * between calls: in each stretch between two calls (entry to the first
    call, call to call, last call to return) the native module's memory
    accesses are the original's own loads and stores one for one, in the
    same order, with the same address and size, and each marked the same
    way as changing memory or not (Replay.check_access). So a read is made
    where the original makes it: not moved across a call or a store, not
    replaced by a value read earlier, not of another width, not added. The
    kind of an access that changes nothing (a load, or a store of the value
    already there) is not observed. The only accesses left out are the
    door's jump-table loads, which the C switch encodes (TABLE_BYTES).
  * after the last store: all 32 MiB of RAM and the whole scratchpad, the
    op09 return value, and a self-check that the store log rebuilds the
    oracle's final memory (rebuilt(): over every byte any code path wrote
    into the oracle's memory, WatchedMemory, which gives the same answer as
    comparing all 32 MiB). When the original's own load or store leaves
    main RAM (0x00000000-0x01FFFFFF) and the scratchpad, or is misaligned,
    the original stops there and the native module must stop with fault 5
    at the same address after the same calls and stores.
  * float hook arguments and results cross the harness as bit patterns
    (FloatArg), so signalling NaNs are delivered unchanged.
  * each helper that runs as original code is first rehearsed with every
    argument register its hook does not pass poisoned; its writes and
    result must not change, so the hook's arguments are all it reads.
  * every case runs twice: as given, and poisoned (poison_patches): each
    byte the function writes before reading it and the untouched bytes
    around every store start with different values, so a store that is
    moved across a call, dropped, misdirected or of the wrong width shows.
  * the run fails unless every reachable original word was executed.
  * the fail-stop contract of em_area01_overlay.h (hook_contract_site), on
    a set of passing cases that calls every hook, the +0x4C callback and
    every entry: each call failing (-1), each memory access refused, each
    hook NULL, returning INT32_MIN, 1 and INT32_MAX, and `bytes` NULL; the
    fault address and code, the calls and accesses up to the fault (none
    after it), the memory at the fault and the unwritten op09 result are
    compared with the original's run. fault_checks adds a fault latched on
    entry, a NULL hook table and a NULL fault pointer at every entry.
  * every call of `bytes`, of a hook and of the callback gets the table's
    ctx (CTX) unchanged.
Memory handling (round 6; nothing it compares changed): each worker keeps
its start, oracle, native and expected images from case to case (Images)
and, after a passing run, copies back only the bytes the run can have
changed; a failing run, a change of capture or any other load makes the
next case copy everything again. Before, each run copied 4 x 32 MiB and
compared 2 x 32 MiB, most of the default run's CPU.

Cases:
  capture   every owner node of every applicable beat, exactly as captured
            (the frame loop's D_00275B40 = actor + 0x110 publication set).
  designed  targeted_cases: every state and step, callee results outside
            {0, 1} at every test of a result, scribbling callees, the
            dispatched and compared bytes (each handled value with each bit
            flipped; all 256 values with EM_TEST_FULL=1), EE-model float
            boundaries, signalling NaNs through the hooks.
  perturbed seeded variations: every state / sub-state value the code
            dispatches on (plus unhandled values), story bytes and globals,
            timers at their boundaries, angles at and around every clamp,
            scripted callee results (0 / 1 / 2 / 4, random words and floats).
Coverage: the report counts the original instruction words the oracle
executed inside each function (branch words excluded).

Default run: every capture and designed case plus the PINNED_RANDOM
random cases (QUICK_RANDOM = 0 since round 6; the target is ~10 s of CPU,
docs/AREA01_OVERLAY.md section 3 has the measured times). EM_TEST_FULL=1
runs the full random sweep and every value of every byte the functions
dispatch on.
"""
import ctypes as C
import random
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode  # noqa: E402
from test_player_slide_reference import EE, read_elf, bits, number, s32, sx32  # noqa: E402
from test_player_fall_reference import FallEE  # noqa: E402
import ee_float_model as M  # noqa: E402

DECOMP = ROOT.parent / 'Extermination'
ROUTE = DECOMP / 'build/s87/route_a01'
OVERLAY_FILE = DECOMP / 'extract/OVERLAY/AREA01.BIN'
OUT = ROOT / 'build/area01/ovl'
MASK = 0xFFFFFFFF
RAM_SIZE, SPAD_SIZE = 0x2000000, 0x4000
ARENA, TEXT_END = 0x823500, 0x828A00
BEATS = ['a01_00_train_room', 'a01_01_tunnel', 'a01_02_shaft_landing', 'a01_03_shaft_locked',
         'a01_04_return_north', 'a01_05_npc_bridge_talk', 'a01_06_return_south',
         'a01_s0_npc_first_talk', 'a01_s1_sentry_doc', 'a01_s2_control_room_items',
         'a01_s3_fire_contact']
POOL, NODE, NODES = 0x7A5640, 0x2F0, 400

# (entry, native symbol, byte length of the function body, kind)
FUNCS = {
    0x823580: ('em_area01_ovl_00823580', 588, 'owner'),
    0x825130: ('em_area01_ovl_00825130', 260, 'op09'),
    0x825240: ('em_area01_ovl_00825240', 260, 'op09'),
    0x825350: ('em_area01_ovl_00825350', 352, 'owner'),
    0x8254B0: ('em_area01_ovl_008254B0', 212, 'owner'),
    0x825590: ('em_area01_ovl_00825590', 220, 'owner'),
    0x825670: ('em_area01_ovl_00825670', 208, 'owner'),
    0x8261A0: ('em_area01_ovl_008261A0', 96, 'owner'),
    0x826200: ('em_area01_ovl_00826200', 572, 'owner'),
    0x826440: ('em_area01_ovl_00826440', 884, 'owner'),
    0x8267C0: ('em_area01_ovl_008267C0', 396, 'owner'),
    0x826CF0: ('em_area01_ovl_00826CF0', 68, 'owner'),
    0x826D40: ('em_area01_ovl_00826D40', 5544, 'owner'),
    0x828850: ('em_area01_ovl_00828850', 408, 'owner'),
}

# Hooks, in the field order of EmArea01OvlHooks (after ctx and bytes).
# name -> (original address, [(ctype, register)], result kind or None)
U, I, F = 'u', 'i', 'f'
HOOKS = [
    ('w_00102760', 0x102760, [(U, 4), (U, 5)], None),
    ('w_001028D0', 0x1028D0, [(U, 4), (U, 5), (U, 6)], None),
    ('w_00102948', 0x102948, [(U, 4), (U, 5)], None),
    ('w_00102958', 0x102958, [(U, 4), (U, 5)], None),
    ('w_0011DBB8', 0x11DBB8, [(F, 12)], F),
    ('w_0011DF78', 0x11DF78, [(F, 12)], F),
    ('w_0011E2A8', 0x11E2A8, [(F, 12)], F),
    ('w_0011E520', 0x11E520, [(F, 12)], F),
    ('w_0011E748', 0x11E748, [(F, 12)], F),
    ('w_00122BB8', 0x122BB8, [], I),
    ('w_00182BF0', 0x182BF0, [(U, 4)], I),
    ('w_0019B6C0', 0x19B6C0, [(U, 4), (U, 5), (U, 6)], I),
    ('w_001A2370', 0x1A2370, [(U, 4), (U, 5)], None),
    ('w_001AFA90', 0x1AFA90, [(I, 4)], U),
    ('w_001AFC10', 0x1AFC10, [(U, 4)], None),
    ('w_001B0FD0', 0x1B0FD0, [(U, 4)], I),
    ('w_001B10B0', 0x1B10B0, [(U, 4), (I, 5), (I, 6)], I),
    ('w_001B1190', 0x1B1190, [(I, 4)], None),
    ('w_001B11E0', 0x1B11E0, [(I, 4)], I),
    ('w_001B1240', 0x1B1240, [(U, 4), (F, 12), (F, 13)], F),
    ('w_001B12B0', 0x1B12B0, [(F, 12), (F, 13), (F, 14)], F),
    ('w_001B1380', 0x1B1380, [(U, 4), (U, 5), (F, 12)], I),
    ('w_001B1470', 0x1B1470, [(F, 12)], F),
    ('w_001B17A0', 0x1B17A0, [(U, 4)], I),
    ('w_001B1B70', 0x1B1B70, [(U, 4)], None),
    ('w_001B1EA0', 0x1B1EA0, [(I, 4), (U, 5), (U, 6), (I, 7)], I),
    ('w_001BA1A0', 0x1BA1A0, [(U, 4), (U, 5)], None),
    ('w_001BA1C0', 0x1BA1C0, [(U, 4), (I, 5)], I),
    ('w_001BA1F0', 0x1BA1F0, [(U, 4)], I),
    ('w_001BA540', 0x1BA540, [(U, 4)], None),
    ('w_001BA580', 0x1BA580, [(U, 4), (I, 5)], None),
    ('w_001BA8E0', 0x1BA8E0, [(U, 4), (I, 5)], None),
    ('w_001BBDA0', 0x1BBDA0, [(U, 4)], None),
    ('w_001BBE40', 0x1BBE40, [(U, 4), (U, 5), (I, 6)], I),
    ('w_001BC0E0', 0x1BC0E0, [(U, 4), (U, 5)], I),
    ('w_001BC240', 0x1BC240, [(U, 4), (U, 5)], None),
    ('w_001BC290', 0x1BC290, [(U, 4), (U, 5)], I),
    ('w_001BC300', 0x1BC300, [(U, 4)], None),
    ('w_001C4760', 0x1C4760, [(I, 4), (I, 5)], I),
    ('w_001C4820', 0x1C4820, [(U, 4)], None),
    ('w_001C5C90', 0x1C5C90, [(U, 4)], None),
    ('w_001C6380', 0x1C6380, [(U, 4)], None),
    ('w_001C63E0', 0x1C63E0, [(U, 4), (I, 5)], None),
    ('w_001C64F0', 0x1C64F0, [(U, 4), (F, 12)], I),
    ('w_001C67E0', 0x1C67E0, [(U, 4), (I, 5), (F, 12), (F, 13)], None),
    ('w_001C68C0', 0x1C68C0, [(U, 4)], None),
    ('w_001EFD90', 0x1EFD90, [(I, 4), (U, 5), (U, 6)], I),
    ('w_001EFE00', 0x1EFE00, [(I, 4), (U, 5)], I),
    ('w_001FBD50', 0x1FBD50, [(U, 4), (I, 5), (I, 6), (F, 12)], I),
    ('w_001FC3C0', 0x1FC3C0, [(U, 4), (U, 5), (I, 6), (F, 12), (F, 13)], None),
    ('w_001FC520', 0x1FC520, [(U, 4)], None),
    ('w_008282F0', 0x8282F0, [(U, 4), (U, 5)], I),
    ('w_008287C0', 0x8287C0, [(U, 4)], None),
]
BY_ADDRESS = {address: (name, args, result) for name, address, args, result in HOOKS}

# Callees that run as original code inside the oracle (writes replayed to
# the native side). Pure helpers only: none of them reaches hardware.
RUN = {0x102760, 0x1028D0, 0x102948, 0x102958, 0x11DBB8, 0x11DF78, 0x11E2A8, 0x11E520,
       0x11E748, 0x122BB8, 0x1B1240, 0x1B12B0, 0x1B1470, 0x1BA1A0}

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
ARG_CT = {U: C.c_uint32, I: C.c_int32, F: FloatArg}
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
def build():
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / ('area01_overlay.dylib' if sys.platform == 'darwin' else 'area01_overlay.so')
    sources = ['src/game/em_area01_overlay.c', 'src/game/em_area01_overlay_826d40.c']
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc'] + sources + ['-lm', '-o', str(lib)],
                   cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    for entry, (symbol, _, kind) in FUNCS.items():
        fn = getattr(native, symbol)
        if kind == 'op09':
            fn.argtypes = [C.POINTER(Hooks), C.c_uint32, C.c_uint32, C.c_uint32,
                           C.POINTER(C.c_int32), C.POINTER(Fault)]
        else:
            fn.argtypes = [C.POINTER(Hooks), C.c_uint32, C.POINTER(Fault)]
        fn.restype = C.c_int
    return native


def load_captures(elf):
    overlay = OVERLAY_FILE.read_bytes()
    captures = {}
    for beat in BEATS:
        folder = ROUTE / beat
        ram = (folder / 'eeMemory.bin').read_bytes()
        spad = (folder / 'scratchpad.bin').read_bytes()
        assert len(ram) == RAM_SIZE and len(spad) == SPAD_SIZE, beat
        assert ram[ARENA:ARENA + 8] == overlay[:8] and ram[ARENA + 4] == 2, (beat, 'AREA01 overlay not resident')
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
        values.append(ee.f[reg] & MASK if kind == F else ee.r[reg] & MASK)
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
            ee.call(entry, args[0])
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
        test compares only the stop (docs/AREA01_OVERLAY.md section 4). A
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
        """The module uses the bytes of a `bytes` request (a01_at) before
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
        (a01_at in em_area01_overlay_internal.h), and replayed callee
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
            normal = tuple(float_arg_bits(v) if k == F else v & MASK for (k, _), v in zip(args, values))
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
CTX = 0xA01C7C00   # the hook table's ctx in every Replay: opaque, never dereferenced by the module


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
    cost = lambda i: 5 if selected[i][2] == 0x826D40 else 1   # noqa: E731
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
    callbacks = {u32(ram, self_node + 0x4C)}
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


def world_patch(beat, node):
    """The frame loop publishes the current actor's bone table before its
    behaviour runs: D_00275B40 = actor + 0x110 (decomp FINDINGS, bone table
    publication). Only 0x826D40 reads it here."""
    return [(0x275B40, w32(node + 0x110))]


F_VALUES = [0.0, -0.0, 1e-39, 0.25, -0.5, 1.0, -1.134464, 1.134464, 1.13446, 1.1345, -1.13447,
            3.1415927, -3.1415927, -1.5707964, -1.5707963, -1.5707965, -0.5235988, -0.52359, 0.05, -0.05,
            2.0, 2.0000002, 1.9999999, 10.0, -123.5, 0.0116, -0.0116, 0.011635528, -0.011635528]


def rf(rng):
    pick = rng.random()
    if pick < 0.4:
        return rng.choice(F_VALUES)
    if pick < 0.8:
        return rng.uniform(-3.5, 3.5)
    return rng.uniform(-200.0, 200.0)


def fb(value):
    return fbits(value)


def case_list(elf):
    rng = random.Random(0xA01)
    cases = []
    count = reference_mode.pick(160, 24)

    def add(label, beat, entry, args, patches=(), spad=(), queues=None, default=None, run_set=RUN):
        cases.append((label, beat, entry, tuple(args), list(patches), list(spad),
                      Script(queues, default), frozenset(run_set)))

    for beat in BEATS:
        ram, spad = CAPTURES[beat]
        base = {}
        for entry, callback in ((0x823580, 0x823580), (0x825350, 0x825350), (0x8261A0, 0x8261A0),
                                (0x8267C0, 0x8267C0), (0x826CF0, 0x826CF0), (0x826D40, 0x826D40),
                                (0x828850, 0x828850)):
            for node in owners(ram, callback):
                base.setdefault(entry, []).append(node)
                add('capture %s %06X @%X' % (beat, entry, node), beat, entry, [node], world_patch(beat, node))
        npc = base.get(0x825350, [None])[0]
        if npc:
            for entry in (0x8254B0, 0x825590, 0x825670):
                add('capture %s %06X @%X' % (beat, entry, npc), beat, entry, [npc])
            for entry, records in ((0x825130, (0x82A020, 0x82A420)), (0x825240, (0x82A3A0, 0x82A4E0))):
                for record in records:
                    add('capture %s %06X rec %X' % (beat, entry, record), beat, entry, [npc, npc + 0x1F0, record])
        for node in base.get(0x8261A0, []):
            sub = 0x826200 if ram[node + 0xD] == 2 else 0x826440 if ram[node + 0xD] == 3 else None
            if sub:
                add('capture %s %06X @%X' % (beat, sub, node), beat, sub, [node])
    captured = len(cases)
    targeted_cases(add)
    targeted = len(cases) - captured

    # ---------------------------------------------------------------- perturbed
    beats = BEATS
    for i in range(count):
        beat = rng.choice(beats)
        ram, _ = CAPTURES[beat]
        spare = free_node(ram)

        # shaft door 0x823580
        for node in owners(ram, 0x823580):
            p = [(node + 4, b8(rng.choice([0, 1, 1, 1, 2, 3, 4]))), (node + 5, b8(rng.choice(range(9)))),
                 (0x8107D9, b8(rng.choice([0, 0x80, 0x81, 0x81, 1, 0x82, 0xFF]))),
                 (node + 0xB, b8(rng.choice([0, 1, 4])))]
            q = {a: [rng.choice([0, 1, 2])] for a in (0x1BBE40, 0x1BC0E0, 0x1BC290, 0x1BA1F0)}
            add('door %d' % i, beat, 0x823580, [node], p, queues=q)

        npcs = owners(ram, 0x825350)
        for node in npcs:
            p = [(node + 4, b8(rng.choice([0, 1, 1, 1, 2, 3, 5]))), (node + 5, b8(rng.choice([0, 1, 2, 3]))),
                 (node + 6, b8(rng.choice([0, 1]))), (node + 0xB, b8(rng.choice([0, 4, 0xFB, 0xFF]))),
                 (0x81075A, b8(rng.choice([0, 0, 1]))),
                 (0x8107D9, b8(rng.choice([0, 0x80, 0x81, 0x7F, 0x82, 0xFF]))),
                 (node + 0xD, b8(rng.randrange(256))), (node + 0x28, h16(rng.randrange(65536)))]
            q = {0x1BA1F0: [rng.choice([0, 1, 2])], 0x1B17A0: [rng.choice([0, 1])],
                 0x1C64F0: [rng.randrange(-5, 5)], 0x1C4760: [rng.choice([0, 1])]}
            add('npc %d' % i, beat, 0x825350, [node], p, queues=q)
            sub = rng.choice([0x8254B0, 0x825590, 0x825670])
            add('npc-talk %d' % i, beat, sub, [node], p, queues=q)
            # op09 callbacks
            rec = rng.choice([0x82A020, 0x82A420])
            yaw = rf(rng)
            p2 = [(node + 0x1F4, b8(rng.choice([0, 1, 1, 2]))), (node + 0xC4, f32b(yaw)),
                  (0x810360, f32b(rf(rng))), (0x810368, f32b(rf(rng)))]
            goal = rf(rng)
            turn = rng.choice([lambda e: e.f[12] & MASK, lambda e, g=fb(rf(rng)): g, lambda e: 0x80000000 if not e.f[12] & MASK else e.f[12] & MASK])
            q2 = {0x1B1380: [rng.choice([0, 1, 7])], 0x1B1240: [fb(goal)], 0x1B12B0: [turn]}
            run2 = RUN - {0x1B1240, 0x1B12B0} if rng.random() < 0.6 else RUN
            add('op09-825130 %d' % i, beat, 0x825130, [node, node + 0x1F0, rec], p2, queues=q2, run_set=run2)
            rec = rng.choice([0x82A3A0, 0x82A4E0])
            p3 = p2 + [(rec + 0x24, f32b(rf(rng)) if rng.random() < 0.5 else w32(u32(ram, rec + 0x24))),
                       (rec + 0x1C, h16(rng.choice([4, 0xFFFE, 7])))]
            q3 = {0x1B1470: [fb(rf(rng))], 0x1B12B0: [turn]}
            run3 = RUN - {0x1B1470, 0x1B12B0} if rng.random() < 0.6 else RUN
            add('op09-825240 %d' % i, beat, 0x825240, [node, node + 0x1F0, rec], p3, queues=q3, run_set=run3)

        # 0x8261A0 / 0x826200 / 0x826440
        for node in owners(ram, 0x8261A0):
            p = [(node + 4, b8(rng.choice([0, 1, 1, 1, 2, 3, 4]))), (node + 5, b8(rng.choice([0, 1, 2, 3]))),
                 (node + 6, b8(rng.choice([0, 1, 2]))), (node + 7, b8(rng.choice([0, 1, 2]))),
                 (node + 0xD, b8(rng.choice([2, 3, 3, 2, 0]))),
                 (0x81075E, b8(rng.choice([0, 1]))), (0x810760, b8(rng.choice([0, 0, 1, 0xFF]))),
                 (0x810784, b8(rng.choice([0, 0, 1]))), (0x8107E0, b8(rng.choice([0, 1, 2, 0xE0, 0xFF, 5]))),
                 (0x810354, f32b(rng.choice([1.0, 2.0, 2.0000002, -0.0, 5.0]))),
                 (0x810360, f32b(rf(rng))), (0x810368, f32b(rf(rng))),
                 (node + 0x240, w32(rng.choice([0, 0x3B, 0x3C, 0xC7, 0xC8, -1, 5]))),
                 (node + 0x2A, h16(rng.choice([0, 0x7FFF, 0xFFFF, 3])))]
            sp = [(0x70003B8D, b8(rng.choice([0, 0, 1])))]
            q = {0x1BA1C0: [rng.choice([0, 0, 1])], 0x1B1EA0: [rng.choice([0, 1, 2]), rng.choice([0, 1, 2])],
                 0x182BF0: [rng.choice([0, 0, 1])], 0x1BA1F0: [rng.choice([0, 1])], 0x1B0FD0: [rng.choice([0, 1])]}
            entry = rng.choice([0x8261A0, 0x8261A0, 0x826200, 0x826440])
            add('bridge %06X %d' % (entry, i), beat, entry, [node], p, sp, queues=q)

        for node in owners(ram, 0x8267C0):
            p = [(node + 4, b8(rng.choice([0, 1, 1, 1, 2, 3, 4]))), (node + 5, b8(rng.choice([0, 1, 2, 3])))]
            q = {0x1B0FD0: [rng.choice([0, 1])], 0x1BA1C0: [rng.choice([0, 0, 1]), rng.choice([0, 1, 1])],
                 0x1B1EA0: [rng.choice([0, 1, 2]), rng.choice([0, 1])], 0x1BA1F0: [rng.choice([0, 1])],
                 0x1B17A0: [rng.choice([0, 1, 1])]}
            add('fixture %d' % i, beat, 0x8267C0, [node], p, queues=q)

        for node in owners(ram, 0x826CF0):
            add('826CF0 %d' % i, beat, 0x826CF0, [node], [(node + 3, b8(rng.choice([0, 1, 1, 2, 0xFF])))])

        for node in owners(ram, 0x828850):
            p = [(node + 4, b8(rng.choice([0, 1, 1, 2, 2, 3, 4]))), (node + 0x36, h16(rng.choice([0, 0, 1, 0x8000]))),
                 (node + 0x28, h16(rng.choice([0, 8, 9, 10, 11, 0xFFFF, 0x8000]))),
                 (node + 0x9A, b8(rng.randrange(256)))]
            q = {0x1B0FD0: [rng.choice([0, 1])], 0x1B11E0: [rng.choice([0, 1])], 0x1EFE00: [rng.choice([0, 1])],
                 0x1B17A0: [rng.choice([0, 1])]}
            add('828850 %d' % i, beat, 0x828850, [node], p, queues=q)

        for node in owners(ram, 0x826D40):
            state = rng.choice([0, 0x64, 4, 4, 4, 1, 1, 1, 1, 2, 2, 3, 7])
            target = rng.choice([0x8102B0] + npcs)
            partner = rng.choice(npcs + [0x8102B0])
            p = world_patch(beat, node) + [
                (node + 4, b8(state)),
                (node + 0x208, w32(rng.choice([-1, 0, 0, 0, 1, 2, 0x1E, 0x1F, 0x20, 0x1E0]))),
                (node + 0x20C, w32(rng.choice([0, 1, 2, 5]))), (node + 0x214, w32(rng.choice([0, 1, 2, 5]))),
                (node + 0x200, w32(rng.choice([0, 1, 0xC, 0xD, 0xE, 0x20]))),
                (node + 0x204, w32(rng.choice([0, target, target]))),
                (node + 0x28, h16(rng.choice([0, 1, 2, 0x22, 0x30, 0xFFFF, 0x7FFF, rng.randrange(65536)]))),
                (node + 0x2A, h16(rng.choice([0, 1, 3, 4, 5, 0xFFFF, 0xFFFB, 0x12C]))),
                (node + 0x36, h16(rng.choice([0, 0, 1]))),
                (node + 0x1F4, f32b(rng.choice([0.01620663, -0.01620663, 0.0, rf(rng)]))),
                (node + 0x1F8, f32b(rng.choice([0.044879895, -0.044879895, rf(rng)]))),
                (node + 0x210, f32b(rng.choice([0.10471976, -0.10471976, rf(rng)]))),
                (node + 0x218, f32b(rng.choice([0.10471976, -0.10471976, rf(rng)]))),
                (node + 0x1FC, f32b(rng.choice([-1.5707964, -1.5707964, 3.1415927, 3.1, rf(rng)]))),
                (node + 0x21C, w32(rng.choice([0, 1, 90, 0x5A]))), (node + 0x224, w32(rng.choice([0, 1]))),
                (u32(ram, node + 0x118) + 0x74, f32b(rf(rng))), (u32(ram, node + 0x11C) + 0x78, f32b(rf(rng))),
                (u32(ram, node + 0x118) + 0xB0, f32b(rng.choice([0.0, rng.uniform(-1, 1)]))),
                (u32(ram, node + 0x118) + 0xB8, f32b(rng.choice([1.0, -1.0, rng.uniform(-1, 1)]))),
                (partner + 2, b8(rng.choice([0, 0, 0x20, 1]))), (partner + 0, b8(rng.choice([1, 3, 0x81]))),
                (partner + 0x1A, b8(rng.choice([0x5A, 0x5B, 0x5C, 0x10])))]
            sp = [(0x70003B68, w32(rng.choice([0, 0x40, 1, 0x37C6]))),
                  (0x700031D0, w32(partner)), (0x700031D4, w32(rng.choice(npcs + [0x8102B0]))),
                  (0x700031D8, w32(rng.choice([0, 1, 1, 2, 4])))]
            q = {0x1B0FD0: [rng.choice([0, 0, 1])], 0x1BA1C0: [rng.choice([0, 1])],
                 0x1AFA90: [rng.choice([0, spare, spare])], 0x8282F0: [rng.choice([0, 1, 2])],
                 0x19B6C0: [rng.choice([0, 2, 4])], 0x1B17A0: [1]}
            run_set = RUN
            if rng.random() < 0.3:  # scripted math: drive every compare of the tracker
                run_set = RUN - {0x11DBB8, 0x11DF78, 0x11E748, 0x11E520, 0x1B1470, 0x122BB8, 0x11E2A8}
                q.update({0x11DBB8: [fb(rf(rng)), fb(rf(rng))], 0x11DF78: [fb(rng.choice([0.0, 0.001, 0.0011, 5.0]))],
                          0x11E748: [fb(rf(rng))], 0x11E520: [fb(rf(rng))], 0x1B1470: [fb(rf(rng)), fb(rf(rng))],
                          0x11E2A8: [fb(rf(rng)), fb(rf(rng))],
                          0x122BB8: [rng.randrange(1 << 32) for _ in range(3)]})
            add('826D40 s%X %d' % (state, i), beat, 0x826D40, [node], p, sp, queues=q, run_set=run_set)
    return cases, captured, targeted


def addend_for(a, target, sub=False):
    """Bits b with EE a + b (or a - b) == target exactly (EE model), found
    by a local search around the host estimate."""
    op = M.ee_sub if sub else M.ee_add
    guess = fbits((number(target) - number(a)) if not sub else (number(a) - number(target)))
    for delta in range(0, 4096):
        for b in (guess + delta, guess - delta):
            if op(a, b & MASK) == target:
                return b & MASK
    raise AssertionError('no exact addend', hex(a), hex(target))


def near(value):
    """Host single precision, round to nearest (what a plain C float
    expression computes): the model the EE truncation must be told apart
    from."""
    return fbits(value)


def disagreeing(rng, draw, ee, host, accept=lambda v: True):
    """Operand bits drawn by draw(rng) for which the EE result differs from
    the host-rounded one (and accept(EE result) holds)."""
    for _ in range(200000):
        ops = draw(rng)
        value = ee(*ops)
        if value != host(*ops) and accept(value):
            return ops
    raise AssertionError('no disagreeing operands')


def targeted_cases(add):
    """Designed cases for the branches random states reach rarely; every
    one of them runs in the default mode."""
    beat = 'a01_01_tunnel'
    ram, _ = CAPTURES[beat]
    npc = owners(ram, 0x825350)[0]
    bridge = owners(ram, 0x8261A0)[0]
    sentry = owners(ram, 0x826D40)[0]
    bone_a, bone_b = u32(ram, sentry + 0x118), u32(ram, sentry + 0x11C)
    vectors = {0x1028D0, 0x102948, 0x102760}
    maths = {0x11DBB8, 0x11DF78, 0x11E748, 0x11E520, 0x1B1470, 0x11E2A8}

    door = owners(ram, 0x823580)[0]
    fixture = owners(ram, 0x8267C0)[0]
    pair = owners(ram, 0x828850)[0]
    spare = free_node(ram)

    # 0x823580: every state, every step with both callee results and gates
    for state in (0, 2, 3, 4):
        add('door s%d' % state, beat, 0x823580, [door], [(door + 4, b8(state))])
    for step in range(8):
        for gate in ((0x81, 0, 0x82, 0xFF) if step != 2 else (0, 0x80, 0x81, 1, 0x82, 0xFF)):
            for result in (0, 1):
                add('door step %d %02X %d' % (step, gate, result), beat, 0x823580, [door],
                    [(door + 4, b8(1)), (door + 5, b8(step)), (0x8107D9, b8(gate))],
                    queues={a: [result] for a in (0x1BBE40, 0x1BC0E0, 0x1BC290, 0x1BA1F0)})
    # 0x825350 and its three talk machines
    for state, flag, gate in ((0, 0, 0), (0, 1, 0), (1, 0, 0), (1, 0, 0x80), (1, 0, 0x81), (1, 0, 0x7F),
                              (1, 0, 0x82), (1, 0, 0xFF), (1, 0, 1), (2, 0, 0), (3, 0, 0), (5, 0, 0)):
        add('npc s%d %d %02X' % (state, flag, gate), beat, 0x825350, [npc],
            [(npc + 4, b8(state)), (0x81075A, b8(flag)), (0x8107D9, b8(gate)), (npc + 5, b8(2))],
            queues={0x1BA1F0: [1]})
    for entry in (0x8254B0, 0x825590, 0x825670):
        for step in range(4):
            for use in (0, 4):
                for done in (0, 1):
                    add('talk %06X %d %d %d' % (entry, step, use, done), beat, entry, [npc],
                        [(npc + 5, b8(step)), (npc + 0xB, b8(use | 0x10))], queues={0x1BA1F0: [done]})
    # 0x826200: set-up picks, the sound script, the quad tail
    for e, f, g in ((0, 0, 0), (1, 0, 0), (0, 1, 0), (1, 1, 1), (0, 0, 1)):
        add('826200 s0 %d%d%d' % (e, f, g), beat, 0x826200, [bridge],
            [(bridge + 4, b8(0)), (0x81075E, b8(e)), (0x810760, b8(f)), (0x810784, b8(g))])
    for step, gate, e0, done, quads in ((0, 0, 1, 0, (1,)), (0, 0xFF, 1, 0, (0, 1)), (0, 0, 0, 0, (0, 0)),
                                        (1, 0, 0, 0, (2, 1)), (1, 0, 0, 1, (0, 0)), (2, 0, 0, 0, (1,)),
                                        (3, 0, 0, 0, (0, 2))):
        add('826200 s1 %d %02X %d %d' % (step, gate, e0, done), beat, 0x826200, [bridge],
            [(bridge + 4, b8(1)), (bridge + 5, b8(step)), (0x810760, b8(gate)), (0x8107E0, b8(e0))],
            queues={0x1BA1F0: [done], 0x1B1EA0: list(quads)})
    for state in (2, 3, 4):
        add('826200 s%d' % state, beat, 0x826200, [bridge], [(bridge + 4, b8(state))])
        add('826440 s%d' % state, beat, 0x826440, [bridge], [(bridge + 4, b8(state))])
    for e, f, g in ((0, 0, 0), (1, 0, 0), (0, 1, 1)):
        add('826440 s0 %d%d%d' % (e, f, g), beat, 0x826440, [bridge],
            [(bridge + 4, b8(0)), (0x81075E, b8(e)), (0x810760, b8(f)), (0x810784, b8(g))])
    for step in (2, 3):
        add('826440 s1 step %d' % step, beat, 0x826440, [bridge], [(bridge + 4, b8(1)), (bridge + 5, b8(step))])
    # 0x8261A0 dispatch
    for state, sub in ((0, 2), (0, 3), (1, 0), (1, 2), (5, 3)):
        add('8261A0 %d %d' % (state, sub), beat, 0x8261A0, [bridge],
            [(bridge + 4, b8(state)), (bridge + 0xD, b8(sub))])
    # 0x8267C0
    for init in (0, 1):
        add('8267C0 s0 %d' % init, beat, 0x8267C0, [fixture], [(fixture + 4, b8(0))], queues={0x1B0FD0: [init]})
    for f15, f7, step, q0, q2, done, shown in ((1, 1, 0, 1, 1, 0, 1), (0, 0, 0, 1, 1, 0, 0), (0, 1, 0, 0, 1, 0, 1),
                                               (0, 1, 0, 1, 0, 0, 1), (0, 1, 0, 1, 1, 0, 1), (0, 1, 1, 0, 0, 0, 1),
                                               (0, 1, 1, 0, 0, 1, 0), (0, 1, 2, 0, 0, 0, 1), (0, 1, 3, 0, 0, 0, 1)):
        add('8267C0 s1 %d%d%d%d%d%d%d' % (f15, f7, step, q0, q2, done, shown), beat, 0x8267C0, [fixture],
            [(fixture + 4, b8(1)), (fixture + 5, b8(step))],
            queues={0x1BA1C0: [f15, f7], 0x1B1EA0: [q0, q2], 0x1BA1F0: [done], 0x1B17A0: [shown]})
    for state in (2, 3, 4):
        add('8267C0 s%d' % state, beat, 0x8267C0, [fixture], [(fixture + 4, b8(state))])
    # 0x828850
    for state, raised, t28, init, cleared, effect in ((0, 0, 0, 0, 1, 0), (0, 0, 0, 0, 0, 0), (0, 0, 0, 1, 0, 0),
                                                       (1, 1, 0, 0, 0, 1), (1, 1, 0, 0, 0, 0), (1, 0, 0, 0, 0, 0),
                                                       (2, 0, 9, 0, 0, 0), (2, 0, 10, 0, 0, 0), (2, 0, 3, 0, 0, 0),
                                                       (2, 0, 0xFFFF, 0, 0, 0), (3, 0, 0, 0, 0, 0), (9, 0, 0, 0, 0, 0)):
        add('828850 %d %d %d' % (state, raised, t28), beat, 0x828850, [pair],
            [(pair + 4, b8(state)), (pair + 0x36, h16(raised)), (pair + 0x28, h16(t28))],
            queues={0x1B0FD0: [init], 0x1B11E0: [cleared], 0x1EFE00: [effect]})
    # 0x826CF0
    for flag in (0, 1, 2):
        add('826CF0 %d' % flag, beat, 0x826CF0, [owners(ram, 0x826CF0)[0]],
            [(owners(ram, 0x826CF0)[0] + 3, b8(flag))])

    # 0x826D40 set-up, wait, free
    base = world_patch(beat, sentry)
    for init, alarm, companion in ((1, 0, 0), (0, 0, 0), (0, 1, spare), (0, 0, spare)):
        add('826D40 s0 %d %d' % (init, alarm), beat, 0x826D40, [sentry], base + [(sentry + 4, b8(0))],
            queues={0x1B0FD0: [init], 0x1BA1C0: [alarm], 0x1AFA90: [companion]})
    for state, alarm in ((0x64, 0), (0x64, 1), (3, 0), (7, 0), (0xFF, 0)):
        add('826D40 s%X %d' % (state, alarm), beat, 0x826D40, [sentry], base + [(sentry + 4, b8(state))],
            queues={0x1BA1C0: [alarm]})
    # 0x826D40 countdown (states 4 and 1): fade and sweep, every count branch,
    # both clamp directions, both random bits (scripted random words)
    bits_set, bits_clear = 13108 << 16, 0
    for state in (4, 1):
        for left in (0x1E, 0x1F, 0x21):
            for c20c, c214, angle, rnd in ((2, 2, 1.13, bits_clear), (1, 1, 1.2, bits_set), (2, 1, -1.2, bits_clear),
                                           (1, 2, 0.0, bits_set), (2, 2, -1.1344, bits_set)):
                add('826D40 count s%d %X %d%d %g' % (state, left, c20c, c214, angle), beat, 0x826D40, [sentry],
                    base + [(sentry + 4, b8(state)), (sentry + 0x208, w32(left + 1)), (sentry + 0x20C, w32(c20c)),
                            (sentry + 0x214, w32(c214)), (bone_a + 0x74, f32b(angle)),
                            (sentry + 0x210, f32b(0.10471976 if angle >= 0 else -0.10471976)),
                            (sentry + 0x1FC, f32b(3.1 if c214 == 2 else 0.5))],
                    queues={0x122BB8: [rnd, rnd, rnd]}, run_set=RUN - {0x122BB8})
    # 0x826D40 state 4 idle reseeds with +0x200 both ways
    for flag in (0, 1):
        add('826D40 reseed %d' % flag, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(4)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(flag)),
                    (sentry + 0x28, h16(0)), (sentry + 0x36, h16(1))],
            queues={0x122BB8: [bits_set, bits_set, bits_set]}, run_set=RUN - {0x122BB8})
    # 0x826D40 arithmetic: non-clamping sums of arbitrary values (every add,
    # sub, mul and madd result reaches memory unclamped)
    LIM, MLIM, PI, MHALF = 0x3F91361E, 0xBF91361E, 0x40490FDB, 0xBFC90FDB
    for state in (4, 1):
        for angle, step, phase, pstep in ((0.3, 0.10471976, 0.777, 0.0123457), (-0.7, -0.0333333, -2.2, 0.3),
                                          (0.123456, 0.0987654, 1.9, -0.071), (1.0001, 0.1234, -0.31, 0.017)):
            add('826D40 sum s%d %g' % (state, angle), beat, 0x826D40, [sentry],
                base + [(sentry + 4, b8(state)), (sentry + 0x208, w32(0x40)), (sentry + 0x20C, w32(3)),
                        (sentry + 0x214, w32(3)), (bone_a + 0x74, f32b(angle)), (sentry + 0x210, f32b(step)),
                        (sentry + 0x1FC, f32b(phase)), (sentry + 0x218, f32b(pstep))])
    for angle, step, phase, pstep, h in ((0.3, 0.0162, 0.777, 0.0449, 7), (-0.9, -0.017, -2.9, 0.33, 9)):
        add('826D40 idle sum %g' % angle, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(4)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(1)), (sentry + 0x28, h16(h)),
                    (bone_a + 0x74, f32b(angle)), (sentry + 0x1F4, f32b(step)), (sentry + 0x1FC, f32b(phase)),
                    (sentry + 0x1F8, f32b(pstep))])
    for ax, az, x, z, cur in ((0.6, 0.8, 0.28, -0.96, -0.77), (-0.33, 0.9439, 0.7071, 0.7071, -0.61),
                              (0.123, -0.99, -0.5, 0.866, -0.9)):
        add('826D40 track %g' % ax, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(1)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0)),
                    (sentry + 0x204, w32(npc)), (bone_a + 0xB0, f32b(ax)), (bone_a + 0xB8, f32b(az)),
                    (bone_a + 0x74, f32b(0.25)), (bone_b + 0x78, f32b(cur))],
            [(0x70003600, f32b(x)), (0x70003608, f32b(z)), (0x70003604, f32b(0.37)),
             (0x70003610, f32b(x * 3.0)), (0x70003618, f32b(z * 3.0))],
            queues={0x11E748: [fb(1.0)], 0x11DBB8: [fb(-0.66)], 0x11DF78: [fb(5.0)], 0x8282F0: [0]},
            run_set=RUN - vectors - {0x11E748, 0x11DBB8, 0x11DF78})
        add('826D40 track run %g' % ax, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(1)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0)),
                    (sentry + 0x204, w32(npc)), (bone_a + 0xB0, f32b(ax)), (bone_a + 0xB8, f32b(az)),
                    (bone_b + 0x78, f32b(cur))])
    # arithmetic where EE truncation (with the add pre-trim) and host
    # rounding disagree, so a host-float translation cannot pass
    srng = random.Random(0xEEF)
    uf = lambda r, lo, hi: fbits(r.uniform(lo, hi))
    add_host = lambda a, b: near(number(a) + number(b))
    sub_host = lambda a, b: near(number(a) - number(b))
    within = lambda lo, hi: (lambda v: lo < number(v) < hi)
    for n in range(3):
        a, st = disagreeing(srng, lambda r: (uf(r, -1.0, 1.0), uf(r, -0.12, 0.12)), M.ee_add, add_host,
                            within(-1.13, 1.13))
        ph, ps = disagreeing(srng, lambda r: (uf(r, -3.0, 3.0), uf(r, -0.1, 0.1)), M.ee_add, add_host,
                             within(-3.1, 3.1))
        sine = disagreeing(srng, lambda r: (uf(r, -1.0, 1.0),),
                           lambda x: M.ee_add(0xBF543B67, M.ee_mul(0x3E9C61AA, x)),
                           lambda x: near(number(0xBF543B67) + number(near(number(0x3E9C61AA) * number(x)))))[0]
        for state in (4, 1):
            add('826D40 ee-sum s%d %d' % (state, n), beat, 0x826D40, [sentry],
                base + [(sentry + 4, b8(state)), (sentry + 0x208, w32(0x40)), (sentry + 0x20C, w32(3)),
                        (sentry + 0x214, w32(3)), (bone_a + 0x74, w32(a)), (sentry + 0x210, w32(st)),
                        (sentry + 0x1FC, w32(ph)), (sentry + 0x218, w32(ps))],
                queues={0x11E2A8: [sine]}, run_set=RUN - {0x11E2A8})
        add('826D40 ee-idle %d' % n, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(4)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(1)), (sentry + 0x28, h16(7)),
                    (bone_a + 0x74, w32(a)), (sentry + 0x1F4, w32(st)), (sentry + 0x1FC, w32(ph)),
                    (sentry + 0x1F8, w32(ps))],
            queues={0x11E2A8: [sine]}, run_set=RUN - {0x11E2A8})
        wa, wsa = disagreeing(srng, lambda r: (uf(r, -3.0, -1.58), uf(r, 0.0, 0.05)), M.ee_add, add_host,
                              within(-3.2, -1.5708))
        wb, wsb = disagreeing(srng, lambda r: (uf(r, -1.56, 1.0), uf(r, 0.0, 0.05)), M.ee_sub, sub_host,
                              within(-1.5707, 3.2))
        for phase, step in ((wa, wsa), (wb, wsb)):
            add('826D40 ee-wind %d %X' % (n, phase), beat, 0x826D40, [sentry],
                base + [(sentry + 4, b8(2)), (sentry + 0x1FC, w32(phase)), (sentry + 0x1F8, w32(step))],
                queues={0x1BA1C0: [1], 0x11E2A8: [sine]}, run_set=RUN - {0x11E2A8})
        # tracker side test: EE madd lands inside the band, host outside
        k = 0x3C3EA2F1

        def side_draw(r):
            z, w = uf(r, -0.2, 0.2), uf(r, -1.0, 1.0)
            x = near(number(k) - number(near(number(z) * number(w)))) + r.randrange(-3, 4)
            return x, z, w
        side_ee = lambda x, z, w: M.ee_madd(M.ee_mul(x, 0x3F800000), z, w)
        side_host = lambda x, z, w: near(number(x) + number(near(number(z) * number(w))))
        for _ in range(200000):
            x, z, w = side_draw(srng)
            inside = M.ee_c_le(side_ee(x, z, w), k)
            if inside != M.ee_c_le(side_host(x, z, w), k):
                break
        else:
            raise AssertionError('no band-splitting operands')
        add('826D40 ee-side %d' % n, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(1)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0)),
                    (sentry + 0x204, w32(npc)), (bone_a + 0xB0, f32b(-1.0)), (bone_a + 0xB8, w32(w ^ 0x80000000))],
            [(0x70003600, w32(x)), (0x70003608, w32(z)), (0x70003604, f32b(0.2)), (0x70003610, f32b(0.3))],
            queues={0x11DF78: [fb(0.0)], 0x11E748: [fb(1.0)], 0x11DBB8: [fb(0.1)], 0x8282F0: [0]},
            run_set=RUN - vectors - {0x11DF78, 0x11E748, 0x11DBB8})
        # elevation step: target equal to the EE cur - k / k + cur where the host result differs
        cur = disagreeing(srng, lambda r: (uf(r, -1.13, -0.53),), lambda c: M.ee_sub(c, k),
                          lambda c: sub_host(c, k))[0]
        cur2 = disagreeing(srng, lambda r: (uf(r, -1.13, -0.53),), lambda c: M.ee_add(k, c),
                           lambda c: add_host(k, c))[0]
        for bcur, at in ((cur, M.ee_sub(cur, k)), (cur2, M.ee_add(k, cur2)), (cur2, add_host(k, cur2))):
            add('826D40 ee-elev %d %X' % (n, at), beat, 0x826D40, [sentry],
                base + [(sentry + 4, b8(1)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0)),
                        (sentry + 0x204, w32(npc)), (bone_a + 0xB0, f32b(-1.0)), (bone_a + 0xB8, f32b(0.0)),
                        (bone_b + 0x78, w32(bcur))],
                [(0x70003600, f32b(0.0)), (0x70003608, f32b(0.0)), (0x70003604, f32b(0.2))],
                queues={0x11E748: [fb(1.0)], 0x11DBB8: [at], 0x11DF78: [fb(0.0)], 0x8282F0: [0]},
                run_set=RUN - vectors - {0x11E748, 0x11DBB8, 0x11DF78})
        # 0x825240 phase 0: EE sub of yaw - target
        yaw, target = disagreeing(srng, lambda r: (uf(r, -3.0, 3.0), uf(r, -3.0, 3.0)), M.ee_sub, sub_host)
        add('825240 ee-sub %d' % n, beat, 0x825240, [npc, npc + 0x1F0, 0x82A3A0],
            [(npc + 0x1F4, b8(0)), (npc + 0xC4, w32(yaw)), (0x82A3A0 + 0x24, w32(target))],
            queues={0x1B1470: [lambda e: e.f[12] & MASK]}, run_set=RUN - {0x1B1470})
    # callees that change memory the caller reads afterwards: the
    # translation must re-read exactly where the original re-reads
    other = owners(ram, 0x826D40)[1]
    add('door keeps +0x1C read at entry', beat, 0x823580, [door],
        [(door + 4, b8(1)), (door + 5, b8(0)), (0x8107D9, b8(0x81))],
        queues={0x1BBE40: [Scribble(1, [(door + 0x1C, w32(npc))])]})
    add('npc re-reads +0x0D', beat, 0x825350, [npc], [(npc + 4, b8(0)), (0x81075A, b8(0))],
        queues={0x1B10B0: [Scribble(0, [(npc + 0xD, b8(0x33))])], 0x1C63E0: [Scribble(0, [(npc + 0xD, b8(0x44))])]})
    add('npc re-reads story byte', beat, 0x825350, [npc], [(npc + 4, b8(1)), (0x8107D9, b8(0x80)), (npc + 5, b8(2))],
        queues={0x1BA1F0: [Scribble(1, [(npc + 0xD, b8(0x21))])]})
    add('8261A0 re-reads +0x0D', beat, 0x8261A0, [bridge], [(bridge + 4, b8(0)), (bridge + 0xD, b8(2))],
        queues={0x1B0FD0: [Scribble(0, [(bridge + 0xD, b8(3))])]})
    add('825240 re-reads the record yaw', beat, 0x825240, [npc, npc + 0x1F0, 0x82A3A0], [(npc + 0x1F4, b8(1))],
        queues={0x1B12B0: [Scribble(fbits(0.5), [(0x82A3A0 + 0x24, f32b(0.5))])]}, run_set=RUN - {0x1B12B0})
    add('828850 re-reads +0x18', beat, 0x828850, [pair], [(pair + 4, b8(0))],
        queues={0x1B0FD0: [0], 0x1B11E0: [Scribble(1, [(pair + 0x18, w32(other))])]})
    add('826D40 re-reads D_00275B40 and +0x220', beat, 0x826D40, [sentry],
        base + [(sentry + 4, b8(4)), (sentry + 0x208, w32(0x40)), (sentry + 0x20C, w32(1)), (sentry + 0x214, w32(2))],
        queues={0x1FBD50: [Scribble(0, [(0x275B40, w32(other + 0x110)), (sentry + 0x220, w32(spare))])],
                0x11E2A8: [Scribble(fbits(0.25), [(0x275B40, w32(sentry + 0x110))])],
                0x1C6380: [Scribble(0, [(0x275B40, w32(other + 0x110))])],
                0x122BB8: [0x80001234, 0xFFFF0000]},
        run_set=RUN - {0x11E2A8, 0x122BB8})
    add('826D40 tracker re-reads', beat, 0x826D40, [sentry],
        base + [(sentry + 4, b8(1)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0xD)),
                (sentry + 0x204, w32(npc)), (sentry + 0x2A, h16(0))],
        [(0x700031D0, w32(0x8102B0)), (0x700031D4, w32(npc)), (0x700031D8, w32(1)), (0x70003B68, w32(0))],
        queues={0x1FBD50: [Scribble(0, [(sentry + 0x220, w32(spare))]),
                           Scribble(0, [(0x700031D0, w32(npc))])],
                0x11DF78: [Scribble(fbits(1.0), [(0x70003610, f32b(-0.6))])],
                0x001028D0: [Scribble(0, [(0x70003600, f32b(0.0)), (0x70003608, f32b(0.0))])],
                0x8282F0: [Scribble(2, [(0x275B40, w32(other + 0x110))])],
                0x1EFD90: [Scribble(0, [(0x700031D4, w32(0x8102B0))])],
                0x11E520: [fbits(0.4)], 0x122BB8: [0xFFFFFFFF]},
        run_set=RUN - {0x1028D0, 0x11DF78, 0x11E520, 0x122BB8})
    # negative random words: the scale is an arithmetic shift
    for word in (0x80000000, 0xFFFF0001, 0xC0010000, 0x7FFFFFFF):
        add('826D40 random %08X' % word, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(0))], queues={0x1B0FD0: [0], 0x122BB8: [word], 0x1AFA90: [0]},
            run_set=RUN - {0x122BB8})
        add('826D40 random idle %08X' % word, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(4)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(word & 1)),
                    (sentry + 0x28, h16(0))],
            queues={0x122BB8: [word]}, run_set=RUN - {0x122BB8})
    # exact boundaries of every compare the EE model decides
    exact = [
        # sweep: A+0x74 + step == +LIM (kept) / == -LIM (kept)
        ('sweep=+LIM', 4, [(bone_a + 0x74, f32b(1.0)), (sentry + 0x210, w32(addend_for(fbits(1.0), LIM)))]),
        ('sweep=-LIM', 4, [(bone_a + 0x74, f32b(-1.0)), (sentry + 0x210, w32(addend_for(fbits(-1.0), MLIM)))]),
        ('phase=pi', 4, [(sentry + 0x1FC, f32b(3.0)), (sentry + 0x218, w32(addend_for(fbits(3.0), PI)))]),
    ]
    for name, state, extra_patch in exact:
        add('826D40 exact %s' % name, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(state)), (sentry + 0x208, w32(0x40)), (sentry + 0x20C, w32(3)),
                    (sentry + 0x214, w32(3))] + extra_patch)
    add('826D40 exact idle=+LIM', beat, 0x826D40, [sentry],
        base + [(sentry + 4, b8(4)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(1)), (sentry + 0x28, h16(7)),
                (bone_a + 0x74, f32b(1.0)), (sentry + 0x1F4, w32(addend_for(fbits(1.0), LIM))),
                (sentry + 0x1FC, f32b(3.0)), (sentry + 0x1F8, w32(addend_for(fbits(3.0), PI)))])
    add('826D40 exact idle=-LIM', beat, 0x826D40, [sentry],
        base + [(sentry + 4, b8(4)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(1)), (sentry + 0x28, h16(7)),
                (bone_a + 0x74, f32b(-1.0)), (sentry + 0x1F4, w32(addend_for(fbits(-1.0), MLIM)))])
    for name, phase, step in (('wind add', -2.0, addend_for(fbits(-2.0), MHALF)),
                              ('wind sub', -1.0, addend_for(fbits(-1.0), MHALF, sub=True))):
        add('826D40 exact %s' % name, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(2)), (sentry + 0x1FC, f32b(phase)), (sentry + 0x1F8, w32(step))],
            queues={0x1BA1C0: [1]})
    step_k = 0x3C3EA2F1
    for side in (step_k, step_k ^ 0x80000000):
        for cur_at in ((0.5, None), (-0.5, None)):
            cur = cur_at[0]
            below = M.ee_sub(fbits(cur), step_k)
            above = M.ee_add(step_k, fbits(cur))
            for at in (below, above):
                add('826D40 exact band %X %g %X' % (side, cur, at), beat, 0x826D40, [sentry],
                    base + [(sentry + 4, b8(1)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0)),
                            (sentry + 0x204, w32(npc)), (bone_a + 0xB0, f32b(-1.0)), (bone_a + 0xB8, f32b(0.0)),
                            (bone_b + 0x78, f32b(cur))],
                    [(0x70003600, w32(side)), (0x70003608, f32b(0.0)), (0x70003604, f32b(0.2)),
                     (0x70003610, f32b(0.3))],
                    queues={0x11E748: [fb(1.0)], 0x11DBB8: [at], 0x8282F0: [0]},
                    run_set=RUN - vectors - {0x11E748, 0x11DBB8})
    for mag in (0x3A83126F, 0x3A831270, 0x3A83126E):
        add('826D40 exact fabs %X' % mag, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(1)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0)),
                    (sentry + 0x204, w32(npc)), (bone_a + 0xB0, f32b(-1.0)), (bone_a + 0xB8, f32b(0.0))],
            [(0x70003600, f32b(0.0)), (0x70003608, f32b(0.0)), (0x70003610, f32b(-0.4)), (0x70003618, f32b(0.9))],
            queues={0x11DF78: [mag], 0x11DBB8: [fb(0.3), fb(0.1)], 0x11E748: [fb(1.0)], 0x1B1470: [fb(0.2)]},
            run_set=RUN - vectors - {0x11DF78, 0x11DBB8, 0x11E748, 0x1B1470})
    for value, at in ((MLIM, MLIM), (0xBF060A92, 0xBF060A92), (0xBF060A91, 0xBF060A91), (0xBF91361D, 0xBF91361D),
                      (fbits(-1.3), fbits(-2.0)), (fbits(-0.2), fbits(0.5))):
        add('826D40 exact pitch %X %X' % (value, at), beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(1)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0)),
                    (sentry + 0x204, w32(npc)), (bone_a + 0xB0, f32b(-1.0)), (bone_a + 0xB8, f32b(0.0)),
                    (bone_b + 0x78, w32(value))],
            [(0x70003600, f32b(0.0)), (0x70003608, f32b(0.0)), (0x70003604, f32b(0.2))],
            queues={0x11E748: [fb(1.0)], 0x11DBB8: [at], 0x11DF78: [fb(0.0)], 0x8282F0: [0]},
            run_set=RUN - vectors - {0x11E748, 0x11DBB8, 0x11DF78})
    # 0x826D40 tracker tail: the 0x8282F0 result, the +0x200 period, the
    # +0x28 hand-off, the re-arm and the return to state 4
    for count, t28, raised, t2a, result in ((0, 5, 0, 5, 0), (0, 0, 1, 5, 2), (1, 0, 0, 0, 0), (0xD, 0, 0, 5, 0),
                                           (0, 0, 0, 0, 1), (5, 3, 1, 0, 2)):
        add('826D40 tail %X %d %d %d %d' % (count, t28, raised, t2a, result), beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(1)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(count)),
                    (sentry + 0x204, w32(npc)), (sentry + 0x28, h16(t28)), (sentry + 0x36, h16(raised)),
                    (sentry + 0x2A, h16(t2a))],
            queues={0x8282F0: [result], 0x122BB8: [bits_set, bits_set, bits_set]}, run_set=RUN - {0x122BB8})

    # the return to state 4 (+0x2A below 0): +0x2A lands exactly on 0 (no
    # return) or on -1 (return), with B+0x78 inside its clamps
    for n, cur in enumerate((fbits(-0.8), fbits(-0.55), fbits(-1.1))):
        for t2a in (1, 0):
            add('826D40 ee-tail %d %d' % (n, t2a), beat, 0x826D40, [sentry],
                base + [(sentry + 4, b8(1)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0)),
                        (sentry + 0x204, w32(npc)), (sentry + 0x2A, h16(t2a)), (sentry + 0x28, h16(0)),
                        (bone_a + 0xB0, f32b(-1.0)), (bone_a + 0xB8, f32b(0.0)), (bone_a + 0x74, f32b(0.25)),
                        (bone_b + 0x78, w32(cur))],
                [(0x70003600, f32b(0.0)), (0x70003608, f32b(0.0)), (0x70003604, f32b(0.2))],
                queues={0x11E748: [fb(1.0)], 0x11DBB8: [cur], 0x11DF78: [fb(0.0)], 0x8282F0: [0]},
                run_set=RUN - vectors - {0x11E748, 0x11DBB8, 0x11DF78})
    # A record aliasing its own pointer word: A = block - 0x6C, so the sweep's
    # store to A+0x74 replaces the pointer at block+8 and the original's
    # re-read of A then follows the new pointer (0x1000000, whose +0x74
    # holds 2.0 and is clamped)
    add('826D40 sweep re-reads A', beat, 0x826D40, [sentry],
        base + [(sentry + 4, b8(4)), (sentry + 0x208, w32(0x40)), (sentry + 0x20C, w32(3)),
                (sentry + 0x214, w32(3)), (sentry + 0x118, w32(sentry + 0x110 - 0x6C)),
                (sentry + 0x210, w32(0x01000000)), (0x1000074, f32b(2.0))])
    # header byte +0x00 not already 1: a store of it on a path that does not
    # make one is visible
    for entry, node, states in ((0x8261A0, bridge, (1, 2)), (0x823580, door, (1, 4)), (0x825350, npc, (1, 2)),
                                (0x8267C0, fixture, (1, 4)), (0x828850, pair, (1, 2)), (0x826D40, sentry, (0x64, 4, 1, 2)),
                                (0x826CF0, owners(ram, 0x826CF0)[0], (0, 1))):
        for state in states:
            add('%06X header 3 s%X' % (entry, state), beat, entry, [node],
                (base if entry == 0x826D40 else []) + [(node + 0, b8(3)), (node + 4, b8(state))])
    # op09 arrival with a negative clip halfword (+0x1C is signed)
    for clip in (0xFFFE, 0x8000):
        add('825240 arrive clip %X' % clip, beat, 0x825240, [npc, npc + 0x1F0, 0x82A3A0],
            [(npc + 0x1F4, b8(1)), (0x82A3A0 + 0x1C, h16(clip))],
            queues={0x1B12B0: [lambda e: e.f[12] & MASK]}, run_set=RUN - {0x1B12B0})
    # op09 callbacks: every phase, both clip picks, reached / not reached
    for phase, pick, turn in ((0, 0, 0), (0, 1, 0), (1, 0, 'goal'), (1, 0, 'other'), (1, 0, '-0'), (2, 0, 0)):
        stub = {'goal': lambda e: e.f[12] & MASK, 'other': lambda e: fbits(0.75),
                '-0': lambda e: 0x80000000 if not e.f[12] & 0x7FFFFFFF else e.f[12] & MASK}.get(turn, 0)
        add('825130 p%d %d %s' % (phase, pick, turn), beat, 0x825130, [npc, npc + 0x1F0, 0x82A020],
            [(npc + 0x1F4, b8(phase))], queues={0x1B1380: [pick], 0x1B1240: [0], 0x1B12B0: [stub]},
            run_set=RUN - {0x1B1240, 0x1B12B0})
    for phase, wrap, turn in ((0, 0.5, 0), (0, 0.0, 0), (0, -0.5, 0), (0, -0.0, 0), (1, 0, 'goal'),
                              (1, 0, 'other'), (2, 0, 0)):
        stub = {'goal': lambda e: e.f[12] & MASK, 'other': lambda e: fbits(0.75)}.get(turn, 0)
        add('825240 p%d %g %s' % (phase, wrap, turn), beat, 0x825240, [npc, npc + 0x1F0, 0x82A3A0],
            [(npc + 0x1F4, b8(phase))], queues={0x1B1470: [fbits(wrap)], 0x1B12B0: [stub]},
            run_set=RUN - {0x1B1470, 0x1B12B0})
    # 0x826440 case 1: every gate of the script start, the E0 ramp, the copy
    gates = [(0, 0, 1, 2.0, 0), (1, 0, 1, 2.0, 0), (0, 1, 1, 2.0, 0), (0, 0, 0, 2.0, 0), (0, 0, 2, 2.0, 0),
             (0, 0, 1, 2.0000002, 0), (0, 0, 1, 2.0, 1), (0, 0, 1, -1.0, 0)]
    for n, (flag, pad, quad, height, busy) in enumerate(gates):
        add('826440 gate %d' % n, beat, 0x826440, [bridge],
            [(bridge + 4, b8(1)), (bridge + 5, b8(0)), (bridge + 0xD, b8(3)), (0x810354, f32b(height))],
            [(0x70003B8D, b8(pad))],
            queues={0x1BA1C0: [flag], 0x1B1EA0: [quad, 0, 0], 0x182BF0: [busy]})
    ramp = [(0xE0, 0, 0, 0x10, 0), (0xE0, 1, 0, 0x3B, 0), (0xE0, 1, 0, 0xC7, 1), (0xE0, 1, 1, 0x10, 0),
            (0xE0, 2, 0, 0x3B, 0), (2, 0, 0, 0x3B, 0), (2, 1, 1, 0x3B, 1), (2, 2, 2, 0, 0), (1, 1, 0, 0x3B, 0),
            (0xE0, 1, 0, 0x3C, 0), (0xE0, 1, 0, 0xC8, 0), (0xE0, 1, 0, 0xC9, 0)]
    for n, (e0, s6, s7, timer, done) in enumerate(ramp):
        add('826440 ramp %d' % n, beat, 0x826440, [bridge],
            [(bridge + 4, b8(1)), (bridge + 5, b8(1)), (bridge + 6, b8(s6)), (bridge + 7, b8(s7)),
             (bridge + 0xD, b8(3)), (0x8107E0, b8(e0)), (bridge + 0x240, w32(timer)),
             (0x810360, f32b(12.5)), (0x810368, f32b(-3.25))],
            queues={0x1BA1F0: [done], 0x1B1EA0: [n % 3, 1]})

    # 0x826D40 state 4 idle: the 0x423 sound tick, both sweep directions
    for n, (h28, step, angle, flag, pad, raised) in enumerate(
            [(3, 0.0162, 1.2, 0, 0, 0), (0x13, -0.0162, -1.2, npc, 0, 1), (0x33, 0.0, 0.5, npc, 1, 0),
             (1, 0.0162, 1.0, 0, 0, 1), (0, -0.0162, -1.0, 0, 0, 0), (7, 0.0162, 0.2, 0, 0, 0)]):
        add('826D40 idle %d' % n, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(4)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(1)),
                    (sentry + 0x28, h16(h28)), (sentry + 0x1F4, f32b(step)), (bone_a + 0x74, f32b(angle)),
                    (sentry + 0x204, w32(flag)), (sentry + 0x36, h16(raised)),
                    (sentry + 0x1FC, f32b(3.13 if n == 5 else 0.5)), (sentry + 0x1F8, f32b(0.044879895))],
            [(0x70003B68, w32(pad))])
    # 0x826D40 state 1 tracker, scripted vectors: the dead band, fabs gate,
    # both heading branches, and the elevation step's three outcomes
    band = [(0.0, 0.0, 0.0, 0.5), (0.0, 0.001, -1.0, 0.5), (0.0, 0.0011, -1.0, 0.5), (0.0, 5.0, 0.5, 0.5),
            (0.0, 5.0, 0.0, 0.5), (0.0116, 5.0, -2.0, 0.25), (0.0117, 5.0, 1.0, 0.25), (-0.0117, 5.0, 1.0, 0.25)]
    for n, (x, mag, dx, dz) in enumerate(band):
        for cur, at, yaw in ((1e6, 1e6, 0.2), (0.3, -0.2, 1.2), (-0.3, 0.2, -1.2), (1000.0, 999.0, 0.2),
                             (-0.9, -0.95, 1.134464)):
            add('826D40 band %d %g' % (n, cur), beat, 0x826D40, [sentry],
                base + [(sentry + 4, b8(1)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0)),
                        (sentry + 0x204, w32(npc)), (bone_a + 0xB0, f32b(-1.0)), (bone_a + 0xB8, f32b(0.0)),
                        (bone_a + 0x74, f32b(yaw)), (bone_b + 0x78, f32b(cur)), (sentry + 0x2A, h16(2))],
                [(0x70003B68, w32(0x40 * (n & 1))), (0x70003600, f32b(x)), (0x70003608, f32b(0.0)), (0x70003604, f32b(0.1)),
                 (0x70003610, f32b(dx)), (0x70003618, f32b(dz))],
                queues={0x11DF78: [fb(mag)], 0x11DBB8: [fb(0.4), fb(at)] if mag > 0.001 and abs(x) < 0.011635528
                        else [fb(at)], 0x11E748: [fb(1.0)], 0x1B1470: [fb(0.7), fb(-0.1)],
                        0x8282F0: [n % 3], 0x11E520: [fb(-0.3)]},
                run_set=RUN - vectors - maths)
    # 0x826D40 state 1 fire: every effect branch
    fire = [(1, 0x20, 0, 0, 0x5A), (1, 0x21, 0, 0, 0x5A), (1, 0, 0, 0, 0x5A), (1, 0, 2, 0, 0x5A)] + \
           [(0, 0, 0, hit, kind) for hit in (0, 2) for kind in (0x5A, 0x5B, 0x5C, 0x10)] + [(2, 0, 0, 4, 0x10)]
    for n, (kind, t2, t0, found, surface) in enumerate(fire):
        add('826D40 fire %d' % n, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(1)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0xD)),
                    (sentry + 0x204, w32(npc)), (npc + 2, b8(t2)), (npc + 0, b8(t0 | 1)),
                    (0x8102B0 + 0x1A, b8(surface)), (sentry + 0x36, h16(n & 1)), (sentry + 0x208, w32(0))],
            [(0x700031D0, w32(0x8102B0)), (0x700031D4, w32(npc)), (0x700031D8, w32(kind))],
            queues={0x8282F0: [1 + (n & 1)], 0x19B6C0: [found]})
    # 0x826D40 period effect: every re-read the original makes after a
    # callee (a scribbling stub changes the pointer or byte in between)
    def fire_case(name, kind, t2, t0, found, extra, run_set=RUN, patches=(), spad=()):
        queues = {0x8282F0: [1], 0x19B6C0: [found]}
        queues.update(extra)
        add('826D40 re-read %s' % name, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(1)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0xD)),
                    (sentry + 0x204, w32(npc)), (npc + 2, b8(t2)), (npc + 0, b8(t0 | 1)),
                    (0x8102B0 + 0x1A, b8(0x5A)), (npc + 0x1A, b8(0x5B)), (sentry + 0x36, h16(0))] + list(patches),
            [(0x700031D0, w32(0x8102B0)), (0x700031D4, w32(npc)), (0x700031D8, w32(kind)),
             (0x70003B68, w32(1))] + list(spad),
            queues=queues, run_set=run_set)
    moved = [(0x700031D4, w32(other))]
    fire_case('06 +0x224 after 001EFD90', 1, 0, 0, 0, {0x1EFD90: [Scribble(0, moved)]})
    fire_case('06 +0x00 after 001EFD90', 1, 0, 0, 0, {0x1EFD90: [Scribble(0, [(npc + 0, b8(0x41))])]})
    # (the tracker calls 001028D0 and 00102760 once before the effect: the
    # first stubbed call writes nothing, the second one scribbles)
    fire_case('06 +0x70 after 001028D0', 1, 0, 0, 0, {0x1028D0: [0, Scribble(0, moved)]}, RUN - {0x1028D0})
    fire_case('06 +0x70 after 00102760', 1, 0, 0, 0, {0x102760: [0, Scribble(0, moved)]}, RUN - {0x102760})
    # the 0x80000006 direction: z and w of the two effect points differ, so
    # the zero stored at 0x7000391C between the two vector calls is visible
    fire_case('06 direction w', 1, 0, 0, 0, {}, spad=[(0x700031A8, f32b(-1100.5)), (0x700031AC, f32b(0.5))])
    fire_case('07 +0x36 after 001EFD90', 1, 0x20, 0, 0, {0x1EFD90: [Scribble(0, moved)]})
    fire_case('hit after 0019B6C0', 0, 0, 0, 2, {0x19B6C0: [Scribble(2, [(0x700031D0, w32(npc))])]})
    fire_case('record words after 008287C0', 0, 0, 0, 0,
              {0x8287C0: [Scribble(0, [(0x700031D0, w32(npc)), (0x700031D8, w32(1)), (0x700031D4, w32(other))])]})
    fire_case('record words after 001FBD50', 1, 0, 0, 2,
              {0x1FBD50: [Scribble(0, [(0x700031D0, w32(npc)), (0x700031D8, w32(0))])]})
    # the tracker's side test when an operand is an EE NaN/Inf pattern: MADD
    # adds the unsaturated product (NaN -> +MAX), an add of a MUL adds the
    # saturated one, so acc = -1e38 separates the two
    for f1, f2v in ((0x7FC00000, fbits(1.0)), (0x7F800000, fbits(1.0)), (0, 0x7F800000)):
        add('826D40 side madd %X %X' % (f1, f2v), beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(1)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0)),
                    (sentry + 0x204, w32(npc)), (bone_a + 0xB0, f32b(1.0)), (bone_a + 0xB8, w32(f2v ^ 0x80000000))],
            [(0x70003600, f32b(1e38)), (0x70003608, w32(f1)), (0x70003604, f32b(0.2)), (0x70003610, f32b(0.3))],
            queues={0x11DF78: [fb(0.0)], 0x11E748: [fb(1.0)], 0x11DBB8: [fb(0.1)], 0x8282F0: [0]},
            run_set=RUN - vectors - {0x11DF78, 0x11E748, 0x11DBB8})
    # op09 arrival test: EE C.EQ treats denormals as zero and saturates
    # exponent-255 patterns, so these pairs are equal on the EE and unequal
    # for a host float ==
    for goal, turned in ((0x00000001, 0x00000000), (0x007FFFFF, 0x80000000), (0x00000001, 0x00000002),
                         (0x7FC00000, 0x7FC00000), (0x7F800000, 0x7F7FFFFF), (0xFF800000, 0xFF7FFFFF)):
        add('825130 ee-eq %X %X' % (goal, turned), beat, 0x825130, [npc, npc + 0x1F0, 0x82A020],
            [(npc + 0x1F4, b8(1))], queues={0x1B1240: [goal], 0x1B12B0: [turned]},
            run_set=RUN - {0x1B1240, 0x1B12B0})
        add('825240 ee-eq %X %X' % (goal, turned), beat, 0x825240, [npc, npc + 0x1F0, 0x82A3A0],
            [(npc + 0x1F4, b8(1)), (0x82A3A0 + 0x24, w32(goal))], queues={0x1B12B0: [turned]},
            run_set=RUN - {0x1B12B0})
    # 0x826D40 state 2: both approaches to -pi/2 with and without the clamp,
    # then the +0x21C fade in both lanes
    for n, (phase, left, lane) in enumerate([(-1.6, 1, 0), (-1.55, 1, 0), (-1.7, 1, 0), (-1.4, 1, 0),
                                             (-1.5707964, 1, 0), (-1.5707964, 90, 1), (-1.5707964, 0, 1),
                                             (-1.5707964, 45, 0)]):
        add('826D40 wind %d' % n, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(2)), (sentry + 0x1FC, f32b(phase)), (sentry + 0x1F8, f32b(0.044879895)),
                    (sentry + 0x21C, w32(left)), (sentry + 0x224, w32(lane))],
            queues={0x1BA1C0: [1]})
    # --- fix round 2: inputs the earlier cases left thin ---------------------
    # the tracker tail re-reads B+0x78 through D_00275B40 after 0x8282F0; the
    # stub changes it to values outside the clamp range on which EE sub and a
    # host subtraction disagree (+0x2A = 0 and result 0, so the tail runs)
    trng = random.Random(0x7A11)
    tail_b = []
    while len(tail_b) < 3:
        v = fbits(trng.uniform(-40.0, 40.0))
        if M.ee_sub(v, MLIM) != near(number(v) - number(MLIM)):
            tail_b.append(v)
    for v in tail_b:
        add('826D40 tail B from 8282F0 %X' % v, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(1)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0)),
                    (sentry + 0x204, w32(npc)), (sentry + 0x2A, h16(0)), (sentry + 0x28, h16(0))],
            queues={0x8282F0: [Scribble(0, [(bone_b + 0x78, w32(v))])]})
    # the 0x80000006 branch with bit 0 of the record's +0x00 clear (the OR
    # constant is visible), and the period effect with kind words other than
    # 1 whose bit 0 is set (kind == 1 is an equality, not a bit test)
    for t0 in (0x00, 0x04, 0x0C):
        fire_case('06 +0x00 = %02X' % t0, 1, 0, 0, 0, {}, patches=[(npc + 0, b8(t0))])
    for kind, found in ((3, 0), (3, 2), (5, 2), (0xFFFFFFFF, 0), (0xFFFFFFFF, 2), (0x80000001, 2), (0x101, 0)):
        fire_case('kind %X hit %d' % (kind, found), kind, 0, 0, found, {})
    # +0x200 words the function itself never produces: negative (the tests are
    # an unsigned != 0 and a signed >= 0xE), and 0x7FFFFFFF (+1 wraps negative)
    for count, t28 in ((0xFFFFFFFF, 3), (0x80000000, 3), (0x7FFFFFFF, 0), (0xFFFFFFF2, 3)):
        add('826D40 tail count %X' % count, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(1)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(count)),
                    (sentry + 0x204, w32(npc)), (sentry + 0x28, h16(t28)), (sentry + 0x2A, h16(5))],
            queues={0x8282F0: [1], 0x122BB8: [bits_set, bits_set, bits_set]}, run_set=RUN - {0x122BB8})
        for h28 in (0, 5):
            add('826D40 idle count %X %d' % (count, h28), beat, 0x826D40, [sentry],
                base + [(sentry + 4, b8(4)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(count)),
                        (sentry + 0x28, h16(h28)), (sentry + 0x1F4, f32b(0.0162))],
                queues={0x122BB8: [bits_set, bits_set, bits_set]}, run_set=RUN - {0x122BB8})
    # +0x36 raised while +0x208 is negative: the re-arm needs +0x208 == 0
    for word in (0xFFFFFFFF, 0x80000000):
        for state in (4, 1):
            add('826D40 rearm %X s%d' % (word, state), beat, 0x826D40, [sentry],
                base + [(sentry + 4, b8(state)), (sentry + 0x208, w32(word)), (sentry + 0x200, w32(0)),
                        (sentry + 0x204, w32(npc if state == 1 else 0)), (sentry + 0x36, h16(1)),
                        (sentry + 0x28, h16(5)), (sentry + 0x2A, h16(5))],
                queues={0x8282F0: [0], 0x122BB8: [bits_set, bits_set, bits_set]}, run_set=RUN - {0x122BB8})
    # the talk machines' use test is bit 2 of +0x0B alone
    for entry in (0x8254B0, 0x825590, 0x825670):
        for use in (0x02, 0xFB, 0x04, 0x01, 0xFF):
            add('talk %06X use %02X' % (entry, use), beat, entry, [npc],
                [(npc + 5, b8(1)), (npc + 0xB, b8(use))], queues={0x1BA1F0: [0]})
    # callee results outside {0, 1}: every test of a result is != 0 or == 0
    # in the original (001BA1C0 itself only returns 0 or 1, decomp C)
    for r in (2, 0xFFFFFFFF, 0x100):
        add('826D40 s0 result %X' % r, beat, 0x826D40, [sentry], base + [(sentry + 4, b8(0))],
            queues={0x1B0FD0: [0], 0x1BA1C0: [r], 0x1AFA90: [spare]})
        add('826D40 s64 result %X' % r, beat, 0x826D40, [sentry], base + [(sentry + 4, b8(0x64))],
            queues={0x1BA1C0: [r]})
        add('826D40 s2 result %X' % r, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(2)), (sentry + 0x1FC, f32b(-1.6)), (sentry + 0x1F8, f32b(0.044879895)),
                    (sentry + 0x21C, w32(3))], queues={0x1BA1C0: [r]})
        add('8267C0 s1 result %X' % r, beat, 0x8267C0, [fixture], [(fixture + 4, b8(1)), (fixture + 5, b8(0))],
            queues={0x1BA1C0: [0, r], 0x1B1EA0: [r, r], 0x1BA1F0: [r], 0x1B17A0: [r]})
        add('8267C0 s1 gate %X' % r, beat, 0x8267C0, [fixture], [(fixture + 4, b8(1)), (fixture + 5, b8(0))],
            queues={0x1BA1C0: [r, 1]})
        add('826440 gate result %X' % r, beat, 0x826440, [bridge],
            [(bridge + 4, b8(1)), (bridge + 5, b8(0)), (bridge + 0xD, b8(3)), (0x810354, f32b(2.0))],
            [(0x70003B8D, b8(0))], queues={0x1BA1C0: [r], 0x1B1EA0: [1, 0, 0], 0x182BF0: [0]})
        add('826440 busy result %X' % r, beat, 0x826440, [bridge],
            [(bridge + 4, b8(1)), (bridge + 5, b8(0)), (bridge + 0xD, b8(3)), (0x810354, f32b(2.0))],
            [(0x70003B8D, b8(0))], queues={0x1BA1C0: [0], 0x1B1EA0: [1, 0, 0], 0x182BF0: [r]})
        add('826440 quad result %X' % r, beat, 0x826440, [bridge],
            [(bridge + 4, b8(1)), (bridge + 5, b8(1)), (bridge + 0xD, b8(3)), (0x8107E0, b8(0))],
            queues={0x1BA1F0: [0], 0x1B1EA0: [r, r]})
        add('828850 result %X' % r, beat, 0x828850, [pair], [(pair + 4, b8(0))],
            queues={0x1B0FD0: [0], 0x1B11E0: [r]})
        add('828850 s1 result %X' % r, beat, 0x828850, [pair], [(pair + 4, b8(1)), (pair + 0x36, h16(1))],
            queues={0x1EFE00: [r], 0x1B17A0: [r]})
        add('door result %X' % r, beat, 0x823580, [door], [(door + 4, b8(1)), (door + 5, b8(0)), (0x8107D9, b8(0))],
            queues={0x1BBE40: [r]})
        add('npc result %X' % r, beat, 0x825350, [npc], [(npc + 4, b8(1)), (0x8107D9, b8(0x80)), (npc + 5, b8(2))],
            queues={0x1BA1F0: [r], 0x1B17A0: [r]})

    # every door step with a callee result of 2 (the steps test != 0)
    for step in range(7):
        for gate in (0, 0x81):
            add('door step %d %02X result 2' % (step, gate), beat, 0x823580, [door],
                [(door + 4, b8(1)), (door + 5, b8(step)), (0x8107D9, b8(gate))],
                queues={a: [2] for a in (0x1BBE40, 0x1BC0E0, 0x1BC290, 0x1BA1F0)})
    # 0x825130's clip pick tests 001B1380's result != 0
    for pick in (2, 7, 0xFFFFFFFF):
        add('825130 p0 pick %X' % pick, beat, 0x825130, [npc, npc + 0x1F0, 0x82A020],
            [(npc + 0x1F4, b8(0))], queues={0x1B1380: [pick]})
    # 0x8261A0 sub-dispatch on +0x0D values other than 2 and 3
    for state in (0, 1):
        for sub in (1, 4, 0xFF):
            add('8261A0 %d sub %X' % (state, sub), beat, 0x8261A0, [bridge],
                [(bridge + 4, b8(state)), (bridge + 0xD, b8(sub))])
    # the frame-counter word 0x70003B68: the sound tests use its low 6 bits
    for pad in (0x20, 0x1F, 0x3F, 0x80, 0x10):
        add('826D40 idle pad %X' % pad, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(4)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0)),
                    (sentry + 0x28, h16(5)), (sentry + 0x204, w32(npc))], [(0x70003B68, w32(pad))])
        add('826D40 track pad %X' % pad, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(1)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0)),
                    (sentry + 0x204, w32(npc)), (sentry + 0x2A, h16(5))], [(0x70003B68, w32(pad))],
            queues={0x8282F0: [0]})
    # the 0x80000007 test is the low five bits of the record's +0x02
    for t2 in (0x10, 0x01, 0xE0, 0x0F):
        fire_case('07 +0x02 = %02X' % t2, 1, t2, 0, 0, {})
    # a companion record placed so that its +0xA0 is the scratchpad word
    # 0x70003A20: the original re-reads 0x70003A20 after storing +0xA0
    alias = 0x70003A20 - 0xA0
    for left in (0x10, 0x1E):
        add('826D40 fade re-reads 3A20 %X' % left, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(4)), (sentry + 0x208, w32(left + 1)), (sentry + 0x220, w32(alias))],
            [(alias + 0x11C, w32(u32(ram, other + 0x11C)))])
    add('826D40 wind re-reads 3A20', beat, 0x826D40, [sentry],
        base + [(sentry + 4, b8(2)), (sentry + 0x1FC, f32b(-1.5707964)), (sentry + 0x21C, w32(45)),
                (sentry + 0x224, w32(0)), (sentry + 0x220, w32(alias))], queues={0x1BA1C0: [1]})
    # --- EE-vs-host: operands on which a host operation or a host compare
    # would decide differently, for the sites the round-2 sweep left alive.
    # Words with an exponent field of 255 are ordinary data to the EE (its
    # compares saturate them to +-MAX); 0xFFC00000 is such a word.
    NEGX = 0xFFC00000
    xrng = random.Random(0x0E0E)
    ee_band = lambda n, patches, spad, q, run_set=RUN - vectors - maths: add(
        '826D40 ee-band %s' % n, beat, 0x826D40, [sentry],
        base + [(sentry + 4, b8(1)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0)),
                (sentry + 0x204, w32(npc)), (bone_a + 0xB0, f32b(-1.0)), (bone_a + 0xB8, f32b(0.0)),
                (sentry + 0x2A, h16(2))] + patches,
        [(0x70003600, f32b(0.0)), (0x70003608, f32b(0.0)), (0x70003604, f32b(0.1))] + spad,
        queues={**{0x11E748: [fb(1.0)], 0x8282F0: [0], 0x11E520: [fb(-0.3)]}, **q}, run_set=run_set)
    # heading: sub(pi, atan) for x < 0, sub(heading, yaw) for x > 0
    while True:
        at = fbits(xrng.uniform(-1.5, 1.5))
        yaw = fbits(xrng.uniform(-3.0, 3.0))
        if M.ee_sub(M.ee_sub(PI, at), yaw) != M.ee_sub(near(number(PI) - number(at)), yaw):
            break
    ee_band('pi-atan %X' % at, [(sentry + 0xC4, w32(yaw))], [(0x70003610, f32b(-0.5)), (0x70003618, f32b(0.3))],
            {0x11DF78: [fb(5.0)], 0x11DBB8: [at, fb(-0.8)], 0x1B1470: [fb(0.2), fb(-0.1)]})
    while True:
        at = fbits(xrng.uniform(-1.5, 1.5))
        yaw = fbits(xrng.uniform(-3.0, 3.0))
        if M.ee_sub(at ^ 0x80000000, yaw) != near(number(at ^ 0x80000000) - number(yaw)):
            break
    ee_band('heading-yaw %X' % at, [(sentry + 0xC4, w32(yaw))], [(0x70003610, f32b(0.5)), (0x70003618, f32b(0.3))],
            {0x11DF78: [fb(5.0)], 0x11DBB8: [at, fb(-0.8)], 0x1B1470: [fb(0.2), fb(-0.1)]})
    # the dead-band gates on words a host compare reads differently: a
    # negative denormal x (EE: zero, not below zero), a wrapped heading and a
    # magnitude with exponent field 255 (the magnitude is outside 0011DF78's
    # range, which clears the sign bit; the case pins the compare itself)
    ee_band('x -denormal', [], [(0x70003610, w32(0x80000001)), (0x70003618, f32b(0.3))],
            {0x11DF78: [fb(5.0)], 0x11DBB8: [fb(0.4), fb(-0.8)], 0x1B1470: [fb(0.2), fb(-0.1)]})
    ee_band('wrapped %X' % NEGX, [], [(0x70003610, f32b(0.5)), (0x70003618, f32b(0.3))],
            {0x11DF78: [fb(5.0)], 0x11DBB8: [fb(0.4), fb(-0.8)], 0x1B1470: [NEGX, fb(-0.1)]})
    ee_band('mag %X' % NEGX, [], [(0x70003610, f32b(0.5)), (0x70003618, f32b(0.3))],
            {0x11DF78: [NEGX], 0x11DBB8: [fb(-0.8)], 0x1B1470: [fb(0.2), fb(-0.1)]})
    # elevation step with B+0x78 an exponent-255 word (EE add/sub give +MAX,
    # so the store-f3 branch is reached) and with B+0x78 = -MAX (f3 = NEGX
    # reaches the store-f3 branch, and the EE low clamp then catches it)
    for cur, elev in ((NEGX, 0x7F7FFFFF), (NEGX, fbits(-0.8)), (0xFF7FFFFF, NEGX), (0x7FC00000, 0x7F800000),
                      (fbits(-0.8), NEGX), (fbits(-0.8), 0x7FC00000), (fbits(-0.8), 0x80000001)):
        ee_band('elev %X %X' % (cur, elev), [(bone_b + 0x78, w32(cur))], [],
                {0x11DF78: [fb(0.0)], 0x11DBB8: [elev]}, RUN - vectors - {0x11E748, 0x11DBB8, 0x11DF78})
    # side test far outside the band: A+0x74 -/+ STEP on operands where EE
    # and host disagree (the band-edge cases leave A inside the clamps)
    for sign in (-1.0, 1.0):
        v = disagreeing(xrng, lambda r: (uf(r, -1.1, 1.1),), lambda a: (M.ee_sub if sign < 0 else M.ee_add)(a, step_k),
                        lambda a: near(number(a) - number(step_k)) if sign < 0 else near(number(a) + number(step_k)),
                        within(-1.12, 1.12))[0]
        ee_band('side %g %X' % (sign, v), [(bone_a + 0x74, w32(v)), (bone_a + 0xB0, f32b(-1.0))],
                [(0x70003600, f32b(0.5 * sign)), (0x70003608, f32b(0.0))],
                {0x11DBB8: [fb(-0.8)], 0x11DF78: [fb(0.0)]}, RUN - vectors - {0x11E748, 0x11DBB8, 0x11DF78})
    # the tail's division on a B the 0x8282F0 stub set to +-MAX: EE DIV
    # saturates the quotient, a host division overflows to infinity
    for v in (0xFF7FFFFF, 0x7F7FFFFF):
        add('826D40 tail B from 8282F0 %X' % v, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(1)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0)),
                    (sentry + 0x204, w32(npc)), (sentry + 0x2A, h16(0)), (sentry + 0x28, h16(0))],
            queues={0x8282F0: [Scribble(0, [(bone_b + 0x78, w32(v))])]})
    # set-up pitch = MLIM + BASE * sin(0): the real sine of 0 makes both
    # operations exact, so the sine result is scripted with a value on which
    # EE mul and add disagree with the host
    while True:
        s = fbits(xrng.uniform(-1.0, 1.0))
        p_ee = M.ee_mul(0xBF543B67, s)
        if (p_ee != near(number(0xBF543B67) * number(s))
                and M.ee_add(MLIM, p_ee) != near(number(MLIM) + number(p_ee))
                and M.ee_add(MLIM, p_ee) != M.ee_add(MLIM, near(number(0xBF543B67) * number(s)))):
            break
    add('826D40 s0 sine %X' % s, beat, 0x826D40, [sentry], base + [(sentry + 4, b8(0))],
        queues={0x1B0FD0: [0], 0x1BA1C0: [0], 0x1AFA90: [spare], 0x11E2A8: [s]}, run_set=RUN - {0x11E2A8})
    # host compares on raw words: a denormal idle step (EE: not above zero),
    # an exponent-255 phase in state 2, wrap and height words in the NPC
    # callback and the bridge gate
    add('826D40 idle step denormal', beat, 0x826D40, [sentry],
        base + [(sentry + 4, b8(4)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(1)), (sentry + 0x28, h16(5)),
                (sentry + 0x1F4, w32(1)), (bone_a + 0x74, f32b(1.2))])
    add('826D40 wind phase %X' % NEGX, beat, 0x826D40, [sentry],
        base + [(sentry + 4, b8(2)), (sentry + 0x1FC, w32(NEGX)), (sentry + 0x1F8, f32b(0.044879895))],
        queues={0x1BA1C0: [1]})
    for wrap in (0x00000001, 0x7FC00000, NEGX):
        add('825240 p0 wrap %X' % wrap, beat, 0x825240, [npc, npc + 0x1F0, 0x82A3A0],
            [(npc + 0x1F4, b8(0))], queues={0x1B1470: [wrap]}, run_set=RUN - {0x1B1470})
    add('826440 gate height %X' % NEGX, beat, 0x826440, [bridge],
        [(bridge + 4, b8(1)), (bridge + 5, b8(0)), (bridge + 0xD, b8(3)), (0x810354, w32(NEGX))],
        [(0x70003B8D, b8(0))], queues={0x1BA1C0: [0], 0x1B1EA0: [1, 0, 0], 0x182BF0: [0]})
    # the A-record re-reads after the sweep's store, when A aliases its own
    # pointer word (the store replaces the pointer): the new A reads an
    # exponent-255 word. Countdown sweep and both idle sweep directions (a
    # denormal step is 'not above zero' and adds nothing, so the new A is 0)
    add('826D40 sweep re-reads A %X' % NEGX, beat, 0x826D40, [sentry],
        base + [(sentry + 4, b8(4)), (sentry + 0x208, w32(0x40)), (sentry + 0x20C, w32(3)),
                (sentry + 0x214, w32(3)), (sentry + 0x118, w32(sentry + 0x110 - 0x6C)),
                (sentry + 0x210, w32(0x01000000)), (0x1000074, w32(NEGX))])
    for step, at in ((0x01000000, 0x1000074), (0x00000001, 0x74)):
        add('826D40 idle re-reads A %X' % step, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(4)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(1)), (sentry + 0x28, h16(5)),
                    (sentry + 0x118, w32(sentry + 0x110 - 0x6C)), (sentry + 0x1F4, w32(step)), (at, w32(NEGX))])
    # +0x2A carrying out of its low byte in 0x826440; the re-arm's two
    # random tests (bit 3 of +0x20C, bit 2 of +0x214) on values 4 and 8;
    # the tracker's +0x28 hand-off when +0x28 + 1 is exactly 0
    for t2a in (0x00FF, 0xFFFF):
        add('826440 +0x2A %X' % t2a, beat, 0x826440, [bridge],
            [(bridge + 4, b8(1)), (bridge + 5, b8(1)), (bridge + 0xD, b8(3)), (0x8107E0, b8(0)),
             (bridge + 0x2A, h16(t2a))], queues={0x1BA1F0: [0], 0x1B1EA0: [0, 0]})
    for hi in (4370, 8739):
        add('826D40 rearm random %d' % hi, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(4)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0)), (sentry + 0x28, h16(5)),
                    (sentry + 0x36, h16(1))],
            queues={0x122BB8: [hi << 16, hi << 16, hi << 16]}, run_set=RUN - {0x122BB8})
    add('826D40 tail +0x28 FFFF', beat, 0x826D40, [sentry],
        base + [(sentry + 4, b8(1)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0)),
                (sentry + 0x204, w32(npc)), (sentry + 0x28, h16(0xFFFF)), (sentry + 0x2A, h16(5))],
        queues={0x8282F0: [0]})

    # --- fix round 3 -----------------------------------------------------------
    # (a) every test of a callee's integer result, with results the 0/1 cases
    # cannot tell apart: 2 and 3 (> 1, odd and even), 0x100 and 0x10000 (low
    # byte / halfword 0), 0x80000000 (negative, low halfword 0), 0xFFFFFFFF
    # (negative, odd), and for the `== 1` / `== 2` tests 0x101, 0x10001,
    # 0x102 and 0x10002. The original tests each result as a whole word.
    gate0 = [(bridge + 4, b8(1)), (bridge + 5, b8(0)), (bridge + 0xD, b8(3)), (0x810354, f32b(1.0))]
    result_sites = [
        ('door step 0 81', 0x823580, [door], [(door + 4, b8(1)), (door + 5, b8(0)), (0x8107D9, b8(0x81))], [],
         lambda r: {0x1BBE40: [r]}),
        ('door step 0 00', 0x823580, [door], [(door + 4, b8(1)), (door + 5, b8(0)), (0x8107D9, b8(0))], [],
         lambda r: {0x1BBE40: [r]}),
        ('door step 1', 0x823580, [door], [(door + 4, b8(1)), (door + 5, b8(1))], [], lambda r: {0x1BC0E0: [r]}),
        ('door step 2 00', 0x823580, [door], [(door + 4, b8(1)), (door + 5, b8(2)), (0x8107D9, b8(0))], [],
         lambda r: {0x1BC0E0: [r], 0x1BA1F0: [r]}),
        ('door step 2 81', 0x823580, [door], [(door + 4, b8(1)), (door + 5, b8(2)), (0x8107D9, b8(0x81))], [],
         lambda r: {0x1BC0E0: [r]}),
        ('door step 3', 0x823580, [door], [(door + 4, b8(1)), (door + 5, b8(3))], [], lambda r: {0x1BC0E0: [r]}),
        ('door step 5', 0x823580, [door], [(door + 4, b8(1)), (door + 5, b8(5))], [], lambda r: {0x1BC290: [r]}),
        ('door step 6', 0x823580, [door], [(door + 4, b8(1)), (door + 5, b8(6))], [], lambda r: {0x1BA1F0: [r]}),
        ('8254B0 step 2', 0x8254B0, [npc], [(npc + 5, b8(2)), (npc + 0xB, b8(4))], [], lambda r: {0x1BA1F0: [r]}),
        ('825590 step 2', 0x825590, [npc], [(npc + 5, b8(2)), (npc + 0xB, b8(4))], [], lambda r: {0x1BA1F0: [r]}),
        ('825670 step 2', 0x825670, [npc], [(npc + 5, b8(2)), (npc + 0xB, b8(4))], [], lambda r: {0x1BA1F0: [r]}),
        ('825130 pick', 0x825130, [npc, npc + 0x1F0, 0x82A020], [(npc + 0x1F4, b8(0))], [],
         lambda r: {0x1B1380: [r]}),
        ('826200 step 1', 0x826200, [bridge], [(bridge + 4, b8(1)), (bridge + 5, b8(1))], [],
         lambda r: {0x1BA1F0: [r], 0x1B1EA0: [0, 0]}),
        ('826200 tail', 0x826200, [bridge], [(bridge + 4, b8(1)), (bridge + 5, b8(2))], [],
         lambda r: {0x1B1EA0: [r, r]}),
        ('826440 step 1', 0x826440, [bridge], [(bridge + 4, b8(1)), (bridge + 5, b8(1)), (0x8107E0, b8(0))], [],
         lambda r: {0x1BA1F0: [r], 0x1B1EA0: [0, 0]}),
        ('826440 gate flag', 0x826440, [bridge], gate0, [(0x70003B8D, b8(0))],
         lambda r: {0x1BA1C0: [r], 0x1B1EA0: [1, 0, 0], 0x182BF0: [0]}),
        ('826440 gate quad', 0x826440, [bridge], gate0, [(0x70003B8D, b8(0))],
         lambda r: {0x1BA1C0: [0], 0x1B1EA0: [r, 0, 0], 0x182BF0: [0]}),
        ('826440 tail quad CC20', 0x826440, [bridge], [(bridge + 4, b8(1)), (bridge + 5, b8(2))], [],
         lambda r: {0x1B1EA0: [r, 0]}),
        ('826440 tail quad CC60', 0x826440, [bridge], [(bridge + 4, b8(1)), (bridge + 5, b8(2))], [],
         lambda r: {0x1B1EA0: [0, r]}),
        ('826440 gate busy', 0x826440, [bridge], gate0, [(0x70003B8D, b8(0))],
         lambda r: {0x1BA1C0: [0], 0x1B1EA0: [1, 0, 0], 0x182BF0: [r]}),
        ('8267C0 s0', 0x8267C0, [fixture], [(fixture + 4, b8(0))], [], lambda r: {0x1B0FD0: [r]}),
        ('8267C0 s1 bit 15', 0x8267C0, [fixture], [(fixture + 4, b8(1)), (fixture + 5, b8(0))], [],
         lambda r: {0x1BA1C0: [r, 1], 0x1B1EA0: [1, 1], 0x1B17A0: [1]}),
        ('8267C0 s1 bit 7', 0x8267C0, [fixture], [(fixture + 4, b8(1)), (fixture + 5, b8(0))], [],
         lambda r: {0x1BA1C0: [0, r], 0x1B1EA0: [1, 1], 0x1B17A0: [1]}),
        ('8267C0 s1 quad 0', 0x8267C0, [fixture], [(fixture + 4, b8(1)), (fixture + 5, b8(0))], [],
         lambda r: {0x1BA1C0: [0, 1], 0x1B1EA0: [r, 1], 0x1B17A0: [1]}),
        ('8267C0 s1 quad 2', 0x8267C0, [fixture], [(fixture + 4, b8(1)), (fixture + 5, b8(0))], [],
         lambda r: {0x1BA1C0: [0, 1], 0x1B1EA0: [1, r], 0x1B17A0: [1]}),
        ('8267C0 s1 step 1', 0x8267C0, [fixture], [(fixture + 4, b8(1)), (fixture + 5, b8(1))], [],
         lambda r: {0x1BA1C0: [0, 1], 0x1BA1F0: [r], 0x1B17A0: [1]}),
        ('8267C0 s1 draw', 0x8267C0, [fixture], [(fixture + 4, b8(1)), (fixture + 5, b8(2))], [],
         lambda r: {0x1BA1C0: [1], 0x1B17A0: [r]}),
        ('828850 s0', 0x828850, [pair], [(pair + 4, b8(0))], [], lambda r: {0x1B0FD0: [r], 0x1B11E0: [0]}),
        ('828850 s0 11E0', 0x828850, [pair], [(pair + 4, b8(0))], [], lambda r: {0x1B0FD0: [0], 0x1B11E0: [r]}),
        ('828850 s1', 0x828850, [pair], [(pair + 4, b8(1)), (pair + 0x36, h16(1))], [],
         lambda r: {0x1EFE00: [r]}),
        ('826D40 s0 init', 0x826D40, [sentry], base + [(sentry + 4, b8(0))], [],
         lambda r: {0x1B0FD0: [r], 0x1BA1C0: [0], 0x1AFA90: [spare]}),
        ('826D40 s0 alarm', 0x826D40, [sentry], base + [(sentry + 4, b8(0))], [],
         lambda r: {0x1B0FD0: [0], 0x1BA1C0: [r], 0x1AFA90: [spare]}),
        ('826D40 s64', 0x826D40, [sentry], base + [(sentry + 4, b8(0x64))], [], lambda r: {0x1BA1C0: [r]}),
        ('826D40 s2', 0x826D40, [sentry], base + [(sentry + 4, b8(2)), (sentry + 0x1FC, f32b(-1.6))], [],
         lambda r: {0x1BA1C0: [r]}),
        # 0x8282F0 in the tracker: `== 2` resets +0x2A, `!= 0` gates the
        # period effect (+0x200 = 0xD reaches the period this frame)
        ('826D40 track 8282F0', 0x826D40, [sentry],
         base + [(sentry + 4, b8(1)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0)),
                 (sentry + 0x204, w32(npc)), (sentry + 0x2A, h16(5))], [], lambda r: {0x8282F0: [r]}),
    ]
    equals_one = ('826440 gate quad', '826440 tail quad CC20', '826440 tail quad CC60', '826200 tail')
    for name, entry, args, patches, spad, queues in result_sites:
        extra = (0x101, 0x10001) if name in equals_one else (0x102, 0x10002) if name == '826D40 track 8282F0' else ()
        for r in (2, 3, 0x100, 0x10000, 0x80000000, 0xFFFFFFFF) + extra:
            add('%s result %X' % (name, r), beat, entry, args, patches, spad, queues=queues(r))
    for r in (0, 1, 2, 3, 0x100, 0x10000, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFE, 0xFFFFFFFF):
        fire_case('period 8282F0 %X' % r, 1, 0, 0, 0, {0x8282F0: [r]})
    for r in (1, 3, 0x100, 0x10000, 0x80000000, 0xFFFFFFFF):
        fire_case('0019B6C0 %X' % r, 0, 0, 0, r, {})
    # 001AFA90's result is the companion pointer, tested against zero: with a
    # word whose bit 31 is set the original goes on to store through it, and
    # the harness maps no address there, so the original stops at that store
    # and the translation must stop at the same address (finish)
    for companion in (0x80000000 | spare, 0xC0000000 | spare, 0xFFFFFF00, 0x40000000, 0x80000000):
        add('826D40 s0 companion %X' % companion, beat, 0x826D40, [sentry], base + [(sentry + 4, b8(0))],
            queues={0x1B0FD0: [0], 0x1BA1C0: [0], 0x1AFA90: [companion]})

    # (b) callees that change what the original reads after them: the
    # 826440 gate re-reads the height D_00810350+4 after 001B1EA0 and the
    # scratchpad byte 0x70003B8D after 001BA1C0 (both directions of each)
    for before, after in ((1.0, 5.0), (5.0, 1.0), (2.0, 2.0000002), (2.0000002, 2.0)):
        add('826440 gate height %g after 001B1EA0 %g' % (before, after), beat, 0x826440, [bridge],
            gate0 + [(0x810354, f32b(before))], [(0x70003B8D, b8(0))],
            queues={0x1BA1C0: [0], 0x1B1EA0: [Scribble(1, [(0x810354, f32b(after))]), 0, 0], 0x182BF0: [0]})
    for before, after in ((0, 1), (1, 0)):
        add('826440 gate 3B8D %d after 001BA1C0 %d' % (before, after), beat, 0x826440, [bridge], gate0,
            [(0x70003B8D, b8(before))],
            queues={0x1BA1C0: [Scribble(0, [(0x70003B8D, b8(after))])], 0x1B1EA0: [1, 0, 0], 0x182BF0: [0]})
    # the door re-reads its step byte for the increments and the story byte
    # for step 2's gate and for the tail
    for step, callee in ((0, 0x1BBE40), (1, 0x1BC0E0), (3, 0x1BC0E0), (4, 0x1BC240)):
        add('door step %d re-reads +0x05' % step, beat, 0x823580, [door],
            [(door + 4, b8(1)), (door + 5, b8(step)), (0x8107D9, b8(0))],
            queues={callee: [Scribble(1, [(door + 5, b8(0x40 | step))])]})
    for gate, new in ((0x81, 0), (0, 0x81), (0x80, 0x81)):
        add('door step 2 story %02X -> %02X' % (gate, new), beat, 0x823580, [door],
            [(door + 4, b8(1)), (door + 5, b8(2)), (0x8107D9, b8(gate))],
            queues={0x1BC0E0: [Scribble(1, [(0x8107D9, b8(new))])], 0x1BA1F0: [0]})
        add('door step 5 story %02X -> %02X' % (gate, new), beat, 0x823580, [door],
            [(door + 4, b8(1)), (door + 5, b8(5)), (0x8107D9, b8(gate))],
            queues={0x1BC290: [Scribble(0, [(0x8107D9, b8(new))])]})

    # (c) every value of every byte the functions dispatch on or compare:
    # each handled value with each single bit flipped, plus 0x7F, 0x80 and
    # 0xFF (EM_TEST_FULL=1: all 256 values). A masked, widened or
    # sign-extended test of the byte maps some of these onto a handled value.
    def byte_values(handled):
        if reference_mode.FULL:
            return range(256)
        return sorted({0x7F, 0x80, 0xFF} | {h ^ (1 << b) for h in handled for b in range(8)} | set(handled))
    byte_sites = [
        ('door +0x04', 0x823580, [door], door + 4, [(door + 5, b8(7))], [], {}, (0, 1, 2, 3)),
        ('door +0x05', 0x823580, [door], door + 5, [(door + 4, b8(1)), (0x8107D9, b8(0))], [],
         {a: [1] for a in (0x1BBE40, 0x1BC0E0, 0x1BC290, 0x1BA1F0)}, range(7)),
        ('door step 0 story', 0x823580, [door], 0x8107D9, [(door + 4, b8(1)), (door + 5, b8(0))], [],
         {0x1BBE40: [1]}, (0x81,)),
        ('door step 2 story', 0x823580, [door], 0x8107D9, [(door + 4, b8(1)), (door + 5, b8(2))], [],
         {0x1BC0E0: [1], 0x1BA1F0: [0]}, (0, 0x80, 0x81)),
        ('talk 8254B0 +0x05', 0x8254B0, [npc], npc + 5, [(npc + 0xB, b8(4))], [], {0x1BA1F0: [1]}, (0, 1, 2)),
        ('talk 825590 +0x05', 0x825590, [npc], npc + 5, [(npc + 0xB, b8(4))], [], {0x1BA1F0: [1]}, (0, 1, 2)),
        ('talk 825670 +0x05', 0x825670, [npc], npc + 5, [(npc + 0xB, b8(4))], [], {0x1BA1F0: [1]}, (0, 1, 2)),
        ('npc +0x04', 0x825350, [npc], npc + 4, [(0x81075A, b8(0)), (0x8107D9, b8(0x80)), (npc + 5, b8(2))], [],
         {0x1BA1F0: [1]}, (0, 1, 2, 3)),
        ('npc story', 0x825350, [npc], 0x8107D9, [(npc + 4, b8(1)), (npc + 5, b8(2))], [], {0x1BA1F0: [1]},
         (0, 0x80, 0x81)),
        ('npc 75A', 0x825350, [npc], 0x81075A, [(npc + 4, b8(0))], [], {}, (0,)),
        ('825130 phase', 0x825130, [npc, npc + 0x1F0, 0x82A020], npc + 0x1F4, [], [], {0x1B1380: [1]}, (0, 1)),
        ('825240 phase', 0x825240, [npc, npc + 0x1F0, 0x82A3A0], npc + 0x1F4, [], [], {}, (0, 1)),
        ('8261A0 +0x04', 0x8261A0, [bridge], bridge + 4, [(bridge + 0xD, b8(0))], [], {}, (0,)),
        ('8261A0 +0x0D', 0x8261A0, [bridge], bridge + 0xD, [(bridge + 4, b8(2))], [], {}, (2, 3)),
        ('826200 +0x04', 0x826200, [bridge], bridge + 4, [(bridge + 5, b8(2))], [], {}, (0, 1, 2, 3)),
        ('826200 +0x05', 0x826200, [bridge], bridge + 5, [(bridge + 4, b8(1)), (0x810760, b8(0)),
                                                          (0x8107E0, b8(1))], [], {0x1BA1F0: [1]}, (0, 1)),
        ('826200 760', 0x826200, [bridge], 0x810760, [(bridge + 4, b8(1)), (bridge + 5, b8(0)),
                                                      (0x8107E0, b8(1))], [], {}, (0, 0xFF)),
        ('826200 7E0', 0x826200, [bridge], 0x8107E0, [(bridge + 4, b8(1)), (bridge + 5, b8(0)),
                                                      (0x810760, b8(0))], [], {}, (0,)),
        ('826200 s0 75E', 0x826200, [bridge], 0x81075E, [(bridge + 4, b8(0)), (0x810760, b8(0)),
                                                         (0x810784, b8(0))], [], {}, (0,)),
        ('826200 s0 760', 0x826200, [bridge], 0x810760, [(bridge + 4, b8(0)), (0x810784, b8(0))], [], {}, (0,)),
        ('826200 s0 784', 0x826200, [bridge], 0x810784, [(bridge + 4, b8(0))], [], {}, (0,)),
        ('826440 +0x04', 0x826440, [bridge], bridge + 4, [(bridge + 5, b8(2))], [], {}, (0, 1, 2, 3)),
        ('826440 +0x05', 0x826440, [bridge], bridge + 5, [(bridge + 4, b8(1)), (0x8107E0, b8(0)),
                                                          (0x810354, f32b(1.0))], [(0x70003B8D, b8(0))],
         {0x1B1EA0: [1, 0, 0], 0x1BA1F0: [1]}, (0, 1)),
        ('826440 s0 75E', 0x826440, [bridge], 0x81075E, [(bridge + 4, b8(0)), (0x810760, b8(0)),
                                                         (0x810784, b8(0))], [], {}, (0,)),
        ('826440 s0 760', 0x826440, [bridge], 0x810760, [(bridge + 4, b8(0)), (0x81075E, b8(1)),
                                                         (0x810784, b8(0))], [], {}, (0,)),
        ('826440 s0 784', 0x826440, [bridge], 0x810784, [(bridge + 4, b8(0)), (0x81075E, b8(1))], [], {}, (0,)),
        ('826440 3B8D', 0x826440, [bridge], 0x70003B8D, gate0, [], {0x1B1EA0: [1, 0, 0]}, (0,)),
        ('826440 7E0', 0x826440, [bridge], 0x8107E0, [(bridge + 4, b8(1)), (bridge + 5, b8(1)), (bridge + 6, b8(1)),
                                                      (bridge + 7, b8(0)), (bridge + 0x240, w32(0x3B))], [], {},
         (0xE0, 2)),
        ('826440 +0x06', 0x826440, [bridge], bridge + 6, [(bridge + 4, b8(1)), (bridge + 5, b8(1)),
                                                          (0x8107E0, b8(0xE0)), (bridge + 0x240, w32(0x3B))], [], {},
         (0, 1)),
        ('826440 +0x07', 0x826440, [bridge], bridge + 7, [(bridge + 4, b8(1)), (bridge + 5, b8(1)),
                                                          (0x8107E0, b8(2)), (0x810360, f32b(12.5))], [], {}, (0,)),
        ('8267C0 +0x04', 0x8267C0, [fixture], fixture + 4, [(fixture + 5, b8(2))], [], {0x1BA1C0: [0, 1]},
         (0, 1, 2, 3)),
        ('8267C0 +0x05', 0x8267C0, [fixture], fixture + 5, [(fixture + 4, b8(1))], [],
         {0x1BA1C0: [0, 1], 0x1B1EA0: [1, 1], 0x1BA1F0: [1]}, (0, 1)),
        ('826CF0 +0x03', 0x826CF0, [owners(ram, 0x826CF0)[0]], owners(ram, 0x826CF0)[0] + 3, [], [], {}, (1,)),
        ('828850 +0x04', 0x828850, [pair], pair + 4, [(pair + 0x36, h16(1)), (pair + 0x28, h16(9))], [],
         {0x1EFE00: [1]}, (0, 1, 2)),
        ('826D40 +0x04', 0x826D40, [sentry], sentry + 4, base + [(sentry + 0x208, w32(0)), (sentry + 0x204, w32(npc)),
                                                                 (sentry + 0x1FC, f32b(-1.6))], [],
         {0x1BA1C0: [1], 0x1AFA90: [spare]}, (0, 0x64, 4, 1, 2, 3)),
    ]
    for name, entry, args, where, patches, spad, queues, handled in byte_sites:
        for value in byte_values(handled):
            if where >= 0x70000000:
                add('byte %s = %02X' % (name, value), beat, entry, args, patches, spad + [(where, b8(value))],
                    queues=queues)
            else:
                add('byte %s = %02X' % (name, value), beat, entry, args, patches + [(where, b8(value))], spad,
                    queues=queues)
    for value in byte_values((0x5A, 0x5B, 0x5C)):
        fire_case('surface %02X' % value, 0, 0, 0, 2, {}, patches=[(0x8102B0 + 0x1A, b8(value))])

    # (d) 0x826D40 state 2 at -pi/2: the +0x21C count is a signed word
    # (`> 0`), the lane word +0x224 a whole word (`!= 0`); and the +0x36
    # halfword (`!= 0`) with a zero low byte, in states 4 and 1
    for left, lane in ((0xFFFFFFFF, 0), (0x80000000, 0), (0x7FFFFFFF, 0), (0x100, 0), (0x10000, 1),
                       (45, 0x100), (45, 0x10000), (45, 0x80000000), (45, 0xFFFFFFFF)):
        add('826D40 wind left %X lane %X' % (left, lane), beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(2)), (sentry + 0x1FC, f32b(-1.5707964)), (sentry + 0x1F8, f32b(0.044879895)),
                    (sentry + 0x21C, w32(left)), (sentry + 0x224, w32(lane))], queues={0x1BA1C0: [1]})
    for raised in (0x100, 0x8000):
        for state in (4, 1):
            add('826D40 +0x36 %X s%d' % (raised, state), beat, 0x826D40, [sentry],
                base + [(sentry + 4, b8(state)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0)),
                        (sentry + 0x204, w32(npc if state == 1 else 0)), (sentry + 0x36, h16(raised)),
                        (sentry + 0x28, h16(5)), (sentry + 0x2A, h16(5))],
                queues={0x8282F0: [0], 0x122BB8: [bits_set, bits_set, bits_set]}, run_set=RUN - {0x122BB8})
        add('828850 +0x36 %X' % raised, beat, 0x828850, [pair], [(pair + 4, b8(1)), (pair + 0x36, h16(raised))],
            queues={0x1EFE00: [1]})

    # (f) words the functions store and then test in the same stretch between
    # two calls, with values whose low byte or halfword alone decides the test
    # differently: the 826440 ramp timer (+0x240, a whole word compared with
    # 0x3C and 0xC8), 828850's +0x28 (a signed halfword, `< 0xA` then
    # `== 0xA`), the idle +0x28 (`< 0` after the decrement), the tracker's
    # +0x200 (`>= 0xE` after the increment) and +0x28 (`> 0`)
    for timer in (0x1003B, 0x100C7, 0x8000003B, 0xFFFF003B):
        add('826440 ramp timer %X' % timer, beat, 0x826440, [bridge],
            [(bridge + 4, b8(1)), (bridge + 5, b8(1)), (bridge + 6, b8(1)), (bridge + 0xD, b8(3)),
             (0x8107E0, b8(0xE0)), (bridge + 0x240, w32(timer))], queues={0x1BA1F0: [0], 0x1B1EA0: [0, 0]})
    for t28 in (0xFF09, 0x8009, 0x0109):
        add('828850 s2 +0x28 %X' % t28, beat, 0x828850, [pair], [(pair + 4, b8(2)), (pair + 0x28, h16(t28))])
    for h28 in (0x81, 0xFF01, 0x100, 0x8000):
        add('826D40 idle +0x28 %X' % h28, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(4)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(1)), (sentry + 0x28, h16(h28)),
                    (sentry + 0x1F4, f32b(0.0162))],
            queues={0x122BB8: [bits_set, bits_set, bits_set]}, run_set=RUN - {0x122BB8})
    for count in (0x10000, 0x8000, 0xFFFF, 0x1000D):
        add('826D40 tail count %X' % count, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(1)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(count)),
                    (sentry + 0x204, w32(npc)), (sentry + 0x28, h16(3)), (sentry + 0x2A, h16(5))],
            queues={0x8282F0: [1], 0x122BB8: [bits_set, bits_set, bits_set]}, run_set=RUN - {0x122BB8})
    for t28 in (0x8000, 0xFFFE, 0x7FFF, 0x00FF):
        add('826D40 tail +0x28 %X' % t28, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(1)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0)),
                    (sentry + 0x204, w32(npc)), (sentry + 0x28, h16(t28)), (sentry + 0x2A, h16(5))],
            queues={0x8282F0: [0]})

    # (e) signalling-NaN floats (exponent 255, bit 22 clear) through the
    # hooks both ways: as a stubbed callee's result that the original stores,
    # and as a float argument read from memory
    for snan in (0x7F800001, 0x7FA00000, 0xFF800001):
        add('825130 p1 sNaN %X' % snan, beat, 0x825130, [npc, npc + 0x1F0, 0x82A020],
            [(npc + 0x1F4, b8(1)), (npc + 0xC4, w32(snan))], queues={0x1B1240: [snan], 0x1B12B0: [snan ^ 0x200000]},
            run_set=RUN - {0x1B1240, 0x1B12B0})
        add('825130 p0 sNaN %X' % snan, beat, 0x825130, [npc, npc + 0x1F0, 0x82A020],
            [(npc + 0x1F4, b8(0)), (npc + 0xC4, w32(snan))], queues={0x1B1380: [1]})
        add('825240 p1 sNaN %X' % snan, beat, 0x825240, [npc, npc + 0x1F0, 0x82A3A0],
            [(npc + 0x1F4, b8(1)), (0x82A3A0 + 0x24, w32(snan)), (0x82A3A0 + 0xC, w32(snan ^ 0x100))],
            queues={0x1B12B0: [snan ^ 0x200000]}, run_set=RUN - {0x1B12B0})
        add('826D40 tail sNaN %X' % snan, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(1)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0)),
                    (sentry + 0x204, w32(npc)), (sentry + 0x2A, h16(0)), (sentry + 0x28, h16(0))],
            queues={0x8282F0: [0], 0x11E520: [snan], 0x1B1470: [snan ^ 0x200000]},
            run_set=RUN - {0x11E520, 0x1B1470})

    # --- fix round 4 -----------------------------------------------------------
    # (g) inputs for the round-3 sweep's survivors (docs/AREA01_OVERLAY.md
    # section 3). The ordered access check already kills the cache, re-read
    # and reorder forms among them; these cases show each one as a
    # difference in behaviour as well.
    # The door's step 0 on the story-0x81 branch (and the other branch) with
    # 001BBE40 changing +0x05: the original stores a constant step
    for gate in (0x81, 0):
        for scribbled in (0x40, 0x03, 0xFF):
            add('door step0 %02X BBE40 writes +5 %02X' % (gate, scribbled), beat, 0x823580, [door],
                [(door + 4, b8(1)), (door + 5, b8(0)), (0x8107D9, b8(gate))],
                queues={0x1BBE40: [Scribble(1, [(door + 5, b8(scribbled))])]})
    # the NPC passes +0x0D to 001BA580 zero-extended
    for d in (0x80, 0xFF, 0x7F):
        add('npc s1 +0xD %02X' % d, beat, 0x825350, [npc],
            [(npc + 4, b8(1)), (0x8107D9, b8(0)), (npc + 5, b8(2)), (npc + 0xD, b8(d))],
            queues={0x1BA1F0: [1]})
    # the countdown's two counts are signed words tested `> 0` from the
    # register: 0 and negative (reseed), and 0x10000 / 0x8001 (a positive
    # word whose low halfword is 0xFFFF / 0x8000)
    for c20c, c214 in ((0, 2), (0xFFFFFFF0, 2), (0x80000000, 2), (2, 0), (2, 0xFFFFFFF0), (2, 0x80000000),
                       (0x10000, 2), (0x8001, 2), (2, 0x10000), (2, 0x8001)):
        add('826D40 count signs 20C %X 214 %X' % (c20c, c214), beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(4)), (sentry + 0x208, w32(0x22)), (sentry + 0x20C, w32(c20c)),
                    (sentry + 0x214, w32(c214)), (bone_a + 0x74, f32b(0.5)),
                    (sentry + 0x210, f32b(0.10471976)), (sentry + 0x1FC, f32b(0.5))],
            queues={0x122BB8: [0, 0, 0]}, run_set=RUN - {0x122BB8})
    # the companion pointer +0x220 is loaded again for every store through
    # it: a companion placed so that its +0xA0, +0xA4, +0xA8 or +0xAC is the
    # pointer word itself, at every site that stores the four colour words
    companion_sites = [
        ('idle', [(sentry + 4, b8(4)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0)), (sentry + 0x28, h16(5))],
         {0x8282F0: [0]}),
        ('track', [(sentry + 4, b8(1)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0)), (sentry + 0x204, w32(npc)),
                   (sentry + 0x2A, h16(5))], {0x8282F0: [0]}),
        ('count sweep', [(sentry + 4, b8(4)), (sentry + 0x208, w32(0x22)), (sentry + 0x20C, w32(2)),
                         (sentry + 0x214, w32(2))], {}),
        ('fade s4', [(sentry + 4, b8(4)), (sentry + 0x208, w32(6))], {}),
        ('fade s1', [(sentry + 4, b8(1)), (sentry + 0x208, w32(6))], {}),
        ('wind lane 1', [(sentry + 4, b8(2)), (sentry + 0x1FC, f32b(-1.5707964)), (sentry + 0x21C, w32(45)),
                         (sentry + 0x224, w32(1))], {0x1BA1C0: [1]}),
        ('wind lane 0', [(sentry + 4, b8(2)), (sentry + 0x1FC, f32b(-1.5707964)), (sentry + 0x21C, w32(45)),
                         (sentry + 0x224, w32(0))], {0x1BA1C0: [1]}),
    ]
    for name, patches, queues in companion_sites:
        for word in (0xA0, 0xA4, 0xA8, 0xAC):
            add('826D40 %s companion +0x%X = +0x220' % (name, word), beat, 0x826D40, [sentry],
                base + patches + [(sentry + 0x220, w32(sentry + 0x220 - word))], queues=queues)
    # the idle sweep tests the step +0x1F4 again after storing A+0x74: A
    # placed so that A+0x74 is +0x1F4, with negative exponent-255 steps (the
    # EE add of such a step to itself gives +MAX, which is above zero)
    for step in (0xFF800001, 0xFFC00000, 0xFF800000):
        add('826D40 idle A+0x74 = +0x1F4 step %08X' % step, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(4)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(1)), (sentry + 0x28, h16(5)),
                    (sentry + 0x118, w32(sentry + 0x180)), (sentry + 0x1F4, w32(step))], queues={0x8282F0: [0]})
    # the tracker's A clamps: D_00275B40 placed so that the A pointer is the
    # word at +0x28 and +0x2A is its high half; the `+0x2A -= 4` store moves
    # A, and the original clamps the new A (both clamp directions)
    for low_half, angle in ((0x0070, -2.0), (0x0070, 2.0)):
        a_rec = low_half << 16
        add('826D40 track A = +0x28 word %g' % angle, beat, 0x826D40, [sentry],
            [(0x275B40, w32(sentry + 0x20)), (sentry + 4, b8(1)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0)),
             (sentry + 0x204, w32(npc)), (sentry + 0x28, h16(0)), (sentry + 0x2A, h16(low_half)),
             (sentry + 0x2C, w32(bone_b)), (a_rec + 0xB0, f32b(-1.0)), (a_rec + 0xB8, f32b(0.0)),
             (a_rec + 0xC0, f32b(0.0)), (a_rec + 0x74, f32b(angle)), (a_rec - 0x40000 + 0x74, f32b(angle))],
            [(0x70003B68, w32(0x40)), (0x70003600, f32b(-0.5)), (0x70003608, f32b(0.0)), (0x70003604, f32b(0.1)),
             (0x70003610, f32b(1.0)), (0x70003618, f32b(0.5))],
            queues={0x11DF78: [fb(1.0)], 0x11DBB8: [fb(0.1)], 0x11E748: [fb(1.0)], 0x8282F0: [0],
                    0x11E520: [fb(-0.3)], 0x1B1470: [fb(0.1)]},
            run_set=RUN - vectors - maths - {0x1B1470})
    # the period effect copies hit+0x24/+0x28/+0x2C to 0x700038B0..B8 one
    # word at a time: hit = 0x70003888, so hit+0x28 is 0x700038B0, which the
    # first copy overwrites (kind 1 and 3 on the first copy; the second copy
    # after a non-zero 0019B6C0)
    for kind, found in ((1, 0), (3, 0), (0, 2)):
        add('826D40 effect hit 70003888 kind %d found %d' % (kind, found), beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(1)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0xD)),
                    (sentry + 0x204, w32(npc)), (npc + 2, b8(1)), (npc + 0, b8(1)), (sentry + 0x36, h16(0))],
            [(0x700031D0, w32(0x70003888)), (0x700031D4, w32(npc)), (0x700031D8, w32(kind)),
             (0x700038B0, w32(0x11111111)), (0x700038B4, w32(0x22222222)), (0x700038B8, w32(0x33333333)),
             (0x700031BC, w32(0x3F000000)), (0x70003B68, w32(1))],
            queues={0x8282F0: [1], 0x19B6C0: [found]})
    # (h) every EE negation (NEG.S: an exponent-255 word saturates, then the
    # sign flips; a zero or denormal flips raw) on operands where a
    # subtraction from +0 (+0, a denormal) or a raw sign flip (an
    # exponent-255 word) gives another word. EE ADD returns +MAX for a NaN
    # operand and -MAX for -Inf, so the low clamps are reached with -Inf.
    for angle, steps in ((2.0, (0, 1, 0x7FC00000)), (-2.0, (0, 1, 0xFFC00000, 0xFF800000))):
        for step in steps:   # sweep_a's two clamps negate +0x210
            add('826D40 sweep clamp %g step %08X' % (angle, step), beat, 0x826D40, [sentry],
                base + [(sentry + 4, b8(4)), (sentry + 0x208, w32(0x22)), (sentry + 0x20C, w32(3)),
                        (sentry + 0x214, w32(3)), (bone_a + 0x74, f32b(angle)), (sentry + 0x210, w32(step)),
                        (sentry + 0x1FC, f32b(0.5))])
    for step in (0, 1, 0x7FC00000):
        # the re-arm negates +0x210 and +0x218 on bits 3 and 2 of its randoms
        add('826D40 rearm negates %08X' % step, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(4)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0)), (sentry + 0x28, h16(5)),
                    (sentry + 0x36, h16(1)), (sentry + 0x210, w32(step)), (sentry + 0x218, w32(step))],
            queues={0x8282F0: [0], 0x122BB8: [bits_set, bits_set, bits_set]}, run_set=RUN - {0x122BB8})
        # the countdown's reseeds negate them on bits 2 and 3
        add('826D40 reseed negates %08X' % step, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(4)), (sentry + 0x208, w32(0x22)), (sentry + 0x20C, w32(1)),
                    (sentry + 0x214, w32(1)), (sentry + 0x210, w32(step)), (sentry + 0x218, w32(step))],
            queues={0x122BB8: [bits_set, bits_set, bits_set]}, run_set=RUN - {0x122BB8})
    for step, angle in ((0x7FC00000, 0.5), (0xFFC00000, 0.5), (0xFF800000, 0.5), (0, -2.0), (1, -2.0),
                        (0x80000001, -2.0)):
        # the idle sweep's clamps negate +0x1F4
        add('826D40 idle clamp negates %08X' % step, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(4)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(1)), (sentry + 0x28, h16(5)),
                    (sentry + 0x1F4, w32(step)), (bone_a + 0x74, f32b(angle))], queues={0x8282F0: [0]})
    # the tracker's side test negates A+0xB0 and A+0xB8: an exponent-255
    # A+0xB0 times a small direction x (the product stays normal), and all
    # four side operands zero (the zero signs decide the stored sum)
    for ax, az, x, z in ((0x7FC00000, fbits(1.0), fbits(1e-10), 0), (0xFFA00000, fbits(1.0), fbits(1e-10), 0),
                         (0, 0, 0, fbits(1.0)), (0, 1, 0, fbits(1.0)), (0x80000000, 0x80000000, 0, fbits(1.0))):
        add('826D40 side negations %X %X %X %X' % (ax, az, x, z), beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(1)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0)),
                    (sentry + 0x204, w32(npc)), (bone_a + 0xB0, w32(ax)), (bone_a + 0xB8, w32(az))],
            [(0x70003600, w32(x)), (0x70003608, w32(z)), (0x70003604, f32b(0.2)), (0x70003610, f32b(0.3))],
            queues={0x11DF78: [fb(0.0)], 0x11E748: [fb(1.0)], 0x11DBB8: [fb(0.1)], 0x8282F0: [0]},
            run_set=RUN - vectors - {0x11DF78, 0x11E748, 0x11DBB8})
    # the heading negates the 0011DBB8 result when x >= 0
    for at in (0, 1, 0x80000000, 0x7FC00000, 0xFFA00000):
        add('826D40 heading negates %08X' % at, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(1)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0)),
                    (sentry + 0x204, w32(npc)), (bone_a + 0xB0, f32b(-1.0)), (bone_a + 0xB8, f32b(0.0)),
                    (bone_a + 0x74, f32b(0.2)), (bone_b + 0x78, f32b(0.3)), (sentry + 0x2A, h16(2))],
            [(0x70003B68, w32(0x40)), (0x70003600, f32b(0.0)), (0x70003608, f32b(0.0)), (0x70003604, f32b(0.1)),
             (0x70003610, f32b(0.5)), (0x70003618, f32b(0.5))],
            queues={0x11DF78: [fb(5.0)], 0x11DBB8: [at, fb(-0.2)], 0x11E748: [fb(1.0)], 0x1B1470: [fb(0.7), fb(-0.1)],
                    0x8282F0: [0], 0x11E520: [fb(-0.3)]},
            run_set=RUN - vectors - maths)
    # (i) sign and overflow: the NPC's set-up passes +0x0D zero-extended to
    # 001B10B0 and 001BA8E0; words next to 0x7FFFFFFF where a signed
    # addition would overflow (826440's ramp timer +1; 828850's partner
    # pointer +4 and +0x21C, where the original stops at the partner store)
    for d in (0x80, 0xFF):
        add('npc s0 +0xD %02X' % d, beat, 0x825350, [npc],
            [(npc + 4, b8(0)), (0x81075A, b8(0)), (npc + 0xD, b8(d))])
    add('826440 ramp timer 7FFFFFFF', beat, 0x826440, [bridge],
        [(bridge + 4, b8(1)), (bridge + 5, b8(1)), (bridge + 6, b8(1)), (bridge + 0xD, b8(3)),
         (0x8107E0, b8(0xE0)), (bridge + 0x240, w32(0x7FFFFFFF))], queues={0x1BA1F0: [0], 0x1B1EA0: [0, 0]})
    for partner in (0x7FFFFFFC, 0x7FFFFFFD, 0x7FFFFFFE, 0x7FFFFFFF, 0x7FFFFDE4, 0x7FFFFF00):
        add('828850 s0 partner %X' % partner, beat, 0x828850, [pair],
            [(pair + 4, b8(0)), (pair + 0x18, w32(partner))], queues={0x1B0FD0: [0], 0x1B11E0: [1]})
        add('828850 s1 partner %X' % partner, beat, 0x828850, [pair],
            [(pair + 4, b8(1)), (pair + 0x36, h16(1)), (pair + 0x18, w32(partner))], queues={0x1EFE00: [1]})
    # (j) constants the round-4 literal sweep found untested: the side band's
    # low edge -STEP exactly and one unit either side (f2 = x when A+0xB0 is
    # -1 and z is 0); random words whose scaled values tell 29, 30 and 31
    # apart and single out bits 0, 2 and 3 (re-arm and reseed); the idle
    # sound tick's mask on +0x28 = 3 and 4 (2 and 3 after the decrement);
    # +0x208 = 1, the smallest count that runs the countdown
    for side in (0xBC3EA2F1, 0xBC3EA2F2, 0xBC3EA2F0):
        add('826D40 side band low edge %08X' % side, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(1)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0)),
                    (sentry + 0x204, w32(npc)), (bone_a + 0xB0, f32b(-1.0)), (bone_a + 0xB8, f32b(0.0)),
                    (bone_a + 0x74, f32b(0.2))],
            [(0x70003600, w32(side)), (0x70003608, w32(0)), (0x70003604, f32b(0.2)), (0x70003610, f32b(0.3))],
            queues={0x11DF78: [fb(0.0)], 0x11E748: [fb(1.0)], 0x11DBB8: [fb(0.1)], 0x8282F0: [0]},
            run_set=RUN - vectors - {0x11DF78, 0x11E748, 0x11DBB8})
    for high in (1093, 4370, 8739, 0x7FFF, 0xC000):   # 0xC0000000 scales to -15: bits 2 and 3 clear, bit 7 set
        word = high << 16
        add('826D40 rearm random %X' % word, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(4)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0)), (sentry + 0x28, h16(5)),
                    (sentry + 0x36, h16(1))],
            queues={0x8282F0: [0], 0x122BB8: [word, word, word]}, run_set=RUN - {0x122BB8})
        add('826D40 reseed random %X' % word, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(4)), (sentry + 0x208, w32(0x22)), (sentry + 0x20C, w32(1)),
                    (sentry + 0x214, w32(1))],
            queues={0x122BB8: [word, word, word]}, run_set=RUN - {0x122BB8})
    for h28 in (3, 4):
        add('826D40 idle tick +0x28 %d' % h28, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(4)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(1)), (sentry + 0x28, h16(h28))],
            queues={0x8282F0: [0]})
    for state in (4, 1):
        add('826D40 count one s%d' % state, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(state)), (sentry + 0x208, w32(1))])
    # (k) round 5: the idle hand-off tests the whole word +0x204 against
    # zero, so a word with bit 31 set hands off to state 1 as any other
    # non-zero word does (a signed `> 0` would not)
    for flag in (0x80000000, 0xFFFFFFFF, 0x00000100, 0x00010000):
        add('826D40 idle hand-off +0x204 %X' % flag, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(4)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0)), (sentry + 0x28, h16(5)),
                    (sentry + 0x204, w32(flag))], queues={0x8282F0: [0]})
    # the re-arm test reads +0x208 again after 0x8282F0 (`+0x208 == 0`, a
    # whole word): the callee moves it to a positive or negative word
    for state in (4, 1):
        for moved in (1, 0x7FFFFFFF, 0x80000000):
            add('826D40 rearm +0x208 after 8282F0 %X s%d' % (moved, state), beat, 0x826D40, [sentry],
                base + [(sentry + 4, b8(state)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0)),
                        (sentry + 0x204, w32(npc if state == 1 else 0)), (sentry + 0x36, h16(1)),
                        (sentry + 0x28, h16(5)), (sentry + 0x2A, h16(5))],
                queues={0x8282F0: [Scribble(0, [(sentry + 0x208, w32(moved))])],
                        0x122BB8: [bits_set, bits_set, bits_set]}, run_set=RUN - {0x122BB8})
    # (l) round 6 (the round-5 review's sweep 3): the tracker's three clamp
    # tests with the compared word exactly on the constant, and a -0 phase
    # into the sine. Tracker template: direction x = 0 (inside the side
    # band), fabs 0 (no heading store), vector helpers stubbed.
    for tag, a74, b78, atan in (
            # A lower clamp `A+0x74 < -LIM` with A+0x74 = -LIM itself: no
            # clamp and no `+0x2A -= 4` (W35: `<=`)
            ('A+0x74 = -LIM', MLIM, fbits(-0.8), fbits(-0.8)),
            # B lower clamp: EE sub(0xBF8FB8D9, STEP) = -LIM exactly (W39)
            ('B+0x78 steps onto -LIM', fbits(0.25), 0xBF8FB8D9, MLIM),
            # B upper clamp `!(B+0x78 <= BMIN)`: EE add(0xBF09051E, STEP)
            # = BMIN (0xBF060A92) exactly (W40: `!(<)`)
            ('B+0x78 steps onto BMIN', fbits(0.25), 0xBF09051E, fbits(0.5))):
        add('826D40 track clamp boundary %s' % tag, beat, 0x826D40, [sentry],
            base + [(sentry + 4, b8(1)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(0)),
                    (sentry + 0x204, w32(npc)), (bone_a + 0xB0, f32b(-1.0)), (bone_a + 0xB8, f32b(0.0)),
                    (bone_a + 0x74, w32(a74)), (bone_b + 0x78, w32(b78)), (sentry + 0x2A, h16(5))],
            [(0x70003600, f32b(0.0)), (0x70003608, f32b(0.0)), (0x70003604, f32b(0.2))],
            queues={0x11E748: [fb(1.0)], 0x11DBB8: [atan], 0x11DF78: [fb(0.0)], 0x8282F0: [0]},
            run_set=RUN - vectors - {0x11E748, 0x11DBB8, 0x11DF78})
    # a phase of -0 reaches 0011E2A8 as 0x80000000: idle (phase -0 + step
    # -0, EE add gives -0) and state 2's wind (phase -0 - step +0, EE sub
    # gives -0); a host `+ 0.0f` on the argument would pass +0 (W45)
    add('826D40 idle phase -0 into the sine', beat, 0x826D40, [sentry],
        base + [(sentry + 4, b8(4)), (sentry + 0x208, w32(0)), (sentry + 0x200, w32(1)), (sentry + 0x28, h16(7)),
                (bone_a + 0x74, f32b(0.0)), (sentry + 0x1F4, f32b(0.01)),
                (sentry + 0x1FC, w32(0x80000000)), (sentry + 0x1F8, w32(0x80000000))])
    add('826D40 wind phase -0 into the sine', beat, 0x826D40, [sentry],
        base + [(sentry + 4, b8(2)), (sentry + 0x1FC, w32(0x80000000)), (sentry + 0x1F8, w32(0))],
        queues={0x1BA1C0: [1]})

JUMP_TABLES = {0x82360C: (0x82CB80, 7)}  # the shaft door's step table (runtime addresses)
# The only bytes the original loads that the translation does not: the door's
# step table, which its C switch encodes (load_captures asserts the table
# equals AREA01.BIN in every image). Replay.check_access leaves them out.
TABLE_BYTES = frozenset(a for table, count in JUMP_TABLES.values() for a in range(table, table + 4 * count))


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
    unmapped address and a fault latched before the call."""
    ram, spad = CAPTURES['a01_01_tunnel']
    npc = owners(ram, 0x825350)[0]
    problems = []

    def attempt(null=None, failing=None, unmapped=None, latched=False, state=1):
        ram_c = (C.c_uint8 * RAM_SIZE).from_buffer_copy(ram)
        spad_c = (C.c_uint8 * SPAD_SIZE).from_buffer_copy(spad)
        ram_c[npc + 4] = state
        ram_c[npc + 5] = 1
        ram_c[npc + 0xB] = 0
        ram_c[0x8107D9] = 0x80
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
                    values[-1][0] = 0
                return -1 if name == failing else 0
            fields[name] = hook_proto(args, result)(fn) if name != null else hook_proto(args, result)()
            keep.append(fields[name])
        fields['w_callback'] = CALLBACK_FN(lambda _c, fn, actor: calls.append('w_callback') or 0)
        keep.append(fields['w_callback'])
        hooks = Hooks(**fields)
        fault = Fault(0x1234, 7) if latched else Fault()
        before = C.string_at(base, RAM_SIZE)
        status = NATIVE.em_area01_ovl_00825350(C.byref(hooks), npc, C.byref(fault))
        return status, fault.address, fault.code, calls, before == C.string_at(base, RAM_SIZE)

    status, address, code, calls, _ = attempt(null='w_001BA580')
    if (status, address, code) != (-1, 0x1BA580, 1):
        problems.append(('NULL hook', status, hex(address), code, calls))
    if 'w_001C64F0' in calls or 'w_callback' in calls:
        problems.append(('hook called after a NULL hook', calls))
    status, address, code, calls, _ = attempt(failing='w_001C64F0')
    if (status, address, code) != (-1, 0x1C64F0, 2) or 'w_001C68C0' in calls or 'w_callback' in calls:
        problems.append(('failing hook', status, hex(address), code, calls))
    status, address, code, calls, _ = attempt(unmapped=npc + 0xD)
    if (status, address, code) != (-1, npc + 0xD, 5) or calls:
        problems.append(('unmapped address', status, hex(address), code, calls))
    status, address, code, calls, same = attempt(latched=True)
    if (status, address, code) != (-1, 0x1234, 7) or calls or not same:
        problems.append(('latched fault', status, hex(address), code, calls, same))
    if NATIVE.em_area01_ovl_00825350(None, npc, C.byref(Fault())) != -1:
        problems.append(('NULL hooks accepted',))
    # every entry: a fault latched on entry, a NULL hook table or a NULL
    # fault pointer returns -1 at once, with no call, no memory change and
    # (op09) no result written
    hooks_ok = []

    def refuse(*_):
        hooks_ok.append('called')
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
            del hooks_ok[:]
            where = C.byref(fault) if fault is not None else None
            if kind == 'op09':
                status = fn(table, npc, npc + 0x1F0, 0x82A020, C.byref(result), where)
            else:
                status = fn(table, npc, where)
            latched = fault is None or (fault.address, fault.code) == ((0x1234, 7) if table is not None else (0, 0))
            if status != -1 or hooks_ok or result.value != 0x5A5A5A5A or not latched:
                problems.append(('entry %06X refused' % entry, status, hooks_ok[:2], hex(result.value & MASK)))
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
    """The fail-stop contract of the header (em_area01_overlay.h), on one
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
    where each wrapper's own `a01_failed` test is observable. Returns
    (problems found, native runs made)."""
    index, targets = site
    label, beat, entry, native_args, patches, spad_patches, script, run_set = CASES[index]
    ram0, spad0 = CAPTURES[beat]
    ram, spad = patched(ram0, patches), patched(spad0, spad_patches)
    oracle = Oracle(ram, spad, entry, (native_args, {u32(ram, native_args[0] + 0x4C)}), copy_script(script),
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


QUICK_RANDOM = 0   # random cases sampled into the default run (round 6: none; EM_TEST_FULL=1 runs all 2,400)
# Random cases the default run keeps, by their number in the random sweep
# (0-based; the sweep is generated from its own fixed seed, so the numbers
# do not move when designed cases are added): each is the only case known
# to kill a mutant from the lane's sweeps (round 6, docs/AREA01_OVERLAY.md
# section 3). The rest of the random sweep runs only with EM_TEST_FULL=1.
PINNED_RANDOM = (
    14,    # '826D40 s4 0': rev Z36 (the idle sound test's 0x70003B68 & 0x3F as & 0x7F)
    24,    # '828850 1': sign O:785 (828850's +0x9A byte for 001B1190 read as s8)
    39,    # '828850 2': sign O:749 (828850's +0x9A byte for 001B11E0 read as s8)
    119,   # '826D40 s1 7': lit D:521 (the period test +0x200 >= 0xE as >= 0xD) and
           # lit D:542 (the tracker's +0x28 reseed rand_scaled(300) as 301)
)


def select_cases(elf):
    """The mode's case list: every capture and designed case, the pinned
    random cases and QUICK_RANDOM sampled ones (EM_TEST_FULL=1: all).
    Returns (cases, captured, designed, cases in the full run)."""
    cases, captured, targeted = case_list(elf)
    fixed = captured + targeted
    assert all(0 <= n < len(cases) - fixed for n in PINNED_RANDOM), 'pinned random case out of range'
    pinned = {fixed + n for n in PINNED_RANDOM}
    return reference_mode.select(cases, reference_mode.pick(len(cases), fixed + len(pinned) + QUICK_RANDOM), 0xA01,
                                 keep=lambda i, c: i < fixed or i in pinned), captured, targeted, len(cases)


def main():
    global NATIVE
    elf = read_elf()
    CAPTURES.update(load_captures(elf))
    NATIVE = build()
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
