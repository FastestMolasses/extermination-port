#!/usr/bin/env python3
"""export_page_textures.py - the textures the chain page's primitives sample.

The chain page D_007635C0 (docs/CHAIN_PAGE.md) draws the effects' sprites
(001CFBE0 through the sprite program of table 0x231770), the glow markers
and equipment sprites (001CD520), the pickup glint (untextured) and the
0015BF90 drop-shadow decal (001CE300). Their TEX0 words come from the
original's data and code, read from the user's ELF and AREA11 overlay:
  * the TEX0 row (+0x70) of each 001CFBE0 source block: D_00253670 (the
    head sprite's), D_002565E0 + 0x90 k, k = 0..7 (the effect handlers'),
    and the impact effects' D_002560D0 / D_00256160 (001EBA20) and
    D_00255620 (001EACF0), D_002561F0 (001EBBB0) and D_00255590 (001EAB50),
    and the bone-burst node 0022BBC0's burst-0 pair 0x268480 / 0x268510
    (the flame contact's effect 0x80000027, subtype 9: AREA11's one
    timeline 0x267940 sets burst kind 0 only; docs/DAMAGE.md section 3);
  * the TEX0 row (+0x70) of the weather's descriptor D_00255170 (001CFFE0's
    object for the snow tiles; the snow program sends it with every sprite);
  * the TEX0 row (+0x70) of the AREA11 flame's descriptor D_00828340
    (owner 008235F0, AREA11.BIN file offset 0x4E40; docs/AREA11_EFFECT.md);
  * values the original code builds as immediates: 001F8D30's decal
    0x2004290511322469 (EM_SHADOW_DECAL_TEX0), 001F4D40 / 001F4BF0's
    glow marker (EM_STATUS_SCENE_TEX0_001F4BF0), the laser dot
    0x20045BA5154222DC that 001854E0 / 00185760 pass to 001CD520 (the aim
    sight, em_aim_fire_target.c), 001F0460's three ring-decal tags (the
    shots' impact marks, em_effect_original.c), 00187780's two flare
    words (the gun lamp, em_aim_fire_lamp.c), 001EAB50's sprite word (the
    cable hit, em_effect_kinds.c) and 0021A500's strip word (the parted
    strand, em_security_gun_rest.c), each verified against the executed
    original by its module's reference test.
None of them is uploaded by the draw: each is resident in GS local memory.
This tool decodes each from the first level's GS local memory rebuilt from
the user's own disc (tools/export_disc_textures_gs.py FirstLevel.world();
docs/DISC_TEXTURES.md) and fails unless every TEX0 is PSMT8 or PSMT4
through a PSMCT32 CLUT (CSM1, CSA 0) with TCC 1 and reads only GS blocks a
disc upload of the route writes. No PCSX2 capture is needed.

Optional cross-check (--route-captures, for developers with the captures):
tools/chain_page_model.py walks the latest page of every route capture
00..14 with the original VU1 microcode; every TEX0 a captured page draws
must be in the set above, and every texture must decode identically from
each capture's GS memory.
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
  python3 tools/export_page_textures.py [--iso FILE | --disc DIR] [--route-captures]
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
MARKER_TEX0 = 0x20045B0599421EF0      # 001F4D40 / 001F4BF0's (EM_STATUS_SCENE_TEX0_001F4BF0)
DOT_TEX0 = 0x20045BA5154222DC         # 001854E0 / 00185760's laser dot (em_aim_fire_target.c sprite)
# 001F0460's ring decals (the shots' impact marks): its slots' tags by n
# (em_effect_original.c, test-effect-original-reference): n 0 / 4 / 5 / 6,
# n 1 / 2 and n 3.
RING_TEX0 = (0x20040F8555322078, 0x200418851532218C, 0x2004108555322080)
# 00187780's flare words (00187690 stores them at D_00248850, the +0x70 TEX0
# row of its source block D_002487E0; em_aim_fire_lamp.c,
# test-aim-fire-lamp-reference): mode 0 and mode 1.
FLARE_TEX0 = (0x20045D05554221F6, 0x20048D0599422050)
# 001EAB50's fading sprite (the cable hit's effect 0x80000045; em_effect_kinds,
# test-effect-kinds-reference): the immediate it hands 001CD520.
CABLE_SPRITE_TEX0 = 0x20045B2599421E98
# 0021A500's strip word (the cable's parted strand, the strip packets it
# builds for 001CE860; em_security_gun_rest, tools/test_security_gun_rest_reference.py).
CABLE_STRIP_TEX0 = 0x20045D8555422188
SOURCE_BLOCKS = [0x00253670] + [0x002565E0 + 0x90 * k for k in range(8)] + [
    0x002560D0, 0x00256160,           # 001EBA20's (the impact effect 0x8000002C; AIM_FIRE.md)
    0x00255620,                       # 001EACF0's (the impact effect 0x80000060)
    0x002561F0,                       # 001EBBB0's (the effect 0x8000000E of 001F0460's ring decal)
    0x00255590,                       # 001EAB50's (the cable hit's effect 0x80000045)
    0x00268480, 0x00268510]           # 0022BBC0's burst 0 (the flame contact's 0x80000027; DAMAGE.md)
WEATHER_DESCRIPTOR = 0x00255170       # D_00255170 (001E67C0 / 001CFFE0)
FLAME_DESCRIPTOR, FLAME_FILE_OFFSET = 0x00828340, 0x4E40   # 008235F0's D_00828340 in AREA11.BIN
CLD_MASK = eot.CLD_MASK


def elf_u64(elf: bytes, address: int) -> int:
    return struct.unpack_from('<Q', elf, address - 0x100000 + 0x300)[0]


def tex0_set(elf: bytes, overlay: bytes) -> dict:
    """{TEX0 (CLD cleared): the original producers} of the chain page."""
    if overlay[:4] != b'MWo3' or struct.unpack_from('<I', overlay, 8)[0] != 0x823500:
        raise SystemExit('extract/OVERLAY/AREA11.BIN: not the AREA11 overlay at 0x823500')
    out = {}
    for block in SOURCE_BLOCKS:
        out.setdefault(elf_u64(elf, block + 0x70) & CLD_MASK, set()).add(f'001CFBE0 source {block:#010x}')
    out.setdefault(elf_u64(elf, WEATHER_DESCRIPTOR + 0x70) & CLD_MASK, set()).add(
        f'001CFFE0 weather descriptor {WEATHER_DESCRIPTOR:#010x}')
    t = struct.unpack_from('<Q', overlay, FLAME_FILE_OFFSET + 0x70)[0] & CLD_MASK
    out.setdefault(t, set()).add(f'001D04B0 flame descriptor {FLAME_DESCRIPTOR:#010x}')
    out.setdefault(DECAL_TEX0 & CLD_MASK, set()).add('001F8D30 decal')
    out.setdefault(MARKER_TEX0 & CLD_MASK, set()).add('001F4D40 / 001F4BF0 glow marker')
    out.setdefault(DOT_TEX0 & CLD_MASK, set()).add('001854E0 / 00185760 laser dot')
    for t in RING_TEX0:
        out.setdefault(t & CLD_MASK, set()).add('001F0460 ring decal')
    for t in FLARE_TEX0:
        out.setdefault(t & CLD_MASK, set()).add('00187780 / 00187690 lamp flare')
    out.setdefault(CABLE_SPRITE_TEX0 & CLD_MASK, set()).add('001EAB50 cable-hit sprite')
    out.setdefault(CABLE_STRIP_TEX0 & CLD_MASK, set()).add('0021A500 cable strand strip')
    return out


def captured_tex0(route: Path) -> dict:
    """Optional cross-check: every textured TEX0 the latest page of each
    route capture 00..14 draws (walked with the original VU1 microcode)."""
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


def drawable(t: int) -> bool:
    f = eot.tex0_fields(t)
    return (f['psm'] in (eot.PSMT8, eot.PSMT4) and not f['cpsm'] and not f['csm'] and not f['csa']
            and f['tcc'] == 1 and 0 < f['tw'] <= 10 and 0 < f['th'] <= 10)


def texels_of(world, texes: dict) -> dict:
    for t in texes:
        if not drawable(t):
            raise SystemExit(f'TEX0 {t:#018x} ({sorted(texes[t])}): not a CT32-CLUT PSMT8/PSMT4 TCC 1 texture')
    return eot.disc_texels(world, texes)


def write(out: Path, texes: dict, texels: dict, captures=()) -> bytes:
    data = eot.emot(texels)
    out.parent.mkdir(parents=True, exist_ok=True)
    for path in (out, out.with_suffix('.json')):
        if path.is_symlink():
            path.unlink()   # a worktree's link to a shared tree: write a file of its own
    out.write_bytes(data)
    index = []
    for t in sorted(texels):
        f = eot.tex0_fields(t)
        index.append(dict(tex0=hex(t), width=1 << f['tw'], height=1 << f['th'], psm=hex(f['psm']), tfx=f['tfx'],
                          users=sorted(texes[t]), sha256=hashlib.sha256(texels[t]).hexdigest()[:16]))
    out.with_suffix('.json').write_text(json.dumps(dict(
        count=len(texels), bytes=len(data), source='disc (FirstLevel.world(), docs/DISC_TEXTURES.md)',
        cross_checked_captures=[p.parent.name for p in captures], textures=index,
        not_exported=[]), indent=1) + '\n')
    return data


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--iso', type=Path, help='the disc image (default ../Extermination/Extermination-rebuilt.iso)')
    ap.add_argument('--disc', type=Path, help='a mounted disc or a copy of its DATA/ directory')
    ap.add_argument('--extract', type=Path, default=DECOMP / 'extract')
    ap.add_argument('--elf', type=Path, default=DECOMP / 'config/SCUS_971.12')
    ap.add_argument('--out', type=Path, default=ROOT / 'assets/scene_snow/page_textures.emot')
    ap.add_argument('--route-captures', action='store_true',
                    help='optional cross-check against the route captures (beats 00..14)')
    ap.add_argument('--route', type=Path, default=DECOMP / 'build/s87/route')
    args = ap.parse_args(argv)
    elf = args.elf.read_bytes()
    if hashlib.sha256(elf).hexdigest() != ELF_SHA256:
        raise SystemExit(f'{args.elf}: not the pinned SCUS-97112 boot ELF')
    import export_disc_textures_gs as G
    texes = tex0_set(elf, (args.extract / 'OVERLAY/AREA11.BIN').read_bytes())
    world = G.FirstLevel(G.Disc(args.iso, args.disc), args.extract).world()
    texels = texels_of(world, texes)
    captures = []
    if args.route_captures:
        drawn = captured_tex0(args.route)
        extra = sorted(set(drawn) - set(texes))
        if extra:
            raise SystemExit('captured pages draw TEX0 with no listed original producer: '
                             + ', '.join(f'{t:#x} ({sorted(drawn[t])})' for t in extra))
        captures = [p for p in eot.default_captures()]
        eot.cross_check(texels, captures)
    data = write(args.out, texes, texels, captures)
    print(f'wrote {args.out}: {len(texels)} textures, {len(data)} bytes; from the disc'
          + (f'; identical in {len(captures)} captures' if captures else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main())
