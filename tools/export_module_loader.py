#!/usr/bin/env python3
"""Export the disc sectors the screen-module loader reads (docs/MODULE_LOADER.md).

The native loader (src/game/em_module_loader.c) runs the original loader's
steps (001FF080 -> 001FF0D0 -> 001FF830 / 001FF3F0, or the area streamer
001FFCD0 / 001FF590 -> 00200780 / 00200730) and answers every read from this
pack at host speed. For each requested module the pack holds the module's
INDEX.IDX header sector and every DATA.DAT span that header makes the loader
read (its chunk entries and its payload); for each requested area and room,
the area's overlay file, its INDEX.IDX header sector (area + 4) and every
DATA.DAT span 001FFCD0 reads (the sound bank entry 0, the A entries, the
resident region, and the same three of the room's nested block when the
header has nested blocks); all exactly as 00200780 rounds them to sectors. A read the
pack does not hold faults at run time (fail-stop); nothing is synthesised.
Default: module 3 (the New Game's 001AD1A0), the status pages' modules the
first level loads (PAGE_MODULES: 0x1F the ITEM root, 0x1E MAP, 0x2C SPR4,
0x24 DATABASE, the ITEM children 0x20 / 0x21 BATTERY / 0x22 / 0x23 and the
SPR4 part pages 0x2D..0x31; docs/STATUS_PAGES.md section 1), the game-over
screen module 0x27 (001AD4E0's 001FF080(0, 0x27); docs/DAMAGE.md), area 0x0B
(AREA11, the New Game's 001FF080(1, 0)) and area 1 room 0 (AREA01 sub 0, the
level exit's load).

Inputs (the user's own, read in place, nothing modified):
  --iso      the user's disc image (default ../Extermination/Extermination-rebuilt.iso).
             This alone is enough: the file descriptors D_0028A480 / D_0028A488
             are the ISO-9660 extents of DATA/INDEX.IDX and DATA/DATA.DAT.
  --capture  optional: an original RAM capture (a folder holding eeMemory.bin)
             for developer checks and for its loader cursors.
  --elf      the user's pinned boot ELF (default ../Extermination/config/
             SCUS_971.12, the file the other exporters read; SHA-256 checked).
The ELF gives three tables the boot leaves in RAM (addresses and sizes):
  * D_0028A3C0, 0x17 {lsn, size}: the boot's 001FEE60 looks up each name of
    the ELF's D_00264E40 table (\\OVERLAY\\AREAnn.BIN;1) in the disc
    directory; the exporter does the same lookup in the ISO;
  * D_00275304[0]: the overlay arena (the area overlay's destination);
  * D_00264890: the five SPU base addresses of the sound-bank buckets
    (001FB3E0 state 1).
Loader cursor seeds (the pack's 0x20 block): the values D_00275C70,
D_00275C74, D_0028A5A0, D_0028A738, D_0028A73C, D_0028A744, D_0028A748 hold
when the first level runs. Earlier loads the port does not run (the boot
loader 001FF1E0, the New Game module-3 load, the area streamer 001FFCD0)
set them. Without --capture the pack gets AREA11_SEEDS, the values every
first-level capture shows (docs/MODULE_LOADER.md section 1.8); with
--capture it gets that capture's values, and the receipt says which.
They are addresses, not disc data.
Checks with --capture:
  * the disc extents equal the captured descriptors D_0028A480 / D_0028A488;
  * the captured header buffer D_00289BC0 equals the disc's header sector of
    the module it names;
  * the captured load destination D_00275C74 holds exactly the disc bytes of
    that module's chunks (when the capture still has them);
  * whether the captured cursors equal AREA11_SEEDS (reported).
Output (ignored, disc-derived): assets/module_loader/modules.emml and a
receipt modules.json (counts, hashes and check results only).

EMML version 2 (little endian):
  0x00 'EMML', u32 2, u32 range count, u32 0
  0x10 u32 D_0028A480 lsn, size, D_0028A488 lsn, size
  0x20 u32 D_00275C70, D_00275C74, D_0028A5A0, D_0028A738, D_0028A73C,
       D_0028A744, D_0028A748 (the captured loader globals), u32 0
  0x40 u32 D_00275304[0], u32 0, u32 D_00264890[0..4], u32 0
  0x60 D_0028A3C0: 0x17 x {u32 lsn, u32 size}, then 8 zero bytes
  0x120 ranges {u32 lsn, u32 sectors, u32 file offset, u32 0}, then data.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
SECTOR = 0x800
D_00289BC0, D_0028A480, D_0028A488 = 0x289BC0, 0x28A480, 0x28A488
CURSORS = (0x275C70, 0x275C74, 0x28A5A0, 0x28A738, 0x28A73C, 0x28A744, 0x28A748)
# 0020CDC0 phase 3 (pages 0..3: 0x1F, 0x1E, 0x2C, 0x24), the ITEM root's
# children (0x20, 0x21, 0x22, 0x23) and SPR4's part pages (0x2D..0x31).
PAGE_MODULES = (0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31)
# 001AD4E0 step 1's 001FF080(0, 0x27): the game-over screen (the player's
# death; port docs/DAMAGE.md section 5).
SCREEN_MODULES = (0x27,)
DEFAULT_MODULES = (3,) + PAGE_MODULES + SCREEN_MODULES
# (area, room): AREA11 (the New Game's 001FF080(1, 0)) and AREA01 sub 0 (the
# level exit's load: Roger's departure requests 001B0C60(1, 0, 4); route beat
# 15, docs/FIRST_LEVEL_EXIT.md).
DEFAULT_AREAS = ((0x0B, 0), (0x01, 0))
HEADER = 0x120
AREA_FILES = 0x17
D_00264E40, D_00275304, D_00264890 = 0x264E40, 0x275304, 0x264890
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
# The loader cursors (CURSORS order) at the first level's entry. The five
# allocation cursors D_0028A5A0..D_0028A748 are equal in all 21 AREA11
# captures (build/s87/route 00..14, startup-reference, c7cap). D_00275C70 is
# D_00289BC0 in all of them. D_00275C74 is 0x10E99C0 before the level's first
# page load (route 00) and that page's destination afterwards; every bank
# load writes D_00275C70 and D_00275C74 before it reads them. Without
# --capture the exporter rebuilds them from the disc (disc_seeds) and
# requires these values.
AREA11_SEEDS = (0x289BC0, 0x10E99C0, 0x1516F40, 0x10E99C0, 0x1335F40, 0x19A3F40, 0x19A3F40)


def disc_seeds(iso: Path) -> tuple:
    """The CURSORS values at the first level's entry, rebuilt from the
    user's disc with the loaders' rules (tools/export_disc_textures_gs.py
    ResourceTable: the boot's 001FF1E0 loads, the title modules, the New
    Game's module 3 and the AREA11 load): D_00275C70 is the header buffer
    D_00289BC0 the area load's state 4 publishes; D_00275C74 is module 3's
    destination (001FF830 state 0, ids 2 / 3 -> D_0028A738); D_0028A5A0 is
    D_0028A490[0x44]; the four allocation cursors are the model's."""
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import export_disc_textures_gs as G
    table = G.ResourceTable(G.Disc(iso))
    c = table.cursors
    module3 = next(base for caller, sector, base, _n, _s in table.loads if sector == 3)
    return (D_00289BC0, module3, table.words[0x44], c['D_0028A738'], c['D_0028A73C'],
            c['D_0028A744'], c['D_0028A748'])


def u32(b, o):
    return struct.unpack_from('<I', b, o)[0]


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


class Disc:
    """INDEX.IDX / DATA.DAT extents of an ISO-9660 image, read in place."""

    def __init__(self, path: Path):
        self.path = path
        with path.open('rb') as f:
            pvd = self._sector(f, 16)
            if pvd[1:6] != b'CD001':
                raise SystemExit(f'{path}: not an ISO-9660 image')
            data = self._lookup(f, pvd[156:190], b'DATA')
            self.index = self._extent(self._lookup(f, data, b'INDEX.IDX'))
            self.data = self._extent(self._lookup(f, data, b'DATA.DAT'))
            self._root = pvd[156:190]

    def lookup(self, path: str):
        """(lsn, size) of an ISO path such as \\OVERLAY\\AREA11.BIN;1."""
        with self.path.open('rb') as f:
            record = self._root
            for part in path.strip('\\').split('\\'):
                record = self._lookup(f, record, part.split(';')[0].encode())
        return self._extent(record)


    @staticmethod
    def _sector(f, lsn, n=1):
        f.seek(lsn * SECTOR)
        return f.read(n * SECTOR)

    @staticmethod
    def _extent(record):
        return u32(record, 2), u32(record, 10)

    def _lookup(self, f, record, name):
        lsn, size = self._extent(record)
        blob = self._sector(f, lsn, (size + SECTOR - 1) // SECTOR)
        pos = 0
        while pos < size:
            n = blob[pos]
            if n == 0:
                pos = (pos // SECTOR + 1) * SECTOR
                continue
            if blob[pos + 33:pos + 33 + blob[pos + 32]].split(b';')[0] == name:
                return blob[pos:pos + n]
            pos += n
        raise SystemExit(f'{self.path}: {name.decode()} not found')

    def sectors(self, lsn, n):
        with self.path.open('rb') as f:
            out = self._sector(f, lsn, n)
        if len(out) != n * SECTOR:
            raise SystemExit(f'{self.path}: short read at sector {lsn:#x}')
        return out


def read_span(file_desc, offset, size):
    """The (lsn, sectors) 00200780 asks 00112440 for (size >= 0)."""
    if size < 0:
        raise SystemExit('a negative read size (the whole-file form) is not exported')
    sectors = s32(size + 0x7FF) >> 11
    return (file_desc[0] + (s32(offset) >> 11)) & 0xFFFFFFFF, sectors


def module_reads(disc, index, data, module):
    """[(what, lsn, sectors)] of a whole 001FF080(0, module) load."""
    reads = [('header', *read_span(index, module << 11, 0x800))]
    header = disc.sectors(reads[0][1], reads[0][2])
    base, total, resident = u32(header, 4), u32(header, 8), u32(header, 0x14)
    count = struct.unpack_from('<H', header, 0xE)[0]
    for i in range(count):  # 001FF3F0: entries 0..count-1
        off, size = u32(header, 0x20 + 8 * i), u32(header, 0x24 + 8 * i)
        reads.append((f'chunk{i}', *read_span(data, base + off, s32(size))))
    reads.append(('payload', *read_span(data, base + resident, s32(total - resident))))
    return header, reads


def boot_tables(disc, elf_path: Path):
    """D_0028A3C0 (0x17 {lsn, size}), D_00275304[0] and D_00264890[0..4]
    from the pinned ELF, the area file names looked up in the disc's
    directory (see the module docstring)."""
    elf = elf_path.read_bytes()
    if hashlib.sha256(elf).hexdigest() != ELF_SHA256:
        raise SystemExit(f'{elf_path}: not the pinned boot ELF')

    def word(address):
        return u32(elf, address - 0x100000 + 0x300)

    def string(address):
        at = address - 0x100000 + 0x300
        return elf[at:elf.index(b'\0', at)].decode('ascii')

    names = [string(word(D_00264E40 + 4 * i)) for i in range(AREA_FILES)]
    return ([disc.lookup(n) for n in names], names, word(D_00275304),
            [word(D_00264890 + 4 * i) for i in range(5)])


def block_reads(data, block, tag):
    """[(what, lsn, sectors)] 001FFCD0's open phase and resident read make
    for one descriptor `block` (the 0x800-byte header, or a 0x70-byte nested
    descriptor): 001FF590(tag, 0) reads entry 0, the sound bank, when +0x0C
    is non-zero; 001FF590(tag, 1) the A entries +0x0C .. +0x0C + +0x0E - 1;
    state 5 (or 9) the resident region +4 + +0x14, +8 - +0x14 bytes."""
    base, total, resident = u32(block, 4), u32(block, 8), u32(block, 0x14)
    first, count = struct.unpack_from('<HH', block, 0xC)
    reads = []
    if first:
        reads.append((f'{tag}bank', *read_span(data, base + u32(block, 0x20), s32(u32(block, 0x24)))))
    for i in range(first, first + count):
        off, n = u32(block, 0x20 + 8 * i), u32(block, 0x24 + 8 * i)
        reads.append((f'{tag}a{i}', *read_span(data, base + off, s32(n))))
    reads.append((f'{tag}resident', *read_span(data, base + resident, s32(total - resident))))
    return reads


def area_reads(disc, index, data, area, files, room=0):
    """[(what, lsn, sectors)] of a whole 001FF080(1, 0) load of `area` with
    D_00810701 = `room` (001FFCD0 with 001FF590): the overlay file, the
    header sector area + 4, the header's block and, when its +0x18 (the
    nested count) is non-zero, the nested block D_00289BC0 + 0x100 + room *
    0x70 (state 7 publishes it; states 8 and 9 read it)."""
    lsn, size = files[area]
    reads = [('overlay', lsn, (size + 0x7FF) >> 11)]      # 00200780(file, buf, 0, -1)
    reads.append(('header', *read_span(index, (area + 4) << 11, 0x800)))
    header = disc.sectors(reads[-1][1], reads[-1][2])
    reads += block_reads(data, header, '')
    nested = u32(header, 0x18)
    if nested:
        if not 0 <= room < nested:
            raise SystemExit(f'area {area:#x}: room {room} outside its {nested} nested blocks')
        at = 0x100 + room * 0x70
        reads += block_reads(data, header[at:at + 0x70], f'room{room}.')
    elif room:
        raise SystemExit(f'area {area:#x} has no nested block: room {room} is not loadable')
    return header, reads


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--iso', type=Path, default=DECOMP / 'Extermination-rebuilt.iso')
    ap.add_argument('--elf', type=Path, default=DECOMP / 'config/SCUS_971.12')
    ap.add_argument('--capture', type=Path, default=None,
                    help='optional original RAM capture folder (eeMemory.bin): checks and cursor seeds')
    ap.add_argument('--modules', default=','.join(f'{m:#x}' for m in DEFAULT_MODULES),
                    help='comma-separated module ids (default 3 and PAGE_MODULES)')
    ap.add_argument('--areas', default=','.join(f'{a:#x}:{r}' for a, r in DEFAULT_AREAS),
                    help='comma-separated area[:room] for 001FF080(1, 0) (default 0xb:0,0x1:0)')
    ap.add_argument('--out', type=Path, default=ROOT / 'assets/module_loader/modules.emml')
    args = ap.parse_args()
    modules = [int(m, 0) for m in args.modules.split(',') if m.strip()]
    areas = [(int(a.split(':')[0], 0), int(a.split(':')[1], 0) if ':' in a else 0)
             for a in args.areas.split(',') if a.strip()]
    if not args.iso.is_file():
        raise SystemExit(f'{args.iso}: the user\'s disc image is required')
    disc = Disc(args.iso)
    index, data = disc.index, disc.data
    files, names, arena, bases = boot_tables(disc, args.elf)
    checks = {}
    ram = None
    if args.capture is not None:
        ram = (args.capture / 'eeMemory.bin').read_bytes()
        if len(ram) < 0x2000000:
            raise SystemExit(f'{args.capture}: not a 32 MiB EE RAM image')
        captured = ((u32(ram, D_0028A480), u32(ram, D_0028A480 + 4)),
                    (u32(ram, D_0028A488), u32(ram, D_0028A488 + 4)))
        if captured != (index, data):
            raise SystemExit(f'captured descriptors {captured} differ from the disc extents '
                             f'{index}/{data}')
        checks['descriptors'] = 'disc extents equal the capture'
        cap_files = [(u32(ram, 0x28A3C0 + 8 * i), u32(ram, 0x28A3C4 + 8 * i)) for i in range(AREA_FILES)]
        if cap_files != [tuple(f) for f in files] or u32(ram, D_00275304) != arena or \
                [u32(ram, D_00264890 + 4 * i) for i in range(5)] != bases:
            raise SystemExit('the captured D_0028A3C0 / D_00275304 / D_00264890 differ from the disc\'s')
        checks['boot_tables'] = 'D_0028A3C0, D_00275304 and D_00264890 equal the capture'
        cursors = [u32(ram, a) for a in CURSORS]
        checks['cursors'] = ('equal to AREA11_SEEDS' if tuple(cursors) == AREA11_SEEDS else
                             'differ from AREA11_SEEDS: ' + ', '.join(
                                 f'{a:#x}={v:#x}' for a, v, s in zip(CURSORS, cursors, AREA11_SEEDS) if v != s))
        seeds = f'capture {args.capture}'
    else:
        cursors = list(disc_seeds(args.iso))
        if tuple(cursors) != AREA11_SEEDS:
            raise SystemExit('the cursors rebuilt from the disc differ from AREA11_SEEDS: ' + ', '.join(
                f'{a:#x}={v:#x}' for a, v, s in zip(CURSORS, cursors, AREA11_SEEDS) if v != s))
        seeds = 'the disc (ResourceTable), equal to AREA11_SEEDS'
    ranges, receipt = {}, []
    for module in modules:
        header, reads = module_reads(disc, index, data, module)
        for what, lsn, sectors in reads:
            if sectors:
                ranges[(lsn, sectors)] = None
            receipt.append(dict(module=module, read=what, lsn=lsn, sectors=sectors))
        if ram is not None and u32(ram, D_00289BC0) == module:
            if ram[D_00289BC0:D_00289BC0 + 0x800] != header:
                raise SystemExit(f'captured D_00289BC0 differs from module {module:#x}\'s header')
            checks[f'{module:#x}_header'] = 'equal to the capture'
            dest = u32(ram, 0x275C74)
            count = struct.unpack_from('<H', header, 0xE)[0]
            length = sum(u32(header, 0x24 + 8 * i) for i in range(count))
            chunks = b''.join(disc.sectors(lsn, n) for what, lsn, n in reads
                              if what.startswith('chunk'))[:length]
            if ram[dest:dest + length] == chunks:
                checks[f'{module:#x}_chunks'] = f'equal to the capture at {dest:#x} ({length:#x} bytes)'
            else:
                checks[f'{module:#x}_chunks'] = 'not resident in the capture'
    for area, room in areas:
        if not 0 <= area < AREA_FILES:
            raise SystemExit(f'area {area:#x}: outside D_0028A3C0')
        _header, reads = area_reads(disc, index, data, area, files, room)
        for what, lsn, sectors in reads:
            if sectors:
                ranges[(lsn, sectors)] = None
            receipt.append(dict(area=area, room=room, read=what, lsn=lsn, sectors=sectors))
    order = sorted(ranges)
    blob = bytearray(struct.pack('<4sIII', b'EMML', 2, len(order), 0))
    blob += struct.pack('<4I', *index, *data)
    blob += struct.pack('<8I', *cursors, 0)
    blob += struct.pack('<8I', arena, 0, *bases, 0)
    for lsn, size in files:
        blob += struct.pack('<2I', lsn, size)
    blob += bytes(HEADER - len(blob))
    offset = HEADER + 0x10 * len(order)
    table, payload = bytearray(), bytearray()
    for lsn, sectors in order:
        table += struct.pack('<4I', lsn, sectors, offset + len(payload), 0)
        payload += disc.sectors(lsn, sectors)
    blob += table + payload
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(bytes(blob))
    report = dict(modules=[f'{m:#x}' for m in modules], areas=[f'{a:#x}:{r}' for a, r in areas],
                  ranges=len(order), bytes=len(blob),
                  area_files={n: [hex(v) for v in f] for n, f in zip(names, files)},
                  d275304=hex(arena), d264890=[hex(v) for v in bases],
                  sha256=hashlib.sha256(blob).hexdigest(), reads=receipt,
                  descriptors=dict(d28A480=[hex(v) for v in index], d28A488=[hex(v) for v in data]),
                  cursors={f'{a:#08x}': hex(v) for a, v in zip(CURSORS, cursors)},
                  cursor_source=seeds, capture=str(args.capture) if args.capture else None,
                  checks=checks)
    args.out.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'module loader pack: {len(order)} ranges, {len(blob)} bytes -> {args.out} (cursors: {seeds})')
    for k, v in checks.items():
        print(f'  {k}: {v}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
