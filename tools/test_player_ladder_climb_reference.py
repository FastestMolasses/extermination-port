#!/usr/bin/env python3
"""Execute the original ladder-climb state and compare em_player_ladder_climb.c.

docs/PLAYER_LADDER_CLIMB.md. The original instructions run from the captured
AREA11 EE RAM (../Extermination/build/startup-reference/playable_ee.bin,
state 04), whose code and table bytes are first checked against the user's
pinned ELF; none are embedded here. The interpreter is the shared EE of
tools/test_player_slide_reference.py, subclassed (LadderEE) so that every
COP1 operation goes through tools/ee_float_model.py (the measured EE rules,
docs/EE_FLOAT_MODEL.md) and any VU0 macro operation is refused. The shared
files are not edited.

Executed, unmodified (never hooked):
  001662D0  state 0xC, with everything below run as original code inside it
  0017FC80 (+ 001885D0 / 001885F0), 0017FD00, 0017FD40, 0017FD80, 0017FE00,
  0017FE80, 0017FF00, 00180420, 00180460, 00180530, 00180600, 001809B0,
  00174AB0, 001031E0, 00102948, 0011DF78, 00181110
and each exported helper alone, over the same hooks.

Every other callee is hooked, scripted per case and recorded (never
simulated as a claim about the callee): the scripted return value and the
scripted actor / skeleton / scratchpad / hit-record writes are applied
identically on both sides, and the call sequence with every argument (floats
as bits, vectors as the words the callee receives and where they live) must
match. After each case all 0x320 actor bytes, the scratchpad words
0x700038A0..0x700038DF and 0x70003A20..0x70003A2F and D_008106F2 must
match, and so must the same state at every callee entry. Branch coverage:
every conditional branch of the executed routines must be seen both taken
and not taken (asserted).

The player record sits at its captured address 0x8102B0. D_00275B40 there is
the player's own bone array at +0x40, so the bone pointers +0x40/+0x44 keep
their captured values and the case's node words are written at the
captured node 0 / node 1 records.

The route part replays the two beat-10 cage climbs
(../Extermination/build/s87/route/10_cage_roof_roger/trace.json) through
the native module: see docs/PLAYER_LADDER_CLIMB.md section 5 for what it
does and does not show.

EM_TEST_FULL=1 runs the exhaustive sweep; the default run is a fixed-seed
sample with the same comparisons and the same coverage assertions.
"""
import ctypes as C
import json
import random
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from test_player_slide_reference import EE, read_elf, bits, number, s32, sx32, REFERENCE, DECOMP  # noqa: E402
from test_player_floor_reference import LiveActor, cached_build  # noqa: E402
import ee_float_model as M  # noqa: E402
import reference_mode  # noqa: E402

RAM_PATH = REFERENCE / 'playable_ee.bin'
ROUTE_TRACE = DECOMP / 'build/s87/route/10_cage_roof_roger/trace.json'
PLAYER = 0x8102B0
STATE = 0x1662D0
HIT_REC = 0x7F0A0000          # the case's hit record (0x700031D0 points here; private stack region)
SPAD_A0, SPAD_3A20 = 0x700038A0, 0x70003A20

# The executed routines (address, size) from the split listing; their bytes
# in the captured RAM must equal the ELF.
ROUTINES = {
    0x1662D0: 0x19A8, 0x1809B0: 0x760, 0x180600: 0xDC, 0x180460: 0xC8, 0x180530: 0xC4,
    0x180420: 0x40, 0x17FC80: 0x74, 0x17FD00: 0x40, 0x17FD40: 0x40, 0x17FD80: 0x78,
    0x17FE00: 0x78, 0x17FE80: 0x78, 0x17FF00: 0x78, 0x1885D0: 0x1C, 0x1885F0: 0x1C,
    0x174AB0: 0x10, 0x1031E0: 0x1C, 0x102948: 0xC, 0x11DF78: 0x1C, 0x181110: 0x6C,
}
TABLES = ((0x2754D0, 8),)     # D_002754D0 / D_002754D4 halfwords
COVERED = [(a, a + n) for a, n in ROUTINES.items()]

# ------------------------------------------------------------------ EE ----


class LadderEE(EE):
    """The shared EE with COP1 routed through the measured model; VU0 macro
    operations are refused (none of the executed routines uses one).
    Records every conditional branch outcome inside the executed routines."""

    def __init__(self, elf, ram):
        super().__init__(elf, ram)
        self.acc_bits = 0
        self.branches = set()

    def reset(self):
        self.r = [0] * 32; self.rh = [0] * 32; self.hi = self.lo = 0
        self.f = [0] * 32; self.acc_bits = 0; self.cond = False
        self.vf = [[0, 0, 0, 0] for _ in range(32)]; self.vf[0][3] = bits(1.0)
        self.r[28] = 0x27D370; self.r[29] = 0x7F0F0000
        self.log = []

    def cop1(self, word, pc):
        rs, fn = word >> 21 & 31, word & 63
        fs, ft, fd = word >> 11 & 31, word >> 16 & 31, word >> 6 & 31
        f = self.f
        if rs == 20:
            if fn == 32:
                f[fd] = M.ee_cvt_s_w(f[fs]); return
            raise M.UnmeasuredCase(('cvt', fn, hex(pc)))
        if rs != 16:
            return super().cop1(word, pc)          # mfc1 / mtc1 / cfc1 / ctc1 moves
        a, b = f[fs], f[ft]
        if fn == 0: f[fd] = M.ee_add(a, b)
        elif fn == 1: f[fd] = M.ee_sub(a, b)
        elif fn == 2: f[fd] = M.ee_mul(a, b)
        elif fn == 3: f[fd] = M.ee_div(a, b)
        elif fn == 6: f[fd] = M.ee_mov(a)
        elif fn == 7: f[fd] = M.ee_neg(a)
        elif fn == 24: self.acc_bits = M.ee_adda(a, b)
        elif fn == 25: self.acc_bits = M.ee_suba(a, b)
        elif fn == 26: self.acc_bits = M.ee_mula(a, b)
        elif fn == 28: f[fd] = M.ee_madd(self.acc_bits, a, b)
        elif fn == 29: f[fd] = M.ee_msub(self.acc_bits, a, b)
        elif fn == 36: f[fd] = M.ee_cvt_w_s(a)
        elif fn == 48: self.cond = False
        elif fn == 50: self.cond = bool(M.ee_c_eq(a, b))
        elif fn == 52: self.cond = bool(M.ee_c_lt(a, b))
        elif fn == 54: self.cond = bool(M.ee_c_le(a, b))
        else:
            raise M.UnmeasuredCase(('COP1 op outside the model', fn, hex(pc)))

    def macro(self, word):
        raise M.UnmeasuredCase(('VU0 op outside this oracle', hex(word)))

    def branch(self, word, pc):
        outcome = super().branch(word, pc)
        if outcome is not None:
            for start, end in COVERED:
                if start <= pc < end:
                    self.branches.add((pc, outcome[0]))
                    break
        return outcome


def conditional_branches(ram, start, end):
    """Every conditional branch (beq/bne/blez/bgtz(+l), regimm, bc1) in
    [start, end), from the executed bytes."""
    found = []
    for pc in range(start, end, 4):
        word = struct.unpack_from('<I', ram, pc)[0]
        op, rs = word >> 26, word >> 21 & 31
        if op in (4, 5, 6, 7, 20, 21, 22, 23):
            if op in (4, 20) and rs == (word >> 16 & 31):
                continue                            # beq x, x: unconditional b
            found.append(pc)
        elif op == 1 or (op == 17 and rs == 8):
            found.append(pc)
    return found


# ------------------------------------------------------- native side -----

class LadderScene(C.Structure):
    _fields_ = [('area', C.c_uint8), ('area_sub', C.c_uint8), ('d8106F2', C.c_uint8),
                ('pad0', C.c_uint8), ('pad', C.c_uint16), ('use_mask', C.c_uint16),
                ('spad38A0', C.c_uint32 * 16), ('spad3A20', C.c_uint32 * 4)]


A = C.POINTER(LiveActor)
FP, IP, UP = C.POINTER(C.c_float), C.POINTER(C.c_int), C.POINTER(C.c_uint32)
NODE_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.c_uint, UP)
KIND_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, IP)
HITY_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, UP)
CALL_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, A, C.c_int, C.c_int, C.c_float)   # request / sound
INT4_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.c_int, C.c_int, C.c_int)
ACT_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, A)
ACT_I_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, A, C.c_int)
ACT_R_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, A, IP)
ACT_I_R_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, A, C.c_int, IP)
FRAMES_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_int, IP)
PROBE_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, A, FP, C.c_int, IP)
COLUMN_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, A, FP, C.c_int, C.c_float, IP)
SWEEP_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, A, FP, FP, C.c_uint, IP)
BOX_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, FP, FP, C.c_int, C.c_int, IP)
PAIR_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, FP, FP, IP)
OUT_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, FP)
VEC_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, FP, FP, FP)
AHEAD_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, A, FP, IP)
F1_FN = C.CFUNCTYPE(C.c_float, C.c_void_p, C.c_float)
F3_FN = C.CFUNCTYPE(C.c_float, C.c_void_p, C.c_float, C.c_float, C.c_float)
TOINT_FN = C.CFUNCTYPE(C.c_int32, C.c_void_p, C.c_float)

WORKER_FIELDS = [
    ('context', C.c_void_p), ('node', NODE_FN), ('hit_kind', KIND_FN), ('hit_y', HITY_FN),
    ('request', CALL_FN), ('sound', CALL_FN), ('sfx', INT4_FN), ('cue', INT4_FN),
    ('sound_109', ACT_FN), ('steer_input', ACT_FN), ('heading', ACT_I_FN), ('skeleton', ACT_FN),
    ('floor', ACT_I_R_FN), ('footstep', ACT_I_FN), ('ground_effect', ACT_FN), ('translate', ACT_I_FN),
    ('reentry', ACT_I_FN), ('handoff', ACT_FN), ('w0021C270', ACT_FN), ('w0021C350', ACT_FN),
    ('camera', ACT_FN), ('clip_row', ACT_R_FN), ('clip_frames', FRAMES_FN),
    ('probe', PROBE_FN), ('column', COLUMN_FN), ('wall', PROBE_FN), ('sweep', SWEEP_FN),
    ('sweep_box', BOX_FN), ('hit_probe', PAIR_FN), ('hit_point', OUT_FN), ('transform', VEC_FN),
    ('ledge_ahead', AHEAD_FN), ('grab_check', ACT_R_FN), ('dash', ACT_I_FN), ('grab', ACT_R_FN),
    ('hit_react', ACT_R_FN),
    ('sqrt', F1_FN), ('sine', F1_FN), ('cosine', F1_FN), ('wrap', F1_FN), ('approach', F3_FN),
    ('to_int', TOINT_FN),
]


class LadderWorkers(C.Structure):
    _fields_ = WORKER_FIELDS


class Ladder(C.Structure):
    _fields_ = [('workers', C.POINTER(LadderWorkers)), ('scene', C.POINTER(LadderScene))]


# Original callee address -> worker name (the executed routines are not hooked).
CALLEES = {
    0x1749A0: 'request', 0x1FBD50: 'sound', 0x1FB9F0: 'sfx', 0x1B61C0: 'cue',
    0x182A70: 'sound_109', 0x174FD0: 'steer_input', 0x174AC0: 'heading', 0x1C68C0: 'skeleton',
    0x175900: 'floor', 0x182430: 'footstep', 0x187EE0: 'ground_effect', 0x178B90: 'translate',
    0x17C440: 'reentry', 0x17C540: 'handoff', 0x21C270: 'w0021C270', 0x21C350: 'w0021C350',
    0x176DC0: 'camera', 0x188550: 'clip_row', 0x1C61D0: 'clip_frames',
    0x180300: 'probe', 0x1760C0: 'column', 0x19AB20: 'wall', 0x19AFE0: 'sweep',
    0x19A570: 'sweep_box', 0x199FA0: 'hit_probe', 0x199DB0: 'hit_point', 0x1026A0: 'transform',
    0x17E250: 'ledge_ahead', 0x178390: 'grab_check', 0x177030: 'dash', 0x1782A0: 'grab',
    0x178080: 'hit_react',
    0x11E748: 'sqrt', 0x11E2A8: 'sine', 0x11DE90: 'cosine', 0x1B1470: 'wrap', 0x1B12B0: 'approach',
    0x1281C0: 'to_int',
}
FLOAT_WORKERS = {'sqrt', 'sine', 'cosine', 'wrap', 'approach', 'to_int'}
PLAIN_ACT = {'sound_109', 'steer_input', 'skeleton', 'handoff', 'w0021C270', 'w0021C350', 'camera'}
ACT_ARG = {'heading', 'footstep', 'translate', 'reentry', 'dash'}
ACT_RESULT = {'clip_row', 'grab_check', 'grab', 'hit_react'}
# Every callee that is not a scalar float routine may write the scratchpad
# words (00174FD0, for one, writes 0x70003A20): the scripts do, so a native
# that caches a scratchpad word across a call fails.
SPAD_WRITERS = set(CALLEES.values()) - FLOAT_WORKERS
HIT_WRITERS = {'sweep', 'sweep_box', 'probe', 'wall', 'grab_check', 'dash'}

# ------------------------------------------------------------ scripts ----


def finite(rng):
    """A finite binary32 word: signed zeros, denormals (zero under DAZ), unit
    values, or a uniform value in the ranges the climb works in."""
    if rng.random() < 0.15:
        return rng.choice([0, 0x80000000, 1, 0x80000001, 0x3F800000, 0xBF800000])
    return bits(rng.uniform(-600.0, 600.0))


def fword(value):
    return bits(value) if isinstance(value, float) else value


def effect(seed, name, index):
    """The scripted result of the index-th call of `name` in case `seed`:
    a dict with 'result' (int or float bits), 'writes' [(offset, size,
    value)], 'nodes' {(node, offset): bits}, 'spad' [(index, bits)],
    'spad3A' [(index, bits)], 'outs' [4 or 8 words] and 'hit' (kind, y)."""
    rng = random.Random(zlib.crc32(('%d/%s/%d' % (seed, name, index)).encode()))
    e = {'result': 0, 'writes': [], 'nodes': {}, 'spad': [], 'spad3A': [], 'outs': None, 'hit': None}
    w = e['writes']

    def maybe(p, offset, size, value):
        if rng.random() < p: w.append((offset, size, value))
    if name == 'steer_input':
        w.append((0x24C, 4, rng.choice([0, 0, 1, 1, 2, 3, 2, 3, 0xFFFFFFFF])))
        maybe(0.5, 0x23F, 1, rng.randrange(5))
        maybe(0.1, 6, 1, rng.randrange(0x54))
    elif name in ('request', 'sound', 'sfx', 'cue', 'sound_109'):
        maybe(0.2, 0x200, 4, rng.choice([0, 0x1000, 0x8000, 0x9000]))
        maybe(0.1, 0x28, 2, rng.choice([0, 1, 2, 3, 4, 0xFFFF]))
        maybe(0.1, 0x2F1, 1, rng.randrange(3))
        maybe(0.1, 6, 1, rng.randrange(0x54))
    elif name == 'skeleton':
        for key in ((1, 0xC0), (1, 0xC4), (1, 0xC8), (1, 0xCC), (0, 0x0), (0, 0x4), (0, 0x8)):
            if rng.random() < 0.8: e['nodes'][key] = finite(rng)
        maybe(0.3, 0xB4, 4, finite(rng))
        maybe(0.1, 0x28, 2, rng.choice([0, 1]))
    elif name == 'floor':
        e['result'] = rng.choice([0, 0, 1, 0x81])
        maybe(0.3, 0xB4, 4, finite(rng)); maybe(0.3, 0x23F, 1, rng.randrange(4))
        maybe(0.2, 6, 1, rng.randrange(0x54)); maybe(0.1, 0x28, 2, rng.choice([0, 1, 5]))
    elif name in ('footstep', 'ground_effect'):
        maybe(0.3, 0xB0, 4, finite(rng)); maybe(0.3, 0xB4, 4, finite(rng))
    elif name == 'heading':
        maybe(0.5, 0x23F, 1, rng.randrange(4)); maybe(0.3, 6, 1, rng.randrange(0x54))
    elif name == 'translate':
        maybe(0.4, 0xB4, 4, finite(rng)); maybe(0.3, 0x38, 4, finite(rng))
        maybe(0.3, 0x200, 4, rng.choice([0, 0x1000, 0x8000]))
        if rng.random() < 0.4: e['nodes'][(0, 0x4)] = finite(rng)
    elif name in ('reentry', 'handoff', 'w0021C270', 'w0021C350', 'camera'):
        maybe(0.3, 6, 1, rng.randrange(0x54)); maybe(0.3, 5, 1, rng.randrange(0x26))
        maybe(0.3, 0x24C, 4, rng.choice([0, 1, 2]))
    elif name == 'clip_row':
        e['result'] = rng.choice([0x7B, 0x8E, 0x97, -1 & 0xFFFFFFFF])
    elif name == 'clip_frames':
        e['result'] = rng.choice([1, 8, 12, 30, 0, -1 & 0xFFFFFFFF, 0x7FFFFFFF, rng.randrange(1, 200)])
    elif name == 'probe':
        e['result'] = rng.choice([0, 0, 1, 2, 2])
        maybe(0.2, 0x23B, 1, rng.choice([0x32, 0x3B, 0x33]))
        maybe(0.2, 0xD, 1, rng.randrange(3))
    elif name == 'column':
        e['result'] = rng.choice([0, 0, 0, 1])
    elif name == 'wall':
        e['result'] = rng.choice([0, 0, 1, 2])
    elif name == 'sweep':
        e['result'] = rng.choice([0, 0, 0, 2, 4, 6, 1, 8, 7, 6])
    elif name == 'sweep_box':
        e['result'] = rng.choice([0, 1, 1, 2])
    elif name == 'hit_probe':
        e['result'] = rng.choice([0, 1, 1])
        e['outs'] = [finite(rng) for _ in range(8)]
    elif name == 'hit_point':
        e['outs'] = [finite(rng) for _ in range(4)]
    elif name == 'transform':
        e['outs'] = [finite(rng) for _ in range(4)]
    elif name == 'ledge_ahead':
        e['result'] = rng.choice([0, 1])
    elif name in ('grab_check', 'grab', 'hit_react'):
        e['result'] = rng.choice([0, 1, 1])
        maybe(0.2, 0xD, 1, rng.randrange(3))
    elif name == 'dash':
        maybe(0.5, 0xB0, 4, finite(rng)); maybe(0.5, 0xB8, 4, finite(rng)); maybe(0.3, 0xC4, 4, finite(rng))
    elif name == 'sqrt':
        e['result'] = bits(rng.choice([0.0, 4.5, 9.0, 13.5, rng.uniform(0.0, 60.0), rng.uniform(0.0, 60.0)]))
    elif name in ('sine', 'cosine'):
        e['result'] = bits(rng.uniform(-1.0, 1.0))
    elif name == 'wrap':
        e['result'] = fword(rng.choice([0.0, -0.0, 1e-30, -1e-30, 9e-38, -9e-38, rng.uniform(-3.2, 3.2),
                                        rng.uniform(-3.2, 3.2)]))
    elif name == 'approach':
        e['result'] = bits(rng.uniform(-3.2, 3.2))
    elif name == 'to_int':
        e['result'] = rng.choice([0, 1, 2, 5, 12, -1, -3, 0x7FFFFFFF, -0x80000000, rng.randrange(-20, 20)])
    if name in SPAD_WRITERS and rng.random() < 0.25:
        e['spad'].append((rng.randrange(16), finite(rng)))
    if name in SPAD_WRITERS and rng.random() < 0.1:
        e['spad3A'].append((rng.randrange(4), finite(rng)))
    if name in HIT_WRITERS and (name in ('sweep', 'sweep_box') or rng.random() < 0.2):
        e['hit'] = (rng.choice([0x3D, 0x3D, 0x32, 0x3B, 0x34, 0x34, 0x10, 0]), finite(rng))
    if forced_window(seed):
        # 001809B0's area (8, 3) window: pass the first two sweeps, hit a
        # plain kind on the third, and let 00178080 report the hit.
        if name == 'sweep':
            e['result'] = (0, 0, 6)[index] if index < 3 else 0
            e['hit'] = (0x10, e['hit'][1] if e['hit'] else 0)
        elif name == 'sweep_box':
            e['result'] = 0
        elif name == 'hit_react':
            e['result'] = 1
    if name in FLOAT_WORKERS and name != 'to_int':
        e['result'] = bits(number(e['result']))       # a representable binary32
    return e


HELPER_BASE = 0x180600 * 3


def forced_window(seed):
    """The helper cases that drive 001809B0 into its area (8, 3) window."""
    return (HELPER_BASE <= seed < HELPER_BASE + 10 ** 7 and HELPERS[seed % len(HELPERS)] == '001809B0'
            and (seed // len(HELPERS)) % 3 == 0)


# ------------------------------------------------------------- oracle ----

RAM = ELF = NATIVE = None
_EE = None
CAPTURED = None   # the captured player record (state 04)
NODE_KEYS = ((0, 0x0), (0, 0x4), (0, 0x8), (1, 0xC0), (1, 0xC4), (1, 0xC8), (1, 0xCC))


def oracle_ee():
    global _EE
    if _EE is None:
        _EE = LadderEE(ELF, RAM)
    return _EE


def node_addresses(ee):
    array = ee.load(0x275B40)                   # D_00275B40 = the player's +0x40
    return {0: ee.load(array), 1: ee.load(array + 4)}


def words(ee, address, count):
    return tuple(ee.load(address + 4 * i) for i in range(count))


def where(address):
    """Where a vector argument lives: a scratchpad offset from 0x700038A0,
    or 'local' (the actor's +290 for 00180420's transform, or the stack)."""
    address &= 0xFFFFFFFF
    if SPAD_A0 <= address < SPAD_A0 + 0x40:
        return address - SPAD_A0
    assert address == PLAYER + 0x290 or 0x7F000000 <= address < 0x7F100000, ('vector at', hex(address))
    return 'local'


def state_bytes(ee):
    """The compared state: actor, both scratchpad blocks and D_008106F2."""
    return (bytes(ee.read(PLAYER, 0x320)) + bytes(ee.read(SPAD_A0, 0x40)) + bytes(ee.read(SPAD_3A20, 0x10))
            + bytes(ee.read(0x8106F2, 1)))


def install_hooks(ee, seed):
    counters = {}
    nodes = node_addresses(ee)
    ee.snapshots = []

    def hook(name):
        def run(o):
            index = counters.get(name, 0); counters[name] = index + 1
            o.snapshots.append(state_bytes(o))
            a = [o.r[4 + i] & 0xFFFFFFFF for i in range(4)]
            if name in PLAIN_ACT | ACT_ARG | ACT_RESULT | {'request', 'sound', 'floor', 'ground_effect',
                                                           'probe', 'column', 'wall', 'sweep', 'ledge_ahead'}:
                assert a[0] == PLAYER, (name, hex(a[0]))
            if name in PLAIN_ACT | ACT_RESULT:
                entry = (name,)
            elif name in ACT_ARG or name == 'floor':
                entry = (name, s32(a[1]))
            elif name in ('request', 'sound'):
                entry = (name, s32(a[1]), s32(a[2]), o.f[12])
            elif name in ('sfx', 'cue'):
                entry = (name,) + tuple(s32(x) for x in a)
            elif name == 'ground_effect':
                assert a[1] == PLAYER + 0xB0 and a[2] == PLAYER + 0xD0, 'ground_effect args'
                entry = (name,)
            elif name == 'clip_frames':
                entry = (name, a[0], s32(a[1]))
            elif name == 'probe':
                entry = (name, where(a[1]), words(o, a[1], 4), s32(a[2]))
            elif name == 'column':
                entry = (name, where(a[1]), words(o, a[1], 4), s32(a[2]), o.f[12])
            elif name == 'wall':
                assert a[2] == PLAYER + 0x280, 'wall a2'
                entry = (name, where(a[1]), words(o, a[1], 4), s32(a[3]))
            elif name == 'sweep':
                entry = (name, where(a[1]), words(o, a[1], 4), where(a[2]), words(o, a[2], 4), a[3])
            elif name == 'sweep_box':
                entry = (name, where(a[0]), words(o, a[0], 4), where(a[1]), words(o, a[1], 4),
                         s32(a[2]), s32(a[3]))
            elif name == 'hit_probe':
                assert where(a[0]) == where(a[1]) == 'local' and a[1] == a[0] + 0x10, 'hit_probe args'
                entry = (name,)
            elif name == 'hit_point':
                entry = (name, where(a[0]))
            elif name == 'transform':
                assert a[1] == PLAYER + 0xD0, 'transform matrix'
                entry = (name, where(a[0]), words(o, a[1], 16), where(a[2]), words(o, a[2], 4))
            elif name == 'ledge_ahead':
                entry = (name, where(a[1]), words(o, a[1], 4))
            elif name == 'approach':
                entry = (name, o.f[12], o.f[13], o.f[14])
            elif name in FLOAT_WORKERS:
                entry = (name, o.f[12])
            else:
                raise AssertionError(('unrecorded callee', name))
            o.log.append(entry)
            e = effect(seed, name, index)
            for offset, size, value in e['writes']:
                o.save(PLAYER + offset, value, size)
            for (node, offset), value in e['nodes'].items():
                o.save(nodes[node] + offset, value)
            for i, value in e['spad']:
                o.save(SPAD_A0 + 4 * i, value)
            for i, value in e['spad3A']:
                o.save(SPAD_3A20 + 4 * i, value)
            if e['outs'] is not None:
                out = a[0]
                for i, value in enumerate(e['outs']): o.save(out + 4 * i, value)
            if e['hit'] is not None:
                o.save(HIT_REC + 0x1A, e['hit'][0], 1); o.save(0x700031B4, e['hit'][1])
            if name == 'to_int':
                o.r[2] = sx32(e['result'])
            elif name in FLOAT_WORKERS:
                o.f[0] = e['result']
            else:
                o.r[2] = sx32(e['result'])
        return run
    ee.hooks = {address: hook(name) for address, name in CALLEES.items()}


class Native:
    """The same callees on the native side, one log, the same scripts."""

    def __init__(self, seed, scene_values, node_values, hit, fault_at=None):
        self.seed, self.log, self.counters = seed, [], {}
        self.node_values = dict(node_values)
        self.hit = list(hit)                # (kind, y bits) of the last sweep
        self.fault_at = fault_at            # (name, index): that call returns -1
        self.keep = []
        self.live = None
        self.snapshots = []
        self.scene = LadderScene()
        area, area_sub, d8106F2, pad, use_mask, spad, spad3A = scene_values
        self.scene.area, self.scene.area_sub, self.scene.d8106F2 = area, area_sub, d8106F2
        self.scene.pad, self.scene.use_mask = pad, use_mask
        for i in range(16): self.scene.spad38A0[i] = spad[i]
        for i in range(4): self.scene.spad3A20[i] = spad3A[i]
        self.spad_base = C.addressof(self.scene) + LadderScene.spad38A0.offset
        w = self.workers = LadderWorkers()
        def node_fn(_, node, offset, out):
            out[0] = self.node_values[(node, offset)]; return 0
        def kind_fn(_, out):
            out[0] = self.hit[0] & 0xFF; return 0
        def hit_y_fn(_, out):
            out[0] = self.hit[1]; return 0
        w.node = self._keep(NODE_FN(node_fn))
        w.hit_kind = self._keep(KIND_FN(kind_fn))
        w.hit_y = self._keep(HITY_FN(hit_y_fn))
        for field, kind in WORKER_FIELDS[4:]:
            setattr(w, field, self._keep(kind(self._make(field))))
        self.ladder = Ladder(C.pointer(self.workers), C.pointer(self.scene))

    def _keep(self, fn):
        self.keep.append(fn); return fn

    def state(self):
        s = self.scene
        return (bytes(self.live.bytes) + bytes(s.spad38A0) + bytes(s.spad3A20) + bytes([s.d8106F2]))

    def where(self, pointer):
        address = C.cast(pointer, C.c_void_p).value
        if self.spad_base <= address < self.spad_base + 0x40:
            return address - self.spad_base
        return 'local'

    def _apply(self, name, actor, out_pointer=None):
        index = self.counters.get(name, 0); self.counters[name] = index + 1
        if self.fault_at == (name, index):
            return None
        e = effect(self.seed, name, index)
        raw = self.live.bytes
        for offset, size, value in e['writes']:
            for i in range(size): raw[offset + i] = (value >> (8 * i)) & 0xFF
        self.node_values.update(e['nodes'])
        for i, value in e['spad']: self.scene.spad38A0[i] = value
        for i, value in e['spad3A']: self.scene.spad3A20[i] = value
        if e['outs'] is not None and out_pointer is not None:
            target = C.cast(out_pointer, UP)
            for i, value in enumerate(e['outs']): target[i] = value
        if e['hit'] is not None:
            self.hit = [e['hit'][0], e['hit'][1]]
        return e['result']

    def _make(self, name):
        def fbits(value): return bits(value)
        def vec(pointer, count): return tuple(C.cast(pointer, UP)[i] for i in range(count))

        def done(entry, out_int=None, out_pointer=None):
            self.log.append(entry)
            self.snapshots.append(self.state())
            result = self._apply(name, None, out_pointer)
            if result is None: return -1
            if out_int is not None: out_int[0] = s32(result)
            return 0

        if name in PLAIN_ACT:
            return lambda _, a: done((name,))
        if name in ACT_ARG:
            return lambda _, a, arg: done((name, arg))
        if name in ACT_RESULT:
            return lambda _, a, r: done((name,), r)
        if name == 'floor':
            return lambda _, a, arg, r: done((name, arg), r)
        if name in ('request', 'sound'):
            return lambda _, a, x, y, f: done((name, x, y, fbits(f)))
        if name in ('sfx', 'cue'):
            return lambda _, a0, a1, a2, a3: done((name, a0, a1, a2, a3))
        if name == 'ground_effect':
            return lambda _, a: done((name,))
        if name == 'clip_frames':
            return lambda _, a0, a1, r: done((name, a0, a1), r)
        if name == 'probe':
            return lambda _, a, at, kind, r: done((name, self.where(at), vec(at, 4), kind), r)
        if name == 'column':
            return lambda _, a, at, arg, h, r: done((name, self.where(at), vec(at, 4), arg, fbits(h)), r)
        if name == 'wall':
            return lambda _, a, at, mask, r: done((name, self.where(at), vec(at, 4), mask), r)
        if name == 'sweep':
            return lambda _, a, p, q, mask, r: done((name, self.where(p), vec(p, 4), self.where(q),
                                                     vec(q, 4), mask), r)
        if name == 'sweep_box':
            return lambda _, p, q, c, d, r: done((name, self.where(p), vec(p, 4), self.where(q), vec(q, 4),
                                                  c, d), r)
        if name == 'hit_probe':
            def pair(_, p, q, r):
                self.log.append((name,))
                self.snapshots.append(self.state())
                e = effect(self.seed, name, self.counters.get(name, 0))
                result = self._apply(name, None)
                if result is None: return -1
                pa, pb = C.cast(p, UP), C.cast(q, UP)
                for i in range(4): pa[i] = e['outs'][i]; pb[i] = e['outs'][4 + i]
                r[0] = s32(result)
                return 0
            return pair
        if name == 'hit_point':
            return lambda _, out: done((name, self.where(out)), None, out)
        if name == 'transform':
            return lambda _, out, m, v: done((name, self.where(out), vec(m, 16), self.where(v), vec(v, 4)),
                                             None, out)
        if name == 'ledge_ahead':
            return lambda _, a, v, r: done((name, self.where(v), vec(v, 4)), r)
        if name == 'to_int':
            def to_int(_, x):
                self.log.append((name, fbits(x)))
                self.snapshots.append(self.state())
                return s32(self._apply(name, None))
            return to_int
        if name in FLOAT_WORKERS:
            def scalar(_, *args):
                self.log.append((name,) + tuple(fbits(x) for x in args))
                self.snapshots.append(self.state())
                return number(self._apply(name, None))
            return scalar
        raise AssertionError(('no native worker for', name))


def build_native():
    out = ROOT / 'build/player_ladder_climb_reference'
    out.mkdir(parents=True, exist_ok=True)
    lib = out / ('ladder.dylib' if sys.platform == 'darwin' else 'ladder.so')
    cached_build(lib, ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                       '-shared', '-fPIC', '-Isrc', 'src/game/em_player_ladder_climb.c',
                       'src/game/em_player_major2.c', '-o', str(lib)])
    native = C.CDLL(str(lib))
    L = C.POINTER(Ladder)
    native.em_player_ladder_climb_state.argtypes = [C.c_void_p, A]
    for name in ('0017FC80', '0017FD00', '0017FD40'):
        getattr(native, 'em_player_ladder_climb_' + name).argtypes = [L, A, C.c_float]
    for name in ('0017FD80', '0017FE00', '0017FE80', '0017FF00'):
        getattr(native, 'em_player_ladder_climb_' + name).argtypes = [L, A, C.c_int, C.c_float]
    native.em_player_ladder_climb_00180420.argtypes = [L, A]
    native.em_player_ladder_climb_00180460.argtypes = [L, A, IP]
    native.em_player_ladder_climb_00180530.argtypes = [L, A, IP]
    native.em_player_ladder_climb_00180600.argtypes = [L, A, C.c_int, C.c_float, C.c_float, C.c_float, IP]
    native.em_player_ladder_climb_001809B0.argtypes = [L, A, C.c_int, IP]
    return native


# --------------------------------------------------------------- cases ----

STATES = (0, 1, 2, 3, 4, 0xA, 0xB, 0xC, 0x14, 0x15, 0x16, 0x1E, 0x1F, 0x20, 0x28, 0x29, 0x2A, 0x2B,
          0x2C, 0x2D, 0x32, 0x33, 0x34, 0x35, 0x3C, 0x3D, 0x3E, 0x3F, 0x46, 0x47, 0x48, 0x50, 0x51, 0x52)
# Sub-states whose paths need more cases to reach every branch.
HEAVY = (1, 4, 0xC, 0x15, 0x34, 0x3E, 0x2B, 0x20)
ACTOR_FLOATS = (0x38, 0x3C, 0xB0, 0xB4, 0xB8, 0xBC, 0xC4, 0x204, 0x218, 0x21C, 0x224, 0x22C, 0x258,
                0x260, 0x264, 0x26C, 0x290, 0x294, 0x298, 0x29C, 0x2E0, 0x2E4, 0x2E8, 0x2EC, 0x2F4, 0x2F8)
LIMITS = (1.0, 2.0, 6.0, 20.0, 22.0, 30.0, 48.0, 64.0, 78.0)


def near(rng, value):
    return rng.choice([value, number(bits(value) + 1), number(bits(value) - 1), value])


def random_actor(rng):
    raw = bytearray(CAPTURED) if rng.random() < 0.3 else bytearray(rng.randrange(256) for _ in range(0x320))
    raw[0x40:0x48] = CAPTURED[0x40:0x48]            # the bone pointers D_00275B40 reads through
    for offset in ACTOR_FLOATS:
        struct.pack_into('<I', raw, offset, finite(rng))
    for i in range(16):
        struct.pack_into('<I', raw, 0xD0 + 4 * i, finite(rng))
    raw[4], raw[5] = rng.choice([1, 1, 2]), rng.choice([0xC, 0xC, 0xC, 9, 0x18])
    raw[7] = rng.choice([0, 0, 1, 2, 3])
    raw[0xD] = rng.choice([0, 0, 1, 2])
    raw[0xF] = rng.choice([0, 0, 0, 0, 2, 0xFD])
    for offset in (0x224, 0x22C):
        struct.pack_into('<I', raw, offset, rng.choice([0, 0, 0, 0, 0x80000000, 1, 0x3F800000]))
    struct.pack_into('<I', raw, 0x200, rng.choice([0, 0x1000, 0x8000, 0x9000, rng.randrange(1 << 32)]))
    struct.pack_into('<f', raw, 0x3C, near(rng, rng.choice(LIMITS)) if rng.random() < 0.7
                     else rng.uniform(-10, 100))
    struct.pack_into('<I', raw, 0x24C, rng.choice([0, 1, 2, 3, 0xFFFFFFFF]))
    raw[0x23F] = rng.randrange(5)
    raw[0x2F1] = rng.choice([0, 1, 0, 1, 2])
    raw[0x235] = rng.randrange(256)
    raw[0x1F1] = rng.choice([1, 1, 3, 5, 7, 0, 2])
    struct.pack_into('<H', raw, 0x28, rng.choice([0, 0, 1, 2, 3, 4, 8, 0xFFFF, 0x8000]))
    if rng.random() < 0.3:
        struct.pack_into('<f', raw, 0xB4, near(rng, 560.0))
    if rng.random() < 0.3:                          # 001809B0's positional window
        struct.pack_into('<f', raw, 0x2E0, rng.choice([near(rng, 120.0), near(rng, 130.0), rng.uniform(119, 131)]))
        struct.pack_into('<f', raw, 0x2E4, rng.choice([near(rng, 250.0), near(rng, 260.0), rng.uniform(249, 261)]))
    struct.pack_into('<i', raw, 0x20C, rng.choice([0xE8, -1, 0x7FFF, rng.randrange(-40000, 40000)]))
    return raw


def random_scene(rng):
    spad = [finite(rng) for _ in range(16)]
    spad3A = [finite(rng) for _ in range(4)]
    area = rng.choice([2, 2, 8, 8, 0xB, 0])
    area_sub = rng.choice([3, 3, 3, 0, 1])
    return (area, area_sub, rng.randrange(256), rng.choice([0, 0x40, 0x4000, 0xFFFF]),
            rng.choice([0x40, 0x40, 0x10]), spad, spad3A)


def load_oracle(ee, raw, scene, node_values, hit):
    ee.reset(); ee.branches = set()
    ee.write(PLAYER, bytes(raw))
    nodes = node_addresses(ee)
    for (node, offset), value in node_values.items():
        ee.save(nodes[node] + offset, value)
    area, area_sub, d8106F2, pad, use_mask, spad, spad3A = scene
    ee.save(0x810700, area, 1); ee.save(0x810701, area_sub, 1); ee.save(0x8106F2, d8106F2, 1)
    ee.save(0x810E74, pad, 2); ee.save(0x70003B76, use_mask, 2)
    for i in range(16): ee.save(SPAD_A0 + 4 * i, spad[i])
    for i in range(4): ee.save(SPAD_3A20 + 4 * i, spad3A[i])
    ee.save(0x700031D0, HIT_REC); ee.save(HIT_REC + 0x1A, hit[0], 1); ee.save(0x700031B4, hit[1])


def random_case(seed):
    rng = random.Random(seed)
    raw = random_actor(rng)
    heavy = rng.random() < 0.4
    raw[6] = rng.choice(HEAVY) if heavy else rng.choice(STATES + (5, 0x30, 0x53, rng.randrange(256)))
    scene = random_scene(rng)
    node_values = {key: finite(rng) for key in NODE_KEYS}
    hit = (rng.choice([0x3D, 0x32, 0x3B, 0x34, 0x10]), finite(rng))
    fault_at = None
    if rng.random() < 0.06:
        fault_at = (rng.choice([n for n in CALLEES.values() if n not in FLOAT_WORKERS]), 0)
    return raw, scene, node_values, hit, fault_at


def compare_run(seed, ee, side, result, got, where_):
    """The shared post-run checks; returns the fault tag or None."""
    fault_at = side.fault_at
    names = [e[0] for e in ee.log]
    if fault_at is not None and fault_at[0] in names:
        k = names.index(fault_at[0])
        assert result == -1, (seed, where_, 'fault not propagated', fault_at)
        assert side.log == ee.log[:k + 1], (seed, where_, 'fault log', side.log, ee.log[:k + 1])
        assert side.snapshots == ee.snapshots[:k + 1], (seed, where_, 'fault: state at an earlier call')
        assert got == ee.snapshots[k], (seed, where_, 'fault bytes',
                                        [hex(i) for i in range(len(got)) if got[i] != ee.snapshots[k][i]][:16])
        return 'fault'
    assert result == 0, (seed, where_, 'native result', result)
    assert side.log == ee.log, (seed, where_, 'callee sequence', side.log, ee.log)
    for k, (mine, theirs) in enumerate(zip(side.snapshots, ee.snapshots)):
        if mine != theirs:
            raise AssertionError((seed, where_, 'state at call', k, ee.log[k][0],
                                  [hex(i) for i in range(len(mine)) if mine[i] != theirs[i]][:16]))
    want = state_bytes(ee)
    if got != want:
        raise AssertionError((seed, where_, 'state differs at', [hex(i) for i in range(len(got)) if got[i] != want[i]][:24]))
    return None


def run_case(seed):
    raw, scene, node_values, hit, fault_at = random_case(seed)
    ee = oracle_ee()
    load_oracle(ee, raw, scene, node_values, hit)
    install_hooks(ee, seed)
    ee.call(STATE, (PLAYER,))
    side = Native(seed, scene, node_values, hit, fault_at)
    live = LiveActor(); C.memmove(live.bytes, bytes(raw), 0x320)
    side.live = live
    result = NATIVE.em_player_ladder_climb_state(C.addressof(side.ladder), C.byref(live))
    tag = compare_run(seed, ee, side, result, side.state(), 'state %#x' % raw[6])
    tags = {tag} if tag else set()
    # Which D_002754D0 / D_002754D4 entry each 0017FC80 request read (all of
    # 001662D0's 0017FC80 calls pass 16.0; sub-state 0x2C's direct read of
    # D_002754D0[0] passes 0.0 and is not counted).
    for entry in ee.log:
        if entry[0] == 'request' and entry[3] == bits(16.0) and entry[1] in TABLE_CLIPS:
            tags.add(TABLE_CLIPS[entry[1]])
    return seed, raw[6], tags, frozenset(ee.branches)


# ------------------------------------------------------ helper cases ------

TABLE_CLIPS = {0xE6: ('table', 0, 'D_002754D0'), 0x100: ('table', 1, 'D_002754D0'),
               0xE7: ('table', 0, 'D_002754D4'), 0x101: ('table', 1, 'D_002754D4')}
HELPERS = ('0017FC80', '0017FD00', '0017FD40', '0017FD80', '0017FE00', '0017FE80', '0017FF00',
           '00180420', '00180460', '00180530', '00180600', '001809B0')
FLOAT_SPECIALS = (0, 0x80000000, 1, 0x80000001, 0x7F7FFFFF, 0xFF7FFFFF, 0x3F800000, 0x41100000,
                  0x41380000, 0xC0000000, 0x7F800000, 0xFF800000)


def helper_case(seed):
    rng = random.Random(seed)
    helper = HELPERS[seed % len(HELPERS)]
    raw = random_actor(rng)
    scene = random_scene(rng)
    node_values = {key: finite(rng) for key in NODE_KEYS}
    hit = (rng.choice([0x3D, 0x32, 0x3B, 0x34, 0x10]), finite(rng))
    side_arg = rng.choice([0, 1, 0, 1, 2, -1])
    blend = rng.choice([finite(rng), 0x41800000, 0x3F800000])
    fs = [rng.choice(FLOAT_SPECIALS) if rng.random() < 0.3 else finite(rng) for _ in range(3)]
    if forced_window(seed):
        scene = (8, 3) + scene[2:]
        struct.pack_into('<f', raw, 0x2E0, rng.choice([near(rng, 120.0), near(rng, 130.0), 125.0]))
        struct.pack_into('<f', raw, 0x2E4, rng.choice([near(rng, 250.0), near(rng, 260.0), 255.0]))
    fault_at = None
    if rng.random() < 0.05 and not forced_window(seed):
        fault_at = (rng.choice(['request', 'transform', 'sweep', 'probe', 'column', 'wall', 'sweep_box',
                                'hit_point', 'dash', 'grab', 'hit_react', 'ledge_ahead', 'grab_check']), 0)
    ee = oracle_ee()
    load_oracle(ee, raw, scene, node_values, hit)
    install_hooks(ee, seed)
    entry = int(helper, 16)
    if helper in ('0017FC80', '0017FD00', '0017FD40'):
        ee.call(entry, (PLAYER,), (number(blend),))
    elif helper in ('0017FD80', '0017FE00', '0017FE80', '0017FF00'):
        ee.call(entry, (PLAYER, side_arg), (number(blend),))
    elif helper == '00180600':
        ee.call(entry, (PLAYER, side_arg), tuple(number(x) for x in fs))
    elif helper == '001809B0':
        ee.call(entry, (PLAYER, side_arg))
    else:
        ee.call(entry, (PLAYER, 1))
    want_result = s32(ee.r[2])
    side = Native(seed, scene, node_values, hit, fault_at)
    live = LiveActor(); C.memmove(live.bytes, bytes(raw), 0x320)
    side.live = live
    fn = getattr(NATIVE, 'em_player_ladder_climb_' + helper)
    out = C.c_int(0x5A5A)
    L = C.byref(side.ladder)
    if helper in ('0017FC80', '0017FD00', '0017FD40'):
        result = fn(L, C.byref(live), number(blend))
    elif helper in ('0017FD80', '0017FE00', '0017FE80', '0017FF00'):
        result = fn(L, C.byref(live), side_arg, number(blend))
    elif helper == '00180420':
        result = fn(L, C.byref(live))
    elif helper == '00180600':
        result = fn(L, C.byref(live), side_arg, *[number(x) for x in fs], C.byref(out))
    elif helper == '001809B0':
        result = fn(L, C.byref(live), side_arg, C.byref(out))
    else:
        result = fn(L, C.byref(live), C.byref(out))
    tag = compare_run(seed, ee, side, result, side.state(), helper)
    if tag is None and helper in ('00180460', '00180530', '00180600', '001809B0'):
        assert out.value == want_result, (seed, helper, 'return value', out.value, want_result)
    return helper, tag, (out.value if tag is None and helper in ('00180460', '00180530', '00180600', '001809B0')
                         else None), frozenset(ee.branches)


def refusal_checks():
    """A missing worker or scene refuses before any write."""
    live = LiveActor(); raw = bytes(range(256)) * 3 + bytes(0x20); C.memmove(live.bytes, raw, 0x320)
    rng = random.Random(7)
    side = Native(1, random_scene(rng), {k: 0 for k in NODE_KEYS}, (0x3D, 0))
    side.live = live
    before = side.state()
    for field, _ in WORKER_FIELDS[1:]:
        saved = getattr(side.workers, field)
        setattr(side.workers, field, type(saved)())
        assert NATIVE.em_player_ladder_climb_state(C.addressof(side.ladder), C.byref(live)) == -1, field
        assert side.state() == before, ('refusal wrote', field)
        setattr(side.workers, field, saved)
    no_scene = Ladder(C.pointer(side.workers), C.POINTER(LadderScene)())
    assert NATIVE.em_player_ladder_climb_state(C.addressof(no_scene), C.byref(live)) == -1
    assert NATIVE.em_player_ladder_climb_state(None, C.byref(live)) == -1
    assert side.state() == before
    return len(WORKER_FIELDS) - 1


# -------------------------------------------------------------- route -----

class RouteNative(Native):
    """Workers for the route replay: every callee returns 0 and writes
    nothing, except steer_input (+24C = 0: the stick held up, as the climb
    continued), the probe 00180460 reaches (its result is taken from the
    trace) and request, which is recorded."""

    def __init__(self):
        super().__init__(0, (0xB, 0, 0, 0, 0x40, [0] * 16, [0] * 4), {k: 0 for k in NODE_KEYS}, (0, 0))
        self.probe_result = 0
        self.requests = []
        w = self.workers
        def zero(*args): return 0
        def result_zero(*args):
            args[-1][0] = 0; return 0
        for field, kind in WORKER_FIELDS[4:]:
            if field in FLOAT_WORKERS:
                setattr(w, field, self._keep(kind(lambda *a: 0)))
            elif kind in (ACT_R_FN, ACT_I_R_FN, FRAMES_FN, PROBE_FN, COLUMN_FN, SWEEP_FN, BOX_FN, PAIR_FN, AHEAD_FN):
                setattr(w, field, self._keep(kind(result_zero)))
            else:
                setattr(w, field, self._keep(kind(zero)))
        def steer(_, a):
            for i in range(4): a.contents.bytes[0x24C + i] = 0
            return 0
        def probe(_, a, at, kind, r):
            r[0] = self.probe_result; return 0
        def request(_, a, clip, flags, blend):
            self.requests.append((clip, flags, blend)); return 0
        w.steer_input = self._keep(ACT_FN(steer))
        w.probe = self._keep(PROBE_FN(probe))
        w.request = self._keep(CALL_FN(request))


def route_check():
    """Replay each beat-10 cage climb through the native state and compare the
    clip requests with the captured clip (+20C) and +3C sequence."""
    if not ROUTE_TRACE.exists():
        return None
    rows = json.loads(ROUTE_TRACE.read_text())['rows']
    climbs, current = [], []
    for row in rows:
        if row['p5'] == 0xC and row['m1F0'] in (0x17, 0x18):
            current.append(row)
        elif current:
            climbs.append(current); current = []
    if current: climbs.append(current)
    checked = []
    for climb in climbs:
        # The captured sequence: each new clip with +3C and +1F0 on its first row.
        seq = []
        for row in climb:
            if not seq or seq[-1][0] != row['clip']:
                seq.append((row['clip'], row['clock'], row['m1F0'], row['f']))
        assert seq[0][0] in (0xE6, 0xE7, 0x100, 0x101), ('climb does not start on the 0017FC80 clip', seq[0])
        cycles = [s for s in seq[1:] if s[0] in (0xE8, 0xEA)]
        top = [s for s in seq[1:] if s[0] not in (0xE8, 0xEA)]
        assert top and top[0][0] == 0xF0, ('climb does not end at clip 0xF0', seq)
        side = RouteNative()
        live = LiveActor(); side.live = live
        raw = bytearray(0x320)
        raw[4], raw[5], raw[0x2F1], raw[0x1F0], raw[0xD] = 1, 0xC, 0, 0x17, 0
        C.memmove(live.bytes, bytes(raw), 0x320)
        call = lambda: NATIVE.em_player_ladder_climb_state(C.addressof(side.ladder), C.byref(live))
        # Sub-state 2: the first cycle clip (0017FD00(p, 4.0)).
        live.bytes[6] = 2
        assert call() == 0
        produced = [side.requests[-1]]
        # Each clip end in sub-state 4: 00180460 (probe) -> 0 continues,
        # 2 with +D == 0 goes to 0x14, whose next tick requests 0xF0.
        for k in range(len(cycles)):
            live.bytes[6] = 4
            struct.pack_into('<I', live.bytes, 0x200, 0x1000)
            side.probe_result = 0 if k + 1 < len(cycles) else 2
            n = len(side.requests)
            assert call() == 0
            if side.probe_result == 0:
                assert len(side.requests) == n + 1, 'no cycle clip'
                produced.append(side.requests[-1])
            else:
                assert len(side.requests) == n and live.bytes[6] == 0x14, 'top not reached'
                assert call() == 0
                produced.append(side.requests[-1])
                assert live.bytes[0x1F0] == 0x18 and live.bytes[6] == 0x15
        want = [(clip, clock) for clip, clock, _, _ in cycles + top[:1]]
        got = [(clip, number(bits(blend))) for clip, flags, blend in produced]
        assert got == want, ('route clips / blends', got, want)
        assert [m for _, _, m, _ in cycles] == [0x17] * len(cycles) and top[0][2] == 0x18, 'route +1F0'
        checked.append((climb[0]['f'], len(cycles)))
    return checked


# ---------------------------------------------------------------- main -----

def main():
    global RAM, ELF, NATIVE, CAPTURED
    ELF = read_elf()
    if not RAM_PATH.exists():
        raise SystemExit('missing %s (the captured AREA11 RAM; docs/PLAYER_LADDER_CLIMB.md)' % RAM_PATH)
    RAM = RAM_PATH.read_bytes()
    for address, size in list(ROUTINES.items()) + list(TABLES):
        at = address - 0x100000 + 0x300
        assert RAM[address:address + size] == ELF[at:at + size], ('captured RAM differs from the ELF', hex(address))
    CAPTURED = RAM[PLAYER:PLAYER + 0x320]
    NATIVE = build_native()

    count = reference_mode.pick(400000, 14000)
    results = reference_mode.parallel_map(run_case, [0x1662D0 * 7 + i for i in range(count)])
    branches, tags, per_state = set(), set(), {}
    for seed, st, case_tags, case_branches in results:
        branches |= case_branches; tags |= case_tags
        per_state[st] = per_state.get(st, 0) + 1
    helper_count = reference_mode.pick(100000, 5000)
    helpers = reference_mode.parallel_map(helper_case, [HELPER_BASE + i for i in range(helper_count)])
    helper_results = {}
    for helper, tag, value, case_branches in helpers:
        branches |= case_branches
        helper_results.setdefault(helper, set()).add(value)
        if tag: tags.add(tag)

    wanted = [(pc, taken) for start, end in COVERED
              for pc in conditional_branches(RAM, start, end) for taken in (False, True)]
    missing = [(hex(pc), taken) for pc, taken in wanted if (pc, taken) not in branches]
    assert not missing, ('branch outcomes never exercised', missing)
    for state in STATES:
        assert per_state.get(state, 0) > 0, ('sub-state never run', hex(state))
    for row in (0, 1):
        for table in ('D_002754D0', 'D_002754D4'):
            assert ('table', row, table) in tags, (table, 'entry never read', row)
    assert 'fault' in tags, 'no worker fault propagated'
    assert helper_results['001809B0'] >= {0, 1, 2, 3, 5, 7}, ('001809B0 results', helper_results['001809B0'])
    assert helper_results['00180460'] >= {0, 1, 2, 3} and helper_results['00180530'] >= {0, 1, 2}
    workers = refusal_checks()
    route = route_check()
    reference_mode.banner(reference_mode.part(len(results), 400000, '001662D0 cases'),
                          reference_mode.part(len(helpers), 100000, 'helper cases'))
    faults = sum(1 for _, _, case_tags, _ in results if 'fault' in case_tags)
    route_text = ('route beat 10: %d climbs replayed (%s cycle clips, clips and blends as captured)'
                  % (len(route), '/'.join(str(n) for _, n in route))) if route else 'route trace absent'
    print('player ladder climb reference: PASS -- %d 001662D0 cases over the %d handled sub-states and %d '
          'default-path +6 values and %d helper cases: all 0x320 actor bytes, the scratchpad words and '
          'D_008106F2 after the call and at every callee entry, and every callee call with its arguments, '
          'identical; %d/%d conditional branch outcomes of the %d executed routines exercised; both rows of '
          'D_002754D0 / D_002754D4 read; %d worker faults stop at the original bytes; %d missing-worker '
          'refusals; %s'
          % (len(results), len(STATES), len(set(per_state) - set(STATES)), len(helpers),
             len(wanted) - len(missing), len(wanted), len(ROUTINES), faults, workers, route_text))


if __name__ == '__main__':
    main()
