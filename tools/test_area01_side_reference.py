#!/usr/bin/env python3
"""Execute the original AREA01 lane-SIDE routines and compare em_area01_side.c.

docs/AREA01_SIDE.md. The user's pinned ELF and the captured AREA01 route RAM
(../Extermination/build/s87/route_a01/<beat>/, end-of-beat images) supply
every instruction and every table; none are embedded here.

A. The seven routines translated in em_area01_side.c (001AF7C0, 001C4720,
   001CB480, 001E3D20, 001EFE00, 001F0190, 001F0290). The oracle is FallEE
   (tools/test_player_fall_reference.py, COP1 through ee_float_model.py, the
   measured model) running the ORIGINAL routine over a copy of a captured
   beat; the native module runs over another copy of the same bytes. The
   design is the lane-SYS oracle (docs/AREA01_SYS.md section 4): every call
   leaving the translated set is caught on both sides and handled by one
   policy, `run` (the quadword copy 00102948 runs as original code, on the
   native side in a second interpreter bound to the module's memory) or
   `stub` (recorded, scripted result, possibly a 64-bit register image).
   Lockstep: at every call the callee, the stack pointer, the argument
   registers the callee is handed (64-bit images), the float argument
   registers, the 16 bytes behind a stack-pointer argument, the bytes
   behind stub pointer arguments, and every RAM / scratchpad line either
   side stored to since the previous call are compared; again after the
   last store; then all 32 MiB, the scratchpad and the result.
   Both stacks start filled with 0xA5 and the interpreter's registers with
   a pattern.
   fx cases (the SYS design, reduced): pass 1 records the data loads the
   translated routines make after each stubbed call; pass 2 changes exactly
   those fields at that stub, identically on both sides ('fx'), or changes
   the fields loaded BEFORE the stub ('fx before', the n-th change flipping
   bits no earlier change flipped), so a value cached across a stub, or a
   read deferred across one, differs.
   Branch coverage: both outcomes of every conditional branch of the seven
   routines are taken (asserted in every run).
   api_checks: the fail-stop contract (NULL context, NULL worker, worker
   failure, unmapped address, NULL output, the latch, clear_fault).
B. Existing port translations of other side-only census rows, checked
   unchanged against the original over the AREA01 fire-contact player
   record (D_008102B0 in a01_s3_fire_contact): em_player_0021BC40 (every
   +0x1F0 value), em_player_reaction_0021D600 (every +0x1F1 value),
   em_player_0021C350 (+0x220 / +0x224 pairs), and the state routine
   em_player_reaction_0021D800 (with 0021D1A0 / 0021D600 inside) through
   tools/test_player_reaction_reference.py's own oracle and workers.
   EM_AREA01_SIDE_EXISTING_ROOT=<tree> compiles those sources from another
   checkout (e.g. a HEAD export while another chain edits the working
   tree); default: this repository.

EM_TEST_FULL=1 runs every case; EM_AREA01_SIDE_SOURCE=<file> tests another
copy of the module source (mutation runs); EM_AREA01_SIDE_ONLY=<prefix,...>
runs only the cases whose name starts with a prefix; EM_AREA01_SIDE_EXISTING=0
skips section B. At most four worker
processes (EM_TEST_JOBS overrides).
"""
import ctypes as C
import os
import random
import struct
import subprocess
import sys
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
OUT = ROOT / 'build' / 'area01' / 'side'
STACK_BASE, STACK_SIZE = 0x7F000000, 0x100000
STACK_LO, STACK_HI = STACK_BASE, STACK_BASE + STACK_SIZE
SP = shared.STACK_TOP
SCRATCH = 0x01E00000            # zero in every captured beat; test records live here
SIDE_BEATS = ('a01_s1_sentry_doc', 'a01_s2_control_room_items', 'a01_s3_fire_contact')
ALL_BEATS = ('a01_00_train_room', 'a01_01_tunnel', 'a01_02_shaft_landing', 'a01_03_shaft_locked',
             'a01_04_return_north', 'a01_05_npc_bridge_talk', 'a01_06_return_south', 'a01_07_level_exit',
             'a01_s0_npc_first_talk') + SIDE_BEATS
PLAYER = 0x008102B0
OWNERS = tuple(0x7A96E0 + 0x2F0 * i for i in range(3)) + (0x7AD780,)   # records whose +0x34 is 001E3D20

# Translated routines (address -> size in bytes, from the census).
FUNCS = {0x1AF7C0: 0x34, 0x1C4720: 0x34, 0x1CB480: 0x70, 0x1E3D20: 0x6C, 0x1EFE00: 0xAC,
         0x1F0190: 0x100, 0x1F0290: 0x30}
FUNC_RANGES = tuple((a, a + n) for a, n in FUNCS.items())

# Callee policy: address -> (kind, integer argument registers compared (a0..),
# float argument registers compared (f12..), [(pointer argument, bytes)] for
# stubs). The counts are the registers the original sets for each call.
R, S = 'run', 'stub'
CALLEES = {
    0x102948: (R, 2, 0, ()),
    0x1D2910: (S, 1, 0, ()), 0x1D8C20: (S, 1, 0, ()), 0x1D2830: (S, 2, 0, ()),
    0x1C7420: (S, 3, 0, ()), 0x1D3BA0: (S, 2, 0, ()),
    0x21BB00: (S, 1, 0, ()), 0x1EF9D0: (S, 2, 1, ((1, 16),)),
    0x21B9A0: (S, 1, 2, ()),
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


class SideEE(FallEE):
    """FallEE recording branch outcomes inside the translated routines, the
    RAM / scratchpad lines it stores to (`dirty`; `pre` keeps each line's
    bytes before its first store), and for the fx cases the data loads the
    translated routines make (`window` = stubs made so far)."""

    def __init__(self, elf, ram=None, spad=None):
        super().__init__(elf, ram, spad)
        self.outcomes = set()
        self.dirty = set()
        self.pre = None
        self._pc = None
        self.window = 0
        self.loads = None

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
    kind, na, nf, snaps = pol
    args = tuple(regs[i] & MASK64 for i in range(na))
    floats = tuple(fregs[i] & MASK for i in range(nf))
    stack = tuple(ee.read(a & MASK, 16) for a in args if STACK_LO <= (a & MASK) < STACK_HI - 16)
    snap = tuple(ee.read(regs[i] & MASK, n) for i, n in snaps) if kind == S else ()
    return (hex(fn), hex(sp & MASK), args, floats, stack, snap)


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


class Side(C.Structure):
    _fields_ = [('regions', P(Region)), ('region_count', C.c_uint), ('call', WORKER), ('ctx', C.c_void_p),
                ('sp', U32), ('fault', I32), ('fault_function', U32), ('fault_address', U32),('view',C.c_void_p)]


NATIVE = None
ELF = None
IMAGES = {}
TRACE = {}

TRACE_C = r"""/* Store trace for the native module (test build only). */
#include <stdint.h>
#define RAM_LINES (0x2000000u / 64u)
#define LINES (RAM_LINES + 0x4000u / 64u)
static uint8_t dirty[LINES];
uint32_t a01side_trace_list[LINES];
uint32_t a01side_trace_count;
void a01side_trace_store(uint32_t address, unsigned size);
void a01side_trace_reset(void);
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
        a01side_trace_list[a01side_trace_count++] = id;
    }
}
void a01side_trace_store(uint32_t address, unsigned size)
{
    mark(address);
    mark(address + size - 1u);
}
void a01side_trace_reset(void)
{
    uint32_t i;
    for (i = 0; i < a01side_trace_count; i++)
        dirty[a01side_trace_list[i]] = 0;
    a01side_trace_count = 0;
}
"""


def build_native():
    """The module with its store trace. EM_AREA01_SIDE_SOURCE names another
    copy of the source (a mutant), built into its own library."""
    OUT.mkdir(parents=True, exist_ok=True)
    trace = OUT / 'store_trace.c'
    if not trace.exists() or trace.read_text() != TRACE_C:
        trace.write_text(TRACE_C)
    ext = 'dylib' if sys.platform == 'darwin' else 'so'
    source = os.environ.get('EM_AREA01_SIDE_SOURCE', '')
    if source:
        source = Path(source).resolve()
        lib = OUT / f'area01_side_{source.stem}.{ext}'
    else:
        source = ROOT / 'src/game/em_area01_side.c'
        lib = OUT / f'area01_side.{ext}'
    deps = [source, ROOT / 'src/game/em_area01_side.h', ROOT / 'src/game/em_ee_float.h', trace, Path(__file__)]
    if not lib.exists() or max(p.stat().st_mtime for p in deps) > lib.stat().st_mtime:
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                        '-ffp-contract=off', '-shared', '-fPIC', '-Isrc', '-Isrc/game',
                        '-DEM_AREA01_SIDE_STORE_TRACE=a01side_trace_store', str(source), str(trace),
                        '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    TRACE['count'] = U32.in_dll(native, 'a01side_trace_count')
    TRACE['list'] = (U32 * (RAM_LINES + 0x4000 // LINE)).in_dll(native, 'a01side_trace_list')
    TRACE['reset'] = native.a01side_trace_reset
    SP_ = P(Side)
    native.em_area01_side_001AF7C0.argtypes = [SP_, P(I32)]
    native.em_area01_side_001C4720.argtypes = [SP_, I32, I32, P(I32)]
    native.em_area01_side_001CB480.argtypes = [SP_, U32]
    native.em_area01_side_001E3D20.argtypes = [SP_, U32, U32]
    native.em_area01_side_001EFE00.argtypes = [SP_, I32, U32, P(I32)]
    native.em_area01_side_001F0190.argtypes = [SP_, U32, U32]
    native.em_area01_side_001F0290.argtypes = [SP_]
    native.em_area01_side_clear_fault.argtypes = [SP_]
    native.em_area01_side_clear_fault.restype = None
    return native


def native_lines():
    n = TRACE['count'].value
    if not n:
        return ()
    out = TRACE['list'][:n]
    TRACE['reset']()
    return out


class Lockstep:
    """The oracle's side of the per-call memory check (as lane SYS)."""

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
        self.regions = (Region * 3)(Region(0, len(ram), C.cast(self.ram, P(U8))),
                                    Region(0x70000000, len(spad), C.cast(self.spad, P(U8))),
                                    Region(STACK_BASE, STACK_SIZE, C.cast(self.stack, P(U8))))
        self.ee = SideEE(ELF, b'', b'')
        self.ee.mem = memoryview(self.ram).cast('B')
        self.ee.spad = memoryview(self.spad).cast('B')
        self.ee.stack = memoryview(self.stack).cast('B')
        self.script, self.log, self.error = script, [], None
        self.lock = None
        self.fx = {}
        self.stubs = 0
        self.worker = WORKER(self._call)
        self.side = Side(self.regions, 3, self.worker, None, SP, 0, 0, 0)
        from area01_reference_view import ENABLED, CanonicalView
        if ENABLED:
            self.canonical_view=CanonicalView(self.regions)
            self.canonical_view.install(self.side)

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
            kind, na, nf = pol[0], pol[1], pol[2]
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
                for address, size, value in self.fx.get(self.stubs, ()):
                    self.ee.save(address, value, size)
                self.stubs += 1
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

def oracle_run(case, ram, spad, on_stub=None, record=False):
    """The original over `ram` / `spad`. Returns (ee, log, v0, entries,
    final delta); entries[k] = (log entry, {line: bytes} stored since the
    previous call, a0..t3 images, f12..f15 bits) at the k-th call."""
    ee = SideEE(ELF, b'', b'')
    ee.mem, ee.spad = ram, spad
    ee.stack[:] = STACK_FILL
    ee.pre = {}
    if record:
        ee.loads = {}
    script, log, entries = Script(case['script']), [], []
    stubs = [0]

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
                if on_stub is not None:
                    on_stub(e, stubs[0])
                stubs[0] += 1
                e.window = stubs[0]
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
    ee.run(case['fn'])
    return ee, log, ee.r[2] & MASK, entries, ee.take()


# ======================================================================
# Stub side effects (fx)
# ======================================================================

def fx_plan(case, before):
    """{stub index k: [(address, size)]}: the fields the translated routines
    load after the k-th stub (before the next), or with `before` the fields
    loaded since the previous stub, changed at the k-th stub."""
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
            plan[w] = items          # loaded before stub number w: change them at it
        elif w > 0:
            plan[w - 1] = items
    return plan


def is_pointer(v):
    return 0x100000 <= v < 0x2000000 or 0x70000000 <= v < 0x70004000


class Perturb:
    """At the k-th stub, change the planned fields (identically on both
    sides; the bytes written are logged for the native run). Plain: bytes
    and halfwords flip bit 0, words flip bit 4. Before: the n-th change of a
    field flips bits no earlier change flipped, in both halves."""

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

RETURNS = {0x1AF7C0, 0x1C4720, 0x1EFE00}


def native_call(fn, sidep, args, fargs):
    out = I32(0)
    N = NATIVE
    a = list(args) + [0] * 4
    f = list(fargs) + [0] * 2
    if fn == 0x1AF7C0:
        return N.em_area01_side_001AF7C0(sidep, C.byref(out)), out.value & MASK
    if fn == 0x1C4720:
        return N.em_area01_side_001C4720(sidep, s32(a[0]), s32(a[1]), C.byref(out)), out.value & MASK
    if fn == 0x1CB480:
        return N.em_area01_side_001CB480(sidep, a[0] & MASK), None
    if fn == 0x1E3D20:
        return N.em_area01_side_001E3D20(sidep, a[0] & MASK, a[1] & MASK), None
    if fn == 0x1EFE00:
        return N.em_area01_side_001EFE00(sidep, s32(a[0]), a[1] & MASK, C.byref(out)), out.value & MASK
    if fn == 0x1F0190:
        return N.em_area01_side_001F0190(sidep, f[0] & MASK, f[1] & MASK), None
    if fn == 0x1F0290:
        return N.em_area01_side_001F0290(sidep), None
    raise AssertionError(('no native entry', hex(fn)))


def s32(v):
    v &= MASK
    return v - (1 << 32) if v & 0x80000000 else v


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
        ee, olog, ov0, entries, last = oracle_run(case, ram, spad, on_stub=fx)
    except AssertionError as e:
        raise AssertionError((where, 'the original run stopped', e.args)) from e
    if fx is not None:
        nat.fx = fx.log
    nat.lock = Lockstep(entries, ee.pre, ee.mem, ee.spad)
    rc, nv0 = native_call(case['fn'], C.byref(nat.side), case['args'], case.get('fargs', ()))
    if nat.error is not None:
        raise AssertionError((where, 'native worker error', repr(nat.error)))
    assert rc == 0, (where, 'native faulted', rc, nat.side.fault, hex(nat.side.fault_function),
                     hex(nat.side.fault_address))
    assert len(nat.log) == len(entries), (where, 'native makes fewer calls than the original', len(nat.log),
                                          len(entries))
    try:
        nat.lock.check(('after the last store',), last, nat.dirty(), nat.ee.mem, nat.ee.spad)
    except AssertionError as e:
        raise AssertionError((where,) + e.args) from e
    if case['fn'] in RETURNS:
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


def cases_1AF7C0(rng):
    out = [case('1AF7C0 captured %s' % b, 0x1AF7C0, b) for b in ALL_BEATS]
    ram, _ = image('a01_s2_control_room_items')
    p0 = u32(ram, 0x275BD0)
    for count in (0, 1, 2, -1, 0x7FFF, -0x8000, 0x8000 - 1):
        for p in (p0, 0x275BD0, 0x275BCC, 0x70000100, SCRATCH + 0x40):
            w = [W16(0x275BCC, count), W32(0x275BD0, p), W32(SCRATCH + 0x40, 0x89ABCDEF),
                 W32(0x70000100, 0x80000001), W16(0x275BCE, 0xA55A)]
            out.append(case('1AF7C0 t count %d p %x' % (count, p), 0x1AF7C0, 'a01_s2_control_room_items',
                            writes=w))
    return out


def cases_1C4720(rng):
    out = []
    for b in SIDE_BEATS:
        for a0 in (0, 1, 5, 0x0B, 0x2F, 0xFF, 0x100, -1, -0x608, -0x607, 0x1000, -0x10CB8):
            for a1 in (1, 0, -1, 0xFF, 0x100, 0x101, 0x7FFFFFFF, -0x80000000):
                if b != 'a01_s2_control_room_items' and a1 not in (1, -1):
                    continue
                out.append(case('1C4720 t %s a0 %d a1 %d' % (b[4:6], a0, a1), 0x1C4720, b, (a0, a1),
                                writes=[W8(0x810CB8 + a0, rng.getrandbits(8))] if rng.randrange(2) else ()))
    # edge: the byte is the last one of RAM (a wider read there leaves the region)
    out.append(case('1C4720 edge last RAM byte', 0x1C4720, 'a01_s2_control_room_items', (0x1FFFFFF - 0x810CB8, 3),
                    writes=[W8(0x1FFFFFF, 0xFE)]))
    return out


def cases_1CB480(rng):
    out = []
    records = [('player', PLAYER), ('owner', OWNERS[0]), ('scratch', SCRATCH + 0x100)]
    kept = (0, 5, -1, 0x80000000, 0x123456789, 0xFFFFFFFF00000000, 0x7FFFFFFF)
    for b in SIDE_BEATS:
        for tag, rec in records:
            for v in kept:
                w = [W32(SCRATCH + 0x100 + 0x44, rng.getrandbits(32))]
                script = {0x1D2910: [v], 0x1D8C20: [rng.getrandbits(32)], 0x1D2830: [3, 4],
                          0x1C7420: [rng.getrandbits(32)], 0x1D3BA0: [rng.getrandbits(32)]}
                out.append(case('1CB480 t %s %s kept %x' % (b[4:6], tag, v & MASK64), 0x1CB480, b, (rec,), script, w))
    return out


def cases_1E3D20(rng):
    out = []
    beat = 'a01_s3_fire_contact'
    for i, owner in enumerate(OWNERS):
        for flag in (3, 0, 1, 2, 0xFD, 0xFF):
            for gate in (0, 1, -1, 0x100000000):
                for spawn in (0, SCRATCH + 0x200, PLAYER):
                    if i and (gate or flag not in (0, 3)):
                        continue
                    w = [W8(PLAYER, flag)]
                    out.append(case('1E3D20 t o%d flag %x gate %x spawn %x' % (i, flag, gate & MASK64, spawn),
                                    0x1E3D20, beat, (owner, PLAYER), {0x21BB00: [gate], 0x1EF9D0: [spawn]}, w))
    # a0 aliasing a1: word a0+0x1F0 lands on byte a1+0x0F (stored after it)
    for spawn in (0, SCRATCH + 0x200):
        out.append(case('1E3D20 t alias spawn %x' % spawn, 0x1E3D20, beat, (PLAYER + 0xF - 0x1F0, PLAYER),
                        {0x21BB00: [0], 0x1EF9D0: [spawn]}, [W8(PLAYER, 0)]))
    for b in SIDE_BEATS:
        out.append(case('1E3D20 captured %s' % b, 0x1E3D20, b, (OWNERS[0], PLAYER), {0x1EF9D0: [SCRATCH + 0x200]}))
    # edge: the bytes around the two stores are non-zero (a wider store shows)
    out.append(case('1E3D20 edge neighbours', 0x1E3D20, beat, (OWNERS[1], PLAYER), {0x1EF9D0: [SCRATCH + 0x200]},
                    [W8(PLAYER, 0), W8(PLAYER + 0xE, 0x5A), W8(PLAYER + 0x10, 0xA5), W32(OWNERS[1] + 0x1EC, 0x11223344),
                     W32(OWNERS[1] + 0x1F4, 0x55667788)]))
    # edge: a1 is not D_008102B0, so the constant 0021BB00 argument and every
    # a1-based access (gate byte, 001EFE00 argument, +0x0F store) are told
    # apart; the D_008102B0 gate byte is set opposite to a1's (review 2:
    # a1 passed to 0021BB00, and the constant used in place of a1)
    for a1 in (OWNERS[1], SCRATCH + 0x400):
        for flag in (0, 3):
            for gate in (0, 1):
                for spawn in (0, SCRATCH + 0x200):
                    if flag and (gate or spawn):
                        continue
                    out.append(case('1E3D20 edge a1 %x flag %x gate %x spawn %x' % (a1, flag, gate, spawn),
                                    0x1E3D20, beat, (OWNERS[0], a1), {0x21BB00: [gate], 0x1EF9D0: [spawn]},
                                    [W8(a1, flag), W8(PLAYER, 3 - flag)]))
    return out


def cases_1EFE00(rng):
    out = []
    beat = 'a01_s3_fire_contact'
    ids = (0x80000027, 0x80000018, 0x80000026, 0x80000028, 0x00000027, -1, 0x7FFFFFFF)
    ys = (None, 0x7F7FFFFF, 0xFF7FFFFF, 0x00000001, 0x80000000, 0x7F800000, 0xC1200000, 0x4B000000,
          0xC11FFFFF, 0x7FC00000)
    for a0 in ids:
        for y in ys:
            if a0 != 0x80000027 and y is not None:
                continue
            for spawn in (0, SCRATCH + 0x300, PLAYER, PLAYER + 0x10, PLAYER - 0x10):
                if y is not None and spawn not in (0, SCRATCH + 0x300):
                    continue
                w = [W32(PLAYER + 0xB4, y)] if y is not None else []
                out.append(case('1EFE00 t id %x y %s spawn %x' % (a0 & MASK, 'c' if y is None else '%x' % y, spawn),
                                0x1EFE00, beat, (a0, PLAYER), {0x1EF9D0: [spawn]}, w))
    # edge: 001EF9D0's v0 has a zero low word and a non-zero upper word; the
    # original tests the whole register (and, in this interpreter, addresses
    # with its low word)
    out.append(case('1EFE00 edge v0 upper only', 0x1EFE00, beat, (0x80000027, PLAYER), {0x1EF9D0: [0x100000000]}))
    for b in SIDE_BEATS:
        for rec in (PLAYER, OWNERS[1]):
            out.append(case('1EFE00 captured %s %x' % (b, rec), 0x1EFE00, b, (0x80000027, rec),
                            {0x1EF9D0: [SCRATCH + 0x300]}))
    return out


def cases_1F0190(rng):
    out = []
    beat = 'a01_s3_fire_contact'
    keys = ((0x01, 0x00), (0x08, 0x00), (0x16, 0x00), (0x08, 0x01), (0x16, 0x01), (0x00, 0x08), (0x07, 0xFF),
            (0x15, 0xFF), (0x00, 0x16))
    base = 0x811CC0
    th = (None, 0x00000000, 0x80000000, 0x00000001, 0x7F800000, 0xFF800000, 0x7FC00000)
    vals = (FB(-110.0), FB(-110.00001), FB(-109.99999), 0, 0x80000000, FB(330.0), FB(331.0), FB(329.99997),
            0x7FC00000, 0x7F800000, 0xFF800000, 0x00000001, 0x80000001, 0x7F7FFFFF, 0xFF7FFFFF)
    for hi, lo in keys:
        w = [W8(0x810700, hi), W8(0x810701, lo), W32(0x275C3C, 0xFFFFFFFF), W8(0x275C3B, 0x5A),
             W8(0x275C40, 0xA5)]
        out.append(case('1F0190 t key %02x%02x' % (hi, lo), 0x1F0190, beat, (), None, w, (FB(-200.0), FB(400.0))))
    for t in th:
        for x in vals:
            for y in (x, FB(0.0), FB(1000.0)):
                w = [W8(0x810700, 1), W8(0x810701, 0), W32(0x275C3C, 0xFF00FF00), W8(0x275C3B, 0x5A)]
                if t is not None:
                    w += [W32(base + 0xB8, t), W32(base + 0xBC, t)]
                out.append(case('1F0190 t th %s x %x y %x' % ('c' if t is None else '%x' % t, x, y), 0x1F0190, beat,
                                (), None, w, (x, y)))
    for b in ALL_BEATS:
        out.append(case('1F0190 captured %s' % b, 0x1F0190, b, (), None, (), (FB(-120.0), FB(10.0))))
    return out


def cases_1F0290(rng):
    out = []
    for v in (0, 1, 0x80000000, 0x100, 0xFFFFFFFF, 0x10000):
        out.append(case('1F0290 t counter %x' % v, 0x1F0290, 'a01_s3_fire_contact', (), None, [W32(0x275C3C, v)]))
    for b in ALL_BEATS:
        out.append(case('1F0290 captured %s' % b, 0x1F0290, b))
    return out


FX_FNS = {0x1CB480, 0x1E3D20, 0x1EFE00, 0x1F0190}


def all_cases():
    rng = random.Random(0xA01D)
    base = []
    for gen in (cases_1AF7C0, cases_1C4720, cases_1CB480, cases_1E3D20, cases_1EFE00, cases_1F0190,
                cases_1F0290):
        base += gen(rng)
    # the 001E3D20 a1 edge cases test argument identity only and have no fx
    # variants, so the fx list (and its fixed-seed default sample) is unchanged
    fx = [dict(c, name=c['name'] + ' fx ' + kind, fx=kind) for c in base if c['fn'] in FX_FNS
          and not c['name'].startswith('1E3D20 edge a1 ') for kind in ('after', 'before')]
    return base, fx


# ======================================================================
# Fail-stop contract
# ======================================================================

def api_checks():
    ram, spad = image('a01_s3_fire_contact')
    N = NATIVE
    out = I32(-7)

    def fresh(script=None, worker=True):
        n = NativeRun(ram, spad, Script(script or {}))
        if not worker:
            n.side.call = WORKER()
        return n
    # NULL context: -1, nothing latched
    assert N.em_area01_side_001F0290(None) == -1
    assert N.em_area01_side_001AF7C0(None, C.byref(out)) == -1
    # NULL worker at the first call: FAULT_NULL naming the callee
    n = fresh(worker=False)
    n.ram[0x275C3C] = 1
    assert N.em_area01_side_001F0290(C.byref(n.side)) == -1
    assert (n.side.fault, n.side.fault_function, n.side.fault_address) == (1, 0x1F0290, 0x21B9A0), \
        ('NULL worker', n.side.fault, hex(n.side.fault_function), hex(n.side.fault_address))
    # latched: every entry refuses before any work, until clear_fault
    before = bytes(n.ram[0x275BCC:0x275BD4])
    assert N.em_area01_side_001AF7C0(C.byref(n.side), C.byref(out)) == -1
    assert bytes(n.ram[0x275BCC:0x275BD4]) == before and n.side.fault == 1
    N.em_area01_side_clear_fault(C.byref(n.side))
    assert (n.side.fault, n.side.fault_function, n.side.fault_address) == (0, 0, 0)
    N.em_area01_side_clear_fault(None)
    # worker failure: FAULT_WORKER naming the callee; the stores made before stay
    n = fresh()
    n.worker = WORKER(lambda _, c: -1)
    n.side.call = n.worker
    struct.pack_into('<I', n.ram, 0x275C3C, 5)
    rc = N.em_area01_side_001F0190(C.byref(n.side), FB(-500.0), FB(0.0))
    assert rc == -1 and (n.side.fault, n.side.fault_function, n.side.fault_address) == (2, 0x1F0190, 0x21B9A0)
    assert struct.unpack_from('<I', n.ram, 0x275C3C)[0] == 0, 'the counter reset before the failing call stays'
    # unmapped: the pop from a pointer outside every region
    n = fresh()
    struct.pack_into('<h', n.ram, 0x275BCC, 3)
    struct.pack_into('<I', n.ram, 0x275BD0, 0x03000000)
    assert N.em_area01_side_001AF7C0(C.byref(n.side), C.byref(out)) == -1
    assert (n.side.fault, n.side.fault_function, n.side.fault_address) == (3, 0x1AF7C0, 0x03000000)
    # NULL output: FAULT_NULL naming the entry, address 0, nothing written
    for entry, call in ((0x1AF7C0, lambda s: N.em_area01_side_001AF7C0(s, None)),
                        (0x1C4720, lambda s: N.em_area01_side_001C4720(s, 1, 1, None)),
                        (0x1EFE00, lambda s: N.em_area01_side_001EFE00(s, 0x27, PLAYER, None))):
        n = fresh()
        n.side.fault_address = 0x1234
        before = bytes(n.ram)
        assert call(C.byref(n.side)) == -1
        assert (n.side.fault, n.side.fault_function, n.side.fault_address) == (1, entry, 0), \
            ('NULL output', hex(entry), n.side.fault, hex(n.side.fault_function), hex(n.side.fault_address))
        assert bytes(n.ram) == before
    # every entry names itself as the faulting function
    n = fresh()
    assert N.em_area01_side_001C4720(C.byref(n.side), 0x03000000 - 0x810CB8, 1, C.byref(out)) == -1
    assert (n.side.fault, n.side.fault_function, n.side.fault_address) == (3, 0x1C4720, 0x03000000)
    for entry, call, callee in ((0x1E3D20, lambda s: N.em_area01_side_001E3D20(s, OWNERS[0], PLAYER), 0x21BB00),
                                (0x1EFE00, lambda s: N.em_area01_side_001EFE00(s, 0x27, PLAYER, C.byref(out)),
                                 0x102948)):
        n = fresh()
        n.ram[PLAYER] = 0
        n.worker = WORKER(lambda _, c: -1)
        n.side.call = n.worker
        assert call(C.byref(n.side)) == -1
        assert (n.side.fault, n.side.fault_function, n.side.fault_address) == (2, entry, callee), \
            ('worker failure', hex(entry), n.side.fault, hex(n.side.fault_function), hex(n.side.fault_address))
    # the first fault is kept
    n = fresh()
    n.worker = WORKER(lambda _, c: -1)
    n.side.call = n.worker
    N.em_area01_side_001CB480(C.byref(n.side), PLAYER)
    first = (n.side.fault, n.side.fault_function, n.side.fault_address)
    assert first == (2, 0x1CB480, 0x1D2910), first
    assert N.em_area01_side_001F0290(C.byref(n.side)) == -1
    assert (n.side.fault, n.side.fault_function, n.side.fault_address) == first


# ======================================================================
# B. Existing translations of other side-only rows
# ======================================================================

EXISTING_ROOT = Path(os.environ.get('EM_AREA01_SIDE_EXISTING_ROOT', str(ROOT))).resolve()


class LiveActor(C.Structure):
    _fields_ = [('bytes', C.c_uint8 * 0x320), ('link_owner', C.c_void_p), ('link_prev', C.c_void_p),
                ('link_flags', C.c_uint8), ('link_type', C.c_uint8)]


def build_existing():
    """em_player_stage_workers.c + em_player_floor.c and em_player_reaction.c
    + em_player_fall.c from EXISTING_ROOT, as their own tests build them."""
    OUT.mkdir(parents=True, exist_ok=True)
    ext = 'dylib' if sys.platform == 'darwin' else 'so'
    libs = {}
    for name, sources in (('stage', ['src/game/em_player_stage_workers.c', 'src/game/em_player_floor.c']),
                          ('reaction', ['src/game/em_player_reaction.c', 'src/game/em_player_fall.c'])):
        lib = OUT / f'existing_{name}.{ext}'
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared',
                        '-fPIC', '-Isrc'] + sources + ['-lm', '-o', str(lib)], cwd=EXISTING_ROOT, check=True)
        libs[name] = C.CDLL(str(lib))
    A = P(LiveActor)
    libs['stage'].em_player_0021BC40.argtypes = [A]
    libs['stage'].em_player_0021C350.argtypes = [C.c_void_p, A]
    libs['reaction'].em_player_reaction_0021D600.argtypes = [A]
    return libs


def leaf(ee, fn, raw, extra=()):
    """The original leaf fn(PLAYER) over the captured memory with the record
    bytes `raw` (no calls). Returns (v0, record bytes after)."""
    ee.write(PLAYER, bytes(raw))
    for a, data in extra:
        ee.write(a, data)
    ee.hooks = {}
    ee.r[4] = PLAYER
    ee.r[29] = SP
    ee.r[31] = shared.RETURN
    ee.run(fn)
    return ee.r[2] & MASK, ee.read(PLAYER, 0x320)


def bank_identity():
    """001FCF60 / 001FCF90 / 001FE660 (em_census_standins.c) read the record
    container at *D_0028A49C (its extent: word +0 plus word +8) and the config
    D_00264CF0 (0x18 bytes). tools/test_census_standins_reference.py sweeps
    them over the 'panel' capture's bytes (asserted identical in its
    first-level captures); here the same bytes are asserted identical in
    every AREA01 capture, so that sweep covers the AREA01 data exactly."""
    ref = (DECOMP / 'build/startup-reference/panel/eeMemory.bin').read_bytes()
    bank = u32(ref, 0x28A49C)
    spans = [(bank, u32(ref, bank) + u32(ref, bank + 8)), (0x264CF0, 0x18)]
    for beat in ALL_BEATS:
        ram, _ = image(beat)
        assert u32(ram, 0x28A49C) == bank, (beat, 'D_0028A49C')
        for a, n in spans:
            assert ram[a:a + n] == ref[a:a + n], (beat, 'record bank bytes differ', hex(a))
    return len(ALL_BEATS)


def existing_checks():
    libs = build_existing()
    count = bank_identity()
    ram, spad = image('a01_s3_fire_contact')
    captured = bytearray(ram[PLAYER:PLAYER + 0x320])
    ee = FallEE(ELF, bytearray(ram), bytearray(spad))
    for field, fn, call in ((0x1F0, 0x21BC40, lambda a: libs['stage'].em_player_0021BC40(a)),
                            (0x1F1, 0x21D600, lambda a: libs['reaction'].em_player_reaction_0021D600(a))):
        for v in range(256):
            for d in (captured[0xD], 2, 0):
                raw = bytearray(captured)
                raw[field], raw[0xD] = v, d
                want, _ = leaf(ee, fn, raw)
                actor = LiveActor()
                C.memmove(actor.bytes, bytes(raw), 0x320)
                have = call(C.byref(actor)) & MASK
                assert have == want, ('existing', hex(fn), v, d, have, want)
                count += 1
    specials = (0, 0x80000000, 0x00000001, FB(35.0), FB(35.000004), FB(34.999996), FB(1.0), FB(-1.0),
                FB(100.0), 0x7F800000, 0xFF7FFFFF, 0x7FC00000)
    for health in specials + (u32(captured, 0x220),):
        for damage in specials + (u32(captured, 0x224),):
            for bits in (captured[0x235], 0, 0xFF):
                raw = bytearray(captured)
                struct.pack_into('<II', raw, 0x220, health, damage)
                raw[0x235] = bits
                _, want = leaf(ee, 0x21C350, raw)
                actor = LiveActor()
                C.memmove(actor.bytes, bytes(raw), 0x320)
                assert libs['stage'].em_player_0021C350(None, C.byref(actor)) == 0
                assert bytes(actor.bytes) == want, ('existing 0021C350', hex(health), hex(damage), bits)
                count += 1
    count += reaction_checks(libs['reaction'], captured)
    return count


def reaction_checks(lib, captured):
    """em_player_reaction_0021D800 over the AREA01 fire-contact record through
    tools/test_player_reaction_reference.py's oracle, workers and compare
    (every call, every actor byte at every call, the scene bytes, the
    scratchpad words)."""
    import test_player_reaction_reference as RX
    S_, W_ = P(RX.Scene), P(RX.Workers)
    lib.em_player_reaction_0021D800.argtypes = [P(RX.LiveActor), S_, W_]
    RX.ELF, RX.NATIVE = ELF, lib
    RX.EE_ = RX.ReactionEE(ELF)
    n = 0
    for seed in range(RM.pick(240, 60)):
        rng = random.Random(0x5D800 + seed)
        raw = bytearray(captured)
        raw[6] = (0, 1, 2)[seed % 3]
        raw[0x1F1] = rng.choice([captured[0x1F1], 1, 2, 3, 4])
        if rng.randrange(2):
            struct.pack_into('<I', raw, 0x200, rng.choice([0, 0x1000, 0x9000]))
        raw[0x23A] = rng.choice([captured[0x23A], 0x5D])
        raw[0x236] = rng.choice([captured[0x236], 0, 1])
        raw[0x234] = rng.choice([captured[0x234], 0, 1])
        raw[0x319] = rng.choice([captured[0x319], 1])
        raw[5] = rng.choice([captured[5], 0x17])
        scene, flag = RX.random_scene(rng)
        script = RX.Script(rng)
        RX.load_case(RX.EE_, raw, scene, flag)
        original = RX.Original(RX.EE_, script.copy())
        native = RX.Native(script.copy(), flag)
        RX.load_spad(RX.EE_, native, RX.case_spad(('area01 side', seed)))
        RX.EE_.call(0x21D800, (RX.ACTOR,))
        actor = RX.LiveActor()
        C.memmove(actor.bytes, bytes(raw), 0x320)
        native.bind(actor, scene)
        result = lib.em_player_reaction_0021D800(C.byref(actor), C.byref(scene), C.byref(native.workers))
        assert result == 0, ('0021D800', seed, result)
        RX.compare(RX.EE_, actor, scene, flag, ('area01 0021D800', seed), original, native)
        n += 1
    return n


# ======================================================================
# Main
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


def main():
    global ELF, NATIVE, ELF_RAM, BRANCH_PCS
    t0 = time.time()
    ELF = read_elf()
    probe = SideEE(ELF)
    ELF_RAM = bytes(probe.mem[:0x300000])
    BRANCH_PCS = branch_pcs()
    NATIVE = build_native()
    # every direct call target of the translated routines has a policy
    for lo, hi in FUNC_RANGES:
        for pc in range(lo, hi, 4):
            word = struct.unpack_from('<I', ELF_RAM, pc)[0]
            if word >> 26 == 3:
                target = (word & 0x3FFFFFF) << 2
                assert target in CALLEES or target in FUNCS, ('call without a policy', hex(pc), hex(target))
    for beat in ALL_BEATS:
        image(beat)
    base, fx = all_cases()
    only = [p for p in os.environ.get('EM_AREA01_SIDE_ONLY', '').split(',') if p]
    if only:
        base = [c for c in base if c['name'].startswith(tuple(only))]
        fx = [c for c in fx if c['name'].startswith(tuple(only))]
    # pinned into the default run: the captured records, the edges, and the
    # cases that killed a mutant only the full run caught (docs/AREA01_SIDE.md
    # section 5)
    pins = ('captured', ' t key ', ' edge ', '1AF7C0 t count 1 ', '1F0290 t ')
    keep = lambda i, c: any(p in c['name'] for p in pins)      # noqa: E731
    run_base = RM.select(base, 270, 0xA01, axes=(lambda c: c['fn'],), keep=keep)
    run_fx = RM.select(fx, 90, 0xA02, axes=(lambda c: (c['fn'], c['fx']),),
                       keep=lambda i, c: ' t key ' in c['name'] and c['fx'] == 'after')
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
    existing = 0 if only or os.environ.get('EM_AREA01_SIDE_EXISTING') == '0' else existing_checks()
    RM.banner(RM.part(len(run_base), len(base), 'cases'), RM.part(len(run_fx), len(fx), 'fx cases'),
              f'{calls} compared calls', f'{fx_bytes} fx field changes',
              f'{len(BRANCH_PCS)} branches both ways', f'{existing} existing-translation checks')
    print('area01 side reference: PASS (%.1f s wall)' % (time.time() - t0))


if __name__ == '__main__':
    main()
