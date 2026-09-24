#!/usr/bin/env python3
"""A message frame of the live port against the original's screenshot (WP-8).

The original: build/startup-reference/elevator/refusal (user-local): the
terminal's refusal line 0x8000001A mid-presentation, its message block at the
fifth present tick (+0x68 = 5, checked here), and the save state's 640x480
screenshot original.png. The port: the level smoke up to its refusal phase,
headless, with EM_LEVEL_SMOKE_MESSAGE_CAPTURE writing the frame whose step F
presents the line for the fifth time.

Compared on the 640x480 frame (the port capture is scaled down by averaging):
the text's pixels (bright, near-grey, in the lower text band), grouped into
lines by empty rows. The two frames must show the same number of text lines,
and each line's bounding box must agree within one pixel on every edge. This
checks the glyph layout chain (001FD950 centring, 001FE070, 001FC7B0,
001CC1E0 advances, 001CC3B0 passes) end to end; the glyph pixels themselves
are the atlas boundary (docs/MESSAGE_GLYPH.md) and are not compared.
"""
import os
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_background_reference import read_bmp, read_png  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT.parent / 'Extermination/build/startup-reference/elevator/refusal'
BAND = (200, 400, 440, 476)          # x0, y0, x1, y1 in the 640x480 frame


def text_lines(w, h, px, scale):
    """Bounding boxes (x0, y0, x1, y1) of the text lines in the band."""
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
            if r > 150 and g > 150 and b > 150 and abs(r - b) < 40:
                rows.setdefault(y, []).append(x)
    lines, current = [], None
    for y in range(BAND[1], BAND[3]):
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


def main():
    ram = (CAPTURE / 'eeMemory.bin').read_bytes()
    mode, phase, line = struct.unpack_from('<3I', ram, 0x2821B0)
    frames = struct.unpack_from('<i', ram, 0x2821B0 + 0x68)[0]
    assert (mode, phase, line, frames) == (2, 1, 0x8000001A, 5), 'unexpected capture message state'
    out = ROOT / 'build/message_capture'
    out.mkdir(parents=True, exist_ok=True)
    bmp = out / 'refusal.bmp'
    if bmp.exists():
        bmp.unlink()
    env = dict(os.environ, EM_UNCAPPED='1', EM_STARTUP_TEST='newgame-level',
               EM_LEVEL_SMOKE_UNTIL='elevator_refusal', EM_LEVEL_SMOKE_MESSAGE_CAPTURE=str(bmp))
    run = subprocess.run([str(ROOT / 'build/extermination')], cwd=ROOT, env=env,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (out / 'run.log').write_text(run.stdout)
    assert run.returncode == 0 and 'elevator_refusal: PASS' in run.stdout, 'the smoke run failed (run.log)'
    assert bmp.exists(), 'no capture was written'
    w, h, px = read_bmp(bmp)
    assert w % 640 == 0 and w * 3 == h * 4, (w, h)
    port = text_lines(w, h, px, w // 640)
    ow, oh, opx = read_png(CAPTURE / 'original.png')
    assert (ow, oh) == (640, 480)
    original = text_lines(ow, oh, opx, 1)
    assert len(original) == 2, ('original text lines', original)
    assert len(port) == len(original), ('text lines', port, original)
    for p, o in zip(port, original):
        assert all(abs(a - b) <= 1 for a, b in zip(p, o)), ('text line box', p, o)
    bmp.unlink()
    print(f'message capture: PASS refusal 0x8000001A at +0x68 = 5: text lines {port} '
          f'against the original {original} (640x480, within 1 px)')


if __name__ == '__main__':
    main()
