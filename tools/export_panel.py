#!/usr/bin/env python3
"""Export original type24 panel scripts, BATTERY page, text and clip15C.

All output is local disc/runtime data and belongs in ignored assets/. The
GS snapshot must be the BATTERY confirmation page, not ordinary gameplay.
The clip is retained as original channel bytes; this exporter does not
guess a root-motion conversion or silently recenter the interaction.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
SCRIPT_BASE, SCRIPT_END = 0x246F20, 0x247E20
SCRIPT_ENTRIES = (0x246F20, 0x2477A0, 0x247BA0, 0x247BE0, 0x247DA0)
ELF_HASH = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--decomp', type=Path, default=ROOT.parent/'Extermination')
    ap.add_argument('--capture', type=Path)
    ap.add_argument('--out', type=Path, default=ROOT/'assets/scene_snow/panel')
    args = ap.parse_args()
    capture = args.capture or args.decomp/'build/startup-reference/panel'
    sys.path.insert(0, str(args.decomp/'tools'))
    from audio_export import ElfImage
    from export_ui import decode_token_lm, pack_shelf, parse_outer
    from gs_vram import read_localmem
    from export_opening_media import parse_lines, write_dialogue
    elf = ElfImage(args.decomp/'config/SCUS_971.12')
    assert hashlib.sha256(elf.data).hexdigest() == ELF_HASH, 'Unexpected original ELF'
    ram = (capture/'eeMemory.bin').read_bytes()
    assert len(ram) == 0x2000000
    assert ram[0x810146] == 0x21, 'Snapshot is not the original BATTERY module'
    assert elf.read(0x265C50, 0xC8) == ram[0x265C50:0x265D18], 'UI table changed'
    _, lm = read_localmem(capture/'gs.bin')
    args.out.mkdir(parents=True, exist_ok=True)

    program = elf.read(SCRIPT_BASE, SCRIPT_END-SCRIPT_BASE)
    assert struct.unpack_from('<I', program, 0)[0] == 7
    assert struct.unpack_from('<I', program, 0x247DE0-SCRIPT_BASE)[0] == 0x80000007
    (args.out/'scripts.emsc').write_bytes(struct.pack('<4sIIII', b'EMSC', 1,
        SCRIPT_BASE, SCRIPT_ENTRIES[0], len(program)) + program)

    # Stable IDs: frame table0..15, three label/icon triples16..24,
    # original row highlight25 and scrolling background26.
    tokens = [struct.unpack('<Q', elf.read(0x265C50+i*8, 8))[0] for i in range(16)]
    tokens += [struct.unpack('<Q', elf.read(0x265CD0+i*8, 8))[0] for i in range(9)]
    tokens += [0x20042D05A1322000, 0x20043C859D422150]
    decoded = [decode_token_lm(lm, t & 0xFFFFFFFF, t >> 32) for t in tokens]
    # The original No cursor is an untextured rectangle, drawn after the
    # decor. A white helper texel preserves that order in the native
    # decor queue without changing its geometry/color or inventing art.
    tokens.append(0)
    decoded.append((b'\xff\xff\xff\xff',{'w':1,'h':1}))
    positions, height = pack_shelf([(m['w'], m['h']) for _, m in decoded], 1024)
    width = 1024
    atlas = bytearray(width*height*4)
    records = bytearray()
    for index, ((rgba, meta), (u, v), token) in enumerate(zip(decoded, positions, tokens)):
        w, h = meta['w'], meta['h']
        records += struct.pack('<6IQ', index, u, v, w, h, 0, token)
        for y in range(h):
            start = ((v+y)*width+u)*4
            atlas[start:start+w*4] = rgba[y*w*4:(y+1)*w*4]

    data = (args.decomp/'extract/chunk00/f02_id02.bin').read_bytes()
    directory, _, _, directory_off = struct.unpack_from('<4I', data)
    texts = []
    def text_record(source, outer, line, label):
        lines, _ = parse_outer(source, outer, label)
        off, _, _, size = struct.unpack_from('<4I',source,outer+16+line*16)
        record_base = outer + struct.unpack_from('<I',source,outer)[0] + off
        spans = []
        for i in range(size//16):
            tag, color, at, _ = struct.unpack_from('<4I',source,record_base+i*16)
            assert tag == 2, 'Unsupported original text markup; do not flatten'
            spans.append((at, elf.u32(0x26EC10+color*4)))
        return lines[line], spans
    for group, line in ((5,0), (5,8), (5,9), (3,27), (3,28), (3,29)):
        outer = directory + struct.unpack_from('<I', data, directory_off+group*16)[0]
        texts.append(text_record(data, outer, line, f'group{group}'))
    global_bank = (args.decomp/'extract/chunk03/f14_id16.bin').read_bytes()
    texts.append(text_record(global_bank, 0, 0x18, 'global'))
    # Same001FD790/001FD950 presenter as the opening, but bit31 selects the
    # global timing/text banks. Keep the terminal record and original timer;
    # the panel's opcodeC waits for actual presenter completion.
    global_lines = parse_lines(global_bank, 0)
    timing_base = elf.u32(0x264DD0)
    message_records = []
    for index in range(0x18, len(global_lines)):
        duration, voice, speaker, terminal, unused = struct.unpack(
            '<HhBBH', elf.read(timing_base+index*8, 8))
        assert voice == -1 and speaker == 255 and terminal in (0, 1) and not unused
        value, skew = global_lines[index]
        message_records.append(dict(line=index, duration=duration, voice=voice,
            speaker=speaker, terminal=terminal, text=value, skew=skew))
        if terminal:
            break
    assert len(message_records) == 2 and message_records[-1]['terminal']
    cfg = struct.unpack('<6I', elf.read(0x264CD0, 24))
    write_dialogue(args.out/'terminal.emod', message_records,
        2*((cfg[2]+cfg[4])>>1), elf.u32(0x26EC10)&0xFFFFFF, 0x100505)
    outer = directory + struct.unpack_from('<I', data, directory_off+5*16)[0]
    texts.append(text_record(data,outer,0x19,'group5'))
    # Acquisition state3 retains group4 while the acquired row is selected.
    outer = directory + struct.unpack_from('<I', data, directory_off+4*16)[0]
    for line in (0x1B, 0x1C, 0x1D):
        texts.append(text_record(data, outer, line, 'group4'))
    text_blob = bytearray()
    for value, spans in texts:
        text_blob += struct.pack('<II', len(value)+1, len(spans))
        text_blob += b''.join(struct.pack('<II',at,rgb) for at,rgb in spans)
        text_blob += value + b'\0'
    # Header then fixed32-byte TEX0 records, length-prefixed strings,
    # original shared background state, and RGBA8 sheet.
    background = elf.read(0x2655A0, 3*32)
    payload = records + text_blob + background + atlas
    header = struct.pack('<4s7I', b'EMBA', 2, width, height, len(tokens),
                         len(texts), len(text_blob), len(payload))
    (args.out/'battery.emba').write_bytes(header+payload)

    bank = (args.decomp/'extract/chunk28/f01_id3c.bin').read_bytes()
    count = struct.unpack_from('<I',bank)[0]
    offsets = struct.unpack_from(f'<{count}I',bank,4)
    clip_start, clip_end = offsets[0x15C:0x15E]
    assert clip_start == 0x1C2E50 and clip_end > clip_start
    clip = bank[clip_start:clip_end]
    bones, frames = struct.unpack_from('<HH',clip)
    assert (bones,frames)==(21,121)
    (args.out/'player_15c.bin').write_bytes(clip)
    report = {'elf_sha256':ELF_HASH,'capture_sha256':hashlib.sha256(ram).hexdigest(),
              'script_entries':[hex(x) for x in SCRIPT_ENTRIES],
              'script_sha256':hashlib.sha256(program).hexdigest(),
              'terminal_message':{'token':'80000018','timing_table':hex(timing_base),
                  'records':[{k:v for k,v in record.items() if k!='text'}
                             for record in message_records]},
              'camera_command':{'callback':'0018CBD0','distance':'current camera+0C',
                                'solve_modes':[5,1],'camera_A0':120},
              'animation':{'bank':'chunk28/f01_id3c.bin','id':348,
                           'offset':clip_start,'size':len(clip),
                           'bones':bones,'frames':frames,'rate':1.0,
                           'sha256':hashlib.sha256(clip).hexdigest(),
                           'root_motion':'original bytes, no guessed conversion'},
              'atlas':[{'id':i,'tex0':f'{token:016x}','x':uv[0],'y':uv[1],
                        'w':m['w'],'h':m['h']} for i,(token,uv,(_,m)) in
                       enumerate(zip(tokens,positions,decoded))],
              'text_sources':['5:0','5:8','5:9','3:27','3:28','3:29','global:18','5:25',
                              '4:27','4:28','4:29']}
    (args.out/'source.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'Panel: original scripts, {len(tokens)} textures, {len(texts)} strings and clip15C -> {args.out}')


if __name__ == '__main__': main()
