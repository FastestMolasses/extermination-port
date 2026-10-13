#!/usr/bin/env python3
"""AREA00 export/capture and native resource-loader checks, not a gameplay pass.

The texture probe reports the current eager-catalog capacity blocker explicitly.
It uses the existing supported catalog path in a private directory to exercise
the production parser without enabling an unfinished AREA00 live composition.
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
                    '-shared', '-fPIC', '-Isrc', 'tests/world_textures_bridge.c',
                    'tests/area00_resources_bridge.c', 'src/game/em_world_textures_live.c',
                    'src/game/em_shadow_original.c', '-o', str(libpath)], check=True)
    lib = C.CDLL(str(libpath))
    lib.em_world_textures_live_bind.argtypes = [C.c_uint, C.c_uint]
    lib.em_world_textures_live_ensure.argtypes = [C.c_void_p]
    lib.area00_receivers_check.argtypes = [C.c_char_p, C.c_uint, C.c_uint]
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
        with tempfile.TemporaryDirectory(dir=OUT) as temp:
            os.chdir(temp)
            path = Path('assets/area01_world_textures.emot')
            path.parent.mkdir()
            path.write_bytes(textures)
            assert lib.em_world_textures_live_bind(1, 0) == 0
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
                delivery = 'catalog parser and upload exact; AREA00 live binding still required'
            os.chdir(ROOT)
        reports.append(dict(sub=sub, textures=tex['count'], texture_captures=len(tex['captures']),
                            shadow=shadow, capacity=capacity, delivery=delivery))
        print(f'AREA00/{sub}: export/capture and shadow-loader checks PASS; {delivery}')
    (OUT/'report.json').write_text(json.dumps(reports, indent=2)+'\n')
    print('Resource verification only; no native AREA00 gameplay or drawing pass is claimed.')


if __name__ == '__main__':
    main()
