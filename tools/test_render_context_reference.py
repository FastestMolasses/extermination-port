#!/usr/bin/env python3
"""Execute the original render-context routines and compare em_render_context.c.

docs/RENDER_CONTEXT.md (census lane L30-render-context). The user's pinned
ELF supplies every instruction; captured original RAM and scratchpad
(build/startup-reference/opening_*, build/s87/route/<beat>/) supply every
world byte. None are embedded here. Routines executed unmodified and
compared with their translations:

  lane      001DD7B0 001DD940 001DD950 001DDA00 001DDAA0 001DDE10 001DEEE0
            001E0C30 001E0C60 001E0C80 001E0CC0 001E0D70 001E0DF0 001E1010
            001D5370 001D52E0
  helpers   001D2910 001D2710 001D2E00 001D2DE0 001D21B0 001D6B10 001D6C90

Workers (every other jal target of these routines; the test asserts the
set) are hooked at their entry and logged with their arguments, in order.
The native module must make the same calls with the same arguments in the
same order. Three kinds:
  bound    the oracle runs the original callee; the native worker is the
           existing port translation (0011DF78 em_sdk_math_original, 001281C0
           em_player_float_to_int, 001D6930 / 001D1F20 / 001D6BA0 / 001D1FF0 /
           001D1F80 / 001006D8 em_load_veil_particles), so their packet
           bytes are produced natively and compared too;
  replay   the oracle runs the original callee and records every byte it
           writes and its result; the native worker checks the arguments and
           applies exactly those bytes (0015D2F0, 0022EBE0, 001B0070,
           001026A0, 001CB760, 001D2D20, 001026D0, 001C6120: other lanes);
  stub     neither side runs it (001D4DA0, 001D4FB0, 001D4B20, 001D5BD0,
           001DE920, 001DDB70, 001DFF70, 001DF110: emission / effect
           callees of other lanes). The translated routines never read what
           these write (their callee trees use no VU register and no
           scratchpad address, checked below), so only the call and its
           arguments are compared.
Nested worker calls inside a running worker are not logged (the native
translation makes its own).

Every case runs the original over one copy of a captured RAM + scratchpad
image and the native module over another copy, then compares the whole
32 MB and the 16 KB scratchpad byte for byte, the worker log and the
return value. Arithmetic: RenderEE (the fall lane's FallEE, imported, not
edited) routes COP1 and VU0 macro arithmetic through tools/ee_float_model.py;
it adds vclipw and the CLIP register read, with the same DAZ'd magnitude
rule the native module uses (not part of the measured model: section 4 of
the doc; an exponent-255 lane fails both sides).

Default run (~10 s): the unit cases over the opening capture (a fixed-seed
sample that still exercises both outcomes of every reachable conditional
branch of the translated routines, asserted; the unreachable ones are
listed with their reason), 001DDA00 / 001E0D70 / 001E0DF0 / 001D52E0 over
the opening capture and every route beat, and 001D5370 over the opening
capture and two route beats. EM_TEST_FULL=1: every unit case and 001D5370
over every route beat too.
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
LANE = os.environ.get('EM_LANE', 'b7-render-context')
OUT = ROOT / 'build' / LANE
RAM_SIZE = 0x2000000
SPAD, SPAD_SIZE = 0x70000000, 0x4000
STACK_TOP = shared.STACK_TOP

# ======================================================================
# Routines, workers, world addresses
# ======================================================================

SIZES = {0x1DD7B0: 0x188, 0x1DD940: 0x8, 0x1DD950: 0x30, 0x1DDA00: 0x9C, 0x1DDAA0: 0xC4,
         0x1DDE10: 0xB08, 0x1DEEE0: 0x134, 0x1E0C30: 0x2C, 0x1E0C60: 0x1C, 0x1E0C80: 0x3C,
         0x1E0CC0: 0x30, 0x1E0D70: 0x7C, 0x1E0DF0: 0x8C, 0x1E1010: 0x88, 0x1D5370: 0x700,
         0x1D52E0: 0x90, 0x1D2910: 0x4C, 0x1D2710: 0x18, 0x1D2E00: 0x14, 0x1D2DE0: 0x14,
         0x1D21B0: 0x2C, 0x1D6B10: 0x44, 0x1D6C90: 0x13C, 0x1D2730: 0x100,
         0x1DEDE0: 0x8}
LANE_FUNCS = [0x1DD7B0, 0x1DD940, 0x1DD950, 0x1DDA00, 0x1DDAA0, 0x1DDE10, 0x1DEEE0, 0x1E0C30,
              0x1E0C60, 0x1E0C80, 0x1E0CC0, 0x1E0D70, 0x1E0DF0, 0x1E1010, 0x1D5370, 0x1D52E0]

BOUND, REPLAY, STUB = 0, 1, 2
# address: (kind, integer argument count (a0..), float argument count (f12..))
WORKERS = {
    0x11DF78: (BOUND, 0, 1), 0x1281C0: (BOUND, 0, 1),
    0x1D6930: (BOUND, 5, 0), 0x1D1F20: (BOUND, 1, 0), 0x1D6BA0: (BOUND, 6, 0),
    0x1D1FF0: (BOUND, 2, 0), 0x1D1F80: (BOUND, 3, 0), 0x1006D8: (BOUND, 6, 0),
    0x15D2F0: (REPLAY, 0, 0), 0x22EBE0: (REPLAY, 0, 0), 0x1B0070: (REPLAY, 0, 0),
    0x1026A0: (REPLAY, 0, 0), 0x1CB760: (REPLAY, 3, 0), 0x1D2D20: (REPLAY, 1, 5),
    0x1026D0: (REPLAY, 3, 0), 0x1C6120: (REPLAY, 2, 0),
    0x1D4DA0: (STUB, 0, 0), 0x1D4FB0: (STUB, 1, 0), 0x1D4B20: (STUB, 1, 0),
    0x1D5BD0: (STUB, 0, 0), 0x1DE920: (STUB, 0, 0), 0x1DDB70: (STUB, 0, 0),
    0x1DFF70: (STUB, 0, 0), 0x1DF110: (STUB, 1, 0),
}
STUBS = [a for a, (k, _, _) in WORKERS.items() if k == STUB]
# Callees the module translates inline where they are called (executed by
# the oracle as part of the caller): 00102948, the quadword copy, and
# 00121870 (block_copy; 001D2730's 32-byte moves).
# 001DEDE0's tail 001DEDF0 and its 001DEDB0 / 001DEE80 / 001DEEC0 are
# translated inside em_render_context_001DEDE0.
INLINE = {0x102948, 0x121870, 0x1DEDF0, 0x1DEDB0, 0x1DEE80, 0x1DEEC0}
# Conditional branches no input reaches, with the reason (doc section 5).
UNREACHABLE = {}
for _site in (0x1DE160, 0x1DE2D8, 0x1DE418, 0x1DE548):
    UNREACHABLE[(_site, False)] = ('001DDE10: the per-slot dispatch falls through only for a slot '
                                   'index outside 0..3; the loop counter is 0..3')

CTX_PTR = 0x275670
FREE = 0x1E00000             # an aligned RAM area for moved cursors / synthetic records
GRID = 0x1D00000             # a synthetic 001D5370 grid (32 x 32 x 4 words)
REC_WORDS = 32

ROUTE = DECOMP / 'build/s87/route'
CAPTURES = [('opening', REFERENCE / 'opening_ee.bin', REFERENCE / 'opening_scratchpad.bin')]
if ROUTE.exists():
    for beat in sorted(p.name for p in ROUTE.iterdir() if (p / 'eeMemory.bin').exists()):
        CAPTURES.append((beat, ROUTE / beat / 'eeMemory.bin', ROUTE / beat / 'scratchpad.bin'))


def in_translated(pc):
    return any(start <= pc < start + size for start, size in SIZES.items())


def jal_targets(ee, start, size):
    out = set()
    for pc in range(start, start + size, 4):
        word = ee.load(pc)
        if word >> 26 in (2, 3):
            target = (word & 0x3FFFFFF) << 2
            if not (start <= target < start + size):
                out.add(target)
    return out


def check_callee_set(elf):
    """The jal/j targets of the translated routines are exactly the routines
    translated here plus the workers, and every worker is called."""
    ee = EE(elf)
    targets = set()
    for start, size in SIZES.items():
        targets |= jal_targets(ee, start, size)
    missing = sorted(t for t in targets if t not in SIZES and t not in WORKERS and t not in INLINE)
    assert not missing, ('callees neither worker nor translated', [hex(t) for t in missing])
    unused = sorted(t for t in WORKERS if t not in targets)
    assert not unused, ('worker addresses no routine calls', [hex(t) for t in unused])
    return len(targets)


def function_sizes():
    """vram -> size from the decomp's FUNCTIONS.csv (addresses and sizes only)."""
    sizes = {}
    for line in (DECOMP / 'docs/FUNCTIONS.csv').read_text().splitlines()[1:]:
        parts = line.split(',')
        sizes[int(parts[0], 16)] = int(parts[2])
    return sizes


def check_stub_trees(elf):
    """The stubbed emission callees reached by 001D5370 (001D4FB0, 001D4B20,
    001D4DA0 and everything they call) never touch a VU register (no COP2,
    lqc2 or sqc2) and never address the two scratchpad clip matrices
    0x70003400..0x7000347F: 001D5370 keeps its matrix in VU registers
    across them and reloads the scratchpad matrices after them."""
    ee = EE(elf)
    sizes = function_sizes()
    seen, todo, scratch_refs = set(), [0x1D4FB0, 0x1D4B20, 0x1D4DA0], set()
    while todo:
        start = todo.pop()
        if start in seen:
            continue
        seen.add(start)
        assert start in sizes, ('no size for', hex(start))
        for pc in range(start, start + sizes[start], 4):
            word = ee.load(pc)
            op = word >> 26
            assert op not in (18, 54, 62), ('COP2 / lqc2 / sqc2 in a stub tree', hex(pc))
            if op == 15 and (word & 0xFFFF) == 0x7000:
                # a scratchpad base: none of its uses may reach the two clip
                # matrices 0x70003400..0x7000347F (the trees read D_70003AC0)
                rx = word >> 16 & 31
                for k in range(1, 12):
                    use = ee.load(pc + 4 * k)
                    if use >> 21 & 31 == rx and use >> 26 not in (0, 28):
                        assert not 0x3400 <= (use & 0xFFFF) < 0x3480, ('clip matrix address in a stub tree', hex(pc))
                        scratch_refs.add(use & 0xFFFF)
            assert not (op == 0 and word & 63 == 9), ('indirect call in a stub tree', hex(pc))
            if op in (2, 3):
                todo.append((word & 0x3FFFFFF) << 2)
    return len(seen), sorted(scratch_refs)


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

def clipw_flags(v):
    """vclipw.xyz against |w|, on DAZ'd magnitude bits (doc section 4);
    None for an exponent-255 lane (not modelled)."""
    if any((x >> 23 & 0xFF) == 0xFF for x in v):
        return None
    daz = lambda x: x & 0x80000000 if (x >> 23 & 0xFF) == 0 else x
    w = daz(v[3]) & 0x7FFFFFFF
    f = 0
    for k in range(3):
        x = daz(v[k])
        if (x & 0x7FFFFFFF) > w:
            f |= (2 if x >> 31 else 1) << (2 * k)
    return f


class RenderEE(FallEE):
    """FallEE (COP1 / VU0 through ee_float_model) over a RAM + scratchpad
    image, plus vclipw and the CLIP register; records stored ranges, branch
    outcomes inside the translated routines, and the worker log."""

    def __init__(self, elf, ram, spad):
        super().__init__(elf, ram, spad)
        self.clip = 0
        self.outcomes = set()
        self.log = []
        self.dirty = []
        self.depth = 0
        for address, (kind, _, _) in WORKERS.items():
            self.hooks[address] = self._worker(address, kind)

    def _track(self, address, size):
        address &= MASK
        if address < 0x40000000:
            self.dirty.append(('ram', address & (RAM_SIZE - 1), size))
        elif SPAD <= address < SPAD + SPAD_SIZE:
            self.dirty.append(('spad', address - SPAD, size))

    def save(self, address, value, size=4):
        self._track(address, size)
        super().save(address, value, size)

    def write(self, address, data):
        self._track(address, len(data))
        super().write(address, data)

    def branch(self, word, pc):
        b = super().branch(word, pc)
        if b is not None and self.depth == 0 and in_translated(pc):
            self.outcomes.add((pc, b[0]))
        return b

    def macro(self, word):
        op, fs, ft, fd = word & 63, word >> 11 & 31, word >> 16 & 31, word >> 6 & 31
        if op >= 60 and ((fd << 2) | (op & 3)) == 0x1F:          # vclipw (x, y, z)
            assert (word >> 21 & 15) == 0xE, ('vclipw mask', hex(word))
            v = [self.vf[fs][0], self.vf[fs][1], self.vf[fs][2], self.vf[ft][3]]
            f = clipw_flags([x & MASK for x in v])
            if f is None:
                raise AssertionError(('vclipw on an exponent-255 lane (not modelled)', [hex(x) for x in v]))
            self.clip = ((self.clip << 6) | f) & 0xFFFFFF
            return
        super().macro(word)

    def cop2(self, word, pc):
        rs, rt, rd = word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        if rs in (2, 6):                                           # CLIP register read / write
            assert rd == 18, ('COP2 control register', rd, hex(pc))
            if rs == 2:
                if rt:
                    self.r[rt] = sx32(self.clip)
            else:
                self.clip = self.r[rt] & 0xFFFFFF
            return
        super().cop2(word, pc)

    def _worker(self, address, kind):
        def hook(ee):
            nargs, nf = WORKERS[address][1], WORKERS[address][2]
            if ee.depth > 0:                           # nested in a running worker: transparent
                assert kind != STUB, ('stubbed worker reached inside a worker', hex(address))
                ret = ee.r[31]
                ee.hooks.pop(address)
                try:
                    ee.r[31] = shared.RETURN
                    ee.run(address)
                finally:
                    ee.hooks[address] = hook
                ee.r[31] = ret
                return
            args = [ee.r[4 + i] & MASK for i in range(8)]
            fargs = [ee.f[12 + i] & MASK for i in range(5)]
            rec = {'addr': address, 'kind': kind, 'a': args[:nargs] + [0] * (8 - nargs),
                   'f': fargs[:nf] + [0] * (5 - nf), 'in': [0] * 4, 'out': [0] * 4,
                   'v0': 0, 'f0': 0, 'writes': []}
            if address == 0x1026A0:
                rec['a'][1] = args[1]                   # the matrix address
                rec['in'] = [ee.load(((args[2] & ~15) + 4 * i) & MASK) for i in range(4)]
            if kind == STUB:
                ee.r[2] = 0
            else:
                ret = ee.r[31]
                mark = len(ee.dirty)
                ee.depth += 1
                ee.hooks.pop(address)
                try:
                    ee.r[31] = shared.RETURN
                    ee.run(address)
                finally:
                    ee.hooks[address] = hook
                    ee.depth -= 1
                ee.r[31] = ret
                rec['v0'] = ee.r[2] & MASK
                rec['f0'] = ee.f[0] & MASK
                if address == 0x1026A0:
                    rec['out'] = [ee.load(((args[0] & ~15) + 4 * i) & MASK) for i in range(4)]
                if kind == REPLAY:
                    spans = {}
                    for where, at, size in ee.dirty[mark:]:
                        for b in range(at, at + size):
                            spans[(where, b)] = True
                    for where in ('ram', 'spad'):
                        offs = sorted(b for (w, b) in spans if w == where)
                        i = 0
                        while i < len(offs):
                            j = i
                            while j + 1 < len(offs) and offs[j + 1] == offs[j] + 1:
                                j += 1
                            base = offs[i] + (0 if where == 'ram' else SPAD)
                            buf = ee.mem if where == 'ram' else ee.spad
                            rec['writes'].append((base, bytes(buf[offs[i]:offs[j] + 1])))
                            i = j + 1
            ee.log.append(rec)
        return hook

    def invoke(self, entry, args=(), fargs=(), stack=()):
        self.r = [0] * 32
        self.rh = [0] * 32
        self.r[28] = 0x27D370
        self.r[29] = STACK_TOP
        self.clip = 0
        for i, value in enumerate(args):
            self.r[4 + i] = sx32(value)
        for i, value in enumerate(fargs):
            self.f[12 + i] = value & MASK
        for i, value in enumerate(stack):             # the caller's outgoing stack words (sd)
            self.save(STACK_TOP + 8 * i, sx32(value) & 0xFFFFFFFFFFFFFFFF, 8)
        self.r[31] = shared.RETURN
        self.run(entry)
        return self.r[2] & MASK


# ======================================================================
# The native module (shim built privately under build/<lane>)
# ======================================================================

SHIM = r'''
#include <string.h>
#include "game/em_render_context.h"
#include "game/em_load_veil_particles.h"
#include "game/em_sdk_math_original.h"
#include "game/em_player_stage_workers.h"

#define RW 32
enum { BOUND, REPLAY, STUB };
typedef struct {
    uint8_t *ram, *spad;
    const uint32_t *rec;      /* RW words per oracle worker call */
    uint32_t nrec, next;
    const uint32_t *wtab;     /* (address, length, blob offset) per write */
    const uint8_t *blob;
    uint32_t mismatch;        /* 1 + index of the first mismatching call */
    EmLoadVeilParticles lvp;
} Replay;

static const uint32_t *take(Replay *r, uint32_t address)
{
    if (r->mismatch) return NULL;
    if (r->next >= r->nrec || r->rec[RW * r->next] != address) {
        r->mismatch = 1 + r->next;
        return NULL;
    }
    return &r->rec[RW * r->next++];
}

static int args_ok(Replay *r, const uint32_t *c, const uint32_t *a, int n, const uint32_t *f, int nf)
{
    for (int i = 0; i < n; ++i) if (c[1 + i] != a[i]) { r->mismatch = r->next; return 0; }
    for (int i = 0; i < nf; ++i) if (c[9 + i] != f[i]) { r->mismatch = r->next; return 0; }
    return 1;
}

static void apply(Replay *r, const uint32_t *c)
{
    for (uint32_t k = 0; k < c[25]; ++k) {
        const uint32_t *w = &r->wtab[3 * (c[24] + k)];
        uint8_t *dst = w[0] >= 0x70000000u ? r->spad + (w[0] - 0x70000000u) : r->ram + w[0];
        memcpy(dst, r->blob + w[2], w[1]);
    }
}

#define GET(address, n, nf, ...) \
    Replay *r = ctx; const uint32_t a_[8] = {__VA_ARGS__}; \
    const uint32_t *c = take(r, address); \
    if (!c || !args_ok(r, c, a_, n, NULL, 0)) return -1; (void)nf

static int w_15D2F0(void *ctx, int32_t *out) { GET(0x15D2F0u, 0, 0, 0); apply(r, c); *out = (int32_t)c[14]; return 0; }
static int w_22EBE0(void *ctx, int32_t *out) { GET(0x22EBE0u, 0, 0, 0); apply(r, c); *out = (int32_t)c[14]; return 0; }
static int w_1B0070(void *ctx, uint32_t *out) { GET(0x1B0070u, 0, 0, 0); apply(r, c); *out = c[14]; return 0; }
static int w_1026A0(void *ctx, uint32_t out[4], uint32_t m, const uint32_t v[4])
{
    Replay *r = ctx;
    const uint32_t *c = take(r, 0x1026A0u);
    if (!c) return -1;
    if (c[2] != m || memcmp(&c[16], v, 16) != 0) { r->mismatch = r->next; return -1; }
    apply(r, c);
    memcpy(out, &c[20], 16);
    return 0;
}
static int w_11DF78(void *ctx, uint32_t x, uint32_t *out)
{
    Replay *r = ctx;
    const uint32_t *c = take(r, 0x11DF78u);
    if (!c || !args_ok(r, c, NULL, 0, &x, 1)) return -1;
    float f, g;
    memcpy(&f, &x, 4);
    g = em_sdk_math_original_0011DF78(f);
    memcpy(out, &g, 4);
    if (*out != c[15]) { r->mismatch = r->next; return -1; }
    return 0;
}
static int w_1281C0(void *ctx, uint32_t x, int32_t *out)
{
    Replay *r = ctx;
    const uint32_t *c = take(r, 0x1281C0u);
    if (!c || !args_ok(r, c, NULL, 0, &x, 1)) return -1;
    *out = em_player_float_to_int(x);
    if ((uint32_t)*out != c[14]) { r->mismatch = r->next; return -1; }
    return 0;
}
static int w_1D6930(void *ctx, int32_t a0, int32_t a1, int32_t a2, int32_t a3, uint32_t t0, uint32_t *out)
{
    GET(0x1D6930u, 5, 0, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3, t0);
    if (em_load_veil_particles_001D6930(&r->lvp, a0, a1, a2, a3, r->ram + (t0 & 0x1FFFFFFu), out) < 0) return -1;
    if (*out != c[14]) { r->mismatch = r->next; return -1; }
    return 0;
}
static int w_1D1F20(void *ctx, int32_t a0)
{
    GET(0x1D1F20u, 1, 0, (uint32_t)a0);
    (void)c;
    return em_load_veil_particles_001D1F20(&r->lvp, a0);
}
static int w_1D6BA0(void *ctx, int32_t a0, int32_t a1, int32_t a2, int32_t a3, int32_t t0, int32_t t1)
{
    GET(0x1D6BA0u, 6, 0, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3, (uint32_t)t0, (uint32_t)t1);
    uint32_t res;
    (void)c;
    return em_load_veil_particles_001D6BA0(&r->lvp, a0, a1, a2, a3, t0, t1, &res);
}
static int w_1D1FF0(void *ctx, int32_t a0, int32_t a1)
{
    GET(0x1D1FF0u, 2, 0, (uint32_t)a0, (uint32_t)a1);
    (void)c;
    return em_load_veil_particles_001D1FF0(&r->lvp, a0, a1);
}
static int w_1D1F80(void *ctx, int32_t a0, int32_t a1, int32_t a2)
{
    GET(0x1D1F80u, 3, 0, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2);
    (void)c;
    return em_load_veil_particles_001D1F80(&r->lvp, a0, a1, a2);
}
static int w_1006D8(void *ctx, uint32_t env, int32_t psm, int32_t w, int32_t h, int32_t t0, int32_t t1)
{
    GET(0x1006D8u, 6, 0, env, (uint32_t)psm, (uint32_t)w, (uint32_t)h, (uint32_t)t0, (uint32_t)t1);
    int32_t res;
    (void)c;
    return em_load_veil_particles_001006D8(&r->lvp, env, psm, w, h, t0, t1, &res);
}
static int w_1CB760(void *ctx, uint32_t a0, int32_t a1, uint32_t a2)
{
    GET(0x1CB760u, 3, 0, a0, (uint32_t)a1, a2);
    apply(r, c);
    return 0;
}
static int w_1D2D20(void *ctx, uint32_t m, uint32_t f12, uint32_t f13, uint32_t f14, uint32_t f15, uint32_t f16)
{
    Replay *r = ctx;
    const uint32_t f[5] = {f12, f13, f14, f15, f16};
    const uint32_t *c = take(r, 0x1D2D20u);
    if (!c || !args_ok(r, c, &m, 1, f, 5)) return -1;
    apply(r, c);
    return 0;
}
static int w_1026D0(void *ctx, uint32_t d, uint32_t a, uint32_t b) { GET(0x1026D0u, 3, 0, d, a, b); apply(r, c); return 0; }
static int w_1C6120(void *ctx, uint32_t bank, int32_t id, uint32_t *out)
{
    GET(0x1C6120u, 2, 0, bank, (uint32_t)id);
    apply(r, c);
    *out = c[14];
    return 0;
}
static int w_1D4DA0(void *ctx) { GET(0x1D4DA0u, 0, 0, 0); (void)c; return 0; }
static int w_1D4FB0(void *ctx, uint32_t o) { GET(0x1D4FB0u, 1, 0, o); (void)c; return 0; }
static int w_1D4B20(void *ctx, uint32_t o) { GET(0x1D4B20u, 1, 0, o); (void)c; return 0; }
static int w_1D5BD0(void *ctx) { GET(0x1D5BD0u, 0, 0, 0); (void)c; return 0; }
static int w_1DE920(void *ctx) { GET(0x1DE920u, 0, 0, 0); (void)c; return 0; }
static int w_1DDB70(void *ctx) { GET(0x1DDB70u, 0, 0, 0); (void)c; return 0; }
static int w_1DFF70(void *ctx) { GET(0x1DFF70u, 0, 0, 0); (void)c; return 0; }
static int w_1DF110(void *ctx, uint32_t p) { GET(0x1DF110u, 1, 0, p); (void)c; return 0; }

static uint32_t word(const uint8_t *ram, uint32_t a) { uint32_t v; memcpy(&v, ram + (a & 0x1FFFFFFu), 4); return v; }
#define U32(a) ((uint32_t *)(void *)(ram + ((a) & 0x1FFFFFFu)))

int rc_run(uint8_t *ram, uint8_t *spad, uint32_t fn, const uint32_t *a, const uint32_t *f,
           const uint32_t *rec, uint32_t nrec, const uint32_t *wtab, const uint8_t *blob,
           uint32_t *used, uint32_t *mismatch, uint32_t *fault, uint32_t *result, int null_worker)
{
    EmRenderContext s;
    Replay r;
    EmRenderContextView views[2] = {{0u, 0x2000000u, ram}, {0x70000000u, 0x4000u, spad}};
    uint32_t ctx = word(ram, 0x275670u), res = 0;
    int rc = -1;
    memset(&s, 0, sizeof s);
    memset(&r, 0, sizeof r);
    r.ram = ram; r.spad = spad; r.rec = rec; r.nrec = nrec; r.wtab = wtab; r.blob = blob;
    r.lvp.world.cursor = U32(ctx + 0x10u);
    r.lvp.world.cursor_count = 4;
    r.lvp.world.ctx_9C = U32(ctx + 0x9Cu);
    r.lvp.world.d00275674 = U32(0x275674u);
    r.lvp.world.d0027568C = U32(0x27568Cu);
    r.lvp.world.d0026E880 = ram + 0x26E880u;
    r.lvp.world.d00241010 = ram + 0x241010u;
    r.lvp.world.packet = ram;
    r.lvp.world.packet_address = 0;
    r.lvp.world.packet_size = 0x2000000u;
    s.world.ctx = ctx;
    s.world.views = views;
    s.world.view_count = 2;
    EmRenderContextWorkers *w = &s.workers;
    w->ctx = &r;
    w->w_0015D2F0 = w_15D2F0; w->w_0022EBE0 = w_22EBE0; w->w_001B0070 = w_1B0070;
    w->w_001026A0 = w_1026A0; w->w_0011DF78 = w_11DF78; w->w_001281C0 = w_1281C0;
    w->w_001D6930 = w_1D6930; w->w_001D1F20 = w_1D1F20; w->w_001D6BA0 = w_1D6BA0;
    w->w_001D1FF0 = w_1D1FF0; w->w_001D1F80 = w_1D1F80; w->w_001006D8 = w_1006D8;
    w->w_001CB760 = w_1CB760; w->w_001D2D20 = w_1D2D20; w->w_001026D0 = w_1026D0;
    w->w_001C6120 = w_1C6120; w->w_001D4DA0 = w_1D4DA0; w->w_001D4FB0 = w_1D4FB0;
    w->w_001D4B20 = w_1D4B20; w->w_001D5BD0 = w_1D5BD0; w->w_001DE920 = w_1DE920;
    w->w_001DDB70 = w_1DDB70; w->w_001DFF70 = w_1DFF70; w->w_001DF110 = w_1DF110;
    if (null_worker >= 0) {   /* fail-stop probe: one worker unbound */
        int (**slots)(void) = (int (**)(void))(void *)&w->w_0015D2F0;
        slots[null_worker] = NULL;
    }
    const int32_t *i = (const int32_t *)a;
    switch (fn) {
    case 0x1DD7B0u: rc = em_render_context_001DD7B0(&s); break;
    case 0x1DD940u: rc = em_render_context_001DD940(&s); break;
    case 0x1DD950u: rc = em_render_context_001DD950(&s, a[0], f[0], f[1]); break;
    case 0x1DDA00u: rc = em_render_context_001DDA00(&s); break;
    case 0x1DDAA0u: rc = em_render_context_001DDAA0(&s); break;
    case 0x1DDE10u: rc = em_render_context_001DDE10(&s); break;
    case 0x1DEEE0u: rc = em_render_context_001DEEE0(&s, a[0]); break;
    case 0x1E0C30u: rc = em_render_context_001E0C30(&s); break;
    case 0x1E0C60u: rc = em_render_context_001E0C60(&s, i[0], &res); break;
    case 0x1E0C80u: rc = em_render_context_001E0C80(&s, i[0], i[1], &res); break;
    case 0x1D2730u: rc = em_render_context_001D2730(&s, i[0], i[1], &res); break;
    case 0x1DEDE0u: rc = em_render_context_001DEDE0(&s); break;
    case 0x1E0CC0u: rc = em_render_context_001E0CC0(&s); break;
    case 0x1E0D70u: rc = em_render_context_001E0D70(&s); break;
    case 0x1E0DF0u: rc = em_render_context_001E0DF0(&s); break;
    case 0x1E1010u: rc = em_render_context_001E1010(&s); break;
    case 0x1D5370u: rc = em_render_context_001D5370(&s); break;
    case 0x1D52E0u: rc = em_render_context_001D52E0(&s); break;
    case 0x1D2910u: rc = em_render_context_001D2910(&s, i[0], &res); break;
    case 0x1D2710u: rc = em_render_context_001D2710(&s, i[0], &res); break;
    case 0x1D2E00u: rc = em_render_context_001D2E00(&s, i[0], &res); break;
    case 0x1D2DE0u: rc = em_render_context_001D2DE0(&s, i[0], a[1]); break;
    case 0x1D21B0u: rc = em_render_context_001D21B0(&s, a[0]); break;
    case 0x1D6B10u: rc = em_render_context_001D6B10(&s, i[0], i[1], i[2], i[3], &res); break;
    case 0x1D6C90u: rc = em_render_context_001D6C90(&s, i, &res); break;
    default: break;
    }
    *used = r.next;
    *mismatch = r.mismatch;
    fault[0] = s.fault.address;
    fault[1] = (uint32_t)s.fault.code;
    *result = res;
    return rc;
}

int rc_clipw(const uint32_t *v, uint32_t *flags) { return em_render_context_clipw(v, flags); }
'''


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    source = OUT / 'rc_shim.c'
    source.write_text(SHIM)
    lib = OUT / ('rc.dylib' if sys.platform == 'darwin' else 'rc.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                    '-Wno-cast-function-type', '-ffp-contract=off', '-shared', '-fPIC', '-Isrc',
                    str(source), 'src/game/em_render_context.c', 'src/game/em_load_veil_particles.c',
                    'src/game/em_sdk_math_original.c', 'src/game/em_player_stage_workers.c',
                    '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    P = C.POINTER
    native.rc_run.argtypes = [C.c_void_p, C.c_void_p, C.c_uint32, P(C.c_uint32), P(C.c_uint32),
                              P(C.c_uint32), C.c_uint32, P(C.c_uint32), C.c_char_p,
                              P(C.c_uint32), P(C.c_uint32), P(C.c_uint32), P(C.c_uint32), C.c_int]
    native.rc_run.restype = C.c_int
    native.rc_clipw.argtypes = [P(C.c_uint32), P(C.c_uint32)]
    native.rc_clipw.restype = C.c_int
    return native


# Worker slot order in EmRenderContextWorkers (after ctx), for the fail-stop probe.
SLOT_ORDER = [0x15D2F0, 0x22EBE0, 0x1B0070, 0x1026A0, 0x11DF78, 0x1281C0, 0x1D6930, 0x1D1F20,
              0x1D6BA0, 0x1D1FF0, 0x1D1F80, 0x1006D8, 0x1CB760, 0x1D2D20, 0x1026D0, 0x1C6120,
              0x1D4DA0, 0x1D4FB0, 0x1D4B20, 0x1D5BD0, 0x1DE920, 0x1DDB70, 0x1DFF70, 0x1DF110]


def pack_log(log):
    recs, wtab, blob = [], [], bytearray()
    for rec in log:
        row = [0] * REC_WORDS
        row[0] = rec['addr']
        row[1:9] = rec['a']
        row[9:14] = rec['f']
        row[14], row[15] = rec['v0'], rec['f0']
        row[16:20] = rec['in']
        row[20:24] = rec['out']
        row[24] = len(wtab) // 3
        for address, data in rec['writes']:
            wtab += [address, len(data), len(blob)]
            blob += data
        row[25] = len(wtab) // 3 - row[24]
        row[26] = rec['kind']
        recs += row
    return ((C.c_uint32 * max(1, len(recs)))(*recs), len(log),
            (C.c_uint32 * max(1, len(wtab)))(*wtab), bytes(blob) or b'\0')


# ======================================================================
# One case: original and native over the same image
# ======================================================================

ELF = NATIVE = None
_STATE = {}


def state(capture):
    """Per-process oracle and native images of one capture."""
    if _STATE.get('name') != capture:
        _STATE.clear()
        label, ee_path, spad_path = next(c for c in CAPTURES if c[0] == capture)
        base, sbase = ee_path.read_bytes(), spad_path.read_bytes()
        assert len(base) == RAM_SIZE and len(sbase) == SPAD_SIZE, ('capture size', label)
        ee = RenderEE(ELF, base, sbase)
        mem, spad = bytearray(base), bytearray(sbase)
        _STATE.update(name=capture, base=base, sbase=sbase, ee=ee, mem=mem, spad=spad,
                      pmem=(C.c_ubyte * RAM_SIZE).from_buffer(mem),
                      pspad=(C.c_ubyte * SPAD_SIZE).from_buffer(spad))
    return _STATE


def restore(st, ranges):
    ee, base, sbase = st['ee'], st['base'], st['sbase']
    for where, at, size in ranges:
        if where == 'ram':
            end = min(RAM_SIZE, at + size)
            ee.mem[at:end] = base[at:end]
            st['mem'][at:end] = base[at:end]
        else:
            end = min(SPAD_SIZE, at + size)
            ee.spad[at:end] = sbase[at:end]
            st['spad'][at:end] = sbase[at:end]


RESULT_FUNCS = {0x1E0C60, 0x1E0C80, 0x1D2910, 0x1D2710, 0x1D2E00, 0x1D6B10, 0x1D6C90, 0x1D2730}


def run_one(st, case):
    """case = (label, capture, entry, args, fargs, pokes). pokes: [(address,
    bytes)] (address >= 0x70000000 is the scratchpad), applied to both
    images first. Returns (outcomes, worker log summary)."""
    label, _capture, entry, args, fargs, pokes = case
    ee, mem, spad = st['ee'], st['mem'], st['spad']
    ee.dirty, ee.log, ee.outcomes = [], [], set()
    for address, data in pokes:
        ee.write(address, data)
        if address >= SPAD:
            spad[address - SPAD:address - SPAD + len(data)] = data
        else:
            mem[address:address + len(data)] = data
    touched = list(ee.dirty)
    try:
        stack = list(args[8:15]) if entry == 0x1D6C90 else ()
        v0 = ee.invoke(entry, list(args[:8]), fargs, stack)
        recs, nrec, wtab, blob = pack_log(ee.log)
        a = (C.c_uint32 * 16)(*[x & MASK for x in list(args) + [0] * (16 - len(args))])
        f = (C.c_uint32 * 5)(*[x & MASK for x in list(fargs) + [0] * (5 - len(fargs))])
        used, mismatch, fault, result = C.c_uint32(), C.c_uint32(), (C.c_uint32 * 2)(), C.c_uint32()
        rc = NATIVE.rc_run(st['pmem'], st['pspad'], entry, a, f, recs, nrec, wtab, blob,
                           C.byref(used), C.byref(mismatch), fault, C.byref(result), -1)
        if mismatch.value:
            k = mismatch.value - 1
            want = ee.log[k] if k < len(ee.log) else None
            raise AssertionError((label, 'worker call %d differs' % k,
                                  'original', want and (hex(want['addr']), [hex(x) for x in want['a']],
                                                        [hex(x) for x in want['f']])))
        assert rc == 0, (label, 'native faulted', hex(fault[0]), fault[1])
        assert used.value == len(ee.log), (label, 'worker calls', used.value, len(ee.log))
        if mem != ee.mem:
            diff = next(i for i in range(RAM_SIZE) if mem[i] != ee.mem[i])
            raise AssertionError((label, 'RAM differs first at', hex(diff),
                                  'native', mem[diff & ~15:(diff & ~15) + 16].hex(),
                                  'original', bytes(ee.mem[diff & ~15:(diff & ~15) + 16]).hex()))
        if spad != ee.spad:
            diff = next(i for i in range(SPAD_SIZE) if spad[i] != ee.spad[i])
            raise AssertionError((label, 'scratchpad differs first at', hex(SPAD + diff)))
        if entry in RESULT_FUNCS:
            assert result.value == v0, (label, 'return value', hex(result.value), hex(v0))
        if entry == 0x1D52E0 and not pokes:
            # capture evidence: the captured grid header is exactly what one
            # executed 001D52E0 writes (it has not changed since the load)
            c = struct.unpack_from('<I', st['base'], CTX_PTR)[0] & (RAM_SIZE - 1)
            assert bytes(ee.mem[c + 0x140:c + 0x168]) == st['base'][c + 0x140:c + 0x168], (
                label, 'captured grid header differs from 001D52E0')
        calls = {}
        for rec in ee.log:
            calls[rec['addr']] = calls.get(rec['addr'], 0) + 1
        return ee.outcomes, calls
    finally:
        restore(st, touched + ee.dirty)


def run_group(group):
    """One capture's cases in one process."""
    capture, cases = group
    st = state(capture)
    outcomes, entries, calls, per = set(), {}, {}, []
    for case in cases:
        cover, counted = run_one(st, case)
        outcomes |= cover
        entries[case[2]] = entries.get(case[2], 0) + 1
        for k, n in counted.items():
            calls[k] = calls.get(k, 0) + n
        per.append((case[0], counted))
    return outcomes, entries, calls, per


# ======================================================================
# Cases
# ======================================================================

def w32(value):
    return struct.pack('<I', value & MASK)


def f32(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def ctx_of(capture='opening'):
    path = next(c for c in CAPTURES if c[0] == capture)[1]
    with open(path, 'rb') as fh:
        fh.seek(CTX_PTR)
        return struct.unpack('<I', fh.read(4))[0] & (RAM_SIZE - 1)


FLOATS = [0.0, -0.0, 1.0, -1.0, 0.5, 62.0, 100.0, 1e-39, 3.0e38, -3.0e38, 1234.5, -77.25, 8500.0,
          40500.0, 16777215.0, 1e6, 0.001, 2.5]


def rfloat(rng):
    if rng.random() < 0.35:
        return f32(rng.choice(FLOATS))
    if rng.random() < 0.1:
        return rng.getrandbits(32) & 0x7F7FFFFF | (rng.getrandbits(1) << 31)
    return f32(rng.uniform(-2e5, 2e5))


def mode_pokes(rng, mode):
    """Pokes that make 0015D2F0 return `mode` (its record D_008102B0,
    active byte D_008102B4, type +5, sub-states +0x1F1 / +0x318)."""
    base = 0x8102B0
    table = {0: (0, 0, 0, 0), 1: (1, 29, 0, 2), 2: (1, 30, 0, 2), 3: (1, 25, 0, 0), 0x82: (1, 35, 1, 0)}
    act, typ, s1f1, s318 = table[mode]
    return [(base + 4, bytes([act])), (base + 5, bytes([typ])), (base + 0x1F1, bytes([s1f1])),
            (base + 0x318, bytes([s318]))]


def unit_cases(rng, scale):
    c = ctx_of()
    out = []

    def add(label, entry, args=(), fargs=(), pokes=()):
        out.append((label, 'opening', entry, list(args), list(fargs), list(pokes)))

    def flag_pokes():
        return [(c + 0xC, w32(rng.getrandbits(32))), (c + 0x174, w32(rng.getrandbits(32)))]

    # flag words
    for k in range(40 * scale):
        a0 = rng.choice([-1, -0x80000000, 0x7FFFFFFF, 0x40, 0x41, 0x1F, 0x20, 0x3F, rng.randrange(-8, 0x48)])
        add('001D2910 #%d' % k, 0x1D2910, [a0], pokes=flag_pokes())
    for k in range(8 * scale):
        add('001D2710 #%d' % k, 0x1D2710, [rng.randrange(-40, 40)], pokes=flag_pokes())
        add('001E0C60 #%d' % k, 0x1E0C60, [rng.randrange(0, 0x60)], pokes=flag_pokes())
    for rep in range(4 * scale):
        for a1 in (0, 1, -1, 2):
            for was in (0, 1):             # the old bit, forced (the result is it, both ways)
                a0 = rng.choice([rng.randrange(0x20, 0x40), rng.randrange(0x18, 0x48)])
                bit = 1 << ((a0 - 0x20) & 31)
                old = rng.getrandbits(32) & ~bit | (bit if was else 0)
                add('001E0C80 %d %d|#%d' % (a1, was, rep), 0x1E0C80, [a0, a1],
                    pokes=[(c + 0x174, w32(old))])
    # 001D2730: every a0 case of its dispatch, both a1 senses and the old bit
    # forced both ways; the three 32-byte blocks are random so each move is
    # observable (one case per combination and repetition).
    for rep in range(scale):
        for a0 in (0, 1, 2, 3, 4, 5, 0x1F, 0x20, 0x21, -1):
            for a1 in (0, 1, -1):
                for was in (0, 1):
                    bit = 1 << (a0 & 31)
                    old = rng.getrandbits(32) & ~bit | (bit if was else 0)
                    pokes = [(c + 0xC, w32(old))] + [(c + off, bytes(rng.getrandbits(8) for _ in range(0x20)))
                                                     for off in (0xA0, 0xC0, 0x100)]
                    add('001D2730 %d %d %d|#%d' % (a0, a1, was, rep), 0x1D2730, [a0, a1], pokes=pokes)
    # 001DEDE0: both ramp records and the D_0026E850 colour words random
    for k in range(2 * scale):
        add('001DEDE0 #%d' % k, 0x1DEDE0,
            pokes=[(c + 0x2470, bytes(rng.getrandbits(8) for _ in range(0x40))),
                   (0x26E850, bytes(rng.getrandbits(8) for _ in range(12)))])
    # +0x2520 words, 001D21B0
    for k in range(6 * scale):
        a0 = rng.choice([0, 1, 2, -1])
        add('001D2E00 #%d' % k, 0x1D2E00, [a0], pokes=[(c + 0x2520 + 4 * a0, w32(rng.getrandbits(32)))])
        add('001D2DE0 #%d' % k, 0x1D2DE0, [a0, rng.getrandbits(32)])
        add('001D21B0 #%d' % k, 0x1D21B0, [rng.getrandbits(32)],
            pokes=[(c + 8, w32(FREE + 16 * rng.randrange(0x100) + rng.choice([0, 0, 4, 8])))])
    # packet builders
    for k in range(10 * scale):
        chan = rng.choice((0, 1, 2, 3))
        pokes = [(c + 0x10 + 4 * chan, w32(FREE + 16 * rng.randrange(0x400))),
                 (c + 0x9C, w32(rng.choice((0, 1, 2))))]
        add('001D6B10 #%d' % k, 0x1D6B10, [chan, rng.choice([0, 0x3FFE000, rng.getrandbits(32)]),
                                           rng.choice([8, 1, 0x100, -1]), rng.choice([8, 2, 0x1C0])], pokes=pokes)
        args = [chan] + [rng.choice([0, 1, 2, 3, -1, 0x7FFFFFFF, -0x80000000, rng.getrandbits(32)])
                         for _ in range(14)]
        add('001D6C90 #%d' % k, 0x1D6C90, args, pokes=pokes[:1])
    # 001DD7B0 / 001DD940
    for k in range(4 * scale):
        pokes = [(0x81C050 + 16 * i, bytes(rng.getrandbits(8) for _ in range(16))) for i in range(10)]
        pokes += [(0x27568C, w32(rng.choice([0x3FFE000, 0x80002000, rng.getrandbits(32)]))),
                  (c + 0x24F0, bytes(rng.getrandbits(8) for _ in range(0x24)))]
        add('001DD7B0 #%d' % k, 0x1DD7B0, pokes=pokes)
        add('001DD940 #%d' % k, 0x1DD940, pokes=pokes)
    # 001DD950: every divisor class (tagged), random sources
    for rep in range(2 * scale):
        for n, divisor in enumerate(FLOATS + [None, None]):
            src = rng.choice([0x8105E0, FREE + 0x40, FREE + 0x48])
            f12 = f32(divisor) if divisor is not None else rng.getrandbits(32)
            add('001DD950 %d|#%d' % (n, rep), 0x1DD950, [src], [f12, rfloat(rng)],
                pokes=[(src & ~15, bytes(rng.getrandbits(8) for _ in range(16)))])
    # 001DEEE0
    for k in range(40 * scale):
        p = rng.choice([c + 0x2470, c + 0x2490, FREE + 0x100])
        state_ = rng.choice([0, 1, 2, 3, 4, 0xFF])
        n = rng.choice([0, 8, 7, 9, 96, 88, 95, 100, -4, rng.randrange(-20, 120)])
        limit = rng.choice([96, 0, 8, 100, rng.randrange(-8, 128)])
        flag = rng.choice([2, 9, 7, 0x21, 0x3F, 0x40, rng.randrange(0, 0x48)])
        pokes = flag_pokes() + [(p, bytes([state_])), (p + 4, w32(limit)), (p + 8, w32(flag)),
                                (p + 0x1C, w32(n))]
        add('001DEEE0 %d|#%d' % (state_, k), 0x1DEEE0, [p], pokes=pokes)
    # 001E0C30 / 001E1010 / 001E0CC0
    for k in range(2 * scale):
        junk = [(0x81E0F0 + 0x180 * r, bytes(rng.getrandbits(8) for _ in range(0x180))) for r in range(16)]
        add('001E1010 #%d' % k, 0x1E1010, pokes=junk)
        add('001E0C30 #%d' % k, 0x1E0C30, pokes=junk + [(c + 0x170, bytes(rng.getrandbits(8) for _ in range(8)))])
        add('001E0CC0 #%d' % k, 0x1E0CC0, pokes=[(c + 0x1D8, w32(rng.getrandbits(32))),
                                                 (c + 0x1E8, w32(rng.getrandbits(32))),
                                                 (c + 0x2520, w32(rng.getrandbits(32)))])
    # 001E0D70 / 001E0DF0: every arm once per repetition (tagged)
    for rep in range(scale):
        for pend in (0, FREE + 0x800, 0x12345670):
            for bit in (0, 1):
                for c8 in (0, 0x02000000, 0x20081910, 0x0E000000):
                    hi = rng.getrandbits(32) & ~1 | bit
                    pokes = [(c + 0x2520, w32(pend)), (c + 0x174, w32(hi)), (0x8106C8, w32(c8))]
                    add('001E0D70 %X %d %X|#%d' % (pend, bit, c8, rep), 0x1E0D70, pokes=pokes)
        for bit in (0, 1):
            for m in range(8):
                hi = rng.getrandbits(32) & ~1 | bit
                lo = rng.getrandbits(32) & ~0x20 | (m >> 2 & 1) << 5
                pokes = [(c + 0x174, w32(hi)), (c + 0x1D8, w32(rng.getrandbits(32) if m & 1 else 0)),
                         (c + 0x1E8, w32(rng.getrandbits(32) if m & 2 else 0)),
                         (c + 0x2520, w32(rng.getrandbits(32) if (m + rep) & 1 else 0)),
                         (c + 0xC, w32(lo)), (c + 8, w32(FREE + 0x1000 + 16 * rng.randrange(64)))]
                add('001E0DF0 %d %d|#%d' % (bit, m, rep), 0x1E0DF0, pokes=pokes)
    # 001D52E0
    for k in range(2 * scale):
        add('001D52E0 #%d' % k, 0x1D52E0, pokes=[(c + 0x140, bytes(rng.getrandbits(8) for _ in range(0x28)))])
    # 001DDE10 / 001DDAA0 / 001DDA00
    keys = [(0x0B, 0x00), (0x0C, 0x00), (0x0D, 0x00), (0x0E, 0x00), (0x0B, 0x01), (0x0F, 0x02), (0x13, 0x00),
            (0x15, 0x00), (0x11, 0x00)]
    for rep in range(scale):
        for ki, key in enumerate(keys):
            for mi, mode in enumerate((0, 1, 2, 3, 0x82)):
                # flags 1 / 6 / 7 and D_008106C6 vary with the key and mode so
                # one case per (entry, key, mode) still takes every arm
                mix = ki + mi + rep
                low = rng.getrandbits(32) & ~0xC2 | 2 | (mix & 1) << 6 | (mix >> 1 & 1) << 7
                for entry in (0x1DDE10, 0x1DDAA0, 0x1DDA00):
                    pokes = mode_pokes(rng, mode) + [
                        (0x810700, bytes(key)),
                        (0x8101E4, bytes([rng.choice([0, 3, 1])])),
                        (0x70003B8D, bytes([rng.choice([0, 0, 4, 2])])),
                        (0x8104E0, w32(rng.choice([1, 0xC, 0xD, 0x29, 0]))),
                        (0x8106C8, w32(rng.choice([0x20081910, 0x20, 0x40, 0x60, 0]))),
                        (0x8106C6, bytes([2 if (ki * 5 + mi) % 3 != 2 else rng.choice([0, 1])])),
                        (c + 0xC, w32(low if rng.random() < 0.9 else low & ~2)),
                        (c + 0x174, w32(rng.getrandbits(32))),
                        (c + 0x1F4, w32(f32(rng.choice([0.0, 0.5, 1.0, 0.25, 0.73])))),
                        (c + 0x2460, w32(rfloat(rng))),
                        (c + 0x2510, w32(rng.choice([0, 1, 0, 7]))),
                        (0x275690, w32(f32(rng.choice([8499.98, 40500.0, 100.0, rng.uniform(1.0, 5e4)])))),
                        (0x275694, w32(f32(rng.choice([2.38095, 50.0, 0.0, rng.uniform(-100.0, 2000.0)])))),
                        (c + 0x1C, w32(FREE + 0x4000 + 16 * rng.randrange(64))),
                    ]
                    if rng.random() < 0.5:
                        pokes.append((c + 0x24F0, bytes(struct.pack('<8f', *[rng.uniform(0, 5e5) for _ in range(8)]))))
                    for p in (c + 0x2470, c + 0x2490):
                        pokes += [(p, bytes([rng.choice([0, 1, 2, 3])])), (p + 8, w32(rng.choice([2, 9, 1, 0x21]))),
                                  (p + 0x1C, w32(rng.choice([0, 8, 96])))]
                    add('%06X key %02X%02X mode %X|#%d' % (entry, key[0], key[1], mode, rep), entry, pokes=pokes)
    # 001D5370: every stage-key arm over a synthetic grid of real bank ids
    keys = [(k >> 8, k & 0xFF, 0) for k in (0x1500, 0x1100, 0x1001, 0x1000, 0x0F00, 0x0803, 0x0806, 0x0801,
                                             0x0805, 0x0800, 0x0703, 0x0700, 0x0601, 0x0600, 0x0401, 0x0400,
                                             0x0301, 0x0101, 0x0100, 0x0B00, 0x0802)]
    keys += [(0x13, 0, room) for room in (1, 4, 5, 7, 8, 9)] + [(0x0D, 0, room) for room in (0, 3, 4, 5)]
    for rep in range(scale):
        for hi, lo, room in keys:
            cells = {}
            for _ in range(12):
                cells[(rng.randrange(32), rng.randrange(32), rng.randrange(4))] = rng.choice(
                    [rng.randrange(1, 700), rng.randrange(1, 700), -1, 0])
            pokes = [(0x810700, bytes([hi, lo, room])), (c + 0x140, w32(GRID)), (c + 0x148, w32(32))]
            for (ix, iz, slot), v in cells.items():
                pokes.append((GRID + 4 * ((ix * 32 + iz) * 4 + slot), w32(v)))
            add('001D5370 key %02X%02X room %d|#%d' % (hi, lo, room, rep), 0x1D5370, pokes=pokes)
    return out


def world_cases(heavy_beats):
    """Every capture: the per-frame chain 001DDA00 and 001E0D70, the
    teardown 001E0DF0 and the grid header 001D52E0 as captured; 001D5370
    over the captured grid for the chosen captures."""
    out = []
    for label, _, _ in CAPTURES:
        for entry in (0x1DDA00, 0x1E0D70, 0x1E0DF0, 0x1D52E0):
            out.append(('%s %06X' % (label, entry), label, entry, [], [], []))
        if label in heavy_beats:
            out.append(('%s 001D5370' % label, label, 0x1D5370, [], [], []))
    return out


# ======================================================================
# Capture evidence (no native call needed: data the lane's startup-only
# routines leave behind)
# ======================================================================

def capture_evidence():
    """001E1010's table and 001DD7B0's GS block words are unchanged in
    every capture from the bytes one executed original call leaves."""
    st = state('opening')
    ee = st['ee']
    ee.dirty, ee.log = [], []
    ee.invoke(0x1E1010)
    table = bytes(ee.mem[0x81E0F0:0x81E0F0 + 0x1800])
    ee.invoke(0x1DD7B0)
    block = bytes(ee.mem[0x81C050:0x81C07C])
    restore(st, ee.dirty)
    count = 0
    for label, path, _ in CAPTURES:
        data = path.read_bytes()
        for row in range(16):
            for j in range(16):
                at = 0x81E0F0 + row * 0x180 + j * 0x18
                assert data[at:at + 16] == table[at - 0x81E0F0:at - 0x81E0F0 + 16], ('001E1010 table', label)
        assert data[0x81C050:0x81C07C] == block, ('001DD7B0 block', label)
        count += 1
    return count


def failstop_cases():
    """Each worker left unbound faults (NULL_WORKER) before the routine that
    reaches it writes a byte: 32 MB and scratchpad stay untouched."""
    st = state('opening')
    checks = 0
    for entry, slots in ((0x1DDA00, SLOT_ORDER), (0x1D5370, [0x1D4DA0, 0x1D2D20, 0x1C6120, 0x1D5BD0]),
                         (0x1DD7B0, [0x1006D8]), (0x1E0D70, [0x1B0070, 0x1CB760]),
                         (0x1D52E0, [0x1C6120])):
        for address in slots:
            if entry == 0x1DDA00 and address in (0x1D1F80, 0x1006D8, 0x1D2D20, 0x1026D0, 0x1C6120,
                                                 0x1D4DA0, 0x1D4FB0, 0x1D4B20, 0x1D5BD0):
                continue
            a, f = (C.c_uint32 * 16)(), (C.c_uint32 * 5)()
            used, mismatch, fault, result = C.c_uint32(), C.c_uint32(), (C.c_uint32 * 2)(), C.c_uint32()
            recs = (C.c_uint32 * 1)()
            before = bytes(st['mem']), bytes(st['spad'])
            rc = NATIVE.rc_run(st['pmem'], st['pspad'], entry, a, f, recs, 0, recs, b'\0',
                               C.byref(used), C.byref(mismatch), fault, C.byref(result),
                               SLOT_ORDER.index(address))
            assert rc == -1 and fault[1] == 1 and fault[0] == address, (hex(entry), hex(address), rc,
                                                                         hex(fault[0]), fault[1])
            assert used.value == 0 and (bytes(st['mem']), bytes(st['spad'])) == before, (
                'wrote before the fail-stop', hex(entry), hex(address))
            checks += 1
    return checks


def clipw_selftest(rng):
    """The oracle's and the native module's clip rule agree bit for bit."""
    n = 0
    for _ in range(4000):
        v = [rng.choice([rng.getrandbits(32), f32(rng.uniform(-10, 10)), 0, 0x80000000,
                         rng.getrandbits(23), 0x7F7FFFFF, 0x7F800000]) for _ in range(4)]
        want = clipw_flags(v)
        flags = C.c_uint32()
        rc = NATIVE.rc_clipw((C.c_uint32 * 4)(*v), C.byref(flags))
        assert (rc < 0) == (want is None) and (want is None or flags.value == want), ([hex(x) for x in v],)
        n += 1
    return n


# ======================================================================

def main():
    global ELF, NATIVE
    started = time.time()
    ELF = read_elf()
    callees = check_callee_set(ELF)
    stub_tree = check_stub_trees(ELF)
    NATIVE = build_native()
    rng = random.Random(0x1DDE10)
    clip_checks = clipw_selftest(rng)
    evidence = capture_evidence()
    failstops = failstop_cases()

    units = unit_cases(rng, 4)
    beats = [c[0] for c in CAPTURES]
    heavy = beats if reference_mode.FULL else ['opening', '05_boxes', '14_roger_encounter']
    world = world_cases(set(heavy))
    pick_units = reference_mode.select(units, 420, 0x1D5, axes=(lambda c: c[2], lambda c: c[0].split('|')[0] if '|' in c[0] else ''))
    groups = {}
    for case in pick_units + world:
        groups.setdefault(case[1], []).append(case)
    # split the opening capture's unit cases so they spread over workers
    items = []
    for capture, cases in groups.items():
        heavy_cases = [c for c in cases if c[2] == 0x1D5370 and not c[5]]
        light = [c for c in cases if c not in heavy_cases]
        for c in heavy_cases:
            items.append((capture, [c]))
        step = 24
        for i in range(0, len(light), step):
            items.append((capture, light[i:i + step]))
    results = reference_mode.parallel_map(run_group, items,
                                          cost=lambda g: sum(40 if c[2] == 0x1D5370 else 1 for c in g[1]))

    outcomes, entries, calls, per = set(), {}, {}, []
    for cover, ent, cnt, rows in results:
        outcomes |= cover
        for k, n in ent.items():
            entries[k] = entries.get(k, 0) + n
        for k, n in cnt.items():
            calls[k] = calls.get(k, 0) + n
        per += rows
    sites = branch_sites(ELF)
    missing = sorted('%06X %s' % (pc, 'taken' if taken else 'not taken')
                     for pc in sites for taken in (True, False)
                     if (pc, taken) not in outcomes and (pc, taken) not in UNREACHABLE)
    assert not missing, ('branch outcomes never exercised', missing)
    reached = sorted(k for k in UNREACHABLE if k in outcomes)
    assert not reached, ('a branch listed unreachable was taken', reached)
    absent = [hex(e) for e in SIZES if e not in entries]
    assert not absent, ('entry points never run directly', absent)
    # Route evidence: on every beat the chain reaches 001DDE10 and none of
    # the four unreached effect callees (census: not executed on the route).
    for label, counted in per:
        if label.endswith(' 001DDA00'):
            assert counted.get(0x1CB760) == 1, (label, 'the chain did not reach 001DDE10')
            for a in (0x1DE920, 0x1DDB70, 0x1DFF70, 0x1DF110):
                assert a not in counted, (label, 'unexpected effect callee', hex(a))

    reference_mode.banner(reference_mode.part(len(pick_units), len(units), 'unit cases'),
                          '%d capture cases (001D5370 on %d of %d captures)' % (
                              len(world), len(heavy), len(CAPTURES)),
                          '%d jal targets (all translated or worker)' % callees)
    print('capture: PASS (001E1010 table and 001DD7B0 GS block identical to one executed original '
          'call in %d captures); fail-stop: PASS %d unbound-worker probes; vclipw rule: PASS %d vectors; '
          'stub trees: %d functions, no VU register use, scratchpad offsets used %s (none in the clip matrices)' % (
              evidence, failstops, clip_checks, stub_tree[0], ['%04X' % x for x in stub_tree[1]]))
    print('render context vs original instructions: PASS %d cases (%s), all 32 MB and the scratchpad '
          'identical after each, %d worker calls identical, every one of %d conditional branches both '
          'ways (%d outcome(s) unreachable, listed) (%.1fs)' % (
              len(pick_units) + len(world), ', '.join('%06X %d' % kv for kv in sorted(entries.items())),
              sum(calls.values()), len(sites), len(UNREACHABLE), time.time() - started))
    return 0


if __name__ == '__main__':
    sys.exit(main())
