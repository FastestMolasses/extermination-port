#!/usr/bin/env python3
"""Execute the original frame setup and projection heads and compare
src/game/em_frame_render_heads.c (docs/FRAME_RENDER_HEADS.md).

The user's pinned ELF and the captured route RAM supply every instruction
and byte; none are embedded here. Routines executed unmodified (the lane
L32-frame-render-heads of docs/FIRST_LEVEL_CENSUS.md, plus the SDK leaves
they reach, which the module translates privately):

  001D1C50  001D1EA0  001D1EF0  001D19E0  001D19D0  001D9070  001D2830
  001D2960  001D2D20  001D25F0  001D2590  001D2610  001C1D00  001D30A0
  001D8060  001D80B0  001D88B0  001D8C30
  copy_qw4 (00102958), 00102948, 001026D0, 001029C0

Memory: both sides see the whole 32 MB RAM image and the 16 KB scratchpad
of a route capture. The native module gets them as views (plus the two
uncached RAM mirrors the EE also decodes), so every address the original
forms reaches the same byte on both sides. After each case the whole RAM
and the whole scratchpad are compared byte for byte, and so is the ordered
list of worker calls with every argument.

Workers (every jal target of the routines that is not translated here) are
hooked on the original side and bound to ctypes callbacks on the native
side; both sides consult the same per-case script. The test asserts that the
hooked set is exactly the set of those jal targets. In the default unit run
the script supplies each worker's result (branch coverage of every
conditional branch of the translated routines is asserted); sqrtf/tanf
results come from the original 0011E748/0011E398 executed in a separate
interpreter. The route mode follows (EM_TEST_WORLD=1 runs it alone): from each route beat's
snapshot, two per-frame passes of the heads (001D1C50, 001C1D00(0x8101D0),
001D1EA0(1)) plus 001D2610/001D88B0 run once all-original and once with the
native translations operating directly on the second interpreter's memory,
every worker executing the ORIGINAL callee in that interpreter (the GS/DMA
kick 001CB800 and the world flush pair are recorded boundaries on both
sides).

Arithmetic: every COP1 op and every VU0 macro op goes through
tools/ee_float_model.py (docs/EE_FLOAT_MODEL.md), in this file.

Default run (~5 s): 700 of 6,000 unit cases and 3 of the 15 route beats (00, 14 and a
fixed-seed pick). EM_TEST_FULL=1: every unit case and every beat (~45 s).
"""
import ctypes as C
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
import ee_float_model as M  # noqa: E402
from test_player_slide_reference import EE, read_elf, sx32, RETURN  # noqa: E402

MASK = 0xFFFFFFFF
DECOMP = ROOT.parent / 'Extermination'
ROUTE = DECOMP / 'build/s87/route'
LANE = os.environ.get('EM_LANE', 'b7-frame-render-heads')
OUT = ROOT / 'build' / LANE
BASE_BEAT = '01_battery'


# ======================================================================
# The interpreter with the measured float model
# ======================================================================

class FrhEE(EE):
    """The shared EE core with COP1 and VU0 macro arithmetic taken from
    ee_float_model (raw bit patterns throughout; ACC and Q hold bits), and
    EE hardware registers refused (a callee that reaches them must be a
    recorded boundary)."""

    def __init__(self, elf, ram=None, spad=None):
        super().__init__(elf, ram, spad)
        self.acc = 0
        self.vacc = [0, 0, 0, 0]
        self.q = 0
        self.outcomes = None

    def _where(self, address):
        address &= 0xFFFFFFFF
        if 0x10000000 <= address < 0x20000000:
            raise AssertionError(('EE hardware register', hex(address)))
        return super()._where(address)

    def mmi(self, word, pc):
        fn, sub = word & 63, word >> 6 & 31
        rs, rt, rd = word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        if (fn, sub) in ((0x08, 0x12), (0x28, 0x12)):       # pextlw / pextuw
            if rd:
                a = (self.r[rs] & 0xFFFFFFFFFFFFFFFF) | (self.rh[rs] << 64)
                b = (self.r[rt] & 0xFFFFFFFFFFFFFFFF) | (self.rh[rt] << 64)
                w = lambda v, i: (v >> (32 * i)) & MASK
                k = 0 if fn == 0x08 else 2
                value = w(b, k) | w(a, k) << 32 | w(b, k + 1) << 64 | w(a, k + 1) << 96
                self.r[rd] = value & 0xFFFFFFFFFFFFFFFF
                self.rh[rd] = value >> 64
            return
        super().mmi(word, pc)

    def branch(self, word, pc):
        b = super().branch(word, pc)
        if b is not None and self.outcomes is not None and in_translated(pc):
            self.outcomes.add((pc, b[0]))
        return b

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


# ======================================================================
# The routines, their sizes and their callees
# ======================================================================

SIZES = {
    0x1D1C50: 0x248, 0x1D1EA0: 0x50, 0x1D1EF0: 0x30, 0x1D19E0: 0xFC, 0x1D19D0: 0x8,
    0x1D9070: 0x124, 0x1D2830: 0x4C, 0x1D2960: 0x3C0, 0x1D2D20: 0xB4, 0x1D25F0: 0x14,
    0x1D2590: 0x58, 0x1D2610: 0x100, 0x1C1D00: 0xBC, 0x1D30A0: 0x724, 0x1D8060: 0x4C,
    0x1D80B0: 0x30, 0x1D88B0: 0x118, 0x1D8C30: 0x394,
    0x102958: 0x24, 0x102948: 0xC, 0x1026D0: 0x44, 0x1029C0: 0x28,     # SDK leaves
}
TRANSLATED = set(SIZES)
LANE_ROUTINES = {a for a in SIZES if a >= 0x1C0000}


def in_translated(pc):
    return any(start <= pc < start + size for start, size in SIZES.items())


# Worker slots in EmFrhWorkers order: (field, original address, signature).
# Signature letters: i int32, u uint32, f float bits (FPR), q 64-bit a0;
# after '>' the result: i/u from v0, f from f0.
WORKERS = (
    ('w_001D2730', 0x1D2730, 'ii>i'), ('w_001E0C80', 0x1E0C80, 'ii>i'),
    ('w_001B0070', 0x1B0070, '>u'), ('w_0015D2F0', 0x15D2F0, '>i'),
    ('w_0021B970', 0x21B970, 'ff'), ('w_0021B9A0', 0x21B9A0, 'ffi'), ('w_0021BA80', 0x21BA80, 'iii'),
    ('w_001D7C30', 0x1D7C30, ''), ('w_001D2910', 0x1D2910, 'i>i'),
    ('w_001E0D70', 0x1E0D70, ''), ('w_001DDA00', 0x1DDA00, ''), ('w_001CB800', 0x1CB800, 'uuuu'),
    ('w_001E2260', 0x1E2260, 'q'), ('w_001E0CF0', 0x1E0CF0, ''), ('w_001D5370', 0x1D5370, ''),
    ('w_0011E748', 0x11E748, 'f>f'), ('w_0011E398', 0x11E398, 'f>f'),
    ('w_skin_arena_init', 0x1D2E20, ''), ('w_001D9720', 0x1D9720, ''), ('w_001DD940', 0x1DD940, ''),
    ('w_001E0C30', 0x1E0C30, ''), ('w_001D9060', 0x1D9060, ''), ('w_001D71F0', 0x1D71F0, ''),
    ('w_001D7BB0', 0x1D7BB0, ''), ('w_001D2DE0', 0x1D2DE0, 'ii'), ('w_001E0CC0', 0x1E0CC0, ''),
    ('w_001E0380', 0x1E0380, ''),
    ('w_001D8130', 0x1D8130, 'iu'), ('w_001D8340', 0x1D8340, 'iuuiu'), ('w_001D8690', 0x1D8690, 'uuui'),
    ('w_001C6120', 0x1C6120, 'uu>u'),
)
BY_ADDRESS = {address: (field, sig) for field, address, sig in WORKERS}


def jal_targets(elf, routines):
    ee = EE(elf)
    targets = set()
    for start in routines:
        for pc in range(start, start + SIZES[start], 4):
            word = ee.load(pc)
            if word >> 26 in (2, 3):
                targets.add((word & 0x3FFFFFF) << 2)
    return targets


def check_callee_set(elf):
    """Every jal/j target inside the translated routines is translated here
    or a worker, and every worker is such a target."""
    targets = jal_targets(elf, TRANSLATED)
    missing = sorted(t for t in targets if t not in BY_ADDRESS and t not in TRANSLATED)
    assert not missing, ('callees neither hooked nor translated', [hex(t) for t in missing])
    unused = sorted(a for a in BY_ADDRESS if a not in targets)
    assert not unused, ('workers no translated routine calls', [hex(a) for a in unused])
    return len(targets)


def branch_sites(elf):
    ee = EE(elf)
    sites = []
    for start in sorted(TRANSLATED):
        for pc in range(start, start + SIZES[start], 4):
            if ee.branch(ee.load(pc), pc) is not None:
                op = ee.load(pc) >> 26
                rs, rt = ee.load(pc) >> 21 & 31, ee.load(pc) >> 16 & 31
                if op == 4 and rs == rt:         # unconditional b
                    continue
                sites.append(pc)
    return sites


# ======================================================================
# Native side (ctypes)
# ======================================================================

I32, U32, U64, VP = C.c_int32, C.c_uint32, C.c_uint64, C.c_void_p
CTYPE = {'i': I32, 'u': U32, 'f': U32, 'q': U64}


def fn_type(sig):
    args, _, result = sig.partition('>')
    types = [VP] + [CTYPE[c] for c in args]
    if result:
        types.append(C.POINTER(CTYPE[result]))
    return C.CFUNCTYPE(C.c_int, *types)


class View(C.Structure):
    _fields_ = [('address', U32), ('size', U32), ('bytes', VP), ('writable', C.c_int)]


class Workers(C.Structure):
    _fields_ = [('ctx', VP)] + [(field, fn_type(sig)) for field, _, sig in WORKERS]


class Fault(C.Structure):
    _fields_ = [('address', U32), ('code', I32), ('data', U32)]


class Frh(C.Structure):
    _fields_ = [('views', C.POINTER(View)), ('view_count', U32), ('workers', Workers),
                ('fault', Fault), ('fn', U32)]


ENTRY_SIGNATURES = {
    'em_frh_001D1C50': [], 'em_frh_001D1EA0': [I32], 'em_frh_001D1EF0': [], 'em_frh_001D19E0': [],
    'em_frh_001D19D0': [], 'em_frh_001D9070': [], 'em_frh_001D2830': [I32, I32, C.POINTER(I32)],
    'em_frh_001D2960': [U32], 'em_frh_001D2D20': [C.POINTER(U32), U32, U32, U32, U32, U32],
    'em_frh_001D25F0': [U32], 'em_frh_001D2590': [U32, U32], 'em_frh_001D2610': [U32],
    'em_frh_001C1D00': [U32], 'em_frh_001D30A0': [], 'em_frh_001D8060': [I32, C.POINTER(U32)],
    'em_frh_001D80B0': [I32], 'em_frh_001D88B0': [U32, U32, U32, U32],
    'em_frh_001D8C30': [I32, U32, U32, U32],
}


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / ('frame_render_heads.dylib' if sys.platform == 'darwin' else 'frame_render_heads.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc', 'src/game/em_frame_render_heads.c',
                    '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    for name, args in ENTRY_SIGNATURES.items():
        fn = getattr(native, name)
        fn.argtypes = [C.POINTER(Frh)] + args
        fn.restype = C.c_int
    return native


# ======================================================================
# Worker behaviour (the same object serves both sides)
# ======================================================================

def F(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


class Leaf:
    """sqrtf / tanf: the original 0011E748 / 0011E398 executed in their own
    interpreter over the ELF image (so their errno-style writes land nowhere
    the comparison looks)."""

    def __init__(self, elf):
        self.ee = FrhEE(elf)
        self.cache = {}

    def __call__(self, address, x):
        key = (address, x)
        if key not in self.cache:
            ee = self.ee
            ee.r[29] = 0x7F0F0000
            ee.f[12] = x
            ee.r[31] = RETURN
            ee.run(address)
            self.cache[key] = ee.f[0] & MASK
        return self.cache[key]


class Script:
    """Per-case worker results: seeded by (case, call index, worker)."""

    def __init__(self, seed, leaf, knobs):
        self.seed, self.leaf, self.knobs, self.count = seed, leaf, knobs, 0

    def result(self, field, address, args):
        rng = random.Random('%d:%d:%s' % (self.seed, self.count, field))
        self.count += 1
        k = self.knobs
        if field in ('w_0011E748', 'w_0011E398'):
            return self.leaf(address, args[0])
        if field in ('w_001D2730', 'w_001E0C80'):
            return rng.choice((0, 0, 1, -1, 0x40))
        if field == 'w_001B0070':
            return k.get('flags', rng.choice((0, 0x80, 0x20081910, 0x20081990, 0xFFFFFF7F)))
        if field == 'w_0015D2F0':
            return k.get('variant', rng.choice((0, 1, 2, 2, 3)))
        if field == 'w_001D2910':
            return k.get('option', rng.choice((0, 0, 1, 0x10)))
        if field == 'w_001C6120':
            return k['model']
        return None


class Side:
    """One side's worker log and behaviour. In unit mode the script supplies
    results; in route mode each worker runs the ORIGINAL callee in `ee`
    (except the recorded boundaries)."""

    def __init__(self, script, ee=None, boundaries=()):
        self.script, self.ee, self.boundaries, self.log = script, ee, set(boundaries), []

    def call(self, field, address, sig, args):
        self.log.append((field,) + tuple(args))
        if self.script is not None:
            return self.script.result(field, address, args)
        if address in self.boundaries:
            return 0
        ee = self.ee
        ints = [a for a, c in zip(args, sig.partition('>')[0]) if c in 'iuq']
        floats = [a for a, c in zip(args, sig.partition('>')[0]) if c == 'f']
        saved = (list(ee.r), list(ee.rh), ee.hi, ee.lo, list(ee.f), ee.acc, ee.cond,
                 [list(v) for v in ee.vf], list(ee.vacc), ee.q)
        ee.r[29] = (ee.r[29] - 0x400) & ~15
        for i, value in enumerate(ints):
            ee.r[4 + i] = value & 0xFFFFFFFFFFFFFFFF if sig.startswith('q') else sx32(value)
        for i, value in enumerate(floats):
            ee.f[12 + i] = value & MASK
        ee.r[31] = RETURN
        # Inside the original callee no worker hook applies (it runs as it
        # is); only the boundaries stay recorded, on both sides alike.
        hooks, ee.hooks = ee.hooks, {b: self.inner(b) for b in self.boundaries}
        try:
            ee.run(address)
        except AssertionError as e:
            raise AssertionError(('original callee not interpretable', field, e.args)) from None
        finally:
            ee.hooks = hooks
        v0, f0 = ee.r[2], ee.f[0]
        (ee.r, ee.rh, ee.hi, ee.lo, ee.f, ee.acc, ee.cond, ee.vf, ee.vacc, ee.q) = saved
        result = sig.partition('>')[2]
        if result == 'f':
            return f0 & MASK
        if result in ('i', 'u'):
            return v0 & MASK
        return None


def _inner_method(self, address):
    def hook(ee):
        self.log.append(('inner', hex(address), ee.r[4] & MASK, ee.r[5] & MASK, ee.r[6] & MASK, ee.r[7] & MASK))
        ee.r[2] = 0
    return hook


Side.inner = _inner_method


def oracle_hook(side, field, address, sig):
    args_sig, _, result = sig.partition('>')

    def hook(ee):
        gi, fi, args = 4, 12, []
        for c in args_sig:
            if c == 'f':
                args.append(ee.f[fi] & MASK); fi += 1
            elif c == 'q':
                args.append(ee.r[gi] & 0xFFFFFFFFFFFFFFFF); gi += 1
            else:
                args.append(ee.r[gi] & MASK); gi += 1
        value = side.call(field, address, sig, args)
        if result == 'f':
            ee.f[0] = value & MASK
        elif result:
            ee.r[2] = sx32(value)
    return hook


def native_callbacks(side):
    """Workers struct whose slots call side.call; the ctypes objects are
    returned too so they stay alive."""
    keep, slots = [], {}
    for field, address, sig in WORKERS:
        args_sig, _, result = sig.partition('>')

        def cb(_ctx, *raw, field=field, address=address, sig=sig, args_sig=args_sig, result=result):
            values = [int(v) & (0xFFFFFFFFFFFFFFFF if c == 'q' else MASK)
                      for v, c in zip(raw, args_sig)]
            value = side.call(field, address, sig, values)
            if result:
                out = raw[len(args_sig)]
                out[0] = sx32(value) if result == 'i' else value & MASK
            return 0
        f = fn_type(sig)(cb)
        keep.append(f)
        slots[field] = f
    workers = Workers(None, *[slots[field] for field, _, _ in WORKERS])
    return workers, keep


def make_frh(ram, spad, workers):
    rb = (C.c_uint8 * len(ram)).from_buffer(ram)
    sb = (C.c_uint8 * len(spad)).from_buffer(spad)
    views = (View * 4)(View(0x00000000, len(ram), C.cast(rb, VP), 1),
                       View(0x20000000, len(ram), C.cast(rb, VP), 1),
                       View(0x30000000, len(ram), C.cast(rb, VP), 1),
                       View(0x70000000, len(spad), C.cast(sb, VP), 1))
    h = Frh(views, 4, workers, Fault(0, 0, 0), 0)
    return h, (rb, sb, views)


# ======================================================================
# Unit cases
# ======================================================================

SCRATCH = 0x01F00000        # a RAM area the capture does not use by this code
MODEL = 0x01F40000          # a synthetic 0x16 model block for 001D9070


def rw(ram, address, value, size=4):
    ram[address & 0x1FFFFFF:(address & 0x1FFFFFF) + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'little')


def ru(ram, address, size=4):
    return int.from_bytes(ram[address & 0x1FFFFFF:(address & 0x1FFFFFF) + size], 'little')


FLOATS = (0.0, -0.0, 1.0, -1.0, 0.5, 224.0, 480.0, 450.0, 449.99997, 450.00003, 1e-38, 3.0e38,
          -3.0e38, 0.3, 1e-45, 190.0, 57.0, 57.000004, 56.999996, -12.5, 2.0, 1280.0)


def rfloat(rng):
    r = rng.random()
    if r < 0.45:
        return F(rng.choice(FLOATS))
    if r < 0.9:
        return F(rng.uniform(-1000, 1000))
    return rng.getrandbits(32)       # any pattern, NaN/Inf/denormal included


ENTRIES = ('1C50', '1EA0', '1EF0', '19E0', '19D0', '9070', '2830', '2960', '2D20', '25F0', '2590',
           '2610', '1C1D00', '30A0', '8060', '80B0', '88B0', '8C30')
WEIGHTS = (6, 3, 2, 2, 1, 3, 3, 3, 3, 1, 2, 4, 3, 2, 3, 3, 4, 6)


def make_case(seed, ctx, slots):
    """(entry, pokes, args, knobs): the pokes apply to both sides."""
    rng = random.Random(seed)
    entry = rng.choices(ENTRIES, WEIGHTS)[0]
    pokes, knobs = [], {}

    def poke(address, value, size=4):
        pokes.append((address, value, size))

    # The flags the heads read, the slot index and the context floats.
    poke(0x8106C4, rng.choice((0, 0, 0, 1, 2)), 1)
    poke(0x8106C6, rng.choice((0, 2, 2, 1)), 1)
    poke(0x8106C7, rng.choice((0, 0, 1)), 1)
    area = rng.choice((0x0B, 0x0B, 8, 0x11, 0x15, 0x15))
    poke(0x810700, area, 1)
    poke(0x810701, rng.choice((0, 0, 0, 1)), 1)
    poke(0x70003B8D, rng.choice((0, 0, 4, 2, 0xFF)), 1)
    poke(ctx + 0x9C, rng.choice((0, 1)))
    poke(ctx + 0x2468, rfloat(rng))
    poke(ctx + 0xF8, rfloat(rng))
    poke(ctx + 0xFC, rfloat(rng))
    poke(ctx + 0x246C, rng.choice((0, 1, 2, 3, 4, 5, 6, 7, 8, 0xFFFFFFFF)))
    for i in range(2):
        poke(ctx + 0xB0 + 4 * i, rng.getrandbits(32))
    for i in range(4):
        poke(ctx + 0xA0 + 4 * i, rfloat(rng))
    for i in range(16):
        poke(0x810610 + 4 * i, rfloat(rng) if rng.random() < 0.8 else F(rng.uniform(-2, 2)))
    for i in range(16):
        poke(0x70003AC0 + 4 * i, rfloat(rng))
    knobs['flags'] = rng.choice((0, 0x80, 0x20081910, 0x20081990))
    knobs['variant'] = rng.choice((0, 2, 2, 1))
    knobs['option'] = rng.choice((0, 1))
    if rng.random() < 0.3:
        del knobs['flags']
    args = ()
    if entry == '1EA0':
        args = (rng.choice((0, 1, 1, 5, -1)),)
    elif entry == '2830':
        args = (rng.choice((-5, 0, 3, 0x1F, 0x20, 0x24, 0x3F, 0x40, 0x7FFFFFFF, -0x80000000)),
                rng.choice((0, 1, -1, 0x55)))
    elif entry == '2960':
        args = (rng.choice((0x810610, 0x810610, SCRATCH + 0x100, SCRATCH + 0x104)),)
        for i in range(16):
            poke(SCRATCH + 0x100 + 4 * i, rfloat(rng))
    elif entry == '2D20':
        args = tuple(rfloat(rng) for _ in range(5))
        if rng.random() < 0.2:
            args = (args[0], args[1], args[2], args[3], args[3])       # near == far
    elif entry in ('25F0', '2610'):
        args = (rng.choice((F(0.0), F(1.0), F(0.5), F(-0.25), F(1.5), rfloat(rng))),)
    elif entry == '2590':
        args = (rng.choice((F(224.0), rfloat(rng))), rng.choice((F(0.8726646), F(0.0), rfloat(rng))))
    elif entry == '1C1D00':
        address = rng.choice((0x8101D0, SCRATCH + 0x33))
        poke(address, rng.choice((0, 1, 1, 2, 0xFF)), 1)
        args = (address,)
    elif entry in ('8060', '80B0'):
        ids = [rng.choice((-1, 0, 5, 7, 0x1234, 31)) for _ in range(32)]
        for i, value in enumerate(ids):
            if rng.random() < 0.5:
                poke(ctx + 0x220 + 0x80 * i + 0xC, value)
        args = (rng.choice((-1, 0, 5, 7, 0x1234, 99, 31)),)
    elif entry in ('88B0', '8C30'):
        m = rng.choice((SCRATCH, SCRATCH + 0x40, SCRATCH + 0x44, SCRATCH + 0x10))
        out = rng.choice((SCRATCH + 0x80, SCRATCH + 0x80, SCRATCH + 0x10, SCRATCH))
        vin = rng.choice((SCRATCH + 0xC0, SCRATCH + 0xC0, SCRATCH + 0x80, SCRATCH + 0x8C))
        for off in range(0, 0x100, 4):
            poke(SCRATCH + off, rfloat(rng))
        w = rng.choice((F(1.0), F(1.0000001), F(0.99999994), F(2.5), F(0.0), rfloat(rng)))
        poke(vin + 0xC, w)
        if entry == '8C30':
            args = (rng.choice((-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 0x7FFFFFFF)), m, out, vin)
        else:
            args = (rng.getrandbits(32), m, out, vin)
    if entry in ('9070', '19D0'):
        knobs['model'] = MODEL
        poke(0x28A56C, rng.choice((0xBAA1C0, 0x123450)))
        groups = rng.choice((0, 1, 1, 2, 3))
        poke(MODEL, groups)
        for g in range(groups):
            for j in range(32):
                e = MODEL + 0x50 + 0x820 * g + 0x40 * j
                t = rng.choice((-1.0, -0.0, 0.0, 0.1, 0.3, 0.30000001, 0.29999998, 0.5, 1.0, 2.0, None))
                poke(e + 0x38, F(190.0 * t) if t is not None else rfloat(rng))
    return entry, pokes, args, knobs


ORIGINAL_ENTRY = {
    '1C50': 0x1D1C50, '1EA0': 0x1D1EA0, '1EF0': 0x1D1EF0, '19E0': 0x1D19E0, '19D0': 0x1D19D0,
    '9070': 0x1D9070, '2830': 0x1D2830, '2960': 0x1D2960, '2D20': 0x1D2D20, '25F0': 0x1D25F0,
    '2590': 0x1D2590, '2610': 0x1D2610, '1C1D00': 0x1C1D00, '30A0': 0x1D30A0, '8060': 0x1D8060,
    '80B0': 0x1D80B0, '88B0': 0x1D88B0, '8C30': 0x1D8C30,
}
FLOAT_ARGS = {'2D20': 5, '25F0': 1, '2590': 2, '2610': 1}


def run_original(ee, entry, args):
    """Call the original entry; returns (v0, f0)."""
    ee.r[29] = 0x7F0F0000
    if entry == '2D20':
        ee.r[4] = SCRATCH + 0x200
        for i, value in enumerate(args):
            ee.f[12 + i] = value & MASK
    elif entry in FLOAT_ARGS:
        for i, value in enumerate(args):
            ee.f[12 + i] = value & MASK
    else:
        for i, value in enumerate(args):
            ee.r[4 + i] = sx32(value)
    ee.r[31] = RETURN
    ee.run(ORIGINAL_ENTRY[entry])
    return ee.r[2] & MASK, ee.f[0] & MASK


def run_native(native, h, entry, args, nram):
    name = 'em_frh_' + ('001C1D00' if entry == '1C1D00' else '001D' + entry)
    fn = getattr(native, name)
    ref = C.byref(h)
    if entry == '2830':
        out = I32(0x5A5A5A5A)
        status = fn(ref, *args, C.byref(out))
        return status, out.value & MASK
    if entry == '8060':
        out = U32(0x5A5A5A5A)
        status = fn(ref, *args, C.byref(out))
        return status, out.value
    if entry == '2D20':
        m = (U32 * 16)()
        status = fn(ref, m, *args)
        for i in range(16):
            rw(nram, SCRATCH + 0x200 + 4 * i, m[i])
        return status, None
    return fn(ref, *[a & MASK if isinstance(a, int) and a < 0 and entry not in ('1EA0', '80B0')
                     else a for a in args]), None


BASE = {}


def base_image():
    if not BASE:
        BASE['ram'] = (ROUTE / BASE_BEAT / 'eeMemory.bin').read_bytes()
        BASE['spad'] = (ROUTE / BASE_BEAT / 'scratchpad.bin').read_bytes()
    return BASE['ram'], BASE['spad']


def run_case(seed):
    ram0, spad0 = base_image()
    oee = FrhEE(ELF, ram0, spad0)
    oee.outcomes = set()
    ctx = oee.load(0x275670)
    entry, pokes, args, knobs = make_case(seed, ctx, oee.load(0x275674))
    nram, nspad = bytearray(ram0), bytearray(spad0)
    for address, value, size in pokes:
        oee.save(address, value, size)
        if address >= 0x70000000:
            rw(nspad, address - 0x70000000, value, size)
        else:
            rw(nram, address, value, size)
    # Original.
    oscript = Script(seed, LEAF, knobs)
    oside = Side(oscript)
    for field, address, sig in WORKERS:
        oee.hooks[address] = oracle_hook(oside, field, address, sig)
    v0, _ = run_original(oee, entry, args)
    # Native.
    nside = Side(Script(seed, LEAF, knobs))
    workers, keep = native_callbacks(nside)
    h, alive = make_frh(nram, nspad, workers)
    status, value = run_native(NATIVE, h, entry, args, nram)
    where = (seed, entry, [hex(a & MASK) for a in args])
    assert status == 0 and h.fault.code == 0, (where, 'native fault', status, hex(h.fault.address),
                                                h.fault.code, hex(h.fault.data))
    assert oside.log == nside.log, (where, 'worker calls differ', oside.log, nside.log)
    if entry == '2830':
        assert value == v0, (where, '001D2830 v0', hex(v0), hex(value))
    if entry == '8060':
        assert value == v0, (where, '001D8060 v0', hex(v0), hex(value))
    if bytes(oee.spad) != bytes(nspad):
        diff = [hex(0x70000000 + i) for i in range(len(nspad)) if oee.spad[i] != nspad[i]]
        raise AssertionError((where, 'scratchpad differs', diff[:16]))
    if oee.mem != nram:
        diff = [hex(i) for i in range(len(nram)) if oee.mem[i] != nram[i]]
        raise AssertionError((where, 'RAM differs', diff[:16], len(diff)))
    del keep, alive
    if entry == '8C30':
        entry = '8C30/%d' % (args[0] if 0 <= args[0] < 7 else 7)
    return entry, len(oside.log), oee.outcomes


# ======================================================================
# Fail-stop: every reached worker NULL, and an unmapped context
# ======================================================================

NEEDS = {
    '1C50': ('w_001D2730', 'w_001B0070', 'w_0015D2F0', 'w_0021B970', 'w_0021B9A0', 'w_0021BA80',
             'w_001D7C30', 'w_0011E748'),
    '1EA0': ('w_001CB800', 'w_001D2910', 'w_001E0D70', 'w_001DDA00'),
    '19E0': ('w_skin_arena_init', 'w_001D9720', 'w_001DD940', 'w_001E0C30', 'w_001D9060', 'w_001D71F0',
             'w_001D7BB0', 'w_001D2730', 'w_001E0C80', 'w_001D2DE0', 'w_001E0CC0', 'w_001E0380'),
    '9070': ('w_001C6120',), '2960': ('w_0011E748',), '2590': ('w_0011E398',),
    '2610': ('w_0011E398', 'w_001B0070', 'w_0021B970'),
    '1C1D00': ('w_001D2910', 'w_001E2260', 'w_001E0CF0', 'w_001D5370'),
}
NEED_ARGS = {'1C50': (), '1EA0': (1,), '19E0': (), '9070': (), '2960': (0x810610,), '2590': (F(224.0), F(0.8)),
             '2610': (F(0.5),), '1C1D00': (0x8101D0,)}


def missing_worker_checks():
    ram0, spad0 = base_image()
    count = 0
    for entry, fields in NEEDS.items():
        for missing in fields:
            nram, nspad = bytearray(ram0), bytearray(spad0)
            rw(nram, 0x28A56C, 0xBAA1C0)
            before = bytes(nram)
            side = Side(Script(1, LEAF, {'model': MODEL, 'flags': 0, 'variant': 2, 'option': 0}))
            workers, keep = native_callbacks(side)
            setattr(workers, missing, fn_type(dict((f, s) for f, _, s in WORKERS)[missing])())
            h, alive = make_frh(nram, nspad, workers)
            status, _ = run_native(NATIVE, h, entry, NEED_ARGS[entry], nram)
            address = [a for f, a, _ in WORKERS if f == missing][0]
            assert status == -1 and h.fault.code == 1 and h.fault.data == address, (
                entry, missing, status, h.fault.code, hex(h.fault.data))
            assert not side.log, (entry, missing, 'a worker ran before the refusal', side.log)
            assert bytes(nram) == before and nspad == spad0, (entry, missing, 'memory changed before the refusal')
            # The latch: a later call refuses too.
            assert NATIVE.em_frh_001D25F0(C.byref(h), F(1.0)) == -1
            count += 1
            del keep, alive
    # An unmapped context: D_00275670 pointing outside every view.
    nram, nspad = bytearray(ram0), bytearray(spad0)
    rw(nram, 0x275670, 0x03000000)
    snapshot = bytes(nram)
    side = Side(Script(1, LEAF, {'flags': 0, 'variant': 2, 'option': 0}))
    workers, keep = native_callbacks(side)
    h, alive = make_frh(nram, nspad, workers)
    assert NATIVE.em_frh_001D1C50(C.byref(h)) == -1 and h.fault.code == 4, (h.fault.code,)
    assert bytes(nram) == snapshot and not side.log, 'unmapped context: something ran'
    return count + 1


# ======================================================================
# Route mode (EM_TEST_WORLD=1)
# ======================================================================

# Route-mode boundaries, recorded (with a0..a3) and returning 0 on both
# sides: the GS/DMA kick 001CB800, the world flush pair 001E0D70/001DDA00,
# and 001D5370 (lane L30), whose clip test uses VCLIP, which the shared
# interpreter does not model.
BOUNDARIES = (0x1CB800, 0x1E0D70, 0x1DDA00, 0x1D5370)
ROUTE_PASSES = 2


def route_beat(beat):
    ram0 = (ROUTE / beat / 'eeMemory.bin').read_bytes()
    spad0 = (ROUTE / beat / 'scratchpad.bin').read_bytes()
    # Original pass.
    oee = FrhEE(ELF, ram0, spad0)
    oside = Side(None, oee, BOUNDARIES)
    for field, address, sig in WORKERS:
        oee.hooks[address] = oracle_hook(oside, field, address, sig)
    # Native pass: the translations operate on nee's own memory.
    nee = FrhEE(ELF, ram0, spad0)
    nside = Side(None, nee, BOUNDARIES)
    workers, keep = native_callbacks(nside)
    h, alive = make_frh(nee.mem, nee.spad, workers)
    ref = C.byref(h)
    steps = []
    for n in range(ROUTE_PASSES):
        steps.append(('1C50', ()))
        steps.append(('1C1D00', (0x8101D0,)))
        steps.append(('1EA0', (1,)))
    steps.append(('2610', (F(0.0),)))
    steps.append(('2610', (F(1.0),)))
    ctx = oee.load(0x275670)
    steps.append(('88B0', (SCRATCH + 0x100, SCRATCH, SCRATCH + 0x40, 0x810610)))
    for entry, args in steps:
        run_original(oee, entry, args)
        status, _ = run_native(NATIVE, h, entry, args, nee.mem)
        assert status == 0, (beat, entry, 'native fault', hex(h.fault.address), h.fault.code, hex(h.fault.data))
        assert oside.log == nside.log, (beat, entry, 'worker calls differ')
        assert bytes(oee.spad) == bytes(nee.spad), (beat, entry, 'scratchpad differs')
        if oee.mem != nee.mem:
            diff = [hex(i) for i in range(len(oee.mem)) if oee.mem[i] != nee.mem[i]]
            raise AssertionError((beat, entry, 'RAM differs', diff[:16], len(diff)))
    del keep, alive
    return beat, len(steps), len(oside.log), hex(ctx), oee.load(ctx + 0x246C)


def world_main(setup=True):
    global ELF, NATIVE, LEAF
    started = time.time()
    if setup:
        ELF = read_elf()
        NATIVE = build_native()
        LEAF = Leaf(ELF)
    beats = sorted(p.name for p in ROUTE.iterdir() if (p / 'eeMemory.bin').exists())
    beats = reference_mode.select(beats, 3, 0xB7, keep=lambda i, b: b in ('00_panel_no_battery',
                                                                          '14_roger_encounter'))
    results = reference_mode.parallel_map(route_beat, beats)
    for beat, steps, calls, ctx, mode in results:
        print('%s: PASS %d head calls identical to the original (whole RAM + scratchpad), %d worker '
              'calls (original callees) identical; context %s, lighting mode %d' % (beat, steps, calls, ctx, mode))
    reference_mode.banner(reference_mode.part(len(beats), len(list(ROUTE.iterdir())), 'route beats'))
    print('frame render heads route mode: PASS (%.1fs)' % (time.time() - started))


# ======================================================================
# Unit main
# ======================================================================

# Outcomes no input can produce (each reason is arithmetic on the original's
# own guards): in 001D9070 the clamps run only for 0 <= t <= 0.3 (a negative
# t, including -0 and a negative denormal under DAZ, took the t < 0 branch or
# compares equal to 0), so v = 1 - t lies in [0.7, 1]: v < 0 and v > 1 never
# hold.
EXEMPT = {(0x1D9120, False): 'v = 1 - t < 0 with t <= 0.3',
          (0x1D9138, False): 'v = 1 - t > 1 with t >= 0'}


def main():
    global ELF, NATIVE, LEAF
    started = time.time()
    ELF = read_elf()
    callees = check_callee_set(ELF)
    NATIVE = build_native()
    LEAF = Leaf(ELF)
    total = 6000
    seeds = reference_mode.select(range(total), 700, 0xF2A1)
    results = reference_mode.parallel_map(run_case, seeds)
    outcomes, entries, calls = set(), {}, 0
    for entry, count, cover in results:
        outcomes |= cover
        entries[entry] = entries.get(entry, 0) + 1
        calls += count
    sites = branch_sites(ELF)
    missing = sorted('%06X %s' % (pc, 'taken' if taken else 'not taken')
                     for pc in sites for taken in (True, False)
                     if (pc, taken) not in outcomes and (pc, taken) not in EXEMPT)
    assert not missing, ('branch outcomes never exercised', missing)
    absent = [e for e in ENTRIES if e != '8C30' and e not in entries]
    absent += ['8C30/%d' % k for k in range(8) if '8C30/%d' % k not in entries]
    assert not absent, ('entry points never run', absent)
    stops = missing_worker_checks()
    reference_mode.banner(reference_mode.part(len(seeds), total, 'cases'),
                          '%d jal targets (all hooked or translated)' % callees)
    print('frame render heads vs original instructions: PASS %d cases (%s), %d worker calls identical, '
          'whole RAM + scratchpad identical after every case, every one of %d conditional branches '
          'both ways (%d unreachable outcomes exempt, reasons in EXEMPT), %d fail-stop refusals (%.1fs)' % (
              len(seeds), ', '.join('%s %d' % kv for kv in sorted(entries.items())), calls,
              len(sites), len(EXEMPT), stops, time.time() - started))
    world_main(setup=False)


if __name__ == '__main__':
    if os.environ.get('EM_TEST_WORLD', '') not in ('', '0'):
        world_main()
    else:
        main()
