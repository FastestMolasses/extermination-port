#!/usr/bin/env python3
"""Execute the original AREA01 lane-EXITB routines and compare em_area01_exitb.c.

docs/AREA01_EXITB.md. The user's pinned ELF and the captured AREA01 exit
RAM (../Extermination/build/s87/route_a01/a01_07_level_exit/, the end-of-beat
image with AREA00 resident) supply every instruction and every table; none
are embedded here.

The oracle is FallEE (tools/test_player_fall_reference.py: every COP1 and
VU0 macro op through tools/ee_float_model.py, the measured model) running
the ORIGINAL routine over a copy of the capture. The native module runs over
another copy of the same bytes. Every call leaving the translated set is
caught on both sides and handled by one Python policy:
  run   the original callee runs (vector / matrix leaves, the math leaves,
        the LCG, the packet allocators): in the oracle inside the same
        interpreter, on the native side in a second interpreter bound to
        the native module's own memory;
  stub  the callee is recorded and returns a scripted value (in the 'fx'
        cases it also changes memory, identically on both sides).

Lockstep (the lane-SYS design, docs/AREA01_SYS.md section 4). The original
runs first and records, at every call leaving the set, the callee entry and
every RAM / scratchpad line it stored to since the previous call. The native
module is built with its store trace (EM_AREA01_EXITB_STORE_TRACE) and, at
the same call, must match: the callee, the stack pointer, every argument
register the callee reads (64-bit images), the float argument registers,
every register the native says it sets (they must hold the original's
values), and all of RAM and the scratchpad (only lines either side stored to
since the last check can differ, so exactly those are compared). The same
memory check runs after the last store, then all 32 MiB, the scratchpad and
the result.

Variants of every case (docs/AREA01_EXITB.md section 4):
  fx after / fx before  (lane SYS's two kinds) at the k-th stub every field
        the translated routines load after it (resp. loaded since the
        previous stub) changes, identically on both sides: a value cached
        across the stub, or a read deferred across it, differs;
  poison  every byte the routines store before loading it starts as the
        complement of its final value (a missing store differs);
  prestore  at each stub the bytes stored after it are set to the
        complement of what is stored (a store left out after a call);
  edge-byte / edge-top  the byte after every byte / halfword access is 0xA5
        (a wider access differs) / the top bit of every byte / halfword
        load is flipped (a sign change differs);
  handler  001B17A0 moves the +0x4C handler (a handler loaded too early).
Pass 1 records the loads / stores, pass 2 applies the changes in the
original and logs the exact bytes, and the native run writes the same bytes
at the same stub.

Register reads: RegScan (imported unchanged from test_area01_sys_reference)
measures what every callee reads before writing; the test fails when a
callee reads an argument register its policy does not compare. Every direct
call target of the translated routines must have a policy. In
EM_TEST_FULL=1 both outcomes of every conditional branch in the translated
routines must be taken, except the ones listed in UNREACHABLE with proof.

EM_TEST_FULL=1 runs every case and variant; the default run is the case
or variant that killed each mutant of the lane's mutation sweep
(QUICK_PINNED), the targeted cases (handler, sweep 1, review 2), the first
captured record of each routine and a branch cover (QUICK_BRANCH). EM_AREA01_EXITB_SOURCE=<file> tests another copy of the module
source (mutation runs); EM_AREA01_EXITB_ONLY=<hex,...> runs only the cases
of those entries; EM_AREA01_EXITB_FAILFAST=1 runs serially and stops at the
first difference. EM_TEST_JOBS sets the worker processes (default 2: the
default run is then about 6 s of CPU, 4 s wall).
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
os.environ.setdefault('EM_TEST_JOBS', '2')
import reference_mode as RM  # noqa: E402
import area01_reference_view as AV  # noqa: E402
import test_player_slide_reference as shared  # noqa: E402
from test_player_slide_reference import read_elf, sx32  # noqa: E402
from test_player_fall_reference import FallEE  # noqa: E402

MASK, MASK64 = 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF
DECOMP = ROOT.parent / 'Extermination'
ROUTE = DECOMP / 'build/s87/route_a01'
EXIT = 'a01_07_level_exit'
OUT = ROOT / 'build' / 'area01' / 'exitb'
STACK_BASE, STACK_SIZE = 0x7F000000, 0x100000
SP = shared.STACK_TOP
SCRATCH = 0x01E00000            # zero in the exit capture; test records live here
POOL_HEAD = 0x275BC0
CODE_END = 0x230000

# Translated routines (address -> size in bytes).
FUNCS = {
    0x156F30: 0x430, 0x1576E0: 0x180, 0x1581A0: 0x13C, 0x158810: 0x3B4, 0x158BD0: 0x154,
    0x15AB00: 0xF4, 0x15B030: 0xF8, 0x1BB520: 0x38, 0x1C2430: 0x94, 0x1C2540: 0x98,
    0x1C3DB0: 0x2FC, 0x1C6160: 0x2C, 0x1D0400: 0xAC, 0x1E8E80: 0x3F4, 0x1E9280: 0x2F4,
    0x1E9580: 0x8D8,
}
FUNC_RANGES = tuple((a, a + n) for a, n in FUNCS.items())

# Callee policy: address -> (kind, integer argument registers compared (a0..),
# float argument registers compared (f12..)). Checked against RegScan.
R, S = 'run', 'stub'
CALLEES = {
    # vector / matrix leaves
    0x102948: (R, 2, 0), 0x102958: (R, 2, 0), 0x1026A0: (R, 3, 0), 0x1026D0: (R, 3, 0),
    0x102718: (R, 3, 0), 0x102760: (R, 2, 0), 0x102798: (R, 2, 0), 0x1028B8: (R, 3, 0),
    0x1028D0: (R, 3, 0), 0x102900: (R, 2, 1), 0x1029C0: (R, 1, 0), 0x102A60: (R, 2, 1),
    0x102BB0: (R, 2, 1), 0x103230: (R, 2, 1), 0x121870: (R, 3, 0),
    # math leaves, the LCG, float -> int
    0x11DE90: (R, 0, 1), 0x11E2A8: (R, 0, 1), 0x11E398: (R, 0, 1), 0x11E620: (R, 0, 2),
    0x11E748: (R, 0, 1), 0x122BB8: (R, 0, 0), 0x1281C0: (R, 0, 1),
    # record lookup and packet allocation (pure RAM leaves)
    0x1C6120: (R, 2, 0), 0x1CB5F0: (R, 3, 0), 0x1CB950: (R, 3, 0), 0x1CB6B0: (R, 4, 0),
    0x1CB760: (R, 3, 0),
    # stubs
    0x1B0FD0: (S, 1, 0), 0x1C6380: (S, 1, 0), 0x1B17A0: (S, 1, 0), 0x1AFC10: (S, 1, 0),
    0x1B6F00: (S, 2, 1), 0x1BA1A0: (S, 2, 0), 0x1BA1F0: (S, 1, 0), 0x1F1110: (S, 2, 0),
    0x1F1180: (S, 1, 0), 0x1FB9F0: (S, 4, 0), 0x1C5570: (S, 4, 0), 0x1F4A10: (S, 3, 0),
    0x15AC00: (S, 1, 0), 0x15AE20: (S, 2, 0), 0x1B1190: (S, 1, 0), 0x19AB20: (S, 4, 0),
    0x19B4C0: (S, 4, 0),
}
INDIRECT = (S, 1, 0)            # the +0x4C handler, called with the record
HANDLER, HANDLER2 = 0x1CAA00, 0x1CAA40   # the captured handler; the one 'handler' cases move it to

# Entry signatures: fn -> (argument kinds, output kind). 'f' = a raw float
# register (f12); outputs: None (void), 'i' (int32), 'u' (uint32), 'q' (the
# full 64-bit v0 register the original leaves: the last callee's).
ENTRIES = {
    0x156F30: ('p', None), 0x1576E0: ('pp', 'i'), 0x1581A0: ('p', None), 0x158810: ('p', None),
    0x158BD0: ('p', None), 0x15AB00: ('p', None), 0x15B030: ('p', None), 0x1BB520: ('p', 'q'),
    0x1C2430: ('ppp', 'q'), 0x1C2540: ('pppp', 'q'), 0x1C3DB0: ('pppp', None), 0x1C6160: ('p', 'u'),
    0x1D0400: ('ppf', None), 0x1E8E80: ('ppp', None), 0x1E9280: ('p', None), 0x1E9580: ('ppp', None),
}

REG_FILL, FREG_FILL = 0x01F0A000, 0x449A5000   # mapped RAM (zero) / a normal float
LINE = 64
RAM_LINES = 0x2000000 // LINE
STACK_LO, STACK_HI = 0x7F000000, 0x7F100000


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


# ======================================================================
# Interpreter
# ======================================================================

class ExEE(FallEE):
    """FallEE with: the two MMI word interleaves 00102798 uses (the shared
    core lacks them; checked by a transpose property below), branch-outcome
    recording inside the translated routines, the RAM / scratchpad lines
    stored to (`dirty`, `pre` = each line before its first store), and
    (when `rec` is set) every data load of the translated routines as
    (address, size, value, stub index)."""

    def __init__(self, elf, ram=None, spad=None):
        super().__init__(elf, ram, spad)
        self.outcomes = set()
        self.dirty = set()
        self.pre = None
        self.rec = None
        self.stored = None      # {address: byte} stored by the translated routines (poison pass 1)
        self.store_sites = set()  # (address, size) of those stores
        self.store_log = []       # (address, byte, stub index) of every stored byte, in order
        self.first_load = {}      # address -> position in store_log when first loaded (poison)
        self.stub_index = 0
        self._pc = None

    def mmi(self, word, pc):
        rs, rt, rd = word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        fn, sub = word & 63, word >> 6 & 31
        if fn in (0x08, 0x28) and sub == 0x12:                       # pextlw / pextuw
            full = lambda n: (self.r[n] & MASK64) | (self.rh[n] << 64)
            a, b = full(rs), full(rt)
            w = lambda v, i: v >> (32 * i) & MASK
            base = 0 if fn == 0x08 else 2
            v = w(b, base) | w(a, base) << 32 | w(b, base + 1) << 64 | w(a, base + 1) << 96
            if rd:
                self.r[rd] = v & MASK64
                self.rh[rd] = v >> 64 & MASK64
            return
        super().mmi(word, pc)

    def _where(self, address):
        a = address & MASK
        if 0x2000000 <= a < 0x40000000:
            # outside the 32 MiB (the core would fold it onto RAM; the EE has
            # no memory there): the input is not comparable
            raise AssertionError(('address', 'outside RAM', hex(a)))
        return super()._where(address)

    def load(self, address, size=4):
        v = super().load(address, size)
        if self.rec is not None and self._pc is not None and in_funcs(self._pc):
            a = address & MASK
            if not (STACK_LO <= a < STACK_HI) and not (0x100000 <= a < CODE_END):
                self.rec.append((a, size, v, self.stub_index, self._pc))
                for i in range(size):
                    self.first_load.setdefault(a + i, len(self.store_log))
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
        if self.stored is not None and self._pc is not None and in_funcs(self._pc):
            a = address & MASK
            if not STACK_LO <= a < STACK_HI:
                for i in range(size):
                    self.stored.setdefault(a + i, len(self.store_log))
                    self.store_log.append((a + i, value >> (8 * i) & 0xFF, self.stub_index))
                self.store_sites.add((a, size))
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
    """Run the original callee `fn` (and all it calls, unhooked) with the
    argument registers as they are; every register except v0 / v1 / f0 is
    restored afterwards."""
    saved = (list(ee.r), list(ee.rh), ee.hi, ee.lo, list(ee.f), ee.acc,
             ee.cond, [list(v) for v in ee.vf], list(ee.vacc), ee.q)
    hooks, ee.hooks = ee.hooks, {}
    rec, ee.rec = ee.rec, None
    ee.r[29] = (ee.r[29] - 0x400) & ~15
    ee.r[31] = shared.RETURN
    try:
        ee.run(fn)
    finally:
        ee.hooks = hooks
        ee.rec = rec
    v0, v1, f0 = ee.r[2], ee.r[3], ee.f[0]
    (ee.r, ee.rh, ee.hi, ee.lo, ee.f, ee.acc, ee.cond, ee.vf, ee.vacc, ee.q) = saved
    ee.r[2], ee.r[3], ee.f[0] = v0, v1, f0
    return v0, f0


def stub_reg(v):
    """The v0 register image a stub leaves: a 32-bit script value sign-extended
    (as the stubbed routines' own results are), a wider one taken as the full
    64-bit register (the 'v0 hi' cases)."""
    if -(1 << 31) <= v < (1 << 32):
        return sx32(v) & MASK64
    return v & MASK64


class Script:
    """Scripted stub results: fn -> list of v0 (or (v0, f0)); default 0."""

    def __init__(self, spec):
        self.queues = {fn: list(values) for fn, values in (spec or {}).items()}

    def take(self, fn):
        q = self.queues.get(fn)
        if q:
            v = q.pop(0)
            return v if isinstance(v, tuple) else (v, 0)
        return (0, 0)


def policy(fn, indirect):
    if fn in CALLEES:
        return CALLEES[fn]
    if fn in indirect:
        return INDIRECT
    raise AssertionError(('call without a policy', hex(fn)))


def log_entry(fn, pol, regs, fregs, sp):
    kind, na, nf = pol
    return (hex(fn), hex(sp & MASK), tuple(regs[i] & MASK64 for i in range(na)),
            tuple(fregs[i] & MASK for i in range(nf)))


# ======================================================================
# Native side
# ======================================================================

P, U8, U32, I32, U64 = C.POINTER, C.c_uint8, C.c_uint32, C.c_int32, C.c_uint64
OUT_TYPES = {'i': I32, 'u': U32, 'q': U64}


class Region(C.Structure):
    _fields_ = [('base', U32), ('size', U32), ('bytes', P(U8))]


class Call(C.Structure):
    _fields_ = [('fn', U32), ('sp', U32), ('a', U64 * 8), ('f', U32 * 4), ('na', U32), ('nf', U32),
                ('v0', U64), ('f0', U32)]


WORKER = C.CFUNCTYPE(C.c_int, C.c_void_p, P(Call))


class Exitb(C.Structure):
    _fields_ = [('regions', P(Region)), ('region_count', C.c_uint), ('call', WORKER), ('ctx', C.c_void_p),
                ('sp', U32), ('fault', I32), ('fault_function', U32), ('fault_address', U32), ('view', C.c_void_p)]


TRACE_C = r"""/* Store trace for the native module (test build only): the 64-byte
 * lines of RAM and scratchpad the module stores to since the last reset. */
#include <stdint.h>
#define RAM_LINES (0x2000000u / 64u)
#define LINES (RAM_LINES + 0x4000u / 64u)
static uint8_t dirty[LINES];
uint32_t a01exitb_trace_list[LINES];
uint32_t a01exitb_trace_count;
void a01exitb_trace_store(uint32_t address, unsigned size);
void a01exitb_trace_reset(void);
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
        a01exitb_trace_list[a01exitb_trace_count++] = id;
    }
}
void a01exitb_trace_store(uint32_t address, unsigned size)
{
    mark(address);
    mark(address + size - 1u);
}
void a01exitb_trace_reset(void)
{
    uint32_t i;
    for (i = 0; i < a01exitb_trace_count; i++)
        dirty[a01exitb_trace_list[i]] = 0;
    a01exitb_trace_count = 0;
}
"""
TRACE = {}
NATIVE = None
ELF = None
IMAGES = {}
BRANCH_PCS = set()


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    trace = OUT / 'store_trace.c'
    if not trace.exists() or trace.read_text() != TRACE_C:
        trace.write_text(TRACE_C)
    source = os.environ.get('EM_AREA01_EXITB_SOURCE', '')
    ext = 'dylib' if sys.platform == 'darwin' else 'so'
    if source:
        source = str(Path(source).resolve())
        lib = OUT / f'area01_exitb_{Path(source).stem}.{ext}'
        rebuild = not lib.exists() or Path(source).stat().st_mtime > lib.stat().st_mtime
    else:
        source = 'src/game/em_area01_exitb.c'
        lib = OUT / f'area01_exitb.{ext}'
        deps = [ROOT / source, ROOT / 'src/game/em_area01_exitb.h', ROOT / 'src/game/em_ee_float.h', trace,
                Path(__file__)]
        rebuild = not lib.exists() or max(d.stat().st_mtime for d in deps) > lib.stat().st_mtime
    if rebuild:
        # a mutant (EM_AREA01_EXITB_SOURCE) may leave a warning (an unused
        # variable after a deleted line): it is built without -Werror
        werror = [] if os.environ.get('EM_AREA01_EXITB_SOURCE') else ['-Werror']
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', *werror, '-Wpedantic',
                        '-ffp-contract=off', '-shared', '-fPIC', '-Isrc', '-Isrc/game',
                        '-DEM_AREA01_EXITB_STORE_TRACE=a01exitb_trace_store', source, str(trace),
                        '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    TRACE['count'] = U32.in_dll(native, 'a01exitb_trace_count')
    TRACE['list'] = (U32 * (RAM_LINES + 0x4000 // LINE)).in_dll(native, 'a01exitb_trace_list')
    TRACE['reset'] = native.a01exitb_trace_reset
    for fn, (kinds, outk) in ENTRIES.items():
        f = getattr(native, f'em_area01_exitb_{fn:08X}')
        f.restype = C.c_int
        f.argtypes = [P(Exitb)] + [U32] * len(kinds) + ([P(OUT_TYPES[outk])] if outk else [])
    native.em_area01_exitb_clear_fault.argtypes = [P(Exitb)]
    return native


def native_lines():
    n = TRACE['count'].value
    if not n:
        return ()
    out = TRACE['list'][:n]
    TRACE['reset']()
    return out


class Lockstep:
    """The oracle's side of the per-call memory check (lane SYS)."""

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
    """One native call over copies of `ram` / `spad`."""

    def __init__(self, ram, spad, script, indirect):
        self.ram = (U8 * len(ram)).from_buffer_copy(ram)
        self.spad = (U8 * len(spad)).from_buffer_copy(spad)
        self.regions = (Region * 2)(Region(0, len(ram), C.cast(self.ram, P(U8))),
                                    Region(0x70000000, len(spad), C.cast(self.spad, P(U8))))
        self.ee = ExEE(ELF, b'', b'')
        self.ee.mem = memoryview(self.ram).cast('B')
        self.ee.spad = memoryview(self.spad).cast('B')
        self.script, self.indirect, self.log, self.error = script, indirect, [], None
        self.lock = None
        self.fx = {}
        self.stubs = 0
        self.worker = WORKER(self._call)
        self.sys = Exitb(self.regions, 2, self.worker, None, SP, 0, 0, 0)

    def dirty(self):
        out = set(native_lines())
        out |= self.ee.dirty
        self.ee.dirty = set()
        return out

    def _call(self, _, pc):
        try:
            c = pc.contents
            pol = policy(c.fn, self.indirect)
            kind, na, nf = pol
            assert na <= c.na <= 8 and nf <= c.nf <= 4, ('native register counts', hex(c.fn), c.na, c.nf)
            k = len(self.log)
            regs = [c.a[i] for i in range(8)]
            entry = log_entry(c.fn, pol, regs, list(c.f), c.sp)
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
                v0, f0 = self.script.take(c.fn)
                v0 = stub_reg(v0)
                for address, data in self.fx.get(self.stubs, ()):
                    for i, b in enumerate(data):
                        self.ee.save(address + i, b, 1)
                self.stubs += 1
            c.v0 = v0 & MASK64
            c.f0 = f0 & MASK
            return 0
        except Exception as e:  # noqa: BLE001 - reported after the call returns
            if self.error is None:
                self.error = e
            return -1


def native_call(fn, sysp, args, fargs):
    kinds, outk = ENTRIES[fn]
    vals, fi = [], 0
    for i, k in enumerate(kinds):
        if k == 'f':
            vals.append(fargs[fi] & MASK)
            fi += 1
        else:
            vals.append(args[i] & MASK)
    out = OUT_TYPES[outk](0x5A5A5A5A5A5A5A5A if outk == 'q' else 0x5A5A5A5A) if outk else None
    rc = getattr(NATIVE, f'em_area01_exitb_{fn:08X}')(sysp, *vals, *([C.byref(out)] if outk else []))
    return rc, (out.value & (MASK64 if outk == 'q' else MASK) if out is not None else None)


# ======================================================================
# Oracle side
# ======================================================================

def oracle_run(case, ram, spad, on_stub=None, rec=False, stored=None):
    """The original over `ram` / `spad` (used in place). Returns (ee, log,
    v0, entries, final delta). on_stub(ee, k) runs after the k-th stubbed
    call has set its result (the fx side effects)."""
    ee = ExEE(ELF, b'', b'')
    ee.mem, ee.spad = ram, spad
    ee.pre = {}
    ee.rec = [] if rec else None
    ee.stored = stored
    script, log, entries = Script(case['script']), [], []
    indirect = set(case.get('indirect', ()))
    stubs = [0]

    def make(fn):
        pol = policy(fn, indirect)

        def hook(e):
            regs = [e.r[4 + i] for i in range(8)]
            entry = log_entry(fn, pol, regs, [e.f[12 + i] for i in range(4)], e.r[29])
            log.append(entry)
            entries.append((entry, e.take(), tuple(r & MASK64 for r in regs),
                            tuple(e.f[12 + i] & MASK for i in range(4))))
            if pol[0] == R:
                run_nested(e, fn)
            else:
                v0, f0 = script.take(fn)
                e.r[2] = stub_reg(v0)
                e.f[0] = f0 & MASK
                e.stub_index = stubs[0] + 1
                if on_stub is not None:
                    on_stub(e, stubs[0], fn)
                stubs[0] += 1
        return hook
    ee.hooks = {fn: make(fn) for fn in set(CALLEES) | indirect}
    for i in range(1, 32):
        if i not in (28, 29, 31):
            ee.r[i] = REG_FILL + i * 0x40
    for i in range(32):
        ee.f[i] = FREG_FILL + i
    kinds = ENTRIES[case['fn']][0]
    for i, v in enumerate(case['args']):
        if kinds[i] != 'f':
            ee.r[4 + i] = sx32(v) & MASK64
    for i, v in enumerate(case.get('fargs', ())):
        ee.f[12 + i] = v & MASK
    ee.r[29] = SP
    ee.r[31] = shared.RETURN
    ee.run(case['fn'])
    return ee, log, ee.r[2] & MASK64, entries, ee.take()


def image(beat):
    if beat not in IMAGES:
        d = ROUTE / beat
        IMAGES[beat] = ((d / 'eeMemory.bin').read_bytes(), (d / 'scratchpad.bin').read_bytes())
    return IMAGES[beat]


def prepared(case):
    ram, spad = image(case['beat'])
    ram, spad = bytearray(ram), bytearray(spad)
    for a, data in case['writes']:
        if 0x70000000 <= a < 0x70004000:
            spad[a - 0x70000000:a - 0x70000000 + len(data)] = data
        else:
            ram[a:a + len(data)] = data
    return ram, spad


# ---- stub side effects ------------------------------------------------

def is_code_word(size, v):
    return size == 4 and 0x100000 <= v < CODE_END


def poison_writes(case):
    """'poison' variants: every byte the translated routines store before
    they (or anything they read through) load it starts as the complement of
    the value it ends with, so a missing store, or a store of a value that
    only happened to be there already, differs. Bytes they load before
    storing keep their value (poisoning them would change the path)."""
    ram, spad = prepared(case)
    stored = {}
    run = oracle_run(case, ram, spad, rec=True, stored=stored)[0]
    out = []
    for a in sorted(stored):
        if run.first_load.get(a, 1 << 62) <= stored[a]:
            continue
        b = run.read(a, 1)[0] ^ 0xFF
        out.append((a, bytes([b])))
    return out


def prestore_plan(case):
    """'prestore' variants: {stub index k: [(address, bytes)]}: at the k-th
    stub every byte the translated routines store after it (before the next
    stub) is set to the complement of the value they store there, so a store
    left out after a stubbed call (where the callee could have changed the
    byte) differs."""
    ram, spad = prepared(case)
    stored = {}
    run = oracle_run(case, ram, spad, rec=True, stored=stored)[0]
    plan, seen = {}, set()
    for a, b, idx in run.store_log:
        if idx == 0 or (a, idx) in seen:
            continue
        seen.add((a, idx))
        plan.setdefault(idx - 1, []).append((a, bytes([b ^ 0xFF])))
    return plan


def edge_writes(case, top):
    """'edge' variants: the byte after every byte / halfword the translated
    routines load or store (when they access that byte in no other way) set
    to 0xA5, so a wider access than the original's differs. 'top' variants:
    the top bit of every byte / halfword they load flipped in the starting
    memory, so a signed read where the original reads unsigned (or the
    reverse) differs."""
    ram, spad = prepared(case)
    stored = {}
    ee = oracle_run(case, ram, spad, rec=True, stored=stored)[0]
    loads = [(a, n) for a, n, v, idx, pc in ee.rec]
    touched = set(stored)
    for a, n in loads:
        touched.update(range(a, a + n))
    out = {}
    if not top:
        for a, n in sorted(ee.store_sites) + loads:
            if n in (1, 2) and a + n not in touched:
                out[a + n] = 0xA5
    else:
        ram0, spad0 = prepared(case)
        for a, n in loads:
            if n in (1, 2) and a + n - 1 not in out:
                t = a + n - 1
                b = spad0[t - 0x70000000] if t >= 0x70000000 else ram0[t]
                out[t] = b ^ 0x80
    return [(a, bytes([v])) for a, v in sorted(out.items())]


def fx_plan(case, before):
    """{stub index k: [(address, size)]}: 'after' = the fields the translated
    routines load after the k-th stub and before the next; 'before' = the
    fields they loaded since the previous stub (before the k-th)."""
    ram, spad = prepared(case)
    ee = oracle_run(case, ram, spad, rec=True)[0]
    plan = {}
    for a, size, v, idx, pc in ee.rec:
        k = idx - 1 if not before else idx
        if k < 0 or is_code_word(size, v):
            continue
        items = plan.setdefault(k, [])
        if (a, size) not in items:
            items.append((a, size))
    return plan


class Perturb:
    """on_stub for the oracle's fx run: changes the planned fields and logs
    the exact bytes (address, bytes) written at each stub index."""

    def __init__(self, plan, before):
        self.plan, self.before, self.log, self.count = plan, before, {}, {}

    def __call__(self, ee, k, fn):
        for a, size in self.plan.get(k, ()):
            v = int.from_bytes(ee.read(a, size), 'little')
            if is_code_word(size, v):
                continue
            n = self.count.get(a, 0)
            self.count[a] = n + 1
            if not self.before:
                flip = 1 if size < 4 else 0x10
            elif size == 1:
                flip = 1 << (n % 8)
            elif size == 2:
                flip = (1 << (n % 8)) | (1 << (8 + n % 8))
            else:
                flip = (1 << (4 + n % 12)) | (1 << (16 + n % 12))
            v ^= flip
            data = v.to_bytes(size, 'little')
            for i, b in enumerate(data):
                ee.save(a + i, b, 1)
            self.log.setdefault(k, []).append((a, data))


class Fixed:
    """on_stub writing planned bytes at given stub indices (logged)."""

    def __init__(self, plan):
        self.plan, self.log = plan, {}

    def __call__(self, ee, k, fn):
        for a, data in self.plan.get(k, ()):
            for i, b in enumerate(data):
                ee.save(a + i, b, 1)
            self.log.setdefault(k, []).append((a, data))


class AtCallee:
    """on_stub for the 'handler' cases: at the first stubbed call of
    `callee`, write the given bytes (logged for the native side)."""

    def __init__(self, callee, writes):
        self.callee, self.writes, self.log, self.done = callee, writes, {}, False

    def __call__(self, ee, k, fn):
        if fn != self.callee or self.done:
            return
        self.done = True
        for a, data in self.writes:
            for i, b in enumerate(data):
                ee.save(a + i, b, 1)
            self.log.setdefault(k, []).append((a, data))


# ---- one case ----------------------------------------------------------

def run_case(case):
    """Returns (branch outcomes, calls). Raises on any difference."""
    fx = None
    if case.get('poison'):
        case = dict(case, writes=case['writes'] + poison_writes(dict(case, poison=False)))
    if case.get('edge'):
        case = dict(case, writes=case['writes'] + edge_writes(dict(case, edge=None), case['edge'] == 'top'))
    if case.get('fx'):
        fx = Perturb(fx_plan(case, case['fx'] == 'before'), case['fx'] == 'before')
    elif case.get('at_callee'):
        fx = AtCallee(case['at_callee'][0], case['at_callee'][1])
    elif case.get('prestore'):
        fx = Fixed(prestore_plan(dict(case, prestore=False)))
    ram, spad = prepared(case)
    nat = NativeRun(ram, spad, Script(case['script']), set(case.get('indirect', ())))
    if AV.ENABLED:
        nat.canonical_view = AV.CanonicalView(nat.regions)
        nat.canonical_view.install(nat.sys)
    native_lines()
    where = (case['name'],)
    try:
        ee, olog, ov0, entries, last = oracle_run(case, ram, spad, on_stub=fx)
    except AssertionError as e:
        if (case.get('fx') or case.get('edge')) and e.args and isinstance(e.args[0], tuple) and e.args[0][:1] == ('address',):
            # the variant sent the original outside the 32 MiB: not comparable
            return None
        raise AssertionError((where, 'the original run stopped', e.args)) from e
    if fx is not None:
        nat.fx = fx.log
    nat.lock = Lockstep(entries, ee.pre, ee.mem, ee.spad)
    rc, nv0 = native_call(case['fn'], C.byref(nat.sys), case['args'], case.get('fargs', ()))
    if nat.error is not None:
        raise AssertionError((where, 'native worker error', repr(nat.error)))
    assert rc == 0, (where, 'native faulted', rc, nat.sys.fault, hex(nat.sys.fault_function),
                     hex(nat.sys.fault_address))
    assert len(nat.log) == len(entries), (where, 'native makes fewer calls than the original', len(nat.log),
                                          len(entries), entries[len(nat.log)][0] if len(nat.log) < len(entries)
                                          else ())
    try:
        nat.lock.check(('after the last store',), last, nat.dirty(), nat.ee.mem, nat.ee.spad)
    except AssertionError as e:
        raise AssertionError((where,) + e.args) from e
    outk = ENTRIES[case['fn']][1]
    if outk:
        want = ov0 if outk == 'q' else ov0 & MASK
        assert nv0 == want, (where, 'result', hex(nv0), hex(want))
    assert olog == nat.log, (where, 'call log differs')
    if bytes(nat.ram) != bytes(ee.mem):
        a = next(i for i in range(len(ee.mem)) if nat.ram[i] != ee.mem[i])
        raise AssertionError((where, 'RAM differs at', hex(a)))
    if bytes(nat.spad) != bytes(ee.spad):
        a = next(i for i in range(len(ee.spad)) if nat.spad[i] != ee.spad[i])
        raise AssertionError((where, 'scratchpad differs at', hex(0x70000000 + a)))
    return ee.outcomes, len(olog), sum(len(v) for v in fx.log.values()) if fx else 0


def run_case_safe(case):
    try:
        return ('ok', run_case(case))
    except AssertionError as e:
        return ('fail', (case['name'], str(e.args)[:1500]))


# ======================================================================
# Cases
# ======================================================================

def u32(ram, a):
    return struct.unpack_from('<I', ram, a & 0x1FFFFFF)[0]


def pool(ram):
    a, seen, out = u32(ram, POOL_HEAD), set(), []
    while a and a not in seen and len(out) < 0x200:
        seen.add(a)
        out.append((a, u32(ram, a + 0x10)))
        a = u32(ram, a + 0x1C)
    return out


def owners(fn, beat=EXIT):
    return [a for a, cb in pool(image(beat)[0]) if cb == fn]


def W8(a, v): return (a, struct.pack('<B', v & 0xFF))
def W16(a, v): return (a, struct.pack('<H', v & 0xFFFF))
def W32(a, v): return (a, struct.pack('<I', v & MASK))
def WF(a, v): return (a, struct.pack('<f', v))
def FB(v): return struct.unpack('<I', struct.pack('<f', v))[0]


def case(name, fn, args, script=None, writes=(), fargs=(), beat=EXIT, pin=False):
    indirect = set()
    if fn in OWNERS:
        word = bytearray(image(beat)[0][args[0] + 0x4C:args[0] + 0x50])
        for a, data in writes:
            for i, b in enumerate(data):
                if 0 <= a + i - (args[0] + 0x4C) < 4:
                    word[a + i - (args[0] + 0x4C)] = b
        indirect.add(struct.unpack('<I', word)[0])
    indirect |= {HANDLER, HANDLER2}
    return dict(name=name, fn=fn, beat=beat, args=list(args), script=script or {}, writes=list(writes),
                fargs=list(fargs), indirect=sorted(indirect), pin=pin)


OWNERS = {0x156F30, 0x1581A0, 0x158810, 0x158BD0, 0x15AB00, 0x15B030}
LCG_A, LCG_C = 1103515245, 12345


def seed_write(seed):
    return W32(u32(image(EXIT)[0], 0x24295C) + 0x58, seed)


def vec(rng, lo=-50.0, hi=50.0):
    return [rng.uniform(lo, hi) for _ in range(3)] + [1.0]


def vwrites(a, v):
    return [WF(a + 4 * i, x) for i, x in enumerate(v)]


def cases_owners(rng):
    out = []
    # 00156F30: the three captured records, then every state and branch
    for n, p in enumerate(owners(0x156F30)):
        out.append(case(f'156F30 captured {n}', 0x156F30, [p], pin=n == 0))
    p = owners(0x156F30)[0]
    for st in (2, 3, 5, 0xFF):
        out.append(case(f'156F30 s{st}', 0x156F30, [p], writes=[W8(p + 4, st)], pin=True))
    for v in (0, 1, -1):
        out.append(case(f'156F30 s0 settle {v}', 0x156F30, [p], script={0x1B0FD0: [v]}, writes=[W8(p + 4, 0)],
                        pin=True))
    for n in range(8):
        tgt = [rng.uniform(-100, 100), rng.uniform(-10, 10), rng.uniform(-100, 100)]
        w = [W8(p + 4, 4), W16(p + 0x36, rng.choice((1, -1, 0x100))), WF(p + 0xC4, rng.uniform(-4, 4))]
        w += [WF(0x810350 + 4 * i, x) for i, x in enumerate(tgt)]
        out.append(case(f'156F30 s4 restart {n}', 0x156F30, [p], writes=w, pin=n < 2))
    angles = [(3.1, 0.08), (3.14, 0.08), (-3.1, -0.08), (-3.14, -0.08), (0.5, 0.08), (3.14159, 0.0),
              (-3.14159, 0.0), (3.1415925, 0.0000002), (0.0, 0.0)]
    for n, (ang, step) in enumerate(angles):
        for timer in (0.1, 0.0005, 0.001, 0.0):
            for trig in (0, 1):
                w = [W8(p + 4, 1), WF(p + 0x2E4, ang), WF(p + 0x2E0, step), WF(p + 0x2E8, timer),
                     W16(p + 0x36, trig), WF(p + 0xC4, rng.uniform(-3, 3))]
                out.append(case(f'156F30 s1 {n} {timer} {trig}', 0x156F30, [p], writes=w,
                                pin=(n in (0, 2, 4) and trig == (timer == 0.0005))))
    for n in range(6):
        w = [W8(p + 4, 1), WF(p + 0x2E4, rng.uniform(-3.3, 3.3)), WF(p + 0x2E0, rng.uniform(-0.2, 0.2)),
             WF(p + 0x2E8, rng.uniform(-0.01, 0.3)), W16(p + 0x36, rng.choice((0, 1)))]
        out.append(case(f'156F30 s1 random {n}', 0x156F30, [p], writes=w))
    # 001581A0
    p = owners(0x1581A0)[0]
    out.append(case('1581A0 captured', 0x1581A0, [p], pin=True))
    for st in (2, 3, 4, 0xFF):
        out.append(case(f'1581A0 s{st}', 0x1581A0, [p], writes=[W8(p + 4, st)], pin=True))
    for area in (0, 1, 5):
        for bit in (0, 2, 7, 8, 33, 0x107):
            for have in (0, 1):
                mask = (1 << (bit & 31)) & 0xFF
                w = [W8(p + 4, 0), W8(0x810700, area), W16(p + 0x2E, bit),
                     W8(0x810841 + area, (mask if have else 0) | (0x40 if bit != 6 else 0))]
                out.append(case(f'1581A0 s0 a{area} b{bit} {have}', 0x1581A0, [p], writes=w,
                                pin=area == 1 and bit in (2, 33)))
    for bit in (0, 3, 7, 9, 0x21):
        for trig in (0, 1, -2):
            w = [W8(p + 4, 1), W16(p + 0x36, trig), W8(p + 0x2E, bit), W8(0x810700, 2),
                 W8(0x810842 + 1, 0x11)]
            out.append(case(f'1581A0 s1 b{bit} t{trig}', 0x1581A0, [p], writes=w, pin=bit in (3, 9)))
    # 00158810
    p = owners(0x158810)[0]
    out.append(case('158810 captured', 0x158810, [p], pin=True))
    for st in (3, 4, 0xFF):
        out.append(case(f'158810 s{st}', 0x158810, [p], writes=[W8(p + 4, st)], pin=True))
    for have in (0, 1):
        for typ in (0x2F, 0x13, 0x12):
            for c87, r6a in ((0, 0), (1, 0), (1, 1)):
                w = [W8(p + 4, 0), W8(p + 3, typ), W16(p + 0x2E, 3), W8(0x810841, 8 if have else 0x10),
                     W8(0x810C87, c87), W8(0x81076A, r6a)]
                out.append(case(f'158810 s0 h{have} t{typ:x} {c87}{r6a}', 0x158810, [p], writes=w,
                                script={0x1C5570: [rng.choice((0, 0x7BD420, -5))]},
                                pin=typ == 0x2F or (have and typ == 0x13)))
    for sub in range(6):
        for typ in (0x2F, 0x13):
            for bits in (0, 4, 0xFF):
                for c87, r6a in ((1, 0), (0, 0), (1, 1)):
                    for res in (0, 1):
                        for q in (0, 1):
                            if sub not in (1, 3) and res:
                                continue
                            if sub != 2 and q:
                                continue
                            if sub not in (0,) and (bits, c87, r6a) != (4, 1, 0):
                                continue
                            w = [W8(p + 4, 1), W8(p + 5, sub), W8(p + 3, typ), W8(p + 0x0B, bits),
                                 W8(0x810C87, c87), W8(0x810C88, c87 ^ 1), W8(0x81076A, r6a)]
                            if sub == 2:
                                w.append(W32(p + 0x20, SCRATCH + 0x100 if q else 0))
                            out.append(case(f'158810 s1 sub{sub} t{typ:x} b{bits} {c87}{r6a} r{res} q{q}',
                                            0x158810, [p], writes=w, script={0x1BA1F0: [res]},
                                            pin=sub in (1, 2, 3, 5) or (sub == 0 and bits == 4)))
    for sub in range(5):
        for h, b in ((0, 0), (1, 0), (0x64, 0), (0x64, 3), (0x77, 1), (0x77, 2), (0x78, 0), (0x100, 0)):
            for bits in ((0, 4) if sub == 0 else (4,)):
                if sub not in (1, 2) and (h, b) != (0, 0):
                    continue
                w = [W8(p + 4, 2), W8(p + 5, sub), W16(0x70003B84, h), W8(0x70003B91, b), W8(p + 0x0B, bits),
                     W8(p + 0x2E, rng.choice((1, 6, 9))), W8(0x810700, 3), W8(0x810844, 0x21)]
                out.append(case(f'158810 s2 sub{sub} h{h:x} b{b} {bits}', 0x158810, [p], writes=w,
                                pin=sub in (1, 2) and h in (0, 1, 0x64, 0x77, 0x78) or sub == 0))
    # 00158BD0
    p = owners(0x158BD0)[0]
    out.append(case('158BD0 captured', 0x158BD0, [p], pin=True))
    for st in (0, 2, 3, 4):
        out.append(case(f'158BD0 s{st}', 0x158BD0, [p], writes=[W8(p + 4, st)], pin=True))
    for res in (0, 1, -1):
        for bit, have in ((4, 1), (4, 0), (36, 1), (9, 1)):
            w = [W8(p + 4, 1), W16(p + 0x2E, bit), W8(0x810700, 4), W8(0x810845, (1 << (bit & 31)) & 0xFF
                                                                     if have else 0xEF)]
            out.append(case(f'158BD0 s1 r{res} b{bit} {have}', 0x158BD0, [p], writes=w,
                            script={0x1B17A0: [res]}, pin=res != -1))
    # 0015AB00 (state 1 runs 001E9280, state 0 runs 001E8E80)
    for n, p in enumerate(owners(0x15AB00)):
        out.append(case(f'15AB00 captured {n}', 0x15AB00, [p], pin=n in (0, 7)))
    p = owners(0x15AB00)[3]
    for st in (2, 3, 4, 0xFF):
        out.append(case(f'15AB00 s{st}', 0x15AB00, [p], writes=[W8(p + 4, st)], pin=True))
    for n, (idx, cell) in enumerate(((0, 0), (5, 3), (70, 7), (-1, 1), (12, 2))):
        w = [W8(p + 4, 0), W16(p + 0x54, idx), W16(p + 0x0E, cell), seed_write(rng.getrandbits(32))]
        out.append(case(f'15AB00 s0 {n}', 0x15AB00, [p], writes=w, pin=n in (0, 2)))
    # 0015B030
    p = owners(0x15B030)[0]
    q = u32(image(EXIT)[0], p + 0x20)
    out.append(case('15B030 captured', 0x15B030, [p], pin=True))
    for st in (2, 3, 4, 0xFF):
        out.append(case(f'15B030 s{st}', 0x15B030, [p], writes=[W8(p + 4, st)], pin=True))
    for st in (0, 1):
        for qs in (0, 1, 2, 3, 0x80):
            for res in ((0, 1, -1) if st == 0 else (0,)):
                w = [W8(p + 4, st), W8(q + 4, qs)]
                out.append(case(f'15B030 s{st} q{qs} r{res}', 0x15B030, [p], writes=w,
                                script={0x15AC00: [res]}, pin=qs in (1, 2)))
    return out


def cases_leaves(rng):
    out = []
    ram = image(EXIT)[0]
    nodes = [a for a, cb in pool(ram)]
    # 001576E0 directly
    p = owners(0x158810)[0]
    for bits in (0, 4, 0xFB, 0xFF):
        for typ in (0x12, 0x2F, 0x13, 0x00):
            for c87 in (0, 1):
                for c88 in (0, 1):
                    if bits in (0, 0xFB) and (typ, c87, c88) != (0x12, 0, 0):
                        continue
                    w = [W8(p + 0x0B, bits), W8(p + 3, typ), W8(0x810C87, c87), W8(0x810C88, c88)]
                    out.append(case(f'1576E0 b{bits:x} t{typ:x} {c87}{c88}', 0x1576E0, [p, p + 0x1F0],
                                    writes=w, pin=bits == 4 or typ == 0x12))
    # 001BB520
    for n, a in enumerate(nodes[:3] + [owners(0x1581A0)[0]]):
        for h in (0, 0x1234, 0xFF80):
            out.append(case(f'1BB520 {n} {h:x}', 0x1BB520, [a], writes=[W16(a + 0x2E, h)],
                            script={0x1B0FD0: [rng.choice((0, 7, -3))]}, pin=n == 0))
    # 001C2430 / 001C2540: a matrix, vectors and a record in scratch RAM
    for n in range(6):
        node = rng.choice(nodes)
        m, v1, v2 = SCRATCH, SCRATCH + 0x40, SCRATCH + 0x50
        w = []
        for row in range(4):
            w += vwrites(m + 16 * row, vec(rng, -1.5, 1.5))
        w += vwrites(v1, vec(rng)) + vwrites(v2, vec(rng))
        out.append(case(f'1C2430 {n}', 0x1C2430, [node, v1, m], writes=w, script={0x19AB20: [n - 2]}, pin=n < 2))
        out.append(case(f'1C2540 {n}', 0x1C2540, [node, v1, v2, m], writes=w, script={0x19B4C0: [n - 3]},
                        pin=n < 2))
    # 001C3DB0: direction pairs, including parallel / zero ones
    pairs = [([1, 0, 0], [0, 1, 0]), ([1, 0, 0], [1, 0, 0]), ([1, 0, 0], [-1, 0, 0]), ([0, 0, 0], [0, 1, 0]),
             ([0.6, 0, 0.8], [0, 0, 1]), ([-0.0, 0.0, 1.0], [0.0, -0.0, 2.0]), ([0, 1, 0], [0, 0.99999, 0.00447])]
    for n in range(10):
        pairs.append((vec(rng, -1, 1)[:3], vec(rng, -1, 1)[:3]))
    for n, (a, b) in enumerate(pairs):
        a0, a1, a2, a3 = SCRATCH, SCRATCH + 0x10, SCRATCH + 0x20, SCRATCH + 0x30
        w = vwrites(a0, list(a) + [0.0]) + vwrites(a1, list(b) + [0.0]) + vwrites(a2, vec(rng, -2, 2))
        w += vwrites(a3, [9.0, 9.0, 9.0, 9.0])
        out.append(case(f'1C3DB0 {n}', 0x1C3DB0, [a0, a1, a2, a3], writes=w, pin=n < 8))
    # 001C3DB0 in the call shape lane EXITA's owner uses: a1 = 0x70003610
    # and a3 = 0x70003620, next to the routine's own scratch vectors
    shape = random.Random(0x3DB0)      # its own stream: the later cases keep their data
    for n in range(3):
        a0, a2 = SCRATCH, SCRATCH + 0x20
        w = vwrites(a0, vec(shape, -1, 1)[:3] + [0.0]) + vwrites(0x70003610, vec(shape, -1, 1)[:3] + [0.0])
        w += vwrites(a2, vec(shape, -2, 2)) + vwrites(0x70003620, [9.0, 9.0, 9.0, 9.0])
        out.append(case(f'1C3DB0 spad shape {n}', 0x1C3DB0, [a0, 0x70003610, a2, 0x70003620], writes=w, pin=n == 0))
    # 001C6160: records with an animation table
    anim = [a for a in nodes if u32(ram, a + 0x40)][:6]
    for n, a in enumerate(anim):
        out.append(case(f'1C6160 captured {n}', 0x1C6160, [a], pin=n < 2))
        for h in (0, 3, -1, 0x7FFF):
            out.append(case(f'1C6160 {n} h{h}', 0x1C6160, [a], writes=[W16(a + 0x2C, h)], pin=n == 0 and h == 3))
    # 001D0400: 0x90-byte records in scratch RAM
    for n, k in enumerate((1.0, 0.5, -2.0, 0.0, 1e-30, 3.3e38, 1.25)):
        src, dst = SCRATCH + 0x200, SCRATCH
        w = [WF(src + 4 * i, rng.uniform(-100, 100)) for i in range(0x24)]
        w.append(W32(src + 0x80, rng.choice((0, 7, -12, 100000, 0x7FFFFFFF))))
        out.append(case(f'1D0400 {n}', 0x1D0400, [dst, src], fargs=[FB(k)], writes=w, pin=n < 3))
    # 001E8E80 / 001E9280 / 001E9580 directly
    p = owners(0x15AB00)[0]
    for n, cell in enumerate((0, 3, 7)):
        w = vwrites(SCRATCH, vec(rng, 1, 30)) + [seed_write(rng.getrandbits(32))]
        out.append(case(f'1E8E80 {n}', 0x1E8E80, [p, cell, SCRATCH], writes=w, pin=n == 0))
        out.append(case(f'1E9280 {n}', 0x1E9280, [p], writes=[W16(p + 0x0E, cell)], pin=n == 0))
    keys = [0x1, 0x1001, 0x0, 0x200, 0x2, 0x101, 0x601, 0x401, 0x202, 0x600, 0x300, 0x400, 0x1000, 0x1301,
            0x1400, 0x100, 0xD00, 0x700, 0x702, 0x703, 0x803, 0x1300, 0x0B00, 0x0301, 0x1302, 0xFFFF]
    for n, key in enumerate(keys):
        w = vwrites(SCRATCH, vec(rng, 1, 30)) + [seed_write(rng.getrandbits(32)), W8(0x810700, key >> 8),
                                                 W8(0x810701, key & 0xFF)]
        out.append(case(f'1E9580 key {key:x}', 0x1E9580, [p, n % 8, SCRATCH], writes=w, pin=True))
    return out


# Routines that make no stubbed call (every callee runs as original code):
# an fx variant of theirs would equal the ordinary case.
NO_STUBS = {0x1E9580, 0x1E8E80, 0x1E9280, 0x1C3DB0, 0x1D0400, 0x1C6160}


def cases_fx(base):
    """The fx variants of every case whose original makes a stubbed call and
    loads memory after (or before) one."""
    out = []
    for c in base:
        if c.get('at_callee') or c['fn'] in NO_STUBS:
            continue
        for kind in ('after', 'before'):
            out.append(dict(c, name=f"{c['name']} fx-{kind}", fx=kind, pin=c['pin'] and kind == 'after'))
    return out


def poison_pin(c):
    """The default run's poison variants: one per path class, not every
    pinned case (the 26 area keys of 001E9580 share one store set)."""
    n = c['name']
    if n.startswith('1E9580'):
        return n.endswith((' key 1', ' key 0', ' key ffff'))
    if n.startswith('158810 s1 '):
        return ' b4 10 r' in n and ' t2f ' in n
    if n.startswith('158810 s2 '):
        return n.endswith(' 4') and (' h64 b0 ' in n or ' h78 ' in n or ' h1 ' in n or 'sub0' in n)
    if n.startswith('158810 s0 '):
        return ' t2f 10' in n
    if n.startswith('156F30 s1 ') and 'random' not in n:
        return n.startswith(('156F30 s1 0 ', '156F30 s1 2 '))
    return True


def cases_poison(base):
    """The poison variant of every case (poison_writes)."""
    out = []
    for c in base:
        out.append(dict(c, name=f"{c['name']} poison", poison=True, pin=c['pin'] and poison_pin(c)))
        for kind in ('byte', 'top'):
            out.append(dict(c, name=f"{c['name']} edge-{kind}", edge=kind, pin=c['pin'] and poison_pin(c)))
        if c['fn'] not in NO_STUBS and not c.get('at_callee'):
            out.append(dict(c, name=f"{c['name']} prestore", prestore=True, pin=c['pin'] and poison_pin(c)))
    return out


def cases_handler():
    """The +0x4C handler moves inside 001B17A0: the original loads the
    pointer after that call, so a translation that loaded it earlier calls
    the old one (HANDLER instead of HANDLER2) and fails at that call."""
    out = []
    for fn, st, extra in ((0x156F30, 4, []), (0x156F30, 1, []), (0x1581A0, 1, []), (0x158810, 1, [4]),
                          (0x158810, 2, [3]), (0x158BD0, 1, [])):
        p = owners(fn)[0]
        w = [W8(p + 4, st)] + ([W8(p + 5, extra[0])] if extra else [])
        c = case(f'{fn:X} handler2 s{st}', fn, [p], writes=w, script={0x1B17A0: [1]}, pin=True)
        c['at_callee'] = (0x1B17A0, [W32(p + 0x4C, HANDLER2)])
        out.append(c)
    return out


def cases_sweep1():
    """Inputs added after the first mutation sweep (docs/AREA01_EXITB.md
    section 5), each for a survivor it kills: bit numbers 8 and 10 with a
    nonzero next area byte (a 16-bit read of D_00810841[area]); the swing's
    sign test at x == 0 exactly and the wrap at exactly +-pi; a +0x20 pointer
    with a nonzero low byte; zero position / spread for the lattice (the
    jitter and the 0.15 lift then show their last bit); q's w lane != 1 for
    0015B030's +0xBC store; and the D_00275B40 records swapped inside
    001C6380 (a pointer read hoisted above that call)."""
    out = []
    ram = image(EXIT)[0]
    p = owners(0x158BD0)[0]
    for bit in (8, 10):
        w = [W8(p + 4, 1), W16(p + 0x2E, bit), W8(0x810700, 4), W8(0x810845, 0x5A), W8(0x810846, 0xFF)]
        out.append(case(f'158BD0 s1 next-byte b{bit}', 0x158BD0, [p], writes=w, script={0x1B17A0: [0]}, pin=True))
    p = owners(0x158810)[0]
    for bit in (8, 10):
        w = [W8(p + 4, 0), W16(p + 0x2E, bit), W8(0x810700, 4), W8(0x810845, 0x5A), W8(0x810846, 0xFF)]
        out.append(case(f'158810 s0 next-byte b{bit}', 0x158810, [p], writes=w, pin=True))
    w = [W8(p + 4, 1), W8(p + 5, 2), W32(p + 0x20, SCRATCH + 0x144)]
    out.append(case('158810 s1 sub2 q low byte', 0x158810, [p], writes=w, pin=True))
    p = owners(0x156F30)[0]
    pos = [u32(ram, p + 0xB0 + 4 * i) for i in range(3)]
    w = [W8(p + 4, 4), W16(p + 0x36, 1), W32(p + 0xC4, 0)] + [W32(0x810350 + 4 * i, v) for i, v in enumerate(pos)]
    out.append(case('156F30 s4 restart x zero', 0x156F30, [p], writes=w, pin=True))
    for bits in (0x40490FDB, 0xC0490FDB):
        w = [W8(p + 4, 1), W32(p + 0x2E4, bits), W32(p + 0x2E0, 0), WF(p + 0x2E8, 0.5)]
        out.append(case(f'156F30 s1 angle {bits:08x}', 0x156F30, [p], writes=w, pin=True))
    t = u32(ram, 0x275B40)
    t0, t1 = u32(ram, t), u32(ram, t + 4)
    for st in (0, 1):
        c = case(f'156F30 s{st} records swapped in 001C6380', 0x156F30, [p], writes=[W8(p + 4, st), WF(p + 0x2E8, 0.5)],
                 pin=True)
        c['at_callee'] = (0x1C6380, [W32(t, t1), W32(t + 4, t0)])
        out.append(c)
    p = owners(0x15AB00)[0]
    zero = [WF(p + 0xB0, 0.0), WF(p + 0xB4, 0.0), WF(p + 0xB8, 0.0)] + vwrites(SCRATCH, [0.0, 0.0, 0.0, 0.0])
    out.append(case('1E8E80 zero origin', 0x1E8E80, [p, 2, SCRATCH], writes=zero + [seed_write(0x1234567)], pin=True))
    out.append(case('1E9580 zero origin', 0x1E9580, [p, 2, SCRATCH], writes=zero + [seed_write(0x7654321)], pin=True))
    p = owners(0x15B030)[0]
    q = u32(ram, p + 0x20)
    w = [W8(p + 4, 1), W8(q + 4, 0), WF(q + 0xBC, 2.0), WF(p + 0xAC, 0.5)]
    out.append(case('15B030 s1 w lane', 0x15B030, [p], writes=w, pin=True))
    return out


def cases_review2():
    """Inputs added in the fix round after the second review (docs/AREA01_EXITB.md
    section 5.1), each pinned: a z component with exponent 255 for 001C3DB0's
    dot product (MADD adds the unsaturated product, MUL saturates it); area bit
    numbers whose low five bits are 9 or 17 over a byte with every bit set (a
    shift by b & 7 instead of b & 31 sets a bit the original never sees); and
    stub v0 values with the upper 32 bits set for the three entries that return
    the last callee's full v0 register."""
    out = []
    S = SCRATCH
    for name, ax, bx in (('dot inf z', -2e19, 2e19), ('dot inf z small', -1.0, 2.0)):
        w = vwrites(S, [ax, 0.0, 0.0, 0.0]) + [W32(S + 8, 0x7F800000)]
        w += vwrites(S + 0x10, [bx, 0.0, 1.0, 0.0]) + vwrites(S + 0x20, [0.5, -1.0, 2.0, 1.0])
        w += vwrites(S + 0x30, [9.0] * 4)
        out.append(case(f'1C3DB0 {name}', 0x1C3DB0, [S, S + 0x10, S + 0x20, S + 0x30], writes=w, pin=True))
    p = owners(0x1581A0)[0]
    for bit in (9, 17):
        w = [W8(p + 4, 0), W8(0x810700, 1), W16(p + 0x2E, bit), W8(0x810842, 0xFF), W8(0x810843, 0xFF)]
        out.append(case(f'1581A0 s0 shift-mask b{bit}', 0x1581A0, [p], writes=w, pin=True))
    p = owners(0x158BD0)[0]
    for bit in (9, 17):
        w = [W8(p + 4, 1), W16(p + 0x2E, bit), W8(0x810700, 4), W8(0x810845, 0xFF), W8(0x810846, 0xFF)]
        out.append(case(f'158BD0 s1 shift-mask b{bit}', 0x158BD0, [p], writes=w, script={0x1B17A0: [0]},
                        pin=True))
    p = owners(0x158810)[0]
    for bit in (9, 17):
        w = [W8(p + 4, 0), W16(p + 0x2E, bit), W8(0x810700, 4), W8(0x810845, 0xFF), W8(0x810846, 0xFF)]
        out.append(case(f'158810 s0 shift-mask b{bit}', 0x158810, [p], writes=w, pin=True))
    node = [a for a, cb in pool(image(EXIT)[0])][0]
    m, v1, v2 = S, S + 0x40, S + 0x50
    w = []
    for row in range(4):
        w += vwrites(m + 16 * row, [1.0 if i == row else 0.0 for i in range(4)])
    w += vwrites(v1, [1.0, 2.0, 3.0, 1.0]) + vwrites(v2, [-1.0, 0.5, 0.25, 0.0])
    for hi in (0x100000000, 0xFFFFFFFF00000000):
        out.append(case(f'1BB520 v0 hi {hi:x}', 0x1BB520, [node], script={0x1B0FD0: [hi]}, pin=True))
        out.append(case(f'1C2430 v0 hi {hi:x}', 0x1C2430, [node, v1, m], writes=w, script={0x19AB20: [hi]},
                        pin=True))
        out.append(case(f'1C2540 v0 hi {hi:x}', 0x1C2540, [node, v1, v2, m], writes=w,
                        script={0x19B4C0: [hi]}, pin=True))
    return out


# The default run (docs/AREA01_EXITB.md section 5): the case or variant that
# killed each mutant of the sweeps (the first difference in the fail-fast
# order), plus every targeted case (handler, sweep 1, review 2), the first captured
# record of each routine, and nothing else. EM_TEST_FULL=1 runs all.
QUICK_PINNED = frozenset((
    '156F30 captured 0',
    '156F30 captured 0 edge-byte',
    '156F30 s0 settle 0',
    '156F30 s0 settle 0 edge-byte',
    '156F30 s0 settle 0 poison',
    '156F30 s1 0 0.0 0',
    '156F30 s1 0 0.0005 1',
    '156F30 s1 0 0.0005 1 edge-byte',
    '156F30 s1 0 0.001 0',
    '156F30 s1 0 0.1 0',
    '156F30 s1 0 0.1 0 poison',
    '156F30 s1 2 0.1 0',
    '156F30 s1 4 0.1 0',
    '156F30 s1 angle 40490fdb',
    '156F30 s1 angle c0490fdb',
    '156F30 s1 records swapped in 001C6380',
    '156F30 s2',
    '156F30 s3',
    '156F30 s4 restart 0',
    '156F30 s4 restart 0 edge-byte',
    '156F30 s4 restart 1',
    '156F30 s4 restart x zero',
    '1576E0 b4 t12 00',
    '1576E0 b4 t12 10 poison',
    '1576E0 bfb t12 00',
    '1581A0 captured',
    '1581A0 captured edge-byte',
    '1581A0 s0 a0 b0 1',
    '1581A0 s0 a1 b2 0',
    '1581A0 s0 a1 b2 0 edge-byte',
    '1581A0 s0 a1 b2 0 poison',
    '1581A0 s0 a1 b2 1',
    '1581A0 s0 a1 b2 1 edge-byte',
    '1581A0 s1 b3 t1',
    '1581A0 s1 b3 t1 edge-byte',
    '1581A0 s1 b9 t1',
    '1581A0 s2',
    '1581A0 s3',
    '1581A0 s4',
    '158810 captured',
    '158810 s0 h0 t13 00 fx-before',
    '158810 s0 h0 t2f 00',
    '158810 s0 h0 t2f 10',
    '158810 s0 h0 t2f 10 edge-byte',
    '158810 s0 h0 t2f 10 poison',
    '158810 s0 h1 t13 00 fx-after',
    '158810 s0 h1 t2f 00',
    '158810 s0 h1 t2f 10 edge-byte',
    '158810 s0 h1 t2f 10 poison',
    '158810 s1 sub0 t13 b4 10 r0 q0',
    '158810 s1 sub0 t2f b4 00 r0 q0',
    '158810 s1 sub0 t2f b4 00 r0 q0 edge-byte',
    '158810 s1 sub0 t2f b4 00 r0 q0 fx-before',
    '158810 s1 sub0 t2f b4 10 r0 q0',
    '158810 s1 sub0 t2f b4 10 r0 q0 edge-byte',
    '158810 s1 sub0 t2f b4 11 r0 q0',
    '158810 s1 sub0 t2f b4 11 r0 q0 edge-byte',
    '158810 s1 sub1 t2f b4 10 r0 q0',
    '158810 s1 sub1 t2f b4 10 r1 q0',
    '158810 s1 sub1 t2f b4 10 r1 q0 edge-byte',
    '158810 s1 sub2 q low byte',
    '158810 s1 sub2 t2f b4 10 r0 q0',
    '158810 s1 sub2 t2f b4 10 r0 q0 edge-byte',
    '158810 s1 sub2 t2f b4 10 r0 q1',
    '158810 s1 sub2 t2f b4 10 r0 q1 edge-byte',
    '158810 s1 sub3 t2f b4 10 r0 q0',
    '158810 s1 sub3 t2f b4 10 r1 q0',
    '158810 s1 sub3 t2f b4 10 r1 q0 edge-byte',
    '158810 s1 sub3 t2f b4 10 r1 q0 poison',
    '158810 s2 sub0 h0 b0 0',
    '158810 s2 sub0 h0 b0 4',
    '158810 s2 sub0 h0 b0 4 edge-byte',
    '158810 s2 sub0 h0 b0 4 fx-after',
    '158810 s2 sub0 h0 b0 4 prestore',
    '158810 s2 sub1 h0 b0 4',
    '158810 s2 sub1 h0 b0 4 edge-byte',
    '158810 s2 sub1 h1 b0 4',
    '158810 s2 sub2 h0 b0 4',
    '158810 s2 sub2 h1 b0 4 edge-byte',
    '158810 s2 sub2 h64 b0 4',
    '158810 s2 sub2 h64 b0 4 fx-before',
    '158810 s2 sub2 h64 b3 4',
    '158810 s2 sub2 h77 b2 4',
    '158810 s2 sub2 h78 b0 4',
    '158810 s2 sub2 h78 b0 4 edge-byte',
    '158810 s3',
    '158810 s4',
    '158BD0 captured',
    '158BD0 captured edge-byte',
    '158BD0 captured poison',
    '158BD0 s0',
    '158BD0 s1 next-byte b8',
    '158BD0 s1 r0 b4 1',
    '158BD0 s1 r0 b4 1 poison',
    '158BD0 s1 r1 b4 1',
    '158BD0 s2',
    '158BD0 s3',
    '15AB00 captured 0',
    '15AB00 captured 0 edge-byte',
    '15AB00 captured 0 poison',
    '15AB00 captured 7',
    '15AB00 s0 0',
    '15AB00 s0 0 edge-byte',
    '15AB00 s0 0 poison',
    '15AB00 s0 2',
    '15AB00 s0 3',
    '15AB00 s2',
    '15AB00 s3',
    '15AB00 s4',
    '15B030 captured',
    '15B030 captured edge-byte',
    '15B030 captured poison',
    '15B030 s0 q1 r0',
    '15B030 s0 q1 r0 edge-byte',
    '15B030 s0 q2 r0',
    '15B030 s0 q2 r0 edge-byte',
    '15B030 s1 q2 r0',
    '15B030 s1 q2 r0 edge-byte',
    '15B030 s1 w lane',
    '15B030 s2',
    '15B030 s2 edge-byte',
    '1BB520 0 0',
    '1BB520 0 1234',
    '1C2430 0',
    '1C2430 0 poison',
    '1C2540 0',
    '1C3DB0 0',
    '1C3DB0 1',
    '1C3DB0 2',
    '1C3DB0 4',
    '1C3DB0 6',
    '1C3DB0 7',
    '1C6160 captured 0',
    '1C6160 captured 0 edge-top',
    '1D0400 0',
    '1D0400 1',
    '1E8E80 0',
    '1E8E80 0 poison',
    '1E8E80 zero origin',
    '1E9580 key 0',
    '1E9580 key 1',
    '1E9580 key 1 edge-byte',
    '1E9580 key 1 poison',
    '1E9580 key 100',
    '1E9580 key 1001',
    '1E9580 key 101',
    '1E9580 key 1300',
    '1E9580 key 1301',
    '1E9580 key 2',
    '1E9580 key 200',
    '1E9580 key 202',
    '1E9580 key 300',
    '1E9580 key 401',
    '1E9580 key 600',
    '1E9580 key 601',
    '1E9580 key 700',
    '1E9580 key 702',
    '1E9580 key 703',
    '1E9580 key 803',
    '1E9580 key d00',
))
# Cases that the killers above leave out and that take the remaining
# branch outcomes (a greedy cover), so the default run also takes both
# outcomes of all 108 conditional branches.
QUICK_BRANCH = frozenset((
    '156F30 s0 settle 1', '156F30 s1 0 0.001 1', '158810 s0 h0 t2f 11', '158810 s1 sub0 t13 b4 00 r0 q0',
    '158810 s1 sub5 t2f b4 10 r0 q0', '158BD0 s4', '15B030 s3', '15B030 s4', '15B030 s0 q0 r1',
    '1E9580 key 400', '1E9580 key 1000', '1E9580 key 1400', '1E9580 key b00',
))


def all_cases():
    rng = random.Random(0xE81B)
    sweep = cases_handler() + cases_sweep1() + cases_review2()
    base = cases_owners(rng) + cases_leaves(rng) + sweep
    variants = cases_fx(base) + cases_poison(base)
    targeted = {c['name'] for c in sweep}
    first = set()
    for c in base + variants:
        routine = c['name'].split(' ')[0]
        keep = c['name'] in QUICK_PINNED or c['name'] in QUICK_BRANCH or c['name'] in targeted \
            or c['name'] == '1C3DB0 spad shape 0'
        if not keep and ' captured' in c['name'] and not c.get('fx') and not c.get('poison') \
                and not c.get('edge') and not c.get('prestore') and routine not in first:
            keep = True
        if keep and not c.get('fx') and not c.get('poison') and not c.get('edge') and not c.get('prestore'):
            first.add(routine)
        c['pin'] = keep
    return base, variants


# ======================================================================
# Static checks
# ======================================================================

def jal_targets(mem):
    out = {}
    for lo, hi in FUNC_RANGES:
        for pc in range(lo, hi, 4):
            w = struct.unpack_from('<I', mem, pc)[0]
            if w >> 26 == 3:
                out.setdefault((pc & 0xF0000000) | ((w & 0x3FFFFFF) << 2), set()).add(lo)
    return out


def branch_pcs(mem):
    out = set()
    for lo, hi in FUNC_RANGES:
        for pc in range(lo, hi, 4):
            w = struct.unpack_from('<I', mem, pc)[0]
            op, rt = w >> 26, w >> 16 & 31
            if op in (4, 5, 20, 21) and not (op == 4 and (w >> 21 & 31) == 0 and rt == 0):
                out.add(pc)
            elif op in (6, 7, 22, 23) or (op == 1 and rt in (0, 1, 2, 3)) or (op == 17 and (w >> 21 & 31) == 8):
                out.add(pc)
    return out


# Conditional branches whose other outcome no input can take (with proof).
UNREACHABLE = {}


def check_register_reads(mem):
    import test_area01_sys_reference as SYSREF    # RegScan, unchanged (read-only reuse)
    scan = SYSREF.RegScan(mem)
    bad, rows = [], []
    items = sorted(CALLEES.items()) + [(HANDLER, INDIRECT)]    # HANDLER2 is only a stubbed address
    for fn, (kind, na, nf) in items:
        ri, rf, notes = scan.reads(fn)
        ints = {r for r in range(36) if ri >> r & 1}
        floats = {r for r in range(SYSREF.VU_BASE + 152) if rf >> r & 1}
        extra_i = sorted(r for r in ints if not 4 <= r < 4 + na and r not in (0, 28, 29, 31)
                         and not (kind == S and SYSREF.SAVED_READ_MASK_I >> r & 1))
        extra_f = sorted(r for r in floats if not 12 <= r < 12 + nf and not (kind == S and 20 <= r < 32)
                         and r < SYSREF.VU_BASE)
        vu = sorted(r for r in floats if r >= SYSREF.VU_BASE)
        rows.append((hex(fn), kind, na, nf, [r for r in ints if 4 <= r <= 11], [r for r in floats if 12 <= r <= 15],
                     len(vu)))
        if extra_i or extra_f:
            bad.append((hex(fn), kind, 'reads', extra_i, extra_f, 'policy', na, nf))
    return bad, rows


def transpose_property():
    """00102798 through ExEE's word interleaves must transpose a 4 x 4
    matrix (the property the shared core cannot check for us)."""
    ee = ExEE(ELF)
    src, dst = 0x01E00000, 0x01E00100
    vals = [0x3F800000 + i * 0x10000 for i in range(16)]
    for i, v in enumerate(vals):
        ee.save(src + 4 * i, v, 4)
    ee.hooks = {}
    ee.r[4], ee.r[5] = dst, src
    ee.r[29] = SP
    ee.r[31] = shared.RETURN
    ee.run(0x102798)
    got = [ee.load(dst + 4 * i, 4) for i in range(16)]
    want = [vals[4 * (i % 4) + i // 4] for i in range(16)]
    assert got == want, ('00102798 transpose property', [hex(x) for x in got])


def api_checks():
    """Fail-stop contract: NULL context, NULL regions, NULL worker, a worker
    error, an unmapped access; the latch and clear_fault."""
    ram = (U8 * 0x1000)()
    regions = (Region * 1)(Region(0x1000, 0x1000, C.cast(ram, P(U8))))
    calls = []

    def worker(_, pc):
        calls.append(pc.contents.fn)
        return -1 if pc.contents.fn == 0x1B0FD0 else 0
    w = WORKER(worker)
    fn = NATIVE.em_area01_exitb_001BB520
    assert fn(None, 0x1000, None) == -1
    s = Exitb(None, 0, w, None, SP, 0, 0, 0)
    assert fn(C.byref(s), 0x1000, None) == -1 and s.fault == 1 and s.fault_function == 0x1BB520
    NATIVE.em_area01_exitb_clear_fault(C.byref(s))
    s = Exitb(regions, 1, WORKER(0), None, SP, 0, 0, 0)
    assert fn(C.byref(s), 0x1000, None) == -1 and s.fault == 1 and s.fault_address == 0x1B0FD0
    s = Exitb(regions, 1, w, None, SP, 0, 0, 0)
    assert fn(C.byref(s), 0x1000, None) == -1 and s.fault == 2 and s.fault_address == 0x1B0FD0
    assert fn(C.byref(s), 0x1000, None) == -1 and calls == [0x1B0FD0], 'refuses while latched'
    NATIVE.em_area01_exitb_clear_fault(C.byref(s))
    assert s.fault == 0 and s.fault_function == 0 and s.fault_address == 0
    NATIVE.em_area01_exitb_clear_fault(None)
    # 001C6160 reads the halfword +0x2C, then the word +0x40, before its
    # call: the first unmapped one is reported (region [0x1000, 0x2000))
    out = U32(0x5A5A5A5A)
    for a0, bad in ((0x1FF0, 0x201C), (0x1FD0, 0x2010)):
        del calls[:]
        assert NATIVE.em_area01_exitb_001C6160(C.byref(s), a0, C.byref(out)) == -1
        assert (s.fault, s.fault_function, s.fault_address) == (3, 0x1C6160, bad), \
            (s.fault, hex(s.fault_function), hex(s.fault_address))
        assert calls == [] and out.value == 0x5A5A5A5A, (calls, hex(out.value))
        NATIVE.em_area01_exitb_clear_fault(C.byref(s))
    # an unmapped access is reported at its address, before any store
    s = Exitb(regions, 1, WORKER(lambda _, pc: 0), None, SP, 0, 0, 0)
    assert NATIVE.em_area01_exitb_0015B030(C.byref(s), 0x1FF0) == -1
    assert s.fault == 3 and s.fault_address == 0x2010 and s.fault_function == 0x15B030, (s.fault,
                                                                                           hex(s.fault_address))


# ======================================================================
# Main
# ======================================================================

def main():
    global NATIVE, ELF, BRANCH_PCS
    t0 = time.time()
    if AV.ENABLED:
        print('canonical callback mode: all original-instruction comparisons; region arrays disabled', flush=True)
    ELF = read_elf()
    NATIVE = build_native()
    mem = ExEE(ELF).mem
    BRANCH_PCS = branch_pcs(mem)
    for fn in FUNCS:
        assert ENTRIES.get(fn), ('entry without a signature', hex(fn))
    missing = sorted(t for t in jal_targets(mem) if t not in CALLEES and t not in FUNCS)
    assert not missing, ('direct call targets without a policy', [hex(t) for t in missing])
    bad, _ = check_register_reads(bytes(mem))
    assert not bad, ('callee register reads outside the compared policy', bad)
    transpose_property()
    api_checks()
    base, fxs = all_cases()
    only = os.environ.get('EM_AREA01_EXITB_ONLY', '')
    if only:
        keep = {int(x, 16) for x in only.split(',') if x}
        base = [c for c in base if c['fn'] in keep]
        fxs = [c for c in fxs if c['fn'] in keep]
    run_base = RM.select(base, 0, 1, keep=lambda i, c: c['pin'])
    run_fx = RM.select(fxs, 0, 2, keep=lambda i, c: c['pin'])
    todo = run_base + run_fx
    if os.environ.get('EM_AREA01_EXITB_FAILFAST'):
        for c in todo:
            r = run_case_safe(c)
            if r[0] == 'fail':
                print('FAIL', r[1])
                sys.exit(1)
        print('test_area01_exitb_reference: survived')
        return
    results = RM.parallel_map(run_case_safe, todo)
    fails = [r[1] for r in results if r[0] == 'fail']
    outcomes, calls, fxw = set(), 0, 0
    skipped = sum(1 for r in results if r[0] == 'ok' and r[1] is None)
    for r in results:
        if r[0] == 'ok' and r[1] is not None:
            outcomes |= r[1][0]
            calls += r[1][1]
            fxw += r[1][2]
    for f in fails[:12]:
        print('FAIL', f)
    assert not fails, f'{len(fails)} case(s) differ'
    both = {pc for pc in BRANCH_PCS if (pc, True) in outcomes and (pc, False) in outcomes}
    gaps = sorted((hex(pc), [t for t in (True, False) if (pc, t) not in outcomes])
                  for pc in BRANCH_PCS - both if pc not in UNREACHABLE)
    if os.environ.get('EM_AREA01_EXITB_GAPS'):
        for g in gaps:
            print('gap', g)
    if RM.FULL and not only:
        assert not gaps, ('branch outcomes not taken', gaps)
    RM.banner(RM.part(len(run_base), len(base), 'cases'), RM.part(len(run_fx), len(fxs), 'fx / poison variants'),
                    f'{calls:,} calls compared', f'{fxw:,} fx field changes',
                    f'{skipped} variants not comparable (the original left RAM)',
                    f"branches both ways {len(both)}/{len(BRANCH_PCS)}")
    print(f'test_area01_exitb_reference: OK ({time.time() - t0:.1f} s)')


if __name__ == '__main__':
    main()
