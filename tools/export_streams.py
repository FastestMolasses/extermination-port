#!/usr/bin/env python3
"""Export the stream sectors the first level reads, from the user's own disc.

The port's IOP stream backend (src/game/em_iop_stream.c, docs/IOP_STREAM.md)
reads music and voice sectors by disc sector number, exactly as the lanes
(em_stream_lanes_original) ask for them through 00112610.  This tool writes
those sectors, and the original tables the lanes and the backend read, into
one local file:

  assets/streams/streams.emst   the EMST container (layout below)
  assets/streams/streams.json   a report: cue provenance, extents, sha256s

Sources (all supplied by the user; nothing here is ever committed):
  --iso    the user's disc image; the start sectors of \\STREAM\\MUSIC.DAT and
           \\STREAM\\VOICE.DAT come from its ISO9660 directory (default: the
           decomp's locally rebuilt image, like the other exporters)
  --music / --voice   the two files extracted from the user's disc instead;
           then --music-lsn / --voice-lsn must give their directory start
           sectors (read them from the disc's directory; they are not guessed)
  --elf    the pinned boot ELF (sha256 checked): the clip rows D_0025DD30
           (68 music rows) and D_0025E170 (179 voice rows), the two search
           names D_0026EBB0 / D_0026EBD0 that sub_O_STREAM_MUSIC_DAT_1 looks
           up, and the area/line stream table D_0026EC60

Which cues are exported (the first level's):
  music: every D_0026EC60 row of area 11 (the opening's line 0x66, the Roger
         encounter's trigger 0 and trigger 25), cue 25 (AREA11's 001FAE70
         selection, captured at D_00282178[0] in every AREA11 image), cue 0x18
         (001FAE70's override cue), cue 0x1B (the game over's 001FA790) and
         cue 13 (playing right after the AREA11 exit, captured)
  voice: 143..151 (the director's and Roger's lines)
A read of any other sector faults in the backend (fail-stop): nothing outside
these cues is substituted.

EMST layout (little endian):
  0x00  'EMST', u32 version 1
  0x08  u32 music_lsn, u32 voice_lsn, u32 music_sectors, u32 voice_sectors
  0x18  u32 music_rows (68), u32 voice_rows (179), u32 extent_count, u32 0
  0x28  char search[2][32]  D_0026EBB0, D_0026EBD0 (the ELF strings)
  0x68  char path[2][32]    the two directory paths as the disc names them
  0xA8  rows: (music_rows + voice_rows) x 16 bytes, the ELF rows verbatim
        extents: extent_count x {u32 lsn, u32 sectors, u32 offset, u32 file}
        sector data (offset is from the start of the file)
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
SECTOR = 2048
MUSIC_ROWS, VOICE_ROWS = 68, 179
MUSIC_TABLE, VOICE_TABLE = 0x25DD30, 0x25E170
SEARCH_NAMES = (0x26EBB0, 0x26EBD0)
STREAM_TABLE = 0x26EC60
AREA = 11
# Cues named by original code paths (see the module docstring).
MUSIC_EXTRA = {25: '001FAE70 AREA11 selection (captured D_00282178[0] = 25)',
               0x18: '001FAE70 override cue (D_008104E4 == 1)',
               0x1B: 'game over 001FA790(0, 0x1B)',
               13: 'the next area\'s music at the AREA11 exit (captured D_00282178[0] = 13 in route 15_level_exit)'}
VOICE_CUES = {c: 'director / Roger voiced lines (docs/STREAM_LANES.md item 6)'
              for c in range(143, 152)}
PATHS = ('/STREAM/MUSIC.DAT', '/STREAM/VOICE.DAT')
HEADER = 0xA8


class Elf:
    def __init__(self, path: Path):
        self.data = path.read_bytes()
        digest = hashlib.sha256(self.data).hexdigest()
        if digest != ELF_SHA256:
            raise ValueError(f'{path}: sha256 {digest} is not the pinned SCUS-97112 boot ELF')

    def read(self, vram: int, size: int) -> bytes:
        offset = vram - 0x100000 + 0x300
        if offset < 0 or offset + size > len(self.data):
            raise ValueError(f'vram {vram:#x} outside the boot ELF')
        return self.data[offset:offset + size]


def iso_entry(iso: Path, name: str) -> tuple[int, int, str]:
    """(start sector, size, directory path) of one ISO9660 file, read-only."""
    with iso.open('rb') as file:
        file.seek(16 * SECTOR)
        pvd = file.read(SECTOR)
        if pvd[:7] != b'\x01CD001\x01':
            raise ValueError(f'{iso}: missing ISO9660 primary volume descriptor')
        record = pvd[156:]
        extent, length = struct.unpack_from('<I', record, 2)[0], struct.unpack_from('<I', record, 10)[0]
        path = ''
        for component in name.strip('/').split('/'):
            file.seek(extent * SECTOR)
            directory = file.read(length)
            pos, found = 0, None
            while pos < len(directory):
                size = directory[pos]
                if not size:
                    pos = (pos // SECTOR + 1) * SECTOR
                    continue
                item = directory[pos:pos + size]
                ident = item[33:33 + item[32]].decode('ascii')
                if ident.split(';')[0] == component:
                    found = (item, ident)
                    break
                pos += size
            if found is None:
                raise ValueError(f'{component} not found in {iso}')
            item, ident = found
            path += '\\' + ident
            extent, length = struct.unpack_from('<I', item, 2)[0], struct.unpack_from('<I', item, 10)[0]
        return extent, length, path


def iso_read(iso: Path, sector: int, size: int) -> bytes:
    with iso.open('rb') as file:
        file.seek(sector * SECTOR)
        data = file.read(size)
    if len(data) != size:
        raise ValueError(f'{iso}: truncated file extent at sector {sector}')
    return data


def cue_list(elf: Elf):
    music = {}
    for index in range(512):
        area, _zero, trigger, cue = struct.unpack('<4i', elf.read(STREAM_TABLE + 16 * index, 16))
        if area == -1:
            break
        if area == AREA:
            music.setdefault(cue, f'D_0026EC60 row {index}: area {AREA}, trigger {trigger:#x}')
    else:
        raise ValueError('D_0026EC60 has no terminator')
    for cue, why in MUSIC_EXTRA.items():
        music.setdefault(cue, why)
    return dict(sorted(music.items())), dict(VOICE_CUES)


def extents_for(rows, cues, file_index, file_sectors):
    spans = []
    for cue in cues:
        sector, start, size, _loop = rows[cue]
        if cue == 0 or size <= 0 or start != sector * SECTOR:
            raise ValueError(f'unsupported row for cue {cue} in file {file_index}')
        count = (size + SECTOR - 1) // SECTOR
        if sector + count > file_sectors:
            raise ValueError(f'cue {cue} runs past the end of file {file_index}')
        spans.append([sector, sector + count])
    spans.sort()
    merged = []
    for lo, hi in spans:
        if merged and lo <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], hi)
        else:
            merged.append([lo, hi])
    return merged


def export(args) -> dict:
    decomp = args.decomp_root.resolve()
    elf = Elf(args.elf or decomp / 'config/SCUS_971.12')
    rows_raw = elf.read(MUSIC_TABLE, 16 * MUSIC_ROWS) + elf.read(VOICE_TABLE, 16 * VOICE_ROWS)
    music_rows = [struct.unpack_from('<4i', rows_raw, 16 * i) for i in range(MUSIC_ROWS)]
    voice_rows = [struct.unpack_from('<4i', rows_raw, 16 * (MUSIC_ROWS + i)) for i in range(VOICE_ROWS)]
    search = [elf.read(a, 32).split(b'\0')[0] for a in SEARCH_NAMES]
    for name in search:
        if not name.startswith(b'\\') or len(name) >= 32:
            raise ValueError(f'unexpected search name {name!r}')

    sources = []
    if args.music or args.voice:
        if not (args.music and args.voice and args.music_lsn is not None and args.voice_lsn is not None):
            raise ValueError('--music/--voice need both files and --music-lsn/--voice-lsn')
        for path, lsn, disc_path in ((args.music, args.music_lsn, PATHS[0]), (args.voice, args.voice_lsn, PATHS[1])):
            size = path.stat().st_size
            sources.append(dict(lsn=lsn, size=size, path=disc_path.replace('/', '\\') + ';1',
                                read=lambda s, n, p=path: p.read_bytes()[s * SECTOR:s * SECTOR + n]))
    else:
        iso = (args.iso or decomp / 'Extermination-rebuilt.iso').resolve()
        for disc_path in PATHS:
            lsn, size, path = iso_entry(iso, disc_path)
            sources.append(dict(lsn=lsn, size=size, path=path,
                                read=lambda s, n, base=lsn: iso_read(iso, base + s, n)))
    for source in sources:
        if source['size'] % SECTOR:
            raise ValueError(f"{source['path']}: size {source['size']} is not whole sectors")
    for source, name in zip(sources, search):
        if source['path'].encode() != name:
            raise ValueError(f"directory path {source['path']} does not match the ELF name {name!r}")

    music_cues, voice_cues = cue_list(elf)
    extents = []
    for file_index, (rows, cues) in enumerate(((music_rows, music_cues), (voice_rows, voice_cues))):
        for lo, hi in extents_for(rows, cues, file_index, sources[file_index]['size'] // SECTOR):
            extents.append((file_index, lo, hi))

    table_end = HEADER + len(rows_raw) + 16 * len(extents)
    blobs, records, offset, report_extents = [], [], table_end, []
    for file_index, lo, hi in extents:
        data = sources[file_index]['read'](lo, (hi - lo) * SECTOR)
        if len(data) != (hi - lo) * SECTOR:
            raise ValueError('short read from the stream source')
        lsn = sources[file_index]['lsn'] + lo
        records.append(struct.pack('<4I', lsn, hi - lo, offset, file_index))
        report_extents.append(dict(file=file_index, lsn=lsn, sectors=hi - lo, offset=offset,
                                   sha256=hashlib.sha256(data).hexdigest()))
        blobs.append(data)
        offset += len(data)

    header = struct.pack('<4sI4I4I', b'EMST', 1,
                         sources[0]['lsn'], sources[1]['lsn'],
                         sources[0]['size'] // SECTOR, sources[1]['size'] // SECTOR,
                         MUSIC_ROWS, VOICE_ROWS, len(extents), 0)
    header += b''.join(name.ljust(32, b'\0') for name in search)
    header += b''.join(s['path'].encode().ljust(32, b'\0') for s in sources)
    assert len(header) == HEADER
    blob = header + rows_raw + b''.join(records) + b''.join(blobs)
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / 'streams.emst').write_bytes(blob)
    report = dict(format='EMST 1', music_lsn=sources[0]['lsn'], voice_lsn=sources[1]['lsn'],
                  paths=[s['path'] for s in sources], search=[n.decode() for n in search],
                  music_cues={str(k): v for k, v in music_cues.items()},
                  voice_cues={str(k): v for k, v in voice_cues.items()},
                  extents=report_extents, bytes=len(blob),
                  sha256=hashlib.sha256(blob).hexdigest())
    (args.out / 'streams.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--decomp-root', type=Path, default=Path(__file__).resolve().parents[1].parent / 'Extermination')
    p.add_argument('--iso', type=Path)
    p.add_argument('--music', type=Path)
    p.add_argument('--voice', type=Path)
    p.add_argument('--music-lsn', type=int)
    p.add_argument('--voice-lsn', type=int)
    p.add_argument('--elf', type=Path)
    p.add_argument('--out', type=Path, default=Path(__file__).resolve().parents[1] / 'assets/streams')
    args = p.parse_args()
    try:
        report = export(args)
    except (OSError, ValueError, struct.error) as error:
        p.exit(1, f'error: {error}\n')
    print(f"streams: {len(report['extents'])} extents, {report['bytes']:,} bytes, "
          f"music LSN {report['music_lsn']}, voice LSN {report['voice_lsn']} -> {args.out}/streams.emst")


if __name__ == '__main__':
    main()
