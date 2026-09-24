#!/usr/bin/env python3
"""Execute the original player recovery helpers and compare em_player_recovery.c.

docs/PLAYER_RECOVERY.md. The user's pinned ELF supplies every instruction and
table (none are embedded here). Two evidence modes:

- Unit oracle (default; EM_TEST_FULL=1 for the exhaustive sweep): the
  ORIGINAL 00178B90, 00178EC0, 001751A0, 002243F0, 00224B80, 0017D080,
  0017C860 and 00162A40 run in an EE interpreter whose FPU (COP1) and VU0
  macro arithmetic follow tools/ee_float_model.py (docs/EE_FLOAT_MODEL.md);
  the shared interpreter's own float ops are not used. Their pure leaves
  (0011DF78, float_to_int and its soft-float unpack, 001B1470, 001026A0,
  001028B8, 00102948, 0017D040, 00128350, 001000C0 and the fp-bit compare)
  execute unhooked; every other callee is hooked, scripted and recorded, and
  may rewrite record bytes (so re-reads after a call are checked). Any other
  call target is refused. Compared: all 0x320 record bytes, the scratchpad
  words 0x70003A20..2C, the return value and the callee sequence with its
  arguments (floats as bit patterns).
  Also: a deterministic call-order sweep (each observed path, each call k
  rewriting every field), fail-stop at every call and for every missing
  worker, direct sweeps of float_to_int / 001B1470 / the 0.6pi compare, the
  binding adapters, and every original instruction executed (DEAD lists the
  unreachable words).
- Route mode (EM_TEST_ROUTE=1): the original player stage replays PCSX2
  route captures (tools/test_player_slide_reference.RouteReplay) twice, once
  unchanged and once with the native routines hooked in at their original
  addresses (their workers call the original callees in the same EE). Every
  frame the 0x320 player bytes, the scratch words and the sound/effect calls
  must be identical, and every trace row must match the capture. A replay
  ends before the first scripted takeover row (RouteReplay runs no scripts).
  EM_ROUTE_BEATS selects beats (default: all six first-level beats).
"""
import ctypes as C
import math
import os
import random
import struct
import subprocess
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode  # noqa: E402
import ee_float_model as M  # noqa: E402
import test_player_slide_reference as shared  # noqa: E402
from test_player_slide_reference import EE, RETURN, read_elf, s32  # noqa: E402

REFERENCE = shared.REFERENCE
LANE = ROOT / 'build/player_recovery_reference'

_F = struct.Struct('<f')
_I = struct.Struct('<I')


def f2b(value):
    return _I.unpack(_F.pack(value))[0]


def b2f(word):
    return _F.unpack(_I.pack(word & 0xFFFFFFFF))[0]


# ======================================================================
# The interpreter with the measured EE float model
# ======================================================================
class ModelEE(EE):
    """The shared bounded EE core with COP1 and VU0 macro arithmetic taken
    from tools/ee_float_model.py (raw bit patterns throughout). Instructions
    the model does not define (absent from the original) raise. With
    `allowed` set, a call to any target that is neither hooked nor listed
    raises (the unit oracle's closed world)."""

    def __init__(self, elf, ram=None, spad=None):
        super().__init__(elf, ram, spad)
        self.acc = 0
        self.vacc = [0, 0, 0, 0]
        self.q = 0
        self.allowed = None
        self.cover, self.cover_range = None, (0, 0)

    # ---- COP1 -------------------------------------------------------------
    def cop1(self, word, pc):
        rs, ft = word >> 21 & 31, word >> 16 & 31
        fs, fd, fn = word >> 11 & 31, word >> 6 & 31, word & 63
        f = self.f
        if rs in (0, 2, 4, 6):
            return EE.cop1(self, word, pc)
        if rs == 20:
            if fn == 32:
                f[fd] = M.ee_cvt_s_w(f[fs]); return
            raise AssertionError(('cvt.?.w', fn, hex(pc)))
        if rs != 16:
            raise AssertionError(('COP1', rs, hex(pc)))
        a, b = f[fs] & 0xFFFFFFFF, f[ft] & 0xFFFFFFFF
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
        else:
            raise AssertionError(('FPU op outside the model', fn, hex(pc)))

    # ---- VU0 macro ------------------------------------------------------------
    def macro(self, word):
        op, fs, ft, fd = word & 63, word >> 11 & 31, word >> 16 & 31, word >> 6 & 31
        dest = word >> 21 & 15
        S, T = list(self.vf[fs]), list(self.vf[ft])

        def write(reg, values, to_acc=False):
            for k in range(4):
                if dest & (8 >> k):
                    if to_acc:
                        self.vacc[k] = values[k]
                    elif reg:
                        self.vf[reg][k] = values[k]

        def lanes(name, bc=None, q=False, reads_acc=False, outer=False):
            out = [0] * 4
            for k in range(4):
                if not dest & (8 >> k):
                    continue
                if outer:
                    s, t = S[(1, 2, 0)[k]], T[(2, 0, 1)[k]]
                else:
                    s = S[k]
                    t = self.q if q else (T[bc] if bc is not None else T[k])
                out[k] = M.vu_lane(name, dest, bc, s, t, self.vacc[k] if reads_acc else None)
            return out

        if op < 4: write(fd, lanes('vaddbc', op))
        elif op < 8: write(fd, lanes('vsubbc', op - 4))
        elif op < 12: write(fd, lanes('vmaddbc', op - 8, reads_acc=True))
        elif 16 <= op < 20: write(fd, [M.vu_max(S[k], T[op - 16]) for k in range(4)])
        elif 20 <= op < 24: write(fd, [M.vu_min(S[k], T[op - 20]) for k in range(4)])
        elif 24 <= op < 28: write(fd, lanes('vmulbc', op - 24))
        elif op == 28: write(fd, lanes('vmulq', q=True))
        elif op == 32: write(fd, lanes('vaddq', q=True))
        elif op == 40: write(fd, lanes('vadd'))
        elif op == 42: write(fd, lanes('vmul'))
        elif op == 44: write(fd, lanes('vsub'))
        elif op == 46: write(fd, lanes('vopmsub', reads_acc=True, outer=True))
        elif op >= 60:
            special = (word >> 6 & 31) << 2 | (op & 3)
            if special in (0x30, 0x31):                                  # vmove / vmr32
                raw = list(S)
                if special == 0x31: raw = raw[1:] + raw[:1]
                write(ft, raw)
            elif 0x08 <= special <= 0x0B:
                write(0, lanes('vmaddabc', special - 8, reads_acc=True), to_acc=True)
            elif 0x18 <= special <= 0x1B:
                write(0, lanes('vmulabc', special - 0x18), to_acc=True)
            elif special == 0x2E:
                write(0, lanes('vopmula', outer=True), to_acc=True)
            elif special == 0x1D:
                write(ft, [M.vu_abs(S[k]) for k in range(4)])
            elif special in (0x14, 0x15):
                write(ft, [M.vu_ftoi(S[k], 0 if special == 0x14 else 4) for k in range(4)])
            elif special in (0x10, 0x11):
                write(ft, [M.vu_itof(S[k], 0 if special == 0x10 else 4) for k in range(4)])
            elif special == 0x38:
                fsf, ftf = word >> 21 & 3, word >> 23 & 3
                self.q = M.vu_div(S[fsf], T[ftf], fsf, ftf)
            elif special == 0x39:
                self.q = M.vu_sqrt(T[word >> 23 & 3])
            elif special in (0x2F, 0x3B):                                # vnop / vwaitq
                pass
            else:
                raise AssertionError(('VU special outside the model', hex(word), hex(special)))
        else:
            raise AssertionError(('VU op outside the model', hex(word), op))

    # ---- control flow with the closed-world call check ---------------------
    def run(self, pc):
        hooks, allowed = self.hooks, self.allowed
        cover = self.cover
        if cover is None:
            load = self.load
        else:
            lo, hi = self.cover_range
            raw = self.load

            def load(address, size=4):
                if lo <= address < hi:
                    cover.add(address)
                return raw(address, size)
        steps, limit = 0, self.limit
        while True:
            if pc == RETURN:
                self.steps += steps
                return
            hook = hooks.get(pc)
            if hook is not None:
                hook(self)
                pc = self.r[31] & 0xFFFFFFFF
                continue
            word = load(pc)
            steps += 1
            if steps > limit:
                raise AssertionError(('step limit', hex(pc)))
            op = word >> 26
            if op in (2, 3):
                target = (pc & 0xF0000000) | ((word & 0x3FFFFFF) << 2)
                if op == 3:
                    self.r[31] = pc + 8
                    if allowed is not None and target not in hooks and target not in allowed:
                        raise AssertionError(('call outside the closed world', hex(target), hex(pc)))
                self.execute(load(pc + 4), pc + 4)
                pc = target
                continue
            if op == 0 and word & 63 in (8, 9):
                target = self.r[word >> 21 & 31] & 0xFFFFFFFF
                if word & 63 == 9:
                    rd = word >> 11 & 31
                    if rd: self.r[rd] = pc + 8
                    if allowed is not None and target not in hooks and target not in allowed:
                        raise AssertionError(('jalr outside the closed world', hex(target), hex(pc)))
                self.execute(load(pc + 4), pc + 4)
                pc = target
                continue
            b = self.branch(word, pc)
            if b is None:
                self.execute(word, pc)
                pc += 4
                continue
            taken, target, likely = b
            if taken:
                self.execute(load(pc + 4), pc + 4)
                pc = target
            elif likely:
                pc += 8
            else:
                self.execute(load(pc + 4), pc + 4)
                pc += 8


# ======================================================================
# Addresses
# ======================================================================
TRANSLATE, STRAFE, QUADRANT = 0x178B90, 0x178EC0, 0x1751A0
REACT_A, REACT_B, CATCH, GRAB, HANG = 0x2243F0, 0x224B80, 0x17D080, 0x17C860, 0x162A40
SINE, COSINE, ATAN2, SQRT = 0x11E2A8, 0x11DE90, 0x11E620, 0x11E748
PROBES, REQUEST, ARBITER, FRAMES, SOUND = 0x1764E0, 0x1749A0, 0x1749F0, 0x1C61D0, 0x1FBD50
SHAKE, RAND = 0x1B61C0, 0x122BB8
R350, R270, R120, R190, R490 = 0x21C350, 0x21C270, 0x21C120, 0x21C190, 0x21D490
MOVE, SWEEP, SEGMENT, TABLE, ATTR = 0x19AD00, 0x19AFE0, 0x19A570, 0x19BC40, 0x19A180
LEDGE, LIP, SIDES, HANDS, DEPTH = 0x177510, 0x1775E0, 0x1776E0, 0x177CF0, 0x177B80
HANG_CLEAR, HANG_ROW, SKELETON, COLUMN = 0x17F320, 0x188550, 0x1C6DA0, 0x1760C0
HEADING, REENTRY, HANDOFF, FLOOR, FALL = 0x174AC0, 0x17C440, 0x17C540, 0x175900, 0x1796C0
# The pure leaves executed unhooked (and the soft-float routines they call).
LEAVES = {0x11DF78, 0x1281C0, 0x1278C0, 0x1B1470, 0x1026A0, 0x1028B8, 0x102948, 0x17D040,
          0x128350, 0x127728, 0x126AB8, 0x1000C0, 0x1274B0, 0x126BE8, 0x127398}
# 00162A40 calls 00178B90, which runs as the original too (its own callees hooked).
INTERNAL = {TRANSLATE}
ACTOR, NODES, RECORDS, OWNERS = 0x680000, 0x6D0000, 0x6E0000, 0x6F0000
D_00282250 = 0x282250
SCRATCH = (0x70003A20, 0x70003A24, 0x70003A28, 0x70003A2C)


# ======================================================================
# Native structures
# ======================================================================
class LiveActor(C.Structure):
    _fields_ = [('bytes', C.c_uint8 * 0x320), ('link_owner', C.c_void_p), ('link_prev', C.c_void_p),
                ('link_flags', C.c_uint8), ('link_type', C.c_uint8)]


ProbeHit = shared.ProbeHit


class ClimbTable(C.Structure):
    _fields_ = [('count', C.c_int), ('flags', C.c_uint16 * 16), ('height', C.c_float * 16),
                ('aux', C.c_float * 16), ('object_kind', C.c_uint8 * 16), ('object_node', C.c_int16 * 16)]


class Ledge(C.Structure):
    _fields_ = [('point', C.c_float * 3), ('normal', C.c_float * 3), ('heading', C.c_float),
                ('matrix', C.c_float * 16)]


class Scene(C.Structure):
    # em_player_recovery.h: D_008106F1 is a pointer at its one canonical
    # byte; `d8106F1` below reads and writes that byte (a cell per scene).
    _fields_ = [('camera_yaw', C.c_float), ('p8106F1', C.POINTER(C.c_uint8)), ('spad3B8D', C.c_uint8),
                ('pad_gait', C.c_uint8), ('pad_x', C.c_uint8), ('pad_y', C.c_uint8), ('area', C.c_uint8)]

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.cell_8106F1 = C.c_uint8(0)
        self.p8106F1 = C.pointer(self.cell_8106F1)

    @property
    def d8106F1(self):
        return self.cell_8106F1.value

    @d8106F1.setter
    def d8106F1(self, value):
        self.cell_8106F1.value = value


class Scratch(C.Structure):
    _fields_ = [('words', C.c_float * 4)]


A = C.POINTER(LiveActor)
FP = C.POINTER(C.c_float)
IP = C.POINTER(C.c_int)
MATH1 = C.CFUNCTYPE(C.c_float, C.c_void_p, C.c_float)
MATH2 = C.CFUNCTYPE(C.c_float, C.c_void_p, C.c_float, C.c_float)
ACT = C.CFUNCTYPE(C.c_int, C.c_void_p, A)
ACT_I = C.CFUNCTYPE(C.c_int, C.c_void_p, A, C.c_int)
ACT_R = C.CFUNCTYPE(C.c_int, C.c_void_p, A, IP)
REQ = C.CFUNCTYPE(C.c_int, C.c_void_p, A, C.c_int, C.c_int, C.c_float)
ARB = C.CFUNCTYPE(C.c_int, C.c_void_p, A, C.c_int, C.c_float, C.c_float)
FRM = C.CFUNCTYPE(C.c_int, C.c_void_p, A, C.c_int, IP)
SND = C.CFUNCTYPE(C.c_int, C.c_void_p, A, C.c_uint)
SHK = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.c_int, C.c_int, C.c_int)
RND = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_uint32))
MOV = C.CFUNCTYPE(C.c_int, C.c_void_p, A, FP, C.c_uint, C.POINTER(ProbeHit))
SWP = C.CFUNCTYPE(C.c_int, C.c_void_p, A, FP, FP, C.c_uint, C.POINTER(ProbeHit))
SEG = C.CFUNCTYPE(C.c_int, C.c_void_p, FP, FP, C.c_uint, C.c_int, IP)
TBL = C.CFUNCTYPE(C.c_int, C.c_void_p, FP, C.POINTER(ClimbTable))
ATR = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(ClimbTable), C.c_int, IP)
LDG = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(ProbeHit), C.POINTER(Ledge))
LIPF = C.CFUNCTYPE(C.c_int, C.c_void_p, A, C.POINTER(Ledge), C.c_int, C.c_float, IP)
SWY = C.CFUNCTYPE(C.c_int, C.c_void_p, A, C.POINTER(Ledge), C.c_float, IP)
SKL = C.CFUNCTYPE(C.c_int, C.c_void_p, A, FP)
COL = C.CFUNCTYPE(C.c_int, C.c_void_p, A, FP, C.c_float, IP)
FLR = C.CFUNCTYPE(C.c_int, C.c_void_p, A, C.c_int, IP)

WORKER_FIELDS = [
    ('sine', MATH1), ('cosine', MATH1), ('atan2', MATH2), ('sqrt', MATH1), ('probes', ACT),
    ('request', REQ), ('arbiter', ARB), ('clip_frames', FRM), ('sound', SND), ('shake', SHK),
    ('random', RND), ('react_0021C350', ACT), ('react_0021C270', ACT), ('react_0021C120', ACT),
    ('react_0021C190', ACT_R), ('react_0021D490', ACT), ('move', MOV), ('sweep', SWP),
    ('segment', SEG), ('table', TBL), ('attribute', ATR), ('ledge', LDG), ('lip', LIPF),
    ('sides', SWY), ('hands', SWY), ('depth', SWY), ('hang_clear', ACT_R), ('hang_row', ACT_R),
    ('skeleton', SKL), ('column', COL), ('heading', ACT_I), ('reentry', ACT_I), ('handoff', ACT),
    ('floor', FLR), ('fall', ACT)]


class Workers(C.Structure):
    _fields_ = [('context', C.c_void_p)] + WORKER_FIELDS


class Live(C.Structure):
    _fields_ = [('workers', Workers), ('live', A),
                ('scene', C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(Scene))),
                ('scene_context', C.c_void_p), ('scratch', Scratch),
                ('shared3A20', C.POINTER(C.c_uint32))]


def build_native(name='recovery'):
    LANE.mkdir(parents=True, exist_ok=True)
    lib = LANE / (name + ('.dylib' if sys.platform == 'darwin' else '.so'))
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-shared', '-fPIC', '-Isrc', 'src/game/em_player_recovery.c',
                    'src/game/em_player_slide.c', 'src/game/em_player_climb.c',
                    'src/game/em_player_floor.c', '-lm', '-o', str(lib)], cwd=ROOT, check=True)
    n = C.CDLL(str(lib))
    W, S, X, I = C.POINTER(Workers), C.POINTER(Scene), C.POINTER(Scratch), C.POINTER(C.c_int)
    n.em_player_recovery_translate.argtypes = [A, C.c_int, W]
    n.em_player_recovery_strafe.argtypes = [A]
    n.em_player_recovery_stick_quadrant.argtypes = [A, S, X, W]
    n.em_player_recovery_react_002243F0.argtypes = [A, X, W, I]
    n.em_player_recovery_react_00224B80.argtypes = [A, S, W, I]
    n.em_player_recovery_ledge_catch.argtypes = [A, S, X, W, I]
    n.em_player_recovery_ledge_grab.argtypes = [A, C.c_float, S, X, W, I]
    n.em_player_recovery_hang_entry.argtypes = [A, W]
    n.em_player_recovery_float_to_int.argtypes = [C.c_uint32]
    n.em_player_recovery_float_to_int.restype = C.c_int32
    n.em_player_recovery_wrap.argtypes = [C.c_uint32, C.POINTER(C.c_uint32)]
    n.em_player_recovery_below_0_6pi.argtypes = [C.c_uint32]
    n.em_player_recovery_state4.argtypes = [C.c_void_p, A]
    n.em_player_recovery_slide_translate.argtypes = [C.c_void_p, C.c_void_p, C.c_int]
    n.em_player_recovery_slide_damage.argtypes = [C.c_void_p, C.c_void_p, I]
    n.em_player_recovery_climb_translate.argtypes = [C.c_void_p, C.c_void_p, C.c_int]
    n.em_player_recovery_translate_worker.argtypes = [C.c_void_p, A, C.c_int]
    n.em_player_recovery_strafe_worker.argtypes = [C.c_void_p, A]
    n.em_player_recovery_stick_quadrant_worker.argtypes = [C.c_void_p, A]
    n.em_player_recovery_react_002243F0_worker.argtypes = [C.c_void_p, A, I]
    n.em_player_recovery_react_00224B80_worker.argtypes = [C.c_void_p, A, I]
    n.em_player_recovery_ledge_catch_worker.argtypes = [C.c_void_p, A, I]
    n.em_player_recovery_ledge_grab_worker.argtypes = [C.c_void_p, A, C.c_uint32, I]
    n.em_player_slide_actor_from_live.argtypes = [A, C.c_void_p]
    n.em_player_slide_actor_to_live.argtypes = [C.c_void_p, A]
    n.em_player_climb_actor_from_live.argtypes = [A, C.c_void_p]
    n.em_player_climb_actor_to_live.argtypes = [C.c_void_p, A]
    return n


# ======================================================================
# Scripts: every boundary result, consumed in call order by both sides
# ======================================================================
def fp32(value):
    """A host double as the nearest binary32 bit pattern."""
    return f2b(value)


def host_sin(bits):
    return fp32(math.sin(b2f(bits)))


def host_cos(bits):
    return fp32(math.cos(b2f(bits)))


def host_sqrt(bits):
    x = b2f(bits)
    return fp32(math.sqrt(x) if x > 0 else 0.0)


def host_atan2(y, x):
    return fp32(math.atan2(b2f(y), b2f(x)))


class Script:
    """Queues of scripted results. `effects` are record rewrites applied by
    every worker that receives the record: (offset, size, raw) triples.
    `results` maps a worker name to its queue of int results."""
    NAMES = ('effects', 'kinds', 'hits', 'tables', 'attributes', 'ledges', 'frames',
             'randoms', 'clips', 'nodes', 'atans', 'cosines', 'sqrts')

    def __init__(self):
        for name in self.NAMES:
            setattr(self, name, [])
        self.results = {}
        self.effect_calls = 0

    def copy(self):
        other = Script()
        for name in self.NAMES:
            setattr(other, name, list(getattr(self, name)))
        other.results = {k: list(v) for k, v in self.results.items()}
        return other

    def take(self, name, default):
        if name == 'effects':
            self.effect_calls += 1
        queue = getattr(self, name)
        return queue.pop(0) if queue else default

    def result(self, name, default=0):
        queue = self.results.get(name)
        return queue.pop(0) if queue else default


def apply_effect(store, effect):
    """store(offset, size, raw) for each triple of one scripted rewrite."""
    for offset, size, raw in effect or ():
        store(offset, size, raw)


# ======================================================================
# The original side
# ======================================================================
class Oracle:
    def __init__(self, record, scene, scratch, script):
        self.ee = ee = ModelEE(ELF)
        ee.allowed = LEAVES | INTERNAL
        self.script, self.log = script, []
        self.records = RECORDS
        ee.write(ACTOR, bytes(record))
        ee.save(0x8106F1, scene.d8106F1, 1)
        ee.save(0x70003B8D, scene.spad3B8D, 1)
        ee.save(0x810E57, scene.pad_gait, 1)
        ee.save(0x810E64, scene.pad_x, 1)
        ee.save(0x810E65, scene.pad_y, 1)
        ee.save(0x810700, scene.area, 1)
        ee.save(0x8106A0, f2b(scene.camera_yaw))
        ee.save(0x275B40, NODES)
        ee.save(NODES + 4, NODES + 0x200)
        for address, word in zip(SCRATCH, scratch):
            ee.save(address, word)
        h = ee.hooks
        h[SINE] = lambda e: self.math('sin', host_sin, e)
        h[COSINE] = self.cosine
        h[SQRT] = self.sqrt
        h[ATAN2] = self.atan2
        h[PROBES] = lambda e: self.actor_call(e, 'probes')
        h[REQUEST] = lambda e: self.actor_call(e, 'request', s32(e.arg(1)), s32(e.arg(2)), e.f[12])
        h[ARBITER] = lambda e: self.actor_call(e, 'arbiter', s32(e.arg(1)), e.f[12], e.f[13])
        h[FRAMES] = self.frames
        h[SOUND] = self.sound
        h[SHAKE] = lambda e: self.plain('shake', s32(e.arg(0)), s32(e.arg(1)), s32(e.arg(2)), s32(e.arg(3)))
        h[RAND] = self.rand
        for address, name in ((R350, 'r350'), (R270, 'r270'), (R120, 'r120'), (R490, 'r490'),
                              (HANDOFF, 'handoff'), (FALL, 'fall')):
            h[address] = (lambda n: lambda e: self.actor_call(e, n))(name)
        for address, name in ((R190, 'r190'), (HANG_CLEAR, 'hang_clear')):
            h[address] = (lambda n: lambda e: self.actor_result(e, n))(name)
        h[HANG_ROW] = self.hang_row
        h[HEADING] = lambda e: self.actor_call(e, 'heading', s32(e.arg(1)))
        h[REENTRY] = lambda e: self.actor_call(e, 'reentry', s32(e.arg(1)))
        h[FLOOR] = lambda e: self.actor_result(e, 'floor', s32(e.arg(1)))
        h[SKELETON] = self.skeleton
        h[COLUMN] = self.column
        h[MOVE] = self.move
        h[SWEEP] = self.sweep
        h[SEGMENT] = self.segment
        h[TABLE] = self.table
        h[ATTR] = self.attribute
        h[LEDGE] = self.ledge
        h[LIP] = lambda e: self.sweeper(e, 'lip', s32(e.arg(1)))
        h[SIDES] = lambda e: self.sweeper(e, 'sides')
        h[HANDS] = lambda e: self.sweeper(e, 'hands')
        h[DEPTH] = lambda e: self.sweeper(e, 'depth')

    # ---- helpers -------------------------------------------------------------
    def vec(self, address, count=3):
        return tuple(self.ee.load(address + 4 * i) for i in range(count))

    def store(self, offset, size, raw):
        self.ee.save(ACTOR + offset, raw, size)

    def check_actor(self, e, name):
        assert e.arg(0) == ACTOR, (name, 'a0 is not the record', hex(e.arg(0)))

    def math(self, name, fn, e):
        x = e.f[12] & 0xFFFFFFFF
        self.log.append((name, x))
        e.f[0] = fn(x)

    def sqrt(self, e):
        x = e.f[12] & 0xFFFFFFFF
        self.log.append(('sqrt', x))
        value = self.script.take('sqrts', None)
        e.f[0] = host_sqrt(x) if value is None else value

    def cosine(self, e):
        x = e.f[12] & 0xFFFFFFFF
        self.log.append(('cos', x))
        value = self.script.take('cosines', None)
        e.f[0] = host_cos(x) if value is None else value

    def atan2(self, e):
        y, x = e.f[12] & 0xFFFFFFFF, e.f[13] & 0xFFFFFFFF
        self.log.append(('atan2', y, x))
        value = self.script.take('atans', None)
        e.f[0] = host_atan2(y, x) if value is None else value

    def plain(self, name, *args):
        self.log.append((name,) + args)
        self.ee.ret_int(0)

    def actor_call(self, e, name, *args):
        self.check_actor(e, name)
        self.log.append((name,) + args)
        apply_effect(self.store, self.script.take('effects', None))
        e.ret_int(0)

    def actor_result(self, e, name, *args):
        self.check_actor(e, name)
        self.log.append((name,) + args)
        apply_effect(self.store, self.script.take('effects', None))
        e.ret_int(self.script.result(name))

    def frames(self, e):
        assert e.arg(0) == self.ee.load(ACTOR + 0x40), ('frames bank', hex(e.arg(0)))
        self.log.append(('frames', s32(e.arg(1))))
        e.ret_int(self.script.take('frames', 0))

    def sound(self, e):
        self.check_actor(e, 'sound')
        assert s32(e.arg(2)) == 0 and e.f[12] & 0xFFFFFFFF == f2b(300.0), 'sound arguments'
        self.log.append(('sound', e.arg(1)))
        apply_effect(self.store, self.script.take('effects', None))
        e.ret_int(0)

    def rand(self, e):
        self.log.append(('rand',))
        e.ret_int(self.script.take('randoms', 0))

    def hang_row(self, e):
        self.check_actor(e, 'hang_row')
        self.log.append(('hang_row',))
        apply_effect(self.store, self.script.take('effects', None))
        e.ret_int(self.script.take('clips', 0))

    def skeleton(self, e):
        self.check_actor(e, 'skeleton')
        self.log.append(('skeleton',))
        apply_effect(self.store, self.script.take('effects', None))
        node = self.script.take('nodes', (0, 0, 0, 0))
        base = self.ee.load(self.ee.load(0x275B40) + 4) + 0xC0
        for i in range(4):
            self.ee.save(base + 4 * i, node[i])
        e.ret_int(0)

    def column(self, e):
        self.check_actor(e, 'column')
        self.log.append(('column', self.vec(e.arg(1)), s32(e.arg(2)), e.f[12] & 0xFFFFFFFF))
        apply_effect(self.store, self.script.take('effects', None))
        e.ret_int(self.script.result('column'))

    def move(self, e):
        self.check_actor(e, 'move')
        self.log.append(('move', self.vec(e.arg(1)), e.arg(2)))
        apply_effect(self.store, self.script.take('effects', None))
        kind, hit = self.script.take('kinds', 0), self.script.take('hits', None)
        if hit is not None:
            self.publish(hit)
        e.ret_int(kind)

    def publish(self, hit):
        """What a probe leaves: 0x700031B0 point, 0x700031D0 node record
        (+1A halfword, +24 normal), 0x700031D4 owner (+2, +3 bytes) or 0."""
        ee = self.ee
        for i in range(3):
            ee.save(0x700031B0 + 4 * i, hit['point'][i])
        record = self.records
        self.records += 0x40
        ee.save(record + 0x1A, hit['node'], 2)
        for i in range(3):
            ee.save(record + 0x24 + 4 * i, hit['normal'][i])
        ee.save(0x700031D0, record)
        owner = hit.get('owner')
        if owner is None:
            ee.save(0x700031D4, 0)
        else:
            address = OWNERS + 0x40 * owner[0]
            ee.save(address + 2, owner[1], 1)
            ee.save(address + 3, owner[2], 1)
            ee.save(0x700031D4, address)

    def sweep(self, e):
        self.check_actor(e, 'sweep')
        self.log.append(('sweep', self.vec(e.arg(1)), self.vec(e.arg(2)), e.arg(3)))
        apply_effect(self.store, self.script.take('effects', None))
        e.ret_int(self.script.result('sweep'))

    def segment(self, e):
        self.log.append(('segment', self.vec(e.arg(0)), self.vec(e.arg(1)), e.arg(2), s32(e.arg(3))))
        e.ret_int(self.script.result('segment'))

    def table(self, e):
        self.log.append(('table', self.vec(e.arg(0))))
        t = self.script.take('tables', {'count': 0})
        ee = self.ee
        ee.save(0x700031E0, t['count'])
        for i in range(t['count']):
            ee.save(0x70003170 + 2 * i, t['flags'][i], 2)
            ee.save(0x700030F0 + 4 * i, t['height'][i])
            ee.save(D_00282250 + 4 * i, t['aux'][i])
        e.ret_int(0)

    def attribute(self, e):
        self.log.append(('attr', s32(e.arg(0)), s32(e.arg(1))))
        e.ret_int(self.script.take('attributes', 0))

    def ledge_frame(self):
        return (self.vec(0x70003050), self.vec(0x70003060), self.ee.load(0x700031E4),
                self.vec(0x70003070, 16))

    def ledge(self, e):
        record = self.ee.load(0x700031D0)
        self.log.append(('ledge', self.vec(0x700031B0), self.vec(record + 0x24)))
        frame = self.script.take('ledges', None)
        ee = self.ee
        for i in range(3):
            ee.save(0x70003050 + 4 * i, frame['point'][i])
            ee.save(0x70003060 + 4 * i, frame['normal'][i])
        ee.save(0x700031E4, frame['heading'])
        for i in range(16):
            ee.save(0x70003070 + 4 * i, frame['matrix'][i])
        e.ret_int(0)

    def sweeper(self, e, name, *args):
        self.check_actor(e, name)
        self.log.append((name,) + args + (e.f[12] & 0xFFFFFFFF, self.ledge_frame()))
        apply_effect(self.store, self.script.take('effects', None))
        e.ret_int(self.script.result(name))

    def record(self):
        return self.ee.read(ACTOR, 0x320)

    def scratch(self):
        return tuple(self.ee.load(a) for a in SCRATCH)


# ======================================================================
# The native side
# ======================================================================
def fvec(pointer, count=3):
    words = C.cast(pointer, C.POINTER(C.c_uint32))
    return tuple(words[i] for i in range(count))


class Native:
    def __init__(self, record, script):
        self.script, self.log = script, []
        self.actor = LiveActor()
        C.memmove(self.actor.bytes, bytes(record), 0x320)
        self.ledges = {}
        fns = {
            'sine': MATH1(lambda _, x: self.math('sin', host_sin, x)),
            'cosine': MATH1(self.cosine),
            'atan2': MATH2(self.atan2),
            'sqrt': MATH1(self.sqrt),
            'probes': ACT(lambda _, a: self.actor_call('probes')),
            'request': REQ(lambda _, a, clip, force, blend: self.actor_call('request', clip, force, f2b(blend))),
            'arbiter': ARB(lambda _, a, clip, blend, frame: self.actor_call('arbiter', clip, f2b(blend), f2b(frame))),
            'clip_frames': FRM(self.frames),
            'sound': SND(self.sound),
            'shake': SHK(lambda _, a0, a1, a2, a3: self.plain('shake', a0, a1, a2, a3)),
            'random': RND(self.rand),
            'react_0021C350': ACT(lambda _, a: self.actor_call('r350')),
            'react_0021C270': ACT(lambda _, a: self.actor_call('r270')),
            'react_0021C120': ACT(lambda _, a: self.actor_call('r120')),
            'react_0021C190': ACT_R(lambda _, a, out: self.actor_result('r190', out)),
            'react_0021D490': ACT(lambda _, a: self.actor_call('r490')),
            'move': MOV(self.move), 'sweep': SWP(self.sweep), 'segment': SEG(self.segment),
            'table': TBL(self.table), 'attribute': ATR(self.attribute), 'ledge': LDG(self.ledge),
            'lip': LIPF(lambda _, a, l, wide, y, out: self.sweeper('lip', l, f2b(y), out, wide)),
            'sides': SWY(lambda _, a, l, y, out: self.sweeper('sides', l, f2b(y), out)),
            'hands': SWY(lambda _, a, l, y, out: self.sweeper('hands', l, f2b(y), out)),
            'depth': SWY(lambda _, a, l, y, out: self.sweeper('depth', l, f2b(y), out)),
            'hang_clear': ACT_R(lambda _, a, out: self.actor_result('hang_clear', out)),
            'hang_row': ACT_R(self.hang_row),
            'skeleton': SKL(self.skeleton),
            'column': COL(self.column),
            'heading': ACT_I(lambda _, a, arg: self.actor_call('heading', arg)),
            'reentry': ACT_I(lambda _, a, arg: self.actor_call('reentry', arg)),
            'handoff': ACT(lambda _, a: self.actor_call('handoff')),
            'floor': FLR(lambda _, a, search, out: self.actor_result('floor', out, search)),
            'fall': ACT(lambda _, a: self.actor_call('fall')),
        }
        self.keep = fns
        self.workers = Workers(None, *[fns[name] for name, _ in WORKER_FIELDS])

    def store(self, offset, size, raw):
        for i in range(size):
            self.actor.bytes[offset + i] = (raw >> (8 * i)) & 0xFF

    def math(self, name, fn, x):
        bits = f2b(x)
        self.log.append((name, bits))
        return b2f(fn(bits))

    def sqrt(self, _, x):
        bits = f2b(x)
        self.log.append(('sqrt', bits))
        value = self.script.take('sqrts', None)
        return b2f(host_sqrt(bits) if value is None else value)

    def cosine(self, _, x):
        bits = f2b(x)
        self.log.append(('cos', bits))
        value = self.script.take('cosines', None)
        return b2f(host_cos(bits) if value is None else value)

    def atan2(self, _, y, x):
        yb, xb = f2b(y), f2b(x)
        self.log.append(('atan2', yb, xb))
        value = self.script.take('atans', None)
        return b2f(host_atan2(yb, xb) if value is None else value)

    def plain(self, name, *args):
        self.log.append((name,) + args)
        return 0

    def actor_call(self, name, *args):
        self.log.append((name,) + args)
        apply_effect(self.store, self.script.take('effects', None))
        return 0

    def actor_result(self, name, out, *args):
        self.log.append((name,) + args)
        apply_effect(self.store, self.script.take('effects', None))
        out[0] = self.script.result(name)
        return 0

    def frames(self, _, a, clip, out):
        self.log.append(('frames', clip))
        out[0] = self.script.take('frames', 0)
        return 0

    def sound(self, _, a, id):
        self.log.append(('sound', id))
        apply_effect(self.store, self.script.take('effects', None))
        return 0

    def rand(self, _, out):
        self.log.append(('rand',))
        out[0] = self.script.take('randoms', 0) & 0xFFFFFFFF
        return 0

    def hang_row(self, _, a, out):
        self.log.append(('hang_row',))
        apply_effect(self.store, self.script.take('effects', None))
        out[0] = self.script.take('clips', 0)
        return 0

    def skeleton(self, _, a, node):
        self.log.append(('skeleton',))
        apply_effect(self.store, self.script.take('effects', None))
        values = self.script.take('nodes', (0, 0, 0, 0))
        words = C.cast(node, C.POINTER(C.c_uint32))
        for i in range(4):
            words[i] = values[i]
        return 0

    def column(self, _, a, at, height, out):
        self.log.append(('column', fvec(at), 1, f2b(height)))
        apply_effect(self.store, self.script.take('effects', None))
        out[0] = self.script.result('column')
        return 0

    def move(self, _, a, target, mask, hit):
        self.log.append(('move', fvec(target), mask))
        apply_effect(self.store, self.script.take('effects', None))
        kind, h = self.script.take('kinds', 0), self.script.take('hits', None)
        if h is not None:
            base = C.addressof(hit[0])
            words = (C.c_uint32 * 3).from_address(base + ProbeHit.point.offset)
            for i in range(3): words[i] = h['point'][i]
            words = (C.c_uint32 * 3).from_address(base + ProbeHit.normal.offset)
            for i in range(3): words[i] = h['normal'][i]
            hit[0].node = h['node']
            hit[0].kind = kind
            owner = h.get('owner')
            if owner is not None:
                hit[0].owner = OWNERS + 0x40 * owner[0]
                hit[0].entity = 1
                hit[0].entity_flags, hit[0].entity_type = owner[1], owner[2]
        return kind

    def sweep(self, _, a, start, end, mask, hit):
        self.log.append(('sweep', fvec(start), fvec(end), mask))
        apply_effect(self.store, self.script.take('effects', None))
        return self.script.result('sweep')

    def segment(self, _, start, end, mask, id, out):
        self.log.append(('segment', fvec(start), fvec(end), mask, id))
        out[0] = self.script.result('segment')
        return 0

    def table(self, _, at, out):
        self.log.append(('table', fvec(at)))
        t = self.script.take('tables', {'count': 0})
        out[0].count = t['count']
        for i in range(t['count']):
            out[0].flags[i] = t['flags'][i]
            base = C.addressof(out[0])
            C.c_uint32.from_address(base + ClimbTable.height.offset + 4 * i).value = t['height'][i]
            C.c_uint32.from_address(base + ClimbTable.aux.offset + 4 * i).value = t['aux'][i]
        return 0

    def attribute(self, _, table, index, out):
        self.log.append(('attr', 0, index))
        out[0] = s32(self.script.take('attributes', 0))
        return 0

    def ledge(self, _, hit, out):
        base = C.addressof(hit[0])
        self.log.append(('ledge', tuple((C.c_uint32 * 3).from_address(base + ProbeHit.point.offset)),
                         tuple((C.c_uint32 * 3).from_address(base + ProbeHit.normal.offset))))
        frame = self.script.take('ledges', None)
        base = C.addressof(out[0])
        words = (C.c_uint32 * 23).from_address(base)
        for i in range(3):
            words[i] = frame['point'][i]
            words[3 + i] = frame['normal'][i]
        words[6] = frame['heading']
        for i in range(16):
            words[7 + i] = frame['matrix'][i]
        return 0

    def sweeper(self, name, ledge, y, out, *args):
        words = (C.c_uint32 * 23).from_address(C.addressof(ledge[0]))
        frame = (tuple(words[0:3]), tuple(words[3:6]), words[6], tuple(words[7:23]))
        self.log.append((name,) + args + (y, frame))
        apply_effect(self.store, self.script.take('effects', None))
        out[0] = self.script.result(name)
        return 0

    def record(self):
        return bytes(self.actor.bytes)


# ======================================================================
# Case generation
# ======================================================================
PI, HALF_PI, QUARTER_PI, THREE_QUARTER_PI = 0x40490FDB, 0x3FC90FDB, 0x3F490FDB, 0x4016CBE4
SPECIALS = (0x00000000, 0x80000000, 0x00000001, 0x807FFFFF, 0x00800000)


def base_record():
    """The captured player record (state 04) when available: realistic
    matrix, bank word and bytes; the cases then rewrite what they vary."""
    path = REFERENCE / 'playable_ee.bin'
    if path.exists():
        with open(path, 'rb') as f:
            f.seek(0x8102B0)
            return bytearray(f.read(0x320))
    record = bytearray(0x320)
    for i in range(4):
        struct.pack_into('<f', record, 0xD0 + 20 * i, 1.0)
    return record


BASE = None


def rf(rng, lo, hi):
    return fp32(rng.uniform(lo, hi))


def ulp(bits, steps):
    """The float `steps` representable values away (same sign side)."""
    return (bits + steps) & 0xFFFFFFFF


def put(record, offset, size, raw):
    record[offset:offset + size] = (raw & ((1 << (8 * size)) - 1)).to_bytes(size, 'little')


def get(record, offset, size=4):
    return int.from_bytes(record[offset:offset + size], 'little')


def solve_add(a, target):
    """A value r with ee_add(a, r) == target, searched near target - a."""
    guess = fp32(b2f(target) - b2f(a))
    for step in range(0, 200):
        for r in (ulp(guess, step), ulp(guess, -step)):
            if M.ee_add(a, r) == target:
                return r
    return None


def solve_minuend_sub(h, target):
    """A value c with ee_sub(h, c) == target, searched near h - target."""
    guess = fp32(b2f(h) - b2f(target))
    for step in range(0, 400):
        for c in (ulp(guess, step), ulp(guess, -step)):
            if M.ee_sub(h, c) == target:
                return c
    return None


def solve_sub(y, target):
    """A value h with ee_sub(h, y) == target, searched near y + target."""
    guess = fp32(b2f(y) + b2f(target))
    for step in range(0, 200):
        for h in (ulp(guess, step), ulp(guess, -step)):
            if M.ee_sub(h, y) == target:
                return h
    return guess


def around(rng, value):
    """value, or one ulp either side."""
    return ulp(value, rng.choice([-1, 0, 0, 1]))


def effect_list(rng, count, fields, rate=0.3):
    """`count` scripted rewrites, each None or a list of (offset, size, raw):
    one field, or (a third of the time) every field drawn with p = 1/2. In
    a third of the cases a single call k (k < 8) rewrites every field and no
    other call rewrites anything, so a store placed on the wrong side of
    call k is caught (a later rewrite cannot mask it)."""
    if rng.random() < 0.33:
        k = rng.choice([0, 0, 0, 1, 1, 2, 3, 4, 5, 6, 7])
        return [tuple((offset, size, make(rng)) for offset, size, make in fields) if i == k else None
                for i in range(count)]
    out = []
    for _ in range(count):
        if rng.random() >= rate:
            out.append(None)
        elif rng.random() < 0.33:
            chosen = [f for f in fields if rng.random() < 0.5] or [rng.choice(fields)]
            out.append(tuple((offset, size, make(rng)) for offset, size, make in chosen))
        else:
            offset, size, make = rng.choice(fields)
            out.append(((offset, size, make(rng)),))
    return out


def float_field(lo, hi, specials=()):
    return lambda rng: rng.choice(specials) if specials and rng.random() < 0.2 else rf(rng, lo, hi)


def byte_field(values):
    return lambda rng: rng.choice(values)


def yaw_matrix(yaw, position=(0.0, 0.0, 0.0)):
    c, s = math.cos(yaw), math.sin(yaw)
    rows = ((c, 0.0, -s, 0.0), (0.0, 1.0, 0.0, 0.0), (s, 0.0, c, 0.0), position + (1.0,))
    return [fp32(v) for row in rows for v in row]


def random_matrix(rng):
    if rng.random() < 0.6:
        m = yaw_matrix(rng.uniform(-math.pi, math.pi),
                       (rng.uniform(-500, 500), rng.uniform(0, 300), rng.uniform(-500, 500)))
    else:
        m = [rf(rng, -2, 2) for _ in range(12)] + [rf(rng, -600, 600) for _ in range(3)] + [fp32(1.0)]
    for _ in range(rng.choice([0, 0, 1, 2])):
        m[rng.randrange(16)] = rng.choice(SPECIALS)
    return m


def speed_value(rng):
    four_half = f2b(4.5)
    return rng.choice([four_half, ulp(four_half, -1), four_half | 0x80000000, ulp(four_half, -1) | 0x80000000,
                       0, 0x80000000, 1, f2b(4.0), f2b(8.0), f2b(-8.0), f2b(12.0), f2b(-16.0),
                       rf(rng, -4.5, 4.5), rf(rng, -4.5, 4.5), rf(rng, -40, 40), rf(rng, -40, 40)])


TRANSLATE_EFFECTS = [
    (0xB0, 4, float_field(-600, 600)), (0xB8, 4, float_field(-600, 600)),
    (0x38, 4, float_field(-4.4, 4.4, SPECIALS)), (0xC4, 4, float_field(-3.2, 3.2)),
    (0x25F, 1, byte_field([0, 1])), (0x9C, 4, float_field(-1.2, 1.2))]
REACT_A_EFFECTS = [
    (7, 1, byte_field([0, 1, 5])), (0x22C, 4, byte_field([0, 0x80000000, f2b(1.0)])),
    (0x224, 4, byte_field([0, f2b(-2.0)])), (0x25C, 1, byte_field([0, 3])),
    (0x200, 4, lambda rng: rng.getrandbits(32)), (0x20E, 2, lambda rng: rng.getrandbits(16))]
REACT_B_EFFECTS = [
    (7, 1, byte_field([0, 1, 0x1D, 0xFF])), (0xF, 1, byte_field([0x63, 0])),
    (0x220, 4, byte_field([0, f2b(1.0)])), (0x228, 4, byte_field([0, f2b(150.0)])),
    (0x234, 1, byte_field([0, 1])), (0x20E, 2, lambda rng: rng.getrandbits(16)),
    (0x3C, 4, byte_field([0, f2b(40.0)])), (6, 1, byte_field([0, 7])), (4, 1, byte_field([1, 9])),
    (5, 1, byte_field([0, 9])), (0x1F0, 1, byte_field([0, 0x11])), (0x224, 4, byte_field([0, f2b(1.0)])),
    (0x22C, 4, byte_field([0, f2b(1.0)])), (0xF, 1, byte_field([2, 0x40]))]
HANG_EFFECTS = [
    (6, 1, byte_field([0, 3, 0xB])), (0x200, 4, lambda r: r.getrandbits(32)),
    (0x23F, 1, byte_field([0, 3])), (0xB4, 4, float_field(-300, 300)),
    (0x235, 1, byte_field([0, 0x80])), (0x25F, 1, byte_field([0, 1])), (0x25C, 1, byte_field([1, 2])),
    (5, 1, byte_field([4, 8])), (0x1F0, 1, byte_field([9, 0x20])), (0xD, 1, byte_field([0, 5])),
    (0x2E, 2, lambda rng: rng.getrandbits(16)), (0x236, 1, byte_field([0, 3])),
    (0x2F4, 4, float_field(-300, 300)), (0x2EC, 4, float_field(-3, 3)), (0xB0, 4, float_field(-300, 300)),
    (0xB8, 4, float_field(-300, 300)), (0xC4, 4, float_field(-3, 3)), (0x1F1, 1, byte_field([0, 1])),
    (0x38, 4, float_field(-4, 4))]


def gen_translate(rng, record, scene, script):
    put(record, 0x25F, 1, rng.choice([0, 0, 0, 1, 2]))
    put(record, 0x23B, 1, rng.choice([0x35, 0x35, 0, 0x5A]))
    if rng.random() < 0.15:
        put(record, 0xC4, 4, 0)
        put(record, 0x310, 4, rng.choice([HALF_PI, ulp(HALF_PI, -1), HALF_PI | 0x80000000]))
    else:
        put(record, 0xC4, 4, rf(rng, -3.3, 3.3))
        put(record, 0x310, 4, rf(rng, -3.3, 3.3))
    put(record, 0x9C, 4, rng.choice([0, rf(rng, -1.3, 1.3), rf(rng, -1.3, 1.3)]))
    put(record, 0x38, 4, speed_value(rng))
    put(record, 0xB0, 4, rf(rng, -600, 600))
    put(record, 0xB8, 4, rf(rng, -600, 600))
    script.effects = effect_list(rng, 64, TRANSLATE_EFFECTS)
    return (rng.choice([0, 1]),)


def gen_strafe(rng, record, scene, script):
    put(record, 0x24C, 4, rng.choice([2, 3, 2, 3, 0, 1, 0xFFFFFFFF, rng.getrandbits(32)]))
    put(record, 0x23F, 1, rng.randrange(4))
    put(record, 0x38, 4, rng.choice([rf(rng, -5, 5), rf(rng, -5, 5)] + list(SPECIALS)))
    for i, word in enumerate(random_matrix(rng)):
        put(record, 0xD0 + 4 * i, 4, word)
    put(record, 0xB0, 4, rf(rng, -600, 600))
    put(record, 0xB8, 4, rf(rng, -600, 600))
    return ()


def gen_quadrant(rng, record, scene, script):
    scene.spad3B8D = rng.choice([0, 0, 0, 0, 1])
    scene.pad_gait = rng.choice([0, 1, 2, 3, 3])
    scene.pad_x = rng.choice([0, 128, 255, rng.randrange(256), rng.randrange(256)])
    scene.pad_y = rng.choice([0, 128, 255, rng.randrange(256), rng.randrange(256)])
    put(record, 0x23F, 1, rng.randrange(4))
    put(record, 0x24C, 4, rng.getrandbits(32))
    if rng.random() < 0.3:
        # The |r| == pi/4 and 3pi/4 boundaries (and one ulp either side): a
        # scripted atan2 result puts the heading at h (camera 0), and +C4 is
        # chosen so that h - +C4 is the boundary exactly.
        scene.camera_yaw = 0.0
        scene.spad3B8D, scene.pad_gait = 0, rng.choice([1, 2, 3])
        h = f2b(round(rng.uniform(0.8, 0.95) * 2 ** 22) / 2 ** 22)
        target = rng.choice([QUARTER_PI, THREE_QUARTER_PI]) | rng.choice([0, 0x80000000])
        target = ulp(target, rng.choice([-1, 0, 0, 1]))
        r, c = solve_add(PI, h), solve_minuend_sub(h, target)
        if r is not None and c is not None:
            script.atans = [r]
            put(record, 0xC4, 4, c)
    else:
        scene.camera_yaw = b2f(rf(rng, -math.pi, math.pi))
        put(record, 0xC4, 4, rf(rng, -3.3, 3.3))
    return ()


def gen_react_a(rng, record, scene, script):
    put(record, 7, 1, rng.choice([0, 0, 0, 1, 1, 2, 0xFF]))
    put(record, 0x200, 4, rng.getrandbits(32) & ~0x1000 | rng.choice([0, 0x1000]))
    put(record, 0x25C, 1, rng.choice([0, 1, 2, 3]))
    for offset in (0x224, 0x22C):
        put(record, offset, 4, rng.choice([0, 0, 0x80000000, 1, rf(rng, -5, 5)]))
    script.frames = [rng.choice([0, 1, 30, 71, 255, -1, 1000, 0x7FFFFFFF])]
    script.effects = effect_list(rng, 8, REACT_A_EFFECTS)
    return ()


def gen_react_b(rng, record, scene, script):
    put(record, 7, 1, rng.choice([0, 0, 0, 1, 1, 2, 0xA, 0xB, 0x14, 0x15, 0x16, 0x17, 0x18, 0x1E, 3, 0x20]))
    for offset in (0x224, 0x22C):
        put(record, offset, 4, rng.choice([0, 0, 0x80000000, 1, rf(rng, -5, 5)]))
    put(record, 0x220, 4, rng.choice([0, 0x80000000, 1, 0x80000001, rf(rng, -5, 5)]))
    hundred = f2b(100.0)
    put(record, 0x228, 4, rng.choice([hundred, ulp(hundred, -1), ulp(hundred, 1), rf(rng, 0, 200)]))
    thirty_two = f2b(32.0)
    put(record, 0x3C, 4, rng.choice([thirty_two, ulp(thirty_two, 1), ulp(thirty_two, -1), rf(rng, 0, 60)]))
    put(record, 0xF, 1, rng.choice([0x63, 2, 3, 0, rng.randrange(256)]))
    put(record, 0x234, 1, rng.choice([0, 1, 2]))
    put(record, 0x200, 4, rng.getrandbits(32) & ~0x9000 | rng.choice([0, 0x1000, 0x8000, 0x9000]))
    scene.d8106F1 = rng.choice([0, 1])
    script.randoms = [rng.getrandbits(32)]
    script.results = {'r190': [rng.choice([0, 1, 2, -1])]}
    script.effects = effect_list(rng, 8, REACT_B_EFFECTS)
    return ()


LEDGE_EFFECTS = [
    (0xB0, 4, float_field(-500, 500)), (0xB4, 4, float_field(150, 260)), (0xB8, 4, float_field(-500, 500)),
    (0xC4, 4, float_field(-3.1, 3.1)), (0x220, 4, float_field(-1, 5)), (0x228, 4, float_field(0, 200)),
    (0x2E0, 4, float_field(-9, 9)), (0x2E4, 4, float_field(-9, 9)), (0x2E8, 4, float_field(-9, 9)),
    (0x254, 4, float_field(-9, 9)), (0x218, 4, float_field(-3, 3)), (0x25F, 1, byte_field([0, 2])),
    (5, 1, byte_field([5, 6])), (6, 1, byte_field([1, 2])), (0x1F0, 1, byte_field([0xB, 0xC])),
    (0x1F1, 1, byte_field([2, 3])), (0x290, 4, float_field(-9, 9)), (0x294, 4, float_field(-9, 9)),
    (0x298, 4, float_field(-9, 9)), (0xD0, 4, float_field(-2, 2)), (0xFC, 4, float_field(-500, 500))]
EXTREMES = (f2b(1e20), f2b(-1e20), 0x7F7FFFFF, 0xFF7FFFFF, f2b(3e19))


def unit_xz(rng):
    angle = rng.uniform(-math.pi, math.pi)
    return fp32(math.cos(angle)), fp32(rng.choice([0.0, rng.uniform(-0.2, 0.2)])), fp32(math.sin(angle))


def hit_value(rng, near):
    if rng.random() < 0.03:
        return {'point': tuple(rng.choice(EXTREMES) for _ in range(3)), 'normal': unit_xz(rng),
                'node': 0x2000, 'owner': None}
    return {'point': tuple(fp32(b2f(near[i]) + rng.uniform(-6, 6)) for i in range(3)),
            'normal': unit_xz(rng),
            'node': rng.choice([0x2000, 0x2000, 0x2005, 0x2046, 0x1000, 0x2032, 0x4000, 0xE046, 0x20FF]),
            'owner': rng.choice([None, None, None, (rng.randrange(8), rng.choice([4, 0x24, 0xE4, 5, 0x14]),
                                                    rng.choice([2, 2, 3]))])}


def ledge_value(rng, record, facing=None):
    heading = facing if facing is not None else rf(rng, -math.pi, math.pi)
    position = [get(record, 0xB0 + 4 * i) for i in range(3)]
    if rng.random() < 0.03:
        return {'point': tuple(rng.choice(EXTREMES) for _ in range(3)),
                'normal': tuple(rng.choice(EXTREMES + (0, f2b(1.0))) for _ in range(3)), 'heading': heading,
                'matrix': random_matrix(rng)}
    return {'point': tuple(fp32(b2f(position[i]) + rng.uniform(-6, 6)) for i in range(3)),
            'normal': unit_xz(rng), 'heading': heading,
            'matrix': yaw_matrix(b2f(heading)) if rng.random() < 0.8 else random_matrix(rng)}


def aux_value(rng):
    return rng.choice([fp32(0.3), 0x3F20D97C, ulp(0x3F20D97C, -1), fp32(1.0), rf(rng, 0, 0.62)])


def table_value(rng, heights):
    count = rng.choice([0, 1, 2, 3, 3, 4, 6, 16])
    return {'count': count,
            'flags': [rng.choice([1, 1, 1, 0x8001, 0, 2]) for _ in range(count)],
            'height': [rng.choice(heights)(rng) for _ in range(count)],
            'aux': [aux_value(rng) for _ in range(count)]}


def ledge_record(rng, record, scene, lean):
    """The gates both scans share. `lean` makes every gate pass (the scripts
    then reach the loop bodies and the success writes); otherwise each gate
    is drawn with its boundary values."""
    hundred = f2b(100.0)
    if lean:
        put(record, 0x220, 4, rf(rng, 0.1, 10))
        put(record, 0x228, 4, rf(rng, 0, 99))
    else:
        put(record, 0x220, 4, rng.choice([rf(rng, 0.1, 10), 0, 0x80000000, 1, f2b(-1.0)]))
        put(record, 0x228, 4, rng.choice([rf(rng, 0, 99), hundred, ulp(hundred, -1), rf(rng, 100, 200)]))
    scene.d8106F1 = rng.choice([0, 0, 1])
    scene.area = rng.choice([0x11, 0x11, 0, 0x12])
    put(record, 0xC4, 4, rf(rng, -math.pi, math.pi))
    if scene.area == 0x11 and rng.random() < 0.7:
        # around (340, 270): inside and outside the 115 radius
        radius, angle = rng.choice([rng.uniform(0, 114), rng.uniform(116, 200), 115.0]), rng.uniform(0, 2 * math.pi)
        put(record, 0xB0, 4, fp32(340 + radius * math.cos(angle)))
        put(record, 0xB8, 4, fp32(270 + radius * math.sin(angle)))
    else:
        put(record, 0xB0, 4, rf(rng, -500, 500))
        put(record, 0xB8, 4, rf(rng, -500, 500))
    put(record, 0xB4, 4, rf(rng, -2, 2) if rng.random() < 0.2 else rf(rng, 150, 260))
    for i, word in enumerate(random_matrix(rng)):
        put(record, 0xD0 + 4 * i, 4, word)


def sweep_results(rng, count, ones=0.2, values=(1, -1, 2)):
    return [rng.choice(values) if rng.random() < ones else 0 for _ in range(count)]


def lean_table(rng, count, height):
    return {'count': count, 'flags': [rng.choice([1, 1, 1, 0x8001, 0]) for _ in range(count)],
            'height': [height(rng) for _ in range(count)],
            'aux': [rng.choice([fp32(0.3), fp32(0.3), ulp(0x3F20D97C, -1), 0x3F20D97C]) for _ in range(count)]}


def gen_catch(rng, record, scene, script):
    lean, boundary = rng.random() < 0.5, False
    ledge_record(rng, record, scene, lean)
    y = get(record, 0xB4)
    position = [get(record, 0xB0 + 4 * i) for i in range(3)]
    yaw = b2f(get(record, 0xC4))
    facing = fp32(math.remainder(yaw + math.pi + rng.uniform(-1.4, 1.4), 2 * math.pi))
    if rng.random() < 0.12:
        # the 0.6 pi facing boundary: yaw 0, heading +-0x3FF1463A (or one ulp off)
        put(record, 0xC4, 4, 0)
        facing = around(rng, 0x3FF1463A) | rng.choice([0, 0x80000000])
    first = ledge_value(rng, record, facing if lean or rng.random() < 0.8 else None)
    turn = rng.choice([0.0, rng.uniform(-0.3, 0.3), rng.uniform(-0.3, 0.3), rng.choice([-1, 1]) * rng.uniform(0.36, 0.6)]) \
        if lean else rng.choice([0.0, rng.uniform(-0.5, 0.5)])
    second = ledge_value(rng, record, fp32(b2f(first['heading']) + turn) if lean or rng.random() < 0.85 else None)
    lean_kind2 = False
    if rng.random() < 0.15:
        # the 0.349 heading-change boundary: sub(first, second) == +-K_PI_9 (or one ulp off)
        target = around(rng, 0x3EB2B8C3) | rng.choice([0, 0x80000000])
        found = solve_minuend_sub(first['heading'], target)
        if found is not None:
            second['heading'] = found
            lean_kind2 = True
    script.ledges = [first, second]
    hits = [hit_value(rng, position), hit_value(rng, position)]
    below = lambda r: rng.choice([fp32(b2f(y) - r.uniform(21, 40)), ulp(M.ee_sub(y, f2b(20.5)), -1),
                                  M.ee_sub(y, f2b(20.5)), fp32(b2f(y) + r.uniform(-5, 5))])
    near = lambda r: rng.choice([fp32(b2f(y) + r.uniform(-2.7, 2.7)), fp32(b2f(y) + r.uniform(-2.7, 2.7)),
                                 fp32(b2f(y) + r.uniform(-6, 6)), around(r, solve_sub(y, 0x40333333)),
                                 around(r, solve_sub(y, 0xC0333333))])
    if lean:
        script.kinds = [rng.choice([2, 4, 6]), 2 if lean_kind2 else rng.choice([0, 0, 2])]
        hits[0]['node'] = rng.choice([0x2000, 0x2005, 0x2046])
        hits[0]['owner'] = rng.choice([None, (1, 4, 2), (2, 0x24, 2), (3, 5, 2)])
        first_table = lean_table(rng, rng.choice([0, 1, 2, 3]),
                                 lambda r: rng.choice([fp32(b2f(y) - r.uniform(21, 40)), ulp(M.ee_sub(y, f2b(20.5)), -1)]))
        second_table = lean_table(rng, rng.choice([1, 2, 3, 5]), near)
        if rng.random() < 0.2:
            # one entry exactly at |h - y| == 2.8 (or one ulp off), all else open
            y = rf(rng, -2, 2)
            put(record, 0xB4, 4, y)
            first_table = lean_table(rng, 1, lambda r: fp32(b2f(y) - 30))
            first_table['flags'] = [1]
            second_table = {'count': 1, 'flags': [1], 'aux': [fp32(0.3)],
                            'height': [around(rng, solve_sub(y, rng.choice([0x40333333, 0xC0333333])))]}
            boundary = True
        script.tables = [first_table, second_table]
        script.attributes = [rng.choice([0, 0, 0, 0x46, 0xFFFF8046]) for _ in range(20)]
        script.results = {'sides': sweep_results(rng, 20, 0.2), 'hands': sweep_results(rng, 20, 0.1),
                          'depth': sweep_results(rng, 20, 0.1), 'segment': sweep_results(rng, 20, 0.3)}
        if scene.area == 0x11 and rng.random() < 0.3:
            script.sqrts = [around(rng, f2b(115.0))]
    else:
        script.kinds = [rng.choice([2, 2, 4, 6, 0, 1]), rng.choice([0, 0, 2, 4, 1])]
        script.tables = [table_value(rng, [below, below, near]), table_value(rng, [near])]
        script.attributes = [rng.choice([0, 0, 0x46, 0x2046, 0xFFFF8046, 0x1005]) for _ in range(20)]
        script.results = {'sides': sweep_results(rng, 20, 0.25), 'hands': sweep_results(rng, 20, 0.1),
                          'depth': sweep_results(rng, 20, 0.1), 'segment': sweep_results(rng, 20, 0.4)}
    script.hits = hits
    script.effects = [] if boundary else effect_list(rng, 40, LEDGE_EFFECTS, 0.15)
    return ()


def gen_grab(rng, record, scene, script):
    lean = rng.random() < 0.5
    ledge_record(rng, record, scene, lean)
    reach = rng.choice([f2b(-1.0), f2b(-3.0), rf(rng, -6, 0), rf(rng, -6, 0)] +
                       ([] if lean else [0, f2b(2.0)]))
    put(record, 0x2E4, 4, reach)
    y = get(record, 0xB4)
    position = [get(record, 0xB0 + 4 * i) for i in range(3)]
    top = M.ee_add(f2b(20.5), y)
    upper = M.ee_sub(top, reach)
    script.ledges = [ledge_value(rng, record)]
    hits = [hit_value(rng, position), hit_value(rng, position)]
    window = lambda r: rng.choice([top, upper, ulp(top, -1), ulp(upper, 1), fp32(r.uniform(b2f(top), b2f(upper))),
                                   fp32(b2f(top) + r.uniform(-3, 3))])
    close = lambda r: rng.choice([fp32(b2f(y) + r.uniform(-0.5, 0.5)), around(r, solve_sub(y, 0x3F000000)),
                                  around(r, solve_sub(y, 0xBF000000)), fp32(b2f(y) + r.uniform(-2, 2))])
    if lean:
        high = rng.random() < 0.5
        script.kinds = [2 if high else 0, rng.choice([0, 0, 2]), rng.choice([2, 4])]
        for hit in hits:
            hit['node'] = rng.choice([0x2000, 0x2005])
        # the second probe's point near the first (the 0.5 distance test)
        if high and rng.random() < 0.7:
            hits[1]['point'] = script.ledges[0]['point']
        inside = (lambda r: rng.choice([top, upper, fp32(r.uniform(b2f(top), b2f(upper)))])) if high else \
            (lambda r: rng.choice([fp32(b2f(y) + r.uniform(-0.5, 0.5)), around(r, solve_sub(y, 0x3F000000)),
                                   around(r, solve_sub(y, 0xBF000000))]))
        script.tables = [lean_table(rng, rng.choice([1, 2, 3, 4]), inside)]
        script.attributes = [rng.choice([0, 0, 0, 0x46]) for _ in range(20)]
        script.results = {'lip': sweep_results(rng, 20, 0.15), 'sides': sweep_results(rng, 20, 0.15),
                          'hands': sweep_results(rng, 20, 0.1), 'sweep': sweep_results(rng, 20, 0.2, (1, 2, 4))}
    else:
        script.kinds = [rng.choice([2, 2, 0, 0, 1, 4]), rng.choice([0, 0, 2, 1]), rng.choice([0, 0, 2, 1])]
        script.tables = [table_value(rng, [window, close])]
        script.attributes = [rng.choice([0, 0, 0x46, 0x2046, 0xFFFF8046, 0x1005]) for _ in range(20)]
        script.results = {'lip': sweep_results(rng, 20, 0.25), 'sides': sweep_results(rng, 20, 0.2),
                          'hands': sweep_results(rng, 20, 0.15), 'sweep': sweep_results(rng, 20, 0.3, (1, 2, 4))}
    script.hits = hits
    if rng.random() < 0.25:
        # the 0.5 + near < far boundary of the two sqrt distances
        near = rf(rng, 0, 10)
        script.sqrts = [around(rng, M.ee_add(0x3F000000, near)), near]
    script.effects = effect_list(rng, 40, LEDGE_EFFECTS, 0.15)
    return (b2f(reach),)


def gen_hang(rng, record, scene, script):
    put(record, 6, 1, rng.choice([0, 0, 1, 1, 2, 2, 3, 0xA, 0xA, 0xB, 0xB, 0xC, 0xC, 4, 0xD]))
    put(record, 0x1F1, 1, rng.choice([0, 1, 1, 2]))
    put(record, 0x200, 4, rng.getrandbits(32) & ~0x9000 | rng.choice([0, 0x1000, 0x8000, 0x9000]))
    put(record, 0x23F, 1, rng.randrange(4))
    for offset in (0x2E0, 0x2E4, 0x2E8, 0x218, 0xB0, 0xB4, 0xB8):
        put(record, offset, 4, rf(rng, -300, 300))
    put(record, 0x25F, 1, rng.choice([0, 1]))
    put(record, 0x23B, 1, rng.choice([0x35, 0]))
    put(record, 0x38, 4, rng.choice([rf(rng, -4.4, 4.4), rf(rng, -12, 12)]))
    put(record, 0xC4, 4, rf(rng, -3.2, 3.2))
    put(record, 0x310, 4, rf(rng, -3.2, 3.2))
    put(record, 0x235, 1, rng.randrange(256))
    script.results = {name: [rng.choice([0, 0, 1, 5, -1])] for name in ('hang_clear', 'column', 'floor')}
    script.clips = [rng.choice([0x7B, 0x90, 0x113])]
    script.nodes = [tuple(rf(rng, -300, 300) for _ in range(3)) + (rng.choice([f2b(1.0), rf(rng, -2, 2)]),)]
    script.effects = effect_list(rng, 40, HANG_EFFECTS)
    return ()


# routine: (entry, generator, native function, kind of call, size of the routine)
ROUTINES = {
    'translate': (TRANSLATE, gen_translate, 'em_player_recovery_translate', 'probe', 0x330),
    'strafe': (STRAFE, gen_strafe, 'em_player_recovery_strafe', 'bare', 0x144),
    'quadrant': (QUADRANT, gen_quadrant, 'em_player_recovery_stick_quadrant', 'scene', 0x1E4),
    'react_a': (REACT_A, gen_react_a, 'em_player_recovery_react_002243F0', 'scratch_result', 0x204),
    'react_b': (REACT_B, gen_react_b, 'em_player_recovery_react_00224B80', 'scene_result', 0x458),
    'catch': (CATCH, gen_catch, 'em_player_recovery_ledge_catch', 'full_result', 0x780),
    'grab': (GRAB, gen_grab, 'em_player_recovery_ledge_grab', 'reach_result', 0x7DC),
    'hang': (HANG, gen_hang, 'em_player_recovery_hang_entry', 'workers', 0x36C),
}
# Workers each routine may reach (their absence must fault before any write).
REQUIRED = {
    'translate': ['sine', 'cosine', 'probes'],
    'strafe': [],
    'quadrant': ['cosine', 'atan2'],
    'react_a': ['sound', 'react_0021C350', 'react_0021C270', 'shake', 'request', 'clip_frames', 'arbiter'],
    'react_b': ['shake', 'sound', 'react_0021C350', 'react_0021C270', 'react_0021C120', 'react_0021C190',
                'react_0021D490', 'random', 'request'],
    'catch': ['move', 'ledge', 'sqrt', 'table', 'attribute', 'sides', 'hands', 'depth', 'segment'],
    'grab': ['move', 'ledge', 'sqrt', 'table', 'attribute', 'sides', 'hands', 'lip', 'sweep'],
    'hang': ['sine', 'cosine', 'probes', 'request', 'hang_clear', 'sound', 'hang_row', 'skeleton', 'column',
             'heading', 'reentry', 'handoff', 'floor', 'fall'],
}
LOG_WORKER = {'sin': 'sine', 'cos': 'cosine', 'r350': 'react_0021C350', 'r270': 'react_0021C270',
              'r120': 'react_0021C120', 'r190': 'react_0021C190', 'r490': 'react_0021D490', 'rand': 'random',
              'frames': 'clip_frames', 'attr': 'attribute'}


def make_case(routine, seed):
    rng = random.Random(seed)
    record = bytearray(BASE)
    scene = Scene()
    scene.camera_yaw = b2f(rf(rng, -math.pi, math.pi))
    scratch = [rng.choice([rf(rng, -9, 9), 0x7F7FFFFF, 0]) for _ in range(4)]
    script = Script()
    args = ROUTINES[routine][1](rng, record, scene, script)
    return record, scene, scratch, script, args


def call_native(routine, native, scene, scratch_words, args):
    fn = getattr(NATIVE, ROUTINES[routine][2])
    kind = ROUTINES[routine][3]
    scratch = Scratch()
    for i in range(4):
        C.c_uint32.from_address(C.addressof(scratch) + 4 * i).value = scratch_words[i]
    out = C.c_int(-99)
    actor, w = C.byref(native.actor), C.byref(native.workers)
    if kind == 'probe': status = fn(actor, args[0], w)
    elif kind == 'bare': status = fn(actor)
    elif kind == 'scene': status = fn(actor, C.byref(scene), C.byref(scratch), w)
    elif kind == 'scratch_result': status = fn(actor, C.byref(scratch), w, C.byref(out))
    elif kind == 'scene_result': status = fn(actor, C.byref(scene), w, C.byref(out))
    elif kind == 'full_result': status = fn(actor, C.byref(scene), C.byref(scratch), w, C.byref(out))
    elif kind == 'reach_result': status = fn(actor, args[0], C.byref(scene), C.byref(scratch), w, C.byref(out))
    else: status = fn(actor, w)
    words = tuple(C.c_uint32.from_address(C.addressof(scratch) + 4 * i).value for i in range(4))
    return status, out.value, words


def run_oracle(routine, record, scene, scratch, script, args):
    entry, size = ROUTINES[routine][0], ROUTINES[routine][4]
    oracle = Oracle(record, scene, scratch, script)
    oracle.ee.cover, oracle.ee.cover_range = set(), (entry, entry + size)
    kind = ROUTINES[routine][3]
    ints = (ACTOR, args[0]) if kind == 'probe' else (ACTOR,)
    floats = (args[0],) if kind == 'reach_result' else ()
    oracle.ee.call(entry, ints, floats)
    return oracle


def run_case(case):
    routine, seed = case
    return compare_case(routine, seed, *make_case(routine, seed))


def compare_case(routine, seed, record, scene, scratch, script, args):
    oracle = run_oracle(routine, record, scene, scratch, script.copy(), args)
    native = Native(record, script.copy())
    status, result, words = call_native(routine, native, scene, scratch, args)
    where = (routine, seed)
    assert status == 0, (where, 'native fault', status)
    if ROUTINES[routine][3].endswith('result'):
        assert result == s32(oracle.ee.r[2]), (where, 'result', result, s32(oracle.ee.r[2]))
    assert oracle.log == native.log, (where, 'calls', oracle.log, native.log)
    want, have = oracle.record(), native.record()
    if want != have:
        diff = [hex(k) for k in range(0x320) if want[k] != have[k]]
        raise AssertionError((where, 'record bytes differ at', diff[:24]))
    assert oracle.scratch() == words, (where, 'scratch', [hex(v) for v in oracle.scratch()], [hex(v) for v in words])
    names = {LOG_WORKER.get(entry[0], entry[0]) for entry in oracle.log}
    missing = names - set(REQUIRED[routine])
    assert not missing, (where, 'worker reached but not required', missing)
    # the path: the dispatch byte (+7 / +6) and each call with its small int
    # argument (a clip, a sub-mode), so the call-order sweep visits every
    # distinct call site
    state = record[{'react_a': 7, 'react_b': 7, 'hang': 6}.get(routine, 4)]
    signature = (state,) + tuple(entry[:2] if len(entry) > 1 and isinstance(entry[1], int) and
                                 entry[1] < 0x10000 else entry[:1] for entry in oracle.log)
    return routine, s32(oracle.ee.r[2]), frozenset(oracle.ee.cover), (signature, seed, oracle.script.effect_calls)


EFFECT_FIELDS = {'translate': TRANSLATE_EFFECTS, 'react_a': REACT_A_EFFECTS, 'react_b': REACT_B_EFFECTS,
                 'hang': HANG_EFFECTS, 'catch': LEDGE_EFFECTS, 'grab': LEDGE_EFFECTS}


SENTINEL = {1: 0xA5, 2: 0xA5A5, 4: 0xC49A5000}   # a byte, a halfword, -1234.5


def order_case(case):
    """Call-site order: the case of one observed callee sequence, with call
    k (and only call k) rewriting every field the routine reads or writes,
    once with values no routine stores (SENTINEL) and once with drawn ones.
    A store the translation puts on the wrong side of that call shows."""
    routine, seed, k = case
    fields = EFFECT_FIELDS[routine]
    rng = random.Random(seed * 131 + k)
    for rewrite in (tuple((offset, size, SENTINEL[size]) for offset, size, _ in fields),
                    tuple((offset, size, make(rng)) for offset, size, make in fields)):
        record, scene, scratch, script, args = make_case(routine, seed)
        script.effects = [None] * k + [rewrite]
        compare_case(routine, seed, record, scene, scratch, script, args)
    return 2


# ---- fail-stop -------------------------------------------------------------------
class FailingNative(Native):
    """The k-th record-or-result worker call fails (returns -1) without
    applying its effect."""

    def __init__(self, record, script, fail_at):
        self.calls, self.fail_at = 0, fail_at
        super().__init__(record, script)
        for name, proto in WORKER_FIELDS:
            if name in ('sine', 'cosine', 'atan2', 'sqrt'):
                continue
            original = self.keep[name]
            self.keep[name] = proto((lambda f: lambda *a: self.gate(f, *a))(original))
            setattr(self.workers, name, self.keep[name])

    def gate(self, fn, *args):
        self.calls += 1
        if self.calls == self.fail_at:
            return -1
        return fn(*args)


class SnapshotOracle(Oracle):
    """Snapshots the record and scratch words at every non-math worker call."""

    def __init__(self, *a):
        super().__init__(*a)
        self.snapshots = []
        for address, hook in list(self.ee.hooks.items()):
            if address in (SINE, COSINE, ATAN2, SQRT):
                continue
            self.ee.hooks[address] = (lambda h: lambda e: (self.snapshots.append((self.record(), self.scratch())),
                                                           h(e)))(hook)


def fail_case(case):
    """A worker that fails at call k: the native routine returns -1 at once,
    with exactly the record bytes and scratch words the original had when it
    made that call; a missing worker faults before any write."""
    routine, seed = case
    record, scene, scratch, script, args = make_case(routine, seed)
    entry, size = ROUTINES[routine][0], ROUTINES[routine][4]
    oracle = SnapshotOracle(record, scene, scratch, script.copy())
    kind = ROUTINES[routine][3]
    oracle.ee.call(entry, (ACTOR, args[0]) if kind == 'probe' else (ACTOR,),
                   (args[0],) if kind == 'reach_result' else ())
    checked = 0
    for k in range(1, len(oracle.snapshots) + 1):
        native = FailingNative(record, script.copy(), k)
        status, _, words = call_native(routine, native, scene, scratch, args)
        assert status == -1, (routine, seed, k, 'no fault', status)
        want_record, want_scratch = oracle.snapshots[k - 1]
        assert native.record() == want_record, (routine, seed, k, 'record at the failing call')
        assert words == want_scratch, (routine, seed, k, 'scratch at the failing call')
        checked += 1
    for name in REQUIRED[routine]:
        native = Native(record, script.copy())
        setattr(native.workers, name, WORKER_FIELDS[[n for n, _ in WORKER_FIELDS].index(name)][1]())
        status, _, words = call_native(routine, native, scene, scratch, args)
        assert status == -1 and native.log == [] and native.record() == bytes(record) and \
            words == tuple(scratch), (routine, seed, name, 'missing worker did not fault cleanly')
        checked += 1
    return checked


def strafe_gait_fault():
    record = bytearray(BASE)
    put(record, 0x24C, 4, 2)
    put(record, 0x23F, 1, 4)
    native = Native(record, Script())
    assert NATIVE.em_player_recovery_strafe(C.byref(native.actor)) == -1
    assert native.record() == bytes(record)


# ---- the binding adapters -----------------------------------------------------------
def adapter_checks(rng, count):
    """em_player_recovery_slide_translate / _slide_damage /
    _climb_translate / _state4 equal the direct routine on the record."""
    offsets = LANE / 'offsets'
    if not offsets.exists() or offsets.stat().st_mtime < (ROOT / 'src/game/em_player_climb.h').stat().st_mtime:
        source = LANE / 'offsets.c'
        source.write_text('#include <stddef.h>\n#include <stdio.h>\n#include "game/em_player_recovery.h"\n'
                          'int main(void){printf("%zu %zu %zu\\n", sizeof(EmPlayerSlideActor), '
                          'sizeof(EmPlayerClimbActor), offsetof(EmPlayerClimbActor, link_kind));return 0;}\n')
        subprocess.run(['cc', '-std=c11', '-Isrc', str(source), '-o', str(offsets)], cwd=ROOT, check=True)
    slide_size, climb_size, link_kind = map(int, subprocess.run(
        [str(offsets)], capture_output=True, text=True, check=True).stdout.split())
    done = 0
    for i in range(count):
        seed = 0xADA + i
        for which in ('slide_translate', 'slide_damage', 'climb_translate', 'state4'):
            routine = {'slide_translate': 'translate', 'climb_translate': 'translate',
                       'slide_damage': 'react_b', 'state4': 'hang'}[which]
            record, scene, scratch, script, args = make_case(routine, seed)
            direct = Native(record, script.copy())
            status, result, _ = call_native(routine, direct, scene, scratch, args)
            assert status == 0
            via = Native(record, script.copy())
            live = Live()
            live.workers = via.workers
            live.live = C.pointer(via.actor)
            scene_fn = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(Scene))(
                lambda _, out: (C.memmove(out, C.byref(scene), C.sizeof(Scene)), 0)[1])
            live.scene = scene_fn
            size = climb_size if which == 'climb_translate' else slide_size
            mirror = (C.c_uint8 * size)()
            if which == 'state4':
                assert NATIVE.em_player_recovery_state4(C.byref(live), C.byref(via.actor)) == 0
            else:
                getattr(NATIVE, 'em_player_%s_actor_from_live' % which.split('_')[0])(C.byref(via.actor), mirror)
                if which == 'climb_translate':
                    mirror[link_kind] = 2
                out = C.c_int(-99)
                if which == 'slide_damage':
                    assert NATIVE.em_player_recovery_slide_damage(C.byref(live), mirror, C.byref(out)) == 0
                    assert out.value == result, (which, seed, out.value, result)
                else:
                    fn = getattr(NATIVE, 'em_player_recovery_%s' % which)
                    assert fn(C.byref(live), mirror, args[0]) == 0
                expect = (C.c_uint8 * size)()
                getattr(NATIVE, 'em_player_%s_actor_from_live' % which.split('_')[0])(C.byref(direct.actor), expect)
                if which == 'climb_translate':
                    assert mirror[link_kind] == 2, 'link_kind not kept'
                    expect[link_kind] = 2
                assert bytes(mirror) == bytes(expect), (which, seed, 'mirror differs')
            assert via.log == direct.log, (which, seed, 'calls')
            assert via.record() == direct.record(), (which, seed, 'record')
            done += 1
        done += worker_adapter_checks(seed)
    return done


WORKER_ADAPTERS = {
    'translate': ('em_player_recovery_translate_worker', 'arg'),
    'strafe': ('em_player_recovery_strafe_worker', 'none'),
    'quadrant': ('em_player_recovery_stick_quadrant_worker', 'none'),
    'react_a': ('em_player_recovery_react_002243F0_worker', 'result'),
    'react_b': ('em_player_recovery_react_00224B80_worker', 'result'),
    'catch': ('em_player_recovery_ledge_catch_worker', 'result'),
    'grab': ('em_player_recovery_ledge_grab_worker', 'reach'),
}


def worker_adapter_checks(seed):
    """The worker-shaped adapters equal the direct routine: record, calls,
    result and scratch words, with 0x70003A20 kept in a shared word."""
    done = 0
    for routine, (name, shape) in WORKER_ADAPTERS.items():
        record, scene, scratch, script, args = make_case(routine, seed)
        direct = Native(record, script.copy())
        status, result, words = call_native(routine, direct, scene, scratch, args)
        assert status == 0
        via = Native(record, script.copy())
        live = Live()
        live.workers = via.workers
        scene_fn = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(Scene))(
            lambda _, out: (C.memmove(out, C.byref(scene), C.sizeof(Scene)), 0)[1])
        live.scene = scene_fn
        base = C.addressof(live.scratch)
        for i in range(4):
            C.c_uint32.from_address(base + 4 * i).value = scratch[i]
        shared = C.c_uint32(scratch[0])
        C.c_uint32.from_address(base).value = 0xDEADBEEF        # must be loaded from `shared`
        live.shared3A20 = C.pointer(shared)
        fn, out = getattr(NATIVE, name), C.c_int(-99)
        if shape == 'arg': got = fn(C.byref(live), C.byref(via.actor), args[0])
        elif shape == 'none': got = fn(C.byref(live), C.byref(via.actor))
        elif shape == 'result': got = fn(C.byref(live), C.byref(via.actor), C.byref(out))
        else: got = fn(C.byref(live), C.byref(via.actor), f2b(args[0]), C.byref(out))
        assert got == 0, (name, seed, got)
        if shape in ('result', 'reach'):
            assert out.value == result, (name, seed, out.value, result)
        assert via.log == direct.log, (name, seed, 'calls')
        assert via.record() == direct.record(), (name, seed, 'record')
        if routine not in ('translate', 'strafe', 'react_b'):
            have = (shared.value,) + tuple(C.c_uint32.from_address(base + 4 * i).value for i in range(1, 4))
            assert have == words, (name, seed, 'scratch', have, words)
        done += 1
    # no scene provider: the scene-reading adapters fault before any write
    record, scene, scratch, script, args = make_case('catch', seed)
    via = Native(record, script.copy())
    live = Live()
    live.workers = via.workers
    out = C.c_int(-99)
    assert NATIVE.em_player_recovery_ledge_catch_worker(C.byref(live), C.byref(via.actor), C.byref(out)) == -1
    assert via.log == [] and via.record() == bytes(record)
    return done + 1


# ---- the inline leaves, swept directly ----------------------------------------------
FLOAT_TO_INT, WRAP, EXTEND, COMPARE = 0x1281C0, 0x1B1470, 0x128350, 0x1000C0
LEAF_EDGES = (0, 0x80000000, 1, 0x807FFFFF, 0x00800000, 0x3F000000, 0x3F7FFFFF, 0x3F800000, 0xBF800000,
              0x4F000000, 0x4EFFFFFF, 0xCF000000, 0xCF000001, 0x4F800000, 0x7F7FFFFF, 0xFF7FFFFF,
              0x7F800000, 0xFF800000, 0x7FC00000, 0x7F800001, 0xFFC00000, 0x3FF1463A, 0x3FF14639,
              0x3FF1463B, 0xBFF1463A, 0x40490FDB, 0x40490FDC, 0x40490FDA, 0xC0490FDB, 0xC0490FDC,
              0xC0490FDA, 0x40C90FDB, 0xC0C90FDB, 0x41490FDB, 0xC1490FDB)


def leaf_sweep(chunk):
    """float_to_int, 001B1470 and 00128350 + 001000C0 against the native
    em_player_recovery_float_to_int / _wrap / _below_0_6pi."""
    seed, count = chunk
    rng = random.Random(seed)
    ee = ModelEE(ELF)
    inputs = list(LEAF_EDGES) + [rng.getrandbits(32) for _ in range(count)]
    for _ in range(count // 4):   # every exponent, including the float_to_int edges 2^30..2^32
        inputs.append(rng.randrange(256) << 23 | rng.getrandbits(23) | rng.choice([0, 0x80000000]))
    angles = [x for x in LEAF_EDGES if abs(b2f(x)) < 1e4] + \
        [f2b(rng.uniform(-60, 60)) for _ in range(count)] + [rng.getrandbits(32) & 0x807FFFFF | 0x3E000000
                                                               for _ in range(count // 4)]
    for x in inputs:
        ee.f[12] = x
        ee.r[31] = RETURN
        ee.run(FLOAT_TO_INT)
        got = NATIVE.em_player_recovery_float_to_int(x)
        assert got == s32(ee.r[2]), ('float_to_int', hex(x), got, s32(ee.r[2]))
        ee.f[12] = x
        ee.r[31] = RETURN
        ee.run(EXTEND)
        ee.r[4], ee.r[5] = ee.r[2], 0x3FFE28C740000000
        ee.r[31] = RETURN
        ee.run(COMPARE)
        got = NATIVE.em_player_recovery_below_0_6pi(x)
        assert got == s32(ee.r[2]), ('0.6pi compare', hex(x), got, s32(ee.r[2]))
    out = C.c_uint32()
    for x in angles:
        ee.f[12] = x
        ee.r[31] = RETURN
        ee.run(WRAP)
        assert NATIVE.em_player_recovery_wrap(x, C.byref(out)) == 0, ('wrap fault', hex(x))
        assert out.value == ee.f[0] & 0xFFFFFFFF, ('wrap', hex(x), hex(out.value), hex(ee.f[0]))
    # Where the original's loop can never end, the native faults instead of hanging.
    for x in (0x7F7FFFFF, 0xFF7FFFFF, 0x7F800000, 0x7FC00000):
        assert NATIVE.em_player_recovery_wrap(x, C.byref(out)) == -1, ('wrap fixed point', hex(x))
    return len(inputs) * 2 + len(angles)


# ======================================================================
# Main (unit oracle)
# ======================================================================
ELF = NATIVE = None


def coverage_report(routine, covered):
    entry, size = ROUTINES[routine][0], ROUTINES[routine][4]
    missing = [pc for pc in range(entry, entry + size, 4) if pc not in covered]
    return missing


def main():
    global ELF, NATIVE, BASE
    ELF = read_elf()
    NATIVE = build_native()
    BASE = base_record()
    counts = {}
    per_routine = {'translate': (6000, 260), 'strafe': (3000, 120), 'quadrant': (3000, 160),
                   'react_a': (3000, 160), 'react_b': (6000, 320), 'catch': (8000, 360),
                   'grab': (8000, 360), 'hang': (6000, 320)}
    cases = []
    for routine, (full, quick) in per_routine.items():
        salt = zlib.crc32(routine.encode()) % 9973
        cases += [(routine, 1000003 * i + salt) for i in range(reference_mode.pick(full, quick))]
    results = reference_mode.parallel_map(run_case, cases)
    covered = {r: set() for r in ROUTINES}
    outcomes = {r: set() for r in ROUTINES}
    paths = {}
    for routine, value, cover, (signature, seed, calls) in results:
        if routine in EFFECT_FIELDS and calls:
            paths.setdefault((routine, signature), (seed, calls))
    for routine, value, cover, _ in results:
        counts[routine] = counts.get(routine, 0) + 1
        covered[routine] |= cover
        outcomes[routine].add(value)
    uncovered = {r: coverage_report(r, covered[r]) for r in ROUTINES}
    for routine, missing in uncovered.items():
        allowed = DEAD.get(routine, set())
        extra = [hex(pc) for pc in missing if pc not in allowed]
        assert not extra, (routine, 'original instructions never executed', extra)
    for routine, want in (('react_a', {0, 1}), ('react_b', {0, 1, 2}), ('catch', {0, 1}), ('grab', {0, 1})):
        assert want <= outcomes[routine], (routine, 'return values reached', outcomes[routine])
    order = [(routine, seed, k) for (routine, _), (seed, calls) in sorted(paths.items(), key=str)
             for k in range(calls)]
    counts['call_order'] = sum(reference_mode.parallel_map(order_case, order))
    counts['paths'] = len(paths)
    fail_cases = [(routine, 77 + 1009 * i) for routine in ROUTINES
                  for i in range(reference_mode.pick(12, 2))]
    counts['fail_stop'] = sum(reference_mode.parallel_map(fail_case, fail_cases))
    strafe_gait_fault()
    chunks = [(0x1281C0 + i, 1000) for i in range(reference_mode.pick(200, 6))]
    counts['leaves'] = sum(reference_mode.parallel_map(leaf_sweep, chunks))
    counts['adapters'] = adapter_checks(random.Random(5), reference_mode.pick(40, 4))
    reference_mode.banner(*('%s %d' % (k, v) for k, v in counts.items()))
    dead = sum(len(v) for v in uncovered.values())
    print('player recovery reference: PASS (original 00178B90/00178EC0/001751A0/002243F0/00224B80/'
          '0017D080/0017C860/00162A40 instructions; every instruction executed except %d '
          'unreachable words, DEAD)' % dead)


# ======================================================================
# Route mode (EM_TEST_ROUTE=1): the PCSX2 route captures
# ======================================================================
PLAYER = shared.PLAYER
TRANSLATED = {TRANSLATE: 'translate', STRAFE: 'strafe', QUADRANT: 'quadrant', REACT_A: 'react_a',
              REACT_B: 'react_b', CATCH: 'catch', GRAB: 'grab', HANG: 'hang'}
VECTORS = (0x700038A0, 0x700038B0)


class ModelReplay(shared.RouteReplay):
    """shared.RouteReplay (the original player stage per frame, pad unpack
    and camera from the capture) on the model interpreter."""

    def __init__(self, elf, trace, ram, spad):
        self.ee = ee = ModelEE(elf, ram, spad)
        self.frame, self.events = 0, []
        for address, name in shared.SOUND_HOOKS.items():
            ee.hooks[address] = self.recorder(name)
        ee.hooks[shared.PAD_READ] = self.pad_read
        self.raw = bytes(8)
        self.rows = {r['counter']: r for r in trace['rows']}
        self.first = trace['first_counter']
        self.inputs = sorted(trace['inputs'], key=lambda i: i['f'])
        self.counter = ee.load(0x70003B64)


class Call:
    """One native routine in progress: its record and scratch words."""

    def __init__(self, ee, base):
        self.base = base
        self.actor = LiveActor()
        self.scratch = Scratch()
        C.memmove(self.actor.bytes, ee.read(base, 0x320), 0x320)
        self.words = (C.c_uint32 * 4).from_address(C.addressof(self.scratch))
        for i, address in enumerate(SCRATCH):
            self.words[i] = ee.load(address)


class RouteNative:
    """The native routines hooked in at their original addresses. Each
    worker runs the ORIGINAL callee in the same EE (nested), with the record
    and the scratch words written back before and read after, and its
    vectors placed where the original places them (0x700038A0/B0)."""

    def __init__(self, ee):
        self.ee, self.calls, self.counts, self.results = ee, [], {}, {}
        e = ee

        def cur():
            return self.calls[-1]

        def out_sync():
            c = cur()
            e.write(c.base, bytes(c.actor.bytes))
            for i, address in enumerate(SCRATCH):
                e.save(address, c.words[i])

        def in_sync():
            c = cur()
            C.memmove(c.actor.bytes, e.read(c.base, 0x320), 0x320)
            for i, address in enumerate(SCRATCH):
                c.words[i] = e.load(address)

        def around(entry, *args, floats=()):
            out_sync()
            r = e.nested(entry, args, floats)
            in_sync()
            return r

        def base():
            return cur().base

        def vector(address, pointer):
            for i, word in enumerate(fvec(pointer, 4)):
                e.save(address + 4 * i, word)
            return address

        def math1(entry):
            return MATH1(lambda _, x: b2f(e.nested(entry, (), (x,))[1]))

        def plain(entry, *extra):
            return ACT(lambda _, a: (around(entry, base(), *extra), 0)[1])

        def result(entry):
            def fn(_, a, out):
                out[0] = s32(around(entry, base())[0])
                return 0
            return ACT_R(fn)

        def hit_from_spad(hit):
            h = hit[0]
            h.kind = 0
            words = (C.c_uint32 * 3).from_address(C.addressof(h) + ProbeHit.point.offset)
            for i in range(3):
                words[i] = e.load(0x700031B0 + 4 * i)
            record = e.load(0x700031D0)
            if record:
                h.node = e.load(record + 0x1A, 2)
                words = (C.c_uint32 * 3).from_address(C.addressof(h) + ProbeHit.normal.offset)
                for i in range(3):
                    words[i] = e.load(record + 0x24 + 4 * i)
            owner = e.load(0x700031D4)
            h.owner = owner or None
            h.entity = int(owner != 0)
            if owner:
                h.entity_flags, h.entity_type = e.load(owner + 2, 1), e.load(owner + 3, 1)

        def move(_, a, target, mask, hit):
            r = around(MOVE, base(), vector(VECTORS[1], target), mask)[0]
            hit_from_spad(hit)
            return s32(r)

        def sweep(_, a, start, end, mask, hit):
            r = around(SWEEP, base(), vector(VECTORS[0], start), vector(VECTORS[1], end), mask)[0]
            hit_from_spad(hit)
            return s32(r)

        def segment(_, start, end, mask, id, out):
            out[0] = s32(around(SEGMENT, vector(VECTORS[0], start), vector(VECTORS[1], end), mask, id)[0])
            return 0

        def table(_, at, out):
            around(TABLE, vector(VECTORS[0], at))
            count = s32(e.load(0x700031E0))
            if count < 0 or count > 16:
                return -1
            out[0].count = count
            for i in range(count):
                out[0].flags[i] = e.load(0x70003170 + 2 * i, 2)
                C.c_uint32.from_address(C.addressof(out[0]) + ClimbTable.height.offset + 4 * i).value = \
                    e.load(0x700030F0 + 4 * i)
                C.c_uint32.from_address(C.addressof(out[0]) + ClimbTable.aux.offset + 4 * i).value = \
                    e.load(D_00282250 + 4 * i)
            return 0

        def attribute(_, t, index, out):
            v = around(ATTR, 0, index)[0] & 0xFFFF
            out[0] = v - 0x10000 if v & 0x8000 else v
            return 0

        def frame_matches(ledge):
            words = (C.c_uint32 * 23).from_address(C.addressof(ledge[0]))
            spad = [e.load(0x70003050 + 4 * i) for i in range(3)] + \
                [e.load(0x70003060 + 4 * i) for i in range(3)] + [e.load(0x700031E4)] + \
                [e.load(0x70003070 + 4 * i) for i in range(16)]
            assert list(words) == spad, 'the ledge frame passed is not the one in the scratchpad'

        def ledge(_, hit, out):
            point = (C.c_uint32 * 3).from_address(C.addressof(hit[0]) + ProbeHit.point.offset)
            assert list(point) == [e.load(0x700031B0 + 4 * i) for i in range(3)], 'ledge hit is not the last probe'
            around(LEDGE)
            words = (C.c_uint32 * 23).from_address(C.addressof(out[0]))
            for i in range(3):
                words[i] = e.load(0x70003050 + 4 * i)
                words[3 + i] = e.load(0x70003060 + 4 * i)
            words[6] = e.load(0x700031E4)
            for i in range(16):
                words[7 + i] = e.load(0x70003070 + 4 * i)
            return 0

        def sweeper(entry, wide=None):
            def fn(_, a, l, *rest):
                frame_matches(l)
                if wide is None:
                    y, out = rest
                    r = around(entry, base(), floats=(y,))[0]
                else:
                    w, y, out = rest
                    r = around(entry, base(), w, floats=(y,))[0]
                out[0] = s32(r)
                return 0
            return fn

        def skeleton(_, a, node):
            around(SKELETON, base())
            source = e.load(e.load(0x275B40) + 4) + 0xC0
            words = C.cast(node, C.POINTER(C.c_uint32))
            for i in range(4):
                words[i] = e.load(source + 4 * i)
            return 0

        def column(_, a, at, height, out):
            assert fvec(at, 3) == tuple(e.load(base() + 0xB0 + 4 * i) for i in range(3)), 'column point'
            out[0] = s32(around(COLUMN, base(), base() + 0xB0, 1, floats=(height,))[0])
            return 0

        def floor(_, a, search, out):
            out[0] = s32(around(FLOOR, base(), search)[0])
            return 0

        def rand(_, out):
            out[0] = around(RAND)[0] & 0xFFFFFFFF
            return 0

        def frames(_, a, clip, out):
            out[0] = s32(around(FRAMES, e.load(base() + 0x40), clip)[0])
            return 0

        fns = {
            'sine': math1(SINE), 'cosine': math1(COSINE),
            'atan2': MATH2(lambda _, y, x: b2f(e.nested(ATAN2, (), (y, x))[1])), 'sqrt': math1(SQRT),
            'probes': plain(PROBES),
            'request': REQ(lambda _, a, clip, force, blend: (around(REQUEST, base(), clip, force, floats=(blend,)), 0)[1]),
            'arbiter': ARB(lambda _, a, clip, blend, frame: (around(ARBITER, base(), clip, floats=(blend, frame)), 0)[1]),
            'clip_frames': FRM(frames),
            'sound': SND(lambda _, a, id: (around(SOUND, base(), id, 0, floats=(300.0,)), 0)[1]),
            'shake': SHK(lambda _, a0, a1, a2, a3: (around(SHAKE, a0, a1, a2, a3), 0)[1]),
            'random': RND(rand),
            'react_0021C350': plain(R350), 'react_0021C270': plain(R270), 'react_0021C120': plain(R120),
            'react_0021C190': result(R190), 'react_0021D490': plain(R490),
            'move': MOV(move), 'sweep': SWP(sweep), 'segment': SEG(segment), 'table': TBL(table),
            'attribute': ATR(attribute), 'ledge': LDG(ledge), 'lip': LIPF(sweeper(LIP, wide=True)),
            'sides': SWY(sweeper(SIDES)), 'hands': SWY(sweeper(HANDS)), 'depth': SWY(sweeper(DEPTH)),
            'hang_clear': result(HANG_CLEAR), 'hang_row': result(HANG_ROW), 'skeleton': SKL(skeleton),
            'column': COL(column),
            'heading': ACT_I(lambda _, a, arg: (around(HEADING, base(), arg), 0)[1]),
            'reentry': ACT_I(lambda _, a, arg: (around(REENTRY, base(), arg), 0)[1]),
            'handoff': plain(HANDOFF), 'floor': FLR(floor), 'fall': plain(FALL),
        }
        self.keep = fns
        self.workers = Workers(None, *[fns[name] for name, _ in WORKER_FIELDS])
        for address, name in TRANSLATED.items():
            ee.hooks[address] = self.hook(name)

    def scene(self):
        e, s = self.ee, Scene()
        s.camera_yaw = b2f(e.load(0x8106A0))
        s.d8106F1, s.spad3B8D = e.load(0x8106F1, 1), e.load(0x70003B8D, 1)
        s.pad_gait, s.pad_x, s.pad_y = e.load(0x810E57, 1), e.load(0x810E64, 1), e.load(0x810E65, 1)
        s.area = e.load(0x810700, 1)
        return s

    def hook(self, name):
        kind = ROUTINES[name][3]
        fn = getattr(NATIVE, ROUTINES[name][2])

        def run(e):
            call = Call(e, e.arg(0))
            self.calls.append(call)
            scene, out = self.scene(), C.c_int(-99)
            actor, w, x = C.byref(call.actor), C.byref(self.workers), C.byref(call.scratch)
            if kind == 'probe': status = fn(actor, s32(e.arg(1)), w)
            elif kind == 'bare': status = fn(actor)
            elif kind == 'scene': status = fn(actor, C.byref(scene), x, w)
            elif kind == 'scratch_result': status = fn(actor, x, w, C.byref(out))
            elif kind == 'scene_result': status = fn(actor, C.byref(scene), w, C.byref(out))
            elif kind == 'full_result': status = fn(actor, C.byref(scene), x, w, C.byref(out))
            elif kind == 'reach_result': status = fn(actor, b2f(e.f[12]), C.byref(scene), x, w, C.byref(out))
            else: status = fn(actor, w)
            assert status == 0, (name, 'native fault on the route')
            e.write(call.base, bytes(call.actor.bytes))
            for i, address in enumerate(SCRATCH):
                e.save(address, call.words[i])
            self.calls.pop()
            self.counts[name] = self.counts.get(name, 0) + 1
            if kind.endswith('result'):
                e.ret_int(out.value)
                key = (name, out.value)
                self.results[key] = self.results.get(key, 0) + 1
        return run


ROUTE_CAPTURE = {}


SCRIPTED = 0x41   # +1F0 of the scripted/cinematic takeover rows (+5 = 0)


def replay_end(trace):
    """The last counter the replay is valid for: the row before the first
    scripted takeover (+1F0 0x41) after the player has moved, else the end.
    RouteReplay runs no scripts or cinematics, so it cannot follow those
    rows (the beat's own opening rows are the idle frames it starts from)."""
    moved = False
    for r in sorted(trace['rows'], key=lambda r: r['counter']):
        if r['p5'] != 0:
            moved = True
        elif moved and r['m1F0'] == SCRIPTED:
            return r['counter'] - 1
    return trace['last_counter']


def route_replay(job):
    """One replay of a route beat: the original stage alone, or with the
    native routines hooked in. Every trace row up to replay_end is checked
    against the capture (shared.route_row_check)."""
    beat, native = job
    trace, ram, spad = ROUTE_CAPTURE[beat]
    replay = ModelReplay(ELF, trace, ram, spad)
    hooked = RouteNative(replay.ee) if native else None
    frames, rows = [], 0
    end = replay_end(trace)
    while replay.counter < end:
        r = replay.step()
        e = replay.ee
        frames.append((replay.counter, replay.actor(), tuple(e.load(a) for a in SCRATCH), len(replay.events)))
        if r is not None:
            shared.route_row_check(e, r, (beat, 'native' if native else 'original', replay.counter))
            rows += 1
    return frames, replay.events, rows, (hooked.counts, hooked.results) if hooked else None


def route_main():
    global ELF, NATIVE
    ELF = read_elf()
    NATIVE = build_native('recovery_route')
    beats = os.environ.get('EM_ROUTE_BEATS', '05_boxes,06_hill_slide,10_cage_roof_roger,11_crevice_prompt,'
                           '12_crevice_jump,14_roger_encounter').split(',')
    for beat in beats:
        found = shared.route_beat(beat)
        if isinstance(found, str):
            raise SystemExit('route mode: %s' % found)
        ROUTE_CAPTURE[beat] = found
    jobs = [(beat, native) for beat in beats for native in (False, True)]
    results = dict(zip(jobs, reference_mode.parallel_map(route_replay, jobs)))
    total = {}
    for beat in beats:
        (a_frames, a_events, rows, _), (b_frames, b_events, _, (counts, outcomes)) = \
            results[(beat, False)], results[(beat, True)]
        assert len(a_frames) == len(b_frames), (beat, 'frame counts')
        for (counter, a, a_words, a_count), (_, b, b_words, b_count) in zip(a_frames, b_frames):
            if a != b:
                diff = [hex(k) for k in range(0x320) if a[k] != b[k]]
                raise AssertionError((beat, counter, 'player bytes differ at', diff[:24]))
            assert a_words == b_words, (beat, counter, 'scratch words differ')
            assert a_count == b_count, (beat, counter, 'sound/effect call counts differ')
        assert a_events == b_events, (beat, 'sound/effect calls differ')
        for name, n in counts.items():
            total[name] = total.get(name, 0) + n
        end = replay_end(ROUTE_CAPTURE[beat][0])
        cut = '' if end == ROUTE_CAPTURE[beat][0]['last_counter'] else \
            ' (up to counter %d, before the scripted takeover)' % end
        print('route %s: PASS %d frames, %d trace rows%s (original and native replays match the capture); '
              'native calls %s; results %s' % (beat, len(a_frames), rows, cut, dict(sorted(counts.items())),
                                               dict(sorted(outcomes.items()))))
    print('player recovery route: PASS; native routines reached: %s' % dict(sorted(total.items())))


# Instructions no input can execute, each checked by hand against the
# listing: words after an unconditional branch and its delay slot that no
# branch targets (compiler padding), and in 001751A0 the `bltz` halves of the
# unsigned-byte-to-float idiom for D_00810E65 / D_00810E64 (the lbu value is
# 0..255, never negative).
DEAD = {
    'translate': {0x178D44},
    'strafe': {0x178F68},
    'quadrant': {0x175200, 0x175368} | set(range(0x175204, 0x17521C, 4)) | set(range(0x175250, 0x17526C, 4)),
    'react_a': {0x224500},
    'react_b': {0x224CAC, 0x224D68, 0x224D74, 0x224DDC, 0x224E30, 0x224EDC},
    'hang': {0x162CB4, 0x162CF0},
}


if __name__ == '__main__':
    if os.environ.get('EM_TEST_ROUTE', '') not in ('', '0'):
        route_main()
    else:
        main()
