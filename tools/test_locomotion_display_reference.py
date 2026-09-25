#!/usr/bin/env python3
"""Execute the original idle / walk states and gait display; compare
em_locomotion_display.c.

docs/LOCOMOTION_DISPLAY.md. The user's pinned ELF (and the captured AREA11
RAM) supplies every instruction and table; none are embedded here. Routines
executed unmodified and compared:

  00161020  idle state            001612D0  walk state
  0017C030  gait display / skid   0017B660  anim_matrix_player
  0017B5C0  walk entry blend      0017B490  clip-table selector
  0017B460  D_00248AB0 lookup     00179D20  node pose seed
  00179FF0  node world matrices   00182D40  +1F0 == 0x17
  001026D0  VU0 4x4 product       00103230  VU0 row scale

The display leaves (001029C0, 00102C58, quat_nlerp, quat_to_mat3,
build_trs_matrix, copy_qw4, 001C9D50 with its 001C9E40 and SDK sqrtf) run
unhooked on the original side; the native side calls the verified
translations the module links (em_owner_services_original, em_pose_host_workers,
em_anim_runtime_rest, em_sdk_math_original). So the comparison covers the
composite, not a model of it.

Every other callee is a worker. Unit cases hook it on the original side and
script its effect (return value, record and node writes), and the native
worker applies the same script; the call sequence and arguments are compared.
The captured-image cases instead run the animation workers (001749F0,
001749A0, 001C61D0, 001B0070) as ORIGINAL instructions on both sides (a
nested original call with every hook lifted; on the native side over the
native memory), so the real clip bank and node channels of the captured
player drive the display. In those cases the heading worker (00174AC0) is
bound as the binder will bind it: the original side runs 00174AC0 as
original code with its whole call tree, the native side runs
em_player_heading_record_worker_result over the native memory (the
captured pad bytes D_00810E57/64/65, camera yaw D_008106A0, 0x70003B8D and
the shared 0x70003A20 word). The test asserts that the hooked set is exactly the
set of jal targets of the routines, so no callee runs unhooked by accident.

Every case compares all 32 MB of RAM, the 16 KB scratchpad, the return value,
the native fault latch and the worker call sequence. Branch outcomes inside
the translated routines are recorded, and every conditional branch must have
been seen both ways (or be listed as impossible, with the reason).

Arithmetic: tools/ee_float_model.py through test_coll_move_reference.FloatEE
(COP1 and VU0 macro ops, raw bit patterns).

Default run (~10 s): unit cases with branch coverage, the leaf sweep, the
fail-stop checks and the playable image plus three route beats.
EM_TEST_FULL=1: the exhaustive random sweep and every route beat.
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
import test_player_slide_reference as SR  # noqa: E402
from test_coll_move_reference import FloatEE  # noqa: E402

DECOMP = SR.DECOMP
REFERENCE = DECOMP / 'build/startup-reference'
ROUTE = DECOMP / 'build/s87/route'
LANE = ROOT / 'build' / os.environ.get('EM_LANE', 'b7-locomotion-display')
MASK = 0xFFFFFFFF
RAM_SIZE = 0x2000000

# ---------------------------------------------------------------- routines

IDLE, WALK, DISPLAY, MATRIX, ENTRY = 0x161020, 0x1612D0, 0x17C030, 0x17B660, 0x17B5C0
SELECT, TABLE, SEED, WORLD, SCRIPTED = 0x17B490, 0x17B460, 0x179D20, 0x179FF0, 0x182D40
PRODUCT, ROWSCALE = 0x1026D0, 0x103230
SIZES = {IDLE: 0x2A8, WALK: 0x3BC, DISPLAY: 0x334, MATRIX: 0x2A8, ENTRY: 0x94, SELECT: 0x124,
         TABLE: 0x24, SEED: 0x2CC, WORLD: 0xC0, SCRIPTED: 0x24, PRODUCT: 0x44, ROWSCALE: 0x18}
TRANSLATED = set(SIZES)
# The display leaves: translated elsewhere, executed unhooked here.
LEAVES = {0x1029C0: 'identity', 0x102C58: 'euler', 0x1CA0A0: 'quat_nlerp', 0x1CA1C0: 'quat_to_mat3',
          0x1C94B0: 'build_trs_matrix', 0x102958: 'copy_qw4', 0x1C9D50: 'blend'}
# Original callee -> EmLocoWorkers field.
CALLEES = {0x1607D0: 'actions', 0x160220: 'ladder', 0x174AC0: 'heading', 0x174A50: 'row_request',
           0x1749A0: 'request', 0x1749F0: 'arbiter', 0x1C61D0: 'clip_frames', 0x1764E0: 'probes',
           0x175900: 'floor', 0x1756E0: 'clearance', 0x1796C0: 'fall_check', 0x17BC40: 'motor',
           0x178B90: 'translate', 0x184BA0: 'use_scan', 0x1798D0: 'use_accepted',
           0x17C540: 'handoff', 0x17C440: 'reentry', 0x17B910: 'foot_stop', 0x1FB9F0: 'sound',
           0x1EFD90: 'effect', 0x1B1470: 'wrap', 0x1B0070: 'mode'}
ADDRESS = {name: address for address, name in CALLEES.items()}
# Run as original instructions (both sides) in the captured-image cases;
# 'heading' runs as original code on the original side and as the
# record-level em_player_heading_record on the native side.
ORIGINAL_IN_WORLD = ('arbiter', 'request', 'clip_frames', 'mode', 'heading')

D_B40, D_28A9A0, D_810E74, D_MODE = 0x275B40, 0x28A9A0, 0x810E74, 0x26C5D0
D_AB0, D_740, D_870, D_7F40, D_8D40 = 0x248AB0, 0x248740, 0x248870, 0x287F40, 0x288D40
PLAYER = 0x8102B0
ACTOR = 0x01E00000          # unit cases: zero RAM in the ELF image (checked)
NODE_TABLE = 0x01E10000
NODES = 0x01E20000

ELF = None
LIB = None
SDK_TABLES = None
BASE = None                 # the ELF-only RAM image
IMAGES = {}                 # label -> (ram, spad)


def F(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def fnum(word):
    return struct.unpack('<f', struct.pack('<I', word & MASK))[0]


def u32(buf, at):
    return struct.unpack_from('<I', buf, at)[0]


def sx32(value):
    value &= MASK
    return value - (1 << 32) if value & 0x80000000 else value


def in_translated(pc):
    return any(start <= pc < start + size for start, size in SIZES.items())


# ---------------------------------------------------------------- the oracle

class OracleEE(FloatEE):
    """FloatEE recording executed addresses and the conditional-branch
    outcomes inside the translated routines."""

    def __init__(self, elf, ram=None, spad=None):
        super().__init__(elf, ram, spad)
        self.pcs = set()
        self.outcomes = set()

    def branch(self, word, pc):
        b = super().branch(word, pc)
        if b is not None and in_translated(pc):
            self.outcomes.add((pc, bool(b[0])))
        return b

    def execute(self, word, pc):
        self.pcs.add(pc)
        return super().execute(word, pc)


def side_ee(ram, spad):
    """An interpreter over the given bytearrays (no copy), no hooks."""
    ee = FloatEE(ELF, ram=b'', spad=b'')
    ee.mem, ee.spad = ram, spad
    return ee


def check_callee_set():
    ee = FloatEE(ELF)
    targets = set()
    for start, size in SIZES.items():
        for pc in range(start, start + size, 4):
            word = ee.load(pc)
            if word >> 26 == 3:
                targets.add((word & 0x3FFFFFF) << 2)
    unknown = sorted(t for t in targets if t not in CALLEES and t not in TRANSLATED and t not in LEAVES)
    assert not unknown, ('callees neither hooked, translated nor a display leaf', [hex(t) for t in unknown])
    unused = sorted(t for t in list(CALLEES) + list(LEAVES) if t not in targets)
    assert not unused, ('listed callees no routine calls', [hex(t) for t in unused])
    return len(targets)


def conditional_branches():
    """Every conditional branch address inside the translated routines."""
    ee = FloatEE(ELF)
    out = []
    for start, size in SIZES.items():
        for pc in range(start, start + size, 4):
            word = ee.load(pc)
            op = word >> 26
            if op == 4 and (word >> 16 & 0x3FF) == 0:
                continue                       # the unconditional branch (both registers zero)
            if op in (1, 4, 5, 6, 7, 20, 21, 22, 23) or (op == 17 and (word >> 21 & 31) == 8):
                out.append(pc)
    return out


# ---------------------------------------------------------------- native side

U8, U32, I32 = C.c_uint8, C.c_uint32, C.c_int32
PU32, PI = C.POINTER(U32), C.POINTER(C.c_int)
VP = C.c_void_p
I, FLT = C.c_int, C.c_float


class Region(C.Structure):
    _fields_ = [('address', U32), ('size', U32), ('bytes', VP), ('writable', C.c_int)]


class PoseGlobals(C.Structure):
    _fields_ = [('d275BF8', U32), ('d275BF4', U32), ('d275BF0', U32), ('d275BEC', U32),
                ('d8111F0', U8 * 0x6C), ('d8106F3', VP), ('spad3400', PU32), ('spad3440', PU32),
                ('spad3600', PU32), ('spad3760', PU32), ('spad3A3C', PU32), ('spad38B0', PU32),
                ('spad3A20', PU32), ('column', VP)]


class PoseCallees(C.Structure):
    _fields_ = [('context', VP), ('column', VP), ('ledge_hit', VP), ('atan2', VP),
                ('advance_context', VP), ('advance', VP)]


class PoseHost(C.Structure):
    _fields_ = [('region', Region * 12), ('region_count', C.c_uint), ('globals', C.POINTER(PoseGlobals)),
                ('callees', PoseCallees), ('events', C.c_int16 * 512)]


class RestWorld(C.Structure):
    _fields_ = [('region', Region * 12), ('region_count', C.c_uint), ('channel', VP), ('channel_count', U32),
                ('scratch', VP), ('spad34C0', PU32), ('spad34D0', PU32), ('spad34E0', PU32),
                ('spad3760', PU32), ('d275B40', PU32), ('d275B48', PU32)]


SQRT_FN = C.CFUNCTYPE(C.c_int, VP, U32, PU32)


class RestWorkers(C.Structure):
    _fields_ = [('sqrt_ctx', VP), ('w_0011E748', SQRT_FN), ('ctx', VP), ('w_001D88B0', VP),
                ('w_001CB760', VP), ('w_001CABA0', VP)]


class RestFault(C.Structure):
    _fields_ = [('address', U32), ('code', I32)]


class Rest(C.Structure):
    _fields_ = [('world', RestWorld), ('workers', RestWorkers), ('fault', RestFault)]


class SdkContext(C.Structure):
    _fields_ = [('tables', VP), ('d26C5D0', VP), ('wctx', VP), ('w0', VP), ('w1', VP), ('w2', VP),
                ('w3', VP), ('fault', U32)]


FN = {
    'test': C.CFUNCTYPE(I, VP, VP, PI),
    'heading': C.CFUNCTYPE(I, VP, VP, I, PI),
    'blend': C.CFUNCTYPE(I, VP, VP, FLT),
    'request': C.CFUNCTYPE(I, VP, VP, I, I, FLT),
    'arbiter': C.CFUNCTYPE(I, VP, VP, I, FLT, FLT),
    'frames': C.CFUNCTYPE(I, VP, U32, I, C.POINTER(I32)),
    'probes': C.CFUNCTYPE(I, VP, VP, U32),
    'floor': C.CFUNCTYPE(I, VP, VP, I, PI),
    'actor': C.CFUNCTYPE(I, VP, VP),
    'actor_int': C.CFUNCTYPE(I, VP, VP, I),
    'sound': C.CFUNCTYPE(I, VP, I, I, I, I),
    'effect': C.CFUNCTYPE(I, VP, U32, VP),
    'wrap': C.CFUNCTYPE(I, VP, U32, PU32),
    'mode': C.CFUNCTYPE(I, VP, C.POINTER(I32)),
}
# EmLocoWorkers, in header order: (field, FN kind).
WORKER_FIELDS = (
    ('actions', 'test'), ('ladder', 'test'), ('heading', 'heading'), ('row_request', 'blend'),
    ('request', 'request'), ('arbiter', 'arbiter'), ('clip_frames', 'frames'), ('probes', 'probes'),
    ('floor', 'floor'), ('clearance', 'test'), ('fall_check', 'actor'), ('motor', 'actor'),
    ('translate', 'actor_int'), ('use_scan', 'floor'), ('use_accepted', 'actor'), ('handoff', 'actor'),
    ('reentry', 'actor_int'), ('foot_stop', 'actor'), ('sound', 'sound'), ('effect', 'effect'),
    ('wrap', 'wrap'), ('mode', 'mode'),
)


class Workers(C.Structure):
    _fields_ = [('context', VP)] + [(name, FN[kind]) for name, kind in WORKER_FIELDS]


class Scene(C.Structure):
    _fields_ = [('d28A9A0', VP), ('d810E74', VP), ('spad3B76', VP), ('caller_s1', VP)]


class Display(C.Structure):
    _fields_ = [('pose', C.POINTER(PoseHost)), ('rest', C.POINTER(Rest)), ('d275B40', PU32)]


class Host(C.Structure):
    _fields_ = [('workers', Workers), ('scene', Scene), ('display', Display), ('fault', U32)]


SOURCES = ('src/game/em_locomotion_display.c', 'src/game/em_pose_host_workers.c',
           'src/game/em_player_stage_workers.c', 'src/game/em_player_floor.c',
           'src/game/em_player_reaction.c', 'src/game/em_player_fall.c',
           'src/game/em_owner_services_original.c',
           'src/game/em_stream_lanes_original.c', 'src/game/em_anim_runtime_rest.c',
           'src/game/em_sdk_math_original.c', 'src/game/em_player_heading_record.c',
           'src/game/em_script_host_workers.c', 'src/game/em_script.c')


def build_native():
    LANE.mkdir(parents=True, exist_ok=True)
    lib = LANE / ('locomotion_display' + ('.dylib' if sys.platform == 'darwin' else '.so'))
    command = ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
               '-shared', '-fPIC', '-Isrc', *SOURCES, '-lm', '-o', str(lib)]
    digest = hashlib.sha256(' '.join(command).encode())
    for path in [ROOT / s for s in SOURCES] + sorted((ROOT / 'src').rglob('*.h')):
        digest.update(path.read_bytes())
    stamp = Path(str(lib) + '.sha256')
    if not (lib.exists() and stamp.exists() and stamp.read_text() == digest.hexdigest()):
        subprocess.run(command, cwd=ROOT, check=True)
        stamp.write_text(digest.hexdigest())
    n = C.CDLL(str(lib))
    H = C.POINTER(Host)
    for name in ('00161020', '001612D0', '0017C030', '0017B660', '0017B5C0', '00179D20', '00179FF0'):
        fn = getattr(n, 'em_loco_' + name)
        fn.argtypes, fn.restype = [H if name not in ('00161020', '001612D0') else VP, VP], C.c_int
    n.em_loco_0017B490.argtypes = [H, VP, I, I, I, C.POINTER(C.c_int16)]
    n.em_loco_0017B460.argtypes = [C.POINTER(PoseHost), I, I, C.POINTER(C.c_int16)]
    n.em_loco_00182D40.argtypes = [VP]
    n.em_loco_bound.argtypes = [H]
    n.em_loco_001026D0.argtypes = [PU32, PU32, PU32]
    n.em_loco_00103230.argtypes = [PU32, PU32, U32]
    n.em_anim_rest_sqrt_0011E748.argtypes = [VP, U32, PU32]
    n.em_sdk_math_original_load_tables.argtypes = [C.c_char_p, C.c_size_t, VP]
    n.em_player_heading_record_worker_result.argtypes = [VP, VP, C.c_int, C.POINTER(C.c_int)]
    return n


class HeadingWorld(C.Structure):
    """EmPlayerHeadingRecordWorld, then EmPlayerHeadingRecord (the layout
    test_player_heading_record_reference.py checks against the C)."""
    _fields_ = [('spad3B8D', VP), ('d810E57', VP), ('d810E64', VP), ('d810E65', VP),
                ('d8106A0', VP), ('spad3A20', VP), ('sdk_tables', VP), ('sdk_world', VP),
                ('sdk_workers', VP)]


class HeadingRecord(C.Structure):
    _fields_ = [('world', HeadingWorld), ('fault_address', U32)]


class HeadingSdkWorld(C.Structure):
    _fields_ = [('d26C5D0', VP)]


def addr(fn):
    return C.cast(fn, VP).value


# ---------------------------------------------------------------- scripts

FLAGS = (0, 0x1000, 0x8000, 0x9000, 0x200, 0x1200)


def unit_quat(rng):
    q = [rng.gauss(0, 1) for _ in range(4)]
    n = math.sqrt(sum(v * v for v in q)) or 1.0
    return [F(v / n) for v in q]


def effect_for(rng, name, actor, nodes):
    """A hooked worker's effect in this case: its return value (or f0), and
    the memory it writes (EE address, size, value)."""
    e = {'ret': 0, 'fret': None, 'writes': []}
    w = e['writes']
    chance = rng.random

    def put(off, size, value):
        w.append((actor + off, size, value))
    if name in ('actions', 'ladder'):
        e['ret'] = rng.choice((0, 0, 0, 0, 0, 0, 0, 1, 5))
    elif name == 'use_scan':
        e['ret'] = rng.choice((0, 0, 1))
    elif name == 'heading':
        e['ret'] = rng.choice((0, 0, 1, 7))
        if chance() < 0.5: put(0x23F, 1, rng.randrange(5))
        if chance() < 0.5: put(0x240, 4, F(rng.choice((0.0, 0.0, -0.0, 0.1, 0.3, 0.8))))
        if chance() < 0.3: put(0x1F0, 1, rng.choice((0, 1, 6, 7)))
        if chance() < 0.2: put(0xC4, 4, F(rng.uniform(-3.2, 3.2)))
    elif name == 'clip_frames':
        e['ret'] = rng.choice((25, 40, 1, 0, 120, -7, 0x1000001, 61, 30))
    elif name == 'mode':
        e['ret'] = rng.choice((0, 4, 0xFF, 3, -4, 0))
    elif name == 'wrap':
        e['fret'] = F(rng.uniform(-3.1415, 3.1415))
    elif name in ('arbiter', 'request', 'row_request'):
        if chance() < 0.3: put(0x200, 4, rng.choice(FLAGS))
        if chance() < 0.3: put(0x20C, 2, rng.randrange(0x60))
        if chance() < 0.3: put(0x3C, 4, F(rng.choice((0.0, 3.0, 12.5, 40.0))))
        if chance() < 0.2: put(0x208, 4, F(rng.choice((0.0, 0.5, 1.0, 0.999))))
        if name != 'row_request' and nodes:
            for node in nodes:
                if chance() < 0.6:
                    for off, q in ((0x30, unit_quat(rng)), (0x40, unit_quat(rng))):
                        for k in range(4): w.append((node + off + 4 * k, 4, q[k]))
                    w.append((node + 0x50, 4, F(rng.choice((0.0, 0.5, 1.0, rng.random())))))
                if chance() < 0.3:
                    for k in range(3): w.append((node + 0x70 + 4 * k, 4, F(rng.uniform(-3.2, 3.2))))
    elif name == 'floor':
        e['ret'] = rng.choice((0, 1, 0x81))
        if chance() < 0.3: put(0x1F0, 1, rng.choice((0, 1, 3, 6, 7)))
        if chance() < 0.2: put(0xB4, 4, F(rng.uniform(-50, 50)))
    elif name in ('probes', 'clearance', 'fall_check', 'motor', 'translate', 'handoff', 'reentry',
                  'foot_stop', 'use_accepted'):
        if name == 'clearance': e['ret'] = rng.choice((0, 1))
        if chance() < 0.3: put(0x1F0, 1, rng.choice((0, 1, 3, 4, 5, 6, 7)))
        if chance() < 0.2: put(0x200, 4, rng.choice(FLAGS))
        if chance() < 0.2: put(0xB4, 4, F(rng.uniform(-50, 50)))
        if chance() < 0.2: put(0x38, 4, F(rng.choice((0.0, 0.1, 0.8))))
        if chance() < 0.15: put(0x25C, 1, rng.randrange(4))
        if chance() < 0.15: put(0x1F1, 1, rng.randrange(5))
        if chance() < 0.15: put(0x28, 2, rng.choice((0, 1, 8, 0xFFFF)))
        if chance() < 0.15: put(0x23B, 1, rng.choice((0x35, 5)))
    return e


class Script:
    def __init__(self, seed, actor, nodes):
        self.seed, self.count, self.actor, self.nodes = seed, 0, actor, nodes

    def next(self, name):
        rng = random.Random('%d:%d:%s' % (self.seed, self.count, name))
        self.count += 1
        return effect_for(rng, name, self.actor, self.nodes)


# ---------------------------------------------------------------- case images

def put(ram, at, size, value):
    ram[at:at + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'little')


def unit_image(rng, entry, rigid):
    """The ELF image with a synthetic player record and skeleton."""
    ram = bytearray(BASE)
    spad = bytearray(rng.getrandbits(8) for _ in range(0x4000))
    count = rng.choice((1, 1, 2, 3, 4))
    nodes = [NODES + 0x100 * i for i in range(count)]
    put(ram, D_B40, 4, NODE_TABLE)
    for i, node in enumerate(nodes):
        put(ram, NODE_TABLE + 4 * i, 4, node)
        ram[node:node + 0xD0] = bytes(rng.getrandbits(8) for _ in range(0xD0))
        for k in range(3): put(ram, node + 4 * k, 4, F(rng.uniform(-5, 5)))
        for k in range(3):
            put(ram, node + 0x18 + 4 * k, 4, F(1.0 if rigid else rng.choice((1.0, 0.5, 1.5, -1.0, 2.0))))
        for off in (0x30, 0x40):
            q = unit_quat(rng)
            for k in range(4): put(ram, node + off + 4 * k, 4, q[k])
        put(ram, node + 0x50, 4, F(rng.choice((0.0, 0.25, 0.5, 1.0, 1.2, rng.random()))))
        parent = -1 if i == 0 else rng.choice((-1, rng.randrange(i), rng.randrange(i),
                                               rng.randrange(count)))
        put(ram, node + 0x64, 2, parent)
        for k in range(3): put(ram, node + 0x70 + 4 * k, 4, F(rng.uniform(-3.2, 3.2)))
        for k in range(3): put(ram, node + 0x7C + 4 * k, 4, F(rng.uniform(-50, 50)))
        for k in range(3):
            put(ram, node + 0x88 + 2 * k, 2,
                0x1000 if rigid else rng.choice((0x1000, 0x800, 0x1800, rng.randrange(-0x2000, 0x2000))))
    a = ACTOR
    ram[a:a + 0x320] = bytes(rng.getrandbits(8) for _ in range(0x320))
    put(ram, a + 0xC, 1, count)
    put(ram, a + 4, 1, 1)
    state = {IDLE: (0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 0x63, 0x64, 0x64, 5),
             WALK: (0, 1, 1, 2, 2, 2, 2, 0x63, 0x63, 3)}.get(entry, (rng.randrange(256),))
    put(ram, a + 6, 1, rng.choice(state))
    put(ram, a + 7, 1, rng.choice((0, 0, 1, 1, 2)))
    put(ram, a + 0x28, 2, rng.choice((0, 0, 0, 1, 8, 16, 0x12C, 0xFFFF, 7)))
    put(ram, a + 0x200, 4, rng.choice(FLAGS))
    put(ram, a + 0x236, 1, rng.choice((0, 0, 0, 1)))
    put(ram, a + 0x235, 1, rng.choice((0, 1, 2, 3, 0)))
    put(ram, a + 0x23F, 1, rng.randrange(5))
    put(ram, a + 0x240, 4, F(rng.choice((0.0, 0.0, -0.0, 0.1, 0.3, 0.8))))
    put(ram, a + 0x25D, 1, rng.choice((0, 0, 1)))
    put(ram, a + 0x1F0, 1, rng.choice({WALK: (0, 1, 1, 3, 5, 6, 6, 7, 7),
                                       DISPLAY: (0, 1, 1, 2, 3, 3, 4, 5, 5, 6, 6, 7, 8, 0x17)}.get(
        entry, (0, 1, 1, 2, 3, 4, 5, 6, 7, 8, 0x17))))
    put(ram, a + 0x1F1, 1, rng.choice((0, 1, 2, 3, 4)))
    put(ram, a + 0x25C, 1, rng.choice((0, 1, 1, 2, 3, 3)))
    put(ram, a + 0x23A, 1, rng.choice((5, 6, 0, 0x5D)))
    put(ram, a + 0x23B, 1, rng.choice((0x35, 5, 5)))
    put(ram, a + 0x23C, 1, rng.choice((0, 0, 1)))
    put(ram, a + 0x23D, 1, rng.choice((0, 0, 1)))
    put(ram, a + 0x20C, 2, rng.randrange(0x60))
    if rng.random() < 0.3:   # the clip 0017B490(p, 1, +235, +25C) selects without the override
        row, tier = ram[a + 0x235], ram[a + 0x25C]
        cell = u32(BASE, D_AB0 + 4) + 2 * ((tier + 4 * row) & 0xFFFF)
        put(ram, a + 0x20C, 2, u32(BASE, cell) & 0xFFFF)
    put(ram, a + 0x3C, 4, F(rng.choice((0.0, 3.0, 12.5, 25.0, 40.0))))
    put(ram, a + 0x208, 4, F(rng.choice((0.0, 0.25, 0.5, 0.999, 1.0, 1.5))))
    put(ram, a + 0x268, 4, F(rng.choice((0.0, 0.5, 0.99999994, 1.0, 1.0000001, 5.0))))
    for off in (0x260, 0x264, 0xB0, 0xB4, 0xB8):
        put(ram, a + off, 4, F(rng.uniform(-300, 300)))
    for off in (0xC0, 0xC4, 0xC8):
        put(ram, a + off, 4, F(rng.uniform(-3.2, 3.2)))
    for off in (0x60, 0x64, 0x68):
        put(ram, a + off, 4, F(rng.choice((1.0, 1.0, 0.5, 2.0))))
    put(ram, D_28A9A0, 2, rng.choice((0, 0, 0, 0, 0, 0, 0, 1)))
    put(ram, D_810E74, 2, rng.choice((0x20, 0x40, 0)))
    put(spad, 0x3B76, 2, rng.choice((0, 0x20, 0x60, 0xFFFF)))
    return ram, spad, nodes


# ---------------------------------------------------------------- one case

class Oracle:
    """The original routines on an OracleEE; `original` names the workers
    that run as original instructions (captured-image cases)."""

    def __init__(self, ram, spad, script, original=()):
        self.ee = OracleEE(ELF, ram=ram, spad=spad)
        self.script, self.log = script, []
        for address, name in CALLEES.items():
            self.ee.hooks[address] = self.nested(address, name) if name in original else self.hook(name)

    def entry(self, name, ee):
        r = lambda n: ee.r[4 + n] & MASK
        f = lambda n: ee.f[12 + n] & MASK
        if name in ('actions', 'ladder', 'clearance', 'fall_check', 'motor', 'use_accepted', 'handoff',
                    'foot_stop'):
            return (name, r(0))
        if name in ('heading', 'floor', 'translate', 'reentry', 'use_scan'):
            return (name, r(0), r(1))
        if name == 'row_request':
            return (name, r(0), f(0))
        if name == 'request':
            return (name, r(0), r(1), r(2), f(0))
        if name == 'arbiter':
            return (name, r(0), r(1), f(0), f(1))
        if name == 'clip_frames':
            return (name, r(0), r(1))
        if name == 'probes':
            return (name, r(0), ee.r[17] & MASK)
        if name == 'sound':
            return (name, r(0), r(1), r(2), r(3))
        if name == 'effect':
            return (name, r(0), r(1), r(2))
        if name == 'wrap':
            return (name, f(0))
        return (name,)

    def hook(self, name):
        def run(ee):
            self.log.append(self.entry(name, ee))
            e = self.script.next(name)
            for at, size, value in e['writes']:
                ee.save(at, value, size)
            if e['fret'] is not None:
                ee.f[0] = e['fret']
            ee.r[2] = sx32(e['ret'])
        return run

    def nested(self, address, name):
        def run(ee):
            self.log.append(self.entry(name, ee))
            hooks, ee.hooks = ee.hooks, {}
            ints = tuple(ee.r[4 + n] & MASK for n in range(4))
            floats = tuple(ee.f[12 + n] & MASK for n in range(2))
            v0, f0 = ee.invoke(address, ints, floats)
            ee.hooks = hooks
            ee.r[2], ee.f[0] = sx32(v0), f0
        return run


class Native:
    """em_locomotion_display over a copy of one memory image."""

    def __init__(self, ram, spad, script, actor, s1, original=()):
        self.ram, self.spad = bytearray(ram), bytearray(spad)
        self.rbuf = (U8 * len(self.ram)).from_buffer(self.ram)
        self.sbuf = (U8 * len(self.spad)).from_buffer(self.spad)
        self.base, self.sbase = C.addressof(self.rbuf), C.addressof(self.sbuf)
        self.script, self.actor, self.log = script, actor, []
        self.original = set(original)
        g = self.g = PoseGlobals()
        for name, at in (('spad3400', 0x3400), ('spad3440', 0x3440), ('spad3600', 0x3600),
                         ('spad3760', 0x3760), ('spad3A3C', 0x3A3C), ('spad38B0', 0x38B0),
                         ('spad3A20', 0x3A20)):
            setattr(g, name, C.cast(self.sbase + at, PU32))
        g.d8106F3 = self.base + 0x8106F3
        p = self.pose = PoseHost()
        p.region[0] = Region(0, RAM_SIZE, self.base, 1)
        p.region_count = 1
        p.globals = C.pointer(g)
        r = self.rest = Rest()
        r.world.region[0] = Region(0, RAM_SIZE, self.base, 1)
        r.world.region_count = 1
        for name, at in (('spad34C0', 0x34C0), ('spad34D0', 0x34D0), ('spad34E0', 0x34E0),
                         ('spad3760', 0x3760)):
            setattr(r.world, name, C.cast(self.sbase + at, PU32))
        r.world.d275B40 = C.cast(self.base + D_B40, PU32)
        r.world.d275B48 = C.cast(self.base + 0x275B48, PU32)
        self.sdk = SdkContext(C.addressof(SDK_TABLES), self.base + D_MODE)
        r.workers.sqrt_ctx = C.addressof(self.sdk)
        r.workers.w_0011E748 = SQRT_FN(addr(LIB.em_anim_rest_sqrt_0011E748))
        self.s1 = U8(s1)
        h = self.host = Host()
        self.keep = []
        self.fns = {}
        for name, kind in WORKER_FIELDS:
            fn = self.fns[name] = FN[kind](getattr(self, 'w_' + kind)(name))
            setattr(h.workers, name, fn)
        h.scene = Scene(self.base + D_28A9A0, self.base + D_810E74, self.sbase + 0x3B76,
                        C.addressof(self.s1))
        h.display = Display(C.pointer(p), C.pointer(r), C.cast(self.base + D_B40, PU32))

    def ptr(self, address):
        return self.base + address

    def ee_addr(self, pointer):
        return (pointer - self.base) & MASK

    # A scripted worker applies the next effect; an original one runs the
    # original instructions over the native memory.
    def scripted(self, name):
        e = self.script.next(name)
        for at, size, value in e['writes']:
            self.ram[at:at + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'little')
        return e

    def run_original(self, name, ints=(), floats=()):
        return side_ee(self.ram, self.spad).invoke(ADDRESS[name], ints, floats)

    def w_test(self, name):
        def run(_, actor, result):
            self.log.append((name, self.ee_addr(actor)))
            result[0] = sx32(self.scripted(name)['ret'])
            return 0
        return run

    def w_heading(self, name):
        def run(_, actor, arg, result):
            self.log.append((name, self.ee_addr(actor), arg & MASK))
            if name == 'heading' and name in self.original:
                return self.bound_heading(actor, arg, result)
            result[0] = sx32(self.scripted(name)['ret'])
            return 0
        return run

    def bound_heading(self, actor, arg, result):
        """00174AC0 as the binder binds it: em_player_heading_record over
        this image's globals and its 0x70003A20 word."""
        if getattr(self, 'heading', None) is None:
            w = HeadingWorld(self.sbase + 0x3B8D, self.base + 0x810E57, self.base + 0x810E64,
                             self.base + 0x810E65, self.base + 0x8106A0, self.sbase + 0x3A20,
                             C.addressof(SDK_TABLES), None, None)
            self.heading_sdk = HeadingSdkWorld(self.base + D_MODE)
            w.sdk_world = C.addressof(self.heading_sdk)
            self.heading = HeadingRecord(w, 0)
        # A fault returns -1 through the worker, so the routine stops and the
        # case fails on its status (an exception here would be swallowed).
        return LIB.em_player_heading_record_worker_result(C.addressof(self.heading), actor, arg,
                                                          result)

    def w_floor(self, name):
        return self.w_heading(name)

    def w_blend(self, name):
        def run(_, actor, blend):
            self.log.append((name, self.ee_addr(actor), F(blend)))
            self.scripted(name)
            return 0
        return run

    def w_request(self, name):
        def run(_, actor, clip, flags, blend):
            entry = (name, self.ee_addr(actor), clip & MASK, flags & MASK, F(blend))
            self.log.append(entry)
            if name in self.original:
                self.run_original(name, entry[1:4], (entry[4],))
            else:
                self.scripted(name)
            return 0
        return run

    def w_arbiter(self, name):
        def run(_, actor, clip, blend, frame):
            entry = (name, self.ee_addr(actor), clip & MASK, F(blend), F(frame))
            self.log.append(entry)
            if name in self.original:
                self.run_original(name, entry[1:3], entry[3:5])
            else:
                self.scripted(name)
            return 0
        return run

    def w_frames(self, name):
        def run(_, bank, clip, frames):
            entry = (name, bank & MASK, clip & MASK)
            self.log.append(entry)
            if name in self.original:
                v0, _f0 = self.run_original(name, entry[1:3])
                frames[0] = sx32(v0)
            else:
                frames[0] = sx32(self.scripted(name)['ret'])
            return 0
        return run

    def w_probes(self, name):
        def run(_, actor, s1):
            self.log.append((name, self.ee_addr(actor), s1 & MASK))
            self.scripted(name)
            return 0
        return run

    def w_actor(self, name):
        def run(_, actor):
            self.log.append((name, self.ee_addr(actor)))
            self.scripted(name)
            return 0
        return run

    def w_actor_int(self, name):
        def run(_, actor, arg):
            self.log.append((name, self.ee_addr(actor), arg & MASK))
            self.scripted(name)
            return 0
        return run

    def w_sound(self, name):
        def run(_, a0, a1, a2, a3):
            self.log.append((name, a0 & MASK, a1 & MASK, a2 & MASK, a3 & MASK))
            self.scripted(name)
            return 0
        return run

    def w_effect(self, name):
        def run(_, ident, actor):
            at = self.ee_addr(actor)
            self.log.append((name, ident & MASK, (at + 0xB0) & MASK, (at + 0xC0) & MASK))
            self.scripted(name)
            return 0
        return run

    def w_wrap(self, name):
        def run(_, x, out):
            self.log.append((name, x & MASK))
            out[0] = self.scripted(name)['fret']
            return 0
        return run

    def w_mode(self, name):
        def run(_, value):
            self.log.append((name,))
            if name in self.original:
                v0, _f0 = self.run_original(name)
                value[0] = sx32(v0)
            else:
                value[0] = sx32(self.scripted(name)['ret'])
            return 0
        return run


def first_diff(a, b):
    if len(a) != len(b):
        return ('length', len(a), len(b))
    if a == b:
        return None
    step = 1 << 16
    for block in range(0, len(a), step):
        if a[block:block + step] != b[block:block + step]:
            for i in range(block, min(len(a), block + step)):
                if a[i] != b[i]:
                    return hex(i), a[i:i + 16].hex(), b[i:i + 16].hex()
    return None


ENTRY_NAMES = {IDLE: '00161020', WALK: '001612D0', DISPLAY: '0017C030', MATRIX: '0017B660',
               ENTRY: '0017B5C0', SEED: '00179D20', WORLD: '00179FF0', SELECT: '0017B490',
               TABLE: '0017B460', SCRIPTED: '00182D40'}


def run_case(case):
    """One case on both sides; returns (outcomes, pcs) or raises."""
    ram, spad = make_image(case['image'])
    actor, entry, s1 = case['actor'], case['entry'], case['s1']
    original = case.get('original', ())
    oracle = Oracle(ram, spad, Script(case['seed'], actor, case['nodes']), original)
    native = Native(ram, spad, Script(case['seed'], actor, case['nodes']), actor, s1, original)
    ee = oracle.ee
    ee.r[17] = s1
    args = case.get('args', ())
    if entry in (SELECT,):
        v0, _ = ee.invoke(entry, (actor,) + tuple(args))
        clip = C.c_int16()
        status = LIB.em_loco_0017B490(C.byref(native.host), native.ptr(actor), *args, C.byref(clip))
        want, got = v0 & MASK, clip.value & MASK
    elif entry == TABLE:
        v0, _ = ee.invoke(entry, tuple(args))
        value = C.c_int16()
        status = LIB.em_loco_0017B460(C.byref(native.pose), *args, C.byref(value))
        want, got = v0 & MASK, value.value & MASK
    elif entry == SCRIPTED:
        v0, _ = ee.invoke(entry, (actor,))
        status, want, got = 0, v0 & MASK, LIB.em_loco_00182D40(native.ptr(actor)) & MASK
    else:
        ee.invoke(entry, (actor,))
        fn = getattr(LIB, 'em_loco_' + ENTRY_NAMES[entry])
        host = C.addressof(native.host) if entry in (IDLE, WALK) else C.byref(native.host)
        status = fn(host, native.ptr(actor))
        want = got = 0
    label = case['label']
    assert status == 0, (label, 'native fault', status, hex(native.host.fault), oracle.log[-3:])
    assert native.host.fault == 0, (label, 'fault latched', hex(native.host.fault))
    assert want == got, (label, 'return value', hex(want), hex(got))
    assert oracle.log == native.log, (label, 'worker calls', first_log_diff(oracle.log, native.log))
    if ee.spad != native.spad:
        raise AssertionError((label, 'scratchpad', first_diff(ee.spad, native.spad)))
    if ee.mem != native.ram:
        raise AssertionError((label, 'RAM', first_diff(ee.mem, native.ram)))
    headings = sum(1 for e in native.log if e[0] == 'heading') if 'heading' in original else 0
    return ee.outcomes, None, len(oracle.log), headings


def first_log_diff(a, b):
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return i, [tuple(hex(v) if isinstance(v, int) else v for v in x) for x in a[i:i + 2]], \
                [tuple(hex(v) if isinstance(v, int) else v for v in y) for y in b[i:i + 2]]
    return 'length', len(a), len(b), a[len(b):len(b) + 2], b[len(a):len(a) + 2]


def run_case_safe(case):
    try:
        outcomes, pcs, calls, headings = run_case(case)
        return ('ok', outcomes, pcs, calls, headings)
    except AssertionError as error:
        return ('fail', case['label'], repr(error)[:1500])


# ---------------------------------------------------------------- case lists

UNIT_ENTRIES = ((IDLE, 14), (WALK, 16), (DISPLAY, 12), (MATRIX, 6), (ENTRY, 3), (SEED, 3),
                (WORLD, 3), (SELECT, 4), (TABLE, 2), (SCRIPTED, 1))


def unit_cases(count, seed):
    rng = random.Random(seed)
    weighted = [e for e, w in UNIT_ENTRIES for _ in range(w)]
    cases = []
    for index in range(count):
        entry = weighted[index % len(weighted)] if index < len(weighted) else rng.choice(weighted)
        case_seed = rng.getrandbits(32)
        rigid = entry not in (SEED, WORLD) or rng.random() < 0.3
        args = ()
        if entry == SELECT:
            args = (rng.randrange(7), rng.choice((0, 1, 2, 3, rng.randrange(8))),
                    rng.choice((0, 1, 2, 3, -1, 4)))
        elif entry == TABLE:
            args = (rng.randrange(7), rng.randrange(24))

        nodes = unit_image_nodes(case_seed, entry, rigid)
        cases.append(dict(image=('unit', case_seed, entry, rigid), actor=ACTOR, entry=entry,
                          seed=case_seed, nodes=nodes,
                          s1=random.Random(case_seed ^ 0x5A).choice((1, 1, 4, 5, 0, 0x84)), args=args,
                          label=('unit', ENTRY_NAMES[entry], index, case_seed)))
    return cases


def unit_image_nodes(case_seed, entry, rigid):
    rng = random.Random(case_seed)
    # unit_image draws the spad first, then the count: replay the same draws.
    for _ in range(0x4000): rng.getrandbits(8)
    count = rng.choice((1, 1, 2, 3, 4))
    return [NODES + 0x100 * i for i in range(count)]


def make_image(spec):
    """(ram, spad) for a case: ('unit', seed, entry, rigid) or
    ('world', label, patch)."""
    if spec[0] == 'unit':
        ram, spad, _ = unit_image(random.Random(spec[1]), spec[2], spec[3])
        return ram, spad
    ram, spad = IMAGES[spec[1]]
    ram = bytearray(ram)
    put(ram, D_B40, 4, PLAYER + 0x110)   # anim_bone_array_setup(player) before its callback
    for at, size, value in spec[2]:
        put(ram, at, size, value)
    return ram, bytearray(spad)


def world_cases(label, rng):
    """The captured player: the display with the animation workers original."""
    ram = IMAGES[label][0]
    count = ram[PLAYER + 0xC]
    nodes = [u32(ram, PLAYER + 0x110 + 4 * i) for i in range(count)]
    cases = []

    def case(entry, patch=(), tag='', s1=1):
        p = [(PLAYER + off, size, value) for off, size, value in patch]
        cases.append(dict(image=('world', label, p), actor=PLAYER, entry=entry,
                          seed=rng.getrandbits(32), nodes=nodes, s1=s1, original=ORIGINAL_IN_WORLD,
                          label=('world', label, ENTRY_NAMES[entry], tag, len(cases))))
    case(SEED, tag='seed')
    case(WORLD, tag='world')
    case(ENTRY, tag='entry')
    case(ENTRY, [(0x235, 1, 1)], tag='entry_row1')
    for f1, tier, row, blend in ((1, 1, 0, 0.5), (2, 2, 0, 0.25), (1, 2, 1, 0.999), (2, 3, 0, 1.0),
                                 (1, 0, 0, 0.0)):
        case(MATRIX, [(0x1F1, 1, f1), (0x25C, 1, tier), (0x235, 1, row), (0x208, 4, F(blend)),
                      (0x3C, 4, F(7.5))], tag='matrix_%d_%d_%d' % (f1, tier, row))
    case(MATRIX, [(0x1F1, 1, 0), (0x25C, 1, 1), (0x3C, 4, F(10.0))], tag='matrix_same_tier')
    case(MATRIX, [(0x1F1, 1, 0), (0x25C, 1, 2), (0x20C, 2, 0), (0x3C, 4, F(10.0))], tag='matrix_retier')
    case(DISPLAY, [(0x1F0, 1, 1), (0x1F1, 1, 1), (0x25C, 1, 1), (0x208, 4, F(0.5))], tag='display1')
    case(DISPLAY, [(0x1F0, 1, 3), (0x25C, 1, 3)], tag='display3')
    case(DISPLAY, [(0x1F0, 1, 7), (0x1F1, 1, 3)], tag='display7')
    case(DISPLAY, [(0x1F0, 1, 6), (0x200, 4, 0x1000), (0x1F1, 1, 4)], tag='display6')
    case(IDLE, [(0x6, 1, 0)], tag='idle0')
    case(IDLE, [(0x6, 1, 1), (0x7, 1, 0), (0x28, 2, 0)], tag='idle1')
    case(WALK, [(0x6, 1, 2), (0x1F0, 1, 1), (0x240, 4, F(0.3)), (0x23F, 1, 2), (0x1F1, 1, 1)],
         tag='walk2_resume', s1=1)
    case(WALK, [(0x6, 1, 1), (0x1F0, 1, 1), (0x1F1, 1, 1), (0x25C, 1, 1), (0x208, 4, F(0.25))],
         tag='walk1_display')
    # The bound 00174AC0 with the stick held (the pad block's gait byte and
    # stick bytes set, the record walking with +5 = 1): the moving and the
    # standing turn, and the reversal gate that stores 0x70003A20.
    gait, stick_x, stick_y = 0x810E57 - PLAYER, 0x810E64 - PLAYER, 0x810E65 - PLAYER
    for tag, sub, g, x, y, speed in (('heading_walk1_g3', 1, 3, 0x80, 0x00, 0.8),
                                     ('heading_walk1_g1', 1, 1, 0xFF, 0x80, 0.0),
                                     ('heading_walk2_g2', 2, 2, 0x00, 0xFF, 0.3)):
        case(WALK, [(0x5, 1, 1), (0x6, 1, sub), (0x1F0, 1, 1), (0x1F1, 1, 1), (0x38, 4, F(speed)),
                    (gait, 1, g), (stick_x, 1, x), (stick_y, 1, y)], tag=tag)
    return cases


QUICK_BEAT_TAGS = ('seed', 'world', 'entry', 'matrix_1_1_0', 'matrix_2_3_0', 'display1', 'walk2_resume',
                   'walk1_display', 'heading_walk1_g3', 'heading_walk1_g1', 'heading_walk2_g2')


def image_list():
    images = [('playable_ee', REFERENCE / 'playable_ee.bin', None)]
    quick = ('00_panel_no_battery', '05_boxes', '08_truck_crossing')
    if ROUTE.exists():
        for beat in sorted(p for p in ROUTE.iterdir() if p.name[:2].isdigit()):
            if (beat / 'eeMemory.bin').exists() and (reference_mode.FULL or beat.name in quick):
                images.append((beat.name, beat / 'eeMemory.bin', beat / 'scratchpad.bin'))
    if not reference_mode.FULL:
        assert len(images) == 1 + len(quick), ('route captures missing', images)
    missing = [str(p) for _, p, _ in images if not p.exists()]
    assert not missing, ('captured RAM missing', missing)
    return images


# ---------------------------------------------------------------- leaf sweep

SPECIALS = (0x00000000, 0x80000000, 0x3F800000, 0xBF800000, 0x7F7FFFFF, 0xFF7FFFFF, 0x7F800000,
            0xFF800000, 0x7FC00000, 0x00000001, 0x00800000, 0x807FFFFF, 0x3F7FFFFF)


def leaf_words(rng, count):
    return [rng.choice(SPECIALS) if rng.random() < 0.15 else F(rng.uniform(-1e3, 1e3)) for _ in range(count)]


def check_leaves(rng, count):
    ee = FloatEE(ELF)
    src_a, src_b, dst = 0x01F00000, 0x01F00100, 0x01F00200
    for index in range(count):
        a, b = leaf_words(rng, 16), leaf_words(rng, 16)
        ee.write(src_a, struct.pack('<16I', *a))
        ee.write(src_b, struct.pack('<16I', *b))
        alias = index % 3
        target = (dst, src_a, src_b)[alias]
        ee.invoke(PRODUCT, (target, src_a, src_b))
        want = list(struct.unpack('<16I', ee.read(target, 64)))
        A, B = (U32 * 16)(*a), (U32 * 16)(*b)
        out = (A, B)[alias - 1] if alias else (U32 * 16)()
        assert LIB.em_loco_001026D0(out, A, B) == 0, ('001026D0 refused', index)
        assert list(out) == want, ('001026D0', index, alias, [hex(v) for v in want], [hex(v) for v in out])
        v, s = leaf_words(rng, 4), leaf_words(rng, 1)[0]
        ee.write(src_a, struct.pack('<4I', *v))
        ee.invoke(ROWSCALE, (dst, src_a), (s,))
        want = list(struct.unpack('<4I', ee.read(dst, 16)))
        V, O = (U32 * 4)(*v), (U32 * 4)()
        assert LIB.em_loco_00103230(O, V, s) == 0, ('00103230 refused', index)
        assert list(O) == want, ('00103230', index, [hex(x) for x in want], [hex(x) for x in O])
    return count


# ---------------------------------------------------------------- fail-stop

def check_fail_stop(rng):
    """Every entry with each worker, scene pointer and display view unbound
    in turn: -1 and nothing written; plus the shared-scratch identity and a
    latched 001C9D50 fault."""
    checks = 0
    entries = ('00161020', '001612D0', '0017C030', '0017B660', '0017B5C0', '00179D20', '00179FF0')
    fields = [('workers', n) for n, _ in WORKER_FIELDS] + [('scene', n) for n, _ in Scene._fields_] + \
             [('display', n) for n, _ in Display._fields_]
    ram, spad, nodes = unit_image(random.Random(7), WALK, True)
    for name in entries:
        n = Native(ram, spad, Script(1, ACTOR, nodes), ACTOR, 1)
        before, bspad = bytes(n.ram), bytes(n.spad)
        host = C.addressof(n.host) if name in ('00161020', '001612D0') else C.byref(n.host)
        for part, field in fields + [('spad3760', None), ('rest_fault', None), ('sqrt', None)]:
            if part == 'spad3760':
                saved = C.cast(C.cast(n.rest.world.spad3760, VP).value, PU32)
                n.rest.world.spad3760 = C.cast(n.sbase + 0x3764, PU32)
                restore = lambda: setattr(n.rest.world, 'spad3760', saved)
            elif part == 'rest_fault':
                n.rest.fault.code = 2
                restore = lambda: setattr(n.rest.fault, 'code', 0)
            elif part == 'sqrt':
                saved = SQRT_FN(addr(LIB.em_anim_rest_sqrt_0011E748))
                n.rest.workers.w_0011E748 = SQRT_FN()
                restore = lambda: setattr(n.rest.workers, 'w_0011E748', saved)
            else:
                holder = getattr(n.host, part)
                kind = dict(holder._fields_)[field]
                # A pointer field read back is a view of the structure in
                # ctypes (it would read NULL after the store): keep the
                # worker object / the address instead.
                if part == 'workers':
                    saved = n.fns[field]
                elif kind is VP:
                    saved = getattr(holder, field)
                else:
                    saved = C.cast(C.cast(getattr(holder, field), VP).value, kind)
                setattr(holder, field, None if kind is VP else kind())
                restore = lambda holder=holder, field=field, saved=saved: setattr(holder, field, saved)
            n.host.fault = 0
            status = getattr(LIB, 'em_loco_' + name)(host, n.ptr(ACTOR))
            restore()
            assert status == -1, ('fail-stop', name, part, field, status)
            assert n.host.fault != 0, ('fail-stop without a fault address', name, part, field)
            assert not n.log, ('a worker ran before the fault', name, part, field, n.log)
            assert n.ram == before and n.spad == bspad, ('a write before the fault', name, part, field)
            checks += 1
        n.host.fault = 0
        assert LIB.em_loco_bound(C.byref(n.host)) == 1, ('restored host not bound', name)
    # A node pointer outside every region faults before the first write.
    n = Native(ram, spad, Script(1, ACTOR, nodes), ACTOR, 1)
    n.pose.region[0].size = NODES
    before = bytes(n.ram)
    assert LIB.em_loco_00179D20(C.byref(n.host), n.ptr(ACTOR)) == -1
    assert bytes(n.ram) == before and n.host.fault == 0x00179D20, hex(n.host.fault)
    checks += 1
    # More nodes than the pose buffers hold.
    n = Native(ram, spad, Script(1, ACTOR, nodes), ACTOR, 1)
    n.ram[ACTOR + 0xC], n.ram[ACTOR + 0x1F1] = 57, 1
    before = bytes(n.ram)
    assert LIB.em_loco_0017B660(C.byref(n.host), n.ptr(ACTOR)) == -1 and bytes(n.ram) == before
    checks += 1
    # 0017B490 with no case (cmd >= 7): the mode worker runs, then the fault.
    n = Native(ram, spad, Script(1, ACTOR, nodes), ACTOR, 1)
    clip = C.c_int16(0x55)
    assert LIB.em_loco_0017B490(C.byref(n.host), n.ptr(ACTOR), 7, 0, 0, C.byref(clip)) == -1
    assert clip.value == 0x55 and n.log == [('mode',)], n.log
    checks += 1
    return checks


# ---------------------------------------------------------------- coverage

# Conditional-branch outcomes no input reaches, with the reason.
IMPOSSIBLE = {
    # 0017B490's jump-table range check taken (cmd >= 7): every caller passes
    # 1..6, and the original then returns its caller's $s0. The native side
    # faults there instead (check_fail_stop), so no comparison is possible.
    (0x17B4C4, True): '0017B490 cmd >= 7 (no case)',
}


def check_branch_coverage(outcomes):
    missing = []
    for pc in conditional_branches():
        for taken in (True, False):
            if (pc, taken) not in outcomes and (pc, taken) not in IMPOSSIBLE:
                missing.append((hex(pc), taken))
    return missing


# ---------------------------------------------------------------- main

def main():
    global ELF, LIB, SDK_TABLES, BASE
    started = time.time()
    ELF = SR.read_elf()
    LIB = build_native()
    SDK_TABLES = (C.c_uint64 * 1024)()
    assert LIB.em_sdk_math_original_load_tables(ELF, len(ELF), SDK_TABLES) == 0
    targets = check_callee_set()
    BASE = bytes(FloatEE(ELF).mem)
    for at, size in ((ACTOR, 0x400), (NODE_TABLE, 0x100), (NODES, 0x1000)):
        assert BASE[at:at + size] == bytes(size), ('unit scratch RAM not zero', hex(at))
    for label, path, spad_path in image_list():
        ram = path.read_bytes()
        spad = spad_path.read_bytes() if spad_path else bytes(0x4000)
        for start, size in SIZES.items():
            off = start - 0x100000 + 0x300
            assert ram[start:start + size] == ELF[off:off + size], ('captured code differs', label, hex(start))
        IMAGES[label] = (ram, spad)

    leaves = check_leaves(random.Random(0x1026), reference_mode.pick(3000, 300))
    fail_stop = check_fail_stop(random.Random(3))

    all_unit = unit_cases(6000, 0x10C0)
    rng = random.Random(0x3A11)
    world = []
    for label in IMAGES:
        cases = world_cases(label, rng)
        if not reference_mode.FULL and label != 'playable_ee':
            cases = [c for c in cases if c['label'][3] in QUICK_BEAT_TAGS]
        world += cases
    # Quick mode: a covering sample first, then further unit cases of the
    # same fixed list in batches until every conditional branch has been
    # seen both ways (deterministic: the list and the order are fixed).
    selected = reference_mode.select(all_unit, 400, 0x5E1, axes=(lambda c: c['entry'],))
    chosen = {id(c) for c in selected}
    rest = [c for c in all_unit if id(c) not in chosen]
    results = reference_mode.parallel_map(run_case_safe, selected + world,
                                          cost=lambda c: 50 if c['label'][0] == 'world' else 1)
    unit_results, world_results = results[:len(selected)], results[len(selected):]
    while True:
        failures = [r for r in unit_results + world_results if r[0] == 'fail']
        if failures:
            for f in failures[:6]:
                print('FAIL', f[1], f[2])
            print('%d of %d cases failed' % (len(failures), len(unit_results) + len(world_results)))
            return 1
        outcomes = set()
        for r in unit_results + world_results:
            outcomes |= r[1]
        missing = check_branch_coverage(outcomes)
        if not missing or not rest:
            break
        batch, rest = rest[:400], rest[400:]
        selected += batch
        unit_results += reference_mode.parallel_map(run_case_safe, batch)
    assert not missing, ('branch outcomes never reached', missing[:20], len(missing))
    calls = sum(r[3] for r in unit_results)
    world_calls = sum(r[3] for r in world_results)
    bound_headings = sum(r[4] for r in world_results)
    assert bound_headings > 0, 'no captured-image case reached the bound 00174AC0'
    reference_mode.banner(
        reference_mode.part(len(selected), len(all_unit), 'unit cases'),
        '%d captured-image cases over %d images' % (len(world), len(IMAGES)),
        '%d leaf cases' % leaves, '%d fail-stop checks' % fail_stop)
    print('locomotion display reference: %d routines executed, %d callees checked, %d worker calls '
          'compared (%d in captured images, %d of them the record-level 00174AC0 bound as the '
          'heading worker against the original 00174AC0), every conditional branch both ways; '
          '%.1f s' % (len(SIZES), targets, calls + world_calls, world_calls, bound_headings,
                      time.time() - started))
    return 0


if __name__ == '__main__':
    sys.exit(main())
