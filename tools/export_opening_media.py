#!/usr/bin/env python3
"""Export AREA11 opening dialogue/stream from the user's own original disc.

Source locations are established by 001FD4C0 (area/message -> music cue),
001FD790 (area/line -> duration), and 001FD950/001FE070 (text + markup).
All output is locally generated and must remain in the ignored assets tree.
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


def parse_lines(data: bytes, outer: int) -> list[tuple[bytes, int]]:
    """Return original byte strings and whole-line skew for this opening.

    Reject unfamiliar markup instead of silently losing it. Opening markup is
    tag3, value1 at character0 and value0 at the end of the printable run.
    The original glyph flush skews the TOP edge by value*8 pixels.
    """
    def words(at, n):
        if at < 0 or at + n * 4 > len(data):
            raise ValueError('message structure outside source')
        return struct.unpack_from('<' + 'I' * n, data, at)
    text_off, count, records_size, directory = words(outer, 4)
    if directory != 16 or not 0 < count <= 4096 or text_off < 16 + count * 16:
        raise ValueError('unsupported message directory')
    text = outer + text_off + records_size
    strbase, count2, byte_count, version = words(text, 4)
    if (count2 != count or version != 1 or strbase < 16 + count * 16
            or text + strbase + byte_count > len(data)):
        raise ValueError('invalid text block')
    lines = []
    for i in range(count):
        at, duplicate, length, nul_length = words(text + 16 + i * 16, 4)
        start = text + strbase + at
        if (at != duplicate or nul_length != length + 1
                or at + nul_length > byte_count or start + length >= len(data)):
            raise ValueError('invalid text record')
        string = data[start:start + length]
        if data[start + length] != 0 or b'\0' in string:
            raise ValueError('invalid message termination')
        roff, _, _, rsize = words(outer + 16 + i * 16, 4)
        skew = 0
        if rsize:
            if rsize % 16 or roff + rsize > records_size:
                raise ValueError('invalid markup record')
            records = [words(outer + text_off + roff + j, 4)
                       for j in range(0, rsize, 16)]
            if (len(records) != 2 or records[0][:3] != (3, 1, 0)
                    or records[1][:2] != (3, 0)
                    or records[1][2] != len(string.rstrip(b'\n'))):
                # Preserve only the opening's confirmed whole-line markup.
                skew = -1
            else:
                skew = 8
        lines.append((string, skew))
    return lines


def write_dialogue(path: Path, records: list[dict], line_height: int,
                   fill: int, outline: int) -> None:
    blob = bytearray()
    packed = bytearray()
    for rec in records:
        string = rec['text']
        if rec['skew'] < 0:
            raise ValueError('opening line has unsupported markup')
        packed += struct.pack('<HHhBBIIB3x', rec['line'], rec['duration'],
                              rec['voice'], rec['speaker'], rec['terminal'],
                              len(blob), len(string), rec['skew'])
        blob += string + b'\0'
    header = struct.pack('<4s7I', b'EMOD', 1, len(records), line_height,
                         fill, outline, 388, len(blob))
    path.write_bytes(header + packed + blob)


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

    source_path = decomp / 'extract/chunk15/f12_id44.bin'
    source_data = source_path.read_bytes()
    lines = parse_lines(source_data, 0x3E800)
    timing = elf.u32(0x264DD0 + (area + 1) * 4)
    records = []
    for index in range(message, len(lines)):
        duration, voice, speaker, terminal, unused = struct.unpack(
            '<HhBBH', elf.read(timing + index * 8, 8))
        if voice != -1 or speaker not in (0, 1, 255) or terminal not in (0, 1) or unused:
            raise ValueError('unexpected opening message timing record')
        text, skew = lines[index]
        records.append(dict(line=index, duration=duration, voice=voice,
                            speaker=speaker, terminal=terminal, text=text, skew=skew))
        if terminal:
            break
    if not records or not records[-1]['terminal']:
        raise ValueError('unterminated opening message sequence')
    # Original new-line pen adds (cfg[2]+cfg[4])>>1 in half-height units.
    cfg = struct.unpack('<6I', elf.read(0x264CD0, 24))
    line_height = 2 * ((cfg[2] + cfg[4]) >> 1)
    fill = elf.u32(0x26EC10) & 0xFFFFFF
    write_dialogue(args.out / 'opening.emod', records, line_height, fill, 0x100505)
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
                  pcm_sha256=hashlib.sha256(pcm).hexdigest(), timing_address=hex(timing),
                  text_source=str(source_path.relative_to(decomp)), text_offset=0x3E800,
                  line_height=line_height, records=[{**r, 'text':r['text'].decode('ascii')}
                                                    for r in records])
    report['resume_music'] = dict(cue=resume_cue, loop=resume_loop,
        source_start=resume_start, source_size=resume_size,
        source_sha256=hashlib.sha256(resume_compressed).hexdigest(),
        pcm_sha256=hashlib.sha256(resume_pcm).hexdigest())
    (args.out / 'opening_media.json').write_text(json.dumps(report, indent=2) + '\n')
    return {k:v for k,v in report.items() if k != 'records'}


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
