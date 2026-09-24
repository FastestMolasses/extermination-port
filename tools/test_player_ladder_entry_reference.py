#!/usr/bin/env python3
"""Execute the original Use surface actions and ladder entry, and compare
em_player_ladder_entry.c.

docs/PLAYER_LADDER_ENTRY.md. The user's pinned ELF (and, for the world mode,
the captured route RAM) supplies every instruction and table; none are
embedded here. Routines executed unmodified:

  0015D4C0  the surface-attribute actions   00176F90  the attribute refresh
  00177030  the facing gate / placement     00180300  the attribute probe
  00199DB0  record centre                   00199FA0  record corners
  00165B60  state 0xB, the ladder entry     00176DC0  the radial wall probes
  0017FC80  clip by +2F1                    00182A70  climb step sound
  001B61C0  pad vibration request           00102948 / 001031E0 (copies)
  001885D0 / 001885F0  the D_002754D0 / D_002754D4 rows 0017FC80 reads

0017FC80 (with 001885D0 / 001885F0) is translated once, in
em_player_ladder_climb.c; em_player_ladder_entry.c runs that translation
over its own request worker, so this oracle checks the bridge and the owner
together.

Every other callee is hooked, scripted per case and recorded (never
simulated as a claim about the callee); the native module gets the same
script through its workers. The test asserts that the hooked set is exactly
the set of jal targets of these routines, so no callee runs unhooked.

Arithmetic: LadderEE is the fall oracle's FallEE (every COP1 op and VU0
macro op through tools/ee_float_model.py, the measured model); the shared
interpreter file is not edited.

Default run (~10 s): unit cases with branch coverage of every conditional
branch in the translated routines asserted, the fail-stop cuts and the
missing-worker refusals. EM_TEST_FULL=1: the exhaustive sweep.
EM_TEST_WORLD=1: route beat 10 (both cage ladder entries) replayed over the
captured RAM, the original stage against the stage with these translations
hooked in at their original addresses, compared byte for byte (player
record and every scratch word these routines own, every frame) and against
the PCSX2 trace rows.
"""
import ctypes as C
import os
import random
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode  # noqa: E402
import ee_float_model as M  # noqa: E402
import test_player_slide_reference as shared  # noqa: E402
from test_player_slide_reference import EE, read_elf, s32, sx32  # noqa: E402
from test_player_fall_reference import FallEE  # noqa: E402

MASK = 0xFFFFFFFF
LANE = os.environ.get('EM_LANE', 'b6-player-ladder-entry')
OUT = ROOT / 'build' / LANE


def F(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


# ======================================================================
# The interpreter
# ======================================================================

class LadderEE(FallEE):
    """FallEE that records every conditional branch outcome inside the
    translated routines and any data read below 0x1000 (a record field
    read through a zero 0x700031D0)."""

    def __init__(self, elf, ram=None, spad=None, cover=False):
        super().__init__(elf, ram, spad)
        self.outcomes = set() if cover else None
        self.low_read = False

    def load(self, address, size=4):
        if (address & MASK) < 0x1000:
            self.low_read = True
        return EE.load(self, address, size)

    def branch(self, word, pc):
        b = super().branch(word, pc)
        if b is not None and self.outcomes is not None and in_translated(pc):
            self.outcomes.add((pc, b[0]))
        return b


# ======================================================================
# The routines and their callees
# ======================================================================

USE, REFRESH, FACING, CLASSIFY = 0x15D4C0, 0x176F90, 0x177030, 0x180300
CENTER, CORNERS, STATE_B, WALLS = 0x199DB0, 0x199FA0, 0x165B60, 0x176DC0
CLIPS, STEP, RUMBLE, QCOPY, COPY3 = 0x17FC80, 0x182A70, 0x1B61C0, 0x102948, 0x1031E0
SIZES = {USE: 0x9F8, REFRESH: 0x9C, FACING: 0x430, CLASSIFY: 0x120, CENTER: 0x1EC,
         CORNERS: 0x1DC, STATE_B: 0x770, WALLS: 0x1CC, CLIPS: 0x74, STEP: 0x3C, RUMBLE: 0x84,
         QCOPY: 0xC, COPY3: 0x1C, 0x1885D0: 0x1C, 0x1885F0: 0x1C}
TRANSLATED = set(SIZES)


def in_translated(pc):
    return any(start <= pc < start + size for start, size in SIZES.items())


CALLEES = {
    0x19BA80: 'probe', 0x19AD00: 'move', 0x19A570: 'segment', 0x19AFE0: 'sweep',
    0x19BC40: 'column', 0x1C94B0: 'trs', 0x1026A0: 'apply', 0x1028B8: 'vadd',
    0x1029C0: 'identity', 0x102C58: 'euler', 0x102918: 'translate', 0x102BB0: 'rotate_y',
    0x102738: 'dot', 0x102760: 'normalize', 0x11E620: 'atan2', 0x1B1470: 'wrap',
    0x11DF78: 'fabs', 0x11DE90: 'cos', 0x11E748: 'sqrt', 0x1749A0: 'request',
    0x1FBD50: 'sound', 0x179B90: 'sound_base',
    0x1762E0: 'wall', 0x111018: 'actuator',
}


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


# ======================================================================
# Native side (ctypes)
# ======================================================================

class LiveActor(C.Structure):
    _fields_ = [('bytes', C.c_uint8 * 0x320), ('link_owner', C.c_void_p), ('link_prev', C.c_void_p),
                ('link_flags', C.c_uint8), ('link_type', C.c_uint8)]


U32 = C.c_uint32
COLUMN_MAX = 32


class Scratch(C.Structure):
    _fields_ = [('s38A0', U32 * 4), ('s38B0', U32 * 4), ('s38C0', U32 * 4), ('s38D0', U32 * 4),
                ('s3A20', U32 * 4), ('s3600', U32 * 4), ('s3610', U32 * 4), ('s36A0', U32 * 16),
                ('s31B0', U32 * 3), ('record', C.c_int32), ('record_word', U32),
                ('record_bytes', C.c_uint8 * 0x40), ('entity', U32), ('entity_0E', C.c_uint16),
                ('s31D8', C.c_int32), ('s31E0', C.c_int32), ('s30F0', U32 * COLUMN_MAX),
                ('s3170', C.c_uint16 * COLUMN_MAX), ('fault', U32)]


# The scratch arrays by original address (the words these routines own and
# the column block); the record block is handled separately.
OWNED = (('s38A0', 0x700038A0, 4), ('s38B0', 0x700038B0, 4), ('s38C0', 0x700038C0, 4),
         ('s38D0', 0x700038D0, 4), ('s3A20', 0x70003A20, 4), ('s3600', 0x70003600, 4),
         ('s3610', 0x70003610, 4), ('s36A0', 0x700036A0, 16))
FIELD_AT = {getattr(Scratch, name).offset: (name, address, n) for name, address, n in OWNED}


class World(C.Structure):
    _fields_ = [('directory', C.POINTER(C.c_uint8)), ('directory_size', U32),
                ('verts', C.POINTER(U32)), ('vert_count', U32), ('area', C.c_uint8),
                ('d810C7C', C.c_uint8), ('d810C7D', C.c_uint8), ('d2754D0', C.c_int16)]


P = C.POINTER
LA = P(LiveActor)
I, FLT, VP = C.c_int, C.c_float, C.c_void_p
PU = P(U32)
FN = {
    'probe': C.CFUNCTYPE(I, VP, LA, PU, PU, I, P(I)),
    'move': C.CFUNCTYPE(I, VP, LA, PU, I, P(I)),
    'segment': C.CFUNCTYPE(I, VP, PU, PU, I, I, P(I)),
    'sweep': C.CFUNCTYPE(I, VP, LA, PU, PU, I, P(I)),
    'column': C.CFUNCTYPE(I, VP, PU),
    'trs': C.CFUNCTYPE(I, VP, PU, PU, PU, PU),
    'apply': C.CFUNCTYPE(I, VP, PU, PU, PU),
    'vadd': C.CFUNCTYPE(I, VP, PU, PU, PU),
    'identity': C.CFUNCTYPE(I, VP, PU),
    'euler': C.CFUNCTYPE(I, VP, PU, PU, PU),
    'translate': C.CFUNCTYPE(I, VP, PU, PU, PU),
    'rotate_y': C.CFUNCTYPE(I, VP, PU, PU, U32),
    'dot': C.CFUNCTYPE(I, VP, PU, PU, PU),
    'normalize': C.CFUNCTYPE(I, VP, PU, PU),
    'atan2': C.CFUNCTYPE(I, VP, U32, U32, PU),
    'scalar': C.CFUNCTYPE(I, VP, U32, PU),
    'request': C.CFUNCTYPE(I, VP, LA, I, I, FLT),
    'sound': C.CFUNCTYPE(I, VP, LA, I),
    'actor_result': C.CFUNCTYPE(I, VP, LA, P(I)),
    'node': C.CFUNCTYPE(I, VP, PU),
}
# EmPlayerLadderWorkers after context/scratch/world, in header order.
WORKER_FIELDS = (
    ('probe_0019BA80', 'probe', 'probe'), ('move_0019AD00', 'move', 'move'),
    ('segment_0019A570', 'segment', 'segment'), ('sweep_0019AFE0', 'sweep', 'sweep'),
    ('column_0019BC40', 'column', 'column'), ('trs', 'trs', 'trs'), ('apply', 'apply', 'apply'),
    ('vadd', 'vadd', 'vadd'), ('identity', 'identity', 'identity'), ('euler', 'euler', 'euler'),
    ('translate', 'translate', 'translate'), ('rotate_y', 'rotate_y', 'rotate_y'),
    ('dot', 'dot', 'dot'), ('normalize', 'normalize', 'normalize'),
    ('atan2_0011E620', 'atan2', 'atan2'), ('wrap_001B1470', 'scalar', 'wrap'),
    ('fabs_0011DF78', 'scalar', 'fabs'), ('cos_0011DE90', 'scalar', 'cos'),
    ('sqrt_0011E748', 'scalar', 'sqrt'), ('request', 'request', 'request'),
    ('sound', 'sound', 'sound'), ('sound_base_00179B90', 'actor_result', 'sound_base'),
    ('wall_001762E0', 'actor_result', 'wall'), ('node', 'node', 'node'),
)


class Workers(C.Structure):
    _fields_ = ([('context', VP), ('scratch', P(Scratch)), ('world', P(World))] +
                [(field, FN[kind]) for field, kind, _ in WORKER_FIELDS])


class RumblePad(C.Structure):
    _fields_ = [('port', C.c_int32), ('slot', C.c_int32), ('ready', C.c_uint8), ('active', C.c_uint8),
                ('act', C.c_uint8 * 6), ('duration', C.c_uint16)]


ACTUATOR_FN = C.CFUNCTYPE(I, VP, I, I, P(C.c_uint8))


class Rumble(C.Structure):
    _fields_ = [('pad', P(RumblePad)), ('enable', P(C.c_uint8)), ('mode', P(C.c_uint8)),
                ('context', VP), ('actuator', ACTUATOR_FN)]


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / ('ladder.dylib' if sys.platform == 'darwin' else 'ladder.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc',
                    'src/game/em_player_ladder_entry.c', 'src/game/em_player_ladder_climb.c',
                    'src/game/em_player_major2.c', '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    W = P(Workers)
    native.em_player_ladder_0015D4C0.argtypes = [W, LA, P(I)]
    native.em_player_ladder_00176F90.argtypes = [W, LA, P(I)]
    native.em_player_ladder_00177030.argtypes = [W, LA, I, P(I)]
    native.em_player_ladder_00180300.argtypes = [W, LA, PU, I, P(I)]
    native.em_player_ladder_probe_00180300.argtypes = [W, LA, PU, I, P(I)]
    native.em_player_ladder_00199DB0.argtypes = [P(World), P(Scratch), PU, P(I)]
    native.em_player_ladder_00199FA0.argtypes = [P(World), P(Scratch), PU, PU, P(I)]
    for name in ('em_player_ladder_00165B60', 'em_player_ladder_00176DC0',
                 'em_player_ladder_00182A70', 'em_player_ladder_state_b'):
        getattr(native, name).argtypes = [W, LA]
    native.em_player_ladder_0017FC80.argtypes = [W, LA, FLT]
    native.em_player_ladder_workers_bound.argtypes = [W]
    native.em_player_rumble_001B61C0.argtypes = [P(Rumble), I, I, I, I]
    return native


# ======================================================================
# Unit layout (addresses in the oracle's RAM)
# ======================================================================

ACTOR = 0x680000
REC = 0x6E0000                  # an OTHER record (a grid node stand-in)
ENT = 0x6E1000                  # the owner 0x700031D4 names
DIR = 0x6E2000                  # the cell directory
DIR_SIZE = 0x200
VERTS, NV = 0x6E4000, 24
NODE_TABLE, NODE = 0x6C0000, 0x6D0000
OUTA, OUTB = 0x6E6000, 0x6E6010
CELL_RECORD = 0x700030B0
PAD = 0x810E40
POISON = 0x7F7F7F7F             # the stack before a call (0015D4C0's case-0x3D read)
USE_FRAME = shared.STACK_TOP - 0x60   # 0015D4C0's $sp
ATTRS = (0x32, 0x32, 0x37, 0x38, 0x3B, 0x33, 0x3A, 0x20, 0x3D, 0x1E, 0x34, 0x3C, 0x35, 5, 0)
FLAGS = (0, 0x1000, 0x8000, 0x9000, 0x200)
ANGLES = (0.0, 0.7853982, 0.78539824, 0.785398, -0.7853982, -0.78539824, -0.785398, 1.0, -1.0,
          1.5707963, 1.5707964, 1.5707965, 2.5, -3.0)
DROPS = (40.0, 19.0, 16.0, 17.5, 60.0, 0.0, 22.0, 18.999998)


def put(buf, offset, size, value):
    buf[offset:offset + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'little')


def record_image(rng, attr):
    image = bytearray(rng.getrandbits(8) for _ in range(0x40))
    for k in range(6):
        put(image, 2 * k, 2, rng.randrange(NV))
    image[0x1A] = attr
    for off in (0x24, 0x2C, 0x34, 0x3C):
        put(image, off, 4, F(rng.uniform(-1, 1)))
    return bytes(image)


def probe_state(rng, attrs):
    """A probe outcome: record kind, image, point, owner and kind word."""
    kind = rng.choice((1, 2, 2, 2, 0))
    return {'record': kind, 'image': record_image(rng, rng.choice(attrs)),
            'point': [F(rng.uniform(-400, 400)) for _ in range(3)],
            'entity': rng.choice((ENT, ENT, 0)), 'e0E': rng.choice((0x0100, 0x0300, 0x0512, 0xFF00, 0x0700)),
            'd8': rng.choice((2, 2, 4, 0))}


def effect_for(rng, name, case):
    e = {'ret': 0, 'out': None, 'fret': None, 'probe': None, 'column': None}
    chance = rng.random
    if name in ('probe', 'move', 'segment', 'sweep'):
        e['ret'] = rng.choice((0, 1, 2, 4, 6, 2, 1)) if chance() < 0.75 else 0
        if chance() < 0.85:
            state = probe_state(rng, case['attrs'])
            if e['ret'] == 0 and chance() < 0.5:
                state['record'] = 0
            if e['ret'] != 0 and state['record'] == 0 and chance() < 0.8:
                state['record'] = 2
            e['probe'] = state
    elif name == 'column':
        n = rng.choice((0, 1, 2, 3, 5, 8, COLUMN_MAX))
        feet = case['feet']
        heights = []
        for _ in range(COLUMN_MAX):
            d = rng.choice((0.0, 4.01, 10.0, 23.99, 24.0, 30.0, -5.0, 'lo', 'hi', 'lo2', 'hi2'))
            if d == 'lo': heights.append(M.ee_add(F(4.01), feet))
            elif d == 'lo2': heights.append(M.ee_add(M.ee_add(F(4.01), feet), 1))
            elif d == 'hi': heights.append(M.ee_add(feet, F(24.0)))
            elif d == 'hi2': heights.append(M.ee_add(feet, F(24.0)) - 1)
            else: heights.append(M.ee_add(feet, F(d)))
        e['column'] = (n, heights, [rng.choice((0, 1, 1, 3, 2)) for _ in range(COLUMN_MAX)])
    elif name in ('trs', 'identity', 'euler', 'translate', 'rotate_y'):
        e['out'] = [F(rng.uniform(-2, 2)) for _ in range(16)]
    elif name in ('apply', 'vadd', 'normalize'):
        e['out'] = [F(rng.uniform(-400, 400)) for _ in range(4)]
    elif name == 'dot':
        e['fret'] = F(rng.choice((0.5, 0.49999997, 0.50000006, 0.9, -1.0, 0.0)))
    elif name in ('atan2', 'wrap', 'cos', 'sqrt'):
        e['fret'] = F(rng.choice(ANGLES + (rng.uniform(-3.2, 3.2),)))
    elif name == 'fabs':
        e['fret'] = F(rng.choice(DROPS + (1.5707963, 1.5707964, 1.5707965, 0.5, 3.0)))
    elif name == 'sound_base':
        e['ret'] = rng.choice((0, 0xE6, 0x100, 0x1C3, 7))
    elif name == 'wall':
        e['ret'] = rng.choice((0, 1, 0, 5))
    return e


class Script:
    def __init__(self, seed, case, fail_at=None):
        self.seed, self.case, self.count, self.fail_at = seed, case, 0, fail_at

    def next(self, name):
        rng = random.Random('%d:%d:%s' % (self.seed, self.count, name))
        index = self.count
        self.count += 1
        return index, effect_for(rng, name, self.case)


ENTRIES = ('use',) * 14 + ('refresh', 'facing', 'facing', 'facing', 'classify', 'center', 'corners') + \
    ('state_b',) * 10 + ('walls', 'clips', 'step', 'rumble')


def elf_d2754D0():
    """D_002754D0[0] as the user's ELF holds it (a signed halfword)."""
    elf = ELF if ELF is not None else read_elf()
    at = 0x2754D0 - 0x100000 + 0x300
    return struct.unpack_from('<h', elf, at)[0]


def make_case(seed):
    rng = random.Random(seed)
    entry = rng.choice(ENTRIES)
    actor = bytearray(rng.getrandbits(8) for _ in range(0x320))
    for off in (0xB0, 0xB8, 0x290, 0x294, 0x298, 0x254, 0x2E0, 0x2E4, 0x2E8):
        put(actor, off, 4, F(rng.uniform(-400, 400)))
    feet = rng.choice((190.0, 195.27, 235.5, 0.0))
    put(actor, 0xB4, 4, F(feet))
    for off in (0xC0, 0xC4, 0xC8, 0x218):
        put(actor, off, 4, F(rng.uniform(-3.2, 3.2)))
    put(actor, 0xBC, 4, F(1.0))
    for i in range(16):
        put(actor, 0xD0 + 4 * i, 4, F(rng.uniform(-2, 2)))
    for off in (0x60, 0x64, 0x68):
        put(actor, off, 4, F(1.0))
    put(actor, 0x280, 4, 0); put(actor, 0x284, 4, F(-13.8)); put(actor, 0x288, 4, 0)
    put(actor, 0x28C, 4, F(1.0))
    put(actor, 0x200, 4, rng.choice(FLAGS))
    put(actor, 0x23B, 1, rng.choice(ATTRS))
    put(actor, 4, 1, 1)
    put(actor, 5, 1, 0xB)
    put(actor, 6, 1, rng.choice((0, 0, 1, 2, 2, 2, 2, 0xA, 0xB, 0xC, 3)))
    put(actor, 7, 1, rng.choice((0, 0, 1, 1, 2, 3)))
    put(actor, 0x1F0, 1, rng.choice((0x15, 0x16, 0x16, 0x17)))
    put(actor, 0x28, 2, rng.choice((0, 1, 2, 3, 4, 0xFFFF)))
    put(actor, 0x2F1, 1, rng.choice((0, 1)))
    put(actor, 0x3C, 4, F(rng.choice((63.0, 63.000004, 52.0, 52.000004, 20.0, 20.000002, 2.0,
                                      2.0000002, 24.0, 24.000002, 25.0, 25.000002, 0.0, 70.0))))
    boxed = rng.random() < 0.3
    if boxed:                                       # 00165B60 case 0's box
        put(actor, 0xB0, 4, F(rng.choice((1010.0, 1009.9999, 1020.0, 1030.0, 1030.0001))))
        put(actor, 0xB4, 4, F(rng.choice((170.0, 169.99998, 175.0, 180.0, 180.00002))))
        put(actor, 0xB8, 4, F(rng.choice((830.0, 829.99994, 840.0, 850.0, 850.00006))))
    attrs = rng.choice((ATTRS, (0x32,), (0x37, 0x38, 0x32), (0x3A, 0x34, 0x1E, 0x3A),
                        (0x20, 0x3C, 0x3C), (0x3D,), (0x33, 0x3B)))
    case = {
        'seed': seed, 'entry': entry, 'actor': bytes(actor), 'attrs': attrs,
        'feet': F(feet) if rng.random() < 0.7 else struct.unpack('<I', bytes(actor[0xB4:0xB8]))[0],
        'scratch': {name: [rng.getrandbits(32) for _ in range(n)] for name, _, n in OWNED},
        'probe': probe_state(rng, attrs),
        'column': (rng.randrange(4), [F(rng.uniform(0, 300)) for _ in range(COLUMN_MAX)],
                   [rng.randrange(4) for _ in range(COLUMN_MAX)]),
        'directory': bytes(rng.getrandbits(8) for _ in range(DIR_SIZE)),
        'verts': [F(rng.uniform(-400, 400)) for _ in range(NV * 3)],
        'node': [F(rng.uniform(-400, 400)) for _ in range(3)] + [F(1.0)],
        'area': rng.choice((0xD, 2, 0xB, 0xB)), 'c7c': rng.choice((0, 1)), 'c7d': rng.choice((0, 1)),
        # D_002754D0 is read-only .sdata (no original code writes it): the
        # case keeps the ELF's word. 0017FC80's one translation
        # (em_player_ladder_climb.c) embeds the same rows, checked against
        # the ELF by its own oracle. The draw stays so later draws do not move.
        'clip': (rng.choice((0, 1, 2)), elf_d2754D0())[1],
        'mode': rng.choice((0, 1, 2, 3, 4, 4, 5)), 'check': rng.choice((0, 1, 2, 3)),
        'blend': rng.choice((16.0, 0.0, 8.0)),
        'rumble': (rng.choice((0, 1, 1)), rng.choice((0, 1, 1)), rng.choice((0, 1, 2, 2)),
                   rng.choice((0, 1)), [rng.getrandbits(8) for _ in range(6)],
                   rng.randrange(0x10000), rng.randrange(2), rng.randrange(4),
                   (rng.choice((0, 1, 2)), rng.choice((0, 0xC0, 0x1EE, -1)),
                    rng.choice((5, 0x3C, 0x12345, -1)), rng.choice((0, 1, 2)))),
    }
    # The directory: count word, offset words for uids 0..7 (some 0), hulls.
    directory = bytearray(case['directory'])
    put(directory, 0, 4, 8)
    for uid in range(8):
        put(directory, 4 + 4 * uid, 4, rng.choice((0, 0x40 + 0x20 * uid, 0x40 + 0x20 * uid)))
    for k in range(0x40, DIR_SIZE, 4):
        put(directory, k, 4, F(rng.uniform(-400, 400)))
    case['directory'] = bytes(directory)
    if entry == 'state_b' and rng.random() < 0.5:     # case 2, the drop measure
        put(actor, 6, 1, 2)
        case['actor'] = bytes(actor)
        if rng.random() < 0.5:
            case['attrs'] = (0x32,)
    if entry == 'state_b' and boxed and rng.random() < 0.7:
        case['area'] = 0xD
    return case


def location(address):
    for name, base, n in OWNED:
        if base <= address < base + 4 * n:
            return name if address == base else '%s+%d' % (name, address - base)
    if address == 0x700031B0:
        return 's31B0'
    return 'copy'


class UnitOracle:
    """The original routines on a LadderEE with every callee hooked."""

    def __init__(self, elf):
        self.ee = LadderEE(elf, cover=True)
        for address, name in CALLEES.items():
            self.ee.hooks[address] = self.hook(name)

    def write_probe(self, state):
        ee = self.ee
        target = {0: 0, 1: CELL_RECORD, 2: REC}[state['record']]
        ee.save(0x700031D0, target)
        if target:
            ee.write(target, state['image'])
        for i in range(3): ee.save(0x700031B0 + 4 * i, state['point'][i])
        ee.save(0x700031D4, state['entity'])
        ee.save(ENT + 0xE, state['e0E'], 2)
        ee.save(0x700031D8, state['d8'])

    def write_column(self, column):
        ee = self.ee
        n, heights, flags = column
        ee.save(0x700031E0, n)
        for i in range(COLUMN_MAX):
            ee.save(0x700030F0 + 4 * i, heights[i])
            ee.save(0x70003170 + 2 * i, flags[i], 2)

    def hook(self, name):
        def run(ee):
            entry = self.log_entry(name, ee)
            _, e = self.script.next(name)
            self.log.append(entry)
            if e['probe'] is not None: self.write_probe(e['probe'])
            if e['column'] is not None: self.write_column(e['column'])
            if e['out'] is not None:
                for i, value in enumerate(e['out']): ee.save(ee.arg(0) + 4 * i, value)
            if e['fret'] is not None: ee.f[0] = e['fret']
            ee.ret_int(e['ret'])
        return run

    def log_entry(self, name, ee):
        a = ee.arg
        vec = lambda address, n: tuple(ee.load(address + 4 * i) for i in range(n))
        ptr = lambda address, n: (location(address), vec(address, n))
        if name in ('probe', 'move', 'sweep', 'request', 'sound', 'sound_base', 'wall'):
            assert a(0) == ACTOR, (name, hex(a(0)))
        if name == 'probe':
            assert a(2) == ACTOR + 0x280, ('0019BA80 box', hex(a(2)))
            return (name, ptr(a(1), 4), vec(a(2), 4), s32(a(3)))
        if name == 'move': return (name, ptr(a(1), 4), s32(a(2)))
        if name == 'segment': return (name, ptr(a(0), 4), ptr(a(1), 4), s32(a(2)), s32(a(3)))
        if name == 'sweep': return (name, ptr(a(1), 4), ptr(a(2), 4), s32(a(3)))
        if name == 'column':
            assert a(1) == ee.load(0x700031D0), ('0019BC40 record', hex(a(1)))
            return (name, ptr(a(0), 4))
        if name == 'trs':
            assert (a(0), a(1), a(2), a(3)) == (ACTOR + 0xD0, ACTOR + 0xB0, ACTOR + 0xC0, ACTOR + 0x60)
            return (name, vec(a(1), 4), vec(a(2), 4), vec(a(3), 4))
        if name in ('apply',): return (name, location(a(0)), ptr(a(1), 16), ptr(a(2), 4))
        if name == 'vadd': return (name, location(a(0)), ptr(a(1), 4), ptr(a(2), 4))
        if name == 'identity': return (name, location(a(0)))
        if name in ('euler', 'translate'): return (name, location(a(0)), ptr(a(1), 16), ptr(a(2), 4))
        if name == 'rotate_y': return (name, location(a(0)), ptr(a(1), 16), ee.f[12] & MASK)
        if name == 'dot': return (name, ptr(a(0), 4), ptr(a(1), 4))
        if name == 'normalize': return (name, location(a(0)), ptr(a(1), 4))
        if name == 'atan2': return (name, ee.f[12] & MASK, ee.f[13] & MASK)
        if name in ('wrap', 'fabs', 'cos', 'sqrt'): return (name, ee.f[12] & MASK)
        if name == 'request': return (name, s32(a(1)), s32(a(2)), ee.f[12] & MASK)
        if name == 'sound':
            assert s32(a(2)) == 0 and ee.f[12] & MASK == F(300.0), ('sound arguments', a(2), ee.f[12])
            return (name, s32(a(1)))
        if name == 'actuator':
            assert a(2) == PAD + 0x18, ('00111018 block', hex(a(2)))
            return (name, s32(a(0)), s32(a(1)), tuple(ee.read(a(2), 6)))
        return (name,)

    def run(self, case, script):
        ee = self.ee
        self.script, self.log = script, []
        ee.r, ee.rh = [0] * 32, [0] * 32
        ee.f, ee.acc, ee.cond = [0] * 32, 0, False
        ee.vacc, ee.q = [0, 0, 0, 0], 0
        ee.r[28], ee.r[29] = 0x27D370, shared.STACK_TOP
        ee.r[16] = ACTOR                        # 00160220's $s0 at its 0015D4C0 call
        ee.low_read = False
        ee.stack[:] = POISON.to_bytes(4, 'little') * (len(ee.stack) // 4)
        ee.write(ACTOR, case['actor'])
        for name, address, n in OWNED:
            for i, value in enumerate(case['scratch'][name]): ee.save(address + 4 * i, value)
        self.write_probe(case['probe'])
        self.write_column(case['column'])
        ee.write(DIR, case['directory'])
        ee.save(0x70003250, DIR)
        for i, value in enumerate(case['verts']): ee.save(VERTS + 4 * i, value)
        ee.save(0x700031FC, VERTS)
        ee.save(0x275B40, NODE_TABLE); ee.save(NODE_TABLE + 4, NODE)
        for i, value in enumerate(case['node']): ee.save(NODE + 0xC0 + 4 * i, value)
        ee.save(0x810700, case['area'], 1)
        ee.save(0x810C7C, case['c7c'], 1); ee.save(0x810C7D, case['c7d'], 1)
        ee.save(0x2754D0, case['clip'], 2)
        for address in (OUTA, OUTB):
            for i in range(3): ee.save(address + 4 * i, 0x5A5A0000 + i)
        entry = case['entry']
        v0 = None
        if entry == 'use':
            ee.call(USE, (ACTOR,)); v0 = s32(ee.r[2])
        elif entry == 'refresh':
            ee.call(REFRESH, (ACTOR,)); v0 = ee.r[2] & 0xFF
        elif entry == 'facing':
            ee.call(FACING, (ACTOR, case['mode'])); v0 = s32(ee.r[2])
        elif entry == 'classify':
            ee.call(CLASSIFY, (ACTOR, 0x700038A0, case['check'])); v0 = s32(ee.r[2])
        elif entry == 'center':
            ee.call(CENTER, (0x700038A0,)); v0 = s32(ee.r[2])
        elif entry == 'corners':
            ee.call(CORNERS, (OUTA, OUTB)); v0 = s32(ee.r[2])
        elif entry == 'state_b':
            ee.call(STATE_B, (ACTOR,))
        elif entry == 'walls':
            ee.call(WALLS, (ACTOR,))
        elif entry == 'clips':
            ee.r[4] = ACTOR; ee.f[12] = F(case['blend']); ee.r[31] = shared.RETURN
            ee.run(CLIPS)
        elif entry == 'step':
            ee.call(STEP, (ACTOR,))
        elif entry == 'rumble':
            return self.run_rumble(case)
        return {'actor': ee.read(ACTOR, 0x320), 'scratch': self.scratch_words(), 'log': self.log,
                'v0': v0, 'out': (ee.vector(OUTA, 3), ee.vector(OUTB, 3)),
                'low_read': ee.low_read, 'stale': ee.load(USE_FRAME + 0x54) == POISON}

    def scratch_words(self):
        ee = self.ee
        out = {name: [ee.load(address + 4 * i) for i in range(n)] for name, address, n in OWNED}
        out['probe'] = (ee.load(0x700031D0), [ee.load(0x700031B0 + 4 * i) for i in range(3)],
                        ee.load(0x700031D4), ee.load(0x700031D8), ee.load(0x700031E0))
        return out

    def run_rumble(self, case):
        ee = self.ee
        big_on, ready, mode, active, act, duration, enable, force_seed, args = case['rumble']
        block = bytearray(0x30)
        put(block, 4, 4, case['seed'] & 3); put(block, 8, 4, (case['seed'] >> 2) & 1)
        block[0x12] = ready; block[0x16] = active; block[0x18:0x1E] = bytes(act)
        put(block, 0x28, 2, duration)
        ee.write(PAD, bytes(block))
        ee.save(0x810119, enable, 1)
        ee.save(0x275BE0, mode, 1)
        ee.call(RUMBLE, args)
        return {'pad': ee.read(PAD, 0x30), 'log': self.log}


# ======================================================================
# The native run
# ======================================================================

def array_of(values, ctype=U32):
    return (ctype * len(values))(*values)


class NativeRun:
    """em_player_ladder_entry.c with Python workers replaying the script."""

    def __init__(self, native, case, script, missing=None):
        self.native, self.case, self.script, self.log = native, case, script, []
        self.narrow = False
        self.live = LiveActor()
        C.memmove(self.live.bytes, case['actor'], 0x320)
        self.scratch = s = Scratch()
        for name, _, n in OWNED:
            arr = getattr(s, name)
            for i, value in enumerate(case['scratch'][name]): arr[i] = value
        self.set_probe(case['probe'])
        self.set_column(case['column'])
        self.directory = array_of(list(case['directory']), C.c_uint8)
        self.verts = array_of(case['verts'])
        self.world = World(C.cast(self.directory, P(C.c_uint8)), DIR_SIZE, C.cast(self.verts, PU), NV,
                           case['area'], case['c7c'], case['c7d'], case['clip'])
        self.keep = []
        fields = {}
        for field, kind, name in WORKER_FIELDS:
            fields[field] = FN[kind](self.worker(field, name)) if field != missing else FN[kind]()
            self.keep.append(fields[field])
        self.workers = Workers(None, C.pointer(self.scratch), C.pointer(self.world), **fields)
        if missing == 'scratch': self.workers.scratch = P(Scratch)()
        if missing == 'world': self.workers.world = P(World)()

    def set_probe(self, state):
        s = self.scratch
        s.record = state['record']
        s.record_word = {0: 0, 1: CELL_RECORD, 2: REC}[state['record']]
        if state['record']:
            C.memmove(s.record_bytes, state['image'], 0x40)
        for i in range(3): s.s31B0[i] = state['point'][i]
        s.entity = state['entity']
        s.entity_0E = state['e0E']
        s.s31D8 = state['d8']

    def set_column(self, column):
        n, heights, flags = column
        self.scratch.s31E0 = n
        for i in range(COLUMN_MAX):
            self.scratch.s30F0[i] = heights[i]
            self.scratch.s3170[i] = flags[i]

    def where(self, pointer):
        address = C.cast(pointer, C.c_void_p).value
        offset = address - C.addressof(self.scratch)
        for base, (name, _, n) in FIELD_AT.items():
            if base <= offset < base + 4 * n:
                return name if offset == base else '%s+%d' % (name, offset - base)
        if offset == Scratch.s31B0.offset:
            return 's31B0'
        return 'copy'

    def call(self, name, entry):
        self.log.append(entry)
        index, e = self.script.next(name)
        if self.script.fail_at == index:
            return None
        if e['probe'] is not None: self.set_probe(e['probe'])
        if e['column'] is not None: self.set_column(e['column'])
        return e

    def worker(self, field, name):
        vec = lambda p, n: tuple(p[i] for i in range(n))
        ptr = lambda p, n: (self.where(p), vec(p, n))

        def done(e, out=None, count=0, result=None, fresult=None):
            if e is None: return -1
            if out is not None and e['out'] is not None:
                for i in range(count): out[i] = e['out'][i]
            if result is not None: result[0] = s32(e['ret'])
            if fresult is not None: fresult[0] = e['fret']
            return 0

        if name == 'probe':
            return lambda _, a, point, box, mask, out: done(
                self.call(name, (name, ptr(point, 4), vec(box, 4), mask)), result=out)
        if name == 'move':
            return lambda _, a, target, mask, out: done(self.call(name, (name, ptr(target, 4), mask)), result=out)
        if name == 'segment':
            return lambda _, f, t, mask, id_, out: done(
                self.call(name, (name, ptr(f, 4), ptr(t, 4), mask, id_)), result=out)
        if name == 'sweep':
            return lambda _, a, f, t, mask, out: done(self.call(name, (name, ptr(f, 4), ptr(t, 4), mask)), result=out)
        if name == 'column':
            return lambda _, at: done(self.call(name, (name, ptr(at, 4))))
        if name == 'trs':
            return lambda _, out, p, r, sc: done(self.call(name, (name, vec(p, 4), vec(r, 4), vec(sc, 4))), out, 16)
        if name == 'apply':
            return lambda _, out, m, v: done(self.call(name, (name, self.where(out), ptr(m, 16), ptr(v, 4))), out, 4)
        if name == 'vadd':
            return lambda _, out, x, y: done(self.call(name, (name, self.where(out), ptr(x, 4), ptr(y, 4))), out, 4)
        if name == 'identity':
            return lambda _, out: done(self.call(name, (name, self.where(out))), out, 16)
        if name in ('euler', 'translate'):
            return lambda _, out, m, v: done(self.call(name, (name, self.where(out), ptr(m, 16), ptr(v, 4))), out, 16)
        if name == 'rotate_y':
            return lambda _, out, m, angle: done(self.call(name, (name, self.where(out), ptr(m, 16), angle)), out, 16)
        if name == 'dot':
            return lambda _, x, y, out: done(self.call(name, (name, ptr(x, 4), ptr(y, 4))), fresult=out)
        if name == 'normalize':
            return lambda _, out, x: done(self.call(name, (name, self.where(out), ptr(x, 4))), out, 4)
        if name == 'atan2':
            return lambda _, y, x, out: done(self.call(name, (name, y, x)), fresult=out)
        if name in ('wrap', 'fabs', 'cos', 'sqrt'):
            return lambda _, x, out: done(self.call(name, (name, x)), fresult=out)
        if name == 'request':
            return lambda _, a, clip, force, blend: done(self.call(name, (name, clip, force, F(blend))))
        if name == 'sound':
            return lambda _, a, id_: done(self.call(name, (name, id_)))
        if name in ('sound_base', 'wall'):
            return lambda _, a, out: done(self.call(name, (name,)), result=out)
        if name == 'node':
            def node(_, out):
                for i in range(4): out[i] = self.case['node'][i]
                return 0
            return node
        raise AssertionError(field)

    def scratch_words(self):
        s = self.scratch
        out = {name: list(getattr(s, name)) for name, _, _ in OWNED}
        out['probe'] = (s.record_word, list(s.s31B0), s.entity, s.s31D8 & MASK, s.s31E0 & MASK)
        return out

    def run(self):
        n, case, entry = self.native, self.case, self.case['entry']
        W, A = C.byref(self.workers), C.byref(self.live)
        out = C.c_int(-99)
        outa, outb = array_of([0x5A5A0000 + i for i in range(3)]), array_of([0x5A5A0000 + i for i in range(3)])
        v0 = None
        if entry == 'use':
            result = n.em_player_ladder_0015D4C0(W, A, C.byref(out)); v0 = out.value
        elif entry == 'refresh':
            result = n.em_player_ladder_00176F90(W, A, C.byref(out)); v0 = out.value
        elif entry == 'facing':
            result = n.em_player_ladder_00177030(W, A, case['mode'], C.byref(out)); v0 = out.value
        elif entry == 'classify':
            # Odd seeds: the narrow entry the closure lane's bridge calls
            # (the same translation, checking only what 00180300 reaches).
            classify = (n.em_player_ladder_probe_00180300 if case['seed'] & 1 or self.narrow
                        else n.em_player_ladder_00180300)
            result = classify(W, A, self.scratch.s38A0, case['check'], C.byref(out))
            v0 = out.value
        elif entry == 'center':
            result = n.em_player_ladder_00199DB0(C.byref(self.world), C.byref(self.scratch),
                                                 self.scratch.s38A0, C.byref(out)); v0 = out.value
        elif entry == 'corners':
            result = n.em_player_ladder_00199FA0(C.byref(self.world), C.byref(self.scratch), outa, outb,
                                                 C.byref(out)); v0 = out.value
        elif entry == 'state_b':
            result = n.em_player_ladder_state_b(W, A)
        elif entry == 'walls':
            result = n.em_player_ladder_00176DC0(W, A)
        elif entry == 'clips':
            result = n.em_player_ladder_0017FC80(W, A, case['blend'])
        elif entry == 'step':
            result = n.em_player_ladder_00182A70(W, A)
        return result, {'actor': bytes(self.live.bytes), 'scratch': self.scratch_words(), 'log': self.log,
                        'v0': v0, 'out': (tuple(outa), tuple(outb)), 'fault': self.scratch.fault}


def native_rumble(native, case, missing=None):
    big_on, ready, mode, active, act, duration, enable, _, args = case['rumble']
    pad = RumblePad((case['seed'] & 3), (case['seed'] >> 2) & 1, ready, active, (C.c_uint8 * 6)(*act), duration)
    enable_b, mode_b = C.c_uint8(enable), C.c_uint8(mode)
    log = []

    def actuator(_, port, slot, block):
        log.append(('actuator', port, slot, tuple(block[i] for i in range(6))))
        return 0
    fn = ACTUATOR_FN(actuator) if missing != 'actuator' else ACTUATOR_FN()
    r = Rumble(C.pointer(pad), C.pointer(enable_b), C.pointer(mode_b), None, fn)
    if missing == 'pad': r.pad = P(RumblePad)()
    result = native.em_player_rumble_001B61C0(C.byref(r), *args)
    block = bytearray(0x30)
    put(block, 4, 4, pad.port); put(block, 8, 4, pad.slot)
    block[0x12] = pad.ready; block[0x16] = pad.active; block[0x18:0x1E] = bytes(pad.act)
    put(block, 0x28, 2, pad.duration)
    return result, {'pad': bytes(block), 'log': log}


# ======================================================================
# One unit case
# ======================================================================

# Branch outcomes no input can produce, each checked by hand against the
# listing: 0015DA94 tests 00177030(p, 2), and 00177030 returns 1 on every
# path of mode 2 (001771C0..00177324 end with v0 = 1).
UNREACHABLE = {(0x15DA94, True)}

NULL_RECORD_SITES = {0x176FF8, 0x177054, 0x177370, 0x1772E8, 0x18039C, 0x15D628, 0x15D750,
                     0x15D9FC, 0x15DA78, 0x15DBAC, 0x15DCD8, 0x15DE2C}
PAD_FIELDS = [4, 5, 6, 7, 8, 9, 10, 11, 0x12, 0x16, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x28, 0x29]

ELF = NATIVE = ORACLE = None


def run_case(seed):
    global ORACLE
    if ORACLE is None:
        ORACLE = UnitOracle(ELF)
    case = make_case(seed)
    ORACLE.ee.outcomes = set()      # this case's branch outcomes (counted only when compared)
    want = ORACLE.run(case, Script(seed, case))
    where = (seed, case['entry'])
    if case['entry'] == 'rumble':
        result, got = native_rumble(NATIVE, case)
        assert result == 0, (where, 'native fault')
        assert want['log'] == got['log'], (where, 'actuator calls', want['log'], got['log'])
        diff = [hex(k) for k in PAD_FIELDS if want['pad'][k] != got['pad'][k]]
        assert not diff, (where, 'pad block differs at', diff)
        return case['entry'], len(want['log']), 0, 'ok', tuple(sorted(ORACLE.ee.outcomes))
    result, got = NativeRun(NATIVE, case, Script(seed, case)).run()
    if result != 0:
        # A read the native world cannot give: prove the original made it.
        fault = got['fault']
        assert result == -1 and fault, (where, 'native fault without a site', result)
        k = len(got['log'])
        assert got['log'] == want['log'][:k], (where, 'calls before the fault', got['log'], want['log'][:k])
        if fault in NULL_RECORD_SITES:
            assert want['low_read'], (where, 'native saw no record at %06X, the original read one' % fault)
            kind = 'null-record'
        elif fault == 0x15DE68:
            assert want['stale'], (where, '0015DE68: the original read a written stack word')
            kind = 'stale-stack'
        else:
            raise AssertionError((where, 'unexpected fault site %06X' % fault))
        return case['entry'], len(want['log']), 0, kind, ()
    assert not want['low_read'], (where, 'the original read low memory but the native did not fault')
    assert want['log'] == got['log'], (where, 'worker calls', want['log'], got['log'])
    if want['actor'] != got['actor']:
        diff = [hex(k) for k in range(0x320) if want['actor'][k] != got['actor'][k]]
        raise AssertionError((where, 'actor bytes differ at', diff[:24]))
    assert want['scratch'] == got['scratch'], (where, 'scratch', want['scratch'], got['scratch'])
    assert want['v0'] == got['v0'], (where, 'return', want['v0'], got['v0'])
    if case['entry'] == 'corners':
        assert want['out'] == got['out'], (where, 'corners', want['out'], got['out'])
    faults = 0
    if want['log'] and seed % 5 == 0:
        k = random.Random(seed).randrange(len(want['log']))
        result, cut = NativeRun(NATIVE, case, Script(seed, case, fail_at=k)).run()
        assert result == -1, (where, 'fault not reported', k)
        assert cut['log'] == want['log'][:k + 1], (where, 'calls after a fault', k)
        faults = 1
    return case['entry'], len(want['log']), faults, 'ok', tuple(sorted(ORACLE.ee.outcomes))


def missing_worker_checks(native):
    """Each worker, the scratch and the world missing: every entry point
    returns -1 before any write and calls nothing."""
    count = 0
    base = make_case(1000)
    fields = [f for f, _, _ in WORKER_FIELDS] + ['scratch', 'world']
    for field in fields:
        for entry in ('use', 'refresh', 'facing', 'classify', 'state_b', 'walls', 'clips', 'step'):
            case = dict(base, entry=entry)
            run = NativeRun(native, case, Script(1000, case), missing=field)
            if field == 'scratch' and entry == 'classify':
                continue                       # its vector argument lives in the scratch
            result, got = run.run()
            assert result == -1 and got['log'] == [] and got['actor'] == case['actor'], (field, entry)
            count += 1
    for field in ('apply', 'vadd', 'sweep_0019AFE0'):   # what the narrow 00180300 reaches
        case = dict(base, entry='classify')
        run = NativeRun(native, case, Script(1000, case), missing=field)
        run.narrow = True
        result, got = run.run()
        assert result == -1 and got['log'] == [] and got['actor'] == case['actor'], ('narrow', field)
        count += 1
    for field in ('pad', 'actuator'):
        case = dict(base, entry='rumble')
        result, got = native_rumble(native, case, missing=field)
        assert result == -1 and got['log'] == [], ('rumble', field)
        count += 1
    return count


def data_fault_checks(native):
    """Reads outside the bound world fault with the scratch naming the site
    and write nothing: a vertex index past the pool, a directory offset past
    the image, a column count past the 32 entries 0015D4C0 can read."""
    n = 0
    case = make_case(2000)
    run = NativeRun(native, dict(case, entry='center'), Script(2000, case))
    run.scratch.record = 2
    run.scratch.record_bytes[0] = NV + 5
    run.scratch.fault = 0
    before = list(run.scratch.s38A0)
    out = C.c_int(-9)
    r = native.em_player_ladder_00199DB0(C.byref(run.world), C.byref(run.scratch), run.scratch.s38A0, C.byref(out))
    assert r == -1 and run.scratch.fault and list(run.scratch.s38A0) == before, ('vertex', r)
    n += 1
    run = NativeRun(native, dict(case, entry='center'), Script(2000, case))
    run.scratch.record, run.scratch.entity, run.scratch.entity_0E, run.scratch.s31D8 = 1, ENT, 0x0200, 2
    C.memmove(C.addressof(run.directory) + 4 + 4 * 2, struct.pack('<I', DIR_SIZE - 4), 4)
    run.scratch.fault = 0
    r = native.em_player_ladder_00199DB0(C.byref(run.world), C.byref(run.scratch), run.scratch.s38A0, C.byref(out))
    assert r == -1 and run.scratch.fault == 0x199E18, ('directory', r, hex(run.scratch.fault))
    n += 1
    return n


# ======================================================================
# World mode: route beat 10 over the captured RAM
# ======================================================================

PLAYER = shared.PLAYER
VEC = 0x7F0E0000                 # private vectors for worker arguments (stack region)
SCRIPTED = 0x41                  # +1F0 of the scripted takeover rows
WORLD_BEATS = ('10_cage_roof_roger',)
HOOKED = {USE: 'use', STATE_B: 'state_b', REFRESH: 'refresh', FACING: 'facing',
          CLASSIFY: 'classify', CENTER: 'center', CORNERS: 'corners', WALLS: 'walls',
          CLIPS: 'clips', STEP: 'step', RUMBLE: 'rumble'}
# Quick world run: through the first ladder entry (0015D4C0 at f267, state
# 0xB f268..f326, the hand-off to 0xC at f327) and 13 frames of state 0xC.
# EM_TEST_FULL=1 replays the whole valid span (both ladders).
WORLD_QUICK_FRAMES = 340


def replay_end(trace):
    """The row before the first scripted takeover after the player has
    moved: RouteReplay runs no scripts, so it cannot follow those rows."""
    moved = False
    for r in sorted(trace['rows'], key=lambda r: r['counter']):
        if r['p5'] != 0:
            moved = True
        elif moved and r['m1F0'] == SCRIPTED:
            return r['counter'] - 1
    return trace['last_counter']


class LadderRoute(shared.RouteReplay):
    """shared.RouteReplay on FallEE (the measured float model)."""

    def __init__(self, elf, trace, ram, spad):
        self.ee = ee = FallEE(elf, ram, spad)
        self.frame, self.events = 0, []
        for address, name in shared.SOUND_HOOKS.items():
            ee.hooks[address] = self.recorder(name)
        ee.hooks[shared.PAD_READ] = self.pad_read
        self.raw = bytes(8)
        self.rows = {r['counter']: r for r in trace['rows']}
        self.first = trace['first_counter']
        self.inputs = sorted(trace['inputs'], key=lambda i: i['f'])
        self.counter = ee.load(0x70003B64)


def in_ram(address, size=4):
    return 0x100000 <= address and address + size <= 0x2000000


class WorldCall:
    """One native call in progress: the actor (or none), the scratch and the
    world built from the EE, and EmPlayerLadderWorkers bound to the ORIGINAL
    callees executed in the same EE (actor and scratch synced around every
    call)."""

    def __init__(self, ee, base):
        self.ee, self.base, self.error = ee, base, None
        self.live = LiveActor()
        if base is not None:
            C.memmove(self.live.bytes, ee.read(base, 0x320), 0x320)
        self.scratch = Scratch()
        for name, address, n in OWNED:
            arr = getattr(self.scratch, name)
            for i in range(n): arr[i] = ee.load(address + 4 * i)
        self.probe_in()
        d = ee.load(0x70003250)
        size = max(0, min(0x40000, 0x2000000 - d)) if in_ram(d) else 0
        self.directory = (C.c_uint8 * size).from_buffer(ee.mem, d) if size else None
        v = ee.load(0x700031FC)
        count = max(0, min(0x10000, (0x2000000 - v) // 12)) if in_ram(v) and v % 4 == 0 else 0
        self.verts = (U32 * (count * 3)).from_buffer(ee.mem, v) if count else None
        self.world = World(C.cast(self.directory, P(C.c_uint8)) if size else P(C.c_uint8)(), size,
                           C.cast(self.verts, PU) if count else PU(), count,
                           ee.load(0x810700, 1), ee.load(0x810C7C, 1), ee.load(0x810C7D, 1),
                           s32(ee.load(0x2754D0, 2) << 16) >> 16)
        self.keep = []
        fields = {}
        for field, kind, name in WORKER_FIELDS:
            fields[field] = FN[kind](self.guard(getattr(self, 'w_' + name)))
            self.keep.append(fields[field])
        self.workers = Workers(None, C.pointer(self.scratch), C.pointer(self.world), **fields)
        self.slots = {}

    # ---- state transfer ------------------------------------------------
    def probe_in(self):
        ee, s = self.ee, self.scratch
        record = ee.load(0x700031D0)
        s.record_word = record
        s.record = 0 if record == 0 else 1 if record == CELL_RECORD else 2
        if record and (in_ram(record, 0x40) or 0x70000000 <= record < 0x70004000 - 0x40):
            C.memmove(s.record_bytes, ee.read(record, 0x40), 0x40)
        elif record:
            s.record = 3                     # unreadable: the native faults if it reads it
        for i in range(3): s.s31B0[i] = ee.load(0x700031B0 + 4 * i)
        entity = ee.load(0x700031D4)
        s.entity = entity
        s.entity_0E = ee.load(entity + 0xE, 2) if in_ram(entity, 0x10) else 0
        s.s31D8 = s32(ee.load(0x700031D8))
        s.s31E0 = s32(ee.load(0x700031E0))
        for i in range(COLUMN_MAX):
            s.s30F0[i] = ee.load(0x700030F0 + 4 * i)
            s.s3170[i] = ee.load(0x70003170 + 2 * i, 2)

    def sync_in(self):
        ee = self.ee
        if self.base is not None:
            ee.write(self.base, bytes(self.live.bytes))
        for name, address, n in OWNED:
            arr = getattr(self.scratch, name)
            for i in range(n): ee.save(address + 4 * i, arr[i])

    def sync_out(self):
        ee = self.ee
        if self.base is not None:
            C.memmove(self.live.bytes, ee.read(self.base, 0x320), 0x320)
        for name, address, n in OWNED:
            arr = getattr(self.scratch, name)
            for i in range(n): arr[i] = ee.load(address + 4 * i)
        self.probe_in()

    def guard(self, fn):
        def run(*args):
            if self.error is not None:
                return -1
            try:
                return fn(*args[1:])
            except BaseException as error:        # surfaced after the native call returns
                self.error = error
                return -1
        return run

    def address(self, pointer, n):
        """The EE address the original passes for a native vector argument:
        the scratch word it points at, else a private slot holding the
        values (the same native array gets the same slot, so an output that
        aliases an input stays aliased)."""
        ee = self.ee
        at = C.cast(pointer, C.c_void_p).value
        offset = at - C.addressof(self.scratch)
        for base, (name, address, count) in FIELD_AT.items():
            if base <= offset < base + 4 * count:
                return address + (offset - base)
        if at not in self.slots:
            self.slots[at] = VEC + 0x100 * len(self.slots)
        slot = self.slots[at]
        for i in range(n): ee.save(slot + 4 * i, pointer[i])
        return slot

    def back(self, pointer, n):
        """Copy a private slot back into a native output array (scratch
        outputs come back through sync_out)."""
        slot = self.slots.get(C.cast(pointer, C.c_void_p).value)
        if slot is not None:
            for i in range(n): pointer[i] = self.ee.load(slot + 4 * i)

    def call(self, entry, args=(), fregs=()):
        from test_player_fall_reference import nested_bits
        self.sync_in()
        v0, f0 = nested_bits(self.ee, entry, args, fregs)
        self.sync_out()
        return s32(v0), f0 & MASK

    # ---- the workers (argument order as in EmPlayerLadderWorkers) ------------
    def w_probe(self, a, point, box, mask, out):
        assert [box[i] for i in range(4)] == [self.ee.load(self.base + 0x280 + 4 * i) for i in range(4)]
        out[0] = self.call(0x19BA80, (self.base, self.address(point, 4), self.base + 0x280, mask))[0]
        return 0

    def w_move(self, a, target, mask, out):
        out[0] = self.call(0x19AD00, (self.base, self.address(target, 4), mask))[0]; return 0

    def w_segment(self, f, t, mask, id_, out):
        out[0] = self.call(0x19A570, (self.address(f, 4), self.address(t, 4), mask, id_))[0]; return 0

    def w_sweep(self, a, f, t, mask, out):
        out[0] = self.call(0x19AFE0, (self.base, self.address(f, 4), self.address(t, 4), mask))[0]; return 0

    def w_column(self, at):
        self.call(0x19BC40, (self.address(at, 4), self.ee.load(0x700031D0))); return 0

    def w_trs(self, out, position, rotation, scale):
        self.call(0x1C94B0, (self.base + 0xD0, self.base + 0xB0, self.base + 0xC0, self.base + 0x60))
        for i in range(16): out[i] = self.ee.load(self.base + 0xD0 + 4 * i)
        return 0

    def vu(self, entry, out, n_out, inputs, fregs=()):
        args = [self.address(out, n_out)] + [self.address(p, n) for p, n, _ in inputs]
        self.call(entry, tuple(args), fregs)
        self.back(out, n_out)
        return 0

    def w_apply(self, out, m, v): return self.vu(0x1026A0, out, 4, [(m, 16, 0xD0), (v, 4, None)])
    def w_vadd(self, out, x, y): return self.vu(0x1028B8, out, 4, [(x, 4, None), (y, 4, None)])
    def w_identity(self, out): return self.vu(0x1029C0, out, 16, [])
    def w_euler(self, out, m, v): return self.vu(0x102C58, out, 16, [(m, 16, 0xD0), (v, 4, 0xC0)])
    def w_translate(self, out, m, v): return self.vu(0x102918, out, 16, [(m, 16, 0xD0), (v, 4, 0xB0)])
    def w_rotate_y(self, out, m, angle): return self.vu(0x102BB0, out, 16, [(m, 16, None)], (angle,))
    def w_normalize(self, out, x): return self.vu(0x102760, out, 4, [(x, 4, None)])

    def w_dot(self, x, y, out):
        out[0] = self.call(0x102738, (self.address(x, 4), self.address(y, 4)))[1]; return 0

    def w_atan2(self, y, x, out): out[0] = self.call(0x11E620, (), (y, x))[1]; return 0
    def w_wrap(self, x, out): out[0] = self.call(0x1B1470, (), (x,))[1]; return 0
    def w_fabs(self, x, out): out[0] = self.call(0x11DF78, (), (x,))[1]; return 0
    def w_cos(self, x, out): out[0] = self.call(0x11DE90, (), (x,))[1]; return 0
    def w_sqrt(self, x, out): out[0] = self.call(0x11E748, (), (x,))[1]; return 0
    def w_request(self, a, clip, force, blend): self.call(0x1749A0, (self.base, clip, force), (F(blend),)); return 0
    def w_sound(self, a, id_): self.call(0x1FBD50, (self.base, id_, 0), (F(300.0),)); return 0
    def w_sound_base(self, a, out): out[0] = self.call(0x179B90, (self.base,))[0]; return 0
    def w_wall(self, a, out): out[0] = self.call(0x1762E0, (self.base,))[0]; return 0

    def w_node(self, out):
        node = self.ee.load(self.ee.load(0x275B40) + 4)
        for i in range(4): out[i] = self.ee.load(node + 0xC0 + 4 * i)
        return 0

    # ---- after the native call ------------------------------------------
    def finish(self, result, what):
        if self.error is not None:
            raise self.error
        assert result == 0, (what, 'native fault', result, hex(self.scratch.fault))
        self.sync_in()


def world_hook(native, kind, counts, results):
    def hook(ee):
        a0 = ee.arg(0)
        out = C.c_int(-99)
        if kind in ('center', 'corners'):
            call = WorldCall(ee, None)
            first = array_of([ee.load(a0 + 4 * i) for i in range(3)])
            if kind == 'center':
                result = native.em_player_ladder_00199DB0(C.byref(call.world), C.byref(call.scratch),
                                                          first, C.byref(out))
                if out.value:
                    for i in range(3): ee.save(a0 + 4 * i, first[i])
            else:
                a1 = ee.arg(1)
                second = array_of([ee.load(a1 + 4 * i) for i in range(3)])
                result = native.em_player_ladder_00199FA0(C.byref(call.world), C.byref(call.scratch),
                                                          first, second, C.byref(out))
                if out.value:
                    for i in range(3): ee.save(a0 + 4 * i, first[i]); ee.save(a1 + 4 * i, second[i])
            assert result == 0, (kind, 'native fault', hex(call.scratch.fault))
            ee.ret_int(out.value)
        elif kind == 'rumble':
            pad = RumblePad(s32(ee.load(PAD + 4)), s32(ee.load(PAD + 8)), ee.load(PAD + 0x12, 1),
                            ee.load(PAD + 0x16, 1), (C.c_uint8 * 6)(*ee.read(PAD + 0x18, 6)),
                            ee.load(PAD + 0x28, 2))
            enable, mode = C.c_uint8(ee.load(0x810119, 1)), C.c_uint8(ee.load(0x275BE0, 1))
            args = tuple(s32(ee.arg(i)) for i in range(4))

            def actuator(_, port, slot, block):
                ee.write(PAD + 0x16, bytes([pad.active])); ee.write(PAD + 0x18, bytes(pad.act))
                ee.save(PAD + 0x28, pad.duration, 2)
                ee.nested(0x111018, (port, slot, PAD + 0x18))
                return 0
            fn = ACTUATOR_FN(actuator)
            r = Rumble(C.pointer(pad), C.pointer(enable), C.pointer(mode), None, fn)
            assert native.em_player_rumble_001B61C0(C.byref(r), *args) == 0
            ee.write(PAD + 0x16, bytes([pad.active])); ee.write(PAD + 0x18, bytes(pad.act))
            ee.save(PAD + 0x28, pad.duration, 2)
        else:
            call = WorldCall(ee, a0)
            W, A = C.byref(call.workers), C.byref(call.live)
            if kind == 'use': result = native.em_player_ladder_0015D4C0(W, A, C.byref(out))
            elif kind == 'refresh': result = native.em_player_ladder_00176F90(W, A, C.byref(out))
            elif kind == 'facing': result = native.em_player_ladder_00177030(W, A, s32(ee.arg(1)), C.byref(out))
            elif kind == 'classify':
                v = ee.arg(1)
                assert v == 0x700038A0, ('00180300 vector', hex(v))
                result = native.em_player_ladder_00180300(W, A, call.scratch.s38A0, s32(ee.arg(2)), C.byref(out))
            elif kind == 'state_b': result = native.em_player_ladder_state_b(W, A)
            elif kind == 'walls': result = native.em_player_ladder_00176DC0(W, A)
            elif kind == 'clips': result = native.em_player_ladder_0017FC80(W, A, C.c_float.from_buffer_copy(
                struct.pack('<I', ee.f[12] & MASK)).value)
            else: result = native.em_player_ladder_00182A70(W, A)
            call.finish(result, kind)
            if kind in ('use', 'refresh', 'facing', 'classify'):
                ee.ret_int(out.value)
                results.setdefault(kind, {})
                results[kind][out.value] = results[kind].get(out.value, 0) + 1
        counts[kind] = counts.get(kind, 0) + 1
    return hook


WORLD = {}


def world_replay(job):
    import hashlib
    beat, native_mode = job
    trace, ram, spad = WORLD[beat]
    replay = LadderRoute(WORLD['elf'], trace, ram, spad)
    counts, results = {}, {}
    if native_mode:
        for address, kind in HOOKED.items():
            replay.ee.hooks[address] = world_hook(WORLD['native'], kind, counts, results)
    frames_default = '100000' if reference_mode.FULL else str(WORLD_QUICK_FRAMES)
    end = min(replay_end(trace), trace['first_counter'] + int(os.environ.get('EM_WORLD_FRAMES', frames_default)))
    frames, rows, states = [], 0, set()
    while replay.counter < end:
        row = replay.step()
        ee = replay.ee
        digest = hashlib.sha1(ee.mem)
        digest.update(ee.spad)
        frames.append((replay.counter, digest.hexdigest(), ee.read(PLAYER, 0x320), len(replay.events)))
        states.add((ee.load(PLAYER + 5, 1), ee.load(PLAYER + 0x1F0, 1)))
        if row is not None:
            shared.route_row_check(ee, row, (beat, 'native' if native_mode else 'original', replay.counter))
            rows += 1
    return frames, replay.events, rows, counts, results, sorted(states)


def world_main():
    elf = read_elf()
    beats = [b for b in os.environ.get('EM_WORLD_BEATS', ','.join(WORLD_BEATS)).split(',') if b]
    for beat in beats:
        loaded = shared.route_beat(beat)
        if isinstance(loaded, str):
            raise SystemExit('world mode: %s (docs/PLAYER_LADDER_ENTRY.md)' % loaded)
        WORLD[beat] = loaded
    WORLD['elf'], WORLD['native'] = elf, build_native()
    jobs = [(beat, mode) for beat in beats for mode in (False, True)]
    by_job = dict(zip(jobs, reference_mode.parallel_map(world_replay, jobs)))
    for beat in beats:
        (a_frames, a_events, rows, _, _, a_states), (b_frames, b_events, b_rows, counts, results, _) = \
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
        entered = results.get('use', {}).get(1, 0)
        assert counts.get('state_b', 0) > 0 and entered > 0, (beat, 'no ladder entry ran natively', counts)
        seen = ' '.join('%X/%X' % st for st in a_states if st[0] in (0xB, 0xC))
        print('%s: PASS %d frames identical (whole RAM + scratchpad + player record), %d trace rows '
              'within precision, %d sound/effect calls; native calls %s; returns %s; ladder states '
              '(+5/+1F0) %s' % (beat, len(a_frames), rows, len(a_events),
                                ', '.join('%s %d' % kv for kv in sorted(counts.items())),
                                {k: dict(sorted(v.items())) for k, v in sorted(results.items())}, seen))


def main():
    global ELF, NATIVE
    import time
    started = time.time()
    ELF = read_elf()
    callees = check_callee_set(ELF)
    NATIVE = build_native()
    total = reference_mode.pick(40000, 40000)
    seeds = reference_mode.select(range(total), 5000, 0x1ADD)
    results = reference_mode.parallel_map(run_case, seeds)
    outcomes, entries, kinds = set(), {}, {}
    faults = calls = 0
    for entry, count, fault, kind, cover in results:
        outcomes.update(cover)
        entries[entry] = entries.get(entry, 0) + 1
        kinds[kind] = kinds.get(kind, 0) + 1
        faults += fault
        calls += count
    sites = branch_sites(ELF)
    missing = sorted('%06X %s' % (pc, 'taken' if taken else 'not taken')
                     for pc in sites for taken in (True, False)
                     if (pc, taken) not in outcomes and (pc, taken) not in UNREACHABLE)
    assert not missing, ('branch outcomes never exercised', missing)
    absent = [e for e in set(ENTRIES) if e not in entries]
    assert not absent, ('entry points never run', absent)
    stops = missing_worker_checks(NATIVE)
    data = data_fault_checks(NATIVE)
    reference_mode.banner(reference_mode.part(len(seeds), total, 'cases'),
                          '%d jal targets (all hooked or translated)' % callees)
    print('player ladder entry vs original instructions: PASS %d cases (%s), %d worker calls '
          'identical, every one of %d conditional branches both ways, %d fault-stop cuts, '
          '%d missing-worker refusals, %d data-fault checks, native data faults proven against '
          'the original: %s (%.1fs)' % (
              len(seeds), ', '.join('%s %d' % kv for kv in sorted(entries.items())), calls,
              len(sites), faults, stops, data,
              ', '.join('%s %d' % kv for kv in sorted(kinds.items()) if kv[0] != 'ok'),
              time.time() - started))


if __name__ == '__main__':
    if os.environ.get('EM_TEST_WORLD', '') not in ('', '0'):
        world_main()
    else:
        main()
