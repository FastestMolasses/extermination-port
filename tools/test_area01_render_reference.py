#!/usr/bin/env python3
"""Execute the ORIGINAL AREA01 render-lane routines and compare the native
em_area01_render_* modules byte for byte.

docs/AREA01_RENDER.md. The user's pinned ELF and the recorded AREA01 route
captures (../Extermination/build/s87/route_a01/<beat>/{eeMemory,scratchpad}.bin)
supply every instruction and every table; none are embedded here.

Translated (native) routines, each run as ORIGINAL code on the oracle side:
  vif    001D5BD0 001D5A70 001D4FC0 001D5170
  gs     001CD070 001CD180 001CD2B0 001F4A10 001F4CC0
  hud    001E8B90 001E9E60 001EAF00 001EAF80 001EB020 001EC270
  frame  0015B610
The leaves 00102948, 00102958 and 001CD370 run inline on both sides.

How a case runs. The oracle executes the original routine over the case's
RAM + scratchpad; every other callee (the "workers") runs as ORIGINAL code
too, nested, and its arguments are logged. The native module runs over a
second copy; each of its worker calls runs the same ORIGINAL callee in a
second interpreter that shares the native side's memory, and is logged the
same way. Before every worker call (the entry check) and after the last step
(the end check) the native RAM (32 MiB) and scratchpad (16 KiB) must equal
the oracle's at the same point, with the logged arguments, the return value
and the worker log. 0015B610's workers are player routines; they are
scripted boundaries (logged, results chosen by the case) on both sides.

Only executions the EE completes are run: both interpreters refuse (and the
case fails) on a misaligned access or an address outside the TLB map of the
AREA01 captures (A01EE, MAPPED).

Memory is not copied per case: each worker process keeps its RAM buffers and
resets only the pages that changed; the native buffer is write-tracked per
page (TRACKER_C), and the checks compare exactly the pages either side wrote
since the previous check, which given equality there is equality of all of
RAM (EM_A01R_FULLCMP=1 re-checks every such claim with a full compare).

Cases:
  capture  each routine at its real call shape over every AREA01 beat: the
           table pass 001D5BD0 as captured; 001F4A10 on the beat's 00158D30
           / 00158BD0 node with both of the two 0x700038B0 word sets;
           001F4CC0 with its caller's spad arguments; 001E9E60 on every
           0015A2C0 node (a1 = node +0xE); the four effect handlers on every
           001EA240 node (work = node +0x1F0, a0 = node +0xD0); 001E8B90 at
           the player's position; 001CD070 + 001CD2B0 as 001F8350 pairs them.
  unit     the same routines over perturbed state: record counts across the
           0x1F8 chunk boundary, moved clip points, other slot indices,
           positions inside / on / outside the 001E8B90 record boxes and
           their cell clamps, other area bytes, blend and seed words, extents
           and f14 / f15 ranges, special float patterns.
  boundary 001CD2B0 at t = n - f14 = 0 with f15 = +-0, at v = t / f15 just
           above, on and below 1.0, and with a 25-bit n;
           001CD180 at and one past each of its four bounds; the quadword at
           context +0xA0 through 001CD070, 001F4A10 and 001E9E60; direct
           001D5A70 results; the first and last bytes of each native view;
           001E8B90 box bounds where truncating and rounding adds differ,
           sub-normal coordinates, nonzero live words with a zero low byte
           or the sign bit set, an exponent-255 width and a depth whose
           32 * (p.z - z0) passes MAX, and cells at mixed box corners (add
           order); 001E9E60's record byte +0x5C = 1 with the bytes above it
           set, and +0x5C = 0x81 or 0x41;
           001CD180 with x - hw and y + hh wrapping in 32 bits.
           Quadword loads through addresses 1, 2 and 3 bytes past a
           quadword (001CD070's p, 001F4A10's subject).
  alias    the channel-3 tag stores landing on the cursor word (context
           +0x18, +0x1C, +0x19; relocated contexts); stores landing on
           D_00275670 before each of its re-reads (the context pointer);
           the words re-read after 00122BB8, 001E8B90's cell stores and
           001CB950; the words a tag store changes before they are read
           (001D5BD0's count, a record's count and vector, D_00275B44,
           D_0028A56C); cursor stores carrying past 64 KiB; 001E9E60's
           packets 8 bytes off a quadword and in the scratchpad over their
           own copy source; packets 4 bytes off a quadword (the
           5-quadword packet's copy realigns the cursor for the last one);
           00122BB8's LCG store landing on 001F4A10's v + 0; 001E9E60's
           packet word copy landing on the blend t (a0 + 0x80).
  frame    0015B610 over every scratchpad byte / +5 state / check result
           combination plus three edge values, and the state-setting paths
           with the player's bytes +7 and +0x1F1 nonzero (114 cases).
  faults   native only, at all 16 entries: every reachable NULL worker, a
           latched fault, no views and NULL view bytes (the fault must name
           the original's first data address); accesses wrapping past
           2**32; a RAM view ending exactly at 001E8B90's first access and
           one starting just after 001CD2B0's store of n (fault address,
           detail and function); a failing worker at each call position.
Every conditional branch of the 16 routines must be taken both ways.

Arithmetic: tools/ee_float_model.py through the shadow-route interpreter
(RouteEE: every COP1 and VU0 macro op), plus the VU clip test with the rule
docs/RENDER_CONTEXT.md section 4 states (DAZ, magnitude compares against
|w|, exponent 255 unmeasured) and the CLIP register read.

Quick mode (the default) runs a covering sample; EM_TEST_FULL=1 runs every
generated case. At most four worker processes (EM_TEST_JOBS=1: serial).
Measured times are in docs/AREA01_RENDER.md section 3.
"""
import ctypes as C
import mmap
import multiprocessing
import os
import random
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode as RM  # noqa: E402
import area01_reference_view as AV  # noqa: E402
import test_player_slide_reference as shared  # noqa: E402
import test_shadow_actor_route_reference as SAR  # noqa: E402
from test_player_slide_reference import EE, read_elf, sx32, s32, RETURN, STACK_TOP  # noqa: E402

MASK, MASK64 = 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF
DECOMP = ROOT.parent / 'Extermination'
ROUTE = DECOMP / 'build/s87/route_a01'
OUT = ROOT / 'build' / 'area01' / os.environ.get('EM_LANE', 'render')
VEC = 0x7F0E0000          # the worker interpreter's private stack slot for vector arguments
PLAYER = 0x8102B0
PAGE = mmap.PAGESIZE      # the unit of the dirty-page bookkeeping and compares
# At most four worker processes (EM_TEST_JOBS=1 runs serially).
JOBS = max(1, min(4, int(os.environ.get('EM_TEST_JOBS', '') or 4)))
# EM_A01R_FULLCMP=1: also compare all 32 MiB at every entry check, end check
# and buffer reset, and fail if that disagrees with the page compare (the
# self-check of the dirty-page bookkeeping; slow).
FULLCMP = os.environ.get('EM_A01R_FULLCMP', '') not in ('', '0')

SIZES = {  # the translated routines (original bytes)
    0x1D5BD0: 164, 0x1D5A70: 344, 0x1D4FC0: 424, 0x1D5170: 368,
    0x1CD070: 272, 0x1CD180: 304, 0x1CD2B0: 192, 0x1F4A10: 480, 0x1F4CC0: 120,
    0x1E8B90: 748, 0x1E9E60: 932, 0x1EAF00: 124, 0x1EAF80: 156, 0x1EB020: 560, 0x1EC270: 384,
    0x15B610: 352,
}
INLINE = {0x102948, 0x102958, 0x1CD370}


def F(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


class Unmeasured(Exception):
    pass


# ======================================================================
# Interpreter
# ======================================================================

class NotCompletable(Exception):
    """The EE cannot complete this execution: an address error (a misaligned
    access) or an address that no valid TLB entry of the model maps."""


# The EE's virtual memory map in every AREA01 capture: the 48-entry TLB of
# the 12 route_a01 PCSX2 save states (identical in all 12; the BIOS sets it
# and no boot-ELF or overlay code calls the SetVTLBRefillHandler stub; docs/
# AREA01_RENDER.md section 3). Main RAM is mapped at 0x00080000..0x01FFFFFF
# (0x00000000..0x0007FFFF is not mapped), its uncached view at 0x20080000..
# 0x21FFFFFF and its uncached-accelerated view at 0x30100000..0x31FFFFFF
# (virtual & 0x1FFFFFF is the physical address in all three), and the 16 KiB
# scratchpad. The TLB also maps the EE, VU and GS register pages
# (0x10000000..0x1000FFFF, 0x11000000.., 0x12000000..), 0x1E000000..
# 0x1FFFFFFF and 0xFFFF8000..0xFFFFFFFF (kernel memory, physical 0x78000..
# 0x7FFFF); they are outside this model, so an access there refuses the
# case too. 0x7F000000 is the interpreters' private stack.
MAPPED = ((0x00080000, 0x02000000), (0x20080000, 0x22000000), (0x30100000, 0x32000000),
          (0x70000000, 0x70004000), (0x7F000000, 0x7F100000))
RAM_SIZE, SPAD_SIZE = 0x2000000, 0x4000


def completable(a, size):
    """a (32-bit) if a size-byte access there completes on the EE, else raise."""
    if a & (size - 1):
        raise NotCompletable('address error (misaligned)', hex(a), size)
    for lo, hi in MAPPED:
        if lo <= a and a + size <= hi:
            return a
    raise NotCompletable('no TLB mapping in the model', hex(a), size)


class A01EE(SAR.RouteEE):
    """RouteEE (measured COP1 / VU0) with plain stores, the VU clip test and
    the CLIP register, branch outcomes of the translated routines, logging
    worker hooks, and the EE's access rules: every load, store and
    instruction fetch must be aligned to its size (a quadword access drops
    the low four bits first, as the EE does) and fall in the TLB map above,
    or the case is refused (NotCompletable)."""

    def __init__(self, elf, ram=None, spad=None, journal=True):
        super().__init__(elf, ram, spad)
        self.clip = 0
        self.outcomes = set()
        self.calls = []
        # The store journal: every RAM / scratchpad store in execution order
        # as (is scratchpad, offset, bytes stored); the private stack is not
        # journaled (it is outside both compared regions). `entries` records,
        # at each worker entry, the journal length and the logged arguments,
        # so the native side can rebuild this side's exact RAM + scratchpad
        # at the same call entry. The native side's own interpreter keeps no
        # journal (journal=False).
        self.journal = [] if journal else None
        self.entries = []
        where, stack, spadbuf, jr = self._where, self.stack, self.spad, self.journal

        def save(address, value, size=4):
            a = address & MASK
            if a & (size - 1) or not 0x80000 <= a <= RAM_SIZE - size:
                completable(a, size)
            buf, at = where(a)
            data = (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'little')
            buf[at:at + size] = data
            if jr is not None and buf is not stack:
                jr.append((buf is spadbuf, at, data))

        def write(address, data):
            a = address & MASK
            completable(a, 1)
            completable(a + len(data) - 1, 1)
            buf, at = where(a)
            buf[at:at + len(data)] = data
            if jr is not None and buf is not stack:
                jr.append((buf is spadbuf, at, bytes(data)))
        self.save, self.write = save, write

    def load(self, address, size=4):
        a = address & MASK
        if a & (size - 1) or not 0x80000 <= a <= RAM_SIZE - size:
            completable(a, size)
        buf, at = self._where(a)
        return int.from_bytes(buf[at:at + size], 'little')

    def branch(self, word, pc):
        b = EE.branch(self, word, pc)
        if b is not None:
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            if not (op == 4 and rs == 0 and rt == 0):
                for start, size in SIZES.items():
                    if start <= pc < start + size:
                        self.outcomes.add((pc, b[0]))
                        break
        return b

    def macro(self, word):
        op = word & 63
        if op >= 60 and ((word >> 6 & 31) << 2 | (op & 3)) == 0x1F:           # clip test, x/y/z vs |w|
            assert (word >> 21 & 15) == 0xE, hex(word)
            fs, ft = word >> 11 & 31, word >> 16 & 31
            v = [x & MASK for x in self.vf[fs][:3]] + [self.vf[ft][3] & MASK]
            if any((x >> 23) & 0xFF == 0xFF for x in v):
                raise Unmeasured('clip lane with exponent 255')
            daz = lambda b: b & 0x80000000 if (b >> 23) & 0xFF == 0 else b
            w = daz(v[3]) & 0x7FFFFFFF
            f = 0
            for k in range(3):
                x = daz(v[k])
                if (x & 0x7FFFFFFF) > w:
                    f |= (2 if x >> 31 else 1) << (2 * k)
            self.clip = ((self.clip << 6) | f) & 0xFFFFFF
            return
        super().macro(word)

    def mmi(self, word, pc):
        rs, rt, rd = word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        fn, sub = word & 63, word >> 6 & 31
        full = lambda n: (self.r[n] & MASK64) | (self.rh[n] << 64)
        w = lambda v, i: (v >> (32 * i)) & MASK
        if (fn, sub) in ((0x08, 0x12), (0x28, 0x12)):              # interleave low / high word pairs
            a, b = full(rs), full(rt)
            lo = 0 if fn == 0x08 else 2
            value = w(b, lo) | w(a, lo) << 32 | w(b, lo + 1) << 64 | w(a, lo + 1) << 96
        elif fn == 0x09 and sub in (0x1E, 0x1F):                  # word exchange / three-word rotate of rt
            v = full(rt)
            x = [w(v, i) for i in range(4)]
            x = [x[2], x[1], x[0], x[3]] if sub == 0x1E else [x[1], x[2], x[0], x[3]]
            value = sum(e << (32 * i) for i, e in enumerate(x))
        else:
            return super().mmi(word, pc)
        if rd:
            self.r[rd], self.rh[rd] = value & MASK64, value >> 64

    def cop2(self, word, pc):
        rs, rt, rd = word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        if rs in (2, 6):
            assert rd == 18, ('control register', rd, hex(pc))
            if rs == 2:
                if rt: self.r[rt] = self.clip
            else:
                self.clip = self.r[rt] & 0xFFFFFF
            return
        super().cop2(word, pc)


def vec(ee, address):
    return tuple(ee.load(address + 4 * i) for i in range(4))


A = lambda e, n: e.r[4 + n] & MASK
FR = lambda e, n: e.f[12 + n] & MASK
# worker address -> (argument log from the registers at entry, result used?)
WORKERS = {
    0x121870: (lambda e: (A(e, 0), A(e, 1), A(e, 2), A(e, 3)), False),
    0x1D2090: (lambda e: (A(e, 0), A(e, 1)), False),
    0x1D4750: (lambda e: (A(e, 0),), False),
    0x1D1F80: (lambda e: (A(e, 0), A(e, 1), A(e, 2), A(e, 3)), False),
    0x1CAAC0: (lambda e: (vec(e, A(e, 0)), A(e, 1), A(e, 2), A(e, 3)), False),
    0x1281C0: (lambda e: (FR(e, 0),), True),
    0x122BB8: (lambda e: (), True),
    0x1C7900: (lambda e: (A(e, 0), vec(e, A(e, 1)), A(e, 2), A(e, 3)), False),
    0x1C6120: (lambda e: (A(e, 0), A(e, 1)), True),
    0x1D3990: (lambda e: (A(e, 0),), False),
    0x1CB760: (lambda e: (A(e, 0), A(e, 1), A(e, 2)), False),
    0x21B9A0: (lambda e: (A(e, 0), FR(e, 0), FR(e, 1)), False),
    0x1F4BF0: (lambda e: (A(e, 0), A(e, 1)), False),
    0x1CB5F0: (lambda e: (A(e, 0), A(e, 1), A(e, 2)), True),
    0x1CB950: (lambda e: (A(e, 0), A(e, 1), e.r[6] & MASK64), False),
    0x1CB6B0: (lambda e: (A(e, 0), A(e, 1), A(e, 2), A(e, 3)), False),
    0x1CFB50: (lambda e: (A(e, 0), A(e, 1), A(e, 2)) + tuple(FR(e, i) for i in range(5)), False),
    0x1CFBE0: (lambda e: (A(e, 0), A(e, 1), A(e, 2), A(e, 3), e.r[8] & MASK), False),
}
FRAME_WORKERS = {  # scripted boundaries of 0015B610
    0x182B30: lambda e: (A(e, 0), A(e, 1)),
    0x174A50: lambda e: (A(e, 0), FR(e, 0)),
    0x182D70: lambda e: (A(e, 0),),
    0x183240: lambda e: (A(e, 0),), 0x183250: lambda e: (A(e, 0),), 0x1833F0: lambda e: (A(e, 0),),
    0x183440: lambda e: (A(e, 0),), 0x1834E0: lambda e: (A(e, 0),),
}


def logging_hooks(ee):
    """Every worker runs as original code (nested, no hooks) and is logged."""
    def make(address, fmt, used):
        def hook(e):
            entry = (address,) + fmt(e)
            e.entries.append((len(e.journal), entry))
            v0, f0 = SAR.nested_bits(e, address)
            if used:
                entry += (v0 & MASK,)
            e.calls.append(entry)
            e.r[2], e.f[0] = v0, f0
        return hook
    ee.hooks = {a: make(a, fmt, used) for a, (fmt, used) in WORKERS.items()}


# ======================================================================
# Native side
# ======================================================================

P, U8, U32, I32, U64 = C.POINTER, C.c_uint8, C.c_uint32, C.c_int32, C.c_uint64
VP = C.c_void_p


class View(C.Structure):
    _fields_ = [('address', U32), ('size', U32), ('bytes', P(U8))]


class World(C.Structure):
    _fields_ = [('views', P(View)), ('view_count', U32), ('view', VP), ('view_ctx', VP)]


class Fault(C.Structure):
    _fields_ = [('address', U32), ('code', I32), ('detail', U32)]


class Core(C.Structure):
    _fields_ = [('world', World), ('fault', Fault), ('function', U32)]


FN = C.CFUNCTYPE
T_4 = FN(C.c_int, VP, U32, U32, I32, U32)                   # 00121870
T_1D2090 = FN(C.c_int, VP, I32, U32)
T_CH = FN(C.c_int, VP, I32)
T_1D1F80 = FN(C.c_int, VP, I32, I32, I32, U32)
T_1CAAC0 = FN(C.c_int, VP, P(U32), U32, I32, U32)
T_F2I = FN(C.c_int, VP, U32, P(I32))
T_RAND = FN(C.c_int, VP, P(I32))
T_1C7900 = FN(C.c_int, VP, U32, P(U32), I32, I32)
T_1C6120 = FN(C.c_int, VP, U32, I32, P(U32))
T_A = FN(C.c_int, VP, U32)
T_CB760 = FN(C.c_int, VP, U32, I32, U32)
T_1B9A0 = FN(C.c_int, VP, I32, U32, U32)
T_AA = FN(C.c_int, VP, U32, U32)
T_CB5F0 = FN(C.c_int, VP, U32, I32, I32, P(U32))
T_CB950 = FN(C.c_int, VP, U32, I32, U64)
T_CB6B0 = FN(C.c_int, VP, U32, I32, I32, U32)
T_CFB50 = FN(C.c_int, VP, U32, I32, U32, U32, U32, U32, U32, U32)
T_CFBE0 = FN(C.c_int, VP, U32, I32, U32, U32, I32)
T_B30 = FN(C.c_int, VP, U32, U32, P(I32))


class VifWorkers(C.Structure):
    _fields_ = [('ctx', VP), ('w_00121870', T_4), ('w_001D2090', T_1D2090), ('w_001D4750', T_CH),
                ('w_001D1F80', T_1D1F80), ('w_001CAAC0', T_1CAAC0)]


class GsWorkers(C.Structure):
    _fields_ = [('ctx', VP), ('w_001281C0', T_F2I), ('w_00122BB8', T_RAND), ('w_001C7900', T_1C7900),
                ('w_001C6120', T_1C6120), ('w_001D3990', T_A), ('w_001CB760', T_CB760),
                ('w_0021B9A0', T_1B9A0), ('w_001F4BF0', T_AA)]


class HudWorkers(C.Structure):
    _fields_ = [('ctx', VP), ('w_001281C0', T_F2I), ('w_001CB5F0', T_CB5F0), ('w_001CB950', T_CB950),
                ('w_001CB6B0', T_CB6B0), ('w_001CB760', T_CB760), ('w_001CFB50', T_CFB50),
                ('w_001CFBE0', T_CFBE0)]


class FrameWorkers(C.Structure):
    _fields_ = [('ctx', VP), ('w_00182B30', T_B30), ('w_00174A50', T_AA), ('w_00182D70', T_A),
                ('w_00183240', T_A), ('w_00183250', T_A), ('w_001833F0', T_A), ('w_00183440', T_A),
                ('w_001834E0', T_A)]


def state_type(workers):
    return type('State' + workers.__name__, (C.Structure,), {'_fields_': [('core', Core), ('workers', workers)]})


VifState, GsState = state_type(VifWorkers), state_type(GsWorkers)
HudState, FrameState = state_type(HudWorkers), state_type(FrameWorkers)
MODULES = {'vif': (VifState, VifWorkers), 'gs': (GsState, GsWorkers), 'hud': (HudState, HudWorkers),
           'frame': (FrameState, FrameWorkers)}
SOURCES = ['src/game/em_area01_render_vif.c', 'src/game/em_area01_render_gs.c',
           'src/game/em_area01_render_hud.c', 'src/game/em_area01_render_frame.c']
NATIVE = ELF = None

ENTRIES = {  # native entry -> (module, argument ctypes, result kind)
    0x1D5BD0: ('vif', [], None), 0x1D5A70: ('vif', [U32, U32], 'u32'),
    0x1D4FC0: ('vif', [U32], None), 0x1D5170: ('vif', [U32], None),
    0x1CD070: ('gs', [U32, U32], 'u32'), 0x1CD180: ('gs', [U32, U32], 'u32'),
    0x1CD2B0: ('gs', [U32, U32, U32, U32], 'u32'), 0x1F4A10: ('gs', [U32, U32], None),
    0x1F4CC0: ('gs', [U32, U32], None),
    0x1E8B90: ('hud', [U32, U32], None), 0x1E9E60: ('hud', [U32, I32], None),
    0x1EAF00: ('hud', [U32, U32], None), 0x1EAF80: ('hud', [U32, U32], None),
    0x1EB020: ('hud', [U32, U32], None), 0x1EC270: ('hud', [U32, U32], None),
    0x15B610: ('frame', [U32], None),
}


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / ('area01_render.dylib' if sys.platform == 'darwin' else 'area01_render.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-ffp-contract=off',
                    '-shared', '-fPIC', '-Isrc'] + SOURCES + ['-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    for address, (module, args, result) in ENTRIES.items():
        fn = getattr(native, 'em_area01_render_%08X' % address)
        fn.argtypes = [P(MODULES[module][0])] + args + ([P(U32)] if result else [])
        fn.restype = C.c_int
    return native


# ======================================================================
# Persistent per-process memory (no per-case image copies)
# ======================================================================
#
# Each process owns four 32 MiB RAM buffers for its whole life: the case's
# starting RAM (scratch), the oracle's RAM, the expected RAM of the entry
# check and the native side's RAM. A Space records which captured image its
# buffer holds and the pages where it may differ from that image; a reset
# copies only those pages (plus the pages where the two captured images
# differ, when the beat changes). The native buffer is page-protected: the
# first write to a page after each check faults once into a small handler
# (TRACKER_C) that records the page and unprotects it, so the checks know
# every page the native side (the C module and its worker interpreter)
# wrote. The entry and end checks compare exactly the pages either side
# wrote since the previous check; given equality at the previous check (or
# at the start, where all buffers equal the scratch), that is equality of
# all 32 MiB. EM_A01R_FULLCMP=1 checks that claim against full compares.

TRACKER_C = r"""
#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

static uintptr_t base_;
static size_t len_, page_, pages_;
static uint8_t bits_[1 << 16];
static struct sigaction old_segv_, old_bus_;
static int installed_;

static void on_fault(int sig, siginfo_t *si, void *uc)
{
    const uintptr_t a = (uintptr_t)si->si_addr;
    (void)uc;
    if (base_ && a >= base_ && a < base_ + len_) {
        const size_t p = (a - base_) / page_;
        bits_[p] = 1;
        if (mprotect((void *)(base_ + p * page_), page_, PROT_READ | PROT_WRITE) == 0) return;
    }
    /* not a tracked page: fault again under the previous handler */
    sigaction(sig, sig == SIGBUS ? &old_bus_ : &old_segv_, NULL);
}

int em_dirty_arm(void *base, size_t len, size_t page)
{
    if (len / page > sizeof bits_ || len % page || (uintptr_t)base % page) return -1;
    if (!installed_) {
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_sigaction = on_fault;
        sa.sa_flags = SA_SIGINFO;
        sigemptyset(&sa.sa_mask);
        if (sigaction(SIGSEGV, &sa, &old_segv_) || sigaction(SIGBUS, &sa, &old_bus_)) return -1;
        installed_ = 1;
    }
    base_ = (uintptr_t)base;
    len_ = len;
    page_ = page;
    pages_ = len / page;
    memset(bits_, 0, pages_);
    return mprotect(base, len, PROT_READ);
}

/* The pages written since the last call, in order; each is protected again. */
long em_dirty_take(uint32_t *out)
{
    long n = 0;
    for (size_t p = 0; p < pages_; ++p)
        if (bits_[p]) {
            bits_[p] = 0;
            if (mprotect((void *)(base_ + p * page_), page_, PROT_READ)) return -1;
            out[n++] = (uint32_t)p;
        }
    return n;
}

int em_dirty_open(void) { return mprotect((void *)base_, len_, PROT_READ | PROT_WRITE); }

int em_dirty_close(void)
{
    memset(bits_, 0, pages_);
    return mprotect((void *)base_, len_, PROT_READ);
}
"""
TRACKER = None


def build_tracker():
    OUT.mkdir(parents=True, exist_ok=True)
    src = OUT / 'area01_render_dirty.c'
    lib = OUT / ('area01_render_dirty.dylib' if sys.platform == 'darwin' else 'area01_render_dirty.so')
    src.write_text(TRACKER_C)
    subprocess.run(['cc', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC', str(src),
                    '-o', str(lib)], check=True)
    t = C.CDLL(str(lib))
    t.em_dirty_arm.argtypes, t.em_dirty_arm.restype = [C.c_void_p, C.c_size_t, C.c_size_t], C.c_int
    t.em_dirty_take.argtypes, t.em_dirty_take.restype = [C.POINTER(C.c_uint32)], C.c_long
    t.em_dirty_open.restype = t.em_dirty_close.restype = C.c_int
    return t


class Space:
    """A persistent RAM buffer. It equals captured image `key` except on the
    pages in `dirty` (key None: unknown, the next reset copies everything)."""

    def __init__(self, data):
        self.data, self.key, self.dirty = data, None, set()


class Work:
    """This process's buffers (made on first use, after the fork)."""

    def __init__(self):
        self.pid = os.getpid()
        self.scratch = Space(bytearray(RAM_SIZE))
        self.oracle = Space(bytearray(RAM_SIZE))
        self.expect = Space(bytearray(RAM_SIZE))
        mm = mmap.mmap(-1, RAM_SIZE, flags=mmap.MAP_PRIVATE | mmap.MAP_ANONYMOUS)
        self.native = Space(mm)
        self.native_c = (U8 * RAM_SIZE).from_buffer(mm)
        self.taken = (C.c_uint32 * (RAM_SIZE // PAGE))()
        assert TRACKER.em_dirty_arm(C.addressof(self.native_c), RAM_SIZE, PAGE) == 0, 'dirty tracker'
        # Self-test: a write by C code and a write by Python are each seen,
        # on exactly their pages (the next reset copies everything anyway).
        C.memset(C.addressof(self.native_c) + 5 * PAGE + 7, 0, 1)
        mm[9 * PAGE - 1:9 * PAGE + 1] = b'\0\0'
        n = TRACKER.em_dirty_take(self.taken)
        assert sorted(self.taken[:n]) == [5, 8, 9], ('dirty tracker self-test', list(self.taken[:n]))
        self.oracle_live = None

    def take(self):
        """The native pages written since the last take (now protected again)."""
        n = TRACKER.em_dirty_take(self.taken)
        assert n >= 0, 'dirty tracker'
        got = set(self.taken[:n])
        self.native.dirty |= got
        return got


WORK = None
DIFF = {}   # beat -> pages where its captured RAM differs from the first beat's


def work():
    global WORK
    if WORK is None or WORK.pid != os.getpid():
        WORK = Work()
    return WORK


def pages(at, size):
    return range(at // PAGE, (at + size - 1) // PAGE + 1)


def journal_pages(journal, start=0, stop=None):
    out = set()
    for is_spad, at, data in journal[start:stop]:
        if not is_spad:
            out.update(pages(at, len(data)))
    return out


def sync(space, src, key, src_dirty, native=False):
    """Make space.data equal src (captured image `key` except on src_dirty)."""
    W = work()
    if native:
        W.take()
    if space.key is None:
        todo = None
    elif space.key == key:
        todo = space.dirty | src_dirty
    else:
        todo = space.dirty | src_dirty | DIFF[space.key] | DIFF[key]
    if todo is None:
        if native:
            assert TRACKER.em_dirty_open() == 0
        space.data[:] = src
        if native:
            assert TRACKER.em_dirty_close() == 0
    else:
        buf = space.data
        for p in todo:
            a = p * PAGE
            buf[a:a + PAGE] = src[a:a + PAGE]
        if native:
            TRACKER.em_dirty_take(W.taken)      # the reset's own writes: protect again, forget
    space.key, space.dirty = key, set(src_dirty)
    if FULLCMP:
        assert bytes(space.data) == bytes(src), 'FULLCMP: buffer reset'


def case_ram(beat):
    """This process's scratch RAM set to `beat`'s captured image; put() on it
    records the pages a case patches. Every case starts from it."""
    W = work()
    sync(W.scratch, image(beat)[0], beat, set())
    return W.scratch.data


def oracle_ee(spad):
    """An oracle interpreter over this process's oracle RAM, set to the scratch."""
    W = work()
    if W.oracle_live is not None:
        W.oracle.dirty |= journal_pages(W.oracle_live.journal)
        W.oracle_live = None
    sync(W.oracle, W.scratch.data, W.scratch.key, W.scratch.dirty)
    ee = A01EE(ELF, b'', spad)
    ee.mem = W.oracle.data
    W.oracle_live = ee
    return ee


def page_differences(a, b, candidates, base=0, limit=6):
    out = []
    for p in sorted(candidates):
        x, y = a[p * PAGE:(p + 1) * PAGE], b[p * PAGE:(p + 1) * PAGE]
        if x != y:
            for k in range(PAGE):
                if x[k] != y[k]:
                    out.append((hex(base + p * PAGE + k), x[k], y[k]))
                    if len(out) >= limit:
                        return out
    return out


class Native:
    """One native run: the module over this process's native RAM (set to the
    scratch) and a copy of `spad`, its workers running the original callees
    in an interpreter sharing that memory."""

    def __init__(self, module, spad, fail_at=None, unbind=None, script=None, views=True, oracle=None,
                 mirror=False):
        W = self.W = work()
        sync(W.native, W.scratch.data, W.scratch.key, W.scratch.dirty, native=True)
        self.ee = A01EE(ELF, b'', spad, journal=False)
        self.ee.mem = W.native.data
        self.ee.hooks = {}
        self.log, self.fail_at, self.script = [], fail_at, script
        # Entry check (oracle given): at every worker call, before the callee
        # runs, the native side's whole RAM and scratchpad must equal the
        # oracle's at the same call entry, and the logged arguments must be
        # equal. The expected RAM is the starting image with the oracle's
        # journal replayed up to that entry.
        self.oracle, self.mismatch, self.jpos, self.checked = oracle, None, 0, 0
        if oracle is not None:
            sync(W.expect, W.scratch.data, W.scratch.key, W.scratch.dirty)
            self.exp_spad = bytearray(spad)
        self.spadc = (U8 * SPAD_SIZE).from_buffer(self.ee.spad)
        ram = C.cast(W.native_c, P(U8))
        vlist = [View(0, RAM_SIZE, ram), View(0x70000000, SPAD_SIZE, C.cast(self.spadc, P(U8)))]
        # mirror: the same RAM also at 0x30100000..0x31FFFFFF (the EE's
        # uncached-accelerated view, as the TLB maps it), which the alias
        # cases reach.
        if mirror:
            vlist.append(View(0x30100000, RAM_SIZE - 0x100000, C.cast(C.byref(W.native_c, 0x100000), P(U8))))
        self.views = (View * len(vlist))(*vlist)
        state_t, workers_t = MODULES[module]
        self.state = state_t()
        if views:
            self.state.core.world = World(C.cast(self.views, P(View)), len(vlist))
        self.keep = []
        for name, kind in workers_t._fields_[1:]:
            if name == unbind:
                continue
            fn = kind(getattr(self, name) if module != 'frame' else self.frame_worker(int(name[2:], 16)))
            self.keep.append(fn)
            setattr(self.state.workers, name, fn)

    # ---- the entry check ----
    def entry_ok(self, entry):
        """False (and self.mismatch set) when the native state at this
        worker entry differs from the oracle's at the same entry."""
        if self.mismatch is not None:
            return False
        o = self.oracle
        if o is None:
            return True
        k = len(self.log)
        if k >= len(o.entries):
            self.mismatch = ('call %d' % k, 'native makes an extra call', entry)
            return False
        stop, want = o.entries[k]
        if entry != want:
            self.mismatch = ('call %d' % k, 'arguments at entry differ (native, original)', entry, want)
            return False
        ram, spad, touched = self.W.expect.data, self.exp_spad, set()
        for is_spad, at, data in o.journal[self.jpos:stop]:
            if is_spad:
                spad[at:at + len(data)] = data
            else:
                ram[at:at + len(data)] = data
                touched.update(pages(at, len(data)))
        self.jpos = stop
        self.W.expect.dirty |= touched
        # Equal at the previous check; since then the expected RAM changed on
        # `touched` and the native RAM on `written`: comparing those pages
        # compares all of RAM.
        written = self.W.take()
        if self.ee.spad != spad:
            self.mismatch = ('call %d' % k, hex(address_of(entry)), 'scratchpad differs at entry (address, native, original)',
                             first_differences(bytes(self.ee.spad), bytes(spad), 0x70000000))
            return False
        bad = page_differences(self.ee.mem, ram, touched | written)
        if FULLCMP:
            assert (not bad) == (self.ee.mem[:] == bytes(ram)), 'FULLCMP: entry check'
        if bad:
            self.mismatch = ('call %d' % k, hex(address_of(entry)), 'RAM differs at entry (address, native, original)', bad)
            return False
        self.checked += 1
        return True

    def end_differences(self, o):
        """RAM differences from the oracle's final RAM (equal at the last
        check, or at the start; since then the native side wrote `written`
        and the oracle the rest of its journal)."""
        candidates = self.W.take() | journal_pages(o.journal, self.jpos)
        bad = page_differences(self.ee.mem, o.mem, candidates)
        if FULLCMP:
            assert (not bad) == (self.ee.mem[:] == bytes(o.mem)), 'FULLCMP: end check'
        return bad

    def unchanged(self, spad):
        """The native RAM and scratchpad still equal the case's start."""
        W = self.W
        return (not page_differences(self.ee.mem, W.scratch.data, W.take() | set(W.native.dirty))
                and bytes(self.ee.spad) == bytes(spad))

    # ---- running an original callee in the shared-memory interpreter ----
    def run(self, address, regs, fregs=(), vector=None, used=False, logged=None):
        e = self.ee
        entry = (address,) + tuple(logged)
        if not self.entry_ok(entry):
            return None
        if self.fail_at is not None and len(self.log) == self.fail_at:
            self.log.append(entry + ('fail',))
            return None
        # Every register the call does not pass is poisoned, so a callee that
        # read anything beyond its logged arguments would diverge from the
        # oracle (whose callee sees the caller's live registers).
        poison(e)
        if vector is not None:
            reg, words = vector
            for i, w in enumerate(words): e.save(VEC + 4 * i, w)
            regs = dict(regs); regs[reg] = VEC
        for n, value in regs.items(): e.r[n] = value & MASK64
        for n, value in fregs: e.f[n] = value & MASK
        e.r[29], e.r[31] = STACK_TOP, RETURN
        try:
            e.run(address)
        except Exception as ex:  # noqa: BLE001 - reported by the case, never swallowed
            self.mismatch = ('call %d' % len(self.log), hex(address), 'the original callee raised on the native side',
                             type(ex).__name__, str(ex)[:200])
            return None
        if used:
            entry += (e.r[2] & MASK,)
        self.log.append(entry)
        return e.r[2]

    @staticmethod
    def sx(value):
        return sx32(value) & MASK64

    def w_00121870(self, _c, dst, src, n, a3):
        r = self.run(0x121870, {4: self.sx(dst), 5: self.sx(src), 6: self.sx(n), 7: self.sx(a3)},
                     logged=(dst, src, n & MASK, a3))
        return -1 if r is None else 0

    def w_001D2090(self, _c, chan, src):
        return -1 if self.run(0x1D2090, {4: self.sx(chan), 5: self.sx(src)}, logged=(chan & MASK, src)) is None else 0

    def w_001D4750(self, _c, chan):
        return -1 if self.run(0x1D4750, {4: self.sx(chan)}, logged=(chan & MASK,)) is None else 0

    def w_001D1F80(self, _c, a0, a1, a2, a3):
        r = self.run(0x1D1F80, {4: self.sx(a0), 5: self.sx(a1), 6: self.sx(a2), 7: self.sx(a3)},
                     logged=(a0 & MASK, a1 & MASK, a2 & MASK, a3))
        return -1 if r is None else 0

    def w_001CAAC0(self, _c, v, packet, a2, a3):
        words = tuple(v[i] for i in range(4))
        r = self.run(0x1CAAC0, {5: self.sx(packet), 6: self.sx(a2), 7: self.sx(a3)}, vector=(4, words),
                     logged=(words, packet, a2 & MASK, a3))
        return -1 if r is None else 0

    def w_001281C0(self, _c, f12, out):
        r = self.run(0x1281C0, {}, fregs=[(12, f12)], used=True, logged=(f12,))
        if r is None: return -1
        out[0] = s32(r)
        return 0

    def w_00122BB8(self, _c, out):
        r = self.run(0x122BB8, {}, used=True, logged=())
        if r is None: return -1
        out[0] = s32(r)
        return 0

    def w_001C7900(self, _c, obj, v, a2, a3):
        words = tuple(v[i] for i in range(4))
        r = self.run(0x1C7900, {4: self.sx(obj), 6: self.sx(a2), 7: self.sx(a3)}, vector=(5, words),
                     logged=(obj, words, a2 & MASK, a3 & MASK))
        return -1 if r is None else 0

    def w_001C6120(self, _c, table, index, out):
        r = self.run(0x1C6120, {4: self.sx(table), 5: self.sx(index)}, used=True, logged=(table, index & MASK))
        if r is None: return -1
        out[0] = r & MASK
        return 0

    def w_001D3990(self, _c, entry):
        return -1 if self.run(0x1D3990, {4: self.sx(entry)}, logged=(entry,)) is None else 0

    def w_001CB760(self, _c, table, key, packet):
        r = self.run(0x1CB760, {4: self.sx(table), 5: self.sx(key), 6: self.sx(packet)},
                     logged=(table, key & MASK, packet))
        return -1 if r is None else 0

    def w_0021B9A0(self, _c, a0, f12, f13):
        r = self.run(0x21B9A0, {4: self.sx(a0)}, fregs=[(12, f12), (13, f13)], logged=(a0 & MASK, f12, f13))
        return -1 if r is None else 0

    def w_001F4BF0(self, _c, a0, a1):
        return -1 if self.run(0x1F4BF0, {4: self.sx(a0), 5: self.sx(a1)}, logged=(a0, a1)) is None else 0

    def w_001CB5F0(self, _c, table, a1, qwc, out):
        r = self.run(0x1CB5F0, {4: self.sx(table), 5: self.sx(a1), 6: self.sx(qwc)}, used=True,
                     logged=(table, a1 & MASK, qwc & MASK))
        if r is None: return -1
        out[0] = r & MASK
        return 0

    def w_001CB950(self, _c, table, a1, a2):
        r = self.run(0x1CB950, {4: self.sx(table), 5: self.sx(a1), 6: a2}, logged=(table, a1 & MASK, a2))
        return -1 if r is None else 0

    def w_001CB6B0(self, _c, table, a1, a2, a3):
        r = self.run(0x1CB6B0, {4: self.sx(table), 5: self.sx(a1), 6: self.sx(a2), 7: self.sx(a3)},
                     logged=(table, a1 & MASK, a2 & MASK, a3))
        return -1 if r is None else 0

    def w_001CFB50(self, _c, dst, a1, src, f12, f13, f14, f15, f16):
        r = self.run(0x1CFB50, {4: self.sx(dst), 5: self.sx(a1), 6: self.sx(src)},
                     fregs=[(12, f12), (13, f13), (14, f14), (15, f15), (16, f16)],
                     logged=(dst, a1 & MASK, src, f12, f13, f14, f15, f16))
        return -1 if r is None else 0

    def w_001CFBE0(self, _c, a0, kind, table, xf, t0):
        r = self.run(0x1CFBE0, {4: self.sx(a0), 5: self.sx(kind), 6: self.sx(table), 7: self.sx(xf),
                                8: self.sx(t0)}, logged=(a0, kind & MASK, table, xf, t0 & MASK))
        return -1 if r is None else 0

    # ---- 0015B610's scripted boundaries ----
    def frame_worker(self, address):
        def w(_c, a, *rest):
            if address == 0x182B30:
                b, out = rest
                entry = (address, a, b)
            elif address == 0x174A50:
                entry = (address, a, rest[0])
            else:
                entry = (address, a)
            if not self.entry_ok(entry):
                return -1
            if self.fail_at is not None and len(self.log) == self.fail_at:
                self.log.append(entry + ('fail',))
                return -1
            if address == 0x182B30:
                v = self.script
                out[0] = s32(v)
                entry += (v & MASK,)
            self.log.append(entry)
            return 0
        return w

    def call(self, address, *args):
        fn = getattr(NATIVE, 'em_area01_render_%08X' % address)
        module, _, result = ENTRIES[address]
        out = U32(0)
        rc = fn(C.byref(self.state), *args, *([C.byref(out)] if result else []))
        return rc, (out.value if result else None)


# ======================================================================
# Running one case on both sides
# ======================================================================

def address_of(entry):
    return entry[0]


POISON64, POISON32 = 0xA5A5A5A5A5A5A5A5, 0xA5A5A5A5


def poison(e):
    """Overwrite every register a callee could read without it being passed:
    GPRs (both halves) except zero / gp / sp / ra, HI / LO, all FPRs, the
    FPU ACC, VF1..VF31, the VU ACC and Q."""
    for n in range(1, 32):
        if n not in (28, 29, 31):
            e.r[n], e.rh[n] = POISON64, POISON64
    e.hi = e.lo = POISON64
    e.f = [POISON32] * 32
    e.acc = POISON32
    e.vf = [list(e.vf[0])] + [[POISON32] * 4 for _ in range(31)]
    e.vacc = [POISON32] * 4
    e.q = POISON32


def first_differences(a, b, base=0, limit=6):
    out, i, n = [], 0, len(a)
    while i < n and len(out) < limit:
        j = i + 0x10000
        if a[i:j] != b[i:j]:
            for k in range(i, min(j, n)):
                if a[k] != b[k]:
                    out.append((hex(base + k), a[k], b[k]))
                    if len(out) >= limit:
                        break
        i = j
    return out


def oracle_call(ee, address, args, fargs=()):
    """The original routine; returns v0 (or f0 bits for 001CD2B0)."""
    for i, value in enumerate(fargs): ee.f[12 + i] = value & MASK
    ee.call(address, args)
    return (ee.f[0] & MASK) if address == 0x1CD2B0 else (ee.r[2] & MASK)


def run_both(where, ram, spad, steps, script=None, mirror=False, check=None):
    """steps: [(address, int args, float args)] run in order on both sides,
    each from this process's scratch RAM (`ram` must be it: case_ram()) and
    `spad`. Compares results, the worker log, RAM and scratchpad at every
    worker entry and at the end. Returns the oracle's branch outcomes, or
    'unmeasured' when both sides refuse. check(oracle), if given, must
    hold after the oracle's run (the case reached what it is built for)."""
    assert ram is work().scratch.data, (where, 'a case starts from case_ram()')
    module = ENTRIES[steps[0][0]][0]
    ee = oracle_ee(spad)
    if module == 'frame':
        frame_hooks(ee, script)
    else:
        logging_hooks(ee)
    unmeasured_o = unmeasured_n = False
    results_o, results_n = [], []
    try:
        for address, args, fargs in steps:
            results_o.append(oracle_call(ee, address, args, fargs))
    except Unmeasured:
        unmeasured_o = True
    except NotCompletable as ex:
        raise AssertionError((where, 'the original cannot complete this case') + ex.args)
    if check is not None and not unmeasured_o:
        assert check(ee), (where, 'the case did not reach the state it is built for')
    native = Native(module, spad, script=script, oracle=None if unmeasured_o else ee, mirror=mirror)
    if AV.ENABLED:
        native.canonical_view = AV.CanonicalView(native.views, base='address')
        native.canonical_view.install_render(native.state.core.world)
    for address, args, fargs in steps:
        rc, value = native.call(address, *(list(args) + list(fargs)))
        assert native.mismatch is None, (where, 'entry check') + native.mismatch
        if rc != 0:
            f = native.state.core.fault
            if f.code == 6 and unmeasured_o:
                unmeasured_n = True
                break
            raise AssertionError((where, 'native faulted', hex(address), hex(f.address), f.code, hex(f.detail)))
        results_n.append(value)
    if unmeasured_o or unmeasured_n:
        assert unmeasured_o and unmeasured_n, (where, 'unmeasured on one side only', unmeasured_o, unmeasured_n)
        return 'unmeasured'
    for (address, _, _), got, want in zip(steps, results_n, results_o):
        if ENTRIES[address][2]:
            assert got == want, (where, hex(address), 'result', hex(got), hex(want))
    assert native.log == ee.calls, (where, 'worker calls', diff_logs(native.log, ee.calls))
    assert native.checked == len(ee.entries), (where, 'entry checks', native.checked, len(ee.entries))
    assert native.ee.spad == ee.spad, (where, 'scratchpad differs (address, native, original)',
                                       first_differences(bytes(native.ee.spad), bytes(ee.spad), 0x70000000))
    bad = native.end_differences(ee)
    assert not bad, (where, 'RAM differs (address, native, original)', bad)
    return ee.outcomes, len(ee.calls), sum(1 for c in ee.calls if c[0] in (0x1CB5F0, 0x1D2090, 0x1D1F80)), native.checked


def diff_logs(a, b):
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return i, x, y
    return 'length', len(a), len(b)


def frame_hooks(ee, script):
    def make(address, fmt):
        def hook(e):
            entry = (address,) + fmt(e)
            e.entries.append((len(e.journal), entry))
            if address == 0x182B30:
                entry += (script & MASK,)
                e.r[2] = sx32(script) & MASK64
            e.calls.append(entry)
        return hook
    ee.hooks = {a: make(a, fmt) for a, fmt in FRAME_WORKERS.items()}


# ======================================================================
# Captures
# ======================================================================

BEATS = sorted(p.name for p in ROUTE.iterdir() if (p / 'eeMemory.bin').exists()) if ROUTE.exists() else []
MAIN = [b for b in BEATS if b[4] != 's' and not b.startswith('a01_07')]
IMAGES = {}


def image(beat):
    if beat not in IMAGES:
        IMAGES[beat] = ((ROUTE / beat / 'eeMemory.bin').read_bytes(), (ROUTE / beat / 'scratchpad.bin').read_bytes())
    return IMAGES[beat]


def w32(buf, at):
    return struct.unpack_from('<I', buf, at & 0x1FFFFFF)[0]


def put(buf, at, value, size=4):
    at &= 0x1FFFFFF
    buf[at:at + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'little')
    if WORK is not None and buf is WORK.scratch.data:
        WORK.scratch.dirty.update(pages(at, size))


def sput(spad, at, value):
    struct.pack_into('<I', spad, at - 0x70000000, value & MASK)


def pool(ram, behaviours):
    out, node, seen = [], w32(ram, 0x275BC0), 0
    while node and seen < 0x200:
        if w32(ram, node + 0x10) in behaviours:
            out.append(node)
        node = w32(ram, node + 0x1C)
        seen += 1
    return out


def check_code(ram, beat, base):
    """The captured RAM holds the ELF's code for every routine run here."""
    for start in list(SIZES) + sorted(INLINE) + sorted(WORKERS):
        size = SIZES.get(start, 0x40)
        assert ram[start:start + size] == bytes(base.mem[start:start + size]), (beat, hex(start))


SPECIAL = [0x7FC00000, 0xFF800000, 0x80000000, 0x00000001, 0x7F7FFFFF, 0x807FFFFF, 0]


def rfloat(rng, lo, hi, special=0.0):
    if rng.random() < special:
        return rng.choice(SPECIAL)
    return F(rng.uniform(lo, hi))


# ---- vif ----------------------------------------------------------------

def vif_capture(beat):
    ram, spad = case_ram(beat), image(beat)[1]
    if w32(ram, w32(ram, 0x28A5A4)) > 64:     # a01_07 ends in AREA00: not the AREA01 table
        return 'skipped'
    return run_both((beat, '001D5BD0'), ram, spad, [(0x1D5BD0, (), ())])


def vif_unit(seed):
    rng = random.Random('vif:%d' % seed)
    beat = MAIN[seed % len(MAIN)]
    ram, spad = case_ram(beat), image(beat)[1]
    ctx, table = w32(ram, 0x275670), w32(ram, 0x28A5A4)
    count = w32(ram, table)
    if rng.random() < 0.3:
        put(ram, ctx + 0x9C, rng.choice((0, 1, 2, 3, 7)))
    new_count = rng.choice((count, count, 0, 1, 3, -1)) if count < 64 else count
    put(ram, table, new_count)
    recs = [table + 0x10 + 0x860 * i for i in range(max(0, min(count, 12)))]
    for rec in recs:
        if rng.random() < 0.4:
            put(ram, rec, rng.choice((0, 1, 2, 0x1F7, 0x1F8, 0x1F9, 0x3F0, 0x3F1, 0x5E8, -1, -0x1F8)))
        if rng.random() < 0.5:
            anchor = [struct.unpack('<f', struct.pack('<I', w32(ram, rec + 0x80 + 4 * k)))[0] for k in range(3)]
            mode = rng.random()
            for j in range(3):
                for k in range(3):
                    if mode < 0.3:        # same side, far away: all outside one plane
                        value = anchor[k] + (1e5 if k == 0 else rng.uniform(-10, 10))
                    elif mode < 0.6:      # scattered
                        value = anchor[k] + rng.uniform(-3000, 3000)
                    else:
                        value = anchor[k] + rng.uniform(-30, 30)
                    word = F(value)
                    if rng.random() < 0.01:
                        word = rng.choice(SPECIAL)
                    put(ram, rec + 0x80 + 0x40 * j + 4 * k, word)
    if rng.random() < 0.15:  # whole-routine targets as well
        rec = rng.choice(recs) if recs else table + 0x10
        target = rng.choice((0x1D4FC0, 0x1D5170, 0x1D5A70))
        args = (rng.getrandbits(32), rec + 0x40 + rng.choice((0, 4, 8, 12))) if target == 0x1D5A70 else (rec,)
        return run_both(('vif unit', seed, beat, hex(target)), ram, spad, [(target, args, ())])
    return run_both(('vif unit', seed, beat), ram, spad, [(0x1D5BD0, (), ())])


# 001D5A70 called directly at 001D5BD0's shape (a1 = rec + 0x40), so its
# result word itself is compared (001D5BD0 only tests it against 0 and 0xFF,
# so a 1 -> 2 slip would not show there). The records' three points are left
# as captured, or moved so that one point, or all three, lie far outside one
# clip plane, or placed around the point the clip matrix maps to clip x = y
# = z = 0 (solved from the matrix in the image; a test input only) with or
# without one far point; main() asserts that all three results 0, 1 and 0xFF
# occur.
VIF_5A70 = ('captured', 'one_far', 'all_far', 'one_far_neg', 'centre', 'centre_one_far')


def clip_centre(ram):
    """A point (x, y, z) that the 001CD370(0) matrix m (context +0x2240) maps
    to clip x = y = 0 with w = W and |z| < |W| for the first W in +-10,
    +-100, +-1000 that gives one (Gaussian elimination on lanes 0, 1, 3); a
    test input only. None when no W does."""
    base = w32(ram, 0x275670) + 0x2240
    m = [[struct.unpack('<f', struct.pack('<I', w32(ram, base + 16 * r + 4 * k)))[0] for k in range(4)]
         for r in range(4)]
    for W in (10.0, -10.0, 100.0, -100.0, 1000.0, -1000.0):
        a = [[m[0][k], m[1][k], m[2][k], (W if k == 3 else 0.0) - m[3][k]] for k in (0, 1, 3)]
        for c in range(3):
            piv = max(range(c, 3), key=lambda r: abs(a[r][c]))
            a[c], a[piv] = a[piv], a[c]
            for r in range(3):
                if r != c and a[c][c]:
                    f = a[r][c] / a[c][c]
                    a[r] = [x - f * y for x, y in zip(a[r], a[c])]
        if not all(a[k][k] for k in range(3)):
            continue
        x = [a[k][3] / a[k][k] for k in range(3)]
        z = m[0][2] * x[0] + m[1][2] * x[1] + m[2][2] * x[2] + m[3][2]
        if abs(z) < abs(W):
            return x
    return None


def vif_5a70(item):
    beat, index, mode = item
    ram, spad = case_ram(beat), image(beat)[1]
    table = w32(ram, 0x28A5A4)
    count = w32(ram, table)
    if not 0 < count < 64 or index >= count:
        return 'skipped'
    rec = table + 0x10 + 0x860 * index
    fl = lambda a: struct.unpack('<f', struct.pack('<I', w32(ram, a)))[0]
    if mode.startswith('centre'):
        centre = clip_centre(ram)
        if centre is None:
            return 'skipped'
        for j in range(3):
            for k in range(3):
                put(ram, rec + 0x80 + 0x40 * j + 4 * k, F(centre[k] + (j - 1) * 0.5))
    for j in range(3):
        if mode == 'all_far' or ('one_far' in mode and j == 2):
            at = rec + 0x80 + 0x40 * j
            put(ram, at, F(fl(at) + (-1e5 if mode.endswith('_neg') else 1e5)))
    probe = oracle_ee(spad)
    logging_hooks(probe)
    result = oracle_call(probe, 0x1D5A70, (0, rec + 0x40))
    return run_both(('vif 001D5A70', beat, index, mode), ram, spad, [(0x1D5A70, (0, rec + 0x40), ())]), result


# ---- gs -----------------------------------------------------------------

def gs_capture(beat):
    spad0 = image(beat)[1]
    ram, spad = case_ram(beat), bytearray(spad0)
    out = []
    # 001F8350's pairing: the subject's position through 001CD070(p, 0x30), then
    # 001CD2B0(6, 6, 96, f15).
    subject = w32(ram, 0x275B44)
    for i in range(4):
        sput(spad, 0x700036D0 + 4 * i, w32(ram, subject + 0xB0 + 4 * i))
    out.append(run_both((beat, '001CD070+001CD2B0'), ram, bytes(spad),
                        [(0x1CD070, (0x700036D0, 0x30), ()),
                         (0x1CD2B0, (), (F(6.0), F(6.0), F(96.0), F(32.0)))]))
    for node in pool(ram, {0x158BD0, 0x158D30}):
        for words in ((0, 0x80, 0, 0x80), (0x80, 0, 0, 0x80)):
            for i, w in enumerate(words): sput(spad, 0x700038B0 + 4 * i, w)
            out.append(run_both((beat, '001F4A10', hex(node), words), ram, bytes(spad),
                                [(0x1F4A10, (node + 0xD0, 0x700038B0), ())]))
    out.append(run_both((beat, '001F4CC0'), ram, spad0, [(0x1F4CC0, (0x700038A0, 0x700038B0), ())]))
    return out


def gs_unit(seed):
    rng = random.Random('gs:%d' % seed)
    beat = MAIN[seed % len(MAIN)]
    ram, spad = case_ram(beat), bytearray(image(beat)[1])
    subject = w32(ram, 0x275B44)
    kind = rng.random()
    if kind < 0.45:
        base = [struct.unpack('<f', struct.pack('<I', w32(ram, subject + 0xB0 + 4 * k)))[0] for k in range(3)]
        radius = rng.choice((1.0, 20.0, 200.0, 2000.0, 20000.0))
        for k in range(3):
            sput(spad, 0x700036D0 + 4 * k, F(base[k] + rng.uniform(-radius, radius)))
        sput(spad, 0x700036DC, rng.choice((F(1.0), rng.getrandbits(32))))
        if rng.random() < 0.02:
            sput(spad, 0x700036D0 + 4 * rng.randrange(3), rng.choice(SPECIAL))
        mask = rng.choice((0x30, 0x3F, 0, 1, 2, 4, 8, 0x10, 0x20, rng.getrandbits(32)))
        size = rng.choice((6.0, 0.5, 40.0, rng.uniform(0, 300)))
        fargs = (F(size), F(rng.choice((size, rng.uniform(0, 300)))),
                 F(rng.choice((96.0, 0.0, rng.uniform(-50, 400)))),
                 F(rng.choice((32.0, 1.0, rng.uniform(0.01, 300), 0.0, -5.0))))
        # p off its quadword boundary as well: the original's quadword load
        # ignores the low four address bits.
        p = 0x700036D0 + rng.choice((0, 0, 0, 4, 8, 12))
        steps = [(0x1CD070, (p, mask), ()), (0x1CD2B0, (), fargs)]
        return run_both(('gs unit', seed, beat, hex(p)), ram, bytes(spad), steps)
    if kind < 0.65:  # 001CD180 alone on arbitrary centres, extents and depths
        for k in range(2):
            sput(spad, 0x70003600 + 4 * k, rng.choice((rng.randrange(0x6000, 0xA000), rng.getrandbits(32),
                                                      0x7000, 0x7900, 0x9000, 0x8700)))
        put(ram, 0x275C04, rng.choice((rng.randrange(0, 0x10000), rng.getrandbits(32), 0, 1)))
        fargs = (rfloat(rng, 0, 400, 0.03), rfloat(rng, 0, 400, 0.03))
        return run_both(('gs 001CD180', seed, beat), ram, bytes(spad), [(0x1CD180, (), fargs)])
    if kind < 0.9:
        nodes = pool(ram, {0x158BD0, 0x158D30}) or [PLAYER - 0xD0]
        words = [rng.choice((0, 0x80, rng.randrange(-300, 300), rng.getrandbits(32))) for _ in range(4)]
        for i, w in enumerate(words): sput(spad, 0x700038B0 + 4 * i, w)
        if rng.random() < 0.5:
            for k in range(3):
                put(ram, subject + 0xB0 + 4 * k,
                    F(struct.unpack('<f', struct.pack('<I', w32(ram, subject + 0xB0 + 4 * k)))[0]
                      + rng.uniform(-500, 500)))
        return run_both(('gs 001F4A10', seed, beat), ram, bytes(spad),
                        [(0x1F4A10, (rng.choice(nodes) + 0xD0, 0x700038B0), ())])
    for i in range(8):
        sput(spad, 0x700038A0 + 4 * i, rng.choice((0, 0x80, 0xFF, rng.getrandbits(32), F(rng.uniform(-500, 500)))))
    return run_both(('gs 001F4CC0', seed, beat), ram, bytes(spad), [(0x1F4CC0, (0x700038A0, 0x700038B0), ())])


# 001CD2B0's t = n - f14 boundary. With n the 001CD180 result of the beat's
# 001F8350 pairing and f14 = (float)n, t is exactly +0: the original then
# divides t by f15 (0 / 0 when f15 is +-0, which the EE saturates, so the
# clamp gives 1 and the result is 0.0), while a "t <= 0 counts as 0" rule
# would give 1.0. f14 = n + 1 and n - 1 put t just below and above 0.
# The v kinds test the clamp at 1.0 itself: f14 = 0 and f15 = (float)n one
# unit below, equal and one unit above (as bit patterns), so v = t / f15
# lands just above 1 (on a01_01, n = 51: 1 + 2**-23, which the clamp turns
# into 1), on 1, and just below 1 (not clamped).
GS_T = {'t0_f15_0': (0, 0), 't0_f15_neg0': (0, 0x80000000), 't0_f15_5': (0, F(5.0)),
        'tneg_f15_0': (1, 0), 'tpos_f15_0': (-1, 0), 'tpos_f15_neg0': (-1, 0x80000000),
        'v_over1': ('v', -1), 'v_is1': ('v', 0), 'v_under1': ('v', 1)}


def gs_boundary(item):
    beat, kind = item
    ram, spad = case_ram(beat), bytearray(image(beat)[1])
    subject = w32(ram, 0x275B44)
    for i in range(4):
        sput(spad, 0x700036D0 + 4 * i, w32(ram, subject + 0xB0 + 4 * i))
    probe = oracle_ee(bytes(spad))
    logging_hooks(probe)
    oracle_call(probe, 0x1CD070, (0x700036D0, 0x30))
    oracle_call(probe, 0x1CD2B0, (), (F(6.0), F(6.0), F(96.0), F(32.0)))
    n = s32(probe.load(0x275C00))
    if n == 0:
        return 'skipped'
    shift, f15 = GS_T[kind]
    f14 = F(float(n + shift)) if shift != 'v' else 0
    if shift == 'v':
        assert 0 < n < 1 << 24, ('gs boundary', beat, kind, n)
        f15 = F(float(n)) + f15
    steps = [(0x1CD070, (0x700036D0, 0x30), ()), (0x1CD2B0, (), (F(6.0), F(6.0), f14, f15))]
    return run_both(('gs boundary', beat, kind, n), ram, bytes(spad), steps)


# 001CD180's four bound tests exactly at and one past each bound. The
# extents (hw, hh) do not depend on the centre, so one oracle run with the
# centre far out (it returns at the first test, leaving (hw, hh) at
# 0x70003610) gives them; each case then puts the centre so that one sum or
# difference lands on the bound while the other coordinate sits at 0x8000.
GS_BOUNDS = {'x_minus_at': (0, 0x9000), 'x_minus_past': (0, 0x9001), 'y_minus_at': (1, 0x8700),
             'y_minus_past': (1, 0x8701), 'x_plus_at': (2, 0x7000), 'x_plus_past': (2, 0x6FFF),
             'y_plus_at': (3, 0x7900), 'y_plus_past': (3, 0x78FF)}
GS_EXTENTS = ((6.0, 6.0), (40.0, 24.0))


def gs_bounds(item):
    beat, extents, kind = item
    ram, spad = case_ram(beat), bytearray(image(beat)[1])
    fargs = (F(extents[0]), F(extents[1]))
    sput(spad, 0x70003600, 0x40000000)
    probe = oracle_ee(bytes(spad))
    logging_hooks(probe)
    oracle_call(probe, 0x1CD180, (), fargs)
    hw, hh = s32(probe.load(0x70003610)), s32(probe.load(0x70003614))
    which, bound = GS_BOUNDS[kind]
    cx, cy = 0x8000, 0x8000
    if which == 0: cx = bound + hw
    elif which == 1: cy = bound + hh
    elif which == 2: cx = bound - hw
    else: cy = bound - hh
    sput(spad, 0x70003600, cx)
    sput(spad, 0x70003604, cy)
    return run_both(('gs bounds', beat, extents, kind, hw, hh), ram, bytes(spad), [(0x1CD180, (), fargs)])


# The native views' edges: 001F8350's pairing with the point quadword at the
# first scratchpad bytes (0x70000000), the last ones (0x70003FF0) and the
# last RAM quadword (0x01FFFFF0). All three are mapped on the EE (the TLB
# map above); an access that starts or ends exactly at a view's edge must
# be served by that view.
GS_EDGE = (0x70000000, 0x70003FF0, 0x01FFFFF0)


def gs_edge(item):
    beat, at = item
    ram, spad = case_ram(beat), bytearray(image(beat)[1])
    subject = w32(ram, 0x275B44)
    for i in range(4):
        word = w32(ram, subject + 0xB0 + 4 * i)
        if at >= 0x70000000:
            sput(spad, at + 4 * i, word)
        else:
            put(ram, at + 4 * i, word)
    return run_both(('gs edge', beat, hex(at)), ram, bytes(spad),
                    [(0x1CD070, (at, 0x30), ()), (0x1CD2B0, (), (F(6.0), F(6.0), F(96.0), F(32.0)))])


# 001CD2B0 with a 001CD180 result n of 25 bits: extents f12 = f13 of a few
# million through the beat's 001F8350 pairing, f14 = 0, f15 = 2**25. The
# EE's CVT.S.W truncates; a host conversion rounds to nearest. The two
# differ only when n has more than 24 significant bits and the dropped bits
# round up, which no extent of the unit cases reaches; main() asserts that
# at least one case has such an n (6 of the 21 do).
GS_BIG = (2.3e6, 2.51e6, 3.03e6)


def gs_big(item):
    beat, f12 = item
    ram, spad = case_ram(beat), bytearray(image(beat)[1])
    subject = w32(ram, 0x275B44)
    for i in range(4):
        sput(spad, 0x700036D0 + 4 * i, w32(ram, subject + 0xB0 + 4 * i))
    seen = []

    def check(ee):
        seen.append(ee.load(0x275C00))
        return True
    r = run_both(('gs big n', beat, f12), ram, bytes(spad),
                 [(0x1CD070, (0x700036D0, 0x30), ()), (0x1CD2B0, (), (F(f12), F(f12), 0, F(2.0 ** 25)))], check=check)
    n = s32(seen[0]) if seen else 0
    cut = max(0, abs(n).bit_length() - 24)
    trunc = (abs(n) >> cut) << cut
    return r, n != 0 and F(float(trunc)) != F(float(abs(n)))


# The quadword at context +0xA0 (ca0). Every capture holds ca0.x = 255, so
# these cases perturb it: em_a01r_project clamps s.w with min(., ca0.x) and
# then max(., 0), and the two orders differ only when ca0.x < 0. The same
# quadword is read by 001CD070 and 001F4A10 (through the projection) and
# copied into 001E9E60's packet; 001CAAC0 (original code on both sides) reads
# it too. x_nan puts a positive NaN pattern (0x7FC00000) in x: the VU's min
# compares raw patterns and keeps the w term, which a host float min would
# not (it would pick the NaN).
CA0 = {'x_neg5': {0: F(-5.0)}, 'x_neg_half': {0: F(-0.5)}, 'x_pos0': {0: 0}, 'x_neg0': {0: 0x80000000},
       'x_small': {0: F(1e-3)}, 'x_subnormal': {0: 1}, 'zw': {2: F(-3.0), 3: F(0.25)},
       'zw_big': {2: F(1000.0), 3: F(-2.0)}, 'x_nan': {0: 0x7FC00000}}
CA0_TARGETS = ('001CD070', '001F4A10', '001E9E60')


def gs_ca0(item):
    beat, variant, target = item
    ram, spad = case_ram(beat), bytearray(image(beat)[1])
    ctx = w32(ram, 0x275670)
    for lane, word in CA0[variant].items():
        put(ram, ctx + 0xA0 + 4 * lane, word)
    where = ('ca0', beat, variant, target)
    if target == '001CD070':
        subject = w32(ram, 0x275B44)
        for i in range(4):
            sput(spad, 0x700036D0 + 4 * i, w32(ram, subject + 0xB0 + 4 * i))
        return run_both(where, ram, bytes(spad),
                        [(0x1CD070, (0x700036D0, 0x30), ()),
                         (0x1CD2B0, (), (F(6.0), F(6.0), F(96.0), F(32.0)))])
    if target == '001F4A10':
        node = (pool(ram, {0x158BD0, 0x158D30}) or [PLAYER - 0xD0])[0]
        for i, w in enumerate((0, 0x80, 0, 0x80)): sput(spad, 0x700038B0 + 4 * i, w)
        return run_both(where, ram, bytes(spad), [(0x1F4A10, (node + 0xD0, 0x700038B0), ())])
    nodes = pool(ram, {0x15A2C0})
    node = nodes[0] if nodes else PLAYER
    a1 = struct.unpack_from('<H', ram, node + 0xE)[0] if nodes else 0
    return run_both(where, ram, bytes(spad), [(0x1E9E60, (node, a1), ())])


# The channel-3 tag stores and the words they re-read. Every case below is
# an execution the EE completes: the interpreters refuse any misaligned
# access or address outside the TLB map (A01EE, NotCompletable), and each
# case asserts that the aliasing it is built for really happened.
#
# The tag helper em_a01r_tag3 (and 001D4FC0's inline first tag, the same
# four stores) loads the cursor word (context +0x1C) again before each
# store: id byte at cursor +3, addr word at +4, qwc halfword at +0, then
# cursor += 0x10. A re-read is observable only when an earlier store of the
# same tag lands on the cursor word:
#   +0x18: the addr store overwrites the cursor (the qwc store then follows
#          it); run at 001D5170's slot and first chunk tag and 001D4FC0's
#          slot tag.
#   +0x1C: the id byte replaces the cursor's top byte (id 0x30: 0x30xxxxxx,
#          the uncached-accelerated view of RAM, which the native world
#          then maps at 0x30100000..) and the qwc halfword its low half; run
#          at the two slot tags. The chunk tag at +0x1C is not an EE
#          execution (a later store is misaligned), nor is 001D4FC0's inline
#          tag at +0x18 (its qwc store goes to address 0, which no TLB entry
#          maps), so neither is run.
#   reloc: +0x1C with the context copied to X = context + 16 MiB and
#          D_00275670 = X. The reloaded cursor 0x30xxxxxx then reaches the
#          low 16 MiB copy of that address, not X +0x20, so the addr store's
#          re-read decides which RAM word it writes.
#   first19: 001D4FC0's inline tag with the cursor at context +0x19, so the
#          id byte (0x10) replaces the cursor's LOW byte: the re-read cursor
#          ((X + 0x19) & ~0xFF) | 0x10 is aligned and the tag completes,
#          while the stale cursor would put the addr word at X + 0x1D. The
#          context is copied to X = 0x1A000E8 (low byte 0xE8, so that the
#          0xE0-byte copy behind the tag does not overwrite the cursor word).
# The context copies take its first 0x3000 bytes, which hold every context
# offset these routines and their callees read (the highest is +0x246C).
TAG_SHAPES = (('001D5170', 'slot', 0x18), ('001D5170', 'slot', 0x1C), ('001D5170', 'loop', 0x18),
              ('001D4FC0', 'slot', 0x18), ('001D4FC0', 'slot', 0x1C), ('001D5170', 'slot', 'reloc'),
              ('001D4FC0', 'slot', 'reloc'), ('001D4FC0', 'first', 'first19'))
TAG_WORKER = {'slot': 0x1D2090, 'loop': 0x1D1F80}
CTX_COPY = 0x3000


def copy_context(ram, dst):
    """Copy the render context (D_00275670's target, first CTX_COPY bytes)
    to dst and point D_00275670 at it."""
    ctx = w32(ram, 0x275670)
    for off in range(0, CTX_COPY, 4):
        put(ram, dst + off, w32(ram, ctx + off))
    put(ram, 0x275670, dst)
    return dst


def tag_site_cursor(ram, spad, routine, worker):
    """The cursor right after the first call of `worker` (the next store is
    the site's tag3) in an oracle run of `routine` on the first table record,
    from the scratch RAM `ram`."""
    ctx = w32(ram, 0x275670)
    ee = oracle_ee(spad)
    logging_hooks(ee)
    got, inner = [], ee.hooks[worker]

    def hook(e):
        inner(e)
        got.append(e.load(ctx + 0x1C))
    ee.hooks[worker] = hook
    oracle_call(ee, int(routine, 16), (w32(ram, 0x28A5A4) + 0x10,))
    return got[0]


def stored(ee, address, size=None):
    """True when the oracle's journal holds a RAM store covering address."""
    a = address & 0x1FFFFFF
    return any(not s and at <= a < at + len(d) and (size is None or len(d) == size) for s, at, d in ee.journal)


def tag_alias(item):
    beat, routine, site, pos = item
    ram, spad = case_ram(beat), image(beat)[1]
    rec = w32(ram, 0x28A5A4) + 0x10
    if pos == 'first19':
        ctx = copy_context(ram, 0x1A000E8)
        put(ram, ctx + 0x1C, ctx + 0x19)
        check = lambda ee: stored(ee, ctx + 0x1C, 1)      # the id byte replaced the cursor's low byte
    else:
        ctx = copy_context(ram, w32(ram, 0x275670) + 0x1000000) if pos == 'reloc' else w32(ram, 0x275670)
        at = 0x1C if pos == 'reloc' else pos
        worker = TAG_WORKER[site]
        start = w32(ram, ctx + 0x1C)
        shift = tag_site_cursor(ram, spad, routine, worker) - start
        put(ram, ctx + 0x1C, ctx + at - shift)
        reached = tag_site_cursor(ram, spad, routine, worker)
        assert reached == ctx + at, ('tag alias', beat, routine, site, pos, hex(reached))
        check = lambda ee: stored(ee, ctx + at + (3 if at == 0x1C else 4), 1 if at == 0x1C else 4)
    return run_both(('tag alias', beat, routine, site, pos), ram, spad, [(int(routine, 16), (rec,), ())],
                    mirror=pos in (0x1C, 'reloc'), check=check)


# D_00275670 (the context pointer) re-read while a routine runs. It changes
# only when a store lands on it; each shape below places a store there, and
# the case checks that the store happened (and so that the original re-read
# sees the new value):
#   slot (001D5170 / 001D4FC0): the slot tag's addr store (cursor 0x27566C)
#     writes the slot address D_00816A40 + (context +0x9C << 7) there.
#     Context +0x9C is set so that the slot is 0x1816A40, which holds a copy
#     of the context (cursor as captured), so the chunk loop's re-read of
#     D_00275670 moves the chunk tags and the end tag to that copy's cursor.
#   slot0 / slotneg: the same with the record count (rec +0) set to 0 or -5,
#     so the chunk loop does not run and only the re-read before the end tag
#     (and the 001CAAC0 context argument) sees the new value.
#   body (001D5170 / 001D4FC0): 001D2090's second tag (id 0x50, qwc 0) has
#     its table argument as addr word (0x237720 / 0x237450). The cursor is
#     placed so that this addr word lands on D_00275670, and the context is
#     copied to that table address, so chain_tail's first re-read (after
#     001D2090 returns) moves the slot tag to the copy's cursor. The copy
#     overwrites the table 001D2090 reads, which then holds context bytes.
#   c7900 (001F4A10): the context is first copied to X = 0xF08000
#     (D_00275670 = X) with X's cursor at 0x275670 - 0x110: 001C7900's fifth
#     tag (id 0x30, qwc 8) lands on D_00275670 and turns it into 0x30F00008,
#     the uncached-accelerated view of 0xF00008, which holds a second copy
#     (cursor as captured). 001F4A10's re-read before its end tag then
#     writes that tag at the second copy's cursor.
#   entry (001F4A10): the re-read after the end tag. 001D3990's last tag
#     (from 001D37D0) has addr word E + 0x40, E being 001C6120's table entry
#     (*D_0028A56C, index 0xC). E is set to 0x275614 and the cursor placed so
#     that this addr word lands on D_00275670, which becomes 0x275654: the
#     context whose cursor word (+0x1C) is D_00275670 itself. 001F4A10's end
#     tag then goes to 0x275654 and its cursor store makes D_00275670 =
#     0x275664, so the next re-read takes the +0xA0 quadword from 0x275700
#     (0x275664 + 0xA0, low four bits dropped) instead of 0x2756F0. Those two
#     quadwords are set to the captured one with x = 255 and x = 100, which
#     gives 001CB760 a different key. D_0027567C (the moved context's +0x18,
#     read by 001CB760) is set to the captured packet cursor.
#   copy (001E9E60): its 9-quadword packet is placed at 0x275610, so the +0x60
#     copy of context +0xA0 lands on D_00275670..7F. Context +0xA0's x word
#     is set to Y (a context copy whose +0x2220 quadword differs) and its y
#     word to the captured D_00275674, so the +0x2220 copy's re-read reads Y.
#   copy3 (001E9E60): the same packet at 0x275600, so the +0x70 copy of
#     context +0x2220 lands on D_00275670..7F. Context +0x2220 is set to
#     (Y, D_00275674), Y being a context copy whose +0x2230 quadword differs,
#     so the re-read before the +0x2230 copy reads Y.
#   word48 (001E9E60): the same packet at 0x275660, so its +0x10 word (the
#     record's +0x48 word) lands on D_00275670 before the first re-read (the
#     one before the +0xA0 copy). Record +0x48 = Y, a context copy whose
#     +0xA0 x word is 100, and record +0x4C = the captured D_00275674.
# The packets written before the aliasing store overwrite words just below
# the aliased word; the oracle's access rules and the case's own check
# guarantee that each run completes and aliases as built.
CTX_ALIAS = (('001D5170', 'slot'), ('001D4FC0', 'slot'), ('001F4A10', 'c7900'), ('001D5170', 'slot0'),
             ('001D4FC0', 'slot0'), ('001D5170', 'slotneg'), ('001D4FC0', 'slotneg'), ('001D5170', 'body'),
             ('001D4FC0', 'body'), ('001F4A10', 'entry'), ('001E9E60', 'copy'), ('001E9E60', 'copy3'),
             ('001E9E60', 'word48'))


def f4a10_setup(ram, spad):
    for i, w in enumerate((0, 0x80, 0, 0x80)): sput(spad, 0x700038B0 + 4 * i, w)
    return (pool(ram, {0x158BD0, 0x158D30}) or [PLAYER - 0xD0])[0]


def e9e60_setup(ram):
    node = pool(ram, {0x15A2C0})[0]
    a1 = struct.unpack_from('<H', ram, node + 0xE)[0]
    return node, a1, (w32(ram, 0x275C1C) + a1 * 0xA060) & MASK


def e9e60_blocks(ram, spad, node, a1):
    """The addresses 001CB5F0 returns in an oracle run of 001E9E60 (0..5: the
    GIF packets, 6: the 5-quadword packet, 7: the 9-quadword packet)."""
    probe = oracle_ee(spad)
    logging_hooks(probe)
    oracle_call(probe, 0x1E9E60, (node, a1))
    return [c[-1] for c in probe.calls if c[0] == 0x1CB5F0]


def ctx_alias(item):
    beat, routine, shape = item
    ram, spad = case_ram(beat), bytearray(image(beat)[1])
    ctx = w32(ram, 0x275670)
    captured = w32(ram, ctx + 0x1C)
    where = ('ctx alias', beat, routine, shape)
    if shape == 'c7900':
        node = f4a10_setup(ram, spad)
        x = copy_context(ram, 0xF08000)
        second = 0x30000000 | (x & 0xFF0000) | 8
        copy_context(ram, second & 0x1FFFFFF)
        put(ram, 0x275670, x)
        put(ram, x + 0x1C, 0x275670 - 0x110)
        put(ram, (second & 0x1FFFFFF) + 0x1C, captured)
        check = lambda ee: stored(ee, 0x275670) and ee.load(0x275670) == second
        return run_both(where, ram, bytes(spad), [(0x1F4A10, (node + 0xD0, 0x700038B0), ())], mirror=True, check=check)
    if shape == 'entry':
        node = f4a10_setup(ram, spad)
        table = w32(ram, 0x28A56C)
        entry = (table + ((s32(w32(ram, table + 0x34)) >> 2) << 2)) & MASK
        probe = oracle_ee(bytes(spad))
        logging_hooks(probe)
        oracle_call(probe, 0x1F4A10, (node + 0xD0, 0x700038B0))
        last = [at for s, at, d in probe.journal if not s and len(d) == 4 and int.from_bytes(d, 'little') == entry + 0x40][-1]
        put(ram, table + 0x34, 0x275614 - table)
        put(ram, ctx + 0x1C, 0x27566C - (last - 4 - (captured & 0x1FFFFFF)))
        put(ram, 0x27567C, w32(ram, ctx + 0x18))
        for k in range(4):
            put(ram, 0x2756F0 + 4 * k, w32(ram, ctx + 0xA0 + 4 * k))
            put(ram, 0x275700 + 4 * k, F(100.0) if k == 0 else w32(ram, ctx + 0xA0 + 4 * k))
        check = lambda ee: (stored(ee, 0x275670) and stored(ee, 0x275657, 1) and ee.load(0x275670) == 0x275664
                            and any(s == 0 and at == 0x275670 and d == (0x275654).to_bytes(4, 'little')
                                    for s, at, d in ee.journal))
        return run_both(where, ram, bytes(spad), [(0x1F4A10, (node + 0xD0, 0x700038B0), ())], check=check)
    if shape in ('copy', 'copy3', 'word48'):
        node, a1, rec = e9e60_setup(ram)
        y = copy_context(ram, 0x1A00000)
        put(ram, 0x275670, ctx)
        if shape == 'copy':
            put(ram, y + 0x2220, w32(ram, y + 0x2220) ^ 0x00010000)
            put(ram, ctx + 0xA0, y)
            put(ram, ctx + 0xA4, w32(ram, 0x275674))
            at = 0x275610
        elif shape == 'copy3':
            put(ram, y + 0x2230, w32(ram, y + 0x2230) ^ 0x00010000)
            put(ram, ctx + 0x2220, y)
            put(ram, ctx + 0x2224, w32(ram, 0x275674))
            at = 0x275600
        else:
            put(ram, y + 0xA0, F(100.0))
            put(ram, rec + 0x48, y)
            put(ram, rec + 0x4C, w32(ram, 0x275674))
            at = 0x275660
        blks = e9e60_blocks(ram, bytes(spad), node, a1)
        put(ram, ctx + 0x18, at - (blks[7] - w32(ram, ctx + 0x18)))
        check = lambda ee: (stored(ee, 0x275670) and ee.load(0x275670) == y
                            and [c[-1] for c in ee.calls if c[0] == 0x1CB5F0][7] == at)
        return run_both(where, ram, bytes(spad), [(0x1E9E60, (node, a1), ())], check=check)
    rec = w32(ram, 0x28A5A4) + 0x10
    if shape == 'body':
        target = 0x237720 if routine == '001D5170' else 0x237450
        copy_context(ram, target)
        put(ram, 0x275670, ctx)
        at = 0x27567C
    else:
        if shape != 'slot':
            put(ram, rec, 0 if shape == 'slot0' else -5)
        target = 0x1816A40
        put(ram, ctx + 0x9C, (target - 0x816A40) >> 7)
        copy_context(ram, target)
        put(ram, 0x275670, ctx)
        at = 0x27566C
    shift = tag_site_cursor(ram, bytes(spad), routine, 0x1D2090) - captured
    put(ram, ctx + 0x1C, at - shift)
    reached = tag_site_cursor(ram, bytes(spad), routine, 0x1D2090)
    assert reached == at, (where, hex(reached))
    check = lambda ee: stored(ee, 0x275670) and ee.load(0x275670) == target
    return run_both(where, ram, bytes(spad), [(int(routine, 16), (rec,), ())], check=check)


# Other words a routine reads again after a call or a store that can change
# them (each case asserts the aliasing store happened):
#   rand (001F4A10): the packet start (context +0x1C) is read before the
#     00122BB8 call, whose LCG state store goes to *D_0024295C + 0x58. The
#     context is copied to X = 0x1A00040, D_0024295C = X + 0x1C - 0x58, and
#     the cursor s0 is chosen so that s0 * 0x41C64E6D + 0x3039 (the stored
#     state) is the captured cursor. 001CB760 then gets s0 as the packet
#     start (it stores only its low 28 bits), a later read would give the
#     new state.
#   grid (001E8B90): D_00275C20 is read again for every record. With
#     D_00275C20 = G = 0x26C9B0, record 0's corner cell (col 4, row 4) is
#     D_00275C20 itself; p = (5.5, 0, 5.5) in a 32 x 32 box at the origin and
#     f12 = -2**-126 make that cell (the pointer read as a sub-normal, so 0)
#     3 * 2**-126 = 0x01400000. Record 1 at the new base is live with the
#     same box; records 1..3 at G and 2..3 at the new base are dead.
#   top (001E9E60): D_00275674 is read after 001CB950, whose block's +0xC
#     word store (0x50000002) is placed on D_00275674 (the block 8 bytes off
#     a quadword, as in the packet +8 group). 001CB6B0 gets it + 0x720.
REREAD = (('001F4A10', 'rand'), ('001E8B90', 'grid'), ('001E9E60', 'top'))


def reread_alias(item):
    beat, routine, shape = item
    ram, spad = case_ram(beat), bytearray(image(beat)[1])
    where = ('reread alias', beat, routine, shape)
    if shape == 'rand':
        node = f4a10_setup(ram, spad)
        captured = w32(ram, w32(ram, 0x275670) + 0x1C)
        x = copy_context(ram, 0x1A00040)
        s0 = ((captured - 0x3039) * pow(0x41C64E6D, -1, 1 << 32)) & MASK
        put(ram, x + 0x1C, s0)
        put(ram, 0x24295C, x + 0x1C - 0x58)
        check = lambda ee: stored(ee, x + 0x1C) and any(c[0] == 0x1CB760 and c[-1] == s0 for c in ee.calls)
        return run_both(where, ram, bytes(spad), [(0x1F4A10, (node + 0xD0, 0x700038B0), ())], check=check)
    if shape == 'grid':
        g, newg = 0x26C9B0, 0x01400000
        put(ram, 0x275C20, g)
        put(ram, 0x810700, 0, 1)
        for base, live in ((g, 0), (newg, 1)):
            for n in range(4):
                put(ram, base + 0xA060 * n + 0x54, 1 if n == live else 0)
            for off, v in ((0, 0), (4, 0), (8, 0), (0x30, F(32.0)), (0x34, F(32.0))):
                put(ram, base + 0xA060 * live + off, v)
        for k, v in enumerate((F(5.5), 0, F(5.5))): put(ram, PLAYER + 0xB0 + 4 * k, v)
        check = lambda ee: stored(ee, 0x275C20) and ee.load(0x275C20) == newg and len(ee.calls) == 4
        return run_both(where, ram, bytes(spad), [(0x1E8B90, (PLAYER + 0xB0,), (0x80800000,))], check=check)
    node, a1, _ = e9e60_setup(ram)
    ctx = w32(ram, 0x275670)
    blks = e9e60_blocks(ram, bytes(spad), node, a1)
    put(ram, ctx + 0x18, 0x275668 - (blks[7] + (9 + 2) * 16 - w32(ram, ctx + 0x18)))
    check = lambda ee: stored(ee, 0x275674) and ee.load(0x275674) == 0x50000002
    return run_both(where, ram, bytes(spad), [(0x1E9E60, (node, a1), ())], check=check)


# 001E9E60's packets 8 bytes off a quadword boundary: the packet cursor word
# 001CB5F0 reads (context +0x18) is moved by 8. Its quadword stores (the
# blend quadword, the 00102948 / 00102958 copies) then drop the low four
# address bits, as the EE's do, while its doubleword and word stores stay
# aligned. The case checks that 001CB5F0 returned a pointer 8 bytes off.
# (With every packet +4 or +0xC off, the 9-quadword packet's doubleword
# stores would be misaligned; the pin3 'stq4' case instead gets packets 4 off
# with that last packet realigned by an earlier packet's copy, which the EE
# completes. +0xC is not run: bits 2 and 3 are each covered.)
def packet_offset(item):
    beat, index = item
    ram, spad = case_ram(beat), image(beat)[1]
    nodes = pool(ram, {0x15A2C0})
    if index >= len(nodes):
        return 'skipped'
    node = nodes[index]
    a1 = struct.unpack_from('<H', ram, node + 0xE)[0]
    ctx = w32(ram, 0x275670)
    put(ram, ctx + 0x18, w32(ram, ctx + 0x18) + 8)
    check = lambda ee: any(c[0] == 0x1CB5F0 and c[-1] & 15 == 8 for c in ee.calls)
    return run_both(('packet +8', beat, hex(node)), ram, spad, [(0x1E9E60, (node, a1), ())], check=check)


# 001E9E60's packets in the scratchpad (an EE packet cursor may point there):
# the cursor is placed so that the 5-quadword packet starts at 0x70003AC0,
# 0x70003AD0 or 0x70003AE0, so its 00102958 copy of the four quadwords at
# 0x70003AC0 writes over quadwords of its own source that it has not yet
# stored from. The original loads all four before the first store; the case
# checks that the packet landed there.
SPAD_PACKET = (0x70003AC0, 0x70003AD0, 0x70003AE0)


def spad_packet(item):
    beat, target = item
    ram, spad = case_ram(beat), image(beat)[1]
    node = pool(ram, {0x15A2C0})[0]
    a1 = struct.unpack_from('<H', ram, node + 0xE)[0]
    ctx = w32(ram, 0x275670)
    probe = oracle_ee(spad)
    logging_hooks(probe)
    oracle_call(probe, 0x1E9E60, (node, a1))
    blks = [c[-1] for c in probe.calls if c[0] == 0x1CB5F0]
    put(ram, ctx + 0x18, target - (blks[6] - w32(ram, ctx + 0x18)))
    check = lambda ee: [c[-1] for c in ee.calls if c[0] == 0x1CB5F0][6] == target
    return run_both(('spad packet', beat, hex(target)), ram, spad, [(0x1E9E60, (node, a1), ())], check=check)


# Stores that change a word the original reads later (sweep-2 review). Each
# case asserts that the changing store happened.
#   count (001D5BD0): the table's count word is read once, before the loop.
#     The cursor is placed so that record 0's end tag starts at base - 4
#     (base = *D_0028A5A4), so its addr word (0) lands on the count word
#     while the loop runs. The original still visits every record. Only the
#     beats whose record 0 builds a chain (a01_04, a01_05) have that tag;
#     the others are skipped.
#   recword (001D5170): the record's vertex count (rec +0) is read after the
#     slot tag and 001D1F80. The slot tag is placed at rec - 4, so its addr
#     word (the slot address) lands on rec +0 first, and the original runs
#     the chunk loop over that count.
#   endtag_v (001D5170 / 001D4FC0): rec +0x34 (001CAAC0's vector x) is read
#     after the end tag. The count is 3 * 0x1F8 and the first chunk tag is
#     placed at rec, so the end tag is at rec + 0x30 and its addr word (0)
#     lands on rec +0x34.
#   subject (001F4A10): D_00275B44 is read after the end tag. The context is
#     copied to 0x275B28, so its cursor word (+0x1C) is D_00275B44 itself:
#     the packets go to S = 0x1A00000 (D_00275B44 = S, holding a different
#     position quadword every 16 bytes) and the end tag's cursor store moves
#     D_00275B44 before it is read. The quadword load of the moved context's
#     +0xA0 reads 0x275BC0..CF, set to x = 1e9, z = 1000, w = 1, so the key
#     follows the point.
#   table (001F4A10): D_0028A56C is read after 001C7900. The context is
#     copied to 0x28A550, so its cursor word (+0x1C) is D_0028A56C: 001C7900's
#     cursor store moves the table pointer to the cursor after its packet,
#     where a copy of the 0x30000 bytes of the 001C6120 table is placed.
REREAD2 = (('001D5BD0', 'count'), ('001D5170', 'recword'), ('001D5170', 'endtag_v'), ('001D4FC0', 'endtag_v'),
           ('001F4A10', 'subject'), ('001F4A10', 'table'))


def put_bytes(ram, at, blob):
    """Copy blob into the scratch RAM at `at`, recording the pages patched."""
    at &= 0x1FFFFFF
    ram[at:at + len(blob)] = blob
    work().scratch.dirty.update(pages(at, len(blob)))


def first_call_cursor(ram, spad, routine, args, worker, after=True):
    """The channel-3 cursor at the first call of `worker` (after it returns,
    or at its entry) in an oracle run of `routine`, or None if no call."""
    ctx = w32(ram, 0x275670)
    ee = oracle_ee(spad)
    logging_hooks(ee)
    got, inner = [], ee.hooks[worker]

    def hook(e):
        if not after:
            got.append(e.load(ctx + 0x1C))
        inner(e)
        if after:
            got.append(e.load(ctx + 0x1C))
    ee.hooks[worker] = hook
    oracle_call(ee, int(routine, 16), args)
    return got[0] if got else None


def reread2(item):
    beat, routine, shape = item
    ram, spad = case_ram(beat), bytearray(image(beat)[1])
    where = ('reread alias 2', beat, routine, shape)
    ctx = w32(ram, 0x275670)
    captured = w32(ram, ctx + 0x1C)
    base = w32(ram, 0x28A5A4)
    rec = base + 0x10
    if shape == 'count':
        at = first_call_cursor(ram, bytes(spad), routine, (), 0x1CAAC0, after=False)
        if at is None:
            return 'skipped'
        # 001CAAC0 is called right after the end tag: that tag is at at - 0x10.
        put(ram, ctx + 0x1C, captured + (base - 4) - (at - 0x10))
        check = lambda ee: (any(not s and a == base and d == bytes(4) for s, a, d in ee.journal)
                            and sum(1 for c in ee.calls if c[0] == 0x1CAAC0) > 1)
        return run_both(where, ram, bytes(spad), [(0x1D5BD0, (), ())], check=check)
    if shape in ('recword', 'endtag_v'):
        if shape == 'recword':
            worker, target = 0x1D2090, rec - 4
        else:
            put(ram, rec, 3 * 0x1F8)
            worker, target = 0x1D1F80, rec
        shift = tag_site_cursor(ram, bytes(spad), routine, worker) - captured
        put(ram, ctx + 0x1C, target - shift)
        assert tag_site_cursor(ram, bytes(spad), routine, worker) == target, where
        if shape == 'recword':
            check = lambda ee: stored(ee, rec, 4) and ee.load(rec) != w32(ram, rec)
        else:
            check = lambda ee: any(not s and a == rec + 0x34 and d == bytes(4) for s, a, d in ee.journal)
        return run_both(where, ram, bytes(spad), [(int(routine, 16), (rec,), ())], check=check)
    node = f4a10_setup(ram, spad)
    if shape == 'subject':
        subject = w32(ram, 0x275B44)
        q = [w32(ram, subject + 0xB0 + 4 * k) for k in range(4)]
        fl = [struct.unpack('<f', struct.pack('<I', v))[0] for v in q[:3]]
        s = 0x1A00000
        blob = bytearray()
        for i in range(0x400):
            blob += struct.pack('<4I', *[F(fl[k] + 3.0 * i) for k in range(3)], q[3])
        put_bytes(ram, s, bytes(blob))
        copy_context(ram, 0x275B28)
        put(ram, 0x275B44, s)
        put(ram, 0x275BC0, F(1.0e9))
        put(ram, 0x275BC8, F(1000.0))
        put(ram, 0x275BCC, F(1.0))
        check = lambda ee: stored(ee, 0x275B44) and ee.load(0x275B44) != s
    else:
        table = w32(ram, 0x28A56C)
        after = first_call_cursor(ram, bytes(spad), routine, (node + 0xD0, 0x700038B0), 0x1C7900)
        blob = bytes(ram[table:table + 0x30000])
        copy_context(ram, 0x28A550)
        put_bytes(ram, after, blob)
        check = lambda ee: any(c[0] == 0x1C6120 and c[1] == after for c in ee.calls)
    return run_both(where, ram, bytes(spad), [(0x1F4A10, (node + 0xD0, 0x700038B0), ())], check=check)


# A cursor store that carries into the cursor's upper half: the tag starts
# 0x10 below a 64 KiB boundary (the shared tag helper at the slot tag), or
# 001D4FC0's inline first tag starts 0x80 below one (cursor + 0xF0). Each
# case asserts the carried cursor was stored.
CARRY = (('001D5170', 'slot'), ('001D4FC0', 'slot'), ('001D4FC0', 'first'))


def cursor_carry(item):
    beat, routine, site = item
    ram, spad = case_ram(beat), image(beat)[1]
    rec = w32(ram, 0x28A5A4) + 0x10
    ctx = w32(ram, 0x275670)
    c = w32(ram, ctx + 0x1C)
    if site == 'first':
        target = (c & ~0xFFFF) + 0x1FF80
        put(ram, ctx + 0x1C, target)
        want = target + 0xF0
    else:
        target = (c & ~0xFFFF) + 0x1FFF0
        shift = tag_site_cursor(ram, spad, routine, 0x1D2090) - c
        put(ram, ctx + 0x1C, target - shift)
        assert tag_site_cursor(ram, spad, routine, 0x1D2090) == target, (beat, routine, site)
        want = target + 0x10
    check = lambda ee: any(not s and a == ctx + 0x1C and d == want.to_bytes(4, 'little') for s, a, d in ee.journal)
    return run_both(('cursor carry', beat, routine, site), ram, spad, [(int(routine, 16), (rec,), ())], check=check)


# ---- hud ----------------------------------------------------------------

def hud_capture(beat):
    ram, spad = case_ram(beat), image(beat)[1]
    out = [run_both((beat, '001E8B90'), ram, spad, [(0x1E8B90, (PLAYER + 0xB0,), (F(1.0),))])]
    for node in pool(ram, {0x15A2C0}):
        a1 = struct.unpack_from('<H', ram, node + 0xE)[0]
        out.append(run_both((beat, '001E9E60', hex(node)), ram, spad, [(0x1E9E60, (node, a1), ())]))
    for node in pool(ram, {0x1EA240}):
        mem = case_ram(beat)
        put(mem, 0x275C34, node + 0x1F0)
        for h in (0x1EAF00, 0x1EAF80, 0x1EB020, 0x1EC270):
            out.append(run_both((beat, hex(h), hex(node)), mem, spad, [(h, (node + 0xD0, 0x2000), ())]))
    return out


def grid_point(rng, ram):
    """A position for 001E8B90: in, on the edge of, or outside a live record."""
    grid = w32(ram, 0x275C20)
    rec = grid + 0xA060 * rng.randrange(4)
    fl = lambda a: struct.unpack('<f', struct.pack('<I', w32(ram, a)))[0]
    x0, y0, z0, w, d = fl(rec), fl(rec + 4), fl(rec + 8), fl(rec + 0x30), fl(rec + 0x34)
    ok = all(abs(v) < 1e6 for v in (x0, y0, z0, w, d))
    if not ok:
        x0, y0, z0, w, d = fl(grid), fl(grid + 4), fl(grid + 8), fl(grid + 0x30), fl(grid + 0x34)
    pick = rng.random()
    if pick < 0.6:
        x, z = x0 + w * rng.uniform(-0.05, 1.05), z0 + d * rng.uniform(-0.05, 1.05)
    elif pick < 0.75:
        x, z = rng.choice((x0, x0 + w, x0 + w / 64)), rng.choice((z0, z0 + d, z0 + d / 64))
    else:
        x, z = x0 + rng.uniform(-2 * abs(w) - 10, 2 * abs(w) + 10), z0 + rng.uniform(-2 * abs(d) - 10, 2 * abs(d) + 10)
    y = rng.choice((y0, y0 + 10.9, y0 + 11.0, y0 + 11.5, y0 - 50, y0 + rng.uniform(-20, 20)))
    return [F(x), F(y), F(z)]


def hud_unit(seed):
    rng = random.Random('hud:%d' % seed)
    beat = MAIN[seed % len(MAIN)]
    ram, spad = case_ram(beat), image(beat)[1]
    kind = rng.random()
    if kind < 0.45:
        point = grid_point(rng, ram)
        if rng.random() < 0.05:
            point[rng.randrange(3)] = rng.choice(SPECIAL)
        for k, w in enumerate(point): put(ram, PLAYER + 0xB0 + 4 * k, w)
        if rng.random() < 0.12:
            put(ram, 0x810700, rng.choice((0x15, 0x10, 0x0B, 0)), 1)
        if rng.random() < 0.2:  # other records live or dead
            grid = w32(ram, 0x275C20)
            put(ram, grid + 0xA060 * rng.randrange(4) + 0x54, rng.choice((0, 1)))
        speed = rfloat(rng, -3, 3, 0.02) if rng.random() < 0.7 else F(rng.choice((1.0, 0.5, 2.0)))
        return run_both(('hud 001E8B90', seed, beat), ram, spad, [(0x1E8B90, (PLAYER + 0xB0,), (speed,))])
    if kind < 0.7:
        nodes = pool(ram, {0x15A2C0}) or [PLAYER]
        node = rng.choice(nodes)
        a1 = rng.choice((struct.unpack_from('<H', ram, node + 0xE)[0], 0, 1, 2, 3))
        for off, lo, hi in ((0x80, -0.5, 1.5), (0x8C, -3, 3), (0xB4, -100, 100)):
            if rng.random() < 0.5:
                put(ram, node + off, rfloat(rng, lo, hi, 0.03))
        if rng.random() < 0.5:
            orig = (w32(ram, 0x275C1C) + a1 * 0xA060) & MASK
            put(ram, orig + 0x5C, rng.choice((0, 1, 2, 0xFF)), 1)
        if rng.random() < 0.5:  # the record words the 9-quadword packet computes from
            orig = (w32(ram, 0x275C1C) + a1 * 0xA060) & MASK
            for off in (0x10, 0x14, 0x18, 0x1C, 0x38, 0x3C, 0x40, 0x44, 0x48, 0x4C, 0x50):
                if rng.random() < 0.5:
                    put(ram, orig + off, rfloat(rng, -500, 500, 0.03))
        return run_both(('hud 001E9E60', seed, beat), ram, spad, [(0x1E9E60, (node, a1), ())])
    nodes = pool(ram, {0x1EA240}) or [PLAYER]
    node = rng.choice(nodes)
    work = node + 0x1F0 if rng.random() < 0.8 else w32(ram, 0x275C34)
    put(ram, 0x275C34, work)
    if rng.random() < 0.5:
        put(ram, work + 4, rng.getrandbits(32))
    for off in (0x54, 0x5C):
        if rng.random() < 0.3:
            put(ram, work + off, rfloat(rng, 0, 3, 0.02))
    h = rng.choice((0x1EAF00, 0x1EAF80, 0x1EB020, 0x1EC270))
    depth = rng.choice((0x2000, 0x100, 0xFFFFFF, rng.randrange(0, 0x1000000)))
    return run_both(('hud handler', seed, beat, hex(h)), ram, spad, [(h, (node + 0xD0, depth), ())])


# Quick mode also runs these unit seeds: each is the full-mode case that
# kills a mutant no other quick case kills (the sweep in docs/AREA01_RENDER.md
# section 3): gs 1680 (001CD180's 0.5 * f12 as a host multiply), 344 (n - f14
# as a host subtract), 637 (1 - v as a host subtract); hud 77 (001E8B90's
# p.x - x0 as a host subtract), 4494 (001E9E60's MADD split into a multiply
# and an add).
GS_PIN = {1680, 344, 637}
HUD_PIN = {77, 4494}
HUD_BYTE = ((0x5D, 1), (0x5E, 1), (0x5F, 1), (0x5C, 0x81), (0x5C, 0x41))
HUD_BOUNDARY = ('area15', 'area10', 'far_corner', 'near_corner', 'z_between_d_w', 'x_between_d_w')
# 001E8B90's two "cell index below 0" outcomes cannot occur: the box test
# admits only x with x > x0 and x <= x0 + w, which forces w > 0 (an EE add of
# a w <= 0 never exceeds x0) and x - x0 >= 0 (the subtraction truncates
# toward zero), so 32 * (x - x0) / w and its float_to_int are never negative;
# likewise for z. Exponent-255 patterns do not change this (the compares
# saturate them to +-MAX first).
IMPOSSIBLE = {(0x1E8CE8, False), (0x1E8D08, False)}


def hud_boundary(kind):
    """001E8B90 on the boundaries a random sample may miss: the two skipped
    area bytes, a point exactly on the far box corner (cell 32 -> 31) and
    just inside the near one, and a point whose z (or x) lies between the
    record's two extents (+0x34 d and +0x30 w differ in AREA01: 50 and 62),
    so a z bound built from w (or an x bound from d) would admit it."""
    ram, spad = case_ram(MAIN[0]), image(MAIN[0])[1]
    grid = w32(ram, 0x275C20)
    x0, y0, z0, w, d = (w32(ram, grid + o) for o in (0, 4, 8, 0x30, 0x34))
    fl = lambda b: struct.unpack('<f', struct.pack('<I', b))[0]
    point = [F(fl(x0) + fl(w) / 2), F(fl(y0)), F(fl(z0) + fl(d) / 2)]
    if kind == 'area15':
        put(ram, 0x810700, 0x15, 1)
    elif kind == 'area10':
        put(ram, 0x810700, 0x10, 1)
    elif kind == 'far_corner':
        point = [F(fl(x0) + fl(w)), F(fl(y0)), F(fl(z0) + fl(d))]
    elif kind == 'z_between_d_w':
        point = [F(fl(x0) + fl(w) / 2), F(fl(y0)), F(fl(z0) + (fl(d) + fl(w)) / 2)]
    elif kind == 'x_between_d_w':  # the record with w < d swapped in: +0x30 <-> +0x34
        put(ram, grid + 0x30, d)
        put(ram, grid + 0x34, w)
        point = [F(fl(x0) + (fl(d) + fl(w)) / 2), F(fl(y0)), F(fl(z0) + fl(w) / 2)]
    else:
        point = [F(fl(x0) + fl(w) / 4096), F(fl(y0)), F(fl(z0) + fl(d) / 4096)]
    for k, v in enumerate(point): put(ram, PLAYER + 0xB0 + 4 * k, v)
    return run_both(('hud boundary', kind), ram, spad, [(0x1E8B90, (PLAYER + 0xB0,), (F(1.0),))])


# 001E8B90's box bounds where the EE's truncating add and a host add
# differ: x0 = 1.7 (0x3FD9999A), w = 32 sum to 0x4206CCCC on the EE and
# 0x4206CCCD on the host; p.x = 0x4206CCCD is outside the original's box
# (xw_round), and likewise for z0 + d (zd_round). subx_round puts p.x - x0
# where the EE's truncated difference and a rounded one differ (x0 =
# -1.5 * 2**-21, p.x = 16 - 2**-20), which moves the mapped cell's
# float_to_int argument. Record 0 of the beat's grid gets the box; the other
# three records are dead.
HUD_ROUND = ('xw_round', 'zd_round', 'subx_round')


def hud_round(kind):
    ram, spad = case_ram(MAIN[0]), image(MAIN[0])[1]
    grid = w32(ram, 0x275C20)
    x0, p = 0, [F(5.5), 0, F(5.5)]
    z0 = 0
    if kind == 'xw_round':
        x0, p[0] = 0x3FD9999A, 0x4206CCCD
    elif kind == 'zd_round':
        z0, p[2] = 0x3FD9999A, 0x4206CCCD
    else:
        x0, p[0] = F(-1.5 * 2.0 ** -21), F(16.0 - 2.0 ** -20)
    for off, v in ((0, x0), (4, 0), (8, z0), (0x30, F(32.0)), (0x34, F(32.0))): put(ram, grid + off, v)
    put(ram, grid + 0x54, 1)
    for n in (1, 2, 3): put(ram, grid + 0xA060 * n + 0x54, 0)
    for k, v in enumerate(p): put(ram, PLAYER + 0xB0 + 4 * k, v)
    return run_both(('hud round', kind), ram, spad, [(0x1E8B90, (PLAYER + 0xB0,), (F(1.0),))])


# 001E8B90's box tests and live test on words a host compare would read
# differently. Record 0 of the a01_00 grid gets the box (x0, y0, z0, w, d)
# with +0x54 = active; the other three records are dead. A sub-normal
# coordinate counts as 0 in the EE compares (DAZ); a host compare would not:
#   x_denorm_hi: x0 = -1, w = 1 (x0 + w = +0), p.x = 0x00000001: inside;
#   x_denorm_lo: x0 = +0, p.x = 0x00000001: outside (x <= x0);
#   z_denorm_hi / z_denorm_lo: the same for z0, d and p.z;
#   y_denorm: y0 = -11 (11 + y0 = +0), p.y = 0x80000001: outside (not y < +0).
# active_neg / active_high: +0x54 = 0x80000000 / 0x00010000 is live (any
# nonzero word).
# w_nan: w = 0x7FC00000, which the EE treats as a number: x0 + w saturates to
# MAX, so p.x = 1 is inside, and px = 32 * (p.x - x0) / w is finite (a host
# divide would give a NaN). dz_big: d = 2**125 and p.z = 1.5 * 2**124, so
# 32 * (p.z - z0) passes MAX: the EE saturates (a host multiply gives inf).
BOX = {'x_denorm_hi': ((F(-1.0), 0, 0, F(1.0), F(32.0), 1), (1, 0, F(5.5))),
       'x_denorm_lo': ((0, 0, 0, F(32.0), F(32.0), 1), (1, 0, F(5.5))),
       'z_denorm_hi': ((0, 0, F(-1.0), F(32.0), F(1.0), 1), (F(5.5), 0, 1)),
       'z_denorm_lo': ((0, 0, 0, F(32.0), F(32.0), 1), (F(5.5), 0, 1)),
       'y_denorm': ((0, F(-11.0), 0, F(32.0), F(32.0), 1), (F(5.5), 0x80000001, F(5.5))),
       'active_neg': ((0, 0, 0, F(32.0), F(32.0), 0x80000000), (F(5.5), 0, F(5.5))),
       'active_high': ((0, 0, 0, F(32.0), F(32.0), 0x00010000), (F(5.5), 0, F(5.5))),
       'w_nan': ((0, 0, 0, 0x7FC00000, F(32.0), 1), (F(1.0), 0, F(5.5))),
       'dz_big': ((0, 0, 0, F(32.0), 0x7E000000, 1), (F(5.5), 0, 0x7DC00000))}


def hud_grid_box(ram, box, point):
    grid = w32(ram, 0x275C20)
    x0, y0, z0, w, d, active = box
    for off, v in ((0, x0), (4, y0), (8, z0), (0x30, w), (0x34, d), (0x54, active)): put(ram, grid + off, v)
    for n in (1, 2, 3): put(ram, grid + 0xA060 * n + 0x54, 0)
    put(ram, 0x810700, 0, 1)
    for k, v in enumerate(point): put(ram, PLAYER + 0xB0 + 4 * k, v)
    return grid


def hud_box(kind):
    ram, spad = case_ram(MAIN[0]), image(MAIN[0])[1]
    hud_grid_box(ram, *BOX[kind])
    return run_both(('hud box', kind), ram, spad, [(0x1E8B90, (PLAYER + 0xB0,), (F(1.0),))])


# 001E8B90 at a mixed corner of a 32 x 32 box at the origin: p at col 0 /
# row 31 (x clamps low, z clamps high) or col 31 / row 0. The clamped
# neighbours fall on the same cell, which then gets an edge, a corner, the
# centre and an edge add, in the original's order (column offset dc outer,
# row offset dr inner: at col 0 / row 31 the edge from (dc -1, dr 0), the
# corner from (-1, +1), the centre, then the edge from (0, +1)). The float
# sums differ with the order. At a same-side corner (both low) both loop
# orders happen to give the same sequence, so only mixed corners test it.
MIXED = [(pos, f12, cell) for pos in ((0.5, 31.5), (31.5, 0.5)) for f12 in (1.1, 0.3, 1e-3, 12345.678, 3.3e-5, 7.77)
         for cell in (0.1, 1e7 + 0.3, -2.5e-3, 123.456)]


def hud_mixed(item):
    (px, pz), f12, cell = item
    ram, spad = case_ram(MAIN[0]), image(MAIN[0])[1]
    grid = hud_grid_box(ram, (0, 0, 0, F(32.0), F(32.0), 1), (F(px), 0, F(pz)))
    col, row = int(px), int(pz)
    put(ram, grid + 0x9060 + (col << 2) + (row << 7), F(cell))
    return run_both(('hud mixed corner', px, pz, f12, cell), ram, spad, [(0x1E8B90, (PLAYER + 0xB0,), (F(f12),))])


# 001E9E60's record +0x5C is read as a byte (the 001CB950 word pair picks on
# == 1): each case sets +0x5C = 1 and then byte `off` to `value`: one of the
# bytes +0x5D..+0x5F to 1, or +0x5C itself to 0x81 or 0x41 (all eight bits
# are compared with 1; nothing bounds that record byte).
def hud_byte(item):
    off, value = item
    ram, spad = case_ram(MAIN[0]), image(MAIN[0])[1]
    node, a1, orig = e9e60_setup(ram)
    put(ram, orig + 0x5C, 1, 1)
    put(ram, orig + off, value, 1)
    return run_both(('hud byte', hex(off), hex(value)), ram, spad, [(0x1E9E60, (node, a1), ())])


# Pinned cases of the sweep-3 review (close-out). Each is an execution the
# EE completes (the oracle refuses anything else), and each asserts that it
# reached the state it is built for.
#   ldq_pair (001CD070 via the 001F8350 pairing): the point quadword at
#     0x700036D0, passed as p = 0x700036D0 + 1, + 2 or + 3. The EE's
#     quadword load drops the low four address bits, so bits 0 and 1 of a
#     load address are ignored as well as bits 2 and 3 (the gs unit cases
#     pass p at + 4, + 8 and + 0xC).
#   ldq_f4a10 (001F4A10): D_00275B44 + 1, + 2 or + 3 (the subject pointer
#     is used only by the quadword load of its +0xB0 point).
#   stq4 (001E9E60): every packet but the last starts 4 bytes past a
#     quadword. The context is copied to X = 0x1A00000; the packet cursor
#     (X + 0x18) is placed so that the 5-quadword packet starts at X - 0x2C,
#     so its 00102958 copy of 0x70003AC0.. stores its last quadword at
#     X + 0x10 (the low four bits of X + 0x14 dropped). That quadword's third
#     word is Y = 0x1B00000, so the cursor moves to Y and the 9-quadword
#     packet (whose doubleword stores need 8-alignment) starts at Y + 0x120.
#     So quadword stores 4 bytes off a quadword are EE executions; the
#     packet +8 group covers 8 bytes off.
#   rand_x (001F4A10): D_0024295C = v - 0x58 with v = 0x700038B0, so
#     00122BB8's LCG store lands on v + 0 (the x word), which the original
#     reads after that call. The state s0 is chosen so that the new state is
#     0x80.
#   t_alias (001E9E60): the 9-quadword packet at node + 0x70, so its +0x10
#     word copy (record +0x48) lands on a0 + 0x80, the blend t, before the
#     original reads t.
#   cd180_x / cd180_y (001CD180, direct): SPAD 0x70003600 x = 0x7FFFFFF0
#     with f12 = -6 or -600 (x - hw wraps in 32 bits and passes the first
#     bound), or y = 0x7FFFFFF0 with f13 = 1e30 or 3e38 (y + hh wraps
#     negative and fails the last bound); the other coordinate is 0x8000.
#     The case asserts the wrap, from the extents an early-return run leaves
#     at 0x70003610 (as the 001CD180 bound cases find them).
PIN3 = (('ldq_pair', 1), ('ldq_pair', 2), ('ldq_pair', 3), ('ldq_f4a10', 1), ('ldq_f4a10', 2),
        ('ldq_f4a10', 3), ('stq4', 0), ('rand_x', 0), ('t_alias', 0), ('cd180_x', -6.0),
        ('cd180_x', -600.0), ('cd180_y', 1e30), ('cd180_y', 3e38))


def pin3(item):
    beat, kind, arg = item
    ram, spad = case_ram(beat), bytearray(image(beat)[1])
    where = ('pin3', beat, kind, arg)
    if kind == 'ldq_pair':
        subject = w32(ram, 0x275B44)
        for i in range(4):
            sput(spad, 0x700036D0 + 4 * i, w32(ram, subject + 0xB0 + 4 * i))
        return run_both(where, ram, bytes(spad), [(0x1CD070, (0x700036D0 + arg, 0x30), ()),
                                                  (0x1CD2B0, (), (F(6.0), F(6.0), F(96.0), F(32.0)))])
    if kind == 'ldq_f4a10':
        node = f4a10_setup(ram, spad)
        put(ram, 0x275B44, w32(ram, 0x275B44) + arg)
        return run_both(where, ram, bytes(spad), [(0x1F4A10, (node + 0xD0, 0x700038B0), ())])
    if kind == 'stq4':
        node, a1, _ = e9e60_setup(ram)
        x = copy_context(ram, 0x1A00000)
        y = 0x1B00000
        blk6 = x - 0x2C
        put(ram, x + 0x18, blk6 - 0x120 - 6 * 0x1C0)
        for k, w in enumerate((w32(ram, x + 0x10), w32(ram, x + 0x14), y, w32(ram, x + 0x1C))):
            sput(spad, 0x70003AF0 + 4 * k, w)

        def check(ee):
            blks = [c[-1] for c in ee.calls if c[0] == 0x1CB5F0]
            return all(b & 15 == 4 for b in blks[:7]) and blks[6] == blk6 and blks[7] == y + 0x120
        return run_both(where, ram, bytes(spad), [(0x1E9E60, (node, a1), ())], check=check)
    if kind == 'rand_x':
        node = f4a10_setup(ram, spad)
        s0 = ((0x80 - 0x3039) * pow(0x41C64E6D, -1, 1 << 32)) & MASK
        sput(spad, 0x700038B0, s0)
        put(ram, 0x24295C, 0x700038B0 - 0x58)
        check = lambda ee: any(c[0] == 0x122BB8 for c in ee.calls) and ee.load(0x700038B0) == 0x80
        return run_both(where, ram, bytes(spad), [(0x1F4A10, (node + 0xD0, 0x700038B0), ())], check=check)
    if kind == 't_alias':
        node, a1, orig = e9e60_setup(ram)
        ctx = w32(ram, 0x275670)
        blks = e9e60_blocks(ram, bytes(spad), node, a1)
        put(ram, ctx + 0x18, (w32(ram, ctx + 0x18) + node + 0x70 - blks[7]) & MASK)
        want = w32(ram, orig + 0x48)
        assert want != w32(ram, node + 0x80), (where, 'record +0x48 equals the old t')

        def check(ee):
            return [c[-1] for c in ee.calls if c[0] == 0x1CB5F0][7] == node + 0x70 and ee.load(node + 0x80) == want
        return run_both(where, ram, bytes(spad), [(0x1E9E60, (node, a1), ())], check=check)
    # cd180_x / cd180_y
    fargs = (F(arg), F(6.0)) if kind == 'cd180_x' else (F(6.0), F(arg))
    sput(spad, 0x70003600, 0x40000000)
    probe = oracle_ee(bytes(spad))
    logging_hooks(probe)
    oracle_call(probe, 0x1CD180, (), fargs)
    hw, hh = s32(probe.load(0x70003610)), s32(probe.load(0x70003614))
    cx, cy = (0x7FFFFFF0, 0x8000) if kind == 'cd180_x' else (0x8000, 0x7FFFFFF0)
    if kind == 'cd180_x':
        assert cx - hw >= 0x9001 and s32((cx - hw) & MASK) < 0x9001, (where, 'no wrap', hw)
    else:
        assert cy + hh >= 0x7900 and s32((cy + hh) & MASK) < 0x7900, (where, 'no wrap', hh)
    sput(spad, 0x70003600, cx)
    sput(spad, 0x70003604, cy)
    return run_both(where, ram, bytes(spad), [(0x1CD180, (), fargs)])


# ---- frame --------------------------------------------------------------

FRAME_B = (0, 1, 2, 3, 4, 5)
FRAME_STATE = (0, 1, 2, 3, 4, 5)
FRAME_CHECK = (0, 1, -1)
FRAME_BASE = 'a01_00_train_room'


# FRAME_HI: the state-setting path (b = 1, check 0) with the player's bytes
# +7 and +0x1F0's neighbour +0x1F1 nonzero; the byte stores at +6 and
# +0x1F0 must leave them.
FRAME_HI = [(1, s, 0, True) for s in (3, 0, 1)]


def frame_case(item):
    b, state, check = item[:3]
    ram, spad = case_ram(FRAME_BASE), bytearray(image(FRAME_BASE)[1])
    spad[0x3B8D] = b
    put(ram, PLAYER + 5, state, 1)
    if len(item) > 3:
        put(ram, PLAYER + 7, 0x5A, 1)
        put(ram, PLAYER + 0x1F1, 0xA5, 1)
    return run_both(('frame',) + tuple(item), ram, bytes(spad), [(0x15B610, (PLAYER,), ())], script=check)


# ======================================================================
# Native-only fault cases
# ======================================================================

def first_access(address, args, fargs=()):
    """The first data address the ORIGINAL routine reads or writes outside
    its stack, before any worker call, on the scratch RAM (instruction
    fetches excluded; a quadword access reports its aligned address, as the
    interpreter issues it), or None when a worker call comes first."""
    ee = oracle_ee(image(MAIN[0])[1])
    if ENTRIES[address][0] == 'frame':
        frame_hooks(ee, 0)
    else:
        logging_hooks(ee)
    seen = []

    class Stop(Exception):
        pass

    def note(a):
        a &= MASK
        if not seen and a < 0x7F000000:
            seen.append(a)
            raise Stop

    load, save = ee.load, ee.save

    def load_w(a, size=4):
        if sys._getframe(1).f_code.co_name != 'run':     # not an instruction fetch
            note(a)
        return load(a, size)

    def save_w(a, value, size=4):
        note(a)
        return save(a, value, size)
    ee.load, ee.save = load_w, save_w
    for w in list(ee.hooks):
        inner = ee.hooks[w]

        def hook(e, inner=inner):
            if not seen:
                seen.append(None)
                raise Stop
            inner(e)
        ee.hooks[w] = hook
    try:
        oracle_call(ee, address, args, fargs)
    except Stop:
        pass
    return seen[0] if seen else None


CHILD_NOTE = None   # in an in_child() child: the pipe for progress notes


def note(text):
    """Record where an in_child() child is, for the report if it crashes."""
    if CHILD_NOTE is not None:
        os.write(CHILD_NOTE, (str(text).replace('\n', ' ') + '\n').encode())


def in_child(fn, what):
    """fn() in a forked child; its int result, or an AssertionError naming
    what failed, including a crash (a signal) of the child and its last
    note()."""
    global CHILD_NOTE
    if 'fork' not in multiprocessing.get_all_start_methods():
        return fn()
    r, w = os.pipe()
    pid = os.fork()
    if pid == 0:
        os.close(r)
        CHILD_NOTE = w
        code, msg = 1, ''
        try:
            msg, code = 'result %d' % fn(), 0
        except BaseException as e:  # noqa: BLE001 - reported to the parent
            msg = 'error ' + repr(e)[:4000]
        note(msg)
        os._exit(code)
    os.close(w)
    chunks = []
    while True:
        b = os.read(r, 65536)
        if not b:
            break
        chunks.append(b)
    os.close(r)
    _, status = os.waitpid(pid, 0)
    lines = b''.join(chunks).decode(errors='replace').splitlines() or ['']
    if os.WIFSIGNALED(status):
        raise AssertionError((what, 'native process crashed', 'signal', os.WTERMSIG(status), 'last at', lines[-1]))
    assert os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0 and lines[-1].startswith('result '), \
        (what, lines[-1])
    return int(lines[-1].split()[1])


def fault_cases():
    """The native-only fault group, in a forked child: a module that skipped
    its need() check would call through a NULL worker pointer, and one with a
    wrong view bound would dereference a wild pointer; either kills the
    process, which the child turns into a failure. (The child makes its own
    buffers: work().)"""
    return in_child(fault_group, 'fault cases')


FAIL_SEEN = {}   # entry -> worker calls it makes in the failing-worker plan (fault_group)


def fault_group():
    ram = case_ram(MAIN[0])
    spad = image(MAIN[0])[1]
    count = 0
    rec = w32(ram, 0x28A5A4) + 0x10
    node = (pool(ram, {0x158BD0, 0x158D30}) or [PLAYER - 0xD0])[0]
    # every native entry, with its real argument shape
    plans = [('vif', 0x1D5BD0, ()), ('vif', 0x1D5A70, (0, rec + 0x40)), ('vif', 0x1D4FC0, (rec,)),
             ('vif', 0x1D5170, (rec,)), ('gs', 0x1CD070, (0x70003AC0, 0)), ('gs', 0x1CD180, (F(6.0), F(6.0))),
             ('gs', 0x1CD2B0, (F(6.0), F(6.0), F(96.0), F(32.0))), ('gs', 0x1F4A10, (node + 0xD0, 0x700038B0)),
             ('gs', 0x1F4CC0, (0x700038A0, 0x700038B0)), ('hud', 0x1E8B90, (PLAYER + 0xB0, F(1.0))),
             ('hud', 0x1E9E60, (PLAYER, 0))] + [('hud', h, (PLAYER + 0xD0, 0x2000)) for h in
                                                 (0x1EAF00, 0x1EAF80, 0x1EB020, 0x1EC270)] + [
             ('frame', 0x15B610, (PLAYER,))]
    assert sorted(a for _, a, _ in plans) == sorted(ENTRIES)

    def per_entry():
        k = 0
        for module, address, args in plans:
            # every reachable worker unbound in turn: a fault naming that worker
            # before any byte is written or any worker runs
            reach = required(address)
            for name, _ in MODULES[module][1]._fields_[1:]:
                if name not in reach:
                    continue
                n = Native(module, spad, unbind=name, script=0)
                note((hex(address), 'unbound', name))
                rc, _ = n.call(address, *args)
                f = n.state.core.fault
                assert rc == -1 and f.code == 1 and f.address == int(name[2:], 16), (hex(address), name, rc, f.code)
                assert n.unchanged(spad) and not n.log, (hex(address), name)
                k += 1
            # a latched fault refuses everything
            n = Native(module, spad, script=0)
            n.state.core.fault.code = 2
            rc, _ = n.call(address, *args)
            assert rc == -1 and n.unchanged(spad) and not n.log
            # no views, and the RAM view present with NULL bytes: the first
            # memory access faults, and the fault names that access's address
            # (the original's first data address). 001F4CC0 reads and writes no
            # memory itself: its workers run.
            split = {0x1CD180: 0, 0x1CD2B0: 0, 0x1E8B90: 1}.get(address, len(args))
            want = first_access(address, args[:split], args[split:])
            for kind in ('none', 'null'):
                note((hex(address), 'views', kind))
                n = Native(module, spad, script=0, views=kind == 'null')
                if kind == 'null':
                    n.views[0].bytes = None
                    n.views[1].bytes = None
                rc, _ = n.call(address, *args)
                f = n.state.core.fault
                if want is None:
                    assert rc == 0 and f.code == 0, (hex(address), kind, f.code)
                else:
                    assert rc == -1 and f.code == 4 and f.address == address and f.detail == want, \
                        (hex(address), kind, rc, f.code, hex(f.address), hex(f.detail), hex(want))
                    assert not n.log, (hex(address), kind)
                k += 1
            k += 1
        return k
    count += per_entry()
    # an access whose end would pass 2**32 (0xFFFFFFF0 + 16, 0xFFFFFFFC + 4)
    # lies in no view: it faults with its address. (On the EE, 0xFFFF8000..
    # 0xFFFFFFFF is TLB-mapped kernel memory, outside the model, so the
    # oracle refuses these and they run on the native side only.)
    for module, address, args, want in (('gs', 0x1CD070, (0xFFFFFFF0, 0), 0xFFFFFFF0),
                                        ('gs', 0x1F4A10, (node + 0xD0, 0xFFFFFFF0), 0xFFFFFFFC)):
        note((hex(address), 'wrap'))
        n = Native(module, spad, script=0)
        rc, _ = n.call(address, *args)
        f = n.state.core.fault
        assert rc == -1 and f.code == 4 and f.address == address and f.detail == want, \
            (hex(address), 'wrap', rc, f.code, hex(f.detail))
        assert not n.log
        count += 1
    # A view that ends exactly where an access starts, and one that starts
    # just after an access: 001E8B90 with the RAM view ending at 0x810700
    # (its first data address, the area byte) must fault there; 001CD2B0
    # with the RAM view starting at 0x275C04 (it covers the D_00275C04 word
    # its inline 001CD180 body reads, not D_00275C00) must fault at its own
    # store of n to 0x275C00 and name 001CD2B0, not 001CD180.
    note(('0x1e8b90', 'view end'))
    n = Native('hud', spad, script=0)
    n.views[0].size = 0x810700
    rc, _ = n.call(0x1E8B90, PLAYER + 0xB0, F(1.0))
    f = n.state.core.fault
    assert rc == -1 and f.code == 4 and f.address == 0x1E8B90 and f.detail == 0x810700 and not n.log, \
        ('view end', rc, f.code, hex(f.address), hex(f.detail))
    count += 1
    note(('0x1cd2b0', 'view start'))
    n = Native('gs', spad, script=0)
    n.views[0].address = 0x275C04
    n.views[0].size = RAM_SIZE - 0x275C04
    n.views[0].bytes = C.cast(C.byref(n.W.native_c, 0x275C04), P(U8))
    rc, _ = n.call(0x1CD2B0, F(6.0), F(6.0), F(96.0), F(32.0))
    f = n.state.core.fault
    assert rc == -1 and f.code == 4 and f.address == 0x1CD2B0 and f.detail == 0x275C00 and not n.log, \
        ('view start', rc, f.code, hex(f.address), hex(f.detail))
    count += 1
    # A failing worker (a negative result) at every entry that has workers,
    # with its plan's arguments: the entry must return -1 with code 2 naming
    # that worker and make no further worker call. Two entries make no worker
    # call from a01_00's state, so they run from another: 001D5BD0 on a01_04
    # (whose records build chains) and 001E8B90 with the player inside a live
    # 32 x 32 record box. Quick mode fails the first call of each distinct
    # worker plus the first, second, middle and last call; full mode fails
    # every call.
    for module, address, args in plans:
        sp = spad
        if address == 0x1D5BD0:
            case_ram(MAIN[4])
            sp = image(MAIN[4])[1]
        elif address == 0x1E8B90:
            hud_grid_box(case_ram(MAIN[0]), (0, 0, 0, F(32.0), F(32.0), 1), (F(5.5), 0, F(5.5)))
        else:
            case_ram(MAIN[0])
        probe = Native(module, sp, script=0)
        assert probe.call(address, *args)[0] == 0, hex(address)
        calls = len(probe.log)
        FAIL_SEEN[address] = calls
        assert (calls > 0) == bool(required(address)), ('failing worker: no worker call', hex(address), calls)
        firsts = {probe.log[i][0]: i for i in reversed(range(calls))}
        positions = range(calls) if RM.FULL else \
            sorted(({0, 1, calls // 2, calls - 1} | set(firsts.values())) & set(range(calls)))
        for k in positions:
            note((hex(address), 'failing worker', k))
            n = Native(module, sp, fail_at=k, script=0)
            rc, _ = n.call(address, *args)
            f = n.state.core.fault
            assert rc == -1 and f.code == 2 and f.address == probe.log[k][0], (hex(address), k, rc, f.code)
            assert len(n.log) == k + 1 and n.log[:k] == probe.log[:k]
            count += 1
    return count


def required(address):
    """The workers an entry can reach (its need() mask)."""
    vif = {'w_00121870', 'w_001D2090', 'w_001D4750', 'w_001D1F80', 'w_001CAAC0'}
    handler = {'w_001CFB50', 'w_001CFBE0'}
    fields = {
        0x1D5BD0: vif, 0x1D5A70: set(), 0x1D4FC0: vif - {'w_001D4750'}, 0x1D5170: vif - {'w_00121870'},
        0x1CD070: {'w_001281C0'}, 0x1CD180: set(), 0x1CD2B0: set(),
        0x1F4A10: {'w_001281C0', 'w_00122BB8', 'w_001C7900', 'w_001C6120', 'w_001D3990', 'w_001CB760'},
        0x1F4CC0: {'w_0021B9A0', 'w_001F4BF0'},
        0x1E8B90: {'w_001281C0'}, 0x1E9E60: {'w_001CB5F0', 'w_001CB950', 'w_001CB6B0', 'w_001CB760'},
        0x1EAF00: handler, 0x1EAF80: handler, 0x1EB020: handler, 0x1EC270: handler,
        0x15B610: {'w_00182B30', 'w_00174A50', 'w_00182D70', 'w_00183240', 'w_00183250', 'w_001833F0',
                   'w_00183440', 'w_001834E0'},
    }
    return fields[address]


# ======================================================================

def all_branches():
    probe = EE(ELF)
    out = set()
    for start, size in SIZES.items():
        for pc in range(start, start + size, 4):
            word = probe.load(pc)
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            if op in (1, 4, 5, 6, 7, 20, 21, 22, 23) or (op == 17 and rs == 8):
                if not (op == 4 and rs == 0 and rt == 0):
                    out.add(pc)
    return out


def callee_set():
    probe = EE(ELF)
    targets = set()
    for start, size in SIZES.items():
        for pc in range(start, start + size, 4):
            word = probe.load(pc)
            if word >> 26 in (2, 3):
                target = (word & 0x3FFFFFF) << 2
                if target not in SIZES:
                    targets.add(target)
    return targets


POOL = None


def pmap(fn, items, key=None):
    """[fn(x) for x in items] on the process pool (at most JOBS = 4 worker
    processes that live for the whole run, so their buffers stay allocated).
    Items are handed out in runs sorted by key(x) (the capture beat), so a
    worker's buffer resets mostly stay within one image. Results come back
    in input order; the first failing item (in that order) raises."""
    items = list(items)
    if POOL is None or len(items) < 2:
        return [fn(x) for x in items]
    order = sorted(range(len(items)), key=lambda i: key(items[i])) if key else list(range(len(items)))
    size = max(1, -(-len(items) // (3 * JOBS)))
    out = [None] * len(items)
    for i, r in zip(order, POOL.imap(fn, [items[i] for i in order], chunksize=size)):
        out[i] = r
    return out


def unit_beat(seed):
    return MAIN[seed % len(MAIN)]


def first(item):
    return item[0]


def main():
    if AV.ENABLED:
        print('canonical callback mode: all original-instruction comparisons; view arrays disabled', flush=True)
    global ELF, NATIVE, TRACKER, POOL
    ELF = read_elf()
    NATIVE = build_native()
    TRACKER = build_tracker()
    assert BEATS, 'no AREA01 captures under ' + str(ROUTE)
    want = set(WORKERS) | set(FRAME_WORKERS) | INLINE
    assert callee_set() == want, ('callee set', sorted(map(hex, callee_set() ^ want)))
    base = EE(ELF)
    for beat in BEATS:
        check_code(image(beat)[0], beat, base)
    ref = image(BEATS[0])[0]
    for beat in BEATS:
        img = image(beat)[0]
        DIFF[beat] = frozenset(p for p in range(RAM_SIZE // PAGE)
                               if img[p * PAGE:(p + 1) * PAGE] != ref[p * PAGE:(p + 1) * PAGE])
    # The fault group forks a child of its own: run it before the pool's
    # threads exist, so that fork happens in a single-threaded process.
    faults = fault_cases()
    if JOBS > 1 and 'fork' in multiprocessing.get_all_start_methods():
        POOL = multiprocessing.get_context('fork').Pool(JOBS)
    try:
        run_groups(faults)
    finally:
        if POOL is not None:
            close_pool(POOL)
            POOL = None


def close_pool(pool, wait=30.0):
    """Terminate and join the pool, giving up after `wait` seconds. After a
    failed case, Pool.terminate() has been seen to block for good in a
    thread join (its workers already exited), which hid the failure: the
    test never returned. The teardown runs in a daemon thread instead; if it
    has not finished in time the workers are killed and the thread is
    abandoned (the pool's exit finalizer has already run by then, so the
    interpreter does not wait on it again)."""
    import threading

    def down():
        pool.terminate()
        pool.join()
    th = threading.Thread(target=down, daemon=True)
    th.start()
    th.join(wait)
    if th.is_alive():
        for proc in list(getattr(pool, '_pool', None) or []):
            try:
                proc.kill()
            except Exception:  # noqa: BLE001 - already gone
                pass


def run_groups(faults):
    outcomes, cases, unmeasured, packets, entries = set(), 0, 0, 0, 0

    def absorb(results):
        nonlocal cases, unmeasured, packets, entries
        for r in results:
            if r == 'skipped':
                continue
            cases += 1
            if r == 'unmeasured':
                unmeasured += 1
            else:
                outcomes.update(r[0])
                packets += r[2]
                entries += r[3]

    absorb(pmap(vif_capture, BEATS, key=lambda b: b))
    for rows in pmap(gs_capture, BEATS, key=lambda b: b):
        absorb(rows)
    for rows in pmap(hud_capture, BEATS, key=lambda b: b):
        absorb(rows)
    n_capture = cases

    vif_items = RM.select(range(4000), 60, 11)
    gs_items = sorted(set(RM.select(range(8000), 120, 12)) | GS_PIN)
    hud_items = sorted(set(RM.select(range(8000), 140, 13)) | HUD_PIN)
    absorb(pmap(vif_unit, vif_items, key=unit_beat))
    absorb(pmap(gs_unit, gs_items, key=unit_beat))
    absorb(pmap(hud_unit, hud_items, key=unit_beat))
    absorb(pmap(hud_boundary, HUD_BOUNDARY))
    absorb(pmap(hud_round, HUD_ROUND))
    absorb(pmap(hud_box, BOX))
    mixed_items = RM.select(MIXED, 4, 24, axes=(lambda it: it[0],), keep=lambda i, it: it[1:] == (1.1, 0.1))
    absorb(pmap(hud_mixed, mixed_items))
    absorb(pmap(hud_byte, HUD_BYTE))
    edge_all = [(b, at) for b in MAIN for at in GS_EDGE]
    edge_items = RM.select(edge_all, 6, 22, axes=(lambda it: it[0], lambda it: it[1]),
                           keep=lambda i, it: it[0] == MAIN[0])
    before = cases
    absorb(pmap(gs_edge, edge_items, key=first))
    n_edge = cases - before
    big_all = [(b, f) for b in MAIN for f in GS_BIG]
    big_items = RM.select(big_all, 6, 23, axes=(lambda it: it[0], lambda it: it[1]),
                          keep=lambda i, it: it[0] == MAIN[1])
    before = cases
    big_ties = 0
    for row in pmap(gs_big, big_items, key=first):
        absorb([row[0]])
        big_ties += row[1]
    n_big = cases - before
    assert big_ties, '001CD2B0 large n: no case where truncation and rounding of n differ'
    gs_t = [(beat, kind) for beat in MAIN for kind in GS_T]
    before = cases
    absorb(pmap(gs_boundary, gs_t, key=first))
    n_gs_t = cases - before
    before = cases
    bound_all = [(b, x, k) for b in MAIN for x in GS_EXTENTS for k in GS_BOUNDS]
    bound_items = RM.select(bound_all, 24, 14, axes=(lambda it: it[0], lambda it: it[1], lambda it: it[2]))
    absorb(pmap(gs_bounds, bound_items, key=first))
    n_gs_b = cases - before
    a70_all = [(b, i, m) for b in MAIN for i in range(3) for m in VIF_5A70]
    a70_items = RM.select(a70_all, 16, 17, axes=(lambda it: it[0], lambda it: it[2]),
                          keep=lambda i, it: it[0] == MAIN[0])
    a70_results = set()
    before = cases
    for row in pmap(vif_5a70, a70_items, key=first):
        if row != 'skipped':
            absorb([row[0]])
            a70_results.add(row[1])
    n_a70 = cases - before
    assert a70_results == {0, 1, 0xFF}, ('001D5A70 results seen', sorted(a70_results))
    ca0_all = [(b, v, g) for b in MAIN for v in CA0 for g in CA0_TARGETS]
    ca0_items = RM.select(ca0_all, 26, 15, axes=(lambda it: it[0],), keep=lambda i, it: it[0] == MAIN[0])
    before = cases
    absorb(pmap(gs_ca0, ca0_items, key=first))
    n_ca0 = cases - before
    tag_all = [(b,) + shape for b in MAIN for shape in TAG_SHAPES]
    tag_items = RM.select(tag_all, 16, 16, axes=(lambda it: it[0], lambda it: it[1:]),
                          keep=lambda i, it: it[0] == MAIN[0])
    before = cases
    absorb(pmap(tag_alias, tag_items, key=first))
    n_tag = cases - before
    alias_all = [(b,) + shape for b in MAIN for shape in CTX_ALIAS]
    alias_items = RM.select(alias_all, 16, 18, axes=(lambda it: it[0], lambda it: it[1:]),
                            keep=lambda i, it: it[0] == MAIN[0])
    before = cases
    absorb(pmap(ctx_alias, alias_items, key=first))
    n_alias = cases - before
    reread_all = [(b,) + shape for b in MAIN for shape in REREAD]
    reread_items = RM.select(reread_all, 6, 21, axes=(lambda it: it[0], lambda it: it[1:]),
                             keep=lambda i, it: it[0] == MAIN[0])
    before = cases
    absorb(pmap(reread_alias, reread_items, key=first))
    n_reread = cases - before
    reread2_all = [(b,) + shape for b in MAIN for shape in REREAD2]
    reread2_items = RM.select(reread2_all, 10, 25, axes=(lambda it: it[0], lambda it: it[1:]),
                              keep=lambda i, it: (it[0] == MAIN[0] and it[2] != 'count')
                              or (it[0] == MAIN[4] and it[2] == 'count'))
    before = cases
    rows = pmap(reread2, reread2_items, key=first)
    skipped2 = rows.count('skipped')
    absorb(rows)
    n_reread2 = cases - before
    carry_all = [(b,) + site for b in MAIN for site in CARRY]
    carry_items = RM.select(carry_all, 8, 26, axes=(lambda it: it[0], lambda it: it[1:]),
                            keep=lambda i, it: it[0] == MAIN[0])
    before = cases
    absorb(pmap(cursor_carry, carry_items, key=first))
    n_carry = cases - before
    packet_all = [(b, i) for b in MAIN for i in range(3)]
    packet_items = RM.select(packet_all, 8, 19, axes=(lambda it: it[0],), keep=lambda i, it: it == (MAIN[0], 0))
    before = cases
    absorb(pmap(packet_offset, packet_items, key=first))
    n_packet = cases - before
    spad_all = [(b, x) for b in MAIN for x in SPAD_PACKET]
    spad_items = RM.select(spad_all, 6, 20, axes=(lambda it: it[0],), keep=lambda i, it: it[0] == MAIN[0])
    before = cases
    absorb(pmap(spad_packet, spad_items, key=first))
    n_spad = cases - before
    pin3_all = [(b,) + k for b in MAIN for k in PIN3]
    # quick: every shape on a01_00, plus the odd loads on a01_01 (the pairing,
    # whose n is 0 on a01_00) and a01_04 (001F4A10), where bit 0 shows
    pin3_items = RM.select(pin3_all, 0, 27, keep=lambda i, it: it[0] == MAIN[0]
                           or (it[0] == MAIN[1] and it[1] == 'ldq_pair') or (it[0] == MAIN[4] and it[1] == 'ldq_f4a10'))
    before = cases
    absorb(pmap(pin3, pin3_items, key=first))
    n_pin3 = cases - before
    frame_items = [(b, s, c) for b in FRAME_B for s in FRAME_STATE for c in FRAME_CHECK]
    frame_items += [(0xFF, 0xC, 0), (1, 0xFF, 0), (4, 1, 0)] + FRAME_HI
    absorb(pmap(frame_case, frame_items))

    branches = all_branches()
    need = {(pc, t) for pc in branches for t in (True, False)} - IMPOSSIBLE
    missing = sorted((hex(pc), t) for pc, t in need - outcomes)
    assert not missing, ('branch outcomes never seen', missing)
    assert not (outcomes & IMPOSSIBLE), 'an outcome thought impossible occurred'

    RM.banner(f'{n_capture} capture cases over {len(BEATS)} AREA01 beats',
              RM.part(len(vif_items), 4000, 'vif unit cases'),
              RM.part(len(gs_items), 8000, 'gs unit cases'),
              f'{n_gs_t} 001CD2B0 t = 0 boundary cases',
              RM.part(n_gs_b, len(bound_all), '001CD180 bound cases'),
              RM.part(n_a70, len(a70_all), 'direct 001D5A70 cases (results 0, 1, 0xFF)'),
              RM.part(n_ca0, len(ca0_all), 'context +0xA0 cases'),
              RM.part(n_tag, len(tag_all), 'tag-alias cases'),
              RM.part(n_alias, len(alias_all), 'D_00275670-alias cases'),
              RM.part(n_reread, len(reread_all), 'other re-read alias cases'),
              f'{n_reread2} re-read cases of the sweep-2 review ({len(reread2_items)} of {len(reread2_all)} items; '
              f'{skipped2} count items skipped: record 0 builds no chain on that beat)',
              RM.part(n_carry, len(carry_all), '64 KiB cursor-carry cases'),
              RM.part(n_edge, len(edge_all), 'view-edge cases'),
              RM.part(n_big, len(big_all), f'001CD2B0 large-n cases ({big_ties} where n\'s truncation and rounding differ)'),
              f'{len(HUD_ROUND)} 001E8B90 rounding cases, {len(BOX)} 001E8B90 sub-normal / live-word / '
              f'saturation cases',
              RM.part(len(mixed_items), len(MIXED), '001E8B90 mixed-corner cases'),
              f'{len(HUD_BYTE)} 001E9E60 record-byte cases',
              RM.part(n_packet, len(packet_all), 'packet +8 cases'),
              RM.part(n_spad, len(spad_all), 'scratchpad-packet cases'),
              RM.part(n_pin3, len(pin3_all), 'sweep-3 pinned cases (odd quadword loads, packets 4 off, '
                      'rand x, blend t, 001CD180 wraps)'),
              RM.part(len(hud_items), 8000, 'hud unit cases'),
              f'{len(frame_items)} 0015B610 cases',
              f'{cases} cases in all ({unmeasured} unmeasured on both sides; {packets} packet-builder calls; '
              f'RAM + scratchpad + worker log byte-exact at the end)',
              f'{entries} worker entries with RAM + scratchpad + arguments equal to the original\'s at entry',
              f'{len(branches)} conditional branches both ways (2 outcomes impossible by construction)',
              f'{faults} fault cases')
    print('area01 render reference: PASS')


if __name__ == '__main__':
    main()
