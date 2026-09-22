#!/usr/bin/env python3
"""Replace door43/45 palettes from the validated original node-channel bank.

Original header and release/rate rows are checked before changing the asset.
The matching original whole player bank is verified against an immutable EE
capture. Other animation slots, geometry, texture bytes and all tables remain
unchanged. No captured door-player matrix sequence is claimed by this exporter.
"""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CLIPS = {0x43: 0x57AF0, 0x45: 0x5D320}

BRIDGE = r'''
#include "game/em_player_pose.h"
static EmPoseBank bank;
static EmPlayerPose pose;
int load(const char *path) { return em_pose_bank_load(&bank, path); }
int start(unsigned clip) { return em_player_pose_init(&pose, &bank, clip, 0); }
int next_frame(float *out) {
    for (unsigned axis = 0; axis < 3; ++axis)
        if (pose.channels[0].translation[axis] != 0) return 0;
    if (!em_player_pose_palette(&pose, out, 22)) return 0;
    return em_player_pose_advance(&pose, 1, 0);
}
'''


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--decomp', type=Path, default=ROOT.parent / 'Extermination')
    parser.add_argument('--player', type=Path, default=ROOT / 'assets/player.emdl')
    parser.add_argument('--channels', type=Path, default=ROOT / 'assets/player_channels.empc')
    args = parser.parse_args()
    bank = (args.decomp / 'extract/chunk28/f01_id3c.bin').read_bytes()
    ram = (args.decomp / 'build/startup-reference/panel/eeMemory.bin').read_bytes()
    assert ram[0xD689C0:0xD689C0 + len(bank)] == bank
    elf = (args.decomp / 'config/SCUS_971.12').read_bytes()
    headers = []
    for clip, address in CLIPS.items():
        assert struct.unpack_from('<I', bank, 4 + clip * 4)[0] == address
        assert struct.unpack_from('<HHhh', bank, address) == (21, 150, -2, 0)
        assert struct.unpack_from('<I', bank, address + 20)[0] == 0
        assert struct.unpack_from('<4hf', elf, 0x248C90 + clip * 12 - 0x100000 + 0x300) == (0, 0, 0, 0, 1)
        headers.append({'clip': clip, 'source_offset': address, 'nodes': 21,
                        'frames': 150, 'next': -2, 'blend': 0, 'event_table': 0,
                        'release_flags': 0, 'rate': 1})
    data = args.player.read_bytes()
    magic, bones, verts, indices, frames, fps, textures, flags, clip_count = struct.unpack_from('<4sIIIIfIII', data)
    assert (magic, bones, flags) == (b'EMD3', 22, 0)
    channels = args.channels.read_bytes()
    assert struct.unpack_from('<4sIII', channels) == (b'EMPC', 1, 21, 14)
    assert struct.unpack_from('<22i', data, 36) == struct.unpack_from('<21i', channels, 16) + (-1,)
    clips_at = 36 + bones * 4 + textures * 16
    mesh_at = clips_at + clip_count * 16
    palettes_at = mesh_at + verts * 40 + indices * 4
    textures_at = palettes_at + frames * bones * 64
    entries = [struct.unpack_from('<IIIf', data, clips_at + 16 * i) for i in range(clip_count)]
    result = bytearray(data)
    changed_ranges = []
    with tempfile.TemporaryDirectory(prefix='door_pose_export_') as folder:
        source = Path(folder) / 'bridge.c'
        library = Path(folder) / 'pose.dylib'
        source.write_text(BRIDGE)
        subprocess.run(['cc', '-std=c11', '-O2', '-ffp-contract=off', '-shared', '-fPIC',
                        '-I' + str(ROOT / 'src'), str(source),
                        str(ROOT / 'src/game/em_player_pose.c'),
                        str(ROOT / 'src/game/em_pose_bank.c'),
                        str(ROOT / 'src/game/em_pose_transition.c'), '-lm', '-o', str(library)], check=True)
        native = C.CDLL(str(library))
        native.load.argtypes = [C.c_char_p]
        native.next_frame.argtypes = [C.POINTER(C.c_float)]
        assert native.load(str(args.channels).encode())
        palette = (C.c_float * (22 * 16))()
        for clip in CLIPS:
            selected = [entry for entry in entries if entry[0] == clip]
            assert len(selected) == 1 and selected[0][2:] == (150, 60.0)
            _, first, count, _ = selected[0]
            assert first + count <= frames
            for other, start, length, _ in entries:
                if other != clip:
                    assert start + length <= first or start >= first + count
            assert native.start(clip)
            replacement = bytearray()
            for frame in range(count):
                assert native.next_frame(palette)
                replacement += bytes(palette)
            start = palettes_at + first * bones * 64
            end = start + len(replacement)
            result[start:end] = replacement
            changed_ranges.append((start, end))
    cursor = 0
    for start, end in sorted(changed_ranges):
        assert result[cursor:start] == data[cursor:start]
        cursor = end
    assert result[cursor:] == data[cursor:]
    assert result[:palettes_at] == data[:palettes_at] and result[textures_at:] == data[textures_at:]
    folder = ROOT / 'build/door_player_export'
    folder.mkdir(parents=True, exist_ok=True)
    changed = result != data
    if changed:
        backup = folder / 'player_before_door_poses.emdl'
        if not backup.exists(): backup.write_bytes(data)
        temporary = args.player.with_name(args.player.name + '.door.tmp')
        temporary.write_bytes(result)
        temporary.replace(args.player)
    report = {'changed': changed, 'source_bank_sha256': sha(bank), 'headers': headers,
              'channel_bank_sha256': sha(args.channels.read_bytes()),
              'before_sha256': sha(data), 'after_sha256': sha(result),
              'geometry_sha256': sha(data[mesh_at:palettes_at]),
              'textures_sha256': sha(data[textures_at:]),
              'preserved_other_clips': clip_count - len(CLIPS),
              'changed_byte_count': sum(a != b for a, b in zip(data, result)),
              'boundaries': 'Raw key decoding and clocks have original-instruction oracles; no live door matrix capture yet.'}
    (folder / ('result.json' if changed else 'idempotence.json')).write_text(json.dumps(report, indent=2) + '\n')
    print('door pose export PASS:', json.dumps(report))


if __name__ == '__main__':
    main()
