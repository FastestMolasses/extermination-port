#!/usr/bin/env python3
"""Execute the original AREA01 lane-EXITA routines and compare em_area01_exita.c.

docs/AREA01_EXITA.md. The user's pinned ELF and the captured AREA01 exit RAM
(../Extermination/build/s87/route_a01/a01_07_level_exit/, the end-of-beat
image with AREA00 resident: the beat in which every one of these routines
ran for the first time) supply every instruction and every table; none are
embedded here.

The oracle is FallEE (tools/test_player_fall_reference.py: every COP1 and
VU0 macro op through tools/ee_float_model.py) running the ORIGINAL routine
over a copy of the capture, with the lane-SYS recording interpreter SysEE
(tools/test_area01_sys_reference.py) for branch outcomes and stored lines.
The native module runs over another copy of the same bytes. Every call that
leaves the translated set is caught on both sides and handled by the same
Python policy:
  run   the original callee runs (vector / matrix leaves, the math leaves,
        the LCG 00122BB8, the fill 00121A28, and 001B1240): in the
        oracle inside the same interpreter, on the native side in a second
        interpreter bound to the native module's own memory;
  stub  the callee is recorded and returns a scripted value (all 64 bits
        of v0 when the script gives a value outside the 32-bit range); in
        the 'fx' cases it also has a scripted side effect (below).
Calls among the twelve translated routines are not intercepted: the
original runs its own code, the native module calls its own translations.

Lockstep (the lane-SYS design). The original runs first and records, at
every call leaving the set, the callee entry and every RAM / scratchpad line
it stored to since the previous call. The native module is built with its
store trace (EM_AREA01_EXITA_STORE_TRACE) and, at the same call, must match
BEFORE any stub side effect is applied: the callee, the stack pointer, every
argument register the policy compares (a0..t3 as 64-bit images, f12..f19),
every register the native module says it sets, the bytes behind stack
arguments (the 0x60-byte block of 0022DCD0, the ninth argument of 0010E8A8
at the call's stack pointer), the bytes behind stub pointer arguments, and
all of RAM and the scratchpad (only lines either side stored to since the
last check can differ, so exactly those are compared). The same memory
check runs after the last store, then all 32 MiB, the scratchpad and the
result.

Stub side effects ('fx' cases, lane SYS round 1 and 5). 'fx': after the
k-th stub every field the translated routines load after it (before the
next stub) changes (bytes / halfwords bit 0, words bit 4; the handler
pointer +0x4C moves to a second stubbed address), so a value cached across
a stub differs. 'fx before': at the k-th stub every field loaded since the
previous stub changes, so a read deferred across a stub differs.

Cases: 'cap' the captured records; 't' enumerated paths (every selector /
state / sub-state, both sides of every test, LCG seeds solved so the
original's own draws take each arm, edge values); 'r' random perturbations
(full mode). EM_TEST_FULL=1 runs every case and every fx variant and
asserts that both outcomes of every conditional branch in the twelve
routines are taken except the documented UNREACHABLE ones; the default run
runs the 't' and 'cap' cases, a fixed sample of fx variants and the API
checks. EM_AREA01_EXITA_ONLY=<prefix,...> runs only matching cases;
EM_AREA01_EXITA_SOURCE=<file> tests another copy of the module source
(mutation sweeps); EM_AREA01_EXITA_GAPS=1 lists branch outcomes not taken.
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
import area01_reference_view as AV  # noqa: E402
import test_player_slide_reference as shared  # noqa: E402
from test_player_slide_reference import read_elf, sx32  # noqa: E402
from test_player_fall_reference import FallEE  # noqa: E402
import test_area01_sys_reference as SR  # noqa: E402

MASK, MASK64 = 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF
DECOMP = ROOT.parent / 'Extermination'
ROUTE = DECOMP / 'build/s87/route_a01'
BEAT = 'a01_07_level_exit'
OUT = ROOT / 'build' / 'area01' / 'exita'
STACK_BASE, STACK_SIZE = 0x7F000000, 0x100000
STACK_LO, STACK_HI = SR.STACK_LO, SR.STACK_HI
SP = shared.STACK_TOP

# Translated routines (address -> size in bytes, from the census).
FUNCS = {
    0x113478: 0xB4, 0x1195A8: 0xA8, 0x128390: 0x34, 0x128600: 0x40, 0x128640: 0x1A4, 0x1289C0: 0xF0,
    0x128AB0: 0xCC, 0x129780: 0x77C, 0x12A5D0: 0x7F0, 0x12ADC0: 0x1FC, 0x12AFC0: 0x444, 0x22DCD0: 0x95C,
}

# Callee policy: address -> (kind, integer argument registers compared
# (a0..t3), float argument registers compared (f12..f19), [(pointer
# argument, bytes)] snapshotted at a stub's entry). Checked against the
# measured register reads (lane SYS RegScan) in main().
R, S = 'run', 'stub'
CALLEES = {
    # run: leaves and math, executed as original code on both sides
    0x1026A0: (R, 3, 0, ()), 0x102718: (R, 3, 0, ()), 0x1027E0: (S, 2, 0, ((1, 16),)), 0x1028B8: (R, 3, 0, ()),
    0x1028D0: (R, 3, 0, ()), 0x102948: (R, 2, 0, ()), 0x102958: (R, 2, 0, ()), 0x1029C0: (R, 1, 0, ()),
    0x102B08: (R, 2, 1, ()), 0x102BB0: (R, 2, 1, ()), 0x1031E0: (R, 2, 0, ()), 0x103230: (R, 2, 1, ()),
    0x11E748: (R, 0, 1, ()), 0x121A28: (R, 3, 0, ()), 0x122BB8: (R, 0, 0, ()), 0x1281C0: (R, 0, 1, ()),
    0x1B12B0: (R, 0, 3, ()), 0x1B13F0: (R, 2, 1, ()), 0x1B1470: (R, 0, 1, ()), 0x1B1240: (R, 1, 2, ()),
    # stubs: 00113478
    0x112E28: (S, 1, 0, ()), 0x10E8A8: (S, 8, 0, ()), 0x10B840: (S, 1, 0, ()),
    # 00128AB0 / 00129780
    0x1B10B0: (S, 3, 0, ()), 0x1C2430: (S, 3, 0, ()), 0x19AD00: (S, 3, 0, ((1, 16),)),
    0x1C2540: (S, 4, 0, ((2, 16),)), 0x1C3BE0: (S, 2, 0, ()), 0x128830: (S, 1, 3, ()),
    0x1287F0: (S, 3, 1, ()), 0x1C69A0: (S, 1, 0, ()), 0x1C3DB0: (S, 4, 0, ((0, 16), (1, 16))),
    # 0012A5D0
    0x1C63E0: (S, 2, 0, ()), 0x1B2140: (S, 1, 0, ()), 0x1B0D80: (S, 1, 0, ()), 0x1B17A0: (S, 1, 0, ()),
    0x128B80: (S, 2, 0, ()), 0x12B410: (S, 2, 0, ()), 0x12B970: (S, 2, 0, ()), 0x12BE20: (S, 2, 0, ()),
    0x12C490: (S, 2, 0, ()), 0x12CAA0: (S, 2, 0, ()), 0x12D240: (S, 2, 0, ()), 0x1C2770: (S, 3, 0, ()),
    0x12D580: (S, 3, 0, ()), 0x1C3D60: (S, 2, 0, ()), 0x12D850: (S, 2, 0, ()), 0x12D940: (S, 2, 0, ()),
    0x12DD70: (S, 2, 0, ()), 0x12B850: (S, 2, 0, ()), 0x1C25E0: (S, 2, 0, ((1, 16),)),
    0x1C64F0: (S, 1, 1, ()), 0x1288D0: (S, 2, 0, ()), 0x129FC0: (S, 2, 0, ()), 0x12E070: (S, 1, 0, ()),
    0x1B1190: (S, 1, 0, ()), 0x1AFC10: (S, 1, 0, ()), 0x1B1630: (S, 0, 3, ()),
    # 0022DCD0
    0x1D0400: (S, 2, 1, ()), 0x1CCF70: (S, 1, 0, ((0, 16),)), 0x1CFB50: (S, 3, 5, ((2, 64),)),
    0x1CFBE0: (S, 5, 0, ((2, 16),)), 0x1FC3C0: (S, 3, 2, ((1, 4),)),
}
INDIRECT = (S, 1, 0, ())    # the +0x4C pointer of 0012A5D0, called with the record
# Stack bytes a stub reads besides its registers: (callee) -> [(offset from
# its stack pointer, or ('arg', i) for a pointer argument into the stack,
# bytes)]. 0010E8A8 takes its ninth argument at its stack pointer; the
# 0x60-byte request block of 0022DCD0 is compared whole.
STACK_READS = {0x10E8A8: ((0, 4),), 0x1CFB50: ((('arg', 0), 0x60),), 0x1CFBE0: ((('arg', 3), 0x60),)}

# Conditional-branch outcomes the full sweep cannot take, with the proof.
UNREACHABLE = {
    (0x22E3E0, True): "0022DCD0's per-entry request loop is always entered: every (first, count) pair its "
                      "switch on 001281C0's result assigns ((1, 2), (3, 6), (2, 5), (2, 8), (0, 1)) has "
                      "first < count",
}


def policy(fn, indirect):
    if fn in CALLEES:
        return CALLEES[fn]
    if fn in indirect:
        return INDIRECT
    raise AssertionError(('call without a policy', hex(fn)))


def log_entry(ee, fn, pol, regs, fregs, sp):
    """What the callee can see at its entry besides memory: the stack
    pointer, the compared argument registers, the stack bytes it reads, and
    for stubs the bytes behind its pointer arguments."""
    kind, na, nf, snaps = pol
    args = tuple(regs[i] & MASK64 for i in range(na))
    floats = tuple(fregs[i] & MASK for i in range(nf))
    stack = []
    for where, n in STACK_READS.get(fn, ()):
        a = (regs[where[1]] & MASK) if isinstance(where, tuple) else (sp + where) & MASK
        stack.append(ee.read(a, n))
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
    def __init__(self, spec):
        self.queues = {fn: list(values) for fn, values in (spec or {}).items()}

    def take(self, fn):
        q = self.queues.get(fn)
        if q:
            v = q.pop(0)
            return v if isinstance(v, tuple) else (v, 0)
        return (0, 0)


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
                ('sp', U32), ('fault', I32), ('fault_function', U32), ('fault_address', U32), ('view', C.c_void_p)]


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
uint32_t a01exa_trace_list[LINES];
uint32_t a01exa_trace_count;
void a01exa_trace_store(uint32_t address, unsigned size);
void a01exa_trace_reset(void);
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
        a01exa_trace_list[a01exa_trace_count++] = id;
    }
}
void a01exa_trace_store(uint32_t address, unsigned size)
{
    mark(address);
    mark(address + size - 1u);
}
void a01exa_trace_reset(void)
{
    uint32_t i;
    for (i = 0; i < a01exa_trace_count; i++)
        dirty[a01exa_trace_list[i]] = 0;
    a01exa_trace_count = 0;
}
"""


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    trace = OUT / 'store_trace.c'
    if not trace.exists() or trace.read_text() != TRACE_C:
        trace.write_text(TRACE_C)
    source = os.environ.get('EM_AREA01_EXITA_SOURCE', '')
    ext = 'dylib' if sys.platform == 'darwin' else 'so'
    if source:
        source = str(Path(source).resolve())
        lib = Path(source).with_suffix('.' + ext)
        rebuild = not lib.exists() or Path(source).stat().st_mtime > lib.stat().st_mtime
    else:
        source = str(ROOT / 'src/game/em_area01_exita.c')
        lib = OUT / f'area01_exita.{ext}'
        deps = [Path(source), ROOT / 'src/game/em_area01_exita.h', ROOT / 'src/game/em_ee_float.h', trace,
                Path(__file__)]
        rebuild = not lib.exists() or max(d.stat().st_mtime for d in deps) > lib.stat().st_mtime
    if rebuild:
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                        '-ffp-contract=off', '-shared', '-fPIC', '-Isrc', '-Isrc/game',
                        '-DEM_AREA01_EXITA_STORE_TRACE=a01exa_trace_store', source, str(trace),
                        '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    TRACE['count'] = U32.in_dll(native, 'a01exa_trace_count')
    TRACE['list'] = (U32 * (RAM_LINES + 0x4000 // LINE)).in_dll(native, 'a01exa_trace_list')
    TRACE['reset'] = native.a01exa_trace_reset
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

    def __init__(self, ram, spad, script, indirect):
        # one set of native buffers per process, refilled per case (a fresh
        # 32 MiB allocation per case costs more than the case itself)
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
        self.ctx = Ctx(self.regions, 3, self.worker, None, SP, 0, 0, 0)

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
                # memory first, before any stub side effect is applied
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
                v0, f0 = self.script.take(c.fn)
                v0 = v0_image(v0)
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


def native_call(fn, ctx, args, fargs):
    out = I32(0)
    N = NATIVE
    a = [x & MASK for x in list(args) + [0] * 4]
    rc = None
    if fn == 0x113478:
        rc = N.em_area01_exita_00113478(ctx, C.byref(out))
    elif fn == 0x1195A8:
        rc = N.em_area01_exita_001195A8(ctx, U32(a[0]), C.byref(out))
    elif fn == 0x128390:
        rc = N.em_area01_exita_00128390(ctx, U32(a[0]), I32(SR.s32(a[1])), C.byref(out))
    elif fn == 0x128600:
        rc = N.em_area01_exita_00128600(ctx, I32(SR.s32(a[0])), C.byref(out))
    elif fn == 0x128640:
        rc = N.em_area01_exita_00128640(ctx, U32(a[0]), C.byref(out))
    elif fn == 0x1289C0:
        return N.em_area01_exita_001289C0(ctx, U32(a[0]), U32(a[1])), None
    elif fn == 0x128AB0:
        rc = N.em_area01_exita_00128AB0(ctx, U32(a[0]), U32(a[1]), C.byref(out))
    elif fn == 0x129780:
        rc = N.em_area01_exita_00129780(ctx, U32(a[0]), U32(a[1]), U32(a[2]), C.byref(out))
    elif fn == 0x12A5D0:
        return N.em_area01_exita_0012A5D0(ctx, U32(a[0])), None
    elif fn == 0x12ADC0:
        rc = N.em_area01_exita_0012ADC0(ctx, U32(a[0]), U32(a[1]), U32(a[2]), U32(fargs[0]), C.byref(out))
    elif fn == 0x12AFC0:
        return N.em_area01_exita_0012AFC0(ctx, U32(a[0]), U32(a[1])), None
    elif fn == 0x22DCD0:
        return N.em_area01_exita_0022DCD0(ctx, U32(a[0])), None
    else:
        raise AssertionError(('no native entry', hex(fn)))
    return rc, out.value & MASK


RETURNS = {0x113478, 0x1195A8, 0x128390, 0x128600, 0x128640, 0x128AB0, 0x129780, 0x12ADC0}


# ======================================================================
# Oracle side
# ======================================================================

def image(beat=BEAT):
    if beat not in IMAGES:
        IMAGES[beat] = ((ROUTE / beat / 'eeMemory.bin').read_bytes(), (ROUTE / beat / 'scratchpad.bin').read_bytes())
    return IMAGES[beat]


def prepared(case):
    """The case's starting RAM and scratchpad, in this process's reused
    buffers (every use of the previous case's buffers has ended)."""
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


def oracle_run(case, ram, spad, cls=None, on_stub=None):
    """The original over `ram` / `spad` (used in place). Returns (ee, log,
    v0, entries, final delta); entries[k] = (log entry, {line: bytes}
    stored since the previous call, a0..t3 images, f12..f19 bits)."""
    ee = (cls or SR.SysEE)(ELF, b'', b'')
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
                v0, f0 = script.take(fn)
                e.r[2] = v0_image(v0)
                e.f[0] = f0 & MASK
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
    for i, v in enumerate(case['args']):
        ee.r[4 + i] = sx32(v) & MASK64
    for i, v in enumerate(case.get('fargs', ())):
        ee.f[12 + i] = v & MASK
    ee.r[29] = SP
    ee.r[31] = shared.RETURN
    ee.run(case['fn'])
    return ee, log, ee.r[2] & MASK, entries, ee.take()


def fx_plan(case, before=False):
    """{stub index k: [(address, size, value, load pc)]}: the loads made
    after the k-th stub (or, before=True, since the previous stub), and the
    alternate handler pointers."""
    ram, spad = prepared(case)

    def count(e, k):
        e.window = k + 1
    ee, *_ = oracle_run(case, ram, spad, cls=SR.RecEE, on_stub=count)
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
    """Returns (branch outcomes, calls, fx writes, SysEE.stores: {store pc:
    {address: (size, bytes before, bytes after)}}). Raises on any
    difference."""
    fx = None
    if case.get('fx'):
        plan, alts = fx_plan(case, case.get('fx_before', False))
        case = dict(case, indirect=list(case.get('indirect', ())) + alts)
        fx = SR.Perturb(plan, case['indirect'], None, False, case.get('fx_before', False))
    ram, spad = prepared(case)
    nat = NativeRun(ram, spad, Script(case['script']), set(case.get('indirect', ())))
    if AV.ENABLED:
        nat.canonical_view = AV.CanonicalView(nat.regions)
        nat.canonical_view.install(nat.ctx)
    native_lines()
    where = (case['name'],)
    try:
        ee, olog, ov0, entries, last = oracle_run(case, ram, spad, on_stub=fx)
    except AssertionError as e:
        if case.get('fx') and e.args and isinstance(e.args[0], tuple) and e.args[0][:1] == ('address',):
            return set(), 0, -1, {}     # the side effect left the mapped memory: not comparable
        raise AssertionError((where, 'the original run stopped', e.args)) from e
    if fx is not None:
        nat.fx = fx.log
    nat.lock = SR.Lockstep(entries, last, ee.pre, ee.mem, ee.spad)
    rc, nv0 = native_call(case['fn'], C.byref(nat.ctx), case['args'], case.get('fargs', ()))
    if nat.error is not None:
        raise AssertionError((where, 'native worker error', repr(nat.error)))
    if rc == -1 and case.get('fx') and nat.ctx.fault == 3 and 0x2000000 <= nat.ctx.fault_address < 0x40000000:
        return set(), 0, -1, {}         # an address the interpreter folds onto RAM: not comparable
    assert rc == 0, (where, 'native faulted', rc, nat.ctx.fault, hex(nat.ctx.fault_function),
                     hex(nat.ctx.fault_address))
    assert len(nat.log) == len(entries), (where, 'native makes fewer calls than the original', len(nat.log),
                                          len(entries), entries[len(nat.log)][0] if len(nat.log) < len(entries) else ())
    try:
        nat.lock.check(('after the last store',), last, nat.dirty(), nat.ee.mem, nat.ee.spad)
    except AssertionError as e:
        raise AssertionError((where,) + e.args) from e
    if case['fn'] in RETURNS:
        assert nv0 == ov0, (where, 'result', hex(nv0), hex(ov0))
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


W8, W16, W32, WF, FB = SR.W8, SR.W16, SR.W32, SR.WF, SR.FB
LCG_A, LCG_C = SR.LCG_A, SR.LCG_C


def case(name, fn, args, script=None, writes=(), fargs=(), indirect=(), beat=BEAT):
    return dict(name=name, fn=fn, beat=beat, args=list(args), script=script or {}, writes=list(writes),
                fargs=list(fargs), indirect=list(indirect))


def seeds(rng, wants):
    """Writes making the original LCG (00122BB8) produce draws satisfying
    wants[i] (None = any) on its next calls (lane SYS seed_writes)."""
    return SR.seed_writes(image()[0], rng, wants)


def owners(fn):
    ram, _ = image()
    return [a for a, cb in SR.pool(ram) if cb == fn]


NODES_A5 = ()           # the captured 0012A5D0 records (main() fills)
NODE_DC = 0             # the captured 0022DCD0 record


def cases_small(rng):
    out = []
    ram, _ = image()
    # 00113478: the gate, the request result's sign (all 64 bits), the word read back
    for g in (0, 1, -1, 1 << 32):
        for q in (0, 5, -1, -(1 << 40), 0x80000000):
            out.append(case(f't 113478 gate {g:#x} rpc {q:#x}', 0x113478, [],
                            script={0x112E28: [g], 0x10E8A8: [q]},
                            writes=[W32(0x27AB40, 0x12345678 ^ (q & 0xFFFF)), W32(0x241D0C, 7)]))
    # 001195A8: the table word, the 48-record scan, the unsigned bound
    for a0 in (0, 3, 4, 6, 7, 0x7F, 0x80, 0xFFFFFFFF, 0x80000000):
        out.append(case(f't 1195A8 {a0:#x}', 0x1195A8, [a0]))
    for k, tag in ((0, 'first'), (47, 'last'), (20, 'mid')):
        rec = 0x27CCC0 + k * 0x6A
        out.append(case(f't 1195A8 named {tag}', 0x1195A8, [5], writes=[W16(rec, 1), W16(rec + 0x22, 5)]))
        out.append(case(f't 1195A8 named off {tag}', 0x1195A8, [5], writes=[W16(rec, 2), W16(rec + 0x22, 5)]))
    out.append(case('t 1195A8 past the scan', 0x1195A8, [5], writes=[W16(0x27CCC0 + 48 * 0x6A, 1),
                                                                       W16(0x27CCC0 + 48 * 0x6A + 0x22, 5)]))
    out.append(case('t 1195A8 table 2', 0x1195A8, [2], writes=[W32(0x27C6C0 + 24, 2)]))
    # the +0x22 halfword with a non-zero high byte (0x0105 against a0 5): a
    # byte-wide compare would match it
    rec = 0x27CCC0 + 20 * 0x6A
    out.append(case('t 1195A8 named halfword 0x105', 0x1195A8, [5],
                    writes=[W32(0x27C6C0 + 5 * 12, 1), W16(rec, 1), W16(rec + 0x22, 0x105)]))
    out.append(case('t 1195A8 table -1', 0x1195A8, [0x7F], writes=[W32(0x27C6C0 + 0x7F * 12, 1)]))
    # 00128390
    for d in (0, 1, 0xFF):
        for a1 in (0, 1, -1, 0x80000000):
            out.append(case(f't 128390 {d} {a1:#x}', 0x128390, [0x7AD490, a1], writes=[W8(0x81070A, d)]))
    # 00128600: rows and the LCG low bits
    for a0, lo in ((0, 0), (4, 15), (1, 7), (-1, 3), (15, 9)):
        out.append(case(f't 128600 {a0} {lo}', 0x128600, [a0], writes=seeds(rng, [lambda v, lo=lo: v & 15 == lo])))
    return out


def dist_writes(p, d):
    """Place record p at distance d (x only) from the point D_00810350."""
    ram, _ = image()
    x, y, z = struct.unpack_from('<3f', ram, 0x810350)
    return [WF(p + 0xB0, x + d), WF(p + 0xB4, y), WF(p + 0xB8, z)]


def row_draw(row, value):
    """An LCG want: the 00128600 draw picks `value` from row `row`."""
    ram, _ = image()
    cells = [i for i in range(16) if ram[0x242ED0 + row * 16 + i] == value]
    if not cells:
        return None
    return lambda v: (v & 15) in cells


def cases_128640(rng):
    out = []
    p = NODES_A5[0]
    for d in (10.0, 30.0, 30.1, 30.2, 60.0, 60.1, 65.0, 70.0, 70.001, 200.0, -20.0):
        for row, val in ((4, 0), (4, 1), (4, 2), (0, 0), (0, 1), (0, 2)):
            want = row_draw(row, val)
            if want is None:
                continue
            if (d < 30.1 and row != 4) or (d >= 30.1 and row != 0):
                continue
            out.append(case(f't 128640 d{d} row{row} v{val}', 0x128640, [p],
                            writes=dist_writes(p, d) + seeds(rng, [want])))
        if d >= 60.1:
            out.append(case(f't 128640 d{d}', 0x128640, [p], writes=dist_writes(p, d)))
    # the three thresholds (70.0, 30.1, 60.1) and their neighbouring floats:
    # the point D_00810350 at the origin, the record at (d, 0, 0)
    for bound in (0x428C0000, 0x41F0CCCD, 0x42706666):
        for k in (-1, 0, 1, 2, 4, 5):
            d = struct.unpack('<f', struct.pack('<I', bound + k))[0]
            out.append(case(f't 128640 bound {bound + k:#x}', 0x128640, [p],
                            writes=[WF(0x810350, 0.0), WF(0x810354, 0.0), WF(0x810358, 0.0),
                                    WF(p + 0xB0, d), WF(p + 0xB4, 0.0), WF(p + 0xB8, 0.0)]))
    out.append(case('t 128640 spr gate', 0x128640, [p], writes=[W8(0x70003B8D, 1)] + dist_writes(p, 5.0)))
    out.append(case('t 128640 halfword gate', 0x128640, [p], writes=[W16(0x28A9A0, 0x8000)] + dist_writes(p, 5.0)))
    out.append(case('t 128640 word gate', 0x128640, [p], writes=[W32(0x8104E0, 0x28)] + dist_writes(p, 5.0)))
    out.append(case('t 128640 word 0x29', 0x128640, [p], writes=[W32(0x8104E0, 0x29)] + dist_writes(p, 5.0)))
    out.append(case('cap 128640', 0x128640, [p]))
    return out


def cases_init(rng):
    out = []
    for p in NODES_A5:
        b = p + 0x1F0
        for e1 in (0, 1, 0x80):
            for d in (0, 1):
                out.append(case(f't 1289C0 {p:x} e1 {e1} d {d}', 0x1289C0, [p, b],
                                writes=[W8(b + 0xE1, e1), W8(0x81070A, d)]))
        for g in (0xFF, 0, 0xFE):
            for gate in (0, 1, -1):
                for bit in (0x83, 0x03, 0x80):
                    out.append(case(f't 128AB0 {p:x} g{g:x} gate{gate} d{bit:x}', 0x128AB0, [p, b],
                                    script={0x1B10B0: [gate]}, writes=[W8(0x810788, g), W8(p + 0xD, bit)]))
    # 001B10B0's result non-zero in the upper half only (the original tests
    # all 64 bits)
    p = NODES_A5[0]
    for g in (0xFF, 0):
        out.append(case(f't 128AB0 {p:x} g{g:x} gate upper d83', 0x128AB0, [p, p + 0x1F0],
                        script={0x1B10B0: [1 << 32]}, writes=[W8(0x810788, g), W8(p + 0xD, 0x83)]))
    return out


def cases_129780(rng):
    out = []
    p = NODES_A5[0]
    b = p + 0x1F0
    ram, _ = image()
    cam = u32(ram, 0x7FFFFFFF & 0) or 0
    for sel in list(range(15)) + [0x10D, 0xFF]:
        base = case(f't 129780 sel {sel:#x}', 0x129780, [p, b, sel])
        out.append(base)
    for sel in (0, 5):
        out.append(case(f't 129780 sel {sel} hit', 0x129780, [p, b, sel], script={0x1C2430: [1]}))
        out.append(case(f't 129780 sel {sel} hit64', 0x129780, [p, b, sel], script={0x1C2430: [1 << 32]}))
    for sel in (2, 7):
        out.append(case(f't 129780 sel {sel} first', 0x129780, [p, b, sel], script={0x19AD00: [1]}))
        out.append(case(f't 129780 sel {sel} second', 0x129780, [p, b, sel], script={0x19AD00: [0, -1]}))
    # results non-zero in the upper half only (the original tests all 64 bits)
    out.append(case('t 129780 sel 2 first64', 0x129780, [p, b, 2], script={0x19AD00: [1 << 32]}))
    out.append(case('t 129780 sel 2 second64', 0x129780, [p, b, 2], script={0x19AD00: [0, 1 << 32]}))
    for k in (0, 5):
        out.append(case(f't 129780 sel 3 probe {k} 64', 0x129780, [p, b, 3], script={0x1C2540: [0] * k + [1 << 32]},
                        writes=[W16(0x1517D2C + 0x1A, 0x0800)]))
    for sel in (3, 9):
        for k in range(6):
            for flags in (0x3800, 0x0800, 0x8000, 0x8800, 0x0001, 0):
                if k not in (0, 5) and flags not in (0x0800, 0x8000, 0):
                    continue
                out.append(case(f't 129780 sel {sel} probe {k} flags {flags:#x}', 0x129780, [p, b, sel],
                                script={0x1C2540: [0] * k + [1]},
                                writes=[W16(0x1517D2C + 0x1A, flags)]))
    for t in (0x27, 0x28, 0x7FFF, -5):
        for sel in (0, 3):
            out.append(case(f't 129780 timer {t} sel {sel}', 0x129780, [p, b, sel],
                            script={0x1C2430: [1], 0x1C2540: [1]}, writes=[W16(p + 0x28, t)]))
    cw = [WF(0x1517D2C + 0x24, 0.25), WF(0x1517D2C + 0x28, -0.5), WF(0x1517D2C + 0x2C, 0.75),
          W32(0x700031D4, 0x1517D2C + 0x40), WF(0x1517D2C + 0x40 + 0x24, 9.0), WF(0x1517D2C + 0x40 + 0x28, 8.0),
          WF(0x1517D2C + 0x40 + 0x2C, 7.0)]
    for sel in (0, 3):
        out.append(case(f't 129780 camera sel {sel}', 0x129780, [p, b, sel],
                        script={0x1C2430: [1], 0x1C2540: [1]}, writes=cw + [W16(p + 0x28, 0)]))
    out.append(case('t 129780 sel 0 hit state 0xFF', 0x129780, [p, b, 0], script={0x1C2430: [1]},
                    writes=[W8(p + 4, 0xFF)]))
    del cam
    return out


def cases_12ADC0(rng):
    out = []
    p = NODES_A5[0]
    ram, _ = image()
    for f in (120.0, -30.0, 0.0, -0.0, 1e-39, 3.0e38):
        for d in (5.0, 100.0, 200.0):
            w = dist_writes(p, d)
            out.append(case(f't 12ADC0 f{f} d{d}', 0x12ADC0, [p, p + 0xB0, 0x810350], fargs=[FB(f)], writes=w))
    out.append(case('t 12ADC0 sub50', 0x12ADC0, [p, p + 0xB0, p + 0x1F0 + 0x50], fargs=[FB(-30.0)]))
    # distinct, non-zero orientation vectors at s +0x70 / +0x80 (their cross
    # product and every scratch word it feeds then differ)
    s = p + 0x1F0
    for k in range(3):
        v = [rng.uniform(-1, 1) for _ in range(6)]
        w = [WF(s + 0x70 + 4 * i, v[i]) for i in range(3)] + [WF(s + 0x80 + 4 * i, v[3 + i]) for i in range(3)]
        out.append(case(f't 12ADC0 vectors {k}', 0x12ADC0, [p, p + 0xB0, 0x810350], fargs=[FB(120.0)],
                        writes=w + dist_writes(p, 5.0)))
    return out


def cases_12AFC0(rng):
    out = []
    for p in NODES_A5:
        b = p + 0x1F0
        out.append(case(f'cap 12AFC0 {p:x}', 0x12AFC0, [p, b], script={0x1C2770: [0]}))
        for st in range(8):
            for mode in (0, 8, 1, 1 << 33):
                out.append(case(f't 12AFC0 {p:x} st {st} mode {mode:#x}', 0x12AFC0, [p, b],
                                script={0x1C2770: [mode]}, writes=[W8(p + 6, st)]))
    p = NODES_A5[0]
    b = p + 0x1F0
    # state 0: both LCG parities, both 00128600 outcomes
    for par in (0, 1):
        for hit in (0, 1):
            want = row_draw(0, 1 if hit else 0)
            out.append(case(f't 12AFC0 st0 par {par} hit {hit}', 0x12AFC0, [p, b], script={0x1C2770: [0]},
                            writes=[W8(p + 6, 0)] + seeds(rng, [lambda v, par=par: v & 1 == par, want, None])))
    # states 1 / 4: the turn reaches the target (equal yaw) or not
    for st in (1, 4):
        out.append(case(f't 12AFC0 st{st} reach', 0x12AFC0, [p, b], script={0x1C2770: [0]},
                        writes=[W8(p + 6, st), WF(b + 0xE8, 1.0), WF(p + 0xC4, 0.99)] + dist_writes(p, 500.0)))
        out.append(case(f't 12AFC0 st{st} far', 0x12AFC0, [p, b], script={0x1C2770: [0]},
                        writes=[W8(p + 6, st), WF(b + 0xE8, 1.0), WF(p + 0xC4, -2.0)] + dist_writes(p, 500.0)))
    # states 2..5: the dwell step near / far, its counter below / at / above 0x5A
    for st in (2, 3, 4, 5):
        for d in (5.0, 500.0):
            for t in (0, 0x59, 0x5A, 0x7FFF):
                for dc in (1, 5):
                    out.append(case(f't 12AFC0 st{st} d{d} t{t:#x} dc{dc}', 0x12AFC0, [p, b],
                                    script={0x1C2770: [0]},
                                    writes=[W8(p + 6, st), W16(p + 0x28, t), W16(b + 0xD0, dc),
                                            WF(b + 0xE8, 1.0), WF(p + 0xC4, 0.99)] + dist_writes(p, d)))
    # state 2: 001C2770's result 8 in the low half with an upper bit set
    # (the original's mode != 8 test uses all 64 bits)
    out.append(case('t 12AFC0 st2 mode 0x100000008', 0x12AFC0, [p, b], script={0x1C2770: [(1 << 32) | 8]},
                    writes=[W8(p + 6, 2)] + dist_writes(p, 500.0)))
    # state 2: the second probe (sub +0x50 at -30.0) inside / outside
    for inside in (0, 1):
        pt = [struct.unpack('<f', v)[0] for _, v in dist_writes(p, 500.0)]
        off = 10.0 if inside else 100.0
        for dc in (1, 3):
            out.append(case(f't 12AFC0 st2 back {inside} dc{dc}', 0x12AFC0, [p, b], script={0x1C2770: [1]},
                            writes=[W8(p + 6, 2), WF(b + 0x50, pt[0] + off), WF(b + 0x54, pt[1]),
                                    WF(b + 0x58, pt[2]), W16(b + 0xD0, dc)] + dist_writes(p, 500.0)))
    return out


def a5(name, p, writes=(), script=None, **kw):
    return case(name, 0x12A5D0, [p], script=script, writes=writes, indirect=[u32(image()[0], p + 0x4C)], **kw)


def cases_12A5D0(rng):
    out = []
    for p in NODES_A5:
        out.append(a5(f'cap 12A5D0 {p:x}', p, script={0x1B2140: [1]}))
    p = NODES_A5[0]
    b = p + 0x1F0
    base = {0x1B2140: [1]}
    # state 0
    for sub in (0, 1, 2):
        for g in (0, 1):
            for kind in (3, 4, 9):
                out.append(a5(f't 12A5D0 st0 sub{sub} g{g} k{kind}', p,
                              script={0x1B10B0: [g], 0x1C2430: [1 - g], 0x1C2540: [1 - g]},
                              writes=[W8(p + 4, 0), W8(p + 5, sub), W8(p + 0xD, kind), W8(0x810788, 0)]))
    # state 1 gates
    for gate in (0, 1, 1 << 32):
        out.append(a5(f't 12A5D0 st1 2140 {gate:#x}', p, script={0x1B2140: [gate]}))
    for m, f6 in ((1, 0), (2, 0), (2, 1), (0xFF, 0)):
        out.append(a5(f't 12A5D0 st1 spr {m} f6 {f6}', p, script=base,
                      writes=[W8(0x70003B8D, m), W16(b + 0xF6, f6)]))
    # phases 0, 5, 0x20 and 0x40 (the 64-frame mask: 0x20 skips the poll,
    # 0x40 polls; a narrower or wider mask flips one of them)
    for poll, v in ((0, 0), (0, 1), (5, 1), (0x20, 1), (0x40, 1)):
        ram, spad = image()
        cnt = struct.unpack_from('<I', spad, 0x3B68)[0]
        out.append(a5(f't 12A5D0 st1 poll {poll} {v}', p, script={**base, 0x1B0D80: [v]},
                      writes=[W32(0x70003B68, (-cnt + poll) & MASK), W16(0x70003B8A, cnt & 0xFFFF)]))
        out.append(a5(f't 12A5D0 st2 poll {poll} {v}', p, script={0x1B0D80: [v]},
                      writes=[W8(p + 4, 2), W32(0x70003B68, (-cnt + poll) & MASK), W16(0x70003B8A, cnt & 0xFFFF)]))
    out.append(a5('t 12A5D0 st2 blocked', p, writes=[W8(p + 4, 2), W8(0x81078F, 1)]))
    out.append(a5('t 12A5D0 st1 80B80', p, script={**base, 0x128B80: [1]}))
    # results non-zero in the upper half only (the original tests all 64 bits)
    out.append(a5('t 12A5D0 st1 80B80 64', p, script={**base, 0x128B80: [1 << 32]}))
    out.append(a5('t 12A5D0 st1 poll 0 64', p, script={**base, 0x1B0D80: [1 << 32]},
                  writes=[W32(0x70003B68, (-cnt) & MASK), W16(0x70003B8A, cnt & 0xFFFF)]))
    out.append(a5('t 12A5D0 st1 sub1 probe 64', p, script={**base, 0x1C25E0: [1 << 32], 0x1C2770: [0]},
                  writes=[W8(p + 5, 1), WF(b + 0xD8, -0.0)]))
    # every movement sub-state, the probe after 1 / 2 / 13, the tail
    for sub in range(16):
        for d8 in (0.0, 0.2):
            for flag1 in (0, 1):
                sc = {**base, 0x1C2770: [0], 0x1C25E0: [0]}
                w = [W8(p + 5, sub), WF(b + 0xD8, d8), W8(p + 1, flag1), W8(p + 0xA, 1)]
                out.append(a5(f't 12A5D0 st1 sub{sub} d8 {d8} p1 {flag1}', p, script=sc, writes=w))
    # sub-states 14 / 15 write no +0xFB: a wider store of +0xFA would clear it
    for sub in (14, 15):
        out.append(a5(f't 12A5D0 st1 sub{sub} fb kept', p, script=base,
                      writes=[W8(p + 5, sub), W8(b + 0xFA, 0x33), W8(b + 0xFB, 0x5A)]))
    for sub in (1, 2, 13):
        out.append(a5(f't 12A5D0 st1 sub{sub} probe hit', p, script={**base, 0x1C25E0: [1], 0x1C2770: [0]},
                      writes=[W8(p + 5, sub), WF(b + 0xD8, -0.0)]))
    out.append(a5('t 12A5D0 st1 sub2 kind2', p, script=base, writes=[W8(p + 5, 2), W8(p, 2), WF(b + 0xD8, 0.0)]))
    for v in (0, 1, -1, 1 << 32):
        out.append(a5(f't 12A5D0 st1 sub8 {v}', p, script={**base, 0x1C2770: [v]}, writes=[W8(p + 5, 8)]))
    out.append(a5('t 12A5D0 st1 sub0 near', p, script=base,
                  writes=[W8(p + 5, 0)] + dist_writes(p, 5.0) + seeds(rng, [None])))
    # sub-state 3 with byte +1 set (sub-state 3 stores +0xFA = 1 before the
    # tail's signed +0xFA test, so +0xFA is positive here)
    out.append(a5('t 12A5D0 st1 sub3 p1', p, script={**base, 0x1C2770: [0]},
                  writes=[W8(p + 5, 3), W8(p + 1, 1)]))
    # a negative +0xFA at the tail: 00128B80 non-zero keeps +0xFA as it was
    # in memory (0x80, 0x01 and 0)
    for fa in (0x80, 0x01, 0):
        out.append(a5(f't 12A5D0 st1 fa {fa:#x}', p, script={**base, 0x128B80: [1]},
                      writes=[W8(p + 1, 1), W8(b + 0xFA, fa)]))
    out.append(a5('t 12A5D0 st1 sub7 p1', p, script=base, writes=[W8(p + 5, 7), W8(p + 1, 1)]))
    # state 3
    for kind in (0xA, 0xB, 0xC, 0xD, 9):
        for e0 in (0, 1):
            out.append(a5(f't 12A5D0 st3 k{kind:x} e0 {e0}', p, writes=[W8(p + 4, 3), W8(p + 0xD, kind),
                                                                     W8(b + 0xE0, e0), W8(p + 0x9A, 0x85)]))
    # state 4
    for sub in range(5):
        for g in (0, 1):
            for kind in (3, 12):
                out.append(a5(f't 12A5D0 st4 sub{sub} g{g} k{kind}', p, script={0x1C2430: [g], 0x1C2540: [g]},
                              writes=[W8(p + 4, 4), W8(p + 5, sub), W8(p + 0xD, kind), W16(p + 0x28, 2)]))
    for q in range(4):
        out.append(a5(f't 12A5D0 st4 sub1 draw {q}', p, script={0x1C2430: [1]},
                      writes=[W8(p + 4, 4), W8(p + 5, 1), W8(p + 0xD, 0)] +
                      seeds(rng, [lambda v, q=q: ((v >> 16) * 4) >> 15 == q])))
    for t in (1, 2, 0, 0x8000):
        out.append(a5(f't 12A5D0 st4 sub2 t{t:#x}', p, writes=[W8(p + 4, 4), W8(p + 5, 2), W16(p + 0x28, t)]))
    for d in (5.0, 200.0):
        for v in (0, 1, 0x100, 0x1FF):
            out.append(a5(f't 12A5D0 st4 sub3 d{d} v{v:#x}', p, script={0x1B1630: [v]},
                          writes=[W8(p + 4, 4), W8(p + 5, 3)] + dist_writes(p, d)))
    out.append(a5('t 12A5D0 st5', p, writes=[W8(p + 4, 5)]))
    return out


def dc(name, writes=(), script=None):
    return case(name, 0x22DCD0, [NODE_DC], script=script, writes=writes)


AREA_KEYS = (0x0, 0x1, 0x2, 0x200, 0x202, 0x300, 0x301, 0x600, 0x601, 0x703, 0x800, 0x803, 0xD00, 0x1001,
             0x1300, 0x3, 0x1302)


def cases_22DCD0(rng):
    out = []
    p = NODE_DC
    c = p + 0x1F0
    out.append(dc('cap 22DCD0'))
    for key in AREA_KEYS:
        w = [W8(p + 4, 0), W8(0x810700, key >> 8), W8(0x810701, key & 0xFF), W8(0x810702, 4)]
        out.append(dc(f't 22DCD0 st0 key {key:#x}', writes=w))
    # the per-entry count classes through 001281C0 of R[i]
    for n, rv in ((8, 8.0), (10, 10.5), (14, 14.0), (17, 17.9), (28, 28.0), (35, 35.2), (0, 3.0), (-1, -1.5)):
        w = [WF(0x823270 + 4 * i, rv) for i in range(6)]
        out.append(dc(f't 22DCD0 st1 n{n}', writes=w))
    # key 0x1300: D_008101E4 / D_0081024E, D_008105D0 against 770 and m +0x30
    for e4, h in ((3, 8), (3, 7), (2, 0)):
        for d in (700.0, 770.0, 800.0):
            w = [W8(0x810700, 0x13), W8(0x810701, 0), W8(0x8101E4, e4), W16(0x81024E, h), WF(0x8105D0, d)]
            w += [WF(0x822CF0 + 64 * i + 0x30, (760.0, 770.0, 780.0, -5.0, 900.0, 771.0)[i]) for i in range(6)]
            out.append(dc(f't 22DCD0 st1 1300 e4 {e4} h {h} d {d}', writes=w))
    for sub in (4, 6, 5, 0):
        out.append(dc(f't 22DCD0 st1 D00 {sub}', writes=[W8(0x810700, 0xD), W8(0x810701, 0), W8(0x810702, sub)]))
    # T wrap above 2.0, Q against 4.0, seeds
    w = [WF(0x8231F0 + 4 * i, (1.995, 2.0, 1.0, 1.999, 2.5, -1.0)[i]) for i in range(6)]
    w += [WF(0x823230 + 4 * i, (3.99, 4.0, 5.0, -1.0, 4.01, 0.0)[i]) for i in range(6)]
    w += [WF(0x823270 + 4 * i, 14.0) for i in range(6)] + [W32(c + 4, 0x8000FFFF)]
    out.append(dc('t 22DCD0 st1 wrap', writes=w))
    # T + PH exactly 2.0 (kept: the wrap needs T + PH above 2.0), and just above
    for t, ph in ((1.5, 0.5), (1.5, 0.5000001)):
        out.append(dc(f't 22DCD0 st1 wrap T{t} PH{ph}', writes=[WF(0x8231F0 + 4 * i, t) for i in range(6)] +
                      [WF(0x823330 + 4 * i, ph) for i in range(6)]))
    for n in (0, 1, -1):
        out.append(dc(f't 22DCD0 st1 count {n}', writes=[W32(c, n & MASK)]))
    for st in (2, 3, 4):
        out.append(dc(f't 22DCD0 st{st}', writes=[W8(p + 4, st)]))
    out.append(dc('t 22DCD0 st1 h', script={0x1CCF70: [-7, 1 << 33, 0x80000000]},
                  writes=[WF(0x823270 + 4 * i, 8.0) for i in range(6)]))
    return out


def cases_random(rng, n):
    """Full mode: random perturbations of the owner records and the globals
    they read."""
    out = []
    p = NODES_A5[0]
    b = p + 0x1F0
    for i in range(n):
        w = [W8(p + 4, rng.choice((0, 1, 1, 2, 3, 4))), W8(p + 5, rng.randrange(16)), W8(p + 6, rng.randrange(7)),
             W8(p + 0xD, rng.choice((0, 3, 4, 9, 10, 11, 12, 0x83))), W8(p + 0xA, rng.randrange(4)),
             W8(p + 1, rng.randrange(2)), WF(b + 0xD8, rng.choice((0.0, 0.2, -0.0))),
             W16(p + 0x28, rng.choice((0, 1, 0x28, 0x5A, 0x5B, -1))), W16(b + 0xD0, rng.choice((0, 1, 2, 0x78))),
             W8(0x70003B8D, rng.choice((0, 0, 1, 2))), W8(0x810788, rng.choice((0, 0xFF)))]
        w += dist_writes(p, rng.choice((5.0, 25.0, 45.0, 65.0, 75.0, 150.0)))
        w += seeds(rng, [None])
        sc = {0x1B2140: [rng.choice((0, 1, 1))], 0x128B80: [rng.choice((0, 0, 1))],
              0x1C2770: [rng.choice((0, 8, 1))] * 2, 0x1C25E0: [rng.randrange(2)], 0x1B10B0: [rng.randrange(2)],
              0x1C2430: [rng.randrange(2)], 0x1C2540: [rng.randrange(2) for _ in range(7)],
              0x19AD00: [rng.randrange(2), rng.randrange(2)], 0x1B1630: [rng.randrange(3)],
              0x1B0D80: [rng.randrange(2)]}
        out.append(a5(f'r 12A5D0 {i}', p, writes=w, script=sc))
    for i in range(n // 2):
        key = rng.choice(AREA_KEYS)
        w = [W8(NODE_DC + 4, rng.choice((0, 1, 1))), W8(0x810700, key >> 8), W8(0x810701, key & 0xFF),
             W8(0x810702, rng.choice((4, 6, 1))), W8(0x8101E4, rng.choice((3, 1))), W16(0x81024E, rng.choice((8, 1))),
             WF(0x8105D0, rng.uniform(700, 840)), W32(NODE_DC + 0x1F0 + 4, rng.getrandbits(32))]
        w += [WF(0x823270 + 4 * j, rng.choice((0.0, 8.0, 10.0, 14.0, 17.0, 28.0, 35.0))) for j in range(6)]
        w += [WF(0x823230 + 4 * j, rng.uniform(0, 8)) for j in range(6)]
        out.append(dc(f'r 22DCD0 {i}', writes=w))
    return out


def all_cases(rng):
    cs = (cases_small(rng) + cases_128640(rng) + cases_init(rng) + cases_129780(rng) + cases_12ADC0(rng) +
          cases_12AFC0(rng) + cases_12A5D0(rng) + cases_22DCD0(rng))
    if RM.FULL:
        cs += cases_random(rng, 120)
    return cs


def store_variants(stored):
    """Store-site variants (lane SYS round 4). `stored` lists (case,
    SysEE.stores) for the ordinary cases that ran. A store instruction of
    the original (a 'site') is covered when, in some case, every byte it
    stores differs from the byte that was there before. For every site no
    case covers, the first case that reaches it gets a variant in which
    every byte that site stores is set beforehand to the complement of the
    byte it stores: a translation that drops the store, stores fewer bytes
    or stores to a neighbouring field leaves the complement, which the
    lockstep compares at the next call. Byte and halfword sites also get a
    variant (covered or not) in which the bytes after the field (as many as
    it has) are 0xA5: a translation that stores a wider field overwrites
    them. The two are kept apart (a poisoned field can turn the path away
    from the store)."""
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


QUICK_CASES = 170
# Mutant killers (docs/AREA01_EXITA.md section 5): cases that alone killed a
# mutant of the bounded sweep, plus every movement sub-state of 0012A5D0 and
# every selector of 00129780 (their jump-table arms have no conditional
# branch of their own, so the cover below need not reach them).
QUICK_KILLERS = {
    't 12ADC0 vectors 0', 't 12AFC0 st0 par 1 hit 0', 't 128640 bound 0x428c0001', 't 1195A8 past the scan',
    't 129780 sel 3 probe 0 flags 0x1', 't 129780 camera sel 0', 't 128AB0 7ad490 gff gate0 d3',
    't 12A5D0 st1 sub14 fb kept',
    # review round 2: the killers of the reviewer's named survivors (B2, G2,
    # J6, K2, K5, K6, L3) and of the twelve mutants the default run missed
    # but the full run killed (A1, A2, A4, B3, F2, H3, J1, J2, K4, K8, L4, L8)
    't 1195A8 named halfword 0x105', 't 128AB0 7ad490 gff gate upper d83', 't 128AB0 7ad490 g0 gate upper d83',
    't 129780 sel 2 first64', 't 129780 sel 2 second64', 't 129780 sel 3 probe 0 64', 't 129780 sel 3 probe 5 64',
    't 12A5D0 st1 80B80 64', 't 12A5D0 st1 poll 0 64', 't 12A5D0 st1 sub1 probe 64', 't 12AFC0 st2 mode 0x100000008',
    't 12A5D0 st1 poll 32 1', 't 12A5D0 st1 poll 64 1', 't 12A5D0 st1 fa 0x80', 't 12A5D0 st1 sub8 4294967296',
    't 22DCD0 st1 wrap T1.5 PH0.5',
    't 113478 gate 0x1 rpc -0x10000000000', 't 113478 gate 0x100000000 rpc 0x5', 't 1195A8 named last',
    't 1289C0 7ad490 e1 128 d 0', 't 129780 timer 39 sel 3', 't 12AFC0 7ad490 st 0 mode 0x200000000',
    't 12AFC0 st2 d5.0 t0x59 dc5', 't 12A5D0 st4 sub1 draw 1', 't 12A5D0 st4 sub3 d200.0 v0x100',
    't 22DCD0 st1 1300 e4 2 h 0 d 770.0', 't 22DCD0 st1 h',
    # and two the reviewer's default run killed only through the sampled
    # filler (H7: 001C2430's result tested in 32 bits; I2: 0012ADC0's f12
    # test with c.lt): pinned so a change to the pinned set cannot drop them
    't 129780 sel 0 hit64', 't 12ADC0 f1e-39 d100.0',
} | {f't 12A5D0 st1 sub{k} d8 0.0 p1 0' for k in range(16)} | {f't 129780 sel {k:#x}' for k in range(15)}
# fx variants the default run always keeps (their plain case is pinned above)
QUICK_FX = {'fx t 113478 gate 0x1 rpc 0x0', 'fx t 12AFC0 7ad490 st 0 mode 0x200000000'}
QUICK_PINNED = QUICK_KILLERS | {
    # a greedy cover of every branch outcome the full case set takes
    # (build/area01/exita/cover.py computed it once; it is data, not a
    # claim: the default run prints its coverage)
    't 22DCD0 st0 key 0x0',
    't 12A5D0 st1 sub1 d8 0.0 p1 1',
    't 12A5D0 st4 sub1 g0 k3',
    't 12A5D0 st0 sub0 g0 k3',
    't 22DCD0 st0 key 0x1300',
    't 12A5D0 st0 sub1 g0 k3',
    't 1195A8 0x3',
    't 12AFC0 7add60 st 4 mode 0x8',
    't 12A5D0 st1 spr 2 f6 1',
    't 128640 d30.1 row0 v1',
    't 128AB0 7ad490 gff gate0 d83',
    't 12AFC0 7add60 st 3 mode 0x1',
    't 12AFC0 st2 d5.0 t0x0 dc1',
    't 12AFC0 st5 d5.0 t0x5a dc1',
    't 12A5D0 st2 poll 0 0',
    't 12A5D0 st3 kd e0 0',
    't 22DCD0 st1 1300 e4 2 h 0 d 800.0',
    't 12A5D0 st1 sub2 d8 0.0 p1 0',
    't 12A5D0 st1 sub0 near',
    't 12A5D0 st4 sub3 g0 k3',
    't 22DCD0 st0 key 0x300',
    't 22DCD0 st0 key 0xd00',
    't 113478 gate 0x1 rpc 0x0',
    't 129780 sel 0x2',
    't 129780 sel 3 probe 1 flags 0x800',
    't 129780 sel 3 probe 2 flags 0x0',
    't 129780 timer 40 sel 0',
    't 12AFC0 7ad490 st 0 mode 0x0',
    't 12AFC0 7ad490 st 5 mode 0x0',
    't 12AFC0 st0 par 0 hit 0',
    't 12AFC0 st4 far',
    't 12AFC0 st3 d500.0 t0x0 dc1',
    't 12AFC0 st5 d5.0 t0x0 dc1',
    't 12A5D0 st0 sub0 g1 k3',
    't 12A5D0 st1 poll 0 0',
    't 12A5D0 st2 blocked',
    't 12A5D0 st1 sub13 d8 0.0 p1 0',
    't 12A5D0 st4 sub0 g0 k3',
    't 12A5D0 st4 sub2 g0 k3',
    't 22DCD0 st0 key 0x600',
    't 22DCD0 st0 key 0x1001',
    't 22DCD0 st1 1300 e4 3 h 8 d 700.0',
    't 22DCD0 st1 D00 6',
    't 113478 gate 0x0 rpc 0x0',
    't 113478 gate 0x1 rpc -0x1',
    't 1195A8 0x0',
    't 1195A8 0x7',
    't 1195A8 0x80',
    't 128390 1 0x0',
    't 128640 d10.0 row4 v2',
    't 128640 d30.1 row0 v0',
    't 128640 d30.1 row0 v2',
    't 128640 d60.1 row0 v0',
    't 128640 spr gate',
    't 128640 halfword gate',
    't 128640 word gate',
    't 128AB0 7ad490 gff gate1 d83',
    't 129780 sel 0x0',
    't 129780 sel 0xd',
    't 129780 sel 2 first',
    't 129780 sel 2 second',
    't 129780 sel 3 probe 3 flags 0x800',
    't 129780 sel 3 probe 4 flags 0x800',
    't 129780 sel 3 probe 5 flags 0x3800',
    't 12AFC0 7ad490 st 1 mode 0x0',
    't 12AFC0 7ad490 st 2 mode 0x8',
    't 12AFC0 7ad490 st 6 mode 0x0',
    't 12AFC0 st1 far',
    't 12AFC0 st2 d5.0 t0x5a dc1',
    't 12AFC0 st3 d5.0 t0x5a dc1',
    't 12AFC0 st4 d5.0 t0x5a dc1',
    't 12AFC0 st2 back 1 dc1',
    't 12A5D0 st0 sub1 g0 k4',
    't 12A5D0 st0 sub1 g0 k9',
    't 12A5D0 st0 sub1 g1 k3',
    't 12A5D0 st0 sub2 g0 k3',
    't 12A5D0 st1 2140 0x0',
    't 12A5D0 st1 spr 1 f6 0',
    't 12A5D0 st1 spr 2 f6 0',
    't 12A5D0 st1 poll 0 1',
    't 12A5D0 st2 poll 0 1',
    't 12A5D0 st1 80B80',
    't 12A5D0 st1 sub0 d8 0.0 p1 0',
    't 12A5D0 st1 sub2 d8 0.2 p1 0',
    't 12A5D0 st1 sub3 d8 0.0 p1 1',
    't 12A5D0 st1 sub7 d8 0.0 p1 0',
    't 12A5D0 st1 sub8 d8 0.0 p1 0',
    't 12A5D0 st1 sub13 d8 0.2 p1 0',
    't 12A5D0 st1 sub14 d8 0.0 p1 0',
    't 12A5D0 st1 sub1 probe hit',
    't 12A5D0 st1 sub2 probe hit',
    't 12A5D0 st1 sub13 probe hit',
    't 12A5D0 st1 sub2 kind2',
    't 12A5D0 st1 sub8 1',
    't 12A5D0 st3 ka e0 0',
    't 12A5D0 st3 kb e0 0',
    't 12A5D0 st3 kd e0 1',
    't 12A5D0 st4 sub1 g0 k12',
    't 12A5D0 st4 sub4 g0 k3',
    't 12A5D0 st4 sub2 t0x1',
    't 12A5D0 st4 sub3 d5.0 v0x0',
    't 12A5D0 st4 sub3 d200.0 v0x1',
    't 12A5D0 st5',
    't 22DCD0 st0 key 0x1',
    't 22DCD0 st0 key 0x2',
    't 22DCD0 st0 key 0x200',
    't 22DCD0 st0 key 0x202',
    't 22DCD0 st0 key 0x301',
    't 22DCD0 st0 key 0x601',
    't 22DCD0 st0 key 0x703',
    't 22DCD0 st0 key 0x800',
    't 22DCD0 st0 key 0x803',
    't 22DCD0 st0 key 0x3',
    't 22DCD0 st1 1300 e4 3 h 7 d 700.0',
    't 22DCD0 st1 D00 5',
    't 22DCD0 st1 wrap',
    't 22DCD0 st2',
    't 22DCD0 st3',
    't 22DCD0 st4',
}


FX_FNS = {0x113478, 0x128AB0, 0x129780, 0x12A5D0, 0x12AFC0, 0x22DCD0}


def fx_cases(cases):
    out = []
    for c in cases:
        if c['fn'] not in FX_FNS:
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
    nat = NativeRun(ram, spad, Script({}), set())
    N = NATIVE
    out = I32(0)
    assert N.em_area01_exita_00128390(None, U32(0), I32(0), C.byref(out)) == -1
    n += 1
    ctx = C.byref(nat.ctx)
    assert N.em_area01_exita_00128390(ctx, U32(0), I32(0), None) == -1 and nat.ctx.fault == 1
    assert nat.ctx.fault_function == 0x128390
    n += 1
    assert N.em_area01_exita_00128390(ctx, U32(0), I32(1), C.byref(out)) == -1     # latched
    n += 1
    N.em_area01_exita_clear_fault(ctx)
    N.em_area01_exita_clear_fault(None)
    assert nat.ctx.fault == 0 and nat.ctx.fault_function == 0 and nat.ctx.fault_address == 0
    assert N.em_area01_exita_00128390(ctx, U32(0), I32(1), C.byref(out)) == 0 and out.value in (0x1E, 0x32)
    n += 1
    # an unmapped record: the first access faults with its address
    assert N.em_area01_exita_0012A5D0(ctx, U32(0x3000000)) == -1
    assert nat.ctx.fault == 3 and nat.ctx.fault_address == 0x3000004 and nat.ctx.fault_function == 0x12A5D0
    n += 1
    N.em_area01_exita_clear_fault(ctx)
    # a NULL worker: fault at the first call, naming the callee
    nat.ctx.call = WORKER()
    assert N.em_area01_exita_00128600(ctx, I32(0), C.byref(out)) == -1
    assert nat.ctx.fault == 1 and nat.ctx.fault_address == 0x122BB8
    n += 1
    N.em_area01_exita_clear_fault(ctx)
    # a worker returning < 0
    bad = WORKER(lambda _c, _p: -1)
    nat.ctx.call = bad
    assert N.em_area01_exita_00113478(ctx, C.byref(out)) == -1
    assert nat.ctx.fault == 2 and nat.ctx.fault_address == 0x112E28 and nat.ctx.fault_function == 0x113478
    n += 1
    return n


# ======================================================================
# Main
# ======================================================================

def setup():
    """Load the ELF, check the callee policies, build the native module and
    find the captured records; returns the lane SYS interpreter base."""
    global NATIVE, ELF, NODES_A5, NODE_DC
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
    # every register a callee reads (measured over all its paths) is compared,
    # except the saved registers a stub reads (it saves them) and the VU0
    # state the run-policy leaves read (identical on both sides: nothing
    # else in either run touches VU0)
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
    NODES_A5 = tuple(owners(0x12A5D0))
    NODE_DC = owners(0x22DCD0)[0]
    assert len(NODES_A5) == 2, NODES_A5
    # the entry count of the 0022DCD0 record bounds its per-entry loop: a
    # side effect keeps it small (bit 0 only, 0..8) so an fx case stays a
    # few thousand steps (lane SYS FX_RANGE)
    SR.FX_RANGE[NODE_DC + 0x1F0] = (0, 8, "0022DCD0's entry count")
    return base


def main():
    t0 = time.time()
    if AV.ENABLED:
        print('canonical callback mode: all original-instruction comparisons; region arrays disabled', flush=True)
    setup()
    rng = random.Random(0xE17A)
    cases = all_cases(rng)
    only = os.environ.get('EM_AREA01_EXITA_ONLY')
    if only:
        cases = [c for c in cases if c['name'].startswith(tuple(only.split(',')))]
    total = len(cases)
    names = {c['name'] for c in cases}
    if not only:
        assert QUICK_PINNED <= names, ('pinned cases missing', sorted(QUICK_PINNED - names))
        assert {n[3:] for n in QUICK_FX} <= QUICK_PINNED
    if not (RM.FULL or only):
        # the default run: every captured case, the pinned cases (a branch
        # cover and the mutant killers) and a fixed-seed sample of the rest
        cases = RM.select(cases, QUICK_CASES, 0xE17A,
                          keep=lambda i, c: c['name'].startswith('cap') or c['name'] in QUICK_PINNED)
    fxs = fx_cases(cases)
    if not (RM.FULL or only):
        fxs = RM.select(fxs, 40, 0xE17A, axes=(lambda c: (c['fn'], c.get('fx_before', False)),),
                        keep=lambda i, c: c['name'] in QUICK_FX)
    batch = cases + fxs
    outcomes, calls, fxw, skipped, stored = set(), 0, 0, 0, []
    for c, (o, n, w, st) in zip(batch, RM.parallel_map(run_case, batch)):
        outcomes |= o
        calls += n
        if w == -1:
            skipped += 1
        else:
            fxw += w
        if not c.get('fx'):
            stored.append((c, st))
    sites, nsites, ncovered = store_variants(stored)
    for c, (o, n, w, st) in zip(sites, RM.parallel_map(run_case, sites)):
        outcomes |= o
        calls += n
    api = api_checks()
    per = {}
    for fn, size in FUNCS.items():
        pcs = [pc for pc in SR.BRANCH_PCS if fn <= pc < fn + size]
        both = sum(1 for pc in pcs if (pc, True) in outcomes and (pc, False) in outcomes)
        per[fn] = (both, len(pcs))
    print('branch coverage (both outcomes / conditional branches): ' +
          ', '.join(f'{fn:06X} {per[fn][0]}/{per[fn][1]}' for fn in sorted(FUNCS)))
    missing = sorted((pc, t) for pc in SR.BRANCH_PCS for t in (True, False) if (pc, t) not in outcomes)
    if os.environ.get('EM_AREA01_EXITA_GAPS'):
        for fn in sorted(FUNCS):
            miss = [f'{pc:06X}{"T" if t else "F"}' for pc, t in missing if fn <= pc < fn + FUNCS[fn]]
            if miss:
                print(f'  {fn:06X} missing outcomes:', ' '.join(miss))
    if RM.FULL and not only:
        assert set(missing) == set(UNREACHABLE), ('full-mode branch coverage gaps',
                                                  [f'{pc:06X}{"T" if t else "F"}' for pc, t in missing
                                                   if (pc, t) not in UNREACHABLE])
        print('full-mode branch coverage asserted: every outcome taken')
    RM.banner(RM.part(len(cases), total, 'cases'), f'{len(fxs)} stub side-effect cases ({fxw} scripted writes, '
              f'{skipped} not comparable)',
              f'{len(sites)} store-site variants ({nsites} store sites, {ncovered} covered by the cases)', f'{calls} worker calls compared at entry and after the last store',
              f'{len(CALLEES)} callee policies checked against measured register reads',
              f'{api} fail-stop API checks')
    print(f'test_area01_exita_reference: OK ({time.time() - t0:.1f} s)')


if __name__ == '__main__':
    main()
