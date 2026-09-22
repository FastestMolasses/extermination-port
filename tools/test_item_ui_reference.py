#!/usr/bin/env python3
"""Compare native ITEM command order/layouts with executed original drawers.

The analog fan and glyph metrics are explicit rendering-worker boundaries.
The six original functions/layouts and TEX0 data come from local game data.
"""
import hashlib
import json
from pathlib import Path
import subprocess

from export_item_root import Original

ROOT = Path(__file__).resolve().parents[1]


def main():
    elf = (ROOT.parent / 'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
    source = json.loads((ROOT / 'assets/scene_snow/panel/item_root_source.json').read_text())
    atlas = {int(record['tex0'], 16): record for record in source['atlas']}
    out = ROOT / 'build/item_ui_reference'
    out.mkdir(parents=True, exist_ok=True)
    binary = out / 'fixture'
    subprocess.run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=address,undefined', '-Isrc', 'tests/item_ui_test.c',
        'src/game/em_item_ui.c', '-o', str(binary)], cwd=ROOT, check=True)
    result = subprocess.run([str(binary), str(ROOT / 'assets/scene_snow/panel/item_root.emir')],
                            cwd=ROOT, check=True, capture_output=True, text=True)
    lines = iter(result.stdout.splitlines())
    checks = 0
    for selection in range(6):
        assert next(lines) == f'L {selection}'
        original = Original(elf, selection)
        original.collect(0x20F170)
        original.collect(0x20F2A0)
        for command in original.commands:
            line = next(lines).split()
            if command['kind'] == 'analog_trail':
                u, v = atlas[0]['xy']
                kind, expected = 'T', [command['x'], command['y'], u + .5, v + .5]
            else:
                sprite = atlas[command['tex0']]
                u, v = sprite['xy']
                w, h = sprite['wh']
                if command['kind'] == 'background':
                    kind, expected = 'B', [u, v, w, h]
                else:
                    kind = 'Q'
                    expected = [command['mode'], command['x'] / 16 - 1792,
                                (command['y'] / 16 - 1936) * 2,
                                command['w'], command['h'], u, v, u + w, v + h]
                    expected += [(command['rgba'] >> shift & 255) / 128 for shift in (0, 8, 16, 24)]
            actual = list(map(float, line[1:]))
            assert line[0] == kind and len(actual) == len(expected)
            assert all(abs(a - b) < 1e-7 for a, b in zip(actual, expected)), (selection, actual, expected)
            checks += 1
    assert next(lines) == 'R'
    assert list(lines)[-1] == 'PASS'
    report = {'original_ordered_commands': checks, 'hover_layouts': 6,
              'atlas_upload_and_page_invalidation': 'PASS',
              'asan_ubsan': 'PASS', 'boundaries': 'analog fan, glyph metrics and final pixels'}
    (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
