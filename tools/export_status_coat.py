#!/usr/bin/env python3
"""Export the status hub's menu player in the coat (lane M1; AREA13's later beats).

0020E6F0 binds D_0028A588 when D_00810C60 is 1 (em_status_scene_original.c,
player_init): resource slot 0x3E, extract/chunk28/f03_id3e.bin. AREA13's coat
sets that byte, so every status page opened in AREA13 after [4]'s scene (the
recharger's and the battery machine's pages, the HEALING page) spawns this model.

The fork recordings hold the slot: its RAM must equal the file. Texels: the GS
memory of a fork snapshot taken with the coat on (a13c_01's end, outdoors, the player's
costume-1 packet resident, 00200890), the same decode tools/export_status_models.py
runs. The clips are the bank's (D_0028A580 is shared), so no new clip file.

Writes (ignored, disc-derived):
  assets/status_models/menu_player_coat.emdl  mesh + texels, 21 nodes
  assets/status_models/menu_player_coat.emsk  model byte +8 and skeleton records

Usage (port root; native arm64 macOS; about 2 s): python3 tools/export_status_coat.py
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(DECOMP / 'tools'))
import export_status_models as esm  # noqa: E402
import reference_root as refs  # noqa: E402

FILE = DECOMP / 'extract/chunk28/f03_id3e.bin'
CAPTURE = 'levels-recordings-20261018-11/s87/route_a13c/a13c_01_to_machine'
SLOT_WORD = 0x28A588
OUT = ROOT / 'assets/status_models'


def main():
    import export_native as en
    capture = refs.capture(CAPTURE)
    ram = (capture / 'eeMemory.bin').read_bytes()
    assert ram[0x810C60] == 1, 'the capture is not in the coat'
    model = FILE.read_bytes()
    at = struct.unpack_from('<I', ram, SLOT_WORD)[0]
    assert ram[at:at + len(model)] == model, 'D_0028A588 is not f03_id3e'
    sections, max_slot, tex_table = en.load_mesh_sections(FILE)
    count, nodes = esm.skeleton(model)
    assert count == 21 and max_slot < count, count
    tex_entries, tex_blob = en.build_texture_blob(None, tex_table, capture / 'gs.bin')
    parents = [p for p, _ in nodes]
    emdl, emsk = OUT / 'menu_player_coat.emdl', OUT / 'menu_player_coat.emsk'
    for f in (emdl, emsk):
        if f.is_symlink():
            raise SystemExit(f'{f} is a symlink: unlink it first')
    en.write_emdl(emdl, sections, [], parents, [[en.mat_identity()] * count], 30.0, tex_entries, tex_blob, flags=0)
    blob = struct.pack('<4sII', b'EMSK', 1, 1) + struct.pack('<II', SLOT_WORD, count)
    for parent, bind in nodes:
        blob += struct.pack('<i', parent) + bind
    emsk.write_bytes(blob)
    print(f'status coat: {count} nodes, {len(tex_entries)} textures -> {emdl.relative_to(ROOT)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
