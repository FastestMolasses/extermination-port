#!/usr/bin/env python3
"""Export the MAP page's models (docs/STATUS_PAGES.md section 7) from the
user's own disc, into ignored assets/status_map/.

0020F950 (the status screen's page 1, MAP) spawns 22 UI-pool nodes running
002101C0; a node whose map byte D_00810CB8[p[3]] is set binds
001C6120(D_0028A570, the word at 0x2658C0 + p[3] * 8 + 4 * p[0xD]) through
001CA5E0(p, model, 7) and draws it with the method 001CB480. D_0028A570 is
D_0028A490[0x38]: resource slot 0x38 of module 0x1E, which 0020CDC0 phase 3
loads for page 1 (001FF830(0x1E): its one A section is the page's texture
upload; its resident region holds only slot 0x38, the map model bank).

The bank is read from the disc (INDEX.IDX sector 0x1E, the descriptor's
slot table; tools/export_disc_textures_gs.py) and checked against the
user's extract (chunk30). Its directory holds 22 models (codes 0..0x15),
each a single node. Texels come from the GS memory the original holds while
the page is open: the first level's world image (DISC_TEXTURES.md) with
module 0x1E's upload applied; every TEX0 a model names must decode from
blocks an upload wrote.

Writes (ignored, disc-derived):
  assets/status_map/map_XX.emdl     model XX of the bank (mesh + texels)
  assets/status_map/map_models.emmp every model's code, its directory
                                    offset (the address 001C6120 returns is
                                    D_0028A570 + this offset), node count
                                    (model +8, 001C6150), radius (model
                                    +0x20, 001D8270's gate) and skeleton
                                    records (parent, bind; 001C62C0)
The receipt build/status_map_export/export.json holds hashes and counts.

Runs natively on arm64 macOS (pure Python, a few seconds).

  python3 tools/export_status_map.py [--iso FILE | --disc DIR] [--extract DIR] [--assets DIR]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import export_disc_textures_gs as G  # noqa: E402

MODULE, SLOT = 0x1E, 0x38            # D_0028A570 = D_0028A490[0x38]
TABLE = 0x002658C0                   # 002101C0's code table: 11 maps x 2 words
MAPS = 11
ELF_BASE, ELF_OFFSET = 0x00100000, 0x300


def u32(b, a):
    return struct.unpack_from('<I', b, a)[0]


def build(disc: G.Disc, extract: Path, assets: Path, scratch: Path) -> dict:
    sys.path.insert(0, str(G.DECOMP / 'tools'))
    import export_native as en
    import export_props as ep
    import export_object_textures as eot

    elf = G.ELF_PATH.read_bytes()
    if hashlib.sha256(elf).hexdigest() != G.ELF_SHA256:
        raise SystemExit(f'{G.ELF_PATH}: not the pinned SCUS_971.12')
    codes = [u32(elf, TABLE - ELF_BASE + ELF_OFFSET + 4 * i) for i in range(2 * MAPS)]

    fl = G.FirstLevel(disc, extract)
    block = G.top_block(disc, MODULE)
    table = G.module_table_index(block, 'module')
    label, region, size = G.module_slot(disc, block, SLOT, table)
    bank = fl._bytes('001FF830(0x1e) state 7: slot 0x38', block, f'chunk{MODULE:02d}', label,
                     region, size)
    count = u32(bank, 0)
    directory = ep.read_directory(bank)
    assert len(directory) == count == 2 * MAPS, ('bank directory', count)
    # 001C6120 masks the code to 15 bits; every table code names an entry.
    for code in codes:
        assert (code & 0x7FFF) < count, ('a table code outside the directory', hex(code))

    world = fl.world()
    page = fl.page(world, MODULE)
    scratch.mkdir(parents=True, exist_ok=True)
    freeze = scratch / 'gs_module_1e.bin'
    freeze.write_bytes(page.freeze())

    out = assets / 'status_map'
    out.mkdir(parents=True, exist_ok=True)
    emmp = bytearray(struct.pack('<4sII', b'EMMP', 1, count))
    report = {'bank': dict(source=label, data_dat_offset=hex(block.offset + region), size=hex(size)),
              'table_codes': [hex(c) for c in codes], 'models': []}
    for code in range(count):
        offset = directory[code]
        model = bank[offset:]
        nodes = model[8]
        skeleton = u32(model, 0xC)
        radius = u32(model, 0x20)
        assert nodes == 1, ('map model with more than one node', code, nodes)
        records = []
        for i in range(nodes):
            rec = skeleton + 0x50 * i
            parent = struct.unpack_from('<h', model, rec + 4)[0]
            records.append((parent, model[rec + 0x10:rec + 0x50]))
        sections, tex_table, _n = ep.build_placed_mesh(bank, {code: [ep.lvl.IDENT34]})
        assert tex_table, ('map model without a texture', code)
        for entry in tex_table:
            key = entry['key']
            if not G.reads_only_covered(page, lambda lm, t=key: eot.decode(lm, t)):
                raise SystemExit(f'map model {code:#x}: TEX0 {key:#x} reads GS blocks no upload wrote')
        tex_entries, tex_blob = en.build_texture_blob(None, tex_table, freeze)
        path = out / f'map_{code:02x}.emdl'
        en.write_emdl(path, sections, [], [-1], [[en.mat_identity()]], 30.0, tex_entries, tex_blob,
                      flags=ep.NORMAL_FLAGS)
        emmp += struct.pack('<IIII', code, offset, nodes, radius)
        for parent, bind in records:
            emmp += struct.pack('<i', parent) + bind
        report['models'].append({'code': code, 'offset': offset, 'nodes': nodes,
                                 'textures': len(tex_entries),
                                 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
    path = out / 'map_models.emmp'
    path.write_bytes(bytes(emmp))
    report['emmp_sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
    report['buffers'] = [dict(caller=c, source=l, data_dat_offset=hex(o), size=hex(s), extract=w)
                         for c, l, o, s, w in fl.sources]
    freeze.unlink()
    (scratch / 'export.json').write_text(json.dumps(report, indent=1) + '\n')
    return report


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--iso', type=Path, help='the disc image (default ../Extermination/Extermination-rebuilt.iso)')
    ap.add_argument('--disc', type=Path, help='a mounted disc or a copy of its DATA/ directory')
    ap.add_argument('--extract', type=Path, default=G.EXTRACT)
    ap.add_argument('--assets', type=Path, default=ROOT / 'assets')
    ap.add_argument('--scratch', type=Path, default=ROOT / 'build/status_map_export')
    args = ap.parse_args(argv)
    report = build(G.Disc(args.iso, args.disc), args.extract, args.assets, args.scratch)
    print(f'status map: {len(report["models"])} models -> {args.assets / "status_map"}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
