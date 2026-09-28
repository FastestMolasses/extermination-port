#!/usr/bin/env python3
"""export_object_textures.py - the textures the AREA11 object units sample.

Every vertex of an object-unit model block carries a 64-bit TEX0 register
value in its qword 0; the object kernel 0x0023C750 kicks it as TEX0_1 and the
clip program 0x002354A0 kicks the entry vertex's (docs/VU1_OBJECT_KERNEL.md
section 3, docs/VU1_OBJECT_CLIP.md section 3). The GS samples the texture at
TBP0 through the CLUT at CBP. The object textures are not uploaded per draw:
they are resident in GS local memory for the whole level.

This exporter takes every TEX0 value (CLD, bits 61..63, ignored: it only
controls the CLUT cache) of every model block of
  * the AREA11 world model bank (tools/export_world_models.py build());
  * the player's model, resource 0x3B (tools/export_player_model.py
    build(): 0015C1F0's D_0028A490[0x3B], drawn by 0015C160's +0x4C);
  * the player equipment models, the ids 0018A8D0 can bind from the global
    library D_0028A56C (extract/chunk27/f01_id37.bin's table: 0x2F; 0x30,
    0x40, 0x6D; 0x31..0x3D; 0x6A; docs/PLAYER_EQUIPMENT.md section 2);
  * the item owners' library model: 00219550's 001B1020(self, +0x0D, -1, 0)
    binds id +0x0D = 0x72 of the same library for the six AREA11 items
    (docs/OWNER_DRAW.md section 10);
  * Roger's model: 008237E0's 001BA1C0 -> 001CA6E0 binds D_0028A490[0x47]
    (extract/chunk15/f18_id94.bin +0x35000, tools/export_roger_banks.py),
    and his equipment node's 001C5C90 binds id 0x6B of the library;
  * the face resources 001CB3C0's 001D3E40 REFs (docs/FACE_ATTACH.md):
    Roger's D_0028A490[0x88] (f18_id94.bin +0x86000, 008237E0's 001BA8E0)
    and Dennis's D_0028A490[0x18] (extract/chunk03/f16_id18.bin, 001B81D0's
    001CA700 on the player). A face block (0x163 qwords) is UNPACK 256
    qwords, UNPACK 96 qwords, MSCAL/MSCNT: vertex i's TEX0 is data qword
    11 i (docs/VU1_FACE_MORPH.md section 3),
all from the user's extract, and decodes
each from the GS local memory of every AREA11 route capture (beats 00..14:
../Extermination/build/s87/route/<beat>/gs.bin, the user's own PCSX2
captures). It fails unless:
  * every TEX0 has PSM PSMT8 or PSMT4, CPSM PSMCT32, CSM1, CSA 0, TCC 1 and
    TFX 2 (HIGHLIGHT): the one form the renderer reproduces. The one
    exception is an equipment model the route never binds: its other forms
    are left out and listed ("not_exported"; equipment 0x36 kicks TEX0 0 on
    four vertices of block 13), so a unit that kicks one faults;
  * the decoded texels and CLUT are identical in every capture (residency:
    the texture a draw samples does not depend on the frame).
The texels are the CLUT entries' four bytes as GS memory holds them: R, G, B
and the raw GS alpha (0x80 = 1.0), NOT rescaled.

Output (disc-derived: git-ignored assets/ only):
  assets/scene_snow/object_textures.emot, little-endian, read by
  em_owner_draw_live (src/game/em_owner_draw_live.c):
    0x00 'EMOT', u32 version 1, u32 count N, u32 0
    0x10 N entries of 24 bytes: u64 TEX0 (CLD cleared), u32 width,
         u32 height, u32 texel offset (from the file start), u32 0
    then the texels, width x height x 4 bytes each, rows top-down
  assets/scene_snow/object_textures.json: counts, TEX0 values, sizes and
  hashes (no texels).

Uses the decomp's GS memory readers (tools/gs_vram.py, clut_pair.py,
extract_textures.py), as tools/export_level.py does for the level
textures. Runs natively on arm64 macOS (pure Python).

Usage (port root):
  python3 tools/export_object_textures.py
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
sys.path.insert(0, str(DECOMP / 'tools'))

import export_player_model as epm  # noqa: E402
import export_world_models as ewm  # noqa: E402

CLD_MASK = ~(7 << 61) & (2 ** 64 - 1)
PSMT8, PSMT4 = 0x13, 0x14
BLOCK_QWORDS = 0x82
CLUT16_WORD = [0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15]


def tex0_fields(t: int) -> dict:
    return dict(tbp=t & 0x3FFF, tbw=(t >> 14) & 0x3F, psm=(t >> 20) & 0x3F, tw=(t >> 26) & 0xF,
                th=(t >> 30) & 0xF, tcc=(t >> 34) & 1, tfx=(t >> 35) & 3, cbp=(t >> 37) & 0x3FFF,
                cpsm=(t >> 51) & 0xF, csm=(t >> 55) & 1, csa=(t >> 56) & 0x1F)


# The ids 0018A8D0 binds for the player's equipment nodes (flavour 0: 0x2F;
# flavour 1: 0x30 / 0x40 / 0x6D; flavour 2: 0x32..0x36, 0x31, 0x37, 0x38,
# 0x39..0x3D; flavour 4: 0x6A), in the global library D_0028A56C.
EQUIPMENT_IDS = (0x2F, 0x30, 0x40, 0x6D, *range(0x31, 0x3E), 0x6A)
# The AREA11 item owners 00219550's model (+0x0D of every placed item), in
# the same library.
ITEM_IDS = (0x72,)
# Roger's equipment node 001C5C90 (001B1020's id 0x6B), in the same library.
ROGER_EQUIPMENT_IDS = (0x6B,)
# Roger's model and face resource in extract/chunk15/f18_id94.bin
# (tools/export_roger_banks.py MODEL_AT / FACE_AT).
ROGER_MODEL_AT, ROGER_FACE_AT = 0x35000, 0x86000
FACE_BLOCK_QWORDS = 0x163


def block_tex0(data: bytes, off: int, blocks: int, label: str, out: dict):
    """Every vertex TEX0 (CLD cleared) of the `blocks` blocks of the model at
    `off` in `data`, into {TEX0: set of labels}."""
    for b in range(blocks):
        block = off + 0x40 + 16 * BLOCK_QWORDS * b
        for i in range(32):
            t = struct.unpack_from('<Q', data, block + 16 + 64 * i)[0] & CLD_MASK
            out.setdefault(t, set()).add(label)


def face_tex0(data: bytes, off: int, label: str, out: dict):
    """Every vertex TEX0 (CLD cleared) of the face resource at `off`: its
    +0x00 word is the block count, the +0x04 low halfword the blocks' qwc
    (001D3E40's face REF); each block is UNPACK V4-32 256 to TOPS + 0,
    UNPACK V4-32 96 to TOPS + 256, MSCAL / MSCNT (checked), and vertex i's
    TEX0 is its data qword 11 i."""
    blocks, w04 = struct.unpack_from('<2I', data, off)
    if not 0 < blocks < 0x100 or (w04 & 0xFFFF) != blocks * FACE_BLOCK_QWORDS:
        raise SystemExit(f'{label}: not a face resource ({blocks} blocks, +0x04 {w04:#x})')
    for b in range(blocks):
        block = off + 0x40 + 16 * FACE_BLOCK_QWORDS * b
        if struct.unpack_from('<2I', data, block + 8) != (0x01000404, 0x6C008000) or \
                struct.unpack_from('<2I', data, block + 16 * 257 + 8) != (0x01000404, 0x6C608100):
            raise SystemExit(f'{label}: block {b} is not UNPACK 256 + UNPACK 96')
        for i in range(32):
            q = 11 * i
            t = struct.unpack_from('<Q', data, block + 16 * (q + (1 if q < 256 else 2)))[0] & CLD_MASK
            out.setdefault(t, set()).add(label)


def model_tex0(x) -> dict:
    """{TEX0 (CLD cleared): set of model labels} over every vertex of every
    block of the world model bank."""
    out, span = {}, x['span']
    for m in x['models']:
        block_tex0(span, m['offset'], m['blocks'], f"{m['id']:#x}", out)
    return out


def player_tex0(extract: Path, out: dict):
    """The player's model (resource 0x3B), the equipment models, the item
    owners' model, Roger's model and equipment, and the two face
    resources."""
    p = epm.build(extract)
    block_tex0(p['model'], 0, p['blocks'], 'player 0x3b', out)
    library = (extract / 'chunk27/f01_id37.bin').read_bytes()
    count = struct.unpack_from('<I', library, 0)[0]
    for kind, ids in (('equipment', EQUIPMENT_IDS), ('item', ITEM_IDS), ('roger equipment', ROGER_EQUIPMENT_IDS)):
        for ident in ids:
            if ident >= count:
                raise SystemExit(f'chunk27/f01_id37.bin: {kind} id {ident:#x} outside the table ({count})')
            off = struct.unpack_from('<i', library, 4 + 4 * ident)[0] >> 2 << 2
            blocks = ewm.model_record(library, off, ident)[0]
            block_tex0(library, off, blocks, f'{kind} {ident:#x}', out)
    roger = (extract / 'chunk15/f18_id94.bin').read_bytes()
    block_tex0(roger, ROGER_MODEL_AT, ewm.model_record(roger, ROGER_MODEL_AT, 0x47)[0], 'roger 0x47', out)
    face_tex0(roger, ROGER_FACE_AT, 'roger face 0x88', out)
    face_tex0((extract / 'chunk03/f16_id18.bin').read_bytes(), 0, 'dennis face 0x18', out)


def decode(lm: bytes, t: int) -> bytes:
    import clut_pair as cp
    import gs_vram
    f = tex0_fields(t)
    w, h = 1 << f['tw'], 1 << f['th']
    if f['psm'] == PSMT8:
        idx = cp.read_psmt8(lm, f['tbp'], f['tbw'], w, h)
        pal = gs_vram.csm1_unswizzle_clut(lm[f['cbp'] * 256:f['cbp'] * 256 + 1024])
    else:
        idx = cp.read_psmt4(lm, f['tbp'], f['tbw'], w, h)
        blk = lm[f['cbp'] * 256:f['cbp'] * 256 + 64]
        pal = b''.join(blk[4 * k:4 * k + 4] for k in CLUT16_WORD)
    return b''.join(pal[4 * i:4 * i + 4] for i in idx)


def default_captures():
    route = DECOMP / 'build/s87/route'
    return sorted(p for p in route.glob('*/gs.bin') if p.parent.name[:2].isdigit() and int(p.parent.name[:2]) <= 14)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--extract', type=Path, default=DECOMP / 'extract')
    ap.add_argument('--out', type=Path, default=ROOT / 'assets/scene_snow/object_textures.emot')
    ap.add_argument('--gs', type=Path, action='append', default=None,
                    help='captured GS freeze blob (repeatable); default: every AREA11 route capture')
    args = ap.parse_args(argv)
    import gs_vram
    x = ewm.build(args.extract)
    texes = model_tex0(x)
    player_tex0(args.extract, texes)
    left_out = {}
    for t in list(texes):
        f = tex0_fields(t)
        if f['psm'] not in (PSMT8, PSMT4) or f['cpsm'] or f['csm'] or f['csa'] or f['tcc'] != 1 or f['tfx'] != 2 \
                or not (0 < f['tw'] <= 10 and 0 < f['th'] <= 10):
            # Only an equipment model the route never binds may carry another
            # form (equipment 0x36, flavour 2 variant 4: four vertices of
            # block 13 with TEX0 0). It is left out: a unit that kicks it
            # faults in em_gfx_object_unit (no registered texture).
            if any(not m.startswith('equipment ') for m in texes[t]):
                raise SystemExit(f'TEX0 {t:#018x}: {f} is not the PSMT8/PSMT4 CSM1 HIGHLIGHT form')
            left_out[t] = texes.pop(t)
    captures = args.gs if args.gs is not None else default_captures()
    if not captures:
        raise SystemExit('no captured gs.bin (../Extermination/build/s87/route/<beat>/gs.bin)')
    texels = {}
    for path in captures:
        _base, lm = gs_vram.read_localmem(path)
        for t in texes:
            data = decode(lm, t)
            if texels.setdefault(t, data) != data:
                raise SystemExit(f'TEX0 {t:#018x} decodes differently in {path}: not resident')
    order = sorted(texes)
    head = struct.pack('<4s3I', b'EMOT', 1, len(order), 0)
    offset = 0x10 + 24 * len(order)
    entries, blob = b'', b''
    index = []
    for t in order:
        f = tex0_fields(t)
        w, h = 1 << f['tw'], 1 << f['th']
        entries += struct.pack('<Q4I', t, w, h, offset + len(blob), 0)
        blob += texels[t]
        index.append(dict(tex0=hex(t), width=w, height=h, psm=hex(f['psm']),
                          models=sorted(texes[t]),
                          sha256=hashlib.sha256(texels[t]).hexdigest()[:16]))
    data = head + entries + blob
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(data)
    args.out.with_suffix('.json').write_text(json.dumps(dict(
        count=len(order), bytes=len(data), captures=[str(p.parent.name) for p in captures],
        textures=index, not_exported=[dict(tex0=hex(t), models=sorted(m)) for t, m in sorted(left_out.items())]),
        indent=1) + '\n')
    print(f'wrote {args.out}: {len(order)} textures, {len(data)} bytes; identical in {len(captures)} captures'
          + ''.join(f'; not exported: TEX0 {t:#x} ({", ".join(sorted(m))})' for t, m in sorted(left_out.items())))
    return 0


if __name__ == '__main__':
    sys.exit(main())
