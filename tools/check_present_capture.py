#!/usr/bin/env python3
"""Check a presented frame of the Original profile against its own GS field
(docs/GS_EXACT.md section 11; the mapping src/gs/em_gs_display.h).

A field frame captured headless (EM_FB_CAPTURE_TICKS, EM_REPLAY_CAPTURE or
em_gfx_request_capture) writes three files: <path> (the presented frame, a
24-bit BMP), <path>.gsfield (the 512 x 224 field) and <path>.present (the
field's XYOFFSET_1, the constants f_gsfield drew it with: the game
rectangle's origin and scale, the shift in pixels and lines with the
field's line; the overlay pass's viewport; BGCOLOR). For every pixel of the
frame this tool computes what the placement says it shows, with the same
expression as the shader:

  inside the game rectangle: px = (i + 0.5 - origin_x) * scale_x,
  py = (j + 0.5 - origin_y) * scale_y, x = px - shift_x, y = py - shift_y;
  BGCOLOR when (x, y) is outside 0..512 x 0..448, else field texel
  (floor(x), floor(y) >> 1);
  outside the game rectangle: black (the drawable's clear).

and compares it with the captured pixel. Where x or y lies within 1e-3 of
a texel edge the GPU's rounding may take either side; both are accepted
and counted ("edge"). It also checks the field's line against its
XYOFFSET_1 (OFY whole: 0, half: 1).

The overlay pass (letterbox bands, text, fades) is placed with the field's
own line (src/gs/em_gs_display.h em_gs_display_viewport): its viewport must
be the game rectangle moved by the same shift, line included, as the field
(checked on every capture; the original draws its bands and text into the
field through the same XYOFFSET_1, so they move with the field). Pixels the
overlay pass drew over the field differ and are reported with their
bounding box; --overlay-ok accepts them (they are not the placement's).
For each field pixel the tool counts how much of its footprint (the frame
pixels that show it) the overlay drew: a field row is in a band when the
overlay drew on at least half its columns, and split when on a quarter of
its columns it drew part of a footprint only (half band, half picture).
--bands=N (letterbox frames) requires exactly N band rows at the top and N
at the bottom of the field (001AE900: 32 of 224 each) and no split row at
the band edges.

Also writes <path>.png (the frame) for viewing. Runs natively on macOS
(numpy). Usage: python3 tools/check_present_capture.py [--overlay-ok] [--bands=N] <capture.bmp>...
"""
import struct
import sys
import zlib
from pathlib import Path

import numpy as np


def read_bmp(path):
    b = Path(path).read_bytes()
    off, = struct.unpack_from('<I', b, 10)
    w, h = struct.unpack_from('<ii', b, 18)
    bpp, = struct.unpack_from('<H', b, 28)
    assert bpp == 24, (path, bpp)
    row = (w * 3 + 3) & ~3
    a = np.frombuffer(b, np.uint8, row * abs(h), off).reshape(abs(h), row)[:, :w * 3].reshape(abs(h), w, 3)
    a = a[::-1] if h > 0 else a
    return a[:, :, ::-1].copy()          # BGR -> RGB, top row first


def write_png(path, rgb):
    h, w, _ = rgb.shape
    raw = b''.join(b'\0' + rgb[y].tobytes() for y in range(h))

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def read_present(path):
    v = {}
    for line in Path(path).read_text().splitlines():
        k, *rest = line.split()
        v[k] = rest
    return v


def check(capture, overlay_ok, bands=None):
    frame = read_bmp(capture)
    field = np.frombuffer(Path(str(capture) + '.gsfield').read_bytes(), np.uint8).reshape(224, 512, 4)[:, :, :3]
    p = read_present(str(capture) + '.present')
    xy = int(p['xyoffset'][0], 16)
    ox, oy = (np.float32(float(t)) for t in p['origin'])
    sx, sy = (np.float32(float(t)) for t in p['scale'])
    shx, shy = (np.float32(float(t)) for t in p['shift'])
    assert 'overlay' in p, (capture, 'no overlay viewport in the .present (a capture before the overlay-line fix)')
    ov = [float(t) for t in p['overlay']]
    bg = np.array([round(float(t) * 255) for t in p['bg']], np.uint8)
    frac = (xy >> 32) & 0xF
    line = {0: 0, 8: 1}.get(frac)
    assert line is not None, (capture, 'OFY fraction', frac)
    H, W, _ = frame.shape
    rw, rh = 512.0 / float(sx), 448.0 / float(sy)
    i = np.arange(W, dtype=np.float32)
    j = np.arange(H, dtype=np.float32)
    x = ((i + np.float32(0.5) - ox) * sx - shx).astype(np.float32)
    y = ((j + np.float32(0.5) - oy) * sy - shy).astype(np.float32)
    inside_rect_x = (i + 0.5 >= float(ox)) & (i + 0.5 < float(ox) + rw)
    inside_rect_y = (j + 0.5 >= float(oy)) & (j + 0.5 < float(oy) + rh)
    rect = inside_rect_y[:, None] & inside_rect_x[None, :]

    def expect(xv, yv):
        """The image the placement gives for the sample positions (xv, yv)."""
        ix, iy = (xv >= 0) & (xv < 512), (yv >= 0) & (yv < 448)
        cx = np.floor(np.clip(xv, 0, 511.5)).astype(np.int64)
        cy = (np.floor(np.clip(yv, 0, 447.5)).astype(np.int64) >> 1)
        img = np.where((iy[:, None] & ix[None, :])[:, :, None], field[cy][:, cx], bg[None, None, :])
        return np.where(rect[:, :, None], img, 0), iy[:, None] & ix[None, :]

    want, covered = expect(x, y)
    exact = np.all(frame == want, axis=2)
    # a sample within 1e-3 of a texel edge may round to either side
    d = np.float32(1e-3)
    edge_ok = np.zeros_like(exact)
    for xv, yv in ((x - d, y), (x + d, y), (x, y - d), (x, y + d)):
        edge_ok |= np.all(frame == expect(xv, yv)[0], axis=2)
    bad = ~exact & ~edge_ok
    n_bg = int(np.sum(rect & ~covered))
    # the overlay pass's viewport: the game rectangle moved by the field's
    # shift, its line included (em_gs_display_viewport)
    want_ov = (float(ox) + float(shx) * rw / 512.0, float(oy) + float(shy) * rh / 448.0, rw, rh)
    ov_ok = all(abs(a - b) < 1e-3 for a, b in zip(ov, want_ov))
    # per field pixel: how much of its footprint the overlay drew
    fr = np.where(inside_rect_y & (y >= 0) & (y < 448), np.floor(np.clip(y, 0, 447.5)).astype(np.int64) >> 1, -1)
    fc = np.where(inside_rect_x & (x >= 0) & (x < 512), np.floor(np.clip(x, 0, 511.5)).astype(np.int64), -1)
    jr, ic = np.nonzero(fr >= 0)[0], np.nonzero(fc >= 0)[0]
    drawn = np.zeros((224, 512))
    count = np.zeros((224, 512))
    sub = bad[np.ix_(jr, ic)].astype(np.float64)
    np.add.at(drawn, (fr[jr][:, None], fc[ic][None, :]), sub)
    np.add.at(count, (fr[jr][:, None], fc[ic][None, :]), 1.0)
    shown = count > 0
    cols = np.maximum(shown.sum(axis=1), 1)
    in_band = ((drawn > 0) & shown).sum(axis=1) / cols >= 0.5
    split = ((drawn > 0) & (drawn < count) & shown).sum(axis=1) / cols >= 0.25
    rows_shown = np.nonzero(shown.any(axis=1))[0]
    top = bottom = 0
    if len(rows_shown):
        r0, r1 = int(rows_shown[0]), int(rows_shown[-1])
        while r0 + top <= r1 and in_band[r0 + top]:
            top += 1
        while r1 - bottom >= r0 and in_band[r1 - bottom]:
            bottom += 1
        edges = {r0 + top - 1, r0 + top, r1 - bottom, r1 - bottom + 1} & set(range(r0, r1 + 1))
    else:
        edges = set()
    split_edges = sorted(r for r in edges if split[r])
    write_png(str(capture) + '.png', frame)
    msg = (f'{capture}: line {line} (OFY .{5 if line else 0}), shift {float(shx):g} px / {float(shy) - line:g} lines, '
           f'{int(exact.sum())} exact, {int((~exact & edge_ok).sum())} on a texel edge, {n_bg} BGCOLOR pixels, '
           f'{int(bad.sum())} differ; overlay viewport y {ov[1]:.4f} ({"with" if ov_ok else "NOT with"} the field), '
           f'band rows {top} + {bottom}, split rows {int(split.sum())}'
           f'{" (at the band edges: " + ", ".join(map(str, split_edges)) + ")" if split_edges else ""}')
    if bad.any():
        rows = np.nonzero(bad.any(axis=1))[0]
        runs, start = [], rows[0]
        for a, b in zip(rows, list(rows[1:]) + [None]):
            if b != a + 1:
                runs.append(f'{start}..{a}')
                start = b
        xs = np.nonzero(bad.any(axis=0))[0]
        msg += f' (rows {", ".join(runs[:6])}{" ..." if len(runs) > 6 else ""}; columns {xs.min()}..{xs.max()})'
    ok = ov_ok and (not bad.any() or overlay_ok)
    if bands is not None:
        band_ok = top == bands and bottom == bands and not split_edges
        msg += f'; bands {"PASS" if band_ok else "FAIL"} (want {bands} + {bands}, no split edge row)'
        ok = ok and band_ok
    print(msg)
    return ok


def main(argv):
    overlay_ok = '--overlay-ok' in argv
    bands = None
    for a in argv:
        if a.startswith('--bands='):
            bands = int(a.split('=', 1)[1])
    paths = [a for a in argv if not a.startswith('--')]
    if not paths:
        print(__doc__)
        return 2
    ok = all([check(Path(a), overlay_ok, bands) for a in paths])
    print('present capture: ' + ('PASS' if ok else 'FAIL'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
