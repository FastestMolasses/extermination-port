#!/usr/bin/env python3
"""Render a three-frame Metal fixture and verify the original UI blend laws.

Requires the macOS graphical session; it creates and closes its own window.
No original assets, emulator process or user save data are involved.
"""
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    assert sys.platform == 'darwin', 'This fixture tests the Metal backend'
    output = ROOT / 'build/overlay_blend'
    output.mkdir(parents=True, exist_ok=True)
    binary, capture = output / 'fixture', output / 'pixels.bmp'
    subprocess.run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
        '-Isrc', 'tests/overlay_blend_test.c', 'src/gfx/metal/em_gfx_metal.m',
        'src/platform/mac/em_platform_mac.m', 'src/game/em_lighting.c',
        'src/game/em_packet_chain_original.c', 'src/game/em_status_ui_leftovers.c',
        'src/game/em_object_unit.c',
        '-framework', 'Cocoa', '-framework', 'Metal', '-framework', 'QuartzCore',
        '-lm', '-o', str(binary)], cwd=ROOT, check=True)
    subprocess.run([str(binary), str(capture)], cwd=ROOT, check=True, timeout=30)
    data = capture.read_bytes()
    offset = struct.unpack_from('<I', data, 10)[0]
    width, height = struct.unpack_from('<ii', data, 18)
    assert data[:2] == b'BM' and struct.unpack_from('<H', data, 28)[0] == 24
    stride = (width * 3 + 3) & ~3
    expected = [(.15, .3, .45), (.3, .6, .9), (.1, .2, .3), (.1, .2, .3),
                (.6, .6, .6), (.9, .9, 1), (.2, .4, .6), (.3, .6, .9),
                (.2, .4, .6), (.2, .4, .6), (.3, .5, .8)]
    samples = []
    for column, color in enumerate(expected):
        x, y = int((column + .5) * width / len(expected)), abs(height) // 2
        address = offset + y * stride + x * 3
        pixel = tuple(reversed(data[address:address + 3]))
        assert all(abs(pixel[i] - round(color[i] * 255)) <= 1 for i in range(3)), \
            (column, pixel, color)
        samples.append(pixel)
    report = {'samples': samples, 'four_blend_modes': 'PASS', 'mixed_clamp_order': 'PASS',
              'opaque_alpha_zero_discard': 'PASS', 'additive_ignores_alpha': 'PASS',
              'subtract_alpha_zero_discard': 'PASS',
              'gouraud_triangle': 'PASS',
              'unorm_rounding_tolerance': 1}
    (output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
