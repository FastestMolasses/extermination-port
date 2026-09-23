#!/usr/bin/env python3
"""Execute the original area-load veil draw and compare em_load_veil_particles.c.

docs/LOAD_VEIL_PARTICLES.md. The user's pinned ELF supplies every
instruction and the captured AREA11 RAM (build/startup-reference) supplies
every world word (the render context *D_00275670, the static GS block base
D_00275674, the capture texture D_0027568C, D_0026E880, the SDK block
D_00241010, the veil block *D_00275888); none are embedded here. Routines
executed unmodified:

  0021B1B0 veil draw (asm-word)    0021B500 phase step
  001D1F80 / 001D1FF0 / 001D2040 / 001D1F20   REF tags
  001D63B0 line packet             001D7080 RGBAQ packet
  001D6BA0 TEX0 packet             001D6E60 draw-environment packet
  001D6930 / 001D6B60 frame copy   001DFA40 lens pass
  001006D8 SDK environment fill    00100610 SDK Z size   00100268
  00102948 quadword copy

The two leaves that are workers of the native module, 0011DF78 (fabsf) and
001281C0 (float_to_int), also run their ORIGINAL code (pass-through hooks);
each call made by a translated routine is logged with its argument and
result, and the native module's workers (bound here to the existing
translations em_sdk_math_original_0011DF78 and em_player_float_to_int) must
make the same calls in the same order with the same values. The test asserts
that the jal targets of the translated routines are exactly these routines
plus the two workers.

Every case runs the original over a copy of the captured RAM and the native
module over another copy of the same RAM (its packet window is the whole
32 MB, so packets land at the same original addresses), then compares all
32 MB byte for byte (packets, cursors, veil block, everything), the return
value, the worker call log, and, when 001DFA40 ran, its 16 x 16 table (the
three lanes the original writes, read from the oracle's stack frame).

Arithmetic: VeilEE (the fall lane's FallEE, imported, not edited) routes
COP1 through tools/ee_float_model.py; the native module uses em_ee_float.h.

Capture evidence: the veil block is the same in every capture after New
Game (opening, playable, handoff and every route beat present). Its +0x14
seed is exactly what one executed 0021B1B0 leaves there, and its +0x04 phase
is reached from 0021B180's 0 by k executed 0021B500 steps; the native module
reproduces both. The route beats (build/s87/route) start at first control,
after the load veil has finished (state 3), so none of them runs these
routines; they are used only for that block check.

Default run (~10 s): a fixed-seed sample that exercises both outcomes of
every reachable conditional branch of the translated routines (asserted;
the unreachable ones are listed with the reason). EM_TEST_FULL=1: the full
sweep.
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
import test_player_slide_reference as shared  # noqa: E402
from test_player_slide_reference import EE, read_elf, sx32, REFERENCE, DECOMP  # noqa: E402
from test_player_fall_reference import FallEE  # noqa: E402

MASK = 0xFFFFFFFF
LANE = os.environ.get('EM_LANE', 'b6-load-veil-particles')
OUT = ROOT / 'build' / LANE
RAM_SIZE = 0x2000000


def F(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


# ======================================================================
# The routines, their callees and the world addresses
# ======================================================================

SIZES = {0x21B1B0: 0x344, 0x21B500: 0x44, 0x1D1F80: 0x6C, 0x1D1FF0: 0x4C, 0x1D2040: 0x4C,
         0x1D1F20: 0x60, 0x1D63B0: 0xE8, 0x1D7080: 0x78, 0x1D6BA0: 0xEC, 0x1D6E60: 0xF8,
         0x1D6930: 0x1D8, 0x1D6B60: 0x3C, 0x1DFA40: 0x3CC, 0x1006D8: 0x1E4, 0x100610: 0xC8,
         0x100268: 0xC, 0x102948: 0xC}
FABS, TO_INT = 0x11DF78, 0x1281C0
WORKERS = {FABS: 'fabsf', TO_INT: 'float_to_int'}
# Conditional branches no input reaches, with the reason (docs section 5).
UNREACHABLE = {
    # 0021B1B0: the noise word is (hi of a 32x32 multiply) >> 2 < 2^30: never negative.
    (0x21B270, True): '0021B1B0 unsigned-conversion branch: the value is < 2^30',
}

CTX_PTR, VEIL_PTR = 0x275670, 0x275888
D241010 = 0x241010             # the SDK GS parameter block's mode dword
FREE = 0x1E00000              # an aligned RAM area the unit packets are written to
VECS = 0x1DF0000              # 001D63B0 / 001D6930 argument vectors
STACK_TOP = shared.STACK_TOP
FRAME_0021B1B0, FRAME_001DFA40, TABLE_AT = 0xB0, 0x10F0, 0xD0
TABLE_BYTES = 0x1000

CAPTURE = REFERENCE / 'opening_ee.bin'
BLOCK_CAPTURES = [REFERENCE / n for n in ('opening_ee.bin', 'playable_ee.bin', 'handoff_ee.bin')]
ROUTE = DECOMP / 'build/s87/route'


def in_translated(pc):
    return any(start <= pc < start + size for start, size in SIZES.items())


def check_callee_set(elf):
    """The jal targets of the translated routines are exactly the routines
    translated here plus the two worker leaves; every worker is called."""
    ee = EE(elf)
    targets = set()
    for start, size in SIZES.items():
        for pc in range(start, start + size, 4):
            word = ee.load(pc)
            if word >> 26 == 3:
                targets.add((word & 0x3FFFFFF) << 2)
    missing = sorted(t for t in targets if t not in SIZES and t not in WORKERS)
    assert not missing, ('callees neither worker nor translated', [hex(t) for t in missing])
    unused = sorted(t for t in WORKERS if t not in targets)
    assert not unused, ('worker addresses no routine calls', [hex(t) for t in unused])
    for start in SIZES:
        if start in (0x21B1B0, 0x21B500):      # both called by 0021B550
            continue
        assert start in targets, ('translated routine no translated routine calls', hex(start))
    return len(targets)


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


# ======================================================================
# The oracle
# ======================================================================

class VeilEE(FallEE):
    """FallEE (COP1 through ee_float_model) over a RAM image: records every
    stored RAM range (to restore the image between cases), the branch
    outcomes inside the translated routines, and the worker leaf calls."""

    def __init__(self, elf, ram):
        super().__init__(elf, ram)
        self.outcomes = set()
        self.calls = []
        self.dirty = []
        self.lens_entry = None
        for address in WORKERS:
            self.hooks[address] = self._worker(address)

    def save(self, address, value, size=4):
        address &= MASK
        if address < 0x40000000:
            self.dirty.append((address & (RAM_SIZE - 1), size))
        super().save(address, value, size)

    def write(self, address, data):
        address &= MASK
        if address < 0x40000000:
            self.dirty.append((address & (RAM_SIZE - 1), len(data)))
        super().write(address, data)

    def branch(self, word, pc):
        b = super().branch(word, pc)
        if b is not None and in_translated(pc):
            self.outcomes.add((pc, b[0]))
        return b

    def execute(self, word, pc):
        if pc == 0x1DFA40 and self.lens_entry is None:
            # The stack words 001DFA40's table will occupy, as they stand at
            # its entry: the original never writes lane 3 of the table but
            # copies whole quadwords into the strips.
            self.lens_entry = self.read(((self.r[29] - FRAME_001DFA40) & MASK) + TABLE_AT, TABLE_BYTES)
        super().execute(word, pc)

    def _worker(self, address):
        def run(ee):
            # The original leaf runs in place, at the caller's stack pointer
            # (its frame lands where the real call puts it); the argument and
            # result are logged. v0 / f0 are its results; every other
            # register is restored (the caller does not read them).
            x = ee.f[12] & MASK
            saved_hook = ee.hooks.pop(address)
            saved = (list(ee.r), list(ee.rh), ee.hi, ee.lo, list(ee.f), ee.acc, ee.cond)
            try:
                ee.r[31] = shared.RETURN
                ee.run(address)
                v0, f0 = ee.r[2], ee.f[0]
            finally:
                ee.hooks[address] = saved_hook
                ee.r, ee.rh, ee.hi, ee.lo, ee.f, ee.acc, ee.cond = saved
            ee.r[2], ee.f[0] = v0, f0
            ee.calls.append((address, x, (f0 if address == FABS else v0) & MASK))
        return run

    def invoke(self, entry, args, f12=None):
        self.r = [0] * 32
        self.rh = [0] * 32
        self.r[28] = 0x27D370
        self.r[29] = STACK_TOP
        self.stack[:] = bytes(len(self.stack))
        for i, value in enumerate(args):
            self.r[4 + i] = sx32(value)
        if f12 is not None:
            self.f[12] = f12 & MASK
        self.r[31] = shared.RETURN
        self.run(entry)
        return self.r[2] & MASK

    def table(self, entry):
        """001DFA40's table as the last 001DFA40 call left it."""
        sp = STACK_TOP - (FRAME_0021B1B0 if entry == 0x21B1B0 else 0) - FRAME_001DFA40
        return self.read(sp + TABLE_AT, TABLE_BYTES)


# ======================================================================
# The native module
# ======================================================================

SHIM = r'''
#include <string.h>
#include "game/em_load_veil_particles.h"
#include "game/em_sdk_math_original.h"
#include "game/em_player_stage_workers.h"

typedef struct { uint32_t *log; uint32_t n, cap; } Rec;

static void note(Rec *r, uint32_t a, uint32_t x, uint32_t y)
{
    if (r->n < r->cap) {
        r->log[3 * r->n] = a;
        r->log[3 * r->n + 1] = x;
        r->log[3 * r->n + 2] = y;
    }
    r->n++;
}

static int w_fabs(void *c, uint32_t x, uint32_t *out)
{
    float f, g;
    memcpy(&f, &x, 4);
    g = em_sdk_math_original_0011DF78(f);
    memcpy(out, &g, 4);
    note((Rec *)c, 0x0011DF78u, x, *out);
    return 0;
}

static int w_to_int(void *c, uint32_t x, int32_t *out)
{
    *out = em_player_float_to_int(x);
    note((Rec *)c, 0x001281C0u, x, (uint32_t)*out);
    return 0;
}

static uint32_t word(const uint8_t *ram, uint32_t a) { uint32_t v; memcpy(&v, ram + (a & 0x1FFFFFFu), 4); return v; }
#define U32(a) ((uint32_t *)(void *)(ram + ((a) & 0x1FFFFFFu)))

int lvp_run(uint8_t *ram, uint32_t fn, const uint32_t *a, uint8_t *table, uint32_t *log,
            uint32_t cap, uint32_t *nlog, uint32_t *fault, uint32_t *result)
{
    EmLoadVeilParticles s;
    EmLoadVeilParticlesBlock v;
    Rec r;
    uint32_t ctx = word(ram, 0x275670u), res = 0;
    int32_t ires = 0;
    int rc = -1;
    memset(&s, 0, sizeof s);
    r.log = log; r.n = 0; r.cap = cap;
    s.world.cursor = U32(ctx + 0x10u);
    s.world.cursor_count = 4;
    s.world.ctx_9C = U32(ctx + 0x9Cu);
    s.world.d00275674 = U32(0x275674u);
    s.world.d0027568C = U32(0x27568Cu);
    s.world.d0026E880 = ram + 0x26E880u;
    s.world.d00241010 = ram + 0x241010u;
    s.world.packet = ram;
    s.world.packet_address = 0;
    s.world.packet_size = 0x2000000u;
    s.world.table = table;
    s.workers.ctx = &r;
    s.workers.w_0011DF78 = w_fabs;
    s.workers.w_001281C0 = w_to_int;
    v.phase = U32(a[0] + 4u);
    v.level0 = (const float *)(const void *)(ram + ((a[0] + 8u) & 0x1FFFFFFu));
    v.seed = U32(a[0] + 0x14u);
    v.base_y = U32(a[0] + 0x18u);
    switch (fn) {
    case 0x21B1B0u: rc = em_load_veil_particles_0021B1B0(&s, &v); break;
    case 0x21B500u: rc = em_load_veil_particles_0021B500(&v); break;
    case 0x1D1F80u: rc = em_load_veil_particles_001D1F80(&s, (int32_t)a[0], (int32_t)a[1], (int32_t)a[2]); break;
    case 0x1D1FF0u: rc = em_load_veil_particles_001D1FF0(&s, (int32_t)a[0], (int32_t)a[1]); break;
    case 0x1D2040u: rc = em_load_veil_particles_001D2040(&s, (int32_t)a[0], (int32_t)a[1]); break;
    case 0x1D1F20u: rc = em_load_veil_particles_001D1F20(&s, (int32_t)a[0]); break;
    case 0x1D63B0u:
        rc = em_load_veil_particles_001D63B0(&s, (int32_t)a[0], U32(a[1]), U32(a[2]), U32(a[3]), U32(a[4]), &res);
        break;
    case 0x1D7080u: rc = em_load_veil_particles_001D7080(&s, (int32_t)a[0], a[1], a[5]); break;
    case 0x1D6BA0u:
        rc = em_load_veil_particles_001D6BA0(&s, (int32_t)a[0], (int32_t)a[1], (int32_t)a[2], (int32_t)a[3],
                                             (int32_t)a[4], (int32_t)a[5], &res);
        break;
    case 0x100610u:
        rc = em_load_veil_particles_00100610(&s, (int32_t)a[0], (int32_t)a[1], (int32_t)a[2], &ires);
        res = (uint32_t)ires;
        break;
    case 0x1006D8u:
        rc = em_load_veil_particles_001006D8(&s, a[0], (int32_t)a[1], (int32_t)a[2], (int32_t)a[3], (int32_t)a[4],
                                             (int32_t)a[5], &ires);
        res = (uint32_t)ires;
        break;
    case 0x1D6E60u:
        rc = em_load_veil_particles_001D6E60(&s, (int32_t)a[0], (int32_t)a[1], (int32_t)a[2], (int32_t)a[3], &res);
        break;
    case 0x1D6930u:
        rc = em_load_veil_particles_001D6930(&s, (int32_t)a[0], (int32_t)a[1], (int32_t)a[2], (int32_t)a[3],
                                             ram + (a[4] & 0x1FFFFFFu), &res);
        break;
    case 0x1D6B60u:
        rc = em_load_veil_particles_001D6B60(&s, (int32_t)a[0], (int32_t)a[1], (int32_t)a[2], (int32_t)a[3],
                                             ram + (a[4] & 0x1FFFFFFu), &res);
        break;
    case 0x1DFA40u: rc = em_load_veil_particles_001DFA40(&s, (int32_t)a[0], a[1], a[2], a[5], &res); break;
    default: break;
    }
    *nlog = r.n;
    fault[0] = s.fault.address;
    fault[1] = (uint32_t)s.fault.code;
    *result = res;
    return rc;
}
'''


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    source = OUT / 'lvp_shim.c'
    source.write_text(SHIM)
    lib = OUT / ('lvp.dylib' if sys.platform == 'darwin' else 'lvp.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc', str(source),
                    'src/game/em_load_veil_particles.c', 'src/game/em_sdk_math_original.c',
                    'src/game/em_player_stage_workers.c', '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    native.lvp_run.argtypes = [C.c_void_p, C.c_uint32, C.POINTER(C.c_uint32), C.c_void_p,
                               C.POINTER(C.c_uint32), C.c_uint32, C.POINTER(C.c_uint32),
                               C.POINTER(C.c_uint32), C.POINTER(C.c_uint32)]
    native.lvp_run.restype = C.c_int
    return native


# ======================================================================
# One case: original and native over the same RAM image
# ======================================================================

ELF = BASE = NATIVE = None
_STATE = {}


def state():
    """Per-process oracle and native images (restored between cases)."""
    if 'ee' not in _STATE:
        _STATE['ee'] = VeilEE(ELF, BASE)
        _STATE['mem'] = bytearray(BASE)
        _STATE['ptr'] = (C.c_ubyte * RAM_SIZE).from_buffer(_STATE['mem'])
    return _STATE['ee'], _STATE['mem'], _STATE['ptr']


def restore(ee, mem, ranges):
    for address, size in ranges:
        end = min(RAM_SIZE, address + size)
        ee.mem[address:end] = BASE[address:end]
        mem[address:end] = BASE[address:end]


def run_case(case):
    """case = (label, entry, args, f12, pokes). pokes: [(address, bytes)]
    applied to both images first. Returns (label, entry, outcomes, calls)."""
    label, entry, args, f12, pokes = case
    ee, mem, ptr = state()
    ee.dirty = []
    ee.calls = []
    ee.outcomes = set()
    ee.lens_entry = None
    for address, data in pokes:
        ee.write(address, data)
        mem[address:address + len(data)] = data
    touched = list(ee.dirty)
    try:
        v0 = ee.invoke(entry, args, f12)
        a = (C.c_uint32 * 8)(*[x & MASK for x in list(args) + [0] * (8 - len(args))])
        if f12 is not None:
            a[5] = f12 & MASK
        cap = 1 << 16
        log = (C.c_uint32 * (3 * cap))()
        n, fault, result = C.c_uint32(), (C.c_uint32 * 2)(), C.c_uint32()
        # The native table buffer holds the same stack words the original's
        # frame held at 001DFA40's entry (only lane 3 of each entry survives).
        table = bytearray(ee.lens_entry) if ee.lens_entry is not None else bytearray(TABLE_BYTES)
        tptr = (C.c_ubyte * TABLE_BYTES).from_buffer(table)
        rc = NATIVE.lvp_run(ptr, entry, a, tptr, log, cap, C.byref(n), fault, C.byref(result))
        assert rc == 0, (label, 'native faulted', hex(fault[0]), fault[1])
        assert n.value <= cap, (label, 'worker log overflow')
        native_calls = [tuple(log[3 * i:3 * i + 3]) for i in range(n.value)]
        assert native_calls == ee.calls, (label, 'worker calls', len(native_calls), len(ee.calls),
                                          next((i, native_calls[i], ee.calls[i]) for i in
                                               range(min(len(native_calls), len(ee.calls)))
                                               if native_calls[i] != ee.calls[i]) if
                                          native_calls[:len(ee.calls)] != ee.calls[:len(native_calls)]
                                          else 'length')
        if mem != ee.mem:
            diff = next(i for i in range(RAM_SIZE) if mem[i] != ee.mem[i])
            raise AssertionError((label, 'RAM differs first at', hex(diff),
                                  'native', mem[diff & ~15:(diff & ~15) + 16].hex(),
                                  'original', bytes(ee.mem[diff & ~15:(diff & ~15) + 16]).hex()))
        if entry in (0x1D63B0, 0x1D6BA0, 0x100610, 0x1006D8, 0x1D6E60, 0x1D6930, 0x1D6B60, 0x1DFA40):
            assert result.value == v0, (label, 'return value', hex(result.value), hex(v0))
        if entry in (0x21B1B0, 0x1DFA40):
            original = ee.table(entry)
            for q in range(TABLE_BYTES // 16):
                assert table[q * 16:q * 16 + 16] == original[q * 16:q * 16 + 16], (label, 'table', q)
                assert table[q * 16 + 12:q * 16 + 16] == ee.lens_entry[q * 16 + 12:q * 16 + 16], (
                    label, 'table lane 3 written', q)
        return label, entry, ee.outcomes, len(ee.calls)
    finally:
        restore(ee, mem, touched + ee.dirty)


# ======================================================================
# Cases
# ======================================================================

def w32(value):
    return struct.pack('<I', value & MASK)


def veil_block_address():
    return struct.unpack_from('<I', BASE, VEIL_PTR)[0] & (RAM_SIZE - 1)


def ctx_address():
    return struct.unpack_from('<I', BASE, CTX_PTR)[0] & (RAM_SIZE - 1)


def cursor_poke(chan, address):
    return (ctx_address() + 0x10 + 4 * chan, w32(address))


def veil_cases(rng, count):
    """0021B1B0 over the captured block and over varied phase / level /
    base-Y / seed / 9C / cursor."""
    veil, ctx = veil_block_address(), ctx_address()
    phases = [None, 0.0, 0.25, 0.5, 0.993, 0.9999, 0.001]
    levels = [None, 0.0, 1.0, 0.37, 0.001, 2.5]
    bases = [None, 0x8000, 0x7F00, 0x8230, 0x7000]
    cases = [('0021B1B0 captured', 0x21B1B0, (veil,), None, [])]
    cases.append(('0021B1B0 captured 9C=0', 0x21B1B0, (veil,), None, [(ctx + 0x9C, w32(0))]))
    for k in range(count):
        pokes = []
        phase, level, base = rng.choice(phases), rng.choice(levels), rng.choice(bases)
        if phase is not None:
            pokes.append((veil + 4, w32(F(phase))))
        if level is not None:
            pokes.append((veil + 8, w32(F(level))))
        if base is not None:
            pokes.append((veil + 0x18, w32(base)))
        pokes.append((veil + 0x14, w32(rng.getrandbits(32))))
        pokes.append((ctx + 0x9C, w32(rng.choice((0, 1)))))
        if rng.random() < 0.5:
            pokes.append(cursor_poke(0, FREE + 16 * rng.randrange(0x100)))
        cases.append(('0021B1B0 #%d' % k, 0x21B1B0, (veil,), None, pokes))
    return cases


def phase_cases(rng, count):
    veil = veil_block_address()
    edges = [0.0, 0.993, 0.99299997, 0.9930001, 1.0, -0.5, 5.0, 0.5, 0.999]
    values = [F(e) for e in edges] + [F(rng.uniform(-2.0, 2.0)) for _ in range(count)]
    return [('0021B500 %08X' % v, 0x21B500, (veil,), None, [(veil + 4, w32(v))]) for v in values]


def lens_cases(rng, count):
    ctx = ctx_address()
    fixed = [(0, 0, 0x80808080, F(-0.45), 1), (0, 0x40, 0x40404040, F(-0.45), 0)]
    out = []
    for k, (chan, a1, a2, f12, c9) in enumerate(fixed):
        out.append(('001DFA40 fixed %d' % k, 0x1DFA40, (chan, a1, a2), f12, [(ctx + 0x9C, w32(c9))]))
    for k in range(count):
        chan = rng.choice((0, 1, 2, 3))
        pokes = [(ctx + 0x9C, w32(rng.choice((0, 1, 2)))), cursor_poke(chan, FREE + 16 * rng.randrange(0x100))]
        f12 = F(rng.choice((-0.45, 0.0, 0.3, rng.uniform(-1.0, 1.0))))
        out.append(('001DFA40 #%d' % k, 0x1DFA40, (chan, rng.getrandbits(32), rng.getrandbits(32)), f12, pokes))
    return out


def builder_cases(rng, count):
    ctx = ctx_address()
    out = []

    def cursor(chan):
        return cursor_poke(chan, FREE + 16 * rng.randrange(0x400))

    def small():
        return rng.choice((0, 1, 2, 3, 7, 8, 9, 15, 31, 32, -1, rng.randrange(-0x8000, 0x8000),
                           rng.getrandbits(32)))

    def half():
        return rng.choice((0, 1, 0x3F, 0x40, 0x41, 0x1FF, 0x200, 0x280, 0x1C0, 0xE0, -1, -0x40, -0x41,
                           0x7FFF, -0x8000, 0x8000, 0x12345, rng.randrange(-0x8000, 0x8000),
                           rng.getrandbits(32)))

    for k in range(count):
        chan = rng.choice((0, 1, 2, 3))
        pokes = [cursor(chan), (ctx + 0x9C, w32(rng.choice((0, 1, 2, 7))))]
        mode = rng.choice((None, 1, 0x0000000100000001, rng.getrandbits(64)))
        if mode is not None:
            pokes.append((D241010, struct.pack('<Q', mode)))
        vecs = bytes(rng.getrandbits(8) for _ in range(0x40))
        pokes.append((VECS, vecs))
        tag = rng.randrange(12)
        if tag == 0:
            out.append(('001D1F80 #%d' % k, 0x1D1F80, (chan, small(), small()), None, pokes))
        elif tag == 1:
            out.append(('001D1FF0 #%d' % k, 0x1D1FF0, (chan, small()), None, pokes))
        elif tag == 2:
            out.append(('001D2040 #%d' % k, 0x1D2040, (chan, small()), None, pokes))
        elif tag == 3:
            out.append(('001D1F20 #%d' % k, 0x1D1F20, (chan,), None, pokes))
        elif tag == 4:
            out.append(('001D63B0 #%d' % k, 0x1D63B0, (chan, VECS, VECS + 0x10, VECS + 0x20, VECS + 0x30),
                        None, pokes))
        elif tag == 5:
            out.append(('001D7080 #%d' % k, 0x1D7080, (chan, rng.getrandbits(32)), rng.getrandbits(32), pokes))
        elif tag == 6:
            out.append(('001D6BA0 #%d' % k, 0x1D6BA0, (chan, small(), small(), small(), small(), small()),
                        None, pokes))
        elif tag == 7:
            out.append(('00100610 #%d' % k, 0x100610, (half(), half(), half()), None, pokes))
        elif tag == 8:
            env = FREE + 8 * rng.randrange(0x800)
            out.append(('001006D8 #%d' % k, 0x1006D8, (env, half(), half(), half(), half(), half()),
                        None, pokes))
        elif tag == 9:
            out.append(('001D6E60 #%d' % k, 0x1D6E60, (chan, small(), small(), small()), None, pokes))
        elif tag == 10:
            out.append(('001D6930 #%d' % k, 0x1D6930, (chan, small(), small(), small(), VECS), None, pokes))
        else:
            out.append(('001D6B60 #%d' % k, 0x1D6B60, (chan, small(), small(), small(), VECS), None, pokes))
    return out


# ======================================================================
# Capture evidence
# ======================================================================

def capture_evidence():
    """The veil block after New Game in every capture, against the executed
    0021B1B0 (the +0x14 seed) and 0021B500 (the +0x04 phase), native too."""
    veil = veil_block_address()
    block = bytes(BASE[veil:veil + 0x1C])
    sources = [p for p in BLOCK_CAPTURES if p.exists()]
    if ROUTE.exists():
        sources += sorted(ROUTE.glob('*/eeMemory.bin'))
    for path in sources:
        with open(path, 'rb') as f:
            data = f.read()
        pointer = struct.unpack_from('<I', data, VEIL_PTR)[0] & (RAM_SIZE - 1)
        assert pointer == veil and data[pointer:pointer + 0x1C] == block, ('veil block differs', str(path))
    assert block[0] == 3, ('the captured veil has not finished', block[0])
    # The seed: one executed 0021B1B0 from any start leaves the captured value.
    ee, mem, ptr = state()
    ee.dirty = []
    ee.write(veil + 0x14, w32(0))
    ee.invoke(0x21B1B0, (veil,))
    seed = ee.load(veil + 0x14)
    restore(ee, mem, ee.dirty)
    assert seed == struct.unpack_from('<I', block, 0x14)[0], ('seed after 0021B1B0', hex(seed))
    # The phase: 0021B180 clears +4; k executed 0021B500 steps reach the capture.
    want = struct.unpack_from('<I', block, 4)[0]
    ee.dirty = []
    ee.write(veil + 4, w32(0))
    mem[veil + 4:veil + 8] = w32(0)
    steps = None
    for k in range(1, 4000):
        ee.invoke(0x21B500, (veil,))
        a = (C.c_uint32 * 8)(veil)
        n, fault, result = C.c_uint32(), (C.c_uint32 * 2)(), C.c_uint32()
        assert NATIVE.lvp_run(ptr, 0x21B500, a, None, None, 0, C.byref(n), fault, C.byref(result)) == 0
        assert mem[veil + 4:veil + 8] == ee.mem[veil + 4:veil + 8], ('phase step', k)
        if ee.load(veil + 4) == want:
            steps = k
            break
    restore(ee, mem, ee.dirty)
    assert steps is not None, 'the captured phase is not reached from 0 by 0021B500 steps'
    return len(sources), seed, steps


# ======================================================================

def main():
    global ELF, BASE, NATIVE
    started = time.time()
    ELF = read_elf()
    BASE = CAPTURE.read_bytes()
    assert len(BASE) == RAM_SIZE, ('capture size', len(BASE))
    callees = check_callee_set(ELF)
    NATIVE = build_native()
    captures, seed, steps = capture_evidence()

    rng = random.Random(0x21B1B0)
    veil = veil_cases(rng, 22)
    phase = phase_cases(rng, 400)
    lens = lens_cases(rng, 18)
    builders = builder_cases(rng, 3000)
    pick_veil = reference_mode.select(veil, 4, 0x1B1, keep=lambda i, c: i < 2)
    pick_phase = reference_mode.select(phase, 40, 0x1B5, keep=lambda i, c: i < 9)
    pick_lens = reference_mode.select(lens, 4, 0xFA4, keep=lambda i, c: i < 2)
    pick_builders = reference_mode.select(builders, 360, 0xB1D, axes=(lambda c: c[1],))
    cases = pick_veil + pick_lens + pick_phase + pick_builders
    cost = {0x21B1B0: 100, 0x1DFA40: 25}
    results = reference_mode.parallel_map(run_case, cases, cost=lambda c: cost.get(c[1], 1))

    outcomes, entries, calls = set(), {}, 0
    for label, entry, cover, count in results:
        outcomes.update(cover)
        entries[entry] = entries.get(entry, 0) + 1
        calls += count
    sites = branch_sites(ELF)
    missing = sorted('%06X %s' % (pc, 'taken' if taken else 'not taken')
                     for pc in sites for taken in (True, False)
                     if (pc, taken) not in outcomes and (pc, taken) not in UNREACHABLE)
    assert not missing, ('branch outcomes never exercised', missing)
    reached = sorted(k for k in UNREACHABLE if k in outcomes)
    assert not reached, ('a branch listed unreachable was taken', reached)
    absent = [hex(e) for e in SIZES if e not in (0x100268, 0x102948) and e not in entries]
    assert not absent, ('entry points never run directly', absent)

    reference_mode.banner(reference_mode.part(len(pick_veil), len(veil), '0021B1B0 cases'),
                          reference_mode.part(len(pick_lens), len(lens), '001DFA40 cases'),
                          reference_mode.part(len(pick_phase), len(phase), '0021B500 cases'),
                          reference_mode.part(len(pick_builders), len(builders), 'builder cases'),
                          '%d jal targets (all translated or worker)' % callees)
    print('capture: PASS (veil block identical in %d captures; seed %08X = one executed 0021B1B0; '
          'phase = %d executed 0021B500 steps from 0)' % (captures, seed, steps))
    print('load veil particles vs original instructions: PASS %d cases (%s), all 32 MB identical '
          'after each, %d worker calls identical, every one of %d conditional branches both ways '
          '(%d outcome(s) unreachable, listed) (%.1fs)' % (
              len(cases), ', '.join('%06X %d' % kv for kv in sorted(entries.items())), calls,
              len(sites), len(UNREACHABLE), time.time() - started))
    return 0


if __name__ == '__main__':
    sys.exit(main())
