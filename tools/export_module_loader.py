#!/usr/bin/env python3
"""Export the disc sectors the screen-module loader reads (docs/MODULE_LOADER.md).

The native loader (src/game/em_module_loader.c) runs the original loader's
steps (001FF080 -> 001FF0D0 -> 001FF830 / 001FF3F0 -> 00200780 / 00200730)
and answers every read from this pack at host speed. For each requested
module the pack holds the module's INDEX.IDX header sector and every
DATA.DAT span that header makes the loader read (its chunk entries and its
payload), exactly as 00200780 rounds them to sectors. A read the pack does
not hold faults at run time (fail-stop); nothing is synthesised.

Inputs (the user's own, read in place, nothing modified):
  --iso      the user's disc image (default ../Extermination/Extermination-rebuilt.iso).
             This alone is enough: the file descriptors D_0028A480 / D_0028A488
             are the ISO-9660 extents of DATA/INDEX.IDX and DATA/DATA.DAT.
  --capture  optional: an original RAM capture (a folder holding eeMemory.bin)
             for developer checks and for its loader cursors.
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

EMML version 1 (little endian):
  0x00 'EMML', u32 1, u32 range count, u32 0
  0x10 u32 D_0028A480 lsn, size, D_0028A488 lsn, size
  0x20 u32 D_00275C70, D_00275C74, D_0028A5A0, D_0028A738, D_0028A73C,
       D_0028A744, D_0028A748 (the captured loader globals), u32 0
  0x40 ranges {u32 lsn, u32 sectors, u32 file offset, u32 0}, then data.
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
DEFAULT_MODULES = (0x21,)
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


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--iso', type=Path, default=DECOMP / 'Extermination-rebuilt.iso')
    ap.add_argument('--capture', type=Path, default=None,
                    help='optional original RAM capture folder (eeMemory.bin): checks and cursor seeds')
    ap.add_argument('--modules', default=','.join(f'{m:#x}' for m in DEFAULT_MODULES),
                    help='comma-separated module ids (default 0x21)')
    ap.add_argument('--out', type=Path, default=ROOT / 'assets/module_loader/modules.emml')
    args = ap.parse_args()
    modules = [int(m, 0) for m in args.modules.split(',') if m.strip()]
    if not args.iso.is_file():
        raise SystemExit(f'{args.iso}: the user\'s disc image is required')
    disc = Disc(args.iso)
    index, data = disc.index, disc.data
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
    order = sorted(ranges)
    blob = bytearray(struct.pack('<4sIII', b'EMML', 1, len(order), 0))
    blob += struct.pack('<4I', *index, *data)
    blob += struct.pack('<8I', *cursors, 0)
    offset = 0x40 + 0x10 * len(order)
    table, payload = bytearray(), bytearray()
    for lsn, sectors in order:
        table += struct.pack('<4I', lsn, sectors, offset + len(payload), 0)
        payload += disc.sectors(lsn, sectors)
    blob += table + payload
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(bytes(blob))
    report = dict(modules=[f'{m:#x}' for m in modules], ranges=len(order), bytes=len(blob),
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
