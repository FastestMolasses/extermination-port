#!/usr/bin/env python3
"""Execute the original lane-L4MISC routines and compare em_area02_misc.c.

docs/AREA02_MISC.md. The user's pinned ELF and the captured AREA02 / AREA01
revisit route RAM (../Extermination/build/s87/route_a02/<beat>/ and
route_a01r/<beat>/, end-of-beat images) supply every instruction and every
table; none are embedded here.

The oracle is the lane-SYS recording interpreter SysEE
(tools/test_area01_sys_reference.py, built on FallEE: every COP1 and VU0
macro op through tools/ee_float_model.py) running the ORIGINAL routine over
a copy of the capture. The native module runs over another copy of the same
bytes. Every call that leaves the translated set is caught on both sides
and handled by the same Python policy:
  run   the original callee runs (the math and vector leaves, the SDK
        float conversions, fabsf / sqrtf / sinf / cosf, the LCG 00122BB8,
        block_copy, the errno cell): in the oracle inside the same
        interpreter, on the native side in a second interpreter bound to
        the native module's own memory;
  stub  the callee is recorded and returns a scripted value (all 64 bits
        of v0 when the script gives a value outside the 32-bit range) and,
        where the case says so, writes scripted bytes behind a pointer
        argument (0011DB90's exception record), identically on both sides.
Calls among the translated routines are not intercepted: the original runs
its own code, the native module calls its own translations.

Lockstep (the lane-SYS / EXITA / A00LOW design). The original runs first
and records, at every call leaving the set, the callee entry and every RAM
/ scratchpad line it stored to since the previous call. The native module
is built with its store trace (EM_AREA02_MISC_STORE_TRACE) and, at the
same call, must match BEFORE any stub side effect is applied: the callee,
the stack pointer, every argument register the policy compares (a0..t3 as
64-bit images, f12..f19), every register the native module says it sets,
the bytes behind stack arguments (0011DB90's whole record, 001F91C0's whole
0x2F0-byte copy), the bytes behind stub pointer arguments, and all of RAM
and the scratchpad (only lines either side stored to since the last check
can differ, so exactly those are compared). The same memory check runs
after the last store, then all 32 MiB, the scratchpad and the result.

Registers the original reads without setting (docs/AREA02_MISC.md section
2): 001A8970's f20 comes from the case ('f20', the oracle's f20 at entry;
the native module gets the same bits through entry_f20); 'undef' cases
give the native module no value (NULL entry_f20, or an 001E4CE0 variant
without settings) and require the UNDEFINED fault at the original
instruction that reads the register, with the oracle shown to execute that
instruction and every call before it compared in lockstep.

Stub side effects ('fx' cases): after the k-th stub every field the
translated routines load after it changes (bytes / halfwords bit 0, words
bit 4; the indirect handler pointer moves to a second stubbed address), so
a value cached across a stub differs; 'fx before' changes, at the k-th stub,
every field loaded since the previous stub, so a read deferred across a stub
differs.

Cases: 'cap' the captured records of the a02 / a01r beats as they are; 't'
enumerated paths (every state and variant, both sides of every test, the
exact float thresholds and one ulp either side, LCG seeds, results non-zero
only in the upper half of v0); 'r' random perturbations (full mode).
EM_TEST_FULL=1 runs every case and every fx variant and asserts that both
outcomes of every conditional branch in the translated routines are taken
except the documented UNREACHABLE ones; the default run runs the pinned
cases, a fixed sample of the rest and of the fx variants, and the API
checks. EM_AREA02_MISC_ONLY=<prefix,...> runs only matching cases;
EM_AREA02_MISC_SOURCE=<file> tests another copy of the module source
(mutation sweeps); EM_AREA02_MISC_GAPS=1 lists branch outcomes not taken.
At most 4 worker processes (EM_TEST_JOBS overrides).
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
import test_area01_sys_reference as SR  # noqa: E402
import ee_float_model as M  # noqa: E402

MASK, MASK64 = 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF
DECOMP = ROOT.parent / 'Extermination'
ROUTES = {'a02': DECOMP / 'build/s87/route_a02', 'a01r': DECOMP / 'build/s87/route_a01r'}
BEAT = 'a02_02_ladder_escape'
OUT = ROOT / 'build' / 'area02' / 'misc'
STACK_BASE, STACK_SIZE = 0x7F000000, 0x100000
STACK_LO, STACK_HI = SR.STACK_LO, SR.STACK_HI
SP = shared.STACK_TOP
SCRATCH = 0x01E00000        # zero in every a02 / a01r beat image; synthetic records live here
PLAYER = 0x8102B0

# Translated routines (address -> size in bytes, from the census).
FUNCS = {
    0x11C128: 0x39C, 0x11E520: 0xFC, 0x15C750: 0x54, 0x15C7C0: 0x204, 0x1A8970: 0x26C, 0x1A8E80: 0xBC,
    0x1A9360: 0x114, 0x1AA640: 0xB8, 0x1AA700: 0x98, 0x1B6AE0: 0x104, 0x1D3A30: 0x90, 0x1D3AC0: 0xC,
    0x1D3C40: 0x98, 0x1D3CE0: 0xC, 0x1D66A0: 0x28C, 0x1E49F0: 0x8, 0x1E4A00: 0x2D8, 0x1E4CE0: 0x910,
    0x1EBD20: 0xE4, 0x1EC5F0: 0x22C, 0x1EC9A0: 0x7C, 0x1ECEF0: 0xB4, 0x1EDAF0: 0x34C, 0x1F4010: 0xA8,
    0x1F4E40: 0xF4, 0x1F6B30: 0x24, 0x1F9660: 0x114, 0x21BD10: 0x4C,
}

# Callee policy: address -> (kind, integer argument registers compared
# (a0..t3), float argument registers compared (f12..f19), [(pointer
# argument, bytes)] snapshotted at a stub's entry). Checked against the
# measured register reads (lane SYS RegScan) in setup().
R, S = 'run', 'stub'
CALLEES = {
    # run: leaves, math and SDK helpers, executed as original code on both sides
    0x11DF78: (R, 0, 1, ()), 0x11CB90: (R, 0, 1, ()), 0x11E080: (R, 0, 1, ()), 0x128350: (R, 0, 1, ()),
    0x11FD78: (R, 0, 0, ()), 0x127758: (R, 1, 0, ()), 0x1B0070: (R, 0, 0, ()), 0x1028D0: (R, 3, 0, ()),
    0x102760: (R, 2, 0, ()), 0x11E748: (R, 0, 1, ()), 0x102738: (R, 2, 0, ()), 0x11E2A8: (R, 0, 1, ()),
    0x11DE90: (R, 0, 1, ()), 0x1281C0: (R, 0, 1, ()), 0x102948: (R, 2, 0, ()), 0x128250: (R, 0, 1, ()),
    0x1029C0: (R, 1, 0, ()), 0x102C58: (R, 3, 0, ()), 0x102918: (R, 3, 0, ()), 0x122BB8: (R, 0, 0, ()),
    0x102958: (R, 2, 0, ()), 0x121870: (R, 3, 0, ()),
    # stubs: the rest (callees outside this lane)
    0x11DB90: (S, 1, 0, ((0, 0x28),)), 0x15C1F0: (S, 1, 0, ()), 0x1749A0: (S, 3, 1, ()),
    0x21BB00: (S, 1, 0, ()), 0x183C40: (S, 2, 0, ()), 0x1A91C0: (S, 2, 1, ()), 0x1FD4C0: (S, 1, 0, ()),
    0x119828: (S, 3, 0, ()), 0x1D1F80: (S, 3, 0, ()), 0x1D37D0: (S, 2, 0, ()), 0x1D3AD0: (S, 2, 0, ()),
    0x1CD520: (S, 5, 3, ()), 0x1EFD20: (S, 2, 0, ()), 0x1CD070: (S, 2, 0, ()), 0x1CD2B0: (S, 0, 4, ()),
    0x1CFB50: (S, 3, 5, ()), 0x1CFBE0: (S, 5, 0, ()), 0x1B1B70: (S, 1, 0, ()), 0x1B17A0: (S, 1, 0, ()),
    0x1AFC10: (S, 1, 0, ()), 0x1F2F90: (S, 4, 0, ()), 0x1F3340: (S, 3, 0, ()), 0x1F6760: (S, 0, 0, ()),
    0x1F6640: (S, 1, 0, ()), 0x1F91C0: (S, 1, 0, ((0, 0x2F0),)),
}
INDIRECT = (S, 3, 0, ())    # 001A8970's handler at a1 +0x34, called with (a1, a0, a0 +0xB0)
HANDLER = 0x0010FFF0        # the synthetic handler address the cases put at a1 +0x34
# Stack bytes behind pointer arguments the run callees read: 0011DB90's
# record (0x24 bytes) is compared whole by its snapshot (a stub).
STACK_READS = {}

# Conditional-branch outcomes the full sweep cannot take, with the proof.
UNREACHABLE = {
    (0x11C1EC, True): "0011C128 for |x| <= 0x31FFFFFF: x + 1e30 (0x7149F2CA) is 1e30 rounded for every such x "
                      "(denormals read as zero on the EE, the largest such |x| is below 2^-27), so 1.0 < x + 1e30 "
                      "always holds and the path that would read the caller's f20 is never taken",
}
UNREACHABLE[(0x1AA720, True)] = ("001AA700's second zero test of the count repeats the first on the same "
                                  "register, unchanged in between (the first test's delay slot copies a0), so "
                                  "it is reached only with a non-zero count")
PROBES = {0x1A8AC4, 0x1E537C, 0x1E53EC, 0x1E555C}   # the instructions that read an UNDEFINED register


def policy(fn, indirect):
    if fn in CALLEES:
        return CALLEES[fn]
    if fn in indirect:
        return INDIRECT
    raise AssertionError(('call without a policy', hex(fn)))


def log_entry(ee, fn, pol, regs, fregs, sp):
    """What the callee can see at its entry besides memory: the stack
    pointer, the compared argument registers, the stack bytes behind
    argument registers, and for stubs the bytes behind its pointer
    arguments."""
    kind, na, nf, snaps = pol
    args = tuple(regs[i] & MASK64 for i in range(na))
    floats = tuple(fregs[i] & MASK for i in range(nf))
    stack = []
    if fn in STACK_READS:
        for i, n in STACK_READS[fn]:
            stack.append(ee.read(regs[i] & MASK, n))
    else:
        for a in args:
            a &= MASK
            if STACK_LO <= a < STACK_HI - 16:
                stack.append(ee.read(a, 16))
    snap = tuple(ee.read(regs[i] & MASK, n) for i, n in snaps) if kind == S else ()
    return (hex(fn), hex(sp & MASK), args, floats, tuple(stack), snap)


def v0_image(v):
    """A scripted v0: values in the signed or unsigned 32-bit range are
    sign-extended as a 32-bit result would be; others are the raw 64 bits."""
    if -(1 << 31) <= v < (1 << 32):
        return sx32(v) & MASK64
    return v & MASK64


class Script:
    """Scripted stub results: fn -> list of v0, (v0, f0) or (v0, f0, outs)
    with outs = [(argument index, offset, bytes)] written behind that
    argument after the call; default (0, 0, ())."""

    def __init__(self, spec):
        self.queues = {fn: list(values) for fn, values in (spec or {}).items()}

    def take(self, fn):
        q = self.queues.get(fn)
        if q:
            v = q.pop(0)
            if not isinstance(v, tuple):
                return v, 0, ()
            return v[0], v[1] if len(v) > 1 else 0, v[2] if len(v) > 2 else ()
        return 0, 0, ()


def apply_outs(ee, regs, outs):
    for i, off, data in outs:
        a = (regs[i] & MASK) + off
        for k, b in enumerate(data):
            ee.save(a + k, b, 1)


# ======================================================================
# Native side
# ======================================================================

P, U8, U32, I32, U64 = C.POINTER, C.c_uint8, C.c_uint32, C.c_int32, C.c_uint64
Region = SR.Region


class Call(C.Structure):
    _fields_ = [('fn', U32), ('sp', U32), ('a', U64 * 8), ('f', U32 * 8), ('na', U32), ('nf', U32),
                ('v0', U64), ('f0', U32)]


WORKER = C.CFUNCTYPE(C.c_int, C.c_void_p, P(Call))


class Ctx(C.Structure):
    _fields_ = [('regions', P(Region)), ('region_count', C.c_uint), ('call', WORKER), ('ctx', C.c_void_p),
                ('sp', U32), ('entry_f20', P(U32)), ('fault', I32), ('fault_function', U32),
                ('fault_address', U32)]


NATIVE = None
ELF = None
IMAGES = {}
BUFFERS = {}
TRACE = {}
LINE = SR.LINE
RAM_LINES = SR.RAM_LINES

TRACE_C = r"""/* Store trace for the native module (test build only): the 64-byte
 * lines of RAM and scratchpad the module stores to since the last reset. */
#include <stdint.h>
#define RAM_LINES (0x2000000u / 64u)
#define LINES (RAM_LINES + 0x4000u / 64u)
static uint8_t dirty[LINES];
uint32_t a02misc_trace_list[LINES];
uint32_t a02misc_trace_count;
void a02misc_trace_store(uint32_t address, unsigned size);
void a02misc_trace_reset(void);
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
        a02misc_trace_list[a02misc_trace_count++] = id;
    }
}
void a02misc_trace_store(uint32_t address, unsigned size)
{
    mark(address);
    mark(address + size - 1u);
}
void a02misc_trace_reset(void)
{
    uint32_t i;
    for (i = 0; i < a02misc_trace_count; i++)
        dirty[a02misc_trace_list[i]] = 0;
    a02misc_trace_count = 0;
}
"""


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    trace = OUT / 'store_trace.c'
    if not trace.exists() or trace.read_text() != TRACE_C:
        trace.write_text(TRACE_C)
    source = os.environ.get('EM_AREA02_MISC_SOURCE', '')
    ext = 'dylib' if sys.platform == 'darwin' else 'so'
    if source:
        source = str(Path(source).resolve())
        lib = Path(source).with_suffix('.' + ext)
        rebuild = not lib.exists() or Path(source).stat().st_mtime > lib.stat().st_mtime
    else:
        source = str(ROOT / 'src/game/em_area02_misc.c')
        lib = OUT / f'area02_misc.{ext}'
        deps = [Path(source), ROOT / 'src/game/em_area02_misc.h', ROOT / 'src/game/em_ee_float.h', trace,
                Path(__file__)]
        rebuild = not lib.exists() or max(d.stat().st_mtime for d in deps) > lib.stat().st_mtime
    if rebuild:
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                        '-ffp-contract=off', '-shared', '-fPIC', '-Isrc', '-Isrc/game',
                        '-DEM_AREA02_MISC_STORE_TRACE=a02misc_trace_store', source, str(trace),
                        '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    TRACE['count'] = U32.in_dll(native, 'a02misc_trace_count')
    TRACE['list'] = (U32 * (RAM_LINES + 0x4000 // LINE)).in_dll(native, 'a02misc_trace_list')
    TRACE['reset'] = native.a02misc_trace_reset
    return native


def native_lines():
    n = TRACE['count'].value
    if not n:
        return ()
    out = TRACE['list'][:n]
    TRACE['reset']()
    return out


class NativeRun:
    """One native call over copies of `ram` / `spad` and a private stack."""

    def __init__(self, ram, spad, script, indirect, f20=None):
        if 'ram' not in BUFFERS or len(BUFFERS['ram']) != len(ram):
            BUFFERS['ram'] = (U8 * len(ram))()
            BUFFERS['spad'] = (U8 * len(spad))()
            BUFFERS['stack'] = (U8 * STACK_SIZE)()
        self.ram, self.spad, self.stack = BUFFERS['ram'], BUFFERS['spad'], BUFFERS['stack']
        for dst, src in ((self.ram, ram), (self.spad, spad), (self.stack, SR.STACK_FILL)):
            if isinstance(src, bytearray):
                C.memmove(dst, (C.c_char * len(src)).from_buffer(src), len(src))
            else:
                C.memmove(dst, src, len(src))
        self.regions = (Region * 3)(Region(0, len(ram), C.cast(self.ram, P(U8))),
                                    Region(0x70000000, len(spad), C.cast(self.spad, P(U8))),
                                    Region(STACK_BASE, STACK_SIZE, C.cast(self.stack, P(U8))))
        self.ee = SR.SysEE(ELF, b'', b'')
        self.ee.mem = memoryview(self.ram).cast('B')
        self.ee.spad = memoryview(self.spad).cast('B')
        self.ee.stack = memoryview(self.stack).cast('B')
        self.script, self.indirect, self.log, self.error = script, indirect, [], None
        self.lock = None
        self.fx = {}
        self.stubs = 0
        self.worker = WORKER(self._call)
        self.f20 = U32(f20 & MASK) if f20 is not None else None
        self.ctx = Ctx(self.regions, 3, self.worker, None, SP,
                       C.pointer(self.f20) if self.f20 is not None else P(U32)(), 0, 0, 0)

    def dirty(self):
        out = set(native_lines())
        out |= self.ee.dirty
        self.ee.dirty = set()
        return out

    def _call(self, _, pc):
        try:
            c = pc.contents
            pol = policy(c.fn, self.indirect)
            kind, na, nf = pol[0], pol[1], pol[2]
            assert na <= c.na <= 8 and nf <= c.nf <= 8, ('native register counts', hex(c.fn), c.na, c.nf)
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
                                                             k, hex(c.fn), 'native', got, 'original',
                                                             (oregs[:c.na], ofregs[:c.nf]))
            if kind == R:
                ee = self.ee
                for i in range(8):
                    ee.r[4 + i] = c.a[i] & MASK64
                    ee.f[12 + i] = c.f[i] & MASK
                ee.r[29] = c.sp
                v0, f0 = SR.run_nested(ee, c.fn)
            else:
                v0, f0, outs = self.script.take(c.fn)
                v0 = v0_image(v0)
                apply_outs(self.ee, regs, outs)
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


TWO = {0x1A8970, 0x1A8E80, 0x1A9360, 0x1AA640, 0x1D3A30, 0x1D3C40, 0x1EBD20, 0x1EC5F0, 0x1EC9A0, 0x1ECEF0,
       0x1EDAF0, 0x1F4010}
ONE = {0x15C750, 0x15C7C0, 0x1AA700, 0x1D3AC0, 0x1D3CE0, 0x1E4CE0}
NONE = {0x1E49F0, 0x1F6B30}
RETURNS = {0x1B6AE0, 0x1D66A0, 0x21BD10}
FRETURNS = {0x11C128, 0x11E520}


def native_call(fn, ctx, args, fargs):
    """(rc, result or None)."""
    out = U32(0)
    N = NATIVE
    a = [x & MASK for x in list(args) + [0] * 5]
    f = [x & MASK for x in list(fargs) + [0] * 3]
    name = f'em_area02_misc_{fn:08X}'
    if fn in TWO:
        return getattr(N, name)(ctx, U32(a[0]), U32(a[1])), None
    if fn in ONE:
        return getattr(N, name)(ctx, U32(a[0])), None
    if fn in NONE:
        return getattr(N, name)(ctx), None
    if fn in FRETURNS:
        rc = getattr(N, name)(ctx, U32(f[0]), C.byref(out))
    elif fn == 0x1B6AE0:
        rc = N.em_area02_misc_001B6AE0(ctx, U32(a[1]), U32(a[2]), C.byref(out))
    elif fn == 0x1D66A0:
        rc = N.em_area02_misc_001D66A0(ctx, U32(a[0]), U32(a[1]), U32(a[2]), U32(a[3]), U32(f[0]), C.byref(out))
    elif fn == 0x1E4A00:
        return N.em_area02_misc_001E4A00(ctx, U32(a[0]), U32(f[0]), U32(f[1])), None
    elif fn == 0x1F4E40:
        return N.em_area02_misc_001F4E40(ctx, U32(a[0]), U32(a[1]), U32(a[2]), U32(f[0])), None
    elif fn == 0x1F9660:
        a1 = args[1] & MASK64 if args[1] > MASK or args[1] < -(1 << 31) else sx32(args[1]) & MASK64
        return N.em_area02_misc_001F9660(ctx, U32(a[0]), U64(a1)), None
    elif fn == 0x21BD10:
        rc = N.em_area02_misc_0021BD10(ctx, C.byref(out))
    else:
        raise AssertionError(('no native entry', hex(fn)))
    return rc, out.value & MASK


# ======================================================================
# Oracle side
# ======================================================================

def route(beat):
    return ROUTES['a01r' if beat.startswith('a01r') else 'a02'] / beat


def image(beat=BEAT):
    if beat not in IMAGES:
        IMAGES[beat] = ((route(beat) / 'eeMemory.bin').read_bytes(), (route(beat) / 'scratchpad.bin').read_bytes())
    return IMAGES[beat]


def prepared(case):
    src_ram, src_spad = image(case.get('beat', BEAT))
    if 'oram' not in BUFFERS:
        BUFFERS['oram'], BUFFERS['ospad'] = bytearray(len(src_ram)), bytearray(len(src_spad))
    ram, spad = BUFFERS['oram'], BUFFERS['ospad']
    ram[:] = src_ram
    spad[:] = src_spad
    for address, data in case.get('writes', ()):
        if 0x70000000 <= address < 0x70004000:
            spad[address - 0x70000000:address - 0x70000000 + len(data)] = data
        else:
            ram[address:address + len(data)] = data
    return ram, spad


LAST = {}


class ProbeMixin:
    """Records which of the PROBES instructions the original executed."""

    def execute(self, word, pc):
        if pc in PROBES:
            self.hits.add(pc)
        return super().execute(word, pc)


class MaxMinMixin:
    """The shared interpreter leaves VMAXbc / VMINIbc (VU0 macro ops
    0x10..0x17) undefined; 001E4A00's ramps use VMAXx and VMINIy. They run
    here through the measured model ee_float_model.vu_max / vu_min
    (docs/EE_FLOAT_MODEL.md; the lane-HUD mixin), lane by lane under the dest
    mask."""

    def macro(self, word):
        op, fs, ft, fd = word & 63, word >> 11 & 31, word >> 16 & 31, word >> 6 & 31
        if 16 <= op < 24:
            mask, bc = word >> 21 & 15, op & 3
            fn = M.vu_max if op < 20 else M.vu_min
            s, t = [x & MASK for x in self.vf[fs]], [x & MASK for x in self.vf[ft]]
            if fd:
                for i in range(4):
                    if mask & (8 >> i):
                        self.vf[fd][i] = fn(s[i], t[bc])
            return
        return super().macro(word)


class MiscEE(ProbeMixin, MaxMinMixin, SR.SysEE):
    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self.hits = set()


class MiscRecEE(ProbeMixin, MaxMinMixin, SR.RecEE):
    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self.hits = set()


def f20_of(case):
    return case.get('f20', SR.FREG_FILL + 20) & MASK


def oracle_run(case, ram, spad, cls=None, on_stub=None):
    """The original over `ram` / `spad` (used in place). Returns (ee, log,
    v0, entries, final delta); entries[k] = (log entry, {line: bytes}
    stored since the previous call, a0..t3 images, f12..f19 bits)."""
    ee = (cls or MiscEE)(ELF, b'', b'')
    LAST['ee'] = ee
    ee.mem, ee.spad = ram, spad
    ee.stack[:] = SR.STACK_FILL
    ee.pre = {}
    script, log, entries = Script(case['script']), [], []
    indirect = set(case.get('indirect', ()))
    stubs = [0]

    def make(fn):
        pol = policy(fn, indirect)

        def hook(e):
            regs = [e.r[4 + i] for i in range(8)]
            fregs = [e.f[12 + i] for i in range(8)]
            entry = log_entry(e, fn, pol, regs, fregs, e.r[29])
            log.append(entry)
            entries.append((entry, e.take(), tuple(r & MASK64 for r in regs), tuple(f & MASK for f in fregs)))
            if pol[0] == R:
                SR.run_nested(e, fn)
            else:
                v0, f0, outs = script.take(fn)
                e.r[2] = v0_image(v0)
                e.f[0] = f0 & MASK
                apply_outs(e, regs, outs)
                if on_stub is not None:
                    on_stub(e, stubs[0])
                stubs[0] += 1
        return hook
    ee.hooks = {fn: make(fn) for fn in set(CALLEES) | indirect}
    for i in range(1, 32):
        if i not in (28, 29, 31):
            ee.r[i] = SR.REG_FILL + i * 0x40
    for i in range(32):
        ee.f[i] = SR.FREG_FILL + i
    ee.f[20] = f20_of(case)
    for i, v in enumerate(case['args']):
        ee.r[4 + i] = (v & MASK64) if v > MASK or v < -(1 << 31) else sx32(v) & MASK64
    for i, v in enumerate(case.get('fargs', ())):
        ee.f[12 + i] = v & MASK
    ee.r[29] = SP
    ee.r[31] = shared.RETURN
    ee.run(case['fn'])
    return ee, log, ee.r[2] & MASK, entries, ee.take()


def fx_plan(case, before=False):
    ram, spad = prepared(case)

    def count(e, k):
        e.window = k + 1
    ee, *_ = oracle_run(case, ram, spad, cls=MiscRecEE, on_stub=count)
    indirect = set(case.get('indirect', ()))
    plan, alts = {}, set()
    for w, loads in ee.loads.items():
        if w == 0 and not before:
            continue
        items = []
        for (a, size), (v, pc) in sorted(loads.items()):
            if size == 4 and 0x100000 <= v < SR.CODE_END:
                if v not in indirect:
                    continue
                alts.add(v ^ 0x10)
            items.append((a, size, v, pc))
        if items:
            plan[w if before else w - 1] = items
    return plan, sorted(alts)


def run_case(case):
    """Returns (branch outcomes, calls, fx writes, SysEE.stores). Raises on
    any difference."""
    fx = None
    if case.get('fx'):
        plan, alts = fx_plan(case, case.get('fx_before', False))
        case = dict(case, indirect=list(case.get('indirect', ())) + alts)
        fx = SR.Perturb(plan, case['indirect'], None, False, case.get('fx_before', False))
    ram, spad = prepared(case)
    undef = case.get('undef')
    nat = NativeRun(ram, spad, Script(case['script']), set(case.get('indirect', ())),
                    None if case.get('f20_null') else f20_of(case))
    native_lines()
    where = (case['name'],)
    crashed = None
    try:
        ee, olog, ov0, entries, last = oracle_run(case, ram, spad, on_stub=fx)
    except AssertionError as e:
        if (case.get('fx') or case.get('store_site')) and e.args and isinstance(e.args[0], tuple) \
                and e.args[0][:1] == ('address',):
            return set(), 0, -1, {}         # a variant that turned a pointer into an unmapped address
        if not undef:
            raise AssertionError((where, 'the original run stopped', e.args)) from e
        crashed = e                 # the original went on with its caller's register value
    if undef:
        oee = LAST['ee']
        assert undef in oee.hits, (where, 'the original never executed the UNDEFINED read', hex(undef))
        if crashed is None:
            nat.lock = SR.Lockstep(entries, last, ee.pre, ee.mem, ee.spad)
        rc, _ = native_call(case['fn'], C.byref(nat.ctx), case['args'], case.get('fargs', ()))
        if nat.error is not None:
            raise AssertionError((where, 'native worker error', repr(nat.error)))
        assert rc == -1 and nat.ctx.fault == 4 and nat.ctx.fault_address == undef \
            and nat.ctx.fault_function == case['fn'], \
            (where, 'the native must fault UNDEFINED at the read', rc, nat.ctx.fault, hex(nat.ctx.fault_address))
        return oee.outcomes, len(nat.log), 0, {}
    if fx is not None:
        nat.fx = fx.log
    nat.lock = SR.Lockstep(entries, last, ee.pre, ee.mem, ee.spad)
    rc, nres = native_call(case['fn'], C.byref(nat.ctx), case['args'], case.get('fargs', ()))
    if nat.error is not None:
        raise AssertionError((where, 'native worker error', repr(nat.error)))
    if rc == -1 and (case.get('fx') or case.get('store_site')) and nat.ctx.fault == 4 \
            and nat.ctx.fault_address in LAST['ee'].hits:
        return set(), 0, -1, {}             # a variant that turned the input into an UNDEFINED one
    if rc == -1 and (case.get('fx') or case.get('store_site')) and nat.ctx.fault == 3 and 0x2000000 <= nat.ctx.fault_address < 0x40000000:
        return set(), 0, -1, {}
    assert rc == 0, (where, 'native faulted', rc, nat.ctx.fault, hex(nat.ctx.fault_function),
                     hex(nat.ctx.fault_address))
    assert len(nat.log) == len(entries), (where, 'native makes fewer calls than the original', len(nat.log),
                                          len(entries), entries[len(nat.log)][0] if len(nat.log) < len(entries) else ())
    try:
        nat.lock.check(('after the last store',), last, nat.dirty(), nat.ee.mem, nat.ee.spad)
    except AssertionError as e:
        raise AssertionError((where,) + e.args) from e
    if case['fn'] in RETURNS:
        assert nres == ov0, (where, 'result', hex(nres), hex(ov0))
    if case['fn'] in FRETURNS:
        assert nres == ee.f[0] & MASK, (where, 'float result', hex(nres), hex(ee.f[0] & MASK))
    assert olog == nat.log, (where, 'call log differs')
    if not SR.same(nat.ram, ee.mem):
        raise AssertionError((where, 'RAM differs (address, native, original)',
                              SR.first_differences(bytes(nat.ram), bytes(ee.mem))))
    if not SR.same(nat.spad, ee.spad):
        raise AssertionError((where, 'scratchpad differs',
                              SR.first_differences(bytes(nat.spad), bytes(ee.spad), 0x70000000)))
    return ee.outcomes, len(olog), sum(len(v) for v in fx.log.values()) if fx else 0, ee.stores


# ======================================================================
# Cases
# ======================================================================

def u32(ram, a):
    return struct.unpack_from('<I', ram, a & 0x1FFFFFF)[0]


def s16(ram, a):
    return struct.unpack_from('<h', ram, a & 0x1FFFFFF)[0]


W8, W16, W32, WF, FB = SR.W8, SR.W16, SR.W32, SR.WF, SR.FB


def W64(a, v):
    return (a, struct.pack('<Q', v & MASK64))


def WV(a, *fs):
    return [WF(a + 4 * i, v) for i, v in enumerate(fs)]


def case(name, fn, args, script=None, writes=(), fargs=(), indirect=(), beat=BEAT, **kw):
    return dict(name=name, fn=fn, beat=beat, args=list(args), script=script or {}, writes=list(writes),
                fargs=list(fargs), indirect=list(indirect), **kw)


def seeds(rng, wants, beat=BEAT):
    """Writes making the original LCG (00122BB8) produce draws satisfying
    wants[i] (None = any) on its next calls (lane SYS seed_writes)."""
    return SR.seed_writes(image(beat)[0], rng, wants)


def ulps(x):
    """Bits of x and one ulp either side (the EE arithmetic decides which
    side of a threshold each lands on)."""
    b = FB(x)
    return [(b - 1) & MASK, b, (b + 1) & MASK]


A02_BEATS = ('a02_00_duct', 'a02_01_switch', 'a02_02_ladder_escape', 'a02_03_over_wreck', 'a02_04_panel',
             'a02_05_progression_exit', 'a02_s0_mts_bed')
A01R_BEATS = ('a01r_00_to_train_room', 'a01r_01_event', 'a01r_02_bridge', 'a01r_03_door16', 'a01r_s0_pickup',
              'a01r_s1_door14_locked')

# -- 0011C128 / 0011E520 -----------------------------------------------------

ASIN_EDGES = [0x00000000, 0x80000000, 0x00000001, 0x80000001, 0x00800000, 0x31FFFFFF, 0x32000000, 0xB2000000,
              0x3EFFFFFF, 0x3F000000, 0xBF000000, 0x3F799999, 0x3F79999A, 0xBF79999A, 0x3F7FFFFF, 0xBF7FFFFF,
              0x3F800000, 0xBF800000, 0x3F800001, 0xBF800001, 0x40000000, 0x7F7FFFFF, 0xFF7FFFFF, 0x7F800000,
              0xFF800000, 0x7FC00000, 0x7F800001, 0x3E800000, 0x3F400000, 0xBF400000, 0x3F666666, 0x3C000000]


def cases_asin(rng):
    out = []
    vals = list(ASIN_EDGES) + [FB(rng.uniform(-1.0, 1.0)) for _ in range(40)] + \
        [FB(rng.uniform(0.95, 1.0)) for _ in range(10)] + [FB(rng.uniform(-3.0, 3.0)) for _ in range(10)]
    for x in vals:
        out.append(case(f't 11C128 x{x:08x}', 0x11C128, [], fargs=[x]))
    # the wrapper: the mode word, 0011E080, the |x| > 1 test, the record
    rec = []
    for x in (0x3F000000, 0xBF800000, 0x3F800001, 0xBF800001, 0x40000000, 0x7F800000, 0x7FC00000, 0x7F800001,
              0x00000000):
        for mode in (1, -1, 2, 0):
            rec.append((x, mode, 0, 0, 0))
    for x in (0x3F800001, 0xC0400000):
        for mode in (1, 0):
            for mv in (0, 1, 1 << 32):
                for err in (0, 0x22, -1):
                    if mv == 0 and err == 0:
                        continue
                    rec.append((x, mode, mv, err, 0x3FF0000000000000 if err else 0))
    for x, mode, mv, err, ret in rec:
        outs = [(0, 0x18, struct.pack('<Q', ret)), (0, 0x20, struct.pack('<i', err))] if (err or ret) else []
        out.append(case(f't 11E520 x{x:08x} m{mode} mv{mv:#x} e{err} r{ret:x}', 0x11E520, [], fargs=[x],
                        writes=[W32(0x26C5D0, mode)], script={0x11DB90: [(mv, 0, outs)]}))
    return out


# -- 0015C750 / 0015C7C0 / 0021BD10 -------------------------------------------

CLIP_PAIRS = [(0x248A02, 0x248A00), (0x248A06, 0x248A04), (0x2754C2, 0x2754C0), (0x2754C6, 0x2754C4),
              (0x2754CA, 0x2754C8), (0x2754CE, 0x2754CC), (0x2754D2, 0x2754D0), (0x2754D6, 0x2754D4),
              (0x2754DA, 0x2754D8)]


def cases_clip(rng):
    out = []
    ram, _ = image()
    e = SCRATCH + 0x1000
    clips = [s16(ram, h) for h, _ in CLIP_PAIRS] + [s16(ram, CLIP_PAIRS[0][1]), s16(ram, 0x248A08), 0x7FFF, -1]
    for i, clip in enumerate(clips):
        for c8 in ((0, 4, 0xFB) if i == 0 else (0,)):
            out.append(case(f't 15C7C0 clip{i} {clip} c8 {c8:#x}', 0x15C7C0, [e],
                            writes=[W16(e + 0x20C, clip), W32(0x8106C8, c8)]))
    # the table halfwords themselves differ: each hurt clip a unique value
    # (so an off-by-one table read shows)
    uniq = []
    for k, (h, n) in enumerate(CLIP_PAIRS):
        uniq += [W16(h, 0x100 + k), W16(n, 0x200 + k)]
    uniq.append(W16(0x248A08, 0x300))
    for k in range(9):
        for c8 in ((0, 4) if k == 0 else (0,)):
            out.append(case(f't 15C7C0 uniq{k} c8 {c8}', 0x15C7C0, [e],
                            writes=uniq + [W16(e + 0x20C, 0x100 + k), W32(0x8106C8, c8)]))
    out.append(case('t 15C7C0 uniq none', 0x15C7C0, [e], writes=uniq + [W16(e + 0x20C, 0x200)]))
    link = SCRATCH + 0x2000
    for ln in (0, link):
        for i in (0, 3, 9 + 2):
            out.append(case(f't 15C750 link{ln:x} clip{i}', 0x15C750, [e],
                            writes=[W32(e + 0x1C, ln), W16(e + 0x20C, clips[i]), W8(e + 0x234, 7),
                                    W8(0x810707, 9), W8(link + 4, 0x55)]))
    out.append(case('cap 15C750 player', 0x15C750, [PLAYER]))
    for beat in A02_BEATS[:3]:
        out.append(case(f'cap {beat} 15C7C0 player', 0x15C7C0, [PLAYER], beat=beat))
    for b in (1, 0, 2):
        for v in (0, 1, 1 << 32, -1):
            out.append(case(f't 21BD10 b{b} v{v:#x}', 0x21BD10, [], writes=[W8(PLAYER, b)],
                            script={0x21BB00: [v]}))
    return out


# -- 001A8970 / 001A8E80 / 001A9360 / 001AA640 / 001AA700 --------------------

SELF, OTHER = SCRATCH + 0x3000, SCRATCH + 0x4000
EXT_S, EXT_O = SCRATCH + 0x5000, SCRATCH + 0x5100


def body(sx=0.0, sy=0.0, sz=0.0, sa4=0.0, ox=0.0, oy=0.0, oz=0.0, es=(1.0, 2.0, 1.0), eo=(3.0, 4.0, 3.0),
         k=0, st=1, extra=()):
    w = [W32(SELF + 0x30, EXT_S), W32(OTHER + 0x30, EXT_O), W32(OTHER + 0x34, HANDLER), W8(OTHER + 0xD, k),
         W8(SELF, st), WF(SELF + 0x220, 0.625), WF(SELF + 0x224, -9.0),
         WF(SELF + 0x22C, -8.0), W8(SELF + 0xF, 0x77), W16(0x70003B86, 0x1234), W16(0x70003B88, 0x5678),
         W8(SELF + 1, 0x5A), W8(0x81070B, 0xA5)]
    w += WV(SELF + 0xB0, sx, sy, sz, 1.0) + WV(OTHER + 0xB0, ox, oy, oz, 1.0) + WV(SELF + 0xA0, 1.0, sa4, 3.0, 1.0)
    w += WV(EXT_S, *es) + WV(EXT_O, *eo)
    return w + list(extra)


def cases_contact(rng):
    out = []
    # 001A8970: the three gates, each failing and passing (exact edges and
    # one ulp either side through the EE subtraction)
    for dx in (0.0, 2.5, 3.0, 3.5):
        for dz in (0.0, 3.0, 3.25):
            out.append(case(f't 1A8970 gate dx{dx} dz{dz}', 0x1A8970, [SELF, OTHER], indirect=[HANDLER],
                            writes=body(sx=10.0 + dx, ox=10.0, sz=-5.0 - dz, oz=-5.0, k=0, st=0)))
    for sy in (0.0, 4.0, 5.0, 6.0, -4.0, -5.0, -6.0):
        out.append(case(f't 1A8970 gate y {sy}', 0x1A8970, [SELF, OTHER], indirect=[HANDLER],
                        writes=body(sa4=sy, oy=0.0, k=0, st=0)))
    for bits in ulps(3.0):
        out.append(case(f't 1A8970 gate x ulp {bits:08x}', 0x1A8970, [SELF, OTHER], indirect=[HANDLER],
                        writes=body(ox=0.0, k=0, st=0) + [W32(SELF + 0xB0, bits)]))
        out.append(case(f't 1A8970 gate z ulp {bits:08x}', 0x1A8970, [SELF, OTHER], indirect=[HANDLER],
                        writes=body(oz=0.0, k=0, st=0) + [W32(SELF + 0xB8, bits)]))
    for bits in ulps(4.0):
        out.append(case(f't 1A8970 gate y ulp {bits:08x}', 0x1A8970, [SELF, OTHER], indirect=[HANDLER],
                        writes=body(oy=1.0, k=0, st=0) + [W32(SELF + 0xA4, bits)]))
    # after the handler: every reaction, state, table and f20 outcome
    for k in (5, 6, 7, 9, 0xC, 0xD, 0xE, 0, 0xB, 4, 8, 0xFF):
        for st in (1, 0, 3):
            for tog in (0, 1):
                if st != 1 and (tog or k not in (5, 6)):
                    continue
                for f20 in ((-1.0, 1.0, 0.0, 0.5, -0.5) if k in (6, 7) and st == 1 else (-1.0,)):
                    for pb, bb in (((1, 0), (1, 1), (0, 0)) if k == 0xE and st == 1 else ((1, 0),)):
                        out.append(case(f't 1A8970 k{k} st{st} t{tog} f20 {f20} pb{pb} bb{bb}', 0x1A8970,
                                        [SELF, OTHER], indirect=[HANDLER], f20=FB(f20),
                                        script={0x21BB00: [bb]},
                                        writes=body(k=k, st=st) + [W8(0x81070A, tog), W8(PLAYER, pb)]))
    for f20 in (0x80000000, 0x00000001, 0x7F800000, 0xFF800000, 0x7FC00000, 0x7F7FFFFF):
        out.append(case(f't 1A8970 k6 f20 bits {f20:08x}', 0x1A8970, [SELF, OTHER], indirect=[HANDLER], f20=f20,
                        writes=body(k=6, st=1)))
    # the reaction byte changed by the handler (scripted: the handler writes
    # a1 +0xD) and the table re-read of a1 +0xD
    for new in (5, 9, 0xC, 0xE, 7):
        out.append(case(f't 1A8970 handler sets k{new}', 0x1A8970, [SELF, OTHER], indirect=[HANDLER],
                        f20=FB(-2.0), script={HANDLER: [(0, 0, [(0, 0xD, bytes([new]))])]},
                        writes=body(k=0, st=1)))
    out.append(case('undef 1A8970 k6 f20 null', 0x1A8970, [SELF, OTHER], indirect=[HANDLER], f20_null=True,
                    undef=0x1A8AC4, writes=body(k=6, st=1)))
    out.append(case('undef 1A8970 k7 f20 null', 0x1A8970, [SELF, OTHER], indirect=[HANDLER], f20_null=True,
                    undef=0x1A8AC4, writes=body(k=7, st=1)))
    out.append(case('t 1A8970 k9 f20 null', 0x1A8970, [SELF, OTHER], indirect=[HANDLER], f20_null=True,
                    writes=body(k=9, st=1)))
    # 001A8E80: planar distance against 7 + ext, height against 8
    for dx, dz in ((3.0, 4.0), (6.0, 8.0), (0.0, 8.0), (0.0, 8.5), (8.0, 0.5)):
        for dy in (0.0, -2.0, 2.0, -14.0, -15.0, 3.0):
            out.append(case(f't 1A8E80 d{dx},{dz} y{dy}', 0x1A8E80, [SELF, OTHER],
                            writes=body(ox=dx, oz=dz, oy=dy, es=(1.0, 2.0, 1.0))))
    for sx, sz in ((2.0, -3.0), (-5.0, 4.0)):
        out.append(case(f't 1A8E80 self at {sx},{sz}', 0x1A8E80, [SELF, OTHER],
                        writes=body(sx=sx, sz=sz, ox=sx + 3.0, oz=sz + 4.0, es=(0.0, 2.0, 1.0))))
    for bits in ulps(8.0):
        out.append(case(f't 1A8E80 dist ulp {bits:08x}', 0x1A8E80, [SELF, OTHER],
                        writes=body(oz=0.0, es=(1.0, 2.0, 1.0)) + [W32(OTHER + 0xB0, bits)]))
        out.append(case(f't 1A8E80 height ulp {bits:08x}', 0x1A8E80, [SELF, OTHER],
                        writes=body() + [W32(SELF + 0xB4, bits), WF(OTHER + 0xB4, 10.0)]))
    # 001A9360: the dot of (0x700038B0 - a0 +0xB0) against (e0 + e1)^2
    for e0, e1 in ((2.0, 3.0), (2.0, 2.9), (3.0, 3.0), (0.0, 0.0), (-6.0, 1.0)):
        for w in (0.0, 7.0):
            out.append(case(f't 1A9360 e{e0},{e1} w{w}', 0x1A9360, [SELF, OTHER],
                            writes=body(sx=1.0, sy=1.0, sz=1.0, es=(e0, 2.0, 1.0), eo=(e1, 4.0, 3.0)) +
                            WV(0x700038B0, 4.0, 5.0, 1.0, w) + [W32(0x70003A20, 0x11111111), W32(0x70003A24, 7)]))
    # 001AA640: the three box gates at their edges
    for axis, lim in ((0, 50.0), (2, 46.0), (1, 40.0)):
        for bits in ulps(lim):
            wr = body(ox=0.0, oy=0.0, oz=0.0)
            wr.append(W32(SELF + 0xB0 + 4 * axis, bits))
            out.append(case(f't 1AA640 axis{axis} {bits:08x}', 0x1AA640, [SELF, OTHER],
                            writes=wr + [W16(OTHER + 0x36, 9)]))
    # 001AA700: synthetic tables, then every captured beat with the player
    tab = SCRATCH + 0x6800
    ents = [0, OTHER, SELF, SCRATCH + 0x4400, SCRATCH + 0x4800, SCRATCH + 0x4C00]
    ew = [W32(tab + 4 * i, e) for i, e in enumerate(ents)]
    ew += [W8(OTHER + 2, 0xE4), W8(OTHER + 3, 6), W8(OTHER, 1), W8(SELF + 2, 5), W8(SELF + 3, 6), W8(SELF, 1)]
    ew += [W8(SCRATCH + 0x4402, 4), W8(SCRATCH + 0x4403, 7), W8(SCRATCH + 0x4400, 1)]
    ew += [W8(SCRATCH + 0x4802, 0x24), W8(SCRATCH + 0x4803, 6), W8(SCRATCH + 0x4800, 2)]
    ew += [W8(SCRATCH + 0x4C02, 4), W8(SCRATCH + 0x4C03, 6), W8(SCRATCH + 0x4C00, 1)]
    ew += WV(OTHER + 0xB0, 1.0, 2.0, 3.0) + WV(SCRATCH + 0x4CB0, 60.0, 0.0, 0.0)
    for n in (0, 1, 2, 6):
        out.append(case(f't 1AA700 n{n}', 0x1AA700, [PLAYER], writes=ew + [W32(0x275B7C, tab), W16(0x275B84, n)] +
                        WV(PLAYER + 0xB0, 1.0, 2.0, 3.0)))
    for beat in A02_BEATS + A01R_BEATS:
        out.append(case(f'cap {beat} 1AA700 player', 0x1AA700, [PLAYER], beat=beat))
    return out


# -- 001B6AE0 -----------------------------------------------------------------

def cases_timer(rng):
    out = []
    a1, a2 = SCRATCH + 0x6000, SCRATCH + 0x6100
    for st in (0, 1, 2, 3, 0xFF):
        for busy in ((0, 1, 0x100) if st == 0 else (0,)):
            for t in ((58.0, 59.0, 60.0, 61.0, -1.0) if st == 1 else (5.0,)):
                for flag in ((1, 0, 2) if st == 2 else (0,)):
                    out.append(case(f't 1B6AE0 st{st} b{busy} t{t} f{flag}', 0x1B6AE0, [0x1234, a1, a2],
                                    writes=[W8(a1 + 4, st), W32(a2 + 8, busy), WF(a2 + 0x10, t),
                                            W32(a2 + 0x18, 0x77), W8(0x8106F4, flag)]))
    for bits in ulps(59.0):
        out.append(case(f't 1B6AE0 st1 ulp {bits:08x}', 0x1B6AE0, [0, a1, a2],
                        writes=[W8(a1 + 4, 1), W32(a2 + 0x10, bits)]))
    return out


# -- 001D3A30 / 001D3AC0 / 001D3C40 / 001D3CE0 / 001D66A0 ----------------------

def cases_list(rng):
    out = []
    for fn in (0x1D3A30, 0x1D3C40):
        for a0 in (0, 1, 2, 3):
            for t1 in (0, 5, 0x3FFFFFF):
                out.append(case(f't {fn:06X} a{a0} t{t1:x}', fn, [a0, 0x55 + a0],
                                writes=[W32(0x811CC0 + 0x9C, t1)]))
    for fn in (0x1D3AC0, 0x1D3CE0):
        for a0 in (0, 7, -1):
            out.append(case(f't {fn:06X} a{a0}', fn, [a0]))
    for beat in A02_BEATS[:2] + A01R_BEATS[:1]:
        out.append(case(f'cap {beat} 1D3CE0', 0x1D3CE0, [2], beat=beat))
    # 001D66A0: slot, position and radius floats, colours, angle
    xyzw = SCRATCH + 0x7000
    c2, c3 = SCRATCH + 0x7100, SCRATCH + 0x7110
    cw = [W32(c2 + 4 * i, 0x11 * (i + 1)) for i in range(4)] + [W32(c3 + 4 * i, 0x80 + i) for i in range(4)]
    for a0 in (0, 1, 3):
        for ang in (0.0, 0.5, -2.0, 3.1, 100.0):
            for p in ((100.0, 50.0, 28.0, 28.0), (-3.5, 7.25, 44.0, 12.0)):
                out.append(case(f't 1D66A0 a{a0} ang{ang} p{p[0]}', 0x1D66A0, [a0, xyzw, c2, c3], fargs=[FB(ang)],
                                writes=cw + WV(xyzw, *p)))
    # a slot word not 16-aligned at +0x10 (the quadword store drops the low bits)
    for off in (0x8008, 0x8004):
        out.append(case(f't 1D66A0 slot +{off:x}', 0x1D66A0, [0, xyzw, c2, c3], fargs=[FB(1.0)],
                        writes=cw + WV(xyzw, 1.0, 2.0, 3.0, 4.0) + [W32(0x811CC0 + 0x10, SCRATCH + off)] +
                        [W32(SCRATCH + 0x8000 + 4 * i, 0xA5A5A5A5) for i in range(12)]))
    return out


# -- 001E4A00 / 001E4CE0 -------------------------------------------------------

EMIT = SCRATCH + 0xA000
EB = EMIT + 0x1F0


def cases_flash(rng):
    out = []
    obj = SCRATCH + 0x9000
    for age in (0.0, 0.1, 0.36, 0.5, 1.0, 1.7, 1.8, 2.0, -1.0):
        for size in (1.0, 4.0, 0.0):
            out.append(case(f't 1E4A00 age{age} s{size}', 0x1E4A00, [obj], fargs=[FB(age), FB(size)]))
    for bits in ulps(1.8):
        out.append(case(f't 1E4A00 age ulp {bits:08x}', 0x1E4A00, [obj], fargs=[bits, FB(2.0)]))
    for bits in ulps(0.36):
        out.append(case(f't 1E4A00 t0.2 ulp {bits:08x}', 0x1E4A00, [obj], fargs=[bits, FB(2.0)]))
    # ramps that leave 0..255 (the clamps) and negative channels
    rw = WV(0x2541F0, -50.0, 300.0, 10.0, 128.0) + WV(0x254200, 400.0, -20.0, 255.0, 0.0) + \
        WV(0x254210, 1e30, -1e30, 0.5, 254.9) + WV(0x254220, -1.0, 256.0, 0.25, 255.5)
    for age in (0.2, 1.0, 1.6):
        out.append(case(f't 1E4A00 clamp age{age}', 0x1E4A00, [obj], fargs=[FB(age), FB(3.0)], writes=rw))
    return out


def emitter(st, var, seed=0x12345678, count=0, age=0.5, step=0.1, x=0.0, z=0.0, params=0x254258, extra=()):
    w = [W8(EMIT + 4, st), W8(EMIT + 0xD, var), W32(EMIT + 0x30, params), W8(EMIT, 0x33), W8(EMIT + 9, 0x44),
         W8(EMIT + 0xC, 0x55), W32(EMIT + 0x34, 0x66), W8(EMIT + 0xE, 0x5A), W8(EMIT + 5, 0x5B), W8(EMIT + 1, 0x5C)]
    w += WV(EMIT + 0xB0, 1.0, 2.0, 3.0, 1.0) + WV(EMIT + 0xC0, 0.0, 0.5, 0.0, 0.0)
    w += WV(EMIT + 0xD0, 2.0, 0.0, 0.0, 0.0, 0.0, 2.0, 0.0, 0.0, 0.0, 0.0, 2.0, 0.0, 5.0, 6.0, 7.0, 1.0)
    w += WV(EMIT + 0x100, x, 0.0, z, 1.0)
    w += [W32(EB, seed), W32(EB + 4, count), WF(EB + 8, age), WF(EB + 0xC, step)]
    return w + list(extra)


BURST_DEFINED = (2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 14)


def cases_emitter(rng):
    out = []
    # state 0 for every variant (then state 1 at once)
    for var in range(16):
        for h in (0xFFFFFF, 5):
            name = f't 1E4CE0 st0 v{var} h{h:x}'
            wr = emitter(0, var) + [W32(0x275C00, 0x100)]
            sc = {0x1CD070: [h]}
            if var in BURST_DEFINED:
                out.append(case(name, 0x1E4CE0, [EMIT], script=sc, writes=wr))
            elif h == 0xFFFFFF:
                out.append(case('undef ' + name[2:], 0x1E4CE0, [EMIT], script=sc, writes=wr, undef=0x1E555C))
    # state 1: every variant, both handle outcomes, ages both sides of 0.2
    for var in range(16):
        for h in (0xFFFFFF, 0x10, 1 << 32 | 0xFFFFFF):
            for age in (0.1, 0.5, 1.75):
                for c00 in (0x100, 0x101):
                    if c00 == 0x101 and (h != 0x10 or age != 0.5):
                        continue
                    name = f't 1E4CE0 st1 v{var} h{h:x} a{age} c{c00:x}'
                    wr = emitter(1, var, age=age) + [W32(0x275C00, c00), W32(0x70003A24, 0)]
                    sc = {0x1CD070: [h], 0x1CD2B0: [(0, FB(0.75))]}
                    if var in BURST_DEFINED:
                        out.append(case(name, 0x1E4CE0, [EMIT], script=sc, writes=wr))
                        continue
                    if h == 0xFFFFFF:
                        pc = 0x1E555C if age + 0.1 < 1.8 else None
                    else:
                        pc = 0x1E537C if age > 0.2 else 0x1E53EC
                    if pc is None:
                        out.append(case(name, 0x1E4CE0, [EMIT], script=sc, writes=wr))
                    else:
                        out.append(case('undef ' + name[2:], 0x1E4CE0, [EMIT], script=sc, writes=wr, undef=pc))
    # undefined variant with a non-zero 0x70003A24 left in the scratchpad: the stretch path, then the read
    out.append(case('undef 1E4CE0 st1 v0 stretch', 0x1E4CE0, [EMIT], script={0x1CD070: [0x10]},
                    writes=emitter(1, 0) + [WF(0x70003A24, 3.0)], undef=0x1E53EC))
    # the area box of variants 6 and 7 (h + 0x40000 lands on 0xFFFFFF or not)
    for var in (6, 7, 5):
        for key in ((0x10, 0x01), (0x10, 0x02), (0x11, 0x01)):
            for x, z in ((300.0, 200.0), (275.0, 200.0), (335.0, 200.0), (300.0, 170.0), (300.0, 230.0),
                         (274.0, 200.0), (300.0, 229.0), (300.0, 171.0), (334.0, 200.0), (276.0, 200.0)):
                if key != (0x10, 0x01) and (x, z) != (300.0, 200.0):
                    continue
                for h in (0xFBFFFF, 0x20):
                    out.append(case(f't 1E4CE0 box v{var} k{key[0]:x}{key[1]:02x} x{x} z{z} h{h:x}', 0x1E4CE0,
                                    [EMIT], script={0x1CD070: [h]},
                                    writes=emitter(1, var, x=x, z=z) + [W8(0x810700, key[0]),
                                                                        W8(0x810701, key[1])]))
    # the ease, the age limit and the frame limit
    for var in (4, 5, 14):
        for count in (29, 30, 998, 999, -5):
            for age, step in ((0.5, 0.1), (1.6, 0.25), (1.7, 0.05), (0.0, 0.0), (1.79, 0.01)):
                out.append(case(f't 1E4CE0 limit v{var} n{count} a{age} s{step}', 0x1E4CE0, [EMIT],
                                script={0x1CD070: [0xFFFFFF]},
                                writes=emitter(1, var, count=count, age=age, step=step)))
    for bits in ulps(1.8):
        out.append(case(f't 1E4CE0 age ulp {bits:08x}', 0x1E4CE0, [EMIT], script={0x1CD070: [0xFFFFFF]},
                        writes=emitter(1, 4, step=0.0) + [W32(EB + 8, bits)]))
    for bits in ulps(0.01):
        out.append(case(f't 1E4CE0 step ulp {bits:08x}', 0x1E4CE0, [EMIT], script={0x1CD070: [0xFFFFFF]},
                        writes=emitter(1, 4, age=0.3) + [W32(EB + 0xC, bits)]))
    for bits in ulps(0.2):
        out.append(case(f't 1E4CE0 age0.2 ulp {bits:08x}', 0x1E4CE0, [EMIT], script={0x1CD070: [0x10]},
                        writes=emitter(1, 6) + [W32(EB + 8, bits)]))
    # 0x70003A24 exactly zero / negative zero for the stretch test
    for a24 in (0x80000000, 0x00000001):
        out.append(case(f't 1E4CE0 a24 {a24:08x}', 0x1E4CE0, [EMIT], script={0x1CD070: [0x10]},
                        writes=emitter(1, 6) + [W32(0x70003A24, a24)]))
    # seeds: the draws' high halves
    for seed in (0, 0xFFFFFFFF, 0x80000000, 0x7FFF0000):
        out.append(case(f't 1E4CE0 seed {seed:08x}', 0x1E4CE0, [EMIT], script={0x1CD070: [0x10]},
                        writes=emitter(1, 9, seed=seed)))
    for st in (2, 3, 4, 0xFF):
        out.append(case(f't 1E4CE0 st{st}', 0x1E4CE0, [EMIT], writes=emitter(st, 4)))
    return out


# -- 001EBD20 / 001EC5F0 / 001EC9A0 / 001ECEF0 / 001EDAF0 ---------------------

def cases_packets(rng):
    out = []
    mat = SCRATCH + 0xB800
    mw = WV(mat, *[float(i) for i in range(16)])
    for fn in (0x1EBD20, 0x1EC5F0, 0x1EC9A0, 0x1ECEF0, 0x1EDAF0):
        for flag in (0, 1):
            for seed in (0x4CC99694, 0, 0xFFFFFFFF):
                if fn in (0x1EBD20, 0x1EC9A0, 0x1ECEF0) and seed:
                    continue
                if fn in (0x1EC5F0, 0x1EC9A0, 0x1EDAF0) and flag:
                    continue
                ram, _ = image()
                c34, c30 = u32(ram, 0x275C34), u32(ram, 0x275C30)
                out.append(case(f't {fn:06X} f{flag} s{seed:08x}', fn, [mat, 0x77],
                                writes=mw + [W32(c30 + 0x38, flag), W32(c34 + 4, seed)]))
    ram, _ = image()
    c34 = u32(ram, 0x275C34)
    for v in (0.0, 0.02, 0.5, -1.0, 0.0199999):
        out.append(case(f't 1EDAF0 ease {v}', 0x1EDAF0, [mat, 5], writes=mw + [WF(c34 + 8, v)]))
    for bits in ulps(0.02):
        out.append(case(f't 1EDAF0 ease ulp {bits:08x}', 0x1EDAF0, [mat, 5], writes=mw + [W32(c34 + 8, bits)]))
    for beat in A02_BEATS + A01R_BEATS:
        for fn in (0x1EC9A0, 0x1EDAF0, 0x1EBD20):
            out.append(case(f'cap {beat} {fn:06X}', fn, [PLAYER + 0xD0, 3], beat=beat))
    return out


# -- 001F4010 / 001F4E40 / 001F6B30 / 001F9660 / 001E49F0 ---------------------

def cases_rest(rng):
    out = []
    for ring in (0, 5, 0x7E, 0x7F, 0x80, -1):
        for a0 in (0, 3, 0x10):
            out.append(case(f't 1F4010 r{ring} i{a0}', 0x1F4010, [a0, 0x99], writes=[W32(0x275C40, ring)]))
    rec = SCRATCH + 0xB000
    for frame in (0, 1, 0x80, 0x100, 0x17F, 0xFFFFFFFF, 0x12345):
        for a2 in (1, 3, 7):
            for col in ((255, 128, 64, 200), (0x12345, 0, 0xFFFFFFFF, 0x10000)):
                if col[0] != 255 and (a2 != 3 or frame not in (0, 0x100)):
                    continue
                out.append(case(f't 1F4E40 f{frame:x} a{a2} c{col[0]:x}', 0x1F4E40, [0x8102B0 + 0xB0, rec, a2],
                                fargs=[FB(2.5)], writes=[W32(0x70003B68, frame)] +
                                [W32(rec + 4 * i, c) for i, c in enumerate(col)] + seeds(rng, [None])))
    for want in (lambda v: v >> 23 & 0xFF == 0, lambda v: v >> 23 & 0xFF == 0xFF):
        out.append(case(f't 1F4E40 draw {len(out)}', 0x1F4E40, [0x8102B0 + 0xB0, rec, 1], fargs=[FB(1.0)],
                        writes=[W32(0x70003B68, 0x40)] + [W32(rec + 4 * i, 0xFF) for i in range(4)] +
                        seeds(rng, [want])))
    for v in (0, 0x123456, 1 << 33, -1):
        out.append(case(f't 1F6B30 v{v:#x}', 0x1F6B30, [], script={0x1F6760: [v]}))
    src = SCRATCH + 0xC000
    sw = [W32(src + 4 * i, (0x01010101 * (i & 0xFF)) ^ 0x5A5A5A5A) for i in range(0x2F0 // 4)]
    for key in ((0x15, 0x00), (0x02, 0x00), (0x15, 0x01)):
        for a1 in (0x9C, 0x9D, 0x6E, 0x72, 0xA0, 0x00, (1 << 32) | 0x9D, 0xA1):
            out.append(case(f't 1F9660 k{key[0]:x}{key[1]:02x} a{a1:#x}', 0x1F9660, [src, a1],
                            writes=sw + [W8(0x810700, key[0]), W8(0x810701, key[1])]))
    out.append(case('t 1E49F0', 0x1E49F0, []))
    return out


def cases_random(rng, n):
    """Full mode: random perturbations of the emitter, the contact records and
    the timer."""
    out = []
    for i in range(n):
        kind = i % 4
        if kind == 0:
            var = rng.randrange(16)
            if var not in BURST_DEFINED:
                var = 4
            wr = emitter(rng.choice((0, 1, 1, 1)), var, seed=rng.getrandbits(32), count=rng.randrange(-2, 1000),
                         age=rng.choice((0.1, 0.2, 0.3, 1.0, 1.79, 1.9)), step=rng.choice((0.0, 0.01, 0.1, 0.5)),
                         x=rng.uniform(270, 340), z=rng.uniform(165, 235)) + \
                [W8(0x810700, rng.choice((0x10, 2))), W8(0x810701, rng.choice((1, 0))),
                 W32(0x275C00, rng.choice((0x100, 0x101, 0))), WF(0x70003A24, rng.choice((0.0, 2.0)))]
            out.append(case(f'r 1E4CE0 {i}', 0x1E4CE0, [EMIT], writes=wr,
                            script={0x1CD070: [rng.choice((0xFFFFFF, 0x10, 0xFBFFFF))],
                                    0x1CD2B0: [(0, FB(rng.uniform(0, 1)))]}))
        elif kind == 1:
            k = rng.choice((5, 6, 7, 9, 0xC, 0xD, 0xE, 0, 3))
            wr = body(sx=rng.uniform(-4, 4), ox=rng.uniform(-4, 4), sz=rng.uniform(-4, 4), oz=rng.uniform(-4, 4),
                      sa4=rng.uniform(-6, 6), oy=rng.uniform(-2, 2), k=k, st=rng.choice((0, 1, 1, 3))) + \
                [W8(0x81070A, rng.randrange(2)), W8(PLAYER, rng.choice((0, 1)))]
            out.append(case(f'r 1A8970 {i}', 0x1A8970, [SELF, OTHER], indirect=[HANDLER], writes=wr,
                            f20=FB(rng.uniform(-2, 2)), script={0x21BB00: [rng.randrange(2)]}))
        elif kind == 2:
            fn = rng.choice((0x1A8E80, 0x1AA640, 0x1A9360))
            wr = body(sx=rng.uniform(-60, 60), ox=rng.uniform(-60, 60), sz=rng.uniform(-60, 60),
                      oz=rng.uniform(-60, 60), sy=rng.uniform(-45, 45), oy=rng.uniform(-45, 45),
                      es=(rng.uniform(0, 5), 2.0, 1.0), eo=(rng.uniform(0, 5), 4.0, 3.0)) + \
                WV(0x700038B0, rng.uniform(-5, 5), rng.uniform(-5, 5), rng.uniform(-5, 5), 0.0)
            out.append(case(f'r {fn:06X} {i}', fn, [SELF, OTHER], writes=wr))
        else:
            x = FB(rng.uniform(-1.2, 1.2))
            out.append(case(f'r 11E520 {i}', 0x11E520, [], fargs=[x],
                            writes=[W32(0x26C5D0, rng.choice((1, 2, -1)))]))
    return out


def all_cases(rng):
    cs = cases_asin(rng) + cases_clip(rng) + cases_contact(rng) + cases_timer(rng) + cases_list(rng)
    cs += cases_flash(rng) + cases_emitter(rng) + cases_packets(rng) + cases_rest(rng)
    if RM.FULL:
        cs += cases_random(rng, 160)
    return cs


def store_variants(stored):
    """Store-site variants (lane SYS round 4, as EXITA / A00LOW): a site no
    case covers (every byte it stores differing from the byte before) gets a
    variant with its target bytes complemented beforehand; byte and halfword
    sites also get a variant with the bytes after the field set to 0xA5."""
    covered, cand = set(), {}
    for c, st in stored:
        for pc, targets in st.items():
            if all(all(o != n for o, n in zip(old, new)) for _, old, new in targets.values()):
                covered.add(pc)
            cand.setdefault(pc, (c, targets))
    out = []
    for pc in sorted(cand):
        c, targets = cand[pc]
        target, after = [], []
        for a, (size, _, new) in sorted(targets.items()):
            if STACK_LO <= a < STACK_HI:
                continue
            at = a if 0x70000000 <= a < 0x70004000 else a & 0x1FFFFFF
            if pc not in covered:
                target.append((at, bytes(b ^ 0xFF for b in new)))
            if size < 4:
                after.append((at + size, bytes([0xA5]) * size))
        for kind, poison in (('', target), (' after', after)):
            if poison:
                out.append(dict(c, name=f"store site {pc:06X}{kind} {c['name']}",
                                writes=list(c['writes']) + poison, store_site=pc))
    return out, len(cand), len(covered)


QUICK_CASES = 260
# Pinned in the default run: the exact-threshold cases (every ulp case),
# the UNDEFINED cases, and the cases the bounded mutation sweep needed
# (docs/AREA02_MISC.md section 5).
# A greedy cover of every branch outcome the deterministic cases take in the
# full run (EM_AREA02_MISC_COVER=1 prints it; it is data, not a claim: the
# default run prints its coverage).
QUICK_COVER = {
    'cap 15C750 player', 'cap a01r_00_to_train_room 1AA700 player', 'cap a01r_00_to_train_room 1EBD20',
    'cap a01r_00_to_train_room 1EDAF0', 'cap a01r_01_event 1EDAF0', 'cap a02_00_duct 1EBD20', 't 11C128 x32000000',
    't 11C128 x3f800000', 't 11C128 xbf79999a', 't 11E520 x00000000 m0 mv0x0 e0 r0',
    't 11E520 x3f000000 m-1 mv0x0 e0 r0', 't 11E520 x3f800001 m0 mv0x0 e-1 r3ff0000000000000',
    't 11E520 x3f800001 m0 mv0x1 e0 r0', 't 11E520 x3f800001 m2 mv0x0 e0 r0', 't 11E520 x7f800001 m0 mv0x0 e0 r0',
    't 15C750 link0 clip3', 't 15C750 link1e02000 clip0', 't 15C7C0 clip0 10 c8 0x4', 't 15C7C0 clip1 85 c8 0x0',
    't 15C7C0 clip2 142 c8 0x0', 't 15C7C0 clip4 180 c8 0x0', 't 15C7C0 clip5 220 c8 0x0',
    't 15C7C0 clip6 256 c8 0x0', 't 15C7C0 clip7 257 c8 0x0', 't 15C7C0 clip8 334 c8 0x0',
    't 1A8970 gate dx0.0 dz0.0', 't 1A8970 gate dx0.0 dz3.25', 't 1A8970 gate dx3.5 dz0.0', 't 1A8970 gate y 5.0',
    't 1A8970 handler sets k12', 't 1A8970 handler sets k14', 't 1A8970 handler sets k5',
    't 1A8970 handler sets k9', 't 1A8970 k14 st1 t0 f20 -1.0 pb0 bb0', 't 1A8970 k14 st1 t0 f20 -1.0 pb1 bb1',
    't 1A8970 k6 f20 bits 7f7fffff', 't 1A8970 k6 st1 t1 f20 -1.0 pb1 bb0', 't 1A8E80 d0.0,8.0 y-14.0',
    't 1A8E80 d0.0,8.0 y-15.0', 't 1A8E80 d0.0,8.5 y-14.0', 't 1A9360 e-6.0,1.0 w0.0', 't 1A9360 e0.0,0.0 w0.0',
    't 1AA640 axis1 42200001', 't 1AA700 n0', 't 1AA700 n6', 't 1B6AE0 st0 b0 t5.0 f0', 't 1B6AE0 st0 b1 t5.0 f0',
    't 1B6AE0 st1 b0 t-1.0 f0', 't 1B6AE0 st1 b0 t60.0 f0', 't 1B6AE0 st2 b0 t5.0 f0', 't 1B6AE0 st2 b0 t5.0 f1',
    't 1B6AE0 st255 b0 t5.0 f0', 't 1D66A0 a0 ang-2.0 p-3.5', 't 1E4A00 age ulp 3fe66666',
    't 1E4CE0 box v5 k1001 x274.0 z200.0 h20', 't 1E4CE0 box v6 k1001 x274.0 z200.0 h20',
    't 1E4CE0 box v6 k1001 x276.0 z200.0 h20', 't 1E4CE0 box v6 k1001 x300.0 z170.0 h20',
    't 1E4CE0 box v6 k1001 x300.0 z230.0 h20', 't 1E4CE0 box v6 k1001 x335.0 z200.0 h20',
    't 1E4CE0 limit v14 n30 a0.0 s0.0', 't 1E4CE0 st0 v10 h5', 't 1E4CE0 st0 v12 h5', 't 1E4CE0 st0 v13 h5',
    't 1E4CE0 st0 v4 h5', 't 1E4CE0 st0 v9 h5', 't 1E4CE0 st1 v15 hffffff a1.75 c100',
    't 1E4CE0 st1 v7 h10 a0.5 c101', 't 1E4CE0 st2', 't 1E4CE0 st255', 't 1E4CE0 st3', 't 1ECEF0 f0 s00000000',
    't 1ECEF0 f1 s00000000', 't 1F4010 r-1 i0', 't 1F4010 r127 i0', 't 1F4E40 draw 41', 't 1F4E40 f100 a1 cff',
    't 1F9660 k1500 a0x0', 't 1F9660 k1500 a0x9d', 't 1F9660 k1500 a0xa0', 't 1F9660 k1501 a0x0',
    't 1F9660 k1501 a0x6e', 't 1F9660 k1501 a0x72', 't 1F9660 k1501 a0x9c', 't 1F9660 k1501 a0x9d',
    'undef 1E4CE0 st0 v15 hffffff',
}
QUICK_PINNED = QUICK_COVER | set()


def quick_keep(i, c):
    n = c['name']
    return n.startswith(('cap', 'undef', 'k ', 't 1E4CE0 st0', 't 1A8E80 self at')) or ' ulp ' in n or n in QUICK_PINNED \
        or (n.startswith('t 1A8970 k6 st1') and 'f20 0.5' in n or 'f20 -0.5' in n)


FX_FNS = {0x11E520, 0x15C750, 0x15C7C0, 0x1A8970, 0x1A9360, 0x1B6AE0, 0x1D3A30, 0x1D3C40, 0x1D3AC0, 0x1D3CE0,
          0x1E4CE0, 0x1EBD20, 0x1EC5F0, 0x1EC9A0, 0x1ECEF0, 0x1EDAF0, 0x1F4010, 0x1F4E40, 0x1F9660, 0x21BD10}


def fx_cases(cases):
    out = []
    for c in cases:
        if c['fn'] not in FX_FNS or c.get('undef'):
            continue
        out.append(dict(c, name='fx ' + c['name'], fx=True))
        out.append(dict(c, name='fx before ' + c['name'], fx=True, fx_before=True))
    return out


# ======================================================================
# Fail-stop API
# ======================================================================

def api_checks():
    n = 0
    ram, spad = image()
    nat = NativeRun(ram, spad, Script({}), set(), None)
    N = NATIVE
    out = U32(0)
    iout = I32(0)
    assert N.em_area02_misc_0021BD10(None, C.byref(iout)) == -1
    assert N.em_area02_misc_001E49F0(None) == -1
    n += 2
    ctx = C.byref(nat.ctx)
    assert N.em_area02_misc_0011C128(ctx, U32(0), None) == -1 and nat.ctx.fault == 1
    assert nat.ctx.fault_function == 0x11C128
    n += 1
    assert N.em_area02_misc_0011C128(ctx, U32(0), C.byref(out)) == -1     # latched
    n += 1
    N.em_area02_misc_clear_fault(ctx)
    N.em_area02_misc_clear_fault(None)
    assert nat.ctx.fault == 0 and nat.ctx.fault_function == 0 and nat.ctx.fault_address == 0
    assert N.em_area02_misc_0011C128(ctx, U32(0x3F800000), C.byref(out)) == 0 and out.value == 0x3FC90FDA  # checked against the oracle by the t 11C128 cases
    n += 1
    # an unmapped record: the first access faults with its address
    assert N.em_area02_misc_001B6AE0(ctx, U32(0x3000000), U32(SCRATCH), C.byref(iout)) == -1
    assert nat.ctx.fault == 3 and nat.ctx.fault_address == 0x3000004 and nat.ctx.fault_function == 0x1B6AE0
    n += 1
    N.em_area02_misc_clear_fault(ctx)
    # a NULL worker: fault at the first call, naming the callee
    nat.ctx.call = WORKER()
    assert N.em_area02_misc_001F6B30(ctx) == -1
    assert nat.ctx.fault == 1 and nat.ctx.fault_address == 0x1F6760 and nat.ctx.fault_function == 0x1F6B30
    n += 1
    N.em_area02_misc_clear_fault(ctx)
    # a worker returning < 0
    nat.ctx.call = WORKER(lambda _c, _p: -1)
    assert N.em_area02_misc_0021BD10(ctx, C.byref(iout)) in (0, -1)
    N.em_area02_misc_clear_fault(ctx)
    nat.ram[PLAYER] = 1
    assert N.em_area02_misc_0021BD10(ctx, C.byref(iout)) == -1
    assert nat.ctx.fault == 2 and nat.ctx.fault_address == 0x21BB00 and nat.ctx.fault_function == 0x21BD10
    n += 1
    N.em_area02_misc_clear_fault(ctx)
    # the UNDEFINED fault of 001E4CE0 (variant 0, state 1, no handle, young)
    nat.ctx.call = nat.worker
    for a, data in emitter(1, 0, age=0.5):
        for k, b in enumerate(data):
            nat.ram[a + k] = b
    nat.script = Script({0x1CD070: [0xFFFFFF]})
    assert N.em_area02_misc_001E4CE0(ctx, U32(EMIT)) == -1
    assert nat.ctx.fault == 4 and nat.ctx.fault_address == 0x1E555C and nat.ctx.fault_function == 0x1E4CE0
    n += 1
    N.em_area02_misc_clear_fault(ctx)
    return n


# ======================================================================
# Main
# ======================================================================

def setup():
    global NATIVE, ELF
    SR.FUNC_RANGES = tuple((fn, fn + size) for fn, size in sorted(FUNCS.items()))
    ELF = read_elf()
    SR.ELF = ELF
    base = FallEE(ELF)
    SR.ELF_IMAGE = bytes(base.mem[:SR.ELF_END])
    SR.BRANCH_PCS.clear()
    for fn, size in FUNCS.items():
        missing = SR.jal_targets(base.mem, fn, size) - set(CALLEES) - set(FUNCS)
        assert not missing, ('direct call targets without a policy', hex(fn), [hex(m) for m in missing])
        SR.BRANCH_PCS.update(SR.branch_pcs(base.mem, fn, size))
    scan = SR.RegScan(base.mem)
    bad = []
    for fn, (kind, na, nf, _) in sorted(CALLEES.items()):
        ri, rf, _notes = scan.reads(fn)
        ints = {r for r in range(36) if ri >> r & 1}
        floats = {r for r in range(SR.VU_BASE) if rf >> r & 1}
        extra_i = sorted(r for r in ints if not 4 <= r < 4 + na and r not in (0, 28, 29, 31)
                         and not (kind == S and SR.SAVED_READ_MASK_I >> r & 1))
        extra_f = sorted(r for r in floats if not 12 <= r < 12 + nf and not (kind == S and 20 <= r < 32))
        if extra_i or extra_f:
            bad.append((hex(fn), kind, extra_i, extra_f))
    assert not bad, ('callee register reads outside the compared policy', bad)
    NATIVE = build_native()
    return base


def main():
    t0 = time.time()
    setup()
    rng = random.Random(0xA02C)
    cases = all_cases(rng)
    names = [c['name'] for c in cases]
    dup = {n for n in names if names.count(n) > 1}
    assert not dup, ('duplicate case names', sorted(dup)[:5])
    only = os.environ.get('EM_AREA02_MISC_ONLY')
    if only:
        cases = [c for c in cases if c['name'].startswith(tuple(only.split(',')))]
    total = len(cases)
    if not (RM.FULL or only):
        cases = RM.select(cases, QUICK_CASES, 0xA02C, axes=(lambda c: c['fn'],), keep=quick_keep)
    fxs = fx_cases(cases)
    if not (RM.FULL or only):
        fxs = RM.select(fxs, 50, 0xA02C, axes=(lambda c: (c['fn'], c.get('fx_before', False)),))
    batch = cases + fxs
    outcomes, calls, fxw, skipped, stored = set(), 0, 0, 0, []
    per_case = {}
    for c, (o, n, w, st) in zip(batch, RM.parallel_map(run_case, batch)):
        outcomes |= o
        per_case[c['name']] = o
        calls += n
        if w == -1:
            skipped += 1
        else:
            fxw += w
        if not c.get('fx') and not c.get('undef'):
            stored.append((c, st))
    sites, nsites, ncovered = store_variants(stored)
    if not (RM.FULL or only):
        sites = RM.select(sites, 30, 0xA02C)
    site_skipped = 0
    for c, (o, n, w, st) in zip(sites, RM.parallel_map(run_case, sites)):
        outcomes |= o
        calls += n
        site_skipped += w == -1
    api = api_checks()
    per = {}
    for fn, size in FUNCS.items():
        pcs = [pc for pc in SR.BRANCH_PCS if fn <= pc < fn + size]
        both = sum(1 for pc in pcs if (pc, True) in outcomes and (pc, False) in outcomes)
        per[fn] = (both, len(pcs))
    print('branch coverage (both outcomes / conditional branches): ' +
          ', '.join(f'{fn:06X} {per[fn][0]}/{per[fn][1]}' for fn in sorted(FUNCS)))
    missing = sorted((pc, t) for pc in SR.BRANCH_PCS for t in (True, False) if (pc, t) not in outcomes)
    if os.environ.get('EM_AREA02_MISC_GAPS'):
        for fn in sorted(FUNCS):
            miss = [f'{pc:06X}{"T" if t else "F"}' for pc, t in missing if fn <= pc < fn + FUNCS[fn]]
            if miss:
                print(f'  {fn:06X} missing outcomes:', ' '.join(miss))
    if os.environ.get('EM_AREA02_MISC_COVER'):
        # a greedy cover of every outcome the ordinary cases take (for QUICK_COVER)
        pool = sorted(n for n in per_case if n.startswith(('t ', 'cap', 'undef', 'k ')))
        left, cover = set().union(*[per_case[n] for n in pool]), []
        while left:
            best = max(pool, key=lambda n: len(per_case[n] & left))
            if not per_case[best] & left:
                break
            cover.append(best)
            left -= per_case[best]
        print('QUICK_COVER = ' + repr(sorted(cover)))
        tset = set().union(*[o for n, o in per_case.items() if n.startswith(('t ', 'cap', 'undef', 'k '))])
        allo = set().union(*[o for n, o in per_case.items() if not n.startswith('fx')])
        print('outcomes only random cases take:', sorted(f'{pc:06X}{"T" if t else "F"}' for pc, t in allo - tset))
    if RM.FULL and not only:
        assert set(missing) == set(UNREACHABLE), ('full-mode branch coverage gaps',
                                                  [f'{pc:06X}{"T" if t else "F"}' for pc, t in missing
                                                   if (pc, t) not in UNREACHABLE])
        print('full-mode branch coverage asserted: every outcome taken')
    RM.banner(RM.part(len(cases), total, 'cases'), f'{len(fxs)} stub side-effect cases ({fxw} scripted writes, '
              f'{skipped} not comparable)',
              f'{len(sites)} store-site variants ({nsites} store sites, {ncovered} covered by the cases, '
              f'{site_skipped} not comparable)',
              f'{calls} worker calls compared at entry and after the last store',
              f'{len(CALLEES)} callee policies checked against measured register reads',
              f'{api} fail-stop API checks')
    print(f'test_area02_misc_reference: OK ({time.time() - t0:.1f} s)')


if __name__ == '__main__':
    main()
