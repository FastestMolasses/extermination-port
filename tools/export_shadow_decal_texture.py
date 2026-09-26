#!/usr/bin/env python3
"""export_shadow_decal_texture.py - the texture the 0015BF90 decal samples.

When the player stands on an actor (player +0x214 != 0), the post-step
0015C160 draws its shadow through 0015BF90 -> 001F9100 -> 001F8D30 ->
001CE300 (docs/SHADOW_ACTOR_ROUTE.md, docs/SHADOW_DECAL.md): one textured
quad with the TEX0 word 0x2004290511322469 that 001F8D30 builds
(EM_SHADOW_DECAL_TEX0): TBP0 0x2469, TBW 8, PSMT8, 16 x 16, TCC 1, TFX 0
(MODULATE), CBP 0x2148, CPSM PSMCT32, CSM1, CSA 0. The texture is not
uploaded by the draw: it is resident in GS local memory in every captured
beat, and its uploader is not identified (SHADOW_DECAL.md section 4: no
boot-ELF instruction builds 0x2469 except 001F8D30's TEX0, and the texels
are not in the extracted disc files as a contiguous block). So, as
tools/export_object_textures.py does for the object textures, this tool
decodes it from the GS local memory of the user's own AREA11 route captures
(../Extermination/build/s87/route/<beat>/gs.bin) and fails unless:
  * the TEX0 decodes to exactly the fields above;
  * the texels and the CLUT are identical in every capture (residency);
  * every texel index names a CLUT entry (PSMT8: 0..255).
The output texels are the CLUT entries' four bytes as GS memory holds them:
R, G, B and the raw GS alpha (0x80 = 1.0), NOT rescaled.

Output (disc-derived: git-ignored assets/ only):
  assets/scene_snow/shadow_decal.emdt, little-endian, read by
  em_shadow_live (src/game/em_shadow_live.c):
    0x00 'EMDT', u32 version 1, u32 width 16, u32 height 16
    0x10 u64 TEX0 (CLD bits kept as 001F8D30 builds it), u64 0
    0x20 width x height x 4 bytes, rows top-down

Uses the decomp's GS memory readers (tools/gs_vram.py, clut_pair.py).
Runs natively on arm64 macOS (pure Python).

Usage (port root):
  python3 tools/export_shadow_decal_texture.py
"""
from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
sys.path.insert(0, str(DECOMP / 'tools'))

TEX0 = 0x2004290511322469
PSMT8 = 0x13


def tex0_fields(t: int) -> dict:
    return dict(tbp=t & 0x3FFF, tbw=(t >> 14) & 0x3F, psm=(t >> 20) & 0x3F, tw=(t >> 26) & 0xF,
                th=(t >> 30) & 0xF, tcc=(t >> 34) & 1, tfx=(t >> 35) & 3, cbp=(t >> 37) & 0x3FFF,
                cpsm=(t >> 51) & 0xF, csm=(t >> 55) & 1, csa=(t >> 56) & 0x1F, cld=(t >> 61) & 7)


def decode(lm: bytes, t: int) -> bytes:
    import clut_pair as cp
    import gs_vram
    f = tex0_fields(t)
    w, h = 1 << f['tw'], 1 << f['th']
    idx = cp.read_psmt8(lm, f['tbp'], f['tbw'], w, h)
    pal = gs_vram.csm1_unswizzle_clut(lm[f['cbp'] * 256:f['cbp'] * 256 + 1024])
    return b''.join(pal[4 * i:4 * i + 4] for i in idx)


def default_captures():
    route = DECOMP / 'build/s87/route'
    return sorted(p for p in route.glob('*/gs.bin') if p.parent.name[:2].isdigit() and int(p.parent.name[:2]) <= 14)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, default=ROOT / 'assets/scene_snow/shadow_decal.emdt')
    ap.add_argument('--gs', type=Path, action='append', default=None,
                    help='captured GS freeze blob (repeatable); default: every AREA11 route capture')
    args = ap.parse_args(argv)
    import gs_vram
    f = tex0_fields(TEX0)
    want = dict(tbp=0x2469, tbw=8, psm=PSMT8, tw=4, th=4, tcc=1, tfx=0, cbp=0x2148, cpsm=0, csm=0, csa=0, cld=1)
    if f != want:
        raise SystemExit(f'TEX0 {TEX0:#018x} decodes to {f}, not {want}')
    captures = args.gs if args.gs is not None else default_captures()
    if not captures:
        raise SystemExit('no captured gs.bin (../Extermination/build/s87/route/<beat>/gs.bin)')
    texels = None
    for path in captures:
        _base, lm = gs_vram.read_localmem(path)
        data = decode(lm, TEX0)
        if texels is None:
            texels = data
        elif texels != data:
            raise SystemExit(f'the decal texture decodes differently in {path}: not resident')
    w, h = 1 << f['tw'], 1 << f['th']
    blob = struct.pack('<4s3I', b'EMDT', 1, w, h) + struct.pack('<2Q', TEX0, 0) + texels
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(blob)
    print(f'wrote {args.out}: {w} x {h}, {len(blob)} bytes, sha256 '
          f'{hashlib.sha256(texels).hexdigest()[:16]}; identical in {len(captures)} captures')
    return 0


if __name__ == '__main__':
    sys.exit(main())
