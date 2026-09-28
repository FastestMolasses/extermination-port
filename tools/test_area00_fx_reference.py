#!/usr/bin/env python3
"""Execute the ORIGINAL AREA00 lane-A00FX routines and compare the native
em_area00_fx_* translations byte for byte.

docs/AREA00_FX.md. The user's pinned ELF and the recorded AREA00 route
captures (../Extermination/build/s87/route_a00/<beat>/{eeMemory,scratchpad}.bin)
supply every instruction and every table; none are embedded here.

Translated routines (each run as ORIGINAL code on the oracle side):
  packets  001CD940 001D6DD0 001D7510 001D7A80 001F4A00 001F4E20
  spawn    001AA840 001EF510 001EFF10 001EFFD0 001F0060 001F00A0 001F02C0
  trail    001F1550 001F15F0 001F18C0
  debris   001F2BA0 001F2E90 001F2F90 001F3340 001F3620 001F3E30
  exit     001F4F40 001F4F90 001F5040 001F5490 001F5F60
  glow     001F6FB0
The leaves 00102948 and 00102958 run inline on both sides.

Method: the render lane's (tools/test_area01_render_reference.py, whose
buffers, dirty-page tracker, entry check and EE access rules are imported)
with the UI lane's callee interface. The oracle runs the original routine
over a captured beat's RAM and scratchpad; every callee outside the lane
runs as ORIGINAL code too, nested, and its entry is logged: the address, the
stack pointer, the 64-bit image of every integer register the callee reads
or the original sets up for it, and every float argument register. The
native module runs over its own copy; each callee it reaches through
EmArea00Fx.call runs the same ORIGINAL code in a second interpreter sharing
the native memory. Before every such call (the entry check) all 32 MiB of
RAM and the 16 KiB scratchpad must equal the oracle's at the same call, with
the logged entry; after the last step the results, the call log, RAM and
scratchpad must be identical. Registers a callee is not passed are poisoned
on the native side. Only executions the EE completes are run (aligned
accesses inside the captures' TLB map, which is the same in all 28 AREA01
and AREA00 save states).

Case states are built from the captures by ORIGINAL code: the spawners
(001EF9D0, 001EFD20, 001EFE00) run over the captured RAM before a case
starts (the "setup"; not compared), and node callbacks then run for several
frames as case steps, with the pool's per-callback words (D_00275B44 /
48 / 40) set as the pool sets them.

Every conditional branch of the 28 routines must be taken both ways except
the ones docs/AREA00_FX.md section 3 lists as unreachable. EM_TEST_FULL=1
runs every generated case; the default runs a covering sample. At most four
worker processes (EM_TEST_JOBS=1: serial).
"""
import ctypes as C
import multiprocessing
import os
import random
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
os.environ.setdefault('EM_LANE', 'a00fx')
import reference_mode as RM  # noqa: E402
import test_area01_render_reference as R  # noqa: E402
import test_shadow_actor_route_reference as SAR  # noqa: E402
from test_player_slide_reference import EE, read_elf, sx32, RETURN, STACK_TOP  # noqa: E402

MASK, MASK64 = 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF
DECOMP = ROOT.parent / 'Extermination'
ROUTE = DECOMP / 'build/s87/route_a00'
OUT = ROOT / 'build' / 'area00' / os.environ.get('EM_A00FX_OUT', 'fx')
R.OUT = OUT                      # the dirty-page tracker library builds here
R.ROUTE = ROUTE                  # R.image() / R.case_ram() read the AREA00 captures
R.IMAGES.clear()
JOBS = R.JOBS
F = R.F

SIZES = {  # the translated routines (original bytes)
    0x1CD940: 0x480, 0x1D6DD0: 0x90, 0x1D7510: 0xD0, 0x1D7A80: 0xAC, 0x1F4A00: 0x8, 0x1F4E20: 0x14,
    0x1AA840: 0x84, 0x1EF510: 0x26C, 0x1EFF10: 0xC0, 0x1EFFD0: 0x88, 0x1F0060: 0x3C, 0x1F00A0: 0x7C,
    0x1F02C0: 0x4C, 0x1F1550: 0x9C, 0x1F15F0: 0x2D0, 0x1F18C0: 0x1E0, 0x1F2BA0: 0x2E4, 0x1F2E90: 0xF4,
    0x1F2F90: 0x3AC, 0x1F3340: 0x2D8, 0x1F3620: 0x80C, 0x1F3E30: 0x16C, 0x1F4F40: 0x48, 0x1F4F90: 0xB0,
    0x1F5040: 0x450, 0x1F5490: 0x44, 0x1F5F60: 0x2AC, 0x1F6FB0: 0x800,
}
TRPC = frozenset(pc for a, n in SIZES.items() for pc in range(a, a + n, 4))
INLINE = {0x102948, 0x102958}

A0, A1, A2, A3, T0, T1, S4 = 4, 5, 6, 7, 8, 9, 20
# callee -> (integer registers, float registers, result used): the registers
# the original sets up for the call, plus any other register the callee
# reads (RegScan, checked at start-up with the exceptions below)
WSPEC = {
    0x0011DE90: ((), (12,), 'f0'), 0x0011E2A8: ((), (12,), 'f0'), 0x00122BB8: ((), (), 'v0'),
    0x001026A0: ((A0, A1, A2), (), None), 0x001026D0: ((A0, A1, A2), (), None),
    0x001028B8: ((A0, A1, A2), (), None), 0x001028D0: ((A0, A1, A2), (), None),
    0x00102900: ((A0, A1), (12,), None), 0x00102918: ((A0, A1, A2), (), None),
    0x00102990: ((A0, A1), (), None), 0x001029C0: ((A0,), (), None), 0x00102BB0: ((A0, A1), (12,), None),
    0x00102C58: ((A0, A1, A2), (), None), 0x001031E0: ((A0, A1), (), None), 0x001281C0: ((), (12,), 'v0'),
    0x0019A570: ((A0, A1, A2, A3, S4), (), 'v0'), 0x001AA7A0: ((A1,), (), 'v0'), 0x001AF780: ((), (), 'v0'),
    0x001AFA90: ((A0,), (), 'v0'), 0x001AFC10: ((A0,), (), None), 0x001B17A0: ((A0,), (), None),
    0x001C22A0: ((A0,), (), 'v0'), 0x001C6120: ((A0, A1), (), 'v0'), 0x001C6150: ((A0,), (), 'v0'),
    0x001C6200: ((A0,), (), None), 0x001C62C0: ((A0,), (), None), 0x001C6380: ((A0,), (), None),
    0x001C63D0: ((A0,), (), None), 0x001C7900: ((A0, A1, A2, A3), (), None), 0x001C9E40: ((A0, A1), (), None),
    0x001CA1C0: ((A0, A1, A2), (), None), 0x001CA3B0: ((A0,), (12, 13, 14), None),
    0x001CA4D0: ((A0, A1, A2), (), None), 0x001CA5E0: ((A0, A1, A2), (), None),
    0x001CA7B0: ((A0,), (12,), 'v0'), 0x001CA940: ((A0, A1), (), None), 0x001CAAC0: ((A0, A1, A2), (), None),
    0x001CB5B0: ((), (), None), 0x001CB5F0: ((A0, A1, A2), (), 'v0'), 0x001CB6B0: ((A0, A1, A2, A3), (), None),
    0x001CB760: ((A0, A1, A2), (), None), 0x001CB900: ((A0, A1, A2), (), None), 0x001CCF70: ((A0,), (), 'v0'),
    0x001CD370: ((A0,), (), 'v0'), 0x001CD390: ((A0, A1), (), None), 0x001CFA60: ((A0, A1), (12, 13), None),
    0x001CFB50: ((A0, A1, A2), (12, 13, 14, 15, 16), None), 0x001CFBE0: ((A0, A1, A2, A3, T0), (), None),
    0x001D3990: ((A0,), (), None), 0x001D80E0: ((A0, A1), (), None), 0x001D8C20: ((A0,), (), None),
    0x001EEEB0: ((A0, A1), (), None), 0x001EF9D0: ((A0, A1), (12,), 'v0'), 0x001EFD20: ((A0, A1), (), None),
    0x001EFD90: ((A0, A1, A2), (), None), 0x001F4BF0: ((A0, A1), (), 'v0'),
    0x001F4D40: ((A0, A1), (12, 13), 'v0'), 0x001FBD50: ((A0, A1, A2), (12,), None),
}
INDIRECT = ((A0,), (), None)     # 001F5040's call through the node's +0x4C

# Registers RegScan reports a callee reading that its policy does not pass,
# with why none is an input here (enforced exactly: a new one fails).
REG_READ_EXCEPTIONS = {
    0x001CFBE0: ({16, 17}, set(), 's0 / s1 when its a1 is 7 or more (its switch bound, the case the AREA01 SYS '
                                  'lane lists); both lane call sites (001EF510, 001F6FB0) pass a1 = 1'),
    0x00102BB0: (set(), {'vf4.y', 'vf4.z', 'vf4.w'}, 'through 001029E8, which multiplies vf4.yzw by vf0.x (0) and '
                                                    'then overwrites vf4.y; 00102BB0 subtracts vf4.zw from itself'),
    0x00102C58: (set(), {'vf4.y', 'vf4.z', 'vf4.w'}, 'the same leaf 001029E8 (through 00102BB0)'),
    0x001CD390: (set(), {'vf6.w'}, 'through the cross-product leaf 00102718, which reads vf6.w only to subtract it '
                                  'from itself'),
}

ELF = NATIVE = None


# ======================================================================
# Interpreter
# ======================================================================

class FXEE(R.A01EE):
    """The render lane's A01EE (measured COP1 / VU0, the EE access rules, the
    store journal) with: branch outcomes of this lane's routines (top level
    only), the MMI byte ops and PCPYH, a refusal of an integer division by
    zero inside the lane's routines, and the lane's indirect call logged as a
    callee."""

    def branch(self, word, pc):
        b = EE.branch(self, word, pc)
        if b is not None and self.hooks and pc in TRPC:
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            if not (op == 4 and rs == 0 and rt == 0):
                self.outcomes.add((pc, b[0]))
        return b

    def execute(self, word, pc):
        if word >> 26 == 0 and (word & 63) in (26, 27) and self.hooks and pc in TRPC \
                and (self.r[word >> 16 & 31] & MASK) == 0:
            raise R.Unmeasured('integer division by zero', hex(pc))
        return EE.execute(self, word, pc)

    def mmi(self, word, pc):
        if (word & 63) == 0x29 and (word >> 6 & 31) == 0x1B:                    # pcpyh
            rt, rd = word >> 16 & 31, word >> 11 & 31
            lo, hi = self.r[rt] & 0xFFFF, self.rh[rt] & 0xFFFF
            if rd:
                self.r[rd] = lo * 0x0001000100010001
                self.rh[rd] = hi * 0x0001000100010001
            return
        fn, sub = word & 63, word >> 6 & 31
        if (fn, sub) in ((0x08, 0x08), (0x08, 0x09), (0x08, 0x0A), (0x28, 0x0A)):   # paddb psubb pcgtb pceqb
            rs, rt, rd = word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
            a = (self.r[rs] & MASK64) | (self.rh[rs] << 64)
            b = (self.r[rt] & MASK64) | (self.rh[rt] << 64)
            value = 0
            for i in range(16):
                x, y = a >> 8 * i & 255, b >> 8 * i & 255
                if sub == 0x08:
                    z = (x + y) & 255
                elif sub == 0x09:
                    z = (x - y) & 255
                elif fn == 0x08:
                    z = 255 if (x - 256 if x > 127 else x) > (y - 256 if y > 127 else y) else 0
                else:
                    z = 255 if x == y else 0
                value |= z << 8 * i
            if rd:
                self.r[rd], self.rh[rd] = value & MASK64, value >> 64
            return
        return R.A01EE.mmi(self, word, pc)

    def run(self, pc):
        hooks, load, limit, steps = self.hooks, self.load, self.limit, 0
        while True:
            if pc == RETURN:
                self.steps += steps
                return
            hook = hooks.get(pc)
            if hook is not None:
                hook(self)
                pc = self.r[31] & MASK
                continue
            word = load(pc)
            steps += 1
            if steps > limit:
                raise AssertionError(('step limit', hex(pc)))
            op = word >> 26
            if op in (2, 3):
                if op == 3: self.r[31] = pc + 8
                self.execute(load(pc + 4), pc + 4)
                pc = (pc & 0xF0000000) | ((word & 0x3FFFFFF) << 2)
                continue
            if op == 0 and word & 63 in (8, 9):
                target = self.r[word >> 21 & 31] & MASK
                if word & 63 == 9:
                    rd = word >> 11 & 31
                    if rd: self.r[rd] = pc + 8
                    if hooks and pc in TRPC and target not in TRPC:
                        self.execute(load(pc + 4), pc + 4)
                        logged_call(self, target, INDIRECT)
                        pc += 8
                        continue
                self.execute(load(pc + 4), pc + 4)
                pc = target
                continue
            b = self.branch(word, pc)
            if b is None:
                self.execute(word, pc)
                pc += 4
                continue
            taken, target, likely = b
            if taken:
                self.execute(load(pc + 4), pc + 4)
                pc = target
            elif likely:
                pc += 8
            else:
                self.execute(load(pc + 4), pc + 4)
                pc += 8


def entry_of(e, address, spec):
    ni, nf, _ = spec
    return (address, e.r[29] & MASK, tuple((n, e.r[n] & MASK64) for n in ni), tuple((n, e.f[n] & MASK) for n in nf))


def result_of(spec, v0, f0):
    kind = spec[2]
    if kind is None:
        return None
    if kind == 'v0':
        return v0 & MASK64
    if kind == 'f0':
        return f0 & MASK
    return (v0 & MASK64, f0 & MASK)


def logged_call(e, address, spec):
    entry = entry_of(e, address, spec)
    e.entries.append((len(e.journal), entry))
    v0, f0 = SAR.nested_bits(e, address)
    e.calls.append(entry + (result_of(spec, v0, f0),))
    e.r[2], e.f[0] = v0, f0


def oracle_hooks(ee):
    ee.hooks = {a: (lambda e, a=a, spec=spec: logged_call(e, a, spec)) for a, spec in WSPEC.items()}


def oracle_ee(spad):
    """An oracle interpreter over this process's oracle RAM (set to the scratch)."""
    W = R.work()
    if W.oracle_live is not None:
        W.oracle.dirty |= R.journal_pages(W.oracle_live.journal)
        W.oracle_live = None
    R.sync(W.oracle, W.scratch.data, W.scratch.key, W.scratch.dirty)
    ee = FXEE(ELF, b'', spad)
    ee.mem = W.oracle.data
    W.oracle_live = ee
    return ee


# ======================================================================
# Native side
# ======================================================================

P, U8, U32, U64 = C.POINTER, C.c_uint8, C.c_uint32, C.c_uint64


class Regs(C.Structure):
    _fields_ = [('imask', U32), ('fmask', U32), ('r', U64 * 32), ('f', U32 * 32)]


CALL = C.CFUNCTYPE(C.c_int, C.c_void_p, U32, U32, P(Regs), P(U64), P(U32))


class FxState(C.Structure):
    _fields_ = [('core', R.Core), ('call', CALL), ('ctx', C.c_void_p), ('sp', U32)]


# native entry -> (argument kinds: 'r' a 64-bit register image in a0, a1,
# ... order, 'f' float bits in f12, f13, ... order; result: None, 'v0'
# (a u64 out) or 'v0f0' (u64 and u32 outs))
ENTRIES = {
    0x1CD940: ('rrrrr', None), 0x1D6DD0: ('rrr', 'v0'), 0x1D7510: ('rrr', None), 0x1D7A80: ('rrrrr', 'v0'),
    0x1F4A00: ('rr', 'v0'), 0x1F4E20: ('rrf', 'v0'),
    0x1AA840: ('', None), 0x1EF510: ('rr', None), 0x1EFF10: ('rrrrrrf', 'v0'), 0x1EFFD0: ('rrrrf', 'v0'),
    0x1F0060: ('rr', None), 0x1F00A0: ('rrrr', 'v0'), 0x1F02C0: ('rrf', None),
    0x1F1550: ('rr', 'v0'), 0x1F15F0: ('rrr', None), 0x1F18C0: ('r', None),
    0x1F2BA0: ('r', None), 0x1F2E90: ('rr', 'v0'), 0x1F2F90: ('rrrr', None), 0x1F3340: ('rrr', None),
    0x1F3620: ('rr', None), 0x1F3E30: ('rrrrr', None),
    0x1F4F40: ('r', 'v0'), 0x1F4F90: ('rf', None), 0x1F5040: ('r', None), 0x1F5490: ('r', 'v0'),
    0x1F5F60: ('rrrr', None), 0x1F6FB0: ('r', None),
}
assert set(ENTRIES) == set(SIZES)
SOURCES = ['src/game/em_area00_fx_gs.c', 'src/game/em_area00_fx_spawn.c', 'src/game/em_area00_fx_trail.c',
           'src/game/em_area00_fx_debris.c', 'src/game/em_area00_fx_exit.c', 'src/game/em_area00_fx_glow.c']


def build_native(sources=None, tag='area00_fx'):
    """EM_A00FX_SRC=<dir> builds <dir>/game/em_area00_fx_*.c instead (headers
    looked up in <dir> first): the mutation runs."""
    OUT.mkdir(parents=True, exist_ok=True)
    alt = os.environ.get('EM_A00FX_SRC')
    incs = ['-Isrc']
    if alt and sources is None:
        sources = [str(Path(alt) / 'game' / Path(s).name) for s in SOURCES]
        incs = ['-I' + alt, '-Isrc']
        tag = 'area00_fx_mut%d' % os.getpid()
    lib = OUT / (tag + ('.dylib' if sys.platform == 'darwin' else '.so'))
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-ffp-contract=off',
                    '-shared', '-fPIC'] + incs + (sources or SOURCES) + ['-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    for address, (kinds, result) in ENTRIES.items():
        fn = getattr(native, 'em_area00_fx_%08X' % address)
        outs = [] if result is None else [P(U64)] if result == 'v0' else [P(U64), P(U32)]
        fn.argtypes = [P(FxState)] + [U64 if k == 'r' else U32 for k in kinds] + outs
        fn.restype = C.c_int
    return native


class Native(R.Native):
    """One native run over this process's native RAM (set to the scratch);
    its callees run the original code in an interpreter sharing that memory.
    entry_ok / end_differences are the render lane's."""

    def __init__(self, spad, oracle=None, fail_at=None, null_call=False, views=True, sp=STACK_TOP, stack=None):
        W = self.W = R.work()
        R.sync(W.native, W.scratch.data, W.scratch.key, W.scratch.dirty, native=True)
        self.ee = FXEE(ELF, b'', spad, journal=False)
        self.ee.mem = W.native.data
        self.ee.hooks = {}
        if stack is not None:
            self.ee.stack[:] = stack
        self.log, self.fail_at = [], fail_at
        self.oracle, self.mismatch, self.jpos, self.checked = oracle, None, 0, 0
        if oracle is not None:
            R.sync(W.expect, W.scratch.data, W.scratch.key, W.scratch.dirty)
            self.exp_spad = bytearray(spad)
        self.spadc = (U8 * R.SPAD_SIZE).from_buffer(self.ee.spad)
        self.stackc = (U8 * len(self.ee.stack)).from_buffer(self.ee.stack)
        vlist = [R.View(0, R.RAM_SIZE, C.cast(W.native_c, P(U8))),
                 R.View(0x70000000, R.SPAD_SIZE, C.cast(self.spadc, P(U8))),
                 R.View(0x7F000000, len(self.ee.stack), C.cast(self.stackc, P(U8)))]
        self.views = (R.View * len(vlist))(*vlist)
        self.state = FxState()
        if views:
            self.state.core.world = R.World(C.cast(self.views, P(R.View)), len(vlist))
        self.cb = CALL(self.dispatch)
        if not null_call:
            self.state.call = self.cb
        self.state.sp = sp

    def dispatch(self, _c, target, sp, regs, v0, f0):
        rr = regs.contents
        ni = tuple(n for n in range(32) if rr.imask >> n & 1)
        nf = tuple(n for n in range(32) if rr.fmask >> n & 1)
        spec = WSPEC.get(target, INDIRECT)
        entry = (target, sp, tuple((n, rr.r[n] & MASK64) for n in ni), tuple((n, rr.f[n] & MASK) for n in nf))
        if not self.entry_ok(entry):
            return -1
        if self.fail_at is not None and len(self.log) == self.fail_at:
            self.log.append(entry + ('fail',))
            return -1
        e = self.ee
        R.poison(e)
        for n, value in entry[2]: e.r[n] = value & MASK64
        for n, value in entry[3]: e.f[n] = value & MASK
        e.r[29], e.r[31] = (sp - 0x400) & ~15, RETURN
        try:
            e.run(target)
        except Exception as ex:  # noqa: BLE001 - reported by the case
            self.mismatch = ('call %d' % len(self.log), hex(target), 'the original callee raised on the native side',
                             type(ex).__name__, str(ex)[:200])
            return -1
        self.log.append(entry + (result_of(spec, e.r[2], e.f[0]),))
        v0[0] = e.r[2] & MASK64
        f0[0] = e.f[0] & MASK
        return 0

    def call(self, address, args):
        fn = getattr(NATIVE, 'em_area00_fx_%08X' % address)
        kinds, result = ENTRIES[address]
        outs = [] if result is None else [U64(0)] if result == 'v0' else [U64(0), U32(0)]
        rc = fn(C.byref(self.state), *args, *[C.byref(o) for o in outs])
        return rc, tuple(o.value for o in outs) if outs else None

    def poke(self, address, value, size):
        self.ee.save(address, value, size)


def oracle_call(ee, address, args, sp):
    """The original routine with the entry's argument registers; its results
    as the native entry reports them."""
    kinds, result = ENTRIES[address]
    ii = ff = 0
    for k, value in zip(kinds, args):
        if k == 'r':
            ee.r[4 + ii] = value & MASK64
            ii += 1
        else:
            ee.f[12 + ff] = value & MASK
            ff += 1
    ee.r[29], ee.r[31] = sp, RETURN
    ee.run(address)
    if result is None:
        return None
    if result == 'v0':
        return (ee.r[2] & MASK64,)
    return (ee.r[2] & MASK64, ee.f[0] & MASK)


# ======================================================================
# Running one case on both sides
# ======================================================================

def run_both(where, spad, steps, check=None, sp=STACK_TOP, stack=None):
    """steps: ('call', address, args) or ('poke', address, value, size), run
    in order on both sides from this process's scratch RAM and `spad`.
    Compares the entry of every callee (arguments, RAM, scratchpad), then the
    results, the call log, RAM and scratchpad. Returns (outcomes, calls,
    entries) or 'unmeasured' when both sides refuse."""
    if RECORD is not None:              # the fault group borrows the state
        RECORD.append((bytes(spad), list(steps)))
        return None
    ee = oracle_ee(spad)
    oracle_hooks(ee)
    if stack is not None:
        ee.stack[:] = stack
    unmeasured = False
    results_o = []
    try:
        for step in steps:
            if step[0] == 'poke':
                ee.save(step[1], step[2], step[3])
                continue
            results_o.append(oracle_call(ee, step[1], step[2], sp))
    except R.Unmeasured:
        unmeasured = True
    except R.NotCompletable as ex:
        raise AssertionError((where, 'the original cannot complete this case') + ex.args)
    if check is not None and not unmeasured:
        assert check(ee), (where, 'the case did not reach the state it is built for')
    native = Native(spad, oracle=None if unmeasured else ee, sp=sp, stack=stack)
    results_n = []
    for step in steps:
        if step[0] == 'poke':
            native.poke(step[1], step[2], step[3])
            continue
        native.state.sp = sp
        rc, value = native.call(step[1], step[2])
        assert native.mismatch is None, (where, 'entry check') + native.mismatch
        if rc != 0:
            f = native.state.core.fault
            if unmeasured and f.code == 6:
                return 'unmeasured'
            raise AssertionError((where, 'native faulted', hex(step[1]), hex(f.address), f.code, hex(f.detail)))
        results_n.append(value)
    assert not unmeasured, (where, 'the original refused (unmeasured) but the native did not fault')
    for step, got, want in zip([s for s in steps if s[0] == 'call'], results_n, results_o):
        assert got == want, (where, hex(step[1]), 'result', got, want)
    assert native.log == ee.calls, (where, 'callee calls', R.diff_logs(native.log, ee.calls))
    assert native.checked == len(ee.entries), (where, 'entry checks', native.checked, len(ee.entries))
    assert native.ee.spad == ee.spad, (where, 'scratchpad differs (address, native, original)',
                                       R.first_differences(bytes(native.ee.spad), bytes(ee.spad), 0x70000000))
    bad = native.end_differences(ee)
    assert not bad, (where, 'RAM differs (address, native, original)', bad)
    return ee.outcomes, len(ee.calls), native.checked


# ======================================================================
# Captures and case helpers
# ======================================================================

BEATS = sorted(p.name for p in ROUTE.iterdir() if (p / 'eeMemory.bin').exists()) if ROUTE.exists() else []
w32 = R.w32


def put(ram, at, value, size=4):
    R.put(ram, at, value, size)


def b8(ram, at):
    return ram[at & 0x1FFFFFF]


def h16(ram, at):
    return struct.unpack_from('<h', ram, at & 0x1FFFFFF)[0]


def f32(ram, at):
    return struct.unpack_from('<f', ram, at & 0x1FFFFFF)[0]


def case(beat):
    """(scratch RAM, a scratchpad copy) of a beat."""
    return R.case_ram(beat), bytearray(R.image(beat)[1])


def spw(spad, at):
    return struct.unpack_from('<I', spad, at - 0x70000000)[0]


def sput(spad, at, value):
    struct.pack_into('<I', spad, at - 0x70000000, value & MASK)


def sx(v):
    return sx32(v & MASK) & MASK64


def setup_call(ram, spad, address, iargs=(), fargs=(), regs=None):
    """Run ORIGINAL code over the case's scratch RAM and scratchpad before a
    case starts (not compared): every callee runs as original code too.
    Returns (v0, f0)."""
    assert ram is R.work().scratch.data
    ee = FXEE(ELF, b'', spad)
    ee.mem = ram
    ee.hooks = {}
    for i, value in enumerate(iargs): ee.r[4 + i] = sx(value) if value <= MASK else value & MASK64
    for i, value in enumerate(fargs): ee.f[12 + i] = value & MASK
    for n, value in (regs or {}).items(): ee.r[n] = value & MASK64
    ee.r[29], ee.r[31] = STACK_TOP, RETURN
    ee.run(address)
    R.work().scratch.dirty |= R.journal_pages(ee.journal)
    spad[:] = ee.spad
    return ee.r[2] & MASK64, ee.f[0] & MASK


def pool_nodes(ram, behaviours):
    out, node, seen = [], w32(ram, 0x275BC0), 0
    while node and seen < 0x400:
        if w32(ram, node + 0x10) in behaviours:
            out.append(node)
        node = w32(ram, node + 0x1C)
        seen += 1
    return out


def callback_pokes(node):
    """What the pool does before a node's callback: the current object
    D_00275B44 / 48 and its bone array D_00275B40."""
    return [('poke', 0x275B44, node, 4), ('poke', 0x275B48, node, 4), ('poke', 0x275B40, node + 0x110, 4)]


def frames(node, address, n):
    out = []
    for _ in range(n):
        out += callback_pokes(node) + [('call', address, (sx(node),))]
    return out


def check_code(ram, beat, base):
    for start in list(SIZES) + sorted(WSPEC) + sorted(INLINE):
        size = SIZES.get(start, 0x40)
        assert ram[start:start + size] == bytes(base.mem[start:start + size]), (beat, hex(start))


def all_branches():
    probe = EE(ELF)
    out = set()
    for pc in sorted(TRPC):
        word = probe.load(pc)
        op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
        if op in (1, 4, 5, 6, 7, 20, 21, 22, 23) or (op == 17 and rs == 8):
            if not (op == 4 and rs == 0 and rt == 0):
                out.add(pc)
    return out


def callee_set():
    probe = EE(ELF)
    targets = set()
    for pc in sorted(TRPC):
        word = probe.load(pc)
        if word >> 26 in (2, 3):
            target = (word & 0x3FFFFFF) << 2
            if target not in SIZES:
                targets.add(target)
    return targets


# ======================================================================
# Register reads of the callees (RegScan, imported unchanged)
# ======================================================================

def check_register_reads(mem, targets):
    """Every register a callee reads (RegScan: before writing it, on some
    path, through its own callees) must be one its policy passes, or be
    listed in REG_READ_EXCEPTIONS. Returns the rows that break the rule."""
    import test_area01_sys_reference as SYS    # RegScan, unchanged (read-only reuse)
    scan = SYS.RegScan(mem)

    def vname(r):
        r -= SYS.VU_BASE
        return 'vf%d.%s' % (r // 4, 'xyzw'[r % 4]) if r < 128 else 'vu%d' % r
    bad = []
    for fn in sorted(targets):
        ni, nf, _ = WSPEC.get(fn, INDIRECT)
        ri, rf, notes = scan.reads(fn)
        ints = {r for r in range(32) if ri >> r & 1} - {0, 28, 29, 31} - set(ni)
        flts = {r for r in range(32) if rf >> r & 1} - set(nf)
        vus = {vname(r) for r in range(SYS.VU_BASE, SYS.VU_BASE + 152) if rf >> r & 1 and r - SYS.VU_BASE < 128}
        vus -= {'vf0.x', 'vf0.y', 'vf0.z', 'vf0.w'}
        xi, xv, _ = REG_READ_EXCEPTIONS.get(fn, (set(), set(), ''))
        if ints - xi or flts or vus - xv or (xi and not ints) or (xv and not vus):
            bad.append((hex(fn), sorted(ints), sorted(flts), sorted(vus), sorted(n for n in notes)[:3]))
    return bad


# ======================================================================
# Case groups
# ======================================================================
#
# Every item is a plain tuple (picklable, repr-stable); its case function
# builds the state from the item's beat and runs it on both sides.

FREE = 0x1A00000                 # zero in every AREA00 capture: crafted buffers live here
PLAYER = 0x8102B0
MAIN = [b for b in BEATS if b != 'a00_s0_shaft_door_back']
FXB = 'a00_09_ne_room_out'       # the beat whose frames ran the crate breaks and the glow
EXB = 'a00_10_progression_exit'  # the beat with the exit-phase nodes (001F5040, 001C50B0)
PAD = 'a00_03_padlock'           # the first melee (the trail)


def rfloat(rng, lo, hi):
    return F(rng.uniform(lo, hi))


def put_vec(ram, at, v):
    for i, x in enumerate(v):
        put(ram, at + 4 * i, x)


def spad_vec(spad, at):
    return [spw(spad, at + 4 * i) for i in range(4)]


def ctx_of(ram):
    return w32(ram, 0x275670)


# ---- 001AA840 ----------------------------------------------------------

def aa840_items():
    items = [('cap', b, 0) for b in BEATS] + [('player', b, 0) for b in BEATS] + [('empty', b, 0) for b in (PAD, EXB)]
    for beat in (PAD, FXB, EXB):
        for seed in range(40):
            items.append(('unit', beat, seed))
    return items


def aa840_case(item):
    kind, beat, seed = item
    ram, spad = case(beat)
    if kind != 'cap':
        put(ram, 0x275B40, PLAYER + 0x110)
    if kind == 'empty':
        put(ram, 0x275B84, 0, 2)
    if kind == 'unit':
        rng = random.Random(repr(item))
        n = h16(ram, 0x275B84)
        base = w32(ram, 0x275B7C)
        ents = [w32(ram, base + 4 * i) for i in range(n)]
        bone = w32(ram, w32(ram, 0x275B40))
        bx, by, bz = f32(ram, bone + 0xC0), f32(ram, bone + 0xC4), f32(ram, bone + 0xC8)
        count = rng.choice([1, 2, 3, 5, n, n])
        put(ram, 0x275B84, count, 2)
        for i in range(min(count, n)):
            e = ents[i]
            if not e:
                continue
            r = rng.random()
            if r < 0.15:
                put(ram, base + 4 * i, 0)
                continue
            b0 = 1 if rng.random() < 0.8 else rng.choice([0, 2, 0x81])
            b2 = (b8(ram, e + 2) & 0xE0) | (4 if rng.random() < 0.8 else rng.choice([5, 0x14, 0]))
            b3 = 0x29 if rng.random() < 0.8 else rng.choice([0x28, 0x2A, 0xA9])
            put(ram, e, b0, 1)
            put(ram, e + 2, b2, 1)
            put(ram, e + 3, b3, 1)
            d = rng.choice([0.0, 3.0, 4.9, 5.0, 5.1, 30.0])
            a = rng.uniform(0, 6.283)
            import math
            put(ram, e + 0xB0, F(bx - d * math.cos(a)))
            put(ram, e + 0xB8, F(bz - d * math.sin(a)))
            put(ram, e + 0xB4, F(by + rng.choice([-50.0, -45.0, -44.9, 0.0, 0.5, -10.0])))
    return run_both(item, spad, [('call', 0x1AA840, ())])


# ---- packets: 001D6DD0 / 001D7510 / 001D7A80 / 001CD940 ------------------

def pk_items():
    items = []
    for beat in BEATS:
        items += [('6dd0', beat, 'cap'), ('7510', beat, 'cap'), ('7a80', beat, 'cap')]
    for seed in range(60):
        beat = MAIN[seed % len(MAIN)]
        items += [('6dd0', beat, seed), ('7510', beat, seed), ('7a80', beat, seed)]
    return items


def pk_case(item):
    which, beat, seed = item
    ram, spad = case(beat)
    rng = random.Random(repr(item))
    unit = seed != 'cap'
    ch = rng.randrange(4) if unit else 3
    ctx = ctx_of(ram)
    if which == '6dd0':
        a1, a2 = (0x7000, 0x7900) if not unit else (rng.getrandbits(32), rng.getrandbits(32))
        args = (sx(ch), sx(a1) if rng.random() < 0.8 else a1 | (rng.getrandbits(32) << 32), sx(a2))
        return run_both(item, spad, [('call', 0x1D6DD0, args)])
    if which == '7510':
        if unit:
            put(ram, ctx + 0x9C, rng.choice([0, 1, 0x80000000, 0x100]))
        a1, a2 = (0, 0) if not unit else (rng.getrandbits(32), rng.getrandbits(32))
        return run_both(item, spad, [('call', 0x1D7510, (sx(ch), sx(a1), sx(a2)))])
    # 7a80: the three source blocks (two quadwords each at a2 / a3, one at t0)
    blocks = []
    for k in range(3):
        at = FREE + 0x100 * k + (rng.choice([0, 4, 8, 12]) if unit else 0)
        for i in range(8):
            put(ram, (at & ~15) + 4 * i, rng.getrandbits(32) if unit else spw(spad, 0x700038A0 + 4 * ((i + 4 * k) % 12)))
        blocks.append(at)
    a1 = 0x40 if not unit else rng.getrandbits(32)
    args = (sx(ch), sx(a1), sx(blocks[0]), sx(blocks[1]), sx(blocks[2]))
    return run_both(item, spad, [('call', 0x1D7A80, args)])


def line_points(ram, rng, far=False):
    """Two points near the player (on screen in every capture), or one far."""
    px, py, pz = f32(ram, PLAYER + 0xB0), f32(ram, PLAYER + 0xB4), f32(ram, PLAYER + 0xB8)
    pts = []
    for _ in range(2):
        d = 5000.0 if far and rng.random() < 0.6 else rng.uniform(0, 30)
        pts.append([F(px + rng.uniform(-d, d)), F(py + rng.uniform(-20, 20)), F(pz + rng.uniform(-d, d)), F(1.0)])
    return pts


def cd940_items():
    items = []
    for seed in range(80):
        items.append((MAIN[seed % len(MAIN)], seed))
    return items


def cd940_case(item):
    beat, seed = item
    ram, spad = case(beat)
    rng = random.Random(repr(item))
    mode = [0, 1, 2, 3, 4, 0xFF][seed % 6]
    p0, p1 = line_points(ram, rng, far=seed % 7 == 3)
    at = [FREE, FREE + 0x10, FREE + 0x20, FREE + 0x30]
    put_vec(ram, at[0], p0)
    put_vec(ram, at[2], p1)
    for k in (1, 3):
        put_vec(ram, at[k], [rng.choice([0, 1, 0x7F, 0x80, 0xFF, 0x100, rng.getrandbits(32)]) for _ in range(4)])
    if seed % 11 == 5:                     # a fog quadword that clamps to 0 and to x
        put_vec(ram, ctx_of(ram) + 0xA0, [F(rng.choice([255.0, 10.0, -5.0])), 0, F(rng.uniform(-300, 300)),
                                           F(rng.uniform(-3, 3))])
    if seed % 11 == 8:                     # a fog limit above 256: the 12.4 word reaches 0x100 and more
        put_vec(ram, ctx_of(ram) + 0xA0, [F(rng.choice([1000.0, 300.0, 256.0])), 0, F(rng.uniform(250, 900)), 0])
    args = (sx(mode), sx(at[0] + rng.choice([0, 4])), sx(at[1]), sx(at[2]), sx(at[3] + rng.choice([0, 8])))
    return run_both(item, spad, [('call', 0x1CD940, args)])



# ---- the 001EF9D0 wrappers, 001F02C0 and the tail calls ------------------

def bad_id(ram):
    """An area-table id whose record +0xC is 0 (001EF9D0 returns 0)."""
    base = w32(ram, 0x259C74 + 4 * b8(ram, 0x810700))
    if base == 0:                  # no table for this area (a00_10 is AREA01 again): the EE would fault
        return None
    for i in range(0x100):
        if w32(ram, base + i * 0x30 + 0xC) == 0:
            return i
    raise AssertionError('no empty effect record')


def sp_items():
    items = []
    for beat in BEATS:
        for which in ('eff10', 'effd0', 'f0060', 'f00a0', 'f02c0', 'f4a00', 'f4e20'):
            items.append((which, beat, 'cap'))
    for seed in range(40):
        beat = MAIN[seed % len(MAIN)]
        for which in ('eff10', 'effd0', 'f0060', 'f00a0', 'f02c0', 'f4e20'):
            items.append((which, beat, seed))
    return items


def sp_case(item):
    which, beat, seed = item
    ram, spad = case(beat)
    rng = random.Random(repr(item))
    unit = seed != 'cap'
    vecs = [0x700038A0, 0x700038B0, 0x700038C0, 0x700038D0]
    if unit:
        vecs = [v + rng.choice([0, 0, 4, 0x40]) for v in vecs]
        for v in vecs:
            for i in range(4):
                sput(spad, (v & ~15) + 4 * i, rfloat(rng, -200, 200))
    pick_id = lambda ids: ids[0] if not unit else (rng.choice(ids) if rng.random() < 0.85 else
                                                   (bad_id(ram) if bad_id(ram) is not None else ids[0]))
    f12 = F(10.0) if not unit else rfloat(rng, -5, 50)
    if which == 'eff10':
        bone = w32(ram, PLAYER + 0x110)
        a1 = bone + 0x90 if not unit or rng.random() < 0.7 else FREE
        args = (sx(pick_id([0x8000000D])), sx(a1)) + tuple(sx(v) for v in vecs) + (f12,)
        return run_both(item, spad, [('call', 0x1EFF10, args)])
    if which == 'effd0':
        args = (sx(pick_id([0x80000008, 0x8000005A, 0x8000006A])), sx(vecs[0]), sx(vecs[1]), sx(rng.getrandbits(16) if unit else 7), f12)
        return run_both(item, spad, [('call', 0x1EFFD0, args)])
    if which == 'f0060':
        owner = PLAYER if not unit else rng.choice([PLAYER, 0, FREE, rng.getrandbits(32)])
        return run_both(item, spad, [('call', 0x1F0060, (sx(pick_id([0x80000061, 0x80000062, 0x80000063])), sx(owner)))])
    if which == 'f00a0':
        args = (sx(pick_id([0x80000003, 0x80000019, 0x80000060, 0x8000001B])), sx(vecs[0]), sx(vecs[1]), sx(rng.getrandbits(32) if unit else 1))
        return run_both(item, spad, [('call', 0x1F00A0, args)])
    if which == 'f02c0':
        sid = rng.choice([0xDB, 0x16A, 0x5E0, 0x5DE, rng.randrange(0x600)]) if unit else 0x16A
        return run_both(item, spad, [('call', 0x1F02C0, (sx(vecs[0]), sx(sid), f12 if unit else F(200.0)))])
    if which == 'f4a00':
        return run_both(item, spad, [('call', 0x1F4A00, (sx(0x700038A0), sx(0x700038B0)))])
    return run_both(item, spad, [('call', 0x1F4E20, (sx(0x70003600), sx(0x70003610), f12))])


# ---- the subtype-6 handler 001EF510 ---------------------------------------

def spawn_at(ram, spad, effect, pos, f12=F(1.0)):
    """001EF9D0(effect, pos; f12) as original code; the node."""
    put_vec(ram, FREE + 0x800, pos)
    node, _ = setup_call(ram, spad, 0x1EF9D0, (effect, FREE + 0x800), (f12,))
    assert node, ('001EF9D0 returned no node', hex(effect))
    return node & MASK


def player_pos(ram, dx=0.0, dz=0.0):
    return [F(f32(ram, PLAYER + 0xB0) + dx), F(f32(ram, PLAYER + 0xB4)), F(f32(ram, PLAYER + 0xB8) + dz), F(1.0)]


def ef510_items():
    return [(b, v) for b in BEATS for v in ('zero', 'run')] + [(MAIN[s % len(MAIN)], s) for s in range(30)]


def ef510_case(item):
    beat, v = item
    ram, spad = case(beat)
    rng = random.Random(repr(item))
    node = spawn_at(ram, spad, 0x80000009, player_pos(ram, rng.uniform(-9, 9), rng.uniform(-9, 9)))
    for at, val in ((0x275B44, node), (0x275B48, node), (0x275B40, node + 0x110)):
        put(ram, at, val)
    setup_call(ram, spad, 0x1EA240, (node,))          # the seeding frame (state 0 -> 1)
    depth, _ = setup_call(ram, spad, 0x1CCF70, (node + 0x100,))
    work = node + 0x1F0
    if v == 'zero':
        put(ram, work + 0x54, 0)
    elif v != 'run':
        put(ram, work + 0x54, rng.choice([0, 0x80000000, F(rng.uniform(0, 2)), 1]))
        put(ram, work + 4, rng.getrandbits(32))
    steps = [('poke', 0x275C30, node, 4), ('poke', 0x275C34, work, 4),
             ('call', 0x1EF510, (sx(node + 0xD0), depth))]
    return run_both(item, spad, steps)


# ---- the trail 0x8000000D (001F18C0, 001F1550, 001F15F0) ------------------

def trail_node(ram, spad, rng=None):
    bone = w32(ram, PLAYER + 0x110)
    args = (0x8000000D, bone + 0x90, 0x700038A0, 0x700038B0, 0x700038C0, 0x700038D0)
    ee_regs = {4 + i: sx(v) for i, v in enumerate(args)}
    node, _ = setup_call(ram, spad, 0x1EFF10, (), (F(10.0),), regs=ee_regs)
    assert node, 'no trail node'
    return node & MASK


def trail_items():
    items = [(b, n, 'cap') for b in (PAD, FXB, EXB) for n in (1, 2, 6, 17)]
    items += [(MAIN[s % len(MAIN)], 1 + s % 5, s) for s in range(40)]
    return items


def trail_case(item):
    beat, n, seed = item
    ram, spad = case(beat)
    rng = random.Random(repr(item))
    node = trail_node(ram, spad)
    if seed != 'cap':
        k = seed % 8
        if k == 0:
            put(ram, 0x275BCC, rng.choice([0, 1, 2]), 2)                      # too few bone slots
            n = 2                                                              # state 3, then freed
        elif k == 1:
            put(ram, node + 4, rng.choice([2, 3, 4, 0xFF]), 1)
            n = 1                                                              # freed (2, 3) or idle
        elif k in (2, 3, 4):                                                   # a running strip
            for at, val in ((0x275B44, node), (0x275B48, node), (0x275B40, node + 0x110)):
                put(ram, at, val)
            setup_call(ram, spad, 0x1F18C0, (node,))
            w = node + 0x1F0
            put(ram, w + 4, rng.choice([0, 2, 4, 6, 8]))
            put(ram, w + 8, rng.choice([0, 1, 2]))
            hist = w32(ram, node + 0x110 + 4 * rng.randrange(3))
            for j in range(10):
                put(ram, hist + 4 * j + 0xA0, rng.choice([0, F(-1.0), F(0.5), F(10.0), 0x80000000, F(1e-40)]))
        elif k == 5:
            put(ram, node + 0x1F0, FREE)                 # a strip matrix of zeros: every point behind
    return run_both(item, spad, frames(node, 0x1F18C0, n))


def strip_items():
    return [(MAIN[s % len(MAIN)], s) for s in range(40)]


def strip_case(item):
    """001F15F0 direct over a running strip, every slot and row start."""
    beat, seed = item
    ram, spad = case(beat)
    rng = random.Random(repr(item))
    node = trail_node(ram, spad)
    for at, val in ((0x275B44, node), (0x275B48, node), (0x275B40, node + 0x110)):
        put(ram, at, val)
    setup_call(ram, spad, 0x1F18C0, (node,))
    for i in range(3):
        hist = w32(ram, node + 0x110 + 4 * i)
        for j in range(10):
            put(ram, hist + 4 * j + 0xA0, rng.choice([0, F(3.0), F(-2.0), F(10.0), 0x80000000, F(0.25)]))
    if seed % 9 == 4:
        put(ram, w32(ram, node + 0x1F0) + 0x30, F(-1e6))      # the page lookup's point off screen
    slot, row = rng.randrange(3), rng.choice([0, 2, 4, 6, 8])
    return run_both(item, spad, callback_pokes(node) + [('call', 0x1F15F0, (sx(node), sx(slot), sx(row)))])



# ---- the debris effects (001F2BA0 and its helpers) ------------------------

DEBRIS_IDS = [0x8000000A, 0x8000000B, 0x8000000C, 0x80000021, 0x80000031, 0x80000032, 0x80000036, 0x8000004C,
              0x8000004D, 0x8000005E, 0x8000006E]
KINDS = list(range(0x19)) + [0xFF]


def crate_pos(ram, rng):
    """A barricade crate's position in a00_09 (the breaks there), else the
    player's, with a small offset."""
    crates = [n for n in (0x7AAB70, 0x7AAE60, 0x7AB150, 0x7AB440, 0x7AB730, 0x7ABA20)
              if w32(ram, n + 0x10) == 0x1551B0]
    if crates and rng.random() < 0.7:
        n = rng.choice(crates)
        return [F(f32(ram, n + 0xB0) + rng.uniform(-2, 2)), F(f32(ram, n + 0xB4) + rng.uniform(0, 8)),
                F(f32(ram, n + 0xB8) + rng.uniform(-2, 2)), F(1.0)]
    return player_pos(ram, rng.uniform(-6, 6), rng.uniform(-6, 6))


def debris_node(ram, spad, rng, effect):
    put_vec(ram, FREE + 0x800, crate_pos(ram, rng))
    node, _ = setup_call(ram, spad, 0x1EFD20, (effect, FREE + 0x800))    # it returns the node
    node &= MASK
    assert node and w32(ram, node + 0x10) == 0x1F2BA0, ('debris node', hex(effect), hex(node))
    return node


def seeded_debris(ram, spad, rng, effect):
    node = debris_node(ram, spad, rng, effect)
    for at, val in ((0x275B44, node), (0x275B48, node), (0x275B40, node + 0x110)):
        put(ram, at, val)
    setup_call(ram, spad, 0x1F2BA0, (node,))          # state 0: the pieces are seeded
    return node


def debris_items():
    items = [(FXB, e, n, 'cap') for e in DEBRIS_IDS for n in (1, 3)]
    items += [(FXB, e, 12, 'long') for e in (0x8000000A, 0x80000031, 0x8000004C)]
    items += [(MAIN[s % len(MAIN)], DEBRIS_IDS[s % len(DEBRIS_IDS)], 1 + s % 4, s) for s in range(40)]
    return items


def debris_case(item):
    beat, effect, n, seed = item
    ram, spad = case(beat)
    rng = random.Random(repr(item))
    if seed in ('cap', 'long'):
        node = debris_node(ram, spad, rng, effect)
    else:
        k = seed % 6
        node = seeded_debris(ram, spad, rng, effect) if k in (1, 2, 3, 4) else debris_node(ram, spad, rng, effect)
        if k == 0:
            put(ram, 0x275BCC, rng.choice([0, 1, 5]), 2)                        # too few bone slots
            n = 2
        elif k == 5:
            put(ram, node + 4, rng.choice([2, 3, 4]), 1)
            n = 1
        elif k in (1, 2, 3):                                                      # pieces in varied states
            m = b8(ram, node + 0xC)
            for i in range(m):
                piece = w32(ram, node + 0x110 + 4 * i)
                r = rng.random()
                if r < 0.3:
                    put(ram, piece + 0x80, 1, 2)                                  # already dead
                elif r < 0.5:
                    put(ram, piece + 0x78, rng.choice([0, 1]))
                    put(ram, piece + 0x70, rng.choice([0, 1, 2, 3]))
                    put(ram, piece + 0x74, rng.choice([1, 2, 20, 45]))
            if k == 3:
                for i in range(m):
                    put(ram, w32(ram, node + 0x110 + 4 * i) + 0x80, 1, 2)          # all dead: the node ends
                n = 2
    return run_both(item, spad, frames(node, 0x1F2BA0, n))


def piece_items():
    items = []
    for s in range(120):
        items.append(('3620', MAIN[s % len(MAIN)], s))
    for s in range(80):
        items.append(('3340', MAIN[s % len(MAIN)], s))
    for s in range(60):
        items.append(('2f90', MAIN[s % len(MAIN)], s))
    for s in range(60):
        items.append(('3e30', MAIN[s % len(MAIN)], s))
    for s in range(20):
        items.append(('2e90', MAIN[s % len(MAIN)], s))
    return items


def piece_case(item):
    which, beat, seed = item
    ram, spad = case(beat)
    rng = random.Random(repr(item))
    effect = DEBRIS_IDS[seed % len(DEBRIS_IDS)]
    node = seeded_debris(ram, spad, rng, effect)
    m = b8(ram, node + 0xC)
    piece = w32(ram, node + 0x110 + 4 * rng.randrange(max(1, m)))
    kind = KINDS[seed % len(KINDS)] if seed % 3 else b8(ram, node + 0xD)
    pokes = callback_pokes(node)
    if which == '3620':
        put(ram, piece + 0x70, rng.choice([0, 0, 1, 1, 2, 3, 4]))
        put(ram, piece + 0x74, rng.choice([1, 2, 0x13, 0x14, 0x15, 45]))
        put(ram, piece + 0x7C, rng.choice([0, 0, 1]))
        put(ram, piece + 4, F(rng.choice([5.0, 9.99, 10.0, 60.0, f32(ram, piece + 4)])))
        put(ram, piece + 0x84, F(rng.choice([1.0, 0.04, 0.005, 0.5])))
        put(ram, 0x275C44, rng.choice([0xFFFFFFFF, 5, 0x80000000]))
        put(ram, 0x275C48, rng.getrandbits(32))
        spad[0x3B8D] = rng.choice([0, 0, 1])
        return run_both(item, spad, pokes + [('call', 0x1F3620, (sx(piece), kind))])
    if which == '3340':
        put(ram, piece + 0x10, F(rng.choice([0.0, 0.1, -0.5])))
        put(ram, piece + 0x14, F(rng.choice([-5.0, -0.5, 0.0, 2.0, -40.0])))
        put(ram, piece + 0x18, F(rng.choice([0.0, 0.3, -2.0])))
        if rng.random() < 0.3:
            put(ram, piece + 4, F(f32(ram, piece + 4) + rng.choice([-3.0, 3.0, 30.0])))
        row = 0x25A350 + 0x60 * (kind if kind < 0x19 else b8(ram, node + 0xD))
        return run_both(item, spad, pokes + [('call', 0x1F3340, (sx(piece), sx(row), kind))])
    if which == '2f90':
        row = 0x25A350 + 0x60 * (kind if kind < 0x19 else b8(ram, node + 0xD))
        return run_both(item, spad, pokes + [('call', 0x1F2F90, (sx(node + 0xD0), sx(piece), sx(row), kind))])
    if which == '3e30':
        setup_call(ram, spad, 0x1F3620, (piece, b8(ram, node + 0xD)))     # the piece matrix at 0x700036A0
        if rng.random() < 0.3:
            sput(spad, 0x700036D0, F(1e6))                                   # far away: 001CA7B0 refuses
        sub = rng.choice([0, 1, 2, 4, 5, 6, 10, 17, 18, 22, 23])
        row = 0x25A350 + 0x60 * sub
        a2, a3 = w32(ram, row + 0x50), w32(ram, row + 0x54 + 4 * rng.randrange(2))
        mode = rng.choice([0, 2, w32(ram, row + 0x5C), 1, 3, MASK])
        return run_both(item, spad, pokes + [('call', 0x1F3E30, (sx(0x700036A0), sx(piece + 0x40), sx(a2), sx(a3),
                                                                  sx(mode)))])
    # 2e90
    if seed % 2:
        put(ram, 0x275BCC, rng.choice([0, 1, 2, 3]), 2)
    row = 0x25A350 + 0x60 * rng.choice([0, 1, 2, 4, 5, 6, 10, 17, 18, 22, 23])
    return run_both(item, spad, pokes + [('call', 0x1F2E90, (sx(node), sx(row)))])


# ---- the glow 0x8000000F (001F6FB0) ----------------------------------------

def glow_parents(ram):
    return pool_nodes(ram, {0x128C10}) or [PLAYER]


def glow_items():
    items = [(FXB, i, 'cap', 2) for i in range(3)]
    items += [(MAIN[s % len(MAIN)], s % 3, s, 1 + s % 2) for s in range(36)]
    return items


def glow_case(item):
    beat, which, seed, n = item
    ram, spad = case(beat)
    rng = random.Random(repr(item))
    parents = glow_parents(ram)
    parent = parents[which % len(parents)]
    node, _ = setup_call(ram, spad, 0x1EFE00, (0x8000000F, parent))
    assert node, 'no glow node'
    node &= MASK
    if seed != 'cap':
        k = seed % 6
        w = node + 0x1F0
        if k == 0:
            put(ram, node + 4, rng.choice([2, 3, 4]), 1)
            n = 1
        else:
            for at, val in ((0x275B44, node), (0x275B48, node), (0x275B40, node + 0x110)):
                put(ram, at, val)
            setup_call(ram, spad, 0x1F6FB0, (node,))                 # state 0 -> 1
            put(ram, node + 5, rng.choice([0, 1, 2, 3, 7]), 1)
            put(ram, w, F(rng.choice([0.0, 1.49, 1.5, 1.6, 3.49, 3.5, 3.99, 4.0, 3.3])))
            put(ram, w + 4, F(rng.choice([0.0, 0.5, 0.99, 1.0])))
            if k == 1:
                put(ram, parent + 4, 3, 1)
            if k == 2:
                put(ram, parent + 0x2E4, h16(ram, parent + 0x2E4) ^ 0x1000, 2)
            if k == 3:
                put(ram, w + 0xC, rng.getrandbits(32))
                put(ram, w + 8, rng.getrandbits(32))
    return run_both(item, spad, frames(node, 0x1F6FB0, n))


# ---- the exit-phase effect (001F5040, 001F4F40, 001F4F90) and 001C50B0's helpers

def exit_items():
    items = [(EXB, i, 'live', n) for i in range(3) for n in (1, 4)]
    items += [(EXB, i, 'fresh', 18) for i in range(3)]
    items += [(EXB, s % 3, s, 1 + s % 3) for s in range(40)]
    return items


def exit_case(item):
    beat, which, seed, n = item
    ram, spad = case(beat)
    rng = random.Random(repr(item))
    nodes = pool_nodes(ram, {0x1F5040})
    node = nodes[which % len(nodes)]
    if seed == 'fresh':
        put(ram, node + 4, 0, 1)
        put(ram, node + 0xD, which, 1)
    elif seed != 'live':
        k = seed % 5
        if k == 0:
            put(ram, node + 4, 0, 1)
            put(ram, node + 0xD, rng.choice([0, 1, 2, 3, 4, 5, 9]), 1)
            if rng.random() < 0.4:
                # fewer free bone slots than the model's bones (001AF780 itself
                # refuses below 31, so only a count below the model's is a state
                # the original completes)
                put(ram, 0x275BCC, 0, 2)
                n = 2
        elif k in (1, 2, 3):
            put(ram, node + 4, 1, 1)
            put(ram, node + 0xD, rng.choice([0, 1, 2, 3, 4, 5]), 1)
            put(ram, node + 0x28, rng.choice([0, 1, 2, 3, 4, 14, 15, 0xFFFF]), 2)
        else:
            put(ram, node + 4, rng.choice([2, 3, 4]), 1)
            n = 1
    return run_both(item, spad, frames(node, 0x1F5040, n))


def exit_direct_items():
    items = [('4f40', EXB, s) for s in range(12)] + [('4f90', EXB, s) for s in range(12)]
    items += [('5490', b, s) for b in (EXB, 'a00_s0_shaft_door_back') for s in range(6)]
    items += [('5f60', b, s) for b in (EXB, 'a00_s0_shaft_door_back') for s in range(12)]
    return items


def exit_direct_case(item):
    which, beat, seed = item
    ram, spad = case(beat)
    rng = random.Random(repr(item))
    if which == '4f40':
        return run_both(item, spad, [('call', 0x1F4F40, (sx(rng.choice([0, 1, 2, 3, 4, 0xFF])),))])
    if which == '4f90':
        node = pool_nodes(ram, {0x1F5040})[seed % 3]
        return run_both(item, spad, callback_pokes(node) + [('call', 0x1F4F90, (sx(node), F(rng.choice([2.4, 1.0, 0.5, 30.0]))))])
    node = pool_nodes(ram, {0x1C50B0})
    if not node:
        node = pool_nodes(ram, {0x1BFFD0})
    node = node[0]
    if which == '5490':
        return run_both(item, spad, callback_pokes(node) + [('call', 0x1F5490, (sx(node),))])
    put_vec(ram, FREE + 0x40, [F(rng.uniform(-1, 1)), F(rng.uniform(-1, 1)), F(rng.uniform(-1, 1)), 0])
    if seed % 4 == 3:
        put_vec(ram, node + 0xC0, [F(rng.uniform(-3.2, 3.2)) for _ in range(3)] + [0])
    args = (sx(node + 0xB0), sx(node + 0xC0), sx(FREE + 0x40), sx(w32(ram, node + 0x44)))
    return run_both(item, spad, callback_pokes(node) + [('call', 0x1F5F60, args)])



# ---- targeted edges (each reaches a branch outcome the sweeps above reach
# only by chance; every one is kept in quick mode) -------------------------

def mask_items():
    # +z (bit 4) is never set with these captures' cull matrix: its z column is its w column less 0.2
    items = [('cd940_clip', FXB, (end, bit)) for end in (0, 1) for bit in (0, 1, 2, 3, 5)]
    items += [('aa840_b2', PAD, b2) for b2 in (0x14, 0x24, 0x84, 0x04, 0x05)]
    items += [('effd0_a3', FXB, a3) for a3 in (0x12345678, 0xFFFF8001)]
    items += [('1550_bcc', PAD, bcc) for bcc in (2, 3, 4)]
    items += [('5040_hi', EXB, 0xA5A5), ('5f60_spad', EXB, 0), ('5f60_spad', 'a00_s0_shaft_door_back', 1)]
    items += [('fog_neg', FXB, s) for s in range(4)]
    items += [('6fb0_f20_4', FXB, 0x12345), ('6fb0_negzero_y', FXB, 0)]
    items += [('5040_seed', EXB, s) for s in range(6)]
    # 00122BB8 answers these: the jitter's 0.049999997 and 0.05 differ after the add
    items += [('5040_rand', EXB, r) for r in (0x3C728830, 0x22171EA1, 0x78FEA151)]
    items += [('6dd0_neg', FXB, a1) for a1 in (0x80001234, 0xFFFFFFFF)]
    items += [('pk_cap', FXB, w) for w in ('6dd0', '7510', '7a80')]      # the call-site shapes, every run
    return items


def clip_points(ram, bit):
    """(a point inside the cull matrix's view, one whose clip judgment is
    exactly flag `bit`), found by a fixed search around the player; host
    float arithmetic picks them with a margin."""
    m = [[f32(ram, ctx_of(ram) + 0x2240 + 16 * r + 4 * c) for c in range(4)] for r in range(4)]

    def flags(p):
        v = [m[0][j] * p[0] + m[1][j] * p[1] + m[2][j] * p[2] + m[3][j] for j in range(4)]
        w, f = abs(v[3]), 0
        for k in range(3):
            if abs(v[k]) > w * 1.01:
                f |= (2 if v[k] < 0 else 1) << (2 * k)
            elif abs(v[k]) > w * 0.99:
                return None
        return f
    rng = random.Random(0xC1B + bit)
    px, py, pz = f32(ram, PLAYER + 0xB0), f32(ram, PLAYER + 0xB4), f32(ram, PLAYER + 0xB8)
    inside = out = None
    for _ in range(200000):
        d = rng.choice([5.0, 50.0, 500.0, 5000.0, 50000.0])
        p = (px + rng.uniform(-d, d), py + rng.uniform(-d, d), pz + rng.uniform(-d, d))
        f = flags(p)
        if f == 0 and inside is None:
            inside = p
        if f == 1 << bit and out is None:
            out = p
        if inside and out:
            return [F(x) for x in inside] + [F(1.0)], [F(x) for x in out] + [F(1.0)]
    raise AssertionError(('no point with the single clip flag', bit))


def edge_items():
    items = [('1550', PAD, 0), ('1550', FXB, 1), ('2ba0_mode4', FXB, 0), ('4f40_full', EXB, 0), ('5490_bones', EXB, 0), ('5490_bones', 'a00_s0_shaft_door_back', 0)]
    items += [('5040_init3', EXB, i) for i in range(3)] + [('5040_frame3', EXB, s) for s in (1, 2, 3, 4, 7)]
    for kind in (2, 3, 7, 8, 9, 10, 23, 0, 0xFF):
        for c44 in (0xFFFFFFFF, 5):
            for mute in (0, 1):
                items.append(('3620_s1', FXB, (kind, c44, mute)))
    items += [('3620_vy', FXB, (kind, vy)) for kind in (3, 9) for vy in (-5.0, -2.5, -2.4)]
    return items


def edge_case(item):
    which, beat, arg = item
    ram, spad = case(beat)
    rng = random.Random(repr(item))
    if which == 'effd0_a3':
        return run_both(item, spad, [('call', 0x1EFFD0, (sx(0x80000008), sx(0x700038A0), sx(0x700038B0),
                                                          arg | (0xDEAD << 32), F(1.0)))])
    if which == '1550_bcc':
        node = trail_node(ram, spad)
        put(ram, 0x275BCC, arg, 2)               # at, below and above the three bones
        return run_both(item, spad, callback_pokes(node) + [('call', 0x1F1550, (sx(node), 3))])
    if which == 'pk_cap':
        return pk_case((arg, beat, 'cap'))
    if which == '6dd0_neg':
        return run_both(item, spad, [('call', 0x1D6DD0, (sx(3), sx(arg), sx(0x1234)))])
    if which == '5040_rand':
        node = pool_nodes(ram, {0x1F5040})[0]
        put(ram, node + 4, 0, 1)
        put(ram, node + 0xD, 1, 1)
        inv = pow(0x41C64E6D, -1, 1 << 32)
        put(ram, w32(ram, 0x24295C) + 0x58, ((arg - 0x3039) * inv) & MASK)     # the next answer is arg
        return run_both(item, spad, frames(node, 0x1F5040, 1),
                        check=lambda ee: any(c[0] == 0x122BB8 and c[-1] == arg for c in ee.calls))
    if which in ('5040_hi', '5040_seed'):
        node = pool_nodes(ram, {0x1F5040})[0]
        if which == '5040_hi':
            put(ram, node + 0x2A, arg, 2)         # the halfword above the frame counter
            return run_both(item, spad, frames(node, 0x1F5040, 2))
        put(ram, node + 4, 0, 1)
        put(ram, node + 0xD, arg % 5, 1)
        put(ram, w32(ram, 0x24295C) + 0x58, random.Random(arg).getrandbits(31))   # the LCG state
        return run_both(item, spad, frames(node, 0x1F5040, 1))
    if which == '5f60_spad':
        for i in range(0x60, 0x70, 4):
            sput(spad, 0x70003400 + i, 0x3F800000 + 0x1111 * i)
        node = (pool_nodes(ram, {0x1C50B0}) or pool_nodes(ram, {0x1BFFD0}))[0]
        put_vec(ram, FREE + 0x40, [F(0.5), F(-0.25), F(0.75), 0])
        args = (sx(node + 0xB0), sx(node + 0xC0), sx(FREE + 0x40), sx(w32(ram, node + 0x44)))
        return run_both(item, spad, callback_pokes(node) + [('call', 0x1F5F60, args)])
    if which == 'fog_neg':
        # a negative fog limit (context +0xA0 x): the order of the VU min and max matters
        put_vec(ram, ctx_of(ram) + 0xA0, [F(-5.0), 0, F(100.0), F(-0.5)])
        if arg < 2:
            p_in, _ = clip_points(ram, 0)
            put_vec(ram, FREE, p_in)
            put_vec(ram, FREE + 0x20, p_in)
            return run_both(item, spad, [('call', 0x1CD940, (sx(arg), sx(FREE), sx(FREE), sx(FREE + 0x20),
                                                              sx(FREE)))])
        node = trail_node(ram, spad)
        for at, val in ((0x275B44, node), (0x275B48, node), (0x275B40, node + 0x110)):
            put(ram, at, val)
        setup_call(ram, spad, 0x1F18C0, (node,))
        return run_both(item, spad, callback_pokes(node) + [('call', 0x1F15F0, (sx(node), sx(arg - 2), 0))])
    if which in ('6fb0_f20_4', '6fb0_negzero_y'):
        parent = glow_parents(ram)[0]
        node, _ = setup_call(ram, spad, 0x1EFE00, (0x8000000F, parent))
        node &= MASK
        for at, val in ((0x275B44, node), (0x275B48, node), (0x275B40, node + 0x110)):
            put(ram, at, val)
        setup_call(ram, spad, 0x1F6FB0, (node,))
        w = node + 0x1F0
        if which == '6fb0_f20_4':
            # the first spark's f20 = 2 * phase - 2 * k is exactly 4.0 (the
            # upper bound of the [0, 4] test)
            import ee_float_model as M
            lcg = arg
            k = M.ee_add(M.ee_div(M.ee_cvt_s_w((lcg >> 16) & 0xFFFF), 0x477FFF00), 0x38D1B717)
            p = F(2.0 + struct.unpack('<f', struct.pack('<I', k))[0])
            for d in range(-64, 65):
                if M.ee_msub(M.ee_mula(0x40000000, p + d), 0x40000000, k) == 0x40800000:
                    p += d
                    break
            else:
                raise AssertionError('no phase with f20 = 4.0')
            put(ram, w + 8, lcg)
            put(ram, w, p)
            put(ram, node + 5, 3, 1)
        else:
            # the first spark: f20 = 2.0 exactly (so its fraction is +0), an
            # angle in (0, pi/2) (every lane of 0x700038B0 positive) and the
            # parent's matrix y column (its position's y for the fourth row) -0:
            # the transformed y lane is -0,
            # which the zero add (vaddx.y) turns into +0
            import ee_float_model as M
            rs = random.Random(7)
            for _ in range(100000):
                lcg = rs.getrandbits(32)
                k = M.ee_add(M.ee_div(M.ee_cvt_s_w((lcg >> 16) & 0xFFFF), 0x477FFF00), 0x38D1B717)
                l1 = (lcg * 37 + 11) & MASK
                q1 = ((l1 >> 16) & 0xFFFF) / 65535 + 1e-4
                ang = 6.2831853 * q1 - 3.1415927
                if not 0.05 < ang < 1.5:
                    continue
                p = F(1.0 + struct.unpack('<f', struct.pack('<I', k))[0])
                for d in range(-64, 65):
                    if M.ee_msub(M.ee_mula(0x40000000, p + d), 0x40000000, k) == 0x40000000:
                        p += d
                        break
                else:
                    continue
                break
            else:
                raise AssertionError('no spark state found')
            put(ram, w + 8, lcg)
            put(ram, w, p)
            put(ram, node + 5, 3, 1)
            for r in range(3):
                put(ram, parent + 0xD0 + 16 * r + 4, 0x80000000)
            put(ram, parent + 0xB4, 0x80000000)          # the matrix's fourth row is the parent's position
        return run_both(item, spad, frames(node, 0x1F6FB0, 1))
    if which == 'cd940_clip':
        end, bit = arg
        p_in, p_out = clip_points(ram, bit)
        put_vec(ram, FREE, p_out if end == 0 else p_in)
        put_vec(ram, FREE + 0x20, p_in if end == 0 else p_out)
        put_vec(ram, FREE + 0x10, [0x80] * 4)
        put_vec(ram, FREE + 0x30, [0x40] * 4)
        return run_both(item, spad, [('call', 0x1CD940, (sx(1), sx(FREE), sx(FREE + 0x10), sx(FREE + 0x20),
                                                          sx(FREE + 0x30)))])
    if which == 'aa840_b2':
        put(ram, 0x275B40, PLAYER + 0x110)
        e = w32(ram, w32(ram, 0x275B7C))
        put(ram, e, 1, 1)
        put(ram, e + 2, arg, 1)
        put(ram, e + 3, 0x29, 1)
        bone = w32(ram, PLAYER + 0x110)
        for off in (0xB0, 0xB4, 0xB8):
            put(ram, e + off, w32(ram, bone + 0xC0 + off - 0xB0))
        put(ram, e + 0xB4, F(f32(ram, bone + 0xC4) - 10.0))
        return run_both(item, spad, [('call', 0x1AA840, ())])
    if which == '1550':
        node = trail_node(ram, spad)
        steps = callback_pokes(node) + [('call', 0x1F1550, (sx(node), 3))]
        return run_both(item, spad, steps)
    if which == '2ba0_mode4':
        node = debris_node(ram, spad, rng, 0x8000000A)
        put(ram, node + 4, 4, 1)
        return run_both(item, spad, frames(node, 0x1F2BA0, 1))
    if which == '4f40_full':
        put(ram, 0x275BC8, 5, 2)                 # fewer than 10 free nodes: 001AFA90 refuses class 0xC
        return run_both(item, spad, [('call', 0x1F4F40, (sx(2),))])
    if which == '5490_bones':
        node = (pool_nodes(ram, {0x1C50B0}) or pool_nodes(ram, {0x1BFFD0}))[0]
        put(ram, 0x275BCC, 0, 2)                 # 001C22A0 finds too few bone slots and returns 1
        return run_both(item, spad, callback_pokes(node) + [('call', 0x1F5490, (sx(node),))])
    if which in ('5040_init3', '5040_frame3'):
        node = pool_nodes(ram, {0x1F5040})[arg % 3]
        if which == '5040_init3':
            put(ram, node + 4, 0, 1)
            put(ram, node + 0xD, [3, 4, 5][arg], 1)
            return run_both(item, spad, frames(node, 0x1F5040, 1))
        put(ram, node + 4, 1, 1)
        put(ram, node + 0xD, arg, 1)
        put(ram, node + 0x28, 3, 2)
        return run_both(item, spad, frames(node, 0x1F5040, 1))
    node = seeded_debris(ram, spad, rng, 0x8000000A)
    piece = w32(ram, node + 0x110)
    put(ram, piece + 0x70, 1)
    put(ram, piece + 0x7C, 0)
    if which == '3620_s1':
        kind, c44, mute = arg
        put(ram, 0x275C44, c44)
        spad[0x3B8D] = mute
    else:
        kind, vy = arg
        put(ram, piece + 0x14, F(vy))
        put(ram, 0x275C44, 5)
    return run_both(item, spad, callback_pokes(node) + [('call', 0x1F3620, (sx(piece), kind))])


def case_groups(everything=False):
    """(name, selected items, total, case function) per group. Quick mode
    runs QUICK_PINS (an outcome cover of the full sweep, `cover` command)
    plus a fixed-seed sample of each group; full mode runs everything."""
    groups = []
    for n, (name, items_fn, count, fn) in enumerate(GROUPS):
        items = items_fn()
        pinned = sum(1 for it in items if repr(it) in QUICK_PINS)
        chosen = items if everything else RM.select(items, pinned + count, 101 + n,
                                                    keep=lambda i, it: repr(it) in QUICK_PINS)
        groups.append((name, chosen, len(items), fn))
    if not everything:      # a pin that matches no generated item would be silently dropped
        every = {repr(it) for _, items_fn, _, _ in GROUPS for it in items_fn()}
        lost = sorted(QUICK_PINS - every)
        assert not lost, ('pins not among the generated items', lost)
    return groups


GROUPS = [   # (name, items, quick-mode sample beyond the pins, case)
    ('001AA840', aa840_items, 3, aa840_case),
    ('packets', pk_items, 6, pk_case),
    ('001CD940', cd940_items, 3, cd940_case),
    ('spawn wrappers', sp_items, 6, sp_case),
    ('001EF510', ef510_items, 1, ef510_case),
    ('trail', trail_items, 1, trail_case),
    ('001F15F0', strip_items, 1, strip_case),
    ('debris', debris_items, 0, debris_case),
    ('pieces', piece_items, 2, piece_case),
    ('glow', glow_items, 0, glow_case),
    ('exit', exit_items, 1, exit_case),
    ('exit helpers', exit_direct_items, 2, exit_direct_case),
    ('edges', edge_items, 0, edge_case),
    ('clip and mask edges', mask_items, 1000, edge_case),
]

# repr() of the items the outcome cover chose (`python3 tools/test_area00_fx_reference.py
# cover` prints this set: a small set of items reaching every branch outcome the full
# sweep reaches, cheapest first)
QUICK_PINS = {
    "('2ba0_mode4', 'a00_09_ne_room_out', 0)",
    "('2f90', 'a00_00_descend', 55)",
    "('2f90', 'a00_02_south_route', 35)",
    "('2f90', 'a00_07_duct_to_ne_room', 7)",
    "('2f90', 'a00_08_switch', 8)",
    "('3340', 'a00_00_descend', 55)",
    "('3340', 'a00_01_door51_locked', 34)",
    "('3340', 'a00_02_south_route', 24)",
    "('3340', 'a00_02_south_route', 35)",
    "('3340', 'a00_02_south_route', 46)",
    "('3340', 'a00_03_padlock', 58)",
    "('3340', 'a00_03_padlock', 69)",
    "('3340', 'a00_04_cage_terminal', 48)",
    "('3340', 'a00_07_duct_to_ne_room', 7)",
    "('3340', 'a00_08_switch', 52)",
    "('3340', 'a00_09_ne_room_out', 53)",
    "('3340', 'a00_10_progression_exit', 10)",
    "('3340', 'a00_10_progression_exit', 21)",
    "('3620', 'a00_02_south_route', 101)",
    "('3620', 'a00_02_south_route', 112)",
    "('3620', 'a00_04_cage_terminal', 4)",
    "('3620', 'a00_04_cage_terminal', 59)",
    "('3620', 'a00_05_ferry_deck', 104)",
    "('3620', 'a00_05_ferry_deck', 60)",
    "('3620', 'a00_05_ferry_deck', 93)",
    "('3620', 'a00_07_duct_to_ne_room', 51)",
    "('3620', 'a00_07_duct_to_ne_room', 62)",
    "('3620', 'a00_07_duct_to_ne_room', 84)",
    "('3620', 'a00_08_switch', 8)",
    "('3620', 'a00_08_switch', 85)",
    "('3620_s1', 'a00_09_ne_room_out', (2, 5, 0))",
    "('3620_s1', 'a00_09_ne_room_out', (3, 4294967295, 0))",
    "('3620_s1', 'a00_09_ne_room_out', (7, 4294967295, 0))",
    "('3620_s1', 'a00_09_ne_room_out', (8, 4294967295, 0))",
    "('3620_s1', 'a00_09_ne_room_out', (9, 4294967295, 0))",
    "('3620_vy', 'a00_09_ne_room_out', (9, -5.0))",
    "('3e30', 'a00_02_south_route', 57)",
    "('3e30', 'a00_03_padlock', 47)",
    "('3e30', 'a00_06_cab_roof', 28)",
    "('4f40', 'a00_10_progression_exit', 7)",
    "('4f40_full', 'a00_10_progression_exit', 0)",
    "('4f90', 'a00_10_progression_exit', 8)",
    "('5040_frame3', 'a00_10_progression_exit', 1)",
    "('5040_frame3', 'a00_10_progression_exit', 2)",
    "('5040_frame3', 'a00_10_progression_exit', 3)",
    "('5040_frame3', 'a00_10_progression_exit', 4)",
    "('5040_frame3', 'a00_10_progression_exit', 7)",
    "('5040_init3', 'a00_10_progression_exit', 0)",
    "('5040_init3', 'a00_10_progression_exit', 1)",
    "('5040_init3', 'a00_10_progression_exit', 2)",
    "('5490', 'a00_s0_shaft_door_back', 4)",
    "('5490_bones', 'a00_10_progression_exit', 0)",
    "('a00_00_descend', 2147483658, 1, 0)",
    "('a00_01_door51_locked', 'zero')",
    "('a00_01_door51_locked', 2, 1)",
    "('a00_01_door51_locked', 2147483659, 1, 12)",
    "('a00_01_door51_locked', 2147483659, 2, 1)",
    "('a00_01_door51_locked', 67)",
    "('a00_02_south_route', 0, 24, 1)",
    "('a00_02_south_route', 1, 13, 2)",
    "('a00_02_south_route', 2, 35, 2)",
    "('a00_02_south_route', 5, 24)",
    "('a00_03_padlock', 2147483681, 1, 36)",
    "('a00_03_padlock', 58)",
    "('a00_04_cage_terminal', 1, 15)",
    "('a00_04_cage_terminal', 1, 4, 1)",
    "('a00_04_cage_terminal', 2147483697, 1, 4)",
    "('a00_04_cage_terminal', 48)",
    "('a00_05_ferry_deck', 0, 27, 2)",
    "('a00_05_ferry_deck', 1, 16, 1)",
    "('a00_05_ferry_deck', 2, 5, 2)",
    "('a00_06_cab_roof', 0, 6, 1)",
    "('a00_06_cab_roof', 2147483702, 2, 17)",
    "('a00_06_cab_roof', 3, 17)",
    "('a00_08_switch', 2, 8, 1)",
    "('a00_08_switch', 2147483725, 1, 8)",
    "('a00_08_switch', 63)",
    "('a00_09_ne_room_out', 'run')",
    "('a00_09_ne_room_out', 0, 'cap', 2)",
    "('a00_09_ne_room_out', 2, 20, 1)",
    "('a00_09_ne_room_out', 2147483697, 1, 'cap')",
    "('a00_09_ne_room_out', 2147483698, 1, 'cap')",
    "('a00_09_ne_room_out', 2147483724, 3, 'cap')",
    "('a00_09_ne_room_out', 2147483725, 1, 'cap')",
    "('a00_09_ne_room_out', 2147483742, 4, 31)",
    "('a00_10_progression_exit', 0, 0, 1)",
    "('a00_10_progression_exit', 0, 15, 1)",
    "('a00_10_progression_exit', 0, 24, 1)",
    "('a00_10_progression_exit', 0, 3, 1)",
    "('a00_10_progression_exit', 0, 36, 1)",
    "('a00_10_progression_exit', 17, 'cap')",
    "('a00_10_progression_exit', 2, 'live', 1)",
    "('a00_10_progression_exit', 2, 14, 3)",
    "('a00_10_progression_exit', 2, 26, 3)",
    "('a00_10_progression_exit', 2, 35, 3)",
    "('a00_10_progression_exit', 2147483758, 3, 10)",
    "('eff10', 'a00_03_padlock', 25)",
    "('eff10', 'a00_04_cage_terminal', 4)",
    "('effd0', 'a00_00_descend', 33)",
    "('effd0', 'a00_03_padlock', 25)",
    "('empty', 'a00_03_padlock', 0)",
    "('f0060', 'a00_03_padlock', 3)",
    "('f0060', 'a00_04_cage_terminal', 26)",
    "('f00a0', 'a00_00_descend', 33)",
    "('f00a0', 'a00_01_door51_locked', 1)",
    "('unit', 'a00_10_progression_exit', 38)",
    "('unit', 'a00_10_progression_exit', 6)",
}

# Branch outcomes no case reaches, with the reason (docs/AREA00_FX.md section 3).
UNREACHED = {
    (0x1AA854, True): 'the second test of the list count runs only after the first found it nonzero, '
                      'and nothing changes the register in between',
    (0x1CDB54, False): 'the fog word is the 12.4 conversion of a lane just raised to at least +0 (raw VU '
                       'max), so it is not negative; >> 4 keeps it so',
    (0x1CDC54, False): 'the same for the second end',
    (0x1F2D70, False): 'the piece index counts up from 0 while below a byte (+0xC)',
    (0x1F2D78, False): 'reached only with a negative piece index (above)',
    (0x1F2D78, True): 'reached only with a negative piece index (above)',
    (0x1F3490, True): 'the sweep count 0x78 / step is 30, 20 or 10 (step 4, 6 or 12)',
    (0x1F35E0, False): 'not reached: +0x74 is set to 0, grows by the positive step and is read back '
                       'after 0019A570 / 001031E0 (their stores in every case run: the scratchpad and '
                       'their stack) and the lane\'s own stores to +0x50..+0x6F',
    (0x1F3B58, False): 'the sound dispatch runs only for kinds 9, 8, 7 and 3 (its enclosing test)',
}


def run_groups(faults, t0):
    outcomes, cases, unmeasured, calls, entries = set(), 0, 0, 0, 0

    def absorb(results):
        nonlocal cases, unmeasured, calls, entries
        for r in results:
            cases += 1
            if r == 'unmeasured':
                unmeasured += 1
            else:
                outcomes.update(r[0])
                calls += r[1]
                entries += r[2]

    counts = []
    for name, items, total, fn in case_groups(RM.FULL):
        before = cases
        tg = time.time()
        absorb(pmap(fn, items))
        if os.environ.get('EM_A00FX_TIMES'):
            print('%-16s %4d cases %.1f s' % (name, len(items), time.time() - tg), flush=True)
        counts.append(RM.part(cases - before, total, name + ' cases'))
    branches = all_branches()
    need = {(pc, v) for pc in branches for v in (True, False)}
    missing = sorted((hex(pc), v) for pc, v in need - outcomes if (pc, v) not in UNREACHED)
    wrongly = sorted((hex(pc), v) for pc, v in outcomes & set(UNREACHED))
    RM.banner(*counts, f'{cases} cases in all ({unmeasured} refused on both sides)',
              f'{calls} callee calls, {entries} entries with RAM + scratchpad + arguments equal to the original\'s',
              f'{len(branches)} conditional branches: {len(need) - len(missing) - len(UNREACHED)} of {len(need)} '
              f'outcomes seen, {len(UNREACHED)} not reachable (listed)',
              f'{faults} fault cases')
    assert not wrongly, ('an outcome listed as unreachable was reached', wrongly)
    assert not missing, ('branch outcomes never seen', missing)
    print('elapsed %.1f s' % (time.time() - t0))
    print('area00 fx reference: PASS')


POOL = None


def pmap(fn, items):
    items = list(items)
    if POOL is None or len(items) < 2:
        return [fn(x) for x in items]
    size = max(1, -(-len(items) // (3 * JOBS)))
    return list(POOL.imap(fn, items, chunksize=size))


def fault_cases():
    return R.in_child(fault_group, 'fault cases')


# The fault group builds each entry's state with the case functions above
# (RECORD makes run_both hand back the spad and steps instead of running).
RECORD = None


def recorded(fn, item):
    global RECORD
    RECORD = []
    try:
        fn(item)
        return RECORD[0]
    finally:
        RECORD = None


# one representative state per entry: (entry, group case function, item)
FAULT_PLANS = [
    (0x1CD940, 'cd940_case', (FXB, 25)), (0x1D6DD0, 'pk_case', ('6dd0', FXB, 'cap')),
    (0x1D7510, 'pk_case', ('7510', FXB, 'cap')), (0x1D7A80, 'pk_case', ('7a80', FXB, 'cap')),
    (0x1F4A00, 'sp_case', ('f4a00', FXB, 'cap')), (0x1F4E20, 'sp_case', ('f4e20', FXB, 'cap')),
    (0x1AA840, 'aa840_case', ('cap', EXB, 0)), (0x1EF510, 'ef510_case', (FXB, 'zero')),
    (0x1EFF10, 'sp_case', ('eff10', FXB, 'cap')), (0x1EFFD0, 'sp_case', ('effd0', FXB, 'cap')),
    (0x1F0060, 'sp_case', ('f0060', FXB, 'cap')), (0x1F00A0, 'sp_case', ('f00a0', FXB, 'cap')),
    (0x1F02C0, 'sp_case', ('f02c0', FXB, 'cap')), (0x1F1550, 'edge_case', ('1550', PAD, 0)),
    (0x1F15F0, 'strip_case', (FXB, 1)), (0x1F18C0, 'trail_case', (PAD, 1, 'cap')),
    (0x1F2BA0, 'debris_case', (FXB, 0x8000000A, 1, 'cap')), (0x1F2E90, 'piece_case', ('2e90', FXB, 0)),
    (0x1F2F90, 'piece_case', ('2f90', FXB, 0)), (0x1F3340, 'piece_case', ('3340', FXB, 7)),
    (0x1F3620, 'piece_case', ('3620', FXB, 1)), (0x1F3E30, 'piece_case', ('3e30', FXB, 0)),
    (0x1F4F40, 'exit_direct_case', ('4f40', EXB, 0)), (0x1F4F90, 'exit_direct_case', ('4f90', EXB, 0)),
    (0x1F5040, 'exit_case', (EXB, 0, 'fresh', 18)), (0x1F5490, 'exit_direct_case', ('5490', EXB, 0)),
    (0x1F5F60, 'exit_direct_case', ('5f60', EXB, 0)), (0x1F6FB0, 'glow_case', (FXB, 0, 'cap', 2)),
]
NO_MEMORY = {0x1F4A00, 0x1F4E20, 0x1F5490}      # entries that make calls and nothing else of their own
FAULT_CALLS = {}


def first_call_steps(steps, address):
    """The steps up to the first call of `address` (pokes before it kept)."""
    out = []
    for s in steps:
        out.append(s)
        if s[0] == 'call' and s[1] == address:
            return out
    raise AssertionError(('no call of', hex(address)))


def oracle_run(spad, steps):
    ee = oracle_ee(spad)
    oracle_hooks(ee)
    for s in steps:
        if s[0] == 'poke':
            ee.save(s[1], s[2], s[3])
        else:
            oracle_call(ee, s[1], s[2], STACK_TOP)
    return ee


def expected_at(n, ee, k):
    """The native RAM and scratchpad equal the oracle's at its call entry k
    (k = len(entries): the end), counting the pokes both sides made."""
    stop = ee.entries[k][0] if k < len(ee.entries) else len(ee.journal)
    W = n.W
    R.sync(W.expect, W.scratch.data, W.scratch.key, W.scratch.dirty)
    ram, spad, touched = W.expect.data, bytearray(n.spad0), set()
    for is_spad, at, data in ee.journal[:stop]:
        if is_spad:
            spad[at:at + len(data)] = data
        else:
            ram[at:at + len(data)] = data
            touched.update(R.pages(at, len(data)))
    W.expect.dirty |= touched
    bad = R.page_differences(n.ee.mem, ram, touched | W.take() | set(W.native.dirty))
    return not bad and bytes(n.ee.spad) == bytes(spad)


def run_native(spad, steps, latch=False, **kw):
    n = Native(bytes(spad), **kw)
    n.spad0 = bytes(spad)
    rcs = []
    for s in steps:
        if s[0] == 'poke':
            n.poke(s[1], s[2], s[3])
        else:
            n.state.sp = STACK_TOP
            if latch:
                n.state.core.fault.code = 6
                n.W.take()
            rcs.append(n.call(s[1], s[2])[0])
    return n, rcs


def fault_group():
    """Per entry, from a real case state: a NULL `call`, no views, a latched
    fault, and a failing callee at call positions (quick: first, second,
    middle, last; full: every call). After the fault the RAM and scratchpad
    must equal the original's at the faulting point (nothing written after
    it) and no further callee may be called."""
    count = 0
    assert sorted(a for a, _, _ in FAULT_PLANS) == sorted(ENTRIES)
    for address, fname, item in FAULT_PLANS:
        note('fault %08X %r' % (address, item))
        spad, steps = recorded(globals()[fname], item)
        steps = first_call_steps(steps, address)
        ee = oracle_run(spad, steps)
        ncalls = len(ee.entries)
        FAULT_CALLS[address] = ncalls
        # latched: refuses without a call or a store
        n, rcs = run_native(spad, steps, latch=True)
        assert rcs[-1] == -1 and n.log == [] and not n.W.take() and bytes(n.ee.spad) == n.spad0, \
            (hex(address), 'latched')
        count += 1
        # no views: code 4 by the routine itself (the entries of NO_MEMORY only call)
        n, rcs = run_native(spad, steps, views=False)
        f = n.state.core.fault
        if address in NO_MEMORY:
            assert rcs[-1] == 0 and f.code == 0, (hex(address), 'no views', rcs, f.code)
        else:
            assert rcs[-1] == -1 and f.code == 4 and f.address in SIZES, (hex(address), 'no views', f.code, hex(f.address))
        count += 1
        if ncalls == 0:
            continue
        # NULL call: code 1 naming the first callee, nothing written after it
        n, rcs = run_native(spad, steps, null_call=True)
        f = n.state.core.fault
        first = ee.entries[0][1][0]
        assert rcs[-1] == -1 and f.code == 1 and f.address == first, (hex(address), 'NULL call', f.code, hex(f.address))
        assert expected_at(n, ee, 0), (hex(address), 'NULL call: memory written after the fault')
        count += 1
        # a failing callee at call positions
        # quick: the first, second, middle and last call; full: every call
        # (every 7th, with the last, past 40 calls)
        if RM.FULL:
            ks = range(ncalls) if ncalls <= 40 else sorted(set(range(0, ncalls, 7)) | {ncalls - 1})
        else:
            ks = sorted({0, 1, ncalls // 2, ncalls - 1} & set(range(ncalls)))
        for k in ks:
            n, rcs = run_native(spad, steps, fail_at=k, oracle=ee)
            f = n.state.core.fault
            assert n.mismatch is None, (hex(address), 'failing callee', k) + n.mismatch
            assert rcs[-1] == -1 and f.code == 2 and f.address == n.log[k][0] and len(n.log) == k + 1, \
                (hex(address), 'failing callee', k, rcs, f.code, len(n.log))
            assert expected_at(n, ee, k), (hex(address), 'failing callee', k, 'memory written after the fault')
            count += 1
    return count


def note(text):
    R.note(text)


def main():
    global POOL
    t0 = time.time()
    prepare()
    faults = fault_cases()
    if JOBS > 1 and 'fork' in multiprocessing.get_all_start_methods():
        POOL = multiprocessing.get_context('fork').Pool(JOBS)
    try:
        run_groups(faults, t0)
    finally:
        if POOL is not None:
            R.close_pool(POOL)
            POOL = None


def cover_item(arg):
    name, it = arg
    fn = {g[0]: g[3] for g in GROUPS}[name]
    t = time.time()
    r = fn(it)
    return repr(it), (r[0] if isinstance(r, tuple) else set()), time.time() - t


def cover():
    """Run every item; print a small set of items (greedy, by outcomes per
    second) that reaches every outcome the full sweep reaches."""
    global POOL
    prepare()
    work = [(name, it) for name, items_fn, _, _ in GROUPS for it in items_fn()]
    POOL = multiprocessing.get_context('fork').Pool(JOBS) if JOBS > 1 else None
    try:
        rows = pmap(cover_item, work)
    finally:
        if POOL is not None:
            R.close_pool(POOL)
    left = set().union(*(r[1] for r in rows))
    chosen = []
    while left:
        best = max(rows, key=lambda r: len(r[1] & left) / (r[2] + 0.05))
        if not best[1] & left:
            break
        chosen.append(best[0])
        left -= best[1]
    print('QUICK_PINS = {')
    for c in sorted(chosen):
        print('    %r,' % c)
    print('}')
    print('# %d pins, %.1f s of cases' % (len(chosen), sum(r[2] for r in rows if r[0] in chosen)))


# ======================================================================
# Main
# ======================================================================

def prepare():
    global ELF, NATIVE
    ELF = read_elf()
    R.ELF = ELF
    NATIVE = build_native()
    if R.TRACKER is None:
        R.TRACKER = R.build_tracker()
    assert BEATS, 'no AREA00 captures under ' + str(ROUTE)
    got = callee_set()
    assert got == set(WSPEC) | INLINE, ('callee set', sorted(map(hex, got ^ (set(WSPEC) | INLINE))))
    # the indirect targets the cases reach: 001F5040's +0x4C in the exit capture
    exb = R.image(EXB)[0]
    indirect = {w32(exb, n + 0x4C) for n in pool_nodes(exb, {0x1F5040})}
    bad = check_register_reads(exb, set(WSPEC) | indirect)
    assert not bad, ('callee register reads outside the policy', bad)
    base = EE(ELF)
    ref = R.image(BEATS[0])[0]
    for beat in BEATS:
        img = R.image(beat)[0]
        check_code(img, beat, base)
        R.DIFF[beat] = frozenset(p for p in range(R.RAM_SIZE // R.PAGE)
                                 if img[p * R.PAGE:(p + 1) * R.PAGE] != ref[p * R.PAGE:(p + 1) * R.PAGE])


def debug(names):
    """Run the named groups serially, printing each failing item."""
    prepare()
    seen = set()
    for name, items, _, fn in case_groups(everything=True):
        if names and name not in names:
            continue
        ok = 0
        t = time.time()
        for it in items:
            try:
                r = fn(it)
                if isinstance(r, tuple):
                    seen |= r[0]
                ok += 1
            except Exception as ex:  # noqa: BLE001
                print('FAIL', name, it, repr(ex)[:1500])
        print(name, ok, 'of', len(items), '%.1f s' % (time.time() - t))
    need = {(pc, v) for pc in all_branches() for v in (True, False)}
    missing = sorted(need - seen)
    by = {}
    for pc, v in missing:
        fn = max(a for a in SIZES if a <= pc)
        by.setdefault(fn, []).append((hex(pc), v))
    for fn, rows in sorted(by.items()):
        print('missing %08X:' % fn, rows)


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == 'debug':
        debug(sys.argv[2:])
    elif len(sys.argv) > 1 and sys.argv[1] == 'cover':
        cover()
    else:
        main()
