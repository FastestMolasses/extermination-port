#!/usr/bin/env python3
"""Execute the original AREA00 lane-A00WORLD routines and compare em_area00_world.c.

docs/AREA00_WORLD.md. The user's pinned ELF and the captured AREA00 RAM
(../Extermination/build/s87/route_a00/<beat>/, the end-of-beat images of
docs/THIRD_LEVEL_ROUTE.md) supply every instruction and every table; none
are embedded here.

The oracle is FallEE (tools/test_player_fall_reference.py: every COP1 and
VU0 macro op through tools/ee_float_model.py, the measured model) running
the ORIGINAL routine over a copy of the capture. The native module runs over
another copy of the same bytes, with a private stack region at the same
original addresses (the routines hand stack locals to their callees).
Every call leaving the translated set is caught on both sides and handled
by one Python policy:
  run   the original callee runs (vector / matrix leaves, the math leaves,
        the LCG, the soft-float compare): in the oracle inside the same
        interpreter, on the native side in a second interpreter bound to the
        native module's own memory;
  stub  the callee is recorded and returns a scripted value (all 64 bits
        when the script gives a value outside the 32-bit range); a case can
        also turn a 'run' callee into a stub ('forced': the LCG with a
        scripted draw).
Calls among the translated routines (00189FE0 -> 00189EC0, 001B41F0) are
not intercepted: the original runs its own code, the native module its own
translations.

Lockstep (the lane-SYS design, docs/AREA01_SYS.md section 4). The original
runs first and records, at every call leaving the set, the callee entry and
every RAM / scratchpad line it stored to since the previous call. The native
module is built with its store trace (EM_AREA00_WORLD_STORE_TRACE) and, at
the same call, must match: the callee, the stack pointer, every argument
register the callee reads (64-bit images), the float argument registers,
the 16 bytes behind every argument that points into the stack, every
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
call target (and the tail-jump target of 001C63D0) must have a policy. In
EM_TEST_FULL=1 both outcomes of every conditional branch in the translated
routines must be taken, except the ones listed in UNREACHABLE with proof.

EM_TEST_FULL=1 runs every case and variant; the default run is the pinned
set (docs/AREA00_WORLD.md section 4). EM_AREA00_WORLD_SOURCE=<file> tests
another copy of the module source (mutation runs); EM_AREA00_WORLD_ONLY=
<hex,...> runs only the cases of those entries; EM_AREA00_WORLD_FAILFAST=1
runs serially and stops at the first difference; EM_AREA00_WORLD_GAPS=1
lists branch outcomes not taken. EM_TEST_JOBS sets the worker processes
(default 2, at most 4 for this lane).
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
ROUTE = DECOMP / 'build/s87/route_a00'
B03, B08, B09, B10 = 'a00_03_padlock', 'a00_08_switch', 'a00_09_ne_room_out', 'a00_10_progression_exit'
OUT = ROOT / 'build' / 'area00' / 'world'
STACK_BASE, STACK_SIZE = 0x7F000000, 0x100000
STACK_FILL = bytes([0xA5]) * STACK_SIZE
SP = shared.STACK_TOP
SCRATCH = 0x01E00000            # zero in every a00 capture; test records live here
PLAYER = 0x008102B0
CODE_END = 0x230000

# Translated routines (address -> size in bytes, from the census).
FUNCS = {
    0x189EC0: 0x118, 0x189FE0: 0x1A0, 0x18A180: 0x6C, 0x19AA80: 0x9C, 0x19B2C0: 0x200, 0x19C6F0: 0x140,
    0x1B0CD0: 0xA4, 0x1B41F0: 0x61C, 0x1C24D0: 0x68, 0x1C5050: 0x58, 0x1C6200: 0xB8, 0x1C63D0: 0x10,
    0x1CA3B0: 0x118, 0x1CA4D0: 0x104, 0x21BD60: 0xE0,
}
FUNC_RANGES = tuple((a, a + n) for a, n in FUNCS.items())

# Callee policy: address -> (kind, integer argument registers compared (a0..),
# float argument registers compared (f12..)). Checked against RegScan.
R, S = 'run', 'stub'
CALLEES = {
    # vector / matrix leaves
    0x102760: (R, 2, 0), 0x102948: (R, 2, 0), 0x102958: (R, 2, 0), 0x1026A0: (R, 3, 0), 0x1028B8: (R, 3, 0),
    0x1028D0: (R, 3, 0), 0x1029C0: (R, 1, 0), 0x1031E0: (R, 2, 0), 0x103230: (R, 2, 1),
    # math leaves, the LCG, the soft-float pair of 00189EC0
    0x11DE90: (R, 0, 1), 0x11DF78: (R, 0, 1), 0x11E2A8: (R, 0, 1), 0x122BB8: (R, 0, 0), 0x128350: (R, 0, 1),
    0x1000C0: (R, 2, 0), 0x1B1470: (R, 0, 1),
    # stubs
    0x19AD00: (S, 3, 0), 0x19CB60: (S, 0, 0), 0x19FE50: (S, 0, 0), 0x1A7280: (S, 1, 0), 0x1B61C0: (S, 4, 0),
    0x1C9610: (S, 3, 0), 0x1D7FA0: (S, 3, 2), 0x1EFD20: (S, 2, 0), 0x1EFD90: (S, 3, 0), 0x1F4F40: (S, 1, 0),
    0x1FBD50: (S, 3, 1), 0x1FC580: (S, 2, 0),
}

# Entry signatures: fn -> (argument kinds, output kind). 'f' = a raw float
# register (f12..); outputs: None (void), 'i' (int32), 'q' (the full 64-bit
# v0 register the original leaves: the last callee's).
ENTRIES = {
    0x189EC0: ('p', 'i'), 0x189FE0: ('ppp', None), 0x18A180: ('p', None), 0x19AA80: ('ppp', 'i'),
    0x19B2C0: ('ppp', 'i'), 0x19C6F0: ('pp', 'i'), 0x1B0CD0: ('pp', None), 0x1B41F0: ('pppppp', 'i'),
    0x1C24D0: ('ppp', 'q'), 0x1C5050: ('pf', 'q'), 0x1C6200: ('p', None), 0x1C63D0: ('p', 'q'),
    0x1CA3B0: ('pfff', None), 0x1CA4D0: ('ppp', None), 0x21BD60: ('p', 'i'),
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
OUT_TYPES = {'i': I32, 'q': U64}


class Region(C.Structure):
    _fields_ = [('base', U32), ('size', U32), ('bytes', P(U8))]


class Call(C.Structure):
    _fields_ = [('fn', U32), ('sp', U32), ('a', U64 * 8), ('f', U32 * 4), ('na', U32), ('nf', U32),
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
uint32_t a00world_trace_list[LINES];
uint32_t a00world_trace_count;
void a00world_trace_store(uint32_t address, unsigned size);
void a00world_trace_reset(void);
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
        a00world_trace_list[a00world_trace_count++] = id;
    }
}
void a00world_trace_store(uint32_t address, unsigned size)
{
    mark(address);
    mark(address + size - 1u);
}
void a00world_trace_reset(void)
{
    uint32_t i;
    for (i = 0; i < a00world_trace_count; i++)
        dirty[a00world_trace_list[i]] = 0;
    a00world_trace_count = 0;
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
    source = os.environ.get('EM_AREA00_WORLD_SOURCE', '')
    ext = 'dylib' if sys.platform == 'darwin' else 'so'
    if source:
        source = str(Path(source).resolve())
        lib = OUT / f'area00_world_{Path(source).stem}.{ext}'
        rebuild = not lib.exists() or Path(source).stat().st_mtime > lib.stat().st_mtime
    else:
        source = 'src/game/em_area00_world.c'
        lib = OUT / f'area00_world.{ext}'
        deps = [ROOT / source, ROOT / 'src/game/em_area00_world.h', ROOT / 'src/game/em_ee_float.h', trace,
                Path(__file__)]
        rebuild = not lib.exists() or max(d.stat().st_mtime for d in deps) > lib.stat().st_mtime
    if rebuild:
        # a mutant (EM_AREA00_WORLD_SOURCE) may leave a warning (an unused
        # variable after a deleted line): it is built without -Werror
        werror = [] if os.environ.get('EM_AREA00_WORLD_SOURCE') else ['-Werror']
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', *werror, '-Wpedantic',
                        '-ffp-contract=off', '-shared', '-fPIC', '-Isrc', '-Isrc/game',
                        '-DEM_AREA00_WORLD_STORE_TRACE=a00world_trace_store', source, str(trace),
                        '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    TRACE['count'] = U32.in_dll(native, 'a00world_trace_count')
    TRACE['list'] = (U32 * (RAM_LINES + 0x4000 // LINE)).in_dll(native, 'a00world_trace_list')
    TRACE['reset'] = native.a00world_trace_reset
    for fn, (kinds, outk) in ENTRIES.items():
        f = getattr(native, f'em_area00_world_{fn:08X}')
        f.restype = C.c_int
        f.argtypes = [P(World)] + [U32] * len(kinds) + ([P(OUT_TYPES[outk])] if outk else [])
    native.em_area00_world_clear_fault.argtypes = [P(World)]
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
        self.regions = (Region * 3)(Region(0, len(ram), C.cast(self.ram, P(U8))),
                                    Region(0x70000000, len(spad), C.cast(self.spad, P(U8))),
                                    Region(STACK_BASE, STACK_SIZE, C.cast(self.stack, P(U8))))
        self.ee = ExEE(ELF, b'', b'')
        self.ee.mem = memoryview(self.ram).cast('B')
        self.ee.spad = memoryview(self.spad).cast('B')
        self.ee.stack = memoryview(self.stack).cast('B')
        self.script, self.forced, self.log, self.error = script, forced, [], None
        self.lock = None
        self.fx = {}
        self.stubs = 0
        self.worker = WORKER(self._call)
        self.sys = World(self.regions, 3, self.worker, None, SP, 0, 0, 0)

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
            assert na <= c.na <= 8 and nf <= c.nf <= 4, ('native register counts', hex(c.fn), c.na, c.nf)
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
    rc = getattr(NATIVE, f'em_area00_world_{fn:08X}')(sysp, *vals, *([C.byref(out)] if outk else []))
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
            regs = [e.r[4 + i] for i in range(8)]
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
    ee.hooks = {fn: make(fn) for fn in CALLEES}
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
        if (case.get('fx') or case.get('edge') or case.get('poison')) and e.args and \
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


def pool(ram):
    a, seen, out = u32(ram, 0x275BC0), set(), []
    while a and a not in seen and len(out) < 0x200:
        seen.add(a)
        out.append(a)
        a = u32(ram, a + 0x1C)
    return out


def W8(a, v): return (a, struct.pack('<B', v & 0xFF))
def W16(a, v): return (a, struct.pack('<H', v & 0xFFFF))
def W32(a, v): return (a, struct.pack('<I', v & MASK))
def WF(a, v): return (a, struct.pack('<f', v))
def FB(v): return struct.unpack('<I', struct.pack('<f', v))[0]


LCG_A, LCG_C = 1103515245, 12345


def seeds(beat, rng, wants):
    """A write making the original LCG (00122BB8: the seed word at
    *(D_0024295C) + 0x58; each draw is the new state & 0x7FFFFFFF) produce
    draws satisfying wants[i] (None = any) on its next calls."""
    ram = image(beat)[0]
    for _ in range(200000):
        seed = rng.getrandbits(32)
        state, ok = seed, True
        for want in wants:
            state = (state * LCG_A + LCG_C) & MASK
            if want is not None and not want(state & 0x7FFFFFFF):
                ok = False
                break
        if ok:
            return [W32(u32(ram, 0x24295C) + 0x58, seed)]
    raise AssertionError('no seed found')


def bit13(v):
    return lambda d: (d >> 13) & 1 == v


def case(name, fn, args, beat=B09, script=None, writes=(), fargs=(), forced=(), pin=False):
    return dict(name=name, fn=fn, beat=beat, args=list(args), script=script or {}, writes=list(writes),
                fargs=list(fargs), forced=sorted(forced), pin=pin)


def vec(rng, lo=-50.0, hi=50.0, w=1.0):
    return [rng.uniform(lo, hi) for _ in range(3)] + [w]


def vwrites(a, v):
    return [WF(a + 4 * i, x) for i, x in enumerate(v)]


def bone_nodes(beat):
    ram = image(beat)[0]
    return [a for a in pool(ram) if u32(ram, a + 0x110) and ram[a + 0xC]]


E_REC = SCRATCH + 0x400             # the 0x700031D4 record of 00189FE0 / a victim of 001B41F0


def cases_189EC0(rng):
    out = []
    rec = SCRATCH + 0x100
    listed = (0x51, 0x30, 0x4F, 0x46, 0x1F, 0x1E, 0x50, 0x1C, 0x06, 0x0E)
    for t in range(256):
        out.append(case(f'189EC0 type {t:02x}', 0x189EC0, [rec], writes=[W8(rec + 3, t)],
                        pin=t in listed or t in (0x00, 0x0F, 0xFF)))
    diffs = [0.0, 1.0, 1.5707962, 1.5707963, 1.5707964, 1.5707965, 1.5707966, -1.5707963, -1.5707964,
             -1.5707965, 3.0, 3.1415927, -3.1415927, 3.2, -3.2, 4.7123890, 6.2831855, 7.0, 100.0, -100.0]
    for n, d in enumerate(diffs):
        for yaw in (0.0, 2.5):
            w = [W8(rec + 3, 0x0E), WF(rec + 0xC4, yaw), WF(0x810374, yaw + d)]
            out.append(case(f'189EC0 facing {n} yaw {yaw}', 0x189EC0, [rec], writes=w,
                            pin=yaw == 0.0 and n in (0, 3, 4, 5, 10, 12)))
    # 001B1470 never returns for a NaN, an infinity or a huge angle (the
    # original loops), so those inputs are outside the comparison
    for n, bits in enumerate((0x00000001, 0x80000000, 0x80000001, 0x00800000)):
        w = [W8(rec + 3, 0x0E), W32(rec + 0xC4, 0), W32(0x810374, bits)]
        out.append(case(f'189EC0 facing special {bits:08x}', 0x189EC0, [rec], writes=w, pin=n < 2))
    for n, a in enumerate(pool(image(B09)[0])[:4]):
        out.append(case(f'189EC0 node {n}', 0x189EC0, [a], pin=n == 0))
    return out


def cases_189FE0(rng):
    out = []
    e = E_REC
    a1, a2 = SCRATCH + 0x100, SCRATCH + 0x110
    ram = image(B09)[0]
    base31d0 = struct.unpack_from('<I', image(B09)[1], 0x31D0)[0]

    def setup(kind, typ, y, h=0x1234, first=1, e_ptr=e):
        w = [W32(0x700031D4, e_ptr), W8(e, first), W8(e + 2, kind), W8(e + 3, typ), W16(PLAYER + 0x36, h),
             W16(e + 0x36, 0x0808)]
        w += vwrites(a1, [3.0, y, -4.0, 9.0]) + vwrites(a2, [1.0, 0.5, 2.0, -9.0])
        w += vwrites(e + 0xB0, vec(rng)) + [W8(e + 0x0D, 0), W8(e + 5, 0)]
        return w
    out.append(case('189FE0 no record', 0x189FE0, [PLAYER, a1, a2], writes=setup(2, 0, 1.0, e_ptr=0), pin=True))
    out.append(case('189FE0 record not 1', 0x189FE0, [PLAYER, a1, a2], writes=setup(2, 0, 1.0, first=2), pin=True))
    for kind in (2, 0x22, 0x42, 0xE2, 4, 0x24, 0xE4, 0, 1, 3, 5, 0x1F, 0x20):
        for y in (3.0, -3.0):
            typ = 0x51 if kind & 0x1F == 4 else 0
            out.append(case(f'189FE0 kind {kind:02x} y {y}', 0x189FE0, [PLAYER, a1, a2],
                            writes=setup(kind, typ, y) + seeds(B09, rng, [None] * 4),
                            script={0x1EFD90: [0]}, pin=kind in (2, 4, 0xE2, 0xE4, 0) and y > 0 or kind == 2))
    # kind 2 over the victim types (001B41F0 from 00189FE0)
    for typ in (0, 1, 3, 9, 13, 19, 0x14):
        for h in (0x1234, -3):
            out.append(case(f'189FE0 kind 2 victim {typ:x} h {h}', 0x189FE0, [PLAYER, a1, a2],
                            writes=setup(2, typ, -1.0, h=h) + seeds(B09, rng, [None, None, None, bit13(typ & 1)]),
                            pin=typ in (0, 13, 19) and h == -3))
    # kind 4 by 00189EC0's status: 2 (0x51), 1 (0x30), 0 (0x00), 3 / 0 (0x0E by the angle)
    for typ, d in ((0x51, 0.0), (0x30, 0.0), (0x00, 0.0), (0x0E, 0.3), (0x0E, 2.5)):
        for h in (0x1234, 0x7001, -0x8000):
            w = setup(4, typ, 2.0, h=h) + [WF(e + 0xC4, 0.0), WF(0x810374, d)]
            out.append(case(f'189FE0 kind 4 type {typ:x} d {d} h {h}', 0x189FE0, [PLAYER, a1, a2], writes=w,
                            script={0x1EFD90: [0], 0x1EFD20: [5]}, pin=h == 0x1234))
    # the captured 0x700031D0 record, and one in scratch with its +0x1C word
    for n, ptr in enumerate((base31d0, SCRATCH + 0x800)):
        w = setup(2, 3, 1.0) + [W32(0x700031D0, ptr), W32(SCRATCH + 0x81C, 0x40)]
        out.append(case(f'189FE0 kind 2 base {n}', 0x189FE0, [PLAYER, a1, a2], writes=w, pin=True))
    # a1 / a2 equal (a zero direction), and the direction's y exactly zero
    w = setup(2, 0, 0.5) + vwrites(a2, [3.0, 0.5, -4.0, 1.0])
    out.append(case('189FE0 zero direction', 0x189FE0, [PLAYER, a1, a2], writes=w, pin=True))
    w = setup(2, 0, 0.5) + vwrites(a2, [1.0, 0.5, 2.0, 1.0])
    out.append(case('189FE0 y zero', 0x189FE0, [PLAYER, a1, a2], writes=w, pin=True))
    w = setup(2, 0, 0.5) + vwrites(a2, [1.0, 0.5, 2.0, 1.0]) + [WF(a1 + 4, -0.0), WF(a2 + 4, 0.0)]
    out.append(case('189FE0 y minus zero', 0x189FE0, [PLAYER, a1, a2], writes=w, pin=True))
    del ram
    return out


def cases_18A180(rng):
    out = []
    p = SCRATCH + 0x200
    for n in range(6):
        w = seeds(B09, rng, [lambda d, n=n: d & 1 == n & 1]) + [W8(p + 0x0A, 0x77), W8(p, 0x55)]
        out.append(case(f'18A180 draw {n}', 0x18A180, [p], writes=w, pin=n < 2))
    for v in (-1, -2, -3, 0x7FFFFFFF, 1 << 40, (1 << 40) + 1, -(1 << 40) + 1):
        out.append(case(f'18A180 scripted draw {v:#x}', 0x18A180, [p], script={0x122BB8: [v]}, forced={0x122BB8},
                        pin=v in (-1, -2)))
    out.append(case('18A180 player', 0x18A180, [PLAYER], pin=True))
    return out


def cases_19AA80(rng):
    out = []
    a0, a1 = SCRATCH + 0x100, SCRATCH + 0x110
    for n, (res, mask) in enumerate(((1, 0x20), (0, 0x20), (-1, 0x12345), (1 << 32, 0xFFFFFFFF), (0, -1),
                                     (7, 0x10000))):
        w = vwrites(a0, vec(rng)) + vwrites(a1, vec(rng)) + [W32(0x700031D0, 0x1234567)]
        out.append(case(f'19AA80 {n}', 0x19AA80, [a0, a1, mask], script={0x1A7280: [res]}, writes=w, pin=True))
    # the inputs overlapping the destination (the copy order shows)
    for n, (x, y) in enumerate(((0x700031A0, 0x70003190), (0x70003194, 0x700031A4), (0x70003190, 0x70003190))):
        w = vwrites(0x70003190, vec(rng)) + vwrites(0x700031A0, vec(rng))
        out.append(case(f'19AA80 overlap {n}', 0x19AA80, [x, y, 0x20], script={0x1A7280: [n & 1]}, writes=w,
                        pin=True))
    return out


def cases_19B2C0(rng):
    out = []
    a0, a1 = SCRATCH + 0x100, SCRATCH + 0x110
    for flags in (0, 2, 4, 6, 1, 0x80000000, 0x80000002, 0x80000004, 0x80000006, 0xFFFFFFFF):
        for r1, r2 in ((0, 0), (1, 0), (0, 1), (1, 1), (-1, 1 << 32)):
            if not flags & 6 and (r1, r2) != (0, 0):
                continue
            w = vwrites(a0, vec(rng)) + vwrites(a1, vec(rng)) + vwrites(0x700031B0, vec(rng))
            w += vwrites(0x700031C0, vec(rng)) + [W32(0x700031D0, 0x1234567)]
            out.append(case(f'19B2C0 f {flags:x} r {r1} {r2}', 0x19B2C0, [a0, a1, flags],
                            script={0x19FE50: [r1], 0x19CB60: [r2]}, writes=w,
                            pin=flags in (0x80000006, 6, 0) and (r1, r2) in ((0, 0), (1, 1), (0, 1))))
    # a zero-length segment (a0 == a1 in x / z), and a0 aliasing 0x700031C0
    # 0019FE50's result with only the upper word set (the whole register is tested)
    for flags in (2, 0x80000006):
        w = vwrites(a0, vec(rng)) + vwrites(a1, vec(rng)) + vwrites(0x700031B0, vec(rng))
        out.append(case(f'19B2C0 f {flags:x} upper word', 0x19B2C0, [a0, a1, flags],
                        script={0x19FE50: [1 << 32], 0x19CB60: [1]}, writes=w, pin=True))
    w = vwrites(a0, [1.0, 2.0, 3.0, 1.0]) + vwrites(a1, [1.0, 5.0, 3.0, 1.0])
    out.append(case('19B2C0 vertical', 0x19B2C0, [a0, a1, 0x80000006], writes=w, pin=True))
    w = vwrites(0x700031C0, vec(rng)) + vwrites(a1, vec(rng)) + vwrites(0x700031B0, vec(rng))
    out.append(case('19B2C0 a0 at 0x700031C0', 0x19B2C0, [0x700031C0, a1, 0x80000002], writes=w, pin=True))
    w = vwrites(a0, vec(rng)) + vwrites(0x700031A0, vec(rng)) + vwrites(0x700031B0, vec(rng))
    out.append(case('19B2C0 a1 at 0x700031A0', 0x19B2C0, [a0, 0x700031A0, 4], writes=w, pin=True))
    return out


def trigger_table(recs, dir_words, area=0, sub=0):
    """Writes that point D_0024D7C0[area] at a scratch descriptor whose
    word `sub` is the address of the record list `recs` [(cls, key, idx)]
    (0x28-byte records; a final (0xFF, 0, 0) ends it), and the directory
    pointer 0x70003250 at the scratch words dir_words."""
    desc, table, dirs = SCRATCH + 0x800, SCRATCH + 0x900, SCRATCH + 0xC00
    w = [W32(0x24D7C0 + 4 * area, desc), W32(desc + 4 * sub, table),
         W8(0x810700, area), W8(0x810701, sub), W32(0x70003250, dirs)]
    for i, (cls, key, idx) in enumerate(recs):
        w += [W16(table + 0x28 * i, cls), W16(table + 0x28 * i + 4, key), W16(table + 0x28 * i + 6, idx << 8 | 0x5A)]
    for i, v in enumerate(dir_words):
        w.append(W32(dirs + 4 * i, v))
    return w


def cases_19C6F0(rng):
    out = []
    # the captured tables: area 0 sub 1 (a00_08), area 1 sub 0 (a00_10)
    for beat, keys in ((B08, (5, 2, 0, 7, -5)), (B10, (1, 41, 0, 2))):
        for key in keys:
            for a1 in (0, 1, -1):
                out.append(case(f'19C6F0 {beat[:6]} key {key} a1 {a1}', 0x19C6F0, [key, a1], beat=beat,
                                pin=a1 != -1 and key in (5, 7, 1, 41)))
    # synthetic lists: the gate bit 29 against the key's sign, bit 30
    recs = [(0xB, 3, 1), (0xB, -3, 2), (0xB, 3, 3), (0xB, 0x7FFF, 4), (0xB, -0x8000, 5), (0xFF, 0, 0)]
    for words in ((0x20000000, 0x20000000, 0, 0, 0x60000000, 0x20000000),
                  (0, 0, 0x20000000, 0x40000000, 0, 0x60000000),
                  (0x40000000, 0xFFFFFFFF, 0xDFFFFFFF, 0x20000000, 0xFFFFFFFF, 0xBFFFFFFF)):
        for key in (3, -3, 0x7FFF, -0x8000, 0x10003, -1, 0x80000003):
            for a1 in (0, 1):
                w = trigger_table(recs, [0x11111111] + list(words))
                out.append(case(f'19C6F0 synth {words[0]:x} key {key:#x} a1 {a1}', 0x19C6F0, [key, a1],
                                writes=w, pin=key in (3, -3) and words[0] == 0x20000000))
    # the scan's two ends: a list that starts with 0xFF; a class other than 0xB
    w = trigger_table([(0xFF, 3, 1)], [0, 0x20000000, 0x20000000])
    out.append(case('19C6F0 empty list', 0x19C6F0, [3, 0], writes=w, pin=True))
    w = trigger_table([(0xB, 9, 1), (0x4, 3, 1), (0xB, 3, 1), (0xFF, 0, 0)], [0, 0, 0])
    out.append(case('19C6F0 other class', 0x19C6F0, [3, 0], writes=w, pin=True))
    w = trigger_table([(0x10B, 3, 1), (0xFF, 0, 0)], [0, 0, 0])
    out.append(case('19C6F0 class 0x10B', 0x19C6F0, [3, 0], writes=w, pin=True))
    w = trigger_table([(0xB, 3, 1), (0x1FF, 0, 0), (0xB, 4, 2), (0xFF, 0, 0)], [0, 0, 0, 0])
    out.append(case('19C6F0 end 0x1FF', 0x19C6F0, [4, 1], writes=w, pin=True))
    # a high index byte (idx 0xFF) and a nonzero area / sub pair
    w = trigger_table([(0xB, 6, 0xFF), (0xFF, 0, 0)], [0] * 0x101, area=3, sub=2)
    out.append(case('19C6F0 idx ff area 3 sub 2', 0x19C6F0, [6, 0], writes=w, pin=True))
    return out


def cases_1B0CD0(rng):
    out = []
    nodes = bone_nodes(B10)
    for n, src in enumerate(nodes[:4] + nodes[-2:]):
        for sel in (0, 1, 2, 3, 6, -1):
            if n > 1 and sel not in (0, 3):
                continue
            out.append(case(f'1B0CD0 node {n} sel {sel}', 0x1B0CD0, [src, sel], beat=B10,
                            script={0x1F4F40: [SCRATCH + 0x300]}, pin=n == 0 and sel in (0, 3, -1)))
    out.append(case('1B0CD0 none', 0x1B0CD0, [nodes[0], 1], beat=B10, script={0x1F4F40: [0]}, pin=True))
    out.append(case('1B0CD0 upper only', 0x1B0CD0, [nodes[0], 1], beat=B10, script={0x1F4F40: [1 << 32]}, pin=True))
    # the new record overlapping the source (the reads after each copy show)
    src = SCRATCH + 0x1000
    w = [W32(src + 0x110, SCRATCH + 0x1100)] + vwrites(src + 0xB0, vec(rng)) + vwrites(SCRATCH + 0x1190, vec(rng))
    w += vwrites(SCRATCH + 0x11A0, vec(rng)) + vwrites(SCRATCH + 0x11B0, vec(rng)) + vwrites(SCRATCH + 0x11C0, vec(rng))
    out.append(case('1B0CD0 overlap', 0x1B0CD0, [src, 2], beat=B10, writes=w, script={0x1F4F40: [src + 0x40]},
                    pin=True))
    return out


def cases_1B41F0(rng):
    out = []
    v, hit, d = E_REC, SCRATCH + 0x600, SCRATCH + 0x610

    def base(typ, b0d=0, b5=0, pos=None):
        w = [W8(v, 1), W8(v + 3, typ), W8(v + 0x0D, b0d), W8(v + 5, b5), W16(v + 0x36, 0x0404)]
        w += vwrites(v + 0xB0, pos or vec(rng)) + vwrites(hit, vec(rng)) + vwrites(d, vec(rng, -1, 1, 0.0))
        w += vwrites(v + 0x70, [7.0, 7.0, 7.0, 7.0])
        return w
    for typ in list(range(0x14)) + [0x14, 0x80, 0xFF]:
        for flags, p5, p6 in ((0, 0x23, 3), (1, 0x23, 3), (0x40, 0x1023, 0x7FFF), (0x41, 0, 0)):
            for bit in (0, 1):
                if bit and (flags or typ in (1, 9, 13, 19) or typ > 0x13):
                    continue
                w = base(typ) + seeds(B09, rng, [None, None, None, bit13(bit)])
                out.append(case(f'1B41F0 t{typ:x} f{flags:x} {p5:x} {p6:x} b{bit}', 0x1B41F0,
                                [v, hit, d, flags, p5, p6], writes=w,
                                pin=(flags in (0, 1) and typ in (0, 1, 2, 3, 4, 6, 9, 10, 13, 16, 19, 0x14))))
    # type 1: byte +0x0D == 3, byte +5 == 9, the 47.0 test
    for b0d, b5, yv in ((3, 9, 100.0), (3, 0, 46.9), (3, 0, 47.0), (3, 0, 47.000004), (3, 0, 100.0), (2, 9, 0.0),
                        (3, 0x109 & 0xFF, 100.0)):
        w = base(1, b0d, b5) + [WF(0x810354, yv)]
        out.append(case(f'1B41F0 type 1 d{b0d} b{b5} y{yv}', 0x1B41F0, [v, hit, d, 0, 5, 6], writes=w,
                        pin=True))
    # p5 bit 12 and the halfword sign, p6 * 5 overflow, flags in the upper bits
    for typ in (0, 2):
        for flags, p5, p6 in ((0, 0x1000, 0), (0, 0xFFFF1000, 0), (0, 0x0FFF, 0), (1, 0x8001, 0x3333),
                              (0x80000000, 7, 0xFFFFFFFF), (0, 0x10000, 1)):
            w = base(typ) + seeds(B09, rng, [None, None, None, None])
            out.append(case(f'1B41F0 t{typ} p5 {p5:#x} p6 {p6:#x} f {flags:#x}', 0x1B41F0,
                            [v, hit, d, flags, p5, p6], writes=w, pin=typ == 2 and p5 in (0x1000, 0xFFFF1000)))
    # scripted draws with the upper bits set (the shifts read the low word)
    for draws in ((0xFFFFFFFF, 0x80000000, 0x7FFFFFFF, 0x2000), ((1 << 40) | 0x1FE0000, -1, 0, 0)):
        out.append(case(f'1B41F0 scripted draws {draws[0]:#x}', 0x1B41F0, [v, hit, d, 0, 0, 0], writes=base(4),
                        script={0x122BB8: list(draws)}, forced={0x122BB8}, pin=True))
    # the hit point overlapping the jitter slots; the victim at the spray
    w = base(16) + vwrites(0x70003680, [1.5, 2.5, 3.5, 4.5])
    out.append(case('1B41F0 hit at 0x70003680', 0x1B41F0, [v, 0x70003680, d, 0, 1, 2], writes=w, pin=True))
    w = base(5) + vwrites(0x70003684, [1.5, 2.5, 3.5, 4.5])
    out.append(case('1B41F0 hit at 0x70003684', 0x1B41F0, [v, 0x70003684, d, 1, 1, 2], writes=w, pin=True))
    # captured victims (the a00_09 box nodes and others)
    for n, a in enumerate(pool(image(B09)[0])[:6]):
        out.append(case(f'1B41F0 node {n}', 0x1B41F0, [a, hit, d, 0, 0x10, 0x2], writes=vwrites(hit, vec(rng)),
                        pin=n < 2))
    return out


def cases_1C24D0(rng):
    out = []
    nodes = pool(image(B09)[0])
    m, v1 = SCRATCH, SCRATCH + 0x40
    for n in range(6):
        w = []
        for row in range(4):
            w += vwrites(m + 16 * row, vec(rng, -1.5, 1.5))
        w += vwrites(v1, vec(rng))
        out.append(case(f'1C24D0 {n}', 0x1C24D0, [nodes[n], v1, m], writes=w,
                        script={0x19AD00: [(0, 1, -1, 1 << 32, 0xFFFFFFFF00000000, 5)[n]]}, pin=n < 3))
    # the vector inside 0x700038C0 (read by 001026A0 while it writes there)
    w = vwrites(0x700038C0, vec(rng)) + vwrites(m, [1, 0, 0, 0]) + vwrites(m + 16, [0, 1, 0, 0])
    w += vwrites(m + 32, [0, 0, 1, 0]) + vwrites(m + 48, [0, 0, 0, 1])
    out.append(case('1C24D0 vector at 0x700038C0', 0x1C24D0, [nodes[0], 0x700038C0, m], writes=w, pin=True))
    return out


def cases_1C5050(rng):
    out = []
    nodes = pool(image(B10)[0])
    for n, k in enumerate((1.0, 0.5, -2.0, 0.0, 3.3e38)):
        out.append(case(f'1C5050 {n}', 0x1C5050, [nodes[n]], beat=B10, fargs=[FB(k)],
                        script={0x1D7FA0: [(0, 1, -1, 1 << 32, 9)[n]]}, pin=n < 2))
    out.append(case('1C5050 nan', 0x1C5050, [nodes[0]], beat=B10, fargs=[0x7FC00001], pin=True))
    return out


def cases_1C6200(rng):
    out = []
    for beat in (B09, B10):
        nodes = bone_nodes(beat)
        counts = {}
        for a in nodes:
            c = image(beat)[0][a + 0xC]
            counts.setdefault(c, a)
        for c, a in sorted(counts.items()):
            out.append(case(f'1C6200 {beat[:6]} count {c}', 0x1C6200, [a], beat=beat, pin=beat == B10 and c in (1, 15)))
    rec = SCRATCH + 0x1000
    out.append(case('1C6200 count 0', 0x1C6200, [rec], writes=[W8(rec + 0xC, 0)], pin=True))
    # bone 1's +0x70 word is its own pointer slot (the re-reads show)
    b0 = SCRATCH + 0x2000
    w = [W8(rec + 0xC, 2), W32(rec + 0x110, b0), W32(rec + 0x114, rec + 0x114 - 0x70)]
    out.append(case('1C6200 pointer overwritten', 0x1C6200, [rec], writes=w, pin=True))
    # bone 0's +0x64 halfword is the count byte (the loop bound is re-read)
    w = [W8(rec + 0xC, 1), W32(rec + 0x110, rec + 0xC - 0x64)]
    out.append(case('1C6200 count overwritten', 0x1C6200, [rec], writes=w, pin=True))
    w = [W8(rec + 0xC, 2), W32(rec + 0x110, rec + 0xC - 0x8C), W32(rec + 0x114, b0)]
    out.append(case('1C6200 count set 0x1000', 0x1C6200, [rec], writes=w, pin=True))
    return out


def cases_1C63D0(rng):
    out = []
    nodes = bone_nodes(B10)
    for n, a in enumerate(nodes[:4]):
        out.append(case(f'1C63D0 {n}', 0x1C63D0, [a], beat=B10,
                        script={0x1C9610: [(0, 1, 1 << 32, -2)[n]]}, pin=n < 2))
    return out


def cases_1CA3B0(rng):
    out = []
    o = SCRATCH + 0x100
    angles = [(0.0, 0.0, 0.0), (math.pi, 0.0, 0.0), (0.0, -math.pi, 0.0), (0.3, 1.2, -2.2), (1e6, 3.0, -1e-3)]
    for n in range(10):
        angles.append(tuple(rng.uniform(-7, 7) for _ in range(3)))
    for n, (x, y, z) in enumerate(angles):
        out.append(case(f'1CA3B0 {n}', 0x1CA3B0, [o], fargs=[FB(x), FB(y), FB(z)], pin=n < 6))
    for n, bits in enumerate((0x7FC00000, 0x7F800000, 0x00000001, 0x7F7FFFFF)):
        out.append(case(f'1CA3B0 special {bits:08x}', 0x1CA3B0, [o], fargs=[bits, FB(0.5), bits], pin=n == 0))
    out.append(case('1CA3B0 out at 0x70003A20', 0x1CA3B0, [0x70003A20], fargs=[FB(1), FB(2), FB(3)], pin=True))
    return out


def cases_1CA4D0(rng):
    out = []
    o, a, b = SCRATCH + 0x100, SCRATCH + 0x140, SCRATCH + 0x180
    for n in range(10):
        w = vwrites(a, vec(rng, -1, 1, rng.uniform(-1, 1))) + vwrites(b, vec(rng, -1, 1, rng.uniform(-1, 1)))
        out.append(case(f'1CA4D0 {n}', 0x1CA4D0, [o, a, b], writes=w, pin=n < 3))
    # exponent-255 operands (MADD / MSUB take the unsaturated product)
    for n, (qa, qb) in enumerate((((0x7F800000, 1.0, 2.0, 0.5), (1.0, 0.5, 0.25, 2.0)),
                                   ((1.0, 2.0, 3.0, 0x7F800000), (0.5, 1.0, 2.0, 1.5)),
                                   ((1e30, 2e30, 3e30, 4e30), (1e30, 1e30, 1e30, 1e30)),
                                   ((0x7FC00000, 0, 0, 0), (1.0, 1.0, 1.0, 1.0)))):
        w = []
        for base, q in ((a, qa), (b, qb)):
            for i, x in enumerate(q):
                w.append(W32(base + 4 * i, x) if isinstance(x, int) and x > 0xFFFF else WF(base + 4 * i, float(x)))
        out.append(case(f'1CA4D0 special {n}', 0x1CA4D0, [o, a, b], writes=w, pin=True))
    # one lane with exponent 255 at a time, in a and in b (each lane enters one
    # MADD / MSUB product somewhere: those take the raw product, MUL saturates)
    for side in ('a', 'b'):
        for lane in range(4):
            for bits in (0xFF800000, 0x7F800001):
                w = vwrites(a, [0.75, -0.5, 0.25, 0.625]) + vwrites(b, [-0.125, 0.875, 0.375, -1.5])
                w.append(W32((a if side == 'a' else b) + 4 * lane, bits))
                out.append(case(f'1CA4D0 {side}{lane} {bits:08x}', 0x1CA4D0, [o, a, b], writes=w,
                                pin=bits == 0xFF800000))
    # an exponent-255 factor whose raw product (MADD / MSUB) cancels a -MAX
    # accumulator, where a separate MUL would saturate first (one per result)
    INF = 0x7F800000
    for n, (qa, qb) in enumerate((((-2e19, INF, 0.0, 0.0), (0.0, 0.0, 1.0, 2e19)),
                                   ((2e19, INF, 0.0, 0.0), (0.0, 0.0, 2e19, 1.0)),
                                   ((2e19, INF, 0.0, 0.0), (1.0, 2e19, 0.0, 0.0)),
                                   ((2e19, 0.0, 0.0, INF), (2e19, 0.0, 0.0, 1.0)))):
        w = []
        for base_, q in ((a, qa), (b, qb)):
            for i, x in enumerate(q):
                w.append(W32(base_ + 4 * i, x) if isinstance(x, int) else WF(base_ + 4 * i, x))
        out.append(case(f'1CA4D0 cancel {"xyzw"[n]}', 0x1CA4D0, [o, a, b], writes=w, pin=True))
    # out not 16-byte aligned (the quadword store drops the low four bits),
    # and out overlapping an input
    w = vwrites(a, vec(rng, -1, 1, 0.5)) + vwrites(b, vec(rng, -1, 1, 0.25))
    out.append(case('1CA4D0 out +4', 0x1CA4D0, [o + 4, a, b], writes=w, pin=True))
    out.append(case('1CA4D0 out +0xC', 0x1CA4D0, [o + 0xC, a, b], writes=w, pin=True))
    out.append(case('1CA4D0 out = a', 0x1CA4D0, [a, a, b], writes=w, pin=True))
    out.append(case('1CA4D0 out = b', 0x1CA4D0, [b, a, a], writes=w, pin=True))
    return out


def cases_21BD60(rng):
    out = []
    p = SCRATCH + 0x200
    out.append(case('21BD60 player', 0x21BD60, [PLAYER], pin=True))
    for b236 in (0, 1, 0x80):
        for b0 in (0, 2, 0xFD):
            for f0 in (0x3B, 0):
                if b236 and (b0, f0) != (0, 0x3B):
                    continue
                for s in (0, 1, 2, 3, 0x81):
                    subs = (0, 1, 0x20, 0x21, 0x22, 0x23, 0x1D, 0x1E, 0x1C, 0x1F, 0x0B, 0x0A, 0xFF) \
                        if s in (1, 2) else (0,)
                    for sub in subs:
                        for f1 in ((0, 1, 2) if sub in (0x1D, 0x1E) and s == 1 else (0,)):
                            w = [W8(p + 0x236, b236), W8(p, b0), W8(p + 0x1F0, f0), W8(p + 4, s), W8(p + 5, sub),
                                 W8(p + 0x1F1, f1)]
                            out.append(case(f'21BD60 {b236:x} {b0:x} {f0:x} {s:x} {sub:x} {f1}', 0x21BD60, [p],
                                            writes=w, pin=b236 == 0 and b0 in (0, 2) and f0 == 0x3B and
                                            (s != 1 or sub in (0, 1, 0x21, 0x22, 0x23, 0x1D, 0x1E, 0x1F))))
    return out


def cases_pinned():
    """Review round 1: inputs that separate the translation from four
    plausible mistranslations no other case told apart (section 5 of the
    doc). Their own rng, after every other maker, so the other cases keep
    their inputs."""
    rng = random.Random(7)
    out = []
    # 00189FE0 masks the kind byte with 0x1F: bit 4 set over the low nibbles 2 and 4
    tmpl = {c['name']: c for c in cases_189FE0(rng)}
    for kind, y in ((0x12, 3.0), (0x14, 3.0), (0x12, -3.0), (0x14, -3.0)):
        c = tmpl[f'189FE0 kind {kind & 0xF:02x} y {y}']
        out.append(dict(c, name=f'189FE0 kind {kind:#x} y {y}', writes=list(c['writes']) + [W8(E_REC + 2, kind)],
                        pin=True))
    # 0019AA80 copies a0[i] and a1[i] interleaved: an input stream that crosses
    # the other destination (a1 inside 0x70003190.., a0 inside 0x700031A0..)
    for n, (x, y) in enumerate(((SCRATCH + 0x100, 0x70003194), (0x7000319C, SCRATCH + 0x110))):
        w = vwrites(0x70003190, vec(rng)) + vwrites(0x700031A0, vec(rng))
        w += vwrites(SCRATCH + 0x100, vec(rng)) + vwrites(SCRATCH + 0x110, vec(rng))
        out.append(case(f'19AA80 cross overlap {n}', 0x19AA80, [x, y, 0x20], script={0x1A7280: [1]}, writes=w,
                        pin=True))
    # 0019C6F0 tests all of a1: nonzero with a zero low byte
    for a1 in (0x100, 0x80000000):
        w = trigger_table([(0xB, 3, 1), (0xFF, 0, 0)], [0x11111111, 0])
        out.append(case(f'19C6F0 a1 {a1:#x}', 0x19C6F0, [3, a1], writes=w, pin=True))
    # region ends: 0021BD60's first read (+0x236) is the last byte of the
    # scratchpad / of RAM
    for top in (0x70004000, 0x2000000):
        p = top - 0x237
        out.append(case(f'21BD60 region end {top:#x}', 0x21BD60, [p], writes=[W8(top - 1, 0)], pin=True))
    return out


# Routines whose original makes no stubbed call: an fx / prestore variant of
# theirs equals the ordinary case.
NO_STUBS = {0x189EC0, 0x19C6F0, 0x1C6200, 0x1CA3B0, 0x1CA4D0, 0x21BD60}


def all_cases():
    rng = random.Random(0xA00)
    base = []
    for maker in (cases_189EC0, cases_189FE0, cases_18A180, cases_19AA80, cases_19B2C0, cases_19C6F0, cases_1B0CD0,
                  cases_1B41F0, cases_1C24D0, cases_1C5050, cases_1C6200, cases_1C63D0, cases_1CA3B0, cases_1CA4D0,
                  cases_21BD60):
        base += maker(rng)
    base += cases_pinned()
    variants = []
    for c in base:
        if c['fn'] not in NO_STUBS and not c['forced']:
            for kind in ('after', 'before'):
                variants.append(dict(c, name=f"{c['name']} fx-{kind}", fx=kind, pin=c['pin'] and c['name'] in VPIN))
            variants.append(dict(c, name=f"{c['name']} prestore", prestore=True, pin=c['pin'] and c['name'] in VPIN))
        variants.append(dict(c, name=f"{c['name']} poison", poison=True, pin=c['pin'] and c['name'] in VPIN))
        for kind in ('byte', 'top'):
            variants.append(dict(c, name=f"{c['name']} edge-{kind}", edge=kind, pin=c['pin'] and c['name'] in VPIN))
    for c in base + variants:
        if c['name'] in QUICK_KILLERS or c['name'] in QUICK_BRANCH:
            c['pin'] = True
    return base, variants


# The base cases whose variants the default run includes (one or two per
# routine and path class; EM_TEST_FULL=1 runs every variant of every case).
VPIN = frozenset((
    '189EC0 type 51', '189EC0 facing 3 yaw 0.0', '189FE0 kind 02 y -3.0', '189FE0 kind 04 y 3.0',
    '189FE0 kind 4 type 51 d 0.0 h 4660', '189FE0 kind 2 victim 13 h -3', '18A180 draw 1', '19AA80 0', '19AA80 1',
    '19AA80 overlap 0', '19B2C0 f 80000006 r 0 0', '19B2C0 f 80000006 r 1 1', '19C6F0 a00_08 key 5 a1 0',
    '19C6F0 synth 20000000 key 0x3 a1 0', '1B0CD0 node 0 sel 3', '1B0CD0 overlap', '1B41F0 t0 f0 23 3 b1',
    '1B41F0 t4 f1 23 3 b0', '1B41F0 t13 f0 23 3 b0', '1B41F0 t19 f1 23 3 b0', '1B41F0 type 1 d3 b0 y46.9',
    '1C24D0 0', '1C5050 0', '1C6200 a00_10 count 15', '1C6200 pointer overwritten', '1C63D0 0', '1CA3B0 3',
    '1CA4D0 0', '1CA4D0 out +4', '21BD60 0 0 3b 1 1e 1',
))


# The case or variant that killed each mutant of the lane's sweep (the first
# difference in the fail-fast order, docs/AREA00_WORLD.md section 5): always
# in the default run.
QUICK_KILLERS = frozenset((
    '1B41F0 t8 f0 23 3 b0',   # review round 1: its mutants r36 / r40 died only in full mode
    '1C24D0 3',
    '189EC0 type 06',
    '189EC0 type 0e',
    '189EC0 type 51',
    '189FE0 kind 02 y -3.0',
    '189FE0 kind 02 y 3.0',
    '189FE0 kind 04 y 3.0',
    '189FE0 kind 2 victim 1 h 4660',
    '189FE0 kind 2 victim 13 h 4660',
    '189FE0 kind 2 victim 9 h 4660',
    '189FE0 kind 2 victim d h 4660',
    '189FE0 kind 22 y 3.0',
    '189FE0 kind 4 type 0 d 0.0 h 4660',
    '189FE0 kind 4 type 51 d 0.0 h -32768',
    '189FE0 record not 1',
    '189FE0 zero direction',
    '18A180 draw 0',
    '18A180 scripted draw -0x1',
    '18A180 scripted draw -0xffffffffff',
    '19AA80 0',
    '19AA80 1',
    '19AA80 2',
    '19AA80 3',
    '19AA80 overlap 0',
    '19B2C0 f 0 r 0 0',
    '19B2C0 f 2 r 0 0',
    '19B2C0 f 2 r 0 0 poison',
    '19B2C0 f 2 upper word',
    '19B2C0 f 4 r 0 0',
    '19B2C0 f 80000002 r 0 0',
    '19C6F0 a00_08 key 2 a1 0',
    '19C6F0 a00_08 key 5 a1 0',
    '19C6F0 a00_08 key 7 a1 0',
    '19C6F0 synth 20000000 key -0x8000 a1 0',
    '19C6F0 synth 20000000 key 0x10003 a1 0',
    '1B0CD0 node 0 sel 0',
    '1B0CD0 node 0 sel 6',
    '1B0CD0 upper only',
    '1B41F0 t0 f0 23 3 b0',
    '1B41F0 t1 f0 23 3 b0',
    '1B41F0 t2 p5 0x1000 p6 0x0 f 0x0',
    '1B41F0 t80 f1 23 3 b0',
    '1B41F0 t9 f40 1023 7fff b0',
    '1B41F0 ta f0 23 3 b0',
    '1B41F0 type 1 d3 b0 y47.0',
    '1B41F0 type 1 d3 b9 y100.0',
    '1C24D0 0',
    '1C5050 0',
    '1C5050 1',
    '1C6200 a00_09 count 1',
    '1C6200 count overwritten',
    '1C6200 pointer overwritten',
    '1C6200 pointer overwritten poison',
    '1C63D0 0',
    '1CA3B0 0',
    '1CA3B0 1',
    '1CA4D0 0',
    '1CA4D0 b0 ff800000',
    '1CA4D0 cancel x',
    '1CA4D0 out +4',
    '1CA4D0 out = a',
    '21BD60 0 0 3b 1 1d 0',
    '21BD60 0 0 3b 1 23 0',
    '21BD60 0 0 3b 2 b 0',
    '21BD60 0 2 3b 1 0 0',
    '21BD60 player',
))

# Cases that the pins above leave out and that take the remaining branch
# outcomes (a greedy cover), so the default run takes every outcome too.
QUICK_BRANCH = frozenset(('21BD60 0 2 0 0 0 0',))


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
# with the proof (docs/AREA00_WORLD.md section 4).
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
        rows.append((hex(fn), kind, na, nf, sorted(ints), sorted(r for r in floats if r < SYSREF.VU_BASE)))
        if extra_i or extra_f:
            bad.append((hex(fn), kind, 'reads', extra_i, extra_f, 'policy', na, nf))
    return bad, rows


def lcg_property():
    """00122BB8 (run as original) returns the new state & 0x7FFFFFFF: never
    negative (the proof behind UNREACHABLE's 0018A180 entry)."""
    ee = ExEE(ELF)
    rng = random.Random(7)
    seed_at = u32(ee.mem, 0x24295C) + 0x58
    for _ in range(64):
        s = rng.getrandbits(32)
        ee.save(seed_at, s, 4)
        ee.hooks = {}
        ee.r[29], ee.r[31] = SP, shared.RETURN
        ee.run(0x122BB8)
        want = ((s * LCG_A + LCG_C) & MASK) & 0x7FFFFFFF
        assert ee.r[2] & MASK64 == want, ('00122BB8 draw', hex(s), hex(ee.r[2]), hex(want))


def api_checks():
    """Fail-stop contract: NULL context, NULL regions, NULL worker, a worker
    error, an unmapped access; the latch and clear_fault."""
    ram = (U8 * 0x1000)()
    regions = (Region * 1)(Region(0x1000, 0x1000, C.cast(ram, P(U8))))
    calls = []

    def worker(_, pc):
        calls.append(pc.contents.fn)
        return -1 if pc.contents.fn == 0x1F4F40 else 0
    w = WORKER(worker)
    fn = NATIVE.em_area00_world_001B0CD0
    assert fn(None, 0x1000, 0) == -1
    s = World(None, 0, w, None, SP, 0, 0, 0)
    assert fn(C.byref(s), 0x1000, 0) == -1 and s.fault == 1 and s.fault_function == 0x1B0CD0
    NATIVE.em_area00_world_clear_fault(C.byref(s))
    s = World(regions, 1, WORKER(0), None, SP, 0, 0, 0)
    assert fn(C.byref(s), 0x1000, 0) == -1 and s.fault == 1 and s.fault_address == 0x1F4F40
    s = World(regions, 1, w, None, SP, 0, 0, 0)
    assert fn(C.byref(s), 0x1000, 0) == -1 and s.fault == 2 and s.fault_address == 0x1F4F40
    assert fn(C.byref(s), 0x1000, 0) == -1 and calls == [0x1F4F40], 'refuses while latched'
    NATIVE.em_area00_world_clear_fault(C.byref(s))
    assert s.fault == 0 and s.fault_function == 0 and s.fault_address == 0
    NATIVE.em_area00_world_clear_fault(None)
    # the latch keeps the first fault whole: a worker that re-enters the
    # module with the same context and faults there (0021BD60's unmapped
    # +0x236 read), then returns an error to the outer entry (001B0CD0)
    inner = []

    def reenter(_, pc):
        o = I32(0)
        inner.append(NATIVE.em_area00_world_0021BD60(C.byref(s), 0x1F00, C.byref(o)))
        return -1
    wr = WORKER(reenter)
    s = World(regions, 1, wr, None, SP, 0, 0, 0)
    assert fn(C.byref(s), 0x1000, 0) == -1 and inner == [-1]
    assert (s.fault, s.fault_function, s.fault_address) == (3, 0x21BD60, 0x2136), \
        ('latch', s.fault, hex(s.fault_function), hex(s.fault_address))
    NATIVE.em_area00_world_clear_fault(C.byref(s))
    # 0021BD60 reads byte +0x236 first: the unmapped address is reported and
    # the result is left alone
    out = I32(0x5A5A5A5A)
    s = World(regions, 1, WORKER(lambda _, pc: 0), None, SP, 0, 0, 0)
    assert NATIVE.em_area00_world_0021BD60(C.byref(s), 0x1F00, C.byref(out)) == -1
    assert (s.fault, s.fault_function, s.fault_address, out.value) == (3, 0x21BD60, 0x2136, 0x5A5A5A5A), \
        (s.fault, hex(s.fault_function), hex(s.fault_address))
    NATIVE.em_area00_world_clear_fault(C.byref(s))
    # 001CA4D0 with its stack unmapped: the first stack store faults (the
    # inputs are read first)
    for i in range(0x1000):
        ram[i] = 0
    assert NATIVE.em_area00_world_001CA4D0(C.byref(s), 0x1100, 0x1200, 0x1300) == -1
    assert (s.fault, s.fault_address) == (3, SP - 0x10), (s.fault, hex(s.fault_address))


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
    if os.environ.get('EM_AREA00_WORLD_REGS'):
        for r in rows:
            print('reads', r)
    lcg_property()
    api_checks()
    base, variants = all_cases()
    only = os.environ.get('EM_AREA00_WORLD_ONLY', '')
    if only:
        keep = {int(x, 16) for x in only.split(',') if x}
        base = [c for c in base if c['fn'] in keep]
        variants = [c for c in variants if c['fn'] in keep]
    run_base = RM.select(base, 0, 1, keep=lambda i, c: c['pin'])
    run_var = RM.select(variants, 0, 2, keep=lambda i, c: c['pin'])
    todo = run_base + run_var
    if os.environ.get('EM_AREA00_WORLD_FAILFAST'):
        for c in todo:
            r = run_case_safe(c)
            if r[0] == 'fail':
                print('FAIL', r[1])
                sys.exit(1)
        print('test_area00_world_reference: survived')
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
    if os.environ.get('EM_AREA00_WORLD_GAPS'):
        for g in gaps:
            print('gap', g)
    if RM.FULL and not only:
        assert not gaps, ('branch outcomes not taken', gaps)
    RM.banner(RM.part(len(run_base), len(base), 'cases'), RM.part(len(run_var), len(variants), 'variants'),
              f'{calls:,} calls compared', f'{fxw:,} fx field changes',
              f'{skipped} variants not comparable (the original left RAM)',
              f"branches both ways {len(both)}/{len(BRANCH_PCS)}")
    print(f'test_area00_world_reference: OK ({time.time() - t0:.1f} s)')


if __name__ == '__main__':
    main()
