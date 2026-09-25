#!/usr/bin/env python3
"""Export the message service's data from the user's own files (WP-8).

docs/MESSAGE_SERVICE.md, docs/MESSAGE_DRAW.md. Reads the pinned boot ELF and
two message banks the user extracted from their own disc, and writes
assets/message/message_data.emmd (ignored; never committed):

  ELF (addresses are the original's):
    D_00264DD0[0] and [area + 1]  the global and area record tables (8-byte
                                  records), each sized by the original's own
                                  pointers: a table ends where the next table
                                  the original points at begins (every
                                  non-zero D_00264DD0 word, D_00275848[2] and
                                  D_00264E40[0x17]; docs/MESSAGE_SERVICE.md)
    D_0026EC60                    the stream table rows up to the -1 row
    D_0026EC10                    the 16 colour words
    D_00264CD0 / D_00264BF0       the line config and the 001FC770 template
                                  (words 0..4; +0x14 is checked to be
                                  &D_00275C50 and 0)
    &D_00264D10                   the address token 001FC9B0 stores at +0x20
  disc banks (loaded at *D_0028A4E8 / *D_0028A594 by the original):
    extract/chunk03/f14_id16.bin at 0        the global bank
    extract/chunk15/f12_id44.bin at 0x3E800  the AREA11 bank
    each sized by its own offsets (header, entries, records, string pool).

Only AREA11 (area 11) is exported: the port's message service faults on any
other area's table, as the first level needs none.

.emmd v1 (little-endian):
  0   "EMMD"   4 u32 version 1   8 u32 area
  12  u32 global_count   16 u32 area_count   20 u32 stream_count
  24  u32 default_cursor 28 u32 global_bank_size 32 u32 area_bank_size
  36  i32 colors[16]     100 i32 line_config[5]  120 i32 template[5]
  140 global records, area records (8 bytes each), stream rows (16 bytes
      each), the global bank, the area bank.

The mode-3 / mode-4 presenters' data (001FCB90, 001FCF60, 001FCF90,
001FD0E0, 001FDDB0; docs/MESSAGE_PRESENTER_REST.md) goes to a second file,
assets/message/message_presenters.emmp, so message_data.emmd stays exactly
as before:

  disc files (loaded at the pointer cell's address by the original; each is
  the leading bytes of one extracted file, the rest sector padding):
    extract/chunk00/f02_id02.bin  the help container *D_0028A498
    extract/chunk00/f03_id03.bin  the record container *D_0028A49C
                                  (a container: header word 0 + word 2
                                  bytes; every 16-byte entry at +0x10 is a
                                  message bank checked like the two above)
    extract/chunk03/f15_id17.bin  the cue bank *D_0028A4EC
  ELF:
    D_00264CF0 / D_00264C90       the configs of 001FCF90 / 001FD0E0 (words
                                  0..4; +0x14 is checked to be &D_00275830
                                  and &D_00275820)
    D_00275830 / D_00275820       those two style blocks (8 bytes each)
    D_00264DB0                    001FD0E0's six counter words
    &D_00264C90                   the address token 001FD0E0 stores at +0x20

.emmp v1 (little-endian):
  0   "EMMP"   4 u32 version 1
  8   u32 help_size   12 u32 records_size   16 u32 cue_size
  20  u32 &D_00264C90
  24  i32 D_00264CF0[5]   44 i32 D_00264C90[5]
  64  u8 D_00275830[8]    72 u8 D_00275820[8]   80 u32 D_00264DB0[6]
  104 the help container, the record container, the cue bank.

Usage (macOS arm64, port repo root):
  python3 tools/export_message_data.py
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
AREA = 11
TABLES, TABLE_WORDS = 0x264DD0, 24          # D_00264DD0 .. D_00264E30
STREAM_NAMES, STREAM_LISTS = 0x275848, 0x264E40
STREAMS, COLORS, LINE_CFG, TEMPLATE, STYLE, CURSOR = 0x26EC60, 0x26EC10, 0x264CD0, 0x264BF0, 0x275C50, 0x264D10
BANKS = {'global': ('extract/chunk03/f14_id16.bin', 0), 'area': ('extract/chunk15/f12_id44.bin', 0x3E800)}
# The mode-3 / mode-4 presenters (.emmp).
CONTAINERS = {'help': 'extract/chunk00/f02_id02.bin', 'records': 'extract/chunk00/f03_id03.bin'}
CUE_BANK = 'extract/chunk03/f15_id17.bin'
CFG_CF0, CFG_C90, STYLE_830, STYLE_820, COUNTERS = 0x264CF0, 0x264C90, 0x275830, 0x275820, 0x264DB0


class Elf:
    def __init__(self, path):
        self.data = path.read_bytes()
        if hashlib.sha256(self.data).hexdigest() != ELF_SHA256:
            raise ValueError('not the pinned SCUS-97112 boot ELF')

    def read(self, address, size):
        offset = address - 0x100000 + 0x300
        if not 0x300 <= offset <= len(self.data) - size:
            raise ValueError(f'{address:#x} outside the loaded image')
        return self.data[offset:offset + size]

    def u32(self, address):
        return struct.unpack('<I', self.read(address, 4))[0]


def bank_extent(data, base):
    """Header, entries, records and string pool, as the four accessors
    001FE460/480/4B0/4D0 read them (docs/MESSAGE_DRAW.md)."""
    w = lambda at: struct.unpack_from('<I', data, base + at)[0]
    h = w(0) + w(8)
    size = h + w(h) + w(h + 8)
    if base + size > len(data):
        raise ValueError('bank runs past its source file')
    for i in range(w(h + 4)):
        start = base + h + w(h) + w(h + 0x10 + 16 * i)
        if data.index(b'\0', start) >= base + size:
            raise ValueError(f'bank string {i} runs past the bank')
    for i in range(w(4)):
        records = w(0x10 + 16 * i + 0xC) >> 4
        if records and w(0) + w(0x10 + 16 * i) + 16 * records > size:
            raise ValueError(f'bank line {i} records run past the bank')
    return data[base:base + size]


def container_extent(data):
    """A container: header word 0 is the data offset, word 1 the entry
    count, word 2 the data size; 16-byte entries {offset, offset / 16, size,
    padded size} at +0x10, each an ordinary message bank at data offset +
    offset (docs/CENSUS_STANDINS.md 2.3)."""
    w = lambda at: struct.unpack_from('<I', data, at)[0]
    size = w(0) + w(8)
    if size > len(data) or 0x10 + 16 * w(4) > w(0):
        raise ValueError('container runs past its source file')
    for i in range(w(4)):
        offset, sixteenth, entry_size, padded = struct.unpack_from('<4I', data, 0x10 + 16 * i)
        if sixteenth != offset >> 4 or entry_size > padded or w(0) + offset + padded > size:
            raise ValueError(f'container entry {i} is not a well-formed entry')
        if len(bank_extent(data[:w(0) + offset + entry_size], w(0) + offset)) > entry_size:
            raise ValueError(f'container entry {i} runs past its size')
    return data[:size]


def export_presenters(decomp, out, elf=None):
    elf = elf or Elf(decomp / 'config/SCUS_971.12')
    if elf.u32(CFG_CF0 + 0x14) != STYLE_830 or elf.u32(CFG_C90 + 0x14) != STYLE_820:
        raise ValueError('unexpected style pointers in D_00264CF0 / D_00264C90')
    parts = {name: container_extent((decomp / path).read_bytes()) for name, path in CONTAINERS.items()}
    parts['cue'] = bank_extent((decomp / CUE_BANK).read_bytes(), 0)
    header = struct.pack('<4s5I', b'EMMP', 1, len(parts['help']), len(parts['records']), len(parts['cue']),
                         CFG_C90)
    body = (elf.read(CFG_CF0, 20) + elf.read(CFG_C90, 20) + elf.read(STYLE_830, 8) + elf.read(STYLE_820, 8) +
            elf.read(COUNTERS, 24) + parts['help'] + parts['records'] + parts['cue'])
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(header + body)
    return {'help_bytes': len(parts['help']), 'records_bytes': len(parts['records']),
            'cue_bytes': len(parts['cue']), 'sha256': hashlib.sha256(header + body).hexdigest()}


def export(decomp, out):
    elf = Elf(decomp / 'config/SCUS_971.12')
    pointers = [elf.u32(TABLES + 4 * i) for i in range(TABLE_WORDS)]
    starts = {p for p in pointers if p}
    starts |= {elf.u32(STREAM_NAMES + 4 * i) for i in range(2)}
    starts |= {elf.u32(STREAM_LISTS + 4 * i) for i in range(0x17)}

    def table(index):
        pointer = pointers[index]
        if not pointer:
            raise ValueError(f'D_00264DD0[{index}] is 0')
        count = (min(s for s in starts if s > pointer) - pointer) // 8
        return count, elf.read(pointer, 8 * count)
    global_count, global_records = table(0)
    area_count, area_records = table(AREA + 1)
    rows = []
    for i in range(512):
        row = elf.read(STREAMS + 16 * i, 16)
        if struct.unpack_from('<i', row)[0] == -1:
            break
        rows.append(row)
    else:
        raise ValueError('D_0026EC60 has no -1 row')
    if elf.u32(LINE_CFG + 0x14) != STYLE or elf.u32(TEMPLATE + 0x14) != 0:
        raise ValueError('unexpected style pointers in D_00264CD0 / D_00264BF0')
    banks = {}
    for name, (path, base) in BANKS.items():
        banks[name] = bank_extent((decomp / path).read_bytes(), base)
    header = struct.pack('<4s8I', b'EMMD', 1, AREA, global_count, area_count, len(rows), CURSOR,
                         len(banks['global']), len(banks['area']))
    body = (elf.read(COLORS, 64) + elf.read(LINE_CFG, 20) + elf.read(TEMPLATE, 20) + global_records +
            area_records + b''.join(rows) + banks['global'] + banks['area'])
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(header + body)
    return {'area': AREA, 'global_records': global_count, 'area_records': area_count,
            'stream_rows': len(rows), 'global_bank_bytes': len(banks['global']),
            'area_bank_bytes': len(banks['area']), 'sha256': hashlib.sha256(header + body).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--decomp', type=Path, default=ROOT.parent / 'Extermination')
    parser.add_argument('--out', type=Path, default=ROOT / 'assets/message/message_data.emmd')
    parser.add_argument('--presenters-out', type=Path, default=ROOT / 'assets/message/message_presenters.emmp')
    args = parser.parse_args()
    try:
        report = export(args.decomp, args.out)
        report['presenters'] = export_presenters(args.decomp, args.presenters_out)
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f'error: {error}\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
