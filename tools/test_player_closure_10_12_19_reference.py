#!/usr/bin/env python3
"""Execute the original +5 0x10 / 0x12 / 0x19 / 0x1A player states and
compare em_player_closure_10_12_19.c.

docs/PLAYER_CLOSURE_10_12_19.md. The original instructions run from the
captured AREA11 EE RAM (../Extermination/build/startup-reference/
playable_ee.bin, state 04), whose code and table bytes are first checked
against the user's pinned ELF; none are embedded here. The interpreter is
the shared EE of tools/test_player_slide_reference.py, subclassed here
(ClosureEE) so that every COP1 operation goes through tools/ee_float_model.py
(the measured EE rules, docs/EE_FLOAT_MODEL.md). No VU0 macro operation runs
unhooked (any would be refused). The shared files are not edited.

Executed, unmodified (the only code the interpreter may run; any other
address faults the case):
  00169730 0016AE40 0016DE40 0016EBA0  the four state routines
  001696A0 0016ADE0 0016A4B0 00181A70 00181950 001814E0 00181730 001818D0
  00181B80 00181BA0 001787B0 00181E20 00181F60 00182090 00182100 00175390
  001811F0 00181430 00179010 00179910 001B0B50 0016A8B0  their callees
  00179880 00181D70 001823E0  (em_player_fall.c / em_player_major2.c)
  00102948 001031E0 0011DF78  the copy and fabs leaves

Every other callee is hooked, scripted per case and recorded (never
simulated as a claim about the callee): the scripted return value and the
scripted record / node / scratch writes are applied identically on both
sides, and the call sequence with every argument (floats as bits, vectors
and matrices as the words the callee receives) must match. After each case
all 0x320 record bytes, the scene globals and the scratchpad words must
match, and so must the same three images at entry to every worker call
(a store moved across a call fails). Worker faults are injected on the
native side and must stop at the original's bytes.

Branch coverage: every conditional branch of the executed routines must be
seen both taken and not taken, except the listed outcomes that cannot occur
(IMPOSSIBLE, each with its reason).

The player record sits at its captured address 0x8102B0. D_00275B40 there
is the player's own bone array at +0x40, and the bone pointer words at
+0x110..+0x163 keep their captured values, so the node words the routines
read (*(*D_00275B40) + 0 / 8 and *(p+15C / 160) + C0 / C8) are case
variables written at the captured node records.

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
from test_player_slide_reference import EE, read_elf, bits, number, s32, sx32, REFERENCE  # noqa: E402
from test_player_floor_reference import LiveActor, cached_build  # noqa: E402
import ee_float_model as M  # noqa: E402
import reference_mode  # noqa: E402

RAM_PATH = REFERENCE / 'playable_ee.bin'
PLAYER = 0x8102B0
NODE = 0x70002000            # the probe hit's node record (a private scratchpad area)
STATE_ENTRY = {0x10: 0x169730, 0x12: 0x16AE40, 0x19: 0x16DE40, 0x1A: 0x16EBA0}
NATIVE_ENTRY = {0x10: 'em_player_closure1019_00169730', 0x12: 'em_player_closure1019_0016AE40',
                0x19: 'em_player_closure1019_0016DE40', 0x1A: 'em_player_closure1019_0016EBA0'}
# Executed original routines: address -> size (bytes).
EXECUTED = {
    0x169730: 0xD78, 0x16AE40: 0x948, 0x16DE40: 0xD5C, 0x16EBA0: 0x3AC,
    0x1696A0: 0x8C, 0x16ADE0: 0x60, 0x16A4B0: 0x3FC, 0x181A70: 0x10C, 0x181950: 0x118,
    0x1814E0: 0x248, 0x181730: 0x1A0, 0x1818D0: 0x80, 0x181B80: 0x14, 0x181BA0: 0x1CC,
    0x1787B0: 0x154, 0x181E20: 0x140, 0x181F60: 0x128, 0x182090: 0x64, 0x182100: 0xD4,
    0x175390: 0x148, 0x1811F0: 0x23C, 0x181430: 0xAC, 0x179010: 0x98, 0x179910: 0x280,
    0x1B0B50: 0x50, 0x16A8B0: 0x394, 0x179880: 0x50, 0x181D70: 0xA8, 0x1823E0: 0x44,
    0x102948: 0xC, 0x1031E0: 0x1C, 0x11DF78: 0x1C,
}
# Tables the executed code reads (checked against the ELF): D_002488B0,
# D_00248630, D_00248950, D_002754B8, and the area-2 row of D_0024D650.
TABLES = ((0x2488B0, 4), (0x248630, 16), (0x248640, 16), (0x248950, 12), (0x2754B8, 8), (0x24D650, 16),
          (0x24D610, 12))

# Original callee address -> worker name.
CALLEES = {
    0x1749A0: 'request', 0x1885B0: 'clip_885B0', 0x188610: 'clip_88610', 0x188550: 'clip_88550',
    0x1751A0: 'stick', 0x174FD0: 'steer', 0x175900: 'floor', 0x178B90: 'translate',
    0x179B90: 'random5', 0x1FBD50: 'sound', 0x1FB9F0: 'sound_1FB9F0', 0x1B1470: 'wrap',
    0x1B12B0: 'approach', 0x11DE90: 'cosine', 0x11E2A8: 'sine', 0x11E620: 'atan2',
    0x11E748: 'sqrt', 0x1281C0: 'to_int', 0x1C94B0: 'trs', 0x1026A0: 'apply', 0x1028B8: 'vadd',
    0x1029C0: 'identity', 0x102BB0: 'rotate_y', 0x102918: 'translate_m', 0x19A570: 'segment',
    0x19AD00: 'move', 0x19AFE0: 'sweep', 0x19AB20: 'ground', 0x19A310: 'slope',
    0x179450: 'floor_query', 0x179150: 'w00179150', 0x199DB0: 'midpoint', 0x1782A0: 'ledge_top',
    0x1C6DA0: 'skeleton', 0x1B0460: 'script_1B0460', 0x1AEDE0: 'fade', 0x1AEE10: 'fade_1AEE10',
    0x184BA0: 'use', 0x176F90: 'use_probe', 0x1749F0: 'arbiter', 0x102B08: 'rotate_x', 0x17C580: 'land',
    0x21D250: 'surface5d', 0x21D2E0: 'teleport', 0x182870: 'land_sound',
    0x182A70: 'sound_182A70', 0x17FC80: 'clip_FC80',
}
FLOAT_WORKERS = {'cosine', 'sine', 'atan2', 'sqrt'}
BITS_WORKERS = {'wrap', 'approach'}
PROBES = {'segment', 'move', 'sweep', 'ground'}

# Scene globals: (field, address, size).
SCENE = [('pad_held', 0x810E70, 2), ('pad_pressed', 0x810E74, 2), ('use_mask', 0x70003B76, 2),
         ('mask_3B7C', 0x70003B7C, 2), ('mask_3B7E', 0x70003B7E, 2), ('fade', 0x28A9A0, 2),
         ('area', 0x810700, 1), ('sub_area', 0x810701, 1), ('zone', 0x810702, 1),
         ('d8106BE', 0x8106BE, 1), ('pad_gait', 0x810E57, 1), ('pad_x', 0x810E64, 1),
         ('pad_y', 0x810E65, 1), ('area_flags2', 0x810732, 1), ('d8106B5', 0x8106B5, 1),
         ('d8106B6', 0x8106B6, 1), ('d8106B7', 0x8106B7, 1), ('d8106B8', 0x8106B8, 1),
         ('d8106C8', 0x8106C8, 4), ('camera_yaw', 0x8106A0, 4), ('spad31E4', 0x700031E4, 4),
         ('d275B10', 0x275B10, 4), ('d275B0C', 0x275B0C, 4), ('d281B64', 0x281B64, 4)]
D275B14 = 0x275B14
SCRATCH = ([('s3A20', 0x70003A20), ('s3A24', 0x70003A24), ('s3A28', 0x70003A28)] +
           [('s36A0', 0x700036A0 + 4 * i, i) for i in range(16)] +
           [(name, base + 4 * i, i) for name, base in (('s38A0', 0x700038A0), ('s38B0', 0x700038B0),
                                                      ('s38C0', 0x700038C0), ('s38D0', 0x700038D0))
            for i in range(4)])

# ------------------------------------------------------------------ EE ----


class ClosureEE(EE):
    """The shared EE with COP1 routed through the measured model (any COP1
    or VU0 operation outside it is refused), restricted to the EXECUTED
    routines, recording every conditional branch outcome."""

    def __init__(self, elf, ram):
        super().__init__(elf, ram)
        self.acc_bits = 0
        self.branches = set()
        self.allowed = {pc for start, size in EXECUTED.items() for pc in range(start, start + size, 4)}

    def reset(self):
        self.r = [0] * 32; self.rh = [0] * 32; self.hi = self.lo = 0
        self.f = [0] * 32; self.acc_bits = 0; self.cond = False
        self.vf = [[0, 0, 0, 0] for _ in range(32)]; self.vf[0][3] = bits(1.0)
        self.r[28] = 0x27D370; self.r[29] = 0x7F0F0000
        self.log = []
        self.branches = set()

    def execute(self, word, pc):
        if pc not in self.allowed:
            raise AssertionError(('unhooked code outside the executed routines', hex(pc)))
        return super().execute(word, pc)

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
        elif fn == 26: self.acc_bits = M.ee_mul(a, b)            # mula.s
        elif fn == 28: f[fd] = M.ee_madd(self.acc_bits, a, b)
        elif fn == 36: f[fd] = M.ee_cvt_w_s(a)
        elif fn == 50: self.cond = bool(M.ee_c_eq(a, b))
        elif fn == 52: self.cond = bool(M.ee_c_lt(a, b))
        elif fn == 54: self.cond = bool(M.ee_c_le(a, b))
        else:
            raise M.UnmeasuredCase(('COP1 op outside this oracle', fn, hex(pc)))

    def macro(self, word):
        raise M.UnmeasuredCase(('VU0 op outside this oracle', hex(word)))

    def branch(self, word, pc):
        outcome = super().branch(word, pc)
        if outcome is not None:
            if pc not in self.allowed:
                raise AssertionError(('branch outside the executed routines', hex(pc)))
            self.branches.add((pc, outcome[0]))
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


# Branch outcomes that cannot occur, with the reason (checked by address).
IMPOSSIBLE = {
    # 00169730 +6 0x14 phase 2: 001814E0 returns 2 only from its +23F == 3
    # retry, and nothing writes +23F between that read and this test.
    (0x169BB0, True): '+23F is 3 whenever 001814E0 returned 2',
    # 00175390: bltz on a zero-extended byte (lbu) is never taken.
    (0x1753D4, True): 'lbu value is never negative',
    (0x175424, True): 'lbu value is never negative',
}

# ------------------------------------------------------- native side -----


class Scene(C.Structure):
    _fields_ = [(name, {1: C.c_uint8, 2: C.c_uint16, 4: C.c_uint32}[size]) for name, _, size in SCENE]


SCENE_FIELDS = [name for name, _, _ in SCENE]


class Scratch(C.Structure):
    _fields_ = [('s3A20', C.c_uint32), ('s3A24', C.c_uint32), ('s3A28', C.c_uint32),
                ('s36A0', C.c_uint32 * 16), ('s38A0', C.c_uint32 * 4), ('s38B0', C.c_uint32 * 4),
                ('s38C0', C.c_uint32 * 4), ('s38D0', C.c_uint32 * 4)]


class Major2Scene(C.Structure):
    _fields_ = [('d8106F1', C.c_uint8), ('d810707', C.c_uint8), ('d275B14', C.c_int32)]


class ProbeHit(C.Structure):
    _fields_ = [('kind', C.c_int), ('node', C.c_uint16), ('entity_flags', C.c_uint8),
                ('entity_type', C.c_uint8), ('entity', C.c_int), ('point', C.c_float * 3),
                ('delta', C.c_float * 3), ('normal', C.c_float * 3), ('axis', C.c_float * 3),
                ('owner', C.c_void_p)]


A = C.POINTER(LiveActor)
U32P, IP, I32P = C.POINTER(C.c_uint32), C.POINTER(C.c_int), C.POINTER(C.c_int32)
HP = C.POINTER(ProbeHit)
VP = C.c_void_p
F = C.CFUNCTYPE
PROTOS = [
    ('request', F(C.c_int, VP, A, C.c_int, C.c_int, C.c_float)),
    ('clip_885B0', F(C.c_int, VP, A, IP)), ('clip_88610', F(C.c_int, VP, A, IP)),
    ('clip_88550', F(C.c_int, VP, A, IP)),
    ('stick', F(C.c_int, VP, A)), ('steer', F(C.c_int, VP, A)),
    ('floor', F(C.c_int, VP, A, C.c_int, IP)), ('translate', F(C.c_int, VP, A, C.c_int)),
    ('random5', F(C.c_int, VP, IP)),
    ('sound', F(C.c_int, VP, A, C.c_int, C.c_int, C.c_float)),
    ('sound_1FB9F0', F(C.c_int, VP, C.c_int, C.c_int, C.c_int, C.c_int)),
    ('wrap', F(C.c_int, VP, C.c_uint32, U32P)),
    ('approach', F(C.c_int, VP, C.c_uint32, C.c_uint32, C.c_uint32, U32P)),
    ('cosine', F(C.c_float, VP, C.c_float)), ('sine', F(C.c_float, VP, C.c_float)),
    ('atan2', F(C.c_float, VP, C.c_float, C.c_float)), ('sqrt', F(C.c_float, VP, C.c_float)),
    ('to_int', F(C.c_int, VP, C.c_uint32, I32P)),
    ('trs', F(C.c_int, VP, U32P, U32P, U32P, U32P)),
    ('apply', F(C.c_int, VP, U32P, U32P, U32P)), ('vadd', F(C.c_int, VP, U32P, U32P, U32P)),
    ('identity', F(C.c_int, VP, U32P)), ('rotate_y', F(C.c_int, VP, U32P, U32P, C.c_uint32)),
    ('translate_m', F(C.c_int, VP, U32P, U32P, U32P)),
    ('segment', F(C.c_int, VP, U32P, U32P, C.c_uint, C.c_int, IP, HP)),
    ('move', F(C.c_int, VP, A, U32P, C.c_uint, IP, HP)),
    ('sweep', F(C.c_int, VP, A, U32P, U32P, C.c_uint, IP, HP)),
    ('ground', F(C.c_int, VP, A, U32P, U32P, C.c_uint, IP, HP)),
    ('slope', F(C.c_int, VP, HP, U32P)),
    ('floor_query', F(C.c_int, VP, A, U32P, IP)), ('w00179150', F(C.c_int, VP, A)),
    ('midpoint', F(C.c_int, VP, U32P, IP)), ('ledge_top', F(C.c_int, VP, A, IP)),
    ('area_point', F(C.c_int, VP, C.c_int, C.c_int, C.c_uint, U32P)),
    ('skeleton', F(C.c_int, VP, A)), ('script_1B0460', F(C.c_int, VP, C.c_int)),
    ('fade', F(C.c_int, VP, C.c_int, C.c_int)), ('fade_1AEE10', F(C.c_int, VP, C.c_int, C.c_int)),
    ('use', F(C.c_int, VP, A, IP)), ('use_probe', F(C.c_int, VP, A, IP)),
    ('arbiter', F(C.c_int, VP, A, C.c_int, C.c_float, C.c_float)),
    ('rotate_x', F(C.c_int, VP, U32P, U32P, C.c_uint32)), ('land', F(C.c_int, VP, A)),
    ('surface5d', F(C.c_int, VP, A, C.c_int)), ('teleport', F(C.c_int, VP, A, C.c_int, C.c_int)),
    ('land_sound', F(C.c_int, VP, A, C.c_int)), ('sound_182A70', F(C.c_int, VP, A)),
    ('clip_FC80', F(C.c_int, VP, A, C.c_float)),
    ('root_node', F(C.c_int, VP, C.c_uint, U32P)),
    ('bone', F(C.c_int, VP, A, C.c_uint, C.c_uint, U32P)),
]
PROTO = dict(PROTOS)


class Workers(C.Structure):
    _fields_ = [('context', VP)] + PROTOS


class Closure(C.Structure):
    _fields_ = [('workers', C.POINTER(Workers)), ('scene', C.POINTER(Scene)),
                ('scratch', C.POINTER(Scratch)), ('major2', C.POINTER(Major2Scene))]


# ------------------------------------------------------------ scripts ----


def finite(rng, lo=-400.0, hi=400.0):
    """A finite binary32 word: signed zeros, denormals (zero under DAZ), unit
    values, or a uniform value in the working range."""
    if rng.random() < 0.12:
        return rng.choice([0, 0x80000000, 1, 0x80000001, 0x3F800000, 0xBF800000])
    return bits(rng.uniform(lo, hi))


def fword(value):
    return bits(value) if isinstance(value, float) else value


ATTRIBUTES = (0x34, 0x1E, 0x36, 0x32, 0x37, 0x05, 0x00)


def effect(seed, name, index, entry):
    """The scripted result of the index-th call of `name` in case `seed`:
    a dict with 'ret' (int, or float bits), 'writes' [(record offset, size,
    value)], 'mem' {address: word} (node words, 0x70003A20), 'out' words,
    'hit' (node halfword, point, normal, axis words)."""
    rng = random.Random(zlib.crc32(('%d/%s/%d' % (seed, name, index)).encode()))
    e = {'ret': 0, 'writes': [], 'mem': {}, 'out': None, 'hit': None}
    w = e['writes']

    def maybe(p, offset, size, value):
        if rng.random() < p: w.append((offset, size, value))
    if name == 'request':
        maybe(0.35, 0x200, 4, rng.choice([0, 0x1000, 0x8000, 0x9000]))
        if rng.random() < 0.1: e['mem'][0x70003A20] = finite(rng, -20, 20)
    elif name in ('clip_885B0', 'clip_88610', 'clip_88550'):
        e['ret'] = rng.choice([0x6D, 0x7B, 0xB3, 0xBC, 0x14B, rng.randrange(0x60, 0x160)])
    elif name == 'stick':
        maybe(0.8, 0x24C, 4, rng.choice([0, 0, 1, 2, 2, 3, 3, 0xFFFFFFFF]))
        maybe(0.3, 0x23F, 1, rng.randrange(4))
    elif name == 'steer':
        maybe(0.8, 0x24C, 4, rng.choice([0, 1, 2, 3, 4]))
        maybe(0.5, 0x23F, 1, rng.randrange(4))
    elif name == 'floor':
        e['ret'] = rng.choice([0, 0, 1, 0x81])
        maybe(0.3, 0xB4, 4, finite(rng)); maybe(0.3, 0x23A, 1, rng.choice([0x5D, 0x5D, 5, 0]))
        maybe(0.2, 0xA, 1, rng.choice([0, 1]))
    elif name == 'translate':
        maybe(0.3, 0xB0, 4, finite(rng)); maybe(0.3, 0xB4, 4, finite(rng)); maybe(0.2, 0xB8, 4, finite(rng))
        maybe(0.2, 0x38, 4, finite(rng, -2, 2))
        if rng.random() < 0.3: e['mem']['root8'] = finite(rng)
    elif name == 'random5':
        e['ret'] = rng.randrange(5)
    elif name == 'sound':
        e['ret'] = rng.choice([0, 3, -1 & 0xFFFFFFFF])
        if rng.random() < 0.1: e['mem'][0x70003A20] = finite(rng, -20, 20)
    elif name == 'wrap':
        e['ret'] = fword(rng.choice([0.0, -0.0, 1e-30, -1e-30, rng.uniform(-3.2, 3.2),
                                     rng.uniform(-3.2, 3.2), rng.uniform(-3.2, 3.2)]))
    elif name == 'approach':
        e['ret'] = entry[1] if rng.random() < 0.45 else bits(rng.uniform(-3.2, 3.2))
    elif name in ('cosine', 'sine'):
        e['ret'] = bits(rng.uniform(-1.0, 1.0))
    elif name == 'atan2':
        e['ret'] = bits(rng.uniform(-3.2, 3.2))
    elif name == 'sqrt':
        e['ret'] = bits(rng.choice([0.0, rng.uniform(0.0, 20.0), rng.uniform(0.0, 400.0)]))
    elif name == 'to_int':
        e['ret'] = rng.choice([12, 0, 11, rng.randrange(-5, 40) & 0xFFFFFFFF])
    elif name == 'trs':
        e['out'] = [finite(rng) for _ in range(16)]
    elif name in ('apply', 'vadd'):
        e['out'] = [finite(rng) for _ in range(4)]
    elif name in ('identity', 'rotate_y', 'rotate_x', 'translate_m'):
        e['out'] = [finite(rng) for _ in range(16)]
    elif name in PROBES:
        if name == 'ground':
            e['ret'] = 1 if index >= 4 else rng.choice([0, 0, 0, 1, 2])
        elif name == 'sweep':
            e['ret'] = rng.choice([0, 2, 4, 6, 1, 8])
        else:
            e['ret'] = rng.choice([0, 0, 1, 1, 2, 4])
        attributes = ATTRIBUTES + ((0x37, 0x37, 0x37) if name == 'ground' else (0x34, 0x1E, 0x36, 0x32))
        e['hit'] = (0xFF00 | rng.choice(attributes) if rng.random() < 0.85 else rng.randrange(0x10000),
                    [finite(rng) for _ in range(3)], [finite(rng, -1, 1) for _ in range(3)],
                    [finite(rng, -1, 1) for _ in range(3)])
    elif name == 'slope':
        e['out'] = [finite(rng, -1.6, 1.6)]
    elif name == 'floor_query':
        e['ret'] = rng.choice([0, 1, 1, 2])
        if rng.random() < 0.8:
            w.append((0x258, 4, rng.choice([bits(-24.0), bits(-24.000002), bits(-23.999998), bits(-4.01),
                                            0xC08051EB, 0xC08051ED, bits(-30.0), bits(-10.0), 0,
                                            bits(-2.0)])))
    elif name == 'w00179150':
        maybe(0.3, 0xB0, 4, finite(rng)); maybe(0.3, 0xB8, 4, finite(rng))
    elif name == 'midpoint':
        e['ret'] = rng.choice([0, 1, 1])
        e['out'] = [finite(rng) for _ in range(3)]
    elif name == 'ledge_top':
        e['ret'] = rng.choice([0, 1, 1])
    elif name == 'use':
        e['ret'] = rng.choice([0, 0, 1])
    elif name == 'use_probe':
        e['ret'] = rng.choice([0x1F, 0x1F, 0, 5])
    elif name == 'skeleton':
        if rng.random() < 0.3: e['mem']['root8'] = finite(rng)
    elif name == 'arbiter':
        maybe(0.2, 0x200, 4, rng.choice([0, 0x1000]))
    elif name in ('land', 'surface5d', 'teleport', 'land_sound', 'sound_182A70', 'clip_FC80'):
        maybe(0.3, 6, 1, rng.randrange(0x64)); maybe(0.2, 5, 1, rng.randrange(0x26))
    return e


# ------------------------------------------------------------- oracle ----

RAM = ELF = NATIVE = None
CAPTURED = None
_EE = None


def oracle_ee():
    global _EE
    if _EE is None:
        _EE = ClosureEE(ELF, RAM)
    return _EE


def words(ee, address, count):
    return tuple(ee.load(address + 4 * i) for i in range(count))


def node_addresses(ee):
    """The node words the routines load: root +0 / +8, bone slots."""
    node0 = ee.load(ee.load(0x275B40))
    out = {'root0': node0, 'root8': node0 + 8}
    for slot in (0x15C, 0x160):
        base = ee.load(PLAYER + slot)
        out[(slot, 0xC0)] = base + 0xC0
        out[(slot, 0xC8)] = base + 0xC8
    return out


def area_records(ee):
    table = ee.load(ee.load(0x24D650 + 8))      # D_0024D650[2]
    return {sub: ee.load(ee.load(0x24D650 + 8) + 4 * sub) for sub in range(3)} if table else {}


def image(ee):
    """(record bytes, scene words, scratch words) as the oracle has them."""
    scene = tuple(ee.load(address, size) for _, address, size in SCENE) + (ee.load(D275B14),)
    scratch = tuple(ee.load(item[1]) for item in SCRATCH)
    return bytes(ee.read(PLAYER, 0x320)), scene, scratch


def install_hooks(ee, seed, addresses):
    counters = {}
    ee.snapshots = []

    def hook(name):
        def run(o):
            index = counters.get(name, 0); counters[name] = index + 1
            o.snapshots.append(image(o))
            a = [o.r[4 + i] & 0xFFFFFFFF for i in range(4)]
            if name in ('request', 'clip_885B0', 'clip_88610', 'clip_88550', 'stick', 'steer', 'floor',
                        'translate', 'sound', 'move', 'sweep', 'ground', 'floor_query', 'w00179150',
                        'ledge_top', 'skeleton', 'use', 'use_probe', 'arbiter', 'land', 'surface5d',
                        'teleport', 'land_sound', 'sound_182A70', 'clip_FC80'):
                assert a[0] == PLAYER, (name, hex(a[0]))
            if name == 'request':
                entry = (name, s32(a[1]), s32(a[2]), o.f[12])
            elif name in ('floor', 'translate', 'land_sound', 'surface5d'):
                entry = (name, s32(a[1]))
            elif name == 'teleport':
                entry = (name, s32(a[1]), s32(a[2]))
            elif name == 'sound':
                entry = (name, s32(a[1]), s32(a[2]), o.f[12])
            elif name == 'sound_1FB9F0':
                entry = (name, s32(a[0]), s32(a[1]), s32(a[2]), s32(a[3]))
            elif name in ('wrap', 'cosine', 'sine', 'sqrt', 'to_int'):
                entry = (name, o.f[12])
            elif name == 'approach':
                entry = (name, o.f[12], o.f[13], o.f[14])
            elif name == 'atan2':
                entry = (name, o.f[12], o.f[13])
            elif name == 'trs':
                assert a == [PLAYER + 0xD0, PLAYER + 0xB0, PLAYER + 0xC0, PLAYER + 0x60], 'trs operands'
                entry = (name, words(o, a[1], 3), words(o, a[2], 3), words(o, a[3], 3))
            elif name == 'apply':
                entry = (name, words(o, a[1], 16), words(o, a[2], 4))
            elif name == 'vadd':
                entry = (name, words(o, a[1], 4), words(o, a[2], 4))
            elif name == 'identity':
                assert a[0] == 0x700036A0, 'identity out'
                entry = (name,)
            elif name in ('rotate_y', 'rotate_x'):
                entry = (name, words(o, a[1], 16), o.f[12])
            elif name == 'arbiter':
                entry = (name, s32(a[1]), o.f[12], o.f[13])
            elif name == 'translate_m':
                entry = (name, words(o, a[1], 16), words(o, a[2], 4))
            elif name == 'segment':
                entry = (name, words(o, a[0], 4), words(o, a[1], 4), a[2], s32(a[3]))
            elif name == 'move':
                entry = (name, words(o, a[1], 4), a[2])
            elif name == 'sweep':
                entry = (name, words(o, a[1], 4), words(o, a[2], 4), a[3])
            elif name == 'ground':
                assert a[1] == PLAYER + 0xB0, 'ground at'
                entry = (name, words(o, a[1], 4), words(o, a[2], 4), a[3])
            elif name == 'slope':
                assert a[0] == PLAYER + 0x9C, 'slope out'
                entry = (name, o.load(o.load(0x700031D0) + 0x1A, 2))
            elif name == 'floor_query':
                assert a[1] == PLAYER + 0xB0, 'floor_query point'
                entry = (name, words(o, a[1], 3))
            elif name == 'midpoint':
                assert a[0] == PLAYER + 0x290, 'midpoint out'
                entry = (name,)
            elif name == 'script_1B0460':
                entry = (name, s32(a[0]))
            elif name in ('fade', 'fade_1AEE10'):
                entry = (name, s32(a[0]), s32(a[1]))
            elif name == 'clip_FC80':
                entry = (name, o.f[12])
            else:
                entry = (name,)
            o.log.append(entry)
            e = effect(seed, name, index, entry)
            for offset, size, value in e['writes']:
                o.save(PLAYER + offset, value, size)
            for key, value in e['mem'].items():
                o.save(addresses.get(key, key), value)
            if e['out'] is not None:
                for i, value in enumerate(e['out']):
                    if name == 'midpoint' and not e['ret']:
                        break
                    o.save(a[0] + 4 * i, value)
            if e['hit'] is not None:
                node, point, normal, axis = e['hit']
                o.save(0x700031D0, NODE)
                o.save(NODE + 0x1A, node, 2)
                for i in range(3):
                    o.save(0x700031B0 + 4 * i, point[i])
                    o.save(NODE + 0x24 + 4 * i, normal[i])
                    o.save(NODE + 0x34 + 4 * i, axis[i])
            if name in FLOAT_WORKERS | BITS_WORKERS:
                o.f[0] = e['ret']
            else:
                o.r[2] = sx32(e['ret'])
        return run
    ee.hooks = {address: hook(name) for address, name in CALLEES.items()}


class Native:
    """The same callees on the native side, one log, the same scripts."""

    def __init__(self, seed, mem, fault_at=None):
        self.seed, self.log, self.counters = seed, [], {}
        self.mem = dict(mem)             # node / area-record words by key or address
        self.fault_at = fault_at
        self.keep, self.snapshots = [], []
        self.live = LiveActor()
        self.scene = Scene()
        self.scratch = Scratch()
        self.major2 = Major2Scene()
        w = self.workers = Workers()
        for field, proto in PROTOS:
            setattr(w, field, self._keep(proto(self._make(field))))
        self.closure = Closure(C.pointer(w), C.pointer(self.scene), C.pointer(self.scratch),
                               C.pointer(self.major2))

    def _keep(self, fn):
        self.keep.append(fn); return fn

    def image(self):
        scene = tuple(getattr(self.scene, name) for name in SCENE_FIELDS) + (self.major2.d275B14 & 0xFFFFFFFF,)
        scratch = []
        for item in SCRATCH:
            value = getattr(self.scratch, item[0])
            scratch.append(value if len(item) == 2 else value[item[2]])
        return bytes(self.live.bytes), scene, tuple(scratch)

    def _apply(self, name, entry):
        index = self.counters.get(name, 0); self.counters[name] = index + 1
        self.log.append(entry)
        self.snapshots.append(self.image())
        if self.fault_at == (name, index):
            return None
        e = effect(self.seed, name, index, entry)
        raw = self.live.bytes
        for offset, size, value in e['writes']:
            for i in range(size): raw[offset + i] = (value >> (8 * i)) & 0xFF
        for key, value in e['mem'].items():
            if key == 0x70003A20: self.scratch.s3A20 = value
            else: self.mem[key] = value
        return e

    def _make(self, name):
        def vec(pointer, count): return tuple(pointer[i] & 0xFFFFFFFF for i in range(count))

        def fin(e, out_int=None, out_vec=None, count=0):
            if e is None: return -1
            if out_int is not None: out_int[0] = s32(e['ret'])
            if out_vec is not None:
                for i in range(count): out_vec[i] = e['out'][i]
            return 0

        def hit_out(e, result, hit):
            if e is None: return -1
            result[0] = s32(e['ret'])
            node, point, normal, axis = e['hit']
            h = hit.contents
            h.node = node
            for i in range(3):
                h.point[i] = number(point[i]); h.normal[i] = number(normal[i]); h.axis[i] = number(axis[i])
            return 0

        if name == 'request':
            return lambda _, a, clip, force, blend: fin(self._apply(name, (name, clip, force, bits(blend))))
        if name in ('clip_885B0', 'clip_88610', 'clip_88550', 'ledge_top', 'use', 'use_probe'):
            return lambda _, a, r: fin(self._apply(name, (name,)), r)
        if name in ('stick', 'steer', 'w00179150', 'skeleton', 'land', 'sound_182A70'):
            return lambda _, a: fin(self._apply(name, (name,)))
        if name == 'floor':
            return lambda _, a, search, r: fin(self._apply(name, (name, search)), r)
        if name in ('translate', 'land_sound', 'surface5d'):
            return lambda _, a, arg: fin(self._apply(name, (name, arg)))
        if name == 'teleport':
            return lambda _, a, x, y: fin(self._apply(name, (name, x, y)))
        if name == 'random5':
            return lambda _, r: fin(self._apply(name, (name,)), r)
        if name == 'sound':
            return lambda _, a, i, x, radius: fin(self._apply(name, (name, i, x, bits(radius))))
        if name == 'sound_1FB9F0':
            return lambda _, p, q, x, y: fin(self._apply(name, (name, p, q, x, y)))
        if name == 'wrap':
            def wrap_fn(_, x, out):
                e = self._apply(name, (name, x))
                if e is None: return -1
                out[0] = e['ret']; return 0
            return wrap_fn
        if name == 'approach':
            def approach_fn(_, t, c, rate, out):
                e = self._apply(name, (name, t, c, rate))
                if e is None: return -1
                out[0] = e['ret']; return 0
            return approach_fn
        if name in ('cosine', 'sine', 'sqrt'):
            return lambda _, x: number(self._apply(name, (name, bits(x)))['ret'])
        if name == 'atan2':
            return lambda _, y, x: number(self._apply(name, (name, bits(y), bits(x)))['ret'])
        if name == 'to_int':
            return lambda _, x, out: fin(self._apply(name, (name, x)), out)
        if name == 'trs':
            return lambda _, out, p, r, s: fin(self._apply(name, (name, vec(p, 3), vec(r, 3), vec(s, 3))),
                                               None, out, 16)
        if name in ('apply', 'vadd', 'translate_m'):
            if name == 'apply':
                return lambda _, m, v, out: fin(self._apply(name, (name, vec(m, 16), vec(v, 4))), None, out, 4)
            if name == 'vadd':
                return lambda _, p, q, out: fin(self._apply(name, (name, vec(p, 4), vec(q, 4))), None, out, 4)
            return lambda _, out, m, v: fin(self._apply(name, (name, vec(m, 16), vec(v, 4))), None, out, 16)
        if name == 'identity':
            return lambda _, out: fin(self._apply(name, (name,)), None, out, 16)
        if name == 'arbiter':
            return lambda _, a, clip, blend, frame: fin(self._apply(name, (name, clip, bits(blend), bits(frame))))
        if name in ('rotate_y', 'rotate_x'):
            return lambda _, out, m, angle: fin(self._apply(name, (name, vec(m, 16), angle)), None, out, 16)
        if name == 'segment':
            return lambda _, p, q, mask, i, r, h: hit_out(self._apply(name, (name, vec(p, 4), vec(q, 4), mask, i)), r, h)
        if name == 'move':
            return lambda _, a, t, mask, r, h: hit_out(self._apply(name, (name, vec(t, 4), mask)), r, h)
        if name == 'sweep':
            return lambda _, a, p, q, mask, r, h: hit_out(self._apply(name, (name, vec(p, 4), vec(q, 4), mask)), r, h)
        if name == 'ground':
            return lambda _, a, p, q, mask, r, h: hit_out(self._apply(name, (name, vec(p, 4), vec(q, 4), mask)), r, h)
        if name == 'slope':
            return lambda _, h, out: fin(self._apply(name, (name, h.contents.node)), None, out, 1)
        if name == 'floor_query':
            return lambda _, a, p, r: fin(self._apply(name, (name, vec(p, 3))), r)
        if name == 'midpoint':
            def midpoint_fn(_, out, r):
                e = self._apply(name, (name,))
                if e is None: return -1
                r[0] = e['ret']
                if e['ret']:
                    for i in range(3): out[i] = e['out'][i]
                return 0
            return midpoint_fn
        if name == 'area_point':
            def area_fn(_, area, sub, offset, out):
                for i in range(3): out[i] = self.mem[('area', area, sub, offset + 4 * i)]
                return 0
            return area_fn
        if name == 'script_1B0460':
            return lambda _, x: fin(self._apply(name, (name, x)))
        if name in ('fade', 'fade_1AEE10'):
            return lambda _, x, y: fin(self._apply(name, (name, x, y)))
        if name == 'clip_FC80':
            return lambda _, a, blend: fin(self._apply(name, (name, bits(blend))))
        if name == 'root_node':
            def root_fn(_, offset, out):
                out[0] = self.mem['root%d' % offset]; return 0
            return root_fn
        if name == 'bone':
            def bone_fn(_, a, slot, offset, out):
                out[0] = self.mem[(slot, offset)]; return 0
            return bone_fn
        raise AssertionError(('no native worker', name))


def build_native():
    out = ROOT / 'build/player_closure_10_12_19_reference'
    out.mkdir(parents=True, exist_ok=True)
    lib = out / ('closure.dylib' if sys.platform == 'darwin' else 'closure.so')
    cached_build(lib, ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                       '-shared', '-fPIC', '-Isrc', 'src/game/em_player_closure_10_12_19.c',
                       'src/game/em_player_fall.c', 'src/game/em_player_major2.c', '-o', str(lib)])
    native = C.CDLL(str(lib))
    for name in NATIVE_ENTRY.values():
        getattr(native, name).argtypes = [C.c_void_p, A]
    return native


# --------------------------------------------------------------- cases ----

HANDLED = {
    0x10: (0, 1, 0xA, 0xB, 0x14, 0x1E, 0x1F, 0x28, 0x32, 0x3C, 0x46, 0x50, 0x5A, 0x5B, 0x5C, 0x5D),
    0x12: (0, 1, 0xA, 0xB, 0x14, 0x1E, 0x28, 0x29, 0x2A, 0x2B, 0x32),
    0x19: (0, 1, 0xA, 0x14, 0x15, 0x16, 0x17, 0x1E, 0x1F, 0x28, 0x29, 0x32, 0x33, 0x63),
    0x1A: (0, 0xA, 0x14, 0x15, 0x16, 0x1E),
}
RECORD_FLOATS = (0x38, 0x60, 0x64, 0x68, 0x9C, 0xB0, 0xB4, 0xB8, 0xBC, 0xC0, 0xC4, 0xC8, 0x204,
                 0x218, 0x21C, 0x244, 0x248, 0x258, 0x26C, 0x290, 0x294, 0x298, 0x29C, 0x2E0,
                 0x2E4, 0x2E8, 0x2EC, 0x2F4, 0x2F8)
# Points inside and at the edges of 0016DE40's zone boxes, by area.
ZONE_POINTS = {
    3: [(658.0, 55.0, 714.0), (634.0, 55.0, 802.0), (557.0, 55.0, 715.0), (648.0, 45.0, 704.0),
        (668.0, 65.0, 724.0), (647.5, 55.0, 714.0), (668.5, 55.0, 714.0), (658.0, 44.5, 714.0),
        (658.0, 65.5, 714.0), (658.0, 55.0, 703.5), (658.0, 55.0, 724.5),
        (624.0, 45.0, 792.0), (644.0, 65.0, 812.0), (634.0, 44.5, 802.0), (634.0, 55.0, 812.5),
        (547.0, 45.0, 705.0), (567.0, 65.0, 725.0), (557.0, 65.5, 715.0), (557.0, 55.0, 704.5),
        (623.5, 55.0, 802.0), (644.5, 55.0, 802.0), (634.0, 65.5, 802.0),
        (634.0, 55.0, 791.5), (546.5, 55.0, 715.0), (567.5, 55.0, 715.0),
        (557.0, 44.5, 715.0), (557.0, 55.0, 725.5)],
    8: [(120.0, 0.0, 160.0), (110.5, 0.0, 146.0), (135.0, 0.0, 176.0), (130.0, 0.0, 97.0),
        (122.6, 0.0, 87.0), (142.6, 0.0, 107.0), (110.49999, 0.0, 160.0), (135.00002, 0.0, 160.0),
        (120.0, 0.0, 145.99998), (120.0, 0.0, 176.00002), (122.59999, 0.0, 97.0),
        (142.60002, 0.0, 97.0), (130.0, 0.0, 86.99999), (130.0, 0.0, 107.00001), (130.0, 0.0, 160.0)],
    0x13: [(720.0, 240.0, 1000.0), (710.0, 230.0, 960.0), (730.0, 250.0, 1160.0), (709.9999, 0, 1000.0),
           (730.0001, 0, 1000.0), (720.0, 0, 959.9999), (720.0, 0, 1160.0001), (703.0, 240.0, 1219.0),
           (693.0, 230.0, 1209.0), (713.0, 250.0, 1229.0), (703.0, 229.5, 1219.0),
           (703.0, 250.5, 1219.0), (692.5, 240.0, 1219.0), (713.5, 240.0, 1219.0),
           (703.0, 240.0, 1208.5), (703.0, 240.0, 1229.5)],
    0: [(185.0, -90.0, -1460.0), (185.0, -85.0, -1460.0), (185.0, -84.9999, -1460.0), (185.0, -65.0, -1460.0),
        (185.0, -64.9999, -1460.0), (185.0, 0.0, -1470.0), (185.0, 0.0, -1470.0001), (185.0, 0.0, -1469.9999)],
}
CLIPS_20C = tuple(range(0xBC, 0xCA)) + (0x6D, 0xBB, 0xCA)


SCENARIOS = ('climb10', 'climb10', 'climb10', 'climb12', 'climb12', 'climb12', 'entry10', 'entry10',
             'ledge10', 'ledge10', 'entry12', 'turn12', 'push', 'push', 'crawl3', 'crawl3', 'crawl3',
             'crawl8', 'crawl13', 'crawl13', 'crawl0', 'exit2', 'exit2', 'exit2', 'dispatch19',
             'guard19', 'begin1A', 'swing', 'swing')


def direct(rng, scenario, raw, scene):
    """Steer a case toward one path: the +5/+6/+7 bytes, the guards that
    would otherwise stop the routine first, and the flags the path tests."""
    def guard():
        struct.pack_into('<I', raw, 0x224, 0); struct.pack_into('<I', raw, 0x22C, 0)
        raw[0xF] = 0
        if rng.random() < 0.9: scene['pad_pressed'] = 0
        if rng.random() < 0.7: scene['pad_held'] = 0
    flags = lambda *choices: struct.pack_into('<I', raw, 0x200, rng.choice(choices))
    if scenario == 'generic':
        return
    if scenario != 'guard19':
        guard()
    if scenario in ('climb10', 'climb12'):
        raw[5], raw[6] = (0x10 if scenario == 'climb10' else 0x12), 0x14
        raw[7] = rng.choice([0, 1, 2, 2, 2, 2, 3, 4, 4])
        flags(0x1000, 0x1000, 0x1000, 0, 0x8000)
        struct.pack_into('<I', raw, 0x24C, rng.choice([0, 0, 0, 1]))
        raw[0x23F] = rng.choice([0, 1, 1, 2, 3, 3])
        raw[0x25C] = rng.choice([0, 1, 2, 3, 3])
        raw[0x2F1] = rng.choice([1, 1, 2, 2, 0])
    elif scenario == 'entry10':
        raw[5], raw[6] = 0x10, 1
    elif scenario == 'ledge10':
        raw[5], raw[6] = 0x10, rng.choice([0x28, 0x32, 0x3C, 0x46])
        raw[7] = rng.choice([0, 1, 1, 2, 2, 2, 3])
        flags(0x1000, 0, 0x8000)
        if rng.random() < 0.5: scene['pad_pressed'] = scene['use_mask']
    elif scenario == 'entry12':
        raw[5], raw[6] = 0x12, 1
        scene['pad_gait'] = rng.choice([1, 2, 3, 0])
    elif scenario == 'turn12':
        raw[5], raw[6] = 0x12, rng.choice([0x1E, 0x32, 0x28, 0x2B])
        raw[7] = rng.choice([0, 1, 2, 3])
        flags(0x1000, 0, 0x8000)
    elif scenario == 'push':
        raw[5], raw[6] = rng.choice([(0x10, 0x50), (0x12, 0x29)])
        raw[7] = rng.choice([0, 1, 2, 2, 3, 3, 4, 5, 5, 0x63])
        flags(0x1000, 0, 0x1000)
        struct.pack_into('<H', raw, 0x2E, rng.choice([3, 3, 2]))
        struct.pack_into('<H', raw, 0x28, rng.choice([0x18, 0x40, 0x17]))
        raw[0x25C] = rng.choice([0, 0, 1, 2, 3])
        struct.pack_into('<I', raw, 0x24C, rng.choice([0, 1, 1, 2]))
    elif scenario.startswith('crawl') or scenario == 'exit2':
        raw[5], raw[6] = 0x19, 0x15
        flags(0, 0, 0, 0x1000)
        raw[0xA] = rng.choice([0, 0, 0, 1])
        raw[0x23B] = rng.choice([0x37, 0x37, 5])
        scene['area'] = {'exit2': 2, 'crawl3': 3, 'crawl8': 8, 'crawl13': 0x13, 'crawl0': 0}[scenario]
        if scenario == 'exit2': scene['sub_area'] = rng.choice([0, 1, 2, 3])
    elif scenario == 'dispatch19':
        raw[5], raw[6] = 0x19, rng.choice([0, 1, 0xA, 0x14, 0x16, 0x17, 0x1E, 0x1F, 0x28, 0x29, 0x32, 0x33])
        scene['area'] = rng.choice([0, 0, 0xB])
    elif scenario == 'swing':
        raw[5], raw[6] = rng.choice([(0x10, 0x50), (0x12, 0x29)])
        raw[7] = rng.choice([2, 3])
        struct.pack_into('<H', raw, 0x2E, rng.choice([0, 1, 2, 3]))
        struct.pack_into('<H', raw, 0x28, rng.choice([0, 0, 1, 0x17, 0x18, 0x40]))
        struct.pack_into('<I', raw, 0x24C, rng.choice([0, 0, 1]))
        raw[0x25C], raw[0x23F] = rng.randrange(4), rng.randrange(4)
    elif scenario == 'guard19':
        raw[5], raw[6] = 0x19, rng.choice(HANDLED[0x19])
        struct.pack_into('<I', raw, 0x224, rng.choice([0x3F800000, 0x80000001, 0x80000000]))
    elif scenario == 'begin1A':
        raw[5], raw[6] = 0x1A, rng.choice([0, 0, 0xA, 0x14, 0x15, 0x16, 0x1E])
        raw[0xD] = rng.choice([0, 1, 2])


def random_case(seed):
    rng = random.Random(seed)
    raw = bytearray(CAPTURED) if rng.random() < 0.25 else bytearray(rng.randrange(256) for _ in range(0x320))
    raw[0x40:0x48] = CAPTURED[0x40:0x48]              # node 0 (D_00275B40 reads through +0x40)
    raw[0x110:0x164] = CAPTURED[0x110:0x164]          # the bone pointer words (00182100)
    for offset in RECORD_FLOATS:
        struct.pack_into('<I', raw, offset, finite(rng))
    for i in range(16):
        struct.pack_into('<I', raw, 0xD0 + 4 * i, finite(rng))
    state = rng.choice((0x10, 0x10, 0x12, 0x12, 0x19, 0x19, 0x19, 0x1A))
    raw[4], raw[5] = 1, state
    sub = rng.choice(HANDLED[state] + (rng.randrange(256),)) if rng.random() < 0.95 else rng.randrange(256)
    raw[6] = sub
    raw[7] = rng.choice([0, 1, 2, 3, 4, 5, 0x63, rng.randrange(256)])
    raw[0xD] = rng.choice([0, 0, 1, 2, 3])
    raw[0xA] = rng.choice([0, 0, 1])
    raw[0xF] = rng.choice([0, 0, 0, 0, 2])
    for offset in (0x224, 0x22C):
        struct.pack_into('<I', raw, offset, 0 if rng.random() < 0.85 else rng.choice([0x3F800000, 0x80000000, 1]))
    struct.pack_into('<I', raw, 0x200, rng.choice([0, 0x1000, 0x8000, 0x9000, rng.randrange(1 << 32)]))
    struct.pack_into('<I', raw, 0x24C, rng.choice([0, 1, 2, 3, 4, 0xFFFFFFFF]))
    struct.pack_into('<H', raw, 0x28, rng.choice([0, 0, 1, 0x10, 0x17, 0x18, 0x19, 0xFFFF, 0x8000]))
    struct.pack_into('<H', raw, 0x2E, rng.choice([3, 3, 0, 2, 0x103]))
    struct.pack_into('<H', raw, 0x20C, rng.choice(CLIPS_20C))
    raw[0x23F] = rng.randrange(4)
    raw[0x25C] = rng.randrange(4)
    raw[0x2F1] = rng.choice([0, 1, 2, 1, 2, 3])
    raw[0x23A] = rng.choice([0x5D, 5, 0])
    raw[0x23B] = rng.choice([0x37, 0x37, 5, 0])
    raw[0x302] = rng.choice([0, 1])
    if rng.random() < 0.2:                            # +38 and +C0 at zero (0016A4B0 case 5)
        struct.pack_into('<I', raw, 0x38, rng.choice([0, 0x80000000]))
        struct.pack_into('<I', raw, 0xC0, rng.choice([0, 0x80000000]))
    if rng.random() < 0.25:                           # +218 == +C4 / +26C == +C4 (turns end)
        struct.pack_into('<I', raw, 0x218, struct.unpack_from('<I', raw, 0xC4)[0])
    scene = {
        'pad_held': rng.choice([0, 0, 0x10, 0x40, 0xFFFF]),
        'pad_pressed': rng.choice([0, 0, 0x40, 0x4000, 0xFFFF]),
        'use_mask': rng.choice([0x40, 0x40, 0x10]),
        'mask_3B7C': rng.choice([0x10, 0, 0x1000]), 'mask_3B7E': rng.choice([0x20, 0, 0x2000]),
        'fade': rng.choice([0, 0, 2, 1, 0xFFFF]),
        'area': rng.choice([0xB, 0, 0, 2, 2, 3, 3, 8, 8, 0x13, 0x13]),
        'sub_area': rng.choice([0, 0, 1, 2, 3]),
        'zone': rng.randrange(256), 'd8106BE': rng.randrange(256),
        'pad_gait': rng.choice([0, 1, 2, 3]), 'pad_x': rng.randrange(256), 'pad_y': rng.randrange(256),
        'area_flags2': rng.choice([0, 0x80, 0x81, 1]),
        'd8106B5': rng.randrange(256), 'd8106B6': rng.randrange(256), 'd8106B7': rng.randrange(256),
        'd8106B8': rng.randrange(256),
        'd8106C8': rng.choice([0, 1, 2, 3, 0x20081910, rng.randrange(1 << 32)]),
        'camera_yaw': finite(rng, -3.2, 3.2), 'spad31E4': finite(rng, -3.2, 3.2),
        'd275B10': rng.randrange(1 << 32), 'd275B0C': rng.randrange(1 << 32), 'd281B64': rng.randrange(1 << 32),
    }
    scenario = rng.choice(SCENARIOS) if rng.random() < 0.75 else 'generic'
    direct(rng, scenario, raw, scene)
    if scene['area'] in ZONE_POINTS and rng.random() < 0.7:
        point = rng.choice(ZONE_POINTS[scene['area']])
        for i, value in enumerate(point):
            if value or rng.random() < 0.5:
                # 0016DE40 +6 0x15 lowers +B4 by 0.2 before the boxes.
                struct.pack_into('<f', raw, 0xB0 + 4 * i, value + (0.2 if i == 1 else 0.0))
    if scene['area'] == 8 and rng.random() < 0.5:
        scene['sub_area'] = 3
    if scene['area'] == 0x13 and rng.random() < 0.5:
        scene['sub_area'] = 0
    scratch = [finite(rng, -20, 20) for _ in SCRATCH]
    d275B14 = rng.randrange(1 << 32)
    nodes = {key: finite(rng) for key in ('root0', 'root8', (0x15C, 0xC0), (0x15C, 0xC8),
                                          (0x160, 0xC0), (0x160, 0xC8))}
    area = {}
    for sub in range(3):
        for base in (0xF0, 0x120):
            for i in range(3):
                near = struct.unpack_from('<f', raw, 0xB0 + 4 * i)[0] - (0.2 if i == 1 else 0.0)
                area[('area', 2, sub, base + 4 * i)] = (bits(near + rng.choice([0.0, 7.9, -7.9, 8.0, -8.0, 7.99999]))
                                                        if rng.random() < 0.7 else finite(rng))
    fault_at = None
    if rng.random() < 0.06:
        fault_at = (rng.choice([n for n in CALLEES.values() if n not in FLOAT_WORKERS]), 0)
    return bytes(raw), scene, scratch, d275B14, nodes, area, fault_at


def seed_ee(ee, raw, scene, scratch, d275B14, nodes, area):
    ee.reset()
    ee.write(PLAYER, raw)
    for name, address, size in SCENE:
        ee.save(address, scene[name], size)
    ee.save(D275B14, d275B14)
    for item, value in zip(SCRATCH, scratch):
        ee.save(item[1], value)
    addresses = node_addresses(ee)
    for key, value in nodes.items():
        ee.save(addresses[key], value)
    records = area_records(ee)
    for (_, _, sub, offset), value in area.items():
        ee.save(records[sub] + offset, value)
    return addresses


def seed_native(side, raw, scene, scratch, d275B14):
    C.memmove(side.live.bytes, raw, 0x320)
    for name in SCENE_FIELDS:
        setattr(side.scene, name, scene[name])
    side.major2.d275B14 = s32(d275B14)
    for item, value in zip(SCRATCH, scratch):
        if len(item) == 2: setattr(side.scratch, item[0], value)
        else: getattr(side.scratch, item[0])[item[2]] = value


def run_case(seed):
    raw, scene, scratch, d275B14, nodes, area, fault_at = random_case(seed)
    ee = oracle_ee()
    addresses = seed_ee(ee, raw, scene, scratch, d275B14, nodes, area)
    install_hooks(ee, seed, addresses)
    state = raw[5]
    ee.call(STATE_ENTRY[state], (PLAYER,))
    want = image(ee)
    side = Native(seed, {**nodes, **area}, fault_at)
    seed_native(side, raw, scene, scratch, d275B14)
    result = getattr(NATIVE, NATIVE_ENTRY[state])(C.addressof(side.closure), C.byref(side.live))
    got = side.image()
    tags = {('state', state, raw[6])}
    names = [e[0] for e in ee.log]
    if fault_at is not None and fault_at[0] in names:
        k = names.index(fault_at[0])
        assert result == -1, (seed, 'fault not propagated', fault_at)
        assert side.log == ee.log[:k + 1], (seed, 'fault log', side.log, ee.log[:k + 1])
        assert side.snapshots == ee.snapshots[:k + 1], (seed, 'fault: image at an earlier call')
        assert got == ee.snapshots[k], (seed, 'fault image', diff(got, ee.snapshots[k]))
        tags.add('fault')
        return seed, tags, frozenset(ee.branches)
    assert result == 0, (seed, 'native result', result)
    assert side.log == ee.log, (seed, 'callee sequence', hex(state), hex(raw[6]), side.log, ee.log)
    for k, (mine, theirs) in enumerate(zip(side.snapshots, ee.snapshots)):
        if mine != theirs:
            raise AssertionError((seed, 'state %#x/%#x' % (state, raw[6]), 'image at call', k, ee.log[k][0],
                                  diff(mine, theirs)))
    if got != want:
        raise AssertionError((seed, 'state %#x/%#x' % (state, raw[6]), 'final image', diff(got, want)))
    if 'trs' in names: tags.add('trs')
    return seed, tags, frozenset(ee.branches)


def run_batch(seeds):
    """run_case over a batch of seeds (one worker task): the union of the
    tags and branch outcomes, and the number of injected faults."""
    tags, branches, faults = set(), set(), 0
    for seed in seeds:
        _, case_tags, case_branches = run_case(seed)
        tags |= case_tags; branches |= case_branches
        faults += 'fault' in case_tags
    return tags, branches, faults


def diff(mine, theirs):
    out = []
    for part, (a, b) in zip(('record', 'scene', 'scratch'), zip(mine, theirs)):
        if part == 'record':
            out += [('record', hex(i), a[i], b[i]) for i in range(len(a)) if a[i] != b[i]][:12]
        else:
            names = SCENE_FIELDS + ['d275B14'] if part == 'scene' else [
                item[0] if len(item) == 2 else '%s[%d]' % (item[0], item[2]) for item in SCRATCH]
            out += [(part, names[i], hex(x), hex(y)) for i, (x, y) in enumerate(zip(a, b)) if x != y]
    return out


def refusal_checks():
    """A missing worker or storage refuses before any write; +25C past
    D_00248630 faults at the read."""
    raw = bytes(range(256)) * 3 + bytes(0x20)
    count = 0
    for state, name in NATIVE_ENTRY.items():
        side = Native(1, {})
        seed_native(side, raw, {n: 0 for n in SCENE_FIELDS}, [0] * len(SCRATCH), 0)
        before = side.image()
        fn = getattr(NATIVE, name)
        for field, _ in PROTOS:
            saved = getattr(side.workers, field)
            setattr(side.workers, field, type(saved)())
            assert fn(C.addressof(side.closure), C.byref(side.live)) == -1, (name, field)
            assert side.image() == before and not side.log, ('refusal wrote', name, field)
            setattr(side.workers, field, saved)
            count += 1
        for part in ('workers', 'scene', 'scratch', 'major2'):
            saved = getattr(side.closure, part)
            setattr(side.closure, part, type(saved)())
            assert fn(C.addressof(side.closure), C.byref(side.live)) == -1, (name, part)
            assert side.image() == before, ('refusal wrote', name, part)
            setattr(side.closure, part, saved)
        assert fn(None, C.byref(side.live)) == -1
        assert fn(C.addressof(side.closure), None) == -1
    # 0016A4B0 +7 3 with +25C = 4: +7 advances, then the table read faults.
    bad = bytearray(raw)
    bad[5], bad[6], bad[7], bad[0x25C] = 0x10, 0x50, 3, 4
    struct.pack_into('<H', bad, 0x2E, 3); struct.pack_into('<H', bad, 0x28, 0x18)
    side = Native(2, {})
    seed_native(side, bytes(bad), {n: 0 for n in SCENE_FIELDS}, [0] * len(SCRATCH), 0)
    assert NATIVE.em_player_closure1019_00169730(C.addressof(side.closure), C.byref(side.live)) == -1
    after = bytes(side.live.bytes)
    assert after[7] == 4 and after[:7] + after[8:] == bytes(bad[:7] + bad[8:]), '+25C = 4 fault'
    return count


ROUTE = REFERENCE.parent / 's87/route'


def route_census():
    """The first-level route captures (docs/FIRST_LEVEL_ROUTE.md): every
    per-frame row's +5. None of the 15 beats enters +5 0x10, 0x12, 0x19 or
    0x1A, so no beat can replay these routines; a recapture that does reach
    one fails here, so that its replay is added. Returns (beats, rows, the
    +5 values seen), or None when the captures are absent."""
    import json
    traces = sorted(ROUTE.glob('[0-9][0-9]_*/trace.json'))
    if not traces:
        return None
    rows, seen = 0, set()
    for trace in traces:
        for row in json.loads(trace.read_text())['rows']:
            rows += 1
            seen.add(row.get('p5'))
            assert row.get('p5') not in STATE_ENTRY, (
                'route reaches +5 %#x at %s: add its replay' % (row.get('p5'), trace.parent.name))
    return len(traces), rows, sorted(v for v in seen if v is not None)


def main():
    global RAM, ELF, NATIVE, CAPTURED
    ELF = read_elf()
    if not RAM_PATH.exists():
        raise SystemExit('missing %s (the captured AREA11 RAM; docs/PLAYER_CLOSURE_10_12_19.md)' % RAM_PATH)
    RAM = RAM_PATH.read_bytes()
    for address, size in tuple(EXECUTED.items()) + TABLES:
        at = address - 0x100000 + 0x300
        assert RAM[address:address + size] == ELF[at:at + size], ('captured RAM differs from the ELF', hex(address))
    CAPTURED = RAM[PLAYER:PLAYER + 0x320]
    NATIVE = build_native()

    count = reference_mode.pick(200000, 16000)
    seeds = [0x169730 * 13 + i for i in range(count)]
    batches = reference_mode.parallel_map(run_batch, [seeds[i:i + 250] for i in range(0, count, 250)])
    branches, tags, faults = set(), set(), 0
    for batch_tags, batch_branches, batch_faults in batches:
        branches |= batch_branches; tags |= batch_tags; faults += batch_faults
    wanted = [(pc, taken) for start, size in EXECUTED.items()
              for pc in conditional_branches(RAM, start, start + size) for taken in (False, True)]
    missing = [(hex(pc), taken) for pc, taken in wanted
               if (pc, taken) not in branches and (pc, taken) not in IMPOSSIBLE]
    assert not missing, ('branch outcomes never exercised', len(missing), missing)
    for pc, taken in IMPOSSIBLE:
        assert (pc, taken) not in branches, ('an IMPOSSIBLE outcome occurred', hex(pc), taken)
    for state, subs in HANDLED.items():
        for sub in subs:
            assert ('state', state, sub) in tags, ('state never run', hex(state), hex(sub))
    assert 'fault' in tags, 'no worker fault propagated'
    refusals = refusal_checks()
    census = route_census()
    reference_mode.banner(reference_mode.part(count, 200000, 'state cases'))
    print('player closure 10/12/19 reference: PASS -- %d cases over the %d handled (+5, +6) pairs: '
          'all 0x320 record bytes, %d scene words and %d scratchpad words after the call and at every '
          'worker call, and every worker call with its arguments, identical; %d/%d conditional '
          'branch outcomes of the %d executed routines exercised (%d impossible, listed); %d worker '
          'faults stop at the original bytes; %d missing-worker/storage refusals'
          % (count, sum(len(v) for v in HANDLED.values()), len(SCENE) + 1, len(SCRATCH),
             len(wanted) - len(missing) - len(IMPOSSIBLE), len(wanted) - len(IMPOSSIBLE),
             len(EXECUTED), len(IMPOSSIBLE), faults, refusals))
    if census is None:
        print('route census: skipped (no captures under %s)' % ROUTE)
    else:
        print('route census: %d beats, %d frames, +5 values %s: none enters 0x10/0x12/0x19/0x1A, '
              'so no route replay reaches these routines' % (census[0], census[1],
                                                             ', '.join('%#x' % v for v in census[2])))


if __name__ == '__main__':
    main()
