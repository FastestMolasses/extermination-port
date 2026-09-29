#!/usr/bin/env python3
"""export_player_model.py - the player's model (D_0028A490[0x3B]).

The player record D_008102B0 binds its model in 0015C1F0: the kind +0x2FF
(0x3B for the AREA11 player: infection mode +0x234 = 0 and D_00810C60 = 0)
indexes the resource table D_0028A490, and 001CA6E0(player, D_0028A490[kind])
stores that address at +0x44 with the draw method +0x4C = 001CAA00
(001CA5F0 kind 0); +0x0C = 001C6150(+0x44), the byte at model +0x08. The
post-step 0015C160 calls +0x4C, which draws the model with the object kernel
exactly as the world owners' 001CAA00 does (docs/OWNER_DRAW.md): the header
(+0x04 block qwords, +0x08 bones, +0x0C skeleton offset, +0x20 radius), the
skeleton records (0x50 bytes each) and the block data REF'd at +0x40.

Source (the user's own extracted disc; nothing is embedded here): the
resource file extract/chunk28/f00_id3b.bin (resource 0x3B), whose first
bytes are the block model: 149 blocks, 21 bones, 0x4C170 bytes in all.

Checks (every run):
  * the model's header and every block's VIF codes (export_world_models.py
    model_record: STCYCL 4,4, UNPACK V4-32 128, MSCAL / MSCNT);
  * the address equals D_0028A490[0x3B] of the resource table rebuilt from
    the user's disc (tools/export_disc_textures_gs.py ResourceTable);
per captured EE RAM image, when the developer has them (--verify-ram,
repeatable; default playable_ee.bin and every AREA11 route capture 00..14):
  * D_0028A490[0x3B] == the exported address (0x00D1C1C0);
  * the exported bytes equal RAM at that address, byte for byte;
  * the player record: +0x2FF == 0x3B, +0x44 == the address, +0x4C ==
    001CAA00, +0x0C == the model's bone count, +0x09 == +0x0C and every
    +0x110 slot below it set.

Output (disc-derived: git-ignored assets/ only):
  assets/scene_snow/player_model.emom, little-endian, read by
  em_object_model_parse (src/game/em_owner_draw_original.c):
    0x00 'EMOM', u32 version 1, u32 address, u32 size S, 4 x u32 0
    0x20 S original bytes (the model: header, blocks, skeleton records)
  assets/scene_snow/player_model.json: address, blocks, bones, bytes,
  radius bits, SHA-256 and the verified captures.

Runs natively on arm64 macOS (pure Python).

Usage (port root):
  python3 tools/export_player_model.py
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

import export_world_models as ewm

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
RESOURCE_TABLE, KIND = 0x0028A490, 0x3B     # D_0028A490[+0x2FF], 0015C1F0
ADDRESS = 0x00D1C1C0                         # D_0028A490[0x3B]: checked against the disc ResourceTable
PLAYER = 0x008102B0
DRAW = 0x001CAA00


def u32(b, a): return struct.unpack_from('<I', b, a)[0]


def build(extract: Path):
    data = (extract / 'chunk28/f00_id3b.bin').read_bytes()
    blocks, qwc, bones, skel, size, radius = ewm.model_record(data, 0, KIND)
    if size > len(data):
        raise SystemExit(f'chunk28/f00_id3b.bin: the model needs {size:#x} bytes, the file has {len(data):#x}')
    return dict(blocks=blocks, qwc=qwc, bones=bones, skeleton=skel, bytes=size, radius_bits=radius,
                model=data[:size])


def verify_ram(x, ram: bytes, address: int):
    if u32(ram, RESOURCE_TABLE + 4 * KIND) != address:
        raise SystemExit(f'D_0028A490[{KIND:#x}] = {u32(ram, RESOURCE_TABLE + 4 * KIND):#x}, not {address:#x}')
    model = x['model']
    if ram[address:address + len(model)] != model:
        diff = next(i for i in range(len(model)) if ram[address + i] != model[i])
        raise SystemExit(f'RAM differs from the exported model at +{diff:#x}')
    p = PLAYER
    if ram[p + 0x2FF] != KIND or u32(ram, p + 0x44) != address or u32(ram, p + 0x4C) != DRAW:
        raise SystemExit(f'player record: +0x2FF {ram[p + 0x2FF]:#x}, +0x44 {u32(ram, p + 0x44):#x}, '
                         f'+0x4C {u32(ram, p + 0x4C):#x}')
    if ram[p + 0x0C] != x['bones'] or ram[p + 0x09] != x['bones'] or \
            not all(u32(ram, p + 0x110 + 4 * k) for k in range(x['bones'])):
        raise SystemExit('player record: +0x0C / +0x09 / the +0x110 slots differ from the model')


def serialize(x, address: int) -> bytes:
    return struct.pack('<4s7I', b'EMOM', 1, address, len(x['model']), 0, 0, 0, 0) + x['model']


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--extract', type=Path, default=DECOMP / 'extract')
    ap.add_argument('--out', type=Path, default=ROOT / 'assets/scene_snow/player_model.emom')
    ap.add_argument('--address', type=lambda v: int(v, 0), default=ADDRESS)
    ap.add_argument('--no-verify', action='store_true')
    ap.add_argument('--iso', type=Path, help='the disc image (default ../Extermination/Extermination-rebuilt.iso)')
    ap.add_argument('--disc', type=Path, help='a mounted disc or a copy of its DATA/ directory')
    ap.add_argument('--verify-ram', type=Path, action='append', default=None,
                    help='captured EE RAM image (repeatable); default: every AREA11 route capture')
    args = ap.parse_args(argv)
    x = build(args.extract)
    # The address is D_0028A490[0x3B] as the loaders leave it: module 3's
    # resource slot 0x3B, rebuilt from the user's disc (tools/
    # export_disc_textures_gs.py ResourceTable); the captures, when present,
    # are checked too.
    sys.path.insert(0, str(ROOT / 'tools'))
    import export_disc_textures_gs as G
    table = G.ResourceTable(G.Disc(args.iso, args.disc))
    if table.words[KIND] != args.address:
        raise SystemExit(f'D_0028A490[{KIND:#x}] from the disc is {table.words[KIND]:#x}, not {args.address:#x}')
    captures = [] if args.no_verify else (args.verify_ram if args.verify_ram is not None else ewm.default_captures())
    verified = []
    for path in captures:
        verify_ram(x, path.read_bytes(), args.address)
        verified.append(str(path.relative_to(DECOMP) if path.is_relative_to(DECOMP) else path))
    data = serialize(x, args.address)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(data)
    args.out.with_suffix('.json').write_text(json.dumps(dict(
        address=hex(args.address), kind=hex(KIND), blocks=x['blocks'], bones=x['bones'], bytes=x['bytes'],
        radius_bits=hex(x['radius_bits']), sha256=hashlib.sha256(x['model']).hexdigest(),
        verified=verified), indent=1) + '\n')
    print(f"wrote {args.out}: model {KIND:#x} at {args.address:#x}, {x['blocks']} blocks, {x['bones']} bones, "
          f"{x['bytes']} bytes; equal to RAM in {len(verified)} captures")
    return 0


if __name__ == '__main__':
    sys.exit(main())
