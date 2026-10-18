#!/usr/bin/env python3
"""Whole area loads against original instructions and exact disc sectors.

Uses the unchanged module-loader oracle: each callee, frame snapshot, store,
delivered byte, upload and area latch must agree. Sound-bank completion is
an explicit shared boundary, returning the captured post-bank cursors; this
test does not certify the sound worker or a native gameplay transition.
All fifteen area/sub pairs run only with EM_TEST_FULL=1.
"""
import argparse
import ctypes as CT
import hashlib
import json
from pathlib import Path
import struct

import export_level_module_pack as E
import export_module_loader as M
import level_readiness as R
import reference_mode as mode
import test_module_loader_reference as T

ROOT = E.ROOT
OUT = ROOT/'build/levels/module-pack-reference'


class MemoryView:
    """Read every native byte across adjacent host regions, without gaps.

    WholeLoad compares a delivery as one EE interval. An unaligned overlay
    BSS clear splits the host allocation at the file end; its two exact
    byte intervals still tile that delivery. Join only the public native
    memory views for comparison. Missing bytes remain a failed read.
    """
    def __init__(self, lib):
        self.lib = lib
        lib.em_module_loader_memory_rest.argtypes = [CT.c_void_p, CT.c_uint32,
                                                     CT.POINTER(CT.c_uint32)]
        lib.em_module_loader_memory_rest.restype = CT.c_void_p
        self.buffer = None
        self.joined_reads = 0

    def __getattr__(self, name):
        return getattr(self.lib, name)

    def em_module_loader_memory(self, loader, address, size):
        direct = self.lib.em_module_loader_memory(loader, address, size)
        if direct:
            return direct
        chunks, count = [], 0
        while count < size:
            available = CT.c_uint32()
            pointer = self.lib.em_module_loader_memory_rest(loader, address+count,
                                                           CT.byref(available))
            if not pointer or not available.value:
                return None
            take = min(available.value, size-count)
            chunks.append(CT.string_at(pointer, take))
            count += take
        self.buffer = CT.create_string_buffer(b''.join(chunks))
        self.joined_reads += 1
        return CT.addressof(self.buffer)


def read_pack(path):
    raw = path.read_bytes()
    report = json.loads(path.with_suffix('.json').read_text())
    assert hashlib.sha256(raw).hexdigest() == report['sha256'], 'pack hash'
    magic, version, count, zero = struct.unpack_from('<4sIII', raw)
    assert (magic, version, zero) == (b'EMML', 2, 0), 'pack header'
    assert set(report['areas']) == {f'{a:#x}:{s}' for a, s in E.AREAS}, 'area coverage'
    ranges = [struct.unpack_from('<4I', raw, M.HEADER+16*i) for i in range(count)]
    end = M.HEADER+16*count
    for lsn, sectors, at, reserved in ranges:
        assert sectors > 0 and reserved == 0 and at == end, 'range layout'
        end = at + sectors*M.SECTOR
        assert end <= len(raw), 'truncated range'
    assert end == len(raw), 'trailing bytes'
    assert len({(r[0], r[1]) for r in ranges}) == len(ranges), 'duplicate range'
    return raw, ranges


def compare_span(raw, ranges, disc, lsn, sectors):
    matching = [r for r in ranges if r[0] <= lsn and lsn+sectors <= r[0]+r[1]]
    assert matching, 'missing disc span'
    row = min(matching, key=lambda r: r[1])
    at = row[2]+(lsn-row[0])*M.SECTOR
    assert raw[at:at+sectors*M.SECTOR] == disc.sectors(lsn, sectors), 'disc bytes differ'


def check_sectors(path):
    raw, ranges = read_pack(path)
    disc = M.Disc(M.DECOMP/'Extermination-rebuilt.iso')
    files, _, _, _ = M.boot_tables(disc, M.DECOMP/'config/SCUS_971.12')
    results = []
    for area, sub in E.AREAS:
        _, reads = M.area_reads(disc, disc.index, disc.data, area, files, sub)
        count = 0
        for _, lsn, sectors in reads:
            if sectors:
                compare_span(raw, ranges, disc, lsn, sectors)
                count += sectors*M.SECTOR
        results.append(dict(area=area, sub=sub, reads=len(reads), compared_bytes=count))
    for module in M.DEFAULT_MODULES:
        _, reads = M.module_reads(disc, disc.index, disc.data, module)
        for _, lsn, sectors in reads:
            if sectors:
                compare_span(raw, ranges, disc, lsn, sectors)
    # The same exact comparator must refuse missing coverage and one changed
    # byte. The fixture is one sector, not a copy of the entire large pack.
    lsn, _, _, _ = ranges[0]
    sector = disc.sectors(lsn, 1)
    for data, rows in ((sector, []), (bytes([sector[0]^1])+sector[1:], [(lsn, 1, 0, 0)])):
        try:
            compare_span(data, rows, disc, lsn, 1)
        except AssertionError:
            pass
        else:
            raise AssertionError('negative disc comparator case accepted')
    return results


def captures():
    """First complete retained original snapshot for each requested pair."""
    found = {(11, 0): T.ROUTE/'03_panel_power'}
    for group in dict.fromkeys(g for groups in R.GROUPS.values() for g in groups):
        folder = M.DECOMP/'build/s87'/f'route_{group}'
        for path in sorted(folder.glob(group+'_*')):
            ram = path/'eeMemory.bin'
            if not ram.is_file() or not (path/'scratchpad.bin').is_file():
                continue
            with ram.open('rb') as stream:
                stream.seek(0x810700)
                pair = tuple(stream.read(2))
            if pair in E.AREAS:
                found.setdefault(pair, path)
    assert set(found) == set(E.AREAS), 'missing retained area snapshot'
    return found


def check_original(elf, lib, pack, pair, capture):
    ram = (capture/'eeMemory.bin').read_bytes()
    spad = (capture/'scratchpad.bin').read_bytes()
    T.assert_code_is_elf(elf, ram, capture.name)
    w = T.WholeLoad(elf, lib, ram, spad, pack, seed_from_pack=True)
    try:
        # Request inputs are explicit fixture inputs on both sides. Loading
        # module 3 first establishes the shared player resources and cursors.
        assert w.run(3, limit=800) is not None
        for address, value in ((0x810700, pair[0]), (0x810701, pair[1])):
            w.o.save(address, value, 1)
            w.area[address].value = value
        w.o.save(0x810CA6, 0, 1)
        w.ca6.value = 0
        disc = M.Disc(M.DECOMP/'Extermination-rebuilt.iso')
        files, _, _, _ = M.boot_tables(disc, M.DECOMP/'config/SCUS_971.12')
        _, reads = M.area_reads(disc, disc.index, disc.data, pair[0], files, pair[1])
        boundaries = [M.u32(ram, 0x28A740 if name.startswith('room') else 0x28A73C)
                      for name, _, _ in reads if name.endswith('bank')]
        w.bank_script = [v for cursor in boundaries for v in (0, 0, cursor)]
        rows = w.run(0, limit=800, area=True)
        assert rows is not None
        assert w.o.load(0x810703, 1) == pair[0] and w.o.load(0x810704, 1) == pair[1], 'loaded area/sub latches'
        assert w.bank_calls == w.o_bank_calls == 3*len(boundaries), 'bank boundary count'
        return dict(area=pair[0], sub=pair[1], dispatches=len(rows),
                    bank_completions=len(boundaries), capture=str(capture.relative_to(M.DECOMP)))
    finally:
        w.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pack', type=Path, default=E.DEFAULT)
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    sectors = check_sectors(args.pack)
    caps = captures()
    cases = mode.select(E.AREAS, 3, 15, keep=lambda i, _: i in (0, 5, 13))
    mode.banner(mode.part(len(cases), len(E.AREAS), 'original area loads'))
    T.OUT = OUT
    lib, elf = MemoryView(T.build()), T.read_elf_bytes()
    results = []
    for pair in cases:
        results.append(check_original(elf, lib, args.pack, pair, caps[pair]))
        print(f'AREA{pair[0]:02d}/{pair[1]} original loader: PASS', flush=True)
    (OUT/'report.json').write_text(json.dumps(dict(sectors=sectors, original_loads=results,
        pack_sha256=hashlib.sha256(args.pack.read_bytes()).hexdigest(),
        joined_native_memory_reads=lib.joined_reads,
        scope='loader original-instruction proof; sound completion boundary scripted'), indent=2)+'\n')
    print(f'level module pack: PASS {len(sectors)} complete sector deliveries, {len(results)} original loads')


if __name__ == '__main__':
    main()
