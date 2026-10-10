#!/usr/bin/env python3
"""The chain page's GS pixel path (em_gfx_gs_prims, the Metal backend)
against a model of the documented GS pixel path (docs/CHAIN_PAGE.md
section 5).

em_gfx_gs_prims draws the primitives em_chain_page hands it. Over a flat
frame (the clear colour Cd), each case draws one primitive in a headless
Metal window and reads the frame back:
- fans (the 0015BF90 decal's state: TEX0 0x2004290511322469, blend preset
  mode 1 = TEST 0x53001, ALPHA 0x44, TEX1 0x60, CLAMP 0, COLCLAMP 1, with
  fog): the five colour / alpha / fog / sample-point cases the dedicated
  decal entry was checked with (tools/test_shadow_decal_reference.py,
  retired there with that entry);
- sprites (the sprite program's and 001CD520's state: ALPHA 0x8000000068 =
  Cs * 0x80 >> 7 + Cd, and 0x44; with and without fog), the GS sprite's
  colour, Z and F from its second vertex;
- the glint's line (untextured, Gouraud, fog, ALPHA 0x44): the one pixel
  the line lights in the frame's centre column;
- TCC 0 MODULATE fans (AREA01's floor fields and ripple surface, 001E9E60
  / 001E7D20: an RGB texture, Af = Av by the GS texture function), the
  decal's texels under its TEX0 with TCC cleared;
and the pixel at the frame centre must equal gs_pixel(): the texture
sampled bilinearly with the GS 4-bit weights at U - 0.5 (REPEAT), TFX
MODULATE with TCC 1, fog FOGCOL + ((C - FOGCOL) * F >> 8) (the rule measured
in PCSX2's software GS, docs/GS_EXACT.md 5.2; this model used the
non-original (C * F + FOGCOL * (255 - F)) >> 8 until 2026-09-28), then
((A - B) * C >> 7) + D with COLCLAMP. The textures are decoded from the
route captures' GS memory (tools/export_object_textures.py decode()).
Refusals: a HIGHLIGHT TEX0 (with TCC 1 or 0), a fogged primitive without the frame's fog, an
unregistered TEX0, a TEST_1 other than 0x53001, FST set, a state the page
did not set: each returns -1 and draws nothing.
Skipped (reported) only on a machine without a Metal device; on one with
a device a harness that does not build or open FAILS. About 2 s.
"""
from __future__ import annotations

import ctypes as C
import math
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_shadow_original_reference as SO  # noqa: E402
from test_chain_page_reference import GsPrim, GsVertex  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
ROUTE = DECOMP / 'build/s87/route'
OUT = ROOT / 'build/chain_page_gpu'
BEAT = '04_elevator_ride'
DECAL = 0x2004290511322469
SPRITE = 0x20041805113222AE      # the sprite program's handler texture (D_002565E0's row)
MARKER = 0x20045B0599421EF0      # 001F4D40's glow-marker texture
DECAL_RGB = DECAL & ~(1 << 34)   # the decal's texels under TCC 0
FOGCOL = (48, 48, 48)
SET_ALL = 0x3F


def f32(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def texels_of(tex0):
    sys.path.insert(0, str(DECOMP / 'tools'))
    import export_object_textures as eot
    import gs_vram
    _, lm = gs_vram.read_localmem(ROUTE / BEAT / 'gs.bin')
    f = eot.tex0_fields(tex0)
    return eot.decode(lm, tex0), 1 << f['tw'], 1 << f['th']


def gs_pixel(tex, u, v, rgba, f, cd, alpha, tcc=1):
    """The GS pixel of one sample of the page's state (MODULATE; TCC 0:
    Af = Av)."""
    if tex is not None:
        texels, w, h = tex
        uu, vv = math.floor(u * w * 16) - 8, math.floor(v * h * 16) - 8
        fu, fv = uu & 15, vv & 15
        x0, x1 = (uu >> 4) & (w - 1), ((uu >> 4) + 1) & (w - 1)
        y0, y1 = (vv >> 4) & (h - 1), ((vv >> 4) + 1) & (h - 1)
        t = lambda x, y, c: texels[4 * (w * y + x) + c]
        tt = [(t(x0, y0, c) * (16 - fu) * (16 - fv) + t(x1, y0, c) * fu * (16 - fv) +
               t(x0, y1, c) * (16 - fu) * fv + t(x1, y1, c) * fu * fv) >> 8 for c in range(4)]
        cs = [min((tt[c] * rgba[c]) >> 7, 255) for c in range(3)]
        a = min((tt[3] * rgba[3]) >> 7, 255) if tcc else rgba[3]
    else:
        cs, a = list(rgba[:3]), rgba[3]
    if f is not None:
        cs = [FOGCOL[c] + (((cs[c] - FOGCOL[c]) * f) >> 8) for c in range(3)]
    sel, fix = alpha & 0xFF, alpha >> 32 & 0xFF
    pick = lambda s, c: cs[c] if s == 0 else cd[c] if s == 1 else 0
    cc = a if (sel >> 4) & 3 == 0 else fix
    return tuple(max(0, min(255, (((pick(sel & 3, c) - pick(sel >> 2 & 3, c)) * cc) >> 7) +
                             pick(sel >> 6 & 3, c))) for c in range(3))


def vertex(x, y, s, t, q, rgba, f, z=0x100000):
    return GsVertex(16 * x, 16 * y, z, f if f is not None else 0, 1, (C.c_uint8 * 4)(*rgba), f32(q), f32(s),
                    f32(t), 0, 0)


def prim(p, tex0, alpha, verts, test=0x53001, set_bits=SET_ALL):
    out = GsPrim()
    out.prim, out.set, out.tex0, out.clamp, out.tex1 = p, set_bits, tex0, 0, 0x60
    out.alpha, out.test, out.colclamp, out.count = alpha, test, 1, len(verts)
    for k, v in enumerate(verts):
        out.v[k] = v
    return out


def read_bmp(path):
    d = path.read_bytes()
    u32 = lambda a: struct.unpack_from('<I', d, a)[0]
    off, w, h, bpp = u32(10), u32(18), struct.unpack_from('<i', d, 22)[0], d[28]
    stride = (w * bpp // 8 + 3) & ~3

    def px(x, y):
        yy = abs(h) - 1 - y if h > 0 else y
        o = off + yy * stride + x * bpp // 8
        return (d[o + 2], d[o + 1], d[o])
    return px, w, abs(h)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    metal = SO.open_metal(OUT, 'chain page gpu')
    if metal is None:
        return 0
    lib, gfx = metal.lib, metal.gfx
    lib.em_gfx_gs_prims.argtypes = [C.c_void_p, C.POINTER(GsPrim), C.c_uint32]
    lib.em_gfx_gs_texture.argtypes = [C.c_void_p, C.c_uint64, C.c_char_p, C.c_uint32, C.c_uint32]
    texes = {}
    for t in (DECAL, SPRITE, MARKER):
        texes[t] = texels_of(t)
        assert lib.em_gfx_gs_texture(gfx, t, texes[t][0], texes[t][1], texes[t][2]) == 0
    texes[DECAL_RGB] = texes[DECAL]
    assert lib.em_gfx_gs_texture(gfx, DECAL_RGB, *texes[DECAL]) == 0
    fog_rgb = (C.c_float * 3)(*[float(c) for c in FOGCOL])
    tmp = Path(tempfile.mkdtemp(prefix='chain_page_gpu_', dir=OUT))
    counts = {'fan': 0, 'rgb': 0, 'sprite': 0, 'line': 0, 'refused': 0}

    def draw(cd, prims, fog=True):
        lib.em_gfx_begin_frame(gfx, cd[0] / 255.0, cd[1] / 255.0, cd[2] / 255.0, 1.0)
        if fog:
            lib.em_gfx_fog(gfx, -209.0, 304.0, fog_rgb)
        else:
            lib.em_gfx_fog_off(gfx)
        arr = (GsPrim * len(prims))(*prims)
        rc = lib.em_gfx_gs_prims(gfx, arr, len(prims))
        path = tmp / 'frame.bmp'
        lib.em_gfx_request_capture(gfx, str(path).encode())
        lib.em_gfx_end_frame(gfx)
        return rc, read_bmp(path)

    corners = ((1700, 1800), (2400, 1800), (2400, 2300), (1700, 2300))
    # fans: the decal cases (TFX MODULATE, TCC 1, fog, ALPHA 0x44)
    for cd, rgba, f, (u, v) in (((91, 106, 106), (5, 5, 5, 0xA2), 124, (0.5, 0.5)),
                                ((91, 106, 106), (5, 5, 5, 0xA2), 255, (0.53, 0.47)),
                                ((200, 40, 90), (8, 8, 8, 0xFF), 60, (0.31, 0.62)),
                                ((30, 30, 30), (128, 128, 128, 0x40), 200, (0.02, 0.97)),
                                ((91, 106, 106), (5, 5, 5, 0x10), 0, (0.5, 0.5))):
        vs = [vertex(x, y, u, v, 1.0, rgba, f) for x, y in corners]
        tris = [prim(0x7D, DECAL, 0x44, [vs[0], vs[1], vs[2]]), prim(0x7D, DECAL, 0x44, [vs[0], vs[2], vs[3]])]
        rc, (px, w, h) = draw(cd, tris)
        assert rc == 0, 'the fan was refused'
        want = gs_pixel(texes[DECAL], u, v, rgba, f, cd, 0x44)
        assert px(w // 2, h // 2) == want, ('fan', cd, rgba, f, (u, v), px(w // 2, h // 2), want)
        counts['fan'] += 1
    # TCC 0 MODULATE strips (the floor fields' PRIM 0x7C: Gouraud, textured,
    # fogged, ALPHA 0x44): Af is the vertex alpha, not At * Av >> 7
    for cd, rgba, f, (u, v) in (((91, 106, 106), (100, 90, 80, 0x40), 200, (0.5, 0.5)),
                                ((200, 40, 90), (128, 128, 128, 0x80), 255, (0.31, 0.62)),
                                ((30, 30, 30), (60, 70, 80, 0x10), 90, (0.02, 0.97))):
        vs = [vertex(x, y, u, v, 1.0, rgba, f) for x, y in corners]
        tris = [prim(0x7C, DECAL_RGB, 0x44, [vs[0], vs[1], vs[2]]),
                prim(0x7C, DECAL_RGB, 0x44, [vs[0], vs[2], vs[3]])]
        rc, (px, w, h) = draw(cd, tris)
        assert rc == 0, 'the TCC 0 strip was refused'
        want = gs_pixel(texes[DECAL], u, v, rgba, f, cd, 0x44, tcc=0)
        assert px(w // 2, h // 2) == want, ('tcc0', cd, rgba, f, (u, v), px(w // 2, h // 2), want)
        assert want != gs_pixel(texes[DECAL], u, v, rgba, f, cd, 0x44), 'the case does not tell TCC apart'
        counts['rgb'] += 1
    # sprites: additive (0x8000000068) and 0x44, with and without fog (FGE)
    for p, tex0, alpha, cd, rgba, f, (u, v) in (
            (0x56, SPRITE, (0x80 << 32) | 0x68, (20, 30, 40), (100, 90, 80, 0x80), None, (0.5, 0.5)),
            (0x56, SPRITE, (0x80 << 32) | 0x68, (200, 190, 180), (128, 128, 128, 0x80), None, (0.4, 0.6)),
            (0x76, MARKER, (0x80 << 32) | 0x68, (20, 30, 40), (180, 60, 40, 0x80), 150, (0.5, 0.5)),
            (0x76, MARKER, 0x44, (60, 60, 70), (200, 80, 60, 0x60), 255, (0.25, 0.75))):
        a = vertex(1700, 1800, u, v, 1.0, (0, 0, 0, 0), 0, z=0x10)
        b = vertex(2400, 2300, u, v, 1.0, rgba, f if f is not None else 0)
        rc, (px, w, h) = draw(cd, [prim(p, tex0, alpha, [a, b])], fog=f is not None)
        assert rc == 0, 'the sprite was refused'
        want = gs_pixel(texes[tex0], u, v, rgba, f, cd, alpha)
        assert px(w // 2, h // 2) == want, ('sprite', hex(p), cd, rgba, f, px(w // 2, h // 2), want)
        counts['sprite'] += 1
    # the glint's line: untextured, Gouraud, fogged, ALPHA 0x44
    for cd, rgba, f in (((20, 30, 40), (200, 180, 150, 0x60), 200), ((90, 90, 90), (255, 255, 255, 0x80), 90)):
        y = 32783
        a = vertex(1700, 0, 0, 0, 1.0, rgba, f)
        b = vertex(2400, 0, 0, 0, 1.0, rgba, f)
        a.y = b.y = y
        rc, (px, w, h) = draw(cd, [prim(0x6A, 0, 0x44, [a, b], set_bits=SET_ALL & ~0x07)])
        assert rc == 0, 'the line was refused'
        lit = [px(w // 2, r) for r in range(h) if px(w // 2, r) != cd]
        want = gs_pixel(None, 0, 0, rgba, f, cd, 0x44)
        assert lit == [want], ('line', cd, rgba, f, lit, want)
        counts['line'] += 1
    # refusals: nothing drawn, -1
    base = [vertex(x, y, 0.5, 0.5, 1.0, (5, 5, 5, 0xA2), 124) for x, y in corners]
    refusals = (
        ([prim(0x7D, DECAL | (2 << 35), 0x44, base[:3])], True, 'a HIGHLIGHT TEX0'),
        ([prim(0x7D, DECAL_RGB | (2 << 35), 0x44, base[:3])], True, 'a TCC 0 HIGHLIGHT TEX0'),
        ([prim(0x7D, DECAL, 0x44, base[:3])], False, 'fog without the frame fog'),
        ([prim(0x7D, 0x2004290511333333, 0x44, base[:3])], True, 'an unregistered TEX0'),
        ([prim(0x7D, DECAL, 0x44, base[:3], test=0x5000D)], True, 'TEST_1 0x5000D'),
        ([prim(0x17D, DECAL, 0x44, base[:3])], True, 'FST'),
        ([prim(0x7D, DECAL, 0x44, base[:3], set_bits=SET_ALL & ~0x08)], True, 'ALPHA_1 not set'),
    )
    for prims, fog, why in refusals:
        rc, (px, w, h) = draw((10, 20, 30), prims, fog=fog)
        assert rc == -1, ('refusal', why)
        assert px(w // 2, h // 2) == (10, 20, 30), ('refusal drew', why)
        counts['refused'] += 1
    for q in tmp.iterdir():
        q.unlink()
    tmp.rmdir()
    print(f"chain page gpu: PASS ({counts['fan']} fan, {counts['rgb']} TCC 0 strip, {counts['sprite']} sprite "
          f"and {counts['line']} line "
          f"pixels equal the GS pixel model; {counts['refused']} refusals draw nothing)")
    return 0


if __name__ == '__main__':
    sys.exit(main())
