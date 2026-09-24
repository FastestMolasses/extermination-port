#!/usr/bin/env python3
"""Execute the original player helpers and compare em_player_record_helpers.c.

docs/PLAYER_RECORD_HELPERS.md. The user's pinned ELF supplies every
instruction and table; none are embedded here. Routines executed unmodified
on the measured float model (FallEE, tools/ee_float_model.py):

  001755B0  heading test            00177510  ledge frame
  001775E0  ledge lip sweep         001776E0  high-ledge side sweeps
  00177CF0  high-ledge hand sweeps  0019A180  column entry attribute
  0017F320  hang clearance sweeps   00188550  hang row clip
  00174FD0  stick quadrant

together with the leaves they call, which run as original code too:
001B1470 (wrap), 0011DF78 (fabsf), 001029C0 / 00102BB0 / 00102918 (the SDK
matrix routines), 001026A0 (matrix x vector) and 001028B8 (vector add).
0019AFE0 (the sweep), 0011E620 (atan2f) and 0011DE90 (cosf) are hooked,
scripted per case and recorded; the native entries get the same script
through their workers. The test asserts the hooked and executed sets are
exactly the jal targets of the routines.

Every case compares all 0x320 record bytes, the scratch words 0x700038A0..AC
and 0x70003A20, the ledge frame 00177510 leaves (0x70003050 point,
0x70003060 normal, 0x700031E4 heading, 0x70003070 matrix), the return value
and the worker calls with their arguments. The default run asserts that
every conditional branch of the nine routines goes both ways.

Captures: over the end snapshot of every route beat 00..14 (the PCSX2
captures, ../Extermination/build/s87/route/), each routine runs as original
code on the captured world (every callee original) and the record entry runs
with its workers bound to the same original routines on a second copy of the
world (the record and the scratch synced around each call). Record, scratch,
ledge frame, result and the whole RAM must be identical.
Default ~5 s; EM_TEST_FULL=1 runs the exhaustive sweep.
"""
import ctypes as C
import math
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
from test_player_slide_reference import EE, read_elf, s32, LIBC  # noqa: E402
from test_player_fall_reference import FallEE  # noqa: E402
from test_player_slide_reference import LiveActor, Scratch, RecordWorld  # noqa: E402
from test_player_climb_reference import Table  # noqa: E402

MASK = 0xFFFFFFFF
OUT = ROOT / 'build' / 'player_record_helpers_reference'

H55B0, H7510, H75E0, H76E0, H7CF0 = 0x1755B0, 0x177510, 0x1775E0, 0x1776E0, 0x177CF0
HA180, HF320, H8550, H4FD0 = 0x19A180, 0x17F320, 0x188550, 0x174FD0
SIZES = {H55B0: 132, H7510: 196, H75E0: 256, H76E0: 1172, H7CF0: 592, HA180: 396,
         HF320: 708, H8550: 28, H4FD0: 456}
NAMES = {H55B0: '001755B0', H7510: '00177510', H75E0: '001775E0', H76E0: '001776E0',
         H7CF0: '00177CF0', HA180: '0019A180', HF320: '0017F320', H8550: '00188550',
         H4FD0: '00174FD0'}
SWEEP, ATAN2, COSINE = 0x19AFE0, 0x11E620, 0x11DE90
HOOKED = {SWEEP: 'sweep', ATAN2: 'atan2', COSINE: 'cosine'}
EXECUTED = {0x1B1470, 0x11DF78, 0x1029C0, 0x102BB0, 0x102918, 0x1026A0, 0x1028B8}

ACTOR, NODE, OBJECTS = 0x680000, 0x6A0000, 0x6E0000


def F(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def number(word):
    return struct.unpack('<f', struct.pack('<I', word & MASK))[0]


def fb(value):
    """ctypes float -> raw bits (the float was produced from raw bits)."""
    return struct.unpack('<I', struct.pack('<f', value))[0]


PI, HALF_PI, TWO_PI = 0x40490FDB, 0x3FC90FDB, 0x40C90FDB


def wrap(x):
    """001B1470 on the model (only used to aim cases at boundaries)."""
    while not M.ee_c_le(x, PI): x = M.ee_sub(x, TWO_PI)
    while M.ee_c_le(x, PI ^ 0x80000000): x = M.ee_add(x, TWO_PI)
    return x


# ======================================================================
# The interpreter with branch coverage
# ======================================================================

def in_translated(pc):
    return any(start <= pc < start + size for start, size in SIZES.items())


class CoverEE(FallEE):
    def __init__(self, elf):
        super().__init__(elf)
        self.outcomes = set()

    def branch(self, word, pc):
        b = super().branch(word, pc)
        if b is not None and in_translated(pc):
            self.outcomes.add((pc, b[0]))
        return b


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


def unreachable_outcomes(elf):
    """Branch outcomes no input can produce, found by decoding (not by
    address): the sign tests 00174FD0 applies to a zero-extended stick byte
    (the unsigned-conversion idiom; a byte is never negative), and
    0019A180's arg0 != 0 exit, which none of its callers takes (0015DF10,
    0017C860 and 0017D080 all pass 0; the record entry has no arg0)."""
    ee = EE(elf)
    out = set()
    for pc in range(H4FD0, H4FD0 + SIZES[H4FD0], 4):
        word = ee.load(pc)
        if word >> 26 != 1 or (word >> 16 & 31) != 0:
            continue
        reg = word >> 21 & 31
        for back in (4, 8, 12):
            prev = ee.load(pc - back)
            if prev >> 26 == 36 and (prev >> 16 & 31) == reg:
                out.add((pc, True))
                break
    first = ee.load(HA180)
    assert first >> 26 == 4 and (first >> 21 & 31) == 4 and (first >> 16 & 31) == 0, hex(first)
    out.add((HA180, False))
    return out


def check_callee_set(elf):
    ee = EE(elf)
    targets = set()
    for start, size in SIZES.items():
        for pc in range(start, start + size, 4):
            word = ee.load(pc)
            if word >> 26 == 3:
                targets.add((word & 0x3FFFFFF) << 2)
    known = set(HOOKED) | EXECUTED
    assert targets == known, ('jal targets', sorted(hex(t) for t in targets ^ known))
    return len(targets)


# ======================================================================
# Native side
# ======================================================================

P = C.POINTER
VP, I, U8, U16, U32, FLT = C.c_void_p, C.c_int, C.c_uint8, C.c_uint16, C.c_uint32, C.c_float
ProbeHit = shared.ProbeHit
SWEEP_FN = C.CFUNCTYPE(I, VP, P(LiveActor), P(FLT), P(FLT), C.c_uint, P(ProbeHit))
MATH2_FN = C.CFUNCTYPE(FLT, VP, FLT, FLT)
MATH1_FN = C.CFUNCTYPE(FLT, VP, FLT)


class Helpers(C.Structure):
    _fields_ = [('context', VP), ('sweep', SWEEP_FN), ('atan2', MATH2_FN), ('cosine', MATH1_FN),
                ('spad3B8D', P(U8)), ('d810E57', P(U8)), ('d810E64', P(U8)), ('d810E65', P(U8)),
                ('d8106A0', P(U32)), ('scratch', P(Scratch))]


class Ledge(C.Structure):
    _fields_ = [('point', FLT * 3), ('normal', FLT * 3), ('heading', FLT), ('matrix', FLT * 16)]


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / ('record_helpers.dylib' if sys.platform == 'darwin' else 'record_helpers.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc',
                    'src/game/em_player_record_helpers.c', 'src/game/em_effect_original.c',
                    'src/game/em_owner_services_original.c', 'src/game/em_player_stage_workers.c',
                    'src/game/em_sdk_math_original.c', '-lm', '-o', str(lib)], cwd=ROOT, check=True)
    n = C.CDLL(str(lib))
    H, A = P(Helpers), P(LiveActor)
    n.em_player_record_001755B0.argtypes = [H, A, P(I)]
    n.em_player_record_0017F320.argtypes = [H, A, P(I)]
    n.em_player_record_00188550.argtypes = [H, A, P(I)]
    n.em_player_record_00174FD0.argtypes = [H, A]
    n.em_player_record_00177510.argtypes = [H, P(ProbeHit), P(Ledge)]
    n.em_player_record_001775E0.argtypes = [H, A, P(Ledge), I, FLT, P(I)]
    n.em_player_record_001776E0.argtypes = [H, A, P(Ledge), FLT, P(I)]
    n.em_player_record_00177CF0.argtypes = [H, A, P(Ledge), FLT, P(I)]
    n.em_player_record_0019A180.argtypes = [H, P(Table), I, P(I)]
    return n


# ======================================================================
# Scripted worker effects (identical on both sides)
# ======================================================================

BOUNDARY_ANGLES = (0x4016CBE4, 0xC016CBE4, 0x3F490FDB, 0xBF490FDB, 0, 0x80000000,
                   0x4016CBE5, 0x4016CBE3, 0x3F490FDC, 0x3F490FDA, PI, 0xC0490FDB)


class Script:
    def __init__(self, seed, fail_at=None, quiet=False):
        self.seed, self.count, self.fail_at, self.quiet = seed, 0, fail_at, quiet

    def next(self, name, args):
        rng = random.Random('%d:%d:%s' % (self.seed, self.count, name))
        index = self.count
        self.count += 1
        if name == 'sweep':
            kind = rng.choice((0, 0, 0, 1, 2, 4, 2, 4, 6))
            if self.quiet and rng.random() < 0.8:
                kind = 0            # reach the later sweeps of 001776E0 / 0017F320
            node = rng.choice((0x2032, 0x2005, 0x4032, 0x1005, 0x2046, rng.randrange(0x10000)))
            return index, (kind, node)
        if name == 'atan2':
            y, x = args
            value = F(LIBC.atan2f(number(y), number(x))) if rng.random() < 0.6 else rng.choice(BOUNDARY_ANGLES)
            return index, value
        value = F(LIBC.cosf(number(args[0]))) if rng.random() < 0.7 else \
            rng.choice((0, 0x80000000, 0x3F800000, 0xBF800000, F(rng.uniform(-1, 1))))
        return index, value


# ======================================================================
# Cases
# ======================================================================

ENTRIES = ('001755B0', '00177510', '001775E0', '001776E0', '00177CF0', '0019A180', '0017F320',
           '00188550', '00174FD0')


def put(buf, offset, size, value):
    buf[offset:offset + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'little')


def yaw_matrix_words(angle):
    """A row-major yaw matrix (host floats): the SDK matrices are well-formed
    rotations; the exact bits do not matter (both sides read the same)."""
    c, s = math.cos(number(angle)), math.sin(number(angle))
    rows = [(c, 0.0, -s, 0.0), (0.0, 1.0, 0.0, 0.0), (s, 0.0, c, 0.0), (0.0, 0.0, 0.0, 1.0)]
    return [F(v) for row in rows for v in row]


def make_case(seed):
    rng = random.Random(seed)
    entry = ENTRIES[seed % len(ENTRIES)]
    actor = bytearray(rng.getrandbits(8) for _ in range(0x320))
    c = {'seed': seed, 'entry': entry}
    # the record: the float fields these routines read. Edge words (signed
    # zeros, denormals, the largest finite value, angles past +-pi) stand in
    # for "wild" values; no NaN or infinity reaches them (a stored word never
    # holds one, and 001B1470 would loop on it in the original).
    edge = rng.random() < 0.08
    angle = lambda: rng.choice((0x80000000, 0x00000001, 0x80000001, F(1500.0), F(-1500.0),
                                F(9.5), PI, PI ^ 0x80000000)) if edge else F(rng.uniform(-3.2, 3.2))
    put(actor, 0xC4, 4, angle())
    put(actor, 0x24C, 4, angle())
    matrix = yaw_matrix_words(F(rng.uniform(-3.2, 3.2)))
    if edge:
        matrix = [rng.choice((0x7F7FFFFF, 0xFF7FFFFF, 0x00000001, 0x80000000, F(1e30), matrix[i]))
                  for i in range(16)]
    for i in range(16):
        put(actor, 0xD0 + 4 * i, 4, matrix[i])
    for i in range(3):
        put(actor, 0xD0 + 48 + 4 * i, 4, F(rng.uniform(-500, 500)))
    put(actor, 4, 1, rng.choice((1, 1, 2)))
    put(actor, 5, 1, rng.choice((9, 9, 0x1C, 0)))
    c['yaw_camera'] = angle()
    if entry == '001755B0' and rng.random() < 0.4:
        # aim at |error| == pi/2 exactly (and one ULP either side)
        heading = wrap(M.ee_add(M.ee_add(PI, int.from_bytes(actor[0x24C:0x250], 'little')), c['yaw_camera']))
        target = rng.choice((HALF_PI, HALF_PI ^ 0x80000000, HALF_PI + 1, HALF_PI - 1))
        c4 = M.ee_sub(heading, target)
        put(actor, 0xC4, 4, c4)
    c['actor'] = bytes(actor)
    c['scratch'] = [rng.getrandbits(32) for _ in range(5)]
    c['spad3B8D'] = rng.choice((0, 0, 0, 1))
    c['gait'] = rng.choice((0, 1, 2, 3, 3))
    c['stick'] = (rng.choice((0, 0x80, 0xFF, rng.randrange(256))), rng.choice((0, 0x80, 0xFF, rng.randrange(256))))
    # the ledge frame
    point = [F(rng.uniform(100, 500)), F(rng.uniform(150, 300)), F(rng.uniform(100, 500))]
    angle = rng.uniform(-3.2, 3.2)
    normal = [F(math.sin(angle)), F(rng.choice((0.0, 0.0, rng.uniform(-0.2, 0.2)))), F(math.cos(angle))]
    if rng.random() < 0.1:
        normal = [F(rng.uniform(-2, 2)) for _ in range(3)]
    heading = F(rng.uniform(-3.2, 3.2))
    c['ledge'] = {'point': point, 'normal': normal, 'heading': heading,
                  'matrix': yaw_matrix_words(heading) if rng.random() < 0.8 else
                  [F(rng.uniform(-2, 2)) for _ in range(16)]}
    c['y'] = rng.choice((F(rng.uniform(150, 330)), F(number(point[1]) + 14.0), F(rng.uniform(-5, 5))))
    c['wide'] = rng.choice((0, 1, 1, 7))
    # the column table (0019A180)
    count = rng.choice((0, 1, 2, 5, 16))
    entries = []
    for i in range(16):
        aux = rng.choice((0.0, -0.0, 0.3, -0.3, 0.70020753, -0.70020753, 1.7320508, -1.7320508,
                          number(0x3F3340CC), number(0x3F3340CE), number(0xBF3340CE), number(0x3FDDB3D8),
                          number(0xBFDDB3D8), number(0xBFDDB3D6), 5.0, -5.0, rng.uniform(-3, 3)))
        entries.append((rng.choice((0x8001, 0x8000, 1, 0, 0x4001)), F(aux), rng.randrange(256),
                        rng.choice((0x2005, 0x2046, 0x8046, 0xFFFF, rng.randrange(0x10000)))))
    c['table'] = (count, entries)
    c['index'] = rng.randrange(0, count + 2)
    c['quiet'] = rng.random() < 0.3
    return c


# ======================================================================
# The original side
# ======================================================================

class UnitOracle:
    def __init__(self, elf):
        self.ee = CoverEE(elf)
        for address, name in HOOKED.items():
            self.ee.hooks[address] = self.hook(name)

    def hook(self, name):
        def run(ee):
            if name == 'sweep':
                assert ee.arg(0) == ACTOR, ('sweep p', hex(ee.arg(0)))
                args = (tuple(ee.load(ee.arg(1) + 4 * i) for i in range(4)),
                        tuple(ee.load(ee.arg(2) + 4 * i) for i in range(4)), ee.arg(3))
            elif name == 'atan2':
                args = (ee.f[12] & MASK, ee.f[13] & MASK)
            else:
                args = (ee.f[12] & MASK,)
            self.log.append((name,) + args)
            index, value = self.script.next(name, args)
            if name == 'sweep':
                kind, node = value
                ee.save(0x700031D8, kind)
                if kind:
                    ee.save(0x700031D0, NODE)
                    ee.save(NODE + 0x1A, node, 2)
                else:
                    ee.save(0x700031D0, 0)
                ee.ret_int(kind)
            else:
                ee.f[0] = value
        return run

    def run(self, case, script):
        ee = self.ee
        self.script, self.log = script, []
        ee.r, ee.rh = [0] * 32, [0] * 32
        ee.f, ee.acc, ee.cond = [0] * 32, 0, False
        ee.vacc, ee.q = [0, 0, 0, 0], 0
        ee.r[28], ee.r[29] = 0x27D370, shared.STACK_TOP
        ee.write(ACTOR, case['actor'])
        for i in range(4): ee.save(0x700038A0 + 4 * i, case['scratch'][i])
        ee.save(0x70003A20, case['scratch'][4])
        ee.save(0x70003B8D, case['spad3B8D'], 1)
        ee.save(0x810E57, case['gait'], 1)
        ee.save(0x810E64, case['stick'][0], 1); ee.save(0x810E65, case['stick'][1], 1)
        ee.save(0x8106A0, case['yaw_camera'])
        l = case['ledge']
        for i in range(3):
            ee.save(0x70003050 + 4 * i, l['point'][i]); ee.save(0x70003060 + 4 * i, l['normal'][i])
            ee.save(0x700031B0 + 4 * i, l['point'][i]); ee.save(NODE + 0x24 + 4 * i, l['normal'][i])
        ee.save(0x700031D0, NODE)
        ee.save(0x700031E4, l['heading'])
        for i in range(16): ee.save(0x70003070 + 4 * i, l['matrix'][i])
        count, entries = case['table']
        ee.save(0x700031E0, count)
        for i, (flags, aux, kind, node) in enumerate(entries):
            ee.save(0x70003170 + 2 * i, flags, 2)
            ee.save(0x282250 + 4 * i, aux)
            ee.save(0x70003130 + 4 * i, OBJECTS + 0x100 * i)
            ee.save(OBJECTS + 0x100 * i + 0x54, kind, 1)
            ee.save(OBJECTS + 0x100 * i + 0x1A, node, 2)
        entry = case['entry']
        address = {v: k for k, v in NAMES.items()}[entry]
        args = {'00177510': (), '001775E0': (ACTOR, case['wide']), '0019A180': (0, case['index'])}
        for i, value in enumerate(args.get(entry, (ACTOR,))): ee.r[4 + i] = shared.sx32(value)
        ee.f[12] = case['y']
        ee.r[31] = shared.RETURN
        ee.run(address)
        out = {'actor': ee.read(ACTOR, 0x320), 'log': self.log,
               'scratch': [ee.load(0x700038A0 + 4 * i) for i in range(4)] + [ee.load(0x70003A20)],
               'v0': None}
        if entry != '00174FD0' and entry != '00177510':
            out['v0'] = s32(ee.r[2])
        if entry == '0019A180':
            out['v0'] = s32(ee.r[2] & 0xFFFF | (0xFFFF0000 if ee.r[2] & 0x8000 else 0))
            assert s32(ee.r[2]) == out['v0'], ('0019A180 returns a sign-extended halfword', hex(ee.r[2]))
        if entry == '00177510':
            out['ledge'] = ([ee.load(0x70003050 + 4 * i) for i in range(3)] +
                            [ee.load(0x70003060 + 4 * i) for i in range(3)] + [ee.load(0x700031E4)] +
                            [ee.load(0x70003070 + 4 * i) for i in range(16)])
        return out


# ======================================================================
# The native side
# ======================================================================

class NativeRun:
    def __init__(self, native, case, script, missing=None):
        self.native, self.case, self.script, self.log = native, case, script, []
        self.live = LiveActor()
        C.memmove(self.live.bytes, case['actor'], 0x320)
        self.scratch = Scratch()
        for i in range(4): self.scratch.s38A0[i] = case['scratch'][i]
        self.scratch.s3A20 = case['scratch'][4]
        self.bytes = {k: U8(case[k]) for k in ('spad3B8D', 'gait')}
        self.bytes['x'], self.bytes['y'] = U8(case['stick'][0]), U8(case['stick'][1])
        self.yaw = U32(case['yaw_camera'])
        fns = {'sweep': SWEEP_FN(self.sweep), 'atan2': MATH2_FN(self.atan2), 'cosine': MATH1_FN(self.cosine)}
        if missing in fns: fns[missing] = type(fns[missing])()
        self.helpers = Helpers(None, fns['sweep'], fns['atan2'], fns['cosine'],
                               C.pointer(self.bytes['spad3B8D']), C.pointer(self.bytes['gait']),
                               C.pointer(self.bytes['x']), C.pointer(self.bytes['y']),
                               C.pointer(self.yaw), C.pointer(self.scratch))
        self.keep = fns
        if missing == 'scratch': self.helpers.scratch = P(Scratch)()
        if missing == 'd8106A0': self.helpers.d8106A0 = P(U32)()
        if missing == 'spad3B8D': self.helpers.spad3B8D = P(U8)()

    def call(self, name, args):
        self.log.append((name,) + args)
        index, value = self.script.next(name, args)
        if self.script.fail_at == index:
            return None
        return value

    def sweep(self, _, actor, start, end, mask, out):
        assert C.addressof(actor.contents) == C.addressof(self.live), 'sweep record'
        value = self.call('sweep', (tuple(fb(start[i]) for i in range(4)), tuple(fb(end[i]) for i in range(4)), mask))
        if value is None: return -1
        kind, node = value
        out[0].kind, out[0].node = kind, node if kind else 0
        return kind

    def atan2(self, _, y, x):
        value = self.call('atan2', (fb(y), fb(x)))
        return number(value if value is not None else 0)

    def cosine(self, _, x):
        value = self.call('cosine', (fb(x),))
        return number(value if value is not None else 0)

    def run(self):
        n, case = self.native, self.case
        H, A = C.byref(self.helpers), C.byref(self.live)
        entry = case['entry']
        result = C.c_int(-99)
        y = number(case['y'])
        l = case['ledge']
        ledge = Ledge()
        for i in range(3):
            ledge.point[i] = number(l['point'][i]); ledge.normal[i] = number(l['normal'][i])
        ledge.heading = number(l['heading'])
        for i in range(16): ledge.matrix[i] = number(l['matrix'][i])
        v0 = None
        out_ledge = None
        if entry == '001755B0':
            status = n.em_player_record_001755B0(H, A, C.byref(result)); v0 = result.value
        elif entry == '0017F320':
            status = n.em_player_record_0017F320(H, A, C.byref(result)); v0 = result.value
        elif entry == '00188550':
            status = n.em_player_record_00188550(H, A, C.byref(result)); v0 = result.value
        elif entry == '00174FD0':
            status = n.em_player_record_00174FD0(H, A)
        elif entry == '00177510':
            hit = ProbeHit()
            for i in range(3):
                hit.point[i] = number(l['point'][i]); hit.normal[i] = number(l['normal'][i])
            out_ledge = Ledge()
            status = n.em_player_record_00177510(H, C.byref(hit), C.byref(out_ledge))
        elif entry == '001775E0':
            status = n.em_player_record_001775E0(H, A, C.byref(ledge), case['wide'], y, C.byref(result))
            v0 = result.value
        elif entry == '001776E0':
            status = n.em_player_record_001776E0(H, A, C.byref(ledge), y, C.byref(result)); v0 = result.value
        elif entry == '00177CF0':
            status = n.em_player_record_00177CF0(H, A, C.byref(ledge), y, C.byref(result)); v0 = result.value
        else:
            count, entries = case['table']
            t = Table()
            t.count = count
            for i, (flags, aux, kind, node) in enumerate(entries):
                t.flags[i], t.aux[i], t.object_kind[i] = flags, number(aux), kind
                t.object_node[i] = node - 0x10000 if node & 0x8000 else node
            status = n.em_player_record_0019A180(H, C.byref(t), case['index'], C.byref(result))
            v0 = result.value
        got = {'actor': bytes(self.live.bytes), 'log': self.log,
               'scratch': list(self.scratch.s38A0) + [self.scratch.s3A20], 'v0': v0}
        if out_ledge is not None:
            got['ledge'] = ([fb(out_ledge.point[i]) for i in range(3)] + [fb(out_ledge.normal[i]) for i in range(3)] +
                            [fb(out_ledge.heading)] + [fb(out_ledge.matrix[i]) for i in range(16)])
        return status, got


# ======================================================================
# Runs
# ======================================================================

ELF = NATIVE = ORACLE = None


def run_case(seed):
    global ORACLE
    if ORACLE is None:
        ORACLE = UnitOracle(ELF)
    case = make_case(seed)
    want = ORACLE.run(case, Script(seed, quiet=case['quiet']))
    status, got = NativeRun(NATIVE, case, Script(seed, quiet=case['quiet'])).run()
    where = (seed, case['entry'])
    assert status == 0, (where, 'native fault', status)
    assert want['log'] == got['log'], (where, 'worker calls', want['log'], got['log'])
    if want['actor'] != got['actor']:
        diff = [hex(k) for k in range(0x320) if want['actor'][k] != got['actor'][k]]
        raise AssertionError((where, 'record bytes differ at', diff[:24]))
    assert want['scratch'] == got['scratch'], (where, 'scratch', [hex(v) for v in want['scratch']],
                                               [hex(v) for v in got['scratch']])
    assert want['v0'] == got['v0'], (where, 'result', want['v0'], got['v0'])
    if case['entry'] == '00177510':
        assert want['ledge'] == got['ledge'], (where, 'ledge frame', want['ledge'], got['ledge'])
    faults = 0
    sweeps = [k for k, e in enumerate(want['log']) if e[0] == 'sweep']
    if sweeps and seed % 3 == 0:
        k = random.Random(seed).choice(sweeps)
        status, cut = NativeRun(NATIVE, case, Script(seed, fail_at=k, quiet=case['quiet'])).run()
        assert status == -1, (where, 'fault not reported', k)
        assert cut['log'] == want['log'][:k + 1], (where, 'calls after a fault', k)
        faults = 1
    return case['entry'], len(want['log']), faults, tuple(sorted(ORACLE.ee.outcomes))


REQUIRED = {  # entry -> the pointers / workers it refuses to run without
    '001755B0': ('scratch', 'd8106A0'), '0017F320': ('sweep', 'scratch'),
    '00174FD0': ('spad3B8D', 'cosine', 'atan2', 'scratch'), '00177510': ('atan2',),
    '001775E0': ('sweep', 'scratch'), '001776E0': ('sweep', 'scratch'), '00177CF0': ('sweep', 'scratch'),
}


def missing_worker_checks(native):
    """Each required worker or pointer missing: the entry returns -1 before
    any write and calls nothing."""
    count = 0
    for entry, fields in REQUIRED.items():
        base = make_case(ENTRIES.index(entry))
        for field in fields:
            status, got = NativeRun(native, base, Script(1), missing=field).run()
            assert status == -1 and got['log'] == [] and got['actor'] == base['actor'], (entry, field)
            assert got['scratch'] == base['scratch'], (entry, field, 'scratch written')
            count += 1
    return count


def wrap_bound_checks(native):
    """001B1470's bounded domain (em_script_host_workers.h): a 001755B0
    heading sum with |x| >= 4096.0, which the original would loop on for
    thousands of iterations and a stick / camera angle never gives, faults
    (-1) before 0x70003A20 is written."""
    count = 0
    for words in ((F(3000.0), F(1100.0)), (F(-3000.0), F(-1100.0)), (0x7F7FFFFF, 0)):
        case = make_case(ENTRIES.index('001755B0'))
        actor = bytearray(case['actor'])
        put(actor, 0x24C, 4, words[0])
        case = dict(case, actor=bytes(actor), yaw_camera=words[1])
        status, got = NativeRun(native, case, Script(1)).run()
        assert status == -1 and got['actor'] == case['actor'] and got['scratch'] == case['scratch'], words
        count += 1
    return count


# ======================================================================
# Captures: the route beats' end snapshots
# ======================================================================

PLAYER = shared.PLAYER
CAPTURE_BEATS = ('00_panel_no_battery', '01_battery', '02_elevator_refusal', '03_panel_power',
                 '04_elevator_ride', '05_boxes', '06_hill_slide', '07_truck_preview',
                 '08_truck_crossing', '09_fence_door', '10_cage_roof_roger', '11_crevice_prompt',
                 '12_crevice_jump', '13_east_tower', '14_roger_encounter')


def capture_jobs():
    """The entry points run on one capture: each as captured, plus variants
    the capture makes meaningful (the stick angle and bytes, the ledge
    heights over the ledge frame the capture's last probe left)."""
    jobs = [('00188550', {}), ('0017F320', {}), ('00174FD0', {}), ('001755B0', {})]
    for angle in (0.5, -2.9, 2.2):
        jobs.append(('001755B0', {'stick': F(angle)}))
    for gait, x, y in ((3, 0x00, 0x80), (2, 0xFF, 0x40), (1, 0x80, 0xFF)):
        jobs.append(('00174FD0', {'gait': gait, 'x': x, 'y': y}))
    for lift in (4.5, 14.0, 26.0):
        jobs.append(('001775E0', {'lift': lift, 'wide': 1}))
        jobs.append(('001776E0', {'lift': lift}))
        jobs.append(('00177CF0', {'lift': lift}))
    jobs.append(('001775E0', {'lift': 1.0, 'wide': 0}))
    jobs.append(('00177510', {}))
    return jobs


def capture_run(job):
    """One capture: the original side and the record side each keep one
    interpreter over the captured world; before every run the player record
    and the scratchpad are restored from the capture, and after all runs the
    two RAM images must be identical."""
    beat, ram, spad = job
    original, world = FallEE(ELF, ram, spad), FallEE(ELF, ram, spad)
    record0 = original.read(PLAYER, 0x320)
    globals0 = [(a, original.load(a, 1)) for a in (0x810E57, 0x810E64, 0x810E65)]
    ok = skipped = 0
    for entry, variant in capture_jobs():
        for ee in (original, world):
            ee.spad[:] = spad
            ee.write(PLAYER, record0)
            for address, value in globals0: ee.save(address, value, 1)
            if 'gait' in variant:
                ee.save(0x810E57, variant['gait'], 1); ee.save(0x70003B8D, 0, 1)
                ee.save(0x810E64, variant['x'], 1); ee.save(0x810E65, variant['y'], 1)
            if 'stick' in variant:
                ee.save(PLAYER + 0x24C, variant['stick'])
        if entry == '001755B0':
            # 001755B0 reads +24C as the float 00174AC0 left there (its
            # callers run 00174AC0 first); a capture whose +24C holds
            # something else (00174FD0's quadrant) is not a state 001755B0
            # meets: skip it rather than loop in the original.
            stick = number(original.load(PLAYER + 0x24C))
            if not (abs(stick) < 3.5):
                skipped += 1
                continue
        if entry == '00177510' and original.load(0x700031D0) == 0:
            skipped += 1
            continue                               # no probe hit left in this capture
        point_y = number(original.load(0x70003054))
        y = F(point_y + variant.get('lift', 0.0))
        # the original, every callee original
        args = {'00177510': (), '001775E0': (PLAYER, variant.get('wide', 1))}.get(entry, (PLAYER,))
        address = {v: k for k, v in NAMES.items()}[entry]
        original.r, original.rh = [0] * 32, [0] * 32
        original.r[28], original.r[29] = 0x27D370, shared.STACK_TOP
        for i, value in enumerate(args): original.r[4 + i] = shared.sx32(value)
        original.f[12] = y
        original.r[31] = shared.RETURN
        original.run(address)
        want = {'actor': original.read(PLAYER, 0x320),
                'scratch': [original.load(0x700038A0 + 4 * i) for i in range(4)] + [original.load(0x70003A20)],
                'v0': s32(original.r[2]) if entry not in ('00174FD0', '00177510') else None}
        # the record entry, its workers the original routines on the second copy
        rw = RecordWorld(world, PLAYER)
        hit = ProbeHit()
        if entry == '00177510':
            record = world.load(0x700031D0)
            for i in range(3):
                hit.point[i] = number(world.load(0x700031B0 + 4 * i))
                hit.normal[i] = number(world.load(record + 0x24 + 4 * i))

        def sweep(_, actor, start, end, mask, out):
            assert C.addressof(actor.contents) == C.addressof(rw.live), 'sweep record'
            kind = rw.call(0x19AFE0, (PLAYER, rw.vector(0, start), rw.vector(1, end), mask))[0]
            out[0].kind = kind
            if kind:
                out[0].node = world.load(world.load(0x700031D0) + 0x1A, 2)
            return kind

        fns = (SWEEP_FN(rw.guard(sweep)),
               MATH2_FN(lambda _, yy, xx: number(rw.call(0x11E620, (), (yy, xx))[1])),
               MATH1_FN(lambda _, xx: number(rw.call(0x11DE90, (), (xx,))[1])))
        cells = {k: U8(world.load(a, 1)) for k, a in (('3B8D', 0x70003B8D), ('57', 0x810E57),
                                                      ('64', 0x810E64), ('65', 0x810E65))}
        yaw = U32(world.load(0x8106A0))
        helpers = Helpers(None, fns[0], fns[1], fns[2], C.pointer(cells['3B8D']), C.pointer(cells['57']),
                          C.pointer(cells['64']), C.pointer(cells['65']), C.pointer(yaw), C.pointer(rw.scratch))
        ledge = Ledge()
        for i in range(3):
            ledge.point[i] = number(world.load(0x70003050 + 4 * i))
            ledge.normal[i] = number(world.load(0x70003060 + 4 * i))
        ledge.heading = number(world.load(0x700031E4))
        for i in range(16): ledge.matrix[i] = number(world.load(0x70003070 + 4 * i))
        H, A, out = C.byref(helpers), C.byref(rw.live), C.c_int(-99)
        n = NATIVE
        if entry == '001755B0': status = n.em_player_record_001755B0(H, A, C.byref(out))
        elif entry == '00188550': status = n.em_player_record_00188550(H, A, C.byref(out))
        elif entry == '0017F320': status = n.em_player_record_0017F320(H, A, C.byref(out))
        elif entry == '00174FD0': status = n.em_player_record_00174FD0(H, A)
        elif entry == '001775E0':
            status = n.em_player_record_001775E0(H, A, C.byref(ledge), variant.get('wide', 1), number(y), C.byref(out))
        elif entry == '001776E0': status = n.em_player_record_001776E0(H, A, C.byref(ledge), number(y), C.byref(out))
        elif entry == '00177CF0': status = n.em_player_record_00177CF0(H, A, C.byref(ledge), number(y), C.byref(out))
        else:
            got_ledge = Ledge()
            status = n.em_player_record_00177510(H, C.byref(hit), C.byref(got_ledge))
        if rw.error is not None:
            raise rw.error
        where = (beat, entry, variant)
        assert status == 0, (where, 'native fault')
        rw.sync_in()
        assert world.read(PLAYER, 0x320) == want['actor'], (where, 'record')
        assert [world.load(0x700038A0 + 4 * i) for i in range(4)] + [world.load(0x70003A20)] == want['scratch'], \
            (where, 'scratch')
        if want['v0'] is not None:
            assert out.value == want['v0'], (where, 'result', out.value, want['v0'])
        if entry == '00177510':
            frame = ([fb(got_ledge.point[i]) for i in range(3)] + [fb(got_ledge.normal[i]) for i in range(3)] +
                     [fb(got_ledge.heading)] + [fb(got_ledge.matrix[i]) for i in range(16)])
            wanted = ([original.load(0x70003050 + 4 * i) for i in range(3)] +
                      [original.load(0x70003060 + 4 * i) for i in range(3)] + [original.load(0x700031E4)] +
                      [original.load(0x70003070 + 4 * i) for i in range(16)])
            assert frame == wanted, (where, 'ledge frame')
        ok += 1
    assert original.mem == world.mem, (beat, 'RAM differs after the runs')
    return beat, ok, skipped


# The quick run takes the beats where the climb, the slide and the falls
# happen; EM_TEST_FULL=1 takes all of 00..14.
QUICK_BEATS = ('05_boxes', '06_hill_slide', '11_crevice_prompt', '13_east_tower')


def capture_checks():
    """The captured route worlds, or the reason they are skipped."""
    route = shared.ROUTE
    jobs = []
    for beat in (CAPTURE_BEATS if reference_mode.FULL else QUICK_BEATS):
        files = [route / beat / name for name in ('eeMemory.bin', 'scratchpad.bin')]
        if not all(f.exists() for f in files):
            return None, 'missing %s' % files
        jobs.append((beat, files[0].read_bytes(), files[1].read_bytes()))
    return reference_mode.parallel_map(capture_run, jobs), None


def main():
    global ELF, NATIVE
    started = time.time()
    ELF = read_elf()
    callees = check_callee_set(ELF)
    NATIVE = build_native()
    total = reference_mode.pick(36000, 36000)
    seeds = reference_mode.select(range(total), 2700, 0x5B0)
    results = reference_mode.parallel_map(run_case, seeds)
    outcomes, entries, calls, faults = set(), {}, 0, 0
    for entry, count, fault, cover in results:
        outcomes.update(cover)
        entries[entry] = entries.get(entry, 0) + 1
        calls += count
        faults += fault
    sites = branch_sites(ELF)
    excluded = unreachable_outcomes(ELF)
    assert len(excluded) == 3, excluded
    missing = sorted('%06X %s' % (pc, 'taken' if taken else 'not taken')
                     for pc in sites for taken in (True, False)
                     if (pc, taken) not in outcomes and (pc, taken) not in excluded)
    assert not missing, ('branch outcomes never exercised', missing)
    refusals = missing_worker_checks(NATIVE) + wrap_bound_checks(NATIVE)
    captured, skipped = capture_checks()
    if skipped:
        print('route captures: SKIP (%s)' % skipped)
    else:
        print('route captures (%s): PASS %d runs on the captured worlds, %d skipped (no probe hit '
              'left, or +24C not a stick angle); record, scratch, ledge frame, result and RAM identical, '
              'every callee original' % ('00..14' if reference_mode.FULL else ', '.join(QUICK_BEATS),
                                         sum(n for _, n, _ in captured), sum(k for _, _, k in captured)))
    reference_mode.banner(reference_mode.part(len(seeds), total, 'cases'),
                          '%d jal targets (3 hooked, 7 run as original code)' % callees)
    print('player record helpers vs original instructions: PASS %d cases (%s), %d worker calls '
          'identical, every one of %d conditional branches both ways (3 unreachable outcomes '
          'excluded), %d fault-stop cuts, %d refusals (a missing worker, or a 001B1470 argument '
          'outside its bounded domain) (%.1fs)' % (
              len(seeds), ', '.join('%s %d' % kv for kv in sorted(entries.items())), calls,
              len(sites), faults, refusals, time.time() - started))


if __name__ == '__main__':
    main()
