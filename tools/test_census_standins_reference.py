#!/usr/bin/env python3
"""Execute the originals behind the census stand-ins and compare
src/game/em_census_standins.c. docs/CENSUS_STANDINS.md.

The user's pinned ELF and the captured EE RAM (../Extermination/build/
startup-reference and the s87 route beats 00..14) supply every instruction,
table, bank and record; none are embedded here. The report in build/ holds
addresses, counts and differences only.

A. 00102CD0 (the look-at 0018C0D0 commits; live since census L13..L16).
   Executed with its SDK leaves 001029C0 / 00102718 / 00102760 / 00102918 /
   001027E0 (COP1 / VU0 through tools/ee_float_model.py). Inputs: every
   capture's own D_700038C0 / D_700038A0 / D_008105F0, random and special
   vectors. Every output word, and em_cs_view_to_native of every result as
   the exact sign flip of the y and z lanes (the renderer's view).
B. 0020CCB0 (the BATTERY page marker; stand-in em_battery_ui.c). float_to_int
   001281C0 executes original instructions on both sides; 00207F80 is
   hooked and compared. The native call replayed through the ORIGINAL
   00207F80 must build a packet that the original built in the capture's
   own packet buffers (panel: page byte 6 set; 03 / 04: clear).
C. 0021BAE0 (the END_PROJECTION event; stand-in: a flag). block_copy
   00121870 executes original instructions on both sides; every context
   byte compared, on random blocks and every capture's own context.
D. The mode-3 / mode-4 presenters 001FCB90, 001FCF60, 001FCF90 (with
   001FE660) and 001FD0E0 over the captured banks *D_0028A498 /
   *D_0028A49C / *D_0028A4EC (asserted identical in every capture), plus a
   synthetic cue bank of this file's own ASCII. 001FE070 / 001FE530 /
   001FC770 / 001FE460 / 001FE480 / 001FE4D0 and the memset execute; the
   glyph advance 001CBE10 (answers from the executed original) and the run
   draw 001FC7B0 are hooked (tools/test_message_draw_reference.py). For
   001FD0E0, 001FC9B0 executes original instructions on both sides and
   001FDDB0 is hooked with scripted results; every call (slot table, count,
   glyph line, block) and every block / global byte is compared, and both
   outcomes of every two-way branch of 001FD0E0 are asserted reached.
E. Fail-stop: NULL workers, containers and banks too small, ranges outside
   the block.

Default run (~10 s): every case class, the boundary cases and a sample of
the real lines. EM_TEST_FULL=1: every real line / group / page and a longer
random sweep for 00102CD0.
"""
import ctypes as C
import json
import math
import random
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode as RM  # noqa: E402
import test_message_draw_reference as md  # noqa: E402
import test_render_verify_rest_reference as rvr  # noqa: E402
from test_player_fall_reference import FallEE, nested_bits  # noqa: E402
from test_player_slide_reference import EE, read_elf, s32, STACK_TOP  # noqa: E402

DECOMP = ROOT.parent / 'Extermination'
REF = DECOMP / 'build/startup-reference'
ROUTE = DECOMP / 'build/s87/route'
OUT = ROOT / 'build' / 'census_standins_reference'
M32 = 0xFFFFFFFF

SIZES = {0x102CD0: 0xB0, 0x1FCB90: 0x38, 0x1FCF60: 0x2C, 0x1FCF90: 0x148, 0x1FE660: 0x50,
         0x1FD0E0: 0x38C, 0x20CCB0: 0x8C, 0x21BAE0: 0x1C}
LOOKAT, HELP, TITLE, LIST, COUNT, CUE, MARKER, RESTORE = \
    0x102CD0, 0x1FCB90, 0x1FCF60, 0x1FCF90, 0x1FE660, 0x1FD0E0, 0x20CCB0, 0x21BAE0
F2I, RECT, BLOCK_COPY = 0x1281C0, 0x207F80, 0x121870
RESET, LINE_DRAW, FE070, FE530, FC770, FE460, FE480, FE4D0, MEMSET = \
    0x1FC9B0, 0x1FDDB0, 0x1FE070, 0x1FE530, 0x1FC770, 0x1FE460, 0x1FE480, 0x1FE4D0, 0x121A28
# Direct call targets of each routine: (executed on the original side, hooked there).
CALLEES = {LOOKAT: ({0x1029C0, 0x102718, 0x102760, 0x102918, 0x1027E0}, set()),
           HELP: ({FE070}, set()), TITLE: ({FE070}, set()),
           LIST: ({FE460, FE480, FE530, COUNT, FC770}, set()), COUNT: ({FE530}, set()),
           CUE: ({RESET, MEMSET, FE480, FE4D0, FE070}, {LINE_DRAW}),
           MARKER: ({F2I}, {RECT}), RESTORE: ({BLOCK_COPY}, set())}
CTX_PTR, BLOCK, D820EC0 = 0x275670, 0x2821B0, 0x820EC0
HELP_PTR, REC_PTR, CUE_PTR = 0x28A498, 0x28A49C, 0x28A4EC
CFG_CF0, CFG_C90, D264DB0 = 0x264CF0, 0x264C90, 0x264DB0
SCRATCH, SYNTH = 0x01F00000, 0x01E00000


def u32(buf, at):
    return struct.unpack_from('<I', buf, at)[0]


def captures():
    """(label, eeMemory, scratchpad or None) for the reference captures that
    hold the level and every in-scope route beat."""
    out = []
    for name in ('panel', 'status-hub', 'roger-encounter'):
        out.append((name, REF / name / 'eeMemory.bin', None))
    for beat in sorted(p.name for p in ROUTE.iterdir() if p.is_dir()):
        if RM.in_scope_beat(beat):
            out.append((beat, ROUTE / beat / 'eeMemory.bin', ROUTE / beat / 'scratchpad.bin'))
    return out


def check_callees(elf):
    e = EE(elf)
    for routine, size in SIZES.items():
        targets = rvr.call_targets(e, routine, size)
        executed, hooked = CALLEES[routine]
        assert targets == executed | hooked, (hex(routine), sorted(map(hex, targets)))
    return len(SIZES)


# ======================================================================
# Native side
# ======================================================================

VP, I32, U32 = C.c_void_p, C.c_int32, C.c_uint32
F2I_FN = C.CFUNCTYPE(C.c_int, VP, U32, C.POINTER(I32))
RECT_FN = C.CFUNCTYPE(C.c_int, VP, I32, I32, I32, I32, I32, U32)
RESET_FN = C.CFUNCTYPE(C.c_int, VP)
LINE_FN = C.CFUNCTYPE(C.c_int, VP, VP, I32, VP, VP, C.POINTER(I32))
COPY_FN = C.CFUNCTYPE(C.c_int, VP, VP, U32, U32, I32)


class RectWorkers(C.Structure):
    _fields_ = [('context', VP), ('float_to_int', F2I_FN), ('rectangle', RECT_FN)]


class SulWorkers(C.Structure):
    """EmSulWorkers: only block_copy is bound here (the rest stay NULL)."""
    _fields_ = [('context', VP)] + [(n, VP) for n in (
        'blend', 'sprite', 'text_fixed', 'text_proportional', 'text_width', 'health', 'battery',
        'float_to_int', 'format', 'copy', 'sound')] + [('block_copy', COPY_FN)] + [(n, VP) for n in (
            'free_actor', 'model_bind', 'place', 'publish', 'method')]


class CsData(C.Structure):
    _fields_ = [('help', md.Bank), ('records', md.Bank), ('cue', md.Bank), ('cue_address', U32),
                ('config_264CF0', C.POINTER(md.Config)), ('config_264C90', C.POINTER(md.Config)),
                ('config_264C90_address', U32), ('d264DB0', U32 * 6)]


class Mode3(C.Structure):
    _fields_ = [('context', VP), ('reset', RESET_FN), ('line_draw', LINE_FN)]


class Presenters(C.Structure):
    _fields_ = [('draw', C.POINTER(md.Draw)), ('data', C.POINTER(CsData)), ('mode3', Mode3),
                ('d820EC0', U32 * 4), ('d282228', C.POINTER(I32)), ('frame_address', U32),
                ('list', C.c_uint8 * 0x280), ('frame', C.c_uint8 * 0x798), ('fault', C.c_char_p)]


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    ext = 'dylib' if sys.platform == 'darwin' else 'so'
    lib = OUT / f'census_standins.{ext}'
    shared = '-dynamiclib' if sys.platform == 'darwin' else '-shared'
    sources = ['src/game/em_census_standins.c', 'src/game/em_effect_original.c',
               'src/game/em_owner_services_original.c', 'src/game/em_render_verify_rest.c',
               'src/game/em_sdk_soft_float.c', 'src/game/em_message_draw_original.c']
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-fPIC', shared,
                    '-Isrc'] + sources + ['-o', str(lib)], cwd=ROOT, check=True)
    n = C.CDLL(str(lib))
    W16, W4 = U32 * 16, U32 * 4
    n.em_cs_00102CD0.argtypes = [W16, W4, W4, W4]
    n.em_cs_view_to_native.argtypes = [C.c_float * 16, W16]
    n.em_cs_view_to_native.restype = None
    n.em_cs_0020CCB0.argtypes = [C.POINTER(RectWorkers), VP, C.c_size_t]
    n.em_cs_0021BAE0.argtypes = [C.POINTER(SulWorkers), VP, C.c_size_t, I32]
    P = C.POINTER(Presenters)
    n.em_cs_presenters_init.argtypes = [P, C.POINTER(md.Draw), C.POINTER(CsData), C.POINTER(Mode3),
                                        C.POINTER(I32)]
    n.em_cs_001FCB90.argtypes = [P, I32, I32, I32, I32, C.POINTER(I32)]
    n.em_cs_001FCF60.argtypes = [P, I32, I32, I32, C.POINTER(I32)]
    n.em_cs_001FCF90.argtypes = [P, I32, I32, C.POINTER(I32)]
    n.em_cs_001FE660.argtypes = [P, C.POINTER(md.Bank), U32, C.POINTER(I32)]
    n.em_cs_001FD0E0.argtypes = [P, VP]
    n.em_cs_worker_help_draw.argtypes = [P, C.c_int, C.c_int, I32, U32]
    n.em_message_draw_init.argtypes = [C.POINTER(md.Draw), C.POINTER(md.Data), C.POINTER(md.Workers)]
    return n, None


# ======================================================================
# A. 00102CD0
# ======================================================================

OUT_M, POS, FWD, UP = SCRATCH + 0x100, SCRATCH, SCRATCH + 0x10, SCRATCH + 0x20


def lookat_original(e, pos, fwd, up):
    for base, words in ((POS, pos), (FWD, fwd), (UP, up)):
        for i, w in enumerate(words):
            e.save(base + 4 * i, w)
    e.write(OUT_M, b'\xA5' * 64)
    e.r[29] = STACK_TOP
    try:
        e.call(LOOKAT, (OUT_M, POS, FWD, UP))
    except AssertionError as refused:
        return None, refused
    return [e.load(OUT_M + 4 * i) for i in range(16)], None


def lookat_native(n, pos, fwd, up):
    out = (U32 * 16)(*([0xA5A5A5A5] * 16))
    r = n.em_cs_00102CD0(out, (U32 * 4)(*pos), (U32 * 4)(*fwd), (U32 * 4)(*up))
    return (list(out) if r == 0 else None), r


def f2b(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def b2f(word):
    return struct.unpack('<f', struct.pack('<I', word))[0]


def lookat_cases(rng, count):
    cases = []
    for _ in range(count):
        a, b = rng.uniform(-3.2, 3.2), rng.uniform(-1.5, 1.5)
        f = [math.cos(b) * math.sin(a), math.sin(b), math.cos(b) * math.cos(a)]
        pos = [rng.uniform(-600, 600) for _ in range(3)]
        up = [0.0, -1.0, 0.0] if rng.random() < 0.7 else [rng.uniform(-1, 1) for _ in range(3)]
        cases.append(([f2b(v) for v in pos] + [f2b(rng.uniform(-2, 2))],
                      [f2b(v) for v in f] + [f2b(1.0)], [f2b(v) for v in up] + [0]))
    unit_y = [0, 0xBF800000, 0, 0]
    specials = [
        ([0, 0, 0, 0], [0, 0, 0x3F800000, 0], unit_y),                       # straight ahead
        ([0x80000000] * 4, [0, 0x3F800000, 0, 0], unit_y),                   # forward parallel to up
        ([0x00000001, 0x80000001, 0x007FFFFF, 0], [0x3F3504F3, 0, 0x3F3504F3, 0], unit_y),  # denormals
        ([0x7F000000, 0xFF000000, 0x7F7FFFFF, 0], [0x3F800000, 0, 0, 0], unit_y),        # huge
        ([f2b(10.0)] * 3 + [0], [f2b(3.0), f2b(4.0), f2b(12.0), 0], unit_y),               # not unit
        ([0] * 4, [0] * 4, unit_y),                                           # zero forward
    ]
    return specials + cases


def section_a(elf, n, s, rng):
    e = rvr.RvrEE(elf)
    stats = {'cases': 0, 'refused_both': 0, 'captures': 0, 'captured_view_equal': 0}
    items = lookat_cases(rng, RM.pick(4000, 150))
    for label, ee_path, sp_path in captures():
        if sp_path is None or not sp_path.exists():
            continue
        ram, spad = ee_path.read_bytes(), sp_path.read_bytes()
        pos = [u32(spad, 0x38C0 + 4 * i) for i in range(4)]
        fwd = [u32(spad, 0x38A0 + 4 * i) for i in range(4)]
        up = [u32(ram, 0x8105F0 + 4 * i) for i in range(4)]
        want, refused = lookat_original(e, pos, fwd, up)
        got, r = lookat_native(n, pos, fwd, up)
        assert refused is None and got == want, ('capture', label, refused, r)
        stats['captures'] += 1
        # The capture's own committed view D_00810610: the scratch inputs
        # are still the last commit's in every beat, so the translation
        # reproduces the matrix the original left in RAM.
        assert want == [u32(ram, 0x810610 + 4 * i) for i in range(16)], ('D_00810610', label)
        stats['captured_view_equal'] += 1
        items.append((pos, fwd, up))
    for pos, fwd, up in items:
        want, refused = lookat_original(e, pos, fwd, up)
        got, r = lookat_native(n, pos, fwd, up)
        if refused is not None:
            # The oracle's measured model refuses the form; the native leaves
            # may still produce a value, so nothing is compared.
            stats['refused_both'] += 1
            continue
        assert got == want, ('00102CD0', [hex(w) for w in pos + fwd + up],
                             [(i, hex(a), hex(b)) for i, (a, b) in enumerate(zip(got or [], want)) if a != b])
        stats['cases'] += 1
        # The renderer's view (em_cs_view_to_native): the original with the
        # y and z lane of every row negated, a sign-bit flip and nothing else.
        native = (C.c_float * 16)()
        n.em_cs_view_to_native(native, (U32 * 16)(*want))
        flipped = [w ^ 0x80000000 if i % 4 in (1, 2) else w for i, w in enumerate(want)]
        assert [struct.unpack('<I', struct.pack('<f', v))[0] for v in native] == flipped, \
            ('em_cs_view_to_native is not the sign flip', [hex(w) for w in pos + fwd + up])
        stats['native_view_flip'] = stats.get('native_view_flip', 0) + 1
    return stats


# ======================================================================
# B. 0020CCB0
# ======================================================================

class F2IValues:
    """float_to_int executed from original instructions, cached."""

    def __init__(self, elf):
        self.e = FallEE(elf)
        self.cache = {}

    def __call__(self, bits):
        if bits not in self.cache:
            v0, _ = nested_bits(self.e, F2I, (), (bits,))
            self.cache[bits] = s32(v0)
        return self.cache[bits]


def marker_original(elf, page6, values):
    e = FallEE(elf)
    log = []

    def f2i(ee):
        bits = ee.f[12] & M32
        log.append(('f2i', bits))
        ee.ret_int(values(bits))

    def rect(ee):
        log.append(('rect', s32(ee.r[4]), s32(ee.r[5]), s32(ee.r[6]), s32(ee.r[7]), s32(ee.r[8]),
                    ee.r[9] & M32))
    e.hooks[F2I], e.hooks[RECT] = f2i, rect
    e.write(SCRATCH, bytes(6) + bytes([page6]) + bytes(9))
    e.call(MARKER, (SCRATCH,))
    return log


def marker_native(n, page6, values, missing=None):
    log = []

    def f2i(_, bits, out):
        log.append(('f2i', bits))
        out[0] = values(bits)
        return 0

    def rect(_, slot, x0, y0, x1, y1, rgba):
        log.append(('rect', slot, x0, y0, x1, y1, rgba))
        return 0
    w = RectWorkers(None, F2I_FN(f2i), RECT_FN(rect))
    if missing:
        setattr(w, missing, type(getattr(w, missing))())
    page = (C.c_uint8 * 16)(*(bytes(6) + bytes([page6]) + bytes(9)))
    r = n.em_cs_0020CCB0(C.byref(w), page, 16)
    return r, log


def replay_packet(elf, ram, call):
    """The native call's arguments through the ORIGINAL 00207F80 into the
    capture's render context (slot 1 cursor moved to scratch): the packet."""
    e = EE(elf, ram)
    ctx = u32(ram, CTX_PTR)
    e.save(ctx + 0x14, SCRATCH)
    e.write(SCRATCH, bytes(0x60))
    e.call(RECT, call[1:6] + (call[6],))
    return e.read(SCRATCH, 0x50)


def section_b(elf, n):
    values = F2IValues(elf)
    stats = {'page_bytes': 0, 'capture_packets': {}}
    calls = {}
    for page6 in RM.select(range(256), 12, 0x20CCB0, keep=lambda i, v: v in (0, 1, 0x80, 0xFF)):
        want = marker_original(elf, page6, values)
        r, got = marker_native(n, page6, values)
        assert r == 0 and got == want, ('0020CCB0', page6, got, want)
        calls[page6 != 0] = got[-1]
        stats['page_bytes'] += 1
    # Capture evidence: the packet the translation's call builds through the
    # original 00207F80 is one the original built in that capture.
    expected = {'panel': True, '03_panel_power': False, '04_elevator_ride': False}
    for label, path, _ in captures():
        ram = path.read_bytes()
        found = []
        for selected, call in sorted(calls.items()):
            packet = replay_packet(elf, ram, call)
            body = packet[0x10:0x50]
            if ram.find(body) >= 0:
                found.append(selected)
        stats['capture_packets'][label] = found
        if label in expected:
            assert found == [expected[label]], ('packet not in the capture', label, found)
    # Fail-stop: a missing worker calls nothing.
    for missing in ('float_to_int', 'rectangle'):
        r, log = marker_native(n, 0, values, missing)
        assert r == -1 and log == [], ('0020CCB0 missing', missing)
    return stats


# ======================================================================
# C. 0021BAE0
# ======================================================================

CTX = 0x01D00000


class CopyValues:
    """block_copy executed from original instructions over a context image."""

    def __init__(self, elf):
        self.e = EE(elf)

    def run(self, context, dst, src, count):
        self.e.write(CTX, bytes(context))
        nested_bits(self.e, BLOCK_COPY, (CTX + dst, CTX + src, count))
        return self.e.read(CTX, len(context))


def restore_original(elf, ram, context, slot):
    e = EE(elf, ram)
    log = []
    e.save(CTX_PTR, CTX)
    e.write(CTX, bytes(context))

    def copy(ee):
        log.append(('copy', (ee.r[4] - CTX) & M32, (ee.r[5] - CTX) & M32, s32(ee.r[6])))
        hook = ee.hooks.pop(BLOCK_COPY)
        nested_bits(ee, BLOCK_COPY, (ee.r[4], ee.r[5], ee.r[6]))
        ee.hooks[BLOCK_COPY] = hook
    e.hooks[BLOCK_COPY] = copy
    e.call(RESTORE, (slot,))
    return e.read(CTX, len(context)), log


def restore_native(n, copier, context, slot, missing=False):
    buf = (C.c_uint8 * len(context))(*context)
    log = []

    def copy(_, block, dst, src, count):
        log.append(('copy', dst, src, count))
        out = copier.run(bytes(buf), dst, src, count)
        C.memmove(buf, out, len(out))
        return 0
    w = SulWorkers()
    if not missing:
        w.block_copy = COPY_FN(copy)
    r = n.em_cs_0021BAE0(C.byref(w), buf, len(context), slot)
    return r, bytes(buf), log


def section_c(elf, n, rng):
    copier = CopyValues(elf)
    stats = {'random': 0, 'captures': 0, 'refused': 0}
    size = 0x400
    # 0x7FFFFFFF and -9 wrap (32-bit shift) to offsets 0x100 and 0 inside the block.
    slots = list(range(23)) + [0x7FFFFFFF, -9]
    for slot in RM.select(slots, 8, 0x21BAE0, keep=lambda i, v: v in (0, 1, 22, 0x7FFFFFFF, -9)):
        context = bytes(rng.randrange(256) for _ in range(size))
        want, wlog = restore_original(elf, None, context, slot)
        r, got, glog = restore_native(n, copier, context, slot)
        assert r == 0 and got == want and glog == wlog, ('0021BAE0', slot, glog, wlog)
        stats['random'] += 1
    for label, path, _ in captures():
        ram = path.read_bytes()
        ctx = u32(ram, CTX_PTR)
        context = ram[ctx:ctx + 0x200]
        want, wlog = restore_original(elf, ram, context, 0)
        r, got, glog = restore_native(n, copier, context, 0)
        assert r == 0 and got == want and glog == wlog, ('0021BAE0 capture', label)
        stats['captures'] += 1
    # Out of the block, or no worker: nothing copied.
    context = bytes(range(256)) * 4
    for slot, missing in ((-10, False), (23, False), (0x7FFFFFF0, False), (0, True)):
        r, got, log = restore_native(n, copier, context, slot, missing)
        assert r == -1 and got == context and log == [], ('0021BAE0 refusal', slot, missing)
        stats['refused'] += 1
    return stats


# ======================================================================
# D. The message presenters
# ======================================================================

def container_extent(ram, address):
    return u32(ram, address) + u32(ram, address + 8)


class CoverOriginal(md.Original):
    """md.Original recording both outcomes of 001FD0E0's branches, with
    001FDDB0 hooked (scripted) and 001FC9B0 recorded then executed."""

    def __init__(self, elf, ram, spad, advances):
        super().__init__(elf, ram, spad, advances)
        self.outcomes = set()
        self.script = []
        ee = self.ee
        branch = ee.branch

        def recording(word, pc):
            b = branch(word, pc)
            unconditional = word >> 26 == 4 and (word >> 16 & 0x3FF) == 0
            if b is not None and not unconditional and CUE <= pc < CUE + SIZES[CUE]:
                self.outcomes.add((pc, b[0]))
            return b
        ee.branch = recording
        ee.hooks[LINE_DRAW] = self.line_draw
        ee.hooks[RESET] = self.reset

    def line_draw(self, e):
        slots, count, block = e.arg(0), s32(e.arg(1)), e.arg(2)
        line = e.load(block + 0x2C)
        self.log.append(('line_draw', e.read(slots, 0x100), count, e.read(line, 0x40), e.read(block, 0x9C)))
        e.ret_int(self.script.pop(0))

    def reset(self, e):
        self.log.append(('reset',))
        hook = e.hooks.pop(RESET)
        e.nested(RESET)
        e.hooks[RESET] = hook


class Bench:
    """One capture's banks on both sides; the native presenters bound to a
    native EmMessageDraw (md.Native) over the same RAM."""

    def __init__(self, elf, lib, ram, spad, advances, cue=None):
        self.elf, self.lib, self.advances = elf, lib, advances
        ram = bytearray(ram)
        if cue is not None:            # a synthetic cue bank of this file's own
            ram[SYNTH:SYNTH + len(cue)] = cue
            struct.pack_into('<I', ram, CUE_PTR, SYNTH)
        self.ram = bytes(ram)
        self.o = CoverOriginal(elf, self.ram, spad, advances)
        self.n = md.Native(lib, self.ram, advances, {})
        self.keep = []
        help_a, rec_a, cue_a = (u32(self.ram, p) for p in (HELP_PTR, REC_PTR, CUE_PTR))
        self.addresses = {'help': help_a, 'records': rec_a, 'cue': cue_a}
        sizes = {'help': container_extent(self.ram, help_a), 'records': container_extent(self.ram, rec_a),
                 'cue': len(cue) if cue is not None else md.bank_extent(self.ram, cue_a)}
        self.banks = {}
        for name, address in self.addresses.items():
            buf = C.create_string_buffer(self.ram[address:address + sizes[name]], sizes[name])
            self.keep.append(buf)
            self.banks[name] = md.Bank(C.cast(buf, VP), sizes[name])
        self.styles = {}
        self.configs = {}
        for cfg in (CFG_CF0, CFG_C90):
            style = u32(self.ram, cfg + 0x14)
            st = md.Style.from_buffer_copy(self.ram[style:style + 8])
            self.styles[style] = st
            self.configs[cfg] = md.Config((I32 * 5)(*struct.unpack_from('<5i', self.ram, cfg)), C.pointer(st))
        self.data = CsData(self.banks['help'], self.banks['records'], self.banks['cue'], cue_a,
                           C.pointer(self.configs[CFG_CF0]), C.pointer(self.configs[CFG_C90]), CFG_C90,
                           (U32 * 6)(*struct.unpack_from('<6I', self.ram, D264DB0)))
        self.block = (C.c_uint8 * 0x9C)(*self.ram[BLOCK:BLOCK + 0x9C])
        self.script = []
        self.mode3 = Mode3(None, RESET_FN(self.reset), LINE_FN(self.line_draw))
        self.p = Presenters()
        d282228 = C.cast(C.addressof(self.block) + 0x78, C.POINTER(I32))
        assert lib.em_cs_presenters_init(C.byref(self.p), C.byref(self.n.draw_state), C.byref(self.data),
                                         C.byref(self.mode3), d282228) == 1
        self.p.d820EC0[:] = struct.unpack_from('<4I', self.ram, D820EC0)
        self.p.frame_address = STACK_TOP - 0x860 + 0xC0
        self.reset_ee = md.DrawEE(elf, self.ram)

    # ---- native workers --------------------------------------------------
    def reset(self, _):
        """001FC9B0 from original instructions over the native block/style."""
        self.n.log.append(('reset',))
        e = self.reset_ee
        e.write(BLOCK, bytes(self.block))
        e.write(md.STYLE, bytes(self.n.style))
        e.call(RESET, ())
        C.memmove(self.block, e.read(BLOCK, 0x9C), 0x9C)
        C.memmove(C.addressof(self.n.style), e.read(md.STYLE, 8), 8)
        return 1

    def line_draw(self, _, slots, count, line, block, result):
        self.n.log.append(('line_draw', C.string_at(slots, 0x100), count, C.string_at(line, 0x40),
                           C.string_at(block, 0x9C)))
        result[0] = self.script.pop(0)
        return 1

    def sub_bank(self, name, entry):
        c = self.addresses[name]
        off = u32(self.ram, c) + u32(self.ram, c + entry)
        bank = self.banks[name]
        return c + off, md.Bank(C.c_void_p(bank.bytes + off), bank.size - off)

    def same(self, where, extra=()):
        count = md.same(self.o, self.n, where)
        assert self.p.fault is None, (where, self.p.fault)
        assert not self.o.unmodelled(extra), (where, [hex(a) for a in self.o.unmodelled(extra)[:8]])
        return count


def sub_count(ram, sub):
    h = sub + u32(ram, sub) + u32(ram, sub + 8)
    return u32(ram, h + 4)


def section_d_mode4(elf, lib, ram, spad, advances):
    b = Bench(elf, lib, ram, spad, advances)
    stats = {'help': 0, 'title': 0, 'list': 0, 'segments': 0, 'calls': 0}
    help_a = b.addresses['help']
    groups = u32(ram, help_a + 4)
    requested = (s32(u32(ram, BLOCK + 0x90)), u32(ram, BLOCK + 8))   # the capture's own request
    cases = []
    for group in range(groups):
        sub, _ = b.sub_bank('help', 0x10 + 16 * group)
        count = sub_count(ram, sub)
        for line in [-1] + list(range(count)) + [count]:
            cases.append((group, line))
    cases = RM.select(cases, 60, 0x1FCB90, axes=(lambda c: c[0],),
                      keep=lambda i, c: c == requested or c[1] in (-1, 0) or c == (5, 0))
    for group, line in cases:
        for x, y in ((0x8A, 0xA8), (0x10E, 0xCC)):
            want = s32(b.o.call(HELP, x, y, group, line))
            got = I32()
            assert lib.em_cs_001FCB90(C.byref(b.p), x, y, group, line, C.byref(got)) == 0, b.p.fault
            assert got.value == want, ('001FCB90', group, line)
            stats['calls'] += b.same(('001FCB90', group, line, x, y))
            stats['help'] += 1
    for name, entry, routine in (('title', 0x20, TITLE), ('list', 0x30, LIST)):
        sub, bank = b.sub_bank('records', entry)
        count = sub_count(ram, sub)
        lines = RM.select(range(count), 12, routine, keep=lambda i, v: v in (0, count - 1))
        if name == 'title':
            for line in list(lines) + [-1, count]:
                for x, y in ((0xA8, 0xBE), (0x64, 0x47)):
                    want = s32(b.o.call(TITLE, line, x, y))
                    got = I32()
                    assert lib.em_cs_001FCF60(C.byref(b.p), line, x, y, C.byref(got)) == 0, b.p.fault
                    assert got.value == want, ('001FCF60', line)
                    stats['calls'] += b.same(('001FCF60', line, x, y))
                    stats['title'] += 1
            continue
        for line in lines:
            # 001FE660 alone, then 001FCF90 for pages around the string's length.
            q = b.o.call(FE480, sub, line) & M32
            want = s32(b.o.call(COUNT, q))
            got = I32()
            assert lib.em_cs_001FE660(C.byref(b.p), C.byref(bank), (q - sub) & M32, C.byref(got)) == 0
            assert got.value == want, ('001FE660', line)
            stats['segments'] += 1
            pages = sorted({0, 1, want // 10, want // 10 + 1, -1})
            for page in pages:
                want_r = s32(b.o.call(LIST, line, page, 0x64))
                got_r = I32()
                assert lib.em_cs_001FCF90(C.byref(b.p), line, page, C.byref(got_r)) == 0, b.p.fault
                assert got_r.value == want_r == 1, ('001FCF90', line, page)
                stats['calls'] += b.same(('001FCF90', line, page))
                stats['list'] += 1
    return stats


# 001FD0E0 cases: (state words written over a base block, 001FDDB0 script)
SCRIPTS = ([1, 1, 1, 1], [2], [0], [1, 2], [1, 1, 1, 0], [7, 1, -1, 1], [1, 1, 3, 2])


def cue_cases(rng, lines, triggers):
    cases = []
    for line in lines:
        cases.append(({0x78: 0, 0x00: rng.getrandbits(32), 0x04: 1, 0x08: line, 0x0C: rng.getrandbits(32),
                       0x34: line, 0x48: 9, 0x60: 3}, []))
        for script in SCRIPTS:
            for base, record in ((0, 0), (triggers.get(line, 0), 0), (0, 1), (0x40, 2)):
                cases.append(({0x78: 1, 0x08: line, 0x3C: line, 0x38: base, 0x44: record, 0x28: 5,
                               0x10: 0}, list(script)))
        cases.append(({0x78: 1, 0x08: line, 0x3C: line + 1}, []))     # stale: D_00282228 = 0
    for state in (2, 3, 0xFFFFFFFF):
        cases.append(({0x78: state, 0x08: 0, 0x3C: 0}, []))
    return cases


def run_cue(b, case, where):
    words, script = case
    block = bytearray(b.ram[BLOCK:BLOCK + 0x9C])
    for off, value in words.items():
        struct.pack_into('<I', block, off, value & M32)
    b.o.ee.write(BLOCK, bytes(block))
    C.memmove(b.block, bytes(block), 0x9C)
    b.o.script, b.script = list(script), list(script)
    b.o.ee.r[29] = STACK_TOP
    b.o.call(CUE, BLOCK, 2)
    assert b.lib.em_cs_001FD0E0(C.byref(b.p), b.block) == 0, (where, b.p.fault)
    assert bytes(b.block) == b.o.ee.read(BLOCK, 0x9C), (where, 'block', [
        hex(i) for i in range(0x9C) if b.block[i] != b.o.ee.read(BLOCK, 0x9C)[i]])
    assert list(b.p.d820EC0) == [b.o.ee.load(D820EC0 + 4 * i) for i in range(4)], (where, 'D_00820EC0')
    assert not b.o.script and not b.script, (where, 'script not consumed', b.o.script, b.script)
    return b.same(where, extra=list(range(BLOCK, BLOCK + 0x9C)) + list(range(D820EC0, D820EC0 + 0x10)))


# Synthetic cue lines (this file's own ASCII): 0x0A ends a line (one of
# four), 0x0C ends the page, 0 ends the string; the records' triggers
# (third word) meet the running byte offset plus +0x38.
SYNTH_CUE = [
    (b'ONE\nTWO\nTHREE\nFOUR\nFIVE', [(3, 1, 0, 0), (2, 2, 4, 0), (4, 1, 8, 1), (3, 0, 30, 0)]),
    (b'PAGE\x0cNEXT PAGE', [(3, 1, 2, 0), (3, 0, 5, 0)]),
    (b'', [(3, 1, 0, 0)]),
    (b'X' * 70 + b'\nTAIL', [(3, 1, 5, 0), (2, 3, 71, 0)]),
    (b'A\n\n\n\nB', [(3, 1, 1, 0), (3, 1, 2, 0)]),
    (b'SAME\nSPOT', [(3, 1, 3, 0), (2, 1, 3, 0), (4, 2, 3, 3)]),
    (b'NO RECORDS AT ALL', []),
    (b'LAST', [(3, 1, 2, 0)]),
]


def section_d_cue(elf, lib, ram, spad, advances, rng):
    stats = {'real': 0, 'synthetic': 0, 'calls': 0}
    outcomes = set()
    b = Bench(elf, lib, ram, spad, advances)
    cue = b.addresses['cue']
    lines = list(range(u32(ram, cue + 4)))
    chosen = RM.select(lines, 4, 0x1FD0E0, keep=lambda i, v: v in (0, len(lines) - 1))
    for case in RM.select(cue_cases(rng, chosen, {}), 40, 0x1FD0E1,
                          keep=lambda i, c: c[0].get(0x78) != 1 or c[1] in ([1, 1, 1, 1],)):
        stats['calls'] += run_cue(b, case, ('cue', case[0].get(0x78), case[0].get(0x08)))
        stats['real'] += 1
    outcomes |= b.o.outcomes
    body = md.synthetic_bank(SYNTH_CUE)
    s = Bench(elf, lib, ram, spad, advances, cue=body)
    triggers = {i: 0 for i in range(len(SYNTH_CUE))}
    for case in cue_cases(rng, range(len(SYNTH_CUE)), triggers):
        stats['calls'] += run_cue(s, case, ('synthetic cue', case[0].get(0x78), case[0].get(0x08)))
        stats['synthetic'] += 1
    outcomes |= s.o.outcomes
    # Both outcomes of every conditional branch except the two that are
    # one-sided by construction: the loop-back after a glyph (the stop
    # counter is never 0 there) and the last test of the 0 / 0x0C / 0x0A
    # dispatch (only 0x0A reaches it).
    sites = {pc for pc, _ in outcomes}
    one_sided = {pc for pc in sites if len({t for p, t in outcomes if p == pc}) == 1}
    stats['branch_sites'], stats['one_sided'] = len(sites), sorted(hex(pc) for pc in one_sided)
    assert one_sided == {0x1FD300, 0x1FD33C}, ('001FD0E0 branch outcomes not reached', stats['one_sided'])
    return stats


def section_e(elf, lib, ram, spad, advances):
    """Fail-stop of the presenters."""
    checks = 0
    b = Bench(elf, lib, ram, spad, advances)
    # A group whose entry lies outside the container faults and draws nothing.
    got = I32()
    assert lib.em_cs_001FCB90(C.byref(b.p), 0x8A, 0xA8, 0x7FFFFFF, 0, C.byref(got)) == -1
    assert b.p.fault is not None and b.n.log == []
    checks += 1
    # A latched fault refuses every later call.
    assert lib.em_cs_001FCF60(C.byref(b.p), 0, 0xA8, 0xBE, C.byref(got)) == -1 and b.n.log == []
    checks += 1
    # Missing mode-3 workers fault before any write.
    for missing in ('reset', 'line_draw'):
        b = Bench(elf, lib, ram, spad, advances)
        setattr(b.mode3, missing, type(getattr(b.mode3, missing))())
        b.p.mode3 = b.mode3
        before = bytes(b.block)
        C.memmove(b.block, before[:0x78] + struct.pack('<I', 0 if missing == 'reset' else 1) + before[0x7C:],
                  0x9C)
        start = bytes(b.block)
        assert lib.em_cs_001FD0E0(C.byref(b.p), b.block) == -1 and bytes(b.block) == start
        assert b.n.log == []
        checks += 1
    # An empty cue bank faults on the first string read.
    b = Bench(elf, lib, ram, spad, advances)
    b.data.cue = md.Bank(None, 0)
    C.memmove(b.block, bytes(0x78) + struct.pack('<I', 1) + bytes(0x20), 0x9C)
    assert lib.em_cs_001FD0E0(C.byref(b.p), b.block) == -1 and b.p.fault is not None
    checks += 1
    return checks


def section_d(elf, lib, rng):
    ram_path = REF / 'panel' / 'eeMemory.bin'
    ram = ram_path.read_bytes()
    # The containers, the cue bank, the two configs and D_00264DB0 are the
    # same bytes in every capture.
    fixed = [(u32(ram, p), container_extent(ram, u32(ram, p))) for p in (HELP_PTR, REC_PTR)]
    fixed += [(u32(ram, CUE_PTR), md.bank_extent(ram, u32(ram, CUE_PTR))),
              (CFG_CF0, 0x18), (CFG_C90, 0x18), (D264DB0, 0x18)]
    for label, path, _ in captures():
        other = path.read_bytes()
        for p in (HELP_PTR, REC_PTR, CUE_PTR):
            assert u32(other, p) == u32(ram, p), (label, hex(p))
        for address, size in fixed:
            assert other[address:address + size] == ram[address:address + size], (label, hex(address))
    advances = md.original_advances(elf, ram)
    stats = {'captures_checked': len(captures())}
    stats['mode4'] = section_d_mode4(elf, lib, ram, None, advances)
    stats['mode3'] = section_d_cue(elf, lib, ram, None, advances, rng)
    stats['fail_stop'] = section_e(elf, lib, ram, None, advances)
    return stats


# ======================================================================

def main():
    elf = read_elf()
    rng = random.Random(0xC5)
    report = {'mode': RM.MODE, 'routines': check_callees(elf)}
    n, shim = build_native()
    report['A_00102CD0'] = section_a(elf, n, shim, rng)
    report['B_0020CCB0'] = section_b(elf, n)
    report['C_0021BAE0'] = section_c(elf, n, rng)
    report['D_presenters'] = section_d(elf, n, rng)
    a, d = report['A_00102CD0'], report['D_presenters']
    RM.banner(f"00102CD0 {a['cases']} cases + {a['captures']} captures",
              f"0020CCB0 {report['B_0020CCB0']['page_bytes']} page bytes",
              f"0021BAE0 {report['C_0021BAE0']['random']} slots + {report['C_0021BAE0']['captures']} captures",
              f"presenters {d['mode4']['help']} help / {d['mode4']['title']} title / {d['mode4']['list']} list"
              f" / {d['mode3']['real'] + d['mode3']['synthetic']} cue calls")
    (OUT / 'result.json').write_text(json.dumps(report, indent=2, default=str) + '\n')
    print(json.dumps(report, default=str))
    print('PASS')


if __name__ == '__main__':
    main()
