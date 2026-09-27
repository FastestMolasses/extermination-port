#!/usr/bin/env python3
"""Execute the original static-world and background-channel routines and
compare em_static_world.c / em_static_world_compose.c (docs/STATIC_WORLD.md).

The user's pinned ELF supplies every instruction; captured original RAM and
scratchpad (build/startup-reference/opening_*, the AREA11 route beats
build/s87/route/00..14 and build/s87/c7cap/**) supply every byte, including
the static-object bank. None are embedded here.

The oracle (SwEE) is the render-context lane's RenderEE (FallEE + VCLIPW and
the CLIP register) with the MMI word interleaves PEXTLW / PEXTUW added (the
transpose 00102798). It executes the ORIGINAL routines, all their callees
included, except the callees no translation covers (STUBBED below: the
flag-0x23 sound branch, 001E1760, 001E17E0, 001D5BD0), which neither side
runs; their results are scripted identically for both.

Checkpoints: at the entry of every routine in TRACED (at any call depth) the
oracle records the address, the argument registers and every RAM /
scratchpad byte written since the previous checkpoint. The native side
reports the same entries through the em_static_world trace hook; at each one
the shim compares the address, the arguments (stack addresses excepted) and
those bytes in its own image, so memory is checked at every callee entry.
After the entry returns, the whole 32 MB RAM, the 16 KB scratchpad, the
checkpoint count and the return value must be equal.

A. 001C1D00 (the whole tree: 001E0CF0 -> 001E1E60, 001D5370 -> 001D4DA0 /
   001D4FB0 / 001D4B20 over the exported-equal bank) over the captures as
   captured (quick: the opening; EM_TEST_FULL=1: all 17 captures with a
   scratchpad image: the opening, route beats 00..14 and the c7cap one).
B. Capture evidence (quick: the opening and 14_roger_encounter;
   EM_TEST_FULL=1: all 17): the frame's own lists. With the channel-3 cursor set to
   the captured ctx+0x1D8, 001E0CF0 rebuilds exactly the captured channel-3
   list, D_00253570..AF and D_002535B8; with the channel-0 cursor at the
   captured static-world start and D_00810610 = the view this frame used
   (ctx+0x2380), 001D5370 rebuilds exactly the captured channel-0 run.
C. Units: 001E1E60 (flags 0x23 / 0x24, the handle, random colours, views,
   zooms, channels), 001E1AD0 (flag 0x22 path, random phase and rand words),
   001E0E80, 001D4F30 / 001D4A90 (run boundaries), 001D4750, 001D2090,
   001D4960, 001D4DA0, 001D4B20, 001D6F60, 001D7000, 001D7100 (sizes,
   alignment), 001D71A0, 001C6120, 00102798, 00102958, 00121870, 001026D0
   (aliasing), 001D52E0, 001D5370 with the other loop-bound keys.
D. Fail-stop: a reached NULL host worker, a missing view, a misaligned
   cursor, a latched fault.
E. The binding rehearsal: 001C1D00 through em_swc over separate views:
   exactly the 20 views em_render_context_live builds (its storage to
   ARENA_END 0x0076B5C0 and every external view), with its read-only
   marking, plus the six the doc says to add (the bank and D_0028A5A0
   read-only); no fault, every view equal to the original's bytes, no byte
   the original writes outside them or inside a read-only one; and the
   marking is enforced (D_00253560 marked read-only -> READ_ONLY fault).
   Host workers check the arguments they are actually called with against
   the original's checkpoint (not the trace).
F. EM_TEST_FULL=1 only: the shim and the modules built with ASan/UBSan run
   001C1D00 over the opening capture under all eight states of flags
   0x22..0x24 (host workers bound).
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
os.environ.setdefault('EM_TEST_JOBS', '4')        # at most 4 worker processes
import reference_mode as RM  # noqa: E402
import test_render_context_reference as rcref  # noqa: E402
import test_player_slide_reference as shared  # noqa: E402
from test_player_slide_reference import read_elf, sx32, REFERENCE, DECOMP  # noqa: E402

MASK = 0xFFFFFFFF
MASK64 = 0xFFFFFFFFFFFFFFFF
LANE = os.environ.get('EM_LANE', 'b15/static_world')
OUT = ROOT / 'build' / LANE
RAM_SIZE = 0x2000000
SPAD, SPAD_SIZE = 0x70000000, 0x4000
STACK_LO, STACK_HI = 0x7F000000, 0x7F100000
CTX_PTR = 0x275670
FREE = 0x1E00000

# address: (integer args, float args, wide-integer mask)
SPEC = {
    0x1C1D00: (1, 0, 0), 0x1E0CF0: (0, 0, 0), 0x1E0CC0: (0, 0, 0), 0x1D2910: (1, 0, 0),
    0x1E2260: (1, 0, 1), 0x1E1E60: (2, 0, 0), 0x1E1AD0: (2, 0, 0), 0x1E0E80: (3, 0, 0),
    0x1D1F80: (3, 0, 0), 0x1D1FF0: (2, 0, 0), 0x1D2040: (2, 0, 0), 0x1D7080: (2, 1, 0),
    0x128250: (0, 1, 0), 0x122BB8: (0, 0, 0), 0x1D73A0: (1, 0, 0), 0x1D72D0: (1, 0, 0),
    0x1D75E0: (4, 2, 4), 0x1CEFD0: (2, 0, 0), 0x1D2E00: (1, 0, 0), 0x1E1760: (1, 0, 0),
    0x1D6BA0: (6, 0, 0), 0x1281C0: (0, 1, 0), 0x1E17E0: (1, 0, 0), 0x1026A0: (3, 0, 0),
    0x11DF78: (0, 1, 0), 0x102798: (2, 0, 0), 0x1026D0: (3, 0, 0), 0x102958: (2, 0, 0),
    0x121870: (3, 0, 0), 0x1D4750: (1, 0, 0), 0x1D2090: (2, 0, 0), 0x1D4960: (1, 0, 0),
    0x1D4DA0: (0, 0, 0), 0x1D4F30: (2, 0, 0), 0x1D4A90: (2, 0, 0), 0x1D4FB0: (1, 0, 0),
    0x1D4B10: (1, 0, 0), 0x1D4B20: (1, 0, 0), 0x1D6F60: (3, 0, 2), 0x1D7000: (2, 0, 0),
    0x1D7100: (4, 0, 0), 0x1D71A0: (2, 0, 0), 0x1C6120: (2, 0, 0), 0x1D5370: (0, 0, 0),
    0x1D2D20: (1, 5, 0), 0x1D5BD0: (0, 0, 0), 0x1D52E0: (0, 0, 0),
}
STUBBED = {0x122BB8, 0x1D73A0, 0x1D72D0, 0x1D75E0, 0x1CEFD0, 0x1E1760, 0x1E17E0, 0x1D5BD0}
TRACED = set(SPEC) - STUBBED
# The routines em_static_world translates (branch outcomes are recorded in these).
SIZES = {0x1D4FB0: 0xC, 0x1D4F30: 0x80, 0x1D4B20: 0x2C, 0x1D4960: 0x70, 0x1D4B10: 0xC,
         0x1D4A90: 0x80, 0x1D4DA0: 0x80, 0x1D4750: 0x210, 0x1D2090: 0x7C, 0x102958: 0x24,
         0x1E1E60: 0x400, 0x1E1AD0: 0x38C, 0x1E0E80: 0x190, 0x1D6F60: 0x94, 0x1D7000: 0x7C,
         0x1D7100: 0xA0, 0x1D71A0: 0x4C, 0x121870: 0xB0, 0x102798: 0x44, 0x1C6120: 0x30}
# Conditional branches no input reaches, with the reason.
UNREACHABLE = {
    (0x1E0FD4, False): '001E0E80: float_to_int(256 * fabsf(y) / d) with d >= 1.0 is never '
                       'negative (fabsf clears the sign; a value past 2^31 gives INT32_MAX)',
}
RW = 28


def in_mine(pc):
    return any(a <= pc < a + n for a, n in SIZES.items())


# ======================================================================
# The oracle
# ======================================================================

class SwEE(rcref.RenderEE):
    def __init__(self, elf, ram, spad):
        super().__init__(elf, ram, spad)
        self.hooks = {}
        self.cp = []
        self.mark = 0
        self.script = {}
        self.outcomes = set()
        for a in TRACED:
            self.hooks[a] = self._traced(a)
        for a in STUBBED:
            self.hooks[a] = self._stub(a)

    def mmi(self, word, pc):
        fn, sub = word & 63, word >> 6 & 31
        rs, rt, rd = word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        if (fn, sub) in ((0x08, 0x12), (0x28, 0x12)):            # pextlw / pextuw
            if rd:
                a = (self.r[rs] & MASK64) | (self.rh[rs] << 64)
                b = (self.r[rt] & MASK64) | (self.rh[rt] << 64)
                w = lambda v, i: (v >> (32 * i)) & MASK
                k = 0 if fn == 0x08 else 2
                value = w(b, k) | w(a, k) << 32 | w(b, k + 1) << 64 | w(a, k + 1) << 96
                self.r[rd] = value & MASK64
                self.rh[rd] = value >> 64
            return
        super().mmi(word, pc)

    def branch(self, word, pc):
        b = rcref.FallEE.branch(self, word, pc)
        if b is not None and in_mine(pc):
            self.outcomes.add((pc, b[0]))
        return b

    def _checkpoint(self, address):
        spans = {}
        for where, at, size in self.dirty[self.mark:]:
            for b in range(at, at + size):
                spans[(where, b)] = True
        self.mark = len(self.dirty)
        delta = []
        for where in ('ram', 'spad'):
            offs = sorted(b for (w, b) in spans if w == where)
            i = 0
            while i < len(offs):
                j = i
                while j + 1 < len(offs) and offs[j + 1] == offs[j] + 1:
                    j += 1
                base = offs[i] + (0 if where == 'ram' else SPAD)
                buf = self.mem if where == 'ram' else self.spad
                delta.append((base, bytes(buf[offs[i]:offs[j] + 1])))
                i = j + 1
        rec = {'addr': address, 'a': [self.r[4 + i] & MASK64 for i in range(8)],
               'f': [self.f[12 + i] & MASK for i in range(5)], 'delta': delta, 'v0': 0,
               'mem': [0] * 8}
        self.cp.append(rec)
        return rec

    def _traced(self, address):
        def hook(ee):
            ee._checkpoint(address)
            ret = ee.r[31]
            ee.hooks.pop(address)
            try:
                ee.r[31] = shared.RETURN
                ee.run(address)
            finally:
                ee.hooks[address] = hook
            ee.r[31] = ret
        return hook

    def _stub(self, address):
        def hook(ee):
            rec = ee._checkpoint(address)
            if address in (0x1D72D0, 0x1D75E0, 0x1CEFD0):
                p = ee.r[4 if address != 0x1D75E0 else 5] & MASK
                rec['mem'][0:4] = [ee.load(p + 4 * i) for i in range(4)]
            if address == 0x1CEFD0:
                p = ee.r[5] & MASK
                rec['mem'][4:8] = [ee.load(p + 4 * i) for i in range(4)]
            queue = ee.script.get(address, [])
            v = queue.pop(0) if queue else 0
            rec['v0'] = v & MASK
            ee.r[2] = sx32(v)
        return hook

    def call_entry(self, entry, args):
        self.r = [0] * 32
        self.rh = [0] * 32
        self.r[28] = 0x27D370
        self.r[29] = shared.STACK_TOP
        self.clip = 0
        for i, value in enumerate(args):
            wide = isinstance(value, tuple)
            v = value[1] if wide else value
            reg = 4 + i
            self.r[reg] = (v & MASK64) if wide else (sx32(v) & MASK64)
        self.r[31] = shared.RETURN
        self.run(entry)
        return self.r[2] & MASK


# ======================================================================
# The native side
# ======================================================================

SHIM = r'''
#include <string.h>
#include "game/em_static_world.h"
#include "game/em_static_world_compose.h"

#define RW 28
typedef struct {
    const uint64_t *rec;
    uint32_t nrec, next;
    const uint32_t *dtab;
    const uint8_t *blob;
    uint8_t *ram, *spad;
    uint32_t mismatch, why;
} T;

static void miss(T *t, uint32_t why) { if (!t->mismatch) { t->mismatch = 1 + t->next; t->why = why; } }

static int stack(uint64_t v) { uint32_t w = (uint32_t)v; return w >= 0x7F000000u && w < 0x7F100000u; }

static void on_trace(void *ctx, uint32_t address, const uint64_t args[6])
{
    T *t = ctx;
    if (t->mismatch) return;
    if (t->next >= t->nrec) { miss(t, 1); return; }
    const uint64_t *r = &t->rec[RW * t->next];
    if ((uint32_t)r[0] != address) { miss(t, 2); return; }
    const uint32_t ni = (uint32_t)r[25], nf = (uint32_t)r[26], wide = (uint32_t)r[27];
    for (uint32_t i = 0; i < ni; ++i) {
        if (stack(r[1 + i]) || args[i] == EM_SW_TRACE_STACK) continue;
        const uint64_t m = (wide >> i & 1u) ? ~UINT64_C(0) : UINT64_C(0xFFFFFFFF);
        if ((r[1 + i] & m) != (args[i] & m)) { miss(t, 10 + i); return; }
    }
    for (uint32_t j = 0; j < nf; ++j)
        if ((uint32_t)r[9 + j] != (uint32_t)args[ni + j]) { miss(t, 20 + j); return; }
    for (uint32_t k = 0; k < (uint32_t)r[16]; ++k) {
        const uint32_t *d = &t->dtab[3 * ((uint32_t)r[15] + k)];
        const uint8_t *mine = d[0] >= 0x70000000u ? t->spad + (d[0] - 0x70000000u) : t->ram + d[0];
        if (memcmp(mine, t->blob + d[2], d[1]) != 0) { miss(t, 30); return; }
    }
    t->next++;
}

/* The record of the checkpoint just passed; with no records at all (the
 * rehearsal and the sanitizer run) a zero record. */
static const uint64_t ZERO_REC[RW];
static const uint64_t *cur(T *t)
{
    if (!t->nrec) return ZERO_REC;
    return t->next ? &t->rec[RW * (t->next - 1)] : NULL;
}
static int words_ok(T *t, const uint32_t *v, int off)
{
    const uint64_t *r = cur(t);
    if (!r) return 0;
    if (!t->nrec) return 1;
    for (int i = 0; i < 4; ++i) if ((uint32_t)r[17 + off + i] != v[i]) { miss(t, 40); return 0; }
    return 1;
}
/* A host worker receives exactly what the original passed at the checkpoint
 * just recorded for it: its own address, the integer argument registers
 * (stack addresses excepted) and the float ones, compared from the values
 * the worker is actually called with (not from the trace). */
static int own_ok(T *t, uint32_t address)
{
    const uint64_t *r = cur(t);
    if (!r) return 0;
    if (!t->nrec) return 1;
    if ((uint32_t)r[0] != address) { miss(t, 41); return 0; }
    return 1;
}
static int int_ok(T *t, uint32_t i, uint64_t v)
{
    const uint64_t *r = cur(t);
    if (!t->nrec) return 1;
    const uint64_t m = ((uint32_t)r[27] >> i & 1u) ? ~UINT64_C(0) : UINT64_C(0xFFFFFFFF);
    if ((r[1 + i] & m) != (v & m)) { miss(t, 50 + i); return 0; }
    return 1;
}
static int flt_ok(T *t, uint32_t j, uint32_t bits)
{
    const uint64_t *r = cur(t);
    if (!t->nrec) return 1;
    if ((uint32_t)r[9 + j] != bits) { miss(t, 60 + j); return 0; }
    return 1;
}
static uint64_t sxw(int32_t v) { return (uint64_t)(int64_t)v; }
static int h_5BD0(void *c) { return own_ok(c, 0x1D5BD0u) ? 0 : -1; }
static int h_2BB8(void *c, int32_t *out)
{
    if (!own_ok(c, 0x122BB8u)) return -1;
    *out = (int32_t)cur(c)[14];
    return 0;
}
static int h_73A0(void *c, int32_t h, int32_t *out)
{
    if (!own_ok(c, 0x1D73A0u) || !int_ok(c, 0, sxw(h))) return -1;
    *out = (int32_t)cur(c)[14];
    return 0;
}
static int h_72D0(void *c, const uint32_t snd[4], int32_t *out)
{
    if (!own_ok(c, 0x1D72D0u) || !words_ok(c, snd, 0)) return -1;
    *out = (int32_t)cur(c)[14];
    return 0;
}
static int h_75E0(void *c, int32_t chan, const uint32_t snd[4], uint64_t a2, uint32_t a3, uint32_t f12, uint32_t f13)
{
    return own_ok(c, 0x1D75E0u) && int_ok(c, 0, sxw(chan)) && int_ok(c, 2, a2)
           && int_ok(c, 3, sxw((int32_t)a3)) && flt_ok(c, 0, f12) && flt_ok(c, 1, f13)
           && words_ok(c, snd, 0) ? 0 : -1;
}
static int h_EFD0(void *c, const uint32_t snd[4], const uint32_t vol[4])
{
    return own_ok(c, 0x1CEFD0u) && words_ok(c, snd, 0) && words_ok(c, vol, 4) ? 0 : -1;
}
static int h_1760(void *c, int32_t chan) { return own_ok(c, 0x1E1760u) && int_ok(c, 0, sxw(chan)) ? 0 : -1; }
static int h_17E0(void *c, int32_t chan) { return own_ok(c, 0x1E17E0u) && int_ok(c, 0, sxw(chan)) ? 0 : -1; }

typedef struct { uint32_t fn; const uint64_t *a; uint32_t *result; } Call;

static int body(EmStaticWorld *s, void *arg)
{
    Call *k = arg;
    const uint64_t *a = k->a;
    const int32_t i0 = (int32_t)a[0], i1 = (int32_t)a[1];
    const uint32_t u0 = (uint32_t)a[0], u1 = (uint32_t)a[1], u2 = (uint32_t)a[2], u3 = (uint32_t)a[3];
    switch (k->fn) {
    case 0x1D4FB0u: return em_static_world_001D4FB0(s, u0);
    case 0x1D4F30u: return em_static_world_001D4F30(s, i0, u1);
    case 0x1D4B20u: return em_static_world_001D4B20(s, u0);
    case 0x1D4960u: return em_static_world_001D4960(s, u0);
    case 0x1D4B10u: return em_static_world_001D4B10(s, u0);
    case 0x1D4A90u: return em_static_world_001D4A90(s, i0, u1);
    case 0x1D4DA0u: return em_static_world_001D4DA0(s);
    case 0x1D4750u: return em_static_world_001D4750(s, i0);
    case 0x1D2090u: return em_static_world_001D2090(s, i0, u1);
    case 0x1E0E80u: return em_static_world_001E0E80(s, u0, i1, (int32_t)u2, k->result);
    case 0x1D6F60u: return em_static_world_001D6F60(s, i0, a[1], (int32_t)u2);
    case 0x1D7000u: return em_static_world_001D7000(s, i0, i1);
    case 0x1D7100u: return em_static_world_001D7100(s, i0, u1, u2, (int32_t)u3, k->result);
    case 0x1D71A0u: return em_static_world_001D71A0(s, i0, u1);
    case 0x1C6120u: return em_static_world_001C6120(s, u0, i1, k->result);
    case 0x102798u: return em_static_world_00102798(s, u0, u1);
    case 0x102958u: return em_static_world_00102958(s, u0, u1);
    case 0x121870u: return em_static_world_00121870(s, u0, u1, u2);
    case 0x1026D0u: return em_static_world_001026D0(s, u0, u1, u2);
    default: return -1;
    }
}

/* E: 001C1D00 over a list of separate views (the live storage plus the
 * additions the doc names), host workers bound, no trace. */
int sw_run_views(uint32_t n, const uint32_t *addr, const uint32_t *size, uint8_t **bytes,
                 const uint8_t *read_only, uint32_t state_address, int32_t fault[4])
{
    EmStaticWorldView views[32];
    if (n > 32) return -2;
    for (uint32_t i = 0; i < n; ++i) views[i] = (EmStaticWorldView){addr[i], size[i], bytes[i]};
    T t;
    memset(&t, 0, sizeof t);
    EmSwc c;
    memset(&c, 0, sizeof c);
    c.views = views;
    c.view_count = n;
    c.read_only = read_only;
    c.host.ctx = &t;
    c.host.w_001D5BD0 = h_5BD0; c.host.w_00122BB8 = h_2BB8; c.host.w_001D73A0 = h_73A0;
    c.host.w_001D72D0 = h_72D0; c.host.w_001D75E0 = h_75E0; c.host.w_001CEFD0 = h_EFD0;
    c.host.w_001E1760 = h_1760; c.host.w_001E17E0 = h_17E0;
    const int rc = em_swc_001C1D00(&c, state_address);
    fault[0] = c.fault.module; fault[1] = (int32_t)c.fault.address; fault[2] = c.fault.code;
    fault[3] = (int32_t)c.fault.detail;
    return rc;
}

/* flags: 1 = no host workers, 2 = drop the views above `cut`, 4 = no trace */
int sw_run(uint8_t *ram, uint8_t *spad, uint32_t fn, const uint64_t *a, const uint64_t *rec,
           uint32_t nrec, const uint32_t *dtab, const uint8_t *blob, uint32_t flags, uint32_t cut,
           uint32_t *used, uint32_t *mismatch, uint32_t *why, int32_t fault[4], uint32_t *result)
{
    T t;
    memset(&t, 0, sizeof t);
    t.rec = rec; t.nrec = nrec; t.dtab = dtab; t.blob = blob; t.ram = ram; t.spad = spad;
    EmStaticWorldView views[3] = {{0u, 0x2000000u, ram}, {0x70000000u, 0x4000u, spad}, {0, 0, NULL}};
    uint32_t nviews = 2;
    if (flags & 2u) {           /* RAM only up to `cut` (the bank and above missing) */
        views[0].size = cut;
    }
    EmSwc c;
    memset(&c, 0, sizeof c);
    c.views = views;
    c.view_count = nviews;
    if (!(flags & 4u)) { c.trace = on_trace; c.trace_ctx = &t; }
    if (!(flags & 1u)) {
        c.host.ctx = &t;
        c.host.w_001D5BD0 = h_5BD0; c.host.w_00122BB8 = h_2BB8; c.host.w_001D73A0 = h_73A0;
        c.host.w_001D72D0 = h_72D0; c.host.w_001D75E0 = h_75E0; c.host.w_001CEFD0 = h_EFD0;
        c.host.w_001E1760 = h_1760; c.host.w_001E17E0 = h_17E0;
    }
    uint32_t res = 0;
    int rc;
    switch (fn) {
    case 0x1C1D00u: rc = em_swc_001C1D00(&c, (uint32_t)a[0]); break;
    case 0x1E0CF0u: rc = em_swc_001E0CF0(&c); break;
    case 0x1D5370u: rc = em_swc_001D5370(&c); break;
    case 0x1D52E0u: rc = em_swc_001D52E0(&c); break;
    case 0x1E1E60u: rc = em_swc_001E1E60(&c, (uint32_t)a[0], (int32_t)a[1], &res); break;
    case 0x1E1AD0u: rc = em_swc_001E1AD0(&c, (uint32_t)a[0], (int32_t)a[1], &res); break;
    default: {
        Call k = {fn, a, &res};
        rc = em_swc_with_static_world(&c, body, &k);
        break;
    }
    }
    if ((flags & 8u) && rc == 0) {   /* a second call after a latched fault must not run */
        rc = em_swc_001E0CF0(&c);
    }
    *used = t.next;
    *mismatch = t.mismatch;
    *why = t.why;
    fault[0] = c.fault.module; fault[1] = (int32_t)c.fault.address; fault[2] = c.fault.code;
    fault[3] = (int32_t)c.fault.detail;
    *result = res;
    return rc;
}
'''

SOURCES = ['src/game/em_static_world.c', 'src/game/em_static_world_compose.c',
           'src/game/em_render_context.c', 'src/game/em_render_verify_rest.c',
           'src/game/em_frame_render_heads.c', 'src/game/em_load_veil_particles.c',
           'src/game/em_stream_lanes_original.c', 'src/game/em_player_stage_workers.c',
           'src/game/em_sdk_math_original.c', 'src/game/em_effect_original.c',
           'src/game/em_sdk_soft_float.c', 'src/game/em_camera_commit_original.c']


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    source = OUT / 'sw_shim.c'
    if not source.exists() or source.read_text() != SHIM:   # keep its mtime: rebuild only on change
        source.write_text(SHIM)
    lib = OUT / ('sw.dylib' if sys.platform == 'darwin' else 'sw.so')
    deps = [ROOT / s for s in SOURCES] + [ROOT / 'src/game/em_static_world.h',
                                          ROOT / 'src/game/em_static_world_compose.h']
    if not lib.exists() or lib.stat().st_mtime < max(p.stat().st_mtime for p in deps + [source]):
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                        '-Wno-cast-function-type', '-ffp-contract=off', '-shared', '-fPIC', '-Isrc',
                        str(source), *SOURCES, '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    P = C.POINTER
    native.sw_run.argtypes = [C.c_void_p, C.c_void_p, C.c_uint32, P(C.c_uint64), P(C.c_uint64),
                              C.c_uint32, P(C.c_uint32), C.c_char_p, C.c_uint32, C.c_uint32,
                              P(C.c_uint32), P(C.c_uint32), P(C.c_uint32), P(C.c_int32), P(C.c_uint32)]
    native.sw_run.restype = C.c_int
    native.sw_run_views.argtypes = [C.c_uint32, P(C.c_uint32), P(C.c_uint32), P(C.c_void_p), P(C.c_uint8), C.c_uint32,
                                    P(C.c_int32)]
    native.sw_run_views.restype = C.c_int
    return native


def pack(cps):
    """Rows of RW words: address, a0..a3 t0..t3, f12..f16, v0, delta start,
    delta count, snd[4], vol[4], integer / float argument counts, wide mask."""
    flat, dtab, blob = [], [], bytearray()
    for rec in cps:
        ni, nf, wide = SPEC[rec['addr']]
        row = [rec['addr']] + rec['a'] + rec['f'] + [rec['v0'], len(dtab) // 3]
        for address, data in rec['delta']:
            dtab += [address, len(data), len(blob)]
            blob += data
        row += [len(dtab) // 3 - row[15]] + rec['mem'] + [ni, nf, wide]
        assert len(row) == RW
        flat += row
    return flat, dtab, bytes(blob) or b'\0'


# ======================================================================
# One case
# ======================================================================

ELF = NATIVE = None
_STATE = {}
CAPTURES = [('opening', REFERENCE / 'opening_ee.bin', REFERENCE / 'opening_scratchpad.bin')]
ROUTE = DECOMP / 'build/s87/route'
for _beat in sorted(p.name for p in ROUTE.iterdir() if (p / 'eeMemory.bin').exists()) if ROUTE.exists() else []:
    if RM.in_scope_beat(_beat):
        CAPTURES.append((_beat, ROUTE / _beat / 'eeMemory.bin', ROUTE / _beat / 'scratchpad.bin'))
for _p in sorted((DECOMP / 'build/s87/c7cap').glob('**/eeMemory.bin')):
    if (_p.parent / 'scratchpad.bin').exists():
        CAPTURES.append(('c7cap/' + _p.parent.name, _p, _p.parent / 'scratchpad.bin'))


def state(capture):
    if _STATE.get('name') != capture:
        _STATE.clear()
        label, ee_path, spad_path = next(c for c in CAPTURES if c[0] == capture)
        base, sbase = ee_path.read_bytes(), spad_path.read_bytes()
        assert len(base) == RAM_SIZE and len(sbase) == SPAD_SIZE, ('capture size', label)
        ee = SwEE(ELF, base, sbase)
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


RESULT = {0x1E1E60, 0x1E1AD0, 0x1E0E80, 0x1D7100, 0x1C6120}


def run_one(st, case):
    """case = dict(label, capture, entry, args, pokes, script, check). Returns
    (branch outcomes, checkpoint count, the check's result)."""
    ee, mem, spad = st['ee'], st['mem'], st['spad']
    ee.dirty, ee.cp, ee.mark, ee.outcomes = [], [], 0, set()
    ee.script = {k: list(v) for k, v in case.get('script', {}).items()}
    label, entry, args = case['label'], case['entry'], case.get('args', [])
    for address, data in case.get('pokes', []):
        ee.write(address, data)
        if address >= SPAD:
            spad[address - SPAD:address - SPAD + len(data)] = data
        else:
            mem[address:address + len(data)] = data
    touched = list(ee.dirty)
    ee.dirty, ee.mark = [], 0
    try:
        v0 = ee.call_entry(entry, args)
        flat, dtab, blob = pack(ee.cp)
        recs = (C.c_uint64 * max(1, len(flat)))(*[x & MASK64 for x in flat])
        a = (C.c_uint64 * 8)(*[(x[1] if isinstance(x, tuple) else sx32(x)) & MASK64
                               for x in list(args) + [0] * (8 - len(args))])
        dt = (C.c_uint32 * max(1, len(dtab)))(*dtab)
        used, mismatch, why, result = C.c_uint32(), C.c_uint32(), C.c_uint32(), C.c_uint32()
        fault = (C.c_int32 * 4)()
        rc = NATIVE.sw_run(st['pmem'], st['pspad'], entry, a, recs, len(ee.cp), dt, blob,
                           case.get('flags', 0), case.get('cut', 0), C.byref(used), C.byref(mismatch),
                           C.byref(why), fault, C.byref(result))
        if mismatch.value:
            k = mismatch.value - 1
            want = ee.cp[k] if k < len(ee.cp) else None
            raise AssertionError((label, 'checkpoint %d differs (reason %d)' % (k, why.value),
                                  'original', want and (hex(want['addr']), [hex(x) for x in want['a'][:6]],
                                                        [hex(x) for x in want['f'][:2]])))
        assert rc == 0, (label, 'native faulted', list(fault))
        assert used.value == len(ee.cp), (label, 'checkpoints', used.value, len(ee.cp))
        if mem != ee.mem:
            diff = next(i for i in range(RAM_SIZE) if mem[i] != ee.mem[i])
            raise AssertionError((label, 'RAM differs first at', hex(diff),
                                  'native', mem[diff & ~15:(diff & ~15) + 16].hex(),
                                  'original', bytes(ee.mem[diff & ~15:(diff & ~15) + 16]).hex()))
        if spad != ee.spad:
            diff = next(i for i in range(SPAD_SIZE) if spad[i] != ee.spad[i])
            raise AssertionError((label, 'scratchpad differs first at', hex(SPAD + diff)))
        if entry in RESULT:
            assert result.value == v0, (label, 'return value', hex(result.value), hex(v0))
        checked = case['check'](st, ee, case) if case.get('check') else None
        return ee.outcomes, len(ee.cp), checked
    finally:
        restore(st, touched + ee.dirty)


def run_group(group):
    capture, cases = group
    st = state(capture)
    out = []
    for case in cases:
        out.append((case['label'],) + run_one(st, case))
    return out


# ======================================================================
# Cases
# ======================================================================

def w32(v):
    return struct.pack('<I', v & MASK)


def f32(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def rd(buf, a, n=4):
    return int.from_bytes(buf[a & 0x1FFFFFF:(a & 0x1FFFFFF) + n], 'little')


def ctx_of(path):
    with open(path, 'rb') as fh:
        fh.seek(CTX_PTR)
        return struct.unpack('<I', fh.read(4))[0] & (RAM_SIZE - 1)


FLOATS = [0.0, -0.0, 1.0, -1.0, 0.5, 2.0, 0.25, 1e-39, 3.0e38, -3.0e38, 123.5, -77.25, 480.0,
          0.999, 1.0001, 16777215.0]


def rfloat(rng, lo=-2e3, hi=2e3):
    if rng.random() < 0.3:
        return f32(rng.choice(FLOATS))
    return f32(rng.uniform(lo, hi))


def flag_pokes(ctx, base, on):
    """Set or clear render flags 0x20..0x3F in context +0x174."""
    word = rd(base, ctx + 0x174)
    for flag, value in on.items():
        bit = 1 << (flag - 0x20)
        word = (word | bit) if value else (word & ~bit)
    return [(ctx + 0x174, w32(word))]


def check_ch3(st, ee, case):
    """B: the channel-3 list rebuilt from the captured start equals the
    captured list, and the matrix / zoom copies equal the captured words."""
    base, ctx = st['base'], ctx_of_state(st)
    start = rd(base, ctx + 0x1D8)
    end = rd(ee.mem, ctx + 0x1C)
    assert rd(ee.mem, ctx + 0x1D8) == start
    assert bytes(ee.mem[start:end]) == base[start:end], ('channel-3 list differs from the capture',
                                                          st['name'])
    assert bytes(ee.mem[0x253570:0x2535B0]) == base[0x253570:0x2535B0], ('D_00253570', st['name'])
    assert bytes(ee.mem[0x2535B8:0x2535BC]) == base[0x2535B8:0x2535BC], ('D_002535B8', st['name'])
    return end - start


def check_ch0(st, ee, case):
    """B: the static-world run rebuilt from the captured start equals the
    captured bytes. (The two clip matrices at 0x70003400 are not compared:
    later routines of the same frame reuse that scratchpad, so the captured
    words there are not 001D5370's.)"""
    base, ctx = st['base'], ctx_of_state(st)
    start = case['s0']
    end = rd(ee.mem, ctx + 0x10)
    assert bytes(ee.mem[start:end]) == base[start:end], ('channel-0 run differs from the capture',
                                                          st['name'])
    return end - start


def ctx_of_state(st):
    return rd(st['base'], CTX_PTR) & (RAM_SIZE - 1)


def static_start(base, ctx):
    """The captured frame's static-world start: the first 001D4750 packet
    (CNT qwc 9, FLUSH, UNPACK 8 at VU 0) in the current buffer's channel 0."""
    cur0 = rd(base, ctx + 0x10)
    b = 0 if cur0 < 0x28F700 + 0x60800 + 0x8000 else 1
    a = 0x28F700 + b * 0x60800 + 0x8000
    while a < cur0:
        if base[a + 3] == 0x10 and rd(base, a, 2) == 9 and rd(base, a + 0x18) == 0x11000000 \
                and rd(base, a + 0x1C) == 0x6C080000:
            return a
        a += 16
    raise AssertionError('no static-world start in the captured channel 0')


def capture_cases(names, whole):
    groups = []
    for name in names:
        path = next(c for c in CAPTURES if c[0] == name)[1]
        base = path.read_bytes()
        ctx = rd(base, CTX_PTR) & (RAM_SIZE - 1)
        s0 = static_start(base, ctx)
        view = base[ctx + 0x2380:ctx + 0x23C0]
        state_byte = 0x8101D0
        cases = [dict(label=f'A 001C1D00 {name}', entry=0x1C1D00, args=[state_byte])] if name in whole else []
        cases += [
            dict(label=f'B channel 3 {name}', entry=0x1E0CF0,
                 pokes=[(ctx + 0x1C, base[ctx + 0x1D8:ctx + 0x1DC])], check=check_ch3),
            dict(label=f'B channel 0 {name}', entry=0x1D5370,
                 pokes=[(ctx + 0x10, w32(s0)), (0x810610, view)], check=check_ch0, s0=s0),
        ]
        groups.append((name, cases))
    return groups


def unit_cases(rng, scale):
    """C / D over the opening capture."""
    path = CAPTURES[0][1]
    base = path.read_bytes()
    ctx = rd(base, CTX_PTR) & (RAM_SIZE - 1)
    cases = []
    ch = lambda c: ctx + 0x10 + 4 * c
    # channel cursors moved to a free area so the builders write fresh bytes
    def cursor_pokes(chan, at):
        return [(ch(chan), w32(at))]
    # --- 001E1E60 ---
    for i in range(scale['bg']):
        chan = [3, 0, 1, 2][i % 4]
        on23, on24 = bool(i & 4), bool(i & 8)
        handle = 0xFFFFFFFF if i % 3 == 0 else rng.getrandbits(16)
        pokes = cursor_pokes(chan, FREE + 0x10000 * (i % 3)) + flag_pokes(ctx, base, {0x23: on23, 0x24: on24})
        pokes += [(0x27569C, w32(handle))]
        pokes += [(ctx + 0x1C0 + 4 * k, w32(rfloat(rng, 0.0, 2.0))) for k in range(4)]
        if i % 2:
            pokes += [(ctx + 0x2380 + 4 * k, w32(rfloat(rng, -1.5, 1.5))) for k in range(16)]
            pokes += [(ctx + 0x2468, w32(rfloat(rng, 100.0, 2000.0)))]
        pokes += [(ctx + 0x1D0, struct.pack('<Q', rng.getrandbits(64)))]
        script = {0x122BB8: [rng.getrandbits(31)], 0x1D73A0: [rng.choice([0, 0, 1, -1])],
                  0x1D72D0: [rng.getrandbits(16)]}
        cases.append(dict(label=f'C 001E1E60 #{i}', entry=0x1E1E60, args=[ctx + 0x180, chan],
                          pokes=pokes, script=script))
    # --- 001E1AD0 (and 001E0E80 through it) ---
    for i in range(scale['grid']):
        chan = 3 if i % 2 == 0 else 1
        rnd = [-rng.getrandbits(31) or -1, rng.getrandbits(31), 0x7FFFFFFF, -1][i % 4]
        pokes = cursor_pokes(chan, FREE + 0x40000)
        pokes += [(ctx + 0x2520 + 4, w32(rnd)), (0x275C0C, w32(rfloat(rng, -3.0, 3.0)))]
        pokes += [(0x8105D4, w32(rfloat(rng, -50.0, 400.0)))]
        if i % 2:
            pokes += [(ctx + 0x2380 + 4 * k, w32(rfloat(rng, -1.5, 1.5))) for k in range(16)]
        cases.append(dict(label=f'C 001E1AD0 #{i}', entry=0x1E1AD0, args=[ctx + 0x1E0, chan], pokes=pokes))
    for i in range(scale['point']):
        pokes = [(0x8105D4, w32(rfloat(rng, -50.0, 400.0)))]
        pokes += [(ctx + 0x2380 + 4 * k, w32(rfloat(rng, -1.5, 1.5))) for k in range(16)]
        pokes += [(ctx + 0x2468, w32(rfloat(rng, -500.0, 2000.0)))]
        cases.append(dict(label=f'C 001E0E80 #{i}', entry=0x1E0E80,
                          args=[FREE + 0x80000 + 16 * i, rng.randint(-300, 300), rng.randint(-300, 300)],
                          pokes=pokes))
    # 001E0E80 boundaries: with an identity view the ray is (x, y, zoom), so
    # y < 0 takes the ST path, 256 |y| / max(1, D - 10) hits 128 / 129 / 256
    ident = b''.join(w32(f32(1.0 if r == c else 0.0)) for r in range(4) for c in range(4))
    for y, d in ((-100, 210.0), (-101, 210.0), (-1, 5.0), (0, 210.0), (-128, 11.0), (-3, 10.5)):
        cases.append(dict(label=f'C 001E0E80 y={y} D={d}', entry=0x1E0E80,
                          args=[FREE + 0x80800, rng.randint(-256, 256), y],
                          pokes=[(ctx + 0x2380, ident), (0x8105D4, w32(f32(d))),
                                 (ctx + 0x2468, w32(f32(480.0)))]))
    # --- 001D4F30 / 001D4A90 / 001D4FB0 / 001D4B10 / 001D4B20 run boundaries ---
    counts = [0, -1, 1, 2, 0x1F7, 0x1F8, 0x1F9, 0x3F0, 0x3F1, 0x5E9, -0x1F8]
    for i, n in enumerate(counts):
        obj = FREE + 0x100000 + 0x100 * i
        for entry in (0x1D4F30, 0x1D4A90):
            chan = i % 4
            cases.append(dict(label=f'C {entry:06X} n={n}', entry=entry, args=[chan, obj],
                              pokes=[(obj, w32(n))] + cursor_pokes(chan, FREE + 0x20000)))
    bank = rd(base, 0x28A5A0)
    for i, oid in enumerate([1, 2, 350, 701]):
        o = bank + ((rd(base, bank + 4 + 4 * oid) >> 2) << 2)
        for entry in (0x1D4FB0, 0x1D4B10, 0x1D4B20, 0x1D4960):
            cases.append(dict(label=f'C {entry:06X} obj {oid}', entry=entry, args=[o],
                              pokes=cursor_pokes(0, FREE + 0x30000)))
    # --- the leaf builders ---
    for chan in range(4):
        cases.append(dict(label=f'C 001D4750 ch{chan}', entry=0x1D4750, args=[chan],
                          pokes=cursor_pokes(chan, FREE + 0x50000)))
        cases.append(dict(label=f'C 001D2090 ch{chan}', entry=0x1D2090, args=[chan, rng.getrandbits(32)],
                          pokes=cursor_pokes(chan, FREE + 0x50000)))
        cases.append(dict(label=f'C 001D6F60 ch{chan}', entry=0x1D6F60,
                          args=[chan, ('w', rng.getrandbits(64)), rng.getrandbits(32)],
                          pokes=cursor_pokes(chan, FREE + 0x50000)))
        cases.append(dict(label=f'C 001D7000 ch{chan}', entry=0x1D7000, args=[chan, rng.getrandbits(32)],
                          pokes=cursor_pokes(chan, FREE + 0x50000)))
        cases.append(dict(label=f'C 001D71A0 ch{chan}', entry=0x1D71A0, args=[chan, rng.getrandbits(16)],
                          pokes=cursor_pokes(chan, FREE + 0x50000)))
    cases.append(dict(label='C 001D4DA0', entry=0x1D4DA0, pokes=cursor_pokes(0, FREE + 0x50000)))
    for i, (n, src) in enumerate([(0x10, 0x253560), (0x80, 0x253570), (0x40, 0x253570), (0x30, 0x253570),
                                  (0x28, 0x253570), (0x7, 0x253568), (0x21, 0x253564), (0, 0x253560),
                                  (0x100, 0x253560)]):
        cases.append(dict(label=f'C 001D7100 n={n:#x} src={src:#x}', entry=0x1D7100,
                          args=[3, rng.choice([0, 0x81, 0x102, 0x200, 0x3FF]), src, n],
                          pokes=cursor_pokes(3, FREE + 0x60000)))
    for i, (dst, src, n) in enumerate([(FREE + 0x70000, 0x253560, 0x90), (FREE + 0x70004, 0x253560, 0x40),
                                       (FREE + 0x70000, 0x253561, 0x33), (FREE + 0x70000, FREE + 0x70010, 0x50),
                                       (FREE + 0x70020, FREE + 0x70000, 0x50), (FREE + 0x70000, 0x253560, 0x1F),
                                       (FREE + 0x70000, 0x253560, 0x20), (FREE + 0x70000, 0x253560, 0x38),
                                       # 8 mod 16 (both, dst only, src only) with n >= 0x20: the
                                       # quadword path needs (src | dst) & 15 == 0, not & 7 (review m21)
                                       (FREE + 0x70008, 0x253568, 0x40), (FREE + 0x70008, 0x253560, 0x40),
                                       (FREE + 0x70000, 0x253568, 0x40), (FREE + 0x70008, 0x253568, 0x20)]):
        cases.append(dict(label=f'C 00121870 #{i}', entry=0x121870, args=[dst, src, n]))
    for i in range(scale['matrix']):
        pokes = [(FREE + 0x71000 + 4 * k, w32(rfloat(rng, -4.0, 4.0))) for k in range(48)]
        a, b, d = FREE + 0x71000, FREE + 0x71040, FREE + 0x71080
        cases.append(dict(label=f'C 001026D0 #{i}', entry=0x1026D0,
                          args=[[d, a, b][i % 3], a, [b, a, b][i % 3]], pokes=pokes))
        cases.append(dict(label=f'C 00102798 #{i}', entry=0x102798, args=[[d, a][i % 2], a], pokes=pokes))
        cases.append(dict(label=f'C 00102958 #{i}', entry=0x102958, args=[[d, a + 0x10][i % 2], a], pokes=pokes))
    for i, oid in enumerate([0, 1, 701, 0x8001, 0x18002, 350]):
        cases.append(dict(label=f'C 001C6120 id {oid:#x}', entry=0x1C6120, args=[bank, oid]))
    cases.append(dict(label='C 001D52E0', entry=0x1D52E0))
    # 001D5370's other loop bounds and the 001D5BD0 key list
    keys = ((0x1300, 5), (0x0D00, 3), (0x0100, 0)) + (((0x1300, 4), (0x1300, 2), (0x0D00, 4)) if RM.FULL else ())
    for key, room in keys:
        cases.append(dict(label=f'C 001D5370 key {key:#06x} room {room}', entry=0x1D5370,
                          pokes=[(0x810700, bytes([key >> 8, key & 0xFF, room])),
                                 (ch(0), w32(FREE + 0xA0000))]))
    # 001C1D00 state byte 0 / 2 and key 0x1500 (flag 0x24 on and off)
    # (quick: key 0x1500 with flag 0x24; the state bytes 0 / 2 of the reused
    # em_frh_001C1D00 and 0x1500 without flag 0x24 in full mode)
    states = ((1, 0x1500, 1),) + (((0, 0x0B00, 0), (2, 0x0B00, 0), (1, 0x1500, 0)) if RM.FULL else ())
    for sb, key, on24 in states:
        cases.append(dict(label=f'C 001C1D00 state {sb} key {key:#06x} f24 {on24}', entry=0x1C1D00,
                          args=[0x8101D0],
                          pokes=[(0x8101D0, bytes([sb])), (0x810700, bytes([key >> 8, key & 0xFF])),
                                 (ch(0), w32(FREE + 0xA0000)), (ch(3), w32(FREE + 0xC0000))]
                          + flag_pokes(ctx, base, {0x24: on24})))
    # 001E0CF0 flag scripts: 0x20 off; 0x21 off; 0x22 on
    scripts = ((0, 1, 0), (1, 0, 0), (1, 1, 1)) + (((1, 0, 1),) if RM.FULL else ())
    for f20, f21, f22 in scripts:
        cases.append(dict(label=f'C 001E0CF0 flags {f20}{f21}{f22}', entry=0x1E0CF0,
                          pokes=flag_pokes(ctx, base, {0x20: f20, 0x21: f21, 0x22: f22})
                          + [(ch(3), w32(FREE + 0xC0000)), (ctx + 0x2524, w32(rng.getrandbits(31)))]))
    return [('opening', cases)]


def failstop(native):
    """D: fail-stop over the opening capture (native only; nothing written
    before the fault is checked where the contract promises it)."""
    base = bytearray(CAPTURES[0][1].read_bytes())
    spad = bytearray(CAPTURES[0][2].read_bytes())
    ctx = rd(base, CTX_PTR) & (RAM_SIZE - 1)
    pmem = (C.c_ubyte * RAM_SIZE).from_buffer(base)
    pspad = (C.c_ubyte * SPAD_SIZE).from_buffer(spad)

    def run(entry, args, flags, cut=0):
        a = (C.c_uint64 * 8)(*[sx32(x) & MASK64 for x in list(args) + [0] * (8 - len(args))])
        rec = (C.c_uint64 * 1)()
        dt = (C.c_uint32 * 1)()
        used, mismatch, why, result = C.c_uint32(), C.c_uint32(), C.c_uint32(), C.c_uint32()
        fault = (C.c_int32 * 4)()
        rc = native.sw_run(pmem, pspad, entry, a, rec, 0, dt, b'\0', flags | 4, cut, C.byref(used),
                           C.byref(mismatch), C.byref(why), fault, C.byref(result))
        return rc, [x & MASK for x in fault]

    results = []
    snapshot = bytes(base)
    # a reached NULL host worker: flag 0x23 on, no host workers -> the sound
    # branch's first host callee (00122BB8) faults; nothing is reached before
    # 001E1E60 checks its workers, so no byte is written.
    word = rd(base, ctx + 0x174)
    base[ctx + 0x174:ctx + 0x178] = w32(word | 1 << 3 | 1 << 4)
    before = bytes(base)
    rc, f = run(0x1E1E60, [ctx + 0x180, 3], 1)
    assert rc == -1 and f[0] == 6 and f[1] == 0x122BB8, ('NULL host worker', f)
    results.append('001E1E60 with flag 0x23 and no host workers: fault at 00122BB8')
    base[:] = snapshot
    # 001D5BD0 unbound: key 0x0100 reaches it after the grid pass
    base[0x810700:0x810702] = bytes([0x01, 0x00])
    rc, f = run(0x1D5370, [], 1)
    assert rc == -1 and f[0] == 6 and f[1] == 0x1D5BD0, ('001D5BD0 unbound', f)
    results.append('001D5370 key 0x0100 with no host workers: fault at 001D5BD0')
    base[:] = snapshot
    # the bank view missing: 001D52E0 faults on the bank word's target
    rc, f = run(0x1D52E0, [], 2, cut=0x1000000)
    assert rc == -1 and f[2] != 0, ('missing bank view', f)
    results.append('001D52E0 without the bank view: fault (module %d, %06X, code %d)' % (f[0], f[1], f[2]))
    base[:] = snapshot
    # a misaligned channel-0 cursor: 001D4DA0's first tag store faults
    base[ctx + 0x10:ctx + 0x14] = w32(FREE + 2)
    before = bytes(base)
    rc, f = run(0x1D4DA0, [], 0)
    assert rc == -1 and f[0] == 1 and f[1] == 0x1D4750 and f[2] == 4, ('misaligned cursor', f)
    diff = [i for i in range(FREE, FREE + 0x200) if base[i] != before[i]]
    # as on the EE: the tag's byte store (+3) lands, the word store at +4
    # takes the address error
    assert diff == [FREE + 5] and base[FREE + 5] == 0x10, ('bytes at a misaligned cursor', diff[:4])
    results.append('001D4DA0 with a misaligned cursor: fault at 001D4750 (address) at the tag\'s '
                   'first word store; only the byte store before it landed')
    base[:] = snapshot
    # a latched fault: the second call (flags 8) must not run
    base[0x810700:0x810702] = bytes([0x01, 0x00])
    rc, f = run(0x1D5370, [], 1 | 8)
    assert rc == -1 and f[1] == 0x1D5BD0, ('latched', f)
    results.append('a latched fault stops the next entry')
    base[:] = snapshot
    return results


# ======================================================================
# E. The binding rehearsal: the live storage plus the named additions
# ======================================================================

# Exactly the views em_render_context_live builds (build_views(): its own
# storage, then every external view the binder and the frame loop hand over;
# ARENA_END 0x0076B5C0, CTX_END 0x00817240), with its read-only marking
# (1 = read-only), then the six the doc says to add. 26 views: em_rcl's
# 24-entry view arrays must grow (docs/STATIC_WORLD.md 5.2).
LIVE_RANGES = [
    (0x0028F700, 0x0076B5C0 - 0x0028F700, 0, 'the packet arena D_0028F700 (ARENA_BASE..ARENA_END)'),
    (0x00811CC0, 0x00817240 - 0x00811CC0, 0, 'the context, GS blocks, skin records (CTX_BASE..CTX_END)'),
    (0x00250F30, 0x2250, 0, 'D_00250F30..D_0025317F'),
    (0x00275670, 0x30, 0, 'D_00275670..D_0027569F (.data)'),
    (0x70003A40, 0x100, 0, 'scratchpad P / K copies (D_70003AC0 inside)'),
    (0x70003B60, 4, 0, 'scratchpad D_70003B60'),
    (0x70003B70, 4, 0, 'scratchpad D_70003B70'),
    (0x00241010, 8, 1, 'D_00241010'),
    (0x0026E510, 16, 1, 'D_0026E510'),
    (0x0026E850, 16, 1, 'D_0026E850'),
    (0x00810E80, 2, 1, 'external D_00810E80'),
    (0x00810610, 0x40, 1, 'external D_00810610 (the live camera pool)'),
    (0x008105E0, 0x10, 1, 'external D_008105E0'),
    (0x008106B0, 0x48, 1, 'external D_008106B0'),
    (0x00810700, 3, 1, 'external: the area bytes D_00810700..702'),
    (0x008101E4, 1, 1, 'external D_008101E4'),
    (0x70003B8D, 1, 1, 'external scratchpad D_70003B8D'),
    (0x008102B0, 0x320, 1, 'external D_008102B0'),
    (0x00810E88, 2, 1, 'external D_00810E88'),
    (0x008106C4, 1, 1, 'external D_008106C4'),
]
ADDED_RANGES = [
    (0x01516F40, 0x2D6FE0, 1, 'the static-object bank (export_static_world.py)'),
    (0x0028A5A0, 4, 1, 'D_0028A5A0: the bank address word'),
    (0x00253560, 0x90, 0, 'D_00253560..D_002535EF (export_static_world.py)'),
    (0x00817240, 0x80, 0, 'D_00817240..BF: 001D4750 constant block (.bss)'),
    (0x70003400, 0x80, 0, 'scratchpad D_70003400 / D_70003440 (001D5370 clip matrices)'),
    (0x008101D0, 1, 0, 'D_008101D0: 001C1D00 state byte'),
]


def native_views(st, ranges, read_only):
    bufs, ptrs = [], []
    for a, n, _, _ in ranges:
        src = st['sbase'][a - SPAD:a - SPAD + n] if a >= SPAD else st['base'][a:a + n]
        b = (C.c_ubyte * n).from_buffer_copy(src)
        bufs.append(b)
        ptrs.append(C.cast(b, C.c_void_p))
    addr = (C.c_uint32 * len(ranges))(*[a for a, _, _, _ in ranges])
    size = (C.c_uint32 * len(ranges))(*[n for _, n, _, _ in ranges])
    flags = (C.c_uint8 * len(ranges))(*read_only)
    pv = (C.c_void_p * len(ranges))(*ptrs)
    fault = (C.c_int32 * 4)()
    rc = NATIVE.sw_run_views(len(ranges), addr, size, pv, flags, 0x8101D0, fault)
    return rc, list(fault), bufs


def rehearsal(names):
    lines = []
    ranges = LIVE_RANGES + ADDED_RANGES

    def hits(w, at, sz, pick):
        lo = at + (SPAD if w == 'spad' else 0)
        return any(pick(ro) and r < lo + sz and lo < r + n for r, n, ro, _ in ranges)

    for k, name in enumerate(names):
        st = state(name)
        ee = st['ee']
        ee.dirty, ee.cp, ee.mark, ee.outcomes, ee.script = [], [], 0, set(), {}
        try:
            ee.call_entry(0x1C1D00, [0x8101D0])
            rc, fault, bufs = native_views(st, ranges, [r for _, _, r, _ in ranges])
            assert rc == 0, (name, 'rehearsal faulted', [hex(x & MASK) for x in fault])
            for (a, n, _, what), b in zip(ranges, bufs):
                want = bytes(ee.spad[a - SPAD:a - SPAD + n]) if a >= SPAD else bytes(ee.mem[a:a + n])
                assert bytes(b) == want, (name, 'rehearsal view differs', what)
            # every byte the original wrote lies inside the views ...
            outside = [(w, at) for w, at, sz in ee.dirty
                       if not all(hits(w, at + i, 1, lambda ro: True) for i in range(sz))]
            assert not outside, (name, 'the original wrote outside the views', outside[:3])
            # ... and none inside a view the binder marks read-only (em_render_context
            # has no read-only notion, so the native side alone would not notice)
            inro = [(w, at) for w, at, sz in ee.dirty if hits(w, at, sz, lambda ro: ro)]
            assert not inro, (name, 'the original wrote into a read-only view', inro[:3])
            if k == 0:
                # the marking is enforced: D_00253560 read-only -> 001E1E60's first
                # store into D_00253570.. faults (em_static_world, READ_ONLY)
                flags = [1 if a == 0x00253560 else r for a, _, r, _ in ranges]
                rc, fault, _ = native_views(st, ranges, flags)
                assert rc == -1 and fault[0] == 1 and fault[2] == 8 and 0x253560 <= fault[3] < 0x2535F0, \
                    (name, 'read-only view not enforced', [hex(x & MASK) for x in fault])
            lines.append(name)
        finally:
            restore(st, ee.dirty)
    return lines


# ======================================================================
# F. Sanitizers (EM_TEST_FULL=1): the same shim as an ASan/UBSan program
# ======================================================================

ASAN_MAIN = r'''
#include <stdio.h>
#include <stdlib.h>
int sw_run_views(uint32_t n, const uint32_t *addr, const uint32_t *size, uint8_t **bytes,
                 const uint8_t *read_only, uint32_t state_address, int32_t fault[4]);
int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    uint8_t *ram = malloc(0x2000000), *spad = calloc(1, 0x4000);
    if (!ram || !spad || fread(ram, 1, 0x2000000, f) != 0x2000000) return 2;
    fclose(f);
    const uint32_t addr[3] = {0u, 0x70000000u, 0u};
    const uint32_t size[3] = {0x2000000u, 0x4000u, 0u};
    uint8_t *bytes[3] = {ram, spad, NULL};
    int32_t fault[4];
    int rc = 0;
    for (int flags = 0; flags < 8 && rc == 0; ++flags) {
        uint32_t flagword;
        memcpy(&flagword, ram + 0x811CC0u + 0x174u, 4);
        flagword = (flagword & ~0x1Cu) | ((uint32_t)flags << 2);   /* flags 0x22, 0x23, 0x24 */
        memcpy(ram + 0x811CC0u + 0x174u, &flagword, 4);
        rc = sw_run_views(2, addr, size, bytes, NULL, 0x8101D0u, fault);
    }
    printf("asan: rc %d\n", rc);
    free(ram);
    free(spad);
    return rc == 0 ? 0 : 1;
}
'''


def sanitizers():
    exe = OUT / 'sw_asan'
    main_c = OUT / 'sw_asan_main.c'
    main_c.write_text('#include <stdint.h>\n#include <string.h>\n' + ASAN_MAIN)
    subprocess.run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-ffp-contract=off',
                    '-Isrc', str(OUT / 'sw_shim.c'), str(main_c), *SOURCES, '-o', str(exe)],
                   cwd=ROOT, check=True)
    r = subprocess.run([str(exe), str(CAPTURES[0][1])], capture_output=True, text=True)
    assert r.returncode == 0 and 'asan: rc 0' in r.stdout, ('sanitizer run', r.returncode, r.stderr[-600:])
    return 'ASan/UBSan: 001C1D00 over the opening capture with flags 0x22..0x24 in all 8 states: clean'


# ======================================================================
# Main
# ======================================================================

def branch_sites(elf):
    ee = shared.EE(elf)
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


def check_callees(elf):
    """Every jal/j target of the translated routines is traced, stubbed, or
    a leaf translated inside its caller."""
    ee = shared.EE(elf)
    targets = set()
    for start, size in SIZES.items():
        targets |= rcref.jal_targets(ee, start, size)
    missing = sorted(t for t in targets if t not in SPEC)
    assert not missing, ('callees neither traced nor stubbed', [hex(t) for t in missing])
    return len(targets)


def main():
    global ELF, NATIVE
    t0 = time.time()
    ELF = read_elf()
    rcref.ELF = ELF
    NATIVE = build_native()
    ncallees = check_callees(ELF)
    rng = random.Random(0x5717)
    full = RM.FULL
    names = [c[0] for c in CAPTURES]
    beats = RM.select(names, 2, 0x5A, keep=lambda i, n: n in ('opening', '14_roger_encounter'))
    whole = beats if full else ['opening']
    scale = dict(bg=RM.pick(48, 16), grid=RM.pick(6, 1), point=RM.pick(64, 16), matrix=RM.pick(12, 3))
    groups = capture_cases(beats, whole) + unit_cases(rng, scale)
    # split the unit group so forked workers share the load
    # (the whole-tree cases each get a group of their own, the rest in 12s)
    heavy = lambda c: c['entry'] in (0x1C1D00, 0x1D5370, 0x1E1AD0, 0x1E0CF0)
    work = []
    for name, cases in groups:
        work += [(name, [c]) for c in cases if heavy(c)]
        light = [c for c in cases if not heavy(c)]
        for k in range(0, len(light), 12):
            work.append((name, light[k:k + 12]))
    results = RM.parallel_map(run_group, work, cost=lambda g: sum(
        30 if c['entry'] in (0x1C1D00, 0x1D5370, 0x1E0CF0) else (80 if c['entry'] == 0x1E1AD0 else 1)
        for c in g[1]))
    outcomes, cps, ch3, ch0 = set(), 0, {}, {}
    ncases = 0
    for group in results:
        for label, cover, n, checked in group:
            outcomes |= cover
            cps += n
            ncases += 1
            if label.startswith('B channel 3'):
                ch3[label.split()[-1]] = checked
            if label.startswith('B channel 0'):
                ch0[label.split()[-1]] = checked
    sites = branch_sites(ELF)
    missed = sorted((pc, t) for pc in sites for t in (True, False)
                    if (pc, t) not in outcomes and (pc, t) not in UNREACHABLE)
    assert not missed, ('branch outcomes never reached', [(hex(p), t) for p, t in missed])
    fs = failstop(NATIVE)
    rehearsed = rehearsal(beats if full else ['opening'])
    san = sanitizers() if full else None
    RM.banner(RM.part(len(beats), len(names), 'captures'), f'{ncases} cases', f'{cps:,} checkpoints')
    print(f'A/B: 001C1D00 whole tree equal over {len(whole)} capture(s); {len(beats)} captures: channel-3 lists rebuilt equal to the '
          f'capture ({", ".join(f"{k} {v:#x}" for k, v in sorted(ch3.items()))} bytes); channel-0 static '
          f'runs rebuilt equal ({", ".join(f"{k} {v:#x}" for k, v in sorted(ch0.items()))} bytes)')
    print(f'C: {len(sites)} branch sites, both outcomes reached except {len(UNREACHABLE)} listed; '
          f'{ncallees} callee addresses accounted for')
    for line in fs:
        print('D:', line)
    print(f'E: 001C1D00 over em_rcl\'s {len(LIVE_RANGES)} views + {len(ADDED_RANGES)} added '
          f'({sum(r for _, _, r, _ in LIVE_RANGES + ADDED_RANGES)} read-only; {", ".join(rehearsed)}): '
          f'no fault, every view equal to the original, no original write outside them or into a '
          f'read-only one; a read-only view is enforced')
    if san:
        print('F:', san)
    print(f'test_static_world_reference: PASS ({time.time() - t0:.1f} s)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
