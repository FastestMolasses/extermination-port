#!/usr/bin/env python3
"""The load veil's frame through the GS frame stage (em_gfx_gs_frame, the
Metal backend) against a model of the GS pixel path (docs/
LOAD_VEIL_PARTICLES.md section 4).

The packets are the port's translation of 0021B1B0 (em_load_veil_particles,
proven against the executed original by test_load_veil_particles_reference)
over the GS blocks of 001D0F20 (em_gs_blocks_original, proven by
test_gs_blocks_reference), with the SDK block D_00241010 and the frame-copy
colour D_0026E880 read from the user's pinned ELF. A test list stands for
step V's: the slot's draw environment (bank A), the black clear (bank C,
+0x420), then the veil's channel-0 run up to a cut, then the END tag at the
GS block + 0x10. em_chain_page's list mode walks it and em_gfx_gs_frame draws
it in a headless Metal window; the GS surfaces are read back.

Cuts of one visible veil (level 1.0, a sweep phase), both buffer slots:
  1. after the 512 lines: every lit pixel of the frame lies on a segment's
     column span, in its Y range (one pixel of slack for the line rule),
     with a colour between the segment's two vertex colours; every other
     pixel is the clear (0, 0, 0, 0x80); every segment brighter than the
     clear lights a pixel;
  2. after the first frame copy: the 256 x 256 surface at GS 0x258000
     equals the model of the copy sprite (U / V at the GS sample point,
     bilinear, REGION_CLAMP 0..511 x 0..223, DECAL, TCC 0) over the frame
     read back after cut 1, every pixel;
  3. after the first lens pass: the frame equals the model of the 450
     opaque strip triangles (S / Q, T / Q per pixel, bilinear, REGION_CLAMP
     0..255, MODULATE with 0x80) over the surface read back after cut 2, on
     every pixel whose sample is unambiguous (not within 1e-4 of a triangle
     edge, its texel coordinate not within 1e-3 of a 1/16 step): Metal's
     float interpolation stands for the GS DDA there;
  4. the whole run: the copy of the frame after cut 3, then the frame plus
     the additive pass (colour 0x40, ALPHA Cs * 0x80 >> 7 + Cd, COLCLAMP).
A black veil (level 0, the live route's case at host speed) draws an all-
black frame. Refusals: a Z test other than ALWAYS, a texture read of the
surface being drawn, a PSMCT16 FRAME: -1, nothing drawn.

Skipped (reported) only on a machine without a Metal device; on one with
a device a harness that does not build or open FAILS. About 6 s.
"""
from __future__ import annotations

import ctypes as C
import math
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_shadow_original_reference as SO  # noqa: E402
from test_chain_page_reference import GsPrim  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
OUT = ROOT / 'build/load_veil_gpu'
ELF_SHA = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
GS = 0x814220
CAPTURE_FBP, CAPTURE_FBW = 0x258000 >> 13, 4


class GsEnv(C.Structure):
    _fields_ = [('frame', C.c_uint64), ('zbuf', C.c_uint64), ('xyoffset', C.c_uint64), ('scissor', C.c_uint64),
                ('prmodecont', C.c_uint64), ('dthe', C.c_uint64), ('fba', C.c_uint64), ('pabe', C.c_uint64),
                ('texa', C.c_uint64), ('scanmsk', C.c_uint64), ('set', C.c_uint32)]


SHIM = r'''
#include <string.h>
#include "em_gfx.h"
#include "game/em_chain_page.h"
#include "game/em_gs_blocks_original.h"
#include "game/em_load_veil_particles.h"
#include "game/em_sdk_math_original.h"
#include "game/em_player_stage_workers.h"

#define BASE 0x800000u
#define SIZE 0x100000u
#define GS   0x814220u
#define RUN  0x830000u
#define COPY 0x860000u
#define LIST 0x850000u
static uint8_t mem[SIZE];
static uint8_t table[EM_LOAD_VEIL_PARTICLES_TABLE_BYTES];
static uint32_t cursor[4], slot9c, gsbase = GS, capture = 0x258000u;
static uint8_t d241010[8], d26e880[16];

static int w_fabs(void *c, uint32_t x, uint32_t *out)
{ (void)c; float f, g; memcpy(&f, &x, 4); g = em_sdk_math_original_0011DF78(f); memcpy(out, &g, 4); return 0; }
static int w_toint(void *c, uint32_t x, int32_t *out) { (void)c; *out = em_player_float_to_int(x); return 0; }

static void tag(uint32_t at, uint32_t id, uint32_t qwc, uint32_t addr)
{
    uint8_t *p = mem + (at - BASE);
    memset(p, 0, 16);
    p[0] = (uint8_t)qwc; p[1] = (uint8_t)(qwc >> 8); p[3] = (uint8_t)(id << 4);
    memcpy(p + 4, &addr, 4);
}

int shim_sizes(void) { return (int)(sizeof(EmGfxGsPrim) | sizeof(EmGfxGsEnv) << 12); }

/* The GS blocks, then 0021B1B0 over (phase, level, base Y) for `slot`.
 * Step V's half-pixel XYOFFSET_1 on slot 0's environment (the captures'
 * phase). Returns the run's length, or -1. */
int shim_veil(uint32_t phase, uint32_t level, uint32_t base_y, uint32_t slot, const uint8_t *sdk,
              const uint8_t *col)
{
    memset(mem, 0, sizeof mem);
    memcpy(d241010, sdk, 8);
    memcpy(d26e880, col, 16);
    if (em_gs_blocks_001D0F20(mem + (GS - BASE), d241010) < 0) return -1;
    uint8_t *xy = mem + (GS - BASE) + 0x20u + 0x40u;
    uint64_t v; memcpy(&v, xy, 8); v += (uint64_t)8 << 32; memcpy(xy, &v, 8);
    slot9c = slot;
    cursor[0] = RUN;
    EmLoadVeilParticles s;
    memset(&s, 0, sizeof s);
    s.world.cursor = cursor; s.world.cursor_count = 4; s.world.ctx_9C = &slot9c;
    s.world.d00275674 = &gsbase; s.world.d0027568C = &capture; s.world.d0026E880 = d26e880;
    s.world.d00241010 = d241010; s.world.packet = mem; s.world.packet_address = BASE;
    s.world.packet_size = SIZE; s.world.table = table;
    s.workers.w_0011DF78 = w_fabs; s.workers.w_001281C0 = w_toint;
    uint32_t seed = 0;
    float lv; memcpy(&lv, &level, 4);
    EmLoadVeilParticlesBlock b = { &phase, &lv, &seed, &base_y };
    if (em_load_veil_particles_0021B1B0(&s, &b) < 0) return -1;
    return (int)(cursor[0] - RUN);
}

/* The test list: bank A of `slot`, the black clear, the run's first `cut`
 * bytes (a copy), the END tag at GS + 0x10. Returns its address. */
uint32_t shim_list(uint32_t slot, uint32_t cut)
{
    memcpy(mem + (COPY - BASE), mem + (RUN - BASE), cut);
    tag(LIST, 3, 0x19, GS + 0x20u + 0x190u * slot);
    tag(LIST + 0x10, 3, 8, GS + 0x420u);
    tag(LIST + 0x20, 2, 0, COPY);
    tag(COPY + cut, 2, 0, GS + 0x10u);
    return LIST;
}

static const uint8_t *reader(void *c, uint32_t a, uint32_t n)
{ (void)c; return a >= BASE && n <= SIZE && a - BASE <= SIZE - n ? mem + (a - BASE) : NULL; }

static EmChainPage page;
int shim_walk(uint32_t chain, EmGfxGsPrim *prims, EmGfxGsEnv *envs, uint32_t cap)
{
    memset(&page, 0, sizeof page);
    page.read = reader; page.prims = prims; page.prim_env = envs; page.prim_capacity = cap;
    if (em_chain_page_run_list(&page, chain) < 0) return -1;
    return (int)page.prim_count;
}

void shim_env(uint32_t slot, uint64_t *frame, uint64_t *scissor)
{
    const uint8_t *e = mem + (GS - BASE) + 0x20u + 0x190u * slot;
    memcpy(frame, e + 0x20, 8);
    memcpy(scissor, e + 0x50, 8);
}
'''


def fail(msg):
    raise AssertionError(msg)


def build(metal):
    src = OUT / 'veil_shim.c'
    src.write_text(SHIM)
    lib_path = OUT / 'veil_shim.dylib'
    sources = [src, ROOT / 'src/game/em_chain_page.c', ROOT / 'src/game/em_gs_blocks_original.c',
               ROOT / 'src/game/em_load_veil_particles.c', ROOT / 'src/game/em_sdk_math_original.c',
               ROOT / 'src/game/em_player_stage_workers.c']
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared', '-fPIC',
                    '-I' + str(ROOT / 'src')] + [str(q) for q in sources] + ['-o', str(lib_path)], check=True)
    lib = C.CDLL(str(lib_path))
    lib.shim_veil.argtypes = [C.c_uint32, C.c_uint32, C.c_uint32, C.c_uint32, C.c_char_p, C.c_char_p]
    lib.shim_list.restype = C.c_uint32
    lib.shim_list.argtypes = [C.c_uint32, C.c_uint32]
    lib.shim_walk.argtypes = [C.c_uint32, C.POINTER(GsPrim), C.POINTER(GsEnv), C.c_uint32]
    lib.shim_env.argtypes = [C.c_uint32, C.POINTER(C.c_uint64), C.POINTER(C.c_uint64)]
    s = lib.shim_sizes()
    if (s & 0xFFF, s >> 12) != (C.sizeof(GsPrim), C.sizeof(GsEnv)):
        fail('ctypes layout differs from em_gfx.h')
    m = metal.lib
    m.em_gfx_gs_frame.argtypes = [C.c_void_p, C.POINTER(GsPrim), C.POINTER(GsEnv), C.c_uint32, C.c_uint64,
                                  C.c_uint64]
    m.em_gfx_gs_surface_read.argtypes = [C.c_void_p, C.c_uint32, C.c_uint32, C.c_uint32, C.c_char_p]
    return lib


def f32(bits):
    return struct.unpack('<f', struct.pack('<I', bits))[0]


class Frame:
    def __init__(self, metal, lib):
        self.metal, self.lib = metal, lib

    def draw(self, slot, cut, prims_hook=None):
        lib, m, gfx = self.lib, self.metal.lib, self.metal.gfx
        chain = lib.shim_list(slot, cut)
        prims, envs = (GsPrim * 4096)(), (GsEnv * 4096)()
        n = lib.shim_walk(chain, prims, envs, 4096)
        if n < 0:
            fail('the test list does not walk')
        if prims_hook:
            prims_hook(prims, envs, n)
        frame, scissor = C.c_uint64(), C.c_uint64()
        lib.shim_env(slot, C.byref(frame), C.byref(scissor))
        m.em_gfx_begin_frame(gfx, 0.0, 0.0, 0.0, 1.0)
        rc = m.em_gfx_gs_frame(gfx, prims, envs, n, frame.value, scissor.value)
        m.em_gfx_end_frame(gfx)
        return rc, prims, envs, n, frame.value

    def surface(self, fbp, fbw, height):
        buf = C.create_string_buffer(64 * fbw * height * 4)
        if self.metal.lib.em_gfx_gs_surface_read(self.metal.gfx, fbp, fbw, height, buf) != 0:
            fail(f'surface {fbp:#x} not readable')
        return buf.raw


# ------------------------------------------------------------- the model
def texel(img, w, x, y):
    o = 4 * (w * y + x)
    return img[o:o + 4]


def bilinear(img, w, u16, v16, clamp_u, clamp_v):
    uu, vv = math.floor(u16) - 8, math.floor(v16) - 8
    fu, fv = uu & 15, vv & 15
    x0, x1 = clamp_u(uu >> 4), clamp_u((uu >> 4) + 1)
    y0, y1 = clamp_v(vv >> 4), clamp_v((vv >> 4) + 1)
    a, b, c, d = texel(img, w, x0, y0), texel(img, w, x1, y0), texel(img, w, x0, y1), texel(img, w, x1, y1)
    return [(a[k] * (16 - fu) * (16 - fv) + b[k] * fu * (16 - fv) + c[k] * (16 - fu) * fv + d[k] * fu * fv) >> 8
            for k in range(4)]


def model_copy(frame_img, sprite, env):
    """The copy sprite (PRIM 0x116: UV, DECAL, TCC 0, colour 0x80) into the
    256 x 256 surface: every pixel."""
    a, b = sprite.v[0], sprite.v[1]
    ox, oy = env.xyoffset & 0xFFFF, (env.xyoffset >> 32) & 0xFFFF
    x0, y0, x1, y1 = a.x - ox, a.y - oy, b.x - ox, b.y - oy
    cu = lambda x: min(max(x, 0), 511)
    cv = lambda y: min(max(y, 0), 223)
    out = bytearray(256 * 256 * 4)
    alpha = b.rgba[3]
    for py in range(256):
        v16 = a.v + (py * 16 - y0) * (b.v - a.v) / (y1 - y0)
        for px in range(256):
            u16 = a.u + (px * 16 - x0) * (b.u - a.u) / (x1 - x0)
            t = bilinear(frame_img, 512, u16, v16, cu, cv)
            o = 4 * (256 * py + px)
            out[o:o + 4] = bytes((t[0], t[1], t[2], alpha))
    return bytes(out)


def model_strips(tex_img, prims, envs, idx, base_img, additive):
    """The lens strips over the frame: per pixel the covering triangle's
    S, T, Q (screen-linear), then S / Q, T / Q, bilinear, MODULATE. Returns
    (image, unambiguous pixel set)."""
    out = bytearray(base_img)
    sure = set()
    cu = lambda x: min(max(x, 0), 255)
    for i in idx:
        p, e = prims[i], envs[i]
        ox, oy = e.xyoffset & 0xFFFF, (e.xyoffset >> 32) & 0xFFFF
        pts = [((v.x - ox) / 16.0, (v.y - oy) / 16.0, f32(v.s), f32(v.t), f32(v.q)) for v in p.v[:3]]
        (xa, ya, *_), (xb, yb, *_), (xc, yc, *_) = pts
        area = (xb - xa) * (yc - ya) - (yb - ya) * (xc - xa)
        if area == 0:
            continue
        rgba = p.v[2].rgba
        for py in range(max(0, math.floor(min(ya, yb, yc))), min(224, math.ceil(max(ya, yb, yc)) + 1)):
            for px in range(max(0, math.floor(min(xa, xb, xc))), min(512, math.ceil(max(xa, xb, xc)) + 1)):
                w0 = ((xb - px) * (yc - py) - (yb - py) * (xc - px)) / area
                w1 = ((xc - px) * (ya - py) - (yc - py) * (xa - px)) / area
                w2 = 1.0 - w0 - w1
                if min(w0, w1, w2) < -1e-4:
                    continue
                s = w0 * pts[0][2] + w1 * pts[1][2] + w2 * pts[2][2]
                t = w0 * pts[0][3] + w1 * pts[1][3] + w2 * pts[2][3]
                q = w0 * pts[0][4] + w1 * pts[1][4] + w2 * pts[2][4]
                u16, v16 = s / q * 256 * 16, t / q * 256 * 16
                amb = min(w0, w1, w2) < 1e-4 or abs(u16 - round(u16)) < 1e-3 or abs(v16 - round(v16)) < 1e-3
                tt = bilinear(tex_img, 256, u16, v16, cu, cu)
                c = [min((tt[k] * rgba[k]) >> 7, 255) for k in range(3)]
                o = 4 * (512 * py + px)
                if additive:
                    c = [min(c[k] + base_img[o + k], 255) for k in range(3)]
                out[o:o + 4] = bytes((c[0], c[1], c[2], rgba[3]))
                if amb:
                    sure.discard((px, py))
                else:
                    sure.add((px, py))
    return bytes(out), sure


def check_lines(img, prims, envs, n, stats):
    """Every lit pixel lies within a segment's X and Y span (one pixel of
    slack for the line rule) with a colour between its two vertices'."""
    lines = [i for i in range(n) if prims[i].prim & 7 == 1]
    if len(lines) != 512:
        fail(f'{len(lines)} lines, not 512')
    segs = []
    for i in lines:
        a, b, e = prims[i].v[0], prims[i].v[1], envs[i]
        ox, oy = e.xyoffset & 0xFFFF, (e.xyoffset >> 32) & 0xFFFF
        segs.append(((a.x - ox) / 16.0, (a.y - oy) / 16.0, (b.x - ox) / 16.0, (b.y - oy) / 16.0,
                     tuple(a.rgba), tuple(b.rgba)))
    lit = 0
    for y in range(224):
        for x in range(512):
            o = 4 * (512 * y + x)
            px = img[o:o + 4]
            if px == bytes((0, 0, 0, 0x80)):
                continue
            lit += 1
            if not any(min(xa, xb) - 1 <= x <= max(xa, xb) + 1 and min(ya, yb) - 1 <= y <= max(ya, yb) + 1 and
                       all(min(ca[k], cb[k]) <= px[k] <= max(ca[k], cb[k]) for k in range(4))
                       for xa, ya, xb, yb, ca, cb in segs):
                fail(f'lit pixel ({x}, {y}) {tuple(px)} lies on no segment')
    if lit < 256:
        fail(f'only {lit} line pixels lit')
    stats['line_pixels'] += lit


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    import hashlib
    if hashlib.sha256(elf).hexdigest() != ELF_SHA:
        fail('boot ELF is not the pinned build')
    off = lambda a: a - 0x100000 + 0x300
    sdk, col = elf[off(0x241010):off(0x241010) + 8], elf[off(0x26E880):off(0x26E880) + 16]
    metal = SO.open_metal(OUT, 'load veil gpu')
    if metal is None:
        return 0
    lib = build(metal)
    fr = Frame(metal, lib)
    stats = {'line_pixels': 0, 'copy_pixels': 0, 'strip_pixels': 0, 'strip_skipped': 0, 'refused': 0}
    one = struct.unpack('<I', struct.pack('<f', 1.0))[0]
    for slot, phase in ((0, 0.37), (1, 0.81)):
        fbp = 0x38 if slot == 0 else 0
        run = lib.shim_veil(struct.unpack('<I', struct.pack('<f', phase))[0], one, 0x8000, slot, sdk, col)
        if run != 0x162B0:
            fail(f'the veil run is {run:#x} bytes')
        cut1 = 0x10 + 512 * 0x70
        cut_pass = 0x10 + 0x4140          # a REF, then one 001DFA40
        # cut 1: lines
        rc, prims, envs, n, frame = fr.draw(slot, cut1)
        if rc != 0:
            fail('the lines were refused')
        f1 = fr.surface(fbp, 8, 224)
        check_lines(f1, prims, envs, n, stats)
        # cut 2: + the first frame copy (001D6930 then 001D1F20)
        cut2 = cut1 + 0x10 + 0x190 + 0x10
        rc, prims, envs, n, _ = fr.draw(slot, cut2)
        sprites = [i for i in range(n) if prims[i].prim & 7 == 6]
        if rc != 0 or len(sprites) != 2:
            fail('the copy was refused')
        t2 = fr.surface(CAPTURE_FBP, CAPTURE_FBW, 256)
        want = model_copy(f1, prims[sprites[1]], envs[sprites[1]])
        if t2 != want:
            bad = sum(t2[k] != want[k] for k in range(len(want)))
            fail(f'slot {slot}: the copy differs from the model in {bad} bytes')
        stats['copy_pixels'] += 256 * 256
        # cut 3: the whole first lens pass
        cut3 = cut1 + cut_pass
        rc, prims, envs, n, _ = fr.draw(slot, cut3)
        if rc != 0:
            fail('the first lens pass was refused')
        f3 = fr.surface(fbp, 8, 224)
        tris = [i for i in range(n) if prims[i].prim & 7 == 4]
        if len(tris) != 450:
            fail(f'{len(tris)} strip triangles, not 450')
        want, sure = model_strips(t2, prims, envs, tris, f1, False)
        for (px, py) in sure:
            o = 4 * (512 * py + px)
            if f3[o:o + 4] != want[o:o + 4]:
                fail(f'slot {slot}: lens pass 1 pixel ({px}, {py}) {tuple(f3[o:o + 4])} '
                     f'model {tuple(want[o:o + 4])}')
        stats['strip_pixels'] += len(sure)
        stats['strip_skipped'] += 512 * 224 - len(sure)
        # cut 4: the whole run
        rc, prims, envs, n, _ = fr.draw(slot, run)
        if rc != 0:
            fail('the whole veil was refused')
        t4 = fr.surface(CAPTURE_FBP, CAPTURE_FBW, 256)
        f4 = fr.surface(fbp, 8, 224)
        sprites = [i for i in range(n) if prims[i].prim & 7 == 6]
        if t4 != model_copy(f3, prims[sprites[-1]], envs[sprites[-1]]):
            fail(f'slot {slot}: the second copy differs from the model')
        tris = [i for i in range(n) if prims[i].prim & 7 == 4][450:]
        want, sure = model_strips(t4, prims, envs, tris, f3, True)
        for (px, py) in sure:
            o = 4 * (512 * py + px)
            if f4[o:o + 3] != want[o:o + 3]:
                fail(f'slot {slot}: lens pass 2 pixel ({px}, {py}) {tuple(f4[o:o + 4])} '
                     f'model {tuple(want[o:o + 4])}')
        stats['strip_pixels'] += len(sure)
        stats['copy_pixels'] += 256 * 256
    # the black veil (level 0): an all-black frame
    run = lib.shim_veil(0, 0, 0x8000, 1, sdk, col)
    rc, *_ = fr.draw(1, run)
    f = fr.surface(0, 8, 224)
    if rc != 0 or any(f[k] for k in range(len(f)) if k % 4 != 3):
        fail('the level-0 veil does not draw a black frame')
    # refusals
    lib.shim_veil(0, one, 0x8000, 0, sdk, col)

    def ztest(prims, envs, n):
        prims[0].test = 0x50000 | (prims[0].test & 0xFFFF)

    def feedback(prims, envs, n):
        for i in range(n):
            if prims[i].prim & 7 == 4:
                envs[i].frame = (envs[i].frame & ~0x1FF) | CAPTURE_FBP | (CAPTURE_FBW << 16)
                envs[i].frame &= ~(0x3F << 16)
                envs[i].frame |= CAPTURE_FBW << 16

    def psm16(prims, envs, n):
        envs[0].frame |= 2 << 24
    for hook, why in ((ztest, 'a GEQUAL Z test'), (feedback, 'a texture read of its own surface'),
                      (psm16, 'a PSMCT16 FRAME')):
        rc, *_ = fr.draw(0, 0x10 + 512 * 0x70 + 0x4140, hook)
        if rc != -1:
            fail(f'refusal: {why} was drawn')
        stats['refused'] += 1
    print(f"load veil gpu: PASS — 2 slots x 4 cuts of a visible veil: {stats['line_pixels']} line pixels on "
          f"their segments, {stats['copy_pixels']} copy pixels equal the model, {stats['strip_pixels']} lens "
          f"pixels equal the model ({stats['strip_skipped']} edge or sub-texel-boundary samples not compared); "
          f"the level-0 veil draws black; {stats['refused']} refusals draw nothing")
    return 0


if __name__ == '__main__':
    sys.exit(main())
