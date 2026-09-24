#!/usr/bin/env python3
"""Execute the original running-jump routines and compare em_player_running_jump.c.

docs/PLAYER_RUNNING_JUMP.md. The user's pinned ELF (and, in world mode, the
captured route RAM) supplies every instruction and table; none are embedded
here. Routines executed unmodified:

  0015EC50  the running-jump probe (Use chain)   0015FDF0  the aim solver
  001AA4E0  the nearest-target scan              001634A0  state 6 callback
  001747F0  state 0x24 callback                  00179880  drop accumulator

Their pure SDK leaves (00102948, 001026A0, 001029C0, 001029E8, 00102BB0,
00102918, 001B1470, 0011DF78) execute unhooked; every other callee is hooked,
scripted per case and recorded (never simulated as a claim about the
callee), and the native module gets the same script through its workers.
Scripted callees may rewrite record bytes and scratch words, so every
re-read after a call is checked. Any other call target is refused (closed
world), and the test asserts that the hooked set is exactly the set of jal
targets of the routines.

Arithmetic: COP1 and VU0 macro ops go through tools/ee_float_model.py (the
measured model, docs/EE_FLOAT_MODEL.md) via the recovery test's ModelEE,
wrapped here as JumpEE; the shared interpreter is not edited.

Default run (~10 s): unit cases with every conditional branch of the
routines taken both ways (DEAD lists the one outcome no valid input
reaches), fail-stop cuts, missing-worker refusals and the binding adapters.
EM_TEST_FULL=1: the exhaustive sweep. EM_TEST_WORLD=1: the route beats with
the running jump (12_crevice_jump, 14_roger_encounter; EM_WORLD_BEATS picks)
replayed over the captured RAM twice, the original stage against the stage
with these translations hooked in at their original addresses (their workers
run the original callees in the same EE); every frame the whole RAM and
scratchpad must be identical, and every trace row must match the capture.
"""
import ctypes as C
import hashlib
import math
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
from test_player_slide_reference import read_elf, s32  # noqa: E402
import test_player_recovery_reference as recovery  # noqa: E402

MASK = 0xFFFFFFFF
LANE = os.environ.get('EM_LANE', 'player_running_jump_reference')
OUT = ROOT / 'build' / LANE
_F, _I = struct.Struct('<f'), struct.Struct('<I')


def F(value):
    return _I.unpack(_F.pack(value))[0]


def b2f(word):
    return _F.unpack(_I.pack(word & MASK))[0]


# ======================================================================
# The interpreter with the measured float model
# ======================================================================

class JumpEE(recovery.ModelEE):
    """recovery.ModelEE (COP1/VU0 on ee_float_model, closed-world calls),
    recording the outcome of every conditional branch inside the translated
    routines."""

    def __init__(self, elf, ram=None, spad=None, cover=False):
        super().__init__(elf, ram, spad)
        self.outcomes = set() if cover else None

    def branch(self, word, pc):
        b = super().branch(word, pc)
        if b is not None and self.outcomes is not None and in_translated(pc):
            self.outcomes.add((pc, b[0]))
        return b


# ======================================================================
# The routines and their callees
# ======================================================================

PROBE, AIM, SCAN, JUMP, STATE24, DROP = 0x15EC50, 0x15FDF0, 0x1AA4E0, 0x1634A0, 0x1747F0, 0x179880
SIZES = {PROBE: 0x11A0, AIM: 0x42C, SCAN: 0x158, JUMP: 0x53C, STATE24: 0x1A4, DROP: 0x50}
TRANSLATED = set(SIZES)
LEAVES = {0x102948, 0x1026A0, 0x1029C0, 0x1029E8, 0x102BB0, 0x102918, 0x1B1470, 0x11DF78}


def in_translated(pc):
    return any(start <= pc < start + size for start, size in SIZES.items())


# Original callee -> worker name (every jal target of the routines above
# that is neither translated here nor a leaf).
CALLEES = {
    0x19AD00: 'move', 0x19AFE0: 'sweep', 0x19BC40: 'table', 0x177510: 'ledge',
    0x11E748: 'sqrt', 0x11E620: 'atan2', 0x11E2A8: 'sin', 0x11DE90: 'cos',
    0x174AC0: 'heading', 0x1AA410: 'radius', 0x1AA2A0: 'sight',
    0x1749A0: 'request', 0x1749F0: 'arbiter', 0x1C61D0: 'frames', 0x1FBD50: 'sound',
    0x178B90: 'translate', 0x178EC0: 'strafe', 0x1751A0: 'quadrant', 0x2243F0: 'react',
    0x17C860: 'grab', 0x17DEB0: 'dust', 0x17C580: 'land', 0x21D250: 'surface5d',
    0x21D2E0: 'teleport', 0x1764E0: 'probes', 0x175900: 'floor', 0x1796C0: 'fall_check',
    0x1760C0: 'column',
}


def jal_targets(ee, start, size):
    out = set()
    for pc in range(start, start + size, 4):
        word = ee.load(pc)
        if word >> 26 == 3:
            out.add((word & 0x3FFFFFF) << 2)
    return out


def check_callee_set(elf):
    ee = JumpEE(elf)
    targets = set()
    for start, size in SIZES.items():
        targets |= jal_targets(ee, start, size)
    missing = sorted(t for t in targets if t not in CALLEES and t not in TRANSLATED and t not in LEAVES)
    assert not missing, ('callees neither hooked, translated nor a leaf', [hex(t) for t in missing])
    unused = sorted(t for t in CALLEES if t not in targets)
    assert not unused, ('hooked addresses no translated routine calls', [hex(t) for t in unused])
    return len(targets)


# The float immediates 0015EC50 compares +B0/+B4/+B8 against, per area
# segment of its box chain (0015ED08..0015F738), read from the instructions:
# per axis, the (lower, upper) pairs of each c.lt.s / c.le.s test. Used only
# to place test positions on and around the boxes.
BOX_SEGMENTS = ((4, 0x15ED08, 0x15EDE4), (0xD, 0x15EDE4, 0x15F038), (0xF, 0x15F038, 0x15F27C),
                (0x10, 0x15F27C, 0x15F410), (0x13, 0x15F410, 0x15F678), (0x16, 0x15F678, 0x15F740))


def box_intervals(elf):
    ee = JumpEE(elf)
    out = {}
    for area, lo, hi in BOX_SEGMENTS:
        v0, freg, pending = 0, {}, {}
        intervals = {0xB0: [], 0xB4: [], 0xB8: []}
        for pc in range(lo, hi, 4):
            w = ee.load(pc)
            op, rs, rt = w >> 26, w >> 21 & 31, w >> 16 & 31
            if op == 0x0F and rt == 2:
                v0 = (w & 0xFFFF) << 16
            elif op == 0x0D and rs == 2 and rt == 2:
                v0 |= w & 0xFFFF
            elif op == 0x11 and rs == 4 and rt == 2:
                freg[w >> 11 & 31] = ('k', v0)
            elif op == 0x31 and rs == 19:
                freg[rt] = ('axis', w & 0xFFFF)
            elif op == 0x11 and rs == 16 and w & 63 in (52, 54):
                a, k = freg.get(w >> 11 & 31), freg.get(rt)
                if a and a[0] == 'axis' and k and k[0] == 'k':
                    if w & 63 == 52:
                        pending[a[1]] = k[1]
                    else:
                        intervals[a[1]].append((pending.pop(a[1]), k[1]))
        assert all(intervals.values()), (hex(area), intervals)
        out[area] = intervals
    return out


# ======================================================================
# Native side (ctypes)
# ======================================================================

class LiveActor(C.Structure):
    _fields_ = [('bytes', C.c_uint8 * 0x320), ('link_owner', C.c_void_p), ('link_prev', C.c_void_p),
                ('link_flags', C.c_uint8), ('link_type', C.c_uint8)]


ProbeHit = shared.ProbeHit
ClimbTable = recovery.ClimbTable
Ledge = recovery.Ledge


class Scene(C.Structure):
    _fields_ = [('area', C.c_uint8), ('subarea', C.c_uint8), ('spad3B8D', C.c_uint8),
                ('target_count', C.c_int16)]


class Scratch(C.Structure):
    _fields_ = [('s36A0', C.c_uint32 * 16), ('s38A0', C.c_uint32 * 16), ('s3A20', C.c_uint32),
                ('s3A28', C.c_uint32), ('s3A2C', C.c_uint32)]


class Target(C.Structure):
    _fields_ = [('object', C.c_void_p), ('flags', C.c_uint8), ('type', C.c_uint8), ('field34', C.c_int16)]


# The scratch words in Scratch order (the oracle compares all of them).
SCRATCH = tuple(0x700036A0 + 4 * i for i in range(16)) + tuple(0x700038A0 + 4 * i for i in range(16)) + \
    (0x70003A20, 0x70003A28, 0x70003A2C)

P = C.POINTER
A = P(LiveActor)
I, U32, FLT, VP, FP, IP = C.c_int, C.c_uint32, C.c_float, C.c_void_p, P(C.c_float), P(C.c_int)
FN = {
    'math1': C.CFUNCTYPE(FLT, VP, FLT),
    'math2': C.CFUNCTYPE(FLT, VP, FLT, FLT),
    'move': C.CFUNCTYPE(I, VP, A, FP, C.c_uint, P(ProbeHit)),
    'sweep': C.CFUNCTYPE(I, VP, A, FP, FP, C.c_uint, P(ProbeHit)),
    'table': C.CFUNCTYPE(I, VP, FP, P(ClimbTable)),
    'ledge': C.CFUNCTYPE(I, VP, P(ProbeHit), P(Ledge)),
    'column': C.CFUNCTYPE(I, VP, A, FP, I, FLT, IP),
    'link_type': C.CFUNCTYPE(I, VP, VP, P(C.c_uint8)),
    'target': C.CFUNCTYPE(I, VP, I, P(Target)),
    'target_xz': C.CFUNCTYPE(I, VP, VP, FP, FP),
    'target_radius': C.CFUNCTYPE(I, VP, VP, FP),
    'target_sight': C.CFUNCTYPE(I, VP, A, VP, FLT, IP),
    'actor_int_result': C.CFUNCTYPE(I, VP, A, I, IP),
    'request': C.CFUNCTYPE(I, VP, A, I, I, FLT),
    'arbiter': C.CFUNCTYPE(I, VP, A, I, FLT, FLT),
    'frames': C.CFUNCTYPE(I, VP, U32, I, P(C.c_int32)),
    'actor_int': C.CFUNCTYPE(I, VP, A, I),
    'actor': C.CFUNCTYPE(I, VP, A),
    'actor_result': C.CFUNCTYPE(I, VP, A, IP),
    'grab': C.CFUNCTYPE(I, VP, A, U32, IP),
    'actor_int2': C.CFUNCTYPE(I, VP, A, I, I),
    'root_clock': C.CFUNCTYPE(I, VP, P(U32)),
}
# EmPlayerRunningJumpWorkers after context/scratch, in header order.
WORKER_FIELDS = (
    ('sine', 'math1'), ('cosine', 'math1'), ('atan2', 'math2'), ('sqrt', 'math1'),
    ('move', 'move'), ('sweep', 'sweep'), ('table', 'table'), ('ledge', 'ledge'),
    ('column', 'column'), ('link_type', 'link_type'), ('target', 'target'),
    ('target_xz', 'target_xz'), ('target_radius', 'target_radius'),
    ('target_sight', 'target_sight'), ('heading', 'actor_int_result'), ('request', 'request'),
    ('arbiter', 'arbiter'), ('clip_frames', 'frames'), ('sound', 'actor_int'),
    ('translate', 'actor_int'), ('strafe', 'actor'), ('quadrant', 'actor'),
    ('react', 'actor_result'), ('grab', 'grab'), ('dust', 'actor'), ('land', 'actor'),
    ('surface5d', 'actor_int'), ('teleport', 'actor_int2'), ('probes', 'actor_int'),
    ('floor', 'actor_int_result'), ('fall_check', 'actor'), ('root_clock', 'root_clock'),
)


class Workers(C.Structure):
    _fields_ = [('context', VP), ('scratch', P(Scratch))] + [(n, FN[k]) for n, k in WORKER_FIELDS]


SCENE_FN = C.CFUNCTYPE(I, VP, P(Scene))


class Live(C.Structure):
    _fields_ = [('workers', Workers), ('scene', SCENE_FN), ('scene_context', VP),
                ('shared3A20', P(C.c_uint32))]


ENTRIES = ('probe', 'aim', 'scan', 'jump', 'state24')


def build_native(name='running_jump'):
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / (name + ('.dylib' if sys.platform == 'darwin' else '.so'))
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc',
                    'src/game/em_player_running_jump.c', 'src/game/em_owner_services_original.c',
                    'src/game/em_player_recovery.c', 'src/game/em_player_slide.c',
                    'src/game/em_player_climb.c', 'src/game/em_player_floor.c',
                    'src/game/em_player_fall.c', '-lm',
                    '-o', str(lib)], cwd=ROOT, check=True)
    n = C.CDLL(str(lib))
    W, S = P(Workers), P(Scene)
    n.em_player_running_jump_probe.argtypes = [A, S, W, IP]
    n.em_player_running_jump_aim.argtypes = [A, S, W, IP]
    n.em_player_running_jump_target.argtypes = [A, S, W, P(VP)]
    n.em_player_running_jump_tick.argtypes = [A, W]
    n.em_player_running_jump_state24_tick.argtypes = [A, W]
    n.em_player_running_jump_workers_bound.argtypes = [W]
    for name_ in ('em_player_running_jump_state6', 'em_player_running_jump_state24'):
        getattr(n, name_).argtypes = [VP, A]
    for name_ in ('em_player_running_jump_use_probe', 'em_player_running_jump_use_aim'):
        getattr(n, name_).argtypes = [VP, A, IP]
    return n


# ======================================================================
# Scripted callee effects (identical on both sides)
# ======================================================================

HALF_PI, AIM_K, K_4_01 = 0x3FC90FDB, 0x3F32B8C3, 0x408051EC
FLAGS = (0, 0x1000, 0x8000, 0x9000, 0x200, 0x1200)


def near(rng, word):
    """word or one of its neighbouring bit patterns."""
    return (word + rng.choice((-1, 0, 0, 1))) & MASK


def angle(rng):
    return rng.choice((F(rng.uniform(-3.14, 3.14)), F(rng.uniform(-3.14, 3.14)), near(rng, HALF_PI),
                       near(rng, HALF_PI | 0x80000000), 0, 0x80000000))


RECORD_WRITES = (
    (0xB0, 4, lambda r: F(r.uniform(-500, 500))), (0xB4, 4, lambda r: F(r.uniform(-50, 300))),
    (0xB8, 4, lambda r: F(r.uniform(-500, 500))), (0xC4, 4, angle), (0x218, 4, angle),
    (0x38, 4, lambda r: F(r.choice((0.0, -0.0, 0.2, 4.1, 1.5, -0.1)))),
    (0x2E4, 4, lambda r: F(r.choice((0.0, -0.0, 0.5, -0.5, 1e-40, 2.0)))),
    (0x2EC, 4, lambda r: F(r.choice((-0.04, -0.03, 0.0, 0.3)))),
    (0x270, 4, lambda r: F(r.choice((0.0, 1.0, 2.1)))), (0x2E0, 4, lambda r: F(r.choice((0.0, 0.2, 5.0)))),
    (0x2F4, 4, lambda r: F(r.uniform(-50, 300))), (0x24C, 4, lambda r: r.choice((0, 1, 2, 3))),
    (0x23F, 1, lambda r: r.randrange(4)), (0x23A, 1, lambda r: r.choice((0x5D, 5, 0))),
    (0xA, 1, lambda r: r.choice((0, 1, 0x81))), (0x314, 1, lambda r: r.choice((0, 1, 2))),
    (0x25C, 1, lambda r: r.randrange(4)), (0x200, 4, lambda r: r.choice(FLAGS)),
    (0x6, 1, lambda r: r.randrange(4)), (0x5, 1, lambda r: r.choice((6, 0x24, 7, 0))),
    (0x4, 1, lambda r: r.choice((1, 2))), (0x0, 1, lambda r: r.choice((0, 1, 3))),
    (0x3C, 4, lambda r: F(r.choice((10.0, 12.0, 5.0)))), (0x21C, 4, lambda r: F(r.uniform(0, 100))),
    (0x319, 1, lambda r: r.choice((0, 1))), (0xD0 + 4 * 5, 4, lambda r: F(r.uniform(-1, 1))),
    (0xD0 + 4 * 12, 4, lambda r: F(r.uniform(-500, 500))),
)
ACTOR_WORKERS = {'move', 'sweep', 'column', 'sight', 'heading', 'request', 'arbiter', 'sound',
                 'translate', 'strafe', 'quadrant', 'react', 'grab', 'dust', 'land', 'surface5d',
                 'teleport', 'probes', 'floor', 'fall_check'}


def random_hit(rng):
    return {'point': [F(rng.uniform(-400, 400)) for _ in range(3)],
            'node': rng.choice((0x2000, 0x2005, 0x2046, 0x1000, 0x4000, 0x20FF, 0x3000)),
            'normal': [F(rng.uniform(-1, 1)) for _ in range(3)]}


def random_table(rng, b4):
    count = rng.choice((0, 0, 1, 1, 2, 3, 4, 6, 16))
    lip = M.ee_add(K_4_01, b4)
    drop = M.ee_add(b4, 0xC08051EC)
    heights = (lip, lip + 1, lip - 1, drop, drop + 1, drop - 1, drop + 2, drop - 2, b4,
               M.ee_add(b4, F(-20.0)), M.ee_add(b4, F(10.0)), F(rng.uniform(-100, 300)))
    return {'count': count, 'flags': [rng.choice((0, 1, 1, 3, 2)) for _ in range(count)],
            'height': [rng.choice(heights) & MASK for _ in range(count)],
            'aux': [F(rng.uniform(0, 1)) for _ in range(count)]}


def random_frame(rng):
    return {'point': [F(rng.uniform(-400, 400)) for _ in range(3)],
            'normal': [F(rng.uniform(-1, 1)) for _ in range(3)], 'heading': angle(rng),
            'matrix': [F(rng.uniform(-2, 2)) for _ in range(16)]}


def host(fn, bits):
    x = b2f(bits)
    try:
        value = fn(x)
    except ValueError:
        value = 0.0
    if value != value or abs(value) > 3.4e38:
        value = 0.0
    return F(value)


def effect_for(rng, name, ctx):
    """What a hooked callee does in this case: its return value, the record
    bytes it writes, scratch words it writes, the probe hit / column table /
    ledge frame it leaves, a float return."""
    e = {'ret': 0, 'fret': None, 'writes': [], 'spad': [], 'hit': None, 'table': None, 'frame': None}
    chance = rng.random
    if name in ACTOR_WORKERS and chance() < 0.35:
        for _ in range(rng.randint(1, 2)):
            offset, size, gen = rng.choice(RECORD_WRITES)
            e['writes'].append((offset, size, gen(rng)))
    if chance() < 0.1:
        e['spad'].append((rng.choice(SCRATCH), rng.choice((F(rng.uniform(-50, 50)), 0, F(1000.0)))))
    if name == 'move':
        e['ret'] = rng.choice((0, 0, 1, 2, 4))
        e['hit'] = random_hit(rng)
    elif name == 'sweep':
        e['ret'] = rng.choice((0, 0, 1, 1, 2))
        e['hit'] = random_hit(rng)
    elif name == 'table':
        e['table'] = random_table(rng, ctx['b4'])
    elif name == 'ledge':
        e['frame'] = random_frame(rng)
    elif name == 'sqrt':
        e['fret'] = rng.choice((host(math.sqrt, ctx['x']),) * 3 + (F(5.0), F(13.5), F(20.0), 0, F(27.0)))
    elif name in ('sin', 'cos'):
        e['fret'] = host(math.sin if name == 'sin' else math.cos, ctx['x'])
    elif name == 'atan2':
        y, x = b2f(ctx['x']), b2f(ctx['y'])
        exact = F(math.atan2(y, x)) if y == y and x == x else 0
        e['fret'] = rng.choice((exact, exact, angle(rng), F(rng.uniform(-3.14, 3.14))))
    elif name == 'radius':
        e['fret'] = F(rng.uniform(0, 30))
    elif name == 'sight':
        e['ret'] = rng.choice((0, 1, 1, 7))
        if chance() < 0.9:
            e['spad'].append((0x70003A20, rng.choice((F(999.99), F(1000.0), 0x447A0001, F(5.0),
                                                     F(50.0), F(50.0), F(-1.0)))))
    elif name == 'heading':
        e['ret'] = rng.choice((0, 1, 1, 5))
        if chance() < 0.4:
            e['writes'].append((0x23F, 1, rng.randrange(4)))
        if chance() < 0.3:
            e['spad'].append((0x70003A20, F(rng.uniform(-3, 3))))
    elif name == 'react':
        e['ret'] = rng.choice((0, 0, 1))
    elif name == 'grab':
        e['ret'] = rng.choice((0, 0, 0, 1))
    elif name == 'floor':
        contact = rng.choice((0, 0, 1, 0x81))
        e['ret'] = contact
        e['writes'].append((0xA, 1, contact))
        if chance() < 0.5:
            e['writes'].append((0x23A, 1, rng.choice((0x5D, 5, 5, 0))))
    elif name == 'column':
        e['ret'] = rng.choice((0, 1))
    elif name == 'frames':
        e['ret'] = rng.choice((25, 40, 0, -7, 120, 0x1000001))
    return e


class Script:
    def __init__(self, seed, fail_at=None):
        self.seed, self.count, self.fail_at = seed, 0, fail_at

    def next(self, name, ctx):
        rng = random.Random('%d:%d:%s' % (self.seed, self.count, name))
        index = self.count
        self.count += 1
        return index, effect_for(rng, name, ctx)


# ======================================================================
# Cases
# ======================================================================

ACTOR, OWNER, LIST, OBJECTS = 0x680000, 0x690000, 0x6A0000, 0x6B0000
NODE_TABLE, NODE0, RECORDS = 0x6C0000, 0x6D0000, 0x6E0000
CALLER_S1 = 0x5A5A0004          # the $s1 0015B130 hands down (bit 2 set on purpose)
S1_CALLER = 2                   # EM_PLAYER_LAND_S1_CALLER
BOXES = None


def put(actor, offset, size, value):
    actor[offset:offset + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'little')


def box_value(rng, intervals):
    lo, hi = rng.choice(intervals)
    return rng.choice((lo, hi, (lo - 1) & MASK, (hi + 1) & MASK, F((b2f(lo) + b2f(hi)) / 2),
                       F(b2f(lo) - 30.0), F(b2f(hi) + 30.0)))


def yaw_matrix(rng, x, y, z):
    t = rng.uniform(-3.2, 3.2)
    c, s = math.cos(t), math.sin(t)
    rows = [c, 0.0, -s, 0.0, 0.0, 1.0, 0.0, 0.0, s, 0.0, c, 0.0, b2f(x), b2f(y), b2f(z), 1.0]
    return [F(v) for v in rows]


# The box boundary cases (every run): for each area with boxes, its enabling
# D_00810701 and every (y, x, z) interval triple: all 27 combinations of
# (midpoint, just below, just above) per axis, plus each axis exactly on its
# lower and on its upper bound with the others at their midpoints.
# EM_TEST_FULL=1 runs all 125 combinations of the five per axis.
BOX_BASE = 10_000_000
BOX_SUBAREA = {4: 0, 0xD: 0, 0xF: 1, 0x10: 1, 0x13: 0, 0x16: 0}


PLAN = None


def box_plan():
    global PLAN
    if PLAN is not None:
        return PLAN
    plan = []
    for area in sorted(BOXES):
        iv = BOXES[area]
        for jy in range(len(iv[0xB4])):
            for jx in range(len(iv[0xB0])):
                for jz in range(len(iv[0xB8])):
                    for pattern in range(125):
                        k = (pattern // 25, pattern // 5 % 5, pattern % 5)
                        edges = [i for i in range(3) if k[i] < 2]
                        if not reference_mode.FULL and edges and \
                                (len(edges) > 1 or any(k[i] != 2 for i in range(3) if i not in edges)):
                            continue
                        plan.append((area, (jy, jx, jz), k))
    PLAN = plan
    return plan


def bound_choice(interval, k):
    lo, hi = interval
    return (lo, hi, F((b2f(lo) + b2f(hi)) / 2), (lo - 1) & MASK, (hi + 1) & MASK)[k]


def make_case(seed):
    if seed >= BOX_BASE:
        area, (jy, jx, jz), (ky, kx, kz) = box_plan()[seed - BOX_BASE]
        case = make_case(seed - BOX_BASE)
        actor = bytearray(case['actor'])
        iv = BOXES[area]
        put(actor, 0xB4, 4, bound_choice(iv[0xB4][jy], ky))
        put(actor, 0xB0, 4, bound_choice(iv[0xB0][jx], kx))
        put(actor, 0xB8, 4, bound_choice(iv[0xB8][jz], kz))
        put(actor, 0x314, 1, 0)
        return dict(case, seed=seed, entry='probe', actor=bytes(actor), area=area,
                    subarea=BOX_SUBAREA[area], owner=None)
    rng = random.Random(seed)
    entry = rng.choice(('probe',) * 7 + ('aim',) * 4 + ('scan',) * 2 + ('jump',) * 7 + ('state24',) * 3)
    actor = bytearray(rng.getrandbits(8) for _ in range(0x320))
    wild = rng.random() < 0.04
    area = rng.choice((4, 0xD, 0xF, 0x10, 0x13, 0x16, 0xB, 0xB, 0x15, 1))
    subarea = rng.choice((0, 1, 1, 0, 2))
    if not wild:
        if area in BOXES and rng.random() < 0.9:
            for axis in (0xB0, 0xB4, 0xB8):
                put(actor, axis, 4, box_value(rng, BOXES[area][axis]))
        else:
            for axis in (0xB0, 0xB4, 0xB8):
                put(actor, axis, 4, F(rng.uniform(-500, 500)))
        put(actor, 0xBC, 4, F(1.0))
        m = yaw_matrix(rng, *(int.from_bytes(actor[o:o + 4], 'little') for o in (0xB0, 0xB4, 0xB8)))
        for i, word in enumerate(m):
            put(actor, 0xD0 + 4 * i, 4, word)
        put(actor, 0x38, 4, F(rng.choice((0.0, 0.2, 4.1, 6.3, 0.9, -0.0))))
        put(actor, 0x270, 4, F(rng.choice((0.0, 1.0, 2.1, 0.18))))
        put(actor, 0x2E0, 4, F(rng.choice((0.0, 0.2, 0.21, 6.3))))
        put(actor, 0x2E4, 4, F(rng.choice((0.0, -0.0, 0.5, -0.5, 1e-40, 0.04, 0.05, -3.97, -3.99))))
        put(actor, 0x2EC, 4, F(rng.choice((-0.04, -0.03, 0.0, -0.029))))
        base = rng.uniform(-50, 300)
        put(actor, 0x2F4, 4, F(base))
        if rng.random() < 0.5:
            put(actor, 0xB4, 4, F(base + rng.choice((0.0, 0.5, -0.5, 1e-3))))
        put(actor, 0x3C, 4, F(rng.choice((10.0, 10.000001, 9.999999, 20.0, 0.0))))
        put(actor, 0x21C, 4, F(rng.uniform(0, 100)))
    for offset in (0xC4, 0x218):        # angles stay angles: 001B1470 of a huge one never ends
        put(actor, offset, 4, angle(rng))
    put(actor, 0x314, 1, rng.choice((0, 0, 0, 1, 2)))
    put(actor, 0x319, 1, rng.choice((0, 1, 2, 3)))
    put(actor, 0x25C, 1, rng.randrange(4))
    put(actor, 0x23F, 1, rng.randrange(4))
    put(actor, 0x236, 1, rng.choice((0, 0, 1)))
    put(actor, 0x23A, 1, rng.choice((0x5D, 5, 5)))
    put(actor, 0x24C, 4, rng.choice((0, 1, 1, 2, 3)))
    put(actor, 0x200, 4, rng.choice(FLAGS) | (rng.getrandbits(8) if rng.random() < 0.3 else 0))
    put(actor, 0x4, 1, rng.choice((1, 1, 2)))
    put(actor, 0x5, 1, rng.choice((6, 0x24, 0x24, 7)))
    put(actor, 0x0, 1, rng.choice((0, 1, 3)))
    put(actor, 0xA, 1, rng.choice((0, 1)))
    sub6 = {'jump': (0, 0, 1, 1, 2, 2, 2, 3, 3, 3, 0x63, 4), 'state24': (0, 1, 1, 2, 2, 3)}.get(entry, (0,))
    put(actor, 0x6, 1, rng.choice(sub6))
    owner = rng.choice((None, None, 0x28, 2, 2, 5))
    put(actor, 0x308, 4, OWNER if owner is not None else 0)
    targets = []
    for _ in range(rng.choice((0, 0, 1, 2, 3, 5))):
        targets.append({'flags': rng.choice((2, 2, 0x22, 0x62, 3, 0x12)),
                        'type': rng.choice((1, 2, 4, 5, 6, 7, 8, 0xC, 0, 3, 9, 0x28)),
                        'f34': rng.choice((0, 1, 1, -1, 0x100)),
                        'x': F(rng.uniform(-500, 500)), 'z': F(rng.uniform(-500, 500))})
    return {'seed': seed, 'entry': entry, 'actor': bytes(actor), 'area': area, 'subarea': subarea,
            'spad3B8D': rng.choice((0, 0, 0, 1)), 'targets': targets, 'owner': owner,
            'clock': F(rng.uniform(0, 100)),
            'scratch': [rng.getrandbits(32) for _ in range(len(SCRATCH))]}


# ======================================================================
# The original side
# ======================================================================

class UnitOracle:
    def __init__(self, elf, cover=True):
        self.ee = JumpEE(elf, cover=cover)
        self.ee.allowed = LEAVES | TRANSLATED
        self.ee.limit = 1_000_000
        for address, name in CALLEES.items():
            self.ee.hooks[address] = self.hook(name)

    def vec(self, address, n):
        return tuple(self.ee.load(address + 4 * i) for i in range(n))

    def hook(self, name):
        def run(ee):
            entry = self.log_entry(name, ee)
            ctx = {'b4': ee.load(ACTOR + 0xB4), 'x': ee.f[12] & MASK, 'y': ee.f[13] & MASK}
            index, e = self.script.next(name, ctx)
            self.log.append(entry)
            for offset, size, value in e['writes']:
                ee.save(ACTOR + offset, value, size)
            for address, value in e['spad']:
                ee.save(address, value)
            if e['hit'] is not None:
                self.publish(e['hit'])
            if e['table'] is not None:
                t = e['table']
                ee.save(0x700031E0, t['count'])
                for i in range(t['count']):
                    ee.save(0x70003170 + 2 * i, t['flags'][i], 2)
                    ee.save(0x700030F0 + 4 * i, t['height'][i])
                    ee.save(0x282250 + 4 * i, t['aux'][i])
            if e['frame'] is not None:
                fr = e['frame']
                for i in range(3):
                    ee.save(0x70003050 + 4 * i, fr['point'][i])
                    ee.save(0x70003060 + 4 * i, fr['normal'][i])
                ee.save(0x700031E4, fr['heading'])
                for i in range(16):
                    ee.save(0x70003070 + 4 * i, fr['matrix'][i])
            if e['fret'] is not None:
                ee.f[0] = e['fret']
            ee.ret_int(e['ret'])
        return run

    def publish(self, hit):
        ee = self.ee
        for i in range(3):
            ee.save(0x700031B0 + 4 * i, hit['point'][i])
        record = self.records
        self.records += 0x40
        ee.save(record + 0x1A, hit['node'], 2)
        for i in range(3):
            ee.save(record + 0x24 + 4 * i, hit['normal'][i])
        ee.save(0x700031D0, record)
        ee.save(0x700031D4, 0)

    def log_entry(self, name, ee):
        a = ee.arg
        if name in ACTOR_WORKERS:
            assert a(0) == ACTOR, (name, 'a0 is not the record', hex(a(0)))
        f12, f13 = ee.f[12] & MASK, ee.f[13] & MASK
        if name == 'move': return (name, self.vec(a(1), 4), a(2))
        if name == 'sweep': return (name, self.vec(a(1), 3), self.vec(a(2), 3), a(3))
        if name == 'table': return (name, self.vec(a(0), 4))
        if name == 'ledge': return (name, self.vec(0x700031B0, 3))
        if name == 'column':
            assert a(1) == ACTOR + 0xB0, ('001760C0 point', hex(a(1)))
            return (name, self.vec(a(1), 3), s32(a(2)), f12)
        if name in ('sqrt', 'sin', 'cos'): return (name, f12)
        if name == 'atan2': return (name, f12, f13)
        if name == 'radius': return (name, a(0))
        if name == 'sight': return (name, a(1), f12)
        if name == 'request': return (name, s32(a(1)), s32(a(2)), f12)
        if name == 'arbiter': return (name, s32(a(1)), f12, f13)
        if name == 'frames': return (name, a(0), s32(a(1)))
        if name == 'sound':
            assert s32(a(2)) == 0 and f12 == F(300.0), ('sound arguments', a(2), hex(f12))
            return (name, s32(a(1)))
        if name in ('heading', 'translate', 'surface5d', 'floor'): return (name, s32(a(1)))
        if name == 'teleport': return (name, s32(a(1)), s32(a(2)))
        if name == 'grab': return (name, f12)
        if name == 'probes':
            source = {0: 0, ACTOR: 1, CALLER_S1: 2}.get(ee.r[17] & MASK)
            assert source is not None, ('001764E0 $s1', hex(ee.r[17] & MASK))
            return (name, source)
        return (name,)

    def run(self, case, script):
        ee = self.ee
        self.script, self.log, self.records = script, [], RECORDS
        ee.r, ee.rh = [0] * 32, [0] * 32
        ee.f, ee.acc, ee.cond = [0] * 32, 0, False
        ee.vacc, ee.q = [0, 0, 0, 0], 0
        ee.r[28], ee.r[29], ee.r[17] = 0x27D370, shared.STACK_TOP, CALLER_S1
        ee.write(ACTOR, case['actor'])
        ee.save(0x810700, case['area'], 1)
        ee.save(0x810701, case['subarea'], 1)
        ee.save(0x70003B8D, case['spad3B8D'], 1)
        ee.save(0x275B94, len(case['targets']), 2)
        ee.save(0x275B8C, LIST)
        for i, t in enumerate(case['targets']):
            obj = OBJECTS + 0x400 * i
            ee.save(LIST + 4 * i, obj)
            ee.save(obj + 2, t['flags'], 1)
            ee.save(obj + 3, t['type'], 1)
            ee.save(obj + 0x34, t['f34'], 2)
            ee.save(obj + 0xB0, t['x'])
            ee.save(obj + 0xB8, t['z'])
        ee.save(OWNER + 3, case['owner'] or 0, 1)
        ee.save(0x275B40, NODE_TABLE)
        ee.save(NODE_TABLE, NODE0)
        ee.save(NODE0 + 8, case['clock'])
        for address, word in zip(SCRATCH, case['scratch']):
            ee.save(address, word)
        entry = {'probe': PROBE, 'aim': AIM, 'scan': SCAN, 'jump': JUMP, 'state24': STATE24}[case['entry']]
        ee.call(entry, (ACTOR,))
        v0 = s32(ee.r[2]) if case['entry'] in ('probe', 'aim') else \
            (ee.r[2] & MASK if case['entry'] == 'scan' else None)
        return {'actor': ee.read(ACTOR, 0x320), 'scratch': [ee.load(a) for a in SCRATCH],
                'log': self.log, 'v0': v0}


# ======================================================================
# The native side
# ======================================================================

def words_of(pointer, n):
    w = C.cast(pointer, P(C.c_uint32))
    return tuple(w[i] for i in range(n))


def set_words(address, values):
    w = (C.c_uint32 * len(values)).from_address(address)
    for i, v in enumerate(values):
        w[i] = v & MASK


def fill_hit(hit, h, kind):
    base = C.addressof(hit.contents)
    set_words(base + ProbeHit.point.offset, h['point'])
    set_words(base + ProbeHit.normal.offset, h['normal'])
    hit.contents.node = h['node']
    hit.contents.kind = kind


class NativeRun:
    """em_player_running_jump.c with Python workers replaying the script."""

    def __init__(self, native, case, script, missing=None):
        self.native, self.case, self.script, self.log = native, case, script, []
        self.live = LiveActor()
        C.memmove(self.live.bytes, case['actor'], 0x320)
        self.live.link_prev = OWNER if case['owner'] is not None else None
        self.scratch = Scratch()
        C.memmove(C.addressof(self.scratch), struct.pack('<%dI' % len(SCRATCH), *case['scratch']),
                  4 * len(SCRATCH))
        self.keep = []
        fields = {}
        for field, kind in WORKER_FIELDS:
            fields[field] = FN[kind](self.worker(field)) if field != missing else FN[kind]()
            self.keep.append(fields[field])
        self.workers = Workers(None, C.pointer(self.scratch), **fields)
        if missing == 'scratch':
            self.workers.scratch = P(Scratch)()
        self.scene = Scene(case['area'], case['subarea'], case['spad3B8D'], len(case['targets']))

    def scratch_words(self):
        return list(struct.unpack('<%dI' % len(SCRATCH),
                                  C.string_at(C.addressof(self.scratch), 4 * len(SCRATCH))))

    def spad_store(self, address, value):
        index = SCRATCH.index(address)
        set_words(C.addressof(self.scratch) + 4 * index, [value])

    def call(self, name, entry, ctx):
        self.log.append(entry)
        index, e = self.script.next(name, ctx)
        if self.script.fail_at == index:
            return None
        for offset, size, value in e['writes']:
            for i in range(size):
                self.live.bytes[offset + i] = (value >> (8 * i)) & 0xFF
        for address, value in e['spad']:
            self.spad_store(address, value)
        return e

    def ctx(self, x=0, y=0):
        return {'b4': int.from_bytes(bytes(self.live.bytes[0xB4:0xB8]), 'little'), 'x': x, 'y': y}

    def worker(self, field):
        c = self.case
        fb = F

        def math1(name):
            def fn(_, x):
                e = self.call(name, (name, fb(x)), self.ctx(fb(x)))
                if e is None:
                    return 0.0
                return b2f(e['fret'])
            return fn

        if field == 'sine': return math1('sin')
        if field == 'cosine': return math1('cos')
        if field == 'sqrt': return math1('sqrt')
        if field == 'atan2':
            def atan2(_, y, x):
                e = self.call('atan2', ('atan2', fb(y), fb(x)), self.ctx(fb(y), fb(x)))
                return 0.0 if e is None else b2f(e['fret'])
            return atan2
        if field in ('move', 'sweep'):
            def probe(_, a, *args):
                hit = args[-1]
                if field == 'move':
                    entry = ('move', words_of(args[0], 4), args[1])
                else:
                    entry = ('sweep', words_of(args[0], 3), words_of(args[1], 3), args[2])
                e = self.call(field, entry, self.ctx())
                if e is None:
                    return -1
                fill_hit(hit, e['hit'], e['ret'])
                return e['ret']
            return probe
        if field == 'table':
            def table(_, at, out):
                e = self.call('table', ('table', words_of(at, 4)), self.ctx())
                if e is None:
                    return -1
                t = e['table']
                out.contents.count = t['count']
                base = C.addressof(out.contents)
                for i in range(t['count']):
                    out.contents.flags[i] = t['flags'][i]
                    set_words(base + ClimbTable.height.offset + 4 * i, [t['height'][i]])
                    set_words(base + ClimbTable.aux.offset + 4 * i, [t['aux'][i]])
                return 0
            return table
        if field == 'ledge':
            def ledge(_, hit, out):
                point = words_of(C.addressof(hit.contents) + ProbeHit.point.offset, 3)
                e = self.call('ledge', ('ledge', point), self.ctx())
                if e is None:
                    return -1
                fr = e['frame']
                set_words(C.addressof(out.contents), fr['point'] + fr['normal'] + [fr['heading']] + fr['matrix'])
                return 0
            return ledge
        if field == 'column':
            def column(_, a, at, arg, height, out):
                e = self.call('column', ('column', words_of(at, 3), arg, fb(height)), self.ctx())
                if e is None:
                    return -1
                out[0] = e['ret']
                return 0
            return column
        if field == 'link_type':
            def link_type(_, owner, out):
                assert owner == OWNER, ('link owner', owner)
                out[0] = c['owner']
                return 0
            return link_type
        if field == 'target':
            def target(_, index, out):
                assert 0 <= index < len(c['targets']), ('target index', index)
                t = c['targets'][index]
                out.contents.object = OBJECTS + 0x400 * index
                out.contents.flags, out.contents.type = t['flags'], t['type']
                out.contents.field34 = t['f34']
                return 0
            return target
        if field == 'target_xz':
            def target_xz(_, obj, x, z):
                t = c['targets'][(obj - OBJECTS) // 0x400]
                set_words(C.addressof(x.contents), [t['x']])
                set_words(C.addressof(z.contents), [t['z']])
                return 0
            return target_xz
        if field == 'target_radius':
            def radius(_, obj, out):
                e = self.call('radius', ('radius', obj), self.ctx())
                if e is None:
                    return -1
                set_words(C.addressof(out.contents), [e['fret']])
                return 0
            return radius
        if field == 'target_sight':
            def sight(_, a, obj, r, out):
                e = self.call('sight', ('sight', obj, fb(r)), self.ctx(fb(r)))
                if e is None:
                    return -1
                out[0] = e['ret']
                return 0
            return sight
        if field in ('heading', 'floor'):
            def with_result(_, a, arg, out):
                e = self.call(field, (field, arg), self.ctx())
                if e is None:
                    return -1
                out[0] = s32(e['ret'])
                return 0
            return with_result
        if field == 'request':
            return lambda _, a, clip, force, blend: (
                -1 if self.call('request', ('request', clip, force, fb(blend)), self.ctx()) is None else 0)
        if field == 'arbiter':
            return lambda _, a, clip, blend, frame: (
                -1 if self.call('arbiter', ('arbiter', clip, fb(blend), fb(frame)), self.ctx()) is None else 0)
        if field == 'clip_frames':
            def frames(_, bank, clip, out):
                e = self.call('frames', ('frames', bank, clip), self.ctx())
                if e is None:
                    return -1
                out[0] = s32(e['ret'])
                return 0
            return frames
        if field in ('sound', 'translate', 'surface5d', 'probes'):
            return lambda _, a, arg: (-1 if self.call(field, (field, arg), self.ctx()) is None else 0)
        if field in ('strafe', 'quadrant', 'dust', 'land', 'fall_check'):
            return lambda _, a: (-1 if self.call(field, (field,), self.ctx()) is None else 0)
        if field == 'react':
            def react(_, a, out):
                e = self.call('react', ('react',), self.ctx())
                if e is None:
                    return -1
                out[0] = s32(e['ret'])
                return 0
            return react
        if field == 'grab':
            def grab(_, a, reach, out):
                e = self.call('grab', ('grab', reach), self.ctx(reach))
                if e is None:
                    return -1
                out[0] = s32(e['ret'])
                return 0
            return grab
        if field == 'teleport':
            return lambda _, a, frames, hold: (
                -1 if self.call('teleport', ('teleport', frames, hold), self.ctx()) is None else 0)
        if field == 'root_clock':
            def root_clock(_, out):
                out[0] = c['clock']
                return 0
            return root_clock
        raise AssertionError(field)

    def run(self, adapter=False, shared3A20=None):
        n, entry = self.native, self.case['entry']
        W, L, S = C.byref(self.workers), C.byref(self.live), C.byref(self.scene)
        out, ptr = C.c_int(-99), C.c_void_p(0xDEAD)
        v0 = None
        if adapter:
            scene = self.scene

            def provide(_, s):
                s.contents.area, s.contents.subarea = scene.area, scene.subarea
                s.contents.spad3B8D, s.contents.target_count = scene.spad3B8D, scene.target_count
                return 0
            self.provider = SCENE_FN(provide)
            live = Live(self.workers, self.provider, None,
                        C.pointer(shared3A20) if shared3A20 is not None else P(C.c_uint32)())
            self.live_context = live
            X = C.byref(live)
            if entry == 'probe': result = n.em_player_running_jump_use_probe(X, L, C.byref(out)); v0 = out.value
            elif entry == 'aim': result = n.em_player_running_jump_use_aim(X, L, C.byref(out)); v0 = out.value
            elif entry == 'jump': result = n.em_player_running_jump_state6(X, L)
            elif entry == 'state24': result = n.em_player_running_jump_state24(X, L)
            else: raise AssertionError(entry)
        elif entry == 'probe':
            result = n.em_player_running_jump_probe(L, S, W, C.byref(out)); v0 = out.value
        elif entry == 'aim':
            result = n.em_player_running_jump_aim(L, S, W, C.byref(out)); v0 = out.value
        elif entry == 'scan':
            result = n.em_player_running_jump_target(L, S, W, C.byref(ptr)); v0 = ptr.value or 0
        elif entry == 'jump':
            result = n.em_player_running_jump_tick(L, W)
        else:
            result = n.em_player_running_jump_state24_tick(L, W)
        return result, {'actor': bytes(self.live.bytes), 'scratch': self.scratch_words(),
                        'log': self.log, 'v0': v0}


# ======================================================================
# The unit comparison
# ======================================================================

ELF = NATIVE = ORACLE = None


def compare(where, want, got):
    assert want['log'] == got['log'], (where, 'worker calls', want['log'], got['log'])
    if want['actor'] != got['actor']:
        diff = [hex(k) for k in range(0x320) if want['actor'][k] != got['actor'][k]]
        raise AssertionError((where, 'record bytes differ at', diff[:24]))
    if want['scratch'] != got['scratch']:
        diff = [hex(SCRATCH[i]) for i in range(len(SCRATCH)) if want['scratch'][i] != got['scratch'][i]]
        raise AssertionError((where, 'scratch words differ at', diff))
    assert want['v0'] == got['v0'], (where, 'return', want['v0'], got['v0'])


def run_case(seed):
    global ORACLE
    if ORACLE is None:
        ORACLE = UnitOracle(ELF)
    case = make_case(seed)
    want = ORACLE.run(case, Script(seed))
    result, got = NativeRun(NATIVE, case, Script(seed)).run()
    where = (seed, case['entry'], case['actor'][6])
    assert result == 0, (where, 'native fault', result)
    compare(where, want, got)
    faults = adapters = 0
    # fail-stop: a worker faulting stops the routine at that call
    # (the SDK math workers return a float and cannot fault, so they are skipped)
    cuttable = [i for i, entry in enumerate(want['log']) if entry[0] not in ('sqrt', 'sin', 'cos', 'atan2')]
    if cuttable and seed % 5 == 0:
        k = random.Random(seed).choice(cuttable)
        result, cut = NativeRun(NATIVE, case, Script(seed, fail_at=k)).run()
        assert result == -1, (where, 'fault not reported', k)
        assert cut['log'] == want['log'][:k + 1], (where, 'calls after a fault', k)
        faults = 1
    # the binding adapters: same result, with and without a shared 0x70003A20
    if case['entry'] != 'scan' and seed % 7 == 0:
        shared_word = C.c_uint32(case['scratch'][SCRATCH.index(0x70003A20)]) if seed % 2 else None
        result, got = NativeRun(NATIVE, case, Script(seed)).run(adapter=True, shared3A20=shared_word)
        assert result == 0, (where, 'adapter fault', result)
        compare(where + ('adapter',), want, got)
        if shared_word is not None:
            assert shared_word.value == want['scratch'][SCRATCH.index(0x70003A20)], (where, 'shared 3A20')
        adapters = 1
    return case['entry'], len(want['log']), faults, adapters, tuple(sorted(ORACLE.ee.outcomes))


def refusal_checks(native):
    """Each worker (and the scratch) missing: every entry point returns -1
    before any write and calls nothing. Also the two faults that stand in
    for out-of-range original reads."""
    count = 0
    base = make_case(1000)
    for field in [f for f, _ in WORKER_FIELDS] + ['scratch']:
        for entry in ENTRIES:
            case = dict(base, entry=entry)
            result, got = NativeRun(native, case, Script(1000), missing=field).run()
            assert result == -1 and got['log'] == [] and got['actor'] == case['actor'], (field, entry)
            count += 1
    # 001634A0 case 1 with +25C = 4: D_002485D0 / D_002485B0 end at tier 3.
    actor = bytearray(base['actor'])
    put(actor, 6, 1, 1)
    put(actor, 0x200, 4, 0x1000)
    put(actor, 0x25C, 1, 4)
    case = dict(base, entry='jump', actor=bytes(actor))
    result, _ = NativeRun(native, case, Script(1000)).run()
    assert result == -1, 'tier 4 must fault'
    # 001AA4E0 with a negative count: the original would walk 2^32 entries.
    run = NativeRun(native, dict(base, entry='scan'), Script(1000))
    run.scene.target_count = -1
    result, _ = run.run()
    assert result == -1, 'negative target count must fault'
    return count + 2


def branch_sites(elf):
    ee = JumpEE(elf)
    sites = set()
    for start, size in SIZES.items():
        for pc in range(start, start + size, 4):
            word = ee.load(pc)
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            if op in (4, 20) and rs == 0 and rt == 0:
                continue                                  # b: unconditional
            if ee.branch(word, pc) is not None:
                sites.add(pc)
    return sites


# The one branch outcome no valid input reaches: 0015FCE0 taken needs
# 0x700031E0 < 0, and 0019BC40 counts its table up from 0 (the native table
# worker faults on a negative count instead).
DEAD = {(0x15FCE0, True)}


def main():
    global ELF, NATIVE, BOXES
    started = time.time()
    ELF = read_elf()
    callees = check_callee_set(ELF)
    BOXES = box_intervals(ELF)
    NATIVE = build_native()
    total = reference_mode.pick(40000, 40000)
    seeds = reference_mode.select(range(total), 5000, 0x6A11)
    boxes = len(box_plan())
    seeds += [BOX_BASE + k for k in range(boxes)]
    results = reference_mode.parallel_map(run_case, seeds)
    outcomes, entries, faults, adapters, calls = set(), {}, 0, 0, 0
    for entry, count, fault, adapter, cover in results:
        outcomes.update(cover)
        entries[entry] = entries.get(entry, 0) + 1
        faults += fault
        adapters += adapter
        calls += count
    sites = branch_sites(ELF)
    missing = sorted('%06X %s' % (pc, 'taken' if taken else 'not taken')
                     for pc in sites for taken in (True, False)
                     if (pc, taken) not in outcomes and (pc, taken) not in DEAD)
    assert not missing, ('branch outcomes never exercised', missing)
    reached_dead = sorted(hex(pc) for pc, taken in DEAD if (pc, taken) in outcomes)
    assert not reached_dead, ('an outcome listed as unreachable was reached', reached_dead)
    absent = [e for e in ENTRIES if e not in entries]
    assert not absent, ('entry points never run', absent)
    refusals = refusal_checks(NATIVE)
    reference_mode.banner(reference_mode.part(len(seeds) - boxes, total, 'random cases'),
                          '%d box boundary cases' % boxes,
                          '%d jal targets (all hooked, translated or leaves)' % callees)
    print('player running jump vs original instructions: PASS %d cases (%s), %d worker calls '
          'identical, %d conditional branches both ways (%d outcome unreachable), %d fault-stop cuts, '
          '%d adapter runs, %d refusals (%.1fs)' % (
              len(seeds), ', '.join('%s %d' % kv for kv in sorted(entries.items())), calls,
              len(sites), len(DEAD), faults, adapters, refusals, time.time() - started))


# ======================================================================
# World mode: the route beats with the running jump, over captured RAM
# ======================================================================

PLAYER = shared.PLAYER
VEC = 0x7F0E0000                 # private vectors for worker arguments (stack region)
HOOKED = {PROBE: 'probe', AIM: 'aim', SCAN: 'scan', JUMP: 'jump', STATE24: 'state24'}
WORLD_BEATS = ('12_crevice_jump', '14_roger_encounter')


class JumpRoute(shared.RouteReplay):
    """shared.RouteReplay (the original player stage per frame, pad unpack
    and camera from the capture) on the model interpreter."""

    def __init__(self, elf, trace, ram, spad, cover=False):
        self.ee = ee = JumpEE(elf, ram, spad, cover=cover)
        self.frame, self.events = 0, []
        for address, name in shared.SOUND_HOOKS.items():
            ee.hooks[address] = self.recorder(name)
        ee.hooks[shared.PAD_READ] = self.pad_read
        self.raw = bytes(8)
        self.rows = {r['counter']: r for r in trace['rows']}
        self.first = trace['first_counter']
        self.inputs = sorted(trace['inputs'], key=lambda i: i['f'])
        self.counter = ee.load(0x70003B64)


class WorldCall:
    """One native routine in progress over the EE record at `base`."""

    def __init__(self, ee, base):
        self.base = base
        self.live = LiveActor()
        C.memmove(self.live.bytes, ee.read(base, 0x320), 0x320)
        self.live.link_prev = ee.load(base + 0x308) or None
        self.scratch = Scratch()
        C.memmove(C.addressof(self.scratch),
                  struct.pack('<%dI' % len(SCRATCH), *(ee.load(a) for a in SCRATCH)), 4 * len(SCRATCH))


class WorldJump:
    """EmPlayerRunningJumpWorkers bound to the ORIGINAL callees in the same
    EE: the record and the scratch words are written back before each call
    and read after it; vectors are passed where the original passes them
    (the scratch vector holding the same words, else a private stack slot)."""

    def __init__(self, ee):
        self.ee, self.calls, self.counts, self.error = ee, [], {}, None
        self.keep = {}
        fields = {}
        for field, kind in WORKER_FIELDS:
            fields[field] = FN[kind](self.guard(getattr(self, 'w_' + field)))
            self.keep[field] = fields[field]
        self.fields = fields

    def guard(self, fn):
        def run(*args):
            if self.error is not None:
                return 0 if fn.__name__ in ('w_sine', 'w_cosine', 'w_atan2', 'w_sqrt') else -1
            try:
                return fn(*args[1:])
            except BaseException as error:        # surfaced after the native call returns
                self.error = error
                return 0 if fn.__name__ in ('w_sine', 'w_cosine', 'w_atan2', 'w_sqrt') else -1
        return run

    def cur(self):
        return self.calls[-1]

    def sync_out(self):
        c, e = self.cur(), self.ee
        e.write(c.base, bytes(c.live.bytes))
        for address, word in zip(SCRATCH, struct.unpack('<%dI' % len(SCRATCH),
                                                        C.string_at(C.addressof(c.scratch), 4 * len(SCRATCH)))):
            e.save(address, word)

    def sync_in(self):
        c, e = self.cur(), self.ee
        C.memmove(c.live.bytes, e.read(c.base, 0x320), 0x320)
        C.memmove(C.addressof(c.scratch),
                  struct.pack('<%dI' % len(SCRATCH), *(e.load(a) for a in SCRATCH)), 4 * len(SCRATCH))

    def around(self, entry, args=(), floats=()):
        """floats are raw bit patterns."""
        self.sync_out()
        v0, f0 = recovery_nested(self.ee, entry, args, floats)
        self.sync_in()
        return s32(v0), f0 & MASK

    def place(self, pointer, n, slot):
        """The scratch vector (0x700038A0 + 16k) whose words the native's
        scratch holds (written to the EE by the call's sync), else a private
        stack slot."""
        words = words_of(pointer, n)
        native = self.cur().scratch.s38A0
        for k in range(4):
            if tuple(native[4 * k + i] for i in range(n)) == words:
                return 0x700038A0 + 16 * k
        for i, w in enumerate(words):
            self.ee.save(VEC + 16 * slot + 4 * i, w)
        return VEC + 16 * slot

    def hit_from_spad(self, hit):
        e, h = self.ee, hit.contents
        set_words(C.addressof(h) + ProbeHit.point.offset, [e.load(0x700031B0 + 4 * i) for i in range(3)])
        record = e.load(0x700031D0)
        if record:
            h.node = e.load(record + 0x1A, 2)
            set_words(C.addressof(h) + ProbeHit.normal.offset, [e.load(record + 0x24 + 4 * i) for i in range(3)])

    def base(self):
        return self.cur().base

    # ---- the workers ------------------------------------------------------
    def w_sine(self, x): return b2f(self.around(0x11E2A8, (), (F(x),))[1])
    def w_cosine(self, x): return b2f(self.around(0x11DE90, (), (F(x),))[1])
    def w_atan2(self, y, x): return b2f(self.around(0x11E620, (), (F(y), F(x)))[1])
    def w_sqrt(self, x): return b2f(self.around(0x11E748, (), (F(x),))[1])

    def w_move(self, a, target, mask, hit):
        r = self.around(0x19AD00, (self.base(), self.place(target, 4, 0), mask))[0]
        self.hit_from_spad(hit)
        return r

    def w_sweep(self, a, start, end, mask, hit):
        s, t = self.place(start, 3, 0), self.place(end, 3, 1)
        r = self.around(0x19AFE0, (self.base(), s, t, mask))[0]
        self.hit_from_spad(hit)
        return r

    def w_table(self, at, out):
        self.around(0x19BC40, (self.place(at, 4, 0),))
        e = self.ee
        count = s32(e.load(0x700031E0))
        if count < 0 or count > 16:
            return -1
        out.contents.count = count
        base = C.addressof(out.contents)
        for i in range(count):
            out.contents.flags[i] = e.load(0x70003170 + 2 * i, 2)
            set_words(base + ClimbTable.height.offset + 4 * i, [e.load(0x700030F0 + 4 * i)])
            set_words(base + ClimbTable.aux.offset + 4 * i, [e.load(0x282250 + 4 * i)])
        return 0

    def w_ledge(self, hit, out):
        point = words_of(C.addressof(hit.contents) + ProbeHit.point.offset, 3)
        assert point == tuple(self.ee.load(0x700031B0 + 4 * i) for i in range(3)), 'ledge hit is not the last probe'
        self.around(0x177510)
        e = self.ee
        set_words(C.addressof(out.contents),
                  [e.load(0x70003050 + 4 * i) for i in range(3)] + [e.load(0x70003060 + 4 * i) for i in range(3)] +
                  [e.load(0x700031E4)] + [e.load(0x70003070 + 4 * i) for i in range(16)])
        return 0

    def w_column(self, a, at, arg, height, out):
        live = bytes(self.cur().live.bytes)             # the native record, not yet written back
        assert words_of(at, 3) == struct.unpack_from('<3I', live, 0xB0), 'column point is not +B0'
        out[0] = self.around(0x1760C0, (self.base(), self.base() + 0xB0, arg), (F(height),))[0]
        return 0

    def w_link_type(self, owner, out):
        out[0] = self.ee.load(owner + 3, 1)
        return 0

    def w_target(self, index, out):
        e = self.ee
        p = e.load(e.load(0x275B8C) + 4 * index)
        out.contents.object = p
        out.contents.flags, out.contents.type = e.load(p + 2, 1), e.load(p + 3, 1)
        out.contents.field34 = s32(e.load(p + 0x34, 2) << 16) >> 16
        return 0

    def w_target_xz(self, obj, x, z):
        set_words(C.addressof(x.contents), [self.ee.load(obj + 0xB0)])
        set_words(C.addressof(z.contents), [self.ee.load(obj + 0xB8)])
        return 0

    def w_target_radius(self, obj, out):
        set_words(C.addressof(out.contents), [self.around(0x1AA410, (obj,))[1]])
        return 0

    def w_target_sight(self, a, obj, radius, out):
        out[0] = self.around(0x1AA2A0, (self.base(), obj), (F(radius),))[0]
        return 0

    def w_heading(self, a, arg, out):
        out[0] = self.around(0x174AC0, (self.base(), arg))[0]
        return 0

    def w_request(self, a, clip, force, blend):
        self.around(0x1749A0, (self.base(), clip, force), (F(blend),)); return 0

    def w_arbiter(self, a, clip, blend, frame):
        self.around(0x1749F0, (self.base(), clip), (F(blend), F(frame))); return 0

    def w_clip_frames(self, bank, clip, out):
        out[0] = self.around(0x1C61D0, (bank, clip))[0]
        return 0

    def w_sound(self, a, id): self.around(0x1FBD50, (self.base(), id, 0), (F(300.0),)); return 0
    def w_translate(self, a, arg): self.around(0x178B90, (self.base(), arg)); return 0
    def w_strafe(self, a): self.around(0x178EC0, (self.base(),)); return 0
    def w_quadrant(self, a): self.around(0x1751A0, (self.base(),)); return 0

    def w_react(self, a, out):
        out[0] = self.around(0x2243F0, (self.base(),))[0]
        return 0

    def w_grab(self, a, reach, out):
        out[0] = self.around(0x17C860, (self.base(),), (reach,))[0]
        return 0

    def w_dust(self, a): self.around(0x17DEB0, (self.base(),)); return 0
    def w_land(self, a): self.around(0x17C580, (self.base(),)); return 0
    def w_surface5d(self, a, arg): self.around(0x21D250, (self.base(), arg)); return 0
    def w_teleport(self, a, frames, hold): self.around(0x21D2E0, (self.base(), frames, hold)); return 0

    def w_probes(self, a, s1):
        assert s1 == S1_CALLER, s1          # $s1 is left as the caller's: nothing to set
        self.around(0x1764E0, (self.base(),)); return 0

    def w_floor(self, a, search, out):
        out[0] = self.around(0x175900, (self.base(), search))[0]
        return 0

    def w_fall_check(self, a): self.around(0x1796C0, (self.base(),)); return 0

    def w_root_clock(self, out):
        e = self.ee
        out[0] = e.load(e.load(e.load(0x275B40)) + 8)
        return 0

    # ---- the hooks at the original addresses --------------------------------
    def hook(self, kind):
        def run(ee):
            call = WorldCall(ee, ee.arg(0))
            self.calls.append(call)
            workers = Workers(None, C.pointer(call.scratch), **self.fields)
            scene = Scene(ee.load(0x810700, 1), ee.load(0x810701, 1), ee.load(0x70003B8D, 1),
                          s32(ee.load(0x275B94, 2) << 16) >> 16)
            L, W, S = C.byref(call.live), C.byref(workers), C.byref(scene)
            out, ptr = C.c_int(-99), C.c_void_p(0)
            if kind == 'probe': status = NATIVE.em_player_running_jump_probe(L, S, W, C.byref(out))
            elif kind == 'aim': status = NATIVE.em_player_running_jump_aim(L, S, W, C.byref(out))
            elif kind == 'scan': status = NATIVE.em_player_running_jump_target(L, S, W, C.byref(ptr))
            elif kind == 'jump': status = NATIVE.em_player_running_jump_tick(L, W)
            else: status = NATIVE.em_player_running_jump_state24_tick(L, W)
            if self.error is not None:
                raise self.error
            assert status == 0, (kind, 'native fault on the route')
            ee.write(call.base, bytes(call.live.bytes))
            for address, word in zip(SCRATCH, struct.unpack('<%dI' % len(SCRATCH),
                                                            C.string_at(C.addressof(call.scratch), 4 * len(SCRATCH)))):
                ee.save(address, word)
            self.calls.pop()
            if kind in ('probe', 'aim'):
                ee.ret_int(out.value)
                key = '%s=%d' % (kind, out.value)
            elif kind == 'scan':
                ee.ret_int(ptr.value or 0)
                key = 'scan=%s' % ('target' if ptr.value else 'none')
            else:
                key = '%s/%X' % (kind, ee.load(call.base + 6, 1))
            self.counts[key] = self.counts.get(key, 0) + 1
        return run


def recovery_nested(ee, entry, args, floats):
    """EE.nested with the float argument registers set from raw bits."""
    saved = (list(ee.r), list(ee.rh), ee.hi, ee.lo, list(ee.f), ee.acc,
             ee.cond, [list(v) for v in ee.vf], list(ee.vacc), ee.q)
    ee.r[29] = (ee.r[29] - 0x400) & ~15
    for i, value in enumerate(args):
        ee.r[4 + i] = shared.sx32(value)
    for i, value in enumerate(floats):
        ee.f[12 + i] = value & MASK
    ee.r[31] = shared.RETURN
    ee.run(entry)
    result = (ee.r[2], ee.f[0])
    (ee.r, ee.rh, ee.hi, ee.lo, ee.f, ee.acc, ee.cond, ee.vf, ee.vacc, ee.q) = saved
    return result


WORLD = {}


def beat_replay(job):
    beat, native_mode = job
    trace, ram, spad = WORLD[beat]
    replay = JumpRoute(WORLD['elf'], trace, ram, spad, cover=not native_mode)
    world = None
    if native_mode:
        world = WorldJump(replay.ee)
        for address, kind in HOOKED.items():
            replay.ee.hooks[address] = world.hook(kind)
    frames, rows, states = [], 0, set()
    end = recovery.replay_end(trace)
    while replay.counter < end:
        row = replay.step()
        ee = replay.ee
        digest = hashlib.sha1(ee.mem)
        digest.update(ee.spad)
        frames.append((replay.counter, digest.hexdigest(), ee.read(PLAYER, 0x320), len(replay.events)))
        states.add((ee.load(PLAYER + 5, 1), ee.load(PLAYER + 6, 1)))
        if row is not None:
            shared.route_row_check(ee, row, (beat, 'native' if native_mode else 'original', replay.counter))
            rows += 1
    outcomes = sorted(replay.ee.outcomes) if replay.ee.outcomes is not None else None
    return frames, replay.events, rows, (world.counts if world else None), sorted(states), end, outcomes


def world_main():
    global NATIVE
    elf = read_elf()
    beats = [b for b in os.environ.get('EM_WORLD_BEATS', ','.join(WORLD_BEATS)).split(',') if b]
    for beat in beats:
        loaded = shared.route_beat(beat)
        if isinstance(loaded, str):
            raise SystemExit('world mode: %s (docs/PLAYER_RUNNING_JUMP.md)' % loaded)
        WORLD[beat] = loaded
    WORLD['elf'] = elf
    NATIVE = build_native('running_jump_world')
    jobs = [(beat, mode) for beat in beats for mode in (False, True)]
    results = reference_mode.parallel_map(beat_replay, jobs)
    by_job = dict(zip(jobs, results))
    for beat in beats:
        (a_frames, a_events, rows, _, a_states, end, reached), (b_frames, b_events, b_rows, counts, _, _, _) = \
            by_job[(beat, False)], by_job[(beat, True)]
        assert len(a_frames) == len(b_frames), (beat, len(a_frames), len(b_frames))
        for (counter, a_hash, a_actor, a_count), (_, b_hash, b_actor, b_count) in zip(a_frames, b_frames):
            if a_actor != b_actor:
                diff = [hex(k) for k in range(0x320) if a_actor[k] != b_actor[k]]
                raise AssertionError((beat, counter, 'actor bytes differ at', diff[:24]))
            assert a_count == b_count, (beat, counter, 'sound/effect call counts differ')
            assert a_hash == b_hash, (beat, counter, 'RAM or scratchpad differs outside the actor')
        assert a_events == b_events, (beat, 'sound/effect calls differ')
        assert rows == b_rows and rows > 0, (beat, rows, b_rows)
        assert any(k.startswith('jump/') for k in counts), (beat, 'no state-6 callback ran', counts)
        jumps = sorted('%X/%X' % s for s in a_states if s[0] == 6)
        # The original replay's branch outcomes inside the translated routines:
        # what the route itself exercises (docs/PLAYER_RUNNING_JUMP.md section 4).
        per = {}
        for pc, taken in reached:
            name = next(n for a, n in list(HOOKED.items()) + [(DROP, 'drop')] if a <= pc < a + SIZES[a])
            per[name] = per.get(name, 0) + 1
        print('%s route coverage (original replay): branch outcomes per routine %s; 0015EC50 outcomes %s' % (
            beat, dict(sorted(per.items())),
            ' '.join('%06X%s' % (pc, '+' if t else '-') for pc, t in reached if PROBE <= pc < PROBE + SIZES[PROBE])))
        print('%s: PASS %d frames identical (whole RAM + scratchpad + actor) up to counter %d, %d trace rows '
              'within precision, %d sound/effect calls; native calls %s; state 6 sub-states seen %s' % (
                  beat, len(a_frames), end, rows, len(a_events),
                  ', '.join('%s %d' % kv for kv in sorted(counts.items())), ' '.join(jumps)))


if __name__ == '__main__':
    if os.environ.get('EM_TEST_WORLD', '') not in ('', '0'):
        world_main()
    else:
        main()
