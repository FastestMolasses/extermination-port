#!/usr/bin/env python3
"""Export and verify AREA03's recorded sub-0 assets into private assets/.

Reuses the existing level, table and sound pipelines without relaxing their
comparisons. World texture delivery is audited separately; this export does
not certify a live renderer or an unrecorded AREA03 subarea.
"""
import argparse
import json

import export_area03_common as A
from level_asset_audit import private_assets


def export(kind='all'):
    private_assets(A.ROOT)
    A.install()
    A.OUT.mkdir(parents=True, exist_ok=True)
    A.SCRATCH.mkdir(parents=True, exist_ok=True)
    results = {}
    if kind in ('all', 'level'):
        el = A.LV.L.load_export_level()
        A.LV.export_target('area03', el, A.C.read_elf(), A.OUT, A.C.ISO_PATH, False)
        results['level'] = True
    if kind in ('all', 'tables'):
        A.TB.export_target('area03', A.OUT)
        results['tables'] = True
    if kind in ('all', 'sfx'):
        A.SX.export_target('area03', A.OUT)
        results['sfx'] = True
    (A.SCRATCH/f'export-{kind}.json').write_text(json.dumps(results, indent=2)+'\n')
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kind', choices=('all', 'level', 'tables', 'sfx'), default='all')
    args = parser.parse_args()
    export(args.kind)
    print('AREA03 recorded asset export: PASS')


if __name__ == '__main__':
    main()
