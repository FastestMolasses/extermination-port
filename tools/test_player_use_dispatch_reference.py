#!/usr/bin/env python3
"""Execute the original Use dispatcher and its two neighbours and compare
em_player_use_dispatch.c.

docs/PLAYER_USE_DISPATCH.md. The user's pinned ELF (and, for the world mode,
the captured route RAM) supplies every instruction and table; none are
embedded here. Routines executed unmodified:

  00160220  the Use dispatcher
  001798D0  the accepted-Use reset
  0017C440  the gait re-entry request

Every other callee is hooked, scripted per case and recorded (never
simulated as a claim about the callee); the native module gets the same
script through its workers. The test asserts that the hooked set is exactly
the set of jal targets of these routines, so no callee runs unhooked.

Compared: all 0x320 record bytes, the scene bytes (D_00810700, which a
scripted worker may change so the repeated loads are tested), the scratch
word 0x70003A20, the return value, and every worker call with its arguments
AND the record bytes, the area and 0x70003A20 at its entry (so a store
moved across a call fails). COP1 goes through tools/ee_float_model.py
(FallEE). Scripted worker faults (a negative worker return at call k) must
stop the native routine with -1 and the record as the original had it at
that call; a missing worker must refuse before any write.

Default run (~10 s): unit cases with every conditional branch outcome of
the three routines asserted. EM_TEST_FULL=1: the exhaustive sweep.
EM_TEST_WORLD=1: route beats 05_boxes (both Use climbs) and
12_crevice_jump (the running jump) replayed over the captured RAM from
their start, the original player stage against the stage with these three
translations in place of the originals (their callees still the original
instructions), compared byte for byte (whole RAM and scratchpad every
frame) and against the PCSX2 trace rows; and every Use press from idle of
05_boxes and 13_east_tower (the high ledge climb, which the stage-only
replay cannot reach from the beat start) run from the frame before the
press, seeded with the captured placement, the same way.
"""
import ctypes as C
import hashlib
import os
import random
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode  # noqa: E402
import ee_float_model as M  # noqa: E402
import test_player_slide_reference as shared  # noqa: E402
from test_player_slide_reference import EE, read_elf, s32  # noqa: E402
from test_player_fall_reference import FallEE, FallRoute, nested_bits  # noqa: E402

MASK = 0xFFFFFFFF
LANE = os.environ.get('EM_LANE', 'player_use_dispatch_reference')
OUT = ROOT / 'build' / LANE


def F(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def fbits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def s16(value):
    value &= 0xFFFF
    return value - 0x10000 if value & 0x8000 else value


# ======================================================================
# The routines and their callees
# ======================================================================

USE, ACCEPTED, REENTRY = 0x160220, 0x1798D0, 0x17C440
SIZES = {USE: 0x5A4, ACCEPTED: 0x40, REENTRY: 0xFC}
TRANSLATED = set(SIZES)
SPEED_TABLE = 0x248870

CALLEES = {
    0x184BA0: 'scan', 0x174A50: 'row', 0x1AAC00: 'classify', 0x15D4C0: 'surface',
    0x1C94B0: 'trs', 0x15DF10: 'ledge', 0x1B1470: 'wrap', 0x15EC50: 'jump', 0x15FDF0: 'aim',
    0x178B90: 'translate', 0x17B490: 'select', 0x1C61D0: 'frames', 0x1749F0: 'arbiter',
}


def in_translated(pc):
    return any(start <= pc < start + size for start, size in SIZES.items())


def check_callee_set(elf):
    ee = EE(elf)
    targets = set()
    for start, size in SIZES.items():
        for pc in range(start, start + size, 4):
            word = ee.load(pc)
            if word >> 26 == 3:
                targets.add((word & 0x3FFFFFF) << 2)
    missing = sorted(t for t in targets if t not in CALLEES and t not in TRANSLATED)
    assert not missing, ('callees neither hooked nor translated', [hex(t) for t in missing])
    unused = sorted(t for t in CALLEES if t not in targets)
    assert not unused, ('hooked addresses no translated routine calls', [hex(t) for t in unused])
    return len(targets)


def branch_sites(elf):
    ee = EE(elf)
    sites = set()
    for start, size in SIZES.items():
        for pc in range(start, start + size, 4):
            word = ee.load(pc)
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            if op in (4, 20) and rs == 0 and rt == 0:
                continue
            if ee.branch(word, pc) is not None:
                sites.add(pc)
    return sites


class CoverEE(FallEE):
    """FallEE recording every conditional branch outcome in the routines."""

    def __init__(self, elf):
        super().__init__(elf)
        self.outcomes = set()

    def branch(self, word, pc):
        b = super().branch(word, pc)
        if b is not None and in_translated(pc):
            self.outcomes.add((pc, b[0]))
        return b


# ======================================================================
# Native side (ctypes)
# ======================================================================

class LiveActor(C.Structure):
    _fields_ = [('bytes', C.c_uint8 * 0x320), ('link_owner', C.c_void_p), ('link_prev', C.c_void_p),
                ('link_flags', C.c_uint8), ('link_type', C.c_uint8)]


class Scene(C.Structure):
    _fields_ = [('d810E74', C.c_uint16), ('spad3B76', C.c_uint16), ('area', C.c_uint8)]


P = C.POINTER
LA = P(LiveActor)
I, U32, FLT, VP = C.c_int, C.c_uint32, C.c_float, C.c_void_p
FN = {
    'test': C.CFUNCTYPE(I, VP, LA, P(I)),
    'row': C.CFUNCTYPE(I, VP, LA, FLT),
    'trs': C.CFUNCTYPE(I, VP, P(U32), P(U32), P(U32), P(U32)),
    'ledge': C.CFUNCTYPE(I, VP, LA, I, U32, P(I)),
    'wrap': C.CFUNCTYPE(I, VP, U32, P(U32)),
    'speed': C.CFUNCTYPE(I, VP, C.c_uint, P(U32)),
    'translate': C.CFUNCTYPE(I, VP, LA, I),
    'select': C.CFUNCTYPE(I, VP, LA, I, I, I, P(C.c_int16)),
    'frames': C.CFUNCTYPE(I, VP, U32, I, P(C.c_int32)),
    'arbiter': C.CFUNCTYPE(I, VP, LA, I, FLT, FLT),
}
USE_FIELDS = (('scan', 'test'), ('row_request', 'row'), ('classify', 'test'), ('surface', 'test'),
              ('trs', 'trs'), ('ledge', 'ledge'), ('wrap', 'wrap'), ('jump', 'test'), ('aim', 'test'))
REENTRY_FIELDS = (('speed', 'speed'), ('translate', 'translate'), ('select', 'select'),
                  ('clip_frames', 'frames'), ('arbiter', 'arbiter'))
FIELD_NAME = {'scan': 'scan', 'row_request': 'row', 'classify': 'classify', 'surface': 'surface',
              'trs': 'trs', 'ledge': 'ledge', 'wrap': 'wrap', 'jump': 'jump', 'aim': 'aim',
              'translate': 'translate', 'select': 'select', 'clip_frames': 'frames',
              'arbiter': 'arbiter'}


class UseWorkers(C.Structure):
    _fields_ = [('context', VP), ('scene', P(Scene))] + [(n, FN[k]) for n, k in USE_FIELDS]


class ReentryWorkers(C.Structure):
    _fields_ = [('context', VP), ('spad3A20', P(U32))] + [(n, FN[k]) for n, k in REENTRY_FIELDS]


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / ('use_dispatch.dylib' if sys.platform == 'darwin' else 'use_dispatch.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc',
                    os.environ.get('EM_USE_DISPATCH_SOURCE', 'src/game/em_player_use_dispatch.c'),
                    '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    native.em_player_use_00160220.argtypes = [P(UseWorkers), LA, P(I)]
    native.em_player_use_001798D0.argtypes = [P(UseWorkers), LA]
    native.em_player_reentry_0017C440.argtypes = [P(ReentryWorkers), LA, I]
    native.em_player_use_workers_bound.argtypes = [P(UseWorkers)]
    native.em_player_reentry_workers_bound.argtypes = [P(ReentryWorkers)]
    return native


# ======================================================================
# Scripted callee effects (identical on both sides)
# ======================================================================

FLAGS = (0, 0x1000, 0x8000, 0x9000)


def effect_for(rng, name):
    """What a hooked callee does in this case: its return value, the record
    bytes it writes (offset, size, value), a new D_00810700, a float return
    and an output matrix (build_trs_matrix)."""
    e = {'ret': 0, 'writes': [], 'area': None, 'fret': None, 'out': None}
    w, chance = e['writes'], rng.random
    if name in ('scan', 'surface', 'classify', 'ledge', 'jump', 'aim'):
        e['ret'] = {'scan': (0, 0, 0, 0, 1, 2, -1), 'surface': (0, 0, 0, 1, 7),
                    'classify': (0, 0, 1, 2, 3, 4, -9), 'ledge': (0, 0, 0, 1, -1),
                    'jump': (0, 0, 1, 2), 'aim': (0, 1, 1, 5)}[name][rng.randrange(
                        {'scan': 7, 'surface': 5, 'classify': 7, 'ledge': 5, 'jump': 4, 'aim': 4}[name])]
        if chance() < 0.3: w.append((5, 1, rng.choice((2, 3, 6, 0xB, 0x24))))
        if chance() < 0.25: w.append((0xC4, 4, F(rng.uniform(-3.2, 3.2))))
        if chance() < 0.2: w.append((rng.choice((0xB0, 0xB4, 0xB8)), 4, F(rng.uniform(-400, 400))))
        if chance() < 0.15: w.append((0x236, 1, rng.choice((0, 1))))
        if chance() < 0.15: w.append((0x23B, 1, rng.choice((0x35, 5, 0x32))))
        if name in ('scan', 'classify', 'surface') and chance() < 0.12:
            e['area'] = rng.choice((0xB, 0x15, 1, 4, 0xD))
    elif name in ('row', 'translate', 'arbiter'):
        if chance() < 0.3: w.append((0x200, 4, rng.choice(FLAGS)))
        if chance() < 0.3: w.append((0x38, 4, F(rng.choice((0.0, 0.05, 0.3)))))
        if chance() < 0.3: w.append((0x20C, 2, rng.randrange(0x200)))
        if chance() < 0.2: w.append((0x25C, 1, rng.randrange(5)))
        if chance() < 0.15: w.append((0x235, 1, rng.randrange(8)))
        if chance() < 0.15: w.append((0x40, 4, rng.getrandbits(32)))
    elif name == 'select':
        e['ret'] = rng.choice((0x6E, 0x72, 0x8001, -1, 0x12345, 0))
        if chance() < 0.2: w.append((0x25C, 1, rng.randrange(5)))
    elif name == 'frames':
        e['ret'] = rng.choice((25, 40, 0, -7, 0x1000001, 120, 0x7FFFFFFF))
        if chance() < 0.2: w.append((0x25C, 1, rng.choice((2, 2, 1, 3))))
    elif name == 'trs':
        e['out'] = [F(rng.uniform(-2, 2)) for _ in range(16)]
    elif name == 'wrap':
        e['fret'] = F(rng.choice((rng.uniform(-3.2, 3.2), 0.0, 3.14159274, -3.14159274)))
    return e


class Script:
    """The per-call effects of a case. `force` (the boundary cases) pins a
    callee's return value and drops its other effects."""

    def __init__(self, seed, fail_at=None, force=None):
        self.seed, self.count, self.fail_at, self.force = seed, 0, fail_at, force or {}

    def next(self, name):
        rng = random.Random('%d:%d:%s' % (self.seed, self.count, name))
        index = self.count
        self.count += 1
        e = effect_for(rng, name)
        if name in self.force:
            e.update(ret=self.force[name], writes=[], area=None)
        return index, e


# ======================================================================
# One unit case
# ======================================================================

ACTOR = 0x680000
S3A20 = 0x70003A20
AREA, E74, B76 = 0x810700, 0x810E74, 0x70003B76
ENTRIES = ('use', 'accepted', 'reentry')

# The trigger boxes as (y, x, z) bounds, read from the constants in the
# instructions by the test's own reading of the ranges; used only to aim
# case positions at the edges (the oracle decides what the edges are).
EDGES = {1: ((-40.0, -20.0), (-35.0, 35.0), (-1050.0, -990.0)),
         4: ((10.0, 20.0), (315.0, 360.0), (315.0, 385.0)),
         0xD: (((150.0, 210.0), (720.0, 800.0), (800.0, 840.0)),
               ((150.0, 215.0), (635.0, 720.0), (1270.0, 1325.0)))}


def ulp(value, steps):
    b = F(value)
    if value == 0.0:
        return b
    if (value > 0) == (steps > 0):
        return (b + abs(steps)) & MASK
    return (b - abs(steps)) & MASK


def axis_value(rng, lo, hi):
    pick = rng.randrange(8)
    if pick == 0: return F(lo)
    if pick == 1: return F(hi)
    if pick == 2: return ulp(lo, -1)
    if pick == 3: return ulp(hi, 1)
    if pick == 4: return F(rng.uniform(lo, hi))
    if pick == 5: return F(rng.uniform(lo - 50, lo))
    if pick == 6: return F(rng.uniform(hi, hi + 50))
    return F((lo + hi) / 2)


def put(actor, offset, size, value):
    actor[offset:offset + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'little')


BOUNDARY_BASE = 10_000_000


def boundary_cases():
    """Every trigger-box bound of every area: each axis at lo, one ULP below
    lo, hi and one ULP above hi with the other two axes inside, plus area
    0xD's hand-over from the first box to the second (y in (210, 215], x/z
    in the second box; x or z outside the first box). The dispatcher then
    reaches the ledge probes (all scripted 0) or skips them, so the call log
    shows which side of each bound the original put the position."""
    out = []
    boxes = [(1, EDGES[1]), (4, EDGES[4]), (0xD, EDGES[0xD][0]), (0xD, EDGES[0xD][1])]
    for area, box in boxes:
        mids = [F((lo + hi) / 2) for lo, hi in box]
        for axis, (lo, hi) in enumerate(box):
            for value in (F(lo), ulp(lo, -1), F(hi), ulp(hi, 1)):
                point = list(mids)
                point[axis] = value
                out.append((area, point))
    (y0, y1), (x0, x1), (z0, z1) = EDGES[0xD][1]
    out.append((0xD, [F(212.5), F((x0 + x1) / 2), F((z0 + z1) / 2)]))
    out.append((0xD, [ulp(210.0, 1), F((x0 + x1) / 2), F((z0 + z1) / 2)]))
    out.append((0xD, [F(215.0), F(720.0), F(1270.0)]))
    (a_y, a_x, a_z) = EDGES[0xD][0]
    out.append((0xD, [F(180.0), F(760.0), ulp(840.0, 1)]))
    out.append((0xD, [F(180.0), ulp(800.0, 1), F(820.0)]))
    return out


BOUNDARY = boundary_cases()


def make_boundary(index):
    area, (y, x, z) = BOUNDARY[index]
    rng = random.Random(index)
    actor = bytearray(rng.getrandbits(8) for _ in range(0x320))
    put(actor, 0xB4, 4, y); put(actor, 0xB0, 4, x); put(actor, 0xB8, 4, z)
    put(actor, 0xC4, 4, F(0.5)); put(actor, 0x236, 1, 0); put(actor, 0x23B, 1, 5)
    return {'seed': BOUNDARY_BASE + index, 'entry': 'use', 'actor': bytes(actor), 'area': area,
            'e74': 0x40, 'b76': 0x40, 's3A20': 0, 'arg': 0,
            'force': {'scan': 0, 'surface': 0, 'ledge': 0, 'jump': 0, 'aim': 0}}


def make_case(seed):
    if seed >= BOUNDARY_BASE:
        return make_boundary(seed - BOUNDARY_BASE)
    rng = random.Random(seed)
    entry = rng.choice(('use',) * 12 + ('accepted',) + ('reentry',) * 3)
    actor = bytearray(rng.getrandbits(8) for _ in range(0x320))
    area = rng.choice((0xB, 0xB, 0xB, 0x15, 0x15, 1, 1, 4, 4, 0xD, 0xD, 0xD, 0, 2))
    if rng.random() < 0.92 and area in EDGES:
        box = EDGES[area]
        if area == 0xD:
            box = box[rng.randrange(2)]
        for (lo, hi), offset in zip(box, (0xB4, 0xB0, 0xB8)):
            put(actor, offset, 4, axis_value(rng, lo, hi))
    elif rng.random() < 0.9:
        for offset in (0xB0, 0xB4, 0xB8):
            put(actor, offset, 4, F(rng.uniform(-400, 400)))
    put(actor, 0xC4, 4, rng.choice((F(rng.uniform(-3.2, 3.2)), F(0.0), F(3.14159274), F(-0.0),
                                     rng.getrandbits(32))))
    put(actor, 0x236, 1, rng.choice((0, 0, 0, 1, 2)))
    put(actor, 0x23B, 1, rng.choice((0x35, 5, 5, 0x32, 0)))
    put(actor, 0x23F, 1, rng.choice((0, 1, 2, 3, 3, 4, 0xFF)))
    put(actor, 0x235, 1, rng.randrange(8))
    return {
        'seed': seed, 'entry': entry, 'actor': bytes(actor), 'area': area,
        'e74': rng.choice((0, 0x40, 0x40, 0x40, 0x4040, 0xFFFF, 0x10)),
        'b76': rng.choice((0x40, 0x40, 0x40, 0x10, 0)),
        's3A20': rng.getrandbits(32),
        'arg': rng.choice((0, 1, 1, 7)),
    }


def call_entry(name, args, actor, area, s3A20):
    return (name,) + tuple(args) + (bytes(actor), area, s3A20)


class UnitOracle:
    """The original routines on a CoverEE with every callee hooked."""

    def __init__(self, elf):
        self.ee = CoverEE(elf)
        for address, name in CALLEES.items():
            self.ee.hooks[address] = self.hook(name)

    def hook(self, name):
        def run(ee):
            args = self.args(name, ee)
            self.log.append(call_entry(name, args, ee.read(ACTOR, 0x320), ee.load(AREA, 1),
                                       ee.load(S3A20)))
            index, e = self.script.next(name)
            for offset, size, value in e['writes']:
                ee.save(ACTOR + offset, value, size)
            if e['area'] is not None:
                ee.save(AREA, e['area'], 1)
            if e['out'] is not None:
                for i, value in enumerate(e['out']): ee.save(ee.arg(0) + 4 * i, value)
            if e['fret'] is not None:
                ee.f[0] = e['fret']
            ee.ret_int(e['ret'])
        return run

    def args(self, name, ee):
        a = ee.arg
        vec = lambda address, n: tuple(ee.load(address + 4 * i) for i in range(n))
        if name in ('scan', 'row', 'classify', 'surface', 'ledge', 'jump', 'aim', 'translate',
                    'select', 'arbiter'):
            assert a(0) == ACTOR, (name, hex(a(0)))
        if name == 'classify':
            assert (a(1), a(2)) == (ACTOR + 0x290, ACTOR + 0x218), ('001AAC00 pointers', hex(a(1)))
        if name == 'row': return (ee.f[12] & MASK,)
        if name == 'trs':
            assert (a(0), a(1), a(2), a(3)) == (ACTOR + 0xD0, ACTOR + 0xB0, ACTOR + 0xC0, ACTOR + 0x60)
            return (vec(a(1), 4), vec(a(2), 4), vec(a(3), 4))
        if name == 'ledge': return (s32(a(1)), ee.f[12] & MASK)
        if name == 'wrap': return (ee.f[12] & MASK,)
        if name == 'translate': return (s32(a(1)),)
        if name == 'select': return (s32(a(1)), s32(a(2)), s32(a(3)))
        if name == 'frames': return (a(0), s32(a(1)))
        if name == 'arbiter': return (s32(a(1)), ee.f[12] & MASK, ee.f[13] & MASK)
        return ()

    def run(self, case, script):
        ee = self.ee
        self.script, self.log = script, []
        ee.r, ee.rh = [0] * 32, [0] * 32
        ee.f, ee.acc, ee.cond = [0] * 32, 0, False
        ee.vacc, ee.q = [0, 0, 0, 0], 0
        ee.r[28], ee.r[29] = 0x27D370, shared.STACK_TOP
        ee.write(ACTOR, case['actor'])
        ee.save(AREA, case['area'], 1)
        ee.save(E74, case['e74'], 2)
        ee.save(B76, case['b76'], 2)
        ee.save(S3A20, case['s3A20'])
        entry = case['entry']
        address = {'use': USE, 'accepted': ACCEPTED, 'reentry': REENTRY}[entry]
        ee.call(address, (ACTOR, case['arg']) if entry == 'reentry' else (ACTOR,))
        return {'actor': ee.read(ACTOR, 0x320), 'area': ee.load(AREA, 1), 's3A20': ee.load(S3A20),
                'log': self.log, 'v0': s32(ee.r[2]) if entry == 'use' else None}


class NativeRun:
    """em_player_use_dispatch.c with Python workers replaying the same script."""

    def __init__(self, native, case, script, missing=None):
        self.native, self.case, self.script, self.log = native, case, script, []
        self.live = LiveActor()
        C.memmove(self.live.bytes, case['actor'], 0x320)
        self.scene = Scene(case['e74'], case['b76'], case['area'])
        self.s3A20 = U32(case['s3A20'])
        self.elf_table = None
        self.keep = []
        use = {}
        for field, kind in USE_FIELDS:
            use[field] = FN[kind](self.worker(field)) if field != missing else FN[kind]()
            self.keep.append(use[field])
        self.use = UseWorkers(None, C.pointer(self.scene) if missing != 'scene' else P(Scene)(), **use)
        rent = {}
        for field, kind in REENTRY_FIELDS:
            rent[field] = FN[kind](self.worker(field)) if field != missing else FN[kind]()
            self.keep.append(rent[field])
        self.reentry = ReentryWorkers(None, C.pointer(self.s3A20) if missing != 'spad3A20' else P(U32)(),
                                      **rent)

    def call(self, name, args):
        self.log.append(call_entry(name, args, bytes(self.live.bytes), self.scene.area, self.s3A20.value))
        index, e = self.script.next(name)
        if self.script.fail_at == index:
            return None
        for offset, size, value in e['writes']:
            for i in range(size): self.live.bytes[offset + i] = (value >> (8 * i)) & 0xFF
        if e['area'] is not None:
            self.scene.area = e['area']
        return e

    def worker(self, field):
        name = FIELD_NAME.get(field, field)
        vec = lambda ptr, n: tuple(ptr[i] for i in range(n))

        if field in ('scan', 'classify', 'surface', 'jump', 'aim'):
            def test(_, a, out):
                e = self.call(name, ())
                if e is None: return -1
                out[0] = s32(e['ret'])
                return 0
            return test
        if field == 'row_request':
            return lambda _, a, blend: -1 if self.call('row', (fbits(blend),)) is None else 0
        if field == 'trs':
            def trs(_, out, position, rotation, scale):
                e = self.call('trs', (vec(position, 4), vec(rotation, 4), vec(scale, 4)))
                if e is None: return -1
                for i in range(16): out[i] = e['out'][i]
                return 0
            return trs
        if field == 'ledge':
            def ledge(_, a, mode, angle, out):
                e = self.call('ledge', (mode, angle))
                if e is None: return -1
                out[0] = s32(e['ret'])
                return 0
            return ledge
        if field == 'wrap':
            def wrap(_, x, out):
                e = self.call('wrap', (x,))
                if e is None: return -1
                out[0] = e['fret']
                return 0
            return wrap
        if field == 'speed':
            def speed(_, tier, out):
                # A data read (no call in the original): D_00248870[tier]
                # from the ELF image the oracle runs.
                if self.case.get('speed_fault'):
                    return -1
                out[0] = ELF_EE.load(SPEED_TABLE + 4 * tier)
                return 0
            return speed
        if field == 'translate':
            return lambda _, a, arg: -1 if self.call('translate', (arg,)) is None else 0
        if field == 'select':
            def select(_, a, cmd, idx, tbl, out):
                e = self.call('select', (cmd, idx, tbl))
                if e is None: return -1
                out[0] = s16(e['ret'])
                return 0
            return select
        if field == 'clip_frames':
            def frames(_, bank, clip, out):
                e = self.call('frames', (bank, clip))
                if e is None: return -1
                out[0] = s32(e['ret'])
                return 0
            return frames
        if field == 'arbiter':
            return lambda _, a, clip, blend, frame: -1 if self.call(
                'arbiter', (clip, fbits(blend), fbits(frame))) is None else 0
        raise AssertionError(field)

    def run(self):
        entry = self.case['entry']
        out = C.c_int(-99)
        if entry == 'use':
            r = self.native.em_player_use_00160220(C.byref(self.use), C.byref(self.live), C.byref(out))
        elif entry == 'accepted':
            r = self.native.em_player_use_001798D0(C.byref(self.use), C.byref(self.live))
        else:
            r = self.native.em_player_reentry_0017C440(C.byref(self.reentry), C.byref(self.live),
                                                       self.case['arg'])
        return r, {'actor': bytes(self.live.bytes), 'area': self.scene.area, 's3A20': self.s3A20.value,
                   'log': self.log, 'v0': out.value if entry == 'use' else None}


ORACLE = None
NATIVE = None
ELF_EE = None


def first_difference(a, b):
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            if isinstance(x, tuple) and len(x) > 3 and isinstance(x[-3], bytes) and x[:-3] == y[:-3]:
                diff = [hex(k) for k in range(0x320) if x[-3][k] != y[-3][k]]
                return (i, x[0], 'record bytes at this call differ at', diff[:16], x[-2:], y[-2:])
            return (i, x[:-3] if isinstance(x, tuple) else x, y[:-3] if isinstance(y, tuple) else y)
    return ('length', len(a), len(b))


def run_case(seed):
    case = make_case(seed)
    rng = random.Random(seed ^ 0x55AA)
    # A scripted worker fault in about one case in eight (reentry: or the
    # D_00248870 read), at a call the original makes.
    first = ORACLE.run(case, Script(seed, None, case.get('force')))
    kind = 'ok'
    if first['log'] and seed < BOUNDARY_BASE and rng.random() < 0.125:
        fail_at = rng.randrange(len(first['log']))
        run = NativeRun(NATIVE, case, Script(seed, fail_at, case.get('force')))
        r, got = run.run()
        assert r == -1, (seed, 'scripted fault did not stop the native routine', r)
        want_log = first['log'][:fail_at + 1]
        assert got['log'] == want_log, (seed, 'log before the fault', first_difference(got['log'], want_log))
        entry_state = first['log'][fail_at]
        assert got['actor'] == entry_state[-3], (seed, 'record after the fault')
        assert (got['area'], got['s3A20']) == entry_state[-2:], (seed, 'scene after the fault')
        kind = 'fault'
    elif case['entry'] == 'reentry' and rng.random() < 0.05:
        case['speed_fault'] = True
        run = NativeRun(NATIVE, case, Script(seed))
        r, got = run.run()
        want = bytearray(case['actor'])
        want[0x25C] = (want[0x23F] - 1) & 0xFF
        assert r == -1 and got['actor'] == bytes(want) and not got['log'], (seed, 'speed read fault')
        kind = 'speed_fault'
    else:
        run = NativeRun(NATIVE, case, Script(seed, None, case.get('force')))
        r, got = run.run()
        assert r == 0, (seed, 'native fault', r)
        assert got['log'] == first['log'], (seed, case['entry'], first_difference(got['log'], first['log']))
        if got['actor'] != first['actor']:
            diff = [hex(k) for k in range(0x320) if got['actor'][k] != first['actor'][k]]
            raise AssertionError((seed, case['entry'], 'record bytes differ at', diff[:24]))
        assert (got['area'], got['s3A20'], got['v0']) == (first['area'], first['s3A20'], first['v0']), \
            (seed, case['entry'], 'scene / scratch / return', got['area'], first['area'], hex(got['s3A20']),
             hex(first['s3A20']), got['v0'], first['v0'])
        if seed >= BOUNDARY_BASE:
            kind = 'box %s' % ('inside' if 'trs' not in [x[0] for x in first['log']] else 'outside')
        elif case['entry'] == 'use':
            kind = 'use %d' % first['v0']
    return case['entry'], len(first['log']), kind, ORACLE.ee.outcomes.copy()


def missing_worker_checks(native):
    """A missing worker (or scene / scratch) refuses before any write; a
    missing classifier faults only where area 0x15 reaches it."""
    n = 0
    base = make_case(7)
    for field in [f for f, _ in USE_FIELDS if f != 'classify'] + ['scene']:
        case = dict(base, entry='use', e74=0x40, b76=0x40)
        run = NativeRun(native, case, Script(1), missing=field)
        r, got = run.run()
        assert r == -1 and got['actor'] == case['actor'] and not got['log'], ('missing', field, r)
        n += 1
    case = dict(base, entry='accepted')
    run = NativeRun(native, case, Script(1), missing='row_request')
    r, got = run.run()
    assert r == -1 and got['actor'] == case['actor'] and not got['log'], ('missing row_request', r)
    n += 1
    for field in [f for f, _ in REENTRY_FIELDS] + ['spad3A20']:
        case = dict(base, entry='reentry')
        run = NativeRun(native, case, Script(1), missing=field)
        r, got = run.run()
        assert r == -1 and got['actor'] == case['actor'] and not got['log'], ('missing', field, r)
        n += 1
    # classify: bound check passes; area 0x15 with a Use press and no scan
    # winner reaches it and faults after the scan call, with no record write.
    for seed in range(1, 400):
        script = Script(seed)
        index, e = script.next('scan')
        if e['ret'] == 0 and not e['writes'] and e['area'] is None:
            break
    case = dict(base, entry='use', e74=0x40, b76=0x40, area=0x15)
    run = NativeRun(native, case, Script(seed), missing='classify')
    r, got = run.run()
    assert r == -1 and got['actor'] == case['actor'] and [x[0] for x in got['log']] == ['scan'], \
        ('missing classify', r, got['log'][:2])
    n += 1
    # The same case with the classifier bound but in AREA11 (0xB): never read.
    case = dict(base, entry='use', e74=0x40, b76=0x40, area=0xB)
    run = NativeRun(native, case, Script(seed), missing='classify')
    r, got = run.run()
    assert r == 0 and 'classify' not in [x[0] for x in got['log']], ('area 0xB reached 001AAC00', r)
    n += 1
    return n


def main():
    global ORACLE, NATIVE, ELF_EE
    started = time.time()
    elf = read_elf()
    callees = check_callee_set(elf)
    ELF_EE = EE(elf)
    ORACLE = UnitOracle(elf)
    NATIVE = build_native()
    total = reference_mode.pick(40000, 40000)
    seeds = reference_mode.select(range(total), 4000, 0x160220)
    seeds += [BOUNDARY_BASE + i for i in range(len(BOUNDARY))]
    results = reference_mode.parallel_map(run_case, seeds)
    outcomes, entries, kinds = set(), {}, {}
    calls = 0
    for entry, count, kind, cover in results:
        outcomes.update(cover)
        entries[entry] = entries.get(entry, 0) + 1
        kinds[kind] = kinds.get(kind, 0) + 1
        calls += count
    sites = branch_sites(elf)
    missing = sorted('%06X %s' % (pc, 'taken' if taken else 'not taken')
                     for pc in sites for taken in (True, False) if (pc, taken) not in outcomes)
    assert not missing, ('branch outcomes never exercised', missing)
    absent = [e for e in ENTRIES if e not in entries]
    assert not absent, ('entry points never run', absent)
    stops = missing_worker_checks(NATIVE)
    reference_mode.banner(reference_mode.part(len(seeds) - len(BOUNDARY), total, 'cases'),
                          '%d trigger-box boundary cases' % len(BOUNDARY),
                          '%d jal targets (all hooked or translated)' % callees)
    print('player use dispatch vs original instructions: PASS %d cases (%s), %d worker calls '
          'identical with the record at each, every one of %d conditional branches both ways, '
          'outcomes %s, %d missing-worker refusals (%.1fs)' % (
              len(seeds), ', '.join('%s %d' % kv for kv in sorted(entries.items())), calls,
              len(sites), ', '.join('%s %d' % kv for kv in sorted(kinds.items())), stops,
              time.time() - started))


# ======================================================================
# World mode: route beats over the captured RAM
# ======================================================================

PLAYER = shared.PLAYER
VEC = 0x7F0E0000
SCRIPTED = 0x41
# beat: the frame count to replay (default: to the row before the first
# scripted takeover, or the end of the trace). 05: Cross at f172 and f390
# (climb states from f175 and f393); 12: Cross at f227 (running jump from
# f230); 13: Cross at f435 (high ledge climb from f438).
WORLD_BEATS = ('05_boxes', '12_crevice_jump')
# Beats the stage-only replay cannot follow from their start (13: the
# original replay itself leaves the trace at f133, where the capture's walk
# stops; the replay runs no owners or scripts). Each Use press there runs
# from the idle frame before it instead, seeded with the captured feet, body
# and yaw as test_player_climb_reference.py's capture route does.
#
# A Use press on an owner (the scan winner, 00184BA0 -> 001798D0,
# +5 = 0x25) is seeded the same way: the fence door (09). Only its press
# frame is compared, since the owner's controller takes the player over on
# the next frame (+1F0 = 0x41) and the stage-only run has no owners. The
# other owner presses (01..04, panel and elevator) cannot be run this way:
# their source snapshots hold D_008106EF (the use inhibit 00184BA0 tests)
# = 0x31, which only the owners and scripts the stage-only run lacks clear
# before the press.
SEEDED_BEATS = ('05_boxes', '13_east_tower', '09_fence_door')


def replay_end(trace):
    moved = False
    for r in sorted(trace['rows'], key=lambda r: r['counter']):
        if r['p5'] != 0:
            moved = True
        elif moved and r['m1F0'] == SCRIPTED:
            return r['counter'] - 1
    return trace['last_counter']


def in_ram(address, size=4):
    return 0x100000 <= address and address + size <= 0x2000000


class WorldCall:
    """One native call: the record, scene and scratch read from the EE, the
    workers bound to the ORIGINAL callees executed in the same EE (state
    synced around every call)."""

    def __init__(self, ee, base, counts):
        self.ee, self.base, self.counts, self.error = ee, base, counts, None
        self.live = LiveActor()
        C.memmove(self.live.bytes, ee.read(base, 0x320), 0x320)
        self.scene = Scene(ee.load(E74, 2), ee.load(B76, 2), ee.load(AREA, 1))
        self.s3A20 = U32(ee.load(S3A20))
        self.keep = []
        use = {f: FN[k](self.guard(getattr(self, 'w_' + f))) for f, k in USE_FIELDS}
        rent = {f: FN[k](self.guard(getattr(self, 'w_' + f))) for f, k in REENTRY_FIELDS}
        self.keep += list(use.values()) + list(rent.values())
        self.use = UseWorkers(None, C.pointer(self.scene), **use)
        self.reentry = ReentryWorkers(None, C.pointer(self.s3A20), **rent)

    def guard(self, fn):
        def run(_context, *args):
            if self.error is not None:
                return -1
            try:
                return fn(*args)
            except BaseException as error:        # surfaced after the native call returns
                self.error = error
                return -1
        return run

    def sync_in(self):
        ee = self.ee
        ee.write(self.base, bytes(self.live.bytes))
        ee.save(AREA, self.scene.area, 1)
        ee.save(S3A20, self.s3A20.value)

    def sync_out(self):
        ee = self.ee
        C.memmove(self.live.bytes, ee.read(self.base, 0x320), 0x320)
        self.scene.area = ee.load(AREA, 1)
        self.s3A20.value = ee.load(S3A20)

    def call(self, name, entry, args=(), fregs=()):
        self.counts[name] = self.counts.get(name, 0) + 1
        self.sync_in()
        v0, f0 = nested_bits(self.ee, entry, args, fregs)
        self.sync_out()
        return s32(v0), f0 & MASK

    def result(self, out, name, entry, args=()):
        out[0] = self.call(name, entry, args)[0]
        return 0

    # ---- the workers -------------------------------------------------------
    def w_scan(self, a, out): return self.result(out, 'scan', 0x184BA0, (self.base,))
    def w_classify(self, a, out):
        return self.result(out, 'classify', 0x1AAC00, (self.base, self.base + 0x290, self.base + 0x218))
    def w_surface(self, a, out): return self.result(out, 'surface', 0x15D4C0, (self.base,))
    def w_jump(self, a, out): return self.result(out, 'jump', 0x15EC50, (self.base,))
    def w_aim(self, a, out): return self.result(out, 'aim', 0x15FDF0, (self.base,))

    def w_row_request(self, a, blend):
        self.call('row', 0x174A50, (self.base,), (fbits(blend),)); return 0

    def w_trs(self, out, position, rotation, scale):
        # The native hands the record's own words; the original passes the
        # record addresses, so the arrays must equal the record.
        record = bytes(self.live.bytes)
        for p, at in ((position, 0xB0), (rotation, 0xC0), (scale, 0x60)):
            assert [p[i] for i in range(4)] == list(struct.unpack_from('<4I', record, at)), ('trs input', hex(at))
        self.call('trs', 0x1C94B0, (self.base + 0xD0, self.base + 0xB0, self.base + 0xC0, self.base + 0x60))
        for i in range(16): out[i] = self.ee.load(self.base + 0xD0 + 4 * i)
        return 0

    def w_ledge(self, a, mode, angle, out):
        out[0] = self.call('ledge', 0x15DF10, (self.base, mode), (angle,))[0]; return 0

    def w_wrap(self, x, out):
        out[0] = self.call('wrap', 0x1B1470, (), (x,))[1]; return 0

    def w_speed(self, tier, out):
        address = SPEED_TABLE + 4 * tier
        assert in_ram(address), ('D_00248870 read outside RAM', tier)
        out[0] = self.ee.load(address)
        return 0

    def w_translate(self, a, arg):
        self.call('translate', 0x178B90, (self.base, arg)); return 0

    def w_select(self, a, cmd, idx, tbl, out):
        out[0] = s16(self.call('select', 0x17B490, (self.base, cmd, idx, tbl))[0]); return 0

    def w_clip_frames(self, bank, clip, out):
        out[0] = self.call('frames', 0x1C61D0, (bank, clip))[0]; return 0

    def w_arbiter(self, a, clip, blend, frame):
        self.call('arbiter', 0x1749F0, (self.base, clip), (fbits(blend), fbits(frame))); return 0

    def finish(self, result, what):
        if self.error is not None:
            raise self.error
        assert result == 0, (what, 'native fault', result)
        self.sync_in()


def world_hook(native, kind, counts, results):
    def hook(ee):
        base = ee.arg(0)
        call = WorldCall(ee, base, counts)
        out = C.c_int(-99)
        if kind == 'use':
            result = native.em_player_use_00160220(C.byref(call.use), C.byref(call.live), C.byref(out))
        elif kind == 'accepted':
            result = native.em_player_use_001798D0(C.byref(call.use), C.byref(call.live))
        else:
            result = native.em_player_reentry_0017C440(C.byref(call.reentry), C.byref(call.live),
                                                       s32(ee.arg(1)))
        call.finish(result, kind)
        if kind == 'use':
            ee.ret_int(out.value)
            key = (out.value, ee.load(base + 5, 1)) if out.value else (0, None)
            results[key] = results.get(key, 0) + 1
        counts['native ' + kind] = counts.get('native ' + kind, 0) + 1
    return hook


WORLD = {}


def world_replay(job):
    beat, native_mode = job
    trace, ram, spad = WORLD[beat]
    replay = FallRoute(WORLD['elf'], trace, ram, spad)
    ee = replay.ee
    counts, results = {}, {}
    if native_mode:
        for address, kind in ((USE, 'use'), (ACCEPTED, 'accepted'), (REENTRY, 'reentry')):
            ee.hooks[address] = world_hook(WORLD['native'], kind, counts, results)
    end = replay_end(trace)
    frames_env = os.environ.get('EM_WORLD_FRAMES')
    if frames_env:
        end = min(end, trace['first_counter'] + int(frames_env))
    frames, rows, states = [], 0, set()
    while replay.counter < end:
        row = replay.step()
        digest = hashlib.sha1(ee.mem)
        digest.update(ee.spad)
        frames.append((replay.counter, digest.hexdigest(), ee.read(PLAYER, 0x320), len(replay.events)))
        states.add((ee.load(PLAYER + 5, 1), ee.load(PLAYER + 0x1F0, 1)))
        if row is not None:
            shared.route_row_check(ee, row, (beat, 'native' if native_mode else 'original', replay.counter))
            rows += 1
    return frames, replay.events, rows, counts, results, sorted(states)


class SeededStage(shared.Stage):
    """shared.Stage (the original 0015BCF0 on a captured RAM image, seeded
    with the player's placement) on FallEE (the measured float model)."""

    def __init__(self, elf, ram, spad, position, yaw):
        self.ee = ee = FallEE(elf, ram, spad)
        self.frame, self.events = 0, []
        for address, name in shared.SOUND_HOOKS.items():
            ee.hooks[address] = self.recorder(name)
        for base in (0xA0, 0xB0):
            for i, value in enumerate(position):
                ee.putf(PLAYER + base + 4 * i, value)
        ee.putf(PLAYER + 0xC4, yaw)


def seeded_replay(beat, native_mode):
    """Every Use press of the beat (a trace frame whose +5 leaves idle for a
    Use state, led by a Cross input): the stage from the idle frame before
    it, seeded with that row's feet, body and yaw, pressed once and run
    until +5 is idle again. Each frame is checked against its trace row
    (state, +1F0 and clip exact; feet, body, yaw and clock within the
    climb capture route's bounds: 5e-4, 5e-4, 2e-5, 1e-3; the press
    frame's body and clock come from the idle pose, so they are compared
    from the next frame). A press the scan gives to an owner (+5 = 0x25)
    ends at its press frame, compared on state, +1F0 and clip only (the
    owner runs later in the same frame and may move the player)."""
    trace, ram, spad = WORLD[beat]
    rows = {r['counter']: r for r in trace['rows']}
    use_states = (2, 3, 6, 0xB, 0x24, 0x25)
    presses = [c for c in sorted(rows) if c - 1 in rows and rows[c - 1]['p5'] == 0 and rows[c]['p5'] in use_states]
    crosses = [trace['first_counter'] + i['f'] for i in trace['inputs'] if i['buttons'] & 0x4000]
    assert presses and all(any(0 < p - c <= 8 for c in crosses) for p in presses), (beat, presses, crosses)
    out = []
    for press in presses:
        before = rows[press - 1]
        stage = SeededStage(WORLD['elf'], ram, spad, tuple(before['pos']), before['yaw'])
        ee = stage.ee
        for i in range(3): ee.putf(PLAYER + 0xB0 + 4 * i, before['hip'][i])
        counts, results = {}, {}
        if native_mode:
            for address, kind in ((USE, 'use'), (ACCEPTED, 'accepted'), (REENTRY, 'reentry')):
                ee.hooks[address] = world_hook(WORLD['native'], kind, counts, results)
        frames, frame, press_input = [], press, {'press': 0x40}
        while True:
            stage.step(**press_input)
            press_input = {}
            digest = hashlib.sha1(ee.mem)
            digest.update(ee.spad)
            frames.append((frame, digest.hexdigest(), ee.read(PLAYER, 0x320)))
            r = rows[frame]
            where = (beat, 'native' if native_mode else 'original', frame)
            state = (ee.load(PLAYER + 5, 1), ee.load(PLAYER + 0x1F0, 1), ee.load(PLAYER + 0x20C, 2))
            assert state == (r['p5'], r['m1F0'], r['clip']), (where, 'state/action/clip', state)
            if r['p5'] == 0x25:
                # The winning owner runs after the player stage in the same
                # frame and may place the player (09: the fence door's
                # alignment), so the row's placement is not the stage's.
                break
            feet = [shared.number(ee.load(PLAYER + 0xA0 + 4 * i)) for i in range(3)]
            body = [shared.number(ee.load(PLAYER + 0xB0 + 4 * i)) for i in range(3)]
            assert all(abs(feet[i] - r['pos'][i]) <= 5e-4 for i in range(3)), (where, 'feet', feet, r['pos'])
            assert abs(shared.number(ee.load(PLAYER + 0xC4)) - r['yaw']) <= 2e-5, (where, 'yaw', r['yaw'])
            if frame != press:
                assert all(abs(body[i] - r['hip'][i]) <= 5e-4 for i in range(3)), (where, 'body', body, r['hip'])
                assert abs(shared.number(ee.load(PLAYER + 0x3C)) - r['clock']) < 1e-3, (where, 'clock')
            if r['p5'] in (0, 0x25):
                break
            frame += 1
        out.append((press, frames, list(stage.events), counts, results))
    return out


def world_job(job):
    kind, beat, native_mode = job
    return world_replay((beat, native_mode)) if kind == 'replay' else seeded_replay(beat, native_mode)


def world_main():
    started = time.time()
    elf = read_elf()
    beats = [b for b in os.environ.get('EM_WORLD_BEATS', ','.join(WORLD_BEATS)).split(',') if b]
    for beat in beats:
        loaded = shared.route_beat(beat)
        if isinstance(loaded, str):
            raise SystemExit('world mode: %s (docs/PLAYER_USE_DISPATCH.md)' % loaded)
        WORLD[beat] = loaded
    WORLD['elf'], WORLD['native'] = elf, build_native()
    seeded = [b for b in os.environ.get('EM_SEEDED_BEATS', ','.join(SEEDED_BEATS)).split(',') if b]
    for beat in seeded:
        loaded = shared.route_beat(beat)
        if isinstance(loaded, str):
            raise SystemExit('world mode: %s (docs/PLAYER_USE_DISPATCH.md)' % loaded)
        WORLD[beat] = loaded
    jobs = [(beat, mode) for beat in beats for mode in (False, True)]
    seeded_jobs = [(beat, mode) for beat in seeded for mode in (False, True)]
    everything = reference_mode.parallel_map(world_job, [('replay',) + j for j in jobs] +
                                             [('seeded',) + j for j in seeded_jobs])
    by_job = dict(zip(jobs, everything[:len(jobs)]))
    by_seeded = dict(zip(seeded_jobs, everything[len(jobs):]))
    for beat in seeded:
        a_presses, b_presses = by_seeded[(beat, False)], by_seeded[(beat, True)]
        assert len(a_presses) == len(b_presses) and a_presses, (beat, 'presses', len(a_presses), len(b_presses))
        for (press, a_frames, a_events, _, _), (_, b_frames, b_events, counts, results) in zip(a_presses, b_presses):
            assert len(a_frames) == len(b_frames), (beat, press, len(a_frames), len(b_frames))
            for (counter, a_hash, a_actor), (_, b_hash, b_actor) in zip(a_frames, b_frames):
                if a_actor != b_actor:
                    diff = [hex(k) for k in range(0x320) if a_actor[k] != b_actor[k]]
                    raise AssertionError((beat, press, counter, 'player bytes differ at', diff[:24]))
                assert a_hash == b_hash, (beat, press, counter, 'RAM or scratchpad differs outside the player')
            assert a_events == b_events, (beat, press, 'sound/effect calls differ')
            taken = sum(v for k, v in results.items() if k[0])
            assert taken > 0, (beat, press, 'the press was not dispatched natively', counts, results)
            owner = results.get((1, 0x25), 0) > 0
            print('%s press f%d (seeded): PASS %d frames identical (whole RAM + scratchpad + player record), '
                  '%s, %d sound/effect calls; calls %s; Use results (return, +5) %s' % (
                      beat, press - WORLD[beat][0]['first_counter'], len(a_frames),
                      'the owner press frame\'s state/+1F0/clip exact' if owner else
                      'state/+1F0/clip exact and feet/body/yaw/clock within the trace precision every frame',
                      len(a_events),
                      ', '.join('%s %d' % kv for kv in sorted(counts.items())),
                      {('%d' % k[0]) + ('/%X' % k[1] if k[1] is not None else ''): v
                       for k, v in sorted(results.items(), key=lambda kv: (kv[0][0], kv[0][1] or 0))}))
    for beat in beats:
        (a_frames, a_events, rows, _, _, a_states), (b_frames, b_events, b_rows, counts, results, _) = \
            by_job[(beat, False)], by_job[(beat, True)]
        assert len(a_frames) == len(b_frames), (beat, len(a_frames), len(b_frames))
        for (counter, a_hash, a_actor, a_count), (_, b_hash, b_actor, b_count) in zip(a_frames, b_frames):
            if a_actor != b_actor:
                diff = [hex(k) for k in range(0x320) if a_actor[k] != b_actor[k]]
                raise AssertionError((beat, counter, 'player bytes differ at', diff[:24]))
            assert a_count == b_count, (beat, counter, 'sound/effect call counts differ')
            assert a_hash == b_hash, (beat, counter, 'RAM or scratchpad differs outside the player')
        assert a_events == b_events, (beat, 'sound/effect calls differ')
        assert rows == b_rows and rows > 0, (beat, rows, b_rows)
        taken = sum(v for k, v in results.items() if k[0])
        assert counts.get('native use', 0) > 0 and taken > 0, (beat, 'no Use press dispatched natively', counts)
        seen = ' '.join('%X/%X' % st for st in a_states if st[0] in (2, 3, 6, 0xB, 0x24, 0x25))
        print('%s: PASS %d frames identical (whole RAM + scratchpad + player record), %d trace rows '
              'within precision, %d sound/effect calls; calls %s; Use results (return, +5) %s; '
              'Use states (+5/+1F0) %s' % (
                  beat, len(a_frames), rows, len(a_events),
                  ', '.join('%s %d' % kv for kv in sorted(counts.items())),
                  {('%d' % k[0]) + ('/%X' % k[1] if k[1] is not None else ''): v
                   for k, v in sorted(results.items(), key=lambda kv: (kv[0][0], kv[0][1] or 0))},
                  seen))
    print('player use dispatch world mode: PASS %d replayed beats, %d seeded beats (%.0fs)' % (
        len(beats), len(seeded), time.time() - started))


if __name__ == '__main__':
    if os.environ.get('EM_TEST_WORLD', '') not in ('', '0'):
        world_main()
    else:
        main()
