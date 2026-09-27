#!/usr/bin/env python3
"""export_disc_textures.py - the first level's GS-resident textures and the
UI font from the user's own disc, with no PCSX2 capture and no RAM dump.

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
and build/disc_textures/:
  first_level_gs.bin   the rebuilt GS memory of the level (no page open) in
                       the freeze layout gs_vram.read_localmem reads, for
                       the exporters that take a --gs / p2s path
  report.json          every disc buffer used (caller, DATA.DAT offset, size,
                       extract files), the steps and the output hashes

Usage (port root; macOS arm64, pure Python, ~10 s):
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

PARTS = ('objects', 'page', 'font', 'status_models', 'status_hub', 'item_root', 'battery')

# The page textures (tools/export_page_textures.py): the 001CFBE0 source
# blocks' TEX0 rows (+0x70) in the ELF, the AREA11 flame's descriptor
# (owner 008235F0's D_00828340, AREA11.BIN file offset 0x4E40, TEX0 row
# +0x70; docs/AREA11_EFFECT.md), and two TEX0 values the original code
# builds as immediates: 001F8D30's decal (em_shadow_decal_original.h
# EM_SHADOW_DECAL_TEX0) and 001F4D40 / 001F4BF0's glow marker
# (em_effect_manager.c, em_status_scene_original.h
# EM_STATUS_SCENE_TEX0_001F4BF0), both verified against the executed
# original by their modules' reference tests.
SOURCE_BLOCKS = [0x00253670] + [0x002565E0 + 0x90 * k for k in range(8)]
FLAME_DESCRIPTOR, FLAME_FILE_OFFSET = 0x00828340, 0x4E40
DECAL_TEX0 = 0x2004290511322469
MARKER_TEX0 = 0x20045B0599421EF0

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


def emot(textures: dict) -> bytes:
    """The EMOT layout of export_object_textures / export_page_textures."""
    import export_object_textures as eot
    order = sorted(textures)
    head = struct.pack('<4s3I', b'EMOT', 1, len(order), 0)
    offset = 0x10 + 24 * len(order)
    entries, blob = b'', b''
    for t in order:
        f = eot.tex0_fields(t)
        w, h = 1 << f['tw'], 1 << f['th']
        entries += struct.pack('<Q4I', t, w, h, offset + len(blob), 0)
        blob += textures[t]
    return head + entries + blob


def resident(gs: G.GSImage, t: int, decode) -> bytes:
    data = decode(bytes(gs.lm))
    if not G.reads_only_covered(gs, decode):
        raise SystemExit(f'TEX0 {t:#018x}: reads GS blocks no disc upload of the route writes')
    return data


# ---------------------------------------------------------------------------
# The parts
# ---------------------------------------------------------------------------

def part_objects(world: G.GSImage, extract: Path, assets: Path) -> list:
    """export_object_textures.py's TEX0 set (every vertex TEX0 of the
    AREA11 world models, the player model, the equipment and the item
    model; the same form checks) decoded from the rebuilt memory."""
    import export_object_textures as eot
    import export_world_models as ewm
    x = ewm.build(extract)
    texes = eot.model_tex0(x)
    eot.player_tex0(extract, texes)
    left_out = {}
    for t in list(texes):
        f = eot.tex0_fields(t)
        if f['psm'] not in (eot.PSMT8, eot.PSMT4) or f['cpsm'] or f['csm'] or f['csa'] or f['tcc'] != 1 \
                or f['tfx'] != 2 or not (0 < f['tw'] <= 10 and 0 < f['th'] <= 10):
            if any(not m.startswith('equipment ') for m in texes[t]):
                raise SystemExit(f'TEX0 {t:#018x}: {f} is not the PSMT8/PSMT4 CSM1 HIGHLIGHT form')
            left_out[t] = texes.pop(t)
    texels = {t: resident(world, t, lambda lm, t=t: eot.decode(lm, t)) for t in texes}
    data = emot(texels)
    out = assets / 'scene_snow/object_textures.emot'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(data)
    index = []
    for t in sorted(texes):
        f = eot.tex0_fields(t)
        index.append(dict(tex0=hex(t), width=1 << f['tw'], height=1 << f['th'], psm=hex(f['psm']),
                          models=sorted(texes[t]), sha256=hashlib.sha256(texels[t]).hexdigest()[:16]))
    out.with_suffix('.json').write_text(json.dumps(dict(
        count=len(texels), bytes=len(data), source='disc (tools/export_disc_textures.py)', textures=index,
        not_exported=[dict(tex0=hex(t), models=sorted(m)) for t, m in sorted(left_out.items())]),
        indent=1) + '\n')
    return [out]


def page_tex0(elf: bytes, overlay: bytes) -> dict:
    import export_object_textures as eot
    out = {}
    for block in SOURCE_BLOCKS:
        t = struct.unpack_from('<Q', elf_read(elf, block + 0x70, 8))[0] & eot.CLD_MASK
        out.setdefault(t, set()).add(f'001CFBE0 source {block:#010x}')
    t = struct.unpack_from('<Q', overlay, FLAME_FILE_OFFSET + 0x70)[0] & eot.CLD_MASK
    out.setdefault(t, set()).add(f'001D04B0 flame descriptor {FLAME_DESCRIPTOR:#010x}')
    out.setdefault(DECAL_TEX0 & eot.CLD_MASK, set()).add('001F8D30 decal')
    out.setdefault(MARKER_TEX0 & eot.CLD_MASK, set()).add('001F4D40 / 001F4BF0 glow marker')
    return out


def part_page(world: G.GSImage, elf: bytes, assets: Path) -> list:
    """export_page_textures.py's EMOT: the page TEX0 set from the ELF, the
    overlay and the two code constants (the capture exporter adds the TEX0s
    its captured pages draw; test_disc_textures_reference checks both sets
    are the same)."""
    import export_object_textures as eot
    import export_page_textures as ept
    overlay = (DECOMP / 'extract/OVERLAY/AREA11.BIN').read_bytes()
    if overlay[:4] != b'MWo3' or struct.unpack_from('<I', overlay, 8)[0] != 0x823500:
        raise SystemExit('extract/OVERLAY/AREA11.BIN: not the AREA11 overlay at 0x823500')
    texes = page_tex0(elf, overlay)
    for t in texes:
        if not ept.drawable(t):
            raise SystemExit(f'TEX0 {t:#018x} ({sorted(texes[t])}): not a CT32-CLUT PSMT8/PSMT4 TCC 1 texture')
    texels = {t: resident(world, t, lambda lm, t=t: eot.decode(lm, t)) for t in texes}
    data = emot(texels)
    out = assets / 'scene_snow/page_textures.emot'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(data)
    index = []
    for t in sorted(texes):
        f = eot.tex0_fields(t)
        index.append(dict(tex0=hex(t), width=1 << f['tw'], height=1 << f['th'], psm=hex(f['psm']), tfx=f['tfx'],
                          users=sorted(texes[t]), sha256=hashlib.sha256(texels[t]).hexdigest()[:16]))
    out.with_suffix('.json').write_text(json.dumps(dict(
        count=len(texels), bytes=len(data), source='disc (tools/export_disc_textures.py)', textures=index,
        not_exported=[]), indent=1) + '\n')
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
    """export_status_models.py without the capture: the same model files,
    clips and glyph set (its GLYPHS), texels from the rebuilt memory (the
    status-hub capture's GS equals it over every uploaded block)."""
    import export_status_models as esm
    import export_native as en
    import export_props as ep
    from export_opening_actors import OpeningClip
    player = (extract / esm.PLAYER_FILE.split('extract/', 1)[1]).read_bytes()
    bank = (extract / esm.BANK_FILE.split('extract/', 1)[1]).read_bytes()
    library = (extract / esm.LIBRARY_FILE.split('extract/', 1)[1]).read_bytes()
    output = assets / 'status_models'
    output.mkdir(parents=True, exist_ok=True)
    emsk, emsk_count, written = bytearray(), 0, []
    sections, max_slot, tex_table = en.load_mesh_sections(extract / esm.PLAYER_FILE.split('extract/', 1)[1])
    count, nodes = esm.skeleton(player)
    assert count == 21 and max_slot < count
    tex_entries, tex_blob = en.build_texture_blob(None, tex_table, world_freeze)
    parents = [parent for parent, _ in nodes]
    out = output / 'menu_player.emdl'
    en.write_emdl(out, sections, [], parents, [[en.mat_identity()] * count], 30.0, tex_entries, tex_blob, flags=0)
    written.append(out)
    emsk += struct.pack('<II', 0x0028A57C, count)
    for parent, bind in nodes:
        emsk += struct.pack('<i', parent) + bind
    emsk_count += 1
    clips = []
    for cid in esm.CLIPS:
        at = esm.word(bank, 4 + cid * 4)
        clip = OpeningClip(bank, at)
        bones, length, next_clip, blend = struct.unpack_from('<HHhh', bank, at)
        assert bones == 21 and next_clip in (-1, -2) and blend == 0, (hex(cid), next_clip, blend)
        assert esm.word(bank, at + 20) == 0, 'event-bearing clips need a separate event binding'
        assert clip.parents == parents, 'clip hierarchy differs from the model'
        payload = bytearray(struct.pack('<HHhH', cid, length, next_clip, blend))
        for bone in range(bones):
            for channel in (clip.rotation[bone], clip.translation[bone], clip.scale[bone]):
                payload += struct.pack('<I', len(channel.keys))
                for time, values, hold in channel.keys:
                    values = (*values,) + (0.0,) * (4 - len(values))
                    payload += struct.pack('<HH4f', time, int(hold), *values)
        clips.append(payload)
    out = output / 'menu_player.empc'
    out.write_bytes(struct.pack('<4sIII', b'EMPC', 1, 21, len(clips)) + struct.pack('<21i', *parents)
                    + b''.join(clips))
    written.append(out)
    directory = ep.read_directory(library)
    for glyph in esm.GLYPHS:
        offset = directory[glyph]
        count, nodes = esm.skeleton(library[offset:])
        assert count == 1, 'multi-node library models are not exported'
        sections, tex_table, _n = ep.build_placed_mesh(library, {glyph: [ep.lvl.IDENT34]})
        tex_entries, tex_blob = en.build_texture_blob(None, tex_table, world_freeze)
        out = output / f'letter_{glyph:02x}.emdl'
        en.write_emdl(out, sections, [], [-1], [[en.mat_identity()]], 30.0, tex_entries, tex_blob,
                      flags=ep.NORMAL_FLAGS)
        written.append(out)
        emsk += struct.pack('<II', glyph, count)
        for parent, bind in nodes:
            emsk += struct.pack('<i', parent) + bind
        emsk_count += 1
    out = output / 'models.emsk'
    out.write_bytes(struct.pack('<4sII', b'EMSK', 1, emsk_count) + emsk)
    written.append(out)
    return written


def status_model_tex0(extract: Path) -> list:
    """The TEX0 keys the status models sample (for the residency check)."""
    import export_status_models as esm
    import export_native as en
    import export_props as ep
    keys = [f['key'] for f in en.load_mesh_sections(extract / esm.PLAYER_FILE.split('extract/', 1)[1])[2]]
    library = (extract / esm.LIBRARY_FILE.split('extract/', 1)[1]).read_bytes()
    for glyph in esm.GLYPHS:
        keys += [f['key'] for f in ep.build_placed_mesh(library, {glyph: [ep.lvl.IDENT34]})[1]]
    return keys


def atlas(gs: G.GSImage, tokens: list):
    """decode_token_lm of every token (resident), shelf-packed 1024 wide,
    as the three status-page exporters do."""
    from export_ui import decode_token_lm, pack_shelf
    decoded = []
    for token in tokens:
        if token == 0:
            decoded.append((bytes((255, 255, 255, 255)), {'w': 1, 'h': 1}))
            continue
        lo, hi = token & 0xFFFFFFFF, token >> 32
        decoded.append((resident(gs, token, lambda lm: decode_token_lm(lm, lo, hi)[0]),
                        decode_token_lm(bytes(gs.lm), lo, hi)[1]))
    positions, height = pack_shelf([(m['w'], m['h']) for _, m in decoded], 1024)
    return decoded, positions, height


HUB_HOVERS = 5          # the hub's five entries (UI + 0x11 = 0..4), as export_status_hub.py
HUB_INFECTIONS = (0, 100)


def hub_tokens(elf: bytes, states: list | None = None) -> list:
    """export_status_hub.py's token list: the five secondary-weapon icons it
    lists, then every TEX0 the original 00209DF0 passes to its sprite
    worker for hovers 0..4 x infection 0 / 100 and the fixture pass. Here
    the drawer runs over the ELF's initial data instead of the status-hub
    capture's RAM (the test checks the two lists are equal). `states`, if
    given, receives each executed (hover, infection) and 'fixture'."""
    import export_status_hub as H
    from test_player_slide_reference import EE
    ram = bytes(EE(elf).mem)
    states = states if states is not None else []
    layouts = []
    for hover in range(HUB_HOVERS):
        for infection in HUB_INFECTIONS:
            original = H.Original(elf, ram, hover, infection)
            original.run(0x209DF0, (H.UI,))
            layouts.append(original.commands)
            states.append((hover, infection))
    fixture = H.Original(elf, ram, ram[H.UI + 0x11], H.number(struct.unpack_from('<I', ram, 0x81085C)[0]), True)
    fixture.run(0x209DF0, (H.UI,))
    states.append('fixture')
    return list(dict.fromkeys([0x20045EE59D421E40, 0x20045385554221C2, 0x20045305554221A6,
                               0x200451A5554221A2, 0x20045325554221B2] +
                              [c['tex0'] for layout in layouts + [fixture.commands] for c in layout if 'tex0' in c]))


def part_status_hub(world: G.GSImage, elf: bytes, assets: Path, tokens=None, states=None) -> list:
    """`tokens`: a token list to use instead of executing 00209DF0 (the
    quick run of test_disc_textures_reference passes the capture exporter's
    list; its full run checks hub_tokens() equals that list)."""
    tokens = list(tokens) if tokens is not None else hub_tokens(elf, states)
    white_index = len(tokens)
    tokens.append(0)
    decoded, positions, height = atlas(world, tokens)
    pixels = bytearray(1024 * height * 4)
    records = b''
    for token, (data, meta), (x, y) in zip(tokens, decoded, positions):
        w, h = meta['w'], meta['h']
        records += struct.pack('<Q4I', token, x, y, w, h)
        for row in range(h):
            at = ((y + row) * 1024 + x) * 4
            pixels[at:at + w * 4] = data[row * w * 4:(row + 1) * w * 4]
    header = struct.pack('<4s6I', b'EMHA', 1, 1024, height, len(tokens), white_index, len(pixels))
    out = assets / 'scene_snow/panel/status_hub_atlas.emha'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(header + records + pixels)
    return [out]


def part_item_root(gs: G.GSImage, elf: bytes, extract: Path, assets: Path) -> list:
    """export_item_root.py without the capture: its executor already runs
    0020F170 / 0020F2A0 over the ELF image; texels from the rebuilt memory
    with the ITEM root page module 0x1F loaded."""
    import export_item_root as eir
    from export_ui import parse_outer
    layouts = []
    for selection in range(6):
        original = eir.Original(elf, selection)
        original.collect(0x20F170)
        original.collect(0x20F2A0)
        layouts.append(original.commands)
    tokens = list(dict.fromkeys(c['tex0'] for commands in layouts for c in commands if 'tex0' in c))
    white_index = len(tokens)
    tokens.append(0)
    decoded, positions, height = atlas(gs, tokens)
    width = 1024
    sheet = bytearray(width * height * 4)
    sprite_records = bytearray()
    for index, ((pixels, meta), (x, y)) in enumerate(zip(decoded, positions)):
        w, h = meta['w'], meta['h']
        sprite_records += struct.pack('<4IQ', x, y, w, h, tokens[index])
        for row in range(h):
            start = ((y + row) * width + x) * 4
            sheet[start:start + w * 4] = pixels[row * w * 4:(row + 1) * w * 4]
    command_bytes = bytearray()
    for commands in layouts:
        for command in commands:
            if command['kind'] == 'sprite':
                values = (0, command['mode'], tokens.index(command['tex0']), command['x'],
                          command['y'], command['w'], command['h'], command['rgba'])
            elif command['kind'] == 'background':
                values = (1, 0, tokens.index(command['tex0']), 0, 0, 0, 0, 0)
            else:
                values = (2, 1, 0, int(command['x']), int(command['y']), 0, 0, 0)
            command_bytes += struct.pack('<8I', *values)
    counts = [len(commands) for commands in layouts]
    text_source = (extract / 'chunk00/f02_id02.bin').read_bytes()
    directory, _, _, directory_offset = struct.unpack_from('<4I', text_source)
    outer = directory + struct.unpack_from('<I', text_source, directory_offset + 16)[0]
    strings, _ = parse_outer(text_source, outer, 'ITEM help group1')
    text_blob = bytearray()
    for line in range(5):
        offset, _, _, size = struct.unpack_from('<4I', text_source, outer + 16 + line * 16)
        record_base = outer + struct.unpack_from('<I', text_source, outer)[0] + offset
        spans = []
        for index in range(size // 16):
            tag, color, at, _ = struct.unpack_from('<4I', text_source, record_base + index * 16)
            assert tag == 2, 'Unsupported original help markup'
            spans.append((at, struct.unpack_from('<I', elf_read(elf, 0x26EC10 + color * 4, 4))[0]))
        value = strings[line]
        text_blob += struct.pack('<II', len(value) + 1, len(spans))
        text_blob += b''.join(struct.pack('<II', at, color) for at, color in spans)
        text_blob += value + b'\0'
    payload = sprite_records + struct.pack('<6I', *counts) + command_bytes + text_blob + sheet
    header = struct.pack('<4s11I', b'EMIR', 2, width, height, len(tokens),
                         sum(counts), 6, len(payload), 5, len(text_blob), white_index, 0)
    out = assets / 'scene_snow/panel/item_root.emir'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(header + payload)
    return [out]


def part_battery(gs: G.GSImage, elf: bytes, extract: Path, assets: Path) -> list:
    """battery.emba of export_panel.py without the capture: its tokens are
    the ELF tables D_00265C50 (16) and D_00265CD0 (9) and its two listed
    constants; texels from the rebuilt memory with the BATTERY page module
    0x21 loaded."""
    from export_ui import parse_outer
    tokens = [struct.unpack('<Q', elf_read(elf, 0x265C50 + i * 8, 8))[0] for i in range(16)]
    tokens += [struct.unpack('<Q', elf_read(elf, 0x265CD0 + i * 8, 8))[0] for i in range(9)]
    tokens += [0x20042D05A1322000, 0x20043C859D422150]
    tokens.append(0)
    decoded, positions, height = atlas(gs, tokens)
    width = 1024
    sheet = bytearray(width * height * 4)
    records = bytearray()
    for index, ((rgba, meta), (u, v), token) in enumerate(zip(decoded, positions, tokens)):
        w, h = meta['w'], meta['h']
        records += struct.pack('<6IQ', index, u, v, w, h, 0, token)
        for y in range(h):
            start = ((v + y) * width + u) * 4
            sheet[start:start + w * 4] = rgba[y * w * 4:(y + 1) * w * 4]
    data = (extract / 'chunk00/f02_id02.bin').read_bytes()
    directory, _, _, directory_off = struct.unpack_from('<4I', data)
    u32 = lambda a: struct.unpack_from('<I', elf_read(elf, a, 4))[0]  # noqa: E731
    texts = []

    def text_record(source, outer, line, label):
        lines, _ = parse_outer(source, outer, label)
        off, _, _, size = struct.unpack_from('<4I', source, outer + 16 + line * 16)
        record_base = outer + struct.unpack_from('<I', source, outer)[0] + off
        spans = []
        for i in range(size // 16):
            tag, color, at, _ = struct.unpack_from('<4I', source, record_base + i * 16)
            assert tag == 2, 'Unsupported original text markup; do not flatten'
            spans.append((at, u32(0x26EC10 + color * 4)))
        return lines[line], spans
    for group, line in ((5, 0), (5, 8), (5, 9), (3, 27), (3, 28), (3, 29)):
        outer = directory + struct.unpack_from('<I', data, directory_off + group * 16)[0]
        texts.append(text_record(data, outer, line, f'group{group}'))
    global_bank = (extract / 'chunk03/f14_id16.bin').read_bytes()
    texts.append(text_record(global_bank, 0, 0x18, 'global'))
    outer = directory + struct.unpack_from('<I', data, directory_off + 5 * 16)[0]
    texts.append(text_record(data, outer, 0x19, 'group5'))
    outer = directory + struct.unpack_from('<I', data, directory_off + 4 * 16)[0]
    for line in (0x1B, 0x1C, 0x1D):
        texts.append(text_record(data, outer, line, 'group4'))
    text_blob = bytearray()
    for value, spans in texts:
        text_blob += struct.pack('<II', len(value) + 1, len(spans))
        text_blob += b''.join(struct.pack('<II', at, rgb) for at, rgb in spans)
        text_blob += value + b'\0'
    background = elf_read(elf, 0x2655A0, 3 * 32)
    payload = records + text_blob + background + sheet
    header = struct.pack('<4s7I', b'EMBA', 2, width, height, len(tokens), len(texts), len(text_blob), len(payload))
    out = assets / 'scene_snow/panel/battery.emba'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(header + payload)
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
    report = {
        'elf_sha256': G.ELF_SHA256,
        'buffers': [dict(caller=c, source=l, data_dat_offset=hex(o), size=hex(s), extract=w)
                    for c, l, o, s, w in fl.sources],
        'world_steps': [dict(caller=c, source=l, transfers=[dict(dbp=hex(t[0]), dbw=t[1], w=t[4], h=t[5])
                                                            for t in log]) for c, l, log in world.steps],
        'page_states': {k: [dict(caller=c, source=l) for c, l, _ in v.steps[len(world.steps):]]
                        for k, v in pages.items()},
        'hub_states': [list(x) if isinstance(x, tuple) else x for x in hub_states],
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
