#!/usr/bin/env python3
"""Re-export exact receiver bytes and exercise the native asset parser.

By default checks AREA02's two subs. EM_TEST_FULL=1 checks every profile in
separate processes because the older exporters configure module globals.
No live area selector or renderer is changed.
"""
import argparse
import ctypes as C
import json
import os
from pathlib import Path
import subprocess
import sys

import export_level_shadow_receivers as E
import level_asset_profiles as P

ROOT = P.ROOT
OUT = ROOT/'build/levels/shadow-reference'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=P.SUBS)
    args = parser.parse_args()
    if args.profile is None:
        profiles = P.SUBS if os.environ.get('EM_TEST_FULL', '') not in ('', '0') else ('02',)
        for profile in profiles:
            subprocess.run([sys.executable, __file__, '--profile', profile], cwd=ROOT, check=True)
        return
    profile = args.profile
    out = OUT/profile
    out.mkdir(parents=True, exist_ok=True)
    library = out/'receivers.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-shared', '-fPIC', '-Isrc', '-Wl,-dead_strip',
                    '-Wl,-exported_symbol,_area00_receivers_check',
                    'tests/area00_resources_bridge.c', 'src/game/em_shadow_original.c',
                    '-o', str(library)], cwd=ROOT, check=True)
    native = C.CDLL(str(library))
    native.area00_receivers_check.argtypes = [C.c_char_p, C.c_uint, C.c_uint]
    results = []
    for sub in P.SUBS[profile]:
        data, report = E.build(profile, sub)
        source = P.output(profile, sub)/'shadow_receivers.emsr'
        assert data == source.read_bytes(), 're-export must match installed private asset'
        assert native.area00_receivers_check(str(source).encode(), report['objects'], report['blocks']) == 0
        bad = out/f'sub{sub}-truncated.emsr'
        bad.write_bytes(data[:-1])
        try:
            assert native.area00_receivers_check(str(bad).encode(), report['objects'], report['blocks']) == -1
        finally:
            bad.unlink()
        results.append(report)
        print(f'{profile}/{sub}: PASS exact receiver bytes and native parser', flush=True)
    (out/'report.json').write_text(json.dumps(results, indent=2)+'\n')


if __name__ == '__main__':
    main()
