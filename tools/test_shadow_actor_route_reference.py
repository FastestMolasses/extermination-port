#!/usr/bin/env python3
"""Execute the original +0x214 shadow route and compare em_shadow_actor_route.c.

docs/SHADOW_ACTOR_ROUTE.md. The user's pinned ELF (and the captured route
RAM) supplies every instruction and table; none are embedded here.

Original code executed unmodified:
  0015BF90  the +0x214 != 0 shadow (player post-step route)
  001F9100  the decal wrapper      001F8D30  the decal
  and their SDK leaves 00102948, 001029C0, 00102A60 (with 001029E8 /
  00102A90), 001026D0, 00102918, 0011DF78, 00102900, 00128250 (001278C0).
Worker boundaries (hooked, recorded, compared call by call; the native side
gets the same outputs through EmShadowActorRouteWorkers):
  0019A570  segment query (route mode: the original itself runs nested over
            the captured RAM; unit mode: scripted hits)
  0011E620  atan2f (the original runs nested; some unit cases script it)
  001CD390  look-at rows (the original runs nested)
  001CE300  the quad's GS packet (recorded only)
The node heights (+0xC4 of the records the player words +0x154 / +0x158
name) are RAM reads in the original and node_c4 worker calls natively; the
test checks the native reads against RAM.

Every case compares the return value, the scratchpad words the routines
write (0x700038A0..0x700038BF, 0x70003A20, 0x70003600..0x7000362F), the
worker call sequence with every argument, and asserts that the original
wrote no RAM or scratchpad byte outside those words (the nested workers'
own writes excepted). Every conditional branch of the three routines must
be taken both ways in the unit cases.

Binding checks: em_effect_original_001CD390 and em_sdk_math_original_0011E620
reproduce every look-at / atan2 call the original made (so the coordinator
can bind them as these workers).

Arithmetic: the shared interpreter's COP1/VU0 arithmetic deviates
(docs/EE_FLOAT_MODEL.md section 5a); RouteEE (below) routes every COP1 op and
every VU0 macro op through tools/ee_float_model.py. No shared file is edited.

Default run (~10 s): the covering unit sample, every route beat's player
(normal path, the 0x41 path, the 0x19 exit), native fault cases, and the
binding checks. EM_TEST_FULL=1: the full random sweeps.
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
from test_player_slide_reference import EE, read_elf, sx32  # noqa: E402

MASK = 0xFFFFFFFF
DECOMP = ROOT.parent / 'Extermination'
ROUTE = DECOMP / 'build/s87/route'
REFERENCE = DECOMP / 'build/startup-reference'
OUT = ROOT / 'build' / os.environ.get('EM_LANE', 'shadow_actor_route_reference')

BF90, F9100, F8D30 = 0x15BF90, 0x1F9100, 0x1F8D30
SIZES = {BF90: 0x1C8, F9100: 0x40, F8D30: 0x3D0}
SEG, ATAN2, LOOK, SUBMIT = 0x19A570, 0x11E620, 0x1CD390, 0x1CE300
WORKERS = {SEG: 'segment', ATAN2: 'atan2', LOOK: 'look_at', SUBMIT: 'submit'}
LEAVES = {0x102948, 0x1029C0, 0x102A60, 0x1026D0, 0x102918, 0x11DF78, 0x102900, 0x128250}
PLAYER = 0x8102B0
COLOUR, FACING = 0x25DAE0, 0x25DAF0
# Scratchpad words the routines may write (offsets into the 16 KiB image).
WRITABLE = set(range(0x38A0, 0x38C0)) | set(range(0x3A20, 0x3A24)) | set(range(0x3600, 0x3630))


def F(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def in_translated(pc):
    return any(start <= pc < start + size for start, size in SIZES.items())


# ======================================================================
# The interpreter with the measured float model
# ======================================================================

class RouteEE(EE):
    """The shared EE core; every COP1 op and VU0 macro op from ee_float_model
    (raw bits throughout; ACC and Q hold bits)."""

    def __init__(self, elf, ram=None, spad=None):
        super().__init__(elf, ram, spad)
        self.acc = 0
        self.vacc = [0, 0, 0, 0]
        self.q = 0
        self.depth = 0          # > 0 inside a worker hook (its writes are its own)
        self.writes = set()     # (space, offset) written by the routines themselves
        self.outcomes = set()   # (pc, taken) of conditional branches in the routines
        save = self.save

        def guarded(address, value, size=4):
            if self.depth == 0:
                address &= MASK
                for i in range(size):
                    self.writes.add(address + i)
            save(address, value, size)
        self.save = guarded

    def branch(self, word, pc):
        b = super().branch(word, pc)
        if b is not None and self.depth == 0 and in_translated(pc):
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            if not (op == 4 and rs == 0 and rt == 0):          # `b` is unconditional
                self.outcomes.add((pc, b[0]))
        return b

    def cop1(self, word, pc):
        rs, rt = word >> 21 & 31, word >> 16 & 31
        fs, fd, fn = word >> 11 & 31, word >> 6 & 31, word & 63
        f = self.f
        if rs == 0:                                             # mfc1
            if rt: self.r[rt] = sx32(f[fs]) & 0xFFFFFFFFFFFFFFFF
            return
        if rs == 4: f[fs] = self.r[rt] & MASK; return            # mtc1
        if rs == 2:                                             # cfc1
            if rt: self.r[rt] = 0
            return
        if rs == 6: return                                      # ctc1
        if rs == 20:
            if fn == 32: f[fd] = M.ee_cvt_s_w(f[fs] & MASK); return
            raise AssertionError(('cvt', fn, hex(pc)))
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
        else:
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
        elif 16 <= op < 20:                                                   # vmaxbc
            result = {i: M.vu_max(x[i], y[op - 16]) for i in lanes}
        elif 20 <= op < 24:                                                   # vminibc
            result = {i: M.vu_min(x[i], y[op - 20]) for i in lanes}
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
        elif op == 46:                                                        # vopmsub
            swz = {0: (1, 2), 1: (2, 0), 2: (0, 1)}
            result = {i: M.vu_lane('vopmsub', mask, None, x[swz[i][0]], y[swz[i][1]], self.vacc[i])
                      for i in lanes if i < 3}
        elif op >= 60:
            special = (word >> 6 & 31) << 2 | (op & 3)
            destination = ft
            if special in (0x30, 0x31):                                       # vmove / vmr32
                raw = list(self.vf[fs])
                if special == 0x31: raw = raw[1:] + raw[:1]
                for lane in lanes:
                    if ft: self.vf[ft][lane] = raw[lane]
                return
            if 0x18 <= special <= 0x1B:                                       # vmulabc
                bc = special - 0x18
                result = {i: M.vu_lane('vmulabc', mask, bc, x[i], y[bc]) for i in lanes}
                accumulate = True
            elif 0x08 <= special <= 0x0B:                                     # vmaddabc
                bc = special - 0x08
                result = {i: M.vu_lane('vmaddabc', mask, bc, x[i], y[bc], self.vacc[i]) for i in lanes}
                accumulate = True
            elif special == 0x2E:                                             # vopmula
                swz = {0: (1, 2), 1: (2, 0), 2: (0, 1)}
                result = {i: M.vu_lane('vopmula', mask, None, x[swz[i][0]], y[swz[i][1]])
                          for i in lanes if i < 3}
                accumulate = True
            elif special in (0x14, 0x15):                                     # vftoi0 / vftoi4
                result = {i: M.vu_ftoi(x[i], 4 * (special - 0x14)) for i in lanes}
            elif special in (0x10, 0x11):                                     # vitof0 / vitof4
                result = {i: M.vu_itof(x[i], 4 * (special - 0x10)) for i in lanes}
            elif special == 0x1D:                                             # vabs
                result = {i: M.vu_abs(x[i]) for i in lanes}
            elif special == 0x39:                                             # vsqrt
                self.q = M.vu_sqrt(y[word >> 23 & 3]); return
            elif special == 0x38:                                             # vdiv
                fsf, ftf = word >> 21 & 3, word >> 23 & 3
                self.q = M.vu_div(x[fsf], y[ftf], fsf, ftf); return
            elif special in (0x3B, 0x2F):                                     # vwaitq / vnop
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


def nested_bits(ee, entry, args=(), fregs=()):
    """Run an original routine from inside a hook with no hooks active and
    the interrupted registers restored afterwards. Returns (v0, f0)."""
    saved = (list(ee.r), list(ee.rh), ee.hi, ee.lo, list(ee.f), ee.acc,
             ee.cond, [list(v) for v in ee.vf], list(ee.vacc), ee.q)
    hooks, ee.hooks = ee.hooks, {}
    try:
        ee.r[29] = (ee.r[29] - 0x400) & ~15
        for i, value in enumerate(args): ee.r[4 + i] = sx32(value) & 0xFFFFFFFFFFFFFFFF
        for i, value in enumerate(fregs): ee.f[12 + i] = value & MASK
        ee.r[31] = shared.RETURN
        ee.run(entry)
        result = (ee.r[2], ee.f[0])
    finally:
        ee.hooks = hooks
    (ee.r, ee.rh, ee.hi, ee.lo, ee.f, ee.acc, ee.cond, ee.vf, ee.vacc, ee.q) = saved
    return result


def words(ee, address, count):
    return [ee.load(address + 4 * i) for i in range(count)]


# ======================================================================
# The worker hooks (oracle side)
# ======================================================================

class Oracle:
    """One case on the original side. `segment_script` is None to run the
    original 0019A570 nested (captured RAM), else a callable returning
    (result, point[4], record address, normal[3]) for the hook to plant.
    `yaw` overrides 0011E620's result (None: the original computes it)."""

    def __init__(self, ee, segment_script=None, yaw=None):
        self.ee, self.segment_script, self.yaw = ee, segment_script, yaw
        self.calls = []
        self.binding = []       # the calls the original computed itself (binding checks)
        ee.hooks = {SEG: self.segment, ATAN2: self.atan2, LOOK: self.look_at, SUBMIT: self.submit}

    def segment(self, ee):
        ee.depth += 1
        try:
            src, dst, mask, ident = ee.arg(0), ee.arg(1), s32(ee.r[6]), s32(ee.r[7])
            frm, to = words(ee, src, 4), words(ee, dst, 4)
            if self.segment_script is None:
                v0, _ = nested_bits(ee, SEG, (src, dst, mask, ident))
                result = s32(v0)
            else:
                result, point, record, normal = self.segment_script(frm, to)
                if result:
                    for i, w in enumerate(point): ee.save(0x700031B0 + 4 * i, w)
                    ee.save(0x700031D0, record)
                    for i, w in enumerate(normal): ee.save(record + 0x24 + 4 * i, w)
                ee.r[2] = sx32(result) & 0xFFFFFFFFFFFFFFFF
            point = normal = None
            if result:
                point = words(ee, 0x700031B0, 4)
                record = ee.load(0x700031D0)
                normal = words(ee, record + 0x24, 3)
            self.calls.append(('segment', tuple(frm), tuple(to), mask, ident, result,
                               tuple(point) if point else None, tuple(normal) if normal else None))
        finally:
            ee.depth -= 1

    def atan2(self, ee):
        ee.depth += 1
        try:
            y, x = ee.f[12] & MASK, ee.f[13] & MASK
            if self.yaw is None:
                _, f0 = nested_bits(ee, ATAN2, (), (y, x))
                value = f0 & MASK
            else:
                value = self.yaw
            ee.f[0] = value
            self.calls.append(('atan2', y, x, value))
            if self.yaw is None:
                self.binding.append(('atan2', y, x, value))
        finally:
            ee.depth -= 1

    def look_at(self, ee):
        ee.depth += 1
        try:
            out, src = ee.arg(0), ee.arg(1)
            v = words(ee, src, 4)
            nested_bits(ee, LOOK, (out, src))
            self.calls.append(('look_at', tuple(v), tuple(words(ee, out, 16))))
            self.binding.append(('look_at', tuple(v), tuple(words(ee, out, 16)),
                                 tuple(words(ee, 0x70003600, 16))))
        finally:
            ee.depth -= 1

    def submit(self, ee):
        ee.depth += 1
        try:
            tex0 = ee.r[6] & 0xFFFFFFFFFFFFFFFF
            self.calls.append(('submit', s32(ee.r[4]), tuple(words(ee, ee.arg(1), 16)), tex0,
                               ee.r[7] & MASK))
        finally:
            ee.depth -= 1


def s32(value):
    value &= MASK
    return value - (1 << 32) if value & 0x80000000 else value


# ======================================================================
# Native side (ctypes)
# ======================================================================

P, U32, I = C.POINTER, C.c_uint32, C.c_int


class LiveActor(C.Structure):
    _fields_ = [('bytes', C.c_uint8 * 0x320), ('link_owner', C.c_void_p), ('link_prev', C.c_void_p),
                ('link_flags', C.c_uint8), ('link_type', C.c_uint8)]


class Tables(C.Structure):
    _fields_ = [('colour', U32 * 4), ('facing', U32 * 4)]


class Scratch(C.Structure):
    _fields_ = [('s38A0', P(U32)), ('s38B0', P(U32)), ('s3A20', P(U32)), ('s3600', P(U32)),
                ('s3B8D', P(C.c_uint8)), ('s3AC0', P(U32))]


NODE_FN = C.CFUNCTYPE(I, C.c_void_p, U32, U32, P(U32))
SEG_FN = C.CFUNCTYPE(I, C.c_void_p, P(U32), P(U32), I, I, P(C.c_int32), P(U32), P(U32))
ATAN2_FN = C.CFUNCTYPE(I, C.c_void_p, U32, U32, P(U32))
LOOK_FN = C.CFUNCTYPE(I, C.c_void_p, P(U32), P(U32))
SUBMIT_FN = C.CFUNCTYPE(I, C.c_void_p, C.c_int32, P(U32), C.c_uint64, U32)


class Workers(C.Structure):
    _fields_ = [('context', C.c_void_p), ('node_c4', NODE_FN), ('segment', SEG_FN),
                ('atan2', ATAN2_FN), ('look_at', LOOK_FN), ('submit', SUBMIT_FN)]


class Fault(C.Structure):
    _fields_ = [('address', U32), ('code', C.c_int32)]


class Route(C.Structure):
    _fields_ = [('tables', P(Tables)), ('scratch', Scratch), ('workers', P(Workers)), ('fault', Fault)]


class EffectFault(C.Structure):
    _fields_ = [('address', U32), ('code', C.c_int32)]


class EffectGlobals(C.Structure):   # EmEffectOriginalGlobals (em_effect_original.h)
    _fields_ = [('d8101E4', C.c_uint8), ('d810700', C.c_uint8), ('d275C38', C.c_int32),
                ('spad3B68', C.c_int32), ('d8102E8', C.c_float), ('d275C30', C.c_void_p),
                ('d275C34', C.c_void_p), ('d275C04', C.c_int32), ('spad3600', U32 * 16),
                ('spad38A0', U32 * 4)]


class Effect(C.Structure):          # EmEffectOriginal
    _fields_ = [('tables', C.c_void_p), ('globals', P(EffectGlobals)), ('decals', C.c_void_p),
                ('view', C.c_void_p), ('workers', C.c_void_p), ('fault', EffectFault)]


NATIVE = None
SDK_TABLES = None


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / ('shadow_actor_route.dylib' if sys.platform == 'darwin' else 'shadow_actor_route.so')
    sources = ['src/game/em_shadow_actor_route.c', 'src/game/em_owner_services_original.c',
               'src/game/em_sdk_math_original.c', 'src/game/em_stream_lanes_original.c',
               'src/game/em_effect_original.c']
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc'] + sources + ['-lm', '-o', str(lib)],
                   cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    R = P(Route)
    native.em_shadow_actor_route_0015BF90.argtypes = [R, P(LiveActor)]
    native.em_shadow_actor_route_001F9100.argtypes = [R, P(U32), P(U32), P(U32), U32]
    native.em_shadow_actor_route_001F8D30.argtypes = [R, P(U32), P(U32), P(U32), P(U32), P(U32),
                                                      U32, U32, U32]
    native.em_shadow_actor_route_load_tables.argtypes = [C.c_char_p, C.c_size_t, P(Tables)]
    native.em_effect_original_001CD390.argtypes = [P(Effect), P(C.c_float), P(C.c_float)]
    native.em_sdk_math_original_load_tables.argtypes = [C.c_char_p, C.c_size_t, C.c_void_p]
    native.em_sdk_math_original_0011E620.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p, C.c_float,
                                                     C.c_float, P(C.c_float), P(U32)]
    return native


def arr(kind, values):
    return (kind * len(values))(*values)


class Native:
    """One native run with workers answering from the oracle's recorded
    outputs (in order) and recording their own arguments."""

    def __init__(self, ram_read, tables, spad, oracle_calls, fail_at=None, unbind=None):
        self.ram_read, self.fail_at = ram_read, fail_at
        self.outputs = [c for c in oracle_calls]
        self.calls, self.index = [], 0
        self.node_reads = []
        self.s38A0 = arr(U32, struct.unpack_from('<4I', spad, 0x38A0))
        self.s38B0 = arr(U32, struct.unpack_from('<4I', spad, 0x38B0))
        self.s3A20 = arr(U32, struct.unpack_from('<I', spad, 0x3A20))
        self.s3600 = arr(U32, struct.unpack_from('<12I', spad, 0x3600))
        self.s3B8D = arr(C.c_uint8, [spad[0x3B8D]])
        self.s3AC0 = arr(U32, struct.unpack_from('<16I', spad, 0x3AC0))
        self.tables = tables
        self.fns = {'node_c4': NODE_FN(self.node_c4), 'segment': SEG_FN(self.segment),
                    'atan2': ATAN2_FN(self.atan2), 'look_at': LOOK_FN(self.look_at),
                    'submit': SUBMIT_FN(self.submit)}
        fields = {k: v for k, v in self.fns.items() if k != unbind}
        self.workers = Workers(None, **fields)
        self.route = Route(C.pointer(tables), Scratch(self.s38A0, self.s38B0, self.s3A20, self.s3600,
                                                      self.s3B8D, self.s3AC0),
                           C.pointer(self.workers), Fault(0, 0))

    def _next(self, name):
        n = len(self.calls)
        if self.fail_at is not None and n == self.fail_at:
            return None
        if self.index >= len(self.outputs):
            return 'extra'
        out = self.outputs[self.index]
        self.index += 1
        return out if out[0] == name else 'mismatch'

    def node_c4(self, _ctx, slot, word, value):
        self.node_reads.append((slot, word))
        if self.fail_at == ('node', len(self.node_reads) - 1):
            return -1
        value[0] = self.ram_read(word + 0xC4)
        return 0

    def segment(self, _ctx, frm, to, mask, ident, result, point, normal):
        out = self._next('segment')
        call = ('segment', tuple(frm[:4]), tuple(to[:4]), mask, ident)
        if out is None:
            self.calls.append(call + ('fail',))
            return -1
        if not isinstance(out, tuple):
            self.calls.append(call + (out,))
            return -1
        self.calls.append(call + (out[5], out[6], out[7]))
        result[0] = out[5]
        if out[5]:
            for i in range(4): point[i] = out[6][i]
            for i in range(3): normal[i] = out[7][i]
        return 0

    def atan2(self, _ctx, y, x, value):
        out = self._next('atan2')
        if not isinstance(out, tuple):
            self.calls.append(('atan2', y, x, 'fail' if out is None else out))
            return -1
        self.calls.append(('atan2', y, x, out[3]))
        value[0] = out[3]
        return 0

    def look_at(self, _ctx, dst, v):
        out = self._next('look_at')
        call = ('look_at', tuple(v[:4]))
        if not isinstance(out, tuple):
            self.calls.append(call + ('fail' if out is None else out,))
            return -1
        self.calls.append(call + (out[2],))
        for i in range(16): dst[i] = out[2][i]
        return 0

    def submit(self, _ctx, tag, corners, tex0, rgba):
        out = self._next('submit')
        self.calls.append(('submit', tag, tuple(corners[:16]), tex0, rgba))
        return -1 if out is None or not isinstance(out, tuple) else 0

    def scratch_words(self):
        return {'38A0': list(self.s38A0), '38B0': list(self.s38B0), '3A20': list(self.s3A20),
                '3600': list(self.s3600)}


def spad_words(spad):
    return {'38A0': list(struct.unpack_from('<4I', spad, 0x38A0)),
            '38B0': list(struct.unpack_from('<4I', spad, 0x38B0)),
            '3A20': list(struct.unpack_from('<I', spad, 0x3A20)),
            '3600': list(struct.unpack_from('<12I', spad, 0x3600))}


def check_writes(ee, where):
    bad = sorted(a for a in ee.writes
                 if not (0x70000000 <= a < 0x70004000 and a - 0x70000000 in WRITABLE)
                 and not (0x7F000000 <= a < 0x7F100000))
    assert not bad, (where, 'original wrote outside the compared words', [hex(a) for a in bad[:8]])


def compare(where, oracle, ee, native, rc):
    assert rc == 0, (where, 'native faulted', rc, native.route.fault.address, native.route.fault.code)
    assert native.index == len(oracle.calls), (where, 'worker calls', native.calls, oracle.calls)
    for n, (a, b) in enumerate(zip(native.calls, oracle.calls)):
        if a[0] == 'segment':
            assert a == b, (where, n, 'segment', a, b)
        elif a[0] == 'atan2':
            assert a == b, (where, n, 'atan2', a, b)
        elif a[0] == 'look_at':
            assert a == b, (where, n, 'look_at', a, b)
        else:
            assert a == b, (where, n, 'submit', a, b)
    assert len(native.calls) == len(oracle.calls), (where, native.calls, oracle.calls)
    got, want = native.scratch_words(), spad_words(ee.spad)
    assert got == want, (where, 'scratch', {k: ([hex(x) for x in got[k]], [hex(x) for x in want[k]])
                                            for k in got if got[k] != want[k]})
    check_writes(ee, where)


# ======================================================================
# Case builders
# ======================================================================

def float_pick(rng, values, lo, hi):
    return F(rng.choice(values)) if rng.random() < 0.25 else F(rng.uniform(lo, hi))


SPECIAL = [0x7FC00000, 0xFFC00000, 0x7F800000, 0xFF800000, 0x80000000, 0x00000001, 0x7F7FFFFF]


def run_bf90(ee, native_args, where, segment_script=None, yaw=None):
    """Run 0015BF90 over `ee` (player at PLAYER) and the native module over a
    copy of the same bytes. Returns the oracle."""
    ram0 = bytes(ee.mem[PLAYER:PLAYER + 0x320])
    spad0 = bytes(ee.spad)
    mem0 = ee.mem
    snapshot = {}

    def ram_read(address):
        address &= MASK
        if address not in snapshot:
            snapshot[address] = int.from_bytes(mem0[address & 0x1FFFFFF:(address & 0x1FFFFFF) + 4], 'little')
        return snapshot[address]
    # the node heights as the original will read them (before any hook plants data)
    w154, w158 = struct.unpack_from('<II', ram0, 0x154)
    ram_read(w154 + 0xC4), ram_read(w158 + 0xC4)
    oracle = Oracle(ee, segment_script, yaw)
    ee.writes.clear()
    ee.call(BF90, (PLAYER,))
    native = Native(ram_read, native_args['tables'], spad0, oracle.calls)
    actor = LiveActor()
    C.memmove(actor.bytes, ram0, 0x320)
    rc = NATIVE.em_shadow_actor_route_0015BF90(C.byref(native.route), C.byref(actor))
    compare(where, oracle, ee, native, rc)
    if ram0[0x1F0] != 0x19:
        assert native.node_reads == [(0x154, w154), (0x158, w158)], (where, native.node_reads)
    else:
        assert native.node_reads == [], (where, native.node_reads)
    return oracle


CAMERAS = []


def base_ram(elf):
    ee = EE(elf)
    return bytes(ee.mem)


BASE = None
TABLES = None
NODE_A, NODE_B, RECORD = 0x6D0000, 0x6D0200, 0x6D1000
VEC = 0x6E0000


def synthetic_ee(elf, rng, spad_from=None):
    ee = RouteEE(elf, BASE, bytearray(spad_from) if spad_from is not None else None)
    cam = rng.choice(CAMERAS) if CAMERAS and rng.random() < 0.8 else None
    for i in range(16):
        if cam is not None:
            value = cam[i]
            if rng.random() < 0.3:
                value = M.ee_mul(value, F(rng.uniform(0.5, 1.5)))
        else:
            value = F(rng.uniform(-2000, 2000))
        ee.save(0x70003AC0 + 4 * i, value)
    return ee


def unit_bf90(item):
    elf, seed = ELF, item
    rng = random.Random('bf90:%d' % seed)
    ee = synthetic_ee(elf, rng)
    x, y, z = rng.uniform(-500, 500), rng.uniform(-100, 400), rng.uniform(-500, 500)
    for i, v in enumerate((F(x), F(y), F(z), rng.choice((F(1.0), F(1.0), F(0.0), rng.getrandbits(32))))):
        ee.save(PLAYER + 0xB0 + 4 * i, v)
    ee.save(PLAYER + 0x1F0, rng.choice((0, 0, 0x19, 0x41, 0x41, 8, 0x30)), 1)
    ee.save(0x70003B8D, rng.choice((0, 0, 1, 2)), 1)
    ee.save(PLAYER + 0x154, NODE_A)
    ee.save(PLAYER + 0x158, NODE_B)
    ya = rng.uniform(y - 10, y + 10)
    yb = rng.choice((ya, rng.uniform(y - 10, y + 10), -0.0 if ya == 0 else ya + 0.5))
    wa, wb = F(ya), F(yb)
    roll = rng.random()
    if roll < 0.08: wa = rng.choice(SPECIAL)
    elif roll < 0.16: wb = rng.choice(SPECIAL)
    elif roll < 0.2: wa, wb = 0x80000000, 0x00000000
    ee.save(NODE_A + 0xC4, wa)
    ee.save(NODE_B + 0xC4, wb)
    # the scratch words start with junk (the routines overwrite what they use)
    for off in range(0x3600, 0x3640, 4): ee.save(0x70000000 + off, rng.getrandbits(32))
    for off in range(0x38A0, 0x38C0, 4): ee.save(0x70000000 + off, rng.getrandbits(32))
    ee.save(0x70003A20, rng.getrandbits(32))

    def script(frm, to):
        result = rng.choice((0, 1, 2, 4, 2, 4))
        if not result:
            return 0, None, None, None
        drop = rng.uniform(0, 100)
        py = M.ee_sub(frm[1] & MASK, F(drop))
        point = [M.ee_add(frm[0], F(rng.uniform(-2, 2))), py, M.ee_add(frm[2], F(rng.uniform(-2, 2))),
                 rng.choice((0, 0, F(1.0), rng.getrandbits(32)))]
        kind = rng.random()
        if kind < 0.3: normal = [0, F(1.0), 0]
        elif kind < 0.4: normal = [0, F(-1.0), 0]
        elif kind < 0.45: normal = [0, 0, 0]
        elif kind < 0.5: normal = [0x80000000, F(1.0), 0x80000000]
        else:
            nx, ny, nz = rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-1, 1)
            normal = [F(nx), F(ny), F(nz)]
        return result, point, RECORD, normal
    yaw = None
    if rng.random() < 0.15:
        yaw = rng.choice((F(3.14159274), F(-3.14159274), 0, 0x80000000, F(1.5707964), F(-0.5)))
    oracle = run_bf90(ee, {'tables': TABLES}, ('unit', seed), script, yaw)
    return ee.outcomes, oracle.binding


def unit_direct(item):
    """001F8D30 (and 001F9100) called directly with general arguments."""
    elf, seed = ELF, item
    rng = random.Random('8d30:%d' % seed)
    ee = synthetic_ee(elf, rng)
    for off in range(0x3600, 0x3640, 4): ee.save(0x70000000 + off, rng.getrandbits(32))
    owner = [F(rng.uniform(-500, 500)) for _ in range(4)]
    point = [F(rng.uniform(-500, 500)) for _ in range(3)] + [rng.choice((0, F(1.0)))]
    if rng.random() < 0.5:
        owner[1] = M.ee_add(point[1], F(rng.choice((rng.uniform(-40, 40), 0.0, 27.0, 30.0, 33.0))))
    if rng.random() < 0.05:
        owner[1] = rng.choice(SPECIAL)
    kind = rng.random()
    if kind < 0.3: normal = [0, F(1.0), 0, F(1.0)]
    elif kind < 0.35: normal = [0, F(-1.0), 0, F(1.0)]
    else: normal = [F(rng.uniform(-1, 1)) for _ in range(3)] + [F(1.0)]
    facing = [F(rng.uniform(-1, 1)), F(rng.uniform(-1, 1)), F(rng.uniform(-1, 1)), 0]
    if rng.random() < 0.2: facing = [0, 0, F(1.0), 0]
    colour = [F(rng.choice((8.0, 255.0, rng.uniform(-10, 300), 0.0))) for _ in range(4)]
    f12, f13 = F(rng.uniform(0.5, 20)), F(rng.uniform(0.5, 20))
    f14 = rng.choice((F(30.0), F(30.0), 0, 0x80000000, F(-30.0), F(rng.uniform(1, 60)), 0x00000001))
    direct = rng.random() < 0.8
    base = VEC
    for i, w in enumerate(owner + point + normal + facing + colour):
        ee.save(base + 4 * i, w)
    spad0 = bytes(ee.spad)
    yaw = None
    if rng.random() < 0.1:
        yaw = rng.choice((F(3.14159274), F(-3.14159274), 0, 0x80000000))
    oracle = Oracle(ee, None, yaw)
    ee.writes.clear()
    if direct:
        ee.r[8] = base + 0x40       # t0 = colour (owner +0x00, point +0x10, normal +0x20, facing +0x30)
        for i, v in enumerate((f12, f13, f14)): ee.f[12 + i] = v
        ee.r[31] = shared.RETURN
        for i, v in enumerate((base, base + 0x10, base + 0x20, base + 0x30)): ee.r[4 + i] = v
        ee.run(F8D30)
    else:
        ee.f[12] = f12
        ee.r[31] = shared.RETURN
        for i, v in enumerate((base, base + 0x10, base + 0x20)): ee.r[4 + i] = v
        ee.run(F9100)
    native = Native(lambda a: 0, TABLES, spad0, oracle.calls)
    A = lambda v: arr(U32, v)  # noqa: E731
    if direct:
        rc = NATIVE.em_shadow_actor_route_001F8D30(C.byref(native.route), A(owner), A(point), A(normal),
                                                   A(facing), A(colour), f12, f13, f14)
    else:
        rc = NATIVE.em_shadow_actor_route_001F9100(C.byref(native.route), A(owner), A(point), A(normal), f12)
    compare(('direct' if direct else '9100', seed), oracle, ee, native, rc)
    return ee.outcomes, oracle.binding


# ---- route beats --------------------------------------------------------------

BEATS = sorted(p.name for p in ROUTE.iterdir() if (p / 'eeMemory.bin').exists()) if ROUTE.exists() else []


def check_code(elf, ram, where):
    ee = EE(elf)
    for start, size in list(SIZES.items()) + [(a, 0x40) for a in LEAVES] + [(SEG, 0x100), (LOOK, 0x188),
                                                                           (ATAN2, 0x80)]:
        assert bytes(ram[start:start + size]) == bytes(ee.mem[start:start + size]), (where, hex(start))


def route_case(item):
    beat, variant = item
    ram = (ROUTE / beat / 'eeMemory.bin').read_bytes() if beat != 'opening' else \
        (REFERENCE / 'opening_ee.bin').read_bytes()
    spad = (ROUTE / beat / 'scratchpad.bin').read_bytes() if beat != 'opening' else \
        (REFERENCE / 'opening_scratchpad.bin').read_bytes()
    check_code(ELF, ram, beat)
    ee = RouteEE(ELF, ram, spad)
    on_actor = ee.load(PLAYER + 0x214) != 0
    if variant == 'x41':
        ee.save(PLAYER + 0x1F0, 0x41, 1)
        ee.save(0x70003B8D, 1, 1)
    elif variant == 'x19':
        ee.save(PLAYER + 0x1F0, 0x19, 1)
    oracle = run_bf90(ee, {'tables': TABLES}, (beat, variant))
    seg = [c for c in oracle.calls if c[0] == 'segment']
    sub = [c for c in oracle.calls if c[0] == 'submit']
    return beat, variant, on_actor, seg[0][5] if seg else None, len(sub), oracle.binding


def dispatch_case(beat):
    """0015C160 itself over a beat whose player stands on an actor: the
    original post-step must call 0015BF90(player) (not 001DA6A0) between
    001CB590 and the +0x4C draw method."""
    ram = (ROUTE / beat / 'eeMemory.bin').read_bytes()
    spad = (ROUTE / beat / 'scratchpad.bin').read_bytes()
    ee = RouteEE(ELF, ram, spad)
    assert ee.load(PLAYER + 0x214) != 0 and ee.load(0x8102B1, 1) != 0 and ee.load(0x810771, 1) != 1, beat
    method = ee.load(PLAYER + 0x4C)
    seen = []

    def record(name, run=False):
        def hook(e):
            seen.append((name, e.arg(0)))
            if run:                  # 001CB590 runs as original (it sets D_00275B44)
                nested_bits(e, 0x1CB590, (e.arg(0), e.arg(1), e.arg(2), e.arg(3)))
        return hook
    ee.hooks = {0x1CB590: record('001CB590', True), 0x1DA6A0: record('001DA6A0'),
                BF90: record('0015BF90'), method: record('+0x4C')}
    ee.call(0x15C160)
    assert seen == [('001CB590', PLAYER), ('0015BF90', PLAYER), ('+0x4C', PLAYER)], (beat, seen)
    return beat


# ---- native fault cases ----------------------------------------------------------

def fault_cases():
    """Missing workers fault before any write; a failing worker stops at once."""
    def hit(frm, to):
        return 4, [frm[0], M.ee_sub(frm[1], F(5.0)), frm[2], 0], RECORD, [0, F(1.0), 0]
    for seed in range(64):          # the first camera whose winding lets the quad through
        rng = random.Random('faults:%d' % seed)
        ee = synthetic_ee(ELF, rng)
        for i, v in enumerate((F(10.0), F(50.0), F(20.0), F(1.0))): ee.save(PLAYER + 0xB0 + 4 * i, v)
        ee.save(PLAYER + 0x154, NODE_A); ee.save(PLAYER + 0x158, NODE_B)
        ee.save(NODE_A + 0xC4, F(40.0)); ee.save(NODE_B + 0xC4, F(41.0))
        spad0 = bytes(ee.spad)
        ram0 = bytes(ee.mem[PLAYER:PLAYER + 0x320])
        oracle = Oracle(ee, hit)
        ee.call(BF90, (PLAYER,))
        names = [c[0] for c in oracle.calls]
        if names == ['segment', 'atan2', 'look_at', 'submit']:
            break
    assert names == ['segment', 'atan2', 'look_at', 'submit'], names
    actor = LiveActor()
    C.memmove(actor.bytes, ram0, 0x320)
    mem = ee.mem
    read = lambda a: int.from_bytes(mem[a & 0x1FFFFFF:(a & 0x1FFFFFF) + 4], 'little')  # noqa: E731
    count = 0
    for missing in ('node_c4', 'segment', 'atan2', 'look_at', 'submit'):
        n = Native(read, TABLES, spad0, oracle.calls, unbind=missing)
        rc = NATIVE.em_shadow_actor_route_0015BF90(C.byref(n.route), C.byref(actor))
        assert rc == -1 and n.route.fault.code == 1 and n.route.fault.address == BF90, (missing, rc)
        assert n.calls == [] and n.node_reads == [] and n.scratch_words() == spad_words(spad0), missing
        count += 1
    # a failing worker at each call position: -1 at that worker's address, earlier writes kept
    addresses = {'segment': SEG, 'atan2': ATAN2, 'look_at': LOOK, 'submit': SUBMIT}
    for k, name in enumerate(names):
        n = Native(read, TABLES, spad0, oracle.calls, fail_at=k)
        rc = NATIVE.em_shadow_actor_route_0015BF90(C.byref(n.route), C.byref(actor))
        assert rc == -1 and n.route.fault.code == 2 and n.route.fault.address in (addresses[name], 0x19A570), \
            (name, rc, hex(n.route.fault.address))
        assert len(n.calls) == k + 1, (name, n.calls)
        # a latched fault refuses the next call without touching anything
        before = n.scratch_words()
        rc = NATIVE.em_shadow_actor_route_0015BF90(C.byref(n.route), C.byref(actor))
        assert rc == -1 and n.scratch_words() == before and len(n.calls) == k + 1
        count += 1
    n = Native(read, TABLES, spad0, oracle.calls, fail_at=('node', 1))
    rc = NATIVE.em_shadow_actor_route_0015BF90(C.byref(n.route), C.byref(actor))
    assert rc == -1 and n.route.fault.address == 0x15BFCC and n.calls == [], (rc, hex(n.route.fault.address))
    count += 1
    # missing tables / scratch
    n = Native(read, TABLES, spad0, oracle.calls)
    n.route.tables = None
    assert NATIVE.em_shadow_actor_route_0015BF90(C.byref(n.route), C.byref(actor)) == -1
    assert n.route.fault.code == 1 and n.calls == []
    n = Native(read, TABLES, spad0, oracle.calls)
    n.route.scratch.s3AC0 = None
    assert NATIVE.em_shadow_actor_route_0015BF90(C.byref(n.route), C.byref(actor)) == -1
    assert n.route.fault.code == 1 and n.calls == []
    return count + 2


# ---- binding checks -------------------------------------------------------------

def binding_checks(calls):
    """em_effect_original_001CD390 / em_sdk_math_original_0011E620 against
    every look-at / atan2 call the original computed."""
    glob = EffectGlobals()
    effect = Effect(None, C.pointer(glob), None, None, None, EffectFault(0, 0))
    looks = atans = 0
    for call in calls:
        if call[0] != 'look_at':
            continue
        spad = call[3]
        v = (C.c_float * 4)()
        C.memmove(v, struct.pack('<4I', *call[1]), 16)
        out = (C.c_float * 16)()
        effect.fault = EffectFault(0, 0)
        rc = NATIVE.em_effect_original_001CD390(C.byref(effect), out, v)
        got = list(struct.unpack('<16I', bytes(out)))
        assert rc == 0 and got == list(call[2]), ('001CD390 binding', [hex(x) for x in call[1]],
                                                    [hex(x) for x in got], [hex(x) for x in call[2]])
        assert list(glob.spad3600) == list(spad), ('001CD390 binding spad', [hex(x) for x in glob.spad3600],
                                                   [hex(x) for x in spad])
        looks += 1
    world_cell = C.c_int32(1)
    world = C.c_void_p(C.addressof(world_cell))
    for call in calls:
        if call[0] != 'atan2':
            continue
        y = C.c_float.from_buffer_copy(struct.pack('<I', call[1])).value
        x = C.c_float.from_buffer_copy(struct.pack('<I', call[2])).value
        result, fault = C.c_float(), U32(0)
        rc = NATIVE.em_sdk_math_original_0011E620(C.addressof(SDK_TABLES), C.byref(world), None, y, x,
                                                  C.byref(result), C.byref(fault))
        got = struct.unpack('<I', struct.pack('<f', result.value))[0]
        assert rc == 0 and got == call[3], ('0011E620 binding', hex(call[1]), hex(call[2]), hex(got),
                                            hex(call[3]))
        atans += 1
    return looks, atans


# ======================================================================

ELF = None


def main():
    global ELF, NATIVE, BASE, TABLES, SDK_TABLES
    ELF = read_elf()
    NATIVE = build_native()
    TABLES = Tables()
    assert NATIVE.em_shadow_actor_route_load_tables(ELF, len(ELF), C.byref(TABLES)) == 0
    assert list(TABLES.colour) == list(struct.unpack_from('<4I', ELF, COLOUR - 0x100000 + 0x300))
    assert list(TABLES.facing) == list(struct.unpack_from('<4I', ELF, FACING - 0x100000 + 0x300))
    assert NATIVE.em_shadow_actor_route_load_tables(ELF[:-1], len(ELF) - 1, C.byref(Tables())) == -1
    SDK_TABLES = C.create_string_buffer(8192)
    assert NATIVE.em_sdk_math_original_load_tables(ELF, len(ELF), SDK_TABLES) == 0
    BASE = base_ram(ELF)

    # the callee set of the three routines is exactly workers + leaves + themselves
    probe = EE(ELF)
    targets = set()
    for start, size in SIZES.items():
        for pc in range(start, start + size, 4):
            word = probe.load(pc)
            if word >> 26 == 3:
                targets.add((word & 0x3FFFFFF) << 2)
    expected = set(WORKERS) | LEAVES | {F9100, F8D30}
    assert targets == expected, ('callee set', sorted(map(hex, targets ^ expected)))

    for beat in BEATS:
        spad = (ROUTE / beat / 'scratchpad.bin').read_bytes()
        CAMERAS.append(struct.unpack_from('<16I', spad, 0x3AC0))

    units = list(range(20000))
    n_bf90 = reference_mode.pick(len(units), 600)
    n_direct = reference_mode.pick(len(units), 500)
    bf90_items = reference_mode.select(units, n_bf90, 1)
    direct_items = reference_mode.select(units, n_direct, 2)
    outcomes, all_calls = set(), []
    for got, calls in reference_mode.parallel_map(unit_bf90, bf90_items):
        outcomes |= got
        all_calls += calls
    for got, calls in reference_mode.parallel_map(unit_direct, direct_items):
        outcomes |= got
        all_calls += calls

    # every conditional branch of the three routines, both ways
    branches = set()
    for start, size in SIZES.items():
        for pc in range(start, start + size, 4):
            word = probe.load(pc)
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            if op in (1, 4, 5, 6, 7, 20, 21, 22, 23) or (op == 17 and rs == 8):
                if not (op == 4 and rs == 0 and rt == 0):
                    branches.add(pc)
    missing = sorted((hex(pc), t) for pc in branches for t in (True, False) if (pc, t) not in outcomes)
    assert not missing, ('branch outcomes never seen', missing)

    faults = fault_cases()

    route_items = [(b, v) for b in BEATS for v in ('play', 'x41', 'x19')]
    if (REFERENCE / 'opening_ee.bin').exists() and (REFERENCE / 'opening_scratchpad.bin').exists():
        route_items.append(('opening', 'play'))
    rows = reference_mode.parallel_map(route_case, route_items, cost=lambda it: it[1] == 'play')
    submitted = on_actor = hits = 0
    for beat, variant, actor, seg, subs, calls in rows:
        all_calls += calls
        submitted += subs
        if variant == 'play':
            on_actor += actor
            hits += bool(seg)
            print(f'  {beat}: +0x214 {"set" if actor else "0"}, 0019A570 -> {seg}, quads {subs}')

    dispatched = [dispatch_case(beat) for beat, variant, actor, *_ in rows if variant == 'play' and actor]

    # binding checks over every look-at and atan2 the original computed
    looks, atans = binding_checks(all_calls)

    reference_mode.banner(
        reference_mode.part(len(bf90_items), len(units), '0015BF90 unit cases'),
        reference_mode.part(len(direct_items), len(units), '001F8D30/001F9100 unit cases'),
        f'{len(branches)} conditional branches both ways',
        f'{len(route_items)} captured-RAM cases ({on_actor} with +0x214 set, {hits} segment hits, '
        f'{submitted} quads)',
        f'0015C160 routes to 0015BF90 in {len(dispatched)} captured states ({", ".join(dispatched)})',
        f'{faults} fault cases',
        f'binding: {looks} 001CD390 and {atans} 0011E620 calls reproduced')
    print('shadow actor route reference: PASS')


if __name__ == '__main__':
    main()
