#!/usr/bin/env python3
"""AREA00 export/capture and native resource-loader checks, not a gameplay pass.

The texture probe exercises the selected AREA00 paths and reports the complete
catalog's capacity refusal explicitly. Receiver checks exercise the actual shared
selector, including retained prior state after a refused or failed replacement.
"""
import ctypes as C
import json
import os
from pathlib import Path
import subprocess
import tempfile

import export_area00_world_textures as T
import export_area00_shadow_receivers as S
import test_world_textures_reference as W

ROOT = T.A0.ROOT
OUT = ROOT/'build/level3/resources'


def main():
    os.chdir(ROOT)
    OUT.mkdir(parents=True, exist_ok=True)
    libpath = OUT/'resources.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-shared', '-fPIC', '-Isrc', '-Wl,-dead_strip',
                    '-Wl,-exported_symbol,_area00_*', '-Wl,-exported_symbol,_texture_test_*',
                    '-Wl,-exported_symbol,_em_world_textures_live_*', 'tests/world_textures_bridge.c',
                    'tests/area00_resources_bridge.c', 'src/game/em_world_textures_live.c',
                    'src/game/em_shadow_original.c', '-o', str(libpath)], check=True)
    lib = C.CDLL(str(libpath))
    lib.em_world_textures_live_bind.argtypes = [C.c_uint, C.c_uint]
    lib.em_world_textures_live_ensure.argtypes = [C.c_void_p]
    lib.area00_receivers_check.argtypes = [C.c_char_p, C.c_uint, C.c_uint]
    lib.area00_receivers_select.argtypes = [C.c_uint, C.c_uint]
    lib.area00_receivers_selected.argtypes = [C.c_uint]
    lib.texture_test_entry.argtypes = [C.c_uint, C.c_uint]
    lib.texture_test_entry.restype = C.POINTER(W.Entry)
    capacity = lib.area00_texture_capacity()
    reports = []
    for sub in T.A0.SUBS:
        textures, tex = T.export(sub, OUT/f'sub{sub}.emot', full=bool(os.getenv('EM_TEST_FULL')))
        assert textures == (ROOT/f'assets/area00_sub{sub}_world_textures.emot').read_bytes(), 're-export textures'
        receivers, shadow = S.build(sub)
        source = ROOT/f'assets/area00_sub{sub}_shadow_receivers.emsr'
        assert receivers == source.read_bytes(), 're-export shadow receivers'
        assert lib.area00_receivers_check(str(source).encode(), shadow['objects'], shadow['blocks']) == 0
        bad = OUT/f'sub{sub}-truncated.emsr'
        bad.write_bytes(receivers[:-1])
        assert lib.area00_receivers_check(str(bad).encode(), shadow['objects'], shadow['blocks']) == -1
        bad.unlink()
        assert lib.area00_receivers_select(0, sub) == 0
        selected = tuple(lib.area00_receivers_selected(i) for i in range(4))
        assert selected == (0, sub, shadow['objects'], shadow['blocks'])
        for invalid in ((0, 2), (0, 255), (1, 1), (99, 0)):
            assert lib.area00_receivers_select(*invalid) == -1
            assert tuple(lib.area00_receivers_selected(i) for i in range(4)) == selected
            assert lib.em_world_textures_live_bind(*invalid) == -1
            assert lib.em_world_textures_live_ensure(1) == -1
        lib.area00_receivers_pending(1)
        assert lib.area00_receivers_select(0, 1-sub) == -1
        assert tuple(lib.area00_receivers_selected(i) for i in range(4)) == selected
        lib.area00_receivers_pending(0)
        with tempfile.TemporaryDirectory(dir=OUT) as temp:
            os.chdir(temp)
            path = Path(f'assets/area00_sub{sub}_world_textures.emot')
            path.parent.mkdir()
            path.write_bytes(textures)
            assert lib.em_world_textures_live_bind(0, sub) == 0
            before = lib.texture_test_uploads(0), lib.texture_test_resets(0)
            result = lib.em_world_textures_live_ensure(1)
            if tex['count'] > capacity:
                assert result == -1
                assert lib.em_world_textures_live_ensure(1) == -1
                assert before == (lib.texture_test_uploads(0), lib.texture_test_resets(0))
                delivery = f'BLOCKED: {tex["count"]} eager keys exceed capacity {capacity}'
            else:
                assert result == 0
                expected = {k: (w, h, W.fnv(p)) for k, (w, h, p) in W.catalog(path).items()}
                got = {}
                for i in range(lib.texture_test_count(0)):
                    e = lib.texture_test_entry(0, i).contents
                    assert e.key not in got
                    got[e.key] = e.width, e.height, e.hash
                assert got == expected
                delivery = 'complete AREA00 catalog parser and upload exact'
            # A missing or malformed selected receiver bank cannot drop the
            # previous area's receivers. These are private fixture paths.
            receiver_path = Path(f'assets/area00_sub{sub}_shadow_receivers.emsr')
            assert lib.area00_receivers_select(0, sub) == -1
            receiver_path.write_bytes(receivers[:-1])
            assert lib.area00_receivers_select(0, sub) == -1
            assert tuple(lib.area00_receivers_selected(i) for i in range(4)) == selected
            receiver_path.write_bytes(receivers)
            assert lib.area00_receivers_select(0, sub) == 0
            assert tuple(lib.area00_receivers_selected(i) for i in range(4)) == selected
            os.chdir(ROOT)
        reports.append(dict(sub=sub, textures=tex['count'], texture_captures=len(tex['captures']),
                            shadow=shadow, capacity=capacity, delivery=delivery))
        print(f'AREA00/{sub}: export/capture and shadow-loader checks PASS; {delivery}')
    (OUT/'report.json').write_text(json.dumps(reports, indent=2)+'\n')
    print('Resource verification only; no native AREA00 gameplay or drawing pass is claimed.')


if __name__ == '__main__':
    main()
