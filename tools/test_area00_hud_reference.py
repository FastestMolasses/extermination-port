#!/usr/bin/env python3
"""Execute the original AREA00 lane-A00HUD routines and compare em_area00_hud.c.

docs/AREA00_HUD.md. The user's pinned ELF and the captured AREA00 RAM
(../Extermination/build/s87/route_a00/<beat>/, end-of-beat images) supply
every instruction and every table; none are embedded here.

The oracle is FallEE (the measured COP1 / VU0 model) extended here with the
VU0 forms the shared core lacks (VMAXbc / VMINIbc, VFTOI4, the clip test and
the CLIP register read), running the ORIGINAL routine over a copy of the
capture. The native module runs over another copy of the same bytes. Every
call leaving the translated set is caught on both sides and handled by one
Python policy:
  run   the original callee runs (vector / matrix leaves, the math leaves,
        the LCG, float_to_int, the packet allocators, 001CD370 / 001CD390):
        in the oracle inside the same interpreter, on the native side in a
        second interpreter bound to the native module's own memory and VU0
        state;
  stub  the callee is recorded and returns a scripted value (in the 'fx'
        cases it also changes memory, identically on both sides).
VU0 registers are not callee-saved: a run callee's VU0 writes persist on
both sides (the native worker loads the module's VU0 state into its
interpreter and writes it back).

Lockstep (the lane-SYS / EXITB design, docs/AREA01_SYS.md section 4). The
original runs first and records, at every call leaving the set, the callee,
every RAM / scratchpad line it stored to since the previous call, the VU0
state and the bytes behind every argument pointing into the stack. At the
same call the native side must match the callee, the stack pointer, every
argument register the callee reads (64-bit images), the float argument
registers, every register the native says it sets, all of RAM and the
scratchpad, the VU0 state (vf1..vf31 except vf3.z / w, ACC, Q, CLIP) and the
stack bytes. The same checks run after the last store, then all 32 MiB, the
scratchpad, the VU0 state and the result.

Variants (as lane EXITB): fx after / fx before, poison, prestore, edge-byte,
edge-top. RegScan (imported from test_area01_sys_reference) checks every
callee's register reads against its policy. EM_TEST_FULL=1 runs every case
and variant and asserts both outcomes of every conditional branch (except
UNREACHABLE, with proof); the default run is QUICK (the mutation sweep's
killers plus a branch cover).

The reuse check: 001E8E80 / 001E9280 are lane EXITB's translations
(em_area01_exitb.c); this test also runs that module's own oracle cases for
001E9280 over the AREA00 captures (the 0015AB00 records AREA00 keeps).

EM_AREA00_HUD_SOURCE=<file> tests another copy of the module source
(mutation runs); EM_AREA00_HUD_ONLY=<hex,...> runs only those entries;
EM_AREA00_HUD_FAILFAST=1 runs serially and stops at the first difference.
EM_TEST_JOBS sets the worker processes (default 2).
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
import ee_float_model as FM  # noqa: E402
import test_player_slide_reference as shared  # noqa: E402
from test_player_slide_reference import read_elf, sx32  # noqa: E402
from test_player_fall_reference import FallEE  # noqa: E402

MASK, MASK64 = 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF
DECOMP = ROOT.parent / 'Extermination'
ROUTE = DECOMP / 'build/s87/route_a00'
B02, B06, B09, B10 = 'a00_02_south_route', 'a00_06_cab_roof', 'a00_09_ne_room_out', 'a00_10_progression_exit'
OUT = ROOT / 'build' / 'area00' / 'hud'
STACK_LO, STACK_HI = 0x7F000000, 0x7F100000
SP = shared.STACK_TOP
SCRATCH = 0x01E00000            # zero in every a00 capture (checked); test records live here
POOL_HEAD = 0x275BC0
CODE_END = 0x230000
CTX_PTR, WORK_PTR, C30_PTR = 0x275670, 0x275C34, 0x275C30

FUNCS = {
    0x1DEDB0: 0x28, 0x1DEE80: 0x34, 0x1DEEC0: 0x20, 0x1DF020: 0xE4, 0x1DF110: 0x64, 0x1DF180: 0x41C,
    0x1DF5A0: 0x60, 0x1E2800: 0x3A0, 0x1E2BA0: 0x2D8, 0x1E2E80: 0x798, 0x1EAB50: 0x19C, 0x1EB980: 0x9C,
    0x1EBBB0: 0x7C, 0x1EBC30: 0xE4, 0x1ECB00: 0x368, 0x1ECFB0: 0x14C, 0x1ED7A0: 0x34C, 0x1EEBA0: 0x30C,
    0x1EEEB0: 0x30C,
}
FUNC_RANGES = tuple((a, a + n) for a, n in FUNCS.items())
HANDLERS = (0x1EAB50, 0x1EB980, 0x1EBBB0, 0x1EBC30, 0x1ECB00, 0x1ECFB0, 0x1ED7A0, 0x1EEBA0, 0x1EEEB0)

# Callee policy: address -> (kind, integer argument registers compared (a0..),
# float argument registers compared (f12..)). Checked against RegScan.
R, S = 'run', 'stub'
CALLEES = {
    # vector / matrix leaves
    0x102948: (R, 2, 0), 0x102958: (R, 2, 0), 0x1026D0: (R, 3, 0), 0x102760: (R, 2, 0), 0x102870: (R, 2, 1),
    0x1028B8: (R, 3, 0), 0x1028D0: (R, 3, 0), 0x102900: (R, 2, 1), 0x102918: (R, 3, 0), 0x1029C0: (R, 1, 0),
    0x102BB0: (R, 2, 1), 0x1031E0: (R, 2, 0),
    # math leaves, the LCG, float -> int
    0x11DF78: (R, 0, 1), 0x11E2A8: (R, 0, 1), 0x11E748: (R, 0, 1), 0x122BB8: (R, 0, 0), 0x1281C0: (R, 0, 1),
    # the context's matrix lookups and the packet chain (pure RAM)
    0x1CD370: (R, 1, 0), 0x1CD390: (R, 2, 0), 0x1CB5F0: (R, 3, 0), 0x1CB6B0: (R, 4, 0), 0x1CB760: (R, 4, 0),
    0x1CB900: (R, 3, 0),
    # stubs
    0x19A570: (S, 4, 0), 0x19AA80: (S, 3, 0), 0x1AFC10: (S, 1, 0), 0x1CD520: (S, 5, 3), 0x1CFB50: (S, 3, 5),
    0x1CFBE0: (S, 5, 0), 0x1D04B0: (S, 3, 2), 0x1D1F20: (S, 1, 0), 0x1D1F80: (S, 3, 0), 0x1D1FF0: (S, 2, 0),
    0x1D2040: (S, 2, 0), 0x1D6B60: (S, 5, 0), 0x1D6BA0: (S, 6, 0), 0x1D6DD0: (S, 3, 0), 0x1D7000: (S, 2, 0),
    0x1D7080: (S, 2, 1), 0x1D7510: (S, 3, 0), 0x1D7A80: (S, 5, 0), 0x1EFD20: (S, 2, 0), 0x1EFEB0: (S, 2, 0),
    0x1F02C0: (S, 2, 1),
}
# Bytes compared behind a stack pointer argument (default 16).
STACK_ARG_BYTES = {(0x1D7A80, 2): 32, (0x1D7A80, 3): 32}

# Entry signatures: fn -> (argument kinds, output kind). 'p' an integer
# register (a0..t0), 'f' a raw float register (f12..).
ENTRIES = {
    0x1DEDB0: ('p', 'u'), 0x1DEE80: ('pp', 'u'), 0x1DEEC0: ('pp', 'u'), 0x1DF020: ('pp', 'u'),
    0x1DF110: ('p', None), 0x1DF180: ('pf', 'u'), 0x1DF5A0: ('f', None), 0x1E2800: ('ppppp', None),
    0x1E2BA0: ('ppp', None), 0x1E2E80: ('p', None),
}
for _h in HANDLERS:
    ENTRIES[_h] = ('pp', None)

REG_FILL, FREG_FILL = 0x01F0A000, 0x449A5000   # mapped RAM (zero) / a normal float
LINE = 64
RAM_LINES = 0x2000000 // LINE


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


class Unmeasured(AssertionError):
    pass


def clipw_flags(v):
    """The clip test of x, y, z against |w| on DAZ'd magnitude bits (as
    em_render_context.c and its oracle); None for an exponent-255 lane."""
    if any((x >> 23 & 0xFF) == 0xFF for x in v):
        return None
    daz = lambda x: x & 0x80000000 if (x >> 23 & 0xFF) == 0 else x
    w = daz(v[3]) & 0x7FFFFFFF
    f = 0
    for k in range(3):
        x = daz(v[k])
        if (x & 0x7FFFFFFF) > w:
            f |= (2 if x >> 31 else 1) << (2 * k)
    return f


# ======================================================================
# Interpreter
# ======================================================================

class HudEE(FallEE):
    """FallEE with: VMAXbc / VMINIbc, VFTOI4, the clip test and the CLIP
    register; branch-outcome recording inside the translated routines; the
    RAM / scratchpad lines stored to (`dirty`, `pre` = each line before its
    first store); and (when `rec` is set) every data load of the translated
    routines as (address, size, value, stub index, pc)."""

    def __init__(self, elf, ram=None, spad=None):
        super().__init__(elf, ram, spad)
        self.clip = 0
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

    def macro(self, word):
        op, fs, ft, fd = word & 63, word >> 11 & 31, word >> 16 & 31, word >> 6 & 31
        mask = word >> 21 & 15
        lanes = [i for i in range(4) if mask & (8 >> i)]
        if 16 <= op < 24:                                                  # vmaxbc / vminibc
            bc = op & 3
            fn = FM.vu_max if op < 20 else FM.vu_min
            s, t = [x & MASK for x in self.vf[fs]], [x & MASK for x in self.vf[ft]]
            if fd:
                for i in lanes:
                    self.vf[fd][i] = fn(s[i], t[bc])
            return
        if op >= 60:
            special = fd << 2 | (op & 3)
            if special == 0x15:                                            # vftoi4
                s = [x & MASK for x in self.vf[fs]]
                if ft:
                    for i in lanes:
                        self.vf[ft][i] = FM.vu_ftoi(s[i], 4)
                return
            if special == 0x1F:                                            # clip test x/y/z vs |w|
                assert mask == 0xE, ('clip mask', hex(word))
                v = [x & MASK for x in self.vf[fs][:3]] + [self.vf[ft][3] & MASK]
                f = clipw_flags(v)
                if f is None:
                    raise Unmeasured(('clip lane with exponent 255', [hex(x) for x in v]))
                self.clip = ((self.clip << 6) | f) & 0xFFFFFF
                return
        super().macro(word)

    def cop2(self, word, pc):
        rs, rt, rd = word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        if rs in (2, 6):                                                   # CLIP register read / write
            assert rd == 18, ('COP2 control register', rd, hex(pc))
            if rs == 2:
                if rt:
                    self.r[rt] = sx32(self.clip) & MASK64
            else:
                self.clip = self.r[rt] & 0xFFFFFF
            return
        super().cop2(word, pc)

    def _where(self, address):
        a = address & MASK
        if 0x2000000 <= a < 0x40000000:
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

    # VU0 state as the module's EmArea00HudVu holds it
    def vu_state(self):
        return (tuple(tuple(x & MASK for x in self.vf[i]) for i in range(32)), tuple(x & MASK for x in self.vacc),
                self.q & MASK, self.clip & 0xFFFFFF)

    def set_vu(self, st):
        vf, acc, q, clip = st
        self.vf = [list(v) for v in vf]
        self.vf[0] = [0, 0, 0, 0x3F800000]
        self.vacc = list(acc)
        self.q = q
        self.clip = clip


def vu_view(st):
    """The compared VU0 state: vf1..vf31 (vf3 without z / w: the original
    fills them from the upper half of a 128-bit GPR the port does not model),
    ACC, Q, CLIP."""
    vf, acc, q, clip = st
    regs = tuple(vf[i] if i != 3 else vf[i][:2] for i in range(1, 32))
    return regs, acc, q, clip


def run_nested(ee, fn):
    """Run the original callee `fn` (and all it calls, unhooked) with the
    argument registers as they are. The integer and float registers except
    v0 / v1 / f0 are restored afterwards (the caller's code never depends on
    them); the VU0 state is NOT restored (VU0 registers are not callee-saved,
    and the translated routines keep vf23..vf31 across calls)."""
    saved = (list(ee.r), list(ee.rh), ee.hi, ee.lo, list(ee.f), ee.acc, ee.cond)
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
    (ee.r, ee.rh, ee.hi, ee.lo, ee.f, ee.acc, ee.cond) = saved
    ee.r[2], ee.r[3], ee.f[0] = v0, v1, f0
    return v0, f0


def stub_reg(v):
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


def policy(fn):
    if fn in CALLEES:
        return CALLEES[fn]
    raise AssertionError(('call without a policy', hex(fn)))


def log_entry(fn, pol, regs, fregs, sp):
    kind, na, nf = pol
    return (hex(fn), hex(sp & MASK), tuple(regs[i] & MASK64 for i in range(na)),
            tuple(fregs[i] & MASK for i in range(nf)))


def stack_args(fn, pol, regs, read):
    """(address, bytes) behind every compared argument that points into the
    stack region."""
    out = []
    for i in range(pol[1]):
        a = regs[i] & MASK
        if STACK_LO <= a < STACK_HI - 64:
            n = STACK_ARG_BYTES.get((fn, i), 16)
            out.append((a, bytes(read(a, n))))
    return tuple(out)


# ======================================================================
# Native side
# ======================================================================

P, U8, U32, I32, U64 = C.POINTER, C.c_uint8, C.c_uint32, C.c_int32, C.c_uint64


class Region(C.Structure):
    _fields_ = [('base', U32), ('size', U32), ('bytes', P(U8))]


class Vu(C.Structure):
    _fields_ = [('vf', (U32 * 4) * 32), ('acc', U32 * 4), ('q', U32), ('clip', U32)]


class Call(C.Structure):
    _fields_ = [('fn', U32), ('sp', U32), ('a', U64 * 8), ('f', U32 * 8), ('na', U32), ('nf', U32),
                ('vu', P(Vu)), ('v0', U64), ('f0', U32)]


WORKER = C.CFUNCTYPE(C.c_int, C.c_void_p, P(Call))


class Hud(C.Structure):
    _fields_ = [('regions', P(Region)), ('region_count', C.c_uint), ('call', WORKER), ('ctx', C.c_void_p),
                ('sp', U32), ('fault', I32), ('fault_function', U32), ('fault_address', U32), ('vu', Vu)]


def vu_from_native(v):
    return (tuple(tuple(v.vf[i][k] for k in range(4)) for i in range(32)), tuple(v.acc[k] for k in range(4)),
            v.q, v.clip)


def vu_to_native(v, st):
    vf, acc, q, clip = st
    for i in range(32):
        for k in range(4):
            v.vf[i][k] = vf[i][k]
    for k in range(4):
        v.acc[k] = acc[k]
    v.q, v.clip = q, clip


TRACE_C = r"""/* Store trace for the native module (test build only). */
#include <stdint.h>
#define RAM_LINES (0x2000000u / 64u)
#define LINES (RAM_LINES + 0x4000u / 64u)
static uint8_t dirty[LINES];
uint32_t a00hud_trace_list[LINES];
uint32_t a00hud_trace_count;
void a00hud_trace_store(uint32_t address, unsigned size);
void a00hud_trace_reset(void);
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
        a00hud_trace_list[a00hud_trace_count++] = id;
    }
}
void a00hud_trace_store(uint32_t address, unsigned size)
{
    mark(address);
    mark(address + size - 1u);
}
void a00hud_trace_reset(void)
{
    uint32_t i;
    for (i = 0; i < a00hud_trace_count; i++)
        dirty[a00hud_trace_list[i]] = 0;
    a00hud_trace_count = 0;
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
    source = os.environ.get('EM_AREA00_HUD_SOURCE', '')
    ext = 'dylib' if sys.platform == 'darwin' else 'so'
    if source:
        source = str(Path(source).resolve())
        lib = OUT / f'area00_hud_{Path(source).stem}.{ext}'
        rebuild = not lib.exists() or Path(source).stat().st_mtime > lib.stat().st_mtime
    else:
        source = 'src/game/em_area00_hud.c'
        lib = OUT / f'area00_hud.{ext}'
        deps = [ROOT / source, ROOT / 'src/game/em_area00_hud.h', ROOT / 'src/game/em_ee_float.h', trace,
                Path(__file__)]
        rebuild = not lib.exists() or max(d.stat().st_mtime for d in deps) > lib.stat().st_mtime
    if rebuild:
        werror = [] if os.environ.get('EM_AREA00_HUD_SOURCE') else ['-Werror']
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', *werror, '-Wpedantic',
                        '-ffp-contract=off', '-shared', '-fPIC', '-Isrc', '-Isrc/game',
                        '-DEM_AREA00_HUD_STORE_TRACE=a00hud_trace_store', source, str(trace),
                        '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    TRACE['count'] = U32.in_dll(native, 'a00hud_trace_count')
    TRACE['list'] = (U32 * (RAM_LINES + 0x4000 // LINE)).in_dll(native, 'a00hud_trace_list')
    TRACE['reset'] = native.a00hud_trace_reset
    for fn, (kinds, outk) in ENTRIES.items():
        f = getattr(native, f'em_area00_hud_{fn:08X}')
        f.restype = C.c_int
        f.argtypes = [P(Hud)] + [U32] * len(kinds) + ([P(U32)] if outk else [])
    native.em_area00_hud_clear_fault.argtypes = [P(Hud)]
    return native


def native_lines():
    n = TRACE['count'].value
    if not n:
        return ()
    out = TRACE['list'][:n]
    TRACE['reset']()
    return out


class Lockstep:
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


def fresh_stack():
    return bytearray(b'\xA5' * (STACK_HI - STACK_LO))


class NativeRun:
    """One native call over copies of `ram` / `spad` and a fresh stack."""

    def __init__(self, ram, spad, script, vu):
        self.ram = (U8 * len(ram)).from_buffer_copy(ram)
        self.spad = (U8 * len(spad)).from_buffer_copy(spad)
        self.stack = (U8 * (STACK_HI - STACK_LO)).from_buffer_copy(fresh_stack())
        self.regions = (Region * 3)(Region(0, len(ram), C.cast(self.ram, P(U8))),
                                    Region(0x70000000, len(spad), C.cast(self.spad, P(U8))),
                                    Region(STACK_LO, STACK_HI - STACK_LO, C.cast(self.stack, P(U8))))
        self.ee = HudEE(ELF, b'', b'')
        self.ee.mem = memoryview(self.ram).cast('B')
        self.ee.spad = memoryview(self.spad).cast('B')
        self.ee.stack = memoryview(self.stack).cast('B')
        self.script, self.log, self.error = script, [], None
        self.lock = None
        self.fx = {}
        self.stubs = 0
        self.worker = WORKER(self._call)
        self.sys = Hud(self.regions, 3, self.worker, None, SP, 0, 0, 0)
        vu_to_native(self.sys.vu, vu)

    def dirty(self):
        out = set(native_lines())
        out |= self.ee.dirty
        self.ee.dirty = set()
        return out

    def _call(self, _, pc):
        try:
            c = pc.contents
            pol = policy(c.fn)
            kind, na, nf = pol
            assert na <= c.na <= 8 and nf <= c.nf <= 8, ('native register counts', hex(c.fn), c.na, c.nf)
            k = len(self.log)
            regs = [c.a[i] for i in range(8)]
            entry = log_entry(c.fn, pol, regs, list(c.f), c.sp)
            self.log.append(entry)
            if self.lock is not None:
                assert k < len(self.lock.entries), ('native makes more calls than the original', k, entry)
                want, delta, oregs, ofregs, ovu, ostack = self.lock.entries[k]
                self.lock.check(('call', k, hex(c.fn)), delta, self.dirty(), self.ee.mem, self.ee.spad)
                assert entry == want, ('callee entry differs at call', k, 'native', entry, 'original', want)
                got = (tuple(c.a[i] & MASK64 for i in range(c.na)), tuple(c.f[i] & MASK for i in range(c.nf)))
                assert got == (oregs[:c.na], ofregs[:c.nf]), ('registers the native says it sets differ at call',
                                                             k, hex(c.fn), got, (oregs[:c.na], ofregs[:c.nf]))
                nvu = vu_view(vu_from_native(c.vu.contents))
                if nvu != vu_view(ovu):
                    raise AssertionError(('VU0 state differs at call', k, hex(c.fn), vu_diff(nvu, vu_view(ovu))))
                nstack = stack_args(c.fn, pol, regs, lambda a, n: self.ee.read(a, n))
                assert nstack == ostack, ('stack argument bytes differ at call', k, hex(c.fn),
                                          [(hex(a), b.hex()) for a, b in nstack], [(hex(a), b.hex()) for a, b in ostack])
            if kind == R:
                ee = self.ee
                for i in range(8):
                    ee.r[4 + i] = c.a[i] & MASK64
                for i in range(8):
                    ee.f[12 + i] = c.f[i] & MASK
                ee.r[29] = c.sp
                ee.set_vu(vu_from_native(c.vu.contents))
                v0, f0 = run_nested(ee, c.fn)
                vu_to_native(c.vu.contents, ee.vu_state())
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


def vu_diff(a, b):
    ra, aa, qa, ca = a
    rb, ab, qb, cb = b
    out = []
    for i, (x, y) in enumerate(zip(ra, rb)):
        if x != y:
            out.append((f'vf{i + 1}', [hex(v) for v in x], [hex(v) for v in y]))
    if aa != ab:
        out.append(('acc', [hex(v) for v in aa], [hex(v) for v in ab]))
    if qa != qb:
        out.append(('q', hex(qa), hex(qb)))
    if ca != cb:
        out.append(('clip', hex(ca), hex(cb)))
    return out[:4]


def native_call(fn, sysp, args, fargs):
    kinds, outk = ENTRIES[fn]
    vals, ai, fi = [], 0, 0
    for k in kinds:
        if k == 'f':
            vals.append(fargs[fi] & MASK)
            fi += 1
        else:
            vals.append(args[ai] & MASK)
            ai += 1
    out = U32(0x5A5A5A5A) if outk else None
    rc = getattr(NATIVE, f'em_area00_hud_{fn:08X}')(sysp, *vals, *([C.byref(out)] if outk else []))
    return rc, (out.value if out is not None else None)


# ======================================================================
# Oracle side
# ======================================================================

def initial_vu(case):
    """The starting VU0 state: a fixed pattern of normal floats, with the
    case's own registers (vf23, vf28..vf31 for direct 001E2800 cases) and
    CLIP."""
    rng = random.Random(0xA00 + len(case['name']))
    vf = [[0, 0, 0, 0x3F800000]] + [[FM_bits(rng.uniform(-4, 4)) for _ in range(4)] for _ in range(31)]
    for i, v in case.get('vu', {}).items():
        vf[i] = list(v)
    acc = [FM_bits(rng.uniform(-4, 4)) for _ in range(4)]
    return (tuple(tuple(v) for v in vf), tuple(acc), FM_bits(rng.uniform(-4, 4)), case.get('clip', 0xABCDE3))


def FM_bits(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def oracle_run(case, ram, spad, on_stub=None, rec=False, stored=None):
    """The original over `ram` / `spad` (used in place). Returns (ee, log,
    v0, entries, final delta)."""
    ee = HudEE(ELF, b'', b'')
    ee.mem, ee.spad = ram, spad
    ee.stack = fresh_stack()
    ee.pre = {}
    ee.rec = [] if rec else None
    ee.stored = stored
    script, log, entries = Script(case['script']), [], []
    stubs = [0]

    def make(fn):
        pol = policy(fn)

        def hook(e):
            regs = [e.r[4 + i] for i in range(8)]
            fregs = [e.f[12 + i] for i in range(8)]
            entry = log_entry(fn, pol, regs, fregs, e.r[29])
            log.append(entry)
            entries.append((entry, e.take(), tuple(r & MASK64 for r in regs), tuple(x & MASK for x in fregs),
                            e.vu_state(), stack_args(fn, pol, regs, e.read)))
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
    ee.set_vu(initial_vu(case))
    kinds = ENTRIES[case['fn']][0]
    ai = fi = 0
    for k in kinds:
        if k == 'f':
            ee.f[12 + fi] = case['fargs'][fi] & MASK
            fi += 1
        else:
            ee.r[4 + ai] = sx32(case['args'][ai]) & MASK64
            ai += 1
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


# ---- stub side effects (lane EXITB's variants) -------------------------

def is_code_word(size, v):
    return size == 4 and 0x100000 <= v < CODE_END


def poison_writes(case):
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
    def __init__(self, plan):
        self.plan, self.log = plan, {}

    def __call__(self, ee, k, fn):
        for a, data in self.plan.get(k, ()):
            for i, b in enumerate(data):
                ee.save(a + i, b, 1)
            self.log.setdefault(k, []).append((a, data))


# ---- one case ----------------------------------------------------------

def run_case(case):
    """Returns (branch outcomes, calls, fx field changes) or None when the
    variant is not comparable. Raises on any difference."""
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
    vu0 = initial_vu(case)
    nat = NativeRun(ram, spad, Script(case['script']), vu0)
    native_lines()
    where = (case['name'],)
    try:
        ee, olog, ov0, entries, last = oracle_run(case, ram, spad, on_stub=fx)
    except Unmeasured:
        if case.get('fx') or case.get('edge') or case.get('poison') or case.get('prestore'):
            return None
        raise
    except AssertionError as e:
        if (case.get('fx') or case.get('edge') or case.get('poison') or case.get('prestore')) and e.args \
                and isinstance(e.args[0], tuple) \
                and e.args[0][:1] == ('address',):
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
    if ENTRIES[case['fn']][1]:
        assert nv0 == ov0 & MASK, (where, 'result', hex(nv0), hex(ov0))
    assert olog == nat.log, (where, 'call log differs')
    nvu, ovu = vu_view(vu_from_native(nat.sys.vu)), vu_view(ee.vu_state())
    assert nvu == ovu, (where, 'final VU0 state differs', vu_diff(nvu, ovu))
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
    while a and a not in seen and len(out) < 0x400:
        seen.add(a)
        out.append((a, u32(ram, a + 0x10)))
        a = u32(ram, a + 0x1C)
    return out


def owners(fn, beat):
    return [a for a, cb in pool(image(beat)[0]) if cb == fn]


def W8(a, v): return (a, struct.pack('<B', v & 0xFF))
def W16(a, v): return (a, struct.pack('<H', v & 0xFFFF))
def W32(a, v): return (a, struct.pack('<I', v & MASK))
def WF(a, v): return (a, struct.pack('<f', v))
def FB(v): return struct.unpack('<I', struct.pack('<f', v))[0]


def case(name, fn, args, script=None, writes=(), fargs=(), beat=B09, pin=False, vu=None, clip=None):
    c = dict(name=name, fn=fn, beat=beat, args=list(args), script=script or {}, writes=list(writes),
             fargs=list(fargs), pin=pin)
    if vu:
        c['vu'] = vu
    if clip is not None:
        c['clip'] = clip
    return c


def vwrites(a, v):
    return [WF(a + 4 * i, x) for i, x in enumerate(v)]


def seed_write(beat, seed):
    return W32(u32(image(beat)[0], 0x24295C) + 0x58, seed)


def quad(ram, a):
    return tuple(struct.unpack_from('<4I', ram, a & 0x1FFFFF0))


def cases_context(rng):
    """001DEDB0 / 001DEE80 / 001DEEC0 / 001DF020 / 001DF110 / 001DF180 /
    001DF5A0 over the a00_09 context (where the census first saw them)."""
    out = []
    ram = image(B09)[0]
    ctx = u32(ram, CTX_PTR)
    for a0 in (9, 2, 0, 8, 10, 0x109, 0xFFFFFFF9):
        out.append(case(f'1DEDB0 a{a0:x}', 0x1DEDB0, [a0], pin=a0 in (9, 2)))
    for a0 in (2, 9):
        w = [W32(SCRATCH + 4 * i, rng.getrandbits(32)) for i in range(4)]
        out.append(case(f'1DEE80 a{a0}', 0x1DEE80, [a0, SCRATCH], writes=w, pin=True))
        rec = ctx + (0x2490 if a0 == 9 else 0x2470)
        for shift in (4, 8, -4):          # the source overlaps the destination words
            out.append(case(f'1DEE80 a{a0} alias{shift}', 0x1DEE80, [a0, rec + 0x10 + shift], pin=shift == 4))
    for a0 in (2, 9, 5):
        for a1 in (0, 0x60, 0xFFFFFFFF, 0x12345678):
            out.append(case(f'1DEEC0 a{a0} {a1:x}', 0x1DEEC0, [a0, a1], pin=a1 == 0x60))
    for a0 in (3, 0, 1, 7):
        for a1 in (0, 5, 0xFFFFFFFE):
            out.append(case(f'1DF020 a{a0} {a1:x}', 0x1DF020, [a0, a1], pin=a0 == 3 and a1 == 5))
    for a0 in (0, 7, 0xFFFFFFFF):
        out.append(case(f'1DF110 {a0:x}', 0x1DF110, [a0], script={0x1D7A80: [a0 & 3]}, pin=a0 == 7))
    for n, (a0, h) in enumerate(((3, 0.05), (1, 0.0), (3, 1.0), (3, -0.3), (2, 3.0e10))):
        out.append(case(f'1DF180 {n}', 0x1DF180, [a0], fargs=[FB(h)], writes=[seed_write(B09, rng.getrandbits(32))],
                        pin=n == 0))
    for n, h in enumerate((0.05, 0.25)):
        out.append(case(f'1DF5A0 {n}', 0x1DF5A0, [], fargs=[FB(h)], writes=[seed_write(B09, rng.getrandbits(32))],
                        pin=n == 0))
    return out


def point_writes(a, p):
    return vwrites(a, p)


def cases_line(rng):
    """001E2800 directly (VU0 registers from the capture or designed) and
    001E2BA0 over designed and captured matrices."""
    out = []
    ram, spad = image(B10)
    ctx = u32(ram, CTX_PTR)
    cap = {28 + k: struct.unpack_from('<4I', spad, 0x3AC0 + 16 * k) for k in range(4)}
    cap[23] = quad(ram, ctx + 0xA0)
    # A designed projection: x' = x, y' = y, z' = z, w' = z + 3 (points with
    # z > -2 give w - 1 > 0); the ramp vf23 = (ceiling, 0, bias, slope).
    ident = {28: (FB(1), 0, 0, 0), 29: (0, FB(1), 0, 0), 30: (0, 0, FB(1), FB(1)), 31: (0, 0, 0, FB(3.0))}
    ramps = [(FB(255.0), 0, FB(40.0), FB(-10.0)), (FB(1000.0), 0, FB(300.0), FB(10.0)),
             (FB(-5.0), 0, FB(10.0), FB(1.0)), (FB(128.0), 0, FB(0.0), FB(64.0))]
    P0, C0, P1, C1 = SCRATCH, SCRATCH + 0x10, SCRATCH + 0x20, SCRATCH + 0x30
    for n in range(10):
        ramp = ramps[n % 4]
        vu = dict(ident)
        vu[23] = ramp
        w = point_writes(P0, [rng.uniform(-2, 2), rng.uniform(-2, 2), rng.uniform(-1.5, 3), 1.0])
        w += point_writes(P1, [rng.uniform(-2, 2), rng.uniform(-2, 2), rng.uniform(-1.5, 3), 1.0])
        w += [W32(C0 + 4 * i, rng.choice((0, 0x80, 0xFF, 0x7FFFFFFF, rng.getrandbits(32)))) for i in range(4)]
        w += [W32(C1 + 4 * i, rng.choice((0, 0x40, 0xFF, rng.getrandbits(32)))) for i in range(4)]
        for mode in (0, 1, 2, 3, 4):
            if n >= 4 and mode in (0, 4):
                continue
            out.append(case(f'1E2800 d{n} m{mode}', 0x1E2800, [mode, P0, C0, P1, C1], writes=w, beat=B10, vu=vu,
                            pin=n < 4 and mode in (1, 2, 3) or (n == 0 and mode in (0, 4))))
    # unaligned pointers (the quadword accesses clear the low four bits)
    w = point_writes(P0, [0.5, -0.25, 0.75, 1.0]) + point_writes(P1, [-1.0, 1.0, 0.0, 1.0])
    w += [W32(C0 + 4 * i, 0x10 * (i + 1)) for i in range(4)] + [W32(C1 + 4 * i, 0x30 + i) for i in range(4)]
    vu = dict(ident)
    vu[23] = ramps[1]
    out.append(case('1E2800 unaligned', 0x1E2800, [2, P0 + 4, C0 + 8, P1 + 12, C1 + 1], writes=w, beat=B10, vu=vu,
                    pin=True))
    # the captured camera and ramp, points around the captured camera matrix
    for n in range(4):
        w = point_writes(P0, [rng.uniform(-3000, 3000), rng.uniform(-500, 500), rng.uniform(-3000, 3000), 1.0])
        w += point_writes(P1, [rng.uniform(-3000, 3000), rng.uniform(-500, 500), rng.uniform(-3000, 3000), 1.0])
        w += [W32(C0 + 4 * i, rng.getrandbits(8)) for i in range(4)] + [W32(C1 + 4 * i, rng.getrandbits(8))
                                                                           for i in range(4)]
        out.append(case(f'1E2800 captured {n}', 0x1E2800, [2, P0, C0, P1, C1], writes=w, beat=B10, vu=cap,
                        pin=n == 0))
    # 001E2BA0: designed clip matrix (context +0x2240 + 2 * 0x40) and
    # projection (0x70003AC0), the fog row at context +0xA0
    clipm = ctx + 0x2240 + 0x80
    for n in range(8):
        w = []
        for row in range(4):
            vals = [1.0 if i == row else 0.0 for i in range(4)]
            if n % 2:
                vals = [v + rng.uniform(-0.2, 0.2) for v in vals]
            w += vwrites(clipm + 16 * row, vals)
        prj = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 1], [0, 0, 0, 3.0]]
        for row in range(4):
            w += vwrites(0x70003AC0 + 16 * row, prj[row])
        w += vwrites(ctx + 0xA0, [[255.0, 1000.0, -5.0, 128.0][n % 4], 0.0, [40.0, 300.0, 10.0, 0.0][n % 4],
                                  [-10.0, 10.0, 1.0, 64.0][n % 4]])
        span = [0.8, 1.6, 3.0, 400.0][n % 4]
        a = [rng.uniform(-span, span) for _ in range(3)] + [1.0]
        b = [rng.uniform(-span, span) for _ in range(3)] + [1.0]
        w += vwrites(SCRATCH + 0x100, a) + vwrites(SCRATCH + 0x110, b)
        w += vwrites(SCRATCH + 0x120, [rng.uniform(0, 1) for _ in range(3)] + [rng.uniform(0, 1)])
        w.append(seed_write(B10, rng.getrandbits(32)))
        out.append(case(f'1E2BA0 d{n}', 0x1E2BA0, [SCRATCH + 0x100, SCRATCH + 0x110, SCRATCH + 0x120], writes=w,
                        beat=B10, pin=n in (0, 3)))
    for n in range(3):
        a = [rng.uniform(-3000, 3000), rng.uniform(-200, 200), rng.uniform(-3000, 3000), 1.0]
        b = [x + rng.uniform(-300, 300) for x in a[:3]] + [1.0]
        w = vwrites(SCRATCH + 0x100, a) + vwrites(SCRATCH + 0x110, b)
        w += vwrites(SCRATCH + 0x120, [1.0, 0.25, 0.0, 0.5]) + [seed_write(B10, rng.getrandbits(32))]
        out.append(case(f'1E2BA0 captured {n}', 0x1E2BA0, [SCRATCH + 0x100, SCRATCH + 0x110, SCRATCH + 0x120],
                        writes=w, beat=B10, pin=n == 0))
    return out


def cases_hazard(rng):
    """001E2E80: the captured records (a00_02, a00_06), every state, both
    kinds, the damage tables, the probes and the timers."""
    out = []
    for beat in (B02, B06):
        for n, p in enumerate(owners(0x1E2E80, beat)):
            out.append(case(f'1E2E80 captured {beat[:6]} {n}', 0x1E2E80, [p], beat=beat, pin=True))
    p = owners(0x1E2E80, B02)[0]
    rec, tgt = SCRATCH + 0x800, SCRATCH + 0x400
    probe = [W32(0x700031D0, rec), W32(0x700031D4, tgt)]
    for st in (2, 3, 4, 0xFF):
        out.append(case(f'1E2E80 s{st}', 0x1E2E80, [p], beat=B02, writes=[W8(p + 4, st)], pin=True))
    for kind in (3, 4, 5):
        out.append(case(f'1E2E80 s0 k{kind}', 0x1E2E80, [p], beat=B02,
                        writes=[W8(p + 4, 0), W8(p + 0xD, kind), WF(p + 0x20, rng.uniform(0.5, 4)),
                                seed_write(B02, rng.getrandbits(32))], pin=True))
    # the damage path: 0019A570 misses, 0019AA80 hits a record whose byte 0 is 1
    for area in (0, 1):
        for kind in (3, 4, 5):
            for h in ((9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19) if kind == 3 else (0x12, 0x13, 0x14, 0x15)):
                if kind == 5 and h != 0x13:
                    continue
                w = probe + [W8(p + 4, 1), W8(p + 0xD, kind), W16(p + 0x94, h), W8(0x81070A, area),
                             W8(tgt, 1), W8(rec + 0x1A, 0)]
                out.append(case(f'1E2E80 dmg a{area} k{kind} h{h:x}', 0x1E2E80, [p], beat=B02, writes=w,
                                script={0x19AA80: [1]}, pin=h in (10, 18, 0x13, 0x14) and kind != 5 or
                                (kind == 3 and h == 9 and area == 0) or kind == 5))
    for h in (-1, 0x8000 + 10, 0x7FFF):
        w = probe + [W8(p + 4, 1), W8(p + 0xD, 3), W16(p + 0x94, h), W8(tgt, 1)]
        out.append(case(f'1E2E80 dmg h{h & 0xFFFF:x}', 0x1E2E80, [p], beat=B02, writes=w, script={0x19AA80: [1]}))
    for tb in (0, 2, 3):
        w = probe + [W8(p + 4, 1), W8(p + 0xD, 3), W16(p + 0x94, 12), W8(tgt, tb)]
        out.append(case(f'1E2E80 dmg target{tb}', 0x1E2E80, [p], beat=B02, writes=w, script={0x19AA80: [1]},
                        pin=tb == 3))
    # the first probe: hits, with the record byte +0x1A 0x32 (cleared) or not
    for b1a in (0x32, 0x31, 0x33):
        for v in (1, -1, 0x100000000):
            for second in (0, 1):
                w = probe + [W8(p + 4, 1), W8(rec + 0x1A, b1a), W8(tgt, 1), W8(p + 0xD, 4), W16(p + 0x94, 0x13)]
                out.append(case(f'1E2E80 probe {b1a:x} {v:x} {second}', 0x1E2E80, [p], beat=B02, writes=w,
                                script={0x19A570: [v], 0x19AA80: [second]},
                                pin=v == 1 and (b1a != 0x31 or second == 0)))
    # the timers: +0x34 across 26, +0x38 across 0, +0x20 across 2, e+5 0 / 1
    b = p + 0x1F0
    for c34 in (24, 25, 26):
        for c38 in (0, 1, 150):
            w = [W8(p + 4, 1), W8(p + 0xD, 4), W32(b + 0x34, c34), W32(b + 0x38, c38), WF(b + 0x30, 0.3)]
            out.append(case(f'1E2E80 t34 {c34} t38 {c38}', 0x1E2E80, [p], beat=B02, writes=w,
                            pin=c34 in (24, 25) and c38 in (0, 1)))
    for ph in (1.9, 1.95, 1.9500000477, 1.96, 2.5, -3.0):
        for e5 in (0, 1):
            w = [W8(p + 4, 1), W8(p + 0xD, 3), WF(b + 0x20, ph), W8(p + 5, e5)]
            out.append(case(f'1E2E80 phase {ph} e5 {e5}', 0x1E2E80, [p], beat=B02, writes=w,
                            pin=ph in (1.9, 1.96) and e5 == 0))
    return out


def cases_handlers(rng):
    """The nine effect-kind handlers with the a00_09 work block (written),
    a0 = a node's +0xD0 (a scratch record for 001EBC30's copy) and a depth
    key."""
    out = []
    ram = image(B09)[0]
    work = u32(ram, WORK_PTR)
    c30 = u32(ram, C30_PTR)
    node = SCRATCH + 0x200
    qw = vwrites(node, [rng.uniform(-50, 50) for _ in range(16)])
    xs = (0.0, 0.3, 0.49999997, 0.5, 0.9, 0.99999994, 1.0, 1.5, -0.2)
    eases = (0.0, 0.02, 0.05, 0.3, -1.0, 0.0199999996)
    for fn in HANDLERS:
        for n, x in enumerate(xs):
            if fn not in (0x1EAB50, 0x1ECB00) and n > 2:
                continue
            for e in (eases if n == 1 else (eases[n % len(eases)],)):
                w = qw + [W32(work + 4, rng.getrandbits(32)), WF(work + 8, e), WF(work + 0x54, x),
                          WF(work + 0x5C, rng.uniform(0, 1)), WF(0x70003434, rng.uniform(-3, 3))]
                if fn == 0x1EBC30:
                    w.append(W32(c30 + 0x38, n % 2))
                out.append(case(f'{fn:X} x{x} e{e}', fn, [node + 0x0, 0x1234 + n], writes=w,
                                pin=(n in (0, 1) and e in (0.0, 0.3)) or (fn in (0x1EAB50, 0x1ECB00) and n in (3, 6))))
        # seeds with the top bit set (the arithmetic shift) and the extremes
        for seed in (0x80000000, 0xFFFFFFFF, 0x7FFF0000, 0x0000FFFF):
            w = qw + [W32(work + 4, seed), WF(work + 0x54, 0.25)]
            out.append(case(f'{fn:X} seed {seed:x}', fn, [node, 7], writes=w, pin=seed == 0x80000000))
    return out


def cases_sweep1(rng):
    """Inputs added after the mutation sweep (docs/AREA00_HUD.md section 5),
    each for a survivor it kills: a fade depth of exactly 0x100 and one
    above 0xFFFF; 001E2BA0 clip lanes equal to |w| and one ulp above it
    (odd mantissas), a line that is outside only in z-minus and then enters
    the clip volume, a lane with exponent 254, a phase reaching exactly 2 pi;
    001DF110 with the cursor
    crossing a 64 KiB boundary; 001DF180 with D_0027568C's bit 31 set;
    001EBC30 with an exact 2.5 sum; a zero LCG fraction (the 0.0001 lift
    shows its last bit); the hazard phase reaching exactly 2.0; the kind-4
    halfword with a nonzero high byte and the +0x34 counter carrying into
    its upper half."""
    out = []
    ram, spad = image(B10)
    ctx = u32(ram, CTX_PTR)
    P0, C0, P1, C1 = SCRATCH, SCRATCH + 0x10, SCRATCH + 0x20, SCRATCH + 0x30
    ident = {28: (FB(1), 0, 0, 0), 29: (0, FB(1), 0, 0), 30: (0, 0, FB(1), FB(1)), 31: (0, 0, 0, FB(3.0))}
    w = point_writes(P0, [0.5, 0.25, 0.5, 1.0]) + point_writes(P1, [-0.5, 0.25, 1.0, 1.0])
    w += [W32(C0 + 4 * i, 0x80 + i) for i in range(4)] + [W32(C1 + 4 * i, 0xFF - i) for i in range(4)]
    for name, ramp in (('256', (FB(1000.0), 0, FB(256.0), 0)), ('big', (FB(1.0e7), 0, FB(2.0e6), FB(3.0)))):
        vu = dict(ident)
        vu[23] = ramp
        for mode in (1, 2):
            out.append(case(f'1E2800 depth {name} m{mode}', 0x1E2800, [mode, P0, C0, P1, C1], writes=w, beat=B10,
                            vu=vu, pin=True))
    clipm = ctx + 0x2240 + 0x80
    prj = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 1], [0, 0, 0, 3.0]]

    def line(name, a, b, wlane=None, col=(1.0, 0.5, 0.25, 1.0), seed=0x13579BDF):
        w = []
        for row in range(4):
            vals = [1.0 if i == row else 0.0 for i in range(4)]
            w += vwrites(clipm + 16 * row, vals)
        if wlane is not None:
            w.append(W32(clipm + 0x3C, wlane))
        for row in range(4):
            w += vwrites(0x70003AC0 + 16 * row, prj[row])
        w += vwrites(ctx + 0xA0, [255.0, 0.0, 40.0, -10.0])
        w += [W32(SCRATCH + 0x100 + 4 * i, v) for i, v in enumerate(a)]
        w += [W32(SCRATCH + 0x110 + 4 * i, v) for i, v in enumerate(b)]
        w += vwrites(SCRATCH + 0x120, list(col)) + [seed_write(B10, seed)]
        return case(f'1E2BA0 {name}', 0x1E2BA0, [SCRATCH + 0x100, SCRATCH + 0x110, SCRATCH + 0x120], writes=w,
                    beat=B10, pin=True)
    one, one1 = FB(1.0), 0x3F800001
    out.append(line('x above w by one ulp', [one1, 0, 0, one], [one1, 0, 0, one]))
    out.append(line('x equal to odd w', [one1, 0, 0, one], [one1, 0, 0, one], wlane=one1))
    out.append(line('z-minus then inside', [0, 0, FB(-1.5), one], [0, 0, FB(-0.25), one]))
    out.append(line('exponent 254 lane', [FB(2.0e38), 0, 0, one], [FB(2.5e38), 0, 0, one]))
    # the LCG seed whose draw is 0 (phase 0) and a step whose phase increment
    # is exactly 2 pi (0.1 * 0x437B53D2 / 4 in EE arithmetic): the phase
    # reaches 2 pi exactly and the "not below 2 pi" wrap is taken on equality
    out.append(line('phase exactly 2 pi', [0, 0, 0, one], [0x437B53D2, 0, 0, one], seed=0xFC77A683))
    ram9 = image(B09)[0]
    ctx9 = u32(ram9, CTX_PTR)
    out.append(case('1DF110 cursor carry', 0x1DF110, [4], writes=[W32(ctx9 + 0x1C, SCRATCH + 0xFFF8)], pin=True))
    out.append(case('1DF180 k8c bit 31', 0x1DF180, [3], fargs=[FB(0.05)], writes=[W32(0x27568C, 0x80258000)],
                    pin=True))
    work = u32(ram9, WORK_PTR)
    node = SCRATCH + 0x200
    w = vwrites(node, [0.0] * 16) + [WF(work + 0x54, 0.25), W32(work + 4, 0x1234)]
    out.append(case('1EBC30 exact 2.5', 0x1EBC30, [node, 3], writes=w, pin=True))
    for fn in (0x1ECB00, 0x1ECFB0, 0x1ED7A0):
        out.append(case(f'{fn:X} zero fraction', fn, [node, 3], writes=[W32(work + 4, 0x0000FFFF), WF(work + 0x54, 0.25)],
                        pin=True))
    p = owners(0x1E2E80, B02)[0]
    b = p + 0x1F0
    rec, tgt = SCRATCH + 0x800, SCRATCH + 0x400
    for h in (0x0113, 0x0114, 0x8013):
        w = [W32(0x700031D0, rec), W32(0x700031D4, tgt), W8(p + 4, 1), W8(p + 0xD, 4), W16(p + 0x94, h),
             W8(0x81070A, 1), W8(tgt, 1)]
        out.append(case(f'1E2E80 k4 high byte {h:x}', 0x1E2E80, [p], beat=B02, writes=w, script={0x19AA80: [1]},
                        pin=True))
    out.append(case('1E2E80 t34 carry', 0x1E2E80, [p], beat=B02,
                    writes=[W8(p + 4, 1), W8(p + 0xD, 4), W32(b + 0x34, 0xFFFF), WF(b + 0x30, 0.3)], pin=True))
    out.append(case('1E2E80 phase to exactly 2', 0x1E2E80, [p], beat=B02,
                    writes=[W8(p + 4, 1), W8(p + 0xD, 3), W32(b + 0x20, FB(1.95)), W8(p + 5, 1)], pin=True))
    return out


def cases_fx(base):
    out = []
    for c in base:
        if c['fn'] in (0x1DEDB0, 0x1DEE80, 0x1DEEC0):
            continue
        for kind in ('after', 'before'):
            out.append(dict(c, name=f"{c['name']} fx-{kind}", fx=kind, pin=False))
    return out


HEAVY = (0x1DF180, 0x1DF5A0)


def cases_poison(base):
    out = []
    for c in base:
        if c['fn'] in HEAVY and not c['pin']:
            continue
        out.append(dict(c, name=f"{c['name']} poison", poison=True, pin=False))
        for kind in ('byte', 'top'):
            out.append(dict(c, name=f"{c['name']} edge-{kind}", edge=kind, pin=False))
        if c['fn'] not in (0x1DEDB0, 0x1DEE80, 0x1DEEC0):
            out.append(dict(c, name=f"{c['name']} prestore", prestore=True, pin=False))
    return out


# The default run (docs/AREA00_HUD.md section 5): the base cases marked pin,
# plus QUICK_PINNED: the case or variant that first killed each mutant the
# pinned base cases left alive (the sweep's EM_TEST_FULL=1 round).
QUICK_PINNED = frozenset((
    '1DEEC0 a2 ffffffff',
    '1DF020 a3 0 fx-before',
    '1DF180 0 fx-before',
    '1DF180 1',
    '1DF180 4',
    '1E2E80 captured a00_02 0 edge-byte',
    '1E2E80 captured a00_02 0 fx-after',
    '1E2E80 dmg a0 k3 h10',
    '1E2E80 dmg a0 k3 h11',
    '1E2E80 dmg a0 k3 ha edge-byte',
    '1E2E80 dmg a0 k3 hb',
    '1E2E80 dmg a0 k3 hc',
    '1E2E80 dmg a0 k3 hd',
    '1E2E80 dmg a0 k3 he',
    '1E2E80 dmg a0 k3 hf',
    '1E2E80 dmg a1 k3 h10',
    '1E2E80 dmg a1 k3 h11',
    '1E2E80 dmg a1 k3 hb',
    '1E2E80 dmg a1 k3 hc',
    '1E2E80 dmg a1 k3 hd',
    '1E2E80 dmg a1 k3 he',
    '1E2E80 dmg a1 k3 hf',
    '1E2E80 dmg h800a',
    '1E2E80 probe 32 1 0 edge-byte',
    '1E2E80 s0 k3 edge-byte',
    '1E2E80 s0 k3 poison',
    '1ED7A0 x0.0 e0.0 fx-after',
    '1ED7A0 x0.0 e0.0 poison',
))


def all_cases():
    rng = random.Random(0xA00D)
    base = cases_context(rng) + cases_line(rng) + cases_hazard(rng) + cases_handlers(rng) + cases_sweep1(rng)
    variants = cases_fx(base) + cases_poison(base)
    for c in base:
        c['pin'] = c['pin'] or c['name'] in QUICK_PINNED
    for c in variants:
        c['pin'] = c['name'] in QUICK_PINNED
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
# 001E2800's fade: the depth word it tests is the 28.4 conversion of a lane
# that the maximum with vf0.x (+0) has just made >= +0 (the VU order puts
# every negative pattern, -0 included, below +0); the conversion of a value
# >= +0 is >= 0 (it saturates to 0x7FFFFFFF), and the arithmetic shift by 4
# keeps it >= 0, so the test "below 0" is never true (fade A and B).
UNREACHABLE = {0x1E2944: 'fade A: the depth word is never negative', 0x1E2A44: 'fade B: the same'}


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
        vu = sorted(r for r in floats if r >= SYSREF.VU_BASE)
        rows.append((hex(fn), kind, na, nf, sorted(r for r in ints if 4 <= r <= 11),
                     sorted(r for r in floats if 12 <= r <= 19), len(vu)))
        if extra_i or extra_f:
            bad.append((hex(fn), kind, 'reads', extra_i, extra_f, 'policy', na, nf))
    return bad, rows


def static_checks(mem):
    """The jump tables 001E2E80 reads (its damage amounts) and the scratch
    area must be as the tests assume in every capture used."""
    elf_tables = [struct.unpack_from('<9I', mem, t) for t in (0x26E8C0, 0x26E890)]
    for beat in (B02, B06, B09, B10):
        ram = image(beat)[0]
        for t, want in zip((0x26E8C0, 0x26E890), elf_tables):
            assert struct.unpack_from('<9I', ram, t) == want, ('jump table differs from the ELF', beat, hex(t))
        assert ram[SCRATCH:SCRATCH + 0x1000] == bytes(0x1000), ('scratch area not zero', beat)
    # the translation's amount tables (em_area00_hud.c) follow these targets in order
    assert [hex(x) for x in elf_tables[0]] == [hex(0x1E316C + 0x10 * i) for i in range(9)], elf_tables[0]
    assert [hex(x) for x in elf_tables[1]] == [hex(0x1E3294 + 0x10 * i) for i in range(9)], elf_tables[1]


def api_checks():
    ram = (U8 * 0x1000)()
    regions = (Region * 1)(Region(0x1000, 0x1000, C.cast(ram, P(U8))))
    calls = []

    def worker(_, pc):
        calls.append(pc.contents.fn)
        return -1 if pc.contents.fn == 0x1AFC10 else 0
    w = WORKER(worker)
    fn = NATIVE.em_area00_hud_001E2E80
    assert fn(None, 0x1000) == -1
    s = Hud(None, 0, w, None, SP, 0, 0, 0)
    assert fn(C.byref(s), 0x1000) == -1 and s.fault == 1 and s.fault_function == 0x1E2E80
    NATIVE.em_area00_hud_clear_fault(C.byref(s))
    ram[4] = 3                                    # state 3: 001AFC10(e)
    s = Hud(regions, 1, WORKER(0), None, SP, 0, 0, 0)
    assert fn(C.byref(s), 0x1000) == -1 and s.fault == 1 and s.fault_address == 0x1AFC10
    s = Hud(regions, 1, w, None, SP, 0, 0, 0)
    assert fn(C.byref(s), 0x1000) == -1 and s.fault == 2 and s.fault_address == 0x1AFC10
    assert fn(C.byref(s), 0x1000) == -1 and calls == [0x1AFC10], 'refuses while latched'
    NATIVE.em_area00_hud_clear_fault(C.byref(s))
    assert s.fault == 0 and s.fault_function == 0 and s.fault_address == 0
    NATIVE.em_area00_hud_clear_fault(None)
    # an unmapped access is reported at its address: 001E2E80's state byte
    s = Hud(regions, 1, w, None, SP, 0, 0, 0)
    assert fn(C.byref(s), 0x1FFC) == -1 and (s.fault, s.fault_address) == (3, 0x2000), (s.fault, hex(s.fault_address))
    # 001DEDB0 reads the context pointer D_00275670 first
    s = Hud(regions, 1, w, None, SP, 0, 0, 0)
    out = U32(0x5A5A5A5A)
    assert NATIVE.em_area00_hud_001DEDB0(C.byref(s), 9, C.byref(out)) == -1
    assert (s.fault, s.fault_address) == (3, CTX_PTR) and out.value == 0x5A5A5A5A


# ======================================================================
# The reuse check: lane EXITB's 001E9280 over the AREA00 captures
# ======================================================================

def reuse_check():
    import test_area01_exitb_reference as EXB
    EXB.ROUTE = ROUTE
    EXB.OUT = OUT / 'exitb'          # its native build goes to this lane's build directory
    EXB.ELF = ELF
    EXB.NATIVE = EXB.build_native()
    EXB.BRANCH_PCS = EXB.branch_pcs(EXB.ExEE(ELF).mem)
    n = 0
    for beat in (B02, B06, B09):
        EXB.EXIT = beat
        for p in [a for a, cb in pool(image(beat)[0]) if cb == 0x15AB00][:8]:
            for fn in (0x1E9280,):
                c = EXB.case(f'reuse 1E9280 {beat[:6]} {p:x}', fn, [p], beat=beat)
                r = EXB.run_case_safe(c)
                assert r[0] == 'ok', ('lane EXITB 001E9280 differs over an AREA00 capture', r[1])
                n += 1
    return n


# ======================================================================
# Main
# ======================================================================

def main():
    global NATIVE, ELF, BRANCH_PCS
    t0 = time.time()
    ELF = read_elf()
    NATIVE = build_native()
    mem = HudEE(ELF).mem
    BRANCH_PCS = branch_pcs(mem)
    for fn in FUNCS:
        assert ENTRIES.get(fn), ('entry without a signature', hex(fn))
    missing = sorted(t for t in jal_targets(mem) if t not in CALLEES and t not in FUNCS)
    assert not missing, ('direct call targets without a policy', [hex(t) for t in missing])
    bad, rows = check_register_reads(bytes(mem))
    if os.environ.get('EM_AREA00_HUD_REGS'):
        for row in rows:
            print('regs', row)
    assert not bad, ('callee register reads outside the compared policy', bad)
    static_checks(mem)
    api_checks()
    base, fxs = all_cases()
    only = os.environ.get('EM_AREA00_HUD_ONLY', '')
    if only:
        keep = {int(x, 16) for x in only.split(',') if x}
        base = [c for c in base if c['fn'] in keep]
        fxs = [c for c in fxs if c['fn'] in keep]
    run_base = RM.select(base, 0, 1, keep=lambda i, c: c['pin'])
    run_fx = RM.select(fxs, 0, 2, keep=lambda i, c: c['pin'])
    todo = run_base + run_fx
    if os.environ.get('EM_AREA00_HUD_FAILFAST'):
        for c in todo:
            r = run_case_safe(c)
            if r[0] == 'fail':
                print('FAIL', r[1])
                sys.exit(1)
        print('test_area00_hud_reference: survived')
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
    if os.environ.get('EM_AREA00_HUD_GAPS'):
        for g in gaps:
            print('gap', g)
    for pc in UNREACHABLE:
        assert (pc, True) not in outcomes or (pc, False) not in outcomes, ('an UNREACHABLE branch went both ways', hex(pc))
    if not only:
        assert not gaps, ('branch outcomes not taken', gaps)
    reused = reuse_check() if not only else 0
    RM.banner(RM.part(len(run_base), len(base), 'cases'), RM.part(len(run_fx), len(fxs), 'fx / poison variants'),
              f'{calls:,} calls compared', f'{fxw:,} fx field changes',
              f'{skipped} variants not comparable', f"branches both ways {len(both)}/{len(BRANCH_PCS)}",
              f'reuse check {reused} EXITB cases over AREA00')
    print(f'test_area00_hud_reference: OK ({time.time() - t0:.1f} s)')


if __name__ == '__main__':
    main()
