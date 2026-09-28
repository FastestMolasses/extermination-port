#!/usr/bin/env python3
"""Execute the original lane-L4MATH routines and compare em_area02_math.c.

docs/AREA02_MATH.md. The user's pinned ELF and the captured revisit / AREA02
RAM (../Extermination/build/s87/route_a01r/<beat>/ and route_a02/<beat>/,
the end-of-beat images of docs/FOURTH_LEVEL_ROUTE.md) supply every
instruction and every table; none are embedded here.

The oracle is FallEE (tools/test_player_fall_reference.py: every COP1 and
VU0 macro op through tools/ee_float_model.py, the measured model) running
the ORIGINAL routine over a copy of the capture. The native module runs over
another copy of the same bytes. Every call leaving the translated set is
caught on both sides and handled by one Python policy:
  run   the original callee runs (the vector leaves and the yaw 001B1240):
        in the oracle inside the same interpreter, on the native side in a
        second interpreter bound to the native module's own memory;
  stub  the callee is recorded and returns a scripted value (all 64 bits
        when the script gives a value outside the 32-bit range).
A node method (word +0x4C) or 001C7EB0's callback is a call through a
loaded pointer; its target must have a policy (0x1CAA00, 0x1CAF60 in the
cases). Calls among the translated routines (00159620 -> 00157B30,
001CAF60 -> 001CAE40, 001CB580 -> 001CB4F0) are not intercepted: the
original runs its own code, the native module its own translations. The
routine under test is never hooked (0x1CAF60 is both an entry and a method
target).

Lockstep (the lane-SYS design, docs/AREA01_SYS.md section 4, as lane
A00WORLD uses it, docs/AREA00_WORLD.md section 4). The original runs first
and records, at every call leaving the set, the callee entry and every RAM /
scratchpad line it stored to since the previous call. The native module is
built with its store trace (EM_AREA02_MATH_STORE_TRACE) and, at the same
call, must match: the callee, the stack pointer, every argument register
the callee reads (64-bit images), the float argument registers, every
register the native says it sets, and all of RAM and the scratchpad (only
lines either side stored to since the last check can differ, so exactly
those are compared). The same memory check runs after the last store, then
all 32 MiB, the scratchpad and the result.

Variants of every case (the lane-EXITB set, docs/AREA01_EXITB.md section 4):
  fx after / fx before  at the k-th stub every field the translated
        routines load after it (resp. loaded since the previous stub)
        changes, identically on both sides;
  poison  every byte the routines store before loading it starts as the
        complement of its final value;
  prestore  at each stub the bytes stored after it are set to the
        complement of what is stored;
  edge-byte / edge-top  the byte after every byte / halfword access is
        0xA5 / the top bit of every byte / halfword load is flipped.

Register reads: RegScan (test_area01_sys_reference, read-only reuse)
measures what every callee reads before writing; the test fails when a
callee reads an argument register its policy does not compare. Every direct
call target must have a policy. check_leaf_keeps proves the two register
hand-overs 001BD560 relies on. In EM_TEST_FULL=1 both outcomes of every
conditional branch in the translated routines must be taken, except the
ones listed in UNREACHABLE with proof.

EM_TEST_FULL=1 runs every case and variant; the default run is the pinned
set (docs/AREA02_MATH.md section 4). EM_AREA02_MATH_SOURCE=<file> tests
another copy of the module source (mutation runs); EM_AREA02_MATH_ONLY=
<hex,...> runs only the cases of those entries; EM_AREA02_MATH_FAILFAST=1
runs serially and stops at the first difference; EM_AREA02_MATH_GAPS=1
lists branch outcomes not taken; EM_AREA02_MATH_REGS=1 prints the callee
register reads. EM_TEST_JOBS sets the worker processes (default 2, at most
4 for this lane).
"""
import ctypes as C
import math
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
import test_player_slide_reference as shared  # noqa: E402
from test_player_slide_reference import read_elf, sx32  # noqa: E402
from test_player_fall_reference import FallEE  # noqa: E402

MASK, MASK64 = 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF
DECOMP = ROOT.parent / 'Extermination'
ROUTE_A01R = DECOMP / 'build/s87/route_a01r'
ROUTE_A02 = DECOMP / 'build/s87/route_a02'
BEATS = {
    'r00': ROUTE_A01R / 'a01r_00_to_train_room', 'r01': ROUTE_A01R / 'a01r_01_event',
    'r03': ROUTE_A01R / 'a01r_03_door16', 'a00': ROUTE_A02 / 'a02_00_duct', 'a02': ROUTE_A02 / 'a02_02_ladder_escape',
    'a04': ROUTE_A02 / 'a02_04_panel', 'a05': ROUTE_A02 / 'a02_05_progression_exit', 'bed': ROUTE_A02 / 'a02_s0_mts_bed',
}
OUT = ROOT / 'build' / 'area02' / 'math'
STACK_BASE, STACK_SIZE = 0x7F000000, 0x100000
STACK_FILL = bytes([0xA5]) * STACK_SIZE
SP = shared.STACK_TOP
SCRATCH = 0x01E00000            # zero in every a01r / a02 capture; test records live here
CODE_END = 0x230000

# Translated routines (address -> size in bytes, from the census).
FUNCS = {
    0x1575E0: 0xF4, 0x157B30: 0x1B0, 0x157F30: 0x2C, 0x158050: 0x64, 0x1582E0: 0x150, 0x158EC0: 0x348,
    0x159620: 0x348, 0x159970: 0x214, 0x159E70: 0x144, 0x183C40: 0x2A4, 0x1B18F0: 0x238, 0x1BC960: 0x81C,
    0x1BD560: 0x490, 0x1BDFC0: 0x630, 0x1C48C0: 0x9C, 0x1C4960: 0x98, 0x1C4AF0: 0xAC, 0x1C4CB0: 0x2F0,
    0x1C7EB0: 0x28C, 0x1C8140: 0x7C, 0x1CAE40: 0x114, 0x1CAF60: 0x8, 0x1CB4F0: 0x90, 0x1CB580: 0x8,
}
FUNC_RANGES = tuple((a, a + n) for a, n in FUNCS.items())

# Callee policy: address -> (kind, integer argument registers compared (a0..),
# float argument registers compared (f12..)). Checked against RegScan.
# 0x1CAA00 and 0x1CAF60 are node methods (word +0x4C) and 001C7EB0's
# callback in the cases; 0x1CAF60 is also an entry (not hooked when it is
# the routine under test).
R, S = 'run', 'stub'
CALLEES = {
    # vector leaves and the yaw of 00157B30 (pure; run as original)
    0x1026A0: (R, 3, 0), 0x102948: (R, 2, 0), 0x1B1240: (R, 1, 2),
    # stubs
    0x157860: (S, 3, 0), 0x157CE0: (S, 2, 0), 0x158590: (S, 3, 0), 0x1AEBA0: (S, 1, 0), 0x1AF780: (S, 0, 0),
    0x1AFA90: (S, 1, 0), 0x1AFC10: (S, 1, 0), 0x1B0FD0: (S, 1, 0), 0x1B1020: (S, 4, 0), 0x1B1630: (S, 0, 3),
    0x1B17A0: (S, 1, 0), 0x1B1B70: (S, 1, 0), 0x1B1D20: (S, 1, 0), 0x1B6F00: (S, 2, 1), 0x1BA1A0: (S, 2, 0),
    0x1BA1C0: (S, 2, 0), 0x1BA1F0: (S, 1, 0), 0x1BBD20: (S, 2, 0), 0x1BC150: (S, 1, 0), 0x1BC740: (S, 2, 0),
    0x1BD180: (S, 1, 0), 0x1BD270: (S, 1, 0), 0x1BD370: (S, 1, 0), 0x1BD460: (S, 1, 0), 0x1C5570: (S, 4, 0),
    0x1C6380: (S, 1, 0), 0x1C63E0: (S, 2, 0), 0x1C64F0: (S, 1, 1), 0x1C68C0: (S, 1, 0), 0x1C7420: (S, 3, 0),
    0x1CA5E0: (S, 3, 0), 0x1CA7B0: (S, 1, 1), 0x1CAAC0: (S, 2, 0), 0x1CB5B0: (S, 0, 0), 0x1D1F80: (S, 3, 0),
    0x1D2830: (S, 2, 0), 0x1D2910: (S, 1, 0), 0x1D38F0: (S, 1, 0), 0x1D3AC0: (S, 1, 0), 0x1D3CE0: (S, 1, 0),
    0x1D8C20: (S, 1, 0), 0x1F1110: (S, 2, 0), 0x1F1180: (S, 1, 0), 0x1F4E40: (S, 3, 1), 0x1FB9F0: (S, 4, 0),
    # methods / callbacks reached through a loaded pointer
    0x1CAA00: (S, 1, 0), 0x1CAF60: (S, 1, 0),
}
INDIRECT = {0x1CAA00, 0x1CAF60}

# Entry signatures: fn -> (argument kinds, output kind); outputs: None
# (void), 'i' (int32).
ENTRIES = {
    0x1575E0: ('p', 'i'), 0x157B30: ('pp', 'i'), 0x157F30: ('p', 'i'), 0x158050: ('p', 'i'), 0x1582E0: ('p', None),
    0x158EC0: ('p', None), 0x159620: ('p', None), 0x159970: ('p', None), 0x159E70: ('p', None),
    0x183C40: ('pp', None), 0x1B18F0: ('ppp', 'i'), 0x1BC960: ('p', None), 0x1BD560: ('p', None),
    0x1BDFC0: ('p', None), 0x1C48C0: ('p', None), 0x1C4960: ('p', None), 0x1C4AF0: ('p', None),
    0x1C4CB0: ('p', None), 0x1C7EB0: ('p', None), 0x1C8140: ('ppp', None), 0x1CAE40: ('pp', None),
    0x1CAF60: ('p', None), 0x1CB4F0: ('pp', None), 0x1CB580: ('p', None),
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
    """FallEE with branch-outcome recording inside the translated routines,
    the RAM / scratchpad lines stored to (`dirty`, `pre` = each line before
    its first store), and (when `rec` is set) every data load of the
    translated routines as (address, size, value, stub index, pc)."""

    def __init__(self, elf, ram=None, spad=None):
        super().__init__(elf, ram, spad)
        self.outcomes = set()
        self.dirty = set()
        self.pre = None
        self.rec = None
        self.stored = None
        self.store_sites = set()
        self.store_log = []
        self.first_load = {}
        self.stub_index = 0
        self._pc = None

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
    """The v0 register image a stub leaves: a 32-bit script value sign-extended,
    a wider one taken as the full 64-bit register."""
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


def policy(fn, forced=()):
    if fn in CALLEES:
        kind, na, nf = CALLEES[fn]
        return (S, na, nf) if fn in forced else (kind, na, nf)
    raise AssertionError(('call without a policy', hex(fn)))


def log_entry(ee, fn, pol, regs, fregs, sp):
    """What the callee sees at its entry besides RAM and the scratchpad: the
    stack pointer, the compared argument registers, and the 16 bytes behind
    every compared argument that points into the stack."""
    kind, na, nf = pol
    args = tuple(regs[i] & MASK64 for i in range(na))
    stack = tuple(ee.read(a & MASK, 16) for a in args if STACK_LO <= (a & MASK) < STACK_HI - 16)
    return (hex(fn), hex(sp & MASK), args, tuple(fregs[i] & MASK for i in range(nf)), stack)


# ======================================================================
# Native side
# ======================================================================

P, U8, U32, I32, U64 = C.POINTER, C.c_uint8, C.c_uint32, C.c_int32, C.c_uint64
OUT_TYPES = {'i': I32}


class Region(C.Structure):
    _fields_ = [('base', U32), ('size', U32), ('bytes', P(U8))]


class Call(C.Structure):
    _fields_ = [('fn', U32), ('sp', U32), ('a', U64 * 4), ('f', U32 * 4), ('na', U32), ('nf', U32),
                ('v0', U64), ('f0', U32)]


WORKER = C.CFUNCTYPE(C.c_int, C.c_void_p, P(Call))


class World(C.Structure):
    _fields_ = [('regions', P(Region)), ('region_count', C.c_uint), ('call', WORKER), ('ctx', C.c_void_p),
                ('sp', U32), ('fault', I32), ('fault_function', U32), ('fault_address', U32)]


TRACE_C = r"""/* Store trace for the native module (test build only): the 64-byte
 * lines of RAM and scratchpad the module stores to since the last reset. */
#include <stdint.h>
#define RAM_LINES (0x2000000u / 64u)
#define LINES (RAM_LINES + 0x4000u / 64u)
static uint8_t dirty[LINES];
uint32_t a02math_trace_list[LINES];
uint32_t a02math_trace_count;
void a02math_trace_store(uint32_t address, unsigned size);
void a02math_trace_reset(void);
static void mark(uint32_t a)
{
    uint32_t id;
    if (a < 0x2000000u)
        id = a / 64u;
    else if (a - 0x70000000u < 0x4000u)
        id = RAM_LINES + (a - 0x70000000u) / 64u;
    else
        return; /* the private stack */
    if (!dirty[id]) {
        dirty[id] = 1;
        a02math_trace_list[a02math_trace_count++] = id;
    }
}
void a02math_trace_store(uint32_t address, unsigned size)
{
    mark(address);
    mark(address + size - 1u);
}
void a02math_trace_reset(void)
{
    uint32_t i;
    for (i = 0; i < a02math_trace_count; i++)
        dirty[a02math_trace_list[i]] = 0;
    a02math_trace_count = 0;
}
"""
TRACE = {}
NATIVE = None
ELF = None
IMAGES = {}
BUFFERS = {}
BRANCH_PCS = set()


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    trace = OUT / 'store_trace.c'
    if not trace.exists() or trace.read_text() != TRACE_C:
        trace.write_text(TRACE_C)
    source = os.environ.get('EM_AREA02_MATH_SOURCE', '')
    ext = 'dylib' if sys.platform == 'darwin' else 'so'
    if source:
        source = str(Path(source).resolve())
        lib = OUT / f'area02_math_{Path(source).stem}.{ext}'
        rebuild = not lib.exists() or Path(source).stat().st_mtime > lib.stat().st_mtime
    else:
        source = 'src/game/em_area02_math.c'
        lib = OUT / f'area02_math.{ext}'
        deps = [ROOT / source, ROOT / 'src/game/em_area02_math.h', ROOT / 'src/game/em_ee_float.h', trace,
                Path(__file__)]
        rebuild = not lib.exists() or max(d.stat().st_mtime for d in deps) > lib.stat().st_mtime
    if rebuild:
        # a mutant (EM_AREA02_MATH_SOURCE) may leave a warning (an unused
        # variable after a deleted line): it is built without -Werror
        werror = [] if os.environ.get('EM_AREA02_MATH_SOURCE') else ['-Werror']
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', *werror, '-Wpedantic',
                        '-ffp-contract=off', '-shared', '-fPIC', '-Isrc', '-Isrc/game',
                        '-DEM_AREA02_MATH_STORE_TRACE=a02math_trace_store', source, str(trace),
                        '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    TRACE['count'] = U32.in_dll(native, 'a02math_trace_count')
    TRACE['list'] = (U32 * (RAM_LINES + 0x4000 // LINE)).in_dll(native, 'a02math_trace_list')
    TRACE['reset'] = native.a02math_trace_reset
    for fn, (kinds, outk) in ENTRIES.items():
        f = getattr(native, f'em_area02_math_{fn:08X}')
        f.restype = C.c_int
        f.argtypes = [P(World)] + [U32] * len(kinds) + ([P(OUT_TYPES[outk])] if outk else [])
    native.em_area02_math_clear_fault.argtypes = [P(World)]
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
    """One native call over copies of `ram` / `spad` and a private stack
    (one set of buffers per process, refilled per case)."""

    def __init__(self, ram, spad, script, forced):
        if 'ram' not in BUFFERS:
            BUFFERS['ram'] = (U8 * len(ram))()
            BUFFERS['spad'] = (U8 * len(spad))()
            BUFFERS['stack'] = (U8 * STACK_SIZE)()
        self.ram, self.spad, self.stack = BUFFERS['ram'], BUFFERS['spad'], BUFFERS['stack']
        for dst, src in ((self.ram, ram), (self.spad, spad), (self.stack, STACK_FILL)):
            if isinstance(src, bytearray):
                C.memmove(dst, (C.c_char * len(src)).from_buffer(src), len(src))
            else:
                C.memmove(dst, src, len(src))
        # the module needs RAM and the scratchpad only; the stack buffer is
        # the nested interpreter's (run-policy callees' frames)
        self.regions = (Region * 2)(Region(0, len(ram), C.cast(self.ram, P(U8))),
                                    Region(0x70000000, len(spad), C.cast(self.spad, P(U8))))
        self.ee = ExEE(ELF, b'', b'')
        self.ee.mem = memoryview(self.ram).cast('B')
        self.ee.spad = memoryview(self.spad).cast('B')
        self.ee.stack = memoryview(self.stack).cast('B')
        self.script, self.forced, self.log, self.error = script, forced, [], None
        self.lock = None
        self.fx = {}
        self.stubs = 0
        self.worker = WORKER(self._call)
        self.sys = World(self.regions, 2, self.worker, None, SP, 0, 0, 0)

    def dirty(self):
        out = set(native_lines())
        out |= self.ee.dirty
        self.ee.dirty = set()
        return out

    def _call(self, _, pc):
        try:
            c = pc.contents
            pol = policy(c.fn, self.forced)
            kind, na, nf = pol
            assert na <= c.na <= 4 and nf <= c.nf <= 4, ('native register counts', hex(c.fn), c.na, c.nf)
            k = len(self.log)
            regs = [c.a[i] for i in range(4)]
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
                for i in range(4):
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
    rc = getattr(NATIVE, f'em_area02_math_{fn:08X}')(sysp, *vals, *([C.byref(out)] if outk else []))
    return rc, (out.value & (MASK64 if outk == 'q' else MASK) if out is not None else None)


# ======================================================================
# Oracle side
# ======================================================================

def oracle_run(case, ram, spad, on_stub=None, rec=False, stored=None):
    """The original over `ram` / `spad` (used in place). Returns (ee, log,
    v0, entries, final delta). on_stub(ee, k, fn) runs after the k-th
    stubbed call has set its result (the fx side effects)."""
    ee = ExEE(ELF, b'', b'')
    ee.mem, ee.spad = ram, spad
    ee.stack[:] = STACK_FILL
    ee.pre = {}
    ee.rec = [] if rec else None
    ee.stored = stored
    script, log, entries = Script(case['script']), [], []
    forced = set(case.get('forced', ()))
    stubs = [0]

    def make(fn):
        pol = policy(fn, forced)

        def hook(e):
            regs = [e.r[4 + i] for i in range(4)]
            fregs = [e.f[12 + i] for i in range(4)]
            entry = log_entry(e, fn, pol, regs, fregs, e.r[29])
            log.append(entry)
            entries.append((entry, e.take(), tuple(r & MASK64 for r in regs), tuple(f & MASK for f in fregs)))
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
    ee.hooks = {fn: make(fn) for fn in CALLEES if fn != case['fn']}
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
        d = BEATS[beat]
        IMAGES[beat] = ((d / 'eeMemory.bin').read_bytes(), (d / 'scratchpad.bin').read_bytes())
    return IMAGES[beat]


def prepared(case):
    """The case's starting RAM and scratchpad, in this process's reused
    oracle buffers (every use of the previous contents has ended)."""
    src_ram, src_spad = image(case['beat'])
    if 'oram' not in BUFFERS:
        BUFFERS['oram'], BUFFERS['ospad'] = bytearray(len(src_ram)), bytearray(len(src_spad))
    ram, spad = BUFFERS['oram'], BUFFERS['ospad']
    ram[:] = src_ram
    spad[:] = src_spad
    for a, data in case['writes']:
        if 0x70000000 <= a < 0x70004000:
            spad[a - 0x70000000:a - 0x70000000 + len(data)] = data
        else:
            ram[a:a + len(data)] = data
    return ram, spad


# ---- variants ------------------------------------------------------------

def is_code_word(size, v):
    return size == 4 and 0x100000 <= v < CODE_END


def poison_writes(case):
    """'poison': every byte the translated routines store before they (or
    anything they read through) load it starts as the complement of the
    value it ends with. Bytes they load before storing keep their value."""
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
    """'prestore': {stub index k: [(address, bytes)]}: at the k-th stub every
    byte the translated routines store after it (before the next stub) is
    set to the complement of the value they store there."""
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
    """'edge-byte': the byte after every byte / halfword the translated
    routines load or store (when they access that byte in no other way) set
    to 0xA5. 'edge-top': the top bit of every byte / halfword they load
    flipped in the starting memory."""
    ram, spad = prepared(case)
    stored = {}
    ee = oracle_run(case, ram, spad, rec=True, stored=stored)[0]
    loads = [(a, n) for a, n, v, idx, pc in ee.rec]
    sites = sorted(ee.store_sites)
    touched = set(stored)
    for a, n in loads:
        touched.update(range(a, a + n))
    out = {}
    if not top:
        for a, n in sites + loads:
            # (the byte after the last byte of RAM / the scratchpad is no memory)
            if n in (1, 2) and a + n not in touched and a + n not in (0x2000000, 0x70004000):
                out[a + n] = 0xA5
    else:
        ram0, spad0 = prepared(case)
        for a, n in loads:
            if n in (1, 2) and a + n - 1 not in out:
                t = a + n - 1
                b = spad0[t - 0x70000000] if t >= 0x70000000 else ram0[t & 0x1FFFFFF]
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


# ---- one case ----------------------------------------------------------

def run_case(case):
    """Returns (branch outcomes, calls, fx bytes) or None when a variant is
    not comparable. Raises on any difference."""
    fx = None
    if case.get('poison'):
        case = dict(case, writes=case['writes'] + poison_writes(dict(case, poison=False)))
    if case.get('edge'):
        case = dict(case, writes=case['writes'] + edge_writes(dict(case, edge=None), case['edge'] == 'top'))
    if case.get('fx'):
        fx = Perturb(fx_plan(case, case['fx'] == 'before'), case['fx'] == 'before')
    elif case.get('prestore'):
        fx = Fixed(prestore_plan(dict(case, prestore=False)))
    ram, spad = prepared(case)
    nat = NativeRun(ram, spad, Script(case['script']), set(case.get('forced', ())))
    native_lines()
    where = (case['name'],)
    try:
        ee, olog, ov0, entries, last = oracle_run(case, ram, spad, on_stub=fx)
    except AssertionError as e:
        if (case.get('fx') or case.get('edge') or case.get('poison') or case.get('prestore')) and e.args and \
                isinstance(e.args[0], tuple) and e.args[0][:1] == ('address',):
            return None     # the variant sent the original outside the 32 MiB: not comparable
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


def W8(a, v): return (a, struct.pack('<B', v & 0xFF))
def W16(a, v): return (a, struct.pack('<H', v & 0xFFFF))
def W32(a, v): return (a, struct.pack('<I', v & MASK))
def WF(a, v): return (a, struct.pack('<f', v))
def FB(v): return struct.unpack('<I', struct.pack('<f', v))[0]


def case(name, fn, args, beat='a05', script=None, writes=(), pin=False):
    return dict(name=name, fn=fn, beat=beat, args=list(args), script=script or {}, writes=list(writes),
                fargs=[], forced=[], pin=pin)


def vec(rng, lo=-50.0, hi=50.0, w=1.0):
    return [rng.uniform(lo, hi) for _ in range(3)] + [w]


def vwrites(a, v):
    return [WF(a + 4 * i, x) for i, x in enumerate(v)]


def sw(node, st, sub=None):
    w = [W8(node + 4, st)]
    if sub is not None:
        w.append(W8(node + 5, sub))
    return w


# Captured nodes (docs/AREA02_MATH.md section 4.1; pool addresses of the
# end-of-beat images, docs/FOURTH_LEVEL_ROUTE.md section 1.1).
BED, R1, R2 = 0x7A5F10, 0x7A6200, 0x7A64F0                    # 'bed': 00159620, 001C4AF0, 00159970
LOCK, PANEL, EMIT2 = 0x7AD780, 0x7AE050, 0x7AEF00             # 'a00': 001582E0, 00158EC0, 001C4CB0
G4960 = (0x7B23E0, 0x7B26D0, 0x7B29C0)                        # 'a00': 001C4960
KEYED, PLAIN = 0x7B3290, 0x7B4140                             # 'a05': 001BD560 (+3 0x0B, 0x0D)
DOOR_A, DOOR_B = 0x7B3870, 0x7B3B60                           # 'a05': 001BC960 (+3 0, 1)
E70 = 0x7B3E50                                                 # 'a05': 00159E70
DF_A, DF_B = 0x7B4720, 0x7B4A10                               # 'a05': 001BDFC0 (+3 0, 1)
C48, EMIT3, G4960_5, AF60_5 = 0x7B6D50, 0x7B84D0, 0x7B87C0, 0x7B26D0   # 'a05'
UPPER = 1 << 32         # a v0 with only its upper word set
SCR = SCRATCH
MASKB = 0x810841


def mask_writes(area, bitset, k):
    """The room-mask byte of `area` with bit k (mod 32) set or clear (all
    other bits the opposite)."""
    b = (1 << (k & 31)) & 0xFF
    v = (0xFF & ~b) if not bitset else (b | 0x00)
    return [W8(0x810700, area), W8(MASKB + area, v)]


def cases_simple_states(fn, node, beat, name, stubs, extra=()):
    """States 0..5 and 0xFF of a small behaviour, with the listed stub
    results for state 0."""
    out = []
    for st in (0, 1, 2, 3, 4, 5, 0xFF):
        results = stubs if st == 0 else (0,)
        for res in results:
            out.append(case(f'{name} st {st:x} r {res:#x}', fn, [node], beat=beat, writes=sw(node, st),
                            script={s: [res] for s in extra} if st == 0 else {}, pin=st < 5 and res in (0, 1)))
    return out


def cases_1575E0(rng):
    out = []
    for n, node in enumerate((BED, R1, 0x7A5640)):
        out.append(case(f'1575E0 node {n}', 0x1575E0, [node], beat='bed', pin=n == 0))
    for n, y in enumerate((0.0, -11.0, 1e-7, -1e-7, 3.4e38, 1e-40)):
        w = [WF(0x810350, 3.5 + n), WF(0x810354, y), WF(0x810358, -2.25)]
        out.append(case(f'1575E0 y {n}', 0x1575E0, [BED], beat='bed', writes=w, pin=n in (1, 2)))
    w = [W32(0x810354, 0x7F800000), W32(0x810350, 0xFF800001)]
    out.append(case('1575E0 exponent 255', 0x1575E0, [BED], beat='bed', writes=w, pin=True))
    # the node aliasing the destination (node + 0xD0 read by 001026A0 after the stores)
    out.append(case('1575E0 node at 0x8105D0-0xD0', 0x1575E0, [0x8105D0 - 0xD0], beat='bed', pin=True))
    return out


def use_writes(node, bits, c84, f5c, f58):
    return [W8(node + 0x0B, bits), W8(0x810C84, c84), W32(0x81085C, f5c), W32(0x810858, f58)]


F100 = FB(100.0)
USE_FLOATS = ((0, F100), (0, FB(250.0)), (0, FB(99.99999)), (0, 0x42C80001), (0x80000000, F100), (FB(1.0), F100),
              (0x00000001, F100), (0, 0x80000000), (0, 0x7F800000), (0, 0xFF800000))


def cases_157B30(rng):
    out = []
    s = BED + 0x1F0
    for bits in (0, 4, 5, 1, 0xFB, 0xFF, 0x04 | 0x80):
        for c84 in (0, 1, 0x80):
            for n, (f5c, f58) in enumerate(USE_FLOATS):
                if not bits & 4 and (c84 or n):
                    continue
                if c84 == 0 and n:
                    continue
                w = use_writes(BED, bits, c84, f5c, f58)
                out.append(case(f'157B30 b {bits:x} c84 {c84:x} f {n}', 0x157B30, [BED, s], beat='bed', writes=w,
                                pin=bits in (4, 5, 0xFF) and c84 in (0, 1) and n in (0, 1, 2, 3, 4, 7)))
    # a1 other than node + 0x1F0, and D_00810350 near the node (the yaw)
    w = use_writes(BED, 5, 1, 0, F100) + vwrites(0x810350, [75.0, 0.0, -188.0, 1.0])
    out.append(case('157B30 a1 scratch', 0x157B30, [BED, SCR + 0x40], beat='bed', writes=w, pin=True))
    return out


def cases_157F30(rng):
    out = []
    for n, node in enumerate((BED, R1)):
        for v in (0, 0x12345678, 0xFFFFFFFF):
            out.append(case(f'157F30 {n} {v:x}', 0x157F30, [node], beat='bed', writes=[W32(node + 0x14, v)],
                            pin=n == 0))
    # the node at D_008106B0 - 0x14 (its +0x14 word is the byte just stored)
    out.append(case('157F30 alias', 0x157F30, [0x8106B0 - 0x14], beat='bed', writes=[W32(0x8106B0, 0x11223344)],
                    pin=True))
    return out


def cases_158050(rng):
    out = []
    for n, node in enumerate((BED, R1, R2)):
        out.append(case(f'158050 node {n}', 0x158050, [node], beat='bed', pin=n == 0))
    a, b, c = SCR + 0x100, SCR + 0x400, SCR + 0x700
    w = [W32(a + 0x1C, b), W32(b + 0x1C, c)] + vwrites(c + 0xB0, vec(rng, w=-3.0))
    out.append(case('158050 chain', 0x158050, [a], beat='bed', writes=w, pin=True))
    # the record overlapping the scratch vector: c + 0xB0 = 0x700038A4
    c2 = 0x700038A4 - 0xB0
    w = [W32(a + 0x1C, b), W32(b + 0x1C, c2)] + vwrites(0x700038A0, [1.0, 2.0, 3.0, 4.0, 5.0])
    out.append(case('158050 overlap', 0x158050, [a], beat='bed', writes=w, pin=True))
    return out


def cases_1582E0(rng):
    out = []
    node = LOCK
    for k in (0, 3, 7, 8, 0x13, 0x1F, 0x21, 0x103):
        for bitset in (0, 1):
            w = sw(node, 0) + mask_writes(2, bitset, k) + [W16(node + 0x2E, k)]
            out.append(case(f'1582E0 st0 k {k:x} set {bitset}', 0x1582E0, [node], beat='a00', writes=w,
                            pin=k in (3, 8, 0x21)))
    for h in (0, 1, 0x100, -0x8000):
        for k in (3, 9, 0x23):
            for f842 in (0x81, 0x40):
                w = sw(node, 1) + [W16(node + 0x36, h), W16(node + 0x2E, 0x1100 | k), W8(0x810842, f842),
                                   W8(0x810700, 2), W8(MASKB + 2, 0x10)]
                out.append(case(f'1582E0 st1 h {h:x} k {k:x} 842 {f842:x}', 0x1582E0, [node], beat='a00', writes=w,
                                pin=h in (0, 0x100) and k in (3, 0x23)))
    for st in (2, 3, 4, 0xFF):
        out.append(case(f'1582E0 st {st:x}', 0x1582E0, [node], beat='a00', writes=sw(node, st), pin=True))
    for beat in ('a00', 'a04'):
        out.append(case(f'1582E0 captured {beat}', 0x1582E0, [node], beat=beat, pin=True))
    return out


R_USE = (0, 1, 2, 3, 4, -1, UPPER, 2 | UPPER)


def cases_158EC0(rng):
    out = []
    node = PANEL
    # state 0
    for r0 in (0, 1, UPPER):
        for bitset in (0, 1):
            for t in (0x14, 0x22, 0x15, 0x94):
                if r0 and (bitset or t != 0x14):
                    continue
                if bitset and t != 0x14:
                    continue
                w = sw(node, 0) + mask_writes(2, bitset, 5) + [W16(node + 0x2E, 5), W8(node + 3, t),
                                                                 W32(node + 0x20, 0x5A5A5A5A)]
                out.append(case(f'158EC0 st0 r {r0:#x} set {bitset} t {t:x}', 0x158EC0, [node], beat='a00',
                                writes=w, script={0x1B0FD0: [r0], 0x1C5570: [0x7B1234 if t != 0x94 else -5]},
                                pin=t in (0x14, 0x22)))
    # state 1
    for sub in range(9):
        for r in (R_USE if sub == 0 else (0, 1, UPPER)):
            for a in ((0, 1) if sub in (1, 6) else (0,)):
                for h in ((0, SCR + 0x200) if sub == 2 else (0,)):
                    w = sw(node, 1, sub) + [W8(node + 0x0A, a), W32(node + 0x20, h), W8(SCR + 0x204, 0x77)]
                    script = {0x157860: [r]} if sub == 0 else {0x1BA1F0: [r]}
                    out.append(case(f'158EC0 st1 sub {sub} r {r:#x} a {a} h {h:x}', 0x158EC0, [node], beat='a00',
                                    writes=w, script=script, pin=r in (0, 1, 2, 3) and sub < 8))
    for st in (2, 3, 4, 0xFF):
        out.append(case(f'158EC0 st {st:x}', 0x158EC0, [node], beat='a00', writes=sw(node, st), pin=True))
    out.append(case('158EC0 captured a04', 0x158EC0, [node], beat='a04', pin=True))
    return out


def cases_159620(rng):
    out = []
    node = BED
    for r0 in (0, 1, UPPER):
        out.append(case(f'159620 st0 r {r0:#x}', 0x159620, [node], beat='bed', writes=sw(node, 0),
                        script={0x1B0FD0: [r0]}, pin=True))
    # sub-state 0 runs 00157B30 (translated): each of its results
    for n, (bits, c84, f5c, f58) in enumerate(((0, 1, 0, F100), (4, 0, 0, F100), (5, 1, 0, F100),
                                              (4, 1, 0, 0x42C00000), (5, 1, FB(1.0), F100), (4, 1, FB(1.0), F100),
                                              (4, 1, 0, F100))):
        w = sw(node, 1, 0) + use_writes(node, bits, c84, f5c, f58)
        out.append(case(f'159620 st1 use {n}', 0x159620, [node], beat='bed', writes=w, pin=True))
    for sub in range(1, 9):
        for r in (0, 1, UPPER):
            for a in ((0, 1) if sub in (1, 6) else (0,)):
                if sub in (1, 3, 6, 7, 8) and r:
                    continue
                w = sw(node, 1, sub) + [W8(node + 0x0A, a)]
                out.append(case(f'159620 st1 sub {sub} r {r:#x} a {a}', 0x159620, [node], beat='bed', writes=w,
                                script={0x1BA1F0: [r]}, pin=r in (0, 1)))
    for st in (2, 3, 4, 0xFF):
        out.append(case(f'159620 st {st:x}', 0x159620, [node], beat='bed', writes=sw(node, st), pin=True))
    out.append(case('159620 captured', 0x159620, [node], beat='bed', pin=True))
    return out


def cases_159970(rng):
    out = []
    node = R2
    for r0 in (0, 1, UPPER):
        for b2 in (0x86, 0x06, 0x26, 0xE6, 0x07, 0x16, 0x00):
            if r0 and b2 != 0x86:
                continue
            w = sw(node, 0) + [W8(node + 2, b2)]
            out.append(case(f'159970 st0 r {r0:#x} b2 {b2:x}', 0x159970, [node], beat='bed', writes=w,
                            script={0x1B0FD0: [r0]}, pin=b2 in (0x86, 0x06, 0x16)))
    for sub in range(6):
        for r in (R_USE if sub == 0 else (0, 1, UPPER)):
            if sub in (1, 4, 5) and r:
                continue
            script = {0x157CE0: [r]} if sub == 0 else {0x1BA1F0: [r]}
            out.append(case(f'159970 st1 sub {sub} r {r:#x}', 0x159970, [node], beat='bed',
                            writes=sw(node, 1, sub), script=script, pin=r in (0, 1, 2, 3)))
    for st in (2, 3, 4, 0xFF):
        out.append(case(f'159970 st {st:x}', 0x159970, [node], beat='bed', writes=sw(node, st), pin=True))
    return out


def cases_159E70(rng):
    out = []
    node = E70
    for r0 in (0, 1, UPPER):
        for area in (4, 2, 0x84):
            if r0 and area != 4:
                continue
            w = sw(node, 0) + [W8(0x810700, area), W16(node + 0x28, 0x5A5A)]
            out.append(case(f'159E70 st0 r {r0:#x} area {area:x}', 0x159E70, [node], writes=w,
                            script={0x1B0FD0: [r0]}, pin=True))
    for idx in (0x29, 0x2A, -1, 0x1FF, -0x100):
        for cnt in (0, 1, 0x80):
            for k in (2, 9, 0x22):
                if idx not in (0x29,) and k != 2:
                    continue
                w = sw(node, 1, 0) + [W16(node + 0x28, idx), W8(0x810C64 + idx, cnt), W8(node + 0x2E, k),
                                      W8(0x810700, 4), W8(MASKB + 4, 0x40)]
                out.append(case(f'159E70 st1 idx {idx:x} cnt {cnt:x} k {k:x}', 0x159E70, [node], writes=w,
                                pin=idx == 0x29 and k in (2, 9)))
    for sub in (1, 2, 0xFF):
        out.append(case(f'159E70 st1 sub {sub:x}', 0x159E70, [node], writes=sw(node, 1, sub), pin=True))
    for st in (2, 3, 4, 0xFF):
        out.append(case(f'159E70 st {st:x}', 0x159E70, [node], writes=sw(node, st), pin=True))
    out.append(case('159E70 captured', 0x159E70, [node], pin=True))
    # the pose record's +0x78 word aliasing the node's +0x2E byte (re-read after the store)
    p4 = 0x8102F4
    w = sw(node, 1, 0) + [W16(node + 0x28, 0x29), W8(0x810C64 + 0x29, 1), W32(p4, node + 0x2E - 0x78 - 2),
                          W8(0x810700, 4)]
    out.append(case('159E70 alias 2E', 0x159E70, [node], writes=w, pin=True))
    return out


def cases_183C40(rng):
    out = []
    p, o = SCR + 0x100, SCR + 0x800
    recs = {slot: SCR + 0x1000 + 0x100 * i for i, slot in enumerate((0x118, 0x11C, 0x120, 0x124, 0x130, 0x148))}
    base = vwrites(p + 0xB0, vec(rng)) + [W32(p + 0x110, SCR + 0x2000)]
    for slot, a in recs.items():
        base += [W32(p + slot, a)] + vwrites(a + 0xC0, vec(rng))
    for i in range(16):
        base.append(WF(SCR + 0x2090 + 4 * i, rng.uniform(-2, 2)))
    base += vwrites(o, [9.0, 9.0, 9.0, 9.0])
    for t2 in (2, 0x22, 0xE2, 0x12, 3, 0x42):
        for t3 in list(range(20)) + [0x7F, 0xFF]:
            if t2 not in (2, 0xE2) and t3 not in (0, 16, 18):
                continue
            w = base + [W8(p + 2, t2), W8(p + 3, t3)]
            out.append(case(f'183C40 t {t2:x} {t3}', 0x183C40, [p, o], beat='a02', writes=w,
                            pin=t2 == 2 or t3 in (0, 18)))
    # out overlapping the record: out + 4 = p + 0xB4 (case 16 stores x before it reads y)
    for t3 in (16, 0, 9):
        for d in (0xB0, 0xB4):
            w = base + [W8(p + 2, 2), W8(p + 3, t3)]
            out.append(case(f'183C40 overlap {t3} {d:x}', 0x183C40, [p, p + d], beat='a02', writes=w, pin=True))
    # out on the slot pointer of +0x118: the x stored there is the address of
    # another record, which the per-component re-read then follows
    alt = SCR + 0x1800
    w = base + [W8(p + 2, 2), W8(p + 3, 0), W32(recs[0x118] + 0xC0, alt)] + vwrites(alt + 0xC0, vec(rng))
    out.append(case('183C40 slot alias', 0x183C40, [p, p + 0x118], beat='a02', writes=w, pin=True))
    w = base + [W8(p + 2, 2), W8(p + 3, 16), WF(p + 0xB4, 1e-8)]
    out.append(case('183C40 16 small y', 0x183C40, [p, o], beat='a02', writes=w, pin=True))
    return out


def cases_1B18F0(rng):
    out = []
    a1, a2 = SCR + 0x100, SCR + 0x110
    for n, node in enumerate((LOCK, PANEL)):
        for res in ([0, 0, 0, 0], [1], [0, 1], [0, 0, 1], [0, 0, 0, 1], [0x100, 0x200, 0x300, 0x400],
                    [0x100, 0x101], [UPPER, 0, 0, 0xFF], [-1]):
            w = vwrites(a1, vec(rng, -2, 2)) + vwrites(a2, vec(rng, -2, 2)) + [W8(node + 1, 0x5A)]
            out.append(case(f'1B18F0 {n} {res}', 0x1B18F0, [node, a1, a2], beat='a00', writes=w,
                            script={0x1B1630: res}, pin=n == 0))
    for n, bits in enumerate((0x80000000, 0x00000000, 0x7F800000, 0xFF800001, 0x00000001)):
        w = [W32(a1 + 4 * i, bits) for i in range(3)] + [W32(a2 + 4 * i, bits ^ 0x80000000) for i in range(3)]
        out.append(case(f'1B18F0 bits {bits:x}', 0x1B18F0, [LOCK, a1, a2], beat='a00', writes=w,
                        script={0x1B1630: [0, 0, 0, 0]}, pin=n < 3))
    # a1 inside the scratch vector (re-read after each store) and a2 == a1
    w = vwrites(0x70003600, [1.0, 2.0, 3.0, 4.0]) + vwrites(a2, vec(rng))
    out.append(case('1B18F0 a1 at 0x70003604', 0x1B18F0, [LOCK, 0x70003604, a2], beat='a00', writes=w,
                    script={0x1B1630: [0, 0, 0, 0]}, pin=True))
    out.append(case('1B18F0 a2 = a1', 0x1B18F0, [LOCK, a1, a1], beat='a00', writes=vwrites(a1, vec(rng)),
                    script={0x1B1630: [0, 0, 0, 0]}, pin=True))
    return out


def chain_writes(node, b1, b2):
    """node +0x18 -> e; e +0x18 -> q1 (byte +0x0B = b1); q1 +0x18 -> q2 (byte
    +0x0B = b2)."""
    e, q1, q2 = SCR + 0x3000, SCR + 0x3400, SCR + 0x3800
    return [W32(node + 0x18, e), W32(e + 0x18, q1), W8(q1 + 0x0B, b1), W32(q1 + 0x18, q2), W8(q2 + 0x0B, b2),
            W8(q1 + 0x0A, 0x5A), W8(q2 + 0x0C, 0x5A)]


def door_cases(fn, nodes, name, story):
    out = []
    # state 0
    combos = ((0x0D, 0), (4, 7), (4, 0), (7, 0), (8, 0), (4, 0x0A), (2, 1), (1, 0), (7, 1), (0x0D, 1), (8, 1))
    for n, node in enumerate(nodes):
        for h in (0xFF, 5, 0x1FF):
            for t3 in (0, 1, 2):
                for area, room in combos:
                    for f76a in ((0, 0xFF, 3) if (area == 4 and h != 0xFF and not story) else (0,)):
                        w = sw(node, 0) + [W16(node + 0x2E, h), W8(node + 3, t3), W8(0x810700, area),
                                           W8(0x810702, room), W8(0x81076A, f76a)]
                        out.append(case(f'{name} st0 {n} h {h:x} t3 {t3} a {area:x} r {room:x} 76a {f76a:x}', fn,
                                        [node], writes=w, pin=n == 0 and h != 0x1FF and t3 < 2 and
                                        (area, room) in ((0x0D, 0), (4, 7), (4, 0), (7, 0), (4, 0x0A), (2, 1))))
    # state 1
    for n, node in enumerate(nodes):
        for sub in range(9):
            for h in (0xFF, 5):
                for t3 in (0, 1):
                    for bitset in (0, 1):
                        for b1, b2 in ((0, 3), (1, 2), (0, 0), (3, 3)):
                            for r in (0, 1):
                                if sub != 0 and (bitset or (b1, b2) not in ((0, 3), (1, 2))):
                                    continue
                                if sub not in (0, 1) and r:
                                    continue
                                if sub in (5, 7, 8) and (h != 5 or t3):
                                    continue
                                w = sw(node, 1, sub) + [W16(node + 0x2E, h), W8(node + 3, t3)]
                                w += mask_writes(4, bitset, h) + chain_writes(node, b1, b2)
                                w += [W8(0x81083E, 1 - bitset), W8(0x8107D8 + 0x5E, bitset),
                                      W8(0x8107D8 + 0x5F, 1 - bitset), W16(node + 0x28, 0x5A5A)]
                                script = {0x1BC740: [r], 0x1BA1F0: [r]}
                                out.append(case(f'{name} st1 {n} sub {sub} h {h:x} t3 {t3} set {bitset} b {b1}{b2} '
                                                f'r {r}', fn, [node], writes=w, script=script,
                                                pin=n == 0 and b1 == 0 and (sub > 1 or bitset == 1 or h == 0xFF)))
        # the 0xFF test reads the halfword; byte +3 other than 0 / 1
        for sub in (0, 1, 2):
            for h, t3 in ((0x1FF, 0), (0x1FF, 1), (0xFF, 2), (5, 2)):
                for bitset in (0, 1):
                    w = sw(node, 1, sub) + [W16(node + 0x2E, h), W8(node + 3, t3)] + mask_writes(4, bitset, h)
                    w += chain_writes(node, 0, 0) + [W8(0x81083E, bitset), W8(0x8107D8 + 0x5E, bitset)]
                    out.append(case(f'{name} st1 {n} sub {sub} h {h:x} t3 {t3} set {bitset} wide', fn, [node],
                                    writes=w, script={0x1BC740: [1], 0x1BA1F0: [1]}, pin=n == 0 and bitset == 0))
        # sub-state 2's flag in each area
        for area in (4, 7, 2):
            for flag in (0, 1):
                for h in (0xFF, 5):
                    w = sw(node, 1, 2) + [W16(node + 0x2E, h), W8(0x810700, area), W8(0x81083E, flag),
                                          W8(0x8107D8 + 0x5E, flag), W8(0x8107D8 + 0x5F, 1 - flag)]
                    out.append(case(f'{name} st1 sub2 area {area} flag {flag} h {h:x} n {n}', fn, [node], writes=w,
                                    pin=n == 0))
        # sub-state 3's timer
        for t in (1, 2, 0, -0x8000, 0x7FFF):
            w = sw(node, 1, 3) + [W16(node + 0x28, t)]
            out.append(case(f'{name} st1 sub3 t {t:x} n {n}', fn, [node], writes=w, pin=n == 0 and t in (1, 0)))
    # the other states
    for n, node in enumerate(nodes):
        for st in (2, 3, 4, 5, 0xFF):
            for r in ((0, 1, UPPER) if st == 4 else (0,)):
                out.append(case(f'{name} st {st:x} r {r:#x} n {n}', fn, [node], writes=sw(node, st),
                                script={0x1BA1F0: [r]}, pin=n == 0))
        out.append(case(f'{name} captured {n}', fn, [node], pin=True))
    return out


def cases_1BC960(rng):
    out = door_cases(0x1BC960, (DOOR_A, DOOR_B), '1BC960', False)
    node = DOOR_A
    # state 2
    for bitset in (0, 1):
        for b84 in (0x208, 0x209, 0x1208):
            w = sw(node, 2, 0) + mask_writes(4, bitset, 5) + [W16(node + 0x2E, 5), W16(0x70003B84, b84)]
            w += chain_writes(node, 0, 0)
            out.append(case(f'1BC960 st2 sub0 set {bitset} b84 {b84:x}', 0x1BC960, [node], writes=w,
                            pin=b84 != 0x1208))
    rec = SCR + 0x2000
    for bitset in (0, 1):
        for f76a in (0xFF, 0):
            for f7ea in (0x10, 0x11):
                for recp, b4, dist in ((0, 0, 0.0), (rec, 2, 0.0), (rec, 3, 0.0), (rec, 1, 553.0),
                                       (rec, 0, 554.0), (rec, 1, 555.0), (rec, 0, -1e9)):
                    for r in (0, 1):
                        if (not bitset or f76a == 0xFF or f7ea != 0x10) and (recp or r):
                            continue
                        w = sw(node, 2, 1) + mask_writes(4, bitset, 5) + [W16(node + 0x2E, 5), W8(0x81076A, f76a),
                                                                          W8(0x8107EA, f7ea), W32(0x8106C0, recp),
                                                                          W8(rec + 4, b4), WF(rec + 0xB0, dist)]
                        out.append(case(f'1BC960 st2 sub1 set {bitset} 76a {f76a:x} 7ea {f7ea:x} rec {recp:x} '
                                        f'{b4} {dist} r {r}', 0x1BC960, [node], writes=w, script={0x1BC740: [r]},
                                        pin=bitset and f76a == 0 and f7ea == 0x10))
    for sub in (2, 3, 0xFF):
        for r in (0, 1):
            out.append(case(f'1BC960 st2 sub {sub:x} r {r}', 0x1BC960, [node], writes=sw(node, 2, sub),
                            script={0x1BC740: [r]}, pin=True))
    return out


def cases_1BDFC0(rng):
    return door_cases(0x1BDFC0, (DF_A, DF_B), '1BDFC0', True)


def cases_1BD560(rng):
    out = []
    for n, node in enumerate((KEYED, PLAIN)):
        for flags in (0x40, 0x80, 0xC0, 0, 0xFF3F, -0x8000 | 0x40):
            w = sw(node, 0) + [W16(node + 0x56, flags), W16(node + 0x2E, 0x1234), W16(node + 0x34, 0x5A5A)]
            out.append(case(f'1BD560 st0 {n} f {flags:x}', 0x1BD560, [node], writes=w, pin=n == 0))
    own = SCR + 0x2000
    for n, (node, t3) in enumerate(((KEYED, 0x0B), (PLAIN, 0x0D), (KEYED, 0x8B))):
        for sub in range(10):
            for b in (2, 4, 0, 3):
                for r in (0, 1, UPPER):
                    for f76c in (0, 1, 0xFF):
                        for area in (4, 7, 2):
                            if sub not in (0, 2) and b not in (0, 3):
                                continue
                            if sub in (0, 2) and r:
                                continue
                            if sub not in (3, 7) and f76c:
                                continue
                            if not (sub == 4 and t3 != 0x0B) and area != 4:
                                continue
                            if sub in (5, 6, 8, 9) and (r or b):
                                continue
                            w = sw(node, 1, sub) + [W8(node + 3, t3), W8(node + 0x0B, b), W8(0x81076C, f76c),
                                                    W8(0x81083E, 1), W8(0x810836, 0), W8(0x810837, 1),
                                                    W8(0x810700, area), W32(node + 0x114, own), WF(own + 0x80, 30.0),
                                                    W32(0x70003258, 0x5A)]
                            out.append(case(f'1BD560 st1 {n} sub {sub} b {b} r {r:#x} 76c {f76c:x} area {area}',
                                            0x1BD560, [node], writes=w,
                                            script={s: [r] for s in (0x1BD180, 0x1BD270, 0x1BD370, 0x1BD460,
                                                                     0x1BA1F0)},
                                            pin=n < 2 and r in (0, 1) and sub < 8))
        # the toggles 1 - x of D_0081083E / D_00810836 / D_00810837 over other bytes
        for sub in ((4, 7) if t3 == 0x0B else (4,)):
            for v in (0, 2, 0xFF):
                w = sw(node, 1, sub) + [W8(node + 3, t3), W8(0x81083E, v), W8(0x810836, v), W8(0x810837, v ^ 2),
                                        W8(0x81076C, 0xFF), W8(0x810700, 4), W32(node + 0x114, own),
                                        WF(own + 0x80, 30.0)]
                out.append(case(f'1BD560 toggle {n} sub {sub} v {v:x}', 0x1BD560, [node], writes=w,
                                script={0x1BA1F0: [1]}, pin=n == 0 and v == 2))
        # the distance test after the method
        for d in (17.0, 18.0, FB(18.0) - 1, 19.0, -0.0):
            for b in (0, 3):
                dv = [W32(own + 0x80, d)] if isinstance(d, int) else [WF(own + 0x80, d)]
                w = sw(node, 1, 6) + [W8(node + 3, t3), W8(node + 0x0B, b), W32(node + 0x114, own)] + dv
                out.append(case(f'1BD560 dist {n} {d} b {b}', 0x1BD560, [node], writes=w, pin=n == 0))
    for st in (2, 3, 4, 0xFF):
        out.append(case(f'1BD560 st {st:x}', 0x1BD560, [KEYED], writes=sw(KEYED, st), pin=True))
    for n, node in enumerate((KEYED, PLAIN)):
        out.append(case(f'1BD560 captured {n}', 0x1BD560, [node], pin=True))
    return out


def cases_1C48C0(rng):
    return cases_simple_states(0x1C48C0, C48, 'a05', '1C48C0', (0, 1, UPPER), (0x1B0FD0,))


def cases_1C4960(rng):
    out = []
    for n, (node, beat) in enumerate(((G4960[0], 'a00'), (G4960_5, 'a05'))):
        for st in (0, 1, 2, 3, 4, 0xFF):
            for r in ((0, 1, UPPER) if st == 0 else (0,)):
                for d in ((0, 0x80, 0xFF) if st == 0 and r == 0 else (0,)):
                    w = sw(node, st) + ([W8(node + 0x0D, d)] if st == 0 else [])
                    out.append(case(f'1C4960 {n} st {st:x} r {r:#x} d {d:x}', 0x1C4960, [node], beat=beat, writes=w,
                                    script={0x1B1020: [r]}, pin=n == 0))
    return out


def cases_1C4AF0(rng):
    out = cases_simple_states(0x1C4AF0, R1, 'bed', '1C4AF0', (0, 1, UPPER), (0x1B0FD0,))
    out.append(case('1C4AF0 captured', 0x1C4AF0, [R1], beat='bed', pin=True))
    return out


def cases_1C4CB0(rng):
    out = []
    node = EMIT2
    q = SCR + 0x2000
    for t3 in (2, 3, 4, 5, 0, 0x82):
        for r in ((0, 1, UPPER) if t3 in (2, 3) else (0,)):
            for f761 in ((1, 0, 2, 0x81) if t3 == 2 and r == 0 else (1,)):
                for h in ((0, 1, 0x8000) if t3 == 4 else (0,)):
                    w = sw(node, 1) + [W8(node + 3, t3), W8(0x810761, f761), W32(node + 0x18, q), W16(q + 0x2E, h)]
                    for i in range(16):
                        w.append(WF(q + 0xD0 + 4 * i, rng.uniform(-3, 3)))
                    out.append(case(f'1C4CB0 t3 {t3:x} r {r:#x} 761 {f761:x} h {h:x}', 0x1C4CB0, [node], beat='a00',
                                    writes=w, script={0x1BA1C0: [r]}, pin=f761 in (0, 1) and r in (0, 1)))
    for st in (0, 2, 3, 4, 0xFF):
        out.append(case(f'1C4CB0 st {st:x}', 0x1C4CB0, [node], beat='a00', writes=sw(node, st), pin=True))
    out.append(case('1C4CB0 captured a05', 0x1C4CB0, [EMIT3], beat='a05', pin=True))
    # the table overlapping the scratch vector (each value read before its store)
    return out


POSE = 0xD689C0      # word at 0x8102F0 in every a01r / a02 capture (checked in main)


def ev_node(extra=()):
    n = SCR + 0x4000
    return n, [W32(n + 0x4C, 0x1CAA00)] + list(extra)


def cases_1C7EB0(rng):
    out = []
    n, base = ev_node()
    for lim in (731, 0, 1, -1, 3):
        for cb in (0, 0x1CAA00):
            w = base + sw(n, 0) + [W16(0x275BCC, lim), W32(n + 0x2EC, cb), W16(n + 0x94, 0x8123), W8(n + 9, 0x5A),
                                   W8(n + 0x0C, 0x77)]
            out.append(case(f'1C7EB0 st0 lim {lim} cb {cb:x}', 0x1C7EB0, [n], beat='r01', writes=w,
                            script={0x1AF780: [0x7A5640, 0x1234]}, pin=lim in (731, 0)))
    # the count byte changed by the allocator (re-read each time): a pool node at +0x0C
    w = base + sw(n, 0) + [W16(0x275BCC, 5)]
    out.append(case('1C7EB0 st0 count alias', 0x1C7EB0, [n], beat='r01', writes=w,
                    script={0x1AF780: [0x33, 0x44, 0x55]}, pin=True))
    rec = SCR + 0x5000
    scales_list = ((0.5, 0.5, 0.5), (0.95, 0.5, 0.5), (0.5, 0.9, 0.5), (0.5, 0.5, 0.9), (0.9, 0.9, 0.9),
                   (0.8999999, 0.8999999, 0.8999999))
    for recp, rh, h in ((0, 0, 0), (rec, 0, 3), (rec, 8, 3), (rec, 8, 0x23), (rec, 0xFFF7, 3), (rec, 0x8000, 0xF)):
        for t in (0, 1, 2, -5, 0x80000000):
            for nres in (0, 0x1000, 0x2000, 0x3000, 0x4FFF, 0x10000):
                for cb in (0, 0x1CAA00):
                    for si, sc in enumerate(scales_list):
                        if (recp and rh) and (t or nres or cb or si):
                            continue
                        if t not in (0, 1) and (nres or si):
                            continue
                        if nres and si:
                            continue
                        w = base + sw(n, 1) + [W32(n + 0x24, recp), W16(rec + 0x2E, rh), W16(n + 0x2E, h),
                                               W32(n + 0x1F0 + 0xF4, t), WF(n + 0x1F0 + 0xF8, 0.75),
                                               W32(n + 0x2EC, cb)]
                        w += [WF(POSE + 0x18 + 4 * i, v) for i, v in enumerate(sc)]
                        out.append(case(f'1C7EB0 st1 rec {recp:x} {rh:x} {h:x} t {t:x} n {nres:x} cb {cb:x} s {si}',
                                        0x1C7EB0, [n], beat='r01', writes=w, script={0x1C64F0: [nres]},
                                        pin=(t in (0, 1) and cb and si in (0, 1, 4)) or (recp and rh == 8)))
    # the pose quadwords overlapping the node (the copy order shows) and a
    # misaligned pose record
    for m, poff in enumerate((n - 0x90 + 0xE0, n + 0x1004, n - 0x80)):
        w = base + sw(n, 1) + [W32(n + 0x24, 0), W32(n + 0x1F0 + 0xF4, 1), W32(0x8102F0, poff)]
        for i in range(64):
            w.append(W32(n + 0xD0 + 4 * i, rng.getrandbits(32)))
        out.append(case(f'1C7EB0 st1 pose overlap {m}', 0x1C7EB0, [n], beat='r01', writes=w, pin=True))
    alt = SCR + 0x6000
    w = base + sw(n, 1) + [W32(n + 0x24, 0), W32(n + 0x1F0 + 0xF4, 1), W32(0x8102F0, alt + 4)]
    for i in range(0, 0x120, 4):
        w.append(W32(alt + i, rng.getrandbits(32)))
    out.append(case('1C7EB0 st1 pose misaligned', 0x1C7EB0, [n], beat='r01', writes=w, pin=True))
    # the node's +0xB0 is the pose pointer word 0x8102F0: its first store
    # (the pose's +0xC0, here an address) redirects the next pose reads
    n2 = 0x8102F0 - 0xB0
    # (the zero stores first clear that word, so the first pose read is record 0: its +0xC0 word is alt)
    w = sw(n2, 1) + [W32(n2 + 0x24, 0), W32(n2 + 0x1F0 + 0xF4, 1), W32(n2 + 0x2EC, 0), W32(0xC0, alt)]
    for i in range(0xC0, 0x120, 4):
        w.append(W32(alt + i, 0x3F000000 + i))
    out.append(case('1C7EB0 st1 pose pointer alias', 0x1C7EB0, [n2], beat='r01', writes=w, pin=True))
    for st in (2, 3, 4, 0xFF):
        out.append(case(f'1C7EB0 st {st:x}', 0x1C7EB0, [n], beat='r01', writes=base + sw(n, st), pin=True))
    return out


def cases_1C8140(rng):
    out = []
    n = SCR + 0x4000
    for res in (n, 0, UPPER, UPPER | n, 0x1FFFD00):
        for a1, a2 in ((0x12345, 0x825910), (-1, 0), (0x8000, 0x1CAA00)):
            w = [W32(n + i, 0xA5A5A5A5) for i in range(0, 0x300, 4)]
            out.append(case(f'1C8140 {res:#x} {a1:x}', 0x1C8140, [0x7B6000, a1, a2], beat='r01', writes=w,
                            script={0x1AFA90: [res]}, pin=res in (n, 0) or a1 == -1))
    return out


def cases_1CAE40(rng):
    out = []
    q = SCR + 0x2000
    for node, beat in ((R1, 'bed'), (AF60_5, 'a05')):
        for p44 in (0, q):
            for res in (0, 1, 2, 3, -1, 0x80000000, UPPER, UPPER | 1, 1 << 63):
                w = [W32(node + 0x44, p44), WF(q + 0x20, 12.5)]
                out.append(case(f'1CAE40 {beat} p44 {p44:x} r {res:#x}', 0x1CAE40, [node, 0x825000 | res & 0xF],
                                beat=beat, writes=w, script={0x1CA7B0: [res]}, pin=beat == 'bed' and res in (0, 1, 2, -1)))
    # the display record overlapping D_00275670's +0x1C word
    d = 0x811CC0
    w = [W32(d + 0x1C, d + 0x1C - 3)]
    out.append(case('1CAE40 record alias', 0x1CAE40, [R1, 7], beat='bed', writes=w, script={0x1CA7B0: [1]}, pin=True))
    return out


def cases_1CAF60(rng):
    out = []
    for node, beat in ((R1, 'bed'), (AF60_5, 'a05')):
        for res in (0, 1, -1):
            out.append(case(f'1CAF60 {beat} r {res}', 0x1CAF60, [node], beat=beat, script={0x1CA7B0: [res]},
                            pin=True))
    return out


def cases_1CB4F0(rng):
    out = []
    for res in (0, 0x12345678, (5 << 40) | 7, -1):
        for a0, a1 in ((BED, 0x825000), (R1, 0)):
            out.append(case(f'1CB4F0 {res:#x} {a0:x}', 0x1CB4F0, [a0, a1], beat='bed', script={0x1D2910: [res]},
                            pin=True))
    return out


def cases_1CB580(rng):
    return [case(f'1CB580 {node:x}', 0x1CB580, [node], beat='bed', script={0x1D2910: [(3 << 33) | 1]}, pin=True,
                 writes=[W32(node + 0x44, 0x7A5000 + i)]) for i, node in enumerate((BED, R1, R2))]


MAKERS = (cases_1575E0, cases_157B30, cases_157F30, cases_158050, cases_1582E0, cases_158EC0, cases_159620,
          cases_159970, cases_159E70, cases_183C40, cases_1B18F0, cases_1BC960, cases_1BDFC0, cases_1BD560,
          cases_1C48C0, cases_1C4960, cases_1C4AF0, cases_1C4CB0, cases_1C7EB0, cases_1C8140, cases_1CAE40,
          cases_1CAF60, cases_1CB4F0, cases_1CB580)

# Routines whose original makes no stubbed call: an fx / prestore variant of
# theirs equals the ordinary case.
NO_STUBS = {0x157F30, 0x158050, 0x183C40}


def all_cases():
    rng = random.Random(0xA02)
    base = []
    for maker in MAKERS:
        base += maker(rng)
    names = [c['name'] for c in base]
    assert len(names) == len(set(names)), 'duplicate case names'
    variants = []
    for c in base:
        vp = c['pin'] and c['name'] in VPIN
        if c['fn'] not in NO_STUBS:
            for kind in ('after', 'before'):
                variants.append(dict(c, name=f"{c['name']} fx-{kind}", fx=kind, pin=vp))
            variants.append(dict(c, name=f"{c['name']} prestore", prestore=True, pin=vp))
        variants.append(dict(c, name=f"{c['name']} poison", poison=True, pin=vp))
        for kind in ('byte', 'top'):
            variants.append(dict(c, name=f"{c['name']} edge-{kind}", edge=kind, pin=vp))
    for c in base + variants:
        if c['name'] in QUICK_EXTRA:
            c['pin'] = True
    return base, variants


# The base cases whose variants the default run includes (EM_TEST_FULL=1 runs
# every variant of every case).
VPIN = frozenset((
    '1575E0 node 0', '157B30 b 5 c84 1 f 0', '157F30 alias', '158050 overlap', '1582E0 st1 h 100 k 3 842 40',
    '158EC0 st1 sub 2 r 0x1 a 0 h 1e00200', '159620 st1 use 2', '159970 st1 sub 0 r 0x3', '159E70 st1 idx 29 cnt 1 k 9',
    '183C40 overlap 16 b4', '1B18F0 0 [0, 0, 1]', '1BC960 captured 0', '1BDFC0 captured 0', '1BD560 captured 0',
    '1C4960 0 st 0 r 0x0 d 80', '1C4CB0 t3 2 r 0x0 761 1 h 0', '1C7EB0 st1 pose overlap 0', '1C8140 0x1e04000 12345',
    '1CAE40 bed p44 1e02000 r 0x1', '1CB4F0 0x12345678 7a5f10',
))
# Base cases added to the default run so that it takes every conditional
# branch both ways (a greedy cover over the full sweep's outcomes).
QUICK_EXTRA = frozenset((
    # killers of the mutants of section 5 that the pinned set alone missed
    '1582E0 st0 k 7 set 1', '1582E0 st0 k 13 set 0', '158EC0 st1 sub 0 r 0x100000002 a 0 h 0',
    '159E70 st1 idx -1 cnt 1 k 2', '1BC960 st1 0 sub 0 h 5 t3 0 set 1 b 12 r 0 prestore',
    '1BC960 st2 sub0 set 1 b84 1208', '1C4CB0 t3 2 r 0x0 761 2 h 0', '1C7EB0 st0 lim 1 cb 1caa00',
    '1C7EB0 st0 lim 731 cb 1caa00 fx-after', '1C7EB0 st1 rec 0 0 0 t 0 n 0 cb 1caa00 s 2',
    '1CAE40 bed p44 0 r 0x8000000000000000',
    # branch cover
    '1BC960 st1 0 sub 1 h 5 t3 0 set 0 b 03 r 1', '1BDFC0 st0 0 h ff t3 1 a 8 r 0 76a 0',
    '1BDFC0 st1 0 sub 1 h 5 t3 0 set 0 b 03 r 1', '1C7EB0 st1 rec 0 0 0 t 0 n 0 cb 0 s 2', '158EC0 st0 r 0x0 set 0 t 15',
    '1BC960 st0 0 h ff t3 1 a d r 1 76a 0', '1BC960 st1 0 sub 0 h ff t3 0 set 0 b 12 r 0',
    '1BC960 st1 0 sub 0 h 5 t3 0 set 0 b 03 r 0', '1BC960 st1 0 sub 0 h 5 t3 0 set 1 b 12 r 0',
    '1BC960 st1 0 sub 1 h 5 t3 1 set 0 b 03 r 1', '1BC960 st1 0 sub 4 h ff t3 0 set 0 b 12 r 0',
    '1BC960 st1 0 sub 6 h ff t3 0 set 0 b 12 r 0', '1BC960 st2 sub1 set 1 76a ff 7ea 10 rec 0 0 0.0 r 0',
    '1BC960 st2 sub1 set 1 76a 0 7ea 11 rec 0 0 0.0 r 0', '1BDFC0 st0 0 h ff t3 1 a 7 r 1 76a 0',
    '1BDFC0 st0 0 h ff t3 1 a 8 r 1 76a 0', '1BDFC0 st1 0 sub 0 h ff t3 0 set 0 b 12 r 0',
    '1BDFC0 st1 0 sub 0 h 5 t3 0 set 1 b 12 r 0', '1BDFC0 st1 0 sub 1 h 5 t3 1 set 0 b 03 r 1',
    '1BDFC0 st1 0 sub 4 h ff t3 0 set 0 b 12 r 0', '1BDFC0 st1 0 sub 6 h ff t3 0 set 0 b 12 r 0',
    '1BD560 st1 0 sub 8 b 0 r 0x0 76c 0 area 4', '1C7EB0 st1 rec 0 0 0 t 0 n 0 cb 0 s 3',
    '1C7EB0 st1 rec 0 0 0 t 0 n 1000 cb 0 s 0',
))


# ======================================================================
# Static checks
# ======================================================================

def call_targets(mem):
    """Direct call (and jump) targets that leave the translated routines."""
    out = {}
    for lo, hi in FUNC_RANGES:
        for pc in range(lo, hi, 4):
            w = struct.unpack_from('<I', mem, pc)[0]
            if w >> 26 in (2, 3):
                t = (pc & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
                if not lo <= t < hi:
                    out.setdefault(t, set()).add(lo)
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


# Conditional branches whose other outcome no input of the full sweep takes,
# with the proof (docs/AREA02_MATH.md section 4).
UNREACHABLE = {}


def check_register_reads(mem):
    import test_area01_sys_reference as SYSREF    # RegScan, unchanged (read-only reuse)
    scan = SYSREF.RegScan(mem)
    bad, rows = [], []
    for fn, (kind, na, nf) in sorted(CALLEES.items()):
        ri, rf, notes = scan.reads(fn)
        ints = {r for r in range(36) if ri >> r & 1}
        floats = {r for r in range(SYSREF.VU_BASE + 152) if rf >> r & 1}
        extra_i = sorted(r for r in ints if not 4 <= r < 4 + na and r not in (0, 28, 29, 31)
                         and not (kind == S and SYSREF.SAVED_READ_MASK_I >> r & 1))
        extra_f = sorted(r for r in floats if not 12 <= r < 12 + nf and not (kind == S and 20 <= r < 32)
                         and r < SYSREF.VU_BASE)
        rows.append((hex(fn), kind, na, nf, sorted(ints), sorted(r for r in floats if r < SYSREF.VU_BASE),
                     sorted(str(x) for x in notes) if notes else ''))
        if extra_i or extra_f:
            bad.append((hex(fn), kind, 'reads', extra_i, extra_f, 'policy', na, nf))
        for note in notes or ():
            if note[1] not in SYSREF.UNRESOLVED_JUMPS:
                bad.append((hex(fn), 'unresolved', note))
    return bad, rows


def check_leaf_keeps(mem):
    """001BD560 relies on 001BD270 and 001BD460 leaving a0 and a1 as they
    received them (it passes the a0 they leave to 001BA1A0 and stores the a1
    they leave to D_0081076C). Both are leaves that run up to the next
    function; none of their instructions writes a0 or a1 or calls."""
    import test_area01_sys_reference as SYSREF
    for lo, hi in ((0x1BD270, 0x1BD370), (0x1BD460, 0x1BD560)):
        for pc in range(lo, hi, 4):
            w = struct.unpack_from('<I', mem, pc)[0]
            eff = SYSREF.reg_effects(w)
            written = eff[1]
            assert 4 not in written and 5 not in written, ('leaf writes a0 / a1', hex(lo), hex(pc))
            assert eff[4] not in ('call', 'call_reg'), ('leaf makes a call', hex(lo), hex(pc))


def api_checks():
    """Fail-stop contract: NULL context, NULL regions, NULL worker, a worker
    error, an unmapped access; the latch and clear_fault."""
    ram = (U8 * 0x1000)()
    regions = (Region * 1)(Region(0x1000, 0x1000, C.cast(ram, P(U8))))
    calls = []

    def worker(_, pc):
        calls.append(pc.contents.fn)
        return -1 if pc.contents.fn == 0x1AFA90 else 0
    w = WORKER(worker)
    fn = NATIVE.em_area02_math_001C8140
    assert fn(None, 0x1000, 0, 0) == -1
    s = World(None, 0, w, None, SP, 0, 0, 0)
    assert fn(C.byref(s), 0x1000, 0, 0) == -1 and s.fault == 1 and s.fault_function == 0x1C8140
    NATIVE.em_area02_math_clear_fault(C.byref(s))
    s = World(regions, 1, WORKER(0), None, SP, 0, 0, 0)
    assert fn(C.byref(s), 0x1000, 0, 0) == -1 and s.fault == 1 and s.fault_address == 0x1AFA90
    s = World(regions, 1, w, None, SP, 0, 0, 0)
    assert fn(C.byref(s), 0x1000, 0, 0) == -1 and s.fault == 2 and s.fault_address == 0x1AFA90
    assert fn(C.byref(s), 0x1000, 0, 0) == -1 and calls == [0x1AFA90], 'refuses while latched'
    NATIVE.em_area02_math_clear_fault(C.byref(s))
    assert s.fault == 0 and s.fault_function == 0 and s.fault_address == 0
    NATIVE.em_area02_math_clear_fault(None)
    # the latch keeps the first fault whole: a worker that re-enters the
    # module with the same context and faults there (00157F30's unmapped
    # D_008106B0 store), then returns an error to the outer entry
    inner = []

    def reenter(_, pc):
        o = I32(0)
        inner.append(NATIVE.em_area02_math_00157F30(C.byref(s), 0x1100, C.byref(o)))
        return -1
    wr = WORKER(reenter)
    s = World(regions, 1, wr, None, SP, 0, 0, 0)
    assert fn(C.byref(s), 0x1000, 0, 0) == -1 and inner == [-1]
    assert (s.fault, s.fault_function, s.fault_address) == (3, 0x157F30, 0x8106B0), \
        ('latch', s.fault, hex(s.fault_function), hex(s.fault_address))
    NATIVE.em_area02_math_clear_fault(C.byref(s))
    # an unmapped read is reported at its address and nothing is written:
    # 001C8140 with a new node outside the region faults at its first store
    s = World(regions, 1, WORKER(lambda _, pc: (setattr(pc.contents, 'v0', 0x5000), 0)[1]), None, SP, 0, 0, 0)
    assert fn(C.byref(s), 0x1000, 0, 0) == -1 and (s.fault, s.fault_address) == (3, 0x5003), \
        (s.fault, hex(s.fault_address))
    NATIVE.em_area02_math_clear_fault(C.byref(s))
    # an output left alone on a fault
    out = I32(0x5A5A5A5A)
    assert NATIVE.em_area02_math_00157F30(C.byref(s), 0x1100, C.byref(out)) == -1 and out.value == 0x5A5A5A5A
    # a NULL output pointer is allowed
    for i in range(0x1000):
        ram[i] = 0
    NATIVE.em_area02_math_clear_fault(C.byref(s))
    regions2 = (Region * 2)(Region(0x1000, 0x1000, C.cast(ram, P(U8))),
                            Region(0x810000, 0x1000, C.cast((U8 * 0x1000)(), P(U8))))
    s = World(regions2, 2, WORKER(lambda _, pc: 0), None, SP, 0, 0, 0)
    assert NATIVE.em_area02_math_00157F30(C.byref(s), 0x1100, None) == 0 and s.fault == 0
    assert ram[0x100] == 1


# ======================================================================
# Main
# ======================================================================

def main():
    global NATIVE, ELF, BRANCH_PCS
    t0 = time.time()
    ELF = read_elf()
    NATIVE = build_native()
    mem = ExEE(ELF).mem
    BRANCH_PCS = branch_pcs(mem)
    for fn in FUNCS:
        assert ENTRIES.get(fn), ('entry without a signature', hex(fn))
    missing = sorted(t for t in call_targets(mem) if t not in CALLEES and t not in FUNCS)
    assert not missing, ('call targets without a policy', [hex(t) for t in missing])
    bad, rows = check_register_reads(bytes(mem))
    assert not bad, ('callee register reads outside the compared policy', bad)
    if os.environ.get('EM_AREA02_MATH_REGS'):
        for r in rows:
            print('reads', r)
    check_leaf_keeps(bytes(mem))
    for beat in BEATS:
        ram = image(beat)[0]
        assert u32(ram, 0x275B40) == 0x8102F0 and u32(ram, 0x8102F0) == POSE, ('pose pointer', beat)
        assert ram[SCRATCH:SCRATCH + 0x10000] == bytes(0x10000), ('scratch not zero', beat)
    api_checks()
    base, variants = all_cases()
    only = os.environ.get('EM_AREA02_MATH_ONLY', '')
    if only:
        keep = {int(x, 16) for x in only.split(',') if x}
        base = [c for c in base if c['fn'] in keep]
        variants = [c for c in variants if c['fn'] in keep]
    run_base = RM.select(base, 0, 1, keep=lambda i, c: c['pin'])
    run_var = RM.select(variants, 0, 2, keep=lambda i, c: c['pin'])
    todo = run_base + run_var
    if os.environ.get('EM_AREA02_MATH_FAILFAST'):
        for c in todo:
            r = run_case_safe(c)
            if r[0] == 'fail':
                print('FAIL', r[1])
                sys.exit(1)
        print('test_area02_math_reference: survived')
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
    if os.environ.get('EM_AREA02_MATH_GAPS'):
        for g in gaps:
            print('gap', g)
    if RM.FULL and not only:
        assert not gaps, ('branch outcomes not taken', gaps)
    RM.banner(RM.part(len(run_base), len(base), 'cases'), RM.part(len(run_var), len(variants), 'variants'),
              f'{calls:,} calls compared', f'{fxw:,} fx field changes',
              f'{skipped} variants not comparable (the original left RAM)',
              f"branches both ways {len(both)}/{len(BRANCH_PCS)}")
    print(f'test_area02_math_reference: OK ({time.time() - t0:.1f} s)')


if __name__ == '__main__':
    main()
