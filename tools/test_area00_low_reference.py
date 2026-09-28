#!/usr/bin/env python3
"""Execute the original AREA00 lane-A00LOW routines and compare em_area00_low.c.

docs/AREA00_LOW.md. The user's pinned ELF and the captured AREA00 route RAM
(../Extermination/build/s87/route_a00/<beat>/, end-of-beat images) supply
every instruction and every table; none are embedded here.

The oracle is the lane-SYS recording interpreter SysEE
(tools/test_area01_sys_reference.py, built on FallEE: every COP1 and VU0
macro op through tools/ee_float_model.py) running the ORIGINAL routine over
a copy of the capture. The native module runs over another copy of the same
bytes. Every call that leaves the translated set is caught on both sides
and handled by the same Python policy:
  run   the original callee runs (vector / matrix leaves, the math leaves,
        the LCG 00122BB8, 001281C0, 001274B0): in the oracle inside the same
        interpreter, on the native side in a second interpreter bound to the
        native module's own memory;
  stub  the callee is recorded and returns a scripted value (all 64 bits of
        v0 when the script gives a value outside the 32-bit range) and, where
        the case says so, writes scripted bytes behind a pointer argument
        (001FBF50's two stack words), identically on both sides.
Calls among the translated routines are not intercepted: the original runs
its own code, the native module calls its own translations.

Lockstep (the lane-SYS / EXITA design). The original runs first and records,
at every call leaving the set, the callee entry and every RAM / scratchpad
line it stored to since the previous call. The native module is built with
its store trace (EM_AREA00_LOW_STORE_TRACE) and, at the same call, must match
BEFORE any stub side effect is applied: the callee, the stack pointer, every
argument register the policy compares (a0..t3 as 64-bit images, f12..f19),
every register the native module says it sets, the bytes behind stack
arguments, the bytes behind stub pointer arguments, and all of RAM and the
scratchpad (only lines either side stored to since the last check can
differ, so exactly those are compared). The same memory check runs after
the last store, then all 32 MiB, the scratchpad and the result.

Stub side effects ('fx' cases): after the k-th stub every field the
translated routines load after it changes (bytes / halfwords bit 0, words
bit 4; the indirect handler pointer moves to a second stubbed address), so
a value cached across a stub differs; 'fx before' changes, at the k-th stub,
every field loaded since the previous stub, so a read deferred across a stub
differs.

Cases: 'cap' the captured records of the AREA00 beats as they are; 't'
enumerated paths (every state and sub-state, both sides of every test, LCG
seeds solved so the original's own draws take each arm, edge values,
results non-zero only in the upper half of v0); 'r' random perturbations
(full mode). EM_TEST_FULL=1 runs every case and every fx variant and asserts
that both outcomes of every conditional branch in the translated routines
are taken except the documented UNREACHABLE ones; the default run runs the
pinned cases, a fixed sample of the rest and of the fx variants, and the API
checks. EM_AREA00_LOW_ONLY=<prefix,...> runs only matching cases;
EM_AREA00_LOW_SOURCE=<file> tests another copy of the module source
(mutation sweeps); EM_AREA00_LOW_GAPS=1 lists branch outcomes not taken.
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
ROUTE = DECOMP / 'build/s87/route_a00'
BEAT = 'a00_09_ne_room_out'
OUT = ROOT / 'build' / 'area00' / 'low'
STACK_BASE, STACK_SIZE = 0x7F000000, 0x100000
STACK_LO, STACK_HI = SR.STACK_LO, SR.STACK_HI
SP = shared.STACK_TOP
SCRATCH = 0x01E00000        # zero in every AREA00 beat image; synthetic records live here
PLAYER, CAM = 0x8102B0, 0x8101E0

# Translated routines (address -> size in bytes, from the census).
FUNCS = {
    0x1000C0: 0x20, 0x102870: 0x20, 0x102990: 0x10, 0x1181B0: 0x264, 0x119080: 0x34, 0x128830: 0x98,
    0x1288D0: 0xE4, 0x129F00: 0xB8, 0x129FC0: 0x604, 0x12B410: 0x434, 0x12B970: 0x4A4, 0x12BE20: 0x66C,
    0x12C490: 0x604, 0x12CAA0: 0x7A0, 0x12D240: 0x33C, 0x12D580: 0x2C4, 0x12D850: 0xE8, 0x12DE90: 0x1E0,
    0x12E070: 0x3C, 0x12E0B0: 0x1A4, 0x12E260: 0x54, 0x12E2C0: 0xE0, 0x198CE0: 0xA4, 0x198F10: 0x308,
    0x1B5360: 0x280, 0x1B7670: 0x60, 0x1B7700: 0x138, 0x1B8AB0: 0x504, 0x1E7310: 0x128, 0x1FC580: 0x15C,
    0x1FF030: 0x48,
}

# Callee policy: address -> (kind, integer argument registers compared
# (a0..t3), float argument registers compared (f12..f19), [(pointer
# argument, bytes)] snapshotted at a stub's entry). Checked against the
# measured register reads (lane SYS RegScan) in setup().
R, S = 'run', 'stub'
CALLEES = {
    # run: leaves and math, executed as original code on both sides
    0x1026A0: (R, 3, 0, ()), 0x1026D0: (R, 3, 0, ()), 0x102760: (R, 2, 0, ()), 0x1028B8: (R, 3, 0, ()),
    0x102948: (R, 2, 0, ()), 0x102958: (R, 2, 0, ()), 0x1029C0: (R, 1, 0, ()), 0x102A60: (R, 2, 1, ()),
    0x102B08: (R, 2, 1, ()), 0x102BB0: (R, 2, 1, ()), 0x102C58: (R, 3, 0, ()), 0x1031E0: (R, 2, 0, ()),
    0x103230: (R, 2, 1, ()), 0x11DE90: (R, 0, 1, ()), 0x11DF78: (R, 0, 1, ()), 0x11E2A8: (R, 0, 1, ()),
    0x122BB8: (R, 0, 0, ()), 0x1281C0: (R, 0, 1, ()), 0x1274B0: (R, 4, 0, ()), 0x1B1240: (R, 1, 2, ()),
    0x1B12B0: (R, 0, 3, ()), 0x1B1470: (R, 0, 1, ()), 0x1B13F0: (R, 2, 1, ()),
    # stubs: callees translated elsewhere (em_area01_exita) or outside this lane
    0x128390: (S, 2, 0, ()), 0x128600: (S, 1, 0, ()), 0x128640: (S, 1, 0, ()), 0x12ADC0: (S, 3, 1, ()),
    0x1287F0: (S, 3, 1, ()), 0x1C2770: (S, 3, 0, ()), 0x1C25E0: (S, 2, 0, ((1, 16),)),
    0x1C2540: (S, 4, 0, ((2, 16),)), 0x1C24D0: (S, 3, 0, ((1, 16),)), 0x1C3D60: (S, 2, 0, ()),
    0x1C3DB0: (S, 4, 0, ((1, 16),)), 0x1C64F0: (S, 1, 1, ()), 0x1C69A0: (S, 1, 0, ()), 0x1B17A0: (S, 1, 0, ()),
    0x1EFD90: (S, 3, 0, ()), 0x1EFE00: (S, 2, 0, ()), 0x1EFFD0: (S, 4, 1, ()), 0x1FBD50: (S, 3, 1, ()),
    0x1FBF50: (S, 4, 2, ()), 0x19A570: (S, 4, 0, ()), 0x19AB20: (S, 4, 0, ()), 0x1F9100: (S, 3, 1, ()),
    0x1F9180: (S, 3, 1, ()), 0x1F4A00: (S, 2, 0, ((0, 16), (1, 16))), 0x18C4B0: (S, 1, 2, ()),
    0x18C6A0: (S, 2, 1, ()), 0x191530: (S, 2, 0, ()), 0x21BD60: (S, 1, 0, ()), 0x1D2830: (S, 2, 0, ()),
    0x1DEE80: (S, 2, 0, ()), 0x1DEEC0: (S, 2, 0, ()), 0x1DF5A0: (S, 0, 1, ()), 0x1AFC10: (S, 1, 0, ()),
    0x1FF080: (S, 2, 0, ()), 0x1179E0: (S, 2, 0, ()), 0x1157F0: (S, 4, 0, ()),
}
INDIRECT = (S, 1, 0, ())    # the +0x4C pointer of 00129FC0, called with the record
TAIL = {0x1FF030: 0x1FF080}
# Stack bytes behind pointer arguments a stub reads: 001FC580 hands 001FBF50
# its two stack words (each four bytes; the bytes after them are 001FC580's
# register save area, which differs by design).
# 001FBF50 stores 0 into both words at its entry, before it reads anything,
# so what they held before the call is not an input (and is not compared);
# the stub always writes them: zeros unless a case scripts values.
STACK_READS = {0x1FBF50: ()}
STUB_OUTS = {0x1FBF50: ((1, bytes(4)), (2, bytes(4)))}  # tail jumps (not jal): the hook is still the callee's entry

# Conditional-branch outcomes the full sweep cannot take, with the proof.
UNREACHABLE = {
    (0x1182A0, True): "001181B0's divide-by-zero guard before the first quotient compares its divisor, the "
                      "constant 60 held in a register, with zero",
    (0x1182C4, True): "the same guard before the second quotient (the divisor is the same constant 60)",
}


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
    with outs = [(argument index, bytes)] written behind that argument
    after the call; default (0, 0, ())."""

    def __init__(self, spec):
        self.queues = {fn: list(values) for fn, values in (spec or {}).items()}

    def take(self, fn):
        q = self.queues.get(fn)
        if q:
            v = q.pop(0)
            if not isinstance(v, tuple):
                return v, 0, STUB_OUTS.get(fn, ())
            return v[0], v[1] if len(v) > 1 else 0, v[2] if len(v) > 2 else STUB_OUTS.get(fn, ())
        return 0, 0, STUB_OUTS.get(fn, ())


def apply_outs(ee, regs, outs):
    for i, data in outs:
        a = regs[i] & MASK
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
                ('sp', U32), ('grab_bits', P(U8)), ('fault', I32), ('fault_function', U32),
                ('fault_address', U32)]


GRAB = 0x81083C     # the canonical byte behind the module's grab_bits view (its line is always compared)


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
uint32_t a00low_trace_list[LINES];
uint32_t a00low_trace_count;
void a00low_trace_store(uint32_t address, unsigned size);
void a00low_trace_reset(void);
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
        a00low_trace_list[a00low_trace_count++] = id;
    }
}
void a00low_trace_store(uint32_t address, unsigned size)
{
    mark(address);
    mark(address + size - 1u);
}
void a00low_trace_reset(void)
{
    uint32_t i;
    for (i = 0; i < a00low_trace_count; i++)
        dirty[a00low_trace_list[i]] = 0;
    a00low_trace_count = 0;
}
"""


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    trace = OUT / 'store_trace.c'
    if not trace.exists() or trace.read_text() != TRACE_C:
        trace.write_text(TRACE_C)
    source = os.environ.get('EM_AREA00_LOW_SOURCE', '')
    ext = 'dylib' if sys.platform == 'darwin' else 'so'
    if source:
        source = str(Path(source).resolve())
        lib = Path(source).with_suffix('.' + ext)
        rebuild = not lib.exists() or Path(source).stat().st_mtime > lib.stat().st_mtime
    else:
        source = str(ROOT / 'src/game/em_area00_low.c')
        lib = OUT / f'area00_low.{ext}'
        deps = [Path(source), ROOT / 'src/game/em_area00_low.h', ROOT / 'src/game/em_ee_float.h', trace,
                Path(__file__)]
        rebuild = not lib.exists() or max(d.stat().st_mtime for d in deps) > lib.stat().st_mtime
    if rebuild:
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                        '-ffp-contract=off', '-shared', '-fPIC', '-Isrc', '-Isrc/game',
                        '-DEM_AREA00_LOW_STORE_TRACE=a00low_trace_store', source, str(trace),
                        '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    TRACE['count'] = U32.in_dll(native, 'a00low_trace_count')
    TRACE['list'] = (U32 * (RAM_LINES + 0x4000 // LINE)).in_dll(native, 'a00low_trace_list')
    TRACE['reset'] = native.a00low_trace_reset
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
        self.ctx = Ctx(self.regions, 3, self.worker, None, SP,
                       C.cast(C.byref(self.ram, GRAB), P(U8)), 0, 0, 0)

    def dirty(self):
        # the view writes are not traced by address: the line holding the
        # canonical byte is compared at every check
        out = set(native_lines()) | {GRAB // LINE}
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


def native_call(fn, ctx, args, fargs):
    """(rc, result or None)."""
    out = I32(0)
    N = NATIVE
    a = [x & MASK for x in list(args) + [0] * 4]
    f = [x & MASK for x in list(fargs) + [0] * 3]
    two = {0x129F00, 0x129FC0, 0x12B410, 0x12B970, 0x12BE20, 0x12C490, 0x12CAA0, 0x12D240, 0x12D850,
           0x198CE0, 0x198F10}
    if fn == 0x1000C0:
        rc = N.em_area00_low_001000C0(ctx, U64(args[0] & MASK64), U64(args[1] & MASK64), U64(args[2] & MASK64),
                                      U64(args[3] & MASK64), C.byref(out))
    elif fn == 0x102870:
        return N.em_area00_low_00102870(ctx, U32(a[0]), U32(a[1]), U32(f[0])), None
    elif fn == 0x102990:
        return N.em_area00_low_00102990(ctx, U32(a[0]), U32(a[1])), None
    elif fn == 0x1181B0:
        return N.em_area00_low_001181B0(ctx, U32(a[0])), None
    elif fn == 0x119080:
        rc = N.em_area00_low_00119080(ctx, U32(a[0]), U32(a[1]), U32(a[2]), U32(a[3]), C.byref(out))
    elif fn == 0x128830:
        return N.em_area00_low_00128830(ctx, U32(a[0]), U32(f[0]), U32(f[1]), U32(f[2])), None
    elif fn == 0x1288D0:
        return N.em_area00_low_001288D0(ctx, U32(a[0]), U32(a[1])), None
    elif fn in two:
        return getattr(N, f'em_area00_low_{fn:08X}')(ctx, U32(a[0]), U32(a[1])), None
    elif fn == 0x12D580:
        a2 = args[2] & MASK64 if args[2] > MASK or args[2] < -(1 << 31) else sx32(args[2]) & MASK64
        return N.em_area00_low_0012D580(ctx, U32(a[0]), U32(a[1]), U64(a2)), None
    elif fn in (0x12DE90, 0x12E070, 0x12E260, 0x1B5360, 0x1E7310, 0x1FF030):
        return getattr(N, f'em_area00_low_{fn:08X}')(ctx, U32(a[0])), None
    elif fn in (0x12E0B0, 0x12E2C0):
        rc = getattr(N, f'em_area00_low_{fn:08X}')(ctx, U32(a[0]), U32(a[1]), C.byref(out))
    elif fn == 0x1B7670:
        rc = N.em_area00_low_001B7670(ctx, U32(a[2]), C.byref(out))
    elif fn in (0x1B7700, 0x1B8AB0):
        rc = getattr(N, f'em_area00_low_{fn:08X}')(ctx, U32(a[1]), U32(a[2]), C.byref(out))
    elif fn == 0x1FC580:
        return N.em_area00_low_001FC580(ctx, U32(a[0]), I32(SR.s32(a[1]))), None
    else:
        raise AssertionError(('no native entry', hex(fn)))
    return rc, out.value & MASK


RETURNS = {0x1000C0, 0x119080, 0x12E0B0, 0x12E2C0, 0x1B7670, 0x1B7700, 0x1B8AB0}


# ======================================================================
# Oracle side
# ======================================================================

def image(beat=BEAT):
    if beat not in IMAGES:
        IMAGES[beat] = ((ROUTE / beat / 'eeMemory.bin').read_bytes(), (ROUTE / beat / 'scratchpad.bin').read_bytes())
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


class VftoiMixin:
    """The shared interpreter leaves VFTOI0 / VFTOI4 (VU0 macro specials
    0x14 / 0x15) undefined; 00102990 uses VFTOI0. They run here through the
    measured model ee_float_model.vu_ftoi (docs/EE_FLOAT_MODEL.md), lane by
    lane under the dest mask."""

    def macro(self, word):
        op = word & 63
        special = (word >> 6 & 31) << 2 | (op & 3)
        if op >= 60 and special in (0x14, 0x15):
            fs, ft, mask = word >> 11 & 31, word >> 16 & 31, word >> 21 & 15
            src = [v & MASK for v in self.vf[fs]]
            for lane in range(4):
                if mask & (8 >> lane) and ft:
                    self.vf[ft][lane] = M.vu_ftoi(src[lane], 0 if special == 0x14 else 4)
            return
        return super().macro(word)


class LowEE(VftoiMixin, SR.SysEE):
    pass


class LowRecEE(VftoiMixin, SR.RecEE):
    pass


def oracle_run(case, ram, spad, cls=None, on_stub=None):
    """The original over `ram` / `spad` (used in place). Returns (ee, log,
    v0, entries, final delta); entries[k] = (log entry, {line: bytes}
    stored since the previous call, a0..t3 images, f12..f19 bits)."""
    ee = (cls or LowEE)(ELF, b'', b'')
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
    ee, *_ = oracle_run(case, ram, spad, cls=LowRecEE, on_stub=count)
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
    nat = NativeRun(ram, spad, Script(case['script']), set(case.get('indirect', ())))
    native_lines()
    where = (case['name'],)
    trap = False
    try:
        ee, olog, ov0, entries, last = oracle_run(case, ram, spad, on_stub=fx)
    except AssertionError as e:
        if case.get('fx') and e.args and isinstance(e.args[0], tuple) and e.args[0][:1] == ('address',):
            return set(), 0, -1, {}
        if case.get('trap') and e.args and e.args[0][:2] == ('SPECIAL', 13) \
                and case['fn'] <= int(e.args[0][2], 16) < case['fn'] + FUNCS[case['fn']]:
            trap = True             # the original's break (divide by zero) inside the routine
        else:
            raise AssertionError((where, 'the original run stopped', e.args)) from e
    if trap:
        rc, _ = native_call(case['fn'], C.byref(nat.ctx), case['args'], case.get('fargs', ()))
        assert rc == -1 and nat.ctx.fault == 4 and nat.ctx.fault_address == case['fn'], \
            (where, 'the original trapped; the native must fault with TRAP', rc, nat.ctx.fault)
        return LAST['ee'].outcomes, 0, 0, {}
    assert not case.get('trap'), (where, 'the original did not trap')
    if fx is not None:
        nat.fx = fx.log
    nat.lock = SR.Lockstep(entries, last, ee.pre, ee.mem, ee.spad)
    rc, nv0 = native_call(case['fn'], C.byref(nat.ctx), case['args'], case.get('fargs', ()))
    if nat.error is not None:
        raise AssertionError((where, 'native worker error', repr(nat.error)))
    if rc == -1 and case.get('fx') and nat.ctx.fault == 3 and 0x2000000 <= nat.ctx.fault_address < 0x40000000:
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


def case(name, fn, args, script=None, writes=(), fargs=(), indirect=(), beat=BEAT, **kw):
    return dict(name=name, fn=fn, beat=beat, args=list(args), script=script or {}, writes=list(writes),
                fargs=list(fargs), indirect=list(indirect), **kw)


def seeds(rng, wants, beat=BEAT):
    """Writes making the original LCG (00122BB8) produce draws satisfying
    wants[i] (None = any) on its next calls (lane SYS seed_writes)."""
    return SR.seed_writes(image(beat)[0], rng, wants)


def WV(a, *fs):
    return [WF(a + 4 * i, v) for i, v in enumerate(fs)]


NODES = (0x7AD490, 0x7ADD60)        # the captured records whose handler is 0012A5D0
ARM = {2: 0x12B410, 3: 0x12B970, 4: 0x12BE20, 5: 0x12C490, 6: 0x12CAA0, 7: 0x12D240, 8: 0x12D580, 9: 0x12D850}
ARM_BEATS = ('a00_00_descend', 'a00_01_door51_locked', 'a00_02_south_route', 'a00_03_padlock',
             'a00_04_cage_terminal', 'a00_05_ferry_deck', 'a00_06_cab_roof', 'a00_07_duct_to_ne_room',
             'a00_08_switch', 'a00_09_ne_room_out')
# A synthetic "D_00275B40" block: +0xC / +0x14 point at two bone-like
# records whose +0x90 matrix and +0xC0 floats the routines use.
MODEL = SCRATCH + 0x2000
MODEL_WRITES = ([W32(0x275B40, MODEL), W32(MODEL + 0xC, MODEL + 0x100), W32(MODEL + 0x14, MODEL + 0x200)] +
                WV(MODEL + 0x100 + 0x90, 1.0, 0.0, 0.0, 0.0, 0.0, 0.9, 0.1, 0.0, 0.0, -0.1, 0.9, 0.0, 3.0, 4.0, 5.0, 1.0) +
                WV(MODEL + 0x100 + 0xC0, 1.5, -2.25, 3.75) +
                WV(MODEL + 0x200 + 0x90, 0.8, 0.6, 0.0, 0.0, -0.6, 0.8, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, -7.0, 2.0, 9.5, 1.0) +
                WV(MODEL + 0x200 + 0xC0, -4.5, 6.125, 0.5))


def solve(fn, target, guess, span=4096):
    """Float bits x near `guess` (bits) with fn(x) == target under the EE
    arithmetic of ee_float_model, or None."""
    for d in range(span):
        for x in ((guess + d) & MASK, (guess - d) & MASK):
            if fn(x) == target:
                return x
    return None


def boundary(fn, bound, guess):
    """[(k, x bits)] with fn(x) == bound + k for k in -1, 0, 1 (found ones)."""
    out = []
    for k in (-1, 0, 1):
        x = solve(fn, (bound + k) & MASK, guess)
        if x is not None:
            out.append((k, x))
    return out


def arm_cases(rng):
    out = []
    for beat in ARM_BEATS:
        ram, _ = image(beat)
        for p in NODES:
            st, sub = ram[p + 4], ram[p + 5]
            b = p + 0x1F0
            ind = [u32(ram, p + 0x4C)]
            if st == 2:
                out.append(case(f'cap {beat} {p:x} 129FC0', 0x129FC0, [p, b], beat=beat, indirect=ind,
                                writes=MODEL_WRITES))
            if st == 1 and sub in ARM:
                fn = ARM[sub]
                args = [p, b, 0] if fn == 0x12D580 else [p, b]
                out.append(case(f'cap {beat} {p:x} {fn:06X}', fn, args, beat=beat, writes=MODEL_WRITES))
            out.append(case(f'cap {beat} {p:x} 1B5360', 0x1B5360, [p], beat=beat, script={0x19A570: [1]}))
            out.append(case(f'cap {beat} {p:x} 12E260', 0x12E260, [p], beat=beat))
            out.append(case(f'cap {beat} {p:x} 12E2C0', 0x12E2C0, [p, b], beat=beat))
    return out


def rec(p, st=None, sub=None, s7=None, extra=()):
    w = []
    if st is not None:
        w.append(W8(p + 6, st))
    if sub is not None:
        w.append(W8(p + 5, sub))
    if s7 is not None:
        w.append(W8(p + 7, s7))
    return w + list(extra)


def cases_129FC0(rng):
    out = []
    p = NODES[0]
    b = p + 0x1F0
    ram, _ = image()
    ind = [u32(ram, p + 0x4C)]
    for sub in range(10):
        for mode in (0, 1, 1 << 33):
            for vis in (0, 1, 1 << 32):
                for fa in (0, 0x80):
                    if vis != 1 and (fa or mode == 1 << 33):
                        continue
                    out.append(case(f't 129FC0 sub{sub} m{mode:#x} vis{vis:#x} fa{fa:x}', 0x129FC0, [p, b],
                                    indirect=ind, script={0x1C2770: [mode], 0x1B17A0: [vis]},
                                    writes=MODEL_WRITES + [W8(p + 5, sub), W8(b + 0xFA, fa)]))
    # state 0: the damage word, the knockdown tests, the death
    for d36 in (0x4005, 0x0005, 0x2003, 0x00FF, 0x6010):
        for hp in (3, 5, 20, 0, -2):
            for fb, e4 in ((0, 0x100), (0x80, 0x100), (0, 0), (0, 0x400), (0, 0x500), (0, 0x600)):
                if hp not in (3, 20) and (fb, e4) != (0, 0x100):
                    continue
                out.append(case(f't 129FC0 st0 d{d36:x} hp{hp} fb{fb:x} e4{e4:x}', 0x129FC0, [p, b],
                                indirect=ind, script={0x1B17A0: [0]},
                                writes=[W8(p + 5, 0), W16(p + 0x36, d36), W16(p + 0x34, hp), W8(b + 0xFB, fb),
                                        W32(b + 0xE4, e4)]))
    # states 1 and 5: cooldown, re-applied damage, anim-finished, 001C25E0
    for sub in (1, 5):
        for cd in (0, 1, 2):
            for d36 in (0, 0x4002, 0x0002, 0x0050):
                for hp in (0, 3, 1, -1):
                    for f4 in (0, 0x1000):
                        if sub == 5 and f4:
                            continue
                        out.append(case(f't 129FC0 st{sub} cd{cd} d{d36:x} hp{hp} f4{f4:x}', 0x129FC0, [p, b],
                                        indirect=ind, script={0x1B17A0: [0], 0x1C25E0: [1]},
                                        writes=[W8(p + 5, sub), W16(p + 0x28, cd), W16(p + 0x36, d36),
                                                W16(p + 0x34, hp), W16(b + 0xF4, f4), W8(b + 0xFB, 0x83),
                                                WF(p + 0x38, 0.75), W32(b + 0xE4, 0x600 if sub == 5 else 0x100)]))
    for g, e4 in ((0, 0x100), (1, 0x100), (1, 0x1FF), (1, 0x200), (1 << 32, 0x100), (1, -0x100)):
        out.append(case(f't 129FC0 st1 fall g{g:#x} e4{e4:#x}', 0x129FC0, [p, b], indirect=ind,
                        script={0x1B17A0: [0], 0x1C25E0: [g]},
                        writes=[W8(p + 5, 1), W16(p + 0x34, 0), W16(p + 0x36, 0), W16(b + 0xF4, 0x1000),
                                W32(b + 0xE4, e4 & MASK)]))
    for e4 in (0x600, 0x500):
        out.append(case(f't 129FC0 st5 e4 {e4:#x}', 0x129FC0, [p, b], indirect=ind, script={0x1B17A0: [0],
                                                                                           0x19A570: [1]},
                        writes=[W8(p + 5, 5), W32(b + 0xE4, e4), WF(p + 0x70, 0.5), WF(p + 0x78, -0.25)]))
    # state 4: +0x3C against 10.0, the event result
    for v3c in (9.0, 10.0, 11.0):
        for ev in (0, 1, 1 << 32):
            for g in (0, 1):
                out.append(case(f't 129FC0 st4 3c{v3c} ev{ev:#x} g{g}', 0x129FC0, [p, b], indirect=ind,
                                script={0x1B17A0: [0], 0x1EFE00: [ev], 0x19AB20: [g], 0x1FBF50: [(1, 0, (
                                    (1, struct.pack('<i', 5)), (2, struct.pack('<i', 7))))]},
                                writes=[W8(p + 5, 4), WF(p + 0x3C, v3c), WF(b + 0xF0, 3.97)]))
    return out


def cases_arms(rng):
    out = []
    p = NODES[0]
    b = p + 0x1F0
    B = [W32(b + 0xE4, 0x100)]
    for fn, states in ((0x12B970, 6), (0x12BE20, 8), (0x12C490, 9)):
        for st in range(states + 1):
            for busy in (0, 1, 1 << 32):
                for f4 in (0, 0x1000, 0x4000):
                    for e4 in (0x100, 0x101, 0x0, 0x300):
                        if busy and (f4 or e4 != 0x100):
                            continue
                        for probe in (0, 1):
                            if probe and (st != 0 or busy):
                                continue
                            out.append(case(f't {fn:06X} st{st} busy{busy:#x} f4{f4:x} e4{e4:x} pr{probe}',
                                            fn, [p, b],
                                            script={0x1C2770: [busy], 0x1C25E0: [probe], 0x128640: [0],
                                                    0x19A570: [1], 0x21BD60: [0], 0x128600: [1]},
                                            writes=MODEL_WRITES + rec(p, st, s7=0) + [
                                                W16(b + 0xF4, f4), W32(b + 0xE4, e4), W8(p + 0xB, 1),
                                                WF(p + 0x3C, 70.0), WF(b + 0xF0, 0.02)]))
    # 0012B970 state 0: 00128600 both ways; state 5 00128640 both ways
    for v in (0, 1, 1 << 32):
        out.append(case(f't 12B970 st0 600 {v:#x}', 0x12B970, [p, b], script={0x1C25E0: [1], 0x128600: [v]},
                        writes=rec(p, 0) + [W32(b + 0xE4, 0x200)]))
        for fn, st in ((0x12B970, 5), (0x12BE20, 7), (0x12C490, 8)):
            out.append(case(f't {fn:06X} st{st} 640 {v:#x}', fn, [p, b], script={0x128640: [v]},
                            writes=rec(p, st) + [W16(b + 0xF4, 0x1000)]))
    for e1 in (0, 1):
        for v3c in (70.0, 69.99):
            out.append(case(f't 12B970 st4 e1 {e1} 3c {v3c}', 0x12B970, [p, b],
                            writes=MODEL_WRITES + rec(p, 4) + [W8(b + 0xE1, e1), WF(p + 0x3C, v3c),
                                                               W16(b + 0xF4, 0x1000)]))
    # 0012BE20 / 0012C490 state 5 and 6: the rise, the e4 mask, 0012E0B0
    for fn in (0x12BE20, 0x12C490):
        for f0 in (0.6, 0.05, 0.04, 0.0, 0.03):
            for e4 in (0, 1, 0x10):
                for v640 in (0, 1):
                    out.append(case(f't {fn:06X} st5 f0 {f0} e4 {e4:#x} 640 {v640}', fn, [p, b],
                                    script={0x19A570: [1], 0x128640: [v640], 0x21BD60: [0]},
                                    writes=MODEL_WRITES + rec(p, 5) + [WF(b + 0xF0, f0), W32(b + 0xE4, e4),
                                                                       W8(p + 0xB, 1), W8(b + 0xE1, v640)]))
    x, y, z = struct.unpack_from('<3f', image()[0], 0x810350)
    for c0 in (-0.2, -0.17453294, -0.1745329, -0.1, 0.0, 0.1):
        for f8 in (0x10, 0):
            for d in (10.0, 100.0):
                f0 = -(c0 * 180.0 / 75.0) / 3.1415927 + 0.04
                out.append(case(f't 12BE20 st5 c0 {c0} f8 {f8:#x} d {d}', 0x12BE20, [p, b],
                                script={0x19A570: [0]},
                                writes=MODEL_WRITES + rec(p, 5) + [W32(b + 0xE4, 0), W16(b + 0xF8, f8),
                                                                   WF(b + 0xF0, f0)] + WV(p + 0xB0, x + d, y, z)))
    # 0012C490 states 5 / 6 with 0012E0B0 refusing (byte +0x0B zero, or all
    # four side bits of D_0081083C taken)
    for st in (5, 6):
        for gb, bits in ((0, 0), (1, 0xFF), (1, 0x0F), (1, 0xF0)):
            out.append(case(f't 12C490 st{st} claim gb {gb} bits {bits:#x}', 0x12C490, [p, b],
                            script={0x19A570: [0], 0x21BD60: [0]},
                            writes=rec(p, st) + [W32(b + 0xE4, 0), WF(b + 0xF0, 0.6), W16(b + 0xD0, 0x10),
                                                 W8(p + 0xB, gb), W8(0x81083C, bits), W8(0x70003B8D, 0),
                                                 W8(0x8106BC, 0)]))
    # the spray of 0012BE20 state 5: the record within / outside 50.0 of the
    # point D_00810350 (001B13F0 runs as original code), both +0xE1 values
    x, y, z = struct.unpack_from('<3f', image()[0], 0x810350)
    for e1 in (0, 1):
        for d in (10.0, 49.0, 51.0, 100.0):
            out.append(case(f't 12BE20 st5 spray e1 {e1} d {d}', 0x12BE20, [p, b], script={0x19A570: [0]},
                            writes=MODEL_WRITES + rec(p, 5) + [W32(b + 0xE4, 0), W16(b + 0xF8, 0), W8(b + 0xE1, e1),
                                                               WF(b + 0xF0, 0.0764)] +
                            WV(p + 0xB0, x + d, y, z)))
    for fn in (0x12BE20, 0x12C490):
        for st in (6, 7):
            for e4 in (0x100, 0x3, 0x0, 0x200):
                for v640 in (0, 1):
                    out.append(case(f't {fn:06X} st{st} e4 {e4:#x} 640 {v640}', fn, [p, b],
                                    script={0x19A570: [0], 0x128640: [v640], 0x21BD60: [0]},
                                    writes=rec(p, st) + [W32(b + 0xE4, e4), W16(b + 0xD0, 0x2C0), W8(p + 0xB, 1)]))
    for d0 in (0x2B8, 0x2B9, 0x2A0, -0x10):
        for gate in (0, 1):
            out.append(case(f't 12C490 st6 d0 {d0:#x} gate {gate}', 0x12C490, [p, b],
                            script={0x19A570: [0], 0x21BD60: [gate]},
                            writes=rec(p, 6) + [W32(b + 0xE4, 0), W16(b + 0xD0, d0 & 0xFFFF), W8(p + 0xB, 1)]))
    # 0012B410
    for st in range(6):
        for busy in (0, 1, 1 << 32):
            for pr, v600 in ((0, 0), (1, 1), (1, 0), (1 << 32, 1 << 32)):
                if st != 0 and (pr, v600) != (0, 0):
                    continue
                for adc in (0, 1, 1 << 32):
                    if st != 0 and adc:
                        continue
                    out.append(case(f't 12B410 st{st} busy{busy:#x} pr{pr:#x} 600{v600:#x} adc{adc:#x}', 0x12B410,
                                    [p, b], script={0x1C2770: [busy], 0x1C25E0: [pr], 0x128600: [v600],
                                                    0x12ADC0: [adc], 0x128640: [0]},
                                    writes=rec(p, st) + seeds(rng, [None])))
    for par in (0, 1):
        out.append(case(f't 12B410 st0 par {par}', 0x12B410, [p, b], script={0x1C25E0: [0]},
                        writes=rec(p, 0) + seeds(rng, [lambda v, par=par: v & 1 == par])))
    ram, spad = image()
    for p230 in (8, 1):
        for m in (0, 0x10, 0x20, 0x1F, 0x0F, 0x40):
            for d0 in (1, 2):
                out.append(case(f't 12B410 st1 p230 {p230} m{m:#x} d0 {d0}', 0x12B410, [p, b],
                                script={0x1C2770: [1]},
                                writes=rec(p, 1) + [W32(PLAYER + 0x230, p230), W32(0x70003B68, m),
                                                    W16(b + 0xD0, d0)] + seeds(rng, [None])))
    for e1 in (0, 1):
        for h54 in (0, 1, -0x8000):
            for d0 in (1, 5):
                out.append(case(f't 12B410 st2 e1 {e1} 54 {h54} d0 {d0}', 0x12B410, [p, b], script={0x1C2770: [1]},
                                writes=rec(p, 2) + [W8(b + 0xE1, e1), W16(p + 0x54, h54 & 0xFFFF), W16(b + 0xD0, d0)]))
    for v in (0, 1, 1 << 32):
        out.append(case(f't 12B410 st3 {v:#x}', 0x12B410, [p, b], script={0x128640: [v]}, writes=rec(p, 3)))
    for s7 in range(4):
        for f4 in (0, 0x1000):
            for d in (0, 1):
                out.append(case(f't 12B410 st4 s7 {s7} f4 {f4:x} d {d}', 0x12B410, [p, b], script={0x1C2770: [1]},
                                writes=rec(p, 4, s7=s7) + [W16(b + 0xF4, f4), W8(0x81070A, d)]))
    # 0012D580 through its own entry (a2 whole) and 0012D850
    for s7 in range(5):
        for a2 in (0, 1, 1 << 32):
            for e4 in (0x300, 0x100, 0x500):
                for f8, f4 in ((8, 0), (7, 0), (0, 0x1000), (0, 0), (7, 0x1000)):
                    for s4, s5 in ((1, 3), (2, 3), (1, 8)):
                        if s7 not in (1, 3) and (f8, f4) not in ((0, 0), (0, 0x1000)):
                            continue
                        if s7 != 3 and (s4, s5) != (1, 3):
                            continue
                        out.append(case(f't 12D580 s7 {s7} a2 {a2:#x} e4 {e4:#x} f8 {f8} f4 {f4:x} s {s4}{s5}',
                                        0x12D580, [p, b, a2], script={0x19A570: [1]},
                                        writes=rec(p, 0, s5, s7) + [W8(p + 4, s4), W32(b + 0xE4, e4),
                                                                   W16(b + 0xF8, f8), W16(b + 0xF4, f4)]))
    for st in range(3):
        for busy in (0, 1, 1 << 32):
            for d0 in (1, 2, 0):
                out.append(case(f't 12D850 st{st} busy{busy:#x} d0 {d0}', 0x12D850, [p, b],
                                script={0x1C2770: [busy]},
                                writes=rec(p, st) + [W16(b + 0xD0, d0)] + seeds(rng, [None])))
    # stores over fields whose old value already matched in the low half
    # (a narrower store would leave the old upper half)
    for s7, f8 in ((1, 8), (1, 7), (2, 0)):
        out.append(case(f't 12D580 s7 {s7} f8 {f8} e4 upper', 0x12D580, [p, b, 0], script={0x19A570: [1]},
                        writes=rec(p, 0, 3, s7) + [W8(p + 4, 1), W32(b + 0xE4, 0x12340100 if s7 == 2 else 0x12340300),
                                                  W16(b + 0xF8, f8), W16(b + 0xF4, 0), WF(b + 0xD8, 0.6),
                                                  WF(b + 0xF0, 0.6)]))
    for fn in (0x12B970, 0x12BE20, 0x12C490):
        out.append(case(f't {fn:06X} st2 d8 set', fn, [p, b], script={0x1C2770: [1]},
                        writes=rec(p, 2, s7=5) + [WF(b + 0xD8, 0.6)]))
    # 0012CAA0 / 0012D240: the attachment
    for fn, states in ((0x12CAA0, 4), (0x12D240, 3)):
        for st in range(states):
            for f6 in (0, 3, 4, 7, 0x85):
                for gate, g3 in ((0, 0), (1, 0), (0, 1)):
                    if gate or g3:
                        if f6 != 3:
                            continue
                    for d4 in (1.0, 0.21, 0.2, 2.0, 1.99):
                        if fn == 0x12CAA0 and st != 1 and d4 not in (1.0,):
                            continue
                        if fn == 0x12D240 and d4 not in (1.99, 2.0):
                            continue
                        out.append(case(f't {fn:06X} st{st} f6 {f6:#x} g{gate}{g3} d4 {d4}', fn, [p, b],
                                        writes=MODEL_WRITES + rec(p, st, s7=0) + [
                                            W16(b + 0xF6, f6), W8(0x8106BC, gate), W8(0x70003B8D, g3),
                                            WF(b + 0xD4, d4), W16(b + 0xD0, 1), W16(b + 0xF4, 0x4000)]))
        for e1 in (0, 1):
            for d in (0, 1):
                for f6 in (3, 5):
                    st = 2 if fn == 0x12CAA0 else 1
                    out.append(case(f't {fn:06X} st{st} e1 {e1} d {d} f6 {f6}', fn, [p, b],
                                    writes=MODEL_WRITES + rec(p, st, s7=0) + [
                                        W16(b + 0xF6, f6), W8(b + 0xE1, e1), W8(0x81070A, d),
                                        WF(b + 0xD4, 1.99), W16(b + 0xD0, 5), W16(b + 0xF4, 0x4000)]))
    for s7, f4 in ((1, 0x4000), (0, 0)):
        out.append(case(f't 12CAA0 st2 s7 {s7} f4 {f4:x}', 0x12CAA0, [p, b],
                        writes=rec(p, 2, s7=s7) + [W16(b + 0xF4, f4), W16(b + 0xD0, 2)]))
    return out


def cases_small(rng):
    out = []
    p = NODES[0]
    b = p + 0x1F0
    ram, _ = image()
    # 001000C0 through the soft-float compare 001274B0 (doubles in a0 / a1)
    dv = lambda x: struct.unpack('<Q', struct.pack('<d', x))[0]
    for x, y in ((1.0, 2.0), (2.0, 1.0), (1.0, 1.0), (-0.0, 0.0), (-3.5, -3.25), (0.6 * 3.14159, 1.884)):
        out.append(case(f't 1000C0 {x} {y}', 0x1000C0, [dv(x), dv(y), dv(y), dv(x)]))
    # 00102870 / 00102990
    for f in (2.0, 0.5, -3.0, 0.0, -0.0, 1e-40, 3e38):
        out.append(case(f't 102870 f{f}', 0x102870, [SCRATCH + 0x40, SCRATCH], fargs=[FB(f)],
                        writes=WV(SCRATCH, 1.5, -2.0, 7.25, 9.0)))
    out.append(case('t 102870 unaligned', 0x102870, [SCRATCH + 0x47, SCRATCH + 0x9], fargs=[FB(3.0)],
                    writes=WV(SCRATCH, 1.5, -2.0, 7.25, 9.0)))
    for vals in ((1.5, -2.7, 2147483647.0, -3e9), (0.99, -0.99, 1e10, float('inf')), (65536.5, -1.0, 0.0, -0.0)):
        out.append(case(f't 102990 {vals}', 0x102990, [SCRATCH + 0x43, SCRATCH + 0xC], writes=WV(SCRATCH, *vals)))
    # 00119080
    for args in ((10, 200, 4, 2), (200, 10, 4, 3), (0x1FF, 0x100, 0x303, 0x480), (5, 5, 1, 0), (0, 255, 255, 255),
                 (255, 0, 1, 255), (-1, -2, -3, -4)):
        out.append(case(f't 119080 {args}', 0x119080, list(args)))
    for args in ((10, 20, 0, 5), (10, 20, 0x100, 5)):
        out.append(case(f't 119080 trap {args}', 0x119080, list(args), trap=True))
    # 00128830 / 001288D0 / 00129F00
    for fs in ((0.0, 0.0, -3.5), (0.0, 5.0, 0.5), (1.25, -2.0, 3.0)):
        out.append(case(f't 128830 {fs}', 0x128830, [p], fargs=[FB(v) for v in fs]))
    for e1 in (0, 1, 0x80):
        out.append(case(f't 1288D0 e1 {e1}', 0x1288D0, [p, b], writes=MODEL_WRITES + [W8(b + 0xE1, e1)]))
    for f0 in (0.0, 3.93, 3.94, 3.95, 4.0, 10.0):
        for g in (0, 1, 1 << 32):
            out.append(case(f't 129F00 f0 {f0} g{g:#x}', 0x129F00, [p, b], script={0x19AB20: [g]},
                            writes=[WF(b + 0xF0, f0)]))
    # thresholds hit exactly (and one ulp either side) through the EE sums
    for k, x in boundary(lambda x: M.ee_add(x, FB(0.06)), 0x40800000, FB(3.94)):
        out.append(case(f't 129F00 f0 at 4.0{k:+d}', 0x129F00, [p, b], writes=[W32(b + 0xF0, x)]))
    for k, x in boundary(lambda x: M.ee_add(x, FB(0.02)), 0x40000000, FB(1.98)):
        out.append(case(f't 12DE90 d4 at 2.0{k:+d}', 0x12DE90, [b], writes=MODEL_WRITES + [W32(b + 0xD4, x)]))
        out.append(case(f't 12D240 d4 at 2.0{k:+d}', 0x12D240, [p, b],
                        writes=MODEL_WRITES + rec(p, 1) + [W32(b + 0xD4, x), W16(b + 0xF6, 3)]))
    for k, x in boundary(lambda x: M.ee_sub(x, FB(0.02)), 0x3E4CCCCD, FB(0.22)):
        out.append(case(f't 12CAA0 d4 at 0.2{k:+d}', 0x12CAA0, [p, b],
                        writes=MODEL_WRITES + rec(p, 1, s7=0) + [W32(b + 0xD4, x), W16(b + 0xF6, 3)]))
    c0 = lambda f: M.ee_div(M.ee_mul(0x40490FDB, M.ee_mul(FB(75.0), M.ee_neg(f))), FB(180.0))
    x0, y0, z0 = struct.unpack_from('<3f', ram, 0x810350)
    for k, x in boundary(c0, 0xBE32B8C3, FB(0.1333)):
        out.append(case(f't 12BE20 st5 c0 at -10deg{k:+d}', 0x12BE20, [p, b], script={0x19A570: [0]},
                        writes=MODEL_WRITES + rec(p, 5) + [W32(b + 0xE4, 0), W16(b + 0xF8, 0), W32(b + 0xF0, x)] +
                        WV(p + 0xB0, x0 + 5.0, y0, z0)))
    for bits in (0x428C0000 - 1, 0x428C0000 + 1):
        out.append(case(f't 12B970 st4 3c {bits:#x}', 0x12B970, [p, b],
                        writes=MODEL_WRITES + rec(p, 4) + [W32(p + 0x3C, bits), W16(b + 0xF4, 0x1000)]))
    for bits in (0x41200000 - 1, 0x41200000):
        out.append(case(f't 129FC0 st4 3c {bits:#x}', 0x129FC0, [p, b], indirect=[u32(ram, p + 0x4C)],
                        script={0x1B17A0: [0]}, writes=[W8(p + 5, 4), W32(p + 0x3C, bits)]))
    # 0012DE90
    for d4 in (0.0, 1.97, 1.98, 1.99, 2.0, 5.0):
        out.append(case(f't 12DE90 d4 {d4}', 0x12DE90, [b], writes=MODEL_WRITES + [WF(b + 0xD4, d4)]))
    # 0012E070 / 0012E0B0 / 0012E260 / 0012E2C0
    for f6 in (0, 0x80, 0x83, 0x87, 0x7F, -0x7F81):
        for bits in (0xFF, 0x00, 0x08):
            out.append(case(f't 12E070 f6 {f6:#x} bits {bits:#x}', 0x12E070, [b],
                            writes=[W16(b + 0xF6, f6 & 0xFFFF), W8(0x81083C, bits)]))
    for g3, g21, g6, gb in ((1, 0, 0, 1), (0, 1, 0, 1), (0, 1 << 32, 0, 1), (0, 0, 1, 1), (0, 0, 0, 0)):
        out.append(case(f't 12E0B0 gates {g3}{g21:#x}{g6}{gb}', 0x12E0B0, [p, b], script={0x21BD60: [g21]},
                        writes=[W8(0x70003B8D, g3), W8(0x8106BC, g6), W8(p + 0xB, gb)]))
    for dy in (0.0, 1.0, 1.5707963, 1.5707964, 1.5707965, 3.0, -3.0, 6.0):
        for bits in (0x00, 0x0F, 0xF0, 0xFF, 0x05, 0x70):
            out.append(case(f't 12E0B0 dy {dy} bits {bits:#x}', 0x12E0B0, [p, b],
                            writes=[WF(PLAYER + 0xC4, dy), WF(p + 0xC4, 0.0), W8(0x81083C, bits), W8(p + 0xB, 1),
                                    W8(0x70003B8D, 0), W8(0x8106BC, 0)]))
    for s7 in (0, 1, 2, 0xFF):
        for d2 in (1, 2, 0):
            for eq in (0, 1):
                out.append(case(f't 12E2C0 s7 {s7} d2 {d2} eq {eq}', 0x12E2C0, [p, b],
                                writes=[W8(p + 7, s7), W16(b + 0xD2, d2), WF(b + 0xE8, 1.0),
                                        WF(p + 0xC4, 1.0 if eq else 0.9)]))
    # 001B5360: every +3 mode, both probe results
    for m in list(range(14)) + [0xFF]:
        for hit in (0, 1, 1 << 32):
            if hit != 1 and m not in (0, 4):
                continue
            out.append(case(f't 1B5360 m{m} hit{hit:#x}', 0x1B5360, [p], script={0x19A570: [hit]},
                            writes=[W8(p + 3, m), W32(0x700031D0, SCRATCH + 0x300)] +
                            WV(SCRATCH + 0x300 + 0x24, 1.5, -2.5, 3.5)))
    # 00198CE0 / 00198F10 on the camera record and the player
    for st in (0, 1, 2):
        for m in (0x11, 0x12, 0x10):
            out.append(case(f't 198CE0 st{st} m{m:#x}', 0x198CE0, [CAM, PLAYER],
                            writes=[W8(CAM + 1, st), W32(PLAYER + 0x230, m)]))
            for kind in (0, 1, 2):
                out.append(case(f't 198F10 st{st} m{m:#x} k{kind}', 0x198F10, [CAM, PLAYER],
                                writes=[W8(CAM + 1, st), W32(PLAYER + 0x230, m), W8(PLAYER + 0xD, kind)]))
    for a7, a1 in ((8, 3), (8, 2), (7, 3)):
        for dx, dz in ((0.0, 0.0), (7.9, 0.0), (8.0, 0.0), (5.0, 6.0), (6.0, 5.4)):
            out.append(case(f't 198F10 home {a7}{a1} {dx} {dz}', 0x198F10, [CAM, PLAYER],
                            writes=[W8(CAM + 1, 1), W32(PLAYER + 0x230, 0x12), W8(0x810700, a7), W8(0x810701, a1),
                                    WF(PLAYER + 0xA0, 123.5 + dx), WF(PLAYER + 0xA8, 156.4 + dz)]))
    # 001B7670 / 001B7700 / 001B8AB0: a script record at SCRATCH + 0x100, the
    # state object at SCRATCH + 0x200
    obj, scr = SCRATCH + 0x200, SCRATCH + 0x100
    for m in (0, 1, 2, 3, -1):
        for g in (0, 1, 2):
            out.append(case(f't 1B7670 m{m} g{g}', 0x1B7670, [0, obj, scr],
                            writes=[W32(scr + 8, m & MASK), W8(0x70003B91, g)]))
        for s in (0, 1, 2, 3):
            for g in (0, 1):
                out.append(case(f't 1B7700 m{m} s{s} g{g}', 0x1B7700, [0, obj, scr],
                                writes=[W32(scr + 8, m & MASK), W8(obj + 4, s), W8(0x275BD8, g),
                                        W8(scr + 0x14, 0x93)]))
        if m in (0, 1, 2, 3):
            for s in (0, 1, 2):
                for c, lim in ((0.0, 5.0), (1.0, 5.0), (4.0, 5.0), (5.0, 5.0), (6.0, 5.0)):
                    for n14 in (0, 3, 0x40, -5):
                        if m != 2 and n14 not in (0, 3):
                            continue
                        out.append(case(f't 1B8AB0 m{m} s{s} c{c} n{n14}', 0x1B8AB0, [0, obj, scr],
                                        writes=[W32(scr + 8, m), W8(obj + 4, s), WF(scr + 0x10, c), WF(scr + 0xC, lim),
                                                W32(scr + 0x14, n14 & MASK)] +
                                        WV(scr + 0x20, 1.0, 2.0, 3.0, 1.0) + WV(scr + 0x30, -1.0, 0.5, 8.0, 1.0) +
                                        seeds(rng, [None, None])))
    # 001E7310
    ent = SCRATCH + 0x400
    for st in (0, 1, 2, 3, 4):
        for x in (0.5, 0.02, 0.0158, 0.015, 0.0):
            if st != 1 and x != 0.5:
                continue
            out.append(case(f't 1E7310 st{st} x{x}', 0x1E7310, [ent],
                            writes=[W8(ent + 4, st), WF(ent + 0x60, x), WF(ent + 0x64, 37.5)]))
    # 001FC580: every sound, both 001FBF50 results, the slot compare
    for snd in (0x19D, 0x19E, 0x19F, 0x1A0, 0x1A1, 0x1AA, 0x1B5, 0x15A, 0x15B, 0x1B0, -0x19D):
        for got in (0, 1, 1 << 32):
            if got != 1 and snd not in (0x19D, 0x1B5):
                continue
            out.append(case(f't 1FC580 {snd:#x} got{got:#x}', 0x1FC580, [p, snd],
                            script={0x1FBF50: [(got, 0, ((1, struct.pack('<i', 30)), (2, struct.pack('<i', 12))))]},
                            writes=[W32(0x281F30 + 16 * k, 0) for k in range(4)]))
    for have, x, y in ((1, 5, 5), (1, 5, 6), (1, 3, 3), (1, 0x7FFFFFFF, 1), (0, 1, 1)):
        out.append(case(f't 1FC580 slot {have} {x} {y}', 0x1FC580, [p, 0x1AA],
                        script={0x1FBF50: [(1, 0, ((1, struct.pack('<i', x)), (2, struct.pack('<i', y))))]},
                        writes=[W32(0x281F50, have), W32(0x281F58, 4), W32(0x281F5C, 6)]))
    # 001FF030
    for a0 in (0x05, 0x85, 0xFF, 0x80, 0x100, 0x1FF):
        for area in (0, 3, 0xFF):
            out.append(case(f't 1FF030 {a0:#x} area {area}', 0x1FF030, [a0], writes=[W8(0x810700, area)]))
    return out


def cases_1181B0(rng):
    """Synthetic events against the 48-record table at 0x0027CCC0: every
    record's fields are set so each test fails once and all pass once."""
    out = []
    ev = SCRATCH + 0x600
    data = SCRATCH + 0x700
    G = 0x281AC0
    base_w = [W32(G + 0x14, data - 0x10), W32(G + 0xC, SCRATCH + 0x680), W32(ev + 8, 0x10), W16(ev + 0x24, 0x33),
              W32(ev + 0x18, 0x21), W8(ev, 0x75), W16(0x27F740 + 0x3A, 0x3C)]
    base_w += [(data, bytes([0x11, 0x22, 0x9C, 0x44, 0x55, 0x66]))]
    for mode in (1, 0):
        # records 0..47: record i fails test (i % 8) (7 = all pass)
        w = []
        for i in range(48):
            v = 0x27CCC0 + i * 0x6A
            k = i % 9
            f = {0: 1, 0x1A: 2 if mode == 1 else 1, 0x22: 0x33, 0x3E: 0x55, 2: 0x66, 6: 0x21, 4: 5, 8: 0, 0x2C: 0x777}
            if k == 0:
                f[0] = 2
            elif k == 1:
                f[0x1A] = 3
            elif k == 2:
                f[0x22] = 0x34
            elif k == 3:
                f[0x3E if mode == 1 else 4] = 0x56 if mode == 1 else 6
            elif k == 4:
                f[2 if mode == 1 else 8] = 0x67 if mode == 1 else 1
            elif k == 5:
                f[6] = 0x22
            w += [W16(v + off, val) for off, val in f.items()]
        for ev18 in (0x21, -0xFFDF):
            out.append(case(f't 1181B0 mode{mode} ev18 {ev18:#x}', 0x1181B0, [ev],
                            script={0x1179E0: [0x12345678, -0x12345, 0x7FFF0000, 1 << 32]},
                            writes=base_w + w + [W16(ev + 0x34, mode), W32(ev + 0x18, ev18 & MASK)]))
    out.append(case('t 1181B0 mode2', 0x1181B0, [ev], writes=base_w + [W16(ev + 0x34, 2)]))
    out.append(case('t 1181B0 mode0 plain', 0x1181B0, [ev], writes=base_w + [W16(ev + 0x34, 0)]))
    return out


def cases_random(rng, n):
    """Full mode: random perturbations of the arms' records and the globals
    they read."""
    out = []
    p = NODES[0]
    b = p + 0x1F0
    fns = [0x12B410, 0x12B970, 0x12BE20, 0x12C490, 0x12CAA0, 0x12D240, 0x12D850, 0x129FC0]
    ram, _ = image()
    for i in range(n):
        fn = fns[i % len(fns)]
        w = MODEL_WRITES + [W8(p + 6, rng.randrange(10)), W8(p + 7, rng.randrange(4)), W8(p + 5, rng.randrange(9)),
                            W16(b + 0xF4, rng.choice((0, 0x1000, 0x4000, 0x5000))),
                            W32(b + 0xE4, rng.choice((0, 0x100, 0x101, 0x300, 0x400, 0x500, 0x600))),
                            W16(b + 0xF6, rng.randrange(0x100)), W16(b + 0xD0, rng.choice((0, 1, 2, 0x2D0, 0x2B9))),
                            WF(b + 0xD4, rng.choice((0.2, 0.22, 1.0, 1.99, 2.0))),
                            WF(b + 0xF0, rng.choice((0.6, 0.04, 0.0, 3.99))),
                            W16(p + 0x34, rng.choice((0, 1, 3, 30))), W16(p + 0x36, rng.choice((0, 0x4002, 0x2001, 5))),
                            W16(p + 0x28, rng.choice((0, 1, 2))), W8(b + 0xE1, rng.randrange(2)),
                            W8(0x70003B8D, rng.choice((0, 0, 0, 1))), W8(0x8106BC, rng.choice((0, 0, 0, 1))),
                            W8(p + 0xB, rng.randrange(2)), WF(p + 0x3C, rng.choice((9.0, 70.0, 12.0)))]
        w += seeds(rng, [None])
        sc = {0x1C2770: [rng.choice((0, 0, 1))], 0x1C25E0: [rng.randrange(2)], 0x128600: [rng.randrange(2)],
              0x128640: [rng.randrange(2)] * 2, 0x12ADC0: [rng.randrange(2)], 0x19A570: [rng.randrange(2)] * 2,
              0x1B17A0: [rng.randrange(2)], 0x21BD60: [rng.randrange(2)] * 2,
              0x1EFE00: [rng.randrange(2)] * 2, 0x19AB20: [rng.randrange(2)]}
        out.append(case(f'r {fn:06X} {i}', fn, [p, b], script=sc, writes=w, indirect=[u32(ram, p + 0x4C)]))
    return out


def cases_review():
    """Round-3 review killers (docs/AREA00_LOW.md section 5): exact inputs
    that separate four mutants the seed-7 sweep never produced. Own RNG, so
    the other generators' streams (and the sampled default run) are
    unchanged; every case here is pinned in the default run."""
    out = []
    rng = random.Random(0xA00D)
    p = NODES[0]
    b = p + 0x1F0
    ram, _ = image()
    ind = [u32(ram, p + 0x4C)]
    # 0012E2C0 state 1: the equality reads b +0xE8, not its neighbour +0xEC
    # (captured +0xEC is 1.0, the value the 'eq 1' cases write to +0xE8).
    for e8, ec in ((1.0, 0.5), (0.5, 1.0)):
        out.append(case(f'k 12E2C0 s7 1 d2 2 e8 {e8} ec {ec}', 0x12E2C0, [p, b],
                        writes=[W8(p + 7, 1), W16(b + 0xD2, 2), WF(b + 0xE8, e8), WF(p + 0xC4, 1.0),
                                WF(b + 0xEC, ec)]))
    # 0012B410 state 1: every single bit of the 0x1F / 0xF frame masks of
    # word 0x70003B68 and the next bit up (0x10 / 0x20: a widened mask),
    # plus 0x11, 0x18, 0x21 (low bits set under a higher bit).
    for p230 in (8, 1):
        for m in (1, 2, 4, 8, 0x10, 0x11, 0x18, 0x20, 0x21):
            out.append(case(f'k 12B410 st1 p230 {p230} m{m:#x}', 0x12B410, [p, b], script={0x1C2770: [1]},
                            writes=rec(p, 1) + [W32(PLAYER + 0x230, p230), W32(0x70003B68, m),
                                                W16(b + 0xD0, 2)] + seeds(rng, [None])))
    # 00198F10 state 1 with e +2 non-zero: the e +1 clear is a byte store.
    out.append(case('k 198F10 st1 m0x11 e2 5', 0x198F10, [CAM, PLAYER],
                    writes=[W8(CAM + 1, 1), W8(CAM + 2, 5), W32(PLAYER + 0x230, 0x11)]))
    # 00129FC0 states 1/5: the damage step leaves +0x34 exactly 0 (<= 0 arm).
    for sub in (1, 5):
        for hp in (2, 3):
            out.append(case(f'k 129FC0 st{sub} hp {hp} dmg 2', 0x129FC0, [p, b], indirect=ind,
                            script={0x1B17A0: [0], 0x1C25E0: [1], 0x19A570: [1]},
                            writes=[W8(p + 5, sub), W16(p + 0x28, 0), W16(p + 0x36, 2), W16(p + 0x34, hp),
                                    W16(b + 0xF4, 0x1000 if sub == 1 else 0), W8(b + 0xFB, 0x83),
                                    WF(p + 0x38, 0.75), W32(b + 0xE4, 0x600 if sub == 5 else 0x100)]))
    return out


def all_cases(rng):
    cs = arm_cases(rng) + cases_129FC0(rng) + cases_arms(rng) + cases_small(rng) + cases_1181B0(rng)
    cs += cases_review()
    if RM.FULL:
        cs += cases_random(rng, 160)
    return cs


def store_variants(stored):
    """Store-site variants (lane SYS round 4, as EXITA): a site no case
    covers (every byte it stores differing from the byte before) gets a
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


QUICK_CASES = 300
# A greedy cover of every branch outcome the full case set takes (computed
# once from the full run; it is data, not a claim: the default run prints
# its coverage).
# Mutant killers of the bounded sweep (docs/AREA00_LOW.md section 5): the
# exact-threshold cases and the cases that alone killed a mutant the
# sampled default run missed.
QUICK_KILLERS = {
    't 129F00 f0 at 4.0-1', 't 129F00 f0 at 4.0+0', 't 129F00 f0 at 4.0+1', 't 12DE90 d4 at 2.0+0',
    't 12D240 d4 at 2.0-1', 't 12D240 d4 at 2.0+0', 't 12CAA0 d4 at 0.2+0', 't 12CAA0 d4 at 0.2+1',
    't 12BE20 st5 c0 at -10deg-1', 't 12BE20 st5 c0 at -10deg+1', 't 12B970 st4 3c 0x428bffff',
    't 12B970 st4 3c 0x428c0001', 't 129FC0 st4 3c 0x411fffff', 't 12E070 f6 -0x7f81 bits 0xff',
    't 129FC0 sub3 m0x0 vis0x1 fa0', 't 129FC0 sub3 m0x0 vis0x0 fa0', 't 12B970 st2 busy0x0 f40 e4100 pr0',
    't 12D580 s7 1 f8 8 e4 upper', 't 12D580 s7 1 f8 7 e4 upper', 't 12D580 s7 2 f8 0 e4 upper',
    't 12B970 st2 d8 set', 't 12BE20 st2 d8 set', 't 12C490 st2 d8 set', 't 129FC0 st5 e4 0x500',
}
QUICK_PINNED = QUICK_KILLERS | {
    't 12C490 st5 claim gb 0 bits 0x0',
    't 1181B0 mode1 ev18 0x21',
    't 1181B0 mode0 ev18 0x21',
    't 12C490 st5 busy0x0 f40 e4100 pr0',
    't 129FC0 st1 cd0 d4002 hp1 f41000',
    't 129FC0 sub0 m0x1 vis0x1 fa80',
    't 12B410 st0 busy0x0 pr0x1 6000x1 adc0x0',
    't 1FC580 0x19d got0x1',
    't 12BE20 st5 c0 -0.1 f8 0x0 d 10.0',
    't 12CAA0 st2 f6 0x0 g00 d4 1.0',
    't 12B970 st1 busy0x0 f40 e4100 pr0',
    't 12D240 st1 f6 0x0 g00 d4 2.0',
    't 129FC0 st5 cd0 d4002 hp3 f40',
    't 1B8AB0 m0 s0 c0.0 n0',
    't 12CAA0 st0 f6 0x3 g10 d4 1.0',
    't 198F10 st0 m0x12 k0',
    't 1B7700 m0 s0 g0',
    't 1B8AB0 m1 s0 c0.0 n0',
    't 129FC0 st4 3c9.0 ev0x0 g0',
    't 12B410 st1 busy0x1 pr0x0 6000x0 adc0x0',
    't 1B8AB0 m2 s0 c0.0 n0',
    't 1E7310 st0 x0.5',
    'cap a00_03_padlock 7ad490 12C490',
    't 129FC0 sub1 m0x0 vis0x1 fa0',
    't 12B970 st0 busy0x0 f40 e4101 pr1',
    't 12B970 st3 busy0x1 f40 e4100 pr0',
    't 12C490 st6 d0 0x2b8 gate 1',
    't 12B410 st2 busy0x0 pr0x0 6000x0 adc0x0',
    't 12B410 st4 busy0x0 pr0x0 6000x0 adc0x0',
    't 12D580 s7 1 a2 0x0 e4 0x300 f8 0 f4 1000 s 13',
    't 12D580 s7 3 a2 0x0 e4 0x300 f8 0 f4 1000 s 13',
    't 12CAA0 st2 e1 1 d 0 f6 5',
    't 12D240 st0 f6 0x0 g00 d4 2.0',
    't 12E0B0 dy 0.0 bits 0xf0',
    't 198F10 home 83 0.0 0.0',
    't 1B7700 m1 s0 g0',
    't 129FC0 sub5 m0x0 vis0x0 fa0',
    't 129FC0 st0 d4005 hp3 fb80 e4100',
    't 129FC0 st1 cd0 d2 hp3 f41000',
    't 12B970 st4 busy0x0 f40 e4100 pr0',
    't 12BE20 st0 busy0x0 f40 e4100 pr1',
    't 12BE20 st4 busy0x0 f41000 e4100 pr0',
    't 12BE20 st5 busy0x1 f40 e4100 pr0',
    't 12BE20 st6 busy0x0 f40 e4101 pr0',
    't 12C490 st0 busy0x0 f40 e4100 pr1',
    't 12C490 st7 busy0x0 f40 e4101 pr0',
    't 12B410 st0 busy0x0 pr0x0 6000x0 adc0x0',
    't 12B410 st1 p230 8 m0x0 d0 1',
    't 12D850 st0 busy0x0 d0 1',
    't 12D850 st1 busy0x1 d0 1',
    't 12CAA0 st1 f6 0x3 g01 d4 0.21',
    't 12D240 st1 e1 1 d 0 f6 5',
    't 12E0B0 dy 1.5707965 bits 0xf',
    't 198CE0 st0 m0x11',
    't 1B7670 m0 g0',
    't 1B7700 m2 s1 g0',
    'cap a00_04_cage_terminal 7ad490 12B970',
    't 129FC0 sub4 m0x0 vis0x0 fa0',
    't 129FC0 st0 d4005 hp20 fb0 e40',
    't 129FC0 st5 cd0 d2 hp1 f40',
    't 129FC0 st4 3c9.0 ev0x1 g1',
    't 12B970 st5 busy0x0 f41000 e4100 pr0',
    't 12BE20 st5 busy0x0 f40 e4101 pr0',
    't 12BE20 st7 busy0x0 f41000 e4100 pr0',
    't 12C490 st0 busy0x1 f40 e4100 pr0',
    't 12C490 st5 busy0x0 f40 e4101 pr0',
    't 12C490 st6 busy0x0 f40 e4101 pr0',
    't 12C490 st8 busy0x0 f41000 e4100 pr0',
    't 12B970 st4 e1 0 3c 69.99',
    't 12B410 st3 busy0x0 pr0x0 6000x0 adc0x0',
    't 12B410 st2 e1 1 54 0 d0 1',
    't 12B410 st4 s7 1 f4 0 d 0',
    't 198F10 st0 m0x11 k1',
    't 198CE0 st1 m0x12',
    't 1B7700 m0 s2 g0',
    't 1B8AB0 m0 s1 c1.0 n0',
    't 1B7670 m1 g0',
    't 1B7700 m1 s2 g0',
    't 1B8AB0 m1 s1 c0.0 n3',
    't 1B8AB0 m2 s1 c1.0 n0',
    't 1E7310 st1 x0.015',
    't 1FC580 slot 1 5 5',
    'cap a00_04_cage_terminal 7ad490 12E2C0',
    'cap a00_05_ferry_deck 7ad490 12C490',
    'cap a00_06_cab_roof 7ad490 12BE20',
    'cap a00_07_duct_to_ne_room 7ad490 12D580',
    't 129FC0 sub2 m0x1 vis0x0 fa0',
    't 129FC0 sub8 m0x0 vis0x0 fa0',
    't 129FC0 st0 d4005 hp3 fb0 e4100',
    't 129FC0 st0 d4005 hp3 fb0 e4400',
    't 129FC0 st0 d4005 hp3 fb0 e4500',
    't 129FC0 st0 d2003 hp3 fb0 e4100',
    't 129FC0 st1 cd0 d4002 hp0 f40',
    't 129FC0 st1 cd2 d4002 hp3 f40',
    't 129FC0 st5 cd0 d4002 hp0 f40',
    't 129FC0 st5 cd2 d4002 hp3 f40',
    't 129FC0 st1 fall g0x0 e40x100',
    't 129FC0 st1 fall g0x1 e40x200',
    't 12B970 st0 busy0x0 f40 e4100 pr0',
    't 12B970 st0 busy0x0 f40 e4100 pr1',
    't 12B970 st0 busy0x1 f40 e4100 pr0',
    't 12B970 st1 busy0x0 f40 e4300 pr0',
    't 12B970 st3 busy0x0 f41000 e4100 pr0',
    't 12B970 st5 busy0x0 f40 e4100 pr0',
    't 12B970 st6 busy0x0 f40 e4100 pr0',
    't 12BE20 st0 busy0x0 f40 e4100 pr0',
    't 12BE20 st0 busy0x0 f40 e4101 pr1',
    't 12BE20 st0 busy0x1 f40 e4100 pr0',
    't 12BE20 st3 busy0x0 f40 e4100 pr0',
    't 12BE20 st3 busy0x0 f41000 e4100 pr0',
    't 12BE20 st4 busy0x0 f40 e4100 pr0',
    't 12BE20 st6 busy0x0 f40 e4100 pr0',
    't 12BE20 st6 busy0x0 f40 e40 pr0',
    't 12BE20 st8 busy0x0 f40 e4100 pr0',
    't 12C490 st0 busy0x0 f40 e4100 pr0',
    't 12C490 st0 busy0x0 f40 e4101 pr1',
    't 12C490 st3 busy0x0 f41000 e4100 pr0',
    't 12C490 st4 busy0x0 f41000 e4100 pr0',
    't 12C490 st6 busy0x0 f40 e4100 pr0',
    't 12C490 st7 busy0x0 f40 e4100 pr0',
    't 12C490 st7 busy0x0 f40 e40 pr0',
    't 12C490 st8 busy0x0 f40 e4100 pr0',
    't 12C490 st9 busy0x0 f40 e4100 pr0',
    't 12B970 st0 600 0x0',
    't 12B970 st5 640 0x1',
    't 12BE20 st7 640 0x1',
    't 12C490 st8 640 0x1',
    't 12B970 st4 e1 1 3c 70.0',
    't 12BE20 st5 f0 0.6 e4 0x0 640 0',
    't 12BE20 st5 f0 0.6 e4 0x1 640 1',
    't 12BE20 st5 f0 0.05 e4 0x0 640 0',
    't 12C490 st5 f0 0.6 e4 0x0 640 0',
    't 12C490 st5 f0 0.6 e4 0x1 640 1',
    't 12BE20 st5 c0 -0.2 f8 0x10 d 10.0',
    't 12BE20 st5 spray e1 1 d 10.0',
    't 12BE20 st6 e4 0x3 640 1',
    't 12C490 st6 e4 0x3 640 1',
    't 12C490 st7 e4 0x3 640 1',
    't 12C490 st6 d0 0x2b9 gate 1',
    't 12B410 st0 busy0x0 pr0x0 6000x0 adc0x1',
    't 12B410 st0 busy0x0 pr0x1 6000x1 adc0x1',
    't 12B410 st0 busy0x0 pr0x1 6000x0 adc0x0',
    't 12B410 st0 busy0x1 pr0x0 6000x0 adc0x0',
    't 12B410 st5 busy0x0 pr0x0 6000x0 adc0x0',
    't 12B410 st1 p230 8 m0x10 d0 1',
    't 12B410 st1 p230 1 m0x0 d0 1',
    't 12B410 st2 e1 0 54 1 d0 1',
    't 12B410 st3 0x1',
    't 12B410 st4 s7 1 f4 1000 d 0',
    't 12B410 st4 s7 2 f4 0 d 0',
    't 12B410 st4 s7 3 f4 0 d 0',
    't 12D580 s7 1 a2 0x0 e4 0x300 f8 8 f4 0 s 13',
    't 12D580 s7 1 a2 0x0 e4 0x300 f8 7 f4 0 s 13',
    't 12D580 s7 1 a2 0x0 e4 0x300 f8 7 f4 1000 s 13',
    't 12D580 s7 2 a2 0x0 e4 0x100 f8 0 f4 1000 s 13',
    't 12D580 s7 3 a2 0x0 e4 0x300 f8 0 f4 1000 s 23',
    't 12D580 s7 3 a2 0x0 e4 0x300 f8 0 f4 1000 s 18',
    't 12D580 s7 4 a2 0x0 e4 0x300 f8 0 f4 1000 s 13',
    't 12D850 st1 busy0x0 d0 2',
    't 12D850 st2 busy0x0 d0 1',
    't 12CAA0 st0 f6 0x4 g00 d4 1.0',
    't 12CAA0 st3 f6 0x0 g00 d4 1.0',
    't 12CAA0 st2 e1 0 d 1 f6 3',
    't 12CAA0 st2 e1 1 d 1 f6 3',
    't 12D240 st0 f6 0x3 g10 d4 2.0',
    't 12D240 st0 f6 0x3 g01 d4 2.0',
    't 12D240 st2 f6 0x0 g00 d4 2.0',
    't 12D240 st1 e1 0 d 1 f6 3',
    't 12D240 st1 e1 1 d 1 f6 3',
    't 12CAA0 st2 s7 1 f4 4000',
    't 12CAA0 st2 s7 0 f4 0',
    't 119080 (10, 200, 4, 2)',
    't 119080 trap (10, 20, 0, 5)',
    't 1288D0 e1 1',
    't 12E070 f6 0x0 bits 0xff',
    't 12E070 f6 0x80 bits 0xff',
    't 12E0B0 gates 10x001',
    't 12E0B0 gates 00x011',
    't 12E0B0 gates 00x000',
    't 12E0B0 dy 0.0 bits 0x0',
    't 12E2C0 s7 1 d2 1 eq 0',
    't 12E2C0 s7 1 d2 2 eq 1',
    't 1B5360 m4 hit0x0',
    't 1B5360 m13 hit0x1',
    't 198F10 st0 m0x11 k2',
    't 198CE0 st2 m0x11',
    't 198F10 st2 m0x11 k0',
    't 198F10 home 83 8.0 0.0',
    't 198F10 home 82 0.0 0.0',
    't 1B7670 m0 g1',
    't 1B7700 m0 s1 g0',
    't 1B7700 m0 s2 g1',
    't 1B7700 m0 s3 g0',
    't 1B8AB0 m0 s1 c5.0 n0',
    't 1B8AB0 m0 s2 c0.0 n0',
    't 1B7670 m1 g1',
    't 1B7700 m1 s1 g0',
    't 1B7700 m1 s2 g1',
    't 1B7700 m1 s3 g0',
    't 1B8AB0 m1 s1 c1.0 n0',
    't 1B8AB0 m1 s1 c5.0 n0',
    't 1B8AB0 m1 s2 c0.0 n0',
    't 1B7670 m2 g0',
    't 1B8AB0 m2 s1 c5.0 n0',
    't 1B8AB0 m2 s2 c0.0 n0',
    't 1B7700 m3 s0 g0',
    't 1B8AB0 m3 s0 c0.0 n0',
    't 1E7310 st2 x0.5',
    't 1E7310 st3 x0.5',
    't 1E7310 st4 x0.5',
    't 1FC580 0x19e got0x1',
    't 1FC580 0x19f got0x1',
    't 1FC580 0x1a0 got0x1',
    't 1FC580 0x1a1 got0x1',
    't 1FC580 0x15a got0x1',
    't 1FC580 0x15b got0x1',
    't 1FC580 0x1b0 got0x1',
    't 1FC580 slot 1 5 6',
    't 1FF030 0x5 area 0',
    't 1FF030 0x85 area 0',
}
FX_FNS = {0x129FC0, 0x12B410, 0x12B970, 0x12BE20, 0x12C490, 0x12CAA0, 0x12D240, 0x12D580, 0x12D850, 0x1B5360,
          0x129F00, 0x1FC580, 0x1E7310, 0x198F10, 0x1B8AB0, 0x12E0B0, 0x1181B0}


def fx_cases(cases):
    out = []
    for c in cases:
        if c['fn'] not in FX_FNS or c.get('trap'):
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
    assert N.em_area00_low_00119080(None, U32(1), U32(2), U32(3), U32(4), C.byref(out)) == -1
    n += 1
    ctx = C.byref(nat.ctx)
    assert N.em_area00_low_00119080(ctx, U32(1), U32(2), U32(3), U32(4), None) == -1 and nat.ctx.fault == 1
    assert nat.ctx.fault_function == 0x119080
    n += 1
    assert N.em_area00_low_00119080(ctx, U32(1), U32(2), U32(3), U32(4), C.byref(out)) == -1     # latched
    n += 1
    N.em_area00_low_clear_fault(ctx)
    N.em_area00_low_clear_fault(None)
    assert nat.ctx.fault == 0 and nat.ctx.fault_function == 0 and nat.ctx.fault_address == 0
    assert N.em_area00_low_00119080(ctx, U32(10), U32(20), U32(5), U32(5), C.byref(out)) == 0 and out.value == 20
    n += 1
    # the divide-by-zero trap
    assert N.em_area00_low_00119080(ctx, U32(10), U32(20), U32(0x100), U32(5), C.byref(out)) == -1
    assert nat.ctx.fault == 4 and nat.ctx.fault_address == 0x119080 and nat.ctx.fault_function == 0x119080
    n += 1
    N.em_area00_low_clear_fault(ctx)
    # an unmapped record: the first access faults with its address
    assert N.em_area00_low_0012E070(ctx, U32(0x3000000)) == -1
    assert nat.ctx.fault == 3 and nat.ctx.fault_address == 0x30000F6 and nat.ctx.fault_function == 0x12E070
    n += 1
    N.em_area00_low_clear_fault(ctx)
    # a NULL grab_bits view reached: fault NULL, address 0
    nat.ctx.grab_bits = P(U8)()
    assert N.em_area00_low_0012E070(ctx, U32(NODES[0] + 0x1F0)) == 0      # +0xF6 bit 7 clear: not reached
    b = NODES[0] + 0x1F0
    nat.ram[b + 0xF6] = 0x81
    assert N.em_area00_low_0012E070(ctx, U32(b)) == -1
    assert nat.ctx.fault == 1 and nat.ctx.fault_address == 0 and nat.ctx.fault_function == 0x12E070
    n += 2
    N.em_area00_low_clear_fault(ctx)
    # a NULL worker: fault at the first call, naming the callee
    nat.ctx.call = WORKER()
    assert N.em_area00_low_001FF030(ctx, U32(5)) == -1
    assert nat.ctx.fault == 1 and nat.ctx.fault_address == 0x1FF080
    n += 1
    N.em_area00_low_clear_fault(ctx)
    # a worker returning < 0
    bad = WORKER(lambda _c, _p: -1)
    nat.ctx.call = bad
    assert N.em_area00_low_0012B410(ctx, U32(NODES[0]), U32(NODES[0] + 0x1F0)) == -1
    assert nat.ctx.fault == 2 and nat.ctx.fault_address == 0x1C2770 and nat.ctx.fault_function == 0x12B410
    n += 1
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
    for fn, target in TAIL.items():
        assert target in CALLEES, ('tail target without a policy', hex(target))
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
    rng = random.Random(0xA00C)
    cases = all_cases(rng)
    names = [c['name'] for c in cases]
    dup = {n for n in names if names.count(n) > 1}
    assert not dup, ('duplicate case names', sorted(dup)[:5])
    only = os.environ.get('EM_AREA00_LOW_ONLY')
    if only:
        cases = [c for c in cases if c['name'].startswith(tuple(only.split(',')))]
    total = len(cases)
    if not (RM.FULL or only):
        cases = RM.select(cases, QUICK_CASES, 0xA00C, axes=(lambda c: c['fn'],),
                          keep=lambda i, c: c['name'].startswith(('cap', 'k ')) or c['name'] in QUICK_PINNED)
    fxs = fx_cases(cases)
    if not (RM.FULL or only):
        fxs = RM.select(fxs, 60, 0xA00C, axes=(lambda c: (c['fn'], c.get('fx_before', False)),))
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
    if not (RM.FULL or only):
        sites = RM.select(sites, 40, 0xA00C)
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
    if os.environ.get('EM_AREA00_LOW_GAPS'):
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
              f'{len(sites)} store-site variants ({nsites} store sites, {ncovered} covered by the cases)',
              f'{calls} worker calls compared at entry and after the last store',
              f'{len(CALLEES)} callee policies checked against measured register reads',
              f'{api} fail-stop API checks')
    print(f'test_area00_low_reference: OK ({time.time() - t0:.1f} s)')


if __name__ == '__main__':
    main()
