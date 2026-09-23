#!/usr/bin/env python3
"""Export the player clips the climb (states 2/3) and slide (state 0x1C) request.

docs/PLAYER_CLIMB_SLIDE.md. The clip ids come from the original routines
translated in src/game/em_player_climb.c and em_player_slide.c:

  slide 0016C6A0/0017F5F0/00224B80: 0x5F 0x60 0x61 0x62 0x63 0x64 0x65 0x66 0x67
                                     0x68 0x6D 0x72 (0x73's successor)
  climb 00161790:                    0x70 0x71 0x77 0x78 0x79 0x7A 0x8A 0x8B 0x8C
                                     0x8D, and the hang rows 0x7B/0x8E (00188550)
  vault 00162190:                    0x69 0x6A 0x6B 0x6C 0x7D

Clips 0x5E and 0x73 are chained in the original bank (next clip 0x5F/0x72
with a blend flag). The EMPC loader (em_pose_bank.c) accepts only terminal
clips, so they are reported, not exported; they need chained-clip support in
the pose bank first.

Like tools/export_player_reversal_clips.py (whose encoder this reuses), the
decoded original node keys are appended to the EMPC source bank and their
channel-derived palettes to the EMDL display model; every existing byte of
both files is preserved. Results go to ignored build/player_climb_slide_export/.
--install replaces assets/player_channels.empc and assets/player.emdl
(backups kept); tests that pin the EMPC clip count
(tools/test_pose_bank_reference.py) must be updated at the same time.
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
sys.path.insert(0, str(ROOT / 'tools'))
from export_player_reversal_clips import BRIDGE, ELF_SHA256, empc_clips, encode, sha  # noqa: E402

SLIDE = (0x5F, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x6D, 0x72)
CLIMB = (0x70, 0x71, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E)
VAULT = (0x69, 0x6A, 0x6B, 0x6C, 0x7D)
CHAINED = (0x5E, 0x73)


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
    # The ELF tables the routines index: D_002754A0 (grab clips by +2F1) and
    # D_002754C0 (hang clips by +235 & 1).
    grab = struct.unpack_from('<3h', elf, 0x2754A0 - 0x100000 + 0x300)
    hang = struct.unpack_from('<2h', elf, 0x2754C0 - 0x100000 + 0x300)
    assert set(grab) <= set(CLIMB) and set(hang) <= set(CLIMB), (grab, hang)
    for clip_id in CHAINED:
        at = struct.unpack_from('<I', bank, 4 + clip_id * 4)[0]
        _, _, next_clip, blend = struct.unpack_from('<HHhh', bank, at)
        assert next_clip >= 0 and blend == 1, ('chained clip changed', clip_id)
    wanted = SLIDE + CLIMB + VAULT

    channels = args.channels.read_bytes()
    present = empc_clips(channels)
    have = {c[0] for c in present}
    missing = [c for c in wanted if c not in have]
    first_id, first_start, first_end = present[0]
    assert encode(bank, first_id, OpeningClip)[0] == channels[first_start:first_end], \
        'channel encoder differs from the existing export'
    payloads, report_clips = [], []
    for clip_id in missing:
        payload, parents, info = encode(bank, clip_id, OpeningClip)
        assert tuple(parents) == struct.unpack_from('<21i', channels, 16), 'rig mismatch'
        payloads.append(payload); report_clips.append(info)
    new_channels = (struct.pack('<4sIII', b'EMPC', 1, 21, len(present) + len(missing)) +
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
    model_have = {e[0] for e in entries}
    model_missing = [c for c in wanted if c not in model_have]

    out = ROOT / 'build/player_climb_slide_export'
    out.mkdir(parents=True, exist_ok=True)
    staged_channels = out / 'player_channels.empc'
    staged_channels.write_bytes(new_channels)
    extra = bytearray(); table_entries = b''
    with tempfile.TemporaryDirectory(prefix='climb_slide_pose_export_') as folder:
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
        cursor = frames
        lengths = {}
        for clip_id in model_missing:
            at = struct.unpack_from('<I', bank, 4 + clip_id * 4)[0]
            lengths[clip_id] = struct.unpack_from('<H', bank, at + 2)[0]
            assert native.start(clip_id)
            for _ in range(lengths[clip_id]):
                assert native.next_frame(palette)
                extra += bytes(palette)
            table_entries += struct.pack('<IIIf', clip_id, cursor, lengths[clip_id], 60.0)
            cursor += lengths[clip_id]
    added = cursor - frames
    header = bytearray(data[:36])
    struct.pack_into('<I', header, 16, frames + added)
    struct.pack_into('<I', header, 32, nclips + len(model_missing))
    result = (bytes(header) + data[36:mesh_at] + table_entries +
              data[mesh_at:texture_at] + bytes(extra) + data[texture_at:])
    shift = len(table_entries)
    assert result[36:mesh_at] == data[36:mesh_at]
    assert result[mesh_at + shift:texture_at + shift] == data[mesh_at:texture_at]
    assert result[texture_at + shift + len(extra):] == data[texture_at:]
    staged_player = out / 'player.emdl'
    staged_player.write_bytes(result)

    report = {'channel_clips_added': [hex(c) for c in missing],
              'model_clips_added': [hex(c) for c in model_missing],
              'chained_not_exported': [hex(c) for c in CHAINED],
              'clips': report_clips, 'grab_table': grab, 'hang_table': hang,
              'channels_before_sha256': sha(channels), 'channels_after_sha256': sha(new_channels),
              'player_before_sha256': sha(data), 'player_after_sha256': sha(result),
              'preserved_channel_clips': len(present), 'preserved_model_clips': nclips,
              'added_model_frames': added, 'installed': False}
    if args.install:
        for staged, target in ((staged_channels, args.channels), (staged_player, args.player)):
            backup = out / f'{target.name}.before_climb_slide'
            if not backup.exists():
                shutil.copyfile(target, backup)
            temporary = target.with_name(target.name + '.climb_slide.tmp')
            shutil.copyfile(staged, temporary)
            temporary.replace(target)
        report['installed'] = True
    (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print('climb/slide clip export PASS: %d channel clips, %d model clips, %d frames staged in %s'
          % (len(missing), len(model_missing), added, out))


if __name__ == '__main__':
    main()
