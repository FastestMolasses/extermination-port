#!/usr/bin/env python3
"""export_object_textures.py - the textures the AREA11 object units sample.

Every vertex of an object-unit model block carries a 64-bit TEX0 register
value in its qword 0; the object kernel 0x0023C750 kicks it as TEX0_1 and the
clip program 0x002354A0 kicks the entry vertex's (docs/VU1_OBJECT_KERNEL.md
section 3, docs/VU1_OBJECT_CLIP.md section 3). The GS samples the texture at
TBP0 through the CLUT at CBP. The object textures are not uploaded per draw:
they are resident in GS local memory for the whole level.

This exporter takes every TEX0 value (CLD, bits 61..63, ignored: it only
controls the CLUT cache) of every model block of the AREA11 world model bank
(tools/export_world_models.py build(), from the user's extract), and decodes
each from the GS local memory of every AREA11 route capture (beats 00..14:
../Extermination/build/s87/route/<beat>/gs.bin, the user's own PCSX2
captures). It fails unless:
  * every TEX0 has PSM PSMT8 or PSMT4, CPSM PSMCT32, CSM1, CSA 0, TCC 1 and
    TFX 2 (HIGHLIGHT): the one form the renderer reproduces;
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

import export_world_models as ewm  # noqa: E402

CLD_MASK = ~(7 << 61) & (2 ** 64 - 1)
PSMT8, PSMT4 = 0x13, 0x14
BLOCK_QWORDS = 0x82
CLUT16_WORD = [0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15]


def tex0_fields(t: int) -> dict:
    return dict(tbp=t & 0x3FFF, tbw=(t >> 14) & 0x3F, psm=(t >> 20) & 0x3F, tw=(t >> 26) & 0xF,
                th=(t >> 30) & 0xF, tcc=(t >> 34) & 1, tfx=(t >> 35) & 3, cbp=(t >> 37) & 0x3FFF,
                cpsm=(t >> 51) & 0xF, csm=(t >> 55) & 1, csa=(t >> 56) & 0x1F)


def model_tex0(x) -> dict:
    """{TEX0 (CLD cleared): set of model ids} over every vertex of every block."""
    out, span = {}, x['span']
    for m in x['models']:
        for b in range(m['blocks']):
            block = m['offset'] + 0x40 + 16 * BLOCK_QWORDS * b
            for i in range(32):
                t = struct.unpack_from('<Q', span, block + 16 + 64 * i)[0] & CLD_MASK
                out.setdefault(t, set()).add(m['id'])
    return out


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
    for t in texes:
        f = tex0_fields(t)
        if f['psm'] not in (PSMT8, PSMT4) or f['cpsm'] or f['csm'] or f['csa'] or f['tcc'] != 1 or f['tfx'] != 2 \
                or not (0 < f['tw'] <= 10 and 0 < f['th'] <= 10):
            raise SystemExit(f'TEX0 {t:#018x}: {f} is not the PSMT8/PSMT4 CSM1 HIGHLIGHT form')
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
                          models=sorted(hex(m) for m in texes[t]),
                          sha256=hashlib.sha256(texels[t]).hexdigest()[:16]))
    data = head + entries + blob
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(data)
    args.out.with_suffix('.json').write_text(json.dumps(dict(
        count=len(order), bytes=len(data), captures=[str(p.parent.name) for p in captures],
        textures=index), indent=1) + '\n')
    print(f'wrote {args.out}: {len(order)} textures, {len(data)} bytes; identical in {len(captures)} captures')
    return 0


if __name__ == '__main__':
    sys.exit(main())
