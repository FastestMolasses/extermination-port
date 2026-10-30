#!/usr/bin/env python3
"""export_player_model_variants.py - the player's other models, kinds 0x3D,
0x3E, 0x3F and 0x40 (D_0028A490[kind]), beside export_player_model.py's 0x3B.

0015C1F0 picks the player's model kind +0x2FF from the infection mode
+0x234 and D_00810C60: mode 0 -> 0x3B (status 1 -> 0x3E, 2 -> 0x3F), mode 1
-> 0x40 (status 1 -> 0x3E, 2 -> 0x3F); other modes 0x3D (0021E830 sets mode
2 when the infection turns the player). 001CA6E0 binds
D_0028A490[kind] at +0x44, which 001CAA00 draws as it draws 0x3B. The
reaction's infection (+0x228 reaching 100: 0021C190's 0015C1F0) binds 0x40;
em_player_draw_live holds the variants beside 0x3B so that bind can draw.

Source (the user's own extracted disc; nothing is embedded here): module 3's
resident files extract/chunk28/f02_id3d.bin, f03_id3e.bin, f04_id3f.bin,
f05_id40.bin,
each a block model at +0 (export_world_models.model_record checks the header
and every block's VIF codes).

Checks (every run):
  * each model's address equals D_0028A490[kind] of the resource table
    rebuilt from the user's disc (export_disc_textures_gs.ResourceTable);
  * reference check against the fork recordings: in every snapshot of the
    middle route's recordings (route_a22, route_a01u, route_a06, route_a06b,
    route_a01v, route_a22b) D_0028A490[kind] is that address and RAM holds
    the exported bytes there (module 3 keeps them resident).

Output (disc-derived: git-ignored assets/ only):
  assets/scene_snow/player_model_<kk>.emom (EMOM v1, as player_model.emom)
  assets/scene_snow/player_model_variants.json (addresses, sizes, SHA-256s,
  the snapshots checked)

Usage (port root, native arm64 macOS, pure Python):
  python3 tools/export_player_model_variants.py
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

import export_world_models as ewm
import export_player_model as epm
import reference_root as refs

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
KINDS = {0x3D: 'chunk28/f02_id3d.bin', 0x3E: 'chunk28/f03_id3e.bin', 0x3F: 'chunk28/f04_id3f.bin', 0x40: 'chunk28/f05_id40.bin'}
ROUTES = ('s87/route_a22', 's87/route_a01u', 's87/route_a06', 's87/route_a06b', 's87/route_a01v', 's87/route_a22b')


def build(extract: Path, kind: int) -> bytes:
    data = (extract / KINDS[kind]).read_bytes()
    size = ewm.model_record(data, 0, kind)[4]
    if size > len(data):
        raise SystemExit(f'{KINDS[kind]}: the model needs {size:#x} bytes, the file has {len(data):#x}')
    return data[:size]


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--extract', type=Path, default=DECOMP / 'extract')
    ap.add_argument('--out', type=Path, default=ROOT / 'assets/scene_snow')
    ap.add_argument('--iso', type=Path, help='the disc image (default ../Extermination/Extermination-rebuilt.iso)')
    args = ap.parse_args(argv)
    sys.path.insert(0, str(ROOT / 'tools'))
    import export_disc_textures_gs as G
    table = G.ResourceTable(G.Disc(args.iso, None))
    snapshots = [p for r in ROUTES for p in sorted(refs.capture(r).glob('*/eeMemory.bin'))]
    if not snapshots:
        raise SystemExit('no fork recordings of the middle route (reference check)')
    rows = {}
    for kind in KINDS:
        model = build(args.extract, kind)
        address = table.words[kind]
        for path in snapshots:
            ram = path.read_bytes()
            got = epm.u32(ram, epm.RESOURCE_TABLE + 4 * kind)
            if got != address:
                raise SystemExit(f'{path.parent.name}: D_0028A490[{kind:#x}] = {got:#x}, the disc says {address:#x}')
            if ram[address:address + len(model)] != model:
                raise SystemExit(f'{path.parent.name}: RAM at {address:#x} differs from model {kind:#x}')
        out = args.out / f'player_model_{kind:02x}.emom'
        if out.is_symlink():
            raise SystemExit(f'{out}: a symlink (write a real file)')
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(struct.pack('<4s7I', b'EMOM', 1, address, len(model), 0, 0, 0, 0) + model)
        rows[f'{kind:#x}'] = dict(address=hex(address), bytes=len(model),
                                  sha256=hashlib.sha256(model).hexdigest())
        print(f'wrote {out.relative_to(ROOT)}: model {kind:#x} at {address:#x}, {len(model)} bytes; '
              f'equal to RAM in {len(snapshots)} snapshots')
    (args.out / 'player_model_variants.json').write_text(json.dumps(dict(
        models=rows, snapshots=[str(p.relative_to(DECOMP)) for p in snapshots]), indent=1) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
