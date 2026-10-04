#!/usr/bin/env python3
"""Execute the original AREA01 math / actor-anim / anim-runtime helpers and
compare the native translations byte for byte (docs/AREA01_MATH.md).

Modules under test (built privately into build/area01/math/):
  src/game/em_area01_math_core.c    EE storage view + callee dispatcher
  src/game/em_area01_math_actor.c   001B13F0 001B2140 001C25E0 001C2770
                                    001C39F0 001C3BE0 001C3D60 001C69A0
  src/game/em_area01_math_owner.c   001BB860 001BB560 001C02E0 001BF630
                                    001BFFD0 001CB360 001B9CF0 001BBAE0
                                    001BBBF0 001D0C80 001D0D40
  src/game/em_area01_math_player.c  00183250 00187DE0 00187EC0

The oracle. The user's pinned ELF and the AREA01 route captures
(../Extermination/build/s87/route_a01/<beat>/eeMemory.bin + scratchpad.bin)
supply every instruction and every byte; nothing original is embedded here.
The interpreter is the shared EE core with the measured float model
(test_player_fall_reference.FallEE: every COP1 and VU0 macro op through
tools/ee_float_model.py). The function under test runs as original code;
each call it makes to a routine outside the translated set is a recorded
boundary: the original callee runs unmodified, and its argument registers,
the argument registers it reads before writing them, its v0 / f0 (only
those it writes) and every RAM / scratchpad byte it writes are recorded,
with the position of the call in the log of all stores. The native routine
runs over a copy of the same memory. At EVERY call its dispatcher checks,
in order: the target; every argument register the native passes against
the original's; that the native passes every argument register the callee
read; and the native side's whole RAM and scratchpad against the oracle's
memory at the entry of the same call (rebuilt from the store log). Only
then does it apply the recorded writes and return the recorded results
(a result register the callee never wrote comes back as a marker). After
the routine, the whole RAM and scratchpad are compared again, plus the
return value, the number of calls and the absence of a native fault.

"Whole memory" is compared through the ranges either side stored: both
copies start from the same bytes, the oracle's copy then changes only at its
logged stores and the native copy only at its traced stores (the view's
store trace) and the replayed callee writes, so bytes outside those ranges
are equal on both sides. The first comparison on each image compares all
32 MiB byte for byte (the shared-start invariant); EM_A01_FULLMEM=1
compares all 32 MiB at every call entry and at the end.

Register inputs. The oracle records which argument registers a callee
reads before writing (conservatively: every register access). A callee
also runs a second time from the same entry memory with every other
register poisoned (all other GPRs and all upper halves, other FPRs, VU0
vf1..vf31 / ACC / Q, the COP1 ACC and condition, HI / LO); any change in
its writes or in the v0 / f0 it sets fails the case. That makes memory plus
the argument registers it read its complete input. Full mode poisons every
call; quick mode the first POISON_QUICK calls of each callee per routine
per job. A callee touching the caller's stack frame fails the case.

Case sources:
  route    the original owners of the captures run as original code over
           every AREA01 beat (the live pool nodes whose behaviour is
           00128C10, 0012A5D0, 001BB860, 001C02E0, 001BFFD0, ...); each entry
           into a routine under test is caught with the whole machine state
           and compared from there.
  perturb  the same caught entries with their state / argument fields
           varied (phases, sub-states, flags, distances, timers, area
           bytes), so both outcomes of the routines' conditions run.
  unit     direct calls over the capture RAM (the script-op routines and the
           exhaustive 001B2140 table sweep).
Scripted boundaries (Python answers recorded and replayed like callees)
supply answers and writes the route does not reach. The marker STOP ends
a case at that call's entry (a prefix comparison: target, arguments and
whole memory at the entry, nothing after it).
Quick (default) run: three beats; every route catch compared as caught;
the pinned ('!') perturbations once per routine (its first catch on the
first quick beat); the deterministic 001C2770 latch_cases; covering subsets
of the unit grids; the native view contract (core_contract). About 15 s of
CPU (docs/AREA01_MATH.md has the measurements). EM_TEST_FULL=1 runs every
beat, every perturbation at every catch (the random ones included), the
random 001C2770 probe selection and the full grids and sweeps.
"""
import ctypes as C
import os
import random
import re
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode as RM  # noqa: E402
import test_player_fall_reference as FR  # noqa: E402
from test_player_slide_reference import EE, RETURN, read_elf  # noqa: E402

MASK = 0xFFFFFFFF
DECOMP = ROOT.parent / 'Extermination'
BEATS = DECOMP / 'build/s87/route_a01'
LANE = ROOT / 'build/area01/math'
SOURCES = ['src/game/em_area01_math_core.c', 'src/game/em_area01_math_actor.c',
           'src/game/em_area01_math_owner.c', 'src/game/em_area01_math_player.c']
ALL_BEATS = ('a01_00_train_room', 'a01_01_tunnel', 'a01_02_shaft_landing', 'a01_03_shaft_locked',
             'a01_04_return_north', 'a01_05_npc_bridge_talk', 'a01_06_return_south', 'a01_07_level_exit',
             'a01_s0_npc_first_talk', 'a01_s1_sentry_doc', 'a01_s2_control_room_items', 'a01_s3_fire_contact')
QUICK_BEATS = ('a01_00_train_room', 'a01_03_shaft_locked', 'a01_07_level_exit')
POOL_HEAD, STACK_LO, STACK_HI = 0x275BC0, 0x7F000000, 0x7F100000


class Spec:
    """A translated routine: native name, original range(s), the argument
    registers it takes (a0.. then f12..), whether it returns v0, and the
    translated routines it calls directly (run inline by the oracle)."""

    def __init__(self, name, size, ints, floats=0, v0=False, inline=(), extra=()):
        self.name, self.size, self.ints, self.floats, self.v0 = name, size, ints, floats, v0
        self.inline, self.extra = tuple(inline), tuple(extra)


SPECS = {
    0x1B13F0: Spec('em_area01_math_001B13F0', 0x7C, 2, 1, v0=True),
    0x1B2140: Spec('em_area01_math_001B2140', 0x9C4, 1, v0=True),
    0x1C25E0: Spec('em_area01_math_001C25E0', 0xA8, 2),
    0x1C2770: Spec('em_area01_math_001C2770', 0x1278, 3, v0=True, inline=(0x1C39F0, 0x1C3BE0)),
    0x1C39F0: Spec('em_area01_math_001C39F0', 0x1E8, 2, 1),
    0x1C3BE0: Spec('em_area01_math_001C3BE0', 0x180, 2),
    0x1C3D60: Spec('em_area01_math_001C3D60', 0x44, 2, inline=(0x1C3BE0,)),
    0x1C69A0: Spec('em_area01_math_001C69A0', 0x3F8, 1),
    0x1BB860: Spec('em_area01_math_001BB860', 0x274, 1, inline=(0x1BB560,)),
    0x1BB560: Spec('em_area01_math_001BB560', 0x260, 3, v0=True),
    0x1C02E0: Spec('em_area01_math_001C02E0', 0x3F8, 1, inline=(0x1BF630, 0x1D0C80, 0x1D0D40)),
    0x1BF630: Spec('em_area01_math_001BF630', 0x7C, 3, v0=True),
    0x1BFFD0: Spec('em_area01_math_001BFFD0', 0x308, 1),
    0x1CB360: Spec('em_area01_math_001CB360', 0x54, 1),
    0x1B9CF0: Spec('em_area01_math_001B9CF0', 0x2FC, 3, v0=True),
    0x1BBAE0: Spec('em_area01_math_001BBAE0', 0x110, 2, v0=True),
    0x1BBBF0: Spec('em_area01_math_001BBBF0', 0x12C, 1, v0=True),
    0x183250: Spec('em_area01_math_00183250', 0x1A0, 1),
    0x187DE0: Spec('em_area01_math_00187DE0', 0xB4, 1),
    0x187EC0: Spec('em_area01_math_00187EC0', 0x20, 2),
    0x1D0C80: Spec('em_area01_math_001D0C80', 0xBC, 2, v0=True),
    0x1D0D40: Spec('em_area01_math_001D0D40', 0x20, 4),
}


# ======================================================================
# Native build and binding
# ======================================================================

CALL = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.POINTER(C.c_uint32), C.c_uint,
                   C.POINTER(C.c_uint32), C.c_uint, C.POINTER(C.c_uint32), C.POINTER(C.c_uint32))


class Mem(C.Structure):
    _fields_ = [('ram', C.c_void_p), ('ram_size', C.c_uint32), ('spad', C.c_void_p), ('call', CALL),
                ('ctx', C.c_void_p), ('fault_address', C.c_uint32), ('fault_code', C.c_int32),
                ('stores', C.c_uint32), ('trace', C.POINTER(C.c_uint32)), ('trace_cap', C.c_uint32),
                ('trace_len', C.c_uint32)]


TRACE_CAP = 1 << 18
# EM_A01_FULLMEM=1 compares all 32 MiB + the scratchpad at every call entry
# instead of the dirty ranges (the validation of the dirty-range proof).
FULLMEM = os.environ.get('EM_A01_FULLMEM', '') not in ('', '0')


def ram_offset(address):
    """(is scratchpad, byte offset) of an EE address, as both the
    interpreter and the native view map it (RAM and its mirrors)."""
    if 0x70000000 <= address < 0x70004000:
        return True, address - 0x70000000
    return False, address & 0x1FFFFFF


def build_native():
    """Build the lane modules privately. EM_A01M_SRC names another source
    root (a scratch copy for mutation runs) and EM_A01M_LIB another build
    directory; both default to this checkout."""
    src = Path(os.environ.get('EM_A01M_SRC', ROOT))
    lane = Path(os.environ.get('EM_A01M_LIB', LANE))
    lane.mkdir(parents=True, exist_ok=True)
    lib = lane / ('em_area01_math' + ('.dylib' if sys.platform == 'darwin' else '.so'))
    sources = [s for s in SOURCES if (src / s).exists()]
    newest = max((src / s).stat().st_mtime for s in sources + ['src/game/em_area01_math_core.h',
                                                                 'src/game/em_ee_float.h'])
    if not lib.exists() or lib.stat().st_mtime < newest or any(
            (src / s.replace('.c', '.h')).stat().st_mtime > lib.stat().st_mtime for s in sources):
        command = ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wconversion', '-Wshadow',
                   '-ffp-contract=off', '-shared', '-fPIC', '-I' + str(src / 'src'),
                   *[str(src / s) for s in sources], '-o', str(lib)]
        subprocess.run(command, cwd=ROOT, check=True)
    return C.CDLL(str(lib))


def check_store_rule():
    """The whole-memory compare (section 3 of docs/AREA01_MATH.md) relies
    on every native store going through the traced em_a01m_sw / _sh / _sb.
    Fail when a translation source touches the storage another way: the
    view's ram / spad fields, the (read-only) em_a01m_where, or a cast that
    would drop its const."""
    src = Path(os.environ.get('EM_A01M_SRC', ROOT))
    bad = re.compile(r'(->|\.)\s*(ram|spad)\b|em_a01m_where|\(\s*uint8_t\s*\*\s*\)')
    for name in SOURCES[1:]:
        for i, line in enumerate((src / name).read_text().splitlines(), 1):
            code = line.split('//')[0]
            if bad.search(code) and not code.lstrip().startswith(('*', '/*')):
                raise AssertionError(f'{name}:{i}: storage touched outside the traced stores: {line.strip()}')


LIB = None
ELF = None


def native_fn(spec):
    fn = getattr(LIB, spec.name)
    fn.restype = C.c_int
    fn.argtypes = ([C.POINTER(Mem)] + [C.c_uint32] * (spec.ints + spec.floats)
                   + ([C.POINTER(C.c_uint32)] if spec.v0 else []))
    return fn


# ======================================================================
# The oracle
# ======================================================================

class Call:
    __slots__ = ('target', 'a', 'f', 'wide', 'v0', 'f0', 'writes', 'at', 'rread', 'fread', 'sp', 'stop')


class StopOracle(Exception):
    """The oracle reached a boundary scripted as STOP."""


# A scripted boundary that ends the case at that call's entry (a prefix
# comparison): the native side must reach the same call with the same
# target, arguments and whole memory; the dispatcher then stops it and
# nothing after the call is compared. Used where the original's execution
# past that call is not defined in the harness (an address outside RAM) or
# not needed (a 128-bone loop whose first call already separates a
# mutant).
STOP = object()


# Registers kept (not poisoned) in the poisoned re-run of a callee: zero,
# gp (a constant), sp (the base of its own frame) and ra (the return).
KEEP_R = frozenset({0, 28, 29, 31})
UNSET = 0x7FBADBAD        # handed to the native side for a v0 / f0 the callee never wrote
# The poisoned re-run of callees: every call in full mode; in quick mode
# the first POISON_QUICK calls of each callee per routine per job.
# EM_A01_NOPOISON=1 skips it (timing only), EM_A01_POISON_ALL=1 forces all.
POISON = os.environ.get('EM_A01_NOPOISON', '') in ('', '0')
POISON_ALL = RM.FULL or os.environ.get('EM_A01_POISON_ALL', '') not in ('', '0')
POISON_QUICK = 2
POISONED = {}             # (routine, callee) -> poisoned re-runs made in this job


class Track(list):
    """A register file recording which registers are read before they are
    written while a callee runs: those are its register inputs."""
    __slots__ = ('rd', 'wr')

    def __init__(self, values):
        super().__init__(values)
        self.rd, self.wr = set(), set()

    def __getitem__(self, i):
        if i not in self.wr:
            self.rd.add(i)
        return list.__getitem__(self, i)

    def __setitem__(self, i, v):
        self.wr.add(i)
        list.__setitem__(self, i, v)


class A01Base(FR.FallEE):
    """FallEE plus the MMI word interleaves PEXTLW (MMI0 sub 0x12: rd words
    = rt.w0, rs.w0, rt.w1, rs.w1) and PEXTUW (MMI1 sub 0x12: rt.w2, rs.w2,
    rt.w3, rs.w3), which the SDK matrix routines 00102798 / 001027E0 use;
    integer operations, no float model."""

    def mmi(self, word, pc):
        fn, sub = word & 63, word >> 6 & 31
        if fn in (0x08, 0x28) and sub == 0x12:
            rs, rt, rd = word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
            if fn == 0x08:
                a, b = self.r[rs] & 0xFFFFFFFFFFFFFFFF, self.r[rt] & 0xFFFFFFFFFFFFFFFF
            else:
                a, b = self.rh[rs] & 0xFFFFFFFFFFFFFFFF, self.rh[rt] & 0xFFFFFFFFFFFFFFFF
            lo = (b & MASK) | ((a & MASK) << 32)
            hi = (b >> 32) | ((a >> 32) << 32)
            if rd:
                self.r[rd], self.rh[rd] = lo, hi
            return
        super().mmi(word, pc)


class CatchError(Exception):
    """A comparison failing inside a route catch (never taken for an owner
    that merely leaves the interpreter)."""


class A01EE(A01Base):
    """A01Base running one routine under test with its outside calls as
    recorded boundaries (callees run as original code).

    Each boundary records, besides the callee's arguments, results and
    writes: `at`, the length of the store log at the call's entry (so the
    comparison can rebuild the oracle's whole memory at that entry), and the
    argument registers a0..a7 / f12..f19 the callee reads before writing
    them (Track; conservative: every access the interpreter makes counts).
    A callee that touches the caller's stack frame fails the case (a native
    caller has no EE stack to pass it). Unless EM_A01_NOPOISON is set, each
    original callee then runs a second time from the same entry memory with
    every register poisoned except zero / gp / sp / ra and the argument
    registers it read: all other GPRs (all 128 bits), the upper 96 bits of
    the argument GPRs, every other FPR, VU0 vf1..vf31 / ACC / Q, the COP1
    ACC and condition, HI / LO. Any difference in its v0, f0 or RAM /
    scratchpad writes fails the case, so the callee's inputs are exactly
    memory (compared at the call's entry) plus those argument registers
    (compared by the dispatcher)."""

    def __init__(self, elf, ram, spad):
        super().__init__(elf, ram, spad)
        self.ranges, self.entries = [], set()
        self.capture = None
        self.depth = 0
        self.calls = []
        self.outcomes = set()
        self.written = []
        self.wlog = []            # every non-stack store in order, at every depth
        self.quiet = False        # the poisoned re-run: its stores are not logged
        self.frame = 1 << 32      # stack addresses >= this belong to the caller
        self.script = {}
        self.rng = random.Random(0xA01C)

    def _where(self, address):
        address &= MASK
        if 0x70000000 <= address < 0x70004000:
            return self.spad, address - 0x70000000
        if STACK_LO <= address < STACK_HI:
            if address >= self.frame:
                raise AssertionError(('callee touched the caller stack frame', hex(address)))
            return self.stack, address - STACK_LO
        if address < 0x40000000:
            return self.mem, address & 0x1FFFFFF
        raise AssertionError(('address', hex(address)))

    def save(self, address, value, size=4):
        address &= MASK
        value &= (1 << (8 * size)) - 1
        if self.capture is not None:
            self.capture.append((address, value, size, EE.load(self, address, size)))
        if not self.quiet:
            self.written.append((address, size))
            if not STACK_LO <= address < STACK_HI:
                self.wlog.append((address, value, size))
        EE.save(self, address, value, size)

    def _callee(self, target, scripted):
        if scripted is not None:
            scripted(self)       # a scripted boundary: its results and writes are recorded the same way
        else:
            EE.run(self, target)

    def _poison(self, keep_r, keep_f):
        rng = self.rng
        self.r = [self.r[i] if i in keep_r else rng.getrandbits(64) for i in range(32)]
        self.rh = [0] + [rng.getrandbits(64) for _ in range(31)]
        self.f = [self.f[i] if i in keep_f else rng.getrandbits(32) for i in range(32)]
        self.vf = [list(self.vf[0])] + [[rng.getrandbits(32) for _ in range(4)] for _ in range(31)]
        self.vacc = [rng.getrandbits(32) for _ in range(4)]
        self.acc, self.q = rng.getrandbits(32), rng.getrandbits(32)
        self.hi, self.lo = FR.sx32(rng.getrandbits(32)), FR.sx32(rng.getrandbits(32))
        self.cond = not self.cond

    def boundary(self, target):
        target &= MASK
        rec = Call()
        rec.target = target
        rec.a = [self.r[4 + i] & MASK for i in range(8)]
        rec.wide = {4 + i for i in range(8) if self.r[4 + i] & 0xFFFFFFFFFFFFFFFF
                    != FR.sx32(self.r[4 + i] & MASK) & 0xFFFFFFFFFFFFFFFF}
        rec.f = [self.f[12 + i] & MASK for i in range(8)]
        rec.at = len(self.wlog)
        rec.stop = False
        saved_ra, sp = self.r[31], self.r[29] & MASK
        rec.sp = sp
        scripted = self.script.get(target)
        if scripted is STOP:
            rec.stop, rec.rread, rec.fread, rec.writes, rec.v0, rec.f0 = True, set(), set(), [], UNSET, UNSET
            self.calls.append(rec)
            raise StopOracle()
        self.r[31] = RETURN
        entry = regs_of(self)
        self.capture = []
        self.depth += 1
        self.frame = sp
        self.r, self.f = Track(self.r), Track(self.f)
        self._callee(target, scripted)
        rec.rread = {i for i in self.r.rd if 4 <= i < 12}
        rec.fread = {i for i in self.f.rd if 12 <= i < 20}
        sets_v0, sets_f0 = 2 in self.r.wr, 0 in self.f.wr
        self.r, self.f = list(self.r), list(self.f)
        writes = self.capture
        key = (self.ranges[0][0], target)
        if POISON and scripted is None and (POISON_ALL or POISONED.get(key, 0) < POISON_QUICK):
            POISONED[key] = POISONED.get(key, 0) + 1
            after = regs_of(self)
            for address, _, size, old in reversed(writes):
                EE.save(self, address, old, size)
            (self.r, self.rh, self.hi, self.lo, self.f, self.acc, self.cond, self.vf, self.vacc,
             self.q) = [list(v) if isinstance(v, list) else v for v in entry]
            self.vf = [list(v) for v in entry[7]]
            self._poison(KEEP_R | rec.rread, rec.fread)
            self.capture, self.quiet = [], True
            try:
                self._callee(target, None)
                fault = None
            except AssertionError as ex:
                fault = str(ex)[:120]
            again, self.quiet = self.capture, False
            plain = lambda ws: [w[:3] for w in ws if not STACK_LO <= w[0] < STACK_HI]
            if (fault or plain(again) != plain(writes)
                    or (sets_v0 and (self.r[2] & MASK) != (after[0][2] & MASK))
                    or (sets_f0 and (self.f[0] & MASK) != (after[4][0] & MASK))):
                raise AssertionError(('callee depends on register state the native side does not pass',
                                      hex(target), fault))
            (self.r, self.rh, self.hi, self.lo, self.f, self.acc, self.cond, self.vf, self.vacc,
             self.q) = [list(v) if isinstance(v, list) else v for v in after]
            self.vf = [list(v) for v in after[7]]
        self.frame = 1 << 32
        self.depth -= 1
        self.capture = None
        rec.writes = [w[:3] for w in writes if not STACK_LO <= w[0] < STACK_HI]
        # A result register the callee did not write holds whatever the
        # caller left there; the native side gets a marker instead, so a
        # translation that used it would differ.
        rec.v0 = self.r[2] & MASK if sets_v0 else UNSET
        rec.f0 = self.f[0] & MASK if sets_f0 else UNSET
        self.r[31] = saved_ra
        self.calls.append(rec)

    def inside(self, pc):
        return any(start <= pc < end for start, end in self.ranges)

    def branch(self, word, pc):
        b = EE.branch(self, word, pc)
        if b is not None and self.depth == 0 and self.inside(pc):
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            if not (op == 4 and rs == 0 and rt == 0):
                self.outcomes.add((pc, b[0]))
        return b

    def run_f(self, pc):
        load = self.load
        steps = 0
        while pc != RETURN:
            assert self.inside(pc), ('left the translated code', hex(pc))
            steps += 1
            assert steps < 5_000_000, ('step limit', hex(pc))
            word = load(pc)
            op = word >> 26
            if op in (2, 3):
                target = (pc & 0xF0000000) | ((word & 0x3FFFFFF) << 2)
                if op == 3: self.r[31] = pc + 8
                self.execute(load(pc + 4), pc + 4)
                if target in self.entries or (op == 2 and self.inside(target)):
                    pc = target
                    continue
                self.boundary(target)            # jal: back to pc + 8; j: a tail call
                pc = self.r[31] & MASK
                continue
            if op == 0 and word & 63 in (8, 9):
                target = self.r[word >> 21 & 31] & MASK
                if word & 63 == 9:
                    rd = word >> 11 & 31
                    if rd: self.r[rd] = pc + 8
                self.execute(load(pc + 4), pc + 4)
                if word & 63 == 8 and (target == RETURN or self.inside(target)):
                    pc = target
                    continue
                if target in self.entries:
                    pc = target
                    continue
                self.boundary(target)
                pc = self.r[31] & MASK
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


# ======================================================================
# One comparison
# ======================================================================

class Mismatch(AssertionError):
    pass


STATS = {}      # entry -> new_stats()


def new_stats():
    """cases, branch outcomes, case sources, boundary calls, and per callee
    the (native a / f registers passed, a / f registers read) pairs seen."""
    return dict(cases=0, outcomes=set(), sources=set(), calls=0, regs={})


def regs_of(ee):
    return (list(ee.r), list(ee.rh), ee.hi, ee.lo, list(ee.f), ee.acc, ee.cond,
            [list(v) for v in ee.vf], list(ee.vacc), ee.q)


class Images:
    """One pristine RAM / scratchpad state and three working copies: the
    oracle's, the native side's, and `b`, which the comparison advances
    through the oracle's store log to hold the oracle's whole memory at the
    entry of each boundary call. Each comparison edits and runs the copies,
    then restores the bytes it touched from the pristine state, so one set
    of 32 MiB copies serves every variant of a catch."""

    def __init__(self, ram, spad):
        self.ram, self.spad = bytes(ram), bytes(spad)
        self.oram, self.nram, self.bram = bytearray(self.ram), bytearray(self.ram), bytearray(self.ram)
        self.ospad, self.nspad, self.bspad = bytearray(self.spad), bytearray(self.spad), bytearray(self.spad)
        self.touched = set()
        self.checked = False      # the first comparison on this image compares all of RAM

    def edit(self, address, value, size):
        data = (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'little')
        if 0x70000000 <= address < 0x70004000:
            at = address - 0x70000000
            self.ospad[at:at + size] = data
            self.nspad[at:at + size] = data
            self.bspad[at:at + size] = data
        else:
            at = address & 0x1FFFFFF
            self.oram[at:at + size] = data
            self.nram[at:at + size] = data
            self.bram[at:at + size] = data
            self.touched.add((at, size))

    def restore(self):
        for at, size in self.touched:
            self.oram[at:at + size] = self.ram[at:at + size]
            self.nram[at:at + size] = self.ram[at:at + size]
            self.bram[at:at + size] = self.ram[at:at + size]
        self.touched.clear()
        self.ospad[:] = self.spad
        self.nspad[:] = self.spad
        self.bspad[:] = self.spad


def put_bytes(ram, spad, address, value, size):
    """Store `size` bytes at an EE address into a RAM / scratchpad pair."""
    data = value.to_bytes(size, 'little')
    if 0x70000000 <= address < 0x70004000:
        spad[address - 0x70000000:address - 0x70000000 + size] = data
    else:
        at = address & 0x1FFFFFF
        ram[at:at + size] = data


def first_difference(a, b, base):
    """The EE address of the first byte where a and b differ."""
    step = 1 << 16
    for i in range(0, len(a), step):
        if a[i:i + step] != b[i:i + step]:
            for j in range(i, min(len(a), i + step)):
                if a[j] != b[j]:
                    return f'{base + j:#x} oracle {b[j]:#04x} native {a[j]:#04x}'
    return None


def u32(ram, address):
    return struct.unpack_from('<I', ram, address & 0x1FFFFFF)[0]


def compare(elf, entry, img, regs, where, args=None, edits=(), script=None):
    """Run `entry` as original and natively from img + regs, with `edits`
    ((EE address, value, size)) applied to both copies first; args overrides
    the argument registers (ints then floats). Both copies are restored
    afterwards when they matched."""
    spec = SPECS[entry]
    for address, value, size in edits:
        img.edit(address, value, size)
    ee = A01EE(elf, b'\0', b'\0')
    ee.mem, ee.spad = img.oram, img.ospad
    ee.script = script or {}
    r, rh, hi, lo, f, acc, cond, vf, vacc, q = regs
    ee.r, ee.rh, ee.hi, ee.lo, ee.f, ee.acc, ee.cond = list(r), list(rh), hi, lo, list(f), acc, cond
    ee.vf, ee.vacc, ee.q = [list(v) for v in vf], list(vacc), q
    ints = [ee.r[4 + i] & MASK for i in range(spec.ints)]
    floats = [ee.f[12 + i] & MASK for i in range(spec.floats)]
    if args is not None:
        ints, floats = list(args[:spec.ints]), list(args[spec.ints:])
        for i, v in enumerate(ints): ee.r[4 + i] = FR.sx32(v)
        for i, v in enumerate(floats): ee.f[12 + i] = v & MASK
    ee.ranges = [(entry, entry + spec.size)] + [(e, e + SPECS[e].size) for e in spec.inline]
    ee.entries = set(spec.inline)
    for start, end in spec.extra:
        ee.ranges.append((start, end))
    ee.r[31] = RETURN
    if not STACK_LO <= (ee.r[29] & MASK) < STACK_HI:
        ee.r[29] = FR.shared.STACK_TOP
    ee.log_all = True
    try:
        ee.run_f(entry)
        stopped = False
    except StopOracle:
        stopped = True
    ret = ee.r[2] & MASK
    for address, size in ee.written:
        if not (0x70000000 <= address < 0x70004000 or STACK_LO <= address < STACK_HI):
            img.touched.add((address & 0x1FFFFFF, size))

    nram, nspad = img.nram, img.nspad
    bram, bspad = img.bram, img.bspad
    log = ee.wlog
    queue = list(ee.calls)
    errors = []
    state = {'index': 0, 'applied': 0, 'traced': 0, 'overflow': False, 'stopped': False}
    dirty = set()
    tbuf = (C.c_uint32 * TRACE_CAP)()
    trace = None
    audit = STATS.setdefault(entry, new_stats())['regs']

    def dispatch(_ctx, target, a, na, f, nf, v0, f0):
        k = state['index']
        state['index'] += 1
        if not queue:
            errors.append(('extra native call', k, hex(target)))
            return -1
        rec = queue.pop(0)
        got_a = [a[i] for i in range(na)]
        got_f = [f[i] for i in range(nf)]
        # 1. the callee and every argument register the native side passes.
        if rec.target != target or got_a != rec.a[:na] or got_f != rec.f[:nf]:
            errors.append(('call differs', k, hex(rec.target), [hex(x) for x in rec.a[:na]],
                           [hex(x) for x in rec.f[:nf]], 'native', hex(target), [hex(x) for x in got_a],
                           [hex(x) for x in got_f]))
            return -1
        # 2. every register the callee reads before writing it is passed
        # (not known for a STOP boundary, which never runs).
        need_a = {i - 4 for i in rec.rread if 4 <= i < 12}
        need_f = {i - 12 for i in rec.fread if 12 <= i < 20}
        if not rec.stop and (any(i >= na for i in need_a) or any(i >= nf for i in need_f) or
                             any(4 + i in rec.wide for i in need_a)):
            errors.append(('callee reads a register the native call does not pass', k, hex(target),
                           'a', sorted(need_a), 'f', sorted(need_f), 'wide', sorted(rec.wide),
                           'native passes', na, nf))
            return -1
        if not rec.stop:
            audit.setdefault(target, set()).add((na, nf, max(need_a, default=-1) + 1,
                                                 max(need_f, default=-1) + 1))
        # 3. the whole memory at the call's entry: the oracle's RAM and
        # scratchpad at that entry (the pristine state + the oracle's stores
        # before the call, in `b`) against the native side's. Both started
        # from the same bytes; since then the oracle's copy changed only at
        # its logged stores and the native copy only at its traced stores and
        # the replayed callee writes, so comparing those ranges compares the
        # whole memory (EM_A01_FULLMEM=1 compares every byte instead).
        for address, value, size in log[state['applied']:rec.at]:
            put_bytes(bram, bspad, address, value, size)
            dirty.add(ram_offset(address) + (size,))
        state['applied'] = rec.at
        n = trace.trace_len
        if n == 0xFFFFFFFF:
            state['overflow'] = True
        else:
            for i in range(state['traced'], n, 2):
                dirty.add(ram_offset(tbuf[i]) + (tbuf[i + 1],))
            state['traced'] = n
        if FULLMEM or state['overflow'] or not img.checked:
            differs = nspad != bspad or nram != bram
        else:
            differs = any((nspad if sp_ else nram)[at:at + size] != (bspad if sp_ else bram)[at:at + size]
                          for sp_, at, size in dirty)
        if differs:
            where_ = first_difference(nspad, bspad, 0x70000000) or first_difference(nram, bram, 0)
            errors.append(('memory differs at call entry', k, hex(target), where_))
            return -1
        if rec.stop:            # the prefix ends here: stop the native side
            state['stopped'] = True
            return -1
        # 4. the callee's recorded writes and results.
        for address, value, size in rec.writes:
            put_bytes(nram, nspad, address, value, size)
            dirty.add(ram_offset(address) + (size,))
        v0[0] = rec.v0
        f0[0] = rec.f0
        return 0

    cb = CALL(dispatch)
    ram_buf = (C.c_uint8 * len(nram)).from_buffer(nram)
    spad_buf = (C.c_uint8 * len(nspad)).from_buffer(nspad)
    mem = Mem(C.cast(ram_buf, C.c_void_p), len(nram), C.cast(spad_buf, C.c_void_p), cb, None, 0, 0, 0,
              C.cast(tbuf, C.POINTER(C.c_uint32)), TRACE_CAP, 0)
    trace = mem
    out = C.c_uint32(0xDEADBEEF)
    status = native_fn(spec)(C.byref(mem), *ints, *floats, *([C.byref(out)] if spec.v0 else []))
    del ram_buf, spad_buf
    if errors:
        raise Mismatch(where, hex(entry), errors[0])
    if queue:
        raise Mismatch(where, hex(entry), 'native skipped calls', [hex(c.target) for c in queue])
    if stopped:
        # A prefix case: the native side reached the STOP call with the same
        # target, arguments and memory (checked above) and was stopped there.
        if not state['stopped']:
            raise Mismatch(where, hex(entry), 'native did not reach the STOP call')
        st = STATS[entry]
        st['cases'] += 1
        st['outcomes'] |= ee.outcomes
        st['sources'].add(where.split(':')[0])
        st['calls'] += len(ee.calls)
        img.restore()
        return None, ee
    if status != 0:
        raise Mismatch(where, hex(entry), 'native fault', status)
    if spec.v0 and out.value != ret:
        raise Mismatch(where, hex(entry), 'v0', hex(ret), hex(out.value))
    # The memory after the last store: all of the scratchpad, and all of RAM
    # by the same argument as at the call entries (the oracle's copy changed
    # only at its logged stores, the native copy only at its traced stores
    # and the replayed writes; both started from the same bytes). With
    # EM_A01_FULLMEM=1, or a trace overflow, all 32 MiB are compared.
    if nspad != ee.spad:
        raise Mismatch(where, hex(entry), 'scratchpad differs', first_difference(nspad, ee.spad, 0x70000000))
    if trace.trace_len == 0xFFFFFFFF:
        state['overflow'] = True
    else:
        for i in range(state['traced'], trace.trace_len, 2):
            dirty.add(ram_offset(tbuf[i]) + (tbuf[i + 1],))
    for address, _, size in log:
        dirty.add(ram_offset(address) + (size,))
    if FULLMEM or state['overflow'] or not img.checked:
        # the whole image (also once per image: the shared-start invariant)
        differs = nram != ee.mem
        img.checked = True
    else:
        differs = any(nram[at:at + size] != ee.mem[at:at + size] for sp_, at, size in dirty if not sp_)
    if differs:
        raise Mismatch(where, hex(entry), 'RAM differs', first_difference(nram, ee.mem, 0))
    st = STATS[entry]
    st['cases'] += 1
    st['outcomes'] |= ee.outcomes
    st['sources'].add(where.split(':')[0])
    st['calls'] += len(ee.calls)
    img.restore()
    return ret, ee


# ======================================================================
# Route catches
# ======================================================================

def pool(ram):
    u = lambda a: struct.unpack_from('<I', ram, a)[0]
    a, out = u(POOL_HEAD), []
    while a and a not in out and len(out) < 0x200:
        out.append(a)
        a = u(a + 0x1C)
    return out


def beat_image(beat):
    return (BEATS / beat / 'eeMemory.bin').read_bytes(), (BEATS / beat / 'scratchpad.bin').read_bytes()


def catch_route(elf, beat, owners, targets, on_catch, limit=None, node_limit=None):
    """Run each live pool node whose behaviour is in `owners` as original
    code over the beat; every entry into a routine of `targets` calls
    on_catch(entry, ram, spad, regs, where) with the machine state at entry,
    then the routine runs on as original. Returns the number of catches."""
    ram, spad = beat_image(beat)
    count = [0]
    nodes = [n for n in pool(ram) if struct.unpack_from('<I', ram, n + 0x10)[0] in owners]
    if node_limit is not None:
        nodes = nodes[:node_limit]
    for node in nodes:
        owner = struct.unpack_from('<I', ram, node + 0x10)[0]
        runner = A01Base(elf, ram, spad)

        def make(entry):
            def hook(e):
                if limit is not None and count[0] >= limit:
                    raise StopCatch()
                if True:
                    count[0] += 1
                    try:
                        on_catch(entry, e.mem, e.spad, regs_of(e), f'route:{beat}:{node:#x}:{owner:#x}')
                    except Exception as ex:
                        raise CatchError(repr(ex)[:2000]) from ex
                del e.hooks[entry]
                ra = e.r[31]
                e.r[31] = RETURN
                EE.run(e, entry)
                e.r[31] = ra
                e.hooks[entry] = hook
            return hook
        for entry in targets:
            runner.hooks[entry] = make(entry)
        try:
            runner.call(owner, (node,))
        except StopCatch:
            break
        except AssertionError as ex:            # an owner leaving the interpreter's op set
            SKIPPED.append((beat, hex(node), hex(owner), str(ex)[:80]))
    return count[0]


class StopCatch(Exception):
    """The catch limit was reached: the owners stop running."""


SKIPPED = []


# ======================================================================
# Cases
# ======================================================================

def F(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def args_of(regs, spec):
    r, f = regs[0], regs[4]
    return [r[4 + i] & MASK for i in range(spec.ints)] + [f[12 + i] & MASK for i in range(spec.floats)]


# entry -> fn(img, regs, rng) -> list of (label, edits, args)
PERTURB = {}


def perturb_1C2770(img, regs, rng):
    act, st, _ = args_of(regs, SPECS[0x1C2770])
    out = []
    states = [(0, s) for s in range(0, 8)] + [(p, 0) for p in range(1, 8)] + [(3, 2), (0, 4)]
    for phase, sub in states:
        for fl in (0, 1, 2, 4, 7):
            for d8, f0, d4 in ((1.0, -0.5, 0.95), (0.0, 0.3, 0.2), (5.0, -0.79, 1.0)):
                edits = [(st + 0xE4, (sub << 8) | phase, 4), (st + 0xD8, F(d8), 4),
                         (st + 0xF0, F(f0), 4), (st + 0xD4, F(d4), 4)]
                out.append((f'e4={sub:x}/{phase} fl={fl} d8={d8}', edits, [act, st, fl]))
    return out


def perturb_1C39F0(img, regs, rng):
    node, state, _ = args_of(regs, SPECS[0x1C39F0])
    out = []
    for mode in (0, 4, 1, 3, -1):
        for dist in (0.0, 2.5, -1.25):
            edits = [(state + 0xE4, ((mode << 8) | 0x5) & MASK, 4), (state + 0xDC, F(rng.uniform(-3.2, 3.2)), 4)]
            out.append((f'mode={mode} d={dist}', edits, [node, state, F(dist)]))
    # Distances -0 and denormals: C.EQ.S compares them equal to 0 (the
    # early return), a bit compare would not. Modes 1 (the 0011E2A8 path)
    # and 0.
    for mode in (1, 0):
        for bits_ in (0x80000000, 0x00000001, 0x807FFFFF):
            edits = [(state + 0xE4, (mode << 8) | 0x5, 4), (state + 0xDC, F(0.75), 4)]
            mark = '!' if mode == 1 and bits_ != 0x807FFFFF else ''    # two always run
            out.append((f'{mark}mode={mode} d bits {bits_:#x}', edits, [node, state, bits_]))
    out += special_1C39F0(node, state)
    out += close_1C39F0(node, state)
    return out


def special_1C39F0(node, state):
    """Always-run 001C39F0 cases (fixed values, scripted callees):
    - modes 0 and 4 with a real step, so each mode test is taken both ways
      in the default run as well;
    - n = (float)001C6160(node): a scripted answer 2 with node +0x3C at
      values (fixed-seed search) where the EE n - +0x3C truncates
      differently from round-to-nearest and the difference survives the
      division by n and the product by 2 pi (the f12 of 001B1470); and
      the answer 0x01000003, whose CVT.S.W truncates (0x4B800001) where
      rounding gives 0x4B800002, with node +0x3C = 2**24 (so n - +0x3C is
      2 against 4; checked below);
    - 001C6160 that also stores node +0x3C (read after the call);
    - 001026A0 that also stores node +0xB0 (read after the calls), modes 0
      and 1;
    - the post-step node +0xB0..B8 - state +0x80..88 / 2 with the
      001026A0 product scripted to 0 (so node +0xB0..B8 keep their value
      through the add) at values where the EE subtraction differs from
      round-to-nearest."""
    import ee_float_model as EM
    f32 = lambda b: struct.unpack('<f', struct.pack('<I', b))[0]
    two_pi = 0x40C90FDB
    chain = lambda n_, d_: EM.ee_mul(two_pi, EM.ee_div(d_, n_))
    rng, xs = random.Random(0x39F1), []
    while len(xs) < 2:
        x_ = F(rng.uniform(-1.5, 1.9))
        if chain(F(2.0), EM.ee_sub(F(2.0), x_)) != chain(F(2.0), F(2.0 - f32(x_))):
            xs.append(x_)
    assert EM.ee_cvt_s_w(0x01000003) == 0x4B800001 and F(float(0x01000003)) == 0x4B800002
    assert chain(0x4B800001, EM.ee_sub(0x4B800001, 0x4B800000)) != chain(0x4B800002, EM.ee_sub(0x4B800002, 0x4B800000))
    out = []
    mode = lambda m_: [(state + 0xE4, (m_ << 8) | 0x5, 4), (state + 0xDC, F(0.75), 4)]
    for m_ in (0, 4):
        out.append((f'!mode={m_} d=2', mode(m_), [node, state, F(2.0)]))
    for x_ in xs:
        out.append((f'!n-3C 001C6160=2 3C={x_:#x}', mode(1) + [(node + 0x3C, x_, 4)], [node, state, F(2.0)],
                    {0x1C6160: answer(2)}))
    out.append(('!cvt 001C6160=0x1000003', mode(1) + [(node + 0x3C, 0x4B800000, 4)], [node, state, F(2.0)],
                {0x1C6160: answer(0x01000003)}))
    out.append(('!001C6160 stores +0x3C', mode(1) + [(node + 0x3C, F(1.5), 4)], [node, state, F(2.0)],
                {0x1C6160: answer(2, 0, [(node + 0x3C, F(0.5), 4)])}))
    for m_ in (0, 1):
        out.append((f'!mode={m_} 001026A0 stores +0xB0', mode(m_), [node, state, F(2.0)],
                    {0x1026A0: answer(0, 0, [(node + 0xB0, F(7.5), 4)])}))
    half = 0x3DCCCCCD                     # state +0x80..88 = 0.2, / 2 exactly
    vs = rounding_values('sub', half, -3.0, 3.0, 3, 0x39F0)
    edits = mode(1) + [(state + 0x80 + 4 * k, 0x3E4CCCCD, 4) for k in range(3)]
    edits += [(node + 0xB0 + 4 * k, vs[k], 4) for k in range(3)]
    zero = answer(0, 0, [(0x70003610 + 4 * k, 0, 4) for k in range(3)])
    out.append(('!post-step sub', edits, [node, state, F(2.0)], {0x1026A0: zero}))
    return out


def oracle_calls(entry, img, regs, args=None, edits=()):
    """The boundary calls of one oracle run over the pristine image plus
    `edits` (the images are left as found)."""
    spec = SPECS[entry]
    ee = A01EE(ELF, b'\0', bytes(img.spad))
    ee.mem = bytearray(img.ram)
    for address, value, size in edits:
        put_bytes(ee.mem, ee.spad, address, value & ((1 << (8 * size)) - 1), size)
    r, rh, hi, lo, f, acc, cond, vf, vacc, q = regs
    ee.r, ee.rh, ee.hi, ee.lo, ee.f, ee.acc, ee.cond = list(r), list(rh), hi, lo, list(f), acc, cond
    ee.vf, ee.vacc, ee.q = [list(v) for v in vf], list(vacc), q
    if args is not None:
        for i, v in enumerate(args[:spec.ints]): ee.r[4 + i] = FR.sx32(v)
        for i, v in enumerate(args[spec.ints:]): ee.f[12 + i] = v & MASK
    ee.ranges = [(entry, entry + spec.size)] + [(e, e + SPECS[e].size) for e in spec.inline]
    ee.entries = set(spec.inline)
    ee.r[31] = RETURN
    ee.run_f(entry)
    return ee.calls


def perturb_1B13F0(img, regs, rng):
    """Limits around the measured distance, the distance itself included
    (the strict compare)."""
    a0, a1, _ = args_of(regs, SPECS[0x1B13F0])
    distance = [c for c in oracle_calls(0x1B13F0, img, regs) if c.target == 0x11E748][0].f0
    out = [(f'limit={v}', [], [a0, a1, F(v)]) for v in (0.0, 1.0, 20.0, 100.0, 1e6)]
    for d in (-1, 0, 1):
        out.append((f'limit=distance{d:+d}ulp', [], [a0, a1, (distance + d) & MASK]))
    # a0 == a1 (the distance is +0) with limits the EE compare treats as -0
    # (a negative denormal) and as -MAX (a negative NaN): C.LT.S differs
    # from an IEEE compare on both.
    for f12 in (0x80000001, 0xFFC00000):
        out.append((f'!a0=a1 limit {f12:#x}', [], [a0, a0, f12]))
    return out


def perturb_1C69A0(img, regs, rng):
    """The model's scale (+0x60 x, y, z) and each bone's row scales (+0x18,
    +0x1C, +0x20), rest angles (+0x70..78) and s16 rest scales (+0x88..8C)
    set to distinct values."""
    (model,) = args_of(regs, SPECS[0x1C69A0])
    out = []
    count = img.ram[model + 0x0C & 0x1FFFFFF]
    for k in range(5):
        edits = [(model + 0x60 + 4 * i, F(rng.uniform(0.5, 2.0)), 4) for i in range(3)]
        for b in range(count):
            bone = u32(img.ram, model + 0x110 + 4 * b)
            edits += [(bone + 0x18 + 4 * i, F(rng.uniform(0.25, 3.0)), 4) for i in range(3)]
            edits += [(bone + 0x88 + 2 * i, rng.randrange(-0x3000, 0x3000), 2) for i in range(3)]
            if k == 2:
                edits += [(bone + 0x70 + 4 * i, F(rng.uniform(-3.1, 3.1)), 4) for i in range(3)]
            if k == 3:   # operands the VU0 forms clamp differently: Inf, NaN, MAX
                edits += [(bone + 0x18, 0x7F800000, 4), (bone + 0x1C, 0x7FC00001, 4),
                          (bone + 0x20, 0xFF7FFFFF, 4)]
            if k == 4:   # rest translations Inf / NaN / MAX: the ACC chains of the row
                # products reach Inf / NaN, where VMADDbc and VMADDAbc clamp differently
                edits += [(bone + 0x7C, 0x7F800000, 4), (bone + 0x80, 0xFFC00001, 4),
                          (bone + 0x84, 0x7F7FFFFF, 4)]
        if k == 3:
            edits += [(model + 0x60, 0x7F800000, 4), (model + 0x64, 0xFFC00000, 4)]
        # k = 3 / 4 (the Inf / NaN / MAX operands of the VU0 clamp forms)
        # always run: their saturating operands separate VMADDbc from
        # VMADDAbc and the other clamp forms.
        out.append((f'{"!" if k in (3, 4) else ""}scales{k}', edits, [model]))
    out += special_1C69A0(img, model, count)
    out += close_1C69A0(img, model)
    return out


QUAT_NLERP = 0x1CA0A0
QUAT_TO_MAT3 = 0x1CA1C0


def special_1C69A0(img, model, count):
    """Always-run 001C69A0 cases (fixed values, no generator):
    - finite, distinct rest translations (bone +0x7C / +0x80 / +0x84) per
      bone, so each field reaches its own lane of the row products (the
      Inf / NaN / MAX variant saturates every lane alike);
    - the last bone's s16 parent +0x64 = -2 with the word at model +0x108
      (the table entry at index -2) pointing at bone 0: the original indexes
      the table with any value other than -1;
    - bone-count bytes 0x80 and 0xFF (loop bound unsigned), compared up to
      the first quat_nlerp call (STOP), which the original reaches with
      bone 0;
    - an unaligned copy of the model (scratchpad 0x70003C04) and of its
      last bone (0x70003E04): the quadword loads and stores ignore address
      bits 0..3, so the scale quadword is read from model +0x5C and the
      last bone's matrix goes to bone +0x8C. The model copy's +0x5C..+0x6C
      hold distinct scales;
    - a scripted quat_to_mat3 that stores the bone count 1 (re-read every
      iteration);
    - the last bone's parent = its own index (the parent matrix is the
      output matrix)."""
    out = []
    bones = [u32(img.ram, model + 0x110 + 4 * b) for b in range(count)]
    edits = []
    for b, bone in enumerate(bones):
        edits += [(bone + 0x7C, F(1.5 + b), 4), (bone + 0x80, F(-2.25 - 0.5 * b), 4), (bone + 0x84, F(0.625 + b), 4)]
    out.append(('!translations', edits, [model]))
    if count >= 2:
        out.append(('!parent -2', [(bones[-1] + 0x64, 0xFFFE, 2), (model + 0x108, bones[0], 4)], [model]))
    for byte in (0x80, 0xFF):
        out.append((f'!count {byte:#x}', [(model + 0x0C, byte, 1)], [model], {QUAT_NLERP: STOP}))
    copy, bone_copy = 0x70003C04, 0x70003E04
    edits = [(copy + 0x0C, count, 1)]
    for i, v in enumerate((1.5, 0.75, 1.25, 2.0, 0.5)):
        edits.append((copy + 0x5C + 4 * i, F(v), 4))
    for b, bone in enumerate(bones):
        edits.append((copy + 0x110 + 4 * b, bone_copy if b == count - 1 else bone, 4))
    if count:
        for i in range(0, 0x100, 4):
            edits.append((bone_copy + i, u32(img.ram, bones[-1] + i), 4))
    out.append(('!unaligned model', edits, [copy]))
    # The bone count byte is re-read every iteration: a scripted
    # quat_to_mat3 that stores model +0x0C = 1 ends the loop after bone 0.
    if count >= 2:
        out.append(('!quat_to_mat3 stores count 1', [], [model],
                    {QUAT_TO_MAT3: answer(0, 0, [(model + 0x0C, 1, 1)])}))
    # A bone whose s16 parent is its own index: the parent matrix (loaded
    # once, all four rows, before the rows are stored) is the output
    # matrix itself. Its +0x90 rows hold distinct values.
    if count >= 1:
        edits = [(bones[-1] + 0x64, count - 1, 2)]
        edits += [(bones[-1] + 0x90 + 4 * i, F(0.25 * i - 1.5), 4) for i in range(16)]
        out.append(('!parent self', edits, [model]))
    return out


def perturb_1C3BE0(img, regs, rng):
    """001C3BE0 copies x, y and z (not w) of the 00102718 result at
    0x70003600 into row 0 of 0x70003000: a scripted 00102718 that leaves
    a nonzero w there (on the real one w is x - x of one register)."""
    node, state = args_of(regs, SPECS[0x1C3BE0])

    def product(ee):
        for i, v in enumerate((0.5, -1.5, 2.25, 3.0)):
            ee.save((ee.r[4] & MASK) + 4 * i, F(v), 4)
        ee.r[2] = 0
        ee.f[0] = 0
    # 00102B08's f12 is node +0xC0 read after 001029C0 / 00102718: a scripted
    # 00102718 that also stores node +0xC0.
    return close_1C3BE0(node, state) + [('!00102718 w', [], [node, state], {0x102718: product}),
            ('!00102718 stores +0xC0', [(node + 0xC0, F(0.25), 4)], [node, state],
             {0x102718: answer(0, 0, [(node + 0xC0, F(-1.125), 4)])})]


PERTURB.update({0x1C2770: perturb_1C2770, 0x1C39F0: perturb_1C39F0, 0x1B13F0: perturb_1B13F0,
                0x1C69A0: perturb_1C69A0, 0x1C3BE0: perturb_1C3BE0})


def near_player(img, node, rng, near):
    """Edits placing the player record (D_008102B0 +0xA0..A8) and the
    D_00810350..58 point near (within 20) or far from the node."""
    pos = struct.unpack_from('<3f', img.ram, (node + 0xB0) & 0x1FFFFFF)
    off = [rng.uniform(-6, 6) for _ in range(3)] if near else [rng.uniform(25, 60) for _ in range(3)]
    edits = [(0x8102B0 + 0xA0 + 4 * i, F(pos[i] + off[i]), 4) for i in range(3)]
    edits += [(0x810350 + 4 * i, F(pos[i] + off[i]), 4) for i in range(3)]
    return edits


def inexact_player(node, rng, near):
    """Edits moving the node (+0xB0..B8) near the origin and placing the
    player record (D_008102B0 +0xA0..A8) and D_00810350..58 at node + d
    (|d| < 6 per axis when `near`, 25..60 otherwise), each axis chosen so
    that player - node is inexact and the EE model's subtraction differs
    from the round-to-nearest binary32 result (near_player's offsets keep
    both operands within a factor of 2, where the subtraction is exact):
    cases that fail a translation doing the subtraction in host floats."""
    import ee_float_model as EM
    f32 = lambda b: struct.unpack('<f', struct.pack('<I', b))[0]
    edits = []
    for i in range(3):
        while True:
            n_ = F(rng.uniform(-4.0, 4.0))
            p_ = F(f32(n_) + (rng.uniform(-6.0, 6.0) if near else rng.uniform(25.0, 60.0)))
            if EM.ee_sub(p_, n_) != F(f32(p_) - f32(n_)):
                break
        edits += [(node + 0xB0 + 4 * i, n_, 4), (0x8102B0 + 0xA0 + 4 * i, p_, 4), (0x810350 + 4 * i, p_, 4)]
    return edits


def perturb_1BB860(img, regs, rng):
    (node,) = args_of(regs, SPECS[0x1BB860])
    out = []
    for st in (0, 1, 2, 3, 4):
        subs = range(0, 6) if st == 1 else (0,)
        for sub in subs:
            for kind in ((0x16, 0x17, 0x3E, 8, 0x40) if sub == 0 and st == 1 else (None,)):
                for bitset in ((0, 1) if kind in (0x16, 0x3E) else (0,)):
                    near = rng.random() < 0.5
                    edits = [(node + 4, st, 1), (node + 5, sub, 1), (node + 0xB, 4 if rng.random() < 0.8 else 0, 1),
                             (node + 2, 0x80 if rng.random() < 0.5 else 0, 1)]
                    if kind is not None:
                        shift = rng.randrange(0, 8)
                        area = img.ram[0x810700]
                        edits += [(node + 3, kind, 1), (node + 0x34, shift, 2),
                                  (0x810841 + area, (1 << shift) if bitset else 0, 1)]
                    edits += near_player(img, node, rng, near)
                    mark = '!' if st != 1 else ''      # states 0, 2, 3, 4: one case each, always run
                    out.append((f'{mark}st={st}/{sub} kind={kind} bit={bitset} near={near}', edits, [node]))
    # Every kind with 001BB560 succeeding (+0xB bit 2) and the flag bit set
    # and clear: the kinds 0x16 / 0x17 / 0x3E take the bit test, others
    # (8, 0x3F, 0x40) do not.
    area = img.ram[0x810700]
    for kind in (0x16, 0x17, 0x3E, 0x3F, 8, 0x40):
        for bitset in (0, 1):
            edits = [(node + 4, 1, 1), (node + 5, 0, 1), (node + 0xB, 4, 1), (node + 3, kind, 1),
                     (node + 0x34, 3, 2), (0x810841 + area, 0x08 if bitset else 0xF7, 1)]
            out.append((f'!kind {kind:#x} bit={bitset}', edits + near_player(img, node, rng, True), [node]))
    for sub in (1, 2, 4, 3):           # both answers of the script-completion tests
        for answer in (0, 1):
            script = {0x1BB7C0: returns(answer), 0x1BB7F0: returns(answer)}
            # +0xB preset to 4, so the sub-state-1 clear of +0xB is observable
            out.append((f'!st=1/{sub} answer={answer}', [(node + 4, 1, 1), (node + 5, sub, 1), (node + 0xB, 4, 1)],
                        [node], script))
    # Bit indexes 8..31 and above 31 (the s16 +0x34 & 31 of a byte flag):
    # an index >= 8 never matches the byte; one masked with & 7 would.
    area = img.ram[0x810700]
    for shift in (8, 9, 15, 31, 0x21, 0x28, -0x7FF8):
        for byte in (0xFF, 0x02):
            edits = [(node + 4, 1, 1), (node + 5, 0, 1), (node + 0xB, 4, 1), (node + 3, 0x16, 1),
                     (node + 0x34, shift, 2), (0x810841 + area, byte, 1)]
            mark = '!' if (shift, byte) in ((8, 0xFF), (0x21, 0x02), (-0x7FF8, 0xFF)) else ''
            out.append((f'{mark}bit index {shift:#x} byte {byte:#x}', edits + near_player(img, node, rng, True),
                        [node]))
    # Bit indexes 0x10..0x17 (and 0x30..0x37, -0x10): & 31 gives bits 16..23,
    # which never match the byte, while a narrower mask (& 15) gives bits
    # 0..7. Each with the byte's bit (index - 16) set, and with only that bit
    # clear. Always run, with their own generator (the shared one feeds the
    # quick sample).
    local = random.Random(0xB860)
    for shift in list(range(0x10, 0x18)) + [0x30, 0x37, -0x10]:
        low = 1 << ((shift & 31) - 16)
        for byte in (low, 0xFF ^ low):
            edits = [(node + 4, 1, 1), (node + 5, 0, 1), (node + 0xB, 4, 1), (node + 3, 0x16, 1),
                     (node + 0x34, shift, 2), (0x810841 + area, byte, 1)]
            out.append((f'!bit index {shift:#x} byte {byte:#x}', edits + near_player(img, node, local, True), [node]))
    # The flag bit set while 001BB560 answers 0 (+0xB without bit 2), and
    # clear with the same answer: both "no success" outcomes.
    for byte in (0x08, 0x00):
        edits = [(node + 4, 1, 1), (node + 5, 0, 1), (node + 0xB, 0, 1), (node + 3, 0x3E, 1),
                 (node + 0x34, 3, 2), (0x810841 + area, byte, 1)]
        out.append((f'!bit set={byte != 0} 1BB560 answers 0', edits + near_player(img, node, rng, True), [node]))
    for near in (True, False):
        for k in range(2):
            edits = [(node + 4, 1, 1), (node + 5, 3, 1), (node + 2, 0x80, 1)] + inexact_player(node, rng, near)
            out.append((f'!inexact near={near} {k}', edits, [node]))
    # The area byte is unsigned: area 0x80 with the flag bit set in
    # D_00810841[0x80] and clear in D_00810841[-0x80].
    for byte in (0x80, 0xFF):
        edits = [(node + 4, 1, 1), (node + 5, 0, 1), (node + 0xB, 4, 1), (node + 3, 0x16, 1),
                 (node + 0x34, 3, 2), (0x810700, byte, 1), (0x810841 + byte, 0x08, 1),
                 (0x810841 + byte - 0x100, 0xF7, 1)]
        out.append((f'!area byte {byte:#x}', edits + near_player(img, node, local, True), [node]))
    # +5 is re-read after the calls that precede its increments: a scripted
    # 001BC150 (sub-state 3) and a scripted 001BA1F0 (the last callee of
    # the inline 001BB560, sub-state 0 with the flag bit clear) that store
    # +5, so a translation that kept +5 from before the call would differ.
    out.append(('!st=1/3 001BC150 stores +5', [(node + 4, 1, 1), (node + 5, 3, 1)], [node],
                {0x1BC150: returns_f(0, [(node + 5, 7, 1)])}))
    edits = [(node + 4, 1, 1), (node + 5, 0, 1), (node + 0xB, 4, 1), (node + 3, 0x16, 1),
             (node + 0x34, 3, 2), (0x810841 + area, 0xF7, 1)]
    out.append(('!st=1/0 bit clear 001BA1F0 stores +5', edits + near_player(img, node, local, True), [node],
                {0x1BA1F0: returns_f(0, [(node + 5, 5, 1)])}))
    for dx, label in ((20.0, '!d=20'), (20.00001, 'd>20'), (19.99999, 'd<20')):
        edits = [(node + 4, 1, 1), (node + 5, 5, 1), (node + 2, 0x80, 1),
                 (node + 0xB0, F(100.0), 4), (node + 0xB4, F(5.0), 4), (node + 0xB8, F(-50.0), 4),
                 (0x810350, F(100.0 + dx), 4), (0x810354, F(5.0), 4), (0x810358, F(-50.0), 4)]
        out.append((label, edits, [node]))
    out += special_1BB860(img, node)
    out += close_1BB860(img, node)
    return out


def special_1BB860(img, node):
    """Always-run 001BB860 cases (own generator; appended after the
    sampled ones):
    - state 0 with byte +1 nonzero (the byte store of 1 at +0);
    - 001BB7C0 / 001BB7F0 answering 2 (the tests are != 0);
    - a scripted 001BB7C0 (sub-state 2) that stores +5 = 7 and answers 1:
      +5 is re-read for the increment;
    - the distance test reads node +0xB0 after 001C6380 and after the +0x4C
      method: each scripted to move the node 100 units away (from within
      20), with +1 = 0 and bit 0x80 of +2 set;
    - kinds 0x56, 0x57, 0x7E and 0x96 (none of 0x16 / 0x17 / 0x3E: the
      other-kinds path) with the flag bit clear."""
    local = random.Random(0xB862)
    area = img.ram[0x810700]
    out = [('!st=0 +1 preset', [(node + 4, 0, 1), (node + 1, 0x5A, 1)], [node])]
    for sub in (1, 2, 4):
        out.append((f'!st=1/{sub} answer=2', [(node + 4, 1, 1), (node + 5, sub, 1), (node + 0xB, 4, 1)], [node],
                    {0x1BB7C0: answer(2), 0x1BB7F0: answer(2)}))
    out.append(('!st=1/2 001BB7C0 stores +5', [(node + 4, 1, 1), (node + 5, 2, 1)], [node],
                {0x1BB7C0: answer(1, 0, [(node + 5, 7, 1)])}))
    near = [(node + 4, 1, 1), (node + 5, 5, 1), (node + 1, 0, 1), (node + 2, 0x80, 1),
            (node + 0xB0, F(100.0), 4), (node + 0xB4, F(5.0), 4), (node + 0xB8, F(-50.0), 4),
            (0x810350, F(105.0), 4), (0x810354, F(5.0), 4), (0x810358, F(-50.0), 4)]
    away = [(node + 0xB0, F(205.0), 4)]
    out.append(('!001C6380 moves the node', near, [node], {0x1C6380: answer(0, 0, away)}))
    method = u32(img.ram, node + 0x4C)
    if method != 0x1C6380:
        out.append(('!method moves the node', near, [node], {method: answer(0, 0, away)}))
    for kind in (0x56, 0x57, 0x7E, 0x96):
        edits = [(node + 4, 1, 1), (node + 5, 0, 1), (node + 0xB, 4, 1), (node + 3, kind, 1),
                 (node + 0x34, 3, 2), (0x810841 + area, 0xF7, 1)]
        out.append((f'!kind {kind:#x} bit clear', edits + near_player(img, node, local, True), [node]))
    return out


def perturb_1BB560(img, regs, rng):
    node, a1, a2 = args_of(regs, SPECS[0x1BB560])
    out = []
    for flag in (0, 1):
        for kind in (8, 0x16, 0x17, 0x40):
            for area_16 in (0, 1):
                for yaw in (0.0, 1.2, 2.9, -2.0):
                    edits = [(node + 0xB, 4, 1), (node + 3, kind, 1), (node + 0xC4, F(yaw), 4),
                             (node + 0x34, rng.choice((0, 1, 2, 0x80)), 2)]
                    if area_16:
                        edits += [(0x810700, 0x16, 1)]
                    edits += near_player(img, node, rng, True)
                    out.append((f'a2={flag} kind={kind:#x} area16={area_16} yaw={yaw}', edits, [node, a1, flag]))
    out.append(('noflag', [(node + 0xB, 0, 1)], [node, a1, 0]))
    # Area 0x16 with +0x34 indexes that differ under & 0x7F and & 0x3F: the
    # table bytes at both candidate rows (both +0x2E halves) are set to
    # opposite answers (1 sets, 2 clears bit 0x80 of D_008106C8, preset
    # to the other state).
    row = u32(img.ram, 0x24E140 + 4 * 0x16)
    for index in (0x41, 0x7F, 0x40):
        for a, b, preset in ((1, 2, 0x40), (2, 1, 0xC0), (1, 0, 0x00)):
            edits = [(node + 0xB, 4, 1), (node + 3, 0x40, 1), (0x810700, 0x16, 1), (node + 0x34, index, 2),
                     (0x8106C8, preset, 4)]
            for half in (0, 1):
                edits += [(row + (index & 0x7F) * 4 + half, a, 1), (row + (index & 0x3F) * 4 + half, b, 1)]
            out.append((f'!area16 index {index:#x} rows {a}/{b} preset {preset:#x}',
                        edits + near_player(img, node, rng, True), [node, a1, 0]))
    # Row bytes other than 1 and 2 (0, 3, 0xFF) leave bit 0x80 of
    # D_008106C8 alone: each with the bit preset set and clear, so a
    # translation testing >= 2 (clears) or >= 1 / != 0 (sets) differs.
    for byte in (0, 3, 0xFF):
        for preset in (0xC0, 0x40):
            edits = [(node + 0xB, 4, 1), (node + 3, 0x40, 1), (0x810700, 0x16, 1), (node + 0x34, 5, 2),
                     (0x8106C8, preset, 4), (row + 5 * 4, byte, 1), (row + 5 * 4 + 1, byte, 1)]
            out.append((f'!area16 row byte {byte:#x} preset {preset:#x}',
                        edits + near_player(img, node, rng, True), [node, a1, 0]))
    # fabs(angle) exactly pi/2 and one ulp either side (0011DF78 scripted).
    for bits_ in (0x3FC90FDB, 0x3FC90FDA, 0x3FC90FDC):
        out.append((f'!fabs={bits_:#x}', [(node + 0xB, 4, 1), (node + 3, 0x40, 1)] + near_player(img, node, rng, True),
                    [node, a1, 0], {0x11DF78: returns_f(bits_)}))
    out += small_position_1BB560(img, regs, node, a1)
    local = random.Random(0xB561)       # the shared generator feeds the quick sample
    # +0xB values without bit 2 but with other bits (the test is & 4).
    for byte in (0x02, 0xFB, 0x80):
        out.append((f'!+0xB={byte:#x}', [(node + 0xB, byte, 1)] + near_player(img, node, local, True),
                    [node, a1, 0]))
    # pi + node.C4 on the fabs <= pi/2 side (0011DF78 scripted to 0) with C4
    # values where the EE sum differs from round-to-nearest binary32.
    for c4 in rounding_values('add', 0x40490FDB, -3.1, 3.1, 2, 0x0C4):
        out.append((f'!pi+C4 {c4:#x}', [(node + 0xB, 4, 1), (node + 3, 0x40, 1), (node + 0xC4, c4, 4)]
                    + near_player(img, node, local, True), [node, a1, 0], {0x11DF78: returns_f(0)}))
    # The table index +0x2E is re-read after 001BBD60 / 001BA1A0: area 0x16
    # with the two row bytes of index 5 at opposite answers, 0011DF78
    # scripted to 0 (so the routine stores +0x2E = 0) and a scripted
    # 001BA1A0 that stores +0x2E = 1.
    for preset in (0x40, 0xC0):
        edits = [(node + 0xB, 4, 1), (node + 3, 0x40, 1), (0x810700, 0x16, 1), (node + 0x34, 5, 2),
                 (0x8106C8, preset, 4), (row + 5 * 4, 1, 1), (row + 5 * 4 + 1, 2, 1)]
        out.append((f'!area16 001BA1A0 stores +0x2E preset {preset:#x}', edits + near_player(img, node, local, True),
                    [node, a1, 0], {0x11DF78: returns_f(0), 0x1BA1A0: returns_f(0, [(node + 0x2E, 1, 2)])}))
    out += special_1BB560(img, node, a1, row)
    out += close_1BB560(img, node, a1, row)
    return out


def special_1BB560(img, node, a1, row):
    """Always-run 001BB560 cases (own generator; appended after the
    sampled ones):
    - the half stores of +0x2E (0 on the fabs <= pi/2 side, 1 on the other)
      over +0x2F preset nonzero;
    - a2 = 0x100 (the test is a2 == 0 on the whole word);
    - a scripted 0011E2A8 that also stores p.C4 (D_00810374): the cosine's
      argument is read again after the sine;
    - a scripted 001BA1A0 that stores area 0x16 over 0x15: the area byte is
      read after 001BBD60 / 001BA1A0 (row byte 1 at index 5, bit 0x80 of
      D_008106C8 preset clear)."""
    local = random.Random(0xB562)
    base = [(node + 0xB, 4, 1), (node + 3, 0x40, 1)]
    out = []
    for label, fabs in (('near', 0), ('far', F(3.0))):
        out.append((f'!+0x2F preset {label}', base + [(node + 0x2E, 0xA5A5, 2)] + near_player(img, node, local, True),
                    [node, a1, 0], {0x11DF78: returns_f(fabs)}))
    out.append(('!a2=0x100', base + near_player(img, node, local, True), [node, a1, 0x100]))
    out.append(('!0011E2A8 stores p.C4', base + near_player(img, node, local, True), [node, a1, 0],
                {0x11E2A8: answer(0, F(0.5), [(0x810374, F(1.0), 4)])}))
    edits = base + [(0x810700, 0x15, 1), (node + 0x34, 5, 2), (0x8106C8, 0x40, 4), (row + 5 * 4, 1, 1)]
    out.append(('!001BA1A0 stores area 0x16', edits + near_player(img, node, local, True), [node, a1, 0],
                {0x11DF78: returns_f(0), 0x1BA1A0: answer(0, 0, [(0x810700, 0x16, 1)])}))
    return out


def six_products(count, seed):
    """Sines / cosines t in [-1, 1] whose product 6 * t in the EE model
    differs from the round-to-nearest binary32 product (fixed seed search)."""
    import ee_float_model as EM
    rng = random.Random(seed)
    f32 = lambda b: struct.unpack('<f', struct.pack('<I', b))[0]
    out = []
    while len(out) < count:
        t = F(rng.uniform(-1.0, 1.0))
        if EM.ee_mul(F(6.0), t) != F(6.0 * f32(t)):
            out.append(t)
    return out


def small_position_1BB560(img, regs, node, a1):
    """001BB560 with node +0xB0 / +0xB8 near 0, so the point's x = B0 -
    6 sin(p.C4) and z = B8 - 6 cos(p.C4) keep the last bit of each product
    (a large position absorbs a 1-ulp product difference). Two kinds:
    - the real sine / cosine (0011E2A8 / 0011DE90) at yaws chosen with the
      oracle so that both products round differently in the EE model and
      in round-to-nearest binary32, and so that each difference survives
      the subtraction;
    - scripted 0011E2A8 / 0011DE90 answers (six_products) with only the
      sine product, only the cosine product, and both differing.
    B0 / B8 are 0 or small nonzero values. All always run; they use their
    own generator (the shared one feeds the quick sample)."""
    import ee_float_model as EM
    f32 = lambda b: struct.unpack('<f', struct.pack('<I', b))[0]
    local = random.Random(0xB560)
    out = []
    base = [(node + 0xB, 4, 1), (node + 3, 0x40, 1)]

    def differs(pos, t):
        return EM.ee_sub(pos, EM.ee_mul(F(6.0), t)) != EM.ee_sub(pos, F(6.0 * f32(t)))

    found = 0
    for _ in range(60):
        if found == 3:
            break
        pos = [0, 0] if found == 0 else [F(local.uniform(-0.5, 0.5)) for _ in range(2)]
        edits = base + [(node + 0xB0, pos[0], 4), (node + 0xB8, pos[1], 4),
                        (node + 0xC4, F(local.uniform(-3.1, 3.1)), 4)]
        calls = oracle_calls(0x1BB560, img, regs, [node, a1, 0], edits)
        sin_ = [c.f0 for c in calls if c.target == 0x11E2A8]
        cos_ = [c.f0 for c in calls if c.target == 0x11DE90]
        if sin_ and cos_ and differs(pos[0], sin_[0]) and differs(pos[1], cos_[0]):
            out.append((f'!small position real {found}', edits, [node, a1, 0]))
            found += 1
    assert found == 3, ('001BB560 small-position yaws not found', found)
    ts = six_products(4, 0x6B560)
    same = F(0.5)                       # 6 * 0.5 = 3 exactly: no rounding either way
    for label, s_, c_ in (('sin', ts[0], same), ('cos', same, ts[1]), ('both', ts[2], ts[3])):
        for pos in ((0, 0), (F(0.25), F(-0.375))):
            if not (differs(pos[0], s_) or s_ == same) or not (differs(pos[1], c_) or c_ == same):
                continue
            edits = base + [(node + 0xB0, pos[0], 4), (node + 0xB8, pos[1], 4)]
            out.append((f'!small position {label} B0={pos[0]:#x}', edits, [node, a1, 0],
                        {0x11E2A8: returns_f(s_), 0x11DE90: returns_f(c_)}))
    return out


def perturb_1C02E0(img, regs, rng):
    (node,) = args_of(regs, SPECS[0x1C02E0])
    out = []
    # State 1, every sub-state / distance / scratchpad byte / +0x36 / +0x28
    # combination; the neighbouring scratchpad bytes hold the opposite value.
    out.append(('!st=0 no node', [(node + 4, 0, 1), (0x810845, 0, 1)], [node], {0x1AFA90: returns(0)}))
    # State 0 with a companion: 001AFA90 answers the node's existing
    # companion from the capture (+0x24), so the set-up path and both
    # direction vectors run in every mode.
    companion = u32(img.ram, node + 0x24)
    if 0x100000 <= companion < 0x2000000:
        out.append(('!st=0 spawn', [(node + 4, 0, 1), (0x810845, 0, 1)], [node],
                    {0x1AFA90: returns(companion)}))
        # the same with 001028D0 scripted to leave a vector with distinct
        # x / y / z at its destination (on the captures the two directions
        # come out axis-aligned, so a mix-up of their y and z would not show)
        def vector_out(ee):
            for i, v in enumerate((1.5, -2.5, 3.25, 1.0)):
                ee.save((ee.r[4] & MASK) + 4 * i, F(v), 4)
            ee.r[2] = 0
            ee.f[0] = 0
        out.append(('!st=0 spawn distinct', [(node + 4, 0, 1), (0x810845, 0, 1)], [node],
                    {0x1AFA90: returns(companion), 0x1028D0: vector_out}))
        # The spawn's half and byte stores over nonzero neighbours: half
        # +0x34 = 0x50 (+0x35 preset 0xA5, +0x36..37 preset), tail halves +0
        # and +2 = 0 (tail +0..3 preset), companion bytes +3 = 0x12 and +0xD
        # = 2 (neighbours +2, +4, +0xC, +0xE preset), so a store of another
        # width differs.
        presets = [(node + 0x34, 0xA55A, 2), (node + 0x36, 0x0101, 2), (node + 0x1F0, 0xA5A5A5A5, 4),
                   (companion + 2, 0x5A, 1), (companion + 4, 0x5A, 1), (companion + 0xC, 0x5A, 1),
                   (companion + 0xE, 0x5A, 1)]
        out.append(('!st=0 spawn presets', [(node + 4, 0, 1), (0x810845, 0, 1)] + presets, [node],
                    {0x1AFA90: returns(companion)}))
        # node +0xBC (copied to SPR 0x700038AC by 00102948 before the
        # routine's own store of 1.0 there) at 0.5.
        out.append(('!st=0 spawn +0xBC', [(node + 4, 0, 1), (0x810845, 0, 1), (node + 0xBC, F(0.5), 4)], [node],
                    {0x1AFA90: returns(companion)}))
        # node matrix +0x10C = 2.0 when 001026A0 runs (001C6380 rebuilds the
        # matrix just before, so a scripted 001C6380 leaves it): the first
        # product's w is then not 1.0 before the routine's store of 1.0 at
        # 0x700038BC.
        out.append(('!st=0 spawn +0x10C', [(node + 4, 0, 1), (0x810845, 0, 1)], [node],
                    {0x1AFA90: returns(companion), 0x1C6380: returns_f(0, [(node + 0x10C, F(2.0), 4)])}))
        # D_00810845 values with other bits than 0x20: the spawn path.
        for byte in (0x10, 0xDF):
            out.append((f'!st=0 845={byte:#x}', [(node + 4, 0, 1), (0x810845, byte, 1)], [node],
                        {0x1AFA90: returns(companion)}))
    # 001AFA90 answers with bit 31 set: the original takes the spawn path
    # (its first act is the 00102948 call with c + 0xB0); compared up to
    # that call (the stores to c + 3 etc. lie outside RAM in the harness).
    for c in (0x80000000, 0xFFFFFF00):
        out.append((f'!st=0 c={c:#x}', [(node + 4, 0, 1), (0x810845, 0, 1)], [node],
                    {0x1AFA90: returns(c), 0x102948: STOP}))
    combos = [(sub, near, b8d, b64, h36, h28) for sub in (0, 1, 2) for near in (0, 1) for b8d in (0, 1)
              for b64 in (0, 1) for h36 in (0, 5) for h28 in (0, 1)]
    for i, (sub, near, b8d, b64, h36, h28) in enumerate(combos):
        edits = [(node + 4, 1, 1), (node + 5, sub, 1), (node + 0x36, h36, 2), (node + 0x28, h28, 2),
                 (0x70003B8D, b8d, 1), (0x70003B8C, 1 - b8d, 1), (0x70003B8E, 1 - b8d, 1),
                 (0x70003B64, b64, 1), (0x70003B63, 1 - b64, 1), (0x70003B65, 1 - b64, 1)]
        edits += near_player(img, node, rng, near)
        mark = '!' if i % 8 == 0 or (sub == 1 and near and not b8d) else ''
        out.append((f'{mark}st=1/{sub} near={near} 8d={b8d} 64={b64} 36={h36} 28={h28}', edits, [node]))
    # s16 +0x36 values whose low byte is 0 (a byte test would miss them),
    # and +0x28 preset to 0x100 so a byte store of 1 would differ from the
    # half store.
    for h36 in (0x5000, 0x0100, -0x8000):
        edits = [(node + 4, 1, 1), (node + 5, 1, 1), (node + 0x36, h36, 2), (0x70003B8D, 0, 1)]
        out.append((f'!st=1 h36={h36:#x}', edits + near_player(img, node, rng, True), [node]))
    for near in (0, 1):
        edits = [(node + 4, 1, 1), (node + 5, 1, 1), (node + 0x28, 0x100, 2), (0x70003B8D, 0, 1),
                 (0x70003B64, 0, 1)]
        out.append((f'!st=1 h28=0x100 near={near}', edits + near_player(img, node, rng, near), [node]))
    # Half +0x28 = 0x100 with sub-states 0 (the half store of 0 before
    # 001BF630) and 2 (no 001BF630: the half test before 001D0D60 sees the
    # preset), and tail +2..3 preset nonzero (the tail half store of the
    # 001D0D60 result).
    local = random.Random(0x02E0)
    for sub, near in ((0, 0), (0, 1), (2, 0), (1, 1)):
        edits = [(node + 4, 1, 1), (node + 5, sub, 1), (node + 0x28, 0x100, 2), (0x70003B8D, 0, 1),
                 (0x70003B64, 0, 1), (node + 0x1F2, 0xA55A, 2)]
        out.append((f'!st=1/{sub} h28=0x100 tail+2 near={near}', edits + near_player(img, node, local, near), [node]))
    for st in (0, 1, 2, 3, 5):
        for sub in ((0, 1, 2) if st == 1 else (0,)):
            for variant in range(3):
                edits = [(node + 4, st, 1), (node + 5, sub, 1), (node + 0x36, rng.choice((0, 0, 5)), 2),
                         (node + 0x28, rng.choice((0, 1)), 2),
                         (0x70003B8D, rng.choice((0, 0, 1)), 1), (0x70003B64, rng.choice((0, 1)), 1)]
                if st == 0:
                    edits.append((0x810845, 0x20 if variant == 2 else 0, 1))
                edits += near_player(img, node, rng, variant != 1)
                mark = '!' if st != 1 and variant in (0, 2) else ''
                out.append((f'{mark}st={st}/{sub} v={variant}', edits, [node]))
    out += special_1C02E0(img, node)
    out += close_1C02E0(img, node)
    return out


def special_1C02E0(img, node):
    """Always-run 001C02E0 cases (appended after the sampled ones). The
    001BF630 limit (the float tail +0x18 points at) is set to 1e6 (near)
    or -1 (far), so the answer does not depend on the geometry:
    - D_00810845 = 0x20 with D_00810767 preset (the byte store of 0xFF at
      D_00810766);
    - state 2 with +5 preset (the byte store of 3 at +4);
    - a scripted 001FBD50 that stores +0x36 = 0x5000 (+0x36 is read after
      it);
    - a scripted 001C6380 that stores half +0x28 = 1 after the routine
      stored 0 (far): +0x28 is read after it;
    - the scratchpad byte 0x70003B64 = 0x80 (the sound test is == 0)."""
    out = [('!st=0 845=0x20 767 preset', [(node + 4, 0, 1), (0x810845, 0x20, 1), (0x810767, 0x5A, 1)], [node]),
           ('!st=2 +5 preset', [(node + 4, 2, 1), (node + 5, 1, 1)], [node])]
    limit = u32(img.ram, node + 0x1F0 + 0x18)
    if not 0x100000 <= limit < 0x2000000:
        return out
    st1 = [(node + 4, 1, 1), (node + 5, 1, 1), (node + 0x36, 0, 2), (node + 0x28, 0, 2), (0x70003B8D, 0, 1),
           (0x70003B64, 0, 1)]
    near, far = [(limit, F(1e6), 4)], [(limit, F(-1.0), 4)]
    out += [('!st=1/1 001FBD50 stores +0x36', st1 + near, [node],
             {0x1FBD50: answer(0, 0, [(node + 0x36, 0x5000, 2)])}),
            ('!st=1/1 001C6380 stores +0x28', st1 + far, [node],
             {0x1C6380: answer(0, 0, [(node + 0x28, 1, 2)])}),
            ('!st=1/1 b64=0x80', st1 + near + [(0x70003B64, 0x80, 1)], [node])]
    return out


def perturb_1BF630(img, regs, rng):
    player, node, tail = args_of(regs, SPECS[0x1BF630])
    out = []
    for near in (0, 1):
        for byte in (0, 1):
            out.append((f'near={near} b={byte}', near_player(img, node, rng, near) + [(0x70003B8D, byte, 1)],
                        [player, node, tail]))
    for byte in (0x80, 0xFF):     # the byte test is unsigned (nonzero)
        out.append((f'!b={byte:#x}', near_player(img, node, random.Random(0xF630 + byte), True) + [(0x70003B8D, byte, 1)],
                    [player, node, tail]))
    # The limit (the float tail +0x18 points at) equal to the measured
    # distance and one ulp either side; the neighbouring scratchpad bytes
    # of 0x70003B8D nonzero.
    limit = u32(img.ram, tail + 0x18)
    geometry = [(0x70003B8D, 0, 1), (0x70003B8C, 0xA5, 1), (0x70003B8E, 0x5A, 1)]
    geometry += near_player(img, node, rng, True)
    calls = oracle_calls(0x1BF630, img, regs, [player, node, tail], geometry)
    distance = [c for c in calls if c.target == 0x11E748][0].f0
    for d in (-1, 0, 1):
        out.append((f'!limit=distance{d:+d}ulp', geometry + [(limit, (distance + d) & MASK, 4)],
                    [player, node, tail]))
    # Inexact player - node subtractions (EE differs from round-to-nearest),
    # near and far, and at the limit edge measured on that geometry.
    for near in (1, 0):
        out.append((f'!inexact near={near}', [(0x70003B8D, 0, 1)] + inexact_player(node, rng, near),
                    [player, node, tail]))
    geometry = [(0x70003B8D, 0, 1)] + inexact_player(node, rng, True)
    calls = oracle_calls(0x1BF630, img, regs, [player, node, tail], geometry)
    distance = [c for c in calls if c.target == 0x11E748][0].f0
    for d in (-1, 0, 1):
        out.append((f'!inexact limit=distance{d:+d}ulp', geometry + [(limit, (distance + d) & MASK, 4)],
                    [player, node, tail]))
    # A player argument other than D_008102B0: a scratchpad record whose
    # +0xA0 / +0xA8 differ from the player's (both fields are read from a0).
    other = 0x70003800
    edits = [(0x70003B8D, 0, 1), (other + 0xA0, F(2.5), 4), (other + 0xA8, F(-3.75), 4),
             (node + 0xB0, F(1.0), 4), (node + 0xB8, F(1.0), 4)]
    out.append(('!player record copy', edits, [other, node, tail]))
    out += close_1BF630(img, player, node, tail)
    return out


def rounding_values(kind, operand, lo, hi, count, seed):
    """Values v in [lo, hi] for which the EE model's v + operand (kind
    'add') or v - operand ('sub') differs from the round-to-nearest
    binary32 result (fixed seed search): cases that fail a translation
    doing the operation in host floats."""
    import ee_float_model as EM
    rng = random.Random(seed)
    f32 = lambda b: struct.unpack('<f', struct.pack('<I', b))[0]
    out = []
    while len(out) < count:
        v = F(rng.uniform(lo, hi))
        exact = f32(v) + f32(operand) if kind == 'add' else f32(v) - f32(operand)
        ee = EM.ee_add(v, operand) if kind == 'add' else EM.ee_sub(v, operand)
        if ee != F(exact):
            out.append(v)
    return out


def fade_values(count=2):
    """+0x64 values whose step by 0.01 rounds differently in the EE model."""
    return rounding_values('sub', 0x3C23D70A, 0.02, 1.0, count, 0x0064)


def perturb_1BFFD0(img, regs, rng):
    (node,) = args_of(regs, SPECS[0x1BFFD0])
    e0 = u32(img.ram, node + 0x20)
    out = []
    # State 1 hand-over matrix: flags x e0 +0 x both tail counters x +0xD.
    combos = [(flags, e0_0, c2, c1, d) for flags in (0x1000, 0x4000, 0x0100, 0) for e0_0 in (1, 2)
              for c2 in (0, 1, 4) for c1 in (0, 3) for d in (1, 2)]
    for i, (flags, e0_0, c2, c1, d) in enumerate(combos):
        edits = [(node + 4, 1, 1), (e0 + 4, 1, 1), (e0 + 1, 1, 1), (node + 0xD, d, 1), (e0, e0_0, 1),
                 (node + 0x36, flags, 2), (e0 + 0x28, (i >> 2) & 1, 2), (node + 0x1F0 + 0x1C, c2, 4),
                 (e0 + 0x1F0 + 0x1C, c1, 4), (e0 + 0x1F0 + 2, (i >> 1) & 1, 2)]
        mark = '!' if (d == 1 and e0_0 == 1 and c1 == 0 and c2 in (0, 1)) or (d == 2 and i % 8 < 2) else ''
        out.append((f'{mark}st=1 flags={flags:#x} e0={e0_0} c2={c2} c1={c1} d={d}', edits, [node]))
    # e1 half +2 values other than 0 / 1: the routine passes 001BFF90 a
    # boolean, a translation passing the raw half would differ.
    for h in (2, 0x100, -0x8000):
        edits = [(node + 4, 1, 1), (e0 + 4, 1, 1), (e0 + 1, 1, 1), (node + 0xD, 1, 1), (e0, 1, 1),
                 (node + 0x36, 0, 2), (e0 + 0x1F0 + 2, h, 2)]
        out.append((f'!st=1 e1+2={h:#x}', edits, [node]))
    # A tail counter with bit 31 set (a signed test would not count it
    # down), and hand-over flags mixing a 0x5000 bit with others (the whole
    # half is handed over, not only the 0x5000 bits).
    for c2, flags in ((-1, 0), (-0x7FFFFFFF, 0), (0, 0x4100), (0, 0x5001)):
        edits = [(node + 4, 1, 1), (e0 + 4, 1, 1), (e0 + 1, 1, 1), (node + 0xD, 1, 1), (e0, 1, 1),
                 (node + 0x36, flags, 2), (node + 0x1F0 + 0x1C, c2, 4), (e0 + 0x1F0 + 0x1C, 0, 4)]
        out.append((f'!st=1 c2={c2:#x} flags={flags:#x}', edits, [node]))
    # +0xD == 2: the parent's half +0x28 alone decides the step; its
    # neighbour +0x2A (and +0x26) hold the opposite value.
    for h28 in (0, 1):
        edits = [(node + 4, 1, 1), (e0 + 4, 1, 1), (e0 + 1, 1, 1), (node + 0xD, 2, 1),
                 (e0 + 0x28, h28, 2), (e0 + 0x2A, 1 - h28, 2), (e0 + 0x26, 1 - h28, 2)]
        out.append((f'!st=1 d=2 e0+28={h28}', edits, [node]))
    out.append(('!st=0 e0=3', [(node + 4, 0, 1), (e0 + 4, 3, 1)], [node]))
    for d in (1, 2):
        out.append((f'!st=0 d={d}', [(node + 4, 0, 1), (e0 + 4, 1, 1), (node + 0xD, d, 1)], [node],
                    {0x1B10B0: returns(0)}))
    out.append(('!st=0 busy', [(node + 4, 0, 1), (e0 + 4, 1, 1)], [node], {0x1B10B0: returns(1)}))
    # e0 state bytes above 3 (the test is == 3) and 001B10B0 answers other
    # than 0 / 1 (the test is != 0).
    for byte in (4, 0xFF):
        out.append((f'!st=0 e0={byte:#x}', [(node + 4, 0, 1), (e0 + 4, byte, 1)], [node], {0x1B10B0: returns(1)}))
    for answer in (2, 0x100, 0x80000000):
        out.append((f'!st=0 busy {answer:#x}', [(node + 4, 0, 1), (e0 + 4, 1, 1)], [node],
                    {0x1B10B0: returns(answer)}))
    # The state-0 initialisation's stores over nonzero neighbours: half +0x34
    # (+0x35), tail half +2 (+0x1F3), the byte +4 (+5), the words +0x30 /
    # +0x1F0 +0x1C / +0x58 preset.
    presets = [(node + 0x34, 0xA55A, 2), (node + 0x1F2, 0xA55A, 2), (node + 0x1F0 + 0x1C, 0xA5A5A5A5, 4),
               (node + 0x30, 0x5A5A5A5A, 4), (node + 0x58, 0x5A5A5A5A, 4), (node + 1, 0x5A, 1)]
    for d in (1, 2):
        out.append((f'!st=0 d={d} presets', [(node + 4, 0, 1), (e0 + 4, 1, 1), (node + 0xD, d, 1)] + presets, [node],
                    {0x1B10B0: returns(0)}))
    # Tail counters as words: e1 / e2 +0x1C = 0x10000 (a half read sees 0).
    for c2, c1 in ((0, 0x10000), (0x10000, 0), (0, 0x80000000)):
        edits = [(node + 4, 1, 1), (e0 + 4, 1, 1), (e0 + 1, 1, 1), (node + 0xD, 1, 1), (e0, 1, 1),
                 (node + 0x36, 0x1000, 2), (node + 0x1F0 + 0x1C, c2, 4), (e0 + 0x1F0 + 0x1C, c1, 4)]
        out.append((f'!st=1 counters {c2:#x}/{c1:#x}', edits, [node]))
    # e0 +0x36 is stored from a re-read of node +0x36 after the effect
    # spawn 001EFE00: a scripted 001EFE00 that changes node +0x36.
    edits = [(node + 4, 1, 1), (e0 + 4, 1, 1), (e0 + 1, 1, 1), (node + 0xD, 1, 1), (e0, 1, 1),
             (node + 0x36, 0x4000, 2), (node + 0x1F0 + 0x1C, 0, 4), (e0 + 0x1F0 + 0x1C, 0, 4)]
    out.append(('!st=1 001EFE00 stores +0x36', edits, [node], {0x1EFE00: returns_f(0, [(node + 0x36, 0x4321, 2)])}))
    # State 2: the method gate reads +0x64 after 001C64F0 / 001C68C0; a
    # scripted 001C68C0 leaving -0 or a denormal there (C.EQ.S: equal to
    # 0, so no method).
    for bits_ in (0x80000000, 0x00000001):
        out.append((f'!st=2 001C68C0 stores +0x64={bits_:#x}', [(node + 4, 2, 1), (node + 0x64, F(0.5), 4)], [node],
                    {0x1C68C0: returns_f(0, [(node + 0x64, bits_, 4)])}))
    # e0 in state exactly 2 (the >= 2 test), and fade values whose step by
    # 0.01 rounds differently truncated (EE) and to nearest (host).
    out.append(('!st=1 e0 state 2', [(node + 4, 1, 1), (e0 + 4, 2, 1), (e0 + 1, 1, 1)], [node]))
    for f64 in fade_values():
        out.append((f'!f64={f64:#x}', [(node + 4, 2, 1), (node + 0x64, f64, 4)], [node]))
    for f64, label in ((0.01, '!f64=0.01'), (0.005, '!f64=0.005'), (0.5, 'f64=0.5')):
        out.append((label, [(node + 4, 2, 1), (node + 0x64, F(f64), 4)], [node]))
    for st in (0, 1, 2, 3, 6):
        for k in range(6 if st in (0, 1) else 2):
            edits = [(node + 4, st, 1), (e0 + 4, rng.choice((0, 1, 2, 3)), 1), (e0 + 1, rng.choice((0, 1, 1)), 1),
                     (node + 0xD, rng.choice((1, 2, 3)), 1), (e0, rng.choice((1, 1, 0)), 1),
                     (node + 0x36, rng.choice((0, 0x1000, 0x4000, 0x0100, -0x8000)), 2),
                     (e0 + 0x28, rng.choice((0, 1)), 2), (node + 0x1F0 + 0x1C, rng.choice((0, 0, 3)), 4),
                     (e0 + 0x1F0 + 0x1C, rng.choice((0, 0, 2)), 4), (e0 + 0x1F0 + 2, rng.choice((0, 1)), 2),
                     (node + 0x64, F(rng.choice((0.005, 0.01, 0.5, 0.0))), 4)]
            out.append((f'{"!" if k == 0 else ""}st={st} k={k}', edits, [node]))
    out += special_1BFFD0(node, e0)
    out += close_1BFFD0(node, e0)
    return out


def special_1BFFD0(node, e0):
    """Always-run 001BFFD0 cases (appended after the sampled ones):
    - +0xD = 0 in state 0 (the test is == 1) and 3 / 0xFF in state 1 (the
      test is == 2);
    - e0 half +0x28 = 0x100 and -0x8000 with +0xD = 2 (a half test);
    - scripted callees storing a field the routine reads after them:
      001EFE00 storing e2 +0x1E (before the word store 0x3C at e2 +0x1C),
      001BFF90 storing +0x36 (the hand-over flags), e2 +0x1C (the counter),
      +0x64 (state 2's fade) or node +0x20 (another node: the hand-over
      tests the e0 read at entry), and bone_init_default_2 storing +0xD."""
    e2 = node + 0x1F0
    st1 = [(node + 4, 1, 1), (e0 + 4, 1, 1), (e0 + 1, 1, 1), (node + 0xD, 1, 1), (e0, 1, 1)]
    out = [('!st=0 d=0', [(node + 4, 0, 1), (e0 + 4, 1, 1), (node + 0xD, 0, 1)], [node], {0x1B10B0: returns(0)})]
    for d in (3, 0xFF):
        out.append((f'!st=1 d={d:#x}', st1[:4] + [(node + 0xD, d, 1), (e0 + 0x28, 0, 2)], [node]))
    for h in (0x100, -0x8000):
        out.append((f'!st=1 d=2 e0+28={h:#x}', st1[:3] + [(node + 0xD, 2, 1), (e0 + 0x28, h, 2)], [node]))
    counters = [(e2 + 0x1C, 0, 4), (e0 + 0x1F0 + 0x1C, 0, 4)]
    out.append(('!st=1 001EFE00 stores e2+0x1E', st1 + counters + [(node + 0x36, 0x4000, 2)], [node],
                {0x1EFE00: answer(0, 0, [(e2 + 0x1E, 1, 2)])}))
    out.append(('!st=1 001BFF90 stores +0x36', st1 + counters + [(node + 0x36, 0, 2)], [node],
                {0x1BFF90: answer(0, 0, [(node + 0x36, 0x0800, 2)])}))
    out.append(('!st=1 001BFF90 stores e2+0x1C', st1 + counters + [(node + 0x36, 0, 2)], [node],
                {0x1BFF90: answer(0, 0, [(e2 + 0x1C, 5, 4)])}))
    out.append(('!st=1 001BFF90 stores +0x20', st1 + counters + [(node + 0x36, 0x0100, 2), (node, 2, 1)], [node],
                {0x1BFF90: answer(0, 0, [(node + 0x20, node, 4)])}))
    out.append(('!st=2 001BFF90 stores +0x64', [(node + 4, 2, 1), (node + 0x64, F(0.5), 4)], [node],
                {0x1BFF90: answer(0, 0, [(node + 0x64, F(0.005), 4)])}))
    out.append(('!st=0 001C63E0 stores +0xD', [(node + 4, 0, 1), (e0 + 4, 1, 1), (node + 0xD, 1, 1)], [node],
                {0x1B10B0: returns(0), 0x1C63E0: answer(0, 0, [(node + 0xD, 2, 1)])}))
    return out


def perturb_1CB360(img, regs, rng):
    """001CB360 hands 001D3F50 the node's +0x44 word read after the other
    three calls: scripted 001C7420 (the first) or 001D1F80 (the last) that
    store another word there (001D3F50 scripted too: it gets that word)."""
    (node,) = args_of(regs, SPECS[0x1CB360])
    other = (u32(img.ram, node + 0x44) ^ 0x00001230) & MASK
    return [(f'!{target:08X} stores +0x44', [], [node],
             {target: returns_f(0, [(node + 0x44, other, 4)]), 0x1D3F50: returns(0)})
            for target in (0x1C7420, 0x1D1F80)]


PERTURB.update({0x1BB860: perturb_1BB860, 0x1BB560: perturb_1BB560, 0x1C02E0: perturb_1C02E0,
                0x1BF630: perturb_1BF630, 0x1BFFD0: perturb_1BFFD0, 0x1CB360: perturb_1CB360})


# ----------------------------------------------------------------------
# Close-out cases (round 6): the independent sweep 3's survivors and the
# final review's fresh ones (docs/AREA01_MATH.md, section 3, "Round 6").
# All pinned ('!'): both modes. Scripted callees that store a field are the
# test device of `answer` (a translation that kept the old value differs;
# nothing is claimed about the real callee).
# ----------------------------------------------------------------------

def close_1C39F0(node, state):
    """T10: the post-step subtraction re-reads state +0x80..88 after the
    calls: a scripted 001026A0 (the step) that also stores state +0x80."""
    edits = [(state + 0xE4, (1 << 8) | 0x5, 4), (state + 0xDC, F(0.75), 4), (state + 0x80, F(0.25), 4)]
    out = [('!001026A0 stores state+0x80', edits, [node, state, F(1.0)],
            {0x1026A0: answer(0, 0, [(state + 0x80, F(2.0), 4)])})]
    # node +0xB0..B8 += the 001026A0 product (scripted to 0.3, -0.7, 1.1)
    # at node values where the EE sum differs from round-to-nearest.
    step = (F(0.3), F(-0.7), F(1.1))
    edits = [(state + 0xE4, 0x5, 4)]
    for k in range(3):
        edits.append((node + 0xB0 + 4 * k, rounding_values('add', step[k], -40.0, 40.0, 1, 0x39F2 + k)[0], 4))
    product = answer(0, 0, [(0x70003610 + 4 * k, step[k], 4) for k in range(3)])
    out.append(('!mode=0 add EE', edits, [node, state, F(2.0)], {0x1026A0: product}))
    return out


def close_1C3BE0(node, state):
    """T14: state +0x70..78 are read after the first 001029C0 (a scripted
    one that also stores state +0x70); T15: node +0xC4 is read after
    00102B08 (a scripted one that stores it)."""
    return [('!001029C0 stores state+0x70', [(state + 0x70, F(0.5), 4)], [node, state],
             {0x1029C0: answer(0, 0, [(state + 0x70, F(-2.5), 4)])}),
            ('!00102B08 stores +0xC4', [(node + 0xC4, F(-1.25), 4)], [node, state],
             {0x102B08: answer(0, 0, [(node + 0xC4, F(0.5), 4)])})]


def close_1C69A0(img, model):
    """T24: the second row pass stores output row j before it loads row
    j + 1 of 0x70003480. A one-bone model copy (scratchpad 0x70003C00)
    whose bone record is at 0x70003400, so the output bone +0x90 is
    0x70003490 = row 1 of 0x70003480. quat_nlerp / quat_to_mat3 /
    001029C0 / 00102C58 are scripted (no writes, except that quat_to_mat3
    stores MAX at 0x70003464, which the row scale by 1.0 keeps: bone +0x64
    then reads -1, the root path)."""
    copy, bone = 0x70003C00, 0x70003400
    edits = [(copy + 0x0C, 1, 1), (copy + 0x110, bone, 4)]
    edits += [(copy + 0x60 + 4 * i, F(1.0), 4) for i in range(4)]
    for i in range(16):
        edits.append((0x70003400 + 4 * i, F(0.25 * i - 1.0) if i % 4 != 3 else F(1.0), 4))
        edits.append((0x70003480 + 4 * i, F(0.5 * i - 2.75), 4))
    edits += [(0x70003418, F(1.0), 4), (0x7000341C, F(1.0), 4), (0x70003420, F(1.0), 4)]
    script = {QUAT_NLERP: answer(), QUAT_TO_MAT3: answer(0, 0, [(0x70003464, 0x7F7FFFFF, 4)]),
              0x1029C0: answer(), 0x102C58: answer()}
    return [('!bone at 0x70003400', edits, [copy], script)]


def close_1BB860(img, node):
    """T32: bit 0x80 of node +2 is read after length3's 0011E748 (a
    scripted one that stores it); T33: D_00810350 is read after 001C6380
    and the +0x4C method (scripted ones that move the point 100 units);
    Z04 / Z11: the sub-state 1 / 4 byte stores of 0 at +0xB / +5 with
    +0xA / +6 preset."""
    geometry = [(node + 0xB0, F(100.0), 4), (node + 0xB4, F(5.0), 4), (node + 0xB8, F(-50.0), 4),
                (0x810350, F(105.0), 4), (0x810354, F(5.0), 4), (0x810358, F(-50.0), 4)]
    st5 = [(node + 4, 1, 1), (node + 5, 5, 1), (node + 1, 0, 1)]
    away = [(0x810350, F(205.0), 4)]
    out = [('!0011E748 stores +2', st5 + [(node + 2, 0, 1)] + geometry, [node],
            {0x11E748: answer(0, F(1.0), [(node + 2, 0x80, 1)])}),
           ('!001C6380 moves the point', st5 + [(node + 2, 0, 1)] + geometry, [node],
            {0x1C6380: answer(0, 0, away)})]
    method = u32(img.ram, node + 0x4C)
    if method != 0x1C6380:
        out.append(('!method moves the point', st5 + [(node + 2, 0, 1)] + geometry, [node],
                    {method: answer(0, 0, away)}))
    out += [('!st=1/1 +0xA preset', [(node + 4, 1, 1), (node + 5, 1, 1), (node + 0xB, 4, 1), (node + 0xA, 0x5A, 1)],
             [node], {0x1BB7C0: answer(1)}),
            ('!st=1/4 +6 preset', [(node + 4, 1, 1), (node + 5, 4, 1), (node + 6, 0x5A, 1)],
             [node], {0x1BB7F0: answer(1)})]
    return out


def close_1BB560(img, node, a1, row):
    """001BB560 (the fabs test scripted to 0: +0x2E = 0, kind 0x40, no
    invert; area 0x16 cases use +0x34 = 5):
    - T01 / T30: a scripted 001BA1A0 that stores half +0x2E = 0x8000 /
      0x0100; the table index is the unsigned half (q + 0x8000 against
      q - 0x8000; q + 0x100 against q), the two bytes at opposite answers;
    - T31: row byte 2 (clear bit 0x80) over D_008106C8 = 0x00010080: bits
      16..31 stay;
    - T25 / T28 / T29: node +0xC4, p.A4 and node +0xB8 are read after
      001B1240 / 0011E2A8 / 0011DE90 (scripted ones that store them)."""
    local = random.Random(0xB563)
    base = [(node + 0xB, 4, 1), (node + 3, 0x40, 1)]
    area16 = base + [(0x810700, 0x16, 1), (node + 0x34, 5, 2), (0x8106C8, 0x40, 4)]
    q = row + 5 * 4
    fabs0 = {0x11DF78: returns_f(0)}
    out = []
    for half, hi, lo in ((0x8000, q + 0x8000, q - 0x8000), (0x0100, q + 0x100, q)):
        edits = area16 + [(hi, 1, 1), (lo, 2, 1)] + near_player(img, node, local, True)
        out.append((f'!001BA1A0 stores +0x2E={half:#x}', edits, [node, a1, 0],
                    {0x11DF78: returns_f(0), 0x1BA1A0: answer(0, 0, [(node + 0x2E, half, 2)])}))
    edits = base + [(0x810700, 0x16, 1), (node + 0x34, 5, 2), (0x8106C8, 0x00010080, 4), (q, 2, 1), (q + 1, 2, 1)]
    out.append(('!area16 clear over 0x10080', edits + near_player(img, node, local, True), [node, a1, 0], fabs0))
    near = near_player(img, node, local, True)
    out += [('!001B1240 stores +0xC4', base + [(node + 0xC4, F(-0.75), 4)] + near, [node, a1, 0],
             {0x1B1240: answer(0, F(0.3), [(node + 0xC4, F(1.0), 4)])}),
            ('!0011E2A8 stores p.A4', base + near, [node, a1, 0],
             {0x11E2A8: answer(0, F(0.5), [(0x8102B0 + 0xA4, F(5.0), 4)])}),
            ('!0011DE90 stores +0xB8', base + near, [node, a1, 0],
             {0x11DE90: answer(0, F(0.5), [(node + 0xB8, F(3.0), 4)])})]
    return out


def close_1BF630(img, player, node, tail):
    """T34: the limit (the float tail +0x18 points at) is read after
    0011E748: a scripted 0011E748 answering 15 that stores the limit 20
    over 10."""
    limit = u32(img.ram, tail + 0x18)
    return [('!0011E748 stores the limit', [(0x70003B8D, 0, 1), (limit, F(10.0), 4)], [player, node, tail],
             {0x11E748: answer(0, F(15.0), [(limit, F(20.0), 4)])})]


def close_1C02E0(img, node):
    """001C02E0 (the 001BF630 limit set to 1e6, near):
    - T36: the tail half store of 001D0D60's v0 (scripted 0x1234) over tail
      +0..1 = 0xA5A5;
    - T49: the scratchpad byte 0x70003B64 is read after the inline
      001BF630's 0011E748 (a scripted one that stores it = 1);
    - T37: D_0028A51C is read after bone_init_default_1 (a scripted
      001C62C0 that stores it);
    - Z15: the state-0 tail half store at +2 with tail +4..5 preset."""
    out = []
    limit = u32(img.ram, node + 0x1F0 + 0x18)
    if 0x100000 <= limit < 0x2000000:
        st1 = [(node + 4, 1, 1), (node + 5, 1, 1), (node + 0x36, 0, 2), (node + 0x28, 0, 2), (0x70003B8D, 0, 1),
               (0x70003B64, 0, 1), (limit, F(1e6), 4)]
        out += [('!st=1/1 001D0D60 0x1234 tail preset', st1 + [(node + 0x1F0, 0xA5A5, 2)], [node],
                 {0x1D0D60: answer(0x1234)}),
                ('!st=1/1 0011E748 stores b64', st1, [node],
                 {0x11E748: answer(0, F(1.0), [(0x70003B64, 1, 1)])})]
    companion = u32(img.ram, node + 0x24)
    if 0x100000 <= companion < 0x2000000:
        spawn = [(node + 4, 0, 1), (0x810845, 0, 1)]
        out += [('!st=0 001C62C0 stores 0x28A51C', spawn, [node],
                 {0x1AFA90: returns(companion), BONE_INIT_1: answer(0, 0, [(0x28A51C, 0x12345678, 4)])}),
                ('!st=0 spawn tail+4 preset', spawn + [(node + 0x1F4, 0xA5A5, 2)], [node],
                 {0x1AFA90: returns(companion)})]
    return out


BONE_INIT_1 = 0x1C62C0


def close_1BFFD0(node, e0):
    """001BFFD0:
    - T39: node +0x110 is read after bone_init_default_2 (a scripted
      001C63E0 that points it at a scratchpad record);
    - T40: e0 byte +1 = 0x80 (the test is == 0 on the unsigned byte);
    - T41: e0 byte +0 = 3 (a value the game stores) with +0xD = 0 and
      +0x36 = 0x0100: no hand-over (the test is == 1);
    - T50: +0xD = 0x11 in state 0 (the test is == 1 on the byte);
    - Z09: e0 state byte 3 in state 1 (the test is >= 2)."""
    st1 = [(node + 4, 1, 1), (e0 + 4, 1, 1), (e0 + 1, 1, 1), (node + 0xD, 1, 1), (e0, 1, 1)]
    counters = [(node + 0x1F0 + 0x1C, 0, 4), (e0 + 0x1F0 + 0x1C, 0, 4)]
    return [('!st=0 001C63E0 stores +0x110', [(node + 4, 0, 1), (e0 + 4, 1, 1), (node + 0xD, 2, 1)], [node],
             {0x1B10B0: returns(0), 0x1C63E0: answer(0, 0, [(node + 0x110, 0x70003D00, 4)])}),
            ('!st=1 e0+1=0x80', st1[:2] + [(e0 + 1, 0x80, 1)] + st1[3:] + counters, [node]),
            ('!st=1 e0=3 d=0 flags 0x100', st1[:3] + [(node + 0xD, 0, 1), (e0, 3, 1), (node + 0x36, 0x0100, 2)]
             + counters, [node]),
            ('!st=0 d=0x11', [(node + 4, 0, 1), (e0 + 4, 1, 1), (node + 0xD, 0x11, 1)], [node],
             {0x1B10B0: returns(0)}),
            ('!st=1 e0 state 3', [(node + 4, 1, 1), (e0 + 4, 3, 1), (e0 + 1, 1, 1)], [node])]


class ProbeScript:
    """Scripted boundaries for 001C2770's surface probes (the variant
    source): 001C2540 / 001C2690 answer from a fixed sequence of (hit,
    flag halfword, normal y); a hit stores the pointer of a synthetic hit
    record (scratchpad 0x70003C00: flags +0x1A, vector +0x24..2C) at
    0x700031D0, as the original probes leave their hit record there.
    The hit answer is the v0 itself: 2 or 4, the values the real probes
    return (001C2540 / 001C2690 return 0019B4C0's v0, which is 0, 2 or 4),
    so a translation testing a probe with `== 1` fails.
    0019AB20 answers from its own sequence (its real answers are 0, 2 and
    4 too; one case answers 0x80000000). Every answer and write is
    recorded as a boundary, so the native side replays exactly these. A
    probe answer may carry a fourth element, writes ((state-block offset,
    value, size)) the probe also makes, so a translation that kept a
    state field from before the probe, where the original re-reads it,
    would differ."""

    RECORD = 0x70003C00
    SECOND = 0x70003C40

    def __init__(self, probes, walls, switch=False, st=0):
        self.probes, self.walls, self.switch, self.st = list(probes), list(walls), switch, st

    def moved(self, ee):
        """With `switch`: 001C3DB0 (the commit's first callee) answers by
        pointing 0x700031D0 at a second record with another vector, so a
        translation that kept the record pointer from before the call,
        where the original re-reads it, would differ."""
        ee.save(self.SECOND + 0x1A, 0x1000, 2)
        for i, v in enumerate((-0.125, 1.0, 0.375)):
            ee.save(self.SECOND + 0x24 + 4 * i, F(v), 4)
        ee.save(0x700031D0, self.SECOND, 4)
        ee.r[2] = 0
        ee.f[0] = 0

    def probe(self, ee):
        answer = self.probes.pop(0) if self.probes else (0, 0, 0.0)
        hit, flags, ny = answer[:3]
        for offset, value, size in (answer[3] if len(answer) > 3 else ()):
            ee.save(self.st + offset, value, size)
        if hit:
            ee.save(self.RECORD + 0x1A, flags, 2)
            for i, v in enumerate((0.25, ny, -0.5)):
                ee.save(self.RECORD + 0x24 + 4 * i, F(v), 4)
            ee.save(0x700031D0, self.RECORD, 4)
        ee.r[2] = hit
        ee.f[0] = 0

    def wall(self, ee):
        ee.r[2] = FR.sx32((self.walls.pop(0) if self.walls else 0) & MASK)
        ee.f[0] = 0

    def table(self):
        out = {0x1C2540: self.probe, 0x1C2690: self.probe, 0x19AB20: self.wall}
        if self.switch:
            out[0x1C3DB0] = self.moved
        return out


def latch_cases():
    """Deterministic 001C2770 cases run in both modes: one or more per
    commit path (latch 1, 2, 3, 5, 9 and 0x11, the flag-1 early exit, the
    flag-4 slide push), sub-states >= 0x100 (the arithmetic >> 8 of +0xE4),
    the blend step landing exactly on 1.0 and one ulp either side, and
    phase-0 sub-state-4 speeds where pi * (56.25 * s) and (pi * 56.25) * s
    differ. Each is (label, phase, sub, flags, probes, walls, +0xD8, +0xF0,
    +0xD4, switch), optionally followed by edits ((base 'act' / 'st',
    offset, value, size)) and scripted callees ((target, writes, v0, f0),
    the writes relative to 'act' / 'st' like the edits). A probe hit
    answers 2 (h) or 4 (h4), the real probes' nonzero answers."""
    miss = (0, 0, 0.0)
    h = lambda flags, ny=1.0: (2, flags, ny)
    h4 = lambda flags, ny=1.0: (4, flags, ny)
    out = []
    for ny in (1.0, 0.8):
        hh = h if ny == 1.0 else h4     # both nonzero probe answers
        out += [
            ('latch1 sub0 8000', 0, 0, 0, [hh(0x8000, ny)], [], 1.0, -0.5),
            ('latch1 sub0 2800', 0, 0, 0, [hh(0x2800, ny)], [], 1.0, -0.5),
            ('latch1 sub1 keep', 0, 1, 1, [hh(0x5000, ny)], [], 1.0, -0.5),
            ('latch1 sub4 plain', 0, 4, 0, [hh(0x1000, ny)], [], 1.0, -0.5),
            ('latch2 sub1', 0, 1, 0, [miss, hh(0x1000, ny)], [], 1.0, -0.5),
            ('latch3 sub2 8000', 0, 2, 0, [miss, miss, hh(0x8000, ny)], [0], 1.0, -0.5),
            ('latch3 sub3 1000', 0, 3, 0, [miss, miss, hh(0x1000, ny)], [0], 1.0, -0.5),
            ('latch5 sub4 1000', 0, 4, 0, [miss, hh(0x1000, ny)], [], 1.0, -0.5),
            ('latch5 sub4 2800', 0, 4, 0, [miss, hh(0x2800, ny)], [], 1.0, -0.5),
            ('latch5 sub5', 0, 5, 0, [miss, hh(0x5000, ny)], [], 1.0, -0.5),
            ('latch5 sub6', 0, 6, 0, [miss, hh(0x4000, ny)], [], 1.0, -0.5),
            ('latch9 sub4', 0, 4, 2, [hh(0x1000, ny)], [], 1.0, -0.5),
            ('latch9 sub5', 0, 5, 2, [hh(0x5000, ny)], [], 1.0, -0.5),
            ('latch9 sub6 second', 0, 6, 2, [miss, hh(0x4000, ny)], [], 1.0, -0.5),
        ]
    out += [
        ('latch11 sub1', 0, 1, 0, [miss, miss, miss], [0], 1.0, -0.5),
        ('latch11 sub3', 0, 3, 0, [miss, miss, miss], [0], 1.0, -0.5),
        ('no latch wall', 0, 1, 0, [miss, miss], [2], 1.0, -0.5),
        ('no latch wall 4', 0, 1, 0, [miss, miss], [4], 1.0, -0.5),
        ('flag1 exit', 0, 3, 1, [h(0x2800), miss], [], 1.0, -0.5),
        ('flag4 slide sub4', 0, 4, 4, [h(0x2800), miss], [], 1.0, -0.5),
        ('flag4 8000 sub4', 0, 4, 4, [h(0x8000), miss], [], 1.0, -0.5),
        ('slide sub5', 0, 5, 0, [h(0x2800), miss], [], 1.0, -0.5),
    ]
    # Every surface-mask test site with only one bit of its mask: 0x2000 and
    # 0x0800 for the 0x2800 tests (phase-0 sub 0, the sub-2 keep, the sub
    # 1..3 and latch-3 +0xE4 commits, both sub-4 probes with and without
    # flag 4, the sub-5 / sub-6 slide push), 0x4000 and 0x1000 for the
    # 0x5000 tests (the sub-1 keep, both sub-5 / sub-6 probes), so a
    # translation that tests one bit of a two-bit mask differs.
    for bit in (0x2000, 0x0800):
        out += [
            (f'split {bit:#x} sub0', 0, 0, 0, [h(bit)], [], 1.0, -0.5),
            (f'split {bit:#x} sub2 keep', 0, 2, 1, [h(bit), miss], [], 1.0, -0.5),
            (f'split {bit:#x} sub1 commit', 0, 1, 0, [h(bit)], [], 1.0, -0.5),
            (f'split {bit:#x} latch3', 0, 2, 0, [miss, miss, h(bit)], [0], 1.0, -0.5),
            (f'split {bit:#x} sub4 first', 0, 4, 0, [h(bit)], [], 1.0, -0.5),
            (f'split {bit:#x} sub4 first flag4', 0, 4, 4, [h(bit), miss], [], 1.0, -0.5),
            (f'split {bit:#x} sub4 second', 0, 4, 0, [miss, h(bit)], [], 1.0, -0.5),
            (f'split {bit:#x} sub4 second flag4', 0, 4, 4, [miss, h(bit)], [], 1.0, -0.5),
            (f'split {bit:#x} sub5 slide', 0, 5, 0, [h(bit), miss], [], 1.0, -0.5),
            (f'split {bit:#x} sub6 slide', 0, 6, 0, [h(bit), miss], [], 1.0, -0.5),
        ]
    for bit in (0x4000, 0x1000):
        out += [
            (f'split {bit:#x} sub1 keep', 0, 1, 1, [h(bit), miss], [], 1.0, -0.5),
            (f'split {bit:#x} sub3 keep', 0, 3, 1, [h(bit), miss], [], 1.0, -0.5),
            (f'split {bit:#x} sub5 first', 0, 5, 0, [h(bit)], [], 1.0, -0.5),
            (f'split {bit:#x} sub6 first', 0, 6, 0, [h(bit)], [], 1.0, -0.5),
            (f'split {bit:#x} sub5 second', 0, 5, 0, [miss, h(bit)], [], 1.0, -0.5),
            (f'split {bit:#x} sub6 second', 0, 6, 0, [miss, h(bit)], [], 1.0, -0.5),
        ]
    # Sub-states 5 / 6 entered with +0xF0 exactly 0.04 / 0.06: the step
    # leaves exactly 0, so both `+0xF0 < 0` tests (after the first probe and
    # after the second) are false while the probe hits; and one ulp either
    # side (-0 and +0 are not both reachable here: x - x is +0). Landing
    # surfaces (0x5000, 0x4000, 0x1000) and the 0x2800 slide at the first
    # site, landing surfaces at the second, flags 0 and 2.
    for sub, k in ((5, 0x3D23D70A), (6, 0x3D75C28F)):
        for f0 in (k, k - 1, k + 1):
            tag = {k: '=', k - 1: '-1ulp', k + 1: '+1ulp'}[f0]
            for fl in ((0, 2) if f0 == k else (0,)):
                for bit in ((0x5000, 0x4000, 0x1000, 0x2800) if f0 == k else (0x5000, 0x2800)):
                    out.append((f'speed{tag} sub{sub} fl{fl} first {bit:#x}', 0, sub, fl, [h(bit), miss], [],
                                1.0, f0))
                for bit in ((0x5000, 0x1000) if f0 == k else (0x5000,)):
                    out.append((f'speed{tag} sub{sub} fl{fl} second {bit:#x}', 0, sub, fl, [miss, h(bit)], [],
                                1.0, f0))
    # Flag halfwords with both a high-priority and a lower bit (0x8000 with
    # 0x2800, 0x5000 with 0x2800) at every site that tests them in order:
    # the tests' order decides the phase.
    for flags in (0xA800, 0x8800, 0xA000):
        out += [
            (f'combo {flags:#x} sub0', 0, 0, 0, [h(flags)], [], 1.0, -0.5),
            (f'combo {flags:#x} sub1 commit', 0, 1, 0, [h(flags)], [], 1.0, -0.5),
            (f'combo {flags:#x} latch3', 0, 2, 0, [miss, miss, h(flags)], [0], 1.0, -0.5),
            (f'combo {flags:#x} sub4 first', 0, 4, 0, [h(flags)], [], 1.0, -0.5),
            (f'combo {flags:#x} sub4 first flag4', 0, 4, 4, [h(flags), miss], [], 1.0, -0.5),
        ]
    for flags in (0x7800, 0x1800, 0x6000):
        out += [(f'combo {flags:#x} sub5 first', 0, 5, 0, [h(flags), miss], [], 1.0, -0.5),
                (f'combo {flags:#x} sub6 first', 0, 6, 0, [h(flags), miss], [], 1.0, -0.5)]
    # +0xD8 = -0 and denormals: C.EQ.S sees them as 0 (the sub-state 1..3
    # break before any probe, and 001C39F0's early return), a bit compare
    # would not.
    for d8 in (0x80000000, 0x00000001, 0x807FFFFF):
        for sub in (1, 3):
            out.append((f'd8 {d8:#x} sub{sub}', 0, sub, 0, [h(0x1000)], [0], d8, -0.5))
    # Sub-state 4 speeds +0, -0, denormals and exponent-255 patterns, where
    # NEG.S (saturate, then flip the sign) and 0 - x differ; latch 0x11
    # itself stores +0xF0 = +0 with sub-state 4.
    for speed in (0x00000000, 0x80000000, 0x00000001, 0x80400000, 0x7F800000, 0x7FC00000, 0xFF800001):
        out.append((f'sub4 speed bits {speed:#x}', 0, 4, 0, [miss, miss], [], 1.0, speed))
    # Sub-states 5 / 6: the `+0xF0 < 0` tests re-read the speed after each
    # probe (the probes reach 0019B4C0 with the node, and the state block
    # lies inside the node). A probe that also stores +0xF0 with the other
    # sign: at the first test (entered at -0.5 / +0.5 so the step keeps the
    # sign) and at the second.
    for sub in (5, 6):
        for f0, other in ((-0.5, 0.5), (0.5, -0.5)):
            w = [(0xF0, F(other), 4)]
            out += [(f'reread F0 sub{sub} first {f0}', 0, sub, 0, [(2, 0x5000, 1.0, w), miss], [], 1.0, f0),
                    (f'reread F0 sub{sub} second {f0}', 0, sub, 0, [miss, (4, 0x5000, 1.0, w)], [], 1.0, f0)]
    out = [c + (0.2, False) for c in out]
    # The commit's `SPR 0x700038AC = 1.0` after 001026A0: that product's w
    # is 1.0 only for node matrices whose last column is (0, 0, 0, 1), so
    # these give node +0x10C = 2.0 (and +0xDC = 0.5).
    out += [('latch1 sub0 8000 w', 0, 0, 0, [h(0x8000)], [], 1.0, -0.5, 0.2, False,
             [('act', 0x10C, F(2.0), 4)]),
            ('latch5 sub4 1000 w', 0, 4, 0, [miss, h(0x1000)], [], 1.0, -0.5, 0.2, False,
             [('act', 0xDC, F(0.5), 4)])]
    # the hit record pointer re-read after 001C3DB0
    out += [('reread latch1', 0, 0, 0, [h(0x8000, 0.8)], [], 1.0, -0.5, 0.2, True),
            ('reread latch1 ny1', 0, 0, 0, [h(0x8000, 1.0)], [], 1.0, -0.5, 0.2, True),
            ('reread latch9', 0, 4, 2, [h(0x1000, 0.8)], [], 1.0, -0.5, 0.2, True)]
    for sub in (0x101, 0x104, 0x105, -0xFC):
        out.append((f'sub {sub:#x}', 0, sub, 0, [h(0x1000), h(0x8000)], [0], 1.0, -0.5, 0.2, False))
    # blend: 0x3F666667 + 0.1 gives exactly 1.0 in the EE model (checked
    # below), 0x3F666666 one ulp below, 0x3F666669 one ulp above.
    for phase in (1, 4, 6):
        for d4 in (0x3F666666, 0x3F666667, 0x3F666669):
            out.append((f'blend {phase} {d4:#x}', phase, 0, 0, [], [], 1.0, -0.5, d4, False))
    for speed in reorder_speeds():
        out.append((f'sub4 speed {speed:#x}', 0, 4, 0, [miss, miss], [], 1.0, speed, 0.2, False))
    # Sub-state 4 speed -2**-126: the act +0xC0 quotient t / 180 is below
    # the smallest normal (DIV.S flushes it to 0; checked in scripted_1C2770).
    out.append(('sub4 speed bits 0x80800000', 0, 4, 0, [miss, miss], [], 1.0, 0x80800000, 0.2, False))
    # flags 2 (a value direct callers pass): no keep test in sub-states 1..3
    # (only bit 0 selects it) and no slide at the sub-state-4 0x2800 hits
    # (only bit 2 selects it).
    out += [('flags2 sub1 8000', 0, 1, 2, [h(0x8000)], [], 1.0, -0.5, 0.2, False),
            ('flags2 sub3 1000', 0, 3, 2, [h4(0x1000)], [], 1.0, -0.5, 0.2, False),
            ('flags2 sub4 2800', 0, 4, 2, [h(0x2800), miss], [], 1.0, -0.5, 0.2, False),
            ('flags2 sub4 2800 second', 0, 4, 2, [miss, h4(0x2800)], [], 1.0, -0.5, 0.2, False)]
    # 0019AB20 answering 0x80000000 (the test is == 0).
    out.append(('wall 0x80000000', 0, 1, 0, [miss, miss], [0x80000000], 1.0, -0.5, 0.2, False))
    # Re-reads after callees (scripted callees that store the field):
    # - the blend value +0xD4 after 001C9D50 (0.95 stored over 0.5: the
    #   blend ends);
    # - +0xE6 stored by 001026A0 (the scripted one of 001C39F0 and of the
    #   commit) before latch 9's word store of 0x100 at +0xE4;
    # - the yaw sum act +0xC4 + 0011E620's answer on the latch 0x11 path:
    #   1.0 + 0x3E4CCCCD, where the EE sum truncates (checked in
    #   scripted_1C2770).
    out += [('blend reread D4', 1, 0, 0, [], [], 1.0, -0.5, 0.5, False, [],
             [(0x1C9D50, [('st', 0xD4, F(0.95), 4)], 0, 0)]),
            ('latch9 e6', 0, 4, 2, [h(0x1000)], [], 1.0, -0.5, 0.2, False, [],
             [(0x1026A0, [('st', 0xE6, 0x5A5A, 2)], 0, 0)]),
            ('yaw add', 0, 1, 0, [miss, miss, miss], [0], 1.0, -0.5, 0.2, False, [('act', 0xC4, F(1.0), 4)],
             [(0x11E620, [], 0, 0x3E4CCCCD)])]
    # The blend end's word store phase << 8 over an +0xE4 word with bits
    # 16..31 set (phase 1, sub-state 0x100).
    out.append(('blend e4 upper', 1, 0x100, 0, [], [], 1.0, -0.5, 0.95, False))
    # Round 6 (sweep 3): flags 2 at the sub-state-4 0x8000 site (T18: the
    # test is bit 2 only); flags 1 at the sub-state-6 second landing (T22:
    # the test is bit 1 only); act +0xC4 re-read after 0011E620 on the
    # latch-0x11 path (T17: a scripted 0011E620 that stores it).
    out += [('flags2 sub4 8000', 0, 4, 2, [h(0x8000)], [], 1.0, -0.5, 0.2, False),
            ('flags1 sub6 second 5000', 0, 6, 1, [miss, h(0x5000)], [], 1.0, -0.5, 0.2, False),
            ('yaw 0011E620 stores C4', 0, 1, 0, [miss, miss, miss], [0], 1.0, -0.5, 0.2, False,
             [('act', 0xC4, F(0.5), 4)], [(0x11E620, [('act', 0xC4, F(1.0), 4)], 0, F(0.2))])]
    # Round 6 (the round-5 kills that came from random perturbations, now
    # pinned): the -0.8 speed floor in sub-states 4, 5 and 6 (-0.79 steps
    # below it; node +0xB4 then adds the clamped speed); flags 1 at the
    # sub-state-4 landing (bit 1 only); phases 8 and 0xF (the phase is the
    # low 4 bits: no case).
    out += [(f'sub{sub} floor', 0, sub, 0, [miss, miss], [], 1.0, -0.79, 0.2, False) for sub in (4, 5, 6)]
    out += [('flags1 sub4 land 1000', 0, 4, 1, [h(0x1000)], [], 1.0, -0.5, 0.2, False),
            ('phase 8', 8, 0, 0, [miss, miss, miss], [0], 1.0, -0.5, 0.2, False),
            ('phase 0xF', 0xF, 0, 0, [miss, miss, miss], [0], 1.0, -0.5, 0.2, False)]
    return out


def reorder_speeds(count=3):
    """+0xF0 speeds s whose -s gives different bits for pi * (56.25 * t) and
    (pi * 56.25) * t in the EE model (fixed seed search)."""
    import ee_float_model as EM
    rng = random.Random(0x5625)
    out = []
    while len(out) < count:
        s_ = F(rng.uniform(-0.8, 0.8))
        t = EM.ee_neg(s_)
        if EM.ee_mul(0x40490FDB, EM.ee_mul(0x42610000, t)) != EM.ee_mul(EM.ee_mul(0x40490FDB, 0x42610000), t):
            out.append(s_)
    return out


def probe_sequences():
    """Answer sequences for 001C2770's probes: (probes, walls). A probe is
    (hit answer 0 / 2 / 4, flag halfword, normal y); the wall answers
    0019AB20 (0 or 2). Every
    surface class at every probe position the routine can reach."""
    miss = (0, 0, 0.0)
    surfaces = ((0x8000, 1.0), (0x2800, 1.0), (0x2000, 1.0), (0x0800, 0.8), (0x5000, 0.8), (0x1000, 1.0),
                (0x4000, 0.8), (0, 0.8))
    seqs = [([miss, miss, miss], [0]), ([miss, miss], [2])]
    for k, (flags, ny) in enumerate(surfaces):
        hit = (2 if k % 2 == 0 else 4, flags, ny)       # the probes' nonzero answers
        seqs += [([hit, hit], [0]), ([hit, miss], [0]), ([miss, hit], [0]), ([miss, miss, hit], [0])]
    return seqs


def scripted_1C2770(elf, beats, count):
    """001C2770 over the 00128C10 nodes' state blocks, every phase 0..15 /
    probe sub-state 0..8 (and -1) / flag set, with every scripted probe
    answer sequence (ProbeScript, probe_sequences), the +0xD8 distance 0 or
    not and the +0xF0 speed below or above 0 after its step."""
    n = 0
    seqs = probe_sequences()
    for beat in beats:
        ram, spad = beat_image(beat)
        img = Images(ram, spad)
        catches = []
        catch_route(elf, beat, {0x128C10, 0x12A5D0}, {0x1C2770},
                    lambda entry, r, sp, regs, where: catches.append((regs, where)), limit=2)
        if not catches:        # no 001C2770 caller node on this beat
            continue
        states = [(0, sub) for sub in list(range(9)) + [-1]] + [(phase, 0) for phase in range(1, 16)]
        cases = [(phase, sub, fl, q, d8, f0) for phase, sub in states for fl in range(8)
                 for q in range(len(seqs)) for d8 in (0.0, 1.0) for f0 in (-0.5, 0.2)]
        cases = [c for c in cases if c[0] == 0 or (c[3] == 0 and c[5] == 0.2)]
        first = lambda c: seqs[c[3]][0][0][:2]          # the first probe's (hit, flags)
        chosen = RM.select(cases, count, 0x2770 + len(beat),
                           axes=(lambda c: (c[0], c[1]), lambda c: (c[1], c[3]),
                                 lambda c: (c[1], c[2] & 1, c[4], first(c)), lambda c: (c[1], c[2], c[5])))
        if not RM.FULL:          # quick: the deterministic latch_cases only
            chosen = []
        import ee_float_model as EM
        assert EM.ee_add(0x3F666667, F(0.1)) == F(1.0) and EM.ee_add(0x3F666666, F(0.1)) < F(1.0) \
            and EM.ee_add(0x3F666669, F(0.1)) > F(1.0)
        # the sub-state 5 / 6 speed edges: exactly +0, just below, just above
        for k in (0x3D23D70A, 0x3D75C28F):
            assert EM.ee_sub(k, k) == 0 and EM.ee_sub(k - 1, k) >> 31 == 1 and EM.ee_sub(k - 1, k) & 0x7FFFFFFF \
                and EM.ee_sub(k + 1, k) >> 31 == 0 and EM.ee_sub(k + 1, k) != 0
        # sub-state 4 at speed -2**-126: t / 180 flushes to 0 in the EE model
        # (round-to-nearest binary32 would give a denormal); the yaw sum
        # 1.0 + 0x3E4CCCCD truncates (round-to-nearest gives 0x3F99999A).
        t = EM.ee_mul(0x40490FDB, EM.ee_mul(0x42610000, EM.ee_neg(0x80800000)))
        assert EM.ee_div(t, 0x43340000) == 0 and 0 < abs(struct.unpack('<f', struct.pack('<I', t))[0] / 180.0) < 2.0 ** -126
        assert EM.ee_add(F(1.0), 0x3E4CCCCD) == 0x3F999999
        for i, case in enumerate(latch_cases()):
            label, phase, sub, fl, probes, walls, d8, f0, d4, switch = case[:10]
            regs, where = catches[i % len(catches)]
            act, st, _ = args_of(regs, SPECS[0x1C2770])
            f0 = f0 if isinstance(f0, int) else F(f0)
            d4 = d4 if isinstance(d4, int) else F(d4)
            d8 = d8 if isinstance(d8, int) else F(d8)
            edits = [(st + 0xE4, ((sub << 8) | phase) & MASK, 4), (st + 0xD8, d8, 4),
                     (st + 0xF0, f0, 4), (st + 0xD4, d4, 4)]
            for base, offset, value, size in (case[10] if len(case) > 10 else ()):
                edits.append(({'act': act, 'st': st}[base] + offset, value, size))
            # distinct staged position words: the blend end copies the state's
            # matrix (+0x90..CF) over node +0xD0..10F, so node +0x100..108
            # then hold state +0xC0..C8, which it copies to node +0xB0..B8
            edits += [(st + 0xC0, F(1.25), 4), (st + 0xC4, F(-2.5), 4), (st + 0xC8, F(3.75), 4)]
            table = ProbeScript(probes, walls, switch, st).table()
            for target, writes, v0_, f0_ in (case[11] if len(case) > 11 else ()):
                table[target] = answer(v0_, f0_, [({'act': act, 'st': st}[b] + o, v, z) for b, o, v, z in writes])
            compare(elf, 0x1C2770, img, regs, f'scripted:{where}:{label}', [act, st, fl], edits, table)
            n += 1
        for i, (phase, sub, fl, q, d8, f0) in enumerate(chosen):
            regs, where = catches[i % len(catches)]
            act, st, _ = args_of(regs, SPECS[0x1C2770])
            probes, walls = seqs[q]
            edits = [(st + 0xE4, ((sub << 8) | phase) & MASK, 4), (st + 0xD8, F(d8), 4),
                     (st + 0xF0, F(f0), 4), (st + 0xD4, F(0.95 if i & 1 else 0.2), 4)]
            compare(elf, 0x1C2770, img, regs, f'scripted:{where}:{sub}/{phase} fl={fl} q={q}', [act, st, fl],
                    edits, ProbeScript(probes, walls).table())
            n += 1
    return n


def returns(value):
    """A scripted boundary answering v0 = value (f0 = 0), writing nothing."""
    def answer(ee):
        ee.r[2] = value & MASK
        ee.f[0] = 0
    return answer


def returns_f(bits_, writes=()):
    """A scripted boundary answering f0 = bits_ (v0 = 0) after storing
    `writes` ((EE address, value, size)); recorded and replayed like any
    callee, so the native side must re-read what it changes."""
    def answer(ee):
        for address, value, size in writes:
            ee.save(address, value, size)
        ee.r[2] = 0
        ee.f[0] = bits_ & MASK
    return answer


def answer(v0=0, f0=0, writes=()):
    """A scripted boundary storing `writes` ((EE address, value, size)) and
    answering v0 (sign-extended, as an EE word result) and f0. Recorded and
    replayed like any callee. Used to make a callee store a field the
    original reads again after the call (a test device: whether the real
    callee stores that field is not claimed)."""
    def run(ee):
        for address, value, size in writes:
            ee.save(address, value, size)
        ee.r[2] = FR.sx32(v0 & MASK)
        ee.f[0] = f0 & MASK
    return run


def per_call(answers):
    """A scripted boundary whose k-th call runs answers[k] (the last one
    for later calls): each is (v0, f0, writes) as for `answer`."""
    state = {'k': 0}

    def run(ee):
        v0, f0, writes = answers[min(state['k'], len(answers) - 1)]
        state['k'] += 1
        answer(v0, f0, writes)(ee)
    return run


def route_cases(elf, beats, owners, targets, node_limit=None, pin_only=None, route=True):
    """Catch every entry of `targets` while `owners` run over `beats`; each
    catch is compared as caught. Full mode: every perturbation of every
    catch. Quick mode: the pinned ('!') perturbations only, once per
    routine, at its first catch on the first quick beat (QUICK_BEATS[0]);
    the random perturbations run only in full mode."""
    rng = random.Random(0xA01)
    counts = {'route': 0, 'perturb': 0}
    pinned = set()
    current = {'beat': None}
    only = set(targets) if pin_only is None else set(pin_only)

    def on_catch(entry, ram, spad, regs, where):
        img = Images(ram, spad)
        if route:
            compare(elf, entry, img, regs, where)
            counts['route'] += 1
        make = PERTURB.get(entry)
        if not make:
            return
        if not RM.FULL:
            if current['beat'] != QUICK_BEATS[0] or entry in pinned or entry not in only:
                return
            pinned.add(entry)
        variants = make(img, regs, rng)
        if not RM.FULL:
            variants = [v for v in variants if v[0].startswith('!')]
        for variant in variants:
            label, edits, args = variant[:3]
            compare(elf, entry, img, regs, f'perturb:{where}:{label}', args, edits,
                    variant[3] if len(variant) > 3 else None)
            counts['perturb'] += 1
    for beat in beats:
        current['beat'] = beat
        catch_route(elf, beat, owners, targets, on_catch, node_limit=node_limit)
    return counts


def sweep_1B2140(elf, beat, part=0):
    """001B2140 over area bytes 0..23, sub 0..3, third byte 0..13, node
    rooms +0x9E 0..13 and +0x9D 0..3, and the 0x11 early exit."""
    ram, spad = beat_image(beat)
    node = pool(ram)[0]
    cases = [(area, sub, third, room, sub9d)
             for area in range(0, 24) for sub in range(4) for third in range(14)
             for room in range(14) for sub9d in range(4)]
    total = len(cases) + 5 + len(RANGE_1B2140)     # + the 0x11 exit, four exit words, the byte ranges
    # quick: the covering set only (every (area, sub, third), (area, third,
    # room) and (area, sub, +0x9D) combination once)
    cases = RM.select(cases, 0, 0x2140, axes=(lambda c: (c[0], c[1], c[2]), lambda c: (c[0], c[2], c[3]),
                                              lambda c: (c[0], c[1], c[4])))
    cases = cases[part::SWEEP_PARTS]
    img = Images(ram, spad)
    regs = regs_of(A01Base(elf, b'\0', b'\0'))
    for (area, sub, third, room, sub9d) in cases:
        edits = [(0x810700, area, 1), (0x810701, sub, 1), (0x810702, third, 1), (node + 0x9E, room, 1),
                 (node + 0x9D, sub9d, 1), (0x8104E0, 0, 4)]
        compare(elf, 0x1B2140, img, regs, f'unit:1B2140:{area}/{sub}/{third}/{room}/{sub9d}', [node], edits)
    extra = 0
    if part == 0:
        compare(elf, 0x1B2140, img, regs, 'unit:1B2140:0x11', [node], [(0x8104E0, 0x11, 4), (0x810700, 11, 1)])
        # the exit test compares the whole word with 0x11
        for word in (0x111, 0x1111, 0x80000011, 0x00110000):
            compare(elf, 0x1B2140, img, regs, f'unit:1B2140:{word:#x}', [node], [(0x8104E0, word, 4), (0x810700, 11, 1)])
        for (area, sub, third, room) in RANGE_1B2140:
            edits = [(0x810700, area, 1), (0x810701, sub, 1), (0x810702, third, 1), (node + 0x9E, room, 1),
                     (node + 0x9D, 0, 1), (0x8104E0, 0, 4)]
            compare(elf, 0x1B2140, img, regs, f'unit:1B2140:range {area:#x}/{sub:#x}/{third:#x}/{room:#x}',
                    [node], edits)
        extra = 5 + len(RANGE_1B2140)
    return len(cases) + extra, total


# 001B2140 bytes outside the swept ranges (area 0..23, sub 0..3, third and
# room 0..13), each against the other fields fixed so that a masked value
# (& 3, & 7, & 0xF, & 0x1F, & 0x3F, & 0x7F) lands on a case that answers
# differently: areas 0x2B / 0x4B / 0x8B (11 under a mask: always 1), 0x2D
# (13, room 8: 1); third bytes 0x15.. with area 0 and room 5; rooms 0x15..
# with area 0 and third 5; sub bytes 5.. with area 2 (sub 1: 0).
RANGE_1B2140 = ([(a, 0, 8, 8) for a in (0x17, 0x18, 0x2B, 0x2D, 0x4B, 0x8B, 0xFF)]
                + [(0, 0, t, 5) for t in (0x0E, 0x15, 0x25, 0x45, 0x85, 0xFF)]
                + [(0, 0, 5, r) for r in (0x0E, 0x15, 0x16, 0x25, 0x45, 0x85, 0xFF)]
                + [(2, sb, 0, 0) for sb in (4, 5, 9, 0x11, 0x21, 0x41, 0x81, 0xFF)])


def unit_bone_init(elf, beats):
    """The arrival-only 001D0C80 / 001D0D40 helpers. Run their original
    instructions over AREA01 records, with allocation callees scripted at
    the boundary. This checks signed capacity, byte widths, the count
    re-read after an allocator writes, and exact integer-to-EE-float words.
    Caller state-0 cases also execute both helpers inline in 001C02E0."""
    n = 0
    for beat in beats:
        ram, spad = beat_image(beat)
        img = Images(ram, spad)
        regs = regs_of(A01Base(elf, b'\0', b'\0'))
        node = next(p for p in pool(ram) if u32(ram, p + 0x10) == 0x1C02E0)
        model = u32(ram, node + 0x44)
        counter, slots = 0x70003900, 0x01E00000

        def allocator(change=None):
            def run(ee):
                i = ee.load(counter, 4)
                ee.save(counter, i + 1, 4)
                if i == 0 and change is not None:
                    ee.save(node + 0xC, change, 1)
                ee.r[2] = slots + i * 0x40
            return run

        counts = range(256) if RM.FULL else (0, 1, 2, 21, 127, 128, 255)
        cases = [(c, limit, None) for c in counts
                 for limit in (-32768, -1, c, c + 1, 32767)]
        cases += [(3, 4, 0), (1, 4, 3)]
        # The returned count is narrowed before testing capacity.
        cases += [(0x100, 1, None), (0x1FF, 256, None), (0xFFFFFFFF, 255, None)]
        for count, limit, change in cases:
            edits = [(0x275BCC, limit, 2), (0x275BCE, 0xA55A, 2),
                     (counter, 0, 4), (node + 4, 0xA1B2C3D4, 4),
                     (node + 8, 0xA1B2C3D4, 4), (node + 0xC, 0xA1B2C3D4, 4)]
            script = {0x1CA5E0: answer(writes=[(node + 0x44, model, 4)]),
                      0x1C6150: returns(count), 0x1AF780: allocator(change),
                      0x1CB5B0: returns(0)}
            compare(elf, 0x1D0C80, img, regs,
                    f'bone:{beat}:count={count:#x}:limit={limit}:change={change}',
                    [node, model], edits, script)
            n += 1

        frames = (0, 1, 91, 0xFFFFFFFF, 0x01000001, 0x7FFFFFFF, 0x80000000)
        if RM.FULL:
            rng = random.Random(0xD0D40)
            frames += tuple(rng.getrandbits(32) for _ in range(256))
        # Pointer alias: the first store may overwrite node+0x90 itself,
        # but every subsequent store still uses the original record pointer.
        records = (node + 0x1F0, node + 0x90, 0x70003800)
        for record in records:
            for frame in frames:
                for loop in ((0, 1, 0x100, 0xFFFFFF80) if RM.FULL else (0xFFFFFF80,)):
                    edits = [(node + 0x90, record, 4),
                             (record + 0xC, 0xA1B2C3D4, 4)]
                    compare(elf, 0x1D0D40, img, regs,
                            f'bone-bind:{beat}:record={record:#x}:frames={frame:#x}:loop={loop:#x}',
                            [node, 0x0024FD50, frame, loop], edits)
                    n += 1
    return n


def unit_1C25E0(elf, beats):
    """001C25E0 over the 00128C10 / 0012A5D0 nodes of each beat, with the
    vector a1 pointing at scratchpad words set to varied values."""
    n = 0
    rng = random.Random(0x25E0)
    for beat in beats:
        ram, spad = beat_image(beat)
        img = Images(ram, spad)
        regs = regs_of(A01Base(elf, b'\0', b'\0'))
        for node in pool(ram):
            if u32(ram, node + 0x10) not in (0x128C10, 0x12A5D0):
                continue
            vec = [F(rng.uniform(-20, 20)) for _ in range(3)] + [F(1.0)]
            edits = [(0x70003900 + 4 * i, v, 4) for i, v in enumerate(vec)]
            compare(elf, 0x1C25E0, img, regs, f'unit:{beat}:{node:#x}', [node, 0x70003900], edits)
            n += 1
    return n


def unit_script_ops(elf, beats, count):
    """001B9CF0 / 001BBAE0 / 001BBBF0 (script-op routines) called directly
    over the capture RAM: the node is a live pool node, the op record and
    the a1 block are scratchpad words (0x70003A00 / 0x70003900) filled with
    every lane index 0..15, targets equal to, near and far from the current
    values, the request-block states and every node kind byte."""
    rng = random.Random(0x9CF0)
    n = 0
    for beat in beats:
        ram, spad = beat_image(beat)
        img = Images(ram, spad)
        regs = regs_of(A01Base(elf, b'\0', b'\0'))
        nodes = pool(ram)
        cases = []
        for lane in range(16):
            for sub in (0, 1, 2):
                for how in ('equal', 'near', 'far'):
                    cases.append((lane, sub, how))
        # both axes: every (lane, how) and every (lane, sub), so the quick run
        # keeps e.g. lanes 11..13 with sub-state byte 2 (no 001B12B0 call)
        for lane, sub, how in RM.select(cases, count, 0x9CF0 + len(beat),
                                        axes=(lambda c: (c[0], c[2]), lambda c: (c[0], c[1]))):
            node = rng.choice(nodes)
            cur = lambda a: struct.unpack_from('<f', ram, a & 0x1FFFFFF)[0]
            edits = [(0x70003A08, lane, 4), (0x70003A0C, F(rng.choice((0.1, 0.5, 2.0))), 4), (0x70003904, sub, 1),
                     (0x70003A04, F(rng.uniform(-3, 3)), 4)]
            for i in range(8):           # op +0x10..+0x2C and a1 +0x30..+0x4C
                edits.append((0x70003A10 + 4 * i, F(rng.uniform(-3, 3)), 4))
                edits.append((0x70003930 + 4 * i, F(rng.uniform(-50, 50)), 4))
            if 4 <= lane <= 6 and how != 'far':
                v = cur(node + 0xB0 + 4 * lane)
                edits.append((0x70003930 + 4 * lane, F(v if how == 'equal' else v + 0.05), 4))
            if 7 <= lane <= 9 and how != 'far':
                v = cur(0x810354 + 4 * lane)
                edits.append((0x70003A04 + 4 * lane, F(v if how == 'equal' else v + 0.05), 4))
            base = {0: node + 0xC0, 1: node + 0xC4, 2: node + 0xC8}.get(lane)
            if base is not None:
                v = cur(base)
                t = v if how == 'equal' else v + (0.05 if how == 'near' else 9.0)
                edits.append((0x70003A20 + 4 * lane, F(t), 4))
            if lane == 3:
                for i in range(3):
                    v = cur(node + 0xC0 + 4 * i)
                    arrive = how == 'equal' or (how == 'near' and i != sub)
                    edits.append((0x70003A20 + 4 * i, F(v if arrive else v + 3.0), 4))
            if 11 <= lane <= 13:
                v = cur(node + 0x94 + 4 * lane)
                edits.append((0x70003A10, F(v if how == 'equal' else v + 0.3), 4))
            compare(elf, 0x1B9CF0, img, regs, f'unit:{beat}:9CF0:{lane}/{sub}/{how}', [node, 0x70003900, 0x70003A00],
                    edits)
            n += 1
        # Lane 3 with exactly one axis not arriving (x, y or z), always run:
        # each of the three arrival compares decides the result on its own.
        local = random.Random(0x39CF0)
        for miss in (0, 1, 2):
            node = local.choice(nodes)
            edits = [(0x70003A08, 3, 4), (0x70003A0C, F(0.5), 4), (0x70003904, 1, 1)]
            for i in range(3):
                v = struct.unpack_from('<f', ram, (node + 0xC0 + 4 * i) & 0x1FFFFFF)[0]
                edits.append((0x70003A20 + 4 * i, F(v + 3.0 if i == miss else v), 4))
            compare(elf, 0x1B9CF0, img, regs, f'unit:{beat}:9CF0:3 only axis {miss} misses',
                    [node, 0x70003900, 0x70003A00], edits)
            n += 1
        # Lane words with bits above 15 or equal to 10 / 14 (the lane is the
        # whole word: these take the default, no call), and a1 blocks at the
        # last scratchpad / RAM words (lane 6 reads a1 +0x48, the last word).
        for lane in (0x10000, 0x10003, 0x10004, 0x10007, 0x1000B, 0x80000000, 0xFFFFFFFF, 10, 14):
            node = local.choice(nodes)
            edits = [(0x70003A08, lane, 4), (0x70003A0C, F(0.5), 4), (0x70003904, 1, 1)]
            compare(elf, 0x1B9CF0, img, regs, f'unit:{beat}:9CF0:lane word {lane:#x}',
                    [node, 0x70003900, 0x70003A00], edits)
            n += 1
        for a1 in (0x70003FB4, 0x01FFFFB4):
            node = local.choice(nodes)
            edits = [(0x70003A08, 6, 4), (0x70003A0C, F(0.5), 4), (a1 + 4, 1, 1), (a1 + 0x48, F(1.25), 4)]
            compare(elf, 0x1B9CF0, img, regs, f'unit:{beat}:9CF0:a1 at {a1:#x}', [node, a1, 0x70003A00], edits)
            n += 1
        # The lane index is re-read from op +8 after the 001B12B0 call: a
        # scripted step that also changes op +8 (and answers the target, so
        # the arrival compare runs on the re-read lane) shows it.
        for lane, moved in ((0, 2), (1, 0), (5, 4), (8, 9), (12, 11)):
            node = rng.choice(nodes)
            edits = [(0x70003A08, lane, 4), (0x70003904, 1, 1), (0x70003A0C, F(0.5), 4)]
            for i in range(8):
                edits.append((0x70003A10 + 4 * i, F(0.25 * i - 1.0), 4))
                edits.append((0x70003930 + 4 * i, F(0.5 * i), 4))
            target = {0: 0x70003A20, 1: 0x70003A20, 2: 0x70003A20, 4: 0x70003930, 5: 0x70003930,
                      6: 0x70003930, 7: 0x70003A04, 8: 0x70003A04, 9: 0x70003A04}
            if moved in target:
                edits.append((target[moved] + 4 * moved, F(0.75), 4))
            else:
                edits.append((0x70003A10, F(0.75), 4))
            script = {0x1B12B0: returns_f(F(0.75), [(0x70003A08, moved, 4)])}
            compare(elf, 0x1B9CF0, img, regs, f'unit:{beat}:9CF0:reread {lane}->{moved}',
                    [node, 0x70003900, 0x70003A00], edits, script)
            n += 1
        for latch in (0, 1, 2):
            for kind in (0, 1, 2, 3, 4, 5, 6, 0x3F, 0x45, 0xC2, 0x20, 0x25):   # 0x20 / 0x25: >= 6 only under & 0x3F
                for status in (1, 2):
                    node = rng.choice(nodes)
                    edits = [(0x70003904, latch, 1), (node + 0x56, kind, 1), (0x2821B4, status, 4)]
                    compare(elf, 0x1BBAE0, img, regs, f'unit:{beat}:BAE0:{latch}/{kind}/{status}',
                            [node, 0x70003900], edits)
                    n += 1
        for status in (0x102, 0x10002, 0x80000002):     # the poll compares the whole word with 2
            node = local.choice(nodes)
            edits = [(0x70003904, 1, 1), (node + 0x56, 0, 1), (0x2821B4, status, 4)]
            compare(elf, 0x1BBAE0, img, regs, f'unit:{beat}:BAE0:1/0/{status:#x}', [node, 0x70003900], edits)
            n += 1
        for k in range(6):
            node = rng.choice(nodes)
            edits = [(node + 0xC4, F(rng.uniform(-3.2, 3.2)), 4), (0x810374, F(rng.uniform(-3.2, 3.2)), 4)]
            compare(elf, 0x1BBBF0, img, regs, f'unit:{beat}:BBF0:{k}', [node], edits)
            n += 1
        # node +0xB0 / +0xB8 near 0: the products 8 cos / 13 sin are then not
        # absorbed by a large position, so a product rounded differently
        # (host instead of EE) shows in the stored point.
        for k in range(6):
            node = rng.choice(nodes)
            edits = [(node + 0xC4, F(rng.uniform(-3.2, 3.2)), 4), (0x810374, F(rng.uniform(-3.2, 3.2)), 4),
                     (node + 0xB0, F(rng.uniform(-0.5, 0.5)), 4), (node + 0xB8, F(rng.uniform(-0.5, 0.5)), 4)]
            compare(elf, 0x1BBBF0, img, regs, f'unit:{beat}:BBF0:small {k}', [node], edits)
            n += 1
        # A scripted 0011DE90 (both cosines) answering denormals whose
        # product by 8 is normal (2**-129 * 8 = 2**-126) and exponent-255
        # patterns, at node +0xB0 / +0xB8 = 0: MUL.S flushes the denormal
        # operand (and saturates), a round-to-nearest product would not.
        for t in (0x00100000, 0x80100000, 0x7F800000, 0x7FC00000):
            node = local.choice(nodes)
            edits = [(node + 0xB0, 0, 4), (node + 0xB8, 0, 4)]
            compare(elf, 0x1BBBF0, img, regs, f'unit:{beat}:BBF0:cos {t:#x}', [node], edits,
                    {0x11DE90: returns_f(t)})
            n += 1
        n += fixed_script_ops(elf, img, regs, local.choice(nodes))
    return n


def fixed_script_ops(elf, img, regs, node):
    """Always-run script-op cases (fixed values):
    - 001B9CF0 lanes 11 and 13 with a1 byte +5 preset (the byte store of
      sub + 1 at a1 +4);
    - 001B9CF0 lane 0 with the node placed so that node +0xC0 is op +8: the
      stored step result (scripted 001B12B0 answer 0x00000001) becomes the
      lane word, and the arrival compare reads node +0xC0 + 4 * 1 = op +0xC
      (0.5) against op +0x24 (0.5): the original re-reads both;
    - 001BBAE0's first call with D_002821BC and st byte +5 preset (the word
      store of 0 and the byte store of 1);
    - 001BBAE0 with st +4 at 0x20000000, 0x30000000 (the RAM mirrors' first
      byte) and 0x70000000 (the scratchpad's first byte);
    - 001BBBF0 with a scripted 0011E2A8 that also stores node +0xB4 or SPR
      0x700038A0: both are read again after the sines."""
    n = 0
    op, a1 = 0x70003A00, 0x70003900
    for lane in (11, 13):
        edits = [(op + 8, lane, 4), (op + 0xC, F(0.5), 4), (a1 + 4, 0, 1), (a1 + 5, 0x77, 1)]
        compare(elf, 0x1B9CF0, img, regs, f'unit:9CF0:{lane} a1+5 preset', [node, a1, op], edits)
        n += 1
    alias = op + 8 - 0xC0
    edits = [(op + 8, 0, 4), (op + 0xC, F(0.5), 4), (op + 0x20, F(0.75), 4), (op + 0x24, F(0.5), 4)]
    compare(elf, 0x1B9CF0, img, regs, 'unit:9CF0:0 node+0xC0 = op+8', [alias, a1, op], edits,
            {0x1B12B0: returns_f(1)})
    n += 1
    edits = [(a1 + 4, 0, 1), (a1 + 5, 0x33, 1), (node + 0x56, 2, 1), (0x2821BC, 0x01020304, 4)]
    compare(elf, 0x1BBAE0, img, regs, 'unit:BAE0:0 presets', [node, a1], edits)
    n += 1
    for st in (0x1FFFFFFC, 0x2FFFFFFC, 0x6FFFFFFC):
        edits = [(st + 4, 0, 1), (node + 0x56, 1, 1)]
        compare(elf, 0x1BBAE0, img, regs, f'unit:BAE0:st {st:#x}', [node, st], edits)
        n += 1
    for label, where_ in (('+0xB4', node + 0xB4), ('0x700038A0', 0x700038A0)):
        compare(elf, 0x1BBBF0, img, regs, f'unit:BBF0:0011E2A8 stores {label}', [node], [],
                {0x11E2A8: answer(0, F(0.5), [(where_, F(3.0), 4)])})
        n += 1
    # Round 6 (sweep 3 / final review):
    # - T44: lanes 11..13 sub-state 0 re-read op +8 after the a1 +4 byte
    #   store: a1 = op + 5, so that store turns lane 11 into 0x10B (the
    #   original then reads node +0x94 + 0x42C and op +0x420);
    # - Z10: a1 byte +4 = 3 (the test is == 1: return 0, no step);
    # - T47: 001BBBF0 reads D_00810374 again for the cosine: a scripted
    #   0011E2A8 that stores it on its second call;
    # - T48: SPR 0x700038A8 is re-read after the second 0011DE90 (a
    #   scripted one that stores it).
    edits = [(op + 8, 11, 4), (op + 0xC, F(0.5), 4), (node + 0x94 + 0x42C, F(1.5), 4), (op + 0x420, F(0.25), 4),
             (node + 0xC0, F(-2.0), 4), (op + 0x20, F(0.75), 4)]
    compare(elf, 0x1B9CF0, img, regs, 'unit:9CF0:11 a1 = op+5', [node, op + 5, op], edits)
    edits = [(op + 8, 11, 4), (op + 0xC, F(0.5), 4), (a1 + 4, 3, 1)]
    compare(elf, 0x1B9CF0, img, regs, 'unit:9CF0:11 sub 3', [node, a1, op], edits)
    compare(elf, 0x1BBBF0, img, regs, 'unit:BBF0:0011E2A8 stores 374 on call 2', [node], [(0x810374, F(-0.5), 4)],
            {0x11E2A8: per_call([(0, F(0.5), []), (0, F(0.25), [(0x810374, F(1.0), 4)])])})
    compare(elf, 0x1BBBF0, img, regs, 'unit:BBF0:0011DE90 stores 0x700038A8', [node], [],
            {0x11DE90: answer(0, F(0.5), [(0x700038A8, F(7.0), 4)])})
    return n + 4


def unit_player(elf, beats, part):
    """00183250 / 00187DE0 / 00187EC0 called directly on the player record
    D_008102B0 of the capture: every phase 0..4 with the timer at 0, 1, 40
    and -1 and +0x38 around the decay step; 00187DE0 for +0x23C bytes 0..3
    and the area byte 0x15; 00187EC0 with byte arguments. `part` 0 runs
    phases 0..2 and the scripted clip answers, part 1 the rest (two jobs)."""
    n = 0
    p = 0x8102B0
    for beat in beats:
        ram, spad = beat_image(beat)
        img = Images(ram, spad)
        regs = regs_of(A01Base(elf, b'\0', b'\0'))
        # The scratchpad byte 0x70003B8D (cleared by phase 3 at timer 0) and
        # its neighbours start nonzero, so the clear is observable.
        # The half after the timer (+0x2A..2B) is nonzero too, so a timer
        # store (50, 30 or the decremented value) or read of another width
        # differs.
        spr = [(0x70003B8C, 0x5A, 1), (0x70003B8D, 0xA5, 1), (0x70003B8E, 0x3C, 1), (p + 0x2A, 0xA55A, 2)]
        # Quick mode: phases 1..3 with every timer, phase 3 (the only one
        # that reads +0x38) with every +0x38 value, phases 0 and 4 (which
        # read neither) once.
        grid = [(phase, timer, s38) for phase in ((0, 1, 2) if part == 0 else (3, 4))
                for timer in (0, 1, 40, -1) for s38 in (0.3, 0.005, 0x3C3A2E8C)]
        if not RM.FULL:
            grid = [c for c in grid if c[0] in (1, 2, 3) or c[1:] == (0, 0.3)]
        grid = RM.select(grid, 0, 0x3250, axes=(lambda c: (c[0], c[1]), lambda c: (c[0] == 3, c[2])))
        for phase, timer, s38 in grid:
            edits = spr + [(p + 6, phase, 1), (p + 0x28, timer, 2),
                           (p + 0x38, s38 if isinstance(s38, int) else F(s38), 4)]
            compare(elf, 0x183250, img, regs, f'unit:{beat}:183250:{phase}/{timer}/{s38}', [p], edits)
            n += 1
        # Phase 3 at timer 0 stores +4 = 1, +5 = +6 = +0x1F0 = 0: those bytes
        # start at other values, and so do +0x1F1..1F3 (the store is one
        # byte); timer 1: the same presets, no reset.
        for timer in ((0, 1) if part == 1 else ()):
            edits = spr + [(p + 6, 3, 1), (p + 0x28, timer, 2), (p + 4, 0, 1), (p + 5, 1, 1),
                           (p + 0x1F0, 0xA5A5A5A5, 4)]
            compare(elf, 0x183250, img, regs, f'unit:{beat}:183250:reset presets {timer}', [p], edits)
            n += 1
        # 0017B490 answering values whose low half is negative or whose
        # upper half is set: the routine passes 001749A0 the s16 of it
        # (001749A0 is scripted too: the original would index its clip
        # table with these made-up values).
        for clip in ((0x00018005, 0xFFFF7FFF, 0x0000FFFF) if part == 0 else ()):
            compare(elf, 0x183250, img, regs, f'unit:{beat}:183250:clip {clip:#x}', [p],
                    spr + [(p + 6, 0, 1)], {0x17B490: returns(clip), 0x1749A0: returns(0)})
            n += 1
        # +0xB4 values whose step by -0.2 rounds differently in the EE model.
        # Byte +0x235 >= 0x80 (passed zero-extended to 0017B490, scripted
        # here with 001749A0).
        for byte in ((0x80, 0xFF) if part == 0 else ()):
            compare(elf, 0x183250, img, regs, f'unit:{beat}:183250:235={byte:#x}', [p],
                    spr + [(p + 6, 0, 1), (p + 0x235, byte, 1)], {0x17B490: returns(5), 0x1749A0: returns(0)})
            n += 1
        # Phase 3 re-reads +0x38 after the move: a scripted 00178B90 that
        # stores another +0x38 (0.001, which the decay takes below 0; and
        # 0.3 over a preset 0.001).
        for before, after in (((0.3, 0.001), (0.001, 0.3)) if part == 1 else ()):
            compare(elf, 0x183250, img, regs, f'unit:{beat}:183250:move stores +0x38 {after}', [p],
                    spr + [(p + 6, 3, 1), (p + 0x28, 40, 2), (p + 0x38, F(before), 4)],
                    {0x178B90: returns_f(0, [(p + 0x38, F(after), 4)])})
            n += 1
        for b4 in (rounding_values('add', 0xBE4CCCCD, -50.0, 50.0, 2, 0x00B4) if part == 1 else ()):
            compare(elf, 0x183250, img, regs, f'unit:{beat}:183250:b4 {b4:#x}', [p],
                    spr + [(p + 6, 4, 1), (p + 0xB4, b4, 4)])
            n += 1
        for byte in ((1, 2, 0, 3) if part == 1 else ()):
            for area in (None, 0x15):
                edits = [(p + 0x23C, byte, 1)] + ([(0x810700, area, 1)] if area else [])
                compare(elf, 0x187DE0, img, regs, f'unit:{beat}:187DE0:{byte}/{area}', [p], edits)
                n += 1
        for next_ in ((1, 0xFF) if part == 1 else ()):     # the area test reads one byte
            compare(elf, 0x187DE0, img, regs, f'unit:{beat}:187DE0:area 0x15 next {next_:#x}', [p],
                    [(0x810700, 0x15, 1), (0x810701, next_, 1)])
            n += 1
        for a0, a1 in (((7, 0), (0x1FF, 0x80), (0, 0xFFFFFFFF)) if part == 1 else ()):
            compare(elf, 0x187EC0, img, regs, f'unit:{beat}:187EC0:{a0}/{a1}', [a0, a1])
            n += 1
        n += fixed_player(elf, img, regs, spr, part)
    return n


def fixed_player(elf, img, regs, spr, part):
    """Always-run player-record cases (fixed values):
    - part 0: phase 0 with +0x25D preset (the byte store of 2 at +0x25C);
    - part 1: phase 3 decay at +0x38 values where the EE subtraction of
      0.01137 truncates differently from round-to-nearest (0x3E800000 and
      two found by search; the move 00178B90 scripted to store nothing);
      a scripted 00178B90 (phase 2) that stores +0xB4 (read after it);
      00187DE0 with a scripted 001EFD90 that stores +0x23C = 1 over 0, or
      the area byte 0x15 over 0x14 (both read after it)."""
    import ee_float_model as EM
    f32 = lambda b: struct.unpack('<f', struct.pack('<I', b))[0]
    p, n = 0x8102B0, 0
    if part == 0:
        compare(elf, 0x183250, img, regs, 'unit:183250:25D preset', [p], spr + [(p + 6, 0, 1), (p + 0x25D, 0x44, 1)])
        # Round 6 (sweep 3): the byte stores +7 = 0 (phase 0, T53) and +6 += 1
        # (phase 1 at timer 0, T56) with the next byte preset.
        compare(elf, 0x183250, img, regs, 'unit:183250:ph0 +8 preset', [p], spr + [(p + 6, 0, 1), (p + 8, 0x5A, 1)])
        compare(elf, 0x183250, img, regs, 'unit:183250:ph1 +7 preset', [p],
                spr + [(p + 6, 1, 1), (p + 0x28, 0, 2), (p + 7, 0x5A, 1)])
        return 3
    # Round 6 (sweep 3): the phase-3 reset's byte store +6 = 0 with +7 preset (T52).
    compare(elf, 0x183250, img, regs, 'unit:183250:ph3 reset +7 preset', [p],
            spr + [(p + 6, 3, 1), (p + 0x28, 0, 2), (p + 7, 0x5A, 1)])
    # Round 6: +0x38 exactly the decay step (the decay gives 0: the test is
    # < 0, so no clamp and no 00174A50).
    compare(elf, 0x183250, img, regs, 'unit:183250:decay to 0', [p],
            spr + [(p + 6, 3, 1), (p + 0x28, 1, 2), (p + 0x38, 0x3C3A2E8C, 4)])
    n += 2
    assert EM.ee_sub(0x3E800000, 0x3C3A2E8C) != F(f32(0x3E800000) - f32(0x3C3A2E8C))
    for s38 in [0x3E800000] + rounding_values('sub', 0x3C3A2E8C, 0.02, 1.0, 2, 0x3250):
        compare(elf, 0x183250, img, regs, f'unit:183250:decay {s38:#x}', [p],
                spr + [(p + 6, 3, 1), (p + 0x28, 40, 2), (p + 0x38, s38, 4)], {0x178B90: answer()})
        n += 1
    compare(elf, 0x183250, img, regs, 'unit:183250:move stores +0xB4', [p],
            spr + [(p + 6, 2, 1), (p + 0x28, 40, 2), (p + 0xB4, F(1.0), 4)],
            {0x178B90: answer(0, 0, [(p + 0xB4, F(5.0), 4)])})
    n += 1
    compare(elf, 0x187DE0, img, regs, 'unit:187DE0:001EFD90 stores +0x23C', [p], [(p + 0x23C, 0, 1)],
            {0x1EFD90: answer(0, 0, [(p + 0x23C, 1, 1)])})
    compare(elf, 0x187DE0, img, regs, 'unit:187DE0:001EFD90 stores area 0x15', [p], [(0x810700, 0x14, 1)],
            {0x1EFD90: answer(0, 0, [(0x810700, 0x15, 1)])})
    return n + 2


def conditional_branches(elf, entry):
    """Addresses of the conditional branches in the routine's own range."""
    ee = EE(elf)
    out = []
    for pc in range(entry, entry + SPECS[entry].size, 4):
        word = ee.load(pc)
        op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
        if op == 4 and rs == 0 and rt == 0:
            continue
        if op in (4, 5, 6, 7, 20, 21, 22, 23) or (op == 1 and rt & 0xF in (0, 1, 2, 3)) or (op == 17 and rs == 8):
            out.append(pc)
    return out


ACTOR_OWNERS = {0x128C10, 0x12A5D0}
ACTOR_TARGETS = {0x1B13F0, 0x1B2140, 0x1C2770, 0x1C3D60, 0x1C69A0, 0x1C25E0, 0x1C39F0, 0x1C3BE0}
OWNER_OWNERS = {0x1BB860, 0x1C02E0, 0x1BFFD0}
OWNER_TARGETS = {0x1BB860, 0x1BB560, 0x1C02E0, 0x1BF630, 0x1BFFD0, 0x1CB360}
# Quick mode runs the first beat's pinned owner cases in two jobs (wall time).
OWNER_PIN_B = {0x1BFFD0}
OWNER_PIN_A = OWNER_TARGETS - OWNER_PIN_B


def run_job(job):
    """One independent piece of the run (forked worker): returns its
    counts and the per-routine statistics."""
    kind, arg = job
    STATS.clear()
    POISONED.clear()
    SKIPPED.clear()
    counts = {}
    if kind == 'actor':
        c = route_cases(ELF, [arg], ACTOR_OWNERS, ACTOR_TARGETS, node_limit=RM.pick(None, 2))
        counts = {'actor route': c['route'], 'actor perturb': c['perturb']}
    elif kind == 'owner':
        c = route_cases(ELF, [arg], OWNER_OWNERS, OWNER_TARGETS, pin_only=OWNER_PIN_A)
        counts = {'owner route': c['route'], 'owner perturb': c['perturb']}
    elif kind == 'owner2':      # quick mode: the rest of the first beat's pinned owner cases
        c = route_cases(ELF, [arg], OWNER_OWNERS, OWNER_TARGETS, pin_only=OWNER_PIN_B, route=False)
        counts = {'owner perturb': c['perturb']}
    elif kind == 'script':
        counts = {'script-op unit': unit_script_ops(ELF, [arg], RM.pick(10**6, 48))}
    elif kind == 'player':
        counts = {'player unit': unit_player(ELF, [arg[0]], arg[1])}
    elif kind == 'probe':
        counts = {'scripted 001C2770': scripted_1C2770(ELF, [arg], RM.pick(10**6, 90))}
    elif kind == '25e0':
        counts = {'001C25E0 unit': unit_1C25E0(ELF, [arg])}
    elif kind == 'bone':
        counts = {'bone init/bind unit': unit_bone_init(ELF, [arg])}
    elif kind == 'sweep':
        n, total = sweep_1B2140(ELF, 'a01_00_train_room', part=arg)
        counts = {'001B2140 table': n, '001B2140 table total': total}
    return counts, dict(STATS), list(SKIPPED)


SWEEP_PARTS = 4


def core_contract():
    """The view's stated contract (em_area01_math_core.h, docs/AREA01_MATH.md
    section 1), checked natively (no original code involved):
    - codes and addresses: an unmapped address latches ADDRESS at it; a NULL
      scratchpad or RAM view latches NULL at the address; a missing
      dispatcher latches NULL at the callee's address; a negative
      dispatcher result latches WORKER at the callee's address;
    - the first fault is the one kept (a dispatcher callback that faults the
      same view, then returns -1, leaves ADDRESS at its address);
    - after a fault no store is made (memory, the store count and the trace
      unchanged), loads read 0, no dispatcher is called, and a routine
      returns -1; stores made before the fault stay; a routine stops at a
      failing call (001C3D60 with its first callee failing: nothing is
      stored after it);
    - em_area01_math_clear_fault resets the code and the address, and a
      routine then runs again;
    - a dispatcher result >= 0 (0 or 1) is success, and em_a01m_call
      delivers the dispatcher's v0 / f0 (also 0 over a nonzero variable);
    - the store trace appends (address, size) while two words fit in
      trace_cap: capacity 6 holds 00187EC0's three stores, capacity 5
      holds two and then marks UINT32_MAX."""
    ram = (C.c_uint8 * 0x1000)()
    spad = (C.c_uint8 * 0x4000)()
    u32p = C.POINTER(C.c_uint32)
    lw = LIB.em_a01m_lw
    lw.restype, lw.argtypes = C.c_uint32, [C.POINTER(Mem), C.c_uint32]
    sw = LIB.em_a01m_sw
    sw.restype, sw.argtypes = None, [C.POINTER(Mem), C.c_uint32, C.c_uint32]
    call = LIB.em_a01m_call
    call.restype = C.c_int
    call.argtypes = [C.POINTER(Mem), C.c_uint32, u32p, C.c_uint, u32p, C.c_uint, u32p, u32p]
    clear = LIB.em_area01_math_clear_fault
    clear.restype, clear.argtypes = None, [C.POINTER(Mem)]
    holder = {'log': []}

    def view(dispatch, spad_on=True, ram_on=True, trace=None, cap=0):
        cb = CALL(dispatch) if dispatch else CALL()
        holder.setdefault('keep', []).append(cb)
        return Mem(C.cast(ram, C.c_void_p) if ram_on else None, 0x1000,
                   C.cast(spad, C.c_void_p) if spad_on else None, cb, None, 0, 0, 0,
                   C.cast(trace, u32p) if trace is not None else None, cap, 0)

    def expect(cond, *what):
        assert cond, ('core contract', *what)

    # first fault kept
    def nested(_ctx, target, a, na, f, nf, v0, f0):
        lw(C.byref(holder['mem']), 0x50000000)
        return -1
    mem = view(nested)
    holder['mem'] = mem
    status = native_fn(SPECS[0x1CB360])(C.byref(mem), 0x100)
    expect(status == -1 and mem.fault_code == 4 and mem.fault_address == 0x50000000, 'first fault',
           status, mem.fault_code, hex(mem.fault_address))
    # codes and addresses
    def ok(_ctx, target, a, na, f, nf, v0, f0):
        holder['log'].append(target)
        v0[0], f0[0] = 0x1234, 0x3F800000
        return 1
    mem = view(ok)
    expect(lw(C.byref(mem), 0x50000000) == 0 and mem.fault_code == 4 and mem.fault_address == 0x50000000, 'ADDRESS')
    mem = view(ok, spad_on=False)
    lw(C.byref(mem), 0x700038B0)
    expect(mem.fault_code == 1 and mem.fault_address == 0x700038B0, 'NULL scratchpad', mem.fault_code)
    mem = view(ok, ram_on=False)
    lw(C.byref(mem), 0x100)
    expect(mem.fault_code == 1 and mem.fault_address == 0x100, 'NULL RAM', mem.fault_code)
    mem = view(None)
    status = native_fn(SPECS[0x1CB360])(C.byref(mem), 0x100)
    expect(status == -1 and mem.fault_code == 1 and mem.fault_address == 0x1C7420, 'no dispatcher',
           status, mem.fault_code, hex(mem.fault_address))
    def fails(_ctx, target, a, na, f, nf, v0, f0):
        holder['log'].append(target)
        return -1
    mem = view(fails)
    status = native_fn(SPECS[0x1CB360])(C.byref(mem), 0x100)
    expect(status == -1 and mem.fault_code == 2 and mem.fault_address == 0x1C7420, 'WORKER',
           status, mem.fault_code, hex(mem.fault_address))
    # success >= 0 and the v0 / f0 delivered (also a 0 over a nonzero variable)
    mem = view(ok)
    v0, f0 = C.c_uint32(0xFFFFFFFF), C.c_uint32(0xFFFFFFFF)
    expect(call(C.byref(mem), 0x1D3F50, None, 0, None, 0, C.byref(v0), C.byref(f0)) == 0
           and mem.fault_code == 0 and v0.value == 0x1234 and f0.value == 0x3F800000, 'result 1 is success')
    def zero(_ctx, target, a, na, f, nf, v0, f0):
        v0[0], f0[0] = 0, 0
        return 0
    mem = view(zero)
    v0, f0 = C.c_uint32(0xFFFFFFFF), C.c_uint32(0xFFFFFFFF)
    expect(call(C.byref(mem), 0x1D3F50, None, 0, None, 0, C.byref(v0), C.byref(f0)) == 0
           and v0.value == 0 and f0.value == 0, 'v0 / f0 of 0 delivered', hex(v0.value), hex(f0.value))
    # after a fault: no store, loads 0, no call, -1; earlier stores stay
    trace = (C.c_uint32 * 16)()
    mem = view(ok, trace=trace, cap=16)
    sw(C.byref(mem), 0x200, 0x11223344)
    lw(C.byref(mem), 0x50000000)
    stores, tlen = mem.stores, mem.trace_len
    sw(C.byref(mem), 0x204, 0x55667788)
    holder['log'].clear()
    status = native_fn(SPECS[0x187EC0])(C.byref(mem), 7, 8)
    status2 = native_fn(SPECS[0x1CB360])(C.byref(mem), 0x100)
    expect(bytes(ram[0x200:0x208]) == bytes.fromhex('4433221100000000') and mem.stores == stores == 1
           and mem.trace_len == tlen == 2 and lw(C.byref(mem), 0x200) == 0 and status == -1 and status2 == -1
           and not holder['log'] and mem.fault_code == 4 and mem.fault_address == 0x50000000,
           'fail-stop', bytes(ram[0x200:0x208]).hex(), mem.stores, mem.trace_len, status, status2, holder['log'])
    # every routine entered with a fault latched: -1, nothing stored or called
    for entry in sorted(SPECS):
        spec = SPECS[entry]
        mem = view(ok, trace=trace, cap=16)
        lw(C.byref(mem), 0x50000000)
        holder['log'].clear()
        out = C.c_uint32(0xDEADBEEF)
        status = native_fn(spec)(C.byref(mem), *([0] * (spec.ints + spec.floats)),
                                 *([C.byref(out)] if spec.v0 else []))
        expect(status == -1 and mem.stores == 0 and mem.trace_len == 0 and not holder['log']
               and out.value == 0xDEADBEEF and mem.fault_code == 4 and mem.fault_address == 0x50000000,
               'latched fault', hex(entry), status, mem.stores, holder['log'], hex(out.value))
    mem = view(ok, trace=trace, cap=16)
    sw(C.byref(mem), 0x200, 0x11223344)
    lw(C.byref(mem), 0x50000000)
    # clear_fault resets both fields; the routine then runs
    clear(C.byref(mem))
    expect(mem.fault_code == 0 and mem.fault_address == 0, 'clear_fault', mem.fault_code, hex(mem.fault_address))
    expect(native_fn(SPECS[0x1CB360])(C.byref(mem), 0x100) == 0 and len(holder['log']) == 4, 'runs after clear')
    # a routine stops at a failing call: 001C3D60's first callee (001029C0
    # of the inline 001C3BE0) fails, nothing is stored afterwards
    mem = view(fails, trace=trace, cap=16)
    holder['log'].clear()
    status = native_fn(SPECS[0x1C3D60])(C.byref(mem), 0x100, 0x400)
    expect(status == -1 and mem.stores == 0 and mem.trace_len == 0 and holder['log'] == [0x1029C0]
           and mem.fault_code == 2 and mem.fault_address == 0x1029C0, 'stop at a failing call',
           status, mem.stores, [hex(t) for t in holder['log']])
    # the trace: capacity 6 holds three stores, capacity 5 two and then UINT32_MAX
    for cap, want in ((6, [0x8102BB, 1, 0x8104EA, 1, 0x8105CE, 1]), (5, [0x8102BB, 1, 0x8104EA, 1])):
        big = (C.c_uint8 * 0x1000000)()
        trace = (C.c_uint32 * 8)()
        mem = Mem(C.cast(big, C.c_void_p), 0x1000000, C.cast(spad, C.c_void_p), CALL(ok), None, 0, 0, 0,
                  C.cast(trace, u32p), cap, 0)
        native_fn(SPECS[0x187EC0])(C.byref(mem), 7, 8)
        got = list(trace[:len(want)])
        expect(got == want and mem.trace_len == (6 if cap == 6 else 0xFFFFFFFF) and mem.stores == 3,
               'trace', cap, [hex(x) for x in got], hex(mem.trace_len))
        del big


def main():
    global LIB, ELF
    t0 = time.time()
    check_store_rule()
    LIB = build_native()
    core_contract()
    ELF = read_elf()
    beats = ALL_BEATS if RM.FULL else QUICK_BEATS
    jobs = [('actor', b) for b in beats] + [('owner', b) for b in beats] + [('script', b) for b in beats]
    if not RM.FULL:
        jobs.append(('owner2', beats[0]))
    jobs += [('player', (b, part)) for b in (beats if RM.FULL else beats[:1]) for part in (0, 1)]
    jobs += [('25e0', b) for b in beats]
    jobs += [('bone', b) for b in (beats if RM.FULL else beats[:1]) if b != 'a01_07_level_exit']
    jobs += [('probe', b) for b in beats[:2 if RM.FULL else 1]]
    jobs += [('sweep', i) for i in range(SWEEP_PARTS)]
    cost = {'actor': 6, 'owner': 3, 'owner2': 3, 'probe': 4, 'sweep': 2, 'script': 1, 'player': 2, '25e0': 1,
            'bone': 1}
    results = RM.parallel_map(run_job, jobs, cost=lambda j: cost[j[0]])
    totals, stats, skipped = {}, {}, []
    for counts, st, sk in results:
        for k, v in counts.items():
            totals[k] = totals.get(k, 0) + v if not k.endswith('total') else v
        for entry, x in st.items():
            y = stats.setdefault(entry, new_stats())
            for target, seen in x['regs'].items():
                y['regs'].setdefault(target, set()).update(seen)
            y['cases'] += x['cases']
            y['calls'] += x['calls']
            y['outcomes'] |= x['outcomes']
            y['sources'] |= x['sources']
        skipped += sk
    parts = [RM.part(len(beats), len(ALL_BEATS), 'beats')]
    parts += [f'{v:,} {k}' for k, v in totals.items() if not k.startswith('001B2140')]
    parts.append(RM.part(totals['001B2140 table'], totals['001B2140 table total'], '001B2140 table cases'))
    RM.banner(*parts)
    for entry in sorted(stats):
        st = stats[entry]
        own = {o for o in st['outcomes'] if entry <= o[0] < entry + SPECS[entry].size}
        print(f'  {entry:08X}: {st["cases"]} cases, {st["calls"]} boundary calls, '
              f'{len(own)} of {2 * len(conditional_branches(ELF, entry))} branch outcomes, '
              f'sources {sorted(st["sources"])}')
        if os.environ.get('EM_A01_UNCOVERED'):
            want = {(pc, t) for pc in conditional_branches(ELF, entry) for t in (True, False)}
            print('    uncovered:', ' '.join(f'{pc:x}{"T" if t else "F"}' for pc, t in sorted(want - own)))
    if os.environ.get('EM_A01_REGREPORT'):
        # Per (routine, callee): the argument registers the native call
        # passes against those the original callee read before writing, over
        # every boundary call of the run (the check itself fails any call
        # that passes fewer than the callee reads).
        more = []
        for entry in sorted(stats):
            for target, seen in sorted(stats[entry]['regs'].items()):
                passed = sorted({(na, nf) for na, nf, _, _ in seen})
                read = sorted({(ra, rf) for _, _, ra, rf in seen})
                line = f'    {entry:08X} -> {target:08X}: passes a/f {passed}, callee read a/f {read}'
                print(line)
                if any(na > ra or nf > rf for na, nf, ra, rf in seen):
                    more.append(f'{entry:08X}->{target:08X}')
        print(f'  register audit: {sum(len(s_["regs"]) for s_ in stats.values())} (routine, callee) pairs; '
              f'native passes more registers than the callee read on some call for {len(more)}')
    missing = sorted(set(SPECS) - set(stats))
    assert not missing, ('routines not exercised', [f'{e:08X}' for e in missing])
    if skipped:
        print(f'  {len(skipped)} owner runs left the interpreter after their catches (compared up to there):')
        for s_ in sorted(set(x[2:] for x in skipped))[:6]:
            print('   ', *s_)
    print(f'area01 math vs original instructions: PASS ({time.time() - t0:.1f} s)')


if __name__ == '__main__':
    main()
