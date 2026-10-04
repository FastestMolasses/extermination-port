#!/usr/bin/env python3
"""Execute the original AREA01 room-beat routines and compare em_area01_room.c;
re-run the existing port translations of the other room-beat rows over the
AREA01 room captures.

docs/AREA01_ROOM.md. The user's pinned ELF and the captured AREA01 route RAM
(../Extermination/build/s87/route_a01/<beat>/, end-of-beat images of the
room beats a01_s4_east_room, a01_s5_duct, a01_s6_bridge_blocked and
a01_s7_npc_third_talk) supply every instruction and every table; none are
embedded here.

A. The seven routines translated in em_area01_room.c (00188610, 00198D90,
   001BB400, 001BB7C0, 001BB7F0, 001D0D60, 00225A00). The oracle is FallEE
   (tools/test_player_fall_reference.py: COP1 and VU0 macro operations
   through ee_float_model.py, the measured model) running the ORIGINAL
   routine over a copy of a captured room beat; the native module runs over
   another copy of the same bytes. The design is lane SIDE's
   (docs/AREA01_SIDE.md section 5; lane SYS before it): every call leaving
   the translated set is caught on both sides and handled by one policy,
   `run` (the SDK leaves 00102948 / 001029C0 / 00102C58 / 001026A0 /
   001028B8, float_to_int 001281C0 and 00121A28 run as ORIGINAL code, on the
   native side in a second interpreter bound to the module's memory) or
   `stub` (0018C4B0, 0018C6A0, 001BA1F0: recorded, scripted v0, possibly a
   whole 64-bit image). Lockstep: at every call the callee, the stack
   pointer, the argument registers it is handed (64-bit images), the float
   argument registers and every RAM / scratchpad line either side stored to
   since the previous call are compared; again after the last store; then
   all 32 MiB, the scratchpad and the result. Both stacks start filled with
   0xA5 and the interpreter's registers with a pattern. Memory model: RAM is
   the 32 MiB at 0 only; the shared interpreter's mirror of
   0x02000000..0x3FFFFFFF is off here (RoomEE._where), so an access there
   stops the original as the module's UNMAPPED fault stops it and a pointer
   rewritten by a float store (a pointer + 0.2 = 0x3E4CCCCD) is compared.
   fx cases (the SIDE design, applied at every call): pass 1 records the
   data loads the translated routines make after each call; pass 2 changes
   exactly those fields when that call returns, identically on both sides
   ('after'), or changes the fields loaded BEFORE the call ('before'), so a
   value cached across a call, or a read deferred across one, differs.
   Branch coverage: both outcomes of every conditional branch of the seven
   routines are taken (asserted in every run).
   api_checks: the fail-stop contract (NULL context, NULL worker, worker
   failure, unmapped address, NULL output, the latch, clear_fault).
B. The existing port translations of the other thirteen room-beat rows,
   re-run over the AREA01 room captures (docs/AREA01_ROOM.md section 5 B).
   Default: a01_s5_duct for the closures and 002160B0, all four room
   captures for the rest; EM_TEST_FULL=1: all four everywhere.
   - The unit oracles of tools/test_player_closure_0e_18_reference.py
     (0016D130, 00179150, 001790B0) and tools/test_player_closure_10_12_19_
     reference.py (0016DE40, 0016EBA0, 00179010, 00179910; 001823E0 inside):
     each module's own run_case with its captured image and player record
     replaced by a room capture's (default 600 seeds each over a01_s5; full
     4,000 each over the four).
   - tools/test_player_major2_reference.py's main (001823E0) over each
     room capture (full mode only).
   - 0015FDF0 / 001AA4E0: per-case world comparisons (jump_items /
     b_jump_case), NOT a route replay: the original routine against the
     native one hooked at its address with the running-jump test's world
     workers (WorldJump; every worker runs the ORIGINAL callee), over each
     room capture; the final v0, all 32 MiB of RAM and the scratchpad must be
     identical (no per-callee-entry memory check). A player-stage route
     replay of the room beats was tried and dropped (the doc says why).
   - 00208AD0 / 002082B0: the original over each capture's RAM and arc
     records against em_status_draw.c / em_item_geometry.c given the data
     read from that capture.
   - 002160B0: tools/test_status_pages_reference.py's runner (every callee
     entry compared) with its HEALING cases moved onto the room images
     (default 8 on a01_s5; full all on a01_s5 and 40 on each other).
   EM_AREA01_ROOM_EXISTING_ROOT=<tree> takes those modules (tests and C) from
   another checkout (a HEAD export while another chain edits the working
   tree); default: this repository. EM_AREA01_ROOM_EXISTING=0 skips B.

EM_TEST_FULL=1 runs every case (and B over all four room captures);
EM_AREA01_ROOM_SOURCE=<file> tests another copy of the module source
(mutation runs); EM_AREA01_ROOM_ONLY=<prefix,...> runs only the cases whose
name starts with a prefix (B skipped); EM_AREA01_ROOM_TIMES=1 prints each
part's wall and CPU time. At most four worker processes (EM_TEST_JOBS
overrides).
"""
import ctypes as C
import importlib
import os
import random
import struct
import subprocess
import sys
import area01_reference_view as AV
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
os.environ.setdefault('EM_TEST_JOBS', '4')
import reference_mode as RM  # noqa: E402
import test_player_slide_reference as shared  # noqa: E402
from test_player_slide_reference import read_elf, sx32  # noqa: E402
from test_player_fall_reference import FallEE  # noqa: E402

MASK, MASK64 = 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF
DECOMP = ROOT.parent / 'Extermination'
ROUTE = DECOMP / 'build/s87/route_a01'
OUT = ROOT / 'build' / 'area01' / 'room'
STACK_BASE, STACK_SIZE = 0x7F000000, 0x100000
STACK_LO, STACK_HI = STACK_BASE, STACK_BASE + STACK_SIZE
SP = shared.STACK_TOP
SCRATCH = 0x01E00000            # zero in every captured beat; test records live here
ROOM_BEATS = ('a01_s4_east_room', 'a01_s5_duct', 'a01_s6_bridge_blocked', 'a01_s7_npc_third_talk')
PLAYER = 0x008102B0
CAMERA = 0x008101E0             # the camera block 0018BC20 hands 00198D90 (docs/CAMERA_LEFTOVERS.md)
DOORS = (0x7ACBC0, 0x7ACEB0)    # the 001BB860 nodes (docs/AREA01_MATH.md); each is its own skeleton
SAMPLER = 0x7DCD40              # node 0x7A93F0's +0x90 record (001C02E0 hands it to 001D0D60)
D275B40 = 0x275B40

# Translated routines (address -> size in bytes, from the census).
FUNCS = {0x188610: 0x1C, 0x198D90: 0x174, 0x1BB400: 0x11C, 0x1BB7C0: 0x28, 0x1BB7F0: 0x6C,
         0x1D0D60: 0x1BC, 0x225A00: 0x14}
FUNC_RANGES = tuple((a, a + n) for a, n in FUNCS.items())

# Callee policy: address -> (kind, integer argument registers compared (a0..),
# float argument registers compared (f12..)). The counts are the registers
# the original sets for each call.
R, S = 'run', 'stub'
CALLEES = {
    0x102948: (R, 2, 0), 0x1029C0: (R, 1, 0), 0x102C58: (R, 3, 0), 0x1026A0: (R, 3, 0),
    0x1028B8: (R, 3, 0), 0x1281C0: (R, 0, 1), 0x121A28: (R, 3, 0),
    0x18C4B0: (S, 1, 2), 0x18C6A0: (S, 2, 1), 0x1BA1F0: (S, 1, 0),
}

REG_FILL, FREG_FILL = 0x01F0A000, 0x449A5000
STACK_FILL = bytes([0xA5]) * STACK_SIZE
LINE = 64
RAM_LINES = 0x2000000 // LINE
CODE_END = 0x230000


def in_funcs(pc):
    for lo, hi in FUNC_RANGES:
        if lo <= pc < hi:
            return True
    return False


def line_of(a):
    a &= MASK
    if 0x70000000 <= a < 0x70004000:
        return RAM_LINES + (a - 0x70000000) // LINE
    if a < 0x40000000:
        return (a & 0x1FFFFFF) // LINE
    return None


def line_address(lid):
    return lid * LINE if lid < RAM_LINES else 0x70000000 + (lid - RAM_LINES) * LINE


def line_bytes(mem, spad, lid):
    if lid < RAM_LINES:
        return bytes(mem[lid * LINE:(lid + 1) * LINE])
    o = (lid - RAM_LINES) * LINE
    return bytes(spad[o:o + LINE])


def img(v):
    """A scripted v0 as a 64-bit register image: a 32-bit value is
    sign-extended (a callee returning int), a larger one is kept whole."""
    if -(1 << 31) <= v < (1 << 32):
        return sx32(v) & MASK64
    return v & MASK64


# ======================================================================
# Interpreters
# ======================================================================

BRANCH_PCS = set()


class RoomEE(FallEE):
    """FallEE recording branch outcomes inside the translated routines, the
    RAM / scratchpad lines it stores to (`dirty`; `pre` keeps each line's
    bytes before its first store), and for the fx cases the data loads the
    translated routines make (`window` = calls made so far)."""

    def __init__(self, elf, ram=None, spad=None):
        super().__init__(elf, ram, spad)
        self.outcomes = set()
        self.dirty = set()
        self.pre = None
        self._pc = None
        self.window = 0
        self.loads = None

    def _where(self, address):
        """The module's memory model: RAM is the 32 MiB at 0 only (the
        shared interpreter mirrors every address below 0x40000000 onto it;
        the module maps no mirror and faults UNMAPPED), so an access in
        0x02000000..0x3FFFFFFF stops here as it does there. A pointer
        rewritten by a float store (1BB400 re-read cases) lands there."""
        a = address & MASK
        if 0x02000000 <= a < 0x40000000:
            raise AssertionError(('address', hex(a)))
        return super()._where(address)

    def load(self, address, size=4):
        v = super().load(address, size)
        pc = self._pc
        if self.loads is not None and pc is not None and in_funcs(pc):
            a = address & MASK
            if not (STACK_LO <= a < STACK_HI) and not (0x100000 <= a < CODE_END):
                self.loads.setdefault(self.window, {}).setdefault((a, size), (v, pc))
        return v

    def execute(self, word, pc):
        self._pc = pc
        try:
            super().execute(word, pc)
        finally:
            self._pc = None

    def mmi(self, word, pc):
        """Adds PCPYH (the C runtime memset 00121A28 broadcasts with it; the
        AREA01 UI lane's extension): each 64-bit half's low halfword is copied
        to its four halfwords."""
        if (word & 63) == 0x29 and (word >> 6 & 31) == 0x1B:
            rt, rd = word >> 16 & 31, word >> 11 & 31
            lo, hi = self.r[rt] & 0xFFFF, self.rh[rt] & 0xFFFF
            if rd:
                self.r[rd] = lo * 0x0001000100010001
                self.rh[rd] = hi * 0x0001000100010001
            return
        return super().mmi(word, pc)

    def branch(self, word, pc):
        b = super().branch(word, pc)
        if b is not None and pc in BRANCH_PCS:
            self.outcomes.add((pc, b[0]))
        return b

    def save(self, address, value, size=4):
        for a in (address, address + size - 1):
            lid = line_of(a)
            if lid is not None:
                if self.pre is not None and lid not in self.pre:
                    self.pre[lid] = line_bytes(self.mem, self.spad, lid)
                self.dirty.add(lid)
        super().save(address, value, size)

    def take(self):
        out = {lid: line_bytes(self.mem, self.spad, lid) for lid in self.dirty}
        self.dirty = set()
        return out


def run_nested(ee, fn):
    """The original callee `fn` (and all it calls) with the argument
    registers as they are; every register except v0 / v1 / f0 restored."""
    saved = (list(ee.r), list(ee.rh), ee.hi, ee.lo, list(ee.f), ee.acc,
             ee.cond, [list(v) for v in ee.vf], list(ee.vacc), ee.q)
    hooks, ee.hooks = ee.hooks, {}
    ee.r[29] = (ee.r[29] - 0x400) & ~15
    ee.r[31] = shared.RETURN
    try:
        ee.run(fn)
    finally:
        ee.hooks = hooks
    v0, v1, f0 = ee.r[2], ee.r[3], ee.f[0]
    (ee.r, ee.rh, ee.hi, ee.lo, ee.f, ee.acc, ee.cond, ee.vf, ee.vacc, ee.q) = saved
    ee.r[2], ee.r[3], ee.f[0] = v0, v1, f0
    return v0, f0


class Script:
    """Scripted stub results: fn -> list of v0 values (img() applies)."""

    def __init__(self, spec):
        self.queues = {fn: list(values) for fn, values in (spec or {}).items()}

    def take(self, fn):
        q = self.queues.get(fn)
        return img(q.pop(0)) if q else 0


def log_entry(ee, fn, pol, regs, fregs, sp):
    kind, na, nf = pol
    args = tuple(regs[i] & MASK64 for i in range(na))
    floats = tuple(fregs[i] & MASK for i in range(nf))
    return (hex(fn), hex(sp & MASK), args, floats)


# ======================================================================
# Native side
# ======================================================================

P, U8, U32, I32, U64 = C.POINTER, C.c_uint8, C.c_uint32, C.c_int32, C.c_uint64


class Region(C.Structure):
    _fields_ = [('base', U32), ('size', U32), ('bytes', P(U8))]


class Call(C.Structure):
    _fields_ = [('fn', U32), ('sp', U32), ('a', U64 * 8), ('f', U32 * 4), ('na', U32), ('nf', U32),
                ('v0', U64), ('f0', U32)]


WORKER = C.CFUNCTYPE(C.c_int, C.c_void_p, P(Call))


class Room(C.Structure):
    _fields_ = [('regions', P(Region)), ('region_count', C.c_uint), ('call', WORKER), ('ctx', C.c_void_p),
                ('sp', U32), ('fault', I32), ('fault_function', U32), ('fault_address', U32),
                ('view', C.c_void_p)]


NATIVE = None
ELF = None
IMAGES = {}
TRACE = {}

TRACE_C = r"""/* Store trace for the native module (test build only). */
#include <stdint.h>
#define RAM_LINES (0x2000000u / 64u)
#define LINES (RAM_LINES + 0x4000u / 64u)
static uint8_t dirty[LINES];
uint32_t a01room_trace_list[LINES];
uint32_t a01room_trace_count;
void a01room_trace_store(uint32_t address, unsigned size);
void a01room_trace_reset(void);
static void mark(uint32_t a)
{
    uint32_t id;
    if (a < 0x2000000u)
        id = a / 64u;
    else if (a - 0x70000000u < 0x4000u)
        id = RAM_LINES + (a - 0x70000000u) / 64u;
    else
        return;
    if (!dirty[id]) {
        dirty[id] = 1;
        a01room_trace_list[a01room_trace_count++] = id;
    }
}
void a01room_trace_store(uint32_t address, unsigned size)
{
    mark(address);
    mark(address + size - 1u);
}
void a01room_trace_reset(void)
{
    uint32_t i;
    for (i = 0; i < a01room_trace_count; i++)
        dirty[a01room_trace_list[i]] = 0;
    a01room_trace_count = 0;
}
"""


def build_native():
    """The module with its store trace. EM_AREA01_ROOM_SOURCE names another
    copy of the source (a mutant), built into its own library."""
    OUT.mkdir(parents=True, exist_ok=True)
    trace = OUT / 'store_trace.c'
    if not trace.exists() or trace.read_text() != TRACE_C:
        trace.write_text(TRACE_C)
    ext = 'dylib' if sys.platform == 'darwin' else 'so'
    source = os.environ.get('EM_AREA01_ROOM_SOURCE', '')
    if source:
        source = Path(source).resolve()
        lib = OUT / f'area01_room_{source.stem}.{ext}'
    else:
        source = ROOT / 'src/game/em_area01_room.c'
        lib = OUT / f'area01_room.{ext}'
    deps = [source, ROOT / 'src/game/em_area01_room.h', ROOT / 'src/game/em_ee_float.h', trace, Path(__file__)]
    if not lib.exists() or max(p.stat().st_mtime for p in deps) > lib.stat().st_mtime:
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                        '-ffp-contract=off', '-shared', '-fPIC', '-Isrc', '-Isrc/game',
                        '-DEM_AREA01_ROOM_STORE_TRACE=a01room_trace_store', str(source), str(trace),
                        '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    TRACE['count'] = U32.in_dll(native, 'a01room_trace_count')
    TRACE['list'] = (U32 * (RAM_LINES + 0x4000 // LINE)).in_dll(native, 'a01room_trace_list')
    TRACE['reset'] = native.a01room_trace_reset
    RP = P(Room)
    native.em_area01_room_00188610.argtypes = [RP, U32, P(I32)]
    native.em_area01_room_00198D90.argtypes = [RP, U32, U32]
    native.em_area01_room_001BB400.argtypes = [RP, U32, P(I32)]
    native.em_area01_room_001BB7C0.argtypes = [RP, U32, P(I32)]
    native.em_area01_room_001BB7F0.argtypes = [RP, U32, P(I32)]
    native.em_area01_room_001D0D60.argtypes = [RP, U32, U32, P(I32)]
    native.em_area01_room_00225A00.argtypes = [RP]
    native.em_area01_room_clear_fault.argtypes = [RP]
    native.em_area01_room_clear_fault.restype = None
    return native


def native_lines():
    n = TRACE['count'].value
    if not n:
        return ()
    out = TRACE['list'][:n]
    TRACE['reset']()
    return out


class Lockstep:
    """The oracle's side of the per-call memory check (as lanes SYS / SIDE)."""

    def __init__(self, entries, pre, mem, spad):
        self.entries, self.pre = entries, pre
        self.mem, self.spad = mem, spad
        self.shadow = {}

    def pristine(self, lid):
        got = self.pre.get(lid)
        return got if got is not None else line_bytes(self.mem, self.spad, lid)

    def check(self, where, delta, native_dirty, nmem, nspad):
        self.shadow.update(delta)
        for lid in sorted(set(native_dirty) | set(delta)):
            want = self.shadow.get(lid)
            if want is None:
                want = self.pristine(lid)
            got = line_bytes(nmem, nspad, lid)
            if got != want:
                k = next(i for i in range(LINE) if got[i] != want[i])
                raise AssertionError(('memory differs', where, 'first address', hex(line_address(lid) + k),
                                      'native', got[k:k + 8].hex(), 'original', want[k:k + 8].hex()))


class NativeRun:
    """One native call over copies of `ram` / `spad` and a private stack."""

    def __init__(self, ram, spad, script):
        self.ram = (U8 * len(ram)).from_buffer_copy(ram)
        self.spad = (U8 * len(spad)).from_buffer_copy(spad)
        self.stack = (U8 * STACK_SIZE).from_buffer_copy(STACK_FILL)
        self.regions = (Region * 2)(Region(0, len(ram), C.cast(self.ram, P(U8))),
                                    Region(0x70000000, len(spad), C.cast(self.spad, P(U8))))
        self.ee = RoomEE(ELF, b'', b'')
        self.ee.mem = memoryview(self.ram).cast('B')
        self.ee.spad = memoryview(self.spad).cast('B')
        self.ee.stack = memoryview(self.stack).cast('B')
        self.script, self.log, self.error = script, [], None
        self.lock = None
        self.fx = {}
        self.calls = 0
        self.worker = WORKER(self._call)
        self.room = Room(self.regions, 2, self.worker, None, SP, 0, 0, 0)
        if AV.ENABLED:
            self.view = AV.CanonicalView(self.regions)
            self.view.install(self.room)

    def dirty(self):
        out = set(native_lines())
        out |= self.ee.dirty
        self.ee.dirty = set()
        return out

    def _call(self, _, pc):
        try:
            c = pc.contents
            assert c.fn in CALLEES, ('call without a policy', hex(c.fn))
            pol = CALLEES[c.fn]
            kind, na, nf = pol
            assert c.na >= na and c.nf >= nf and c.na <= 8 and c.nf <= 4, ('native register counts', hex(c.fn),
                                                                           c.na, c.nf)
            k = len(self.log)
            regs = [c.a[i] for i in range(8)]
            entry = log_entry(self.ee, c.fn, pol, regs, list(c.f), c.sp)
            self.log.append(entry)
            if self.lock is not None:
                assert k < len(self.lock.entries), ('native makes more calls than the original', k, entry)
                want, delta, oregs, ofregs = self.lock.entries[k]
                self.lock.check(('call', k, hex(c.fn)), delta, self.dirty(), self.ee.mem, self.ee.spad)
                assert entry == want, ('callee entry differs at call', k, 'native', entry, 'original', want)
                got = (tuple(c.a[i] & MASK64 for i in range(c.na)), tuple(c.f[i] & MASK for i in range(c.nf)))
                assert got == (oregs[:c.na], ofregs[:c.nf]), ('registers the native says it sets differ at call',
                                                             k, hex(c.fn), got, (oregs[:c.na], ofregs[:c.nf]))
            if kind == R:
                ee = self.ee
                for i in range(8):
                    ee.r[4 + i] = c.a[i] & MASK64
                for i in range(4):
                    ee.f[12 + i] = c.f[i] & MASK
                ee.r[29] = c.sp
                v0, f0 = run_nested(ee, c.fn)
            else:
                v0, f0 = self.script.take(c.fn), 0
            for address, size, value in self.fx.get(self.calls, ()):
                self.ee.save(address, value, size)
            self.calls += 1
            c.v0 = v0 & MASK64
            c.f0 = f0 & MASK
            return 0
        except Exception as e:  # noqa: BLE001 - reported after the call returns
            if self.error is None:
                self.error = e
            return -1


# ======================================================================
# Oracle side
# ======================================================================

def oracle_run(case, ram, spad, on_call=None, record=False):
    """The original over `ram` / `spad`. Returns (ee, log, v0, entries,
    final delta); entries[k] = (log entry, {line: bytes} stored since the
    previous call, a0..t3 images, f12..f15 bits) at the k-th call."""
    ee = RoomEE(ELF, b'', b'')
    ee.mem, ee.spad = ram, spad
    ee.stack[:] = STACK_FILL
    ee.pre = {}
    if record:
        ee.loads = {}
    script, log, entries = Script(case['script']), [], []
    calls = [0]

    def make(fn):
        pol = CALLEES[fn]

        def hook(e):
            regs = [e.r[4 + i] for i in range(8)]
            entry = log_entry(e, fn, pol, regs, [e.f[12 + i] for i in range(4)], e.r[29])
            log.append(entry)
            entries.append((entry, e.take(), tuple(r & MASK64 for r in regs),
                            tuple(e.f[12 + i] & MASK for i in range(4))))
            if pol[0] == R:
                run_nested(e, fn)
            else:
                e.r[2] = script.take(fn)
                e.f[0] = 0
            if on_call is not None:
                on_call(e, calls[0])
            calls[0] += 1
            e.window = calls[0]
        return hook
    ee.hooks = {fn: make(fn) for fn in CALLEES}
    for i in range(1, 32):
        if i not in (28, 29, 31):
            ee.r[i] = REG_FILL + i * 0x40
    for i in range(32):
        ee.f[i] = FREG_FILL + i
    for i, v in enumerate(case['args']):
        ee.r[4 + i] = sx32(v) & MASK64
    for i, v in enumerate(case.get('fargs', ())):
        ee.f[12 + i] = v & MASK
    ee.r[29] = SP
    ee.r[31] = shared.RETURN
    crash = None
    try:
        ee.run(case['fn'])
    except AssertionError as err:
        what = err.args[0] if err.args else None
        if not (case.get('crash') and isinstance(what, tuple) and len(what) == 2 and what[0] == 'address'):
            raise
        crash = int(what[1], 16)
    return ee, log, ee.r[2] & MASK, entries, ee.take(), crash


# ======================================================================
# Call side effects (fx)
# ======================================================================

def fx_plan(case, before):
    """{call index k: [(address, size)]}: the fields the translated routines
    load after the k-th call returns (before the next), or with `before` the
    fields loaded since the previous call, changed when the k-th returns."""
    ram, spad = prepared(case)
    ee, *_ = oracle_run(case, ram, spad, record=True)
    plan = {}
    for w, loads in (ee.loads or {}).items():
        items = []
        for (a, size), (v, _pc) in sorted(loads.items()):
            if size == 4 and 0x100000 <= v < CODE_END:
                continue            # code words (none of these routines calls through data)
            items.append((a, size))
        if not items:
            continue
        if before:
            plan[w] = items          # loaded before call number w: change them when it returns
        elif w > 0:
            plan[w - 1] = items
    return plan


def is_pointer(v):
    return 0x100000 <= v < 0x2000000 or 0x70000000 <= v < 0x70004000


class Perturb:
    """When the k-th call returns, change the planned fields (identically on
    both sides; the bytes written are logged for the native run). 'after':
    bytes and halfwords flip bit 0, words flip bit 4. 'before': the n-th
    change of a field flips bits no earlier change flipped, in both halves."""

    def __init__(self, plan, before):
        self.plan, self.before, self.log, self.flips = plan, before, {}, {}

    def __call__(self, e, k):
        out = []
        for a, size in self.plan.get(k, ()):
            cur = e.load(a, size)
            if not self.before:
                new = cur ^ (1 if size < 4 else 0x10)
            else:
                n = self.flips.get((a, size), 0)
                self.flips[(a, size)] = n + 1
                if size == 1:
                    mask = 1 << n % 8
                elif size == 2:
                    mask = (1 << n % 8) | (1 << (8 + n % 8))
                elif is_pointer(cur):
                    mask = 1 << (4 + n % 20)
                else:
                    mask = (1 << (4 + n % 12)) | (1 << (16 + n % 12))
                new = cur ^ mask
            e.save(a, new, size)
            out.append((a, size, new))
        if out:
            self.log[k] = out


# ======================================================================
# One case
# ======================================================================

RETURNS = {0x188610, 0x1BB400, 0x1BB7C0, 0x1BB7F0, 0x1D0D60}


def native_call(fn, roomp, args, fargs):
    out = I32(0)
    N = NATIVE
    a = [x & MASK for x in args] + [0] * 3
    f = list(fargs) + [0] * 2
    if fn == 0x188610:
        return N.em_area01_room_00188610(roomp, a[0], C.byref(out)), out.value & MASK
    if fn == 0x198D90:
        return N.em_area01_room_00198D90(roomp, a[0], a[1]), None
    if fn == 0x1BB400:
        return N.em_area01_room_001BB400(roomp, a[0], C.byref(out)), out.value & MASK
    if fn == 0x1BB7C0:
        return N.em_area01_room_001BB7C0(roomp, a[0], C.byref(out)), out.value & MASK
    if fn == 0x1BB7F0:
        return N.em_area01_room_001BB7F0(roomp, a[0], C.byref(out)), out.value & MASK
    if fn == 0x1D0D60:
        return N.em_area01_room_001D0D60(roomp, a[0], f[0] & MASK, C.byref(out)), out.value & MASK
    if fn == 0x225A00:
        return N.em_area01_room_00225A00(roomp), None
    raise AssertionError(('no native entry', hex(fn)))


def image(beat):
    if beat not in IMAGES:
        IMAGES[beat] = ((ROUTE / beat / 'eeMemory.bin').read_bytes(), (ROUTE / beat / 'scratchpad.bin').read_bytes())
    return IMAGES[beat]


def prepared(case):
    ram, spad = image(case['beat'])
    ram, spad = bytearray(ram), bytearray(spad)
    for address, data in case.get('writes', ()):
        if 0x70000000 <= address < 0x70004000:
            spad[address - 0x70000000:address - 0x70000000 + len(data)] = data
        else:
            ram[address:address + len(data)] = data
    return ram, spad


def run_case(case):
    """Returns (branch outcomes, calls, fx bytes written). Raises on any
    difference."""
    fx = None
    if case.get('fx'):
        before = case['fx'] == 'before'
        fx = Perturb(fx_plan(case, before), before)
    ram, spad = prepared(case)
    nat = NativeRun(ram, spad, Script(case['script']))
    native_lines()
    where = (case['name'],)
    try:
        ee, olog, ov0, entries, last, crash = oracle_run(case, ram, spad, on_call=fx)
    except AssertionError as e:
        raise AssertionError((where, 'the original run stopped', e.args)) from e
    if fx is not None:
        nat.fx = fx.log
    nat.lock = Lockstep(entries, ee.pre, ee.mem, ee.spad)
    rc, nv0 = native_call(case['fn'], C.byref(nat.room), case['args'], case.get('fargs', ()))
    if nat.error is not None:
        raise AssertionError((where, 'native worker error', repr(nat.error)))
    if case.get('crash'):
        assert crash is not None, (where, 'the original did not stop at an unmapped address')
        assert (rc, nat.room.fault, nat.room.fault_function, nat.room.fault_address) == \
            (-1, 3, case['fn'], crash), (where, 'native stop', rc, nat.room.fault, hex(nat.room.fault_function),
                                         hex(nat.room.fault_address), 'original stopped at', hex(crash))
    else:
        assert rc == 0, (where, 'native faulted', rc, nat.room.fault, hex(nat.room.fault_function),
                         hex(nat.room.fault_address))
    assert len(nat.log) == len(entries), (where, 'native makes fewer calls than the original', len(nat.log),
                                          len(entries))
    try:
        nat.lock.check(('after the last store',), last, nat.dirty(), nat.ee.mem, nat.ee.spad)
    except AssertionError as e:
        raise AssertionError((where,) + e.args) from e
    if case['fn'] in RETURNS and not case.get('crash'):
        assert nv0 == ov0, (where, 'result', hex(nv0), hex(ov0))
    assert olog == nat.log, (where, 'call log differs', olog, nat.log)
    if bytes(nat.ram) != bytes(ee.mem):
        a, b = bytes(nat.ram), bytes(ee.mem)
        k = next(i for i in range(len(a)) if a[i] != b[i])
        raise AssertionError((where, 'RAM differs at', hex(k)))
    assert bytes(nat.spad) == bytes(ee.spad), (where, 'scratchpad differs')
    return ee.outcomes, len(olog), sum(len(v) for v in fx.log.values()) if fx else 0


# ======================================================================
# Cases
# ======================================================================

def W8(a, v): return (a, struct.pack('<B', v & 0xFF))
def W16(a, v): return (a, struct.pack('<H', v & 0xFFFF))
def W32(a, v): return (a, struct.pack('<I', v & MASK))
def FB(v): return struct.unpack('<I', struct.pack('<f', v))[0]


def case(name, fn, beat, args=(), script=None, writes=(), fargs=()):
    return dict(name=name, fn=fn, beat=beat, args=tuple(args), script=script or {}, writes=tuple(writes),
                fargs=tuple(fargs))


def u32(ram, a):
    return struct.unpack_from('<I', ram, a)[0]


def short(b):
    return b[4:6]


def cases_188610(rng):
    out = []
    for b in ROOM_BEATS:
        out.append(case('188610 captured %s' % b, 0x188610, b, (PLAYER,)))
    beat = 'a01_s5_duct'
    for v in range(256):
        tab = rng.choice(((None, None), (0x8000, 0x7FFF), (0xFFFF, 0x0001), (0x1234, 0xFEDC)))
        w = [W8(PLAYER + 0x235, v)]
        if tab[0] is not None:
            w += [W16(0x2754D8, tab[0]), W16(0x2754DA, tab[1])]
        out.append(case('188610 t byte %02x tab %s' % (v, 'c' if tab[0] is None else '%04x' % tab[0]), 0x188610,
                        beat, (PLAYER,), writes=w))
    # other records as a0 (the byte at +0x235 of a door node and a scratch record)
    for rec in DOORS + (SCRATCH + 0x100,):
        for v in (0, 1, 0xFE, 0xFF):
            out.append(case('188610 t rec %x byte %02x' % (rec, v), 0x188610, 'a01_s4_east_room', (rec,),
                            writes=[W8(rec + 0x235, v), W16(0x2754D8, 0x8001), W16(0x2754DA, 0x7FFE)]))
    # edge: the neighbours of the table differ (a wider or shifted read shows)
    out.append(case('188610 edge neighbours', 0x188610, beat, (PLAYER,),
                    writes=[W8(PLAYER + 0x235, 3), W16(0x2754D6, 0x1111), W16(0x2754D8, 0x2222),
                            W16(0x2754DA, 0x9333), W16(0x2754DC, 0x4444), W8(PLAYER + 0x234, 0xFF),
                            W8(PLAYER + 0x236, 0xFF)]))
    return out


def cases_198D90(rng):
    out = []
    for b in ROOM_BEATS:
        for st in (None, 0, 1):
            w = [] if st is None else [W8(CAMERA + 1, st)]
            out.append(case('198D90 captured %s st %s' % (b, 'c' if st is None else st), 0x198D90, b,
                            (CAMERA, PLAYER), {0x18C4B0: [rng.getrandbits(32)], 0x18C6A0: [rng.getrandbits(32)]}, w))
    beat = 'a01_s5_duct'
    # the added float: the first call 00102948(a0+0x10, a1+0xA0) copies the
    # player's +0xA0..+0xAF over a0+0x10..+0x1F, so the edge values go in the
    # player's +0xA4 (a value at a0+0x14 never reaches the add). 0x3F800003,
    # 0x3F800007 and 0x40A00003 are sums that round differently toward zero
    # (the EE) and to nearest (IEEE).
    ys = (None, 0, 0x80000000, FB(-3.0), 0x7F7FFFFF, 0xFF7FFFFF, 0x00000001, FB(1e7), 0x7FC00000,
          0x3F800003, 0x3F800007, 0x40A00003)
    for st in (0, 1, 2, 0xFF):
        for mode in (0x12, 0x11, 0, 0x112, 0x12000000):
            for y in ys:
                if st != 1 and mode != 0x12:
                    continue
                w = [W8(CAMERA + 1, st), W32(PLAYER + 0x230, mode), W8(CAMERA + 6, 0x5A), W8(CAMERA + 2, 0xA5)]
                # the neighbours of the byte stores +1 / +2 / +6 non-zero (a wider store shows)
                w += [W8(CAMERA + 3, 0x3C), W8(CAMERA + 4, 0x4C), W8(CAMERA + 5, 0x5C), W8(CAMERA + 7, 0x7C),
                      W8(CAMERA + 8, 0x8C), W8(CAMERA + 9, 0x9C)] if y is None else []
                if y is not None:
                    w.append(W32(PLAYER + 0xA4, y))
                out.append(case('198D90 t st %x mode %x y %s' % (st, mode, 'c' if y is None else '%x' % y),
                                0x198D90, beat, (CAMERA, PLAYER), {}, w))
    # another record pair: a scratch record and a door node as the actor
    for st in (0, 1):
        w = [W8(SCRATCH + 0x201, st)] + [W32(SCRATCH + 0x200 + 4 * i, rng.getrandbits(32) & 0x3FFFFFFF)
                                         for i in (4, 5, 6, 7, 9)]
        out.append(case('198D90 t scratch st %d' % st, 0x198D90, beat, (SCRATCH + 0x200, DOORS[0]), {}, w))
    return out


def door_writes(node, kind=None, flag=None, panels=None):
    w = [W32(D275B40, node + 0x110)]
    if kind is not None:
        w.append(W8(node + 3, kind))
    if flag is not None:
        w.append(W8(0x8106B8, flag))
    if panels:
        ram, _ = image('a01_s4_east_room')
        for i, v in enumerate(panels):
            if v is not None:
                w.append(W32(u32(ram, node + 0x110 + 4 * i) + 0x7C, v))
    return w


KINDS = (None, 8, 0x16, 0x3D, 0x3E, 0x17, 0x09, 0x00, 0xFF, 0x3C, 0x3F, 0x15)


def cases_1BB400(rng):
    out = []
    for b in ROOM_BEATS:
        for node in DOORS:
            out.append(case('1BB400 captured %s %x' % (b, node), 0x1BB400, b, (node,), writes=door_writes(node)))
    beat = 'a01_s4_east_room'
    vals = (FB(-8.8), FB(-8.8) + 1, FB(-8.8) - 1, FB(-8.9), FB(-12.8), FB(-12.8) + 1, FB(-12.8) - 1, FB(-12.9),
            0, 0x80000000, FB(0.2), FB(-9.0), FB(-13.0), 0x7F7FFFFF, 0xFF7FFFFF, 0x7FC00000, 0xFFFFFFFF, 1)
    for kind in KINDS:
        for v in vals:
            p = (v, rng.choice(vals), rng.choice(vals)) if kind in (8, 0x16) else (rng.choice(vals), v, rng.choice(vals))
            out.append(case('1BB400 t kind %s v %x' % ('c' if kind is None else '%x' % kind, v), 0x1BB400, beat,
                            (DOORS[0],), writes=door_writes(DOORS[0], kind, None, p)))
    # edge (crash cases): a panel store that rewrites the pointer array or
    # D_00275B40 itself; the original reads them again, so its next access
    # goes through the new (float) word, which is no address: both sides must
    # stop at that same address with the same bytes written
    arr, b0, b1, b2 = SCRATCH + 0x400, SCRATCH + 0x500, SCRATCH + 0x600, SCRATCH + 0x700
    node = SCRATCH + 0x800
    for kind, a0w, a1w, name in ((9, b0, arr + 8 - 0x7C, 'array word 2 (panel 1 store)'),
                                 (0x3D, b0, D275B40 - 0x7C, 'D_00275B40 (panel 1 store)'),
                                 (8, D275B40 - 0x7C, b1, 'D_00275B40 (panel 0 store)')):
        w = [W32(D275B40, arr), W32(arr, a0w), W32(arr + 4, a1w), W32(arr + 8, b2),
             W32(b0 + 0x7C, FB(-8.85)), W32(b1 + 0x7C, FB(-8.85)), W32(b2 + 0x7C, FB(-8.85)), W8(node + 3, kind)]
        c = case('1BB400 edge crash %s' % name, 0x1BB400, beat, (node,), writes=w)
        c['crash'] = True
        out.append(c)
    # edge (crash cases): panel 2's store rewrites panel 1's pointer (array
    # word 1) or D_00275B40; the original reads the array pointer and panel
    # 1's pointer again for the final compare, so it goes through the new
    # float word (a pointer + 0.2 = 0x3E4CCCCD, no address). A translation
    # that kept panel 1's address from before panel 2's store would not stop.
    for kind, a2w, name in ((9, arr + 4 - 0x7C, 'array word 1 (panel 2 store)'),
                            (0x3D, arr + 4 - 0x7C, 'array word 1 (panel 2 store) kind 3d'),
                            (9, D275B40 - 0x7C, 'D_00275B40 (panel 2 store)')):
        w = [W32(D275B40, arr), W32(arr, b0), W32(arr + 4, b1), W32(arr + 8, a2w),
             W32(b0 + 0x7C, FB(-8.85)), W32(b1 + 0x7C, FB(-8.85)), W8(node + 3, kind)]
        c = case('1BB400 edge crash %s' % name, 0x1BB400, beat, (node,), writes=w)
        c['crash'] = True
        out.append(c)
    # edge: panel 2's float overlaps the kind byte (the kind is read again
    # after panel 2 is written); panel 1 lands between -13.0 and -9.0
    for kind in (0x3D, 0x3E, 0x3C):
        w = [W32(D275B40, arr), W32(arr, b0), W32(arr + 4, b1), W32(arr + 8, node + 3 - 0x7C),
             W32(b1 + 0x7C, FB(-9.5)), W32(node, kind << 24), W32(node + 4, 0)]
        out.append(case('1BB400 edge kind rewrite %x' % kind, 0x1BB400, beat, (node,), writes=w))
    return out


def cases_1BB7C0(rng):
    out = []
    for b in ROOM_BEATS:
        for node in DOORS:
            out.append(case('1BB7C0 captured %s %x' % (b, node), 0x1BB7C0, b, (node,), {0x1BA1F0: [1]}))
    for v in (0, 1, 3, 2, -1, 0x80000000, 0x100000000, 0xFFFFFFFF00000000, 0x7FFFFFFF):
        for rec in DOORS + (PLAYER, SCRATCH + 0x40):
            out.append(case('1BB7C0 t v0 %x rec %x' % (v & MASK64, rec), 0x1BB7C0, 'a01_s4_east_room', (rec,),
                            {0x1BA1F0: [v]}))
    return out


def cases_1BB7F0(rng):
    out = []
    for b in ROOM_BEATS:
        for node in DOORS:
            out.append(case('1BB7F0 captured %s %x' % (b, node), 0x1BB7F0, b, (node,),
                            writes=door_writes(node, None, None, (FB(-3.0), FB(-4.0), FB(5.0)))))
    beat = 'a01_s4_east_room'
    for kind in KINDS:
        for flag in (0, 1, 0x80, 0xFF):
            w = door_writes(DOORS[1], kind, flag, (FB(-9.2), FB(-9.2), FB(9.2)))
            w += [W8(DOORS[1] + 0xB, 0x5A), W8(DOORS[1] + 0xA, 0x11), W8(DOORS[1] + 0xC, 0x22)]
            out.append(case('1BB7F0 t kind %s flag %x' % ('c' if kind is None else '%x' % kind, flag), 0x1BB7F0,
                            beat, (DOORS[1],), writes=w))
    # edge: the flag byte D_008106B8 zero with its neighbours non-zero (a
    # wider read of the flag shows)
    for kind in (8, 9):
        w = door_writes(DOORS[1], kind, 0, (FB(-9.2), FB(-9.2), FB(9.2))) + [W8(0x8106B9, 0x5A), W8(0x8106BA, 0x01),
                                                                            W8(0x8106BB, 0x80)]
        out.append(case('1BB7F0 edge flag neighbours kind %x' % kind, 0x1BB7F0, beat, (DOORS[1],), writes=w))
    # edge: array aliasing (bone 1's +0x7C is the array word 2)
    base = SCRATCH + 0x400
    for kind in (8, 9):
        w = [W32(D275B40, base), W32(base, base + 0x100), W32(base + 4, base + 8 - 0x7C), W32(base + 8, base + 0x300),
             W32(base + 0x17C, 0x11111111), W32(base + 0x37C, 0x33333333), W8(0x8106B8, 0), W8(SCRATCH + 0x803, kind),
             W8(SCRATCH + 0x80B, 0x77)]
        out.append(case('1BB7F0 edge alias kind %x' % kind, 0x1BB7F0, beat, (SCRATCH + 0x800,), writes=w))
    # edge: D_008106B8 set with a0 unmapped (the kind byte is read only
    # after the flag test, so the original returns 0 without touching a0)
    for a0 in (0x50000000, 0x03000000):
        out.append(case('1BB7F0 edge flag set a0 %x' % a0, 0x1BB7F0, beat, (a0,), writes=[W8(0x8106B8, 1)]))
    # edge: the array is the node itself, so the byte a0+0x0B is the high
    # byte of array word 2 (panel 2's pointer 0x70000200); the byte store
    # comes after the panel stores, so panel 2 is zeroed at 0x7000027C, not
    # at 0x27C (a byte store moved first would zero RAM 0x27C)
    node = SCRATCH + 0x800
    for kind in (9, 0x3D):
        w = [W32(D275B40, node), W32(node, kind << 24), W32(node + 4, SCRATCH + 0x600), W32(node + 8, 0x70000200),
             W32(SCRATCH + 0x67C, 0x11111111), W32(0x7000027C, 0x22222222), W32(0x27C, 0x33333333),
             W8(0x8106B8, 0)]
        out.append(case('1BB7F0 edge byte order kind %x' % kind, 0x1BB7F0, beat, (node,), writes=w))
    return out


def sampler_writes(rec, table=None, length=None, t=None, flag=None, outs=None):
    w = []
    if table is not None:
        w.append(W32(rec, table))
    if length is not None:
        w.append(W32(rec + 4, length))
    if t is not None:
        w.append(W32(rec + 8, t))
    if flag is not None:
        w.append(W8(rec + 0xC, flag))
    if outs:
        w += [W32(rec + 0x40 + 4 * i, v) for i, v in enumerate(outs)]
    return w


def cases_1D0D60(rng):
    out = []
    for b in ROOM_BEATS:
        for step in (1.0, 0.5):
            out.append(case('1D0D60 captured %s step %s' % (b, step), 0x1D0D60, b, (SAMPLER,), fargs=(FB(step),)))
    beat = 'a01_s5_duct'
    # the captured table (0x24FD50, rows up to the captured length 91)
    ts = (0.0, 0.5, 1.0, 50.25, 89.0, 89.5, 89.999, 90.0, 90.5, 91.0, 91.25, 181.9, 182.0, 300.5, -0.25, -1.0,
          -0.0, 1e-30)
    steps = (1.0, 0.0, 0.5, -0.5, 2.0, 91.0)
    for flag in (1, 0, 0x80):
        for t in ts:
            for step in steps:
                if flag == 0 and t < -0.9:
                    continue        # a clamp below zero indexes row -1: shown by the scratch table below
                out.append(case('1D0D60 t flag %x t %s step %s' % (flag, t, step), 0x1D0D60, beat, (SAMPLER,),
                                writes=sampler_writes(SAMPLER, None, FB(91.0), FB(t), flag), fargs=(FB(step),)))
    # a scratch table (rows -2..12 of random floats) with small lengths
    table = SCRATCH + 0x1000
    rows = [W32(table - 56 + 4 * i, FB(rng.uniform(-100, 100))) for i in range(7 * 15)]
    for flag in (1, 0):
        for length in (1.0, 2.0, 3.5, 10.0, 10.25, 0.75):
            for t in (0.0, 0.3, 1.0, 1.7, 2.0, 3.49, 3.5, 9.99, 10.0, 10.2, 21.0, -0.5, -1.5):
                if flag and (t < 0 or length < 1.0):
                    continue        # length < 1 wraps into the next index; negative t with a wrap never ends
                w = rows + sampler_writes(SAMPLER, table, FB(length), FB(t), flag)
                out.append(case('1D0D60 t scratch flag %d len %s t %s' % (flag, length, t), 0x1D0D60, beat,
                                (SAMPLER,), writes=w, fargs=(FB(rng.choice((0.0, 0.25, 1.0))),)))
    # EE float edges: the time and length at extreme patterns (non-loop)
    for t, length in ((0x7F7FFFFF, FB(10.0)), (0xFF7FFFFF, FB(10.0)), (0x7FC00000, FB(10.0)),
                      (FB(9.5), 0x7F800000), (0x00000001, FB(2.0)), (0x80000000, FB(2.0))):
        w = rows + sampler_writes(SAMPLER, table, length, t, 0)
        if t in (0x7F7FFFFF, 0xFF7FFFFF, 0x7FC00000):
            continue            # float_to_int of these indexes outside RAM (both sides would fault)
        out.append(case('1D0D60 edge float t %x len %x' % (t, length), 0x1D0D60, beat, (SAMPLER,), writes=w,
                        fargs=(0,)))
    # edge: Inf patterns in the rows (i = 1, next = 2, w = frac = 0.5). The
    # MULA product saturates to -+MAX; the MADD product stays +-Inf, so the
    # sum is the Inf's sign with MAX (a MUL then ADD, or the two products
    # swapped between MULA and MADD, gives 0 or the other sign)
    for k, (lo, hi) in enumerate(((0xFF800000, 0x7F800000), (0x7F800000, 0xFF800000), (0xFF800000, 0x7F7FFFFF),
                                  (0x7F7FFFFF, 0xFF800000), (0x7FC00000, 0xFF800000), (0xFF800000, 0x7FC00000),
                                  (0x3F800000, 0x7F800000))):
        w = rows + sampler_writes(SAMPLER, table, FB(10.0), FB(1.5), 0)
        w += [W32(table + 28 * 1 + 4 * k, lo), W32(table + 28 * 2 + 4 * k, hi)]
        out.append(case('1D0D60 edge inf rows %x %x' % (lo, hi), 0x1D0D60, beat, (SAMPLER,), writes=w,
                        fargs=(0,)))
    # edge: the loop flag byte zero with its neighbours +0x0D..+0x0F
    # non-zero (a wider read of the flag shows), and set with them
    for flag in (0, 1):
        w = sampler_writes(SAMPLER, None, FB(91.0), FB(90.5), flag) + [W8(SAMPLER + 0xD, 0x11), W8(SAMPLER + 0xE, 0x22),
                                                                     W8(SAMPLER + 0xF, 0x80)]
        out.append(case('1D0D60 edge flag neighbours %d' % flag, 0x1D0D60, beat, (SAMPLER,), writes=w,
                        fargs=(FB(1.0),)))
    # edge: the outputs overlap the rows (a store changes a later load) and
    # the record's own fields (the table pointer is output 0's neighbour)
    rec = SCRATCH + 0x3000
    for flag in (1, 0):
        w = sampler_writes(rec, rec + 0x40 - 28, FB(4.0), FB(1.5), flag)
        w += [W32(rec + 0x40 - 28 + 4 * i, FB(rng.uniform(-10, 10))) for i in range(7 * 5)]
        out.append(case('1D0D60 edge overlap flag %d' % flag, 0x1D0D60, beat, (rec,), writes=w, fargs=(FB(0.25),)))
    return out


def cases_225A00(rng):
    out = []
    for b in ROOM_BEATS:
        out.append(case('225A00 captured %s' % b, 0x225A00, b))
    out.append(case('225A00 t filled', 0x225A00, 'a01_s5_duct',
                    writes=[(0x81003C, bytes([0xFF]) * (4 + 0xD4 + 4))]))
    return out


FX_FNS = {0x198D90, 0x1D0D60}


def all_cases():
    rng = random.Random(0xA01200)
    base = []
    for gen in (cases_188610, cases_198D90, cases_1BB400, cases_1BB7C0, cases_1BB7F0, cases_1D0D60, cases_225A00):
        base += gen(rng)
    fx = [dict(c, name=c['name'] + ' fx ' + kind, fx=kind) for c in base if c['fn'] in FX_FNS
          for kind in ('after', 'before')]
    return base, fx


# ======================================================================
# Fail-stop contract
# ======================================================================

def api_checks():
    ram, spad = image('a01_s5_duct')
    N = NATIVE
    out = I32(-7)

    def fresh(script=None, worker=True):
        n = NativeRun(ram, spad, Script(script or {}))
        if not worker:
            n.room.call = WORKER()
        return n

    def failing(n):
        n.worker = WORKER(lambda _, c: -1)
        n.room.call = n.worker
    # NULL context: -1, nothing latched
    assert N.em_area01_room_00225A00(None) == -1
    assert N.em_area01_room_00188610(None, PLAYER, C.byref(out)) == -1
    # NULL worker at the first call: FAULT_NULL naming the callee
    n = fresh(worker=False)
    assert N.em_area01_room_00225A00(C.byref(n.room)) == -1
    assert (n.room.fault, n.room.fault_function, n.room.fault_address) == (1, 0x225A00, 0x121A28), \
        ('NULL worker', n.room.fault, hex(n.room.fault_function), hex(n.room.fault_address))
    # latched: every entry refuses before any work, until clear_fault
    before = bytes(n.ram)
    assert N.em_area01_room_001BB7F0(C.byref(n.room), DOORS[0], C.byref(out)) == -1
    assert bytes(n.ram) == before and n.room.fault == 1
    N.em_area01_room_clear_fault(C.byref(n.room))
    assert (n.room.fault, n.room.fault_function, n.room.fault_address) == (0, 0, 0)
    N.em_area01_room_clear_fault(None)
    # worker failure: FAULT_WORKER naming the callee; the store made before stays
    n = fresh()
    failing(n)
    t0 = u32(n.ram, SAMPLER + 8)
    rc = N.em_area01_room_001D0D60(C.byref(n.room), SAMPLER, FB(1.0), C.byref(out))
    assert rc == -1 and (n.room.fault, n.room.fault_function, n.room.fault_address) == (2, 0x1D0D60, 0x1281C0)
    assert u32(n.ram, SAMPLER + 8) != t0, 'the time advanced before the failing call stays'
    # unmapped: 00188610's byte, 001BB400's bone pointer
    n = fresh()
    assert N.em_area01_room_00188610(C.byref(n.room), 0x03000000, C.byref(out)) == -1
    assert (n.room.fault, n.room.fault_function, n.room.fault_address) == (3, 0x188610, 0x03000235)
    n = fresh()
    struct.pack_into('<I', n.ram, D275B40, SCRATCH)
    struct.pack_into('<I', n.ram, SCRATCH + 4, 0x02400000)
    n.ram[DOORS[0] + 3] = 9
    assert N.em_area01_room_001BB400(C.byref(n.room), DOORS[0], C.byref(out)) == -1
    assert (n.room.fault, n.room.fault_function, n.room.fault_address) == (3, 0x1BB400, 0x0240007C)
    n = fresh()
    struct.pack_into('<I', n.ram, D275B40, SCRATCH)
    struct.pack_into('<I', n.ram, SCRATCH, 0x02400000)
    n.ram[0x8106B8], n.ram[DOORS[0] + 3] = 0, 8
    assert N.em_area01_room_001BB7F0(C.byref(n.room), DOORS[0], C.byref(out)) == -1
    assert (n.room.fault, n.room.fault_function, n.room.fault_address) == (3, 0x1BB7F0, 0x0240007C)
    # NULL output: FAULT_NULL naming the entry, address 0, nothing written
    for entry, call in ((0x188610, lambda s: N.em_area01_room_00188610(s, PLAYER, None)),
                        (0x1BB400, lambda s: N.em_area01_room_001BB400(s, DOORS[0], None)),
                        (0x1BB7C0, lambda s: N.em_area01_room_001BB7C0(s, DOORS[0], None)),
                        (0x1BB7F0, lambda s: N.em_area01_room_001BB7F0(s, DOORS[0], None)),
                        (0x1D0D60, lambda s: N.em_area01_room_001D0D60(s, SAMPLER, FB(1.0), None))):
        n = fresh()
        n.room.fault_address = 0x1234
        before = bytes(n.ram)
        assert call(C.byref(n.room)) == -1
        assert (n.room.fault, n.room.fault_function, n.room.fault_address) == (1, entry, 0), \
            ('NULL output', hex(entry), n.room.fault, hex(n.room.fault_function), hex(n.room.fault_address))
        assert bytes(n.ram) == before and not n.log
    # every calling entry names itself and the callee on a worker failure
    for entry, call, callee in ((0x198D90, lambda s: N.em_area01_room_00198D90(s, CAMERA, PLAYER), 0x102948),
                                (0x1BB7C0, lambda s: N.em_area01_room_001BB7C0(s, DOORS[0], C.byref(out)),
                                 0x1BA1F0),
                                (0x225A00, lambda s: N.em_area01_room_00225A00(s), 0x121A28)):
        n = fresh()
        failing(n)
        assert call(C.byref(n.room)) == -1
        assert (n.room.fault, n.room.fault_function, n.room.fault_address) == (2, entry, callee), \
            ('worker failure', hex(entry), n.room.fault, hex(n.room.fault_function), hex(n.room.fault_address))
    # the first fault is kept
    n = fresh()
    failing(n)
    N.em_area01_room_00198D90(C.byref(n.room), CAMERA, PLAYER)
    first = (n.room.fault, n.room.fault_function, n.room.fault_address)
    assert first == (2, 0x198D90, 0x102948), first
    assert N.em_area01_room_00225A00(C.byref(n.room)) == -1
    assert (n.room.fault, n.room.fault_function, n.room.fault_address) == first


# ======================================================================
# Main (section A)
# ======================================================================

def branch_pcs():
    out = set()
    for lo, hi in FUNC_RANGES:
        for pc in range(lo, hi, 4):
            word = struct.unpack_from('<I', ELF_RAM, pc)[0]
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            if op in (4, 20) and rs == rt:
                continue            # unconditional
            if op in (1, 4, 5, 6, 7, 20, 21, 22, 23) or (op == 17 and rs == 8):
                out.add(pc)
    return out


ELF_RAM = b''


def one(c):
    return run_case(c)


def section_a():
    global NATIVE, BRANCH_PCS
    BRANCH_PCS = branch_pcs()
    NATIVE = build_native()
    # every direct call / jump target of the translated routines has a policy
    for lo, hi in FUNC_RANGES:
        for pc in range(lo, hi, 4):
            word = struct.unpack_from('<I', ELF_RAM, pc)[0]
            if word >> 26 in (2, 3):
                target = (word & 0x3FFFFFF) << 2
                if lo <= target < hi:
                    continue
                assert target in CALLEES or target in FUNCS, ('call without a policy', hex(pc), hex(target))
    for beat in ROOM_BEATS:
        ram, _ = image(beat)
        for lo, hi in FUNC_RANGES:        # the captured code is the pinned ELF's
            assert ram[lo:hi] == ELF_RAM[lo:hi], (beat, 'captured code differs', hex(lo))
    base, fx = all_cases()
    only = [p for p in os.environ.get('EM_AREA01_ROOM_ONLY', '').split(',') if p]
    if only:
        base = [c for c in base if c['name'].startswith(tuple(only))]
        fx = [c for c in fx if c['name'].startswith(tuple(only))]
    pins = ('captured', ' edge ', '1BB7F0 t kind 16 flag 0',
            # 00198D90's +3.0 add with sums that round differently (EE vs IEEE)
            '198D90 t st 1 mode 12 y 3f800003', '198D90 t st 1 mode 12 y 3f800007',
            '198D90 t st 1 mode 12 y 40a00003',
            # 001BB400 kind 0x3D with panel 1 ending below -13.0 (a branch the sample may miss)
            '1BB400 t kind 3d v c14cccce',
            # the limits hit exactly (-8.8 - 0.2 = -9.0, -12.8 - 0.2 = -13.0): below, not at or below
            '1BB400 t kind c v c10ccccd', '1BB400 t kind 3d v c14ccccd',
            # the NaN pattern through the EE subtract (IEEE would keep a NaN)
            '1BB400 t kind 8 v 7fc00000', '1BB400 t kind c v 7fc00000',
            # 00198D90's word compare of +0x230 (the low byte 0x12 with other bytes set)
            '198D90 t st 1 mode 112 y c', '198D90 t st 1 mode 12000000 y c',
            # 001BB7C0's whole 64-bit v0 (low word zero, high word not)
            '1BB7C0 t v0 100000000 rec', '1BB7C0 t v0 ffffffff00000000 rec')
    keep = lambda i, c: any(p in c['name'] for p in pins)      # noqa: E731
    run_base = RM.select(base, 180, 0xA011, axes=(lambda c: c['fn'],), keep=keep)
    run_fx = RM.select(fx, 60, 0xA012, axes=(lambda c: (c['fn'], c['fx']),),
                       keep=lambda i, c: ' captured ' in c['name'] and c['fx'] == 'after')
    results = RM.parallel_map(one, run_base + run_fx)
    outcomes, calls, fx_bytes = set(), 0, 0
    for o, n, fb in results:
        outcomes |= o
        calls += n
        fx_bytes += fb
    missing = sorted((hex(pc), t) for pc in BRANCH_PCS for t in (True, False) if (pc, t) not in outcomes)
    if not only:
        assert not missing, ('branch outcomes never taken', missing)
    api_checks()
    return [RM.part(len(run_base), len(base), 'cases'), RM.part(len(run_fx), len(fx), 'fx cases'),
            f'{calls} compared calls', f'{fx_bytes} fx field changes',
            f'{len(BRANCH_PCS)} branches both ways'], bool(only)


# ======================================================================
# B. Existing translations of the other room-beat rows
# ======================================================================

EXISTING_ROOT = Path(os.environ.get('EM_AREA01_ROOM_EXISTING_ROOT', str(ROOT))).resolve()


class CachedBuild:
    """subprocess for an existing module: a compiler run whose output (-o)
    is newer than every source it names and every header of src/ is skipped
    (the modules rebuild their libraries on every run); anything else runs."""

    def __init__(self, real):
        self.real = real

    def __getattr__(self, name):
        return getattr(self.real, name)

    def run(self, argv, *args, **kwargs):
        if argv and argv[0] == 'cc' and '-o' in argv:
            cwd = Path(kwargs.get('cwd') or '.')
            out = cwd / argv[argv.index('-o') + 1]
            inputs = [cwd / a for a in argv[1:] if a.endswith('.c')]
            headers = list((cwd / 'src').rglob('*.h'))
            if out.exists() and inputs and all(p.exists() for p in inputs):
                newest = max(p.stat().st_mtime for p in inputs + headers)
                if newest < out.stat().st_mtime:
                    return self.real.CompletedProcess(argv, 0)
        return self.real.run(argv, *args, **kwargs)


def existing_module(name):
    """tools/<name>.py from EXISTING_ROOT, imported fresh."""
    tools = str(EXISTING_ROOT / 'tools')
    if sys.path[0] != tools:
        sys.path.insert(0, tools)
    sys.modules.pop(name, None)
    mod = importlib.import_module(name)
    if hasattr(mod, 'subprocess'):
        mod.subprocess = CachedBuild(subprocess)
    return mod


B_JOB = {}


def b_closure_case(seed):
    return B_JOB['module'].run_case(seed)


def b_closure(name, beat, count, code_tables, entry_note):
    """The module's own unit oracle and native build with its captured image
    and player record replaced by `beat`'s."""
    mod = existing_module(name)
    mod.ELF = ELF_BYTES
    ram = (ROUTE / beat / 'eeMemory.bin').read_bytes()
    for address, size in code_tables(mod):
        at = address - 0x100000 + 0x300
        assert ram[address:address + size] == ELF_BYTES[at:at + size], (name, beat, 'captured bytes differ from the '
                                                                         'ELF', hex(address))
    mod.RAM = ram
    mod.CAPTURED = ram[PLAYER:PLAYER + 0x320]
    if hasattr(mod, 'NODE_ADDR'):
        mod.NODE_ADDR = {0: struct.unpack_from('<I', mod.CAPTURED, 0x40)[0],
                         1: struct.unpack_from('<I', mod.CAPTURED, 0x44)[0]}
    mod.NATIVE = mod.build_native()
    B_JOB['module'] = mod
    seeds = [0xA01500 + 7919 * i for i in range(count)]
    results = RM.parallel_map(b_closure_case, seeds)
    return len(results)


def closure_0e_tables(mod):
    return list(mod.TRANSLATED.items()) + list(mod.RUN_UNHOOKED.items()) + list(mod.TABLES)


def closure_1019_tables(mod):
    return tuple(mod.EXECUTED.items()) + tuple(mod.TABLES)


def b_major2(beat):
    """tools/test_player_major2_reference.py's main over `beat`'s image (its
    own case set, seeds, fixed scenarios and path assertions)."""
    mod = existing_module('test_player_major2_reference')
    mod.WORLD_RAM = ROUTE / beat / 'eeMemory.bin'
    mod.SEED_CAPTURES = ()
    rc = mod.main()
    assert rc == 0, ('major2 over', beat, rc)
    return 1


def jump_items(beat):
    """(beat, routine, writes) for 001AA4E0 and 0015FDF0 over one room image:
    the captured player; the player placed around each object of the
    captured target list D_00275B8C (count D_00275B94) at several distances
    and headings; the scratchpad byte 0x70003B8D set (the scan finds
    nothing while it is); the list emptied."""
    ram, _ = image(beat)
    lst, count = u32(ram, 0x275B8C), struct.unpack_from('<h', ram, 0x275B94)[0]
    hip = struct.unpack_from('<f', ram, PLAYER + 0xB4)[0] - struct.unpack_from('<f', ram, PLAYER + 0xA4)[0]
    spots = [(None, None, None, None)]
    objs = [u32(ram, lst + 4 * i) for i in range(max(0, count))]
    for obj in objs:
        ox, oy, oz = struct.unpack_from('<3f', ram, obj + 0xB0)
        for d in (0.0, 3.0, 8.0, 15.0, 30.0):
            for yaw in (0.0, 1.5707964, 3.1415927, -2.0):
                for dy in (0.0, -5.0, -9.5, 12.0, -40.0):     # feet +A4 relative to the entry's +B4
                    spots.append((ox + d, oy + dy, oz - d * 0.5, yaw))
    # the captured entries as they are (none qualifies in these captures:
    # docs/AREA01_ROOM.md section 4), and made to qualify: flags +2 = 2,
    # type +3 = 1 / 4 / 0xC (the target and aim paths over the AREA01 world)
    variants = [()]
    if objs:
        for kind in (1, 4, 0xC):
            variants.append(tuple(W8(obj + 2, (ram[obj + 2] & ~0x1F) | 2) for obj in objs) +
                            tuple(W8(obj + 3, kind) for obj in objs))
    out = []
    for vi, var in enumerate(variants):
        for si, (x, y, z, yaw) in enumerate(spots):
            if si and (si + vi) % 3:
                continue            # a third of the spots per variant (each spot in one variant)
            w = list(var)
            if x is not None:
                w += [W32(PLAYER + 0xB0, FB(x)), W32(PLAYER + 0xB4, FB(y + hip)), W32(PLAYER + 0xB8, FB(z)),
                      W32(PLAYER + 0xA0, FB(x)), W32(PLAYER + 0xA4, FB(y)), W32(PLAYER + 0xA8, FB(z)),
                      W32(PLAYER + 0xC4, FB(yaw))]
            for fn in (0x1AA4E0, 0x15FDF0):
                out.append((beat, fn, tuple(w)))
    for fn in (0x1AA4E0, 0x15FDF0):
        last = variants[-1]
        near = () if len(spots) < 2 else (W32(PLAYER + 0xA0, FB(spots[1][0])), W32(PLAYER + 0xA4, FB(spots[1][1])),
                                          W32(PLAYER + 0xA8, FB(spots[1][2])))
        out.append((beat, fn, last + near + (W8(0x70003B8D, 1),)))
        out.append((beat, fn, last + near + (W16(0x275B94, 0),)))
    return out


def b_jump_case(item):
    """The original routine against the module's native routine hooked at its
    address with the running-jump test's world workers (WorldJump: every
    worker runs the ORIGINAL callee in the same EE), over one room image;
    v0, all 32 MiB of RAM and the scratchpad must be identical."""
    beat, fn, writes = item
    mod = B_JOB['jump']
    case_ = dict(beat=beat, writes=writes)
    ram, spad = prepared(case_)
    o = mod.JumpEE(ELF, bytearray(ram), bytearray(spad))
    o.call(fn, (PLAYER,))
    ov0 = o.r[2] & MASK
    n = mod.JumpEE(ELF, bytearray(ram), bytearray(spad))
    world = mod.WorldJump(n)
    n.hooks[fn] = world.hook('scan' if fn == 0x1AA4E0 else 'aim')
    n.call(fn, (PLAYER,))
    if world.error is not None:
        raise world.error
    nv0 = n.r[2] & MASK
    where = ('jump', beat, hex(fn), writes)
    assert nv0 == ov0, (where, 'result', hex(nv0), hex(ov0))
    if bytes(n.mem) != bytes(o.mem):
        a, b = bytes(n.mem), bytes(o.mem)
        k = next(i for i in range(len(a)) if a[i] != b[i])
        raise AssertionError((where, 'RAM differs at', hex(k)))
    assert bytes(n.spad) == bytes(o.spad), (where, 'scratchpad differs')
    return ov0 != 0


JUMP_TOTAL = [0]


def b_jump(beats):
    mod = existing_module('test_player_running_jump_reference')
    mod.NATIVE = mod.build_native('running_jump_area01_room')
    B_JOB['jump'] = mod
    items = [it for beat in beats for it in jump_items(beat)]
    # quick: every beat's captured player (no writes) and its edge cases,
    # plus a fixed-seed sample; full: every spot
    JUMP_TOTAL[0] = len(items)
    items = RM.select(items, 60, 0x1AA4E0, axes=(lambda it: (it[0], it[1]),),
                      keep=lambda i, it: not it[2] or any(a in (0x70003B8D, 0x275B94) for a, _ in it[2]))
    found = RM.parallel_map(b_jump_case, items)
    return len(items), sum(found)


def b_status(beats):
    """00208AD0 (em_status_health_draw) and 002082B0 (em_item_geometry_arc)
    over the room images: the original routine over each image's RAM, the
    native module given the data read from that same image (the resident
    arc records D_00265390.., the text styles, the label strings); the
    captured counter / health / warning plus the threshold values. The arc
    records of each image also go through 002082B0 directly."""
    sd = existing_module('test_status_draw_reference')
    ig = existing_module('test_item_geometry_reference')
    out = OUT / 'existing'
    out.mkdir(parents=True, exist_ok=True)
    ext = 'dylib' if sys.platform == 'darwin' else 'so'
    libs = {}
    for name, srcs in (('draw', ['src/game/em_status_draw.c']),
                       ('geometry', ['src/game/em_item_geometry.c', 'src/game/em_item_sdk_math.c',
                                     'src/game/em_interaction_scan.c'])):
        lib = out / f'{name}.{ext}'
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-fPIC',
                        '-shared', '-Isrc'] + srcs + ['-lm', '-o', str(lib)], cwd=EXISTING_ROOT, check=True)
        libs[name] = C.CDLL(str(lib))
    draw, geo = libs['draw'], libs['geometry']
    draw.em_status_health_draw.argtypes = [C.POINTER(C.c_uint32), C.c_float, C.c_uint8, C.c_int, C.c_int,
                                           C.POINTER(sd.Data), C.POINTER(sd.Workers)]
    geo.em_item_geometry_arc.argtypes = [C.POINTER(C.c_float), C.POINTER(ig.Vertex), C.c_size_t,
                                         C.POINTER(C.c_size_t)]
    calls = []
    workers = sd.Workers(None, sd.Blend(lambda _, m: calls.append(('blend', m)) or 1),
                         sd.Rect(lambda _, x, y, x1, y1, c: calls.append(('rectangle', x, y, x1, y1, c)) or 1),
                         sd.Text(lambda _, p, x, y, w, h, t, c: calls.append(('text', p, x, y, w, h,
                                                                             t.decode('latin1'), c)) or 1),
                         sd.Arc(lambda _, a: calls.append(('arc', C.string_at(a, 96))) or 1),
                         sd.Sprite(lambda _, x, y, w, h, c, t: calls.append(('sprite', x, y, w, h, c, t)) or 1))
    health_cases = arcs = 0
    for beat in beats:
        ram, _ = image(beat)
        o = sd.Original(ELF, ram, 0, 0, True)
        d = sd.Data()
        for i, at in enumerate(range(0x265390, 0x265510, 0x60)):
            d.arcs[i] = (C.c_float * 24).from_buffer_copy(o.read(at, 96))
        d.white, d.red = o.load(0x265510, 8), o.load(0x265528, 8)
        d.label = o.string_bytes(o.load(0x267298))
        o.run(0x1CC170, (o.load(0x267298),))
        d.label_width = o.r[2]
        d.warning_max, d.normal_max, d.separator = [o.string_bytes(p) for p in (0x273558, 0x273560, 0x273568)]
        counter0 = struct.unpack_from('<I', ram, sd.UI + 0x20)[0]
        health0 = struct.unpack_from('<f', ram, 0x810858)[0]
        warn0 = ram[0x8104E4]
        combos = [(counter0, health0, warn0, 208, 196)]
        for counter in (counter0, 0, 59, 60):
            for health in (health0, 0.0, 34.99, 35.0, 59.99, 60.0, 100.0):
                for warn in (warn0, 0, 1, 2):
                    combos.append((counter, health, warn, 208, 196))
        combos = combos if RM.FULL else combos[:1] + random.Random(0x208AD0).sample(combos[1:], 5)
        for counter, health, warn, x, y in combos:
            health = sd.number(sd.bits(health))
            want_counter, wanted = sd.expected(ELF, ram, counter, health, warn, x, y)
            got_counter = C.c_uint32(counter)
            calls.clear()
            assert draw.em_status_health_draw(C.byref(got_counter), health, warn, x, y, C.byref(d),
                                              C.byref(workers)) == 1
            assert got_counter.value == want_counter and calls == wanted, (
                'status 00208AD0', beat, counter, health, warn,
                [(i, a, b) for i, (a, b) in enumerate(zip(calls, wanted)) if a != b][:2])
            health_cases += 1
        for at in range(0x265390, 0x265510, 0x60):
            desc = list(struct.unpack_from('<24f', ram, at))
            for start, end in ((desc[2], desc[3]),) + (((-44, 44), (46, 134), (-180, 180)) if RM.FULL else ()):
                dd = list(desc)
                dd[2:4] = [start, end]
                values = (C.c_float * 24)(*dd)
                wanted = ig.original_arc(ELF, list(values))
                verts, size = (ig.Vertex * 256)(), C.c_size_t()
                assert geo.em_item_geometry_arc(values, verts, 256, C.byref(size)) == 1, ('002082B0', beat, hex(at))
                got = [(v.rgba, v.x, v.y) for v in verts[:size.value]]
                assert got == wanted, ('status 002082B0', beat, hex(at), start, end)
                arcs += 1
    return health_cases, arcs


def b_pages_case(item):
    """One HEALING case; a case whose ORIGINAL run cannot complete on this
    image (a pointer the case's knobs rely on is unset in the room capture:
    the status hub is not open there) is not applicable and is counted, not
    compared."""
    try:
        return B_JOB['pages'].item_case(item)
    except AssertionError as err:
        if 'the original cannot complete this case' in repr(err.args[:1]) or \
                'the original cannot complete this case' in repr(err.args):
            return 'not applicable'
        raise


def b_pages(beats):
    """002160B0 (em_status_pages_002160B0) through
    tools/test_status_pages_reference.py's own runner (its interpreter,
    callees as ORIGINAL code, every callee entry with all of RAM, scratchpad
    and arguments compared) with its HEALING cases moved onto the room
    images."""
    sp = existing_module('test_status_pages_reference')
    for beat in beats:
        sp.IMAGES[beat] = ROUTE / beat
        sp.BEATS.append(beat)
    sp.setup()
    B_JOB['pages'] = sp
    base = [it for it in sp.item_items() + sp.item_extra() if it[0] == '002160B0' and it[1] == 'hub']
    rng = random.Random(0x2160B0)
    items = []
    for beat in beats:
        cases_ = [(k, beat, knobs) for k, _, knobs in base]
        # full: every HEALING case over a01_s5_duct (the beat whose pickup
        # opens the page), 40 over each other room capture; quick: 8
        items += cases_ if beat == 'a01_s5_duct' and RM.FULL else rng.sample(cases_, 40 if RM.FULL else 8)
    PAGES_TOTAL[0] = len(base) * len(beats)
    results = RM.parallel_map(b_pages_case, items)
    return (len(results), sum(1 for r in results if r not in ('unmeasured', 'skipped', 'not applicable')),
            sum(1 for r in results if r == 'not applicable'))


PAGES_TOTAL = [0]


ELF_BYTES = b''


def cpu():
    import resource
    return sum(resource.getrusage(w).ru_utime + resource.getrusage(w).ru_stime
               for w in (resource.RUSAGE_SELF, resource.RUSAGE_CHILDREN))


def timed(label, fn, *args):
    t, c = time.time(), cpu()
    try:
        return fn(*args)
    finally:
        if os.environ.get('EM_AREA01_ROOM_TIMES'):
            print('%-28s %.1f s wall, %.1f s CPU' % (label, time.time() - t, cpu() - c), flush=True)


def section_b():
    beats = ROOM_BEATS if RM.FULL else ('a01_s5_duct',)
    n0e = n1019 = 0
    for beat in beats:
        n0e += timed('closure 0e ' + beat, b_closure, 'test_player_closure_0e_18_reference', beat, RM.pick(4000, 600), closure_0e_tables,
                         '0016D130')
        n1019 += timed('closure 1019 ' + beat, b_closure, 'test_player_closure_10_12_19_reference', beat, RM.pick(4000, 600), closure_1019_tables,
                           '0016DE40')
    majors = sum(timed('major2 ' + beat, b_major2, beat) for beat in (beats if RM.FULL else ()))
    jumps, found = timed('jump', b_jump, ROOM_BEATS)
    health, arcs = timed('status', b_status, ROOM_BEATS)
    pages, measured, na = timed('pages', b_pages, beats)
    return [f'B: closure 0E/18 {n0e} and closure 10/12/19 {n1019} cases over {len(beats)} room capture(s)',
            f'major2 main over {majors} room captures' if majors else 'major2 main: full mode only',
            RM.part(jumps, JUMP_TOTAL[0], '001AA4E0 / 0015FDF0 world cases over 4 room captures') + f' ({found} with a target)',
            f'{health} 00208AD0 and {arcs} 002082B0 cases over 4 room captures',
            f'{pages:,} of {PAGES_TOTAL[0]:,} 002160B0 cases over {len(beats)} room capture(s)' +
            f' ({measured} compared, {na} not applicable: the original cannot run them on the image)']


def main():
    global ELF, ELF_RAM, ELF_BYTES
    t0 = time.time()
    ELF = read_elf()
    ELF_BYTES = ELF
    probe = RoomEE(ELF)
    ELF_RAM = bytes(probe.mem[:0x300000])
    for beat in ROOM_BEATS:
        image(beat)
    parts, only = timed('section A', section_a)
    if not only and os.environ.get('EM_AREA01_ROOM_EXISTING') != '0':
        parts += section_b()
    RM.banner(*parts)
    print('area01 room reference: PASS (%.1f s wall)' % (time.time() - t0))


if __name__ == '__main__':
    main()
