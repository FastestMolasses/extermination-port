#!/usr/bin/env python3
"""export_disc_textures.py - the first level's GS-resident textures, the
UI font and the two run-time-state assets from the user's own disc, with no
PCSX2 capture and no RAM dump.

Release blocker "disc-sourced textures" (docs/FIDELITY_FEATURES.md). The
capture exporters decode these textures from the GS local memory of a PCSX2
capture (or, for the font, from an EE RAM dump). This exporter rebuilds the
same GS local memory from the disc with the original's own upload sequence
(tools/export_disc_textures_gs.py, docs/DISC_TEXTURES.md section 2) and
decodes each texture with the capture exporters' own rules, so every output
is byte-identical to the capture-derived file (tools/
test_disc_textures_reference.py compares them).

Inputs (all the user's own): the disc image (--iso, default
../Extermination/Extermination-rebuilt.iso, or --disc DIR for a mounted disc
or a copy of its DATA/ directory; INDEX.IDX gives the loaders' descriptors),
the extract (../Extermination/extract/, checked equal to DATA.DAT for every
uploaded buffer), the pinned boot ELF and the AREA11 overlay.

Outputs (disc-derived: git-ignored assets/ and build/ only), relative to
--assets (default assets/):
  scene_snow/object_textures.emot / .json   (tools/export_object_textures.py)
  scene_snow/page_textures.emot / .json     (tools/export_page_textures.py;
                                             holds the 001F8D30 decal)
  font.emfn                                 (decomp tools/export_font.py)
  status_models/menu_player.emdl, menu_player.empc, letter_XX.emdl,
                models.emsk                 (tools/export_status_models.py)
  scene_snow/panel/status_hub_atlas.emha    (tools/export_status_hub.py)
  scene_snow/panel/item_root.emir           (tools/export_item_root.py)
  scene_snow/panel/battery.emba             (tools/export_panel.py)
  scene_snow/interaction.emis               (tools/export_interaction_scan.py)
  scene_snow/background.embg + its scene.txt line
                                            (the decomp's export_level.py
                                             export_background)
The last two read the EE state of the first world frame that the original
code builds from the disc (tools/export_disc_state.py, DISC_TEXTURES.md
9.4); their former source was a capture's EE RAM.
and build/disc_textures/:
  first_level_gs.bin   the rebuilt GS memory of the level (no page open) in
                       the freeze layout gs_vram.read_localmem reads, for
                       the exporters that take a --gs / p2s path
  first_frame_ee.bin   that first-frame EE image (32 MB, the layout of a
                       capture's eeMemory.bin), for export_background
  report.json          every disc buffer used (caller, DATA.DAT offset, size,
                       extract files), the steps and the output hashes

Usage (port root; macOS arm64, pure Python, ~15 s; the first frame about 6 s):
  python3 tools/export_disc_textures.py [--iso FILE | --disc DIR] [--only NAME ...]
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

DECOMP = G.DECOMP
sys.path.insert(0, str(DECOMP / 'tools'))

PARTS = ('objects', 'page', 'font', 'status_models', 'status_hub', 'item_root', 'battery',
         'interaction', 'background')

# The status pages whose GS state an asset was captured in
# (docs/DISC_TEXTURES.md section 4): the ITEM root page module 0x1F and the
# BATTERY page module 0x21 (the panel capture's D_00810146).
ITEM_ROOT_MODULE, BATTERY_MODULE = 0x1F, 0x21


def elf_bytes() -> bytes:
    elf = G.ELF_PATH.read_bytes()
    if hashlib.sha256(elf).hexdigest() != G.ELF_SHA256:
        raise SystemExit(f'{G.ELF_PATH}: not the pinned SCUS-97112 boot ELF')
    return elf


def elf_read(elf: bytes, address: int, size: int) -> bytes:
    at = address - 0x100000 + 0x300
    return elf[at:at + size]


def resident(gs: G.GSImage, t: int, decode) -> bytes:
    data = decode(bytes(gs.lm))
    if not G.reads_only_covered(gs, decode):
        raise SystemExit(f'TEX0 {t:#018x}: reads GS blocks no disc upload of the route writes')
    return data


# ---------------------------------------------------------------------------
# The parts
# ---------------------------------------------------------------------------

def part_objects(world: G.GSImage, extract: Path, assets: Path) -> list:
    """export_object_textures.py (its TEX0 set, form checks, disc decode
    and file layout: that tool is the one owner) over the rebuilt memory."""
    import export_object_textures as eot
    texes, left_out, _blocks = eot.tex0_set(extract)
    out = assets / 'scene_snow/object_textures.emot'
    eot.write(out, texes, eot.disc_texels(world, texes), left_out)
    return [out]


def page_tex0(elf: bytes, overlay: bytes) -> dict:
    """The page TEX0 set (export_page_textures.tex0_set, its one owner)."""
    import export_page_textures as ept
    return ept.tex0_set(elf, overlay)


def part_page(world: G.GSImage, elf: bytes, assets: Path) -> list:
    """export_page_textures.py (the one owner) over the rebuilt memory."""
    import export_page_textures as ept
    texes = ept.tex0_set(elf, (DECOMP / 'extract/OVERLAY/AREA11.BIN').read_bytes())
    out = assets / 'scene_snow/page_textures.emot'
    ept.write(out, texes, ept.texels_of(world, texes))
    return [out]


def font_bytes(disc: G.Disc, fl: G.FirstLevel) -> bytes:
    """assets/font.emfn in export_font.py's layout. The fonts are resource
    slots 0 (tall) and 1 (small) of module 0, which the boot main loop
    loads with 001FF1E0(0) at 0xB00000; export_font.py's glyph counts are
    (slot 1 - slot 0) / 30 and (slot 2 - slot 1) / 32 over the pointers
    D_0028A490[0..2] = 0xB00000 + each slot's resident offset."""
    import export_font as ef
    block = G.top_block(disc, 0)
    table = dict(block.slot_entries(G.module_table_index(block, 'module')))
    t0, t1, t2 = table[0], table[1], table[2]
    if not (t0 < t1 < t2):
        raise SystemExit('module 0: font slots out of order')
    region = fl._bytes('001FF1E0(0)', block, 'chunk00', 'sector 0 slots 0..1',
                       block.resident + t0, t2 - t0)
    tall = region[:t1 - t0]
    small = region[t1 - t0:]
    tall_count, small_count = (t1 - t0) // ef.TALL_BYTES, (t2 - t1) // ef.SMALL_BYTES
    per_row_tall, per_row_small = ef.SHEET_W // ef.TALL_W, ef.SHEET_W // ef.SMALL_W
    small_y0 = (tall_count + per_row_tall - 1) // per_row_tall * ef.TALL_H
    sheet_h = small_y0 + (small_count + per_row_small - 1) // per_row_small * ef.SMALL_H
    sheet = bytearray(ef.SHEET_W * sheet_h * 4)

    def blit(rows, x0, y0):
        for dy, row in enumerate(rows):
            o = ((y0 + dy) * ef.SHEET_W + x0) * 4
            for px in row:
                sheet[o:o + 4] = b'\xff\xff\xff\xff' if px else b'\xff\xff\xff\x00'
                o += 4
    glyphs = []
    for i in range(tall_count):
        u, v = (i % per_row_tall) * ef.TALL_W, (i // per_row_tall) * ef.TALL_H
        blit(ef.decode_tall(tall[i * ef.TALL_BYTES:(i + 1) * ef.TALL_BYTES]), u, v)
        glyphs.append((u, v, ef.TALL_W, ef.TALL_H, ef.TALL_ADVANCE.get(i + 0x20, ef.TALL_ADVANCE_DEFAULT)))
    for i in range(small_count):
        u, v = (i % per_row_small) * ef.SMALL_W, small_y0 + (i // per_row_small) * ef.SMALL_H
        blit(ef.decode_small(small[i * ef.SMALL_BYTES:(i + 1) * ef.SMALL_BYTES]), u, v)
        glyphs.append((u, v, ef.SMALL_W, ef.SMALL_H, ef.SMALL_W))
    out = b'EMFN' + struct.pack('<5I', 1, ef.SHEET_W, sheet_h, 2, len(glyphs))
    out += struct.pack('<4I', 0, tall_count, ef.TALL_W, ef.TALL_H)
    out += struct.pack('<4I', tall_count, small_count, ef.SMALL_W, ef.SMALL_H)
    out += b''.join(struct.pack('<2H4B', u, v, w, h, adv, 0) for (u, v, w, h, adv) in glyphs)
    return out + bytes(sheet)


def part_font(disc: G.Disc, fl: G.FirstLevel, assets: Path) -> list:
    out = assets / 'font.emfn'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(font_bytes(disc, fl))
    return [out]


def part_status_models(world_freeze: Path, extract: Path, assets: Path) -> list:
    """export_status_models.py (the one owner) with texels from the rebuilt
    memory (the status-hub capture's GS equals it over every uploaded
    block)."""
    import export_status_models as esm
    return esm.write(assets / 'status_models', extract, world_freeze)[0]


def status_model_tex0(extract: Path) -> list:
    """The TEX0 keys the status models sample (for the residency check)."""
    import export_status_models as esm
    return esm.texture_keys(extract)


def hub_tokens(elf: bytes, states: list | None = None) -> list:
    """export_status_hub.py's token list (its constants and order): the
    five secondary-weapon icons, then every TEX0 the original 00209DF0
    passes to its sprite worker for hovers 0..4 x infection 0 / 100 and the
    fixture pass, the drawer run over the ELF's initial data (the atlas
    part needs only the tokens; export_status_hub.py runs over the full
    disc memory). `states`, if given, receives each executed (hover,
    infection) and 'fixture'."""
    import export_status_hub as H
    from test_player_slide_reference import EE
    ram = bytes(EE(elf).mem)
    states = states if states is not None else []
    layouts = []
    for hover in H.HOVERS:
        for infection in H.INFECTIONS:
            original = H.Original(elf, ram, hover, infection)
            original.run(0x209DF0, (H.UI,))
            layouts.append(original.commands)
            states.append((hover, infection))
    fixture = H.Original(elf, ram, ram[H.UI + 0x11], H.number(struct.unpack_from('<I', ram, 0x81085C)[0]), True)
    fixture.run(0x209DF0, (H.UI,))
    states.append('fixture')
    return list(dict.fromkeys(H.SECONDARY_ICONS +
                              [c['tex0'] for layout in layouts + [fixture.commands] for c in layout if 'tex0' in c]))


def part_status_hub(world: G.GSImage, elf: bytes, assets: Path, tokens=None, states=None) -> list:
    """status_hub_atlas.emha (export_status_hub.atlas_emha, the one owner).
    `tokens`: a token list to use instead of executing 00209DF0 (the quick
    run of test_disc_textures_reference passes its pinned list; its full
    run checks hub_tokens() equals it)."""
    import export_status_hub as H
    tokens = list(tokens) if tokens is not None else hub_tokens(elf, states)
    out = assets / 'scene_snow/panel/status_hub_atlas.emha'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(H.atlas_emha(tokens, token_decoder(world))[0])
    return [out]


def part_item_root(gs: G.GSImage, elf: bytes, extract: Path, assets: Path) -> list:
    """export_item_root.py (the one owner: its executor runs 0020F170 /
    0020F2A0 over the ELF image); texels from the rebuilt memory with the
    ITEM root page module 0x1F loaded."""
    import export_item_root as eir
    out = assets / 'scene_snow/panel/item_root.emir'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(eir.item_root_emir(elf, token_decoder(gs), extract)[0])
    return [out]


def token_decoder(gs: G.GSImage):
    """decode_token(token) -> (RGBA8, {'w', 'h'}) from the rebuilt memory,
    refusing a token that reads a block no disc upload writes."""
    from export_ui import decode_token_lm
    lm = bytes(gs.lm)

    def decode(token):
        lo, hi = token & 0xFFFFFFFF, token >> 32
        resident(gs, token, lambda m: decode_token_lm(m, lo, hi)[0])
        return decode_token_lm(lm, lo, hi)
    return decode


def part_battery(gs: G.GSImage, elf: bytes, extract: Path, assets: Path) -> list:
    """battery.emba of export_panel.py (the one owner: its tokens are the
    ELF tables D_00265C50 (16) and D_00265CD0 (9) and its two listed
    constants); texels from the rebuilt memory with the BATTERY page module
    0x21 loaded."""
    import export_panel as epn
    out = assets / 'scene_snow/panel/battery.emba'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(epn.battery_emba(elf, token_decoder(gs), extract)[0])
    return [out]


def part_interaction(state, assets: Path) -> list:
    """interaction.emis of export_interaction_scan.py (the one owner) over
    the disc first frame: each owner's first tick wrote its status,
    selector and descriptor pointer there."""
    import export_interaction_scan as eis
    out = assets / 'scene_snow/interaction.emis'
    blob, metadata = eis.build(state.image(), 'disc first frame (tools/export_disc_state.py)', DECOMP)
    eis.write(out, blob, metadata)
    return [out]


def part_background(state, disc: G.Disc, scratch: Path, assets: Path) -> list:
    """background.embg of the decomp's export_level.export_background (the
    one owner, unchanged) over the disc first frame's EE image: its
    001C1F50 armed flags 0x20 / 0x21 and stored the TEX0 and colour, and
    its 001C1D00 -> 001E0CF0 -> 001E1E60 built render channel 3's list. The
    exporter writes into scratch; the asset is copied and its manifest line
    added to assets/scene_snow/scene.txt only when that file lacks it, so an
    existing manifest keeps its order."""
    el = G.export_level()
    image = scratch / 'first_frame_ee.bin'
    image.write_bytes(state.image())
    work = scratch / 'background'
    work.mkdir(parents=True, exist_ok=True)
    (work / 'scene.txt').write_text('')
    el.export_background(work, G.ELF_PATH, image, None, 11, 0, disc.image)
    scene = assets / 'scene_snow'
    scene.mkdir(parents=True, exist_ok=True)
    out = scene / el.BG_ASSET
    out.write_bytes((work / el.BG_ASSET).read_bytes())
    line = (work / 'scene.txt').read_text().strip()
    manifest = scene / 'scene.txt'
    if not manifest.exists() or line not in manifest.read_text().splitlines():
        key, value = line.split(' ', 1)
        el.update_manifest(scene, key, value)
    return [out]


# ---------------------------------------------------------------------------

def build(disc: G.Disc, extract: Path, assets: Path, scratch: Path, parts=PARTS,
          fl: G.FirstLevel | None = None, world: G.GSImage | None = None, hub_token_list=None,
          write_freeze: bool = True) -> dict:
    elf = elf_bytes()
    fl = fl or G.FirstLevel(disc, extract)
    world = world or fl.world()
    scratch.mkdir(parents=True, exist_ok=True)
    freeze = scratch / 'first_level_gs.bin'
    if write_freeze or 'status_models' in parts:
        freeze.write_bytes(world.freeze())
    written = []
    if 'objects' in parts:
        written += part_objects(world, extract, assets)
    if 'page' in parts:
        written += part_page(world, elf, assets)
    if 'font' in parts:
        written += part_font(disc, fl, assets)
    if 'status_models' in parts:
        for key in status_model_tex0(extract):
            import export_object_textures as eot
            resident(world, key, lambda lm, t=key: eot.decode(lm, t))
        written += part_status_models(freeze, extract, assets)
    hub_states = []
    if 'status_hub' in parts:
        written += part_status_hub(world, elf, assets, hub_token_list, hub_states)
    pages = {}
    if 'item_root' in parts:
        pages['item_root'] = fl.page(world, ITEM_ROOT_MODULE)
        written += part_item_root(pages['item_root'], elf, extract, assets)
    if 'battery' in parts:
        pages['battery'] = fl.page(world, BATTERY_MODULE)
        written += part_battery(pages['battery'], elf, extract, assets)
    state = None
    if 'interaction' in parts or 'background' in parts:
        import export_disc_state as D
        state = D.first_frame(disc, elf, extract)
    if 'interaction' in parts:
        written += part_interaction(state, assets)
    if 'background' in parts:
        written += part_background(state, disc, scratch, assets)
    report = {
        'elf_sha256': G.ELF_SHA256,
        'buffers': [dict(caller=c, source=l, data_dat_offset=hex(o), size=hex(s), extract=w)
                    for c, l, o, s, w in fl.sources],
        'world_steps': [dict(caller=c, source=l, transfers=[dict(dbp=hex(t[0]), dbw=t[1], w=t[4], h=t[5])
                                                            for t in log]) for c, l, log in world.steps],
        'page_states': {k: [dict(caller=c, source=l) for c, l, _ in v.steps[len(world.steps):]]
                        for k, v in pages.items()},
        'hub_states': [list(x) if isinstance(x, tuple) else x for x in hub_states],
        'first_frame': None if state is None else dict(
            executed=state.steps, boundaries=state.boundaries,
            dma_kicks=[dict(k, chcr=hex(k['chcr']), tadr=hex(k['tadr'])) for k in state.kicks],
            nodes_after_first_tick=len(state.first_tick_nodes),
            freed_by_own_tick=[hex(n) for n in state.freed]),
        'freeze': str(freeze),
        'outputs': {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in written},
    }
    (scratch / 'report.json').write_text(json.dumps(report, indent=1) + '\n')
    return report


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--iso', type=Path, help='the disc image (default ../Extermination/Extermination-rebuilt.iso)')
    ap.add_argument('--disc', type=Path, help='a mounted disc or a copy of its DATA/ directory')
    ap.add_argument('--extract', type=Path, default=G.EXTRACT)
    ap.add_argument('--assets', type=Path, default=ROOT / 'assets')
    ap.add_argument('--scratch', type=Path, default=ROOT / 'build/disc_textures')
    ap.add_argument('--only', action='append', choices=PARTS)
    args = ap.parse_args(argv)
    disc = G.Disc(args.iso, args.disc)
    report = build(disc, args.extract, args.assets, args.scratch, tuple(args.only or PARTS))
    for path, digest in report['outputs'].items():
        print(f'wrote {path} ({digest[:16]})')
    print(f'{len(report["buffers"])} disc buffers read; report {args.scratch / "report.json"}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
