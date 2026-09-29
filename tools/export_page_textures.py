#!/usr/bin/env python3
"""export_page_textures.py - the textures the chain page's primitives sample.

The chain page D_007635C0 (docs/CHAIN_PAGE.md) draws the effects' sprites
(001CFBE0 through the sprite program of table 0x231770), the glow markers
and equipment sprites (001CD520), the pickup glint (untextured) and the
0015BF90 drop-shadow decal (001CE300). Their TEX0 words come from the
original's data and code:
  * every TEX0 a captured page draws (tools/chain_page_model.py walks the
    latest page of every route capture 00..14 with the original VU1
    microcode; the arena CALLs of producers the port does not run on the
    page are walked over);
  * the TEX0 row (+0x70) of each 001CFBE0 source block: D_00253670 (the
    head sprite's) and D_002565E0 + 0x90 k, k = 0..7 (the effect handlers');
  * the TEX0 row (+0x70) of the weather's descriptor D_00255170 (001CFFE0's
    object for the snow tiles; the snow program sends it with every sprite;
    the captured pages' weather kicks are walked over here, so the row is
    read from the ELF and required like a drawn TEX0);
  * the decal's TEX0 0x2004290511322469 (001F8D30's constant,
    EM_SHADOW_DECAL_TEX0).
None of them is uploaded by the draw: each is resident in GS local memory.
This tool decodes each from the GS local memory of every AREA11 route
capture (../Extermination/build/s87/route/<beat>/gs.bin, the user's own
PCSX2 captures) and fails unless every TEX0 a captured page draws is
PSMT8 or PSMT4 through a PSMCT32 CLUT (CSM1, CSA 0) with TCC 1 and decodes
identically in every capture (residency). A source-block TEX0 that is not of
that form or not resident is left out and listed ("not_exported"): a page
that draws it faults in the renderer.
The texels are the CLUT entries' four bytes as GS memory holds them: R, G, B
and the raw GS alpha (0x80 = 1.0), NOT rescaled.

Output (disc-derived: git-ignored assets/ only):
  assets/scene_snow/page_textures.emot, little-endian, read by
  em_chain_page_live (src/game/em_chain_page_live.c):
    0x00 'EMOT', u32 version 1, u32 count N, u32 0
    0x10 N entries of 24 bytes: u64 TEX0 (CLD cleared), u32 width,
         u32 height, u32 texel offset (from the file start), u32 0
    then the texels, width x height x 4 bytes each, rows top-down
  assets/scene_snow/page_textures.json: counts, TEX0 values, sizes and
  hashes (no texels).

Uses the decomp's GS memory readers (tools/gs_vram.py, clut_pair.py) through
tools/export_object_textures.py. Runs natively on arm64 macOS (pure Python).

Usage (port root):
  python3 tools/export_page_textures.py
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
sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(DECOMP / 'tools'))

import chain_page_model as M  # noqa: E402
import export_object_textures as eot  # noqa: E402

ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
DECAL_TEX0 = 0x2004290511322469       # 001F8D30's constant (em_shadow_decal_original.h)
SOURCE_BLOCKS = [0x00253670] + [0x002565E0 + 0x90 * k for k in range(8)]
WEATHER_DESCRIPTOR = 0x00255170       # D_00255170 (001E67C0 / 001CFFE0)
CLD_MASK = eot.CLD_MASK


def captured_tex0(route: Path) -> dict:
    out = {}
    for d in sorted(route.iterdir()):
        ram_path = d / 'eeMemory.bin'
        if not ram_path.exists() or not d.name[:2].isdigit() or int(d.name[:2]) > 14:
            continue
        ram = ram_path.read_bytes()
        start = M.latest_start(ram)
        probe = M.Page(M.ram_reader(ram))
        probe.dma(start)
        skip = {a for (_cur, tid, _q, a) in probe.transfers if tid == 5 and M.ARENA <= a < 0x800000}
        page = M.Page(M.ram_reader(ram), skip_calls=skip)
        page.run(start)
        for prim, state, _verts in page.gs.prims:
            if prim & 0x10:
                out.setdefault(state[0] & CLD_MASK, set()).add(f'page {d.name[:2]}')
    return out


def source_tex0(elf: bytes) -> dict:
    out = {}
    for block in SOURCE_BLOCKS:
        o = block + 0x70 - 0x100000 + 0x300
        t = struct.unpack_from('<Q', elf, o)[0] & CLD_MASK
        out.setdefault(t, set()).add(f'001CFBE0 source {block:#010x}')
    return out


def weather_tex0(elf: bytes) -> dict:
    o = WEATHER_DESCRIPTOR + 0x70 - 0x100000 + 0x300
    return {struct.unpack_from('<Q', elf, o)[0] & CLD_MASK: {f'001CFFE0 weather descriptor {WEATHER_DESCRIPTOR:#010x}'}}


def drawable(t: int) -> bool:
    f = eot.tex0_fields(t)
    return (f['psm'] in (eot.PSMT8, eot.PSMT4) and not f['cpsm'] and not f['csm'] and not f['csa']
            and f['tcc'] == 1 and 0 < f['tw'] <= 10 and 0 < f['th'] <= 10)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--elf', type=Path, default=DECOMP / 'config/SCUS_971.12')
    ap.add_argument('--route', type=Path, default=DECOMP / 'build/s87/route')
    ap.add_argument('--out', type=Path, default=ROOT / 'assets/scene_snow/page_textures.emot')
    args = ap.parse_args(argv)
    elf = args.elf.read_bytes()
    if hashlib.sha256(elf).hexdigest() != ELF_SHA256:
        raise SystemExit(f'{args.elf}: not the pinned SCUS-97112 boot ELF')
    import gs_vram
    drawn = captured_tex0(args.route)
    for t, why in weather_tex0(elf).items():
        drawn.setdefault(t, set()).update(why)
    texes = {}
    for src in (drawn, source_tex0(elf), {DECAL_TEX0 & CLD_MASK: {'001F8D30 decal'}}):
        for t, why in src.items():
            texes.setdefault(t, set()).update(why)
    left_out = {}
    for t in list(texes):
        if not drawable(t):
            if t in drawn:
                raise SystemExit(f'TEX0 {t:#018x} ({sorted(texes[t])}): not a CT32-CLUT PSMT8/PSMT4 TCC 1 texture')
            left_out[t] = texes.pop(t)
    captures = eot.default_captures()
    if not captures:
        raise SystemExit('no captured gs.bin (../Extermination/build/s87/route/<beat>/gs.bin)')
    texels = {}
    for path in captures:
        _base, lm = gs_vram.read_localmem(path)
        for t in list(texes):
            data = eot.decode(lm, t)
            if texels.setdefault(t, data) != data:
                if t in drawn:
                    raise SystemExit(f'TEX0 {t:#018x} decodes differently in {path}: not resident')
                left_out[t] = texes.pop(t)
                texels.pop(t)
    order = sorted(texes)
    head = struct.pack('<4s3I', b'EMOT', 1, len(order), 0)
    offset = 0x10 + 24 * len(order)
    entries, blob, index = b'', b'', []
    for t in order:
        f = eot.tex0_fields(t)
        w, h = 1 << f['tw'], 1 << f['th']
        entries += struct.pack('<Q4I', t, w, h, offset + len(blob), 0)
        blob += texels[t]
        index.append(dict(tex0=hex(t), width=w, height=h, psm=hex(f['psm']), tfx=f['tfx'],
                          users=sorted(texes[t]), sha256=hashlib.sha256(texels[t]).hexdigest()[:16]))
    data = head + entries + blob
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(data)
    args.out.with_suffix('.json').write_text(json.dumps(dict(
        count=len(order), bytes=len(data), captures=[p.parent.name for p in captures], textures=index,
        not_exported=[dict(tex0=hex(t), users=sorted(u)) for t, u in sorted(left_out.items())]), indent=1) + '\n')
    print(f'wrote {args.out}: {len(order)} textures, {len(data)} bytes; identical in {len(captures)} captures'
          + ''.join(f'; not exported: TEX0 {t:#x} ({", ".join(sorted(u))})' for t, u in sorted(left_out.items())))
    return 0


if __name__ == '__main__':
    sys.exit(main())
