#!/usr/bin/env python3
"""Execute the ORIGINAL AREA01 lane-UI routines and compare the native
em_area01_ui_* translations byte for byte.

docs/AREA01_UI.md. The user's pinned ELF and the recorded AREA01 route
captures (../Extermination/build/s87/route_a01/<beat>/{eeMemory,scratchpad}.bin)
supply every instruction and every table; none are embedded here.

Translated routines (each run as ORIGINAL code on the oracle side):
  status pages  0020F950 002101C0 00210030 00210A00 00210C00 00210F30
                00211400 00214020 002131B0 002134C0 00213F30 00207D90
                00208040
  effect        0022BBC0 0022B7A0 0022B700 0022BB70
The leaves 00102948 and 00102958 run inline on both sides.

Method (the render lane's, tools/test_area01_render_reference.py, whose
buffers, dirty-page tracker, entry check and EE access rules are imported):
the oracle runs the original routine over a captured beat's RAM and
scratchpad; every callee outside the lane runs as ORIGINAL code too, nested,
and its entry is logged: the address, the stack pointer, every integer
argument register it takes (full 64-bit images, a0..a3 t0..t2) and every
float argument register (f12..). The native module runs over its own copy;
each callee it reaches through EmArea01Ui.call runs the same ORIGINAL code
in a second interpreter sharing the native memory. Before every such call
(the entry check) all 32 MiB of RAM and the 16 KiB scratchpad must equal the
oracle's at the same call (the oracle's store journal replayed onto the
starting image), with the logged entry; after the last step the results,
the call log, RAM and scratchpad must be identical. Registers a callee is
not passed are poisoned on the native side. An indirect call (002101C0's
node +0x4C) is logged by its loaded target. Only executions the EE
completes are run (aligned accesses inside the captures' TLB map).

Cases (docs/AREA01_UI.md section 3 lists them with counts):
  capture  the real call shapes the side beats reached, rebuilt on the
           beats' end images: page 3 (00214020, status block t =
           D_00810130) opened by the a01_s1 / a01_s2 pickups (request 3,
           the captured record ids) and run through its states; page 1
           (0020F950) opened, its 22 nodes allocated and ticked; the a01_s3
           burn node (0x7B0F50, 0022BBC0) as captured, as pool callbacks
           run (D_00275B44 / 48 = node, D_00275B40 = node + 0x110).
  unit     the same routines over perturbed state: every page state, pad
           edges and repeats, cursor wraps, owned-map patterns, area bytes,
           record ids across the category bounds, page counts, 0022BBC0
           variants 0..9, crafted timelines with every op, ramps, emitter
           phases and wraps, burst kinds, ring ages / kinds, direct calls
           of every helper.
  faults   native only: NULL call, a failing callee at each call position
           (quick: first, second, last), no views, a latched fault, and the
           code-6 cases (00213F30's caller registers, 0022BBC0's period /
           burst registers, a zero timeline divisor).
Every conditional branch of the 17 routines must be taken both ways except
the ones docs/AREA01_UI.md section 3 lists as unreachable.

EM_TEST_FULL=1 runs every generated case; the default runs a covering
sample. At most four worker processes (EM_TEST_JOBS=1: serial).
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
os.environ.setdefault('EM_LANE', 'ui')          # the render machinery builds under build/area01/<EM_LANE>
import reference_mode as RM  # noqa: E402
import test_area01_render_reference as R  # noqa: E402
import test_shadow_actor_route_reference as SAR  # noqa: E402
from test_player_slide_reference import EE, read_elf, sx32, RETURN, STACK_TOP  # noqa: E402

MASK, MASK64 = 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF
OUT = ROOT / 'build' / 'area01' / os.environ['EM_LANE']
JOBS = R.JOBS
F = R.F

SIZES = {  # the translated routines (original bytes)
    0x207D90: 0xAC, 0x208040: 0x160, 0x20F950: 0x6D8, 0x210030: 0x188, 0x2101C0: 0x83C, 0x210A00: 0x1FC,
    0x210C00: 0x32C, 0x210F30: 0x30C, 0x211400: 0x3D0, 0x2131B0: 0x308, 0x2134C0: 0x534, 0x213F30: 0xF0,
    0x214020: 0x548, 0x22B700: 0xA0, 0x22B7A0: 0x3CC, 0x22BB70: 0x44, 0x22BBC0: 0x1794,
}
TRPC = frozenset(pc for a, n in SIZES.items() for pc in range(a, a + n, 4))
INLINE = {0x102948, 0x102958}

# callee -> (integer argument registers, float argument registers, result used)
WSPEC = {
    0x1026A0: (3, 0, None), 0x1026D0: (3, 0, None), 0x102760: (2, 0, None), 0x1028B8: (3, 0, None),
    0x1029C0: (1, 0, None), 0x102BB0: (2, 1, None), 0x1031E0: (2, 0, None), 0x103230: (2, 1, None),
    0x122BB8: (0, 0, 'v0'), 0x123168: (2, 0, None), 0x1281C0: (0, 1, 'v0'), 0x1AF780: (1, 0, 'v0'),
    0x1AF7C0: (0, 0, 'v0'), 0x1AFC10: (2, 0, None), 0x1AFF10: (0, 0, 'v0'), 0x1AFF90: (1, 0, None),
    0x1B0000: (0, 0, None), 0x1B1470: (0, 1, 'f0'), 0x1C5FB0: (3, 0, 'v0'), 0x1C6120: (2, 0, 'v0'),
    0x1C6150: (1, 0, 'v0'), 0x1C62C0: (1, 0, None), 0x1C6380: (1, 0, None), 0x1CA5E0: (3, 0, None),
    0x1CA6E0: (2, 0, None), 0x1CB5B0: (1, 0, None), 0x1CBA50: (7, 0, None), 0x1CCF70: (1, 0, 'v0'),
    0x1CD070: (2, 0, 'v0'), 0x1CD2B0: (0, 4, 'f0'), 0x1CFB50: (3, 5, None), 0x1CFBE0: (5, 0, None),
    0x1F0190: (0, 2, None), 0x1F0290: (0, 0, None), 0x1FB9F0: (4, 0, None), 0x1FCF30: (3, 0, None),
    0x207D00: (2, 0, None), 0x207E40: (7, 0, None), 0x20A7A0: (1, 0, None), 0x20BEF0: (1, 0, 'v0'),
    0x20CD40: (0, 0, None), 0x20CD60: (0, 0, None), 0x20CD80: (0, 0, None), 0x20CDA0: (0, 0, None),
    0x211240: (1, 0, None), 0x211310: (1, 0, None), 0x2117D0: (4, 0, None), 0x213A00: (2, 0, 'v0'),
    0x213C50: (2, 0, None), 0x213CC0: (1, 0, 'v0'), 0x21B9A0: (1, 2, None),
}
# Callees the native side replays by default instead of running: after the
# entry check (identical RAM, scratchpad and arguments) it applies the
# oracle's journaled stores of that call and its v0 / f0. 0020A7A0 (the
# status background, about 155,000 instructions per call) dominates every
# page case; EM_A01UI_RUNALL=1 (and EM_TEST_FULL=1) run it as original code
# on the native side too.
REPLAY = set() if (RM.FULL or os.environ.get('EM_A01UI_RUNALL', '') not in ('', '0')) else {0x20A7A0, 0x213A00, 0x1FCF30}
INDIRECT = (1, 0, None)          # 002101C0's call through the node's +0x4C

T = 0x810130                     # the status block (the page handlers' argument)
BURN = 0x7B0F50                  # the a01_s3 node running 0022BBC0
FREE = 0x1A00000                 # zero in every capture: crafted test tables live here
ELF = NATIVE = None


# ======================================================================
# Interpreter
# ======================================================================

class UIEE(R.A01EE):
    """The render lane's A01EE (measured COP1 / VU0, the EE access rules, the
    store journal) with: branch outcomes of this lane's routines (top level
    only), PCPYH, a refusal of integer division by zero inside the lane's
    routines (the EE result is not modelled), and the lane's indirect call
    logged as a callee."""

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
                        indirect_call(self, target)
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
    return (address, e.r[29] & MASK, tuple(e.r[4 + i] & MASK64 for i in range(ni)),
            tuple(e.f[12 + i] & MASK for i in range(nf)))


def result_of(spec, v0, f0):
    return None if spec[2] is None else (v0 & MASK if spec[2] == 'v0' else f0 & MASK)


# Oracle-side memo of 0020A7A0 (quick mode only): a call whose entry (stack
# pointer, argument) and every value it read on a recorded run (first reads
# of addresses it had not yet written, instruction fetches included) are
# unchanged makes the same stores and returns the same v0 / f0, so the
# recorded stores are replayed into the journal instead of re-executing.
MEMO_ON = set() if (RM.FULL or os.environ.get('EM_A01UI_RUNALL', '') not in ('', '0')) else {0x20A7A0, 0x213A00, 0x1FCF30}
MEMO = {}


def memo_run(e, address):
    key = (address, e.r[29] & MASK, e.r[4] & MASK64)
    for reads, writes, v0, f0 in MEMO.get(key, ()):
        if all(e.load(a, n) == v for (a, n), v in reads):
            for is_spad, at, data in writes:
                (e.spad if is_spad else e.mem)[at:at + len(data)] = data
                e.journal.append((is_spad, at, data))
            return v0, f0
    reads, written = {}, set()
    load0, save0, write0 = e.load, e.save, e.write

    def load(a, size=4):
        v = load0(a, size)
        a &= MASK
        if (a, size) not in reads and all(b not in written for b in range(a, a + size)):
            reads[(a, size)] = v
        return v

    def save(a, value, size=4):
        a &= MASK
        written.update(range(a, a + size))
        save0(a, value, size)

    def write(a, data):
        a &= MASK
        written.update(range(a, a + len(data)))
        write0(a, data)
    j0 = len(e.journal)
    e.load, e.save, e.write = load, save, write
    try:
        v0, f0 = SAR.nested_bits(e, address)
    finally:
        e.load, e.save, e.write = load0, save0, write0
    MEMO.setdefault(key, []).append((tuple(reads.items()), list(e.journal[j0:]), v0, f0))
    return v0, f0


def logged_call(e, address, spec):
    entry = entry_of(e, address, spec)
    e.entries.append((len(e.journal), entry))
    if address in MEMO_ON:
        v0, f0 = memo_run(e, address)
    else:
        v0, f0 = SAR.nested_bits(e, address)
    e.calls.append(entry + (result_of(spec, v0, f0),))
    e.exits.append((len(e.journal), v0, f0))
    e.r[2], e.f[0] = v0, f0


def indirect_call(e, target):
    logged_call(e, target, INDIRECT)


def oracle_hooks(ee):
    ee.hooks = {a: (lambda e, a=a, spec=spec: logged_call(e, a, spec)) for a, spec in WSPEC.items()}


def oracle_ee(spad):
    """An oracle interpreter over this process's oracle RAM (set to the scratch)."""
    W = R.work()
    if W.oracle_live is not None:
        W.oracle.dirty |= R.journal_pages(W.oracle_live.journal)
        W.oracle_live = None
    R.sync(W.oracle, W.scratch.data, W.scratch.key, W.scratch.dirty)
    ee = UIEE(ELF, b'', spad)
    ee.mem = W.oracle.data
    ee.exits = []
    W.oracle_live = ee
    return ee


# ======================================================================
# Native side
# ======================================================================

P, U8, U32, I32, U64 = C.POINTER, C.c_uint8, C.c_uint32, C.c_int32, C.c_uint64
CALL = C.CFUNCTYPE(C.c_int, C.c_void_p, U32, U32, P(U64), C.c_uint, P(U32), C.c_uint, P(U64), P(U32))


class UiState(C.Structure):
    _fields_ = [('core', R.Core), ('call', CALL), ('ctx', C.c_void_p), ('sp', U32),
                ('leaf_calls', U32)]


# native entry -> (argument ctypes, has a v0 result, oracle register layout)
ENTRIES = {
    0x207D90: ([I32] * 5, False), 0x208040: ([I32, U32, U32, U32, U64], False), 0x20F950: ([U32], False),
    0x210030: ([U32, I32], False), 0x2101C0: ([U32], False), 0x210A00: ([I32], False),
    0x210C00: ([I32], False), 0x210F30: ([U32], False), 0x211400: ([U32, I32], False),
    0x2131B0: ([U32, I32], False), 0x2134C0: ([U32, I32], False), 0x213F30: ([U32, I32, I32], False),
    0x214020: ([U32], False), 0x22B700: ([U32, U32], True), 0x22B7A0: ([U32], True),
    0x22BB70: ([I32], True), 0x22BBC0: ([U32], False),
}
SOURCES = ['src/game/em_area01_ui_pages.c', 'src/game/em_area01_ui_effect.c']


def build_native(sources=None, tag='area01_ui'):
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / (tag + ('.dylib' if sys.platform == 'darwin' else '.so'))
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-ffp-contract=off',
                    '-shared', '-fPIC', '-Isrc'] + (sources or SOURCES) + ['-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    for address, (args, result) in ENTRIES.items():
        fn = getattr(native, 'em_area01_ui_%08X' % address)
        fn.argtypes = [P(UiState)] + args + ([P(U32)] if result else [])
        fn.restype = C.c_int
    return native


def oracle_regs(address, args):
    """The argument registers the original entry receives."""
    if address == 0x213F30:                       # (p, a1u, a2u, lo, hi): lo in a3, hi in t0
        p, lo, hi = args
        return [p, 0, 0, lo, hi]
    return list(args)


class Native(R.Native):
    """One native run over this process's native RAM (set to the scratch);
    its callees run the original code in an interpreter sharing that memory.
    entry_ok / end_differences are the render lane's."""

    def __init__(self, spad, oracle=None, fail_at=None, null_call=False, views=True):
        W = self.W = R.work()
        R.sync(W.native, W.scratch.data, W.scratch.key, W.scratch.dirty, native=True)
        self.ee = UIEE(ELF, b'', spad, journal=False)
        self.ee.mem = W.native.data
        self.ee.hooks = {}
        self.log, self.fail_at, self.replayed = [], fail_at, 0
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
        self.state = UiState()
        if views:
            self.state.core.world = R.World(C.cast(self.views, P(R.View)), len(vlist))
        self.cb = CALL(self.dispatch)
        if not null_call:
            self.state.call = self.cb
        self.state.sp = STACK_TOP

    def dispatch(self, _c, target, sp, a, na, f, nf, v0, f0):
        spec = WSPEC.get(target, (na, nf, None))
        entry = (target, sp, tuple(a[i] for i in range(na)), tuple(f[i] for i in range(nf)))
        if not self.entry_ok(entry):
            return -1
        if self.fail_at is not None and len(self.log) == self.fail_at:
            self.log.append(entry + ('fail',))
            return -1
        e = self.ee
        k = len(self.log)
        if self.oracle is not None and target in REPLAY:
            # after the entry check: the callee's stores as the original made
            # them (its RAM and scratchpad writes, in order) and its v0 / f0
            stop, rv0, rf0 = self.oracle.exits[k]
            for is_spad, at, data in self.oracle.journal[self.oracle.entries[k][0]:stop]:
                if is_spad:
                    e.spad[at:at + len(data)] = data
                else:
                    e.mem[at:at + len(data)] = data
            self.replayed += 1
            self.log.append(entry + (result_of(spec, rv0, rf0),))
            v0[0] = rv0 & MASK64
            f0[0] = rf0 & MASK
            return 0
        R.poison(e)
        for i, value in enumerate(entry[2]): e.r[4 + i] = value & MASK64
        for i, value in enumerate(entry[3]): e.f[12 + i] = value & MASK
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
        fn = getattr(NATIVE, 'em_area01_ui_%08X' % address)
        types, result = ENTRIES[address]
        out = U32(0)
        rc = fn(C.byref(self.state), *args, *([C.byref(out)] if result else []))
        return rc, (out.value if result else None)

    def poke(self, address, value, size):
        self.ee.save(address, value, size)


# ======================================================================
# Running one case on both sides
# ======================================================================

def run_both(where, spad, steps, check=None, stand_in=False):
    """steps: ('call', address, args) or ('poke', address, value, size), run
    in order on both sides from this process's scratch RAM and `spad`.
    Compares the entry of every callee (arguments, RAM, scratchpad), then the
    results, the call log, RAM and scratchpad. Returns (outcomes, calls,
    entries) or 'unmeasured' when both sides refuse (a division by zero)."""
    ee = oracle_ee(spad)
    oracle_hooks(ee)
    unmeasured = False
    results_o = []
    try:
        for step in steps:
            if step[0] == 'poke':
                ee.save(step[1], step[2], step[3])
                continue
            _, address, args = step
            for i, value in enumerate(oracle_regs(address, args)):
                ee.r[4 + i] = (sx32(value) if value <= MASK else value) & MASK64
            ee.r[29], ee.r[31] = STACK_TOP, RETURN
            ee.run(address)
            results_o.append(ee.r[2] & MASK)
    except R.Unmeasured:
        unmeasured = True
    except R.NotCompletable as ex:
        if stand_in:        # a stand-in model bank the original cannot run through
            return 'skipped'
        raise AssertionError((where, 'the original cannot complete this case') + ex.args)
    if check is not None and not unmeasured:
        assert check(ee), (where, 'the case did not reach the state it is built for')
    native = Native(spad, oracle=None if unmeasured else ee)
    results_n = []
    for step in steps:
        if step[0] == 'poke':
            native.poke(step[1], step[2], step[3])
            continue
        _, address, args = step
        rc, value = native.call(address, args)
        assert native.mismatch is None, (where, 'entry check') + native.mismatch
        if rc != 0:
            f = native.state.core.fault
            if unmeasured and f.code == 6:
                return 'unmeasured'
            raise AssertionError((where, 'native faulted', hex(address), hex(f.address), f.code, hex(f.detail)))
        results_n.append(value)
    assert not unmeasured, (where, 'the original refused (division by zero) but the native did not fault')
    for step, got, want in zip([s for s in steps if s[0] == 'call'], results_n, results_o):
        if ENTRIES[step[1]][1]:
            assert got == want, (where, hex(step[1]), 'result', hex(got), hex(want))
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

BEATS = R.BEATS
S1, S2, S3 = 'a01_s1_sentry_doc', 'a01_s2_control_room_items', 'a01_s3_fire_contact'
PAGE_BEATS = [S1, S2, 'a01_05_npc_bridge_talk']
USED = [S1, S2, S3, 'a01_05_npc_bridge_talk']      # the images the cases start from
w32 = R.w32


def put(ram, at, value, size=4):
    R.put(ram, at, value, size)


def b8(ram, at):
    return ram[at & 0x1FFFFFF]


def h16(ram, at):
    return struct.unpack_from('<h', ram, at & 0x1FFFFFF)[0]


def case(beat):
    """(scratch RAM, a scratchpad copy) of a beat."""
    return R.case_ram(beat), bytearray(R.image(beat)[1])


def sput(spad, at, value):
    struct.pack_into('<I', spad, at - 0x70000000, value & MASK)


def pads(ram, e70=0, e74=0, e78=0):
    put(ram, 0x810E70, e70, 2)
    put(ram, 0x810E74, e74, 2)
    put(ram, 0x810E78, e78, 2)


def callback_pokes(node):
    """What the pool does before a node's callback (001CB590): the current
    object D_00275B44 / 48 and its bone array D_00275B40."""
    return [('poke', 0x275B44, node, 4), ('poke', 0x275B48, node, 4), ('poke', 0x275B40, node + 0x110, 4)]


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


# ---- page 3 (00214020) ------------------------------------------------------

def db_ids(ram):
    """Record ids whose owned byte D_00810700[id + 0x5C3] is set."""
    return [i for i in range(0x6D) if b8(ram, 0x810700 + i + 0x5C3)]


def db_case(item):
    beat, state, knobs = item
    ram, spad = case(beat)
    rng = random.Random(repr(item))
    put(ram, T + 3, state, 1)
    pads(ram, knobs.get('e70', 0), knobs.get('e74', 0), knobs.get('e78', 0))
    if 'req' in knobs:
        put(ram, 0x8106B0, 3, 1)
        put(ram, 0x8106B1, knobs['req'], 1)
    else:
        put(ram, 0x8106B0, knobs.get('b0', 0), 1)
    if 'own' in knobs:                            # owned flags of a block of ids
        lo, hi, v = knobs['own']
        for i in range(lo, hi):
            put(ram, 0x810700 + i + 0x5C3, v, 1)
    for k, size in (('cat', 1), ('rec', 1), ('page', 2), ('count4', 1), ('cur', 1), ('top', 1), ('n18', 1),
                    ('t17', 1)):
        if k in knobs:
            at = {'cat': 0x12, 'rec': 0x1B, 'page': 0x1C, 'count4': 4, 'cur': 0x19, 'top': 0x1A, 'n18': 0x18,
                  't17': 0x17}[k]
            put(ram, T + at, knobs[k], size)
    if 'b64' in knobs:
        sput(spad, 0x70003B64, knobs['b64'])
    if 'list' in knobs:                           # the id list at t + 0x50
        for i, v in enumerate(knobs['list']):
            put(ram, T + 0x50 + i, v, 1)
    return run_both(('db', beat, state, knobs), bytes(spad), [('call', 0x214020, (T,))])


DB_ITEMS = []
for _beat in PAGE_BEATS:
    DB_ITEMS += [(_beat, 0, {'req': r}) for r in (0x48, 0x05, 0x1F, 0x20, 0x31, 0x32, 0x47, 0x5C, 0x5D, 0x6C)]
    DB_ITEMS += [(_beat, 0, {'req': 0x48, 'own': (0x48, 0x5D, 1)}), (_beat, 0, {'req': 0x4B, 'own': (0x48, 0x5D, 1)}),
                 (_beat, 0, {'req': 0x50, 'own': (0x48, 0x5D, 0)})]
    DB_ITEMS += [(_beat, 0, {}), (_beat, 0, {'e74': 0x20}), (_beat, 0, {'e78': 0x2000}), (_beat, 0, {'e78': 0x8000})]
    for _e74 in (0, 0x20, 0x40):
        for _e78 in (0, 0x2000, 0x8000):
            for _cat in (0, 4, 2, 0xFF, 0x81):
                DB_ITEMS.append((_beat, 1, {'e74': _e74, 'e78': _e78, 'cat': _cat}))
    DB_ITEMS += [(_beat, 1, {'e74': 0x40, 'list': [0x6D] * 0x14, 'cat': 1}),
                 (_beat, 1, {'e74': 0x40, 'list': [0x21] * 0x14, 'cat': 1, 'own': (0x20, 0x32, 1)}),
                 (_beat, 1, {'e70': 0x1000}), (_beat, 1, {'e70': 0x4000}), (_beat, 1, {'e70': 0x2000}),
                 (_beat, 1, {'e70': 0x8000}), (_beat, 1, {'e78': 0x1000}), (_beat, 1, {'e78': 0x4000}),
                 (_beat, 1, {'e78': 0x1000, 't17': 0}), (_beat, 1, {'e78': 0x4000, 't17': 7}),
                 (_beat, 1, {'e78': 0x4000, 't17': 9}), (_beat, 1, {'e78': 0x1000, 't17': 0, 'n18': 0})]
    DB_ITEMS += [(_beat, 0, {'req': r, 'own': (0x48, 0x5D, 1)}) for r in (0x4C, 0x50, 0x5C)]
    DB_ITEMS += [(_beat, 2, {'page': pg, 'top': top}) for pg in (11, 12, -11, -12, 0) for top in (1, 2)]
    for _e in (0, 0x1000, 0x4000, 0x20, 0x40):
        DB_ITEMS.append((_beat, 2, {'e78': _e if _e >= 0x1000 else 0, 'e74': _e if _e < 0x1000 else 0}))
    DB_ITEMS += [(_beat, 3, {'count4': 1}), (_beat, 3, {'count4': 8}), (_beat, 3, {'count4': 0}),
                 (_beat, 3, {'count4': 1, 'b0': 3})]
    for _rec in (0x48, 0x00, 0x21, 0x33, 0x60):
        for _page in (0, 1, 2, 5):
            for _e78 in (0, 0x2000, 0x8000):
                DB_ITEMS.append((_beat, 4, {'rec': _rec, 'page': _page, 'e78': _e78, 'b64': 4 * (_page & 1)}))
    DB_ITEMS += [(_beat, 4, {'rec': 0x48, 'e74': e}) for e in (0x20, 0x40, 0x60)]
    DB_ITEMS += [(_beat, 5, {}), (_beat, 0xFF, {})]


def db_pages(ram):
    """(rec, count) pairs with page counts >= 2 (the table D_002659C0)."""
    return [(r, w32(ram, 0x2659C0 + 4 * r)) for r in range(0x6D) if w32(ram, 0x2659C0 + 4 * r) >= 2]


def helper_case(item):
    """Direct calls of the page-3 helpers and the packet builders."""
    kind, beat, knobs = item
    ram, spad = case(beat)
    pads(ram, knobs.get('e70', 0))
    if 'b64' in knobs:
        sput(spad, 0x70003B64, knobs['b64'])
    for k, at, size in (('cat', 0x12, 1), ('rec', 0x1B, 1), ('page', 0x1C, 2)):
        if k in knobs:
            put(ram, T + at, knobs[k], size)
    if kind == '00213F30':
        steps = [('call', 0x213F30, (T, knobs['lo'], knobs['hi']))]
    elif kind in ('002131B0', '002134C0', '00211400'):
        steps = [('call', int(kind, 16), (T, knobs['a1']))]
    elif kind in ('00210A00', '00210C00'):
        steps = [('call', int(kind, 16), (knobs['a0'],))]
    elif kind == '00207D90':
        steps = [('call', 0x207D90, knobs['args'])]
    elif kind == '00208040':
        for i, v in enumerate(knobs['vec']):
            sput(spad, 0x700038A0 + 4 * i, v)
        steps = [('call', 0x208040, (knobs['slot'], 0x700038A0, 0x700038B0, 0x700038C0, knobs['rgba']))]
    elif kind == '00210030':
        node = FREE + 0x1000
        for off, v in knobs.get('node', {}).items():
            put(ram, node + off, v)
        for at, v in knobs.get('glob', {}).items():
            put(ram, at, v)
        if 'ctx2468' in knobs:
            put(ram, w32(ram, 0x275670) + 0x2468, knobs['ctx2468'])
        steps = [('call', 0x210030, (node, knobs['a1']))]
    elif kind == '00210F30':
        put(ram, 0x810700, knobs.get('area', b8(ram, 0x810700)), 1)
        for at, v in knobs.get('glob', {}).items():
            put(ram, at, v, 1 if at in (0x810142, 0x810144) else 4)
        steps = [('call', 0x210F30, (T,))]
    else:
        raise AssertionError(kind)
    for at, v, size in knobs.get('bytes', ()):
        put(ram, at, v, size)
    return run_both(('helper', kind, beat, knobs), bytes(spad), steps)


def SIGNED_ZERO_0030(beat):
    """00210030 with b = 0 + (-0 * t): the add makes b +0, which shows in
    +0xB0 when the other terms are -0 (the camera-block words, test inputs)."""
    return ('00210030', beat, {'a1': 1, 'ctx2468': F(1.0),
                               'glob': {0x810154: F(4.0), 0x810158: 0x80000000, 0x81015C: F(1.0),
                                        0x81061C: 0x80000000, 0x810618: 0x80000000, 0x810610: F(1.0),
                                        0x810614: 0}})


def helper_items():
    out = []
    for beat in PAGE_BEATS:
        for cat in range(6):
            out.append(('00213F30', beat, {'cat': cat, 'lo': 3, 'hi': 9}))
        out.append(('00213F30', beat, {'cat': 9, 'lo': 0x6A, 'hi': 0x70}))
        out.append(('00213F30', beat, {'cat': 7, 'lo': 5, 'hi': 5}))
        for a1 in (0, 1):
            for e70 in (0, 0x2000, 0x8000, 0x1000, 0x4000, 0xA000, 0x5000):
                out.append(('002134C0', beat, {'a1': a1, 'e70': e70, 'rec': 0x48, 'page': 0}))
            out.append(('002131B0', beat, {'a1': a1, 'cat': 2}))
        for rec, page, b64 in ((0x48, 0, 0), (0x48, 1, 4), (0, 0, 0), (0x21, 3, 4), (0x21, 1, 0)):
            out.append(('002134C0', beat, {'a1': 1, 'rec': rec, 'page': page, 'b64': b64}))
        for a0 in (0, 1, 2, -1):
            out.append(('00210A00', beat, {'a0': a0}))
            for e70 in (0, 0x2000, 0x8000, 0x1000, 0x4000, 0xF000):
                out.append(('00210C00', beat, {'a0': a0, 'e70': e70}))
        for args in ((1, 0x12, 9, 0x1EF, 0x96), (1, 0, 0, 0x200, 0xE0), (0, -1, -2, -3, -4), (3, 0x7FFF, 0x8000, -0x8000, 0x12345),
                     (1, -1, 0, 0, 0)):   # b0 negative, b2 not: b0's sign bits alone fill +0x34
            out.append(('00207D90', beat, {'args': args}))
        for slot, rgba, vec in ((1, 0xFFFFFFFF802040A0, (F(28000.0), 0, F(31000.0), F(1.0), F(28100.5), 0, F(30999.9), 0,
                                                        F(-12.5), 0, F(65000.0), 0)),
                                (0, 0x12345678, (F(1e10), 0, F(-1e10), 0, 0x7FC00000, 0, F(0.5), 0, F(-0.5), 0, F(3.0), 0))):
            out.append(('00208040', beat, {'slot': slot, 'rgba': rgba, 'vec': vec}))
        for a1 in (0, 1, -1):
            out.append(('00210030', beat, {'a1': a1, 'node': {0x60: F(0.3), 0xA0: F(12.0), 0xA8: F(-7.5)},
                                             'glob': {0x810154: F(4.0), 0x810158: F(-6.0), 0x81015C: F(10.0)}}))
        out.append(('00210030', beat, {'a1': 0, 'node': {0x60: F(0.10666667), 0xA0: F(85.0014), 0xA8: 0x80000000}}))
        out.append(SIGNED_ZERO_0030(beat))
        for area in (0x12, 0x15, 1, 0x10):
            out.append(('00210F30', beat, {'area': area}))
        out.append(('00210F30', beat, {'glob': {0x810142: 3, 0x810144: 1, 0x810374: F(2.5)}}))
        out.append(('00210F30', beat, {'bytes': [(T + 0x40, F(12.5), 4), (T + 0x48, F(-3.25), 4)]}))
        for a1 in (0, 1):
            for b64 in (0, 1, 2, 3):
                out.append(('00211400', beat, {'a1': a1, 'b64': b64, 'cat': 5}))
        gates = [(0x810C8D, 1, 1), (0x81076D, 0, 1), (0x810C8E, 1, 1), (0x810770, 0, 1), (0x810784, 1, 1),
                 (0x81077F, 0xFF, 1), (0x810789, 0, 1), (0x810782, 0xFF, 1), (0x81078C, 0, 1)]
        closed = [(0x810C8D, 0, 1), (0x810C8E, 0, 1), (0x810784, 0, 1), (0x81077F, 0, 1), (0x810782, 0, 1)]
        half = [(0x810C8D, 1, 1), (0x81076D, 0xFF, 1), (0x810C8E, 1, 1), (0x810770, 0xFF, 1), (0x810784, 2, 1),
                (0x81077F, 0xFF, 1), (0x810789, 0xFF, 1), (0x810782, 0xFF, 1), (0x81078C, 1, 1)]
        for g in (gates, closed, half):
            out.append(('00211400', beat, {'a1': 0, 'b64': 1, 'bytes': g}))
            for kind in (5, 6, 0, 3, 4, 1, 7):
                out.append(('00211400', beat, {'a1': 1, 'b64': 1, 'cat': kind, 'bytes': g}))
    return out


# ---- page 1 (0020F950) and its nodes ----------------------------------------

def map_setup(ram, knobs):
    if 'owned' in knobs:                          # owned-map bytes (0x810CB8 + i, read as D_00810700 + 0x5B8 + i)
        for i in range(11):
            put(ram, 0x810CB8 + i, 1 if i in knobs['owned'] else 0, 1)
    for k, at, size in (('cur6cd', 0x8106CD, 1), ('area', 0x810700, 1), ('sel', 0x810142, 1),
                        ('zoommode', 0x810143, 1), ('floor', 0x810144, 1)):
        if k in knobs:
            put(ram, at, knobs[k], size)
    for k, at in (('t12', 0x12), ('t13', 0x13), ('t14', 0x14)):
        if k in knobs:
            put(ram, T + at, knobs[k], 1)
    if 'req' in knobs:
        put(ram, 0x8106B0, 2, 1)
        put(ram, 0x8106B1, knobs['req'], 1)
    else:
        put(ram, 0x8106B0, 0, 1)
    pads(ram, knobs.get('e70', 0), knobs.get('e74', 0), knobs.get('e78', 0))
    for at, v in knobs.get('glob', {}).items():
        put(ram, at, v)
    if 'bank' in knobs:
        # The map page's model bank (asset slot D_0028A570[0], loaded with the
        # page) is not resident in the end-of-beat images; another resident
        # slot's bank stands in as a test input so the model paths run.
        put(ram, 0x28A570, w32(ram, 0x28A570 + 4 * knobs['bank']))


def map_case(item):
    beat, mode, knobs = item
    ram, spad = case(beat)
    map_setup(ram, knobs)
    put(ram, T + 3, mode, 1)
    if 'b64' in knobs:
        sput(spad, 0x70003B64, knobs['b64'])
    return run_both(('map', beat, mode, knobs), bytes(spad), [('call', 0x20F950, (T,))])


def map_items():
    out = []
    for beat in PAGE_BEATS:
        for owned in ((), (0,), (3,), (0, 5, 10), tuple(range(11))):
            for cur in (0, 3, 0x25):
                out.append((beat, 0, {'owned': owned, 'cur6cd': cur}))
        out += [(beat, 0, {'owned': (0,), 'req': 4}), (beat, 0, {'owned': (), 'req': 0})]
        # bits 6/7 of D_008106CD outside the 0x30 floor mask
        out.append((beat, 0, {'owned': (0,), 'cur6cd': 0x40}))
        # current-map nibble 15: the search starts on 0x810CC7 (past the
        # eleven map bytes) and its 11 steps down (14..4) miss map 0
        out.append((beat, 0, {'owned': (0,), 'cur6cd': 0x0F, 'glob': {0x810CC3: 0, 0x810CC7: 0}}))
        for e74 in (0, 0x20, 0x40):
            for e78 in (0, 0x2000, 0x8000):
                for owned, t12, t13 in (((0, 5), 0, 0), ((0, 5), 10, 0), ((0, 5), 5, 1), ((), 0xFF, 0), ((), 4, 0),
                                        ((3,), 3, 0), (tuple(range(11)), 10, 0)):
                    out.append((beat, 1, {'e74': e74, 'e78': e78, 'owned': owned, 't12': t12, 't13': t13, 'b64': 3}))
        for area in (0x12, 0x15, 1):
            for cur in (0, 1):
                out.append((beat, 1, {'e74': 0x40, 'owned': (0, 1), 't12': 0, 't13': 0, 'area': area,
                                      'cur6cd': cur, 't14': 1}))
                out.append((beat, 2, {'owned': (0, 1), 't12': 0, 'area': area, 'cur6cd': cur, 'b64': 1}))
        out += [(beat, 2, {'owned': (0,), 't12': 0, 'e74': 0x20}), (beat, 3, {}), (beat, 0x80, {})]
    return out


def map_nodes(beat, knobs):
    """The 22 nodes page 1's mode 0 allocates on this image (an oracle run)."""
    ram, spad = case(beat)
    map_setup(ram, knobs)
    put(ram, T + 3, 0, 1)
    ee = oracle_ee(bytes(spad))
    oracle_hooks(ee)
    ee.r[4], ee.r[29], ee.r[31] = T, STACK_TOP, RETURN
    ee.run(0x20F950)
    return [c[-1] for c in ee.calls if c[0] == 0x1AFF10]


def node_case(item):
    """002101C0 on page 1's nodes: mode 0 allocates them (both sides), then
    the callback runs as the pool runs it (current object set) in the
    requested state, flags and pads."""
    beat, which, state, knobs = item
    nodes = map_nodes(beat, knobs)
    ram, spad = case(beat)
    map_setup(ram, knobs)
    put(ram, T + 3, 0, 1)
    node = nodes[which]
    steps = [('call', 0x20F950, (T,))] + callback_pokes(node)
    # D_00810142 / 43 / 44 and D_00810154.. are the status block's own
    # t[0x12] / t[0x13] / t[0x14] and t+0x24.., which mode 0 has just set:
    # the node's view knobs go in after it
    for k, at in (('sel', 0x810142), ('zoommode', 0x810143), ('floor', 0x810144)):
        if k in knobs:
            steps.append(('poke', at, knobs[k], 1))
    for at, v in knobs.get('glob', {}).items():
        steps.append(('poke', at, v, 4))
    if 'cap' in knobs:
        steps.append(('poke', 0x275BCC, knobs['cap'], 2))
    if state == 1:          # a node reaches state 1 through state 0 (its model set up there)
        steps.append(('poke', node + 4, 0, 1))
        steps.append(('call', 0x2101C0, (node,)))
    steps.append(('poke', node + 4, state, 1))
    for at, v, size in knobs.get('node', ()):
        steps.append(('poke', node + at, v, size))
    steps.append(('call', 0x2101C0, (node,)))
    if knobs.get('twice'):
        steps.append(('poke', node + 4, 1, 1))
        steps.append(('call', 0x2101C0, (node,)))
    return run_both(('node', beat, which, state, knobs), bytes(spad), steps, stand_in='bank' in knobs)


# (node index, asset slot whose bank lets that node's model setup complete
# on the a01_s2 image; found by trial, a test input only)
NODE_BANK = {21: 1, 11: 5, 10: 11, 0: 11}


def node_items():
    out = []
    for beat in (S2,):
        base = {'owned': (0, 10), 'cur6cd': 0, 'sel': 0, 'zoommode': 0}
        for which in (0, 1, 11, 12, 21, 10):
            for state in (0, 1, 2, 3, 7):
                k = dict(base, bank=NODE_BANK.get(which, 11))
                if which in (1, 12):            # unowned maps: no model to set up
                    k.pop('bank')
                out.append((beat, which, state, k))
        for zoom in (0, 1):
            for sel in (0, 10, 5):
                for cur in (0, 10):
                    for which in (0, 10, 11, 21):
                        out.append((beat, which, 0, dict(base, zoommode=zoom, sel=sel, cur6cd=cur,
                                                         bank=NODE_BANK[which])))
                        out.append((beat, which, 1, dict(base, zoommode=zoom, sel=sel, cur6cd=cur,
                                                         bank=NODE_BANK[which])))
        for e70 in (2, 8, 0x2000, 0x8000, 0x1000, 0x4000, 0x200A, 0x9008, 0x5002, 0x3000):
            for zoom_v in (F(2.05), F(9.95), F(5.0), 0x40066666, 0x411E6667):   # the last two land exactly on 2 / 10
                out.append((beat, 10, 1, dict(base, zoommode=1, sel=10, cur6cd=10, e70=e70, bank=11,
                                              glob={0x810154: zoom_v, 0x810158: F(79.0), 0x81015C: F(-79.0)})))
        # map 10's pan limits (0x2657B0 + 10 * 20): 0x810158 in [-130, 130], 0x81015C in [-120, 120]
        for e70, g in ((0x2000, {0x810158: F(0.0)}), (0x4000, {0x81015C: F(0.0)}), (0x8000, {0x810158: F(0.0)}),
                       (0x1000, {0x81015C: F(0.0)}), (0x2000, {0x810158: F(129.5)}), (0x8000, {0x810158: F(-129.5)}),
                       (0x4000, {0x81015C: F(119.5)}), (0x1000, {0x81015C: F(-119.5)})):
            out.append((beat, 10, 1, dict(base, zoommode=1, sel=10, cur6cd=10, e70=e70, glob=g, bank=11)))
        for area in (0x12, 0x15):
            out.append((beat, 10, 1, dict(base, zoommode=1, sel=10, cur6cd=10, area=area, bank=11)))
            out.append((beat, 10, 0, dict(base, zoommode=1, sel=10, cur6cd=10, area=area, bank=11)))
        out.append((beat, 10, 1, dict(base, zoommode=1, sel=10, cur6cd=10, bank=11, glob={0x810174: F(3.0)})))
        out.append((beat, 10, 0, dict(base, cap=2, bank=11)))
        out.append((beat, 10, 0, dict(base, cap=0, bank=11)))
        out.append((beat, 1, 1, dict(base, zoommode=1, sel=1)))
        out.append((beat, 1, 0, dict(base, zoommode=1, sel=1, cur6cd=1)))
        out.append((beat, 21, 0, dict(base, cap=-1, bank=1)))
        out.append((beat, 0, 0, dict(base, owned=(1,))))
        out.append((beat, 21, 0, dict(base, twice=True, zoommode=1, sel=10, bank=1)))
        out.append((beat, 10, 0, dict(base, twice=True, zoommode=1, sel=10, cur6cd=10, bank=11)))
    return out


def map_seq_case(item):
    """Page 1 as the status screen runs it: mode 0, then mode 1 frames
    (001B0000 ticks the 22 nodes through the ORIGINAL 002101C0), a Cross,
    then mode 2 frames."""
    beat, frames = item
    ram, spad = case(beat)
    map_setup(ram, {'owned': (0, 10), 'cur6cd': 0, 'bank': 11})
    put(ram, T + 3, 0, 1)
    steps = [('call', 0x20F950, (T,))]
    for k in range(frames):
        if k == frames - 2:
            steps.append(('poke', 0x810E74, 0x40, 2))
        elif k == frames - 1:
            steps.append(('poke', 0x810E74, 0, 2))
        steps.append(('call', 0x20F950, (T,)))
    return run_both(('map sequence', beat, frames), bytes(spad), steps)


# ---- the burn node (0022BBC0) ----------------------------------------------

def burn_setup(ram, knobs):
    node = BURN
    for at, v, size in knobs.get('node', ()):
        put(ram, node + at, v, size)
    scn = w32(ram, node + 0x24)
    for at, v, size in knobs.get('scn', ()):
        put(ram, scn + at, v, size)
    if 'events' in knobs:                         # a crafted timeline at FREE (test input)
        for i, rec in enumerate(knobs['events']):
            for k, v in enumerate(rec):
                put(ram, FREE + 8 * i + 2 * k, v & 0xFFFF, 2)
        put(ram, node + 0x1F0 + 0x90, FREE)
        put(ram, node + 0x1F0 + 0x98, len(knobs['events']))
    return node


W0 = 0x1F0


def burn_items():
    out = [('captured', {})]
    out += [('frames', {'frames': 3})]
    for kind in range(10):
        out.append(('kind%d' % kind, {'node': [(0xD, kind, 1)]}))
    for st in (2, 3, 4, 0x80):
        out.append(('state%d' % st, {'node': [(4, st, 1)]}))
    out.append(('scn_stop', {'scn': [(4, 3, 1)]}))
    # state 0: the table pick (0022B7A0) and init; q = the player block
    for kind in range(11):
        out.append(('init%d' % kind, {'node': [(4, 0, 1), (0xD, kind, 1)]}))
    out.append(('init_fail', {'node': [(4, 0, 1), (0xD, 0, 1)], 'scn': [(3, 2, 1)]}))
    out.append(('init_cap', {'node': [(4, 0, 1), (0xD, 9, 1)], 'glob2': True}))
    # the timeline: every op at the clock, each seq[0xD] that gates it
    clock = 0x40
    ops = [(clock, 0, 0, 20), (clock, 1, 0, 7), (clock, 2, 3, 4), (clock, 3, 2, 0), (clock, 4, 0, 0),
           (clock, 5, 0, 12), (clock, 6, 0, 9), (clock, 7, 0, 1), (clock - 1, 0, 0, 5), (clock, 9, 0, 0),
           (clock, -1, 0, 0), (clock, 8, 0, 0)]
    for kind in (0, 1, 3, 9, 2):
        out.append(('ops%d' % kind, {'node': [(0xD, kind, 1), (W0 + 0xAC, clock, 4)], 'events': ops}))
    out.append(('op1_neg', {'node': [(W0 + 0xAC, 5, 4)], 'events': [(5, 1, 0, -3), (5, 2, 20, -2)]}))
    out.append(('op1_zero', {'node': [(W0 + 0xAC, 5, 4)], 'events': [(5, 1, 0, 0)]}))
    # ramps and the scroll
    out.append(('ramps', {'node': [(W0 + 0x9C, 3, 4), (W0 + 0xA0, 1, 4), (W0 + 0xA4, 2, 4), (W0 + 0xA8, 0x300, 4),
                                   (W0 + 0x70, F(0.25), 4), (W0 + 0x80, F(-0.5), 4)]}))
    out.append(('scroll_big', {'node': [(W0 + 0xA4, 1, 4), (W0 + 0xA8, 0x12345, 4)]}))
    # emitters: phases and wraps
    for kind in (0, 1, 2, 4, 6, 9):
        for phase in (1, 0x14, 0x15, 0x28, 0x29, 0x3C, 0x4F, 0x50, 0x78, -3):
            out.append(('emit%d_%d' % (kind, phase),
                        {'node': [(0xD, kind, 1)] + [(W0 + i, phase & 0xFF, 1) for i in range(0, 21, 3)]}))
    for burst in (0, 1, 2, 3, 4, 5):
        out.append(('burst%d' % burst, {'node': [(0xD, 9, 1)] + [(W0 + i, 0x15, 1) for i in (2, 5)] +
                                        [(W0 + 0x38 + i, burst, 1) for i in (2, 5)]}))
    out.append(('burst_bad_after_good', {'node': [(0xD, 9, 1), (W0 + 2, 0x15, 1), (W0 + 5, 0x15, 1),
                                                  (W0 + 0x3A, 1, 1), (W0 + 0x3D, 9, 1)]}))
    out.append(('fade_neg', {'node': [(0xD, 0, 1), (W0 + 2, 5, 1)], 'scn_bone_88': (2, 0x20)}))
    out.append(('fade_neg6', {'node': [(0xD, 6, 1), (W0 + 2, 5, 1)], 'scn_bone_88': (2, 0x20)}))
    out.append(('ring_wrap', {'node': [(0xD, 0, 1), (W0 + 2, 0x29, 1), (W0 + 0xB0, 0x40, 4)]}))
    out.append(('ring_wrap_below', {'node': [(0xD, 0, 1), (W0 + 2, 0x29, 1), (W0 + 0xB0, 0x3F, 4)]}))
    for kind in (0, 1, 6):
        out.append(('ring_side%d' % kind, {'node': [(0xD, kind, 1)], 'ring': 'side'}))
    # the ring: ages and kinds
    for kind in (0, 1, 2, 4, 5, 6, 9):
        out.append(('ring%d' % kind, {'node': [(0xD, kind, 1)], 'ring': True}))
    out.append(('ring_old', {'ring_age': 0x3A90}))
    return out


def burn_ring(ram, side=False):
    """Ring slots across ages and kinds (a test input): around the captured
    slot 0 position (an emission the original made near the player), every
    third slot far away, and with `side` some slots moved far to the side
    at the same depth (in front of the camera but off screen)."""
    rows = [w32(ram, BURN + 0x110 + 4 * r) for r in range(5)]
    fl = lambda a: struct.unpack('<f', struct.pack('<I', w32(ram, a)))[0]
    base = [fl(rows[0] + 4 * k) for k in range(3)]
    for i in range(0x41):
        slot = rows[i // 13] + 16 * (i % 13)
        pos = [base[0] + (i % 5) * 0.7, base[1] + (i % 3), base[2] - (i % 7) * 0.9]
        if i % 3 == 0:
            pos = [1e6, 1e6, 1e6]
        if side and i % 3 == 1:
            pos[0] += (60.0 + 90.0 * (i % 4)) * (1 if i & 1 else -1)
        for k in range(3):
            put(ram, slot + 4 * k, F(pos[k]))
        put(ram, slot + 0xC, (0, 1, 2, 3, 4, 5, -1)[i % 7], 2)
        put(ram, slot + 0xE, (0x3A99, 100, 0x3A00, -1, 0x3A99, 7, 0)[i % 7], 2)


def burn_case2(item):
    tag, knobs = item
    ram, spad = case(S3)
    node = burn_setup(ram, knobs)
    if knobs.get('ring'):
        burn_ring(ram, knobs['ring'] == 'side')
    if 'ring_age' in knobs:
        rows = [w32(ram, BURN + 0x110 + 4 * r) for r in range(5)]
        for i in range(0x41):
            put(ram, rows[i // 13] + 16 * (i % 13) + 0xE, knobs['ring_age'] + i, 2)
    if 'scn_bone_88' in knobs:
        scn = w32(ram, node + 0x24)
        b, v = knobs['scn_bone_88']
        put(ram, w32(ram, scn + 0x110 + 4 * b) + 0x88, v, 2)
    if knobs.get('glob2'):
        put(ram, 0x275BCC, 3, 2)
    steps = callback_pokes(node) + [('call', 0x22BBC0, (node,))]
    for _ in range(knobs.get('frames', 1) - 1):
        steps.append(('call', 0x22BBC0, (node,)))
    return run_both(('burn', tag), bytes(spad), steps)


def pick_case(item):
    """0022B7A0 / 0022B700 / 0022BB70 called directly (no ring or pool
    context needed beyond the image)."""
    kind, knobs = item
    ram, spad = case(S3)
    e = FREE + 0x2000
    q = FREE + 0x3000
    if kind == '0022B7A0':
        put(ram, e + 0x24, q)
        put(ram, e + 0xD, knobs['k'], 1)
        put(ram, q + 3, knobs['q3'], 1)
        put(ram, q + 2, knobs['q2'], 1)
        put(ram, q + 0xD, knobs['qd'], 1)
        put(ram, 0x8101E4, knobs['mode'], 1)
        return run_both(('0022B7A0', knobs), bytes(spad), [('call', 0x22B7A0, (e,))])
    if kind == '0022B700':
        put(ram, 0x275BCC, knobs['cap'], 2)
        return run_both(('0022B700', knobs), bytes(spad), callback_pokes(e) + [('call', 0x22B700, (e, knobs['n']))])
    if kind == '0022BB70':
        put(ram, 0x275B40, BURN + 0x110)
        return run_both(('0022BB70', knobs), bytes(spad), [('call', 0x22BB70, (knobs['i'],))])
    raise AssertionError(kind)


def pick_items():
    out = []
    for k in range(11):
        for q3 in (0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 16, 17, 18, 19):
            for q2, qd, mode in ((0, 0, 0), (0x21, 1, 3), (0x21, 0, 0), (0xE0, 1, 1)):
                if k not in (0, 1, 9) and q3 > 1:
                    continue
                out.append(('0022B7A0', {'k': k, 'q3': q3, 'q2': q2, 'qd': qd, 'mode': mode}))
    for cap, n in ((5, 5), (4, 5), (0, 0), (-1, 0), (30, 21), (0x7FFF, 0xFF)):
        out.append(('0022B700', {'cap': cap, 'n': n}))
    for i in (0, 1, 12, 13, 25, 26, 64, -1, -13, -14):
        out.append(('0022BB70', {'i': i}))
    return out


# ======================================================================
# Native-only fault cases
# ======================================================================

def fault_group():
    """NULL call, no views, a latched fault, a failing callee at call
    positions, and the code-6 register / division cases."""
    count = 0
    ram, spad0 = case(S1)
    spad = bytes(spad0)
    plans = [(0x207D90, (1, 2, 3, 4, 5)), (0x208040, (1, 0x700038A0, 0x700038B0, 0x700038C0, 0)),
             (0x20F950, (T,)), (0x210030, (T, 0)), (0x2101C0, (T,)), (0x210A00, (0,)), (0x210C00, (1,)),
             (0x210F30, (T,)), (0x211400, (T, 1)), (0x2131B0, (T, 0)), (0x2134C0, (T, 1)), (0x213F30, (T, 0, 0)),
             (0x214020, (T,)), (0x22B700, (T, 1)), (0x22B7A0, (T,)), (0x22BB70, (1,)), (0x22BBC0, (T,))]
    assert sorted(a for a, _ in plans) == sorted(ENTRIES)
    needs_call = {a for a in ENTRIES} - {0x207D90, 0x210030, 0x213F30, 0x22B7A0, 0x22BB70}
    for address, args in plans:
        # no views: code 4 (00210A00 makes only calls, nothing of its own to read)
        if address != 0x210A00:
            n = Native(spad, views=False)
            rc, _ = n.call(address, args)
            f = n.state.core.fault
            assert rc == -1 and f.code == 4, (hex(address), 'no views', rc, f.code)
            count += 1
        # latched: refuses without a call or a store
        n = Native(spad)
        n.state.core.fault.code = 6
        rc, _ = n.call(address, args)
        assert rc == -1 and n.log == [] and n.unchanged(spad), (hex(address), 'latched')
        count += 1
        if address in needs_call:
            n = Native(spad, null_call=True)
            rc, _ = n.call(address, args)
            f = n.state.core.fault
            assert rc == -1 and f.code == 1 and n.unchanged(spad), (hex(address), 'NULL call', f.code)
            count += 1
    # a failing callee at call positions of real cases (first, second, middle, last)
    def setup_fail(address):
        ram, spad0 = case(S3 if address == 0x22BBC0 else S1)
        if address == 0x22BBC0:
            for at, v in ((0x275B44, BURN), (0x275B48, BURN), (0x275B40, BURN + 0x110)):
                put(ram, at, v)
            return bytes(spad0), (BURN,)
        return bytes(spad0), (T, 1 if address == 0x2134C0 else 0)
    for address in (0x2131B0, 0x2134C0, 0x22BBC0):
        spad1, args = setup_fail(address)
        probe = Native(spad1)
        assert probe.call(address, args)[0] == 0, (hex(address), 'probe run')
        total = len(probe.log)
        for k in sorted({0, 1, total // 2, total - 1}):
            spad1, args = setup_fail(address)
            n = Native(spad1, fail_at=k)
            rc, _ = n.call(address, args)
            f = n.state.core.fault
            assert rc == -1 and f.code == 2 and f.address == n.log[k][0] and len(n.log) == k + 1, \
                (hex(address), 'failing callee', k, rc, f.code, len(n.log))
            count += 1
    # code 6: 00214020 state 1 with a category outside 0..4 and a decrement
    ram, spad0 = case(S1)
    put(ram, T + 3, 1, 1)
    put(ram, T + 0x12, 7, 1)
    pads(ram, 0, 0, 0x8000)
    n = Native(bytes(spad0))
    rc, _ = n.call(0x214020, (T,))
    f = n.state.core.fault
    assert rc == -1 and f.code == 6 and f.address == 0x213F30, ('00213F30 lo/hi', f.code, hex(f.address))
    count += 1
    # code 6: 0022BBC0 with seq[0xD] >= 10 and an active bone; with a first
    # burst kind outside 0..5
    for knobs in ({'node': [(0xD, 10, 1), (W0 + 2, 3, 1)]},
                  {'node': [(0xD, 9, 1), (W0 + 0, 0, 1), (W0 + 1, 0, 1), (W0 + 2, 0x15, 1), (W0 + 0x3A, 7, 1)]}):
        ram, spad0 = case(S3)
        burn_setup(ram, knobs)
        for i in range(21):
            if all(i != at - W0 for at, _, _ in knobs['node'] if W0 <= at < W0 + 0x38):
                put(ram, BURN + W0 + i, 0, 1)
        for at, v in ((0x275B44, BURN), (0x275B48, BURN), (0x275B40, BURN + 0x110)):
            put(ram, at, v)
        n = Native(bytes(spad0))
        rc, _ = n.call(0x22BBC0, (BURN,))
        f = n.state.core.fault
        assert rc == -1 and f.code == 6 and f.address == 0x22BBC0, ('0022BBC0 registers', knobs, f.code)
        count += 1
    return count


def fault_cases():
    return R.in_child(fault_group, 'fault cases')


# ======================================================================
# Main
# ======================================================================

POOL = None
# repr() of the items the outcome cover chose (`python3 tools/test_area01_ui_reference.py cover`
# prints this set: every branch outcome any generated item reaches)
QUICK_PINS = {
    "('00210C00', 'a01_s1_sentry_doc', {'a0': 0, 'e70': 32768})",
    "('00210C00', 'a01_s1_sentry_doc', {'a0': 1, 'e70': 16384})",
    "('00210C00', 'a01_s1_sentry_doc', {'a0': 1, 'e70': 61440})",
    "('00210F30', 'a01_s1_sentry_doc', {'area': 18})",
    "('00210F30', 'a01_s1_sentry_doc', {'area': 21})",
    "('00211400', 'a01_s1_sentry_doc', {'a1': 0, 'b64': 1, 'bytes': [(8457357, 1, 1), (8456045, 0, 1), (8457358, 1, 1), (8456048, 0, 1), (8456068, 1, 1), (8456063, 255, 1), (8456073, 0, 1), (8456066, 255, 1), (8456076, 0, 1)]})",
    "('00211400', 'a01_s1_sentry_doc', {'a1': 0, 'b64': 1, 'bytes': [(8457357, 1, 1), (8456045, 255, 1), (8457358, 1, 1), (8456048, 255, 1), (8456068, 2, 1), (8456063, 255, 1), (8456073, 255, 1), (8456066, 255, 1), (8456076, 1, 1)]})",
    "('00211400', 'a01_s1_sentry_doc', {'a1': 1, 'b64': 1, 'cat': 0, 'bytes': [(8457357, 1, 1), (8456045, 0, 1), (8457358, 1, 1), (8456048, 0, 1), (8456068, 1, 1), (8456063, 255, 1), (8456073, 0, 1), (8456066, 255, 1), (8456076, 0, 1)]})",
    "('00211400', 'a01_s1_sentry_doc', {'a1': 1, 'b64': 1, 'cat': 1, 'bytes': [(8457357, 1, 1), (8456045, 0, 1), (8457358, 1, 1), (8456048, 0, 1), (8456068, 1, 1), (8456063, 255, 1), (8456073, 0, 1), (8456066, 255, 1), (8456076, 0, 1)]})",
    "('00211400', 'a01_s1_sentry_doc', {'a1': 1, 'b64': 1, 'cat': 3, 'bytes': [(8457357, 0, 1), (8457358, 0, 1), (8456068, 0, 1), (8456063, 0, 1), (8456066, 0, 1)]})",
    "('00211400', 'a01_s1_sentry_doc', {'a1': 1, 'b64': 1, 'cat': 3, 'bytes': [(8457357, 1, 1), (8456045, 0, 1), (8457358, 1, 1), (8456048, 0, 1), (8456068, 1, 1), (8456063, 255, 1), (8456073, 0, 1), (8456066, 255, 1), (8456076, 0, 1)]})",
    "('00211400', 'a01_s1_sentry_doc', {'a1': 1, 'b64': 1, 'cat': 3, 'bytes': [(8457357, 1, 1), (8456045, 255, 1), (8457358, 1, 1), (8456048, 255, 1), (8456068, 2, 1), (8456063, 255, 1), (8456073, 255, 1), (8456066, 255, 1), (8456076, 1, 1)]})",
    "('00211400', 'a01_s1_sentry_doc', {'a1': 1, 'b64': 1, 'cat': 4, 'bytes': [(8457357, 0, 1), (8457358, 0, 1), (8456068, 0, 1), (8456063, 0, 1), (8456066, 0, 1)]})",
    "('00211400', 'a01_s1_sentry_doc', {'a1': 1, 'b64': 1, 'cat': 4, 'bytes': [(8457357, 1, 1), (8456045, 0, 1), (8457358, 1, 1), (8456048, 0, 1), (8456068, 1, 1), (8456063, 255, 1), (8456073, 0, 1), (8456066, 255, 1), (8456076, 0, 1)]})",
    "('00211400', 'a01_s1_sentry_doc', {'a1': 1, 'b64': 1, 'cat': 4, 'bytes': [(8457357, 1, 1), (8456045, 255, 1), (8457358, 1, 1), (8456048, 255, 1), (8456068, 2, 1), (8456063, 255, 1), (8456073, 255, 1), (8456066, 255, 1), (8456076, 1, 1)]})",
    "('00211400', 'a01_s1_sentry_doc', {'a1': 1, 'b64': 1, 'cat': 5, 'bytes': [(8457357, 1, 1), (8456045, 0, 1), (8457358, 1, 1), (8456048, 0, 1), (8456068, 1, 1), (8456063, 255, 1), (8456073, 0, 1), (8456066, 255, 1), (8456076, 0, 1)]})",
    "('00211400', 'a01_s1_sentry_doc', {'a1': 1, 'b64': 1, 'cat': 5, 'bytes': [(8457357, 1, 1), (8456045, 255, 1), (8457358, 1, 1), (8456048, 255, 1), (8456068, 2, 1), (8456063, 255, 1), (8456073, 255, 1), (8456066, 255, 1), (8456076, 1, 1)]})",
    "('00211400', 'a01_s1_sentry_doc', {'a1': 1, 'b64': 1, 'cat': 5})",
    "('00211400', 'a01_s1_sentry_doc', {'a1': 1, 'b64': 1, 'cat': 6, 'bytes': [(8457357, 0, 1), (8457358, 0, 1), (8456068, 0, 1), (8456063, 0, 1), (8456066, 0, 1)]})",
    "('00211400', 'a01_s1_sentry_doc', {'a1': 1, 'b64': 1, 'cat': 6, 'bytes': [(8457357, 1, 1), (8456045, 0, 1), (8457358, 1, 1), (8456048, 0, 1), (8456068, 1, 1), (8456063, 255, 1), (8456073, 0, 1), (8456066, 255, 1), (8456076, 0, 1)]})",
    "('00211400', 'a01_s1_sentry_doc', {'a1': 1, 'b64': 1, 'cat': 6, 'bytes': [(8457357, 1, 1), (8456045, 255, 1), (8457358, 1, 1), (8456048, 255, 1), (8456068, 2, 1), (8456063, 255, 1), (8456073, 255, 1), (8456066, 255, 1), (8456076, 1, 1)]})",
    "('00213F30', 'a01_s1_sentry_doc', {'cat': 7, 'lo': 5, 'hi': 5})",
    "('0022B7A0', {'k': 0, 'q3': 1, 'q2': 0, 'qd': 0, 'mode': 0})",
    "('0022B7A0', {'k': 0, 'q3': 12, 'q2': 0, 'qd': 0, 'mode': 0})",
    "('0022B7A0', {'k': 0, 'q3': 16, 'q2': 0, 'qd': 0, 'mode': 0})",
    "('0022B7A0', {'k': 0, 'q3': 4, 'q2': 0, 'qd': 0, 'mode': 0})",
    "('0022B7A0', {'k': 0, 'q3': 5, 'q2': 0, 'qd': 0, 'mode': 0})",
    "('0022B7A0', {'k': 0, 'q3': 6, 'q2': 0, 'qd': 0, 'mode': 0})",
    "('0022B7A0', {'k': 0, 'q3': 7, 'q2': 0, 'qd': 0, 'mode': 0})",
    "('0022B7A0', {'k': 1, 'q3': 1, 'q2': 0, 'qd': 0, 'mode': 0})",
    "('0022B7A0', {'k': 1, 'q3': 1, 'q2': 224, 'qd': 1, 'mode': 1})",
    "('0022B7A0', {'k': 1, 'q3': 12, 'q2': 33, 'qd': 1, 'mode': 3})",
    "('0022B7A0', {'k': 1, 'q3': 2, 'q2': 0, 'qd': 0, 'mode': 0})",
    "('0022B7A0', {'k': 1, 'q3': 5, 'q2': 0, 'qd': 0, 'mode': 0})",
    "('0022B7A0', {'k': 1, 'q3': 7, 'q2': 0, 'qd': 0, 'mode': 0})",
    "('0022B7A0', {'k': 9, 'q3': 0, 'q2': 33, 'qd': 1, 'mode': 3})",
    "('0022B7A0', {'k': 9, 'q3': 19, 'q2': 33, 'qd': 1, 'mode': 3})",
    "('a01_05_npc_bridge_talk', 1, {'e74': 64, 'owned': (0, 1), 't12': 0, 't13': 0, 'area': 18, 'cur6cd': 0, 't14': 1})",
    "('a01_s1_sentry_doc', 0, {'e74': 32})",
    "('a01_s1_sentry_doc', 0, {'e78': 32768})",
    "('a01_s1_sentry_doc', 0, {'owned': (), 'req': 0})",
    "('a01_s1_sentry_doc', 0, {'req': 32})",
    "('a01_s1_sentry_doc', 0, {'req': 50})",
    "('a01_s1_sentry_doc', 0, {'req': 5})",
    "('a01_s1_sentry_doc', 0, {'req': 75, 'own': (72, 93, 1)})",
    "('a01_s1_sentry_doc', 0, {'req': 76, 'own': (72, 93, 1)})",
    "('a01_s1_sentry_doc', 0, {'req': 93})",
    "('a01_s1_sentry_doc', 1, {'e70': 16384})",
    "('a01_s1_sentry_doc', 1, {'e70': 32768})",
    "('a01_s1_sentry_doc', 1, {'e70': 4096})",
    "('a01_s1_sentry_doc', 1, {'e70': 8192})",
    "('a01_s1_sentry_doc', 1, {'e74': 0, 'e78': 0, 'owned': (), 't12': 255, 't13': 0, 'b64': 3})",
    "('a01_s1_sentry_doc', 1, {'e74': 0, 'e78': 0, 'owned': (0, 5), 't12': 5, 't13': 1, 'b64': 3})",
    "('a01_s1_sentry_doc', 1, {'e74': 0, 'e78': 32768, 'cat': 4})",
    "('a01_s1_sentry_doc', 1, {'e74': 0, 'e78': 32768, 'owned': (), 't12': 4, 't13': 0, 'b64': 3})",
    "('a01_s1_sentry_doc', 1, {'e74': 0, 'e78': 8192, 'cat': 4})",
    "('a01_s1_sentry_doc', 1, {'e74': 0, 'e78': 8192, 'owned': (0, 5), 't12': 0, 't13': 0, 'b64': 3})",
    "('a01_s1_sentry_doc', 1, {'e74': 32, 'e78': 0, 'owned': (0, 5), 't12': 0, 't13': 0, 'b64': 3})",
    "('a01_s1_sentry_doc', 1, {'e74': 64, 'e78': 0, 'cat': 0})",
    "('a01_s1_sentry_doc', 1, {'e74': 64, 'e78': 32768, 'owned': (0, 5), 't12': 0, 't13': 0, 'b64': 3})",
    "('a01_s1_sentry_doc', 1, {'e74': 64, 'e78': 8192, 'cat': 0})",
    "('a01_s1_sentry_doc', 1, {'e74': 64, 'e78': 8192, 'owned': (), 't12': 4, 't13': 0, 'b64': 3})",
    "('a01_s1_sentry_doc', 1, {'e74': 64, 'owned': (0, 1), 't12': 0, 't13': 0, 'area': 21, 'cur6cd': 0, 't14': 1})",
    "('a01_s1_sentry_doc', 1, {'e78': 4096, 't17': 0})",
    "('a01_s1_sentry_doc', 2, {'owned': (0, 1), 't12': 0, 'area': 18, 'cur6cd': 1, 'b64': 1})",
    "('a01_s1_sentry_doc', 2, {'owned': (0,), 't12': 0, 'e74': 32})",
    "('a01_s1_sentry_doc', 2, {'page': 11, 'top': 1})",
    "('a01_s1_sentry_doc', 2, {'page': 11, 'top': 2})",
    "('a01_s1_sentry_doc', 3, {'count4': 1})",
    "('a01_s1_sentry_doc', 3, {'count4': 8})",
    "('a01_s1_sentry_doc', 3, {})",
    "('a01_s1_sentry_doc', 4, {'rec': 0, 'page': 0, 'e78': 0, 'b64': 0})",
    "('a01_s1_sentry_doc', 4, {'rec': 72, 'e74': 32})",
    "('a01_s1_sentry_doc', 4, {'rec': 72, 'page': 0, 'e78': 32768, 'b64': 0})",
    "('a01_s1_sentry_doc', 4, {'rec': 72, 'page': 0, 'e78': 8192, 'b64': 0})",
    "('a01_s1_sentry_doc', 4, {'rec': 72, 'page': 1, 'e78': 8192, 'b64': 4})",
    "('a01_s1_sentry_doc', 4, {'rec': 72, 'page': 2, 'e78': 0, 'b64': 0})",
    "('a01_s1_sentry_doc', 4, {'rec': 96, 'page': 1, 'e78': 32768, 'b64': 4})",
    "('a01_s1_sentry_doc', 5, {})",
    "('a01_s2_control_room_items', 0, 0, {'owned': (0, 10), 'cur6cd': 10, 'sel': 0, 'zoommode': 1, 'bank': 11})",
    "('a01_s2_control_room_items', 0, 0, {'owned': (1,), 'cur6cd': 0, 'sel': 0, 'zoommode': 0})",
    "('a01_s2_control_room_items', 0, 2, {'owned': (0, 10), 'cur6cd': 0, 'sel': 0, 'zoommode': 0, 'bank': 11})",
    "('a01_s2_control_room_items', 0, 3, {'owned': (0, 10), 'cur6cd': 0, 'sel': 0, 'zoommode': 0, 'bank': 11})",
    "('a01_s2_control_room_items', 0, 7, {'owned': (0, 10), 'cur6cd': 0, 'sel': 0, 'zoommode': 0, 'bank': 11})",
    "('a01_s2_control_room_items', 1, 1, {'owned': (0, 10), 'cur6cd': 0, 'sel': 1, 'zoommode': 1})",
    "('a01_s2_control_room_items', 10, 0, {'owned': (0, 10), 'cur6cd': 0, 'sel': 0, 'zoommode': 0, 'cap': 0, 'bank': 11})",
    "('a01_s2_control_room_items', 10, 0, {'owned': (0, 10), 'cur6cd': 0, 'sel': 0, 'zoommode': 1, 'bank': 11})",
    "('a01_s2_control_room_items', 10, 0, {'owned': (0, 10), 'cur6cd': 10, 'sel': 0, 'zoommode': 0, 'bank': 11})",
    "('a01_s2_control_room_items', 10, 1, {'owned': (0, 10), 'cur6cd': 10, 'sel': 10, 'zoommode': 1, 'area': 18, 'bank': 11})",
    "('a01_s2_control_room_items', 10, 1, {'owned': (0, 10), 'cur6cd': 10, 'sel': 10, 'zoommode': 1, 'area': 21, 'bank': 11})",
    "('a01_s2_control_room_items', 10, 1, {'owned': (0, 10), 'cur6cd': 10, 'sel': 10, 'zoommode': 1, 'e70': 16384, 'bank': 11, 'glob': {8454484: 1073951539, 8454488: 1117650944, 8454492: 3265134592}})",
    "('a01_s2_control_room_items', 10, 1, {'owned': (0, 10), 'cur6cd': 10, 'sel': 10, 'zoommode': 1, 'e70': 16384, 'glob': {8454492: 1122959360}, 'bank': 11})",
    "('a01_s2_control_room_items', 10, 1, {'owned': (0, 10), 'cur6cd': 10, 'sel': 10, 'zoommode': 1, 'e70': 32768, 'glob': {8454488: 3271655424}, 'bank': 11})",
    "('a01_s2_control_room_items', 10, 1, {'owned': (0, 10), 'cur6cd': 10, 'sel': 10, 'zoommode': 1, 'e70': 36872, 'bank': 11, 'glob': {8454484: 1092511335, 8454488: 1117650944, 8454492: 3265134592}})",
    "('a01_s2_control_room_items', 10, 1, {'owned': (0, 10), 'cur6cd': 10, 'sel': 10, 'zoommode': 1, 'e70': 4096, 'glob': {8454492: 3270443008}, 'bank': 11})",
    "('a01_s2_control_room_items', 10, 1, {'owned': (0, 10), 'cur6cd': 10, 'sel': 10, 'zoommode': 1, 'e70': 8192, 'glob': {8454488: 1124171776}, 'bank': 11})",
    "('a01_s2_control_room_items', 10, 1, {'owned': (0, 10), 'cur6cd': 10, 'sel': 10, 'zoommode': 1, 'e70': 8202, 'bank': 11, 'glob': {8454484: 1074161254, 8454488: 1117650944, 8454492: 3265134592}})",
    "('a01_s2_control_room_items', 11, 0, {'owned': (0, 10), 'cur6cd': 0, 'sel': 0, 'zoommode': 0, 'bank': 5})",
    "('a01_s2_control_room_items', 11, 0, {'owned': (0, 10), 'cur6cd': 0, 'sel': 0, 'zoommode': 1, 'bank': 5})",
    "('a01_s2_control_room_items', 12, 0, {'owned': (0, 10), 'cur6cd': 0, 'sel': 0, 'zoommode': 0})",
    "('a01_s2_control_room_items', 3)",
    "('burst3', {'node': [(13, 9, 1), (498, 21, 1), (501, 21, 1), (554, 3, 1), (557, 3, 1)]})",
    "('burst5', {'node': [(13, 9, 1), (498, 21, 1), (501, 21, 1), (554, 5, 1), (557, 5, 1)]})",
    "('emit1_80', {'node': [(13, 1, 1), (496, 80, 1), (499, 80, 1), (502, 80, 1), (505, 80, 1), (508, 80, 1), (511, 80, 1), (514, 80, 1)]})",
    "('emit2_120', {'node': [(13, 2, 1), (496, 120, 1), (499, 120, 1), (502, 120, 1), (505, 120, 1), (508, 120, 1), (511, 120, 1), (514, 120, 1)]})",
    "('emit9_60', {'node': [(13, 9, 1), (496, 60, 1), (499, 60, 1), (502, 60, 1), (505, 60, 1), (508, 60, 1), (511, 60, 1), (514, 60, 1)]})",
    "('fade_neg6', {'node': [(13, 6, 1), (498, 5, 1)], 'scn_bone_88': (2, 32)})",
    "('init0', {'node': [(4, 0, 1), (13, 0, 1)]})",
    "('init1', {'node': [(4, 0, 1), (13, 1, 1)]})",
    "('init10', {'node': [(4, 0, 1), (13, 10, 1)]})",
    "('init9', {'node': [(4, 0, 1), (13, 9, 1)]})",
    "('init_cap', {'node': [(4, 0, 1), (13, 9, 1)], 'glob2': True})",
    "('op1_neg', {'node': [(668, 5, 4)], 'events': [(5, 1, 0, -3), (5, 2, 20, -2)]})",
    "('ops0', {'node': [(13, 0, 1), (668, 64, 4)], 'events': [(64, 0, 0, 20), (64, 1, 0, 7), (64, 2, 3, 4), (64, 3, 2, 0), (64, 4, 0, 0), (64, 5, 0, 12), (64, 6, 0, 9), (64, 7, 0, 1), (63, 0, 0, 5), (64, 9, 0, 0), (64, -1, 0, 0), (64, 8, 0, 0)]})",
    "('ops1', {'node': [(13, 1, 1), (668, 64, 4)], 'events': [(64, 0, 0, 20), (64, 1, 0, 7), (64, 2, 3, 4), (64, 3, 2, 0), (64, 4, 0, 0), (64, 5, 0, 12), (64, 6, 0, 9), (64, 7, 0, 1), (63, 0, 0, 5), (64, 9, 0, 0), (64, -1, 0, 0), (64, 8, 0, 0)]})",
    "('ops3', {'node': [(13, 3, 1), (668, 64, 4)], 'events': [(64, 0, 0, 20), (64, 1, 0, 7), (64, 2, 3, 4), (64, 3, 2, 0), (64, 4, 0, 0), (64, 5, 0, 12), (64, 6, 0, 9), (64, 7, 0, 1), (63, 0, 0, 5), (64, 9, 0, 0), (64, -1, 0, 0), (64, 8, 0, 0)]})",
    "('ops9', {'node': [(13, 9, 1), (668, 64, 4)], 'events': [(64, 0, 0, 20), (64, 1, 0, 7), (64, 2, 3, 4), (64, 3, 2, 0), (64, 4, 0, 0), (64, 5, 0, 12), (64, 6, 0, 9), (64, 7, 0, 1), (63, 0, 0, 5), (64, 9, 0, 0), (64, -1, 0, 0), (64, 8, 0, 0)]})",
    "('ring_side0', {'node': [(13, 0, 1)], 'ring': 'side'})",
    "('ring_wrap', {'node': [(13, 0, 1), (498, 41, 1), (672, 64, 4)]})",
    "('scn_stop', {'scn': [(4, 3, 1)]})",
    "('state2', {'node': [(4, 2, 1)]})",
    "('state3', {'node': [(4, 3, 1)]})",
    "('state4', {'node': [(4, 4, 1)]})",
}


# items the bounded mutation check needed beyond the outcome cover (data
# conditions, not branches: docs/AREA01_UI.md section 4)
EXTRA_PINS = {repr(SIGNED_ZERO_0030(S1)),
              repr(('ring_wrap_below', {'node': [(0xD, 0, 1), (W0 + 2, 0x29, 1), (W0 + 0xB0, 0x3F, 4)]})),
              repr(('00210F30', S1, {'bytes': [(T + 0x40, F(12.5), 4), (T + 0x48, F(-3.25), 4)]})),
              repr((S1, 3, {'count4': 1, 'b0': 3})),
              repr(('00210F30', S1, {'glob': {0x810142: 3, 0x810144: 1, 0x810374: F(2.5)}})),
              # review round 2 (docs/AREA01_UI.md section 4)
              repr(('00207D90', S1, {'args': (1, -1, 0, 0, 0)})),
              repr((S1, 0, {'owned': (0,), 'cur6cd': 0x40})),
              repr((S1, 0, {'owned': (0,), 'cur6cd': 0x0F, 'glob': {0x810CC3: 0, 0x810CC7: 0}})),
              repr((S2, 10, 1, {'owned': (0, 10), 'cur6cd': 10, 'sel': 10, 'zoommode': 1, 'bank': 11,
                                'glob': {0x810174: F(3.0)}})),
              repr(('emit1_79', {'node': [(0xD, 1, 1)] + [(W0 + i, 0x4F, 1) for i in range(0, 21, 3)]})),
              repr(('0022B7A0', {'k': 9, 'q3': 17, 'q2': 0x21, 'qd': 1, 'mode': 3})),
              repr(('0022B7A0', {'k': 9, 'q3': 17, 'q2': 0x21, 'qd': 0, 'mode': 0}))}


def pick(items, count, seed, axes=(), keep=None):
    base_keep = keep
    return RM.select(items, count, seed, axes=axes,
                     keep=lambda i, it: repr(it) in QUICK_PINS or repr(it) in EXTRA_PINS or (base_keep is not None and base_keep(i, it)))


def pmap(fn, items, key=None):
    items = list(items)
    if POOL is None or len(items) < 2:
        return [fn(x) for x in items]
    order = sorted(range(len(items)), key=lambda i: key(items[i])) if key else list(range(len(items)))
    size = max(1, -(-len(items) // (3 * JOBS)))
    out = [None] * len(items)
    for i, r in zip(order, POOL.imap(fn, [items[i] for i in order], chunksize=size)):
        out[i] = r
    return out


# Branch outcomes no case reaches, with the reason (docs/AREA01_UI.md 3).
UNREACHED = {
    # 0022BBC0 with seq[0xD] >= 10 and a live bone: the original divides by
    # its caller's s1; the native faults (code 6), tested in the fault group
    (0x22C3F4, True), (0x22C81C, True),
}


def main():
    global ELF, NATIVE, POOL
    t0 = time.time()
    ELF = read_elf()
    R.ELF = ELF
    NATIVE = build_native()
    R.TRACKER = R.build_tracker()
    assert BEATS, 'no AREA01 captures under ' + str(R.ROUTE)
    want = set(WSPEC) | INLINE
    got = callee_set()
    assert got == want, ('callee set', sorted(map(hex, got ^ want)))
    base = EE(ELF)
    for beat in USED:
        check_code(R.image(beat)[0], beat, base)
    ref = R.image(USED[0])[0]
    for beat in USED:
        img = R.image(beat)[0]
        R.DIFF[beat] = frozenset(p for p in range(R.RAM_SIZE // R.PAGE)
                                 if img[p * R.PAGE:(p + 1) * R.PAGE] != ref[p * R.PAGE:(p + 1) * R.PAGE])
    for beat in USED:
        assert beat in BEATS, ('capture missing', beat)
    tf = time.time()
    faults = fault_cases()
    if os.environ.get('EM_A01UI_TIMES'):
        print('fault cases %.1f s, setup %.1f s' % (time.time() - tf, tf - t0), flush=True)
    if JOBS > 1 and 'fork' in multiprocessing.get_all_start_methods():
        POOL = multiprocessing.get_context('fork').Pool(JOBS)
    try:
        run_groups(faults, t0)
    finally:
        if POOL is not None:
            R.close_pool(POOL)
            POOL = None


def case_groups(everything=False):
    """(name, selected items, total, case function) per group; quick mode
    selects QUICK_PINS (the outcome cover) plus a covering sample."""
    sel = (lambda items, *a, **k: list(items)) if everything else pick
    groups = []
    db = sel(DB_ITEMS, 50, 31, axes=(lambda it: it[1], lambda it: it[0]))
    groups.append(('page 3 (00214020)', db, len(DB_ITEMS), db_case))
    hl_all = helper_items()
    hl = sel(hl_all, 60, 32, axes=(lambda it: it[0],))
    groups.append(('page helpers and packets', hl, len(hl_all), helper_case))
    mp_all = map_items()
    mp = sel(mp_all, 30, 33, axes=(lambda it: it[1],))
    groups.append(('page 1 (0020F950)', mp, len(mp_all), map_case))
    nd_all = node_items()
    nd = sel(nd_all, 25, 34, axes=(lambda it: it[2],))
    groups.append(('page-1 nodes (002101C0)', nd, len(nd_all), node_case))
    sq_all = [(b, n) for b in (S2, S1) for n in (1, 3, 5)]
    sq = sel(sq_all, 1, 35, keep=lambda i, it: it == (S2, 3))
    groups.append(('page-1 sequences', sq, len(sq_all), map_seq_case))
    bu_all = burn_items()
    bu = sel(bu_all, 40, 36)
    groups.append(('burn node (0022BBC0)', bu, len(bu_all), burn_case2))
    pk_all = pick_items()
    pk = sel(pk_all, 60, 37, axes=(lambda it: it[0], lambda it: it[1].get('k', -1)))
    groups.append(('effect helpers', pk, len(pk_all), pick_case))
    if not everything:      # a pin that matches no generated item would be silently dropped
        chosen = {repr(it) for _, items, _, _ in groups for it in items}
        lost = sorted((QUICK_PINS | EXTRA_PINS) - chosen)
        assert not lost, ('pins not among the generated items', lost)

    return groups


def run_groups(faults, t0):
    outcomes, cases, unmeasured, calls, entries, skipped = set(), 0, 0, 0, 0, 0

    def absorb(results):
        nonlocal cases, unmeasured, calls, entries, skipped
        for r in results:
            if r == 'skipped':
                skipped += 1
                continue
            cases += 1
            if r == 'unmeasured':
                unmeasured += 1
            else:
                outcomes.update(r[0])
                calls += r[1]
                entries += r[2]

    groups = case_groups()
    counts = []
    for name, items, total, fn in groups:
        before = cases
        tg = time.time()
        absorb(pmap(fn, items))
        if os.environ.get('EM_A01UI_TIMES'):
            print('%-28s %4d cases %.1f s' % (name, len(items), time.time() - tg), flush=True)
        counts.append(RM.part(cases - before, total, name + ' cases'))

    branches = all_branches()
    need = {(pc, t) for pc in branches for t in (True, False)}
    missing = sorted((hex(pc), t) for pc, t in need - outcomes if (pc, t) not in UNREACHED)
    RM.banner(*counts, f'{cases} cases in all ({unmeasured} refused on both sides: a zero divisor; {skipped} stand-in-bank '
              f'node cases the original cannot complete, skipped)',
              f'{calls} callee calls, {entries} entries with RAM + scratchpad + arguments equal to the original\'s',
              f'{len(branches)} conditional branches: {len(need) - len(missing) - len(UNREACHED)} of {len(need)} '
              f'outcomes seen, {len(UNREACHED)} unreachable by construction',
              f'{faults} fault cases')
    assert not missing, ('branch outcomes never seen', missing)
    print('elapsed %.1f s' % (time.time() - t0))
    print('area01 ui reference: PASS')


# ======================================================================
# Bounded mutation check (`mutants` command; not part of the default run)
# ======================================================================

PG, FX = 'src/game/em_area01_ui_pages.c', 'src/game/em_area01_ui_effect.c'
MUTANTS = [   # (name, file, text, replacement): one operation each
    ('7D90_qwc', PG, 'ui_sh(s, ui_lw(s, entry + 0x10u), 3);', 'ui_sh(s, ui_lw(s, entry + 0x10u), 4);'),
    ('7D90_lane3', PG, '((uint64_t)(int64_t)b3 << 48)', '((uint64_t)(int64_t)b3 << 47)'),
    ('8040_rgba_zext', PG, 'ui_sd(s, q + 0x38u, a4 & UINT64_C(0xFFFFFFFF));', 'ui_sd(s, q + 0x38u, a4);'),
    ('8040_shift', PG, 'const uint32_t hi = (uint32_t)r << 16;', 'const uint32_t hi = (uint32_t)r << 15;'),
    ('0030_bias', PG, '0xC2AA00B8u', '0xC2AA00B9u'),
    ('0030_zero_add', PG, 'b = ui_fadd(0, x);', 'b = x;'),
    ('01C0_colour', PG, 'sel == i ? 0x3F0CCCCDu : 0x3E19999Au', 'sel == i ? 0x3E19999Au : 0x3F0CCCCDu'),
    ('01C0_clamp_lt', PG, 'if (em_ee_c_lt_bits(f, UI_F_TWO))', 'if (em_ee_c_le_bits(f, UI_F_TWO))'),
    ('01C0_k2', PG, 'ui_fmul(0x3DAEC33Eu, z2)', 'ui_fmul(UI_F_K, z2)'),
    ('01C0_indirect_arg', PG, '(void)ui_call1(s, ui_lw(s, p + 0x4Cu), p);\n        }\n    } else {',
     '(void)ui_call1(s, ui_lw(s, p + 0x4Cu), p + 4u);\n        }\n    } else {'),
    ('0F30_sub', PG, 'ui_fsub(0x43240000u, fx)', 'ui_fadd(0x43240000u, fx)'),
    ('0F30_halfpi', PG, '0x3FC90FDBu', '0x3FC90FDAu'),
    ('1400_a3', PG, 'case 4: open = GATE4; slot = 4; a3 = 3; break;', 'case 4: open = GATE4; slot = 4; a3 = 2; break;'),
    ('F950_wrap0', PG, 'ui_sb(s, t + 0x12u, c != 0 ? c - 1u : 0xAu);\n                if (owned(s, ui_lbu(s, t + 0x12u))) break;',
     'ui_sb(s, t + 0x12u, c != 0 ? c - 1u : 0xBu);\n                if (owned(s, ui_lbu(s, t + 0x12u))) break;'),
    ('F950_wrap_up', PG, 'if ((int32_t)ui_lbu(s, t + 0x12u) >= 11) ui_sb', 'if ((int32_t)ui_lbu(s, t + 0x12u) > 11) ui_sb'),
    ('F950_help', PG, 'ui_sw(s, UI_D_00282240, 6);', 'ui_sw(s, UI_D_00282240, 5);'),
    ('3F30_range', PG, 'case 3: lo = 0x48; hi = 0x5D; break;', 'case 3: lo = 0x48; hi = 0x5C; break;'),
    ('4020_slot_sign', PG, 'if ((int8_t)v < 0) ui_sb', 'if ((int8_t)v <= 0) ui_sb'),
    ('4020_last_page', PG, 'ui_sh(s, t + 0x1Cu, (uint32_t)(ui_lh(s, tbl) - 1));', 'ui_sh(s, t + 0x1Cu, (uint32_t)(count - 1));'),
    ('4020_req_clear', PG, '            ui_sb(s, UI_D_008106B0, 0);\n            ui_sb(s, t + 3u, 1);', '            ui_sb(s, t + 3u, 1);'),
    ('34C0_next', PG, 'if (ui_lh(s, p + 0x1Cu) < count - 1) {', 'if (ui_lh(s, p + 0x1Cu) <= count - 1) {'),
    ('34C0_frame', PG, 'UiFrame fr = ui_enter(s, 0x002134C0u, 0x40);', 'UiFrame fr = ui_enter(s, 0x002134C0u, 0x30);'),
    ('31B0_line', PG, '0x64, 0x2F);', '0x64, 0x2E);'),
    ('BBC0_ramp08', FX, 'c[1] = 0x3F4CCCCDu;', 'c[1] = 0x3F4CCCCCu;'),
    ('BBC0_lcg', FX, 'const uint32_t seed_b = seed * 37u + 11u;', 'const uint32_t seed_b = seed * 37u + 12u;'),
    ('BBC0_ring_wrap', FX, 'if (!((int32_t)ui_lw(s, w + 0xB0u) < 0x41))', 'if (!((int32_t)ui_lw(s, w + 0xB0u) < 0x40))'),
    ('BBC0_age_step', FX, 'add = 0xFA;', 'add = 0xC8;'),
    ('BBC0_phase', FX, 'if (ui_lb(s, w + i) % period == 1) {', 'if (ui_lb(s, w + i) % period == 0) {'),
    ('BBC0_scale15', FX, 'case 1: ui_sw(s, 0x70003A28u, 0x3FC00000u); break;', 'case 1: ui_sw(s, 0x70003A28u, 0x3FC00001u); break;'),
    ('BBC0_op2_byte', FX, 'const uint32_t v = (uint32_t)ui_lb(s, e2 + 6u);', 'const uint32_t v = (uint32_t)ui_lb(s, e2 + 7u);'),
    ('BBC0_burst', FX, '0x00268480u + (uint32_t)(burst + 1) * 0x90u', '0x00268480u + (uint32_t)burst * 0x90u'),
    ('BBC0_buf', FX, 'const uint32_t buf = s->sp + 0xA0u;', 'const uint32_t buf = s->sp + 0xA4u;'),
    ('BBC0_scroll', FX, 'ui_sw(s, w + 0xA8u, ui_lw(s, w + 0xA8u) + 4u);', 'ui_sw(s, w + 0xA8u, ui_lw(s, w + 0xA8u) + 3u);'),
    ('BBC0_age_cut', FX, 'if (ui_lh(s, act + 0xEu) >= 0x3A99)', 'if (ui_lh(s, act + 0xEu) > 0x3A99)'),
    ('B7A0_table', FX, 'case 16: t = 0x00267760u; break;', 'case 16: t = 0x00267770u; break;'),
    ('B700_cap', FX, 'if (ui_lh(s, UI_D_00275BCC) < (int32_t)ui_lbu(s, p + 0xCu)) {\n        ui_sb(s, p + 4u, 3);\n        (void)ui_leave',
     'if (ui_lh(s, UI_D_00275BCC) <= (int32_t)ui_lbu(s, p + 0xCu)) {\n        ui_sb(s, p + 4u, 3);\n        (void)ui_leave'),
    ('BB70_row', FX, '(uint32_t)(i / 13) * 4u', '(uint32_t)(i / 12) * 4u'),
    # review round 2 survivors, now pinned
    ('7D90_b0_zext', PG, '((uint64_t)(int64_t)b0 |', '((uint64_t)(uint32_t)b0 |'),
    ('01C0_vy', PG, 'ui_sw(s, 0x700038A4u, vy);', 'ui_sw(s, 0x700038A4u, vy & 0u);'),   # stores 0 (vy kept used for -Werror)
    ('F950_floor_mask', PG, 'ui_sb(s, t + 0x14u, (ui_lbu(s, UI_D_008106CD) & 0x30u) >> 4);',
     'ui_sb(s, t + 0x14u, (ui_lbu(s, UI_D_008106CD) & 0x70u) >> 4);'),
    ('BBC0_wrap1', FX, 'if (ui_lb(s, w + i) >= 0x51) ui_sb(s, w + i, 0);', 'if (ui_lb(s, w + i) >= 0x50) ui_sb(s, w + i, 0);'),
    ('B7A0_t17', FX, 'case 17: t = 0x00267D90u;', 'case 17: t = 0x00267DA0u;'),
]


def mutants():
    """Build each mutant, run the quick case groups against it and report
    whether a case fails (killed) or all pass (survived)."""
    global ELF, NATIVE, POOL
    ELF = read_elf()
    R.ELF = ELF
    R.TRACKER = R.build_tracker()
    ref = R.image(USED[0])[0]
    for beat in USED:
        img = R.image(beat)[0]
        R.DIFF[beat] = frozenset(p for p in range(R.RAM_SIZE // R.PAGE)
                                 if img[p * R.PAGE:(p + 1) * R.PAGE] != ref[p * R.PAGE:(p + 1) * R.PAGE])
    mdir = OUT / 'mut'
    mdir.mkdir(parents=True, exist_ok=True)
    only = set(sys.argv[2:])
    rows = []
    for name, path, old, new in MUTANTS:
        if only and name not in only:
            continue
        srcs = {q: (ROOT / q).read_text() for q in SOURCES}
        assert srcs[path].count(old) == 1, ('mutant text not unique', name)
        srcs[path] = srcs[path].replace(old, new)
        files = []
        for q, text in srcs.items():
            f = mdir / Path(q).name
            f.write_text(text)
            files.append(str(f))
        NATIVE = build_native(files, 'mut_' + name)
        POOL = multiprocessing.get_context('fork').Pool(JOBS)
        verdict = 'survived'
        try:
            for gname, items, total, fn in case_groups():
                try:
                    pmap(fn, items)
                except AssertionError as ex:
                    verdict = 'killed by ' + gname + ': ' + repr(ex)[:160]
                    break
        finally:
            R.close_pool(POOL)
            POOL = None
        rows.append((name, verdict))
        print('%-18s %s' % (name, verdict), flush=True)
    return rows


def cover_item(arg):
    fn, it = arg
    r = fn(it)
    return set() if r in ('unmeasured', 'skipped') else r[0]


def cover():
    """Run every item, then print the greedy set of items that together
    reach every outcome any item reaches (paste into QUICK_PINS)."""
    global ELF, NATIVE, POOL
    ELF = read_elf()
    R.ELF = ELF
    NATIVE = build_native()
    R.TRACKER = R.build_tracker()
    ref = R.image(BEATS[0])[0]
    for beat in BEATS:
        img = R.image(beat)[0]
        R.DIFF[beat] = frozenset(p for p in range(R.RAM_SIZE // R.PAGE)
                                 if img[p * R.PAGE:(p + 1) * R.PAGE] != ref[p * R.PAGE:(p + 1) * R.PAGE])
    POOL = multiprocessing.get_context('fork').Pool(JOBS)
    rows = []
    for name, items, total, fn in case_groups(everything=True):
        outs = pmap(cover_item, [(fn, it) for it in items])
        rows += [(repr(it), o) for it, o in zip(items, outs)]
    R.close_pool(POOL)
    left = set().union(*(o for _, o in rows))
    chosen = []
    while left:
        best = max(rows, key=lambda r: len(r[1] & left))
        chosen.append(best[0])
        left -= best[1]
    print('QUICK_PINS = {')
    for c in sorted(chosen):
        print('    %r,' % c)
    print('}')


def debug(names):
    global ELF, NATIVE
    ELF = read_elf()
    R.ELF = ELF
    NATIVE = build_native()
    R.TRACKER = R.build_tracker()
    ref = R.image(BEATS[0])[0]
    for beat in BEATS:
        img = R.image(beat)[0]
        R.DIFF[beat] = frozenset(p for p in range(R.RAM_SIZE // R.PAGE)
                                 if img[p * R.PAGE:(p + 1) * R.PAGE] != ref[p * R.PAGE:(p + 1) * R.PAGE])
    table = {'db': (DB_ITEMS, db_case), 'helper': (helper_items(), helper_case), 'map': (map_items(), map_case),
             'node': (node_items(), node_case), 'burn': (burn_items(), burn_case2), 'pick': (pick_items(), pick_case),
             'seq': ([(S2, 1), (S2, 3), (S2, 5)], map_seq_case)}
    for name in names:
        group, _, idx = name.partition(':')
        items, fn = table[group]
        sel = items if not idx else [items[int(i)] for i in idx.split(',')]
        for it in sel:
            t = time.time()
            try:
                r = fn(it)
                print('ok', group, repr(it)[:120], 'calls', r[1] if r not in ('unmeasured', 'skipped') else r,
                      '%.2fs' % (time.time() - t))
            except AssertionError as ex:
                print('FAIL', group, repr(it)[:120], repr(ex)[:1500])


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == 'debug':
        debug(sys.argv[2:])
    elif len(sys.argv) > 1 and sys.argv[1] == 'cover':
        cover()
    elif len(sys.argv) > 1 and sys.argv[1] == 'mutants':
        mutants()
    else:
        main()
