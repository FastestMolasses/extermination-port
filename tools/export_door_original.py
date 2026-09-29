#!/usr/bin/env python3
"""Recover AREA11's original door model, source keys and placement descriptor.

The model crosses chunk15 f05/f06/f07. Its initial pose is global bank39
clip0, not model rest. Everything is read from the user's own disc files:
the extract, the AREA11 overlay and the pinned ELF (the door's descriptor
D_002755F0, the destination table D_0024E140 and the sound table
D_0024DB80 are .data the original reads unchanged). The texels come from
the first level's GS memory rebuilt from the disc (tools/
export_disc_textures_gs.py FirstLevel.world(); docs/DISC_TEXTURES.md), or
from --gs FILE (a GS freeze blob); every texture must read only GS blocks
a disc upload writes. No PCSX2 capture is needed.

With --verify-ram FILE (optional; default the first-control capture
../Extermination/build/startup-reference/playable_ee.bin when it exists,
--no-verify skips it) the captured original's door actor is checked
against the exported model, bank, placement and .data. Disc-derived
assets and evidence remain in ignored directories.

Usage (port root): python3 tools/export_door_original.py [--iso FILE | --disc DIR] [--gs FILE]
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]


def mesh_sections(props, data, offset):
    positions, normals, indices, bones, uvs, texture_ids = [], [], [], [], [], []
    textures, welded = [], {}
    texture_id = props.make_tex_of(textures, {})
    for texture, corners, parity in props.model_tris_slots(data, offset):
        tid = texture_id(texture)
        triangle = []
        for position, attribute, uv, bone in corners:
            key = (tuple(position), tuple(attribute[:3]), tuple(uv), bone, tid)
            index = welded.get(key)
            if index is None:
                index = len(positions)
                welded[key] = index
                positions.append(key[0]); normals.append(key[1]); uvs.append(key[2])
                bones.append(bone); texture_ids.append(tid)
            triangle.append(index)
        a, b, c = triangle
        indices.extend((c, b, a) if parity else (c, a, b))
    return [(positions, normals, indices, bones, uvs, texture_ids)], textures


ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
DESCRIPTOR = 0x2755F0         # D_002755F0: the door's {10.0, 8.0} descriptor (+0x30)
DESTINATIONS = 0x24E140       # D_0024E140[area]: the door destination rows
SOUNDS = 0x24DB80             # D_0024DB80[link >> 8]: the door's two sound ids


def verify_ram(ram: bytes, record: bytes, raw_model: bytes, bank: bytes, elf_words: dict) -> int:
    """The captured original's door actor (behaviour 001BC350 in the actor
    list D_00275BC0) holds the exported placement, model and bank, and the
    .data words the export reads from the ELF are unchanged in RAM."""
    u32 = lambda address: struct.unpack_from('<I', ram, address)[0]  # noqa: E731
    actor = u32(0x275bc0)
    doors = []
    while actor:
        if u32(actor + 16) == 0x1bc350: doors.append(actor)
        actor = u32(actor + 0x1c)
    assert len(doors) == 1
    actor = doors[0]
    assert ram[actor + 2:actor + 6] == bytes((0x85, 3, 1, 0))
    assert u32(actor + 0x30) == DESCRIPTOR
    assert record[12:36] == ram[actor + 0xb0:actor + 0xbc] + ram[actor + 0xc0:actor + 0xcc]
    assert raw_model == ram[u32(actor + 0x44):u32(actor + 0x44) + len(raw_model)]
    assert bank == ram[u32(actor + 0x40):u32(actor + 0x40) + len(bank)]
    assert struct.unpack_from('<f', ram, actor + 0x3c)[0] == 150.0
    for address, data in elf_words.items():
        assert ram[address:address + len(data)] == data, hex(address)
    return actor


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--decomp', type=Path, default=ROOT.parent/'Extermination')
    parser.add_argument('--scene', type=Path, default=ROOT/'assets/scene_snow')
    parser.add_argument('--iso', type=Path, help='the disc image (default ../Extermination/Extermination-rebuilt.iso)')
    parser.add_argument('--disc', type=Path, help='a mounted disc or a copy of its DATA/ directory')
    parser.add_argument('--gs', type=Path, help='a GS freeze blob to take the texels from instead of the disc')
    parser.add_argument('--verify-ram', type=Path, help='optional: a captured EE RAM image with the door actor')
    parser.add_argument('--no-verify', action='store_true')
    args = parser.parse_args()
    sys.path.insert(0, str(args.decomp/'tools'))
    sys.path.insert(0, str(ROOT/'tools'))
    from export_opening_actors import OpeningClip
    import export_props as props
    from export_props import table_entry_offset
    from export_level import build_texture_blob
    from export_native import mat_identity, write_emdl
    import export_disc_textures_gs as G

    elf = (args.decomp/'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA256, 'not the pinned SCUS-97112 boot ELF'
    elf_at = lambda address, size: elf[address - 0x100000 + 0x300:address - 0x100000 + 0x300 + size]  # noqa: E731
    overlay = (args.decomp/'extract/OVERLAY/AREA11.BIN').read_bytes()
    bank = (args.decomp/'extract/chunk27/f02_id39.bin').read_bytes()
    area = b''.join((args.decomp/'extract/chunk15'/name).read_bytes() for name in
                   ('f05_id97.bin', 'f06_id98.bin', 'f07_id52.bin'))
    record = overlay[0x82a3c0 - 0x823500:0x82a3c0 - 0x823500 + 40]
    assert len(record) == 40 and struct.unpack_from('<I', record, 36)[0] == 0x1bc350
    flags, subtype, selector, model_id, door_id, _, link = struct.unpack_from('<HBB4H', record)
    assert (flags, subtype, selector, model_id, door_id, link) == (0x85, 3, 0, 0x14, 0, 0x400)
    offset = table_entry_offset(area, 0x5000, model_id)
    node_table = struct.unpack_from('<I', area, offset + 12)[0]
    assert struct.unpack_from('<I', area, offset + 8)[0] == 2
    size = node_table + 2*0x50
    raw_model = area[offset:offset + size]
    assert len(raw_model) == size
    sections, textures = mesh_sections(props, area, offset)
    assert set(sections[0][3]) == {0, 1}
    if args.gs:
        texel_source = args.gs
    else:
        world = G.first_level_world(args.iso, args.disc, args.decomp/'extract')
        G.require_resident(world, textures, 'the AREA11 door')
        texel_source = G.first_level_freeze(ROOT/'build/disc_textures/first_level_gs.bin', world)
    entries, pixels = build_texture_blob(None, textures, p2s=texel_source)
    output = args.scene/'door_original'
    output.mkdir(parents=True, exist_ok=True)
    # The one EMDL identity palette is a format placeholder. Runtime always
    # supplies the actual two node channels and original owner placement.
    write_emdl(output/'model.emdl', sections, [], [-1, 0],
               [[mat_identity(), mat_identity()]], 60, entries, pixels, flags=0)
    payload = bytearray(struct.pack('<4sIII2i', b'EMPC', 1, 2, 4, -1, 0))
    clips = []
    for clip_id, length in enumerate((150, 200, 150, 200)):
        header = struct.unpack_from('<I', bank, 4 + clip_id*4)[0] & ~3
        assert struct.unpack_from('<HHhh', bank, header) == (2, length, -2, 0)
        assert struct.unpack_from('<I', bank, header + 20)[0] == 0
        clip = OpeningClip(bank, header)
        assert clip.parents == [-1, 0]
        payload += struct.pack('<HHhH', clip_id, length, -2, 0)
        keys = 0
        for bone in range(2):
            for channel in (clip.rotation[bone], clip.translation[bone], clip.scale[bone]):
                keys += len(channel.keys)
                payload += struct.pack('<I', len(channel.keys))
                for time, values, hold in channel.keys:
                    payload += struct.pack('<HH4f', time, int(hold), *values,
                                           *((0.0,)*(4-len(values))))
        clips.append(dict(id=clip_id, header=header, duration=length, keys=keys))
    (output/'channels.empc').write_bytes(payload)
    descriptor = elf_at(DESCRIPTOR, 8)
    assert struct.unpack('<2f', descriptor) == (10.0, 8.0)
    row = struct.unpack('<I', elf_at(DESTINATIONS + 11*4, 4))[0] + (door_id & 0x7f)*4
    destination = elf_at(row, 4)
    sound_row = SOUNDS + ((link >> 8) & 255)*4
    sounds = elf_at(sound_row, 4)
    metadata = struct.pack('<4s7I', b'EMDO', 1, 0x82a3c0, 0x1bc350,
                           model_id, link, door_id, 0x39)
    metadata += record[12:36] + descriptor + destination + sounds
    assert len(metadata) == 72
    (output/'source.emdo').write_bytes(metadata)
    reference = None
    ram_path = args.verify_ram or args.decomp/'build/startup-reference/playable_ee.bin'
    if not args.no_verify and ram_path.is_file():
        actor = verify_ram(ram_path.read_bytes(), record, raw_model, bank,
                           {DESCRIPTOR: descriptor, DESTINATIONS + 11*4: elf_at(DESTINATIONS + 11*4, 4),
                            row: destination, sound_row: sounds})
        reference = dict(ram=str(ram_path), original_actor=hex(actor), whole_model_matches_ram=True,
                         whole_bank_matches_ram=True, elf_words_match_ram=True)
    report = dict(source_id='0082A3C0', texels=str(texel_source),
        model_offset=offset, model_bytes=size, model_sha256=hashlib.sha256(raw_model).hexdigest(),
        bank_sha256=hashlib.sha256(bank).hexdigest(), clips=clips,
        initial_remaining=150, descriptor=struct.unpack('<2f', descriptor),
        destination=list(destination), sounds=struct.unpack('<2H', sounds),
        vertices=len(sections[0][0]), triangles=len(sections[0][2])//3, capture_check=reference,
        mesh_attributes='original normals and node indices; no baked guessed lighting')
    build = ROOT/'build/door_original_reference'
    build.mkdir(parents=True, exist_ok=True)
    (build/'export.json').write_text(json.dumps(report, indent=2)+'\n')
    print(f'Original AREA11 door: model {size} bytes, four raw clips, metadata -> {output}'
          + (' (checked against the capture)' if reference else ''))


if __name__ == '__main__': main()
