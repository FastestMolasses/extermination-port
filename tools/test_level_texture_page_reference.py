#!/usr/bin/env python3
"""Exact parser parity for all existing page packets, plus the new surface.

Reads the user's pinned ELF. Uploads/constant rows are compared byte-for-byte;
unknown packet source addresses are rejected. Full texture catalog tests run
these original programs over the recorded pages and compare their texels.
"""
import struct

import export_area01_common as C
import chain_page_model as M
import level_texture_page as L


def packet(elf, address):
    off = address-0x100000+0x300
    header = C.u32(elf, off)
    assert header >> 28 & 7 == 1
    size = (header & 0xFFFF)*16
    end = off+16+size
    assert C.u32(elf, end) >> 28 & 7 == 6 and not C.u32(elf, end) & 0xFFFF
    return [(address+16, elf[off+16:end])]


def run(cls, stream):
    class Stream(cls):
        def dma(self, _start):
            self.pass_entries = set()
            return stream
    page = Stream(lambda address, size: bytes(size))
    page.run(0)
    return page


def main():
    elf = C.read_elf()
    fields = ('program', 'parts', 'mpg', 'known', 'base', 'offset', 'tops',
              'cl', 'wl', 'cycle_set', 'cycle_inherited')
    for address in M.PROGRAMS:
        stream = packet(elf, address)
        before, after = run(M.Page, stream), run(L.Page, stream)
        assert all(getattr(before, f) == getattr(after, f) for f in fields), 'existing packet state changed'
        assert before.vu.code == after.vu.code and before.vu.mem == after.vu.mem, 'existing packet bytes changed'
    stream = packet(elf, L.PROGRAM_SURFACE)
    page = run(L.Page, stream)
    assert page.program == L.PROGRAM_SURFACE and page.parts == 1
    first, count, micro = L.SURFACE_MPG
    off = first-0x100000+0x300
    assert page.vu.code[8*micro:8*(micro+count)] == elf[off:off+8*count], 'new original upload'
    assert page.mpg == [(first, micro, count)]
    # Same bytes with false provenance must not be accepted as a known program.
    rejected = 0
    for delta in (4, 8, 16):
        try:
            run(L.Page, [(a+delta, data) for a, data in stream])
        except M.ModelError:
            rejected += 1
    assert rejected == 3
    print(f'PASS page parser: {len(M.PROGRAMS)} unchanged packets, original surface upload, 3 false-source rejections')


if __name__ == '__main__':
    main()
