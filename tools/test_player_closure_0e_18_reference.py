#!/usr/bin/env python3
"""Execute the original FLOOR-closure states 0xE / 0x13 / 0x14 / 0x18 and
compare em_player_closure_0e_18.c.

docs/PLAYER_CLOSURE_0E_18.md. The original instructions run from the captured
AREA11 EE RAM (../Extermination/build/startup-reference/playable_ee.bin, state
04), whose code and table bytes are first checked against the user's pinned
ELF; none are embedded here. The interpreter is the shared EE of
tools/test_player_slide_reference.py as subclassed by
tools/test_player_hang_reference.py (HangEE: every COP1 operation through
tools/ee_float_model.py, the measured EE rules of docs/EE_FLOAT_MODEL.md),
subclassed once more here (ClosureEE) to record this lane's branches. The
shared files are not edited.

Executed, unmodified:
  00168050  state 0xE          0016B790  state 0x13
  0016B8A0  state 0x14         0016D130  state 0x18
  00180000/00180004, 00180040, 00180080, 001800C0, 00180100, 00180180,
  00180200, 00180280, 00180420, 00180300, 001806E0, 00180790, 00180850,
  00182AB0, 00174AB0, 00178620, 00179150, 001790B0, 0016BAE0
and, as original code inside them, the leaves other lanes translated
(00181180 em_player_major2, 0017F240 em_player_hang, 0011DF78
em_sdk_math_original) and the copies 00102948 / 001031E0.

Every other callee is hooked, scripted per case and recorded (never
simulated as a claim about the callee): the scripted return value and the
scripted record / node / hit writes are applied identically on both sides,
and the call sequence with every argument (floats as bits, vectors as the
words the callee receives) must match. At every worker call, and after the
routine, all 0x320 record bytes, the compared scratchpad words, the two
globals the routines store and the spawned node must match. The hooked set
must equal the jal/j targets of the executed routines. Every conditional
branch of the translated routines must be seen both taken and not taken.

EM_TEST_FULL=1 runs the exhaustive sweep; the default run is a fixed-seed
sample with the same comparisons and the same coverage assertions.
"""
import ctypes as C
import random
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from test_player_slide_reference import read_elf, bits, number, s32, sx32, REFERENCE  # noqa: E402
from test_player_floor_reference import LiveActor, cached_build  # noqa: E402
from test_player_hang_reference import HangEE, conditional_branches  # noqa: E402
import reference_mode  # noqa: E402

RAM_PATH = REFERENCE / 'playable_ee.bin'
PLAYER = 0x8102B0
LANE_DIR = ROOT / 'build/player_closure_0e_18_reference'

# Translated routines: (address, size in bytes).
TRANSLATED = {
    0x168050: 0x11F4, 0x16B790: 0x10C, 0x16B8A0: 0x23C, 0x16D130: 0xD04,
    0x180000: 0x40, 0x180040: 0x40, 0x180080: 0x40, 0x1800C0: 0x40, 0x180100: 0x78,
    0x180180: 0x78, 0x180200: 0x78, 0x180280: 0x78, 0x180300: 0x120, 0x180420: 0x40,
    0x1806E0: 0xB0, 0x180790: 0xB8, 0x180850: 0x154, 0x182AB0: 0x3C, 0x174AB0: 0x10,
    0x178620: 0x18C, 0x179150: 0x80, 0x1790B0: 0x9C, 0x16BAE0: 0x94,
}
# Other lanes' translations and the copies, run as original code.
RUN_UNHOOKED = {0x181180: 0x6C, 0x17F240: 0xE0, 0x11DF78: 0x1C, 0x102948: 0xC, 0x1031E0: 0x1C}
TABLES = ((0x248610, 16), (0x248620, 16), (0x2754B0, 8), (0x2488AC, 4), (0x275498, 8),
          (0x248970, 112))

# Scratchpad words the native keeps (EmPlayerClosureScratch).
SPAD = ((0x70003600, 8), (0x700038A0, 16), (0x70003A20, 1))
G275B08, G810702 = 0x275B08, 0x810702
SPAWN, HIT, VEC = 0x7F0A0000, 0x7F0B0000, 0x7F0C0000    # private stack-region records

# ------------------------------------------------------------------ EE ----


class ClosureEE(HangEE):
    def branch(self, word, pc):
        outcome = super(HangEE, self).branch(word, pc)
        if outcome is not None and any(a <= pc < a + n for a, n in TRANSLATED.items()):
            self.branches.add((pc, outcome[0]))
        return outcome


# ------------------------------------------------------- native side -----

class Scene(C.Structure):
    _fields_ = [('pad', C.c_uint16), ('use_mask', C.c_uint16), ('area', C.c_uint8),
                ('fade', C.c_int16), ('d275B14', C.c_int32), ('d275B0C', C.c_uint32),
                ('d275B10', C.c_uint32), ('d281B64', C.c_uint32)]


class Scratch(C.Structure):
    _fields_ = [('s3600', C.c_uint32 * 8), ('s38A0', C.c_uint32 * 16), ('s3A20', C.c_uint32)]


A = C.POINTER(LiveActor)
U32P, IP, FP = C.POINTER(C.c_uint32), C.POINTER(C.c_int), C.POINTER(C.c_float)
F = C.CFUNCTYPE
SCENE_FN = F(C.c_int, C.c_void_p, C.POINTER(Scene))
NODE_FN = F(C.c_int, C.c_void_p, C.c_int, C.c_uint, U32P)
HIT_FN = F(C.c_int, C.c_void_p, C.POINTER(C.c_uint8))
SET32_FN = F(C.c_int, C.c_void_p, C.c_int32)
SET8_FN = F(C.c_int, C.c_void_p, C.c_uint8)
CALL_FN = F(C.c_int, C.c_void_p, A, C.c_int, C.c_int, C.c_float)       # request / sound
ACT_FN = F(C.c_int, C.c_void_p, A)
ACT_R_FN = F(C.c_int, C.c_void_p, A, IP)
ACT_I_FN = F(C.c_int, C.c_void_p, A, C.c_int)
ACT_I_R_FN = F(C.c_int, C.c_void_p, A, C.c_int, IP)
BLEND_FN = F(C.c_int, C.c_void_p, A, C.c_float)
SIDE_FN = F(C.c_int, C.c_void_p, A, C.c_int, C.c_float)
ARB_FN = F(C.c_int, C.c_void_p, A, C.c_int, C.c_float, C.c_float)
SWEEP_FN = F(C.c_int, C.c_void_p, A, U32P, U32P, C.c_uint, IP)
POINT_FN = F(C.c_int, C.c_void_p, A, U32P, C.c_uint, IP)
R_FN = F(C.c_int, C.c_void_p, IP)
FRAMES_FN = F(C.c_int, C.c_void_p, C.c_uint32, C.c_int, C.POINTER(C.c_int32))
TOINT_FN = F(C.c_int, C.c_void_p, C.c_uint32, C.POINTER(C.c_int32))
II_FN = F(C.c_int, C.c_void_p, C.c_int, C.c_int)
I_FN = F(C.c_int, C.c_void_p, C.c_int)
ALLOC_FN = F(C.c_int, C.c_void_p, C.c_int, C.POINTER(C.POINTER(C.c_uint8)))
TRS_FN = F(C.c_int, C.c_void_p, U32P, U32P, U32P, U32P)
XFORM_FN = F(C.c_int, C.c_void_p, U32P, U32P, U32P)
FLOAT_FN = F(C.c_int, C.c_void_p, C.c_float, FP)
BITS1_FN = F(C.c_int, C.c_void_p, C.c_uint32, U32P)
BITS3_FN = F(C.c_int, C.c_void_p, C.c_uint32, C.c_uint32, C.c_uint32, U32P)

WORKER_FIELDS = [
    ('context', C.c_void_p), ('scratch', C.POINTER(Scratch)),
    ('scene', SCENE_FN), ('node', NODE_FN), ('hit_surface', HIT_FN), ('set_275B08', SET32_FN),
    ('set_810702', SET8_FN),
    ('request', CALL_FN), ('sound', CALL_FN), ('steer', ACT_FN), ('ledge_move', ACT_I_R_FN),
    ('clip_FF80', BLEND_FN), ('clip_FC80', BLEND_FN), ('clip_DFB0', SIDE_FN), ('clip_E0D0', SIDE_FN),
    ('clip_E150', SIDE_FN), ('clip_E1D0', SIDE_FN), ('surface_sound', ACT_I_FN), ('sound_109', ACT_FN),
    ('land_sound', ACT_I_FN), ('floor', ACT_I_R_FN), ('place', ACT_FN), ('pose_reset', BLEND_FN),
    ('skeleton', ACT_FN), ('translate', ACT_I_FN), ('arbiter', ARB_FN), ('use_test', ACT_R_FN),
    ('clip_row', ACT_R_FN), ('clip_row_B', ACT_R_FN), ('ledge_3D', ACT_R_FN), ('ledge_3B', ACT_R_FN),
    ('sweep', SWEEP_FN), ('ground', POINT_FN), ('point_test', POINT_FN),
    ('random', R_FN), ('clip_frames', FRAMES_FN), ('to_int', TOINT_FN), ('fade', II_FN),
    ('fade_end', II_FN), ('camera', I_FN), ('alloc', ALLOC_FN), ('trs', TRS_FN),
    ('transform', XFORM_FN), ('vadd', XFORM_FN), ('sine', FLOAT_FN), ('cosine', FLOAT_FN),
    ('wrap', BITS1_FN), ('approach', BITS3_FN),
]


class Workers(C.Structure):
    _fields_ = WORKER_FIELDS


# Original callee address -> worker name.
CALLEES = {
    0x1749A0: 'request', 0x1FBD50: 'sound', 0x174FD0: 'steer', 0x1809B0: 'ledge_move',
    0x17FF80: 'clip_FF80', 0x17FC80: 'clip_FC80', 0x17DFB0: 'clip_DFB0', 0x17E0D0: 'clip_E0D0',
    0x17E150: 'clip_E150', 0x17E1D0: 'clip_E1D0', 0x182430: 'surface_sound', 0x182A70: 'sound_109',
    0x182870: 'land_sound', 0x175900: 'floor', 0x187EE0: 'place', 0x174A50: 'pose_reset',
    0x1C68C0: 'skeleton', 0x178B90: 'translate', 0x1749F0: 'arbiter', 0x1607D0: 'use_test',
    0x188550: 'clip_row', 0x1885B0: 'clip_row_B', 0x178390: 'ledge_3D', 0x1782A0: 'ledge_3B',
    0x19AFE0: 'sweep', 0x19AB20: 'ground', 0x19AD00: 'point_test', 0x179B90: 'random',
    0x1C61D0: 'clip_frames', 0x1281C0: 'to_int', 0x1AEDE0: 'fade', 0x1AEE10: 'fade_end',
    0x1B0460: 'camera', 0x1AFA90: 'alloc', 0x1C94B0: 'trs', 0x1026A0: 'transform', 0x1028B8: 'vadd',
    0x11E2A8: 'sine', 0x11DE90: 'cosine', 0x1B1470: 'wrap', 0x1B12B0: 'approach',
}
SCALAR = {'sine', 'cosine', 'wrap', 'approach', 'to_int'}
NO_ACTOR = SCALAR | {'random', 'clip_frames', 'fade', 'fade_end', 'camera', 'alloc', 'trs',
                     'transform', 'vadd'}

# ------------------------------------------------------------ scripts ----


def finite(rng):
    if rng.random() < 0.15:
        return rng.choice([0, 0x80000000, 1, 0x80000001, 0x3F800000, 0xBF800000])
    return bits(rng.uniform(-400.0, 400.0))


KNOBS = {}


def profile(seed):
    """Per-case knobs (set by random_case, read by both sides' scripts): the
    hit surface the case's sweeps favour and the steering quadrant 00174FD0
    favours, so that the two-sweep tests and the quadrant-specific sub-states
    are reached."""
    return KNOBS.get(seed, (0x05, None))


def effect(seed, name, index):
    """The scripted result of the index-th call of `name` in case `seed`:
    (result, [(offset, size, value)], {(node, offset): bits}, out words,
    hit surface)."""
    rng = random.Random(zlib.crc32(('%d/%s/%d' % (seed, name, index)).encode()))
    writes, nodes, out, result, surface = [], {}, None, 0, None

    def maybe(p, offset, size, value):
        if rng.random() < p: writes.append((offset, size, value))
    if name in ('request', 'clip_FF80', 'clip_FC80', 'clip_DFB0', 'clip_E0D0', 'clip_E150',
                'clip_E1D0', 'pose_reset', 'arbiter'):
        maybe(0.3, 0x200, 4, rng.choice([0, 0x1000, 0x8000, 0x9000]))
        maybe(0.1, 0x3C, 4, finite(rng))
    elif name == 'sound':
        result = rng.choice([0, 3, -1 & 0xFFFFFFFF])
    elif name == 'steer':
        mode = profile(seed)[1]
        if mode is not None and rng.random() < 0.7:
            writes.append((0x24C, 4, mode))
        else:
            writes.append((0x24C, 4, rng.choice([0, 1, 2, 3, 0xFFFFFFFF, 5])))
        writes.append((0x23F, 1, rng.randrange(4)))
        maybe(0.3, 0x2F1, 1, rng.randrange(2))
        maybe(0.2, 0x200, 4, rng.choice([0, 0x1000, 0x8000]))
        maybe(0.2, 7, 1, rng.randrange(5))
    elif name == 'ledge_move':
        result = rng.choice([0, 1, 1, 2])
    elif name in ('surface_sound', 'land_sound', 'place', 'sound_109'):
        maybe(0.3, 0xB0, 4, finite(rng)); maybe(0.3, 0xB4, 4, finite(rng))
        maybe(0.2, 0x254, 4, finite(rng)); maybe(0.2, 6, 1, rng.randrange(0x60))
    elif name == 'floor':
        result = rng.choice([0, 1, 0x81])
        maybe(0.4, 0xB4, 4, finite(rng)); maybe(0.2, 0x23B, 1, rng.randrange(256))
    elif name == 'skeleton':
        for key in ((1, 0xC0), (1, 0xC4), (1, 0xC8), (1, 0xCC), (0, 4), (0, 8)):
            if rng.random() < 0.7: nodes[key] = finite(rng)
        maybe(0.3, 0xB4, 4, finite(rng))
    elif name == 'translate':
        maybe(0.4, 0xB4, 4, finite(rng)); maybe(0.3, 0x2E4, 4, finite(rng))
        if rng.random() < 0.4: nodes[(0, 4)] = finite(rng)
        if rng.random() < 0.3: nodes[(0, 8)] = finite(rng)
    elif name == 'use_test':
        result = rng.choice([0, 0, 1])
        maybe(0.3, 6, 1, rng.randrange(8))
    elif name in ('clip_row', 'clip_row_B'):
        result = rng.choice([0x7B, 0x8E, 0xBB, 0xE6, -1 & 0xFFFFFFFF, 0x8000 | 0xFFFF0000])
    elif name == 'ledge_3D':
        result = rng.choice([0, 1, 5])
    elif name == 'ledge_3B':
        result = rng.choice([0, 1, 2])
        maybe(0.3, 0xD, 1, rng.randrange(6))
    elif name == 'sweep':
        # Each case favours one hit surface, so that the two-sweep tests
        # (00180850, 001806E0) also see the same surface twice.
        favourite = profile(seed)[0]
        result = rng.choice([0, 1, 2, 4, 6, 7, 8, 9, 1, 6, 6, 2])
        surface = favourite if rng.random() < 0.8 else rng.choice([0x32, 0x3B, 0x33, 0x3D, 0x05])
    elif name == 'ground':
        result = rng.choice([0, 0, 1])
        maybe(0.5, 0x280, 4, finite(rng)); maybe(0.5, 0x284, 4, finite(rng))
    elif name == 'point_test':
        result = rng.choice([0, 1, 0, 2])
    elif name == 'random':
        result = rng.randrange(5)
    elif name == 'clip_frames':
        result = rng.choice([0, 53, 60, 90, rng.randrange(-100, 200), 0x7FFFFFFF, -1 & 0xFFFFFFFF])
    elif name == 'to_int':
        result = rng.choice([0, 1, 24, 0x10005, -3 & 0xFFFFFFFF, rng.randrange(-200, 200) & 0xFFFFFFFF])
    elif name == 'alloc':
        result = 0 if rng.random() < 0.3 else SPAWN
    elif name == 'trs':
        out = [finite(rng) for _ in range(16)]
    elif name in ('transform', 'vadd'):
        out = [finite(rng) for _ in range(4)]
    elif name in ('sine', 'cosine'):
        result = bits(rng.uniform(-1.0, 1.0)) if rng.random() < 0.9 else rng.choice([0, 0x80000000])
        # The originals are pure; these scripted writes only check that the
        # native reads each operand when the original does (before or after).
        maybe(0.15, 0x38, 4, finite(rng)); maybe(0.15, 0xB0, 4, finite(rng))
        maybe(0.15, 0xB8, 4, finite(rng)); maybe(0.1, 0x9C, 4, finite(rng))
    elif name == 'wrap':
        result = rng.choice([bits(rng.uniform(-3.2, 3.2)), bits(1.5707964), bits(1.5707963),
                             bits(-1.5707964), bits(-1.5707962), 0, 0x80000000, 1, 0x80000001,
                             0x00400000, 0x80400000, 0x007FFFFF, 0x00800000, 0x80800000])
    elif name == 'approach':
        result = 'target' if rng.random() < 0.4 else bits(rng.uniform(-3.2, 3.2))
    elif name in ('fade', 'fade_end', 'camera'):
        maybe(0.2, 0xB4, 4, finite(rng))
    return result, writes, nodes, out, surface


# ------------------------------------------------------------- oracle ----

RAM = ELF = NATIVE = CAPTURED = None
NODE_ADDR = None
_EE = None


def oracle_ee():
    global _EE
    if _EE is None:
        _EE = ClosureEE(ELF, RAM)
    return _EE


def words(ee, address, count):
    return tuple(ee.load(address + 4 * i) for i in range(count))


def spad_words(ee):
    return tuple(ee.load(base + 4 * i) for base, n in SPAD for i in range(n))


def state_of(ee):
    return (bytes(ee.read(PLAYER, 0x320)), spad_words(ee), ee.load(G275B08), ee.load(G810702, 1),
            bytes(ee.read(SPAWN, 0x100)))


def install_hooks(ee, seed):
    counters = {}
    ee.snapshots = []

    def hook(name):
        def run(o):
            index = counters.get(name, 0); counters[name] = index + 1
            o.snapshots.append(state_of(o))
            a = [o.r[4 + i] & 0xFFFFFFFF for i in range(4)]
            f = o.f
            if name not in NO_ACTOR:
                assert a[0] == PLAYER, (name, hex(a[0]))
            if name in ('request', 'sound'):
                entry = (name, s32(a[1]), s32(a[2]), f[12])
            elif name in ('ledge_move', 'surface_sound', 'land_sound', 'floor', 'translate'):
                entry = (name, s32(a[1]))
            elif name in ('clip_FF80', 'clip_FC80', 'pose_reset'):
                entry = (name, f[12])
            elif name.startswith('clip_E') or name == 'clip_DFB0':
                entry = (name, s32(a[1]), f[12])
            elif name == 'arbiter':
                entry = (name, s32(a[1]), f[12], f[13])
            elif name == 'place':
                assert a[1] == PLAYER + 0xB0 and a[2] == PLAYER + 0xD0, 'place args'
                entry = (name,)
            elif name == 'sweep':
                entry = (name, words(o, a[1], 4), words(o, a[2], 4), a[3])
            elif name == 'ground':
                assert a[2] == PLAYER + 0x280, 'ground out'
                entry = (name, words(o, a[1], 4), a[3])
            elif name == 'point_test':
                entry = (name, words(o, a[1], 4), a[2])
            elif name == 'clip_frames':
                entry = (name, a[0], s32(a[1]))
            elif name in ('fade', 'fade_end'):
                entry = (name, s32(a[0]), s32(a[1]))
            elif name in ('camera', 'alloc'):
                entry = (name, s32(a[0]))
            elif name == 'trs':
                assert a == [PLAYER + 0xD0, PLAYER + 0xB0, PLAYER + 0xC0, PLAYER + 0x60], 'trs args'
                entry = (name, words(o, a[1], 3), words(o, a[2], 3), words(o, a[3], 3))
            elif name == 'transform':
                assert a[1] == PLAYER + 0xD0, 'transform matrix'
                entry = (name, words(o, a[1], 16), words(o, a[2], 4))
            elif name == 'vadd':
                entry = (name, words(o, a[1], 4), words(o, a[2], 4))
            elif name in ('sine', 'cosine', 'wrap', 'to_int'):
                entry = (name, f[12])
            elif name == 'approach':
                entry = (name, f[12], f[13], f[14])
            else:
                entry = (name,)
            o.log.append(entry)
            result, writes, node_writes, out, surface = effect(seed, name, index)
            for offset, size, value in writes:
                o.save(PLAYER + offset, value, size)
            for (node, offset), value in node_writes.items():
                o.save(NODE_ADDR[node] + offset, value)
            if out is not None:
                for i, value in enumerate(out): o.save(a[0] + 4 * i, value)
            if surface is not None:
                o.save(0x700031D0, HIT)
                o.save(HIT + 0x1A, surface, 1)
            if result == 'target':
                result = f[12]
            if name in ('sine', 'cosine', 'wrap', 'approach'):
                o.f[0] = result
            else:
                o.r[2] = sx32(result)
        return run
    ee.hooks = {address: hook(name) for address, name in CALLEES.items()}


class Native:
    """The same callees on the native side, one log, the same scripts."""

    def __init__(self, seed, scene, node_values, g275B08, g810702, spad, spawn, fault_at=None):
        self.seed, self.log, self.counters = seed, [], {}
        self.node_values = dict(node_values)
        self.fault_at = fault_at
        self.keep, self.snapshots = [], []
        self.live = None
        self.g275B08, self.g810702 = g275B08, g810702
        self.scratch = Scratch()
        flat = list(spad)
        for i in range(8): self.scratch.s3600[i] = flat[i]
        for i in range(16): self.scratch.s38A0[i] = flat[8 + i]
        self.scratch.s3A20 = flat[24]
        self.spawn = (C.c_uint8 * 0x100).from_buffer_copy(spawn)
        self.surface = None
        w = self.workers = Workers()
        w.scratch = C.pointer(self.scratch)

        def scene_fn(_, out):
            out[0] = scene; return 0

        def node_fn(_, node, offset, value):
            value[0] = self.node_values[(node, offset)]; return 0

        def hit_fn(_, out):
            assert self.surface is not None, 'hit read without a sweep hit'
            out[0] = self.surface; return 0

        def set32_fn(_, value):
            self.g275B08 = value & 0xFFFFFFFF; return 0

        def set8_fn(_, value):
            self.g810702 = value & 0xFF; return 0
        w.scene = self._keep(SCENE_FN(scene_fn))
        w.node = self._keep(NODE_FN(node_fn))
        w.hit_surface = self._keep(HIT_FN(hit_fn))
        w.set_275B08 = self._keep(SET32_FN(set32_fn))
        w.set_810702 = self._keep(SET8_FN(set8_fn))
        for field, kind in WORKER_FIELDS[7:]:
            setattr(w, field, self._keep(kind(self._make(field))))

    def _keep(self, fn):
        self.keep.append(fn); return fn

    def state(self):
        s = self.scratch
        spad = tuple(s.s3600) + tuple(s.s38A0) + (s.s3A20,)
        return (bytes(self.live.bytes), spad, self.g275B08, self.g810702, bytes(self.spawn))

    def _make(self, name):
        def vec(pointer, count): return tuple(pointer[i] for i in range(count))

        def done(entry, actor=None, out_int=None, out_vec=None, out_count=4, out_float=None,
                 out_bits=None, target=None):
            self.log.append(entry)
            self.snapshots.append(self.state())
            index = self.counters.get(name, 0); self.counters[name] = index + 1
            if self.fault_at == (name, index):
                return -1
            result, writes, node_writes, out, surface = effect(self.seed, name, index)
            raw = self.live.bytes
            for offset, size, value in writes:
                for i in range(size): raw[offset + i] = (value >> (8 * i)) & 0xFF
            self.node_values.update(node_writes)
            if surface is not None: self.surface = surface
            if result == 'target': result = target
            if out_int is not None: out_int[0] = s32(result)
            if out_vec is not None:
                for i in range(out_count): out_vec[i] = out[i]
            if out_float is not None: out_float[0] = number(result)
            if out_bits is not None: out_bits[0] = result
            if name == 'alloc':
                return ('alloc', result)
            return 0

        fb = bits
        if name in ('request', 'sound'):
            return lambda _, a, x, y, f: done((name, x, y, fb(f)))
        if name in ('ledge_move', 'floor'):
            return lambda _, a, arg, r: done((name, arg), out_int=r)
        if name in ('surface_sound', 'land_sound', 'translate'):
            return lambda _, a, arg: done((name, arg))
        if name in ('clip_FF80', 'clip_FC80', 'pose_reset'):
            return lambda _, a, f: done((name, fb(f)))
        if name.startswith('clip_E') or name == 'clip_DFB0':
            return lambda _, a, side, f: done((name, side, fb(f)))
        if name == 'arbiter':
            return lambda _, a, clip, b, fr: done((name, clip, fb(b), fb(fr)))
        if name in ('use_test', 'clip_row', 'clip_row_B', 'ledge_3D', 'ledge_3B'):
            return lambda _, a, r: done((name,), out_int=r)
        if name == 'sweep':
            return lambda _, a, p, q, mask, r: done((name, vec(p, 4), vec(q, 4), mask), out_int=r)
        if name in ('ground', 'point_test'):
            return lambda _, a, p, mask, r: done((name, vec(p, 4), mask), out_int=r)
        if name == 'random':
            return lambda _, r: done((name,), out_int=r)
        if name == 'clip_frames':
            return lambda _, bank, clip, r: done((name, bank, clip), out_int=r)
        if name == 'to_int':
            return lambda _, x, r: done((name, x), out_int=r)
        if name in ('fade', 'fade_end'):
            return lambda _, x, y: done((name, x, y))
        if name == 'camera':
            return lambda _, x: done((name, x))
        if name == 'alloc':
            def alloc(_, cls, node):
                got = done((name, cls))
                if got == -1: return -1
                node[0] = C.cast(self.spawn, C.POINTER(C.c_uint8)) if got[1] else C.POINTER(C.c_uint8)()
                return 0
            return alloc
        if name == 'trs':
            return lambda _, out, p, r, s: done((name, vec(p, 3), vec(r, 3), vec(s, 3)), out_vec=out,
                                                out_count=16)
        if name in ('transform', 'vadd'):
            return lambda _, m, v, out: done((name, vec(m, 16 if name == 'transform' else 4), vec(v, 4)),
                                             out_vec=out)
        if name in ('sine', 'cosine'):
            return lambda _, x, r: done((name, fb(x)), out_float=r)
        if name == 'wrap':
            return lambda _, x, r: done((name, x), out_bits=r)
        if name == 'approach':
            return lambda _, t, cur, rate, r: done((name, t, cur, rate), out_bits=r, target=t)
        return lambda _, a: done((name,))


def build_native():
    LANE_DIR.mkdir(parents=True, exist_ok=True)
    lib = LANE_DIR / ('closure.dylib' if sys.platform == 'darwin' else 'closure.so')
    cached_build(lib, ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                       '-shared', '-fPIC', '-Isrc', 'src/game/em_player_closure_0e_18.c',
                       'src/game/em_player_hang.c', 'src/game/em_player_major2.c',
                       'src/game/em_player_ladder_climb.c', 'src/game/em_player_ladder_entry.c',
                       'src/game/em_sdk_math_original.c', '-o', str(lib)])
    native = C.CDLL(str(lib))
    WP = C.POINTER(Workers)
    for state in ('0E', '13', '14', '18'):
        getattr(native, 'em_player_closure_state' + state).argtypes = [C.c_void_p, A]
    for name, extra in DIRECT_ARGS.items():
        getattr(native, 'em_player_closure_' + name).argtypes = [WP, A] + extra
    return native


# --------------------------------------------------------------- cases ----

ENTRIES = {'0E': 0x168050, '13': 0x16B790, '14': 0x16B8A0, '18': 0x16D130}
SUBSTATES = {
    '0E': (0, 1, 0xA, 0x14, 0x1E, 0x1F, 0x20, 0x21, 0x28, 0x29, 0x2A, 0x2B, 0x32, 0x3C, 0x46, 0x50,
           0x5A, 0x5B, 0x5C),
    '13': (0, 1, 2, 3),
    '14': (0, 1, 2, 0xA, 0xB, 0xC),
    '18': (0, 1, 2, 3, 4, 0xA, 0xB, 0xC, 0xD, 0x14, 0x15, 0x1E, 0x1F, 0x20, 0x28, 0x29, 0x2A, 0x2B,
           0x2C, 0x2D, 0x31),
}
# The exported private callees: name -> extra ctypes argument types (after w, a).
DIRECT_ARGS = {
    '00180000': [C.c_uint32], '00180040': [C.c_uint32], '00180080': [C.c_uint32],
    '001800C0': [C.c_uint32], '00180100': [C.c_int, C.c_uint32], '00180180': [C.c_int, C.c_uint32],
    '00180200': [C.c_int, C.c_uint32], '00180280': [C.c_int, C.c_uint32], '00180420': [],
    '00180300': [U32P, C.c_int, IP], '001806E0': [IP], '00180790': [IP], '00180850': [C.c_int, IP],
    '00182AB0': [], '00174AB0': [], '00178620': [C.c_int, IP], '00179150': [], '001790B0': [IP],
    '0016BAE0': [C.c_int],
}
THRESHOLDS = (6.0, 14.0, 18.0, 22.0, 25.0, 42.0, 64.0, 78.0)
FLOATS = (0x38, 0x3C, 0x9C, 0xB0, 0xB4, 0xB8, 0xBC, 0xC0, 0xC4, 0xC8, 0x204, 0x218, 0x21C, 0x224,
          0x22C, 0x254, 0x258, 0x26C, 0x280, 0x290, 0x294, 0x298, 0x2E0, 0x2E4, 0x2E8, 0x2EC, 0x2F4,
          0x2F8)


def near(rng, value):
    return rng.choice([bits(value), bits(value) + 1, bits(value) - 1, bits(rng.uniform(-10, 120))])


def random_case(seed):
    rng = random.Random(seed)
    kind = rng.choice(['0E'] * 6 + ['18'] * 5 + ['13', '14', '14', 'direct', 'direct'])
    raw = bytearray(CAPTURED) if rng.random() < 0.25 else bytearray(rng.randrange(256) for _ in range(0x320))
    raw[0x40:0x48] = CAPTURED[0x40:0x48]            # the node pointers D_00275B40 reads through
    for offset in FLOATS:
        struct.pack_into('<I', raw, offset, finite(rng))
    for i in range(16):
        struct.pack_into('<I', raw, 0xD0 + 4 * i, finite(rng))
    if kind != 'direct':
        raw[6] = rng.choice(SUBSTATES[kind] + SUBSTATES[kind] + (rng.randrange(256),))
    raw[7] = rng.choice([0, 1, 2, 2, 2, 3, 3, 4, 5, rng.randrange(256)])
    raw[0xD] = rng.choice([0, 1, 2, 3, 0, 1, 2, rng.randrange(256)])
    favourite = rng.choice([0x32, 0x3B, 0x33, 0x3D, 0x05])
    if raw[0xD] < 3 and rng.random() < 0.7:         # the surface 00180300 accepts for kind +D
        favourite = (0x32, 0x3B, 0x33)[raw[0xD]]
    if kind == '0E' and raw[6] in (0x32, 0x3C, 0x46, 0x50) and rng.random() < 0.5:
        raw[7] = 2 if raw[6] < 0x46 else 3
    mode = rng.choice([0, 1, 2, 3, None])
    if kind == '0E' and rng.random() < 0.7:         # the quadrant the sub-state continues on
        mode = {0xA: 0, 0x14: 1, 0x32: 2, 0x3C: 3, 0x46: 2, 0x50: 3}.get(raw[6], mode)
    if kind == '18' and raw[6] == 0x2A and rng.random() < 0.7:
        favourite = rng.choice([0x3D, 0x3B])         # 00178620's two ledge surfaces
        mode = raw[0x2F1] + 2                       # D_00275498[+2F1]: the Use test runs
    KNOBS[seed] = (favourite, mode)
    raw[0xF] = rng.choice([0] * 8 + [2, 0xFF, 0xFD])
    for offset in (0x224, 0x22C):
        struct.pack_into('<I', raw, offset, rng.choice([0] * 14 + [0x80000000, 1, 0x3F800000]))
    struct.pack_into('<I', raw, 0x200, rng.choice([0, 0x1000, 0x1000, 0x8000, 0x9000, rng.randrange(1 << 32)]))
    struct.pack_into('<I', raw, 0x3C, near(rng, rng.choice(THRESHOLDS)))
    struct.pack_into('<I', raw, 0x24C, rng.choice([0, 1, 2, 3, 0xFFFFFFFF, 5]))
    raw[0x23F] = rng.randrange(4)
    raw[0x2F1] = rng.choice([0, 1, 0, 1, 2] if kind == '0E' else [0, 1])
    raw[0x1F1] = rng.choice([0, 1, 3, 1, 3, 2, rng.randrange(256)])
    raw[0x23B] = rng.choice([0x1E, 0x1E, 0, 0x32, rng.randrange(256)])
    struct.pack_into('<H', raw, 0x28, rng.choice([0, 0, 1, 8, 0xFFFF, 0x8000]))
    if rng.random() < 0.3:                          # +218 near +C4 for 0016B8A0 / the corner turn
        struct.pack_into('<I', raw, 0x218, struct.unpack_from('<I', raw, 0xC4)[0])
    scene = Scene(rng.choice([0, 0x40, 0x4000, 0xFFFF]), rng.choice([0x40, 0x40, 0x10]),
                  rng.choice([0, 0x11, 5, 0]), rng.choice([2, 2, 2, 0, 1, 3, -1, 0x102, -0x8000]),
                  rng.choice([0x1E, 0x34, 0x34, 0x36, 0]), finite(rng), finite(rng), finite(rng))
    node_values = {key: finite(rng) for key in ((0, 4), (0, 8), (1, 0xC0), (1, 0xC4), (1, 0xC8), (1, 0xCC))}
    g275B08, g810702 = rng.randrange(1 << 32), rng.randrange(256)
    spad = tuple(finite(rng) for _ in range(25))
    spawn = bytes(rng.randrange(256) for _ in range(0x100))
    fault_at = None
    if rng.random() < 0.1:
        fault_at = (rng.choice(sorted(set(CALLEES.values()))), 0)
    direct = None
    if kind == 'direct':
        name = rng.choice(sorted(DIRECT_ARGS))
        direct = (name, rng.choice([0, 1, 0, 1, 2, rng.randrange(256)]), finite(rng),
                  tuple(finite(rng) for _ in range(4)))
    return kind, raw, scene, node_values, g275B08, g810702, spad, spawn, fault_at, direct


def prepare(ee, raw, scene, node_values, g275B08, g810702, spad, spawn):
    ee.reset(); ee.branches = set()
    ee.write(PLAYER, bytes(raw))
    for (node, offset), value in node_values.items():
        ee.save(NODE_ADDR[node] + offset, value)
    ee.save(0x810E74, scene.pad, 2); ee.save(0x70003B76, scene.use_mask, 2)
    ee.save(0x810700, scene.area, 1); ee.save(0x28A9A0, scene.fade & 0xFFFF, 2)
    ee.save(0x275B14, scene.d275B14 & 0xFFFFFFFF); ee.save(0x275B0C, scene.d275B0C)
    ee.save(0x275B10, scene.d275B10); ee.save(0x281B64, scene.d281B64)
    ee.save(G275B08, g275B08); ee.save(G810702, g810702, 1)
    flat = list(spad)
    for base, n in SPAD:
        for i in range(n): ee.save(base + 4 * i, flat.pop(0))
    ee.write(SPAWN, spawn)
    ee.save(0x700031D0, 0)


def run_direct(ee, side, live, direct):
    """One exported private callee on both sides; returns (v0, native result)."""
    name, arg, blend, vec = direct
    address = int(name, 16)
    w = C.byref(side.workers)
    ee.f[12] = blend
    result = C.c_int(-12345)
    if name in ('00180000', '00180040', '00180080', '001800C0'):
        ee.call(address, (PLAYER,)); got = getattr(NATIVE, 'em_player_closure_' + name)(w, live, blend)
        return None, got, None
    if name in ('00180100', '00180180', '00180200', '00180280'):
        ee.call(address, (PLAYER, arg)); got = getattr(NATIVE, 'em_player_closure_' + name)(w, live, arg, blend)
        return None, got, None
    if name in ('00180420', '00182AB0', '00174AB0', '00179150'):
        ee.call(address, (PLAYER,)); got = getattr(NATIVE, 'em_player_closure_' + name)(w, live)
        return None, got, None
    if name == '0016BAE0':
        ee.call(address, (PLAYER, arg)); got = NATIVE.em_player_closure_0016BAE0(w, live, arg)
        return None, got, None
    if name == '00180300':
        for i, value in enumerate(vec): ee.save(VEC + 4 * i, value)
        ee.call(address, (PLAYER, VEC, arg))
        got = NATIVE.em_player_closure_00180300(w, live, (C.c_uint32 * 4)(*vec), arg, C.byref(result))
    elif name == '00180850':
        ee.call(address, (PLAYER, arg))
        got = NATIVE.em_player_closure_00180850(w, live, arg, C.byref(result))
    elif name == '00178620':
        ee.call(address, (PLAYER, arg))
        got = NATIVE.em_player_closure_00178620(w, live, arg, C.byref(result))
    else:                                           # 001806E0, 00180790, 001790B0
        ee.call(address, (PLAYER,))
        got = getattr(NATIVE, 'em_player_closure_' + name)(w, live, C.byref(result))
    return s32(ee.r[2]), got, result.value


def run_case(seed):
    kind, raw, scene, node_values, g275B08, g810702, spad, spawn, fault_at, direct = random_case(seed)
    ee = oracle_ee()
    prepare(ee, raw, scene, node_values, g275B08, g810702, spad, spawn)
    install_hooks(ee, seed)
    side = Native(seed, scene, node_values, g275B08, g810702, spad, spawn, fault_at)
    live = LiveActor(); C.memmove(live.bytes, bytes(raw), 0x320)
    side.live = live
    want_v0 = got_result = None
    if kind == 'direct':
        want_v0, result, got_result = run_direct(ee, side, C.byref(live), direct)
    else:
        ee.call(ENTRIES[kind], (PLAYER,))
        result = getattr(NATIVE, 'em_player_closure_state' + kind)(C.addressof(side.workers), C.byref(live))
    want = state_of(ee)
    names = [e[0] for e in ee.log]
    tags = {('entry', kind if kind != 'direct' else direct[0])}
    if fault_at is not None and fault_at[0] in names:
        k = names.index(fault_at[0])
        assert result == -1, (seed, 'fault not propagated', fault_at)
        assert side.log == ee.log[:k + 1], (seed, 'fault log', side.log, ee.log[:k + 1])
        assert side.snapshots == ee.snapshots[:k + 1], (seed, 'fault: state at an earlier call')
        assert side.state() == ee.snapshots[k], (seed, 'fault state')
        tags.add('fault')
        return seed, kind, raw[6], tags, frozenset(ee.branches)
    assert result == 0, (seed, kind, 'native result', result)
    if want_v0 is not None:
        assert got_result == want_v0, (seed, direct[0], 'return value', got_result, want_v0)
    assert side.log == ee.log, (seed, kind, hex(raw[6]), 'callee sequence', side.log, ee.log)
    for k, (mine, theirs) in enumerate(zip(side.snapshots, ee.snapshots)):
        if mine != theirs:
            raise AssertionError((seed, kind, hex(raw[6]), 'state at call', k, ee.log[k][0], diff(mine, theirs)))
    got = side.state()
    if got != want:
        raise AssertionError((seed, kind, hex(raw[6]), 'final state differs', diff(got, want)))
    # Which original table entries the case read (the native embeds them).
    for e, snap in zip(ee.log, ee.snapshots):
        if e[0] == 'cosine' and kind == '0E' and raw[6] in (0x32, 0x3C):
            tags.add(('D_00248610', snap[0][0x23F])); tags.add(('D_00248620', snap[0][0x23F]))
    if kind == '18' and raw[6] == 0x2A and 'steer' in names:
        tags.add(('D_00275498', want[0][0x2F1]))    # no later worker here writes +2F1
    if 'arbiter' in names:
        tags.add(('D_002488AC', 0))
    if names.count('point_test') == 7:
        tags.add(('D_00248970', 'all'))
    if names.count('vadd') >= 4:                    # 00180850 ran its two-offset loop
        tags.add(('D_002754B0', 'used'))
    return seed, kind, raw[6], tags, frozenset(ee.branches)


def diff(mine, theirs):
    parts = ('actor', 'spad', 'D_00275B08', 'D_00810702', 'spawn')
    out = []
    for label, x, y in zip(parts, mine, theirs):
        if x == y: continue
        if isinstance(x, (bytes, tuple)):
            out.append((label, [hex(i) if isinstance(x, bytes) else i for i in range(len(x)) if x[i] != y[i]][:16]))
        else:
            out.append((label, hex(x), hex(y)))
    return out


def refusal_checks():
    """A missing worker (or scratch) refuses before any write; table indexes
    outside the original tables fault."""
    base = dict(scene=Scene(0, 0x40, 0x11, 2, 0x34, 0, 0, 0),
                node_values={k: 0 for k in ((0, 4), (0, 8), (1, 0xC0), (1, 0xC4), (1, 0xC8), (1, 0xCC))},
                g275B08=0, g810702=0, spad=(0,) * 25, spawn=bytes(0x100))
    raw = bytes(range(256)) * 3 + bytes(0x20)
    checks = 0
    for field, _ in WORKER_FIELDS[1:]:
        for state in ('0E', '13', '14', '18'):
            side = Native(1, **base)
            live = LiveActor(); C.memmove(live.bytes, raw, 0x320); side.live = live
            saved = getattr(side.workers, field)
            setattr(side.workers, field, type(saved)())
            fn = getattr(NATIVE, 'em_player_closure_state' + state)
            assert fn(C.addressof(side.workers), C.byref(live)) == -1, (field, state)
            assert bytes(live.bytes) == raw and not side.log, ('refusal wrote', field, state)
            checks += 1
        side = Native(1, **base)
        live = LiveActor(); C.memmove(live.bytes, raw, 0x320); side.live = live
        setattr(side.workers, field, type(getattr(side.workers, field))())
        assert NATIVE.em_player_closure_00180850(C.byref(side.workers), C.byref(live), 0,
                                                 C.byref(C.c_int())) == -1, field
        assert bytes(live.bytes) == raw and not side.log
        checks += 1
    for state in ('0E', '13', '14', '18'):
        assert getattr(NATIVE, 'em_player_closure_state' + state)(None, C.byref(live)) == -1
    # +2F1 = 2 at 0016D130 sub-state 0x2A (after the steering; outside D_00275498).
    bad = bytearray(raw); bad[6] = 0x2A; bad[0xF] = 0
    for offset in (0x224, 0x22C): struct.pack_into('<I', bad, offset, 0)
    side = Native(3, **base)
    live = LiveActor(); C.memmove(live.bytes, bytes(bad), 0x320); side.live = live
    side.workers.steer = side._keep(ACT_FN(lambda _, a: (a.contents.bytes.__setitem__(0x2F1, 2), 0)[1]))
    assert NATIVE.em_player_closure_state18(C.addressof(side.workers), C.byref(live)) == -1, '+2F1 = 2'
    # +23F = 4 at 00168050 sub-state 0x32 +7 2 with a reach result of 0.
    bad = bytearray(raw); bad[6] = 0x32; bad[7] = 2; bad[0xF] = 0; bad[0xD] = 0
    for offset in (0x224, 0x22C): struct.pack_into('<I', bad, offset, 0)
    side = Native(4, **base)
    live = LiveActor(); C.memmove(live.bytes, bytes(bad), 0x320); side.live = live

    def steer(_, a):
        a.contents.bytes[0x23F] = 4
        for i in range(4): a.contents.bytes[0x24C + i] = (2 >> (8 * i)) & 0xFF
        return 0
    side.workers.steer = side._keep(ACT_FN(steer))
    side.workers.sweep = side._keep(SWEEP_FN(lambda _, a, p, q, m, r: (r.__setitem__(0, 1), 0)[1]))
    side.surface = 0x32
    side.workers.hit_surface = side._keep(HIT_FN(lambda _, out: (out.__setitem__(0, 0x32), 0)[1]))
    assert NATIVE.em_player_closure_state0E(C.addressof(side.workers), C.byref(live)) == -1, '+23F = 4'
    return checks


def jal_targets(start, size):
    out = set()
    for pc in range(start, start + size, 4):
        word = struct.unpack_from('<I', RAM, pc)[0]
        if word >> 26 in (2, 3):
            out.add((pc & 0xF0000000) | ((word & 0x3FFFFFF) << 2))
    return out


def main():
    global RAM, ELF, NATIVE, CAPTURED, NODE_ADDR
    ELF = read_elf()
    if not RAM_PATH.exists():
        raise SystemExit('missing %s (the captured AREA11 RAM; docs/PLAYER_CLOSURE_0E_18.md)' % RAM_PATH)
    RAM = RAM_PATH.read_bytes()
    for address, size in list(TRANSLATED.items()) + list(RUN_UNHOOKED.items()) + list(TABLES):
        at = address - 0x100000 + 0x300
        assert RAM[address:address + size] == ELF[at:at + size], ('captured RAM differs from the ELF', hex(address))
    CAPTURED = RAM[PLAYER:PLAYER + 0x320]
    NODE_ADDR = {0: struct.unpack_from('<I', CAPTURED, 0x40)[0], 1: struct.unpack_from('<I', CAPTURED, 0x44)[0]}
    # Every jal/j target of the executed routines is translated, run unhooked or hooked.
    targets = set()
    for address, size in list(TRANSLATED.items()) + list(RUN_UNHOOKED.items()):
        targets |= jal_targets(address, size)
    executed = set(TRANSLATED) | set(RUN_UNHOOKED) | {0x180004}
    assert targets - executed == set(CALLEES), ('hooked set differs from the jal targets',
                                                sorted(hex(t) for t in (targets - executed) ^ set(CALLEES)))
    NATIVE = build_native()

    count = reference_mode.pick(400000, 16000)
    results = reference_mode.parallel_map(run_case, [0x168050 + 7919 * i for i in range(count)])
    branches, tags, per_state = set(), set(), {}
    for seed, kind, st, case_tags, case_branches in results:
        branches |= case_branches; tags |= case_tags
        per_state[(kind, st)] = per_state.get((kind, st), 0) + 1
    wanted = [(pc, taken) for address, size in TRANSLATED.items()
              for pc in conditional_branches(RAM, address, address + size) for taken in (False, True)]
    missing = [(hex(pc), taken) for pc, taken in wanted if (pc, taken) not in branches]
    assert not missing, ('branch outcomes never exercised', missing)
    for kind, states in SUBSTATES.items():
        for st in states:
            assert per_state.get((kind, st), 0) > 0, ('sub-state never run', kind, hex(st))
    for name in DIRECT_ARGS:
        assert ('entry', name) in tags, ('exported callee never run directly', name)
    for table, size in (('D_00248610', 4), ('D_00248620', 4), ('D_00275498', 2)):
        for index in range(size):
            assert (table, index) in tags, (table, 'entry never read', index)
    for tag in (('D_002488AC', 0), ('D_00248970', 'all'), ('D_002754B0', 'used')):
        assert tag in tags, ('table never read', tag)
    assert 'fault' in tags, 'no worker fault propagated'
    checks = refusal_checks()
    faults = sum(1 for r in results if 'fault' in r[3])
    reference_mode.banner(reference_mode.part(len(results), 400000, 'cases'))
    handled = sum(len(v) for v in SUBSTATES.values())
    others = len([k for k in per_state if k[0] in SUBSTATES and k[1] not in SUBSTATES[k[0]]])
    print('player closure 0E/13/14/18 reference: PASS -- %d cases (%d worker faults) over the %d handled '
          '(state, +6) pairs, %d default-path +6 values and %d exported callees: all 0x320 record bytes, '
          'the scratchpad words, '
          'D_00275B08, D_00810702 and the spawned node after the call and at every worker call, and '
          'every worker call with its arguments, identical; %d/%d conditional branch outcomes of the '
          '%d translated routines exercised; every embedded table entry read; %d missing-worker '
          'refusals; the hooked set equals the %d hooked jal/j targets'
          % (len(results), faults, handled, others, len(DIRECT_ARGS), len(wanted) - len(missing), len(wanted),
             len(TRANSLATED), checks, len(CALLEES)))


if __name__ == '__main__':
    main()
