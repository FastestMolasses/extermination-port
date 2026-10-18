#!/usr/bin/env python3
"""Export a recorded area's complete shadow grid and receiver geometry.

Uses the existing EMSR serializer, disc load maps and full byte comparisons
with every snapshot in the selected profile. No geometry comes from RAM.
"""
import argparse
import hashlib
import json

from level_asset_audit import private_assets
import level_asset_profiles as P


def build(profile, sub):
    common = P.configure(profile, sub)
    import export_area01_shadow_receivers as E
    data, report = E.build()
    report.update(profile=profile, area=common.AREA, sub=sub,
                  sha256=hashlib.sha256(data).hexdigest(),
                  capture_paths=[str(c.path.relative_to(common.DECOMP)) for c in common.captures()],
                  scope='exact disc/capture data; native drawing remains unbound')
    return data, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=P.SUBS, required=True)
    args = parser.parse_args()
    private_assets(P.ROOT)
    for sub in P.SUBS[args.profile]:
        data, report = build(args.profile, sub)
        out = P.output(args.profile, sub)/'shadow_receivers.emsr'
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(data)
        out.with_suffix('.json').write_text(json.dumps(report, indent=2)+'\n')
        print(f'{args.profile}/{sub}: PASS {report["objects"]} receivers, '
              f'{report["captures"]} captures, {len(data)} bytes', flush=True)


if __name__ == '__main__':
    main()
