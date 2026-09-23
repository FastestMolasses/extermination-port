#!/usr/bin/env python3
"""Execute the original ledge-hang state and compare em_player_hang.c.

docs/PLAYER_HANG.md. The original instructions run from the captured AREA11
EE RAM (../Extermination/build/startup-reference/playable_ee.bin, state 04),
whose code and table bytes are first checked against the user's pinned ELF;
none are embedded here. The interpreter is the shared EE of
tools/test_player_slide_reference.py, subclassed (HangEE) so that every COP1
and VU0 macro operation goes through tools/ee_float_model.py (the measured EE
rules, docs/EE_FLOAT_MODEL.md; the shared EE.cop1 lacks the add/sub pre-trim
and truncates div.s, section 5a). The shared files are not edited.

Executed, unmodified:
  001647D0  state 9 (the hang), with 0017F240 run as original code inside it
  0017F240  hang reaction test, alone, for both argument values
  001028B8  VU0 vadd.xyzw, alone

Every other 001647D0 callee is hooked, scripted per case and recorded (never
simulated as a claim about the callee): the scripted return value and the
scripted actor/skeleton writes are applied identically on both sides, and
the call sequence with every argument (floats as bits, vectors as the words
the callee receives) must match. After each case all 0x320 actor bytes must
match. Branch coverage: every conditional branch of 001647D0 and 0017F240
must be seen both taken and not taken (asserted).

The player record sits at its captured address 0x8102B0. D_00275B40 there is
the player's own bone array at +0x40 (anim_bone_array_setup: D_00275B48 +
0x110), so the bone pointers +0x40/+0x44 keep their captured values and the
case's node words are written at the captured node 0 / node 1 records.

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
HANG, F240, VADD = 0x1647D0, 0x17F240, 0x1028B8
HANG_END, F240_END = HANG + 0x138C, F240 + 0xE0
TABLES = ((0x2485E0, 16), (0x2485F0, 16), (0x248600, 16), (0x275498, 8))
SCRATCH = 0x7F080000          # vectors for the standalone 001028B8 cases (private stack region)

# ------------------------------------------------------------------ EE ----


class HangEE(EE):
    """The shared EE with COP1 and the VU0 vadd routed through the measured
    model; any other COP1/VU0 operation is refused (fail-stop). Records every
    conditional branch outcome inside the two translated routines."""

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
        if word & 63 != 40:
            raise M.UnmeasuredCase(('VU0 op outside this oracle', hex(word)))
        dest, fs, ft, fd = word >> 21 & 15, word >> 11 & 31, word >> 16 & 31, word >> 6 & 31
        s, t = list(self.vf[fs]), list(self.vf[ft])
        for lane in range(4):
            if dest & (8 >> lane) and fd:
                self.vf[fd][lane] = M.vu_lane('vadd', dest, None, s[lane], t[lane])

    def branch(self, word, pc):
        outcome = super().branch(word, pc)
        if outcome is not None and (HANG <= pc < HANG_END or F240 <= pc < F240_END):
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


# ------------------------------------------------------- native side -----

class HangScene(C.Structure):
    _fields_ = [('area', C.c_uint8), ('scripted', C.c_uint8), ('pad', C.c_uint16),
                ('use_mask', C.c_uint16)]


A = C.POINTER(LiveActor)
FP, IP = C.POINTER(C.c_float), C.POINTER(C.c_int)
SCENE_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(HangScene))
NODE_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.c_uint, FP)
ACT_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, A)
ACT_R_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, A, IP)
ACT_I_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, A, C.c_int)
ACT_I_R_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, A, C.c_int, IP)
CALL_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, A, C.c_int, C.c_int, C.c_float)   # request / sound
COLUMN_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, A, FP, C.c_int, C.c_float, IP)
SIDE_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, A, C.c_int, C.c_float)
BLEND_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, A, C.c_float)
SWEEP_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, A, FP, FP, C.c_uint, IP)
VEC_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, FP, FP, FP)
F1_FN = C.CFUNCTYPE(C.c_float, C.c_void_p, C.c_float)
F3_FN = C.CFUNCTYPE(C.c_float, C.c_void_p, C.c_float, C.c_float, C.c_float)

WORKER_FIELDS = [
    ('context', C.c_void_p), ('scene', SCENE_FN), ('node', NODE_FN),
    ('aim_track', ACT_FN), ('hang_clear', ACT_R_FN), ('steer_input', ACT_FN),
    ('ledge_ahead', ACT_R_FN), ('ledge_above', ACT_R_FN), ('ledge_side', ACT_I_R_FN),
    ('request', CALL_FN), ('sound', CALL_FN), ('skeleton', ACT_FN), ('translate', ACT_I_FN),
    ('floor', ACT_I_R_FN), ('column', COLUMN_FN), ('land_sound', ACT_I_FN), ('heading', ACT_I_FN),
    ('reentry', ACT_I_FN), ('handoff', ACT_FN), ('fall', ACT_FN),
    ('clip_DF70', SIDE_FN), ('clip_DFB0', SIDE_FN), ('clip_E0D0', SIDE_FN), ('clip_E150', SIDE_FN),
    ('clip_E1D0', SIDE_FN), ('clip_FC80', BLEND_FN), ('clip_FF80', BLEND_FN),
    ('clip_row', ACT_R_FN), ('sound_100', ACT_FN), ('sound_109', ACT_FN),
    ('sweep', SWEEP_FN), ('ledge_top', ACT_I_R_FN), ('transform', VEC_FN), ('vadd', VEC_FN),
    ('sqrt', F1_FN), ('cosine', F1_FN), ('sine', F1_FN), ('wrap', F1_FN), ('approach', F3_FN),
]


class HangWorkers(C.Structure):
    _fields_ = WORKER_FIELDS


# Original callee address -> worker name (0017F240 is not hooked: it runs).
CALLEES = {
    0x182250: 'aim_track', 0x17F320: 'hang_clear', 0x174FD0: 'steer_input',
    0x17E250: 'ledge_ahead', 0x17E510: 'ledge_above', 0x17E7C0: 'ledge_side',
    0x1749A0: 'request', 0x1FBD50: 'sound', 0x1C68C0: 'skeleton', 0x178B90: 'translate',
    0x175900: 'floor', 0x1760C0: 'column', 0x182870: 'land_sound', 0x174AC0: 'heading',
    0x17C440: 'reentry', 0x17C540: 'handoff', 0x1796C0: 'fall',
    0x17DF70: 'clip_DF70', 0x17DFB0: 'clip_DFB0', 0x17E0D0: 'clip_E0D0', 0x17E150: 'clip_E150',
    0x17E1D0: 'clip_E1D0', 0x17FC80: 'clip_FC80', 0x17FF80: 'clip_FF80', 0x188550: 'clip_row',
    0x182AF0: 'sound_100', 0x182A70: 'sound_109', 0x19AFE0: 'sweep', 0x178910: 'ledge_top',
    0x1026A0: 'transform', 0x1028B8: 'vadd', 0x11E748: 'sqrt', 0x11DE90: 'cosine',
    0x11E2A8: 'sine', 0x1B1470: 'wrap', 0x1B12B0: 'approach',
}
FLOAT_WORKERS = {'sqrt', 'cosine', 'sine', 'wrap', 'approach'}
VECTOR_WORKERS = {'transform', 'vadd'}

# ------------------------------------------------------------ scripts ----


def finite(rng):
    """A finite binary32 word: signed zeros, denormals (zero under DAZ), unit
    values, or a uniform value in the ranges the hang works in."""
    if rng.random() < 0.15:
        return rng.choice([0, 0x80000000, 1, 0x80000001, 0x3F800000, 0xBF800000])
    return bits(rng.uniform(-400.0, 400.0))


def fword(value):
    return bits(value) if isinstance(value, float) else value


ACTOR_FLOATS = (0x38, 0x3C, 0xB0, 0xB4, 0xB8, 0xBC, 0xC4, 0x204, 0x218, 0x21C, 0x224, 0x22C,
                0x258, 0x26C, 0x2E0, 0x2E4, 0x2E8, 0x2EC, 0x2F4, 0x2F8)


def effect(seed, name, index):
    """The scripted result of the index-th call of `name` in case `seed`:
    (int or float-bits result, [(offset, size, value)], {(node, offset): bits},
    out-vector words)."""
    rng = random.Random(zlib.crc32(('%d/%s/%d' % (seed, name, index)).encode()))
    writes, nodes, out, result = [], {}, None, 0
    def maybe(p, offset, size, value):
        if rng.random() < p: writes.append((offset, size, value))
    if name == 'hang_clear':
        result = rng.choice([0, 0, 0, 1, 2])
    elif name == 'steer_input':
        writes.append((0x23F, 1, rng.randrange(4)))
        writes.append((0x24C, 4, rng.choice([0, 0, 1, 2, 3, 2, 3, 0xFFFFFFFF])))
        maybe(0.3, 0x244, 4, finite(rng)); maybe(0.3, 0x248, 4, finite(rng))
    elif name in ('ledge_ahead', 'ledge_above'):
        result = rng.choice([0, 0, 1])
    elif name == 'ledge_side':
        result = rng.choice([1, 1, 1, 2, 0xA, 0, 3, -1 & 0xFFFFFFFF])
        maybe(0.3, 0x2F1, 1, rng.randrange(2)); maybe(0.3, 0x23F, 1, rng.randrange(4))
    elif name in ('request', 'clip_DF70', 'clip_DFB0', 'clip_E0D0', 'clip_E150', 'clip_E1D0',
                  'clip_FC80', 'clip_FF80'):
        maybe(0.3, 0x200, 4, rng.choice([0, 0x1000, 0x8000, 0x9000]))
    elif name == 'sound':
        result = rng.choice([0, 3, -1 & 0xFFFFFFFF])
    elif name == 'aim_track':
        maybe(0.3, 0xB0, 4, finite(rng)); maybe(0.3, 0xB8, 4, finite(rng)); maybe(0.3, 0xC4, 4, finite(rng))
    elif name == 'skeleton':
        for key in ((1, 0xC4), (1, 0x8), (0, 0x4), (0, 0x8)):
            if rng.random() < 0.8: nodes[key] = finite(rng)
        maybe(0.3, 0xB4, 4, finite(rng))
    elif name == 'translate':
        maybe(0.4, 0xB4, 4, finite(rng)); maybe(0.3, 0xB0, 4, finite(rng)); maybe(0.3, 0x38, 4, finite(rng))
        maybe(0.3, 0x200, 4, rng.choice([0, 0x1000, 0x8000]))
        if rng.random() < 0.4: nodes[(0, 0x4)] = finite(rng)
    elif name == 'floor':
        result = rng.choice([0, 0, 1, 0x81])
        maybe(0.4, 0xB4, 4, finite(rng)); maybe(0.4, 0x23B, 1, rng.choice([0x39, 0x39, 5, 0]))
        maybe(0.3, 0x23F, 1, rng.randrange(4)); maybe(0.3, 0xD0, 4, finite(rng))
    elif name == 'column':
        result = rng.choice([0, 0, 1, 2])
    elif name == 'heading':
        maybe(0.5, 0x23F, 1, rng.randrange(4)); maybe(0.3, 6, 1, rng.randrange(0x40))
    elif name in ('land_sound', 'reentry', 'handoff', 'fall'):
        maybe(0.3, 6, 1, rng.randrange(0x40)); maybe(0.3, 5, 1, rng.randrange(0x26))
    elif name == 'clip_row':
        result = rng.choice([0x7B, 0x8E, 0x97, 0xB3, -1 & 0xFFFFFFFF])
    elif name == 'sweep':
        result = rng.choice([0, 2, 4, 6, 1, 8, 7])
    elif name == 'ledge_top':
        result = rng.choice([0, 1, 1])
        maybe(0.3, 0x2F1, 1, rng.randrange(2))
    elif name in VECTOR_WORKERS:
        out = [finite(rng) for _ in range(4)]
    elif name == 'sqrt':
        result = fword(rng.choice([115.0, 115.00001, 114.99999, 0.0, rng.uniform(0.0, 400.0)]))
    elif name == 'wrap':
        result = fword(rng.choice([0.0, -0.0, 1e-30, -1e-30, 9e-38, -9e-38, rng.uniform(-3.2, 3.2),
                                   rng.uniform(-3.2, 3.2)]))
    elif name in FLOAT_WORKERS:
        result = fword(rng.uniform(-1.0, 1.0) if name != 'approach' else rng.uniform(-3.2, 3.2))
    if name in FLOAT_WORKERS:
        result = bits(number(result))       # a representable binary32
    return result, writes, nodes, out


# ------------------------------------------------------------- oracle ----

RAM = ELF = NATIVE = None
_EE = None


def oracle_ee():
    global _EE
    if _EE is None:
        _EE = HangEE(ELF, RAM)
    return _EE


def node_addresses(ee):
    array = ee.load(0x275B40)                   # D_00275B40 = the player's +0x40
    return {0: ee.load(array), 1: ee.load(array + 4)}


def words(ee, address, count):
    return tuple(ee.load(address + 4 * i) for i in range(count))


def install_hooks(ee, seed):
    counters = {}
    nodes = node_addresses(ee)
    ee.snapshots = []

    def hook(name):
        def run(o):
            index = counters.get(name, 0); counters[name] = index + 1
            o.snapshots.append(bytes(o.read(PLAYER, 0x320)))
            a0 = o.r[4] & 0xFFFFFFFF
            if name not in FLOAT_WORKERS | VECTOR_WORKERS:
                assert a0 == PLAYER, (name, hex(a0))
            if name == 'ledge_ahead':
                assert o.r[5] & 0xFFFFFFFF == PLAYER + 0xB0, 'ledge_ahead a1'
                entry = (name,)
            elif name in ('ledge_side', 'translate', 'floor', 'land_sound', 'heading', 'reentry', 'ledge_top'):
                entry = (name, s32(o.r[5]))
            elif name in ('request', 'sound'):
                entry = (name, s32(o.r[5]), s32(o.r[6]), o.f[12])
            elif name == 'column':
                assert o.r[5] & 0xFFFFFFFF == PLAYER + 0xB0, 'column at'
                entry = (name, words(o, PLAYER + 0xB0, 4), s32(o.r[6]), o.f[12])
            elif name.startswith('clip_') and name not in ('clip_FC80', 'clip_FF80', 'clip_row'):
                entry = (name, s32(o.r[5]), o.f[12])
            elif name in ('clip_FC80', 'clip_FF80'):
                entry = (name, o.f[12])
            elif name == 'sweep':
                entry = (name, words(o, o.r[5], 4), words(o, o.r[6], 4), o.r[7] & 0xFFFFFFFF)
            elif name == 'transform':
                assert o.r[5] & 0xFFFFFFFF == PLAYER + 0xD0, 'transform matrix'
                entry = (name, words(o, o.r[5], 16), words(o, o.r[6], 4))
            elif name == 'vadd':
                assert o.r[4] & 0xFFFFFFFF == PLAYER + 0xB0 == o.r[5] & 0xFFFFFFFF, 'vadd out/a'
                entry = (name, words(o, o.r[5], 4), words(o, o.r[6], 4))
            elif name == 'approach':
                entry = (name, o.f[12], o.f[13], o.f[14])
            elif name in FLOAT_WORKERS:
                entry = (name, o.f[12])
            else:
                entry = (name,)
            o.log.append(entry)
            result, writes, node_writes, out = effect(seed, name, index)
            for offset, size, value in writes:
                o.save(PLAYER + offset, value, size)
            for (node, offset), value in node_writes.items():
                o.save(nodes[node] + offset, value)
            if out is not None:
                for i, value in enumerate(out): o.save((o.r[4] & 0xFFFFFFFF) + 4 * i, value)
            if name in FLOAT_WORKERS:
                o.f[0] = result
            else:
                o.r[2] = sx32(result)
        return run
    ee.hooks = {address: hook(name) for address, name in CALLEES.items()}


class Native:
    """The same callees on the native side, one log, the same scripts."""

    def __init__(self, seed, scene, node_values, fault_at=None):
        self.seed, self.log, self.counters = seed, [], {}
        self.node_values = dict(node_values)
        self.fault_at = fault_at            # (name, index): that call returns -1
        self.keep = []
        self.live = None           # the live actor under test (set by the caller)
        self.snapshots = []        # its 0x320 bytes at every worker call, before the call's effects
        w = self.workers = HangWorkers()
        def scene_fn(_, out):
            out[0] = scene; return 0
        def node_fn(_, node, offset, value):
            value[0] = number(self.node_values[(node, offset)]); return 0
        w.scene = self._keep(SCENE_FN(scene_fn))
        w.node = self._keep(NODE_FN(node_fn))
        for field, kind in WORKER_FIELDS[3:]:
            setattr(w, field, self._keep(kind(self._make(field))))

    def _keep(self, fn):
        self.keep.append(fn); return fn

    def _apply(self, name, actor):
        index = self.counters.get(name, 0); self.counters[name] = index + 1
        if self.fault_at == (name, index):
            return None
        result, writes, node_writes, out = effect(self.seed, name, index)
        if actor is not None:
            raw = actor.contents.bytes
            for offset, size, value in writes:
                for i in range(size): raw[offset + i] = (value >> (8 * i)) & 0xFF
        self.node_values.update(node_writes)
        return result, out

    def _make(self, name):
        def fbits(value): return bits(value)
        def vec(pointer, count): return tuple(bits(pointer[i]) for i in range(count))

        def done(entry, actor, out_int=None, out_vec=None):
            self.log.append(entry)
            self.snapshots.append(bytes(self.live.bytes) if self.live is not None else None)
            applied = self._apply(name, actor)
            if applied is None: return -1
            result, out = applied
            if out_int is not None: out_int[0] = s32(result)
            if out_vec is not None:
                for i in range(4): out_vec[i] = number(out[i])
            return 0

        if name in ('hang_clear', 'ledge_ahead', 'ledge_above', 'clip_row'):
            return lambda _, a, r: done((name,), a, r)
        if name in ('ledge_side', 'floor', 'ledge_top'):
            return lambda _, a, arg, r: done((name, arg), a, r)
        if name in ('translate', 'land_sound', 'heading', 'reentry'):
            return lambda _, a, arg: done((name, arg), a)
        if name in ('request', 'sound'):
            return lambda _, a, x, y, f: done((name, x, y, fbits(f)), a)
        if name == 'column':
            return lambda _, a, at, arg, h, r: done((name, vec(at, 4), arg, fbits(h)), a, r)
        if name in ('clip_FC80', 'clip_FF80'):
            return lambda _, a, f: done((name, fbits(f)), a)
        if name.startswith('clip_'):
            return lambda _, a, side, f: done((name, side, fbits(f)), a)
        if name == 'sweep':
            return lambda _, a, p, q, mask, r: done((name, vec(p, 4), vec(q, 4), mask), a, r)
        if name == 'transform':
            return lambda _, out, m, v: done((name, vec(m, 16), vec(v, 4)), None, None, out)
        if name == 'vadd':
            return lambda _, out, p, q: done((name, vec(p, 4), vec(q, 4)), None, None, out)
        if name in FLOAT_WORKERS:
            def scalar(_, *args):
                self.log.append((name,) + tuple(fbits(x) for x in args))
                self.snapshots.append(bytes(self.live.bytes) if self.live is not None else None)
                result, _out = self._apply(name, None)
                return number(result)
            return scalar
        return lambda _, a: done((name,), a)


def build_native():
    out = ROOT / 'build/player_hang_reference'
    out.mkdir(parents=True, exist_ok=True)
    lib = out / ('hang.dylib' if sys.platform == 'darwin' else 'hang.so')
    cached_build(lib, ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                       '-shared', '-fPIC', '-Isrc', 'src/game/em_player_hang.c', '-o', str(lib)])
    native = C.CDLL(str(lib))
    native.em_player_hang_state.argtypes = [C.c_void_p, A]
    native.em_player_hang_0017F240.argtypes = [A, C.c_int]
    native.em_player_hang_vadd.argtypes = [C.c_void_p, FP, FP, FP]
    return native


# --------------------------------------------------------------- cases ----

STATES = (0, 1, 2, 3, 4, 5, 0xA, 0xB, 0x14, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x27, 0x28, 0x29,
          0x2A, 0x31)
CAPTURED = None   # the captured player record (state 04)


def random_case(seed):
    rng = random.Random(seed)
    raw = bytearray(CAPTURED) if rng.random() < 0.3 else bytearray(rng.randrange(256) for _ in range(0x320))
    raw[0x40:0x48] = CAPTURED[0x40:0x48]            # the bone pointers D_00275B40 reads through
    for offset in ACTOR_FLOATS:
        struct.pack_into('<I', raw, offset, finite(rng))
    for i in range(16):
        struct.pack_into('<I', raw, 0xD0 + 4 * i, finite(rng))
    st = rng.choice(STATES + (1, 3, 0x14, 0x21, 0x2A, 6, 0x30, rng.randrange(256)))
    raw[4], raw[5], raw[6] = rng.choice([1, 1, 2]), rng.choice([9, 9, 7]), st
    raw[7] = rng.choice([0, 1, 2, 2, 3])
    raw[0xD] = rng.choice([0, 0, 1, 2])
    raw[0xF] = rng.choice([0, 0, 0, 2, 0xFF, 0xFD])
    for offset in (0x224, 0x22C):
        struct.pack_into('<I', raw, offset, rng.choice([0, 0, 0, 0, 0x80000000, 1, 0x3F800000]))
    struct.pack_into('<I', raw, 0x200, rng.choice([0, 0x1000, 0x8000, 0x9000, rng.randrange(1 << 32)]))
    struct.pack_into('<f', raw, 0x3C, rng.choice([25.0, 25.000002, 24.999998, 6.0, 6.0000005, 5.9999995,
                                                   3.0, 2.9999998, rng.uniform(-50, 120), 0.0,
                                                   rng.uniform(25, 120), rng.uniform(25, 120)]))
    struct.pack_into('<I', raw, 0x24C, rng.choice([0, 1, 2, 3, 0xFFFFFFFF]))
    raw[0x23F] = rng.randrange(4)
    raw[0x2F1] = rng.randrange(2)
    raw[0x315] = rng.choice([0, 0, 1])
    raw[0x1F1] = rng.choice([1, 6, 2, 2, 5, 0, 3])
    struct.pack_into('<H', raw, 0x28, rng.choice([0, 0, 1, 8, 0xFFFF, 0x8000]))
    raw[0x23B] = rng.choice([0x39, 5, 0])
    raw[0x235] = rng.randrange(256)
    if rng.random() < 0.5:                          # near the area 0x11 radius centre (340, 270)
        struct.pack_into('<f', raw, 0xB0, rng.uniform(200, 480))
        struct.pack_into('<f', raw, 0xB8, rng.uniform(130, 410))
    if rng.random() < 0.3:                          # +26C at the D_00248600 boundary
        struct.pack_into('<I', raw, 0x26C, rng.choice([0, 0x80000000, 1, 0x80000001, 0x3F000000, 0x3F4CCCCD, 0x3F99999A,
                                                        0x3F4CCCCC]))
    if seed % 40 == 0:                              # sub-state 3's hold: +26C against D_00248600
        raw[6] = 3; struct.pack_into('<I', raw, 0x200, 0); struct.pack_into('<f', raw, 0x3C, 99.0)
        struct.pack_into('<I', raw, 0x26C, rng.choice([0, 0x80000000, 1, 0x80000001, 0x3F000000, 0x3F4CCCCD,
                                                       0x3F99999A]))
    scene = HangScene(rng.choice([0x11, 0x11, 0xB]), rng.choice([0, 0, 0, 3]),
                      rng.choice([0, 0x40, 0x4000, 0xFFFF]), rng.choice([0x40, 0x40, 0x10]))
    node_values = {key: finite(rng) for key in ((0, 0x4), (0, 0x8), (1, 0x8), (1, 0xC4))}
    fault_at = None
    if rng.random() < 0.08:
        fault_at = (rng.choice([n for n in CALLEES.values() if n not in FLOAT_WORKERS]), 0)
    return raw, scene, node_values, fault_at


def run_oracle(raw, scene, node_values, seed):
    ee = oracle_ee()
    ee.reset(); ee.branches = set()
    ee.write(PLAYER, bytes(raw))
    nodes = node_addresses(ee)
    for (node, offset), value in node_values.items():
        ee.save(nodes[node] + offset, value)
    ee.save(0x810700, scene.area, 1); ee.save(0x70003B8D, scene.scripted, 1)
    ee.save(0x810E74, scene.pad, 2); ee.save(0x70003B76, scene.use_mask, 2)
    install_hooks(ee, seed)
    ee.call(HANG, (PLAYER,))
    return ee


def run_case(seed):
    raw, scene, node_values, fault_at = random_case(seed)
    ee = run_oracle(raw, scene, node_values, seed)
    want = bytes(ee.read(PLAYER, 0x320))
    side = Native(seed, scene, node_values, fault_at)
    live = LiveActor(); C.memmove(live.bytes, bytes(raw), 0x320)
    side.live = live
    result = NATIVE.em_player_hang_state(C.addressof(side.workers), C.byref(live))
    got = bytes(live.bytes)
    tags = set()
    if fault_at is not None and fault_at[0] in [e[0] for e in ee.log]:
        # The faulting call: the log up to and including it, and the actor as
        # the original had it when it made that call.
        k = [e[0] for e in ee.log].index(fault_at[0])
        assert result == -1, (seed, 'fault not propagated', fault_at)
        assert side.log == ee.log[:k + 1], (seed, 'fault log', side.log, ee.log[:k + 1])
        assert side.snapshots == ee.snapshots[:k + 1], (seed, 'fault: actor bytes at an earlier call')
        assert got == ee.snapshots[k], (seed, 'fault bytes',
                                        [hex(i) for i in range(0x320) if got[i] != ee.snapshots[k][i]][:16])
        tags.add('fault')
        return seed, raw[6], tags, frozenset(ee.branches)
    assert result == 0, (seed, 'native result', result)
    assert side.log == ee.log, (seed, 'callee sequence', raw[6], side.log, ee.log)
    # Every store the original makes before a call is in place at that call,
    # and none it makes after it is: all 0x320 bytes at every callee entry.
    for k, (mine, theirs) in enumerate(zip(side.snapshots, ee.snapshots)):
        if mine != theirs:
            raise AssertionError((seed, 'state %#x' % raw[6], 'actor bytes at call', k, ee.log[k][0],
                                  [hex(i) for i in range(0x320) if mine[i] != theirs[i]][:16]))
    if got != want:
        diff = [hex(i) for i in range(0x320) if got[i] != want[i]]
        raise AssertionError((seed, 'state %#x' % raw[6], 'actor bytes differ at', diff[:24]))
    # Which entries of the original tables the case read (the native embeds
    # them; every entry must be read and matched at least once).
    names = [e[0] for e in ee.log]
    if 'steer_input' in names and raw[6] in (0x14, 0x20, 0x2A):
        k = names.index('steer_input')
        tags.add(('D_00275498', ee.snapshots[k][0x2F1]))
    if ('request', 0x7C, 0, bits(5.0)) in ee.log:
        tags.add(('D_00248600', want[0x23F]))
    if raw[6] == 3 and names[:1] == ['hang_clear'] and len(names) <= 2 and want[0x24C:0x250] == bytes(4):
        tags.add(('D_00248600', want[0x23F]))
    if raw[6] == 0x14 and 'cosine' in names:
        tags.add(('D_002485E0', ee.snapshots[names.index('cosine')][0x23F]))
        # D_002485F0 is read with the same +23F index right after D_002485E0,
        # with no call between the two reads (+204 is compared byte for byte).
        tags.add(('D_002485F0', ee.snapshots[names.index('cosine')][0x23F]))
    return seed, raw[6], tags, frozenset(ee.branches)


def f240_case(seed):
    rng = random.Random(seed)
    raw = bytearray(rng.randrange(256) for _ in range(0x320))
    for offset in (0x224, 0x22C):
        struct.pack_into('<I', raw, offset, rng.choice([0, 0x80000000, 1, 0x807FFFFF, 0x00800000, 0x3F800000,
                                                        0x7F800000, 0x7FC00000, finite(rng)]))
    raw[0xF] = rng.choice([0, 2, 0xFD, 0xFF, rng.randrange(256)])
    struct.pack_into('<I', raw, 0x200, rng.choice([0, 0x1000, 0xFFFFEFFF, rng.randrange(1 << 32)]))
    struct.pack_into('<I', raw, 0x3C, rng.choice([bits(3.0), bits(2.9999998), bits(3.0000002), 0x7F800000,
                                                   0xFF800000, 0x7FC00000, 0x7F7FFFFF, 1, 0x80000000, finite(rng)]))
    arg = rng.choice([0, 1, 2, -1])
    ee = oracle_ee()
    ee.reset(); ee.branches = set(); ee.hooks = {}
    ee.write(PLAYER, bytes(raw))
    ee.call(F240, (PLAYER, arg))
    live = LiveActor(); C.memmove(live.bytes, bytes(raw), 0x320)
    result = NATIVE.em_player_hang_0017F240(C.byref(live), arg)
    assert result == s32(ee.r[2]), (seed, 'return', result, ee.r[2])
    want = bytes(ee.read(PLAYER, 0x320))
    assert bytes(live.bytes) == want, (seed, [hex(i) for i in range(0x320) if live.bytes[i] != want[i]])
    return frozenset(ee.branches)


VADD_SPECIALS = (0, 0x80000000, 1, 0x807FFFFF, 0x7F7FFFFF, 0xFF7FFFFF, 0x7F800000, 0xFF800000,
                 0x7FC00000, 0x7F800001, 0xFFC00000, 0x3F800000, 0x33800000, 0x4B000000)


def vadd_case(seed):
    rng = random.Random(seed)
    a = [rng.choice(VADD_SPECIALS) if rng.random() < 0.3 else rng.randrange(1 << 32) for _ in range(4)]
    b = [rng.choice(VADD_SPECIALS) if rng.random() < 0.3 else
         (bits(-number(a[i])) if rng.random() < 0.2 and (a[i] >> 23 & 0xFF) != 0xFF else rng.randrange(1 << 32))
         for i in range(4)]
    alias = rng.choice(['none', 'a', 'b'])
    pa, pb = SCRATCH, SCRATCH + 0x10
    out = {'none': SCRATCH + 0x20, 'a': pa, 'b': pb}[alias]
    ee = oracle_ee()
    ee.reset(); ee.hooks = {}
    for i in range(4):
        ee.save(pa + 4 * i, a[i]); ee.save(pb + 4 * i, b[i]); ee.save(SCRATCH + 0x20 + 4 * i, 0)
    ee.call(VADD, (out, pa, pb))
    want = words(ee, out, 4)
    fa, fb, fo = (C.c_uint32 * 4)(*a), (C.c_uint32 * 4)(*b), (C.c_uint32 * 4)()
    target = {'none': fo, 'a': fa, 'b': fb}[alias]
    cast = lambda arr: C.cast(arr, FP)
    assert NATIVE.em_player_hang_vadd(None, cast(target), cast(fa), cast(fb)) == 0
    got = tuple(target[i] for i in range(4))
    assert got == want, (seed, alias, [hex(x) for x in a], [hex(x) for x in b], [hex(x) for x in want],
                         [hex(x) for x in got])
    return alias


def refusal_checks():
    """A missing worker refuses before any write; out-of-table indexes fault."""
    live = LiveActor(); raw = bytes(range(256)) * 3 + bytes(0x20); C.memmove(live.bytes, raw, 0x320)
    side = Native(1, HangScene(0x11, 0, 0, 0x40), {k: 0 for k in ((0, 4), (0, 8), (1, 8), (1, 0xC4))})
    for field, _ in WORKER_FIELDS[1:]:
        saved = getattr(side.workers, field)
        setattr(side.workers, field, type(saved)())
        assert NATIVE.em_player_hang_state(C.addressof(side.workers), C.byref(live)) == -1, field
        assert bytes(live.bytes) == raw, ('refusal wrote', field)
        setattr(side.workers, field, saved)
    assert NATIVE.em_player_hang_state(None, C.byref(live)) == -1
    # +23F outside D_00248600 (sub-state 3 hold, +24C = 0 after a scripted takeover).
    bad = bytearray(raw); bad[6] = 3; struct.pack_into('<I', bad, 0x200, 0); struct.pack_into('<f', bad, 0x3C, 99.0)
    live2 = LiveActor(); C.memmove(live2.bytes, bytes(bad), 0x320)
    side2 = Native(2, HangScene(0xB, 0, 0, 0), {k: 0 for k in ((0, 4), (0, 8), (1, 8), (1, 0xC4))})
    side2.workers.hang_clear = side2._keep(ACT_R_FN(lambda _, a, r: (r.__setitem__(0, 0), 0)[1]))
    side2.workers.steer_input = side2._keep(ACT_FN(lambda _, a: (a.contents.bytes.__setitem__(0x23F, 4),
                                                                  [a.contents.bytes.__setitem__(0x24C + i, 0) for i in range(4)], 0)[2]))
    assert NATIVE.em_player_hang_state(C.addressof(side2.workers), C.byref(live2)) == -1, '+23F = 4'
    return len(WORKER_FIELDS) - 1


def main():
    global RAM, ELF, NATIVE, CAPTURED
    ELF = read_elf()
    if not RAM_PATH.exists():
        raise SystemExit('missing %s (the captured AREA11 RAM; docs/PLAYER_HANG.md)' % RAM_PATH)
    RAM = RAM_PATH.read_bytes()
    # The executed code and the tables in the captured RAM are the pinned ELF's.
    for address, size in ((HANG, HANG_END - HANG), (F240, F240_END - F240), (VADD, 0x14)) + TABLES:
        at = address - 0x100000 + 0x300
        assert RAM[address:address + size] == ELF[at:at + size], ('captured RAM differs from the ELF', hex(address))
    CAPTURED = RAM[PLAYER:PLAYER + 0x320]
    NATIVE = build_native()

    counts = {}
    count = reference_mode.pick(200000, 12000)
    results = reference_mode.parallel_map(run_case, [0x1647D0 * 7 + i for i in range(count)])
    branches, tags, per_state = set(), set(), {}
    for seed, st, case_tags, case_branches in results:
        branches |= case_branches; tags |= case_tags
        per_state[st] = per_state.get(st, 0) + 1
    f240 = reference_mode.parallel_map(f240_case, [0x17F240 + i for i in range(reference_mode.pick(40000, 3000))])
    for case_branches in f240:
        branches |= case_branches
    vadd = reference_mode.parallel_map(vadd_case, [0x1028B8 + i for i in range(reference_mode.pick(40000, 3000))])

    wanted = [(pc, taken) for start, end in ((HANG, HANG_END), (F240, F240_END))
              for pc in conditional_branches(RAM, start, end) for taken in (False, True)]
    missing = [(hex(pc), taken) for pc, taken in wanted if (pc, taken) not in branches]
    assert not missing, ('branch outcomes never exercised', missing)
    for state in STATES:
        assert per_state.get(state, 0) > 0, ('sub-state never run', hex(state))
    for table, size in (('D_00275498', 2), ('D_00248600', 4), ('D_002485E0', 4), ('D_002485F0', 4)):
        for index in range(size):
            assert (table, index) in tags, (table, 'entry never read', index)
    assert 'fault' in tags, 'no worker fault propagated'
    assert set(vadd) == {'none', 'a', 'b'}
    workers = refusal_checks()
    counts['001647D0 cases'] = len(results)
    reference_mode.banner(reference_mode.part(len(results), 200000, '001647D0 cases'),
                          reference_mode.part(len(f240), 40000, '0017F240 cases'),
                          reference_mode.part(len(vadd), 40000, '001028B8 cases'))
    faults = sum(1 for _, _, case_tags, _ in results if 'fault' in case_tags)
    print('player hang reference: PASS -- %d 001647D0 cases over the %d handled sub-states and %d '
          'default-path +6 values: all 0x320 actor bytes after the call and at every callee entry, and '
          'every callee call with its arguments, identical; %d/%d conditional branch outcomes of '
          '001647D0 + 0017F240 exercised; every entry of D_00248600 / D_002485E0 / D_00275498 read; '
          '%d worker faults stop at the original bytes; %d 0017F240 and %d 001028B8 cases '
          'bit-identical; %d missing-worker refusals'
          % (len(results), len(STATES), len(set(per_state) - set(STATES)), len(wanted) - len(missing),
             len(wanted), faults, len(f240), len(vadd), workers))


if __name__ == '__main__':
    main()
