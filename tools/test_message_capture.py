#!/usr/bin/env python3
"""A message frame of the live port against the original's (WP-8).

The original (EM_REFERENCE, tools/reference_root.py):
- fork (default): the PCSX2 fork's capture of the elevator refusal
  (decomp build/fork_refs/pixels/messages/elevator_refusal, PCSX2_FORK.md
  "Pixel references re-recorded on the fork"): the canonical route beat 02
  replayed, stopped at the vsync wait where the message block at 0x2821B0
  holds the terminal's refusal line 0x8000001A in its fifth present tick
  (mode 2, phase 1, +0x68 = 5; wait/eeMemory.bin, checked here), and
  displayed.bin, the 512x224 field shown at the next loop top (the line's
  fifth presentation);
- v263: build/startup-reference/elevator/refusal (user-local), the same
  message state, and the save state's 640x480 host screenshot original.png.

The port: the level smoke up to its refusal phase, headless, with
EM_LEVEL_SMOKE_MESSAGE_CAPTURE writing the frame whose step F presents the
line for the fifth time.

Like with like. On the fork the comparison is field to field: the port's
presented frame read back at the centre of each field pixel's place in the
picture (tools/test_fb2_pixels.py shown_field: the message glyphs are the
GPU's 2D overlay pass over the GS field, as the original's are part of its
displayed buffer), against the original's 512x224 field. On v263 it is the
640x480 frame (the port capture scaled down by averaging) against the
screenshot.

Compared: the text's pixels (bright, near-grey, in the lower text band),
grouped into lines by empty rows. The two frames must show the same number
of text lines, and each line's bounding box must agree within one pixel on
every edge (field pixels and field rows on the fork). This checks the glyph
layout chain (001FD950 centring, 001FE070, 001FC7B0, 001CC1E0 advances,
001CC3B0 passes) end to end; the glyph pixels themselves are the atlas
boundary (docs/MESSAGE_GLYPH.md) and are not compared.
"""
import os
import struct
import subprocess
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import reference_root as refs  # noqa: E402
from test_background_reference import read_bmp, read_png  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
if refs.FORK_MODE:
    CAPTURE = refs.FORK / 'pixels/messages/elevator_refusal'
    RAM = CAPTURE / 'wait/eeMemory.bin'
else:
    CAPTURE = refs.BUILD / 'startup-reference/elevator/refusal'
    RAM = CAPTURE / 'eeMemory.bin'
BAND = (200, 400, 440, 476)          # x0, y0, x1, y1 in the 640x480 frame
# The same band in the 512x224 field (x * 512 / 640, y * 224 / 480, rounded
# outward): the fork capture's two lines lie in x 214..297, rows 195..213.
BAND_FIELD = (160, 186, 352, 223)
GS_W, GS_H = 512, 224


def is_text(r, g, b):
    return r > 150 and g > 150 and b > 150 and abs(r - b) < 40


def group_lines(rows, y0, y1):
    """Bounding boxes (x0, y0, x1, y1) of the runs of rows holding text."""
    lines, current = [], None
    for y in range(y0, y1):
        xs = rows.get(y, [])
        if len(xs) > 2:
            if current is None:
                current = [min(xs), y, max(xs), y]
            else:
                current = [min(current[0], min(xs)), current[1], max(current[2], max(xs)), y]
        elif current is not None:
            lines.append(tuple(current))
            current = None
    if current is not None:
        lines.append(tuple(current))
    return lines


def text_lines(w, h, px, scale):
    """The text lines in the 640x480 band (scale: the port capture's factor)."""
    rows = {}
    for y in range(BAND[1], BAND[3]):
        for x in range(BAND[0], BAND[2]):
            if scale == 1:
                r, g, b = px(x, y)
            else:
                acc = [0, 0, 0]
                for dy in range(scale):
                    for dx in range(scale):
                        c = px(x * scale + dx, y * scale + dy)
                        for k in range(3):
                            acc[k] += c[k]
                r, g, b = (v // (scale * scale) for v in acc)
            if is_text(r, g, b):
                rows.setdefault(y, []).append(x)
    return group_lines(rows, BAND[1], BAND[3])


def field_lines(rgb):
    """The text lines in a (224, 512, 3) field's band."""
    rows = {}
    for y in range(BAND_FIELD[1], BAND_FIELD[3]):
        for x in range(BAND_FIELD[0], BAND_FIELD[2]):
            if is_text(*(int(v) for v in rgb[y, x])):
                rows.setdefault(y, []).append(x)
    return group_lines(rows, BAND_FIELD[1], BAND_FIELD[3])


def original_field():
    raw = np.frombuffer((CAPTURE / 'displayed.bin').read_bytes(), dtype=np.uint8)
    assert raw.size == GS_W * GS_H * 4, ('displayed.bin is not a 512x224 field', raw.size)
    return raw.reshape(GS_H, GS_W, 4)[:, :, :3]


def main():
    ram = RAM.read_bytes()
    mode, phase, line = struct.unpack_from('<3I', ram, 0x2821B0)
    frames = struct.unpack_from('<i', ram, 0x2821B0 + 0x68)[0]
    assert (mode, phase, line, frames) == (2, 1, 0x8000001A, 5), 'unexpected capture message state'
    out = ROOT / 'build/message_capture'
    out.mkdir(parents=True, exist_ok=True)
    bmp = out / 'refusal.bmp'
    sidecars = [Path(str(bmp) + ext) for ext in ('.gsfield', '.present')]
    for f in [bmp] + sidecars:
        if f.exists():
            f.unlink()
    env = dict(os.environ, EM_UNCAPPED='1', EM_STARTUP_TEST='newgame-level',
               EM_LEVEL_SMOKE_UNTIL='elevator_refusal', EM_LEVEL_SMOKE_MESSAGE_CAPTURE=str(bmp))
    run = subprocess.run([str(ROOT / 'build/extermination')], cwd=ROOT, env=env,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (out / 'run.log').write_text(run.stdout)
    assert run.returncode == 0 and 'elevator_refusal: PASS' in run.stdout, 'the smoke run failed (run.log)'
    assert bmp.exists(), 'no capture was written'
    if refs.FORK_MODE:
        import test_fb2_pixels as fb2
        assert all(f.exists() for f in sidecars), \
            ('the capture wrote no GS field / placement (the Original profile draws the field)', sidecars)
        port_field, _geometry = fb2.shown_field(fb2.read_bmp(bmp), bmp)
        port = field_lines(port_field)
        original = field_lines(original_field())
        unit = '512x224 field'
    else:
        w, h, px = read_bmp(bmp)
        assert w % 640 == 0 and w * 3 == h * 4, (w, h)
        port = text_lines(w, h, px, w // 640)
        ow, oh, opx = read_png(CAPTURE / 'original.png')
        assert (ow, oh) == (640, 480)
        original = text_lines(ow, oh, opx, 1)
        unit = '640x480'
    assert len(original) == 2, ('original text lines', original)
    assert len(port) == len(original), ('text lines', port, original)
    for p, o in zip(port, original):
        assert all(abs(a - b) <= 1 for a, b in zip(p, o)), ('text line box', p, o)
    for f in [bmp] + sidecars:
        if f.exists():
            f.unlink()
    print(f'message capture ({refs.label()}): PASS refusal 0x8000001A at +0x68 = 5: text lines {port} '
          f'against the original {original} ({unit}, within 1 px)')


if __name__ == '__main__':
    main()
