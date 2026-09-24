#!/usr/bin/env python3
"""Export the AREA11 opening stream from the user's own original disc.

The stream is the music cue 001FD4C0 selects for the opening's line 0x66
(the D_0026EC60 stream table row of area 11). The opening's text lines run
on the message service (tools/export_message_data.py, WP-8). All output is
locally generated and must remain in the ignored assets tree.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys


def load_tool(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def export(args) -> dict:
    decomp = args.decomp_root.resolve()
    audio = load_tool(decomp / 'tools/audio_export.py', '_opening_audio_export')
    movie = load_tool(Path(__file__).with_name('export_movie.py'), '_opening_iso')
    elf_path = decomp / 'config/SCUS_971.12'
    elf = audio.ElfImage(elf_path)
    area, message = 11, 0x66
    cue = None
    for index in range(512):
        rec = struct.unpack('<4i', elf.read(0x26EC60 + index * 16, 16))
        if rec[0] == -1:
            break
        if rec[0] == area and rec[2] == message:
            cue = rec[3]
            break
    if cue is None:
        raise ValueError('original opening stream lookup failed')
    sector, start, size, loop = audio.read_cue_table(elf_path, True)[cue]
    if loop or start != sector * 2048 or size % 2048:
        raise ValueError('unsupported opening stream boundaries')
    source = (args.music.read_bytes() if args.music else
              movie.iso_file(args.iso or decomp / 'Extermination-rebuilt.iso',
                             '/STREAM/MUSIC.DAT'))
    compressed = source[start:start + size]
    if len(compressed) != size:
        raise ValueError('music cue outside source')
    left, right = audio.deinterleave(compressed, 64)
    if len(left) != len(right):
        raise ValueError('unequal opening stream channels')
    pcm = audio.interleave_pcm(audio.decode_adpcm(left), audio.decode_adpcm(right))
    args.out.mkdir(parents=True, exist_ok=True)
    audio.write_wav(args.out / 'opening.wav', pcm, 48000, 2)
    # 001FAE70(0): AREA11's D8106C8=20081910, no override, selects25.
    # Both isolated cold-boot snapshots confirm this route; playable RAM
    # D282178 changes from opening stream63 to ambient stream25.
    resume_cue = 25
    resume_sector, resume_start, resume_size, resume_loop = audio.read_cue_table(elf_path, True)[resume_cue]
    if resume_loop != 1 or resume_start != resume_sector * 2048 or resume_size % 2048:
        raise ValueError('unsupported ambient resume stream boundaries')
    resume_compressed = source[resume_start:resume_start + resume_size]
    if len(resume_compressed) != resume_size:
        raise ValueError('ambient resume stream outside source')
    resume_left, resume_right = audio.deinterleave(resume_compressed, 64)
    resume_pcm = audio.interleave_pcm(audio.decode_adpcm(resume_left), audio.decode_adpcm(resume_right))
    audio.write_wav(args.out / 'opening_resume.wav', resume_pcm, 48000, 2)

    # Scene34 binds this fullscreen-fade track (0022EC30), not subtitles.
    fade_track = []
    for index in range(64):
        value = struct.unpack('<f', elf.read(0x26AE00 + index * 4, 4))[0]
        fade_track.append(value)
        if value == 0.0:
            break
    if fade_track[-1] != 0.0:
        raise ValueError('unterminated opening fade track')
    (args.out / 'opening.emfx').write_bytes(struct.pack('<4sII', b'EMFX', 1, len(fade_track)) + struct.pack('<' + 'f' * len(fade_track), *fade_track))
    report = dict(area=area, message=message, music_cue=cue, source_start=start,
                  source_size=size, rate=48000, channels=2, pcm_frames=len(pcm)//4,
                  pcm_seconds=len(pcm)/4/48000, source_sha256=hashlib.sha256(compressed).hexdigest(),
                  pcm_sha256=hashlib.sha256(pcm).hexdigest())
    report['resume_music'] = dict(cue=resume_cue, loop=resume_loop,
        source_start=resume_start, source_size=resume_size,
        source_sha256=hashlib.sha256(resume_compressed).hexdigest(),
        pcm_sha256=hashlib.sha256(resume_pcm).hexdigest())
    (args.out / 'opening_media.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--decomp-root', type=Path, default=Path('../Extermination'))
    p.add_argument('--iso', type=Path)
    p.add_argument('--music', type=Path)
    p.add_argument('--out', type=Path, default=Path('assets/opening'))
    args = p.parse_args()
    try:
        print(json.dumps(export(args), indent=2))
    except (OSError, ValueError, struct.error) as error:
        p.exit(1, f'error: {error}\n')

if __name__ == '__main__':
    main()
