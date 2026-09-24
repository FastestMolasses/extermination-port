#!/usr/bin/env python3
"""Execute the original main loop 0x1AAE40, its vblank handler 0x1AB140 and
the gap routine 001050E8, and compare em_main_loop_and_gap.c.

docs/MAIN_LOOP_AND_GAP.md. The user's pinned ELF and the captured route RAM
(../Extermination/build/s87/route/*/eeMemory.bin + scratchpad.bin, taken at
the loop top 0x1AAF28) supply every instruction and every input; nothing
original is embedded here. Executed unmodified:

  0x1AAE40..0x1AAF24  the start-up part of the main loop
  0x1AAF28..0x1AB134  one pass of its endless loop (steps A..W)
  0x1AB140..0x1AB1D8  the vblank interrupt handler (delivered by the oracle
                      at scripted points: a polling pass or inside a callee)
  0x1050E8..0x105147  the unlisted libmpeg routine in the 0x1050E4 gap

Every callee (the 44 distinct jal targets of the symbol, asserted against
the ELF) is hooked, scripted and recorded, never simulated as a claim about
the callee. The loop's own hardware accesses (the timer-0 stores, the GS CSR
load, the COP0 interrupt disable/enable) are recorded events too. The native
module gets the same script through its workers; every event (order and
arguments), the state at every event, the return code (the handler's v0
included) and every field of the final state are compared, and
every byte the original stores outside the stack must be a compared field.
These routines contain no COP1/VU0 arithmetic; the oracle faults on any.

Also checked against original captures (not the oracle):
  - the PCSX2 frame traces (../Extermination/build/s87/frame_trace/*.json,
    ORIGINAL_FRAME_ORDER.md): the native frame's call sites, in order;
  - the 15 route beats: the counter 0x70003B64 advances by exactly one per
    traced frame, the snapshot's counter is 0x70003B64, and D_00810E90 (the
    handler's vblank count) equals the recorded vsync counter;
  - asserted route facts: D_00810E90 - 0x70003B64 is the single value 9937
    over all 15 beats, (D_00810E80, D_00810E88) is (0, 1) or (1, 0), and
    D_00810E98 is 1 at every loop top.

The live em_frame.c is NOT audited here (this test depends only on the
lane's own files and the original); tools/test_main_loop_and_gap_frame_audit_reference.py
is a separate report for the lead.

Default run (~10 s): every case class and boundary, all 15 route beats with
the base script, a fixed-seed sample of the rest. EM_TEST_FULL=1: every
script on every captured state and the full random sweeps.
"""
import ctypes as C
import json
import os
import random
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode  # noqa: E402
from test_player_slide_reference import EE, read_elf, s32, sx32, RETURN  # noqa: E402

MASK = 0xFFFFFFFF
DECOMP = ROOT.parent / 'Extermination'
ROUTE = DECOMP / 'build/s87/route'
FRAME_TRACE = DECOMP / 'build/s87/frame_trace'
STARTUP = DECOMP / 'build/startup-reference'
LANE = os.environ.get('EM_LANE', 'b7-main-loop-and-gap')
OUT = ROOT / 'build' / LANE

MAIN, LOOP_TOP, BACK_BRANCH, MAIN_END = 0x1AAE40, 0x1AAF28, 0x1AB130, 0x1AB140
HANDLER, HANDLER_END = 0x1AB140, 0x1AB1DC
HANG_SITE = 0x1AAE58
SPIN_FIELD, SPIN_FIELD_BRANCH = 0x1AAEFC, 0x1AAF10
SPIN_VBLANK, SPIN_VBLANK_BRANCH = 0x1AAFF0, 0x1AB004
GAP, GAP_END, GAP_CONST = 0x1050E4, 0x105148, 0x105130
GAP_FN = 0x1050E8
EIE = 0x10000
GS_CSR = 0x12001000

# (worker field, kind, event name, original target). Order = EmMlgWorkers.
WORKERS = [
    ('w001AB1E0', 'ret', 0x1AB1E0), ('w001FEE60', 'p', 0x1FEE60), ('w001AB370', 'p', 0x1AB370),
    ('w001CCCC0', 'p', 0x1CCCC0), ('w001CCBD0', 'iii', 0x1CCBD0), ('w001AB430', 'p', 0x1AB430),
    ('w001FB210', 'p', 0x1FB210), ('w001F9820', 'p', 0x1F9820), ('w001F9780', 'p', 0x1F9780),
    ('w00101548', 'u', 0x101548), ('w001FF1E0', 'i', 0x1FF1E0), ('w001D0F20', 'p', 0x1D0F20),
    ('w001CCB10', 'p', 0x1CCB10), ('w001B5790', 'p', 0x1B5790), ('w00225CC0', 'p', 0x225CC0),
    ('w001AB650', 'p', 0x1AB650), ('w001AB740', 'iu', 0x1AB740), ('w001AED80', 'i', 0x1AED80),
    ('w001D1AE0', 'i', 0x1D1AE0), ('w001B57E0', 'p', 0x1B57E0), ('w001AEBE0', 'p', 0x1AEBE0),
    ('w001AB6A0', 'p', 0x1AB6A0), ('w001FCA10', 'p', 0x1FCA10), ('w001AEE70', 'p', 0x1AEE70),
    ('w001FB100', 'p', 0x1FB100), ('w001B5B70', 'p', 0x1B5B70), ('w00100A60', 'iiret', 0x100A60),
    ('w0011B910', 'p', 0x11B910), ('w0011B5E0', 'p', 0x11B5E0), ('w0011B328', 'p', 0x11B328),
    ('w0011AE88', 'p', 0x11AE88), ('w0011A9D8', 'p', 0x11A9D8), ('w001D7410', 'p', 0x1D7410),
    ('w001AB590', 'p', 0x1AB590), ('w00203350', 'p', 0x203350), ('w001D1C10', 'i', 0x1D1C10),
    ('w001AB4E0', 'ii', 0x1AB4E0), ('w001015A8', 'uiii', 0x1015A8), ('w00101810', 'uiii', 0x101810),
    ('w0010BAA0', 'i', 0x10BAA0), ('w00100550', 'u', 0x100550), ('w001D2300', 'p', 0x1D2300),
    ('w001D2580', 'i', 0x1D2580),
    ('io_store', 'uu', None), ('spin', 'u', None),
    ('cop0_di', 'status', None), ('gs_csr_load', 'csr', None), ('w0010C710', 'iret', 0x10C710),
    ('cop0_ei', 'p', None),
]
EVENT = {'io_store': 'io', 'spin': 'spin', 'cop0_di': 'di', 'gs_csr_load': 'csr', 'cop0_ei': 'ei'}
def event_name(field): return EVENT.get(field, field[1:])
TARGETS = {target: (field, kind) for field, kind, target in WORKERS if target is not None}
INIT_SET = [f for f, _, _ in WORKERS[:18]] + ['io_store', 'spin']
FRAME_SET = [f for f, _, _ in WORKERS[18:43]] + ['io_store', 'spin']
HANDLER_SET = ['cop0_di', 'gs_csr_load', 'w0010C710', 'cop0_ei']

# Call sites of the frame (the PCSX2 traces record these).
SITES = {'001D1AE0': [0x1AAF34], '001B57E0': [0x1AAF3C], '001AEBE0': [0x1AAF44],
         '001AB6A0': [0x1AAF4C], '001FCA10': [0x1AAF54], '001AEE70': [0x1AAF5C, 0x1AAFE8],
         '001FB100': [0x1AAF64], '001B5B70': [0x1AAF6C], '00100A60': [0x1AAF78],
         '0011B910': [0x1AAF88], '0011B5E0': [0x1AAF90], '0011B328': [0x1AAF98],
         '0011AE88': [0x1AAFA0], '0011A9D8': [0x1AAFA8], '001D7410': [0x1AAFB0],
         '001AB590': [0x1AAFB8], '00203350': [0x1AAFD4], '001D1C10': [0x1AAFE0],
         '001AB4E0': [0x1AB020], '001015A8': [0x1AB070], '00101810': [0x1AB0C0],
         '0010BAA0': [0x1AB0C8], '00100550': [0x1AB0EC], '001D2300': [0x1AB0F4],
         '001D2580': [0x1AB118]}

STATE_FIELDS = [('d810E98', 0x810E98, 4, True), ('d810E90', 0x810E90, 4, True),
                ('d810E80', 0x810E80, 2, True), ('d810E88', 0x810E88, 2, True),
                ('d821058', 0x821058, 1, False), ('d282184', 0x282184, 4, True),
                ('spad3B70', 0x70003B70, 2, True), ('spad3B72', 0x70003B72, 2, True),
                ('spad3B94', 0x70003B94, 2, True), ('spad3B96', 0x70003B96, 2, True),
                ('spad3B64', 0x70003B64, 4, False)]
COVERED = set()
for _n, _a, _s, _ in STATE_FIELDS:
    COVERED.update(range(_a, _a + _s))


def signed(value, size):
    bits = 8 * size
    value &= (1 << bits) - 1
    return value - (1 << bits) if value >> (bits - 1) else value


# ======================================================================
# The script both sides follow
# ======================================================================

class Script:
    """Callee results, the handler's inputs and where the vblank interrupt
    is delivered. fire: ('spin', site, k) = during the k-th polling pass at
    site; ('call', name, n) = inside the n-th call of that worker. csr and
    status are consumed in order (0 once exhausted, on both sides alike);
    so is wake, the v0 of each 0010C710 call (the handler returns it)."""

    def __init__(self, iop=0, jret=0, fire=(), csr=(), status=(), wake=(), fault=None):
        self.iop, self.jret, self.fault = iop, jret, fault
        self.fire = list(fire)
        self.csr, self.status, self.wake = list(csr), list(status), list(wake)
        self.counts = {}

    def fires(self, kind, key):
        n = self.counts.get((kind, key), 0)
        self.counts[(kind, key)] = n + 1
        return sum(1 for t in self.fire if t[0] == kind and t[1] == key and t[2] == n)

    def next_csr(self):
        return self.csr.pop(0) if self.csr else 0

    def next_status(self):
        return self.status.pop(0) if self.status else 0

    def next_wake(self):
        return self.wake.pop(0) if self.wake else 0


class Hang(Exception):
    pass


# ======================================================================
# The oracle: the shared EE core with the loop's hardware as events
# ======================================================================

class LoopEE(EE):
    def __init__(self, elf, ram, spad, script):
        super().__init__(elf, ram, spad)
        self.script = script
        self.events, self.snapshots = [], []
        self.written = set()
        self.di_pending = False
        self.spins = 0
        for target, (field, kind) in TARGETS.items():
            self.hooks[target] = self._hook(field, kind)

    # ---- state -----------------------------------------------------------
    def state(self):
        return {name: (signed(self.load(a, s), s) if sgn else self.load(a, s))
                for name, a, s, sgn in STATE_FIELDS}

    def event(self, *entry):
        self.events.append(entry)
        self.snapshots.append(self.state())

    def deliver(self, count):
        for _ in range(count):
            self.nested(HANDLER)

    # ---- callees -----------------------------------------------------------
    def _hook(self, field, kind):
        name = event_name(field)

        def hook(ee):
            a = [ee.r[4 + i] & MASK for i in range(4)]
            args = {'p': (), 'ret': (), 'i': (s32(a[0]),), 'iret': (s32(a[0]),), 'u': (a[0],),
                    'iii': (s32(a[0]), s32(a[1]), s32(a[2])), 'iu': (s32(a[0]), a[1]),
                    'iiret': (s32(a[0]), s32(a[1])), 'ii': (s32(a[0]), s32(a[1])),
                    'uiii': (a[0], s32(a[1]), s32(a[2]), s32(a[3]))}[kind]
            ee.event(name, *args)
            ee.deliver(ee.script.fires('call', name))
            if field == 'w001AB1E0':
                ee.r[2] = sx32(ee.script.iop)
            elif field == 'w00100A60':
                ee.r[2] = sx32(ee.script.jret)
            elif field == 'w0010C710':
                ee.r[2] = sx32(ee.script.next_wake())
        return hook

    # ---- hardware ------------------------------------------------------------
    def load(self, address, size=4):
        a = address & MASK
        if 0x10000000 <= a < 0x20000000:
            assert a == GS_CSR and size == 8, ('hardware load', hex(a), size)
            value = self.script.next_csr()
            self.event('csr', value)
            return value
        return super().load(address, size)

    def save(self, address, value, size=4):
        a = address & MASK
        if 0x10000000 <= a < 0x20000000:
            assert size == 4 and a in (0x10000000, 0x10000010), ('hardware store', hex(a), size)
            self.event('io', a, value & MASK)
            return
        if not 0x7F000000 <= a < 0x7F100000:
            self.written.update(range(a, a + size))
        super().save(address, value, size)

    def execute(self, word, pc):
        op = word >> 26
        if op == 16:                                            # COP0
            if word == 0x42000039:                              # di
                self.di_pending = True
                return
            if word == 0x42000038:                              # ei
                self.event('ei')
                return
            rs, rt, rd = word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
            assert rs == 0 and rd == 12 and self.di_pending, ('COP0', hex(word), hex(pc))
            self.di_pending = False
            status = self.script.next_status()
            self.event('di', status)
            if rt: self.r[rt] = sx32(status)
            return
        assert op not in (17, 18), ('float op in an integer routine', hex(word), hex(pc))
        super().execute(word, pc)

    def mmi(self, word, pc):
        rs, rt, rd = word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        fn, sub = word & 63, word >> 6 & 31
        if (fn, sub) not in ((0x08, 0x07), (0x28, 0x07), (0x08, 0x1B)):
            return super().mmi(word, pc)
        full = lambda n: (self.r[n] & 0xFFFFFFFFFFFFFFFF) | (self.rh[n] << 64)
        a, b = full(rs), full(rt)
        ha = [signed(a >> 16 * i, 2) for i in range(8)]
        hb = [signed(b >> 16 * i, 2) for i in range(8)]
        if sub == 0x1B:                                         # ppacb
            lanes = [x & 0xFF for x in hb] + [x & 0xFF for x in ha]
            value = sum(v << 8 * i for i, v in enumerate(lanes))
        else:
            pick = max if fn == 0x08 else min                   # pmaxh / pminh
            value = sum((pick(x, y) & 0xFFFF) << 16 * i for i, (x, y) in enumerate(zip(ha, hb)))
        if rd:
            self.r[rd] = value & 0xFFFFFFFFFFFFFFFF
            self.rh[rd] = value >> 64

    def branch(self, word, pc):
        b = super().branch(word, pc)
        if pc == HANG_SITE:
            raise Hang()
        if b is not None and b[0] and pc in (SPIN_FIELD_BRANCH, SPIN_VBLANK_BRANCH):
            site = SPIN_FIELD if pc == SPIN_FIELD_BRANCH else SPIN_VBLANK
            self.spins += 1
            assert self.spins < 64, 'the polling loop never saw the interrupt'
            self.event('spin', site)
            self.deliver(self.script.fires('spin', site))
        return b


def patch_state(ee, patch):
    for name, a, s, _ in STATE_FIELDS:
        if name in patch:
            ee.save(a, patch[name] & ((1 << 8 * s) - 1), s)
    ee.written.clear()


def original_frame(elf, ram, spad, script, patch):
    ee = LoopEE(elf, ram, spad, script)
    patch_state(ee, patch)
    before = ee.state()

    def stop(e):
        e.r[31] = RETURN
    ee.hooks[LOOP_TOP] = stop
    ee.execute(ee.load(LOOP_TOP), LOOP_TOP)
    ee.run(LOOP_TOP + 4)
    return 0, before, ee


def original_init(elf, script, patch):
    ee = LoopEE(elf, None, None, script)
    patch_state(ee, patch)
    before = ee.state()

    def stop(e):
        e.r[31] = RETURN
    ee.hooks[LOOP_TOP] = stop
    ee.r[31] = RETURN
    try:
        ee.run(MAIN)
        rc = 0
    except Hang:
        rc = 1
    return rc, before, ee


def original_handler(elf, script, patch):
    """Returns the handler's v0 (0x1AB1D4) as ee.v0."""
    ee = LoopEE(elf, None, None, script)
    patch_state(ee, patch)
    before = ee.state()
    v0, _ = ee.nested(HANDLER)
    ee.v0 = s32(v0 & MASK)
    return 0, before, ee


# ======================================================================
# The native module
# ======================================================================

I32, U32, P = C.c_int32, C.c_uint32, C.POINTER
KINDS = {
    'p': [], 'i': [I32], 'u': [U32], 'iii': [I32, I32, I32], 'iu': [I32, U32],
    'ret': [P(I32)], 'iret': [I32, P(I32)], 'iiret': [I32, I32, P(I32)], 'ii': [I32, I32],
    'uiii': [U32, I32, I32, I32], 'uu': [U32, U32], 'status': [P(U32)], 'csr': [P(C.c_uint64)],
}
FN = {k: C.CFUNCTYPE(C.c_int, C.c_void_p, *v) for k, v in KINDS.items()}


# The storage of the globals (one per run) and EmMlgState, which points at it.
STORE_TYPES = [('d810E98', I32), ('d810E90', I32), ('d810E80', C.c_int16), ('d810E88', C.c_int16),
               ('d821058', C.c_uint8), ('d282184', I32), ('spad3B70', C.c_int16),
               ('spad3B72', C.c_int16), ('spad3B94', C.c_int16), ('spad3B96', C.c_int16),
               ('spad3B64', U32)]


class Store(C.Structure):
    _fields_ = STORE_TYPES


class State(C.Structure):
    _fields_ = [(n, P(t)) for n, t in STORE_TYPES]


def state_view(store, null=()):
    """EmMlgState over `store`; the fields in `null` stay unbound."""
    view = State()
    for n, t in STORE_TYPES:
        if n not in null:
            setattr(view, n, C.cast(C.addressof(store) + getattr(Store, n).offset, P(t)))
    return view


class Workers(C.Structure):
    _fields_ = [('context', C.c_void_p)] + [(f, FN[k]) for f, k, _ in WORKERS]


NATIVE = None


def build_native():
    global NATIVE
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / ('mlg.dylib' if sys.platform == 'darwin' else 'mlg.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc',
                    'src/game/em_main_loop_and_gap.c', '-o', str(lib)], cwd=ROOT, check=True)
    NATIVE = C.CDLL(str(lib))
    for name in ('em_mlg_001AAE40_init', 'em_mlg_001AAE40_frame'):
        getattr(NATIVE, name).argtypes = [P(Workers), P(State)]
        getattr(NATIVE, name).restype = C.c_int
    NATIVE.em_mlg_001AB140.argtypes = [P(Workers), P(State), P(I32)]
    NATIVE.em_mlg_001AB140.restype = C.c_int
    NATIVE.em_mlg_001050E8.argtypes = [C.c_void_p, C.c_void_p]
    NATIVE.em_mlg_001050E8.restype = None


def state_of(st):
    return {name: getattr(st, name) for name, _, _, _ in STATE_FIELDS}


V0_SENTINEL = 0x5A5A5A5A


def native_run(entry, before, script, unbound=(), null=()):
    """Run a native routine with Python workers that follow `script`.
    Returns (rc, events, snapshots, final state, v0); v0 is the handler's
    result when entry is the handler (V0_SENTINEL if it was not written),
    else None. The handler's v0 inside a frame goes to the kernel (the
    oracle drops it too) and is discarded here."""
    st = Store(**before)
    view = state_view(st, null)
    events, snapshots, keep = [], [], []
    holder = {}
    dropped = I32(0)

    def record(*entry):
        events.append(entry)
        snapshots.append(state_of(st))
        return script.fault is not None and len(events) - 1 == script.fault

    def deliver(count):
        for _ in range(count):
            if NATIVE.em_mlg_001AB140(C.byref(holder['w']), C.byref(view), C.byref(dropped)) < 0:
                return -1
        return 0

    def make(field, kind):
        name = event_name(field)

        def fn(_ctx, *args):
            if field == 'spin':
                if record('spin', args[0]): return -1
                return deliver(script.fires('spin', args[0]))
            if field == 'io_store':
                return -1 if record('io', args[0], args[1]) else 0
            if field == 'cop0_di':
                status = script.next_status()
                if record('di', status): return -1
                args[0][0] = status
                return 0
            if field == 'gs_csr_load':
                csr = script.next_csr()
                if record('csr', csr): return -1
                args[0][0] = csr
                return 0
            if field == 'cop0_ei':
                return -1 if record('ei') else 0
            plain = tuple(a for a in args if not isinstance(a, C._Pointer))
            if record(name, *plain): return -1
            if deliver(script.fires('call', name)) < 0: return -1
            if field == 'w001AB1E0': args[0][0] = script.iop
            if field == 'w00100A60': args[2][0] = script.jret
            if field == 'w0010C710': args[1][0] = script.next_wake()
            return 0
        return FN[kind](fn)

    w = Workers()
    for field, kind, _ in WORKERS:
        if field in unbound:
            continue
        cb = make(field, kind)
        keep.append(cb)
        setattr(w, field, cb)
    holder['w'] = w
    if entry == 'em_mlg_001AB140':
        v0 = I32(V0_SENTINEL)
        rc = NATIVE.em_mlg_001AB140(C.byref(w), C.byref(view), C.byref(v0))
        return rc, events, snapshots, state_of(st), v0.value
    rc = getattr(NATIVE, entry)(C.byref(w), C.byref(view))
    return rc, events, snapshots, state_of(st), None


# ======================================================================
# Comparison
# ======================================================================

FAILS = []


def check(cond, *what):
    if not cond:
        FAILS.append(what)
        if len(FAILS) <= 5:
            print('FAIL', *what, flush=True)


def compare(label, entry, rc, before, ee, script_args):
    nrc, events, snapshots, after, v0 = native_run(entry, before, Script(**script_args))
    check(nrc == rc, label, 'return', rc, nrc)
    check(events == ee.events, label, 'events', first_diff(ee.events, events))
    # The state at every event: where each store sits against every call.
    check(snapshots == ee.snapshots, label, 'state at event', first_diff(ee.snapshots, snapshots))
    check(after == ee.state(), label, 'state', ee.state(), after)
    if entry == 'em_mlg_001AB140':
        check(v0 == ee.v0, label, 'handler v0', ee.v0, v0)
    stray = sorted(a for a in ee.written if a not in COVERED)
    check(not stray, label, 'original stores outside the compared fields', [hex(a) for a in stray[:8]])
    return events


def first_diff(a, b):
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return i, x, y
    return len(a), len(b)


def fault_cases(label, entry, before, ee, script_args, picks):
    """Native fault at event k: the native returns -1 at once, its events are
    the original's first k+1 and its state is the original's at event k."""
    for k in picks:
        if k >= len(ee.events) or ee.events[k][0] in ('di', 'csr', 'ei', '0010C710'):
            continue            # faults inside the handler: tested on the handler itself
        args = dict(script_args, fault=k)
        nrc, events, _, after, _ = native_run(entry, before, Script(**args))
        check(nrc == -1, label, 'fault', k, 'return', nrc)
        check(events == ee.events[:k + 1], label, 'fault', k, 'events', first_diff(ee.events[:k + 1], events))
        check(after == ee.snapshots[k], label, 'fault', k, 'state', ee.snapshots[k], after)


# The globals each routine reads or writes (a NULL pointer fails it).
INIT_DATA = ['d810E88']
FRAME_DATA = ['d810E98', 'd810E80', 'd810E88', 'd821058', 'spad3B70', 'spad3B72', 'spad3B94',
              'spad3B96', 'spad3B64']
HANDLER_DATA = ['d810E98', 'd810E90', 'd810E88', 'd282184']


def unbound_cases(label, entry, before, required, data):
    for field in required:
        nrc, events, _, after, v0 = native_run(entry, before, Script(), unbound=(field,))
        check(nrc == -1 and not events and after == before and v0 in (None, V0_SENTINEL),
              label, 'unbound', field, nrc, len(events))
    for name in data:
        nrc, events, _, after, v0 = native_run(entry, before, Script(), null=(name,))
        check(nrc == -1 and not events and after == before and v0 in (None, V0_SENTINEL),
              label, 'NULL state pointer', name, nrc, len(events))
    # The globals it does not use may stay unbound (not for the frame: its
    # vblank wait needs the handler, which uses all the others).
    if entry != 'em_mlg_001AAE40_frame':
        unused = [n for n, _ in STORE_TYPES if n not in data]
        nrc, _, _, _, _ = native_run(entry, dict(before, d810E88=1), Script(), null=unused)
        check(nrc == 0, label, 'unused state pointers NULL', nrc)


# ======================================================================
# Inputs
# ======================================================================

def captured_states():
    """(label, eeMemory, scratchpad) at the loop top: the 15 route beats and
    the startup-reference captures that carry a scratchpad."""
    out = []
    for d in sorted(ROUTE.iterdir()):
        if not reference_mode.in_scope_beat(d.name):
            continue
        if (d / 'eeMemory.bin').exists() and (d / 'scratchpad.bin').exists():
            out.append((d.name, d / 'eeMemory.bin', d / 'scratchpad.bin'))
    for d in sorted(STARTUP.rglob('scratchpad.bin')):
        if (d.parent / 'eeMemory.bin').exists():
            out.append(('startup-reference/' + str(d.parent.relative_to(STARTUP)),
                        d.parent / 'eeMemory.bin', d))
    assert len([o for o in out if not o[0].startswith('startup')]) == 15, 'route beats missing'
    return out


def csr_value(rng, field):
    return (rng.getrandbits(64) & ~(1 << 13)) | (field << 13)


def frame_scripts(rng):
    """Script classes: J result, interrupt timing, handler inputs, movie gate,
    and the sign/parity of every halfword the loop reads."""
    base = dict(jret=0, fire=[('spin', SPIN_VBLANK, 0)], csr=[1 << 13], status=[0])
    cases = [('base', base, {})]
    for jret in (1, -1, 0x7FFFFFFF, -0x80000000):
        cases.append((f'jret={jret:#x}', dict(base, jret=jret), {}))
    for movie in (0, 1, 2, 0xFF):
        cases.append((f'movie={movie}', base, {'d821058': movie}))
    cases.append(('overrun: vblank inside E', dict(base, fire=[('call', '001AB6A0', 0)]), {}))
    cases.append(('overrun: two vblanks in M', dict(base, fire=[('call', '00203350', 0)] * 2),
                  {'d821058': 1}))
    cases.append(('wait: vblank on pass 3', dict(base, fire=[('spin', SPIN_VBLANK, 3)]), {}))
    cases.append(('late: vblank inside V after the wait', dict(
        base, fire=[('spin', SPIN_VBLANK, 0), ('call', '001D2300', 0)], csr=[0, 1 << 13]), {}))
    cases.append(('handler retries di twice', dict(base, status=[EIE | 1, EIE, 0]), {}))
    for parity, field in ((0, 0), (0, 1), (1, 0), (1, 1), (-1, 0x7FFF), (0x7FFF, -0x8000), (2, 2)):
        cases.append((f'parity={parity} field={field}', dict(base, csr=[csr_value(rng, field & 1)]),
                      {'d810E80': parity, 'd810E88': field}))
    cases.append(('negative scratchpad halfwords', base,
                  {'spad3B70': -1, 'spad3B72': -0x8000, 'spad3B94': -3, 'spad3B96': 0x7FFF}))
    cases.append(('counter wrap', base, {'spad3B64': 0xFFFFFFFF, 'd810E90': -1, 'd810E98': 5}))
    n = reference_mode.pick(400, 24)
    for i in range(n):
        fire = [('spin', SPIN_VBLANK, rng.randrange(3))] if rng.random() < 0.7 else \
               [('call', rng.choice(['001D1AE0', '001AB6A0', '001FB100', '001D7410', '001AB590']), 0)]
        script = dict(jret=rng.choice([0, 0, 1, rng.getrandbits(32) - (1 << 31)]), fire=fire,
                      csr=[rng.getrandbits(64) for _ in range(3)],
                      status=[rng.choice([0, EIE, EIE | 0xFF, 0x13]) for _ in range(4)] + [0])
        patch = {'d810E80': rng.choice([0, 1, rng.getrandbits(16)]),
                 'd810E88': rng.choice([0, 1, rng.getrandbits(16)]),
                 'd821058': rng.choice([0, 1, rng.getrandbits(8)]),
                 'spad3B70': rng.getrandbits(16), 'spad3B72': rng.getrandbits(16),
                 'spad3B94': rng.getrandbits(16), 'spad3B96': rng.getrandbits(16),
                 'spad3B64': rng.getrandbits(32), 'd810E90': rng.getrandbits(32),
                 'd282184': rng.getrandbits(32)}
        cases.append((f'random {i}', script, patch))
    return cases


def assert_static(elf):
    """Structure facts the translation and the census notes rely on."""
    def w(a): return struct.unpack_from('<I', elf, a - 0x100000 + 0x300)[0]
    targets = {((w(a) & 0x3FFFFFF) << 2) for a in range(MAIN, HANDLER_END, 4) if w(a) >> 26 == 3}
    assert targets == set(TARGETS), ('jal targets', sorted(map(hex, targets ^ set(TARGETS))))
    returns = [a for a in range(MAIN, HANDLER_END, 4) if w(a) == 0x03E00008]
    assert returns == [HANDLER_END - 8], [hex(a) for a in returns]       # the loop never returns
    # The gap: one zero word, then one routine whose only return is 0x105140,
    # with the 0x00FF halfword constant inside its own body.
    assert w(GAP) == 0
    assert [a for a in range(GAP, GAP_END, 4) if w(a) == 0x03E00008] == [0x105140]
    assert [w(a) for a in range(GAP_CONST, GAP_CONST + 16, 4)] == [0x00FF00FF] * 4
    callers, refs = [], []
    for a in range(0x100000, 0x100000 + 0x175B00, 4):
        x = w(a)
        if x >> 26 in (2, 3) and GAP <= ((a + 4) & 0xF0000000 | (x & 0x3FFFFFF) << 2) < GAP_END:
            callers.append(a)
        if x >> 26 == 15 and (x & 0xFFFF) == 0x10:
            rt = x >> 16 & 31
            for b in range(a + 4, a + 64, 4):
                y = w(b)
                if y >> 21 & 31 == rt and y >> 26 in (9, 8):
                    if 0x100000 + (y & 0xFFFF) - (0x10000 if y & 0x8000 else 0) == GAP_CONST:
                        refs.append(a)
                    break
    assert callers == [0x1042FC, 0x104334], [hex(a) for a in callers]   # both inside 001041E8
    assert refs == [0x10508C, 0x1050EC], [hex(a) for a in refs]         # 00105088 and 001050E8
    return len(targets)


# ======================================================================
# Original captures
# ======================================================================

def trace_checks():
    """PCSX2 frame traces: the native frame's call sites in order."""
    frames = 0
    for path in sorted(FRAME_TRACE.glob('*.json')):
        doc = json.loads(path.read_text())
        if not isinstance(doc, dict) or 'frames' not in doc:
            continue
        for fr in doc['frames']:
            traced = [int(e['pc'], 16) for e in fr['events'] if e.get('fn') == 'main']
            jret = 1 if 0x1AAF88 in traced else 0
            before = {name: 0 for name, _, _, _ in STATE_FIELDS}
            before.update(d821058=fr['D_00821058'], spad3B64=fr['counter'], d810E88=1)
            script = dict(jret=jret, fire=[('spin', SPIN_VBLANK, 0)], csr=[0])
            rc, events, _, after, _ = native_run('em_mlg_001AAE40_frame', before, Script(**script))
            seen, sites = {}, []
            for e in events:
                if e[0] in SITES:
                    k = seen.get(e[0], 0)
                    seen[e[0]] = k + 1
                    sites.append(SITES[e[0]][k])
            check(rc == 0 and sites == traced, path.name, fr['counter'], first_diff(traced, sites))
            check(after['spad3B64'] == (fr['counter'] + 1) & MASK, path.name, 'counter')
            frames += 1
    assert frames >= 17, ('frame traces', frames)
    return frames


def route_checks():
    """Route beats: W advances the counter by exactly one per frame; the
    snapshot's counter is 0x70003B64 and its vsync counter is D_00810E90.
    Asserted facts of docs/MAIN_LOOP_AND_GAP.md section 2.4: D_00810E90 -
    0x70003B64 is one constant (9937) over all beats, (D_00810E80,
    D_00810E88) is (0, 1) or (1, 0), and D_00810E98 is 1 at every loop top."""
    rows = beats = 0
    offsets, phases, flags = set(), set(), set()
    for d in sorted(ROUTE.iterdir()):
        if not (d / 'trace.json').exists() or not reference_mode.in_scope_beat(d.name):
            continue
        doc = json.loads((d / 'trace.json').read_text())
        counters = [r['counter'] for r in doc['rows']]
        check(all(b - a == 1 for a, b in zip(counters, counters[1:])), d.name, 'counter step')
        snap = json.loads((d / 'snapshot.json').read_text())
        spad = (d / 'scratchpad.bin').read_bytes()
        with open(d / 'eeMemory.bin', 'rb') as f:
            f.seek(0x810E80)
            e80, e88, e90, e98 = struct.unpack('<h2x4xh2x4xI4xi', f.read(0x1C))
        phases.add((e80, e88))
        flags.add(e98)
        check(e98 == 1, d.name, 'D_00810E98 at the loop top', e98)
        c = struct.unpack_from('<I', spad, 0x3B64)[0]
        check(snap['main_loop_counter'] == counters[-1] == c, d.name, 'snapshot counter')
        check(snap['vsync_counter'] == e90, d.name, 'vsync counter')
        offsets.add(e90 - c)
        rows += len(counters)
        beats += 1
    assert beats == 15, beats
    check(offsets == {9937}, 'route', 'D_00810E90 - 0x70003B64 not one constant 9937', sorted(offsets))
    check(phases <= {(0, 1), (1, 0)}, 'route', '(D_00810E80, D_00810E88) phase', sorted(phases))
    return rows, beats, offsets, phases, flags


# ======================================================================
# Main
# ======================================================================

def main():
    elf = read_elf()
    build_native()
    targets = assert_static(elf)
    rng = random.Random(0x1AAE40)

    # ---- one frame over every captured state ----
    states = captured_states()
    scripts = frame_scripts(rng)
    runs = []
    for i, (label, ram_path, spad_path) in enumerate(states):
        for j, (name, script, patch) in enumerate(scripts):
            runs.append((i, j))
    base_only = lambda idx, item: item[1] == 0 or item[0] == 0
    picked = reference_mode.select(runs, 60, 0x1AAF28, keep=base_only)
    by_state = {}
    for i, j in picked:
        by_state.setdefault(i, []).append(j)
    fault_budget = reference_mode.pick(10_000, 6)
    frame_events = 0
    for i, js in sorted(by_state.items()):
        label, ram_path, spad_path = states[i]
        ram, spad = ram_path.read_bytes(), spad_path.read_bytes()
        code = elf[MAIN - 0x100000 + 0x300:HANDLER_END - 0x100000 + 0x300]
        assert ram[MAIN:HANDLER_END] == code, (label, 'loop code in RAM differs from the ELF')
        for j in js:
            name, script, patch = scripts[j]
            rc, before, ee = original_frame(elf, ram, spad, Script(**script), patch)
            events = compare(f'{label} / {name}', 'em_mlg_001AAE40_frame', rc, before, ee, script)
            frame_events += len(events)
            if fault_budget > 0 and (j == 0 or reference_mode.FULL):
                picks = range(len(ee.events)) if reference_mode.FULL else \
                    sorted(rng.sample(range(len(ee.events)), min(4, len(ee.events))))
                fault_cases(f'{label} / {name}', 'em_mlg_001AAE40_frame', before, ee, script, picks)
                fault_budget -= 1
        if i == 0:
            unbound_cases(label, 'em_mlg_001AAE40_frame', before, FRAME_SET, FRAME_DATA)

    # ---- start-up over the ELF image ----
    init_cases = [
        ('iop fails', dict(iop=1), {}),
        ('iop fails (negative)', dict(iop=-1), {}),
        ('odd field on the first pass', dict(fire=[('spin', SPIN_FIELD, 0)], csr=[1 << 13]), {}),
        ('even fields first', dict(fire=[('spin', SPIN_FIELD, k) for k in range(3)],
                                   csr=[0, 0, (1 << 13) | 0xFFFF]), {}),
        ('vblank inside 001AB740', dict(fire=[('call', '001AB740', 0)], csr=[1 << 13]), {}),
        ('field already set', dict(), {'d810E88': 1}),
        ('field set to a negative halfword', dict(), {'d810E88': -2}),
    ]
    for k in range(reference_mode.pick(60, 4)):
        n = rng.randrange(1, 5)
        init_cases.append((f'random {k}', dict(
            fire=[('spin', SPIN_FIELD, i) for i in range(n)],
            csr=[rng.getrandbits(64) & ~(1 << 13) for _ in range(n - 1)] + [rng.getrandbits(64) | 1 << 13],
            status=[rng.choice([0, EIE]) for _ in range(3)] + [0]), {}))
    for name, script, patch in init_cases:
        rc, before, ee = original_init(elf, Script(**script), patch)
        compare(f'init / {name}', 'em_mlg_001AAE40_init', rc, before, ee, script)
        if name == 'even fields first':
            fault_cases('init', 'em_mlg_001AAE40_init', before, ee, script,
                        range(len(ee.events)) if reference_mode.FULL else (0, 3, 17, len(ee.events) - 1))
            unbound_cases('init', 'em_mlg_001AAE40_init', before, INIT_SET, INIT_DATA)

    # ---- the handler alone ----
    handler_cases = []
    for k in range(reference_mode.pick(400, 40)):
        handler_cases.append((dict(csr=[rng.getrandbits(64) if k % 3 else (rng.getrandbits(1) << 13)],
                                   status=[EIE] * (k % 4) + [rng.getrandbits(16) & ~EIE],
                                   wake=[rng.choice([0, -1, 3, 0xFF, 0x100, rng.getrandbits(32) - (1 << 31)])]),
                              {'d810E98': rng.choice([0, 1, -1, rng.getrandbits(32)]),
                               'd810E90': rng.choice([0, -1, 0x7FFFFFFF, rng.getrandbits(32)]),
                               'd810E88': rng.getrandbits(16), 'd282184': rng.getrandbits(32)}))
    for k, (script, patch) in enumerate(handler_cases):
        rc, before, ee = original_handler(elf, Script(**script), patch)
        compare(f'handler {k}', 'em_mlg_001AB140', rc, before, ee, script)
        if k < 3:
            for f in range(len(ee.events)):
                nrc, events, _, after, v0 = native_run('em_mlg_001AB140', before, Script(**dict(script, fault=f)))
                check(nrc == -1 and events == ee.events[:f + 1] and after == ee.snapshots[f]
                      and v0 == V0_SENTINEL, 'handler fault', k, f)
    unbound_cases('handler', 'em_mlg_001AB140', {n: 0 for n, _, _, _ in STATE_FIELDS}, HANDLER_SET,
                  HANDLER_DATA)

    # ---- the gap routine 001050E8 ----
    gap = LoopEE(elf, None, None, Script())
    DST, SRC = 0x700000, 0x710000
    edge = [0, 1, -1, 254, 255, 256, 0x7FFF, -0x8000, 0x00FF, 0x0100, 0xFF00 - 0x10000, 128]
    gap_cases = [[edge[i % len(edge)] for i in range(384)]]
    for _ in range(reference_mode.pick(300, 12)):
        gap_cases.append([rng.choice([rng.randrange(-0x8000, 0x8000), rng.randrange(-4, 260)])
                          for _ in range(384)])
    for k, values in enumerate(gap_cases):
        raw = struct.pack('<384h', *values)
        gap.write(SRC, raw)
        gap.write(DST, b'\xA5' * 0x200)
        gap.written.clear()
        gap.call(GAP_FN, (DST, SRC))
        check(gap.written == set(range(DST, DST + 384)), 'gap', k, 'store range',
              min(gap.written, default=0), max(gap.written, default=0))
        want = gap.read(DST, 384)
        src_buf = C.create_string_buffer(raw, 768)
        dst_buf = C.create_string_buffer(b'\xA5' * 384, 384)
        NATIVE.em_mlg_001050E8(dst_buf, src_buf)
        check(dst_buf.raw == want, 'gap', k, first_diff(want, dst_buf.raw))

    # ---- original captures ----
    traced = trace_checks()
    rows, beats, offsets, phases, flags = route_checks()

    reference_mode.banner(
        f'{targets} callees hooked (the ELF\'s jal targets)',
        reference_mode.part(len(picked), len(runs), 'frame runs'),
        f'{len(states)} captured states', f'{frame_events} frame events',
        f'{len(init_cases)} start-up runs', f'{len(handler_cases)} handler runs',
        f'{len(gap_cases)} gap runs', f'{traced} PCSX2-traced frames',
        f'{rows} route counter rows over {beats} beats')
    print(f'route: D_00810E90 - 0x70003B64 = {sorted(offsets)}; (D_00810E80, D_00810E88) = '
          f'{sorted(phases)}; D_00810E98 = {sorted(flags)} over every beat snapshot', flush=True)
    if FAILS:
        print(f'{len(FAILS)} FAILURES', flush=True)
        sys.exit(1)
    print('main loop 0x1AAE40, handler 0x1AB140 and gap 001050E8: native == original', flush=True)


if __name__ == '__main__':
    main()
