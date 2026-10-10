#!/usr/bin/env python3
"""The port's GS fog arithmetic against the GS conformance captures.

The decomp's conformance harness (../Extermination/tools/gs_conformance*.py,
decomp docs/GS_CONFORMANCE.md 5.5) drew designed fogged sprites in PCSX2's
software GS and saved the GS local memory (build/b16/gscap/pixel, ignored,
generated locally; nothing disc-derived). Two of its tests isolate the fog
blend with a constant F:
  - fog_cols: 256 flat one-pixel-wide sprites, 64 F values x 4 colour bands,
    FOGCOL 0x906030 (4,096 pixels);
  - fog_tex: 4 textured sprites (TFX MODULATE, TCC 1, nearest, texture A of
    the batch's uploads) with F 21 / 85 / 149 / 213, FOGCOL 0x11AA55 (4,096
    pixels): fog follows the texture function.
The captured colour buffers are the expected output. This test builds each
pixel's input (the colour before fog, F, FOGCOL) from the batch's own
recorded primitives and texture upload, and requires EVERY pixel equal for:
  A. the CPU mirror em_fog_gs_blend (src/gfx/metal/em_fog_gs.h), the one C
     copy of the rule, which em_shadow_gs.h's receiver pixel uses;
  B. the Metal shader (EM_FOG_GS_MSL, the one shader copy spliced into the
     object-unit, chain-page and shadow-receiver shaders), run through the
     chain page's em_gfx_gs_prims in a headless window: every distinct
     input is drawn as one fogged sprite of a grid over a constant 128
     texel (MODULATE then passes the vertex colour through unchanged), with
     the conformance tests' own ALPHA_1 0x80000000A8 (Cv = Cs), and the
     frame is read back.
Defect check: the form (F * C + (255 - F) * FOGCOL) >> 8 the port used
before 2026-09-28 must match exactly 1,040 (fog_cols) and 640 (fog_tex) of
the 4,096 pixels (GS_CONFORMANCE.md 5.5), so the comparison discriminates.
With that form put back into EM_FOG_GS_MSL (2026-09-28), part B fails on
3,056 of fog_cols' 4,096 pixels.

  C. a Gouraud F: the eight p3_start fog_* tests (decomp build/b16/gscap3,
     GS_CONFORMANCE.md section 8: flat colour, F varying across one
     triangle, 342,831 covered channel values). The GS fogs with the 8.7
     weight there (docs/GS_EXACT.md 3.2 / 5.2). Per covered pixel the exact
     plane of F at the integer sample point goes through em_fog_gs_weight7
     (floor(128 * F + 0.01), the shaders' weight) and em_fog_gs_blend7; the
     values that match are counted and must equal FOG_TRI_EXACT, and the
     8-bit weight the shaders used before the fb2 step (floor(F + 0.001), the >> 8
     rule) must match exactly FOG_TRI_EXACT_8BIT, so the part discriminates.
     The other 482 values follow the GS's DDA stepping (GS_EXACT.md 3.2),
     which the plane value does not reproduce (469 with no epsilon). The
     shaders evaluate the same weight at Metal's own sample points (the fb2
     pixel harness measures that, tools/test_fb2_pixels.py).

EM_TEST_FULL=1 also checks the repeat capture (build/b16/gscap_repeat).
Skipped (reported) without the captures; part B is skipped only on a
machine without a Metal device (with one, a harness that does not build or
open FAILS). About 2 s once the fixture library is built.
"""
from __future__ import annotations

import ctypes as C
import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from reference_mode import FULL, MODE  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
B16 = Path(os.environ.get('GSCAP_ROOT', DECOMP / 'build/b16'))
OUT = ROOT / 'build/gs_fog'
TESTS = ('fog_cols', 'fog_tex')
OLD_FORM_MATCHES = {'fog_cols': 1040, 'fog_tex': 640}   # GS_CONFORMANCE.md 5.5
B16_TRI = B16 / 'gscap3/p3_start'
FOG_TRI_VALUES = 342831          # covered channel values of the eight fog_* tests
FOG_TRI_EXACT = 342349           # em_fog_gs_weight7 + em_fog_gs_blend7 at the exact plane
FOG_TRI_EXACT_8BIT = 186635      # floor(F + 0.001) with the >> 8 rule (the shaders before the fb2 step)
SPRITE_FOG_TEX = 0x76            # sprite | TME | FGE | ABE
ALPHA_CS = (0x80 << 32) | 0xA8   # the conformance tests' ALPHA_1: (Cs - 0) * FIX 0x80 >> 7 + 0
# A 1x1 PSMT8 texture with a CT32 CLUT, TCC 1, MODULATE: what em_gfx_gs_texture
# registers for the chain page (the page's refusal rules need that form).
TEX0_CONST = (0x13 << 20) | (1 << 34)


def fogcol_of(test):
    vals = [int(v, 16) for it in test['items'] if it.get('role') != 'reset'
            for r, v in it.get('ad', []) if r == 'FOGCOL']
    assert len(set(vals)) == 1, (test['name'], 'one FOGCOL per test', vals)
    return tuple((vals[0] >> s) & 255 for s in (0, 8, 16))


def texture_of(batch, inputs, tex0):
    """The batch's CT32 upload at TEX0's TBP, as (h, w, 4) bytes, checked
    against the upload's recorded sha256."""
    tbp = tex0 & 0x3FFF
    ups = [it['image'] for t in batch['tests'] if t['name'] == 'upload'
           for it in t['items'] if 'image' in it and it['image']['dbp'] == tbp]
    sha = [it['data_sha256'] for t in batch['tests'] if t['name'] == 'upload'
           for it in t['items'] if 'image' in it and it['image']['dbp'] == tbp]
    assert len(ups) == 1 and ups[0]['psm'] == 'CT32', ('the texture upload at TBP', tbp, ups)
    arr = inputs['tex_' + ups[0]['label']]
    assert hashlib.sha256(arr.astype('<u4').tobytes()).hexdigest() == sha[0], 'texture bytes != the upload'
    return arr.astype('<u4').view(np.uint8).reshape(arr.shape + (4,)).astype(np.int64)


def inputs_of(batch, inputs, test):
    """Per pixel: the colour entering the fog (h, w, 3), F (h, w), and the
    covered mask, from the test's recorded primitives."""
    h, w = test['h'], test['w']
    cin = np.zeros((h, w, 3), np.int64)
    f = np.zeros((h, w), np.int64)
    cov = np.zeros((h, w), np.int64)
    meta = test['meta']
    tex = None
    if 'tex' in meta:
        tex0 = int(meta['tex']['tex0'], 16)
        assert (tex0 >> 35) & 3 == 0 and (tex0 >> 34) & 1 == 1, 'MODULATE, TCC 1'
        assert meta['tex']['mmag'] == 0 and meta['tex']['mmin'] == 0, 'nearest'
        tex = texture_of(batch, inputs, tex0)
    for p in meta['prims']:
        assert p['kind'] == 'sprite' and p['flags'].get('fge') == 1, p
        a, b = p['v']
        x0, x1, y0, y1 = a['x'], b['x'], a['y'], b['y']
        assert x0 < x1 and y0 < y1 and x0 == int(x0) and y0 == int(y0) and x1 == int(x1) and y1 == int(y1)
        rgb = np.array(b['rgba'][:3], np.int64)   # the sprite's colour and F: its second vertex
        for y in range(int(y0), int(y1)):
            for x in range(int(x0), int(x1)):
                if tex is not None:
                    # nearest at the integer sample; UV affine between the corners
                    u = a['uv'][0] + (x - x0) * (b['uv'][0] - a['uv'][0]) // (x1 - x0)
                    v = a['uv'][1] + (y - y0) * (b['uv'][1] - a['uv'][1]) // (y1 - y0)
                    cin[y, x] = np.minimum((tex[v, u, :3] * rgb) >> 7, 255)
                else:
                    cin[y, x] = rgb
                f[y, x] = b['f']
                cov[y, x] += 1
    assert (cov == 1).all(), (test['name'], 'every pixel drawn by exactly one sprite')
    return cin, f


def expected_of(batch_dir, name):
    col = np.load(batch_dir / f'{name}.npz')['color']
    return col.astype('<u4').view(np.uint8).reshape(col.shape + (4,))[..., :3].astype(np.int64)


def cpu_mirror():
    OUT.mkdir(parents=True, exist_ok=True)
    src, lib = OUT / 'fog_mirror.c', OUT / 'fog_mirror.dylib'
    src.write_text('#include "gfx/metal/em_fog_gs.h"\n'
                   'void fog_blend_n(const uint32_t *c, const uint32_t *f, const uint32_t *fc,\n'
                   '                 uint32_t *out, uint32_t n)\n'
                   '{ for (uint32_t i = 0; i < n; i++) out[i] = em_fog_gs_blend(c[i], f[i], fc[i]); }\n'
                   'void fog_blend7_n(const uint32_t *c, const float *f, const uint32_t *fc,\n'
                   '                  uint32_t *out, uint32_t n)\n'
                   '{ for (uint32_t i = 0; i < n; i++)\n'
                   '      out[i] = em_fog_gs_blend7(c[i], em_fog_gs_weight7(f[i]), fc[i]); }\n')
    head = ROOT / 'src/gfx/metal/em_fog_gs.h'
    if not lib.exists() or max(head.stat().st_mtime, src.stat().st_mtime) > lib.stat().st_mtime:
        subprocess.run(['clang', '-O1', '-std=c11', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                        '-I' + str(ROOT / 'src'), str(src), '-o', str(lib)], check=True)
    dll = C.CDLL(str(lib))
    P = C.POINTER(C.c_uint32)
    dll.fog_blend_n.argtypes = [P, P, P, P, C.c_uint32]
    dll.fog_blend7_n.argtypes = [P, C.POINTER(C.c_float), P, P, C.c_uint32]

    def run(c, f, fc):
        c, f, fc = (np.ascontiguousarray(q.ravel(), np.uint32) for q in (c, f, fc))
        out = np.zeros_like(c)
        dll.fog_blend_n(*(q.ctypes.data_as(P) for q in (c, f, fc, out)), len(c))
        return out

    def run7(c, f, fc):
        c, fc = (np.ascontiguousarray(q.ravel(), np.uint32) for q in (c, fc))
        f = np.ascontiguousarray(f.ravel(), np.float32)
        out = np.zeros_like(c)
        dll.fog_blend7_n(c.ctypes.data_as(P), f.ctypes.data_as(C.POINTER(C.c_float)), fc.ctypes.data_as(P),
                         out.ctypes.data_as(P), len(c))
        return out
    run.gouraud = run7
    return run


def gouraud_fog(mirror):
    """Part C: (values, 8.7 matches, 8-bit matches) over the p3_start fog_*
    tests, or None without the captures."""
    if not (B16_TRI / 'batch.json').exists():
        return None
    batch = json.loads((B16_TRI / 'batch.json').read_text())
    n = ok7 = ok8 = 0
    for test in batch['tests']:
        if not test['name'].startswith('fog_'):
            continue
        (prim,) = test['meta']['prims']
        assert prim['kind'] == 'tri' and prim['flags'] == {'iip': 1, 'fge': 1}, (test['name'], prim)
        v = prim['v']
        assert all(p['rgba'] == v[0]['rgba'] for p in v), (test['name'], 'a flat colour')
        fc = np.array(fogcol_of(test), np.int64)
        col = np.load(B16_TRI / f"{test['name']}.npz")['color'].astype('<u4')
        ys, xs = np.nonzero(col >> 24)                  # covered: the clear is 0, a drawn pixel has alpha 0x80
        (x0, y0), (x1, y1), (x2, y2) = [(p['x'], p['y']) for p in v]
        det = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
        l1 = ((xs - x0) * (y2 - y0) - (x2 - x0) * (ys - y0)) / det
        l2 = ((x1 - x0) * (ys - y0) - (xs - x0) * (y1 - y0)) / det
        e = v[0]['f'] + (v[1]['f'] - v[0]['f']) * l1 + (v[2]['f'] - v[0]['f']) * l2
        want = col[ys, xs].view(np.uint8).reshape(-1, 4)[:, :3].astype(np.int64)
        c = np.broadcast_to(np.array(v[0]['rgba'][:3], np.int64), want.shape)
        fcs = np.broadcast_to(fc, want.shape)
        got7 = mirror.gouraud(c, np.broadcast_to(e[:, None], want.shape), fcs).reshape(want.shape)
        f8 = np.clip(np.floor(e + 0.001), 0, 255).astype(np.int64)[:, None]
        got8 = fcs + (((c - fcs) * f8) >> 8)
        n += want.size
        ok7 += int((got7 == want).sum())
        ok8 += int((got8 == want).sum())
    return n, ok7, ok8


def read_bmp(path):
    d = path.read_bytes()
    off, w, h, bpp = (struct.unpack_from('<I', d, 10)[0], struct.unpack_from('<I', d, 18)[0],
                      struct.unpack_from('<i', d, 22)[0], d[28])
    stride = (w * bpp // 8 + 3) & ~3

    def px(x, y):
        yy = abs(h) - 1 - y if h > 0 else y
        o = off + yy * stride + x * bpp // 8
        return (d[o + 2], d[o + 1], d[o])
    return px, w, abs(h)


def gpu_frames(cases):
    """cases: [(fogcol, [(r, g, b, f), ...])]. Draws each input as one
    fogged sprite of a 16 x 16 grid over the 512 x 224 field through
    em_gfx_gs_prims and returns the frame's colour per input, or None
    without a Metal device."""
    import test_shadow_original_reference as SO
    from test_chain_page_reference import GsPrim, GsVertex
    metal = SO.open_metal(OUT, 'gs fog: part B', size=(512, 448))
    if metal is None:
        return None
    lib, gfx = metal.lib, metal.gfx
    lib.em_gfx_gs_prims.argtypes = [C.c_void_p, C.POINTER(GsPrim), C.c_uint32]
    lib.em_gfx_gs_texture.argtypes = [C.c_void_p, C.c_uint64, C.c_char_p, C.c_uint32, C.c_uint32]
    assert lib.em_gfx_gs_texture(gfx, TEX0_CONST, bytes([128, 128, 128, 128]), 1, 1) == 0
    f32 = lambda x: struct.unpack('<I', struct.pack('<f', x))[0]
    tmp = Path(tempfile.mkdtemp(prefix='gs_fog_', dir=OUT))
    # The field's place in the captured frame (the presentation may
    # letterbox it): the extent of a cleared frame's clear colour.
    lib.em_gfx_begin_frame(gfx, 0.1, 0.2, 0.3, 1.0)
    path = tmp / 'frame.bmp'
    lib.em_gfx_request_capture(gfx, str(path).encode())
    lib.em_gfx_end_frame(gfx)
    px, w, h = read_bmp(path)
    clear = px(w // 2, h // 2)
    xs = [x for x in range(w) if px(x, h // 2) == clear]
    ys = [y for y in range(h) if px(w // 2, y) == clear]
    vx, vw, vy, vh = xs[0], xs[-1] + 1 - xs[0], ys[0], ys[-1] + 1 - ys[0]
    assert len(xs) == vw and len(ys) == vh, 'one contiguous field'
    results = []
    for fogcol, ins in cases:
        assert len(ins) <= 256, 'one 16 x 16 grid per frame'
        prims = []
        for i, (r, g, b, f) in enumerate(ins):
            gx, gy = 1792 + 32 * (i % 16), 1936 + 14 * (i // 16)
            corner = [GsVertex(16 * x, 16 * y, 0x100000, f, 1, (C.c_uint8 * 4)(r, g, b, 0x80),
                               f32(1.0), f32(0.5), f32(0.5), 0, 0) for x, y in ((gx, gy), (gx + 32, gy + 14))]
            p = GsPrim()
            p.prim, p.set, p.tex0, p.clamp, p.tex1 = SPRITE_FOG_TEX, 0x3F, TEX0_CONST, 0, 0x60
            p.alpha, p.test, p.colclamp, p.count = ALPHA_CS, 0x53001, 1, 2
            p.v[0], p.v[1] = corner
            prims.append(p)
        lib.em_gfx_begin_frame(gfx, 0.1, 0.2, 0.3, 1.0)
        lib.em_gfx_fog(gfx, -209.0, 304.0, (C.c_float * 3)(*[float(c) for c in fogcol]))
        rc = lib.em_gfx_gs_prims(gfx, (GsPrim * len(prims))(*prims), len(prims))
        path = tmp / 'frame.bmp'
        lib.em_gfx_request_capture(gfx, str(path).encode())
        lib.em_gfx_end_frame(gfx)
        assert rc == 0, 'the fogged sprites were refused'
        px, w, h = read_bmp(path)
        results.append([px(vx + int((32 * (i % 16) + 16) * vw / 512), vy + int((14 * (i // 16) + 7) * vh / 224))
                        for i in range(len(ins))])
    for q in tmp.iterdir():
        q.unlink()
    tmp.rmdir()
    return results


def main():
    sets = [B16 / 'gscap/pixel'] + ([B16 / 'gscap_repeat/pixel'] if FULL else [])
    if not (sets[0] / 'batch.json').exists():
        print(f'gs fog: SKIPPED (no conformance captures at {sets[0]}; decomp docs/GS_CONFORMANCE.md)')
        return 0
    mirror = cpu_mirror()
    cases, where = [], []
    counts = {'pixels': 0, 'mirror_exact': 0, 'gpu_exact': 0}
    for bdir in sets:
        batch = json.loads((bdir / 'batch.json').read_text())
        inputs = np.load(bdir / 'inputs.npz')
        for name in TESTS:
            test = next(t for t in batch['tests'] if t['name'] == name)
            fc = np.array(fogcol_of(test), np.int64)
            cin, f = inputs_of(batch, inputs, test)
            want = expected_of(bdir, name)
            fcs = np.broadcast_to(fc, cin.shape)
            fs = np.broadcast_to(f[..., None], cin.shape)
            got = mirror(cin, fs, fcs).reshape(cin.shape).astype(np.int64)
            bad = int((got != want).any(-1).sum())
            assert bad == 0, (bdir.parent.name, name, 'em_fog_gs_blend differs from the capture', bad)
            old = (fs * cin + (255 - fs) * fcs) >> 8
            old_ok = int((old == want).all(-1).sum())
            assert old_ok == OLD_FORM_MATCHES[name], (name, 'the (255 - F) form should match exactly',
                                                      OLD_FORM_MATCHES[name], old_ok)
            counts['pixels'] += cin.shape[0] * cin.shape[1]
            counts['mirror_exact'] += cin.shape[0] * cin.shape[1]
            key = [tuple(int(v) for v in cin[y, x]) + (int(f[y, x]),)
                   for y in range(cin.shape[0]) for x in range(cin.shape[1])]
            uniq = sorted(set(key))
            cases.append((tuple(int(v) for v in fc), uniq))
            where.append((bdir.parent.name, name, key, want.reshape(-1, 3)))
    tri = gouraud_fog(mirror)
    if tri is not None:
        assert tri == (FOG_TRI_VALUES, FOG_TRI_EXACT, FOG_TRI_EXACT_8BIT), ('part C: the Gouraud-F fog tests',
                                                                          tri)
    frames = gpu_frames(cases)
    if frames is not None:
        for (fc, uniq), frame, (bset, name, key, want) in zip(cases, frames, where):
            out = dict(zip(uniq, frame))
            got = np.array([out[k] for k in key], np.int64)
            bad = int((got != want).any(-1).sum())
            assert bad == 0, (bset, name, 'the Metal fog differs from the capture', bad,
                              [(k, out[k], tuple(wv)) for k, wv in zip(key, want) if out[k] != tuple(wv)][:4])
            counts['gpu_exact'] += len(key)
    gpu = (f"{counts['gpu_exact']} of {counts['pixels']} through the Metal shader"
           if frames is not None else 'Metal part skipped')
    print(f"gs fog: PASS (mode {MODE}: {len(where)} captured tests; {counts['mirror_exact']} of "
          f"{counts['pixels']} pixels exact through em_fog_gs_blend, {gpu}; the old (255 - F) form "
          f"matches only {OLD_FORM_MATCHES['fog_cols']} / {OLD_FORM_MATCHES['fog_tex']} of 4,096; "
          + (f"Gouraud F (p3_start fog_*): {tri[1]:,} of {tri[0]:,} channel values through em_fog_gs_weight7 + "
             f"em_fog_gs_blend7, the 8-bit weight {tri[2]:,})" if tri else "Gouraud-F part skipped: no gscap3)"))
    return 0


if __name__ == '__main__':
    sys.exit(main())
