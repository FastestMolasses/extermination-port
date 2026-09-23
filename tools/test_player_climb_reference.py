#!/usr/bin/env python3
"""Execute the original ledge probe and climb state and compare em_player_climb.c.

docs/PLAYER_CLIMB_SLIDE.md. The user's pinned ELF supplies every instruction
and table; none are embedded here. The EE interpreter from
tools/test_player_slide_reference.py runs, unmodified:

  0015DF10  ledge probe        0015DEC0  face gate      00177510  ledge frame
  001775E0  lip sweep          00177F40  depth test     00177460  vault test
  001776E0  high-ledge sides   00177CF0  high-ledge hands
  0019A180  column attribute   build_trs_matrix and the SDK vector routines
  00161790  climb state 2      00161690  AREA11 target  0017D800 / 0017D8D0
  0017DEB0  grab effect        0017F320  hang clearance 00188550 hang clip

Hooked boundaries, scripted per case and recorded (never simulated):
0019AD00 / 0019AFE0 / 0019A570 probes and 0019BC40 (each writes the case's
result into the original scratchpad block), 001760C0 column, the SDK atan2 /
sqrt (host models on both sides), 001749A0, 001FBD50, 001EFD90, 00182870,
anim_eval_skeleton (the case's node 1 values), 00178B90, 00175900, 001764E0,
00174AC0 (the case's gait byte), 0017C440, 0017C540, 001796C0.
"""
import ctypes as C
import random
import struct
import subprocess
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from test_player_slide_reference import (EE, read_elf, bits, number, fp, s32, DECOMP,  # noqa: E402
                                         ProbeHit, LIBC, record_writes, assert_covered)
import reference_mode  # noqa: E402

ACTOR, NODES, NODE, ENTITY, LINK, OBJECTS = 0x680000, 0x6D0000, 0x6A0000, 0x6B0000, 0x6C0000, 0x6E0000
PROBE, STATE2, STATE3, HANG = 0x15DF10, 0x161790, 0x162190, 0x17F320
ARBITER, FRAMES, LAND = 0x1749F0, 0x1C61D0, 0x17C580
MOVE, SWEEP, SEGMENT, COLUMN, TABLE = 0x19AD00, 0x19AFE0, 0x19A570, 0x1760C0, 0x19BC40
ATAN2, SQRT, FABS = 0x11E620, 0x11E748, 0x11DF78
REQUEST, SOUND, EFFECT, LAND_SOUND, SKELETON = 0x1749A0, 0x1FBD50, 0x1EFD90, 0x182870, 0x1C6DA0
TRANSLATE, FLOOR, PROBES, HEADING, REENTRY, HANDOFF, FALL = (0x178B90, 0x175900, 0x1764E0, 0x174AC0,
                                                            0x17C440, 0x17C540, 0x1796C0)
LIBC.sqrtf.argtypes = [C.c_float]; LIBC.sqrtf.restype = C.c_float
LIBC.atanf.argtypes = [C.c_float]; LIBC.atanf.restype = C.c_float


class Actor(C.Structure):
    _fields_ = [('position', C.c_float * 4), ('rotation', C.c_float * 3), ('scale', C.c_float * 3),
                ('matrix', C.c_float * 16), ('speed', C.c_float), ('clock', C.c_float),
                ('rate', C.c_float), ('ledge', C.c_float), ('target_y', C.c_float),
                ('velocity', C.c_float * 3), ('goal', C.c_float * 2), ('drop', C.c_float),
                ('aim', C.c_float), ('ledge_normal', C.c_float * 2), ('surface_y', C.c_float),
                ('push', C.c_float), ('push_decay', C.c_float), ('anim_flags', C.c_uint32), ('counter', C.c_int16), ('major', C.c_uint8),
                ('state', C.c_uint8), ('walk', C.c_uint8), ('hang', C.c_uint8), ('mode', C.c_uint8),
                ('variant', C.c_uint8), ('lock', C.c_uint8), ('running', C.c_uint8),
                ('height_class', C.c_uint8), ('tier', C.c_uint8), ('surface', C.c_uint8),
                ('depth', C.c_uint8), ('puddle', C.c_uint8), ('row', C.c_uint8),
                ('special', C.c_uint8), ('gait', C.c_uint8), ('link_kind', C.c_uint8)]


class Scene(C.Structure):
    _fields_ = [('hip_world', C.c_float * 4), ('flags', C.c_uint8), ('area', C.c_uint8)]


class Table(C.Structure):
    _fields_ = [('count', C.c_int), ('flags', C.c_uint16 * 16), ('height', C.c_float * 16),
                ('aux', C.c_float * 16), ('object_kind', C.c_uint8 * 16), ('object_node', C.c_int16 * 16)]


class Hit(C.Structure):
    _fields_ = [('probe', ProbeHit), ('pickup_box', C.c_uint8)]


F = C.c_float
PF = C.POINTER(C.c_float)
PA = C.POINTER(Actor)
MOVE_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, PF, PF, C.c_uint, C.POINTER(Hit))
SWEEP_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, PF, PF, C.c_uint, C.POINTER(Hit))
SEGMENT_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, PF, PF, C.c_uint, C.c_int)
COLUMN_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, PF, F)
TABLE_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, PF, C.POINTER(Table))
MATH2_FN = C.CFUNCTYPE(F, C.c_void_p, F, F)
MATH1_FN = C.CFUNCTYPE(F, C.c_void_p, F)
REQUEST_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.c_int, F)
ARBITER_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, F, F)
FRAMES_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.POINTER(C.c_int))
SOUND_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint)
EFFECT_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, PF, PF)
TIER_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int)
SKELETON_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, PA, PF, PF)
ARG_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, PA, C.c_int)
FLOOR_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, PA, C.c_int, C.POINTER(C.c_int))
ACTOR_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, PA)


class Workers(C.Structure):
    _fields_ = [('context', C.c_void_p), ('move', MOVE_FN), ('sweep', SWEEP_FN),
                ('segment', SEGMENT_FN), ('column', COLUMN_FN), ('table', TABLE_FN),
                ('atan2', MATH2_FN), ('sqrt', MATH1_FN), ('request', REQUEST_FN),
                ('arbiter', ARBITER_FN), ('clip_frames', FRAMES_FN), ('sound', SOUND_FN), ('effect', EFFECT_FN), ('land_sound', TIER_FN),
                ('skeleton', SKELETON_FN), ('translate', ARG_FN), ('floor', FLOOR_FN),
                ('probes', ACTOR_FN), ('heading', ARG_FN), ('reentry', ARG_FN),
                ('handoff', ACTOR_FN), ('fall', ACTOR_FN), ('land', ACTOR_FN)]


# (name, offset, size, kind)
FIELDS = [('speed', 0x38, 4, 'f'), ('clock', 0x3C, 4, 'f'), ('rate', 0x204, 4, 'f'),
          ('ledge', 0x254, 4, 'f'), ('target_y', 0x258, 4, 'f'), ('drop', 0x2EC, 4, 'f'),
          ('aim', 0x218, 4, 'f'), ('surface_y', 0x250, 4, 'f'), ('push', 0x26C, 4, 'f'),
          ('push_decay', 0x270, 4, 'f'), ('anim_flags', 0x200, 4, 'u'),
          ('counter', 0x28, 2, 's'), ('major', 4, 1, 'u'), ('state', 5, 1, 'u'), ('walk', 6, 1, 'u'),
          ('hang', 0xD, 1, 'u'), ('mode', 0x1F0, 1, 'u'), ('variant', 0x1F1, 1, 'u'),
          ('lock', 0x25F, 1, 'u'), ('running', 0x2F2, 1, 'u'), ('height_class', 0x2F1, 1, 'u'),
          ('tier', 0x25C, 1, 'u'), ('surface', 0x23A, 1, 'u'), ('depth', 0x23C, 1, 'u'),
          ('puddle', 0x23D, 1, 'u'), ('row', 0x235, 1, 'u'), ('special', 0x236, 1, 'u'),
          ('gait', 0x23F, 1, 'u')]
VECTORS = [('position', 0xB0, 4), ('rotation', 0xC0, 3), ('scale', 0x60, 3), ('matrix', 0xD0, 16),
           ('velocity', 0x2E0, 3), ('goal', 0x2F4, 2)]
# Every actor byte the oracle compares (FIELDS, VECTORS, the ledge normal +290/+298).
COMPARED = ({offset + i for _, offset, size, _ in FIELDS for i in range(size)} |
            {offset + i for _, offset, count in VECTORS for i in range(4 * count)} |
            set(range(0x290, 0x294)) | set(range(0x298, 0x29C)))


def raw_of(value, size, kind):
    return bits(value) if kind == 'f' else value & ((1 << (8 * size)) - 1)


def fvec(values, count):
    return tuple(bits(values[i]) for i in range(count))


class Script:
    NAMES = ('moves', 'sweeps', 'segments', 'columns', 'tables', 'skeletons', 'floors', 'gaits')

    def __init__(self):
        for name in self.NAMES: setattr(self, name, [])

    def copy(self):
        other = Script()
        for name in self.NAMES: setattr(other, name, list(getattr(self, name)))
        return other

    @staticmethod
    def pop(queue, default):
        return queue.pop(0) if queue else default


def empty_hit():
    return Hit()


class Oracle:
    def __init__(self, elf, script, scene):
        self.ee = ee = EE(elf)
        self.script, self.log = script, []
        ee.save(0x8106BE, scene.flags, 1)
        ee.save(0x810700, scene.area, 1)
        ee.save(0x275B40, NODES); ee.save(NODES + 4, NODES + 0x200)
        for i in range(4): ee.putf(NODES + 0x200 + 0xC0 + 4 * i, scene.hip_world[i])
        ee.save(ACTOR + 0x40, 0x500000)
        h = ee.hooks
        h[MOVE] = lambda e: self.probe('move', e.vector(e.arg(1), 4), e.arg(2))
        h[SWEEP] = lambda e: self.probe('sweep', e.vector(e.arg(1), 4), e.vector(e.arg(2), 4), e.arg(3))
        h[SEGMENT] = self.segment
        h[COLUMN] = self.column
        h[TABLE] = self.table
        h[ATAN2] = lambda e: e.ret_float(LIBC.atan2f(e.farg(0), e.farg(1)))
        h[SQRT] = lambda e: e.ret_float(LIBC.sqrtf(e.farg(0)))
        h[FABS] = lambda e: e.ret_float(abs(e.farg(0)))
        h[REQUEST] = lambda e: self.rec('request', e.arg(1) & 0xFFFF, e.arg(2), e.f[12])
        h[ARBITER] = lambda e: self.rec('arbiter', e.arg(1) & 0xFFFF, e.f[12], e.f[13])
        h[FRAMES] = self.frames
        h[LAND] = lambda e: self.rec('land', e.vector(ACTOR + 0xB0))
        h[SOUND] = lambda e: self.rec('sound', e.arg(1))
        h[EFFECT] = lambda e: self.rec('effect', e.arg(0), e.vector(e.arg(1)), e.vector(e.arg(2)))
        h[LAND_SOUND] = lambda e: self.rec('land_sound', e.arg(1))
        h[SKELETON] = self.skeleton
        h[TRANSLATE] = lambda e: self.rec('translate', e.arg(1), e.load(ACTOR + 0x38), e.vector(ACTOR + 0xB0))
        h[FLOOR] = self.floor
        h[PROBES] = lambda e: self.rec('probes', e.vector(ACTOR + 0xB0))
        h[HEADING] = self.heading
        h[REENTRY] = lambda e: self.rec('reentry', e.arg(1))
        h[HANDOFF] = lambda e: self.rec('handoff', e.load(ACTOR + 0x25C, 1))
        h[FALL] = lambda e: self.rec('fall', e.vector(ACTOR + 0xB0))

    def rec(self, *entry):
        self.log.append(entry)
        self.ee.ret_int(0)

    def frames(self, e):
        clip = e.arg(1) & 0xFFFF
        self.log.append(('frames', clip))
        e.ret_int(clip_length(BANK, clip))

    def probe(self, name, *entry):
        e = self.ee
        hit = Script.pop(self.script.moves if name == 'move' else self.script.sweeps, empty_hit())
        self.log.append((name,) + entry)
        p = hit.probe
        e.save(0x700031D8, p.kind)
        if p.kind:
            e.save(0x700031D0, NODE)
            e.save(NODE + 0x1A, p.node, 2)
            for i in range(3):
                e.putf(NODE + 0x24 + 4 * i, p.normal[i])
                e.putf(0x700031B0 + 4 * i, p.point[i])
                e.putf(0x700031C0 + 4 * i, p.delta[i])
            if p.entity:
                e.save(0x700031D4, ENTITY)
                e.save(ENTITY + 2, p.entity_flags, 1)
                e.save(ENTITY + 0x10, 0x219550 if hit.pickup_box else 0x1C4820)
            else:
                e.save(0x700031D4, 0)
        else:
            e.save(0x700031D0, 0)
            e.save(0x700031D4, 0)
        e.ret_int(p.kind)

    def segment(self, e):
        self.log.append(('segment', e.vector(e.arg(0), 4), e.vector(e.arg(1), 4), e.arg(2), s32(e.arg(3))))
        e.ret_int(Script.pop(self.script.segments, 0))

    def column(self, e):
        self.log.append(('column', e.vector(e.arg(1), 3), e.arg(2), e.f[12]))
        e.ret_int(Script.pop(self.script.columns, 0))

    def table(self, e):
        t = Script.pop(self.script.tables, Table())
        self.log.append(('table', e.vector(e.arg(0), 4)))
        e.save(0x700031E0, t.count)
        for i in range(t.count):   # 0019BC40's compaction order
            e.save(0x70003170 + 2 * i, t.flags[i], 2)
            e.putf(0x700030F0 + 4 * i, t.height[i])
            e.save(0x70003130 + 4 * i, OBJECTS + 0x100 * i)
            e.putf(0x282250 + 4 * i, t.aux[i])
            e.save(OBJECTS + 0x100 * i + 0x54, t.object_kind[i], 1)
            e.save(OBJECTS + 0x100 * i + 0x1A, t.object_node[i], 2)

    def skeleton(self, e):
        y, eight = Script.pop(self.script.skeletons, (0.0, 0.0))
        self.log.append(('skeleton',))
        e.putf(NODES + 0x200 + 0xC4, y)
        e.putf(NODES + 0x200 + 8, eight)

    def floor(self, e):
        result, dy = Script.pop(self.script.floors, (0, 0.0))
        self.log.append(('floor', e.arg(1), e.vector(ACTOR + 0xB0)))
        e.putf(ACTOR + 0xB4, fp(e.getf(ACTOR + 0xB4) + dy))
        e.ret_int(result)

    def heading(self, e):
        self.log.append(('heading', e.arg(1)))
        e.save(ACTOR + 0x23F, Script.pop(self.script.gaits, 0), 1)
        e.ret_int(0)

    def load(self, a, link_kind):
        e = self.ee
        for name, offset, size, kind in FIELDS:
            e.save(ACTOR + offset, raw_of(getattr(a, name), size, kind), size)
        for name, offset, count in VECTORS:
            for i in range(count): e.putf(ACTOR + offset + 4 * i, getattr(a, name)[i])
        e.putf(ACTOR + 0x290, a.ledge_normal[0]); e.putf(ACTOR + 0x298, a.ledge_normal[1])
        e.putf(ACTOR + 0x26C, a.push); e.putf(ACTOR + 0x270, a.push_decay)
        e.putf(ACTOR + 0x2E4, a.velocity[1])
        if link_kind:
            e.save(ACTOR + 0x308, LINK)
            e.save(LINK + 0x10, (0x828700, 0x827880)[link_kind & 1] if link_kind >= 2 else 0x1C4820)
        else:
            e.save(ACTOR + 0x308, 0)

    def fields(self):
        e = self.ee
        out = {name: e.load(ACTOR + offset, size) for name, offset, size, _ in FIELDS}
        for name, offset, count in VECTORS:
            for i in range(count): out['%s%d' % (name, i)] = e.load(ACTOR + offset + 4 * i)
        out['ledge_normal0'] = e.load(ACTOR + 0x290); out['ledge_normal1'] = e.load(ACTOR + 0x298)
        return out


class Native:
    def __init__(self, script):
        self.script, self.log = script, []
        self.workers = Workers(
            None, MOVE_FN(lambda _, pos, t, m, out: self.probe('move', out, fvec(t, 4), m)),
            SWEEP_FN(lambda _, a, b, m, out: self.probe('sweep', out, fvec(a, 4), fvec(b, 4), m)),
            SEGMENT_FN(self.segment), COLUMN_FN(self.column), TABLE_FN(self.table),
            MATH2_FN(lambda _, y, x: LIBC.atan2f(y, x)), MATH1_FN(lambda _, x: LIBC.sqrtf(x)),
            REQUEST_FN(lambda _, c, f, b: self.rec('request', c & 0xFFFF, f, bits(b))),
            ARBITER_FN(lambda _, c, b, f: self.rec('arbiter', c & 0xFFFF, bits(b), bits(f))),
            FRAMES_FN(self.frames),
            SOUND_FN(lambda _, i: self.rec('sound', i)),
            EFFECT_FN(lambda _, i, p, r: self.rec('effect', i, fvec(p, 3), fvec(r, 3))),
            TIER_FN(lambda _, t: self.rec('land_sound', t)), SKELETON_FN(self.skeleton),
            ARG_FN(lambda _, a, arg: self.rec('translate', arg, bits(a.contents.speed),
                                                fvec(a.contents.position, 3))),
            FLOOR_FN(self.floor), ACTOR_FN(lambda _, a: self.rec('probes', fvec(a.contents.position, 3))),
            ARG_FN(self.heading), ARG_FN(lambda _, a, arg: self.rec('reentry', arg)),
            ACTOR_FN(lambda _, a: self.rec('handoff', a.contents.tier)),
            ACTOR_FN(lambda _, a: self.rec('fall', fvec(a.contents.position, 3))),
            ACTOR_FN(lambda _, a: self.rec('land', fvec(a.contents.position, 3))))

    def frames(self, _, clip, out):
        self.log.append(('frames', clip)); out[0] = clip_length(BANK, clip); return 0

    def rec(self, *entry):
        self.log.append(entry); return 0

    def probe(self, name, out, *entry):
        hit = Script.pop(self.script.moves if name == 'move' else self.script.sweeps, empty_hit())
        self.log.append((name,) + entry)
        C.memmove(out, C.byref(hit), C.sizeof(Hit))
        return hit.probe.kind

    def segment(self, _, a, b, mask, ident):
        self.log.append(('segment', fvec(a, 4), fvec(b, 4), mask, ident))
        return Script.pop(self.script.segments, 0)

    def column(self, _, at, height):
        self.log.append(('column', fvec(at, 3), 1, bits(height)))
        return Script.pop(self.script.columns, 0)

    def table(self, _, at, out):
        t = Script.pop(self.script.tables, Table())
        self.log.append(('table', fvec(at, 4)))
        C.memmove(out, C.byref(t), C.sizeof(Table))
        return 0

    def skeleton(self, _, actor, y, eight):
        v = Script.pop(self.script.skeletons, (0.0, 0.0))
        self.log.append(('skeleton',))
        y[0], eight[0] = v
        return 0

    def floor(self, _, actor, search, out):
        result, dy = Script.pop(self.script.floors, (0, 0.0))
        a = actor.contents
        self.log.append(('floor', search, fvec(a.position, 3)))
        a.position[1] = fp(a.position[1] + dy)
        out[0] = result
        return 0

    def heading(self, _, actor, arg):
        self.log.append(('heading', arg))
        actor.contents.gait = Script.pop(self.script.gaits, 0)
        return 0


def native_fields(a):
    out = {name: raw_of(getattr(a, name), size, kind) for name, _, size, kind in FIELDS}
    for name, _, count in VECTORS:
        for i in range(count): out['%s%d' % (name, i)] = bits(getattr(a, name)[i])
    out['ledge_normal0'] = bits(a.ledge_normal[0]); out['ledge_normal1'] = bits(a.ledge_normal[1])
    return out


def normal_log(log):
    out = []
    for entry in log:
        if entry[0] == 'request':
            out.append(('request', entry[1], entry[2], entry[3] & 0xFFFFFFFF))
        elif entry[0] == 'arbiter':
            out.append(('arbiter', entry[1], entry[2] & 0xFFFFFFFF, entry[3] & 0xFFFFFFFF))
        elif entry[0] == 'column':
            out.append(('column', entry[1], entry[2], entry[3] & 0xFFFFFFFF))
        else:
            out.append(entry)
    return out


# ------------------------------------------------------------------ cases

def random_actor(rng, native_trs):
    a = Actor()
    a.position[0], a.position[1], a.position[2] = rng.uniform(150, 350), rng.uniform(180, 240), rng.uniform(150, 400)
    a.position[3] = 1.0
    a.rotation[0] = rng.choice([0.0, 0.0, rng.uniform(-0.3, 0.3)])
    a.rotation[1] = rng.uniform(-3.14159, 3.14159)
    a.rotation[2] = rng.choice([0.0, 0.0, rng.uniform(-0.3, 0.3)])
    for i in range(3): a.scale[i] = rng.choice([1.0, 1.0, rng.uniform(0.8, 1.2)])
    native_trs(C.byref(a))
    a.speed = rng.uniform(0, 1)
    a.clock = rng.choice([rng.uniform(0, 40), 14.0, 13.0, 11.0, 9.0, 20.0, 26.0, 32.0, number(bits(14.0) + 1)])
    a.rate = 1.0
    a.ledge = rng.choice([rng.uniform(4, 34), 4.01, 7.0, 12.0, 17.0, 24.0, number(bits(24.0) + 1), 14.0])
    a.target_y = rng.uniform(-2, 30)
    for i in range(3): a.velocity[i] = rng.uniform(-400, 400) if i != 1 else rng.uniform(-2, 2)
    a.goal[0], a.goal[1] = rng.uniform(150, 350), rng.uniform(150, 400)
    a.drop = rng.uniform(-1, 0)
    a.aim = rng.uniform(-3, 3)
    a.ledge_normal[0], a.ledge_normal[1] = rng.uniform(-1, 1), rng.uniform(-1, 1)
    a.surface_y = rng.uniform(150, 250)
    a.anim_flags = rng.choice([0, 0x1000, 0x8000, 0x9000])
    a.counter = rng.choice([0, 1, 5, 9, 10, rng.randrange(-3, 40)])
    a.major = 1; a.state = rng.choice([1, 2, 0])
    a.walk = rng.choice([0, 10, 11, 12, 13, 14, 20, 21, 22, 23, 24, 30, 31, 32, 33, 1])
    a.hang = rng.randrange(3); a.mode = rng.choice([0, 1, 8])
    a.variant = rng.randrange(2); a.lock = rng.randrange(3); a.running = rng.randrange(2)
    a.height_class = rng.randrange(4); a.tier = rng.randrange(4)
    a.surface = rng.choice([0, 5, 6, 7]); a.depth = rng.choice([0, 0, 1]); a.puddle = rng.choice([0, 0, 1])
    a.row = rng.randrange(4); a.special = rng.randrange(2); a.gait = rng.randrange(4)
    a.link_kind = 0
    return a


def random_hit(rng, near, wall=True):
    hit = Hit()
    p = hit.probe
    p.kind = rng.choice([2, 4, 4, 1]) if wall else rng.choice([0, 0, 0, 2, 4, 1])
    p.node = rng.choice([0x2005, 0x2005, 0x2000, 0x2046, 0x2032, 0x4005, 0x1005])
    angle = rng.uniform(-3.14159, 3.14159)
    p.normal[0], p.normal[1], p.normal[2] = number(bits(LIBC.sinf(angle))), 0.0, number(bits(LIBC.cosf(angle)))
    for i in range(3): p.point[i] = near[i] + rng.uniform(-6, 6)
    p.entity = rng.randrange(2)
    p.entity_flags = rng.choice([4, 0x24, 0xE4, 2, 0x84])
    hit.pickup_box = rng.randrange(2)
    return hit


def probe_script(rng, a):
    s = Script()
    near = (a.position[0], a.position[1], a.position[2])
    for _ in range(3):
        s.moves.append(random_hit(rng, near, wall=rng.random() < 0.85) if rng.random() < 0.9 else Hit())
    t = Table()
    t.count = rng.choice([0, 1, 2, 3, 4, 6, 16])
    for i in range(16):
        t.flags[i] = rng.choice([1, 1, 0, 0x8001, 0x8000, 0x4001])
        t.height[i] = a.position[1] + rng.choice([rng.uniform(-5, 36), 4.01, number(bits(4.01) + 1), 14.0,
                                                   24.0, number(bits(24.0) + 1), 32.0, 20.0, 8.0])
        t.aux[i] = rng.choice([0.0, 0.3, 0.62831855, number(bits(0.62831855) - 1), -0.8, 1.0, 2.0, -2.0])
        t.object_kind[i] = rng.choice([0x46, 5, 0, 0x32])
        t.object_node[i] = rng.choice([0x2005, 0x2046, 0x4046, 0x1005])
    s.tables.append(t)
    for _ in range(40):
        s.sweeps.append(random_hit(rng, near, wall=False) if rng.random() < 0.25 else Hit())
    s.segments = [rng.choice([0, 2, 4, 4]) for _ in range(20)]
    s.columns = [rng.choice([0, 0, 0, 4]) for _ in range(20)]
    return s


def exact_above(base, delta):
    """A float y with fp(y - base) == delta when one exists near base + delta."""
    delta = number(bits(delta))
    y0 = fp(base + delta)
    for step in (0, 1, -1, 2, -2):
        y = number(bits(y0) + step)
        if fp(y - base) == delta:
            return y
    return y0


def friendly_probe_script(rng, a, ang):
    """Probe results consistent enough to pass most gates, so the case
    reaches the table walk and the climb commits (and their boundaries)."""
    s = Script()
    ideal = rng.random() < 0.6
    for n in range(3):
        if (n == 2 and rng.random() < 0.6) or (ideal and n >= 1):
            s.moves.append(Hit()); continue
        hit = Hit(); p = hit.probe
        p.kind = rng.choice([2, 4, 4])
        p.node = 0x2005 if ideal else rng.choice([0x2005, 0x2005, 0x2005, 0x2046, 0x4005])
        heading = ang + (rng.uniform(-0.4, 0.4) if ideal else
                         rng.choice([0.0, rng.uniform(-0.6, 0.6), 0.5235988, -0.5235988]))
        p.normal[0] = number(bits(-LIBC.sinf(heading))); p.normal[2] = number(bits(-LIBC.cosf(heading)))
        reach = rng.uniform(0.5, 16.0)
        p.point[0] = a.position[0] + LIBC.sinf(heading) * reach
        p.point[1] = a.position[1] + rng.uniform(4, 18)
        p.point[2] = a.position[2] + LIBC.cosf(heading) * reach
        s.moves.append(hit)
    t = Table()
    t.count = 1 if ideal else rng.choice([1, 1, 2, 3])
    for i in range(16):
        t.flags[i] = 1 if ideal else rng.choice([1, 1, 1, 0x8001, 0])
        t.height[i] = a.position[1] + rng.choice([rng.uniform(4.0, 33.0), 14.0, 13.94, 4.01,
                                                   number(bits(4.01) + 1), 24.0, number(bits(24.0) + 1),
                                                   32.0, number(bits(32.0) + 1)])
        t.aux[i] = 0.0 if ideal else rng.choice([0.0, 0.0, 0.3, number(bits(0.62831855) - 1),
                                                  0.62831855, -0.8, 1.0])
        t.object_kind[i] = 5 if ideal else rng.choice([5, 5, 0x46])
        t.object_node[i] = rng.choice([0x4005, 0x4005, 0x2046])
    if rng.random() < 0.3:          # boundary heights: 4.01 above the feet, 14 below a found entry
        t.height[0] = exact_above(a.position[1], rng.choice([4.01, 24.0, 32.0]))
        if rng.random() < 0.5:
            t.count = max(t.count, 2)
            t.flags[1] = 0
            t.height[1] = exact_above(t.height[0], rng.choice([14.0, number(bits(14.0) - 1), 20.0]))
    s.tables.append(t)
    for n in range(40):
        side = n in (5, 6, 7, 8, 12, 13)
        if rng.random() < (0.6 if side else 0.02 if ideal else 0.08):
            hit = Hit(); hit.probe.kind = rng.choice([2, 4, 1])
            hit.probe.node = rng.choice([0x2005, 0x2032, 0x2032, 0x4005])
            hit.probe.entity = rng.randrange(2)
            hit.probe.entity_flags = rng.choice([4, 0x24, 2])
            hit.pickup_box = rng.randrange(2)
            s.sweeps.append(hit)
        else:
            s.sweeps.append(Hit())
    s.segments = [4 if ideal else rng.choice([2, 4, 4, 0]) for _ in range(20)]
    s.columns = [rng.choice([0] * 12 + [4]) if ideal else rng.choice([0, 0, 0, 0, 4]) for _ in range(20)]
    return s


def state_script(rng):
    s = Script()
    for _ in range(12): s.sweeps.append(random_hit(rng, (0, 0, 0), wall=False) if rng.random() < 0.3 else Hit())
    s.columns = [rng.choice([0, 4]) for _ in range(3)]
    s.skeletons = [(rng.uniform(190, 240), rng.uniform(-5, 5))]
    s.floors = [(rng.choice([0, 1, 1, 3]), rng.choice([0.0, rng.uniform(-1, 1)])) for _ in range(3)]
    s.gaits = [rng.randrange(4)]
    return s


def vault_boundary_case(rng, a, ang):
    """A single-entry ledge whose grab point lies exactly 5 (dy <= 24) or 8
    (high ledge) from the feet, so 00177460's `d <= limit` edge is exercised."""
    for i in range(3): a.position[i] = float(int(a.position[i]))
    high = rng.random() < 0.5
    scale = 1.5 if high else 4.5
    n = (-0.5, -0.75)
    offset = rng.choice([(0.0, 8.0), (8.0, 0.0), (0.0, 9.0)] if high else [(3.0, 4.0), (4.0, 3.0), (3.0, 5.0)])
    s = Script()
    hit = Hit(); p = hit.probe
    p.kind, p.node = 4, 0x2005
    p.normal[0], p.normal[2] = n
    p.point[0] = a.position[0] + offset[0] - scale * n[0]
    p.point[1] = a.position[1] + 10.0
    p.point[2] = a.position[2] + offset[1] - scale * n[1]
    s.moves = [hit, Hit(), Hit()]
    t = Table(); t.count = 1; t.flags[0] = 1; t.aux[0] = 0.0; t.object_kind[0] = 5
    t.height[0] = a.position[1] + (28.0 if high else 14.0)
    s.tables.append(t)
    s.sweeps = [Hit() for _ in range(10)]
    if high:   # a side sweep must be blocked for 001776E0 to return 0
        side = Hit(); side.probe.kind, side.probe.node = 4, 0x2005
        s.sweeps[5] = side
    s.segments = [4] * 4
    s.columns = [0] * 8
    heading = LIBC.atan2f(-n[1], n[0])
    return s, heading


ELF = NATIVE = BANK = None


def clip_length(bank, clip):
    header = struct.unpack_from('<I', bank, 4 + clip * 4)[0]
    return struct.unpack_from('<H', bank, header + 2)[0]


def run_case(case):
    kind, seed = case
    rng = random.Random(seed)
    actor = random_actor(rng, NATIVE.em_player_climb_trs)
    scene = Scene((C.c_float * 4)(rng.uniform(150, 350), rng.uniform(180, 240), rng.uniform(150, 400),
                                  rng.choice([1.0, 1.0, 0.5])),
                  rng.choice([0, 0, 0, 1, 0x81]), rng.choice([0xB, 0xB, 2, 4]))
    if kind == 'vault':
        actor.walk = rng.choice([0, 0, 10, 11, 12, 20, 21, 22, 23, 24, 30, 31, 32, 33, 34, 5])
        actor.push = rng.uniform(0, 1); actor.push_decay = rng.uniform(0, 0.2)
        if rng.random() < 0.5:   # a grab point near the feet: the 8/12-tick floors apply
            actor.velocity[0] = actor.position[0] + rng.uniform(-8, 8)
            actor.velocity[2] = actor.position[2] + rng.uniform(-8, 8)
    link_kind = 0
    if kind == 'probe':
        link_kind = rng.choice([0, 0, 1, 2, 3])
        actor.link_kind = 2 if link_kind >= 2 else link_kind
        if rng.random() < 0.3: actor.position[1] = rng.choice([59.0, 60.0, number(bits(60.0) - 1), 200.0])
        mode = rng.choice([0, 0, 1])
        ang = actor.rotation[1] + rng.choice([0.0, rng.uniform(-0.6, 0.6)])
        if seed % 11 == 1:
            scene.flags, link_kind, actor.link_kind, mode = 0, 0, 0, 0
            script, heading = vault_boundary_case(rng, actor, ang)
            NATIVE.em_player_climb_trs(C.byref(actor))
            ang = fp(4.71238899230957 + heading)
            ang = ang - 6.2831855 if ang > 3.1415927 else ang
        elif seed % 5:
            scene.flags = rng.choice([0, 0x81])
            if seed % 7 == 0:   # low feet, where dy == 4.01f is representable exactly
                actor.position[1] = rng.choice([0.0, 0.5, 1.0, 2.0])
                NATIVE.em_player_climb_trs(C.byref(actor))
            if link_kind >= 2 and rng.random() < 0.7: link_kind = actor.link_kind = 0
            script = friendly_probe_script(rng, actor, ang)
        else:
            script = probe_script(rng, actor)
    elif kind == 'hang':
        script = state_script(rng)
    else:
        script = state_script(rng)
        if seed % 4 == 0:   # 00161690 / 00162080: AREA11's corrected ledge, at its box edges
            scene.area, actor.walk = 0xB, 0
            lo, hi = (400.0, 410.0) if kind == 'state' else (397.0, 427.0)
            actor.velocity[0] = rng.choice([lo, hi, (lo + hi) / 2, number(bits(lo) - 1), number(bits(hi) + 1)])
            lo, hi = (236.0, 246.0) if kind == 'state' else (227.0, 257.0)
            actor.velocity[2] = rng.choice([lo, hi, (lo + hi) / 2, number(bits(lo) - 1), number(bits(hi) + 1)])
            top = rng.choice([285.0, 587.0, number(bits(285.0) - 1), number(bits(587.0) + 1), 300.0])
            actor.ledge = rng.choice([14.0, 20.0, 30.0])
            actor.position[1] = exact_above(0.0, 0.0) + (top - actor.ledge)
            if fp(actor.position[1] + actor.ledge) != top:
                actor.position[1] = fp(top - actor.ledge)
    oracle = Oracle(ELF, script.copy(), scene)
    oracle.load(actor, link_kind)
    native = Native(script.copy())
    written = record_writes(oracle.ee, ACTOR)
    if kind == 'probe':
        oracle.ee.call(PROBE, (ACTOR, mode), (ang,))
        result = NATIVE.em_player_climb_probe(C.byref(actor), C.byref(scene), mode, ang, C.byref(native.workers))
        assert result == s32(oracle.ee.r[2]), (kind, seed, result, oracle.ee.r[2])
    elif kind == 'vault':
        oracle.ee.call(STATE3, (ACTOR,))
        result = NATIVE.em_player_climb_vault_tick(C.byref(actor), C.byref(scene), C.byref(native.workers))
        assert result == 0, (kind, seed, result)
    elif kind == 'hang':
        oracle.ee.call(HANG, (ACTOR,))
        result = NATIVE.em_player_climb_hang_clear(C.byref(actor), C.byref(native.workers))
        assert result == s32(oracle.ee.r[2]), (kind, seed, result, oracle.ee.r[2])
    else:
        oracle.ee.call(STATE2, (ACTOR,))
        result = NATIVE.em_player_climb_tick(C.byref(actor), C.byref(scene), C.byref(native.workers))
        assert result == 0, (kind, seed, result)
    assert_covered(written, COMPARED, (kind, seed))
    expected = normal_log(oracle.log)
    assert expected == native.log, (kind, seed, expected, native.log)
    want, have = oracle.fields(), native_fields(actor)
    for key in want:
        assert want[key] == have[key], (kind, seed, key, hex(want[key]), hex(have[key]))
    if kind == 'probe':
        outcome = ('start', actor.state, actor.variant) if result == 1 else ('none',)
    else:
        outcome = actor.walk
    return kind, outcome


def build_native():
    out = ROOT / 'build/player_climb_reference'
    out.mkdir(parents=True, exist_ok=True)
    lib = out / ('climb.dylib' if sys.platform == 'darwin' else 'climb.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-shared', '-fPIC', '-Isrc', 'src/game/em_player_climb.c',
                    'src/game/em_player_floor.c', '-lm', '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    A, S, W = C.POINTER(Actor), C.POINTER(Scene), C.POINTER(Workers)
    native.em_player_climb_trs.argtypes = [A]
    native.em_player_climb_probe.argtypes = [A, S, C.c_int, C.c_float, W]
    native.em_player_climb_hang_clear.argtypes = [A, W]
    native.em_player_climb_tick.argtypes = [A, S, W]
    native.em_player_climb_vault_tick.argtypes = [A, S, W]
    return native


def main():
    global ELF, NATIVE, BANK
    ELF = read_elf()
    BANK = (DECOMP / 'extract/chunk28/f01_id3c.bin').read_bytes()
    NATIVE = build_native()
    # build_trs_matrix: the native TRS against the original routine.
    rng = random.Random(0x1C94B0)
    trs = reference_mode.pick(3000, 300)
    for _ in range(trs):
        a = random_actor(rng, NATIVE.em_player_climb_trs)
        ee = EE(ELF)
        for name, offset, count in VECTORS[:3]:
            for i in range(count): ee.putf(ACTOR + offset + 4 * i, getattr(a, name)[i])
        ee.call(0x1C94B0, (ACTOR + 0xD0, ACTOR + 0xB0, ACTOR + 0xC0, ACTOR + 0x60))
        for i in range(16):
            assert ee.load(ACTOR + 0xD0 + 4 * i) == bits(a.matrix[i]), ('trs', i)
    cases = []
    for kind, (full, quick) in {'probe': (20000, 1500), 'state': (8000, 700), 'vault': (8000, 700),
                                'hang': (1000, 80)}.items():
        base = zlib.crc32(kind.encode())
        cases += [(kind, base + 7919 * i) for i in range(reference_mode.pick(full, quick))]
    results = reference_mode.parallel_map(run_case, cases)
    counts, outcomes = {}, {}
    for kind, outcome in results:
        counts[kind] = counts.get(kind, 0) + 1
        outcomes.setdefault(kind, {}).setdefault(outcome, 0)
        outcomes[kind][outcome] += 1
    starts = {k: v for k, v in outcomes['probe'].items() if k[0] == 'start'}
    for state in (2, 3):
        for variant in (0, 1):
            assert ('start', state, variant) in starts, ('climb start not covered', state, variant)
    reference_mode.banner('trs %d' % trs, *('%s %d' % kv for kv in counts.items()))
    print('climb starts (state, +1F1): %s' % sorted(starts.items()))
    # em_collision_column_table against the original 0019BC40 over the
    # captured AREA11 world: a sample weighted to the crate and hill boxes.
    emcl = ROOT / 'assets/scene_snow/snow.emcl'
    missing = world_inputs() or (None if emcl.exists() else 'missing %s' % emcl)
    if missing:
        print('column table world sample: SKIP (%s)' % missing)
    else:
        checked, entries, mismatches, rank_excluded = world_column(reference_mode.pick(2000, 240))
        for point, want, got in mismatches[:5]:
            print('column mismatch at', point, '\n  original', want, '\n  native  ', got)
        assert not mismatches, ('column table mismatches', len(mismatches), 'of', checked)
        print('column table world sample: %d points, %d entries identical; %d rank-excluded'
              % (checked, entries, len(rank_excluded)))
    print('player climb reference: PASS (original 0015DF10/00161790/0017F320 and helpers)')


# ======================================================================
# Whole-world mode (EM_TEST_WORLD=1): the original player stage over the
# captured AREA11 world, with and without the native climb routines
# ======================================================================
import math  # noqa: E402
import os  # noqa: E402
from test_player_slide_reference import Stage, PLAYER, WORLD_RAM, WORLD_SPAD, world_inputs  # noqa: E402


def climb_from_ee(ee, a):
    for name, offset, size, kind in FIELDS:
        raw = ee.load(PLAYER + offset, size)
        if kind == 'f': setattr(a, name, number(raw))
        elif kind == 's': setattr(a, name, raw - (1 << (8 * size)) if raw >> (8 * size - 1) else raw)
        else: setattr(a, name, raw)
    for name, offset, count in VECTORS:
        for i in range(count): getattr(a, name)[i] = number(ee.load(PLAYER + offset + 4 * i))
    a.ledge_normal[0] = number(ee.load(PLAYER + 0x290)); a.ledge_normal[1] = number(ee.load(PLAYER + 0x298))
    a.velocity[1] = number(ee.load(PLAYER + 0x2E4))
    link = ee.load(PLAYER + 0x308)
    a.link_kind = 0 if link == 0 else 2 if ee.load(link + 0x10) in (0x828700, 0x827880) else 1


def climb_to_ee(ee, a):
    for name, offset, size, kind in FIELDS:
        ee.save(PLAYER + offset, raw_of(getattr(a, name), size, kind), size)
    for name, offset, count in VECTORS:
        for i in range(count): ee.save(PLAYER + offset + 4 * i, bits(getattr(a, name)[i]))
    ee.save(PLAYER + 0x290, bits(a.ledge_normal[0])); ee.save(PLAYER + 0x298, bits(a.ledge_normal[1]))


class WorldClimbWorkers:
    """em_player_climb.c workers bound to the ORIGINAL routines on the world."""

    def __init__(self, ee, actor_ref):
        e = self.ee = ee
        scratch = 0x7F0E0000
        sync = lambda: climb_to_ee(e, actor_ref.contents)
        back = lambda: climb_from_ee(e, actor_ref.contents)

        def around(entry, *args, floats=()):
            sync(); result = e.nested(entry, args, floats); back(); return result

        def vector(slot, values, count=4):
            address = scratch + 0x10 * slot
            for i in range(count): e.save(address + 4 * i, bits(values[i]))
            return address

        def fill(kind, out):
            h = out[0]; p = h.probe
            p.kind = kind
            if kind:
                record = e.load(0x700031D0)
                p.node = e.load(record + 0x1A, 2)
                for i in range(3):
                    p.normal[i] = number(e.load(record + 0x24 + 4 * i))
                    p.point[i] = number(e.load(0x700031B0 + 4 * i))
                    p.delta[i] = number(e.load(0x700031C0 + 4 * i))
                entity = e.load(0x700031D4)
                p.entity = 1 if entity else 0
                if entity:
                    p.entity_flags = e.load(entity + 2, 1)
                    h.pickup_box = 1 if e.load(entity + 0x10) == 0x219550 else 0
            return kind

        def move(_, position, target, mask, out):
            return fill(s32(around(0x19AD00, PLAYER, vector(0, target), mask)[0]), out)

        def sweep(_, a, b, mask, out):
            return fill(s32(around(0x19AFE0, PLAYER, vector(0, a), vector(1, b), mask)[0]), out)

        def segment(_, a, b, mask, ident):
            return s32(around(0x19A570, vector(0, a), vector(1, b), mask, ident)[0])

        def column(_, at, height):
            return s32(around(0x1760C0, PLAYER, vector(0, at), 1, floats=(height,))[0])

        def table(_, at, out):
            around(0x19BC40, vector(0, at))
            t = out[0]
            t.count = e.load(0x700031E0)
            assert t.count <= 16, t.count
            for i in range(t.count):
                t.flags[i] = e.load(0x70003170 + 2 * i, 2)
                t.height[i] = number(e.load(0x700030F0 + 4 * i))
                t.aux[i] = number(e.load(0x282250 + 4 * i))
                obj = e.load(0x70003130 + 4 * i)
                t.object_kind[i] = e.load(obj + 0x54, 1)
                raw = e.load(obj + 0x1A, 2)
                t.object_node[i] = raw - 0x10000 if raw & 0x8000 else raw
            return 0

        def skeleton(_, actor, y, eight):
            around(0x1C6DA0, PLAYER)
            node1 = e.load(e.load(0x275B40) + 4)
            y[0] = number(e.load(node1 + 0xC4)); eight[0] = number(e.load(node1 + 8))
            return 0

        def frames(_, clip, out):
            out[0] = s32(around(0x1C61D0, e.load(PLAYER + 0x40), clip)[0]); return 0

        def floor(_, actor, search, out):
            out[0] = s32(around(0x175900, PLAYER, search)[0]); return 0

        plain = lambda entry: ACTOR_FN(lambda _, actor: (around(entry, PLAYER), 0)[1])
        with_arg = lambda entry: ARG_FN(lambda _, actor, arg: (around(entry, PLAYER, arg), 0)[1])
        self.workers = Workers(
            None, MOVE_FN(move), SWEEP_FN(sweep), SEGMENT_FN(segment), COLUMN_FN(column),
            TABLE_FN(table),
            MATH2_FN(lambda _, y, x: number(around(0x11E620, floats=(y, x))[1])),
            MATH1_FN(lambda _, x: number(around(0x11E748, floats=(x,))[1])),
            REQUEST_FN(lambda _, clip, force, blend: (around(0x1749A0, PLAYER, clip, force, floats=(blend,)), 0)[1]),
            ARBITER_FN(lambda _, clip, blend, frame: (around(0x1749F0, PLAYER, clip, floats=(blend, frame)), 0)[1]),
            FRAMES_FN(frames),
            SOUND_FN(lambda _, id: (around(0x1FBD50, PLAYER, id, 0, floats=(300.0,)), 0)[1]),
            EFFECT_FN(lambda _, id, p, r: self.effect(around, vector, id, p)),
            TIER_FN(lambda _, tier: (around(0x182870, PLAYER, tier), 0)[1]),
            SKELETON_FN(skeleton), with_arg(0x178B90), FLOOR_FN(floor), plain(0x1764E0),
            with_arg(0x174AC0), with_arg(0x17C440), plain(0x17C540), plain(0x1796C0), plain(0x17C580))

    def effect(self, around, vector, id, position):
        # 0017DEB0 passes p+B0, or a stack copy of it with y = +250.
        address = PLAYER + 0xB0
        if bits(position[1]) != self.ee.load(PLAYER + 0xB4):
            address = vector(2, (position[0], position[1], position[2], 1.0))
        around(0x1EFD90, id, address, PLAYER + 0xC0)
        return 0


def climb_scene(ee):
    scene = Scene()
    node1 = ee.load(ee.load(0x275B40) + 4)
    for i in range(4): scene.hip_world[i] = number(ee.load(node1 + 0xC0 + 4 * i))
    scene.flags = ee.load(0x8106BE, 1)
    scene.area = ee.load(0x810700, 1)
    return scene


def native_climb_hooks(ee, counts):
    def run(name, body):
        def hook(e):
            actor = Actor(); climb_from_ee(e, actor)
            scene = climb_scene(e)
            workers = WorldClimbWorkers(e, C.pointer(actor))
            result = body(e, actor, scene, workers.workers)
            assert result >= 0, (name, result)
            climb_to_ee(e, actor)
            e.ret_int(result)
            counts[name] = counts.get(name, 0) + 1
        return hook
    ee.hooks[PROBE] = run('probe', lambda e, a, s, w: NATIVE.em_player_climb_probe(
        C.byref(a), C.byref(s), s32(e.arg(1)), number(e.f[12]), C.byref(w)))
    ee.hooks[STATE2] = run('state2', lambda e, a, s, w: NATIVE.em_player_climb_tick(
        C.byref(a), C.byref(s), C.byref(w)))
    ee.hooks[STATE3] = run('state3', lambda e, a, s, w: NATIVE.em_player_climb_vault_tick(
        C.byref(a), C.byref(s), C.byref(w)))


class BoxFace(C.Structure):
    _fields_ = [('face', C.c_uint32), ('origin', C.c_float * 3), ('extent', C.c_float * 3)]


class Cell(C.Structure):
    _fields_ = [('uid', C.c_uint32), ('attr', C.c_uint32), ('face_count', C.c_uint32),
                ('bbox', C.c_float * 6), ('faces', C.POINTER(BoxFace))]


class ColumnOwner(C.Structure):
    _fields_ = [('cell', C.POINTER(Cell)), ('alive', C.c_uint8), ('owner_class', C.c_uint8),
                ('uid', C.c_uint8), ('kind54', C.c_uint8)]


class Column(C.Structure):
    _fields_ = [('count', C.c_int), ('flags', C.c_uint16 * 20), ('height', C.c_float * 20),
                ('aux', C.c_float * 20), ('owner', C.c_int * 20), ('poly', C.c_int * 20),
                ('object_node', C.c_int16 * 20), ('object_kind', C.c_uint8 * 20)]


class ColumnMath(C.Structure):
    _fields_ = [('sqrt', MATH1_FN), ('atan', MATH1_FN), ('context', C.c_void_p)]


COLUMN_BRIDGE = r"""
#include <stdlib.h>
#include "game/em_collision.h"
void *world_new(const char *path) {
    EmCollision *c = calloc(1, sizeof *c);
    if (!c || em_collision_load(c, path)) { free(c); return 0; }
    return c;
}
"""


def column_owners(ram, spad):
    """The published class-4 owners (D_00275B7C/D_00275B84) and their cells
    from the captured zone table (0x70003250), as the port would bind them."""
    table = struct.unpack_from('<I', spad, 0x3250)[0]
    listing = struct.unpack_from('<I', ram, 0x275B7C)[0]
    count = struct.unpack_from('<h', ram, 0x275B84)[0]
    owners, keep, unsupported = (ColumnOwner * max(1, count))(), [], []
    for i in range(count):
        actor = struct.unpack_from('<I', ram, listing + 4 * i)[0]
        o = owners[i]
        o.alive, o.owner_class = ram[actor], ram[actor + 2] & 0x1F
        o.uid, o.kind54 = struct.unpack_from('<H', ram, actor + 0xE)[0] >> 8, ram[actor + 0x54]
        offset = struct.unpack_from('<I', ram, table + 4 * o.uid + 4)[0] if o.uid != 0xFF else 0
        if not offset:
            continue
        hull = table + (offset & 0x3FFFFFFF)
        prims = struct.unpack_from('<h', ram, hull + 0x18)[0]
        faces = (BoxFace * prims)()
        at = hull + 0x1C
        if struct.unpack_from('<H', ram, at)[0] & 0xF000 != 0x2000:
            # n-gon cells (elevator uid 4, truck uid 14, pickup boxes uid 19/23):
            # EmCollCell carries type-0x2000 faces only, so these owners cannot
            # be bound; their columns are excluded from the comparison.
            unsupported.append((o.uid, struct.unpack_from('<6f', ram, hull)))
            o.alive = 0
            continue
        for j in range(prims):
            header = struct.unpack_from('<H', ram, at)[0]
            assert header & 0xF000 == 0x2000, ('mixed cell prims', hex(header))
            faces[j].face = ram[at + 2]
            faces[j].origin[:] = struct.unpack_from('<3f', ram, at + 4)
            faces[j].extent[:] = struct.unpack_from('<3f', ram, at + 0x10)
            at += 0x1C
        cell = Cell(o.uid, 0, prims, (C.c_float * 6)(*struct.unpack_from('<6f', ram, hull)), faces)
        keep += [faces, cell]
        o.cell = C.pointer(cell)
    return owners, count, keep, unsupported


def world_column(points, seed=0x19BC40, explicit=None):
    """0019BC40 over the captured AREA11 world against em_collision_column_table
    over the user's EMCL and the captured cells."""
    ram, spad = WORLD_RAM.read_bytes(), WORLD_SPAD.read_bytes()
    out = ROOT / 'build/player_climb_reference'
    source, lib = out / 'column_bridge.c', out / ('column.dylib' if sys.platform == 'darwin' else 'column.so')
    source.write_text(COLUMN_BRIDGE)
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared',
                    '-fPIC', '-Isrc', str(source), 'src/game/em_collision.c', '-lm', '-o', str(lib)],
                   cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    native.world_new.restype = C.c_void_p; native.world_new.argtypes = [C.c_char_p]
    native.em_collision_column_table.argtypes = [C.c_void_p, C.POINTER(ColumnOwner), C.c_uint,
                                                 C.POINTER(C.c_float), C.POINTER(ColumnMath),
                                                 C.POINTER(Column)]
    world = native.world_new(str(ROOT / 'assets/scene_snow/snow.emcl').encode())
    assert world, 'assets/scene_snow/snow.emcl'
    owners, count, keep, unsupported = column_owners(ram, spad)
    math_workers = ColumnMath(MATH1_FN(lambda _, x: LIBC.sqrtf(x)), MATH1_FN(lambda _, x: LIBC.atanf(x)), None)
    rng = random.Random(seed)
    ee = EE(ELF, ram, spad)
    ee.hooks[SQRT] = lambda e: e.ret_float(LIBC.sqrtf(e.farg(0)))
    ee.hooks[0x11DBB8] = lambda e: e.ret_float(LIBC.atanf(e.farg(0)))
    ee.hooks[FABS] = lambda e: e.ret_float(abs(e.farg(0)))
    spad_before = bytes(ee.spad)
    node_base = struct.unpack_from('<I', ram, struct.unpack_from('<I', ram, 0x28A598)[0] + 0x20)[0] + \
        struct.unpack_from('<I', ram, 0x28A598)[0]
    emcl = (ROOT / 'assets/scene_snow/snow.emcl').read_bytes()
    polys = struct.unpack_from('<I', emcl, 12)[0]
    pool = 0x30 + 12 * struct.unpack_from('<I', emcl, 8)[0]
    first_grid = [emcl[pool + 24 * i + 21] for i in range(polys)].index(4)
    s16 = lambda v: v - 0x10000 if v & 0x8000 else v
    rank_excluded = []
    at = 0x7F0E0000
    mismatches, entries = [], 0
    crate_points = [(rng.uniform(205, 238), rng.uniform(185, 225), rng.uniform(283, 302)) for _ in range(points // 4)]
    hill_points = [(rng.uniform(205, 280), rng.uniform(180, 225), rng.uniform(290, 370)) for _ in range(points // 4)]
    level = [(rng.uniform(100, 520), rng.uniform(150, 350), rng.uniform(80, 520)) for _ in range(points - 2 * (points // 4))]
    def excluded(x, z):
        return any(b[0] <= x <= b[3] and b[2] <= z <= b[5] for _, b in unsupported)
    checked = 0
    for x, y, z in (explicit if explicit is not None else crate_points + hill_points + level):
        x, y, z = number(bits(x)), number(bits(y)), number(bits(z))
        if excluded(x, z):
            continue
        checked += 1
        ee.spad[:] = spad_before
        for i, v in enumerate((x, y, z, 1.0)): ee.putf(at + 4 * i, v)
        ee.call(TABLE, (at,))
        expect = []
        for i in range(ee.load(0x700031E0)):
            obj = ee.load(0x70003130 + 4 * i)
            expect.append((ee.load(0x70003170 + 2 * i, 2), ee.load(0x700030F0 + 4 * i),
                           ee.load(0x282250 + 4 * i), ee.load(obj + 0x54, 1), ee.load(obj + 0x1A, 1)))
        col = Column()
        native.em_collision_column_table(world, owners, count, (C.c_float * 3)(x, y, z),
                                         C.byref(math_workers), C.byref(col))
        got = []
        for i in range(col.count):
            kind = col.object_kind[i] if col.owner[i] >= 0 else None
            got.append((col.flags[i], bits(col.height[i]), bits(col.aux[i]),
                        kind, col.object_node[i] & 0xFF if col.owner[i] < 0 else None))
        want = [(f, h, a, k if (f & 0x8000) else None, n if not (f & 0x8000) else None)
                for f, h, a, k, n in expect]
        entries += len(want)
        if want != got:
            # Known class: 0019BC40 only visits grid nodes whose rank bounds
            # (node +0x0C, from the 0019F1A0 point ranks) admit the point; the
            # port has no rank tables and tests every node. Accept a mismatch
            # only if dropping exactly the rank-excluded native entries (and
            # re-running the cull on the rest is not needed because none of
            # them sat within 3 units of a survivor) reproduces the original.
            ee.spad[:] = spad_before
            ee.call(0x19F1A0, (at, 0x33))
            ranks = [s16(ee.load(0x70003240 + 2 * i, 2)) for i in range(6)]
            kept = []
            for i in range(col.count):
                if col.poly[i] >= 0:
                    node = node_base + 64 * (col.poly[i] - first_grid)
                    bounds = [s16(ee.load(node + 0xC + 2 * k, 2)) for k in range(6)]
                    if (ranks[0] < bounds[0] or bounds[1] < ranks[1] or bounds[4] > ranks[4]
                            or bounds[5] < ranks[5]):
                        continue
                kept.append(got[i])
            if kept == want:
                rank_excluded.append((x, y, z))
            else:
                mismatches.append(((x, y, z), want, got))
    return checked, entries, mismatches, rank_excluded


WORLD_ROUTES = {
    # Use in front of the single crate 0x7A7C70, standing on the ground.
    'crate': ((228.8, 189.9, 280.0), 0.0, [(3, {}), (1, {'press': 0x40}), (120, {})]),
    # Run at the crate (tier 3, speed 0.8) and press Use 18 units away: vault.
    'vault': ((228.8, 189.9, 250.0), 0.0,
              [(3, {}), (40, {'gait': 3, 'ly': 0, 'camera': -math.pi / 2}),
               (1, {'gait': 3, 'ly': 0, 'press': 0x40}), (120, {})]),
    # From the top of crate 0x7A7C70, face the stacked crate 0x7A7980 (west).
    'stack': ((228.8, 203.8, 292.0), -math.pi / 2, [(3, {}), (1, {'press': 0x40}), (120, {})]),
}


ROUTE = DECOMP / 'build/s87/route'
# The PCSX2 route beats (docs/FIRST_LEVEL_ROUTE.md) that contain a Use climb
# from idle: the crates (05), the tank and the pipe end (11) and the east tower
# (13). Each beat's world is the RAM/scratchpad snapshot its trace resumed from
# (trace['source']), so the owners and flags are the ones the climb really met.
CAPTURE_BEATS = ('05_boxes', '11_crevice_prompt', '13_east_tower')


def capture_inputs():
    """The trace and source snapshot files the capture route needs, or the
    missing ones."""
    import json
    missing, beats = [], []
    for beat in CAPTURE_BEATS:
        trace_path = ROUTE / beat / 'trace.json'
        if not trace_path.exists():
            missing.append(str(trace_path)); continue
        trace = json.loads(trace_path.read_text())
        ram, spad = (ROUTE / trace['source'] / name for name in ('eeMemory.bin', 'scratchpad.bin'))
        missing += [str(f) for f in (ram, spad) if not f.exists()]
        beats.append((beat, trace, ram, spad))
    return beats, missing


def world_capture(beat, trace, ram_path, spad_path):
    """The native climb (workers bound to the original routines) against a
    real PCSX2 route capture: from the idle frame before each Cross press, the
    original stage and the native stage run side by side on the beat's source
    snapshot, seeded with the captured feet +A0, body +B0 and yaw +C4. Every
    frame their full 0x320-byte actors and sound/effect calls must be
    identical, the state/+1F0/clip must equal the trace's, and the feet, body
    position, yaw and clip clock must agree with it within the trace's printed
    precision plus the seed's rounding (5e-4, 5e-4, 2e-5, 1e-3; measured
    maxima 9.1e-5, 9.3e-5, 4.3e-6, 0). The press frame's clock and body
    position come from the idle pose (clip phase and blend, e.g. 13_east_tower
    presses 4 frames after a walk stop), which the seeded stage does not share,
    so those two are compared from the next frame on."""
    rows = {r['counter']: r for r in trace['rows']}
    # Route beats are re-captured, so the press counters are read from the
    # trace: the frames whose +5 goes from idle (0) to climb (2). Each must
    # follow one of the trace's Cross inputs (pad bit 0x4000) by a few frames
    # of pad latency, one climb per Cross.
    presses = [c for c in sorted(rows) if c - 1 in rows and rows[c - 1]['p5'] == 0 and rows[c]['p5'] == 2]
    crosses = [trace['first_counter'] + i['f'] for i in trace['inputs'] if i['buttons'] & 0x4000]
    assert presses and len(presses) == len(crosses), (beat, 'expected one Cross per climb', presses, crosses)
    assert all(0 < p - c <= 8 for p, c in zip(presses, crosses)), (beat, 'climb not led by Cross', presses, crosses)
    ram, spad = ram_path.read_bytes(), spad_path.read_bytes()
    results = []
    for press in presses:
        before = rows[press - 1]
        original = Stage(ELF, ram, spad, tuple(before['pos']), before['yaw'])
        stage = Stage(ELF, ram, spad, tuple(before['pos']), before['yaw'])
        for seeded in (original, stage):
            # the idle body position +B0 moves with the idle clip; seed the
            # captured one (Stage seeds +B0 = the feet)
            for i in range(3): seeded.ee.putf(PLAYER + 0xB0 + 4 * i, before['hip'][i])
        counts = {}
        native_climb_hooks(stage.ee, counts)
        press_input = {'press': 0x40}
        frame = press
        while True:
            original.step(**press_input); stage.step(**press_input)
            press_input = {}
            a, b = original.actor(), stage.actor()
            if a != b:
                diff = [hex(k) for k in range(0x320) if a[k] != b[k]]
                raise AssertionError((beat, press, 'frame', frame, 'actor bytes differ at', diff[:24]))
            assert original.events == stage.events, (beat, press, 'frame', frame, 'sound/effect calls')
            r = rows[frame]
            ee = stage.ee
            state = (ee.load(PLAYER + 5, 1), ee.load(PLAYER + 0x1F0, 1), ee.load(PLAYER + 0x20C, 2))
            assert state == (r['p5'], r['m1F0'], r['clip']), (beat, press, frame, state, r)
            feet = [number(ee.load(PLAYER + 0xA0 + 4 * i)) for i in range(3)]
            body = [number(ee.load(PLAYER + 0xB0 + 4 * i)) for i in range(3)]
            assert all(abs(feet[i] - r['pos'][i]) <= 5e-4 for i in range(3)), (beat, press, frame, feet, r['pos'])
            assert abs(number(ee.load(PLAYER + 0xC4)) - r['yaw']) <= 2e-5, (beat, press, frame, 'yaw', r['yaw'])
            if frame != press:
                assert all(abs(body[i] - r['hip'][i]) <= 5e-4 for i in range(3)), (beat, press, frame, body, r['hip'])
                assert abs(number(ee.load(PLAYER + 0x3C)) - r['clock']) < 1e-3, (beat, press, frame)
            if r['p5'] == 0:
                break
            frame += 1
        results.append((press, frame - press + 1, counts, [round(v, 3) for v in feet],
                        sorted({e[1] for e in stage.events})))
    return results


def world_main():
    global ELF, NATIVE, BANK
    ELF = read_elf(); NATIVE = build_native()
    missing = world_inputs()
    if missing:
        raise SystemExit('world mode: %s (docs/PLAYER_CLIMB_SLIDE.md)' % missing)
    ram, spad = WORLD_RAM.read_bytes(), WORLD_SPAD.read_bytes()
    routes = os.environ.get('EM_WORLD_ROUTES', ','.join(list(WORLD_ROUTES) + ['column', 'capture'])).split(',')
    if 'capture' in routes:
        beats, missing = capture_inputs()
        if missing:
            raise SystemExit('capture route needs %s' % missing)
        for beat, trace, ram_path, spad_path in beats:
            for press, frames, counts, end, events in world_capture(beat, trace, ram_path, spad_path):
                print('climb vs PCSX2 capture %s press %d: PASS %d frames (native = original bytes; state, '
                      '+1F0, clip exact; feet, body, yaw and clock within the trace precision), native calls %s, end %s, '
                      'events %s' % (beat, press, frames, counts, end, events))
        routes = [r for r in routes if r != 'capture']
    if 'column' in routes:
        checked, entries, mismatches, rank_excluded = world_column(int(os.environ.get('EM_WORLD_POINTS', '2000')))
        for point, want, got in mismatches[:5]:
            print('column mismatch at', point, '\n  original', want, '\n  native  ', got)
        assert not mismatches, ('column table mismatches', len(mismatches), 'of', checked)
        print('column table world: PASS %d points, %d entries identical (flags, height, aux, object); '
              '%d points differ only by rank-excluded grid nodes (no rank tables in the EMCL): %s'
              % (checked, entries, len(rank_excluded), rank_excluded[:4]))
        routes = [r for r in routes if r != 'column']
    for name in routes:
        start, yaw, script = WORLD_ROUTES[name]
        original = Stage(ELF, ram, spad, start, yaw)
        translated = Stage(ELF, ram, spad, start, yaw)
        counts = {}
        native_climb_hooks(translated.ee, counts)
        frame, states = 0, set()
        for count, inputs in script:
            for _ in range(count):
                original.step(**inputs); translated.step(**inputs)
                a, b = original.actor(), translated.actor()
                if a != b:
                    diff = [hex(k) for k in range(0x320) if a[k] != b[k]]
                    raise AssertionError((name, 'frame', frame, 'actor bytes differ at', diff[:24]))
                assert original.events == translated.events, (name, 'frame', frame, 'sound/effect calls')
                states.add(original.ee.load(PLAYER + 5, 1))
                frame += 1
        position = [round(number(original.ee.load(PLAYER + 0xA0 + 4 * i)), 3) for i in range(3)]
        print('player climb world %s: PASS %d frames, native calls %s, player states %s, end %s'
              % (name, frame, counts, sorted(states), position))


if __name__ == '__main__':
    if os.environ.get('EM_TEST_WORLD', '') not in ('', '0'):
        world_main()
    else:
        main()
