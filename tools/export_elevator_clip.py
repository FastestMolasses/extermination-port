#!/usr/bin/env python3
"""Replace the lever clip47 palettes using original stateful channel sampling.

Requires the owner's original animation bank and captured lever-animation EE
state. The capture must match the source bank and all21 world-space bone
matrices before any asset is changed. Other clips, mesh, textures, parent and
clip tables remain byte-identical. Generated assets and reports stay ignored.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
CLIP_ID = 0x47
ACTOR = 0x8102B0
BANK = 0xD689C0


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--decomp', type=Path, default=ROOT.parent/'Extermination')
    parser.add_argument('--player', type=Path, default=ROOT/'assets/player.emdl')
    parser.add_argument('--reference-ee', type=Path)
    args = parser.parse_args()
    sys.path.insert(0, str(args.decomp/'tools'))
    from export_opening_actors import OpeningClip, matrix_multiply, original_palette
    from export_native import mat_identity

    bank = (args.decomp/'extract/chunk28/f01_id3c.bin').read_bytes()
    reference = args.reference_ee or args.decomp/'build/startup-reference/elevator/clip47_ee.bin'
    ram = reference.read_bytes()
    assert struct.unpack_from('<I', ram, ACTOR+0x40)[0] == BANK
    assert ram[BANK:BANK+len(bank)] == bank, 'Original animation bank mismatch'
    assert struct.unpack_from('<H', ram, ACTOR+0x20C)[0] == CLIP_ID
    header = struct.unpack_from('<I', bank, 4+CLIP_ID*4)[0]
    clip = OpeningClip(bank, header)
    assert (clip.bones, clip.length) == (21, 200)
    owner = [struct.unpack_from('<4f', ram, ACTOR+0xD0+c*16) for c in range(4)]
    assert tuple(owner[3][:3]) == struct.unpack_from('<3f', ram, ACTOR+0xA0)
    expected = original_palette(ram, ACTOR, clip.bones)

    def world_error(palette):
        world = [matrix_multiply(owner, matrix) for matrix in palette]
        return max(abs(a-b) for original, matrix in zip(expected, world)
                   for a, b in zip(original, [v for column in matrix for v in column]))

    frames = []
    errors = []
    for frame in range(clip.length):
        assert clip.translation[0].value == (0.0, 0.0, 0.0), 'Unexpected root motion'
        palette = clip.palette()
        frames.append(palette)
        errors.append(world_error(palette))
        hold = False
        for rotation, translation, scale in zip(clip.rotation, clip.translation, clip.scale):
            rotation.advance(1.0)
            translation.advance(1.0)
            flag = scale.advance(1.0)
            if flag is not None:
                hold = flag
        if hold:
            for rotation, translation, scale in zip(clip.rotation, clip.translation, clip.scale):
                rotation.reciprocal = 0.0
                translation.velocity = scale.velocity = (0.0, 0.0, 0.0)
    nearest = min(range(clip.length), key=errors.__getitem__)
    remaining = struct.unpack_from('<f', ram, ACTOR+0x3C)[0]
    assert nearest == 39 and remaining == 161.0, 'Unexpected reference cursor'
    assert errors[nearest] <= 0.0001, ('Original bone palettes differ', errors[nearest])

    data = args.player.read_bytes()
    magic, bones, verts, indices, nframes, fps, textures, flags, nclips = struct.unpack_from(
        '<4sIIIIfIII', data)
    assert magic == b'EMD3' and bones == 22 and flags == 0
    assert struct.unpack_from('<22i', data, 36) == tuple(clip.parents)+(-1,)
    clips_at = 36+bones*4+textures*16
    mesh_at = clips_at+nclips*16
    palettes_at = mesh_at+verts*40+indices*4
    textures_at = palettes_at+nframes*bones*64
    assert textures_at <= len(data)
    entries = [struct.unpack_from('<IIIf', data, clips_at+i*16) for i in range(nclips)]
    selected = [entry for entry in entries if entry[0] == CLIP_ID]
    assert len(selected) == 1 and selected[0][2:] == (200, 60.0)
    _, first_frame, count, _ = selected[0]
    assert first_frame+count <= nframes
    for entry in entries:
        if entry[0] != CLIP_ID:
            assert entry[1]+entry[2] <= first_frame or entry[1] >= first_frame+count, 'Overlapping clip'
    start = palettes_at+first_frame*bones*64
    replacement = b''.join(struct.pack('<16f', *[v for col in matrix for v in col])
                           for frame in frames for matrix in [*frame, mat_identity()])
    end = start+len(replacement)
    assert end <= textures_at and len(replacement) == count*bones*64
    old_palette = [[struct.unpack_from('<4f', data,
                    start+nearest*bones*64+bone*64+column*16) for column in range(4)]
                   for bone in range(clip.bones)]
    previous_error = world_error(old_palette)
    result = data[:start]+replacement+data[end:]
    assert len(result) == len(data) and result[:start] == data[:start] and result[end:] == data[end:]

    output = ROOT/'build/elevator_clip_export'
    output.mkdir(parents=True, exist_ok=True)
    changed = result != data
    if changed:
        backup = output/'player_before_clip47.emdl'
        if not backup.exists():
            backup.write_bytes(data)
        staged = args.player.with_name(args.player.name+'.clip47.tmp')
        staged.write_bytes(result)
        staged.replace(args.player)
    report = {
        'status': 'PASS', 'changed': changed, 'clip': CLIP_ID, 'frames': count,
        'source_bank_header': header, 'source_bank_sha256': sha256(bank),
        'reference_ee_sha256': sha256(ram), 'reference_cursor': nearest,
        'reference_remaining_time': remaining, 'max_matrix_error': errors[nearest],
        'previous_max_matrix_error': previous_error,
        'before_sha256': sha256(data), 'after_sha256': sha256(result),
        'preserved_other_clips': nclips-1, 'preserved_other_frames': nframes-count,
        'mesh_sha256': sha256(data[mesh_at:palettes_at]),
        'textures_sha256': sha256(data[textures_at:]),
        'changed_byte_count': sum(a != b for a, b in zip(data[start:end], replacement)),
        'root_motion': 'zero; actor world placement preserved',
        'limits': 'Captured frame39 validates21 world matrices; native transition blending remains separate.'
    }
    report_path = output/('result.json' if changed else 'idempotence.json')
    report_path.write_text(json.dumps(report, indent=2)+'\n')
    print(f'Lever clip47 {"replaced" if changed else "already verified"}: '
          f'{count} frames,21 original bones; captured matrix error {errors[nearest]:.9g}; '
          f'{nclips-1} other clips and all mesh/texture bytes preserved')


if __name__ == '__main__':
    main()
