#!/usr/bin/env python3
"""World textures, shadow receivers and player texture packets of AREA03 subs
1 and 2 and AREA04 sub 1 (export_area03x_common.py): the descriptor-driven
tools export_level_world_textures, export_level_shadow_receivers and
export_player_texture_variants, unchanged, into build/area03x/assets/tree/.
One process per profile (level_asset_profiles.configure).

Usage (port root, macOS arm64, pure Python):
  python3 tools/export_area03x_textures.py --profile 03x|04x [--iso PATH]
"""
import argparse
import json

import level_asset_profiles as P
import export_level_world_textures as LW
import export_level_shadow_receivers as LS
import export_player_texture_variants as PV


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--profile', choices=('03x', '04x'), required=True)
    ap.add_argument('--iso')
    args = ap.parse_args()
    for sub in P.SUBS[args.profile]:
        r = LW.export(args.profile, sub, args.iso)
        print(f'{args.profile}/{sub}: {r["fixed_textures"]} fixed textures, {r["unresolved_keys"]} unresolved, '
              f'{len(r["variants"])} variants, {r["capture_count"]} GS freezes', flush=True)
        data, report = LS.build(args.profile, sub)
        out = P.output(args.profile, sub)/'shadow_receivers.emsr'
        out.write_bytes(data)
        out.with_suffix('.json').write_text(json.dumps(report, indent=2)+'\n')
        print(f'{args.profile}/{sub}: {report["objects"]} receivers, {len(data)} bytes', flush=True)
        for row in PV.export(args.profile, sub, args.iso):
            print(f'{args.profile}/{sub}: {row["file"]}: {row["textures"]} textures', flush=True)


if __name__ == '__main__':
    main()
