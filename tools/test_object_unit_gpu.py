#!/usr/bin/env python3
"""The Metal pixel path of em_gfx_object_unit (em_gfx_metal.m) against a
model of the GS pixel path it implements (docs/OWNER_DRAW.md section 7.2),
over captured owner units.

For route beats 03 (crates, drums, truck, panel, elevator, pickup) and 07
(the whole truck): every intact bank-model unit of the capture's current
display list (before the context's channel-0 cursor) is drawn by the
headless GPU fixture tests/object_unit_gpu_test.c (em_object_unit_parse over
the captured RAM, em_gfx_object_unit, the frame's fog from the capture), and
the frame is captured. Independently, the triangles of the same units
(em_object_unit_run: every one equal to the original microcode's,
tools/test_object_unit_reference.py) are rasterized here at the capture's
pixel centres (the em_background_gs_ndc mapping) with the documented pixel
path: screen-linear RGBA, F and S, T, Q with the per-pixel divide, GS
bilinear at U - 0.5 with 4-bit weights and REPEAT, TFX HIGHLIGHT with TCC 1,
the alpha test, the fog blend with FOGCOL, and the nearest depth wins (ties:
the later triangle). Pixels within 1.5 output pixels of their triangle's
edges and pixels where two triangles' depths are within 1e-7 are not
compared (rasterization edges and ties are the GPU's, section 12).

Asserted: at least 99 % of the compared pixels agree within 2 in every
channel and at least 90 % exactly. The default run draws beat 03;
EM_TEST_FULL=1 both. Mac only, headless (EM_HEADLESS=1); the capture BMP
stays under build/object_unit_gpu/.

No original data is written by this file; the report holds counts only.
"""
import json
import math
import os
import struct
import subprocess
import sys
from pathlib import Path

import numpy as np

import test_object_unit_reference as T
import export_world_models as ewm
from reference_mode import FULL, banner

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
OUT = ROOT / 'build/object_unit_gpu'
BEATS = ('03_panel_power', '07_truck_preview')   # the default run: 03 only
CLD = ~(7 << 61) & (2 ** 64 - 1)


def u32(b, a): return struct.unpack_from('<I', b, a)[0]
def f32(w): return struct.unpack('<f', struct.pack('<I', w))[0]


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    binary = OUT / 'fixture'
    subprocess.run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Isrc',
                    'tests/object_unit_gpu_test.c', 'src/gfx/metal/em_gfx_metal.m', 'src/platform/mac/em_platform_mac.m',
                    'src/game/em_lighting.c', 'src/game/em_packet_chain_original.c',
                    'src/game/em_status_ui_leftovers.c', 'src/game/em_object_unit.c',
                    '-framework', 'Cocoa', '-framework', 'Metal', '-framework', 'QuartzCore', '-lm',
                    '-o', str(binary)], cwd=ROOT, check=True)
    return binary


def units(ram, bank):
    """(start, size) of every bank-model object unit before the channel-0
    cursor, in list order."""
    ctx = u32(ram, 0x275670)
    cursor = u32(ram, ctx + 0x10)
    out, p = [], T.ARENA[0]
    while p < cursor:
        if u32(ram, p) & 0x7000FFFF == 0x10000005 and \
                struct.unpack_from('<4I', ram, p + 16) == (0, 0x11000000, 0x01000101, 0x6C0403F5):
            o, calls, model = p, [], None
            for _ in range(40):
                w0, addr = struct.unpack_from('<2I', ram, o)
                tid, qwc = (w0 >> 28) & 7, w0 & 0xFFFF
                if tid == 5: calls.append(addr)
                o += 16 + (16 * qwc if tid == 1 else 0)
                if tid == 3 and qwc and qwc % 0x82 == 0 and addr not in (0x815360, 0x816440):
                    model = addr
                    nxt = u32(ram, o)
                    if (nxt >> 28) & 7 == 3 and nxt & 0xFFFF == 8 and 0x816440 <= u32(ram, o + 4) < 0x817240:
                        continue
                    break
            if model and model - 0x40 in bank and T.FACE_KERNEL not in calls and o <= cursor:
                out.append((p, o - p))
                p = o
                continue
        p += 16
    return out


def read_bmp(path):
    b = path.read_bytes()
    off = u32(b, 10)
    w, h = struct.unpack_from('<ii', b, 18)
    stride = (w * 3 + 3) & ~3
    a = np.frombuffer(b[off:off + stride * abs(h)], dtype=np.uint8).reshape(abs(h), stride)[:, :w * 3]
    a = a.reshape(abs(h), w, 3)[:, :, ::-1]
    return (a[::-1] if h > 0 else a).astype(np.int64)


def textures():
    d = (ROOT / 'assets/scene_snow/object_textures.emot').read_bytes()
    out = {}
    for i in range(u32(d, 8)):
        t, w, h, at, _ = struct.unpack_from('<Q4I', d, 0x10 + 24 * i)
        out[t] = (w, h, np.frombuffer(d[at:at + 4 * w * h], dtype=np.uint8).reshape(h, w, 4).astype(np.int64))
    return out


def model(tris, tex, fogc, W, H):
    """The documented GS pixel path at the W x H pixel centres."""
    img = np.zeros((H, W, 3), dtype=np.int64)
    depth = np.full((H, W), -np.inf)
    second = np.full((H, W), -np.inf)
    edge = np.zeros((H, W), dtype=bool)
    cov = np.zeros((H, W), dtype=bool)
    sx, sy = 256.0 * 2.0 / W, 112.0 * 2.0 / H                  # GS field pixels per output pixel
    fc = np.array(fogc, dtype=np.int64)
    for t0, _pass, _blk, vs in tris:
        w, h, tx = tex[t0 & CLD]
        X = [v[0] / 16.0 for v in vs]
        Y = [v[1] / 16.0 for v in vs]
        den = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0])
        if den == 0: continue
        # output pixel ranges: X = 2048 + 256 * ndc_x, ndc_x = (px + 0.5) / W * 2 - 1
        px0 = max(0, int(math.floor((min(X) - 1792.0) / sx - 0.5)))
        px1 = min(W - 1, int(math.ceil((max(X) - 1792.0) / sx)))
        py0 = max(0, int(math.floor((min(Y) - 1936.0) / sy - 0.5)))
        py1 = min(H - 1, int(math.ceil((max(Y) - 1936.0) / sy)))
        if px0 > px1 or py0 > py1: continue
        gx = 1792.0 + (np.arange(px0, px1 + 1) + 0.5) * sx
        gy = 1936.0 + (np.arange(py0, py1 + 1) + 0.5) * sy
        GX, GY = np.meshgrid(gx, gy)
        l1 = ((X[0] - X[2]) * (GY - Y[0]) + (Y[2] - Y[0]) * (GX - X[0])) / den
        l2 = ((X[1] - X[0]) * (GY - Y[0]) - (Y[1] - Y[0]) * (GX - X[0])) / den
        l0 = 1.0 - l1 - l2
        inside = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
        if not inside.any(): continue
        # distance to the edges in output pixels (for the exclusion band)
        dist = np.full(GX.shape, np.inf)
        for a, b in ((0, 1), (1, 2), (2, 0)):
            ex, ey = (X[b] - X[a]) / sx, (Y[b] - Y[a]) / sy
            n = math.hypot(ex, ey) or 1.0
            dist = np.minimum(dist, np.abs(((GX - X[a]) / sx) * ey - ((GY - Y[a]) / sy) * ex) / n)
        lam = (l0, l1, l2)
        z = sum(lam[k] * vs[k][2] for k in range(3))
        S = sum(lam[k] * f32(vs[k][5]) for k in range(3))
        Tt = sum(lam[k] * f32(vs[k][6]) for k in range(3))
        Q = sum(lam[k] * f32(vs[k][7]) for k in range(3))
        F = np.clip(np.floor(sum(lam[k] * vs[k][3] for k in range(3)) + 0.001), 0, 255).astype(np.int64)
        cf = [np.clip(np.floor(sum(lam[k] * vs[k][4][c] for k in range(3)) + 0.001), 0, 255).astype(np.int64)
              for c in range(4)]
        with np.errstate(divide='ignore', invalid='ignore'):
            uu = np.floor(S / Q * w * 16).astype(np.int64) - 8
            vv = np.floor(Tt / Q * h * 16).astype(np.int64) - 8
        fu, fv = uu & 15, vv & 15
        xa, xb = (uu >> 4) & (w - 1), ((uu >> 4) + 1) & (w - 1)
        ya, yb = (vv >> 4) & (h - 1), ((vv >> 4) + 1) & (h - 1)
        ct = (tx[ya, xa] * ((16 - fu) * (16 - fv))[..., None] + tx[ya, xb] * (fu * (16 - fv))[..., None] +
              tx[yb, xa] * ((16 - fu) * fv)[..., None] + tx[yb, xb] * (fu * fv)[..., None]) >> 8
        rgb = np.stack([np.minimum(((ct[..., k] * cf[k]) >> 7) + cf[3], 255) for k in range(3)], axis=-1)
        alpha = np.minimum(ct[..., 3] + cf[3], 255)
        rgb = (rgb * F[..., None] + fc * (255 - F)[..., None]) >> 8
        draw = inside & (alpha > 0)
        sub = (slice(py0, py1 + 1), slice(px0, px1 + 1))
        d0, s0 = depth[sub], second[sub]
        wins = draw & (z >= d0)                                  # the port's less-equal on d(Z): larger Z wins
        s0[:] = np.where(draw & ~wins, np.maximum(s0, z), np.where(wins, np.maximum(s0, d0), s0))
        d0[:] = np.where(wins, z, d0)
        img[sub] = np.where(wins[..., None], rgb, img[sub])
        edge[sub] = np.where(wins, dist < 1.5, edge[sub])
        cov[sub] |= draw
    tie = np.abs(depth - second) < 1e-7 * np.maximum(1.0, np.abs(depth))
    return img, cov & ~edge & ~tie


def main():
    assert sys.platform == 'darwin', 'the Metal backend fixture'
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    T.ELF = elf
    T.LIB = T.build_library()
    binary = build()
    bank = {ewm.TABLE_ADDRESS + m['offset'] for m in ewm.build(DECOMP / 'extract')['models']}
    tex = textures()
    report = {}
    for beat in (BEATS if FULL else BEATS[:1]):
        p = DECOMP / 'build/s87/route' / beat
        ram = (p / 'eeMemory.bin').read_bytes()
        found = units(ram, bank)
        assert found, (beat, 'no bank-model unit in the current list')
        ctx = u32(ram, 0x275670)
        idx = u32(ram, ctx + 0x9C)
        fog_row = struct.unpack_from('<4I', ram, 0x816440 + 0x80 * idx + 0x10 + 16 * 4)
        fogc = list(ram[0x814220 + idx * 0x30 + 0x360:0x814220 + idx * 0x30 + 0x363])
        bmp = OUT / f'{beat}.bmp'
        env = dict(os.environ, EM_HEADLESS='1')
        subprocess.run([str(binary), str(p / 'eeMemory.bin'), repr(f32(fog_row[2])), repr(f32(fog_row[3])),
                        str(fogc[0]), str(fogc[1]), str(fogc[2]), str(bmp)] +
                       [f'{a:x}:{n:x}' for a, n in found], cwd=ROOT, env=env, check=True, timeout=60)
        got = read_bmp(bmp)
        H, W = got.shape[:2]
        assert W * 3 == H * 4, ('the capture is not the 4:3 frame', W, H)
        tris = []
        for a, n in found:
            k, out, _objs, _pieces, why = T.native(ram, ram[a:a + n])
            assert k >= 0, (beat, hex(a), why)
            tris += T.ntris(out, k)
        want, compare = model(tris, tex, fogc, W, H)
        diff = np.abs(got - want).max(axis=2)[compare]
        n = int(compare.sum())
        exact, close = float((diff == 0).mean()), float((diff <= 2).mean())
        assert n > 1000, (beat, 'too few interior pixels', n)
        assert close >= 0.99 and exact >= 0.90, (beat, 'pixels', n, 'exact', exact, 'within 2', close,
                                                  'max', int(diff.max()))
        report[beat] = dict(units=len(found), triangles=len(tris), pixels_compared=n, exact=round(exact, 4),
                            within_2=round(close, 4), max_difference=int(diff.max()), size=[W, H])
    (OUT / 'report.json').write_text(json.dumps(report, indent=1) + '\n')
    banner(f'{len(report)} of {len(BEATS)} route beats')
    print('object unit GPU: PASS ' + json.dumps(report))


if __name__ == '__main__':
    sys.exit(main())
