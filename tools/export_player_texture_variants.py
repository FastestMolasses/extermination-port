#!/usr/bin/env python3
"""export_player_texture_variants.py - a recorded area's world texture
catalogs for the player's other texture packets (the status / infection
variants), beside export_level_world_textures.py's recorded pairs.

00200890 uploads the player texture packet D_0028A490[slot] chosen by
D_00810707 (mode) and D_00810C60 (costume): (0, 0) slot 8 (model 0x3B),
(0, 1) / (1, 1) slot 0xB (0x3E), (0, 2) / (1, 2) slot 0xA (0x3F), (1, 0)
slot 0xC (0x40), any mode above 1 slot 9 (0x3D, one catalog (2, 0)) (export_disc_textures_gs.player_texture_slot; 0015C1F0's
kinds). It runs at an area load and again when 0015C1F0 rebinds the model
(the reaction's infection at +0x228 = 100). The new packet replaces the
player's texture blocks in GS memory; every other block keeps the area's.

For each pair the catalog is the GS memory the disc builds for that area
with that packet (export_disc_textures_gs.FirstLevel.world: the same
replay the recorded pairs use), decoded for the recorded catalog's keys and
the pair's player model keys (tools/export_player_model_variants.py's
models). Checks:
  * the (0, 0) decode of the recorded keys equals the installed
    world_textures_mode0_costume0.emot byte for byte (the same method,
    which export_level_world_textures cross-checks against every capture);
  * every recorded key a pair's packet does not reach decodes to the
    (0, 0) texels: the packet changes the player's blocks only;
  * every key reads only blocks the disc uploads write (disc_texels);
  * reference check: every fork recording of the area (FORK_ROUTES) whose
    end holds one of these pairs (the a06_05 recording: the player turned,
    (1, 0)) decodes every key of that pair's catalog from its GS memory
    (gs.bin) to the same texels as the disc replay (cross_check).

Output (disc-derived, git-ignored): assets/levels/<profile>/sub<n>/
world_textures_mode<m>_costume<c>.emot for the five pairs, and
player_texture_variants.json (sizes, SHA-256s, the checks).

Usage (port root): python3 tools/export_player_texture_variants.py --profile 06
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

import level_asset_profiles as P
import reference_root as refs
import export_level_world_textures as LW
import export_world_models as ewm

PAIRS = {(0, 1): 0x3E, (0, 2): 0x3F, (1, 0): 0x40, (1, 1): 0x3E, (1, 2): 0x3F, (2, 0): 0x3D}
FORK_ROUTES = {'06': ('s87/route_a06', 's87/route_a06b'), '22': ('s87/route_a22', 's87/route_a22b', 's87/route_a01v'),
               '01': ('s87/route_a01', 's87/route_a01u', 's87/route_a01v'), '04': ('s87/route_a04',),
               '00': ('s87/route_a00',), '02': ('s87/route_a02',)}
MODEL_FILES = {0x3D: 'chunk28/f02_id3d.bin', 0x3E: 'chunk28/f03_id3e.bin', 0x3F: 'chunk28/f04_id3f.bin', 0x40: 'chunk28/f05_id40.bin'}


def read_emot(data):
    magic, version, count, _ = struct.unpack_from('<4s3I', data)
    assert magic == b'EMOT' and version == 1
    out = {}
    for i in range(count):
        t, w, h, off, _ = struct.unpack_from('<Q4I', data, 0x10 + 24 * i)
        out[t] = data[off:off + 4 * w * h]
    return out


def export(profile, sub, iso=None):
    C = P.configure(profile, sub)
    import export_area01_world_textures as E
    caps = C.captures()
    keys, _inventory = LW.collect(C, caps)
    fixed = {k for k in keys if k != 0}
    out = P.output(profile, sub)
    base_file = out / 'world_textures_mode0_costume0.emot'
    base = read_emot(base_file.read_bytes())
    loader = E.G.FirstLevel(E.G.Disc(iso), C.EXTRACT)
    if E.O.disc_texels(loader.world(C.AREA, sub, 0, 0), fixed) != {k: base[k] for k in fixed} or set(base) != fixed:
        raise SystemExit(f'{profile}/{sub}: the (0, 0) decode differs from {base_file.name}')
    rows = []
    recorded = {}
    for route in FORK_ROUTES[profile]:
        for folder in sorted(refs.capture(route).glob('*/eeMemory.bin')):
            ram = folder.read_bytes()
            if (ram[0x810700], ram[0x810701]) == (C.AREA, sub) and (ram[0x810707], ram[0x810C60]) in PAIRS:
                recorded.setdefault((ram[0x810707], ram[0x810C60]), []).append(folder.parent / 'gs.bin')
    for (mode, costume), kind in PAIRS.items():
        model = (C.EXTRACT / MODEL_FILES[kind]).read_bytes()
        mkeys = {}
        E.O.block_tex0(model, 0, ewm.model_record(model, 0, kind)[0], f'player {kind:#x}', mkeys)
        texels = E.O.disc_texels(loader.world(C.AREA, sub, mode, costume), fixed | set(mkeys))
        changed = sorted(k for k in fixed if texels[k] != base[k])
        # the packet writes the player's blocks only: a recorded key it
        # changes is a 0x3B player key (the same TBP, the new packet's texels)
        if any(not any(lbl.startswith('player') for lbl in keys[k]) for k in changed):
            raise SystemExit(f'{profile}/{sub}: pair {mode, costume} changes a non-player texture')
        E.O.cross_check(texels, recorded.get((mode, costume), []))
        data = E.O.emot(texels)
        path = out / f'world_textures_mode{mode}_costume{costume}.emot'
        if path.is_symlink():
            raise SystemExit(f'{path}: a symlink (write a real file)')
        path.write_bytes(data)
        rows.append(dict(mode=mode, costume=costume, model=hex(kind), file=path.name, textures=len(texels),
                         player_keys_added=len(set(mkeys) - fixed), changed_player_keys=len(changed),
                         cross_checked=[str(g.parent.name) for g in recorded.get((mode, costume), [])],
                         bytes=len(data), sha256=hashlib.sha256(data).hexdigest()))
    (out / 'player_texture_variants.json').write_text(json.dumps(dict(
        profile=profile, sub=sub, base=base_file.name, variants=rows,
        limitation='pairs without a cross_checked recording: the texels are the disc replay of 00200890 only'),
        indent=1) + '\n')
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--profile', choices=P.SUBS, required=True)
    ap.add_argument('--iso')
    args = ap.parse_args()
    # the outputs' own folders must be private (a worktree may link the
    # other recorded files in; each written file is refused as a symlink)
    for sub in P.SUBS[args.profile]:
        out = P.output(args.profile, sub)
        if any(p.is_symlink() for p in (out, *out.parents)):
            raise SystemExit(f'{out}: a symlinked folder (write private files)')
    for sub in P.SUBS[args.profile]:
        for r in export(args.profile, sub, args.iso):
            print(f'{args.profile}/{sub}: {r["file"]}: {r["textures"]} textures ({r["player_keys_added"]} '
                  f'player keys added, {r["changed_player_keys"]} 0x3B keys changed)', flush=True)


if __name__ == '__main__':
    main()
