#!/usr/bin/env python3
"""Export Roger's original AREA11 streams and message timing report into ignored assets.

Roger's text lines run on the message service (tools/export_message_data.py,
WP-8); this exports the encounter and resume streams and a report of the
encounter's message records. Only the user's extracted game data and
original cue/message tables are used.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

from export_opening_media import load_tool


ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
PROGRAM = 0x8283D0
TEXT_OFFSET = 0x3E800


def message_records(elf):
    """The encounter's records of the area-11 timing table D_00264DD0[12],
    from line 0 up to the first terminal record."""
    timing = elf.u32(0x264DD0 + 12 * 4)
    records = []
    for index in range(162):
        duration, voice, speaker, terminal, reserved = struct.unpack(
            '<HhBBH', elf.read(timing + index * 8, 8))
        if (voice != -1 or speaker not in (0, 1, 255) or terminal not in (0, 1)
                or reserved):
            raise ValueError('unsupported original Roger message record')
        records.append(dict(line=index, duration=duration, voice=voice,
                            speaker=speaker, terminal=terminal))
        if terminal:
            break
    if not records or not records[-1]['terminal']:
        raise ValueError('unterminated original Roger message sequence')
    return timing, records


def export(args):
    decomp = args.decomp_root.resolve()
    audio = load_tool(decomp / 'tools/audio_export.py', '_roger_audio_export')
    movie = load_tool(ROOT / 'tools/export_movie.py', '_roger_iso')
    elf_path = decomp / 'config/SCUS_971.12'
    elf = audio.ElfImage(elf_path)
    overlay = (decomp / 'extract/OVERLAY/AREA11.BIN').read_bytes()
    frame = struct.unpack_from('<16I', overlay, 0x828410 - 0x823500)
    message = struct.unpack_from('<16I', overlay, 0x828490 - 0x823500)
    if (frame[:3] != (7, 0, 12) or frame[5:8] != (0, 0, 0)
            or message[:3] != (12, 0, 1) or message[5:8] != (0, 0, 0)):
        raise ValueError('original encounter command parameters changed')

    stream = None
    area_rows = []
    for index in range(512):
        address = 0x26EC60 + index * 16
        row = struct.unpack('<4i', elf.read(address, 16))
        if row[0] == -1:
            break
        if row[0] == 11:
            area_rows.append(dict(address=address, area=row[0], reserved=row[1],
                                  message=row[2], cue=row[3]))
            if row[2] == frame[6]:
                if stream is not None:
                    raise ValueError('ambiguous Roger stream mapping')
                stream = row[3]
    if stream is None:
        raise ValueError('Roger stream is absent from the original area table')

    timing, records = message_records(elf)
    args.out.mkdir(parents=True, exist_ok=True)

    music = (args.music.read_bytes() if args.music else
             movie.iso_file(args.iso or decomp / 'Extermination-rebuilt.iso',
                            '/STREAM/MUSIC.DAT'))
    cues = audio.read_cue_table(elf_path, True)
    streams = []
    # 001FAE70 uses bits 8..14 of D8106C8 when no override is active.
    # Original state15 and first-control state04 both contain 0x20081910.
    for name, cue, expected_loop in (('encounter', stream, 0),
                                     ('encounter_resume', 25, 1)):
        sector, start, size, loop = cues[cue]
        if (loop != expected_loop or start != sector * 2048 or size % 2048
                or start + size > len(music)):
            raise ValueError(f'unsupported {name} stream boundaries')
        compressed = music[start:start + size]
        left, right = audio.deinterleave(compressed, 64)
        if len(left) != len(right):
            raise ValueError('unequal original stereo channels')
        pcm = audio.interleave_pcm(audio.decode_adpcm(left), audio.decode_adpcm(right))
        audio.write_wav(args.out / f'{name}.wav', pcm, 48000, 2)
        streams.append(dict(name=name, cue=cue, loop=loop, source_start=start,
                            source_size=size, source_sector=sector, rate=48000,
                            channels=2, pcm_frames=len(pcm) // 4,
                            source_sha256=hashlib.sha256(compressed).hexdigest(),
                            pcm_sha256=hashlib.sha256(pcm).hexdigest()))

    report = dict(program=PROGRAM, frame_command=0x828410, message_command=0x828490,
                  area=11, stream_key=frame[6], first_message=message[5], delay=message[6],
                  area_stream_rows=area_rows, streams=streams, timing_address=timing,
                  draw_callbacks=sum(r['duration'] + 1 for r in records), records=records,
                  elf_sha256=hashlib.sha256(elf_path.read_bytes()).hexdigest(),
                  boundaries=['ADPCM source bytes are exact; hardware SPU2 interpolation and mixing are not claimed.',
                              'Resume cue25 is the captured AREA11 no-override route, not a universal BGM rule.',
                              'No camera fade track is borrowed from the opening.'])
    (args.out / 'media.json').write_text(json.dumps(report, indent=2) + '\n')
    return {key: value for key, value in report.items() if key != 'records'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--decomp-root', type=Path, default=DECOMP)
    parser.add_argument('--iso', type=Path)
    parser.add_argument('--music', type=Path)
    parser.add_argument('--out', type=Path, default=ROOT / 'assets/scene_snow/roger')
    args = parser.parse_args()
    try:
        print(json.dumps(export(args), indent=2))
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f'error: {error}\n')


if __name__ == '__main__':
    main()
