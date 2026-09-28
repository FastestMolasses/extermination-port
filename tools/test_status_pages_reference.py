#!/usr/bin/env python3
"""Execute the ORIGINAL status-page routines the first level can reach and
compare the native em_status_pages_* translations byte for byte.

docs/STATUS_PAGES.md. The user's pinned ELF and the recorded AREA11
captures (../Extermination/build/startup-reference/status-hub and
../Extermination/build/s87/route/<beat>/{eeMemory,scratchpad}.bin) supply
every instruction and every table; none are embedded here.

Method: the AREA01 UI lane's (tools/test_area01_ui_reference.py, whose
interpreter, buffers, dirty-page tracker, entry check and run_both are
imported and pointed at this lane's routines). The oracle runs the original
routine over a captured image; every callee outside the routine's lane runs
as ORIGINAL code too, nested, and its entry is logged (address, stack
pointer, the 64-bit integer argument registers it takes, the float argument
registers). The native translation runs over a second copy; each callee it
reaches through EmArea01Ui.call runs the same ORIGINAL code in a second
interpreter sharing the native memory. At every callee entry all 32 MiB of
RAM and the 16 KiB scratchpad must equal the oracle's at the same call, and
at the end the results, the call log, RAM and scratchpad must be identical.

Routines of this lane call each other directly (both sides run them as one
routine); routines of the AREA01 lane (em_area01_ui_*) are callees here,
and this lane's routines are callees of theirs. The `reuse` group runs the
AREA01 translations of MAP 0020F950 and DATABASE 00214020 over the AREA11
images (their callees as original code), which is what the binding reuses.

Cases are designed inputs over the captured images: no capture shows these
pages open (docs/STATUS_PAGES.md section 3 lists the groups). Every
conditional branch of this lane's routines must be taken both ways except
the ones UNREACHED lists.

EM_TEST_FULL=1 runs every generated case; the default runs the outcome
cover (QUICK_PINS, from the `cover` command) plus EXTRA_PINS, with the
heavy callees memoised / replayed and 0020A7A0 / 0020AC70 as entry-checked
boundaries (docs/STATUS_PAGES.md section 3). Both runs add SURVIVOR_PINS,
the cases that kill the review's surviving mutants. Commands: `debug`,
`cover`, `mutants`. Both runs use up to four worker processes;
EM_TEST_JOBS=n overrides (1: serial).
"""
import ctypes as C
import hashlib
import math
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
os.environ.setdefault('EM_LANE', 'status_pages')
import reference_mode as RM  # noqa: E402
import test_area01_render_reference as R  # noqa: E402
import test_area01_ui_reference as U  # noqa: E402
from test_player_slide_reference import EE, read_elf  # noqa: E402

MASK, MASK64 = 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF
OUT = ROOT / 'build' / 'b15' / 'status_pages'
R.OUT = U.OUT = OUT
F = R.F
T = 0x810130                       # the status block D_00810130
FREE = 0x1A00000                   # zero in every capture: crafted inputs live here
# Worker processes: up to four in both runs (EM_TEST_JOBS overrides). The
# quick-mode memo is per process, so workers re-run the heavy callees: the
# default run costs about 10 % more CPU in parallel (13.9 s against 12.7 s
# serial on the M1, 2026-09-27) but ends in about 5 s of wall time instead
# of 13 s (the ~10 s default budget). EM_TEST_JOBS=1 runs it serially.
JOBS = R.JOBS

CAP = ROOT.parent / 'Extermination' / 'build'
IMAGES = {  # name -> capture directory (eeMemory.bin, scratchpad.bin)
    'hub': CAP / 'startup-reference' / 'status-hub',      # the status hub open (t[1] = 1)
    'r01': CAP / 's87' / 'route' / '01_battery',           # after the battery take
    'r03': CAP / 's87' / 'route' / '03_panel_power',
    'r14': CAP / 's87' / 'route' / '14_roger_encounter',
}
BEATS = list(IMAGES)

# ---- this lane's routines (original bytes) ----------------------------------
LANE = {
    0x211240: 0xC8, 0x211310: 0xEC, 0x2117D0: 0x1A0, 0x213A00: 0x244, 0x213C50: 0x68, 0x213CC0: 0x268,
    0x1FCF30: 0x2C,
    0x214570: 0x478, 0x215870: 0x768, 0x2160B0: 0xFD8, 0x215FE0: 0xD0, 0x20BBE0: 0x68, 0x20BC50: 0x2A0,
    0x211970: 0x828, 0x2121A0: 0x404, 0x2125B0: 0x5A4, 0x212B60: 0x3C8, 0x212F30: 0x280, 0x20BF20: 0xD8C,
    0x218D90: 0x7B8, 0x217090: 0x71C, 0x218640: 0x744, 0x2177B0: 0x7F0, 0x217FA0: 0x69C,
}
A01 = dict(U.SIZES)                # the AREA01 lane's routines (reused)
FAMILY = {a: 'lane' for a in LANE}
FAMILY.update({a: 'a01' for a in A01})
TRPC = frozenset(pc for a, n in list(LANE.items()) + list(A01.items()) for pc in range(a, a + n, 4))
LANE_PC = frozenset(pc for a, n in LANE.items() for pc in range(a, a + n, 4))

# callee -> (integer argument registers, float argument registers, result used)
WSPEC = dict(U.WSPEC)
WSPEC.update({
    0x207D90: (5, 0, None), 0x001FCF60: (3, 0, None), 0x1FE070: (4, 0, 'v0'), 0x001FCF30: (3, 0, 'v0'),
    0x1029C0: (1, 0, None), 0x2131B0: (2, 0, None), 0x2134C0: (2, 0, None), 0x213F30: (5, 0, None),
    0x20AE40: (3, 0, None), 0x20B210: (4, 0, 'v0'), 0x20B0D0: (2, 0, None), 0x185420: (1, 0, 'v0'),
    0x182B30: (1, 0, 'v0'), 0x20CCB0: (1, 0, None), 0x1FCF10: (0, 0, None), 0x1C47E0: (2, 0, None),
    0x15C750: (4, 0, None), 0x15C700: (1, 0, None), 0x20BBE0: (2, 0, None), 0x20BC50: (4, 0, 'v0'),
    0x215FE0: (1, 0, None), 0x20E020: (0, 0, None), 0x20D930: (2, 0, None), 0x20AC70: (3, 0, None),
    0x1C5FB0: (3, 0, 'v0'), 0x123168: (2, 0, None), 0x1CBA50: (7, 0, None), 0x1FF080: (2, 0, None),
    0x1FB9F0: (4, 0, None),
})
for _a in A01:                     # AREA01 routines as callees of this lane
    WSPEC.setdefault(_a, (1, 0, None))

# native entry -> (argument ctypes, has a v0 result, symbol prefix)
P, U8, U32, I32, U64 = C.POINTER, C.c_uint8, C.c_uint32, C.c_int32, C.c_uint64
ENTRIES = {
    0x211240: ([I32], False, 'em_status_pages'), 0x211310: ([U32], False, 'em_status_pages'),
    0x2117D0: ([U64, U32, I32, I32], False, 'em_status_pages'),
    0x213A00: ([U32, I32], True, 'em_status_pages'), 0x213C50: ([U32, I32], False, 'em_status_pages'),
    0x213CC0: ([U32], True, 'em_status_pages'), 0x1FCF30: ([U64, U64, U64], True, 'em_status_pages'),
    0x214570: ([U32], False, 'em_status_pages'), 0x215870: ([U32], False, 'em_status_pages'),
    0x2160B0: ([U32, U32], False, 'em_status_pages'), 0x215FE0: ([U32], False, 'em_status_pages'),
    0x20BBE0: ([U32, I32], False, 'em_status_pages'),
    0x20BC50: ([U32, U32, U64, I32], True, 'em_status_pages'),
    0x211970: ([U32], False, 'em_status_pages'), 0x2121A0: ([I32], False, 'em_status_pages'),
    0x2125B0: ([U32, I32], False, 'em_status_pages'), 0x212B60: ([U32], False, 'em_status_pages'),
    0x212F30: ([I32, U64], False, 'em_status_pages'), 0x20BF20: ([U32, I32, I32], False, 'em_status_pages'),
    0x218D90: ([U32], False, 'em_status_pages'), 0x217090: ([U32], False, 'em_status_pages'),
    0x218640: ([U32], False, 'em_status_pages'), 0x2177B0: ([U32], False, 'em_status_pages'),
    0x217FA0: ([U32], False, 'em_status_pages'),
}
for _a, (_args, _res) in U.ENTRIES.items():
    ENTRIES[_a] = (_args, _res, 'em_area01_ui')
SOURCES = ['src/game/em_status_pages_helpers.c', 'src/game/em_status_pages_item.c',
           'src/game/em_status_pages_spr4.c', 'src/game/em_status_pages_parts.c', 'src/game/em_area01_ui_pages.c', 'src/game/em_area01_ui_effect.c']
ELF = NATIVE = None
POOL = None


CFLAGS = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-ffp-contract=off', '-shared', '-fPIC',
          '-Isrc']


def build_native(sources=None, tag='status_pages'):
    """Compile the translations into a private library. The library is
    reused only when its recorded digest (compiler flags, every source and
    every header under src/) equals the current one, so a run always tests
    the sources as they are."""
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / (tag + ('.dylib' if sys.platform == 'darwin' else '.so'))
    sources = sources or SOURCES
    h = hashlib.sha256(repr(CFLAGS).encode())
    for q in list(sources) + sorted(str(x) for x in (ROOT / 'src').rglob('*.h')):
        h.update(str(q).encode())
        h.update((ROOT / q).read_bytes())
    stamp = lib.with_suffix(lib.suffix + '.sha256')
    if not (lib.exists() and stamp.exists() and stamp.read_text() == h.hexdigest()):
        subprocess.run(['cc'] + CFLAGS + list(sources) + ['-o', str(lib)], cwd=ROOT, check=True)
        stamp.write_text(h.hexdigest())
    native = C.CDLL(str(lib))
    for address, (args, result, prefix) in ENTRIES.items():
        fn = getattr(native, '%s_%08X' % (prefix, address))
        fn.argtypes = [P(U.UiState)] + args + ([P(U32)] if result else [])
        fn.restype = C.c_int
    return native


# Callees that reach hardware registers outside the interpreter's model
# (0015C750, the player's item-use hand-off, touches the DMA controller at
# 0x10009000): scripted boundaries on both sides, their entry (arguments,
# RAM and scratchpad) checked and logged like every other call, not run.
STUBS = {0x15C750}
# Quick mode only: the moving background 0020A7A0 and the stick trail
# 0020AC70 (verified on their own by test_status_background_reference /
# test_status_hub_ui_reference; neither result is used and no page reads
# what they write) are boundaries too, entry-checked but not run, so the
# 300-case outcome cover fits the default run. EM_TEST_FULL=1 runs them as
# original code on both sides.
if not RM.FULL and os.environ.get('EM_SP_RUNALL', '') in ('', '0'):
    STUBS |= {0x20A7A0, 0x20AC70}


def stub_hook(e, address):
    spec = U.WSPEC[address]
    entry = U.entry_of(e, address, spec)
    e.entries.append((len(e.journal), entry))
    e.calls.append(entry + (U.result_of(spec, 0, 0),))
    e.exits.append((len(e.journal), 0, 0))
    e.r[2], e.f[0] = 0, 0


BASE_ORACLE_HOOKS = U.oracle_hooks


def oracle_hooks(ee):
    """The AREA01 harness's hooks with this lane's scripted boundaries."""
    BASE_ORACLE_HOOKS(ee)
    for a in STUBS:
        if a in ee.hooks:
            ee.hooks[a] = lambda e, a=a: stub_hook(e, a)


class Native(U.Native):
    def dispatch(self, _c, target, sp, a, na, f, nf, v0, f0):
        if target not in STUBS:
            return super().dispatch(_c, target, sp, a, na, f, nf, v0, f0)
        spec = U.WSPEC.get(target, (na, nf, None))
        entry = (target, sp, tuple(a[i] for i in range(na)), tuple(f[i] for i in range(nf)))
        if not self.entry_ok(entry):
            return -1
        if self.fail_at is not None and len(self.log) == self.fail_at:
            self.log.append(entry + ('fail',))
            return -1
        self.log.append(entry + (U.result_of(spec, 0, 0),))
        v0[0] = 0
        f0[0] = 0
        return 0

    def call(self, address, args):
        types, result, prefix = ENTRIES[address]
        fn = getattr(U.NATIVE, '%s_%08X' % (prefix, address))
        out = U32(0)
        rc = fn(C.byref(self.state), *args, *([C.byref(out)] if result else []))
        return rc, (out.value if result else None)


# Native arguments the original receives in a register other than the
# next argument register: 002160B0's second native argument is the
# caller's s0 (r16), which its state 4 reads.
EXTRA_REG = {0x2160B0: {1: 16}}


def oracle_regs(address, args):
    """(argument registers a0.., {other register: value}) of an entry."""
    if address == 0x213F30:
        p, lo, hi = args
        return [p, 0, 0, lo, hi], {}
    move = EXTRA_REG.get(address, {})
    regs = [a for i, a in enumerate(args) if i not in move]
    return regs, {r: args[i] for i, r in move.items()}


def hooks_for(steps):
    """The callees hooked (logged) for a case: every routine outside the
    family of the case's top-level routines (a lane calls its own routines
    directly, so they run inside the caller on both sides)."""
    tops = {s[1] for s in steps if s[0] == 'call'}
    fams = {FAMILY[a] for a in tops}
    assert len(fams) == 1, ('one family per case', fams)
    fam = fams.pop()
    return {a: spec for a, spec in WSPEC.items() if FAMILY.get(a) != fam}


def run_both(where, spad, steps, check=None, stand_in=False):
    """The AREA01 UI harness's run_both (tools/test_area01_ui_reference.py),
    with the hooks chosen per case (hooks_for) and registers other than
    a0.. set on the oracle side (EXTRA_REG)."""
    U.WSPEC = hooks_for(steps)
    ee = U.oracle_ee(spad)
    U.oracle_hooks(ee)
    unmeasured = False
    results_o = []
    try:
        for step in steps:
            if step[0] == 'poke':
                ee.save(step[1], step[2], step[3])
                continue
            _, address, args = step
            regs, extra = oracle_regs(address, args)
            for i, value in enumerate(regs):
                ee.r[4 + i] = (U.sx32(value) if value <= MASK else value) & MASK64
            for r, value in extra.items():
                ee.r[r] = (U.sx32(value) if value <= MASK else value) & MASK64
            ee.r[29], ee.r[31] = U.STACK_TOP, U.RETURN
            ee.run(address)
            results_o.append(ee.r[2] & MASK)
    except R.Unmeasured:
        unmeasured = True
    except R.NotCompletable as ex:
        if stand_in:
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
    assert not unmeasured, (where, 'the original refused but the native did not fault')
    for step, got, want in zip([x for x in steps if x[0] == 'call'], results_n, results_o):
        if ENTRIES[step[1]][1]:
            assert got == want, (where, hex(step[1]), 'result', hex(got), hex(want))
    assert native.log == ee.calls, (where, 'callee calls', R.diff_logs(native.log, ee.calls))
    assert native.checked == len(ee.entries), (where, 'entry checks', native.checked, len(ee.entries))
    assert native.ee.spad == ee.spad, (where, 'scratchpad differs (address, native, original)',
                                       R.first_differences(bytes(native.ee.spad), bytes(ee.spad), 0x70000000))
    bad = native.end_differences(ee)
    assert not bad, (where, 'RAM differs (address, native, original)', bad)
    return ee.outcomes, len(ee.calls), native.checked


def install():
    """Point the AREA01 UI harness at this lane (module globals it reads at
    call time)."""
    U.TRPC = TRPC
    U.ENTRIES = {a: (e[0], e[1]) for a, e in ENTRIES.items()}
    U.Native = Native
    U.oracle_hooks = oracle_hooks
    U.oracle_regs = lambda address, args: oracle_regs(address, args)[0]
    U.ELF = R.ELF = ELF
    U.NATIVE = NATIVE
    U.REPLAY = set() if RM.FULL else set(HEAVY)
    U.MEMO_ON = set() if RM.FULL else set(HEAVY)
    U.memo_run = memo_run


# Quick mode only (EM_TEST_FULL=1 runs everything on both sides): these
# callees dominate the page cases. The oracle memoises them and the native
# side replays the oracle's journaled stores after the entry check (the
# AREA01 UI lane's scheme). The memo key here is the full entry (every
# integer argument register a0..t3, f12..f15 and sp), not only a0, and a
# recorded run is replayed only when every value it read is unchanged.
HEAVY = {0x20A7A0, 0x20AC70, 0x213A00, 0x1FCF30, 0x1FCF60, 0x1FE070, 0x20B210, 0x20AE40, 0x1CBA50, 0x20B0D0,
         0x001C5FB0, 0x1FCF10}


def memo_run(e, address):
    key = (address, e.r[29] & MASK, tuple(e.r[4 + i] & MASK64 for i in range(8)),
           tuple(e.f[12 + i] & MASK for i in range(4)))
    for reads, writes, v0, f0 in U.MEMO.get(key, ()):
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
        v0, f0 = U.SAR.nested_bits(e, address)
    finally:
        e.load, e.save, e.write = load0, save0, write0
    U.MEMO.setdefault(key, []).append((tuple(reads.items()), list(e.journal[j0:]), v0, f0))
    return v0, f0


def load_images():
    for name, d in IMAGES.items():
        if name not in R.IMAGES:
            R.IMAGES[name] = ((d / 'eeMemory.bin').read_bytes(), (d / 'scratchpad.bin').read_bytes())
    ref = R.IMAGES[BEATS[0]][0]
    for name in BEATS:
        img = R.IMAGES[name][0]
        R.DIFF[name] = frozenset(p for p in range(R.RAM_SIZE // R.PAGE)
                                 if img[p * R.PAGE:(p + 1) * R.PAGE] != ref[p * R.PAGE:(p + 1) * R.PAGE])


def check_code(base):
    for name in BEATS:
        ram = R.IMAGES[name][0]
        for start in sorted(set(LANE) | set(A01) | set(WSPEC)):
            size = LANE.get(start, A01.get(start, 0x40))
            assert ram[start:start + size] == bytes(base.mem[start:start + size]), (name, hex(start))


def all_branches():
    probe = EE(ELF)
    out = set()
    for pc in sorted(LANE_PC):
        word = probe.load(pc)
        op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
        if op in (1, 4, 5, 6, 7, 20, 21, 22, 23) or (op == 17 and rs == 8):
            if not (op == 4 and rs == 0 and rt == 0):
                out.add(pc)
    return out


def callee_set():
    """Direct call targets of this lane's routines outside the lane."""
    probe = EE(ELF)
    targets = set()
    for pc in sorted(LANE_PC):
        word = probe.load(pc)
        if word >> 26 in (2, 3):
            target = (word & 0x3FFFFFF) << 2
            if target not in LANE:
                targets.add(target)
    return targets


# ======================================================================
# Case helpers
# ======================================================================

put = U.put
w32 = R.w32


def case(beat):
    return R.case_ram(beat), bytearray(R.IMAGES[beat][1])


def sput(spad, at, value):
    struct.pack_into('<I', spad, at - 0x70000000, value & MASK)


def pads(ram, e70=0, e74=0, e78=0):
    put(ram, 0x810E70, e70, 2)
    put(ram, 0x810E74, e74, 2)
    put(ram, 0x810E78, e78, 2)


def apply(ram, spad, knobs):
    """Common knobs: 'b' [(address, value, size)], 'spad' {address: word},
    'pads' (e70, e74, e78), 't' {offset: (value, size)}."""
    if 'pads' in knobs:
        pads(ram, *knobs['pads'])
    for at, v, size in knobs.get('b', ()):
        put(ram, at, v, size)
    for off, (v, size) in knobs.get('t', {}).items():
        put(ram, T + off, v, size)
    for at, v in knobs.get('spad', {}).items():
        sput(spad, at, v)
    if 'stick' in knobs:                          # the analog stick bytes 001B62C0 reads
        put(ram, 0x810E64, knobs['stick'][0], 1)
        put(ram, 0x810E65, knobs['stick'][1], 1)


# ---- MAP / DATABASE helpers ----------------------------------------------

def helper_case(item):
    kind, beat, knobs = item
    ram, spad = case(beat)
    apply(ram, spad, knobs)
    a = int(kind, 16)
    if a == 0x211240:
        steps = [('call', a, (knobs['k'],))]
    elif a == 0x211310:
        for i, v in enumerate(knobs['vec']):
            sput(spad, 0x700038A0 + 4 * i, v)
        steps = [('call', a, (0x700038A0,))]
    elif a == 0x2117D0:
        for i, v in enumerate(knobs['vec']):
            sput(spad, 0x700038A0 + 4 * i, v)
        steps = [('call', a, (knobs.get('a0', T), 0x700038A0, knobs['map'], knobs['floor']))]
    elif a in (0x213A00, 0x213C50):
        steps = [('call', a, (T, knobs['a1']))]
    elif a == 0x213CC0:
        steps = [('call', a, (T,))]
    elif a == 0x1FCF30:
        # 'bank': {offset: word} written into the help bank header at the
        # word D_0028A49C points to (crafted: +0x10 / +0x14 are 0 in every image)
        p = w32(ram, 0x28A49C)
        for off, v in knobs.get('bank', {}).items():
            put(ram, p + off, v)
        steps = [('call', a, knobs['args'])]
    else:
        raise AssertionError(kind)
    return run_both(('helper', kind, beat, knobs), bytes(spad), steps)


def ring(n, seed):
    """A ring of record ids. The same ids for every case (seed unused): the
    list code's branches do not depend on the ids, and repeated help-line
    calls (001FCF60 with the same id) then replay from the quick-mode memo."""
    del seed
    return [(0x48 + 5 * i) % 0x6D for i in range(n)]


def ring_bytes(ids, n18, n19=0, n17=None, n1a=None, h1c=None):
    out = [(T + 0x50 + i, v, 1) for i, v in enumerate(ids)] + [(T + 0x18, n18, 1), (T + 0x19, n19, 1)]
    if n17 is not None:
        out.append((T + 0x17, n17, 1))
    if n1a is not None:
        out.append((T + 0x1A, n1a, 1))
    if h1c is not None:
        out.append((T + 0x1C, h1c & 0xFFFF, 2))
    return out


def helper_items():
    out = []
    for beat in BEATS:
        for k in (0, 1, 2, 3, 4, 5, -1):
            out.append(('00211240', beat, {'k': k}))
        for vec in ((F(12.5), 0, F(-40.25), F(1.0)), (0, 0, 0, 0), (F(1e9), 0, F(-1e9), 0),
                    (0x7FC00000, 0, F(3.0), 0), (F(-163.9), F(2.0), F(255.9), 0)):
            out.append(('00211310', beat, {'vec': vec}))
        for m, fl in ((0, 0), (3, 1), (10, 2), (5, 0)):
            for zoom, pan in ((F(1.0), (0, 0)), (F(4.5), (F(10.0), F(-20.0))), (F(2.0), (0x80000000, F(7.0)))):
                out.append(('002117D0', beat, {'map': m, 'floor': fl, 'vec': (F(40.0), F(3.0), F(-12.0), F(1.0)),
                                              'b': [(0x810154, zoom, 4), (0x810158, pan[0], 4),
                                                    (0x81015C, pan[1], 4)]}))
        out.append(('002117D0', beat, {'map': 1, 'floor': 0, 'a0': 0xFFFFFFFF80000000,
                                      'vec': (0x7F800000, 0, F(-0.0), 0)}))
        for n18 in (0, 1, 9, 20):
            for n19 in (0, 3, n18 - 1 if n18 else 0):
                for a1 in (0, 1, 2):
                    out.append(('00213C50', beat, {'a1': a1, 'b': ring_bytes(ring(20, n18 * 31 + n19), n18, n19)}))
        for n18 in (0, 9, 20):
            for a1 in (0, 0x400):
                for e78 in (0, 0x1000, 0x4000, 0x5000):
                    for n17 in (0, 3, 7, 9):
                        out.append(('00213A00', beat, {'a1': a1, 'pads': (0, 0, e78),
                                                       'b': ring_bytes(ring(20, n17 + e78), n18, n18 // 2, n17)}))
        for n1a in (0, 1, 2):
            for h1c in (-13, -12, -11, -10, 0, 5, 10, 11, 12):
                for n18, n19 in ((9, 0), (9, 8), (20, 7), (0, 0)):
                    out.append(('00213CC0', beat, {'b': ring_bytes(ring(20, h1c), n18, n19, 5, n1a, h1c)}))
        for args in ((0, 0x64, 0x2F), (4, 0x64, 0x2F), (0xFFFFFFFFFFFFFFFF, 0x64, 0x2F), (2, 0, 0x100000047)):
            out.append(('001FCF30', beat, {'args': args}))
    return out


# ---- ITEM children (EQUIPMENT / EVENT / HEALING) and list helpers ------

PLAYER = 0x8102B0
DEVLIST = FREE                     # crafted interaction list (D_00275B5C)
DEVICE = FREE + 0x100              # one crafted device record


def device(ram, ev, shape=5, near=True):
    """A one-entry interaction list D_00275B5C / D_00275B64 whose device
    00184D20 accepts for item `ev` (shape 5: within 14 units, 4 high) or
    refuses (near=False puts it 100 units away)."""
    kind = {0x20: (4, 0x36), 0x25: (4, 0x21), 0x26: (4, 0x49), 0x27: (4, 0x35), 0x23: (6, 0x2F),
            0x24: (6, 0x13)}.get(ev, (4, 0x36))
    put(ram, 0x275B5C, DEVLIST)
    put(ram, 0x275B64, 1, 2)
    put(ram, DEVLIST, DEVICE)
    put(ram, DEVICE + 0, 1, 1)
    put(ram, DEVICE + 2, 0x80 | kind[0], 1)
    put(ram, DEVICE + 3, kind[1], 1)
    put(ram, DEVICE + 8, shape, 1)
    put(ram, DEVICE + 0xB, 0, 1)
    for i in range(3):
        v = w32(ram, PLAYER + 0xA0 + 4 * i)
        if i == 0 and not near:
            v = F(struct.unpack('<f', struct.pack('<I', v))[0] + 100.0)
        put(ram, DEVICE + 0xB0 + 4 * i, v)


def item_case(item):
    kind, beat, knobs = item
    ram, spad = case(beat)
    put(ram, T + 1, 3, 1)
    put(ram, T + 2, 2, 1)
    apply(ram, spad, knobs)
    if 'ring' in knobs:
        ids, n18 = knobs['ring']
        for i, v in enumerate(ids):
            put(ram, T + 0x50 + i, v, 1)
        put(ram, T + 0x18, n18, 1)
    if 'dev' in knobs:
        device(ram, *knobs['dev'])
    a = int(kind, 16)
    if a == 0x2160B0:
        steps = [('call', a, (T, knobs.get('s0', T)))]
    elif a == 0x20BBE0:
        steps = [('call', a, (T, knobs['n']))]
    elif a == 0x20BC50:
        steps = [('call', a, (T, knobs.get('rows', 0x266060), knobs.get('tex', 0x20042D05A1321F80), knobs.get('fl', 8)))]
    else:
        steps = [('call', a, (T,))]
    for _ in range(knobs.get('frames', 1) - 1):
        steps.append(steps[0])
    return run_both(('item', kind, beat, knobs), bytes(spad), steps)


def item_extra():
    """Cases the outcome cover needed: EQUIPMENT's C7B-only list, list
    scrolls (0020B210 wrap events) on the three pages, EVENT's busy player
    (D_008106F1 makes 00182B30 refuse), and HEALING's commit with a full
    list (00215FE0 and the cursor clamp)."""
    out, beat = [], 'hub'
    out.append(('00214570', beat, {'b': flags(0x810C7B, 0x01, 4) + [(0x8106B0, 0, 1)], 't': {5: (0, 1)}}))
    for kind in ('00214570', '00215870', '002160B0'):
        for e78, n17 in ((0x4000, 3), (0x1000, 0)):
            out.append((kind, beat, {'t': {5: (1, 1), 0x17: (n17, 1), 0x19: (0, 1), 0x1A: (0, 1), 6: (0, 1)},
                                     'pads': (0, 0, e78), 'ring': ([0, 1, 2, 3, 4, 0, 1], 7)}))
    out.append(('00215870', beat, {'pads': (0, 0x40, 0), 'ring': ([2], 1), 'dev': (0x25,),
                                   't': {5: (1, 1), 0x17: (0, 1), 0x19: (0, 1)}, 'b': [(0x8106F1, 1, 1)]}))
    for n17 in (0, 2, 4):
        for bits in (0x1F, 0x0F, 0x07):
            out.append(('002160B0', beat, {'pads': (0, 0x40, 0), 'ring': ([0, 1, 2, 3, 4], 5),
                                           'b': flags(0x810C82, bits) + [(0x810858, F(20.0), 4)],
                                           't': {5: (4, 1), 6: (0, 1), 0x17: (n17, 1), 0x19: (0, 1)}}))
    return out


def flags(base, bits, n=5):
    return [(base + k, 1 if bits >> k & 1 else 0, 1) for k in range(n)]


F100, F60, F30 = F(100.0), F(60.0), F(30.0)


def item_items():
    out = []
    for beat in ('hub', 'r03'):
        # ---- HEALING 002160B0
        H = '002160B0'
        for bits in (0, 0x1F, 0x05, 0x10, 0x04):
            out.append((H, beat, {'b': flags(0x810C82, bits) + [(0x8106B0, 0, 1)], 't': {5: (0, 1)}}))
            for b1 in (0x1E, 0x1F, 0x20, 0x22, 0x21):
                out.append((H, beat, {'b': flags(0x810C82, bits) + [(0x8106B0, 1, 1), (0x8106B1, b1, 1)],
                                      't': {5: (0, 1)}}))
        for inf, hp in ((0, F100), (0, F(99.0)), (F(5.0), F100), (0, F(120.0))):
            for bits in (0x1F, 0x04, 0x03):
                out.append((H, beat, {'b': flags(0x810C82, bits) + [(0x8106B0, 4, 1), (0x8106D0, DEVICE, 4),
                                      (0x81085C, inf, 4), (0x810858, hp, 4)], 't': {5: (0, 1)}}))
        for e74 in (0, 0x20, 0x40, 0x60):
            for e78 in (0, 0x1000, 0x4000):
                for n17, n19 in ((0, 0), (3, 0), (0, 4)):
                    out.append((H, beat, {'pads': (0, e74, e78), 'ring': ([0, 1, 2, 3, 4, 0, 1], 5),
                                          't': {5: (1, 1), 0x17: (n17, 1), 0x19: (n19, 1)}}))
        # Cross on each kind with the gates
        for k in range(6):
            for mode in (0, 1):
                for inf, hp in ((0, F(59.0)), (0, F60), (F(10.0), F(99.0)), (F(10.0), F100), (0, F100)):
                    kn = {'pads': (0, 0x40, 0), 'ring': ([k] * 4, 4), 't': {5: (1, 1), 0x17: (0, 1), 0x19: (0, 1)},
                          'b': [(0x810707, mode, 1), (0x81085C, inf, 4), (0x810858, hp, 4)]}
                    if k == 2:
                        out.append((H, beat, dict(kn, dev=(0x20,))))
                        out.append((H, beat, dict(kn, dev=(0x20, 5, False))))
                    else:
                        out.append((H, beat, kn))
        out.append((H, beat, {'pads': (0, 0x40, 0), 'ring': ([], 0), 't': {5: (1, 1)}}))
        # state 2 / 6 / 3
        for n1a in (1, 2):
            for h1c in (0, 0x2C, 0x30, -0x2C, -0x30):
                out.append((H, beat, {'ring': ([0, 1, 2, 3, 4], 5), 't': {5: (2, 1), 0x1A: (n1a, 1),
                                      0x1C: (h1c & 0xFFFF, 2), 0x19: (0, 1)}}))
        for st in (6, 3):
            for e74 in (0, 0x20, 0x40, 0x1000):
                for c in (1, 2, 0):
                    out.append((H, beat, {'pads': (0, e74, 0), 'ring': ([0, 1, 2], 3), 't': {5: (st, 1), 6: (c, 1)}}))
        # state 4: prompt line by the s0 byte, cursor, confirm per kind
        for s0, byte in ((T, None), (T, 2), (0x20, 2), (0x20, 0)):
            kn = {'ring': ([2, 0, 1], 3), 't': {5: (4, 1), 6: (1, 1)}}
            if byte is not None:
                kn['b'] = [((s0 + T + 0x50) & 0x1FFFFFF, byte, 1)]
            out.append((H, beat, dict(kn, s0=s0)))
        for e74, c in ((0x8000, 1), (0x8000, 0), (0x2000, 0), (0x2000, 1), (0x40, 1), (0x20, 0), (0x60, 0), (0, 0)):
            out.append((H, beat, {'pads': (0, e74, 0), 'ring': ([0, 1], 2), 't': {5: (4, 1), 6: (c, 1)}}))
        for k in range(6):
            for mode in (0, 1):
                for inf, hp in ((F(40.0), F(20.0)), (F(10.0), F(45.0)), (0, F(80.0)), (F(40.0), F(95.0))):
                    kn = {'pads': (0, 0x40, 0), 'ring': ([k, 0, 1, 2], 4 if k != 5 else 3),
                          't': {5: (4, 1), 6: (0, 1), 0x17: (0, 1), 0x19: (0, 1), 0x30: (DEVICE, 4)},
                          'b': [(0x810707, mode, 1), (0x81085C, inf, 4), (0x810858, hp, 4)]}
                    out.append((H, beat, kn))
        for n18, n17 in ((1, 0), (3, 2), (3, 0), (4, 3)):
            out.append((H, beat, {'pads': (0, 0x40, 0), 'ring': ([0] * 5, n18), 'b': flags(0x810C82, 0x01),
                                  't': {5: (4, 1), 6: (0, 1), 0x17: (n17, 1), 0x19: (0, 1)}}))
        # state 5: the count-up
        for tgt, hp in ((F(50.0), F(40.0)), (F(40.0), F(40.0)), (F(41.0), F(40.0))):
            for c3c in (1, 2):
                for b64 in (0, 3, 10, -10):
                    for e74 in (0, 0x800):
                        out.append((H, beat, {'pads': (0, e74, 0), 'spad': {0x70003B64: b64},
                                              't': {5: (5, 1), 0x34: (tgt, 4), 0x3C: (c3c, 2), 6: (0, 1)},
                                              'b': [(0x810858, hp, 4)]}))
        out.append((H, beat, {'t': {5: (7, 1)}}))
        # ---- EQUIPMENT 00214570
        Q = '00214570'
        for bits in (0, 0x0F, 0x03, 0x02, 0x0C):
            out.append((Q, beat, {'b': flags(0x810C7B, bits, 4) + [(0x8106B0, 0, 1)], 't': {5: (0, 1)}}))
            for b1, c60 in ((0x17, 1), (0x19, 1), (0x19, 2), (0x18, 1), (0x1A, 1), (0x30, 1)):
                out.append((Q, beat, {'b': flags(0x810C7B, bits, 4) + [(0x8106B0, 1, 1), (0x8106B1, b1, 1),
                                      (0x810C60, c60, 1)], 't': {5: (0, 1)}}))
        for e74 in (0, 0x20, 0x40):
            for c in (0, 1, 5):
                for e78 in (0, 0x1000, 0x4000):
                    out.append((Q, beat, {'pads': (0, e74, e78), 'ring': ([0, 1, 2, 3], 4),
                                          't': {5: (1, 1), 6: (c, 1), 0x17: (0 if e78 != 0x4000 else 3, 1)}}))
        for n1a in (1, 2):
            for h1c in (0, 0x2C, -0x2C, 0x30, -0x30):
                out.append((Q, beat, {'ring': ([0, 1, 2, 3], 4), 't': {5: (2, 1), 0x1A: (n1a, 1), 0x1C: (h1c & 0xFFFF, 2)}}))
        for e74 in (0, 0x1000, 0x20):
            for c in (1, 2):
                out.append((Q, beat, {'pads': (0, e74, 0), 'ring': ([0, 1], 2), 't': {5: (3, 1), 6: (c, 1)}}))
        out.append((Q, beat, {'t': {5: (4, 1)}}))
        # ---- EVENT 00215870
        V = '00215870'
        for c87, c88 in ((0, 0), (1, 0), (0, 1), (1, 1)):
            for ev in (0, 0x7FFFFF, 0x1001, 0x400000):
                out.append((V, beat, {'b': [(0x810C87, c87, 1), (0x810C88, c88, 1), (0x8106B0, 0, 1)] +
                                      flags(0x810C89, ev, 23), 't': {5: (0, 1)}}))
        for b1 in (0x23, 0x24, 0x32, 0x3B, 0x3C):
            out.append((V, beat, {'b': [(0x810C87, 1, 1), (0x8106B0, 3, 1), (0x8106B1, b1, 1)] +
                                  flags(0x810C89, 0x7FFFFF, 23), 't': {5: (0, 1)}}))
        for e74 in (0, 0x20, 0x40):
            for e78 in (0, 0x1000, 0x4000):
                out.append((V, beat, {'pads': (0, e74, e78), 'ring': ([0, 2, 3, 0xF], 4), 't': {5: (1, 1), 0x17: (0, 1)}}))
        for ev_id, dev in ((2, (0x25,)), (3, (0x26,)), (4, (0x27,)), (0, (0x23,)), (1, (0x24,)), (0xF, (0x32,)),
                           (2, (0x25, 5, False)), (2, None)):
            kn = {'pads': (0, 0x40, 0), 'ring': ([ev_id], 1), 't': {5: (1, 1), 0x17: (0, 1), 0x19: (0, 1)}}
            if dev is not None:
                kn['dev'] = dev
            out.append((V, beat, kn))
        out.append((V, beat, {'pads': (0, 0x40, 0), 'ring': ([], 0), 't': {5: (1, 1)}}))
        for n1a in (1, 2):
            for h1c in (0, 0x2C, -0x30):
                out.append((V, beat, {'ring': ([0, 2, 3], 3), 't': {5: (2, 1), 0x1A: (n1a, 1), 0x1C: (h1c & 0xFFFF, 2)}}))
        for st in (5, 3):
            for e74 in (0, 0x20, 0x4000):
                for c in (1, 2):
                    out.append((V, beat, {'pads': (0, e74, 0), 'ring': ([0], 1), 't': {5: (st, 1), 6: (c, 1)}}))
        for e74, c in ((0x8000, 1), (0x8000, 0), (0x2000, 0), (0x2000, 1), (0x40, 1), (0x40, 0), (0x20, 0), (0, 0)):
            out.append((V, beat, {'pads': (0, e74, 0), 'ring': ([2], 1),
                                  't': {5: (4, 1), 6: (c, 1), 0x30: (DEVICE, 4)}}))
        out.append((V, beat, {'t': {5: (6, 1)}}))
        if beat == 'hub':
            out += item_extra()
        # ---- the list helpers directly
        for n in (0, 1, 2):
            for n18, n19 in ((0, 0), (1, 0), (5, 0), (5, 4), (9, 0), (9, 7)):
                out.append(('0020BBE0', beat, {'n': n, 'ring': (list(range(9)), n18), 't': {0x19: (n19, 1)}}))
        for n1a in (0, 1, 2):
            for h1c in (-0x31, -0x30, -0x2F, 0, 0x2F, 0x30, 0x31):
                for n18, n19 in ((5, 0), (5, 4), (1, 0)):
                    out.append(('0020BC50', beat, {'ring': ([0, 1, 2, 3, 4], n18), 'rows': 0x266060,
                                                   't': {0x1A: (n1a, 1), 0x1C: (h1c & 0xFFFF, 2), 0x19: (n19, 1),
                                                         0x17: (n19 % 4, 1),
                                                         0x90: (0x03020100, 4), 0x94: (4, 1)}}))
        out.append(('0020BC50', beat, {'rows': 0x265BF0, 'tex': 0xFFFFFFFF80000001, 'fl': -1,
                                       't': {0x1A: (1, 1), 0x90: (0x00010203, 4), 0x94: (0, 1)}}))
    return out


# ---- SPR4 page, its helpers and the five part pages ----------------------

PARTS = {0x218D90: 4, 0x217090: 5, 0x218640: 6, 0x2177B0: 7, 0x217FA0: 8}


def spr4_case(item):
    kind, beat, knobs = item
    ram, spad = case(beat)
    put(ram, T + 1, 3, 1)
    put(ram, T + 2, 2, 1)
    put(ram, T + 0x10, 2, 1)
    apply(ram, spad, knobs)
    if 'ring' in knobs:
        ids, n18 = knobs['ring']
        for i, v in enumerate(ids):
            put(ram, T + 0x50 + i, v, 1)
        put(ram, T + 0x18, n18, 1)
    a = int(kind, 16)
    if a in (0x2121A0,):
        steps = [('call', a, (knobs['a0'],))]
    elif a == 0x2125B0:
        steps = [('call', a, (T, knobs['a1']))]
    elif a == 0x212F30:
        steps = [('call', a, (knobs['id'], knobs['ctx']))]
    elif a == 0x20BF20:
        steps = [('call', a, (knobs.get('tbl', 0x265980), knobs['mode'], knobs['sel']))]
    else:
        steps = [('call', a, (T,))]
    for _ in range(knobs.get('frames', 1) - 1):
        steps.append(steps[0])
    return run_both(('spr4', kind, beat, knobs), bytes(spad), steps)


def counters(pa=0, sa=0, fuel=(0, 0), grn=0):
    return [(0x810CA8, pa, 2), (0x810CAA, sa, 2), (0x810CAC, fuel[0], 2), (0x810CAE, fuel[1], 2), (0x810CB0, grn, 2)]


def spr4_items():
    out = []
    for beat in ('hub', 'r03'):
        # 00211970 states
        S = '00211970'
        for b0, b1, c63, cb4 in ((0, 0, 2, 60), (5, 0, 2, 60), (5, 0, 2, 30), (1, 0x10, 2, 60), (1, 0x11, 2, 60),
                                 (3, 0x32, 2, 60)):
            out.append((S, beat, {'t': {4: (0, 1)}, 'b': [(0x8106B0, b0, 1), (0x8106B1, b1, 1), (0x810C63, c63, 1),
                                                          (0x810CB4, cb4, 2)]}))
        for hv in range(8):
            for e74 in (0, 0x20, 0x40):
                out.append((S, beat, {'t': {4: (1, 1), 0x11: (hv, 1)}, 'pads': (0, e74, 0)}))
        for k in range(8):
            out.append((S, beat, {'t': {4: (2, 1), 0x15: (k, 1)}}))
        for t5, bd8 in ((0, 1), (1, 1), (1, 0)):
            out.append((S, beat, {'t': {4: (3, 1), 5: (t5, 1), 6: (7, 1), 0x16: (0x30, 1)}, 'b': [(0x275BD8, bd8, 1)]}))
        for st in (4, 5, 6, 7, 8):
            out.append((S, beat, {'t': {4: (st, 1), 5: (0, 1)}, 'b': flags(0x810C64, 0x1F, 16), 'pads': (0, 0x20, 0)}))
        for b1 in (0x10, 0x12):
            for e74, c in ((0, 2), (0, 1), (0x40, 5), (0x20, 5)):
                out.append((S, beat, {'t': {4: (9, 1), 6: (c, 1)}, 'pads': (0, e74, 0), 'b': [(0x8106B1, b1, 1)]}))
        for e74, c in ((0x8000, 1), (0x8000, 0), (0x2000, 0), (0x2000, 1), (0x40, 1), (0x40, 0), (0x10, 0), (0, 0)):
            out.append((S, beat, {'t': {4: (10, 1), 6: (c, 1)}, 'pads': (0, e74, 0)}))
        for h1e, h1c, e74, b64 in ((10, 60, 0, 0), (10, 60, 0, 7), (58, 60, 0, 20), (60, 60, 0, 1), (10, 60, 0x40, 3)):
            out.append((S, beat, {'t': {4: (11, 1), 0x1E: (h1e, 2), 0x1C: (h1c, 2)}, 'pads': (0, e74, 0),
                                  'spad': {0x70003B64: b64}}))
        for c3c, e74 in ((2, 0), (1, 0), (5, 0x10), (0, 0)):
            out.append((S, beat, {'t': {4: (12, 1), 0x3C: (c3c, 2)}, 'pads': (0, e74, 0), 'b': [(0x8106B1, 0x13, 1)]}))
        out.append((S, beat, {'t': {4: (13, 1)}}))
        # helpers
        for a0 in (0, 1, 2, 3):
            for b64 in (0, 0x10):
                out.append(('002121A0', beat, {'a0': a0, 'spad': {0x70003B64: b64}, 'b': [(0x810CB4, 0xFFF0, 2)]}))
        for a1 in (0, 1):
            for hv in range(8):
                out.append(('002125B0', beat, {'a1': a1, 't': {0x11: (hv, 1)}}))
        for k in (0, 1, 5, 9, 10, 0x105, 0xFF):
            out.append(('00212F30', beat, {'id': k, 'ctx': 0xFFFFFFFF80808080}))
        out.append(('00212F30', beat, {'id': 3, 'ctx': 0x40808080}))
        for eq in (0, 1, 2, 3):
            for hv in (0, 2, 3, 5, 6):
                out.append(('00212B60', beat, {'t': {0x11: (hv, 1)},
                                               'b': [(0x810CA4, eq, 1), (0x810CA5, 5, 1), (0x810CA6, 2, 1), (0x810CA7, 7, 1)]}))
        for mode in (0, 1, 2, 3):
            for sel in (0, 1, 2, 3, 4, 5, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16):
                for b64 in (0, 0x10):
                    out.append(('0020BF20', beat, {'mode': mode, 'sel': sel, 'spad': {0x70003B64: b64},
                                                   'b': counters(12, 3, (1, 5), 2) + [(0x810CA6, sel % 5, 1),
                                                                                      (0x810CA4, sel % 3, 1)]}))
        for c in (counters(), counters(0, 7), counters(-3, 0, (0, 0), 0), counters(0, 0, (2, 0)), counters(0, 0, (0, 0), 9)):
            out.append(('0020BF20', beat, {'mode': 0, 'sel': 0, 'b': c}))
        out.append(('0020BF20', beat, {'mode': -1, 'sel': -1, 'b': counters(1, 1, (1, 1), 1)}))
        # the five part pages
        for addr in PARTS:
            P = '%08X' % addr
            for bits in (0, 0x1F, 0x0B, 0x04):
                out.append((P, beat, {'t': {5: (0, 1)}, 'b': flags(0x810C64, bits, 16) + [(0x8106B0, 0, 1),
                                      (0x810CA4, 2, 1), (0x810CA6, 1, 1), (0x810C61, 1, 1)]}))
                out.append((P, beat, {'t': {5: (0, 1)}, 'b': flags(0x810C64, bits, 16) + [(0x8106B0, 1, 1),
                                      (0x8106B1, {0x218D90: 2, 0x217090: 6, 0x218640: 8, 0x2177B0: 0xC,
                                                  0x217FA0: 0xE}[addr], 1), (0x810CA4, 0xFF, 1)]}))
            for e74 in (0, 0x20, 0x40):
                for e78 in (0, 0x1000, 0x4000):
                    out.append((P, beat, {'t': {5: (1, 1), 0x17: (0 if e78 != 0x4000 else 3, 1), 7: (0, 1)},
                                          'pads': (0, e74, e78), 'ring': ([0, 1, 2, 3, 4], 5)}))
            for k in range(6):
                for c7, cur in ((0, 0), (0, 1), (3, 0)):
                    kn = {'t': {5: (1, 1), 0x17: (0, 1), 0x19: (0, 1), 7: (c7, 1), 0x12: (0xA + cur, 1)},
                          'pads': (0, 0x40, 0), 'ring': ([k, 0, 1], 3),
                          'b': [(0x810CA5, 5 + cur, 1), (0x810CA6, cur, 1), (0x810CA7, 7 + cur, 1), (0x810C61, cur, 1),
                                (0x810C70, 1, 1), (0x810C71, 1, 1), (0x810C72, cur, 1)]}
                    out.append((P, beat, kn))
            for n1a in (1, 2):
                for h1c in (0, 0x2C, -0x30):
                    out.append((P, beat, {'t': {5: (2, 1), 0x1A: (n1a, 1), 0x1C: (h1c & 0xFFFF, 2)},
                                          'ring': ([0, 1, 2, 3, 4], 5)}))
            for e74 in (0, 0x20, 0x1000):
                for c in (1, 2):
                    out.append((P, beat, {'t': {5: (3, 1), 6: (c, 1)}, 'pads': (0, e74, 0), 'ring': ([0, 1], 2)}))
            for e74, c in ((0x8000, 1), (0x8000, 0), (0x2000, 0), (0x2000, 1), (0x40, 1), (0x20, 0), (0, 0)):
                out.append((P, beat, {'t': {5: (4, 1), 6: (c, 1), 0x13: (0xC, 1)}, 'pads': (0, e74, 0),
                                      'ring': ([1, 2], 2)}))
            for eq, ca6, t13 in ((0xFF, 1, 0xB), (2, 1, 0xC), (0, 0xFF, 0xB), (1, 2, 0xD)):
                out.append((P, beat, {'t': {5: (4, 1), 6: (0, 1), 0x13: (t13, 1), 0x17: (1, 1), 0x19: (0, 1),
                                            0x1E: ({0x218D90: 0, 0x217090: 5, 0x218640: 7, 0x2177B0: 0xA,
                                                    0x217FA0: 0xD}[addr], 2)},
                                      'pads': (0, 0x40, 0), 'ring': ([1, 2, 0], 3),
                                      'b': [(0x810CA4, eq, 1), (0x810CA6, ca6, 1)]}))
            out.append((P, beat, {'t': {5: (5, 1), 7: (2, 1)}}))
    out += spr4_extra()
    return out


STICK = [(128, 128)] + [(int(round(128 + 127 * math.cos(math.radians(a)))), int(round(128 + 127 * math.sin(math.radians(a)))))
                        for a in range(0, 360, 30)]


def spr4_extra():
    """Cases the outcome cover needed: the stick (0020D930 mode 2 sets the
    hover t[0x11]), the rows' blink icons, the ownership bytes of the part
    lists, list scrolls (0020B210 wrap events), the part pages' panel
    selection in states 3 / 4, and 002177B0's Cross gates."""
    out = []
    for beat in ('hub',):
        for st in STICK:
            for e74 in (0, 0x40):
                out.append(('00211970', beat, {'t': {4: (1, 1), 0x11: (7, 1)}, 'pads': (0, e74, 0), 'stick': st}))
            out.append(('002125B0', beat, {'a1': 0, 't': {0x11: (0, 1)}, 'stick': st}))
        for hv in (4, 1):
            out.append(('00212B60', beat, {'t': {0x11: (hv, 1)}, 'b': [(0x810CA4, 1, 1), (0x810CA6, 3, 1)]}))
        for sel, ca6, ca4 in ((0x11, 1, 0), (0x12, 0, 0), (0x13, 2, 0), (0x14, 3, 0), (0x13, 1, 0), (0x15, 4, 0),
                              (0x15, 0, 0), (0x16, 0, 2), (0x16, 0, 1)):
            out.append(('0020BF20', beat, {'mode': 2, 'sel': sel, 'spad': {0x70003B64: 0x10},
                                           'b': counters(12, 3, (1, 5), 2) + [(0x810CA6, ca6, 1), (0x810CA4, ca4, 1)]}))
        for addr in PARTS:
            P = '%08X' % addr
            b1 = {0x218D90: 2, 0x217090: 6, 0x218640: 8, 0x2177B0: 0xC, 0x217FA0: 0xE}[addr]
            for bits in (0xFFFF, 0x0000):
                for cax in (0xFF, 3):
                    out.append((P, beat, {'t': {5: (0, 1)}, 'b': flags(0x810C64, bits, 16) + [
                        (0x8106B0, 1, 1), (0x8106B1, b1, 1), (0x810CA4, cax, 1), (0x810CA5, cax, 1),
                        (0x810CA6, cax, 1), (0x810CA7, cax, 1)]}))
            out.append((P, beat, {'t': {5: (0, 1)}, 'b': flags(0x810C64, 0xFFFF, 16) + [
                (0x8106B0, 1, 1), (0x8106B1, 0x30, 1)]}))
            for e78, n17 in ((0x4000, 3), (0x1000, 0)):
                out.append((P, beat, {'t': {5: (1, 1), 0x17: (n17, 1), 0x19: (0, 1), 0x1A: (0, 1)},
                                      'pads': (0, 0, e78), 'ring': ([0, 1, 2, 3, 4, 0, 1], 7)}))
            out.append((P, beat, {'t': {5: (1, 1)}, 'pads': (0, 0x40, 0), 'ring': ([], 0)}))
            for st in (3, 4):
                for k in range(6):
                    out.append((P, beat, {'t': {5: (st, 1), 6: (2, 1), 0x17: (0, 1), 0x19: (0, 1)},
                                          'ring': ([k, 1], 2)}))
        for c70, c71, c72 in ((1, 0, 1), (0, 1, 1), (1, 1, 0), (0, 0, 0)):
            for k in (2, 3, 4):
                out.append(('002177B0', beat, {'t': {5: (1, 1), 0x17: (0, 1), 0x19: (0, 1), 7: (0, 1), 0x12: (0xB, 1)},
                                               'pads': (0, 0x40, 0), 'ring': ([k], 1),
                                               'b': [(0x810C70, c70, 1), (0x810C71, c71, 1), (0x810C72, c72, 1)]}))
    return out


# ---- reuse: the AREA01 translations over the AREA11 images ---------------

def reuse_items():
    out = []
    for beat in ('hub', 'r03'):
        for it in U.DB_ITEMS:
            if it[0] == U.S1:
                out.append(('db', (beat,) + it[1:]))
        for it in U.map_items():
            if it[0] == U.S1:
                out.append(('map', (beat,) + it[1:]))
    return out


def reuse_case(item):
    kind, it = item
    U.WSPEC = hooks_for([('call', 0x214020 if kind == 'db' else 0x20F950, ())])
    beat = it[0]
    if beat == 'hub':
        # The page is entered from the hub: 0020CDC0 phase 3 sub-state 0 runs
        # 001AFEB0 / 001AFE60 (the UI pool freed) before the page's first
        # call. Run them (original code) on the case image first.
        orig = U.case
        U.case = lambda b: page_entry(*orig(b))
        try:
            return U.db_case(it) if kind == 'db' else U.map_case(it)
        finally:
            U.case = orig
    return U.db_case(it) if kind == 'db' else U.map_case(it)


def pre_original(ram, spad, calls):
    """Run original routines (no hooks) on the case image before the case:
    their stores land in `ram` (pages marked dirty) and `spad`."""
    ee = U.UIEE(ELF, b'', bytes(spad))
    ee.mem = bytearray(ram)
    ee.hooks = {}
    for address, args in calls:
        for i, value in enumerate(args):
            ee.r[4 + i] = value & MASK64
        ee.r[29], ee.r[31] = U.STACK_TOP, U.RETURN
        ee.run(address)
    for is_spad, at, data in ee.journal:
        if is_spad:
            spad[at:at + len(data)] = data
        else:
            put(ram, at, int.from_bytes(data, 'little'), len(data))
    return ram, spad


def page_entry(ram, spad):
    return pre_original(ram, spad, [(0x1AFEB0, ()), (0x1AFE60, ())])


# ======================================================================
# Faults (native only)
# ======================================================================

def fault_cases():
    """NULL call, a latched fault, no views, and a failing callee at the
    first call position of every entry of this lane that calls one."""
    n = 0
    ram, spad = case('hub')
    for address in LANE:
        types, result, prefix = ENTRIES[address]
        args = default_args(address)
        # NULL call: -1, nothing written (entries that call something)
        nat = Native(bytes(spad), oracle=None, null_call=True)
        rc, _ = nat.call(address, args)
        if address not in (0x213C50, 0x215FE0, 0x20BBE0):
            assert rc == -1 and nat.state.core.fault.code == 1, ('null call', hex(address), rc)
            assert nat.unchanged(bytes(spad)), ('null call wrote', hex(address))
        n += 1
        # latched: -1 and nothing written
        nat = Native(bytes(spad), oracle=None)
        nat.state.core.fault.code = 2
        rc, _ = nat.call(address, args)
        assert rc == -1 and nat.log == [] and nat.unchanged(bytes(spad)), ('latched', hex(address))
        n += 1
        # no views: code 4 at the first access
        nat = Native(bytes(spad), oracle=None, views=False)
        rc, _ = nat.call(address, args)
        if address == 0x212F30:     # reads no memory: runs to its end
            assert rc == 0 and nat.state.core.fault.code == 0, ('no views', hex(address))
        else:
            assert rc == -1 and nat.state.core.fault.code == 4, ('no views', hex(address), nat.state.core.fault.code)
        n += 1
        # a failing callee at call 0: code 2, and no call after it
        nat = Native(bytes(spad), oracle=None, fail_at=0)
        rc, _ = nat.call(address, args)
        if nat.log:
            assert rc == -1 and nat.state.core.fault.code == 2 and len(nat.log) == 1, ('fail', hex(address))
        n += 1
    return n


def default_args(address):
    return {0x211240: (0,), 0x211310: (0x700038A0,), 0x2117D0: (0, 0x700038A0, 0, 0), 0x213A00: (T, 0),
            0x213C50: (T, 0), 0x213CC0: (T,), 0x1FCF30: (0, 0x64, 0x2F), 0x214570: (T,), 0x215870: (T,),
            0x2160B0: (T, T), 0x215FE0: (T,), 0x20BBE0: (T, 0), 0x20BC50: (T, 0x266060, 0, 8),
            0x211970: (T,), 0x2121A0: (0,), 0x2125B0: (T, 0), 0x212B60: (T,), 0x212F30: (0, 0),
            0x20BF20: (0x265980, 0, 0), 0x218D90: (T,), 0x217090: (T,), 0x218640: (T,), 0x2177B0: (T,),
            0x217FA0: (T,)}[address]


# ======================================================================
# Groups and main
# ======================================================================

# Branch outcomes no input reaches, with the reason (docs/STATUS_PAGES.md 3).
UNREACHED = {
    # i % 4 of a list index: the index is a loop counter from 0, so the
    # sign test never fails and the negative-remainder fix-up never runs
    (0x2159BC, False), (0x2159C4, False), (0x2159C4, True),     # 00215870
    (0x2162B0, False), (0x2162B8, False), (0x2162B8, True),     # 002160B0 request 4
    (0x216338, False), (0x216340, False), (0x216340, True),     # 002160B0 take
    (0x2178E4, False), (0x2178EC, False), (0x2178EC, True),     # 002177B0
    (0x218EB0, False), (0x218EB8, False), (0x218EB8, True),     # 00218D90
    # 002160B0 kind 1: the target was just set to 100.0, so 100 < 60 and
    # 100 < 100 are never true
    (0x216CCC, True), (0x216CF0, True),
    # 00211970 state 1: 002125B0 has just run 0020D930(t, 2), which leaves
    # the hover t[0x11] in 0..6 (a stick sweep over every direction and the
    # dead zone, with t[0x11] = 7 on entry, never kept 7)
    (0x211AE4, True),
}


def pmap(fn, items):
    items = list(items)
    if POOL is None or len(items) < 2:
        return [fn(x) for x in items]
    size = max(1, -(-len(items) // (3 * JOBS)))
    return list(POOL.imap(fn, items, chunksize=size))


# The greedy outcome cover of the lane groups (`cover` command): every
# default run keeps these items, so it takes every reachable branch outcome.
QUICK_PINS = {
    ("('0020BC50', 'hub', {'ring': ([0, 1, 2, 3, 4], 5), 'rows': 2515040, 't': {26: (0, 1), 28: "
     '(65487, 2), 25: (4, 1), 23: (0, 1), 144: (50462976, 4), 148: (4, 1)}})'),
    ("('0020BC50', 'hub', {'ring': ([0, 1, 2, 3, 4], 5), 'rows': 2515040, 't': {26: (1, 1), 28: "
     '(47, 2), 25: (4, 1), 23: (0, 1), 144: (50462976, 4), 148: (4, 1)}})'),
    ("('0020BF20', 'hub', {'mode': 1, 'sel': 2, 'spad': {1879063396: 0}, 'b': [(8457384, 12, 2),"
     ' (8457386, 3, 2), (8457388, 1, 2), (8457390, 5, 2), (8457392, 2, 2), (8457382, 2, 1), (845'
     '7380, 2, 1)]})'),
    ("('0020BF20', 'hub', {'mode': 1, 'sel': 5, 'spad': {1879063396: 0}, 'b': [(8457384, 12, 2),"
     ' (8457386, 3, 2), (8457388, 1, 2), (8457390, 5, 2), (8457392, 2, 2), (8457382, 0, 1), (845'
     '7380, 2, 1)]})'),
    ("('0020BF20', 'hub', {'mode': 2, 'sel': 17, 'spad': {1879063396: 0}, 'b': [(8457384, 12, 2)"
     ', (8457386, 3, 2), (8457388, 1, 2), (8457390, 5, 2), (8457392, 2, 2), (8457382, 2, 1), (84'
     '57380, 2, 1)]})'),
    ("('0020BF20', 'hub', {'mode': 2, 'sel': 17, 'spad': {1879063396: 16}, 'b': [(8457384, 12, 2"
     '), (8457386, 3, 2), (8457388, 1, 2), (8457390, 5, 2), (8457392, 2, 2), (8457382, 1, 1), (8'
     '457380, 0, 1)]})'),
    ("('0020BF20', 'hub', {'mode': 2, 'sel': 18, 'spad': {1879063396: 16}, 'b': [(8457384, 12, 2"
     '), (8457386, 3, 2), (8457388, 1, 2), (8457390, 5, 2), (8457392, 2, 2), (8457382, 3, 1), (8'
     '457380, 0, 1)]})'),
    ("('0020BF20', 'hub', {'mode': 2, 'sel': 19, 'spad': {1879063396: 0}, 'b': [(8457384, 12, 2)"
     ', (8457386, 3, 2), (8457388, 1, 2), (8457390, 5, 2), (8457392, 2, 2), (8457382, 4, 1), (84'
     '57380, 1, 1)]})'),
    ("('0020BF20', 'hub', {'mode': 2, 'sel': 19, 'spad': {1879063396: 16}, 'b': [(8457384, 12, 2"
     '), (8457386, 3, 2), (8457388, 1, 2), (8457390, 5, 2), (8457392, 2, 2), (8457382, 2, 1), (8'
     '457380, 0, 1)]})'),
    ("('0020BF20', 'hub', {'mode': 2, 'sel': 19, 'spad': {1879063396: 16}, 'b': [(8457384, 12, 2"
     '), (8457386, 3, 2), (8457388, 1, 2), (8457390, 5, 2), (8457392, 2, 2), (8457382, 4, 1), (8'
     '457380, 1, 1)]})'),
    ("('0020BF20', 'hub', {'mode': 2, 'sel': 20, 'spad': {1879063396: 16}, 'b': [(8457384, 12, 2"
     '), (8457386, 3, 2), (8457388, 1, 2), (8457390, 5, 2), (8457392, 2, 2), (8457382, 3, 1), (8'
     '457380, 0, 1)]})'),
    ("('0020BF20', 'hub', {'mode': 2, 'sel': 21, 'spad': {1879063396: 0}, 'b': [(8457384, 12, 2)"
     ', (8457386, 3, 2), (8457388, 1, 2), (8457390, 5, 2), (8457392, 2, 2), (8457382, 1, 1), (84'
     '57380, 0, 1)]})'),
    ("('0020BF20', 'hub', {'mode': 2, 'sel': 21, 'spad': {1879063396: 16}, 'b': [(8457384, 12, 2"
     '), (8457386, 3, 2), (8457388, 1, 2), (8457390, 5, 2), (8457392, 2, 2), (8457382, 1, 1), (8'
     '457380, 0, 1)]})'),
    ("('0020BF20', 'hub', {'mode': 2, 'sel': 21, 'spad': {1879063396: 16}, 'b': [(8457384, 12, 2"
     '), (8457386, 3, 2), (8457388, 1, 2), (8457390, 5, 2), (8457392, 2, 2), (8457382, 4, 1), (8'
     '457380, 0, 1)]})'),
    ("('0020BF20', 'hub', {'mode': 2, 'sel': 22, 'spad': {1879063396: 0}, 'b': [(8457384, 12, 2)"
     ', (8457386, 3, 2), (8457388, 1, 2), (8457390, 5, 2), (8457392, 2, 2), (8457382, 2, 1), (84'
     '57380, 1, 1)]})'),
    ("('0020BF20', 'hub', {'mode': 2, 'sel': 22, 'spad': {1879063396: 16}, 'b': [(8457384, 12, 2"
     '), (8457386, 3, 2), (8457388, 1, 2), (8457390, 5, 2), (8457392, 2, 2), (8457382, 0, 1), (8'
     '457380, 2, 1)]})'),
    ("('0020BF20', 'hub', {'mode': 2, 'sel': 22, 'spad': {1879063396: 16}, 'b': [(8457384, 12, 2"
     '), (8457386, 3, 2), (8457388, 1, 2), (8457390, 5, 2), (8457392, 2, 2), (8457382, 2, 1), (8'
     '457380, 1, 1)]})'),
    ("('00211970', 'hub', {'t': {4: (0, 1)}, 'b': [(8455856, 0, 1), (8455857, 0, 1), (8457315, 2"
     ', 1), (8457396, 60, 2)]})'),
    ("('00211970', 'hub', {'t': {4: (0, 1)}, 'b': [(8455856, 1, 1), (8455857, 16, 1), (8457315, "
     '2, 1), (8457396, 60, 2)]})'),
    ("('00211970', 'hub', {'t': {4: (0, 1)}, 'b': [(8455856, 5, 1), (8455857, 0, 1), (8457315, 2"
     ', 1), (8457396, 30, 2)]})'),
    ("('00211970', 'hub', {'t': {4: (0, 1)}, 'b': [(8455856, 5, 1), (8455857, 0, 1), (8457315, 2"
     ', 1), (8457396, 60, 2)]})'),
    ("('00211970', 'hub', {'t': {4: (1, 1), 17: (0, 1)}, 'pads': (0, 0, 0)})"),
    ("('00211970', 'hub', {'t': {4: (1, 1), 17: (0, 1)}, 'pads': (0, 32, 0)})"),
    ("('00211970', 'hub', {'t': {4: (1, 1), 17: (7, 1)}, 'pads': (0, 0, 0), 'stick': (1, 128)})"),
    ("('00211970', 'hub', {'t': {4: (1, 1), 17: (7, 1)}, 'pads': (0, 0, 0), 'stick': (128, 1)})"),
    ("('00211970', 'hub', {'t': {4: (1, 1), 17: (7, 1)}, 'pads': (0, 0, 0), 'stick': (18, 64)})"),
    ("('00211970', 'hub', {'t': {4: (1, 1), 17: (7, 1)}, 'pads': (0, 0, 0), 'stick': (238, 192)}"
     ')'),
    ("('00211970', 'hub', {'t': {4: (1, 1), 17: (7, 1)}, 'pads': (0, 0, 0), 'stick': (255, 128)}"
     ')'),
    ("('00211970', 'hub', {'t': {4: (1, 1), 17: (7, 1)}, 'pads': (0, 64, 0), 'stick': (128, 255)"
     '})'),
    ("('00211970', 'hub', {'t': {4: (10, 1), 6: (0, 1)}, 'pads': (0, 16, 0)})"),
    ("('00211970', 'hub', {'t': {4: (10, 1), 6: (0, 1)}, 'pads': (0, 32768, 0)})"),
    ("('00211970', 'hub', {'t': {4: (10, 1), 6: (0, 1)}, 'pads': (0, 64, 0)})"),
    ("('00211970', 'hub', {'t': {4: (10, 1), 6: (0, 1)}, 'pads': (0, 8192, 0)})"),
    ("('00211970', 'hub', {'t': {4: (10, 1), 6: (1, 1)}, 'pads': (0, 32768, 0)})"),
    ("('00211970', 'hub', {'t': {4: (10, 1), 6: (1, 1)}, 'pads': (0, 64, 0)})"),
    ("('00211970', 'hub', {'t': {4: (10, 1), 6: (1, 1)}, 'pads': (0, 8192, 0)})"),
    ("('00211970', 'hub', {'t': {4: (11, 1), 30: (10, 2), 28: (60, 2)}, 'pads': (0, 0, 0), 'spad"
     "': {1879063396: 0}})"),
    ("('00211970', 'hub', {'t': {4: (11, 1), 30: (10, 2), 28: (60, 2)}, 'pads': (0, 64, 0), 'spa"
     "d': {1879063396: 3}})"),
    ("('00211970', 'hub', {'t': {4: (11, 1), 30: (60, 2), 28: (60, 2)}, 'pads': (0, 0, 0), 'spad"
     "': {1879063396: 1}})"),
    ("('00211970', 'hub', {'t': {4: (12, 1), 60: (1, 2)}, 'pads': (0, 0, 0), 'b': [(8455857, 19,"
     ' 1)]})'),
    ("('00211970', 'hub', {'t': {4: (12, 1), 60: (2, 2)}, 'pads': (0, 0, 0), 'b': [(8455857, 19,"
     ' 1)]})'),
    ("('00211970', 'hub', {'t': {4: (12, 1), 60: (5, 2)}, 'pads': (0, 16, 0), 'b': [(8455857, 19"
     ', 1)]})'),
    ("('00211970', 'hub', {'t': {4: (13, 1)}})"),
    ("('00211970', 'hub', {'t': {4: (2, 1), 21: (0, 1)}})"),
    ("('00211970', 'hub', {'t': {4: (2, 1), 21: (7, 1)}})"),
    ("('00211970', 'hub', {'t': {4: (3, 1), 5: (0, 1), 6: (7, 1), 22: (48, 1)}, 'b': [(2579416, "
     '1, 1)]})'),
    ("('00211970', 'hub', {'t': {4: (3, 1), 5: (1, 1), 6: (7, 1), 22: (48, 1)}, 'b': [(2579416, "
     '0, 1)]})'),
    ("('00211970', 'hub', {'t': {4: (3, 1), 5: (1, 1), 6: (7, 1), 22: (48, 1)}, 'b': [(2579416, "
     '1, 1)]})'),
    ("('00211970', 'hub', {'t': {4: (4, 1), 5: (0, 1)}, 'b': [(8457316, 1, 1), (8457317, 1, 1), "
     '(8457318, 1, 1), (8457319, 1, 1), (8457320, 1, 1), (8457321, 0, 1), (8457322, 0, 1), (8457'
     '323, 0, 1), (8457324, 0, 1), (8457325, 0, 1), (8457326, 0, 1), (8457327, 0, 1), (8457328, '
     "0, 1), (8457329, 0, 1), (8457330, 0, 1), (8457331, 0, 1)], 'pads': (0, 32, 0)})"),
    ("('00211970', 'hub', {'t': {4: (5, 1), 5: (0, 1)}, 'b': [(8457316, 1, 1), (8457317, 1, 1), "
     '(8457318, 1, 1), (8457319, 1, 1), (8457320, 1, 1), (8457321, 0, 1), (8457322, 0, 1), (8457'
     '323, 0, 1), (8457324, 0, 1), (8457325, 0, 1), (8457326, 0, 1), (8457327, 0, 1), (8457328, '
     "0, 1), (8457329, 0, 1), (8457330, 0, 1), (8457331, 0, 1)], 'pads': (0, 32, 0)})"),
    ("('00211970', 'hub', {'t': {4: (6, 1), 5: (0, 1)}, 'b': [(8457316, 1, 1), (8457317, 1, 1), "
     '(8457318, 1, 1), (8457319, 1, 1), (8457320, 1, 1), (8457321, 0, 1), (8457322, 0, 1), (8457'
     '323, 0, 1), (8457324, 0, 1), (8457325, 0, 1), (8457326, 0, 1), (8457327, 0, 1), (8457328, '
     "0, 1), (8457329, 0, 1), (8457330, 0, 1), (8457331, 0, 1)], 'pads': (0, 32, 0)})"),
    ("('00211970', 'hub', {'t': {4: (7, 1), 5: (0, 1)}, 'b': [(8457316, 1, 1), (8457317, 1, 1), "
     '(8457318, 1, 1), (8457319, 1, 1), (8457320, 1, 1), (8457321, 0, 1), (8457322, 0, 1), (8457'
     '323, 0, 1), (8457324, 0, 1), (8457325, 0, 1), (8457326, 0, 1), (8457327, 0, 1), (8457328, '
     "0, 1), (8457329, 0, 1), (8457330, 0, 1), (8457331, 0, 1)], 'pads': (0, 32, 0)})"),
    ("('00211970', 'hub', {'t': {4: (8, 1), 5: (0, 1)}, 'b': [(8457316, 1, 1), (8457317, 1, 1), "
     '(8457318, 1, 1), (8457319, 1, 1), (8457320, 1, 1), (8457321, 0, 1), (8457322, 0, 1), (8457'
     '323, 0, 1), (8457324, 0, 1), (8457325, 0, 1), (8457326, 0, 1), (8457327, 0, 1), (8457328, '
     "0, 1), (8457329, 0, 1), (8457330, 0, 1), (8457331, 0, 1)], 'pads': (0, 32, 0)})"),
    ("('00211970', 'hub', {'t': {4: (9, 1), 6: (1, 1)}, 'pads': (0, 0, 0), 'b': [(8455857, 16, 1"
     ')]})'),
    ("('00211970', 'hub', {'t': {4: (9, 1), 6: (2, 1)}, 'pads': (0, 0, 0), 'b': [(8455857, 16, 1"
     ')]})'),
    ("('00211970', 'hub', {'t': {4: (9, 1), 6: (5, 1)}, 'pads': (0, 64, 0), 'b': [(8455857, 18, "
     '1)]})'),
    ("('00212B60', 'hub', {'t': {17: (0, 1)}, 'b': [(8457380, 2, 1), (8457381, 5, 1), (8457382, "
     '2, 1), (8457383, 7, 1)]})'),
    ("('00212B60', 'hub', {'t': {17: (2, 1)}, 'b': [(8457380, 0, 1), (8457381, 5, 1), (8457382, "
     '2, 1), (8457383, 7, 1)]})'),
    ("('00212B60', 'hub', {'t': {17: (3, 1)}, 'b': [(8457380, 1, 1), (8457381, 5, 1), (8457382, "
     '2, 1), (8457383, 7, 1)]})'),
    ("('00212B60', 'hub', {'t': {17: (6, 1)}, 'b': [(8457380, 0, 1), (8457381, 5, 1), (8457382, "
     '2, 1), (8457383, 7, 1)]})'),
    ("('00212B60', 'hub', {'t': {17: (6, 1)}, 'b': [(8457380, 1, 1), (8457381, 5, 1), (8457382, "
     '2, 1), (8457383, 7, 1)]})'),
    ("('00212F30', 'hub', {'id': 10, 'ctx': 18446744071570489472})"),
    ("('00213A00', 'hub', {'a1': 0, 'pads': (0, 0, 0), 'b': [(8454528, 72, 1), (8454529, 77, 1),"
     ' (8454530, 82, 1), (8454531, 87, 1), (8454532, 92, 1), (8454533, 97, 1), (8454534, 102, 1)'
     ', (8454535, 107, 1), (8454536, 3, 1), (8454537, 8, 1), (8454538, 13, 1), (8454539, 18, 1),'
     ' (8454540, 23, 1), (8454541, 28, 1), (8454542, 33, 1), (8454543, 38, 1), (8454544, 43, 1),'
     ' (8454545, 48, 1), (8454546, 53, 1), (8454547, 58, 1), (8454472, 0, 1), (8454473, 0, 1), ('
     '8454471, 0, 1)]})'),
    ("('00213A00', 'hub', {'a1': 0, 'pads': (0, 0, 0), 'b': [(8454528, 72, 1), (8454529, 77, 1),"
     ' (8454530, 82, 1), (8454531, 87, 1), (8454532, 92, 1), (8454533, 97, 1), (8454534, 102, 1)'
     ', (8454535, 107, 1), (8454536, 3, 1), (8454537, 8, 1), (8454538, 13, 1), (8454539, 18, 1),'
     ' (8454540, 23, 1), (8454541, 28, 1), (8454542, 33, 1), (8454543, 38, 1), (8454544, 43, 1),'
     ' (8454545, 48, 1), (8454546, 53, 1), (8454547, 58, 1), (8454472, 9, 1), (8454473, 4, 1), ('
     '8454471, 0, 1)]})'),
    ("('00213A00', 'hub', {'a1': 0, 'pads': (0, 0, 16384), 'b': [(8454528, 72, 1), (8454529, 77,"
     ' 1), (8454530, 82, 1), (8454531, 87, 1), (8454532, 92, 1), (8454533, 97, 1), (8454534, 102'
     ', 1), (8454535, 107, 1), (8454536, 3, 1), (8454537, 8, 1), (8454538, 13, 1), (8454539, 18,'
     ' 1), (8454540, 23, 1), (8454541, 28, 1), (8454542, 33, 1), (8454543, 38, 1), (8454544, 43,'
     ' 1), (8454545, 48, 1), (8454546, 53, 1), (8454547, 58, 1), (8454472, 9, 1), (8454473, 4, 1'
     '), (8454471, 0, 1)]})'),
    ("('00213A00', 'hub', {'a1': 0, 'pads': (0, 0, 16384), 'b': [(8454528, 72, 1), (8454529, 77,"
     ' 1), (8454530, 82, 1), (8454531, 87, 1), (8454532, 92, 1), (8454533, 97, 1), (8454534, 102'
     ', 1), (8454535, 107, 1), (8454536, 3, 1), (8454537, 8, 1), (8454538, 13, 1), (8454539, 18,'
     ' 1), (8454540, 23, 1), (8454541, 28, 1), (8454542, 33, 1), (8454543, 38, 1), (8454544, 43,'
     ' 1), (8454545, 48, 1), (8454546, 53, 1), (8454547, 58, 1), (8454472, 9, 1), (8454473, 4, 1'
     '), (8454471, 7, 1)]})'),
    ("('00213A00', 'hub', {'a1': 0, 'pads': (0, 0, 4096), 'b': [(8454528, 72, 1), (8454529, 77, "
     '1), (8454530, 82, 1), (8454531, 87, 1), (8454532, 92, 1), (8454533, 97, 1), (8454534, 102,'
     ' 1), (8454535, 107, 1), (8454536, 3, 1), (8454537, 8, 1), (8454538, 13, 1), (8454539, 18, '
     '1), (8454540, 23, 1), (8454541, 28, 1), (8454542, 33, 1), (8454543, 38, 1), (8454544, 43, '
     '1), (8454545, 48, 1), (8454546, 53, 1), (8454547, 58, 1), (8454472, 9, 1), (8454473, 4, 1)'
     ', (8454471, 0, 1)]})'),
    ("('00213A00', 'hub', {'a1': 0, 'pads': (0, 0, 4096), 'b': [(8454528, 72, 1), (8454529, 77, "
     '1), (8454530, 82, 1), (8454531, 87, 1), (8454532, 92, 1), (8454533, 97, 1), (8454534, 102,'
     ' 1), (8454535, 107, 1), (8454536, 3, 1), (8454537, 8, 1), (8454538, 13, 1), (8454539, 18, '
     '1), (8454540, 23, 1), (8454541, 28, 1), (8454542, 33, 1), (8454543, 38, 1), (8454544, 43, '
     '1), (8454545, 48, 1), (8454546, 53, 1), (8454547, 58, 1), (8454472, 9, 1), (8454473, 4, 1)'
     ', (8454471, 3, 1)]})'),
    ("('00213A00', 'hub', {'a1': 1024, 'pads': (0, 0, 0), 'b': [(8454528, 72, 1), (8454529, 77, "
     '1), (8454530, 82, 1), (8454531, 87, 1), (8454532, 92, 1), (8454533, 97, 1), (8454534, 102,'
     ' 1), (8454535, 107, 1), (8454536, 3, 1), (8454537, 8, 1), (8454538, 13, 1), (8454539, 18, '
     '1), (8454540, 23, 1), (8454541, 28, 1), (8454542, 33, 1), (8454543, 38, 1), (8454544, 43, '
     '1), (8454545, 48, 1), (8454546, 53, 1), (8454547, 58, 1), (8454472, 9, 1), (8454473, 4, 1)'
     ', (8454471, 0, 1)]})'),
    ("('00213C50', 'hub', {'a1': 0, 'b': [(8454528, 72, 1), (8454529, 77, 1), (8454530, 82, 1), "
     '(8454531, 87, 1), (8454532, 92, 1), (8454533, 97, 1), (8454534, 102, 1), (8454535, 107, 1)'
     ', (8454536, 3, 1), (8454537, 8, 1), (8454538, 13, 1), (8454539, 18, 1), (8454540, 23, 1), '
     '(8454541, 28, 1), (8454542, 33, 1), (8454543, 38, 1), (8454544, 43, 1), (8454545, 48, 1), '
     '(8454546, 53, 1), (8454547, 58, 1), (8454472, 0, 1), (8454473, 0, 1)]})'),
    ("('00213C50', 'hub', {'a1': 1, 'b': [(8454528, 72, 1), (8454529, 77, 1), (8454530, 82, 1), "
     '(8454531, 87, 1), (8454532, 92, 1), (8454533, 97, 1), (8454534, 102, 1), (8454535, 107, 1)'
     ', (8454536, 3, 1), (8454537, 8, 1), (8454538, 13, 1), (8454539, 18, 1), (8454540, 23, 1), '
     '(8454541, 28, 1), (8454542, 33, 1), (8454543, 38, 1), (8454544, 43, 1), (8454545, 48, 1), '
     '(8454546, 53, 1), (8454547, 58, 1), (8454472, 0, 1), (8454473, 3, 1)]})'),
    ("('00213C50', 'hub', {'a1': 1, 'b': [(8454528, 72, 1), (8454529, 77, 1), (8454530, 82, 1), "
     '(8454531, 87, 1), (8454532, 92, 1), (8454533, 97, 1), (8454534, 102, 1), (8454535, 107, 1)'
     ', (8454536, 3, 1), (8454537, 8, 1), (8454538, 13, 1), (8454539, 18, 1), (8454540, 23, 1), '
     '(8454541, 28, 1), (8454542, 33, 1), (8454543, 38, 1), (8454544, 43, 1), (8454545, 48, 1), '
     '(8454546, 53, 1), (8454547, 58, 1), (8454472, 9, 1), (8454473, 0, 1)]})'),
    ("('00213CC0', 'hub', {'b': [(8454528, 72, 1), (8454529, 77, 1), (8454530, 82, 1), (8454531,"
     ' 87, 1), (8454532, 92, 1), (8454533, 97, 1), (8454534, 102, 1), (8454535, 107, 1), (845453'
     '6, 3, 1), (8454537, 8, 1), (8454538, 13, 1), (8454539, 18, 1), (8454540, 23, 1), (8454541,'
     ' 28, 1), (8454542, 33, 1), (8454543, 38, 1), (8454544, 43, 1), (8454545, 48, 1), (8454546,'
     ' 53, 1), (8454547, 58, 1), (8454472, 9, 1), (8454473, 0, 1), (8454471, 5, 1), (8454474, 0,'
     ' 1), (8454476, 0, 2)]})'),
    ("('00213CC0', 'hub', {'b': [(8454528, 72, 1), (8454529, 77, 1), (8454530, 82, 1), (8454531,"
     ' 87, 1), (8454532, 92, 1), (8454533, 97, 1), (8454534, 102, 1), (8454535, 107, 1), (845453'
     '6, 3, 1), (8454537, 8, 1), (8454538, 13, 1), (8454539, 18, 1), (8454540, 23, 1), (8454541,'
     ' 28, 1), (8454542, 33, 1), (8454543, 38, 1), (8454544, 43, 1), (8454545, 48, 1), (8454546,'
     ' 53, 1), (8454547, 58, 1), (8454472, 9, 1), (8454473, 0, 1), (8454471, 5, 1), (8454474, 0,'
     ' 1), (8454476, 65523, 2)]})'),
    ("('00213CC0', 'hub', {'b': [(8454528, 72, 1), (8454529, 77, 1), (8454530, 82, 1), (8454531,"
     ' 87, 1), (8454532, 92, 1), (8454533, 97, 1), (8454534, 102, 1), (8454535, 107, 1), (845453'
     '6, 3, 1), (8454537, 8, 1), (8454538, 13, 1), (8454539, 18, 1), (8454540, 23, 1), (8454541,'
     ' 28, 1), (8454542, 33, 1), (8454543, 38, 1), (8454544, 43, 1), (8454545, 48, 1), (8454546,'
     ' 53, 1), (8454547, 58, 1), (8454472, 9, 1), (8454473, 0, 1), (8454471, 5, 1), (8454474, 1,'
     ' 1), (8454476, 11, 2)]})'),
    ("('00213CC0', 'hub', {'b': [(8454528, 72, 1), (8454529, 77, 1), (8454530, 82, 1), (8454531,"
     ' 87, 1), (8454532, 92, 1), (8454533, 97, 1), (8454534, 102, 1), (8454535, 107, 1), (845453'
     '6, 3, 1), (8454537, 8, 1), (8454538, 13, 1), (8454539, 18, 1), (8454540, 23, 1), (8454541,'
     ' 28, 1), (8454542, 33, 1), (8454543, 38, 1), (8454544, 43, 1), (8454545, 48, 1), (8454546,'
     ' 53, 1), (8454547, 58, 1), (8454472, 9, 1), (8454473, 0, 1), (8454471, 5, 1), (8454474, 1,'
     ' 1), (8454476, 65523, 2)]})'),
    ("('00213CC0', 'hub', {'b': [(8454528, 72, 1), (8454529, 77, 1), (8454530, 82, 1), (8454531,"
     ' 87, 1), (8454532, 92, 1), (8454533, 97, 1), (8454534, 102, 1), (8454535, 107, 1), (845453'
     '6, 3, 1), (8454537, 8, 1), (8454538, 13, 1), (8454539, 18, 1), (8454540, 23, 1), (8454541,'
     ' 28, 1), (8454542, 33, 1), (8454543, 38, 1), (8454544, 43, 1), (8454545, 48, 1), (8454546,'
     ' 53, 1), (8454547, 58, 1), (8454472, 9, 1), (8454473, 8, 1), (8454471, 5, 1), (8454474, 0,'
     ' 1), (8454476, 65523, 2)]})'),
    ("('00213CC0', 'hub', {'b': [(8454528, 72, 1), (8454529, 77, 1), (8454530, 82, 1), (8454531,"
     ' 87, 1), (8454532, 92, 1), (8454533, 97, 1), (8454534, 102, 1), (8454535, 107, 1), (845453'
     '6, 3, 1), (8454537, 8, 1), (8454538, 13, 1), (8454539, 18, 1), (8454540, 23, 1), (8454541,'
     ' 28, 1), (8454542, 33, 1), (8454543, 38, 1), (8454544, 43, 1), (8454545, 48, 1), (8454546,'
     ' 53, 1), (8454547, 58, 1), (8454472, 9, 1), (8454473, 8, 1), (8454471, 5, 1), (8454474, 1,'
     ' 1), (8454476, 11, 2)]})'),
    ("('00214570', 'hub', {'b': [(8457339, 0, 1), (8457340, 0, 1), (8457341, 0, 1), (8457342, 0,"
     " 1), (8455856, 0, 1)], 't': {5: (0, 1)}})"),
    ("('00214570', 'hub', {'b': [(8457339, 0, 1), (8457340, 0, 1), (8457341, 0, 1), (8457342, 0,"
     " 1), (8455856, 1, 1), (8455857, 23, 1), (8457312, 1, 1)], 't': {5: (0, 1)}})"),
    ("('00214570', 'hub', {'b': [(8457339, 1, 1), (8457340, 0, 1), (8457341, 0, 1), (8457342, 0,"
     " 1), (8455856, 0, 1)], 't': {5: (0, 1)}})"),
    ("('00214570', 'hub', {'b': [(8457339, 1, 1), (8457340, 1, 1), (8457341, 1, 1), (8457342, 1,"
     " 1), (8455856, 1, 1), (8455857, 24, 1), (8457312, 1, 1)], 't': {5: (0, 1)}})"),
    ("('00214570', 'hub', {'b': [(8457339, 1, 1), (8457340, 1, 1), (8457341, 1, 1), (8457342, 1,"
     " 1), (8455856, 1, 1), (8455857, 25, 1), (8457312, 1, 1)], 't': {5: (0, 1)}})"),
    ("('00214570', 'hub', {'b': [(8457339, 1, 1), (8457340, 1, 1), (8457341, 1, 1), (8457342, 1,"
     " 1), (8455856, 1, 1), (8455857, 25, 1), (8457312, 2, 1)], 't': {5: (0, 1)}})"),
    ("('00214570', 'hub', {'pads': (0, 0, 0), 'ring': ([0, 1, 2, 3], 4), 't': {5: (1, 1), 6: (1,"
     ' 1), 23: (0, 1)}})'),
    ("('00214570', 'hub', {'pads': (0, 0, 0), 'ring': ([0, 1], 2), 't': {5: (3, 1), 6: (1, 1)}})"),
    ("('00214570', 'hub', {'pads': (0, 0, 0), 'ring': ([0, 1], 2), 't': {5: (3, 1), 6: (2, 1)}})"),
    ("('00214570', 'hub', {'pads': (0, 32, 0), 'ring': ([0, 1, 2, 3], 4), 't': {5: (1, 1), 6: (0"
     ', 1), 23: (0, 1)}})'),
    ("('00214570', 'hub', {'pads': (0, 4096, 0), 'ring': ([0, 1], 2), 't': {5: (3, 1), 6: (2, 1)"
     '}})'),
    ("('00214570', 'hub', {'pads': (0, 64, 0), 'ring': ([0, 1, 2, 3], 4), 't': {5: (1, 1), 6: (0"
     ', 1), 23: (0, 1)}})'),
    ("('00214570', 'hub', {'ring': ([0, 1, 2, 3], 4), 't': {5: (2, 1), 26: (1, 1), 28: (0, 2)}})"),
    ("('00214570', 'hub', {'ring': ([0, 1, 2, 3], 4), 't': {5: (2, 1), 26: (1, 1), 28: (44, 2)}}"
     ')'),
    ("('00214570', 'hub', {'t': {5: (1, 1), 23: (3, 1), 25: (0, 1), 26: (0, 1), 6: (0, 1)}, 'pad"
     "s': (0, 0, 16384), 'ring': ([0, 1, 2, 3, 4, 0, 1], 7)})"),
    ("('00214570', 'hub', {'t': {5: (4, 1)}})"),
    ("('00215870', 'hub', {'b': [(8457351, 0, 1), (8457352, 0, 1), (8455856, 0, 1), (8457353, 0,"
     ' 1), (8457354, 0, 1), (8457355, 0, 1), (8457356, 0, 1), (8457357, 0, 1), (8457358, 0, 1), '
     '(8457359, 0, 1), (8457360, 0, 1), (8457361, 0, 1), (8457362, 0, 1), (8457363, 0, 1), (8457'
     '364, 0, 1), (8457365, 0, 1), (8457366, 0, 1), (8457367, 0, 1), (8457368, 0, 1), (8457369, '
     '0, 1), (8457370, 0, 1), (8457371, 0, 1), (8457372, 0, 1), (8457373, 0, 1), (8457374, 0, 1)'
     ", (8457375, 0, 1)], 't': {5: (0, 1)}})"),
    ("('00215870', 'hub', {'b': [(8457351, 0, 1), (8457352, 0, 1), (8455856, 0, 1), (8457353, 1,"
     ' 1), (8457354, 0, 1), (8457355, 0, 1), (8457356, 0, 1), (8457357, 0, 1), (8457358, 0, 1), '
     '(8457359, 0, 1), (8457360, 0, 1), (8457361, 0, 1), (8457362, 0, 1), (8457363, 0, 1), (8457'
     '364, 0, 1), (8457365, 1, 1), (8457366, 0, 1), (8457367, 0, 1), (8457368, 0, 1), (8457369, '
     '0, 1), (8457370, 0, 1), (8457371, 0, 1), (8457372, 0, 1), (8457373, 0, 1), (8457374, 0, 1)'
     ", (8457375, 0, 1)], 't': {5: (0, 1)}})"),
    ("('00215870', 'hub', {'b': [(8457351, 0, 1), (8457352, 1, 1), (8455856, 0, 1), (8457353, 0,"
     ' 1), (8457354, 0, 1), (8457355, 0, 1), (8457356, 0, 1), (8457357, 0, 1), (8457358, 0, 1), '
     '(8457359, 0, 1), (8457360, 0, 1), (8457361, 0, 1), (8457362, 0, 1), (8457363, 0, 1), (8457'
     '364, 0, 1), (8457365, 0, 1), (8457366, 0, 1), (8457367, 0, 1), (8457368, 0, 1), (8457369, '
     '0, 1), (8457370, 0, 1), (8457371, 0, 1), (8457372, 0, 1), (8457373, 0, 1), (8457374, 0, 1)'
     ", (8457375, 0, 1)], 't': {5: (0, 1)}})"),
    ("('00215870', 'hub', {'b': [(8457351, 1, 1), (8455856, 3, 1), (8455857, 36, 1), (8457353, 1"
     ', 1), (8457354, 1, 1), (8457355, 1, 1), (8457356, 1, 1), (8457357, 1, 1), (8457358, 1, 1),'
     ' (8457359, 1, 1), (8457360, 1, 1), (8457361, 1, 1), (8457362, 1, 1), (8457363, 1, 1), (845'
     '7364, 1, 1), (8457365, 1, 1), (8457366, 1, 1), (8457367, 1, 1), (8457368, 1, 1), (8457369,'
     ' 1, 1), (8457370, 1, 1), (8457371, 1, 1), (8457372, 1, 1), (8457373, 1, 1), (8457374, 1, 1'
     "), (8457375, 1, 1)], 't': {5: (0, 1)}})"),
    ("('00215870', 'hub', {'b': [(8457351, 1, 1), (8455856, 3, 1), (8455857, 50, 1), (8457353, 1"
     ', 1), (8457354, 1, 1), (8457355, 1, 1), (8457356, 1, 1), (8457357, 1, 1), (8457358, 1, 1),'
     ' (8457359, 1, 1), (8457360, 1, 1), (8457361, 1, 1), (8457362, 1, 1), (8457363, 1, 1), (845'
     '7364, 1, 1), (8457365, 1, 1), (8457366, 1, 1), (8457367, 1, 1), (8457368, 1, 1), (8457369,'
     ' 1, 1), (8457370, 1, 1), (8457371, 1, 1), (8457372, 1, 1), (8457373, 1, 1), (8457374, 1, 1'
     "), (8457375, 1, 1)], 't': {5: (0, 1)}})"),
    ("('00215870', 'hub', {'pads': (0, 0, 0), 'ring': ([0], 1), 't': {5: (3, 1), 6: (1, 1)}})"),
    ("('00215870', 'hub', {'pads': (0, 0, 0), 'ring': ([0], 1), 't': {5: (3, 1), 6: (2, 1)}})"),
    ("('00215870', 'hub', {'pads': (0, 0, 0), 'ring': ([0], 1), 't': {5: (5, 1), 6: (1, 1)}})"),
    ("('00215870', 'hub', {'pads': (0, 0, 0), 'ring': ([0], 1), 't': {5: (5, 1), 6: (2, 1)}})"),
    ("('00215870', 'hub', {'pads': (0, 32, 0), 'ring': ([0, 2, 3, 15], 4), 't': {5: (1, 1), 23: "
     '(0, 1)}})'),
    ("('00215870', 'hub', {'pads': (0, 32, 0), 'ring': ([0], 1), 't': {5: (3, 1), 6: (2, 1)}})"),
    ("('00215870', 'hub', {'pads': (0, 32, 0), 'ring': ([0], 1), 't': {5: (5, 1), 6: (1, 1)}})"),
    ("('00215870', 'hub', {'pads': (0, 32, 0), 'ring': ([2], 1), 't': {5: (4, 1), 6: (0, 1), 48:"
     ' (27263232, 4)}})'),
    ("('00215870', 'hub', {'pads': (0, 32768, 0), 'ring': ([2], 1), 't': {5: (4, 1), 6: (0, 1), "
     '48: (27263232, 4)}})'),
    ("('00215870', 'hub', {'pads': (0, 32768, 0), 'ring': ([2], 1), 't': {5: (4, 1), 6: (1, 1), "
     '48: (27263232, 4)}})'),
    ("('00215870', 'hub', {'pads': (0, 64, 0), 'ring': ([0, 2, 3, 15], 4), 't': {5: (1, 1), 23: "
     '(0, 1)}})'),
    ("('00215870', 'hub', {'pads': (0, 64, 0), 'ring': ([2], 1), 'dev': (37,), 't': {5: (1, 1), "
     "23: (0, 1), 25: (0, 1)}, 'b': [(8455921, 1, 1)]})"),
    ("('00215870', 'hub', {'pads': (0, 64, 0), 'ring': ([2], 1), 't': {5: (1, 1), 23: (0, 1), 25"
     ": (0, 1)}, 'dev': (37,)})"),
    ("('00215870', 'hub', {'pads': (0, 64, 0), 'ring': ([2], 1), 't': {5: (4, 1), 6: (0, 1), 48:"
     ' (27263232, 4)}})'),
    ("('00215870', 'hub', {'pads': (0, 64, 0), 'ring': ([2], 1), 't': {5: (4, 1), 6: (1, 1), 48:"
     ' (27263232, 4)}})'),
    ("('00215870', 'hub', {'pads': (0, 8192, 0), 'ring': ([2], 1), 't': {5: (4, 1), 6: (0, 1), 4"
     '8: (27263232, 4)}})'),
    ("('00215870', 'hub', {'pads': (0, 8192, 0), 'ring': ([2], 1), 't': {5: (4, 1), 6: (1, 1), 4"
     '8: (27263232, 4)}})'),
    ("('00215870', 'hub', {'ring': ([0, 2, 3], 3), 't': {5: (2, 1), 26: (1, 1), 28: (0, 2)}})"),
    ("('00215870', 'hub', {'ring': ([0, 2, 3], 3), 't': {5: (2, 1), 26: (1, 1), 28: (44, 2)}})"),
    ("('00215870', 'hub', {'t': {5: (1, 1), 23: (3, 1), 25: (0, 1), 26: (0, 1), 6: (0, 1)}, 'pad"
     "s': (0, 0, 16384), 'ring': ([0, 1, 2, 3, 4, 0, 1], 7)})"),
    ("('00215870', 'hub', {'t': {5: (6, 1)}})"),
    ("('002160B0', 'hub', {'b': [(8457346, 0, 1), (8457347, 0, 1), (8457348, 0, 1), (8457349, 0,"
     " 1), (8457350, 0, 1), (8455856, 0, 1)], 't': {5: (0, 1)}})"),
    ("('002160B0', 'hub', {'b': [(8457346, 0, 1), (8457347, 0, 1), (8457348, 0, 1), (8457349, 0,"
     " 1), (8457350, 0, 1), (8455856, 1, 1), (8455857, 30, 1)], 't': {5: (0, 1)}})"),
    ("('002160B0', 'hub', {'b': [(8457346, 1, 1), (8457347, 0, 1), (8457348, 1, 1), (8457349, 0,"
     " 1), (8457350, 0, 1), (8455856, 1, 1), (8455857, 32, 1)], 't': {5: (0, 1)}})"),
    ("('002160B0', 'hub', {'b': [(8457346, 1, 1), (8457347, 1, 1), (8457348, 0, 1), (8457349, 0,"
     ' 1), (8457350, 0, 1), (8455856, 4, 1), (8455888, 27263232, 4), (8456284, 1084227584, 4), ('
     "8456280, 1120403456, 4)], 't': {5: (0, 1)}})"),
    ("('002160B0', 'hub', {'b': [(8457346, 1, 1), (8457347, 1, 1), (8457348, 1, 1), (8457349, 1,"
     " 1), (8457350, 1, 1), (8455856, 0, 1)], 't': {5: (0, 1)}})"),
    ("('002160B0', 'hub', {'b': [(8457346, 1, 1), (8457347, 1, 1), (8457348, 1, 1), (8457349, 1,"
     ' 1), (8457350, 1, 1), (8455856, 4, 1), (8455888, 27263232, 4), (8456284, 0, 4), (8456280, '
     "1120272384, 4)], 't': {5: (0, 1)}})"),
    ("('002160B0', 'hub', {'b': [(8457346, 1, 1), (8457347, 1, 1), (8457348, 1, 1), (8457349, 1,"
     ' 1), (8457350, 1, 1), (8455856, 4, 1), (8455888, 27263232, 4), (8456284, 0, 4), (8456280, '
     "1120403456, 4)], 't': {5: (0, 1)}})"),
    ("('002160B0', 'hub', {'pads': (0, 0, 0), 'ring': ([0, 1, 2], 3), 't': {5: (3, 1), 6: (1, 1)"
     '}})'),
    ("('002160B0', 'hub', {'pads': (0, 0, 0), 'ring': ([0, 1, 2], 3), 't': {5: (3, 1), 6: (2, 1)"
     '}})'),
    ("('002160B0', 'hub', {'pads': (0, 0, 0), 'ring': ([0, 1, 2], 3), 't': {5: (6, 1), 6: (1, 1)"
     '}})'),
    ("('002160B0', 'hub', {'pads': (0, 0, 0), 'ring': ([0, 1, 2], 3), 't': {5: (6, 1), 6: (2, 1)"
     '}})'),
    ("('002160B0', 'hub', {'pads': (0, 0, 0), 'spad': {1879063396: 0}, 't': {5: (5, 1), 52: (110"
     "9393408, 4), 60: (1, 2), 6: (0, 1)}, 'b': [(8456280, 1109393408, 4)]})"),
    ("('002160B0', 'hub', {'pads': (0, 0, 0), 'spad': {1879063396: 0}, 't': {5: (5, 1), 52: (111"
     "2014848, 4), 60: (1, 2), 6: (0, 1)}, 'b': [(8456280, 1109393408, 4)]})"),
    ("('002160B0', 'hub', {'pads': (0, 0, 0), 'spad': {1879063396: 0}, 't': {5: (5, 1), 52: (111"
     "2014848, 4), 60: (2, 2), 6: (0, 1)}, 'b': [(8456280, 1109393408, 4)]})"),
    ("('002160B0', 'hub', {'pads': (0, 0, 4096), 'ring': ([0, 1, 2, 3, 4, 0, 1], 5), 't': {5: (1"
     ', 1), 23: (0, 1), 25: (4, 1)}})'),
    ("('002160B0', 'hub', {'pads': (0, 2048, 0), 'spad': {1879063396: 3}, 't': {5: (5, 1), 52: ("
     "1112014848, 4), 60: (2, 2), 6: (0, 1)}, 'b': [(8456280, 1109393408, 4)]})"),
    ("('002160B0', 'hub', {'pads': (0, 32, 0), 'ring': ([0, 1, 2, 3, 4, 0, 1], 5), 't': {5: (1, "
     '1), 23: (0, 1), 25: (0, 1)}})'),
    ("('002160B0', 'hub', {'pads': (0, 32, 0), 'ring': ([0, 1, 2], 3), 't': {5: (3, 1), 6: (2, 1"
     ')}})'),
    ("('002160B0', 'hub', {'pads': (0, 32, 0), 'ring': ([0, 1, 2], 3), 't': {5: (6, 1), 6: (1, 1"
     ')}})'),
    ("('002160B0', 'hub', {'pads': (0, 32, 0), 'ring': ([0, 1], 2), 't': {5: (4, 1), 6: (0, 1)}}"
     ')'),
    ("('002160B0', 'hub', {'pads': (0, 32768, 0), 'ring': ([0, 1], 2), 't': {5: (4, 1), 6: (0, 1"
     ')}})'),
    ("('002160B0', 'hub', {'pads': (0, 32768, 0), 'ring': ([0, 1], 2), 't': {5: (4, 1), 6: (1, 1"
     ')}})'),
    ("('002160B0', 'hub', {'pads': (0, 64, 0), 'ring': ([0, 0, 0, 0], 4), 't': {5: (1, 1), 23: ("
     "0, 1), 25: (0, 1)}, 'b': [(8455943, 0, 1), (8456284, 0, 4), (8456280, 1114374144, 4)]})"),
    ("('002160B0', 'hub', {'pads': (0, 64, 0), 'ring': ([0, 0, 0, 0], 4), 't': {5: (1, 1), 23: ("
     "0, 1), 25: (0, 1)}, 'b': [(8455943, 1, 1), (8456284, 0, 4), (8456280, 1114374144, 4)]})"),
    ("('002160B0', 'hub', {'pads': (0, 64, 0), 'ring': ([0, 0, 0, 0], 4), 't': {5: (1, 1), 23: ("
     "0, 1), 25: (0, 1)}, 'b': [(8455943, 1, 1), (8456284, 0, 4), (8456280, 1114636288, 4)]})"),
    ("('002160B0', 'hub', {'pads': (0, 64, 0), 'ring': ([0, 0, 1, 2], 4), 't': {5: (4, 1), 6: (0"
     ", 1), 23: (0, 1), 25: (0, 1), 48: (27263232, 4)}, 'b': [(8455943, 1, 1), (8456284, 1092616"
     '192, 4), (8456280, 1110704128, 4)]})'),
    ("('002160B0', 'hub', {'pads': (0, 64, 0), 'ring': ([0, 0, 1, 2], 4), 't': {5: (4, 1), 6: (0"
     ", 1), 23: (0, 1), 25: (0, 1), 48: (27263232, 4)}, 'b': [(8455943, 1, 1), (8456284, 1109393"
     '408, 4), (8456280, 1101004800, 4)]})'),
    ("('002160B0', 'hub', {'pads': (0, 64, 0), 'ring': ([0, 1, 2, 3, 4, 0, 1], 5), 't': {5: (1, "
     '1), 23: (0, 1), 25: (0, 1)}})'),
    ("('002160B0', 'hub', {'pads': (0, 64, 0), 'ring': ([0, 1, 2, 3, 4, 0, 1], 5), 't': {5: (1, "
     '1), 23: (3, 1), 25: (0, 1)}})'),
    ("('002160B0', 'hub', {'pads': (0, 64, 0), 'ring': ([0, 1, 2, 3, 4], 5), 'b': [(8457346, 1, "
     '1), (8457347, 1, 1), (8457348, 1, 1), (8457349, 0, 1), (8457350, 0, 1), (8456280, 11010048'
     "00, 4)], 't': {5: (4, 1), 6: (0, 1), 23: (0, 1), 25: (0, 1)}})"),
    ("('002160B0', 'hub', {'pads': (0, 64, 0), 'ring': ([0, 1, 2, 3, 4], 5), 'b': [(8457346, 1, "
     '1), (8457347, 1, 1), (8457348, 1, 1), (8457349, 1, 1), (8457350, 1, 1), (8456280, 11010048'
     "00, 4)], 't': {5: (4, 1), 6: (0, 1), 23: (0, 1), 25: (0, 1)}})"),
    ("('002160B0', 'hub', {'pads': (0, 64, 0), 'ring': ([0, 1], 2), 't': {5: (4, 1), 6: (1, 1)}}"
     ')'),
    ("('002160B0', 'hub', {'pads': (0, 64, 0), 'ring': ([1, 0, 1, 2], 4), 't': {5: (4, 1), 6: (0"
     ", 1), 23: (0, 1), 25: (0, 1), 48: (27263232, 4)}, 'b': [(8455943, 0, 1), (8456284, 1109393"
     '408, 4), (8456280, 1101004800, 4)]})'),
    ("('002160B0', 'hub', {'pads': (0, 64, 0), 'ring': ([1, 0, 1, 2], 4), 't': {5: (4, 1), 6: (0"
     ", 1), 23: (0, 1), 25: (0, 1), 48: (27263232, 4)}, 'b': [(8455943, 1, 1), (8456284, 1109393"
     '408, 4), (8456280, 1101004800, 4)]})'),
    ("('002160B0', 'hub', {'pads': (0, 64, 0), 'ring': ([2, 0, 1, 2], 4), 't': {5: (4, 1), 6: (0"
     ", 1), 23: (0, 1), 25: (0, 1), 48: (27263232, 4)}, 'b': [(8455943, 0, 1), (8456284, 1109393"
     '408, 4), (8456280, 1101004800, 4)]})'),
    ("('002160B0', 'hub', {'pads': (0, 64, 0), 'ring': ([2, 2, 2, 2], 4), 't': {5: (1, 1), 23: ("
     "0, 1), 25: (0, 1)}, 'b': [(8455943, 0, 1), (8456284, 0, 4), (8456280, 1114374144, 4)], 'de"
     "v': (32,)})"),
    ("('002160B0', 'hub', {'pads': (0, 64, 0), 'ring': ([2, 2, 2, 2], 4), 't': {5: (1, 1), 23: ("
     "0, 1), 25: (0, 1)}, 'b': [(8455943, 0, 1), (8456284, 0, 4), (8456280, 1120403456, 4)], 'de"
     "v': (32,)})"),
    ("('002160B0', 'hub', {'pads': (0, 64, 0), 'ring': ([2, 2, 2, 2], 4), 't': {5: (1, 1), 23: ("
     "0, 1), 25: (0, 1)}, 'b': [(8455943, 0, 1), (8456284, 1092616192, 4), (8456280, 1120272384,"
     " 4)], 'dev': (32,)})"),
    ("('002160B0', 'hub', {'pads': (0, 64, 0), 'ring': ([3, 0, 1, 2], 4), 't': {5: (4, 1), 6: (0"
     ", 1), 23: (0, 1), 25: (0, 1), 48: (27263232, 4)}, 'b': [(8455943, 0, 1), (8456284, 0, 4), "
     '(8456280, 1117782016, 4)]})'),
    ("('002160B0', 'hub', {'pads': (0, 64, 0), 'ring': ([3, 0, 1, 2], 4), 't': {5: (4, 1), 6: (0"
     ", 1), 23: (0, 1), 25: (0, 1), 48: (27263232, 4)}, 'b': [(8455943, 0, 1), (8456284, 1109393"
     '408, 4), (8456280, 1101004800, 4)]})'),
    ("('002160B0', 'hub', {'pads': (0, 64, 0), 'ring': ([3, 0, 1, 2], 4), 't': {5: (4, 1), 6: (0"
     ", 1), 23: (0, 1), 25: (0, 1), 48: (27263232, 4)}, 'b': [(8455943, 1, 1), (8456284, 1092616"
     '192, 4), (8456280, 1110704128, 4)]})'),
    ("('002160B0', 'hub', {'pads': (0, 64, 0), 'ring': ([3, 0, 1, 2], 4), 't': {5: (4, 1), 6: (0"
     ", 1), 23: (0, 1), 25: (0, 1), 48: (27263232, 4)}, 'b': [(8455943, 1, 1), (8456284, 1109393"
     '408, 4), (8456280, 1101004800, 4)]})'),
    ("('002160B0', 'hub', {'pads': (0, 64, 0), 'ring': ([3, 3, 3, 3], 4), 't': {5: (1, 1), 23: ("
     "0, 1), 25: (0, 1)}, 'b': [(8455943, 0, 1), (8456284, 0, 4), (8456280, 1114374144, 4)]})"),
    ("('002160B0', 'hub', {'pads': (0, 64, 0), 'ring': ([3, 3, 3, 3], 4), 't': {5: (1, 1), 23: ("
     "0, 1), 25: (0, 1)}, 'b': [(8455943, 0, 1), (8456284, 1092616192, 4), (8456280, 1120272384,"
     ' 4)]})'),
    ("('002160B0', 'hub', {'pads': (0, 64, 0), 'ring': ([3, 3, 3, 3], 4), 't': {5: (1, 1), 23: ("
     "0, 1), 25: (0, 1)}, 'b': [(8455943, 1, 1), (8456284, 0, 4), (8456280, 1114374144, 4)]})"),
    ("('002160B0', 'hub', {'pads': (0, 64, 0), 'ring': ([3, 3, 3, 3], 4), 't': {5: (1, 1), 23: ("
     "0, 1), 25: (0, 1)}, 'b': [(8455943, 1, 1), (8456284, 1092616192, 4), (8456280, 1120272384,"
     ' 4)]})'),
    ("('002160B0', 'hub', {'pads': (0, 64, 0), 'ring': ([4, 0, 1, 2], 4), 't': {5: (4, 1), 6: (0"
     ", 1), 23: (0, 1), 25: (0, 1), 48: (27263232, 4)}, 'b': [(8455943, 0, 1), (8456284, 1109393"
     '408, 4), (8456280, 1101004800, 4)]})'),
    ("('002160B0', 'hub', {'pads': (0, 64, 0), 'ring': ([4, 0, 1, 2], 4), 't': {5: (4, 1), 6: (0"
     ", 1), 23: (0, 1), 25: (0, 1), 48: (27263232, 4)}, 'b': [(8455943, 1, 1), (8456284, 1109393"
     '408, 4), (8456280, 1101004800, 4)]})'),
    ("('002160B0', 'hub', {'pads': (0, 64, 0), 'ring': ([5, 0, 1, 2], 3), 't': {5: (4, 1), 6: (0"
     ", 1), 23: (0, 1), 25: (0, 1), 48: (27263232, 4)}, 'b': [(8455943, 0, 1), (8456284, 1109393"
     '408, 4), (8456280, 1101004800, 4)]})'),
    ("('002160B0', 'hub', {'pads': (0, 64, 4096), 'ring': ([0, 1, 2, 3, 4, 0, 1], 5), 't': {5: ("
     '1, 1), 23: (3, 1), 25: (0, 1)}})'),
    ("('002160B0', 'hub', {'pads': (0, 8192, 0), 'ring': ([0, 1], 2), 't': {5: (4, 1), 6: (0, 1)"
     '}})'),
    ("('002160B0', 'hub', {'pads': (0, 8192, 0), 'ring': ([0, 1], 2), 't': {5: (4, 1), 6: (1, 1)"
     '}})'),
    ("('002160B0', 'hub', {'pads': (0, 96, 0), 'ring': ([0, 1], 2), 't': {5: (4, 1), 6: (0, 1)}}"
     ')'),
    ("('002160B0', 'hub', {'ring': ([0, 1, 2, 3, 4], 5), 't': {5: (2, 1), 26: (1, 1), 28: (0, 2)"
     ', 25: (0, 1)}})'),
    ("('002160B0', 'hub', {'ring': ([0, 1, 2, 3, 4], 5), 't': {5: (2, 1), 26: (1, 1), 28: (44, 2"
     '), 25: (0, 1)}})'),
    ("('002160B0', 'hub', {'ring': ([2, 0, 1], 3), 't': {5: (4, 1), 6: (1, 1)}, 'b': [(16908976,"
     " 2, 1)], 's0': 8454448})"),
    ("('002160B0', 'hub', {'t': {5: (7, 1)}})"),
    ("('00217090', 'hub', {'t': {5: (0, 1)}, 'b': [(8457316, 0, 1), (8457317, 0, 1), (8457318, 0"
     ', 1), (8457319, 0, 1), (8457320, 0, 1), (8457321, 0, 1), (8457322, 0, 1), (8457323, 0, 1),'
     ' (8457324, 0, 1), (8457325, 0, 1), (8457326, 0, 1), (8457327, 0, 1), (8457328, 0, 1), (845'
     '7329, 0, 1), (8457330, 0, 1), (8457331, 0, 1), (8455856, 0, 1), (8457380, 2, 1), (8457382,'
     ' 1, 1), (8457313, 1, 1)]})'),
    ("('00217090', 'hub', {'t': {5: (0, 1)}, 'b': [(8457316, 0, 1), (8457317, 0, 1), (8457318, 0"
     ', 1), (8457319, 0, 1), (8457320, 0, 1), (8457321, 0, 1), (8457322, 0, 1), (8457323, 0, 1),'
     ' (8457324, 0, 1), (8457325, 0, 1), (8457326, 0, 1), (8457327, 0, 1), (8457328, 0, 1), (845'
     '7329, 0, 1), (8457330, 0, 1), (8457331, 0, 1), (8455856, 1, 1), (8455857, 6, 1), (8457380,'
     ' 255, 1)]})'),
    ("('00217090', 'hub', {'t': {5: (0, 1)}, 'b': [(8457316, 1, 1), (8457317, 1, 1), (8457318, 1"
     ', 1), (8457319, 1, 1), (8457320, 1, 1), (8457321, 1, 1), (8457322, 1, 1), (8457323, 1, 1),'
     ' (8457324, 1, 1), (8457325, 1, 1), (8457326, 1, 1), (8457327, 1, 1), (8457328, 1, 1), (845'
     '7329, 1, 1), (8457330, 1, 1), (8457331, 1, 1), (8455856, 1, 1), (8455857, 6, 1), (8457380,'
     ' 255, 1), (8457381, 255, 1), (8457382, 255, 1), (8457383, 255, 1)]})'),
    ("('00217090', 'hub', {'t': {5: (1, 1), 23: (0, 1), 25: (0, 1), 7: (0, 1), 18: (11, 1)}, 'pa"
     "ds': (0, 64, 0), 'ring': ([0, 0, 1], 3), 'b': [(8457381, 6, 1), (8457382, 1, 1), (8457383,"
     ' 8, 1), (8457313, 1, 1), (8457328, 1, 1), (8457329, 1, 1), (8457330, 1, 1)]})'),
    ("('00217090', 'hub', {'t': {5: (1, 1), 23: (0, 1), 25: (0, 1), 7: (3, 1), 18: (10, 1)}, 'pa"
     "ds': (0, 64, 0), 'ring': ([0, 0, 1], 3), 'b': [(8457381, 5, 1), (8457382, 0, 1), (8457383,"
     ' 7, 1), (8457313, 0, 1), (8457328, 1, 1), (8457329, 1, 1), (8457330, 0, 1)]})'),
    ("('00217090', 'hub', {'t': {5: (1, 1), 23: (0, 1), 7: (0, 1)}, 'pads': (0, 0, 0), 'ring': ("
     '[0, 1, 2, 3, 4], 5)})'),
    ("('00217090', 'hub', {'t': {5: (1, 1), 23: (0, 1), 7: (0, 1)}, 'pads': (0, 0, 4096), 'ring'"
     ': ([0, 1, 2, 3, 4], 5)})'),
    ("('00217090', 'hub', {'t': {5: (1, 1), 23: (0, 1), 7: (0, 1)}, 'pads': (0, 64, 0), 'ring': "
     '([0, 1, 2, 3, 4], 5)})'),
    ("('00217090', 'hub', {'t': {5: (2, 1), 26: (1, 1), 28: (0, 2)}, 'ring': ([0, 1, 2, 3, 4], 5"
     ')})'),
    ("('00217090', 'hub', {'t': {5: (2, 1), 26: (1, 1), 28: (44, 2)}, 'ring': ([0, 1, 2, 3, 4], "
     '5)})'),
    ("('00217090', 'hub', {'t': {5: (3, 1), 6: (1, 1)}, 'pads': (0, 0, 0), 'ring': ([0, 1], 2)})"),
    ("('00217090', 'hub', {'t': {5: (3, 1), 6: (2, 1)}, 'pads': (0, 0, 0), 'ring': ([0, 1], 2)})"),
    ("('00217090', 'hub', {'t': {5: (3, 1), 6: (2, 1)}, 'pads': (0, 32, 0), 'ring': ([0, 1], 2)}"
     ')'),
    ("('00217090', 'hub', {'t': {5: (4, 1), 6: (0, 1), 19: (11, 1), 23: (1, 1), 25: (0, 1), 30: "
     "(5, 2)}, 'pads': (0, 64, 0), 'ring': ([1, 2, 0], 3), 'b': [(8457380, 0, 1), (8457382, 255,"
     ' 1)]})'),
    ("('00217090', 'hub', {'t': {5: (4, 1), 6: (0, 1), 19: (11, 1), 23: (1, 1), 25: (0, 1), 30: "
     "(5, 2)}, 'pads': (0, 64, 0), 'ring': ([1, 2, 0], 3), 'b': [(8457380, 255, 1), (8457382, 1,"
     ' 1)]})'),
    ("('00217090', 'hub', {'t': {5: (4, 1), 6: (0, 1), 19: (12, 1), 23: (1, 1), 25: (0, 1), 30: "
     "(5, 2)}, 'pads': (0, 64, 0), 'ring': ([1, 2, 0], 3), 'b': [(8457380, 2, 1), (8457382, 1, 1"
     ')]})'),
    ("('00217090', 'hub', {'t': {5: (4, 1), 6: (0, 1), 19: (12, 1)}, 'pads': (0, 32, 0), 'ring':"
     ' ([1, 2], 2)})'),
    ("('00217090', 'hub', {'t': {5: (4, 1), 6: (0, 1), 19: (12, 1)}, 'pads': (0, 32768, 0), 'rin"
     "g': ([1, 2], 2)})"),
    ("('00217090', 'hub', {'t': {5: (4, 1), 6: (0, 1), 19: (12, 1)}, 'pads': (0, 8192, 0), 'ring"
     "': ([1, 2], 2)})"),
    ("('00217090', 'hub', {'t': {5: (4, 1), 6: (0, 1), 19: (13, 1), 23: (1, 1), 25: (0, 1), 30: "
     "(5, 2)}, 'pads': (0, 64, 0), 'ring': ([1, 2, 0], 3), 'b': [(8457380, 1, 1), (8457382, 2, 1"
     ')]})'),
    ("('00217090', 'hub', {'t': {5: (4, 1), 6: (1, 1), 19: (12, 1)}, 'pads': (0, 32768, 0), 'rin"
     "g': ([1, 2], 2)})"),
    ("('00217090', 'hub', {'t': {5: (4, 1), 6: (1, 1), 19: (12, 1)}, 'pads': (0, 64, 0), 'ring':"
     ' ([1, 2], 2)})'),
    ("('00217090', 'hub', {'t': {5: (4, 1), 6: (1, 1), 19: (12, 1)}, 'pads': (0, 8192, 0), 'ring"
     "': ([1, 2], 2)})"),
    ("('00217090', 'hub', {'t': {5: (5, 1), 7: (2, 1)}})"),
    ("('002177B0', 'hub', {'t': {5: (0, 1)}, 'b': [(8457316, 0, 1), (8457317, 0, 1), (8457318, 0"
     ', 1), (8457319, 0, 1), (8457320, 0, 1), (8457321, 0, 1), (8457322, 0, 1), (8457323, 0, 1),'
     ' (8457324, 0, 1), (8457325, 0, 1), (8457326, 0, 1), (8457327, 0, 1), (8457328, 0, 1), (845'
     '7329, 0, 1), (8457330, 0, 1), (8457331, 0, 1), (8455856, 0, 1), (8457380, 2, 1), (8457382,'
     ' 1, 1), (8457313, 1, 1)]})'),
    ("('002177B0', 'hub', {'t': {5: (0, 1)}, 'b': [(8457316, 0, 1), (8457317, 0, 1), (8457318, 0"
     ', 1), (8457319, 0, 1), (8457320, 0, 1), (8457321, 0, 1), (8457322, 0, 1), (8457323, 0, 1),'
     ' (8457324, 0, 1), (8457325, 0, 1), (8457326, 0, 1), (8457327, 0, 1), (8457328, 0, 1), (845'
     '7329, 0, 1), (8457330, 0, 1), (8457331, 0, 1), (8455856, 1, 1), (8455857, 12, 1), (8457380'
     ', 255, 1)]})'),
    ("('002177B0', 'hub', {'t': {5: (0, 1)}, 'b': [(8457316, 1, 1), (8457317, 1, 1), (8457318, 1"
     ', 1), (8457319, 1, 1), (8457320, 1, 1), (8457321, 1, 1), (8457322, 1, 1), (8457323, 1, 1),'
     ' (8457324, 1, 1), (8457325, 1, 1), (8457326, 1, 1), (8457327, 1, 1), (8457328, 1, 1), (845'
     '7329, 1, 1), (8457330, 1, 1), (8457331, 1, 1), (8455856, 1, 1), (8455857, 12, 1), (8457380'
     ', 255, 1), (8457381, 255, 1), (8457382, 255, 1), (8457383, 255, 1)]})'),
    ("('002177B0', 'hub', {'t': {5: (1, 1), 23: (0, 1), 25: (0, 1), 7: (0, 1), 18: (10, 1)}, 'pa"
     "ds': (0, 64, 0), 'ring': ([2, 0, 1], 3), 'b': [(8457381, 5, 1), (8457382, 0, 1), (8457383,"
     ' 7, 1), (8457313, 0, 1), (8457328, 1, 1), (8457329, 1, 1), (8457330, 0, 1)]})'),
    ("('002177B0', 'hub', {'t': {5: (1, 1), 23: (0, 1), 25: (0, 1), 7: (0, 1), 18: (11, 1)}, 'pa"
     "ds': (0, 64, 0), 'ring': ([2, 0, 1], 3), 'b': [(8457381, 6, 1), (8457382, 1, 1), (8457383,"
     ' 8, 1), (8457313, 1, 1), (8457328, 1, 1), (8457329, 1, 1), (8457330, 1, 1)]})'),
    ("('002177B0', 'hub', {'t': {5: (1, 1), 23: (0, 1), 25: (0, 1), 7: (0, 1), 18: (11, 1)}, 'pa"
     "ds': (0, 64, 0), 'ring': ([2], 1), 'b': [(8457328, 1, 1), (8457329, 0, 1), (8457330, 1, 1)"
     ']})'),
    ("('002177B0', 'hub', {'t': {5: (1, 1), 23: (0, 1), 25: (0, 1), 7: (3, 1), 18: (10, 1)}, 'pa"
     "ds': (0, 64, 0), 'ring': ([0, 0, 1], 3), 'b': [(8457381, 5, 1), (8457382, 0, 1), (8457383,"
     ' 7, 1), (8457313, 0, 1), (8457328, 1, 1), (8457329, 1, 1), (8457330, 0, 1)]})'),
    ("('002177B0', 'hub', {'t': {5: (1, 1), 23: (0, 1), 7: (0, 1)}, 'pads': (0, 0, 0), 'ring': ("
     '[0, 1, 2, 3, 4], 5)})'),
    ("('002177B0', 'hub', {'t': {5: (1, 1), 23: (0, 1), 7: (0, 1)}, 'pads': (0, 0, 4096), 'ring'"
     ': ([0, 1, 2, 3, 4], 5)})'),
    ("('002177B0', 'hub', {'t': {5: (2, 1), 26: (1, 1), 28: (0, 2)}, 'ring': ([0, 1, 2, 3, 4], 5"
     ')})'),
    ("('002177B0', 'hub', {'t': {5: (2, 1), 26: (1, 1), 28: (44, 2)}, 'ring': ([0, 1, 2, 3, 4], "
     '5)})'),
    ("('002177B0', 'hub', {'t': {5: (3, 1), 6: (1, 1)}, 'pads': (0, 0, 0), 'ring': ([0, 1], 2)})"),
    ("('002177B0', 'hub', {'t': {5: (3, 1), 6: (2, 1)}, 'pads': (0, 0, 0), 'ring': ([0, 1], 2)})"),
    ("('002177B0', 'hub', {'t': {5: (3, 1), 6: (2, 1)}, 'pads': (0, 32, 0), 'ring': ([0, 1], 2)}"
     ')'),
    ("('002177B0', 'hub', {'t': {5: (4, 1), 6: (0, 1), 19: (11, 1), 23: (1, 1), 25: (0, 1), 30: "
     "(10, 2)}, 'pads': (0, 64, 0), 'ring': ([1, 2, 0], 3), 'b': [(8457380, 0, 1), (8457382, 255"
     ', 1)]})'),
    ("('002177B0', 'hub', {'t': {5: (4, 1), 6: (0, 1), 19: (11, 1), 23: (1, 1), 25: (0, 1), 30: "
     "(10, 2)}, 'pads': (0, 64, 0), 'ring': ([1, 2, 0], 3), 'b': [(8457380, 255, 1), (8457382, 1"
     ', 1)]})'),
    ("('002177B0', 'hub', {'t': {5: (4, 1), 6: (0, 1), 19: (12, 1), 23: (1, 1), 25: (0, 1), 30: "
     "(10, 2)}, 'pads': (0, 64, 0), 'ring': ([1, 2, 0], 3), 'b': [(8457380, 2, 1), (8457382, 1, "
     '1)]})'),
    ("('002177B0', 'hub', {'t': {5: (4, 1), 6: (0, 1), 19: (12, 1)}, 'pads': (0, 32, 0), 'ring':"
     ' ([1, 2], 2)})'),
    ("('002177B0', 'hub', {'t': {5: (4, 1), 6: (0, 1), 19: (12, 1)}, 'pads': (0, 32768, 0), 'rin"
     "g': ([1, 2], 2)})"),
    ("('002177B0', 'hub', {'t': {5: (4, 1), 6: (0, 1), 19: (12, 1)}, 'pads': (0, 8192, 0), 'ring"
     "': ([1, 2], 2)})"),
    ("('002177B0', 'hub', {'t': {5: (4, 1), 6: (1, 1), 19: (12, 1)}, 'pads': (0, 32768, 0), 'rin"
     "g': ([1, 2], 2)})"),
    ("('002177B0', 'hub', {'t': {5: (4, 1), 6: (1, 1), 19: (12, 1)}, 'pads': (0, 64, 0), 'ring':"
     ' ([1, 2], 2)})'),
    ("('002177B0', 'hub', {'t': {5: (4, 1), 6: (1, 1), 19: (12, 1)}, 'pads': (0, 8192, 0), 'ring"
     "': ([1, 2], 2)})"),
    ("('002177B0', 'hub', {'t': {5: (5, 1), 7: (2, 1)}})"),
    ("('00217FA0', 'hub', {'t': {5: (0, 1)}, 'b': [(8457316, 0, 1), (8457317, 0, 1), (8457318, 0"
     ', 1), (8457319, 0, 1), (8457320, 0, 1), (8457321, 0, 1), (8457322, 0, 1), (8457323, 0, 1),'
     ' (8457324, 0, 1), (8457325, 0, 1), (8457326, 0, 1), (8457327, 0, 1), (8457328, 0, 1), (845'
     '7329, 0, 1), (8457330, 0, 1), (8457331, 0, 1), (8455856, 0, 1), (8457380, 2, 1), (8457382,'
     ' 1, 1), (8457313, 1, 1)]})'),
    ("('00217FA0', 'hub', {'t': {5: (0, 1)}, 'b': [(8457316, 1, 1), (8457317, 1, 1), (8457318, 1"
     ', 1), (8457319, 1, 1), (8457320, 1, 1), (8457321, 1, 1), (8457322, 1, 1), (8457323, 1, 1),'
     ' (8457324, 1, 1), (8457325, 1, 1), (8457326, 1, 1), (8457327, 1, 1), (8457328, 1, 1), (845'
     '7329, 1, 1), (8457330, 1, 1), (8457331, 1, 1), (8455856, 1, 1), (8455857, 14, 1), (8457380'
     ', 255, 1), (8457381, 255, 1), (8457382, 255, 1), (8457383, 255, 1)]})'),
    ("('00217FA0', 'hub', {'t': {5: (0, 1)}, 'b': [(8457316, 1, 1), (8457317, 1, 1), (8457318, 1"
     ', 1), (8457319, 1, 1), (8457320, 1, 1), (8457321, 1, 1), (8457322, 1, 1), (8457323, 1, 1),'
     ' (8457324, 1, 1), (8457325, 1, 1), (8457326, 1, 1), (8457327, 1, 1), (8457328, 1, 1), (845'
     '7329, 1, 1), (8457330, 1, 1), (8457331, 1, 1), (8455856, 1, 1), (8455857, 48, 1)]})'),
    ("('00217FA0', 'hub', {'t': {5: (1, 1), 23: (0, 1), 25: (0, 1), 7: (0, 1), 18: (11, 1)}, 'pa"
     "ds': (0, 64, 0), 'ring': ([0, 0, 1], 3), 'b': [(8457381, 6, 1), (8457382, 1, 1), (8457383,"
     ' 8, 1), (8457313, 1, 1), (8457328, 1, 1), (8457329, 1, 1), (8457330, 1, 1)]})'),
    ("('00217FA0', 'hub', {'t': {5: (1, 1), 23: (0, 1), 25: (0, 1), 7: (3, 1), 18: (10, 1)}, 'pa"
     "ds': (0, 64, 0), 'ring': ([0, 0, 1], 3), 'b': [(8457381, 5, 1), (8457382, 0, 1), (8457383,"
     ' 7, 1), (8457313, 0, 1), (8457328, 1, 1), (8457329, 1, 1), (8457330, 0, 1)]})'),
    ("('00217FA0', 'hub', {'t': {5: (1, 1), 23: (0, 1), 7: (0, 1)}, 'pads': (0, 0, 4096), 'ring'"
     ': ([0, 1, 2, 3, 4], 5)})'),
    ("('00217FA0', 'hub', {'t': {5: (1, 1), 23: (0, 1), 7: (0, 1)}, 'pads': (0, 64, 0), 'ring': "
     '([0, 1, 2, 3, 4], 5)})'),
    ("('00217FA0', 'hub', {'t': {5: (1, 1)}, 'pads': (0, 64, 0), 'ring': ([], 0)})"),
    ("('00217FA0', 'hub', {'t': {5: (2, 1), 26: (1, 1), 28: (0, 2)}, 'ring': ([0, 1, 2, 3, 4], 5"
     ')})'),
    ("('00217FA0', 'hub', {'t': {5: (2, 1), 26: (1, 1), 28: (44, 2)}, 'ring': ([0, 1, 2, 3, 4], "
     '5)})'),
    ("('00217FA0', 'hub', {'t': {5: (3, 1), 6: (1, 1)}, 'pads': (0, 0, 0), 'ring': ([0, 1], 2)})"),
    ("('00217FA0', 'hub', {'t': {5: (3, 1), 6: (2, 1)}, 'pads': (0, 0, 0), 'ring': ([0, 1], 2)})"),
    ("('00217FA0', 'hub', {'t': {5: (3, 1), 6: (2, 1)}, 'pads': (0, 32, 0), 'ring': ([0, 1], 2)}"
     ')'),
    ("('00217FA0', 'hub', {'t': {5: (4, 1), 6: (0, 1), 19: (11, 1), 23: (1, 1), 25: (0, 1), 30: "
     "(13, 2)}, 'pads': (0, 64, 0), 'ring': ([1, 2, 0], 3), 'b': [(8457380, 255, 1), (8457382, 1"
     ', 1)]})'),
    ("('00217FA0', 'hub', {'t': {5: (4, 1), 6: (0, 1), 19: (12, 1)}, 'pads': (0, 32, 0), 'ring':"
     ' ([1, 2], 2)})'),
    ("('00217FA0', 'hub', {'t': {5: (4, 1), 6: (0, 1), 19: (12, 1)}, 'pads': (0, 32768, 0), 'rin"
     "g': ([1, 2], 2)})"),
    ("('00217FA0', 'hub', {'t': {5: (4, 1), 6: (0, 1), 19: (12, 1)}, 'pads': (0, 8192, 0), 'ring"
     "': ([1, 2], 2)})"),
    ("('00217FA0', 'hub', {'t': {5: (4, 1), 6: (1, 1), 19: (12, 1)}, 'pads': (0, 32768, 0), 'rin"
     "g': ([1, 2], 2)})"),
    ("('00217FA0', 'hub', {'t': {5: (4, 1), 6: (1, 1), 19: (12, 1)}, 'pads': (0, 64, 0), 'ring':"
     ' ([1, 2], 2)})'),
    ("('00217FA0', 'hub', {'t': {5: (4, 1), 6: (1, 1), 19: (12, 1)}, 'pads': (0, 8192, 0), 'ring"
     "': ([1, 2], 2)})"),
    ("('00217FA0', 'hub', {'t': {5: (5, 1), 7: (2, 1)}})"),
    ("('00218640', 'hub', {'t': {5: (0, 1)}, 'b': [(8457316, 0, 1), (8457317, 0, 1), (8457318, 0"
     ', 1), (8457319, 0, 1), (8457320, 0, 1), (8457321, 0, 1), (8457322, 0, 1), (8457323, 0, 1),'
     ' (8457324, 0, 1), (8457325, 0, 1), (8457326, 0, 1), (8457327, 0, 1), (8457328, 0, 1), (845'
     '7329, 0, 1), (8457330, 0, 1), (8457331, 0, 1), (8455856, 0, 1), (8457380, 2, 1), (8457382,'
     ' 1, 1), (8457313, 1, 1)]})'),
    ("('00218640', 'hub', {'t': {5: (0, 1)}, 'b': [(8457316, 0, 1), (8457317, 0, 1), (8457318, 0"
     ', 1), (8457319, 0, 1), (8457320, 0, 1), (8457321, 0, 1), (8457322, 0, 1), (8457323, 0, 1),'
     ' (8457324, 0, 1), (8457325, 0, 1), (8457326, 0, 1), (8457327, 0, 1), (8457328, 0, 1), (845'
     '7329, 0, 1), (8457330, 0, 1), (8457331, 0, 1), (8455856, 1, 1), (8455857, 8, 1), (8457380,'
     ' 255, 1)]})'),
    ("('00218640', 'hub', {'t': {5: (0, 1)}, 'b': [(8457316, 1, 1), (8457317, 1, 1), (8457318, 1"
     ', 1), (8457319, 1, 1), (8457320, 1, 1), (8457321, 1, 1), (8457322, 1, 1), (8457323, 1, 1),'
     ' (8457324, 1, 1), (8457325, 1, 1), (8457326, 1, 1), (8457327, 1, 1), (8457328, 1, 1), (845'
     '7329, 1, 1), (8457330, 1, 1), (8457331, 1, 1), (8455856, 1, 1), (8455857, 8, 1), (8457380,'
     ' 255, 1), (8457381, 255, 1), (8457382, 255, 1), (8457383, 255, 1)]})'),
    ("('00218640', 'hub', {'t': {5: (1, 1), 23: (0, 1), 25: (0, 1), 7: (0, 1), 18: (11, 1)}, 'pa"
     "ds': (0, 64, 0), 'ring': ([0, 0, 1], 3), 'b': [(8457381, 6, 1), (8457382, 1, 1), (8457383,"
     ' 8, 1), (8457313, 1, 1), (8457328, 1, 1), (8457329, 1, 1), (8457330, 1, 1)]})'),
    ("('00218640', 'hub', {'t': {5: (1, 1), 23: (0, 1), 25: (0, 1), 7: (3, 1), 18: (10, 1)}, 'pa"
     "ds': (0, 64, 0), 'ring': ([0, 0, 1], 3), 'b': [(8457381, 5, 1), (8457382, 0, 1), (8457383,"
     ' 7, 1), (8457313, 0, 1), (8457328, 1, 1), (8457329, 1, 1), (8457330, 0, 1)]})'),
    ("('00218640', 'hub', {'t': {5: (1, 1), 23: (0, 1), 7: (0, 1)}, 'pads': (0, 0, 0), 'ring': ("
     '[0, 1, 2, 3, 4], 5)})'),
    ("('00218640', 'hub', {'t': {5: (1, 1), 23: (0, 1), 7: (0, 1)}, 'pads': (0, 0, 4096), 'ring'"
     ': ([0, 1, 2, 3, 4], 5)})'),
    ("('00218640', 'hub', {'t': {5: (1, 1), 23: (0, 1), 7: (0, 1)}, 'pads': (0, 64, 0), 'ring': "
     '([0, 1, 2, 3, 4], 5)})'),
    ("('00218640', 'hub', {'t': {5: (2, 1), 26: (1, 1), 28: (44, 2)}, 'ring': ([0, 1, 2, 3, 4], "
     '5)})'),
    ("('00218640', 'hub', {'t': {5: (2, 1), 26: (2, 1), 28: (0, 2)}, 'ring': ([0, 1, 2, 3, 4], 5"
     ')})'),
    ("('00218640', 'hub', {'t': {5: (3, 1), 6: (1, 1)}, 'pads': (0, 0, 0), 'ring': ([0, 1], 2)})"),
    ("('00218640', 'hub', {'t': {5: (3, 1), 6: (2, 1)}, 'pads': (0, 0, 0), 'ring': ([0, 1], 2)})"),
    ("('00218640', 'hub', {'t': {5: (3, 1), 6: (2, 1)}, 'pads': (0, 32, 0), 'ring': ([0, 1], 2)}"
     ')'),
    ("('00218640', 'hub', {'t': {5: (4, 1), 6: (0, 1), 19: (11, 1), 23: (1, 1), 25: (0, 1), 30: "
     "(7, 2)}, 'pads': (0, 64, 0), 'ring': ([1, 2, 0], 3), 'b': [(8457380, 0, 1), (8457382, 255,"
     ' 1)]})'),
    ("('00218640', 'hub', {'t': {5: (4, 1), 6: (0, 1), 19: (11, 1), 23: (1, 1), 25: (0, 1), 30: "
     "(7, 2)}, 'pads': (0, 64, 0), 'ring': ([1, 2, 0], 3), 'b': [(8457380, 255, 1), (8457382, 1,"
     ' 1)]})'),
    ("('00218640', 'hub', {'t': {5: (4, 1), 6: (0, 1), 19: (12, 1), 23: (1, 1), 25: (0, 1), 30: "
     "(7, 2)}, 'pads': (0, 64, 0), 'ring': ([1, 2, 0], 3), 'b': [(8457380, 2, 1), (8457382, 1, 1"
     ')]})'),
    ("('00218640', 'hub', {'t': {5: (4, 1), 6: (0, 1), 19: (12, 1)}, 'pads': (0, 32, 0), 'ring':"
     ' ([1, 2], 2)})'),
    ("('00218640', 'hub', {'t': {5: (4, 1), 6: (0, 1), 19: (12, 1)}, 'pads': (0, 32768, 0), 'rin"
     "g': ([1, 2], 2)})"),
    ("('00218640', 'hub', {'t': {5: (4, 1), 6: (0, 1), 19: (12, 1)}, 'pads': (0, 8192, 0), 'ring"
     "': ([1, 2], 2)})"),
    ("('00218640', 'hub', {'t': {5: (4, 1), 6: (0, 1), 19: (13, 1), 23: (1, 1), 25: (0, 1), 30: "
     "(7, 2)}, 'pads': (0, 64, 0), 'ring': ([1, 2, 0], 3), 'b': [(8457380, 1, 1), (8457382, 2, 1"
     ')]})'),
    ("('00218640', 'hub', {'t': {5: (4, 1), 6: (1, 1), 19: (12, 1)}, 'pads': (0, 32768, 0), 'rin"
     "g': ([1, 2], 2)})"),
    ("('00218640', 'hub', {'t': {5: (4, 1), 6: (1, 1), 19: (12, 1)}, 'pads': (0, 64, 0), 'ring':"
     ' ([1, 2], 2)})'),
    ("('00218640', 'hub', {'t': {5: (4, 1), 6: (1, 1), 19: (12, 1)}, 'pads': (0, 8192, 0), 'ring"
     "': ([1, 2], 2)})"),
    ("('00218640', 'hub', {'t': {5: (5, 1), 7: (2, 1)}})"),
    ("('00218D90', 'hub', {'t': {5: (0, 1)}, 'b': [(8457316, 0, 1), (8457317, 0, 1), (8457318, 0"
     ', 1), (8457319, 0, 1), (8457320, 0, 1), (8457321, 0, 1), (8457322, 0, 1), (8457323, 0, 1),'
     ' (8457324, 0, 1), (8457325, 0, 1), (8457326, 0, 1), (8457327, 0, 1), (8457328, 0, 1), (845'
     '7329, 0, 1), (8457330, 0, 1), (8457331, 0, 1), (8455856, 0, 1), (8457380, 2, 1), (8457382,'
     ' 1, 1), (8457313, 1, 1)]})'),
    ("('00218D90', 'hub', {'t': {5: (0, 1)}, 'b': [(8457316, 0, 1), (8457317, 0, 1), (8457318, 0"
     ', 1), (8457319, 0, 1), (8457320, 0, 1), (8457321, 0, 1), (8457322, 0, 1), (8457323, 0, 1),'
     ' (8457324, 0, 1), (8457325, 0, 1), (8457326, 0, 1), (8457327, 0, 1), (8457328, 0, 1), (845'
     '7329, 0, 1), (8457330, 0, 1), (8457331, 0, 1), (8455856, 1, 1), (8455857, 2, 1), (8457380,'
     ' 255, 1)]})'),
    ("('00218D90', 'hub', {'t': {5: (0, 1)}, 'b': [(8457316, 0, 1), (8457317, 0, 1), (8457318, 1"
     ', 1), (8457319, 0, 1), (8457320, 0, 1), (8457321, 0, 1), (8457322, 0, 1), (8457323, 0, 1),'
     ' (8457324, 0, 1), (8457325, 0, 1), (8457326, 0, 1), (8457327, 0, 1), (8457328, 0, 1), (845'
     '7329, 0, 1), (8457330, 0, 1), (8457331, 0, 1), (8455856, 0, 1), (8457380, 2, 1), (8457382,'
     ' 1, 1), (8457313, 1, 1)]})'),
    ("('00218D90', 'hub', {'t': {5: (0, 1)}, 'b': [(8457316, 1, 1), (8457317, 1, 1), (8457318, 0"
     ', 1), (8457319, 1, 1), (8457320, 0, 1), (8457321, 0, 1), (8457322, 0, 1), (8457323, 0, 1),'
     ' (8457324, 0, 1), (8457325, 0, 1), (8457326, 0, 1), (8457327, 0, 1), (8457328, 0, 1), (845'
     '7329, 0, 1), (8457330, 0, 1), (8457331, 0, 1), (8455856, 0, 1), (8457380, 2, 1), (8457382,'
     ' 1, 1), (8457313, 1, 1)]})'),
    ("('00218D90', 'hub', {'t': {5: (0, 1)}, 'b': [(8457316, 1, 1), (8457317, 1, 1), (8457318, 1"
     ', 1), (8457319, 1, 1), (8457320, 1, 1), (8457321, 1, 1), (8457322, 1, 1), (8457323, 1, 1),'
     ' (8457324, 1, 1), (8457325, 1, 1), (8457326, 1, 1), (8457327, 1, 1), (8457328, 1, 1), (845'
     '7329, 1, 1), (8457330, 1, 1), (8457331, 1, 1), (8455856, 1, 1), (8455857, 2, 1), (8457380,'
     ' 255, 1), (8457381, 255, 1), (8457382, 255, 1), (8457383, 255, 1)]})'),
    ("('00218D90', 'hub', {'t': {5: (1, 1), 23: (0, 1), 25: (0, 1), 7: (0, 1), 18: (10, 1)}, 'pa"
     "ds': (0, 64, 0), 'ring': ([4, 0, 1], 3), 'b': [(8457381, 5, 1), (8457382, 0, 1), (8457383,"
     ' 7, 1), (8457313, 0, 1), (8457328, 1, 1), (8457329, 1, 1), (8457330, 0, 1)]})'),
    ("('00218D90', 'hub', {'t': {5: (1, 1), 23: (0, 1), 25: (0, 1), 7: (0, 1), 18: (11, 1)}, 'pa"
     "ds': (0, 64, 0), 'ring': ([1, 0, 1], 3), 'b': [(8457381, 6, 1), (8457382, 1, 1), (8457383,"
     ' 8, 1), (8457313, 1, 1), (8457328, 1, 1), (8457329, 1, 1), (8457330, 1, 1)]})'),
    ("('00218D90', 'hub', {'t': {5: (1, 1), 23: (0, 1), 25: (0, 1), 7: (3, 1), 18: (10, 1)}, 'pa"
     "ds': (0, 64, 0), 'ring': ([0, 0, 1], 3), 'b': [(8457381, 5, 1), (8457382, 0, 1), (8457383,"
     ' 7, 1), (8457313, 0, 1), (8457328, 1, 1), (8457329, 1, 1), (8457330, 0, 1)]})'),
    ("('00218D90', 'hub', {'t': {5: (1, 1), 23: (3, 1), 7: (0, 1)}, 'pads': (0, 0, 16384), 'ring"
     "': ([0, 1, 2, 3, 4], 5)})"),
    ("('00218D90', 'hub', {'t': {5: (2, 1), 26: (1, 1), 28: (0, 2)}, 'ring': ([0, 1, 2, 3, 4], 5"
     ')})'),
    ("('00218D90', 'hub', {'t': {5: (2, 1), 26: (2, 1), 28: (65488, 2)}, 'ring': ([0, 1, 2, 3, 4"
     '], 5)})'),
    ("('00218D90', 'hub', {'t': {5: (3, 1), 6: (1, 1)}, 'pads': (0, 0, 0), 'ring': ([0, 1], 2)})"),
    ("('00218D90', 'hub', {'t': {5: (3, 1), 6: (2, 1), 23: (0, 1), 25: (0, 1)}, 'ring': ([1, 1],"
     ' 2)})'),
    ("('00218D90', 'hub', {'t': {5: (3, 1), 6: (2, 1), 23: (0, 1), 25: (0, 1)}, 'ring': ([2, 1],"
     ' 2)})'),
    ("('00218D90', 'hub', {'t': {5: (3, 1), 6: (2, 1), 23: (0, 1), 25: (0, 1)}, 'ring': ([3, 1],"
     ' 2)})'),
    ("('00218D90', 'hub', {'t': {5: (3, 1), 6: (2, 1), 23: (0, 1), 25: (0, 1)}, 'ring': ([4, 1],"
     ' 2)})'),
    ("('00218D90', 'hub', {'t': {5: (3, 1), 6: (2, 1)}, 'pads': (0, 32, 0), 'ring': ([0, 1], 2)}"
     ')'),
    ("('00218D90', 'hub', {'t': {5: (4, 1), 6: (0, 1), 19: (11, 1), 23: (1, 1), 25: (0, 1), 30: "
     "(0, 2)}, 'pads': (0, 64, 0), 'ring': ([1, 2, 0], 3), 'b': [(8457380, 0, 1), (8457382, 255,"
     ' 1)]})'),
    ("('00218D90', 'hub', {'t': {5: (4, 1), 6: (0, 1), 19: (11, 1), 23: (1, 1), 25: (0, 1), 30: "
     "(0, 2)}, 'pads': (0, 64, 0), 'ring': ([1, 2, 0], 3), 'b': [(8457380, 255, 1), (8457382, 1,"
     ' 1)]})'),
    ("('00218D90', 'hub', {'t': {5: (4, 1), 6: (0, 1), 19: (12, 1), 23: (1, 1), 25: (0, 1), 30: "
     "(0, 2)}, 'pads': (0, 64, 0), 'ring': ([1, 2, 0], 3), 'b': [(8457380, 2, 1), (8457382, 1, 1"
     ')]})'),
    ("('00218D90', 'hub', {'t': {5: (4, 1), 6: (0, 1), 19: (12, 1)}, 'pads': (0, 32, 0), 'ring':"
     ' ([1, 2], 2)})'),
    ("('00218D90', 'hub', {'t': {5: (4, 1), 6: (0, 1), 19: (12, 1)}, 'pads': (0, 32768, 0), 'rin"
     "g': ([1, 2], 2)})"),
    ("('00218D90', 'hub', {'t': {5: (4, 1), 6: (0, 1), 19: (12, 1)}, 'pads': (0, 8192, 0), 'ring"
     "': ([1, 2], 2)})"),
    ("('00218D90', 'hub', {'t': {5: (4, 1), 6: (1, 1), 19: (12, 1)}, 'pads': (0, 32768, 0), 'rin"
     "g': ([1, 2], 2)})"),
    ("('00218D90', 'hub', {'t': {5: (4, 1), 6: (1, 1), 19: (12, 1)}, 'pads': (0, 64, 0), 'ring':"
     ' ([1, 2], 2)})'),
    ("('00218D90', 'hub', {'t': {5: (4, 1), 6: (1, 1), 19: (12, 1)}, 'pads': (0, 8192, 0), 'ring"
     "': ([1, 2], 2)})"),
    ("('00218D90', 'hub', {'t': {5: (4, 1), 6: (2, 1), 23: (0, 1), 25: (0, 1)}, 'ring': ([0, 1],"
     ' 2)})'),
    ("('00218D90', 'hub', {'t': {5: (4, 1), 6: (2, 1), 23: (0, 1), 25: (0, 1)}, 'ring': ([3, 1],"
     ' 2)})'),
    ("('00218D90', 'hub', {'t': {5: (4, 1), 6: (2, 1), 23: (0, 1), 25: (0, 1)}, 'ring': ([4, 1],"
     ' 2)})'),
    ("('00218D90', 'hub', {'t': {5: (5, 1), 7: (2, 1)}})"),
}


# Data conditions the mutation check needed beyond the outcome cover (a
# boundary value or a nonzero operand the cover's items do not hold).
EXTRA_PINS = {
    repr(('002117D0', 'hub', {'map': 0, 'floor': 0, 'vec': (F(40.0), F(3.0), F(-12.0), F(1.0)),
                              'b': [(0x810154, F(4.5), 4), (0x810158, F(10.0), 4), (0x81015C, F(-20.0), 4)]})),
}


# Cases pinned to kill the review's surviving mutants (docs/STATUS_PAGES.md
# section 3, "Mutation check"): each holds the data condition a
# single-operation change needs to show, run in the default and the full
# run alike. group -> [(case, what it pins)].
SURVIVOR_PINS = {
    'MAP / DATABASE helpers': [
        (('00213A00', 'hub', {'a1': 0, 'pads': (0, 0, 0x4000), 'b': ring_bytes(ring(20, 0), 20, 10, 6)}),
         '00213A00 down move from cursor 6 (the < 7 bound)'),
        (('00213CC0', 'hub', {'b': ring_bytes(ring(20, -11), 9, 0, 5, 2, -11)}),
         '00213CC0 d = -1 from -11: t+0x1C reaches -12 (the < -11 bound)'),
        (('001FCF30', 'hub', {'args': (0, 0x64, 0x2F), 'bank': {0x14: 0x40}}),
         '001FCF30 bank header +0x14 nonzero: only word +0x10 is added'),
    ],
    'ITEM children and list helpers': [
        (('002160B0', 'hub', {'pads': (0, 0, 0), 'spad': {0x70003B64: 10},
                              't': {5: (5, 1), 0x34: (F(50.0), 4), 0x3C: (2, 2), 6: (0, 1)},
                              'b': [(0x810858, F(40.0), 4)]}),
         '002160B0 state 5 on main-loop frame 10 (the % 10 cue)'),
        (('002160B0', 'hub', {'pads': (0, 0x40, 0), 'ring': ([0, 1, 2, 3, 4], 5),
                              'b': flags(0x810C82, 0x1F) + [(0x810858, F(20.0), 4)],
                              't': {5: (4, 1), 6: (0, 1), 0x17: (4, 1), 0x19: (0, 1)}}),
         '002160B0 Yes consuming the last kind 4: the rebuilt list has 4 entries, cursor 4 (the n < 4 clamp)'),
    ],
    'SPR4 page, helpers and part pages': [
        (('0020BF20', 'hub', {'mode': 2, 'sel': 0x14, 'spad': {0x70003B64: 0},
                              'b': counters(12, 3, (1, 5), 2) + [(0x810CA6, 4, 1), (0x810CA4, 2, 1)]}),
         '0020BF20 mode 2 sel 0x14 on a dark blink frame (the secondary row test)'),
        (('00211970', 'hub', {'t': {4: (2, 1), 0x15: (3, 1)}}),
         '00211970 state 2 hover 3: MULTI, module 0x30'),
        (('002177B0', 'hub', {'t': {5: (1, 1), 0x17: (0, 1), 0x19: (0, 1), 7: (0, 1), 0x12: (0xB, 1)},
                              'pads': (0, 0x40, 0), 'ring': ([4], 1),
                              'b': [(0x810C70, 1, 1), (0x810C71, 1, 1), (0x810C72, 1, 1)]}),
         '002177B0 Cross on entry 4 (c = 2, the c < 3 gate) with C70..C72 set'),
        (('002177B0', 'hub', {'t': {5: (0, 1)}, 'b': flags(0x810C64, 0xFFFF, 16) +
                              [(0x8106B0, 1, 1), (0x8106B1, 0xE, 1)]}),
         '002177B0 take of B1 0xE with all five owned: entry 4 at list index 4 (page 4, row 0)'),
        (('00218D90', 'hub', {'t': {5: (1, 1), 0x17: (0, 1), 0x19: (0, 1), 7: (0, 1)}, 'pads': (0, 0, 0),
                              'ring': ([4, 0, 1], 3), 'b': counters(12, 3, (1, 5), 2)}),
         '00218D90 panel on entry 4 with counter rows 1..3 nonzero (selection 3)'),
        (('002177B0', 'hub', {'t': {5: (1, 1), 0x17: (0, 1), 0x19: (0, 1), 7: (0, 1)}, 'pads': (0, 0, 0),
                              'ring': ([2, 0, 1], 3), 'b': counters(12, 3, (1, 5), 2)}),
         '002177B0 panel on entry 2 with row 4 (D_00810CB0) nonzero (selection 4)'),
    ],
}


def sel(items, count, seed, axes=(), everything=False):
    if everything:
        return list(items)
    extra = {repr(it) for it in items if it[0] == '002177B0' and it[1] == 'hub' and it[2].get('ring') == ([1, 0, 1], 3)
             and it[2]['t'].get(0x12) == (0xA, 1) and it[2]['t'].get(7) == (0, 1)}
    extra |= {repr(it) for it in items if it[0] == '00213CC0' and it[1] == 'hub'
             and dict((a, v) for a, v, _ in it[2]['b']).get(T + 0x1C) in (10, 0xFFF6)
             and dict((a, v) for a, v, _ in it[2]['b']).get(T + 0x1A) in (1, 2)
             and dict((a, v) for a, v, _ in it[2]['b']).get(T + 0x18) == 9}
    return RM.select(items, count, seed, axes=axes,
                     keep=lambda i, it: repr(it) in QUICK_PINS or repr(it) in EXTRA_PINS or repr(it) in extra)


def cover_item(arg):
    fn, it = arg
    r = fn(it)
    return set() if r in ('unmeasured', 'skipped') else {o for o in r[0] if o[0] in LANE_PC}


def cover():
    """Run every lane item and print the greedy set reaching every outcome
    (paste into QUICK_PINS)."""
    global POOL
    setup()
    POOL = multiprocessing.get_context('fork').Pool(JOBS)
    rows = []
    try:
        for name, items, total, fn in case_groups(everything=True):
            if name.startswith('reuse'):
                continue
            outs = pmap(cover_item, [(fn, it) for it in items])
            rows += [(repr(it), o) for it, o in zip(items, outs)]
    finally:
        R.close_pool(POOL)
        POOL = None
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


def with_pins(name, chosen):
    have = {repr(it) for it in chosen}
    return list(chosen) + [it for it, _ in SURVIVOR_PINS.get(name, ()) if repr(it) not in have]


def total_with_pins(name, items):
    have = {repr(it) for it in items}
    return len(items) + sum(repr(it) not in have for it, _ in SURVIVOR_PINS.get(name, ()))


def case_groups(everything=False):
    groups = []
    hl = helper_items()
    groups.append(('MAP / DATABASE helpers',
                   with_pins('MAP / DATABASE helpers', sel(hl, 0, 41, axes=(lambda it: it[0],), everything=everything)),
                   total_with_pins('MAP / DATABASE helpers', hl), helper_case))
    it = item_items()
    groups.append(('ITEM children and list helpers',
                   with_pins('ITEM children and list helpers',
                             sel(it, 0, 43, axes=(lambda x: x[0],), everything=everything)),
                   total_with_pins('ITEM children and list helpers', it), item_case))
    sp = spr4_items()
    groups.append(('SPR4 page, helpers and part pages',
                   with_pins('SPR4 page, helpers and part pages',
                             sel(sp, 0, 44, axes=(lambda x: x[0],), everything=everything)),
                   total_with_pins('SPR4 page, helpers and part pages', sp), spr4_case))
    ru = reuse_items()
    groups.append(('reuse: AREA01 MAP / DATABASE on AREA11',
                   sel(ru, 4, 42, axes=(lambda it: it[0], lambda it: it[1][1]), everything=everything),
                   len(ru), reuse_case))
    return groups


def setup():
    global ELF, NATIVE
    ELF = read_elf()
    NATIVE = build_native()
    install()
    R.TRACKER = R.build_tracker()
    load_images()
    check_code(EE(ELF))
    got = callee_set()
    missing = sorted(hex(a) for a in got if a not in WSPEC and a not in U.INLINE)
    assert not missing, ('callees without a spec', missing)


def main():
    global POOL
    t0 = time.time()
    setup()
    faults = fault_cases()
    if JOBS > 1 and 'fork' in multiprocessing.get_all_start_methods():
        POOL = multiprocessing.get_context('fork').Pool(JOBS)
    outcomes, cases, calls, entries, skipped, unmeasured = set(), 0, 0, 0, 0, 0
    counts = []
    try:
        only = os.environ.get('EM_SP_GROUPS', '')
        for name, items, total, fn in case_groups():
            if only and not any(k in name for k in only.split(',')):
                continue
            tg = time.time()
            before = cases
            for r in pmap(fn, items):
                if r == 'skipped':
                    skipped += 1
                    continue
                cases += 1
                if r == 'unmeasured':
                    unmeasured += 1
                    continue
                outcomes.update(r[0])
                calls += r[1]
                entries += r[2]
            if os.environ.get('EM_SP_TIMES'):
                print('%-40s %4d cases %.1f s' % (name, len(items), time.time() - tg), flush=True)
            counts.append(RM.part(cases - before, total, name + ' cases'))
    finally:
        if POOL is not None:
            R.close_pool(POOL)
            POOL = None
    branches = all_branches()
    need = {(pc, t) for pc in branches for t in (True, False)}
    got = {o for o in outcomes if o[0] in LANE_PC}
    missing = sorted((hex(pc), t) for pc, t in need - got if (pc, t) not in UNREACHED)
    RM.banner(*counts, f'{cases} cases ({unmeasured} refused on both sides, {skipped} skipped)',
              f'{calls} callee calls, {entries} entries with RAM + scratchpad + arguments equal to the original\'s',
              f'{len(branches)} conditional branches: {len(need & got)} of {len(need)} outcomes seen, '
              f'{len(UNREACHED)} unreachable by construction',
              f'{faults} fault cases')
    if (RM.FULL and not os.environ.get('EM_SP_GROUPS')) or os.environ.get('EM_SP_REQUIRE_COVER'):
        assert not missing, ('branch outcomes never seen', missing)
    elif missing:
        print('outcomes not in this sample (EM_TEST_FULL=1 requires all):', len(missing))
    if missing and os.environ.get('EM_SP_SHOW_MISSING'):
        print('missing:', missing)
    print('elapsed %.1f s' % (time.time() - t0))
    print('status pages reference: PASS')


def debug(names):
    setup()
    table = {'helper': (helper_items(), helper_case), 'reuse': (reuse_items(), reuse_case),
             'item': (item_items(), item_case), 'spr4': (spr4_items(), spr4_case)}
    for name in names:
        group, _, idx = name.partition(':')
        items, fn = table[group]
        chosen = items if not idx else [items[int(i)] for i in idx.split(',')]
        for it in chosen:
            t = time.time()
            try:
                r = fn(it)
                print('ok', group, repr(it)[:140], 'calls', r[1] if r not in ('unmeasured', 'skipped') else r,
                      '%.2fs' % (time.time() - t))
            except AssertionError as ex:
                print('FAIL', group, repr(it)[:140], repr(ex)[:2000])


# ======================================================================
# Bounded mutation check (`mutants` command; not part of the default run)
# ======================================================================

H, I, SP4, PT = ('src/game/em_status_pages_helpers.c', 'src/game/em_status_pages_item.c',
                 'src/game/em_status_pages_spr4.c', 'src/game/em_status_pages_parts.c')
MUTANTS = [   # (name, file, text, replacement): one operation each
    ('3CC0_bound', H, 'if (!(ui_lh(s, t + 0x1Cu) < 12)) {', 'if (!(ui_lh(s, t + 0x1Cu) < 11)) {'),
    ('17D0_scale', H, 'm = ui_fmul(0x3DAEC33Eu,', 'm = ui_fmul(0x3DAEC33Fu,'),
    ('BC50_step', I, "const int32_t d = ui_lbu(s, t + 0x1Au) == 1 ? 4 : -4;",
     "const int32_t d = ui_lbu(s, t + 0x1Au) == 1 ? 4 : -3;"),
    ('60B0_s0', I, 'const uint32_t probe = s0 + t;', 'const uint32_t probe = s0 + t + 1u;'),
    ('60B0_cap60', I, 'if (!sp_clt(lvl, F_60)) ui_sw(s, t + 0x34u, F_60);',
     'if (!sp_clt(lvl, F_100)) ui_sw(s, t + 0x34u, F_60);'),
    ('5870_dev', I, 'ui_sb(s, dev + 0xBu, 5);\n            ui_sb(s, 0x70003B8Du, 3);\n            ui_sb(s, SP_D',
     'ui_sb(s, dev + 0xAu, 5);\n            ui_sb(s, 0x70003B8Du, 3);\n            ui_sb(s, SP_D'),
    ('BF20_row2', SP4, 'n = ui_lh(s, 0x00810CAEu) + ui_lh(s, 0x00810CACu) * 100;',
     'n = ui_lh(s, 0x00810CAEu) + ui_lh(s, 0x00810CACu) * 10;'),
    ('1970_line', SP4, 'static const uint8_t line[7] = {0, 5, 3, 1, 4, 0, 2};',
     'static const uint8_t line[7] = {0, 5, 3, 1, 4, 2, 0};'),
    ('77B0_gate', PT, 'want = full == 7u ? 0xCu : ui_lbu(s, t + 0x12u);',
     'want = full == 0u ? 0xCu : ui_lbu(s, t + 0x12u);'),
    ('77B0_else', PT, 'uint32_t want = k + 0xAu;', 'uint32_t want = ui_lbu(s, t + 0x12u);'),
    # the review's survivors (SURVIVOR_PINS kill them)
    ('3A00_7', H, '(int32_t)c < 7', '(int32_t)c < 6'),
    ('3CC0_m11', H, 'ui_lh(s, t + 0x1Cu) < -11', 'ui_lh(s, t + 0x1Cu) < -12'),
    ('CF30_10', H, 'ui_lw(s, p + 0x10u)', 'ui_lw(s, p + 0x14u)'),
    ('60B0_tenth', I, '(int32_t)ui_lw(s, UI_SPAD_3B64) % 10 == 0', '(int32_t)ui_lw(s, UI_SPAD_3B64) % 9 == 0'),
    ('60B0_clamp', I, 'if (n < 4 &&', 'if (n < 5 &&'),
    ('BF20_13', SP4, '(uint32_t)(sel - 0x13) < 2u', '(uint32_t)(sel - 0x13) < 1u'),
    ('1970_mod', SP4, '{0, 0, 0x2E, 0x30,', '{0, 0, 0x2E, 0x31,'),
    ('panel4', PT, 'k == 4 ? 3', 'k == 4 ? 2'),
    ('panel2', PT, '(int32_t)k < 2 ? 0 : 4', '(int32_t)k < 3 ? 0 : 4'),
    ('multi3', PT, 'if (c < 3u)', 'if (c < 2u)'),
    ('multilocate', PT, 't, 0xA, multi_init, 1,', 't, 0xA, multi_init, 0,'),
]


def mutants():
    """Build each mutant, run the quick groups and report killed / survived."""
    global NATIVE, POOL
    setup()
    mdir = OUT / 'mut'
    mdir.mkdir(parents=True, exist_ok=True)
    rows = []
    only = set(sys.argv[2:])
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
        U.NATIVE = NATIVE
        POOL = multiprocessing.get_context('fork').Pool(JOBS)
        verdict = 'survived'
        try:
            for gname, items, total, fn in case_groups():
                try:
                    pmap(fn, items)
                except AssertionError as ex:
                    verdict = 'killed by ' + gname + ': ' + repr(ex)[:120]
                    break
        finally:
            R.close_pool(POOL)
            POOL = None
        rows.append((name, verdict))
        print('%-12s %s' % (name, verdict), flush=True)
    return rows


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == 'debug':
        debug(sys.argv[2:])
    elif len(sys.argv) > 1 and sys.argv[1] == 'cover':
        cover()
    elif len(sys.argv) > 1 and sys.argv[1] == 'mutants':
        mutants()
    else:
        main()
