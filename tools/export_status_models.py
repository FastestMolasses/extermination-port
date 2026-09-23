#!/usr/bin/env python3
"""Export the status hub's 3D models from the user's own disc files.

The hub 0020CDC0 draws, through its static actor pool D_0028B020:
  - the menu player 0020E6F0: model D_0028A57C (variant D_008104E4 0,
    costume D_00810C60 0) = extract/chunk28/f00_id3b.bin, animated with
    clips 0x1C2 (health > 35) and 0x0A (health <= 35) of the player bank
    D_0028A580 = extract/chunk28/f01_id3c.bin;
  - the equipment letter models 0020E1E0: 001C6120(D_0028A56C, glyph) over
    the equipment library D_0028A56C = extract/chunk27/f01_id37.bin.

The status-hub capture (../Extermination/build/startup-reference/status-hub)
ties both to the running original: its RAM holds the same bytes at the
captured D_0028A57C / D_0028A580 / D_0028A56C, and its pool records 0..6
bind exactly these models. Texels come from that capture's GS memory
(gs.bin), where the hub itself had them resident.

Only the models the capture shows are exported: the menu player of
variant 0 / costume 0 and the single-node glyph models the captured
inventory spawns ('/', '@', '0', '1', '2', '8'). Any other selection is a
missing asset the port faults on (em_status_models.c).

Writes (ignored, disc-derived):
  assets/status_models/menu_player.emdl   mesh + texels, 21 nodes
  assets/status_models/menu_player.empc   clips 0x1C2 and 0x0A (keys)
  assets/status_models/letter_XX.emdl     glyph model XX
  assets/status_models/models.emsk        each model's byte +8 and its
                                          skeleton records (parent, bind)
The report build/status_models_export/export.json holds hashes and counts.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
CAPTURE = DECOMP / 'build/startup-reference/status-hub'

PLAYER_FILE = 'extract/chunk28/f00_id3b.bin'   # D_0028A57C
BANK_FILE = 'extract/chunk28/f01_id3c.bin'     # D_0028A580
LIBRARY_FILE = 'extract/chunk27/f01_id37.bin'  # D_0028A56C
CLIPS = (0x1C2, 0x0A)                          # 0020E6F0's two clips
# The glyph models the status-hub capture's pool records 1..6 bind
# (CA4..CA7 = FF 05 00 07 through 0020E250 / 0020E3A0).
GLYPHS = (0x2F, 0x40, 0x30, 0x31, 0x32, 0x38)
POOL, STRIDE = 0x28B020, 0x2F0


def word(data, at):
    return struct.unpack_from('<I', data, at)[0]


def skeleton(model: bytes):
    """001C6150 (the byte at model + 8) and the records bone_init_default_1
    reads at model + *(model + 0xC) + 0x50 * i (parent +4, bind +0x10)."""
    count = model[8]
    base = word(model, 0xC)
    nodes = []
    for i in range(count):
        record = base + 0x50 * i
        parent = struct.unpack_from('<h', model, record + 4)[0]
        bind = model[record + 0x10:record + 0x50]
        assert len(bind) == 64
        nodes.append((parent, bind))
    return count, nodes


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', type=Path, default=ROOT / 'assets/status_models')
    args = ap.parse_args()
    sys.path.insert(0, str(DECOMP / 'tools'))
    import export_native as en
    import export_props as ep
    from export_opening_actors import OpeningClip

    ram = (CAPTURE / 'eeMemory.bin').read_bytes()
    gs = CAPTURE / 'gs.bin'
    player = (DECOMP / PLAYER_FILE).read_bytes()
    bank = (DECOMP / BANK_FILE).read_bytes()
    library = (DECOMP / LIBRARY_FILE).read_bytes()
    report = {'capture': str(CAPTURE.relative_to(DECOMP)), 'models': []}

    # The captured globals point at exactly these files.
    a57c, a580, a56c = word(ram, 0x28A57C), word(ram, 0x28A580), word(ram, 0x28A56C)
    assert ram[0x8104E4] == 0 and ram[0x810C60] == 0, 'capture is not variant 0 / costume 0'
    assert ram[a57c:a57c + len(player)] == player, 'D_0028A57C is not f00_id3b'
    assert ram[a580:a580 + len(bank)] == bank, 'D_0028A580 is not f01_id3c'
    assert word(ram, POOL + 0x44) == a57c and word(ram, POOL + 0x40) == a580

    args.output.mkdir(parents=True, exist_ok=True)
    emsk = bytearray()
    emsk_count = 0

    # ---- the menu player (pool record 0)
    sections, max_slot, tex_table = en.load_mesh_sections(DECOMP / PLAYER_FILE)
    count, nodes = skeleton(player)
    assert count == 21 and max_slot < count
    tex_entries, tex_blob = en.build_texture_blob(None, tex_table, gs)
    parents = [parent for parent, _ in nodes]
    frames = [[en.mat_identity()] * count]
    out = args.output / 'menu_player.emdl'
    en.write_emdl(out, sections, [], parents, frames, 30.0, tex_entries, tex_blob, flags=0)
    emsk += struct.pack('<II', 0x0028A57C, count)
    for parent, bind in nodes:
        emsk += struct.pack('<i', parent) + bind
    emsk_count += 1
    report['models'].append({'name': 'menu_player', 'source': PLAYER_FILE, 'runtime': f'{a57c:08X}',
                             'nodes': count, 'textures': len(tex_entries),
                             'sha256': hashlib.sha256(out.read_bytes()).hexdigest()})

    # ---- its clips (same EMPC layout as export_player_pose_channels.py)
    clips = []
    clip_report = []
    clip_parents = None
    for cid in CLIPS:
        at = word(bank, 4 + cid * 4)
        clip = OpeningClip(bank, at)
        bones, length, next_clip, blend = struct.unpack_from('<HHhh', bank, at)
        assert bones == 21 and next_clip in (-1, -2) and blend == 0, (hex(cid), next_clip, blend)
        assert word(bank, at + 20) == 0, 'event-bearing clips need a separate event binding'
        if clip_parents is None:
            clip_parents = clip.parents
        assert clip.parents == clip_parents == parents, 'clip hierarchy differs from the model'
        payload = bytearray(struct.pack('<HHhH', cid, length, next_clip, blend))
        keys = 0
        for bone in range(bones):
            for channel in (clip.rotation[bone], clip.translation[bone], clip.scale[bone]):
                payload += struct.pack('<I', len(channel.keys))
                keys += len(channel.keys)
                for time, values, hold in channel.keys:
                    values = (*values,) + (0.0,) * (4 - len(values))
                    payload += struct.pack('<HH4f', time, int(hold), *values)
        clips.append(payload)
        clip_report.append({'clip': cid, 'frames': length, 'next': next_clip, 'keys': keys})
    empc = struct.pack('<4sIII', b'EMPC', 1, 21, len(clips)) + struct.pack('<21i', *parents) + \
        b''.join(clips)
    (args.output / 'menu_player.empc').write_bytes(empc)
    report['clips'] = clip_report

    # ---- the glyph models (pool records 1..6)
    captured = {}
    for i in range(1, 24):
        record = POOL + i * STRIDE
        if ram[record] and word(ram, record + 0x10) == 0x0020E460:
            captured[ram[record + 0xD]] = word(ram, record + 0x44)
    assert sorted(captured) == sorted(GLYPHS), ('captured glyphs', sorted(captured))
    directory = ep.read_directory(library)
    for glyph in GLYPHS:
        offset = directory[glyph]
        model_address = a56c + offset
        assert captured[glyph] == model_address, 'record +0x44 is not 001C6120(D_0028A56C, glyph)'
        size = word(library, offset + 0xC)
        count, nodes = skeleton(library[offset:])
        end = offset + size + 0x50 * count
        assert ram[model_address:model_address + (end - offset)] == library[offset:end]
        assert count == 1, 'multi-node library models are not exported'
        sections, tex_table, _n = ep.build_placed_mesh(library, {glyph: [ep.lvl.IDENT34]})
        tex_entries, tex_blob = en.build_texture_blob(None, tex_table, gs)
        out = args.output / f'letter_{glyph:02x}.emdl'
        en.write_emdl(out, sections, [], [-1], [[en.mat_identity()]], 30.0, tex_entries, tex_blob,
                      flags=ep.NORMAL_FLAGS)
        emsk += struct.pack('<II', glyph, count)
        for parent, bind in nodes:
            emsk += struct.pack('<i', parent) + bind
        emsk_count += 1
        report['models'].append({'name': f'letter_{glyph:02x}', 'source': LIBRARY_FILE,
                                 'offset': offset, 'runtime': f'{model_address:08X}',
                                 'nodes': count, 'textures': len(tex_entries),
                                 'sha256': hashlib.sha256(out.read_bytes()).hexdigest()})
    (args.output / 'models.emsk').write_bytes(struct.pack('<4sII', b'EMSK', 1, emsk_count) + emsk)

    out = ROOT / 'build/status_models_export'
    out.mkdir(parents=True, exist_ok=True)
    (out / 'export.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'status models: menu player + {len(GLYPHS)} glyph models -> {args.output}')


if __name__ == '__main__':
    main()
