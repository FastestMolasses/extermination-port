#!/usr/bin/env python3
"""Execute the script host's original callees and compare em_script_host_workers.c.

docs/SCRIPT_HOST_WORKERS.md. The user's pinned ELF (and the captured RAM)
supplies every instruction and table; none are embedded here. Routines
executed unmodified, each with its whole original call tree unless named as
hooked:

  00182BF0  op16 frame predicate (0021BB00 runs as original code)
  001B1240  bearing       (0011E620 atan2f and 001B1470 run as original code)
  001B1380  side test     (the same)
  001B12B0  turn toward   (001B1470 runs as original code)
  001B6250  pad actuator stop     (00111018 hooked and recorded)
  001B0C00  fade-out + stream fades (001AEDE0 / 001FAD70 hooked and recorded)

The atan2f error path (both operands EE zero) starts with 00128350; that
callee is hooked and ends the original run, and the native side must fault
at 0x00128350 (the SDK module's unbound error-path worker).

Arithmetic: ScriptEE routes every COP1 operation through
tools/ee_float_model.py (the measured model, docs/EE_FLOAT_MODEL.md); the
shared interpreter file is not edited.

Parts (every mode):
  1. unit cases over synthetic records, with branch coverage of every
     conditional branch of the six routines asserted;
  2. captured RAM: every startup-reference capture and every route snapshot
     (player record, flag bytes, pad block, owner/player vectors, script
     rates) through the original and the native routines;
  3. the AREA11 script host (tools/test_area_script_reference.py, imported,
     not edited) with these translations answering w_001B1240 / w_001B12B0 /
     w_001B1380 instead of scratch original executions: its lockstep
     scenarios that reach them, and the route capture of beat 10 (Roger
     0x828990's op15 turn, concurrent with director 0x8294C0);
  4. the AREA11 script export and the native loader, byte for byte against
     the captured overlay RAM, plus the loader's refusals;
  5. fail-stop: every missing pointer/worker faults at its address before
     any write; failing workers stop the routine.
Quick mode samples the bulk sweeps; EM_TEST_FULL=1 runs them whole.
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
import reference_mode as rm  # noqa: E402
import ee_float_model as M  # noqa: E402
from test_player_slide_reference import EE, RETURN, sx32, ELF_SHA256  # noqa: E402

MASK = 0xFFFFFFFF
DECOMP = ROOT.parent / 'Extermination'
REF = DECOMP / 'build/startup-reference'
ROUTE = DECOMP / 'build/s87/route'
LANE = os.environ.get('EM_LANE', 'script_host_workers_reference')
OUT = ROOT / 'build' / LANE

PRED, BEARING, SIDE, TURN, PAD_STOP, FADE8 = 0x182BF0, 0x1B1240, 0x1B1380, 0x1B12B0, 0x1B6250, 0x1B0C00
WRAP, ATAN2, ERROR_PATH, PAD_WRITE, FADE, STREAM = 0x1B1470, 0x11E620, 0x128350, 0x111018, 0x1AEDE0, 0x1FAD70
SIZES = {PRED: 0x148, BEARING: 0x30, SIDE: 0x68, TURN: 0xCC, PAD_STOP: 0x4C, FADE8: 0x58,
         0x1B0460: 0x358}
PLAYER, PAD = 0x8102B0, 0x810E40
G_BC, G_3C, G_F1 = 0x8106BC, 0x81083C, 0x8106F1
WRAP_LIMIT = 0x45800000
VEC_A, VEC_B = 0x01F80000, 0x01F80100      # oracle argument vectors (test-owned)


def F(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def read_elf():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA256, 'wrong original executable'
    return elf


# ======================================================================
# The interpreter with the measured float model
# ======================================================================

class ErrorPath(Exception):
    """The original reached 00128350 (the atan2f error path)."""


class ScriptEE(EE):
    """The shared EE core with COP1 taken from ee_float_model (raw bits),
    write tracking and optional branch-outcome recording."""

    def __init__(self, elf, ram=None):
        super().__init__(elf, ram)
        self.acc = 0
        self.outcomes = None
        self.written = set()

    def save(self, address, value, size=4):
        a = address & MASK
        if not 0x7F000000 <= a < 0x7F100000:
            self.written.update(range(a, a + size))
        super().save(address, value, size)

    def branch(self, word, pc):
        b = super().branch(word, pc)
        if b is not None and self.outcomes is not None and in_translated(pc):
            self.outcomes.add((pc, b[0]))
        return b

    def cop1(self, word, pc):
        rs, rt = word >> 21 & 31, word >> 16 & 31
        fs, fd, fn = word >> 11 & 31, word >> 6 & 31, word & 63
        f = self.f
        if rs == 0:                                          # mfc1
            if rt: self.r[rt] = sx32(f[fs])
            return
        if rs == 4: f[fs] = self.r[rt] & MASK; return        # mtc1
        if rs == 2:                                          # cfc1
            if rt: self.r[rt] = 0
            return
        if rs == 6: return                                   # ctc1
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

    def call_bits(self, entry, args=(), fregs=()):
        """Run an original routine; returns (v0 low word, f0 bits)."""
        self.r[29] = 0x7F0F0000
        for i, value in enumerate(args): self.r[4 + i] = sx32(value)
        for i, value in enumerate(fregs): self.f[12 + i] = value & MASK
        self.r[31] = RETURN
        self.run(entry)
        return self.r[2] & MASK, self.f[0] & MASK


def in_translated(pc):
    return any(start <= pc < start + size for start, size in SIZES.items())


def conditional_branches(elf):
    """Every conditional branch in the seven routines (the unconditional
    branch-always form, equal registers zero and zero, excluded)."""
    ee = EE(elf)
    found = set()
    for start, size in SIZES.items():
        for pc in range(start, start + size, 4):
            word = ee.load(pc)
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            if op in (4, 20) and rs == 0 and rt == 0:
                continue
            if op in (4, 5, 6, 7, 20, 21, 22, 23, 1) or (op == 17 and rs == 8):
                found.add(pc)
    return found


class Oracle:
    """The original routines on one ScriptEE; 001B1470 spied (its original
    body still runs), the named callees hooked and recorded."""

    def __init__(self, elf, ram=None):
        self.ee = ScriptEE(elf, ram)
        self.calls, self.wraps = [], []
        ee = self.ee
        ee.hooks[WRAP] = self.spy
        ee.hooks[ERROR_PATH] = self.error_path
        ee.hooks[PAD_WRITE] = self.pad_write
        ee.hooks[FADE] = lambda e: self.record('001AEDE0', e, 2)
        ee.hooks[STREAM] = lambda e: self.record('001FAD70', e, 3)

    def spy(self, ee):
        self.wraps.append(ee.f[12] & MASK)
        ra = ee.r[31]
        del ee.hooks[WRAP]
        try:
            ee.r[31] = RETURN
            ee.run(WRAP)
        finally:
            ee.hooks[WRAP] = self.spy
        ee.r[31] = ra

    def error_path(self, ee):
        raise ErrorPath()

    def pad_write(self, ee):
        act = ee.arg(2)
        self.calls.append(('00111018', ee.arg(0), ee.arg(1), act, ee.read(act, 6)))
        ee.ret_int(0)

    def record(self, name, ee, count):
        self.calls.append((name,) + tuple(ee.arg(i) for i in range(count)))
        ee.ret_int(0)

    def run(self, entry, args=(), fregs=()):
        """(outcome, v0, f0): outcome 'ok' or 'error-path'."""
        self.calls, self.wraps = [], []
        self.ee.written = set()
        try:
            v0, f0 = self.ee.call_bits(entry, args, fregs)
        except ErrorPath:
            return 'error-path', None, None
        return 'ok', v0, f0


# ======================================================================
# Native side (ctypes)
# ======================================================================

P = C.POINTER
U8, U32, I32, VP = C.c_uint8, C.c_uint32, C.c_int32, C.c_void_p


class LiveActor(C.Structure):
    _fields_ = [('bytes', U8 * 0x320), ('link_owner', VP), ('link_prev', VP),
                ('link_flags', U8), ('link_type', U8)]


class SdkWorld(C.Structure):
    _fields_ = [('d26C5D0', P(I32))]


class SdkWorkers(C.Structure):
    _fields_ = [('context', VP), ('w_00128350', VP), ('w_0011DB90', VP), ('w_0011FD78', VP),
                ('w_00127758', VP)]


PAD_FN = C.CFUNCTYPE(C.c_int, VP, I32, I32, P(U8))
FADE_FN = C.CFUNCTYPE(C.c_int, VP, I32, I32)
STREAM_FN = C.CFUNCTYPE(C.c_int, VP, I32, I32, I32)


PF = P(C.c_float)
# 001B0460's pointers: (field, original address, ctype).
CAMERA_FIELDS = [
    ('d810700', 0x810700, U8), ('d810701', 0x810701, U8), ('d810702', 0x810702, U8),
    ('d8101E1', 0x8101E1, U8), ('d8101E2', 0x8101E2, U8), ('d8101E3', 0x8101E3, U8),
    ('d8101E5', 0x8101E5, U8), ('d8101E6', 0x8101E6, U8), ('d8101E7', 0x8101E7, U8),
    ('d8101E8', 0x8101E8, C.c_int16), ('d8101EC', 0x8101EC, C.c_float),
    ('cam_10', 0x8101F0, C.c_float), ('cam_20', 0x810200, C.c_float),
    ('d810244', 0x810244, C.c_float), ('d8106C8', 0x8106C8, I32), ('d8106CD', 0x8106CD, U8),
    ('d275BE0', 0x275BE0, U8), ('d8106BE', 0x8106BE, U8), ('d8104E0', 0x8104E0, I32),
    ('d810350', 0x810350, C.c_float), ('d810370', 0x810370, C.c_float),
    ('d8105D0', 0x8105D0, C.c_float), ('d8105E0', 0x8105E0, C.c_float),
    ('spad3400', 0x70003400, C.c_float), ('spad3600', 0x70003600, C.c_float)]
# The storage those pointers cover: (original base, size).
CAMERA_REGIONS = [(0x810700, 3), (0x8101E0, 0x30), (0x810244, 4), (0x8106C8, 4), (0x8106CD, 1),
                  (0x275BE0, 1), (0x8106BE, 1), (0x8104E0, 4), (0x810350, 16), (0x810370, 16),
                  (0x8105D0, 32), (0x70003400, 64), (0x70003600, 16)]
SCRIPT_FN = {
    'w_001B0250': C.CFUNCTYPE(C.c_int, VP), 'w_001B0B50': C.CFUNCTYPE(C.c_int, VP),
    'w_001B0080': C.CFUNCTYPE(C.c_int, VP, U32, C.c_float),
    'w_0018C0D0': C.CFUNCTYPE(C.c_int, VP, U32, I32),
    'w_001DD980': C.CFUNCTYPE(C.c_int, VP, PF, PF),
    'w_001029C0': C.CFUNCTYPE(C.c_int, VP, PF),
    'w_00102C58': C.CFUNCTYPE(C.c_int, VP, PF, PF, PF),
    'w_001026A0': C.CFUNCTYPE(C.c_int, VP, PF, PF, PF),
    'w_001028B8': C.CFUNCTYPE(C.c_int, VP, PF, PF, PF),
}


class World(C.Structure):
    _fields_ = [('player_address', U32), ('player', P(LiveActor)), ('d8106BC', P(U8)),
                ('d81083C', P(U8)), ('d8106F1', P(U8)), ('pad_address', U32), ('d810E40', P(U8)),
                ('sdk_tables', VP), ('sdk_world', P(SdkWorld)), ('sdk_workers', P(SdkWorkers)),
                ('elf', P(C.c_ubyte)), ('elf_size', C.c_size_t)] + [
                    (name, P(ctype)) for name, _, ctype in CAMERA_FIELDS]


class Callees(C.Structure):
    _fields_ = [('ctx', VP), ('w_00111018', PAD_FN), ('w_001AEDE0', FADE_FN),
                ('w_001FAD70', STREAM_FN)] + [(name, fn) for name, fn in SCRIPT_FN.items()]


class Host(C.Structure):
    _fields_ = [('world', World), ('callees', Callees), ('fault_address', U32)]


class Scripts(C.Structure):
    pass


SHIM = r'''
#include <stddef.h>
#include "game/em_script_host_workers.h"
size_t shw_layout(int i) {
    switch (i) {
    case 0: return sizeof(EmScriptHostWorkers);
    case 1: return offsetof(EmScriptHostWorkers, callees);
    case 2: return offsetof(EmScriptHostWorkers, fault_address);
    case 3: return offsetof(EmScriptHostWorkersWorld, sdk_workers);
    case 4: return sizeof(EmSdkMathTables);
    case 5: return sizeof(EmArea11Scripts);
    case 6: return offsetof(EmArea11Scripts, quad);
    case 7: return sizeof(EmScriptImage);
    case 8: return sizeof(EmPlayerLiveActor);
    case 9: return offsetof(EmScriptHostWorkersWorld, spad3600);
    case 10: return offsetof(EmScriptHostWorkersCallees, w_001028B8);
    default: return 0;
    }
}
'''
SOURCES = ['src/game/em_script_host_workers.c', 'src/game/em_player_stage_workers.c',
           'src/game/em_sdk_math_original.c', 'src/game/em_script.c']


class Image(C.Structure):
    _fields_ = [('bytes', P(C.c_ubyte)), ('base', U32), ('entry', U32), ('length', U32)]


Scripts._fields_ = [('image', Image * 3), ('quad', C.c_float * 48), ('quads_loaded', C.c_int)]


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    shim = OUT / 'layout_shim.c'
    shim.write_text(SHIM)
    lib = OUT / ('script_host_workers.dylib' if sys.platform == 'darwin' else 'script_host_workers.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc', *SOURCES, str(shim),
                    '-o', str(lib)], cwd=ROOT, check=True)
    n = C.CDLL(str(lib))
    n.shw_layout.restype = C.c_size_t
    n.shw_layout.argtypes = [C.c_int]
    mine = (C.sizeof(Host), Host.callees.offset, Host.fault_address.offset, World.sdk_workers.offset,
            None, C.sizeof(Scripts), Scripts.quad.offset, C.sizeof(Image), C.sizeof(LiveActor),
            World.spad3600.offset, Callees.w_001028B8.offset)
    for i, value in enumerate(mine):
        if value is not None:
            assert n.shw_layout(i) == value, ('ctypes layout differs from C', i, n.shw_layout(i), value)
    H = P(Host)
    n.em_script_host_00182BF0.argtypes = [H, U32, P(I32)]
    n.em_script_host_001B1240.argtypes = [H, P(U32), U32, U32, P(U32)]
    n.em_script_host_001B1380.argtypes = [H, P(U32), P(U32), U32, P(I32)]
    n.em_script_host_001B12B0.argtypes = [H, U32, U32, U32, P(U32)]
    n.em_script_host_001B6250.argtypes = [H, U32]
    n.em_script_host_001B0C00.argtypes = [H, I32]
    n.em_script_host_001B0460.argtypes = [H, I32]
    n.em_script_host_approach.argtypes = [VP, U32, U32, U32, P(U32)]
    n.em_script_host_w_001B1240.argtypes = [VP, P(C.c_float), C.c_float, C.c_float, P(C.c_float)]
    n.em_script_host_w_001B12B0.argtypes = [VP, C.c_float, C.c_float, C.c_float, P(C.c_float)]
    n.em_script_host_w_001B1380.argtypes = [VP, P(C.c_float), P(C.c_float), C.c_float, P(I32)]
    n.em_script_host_owner_001B6250.argtypes = [VP]
    n.em_sdk_math_original_load_tables.argtypes = [P(C.c_ubyte), C.c_size_t, VP]
    n.em_area11_scripts_load.argtypes = [P(Scripts), C.c_char_p, C.c_char_p, C.c_char_p, C.c_char_p]
    n.em_area11_scripts_free.argtypes = [P(Scripts)]
    n.em_area11_scripts_image.argtypes = [P(Scripts), U32]
    n.em_area11_scripts_image.restype = P(Image)
    n.em_area11_scripts_entries.argtypes = [P(C.c_size_t)]
    n.em_area11_scripts_entries.restype = P(U32)
    n.em_area11_scripts_director_quads.argtypes = [P(Scripts), P(VP * 3)]
    return n


class Native:
    """One EmScriptHostWorkers over test-owned canonical storage."""

    def __init__(self, lib, elf):
        self.lib = lib
        self.actor = LiveActor()
        self.flags = {name: U8() for name in ('bc', '3c', 'f1')}
        self.pad = (U8 * 0x40)()
        self.tables = (C.c_ubyte * lib.shw_layout(4))()
        buf = (C.c_ubyte * len(elf)).from_buffer_copy(elf)
        assert lib.em_sdk_math_original_load_tables(buf, len(elf), C.cast(self.tables, VP)) == 0
        mode = struct.unpack_from('<i', elf, 0x26C5D0 - 0x100000 + 0x300)[0]
        self.mode = I32(mode)
        self.sdk_world = SdkWorld(C.pointer(self.mode))
        self.sdk_workers = SdkWorkers()
        self.calls, self.fail_on = [], None
        self.keep = [PAD_FN(self.pad_write), FADE_FN(self.fade), STREAM_FN(self.stream)]
        self.host = Host()
        self.reset()

    def reset(self):
        h = self.host
        w = h.world
        w.player_address, w.player = PLAYER, C.pointer(self.actor)
        w.d8106BC, w.d81083C, w.d8106F1 = (C.pointer(self.flags[k]) for k in ('bc', '3c', 'f1'))
        w.pad_address, w.d810E40 = PAD, C.cast(self.pad, P(U8))
        w.sdk_tables, w.sdk_world = C.cast(self.tables, VP), C.pointer(self.sdk_world)
        w.sdk_workers = C.pointer(self.sdk_workers)
        h.callees.ctx = None
        h.callees.w_00111018, h.callees.w_001AEDE0, h.callees.w_001FAD70 = self.keep
        h.fault_address = 0
        self.calls, self.fail_on = [], None

    def result(self, name):
        """0, or -1 for the call numbered fail_on (0 = the first)."""
        return -1 if self.fail_on is not None and len(self.calls) - 1 == self.fail_on else 0

    def pad_write(self, _, port, slot, act):
        offset = C.addressof(act.contents) - C.addressof(self.pad)
        self.calls.append(('00111018', port & MASK, slot & MASK, PAD + offset, bytes(act[:6])))
        return self.result('00111018')

    def fade(self, _, a0, a1):
        self.calls.append(('001AEDE0', a0 & MASK, a1 & MASK))
        return self.result('001AEDE0')

    def stream(self, _, a0, a1, a2):
        self.calls.append(('001FAD70', a0 & MASK, a1 & MASK, a2 & MASK))
        return self.result('001FAD70')

    def ref(self):
        return C.byref(self.host)


def vec(words):
    return (U32 * 3)(*words[:3])


# ======================================================================
# Part 1: unit cases
# ======================================================================

ANGLES = [0x00000000, 0x80000000, 0x00000001, 0x80000001, 0x40490FDB, 0xC0490FDB, 0x40490FDC,
          0xC0490FDC, 0x40490FDA, 0x40C90FDB, 0xC0C90FDB, 0x3F800000, 0xBF800000, 0x3B64C389,
          0x3D8EFA35, 0x41200000, 0xC1200000, 0x45000000, 0x457FFFFF, 0xC57FFFFF]
STEPS = [0x00000000, 0x80000000, 0x00000001, 0x3B64C389, 0x3D8EFA35, 0x3DCCCCCD, 0x3F000000,
         0x3F800000, 0x40490FDB, 0x41200000, 0xBDCCCCCD]


def turn_cases(seed, count):
    rng = random.Random(seed)
    cases = []
    for t in ANGLES:
        for c in ANGLES:
            cases.append((t, c, rng.choice(STEPS)))
    for _ in range(count):
        c = F(rng.uniform(-12.6, 12.6)) if rng.random() < 0.8 else rng.choice(ANGLES)
        s = rng.choice(STEPS) if rng.random() < 0.6 else F(rng.uniform(0, 0.5))
        kind = rng.random()
        if kind < 0.3:
            t = M.ee_add(c, s) if rng.random() < 0.5 else M.ee_sub(c, s)     # difference == step
        elif kind < 0.4:
            t = c
        elif kind < 0.45:
            t = F(rng.uniform(4000, 6000) * rng.choice((1, -1)))           # near the domain bound
        else:
            t = F(rng.uniform(-12.6, 12.6))
        cases.append((t, c, s))
    return cases


def bearing_cases(seed, count):
    rng = random.Random(seed)
    coord = lambda: F(rng.uniform(-100, 600))
    cases = [((0, 0, 0), 0, 0), ((0x00000001, 0, 0), 0, 0x80000001),       # EE-zero deltas
             ((F(300.0), 0, F(200.0)), F(300.0), F(200.0)),
             ((F(300.0), 0, F(200.0)), F(300.0), F(250.0)), ((F(300.0), 0, F(200.0)), F(350.0), F(200.0)),
             ((F(300.0), 0, F(200.0)), F(250.0), F(200.0)), ((F(300.0), 0, F(200.0)), F(300.0), F(150.0)),
             ((F(300.0), 0, F(200.0)), F(299.9), F(199.9))]
    for _ in range(count):
        o = (coord(), coord(), coord())
        x, z = coord(), coord()
        if rng.random() < 0.1: x = o[0]
        if rng.random() < 0.1: z = o[2]
        cases.append((o, x, z))
    return cases


def side_cases(seed, count, oracle):
    rng = random.Random(seed)
    coord = lambda: F(rng.uniform(-100, 600))
    cases = [((0, 0, 0), (0, 0, 0), 0), ((F(1.0), 0, 0), (F(1.0), 0, 0), F(1.0))]
    for _ in range(count):
        a, b = (coord(), coord(), coord()), (coord(), coord(), coord())
        kind = rng.random()
        if kind < 0.6:
            yaw = F(rng.uniform(-3.2, 3.2))
        elif kind < 0.8:
            # a yaw on the angle itself or one ulp either side (relative 0 / tiny)
            outcome, _, angle = run_bearing_angle(oracle, a, b)
            if outcome != 'ok': continue
            yaw = rng.choice((angle, (angle + 1) & MASK, (angle - 1) & MASK, M.ee_neg(angle)))
        elif kind < 0.9:
            yaw = rng.choice(ANGLES)
        else:
            yaw = F(rng.uniform(4000, 6000) * rng.choice((1, -1)))
        cases.append((a, b, yaw))
    return cases


def run_bearing_angle(oracle, a, b):
    """atan2(a.x - b.x, a.z - b.z) as 001B1380 computes it (original code)."""
    dx, dz = M.ee_sub(a[0], b[0]), M.ee_sub(a[2], b[2])
    return oracle.run(ATAN2, (), (dx, dz))


def compare_math(oracle, native, kind, case):
    """One 001B1240 / 001B1380 / 001B12B0 case; returns the outcome tag."""
    ee = oracle.ee
    native.reset()
    lib = native.lib
    if kind == 'turn':
        t, c, s = case
        outcome, _, f0 = oracle.run(TURN, (), (t, c, s))
        out = U32()
        r = lib.em_script_host_001B12B0(native.ref(), t, c, s, C.byref(out))
        got = (r, native.host.fault_address, out.value if r == 0 else None)
        # the approach adapter is the same routine
        out2 = U32()
        r2 = lib.em_script_host_approach(None, t, c, s, C.byref(out2))
        assert (r2, out2.value if r2 == 0 else None) == (r, got[2]), (case, r2)
    elif kind == 'bearing':
        o, x, z = case
        for i, word in enumerate(o): ee.save(VEC_A + 4 * i, word)
        outcome, _, f0 = oracle.run(BEARING, (VEC_A,), (x, z))
        out = U32()
        r = lib.em_script_host_001B1240(native.ref(), vec(o), x, z, C.byref(out))
        got = (r, native.host.fault_address, out.value if r == 0 else None)
    else:
        a, b, yaw = case
        for i in range(3):
            ee.save(VEC_A + 4 * i, a[i]); ee.save(VEC_B + 4 * i, b[i])
        outcome, v0, _ = oracle.run(SIDE, (VEC_A, VEC_B), (yaw,))
        f0 = v0
        out = I32()
        r = lib.em_script_host_001B1380(native.ref(), vec(a), vec(b), yaw, C.byref(out))
        got = (r, native.host.fault_address, (out.value & MASK) if r == 0 else None)
    if outcome == 'error-path':
        assert got[:2] == (-1, ERROR_PATH), (kind, case, got)
        return 'error-path'
    refused = any(w & 0x7FFFFFFF >= WRAP_LIMIT for w in oracle.wraps)
    if refused:
        assert got[:2] == (-1, WRAP), (kind, case, [hex(w) for w in oracle.wraps], got)
        return 'refused'
    assert got == (0, 0, f0), (kind, case, hex(f0), got)
    return 'equal'


def pred_case(rng):
    actor = bytearray(rng.getrandbits(8) for _ in range(0x320))
    zeros = (0x00000000, 0x80000000, 0x00000001, 0x80000001)
    choose = lambda seq: rng.choice(seq)
    struct.pack_into('<I', actor, 0x220, choose(zeros + (F(1.0), F(-1.0), F(100.0), 0x7F7FFFFF,
                                                         0x7F800000, 0xFF800000, F(100.0), F(60.0))))
    actor[0xF] = choose((0x63, 0x62, 0, 0xB, 5, 5, 5))
    mode = choose(list(range(0x40)) + [0x3C, 0x3D, 0xB, 0xC, 0xD, 0xE, 0xF, 0xF, 0x2C, 0x2C,
                                        rng.randrange(256)])
    actor[0x1F0] = mode
    actor[4] = choose((1, 1, 0, 2))
    actor[5] = choose((8, 8, 7))
    actor[6] = choose((2, 3, 4, 5, 6, 0))
    actor[0xD] = choose((2, 1))
    for off in (0x22C, 0x224):
        struct.pack_into('<I', actor, off, choose(zeros + (F(1.0), F(3.0), F(-2.0))))
    return dict(actor=bytes(actor), bc=choose((0, 0, 1, 0x80)), c3c=choose((0, 0, 0, 1)),
                f1=choose((0, 0, 0, 1)))


def compare_pred(oracle, native, case, where='unit'):
    ee = oracle.ee
    ee.write(PLAYER, case['actor'])
    ee.save(G_BC, case['bc'], 1); ee.save(G_3C, case['c3c'], 1); ee.save(G_F1, case['f1'], 1)
    outcome, v0, _ = oracle.run(PRED, (PLAYER,))
    assert outcome == 'ok'
    allowed = set(range(PLAYER, PLAYER + 0x320)) | {G_BC}
    assert ee.written <= allowed, (where, 'original writes outside the compared storage',
                                   sorted(hex(a) for a in ee.written - allowed)[:8])
    native.reset()
    C.memmove(native.actor.bytes, case['actor'], 0x320)
    native.flags['bc'].value, native.flags['3c'].value, native.flags['f1'].value = (
        case['bc'], case['c3c'], case['f1'])
    out = I32()
    r = lib_call(native, 'em_script_host_00182BF0', native.ref(), PLAYER, C.byref(out))
    assert r == 0 and native.host.fault_address == 0, (where, r, hex(native.host.fault_address))
    assert out.value == sx32(v0), (where, out.value, v0)
    assert bytes(native.actor.bytes) == ee.read(PLAYER, 0x320), (where, 'record bytes differ')
    assert native.flags['bc'].value == ee.load(G_BC, 1), (where, 'D_008106BC differs')
    return out.value


def lib_call(native, name, *args):
    return getattr(native.lib, name)(*args)


def compare_pad(oracle, native, block, where='unit'):
    ee = oracle.ee
    ee.write(PAD, block)
    outcome, _, _ = oracle.run(PAD_STOP, (PAD,))
    assert outcome == 'ok'
    allowed = set(range(PAD, PAD + 0x2A))
    assert ee.written <= allowed, (where, sorted(hex(a) for a in ee.written - allowed))
    native.reset()
    C.memmove(native.pad, block, len(block))
    r = native.lib.em_script_host_001B6250(native.ref(), PAD)
    assert r == 0, (where, r, hex(native.host.fault_address))
    assert bytes(native.pad[:len(block)]) == ee.read(PAD, len(block)), (where, 'pad block differs')
    assert native.calls == oracle.calls, (where, native.calls, oracle.calls)
    return len(oracle.calls)


def compare_fade(oracle, native, a0):
    outcome, _, _ = oracle.run(FADE8, (a0,))
    assert outcome == 'ok' and not oracle.ee.written
    native.reset()
    assert native.lib.em_script_host_001B0C00(native.ref(), sx32(a0 & MASK)) == 0
    assert native.calls == oracle.calls, (hex(a0), native.calls, oracle.calls)


def unit_cases(elf):
    """Every unit case, generated once in the parent: (kind, case) pairs."""
    rng = random.Random(0x5C0B)
    pred = [pred_case(rng) for _ in range(rm.pick(20000, 900))]
    for mode in range(256):                           # every +1F0 value once
        case = pred_case(rng)
        actor = bytearray(case['actor']); actor[0x1F0] = mode
        struct.pack_into('<I', actor, 0x220, F(100.0)); actor[0xF] = 0
        pred.append(dict(case, actor=bytes(actor), bc=0, c3c=0, f1=0))
    blocks = []
    for ready in (0, 1, 0xFF):
        for active in (0, 1, 0x80):
            for _ in range(rm.pick(20, 3)):
                b = bytearray(rng.getrandbits(8) for _ in range(0x40))
                b[0x12], b[0x16] = ready, active
                blocks.append(bytes(b))
    cases = [('pred', c) for c in pred]
    cases += [('turn', c) for c in turn_cases(1, rm.pick(30000, 900))]
    cases += [('bearing', c) for c in bearing_cases(2, rm.pick(6000, 150))]
    cases += [('side', c) for c in side_cases(3, rm.pick(6000, 150), Oracle(elf))]
    cases += [('pad', b) for b in blocks]
    cases += [('fade', a0) for a0 in (8, 4, 0, 1, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 0x12345678)]
    return cases


def unit_job(cases):
    """One slice of the unit cases on its own oracle; returns the tags and
    the branch outcomes it saw."""
    elf, lib = CONTEXT['elf'], CONTEXT['lib']
    oracle = Oracle(elf)
    oracle.ee.outcomes = set()
    native = Native(lib, elf)
    tags = {}
    for kind, case in cases:
        if kind == 'pred':
            tag = 'take' if compare_pred(oracle, native, case) == 0 else 'wait'
        elif kind == 'pad':
            tag = 'write' if compare_pad(oracle, native, case) else 'idle'
        elif kind == 'fade':
            compare_fade(oracle, native, case)
            tag = 'equal'
        else:
            tag = compare_math(oracle, native, kind, case)
        tags[kind, tag] = tags.get((kind, tag), 0) + 1
    return tags, oracle.ee.outcomes


def unit_summary(elf, results):
    tags, seen = {}, set()
    for part, outcomes in results:
        for key, n in part.items(): tags[key] = tags.get(key, 0) + n
        seen |= outcomes
    for kind in ('turn', 'side'):
        assert tags.get((kind, 'refused'), 0) > 0, ('no domain refusal', kind)
    assert tags.get(('bearing', 'error-path'), 0) >= 2 and tags.get(('side', 'error-path'), 0) >= 1
    assert tags.get(('pred', 'take'), 0) and tags.get(('pred', 'wait'), 0)
    wanted = conditional_branches(elf)
    missing = sorted(hex(pc) + ('/taken' if t else '/not') for pc in wanted for t in (True, False)
                     if (pc, t) not in seen)
    assert not missing, ('branch outcomes not exercised', missing)
    return tags, len(wanted)


# ======================================================================
# Part 1b: 001B0460 (camera re-seat)
# ======================================================================

SEAT = 0x1B0460
SEAT_CALLEES = {0x1B0250: 'w_001B0250', 0x1B0B50: 'w_001B0B50', 0x1B0080: 'w_001B0080',
                0x18C0D0: 'w_0018C0D0', 0x1DD980: 'w_001DD980', 0x1029C0: 'w_001029C0',
                0x102C58: 'w_00102C58', 0x1026A0: 'w_001026A0', 0x1028B8: 'w_001028B8'}
SYNTH_AREAS, SYNTH_ROOMS, SYNTH_RECORDS = (5, 0x13), 0x200000, 0x200100


def elf_word(elf, address):
    return struct.unpack_from('<I', elf, address - 0x100000 + 0x300)[0]


def in_elf(address):
    return 0x100000 <= address <= 0x275B00 - 4


def seat_record(elf, area, room, index):
    """The record address 001B0460 resolves, or None when a read leaves the ELF."""
    if not in_elf(0x24D650 + 4 * area): return None
    rooms = elf_word(elf, 0x24D650 + 4 * area)
    if not in_elf(rooms + 4 * room): return None
    record = elf_word(elf, rooms + 4 * room) + index * 0x30
    if not (in_elf(record + 0x10) and in_elf(record + 0x18)): return None
    flags = elf_word(elf, record + 0x10)
    spot = (0x24A8D0 + (struct.unpack('<i', struct.pack('<I', flags))[0] >> 8) * 12) & MASK
    if flags & 0x80 and not (in_elf(spot) and in_elf(spot + 8)): return None
    return record


def seat_elf(elf, records):
    """The user's ELF with test-generated camera records for areas 5 (0 in
    the original table) and 0x13 (the area the kind-4 branch tests), planted
    over code 001B0460 never runs. Both sides read this same image."""
    data = bytearray(elf)
    put = lambda address, word: struct.pack_into('<I', data, address - 0x100000 + 0x300, word)
    for area in SYNTH_AREAS:
        put(0x24D650 + 4 * area, SYNTH_ROOMS)
    put(SYNTH_ROOMS, SYNTH_RECORDS)
    for i, (flags, kind, distance) in enumerate(records):
        base = SYNTH_RECORDS + 0x30 * i
        for off in range(0, 0x30, 4): put(base + off, 0)
        put(base + 0x10, flags); put(base + 0x14, kind); put(base + 0x18, distance)
    return bytes(data)


# Test-generated records: (flags, kind, distance). flags low byte = mode bit
# 0x80 and sub; flags >> 8 = the D_0024A8D0 row.
SYNTH = [(0x00000000, 0, F(40.0)), (0x0000000A, 0, F(35.0)), (0x00000080, 0, F(50.0)),
         (0x00000380, 1, F(45.5)), (0x0000058A, 0, F(30.0)), (0x00000005, 5, F(20.0)),
         (0x00000003, 4, F(25.0)), (0x00000009, 2, F(60.0)), (0x0000FF80, 0, F(10.0))]


def seat_cases(seed, count, real_elf):
    rng = random.Random(seed)
    real = []
    for area in (a for a in range(0x18) if a not in SYNTH_AREAS):
        for room in range(4):
            for index in range(3):
                if seat_record(real_elf, area, room, index) is not None:
                    real.append((area, room, index))
    cases = []
    for i in range(count):
        if i < len(SYNTH) * 4:
            where = (SYNTH_AREAS[i // len(SYNTH) % 2], 0, i % len(SYNTH))
        elif rng.random() < 0.5:
            where = rng.choice(real)
        else:
            where = (rng.choice(SYNTH_AREAS), 0, rng.randrange(len(SYNTH)))
        cases.append(dict(where=where, a0=rng.choice((0, 1, 1, 0xFFFFFFFF)),
                          b275=rng.choice((0, 1, 1, 2)), pose=rng.choice((0x10, 0x12, 0x11, 0)),
                          seed=rng.getrandbits(32),
                          player=[F(rng.uniform(-400, 400)) for _ in range(8)],
                          camera=bytes(rng.getrandbits(8) for _ in range(0x30))))
    return cases


class SeatScript:
    """The scripted callee effects of one case, identical on both sides."""

    def __init__(self, seed):
        self.seed, self.count = seed, 0

    def next(self, name):
        rng = random.Random('%d:%d:%s' % (self.seed, self.count, name))
        self.count += 1
        words = lambda n: [F(rng.uniform(-500, 500)) for _ in range(n)]
        if name == 'w_001B0250': return {'c8': rng.getrandbits(32)}
        if name == 'w_001B0B50': return {'be': rng.choice((0, 1, 0x81))}
        if name == 'w_001B0080': return {'e6': rng.choice((0, 3, 0xD, 0xF, 0x40))}
        if name == 'w_0018C0D0': return {'eye': words(4), 'target': words(4)}
        if name == 'w_001029C0': return {'out': words(16)}
        if name == 'w_00102C58': return {'out': words(16)}
        return {'out': words(4)}


def seat_oracle(elf, case):
    """Run the original 001B0460 over the case; returns (calls, region bytes)."""
    ee = ScriptEE(elf)
    ee.outcomes = CONTEXT.get('seat_outcomes')
    area, room, index = case['where']
    script = SeatScript(case['seed'])
    calls = []
    ee.save(0x810700, area, 1); ee.save(0x810701, room, 1); ee.save(0x810702, index, 1)
    ee.write(0x8101E0, case['camera'])
    ee.save(0x275BE0, case['b275'], 1)
    ee.save(0x8104E0, case['pose'])
    for i, word in enumerate(case['player']): ee.save(0x810350 + 4 * i if i < 4 else 0x810370 + 4 * (i - 4), word)
    vec = lambda a, n=4: tuple(ee.load(a + 4 * i) for i in range(n))

    def hook(name):
        def run(e):
            effect = script.next(name)
            if name == 'w_001B0250':
                calls.append((name,)); e.save(0x8106C8, effect['c8'])
            elif name == 'w_001B0B50':
                calls.append((name,)); e.save(0x8106BE, effect['be'], 1)
            elif name == 'w_001B0080':
                calls.append((name, e.arg(0), e.f[12] & MASK)); e.save(0x8101E6, effect['e6'], 1)
            elif name == 'w_0018C0D0':
                calls.append((name, e.arg(0), e.arg(1)))
                for i in range(4):
                    e.save(0x8105D0 + 4 * i, effect['eye'][i]); e.save(0x8105E0 + 4 * i, effect['target'][i])
            elif name == 'w_001DD980':
                calls.append((name, e.arg(0), e.arg(1), vec(e.arg(0)), vec(e.arg(1))))
            elif name == 'w_001029C0':
                calls.append((name, e.arg(0)))
                for i, w in enumerate(effect['out']): e.save(e.arg(0) + 4 * i, w)
            else:
                calls.append((name, e.arg(0), e.arg(1), e.arg(2),
                              vec(e.arg(1), 16 if name in ('w_00102C58', 'w_001026A0') else 4),
                              vec(e.arg(2))))
                for i, w in enumerate(effect['out']): e.save(e.arg(0) + 4 * i, w)
            e.ret_int(0)
        return run
    for address, name in SEAT_CALLEES.items():
        ee.hooks[address] = hook(name)
    ee.written = set()
    ee.call_bits(SEAT, (case['a0'],))
    allowed = set()
    for base, size in CAMERA_REGIONS: allowed.update(range(base, base + size))
    stray = ee.written - allowed
    assert not stray, ('001B0460 writes outside the compared storage', sorted(hex(a) for a in stray)[:8])
    regions = {base: ee.read(base, size) for base, size in CAMERA_REGIONS}
    return calls, regions


class SeatNative:
    """001B0460 over test-owned canonical storage, one buffer per region."""

    def __init__(self, lib, elf, native):
        self.lib, self.native = lib, native
        self.elf_buf = (C.c_ubyte * len(elf)).from_buffer_copy(elf)
        self.buffers = {base: (C.c_ubyte * size)() for base, size in CAMERA_REGIONS}
        self.keep = {name: fn(getattr(self, 'cb_' + name[2:])) for name, fn in SCRIPT_FN.items()}

    def locate(self, address):
        for base, size in CAMERA_REGIONS:
            if base <= address < base + size:
                return C.addressof(self.buffers[base]) + address - base
        raise AssertionError(hex(address))

    def original(self, pointer):
        at = C.cast(pointer, VP).value
        for base, size in CAMERA_REGIONS:
            start = C.addressof(self.buffers[base])
            if start <= at < start + size:
                return base + at - start
        raise AssertionError('pointer outside the canonical storage')

    def words(self, pointer, n=4):
        return tuple(C.cast(pointer, P(U32))[i] for i in range(n))

    def put(self, address, words):
        for i, w in enumerate(words): C.cast(self.locate(address), P(U32))[i] = w

    def cb_001B0250(self, _):
        e = self.script.next('w_001B0250'); self.calls.append(('w_001B0250',))
        C.cast(self.locate(0x8106C8), P(U32))[0] = e['c8']; return 0

    def cb_001B0B50(self, _):
        e = self.script.next('w_001B0B50'); self.calls.append(('w_001B0B50',))
        C.cast(self.locate(0x8106BE), P(U8))[0] = e['be']; return 0

    def cb_001B0080(self, _, camera, a1):
        e = self.script.next('w_001B0080'); self.calls.append(('w_001B0080', camera, F(a1)))
        C.cast(self.locate(0x8101E6), P(U8))[0] = e['e6']; return 0

    def cb_0018C0D0(self, _, camera, a1):
        e = self.script.next('w_0018C0D0'); self.calls.append(('w_0018C0D0', camera, a1 & MASK))
        self.put(0x8105D0, e['eye']); self.put(0x8105E0, e['target']); return 0

    def cb_001DD980(self, _, eye, target):
        self.script.next('w_001DD980')
        self.calls.append(('w_001DD980', self.original(eye), self.original(target),
                           self.words(eye), self.words(target)))
        return 0

    def cb_001029C0(self, _, m):
        e = self.script.next('w_001029C0'); self.calls.append(('w_001029C0', self.original(m)))
        self.put(self.original(m), e['out']); return 0

    def vu(self, name, out, a, b, n):
        e = self.script.next(name)
        self.calls.append((name, self.original(out), self.original(a), self.original(b),
                           self.words(a, n), self.words(b)))
        self.put(self.original(out), e['out']); return 0

    def cb_00102C58(self, _, out, a, b): return self.vu('w_00102C58', out, a, b, 16)
    def cb_001026A0(self, _, out, a, b): return self.vu('w_001026A0', out, a, b, 16)
    def cb_001028B8(self, _, out, a, b): return self.vu('w_001028B8', out, a, b, 4)

    def bind(self, host):
        w, k = host.world, host.callees
        w.elf, w.elf_size = C.cast(self.elf_buf, P(C.c_ubyte)), len(self.elf_buf)
        for name, address, ctype in CAMERA_FIELDS:
            setattr(w, name, C.cast(self.locate(address), P(ctype)))
        for name, fn in self.keep.items():
            setattr(k, name, fn)

    def run(self, case):
        for base, size in CAMERA_REGIONS:
            C.memset(self.buffers[base], 0, size)
        area, room, index = case['where']
        b = self.buffers
        b[0x810700][0], b[0x810700][1], b[0x810700][2] = area, room, index
        C.memmove(b[0x8101E0], case['camera'], 0x30)
        b[0x275BE0][0] = case['b275']
        C.cast(self.locate(0x8104E0), P(U32))[0] = case['pose']
        self.put(0x810350, case['player'][:4]); self.put(0x810370, case['player'][4:])
        self.script, self.calls = SeatScript(case['seed']), []
        n = self.native
        n.reset()
        self.bind(n.host)
        r = self.lib.em_script_host_001B0460(n.ref(), sx32(case['a0']))
        assert r == 0, (case['where'], r, hex(n.host.fault_address))
        return self.calls, {base: bytes(self.buffers[base]) for base, size in CAMERA_REGIONS}


def seat_part_job(cases):
    """Compare 001B0460 on a slice of cases; returns the branch outcomes."""
    elf, lib = CONTEXT['elf'], CONTEXT['lib']
    CONTEXT['seat_outcomes'] = set()
    image = seat_elf(elf, SYNTH)
    native = SeatNative(lib, image, Native(lib, elf))
    for case in cases:
        expected = seat_oracle(image, case)
        actual = native.run(case)
        assert actual[0] == expected[0], (case['where'], 'calls', actual[0], expected[0])
        for base in expected[1]:
            assert actual[1][base] == expected[1][base], (case['where'], hex(base), 'bytes differ')
    return CONTEXT['seat_outcomes']


# ======================================================================
# Part 2: captured RAM
# ======================================================================

OWNERS = {'roger': 0x7A8830, 'director': 0x7A93F0, 'trigger': 0x7AA2A0, 'elevator': 0x7AA880,
          'panel': 0x7AA590}


def captures():
    out = [(name, REF / f'{name}.bin') for name in ('opening_ee', 'handoff_ee', 'playable_ee')]
    out += [(p.parent.name, p) for p in sorted(ROUTE.glob('*/eeMemory.bin'))]
    return [(name, path) for name, path in out if path.is_file()]


def script_rates(ram):
    """The op15 turn rates (+0x24 / +0x34) of Roger's scripts in this RAM and
    the op01 kind 3/5 constant 0.06981317 (0x3D8EFA35, 001B94F0)."""
    rates = {0x3D8EFA35}
    for pc in range(0x8283D0, 0x828BD0, 64):
        if struct.unpack_from('<I', ram, pc)[0] & 0xFFF == 0x15:
            for off in (0x24, 0x34):
                word = struct.unpack_from('<I', ram, pc + off)[0]
                if word: rates.add(word)
    return sorted(rates)


def capture_case(item):
    name, path = item
    elf, lib = CONTEXT['elf'], CONTEXT['lib']
    ram = path.read_bytes()
    oracle = Oracle(elf, ram)
    native = Native(lib, elf)
    words = lambda address, n=3: tuple(struct.unpack_from('<I', ram, address + 4 * i)[0] for i in range(n))
    base = dict(actor=ram[PLAYER:PLAYER + 0x320], bc=ram[G_BC], c3c=ram[G_3C], f1=ram[G_F1])
    predicate = compare_pred(oracle, native, base, where=name)
    pad = compare_pad(oracle, native, ram[PAD:PAD + 0x2A], where=name)
    player_a0, player_b0 = words(PLAYER + 0xA0), words(PLAYER + 0xB0)
    player_yaw = struct.unpack_from('<I', ram, PLAYER + 0xC4)[0]
    rates = script_rates(ram)
    math = 0
    for owner in OWNERS.values():
        o_b0 = words(owner + 0xB0)
        o_yaw = struct.unpack_from('<I', ram, owner + 0xC4)[0]
        cases = [('side', (player_a0, o_b0, o_yaw)), ('side', (o_b0, player_a0, player_yaw)),
                 ('bearing', (o_b0, player_b0[0], player_b0[2])),
                 ('bearing', (player_b0, o_b0[0], o_b0[2]))]
        for kind, case in cases:
            compare_math(oracle, native, kind, case)
            math += 1
        # 001B12B0 from the owner's and the player's yaw toward the bearings
        for current, (obj, px, pz) in ((o_yaw, (o_b0, player_b0[0], player_b0[2])),
                                       (player_yaw, (player_b0, o_b0[0], o_b0[2]))):
            out = U32()
            native.reset()
            if native.lib.em_script_host_001B1240(native.ref(), vec(obj), px, pz, C.byref(out)) != 0:
                continue
            for rate in rates:
                compare_math(oracle, native, 'turn', (out.value, current, rate))
                math += 1
    # 001B0460 over the captured area/room/camera state (the tables are the
    # ELF's, which the captured RAM holds unchanged).
    seat = dict(where=(ram[0x810700], ram[0x810701], ram[0x810702]), a0=1, b275=ram[0x275BE0],
                pose=struct.unpack_from('<I', ram, 0x8104E0)[0], seed=len(name),
                player=list(words(0x810350, 4) + words(0x810370, 4)), camera=ram[0x8101E0:0x810210])
    if seat_record(elf, *seat['where']) is not None:
        expected = seat_oracle(elf, seat)
        actual = SeatNative(lib, elf, native).run(seat)
        assert actual == (expected[0], expected[1]), (name, '001B0460 differs')
        math += 1
    return name, predicate, pad, math


# ======================================================================
# Part 3: the AREA11 script host with these translations bound
# ======================================================================

BOUND = {BEARING: 'em_script_host_001B1240', TURN: 'em_script_host_001B12B0',
         SIDE: 'em_script_host_001B1380'}
HOST_SCENARIOS = ('roger 828990', 'roger 828810', 'roger 828A10', 'director 829E80')


def install_bound(A):
    """test_area_script_reference answers its math workers by scratch
    executions of the original (scratch_math); route 001B1240 / 001B12B0 /
    001B1380 to the native translations instead (the others unchanged)."""
    original = A.scratch_math

    def bound(elf, ram, spad, address, args, kinds, result):
        if address not in BOUND:
            return original(elf, ram, spad, address, args, kinds, result)
        native = CONTEXT['native'] or Native(CONTEXT['lib'], elf)
        CONTEXT['native'] = native
        native.reset()
        lib = native.lib
        CONTEXT['bound_calls'] += 1
        if address == BEARING:
            out = U32()
            r = lib.em_script_host_001B1240(native.ref(), vec(args[0]), args[1], args[2], C.byref(out))
        elif address == TURN:
            out = U32()
            r = lib.em_script_host_001B12B0(native.ref(), args[0], args[1], args[2], C.byref(out))
        else:
            out = I32()
            r = lib.em_script_host_001B1380(native.ref(), vec(args[0]), vec(args[1]), args[2],
                                            C.byref(out))
        assert r == 0, ('bound worker faulted', hex(address), hex(native.host.fault_address), args)
        return out.value & MASK if result == 'fo' else out.value
    A.scratch_math = bound


def host_scenario(case):
    CONTEXT['bound_calls'] = 0
    label, ticks, outcome, fault, _ = CONTEXT['A'].run_scenario(case)
    return label, ticks, outcome, CONTEXT['bound_calls']


def host_route(case):
    CONTEXT['bound_calls'] = 0
    label, frames, checks = CONTEXT['A'].capture_case(case)
    return label, frames, checks, CONTEXT['bound_calls']


def host_part(elf):
    import test_area_script_reference as A
    A.BUILD = OUT / 'area_script'
    ram = (REF / 'playable_ee.bin').read_bytes()
    A.CONTEXT.update(elf=elf, ram=ram, spad=bytes(0x4000), lib=A.build(),
                     synthetic=A.synthetic_arena()[1].ljust(A.SYNTHETIC[1] - A.SYNTHETIC[0], b'\0'))
    install_bound(A)
    CONTEXT['A'] = A
    return A


# ======================================================================
# Part 4: the script export and the loader
# ======================================================================

def export_part(lib, parallel_results=None):
    import export_area11_scripts as X
    import export_elevator
    overlay_path = DECOMP / 'extract/OVERLAY/AREA11.BIN'
    overlay = overlay_path.read_bytes()
    scripts, quads, report = X.export(overlay)
    d = OUT / 'export'
    d.mkdir(parents=True, exist_ok=True)
    paths = {'scripts': d / 'scripts.emsc', 'quads': d / 'director_quads.emsc',
             'elevator': d / 'elevator.emsc', 'roger': d / 'programs.emsc'}
    paths['scripts'].write_bytes(scripts)
    paths['quads'].write_bytes(quads)
    paths['elevator'].write_bytes(export_elevator.export(overlay))
    lo, hi = 0x8283D0, 0x828BD0      # tools/export_roger_resources.py programs.emsc
    paths['roger'].write_bytes(struct.pack('<4s4I', b'EMSC', 1, lo, lo, hi - lo) +
                               overlay[lo - 0x823500:hi - 0x823500])
    # The committed assets, when present, must equal a fresh export.
    for name, blob in (('scripts.emsc', scripts), ('director_quads.emsc', quads)):
        asset = X.OUT / name
        if asset.is_file():
            assert asset.read_bytes() == blob, ('stale asset, re-run tools/export_area11_scripts.py', asset)
    s = Scripts()
    enc = lambda p: str(p).encode()
    assert lib.em_area11_scripts_load(C.byref(s), enc(paths['scripts']), enc(paths['quads']),
                                      enc(paths['elevator']), enc(paths['roger'])) == 0
    count = C.c_size_t()
    entries = lib.em_area11_scripts_entries(C.byref(count))
    entries = [entries[i] for i in range(count.value)]
    ranges = {0: (0x8292C0, 0x82A3C0), 1: (0x82A750, 0x82AB10), 2: (0x8283D0, 0x828BD0)}
    images = {}
    for i, (base, end) in ranges.items():
        img = s.image[i]
        assert (img.base, img.entry, img.length) == (base, base, end - base), (i, img.base, img.length)
        images[i] = bytes(img.bytes[:img.length])
    for entry in entries:
        found = lib.em_area11_scripts_image(C.byref(s), entry)
        assert found, hex(entry)
        assert found.contents.base <= entry < found.contents.base + found.contents.length, hex(entry)
    assert not lib.em_area11_scripts_image(C.byref(s), 0x82A3C0)
    quad_ptrs = (VP * 3)()
    assert lib.em_area11_scripts_director_quads(C.byref(s), C.byref(quad_ptrs)) == 0
    quad_words = [struct.unpack('<16I', C.string_at(quad_ptrs[q], 64)) for q in range(3)]
    # Byte for byte against the captured RAM. The scripts range must equal
    # every capture taken before those scripts ran. Elsewhere, and after they
    # ran, only fields the game writes in place may differ (checked per
    # record op and word offset):
    #   scripts:  op00 +0x10, the tick counter 001B8FC0 keeps in the record;
    #   elevator: op00/op01 +0x34, the heights 00827B10 patches;
    #   Roger:    op15 +0x10 (001B6FA0 saves the owner yaw), op0A +0x30..+0x3F
    #             (001B9A00 sub 5 stores the player bone translation).
    allowed = {0: {(0x00, 0x10)}, 1: {(0x00, 0x34), (0x01, 0x34)},
               2: {(0x15, 0x10), (0x0A, 0x30), (0x0A, 0x34), (0x0A, 0x38), (0x0A, 0x3C)}}
    checked, in_place = [], {}
    for name, path in captures():
        ram = path.read_bytes()
        for i, (base, end) in ranges.items():
            diff = [a for a in range(base, end) if ram[a] != images[i][a - base]]
            before = name in ('opening_ee', 'handoff_ee', 'playable_ee') or name < '07'
            if i == 0 and before:
                assert not diff, (name, 'scripts differ from the captured RAM', [hex(a) for a in diff[:8]])
            for a in diff:
                record = a - (a - base) % 64
                op = struct.unpack_from('<I', images[i], record - base)[0] & 0xFFF
                assert (op, (a - record) & ~3) in allowed[i], (name, hex(a), 'unexpected difference')
            in_place[i] = in_place.get(i, 0) + len(diff)
        for q in range(3):
            assert quad_words[q] == struct.unpack_from('<16I', ram, 0x82ABE0 + 0x40 * q), (name, 'quad', q)
        checked.append(name)
    route_diffs = in_place
    # The loader's refusals.
    bad = d / 'bad.emsc'
    refusals = 0
    for label, blob in (
            ('wrong base', struct.pack('<4s4I', b'EMSC', 1, 0x8292C4, 0x8292C4, len(scripts) - 20) + scripts[20:]),
            ('truncated', scripts[:-1]),
            ('no stop record', clear_stops(scripts)),
            ('wrong magic', b'EMSX' + scripts[4:])):
        bad.write_bytes(blob)
        t = Scripts()
        assert lib.em_area11_scripts_load(C.byref(t), enc(bad), enc(paths['quads']), None, None) == -1, label
        assert not t.image[0].bytes, label
        refusals += 1
    bad.write_bytes(quads[:-4])
    assert lib.em_area11_scripts_load(C.byref(Scripts()), enc(paths['scripts']), enc(bad), None, None) == -1
    refusals += 1
    only = Scripts()
    assert lib.em_area11_scripts_load(C.byref(only), enc(paths['scripts']), enc(paths['quads']), None, None) == 0
    assert not lib.em_area11_scripts_image(C.byref(only), 0x82A990)
    assert lib.em_area11_scripts_image(C.byref(only), 0x829CC0)
    lib.em_area11_scripts_free(C.byref(only))
    lib.em_area11_scripts_free(C.byref(s))
    return len(entries), checked, route_diffs, refusals, report


def clear_stops(scripts):
    """The scripts image with every stop flag cleared (header kept)."""
    data = bytearray(scripts)
    for off in range(20, len(data), 64):
        word = struct.unpack_from('<I', data, off)[0]
        struct.pack_into('<I', data, off, word & 0x7FFFFFFF)
    return bytes(data)


# ======================================================================
# Part 5: fail-stop
# ======================================================================

def fault_part(elf, lib):
    native = Native(lib, elf)
    out, fout = I32(), U32()
    checks = 0
    rng = random.Random(7)
    record = bytes(rng.getrandbits(8) for _ in range(0x320))

    def pred_with(field, expect, actor=PLAYER):
        nonlocal checks
        native.reset()
        C.memmove(native.actor.bytes, record, 0x320)
        native.flags['bc'].value = 1
        if field: setattr(native.host.world, field, None)
        r = lib.em_script_host_00182BF0(native.ref(), actor, C.byref(out))
        assert (r, native.host.fault_address) == (-1, expect), (field, r, hex(native.host.fault_address))
        assert bytes(native.actor.bytes) == record and native.flags['bc'].value == 1, (field, 'wrote')
        checks += 1
    pred_with('player', 0x182BF0)
    pred_with('d8106BC', 0x182C00)
    pred_with('d81083C', 0x182C10)
    pred_with('d8106F1', 0x182C90)
    pred_with(None, 0x182BF0, actor=0x7A8830)

    def pad_with(change, expect, writes):
        nonlocal checks
        native.reset()
        block = bytearray(rng.getrandbits(8) for _ in range(0x40))
        block[0x12] = block[0x16] = 1
        C.memmove(native.pad, bytes(block), 0x40)
        address = change(native)
        r = lib.em_script_host_001B6250(native.ref(), address)
        assert (r, native.host.fault_address) == (-1, expect), (r, hex(native.host.fault_address))
        assert (bytes(native.pad) != bytes(block)) == writes
        checks += 1
    pad_with(lambda n: setattr(n.host.world, 'd810E40', None) or PAD, 0x1B6250, False)
    pad_with(lambda n: PAD + 4, 0x1B6250, False)
    pad_with(lambda n: setattr(n.host.callees, 'w_00111018', PAD_FN()) or PAD, 0x111018, False)
    pad_with(lambda n: setattr(n, 'fail_on', 0) or PAD, 0x111018, True)   # the stores stay

    for missing, expect in (('w_001AEDE0', 0x1AEDE0), ('w_001FAD70', 0x1FAD70)):
        native.reset()
        setattr(native.host.callees, missing, FADE_FN() if missing == 'w_001AEDE0' else STREAM_FN())
        assert lib.em_script_host_001B0C00(native.ref(), 8) == -1
        assert native.host.fault_address == expect and native.calls == [], (missing, native.calls)
        checks += 1
    for fail_on, expect, made in ((0, 0x1AEDE0, 1), (2, 0x1FAD70, 3)):
        native.reset(); native.fail_on = fail_on
        assert lib.em_script_host_001B0C00(native.ref(), 8) == -1
        assert native.host.fault_address == expect and len(native.calls) == made
        checks += 1

    a, b = vec((F(10.0), 0, F(20.0))), vec((F(30.0), 0, F(5.0)))
    for field, expect in (('sdk_tables', 0x11E620), ('sdk_world', 0x11E648)):
        native.reset(); setattr(native.host.world, field, None)
        assert lib.em_script_host_001B1240(native.ref(), a, F(1.0), F(2.0), C.byref(fout)) == -1
        assert native.host.fault_address == expect, (field, hex(native.host.fault_address))
        native.reset(); setattr(native.host.world, field, None)
        assert lib.em_script_host_001B1380(native.ref(), a, b, 0, C.byref(out)) == -1
        assert native.host.fault_address == expect
        checks += 2
    native.reset()
    assert lib.em_script_host_001B12B0(native.ref(), 0, 0, 0, None) == -1
    assert native.host.fault_address == 0x1B12B0
    # the first fault is kept
    assert lib.em_script_host_001B1240(native.ref(), None, 0, 0, C.byref(fout)) == -1
    assert native.host.fault_address == 0x1B12B0
    native.reset()
    assert lib.em_script_host_001B1240(native.ref(), a, F(1.0), F(2.0), None) == -1
    assert native.host.fault_address == 0x1B1240
    checks += 3
    # float-typed adapters equal the bit routines on a sample
    host = C.cast(native.ref(), VP)
    fa, fb = (C.c_float * 4)(10.0, 0.0, 20.0, 1.0), (C.c_float * 4)(30.0, 0.0, 5.0, 1.0)
    native.reset()
    got = C.c_float()
    assert lib.em_script_host_w_001B1240(host, fa, 1.0, 2.0, C.byref(got)) == 0
    assert lib.em_script_host_001B1240(native.ref(), a, F(1.0), F(2.0), C.byref(fout)) == 0
    assert F(got.value) == fout.value
    assert lib.em_script_host_w_001B12B0(host, 1.0, -2.0, 0.1, C.byref(got)) == 0
    assert lib.em_script_host_001B12B0(native.ref(), F(1.0), F(-2.0), F(0.1), C.byref(fout)) == 0
    assert F(got.value) == fout.value
    side = I32()
    assert lib.em_script_host_w_001B1380(host, fa, fb, 0.5, C.byref(side)) == 0
    assert lib.em_script_host_001B1380(native.ref(), a, b, F(0.5), C.byref(out)) == 0
    assert side.value == out.value
    native.reset()
    native.pad[0x12] = native.pad[0x16] = 1
    assert lib.em_script_host_owner_001B6250(host) == 0 and native.calls[0][3] == PAD + 0x18
    checks += 4
    # 001B0460: every pointer and worker is checked before the first write
    seat = SeatNative(lib, elf, native)
    case = dict(where=(0xB, 0, 0), a0=1, b275=0, pose=0, seed=1, player=[0] * 8, camera=bytes(0x30))
    for name, address in (('d8101E6', 0x8101E6), ('spad3400', 0x70003400), ('elf', 0x24D650)):
        seat.run(case)
        before = {base: bytes(buf) for base, buf in seat.buffers.items()}
        seat.calls = []
        native.reset(); seat.bind(native.host)
        setattr(native.host.world, name, None)
        assert lib.em_script_host_001B0460(native.ref(), 1) == -1
        assert native.host.fault_address == address, (name, hex(native.host.fault_address))
        assert {base: bytes(buf) for base, buf in seat.buffers.items()} == before and not seat.calls
        checks += 1
    for name, address in (('w_001028B8', 0x1028B8), ('w_001DD980', 0x1DD980)):
        native.reset(); seat.bind(native.host); seat.calls = []
        setattr(native.host.callees, name, SCRIPT_FN[name]())
        assert lib.em_script_host_001B0460(native.ref(), 1) == -1
        assert native.host.fault_address == address and not seat.calls, name
        checks += 1
    # a room whose record lies outside the ELF faults at the read
    native.reset(); seat.bind(native.host)
    C.cast(seat.locate(0x810700), P(U8))[0] = 0x18      # D_0024D650[0x18] is not a pointer
    assert lib.em_script_host_001B0460(native.ref(), 1) == -1
    assert native.host.fault_address == 0x1B04BC, hex(native.host.fault_address)
    checks += 1
    return checks


# ======================================================================

CONTEXT = {'native': None, 'bound_calls': 0}


def job(item):
    kind, payload = item
    if kind == 'unit': return kind, unit_job(payload)
    if kind == 'seat': return kind, seat_part_job(payload)
    if kind == 'capture': return kind, capture_case(payload)
    if kind == 'host': return kind, host_scenario(payload)
    return kind, host_route(payload)


def main():
    import time
    start = time.time()
    elf = read_elf()
    lib = build_native()
    CONTEXT.update(elf=elf, lib=lib)
    A = host_part(elf)
    units = unit_cases(elf)
    slices = 8
    items = [('unit', units[i::slices]) for i in range(slices)]
    seats = seat_cases(4, rm.pick(3000, 120), elf)
    items += [('seat', seats[i::2]) for i in range(2)]
    items += [('capture', c) for c in captures()]
    scenarios = [c for c in A.scenarios() if c[0].startswith(HOST_SCENARIOS)]
    items += [('host', c) for c in scenarios]
    beat10 = [c for c in A.ROUTE_CASES if c[1] == '10_cage_roof_roger']
    items += [('route', c) for c in beat10]
    cost = {'route': 4, 'host': 3, 'unit': 2, 'seat': 2, 'capture': 1}
    results = rm.parallel_map(job, items, cost=lambda item: cost[item[0]])
    by = lambda kind: [r for k, r in results if k == kind]

    tags, branches = unit_summary(elf, by('unit') + [({}, seen) for seen in by('seat')])
    count = lambda kind: sum(n for (k, _), n in tags.items() if k == kind)
    print(f'unit: 00182BF0 {count("pred"):,} records ({tags.get(("pred", "take"), 0):,} take the frame); '
          f'001B12B0 {count("turn"):,}, 001B1240 {count("bearing"):,}, 001B1380 {count("side"):,} '
          'argument sets (' + ', '.join(f'{k} {t} {n}' for (k, t), n in sorted(tags.items())
                                        if k in ('turn', 'bearing', 'side')) +
          f'); 001B6250 {count("pad")} blocks ({tags.get(("pad", "write"), 0)} actuator writes); '
          f'001B0C00 {count("fade")} arguments; 001B0460 {len(seats):,} camera records/states; '
          f'{branches} conditional branches, both outcomes each')
    captured = by('capture')
    assert len(captured) >= 18, ('captures missing', [c[0] for c in captured])
    print(f'captured RAM: {len(captured)} captures; 00182BF0 on each player record '
          f'(results {sorted(set(p for _, p, _, _ in captured))}), 001B6250 on each pad block, '
          f'{sum(m for *_, m in captured):,} bearing/side/turn argument sets from the owner and '
          'player vectors and the scripts\' turn rates, 001B0460 on each area/room/camera state')
    hosted = by('host')
    for label, ticks, outcome, n in hosted:
        assert n > 0 or 'skip' in label, (label, 'never reached the translations')
    print(f'script host lockstep: {len(hosted)} scenarios ({", ".join(sorted(h[0] for h in hosted))}), '
          f'{sum(h[1] for h in hosted):,} ticks, {sum(h[3] for h in hosted):,} calls answered by '
          'these translations')
    route = by('route')
    assert len(route) == 1
    for label, frames, checks, n in route:
        assert n > 0, (label, 'the route never reached the translations')
        print(f'route capture {label}: {frames:,} frames, 0 differences, {n:,} calls answered by '
              'these translations; compared ' + ', '.join(f'{k} {v:,}' for k, v in sorted(checks.items())))
    entries, checked, in_place, refusals, report = export_part(lib)
    print(f'export: scripts 0x8292C0..0x82A3C0 ({sum(e["records"] for e in report["scripts"]["entries"].values())}'
          f' records, 5 entries) and 3 director quads byte-identical to the captured RAM in '
          f'{len(checked)} captures (elevator and Roger images: only in-place fields differ, '
          f'{in_place.get(1, 0) + in_place.get(2, 0):,} bytes; scripts after they ran: op00 tick '
          f'counters, {in_place.get(0, 0)} bytes); loader: {entries} entries resolved, '
          f'{refusals} malformed images refused')
    checks = fault_part(elf, lib)
    print(f'fail-stop: {checks} checks')
    rm.banner(f'{count("pred"):,} predicate records',
              f'{count("turn") + count("bearing") + count("side"):,} math argument sets',
              f'{len(captured)} captures', f'{len(hosted)} host scenarios', f'{len(route)} route beat')
    print(f'script host workers match the original instructions ({time.time() - start:.1f} s)')


if __name__ == '__main__':
    sys.exit(main())
