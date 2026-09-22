#!/usr/bin/env python3
"""Recover AREA11's original door model, source keys and placement descriptor.

The model crosses chunk15 f05/f06/f07. Its initial pose is global bank39
clip0, not model rest. Whole-source RAM comparisons precede every export;
the shared texture decoder supplies the corrected PS2 palette lookup.
Disc-derived assets and evidence remain in ignored directories.
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--decomp', type=Path, default=ROOT.parent/'Extermination')
    parser.add_argument('--scene', type=Path, default=ROOT/'assets/scene_snow')
    args = parser.parse_args()
    sys.path.insert(0, str(args.decomp/'tools'))
    from export_opening_actors import OpeningClip
    import export_props as props
    from export_props import table_entry_offset
    from export_level import build_texture_blob
    from export_native import mat_identity, write_emdl

    ram = (args.decomp/'build/startup-reference/playable_ee.bin').read_bytes()
    overlay = (args.decomp/'extract/OVERLAY/AREA11.BIN').read_bytes()
    bank = (args.decomp/'extract/chunk27/f02_id39.bin').read_bytes()
    area = b''.join((args.decomp/'extract/chunk15'/name).read_bytes() for name in
                   ('f05_id97.bin', 'f06_id98.bin', 'f07_id52.bin'))
    u32 = lambda address: struct.unpack_from('<I', ram, address)[0]
    actor = u32(0x275bc0)
    doors = []
    while actor:
        if u32(actor + 16) == 0x1bc350: doors.append(actor)
        actor = u32(actor + 0x1c)
    assert len(doors) == 1
    actor = doors[0]
    assert ram[actor + 2:actor + 6] == bytes((0x85, 3, 1, 0))
    assert u32(actor + 0x30) == 0x2755f0
    record = overlay[0x82a3c0 - 0x823500:0x82a3c0 - 0x823500 + 40]
    assert len(record) == 40 and struct.unpack_from('<I', record, 36)[0] == 0x1bc350
    flags, subtype, selector, model_id, door_id, _, link = struct.unpack_from('<HBB4H', record)
    assert (flags, subtype, selector, model_id, door_id, link) == (0x85, 3, 0, 0x14, 0, 0x400)
    assert record[12:36] == ram[actor + 0xb0:actor + 0xbc] + ram[actor + 0xc0:actor + 0xcc]
    offset = table_entry_offset(area, 0x5000, model_id)
    node_table = struct.unpack_from('<I', area, offset + 12)[0]
    assert struct.unpack_from('<I', area, offset + 8)[0] == 2
    size = node_table + 2*0x50
    raw_model = area[offset:offset + size]
    assert len(raw_model) == size and raw_model == ram[u32(actor + 0x44):u32(actor + 0x44) + size]
    assert bank == ram[u32(actor + 0x40):u32(actor + 0x40) + len(bank)]
    assert struct.unpack_from('<f', ram, actor + 0x3c)[0] == 150.0
    sections, textures = mesh_sections(props, area, offset)
    assert set(sections[0][3]) == {0, 1}
    entries, pixels = build_texture_blob(None, textures,
        p2s=args.decomp/'build/startup-reference/opening_gs.bin')
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
    descriptor = ram[0x2755f0:0x2755f8]
    assert struct.unpack('<2f', descriptor) == (10.0, 8.0)
    destination = u32(0x24e140 + 11*4) + (door_id & 0x7f)*4
    sounds = ram[0x24db80 + ((link >> 8) & 255)*4:0x24db84 + ((link >> 8) & 255)*4]
    metadata = struct.pack('<4s7I', b'EMDO', 1, 0x82a3c0, 0x1bc350,
                           model_id, link, door_id, 0x39)
    metadata += record[12:36] + descriptor + ram[destination:destination + 4] + sounds
    assert len(metadata) == 72
    (output/'source.emdo').write_bytes(metadata)
    report = dict(source_id='0082A3C0', original_actor=hex(actor),
        model_offset=offset, model_bytes=size, model_sha256=hashlib.sha256(raw_model).hexdigest(),
        bank_sha256=hashlib.sha256(bank).hexdigest(), reference_bank_address=hex(u32(actor+0x40)),
        whole_model_matches_ram=True, whole_bank_matches_ram=True, clips=clips,
        initial_remaining=150, descriptor=struct.unpack('<2f', descriptor),
        destination=list(ram[destination:destination + 4]), sounds=struct.unpack('<2H', sounds),
        vertices=len(sections[0][0]), triangles=len(sections[0][2])//3,
        mesh_attributes='original normals and node indices; no baked guessed lighting')
    build = ROOT/'build/door_original_reference'
    build.mkdir(parents=True, exist_ok=True)
    (build/'export.json').write_text(json.dumps(report, indent=2)+'\n')
    print(f'Original AREA11 door: model {size} bytes, four raw clips, metadata -> {output}')


if __name__ == '__main__': main()
