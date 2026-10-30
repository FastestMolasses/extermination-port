#!/usr/bin/env python3
"""Export the AREA04 receiver bank using the existing checked EMSR writer
(export_area00_shadow_receivers.py over export_area04_common)."""
import argparse
import json
from pathlib import Path

import export_area04_common as A0
import export_area01_shadow_receivers as E
import reference_root as refs


def build(sub):
    A0.configure(sub)
    data, report = E.build()
    report.update(area=4, sub=sub, reference=refs.label())
    return data, report


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out', type=Path, default=A0.ROOT/'assets')
    args = ap.parse_args()
    if args.out.is_symlink() or any(p.is_symlink() for p in args.out.parents):
        raise SystemExit('symlink output directory')
    args.out.mkdir(parents=True, exist_ok=True)
    for sub in A0.SUBS:
        data, report = build(sub)
        path = args.out/f'area04_sub{sub}_shadow_receivers.emsr'
        for output, content in ((path, data), (path.with_suffix('.json'), (json.dumps(report, indent=2)+'\n').encode())):
            if output.is_symlink():
                output.unlink()
            output.write_bytes(content)
        print('AREA04 shadow receivers:', json.dumps(report, sort_keys=True))


if __name__ == '__main__':
    main()
