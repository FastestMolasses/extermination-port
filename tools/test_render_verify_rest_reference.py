#!/usr/bin/env python3
"""Execute the original instructions of the census rows of lanes L31, L37,
L29b and the L20 missing row, and compare em_render_verify_rest.c (and, for
L29b, em_shadow_original.c). docs/RENDER_VERIFY_REST.md.

The user's pinned ELF and the captured EE RAM (../Extermination/build/
startup-reference and the s87 route beats) supply every instruction, table
and record; none are embedded here. The report holds addresses, counts and
differences only.

Interpreter: the shared EE core (tools/test_player_slide_reference.EE) with
COP1 and VU0 macro arithmetic from tools/ee_float_model.py
(test_player_fall_reference.FallEE), plus the MMI word interleaves, VCLIPW and
the CLIP register that these routines execute. Hooked callees are recorded
and scripted identically for both sides, never simulated as a claim about
the callee; for every routine the hooked set plus the executed set is
exactly its set of call and jump targets.

A. L31 (001C1F50, 001C1DC0 with the thunks 001C1E70/80/90, 001E2260,
   001E2270, 001E0CF0, 001C22A0, 001C2360): area keys (quick: the ten
   special keys, their neighbours and a sample; EM_TEST_FULL=1: all 65,536),
   the render flag scripts of 001E0CF0, synthetic entity records for the
   model binds; every call (order, arguments) and every written byte.
   001E0CF0 and 001C1F50 also run over every capture and route beat with the
   capture's own key and flags (001D2910 executed, its results replayed).
B. L37 (001027E0, 00102850, 001000E0): random and special bit patterns and
   the captured view matrices; every output bit.
C. L20 (001FCF10): the call.
D. L29b. 001D4B50, 001DA1E0 and 001DA290 as units (call logs, record bytes,
   cursor, the in-place payload order). Then the original 001CB590 +
   001DA6A0 over every capture and route beat (quick: four of them) and
   over synthetic node/light perturbations, with 001DA080, 001DA310,
   001D5C80, 001D4B50 and 001DA290 intercepted (executed, their inputs and
   outputs recorded) and compared with em_shadow_original_001DA6A0's plan:
   picks and indices, box inputs/uploads/RGBAQ/clip pass, receiver probe
   order, receiver draw order and classes, the two clip matrices; and the
   001DA290 record with em_rvr_001DA290. Both outcomes of every conditional
   branch of 001DA080 are asserted reached.
E. Fail-stop: NULL workers, failing workers, a latched fault, views too small.
"""
import ctypes as C
import json
import os
import random
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import ee_float_model as M  # noqa: E402
import reference_mode as RM  # noqa: E402
import test_player_fall_reference as fall  # noqa: E402
import test_shadow_original_reference as ts  # noqa: E402
from test_player_slide_reference import read_elf, flt, sx32, s32, RETURN  # noqa: E402

MASK32, MASK64 = 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF
DECOMP = ROOT.parent / 'Extermination'
REF = DECOMP / 'build/startup-reference'
ROUTE = DECOMP / 'build/s87/route'
OUT = ROOT / 'build' / 'render_verify_rest_reference'
CTX_PTR, D810700 = 0x275670, 0x810700
PLAYER = 0x8102B0
STACK_TOP = 0x7F0F0000

SIZES = {  # routine -> size (bytes), from FUNCTIONS.csv / the split listing
    0x1C1DC0: 0xB0, 0x1C1E70: 8, 0x1C1E80: 8, 0x1C1E90: 8, 0x1C1F50: 0x344,
    0x1E2260: 0xC, 0x1E2270: 0x10, 0x1E2280: 0xC, 0x1E0CF0: 0x80, 0x1C22A0: 0xC0, 0x1C2360: 0xC0,
    0x1027E0: 0x6C, 0x102850: 0x20, 0x1000E0: 0x24, 0x1FCF10: 0x14,
    0x1D4B50: 0x2C, 0x1DA1E0: 0xA4, 0x1DA290: 0x74, 0x1DA080: 0x158,
}


# ======================================================================
# Interpreter
# ======================================================================

class RvrEE(fall.FallEE):
    """FallEE plus PEXTLW/PEXTUW, VCLIPW.xyz and CFC2/CTC2 of the CLIP
    register, per-function branch outcome recording, and 64-bit calls."""

    def __init__(self, elf, ram=None, spad=None):
        super().__init__(elf, ram, spad)
        self.clip = 0
        self.cover_ranges = []
        self.outcomes = set()

    def macro(self, word):
        op = word & 63
        if op >= 60 and ((word >> 6 & 31) << 2 | (op & 3)) == 0x1F:           # vclipw.xyz
            fs, ft = word >> 11 & 31, word >> 16 & 31
            w = abs(flt(self.vf[ft][3]))
            x = [flt(v) for v in self.vf[fs][:3]]
            f = (x[0] > w) | (x[0] < -w) << 1 | (x[1] > w) << 2 | (x[1] < -w) << 3 | \
                (x[2] > w) << 4 | (x[2] < -w) << 5
            self.clip = ((self.clip << 6) | f) & 0xFFFFFF
            return
        super().macro(word)

    def cop2(self, word, pc):
        rs, rt, rd = word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        if rs in (2, 6):
            if rd != 18: raise AssertionError(('cfc2/ctc2 register', rd, hex(pc)))
            if rs == 2:
                if rt: self.r[rt] = self.clip
            else:
                self.clip = self.r[rt] & 0xFFFFFF
            return
        super().cop2(word, pc)

    def mmi(self, word, pc):
        rs, rt, rd = word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        fn, sub = word & 63, word >> 6 & 31
        if (fn, sub) in ((0x08, 0x12), (0x28, 0x12)):                          # pextlw / pextuw
            full = lambda n: (self.r[n] & MASK64) | (self.rh[n] << 64)
            a, b = full(rs), full(rt)
            w = lambda v, i: (v >> (32 * i)) & MASK32
            lo = 0 if fn == 0x08 else 2
            value = w(b, lo) | w(a, lo) << 32 | w(b, lo + 1) << 64 | w(a, lo + 1) << 96
            if rd:
                self.r[rd] = value & MASK64
                self.rh[rd] = value >> 64
            return
        super().mmi(word, pc)

    def branch(self, word, pc):
        b = super().branch(word, pc)
        if b is not None and any(s <= pc < e for s, e in self.cover_ranges):
            self.outcomes.add((pc, b[0]))
        return b

    def call64(self, entry, args=(), fregs=()):
        """Call with full 64-bit argument registers and raw float bits."""
        for i, value in enumerate(args): self.r[4 + i] = value & MASK64
        for i, value in enumerate(fregs): self.f[12 + i] = value & MASK32
        self.r[31] = RETURN
        self.run(entry)
        return self.r[2]


def validate_ops(elf):
    """The added ops on synthetic words before any original code runs."""
    e = RvrEE(elf)
    e.r[8], e.rh[8] = 0x2222222211111111, 0x4444444433333333
    e.r[9], e.rh[9] = 0x6666666655555555, 0x8888888877777777
    e.mmi(0x71286488, 0)                 # GPR 12 = low words of GPR 8 / 9 interleaved
    assert (e.r[12], e.rh[12]) == (0x5555555511111111, 0x6666666622222222)
    e.mmi(0x71286CA8, 0)                 # GPR 13 = high words of GPR 8 / 9 interleaved
    assert (e.r[13], e.rh[13]) == (0x7777777733333333, 0x8888888844444444)
    e.vf[1] = [M.f2b(2.0), M.f2b(-3.0), M.f2b(0.5), M.f2b(1.0)]
    e.macro(0x4BC109FF)              # clip test of vf1's x/y/z lanes against its |w|
    assert e.clip == (1 | 8)
    e.cop2(0x48489000, 0)            # read the CLIP register into GPR 8
    assert e.r[8] == (1 | 8)


def call_targets(e, start, size):
    """Every jal/j target of a routine that leaves its own body."""
    out = set()
    for pc in range(start, start + size, 4):
        word = e.load(pc)
        if word >> 26 in (2, 3):
            target = (pc & 0xF0000000) | (word & 0x3FFFFFF) << 2
            if not start <= target < start + size:
                out.add(target)
    return out


def assert_callees(e, routine, hooked, executed=()):
    targets = call_targets(e, routine, SIZES[routine])
    want = set(hooked) | set(executed)
    assert targets == want, (hex(routine), 'call targets', sorted(map(hex, targets)), sorted(map(hex, want)))


class Log:
    """Hook factory: record calls and apply scripted results."""

    def __init__(self, e):
        self.e = e
        self.calls = []
        self.script = {}

    def hook(self, address, name, nargs=0, ret=None, wide=False):
        def h(e):
            args = tuple((e.r[4 + i] & (MASK64 if wide else MASK32)) for i in range(nargs))
            self.calls.append((name,) + args)
            if ret is not None:
                value = ret(args)
                e.r[2] = sx32(value) if value is not None else e.r[2]
        self.e.hooks[address] = h


def watch(e, address, before=None, after=None, counts=None):
    """Execute the routine at `address` (not hooked) and observe it."""
    def h(ee):
        state = before(ee) if before else None
        if counts is not None: counts[address] = counts.get(address, 0) + 1
        del ee.hooks[address]
        ra = ee.r[31]
        ee.r[31] = RETURN
        ee.run(address)
        ee.r[31] = ra
        ee.hooks[address] = h
        if after: after(ee, state)
    e.hooks[address] = h


# ======================================================================
# Native side
# ======================================================================

P, VP, U8, U32, U64, I32 = C.POINTER, C.c_void_p, C.c_uint8, C.c_uint32, C.c_uint64, C.c_int32
I = C.c_int
FN = C.CFUNCTYPE


class Fault(C.Structure):
    _fields_ = [('address', U32), ('code', I32)]


class RenderCtx(C.Structure):
    _fields_ = [('bytes', P(U8)), ('size', U32)]


class Memory(C.Structure):
    _fields_ = [('bytes', P(U8)), ('base', U32), ('size', U32)]


AREA_FIELDS = [('ctx', VP), ('w_001D2830', FN(I, VP, I32, I32)), ('w_001E2260', FN(I, VP, U64)),
               ('w_001E2270', FN(I, VP, U32)), ('w_001E2280', FN(I, VP, U64)), ('w_001D52E0', FN(I, VP)),
               ('w_001D8FD0', FN(I, VP)), ('w_001C1EA0', FN(I, VP, U32))]
BG_FIELDS = [('ctx', VP), ('w_001E0CC0', FN(I, VP)), ('w_001D2910', FN(I, VP, I32, P(I32))),
             ('w_001E1E60', FN(I, VP, U32, I32, P(U32))), ('w_001E1AD0', FN(I, VP, U32, I32, P(U32)))]
MODEL_FIELDS = [('ctx', VP), ('w_001C6120', FN(I, VP, U32, U32, P(U32))),
                ('w_001CA5E0', FN(I, VP, P(U8), U32, U32, I32)), ('w_001C6150', FN(I, VP, U32, P(U32))),
                ('w_001AF780', FN(I, VP, P(U32))), ('w_001CB5B0', FN(I, VP, I32)),
                ('w_001C62C0', FN(I, VP, P(U8), U32))]
MSG_FIELDS = [('ctx', VP), ('w_001FCB90', FN(I, VP, I32, I32, I32, I32))]
CLIP_FIELDS = [('ctx', VP), ('w_001D49D0', FN(I, VP, U32)), ('w_001D4B10', FN(I, VP, U32))]
STATE_FIELDS = [('ctx', VP), ('w_001D1F80', FN(I, VP, I32, I32, I32))]


def struct_of(name, fields):
    return type(name, (C.Structure,), {'_fields_': fields})


AreaW = struct_of('AreaW', AREA_FIELDS)
BgW = struct_of('BgW', BG_FIELDS)
ModelW = struct_of('ModelW', MODEL_FIELDS)
MsgW = struct_of('MsgW', MSG_FIELDS)
ClipW = struct_of('ClipW', CLIP_FIELDS)
StateW = struct_of('StateW', STATE_FIELDS)


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / ('rvr.dylib' if sys.platform == 'darwin' else 'rvr.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-ffp-contract=off',
                    '-shared', '-fPIC', '-Isrc', 'src/game/em_render_verify_rest.c',
                    'src/game/em_sdk_soft_float.c', '-o', str(lib)], cwd=ROOT, check=True)
    n = C.CDLL(str(lib))
    F = P(Fault)
    n.em_rvr_001C1F50.argtypes = [P(U8), P(AreaW), F]
    n.em_rvr_001C1DC0.argtypes = [P(U8), P(AreaW), F]
    n.em_rvr_001C1E70.argtypes = [P(AreaW), F]
    n.em_rvr_001C1E80.argtypes = [P(AreaW), F]
    n.em_rvr_001C1E90.argtypes = [F]
    n.em_rvr_001E2260.argtypes = [P(RenderCtx), U64, F]
    n.em_rvr_001E2270.argtypes = [P(RenderCtx), P(U8), F]
    n.em_rvr_001E2280.argtypes = [P(RenderCtx), U64, F]
    n.em_rvr_001E0CF0.argtypes = [P(RenderCtx), P(BgW), F]
    for name in ('em_rvr_001C22A0', 'em_rvr_001C2360'):
        getattr(n, name).argtypes = [P(U8), U32, U32, U32, C.c_int16, P(ModelW), P(I32), F]
    n.em_rvr_001027E0.argtypes = [P(U32), P(U32), F]
    n.em_rvr_00102850.argtypes = [P(U32), P(U32), U32, F]
    n.em_rvr_001000E0.argtypes = [U64, U64]
    n.em_rvr_001000E0.restype = I32
    n.em_rvr_001FCF10.argtypes = [P(MsgW), F]
    n.em_rvr_001D4B50.argtypes = [P(ClipW), U32, F]
    n.em_rvr_001DA1E0.argtypes = [P(RenderCtx), P(Memory), I32, P(U8), U32, P(U32), F]
    n.em_rvr_001DA290.argtypes = [P(RenderCtx), P(Memory), P(StateW), P(U8), I32, U32, F]
    return n


def u8buf(data):
    return (U8 * len(data)).from_buffer_copy(bytes(data))


def u32(buf, at): return struct.unpack_from('<I', buf, at)[0]


# ======================================================================
# Stats
# ======================================================================

STATS = {}


def count(key, n=1):
    STATS[key] = STATS.get(key, 0) + n


def same(label, native, original):
    if native != original:
        raise AssertionError((label, 'native', native if not isinstance(native, (bytes, bytearray)) else native.hex(),
                              'original', original if not isinstance(original, (bytes, bytearray))
                              else original.hex()))


# ======================================================================
# A. L31
# ======================================================================

SPECIAL_KEYS = (0x0B00, 0x0C00, 0x0D00, 0x0E00, 0x0F00, 0x0F01, 0x1100, 0x1200, 0x1500)


class Region:
    """Snapshot and restore of byte ranges of an interpreter's memory."""

    def __init__(self, e, spans):
        self.e = e
        self.saved = [(a, e.read(a, n)) for a, n in spans]

    def restore(self):
        for a, data in self.saved:
            self.e.write(a, data)


def area_native(n, elf_colour, rc):
    """AreaW whose 001E2260/001E2270 run the translations over `rc`."""
    log = []
    fault = Fault()

    def f2260(_, tag):
        log.append(('2260', tag))
        return n.em_rvr_001E2260(C.byref(rc), tag, C.byref(fault))

    def f2270(_, address):
        log.append(('2270', address))
        assert address == 0x250F30
        return n.em_rvr_001E2270(C.byref(rc), u8buf(elf_colour), C.byref(fault))
    def f2280(_, tag):
        log.append(('2280', tag))
        return n.em_rvr_001E2280(C.byref(rc), tag, C.byref(fault))
    cbs = [FN(I, VP, I32, I32)(lambda _, a, b: log.append(('001D2830', a & MASK32, b & MASK32)) or 0),
           FN(I, VP, U64)(f2260), FN(I, VP, U32)(f2270),
           FN(I, VP, U64)(f2280),
           FN(I, VP)(lambda _: log.append(('001D52E0',)) or 0),
           FN(I, VP)(lambda _: log.append(('001D8FD0',)) or 0),
           FN(I, VP, U32)(lambda _, b: log.append(('001C1EA0', b)) or 0)]
    w = AreaW(None, *cbs)
    w._keep = cbs
    return w, log, fault


def area_original(e, routine, key, ctx):
    """Execute 001C1F50 or 001C1DC0 with its non-translated callees hooked;
    returns (hooked call log incl. the executed 001E2260/2270 entries,
    ctx bytes 0x1C0..0x1E0 after, entry counts)."""
    region = Region(e, [(D810700, 2), (ctx + 0x1C0, 0x30)])
    e.save(D810700, key >> 8, 1)
    e.save(D810700 + 1, key & 0xFF, 1)
    log = Log(e)
    log.hook(0x1D2830, '001D2830', 2)
    log.hook(0x1D52E0, '001D52E0')
    log.hook(0x1D8FD0, '001D8FD0')
    log.hook(0x1C1EA0, '001C1EA0', 1)
    counts = {}
    watch(e, 0x1E2260, before=lambda ee: log.calls.append(('2260', ee.r[4] & MASK64)), counts=counts)
    watch(e, 0x1E2270, before=lambda ee: log.calls.append(('2270', ee.r[4] & MASK32)), counts=counts)
    watch(e, 0x1E2280, before=lambda ee: log.calls.append(('2280', ee.r[4] & MASK64)), counts=counts)
    for a in (0x1C1E70, 0x1C1E80, 0x1C1E90, 0x1C1F50):
        if a != routine: watch(e, a, counts=counts)
    e.r[29] = STACK_TOP
    e.call64(routine, (0x8101D0,))
    after = e.read(ctx + 0x1C0, 0x30)
    for a in (0x1D2830, 0x1E2280, 0x1D52E0, 0x1D8FD0, 0x1C1EA0, 0x1E2260, 0x1E2270, 0x1C1E70, 0x1C1E80,
              0x1C1E90, 0x1C1F50):
        e.hooks.pop(a, None)
    region.restore()
    return log.calls, after, counts


def area_case(e, n, routine, key, ctx, colour):
    calls, after, counts = area_original(e, routine, key, ctx)
    before = bytearray(e.read(ctx, 0x200))
    rc_bytes = u8buf(before)
    rc = RenderCtx(rc_bytes, len(before))
    w, log, fault = area_native(n, colour, rc)
    kb = u8buf(bytes([key >> 8, key & 0xFF]))
    fn = n.em_rvr_001C1F50 if routine == 0x1C1F50 else n.em_rvr_001C1DC0
    assert fn(kb, C.byref(w), C.byref(fault)) == 0 and fault.code == 0, (hex(key), fault.code)
    same(f'{routine:08X} calls key {key:#06x}', log, calls)
    same(f'{routine:08X} ctx+0x1C0 key {key:#06x}', bytes(rc_bytes)[0x1C0:0x1F0], after)
    if routine == 0x1C1DC0:
        assert all(counts.get(a) == 1 for a in (0x1C1E70, 0x1C1E80, 0x1C1E90, 0x1C1F50)), counts
    count(f'A {routine:08X} keys')


def section_a_area(e, n, ctx, colour):
    assert_callees(e, 0x1C1F50, (0x1D2830,), (0x1E2260, 0x1E2270, 0x1E2280))
    assert_callees(e, 0x1C1DC0, (0x1D2830, 0x1C1EA0), (0x1C1E70, 0x1C1E80, 0x1C1E90, 0x1C1F50))
    assert_callees(e, 0x1C1E70, (0x1D52E0,))
    assert_callees(e, 0x1C1E80, (0x1D8FD0,))
    assert_callees(e, 0x1C1E90, ())
    assert_callees(e, 0x1E2260, ())
    assert_callees(e, 0x1E2270, ())
    assert_callees(e, 0x1E2280, ())
    keys = list(range(0x10000))
    near = {k + d for k in SPECIAL_KEYS for d in (-0x100, -1, 0, 1, 0x100)} | {0, 0xFFFF, 0x0B01, 0x1501}
    quick = RM.select(keys, 300, 11, keep=lambda i, k: k in near)
    for key in quick:
        area_case(e, n, 0x1C1F50, key, ctx, colour)
    for key in RM.select(keys, 40, 12, keep=lambda i, k: k in SPECIAL_KEYS or k == 0):
        area_case(e, n, 0x1C1DC0, key, ctx, colour)
    STATS['A keys total'] = len(keys)


def section_a_stores(e, n, ctx, rng):
    """001E2260 / 001E2270 with random values; nothing but their bytes may change."""
    for i in range(RM.pick(2000, 200)):
        tag = rng.getrandbits(64)
        src = bytes(rng.getrandbits(8) for _ in range(16))
        region = Region(e, [(ctx, 0x200), (0x6F0000, 16)])
        before = e.read(ctx, 0x200)
        e.write(0x6F0000, src)
        e.r[29] = STACK_TOP
        e.call64(0x1E2260, (tag,))
        e.call64(0x1E2270, (0x6F0000,))
        e.call64(0x1E2280, (tag ^ MASK64,))
        after = e.read(ctx, 0x200)
        region.restore()
        buf = u8buf(before)
        rc = RenderCtx(buf, 0x200)
        fault = Fault()
        assert n.em_rvr_001E2260(C.byref(rc), tag, C.byref(fault)) == 0
        assert n.em_rvr_001E2270(C.byref(rc), u8buf(src), C.byref(fault)) == 0
        assert n.em_rvr_001E2280(C.byref(rc), tag ^ MASK64, C.byref(fault)) == 0
        same('001E2260/001E2270/001E2280 ctx', bytes(buf), after)
        count('A 001E2260/2270/2280 cases')


def bg_native(n, results):
    """BgW with scripted flag/list results (consumed in call order)."""
    log = []
    flags = list(results['flags'])
    lists = list(results['lists'])

    def flag(_, f, out):
        log.append(('001D2910', f & MASK32))
        out[0] = flags.pop(0)
        return 0

    def build(name):
        def fn(_, off, ch, out):
            log.append((name, off, ch & MASK32))
            out[0] = lists.pop(0)
            return 0
        return fn
    cbs = [FN(I, VP)(lambda _: log.append(('001E0CC0',)) or 0), FN(I, VP, I32, P(I32))(flag),
           FN(I, VP, U32, I32, P(U32))(build('001E1E60')), FN(I, VP, U32, I32, P(U32))(build('001E1AD0'))]
    w = BgW(None, *cbs)
    w._keep = cbs
    return w, log


def bg_original(e, ctx, flag_script=None, lists=()):
    """001E0CF0 with 001E0CC0 / 001E1E60 / 001E1AD0 hooked. flag_script:
    list of scripted 001D2910 results, or None to execute 001D2910 (its
    results recorded)."""
    region = Region(e, [(ctx + 0x1D8, 4), (ctx + 0x1E8, 4)])
    log = Log(e)
    log.hook(0x1E0CC0, '001E0CC0')
    pending = list(lists)
    for a, name in ((0x1E1E60, '001E1E60'), (0x1E1AD0, '001E1AD0')):
        def h(ee, name=name):
            log.calls.append((name, ((ee.r[4] & MASK32) - ctx) & MASK32, ee.r[5] & MASK32))
            ee.r[2] = sx32(pending.pop(0))
        e.hooks[a] = h
    seen = []
    if flag_script is not None:
        script = list(flag_script)
        def h(ee):
            log.calls.append(('001D2910', ee.r[4] & MASK32))
            ee.r[2] = sx32(script.pop(0))
        e.hooks[0x1D2910] = h
    else:
        watch(e, 0x1D2910, before=lambda ee: log.calls.append(('001D2910', ee.r[4] & MASK32)),
              after=lambda ee, _: seen.append(ee.r[2] & MASK32))
    e.r[29] = STACK_TOP
    e.call64(0x1E0CF0)
    out = (e.read(ctx + 0x1D8, 4), e.read(ctx + 0x1E8, 4))
    for a in (0x1E0CC0, 0x1E1E60, 0x1E1AD0, 0x1D2910):
        e.hooks.pop(a, None)
    region.restore()
    return log.calls, out, seen


def bg_case(e, n, ctx, flags, lists):
    calls, out, seen = bg_original(e, ctx, flags, lists)
    used_flags = flags if flags is not None else seen
    before = bytearray(e.read(ctx, 0x200))
    buf = u8buf(before)
    rc = RenderCtx(buf, 0x200)
    w, log = bg_native(n, {'flags': [s32(v) for v in used_flags], 'lists': list(lists)})
    fault = Fault()
    assert n.em_rvr_001E0CF0(C.byref(rc), C.byref(w), C.byref(fault)) == 0 and fault.code == 0
    same('001E0CF0 calls', log, calls)
    same('001E0CF0 ctx+0x1D8', bytes(buf)[0x1D8:0x1DC], out[0])
    same('001E0CF0 ctx+0x1E8', bytes(buf)[0x1E8:0x1EC], out[1])
    return used_flags


def section_a_background(e, n, ctx, rng):
    assert_callees(e, 0x1E0CF0, (0x1E0CC0, 0x1D2910, 0x1E1E60, 0x1E1AD0))
    for bits_ in range(8):
        for _ in range(RM.pick(40, 4)):
            f20, f21, f22 = bits_ & 1, bits_ >> 1 & 1, bits_ >> 2 & 1
            vals = [f20 * rng.choice((1, 2, 0x80, -1)), f21 * rng.choice((1, 7, -5)),
                    f22 * rng.choice((1, 0x100))]
            script = [vals[0]] + ([vals[1], vals[2]] if f20 else [])
            lists = [rng.getrandbits(32), rng.getrandbits(32)]
            bg_case(e, n, ctx, script, lists)
            count('A 001E0CF0 flag scripts')


def section_a_model(e, n, rng):
    assert_callees(e, 0x1C22A0, (0x1C6120, 0x1CA5E0, 0x1C6150, 0x1AF780, 0x1CB5B0, 0x1C62C0))
    assert_callees(e, 0x1C2360, (0x1C6120, 0x1CA5E0, 0x1C6150, 0x1AF780, 0x1CB5B0, 0x1C62C0))
    SELF, SIZE = 0x6E0000, 0x520
    cases = []
    for routine in (0x1C22A0, 0x1C2360):
        for cap in (-1, 0, 1, 5, 0x20, 0x7F, 0xFF, 0x100, 0x7FFF, -0x8000):
            for cnt in (0, 1, 3, 0x21, 0x80, 0xFF, 0x100, 0x1FF, 0xFFFFFF05):
                cases.append((routine, cap, cnt))
    picked = RM.select(cases, 60, 21, axes=(lambda c: c[0], lambda c: c[1], lambda c: c[2]))
    for routine, cap, cnt in picked:
        table = 0x28A59C if routine == 0x1C22A0 else 0x28A56C
        record = bytes(rng.getrandbits(8) for _ in range(SIZE))
        model = rng.getrandbits(32)
        bones = [rng.getrandbits(32) for _ in range(256)]
        region = Region(e, [(SELF, SIZE), (0x275BCC, 2)])
        e.write(SELF, record)
        e.save(0x275BCC, cap & 0xFFFF, 2)
        table_word = e.load(table)
        log = Log(e)
        queue = list(bones)
        log.hook(0x1C6120, '001C6120', 2, ret=lambda a: model)
        log.hook(0x1CA5E0, '001CA5E0', 3)
        log.hook(0x1C6150, '001C6150', 1, ret=lambda a: cnt)
        log.hook(0x1AF780, '001AF780', 0, ret=lambda a: queue.pop(0))
        log.hook(0x1CB5B0, '001CB5B0', 1)
        log.hook(0x1C62C0, '001C62C0', 1)
        e.r[29] = STACK_TOP
        v0 = e.call64(routine, (SELF,)) & MASK32
        after = e.read(SELF, SIZE)
        for a in (0x1C6120, 0x1CA5E0, 0x1C6150, 0x1AF780, 0x1CB5B0, 0x1C62C0):
            e.hooks.pop(a, None)
        region.restore()
        # native
        buf = u8buf(record)
        nlog = []
        nq = list(bones)
        cbs = [FN(I, VP, U32, U32, P(U32))(lambda _, b, c, out: (nlog.append(('001C6120', b, c)),
                                                                  out.__setitem__(0, model))[-1] or 0),
               FN(I, VP, P(U8), U32, U32, I32)(lambda _, s, sa, m, mode: nlog.append(
                   ('001CA5E0', sa, m, mode & MASK32)) or 0),
               FN(I, VP, U32, P(U32))(lambda _, v, out: (nlog.append(('001C6150', v)),
                                                         out.__setitem__(0, cnt & MASK32))[-1] or 0),
               FN(I, VP, P(U32))(lambda _, out: (nlog.append(('001AF780',)),
                                                 out.__setitem__(0, nq.pop(0)))[-1] or 0),
               FN(I, VP, I32)(lambda _, c: nlog.append(('001CB5B0', c & MASK32)) or 0),
               FN(I, VP, P(U8), U32)(lambda _, s, sa: nlog.append(('001C62C0', sa)) or 0)]
        w = ModelW(None, *cbs)
        res, fault = I32(), Fault()
        fn = n.em_rvr_001C22A0 if routine == 0x1C22A0 else n.em_rvr_001C2360
        assert fn(buf, SIZE, SELF, table_word, cap, C.byref(w), C.byref(res), C.byref(fault)) == 0, fault.code
        same(f'{routine:08X} calls', nlog, log.calls)
        same(f'{routine:08X} record', bytes(buf), after)
        same(f'{routine:08X} v0', res.value & MASK32, v0)
        count('A 001C22A0/001C2360 cases')
    STATS['A model cases total'] = len(cases)


def route_items():
    """(label, ee path, scratchpad path or None) for every capture and beat."""
    items = [(name, REF / ee, (REF / sp) if sp else None) for name, ee, sp, _ in ts.CAPTURES]
    for beat in sorted(p.name for p in ROUTE.iterdir() if (p / 'eeMemory.bin').exists()):
        sp = ROUTE / beat / 'scratchpad.bin'
        items.append((beat, ROUTE / beat / 'eeMemory.bin', sp if sp.exists() else None))
    return items


# ======================================================================
# B. L37
# ======================================================================

SPECIAL_BITS = (0, 0x80000000, 0x3F800000, 0xBF800000, 0x00000001, 0x80400000, 0x7F7FFFFF, 0xFF7FFFFF,
                0x7F800000, 0xFF800000, 0x7FC00000, 0x7F800001, 0x00800000, 0x4B000000, 0x3F7FFFFF)


def rand_float(rng, special=0.15):
    if rng.random() < special:
        return rng.choice(SPECIAL_BITS)
    return M.f2b(rng.uniform(-1, 1) * 10 ** rng.uniform(-6, 6)) & MASK32


def section_b(e, n, rng, captured):
    assert_callees(e, 0x1027E0, ())
    assert_callees(e, 0x102850, ())
    assert_callees(e, 0x1000E0, (0x1274B0,))
    A, B = 0x6D0000, 0x6D0100
    mats = [list(m) for m in captured]
    for i in range(RM.pick(3000, 300)):
        mats.append([rand_float(rng, 0.05 if i % 3 else 0.4) for _ in range(16)])
    for i, m in enumerate(mats):
        alias = i % 5 == 0
        region = Region(e, [(A, 64), (B, 64)])
        e.write(A, struct.pack('<16I', *m))
        e.r[29] = STACK_TOP
        e.call64(0x1027E0, (A if alias else B, A))
        out = struct.unpack('<16I', e.read(A if alias else B, 64))
        region.restore()
        nin = (U32 * 16)(*m)
        nout = nin if alias else (U32 * 16)()
        fault = Fault()
        rv = n.em_rvr_001027E0(nout, nin, C.byref(fault))
        assert rv == 0 and fault.code == 0, (fault.address, fault.code)
        same('001027E0', list(nout), list(out))
        count('B 001027E0 matrices')
    for i in range(RM.pick(6000, 600)):
        v = [rand_float(rng, 0.2) for _ in range(4)]
        s = M.f2b(12.0) if i % 7 == 0 else rand_float(rng, 0.3)   # 00209280 divides by 12.0 in place
        alias = i % 2 == 0
        region = Region(e, [(A, 16), (B, 16)])
        e.write(A, struct.pack('<4I', *v))
        e.r[29] = STACK_TOP
        e.call64(0x102850, (A if alias else B, A), (s,))
        out = struct.unpack('<4I', e.read(A if alias else B, 16))
        region.restore()
        nin = (U32 * 4)(*v)
        nout, fault = nin if alias else (U32 * 4)(), Fault()
        assert n.em_rvr_00102850(nout, nin, s, C.byref(fault)) == 0
        same('00102850', list(nout), list(out))
        count('B 00102850 cases')
    specials = [0, 1 << 63, 0x3FF0000000000000, 0xBFF0000000000000, 0x7FF0000000000000, 0xFFF0000000000000,
                0x7FF8000000000000, 0x7FF0000000000001, 0x0000000000000001, 0x8000000000000001]
    doubles = specials + [struct.unpack('<Q', struct.pack('<d', flt(rand_float(rng, 0.2))))[0]
                          for _ in range(RM.pick(3000, 300))] + [rng.getrandbits(64) for _ in range(RM.pick(1000, 100))]
    for i, a in enumerate(doubles):
        b = 0 if i % 2 else rng.choice(doubles)
        e.r[29] = STACK_TOP
        v0 = e.call64(0x1000E0, (a, b)) & MASK32
        same(f'001000E0({a:#x}, {b:#x})', n.em_rvr_001000E0(a, b) & MASK32, v0)
        count('B 001000E0 cases')
    # Caller check (report only): 0017C580 passes the whole 00128350 double
    # (a 128-bit register copy); a 32-bit (sign-extended low word) argument
    # changes the answer for ordinary values.
    one = 0x3FF0000000000000
    e.r[29] = STACK_TOP
    full = e.call64(0x1000E0, (one, 0)) & MASK32
    e.r[29] = STACK_TOP
    truncated = e.call64(0x1000E0, (sx32(one & MASK32), 0)) & MASK32
    STATS['B 001000E0(1.0 as double, 0)'] = full
    STATS['B 001000E0(low word of 1.0 only, 0)'] = truncated


# ======================================================================
# C. L20
# ======================================================================

def section_c(e, n):
    assert_callees(e, 0x1FCF10, (0x1FCB90,))
    log = Log(e)
    log.hook(0x1FCB90, '001FCB90', 4)
    e.r[29] = STACK_TOP
    e.call64(0x1FCF10)
    e.hooks.pop(0x1FCB90)
    nlog = []
    cb = FN(I, VP, I32, I32, I32, I32)(lambda _, a, b, c, d: nlog.append(
        ('001FCB90', a & MASK32, b & MASK32, c & MASK32, d & MASK32)) or 0)
    w, fault = MsgW(None, cb), Fault()
    assert n.em_rvr_001FCF10(C.byref(w), C.byref(fault)) == 0
    same('001FCF10', nlog, log.calls)
    count('C 001FCF10 cases')


# ======================================================================
# D. L29b
# ======================================================================

def clip_native(n, obj):
    nlog = []
    cbs = [FN(I, VP, U32)(lambda _, o: nlog.append(('001D49D0', o)) or 0),
           FN(I, VP, U32)(lambda _, o: nlog.append(('001D4B10', o)) or 0)]
    w, fault = ClipW(None, *cbs), Fault()
    assert n.em_rvr_001D4B50(C.byref(w), obj, C.byref(fault)) == 0
    return nlog


def record_native(n, ctx_bytes, window_base, window, channel, payload_addr, payload, word,
                  a290=None, template=None):
    """em_rvr_001DA1E0 (or 001DA290) over copies; returns (window, ctx, body, log)."""
    rcb = u8buf(ctx_bytes)
    rc = RenderCtx(rcb, len(ctx_bytes))
    mb = u8buf(window)
    mem = Memory(mb, window_base, len(window))
    fault, body = Fault(), U32()
    if a290 is None:
        # the payload may lie in the window: read it through the window view
        if window_base <= payload_addr < window_base + len(window):
            pp = C.cast(C.byref(mb, payload_addr - window_base), P(U8))
        else:
            pp = u8buf(payload)
        assert n.em_rvr_001DA1E0(C.byref(rc), C.byref(mem), channel, pp, word, C.byref(body),
                                 C.byref(fault)) == 0, fault.code
        return bytes(mb), bytes(rcb), body.value, []
    nlog = []
    cb = FN(I, VP, I32, I32, I32)(lambda _, a, b, c: nlog.append(('001D1F80', a & MASK32, b & MASK32,
                                                                  c & MASK32)) or 0)
    w = StateW(None, cb)
    assert n.em_rvr_001DA290(C.byref(rc), C.byref(mem), C.byref(w), u8buf(template), a290[0], a290[1],
                             C.byref(fault)) == 0, fault.code
    return bytes(mb), bytes(rcb), None, nlog


def section_d_units(e, n, ctx, rng, template):
    assert_callees(e, 0x1D4B50, (0x1D49D0, 0x1D4B10))
    assert_callees(e, 0x1DA1E0, ())
    assert_callees(e, 0x1DA290, (0x1D1F80,), (0x1DA1E0,))
    for i in range(RM.pick(400, 40)):
        obj = rng.getrandbits(32)
        log = Log(e)
        log.hook(0x1D49D0, '001D49D0', 1)
        log.hook(0x1D4B10, '001D4B10', 1)
        e.r[29] = STACK_TOP
        e.call64(0x1D4B50, (obj,))
        e.hooks.pop(0x1D49D0); e.hooks.pop(0x1D4B10)
        same('001D4B50', clip_native(n, obj), log.calls)
        count('D 001D4B50 cases')
    WIN = 0x6C0000
    for i in range(RM.pick(1500, 150)):
        use290 = i % 3 == 0
        channel = 0 if use290 and i % 2 else rng.randrange(0, 4)
        rec = WIN + 0x100 + 0x10 * rng.randrange(0, 16)
        garbage = bytes(rng.getrandbits(8) for _ in range(0x400))
        payload_addr = WIN + 0x300
        mode = rng.randrange(4)
        if mode == 1: payload_addr = rec + 0x40     # the payload is the record's own tail
        if mode == 2: payload_addr = rec + 0x10     # the payload overlaps the header stores
        word = rng.getrandbits(32)
        region = Region(e, [(WIN, 0x400), (ctx, 0x40)])
        e.write(WIN, garbage)
        for c in range(4):
            e.save(ctx + 0x10 + 4 * c, rec if c == channel else rng.getrandbits(32))
        ctx_before, win_before = e.read(ctx, 0x40), e.read(WIN, 0x400)
        payload = e.read(payload_addr, 64)
        log = Log(e)
        e.r[29] = STACK_TOP
        if use290:
            log.hook(0x1D1F80, '001D1F80', 3)
            e.call64(0x1DA290, (channel, word))
            e.hooks.pop(0x1D1F80)
            v0 = None
        else:
            v0 = e.call64(0x1DA1E0, (channel, payload_addr, word)) & MASK32
        win_after, ctx_after = e.read(WIN, 0x400), e.read(ctx, 0x40)
        region.restore()
        mb, rcb, body, nlog = record_native(n, ctx_before, WIN, win_before, channel, payload_addr, payload,
                                            word, (channel, word) if use290 else None, template)
        same('001DA1E0/001DA290 window', mb, win_after)
        same('001DA1E0/001DA290 ctx cursors', rcb, ctx_after)
        if use290:
            same('001DA290 calls', nlog, log.calls)
            count('D 001DA290 cases')
        else:
            same('001DA1E0 v0', body, v0)
            count('D 001DA1E0 cases')
            if mode in (1, 2): count('D 001DA1E0 aliased payload cases')


# ---- D4: the shadow chain intercepted ---------------------------------------

def box_library(ram):
    lib = u32(ram, 0x28A56C)
    return [(lib + ((s32(u32(ram, lib + 4 * m + 4)) >> 2) << 2)) & MASK32 for m in (0x14, 0x15)]


def shadow_original(elf, ram, scratch, ff0=None):
    """001CB590 + 001DA6A0 with the lane's routines intercepted."""
    e = RvrEE(elf, ram, scratch)
    ctx = u32(ram, CTX_PTR)
    if scratch is None:                      # as test_shadow_original_reference: the
        e.write(0x70003AC0, ram[ctx + 0x23C0:ctx + 0x2400])   # scratch VP is the frame camera
    if ff0 is not None:
        e.write(0x817FF0, struct.pack('<4f', *ff0))
    e.cover_ranges = [(0x1DA080, 0x1DA080 + SIZES[0x1DA080])]
    rec = {'1DA080': [], '1DA310': [], '1D5C80': [], '1D4B50': [], '1DA290': [], '1DA1E0': []}
    active = []

    def enter(tag, fn):
        def before(ee):
            active.append(tag)
            return fn(ee) if fn else None
        return before

    def leave(fn):
        def after(ee, state):
            active.pop()
            if fn: fn(ee, state)
        return after

    # 001DA080
    watch(e, 0x1DA080, enter('080', lambda ee: {'args': [ee.r[4 + i] & MASK32 for i in range(4)],
                                               'fc0': ee.read(0x817FC0, 16),
                                               'a3': ee.read(ee.r[7] & MASK32, 16)}),
          leave(lambda ee, st: rec['1DA080'].append(dict(st, out_a0=ee.read(st['args'][0], 16),
                                                         out_a1=ee.read(st['args'][1], 16)))))

    # 001DA310
    def b310(ee):
        st = {'args': [ee.r[4 + i] & MASK32 for i in range(4)], 'f12': ee.f[12] & MASK32,
              'cursor': ee.load((u32(ram, CTX_PTR)) + 0x10), 'vp': ee.read(0x70003AC0, 64),
              'color': ee.read(ee.r[6] & MASK32, 16), 'rgbaq': [], 'clip_vp': []}
        st['anchor'] = ee.read(st['args'][0], 16)
        return st
    boxes = []

    def a310(ee, st):
        q1 = st['cursor']
        st['record1'] = ee.read(q1, 0x60)
        st['record2'] = ee.read(q1 + 0x60, 0xA0)
        st['vp_after'] = ee.read(0x70003AC0, 64)
        rec['1DA310'].append(st)
    watch(e, 0x1DA310, enter('310', lambda ee: boxes.append(b310(ee)) or boxes[-1]), leave(a310))

    def in_box(ee):
        return active and active[-1] == '310'
    # 001D7080 and 001D4FB0 inside 001DA310 / 001D5C80
    watch(e, 0x1D7080, before=lambda ee: boxes[-1]['rgbaq'].append(ee.r[5] & MASK32) if in_box(ee) else None)
    receivers = []
    probes = []

    def fb0(ee):
        obj = ee.r[4] & MASK32
        if in_box(ee):
            boxes[-1]['clip_vp'].append((obj, ee.read(0x70003AC0, 64)))
        elif active and active[-1] == 'C80':
            receivers.append([obj, 0])
    watch(e, 0x1D4FB0, before=fb0)

    def b4b50(ee):
        obj = ee.r[4] & MASK32
        if 'C80' in active and receivers and receivers[-1][0] == obj:
            receivers[-1][1] = 2
        return {'obj': obj}
    watch(e, 0x1D4B50, enter('B50', b4b50), leave(lambda ee, st: rec['1D4B50'].append(st)))
    pending_b50 = []
    for a, name in ((0x1D49D0, '001D49D0'), (0x1D4B10, '001D4B10')):
        watch(e, a, before=lambda ee, name=name: pending_b50.append((name, ee.r[4] & MASK32))
              if active and active[-1] == 'B50' else None)

    watch(e, 0x1C6120, before=lambda ee: probes.append(ee.r[5] & MASK32)
          if active and active[-1] == 'C80' else None)
    watch(e, 0x1D5C80, enter('C80', lambda ee: {'a0': ee.r[4] & MASK32}),
          leave(lambda ee, st: rec['1D5C80'].append(dict(st, s3400=ee.read(0x70003400, 64),
                                                         s3440=ee.read(0x70003440, 64)))))

    calls_1f80 = []
    watch(e, 0x1D1F80, before=lambda ee: calls_1f80.append(
        ('001D1F80', ee.r[4] & MASK32, ee.r[5] & MASK32, ee.r[6] & MASK32)) if active and active[-1] == '290' else None)
    watch(e, 0x1DA290, enter('290', lambda ee: {'a0': ee.r[4] & MASK32, 'a1': ee.r[5] & MASK32}),
          leave(lambda ee, st: rec['1DA290'].append(dict(st, calls=list(calls_1f80)))))

    def b1e0(ee):
        c = u32(ram, CTX_PTR)
        cursor = ee.load(c + 0x10 + 4 * s32(ee.r[4]))
        return {'a0': ee.r[4] & MASK32, 'payload': ee.read(ee.r[5] & MASK32, 64), 'a2': ee.r[6] & MASK32,
                'ctx': ee.read(c, 0x40), 'cursor': cursor, 'window': ee.read(cursor, 0x80)}

    def a1e0(ee, st):
        c = u32(ram, CTX_PTR)
        rec['1DA1E0'].append(dict(st, ctx_after=ee.read(c, 0x40), window_after=ee.read(st['cursor'], 0x80),
                                  v0=ee.r[2] & MASK32))
    watch(e, 0x1DA1E0, b1e0, a1e0)

    e.r[29] = STACK_TOP
    e.call64(0x1CB590, (PLAYER, 0x320, ram[PLAYER + 9]))
    e.r[29] = STACK_TOP
    e.call64(0x1DA6A0, (PLAYER,))
    rec['receivers'] = receivers
    rec['probes'] = probes
    rec['b50_calls'] = pending_b50
    rec['outcomes'] = e.outcomes
    return rec


def shadow_compare(n, lib, label, ram, scratch, template, ff0=None):
    """Compare the intercepted original routines with em_shadow_original's plan."""
    rec = shadow_original(ELF[0], ram, scratch, ff0)
    scene = ts.scene_view(ram, scratch)
    nat = ts.NativeRun(lib, ram, scene, ff0=ff0)
    p = nat.plan
    assert nat.fault.code == 0, (label, nat.fault.address, nat.fault.code)
    drawn = bool(rec['1DA290'])
    same(f'{label} drawn', p.drawn, int(drawn))
    if not drawn:
        assert not rec['1DA080'] and not rec['1DA310'] and not rec['1D5C80'], label
        return {'label': label, 'drawn': 0}, rec['outcomes']
    fb = lambda vals: struct.pack(f'<{len(vals)}f', *vals)
    # 001DA080
    assert len(rec['1DA080']) == 1, label
    r = rec['1DA080'][0]
    same(f'{label} 001DA080 args', r['args'][:3], [0x817FB0, 0x817FA0, PLAYER])
    same(f'{label} 001DA080 D_00817FC0 at entry', fb(p.cross_817FC0), r['fc0'])
    same(f'{label} 001DA080 *a3 at entry', fb(nat.state.d817FF0), r['a3'])
    same(f'{label} 001DA080 D_00817FB0', fb(p.near_817FB0), r['out_a0'])
    same(f'{label} 001DA080 D_00817FA0', fb(p.far_817FA0), r['out_a1'])
    for idx, out in ((p.near_index, r['out_a0']), (p.far_index, r['out_a1'])):
        node = u32(ram, PLAYER + 0x110 + 4 * idx)
        same(f'{label} 001DA080 index {idx}', ram[node + 0xC0:node + 0xD0], out)
    count('D shadow 001DA080 calls')
    # 001DA290 and the record its 001DA1E0 builds
    assert len(rec['1DA290']) == 1 and len(rec['1DA1E0']) == 1, label
    r290, r = rec['1DA290'][0], rec['1DA1E0'][0]
    same(f'{label} 001DA290 calls', [('001D1F80', r290['a0'], 2, 9)], r290['calls'])
    same(f'{label} 001DA1E0 arguments', (r290['a0'], TEMPLATE[0], r290['a1']), (r['a0'], r['payload'], r['a2']))
    mb, rcb, body, _ = record_native(n, r['ctx'], r['cursor'], r['window'], s32(r['a0']), 0, r['payload'], r['a2'])
    same(f'{label} 001DA1E0 record', mb, r['window_after'])
    same(f'{label} 001DA1E0 cursors', rcb, r['ctx_after'])
    same(f'{label} 001DA1E0 v0', body, r['v0'])
    count('D shadow 001DA290/001DA1E0 records')
    # 001DA310 x2
    assert len(rec['1DA310']) == 2, label
    lib_models = box_library(ram)
    colours = (fb((0.0, 128.0, 0.0, 128.0)), fb((128.0, 0.0, 0.0, 1.0)))
    for k, st in enumerate(rec['1DA310']):
        box = p.box[k]
        same(f'{label} 001DA310[{k}] anchor', fb(p.anchor)[:12], st['anchor'][:12])
        same(f'{label} 001DA310[{k}] size', M.f2b(p.box_size) & MASK32, st['f12'])
        same(f'{label} 001DA310[{k}] colour', colours[k], st['color'])
        same(f'{label} 001DA310[{k}] model object', lib_models[k], st['args'][3])
        same(f'{label} 001DA310[{k}] model', box.model, (0x14, 0x15)[k])
        same(f'{label} 001DA310[{k}] colour row', fb(box.color_row), st['record1'][0x50:0x60])
        same(f'{label} 001DA310[{k}] camera upload', fb(box.camera), st['record2'][0x20:0x60])
        same(f'{label} 001DA310[{k}] normal upload', fb(box.normal), st['record2'][0x60:0xA0])
        same(f'{label} 001DA310[{k}] RGBAQ', [box.rgbaq], st['rgbaq'])
        same(f'{label} 001DA310[{k}] clip pass VP', [fb(box.clip_pass)], [v for _, v in st['clip_vp']])
        same(f'{label} 001DA310[{k}] VP restored', st['vp'], st['vp_after'])
        count('D shadow 001DA310 calls')
    # 001D5C80
    assert len(rec['1D5C80']) == 1, label
    r = rec['1D5C80'][0]
    same(f'{label} 001D5C80 argument', 0x817FB0, r['a0'])
    same(f'{label} 001D5C80 screen matrix{HINT}', fb(p.screen_3400), r['s3400'])
    same(f'{label} 001D5C80 guard matrix{HINT}', fb(p.guard_3440), r['s3440'])
    bounds = [x[1] for x in nat.log if isinstance(x, tuple) and x[0] == 'bounds']
    same(f'{label} 001D5C80 probe order (ids){HINT}', [x & 0xFFFFFFFF for x in bounds],
         [x & 0xFFFFFFFF for x in rec['probes']])
    want = [[ts.object_address(ram, p.receiver[i].id), 2 if p.receiver[i].cls == 2 else 0]
            for i in range(p.receiver_count)]
    same(f'{label} 001D5C80 receiver draws', want, rec['receivers'])
    count('D shadow 001D5C80 calls')
    count('D shadow receivers', p.receiver_count)
    # 001D4B50
    b50 = rec['1D4B50']
    same(f'{label} 001D4B50 calls', [x[0] for x in want if x[1] == 2], [st['obj'] for st in b50])
    expect = []
    for st in b50:
        expect += clip_native(n, st['obj'])
    same(f'{label} 001D4B50 worker calls', expect, rec['b50_calls'])
    count('D shadow 001D4B50 calls', len(b50))
    return {'label': label, 'drawn': 1, 'receivers': p.receiver_count,
            'class2': len(b50), 'near': p.near_index, 'far': p.far_index}, rec['outcomes']


def shadow_library():
    """em_shadow_original.c as test_shadow_original_reference builds it.
    EM_RVR_SHADOW_SRC may name another copy of the file (used to check a
    proposed fix before it lands; the default is the tree's file)."""
    src = Path(os.environ.get('EM_RVR_SHADOW_SRC', str(ROOT / 'src/game/em_shadow_original.c')))
    STATS['D shadow source'] = str(src.relative_to(ROOT)) if src.is_relative_to(ROOT) else str(src)
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / ('shadow.dylib' if sys.platform == 'darwin' else 'shadow.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared',
                    '-fPIC', '-I' + str(ROOT / 'src'), str(src), '-o', str(path)], check=True)
    lib = C.CDLL(str(path))
    lib.em_shadow_original_001DA6A0.argtypes = [C.c_char_p, P(C.c_char_p), U32, P(ts.Scene), P(ts.State),
                                                P(ts.Plan), P(ts.Workers), P(ts.Fault)]
    return lib


HINT = (' (em_shadow_original.c: the COP1 add.s/sub.s of 001D2D20 and of 001D5C80\'s cell math need '
        'the EE pre-trim; docs/RENDER_VERIFY_REST.md "Findings")')
BOUNDARY_Z = 0x412E8F89   # found by a search over the row boundaries of the playable grid


def cell_rows(pos, origin, cell, pretrim):
    """001D5C80's row range (after its clamps) for one axis, with the EE
    pre-trim add/sub (the original's COP1 rule) or plain truncation."""
    add = M.ee_add if pretrim else lambda a, b: M.f2b(fall.shared.fp(M.b2f(a) + M.b2f(b))) & MASK32
    sub = M.ee_sub if pretrim else lambda a, b: M.f2b(fall.shared.fp(M.b2f(a) - M.b2f(b))) & MASK32
    one, half, fifteen = M.f2b(1.0), M.f2b(0.5), M.f2b(15.0)
    d = sub(pos, origin)
    c = s32(M.ee_cvt_w_s(add(one, M.ee_div(d, cell))))
    lo = s32(M.ee_cvt_w_s(sub(M.ee_div(sub(d, fifteen), cell), half)))
    hi = s32(M.ee_cvt_w_s(add(half, M.ee_div(add(fifteen, d), cell))))
    lo = c - 1 if not lo < c - 1 else lo
    hi = c + 1 if not c + 1 < hi else hi
    return max(lo, 0), hi if hi < 0x20 else 0x1F


ELF = [None]
LIBS = [None, None]
TEMPLATE = [None]


def shadow_worker(item):
    kind, label, ee_path, sp_path, seed = item
    n, lib = LIBS
    ram = Path(ee_path).read_bytes()
    scratch = Path(sp_path).read_bytes() if sp_path else None
    saved = dict(STATS)
    STATS.clear()
    try:
        entry, outcomes = shadow_case(n, lib, kind, label, ram, scratch, seed)
        return [entry], outcomes, dict(STATS)
    finally:
        STATS.clear()
        STATS.update(saved)


def shadow_case(n, lib, kind, label, ram, scratch, seed):
    if kind == 'capture':
        return shadow_compare(n, lib, label, ram, scratch, TEMPLATE[0])
    if kind == 'boundary':
        # A receiver-row boundary. The anchor is pinned (actor+0x98 = 0xFF,
        # actor+0xB0 = the captured anchor, so the shadow still draws) and the
        # highest node is moved to z = BOUNDARY_Z, 60 units up: with the
        # playable grid (ctx+0x150..0x15C) the original's 001D5C80 cell math
        # (COP1 add.s/sub.s with the EE pre-trim) and plain truncated add/sub
        # select different grid rows there (asserted below). A searched input
        # that discriminates the two rules, not original data.
        buf = bytearray(ram)
        ctx = u32(buf, CTX_PTR)
        nat = ts.NativeRun(lib, bytes(buf), ts.scene_view(bytes(buf), scratch))
        cell, origin = u32(buf, ctx + 0x154), u32(buf, ctx + 0x15C)
        assert cell_rows(BOUNDARY_Z, origin, cell, True) != cell_rows(BOUNDARY_Z, origin, cell, False), \
            'BOUNDARY_Z no longer discriminates for this grid'
        struct.pack_into('<4f', buf, PLAYER + 0xB0, *nat.plan.anchor)
        buf[PLAYER + 0x98] = 0xFF
        node = u32(buf, PLAYER + 0x110 + 4 * nat.plan.near_index)
        x, y = struct.unpack_from('<2f', buf, node + 0xC0)
        struct.pack_into('<fI', buf, node + 0xC4, y + 60.0, BOUNDARY_Z)
        STATS['D boundary rows (pre-trim, truncated)'] = [cell_rows(BOUNDARY_Z, origin, cell, True),
                                                          cell_rows(BOUNDARY_Z, origin, cell, False)]
        return shadow_compare(n, lib, label, bytes(buf), scratch, TEMPLATE[0])
    # synthetic: perturb the player's node positions and D_00817FF0
    # seed % 4: 0/1 jitter the nodes by up to 3 units, 2 also moves the whole
    # body by up to 40 units in x/z (other grid cells), 3 leaves two nodes
    # (001DA080's scan loop runs zero times).
    rng = random.Random(seed)
    buf = bytearray(ram)
    cnt = buf[PLAYER + 9]
    shift = (rng.uniform(-40, 40), rng.uniform(-40, 40)) if seed % 4 == 2 else (0.0, 0.0)
    for i in range(1, cnt):
        node = u32(buf, PLAYER + 0x110 + 4 * i)
        x, y, z, w = struct.unpack_from('<4f', buf, node + 0xC0)
        struct.pack_into('<3f', buf, node + 0xC0, x + shift[0] + rng.uniform(-3, 3), y + rng.uniform(-3, 3),
                         z + shift[1] + rng.uniform(-3, 3))
    if seed % 4 == 2:
        x, y, z = struct.unpack_from('<3f', buf, PLAYER + 0xB0)
        struct.pack_into('<3f', buf, PLAYER + 0xB0, x + shift[0], y, z + shift[1])
    if seed % 4 == 3:
        buf[PLAYER + 9] = 2
    ff0 = [rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-1, 1), 0.0]
    return shadow_compare(n, lib, label, bytes(buf), scratch, TEMPLATE[0], ff0)


def section_d_shadow():
    items = [('capture', lab, str(p), str(s) if s else None, 0) for lab, p, s in route_items()]
    quick_keep = {'playable', 'opening', '06_hill_slide', '14_roger_encounter'}
    items = [it for it in items if RM.FULL or it[1] in quick_keep]
    base = REF / 'playable_ee.bin'
    synth = [('synthetic', f'synthetic-{i}', str(base), None, 100 + i) for i in range(RM.pick(48, 8))]
    synth.append(('boundary', 'boundary-row', str(base), None, 0))
    results = RM.parallel_map(shadow_worker, items + synth)
    entries, outcomes = [], set()
    for ents, outs, stats in results:
        entries += ents
        outcomes |= outs
        for k, v in stats.items():
            if isinstance(v, int): count(k, v)
            else: STATS[k] = v
    # both outcomes of every conditional branch of 001DA080
    e = RvrEE(ELF[0])
    branches = set()
    for pc in range(0x1DA080, 0x1DA080 + SIZES[0x1DA080], 4):
        word = e.load(pc)
        if e.branch(word, pc) is not None and not (word >> 26 == 4 and word >> 16 & 0x3FF == 0):
            branches.add(pc)                 # conditional only (not b = beq zero, zero)
    missing = sorted((hex(pc), t) for pc in branches for t in (True, False) if (pc, t) not in outcomes)
    assert not missing, ('001DA080 branch outcomes not reached', missing)
    STATS['D 001DA080 branch outcomes'] = 2 * len(branches)
    return entries


# ======================================================================
# E. Fail-stop
# ======================================================================

def section_e(n):
    fault = Fault()
    empty = AreaW()
    key = u8buf(b'\x0b\x00')
    assert n.em_rvr_001C1DC0(key, C.byref(empty), C.byref(fault)) == -1 and fault.code == 1
    assert fault.address == 0x1D2830
    # a failing worker latches, and a latched fault refuses the next call
    calls = []
    cbs = [FN(I, VP, I32, I32)(lambda _, a, b: calls.append(a) or (-1 if a == 0x21 else 0)),
           FN(I, VP, U64)(lambda _, t: 0), FN(I, VP, U32)(lambda _, a: 0), FN(I, VP, U64)(lambda _, t: 0),
           FN(I, VP)(lambda _: 0), FN(I, VP)(lambda _: 0), FN(I, VP, U32)(lambda _, b: 0)]
    w, fault = AreaW(None, *cbs), Fault()
    assert n.em_rvr_001C1F50(key, C.byref(w), C.byref(fault)) == -1
    assert (fault.code, fault.address, calls) == (2, 0x1D2830, [0x20, 0x21])
    assert n.em_rvr_001C1E90(C.byref(fault)) == -1
    # views too small / outside the window
    rc = RenderCtx(u8buf(bytes(0x1D4)), 0x1D4)
    fault = Fault()
    assert n.em_rvr_001E2260(C.byref(rc), 1, C.byref(fault)) == -1 and fault.code == 3
    rc = RenderCtx(u8buf(struct.pack('<4I', 0, 0, 0, 0) + struct.pack('<I', 0x1000) + bytes(0x40)), 0x54)
    mem = Memory(u8buf(bytes(0x7F)), 0x1000, 0x7F)
    fault, body = Fault(), U32()
    assert n.em_rvr_001DA1E0(C.byref(rc), C.byref(mem), 0, u8buf(bytes(64)), 0, C.byref(body),
                             C.byref(fault)) == -1 and fault.code == 3
    assert bytes(C.string_at(mem.bytes, 0x7F)) == bytes(0x7F), 'wrote before the fault'
    # 001C22A0: a record too short for the bone table faults at the store
    SIZE = 0x118
    rec = u8buf(bytes(SIZE))
    cbs = [FN(I, VP, U32, U32, P(U32))(lambda _, b, c, o: 0), FN(I, VP, P(U8), U32, U32, I32)(lambda *a: 0),
           FN(I, VP, U32, P(U32))(lambda _, v, o: o.__setitem__(0, 3) or 0),
           FN(I, VP, P(U32))(lambda _, o: o.__setitem__(0, 0xAABBCCDD) or 0),
           FN(I, VP, I32)(lambda _, c: 0), FN(I, VP, P(U8), U32)(lambda *a: 0)]
    w, res, fault = ModelW(None, *cbs), I32(), Fault()
    assert n.em_rvr_001C22A0(rec, SIZE, 0x6E0000, 0, 0x7F, C.byref(w), C.byref(res), C.byref(fault)) == -1
    assert (fault.code, fault.address) == (3, 0x1C22A0 + 0x74)
    assert bytes(rec)[0x110:0x118] == bytes.fromhex('ddccbbaaddccbbaa')
    # 001027E0 with NULL
    fault = Fault()
    assert n.em_rvr_001027E0(None, None, C.byref(fault)) == -1 and fault.code == 3
    count('E fail-stop cases', 7)


# ======================================================================

def main():
    elf = read_elf()
    ELF[0] = elf
    validate_ops(elf)
    n = build_native()
    lib = shadow_library()
    LIBS[0], LIBS[1] = n, lib
    rng = random.Random(0x5EED)
    playable = (REF / 'playable_ee.bin').read_bytes()
    e = RvrEE(elf, playable)
    ctx = u32(playable, CTX_PTR)
    colour = e.read(0x250F30, 16)
    base = RvrEE(elf)                     # the ELF image alone
    same('D_00250F30 capture vs ELF', colour, base.read(0x250F30, 16))
    template = base.read(0x2531D0, 64)
    TEMPLATE[0] = template
    report = {}

    section_a_area(e, n, ctx, colour)
    section_a_stores(e, n, ctx, rng)
    section_a_background(e, n, ctx, rng)
    section_a_model(e, n, rng)
    # A (route): 001C1F50 and 001E0CF0 over every capture/beat with its own key and flags
    route = []
    for label, ee_path, sp_path in route_items():
        if not RM.FULL and label not in ('playable', 'opening', '03_panel_power', '09_fence_door'):
            continue
        ram = ee_path.read_bytes()
        er = RvrEE(elf, ram, sp_path.read_bytes() if sp_path else None)
        c = u32(ram, CTX_PTR)
        key = ram[D810700] << 8 | ram[D810700 + 1]
        area_case(er, n, 0x1C1F50, key, c, colour)
        flags = bg_case(er, n, c, None, [0x11110000, 0x22220000])
        route.append({'label': label, 'key': hex(key), '001D2910': [hex(f) for f in flags],
                      'ctx_1C0_matches_001C1F50_colour': er.read(c + 0x1C0, 16) == colour,
                      'ctx_1D0_tag': hex(struct.unpack('<Q', er.read(c + 0x1D0, 8))[0])})
        count('A route captures')
    report['route_L31'] = route

    section_b(e, n, rng, [struct.unpack('<16I', playable[ctx + off:ctx + off + 64])
                          for off in (0x2380, 0x2340, 0x2240, 0x23C0)] +
              [struct.unpack('<16I', playable[0x810610:0x810650])])
    section_c(e, n)
    section_d_units(e, n, ctx, rng, template)
    report['shadow'] = section_d_shadow()
    section_e(n)

    report['stats'] = STATS
    (OUT / 'report.json').write_text(json.dumps(report, indent=1, default=str))
    for r in report['route_L31']:
        print('L31 route', r['label'], 'key', r['key'], '001D2910', r['001D2910'], 'tag', r['ctx_1D0_tag'])
    for s in report['shadow']:
        print('L29b shadow', s)
    print(json.dumps(STATS))
    RM.banner(RM.part(STATS.get('A 001C1F50 keys', 0), STATS['A keys total'], '001C1F50 keys'),
              RM.part(STATS.get('A 001C22A0/001C2360 cases', 0), STATS['A model cases total'], 'model-bind cases'),
              f"{STATS.get('D shadow 001DA080 calls', 0)} intercepted shadow chains")
    print('PASS test_render_verify_rest_reference')


if __name__ == '__main__':
    main()
