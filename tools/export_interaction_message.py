#!/usr/bin/env python3
"""Export the original panel/elevator global dialogue from the user's disc.

Outputs contain original strings and timing data and must remain ignored.
The shared EMOD format preserves the terminal record and presenter geometry.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
ELF_HASH = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'


def export_message(decomp, first_line, output):
    if first_line not in (0x18, 0x1A):
        raise ValueError('Only the recovered first-level interaction messages are supported')
    sys.path.insert(0, str(decomp/'tools'))
    from audio_export import ElfImage
    from export_opening_media import parse_lines, write_dialogue
    elf = ElfImage(decomp/'config/SCUS_971.12')
    assert hashlib.sha256(elf.data).hexdigest() == ELF_HASH
    bank = (decomp/'extract/chunk03/f14_id16.bin').read_bytes()
    lines = parse_lines(bank, 0)
    timing_base = elf.u32(0x264DD0)
    records = []
    for index in range(first_line, len(lines)):
        duration, voice, speaker, terminal, reserved = struct.unpack(
            '<HhBBH', elf.read(timing_base + index * 8, 8))
        assert voice == -1 and speaker == 255 and terminal in (0, 1) and reserved == 0
        text, skew = lines[index]
        records.append(dict(line=index, duration=duration, voice=voice,
            speaker=speaker, terminal=terminal, text=text, skew=skew))
        if terminal:
            break
    assert len(records) == 2 and records[-1]['terminal']
    config = struct.unpack('<6I', elf.read(0x264CD0, 24))
    output.parent.mkdir(parents=True, exist_ok=True)
    write_dialogue(output, records, 2 * ((config[2] + config[4]) >> 1),
                   elf.u32(0x26EC10) & 0xFFFFFF, 0x100505)
    return {'token': hex(0x80000000 | first_line), 'timing_table': hex(timing_base),
            'elf_sha256': ELF_HASH, 'asset_sha256': hashlib.sha256(output.read_bytes()).hexdigest(),
            'records': [{k: v for k, v in r.items() if k != 'text'} for r in records]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--decomp', type=Path, default=ROOT.parent/'Extermination')
    parser.add_argument('--line', type=lambda value: int(value, 0), default=0x1A)
    parser.add_argument('--out', type=Path, default=ROOT/'assets/scene_snow/elevator_refusal.emod')
    args = parser.parse_args()
    report = export_message(args.decomp, args.line, args.out)
    target = ROOT/'build/interaction_message'
    target.mkdir(parents=True, exist_ok=True)
    (target/f'message_{args.line:02x}.json').write_text(json.dumps(report, indent=2)+'\n')
    print('Exported original interaction message', hex(0x80000000 | args.line))


if __name__ == '__main__':
    main()
