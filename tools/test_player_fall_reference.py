#!/usr/bin/env python3
"""Execute the original fall/landing routines and compare em_player_fall.c.

docs/PLAYER_FALL.md. The user's pinned ELF (and, for the world mode, the
captured route RAM) supplies every instruction and table; none are embedded
here. Routines executed unmodified:

  00162DB0  state 5 fall         001639E0  state 7 drop
  00163B40  state 8 landing, with 00163C10 / 00163D50 / 00163E90 /
            00164220 / 001643B0 (its sub-state routines)
  0017C580  land                 00224290  landing hit check
  0021D250  surface 0x5D         0021D2E0  the fade wait
  00179880  drop accumulator

Every other callee is hooked, scripted per case and recorded (never
simulated as a claim about the callee); the native module gets the same
script through its workers. The test asserts that the hooked set is exactly
the set of jal targets of these routines, so no callee runs unhooked.

Arithmetic: the shared interpreter's COP1/VU0 arithmetic is known to deviate
(docs/EE_FLOAT_MODEL.md section 5a). FallEE routes every COP1 op and every
VU0 macro op through tools/ee_float_model.py (the measured model) instead;
the shared file is not edited.

Default run (~10 s): unit cases with branch coverage of every conditional
branch in the translated routines asserted. EM_TEST_FULL=1: the exhaustive
sweep. EM_TEST_WORLD=1: the route beats that fall and land (10, 11, 12, 14)
replayed over the captured RAM, the original stage against the stage with
these translations in place, compared byte for byte (whole RAM and
scratchpad every frame) and against the PCSX2 trace rows.
"""
import ctypes as C
import hashlib
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
from test_player_slide_reference import EE, read_elf, bits, number, s32, sx32  # noqa: E402

MASK = 0xFFFFFFFF
MASK64 = 0xFFFFFFFFFFFFFFFF
LANE = os.environ.get('EM_LANE', 'player_fall_reference')
OUT = ROOT / 'build' / LANE


# ======================================================================
# The interpreter with the measured float model
# ======================================================================

class FallEE(EE):
    """The shared EE core with COP1 and VU0 macro arithmetic taken from
    ee_float_model (raw bit patterns throughout; ACC and Q hold bits)."""

    def __init__(self, elf, ram=None, spad=None):
        super().__init__(elf, ram, spad)
        self.acc = 0
        self.vacc = [0, 0, 0, 0]
        self.q = 0

    def cop1(self, word, pc):
        rs, rt = word >> 21 & 31, word >> 16 & 31
        fs, fd, fn = word >> 11 & 31, word >> 6 & 31, word & 63
        f = self.f
        if rs == 0:                                         # mfc1
            if rt: self.r[rt] = sx32(f[fs])
            return
        if rs == 4: f[fs] = self.r[rt] & MASK; return        # mtc1
        if rs == 2:                                         # cfc1
            if rt: self.r[rt] = 0
            return
        if rs == 6: return                                  # ctc1
        if rs == 20:
            if fn == 32: f[fd] = M.ee_cvt_s_w(f[fs] & MASK); return
            raise AssertionError(('cvt.w', fn, hex(pc)))
        if rs != 16: raise AssertionError(('COP1', rs, hex(pc)))
        a, b = f[fs] & MASK, f[rt] & MASK
        if fn == 0: f[fd] = M.ee_add(a, b)
        elif fn == 1: f[fd] = M.ee_sub(a, b)
        elif fn == 2: f[fd] = M.ee_mul(a, b)
        elif fn == 3: f[fd] = M.ee_div(a, b)
        elif fn == 6: f[fd] = M.ee_mov(a)
        elif fn == 7: f[fd] = M.ee_neg(a)
        elif fn == 24: self.acc = M.ee_adda(a, b)
        elif fn == 25: self.acc = M.ee_suba(a, b)
        elif fn == 26: self.acc = M.ee_mula(a, b)
        elif fn == 28: f[fd] = M.ee_madd(self.acc, a, b)
        elif fn == 29: f[fd] = M.ee_msub(self.acc, a, b)
        elif fn == 36: f[fd] = M.ee_cvt_w_s(a)
        elif fn == 48: self.cond = False
        elif fn == 50: self.cond = bool(M.ee_c_eq(a, b))
        elif fn == 52: self.cond = bool(M.ee_c_lt(a, b))
        elif fn == 54: self.cond = bool(M.ee_c_le(a, b))
        else:   # SQRT/ABS/RSQRT/MAX/MIN/MADDA/MSUBA: absent from the original, unmeasured
            raise AssertionError(('COP1 op the model does not define', fn, hex(pc)))

    def macro(self, word):
        op, fs, ft, fd = word & 63, word >> 11 & 31, word >> 16 & 31, word >> 6 & 31
        mask = word >> 21 & 15
        x, y = [v & MASK for v in self.vf[fs]], [v & MASK for v in self.vf[ft]]
        lanes = [i for i in range(4) if mask & (8 >> i)]
        result, destination, accumulate = {}, fd, False
        if op < 4:
            result = {i: M.vu_lane('vaddbc', mask, op, x[i], y[op]) for i in lanes}
        elif op < 8:
            result = {i: M.vu_lane('vsubbc', mask, op - 4, x[i], y[op - 4]) for i in lanes}
        elif op < 12:
            result = {i: M.vu_lane('vmaddbc', mask, op - 8, x[i], y[op - 8], self.vacc[i]) for i in lanes}
        elif 24 <= op < 28:
            result = {i: M.vu_lane('vmulbc', mask, op - 24, x[i], y[op - 24]) for i in lanes}
        elif op == 28:
            result = {i: M.vu_lane('vmulq', mask, None, x[i], self.q) for i in lanes}
        elif op == 32:
            result = {i: M.vu_lane('vaddq', mask, None, x[i], self.q) for i in lanes}
        elif op == 40:
            result = {i: M.vu_lane('vadd', mask, None, x[i], y[i]) for i in lanes}
        elif op == 42:
            result = {i: M.vu_lane('vmul', mask, None, x[i], y[i]) for i in lanes}
        elif op == 44:
            result = {i: M.vu_lane('vsub', mask, None, x[i], y[i]) for i in lanes}
        elif op == 46:                                                       # vopmsub
            swz = {0: (1, 2), 1: (2, 0), 2: (0, 1)}
            result = {i: M.vu_lane('vopmsub', mask, None, x[swz[i][0]], y[swz[i][1]], self.vacc[i])
                      for i in lanes if i < 3}
        elif op >= 60:
            special = (word >> 6 & 31) << 2 | (op & 3)
            destination = ft
            if special in (0x30, 0x31):                                      # vmove / vmr32
                raw = list(self.vf[fs])
                if special == 0x31: raw = raw[1:] + raw[:1]
                for lane in lanes:
                    if ft: self.vf[ft][lane] = raw[lane]
                return
            if 0x18 <= special <= 0x1B:                                      # vmulabc
                bc = special - 0x18
                result = {i: M.vu_lane('vmulabc', mask, bc, x[i], y[bc]) for i in lanes}
                accumulate = True
            elif 0x08 <= special <= 0x0B:                                    # vmaddabc
                bc = special - 0x08
                result = {i: M.vu_lane('vmaddabc', mask, bc, x[i], y[bc], self.vacc[i]) for i in lanes}
                accumulate = True
            elif special == 0x2E:                                            # vopmula
                swz = {0: (1, 2), 1: (2, 0), 2: (0, 1)}
                result = {i: M.vu_lane('vopmula', mask, None, x[swz[i][0]], y[swz[i][1]])
                          for i in lanes if i < 3}
                accumulate = True
            elif special == 0x39:                                            # vsqrt
                self.q = M.vu_sqrt(y[word >> 23 & 3]); return
            elif special == 0x38:                                            # vdiv
                fsf, ftf = word >> 21 & 3, word >> 23 & 3
                self.q = M.vu_div(x[fsf], y[ftf], fsf, ftf); return
            elif special in (0x3B, 0x2F):                                    # vwaitq / vnop
                return
            else:
                raise AssertionError(('VU special the model does not define', hex(word), hex(special)))
        else:
            raise AssertionError(('VU op the model does not define', hex(word), op))
        for lane, value in result.items():
            if accumulate:
                self.vacc[lane] = value
            elif destination:
                self.vf[destination][lane] = value


class CoverEE(FallEE):
    """FallEE recording the outcome of every conditional branch inside the
    translated routines (unit mode only)."""

    def __init__(self, elf):
        super().__init__(elf)
        self.outcomes = set()

    def branch(self, word, pc):
        b = super().branch(word, pc)
        if b is not None and in_translated(pc):
            self.outcomes.add((pc, b[0]))
        return b


def nested_bits(ee, entry, args=(), fregs=(), wide=False):
    """EE.nested with the float argument registers set from raw bits (and,
    with wide, the integer argument registers set to whole 64-bit values)."""
    saved = (list(ee.r), list(ee.rh), ee.hi, ee.lo, list(ee.f), ee.acc,
             ee.cond, [list(v) for v in ee.vf], list(ee.vacc), ee.q)
    ee.r[29] = (ee.r[29] - 0x400) & ~15
    for i, value in enumerate(args): ee.r[4 + i] = value & MASK64 if wide else sx32(value)
    for i, value in enumerate(fregs): ee.f[12 + i] = value & MASK
    ee.r[31] = shared.RETURN
    ee.run(entry)
    result = (ee.r[2], ee.f[0])
    (ee.r, ee.rh, ee.hi, ee.lo, ee.f, ee.acc, ee.cond, ee.vf, ee.vacc, ee.q) = saved
    return result


# ======================================================================
# The routines and their callees
# ======================================================================

STATE5, STATE7, STATE8 = 0x162DB0, 0x1639E0, 0x163B40
LAND, LAND_CHECK, SURFACE5D, TELEPORT, DROP = 0x17C580, 0x224290, 0x21D250, 0x21D2E0, 0x179880
SUBSTATES = (0x163C10, 0x163D50, 0x163E90, 0x164220, 0x1643B0)
SIZES = {STATE5: 0x6E4, STATE7: 0x160, STATE8: 0xCC, 0x163C10: 0x13C, 0x163D50: 0x13C,
         0x163E90: 0x38C, 0x164220: 0x184, 0x1643B0: 0x214, LAND: 0x2D4, LAND_CHECK: 0x160,
         SURFACE5D: 0x8C, TELEPORT: 0x1A4, DROP: 0x50}
TRANSLATED = set(SIZES)


def in_translated(pc):
    return any(start <= pc < start + size for start, size in SIZES.items())


# Original callee -> worker name (every jal target of the routines above).
CALLEES = {
    0x1749A0: 'request', 0x1749F0: 'arbiter', 0x1C61D0: 'frames', 0x1FBD50: 'sound',
    0x1B61C0: 'rumble', 0x1EFD90: 'effect', 0x1AEDE0: 'fade', 0x1C6DA0: 'skeleton',
    0x174AC0: 'heading', 0x1755B0: 't55B0', 0x17D080: 'tD080', 0x17F320: 'tF320',
    0x21C190: 'tC190', 0x188550: 'pose_clip', 0x1B1470: 'wrap', 0x1B12B0: 'approach',
    0x1C94B0: 'trs', 0x1026A0: 'apply', 0x179450: 'query', 0x182870: 'land_sound',
    0x178B90: 'translate', 0x1764E0: 'probes', 0x175900: 'floor', 0x1796C0: 'fall_check',
    0x17C860: 'ledge', 0x17C440: 'reentry', 0x17C540: 'handoff', 0x21C120: 'r120',
    0x21C350: 'r350', 0x21C270: 'r270', 0x128350: 'convert', 0x1000E0: 't00E0',
}


def check_callee_set(elf):
    """Every jal target inside the translated routines is either translated
    here or hooked (so no original callee runs as part of the 'original'
    side while the native side calls a worker, and vice versa)."""
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


# ======================================================================
# Native side (ctypes)
# ======================================================================

class LiveActor(C.Structure):
    _fields_ = [('bytes', C.c_uint8 * 0x320), ('link_owner', C.c_void_p), ('link_prev', C.c_void_p),
                ('link_flags', C.c_uint8), ('link_type', C.c_uint8)]


class Scratch(C.Structure):
    _fields_ = [('s38A0', C.c_uint32 * 4), ('s3A20', C.c_uint32)]


P = C.POINTER
LA = P(LiveActor)
I, U32, FLT, VP = C.c_int, C.c_uint32, C.c_float, C.c_void_p
FN = {
    'request': C.CFUNCTYPE(I, VP, LA, I, I, FLT),
    'arbiter': C.CFUNCTYPE(I, VP, LA, I, FLT, FLT),
    'frames': C.CFUNCTYPE(I, VP, U32, I, P(C.c_int32)),
    'sound': C.CFUNCTYPE(I, VP, LA, I),
    'rumble': C.CFUNCTYPE(I, VP, I, I, I, I),
    'effect': C.CFUNCTYPE(I, VP, U32, P(U32), P(U32)),
    'fade': C.CFUNCTYPE(I, VP, I, I),
    'actor': C.CFUNCTYPE(I, VP, LA),
    'hip': C.CFUNCTYPE(I, VP, P(U32), P(U32)),
    'heading': C.CFUNCTYPE(I, VP, LA, I, P(I)),
    'test': C.CFUNCTYPE(I, VP, LA, P(I)),
    'wrap': C.CFUNCTYPE(I, VP, U32, P(U32)),
    'approach': C.CFUNCTYPE(I, VP, U32, U32, U32, P(U32)),
    'trs': C.CFUNCTYPE(I, VP, P(U32), P(U32), P(U32), P(U32)),
    'apply': C.CFUNCTYPE(I, VP, P(U32), P(U32), P(U32)),
    'query': C.CFUNCTYPE(I, VP, LA, P(U32), P(I)),
    'actor_int': C.CFUNCTYPE(I, VP, LA, I),
    'floor': C.CFUNCTYPE(I, VP, LA, I, P(I)),
    'ledge': C.CFUNCTYPE(I, VP, LA, U32, P(I)),
    'convert': C.CFUNCTYPE(I, VP, U32, P(C.c_uint64)),
    't2': C.CFUNCTYPE(I, VP, C.c_uint64, C.c_uint64, P(I)),
    'progress': C.CFUNCTYPE(I, VP, P(C.c_uint8)),
}
# EmPlayerLandWorkers, in header order: (field, FN kind).
WORKER_FIELDS = (
    ('request', 'request'), ('arbiter', 'arbiter'), ('clip_frames', 'frames'), ('sound', 'sound'),
    ('rumble', 'rumble'), ('effect', 'effect'), ('fade', 'fade'), ('skeleton', 'actor'),
    ('hip', 'hip'), ('heading', 'heading'), ('test_001755B0', 'test'), ('test_0017D080', 'test'),
    ('test_0017F320', 'test'), ('test_0021C190', 'test'), ('pose_clip', 'test'), ('wrap', 'wrap'),
    ('approach', 'approach'), ('trs', 'trs'), ('apply', 'apply'), ('floor_query', 'query'),
    ('land_sound', 'actor_int'), ('translate', 'actor_int'), ('probes', 'actor_int'),
    ('floor', 'floor'), ('fall_check', 'actor'), ('ledge', 'ledge'), ('reentry', 'actor_int'),
    ('handoff', 'actor'), ('react_0021C120', 'actor'), ('react_0021C350', 'actor'),
    ('react_0021C270', 'actor'), ('convert_00128350', 'convert'), ('test_001000E0', 't2'),
    ('progress_8106F1', 'progress'),
)


class Workers(C.Structure):
    _fields_ = [('context', VP), ('scratch', P(Scratch))] + [(name, FN[kind]) for name, kind in WORKER_FIELDS]


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / ('fall.dylib' if sys.platform == 'darwin' else 'fall.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc', 'src/game/em_player_fall.c',
                    '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    W = P(Workers)
    for name in ('em_player_fall_state5', 'em_player_fall_state7', 'em_player_fall_state8'):
        getattr(native, name).argtypes = [W, LA]
    for name in ('em_player_fall_land', 'em_player_fall_00163C10', 'em_player_fall_00163D50',
                 'em_player_fall_00163E90', 'em_player_fall_00164220', 'em_player_fall_001643B0'):
        getattr(native, name).argtypes = [W, LA]
    native.em_player_fall_land_check.argtypes = [W, LA, P(I)]
    native.em_player_fall_surface5d.argtypes = [W, LA, I]
    native.em_player_fall_teleport.argtypes = [W, LA, I, I]
    native.em_player_fall_0021D250.argtypes = [W, LA, I]
    native.em_player_fall_0021D2E0.argtypes = [W, LA, I, I]
    native.em_player_fall_drop.argtypes = [LA]
    native.em_player_fall_drop.restype = None
    native.em_player_fall_workers_bound.argtypes = [W]
    return native


# ======================================================================
# Scripted callee effects (identical on both sides)
# ======================================================================

def F(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def D(value):
    return struct.unpack('<Q', struct.pack('<d', value))[0]


FLAGS = (0, 0x1000, 0x8000, 0x9000, 0x200, 0x1200, 0x8200)


def effect_for(rng, name):
    """What a hooked callee does in this case: its return value, the actor
    bytes it writes (offset, size, value), a 0x70003A20 write, new node x/z
    (anim_eval_skeleton) and output vectors (apply/trs)."""
    e = {'ret': 0, 'writes': [], 's3A20': None, 'node': None, 'out': None, 'fret': None}
    w = e['writes']
    chance = rng.random
    if name == 'floor':
        contact = rng.choice((0, 0, 1, 0x81))
        e['ret'] = contact
        w.append((0xA, 1, contact))
        if chance() < 0.7: w.append((0x23A, 1, rng.choice((0, 5, 0x5D, 0x5D, 0x5A))))
        if chance() < 0.2: w.append((0x200, 4, rng.choice(FLAGS)))
        if chance() < 0.2: w.append((0x38, 4, F(rng.choice((0.0, 0.1, -0.0)))))
    elif name in ('probes', 'fall_check', 'translate', 'reentry', 'handoff', 'r120', 'r350', 'r270'):
        if chance() < 0.3: w.append((0x314, 1, rng.randrange(256)))
        if chance() < 0.3: w.append((0xB0 + 4 * rng.randrange(3), 4, F(rng.uniform(-300, 300))))
        if chance() < 0.3: w.append((0x200, 4, rng.choice(FLAGS)))
        if name in ('reentry', 'handoff') and chance() < 0.3: w.append((0x23F, 1, rng.randrange(4)))
        if name in ('r350', 'r270', 'r120') and chance() < 0.4:
            w.append((0x22C, 4, F(rng.choice((0.0, 3.0)))))
        if name in ('r350', 'r270') and chance() < 0.4:
            w.append((rng.choice((0x38, 0x2E0)), 4, F(rng.choice((0.0, 0.05, 0.3)))))
    elif name == 'heading':
        e['ret'] = rng.choice((0, 1, 1, 7))
        if chance() < 0.6: w.append((0x23F, 1, rng.randrange(4)))
        if chance() < 0.3: w.append((0xC4, 4, F(rng.uniform(-3.2, 3.2))))
        if chance() < 0.3: e['s3A20'] = F(rng.choice((-60.0, -20.0, -5.0, -104.5, 1.0)))
    elif name in ('t55B0', 'tD080'):
        e['ret'] = rng.choice((0, 1, 2))
        if chance() < 0.3: e['s3A20'] = F(rng.choice((-60.0, -20.0, -5.0, 1.0)))
    elif name in ('tF320', 'tC190', 't00E0'):
        e['ret'] = rng.choice((0, 1, -3))
    elif name == 'ledge':
        e['ret'] = rng.choice((0, 1))
        if e['ret'] and chance() < 0.7: w.append((5, 1, 4))
    elif name == 'convert':
        # 00128350 returns the double in the whole 64-bit $v0. The worker is
        # scripted, so any double will do: +220-like values (60.0's low word
        # is 0, so a copy of only the low word cannot tell 60.0 from 0.0) and
        # 1e-300, whose low word is not 0.
        e['ret'] = D(rng.choice((60.0, 0.0, -0.0, 100.0, -5.0, 1.5, 1e-300)))
    elif name == 'pose_clip':
        e['ret'] = rng.choice((0x6E, 0x72, 0x1C3, -1 & MASK, 0x8000))
    elif name == 'frames':
        e['ret'] = rng.choice((25, 40, 0, -7, 0x1000001, 120))
    elif name == 'query':
        e['ret'] = rng.choice((0, 0, 1, 2))
        if chance() < 0.8:
            e_value = rng.choice((-22.2, -22.200001, -22.199999, -30.0, -10.0, 0.0, -0.0))
            w.append((0x258, 4, F(e_value) if e_value != -22.2 else M.ee_neg(M.ee_sub(0x41C00000, 0x3FE66666))))
    elif name == 'apply':
        e['out'] = [F(rng.uniform(-400, 400)) for _ in range(4)]
    elif name == 'trs':
        e['out'] = [F(rng.uniform(-2, 2)) for _ in range(16)]
    elif name in ('wrap', 'approach'):
        e['fret'] = F(rng.choice((rng.uniform(-3.2, 3.2), 0.0, 3.14159274)))
    elif name == 'skeleton':
        if chance() < 0.8: e['node'] = (F(rng.uniform(-500, 500)), F(rng.uniform(-500, 500)))
    elif name == 'request':
        if chance() < 0.3: w.append((0x200, 4, rng.choice(FLAGS)))
        if chance() < 0.2: w.append((rng.choice((0x38, 0x2E0)), 4, F(rng.choice((0.0, 0.05, 0.3)))))
        if chance() < 0.3: w.append((0x20C, 2, rng.randrange(0x200)))
    return e


class Script:
    def __init__(self, seed, fail_at=None):
        self.seed, self.count, self.fail_at = seed, 0, fail_at

    def next(self, name):
        rng = random.Random('%d:%d:%s' % (self.seed, self.count, name))
        index = self.count
        self.count += 1
        return index, effect_for(rng, name)


# ======================================================================
# One unit case
# ======================================================================

ACTOR = 0x680000
NODE_TABLE, NODE = 0x6C0000, 0x6D0000
CALLER_S1 = 0x5A5A0004          # the $s1 a caller hands down (bit 2 set on purpose)
ENTRY_POINT = {'state5': STATE5, 'state7': STATE7, 'state8': STATE8, 'land': LAND,
               'land_check': LAND_CHECK, 'surface5d': SURFACE5D, 'teleport': TELEPORT, 'drop': DROP}


def put(actor, offset, size, value):
    actor[offset:offset + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'little')


def make_case(seed):
    rng = random.Random(seed)
    entry = rng.choice(('state5',) * 5 + ('state7',) * 3 + ('state8',) * 8 +
                       ('land',) * 4 + ('land_check',) * 2 + ('surface5d', 'teleport', 'teleport', 'drop'))
    actor = bytearray(rng.getrandbits(8) for _ in range(0x320))
    wild = rng.random() < 0.08          # leave the float fields as random words
    put(actor, 4, 1, 1)
    sub6 = {'state5': (0, 0, 0, 0, 1, 2, 3, 4, 5, 0xA, 0xB, 0xB, 0x63, 7),
            'state7': (0, 1, 1, 2, 3), 'state8': (0, 1, 2, 3, 3, 4, 5, 0xA, 0xA, 6)}.get(entry, (0,))
    put(actor, 6, 1, rng.choice(sub6))
    if entry == 'state8' and actor[6] == 3:
        put(actor, 7, 1, rng.randrange(17))
    elif entry == 'state8' and actor[6] == 0xA:
        put(actor, 7, 1, rng.randrange(7))
    else:
        put(actor, 7, 1, rng.choice((0, 0, 1, 1, 2, 3, 4)))
    choose = lambda values: F(rng.choice(values))
    if not wild:
        put(actor, 0x3C, 4, choose((10.0, 12.0, 12.000001, 11.999999, 15.0, 15.000001, 38.0, 38.000004,
                                    40.0, 40.000004, 50.0)))
        put(actor, 0x38, 4, choose((0.0, 0.2, 0.5, 0.01, 0.022727273, 0.8)))
        put(actor, 0x2E0, 4, choose((0.0, 0.01, 0.2, 0.5, 0.0033333334)))
        base = rng.choice((200.0, 289.75, 0.0, -50.0, 279.65))
        diff = rng.choice((-104.00001, -104.0, -103.99, -50.00001, -50.0, -49.99, -14.5, -14.500001,
                           -14.49, 0.0, -200.0, -12.0, -11.99, -12.01, 5.0))
        put(actor, 0x2F4, 4, F(base))
        put(actor, 0xB4, 4, F(base + diff))
        put(actor, 0x220, 4, choose((0.0, -0.0, 1e-40, -5.0, 10.0, 60.0, 60.000004, 100.0)))
        put(actor, 0x228, 4, choose((99.99999, 100.0, 150.0)))
        put(actor, 0x224, 4, choose((0.0, -0.0, 1e-40, 5.0)))
        put(actor, 0x22C, 4, choose((0.0, -0.0, 1e-40, 3.0)))
        put(actor, 0x2EC, 4, choose((-0.2, -3.99, -3.97, -4.0, 0.0, -0.4)))
        put(actor, 0x258, 4, choose((-22.2, -30.0, 0.0)))
        for off in (0x290, 0x294, 0x298, 0xB0, 0xB8, 0x250):
            put(actor, off, 4, F(rng.uniform(-400, 400)))
        for off in (0xC0, 0xC4, 0xC8):
            put(actor, off, 4, F(rng.uniform(-3.2, 3.2)))
    put(actor, 0x200, 4, rng.choice(FLAGS) | (rng.getrandbits(8) if rng.random() < 0.3 else 0))
    put(actor, 0x234, 1, rng.choice((0, 1, 1, 2)))
    put(actor, 0xF, 1, rng.choice((0x63, 0xB, 0, 5, 5, 5)))
    put(actor, 0x23B, 1, rng.choice((0x39, 5, 5)))
    put(actor, 0x23A, 1, rng.choice((0x5D, 5, 5, 0)))
    put(actor, 0x25C, 1, rng.randrange(4))
    put(actor, 0x23F, 1, rng.randrange(4))
    put(actor, 0x25F, 1, rng.choice((0, 0, 2)))
    put(actor, 0x300, 2, rng.choice((0, 0x8000, 0x7FFF)))
    put(actor, 0x319, 1, rng.choice((0, 1)))
    put(actor, 0x1F0, 1, rng.choice((0xE, 0xF, 0)))
    put(actor, 0x28, 2, rng.choice((0, 0, 1, 5, 0xFFFF)))
    put(actor, 0x302, 1, rng.choice((0, 1)))
    return {
        'seed': seed, 'entry': entry, 'actor': bytes(actor),
        'progress': rng.choice((0, 1)),
        'node': (F(rng.uniform(-500, 500)), F(rng.uniform(-500, 500))),
        'scratch': [rng.getrandbits(32) for _ in range(5)],
        'arg': rng.choice((0, 0, 1)),                       # 0021D250 a1
        'frames': rng.choice((0x78, 0x78, 3, 0x8001)),      # 0021D2E0 a1 (a halfword)
        'hold': rng.choice((0, 0, 1)),                      # 0021D2E0 a2
    }


class UnitOracle:
    """The original routines on a FallEE with every callee hooked."""

    def __init__(self, elf):
        self.ee = CoverEE(elf)
        for address, name in CALLEES.items():
            self.ee.hooks[address] = self.hook(name)

    def hook(self, name):
        def run(ee):
            entry = self.log_entry(name, ee)
            index, e = self.script.next(name)
            self.log.append(entry)
            for offset, size, value in e['writes']:
                ee.save(ACTOR + offset, value, size)
            if e['s3A20'] is not None:
                ee.save(0x70003A20, e['s3A20'])
            if e['node'] is not None:
                ee.save(NODE + 0xC0, e['node'][0]); ee.save(NODE + 0xC8, e['node'][1])
            if e['out'] is not None:
                for i, value in enumerate(e['out']): ee.save(ee.arg(0) + 4 * i, value)
            if e['fret'] is not None:
                ee.f[0] = e['fret']
            if name == 'convert':
                ee.r[2] = e['ret'] & MASK64       # the double, the whole 64-bit $v0
            else:
                ee.ret_int(e['ret'])
        return run

    def log_entry(self, name, ee):
        a = ee.arg
        vec = lambda address, n: tuple(ee.load(address + 4 * i) for i in range(n))
        if name in ('request', 'arbiter', 'sound', 'skeleton', 'heading', 't55B0', 'tD080', 'tF320',
                    'tC190', 'pose_clip', 'query', 'land_sound', 'translate', 'probes', 'floor',
                    'fall_check', 'ledge', 'reentry', 'handoff', 'r120', 'r350', 'r270'):
            assert a(0) == ACTOR, (name, hex(a(0)))
        if name == 'request': return (name, s32(a(1)), s32(a(2)), ee.f[12] & MASK)
        if name == 'arbiter': return (name, s32(a(1)), ee.f[12] & MASK, ee.f[13] & MASK)
        if name == 'frames': return (name, a(0), s32(a(1)))
        if name == 'sound':
            assert s32(a(2)) == 0 and ee.f[12] & MASK == F(300.0), ('sound arguments', a(2), ee.f[12])
            return (name, s32(a(1)))
        if name in ('rumble',): return (name,) + tuple(s32(a(i)) for i in range(4))
        if name == 'effect':
            assert a(1) == 0x700038A0 and a(2) == ACTOR + 0xB0, ('effect pointers', hex(a(1)), hex(a(2)))
            return (name, a(0), vec(a(1), 4), vec(a(2), 4))
        if name == 'fade': return (name, s32(a(0)), s32(a(1)))
        if name in ('heading', 'land_sound', 'translate', 'reentry'): return (name, s32(a(1)))
        if name == 'probes':
            s1 = self.ee.r[17] & MASK
            source = {0: 0, ACTOR: 1, CALLER_S1: 2}.get(s1)
            assert source is not None, ('001764E0 $s1', hex(s1))
            return (name, source)
        if name == 'floor': return (name, s32(a(1)))
        if name == 'wrap': return (name, ee.f[12] & MASK)
        if name == 'approach': return (name, ee.f[12] & MASK, ee.f[13] & MASK, ee.f[14] & MASK)
        if name == 'trs':
            assert (a(0), a(1), a(2), a(3)) == (ACTOR + 0xD0, ACTOR + 0xB0, ACTOR + 0xC0, ACTOR + 0x60)
            return (name, vec(a(1), 3), vec(a(2), 3), vec(a(3), 3))
        if name == 'apply':
            assert a(0) == 0x700038A0 and a(1) == ACTOR + 0xD0, ('apply pointers', hex(a(0)), hex(a(1)))
            return (name, vec(a(1), 16), vec(a(2), 4))
        if name == 'query': return (name, vec(a(1), 3))
        if name == 'ledge': return (name, ee.f[12] & MASK)
        if name == 'convert': return (name, ee.f[12] & MASK)
        if name == 't00E0': return (name, ee.r[4] & MASK64, ee.r[5] & MASK64)
        return (name,)

    def run(self, case, script):
        ee = self.ee
        self.script, self.log = script, []
        ee.r, ee.rh = [0] * 32, [0] * 32
        ee.f, ee.acc, ee.cond = [0] * 32, 0, False
        ee.vacc, ee.q = [0, 0, 0, 0], 0
        ee.r[28], ee.r[29], ee.r[17] = 0x27D370, shared.STACK_TOP, CALLER_S1
        ee.write(ACTOR, case['actor'])
        ee.save(0x275B40, NODE_TABLE); ee.save(NODE_TABLE + 4, NODE)
        ee.save(NODE + 0xC0, case['node'][0]); ee.save(NODE + 0xC8, case['node'][1])
        ee.save(0x8106F1, case['progress'], 1)
        for i in range(4): ee.save(0x700038A0 + 4 * i, case['scratch'][i])
        ee.save(0x70003A20, case['scratch'][4])
        entry = case['entry']
        args = {'surface5d': (ACTOR, case['arg']), 'teleport': (ACTOR, case['frames'], case['hold']),
                'drop': (ACTOR, ACTOR + 0x2EC)}
        ee.call(ENTRY_POINT[entry], args.get(entry, (ACTOR,)))
        scratch = [ee.load(0x700038A0 + 4 * i) for i in range(4)] + [ee.load(0x70003A20)]
        return {'actor': ee.read(ACTOR, 0x320), 'scratch': scratch, 'log': self.log,
                'v0': s32(ee.r[2]) if entry == 'land_check' else None}


class NativeRun:
    """em_player_fall.c with Python workers that replay the same script."""

    def __init__(self, native, case, script, missing=None, narrow=False):
        self.native, self.case, self.script, self.log = native, case, script, []
        # narrow: 0021D250 / 0021D2E0 through em_player_fall_0021D250 /
        # _0021D2E0, the entries the reaction lane's bridge calls (the same
        # translation, checking only the workers each routine reaches).
        self.narrow = narrow
        self.live = LiveActor()
        C.memmove(self.live.bytes, case['actor'], 0x320)
        self.scratch = Scratch()
        for i in range(4): self.scratch.s38A0[i] = case['scratch'][i]
        self.scratch.s3A20 = case['scratch'][4]
        self.node = list(case['node'])
        self.keep = []
        fields = {}
        for field, kind in WORKER_FIELDS:
            fields[field] = FN[kind](self.worker(field)) if field != missing else FN[kind]()
            self.keep.append(fields[field])
        self.workers = Workers(None, C.pointer(self.scratch), **fields)
        if missing == 'scratch':
            self.workers.scratch = P(Scratch)()

    def apply(self, name, e):
        for offset, size, value in e['writes']:
            for i in range(size): self.live.bytes[offset + i] = (value >> (8 * i)) & 0xFF
        if e['s3A20'] is not None: self.scratch.s3A20 = e['s3A20']
        if e['node'] is not None: self.node = list(e['node'])

    def call(self, name, entry):
        self.log.append(entry)
        index, e = self.script.next(name)
        if self.script.fail_at == index:
            return None
        self.apply(name, e)
        return e

    def worker(self, field):
        live_vec = lambda ptr, n: tuple(ptr[i] for i in range(n))
        fbits = lambda value: F(value) if value == value else struct.unpack('<I', struct.pack('<f', value))[0]

        def simple(name, *extra):
            def fn(_, *args):
                e = self.call(name, (name,) + extra)
                return -1 if e is None else 0
            return fn

        def with_result(name, entry_of=lambda args: ()):
            def fn(_, *args):
                out = args[-1]
                e = self.call(name, (name,) + entry_of(args[:-1]))
                if e is None: return -1
                out[0] = s32(e['ret'])
                return 0
            return fn

        if field == 'request':
            return lambda _, a, clip, force, blend: (-1 if self.call('request', ('request', clip, force, fbits(blend))) is None else 0)
        if field == 'arbiter':
            return lambda _, a, clip, blend, frame: (-1 if self.call('arbiter', ('arbiter', clip, fbits(blend), fbits(frame))) is None else 0)
        if field == 'clip_frames':
            return with_result('frames', lambda args: (args[0], args[1]))
        if field == 'sound':
            return lambda _, a, id: (-1 if self.call('sound', ('sound', id)) is None else 0)
        if field == 'rumble':
            return lambda _, *v: (-1 if self.call('rumble', ('rumble',) + tuple(v)) is None else 0)
        if field == 'effect':
            return lambda _, id, point, at: (-1 if self.call('effect', ('effect', id, live_vec(point, 4), live_vec(at, 4))) is None else 0)
        if field == 'fade':
            return lambda _, x, y: (-1 if self.call('fade', ('fade', x, y)) is None else 0)
        if field == 'skeleton':
            return lambda _, a: (-1 if self.call('skeleton', ('skeleton',)) is None else 0)
        if field == 'hip':
            def hip(_, x, z):
                x[0], z[0] = self.node
                return 0
            return hip
        if field == 'heading':
            return with_result('heading', lambda args: (args[1],))
        if field in ('test_001755B0', 'test_0017D080', 'test_0017F320', 'test_0021C190', 'pose_clip'):
            name = {'test_001755B0': 't55B0', 'test_0017D080': 'tD080', 'test_0017F320': 'tF320',
                    'test_0021C190': 'tC190', 'pose_clip': 'pose_clip'}[field]
            return with_result(name)
        if field in ('wrap', 'approach'):
            def fret(_, *args):
                e = self.call(field, (field,) + tuple(args[:-1]))
                if e is None: return -1
                args[-1][0] = e['fret']
                return 0
            return fret
        if field == 'trs':
            def trs(_, out, position, rotation, scale):
                e = self.call('trs', ('trs', live_vec(position, 3), live_vec(rotation, 3), live_vec(scale, 3)))
                if e is None: return -1
                for i in range(16): out[i] = e['out'][i]
                return 0
            return trs
        if field == 'apply':
            def apply(_, matrix, v, out):
                e = self.call('apply', ('apply', live_vec(matrix, 16), live_vec(v, 4)))
                if e is None: return -1
                for i in range(4): out[i] = e['out'][i]
                return 0
            return apply
        if field == 'floor_query':
            def query(_, a, point, out):
                e = self.call('query', ('query', live_vec(point, 3)))
                if e is None: return -1
                out[0] = s32(e['ret'])
                return 0
            return query
        if field in ('land_sound', 'translate', 'reentry', 'probes'):
            name = field
            return lambda _, a, arg: (-1 if self.call(name, (name, arg)) is None else 0)
        if field == 'floor':
            return with_result('floor', lambda args: (args[1],))
        if field in ('fall_check', 'handoff', 'react_0021C120', 'react_0021C350', 'react_0021C270'):
            name = {'fall_check': 'fall_check', 'handoff': 'handoff', 'react_0021C120': 'r120',
                    'react_0021C350': 'r350', 'react_0021C270': 'r270'}[field]
            return lambda _, a: (-1 if self.call(name, (name,)) is None else 0)
        if field == 'ledge':
            return with_result('ledge', lambda args: (args[1],))
        if field == 'convert_00128350':
            def convert(_, value, out):
                e = self.call('convert', ('convert', value))
                if e is None: return -1
                out[0] = e['ret'] & MASK64
                return 0
            return convert
        if field == 'test_001000E0':
            return with_result('t00E0', lambda args: (args[0], args[1]))
        if field == 'progress_8106F1':
            def progress(_, out):
                out[0] = self.case['progress']
                return 0
            return progress
        raise AssertionError(field)

    def run(self):
        n, entry = self.native, self.case['entry']
        W, A = C.byref(self.workers), C.byref(self.live)
        v0 = None
        if entry == 'state5': result = n.em_player_fall_state5(W, A)
        elif entry == 'state7': result = n.em_player_fall_state7(W, A)
        elif entry == 'state8': result = n.em_player_fall_state8(W, A)
        elif entry == 'land': result = n.em_player_fall_land(W, A)
        elif entry == 'surface5d' and self.narrow:
            result = n.em_player_fall_0021D250(W, A, self.case['arg'])
        elif entry == 'surface5d': result = n.em_player_fall_surface5d(W, A, self.case['arg'])
        elif entry == 'teleport' and self.narrow:
            result = n.em_player_fall_0021D2E0(W, A, self.case['frames'], self.case['hold'])
        elif entry == 'teleport':
            result = n.em_player_fall_teleport(W, A, self.case['frames'], self.case['hold'])
        elif entry == 'land_check':
            out = C.c_int(-99)
            result = n.em_player_fall_land_check(W, A, C.byref(out))
            v0 = out.value
        elif entry == 'drop':
            n.em_player_fall_drop(A); result = 0
        scratch = list(self.scratch.s38A0) + [self.scratch.s3A20]
        return result, {'actor': bytes(self.live.bytes), 'scratch': scratch, 'log': self.log, 'v0': v0}


ELF = NATIVE = ORACLE = None


def run_case(seed):
    global ORACLE
    if ORACLE is None:
        ORACLE = UnitOracle(ELF)
    case = make_case(seed)
    want = ORACLE.run(case, Script(seed))
    result, got = NativeRun(NATIVE, case, Script(seed)).run()
    assert result == 0, (seed, case['entry'], 'native fault', result)
    where = (seed, case['entry'], case['actor'][6], case['actor'][7])
    assert want['log'] == got['log'], (where, 'worker calls', want['log'], got['log'])
    if want['actor'] != got['actor']:
        diff = [hex(k) for k in range(0x320) if want['actor'][k] != got['actor'][k]]
        raise AssertionError((where, 'actor bytes differ at', diff[:24]))
    assert want['scratch'] == got['scratch'], (where, 'scratch', want['scratch'], got['scratch'])
    assert want['v0'] == got['v0'], (where, 'return', want['v0'], got['v0'])
    if case['entry'] in ('surface5d', 'teleport'):
        result, other = NativeRun(NATIVE, case, Script(seed), narrow=True).run()
        assert result == 0 and other == got, (where, 'the narrow entry differs')
    # fail-stop: the first worker call faulting stops the routine there
    faults = 0
    if want['log'] and seed % 5 == 0:
        k = random.Random(seed).randrange(len(want['log']))
        result, cut = NativeRun(NATIVE, case, Script(seed, fail_at=k)).run()
        assert result == -1, (where, 'fault not reported', k)
        assert cut['log'] == want['log'][:k + 1], (where, 'calls after a fault', k)
        faults = 1
    return (case['entry'], len(want['log']), faults, tuple(sorted(ORACLE.ee.outcomes)))


def missing_worker_checks(native):
    """Each worker (and the scratch) missing: every entry point returns -1
    before any write and calls nothing."""
    count = 0
    fields = [f for f, _ in WORKER_FIELDS] + ['scratch']
    base = make_case(1000)
    for field in fields:
        for entry in ('state5', 'state7', 'state8', 'land', 'land_check', 'surface5d', 'teleport'):
            case = dict(base, entry=entry)
            result, got = NativeRun(native, case, Script(1000), missing=field).run()
            assert result == -1 and got['log'] == [] and got['actor'] == case['actor'], (field, entry)
            count += 1
    # The narrow entries refuse only on what their own instructions reach.
    reached = {'surface5d': ('request', 'rumble', 'sound'),
               'teleport': ('scratch', 'request', 'skeleton', 'hip', 'effect', 'fade', 'floor')}
    for entry, needed in reached.items():
        for field in needed:
            case = dict(base, entry=entry)
            result, got = NativeRun(native, case, Script(1000), missing=field, narrow=True).run()
            assert result == -1 and got['log'] == [] and got['actor'] == case['actor'], (field, entry)
            count += 1
    return count


# ======================================================================
# The record-level 00174AC0 bound as the heading worker
# ======================================================================

HEADING, HEADING_SIZE = 0x174AC0, 0x508
WRAP, APPROACH = 0x1B1470, 0x1B12B0


class BoundHeadingOracle(UnitOracle):
    """UnitOracle with 00174AC0 executed as original code (its whole call
    tree: cosf, atan2f, fabsf, 001B1470, 001B12B0) instead of scripted. The
    001B1470 / 001B12B0 hooks stay scripted for the fall routines' own calls
    and run the original when 00174AC0's tree calls them."""

    def __init__(self, elf):
        super().__init__(elf)
        self.inside = 0
        self.headings = 0
        self.ee.hooks[HEADING] = self.through(HEADING, None)
        for address in (WRAP, APPROACH):
            self.ee.hooks[address] = self.through(address, self.ee.hooks[address])

    def through(self, address, scripted):
        def run(ee):
            if address != HEADING and not self.inside:
                return scripted(ee)
            if address == HEADING:
                self.headings += 1
            back, hook = ee.r[31], ee.hooks.pop(address)
            self.inside += 1
            ee.r[31] = shared.RETURN
            try:
                ee.run(address)
            finally:
                ee.hooks[address] = hook
                self.inside -= 1
            ee.r[31] = back
        return run


def bound_heading_checks(elf):
    """0017C580, 00162DB0 and 00163B40 with the heading slot bound to
    em_player_heading_record_worker_result (docs/PLAYER_HEADING_RECORD.md
    section 4), its 0x70003A20 pointed at this lane's scratch word (the one
    0017C580 reloads after the call), against the original with 00174AC0
    running as original code. Returns (cases, heading calls)."""
    import test_player_heading_record_reference as HR
    hlib = HR.build_native()
    hr = HR.Native(hlib, elf)
    oracle = BoundHeadingOracle(elf)
    wanted = reference_mode.pick(6000, 600)
    cases = headings = land_headings = 0
    seed = 0xB0D0
    while cases < wanted:
        seed += 1
        case = make_case(seed)
        if case['entry'] not in ('land', 'state5', 'state8'):
            continue
        rng = random.Random('heading/%d' % seed)
        world = {'s3B8D': rng.choice((0,) * 9 + (1,)), 'gait': rng.randrange(4),
                 'x': rng.randrange(256), 'y': rng.randrange(256),
                 'camera': F(rng.uniform(-3.14159, 3.14159))}
        ee = oracle.ee
        ee.save(0x70003B8D, world['s3B8D'], 1)
        ee.save(0x810E57, world['gait'], 1)
        ee.save(0x810E64, world['x'], 1)
        ee.save(0x810E65, world['y'], 1)
        ee.save(0x8106A0, world['camera'])
        before = oracle.headings
        want = oracle.run(case, Script(seed))
        run = NativeRun(NATIVE, case, Script(seed))
        hr.bind()
        for key in ('s3B8D', 'gait', 'x', 'y'):
            hr.cells[key].value = world[key]
        hr.camera.value = world['camera']
        word = C.c_uint32.from_buffer(run.scratch, Scratch.s3A20.offset)
        hr.h.world.spad3A20 = C.pointer(word)
        calls = [0]

        def heading(_, a, arg, out):
            calls[0] += 1
            return hlib.em_player_heading_record_worker_result(
                C.byref(hr.h), C.cast(a, C.POINTER(HR.LiveActor)), arg, out)
        bound = FN['heading'](heading)
        run.keep.append(bound)
        run.workers.heading = bound
        result, got = run.run()
        where = ('bound heading', seed, case['entry'], case['actor'][6])
        assert result == 0, (where, 'native fault', result, hex(hr.h.fault_address))
        assert calls[0] == oracle.headings - before, (where, 'heading calls', calls[0])
        assert want['log'] == got['log'], (where, 'worker calls', want['log'], got['log'])
        if want['actor'] != got['actor']:
            diff = [hex(k) for k in range(0x320) if want['actor'][k] != got['actor'][k]]
            raise AssertionError((where, 'actor bytes differ at', diff[:24]))
        assert want['scratch'] == got['scratch'], (where, 'scratch', want['scratch'], got['scratch'])
        cases += 1
        headings += calls[0]
        if case['entry'] == 'land' and calls[0]:
            land_headings += 1
    # 0017C580 always reaches 00174AC0 past its early exits; the reload of
    # 0x70003A20 follows it on the drop paths.
    assert land_headings > 0 and headings >= cases // 10, ('bound heading rarely reached',
                                                          headings, land_headings, cases)
    return cases, headings


def branch_sites(elf):
    """Every conditional branch in the translated routines."""
    ee = EE(elf)
    sites = set()
    for start, size in SIZES.items():
        for pc in range(start, start + size, 4):
            word = ee.load(pc)
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            if op in (4, 20) and rs == 0 and rt == 0:
                continue                                  # b (beq zero, zero): unconditional
            if ee.branch(word, pc) is not None:
                sites.add(pc)
    return sites


def main():
    global ELF, NATIVE
    import time
    started = time.time()
    ELF = read_elf()
    callees = check_callee_set(ELF)
    NATIVE = build_native()
    total = reference_mode.pick(24000, 24000)
    seeds = reference_mode.select(range(total), 4000, 0x5EED)
    results = reference_mode.parallel_map(run_case, seeds)
    outcomes = set()
    entries, faults, calls = {}, 0, 0
    for entry, count, fault, cover in results:
        outcomes.update(cover)
        entries[entry] = entries.get(entry, 0) + 1
        faults += fault
        calls += count
    sites = branch_sites(ELF)
    missing = sorted('%06X %s' % (pc, 'taken' if taken else 'not taken')
                     for pc in sites for taken in (True, False) if (pc, taken) not in outcomes)
    assert not missing, ('branch outcomes never exercised', missing)
    absent = [e for e in ENTRY_POINT if e not in entries]
    assert not absent, ('entry points never run', absent)
    stops = missing_worker_checks(NATIVE)
    bound_cases, bound_calls = bound_heading_checks(ELF)
    reference_mode.banner(reference_mode.part(len(seeds), total, 'cases'),
                          '%d jal targets (all hooked or translated)' % callees)
    print('player fall/landing vs original instructions: PASS %d cases (%s), %d worker calls '
          'identical, every one of %d conditional branches both ways, %d fault-stop cuts, '
          '%d missing-worker refusals; %d land/fall/landing cases with the record-level '
          '00174AC0 bound as the heading worker (%d calls, 00174AC0 run as original code, '
          '0x70003A20 shared) identical (%.1fs)' % (
              len(seeds), ', '.join('%s %d' % kv for kv in sorted(entries.items())), calls,
              len(sites), faults, stops, bound_cases, bound_calls, time.time() - started))


# ======================================================================
# World mode: the route beats that fall and land, over the captured RAM
# ======================================================================

PLAYER = shared.PLAYER
VEC = 0x7F0E0000                 # private vectors for worker arguments (stack region)
# beat: (last trace frame replayed, last frame compared with the trace rows).
# 14: Roger's script starts at f283 and the replay runs only the player stage,
# so its rows are compared up to f282 (docs/PLAYER_FALL.md).
BEATS = {'10_cage_roof_roger': (170, 170), '11_crevice_prompt': (560, 560),
         '12_crevice_jump': (336, 336), '14_roger_encounter': (300, 282)}
HOOKED = {STATE5: 'state5', STATE7: 'state7', STATE8: 'state8', LAND: 'land',
          LAND_CHECK: 'land_check', SURFACE5D: 'surface5d', TELEPORT: 'teleport'}


class FallRoute(shared.RouteReplay):
    """RouteReplay on FallEE (the measured float model)."""

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


class WorldLand:
    """EmPlayerLandWorkers bound to the ORIGINAL callees, executed in the same
    EE on the same world (the actor bytes and the scratch words are synced
    around every call)."""

    def __init__(self, ee, base, live, scratch):
        self.ee, self.base, self.live, self.scratch = ee, base, live, scratch
        self.error = None
        self.keep = []
        fields = {}
        for field, kind in WORKER_FIELDS:
            fields[field] = FN[kind](self.guard(getattr(self, 'w_' + field)))
            self.keep.append(fields[field])
        self.workers = Workers(None, C.pointer(scratch), **fields)

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

    def sync_in(self):
        ee = self.ee
        ee.write(self.base, bytes(self.live.bytes))
        for i in range(4): ee.save(0x700038A0 + 4 * i, self.scratch.s38A0[i])
        ee.save(0x70003A20, self.scratch.s3A20)

    def sync_out(self):
        ee = self.ee
        C.memmove(self.live.bytes, ee.read(self.base, 0x320), 0x320)
        for i in range(4): self.scratch.s38A0[i] = ee.load(0x700038A0 + 4 * i)
        self.scratch.s3A20 = ee.load(0x70003A20)

    def call(self, entry, args=(), fregs=(), s1=None):
        ee = self.ee
        self.sync_in()
        saved = ee.r[17]
        if s1 is not None: ee.r[17] = s1
        v0, f0 = nested_bits(ee, entry, args, fregs)
        ee.r[17] = saved
        self.sync_out()
        return s32(v0), f0 & MASK

    # ---- the workers (argument order as in EmPlayerLandWorkers) ----------
    def w_request(self, a, clip, force, blend): self.call(0x1749A0, (self.base, clip, force), (F(blend),)); return 0
    def w_arbiter(self, a, clip, blend, frame): self.call(0x1749F0, (self.base, clip), (F(blend), F(frame))); return 0

    def w_clip_frames(self, bank, clip, out):
        out[0] = self.call(0x1C61D0, (bank, clip))[0]; return 0

    def w_sound(self, a, id): self.call(0x1FBD50, (self.base, id, 0), (F(300.0),)); return 0
    def w_rumble(self, *v): self.call(0x1B61C0, v); return 0

    def w_effect(self, id, point, at):
        assert [point[i] for i in range(4)] == list(self.scratch.s38A0), 'effect point is not 0x700038A0'
        assert [at[i] for i in range(4)] == [int.from_bytes(bytes(self.live.bytes[0xB0 + 4 * i:0xB4 + 4 * i]), 'little') for i in range(4)]
        self.call(0x1EFD90, (id, 0x700038A0, self.base + 0xB0)); return 0

    def w_fade(self, x, y): self.call(0x1AEDE0, (x, y)); return 0
    def w_skeleton(self, a): self.call(0x1C6DA0, (self.base,)); return 0

    def w_hip(self, x, z):
        node = self.ee.load(self.ee.load(0x275B40) + 4)
        x[0], z[0] = self.ee.load(node + 0xC0), self.ee.load(node + 0xC8); return 0

    def w_heading(self, a, arg, out): out[0] = self.call(0x174AC0, (self.base, arg))[0]; return 0
    def w_test_001755B0(self, a, out): out[0] = self.call(0x1755B0, (self.base,))[0]; return 0
    def w_test_0017D080(self, a, out): out[0] = self.call(0x17D080, (self.base,))[0]; return 0
    def w_test_0017F320(self, a, out): out[0] = self.call(0x17F320, (self.base,))[0]; return 0
    def w_test_0021C190(self, a, out): out[0] = self.call(0x21C190, (self.base,))[0]; return 0
    def w_pose_clip(self, a, out): out[0] = self.call(0x188550, (self.base,))[0]; return 0
    def w_wrap(self, x, out): out[0] = self.call(0x1B1470, (), (x,))[1]; return 0
    def w_approach(self, t, c, r, out): out[0] = self.call(0x1B12B0, (), (t, c, r))[1]; return 0

    def w_trs(self, out, position, rotation, scale):
        self.call(0x1C94B0, (self.base + 0xD0, self.base + 0xB0, self.base + 0xC0, self.base + 0x60))
        for i in range(16): out[i] = int.from_bytes(bytes(self.live.bytes[0xD0 + 4 * i:0xD4 + 4 * i]), 'little')
        return 0

    def w_apply(self, matrix, v, out):
        for i in range(4): self.ee.save(VEC + 4 * i, v[i])
        self.call(0x1026A0, (0x700038A0, self.base + 0xD0, VEC))
        for i in range(4): out[i] = self.scratch.s38A0[i]
        return 0

    def w_floor_query(self, a, point, out):
        values = [point[i] for i in range(3)]
        if C.addressof(point.contents) == C.addressof(self.scratch.s38A0):
            address = 0x700038A0
        elif values == [int.from_bytes(bytes(self.live.bytes[0xB0 + 4 * i:0xB4 + 4 * i]), 'little') for i in range(3)]:
            address = self.base + 0xB0
        else:
            raise AssertionError('00179450 point is neither +B0 nor 0x700038A0')
        out[0] = self.call(0x179450, (self.base, address))[0]; return 0

    def w_land_sound(self, a, tier): self.call(0x182870, (self.base, tier)); return 0
    def w_translate(self, a, arg): self.call(0x178B90, (self.base, arg)); return 0

    def w_probes(self, a, source):
        s1 = {0: 0, 1: self.base, 2: None}[source]
        self.call(0x1764E0, (self.base,), s1=s1); return 0

    def w_floor(self, a, search, out): out[0] = self.call(0x175900, (self.base, search))[0]; return 0
    def w_fall_check(self, a): self.call(0x1796C0, (self.base,)); return 0
    def w_ledge(self, a, drop, out): out[0] = self.call(0x17C860, (self.base,), (drop,))[0]; return 0
    def w_reentry(self, a, arg): self.call(0x17C440, (self.base, arg)); return 0
    def w_handoff(self, a): self.call(0x17C540, (self.base,)); return 0
    def w_react_0021C120(self, a): self.call(0x21C120, (self.base,)); return 0
    def w_react_0021C350(self, a): self.call(0x21C350, (self.base,)); return 0
    def w_react_0021C270(self, a): self.call(0x21C270, (self.base,)); return 0
    def call_wide(self, entry, args=(), fregs=()):
        """The same call with whole 64-bit integer arguments and $v0 (the
        double 00128350 returns and 001000E0 takes)."""
        ee = self.ee
        self.sync_in()
        v0, _ = nested_bits(ee, entry, args, fregs, wide=True)
        self.sync_out()
        return v0 & MASK64

    def w_convert_00128350(self, value, out): out[0] = self.call_wide(0x128350, (), (value,)); return 0
    def w_test_001000E0(self, x, y, out): out[0] = s32(self.call_wide(0x1000E0, (x, y))); return 0
    def w_progress_8106F1(self, out): out[0] = self.ee.load(0x8106F1, 1); return 0


def native_hook(native, kind, counts):
    def hook(ee):
        base = ee.arg(0)
        live, scratch = LiveActor(), Scratch()
        C.memmove(live.bytes, ee.read(base, 0x320), 0x320)
        for i in range(4): scratch.s38A0[i] = ee.load(0x700038A0 + 4 * i)
        scratch.s3A20 = ee.load(0x70003A20)
        world = WorldLand(ee, base, live, scratch)
        W, A = C.byref(world.workers), C.byref(live)
        out = C.c_int(-99)
        if kind == 'state5': result = native.em_player_fall_state5(W, A)
        elif kind == 'state7': result = native.em_player_fall_state7(W, A)
        elif kind == 'state8': result = native.em_player_fall_state8(W, A)
        elif kind == 'land': result = native.em_player_fall_land(W, A)
        elif kind == 'land_check': result = native.em_player_fall_land_check(W, A, C.byref(out))
        elif kind == 'surface5d': result = native.em_player_fall_surface5d(W, A, s32(ee.arg(1)))
        else: result = native.em_player_fall_teleport(W, A, s32(ee.arg(1)), s32(ee.arg(2)))
        if world.error is not None:
            raise world.error
        assert result == 0, (kind, 'native fault', result)
        ee.write(base, bytes(live.bytes))
        for i in range(4): ee.save(0x700038A0 + 4 * i, scratch.s38A0[i])
        ee.save(0x70003A20, scratch.s3A20)
        if kind == 'land_check': ee.ret_int(out.value)
        counts[kind] = counts.get(kind, 0) + 1
    return hook


WORLD = {}


def beat_replay(job):
    beat, native_mode = job
    trace, ram, spad = WORLD[beat]
    last, checked_until = BEATS[beat]
    replay = FallRoute(WORLD['elf'], trace, ram, spad)
    counts = {}
    if native_mode:
        for address, kind in HOOKED.items():
            replay.ee.hooks[address] = native_hook(WORLD['native'], kind, counts)
    frames, rows, states = [], 0, set()
    end = trace['first_counter'] + last
    while replay.counter < end:
        row = replay.step()
        ee = replay.ee
        digest = hashlib.sha1(ee.mem)
        digest.update(ee.spad)
        frames.append((replay.counter, digest.hexdigest(), ee.read(PLAYER, 0x320), len(replay.events)))
        states.add((ee.load(PLAYER + 5, 1), ee.load(PLAYER + 6, 1)))
        if row is not None and replay.counter - trace['first_counter'] <= checked_until:
            shared.route_row_check(ee, row, (beat, 'native' if native_mode else 'original', replay.counter))
            rows += 1
    return frames, replay.events, rows, counts, sorted(states)


def world_main():
    elf = read_elf()
    beats = [b for b in os.environ.get('EM_WORLD_BEATS', ','.join(BEATS)).split(',') if b]
    for beat in beats:
        loaded = shared.route_beat(beat)
        if isinstance(loaded, str):
            raise SystemExit('world mode: %s (docs/PLAYER_FALL.md)' % loaded)
        WORLD[beat] = loaded
    WORLD['elf'], WORLD['native'] = elf, build_native()
    jobs = [(beat, mode) for beat in beats for mode in (False, True)]
    results = reference_mode.parallel_map(beat_replay, jobs,
                                          cost=lambda job: BEATS[job[0]][0])
    by_job = dict(zip(jobs, results))
    for beat in beats:
        (a_frames, a_events, rows, _, a_states), (b_frames, b_events, b_rows, counts, _) = \
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
        landed = [s for s in a_states if s[0] in (5, 7, 8)]
        assert counts.get('state5', 0) + counts.get('state8', 0) > 0, (beat, 'no fall/landing callback ran', counts)
        print('%s: PASS %d frames identical (whole RAM + scratchpad + actor), %d trace rows within '
              'precision, %d sound/effect calls; native callbacks %s; states (+5, +6) seen %s' % (
                  beat, len(a_frames), rows, len(a_events),
                  ', '.join('%s %d' % kv for kv in sorted(counts.items())),
                  ' '.join('%X/%X' % s for s in landed)))


if __name__ == '__main__':
    if os.environ.get('EM_TEST_WORLD', '') not in ('', '0'):
        world_main()
    else:
        main()
