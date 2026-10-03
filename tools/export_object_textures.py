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
  * the shot's library models: the muzzle node 001F5040's 0x07, 0x08,
    0x0B, 0x0D, 0x0E, 0x0F and the shell casing's 0x19 (001F3E30);
  * a box's damage break (001551B0): its husk 0x22 and the debris pieces
    0x1C / 0x1E (001F2BA0 / 001F3E30), the same library;
  * Roger's model: 008237E0's 001BA1C0 -> 001CA6E0 binds D_0028A490[0x47]
    (extract/chunk15/f18_id94.bin +0x35000, tools/export_roger_banks.py),
    and his equipment node's 001C5C90 binds id 0x6B of the library;
  * the face resources 001CB3C0's 001D3E40 REFs (docs/FACE_ATTACH.md):
    Roger's D_0028A490[0x88] (f18_id94.bin +0x86000, 008237E0's 001BA8E0)
    and Dennis's D_0028A490[0x18] (extract/chunk03/f16_id18.bin, 001B81D0's
    001CA700 on the player). A face block (0x163 qwords) is UNPACK 256
    qwords, UNPACK 96 qwords, MSCAL/MSCNT: vertex i's TEX0 is data qword
    11 i (docs/VU1_FACE_MORPH.md section 3);
  * the static-object bank *D_0028A5A0 (tools/export_static_world.py: the
    chunk15 concatenation from 0x304000): every block of its 701 objects,
    which the static world's channel-0 run REFs and the level kernel
    0x00237180 kicks (docs/STATIC_WORLD.md section 7),
all from the user's extract, and decodes
each from the first level's GS local memory rebuilt from the user's own
disc with the original's upload sequence (tools/export_disc_textures_gs.py
FirstLevel.world(): module 0x1B's library sheet, 001AD1A0's re-upload,
the AREA11 load and the player texture packet; docs/DISC_TEXTURES.md).
No PCSX2 capture is needed. It fails unless:
  * every TEX0 has PSM PSMT8 or PSMT4, CPSM PSMCT32, CSM1, CSA 0, TCC 1 and
    TFX 2 (HIGHLIGHT), or, for the static-object bank's blocks only, TFX 0
    (MODULATE): the forms the renderer reproduces. The one exception is an
    equipment model the route never binds: its other forms are left out and
    listed ("not_exported"; equipment 0x36 kicks TEX0 0 on four vertices of
    block 13), so a unit that kicks one faults;
  * every decode reads only GS blocks a disc upload of the route writes
    (residency: the decode must not change when every other block is
    filled with 0x00 or with 0xA5).
With --gs FILE (repeatable) or --route-captures, each texture must also
decode identically from those captured GS freezes (an optional cross-check
for developers who have the captures; test_disc_textures_reference.py
compares the disc decodes with the route captures).
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
  python3 tools/export_object_textures.py [--iso FILE | --disc DIR] [--route-captures]
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
import export_static_world as esw  # noqa: E402
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
# The muzzle node 001F5040 (0x07, 0x08, 0x0B, 0x0D, 0x0E, 0x0F: its +0x0D's
# model and the clips 7 / 8 it switches to) and the shell casing's 001F3E30
# (0x19: D_0025A350 row 3), in the same library (docs/AIM_FIRE.md sections 9.1 / 9.2).
SHOT_IDS = (0x07, 0x08, 0x0B, 0x0D, 0x0E, 0x0F, 0x19)
# A model-6 box's damage break (001551B0; BRANCH br_04 / br_06): the husk
# 0x22 its 001CA6E0 rebinds and the debris pieces 0x1C / 0x1E (effect
# 0x8000000A's 001F2BA0, D_0025A350 row 0, drawn by 001F3E30), in the same
# library.
BREAK_IDS = (0x1C, 0x1E, 0x22)
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
    for kind, ids in (('equipment', EQUIPMENT_IDS), ('item', ITEM_IDS), ('roger equipment', ROGER_EQUIPMENT_IDS),
                      ('shot', SHOT_IDS), ('break', BREAK_IDS)):
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


STATIC_LABEL = 'static bank'


def static_tex0(extract: Path, out: dict):
    """Every vertex TEX0 (CLD cleared) of every block of the static-object
    bank's objects (entries 1..n-1: the block count at +0, the blocks of
    0x820 bytes from +0x40, each STCYCL 4,4 + UNPACK V4-32 128 to TOPS,
    32 vertices of 4 qwords, MSCAL / MSCNT: checked)."""
    cat = esw.concatenation(extract / 'chunk15')
    base = esw.CONCAT_OFFSET
    span, entries = esw.bank_extent(cat, base)
    bank = cat[base:base + span]
    blocks = 0
    for i, (off, _end) in enumerate(entries[1:], start=1):
        count = struct.unpack_from('<I', bank, off)[0]
        for b in range(count):
            block = off + 0x40 + esw.BLOCK_BYTES * b
            if struct.unpack_from('<2I', bank, block + 8) != (0x01000404, 0x6C808000) or \
                    (bank[block + 0x813] not in (0x14, 0x17)):
                raise SystemExit(f'static bank object {i} block {b}: not STCYCL 4,4 + UNPACK 128 + MSCAL / MSCNT')
            for v in range(32):
                t = struct.unpack_from('<Q', bank, block + 16 + 64 * v)[0] & CLD_MASK
                out.setdefault(t, set()).add(STATIC_LABEL)
            blocks += 1
    return blocks


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
    """The AREA11 route captures' GS freezes (optional cross-checks only)."""
    route = DECOMP / 'build/s87/route'
    return sorted(p for p in route.glob('*/gs.bin') if p.parent.name[:2].isdigit() and int(p.parent.name[:2]) <= 14)


def tex0_set(extract: Path):
    """({TEX0: model labels} the object units sample, {TEX0: labels} left
    out, the static bank's block count): every vertex TEX0 of the models
    listed in the module docstring, with the form checks."""
    x = ewm.build(extract)
    texes = model_tex0(x)
    player_tex0(extract, texes)
    static_blocks = static_tex0(extract, texes)
    left_out = {}
    for t in list(texes):
        f = tex0_fields(t)
        tfx_ok = f['tfx'] == 2 or (f['tfx'] == 0 and texes[t] == {STATIC_LABEL})
        if f['psm'] not in (PSMT8, PSMT4) or f['cpsm'] or f['csm'] or f['csa'] or f['tcc'] != 1 or not tfx_ok \
                or not (0 < f['tw'] <= 10 and 0 < f['th'] <= 10):
            # Only an equipment model the route never binds may carry another
            # form (equipment 0x36, flavour 2 variant 4: four vertices of
            # block 13 with TEX0 0). It is left out: a unit that kicks it
            # faults in em_gfx_object_unit (no registered texture).
            if any(not m.startswith('equipment ') for m in texes[t]):
                raise SystemExit(f'TEX0 {t:#018x}: {f} is not the PSMT8/PSMT4 CSM1 HIGHLIGHT form')
            left_out[t] = texes.pop(t)
    return texes, left_out, static_blocks


def emot(texels: dict) -> bytes:
    """The EMOT file (layout in the module docstring) of {TEX0: texels}."""
    order = sorted(texels)
    head = struct.pack('<4s3I', b'EMOT', 1, len(order), 0)
    offset = 0x10 + 24 * len(order)
    entries, blob = b'', b''
    for t in order:
        f = tex0_fields(t)
        w, h = 1 << f['tw'], 1 << f['th']
        entries += struct.pack('<Q4I', t, w, h, offset + len(blob), 0)
        blob += texels[t]
    return head + entries + blob


def disc_texels(world, texes) -> dict:
    """Each TEX0 decoded from the first level's GS memory rebuilt from the
    disc (tools/export_disc_textures_gs.py FirstLevel.world()); refuses a
    texture that reads a block no disc upload of the route writes."""
    import export_disc_textures_gs as G
    res = G.Residency(world)
    out = {}
    for t in texes:
        out[t] = decode(res.lm, t)
        if not res.ok(lambda lm, t=t: decode(lm, t)):
            raise SystemExit(f'TEX0 {t:#018x}: reads GS blocks no disc upload of the route writes')
    return out


def cross_check(texels: dict, captures) -> None:
    """Optional: every texture decodes to the same texels from each captured
    GS freeze (a PCSX2 capture of the running original) as from the disc."""
    import gs_vram
    for path in captures:
        _base, lm = gs_vram.read_localmem(path)
        for t, data in texels.items():
            if decode(lm, t) != data:
                raise SystemExit(f'TEX0 {t:#018x}: the capture {path} differs from the disc decode')


def write(out: Path, texes: dict, texels: dict, left_out: dict, captures=()) -> bytes:
    """Writes the EMOT and its JSON report; returns the EMOT bytes."""
    data = emot(texels)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(data)
    index = []
    for t in sorted(texels):
        f = tex0_fields(t)
        index.append(dict(tex0=hex(t), width=1 << f['tw'], height=1 << f['th'], psm=hex(f['psm']),
                          models=sorted(texes[t]),
                          sha256=hashlib.sha256(texels[t]).hexdigest()[:16]))
    out.with_suffix('.json').write_text(json.dumps(dict(
        count=len(texels), bytes=len(data), source='disc (FirstLevel.world(), docs/DISC_TEXTURES.md)',
        cross_checked_captures=[str(p.parent.name) for p in captures],
        textures=index, not_exported=[dict(tex0=hex(t), models=sorted(m)) for t, m in sorted(left_out.items())]),
        indent=1) + '\n')
    return data


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--iso', type=Path, help='the disc image (default ../Extermination/Extermination-rebuilt.iso)')
    ap.add_argument('--disc', type=Path, help='a mounted disc or a copy of its DATA/ directory')
    ap.add_argument('--extract', type=Path, default=DECOMP / 'extract')
    ap.add_argument('--out', type=Path, default=ROOT / 'assets/scene_snow/object_textures.emot')
    ap.add_argument('--gs', type=Path, action='append', default=None,
                    help='optional cross-check: a captured GS freeze blob (repeatable) that must decode '
                         'every texture identically to the disc')
    ap.add_argument('--route-captures', action='store_true',
                    help='optional cross-check against every AREA11 route capture (beats 00..14)')
    args = ap.parse_args(argv)
    sys.path.insert(0, str(ROOT / 'tools'))
    import export_disc_textures_gs as G
    texes, left_out, static_blocks = tex0_set(args.extract)
    world = G.FirstLevel(G.Disc(args.iso, args.disc), args.extract).world()
    texels = disc_texels(world, texes)
    captures = list(args.gs or []) + (default_captures() if args.route_captures else [])
    cross_check(texels, captures)
    data = write(args.out, texes, texels, left_out, captures)
    static = sum(1 for t in texels if STATIC_LABEL in texes[t])
    print(f'wrote {args.out}: {len(texels)} textures ({static} of the static bank\'s {static_blocks} blocks), '
          f'{len(data)} bytes; from the disc'
          + (f'; identical in {len(captures)} captures' if captures else '')
          + ''.join(f'; not exported: TEX0 {t:#x} ({", ".join(sorted(m))})' for t, m in sorted(left_out.items())))
    return 0


if __name__ == '__main__':
    sys.exit(main())
