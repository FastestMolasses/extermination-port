#!/usr/bin/env python3
"""Native EMOT parser over complete private later-area catalogs; never live binds."""
import ctypes as C
import json
import os
from pathlib import Path
import subprocess

from level_asset_profiles import ROOT, SUBS, output
from test_level_world_textures_reference import decode


class Texture(C.Structure):
    _fields_ = [('key', C.c_uint64), ('width', C.c_uint32), ('height', C.c_uint32), ('pixels', C.c_void_p)]


def main():
    out = ROOT/'build/levels/texture-catalog-parser'
    out.mkdir(parents=True, exist_ok=True)
    library = out/'parser.dylib'
    subprocess.run(['cc', '-O2', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC', '-Isrc',
                    '-Wl,-dead_strip', '-Wl,-exported_symbol,_level_texture_catalog_*',
                    'tests/level_texture_catalog_bridge.c', '-o', str(library)], cwd=ROOT, check=True)
    lib = C.CDLL(str(library))
    lib.level_texture_catalog_load.argtypes = [C.c_char_p]
    lib.level_texture_catalog_entry.argtypes = [C.c_uint]
    lib.level_texture_catalog_entry.restype = C.POINTER(Texture)
    full = os.getenv('EM_TEST_FULL', '') not in ('', '0')
    rows = []
    for profile in SUBS if full else ('03',):
        for sub in SUBS[profile]:
            folder = output(profile, sub)
            catalog = json.loads((folder/'world_texture_catalog.json').read_text())
            for variant in catalog['variants']:
                source = folder/variant['file']; data = source.read_bytes(); expected = decode(data)
                assert lib.level_texture_catalog_load(str(source).encode()) == 0
                count = lib.level_texture_catalog_count()
                assert count == len(expected) == catalog['fixed_textures']
                got = {}
                for index in range(count):
                    row = lib.level_texture_catalog_entry(index).contents
                    assert row.key not in got
                    got[row.key] = C.string_at(row.pixels, 4*row.width*row.height)
                assert got == expected, 'native parser changed keys or texels'
                assert not lib.level_texture_catalog_entry(count), 'entry bound'
                bad = out/'truncated.emot'
                bad.write_bytes(data[:-1])
                try:
                    assert lib.level_texture_catalog_load(str(bad).encode()) == -1
                    assert lib.level_texture_catalog_count() == 0
                finally:
                    bad.unlink()
                rows.append(dict(profile=profile, sub=sub, mode=variant['mode'], costume=variant['costume'],
                                 textures=count, bytes=len(data)))
            print('PASS native texture catalog', profile, sub, len(catalog['variants']), 'variants', flush=True)
    lib.level_texture_catalog_free()
    (out/('full.json' if full else 'quick.json')).write_text(json.dumps(rows, indent=2)+'\n')


if __name__ == '__main__':
    main()
