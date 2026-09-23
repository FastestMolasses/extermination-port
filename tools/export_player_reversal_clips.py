#!/usr/bin/env python3
"""Export the reversal-skid player clips 6/7/8/9 (WP-15/H11).

0017C030 case 7 requests D_00248AB0[2|4][row] and case 6 requests
D_00248AB0[3|5][row]; for the healthy row 0 these are clips 6/7 (15 frames,
non-looping) and 8/9 (1 frame). This tool appends their decoded original
node keys to the EMPC source bank and their channel-derived palettes to the
EMDL display model. Every existing byte of both files is preserved.

By default the results go to ignored build/player_reversal_export/. Pass
--install to replace assets/player_channels.empc and assets/player.emdl
(backups are kept beside the results). Tests that pin the EMPC clip count
(tools/test_pose_bank_reference.py) must be updated when installing.
No original data is embedded in this script.
"""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CLIPS = (6, 7, 8, 9)
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'

BRIDGE = r'''
#include "game/em_player_pose.h"
static EmPoseBank bank;
static EmPlayerPose pose;
int load(const char *path) { return em_pose_bank_load(&bank, path); }
int start(unsigned clip) { return em_player_pose_init(&pose, &bank, clip, 0); }
int next_frame(float *out) {
    if (!em_player_pose_palette(&pose, out, 22)) return 0;
    return em_player_pose_advance(&pose, 1, 0);
}
'''


def sha(data):
    return hashlib.sha256(data).hexdigest()


def encode(bank, clip_id, clip_class):
    """The export_player_pose_channels.py payload for one original clip."""
    at = struct.unpack_from('<I', bank, 4 + clip_id * 4)[0]
    clip = clip_class(bank, at)
    bones, length, next_clip, blend = struct.unpack_from('<HHhh', bank, at)
    assert bones == 21 and next_clip in (-1, -2) and blend == 0
    assert struct.unpack_from('<I', bank, at + 20)[0] == 0, 'event-bearing clip'
    payload = bytearray(struct.pack('<HHhH', clip_id, length, next_clip, blend))
    for bone in range(bones):
        for channel in (clip.rotation[bone], clip.translation[bone], clip.scale[bone]):
            payload += struct.pack('<I', len(channel.keys))
            for time, values, hold in channel.keys:
                values = (*values,) + (0.0,) * (4 - len(values))
                payload += struct.pack('<HH4f', time, int(hold), *values)
    return bytes(payload), clip.parents, {'clip': clip_id, 'frames': length,
                                          'next': next_clip, 'source_header': at}


def empc_clips(data):
    """(id, start, end) of each payload in an EMPC file."""
    magic, version, bones, count = struct.unpack_from('<4sIII', data)
    assert (magic, version, bones) == (b'EMPC', 1, 21)
    cursor = 16 + bones * 4
    result = []
    for _ in range(count):
        start = cursor
        clip_id = struct.unpack_from('<H', data, cursor)[0]
        cursor += 8
        for _ in range(bones * 3):
            keys = struct.unpack_from('<I', data, cursor)[0]
            cursor += 4 + keys * 20
        result.append((clip_id, start, cursor))
    assert cursor == len(data)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--decomp', type=Path, default=ROOT.parent / 'Extermination')
    parser.add_argument('--player', type=Path, default=ROOT / 'assets/player.emdl')
    parser.add_argument('--channels', type=Path, default=ROOT / 'assets/player_channels.empc')
    parser.add_argument('--install', action='store_true',
                        help='replace --player/--channels in place (backups kept)')
    args = parser.parse_args()
    sys.path.insert(0, str(args.decomp / 'tools'))
    from export_opening_actors import OpeningClip

    elf = (args.decomp / 'config/SCUS_971.12').read_bytes()
    assert sha(elf) == ELF_SHA256, 'wrong original executable'
    bank = (args.decomp / 'extract/chunk28/f01_id3c.bin').read_bytes()
    # 0017B490 row 0 for commands 2..5 (D_00248AB0 -> D_00248A38/48/58/68).
    table = [struct.unpack_from('<I', elf, 0x248AB0 + 4 * command - 0x100000 + 0x300)[0]
             for command in range(2, 6)]
    row0 = [struct.unpack_from('<h', elf, pointer - 0x100000 + 0x300)[0] for pointer in table]
    assert sorted(row0) == list(CLIPS), row0

    channels = args.channels.read_bytes()
    present = empc_clips(channels)
    assert not {c[0] for c in present} & set(CLIPS), 'EMPC already has reversal clips'
    # The encoder must reproduce an existing payload byte-for-byte.
    first_id, first_start, first_end = present[0]
    assert encode(bank, first_id, OpeningClip)[0] == channels[first_start:first_end], \
        'channel encoder differs from the existing export'
    payloads, report_clips = [], []
    for clip_id in CLIPS:
        payload, parents, info = encode(bank, clip_id, OpeningClip)
        assert tuple(parents) == struct.unpack_from('<21i', channels, 16), 'rig mismatch'
        payloads.append(payload); report_clips.append(info)
    new_channels = (struct.pack('<4sIII', b'EMPC', 1, 21, len(present) + len(CLIPS)) +
                    channels[16:] + b''.join(payloads))
    assert new_channels[16:len(channels)] == channels[16:]

    data = args.player.read_bytes()
    magic, bones, verts, indices, frames, fps, textures, flags, nclips = \
        struct.unpack_from('<4sIIIIfIII', data)
    assert (magic, bones, flags) == (b'EMD3', 22, 0)
    clips_at = 36 + bones * 4 + textures * 16
    mesh_at = clips_at + nclips * 16
    palette_at = mesh_at + verts * 40 + indices * 4
    texture_at = palette_at + frames * bones * 64
    entries = [struct.unpack_from('<IIIf', data, clips_at + 16 * i) for i in range(nclips)]
    assert not {e[0] for e in entries} & set(CLIPS), 'EMDL already has reversal clips'

    out = ROOT / 'build/player_reversal_export'
    out.mkdir(parents=True, exist_ok=True)
    staged_channels = out / 'player_channels.empc'
    staged_channels.write_bytes(new_channels)
    extra = bytearray(); table_entries = b''; validation = {}
    with tempfile.TemporaryDirectory(prefix='reversal_pose_export_') as folder:
        source = Path(folder) / 'bridge.c'
        library = Path(folder) / 'pose.dylib'
        source.write_text(BRIDGE)
        subprocess.run(['cc', '-std=c11', '-O2', '-ffp-contract=off', '-shared', '-fPIC',
                        '-I' + str(ROOT / 'src'), str(source),
                        str(ROOT / 'src/game/em_player_pose.c'),
                        str(ROOT / 'src/game/em_pose_bank.c'),
                        str(ROOT / 'src/game/em_pose_transition.c'), '-lm', '-o', str(library)],
                       check=True)
        native = C.CDLL(str(library))
        native.load.argtypes = [C.c_char_p]
        native.start.argtypes = [C.c_uint]
        native.next_frame.argtypes = [C.POINTER(C.c_float)]
        assert native.load(str(staged_channels).encode()), 'extended EMPC does not load'
        palette = (C.c_float * (22 * 16))()

        def frames_of(clip_id, count):
            assert native.start(clip_id)
            blob = bytearray()
            for _ in range(count):
                assert native.next_frame(palette)
                blob += bytes(palette)
            return bytes(blob)

        # Frame-alignment check against the existing stop clips 4/5 (decomp
        # baker output): every regenerated frame must be closest to the
        # same-index existing frame. The palettes themselves come from the
        # original-instruction channel core (as for pickup clips 40-42), not
        # from that baker; their difference is reported, not asserted.
        stride = bones * 16
        for clip_id in (4, 5):
            entry = next(e for e in entries if e[0] == clip_id)
            regenerated = frames_of(clip_id, entry[2])
            existing = data[palette_at + entry[1] * bones * 64:
                            palette_at + (entry[1] + entry[2]) * bones * 64]
            a = struct.unpack(f'<{len(regenerated) // 4}f', regenerated)
            b = struct.unpack(f'<{len(existing) // 4}f', existing)
            def distance(f, g):
                return max(abs(x - y) for x, y in zip(a[f * stride:(f + 1) * stride],
                                                     b[g * stride:(g + 1) * stride]))
            worst = 0.0
            for f in range(entry[2]):
                same = distance(f, f); worst = max(worst, same)
                for g in (f - 1, f + 1):
                    if 0 <= g < entry[2]:
                        assert same <= distance(f, g), ('frame alignment', clip_id, f, g)
            validation[f'clip{clip_id}_max_abs_difference_vs_baker'] = worst
        cursor = frames
        for info in report_clips:
            blob = frames_of(info['clip'], info['frames'])
            extra += blob
            table_entries += struct.pack('<IIIf', info['clip'], cursor, info['frames'], 60.0)
            cursor += info['frames']
    added = cursor - frames
    header = bytearray(data[:36])
    struct.pack_into('<I', header, 16, frames + added)
    struct.pack_into('<I', header, 32, nclips + len(CLIPS))
    result = (bytes(header) + data[36:mesh_at] + table_entries +
              data[mesh_at:texture_at] + bytes(extra) + data[texture_at:])
    shift = len(table_entries)
    assert result[36:mesh_at] == data[36:mesh_at]
    assert result[mesh_at + shift:texture_at + shift] == data[mesh_at:texture_at]
    assert result[texture_at + shift + len(extra):] == data[texture_at:]
    staged_player = out / 'player.emdl'
    staged_player.write_bytes(result)

    report = {'row0_clips': row0, 'clips': report_clips, 'validation': validation,
              'channels_before_sha256': sha(channels), 'channels_after_sha256': sha(new_channels),
              'player_before_sha256': sha(data), 'player_after_sha256': sha(result),
              'preserved_channel_clips': len(present), 'preserved_model_clips': nclips,
              'added_model_frames': added, 'installed': False,
              'boundary': 'Node 0 is identity in em_player_pose_palette; the root '
                          'translation keys of clips 6/7 are retained in the EMPC only.'}
    if args.install:
        for staged, target, name in ((staged_channels, args.channels, 'channels'),
                                     (staged_player, args.player, 'player')):
            backup = out / f'{target.name}.before_reversal'
            if not backup.exists():
                shutil.copyfile(target, backup)
            temporary = target.with_name(target.name + '.reversal.tmp')
            shutil.copyfile(staged, temporary)
            temporary.replace(target)
        report['installed'] = True
    (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print('reversal clip export PASS:', json.dumps(report))


if __name__ == '__main__':
    main()
