#!/usr/bin/env python3
"""Check tools/export_disc_textures.py against the ORIGINAL instructions and
the user's own captures (docs/DISC_TEXTURES.md section 5).

Nothing original is embedded here: the pinned boot ELF supplies every
instruction, the user's disc image the loaded bytes, and the user's PCSX2
captures (../Extermination/build/s87/route/<beat>/, build/startup-reference/)
the state and the reference GS memory. build/<lane>/ holds only counts and
the re-exported files (ignored).

A. The loaders, executed unmodified over captured route RAM (FallEE, the
   measured EE core): 00200890 (5 x 5 mode / costume values), 00200970
   (both arguments x three 0x70003B90 values), 001AB7E0 steps 3 and 4,
   001AD1A0 cases 0 and 1, 001FF1E0 (modules 0, 0x1B, 0x1C), 001FF830 with
   001FF3F0 (modules 3, 0x1F, 0x21; EM_TEST_FULL: every module 0x1E..0x31
   and 0x37) and 001FFCD0 with 001FF590 and 00200890 (area 11) until their
   status byte reads 0x63; plus the same loaders over synthetic
   descriptors built from the disc at run time (kinds ending in '*': a
   resident offset, two A and two B sections, B entry offsets that must be
   ignored, an area with a sound bank, B sections and two nested blocks),
   which the route's data never exercise. Hooked leaves: the disc read
   00200780 (answered from the disc image, or its patched view, by the
   handle's {LBA, size}), the polls 00200700 / 00200730 (ready), 002009E0,
   001FB370, 001D19D0, 001CCB10 and the task calls (arguments recorded);
   the hooked set of every executed routine must equal its jal targets.
   Memory is compared at every 00200830 entry: the chain in memory must
   equal the next buffer of the exporter's model, in order and in number.
   After the run: every relocated D_0028A490 slot equals base + its entry
   offset, and the model's slot extent and bytes equal the original's
   pointers and memory; the cursors the loaders write (D_0028A734 /
   D_0028A738 against the entry value, D_0028A740 / 744 / 748, D_0028A73C
   for module 3) and the descriptor pointer D_00275C70 equal the model's
   values; the original's own stores (not the drive's) stay inside the
   named globals; 00200890 and 00200970 store nothing.
B. The rebuilt GS memory against the captures: every block the model
   uploads equals the GS local memory of every AREA11 route capture 00..14,
   the status-hub capture and the opening capture; the page states equal
   the BATTERY (panel) and ITEM root (panel/root) captures; the title
   modules 0x28 / 0x29 upload only blocks the area load overwrites.
C. The outputs: each texture decoded from the disc equals its decode from
   the captures it was exported from (object and page textures: every route
   capture in full mode, 1 in quick; status pages: their capture), the two
   font slots equal the captured RAM at D_0028A490[0] / [1], and every file
   the exporter writes has the pinned SHA-256 of the capture-derived file
   (CAPTURE_SHA256, so the check stays independent after binding). The
   model's buffers for the world, both page states and the font equal the
   pinned table (PINNED_SOURCES).
D. The TEX0 sets: the page set from the ELF / overlay / code constants
   equals the pinned captured set; the status-hub token list written equals
   the pinned capture list (CAPTURE_HUB_TOKENS, export_status_hub.py's list
   over the status-hub capture before it became disc-first); full mode also
   re-executes 00209DF0 over the status-hub capture's RAM and checks the
   exporter executed all 5 x 2 states and the fixture pass.
E. Controls, all of which must be caught: a wrong player texture slot, the
   area's sound-bank entry in place of its upload section, a missing
   library upload, a wrong page module, swapped font slots, an uncovered
   block holding 0x00 / 0xA5 / its texels (the residency check), a one-byte
   extract difference and a span past the chunk's files (the extract check).
F. EM_TEST_FULL only: every other exporter that decodes first-level
   textures from a capture (flame, snow, level zones, props, pickup lights,
   fence door, Roger, opening actors and faces, player.emdl) gets the same
   texels from the rebuilt memory.

Default run ~10 s (4 workers); EM_TEST_FULL=1 runs everything.
"""
from __future__ import annotations

import hashlib
import json
import os
import struct
import sys
from pathlib import Path

os.environ.setdefault('EM_TEST_JOBS', '4')
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from reference_mode import FULL, banner, parallel_map  # noqa: E402
import export_disc_textures_gs as G  # noqa: E402

DECOMP = G.DECOMP
sys.path.insert(0, str(DECOMP / 'tools'))
LANE = os.environ.get('EM_LANE', 'b15/disc_textures')
OUT = ROOT / 'build' / LANE
ROUTE = DECOMP / 'build/s87/route'
REF = DECOMP / 'build/startup-reference'
QUICK_BEATS = ('00_panel_no_battery', '04_elevator_ride', '14_roger_encounter')

# Executed routines and their extents (the decomp's objects).
SIZES = {0x200890: 0xD8, 0x200970: 0x64, 0x1AB7E0: 0x1F0, 0x1AD1A0: 0x90, 0x1FF1E0: 0x210,
         0x1FF830: 0x494, 0x1FF3F0: 0x19C, 0x1FFCD0: 0x688, 0x1FF590: 0x29C}
READ, READ_POLL, POLL, DMA = 0x200780, 0x200700, 0x200730, 0x200830
PLAYER_PACKET, RESTORE = 0x200890, 0x200970


def beats():
    out = sorted(p for p in ROUTE.iterdir() if p.name[:2].isdigit() and int(p.name[:2]) <= 14)
    if len(out) != 15:
        raise SystemExit(f'{ROUTE}: expected the 15 AREA11 route captures 00..14')
    return out


# ======================================================================
# A. The loaders on the original instructions
# ======================================================================

def chain_length(read, a):
    off = 0
    while True:
        w = read(a + off, 4)
        tid, qwc = (int.from_bytes(w, 'little') >> 28) & 7, int.from_bytes(w, 'little') & 0xFFFF
        off += 16 * (qwc + 1)
        if tid != 1:
            return off


class LoaderEE:
    """FallEE over one capture's RAM and scratchpad, with the drive, DMA and
    task leaves hooked and the original's own stores recorded."""

    def __init__(self, elf, ram, spad, iso, extra=None):
        """`iso(position, size)`: the disc image's bytes (the drive)."""
        from test_player_fall_reference import FallEE

        outer = self

        class EE(FallEE):
            def save(self, address, value, size=4):
                outer._store(address, size)
                super().save(address, value, size)

            def write(self, address, data):
                outer._store(address, len(data))
                super().write(address, data)
        self.ee = EE(elf, ram, spad)
        self.iso = iso
        self.events = []
        self.stores = set()
        self.in_hook = False
        hooks = {READ: self.read, READ_POLL: self.ready, POLL: self.ready, DMA: self.dma}
        for address, name in {0x2009E0: 'overlay_relocate', 0x1D19D0: '001D19D0', 0x1CCB10: '001CCB10',
                              0x1FF080: '001FF080', 0x1D2830: '001D2830', 0x119978: '00119978',
                              0x1AB790: '001AB790'}.items():
            hooks[address] = self.recorder(name)
        hooks[0x1FB370] = self.sound_bank
        hooks.update(extra or {})
        for address, hook in hooks.items():
            self.ee.hooks[address] = self.wrap(hook)

    def _store(self, address, size):
        a = address & 0xFFFFFFFF
        if not self.in_hook and not 0x7F000000 <= a < 0x7F100000:
            self.stores.update(range(a, a + size))

    def wrap(self, hook):
        def run(ee):
            self.in_hook = True
            try:
                hook(ee)
            finally:
                self.in_hook = False
        return run

    def recorder(self, name):
        def hook(ee):
            self.events.append((name, ee.arg(0), ee.arg(1)))
            ee.ret_int(0)
        return hook

    def read(self, ee):
        handle, buf, off, size = ee.arg(0), ee.arg(1), ee.arg(2), ee.arg(3)
        lba, fsize = ee.load(handle), ee.load(handle + 4)
        if size == 0xFFFFFFFF:
            size = fsize
        data = self.iso(lba * 0x800 + off, size)
        assert len(data) == size, ('short read', hex(handle), hex(off), hex(size))
        ee.write(buf, data)
        self.events.append(('read', handle, buf, off, size))
        ee.ret_int(0)

    def ready(self, ee):
        ee.ret_int(1)

    def dma(self, ee):
        a = ee.arg(0)
        n = chain_length(ee.read, a)
        self.events.append(('dma', a, ee.read(a, n)))
        ee.ret_int(0)

    def sound_bank(self, ee):
        self.events.append(('001FB370', ee.arg(0), 0))
        ee.ret_int(ee.arg(0))

    def call(self, entry, args=()):
        self.ee.call(entry, args)

    def uploads(self):
        return [e for e in self.events if e[0] == 'dma']


def jal_targets(ee, start, size):
    out = set()
    for pc in range(start, start + size, 4):
        w = ee.load(pc)
        if w >> 26 == 3:
            out.add((w & 0x3FFFFFF) << 2)
    return out


def check_callee_sets(elf):
    from test_player_slide_reference import EE
    ee = EE(elf)
    expect = {
        0x200890: {DMA},
        0x200970: {DMA, 0x1CCB10, PLAYER_PACKET},
        0x1AD1A0: {0x1FF080, DMA, 0x1D19D0},
        0x1FF1E0: {READ, READ_POLL, DMA},
        0x1FF830: {READ, POLL, 0x1FF3F0, DMA, 0x1FB370},
        0x1FF3F0: {READ, POLL, DMA},
        0x1FF590: {READ, POLL, DMA, 0x1FB370},
        0x1FFCD0: {READ, POLL, 0x2009E0, 0x1FF590, DMA, PLAYER_PACKET},
    }
    for fn, callees in expect.items():
        got = jal_targets(ee, fn, SIZES[fn])
        assert got == callees, (hex(fn), sorted(map(hex, got)), sorted(map(hex, callees)))
    # 001AB7E0: steps 3/4 are the ones exercised; the others' callees are hooked too.
    got = jal_targets(ee, 0x1AB7E0, SIZES[0x1AB7E0])
    assert {0x1FF1E0, 0x1ABC60, 0x1ABE10, 0x1D2830} <= got, sorted(map(hex, got))
    return len(expect) + 1


def iso_reader(path):
    """The drive over the user's disc image: bytes at an image position."""
    def read(position, size):
        with open(path, 'rb') as f:
            f.seek(position)
            return f.read(size)
    return read


def load_capture(beat):
    return (beat / 'eeMemory.bin').read_bytes(), (beat / 'scratchpad.bin').read_bytes()


# ----------------------------------------------------------------------
# Synthetic descriptors. The route's own data leave several branches of
# the loaders unexercised (no module with a resident offset, no module or
# area with more than one A or B section, no area with B sections or
# nested blocks). These cases build, from the user's disc at run time, a
# descriptor per loader that exercises every one of them, and run the same
# original instructions and the same model over it. Only the INDEX.IDX
# sector and its DATA.DAT region are replaced (in memory; nothing is
# written); the section contents are the disc's own player texture packets
# (module 3 slots 8..0xC, cut to their chain length), so every upload is a
# chain both the original's DMA and the model's replay accept.
# ----------------------------------------------------------------------

SYN_A = (0x10800, 0x11000)      # every A and B size differs, so an entry-index
SYN_B = (0x11800, 0x12000)      # slip moves a section and the check sees it
SYN_GAP = 0x80000               # DATA.DAT spacing of the synthetic area's regions
SYN_SUB = 1                     # D_00810701: the nested block the area load takes


class PatchedDisc:
    """The user's disc with some INDEX.IDX sectors and DATA.DAT ranges
    replaced. The model reads it through descriptor() / read(), the
    executed original's drive through iso()."""

    def __init__(self, disc, sectors, data):
        self.base, self.image = disc, disc.image
        self.sectors, self.data = dict(sectors), list(data)

    def descriptor(self, sector):
        return bytes(self.sectors[sector]) if sector in self.sectors else self.base.descriptor(sector)

    @staticmethod
    def _overlay(buf, start, at, patch):
        lo, hi = max(start, at), min(start + len(buf), at + len(patch))
        if lo < hi:
            buf[lo - start:hi - start] = patch[lo - at:hi - at]

    def read(self, offset, size):
        buf = bytearray(self.base.read(offset, size))
        for at, patch in self.data:
            self._overlay(buf, offset, at, patch)
        return bytes(buf)

    def iso(self, position, size):
        (path, index_base), (_path, data_base) = self.image.index, self.image.data
        with open(path, 'rb') as f:
            f.seek(position)
            buf = bytearray(f.read(size))
        for sector, raw in self.sectors.items():
            self._overlay(buf, position, index_base + sector * 0x800, raw)
        for at, patch in self.data:
            self._overlay(buf, position, data_base + at, patch)
        return bytes(buf)


def player_packets(disc):
    """Module 3's slots 8..0xC, each cut to its VIF1 chain."""
    block = G.top_block(disc, G.PLAYER_TEXTURE_MODULE)
    index = G.module_table_index(block, 'module')
    out = {}
    for slot in (8, 9, 0xA, 0xB, 0xC):
        _l, off, size = G.module_slot(disc, block, slot, index)
        data = disc.read(block.offset + off, size)
        out[slot] = data[:chain_length(lambda a, n, d=data: d[a:a + n], 0)]
    return out


def pad(data, size):
    assert len(data) <= size, (hex(len(data)), hex(size))
    return data + bytes(size - len(data))


def block_raw(head, offset, size, first, count, b_count, resident, nested, entries, table, length):
    raw = bytearray(length)
    raw[0:4] = head
    struct.pack_into('<2I2H4I', raw, 4, offset, size, first, count, b_count, resident, nested, len(table))
    for k, (o, n) in enumerate(entries):
        struct.pack_into('<2I', raw, 0x20 + 8 * k, o, n)
    at = 0x20 + 8 * len(entries)
    assert at + 4 * len(table) <= length
    for i, (slot, o) in enumerate(table):
        struct.pack_into('<I', raw, at + 4 * i, slot << 24 | o)
    return raw


def synthetic_module(disc, sector, p):
    """A module for 001FF1E0 / 001FF830: hdr[0x0C] = 2 (the module loaders
    ignore it), two A sections at region offsets 0 and SYN_A[0], the
    resident region after them (hdr[0x14] != 0) holding two B sections and
    a tail, the B entries' offset fields pointing past the region (the
    loaders use only their sizes), and a pointer table with slot 0x35 (the
    library packet slot) at resident offset 0."""
    real = disc.descriptor(sector)
    offset = G.u32(real, 4)
    region = (pad(p[9], SYN_A[0]) + pad(p[0xA], SYN_A[1]) + pad(p[0xB], SYN_B[0]) + pad(p[0xC], SYN_B[1])
              + pad(p[8], 0x8800))
    resident = sum(SYN_A)
    entries = [(0, SYN_A[0]), (SYN_A[0], SYN_A[1]), (len(region), SYN_B[0]), (len(region), SYN_B[1])]
    table = [(0x35, 0), (0x60, SYN_B[0]), (0x61, sum(SYN_B)), (0x62, sum(SYN_B) + 0x4000)]
    raw = block_raw(real[:4], offset, len(region), 2, 2, 2, resident, 0, entries, table, 0x800)
    return raw, [(offset, region)]


def synthetic_area(disc, sector, p):
    """An area descriptor for 001FFCD0: the top block with entry 0 (a
    zero sound bank, 001FF590(tag, 0)), A sections 1 and 2, a gap, the
    resident region with two B sections and a tail, and two nested blocks
    (hdr[0x18] = 2) of which D_00810701 = SYN_SUB selects the second; each
    nested block has its own region, sound bank, one A and two B sections
    and pointer table. The two nested blocks differ, so a wrong 0x70 stride
    or index reads the other one."""
    real = disc.descriptor(sector)
    offset = G.u32(real, 4)
    bank = bytes(0x800)
    region = (bank + pad(p[9], SYN_A[0]) + pad(p[0xA], SYN_A[1]) + bytes(0x800)
              + pad(p[0xB], SYN_B[0]) + pad(p[0xC], SYN_B[1]) + pad(p[8], 0x8800))
    resident = 0x800 + sum(SYN_A) + 0x800
    entries = [(0, 0x800), (0x800, SYN_A[0]), (0x800 + SYN_A[0], SYN_A[1]),
               (len(region), SYN_B[0]), (len(region), SYN_B[1])]
    table = [(0x60, 0), (0x61, SYN_B[0]), (0x62, sum(SYN_B))]
    assert len(region) <= SYN_GAP
    raw = bytearray(0x800)
    raw[:0x100] = block_raw(real[:4], offset, len(region), 1, 2, 2, resident, 2, entries, table, 0x100)
    data = [(offset, region)]
    for sub, (a, b0, b1) in enumerate(((0xA, 9, 8), (8, 0xB, 0xC))):
        at = offset + SYN_GAP * (sub + 1)
        nregion = bank + pad(p[a], SYN_A[1]) + pad(p[b0], SYN_B[1]) + pad(p[b1], SYN_A[0])
        nentries = [(0, 0x800), (0x800, SYN_A[1]), (len(nregion), SYN_B[1]), (len(nregion), SYN_A[0])]
        ntable = [(0x63 + 2 * sub, 0), (0x64 + 2 * sub, SYN_B[1] + 0x2000)]
        raw[0x100 + 0x70 * sub:0x170 + 0x70 * sub] = block_raw(
            bytes(4), at, len(nregion), 1, 1, 2, 0x800 + SYN_A[1], 0, nentries, ntable, 0x70)
        data.append((at, nregion))
    return raw, data


def synthetic_disc(disc):
    p = player_packets(disc)
    sectors, data = {}, []
    for sector, build in ((G.LIBRARY_MODULE, synthetic_module), (0x1F, synthetic_module),
                          (G.FIRST_LEVEL_AREA + 4, synthetic_area)):
        raw, regions = build(disc, sector, p)
        sectors[sector] = raw
        data += regions
    return PatchedDisc(disc, sectors, data)


def slot_checks(o, disc, block, loader, base, end):
    """Every pointer entry of `block`: the original's relocated
    D_0028A490[slot] is base + the entry offset; the model's module_slot
    extent runs exactly to the next greater relocated pointer, or to the
    original's end-of-region `end`; its bytes are what the original holds
    there. Returns the table and the check count."""
    s = struct.unpack_from('<256I', o.ee.mem, 0x28A490)
    index = G.module_table_index(block, loader)
    table = block.slot_entries(index)
    pointers = sorted({s[slot] for slot, _ in table})
    for slot, offset in table:
        assert s[slot] == base + offset, (block.label, hex(slot))
        _l, off, size = G.module_slot(disc, block, slot, index)
        nxt = next((q for q in pointers if q > s[slot]), end)
        assert size == nxt - s[slot], (block.label, hex(slot), 'extent', hex(size), hex(nxt - s[slot]))
        assert o.ee.read(s[slot], size) == disc.read(block.offset + off, size), (block.label, hex(slot), 'bytes')
    return table, 3 * len(table)


def uploads_equal(got, want, label):
    assert len(got) == len(want), (label, len(got), len(want))
    for (_k, _a, data), section in zip(got, want):
        assert len(data) <= len(section) and section[:len(data)] == data, (label, 'upload bytes')
    return len(got)


def oracle_case(case):
    """One loader case; returns (kind, arg, count of checks). A kind ending
    in '*' runs over the synthetic descriptors."""
    kind, beat_name, arg = case
    synthetic = kind.endswith('*')
    kind = kind.rstrip('*')
    elf = G.ELF_PATH.read_bytes()
    beat = ROUTE / beat_name
    ram, spad = load_capture(beat)
    disc = G.Disc()
    if synthetic:
        disc = synthetic_disc(disc)
        iso = disc.iso
    else:
        iso = iso_reader(G.ISO_PATH)
    fl = G.FirstLevel(disc, check_extract=not synthetic)
    checks = 0
    slots = lambda ee: struct.unpack_from('<256I', ee.mem, 0x28A490)  # noqa: E731
    if kind == 'packet':
        for mode in (0, 1, 2, 3, 0xFF):
            for costume in (0, 1, 2, 3, 0xFF):
                o = LoaderEE(elf, ram, spad, iso)
                o.ee.save(0x810707, mode, 1)
                o.ee.save(0x810C60, costume, 1)
                o.stores.clear()
                o.call(PLAYER_PACKET)
                slot = G.player_texture_slot(mode, costume)
                assert [(e[0], e[1]) for e in o.events] == [('dma', slots(o.ee)[slot])], (mode, costume, o.events)
                assert not o.stores, ('00200890 stored', sorted(o.stores)[:4])
                checks += 1
    elif kind == 'restore':
        for which in (0, 1):
            for flag in (0, 1, 2):
                o = LoaderEE(elf, ram, spad, iso)
                o.ee.save(0x70003B90, flag, 1)
                o.stores.clear()
                o.call(RESTORE, (which,))
                s = slots(o.ee)
                player = s[G.player_texture_slot(o.ee.load(0x810707, 1), o.ee.load(0x810C60, 1))]
                want = [('dma', s[G.LIBRARY_SLOT])]
                want += [('dma', player)] if which else [('001CCB10', 0)] + ([('dma', player)] if flag == 2 else [])
                assert [(e[0], e[1] if e[0] == 'dma' else 0) for e in o.events] == want, (which, flag, o.events)
                assert not o.stores
                checks += 1
        # the model's page_close is 00200970(1)
        gs = fl.page_close(G.GSImage())
        assert [s[1] for s in gs.steps] == [f'sector {G.LIBRARY_MODULE} slot {G.LIBRARY_SLOT:#x}',
                                             'sector 3 slot 0x8']
        checks += 1
    elif kind == 'boot':
        # 001AB7E0 step 3 (001ABC60 done) and step 4 (001ABE10 done).
        for step, helper, module in ((3, 0x1ABC60, 0x1B), (4, 0x1ABE10, 0x1C)):
            calls = []
            o = LoaderEE(elf, ram, spad, iso, {
                helper: lambda ee: ee.ret_int(1),
                0x1FF1E0: lambda ee: (calls.append(ee.arg(0)), ee.ret_int(0))})
            rec = o.ee.load(0x70003B6C)
            o.ee.save(rec + 8, step, 1)
            o.call(0x1AB7E0)
            assert calls == [module], (step, calls)
            checks += 1
        # 001AD1A0: case 0 requests module 3, case 1 uploads D_0028A564.
        o = LoaderEE(elf, ram, spad, iso)
        rec = o.ee.load(0x70003B6C)
        o.ee.save(rec + 9, 0, 1)
        o.call(0x1AD1A0)
        assert [(e[0], e[1], e[2]) for e in o.events] == [('001FF080', 0, G.PLAYER_TEXTURE_MODULE)], o.events
        o.events.clear()
        o.ee.save(0x275BD8, 0, 1)
        o.call(0x1AD1A0)
        assert [(e[0], e[1]) for e in o.events][:1] == [('dma', slots(o.ee)[G.LIBRARY_SLOT])], o.events
        assert [e[0] for e in o.events] == ['dma', '001D19D0'], o.events
        checks += 2
    elif kind == 'module':
        # 001FF1E0(arg): the boot bank loader.
        o = LoaderEE(elf, ram, spad, iso)
        pre = o.ee.load(0x28A734)
        o.stores.clear()
        o.call(0x1FF1E0, (arg,))
        block = G.top_block(disc, arg)
        want = [disc.read(block.offset + off, size) for _l, off, size in G.module_sections(block)]
        checks += uploads_equal(o.uploads(), want, hex(arg))
        # the region goes to 0xB00000 (id 0) or the cursor D_0028A734 as
        # it was on entry; both cursors then point past its resident part
        base = 0xB00000 if arg == 0 else pre
        post = o.ee.load(0x28A734)
        assert post == o.ee.load(0x28A738) == base + block.size - block.resident, (hex(arg), 'cursors')
        assert o.ee.load(0x275C70) == 0x289BC0, (hex(arg), 'descriptor pointer')
        table, n = slot_checks(o, disc, block, 'module', base, post)
        allowed = set(range(0x275C70, 0x275C74)) | set(range(0x28A734, 0x28A73C))
        allowed |= {a for slot, _ in table for a in range(0x28A490 + 4 * slot, 0x28A494 + 4 * slot)}
        assert o.stores <= allowed, (hex(arg), sorted(map(hex, o.stores - allowed))[:6])
        checks += 3 + n
    elif kind == 'page':
        o = LoaderEE(elf, ram, spad, iso)
        rec = o.ee.load(0x70003B6C)
        for off in range(8, 0x18):
            o.ee.save(rec + off, 0, 1)
        o.stores.clear()
        for _ in range(64):
            if o.ee.load(rec + 8, 1) == 0x63:
                break
            o.call(0x1FF830, (arg,))
        assert o.ee.load(rec + 8, 1) == 0x63, (hex(arg), 'did not finish')
        fl.page(G.GSImage(), arg)
        want = [disc.read(off, size) for _c, _l, off, size, _w in fl.sources]
        checks += uploads_equal(o.uploads(), want, hex(arg))
        block = G.top_block(disc, arg)
        base = o.ee.load(0x275C74)
        end = base + block.size - block.resident
        assert o.ee.load(0x275C70) == 0x289BC0, (hex(arg), 'descriptor pointer')
        if arg in (2, 3):                   # 001FF830 state 5, kind 0: the cursor D_0028A73C
            assert o.ee.load(0x28A73C) == end, (hex(arg), 'cursor')
            checks += 1
        table, n = slot_checks(o, disc, block, 'module', base, end)
        allowed = set(range(rec, rec + 0x20)) | set(range(0x275C70, 0x275C78)) | set(range(0x28A73C, 0x28A74C))
        allowed |= {a for slot, _ in table for a in range(0x28A490 + 4 * slot, 0x28A494 + 4 * slot)}
        assert o.stores <= allowed, (hex(arg), sorted(map(hex, o.stores - allowed))[:6])
        checks += 2 + n
    elif kind == 'area':
        sub = SYN_SUB if synthetic else 0
        o = LoaderEE(elf, ram, spad, iso)
        rec = o.ee.load(0x70003B6C)
        for off in range(8, 0x18):
            o.ee.save(rec + off, 0, 1)
        o.ee.save(0x810700, arg, 1)
        o.ee.save(0x810701, sub, 1)
        pre = o.ee.load(0x28A73C)
        o.stores.clear()
        for _ in range(256):
            if o.ee.load(rec + 8, 1) == 0x63:
                break
            o.call(0x1FFCD0)
        assert o.ee.load(rec + 8, 1) == 0x63, 'area load did not finish'
        fl.area(G.GSImage(), arg, sub, o.ee.load(0x810707, 1), o.ee.load(0x810C60, 1))
        want = [disc.read(off, size) for _c, _l, off, size, _w in fl.sources]
        checks += uploads_equal(o.uploads(), want, 'area')
        sector = arg + 4
        top = G.top_block(disc, sector)
        nested = G.u32(disc.descriptor(sector), 0x18)
        assert nested == (2 if synthetic else 0), nested
        base, top_end = o.ee.load(0x28A73C), o.ee.load(0x28A740)
        assert base == pre, 'area base moved'
        assert top_end == base + top.size - top.resident, 'D_0028A740'
        table, n = slot_checks(o, disc, top, 'area', base, top_end)
        checks += 2 + n
        if nested:
            inner = G.nested_block(disc, sector, sub)
            inner_end = o.ee.load(0x28A744)
            assert inner_end == top_end + inner.size - inner.resident, 'D_0028A744'
            assert o.ee.load(0x28A748) == inner_end, 'D_0028A748'
            assert o.ee.load(0x275C70) == 0x289BC0 + 0x100 + 0x70 * sub, 'nested descriptor pointer'
            assert o.ee.load(0x810704, 1) == sub, 'D_00810704'
            inner_table, m = slot_checks(o, disc, inner, 'area', top_end, inner_end)
            table = table + inner_table
            checks += 4 + m
        else:
            assert o.ee.load(0x28A744) == o.ee.load(0x28A748) == top_end, 'D_0028A744 / D_0028A748'
            assert o.ee.load(0x275C70) == 0x289BC0, 'descriptor pointer'
            assert o.ee.load(0x810701, 1) == o.ee.load(0x810704, 1) == 0, 'D_00810701 / D_00810704'
            checks += 3
        assert o.ee.load(0x810703, 1) == arg, 'D_00810703'
        allowed = set(range(rec, rec + 0x20)) | set(range(0x275C70, 0x275C74)) | set(range(0x28A73C, 0x28A74C))
        allowed |= set(range(0x810701, 0x810705))
        allowed |= {a for slot, _ in table for a in range(0x28A490 + 4 * slot, 0x28A494 + 4 * slot)}
        assert o.stores <= allowed, ('area', sorted(map(hex, o.stores - allowed))[:6])
        checks += 2
    else:
        raise AssertionError(kind)
    return (kind + ('*' if synthetic else ''), arg, checks)


# ======================================================================
# B. The rebuilt GS memory against the captures
# ======================================================================

def gs_blocks_equal(gs, capture_gs):
    import gs_vram
    _base, lm = gs_vram.read_localmem(capture_gs)
    bad = [b for b in sorted(gs.covered) if lm[b * 256:b * 256 + 256] != gs.lm[b * 256:b * 256 + 256]]
    return bad


def check_gs(fl, world):
    captures = [b / 'gs.bin' for b in beats()] + [REF / 'status-hub/gs.bin', REF / 'opening_gs.bin']
    n = 0
    for c in captures:
        bad = gs_blocks_equal(world, c)
        assert not bad, (str(c), len(bad), hex(bad[0]))
        n += 1
    battery = fl.page(world, 0x21)
    assert not gs_blocks_equal(battery, REF / 'panel/gs.bin')
    root = fl.page(battery, 0x1F)
    assert not gs_blocks_equal(root, REF / 'panel/root/gs.bin')
    # The title modules the boot loads before module 0x1B (001AB9D0's 0x28,
    # 001ABC60's 0x29) write only blocks the area load rewrites.
    for module in (0x28, 0x29):
        title = fl.page(G.GSImage(), module)
        assert title.covered <= world.covered - set(range(0x1B80, 0x1C00)) - set(range(0x1D00, 0x2480)), hex(module)
    return n + 2, len(world.covered)


# ======================================================================
# C. / D. Outputs, TEX0 sets
# ======================================================================

ASSET_FILES = {
    'objects': ['scene_snow/object_textures.emot'],
    'page': ['scene_snow/page_textures.emot'],
    'font': ['font.emfn'],
    'status_models': ['status_models/menu_player.emdl', 'status_models/menu_player.empc',
                      'status_models/letter_2f.emdl', 'status_models/letter_40.emdl',
                      'status_models/letter_30.emdl', 'status_models/letter_31.emdl',
                      'status_models/letter_32.emdl', 'status_models/letter_38.emdl',
                      'status_models/models.emsk'],
    'status_hub': ['scene_snow/panel/status_hub_atlas.emha'],
    'item_root': ['scene_snow/panel/item_root.emir'],
    'battery': ['scene_snow/panel/battery.emba'],
}


def emot_entries(data):
    n = struct.unpack_from('<I', data, 8)[0]
    out = {}
    for i in range(n):
        t, w, h, o, _ = struct.unpack_from('<Q4I', data, 16 + 24 * i)
        out[t] = data[o:o + w * h * 4]
    return out


SHARED = {}          # the parent's FirstLevel / world / quick hub list (fork-inherited)


def export_part(part):
    """One exporter part into OUT/assets; returns (part, [(file, sha256)])."""
    import export_disc_textures as X
    assets = OUT / 'assets'
    scratch = OUT / 'scratch'
    fl = SHARED['fl']
    report = X.build(fl.disc, G.EXTRACT, assets, scratch / part, (part,), fl=fl, world=SHARED['world'],
                     hub_token_list=SHARED.get('hub_tokens'), write_freeze=False)
    return (part, sorted((str(Path(p).relative_to(assets)), d) for p, d in report['outputs'].items()),
            report['hub_states'])


def direct_checks(fl, world, hub_list):
    """Disc decodes against capture decodes, independent of assets/."""
    import gs_vram
    import export_object_textures as eot
    from export_ui import decode_token_lm
    out = OUT / 'assets'
    n = 0
    caps = beats() if FULL else [ROUTE / QUICK_BEATS[1]]
    for name in ('scene_snow/object_textures.emot', 'scene_snow/page_textures.emot'):
        mine = emot_entries((out / name).read_bytes())
        for beat in caps:
            _b, lm = gs_vram.read_localmem(beat / 'gs.bin')
            for t, texels in mine.items():
                assert eot.decode(lm, t) == texels, (name, beat.name, hex(t))
                n += 1
    # status pages: their tokens against their captures (the hub, the
    # BATTERY page with module 0x21, the ITEM root with 0x21 then 0x1F)
    import export_disc_textures as X
    elf = G.ELF_PATH.read_bytes()
    battery = fl.page(world, 0x21)
    root_tokens = []
    import export_item_root as eir
    for selection in range(6):
        o = eir.Original(elf, selection)
        o.collect(0x20F170)
        o.collect(0x20F2A0)
        root_tokens += [c['tex0'] for c in o.commands if 'tex0' in c]
    battery_tokens = [struct.unpack('<Q', X.elf_read(elf, 0x265C50 + i * 8, 8))[0] for i in range(16)]
    battery_tokens += [struct.unpack('<Q', X.elf_read(elf, 0x265CD0 + i * 8, 8))[0] for i in range(9)]
    battery_tokens += [0x20042D05A1322000, 0x20043C859D422150]
    for capture, gs, tokens in ((REF / 'status-hub/gs.bin', world, hub_list),
                                (REF / 'panel/gs.bin', battery, battery_tokens),
                                (REF / 'panel/root/gs.bin', fl.page(battery, 0x1F), sorted(set(root_tokens)))):
        _b, lm = gs_vram.read_localmem(capture)
        for token in tokens:
            lo, hi = token & 0xFFFFFFFF, token >> 32
            assert decode_token_lm(lm, lo, hi)[0] == decode_token_lm(bytes(gs.lm), lo, hi)[0], hex(token)
            n += 1
    # font slots against the captured RAM
    block = G.top_block(fl.disc, 0)
    table = dict(block.slot_entries(G.module_table_index(block, 'module')))
    tall = fl.disc.read(block.offset + block.resident + table[0], table[1] - table[0])
    small = fl.disc.read(block.offset + block.resident + table[1], table[2] - table[1])
    for beat in caps:
        ram = (beat / 'eeMemory.bin').read_bytes()
        p0, p1, p2 = struct.unpack_from('<3I', ram, 0x28A490)
        assert (p0, p1, p2) == tuple(0xB00000 + table[k] for k in range(3)), beat.name
        assert ram[p0:p1] == tall and ram[p1:p2] == small, (beat.name, 'font bytes')
        n += 1
    return n


# The capture-derived reference, pinned: the SHA-256 of each file the
# capture exporters wrote (export_object_textures, export_page_textures,
# export_status_models, export_status_hub, export_item_root, export_panel
# over the route / status-hub / panel captures; the decomp's export_font
# over an EE RAM dump), taken before this exporter existed. Pinned so the
# comparison stays independent once the disc exporter writes into assets/.
CAPTURE_SHA256 = {
    # chain C8b FACE and the static-world step: 465 TEX0, the face resources 0x88 / 0x18 and the
    # static bank's 119 MODULATE textures; the capture exporter's output over the 15 route
    # captures, taken before it became disc-first
    # chain step AIMLIVE (2026-10-02): + the shot library models 0x07, 0x08, 0x0B, 0x0D..0x0F
    # and 0x19 (the muzzle node, the shell casing): 469 TEX0; the pin is the disc export's,
    # each of its textures decoding identically from all 15 route captures' GS memory
    # (export_object_textures.py --route-captures); before it: 02827237...
    'scene_snow/object_textures.emot':  # sha256
        '9392501dd56b9ed3f6919a80b9ab62345451a7a788da98fa22b388d48637fcb1',
    # chain step AIMCAM: + the laser dot, CODE_PAGE_TEX0; chain step AIMLIVE: + the impact
    # effects' source TEX0 (SOURCE_PAGE_TEX0), identical in all 15 route captures
    # (export_page_textures.py --route-captures); chain step AIMLIVE fix round: + the
    # ring decals', the lamp flare's, the cable-hit sprite's and the cable strand's
    # (CODE_PAGE_TEX0); chain step DAMAGE: + the bone burst's D_00268480 TEX0
    # (SOURCE_PAGE_TEX0; it decodes identically from the DAMAGE lane's eight
    # in-level end snapshots dmg_00..04, 06..08);
    # before it: b1367533..., 3f229ed9...
    'scene_snow/page_textures.emot':  # sha256
        'ea49260754ac521d82207e29b359013915f06d3e283ad31de75922d5051d3d7e',
    'font.emfn':  # sha256
        '1ce3b7a2e1e2dccbb32ae4ee7fd161bed39d63385aa22ecce3a971166a2536b5',
    'status_models/menu_player.emdl':  # sha256
        '361d7ffa0a2c98c35845b552820d79a004cc80686a6487e6d02661b734b92202',
    'status_models/menu_player.empc':  # sha256
        '2552550808cdb0f26b6d693b8063c2031006f5d49e75bfb3ba1fe4fba272b0e1',
    'status_models/letter_2f.emdl':  # sha256
        'a781feacdb6f8125db95b77c624cd467d69c3c441d9fb2e795d76267327e4863',
    'status_models/letter_40.emdl':  # sha256
        'b6c612875a68759ff45b28c966d4b4e4d68dcd4a6036fb70d99b6af7c5a26c8e',
    'status_models/letter_30.emdl':  # sha256
        'bbdc093c0fd094c45f5f1db8077c38b2a52bf32b1e06c00f8222d06650d0e027',
    'status_models/letter_31.emdl':  # sha256
        '42e040556c865ee184ba3195375294240eaaf9c92b54caa9ce11b86a92047040',
    'status_models/letter_32.emdl':  # sha256
        '630fb29368c176422e0eac4d8881be2a70280537a7585ad7db0647688c71ea9f',
    'status_models/letter_38.emdl':  # sha256
        '37fe0286f7b021c83f6d4eaac3e53476acf68cc5a613b13132d6bbbf0bdde7d7',
    'status_models/models.emsk':  # sha256
        '15063de26d037c6abf77ef7a684bb60d17f9031693c442d42c1e0e2f5f981533',
    'scene_snow/panel/status_hub_atlas.emha':  # sha256
        '687e18c4a5177d7c5946c26194784bb127cab257908d4e07990c8cf4c21f787e',
    'scene_snow/panel/item_root.emir':  # sha256
        '951c903c461d62eb7e1008e58ee9f942bde0cad9194d89330d9d937e9ae5a5ab',
    'scene_snow/panel/battery.emba':  # sha256
        '405e617fb6c9272a6e712d81fb7dc98fffa8b77d613510fbae97442af8ed2b29',
}
# export_status_hub.py's sprite token list over the status-hub capture's RAM
# (the capture-derived status_hub_commands.json, pinned before that exporter
# became disc-first): the five secondary icons, then 00209DF0's tokens.
CAPTURE_HUB_TOKENS = (0x20045EE59D421E40, 0x20045385554221C2, 0x20045305554221A6, 0x200451A5554221A2,
                      0x20045325554221B2, 0x2004512515422288, 0x20045EC555422186, 0x20045EC5554221F0,
                      0x20045EC555422192, 0x20045EC5554221F4, 0x20045505DD421D40, 0x200453A59D421E50,
                      0x2004518555422196)
# export_page_textures.py's captured page TEX0 set (CLD-masked), pinned.
CAPTURE_PAGE_TEX0 = {0x4128555322090, 0x41805113222AE, 0x4290511322469, 0x455E599421ED8,
                     0x457E599421F00, 0x45B0599421EF0,
                     0x41605113222CD}   # D_00255170's (the snow), chain C8b FLAMESNOW
# Page TEX0 the original code builds as an immediate that no captured page
# draws (the route never aims; the AIM captures' end snapshots are idle):
# the laser dot 001854E0 / 00185760 pass to 001CD520 (the aim/fire target
# oracle executes it), the ring decals, the lamp flare, the cable-hit sprite and
# the cable strand. Its texels are checked against every compared
# capture's GS memory by direct_checks, and the page file without its entry
# must still be the capture-derived file (CAPTURE_PAGE_FILE_SHA256).
CODE_PAGE_TEX0 = {0x45BA5154222DC,
                  # 001F0460's three ring-decal tags (the shots' impact marks;
                  # test-effect-original-reference executes it)
                  0x40F8555322078, 0x418851532218C, 0x4108555322080,
                  # 00187780's two flare words (stored by 00187690 at the +0x70
                  # TEX0 row of D_002487E0; test-aim-fire-lamp-reference)
                  0x45D05554221F6, 0x48D0599422050,
                  # 001EAB50's sprite word (the cable hit's effect 0x80000045;
                  # test-effect-kinds-reference)
                  0x45B2599421E98,
                  # 0021A500's strip word (the parted strand; tools/test_security_gun_rest_reference.py)
                  0x45D8555422188}
# Page TEX0 of a 001CFBE0 source block no captured page draws (chain step
# AIMLIVE): 001EBA20's D_002560D0 (the impact effect 0x8000002C; its other
# block D_00256160 and 001EACF0's D_00255620 carry TEX0 already in the
# captured set). Like the dot: checked against the captures' GS memory, and
# the page file without it must still be the capture-derived file. Chain
# step DAMAGE: 0022BBC0's burst-0 block D_00268480 (the flame contact's
# effect 0x80000027; its pair D_00268510 carries a captured TEX0).
SOURCE_PAGE_TEX0 = {0x4556599421EC8, 0x4298599321E80}
CAPTURE_PAGE_FILE_SHA256 = '4e5416c0dbde25c7db55d5bc1154cfdbad6b681aeb4fe266380e151a227a4927'
# The buffers the model reads for the world, the two page states and the
# font (caller, source, DATA.DAT offset, size, extract span), pinned from
# the lane's first disc run; the extract spans are the user's extract file names.
PINNED_SOURCES = [
    ('001AB7E0 step 3: 001FF1E0(0x1B)', 'sector 27 B0', 0xDA78000, 0x78800, 'chunk27/f00_id35.bin'),
    ('001AD1A0: 00200830(D_0028A564)', 'sector 27 slot 0x35', 0xDA78000, 0x78800, 'chunk27/f00_id35.bin'),
    ('001FFCD0 state 4: 001FF590(tag, 1)', 'sector 15 A1', 0x7732000, 0xD8800,
     'chunk15/f00_id43.bin[0x4a800:0x85800] + chunk15/f01_id42.bin + chunk15/f02_id46.bin + '
     'chunk15/f03_id41.bin + chunk15/f04_id96.bin + chunk15/f05_id97.bin[0x0:0x5000]'),
    ('001FFCD0 state 7: 00200890', 'sector 3 slot 0x8', 0x214800, 0x8800, 'chunk03/f00_id08.bin'),
    ('001FF830(0x21) 001FF3F0', 'sector 33 A0', 0xE4BC000, 0x50800, 'chunk33/f00_id00.bin'),
    ('001FF830(0x1f) 001FF3F0', 'sector 31 A0', 0xE45B000, 0x18800, 'chunk31/f00_id00.bin'),
    ('001FF1E0(0)', 'sector 0 slots 0..1', 0x0, 0x5000, 'chunk00/f00_id00.bin + chunk00/f01_id01.bin'),
]
PINNED_WORLD_CALLERS = [s[0] for s in PINNED_SOURCES[:4]]


def check_page_set(elf):
    """The page TEX0 set from original sources equals the captured set
    (pinned); page_textures.json is compared too while it is still the
    capture exporter's (it has a 'captures' key)."""
    import export_disc_textures as X
    overlay = (DECOMP / 'extract/OVERLAY/AREA11.BIN').read_bytes()
    mine = set(X.page_tex0(elf, overlay))
    extra = CODE_PAGE_TEX0 | SOURCE_PAGE_TEX0
    assert mine == CAPTURE_PAGE_TEX0 | extra, sorted(map(hex, mine ^ (CAPTURE_PAGE_TEX0 | extra)))
    # the produced page file without the code-only entries is the
    # capture-derived file, byte for byte
    import export_object_textures as eot
    entries = emot_entries((OUT / 'assets/scene_snow/page_textures.emot').read_bytes())
    captured = {t: v for t, v in entries.items() if t not in extra}
    assert hashlib.sha256(eot.emot(captured)).hexdigest() == CAPTURE_PAGE_FILE_SHA256, 'page file minus the dot'
    report = ROOT / 'assets/scene_snow/page_textures.json'
    if report.exists():
        data = json.loads(report.read_text())
        if 'captures' in data:
            assert {int(t['tex0'], 16) for t in data['textures']} == mine - extra
    return len(mine)


def check_sources():
    """A fresh model: its buffers for the world, BATTERY then ITEM root, and
    the font equal the pinned table; the world's steps are the four route
    uploads in order (001AD1A0's re-upload of slot 0x35 included)."""
    import export_disc_textures as X
    fl = G.FirstLevel(G.Disc())
    world = fl.world()
    assert [c for c, _l, _log in world.steps] == PINNED_WORLD_CALLERS, [c for c, _l, _ in world.steps]
    battery = fl.page(world, 0x21)
    fl.page(battery, 0x1F)
    X.font_bytes(fl.disc, fl)
    got = [(c, lab, o, n, w) for c, lab, o, n, w in fl.sources]
    assert got == PINNED_SOURCES, [g for g in got if g not in PINNED_SOURCES]
    return len(got)


def emha_tokens(path: Path) -> list:
    """The sprite tokens of a status_hub_atlas.emha (without the white
    helper entry)."""
    data = path.read_bytes()
    count, white = struct.unpack_from('<2I', data, 16)
    tokens = [struct.unpack_from('<Q', data, 28 + 24 * i)[0] for i in range(count)]
    assert tokens[white] == 0
    return tokens[:white] + tokens[white + 1:]


def capture_hub_tokens(_=None):
    """export_status_hub.py's token list over the status-hub capture's RAM
    (full mode; quick mode reads the list that exporter wrote)."""
    import export_status_hub as H
    elf = G.ELF_PATH.read_bytes()
    ram = (REF / 'status-hub/eeMemory.bin').read_bytes()
    layouts = []
    for hover in range(5):
        for infection in (0, 100):
            o = H.Original(elf, ram, hover, infection)
            o.run(0x209DF0, (H.UI,))
            layouts.append(o.commands)
    fx = H.Original(elf, ram, ram[H.UI + 0x11], H.number(struct.unpack_from('<I', ram, 0x81085C)[0]), True)
    fx.run(0x209DF0, (H.UI,))
    return list(dict.fromkeys([0x20045EE59D421E40, 0x20045385554221C2, 0x20045305554221A6,
                               0x200451A5554221A2, 0x20045325554221B2] +
                              [c['tex0'] for layout in layouts + [fx.commands] for c in layout
                               if 'tex0' in c]))


# ======================================================================
# E. Mutation controls
# ======================================================================

def mutations(fl, world):
    import gs_vram
    import export_object_textures as eot
    import export_disc_textures as X
    caught = []
    _b, cap = gs_vram.read_localmem(ROUTE / QUICK_BEATS[1] / 'gs.bin')
    player_tex = [0x37C15DD421B80, 0x37CF5DD421BA0, 0x37E559D421BC0]
    # 1. the player texture of costume 1 (slot 0xB) instead of slot 8
    gs = G.GSImage()
    fl.library(gs)
    fl.library_slot(gs, 'mutant')
    fl.area(gs, 11, 0, 0, 1)
    caught.append(('costume slot', any(eot.decode(bytes(gs.lm), t) != eot.decode(cap, t) for t in player_tex)))
    # 2. the area's sound-bank entry 0 uploaded in place of its A section
    top = G.top_block(fl.disc, 15)
    try:
        off, size = top.entry(0)
        m = world.copy()
        m.upload('mutant', 'entry 0', fl.disc.read(top.offset + off, size))
        caught.append(('area entry 0', bool(gs_blocks_equal(m, ROUTE / QUICK_BEATS[1] / 'gs.bin'))))
    except SystemExit:
        caught.append(('area entry 0', True))       # the replay refuses it
    # 3. no library upload
    gs = G.GSImage()
    fl.area(gs, 11, 0, 0, 0)
    gs.covered |= world.covered
    caught.append(('no library', bool(gs_blocks_equal(gs, ROUTE / QUICK_BEATS[1] / 'gs.bin'))))
    # 4. the ITEM root page state built with module 0x1E instead of 0x1F
    m = fl.page(fl.page(world, 0x21), 0x1E)
    caught.append(('page module', bool(gs_blocks_equal(m, REF / 'panel/root/gs.bin'))))
    # 5. swapped font slots
    good = X.font_bytes(fl.disc, fl)
    orig = G.Block.slot_entries

    def swapped(self, table_index):
        e = orig(self, table_index)
        return [(1, o) if s == 0 else (0, o) if s == 1 else (s, o) for s, o in e]
    G.Block.slot_entries = swapped
    try:
        try:
            bad = X.font_bytes(fl.disc, fl)
            caught.append(('font slots', bad != good))
        except SystemExit:
            caught.append(('font slots', True))
    finally:
        G.Block.slot_entries = orig
    # 6-8. The residency check (reads_only_covered) must refuse a decode
    # that reads a block no replay wrote, whatever that block holds: its
    # content is set to 0x00, to 0xA5 and to the captured texels in turn
    # (each fill value alone would pass one of the two probes).
    t = player_tex[0]
    decode = (lambda lm, t=t: eot.decode(lm, t))  # noqa: E731
    assert G.reads_only_covered(world, decode), 'resident texture refused'
    tbp = t & 0x3FFF                      # TBP0: the texture's first GS block
    for label, fill in (('uncovered 0x00', b'\x00' * 256), ('uncovered 0xA5', b'\xA5' * 256),
                        ('uncovered texels', None)):
        m = world.copy()
        m.covered.discard(tbp)
        if fill is not None:
            m.lm[tbp * 256:tbp * 256 + 256] = fill
        caught.append((label, not G.reads_only_covered(m, decode)))
    # 9-10. The extract comparison must refuse a buffer that differs from
    # the extract by one byte, and a span past the chunk's files.
    block = G.top_block(fl.disc, G.PLAYER_TEXTURE_MODULE)
    _l, off, size = G.module_slot(fl.disc, block, 8, G.module_table_index(block, 'module'))
    data = bytearray(fl.disc.read(block.offset + off, size))
    fl._extract_span('chunk03', off, size, bytes(data))
    data[size // 2] ^= 1
    for label, args in (('extract byte', ('chunk03', off, size, bytes(data))),
                        ('extract span', ('chunk03', 0x7FFF0000, 0x100, bytes(0x100)))):
        try:
            fl._extract_span(*args)
            caught.append((label, False))
        except SystemExit:
            caught.append((label, True))
    missed = [name for name, ok in caught if not ok]
    assert not missed, ('mutants not caught', missed)
    return len(caught)


# ======================================================================
# F. Other capture-textured exporters (full only)
# ======================================================================

def other_exporters(world):
    import export_native as native
    import export_object_textures as eot
    import gs_vram
    freeze = OUT / 'scratch/first_level_gs.bin'
    freeze.parent.mkdir(parents=True, exist_ok=True)
    freeze.write_bytes(world.freeze())
    opening = REF / 'opening_gs.bin'
    n = 0

    def same(textures, label):
        nonlocal n
        a = native.build_texture_blob(None, textures, p2s=opening)
        b = native.build_texture_blob(None, textures, p2s=freeze)
        assert a == b, label
        for t in textures:
            assert G.reads_only_covered(world, lambda lm, k=t['key']: eot.decode(lm, k)), (label, hex(t['key']))
        n += len(textures)
    elf = G.ELF_PATH.read_bytes()
    overlay = (DECOMP / 'extract/OVERLAY/AREA11.BIN').read_bytes()
    for label, key in (('flame', struct.unpack_from('<Q', overlay, 0x4E40 + 112)[0]),
                       ('snow', struct.unpack_from('<Q', elf, 0x255170 - 0x100000 + 0x300 + 112)[0])):
        key &= native.TEX0_KEY_MASK
        t = native.tex0_fields(key)
        t['key'] = key
        same([t], label)
    _b, cap = gs_vram.read_localmem(opening)
    for z in sorted((ROOT / 'assets/scene_snow').glob('*.gsmat.json')):
        for t in json.loads(z.read_text())['textures']:
            k = int(t['tex0'], 16)
            assert eot.decode(bytes(world.lm), k) == eot.decode(cap, k), (z.name, hex(k))
            assert G.reads_only_covered(world, lambda lm, k=k: eot.decode(lm, k)), (z.name, hex(k))
            n += 1
    import export_pickup_lights as epl
    import export_area11_props as eap
    import export_door_original as edo
    from export_opening_actors import exact_mesh_sections
    props = epl.load_tool(DECOMP / 'tools/export_props.py', '_disc_textures_props')
    area = (DECOMP / 'extract/chunk15/f05_id97.bin').read_bytes() + (DECOMP / 'extract/chunk15/f06_id98.bin').read_bytes()
    lib = (DECOMP / 'extract/chunk27/f01_id37.bin').read_bytes()
    for data, table, model, label in ((area, 0x5000, 4, 'area_item_04'), (area, 0x5000, 15, 'area_elevator'),
                                      (area, 0x5000, 16, 'area_indicator_10'), (lib, 0, 0x75, 'item_75')):
        same(eap.exact_static_mesh(props, data, props.table_entry_offset(data, table, model))[1], label)
    same(epl.owner_rest_mesh(props, lib)[1], 'item_72 body')
    same(props.build_blob_mesh(lib, props.table_entry_offset(lib, 0, 0x73))[1], 'item_73')
    area3 = b''.join((DECOMP / 'extract/chunk15' / f).read_bytes() for f in ('f05_id97.bin', 'f06_id98.bin', 'f07_id52.bin'))
    same(edo.mesh_sections(props, area3, props.table_entry_offset(area3, 0x5000, 0x14))[1], 'door_original')
    src = (DECOMP / 'extract/chunk15/f18_id94.bin').read_bytes()
    size = struct.unpack_from('<I', src, 0x35000 + 12)[0]
    same(exact_mesh_sections(src[0x35000:0x35000 + size])[1], 'roger')
    same(native.load_mesh_sections(DECOMP / 'extract/chunk28/f00_id3b.bin')[2], 'player 0x3b')
    # player.emdl (STARTUP step 6, exported from a GS dump): every texture of
    # the file equals the export_native decode of one resident object TEX0
    emdl = (ROOT / 'assets/player.emdl').read_bytes()
    nb, _v, _i, _f, _fps, nt = struct.unpack_from('<4If I', emdl, 4)[:6]
    at = 4 + 32 + 4 * nb
    entries = [struct.unpack_from('<4I', emdl, at + 16 * i) for i in range(nt)]
    blob = emdl[len(emdl) - sum(4 * w * h for w, h, _o, _z in entries):]
    keys = []
    for t in emot_entries((OUT / 'assets/scene_snow/object_textures.emot').read_bytes()):
        f = native.tex0_fields(t)
        f['key'] = t
        keys.append(f)
    ents, texels = native.build_texture_blob(None, keys, p2s=freeze)
    pool = {(e['w'], e['h'], texels[e['off']:e['off'] + 4 * e['w'] * e['h']]) for e in ents}
    for w, h, o, _z in entries:
        assert (w, h, blob[o:o + 4 * w * h]) in pool, ('player.emdl texture', o)
        n += 1
    # the opening actors and faces: the decomp exporters rerun on the
    # rebuilt memory (their own --gs), every output byte-identical
    import subprocess
    for tool in ('export_opening_actors.py', 'export_opening_faces.py'):
        target = OUT / 'opening' / tool[:-3]
        target.mkdir(parents=True, exist_ok=True)
        subprocess.run([sys.executable, str(DECOMP / 'tools' / tool), '--gs', str(freeze),
                        '--reference-ee', str(REF / 'opening_ee.bin'), '--out', str(target),
                        '--report', str(target / 'report.json')], check=True, cwd=DECOMP,
                       stdout=subprocess.DEVNULL)
        for f in sorted(target.glob('*.em*')):
            assert f.read_bytes() == (ROOT / 'assets/scene_snow/opening' / f.name).read_bytes(), f.name
            n += 1
    return n


# ======================================================================

def main() -> int:
    if not G.ISO_PATH.exists():
        raise SystemExit(f'{G.ISO_PATH}: the disc image is required (the loaders read it)')
    elf = G.ELF_PATH.read_bytes()
    if hashlib.sha256(elf).hexdigest() != G.ELF_SHA256:
        raise SystemExit('wrong original executable')
    OUT.mkdir(parents=True, exist_ok=True)
    callee_sets = check_callee_sets(elf)
    beat = QUICK_BEATS[1]
    cases = [('packet', beat, 0), ('restore', beat, 0), ('boot', beat, 0),
             ('module', beat, 0x1B), ('module', beat, 0x1C), ('module', beat, 0),
             ('page', beat, 3), ('page', beat, 0x1F), ('page', beat, 0x21), ('area', beat, 11),
             ('module*', beat, 0x1B), ('page*', beat, 0x1F), ('area*', beat, 11)]
    if FULL:
        cases += [('page', beat, m) for m in [*range(0x1E, 0x32), 0x37] if m not in (0x1F, 0x21)]
        cases += [('module*', QUICK_BEATS[0], 0x1B), ('area*', QUICK_BEATS[2], 11)]
        for other in (QUICK_BEATS[0], QUICK_BEATS[2]):
            cases += [('area', other, 11), ('module', other, 0x1B), ('packet', other, 0)]
    parts = list(ASSET_FILES)
    fl = G.FirstLevel(G.Disc())
    world = fl.world()
    SHARED.update(fl=fl, world=world)
    capture_list = list(CAPTURE_HUB_TOKENS)
    if not FULL:
        SHARED['hub_tokens'] = capture_list
    work = [('oracle', c) for c in cases] + [('part', p) for p in parts] + [('sources', None)]
    work += [('hub', None)] if FULL else []
    cost = {'hub': 5, 'part': 2, 'oracle': 1, 'sources': 2}
    results = parallel_map(run_item, work, cost=lambda w: cost[w[0]] + (5 if w[1] == 'status_hub' else 0))
    oracle_checks = sum(r[2] for kind, r in results if kind == 'oracle')
    exported = [r for kind, r in results if kind == 'part']
    sources = [r for kind, r in results if kind == 'sources'][0]
    hub_states = [states for part, _o, states in exported if part == 'status_hub'][0]
    if FULL:
        # the exporter executed 00209DF0 for every state export_status_hub.py
        # does: hovers 0..4 x infection 0 / 100, then the fixture pass
        assert hub_states == [[h, i] for h in range(5) for i in (0, 100)] + ['fixture'], hub_states
    else:
        assert hub_states == [], hub_states         # quick: the capture list was passed
    disc_list = emha_tokens(OUT / 'assets/scene_snow/panel/status_hub_atlas.emha')
    assert disc_list == capture_list, ('status-hub tokens', [hex(t) for t in disc_list], [hex(t) for t in capture_list])
    if FULL:
        # the list over the ELF image (the exporter's) equals the list over
        # the status-hub capture's RAM, re-executed
        assert [r for kind, r in results if kind == 'hub'][0] == capture_list
    # C. files against the capture-derived files (pinned SHA-256)
    files = 0
    for part, outputs, _states in exported:
        names = {name for name, _ in outputs}
        assert names == set(ASSET_FILES[part]), (part, sorted(names))
        for name, digest in outputs:
            assert CAPTURE_SHA256[name] == digest, ('differs from the capture-derived file', name)
            files += 1
    assert files == len(CAPTURE_SHA256)
    gs_captures, blocks = check_gs(fl, world)
    direct = direct_checks(fl, world, disc_list)
    page_set = check_page_set(elf)
    mutants = mutations(fl, world)
    others = other_exporters(world) if FULL else 0
    counts = {'loader_cases': len(cases), 'loader_checks': oracle_checks, 'callee_sets': callee_sets,
              'gs_captures': gs_captures, 'gs_blocks': blocks, 'files_identical': files,
              'direct_decodes': direct, 'page_tex0': page_set, 'hub_tokens': len(disc_list),
              'mutants_caught': mutants, 'other_exporter_textures': others, 'pinned_sources': sources}
    (OUT / 'counts.json').write_text(json.dumps(counts, indent=1) + '\n')
    banner(f'{len(cases)} loader cases ({oracle_checks} checks, {callee_sets} callee sets)',
           f'{gs_captures} captures x {blocks} GS blocks', f'{files} files byte-identical',
           f'{direct} direct decodes', f'{sources} pinned buffers',
           f'{page_set} page TEX0 + {len(disc_list)} hub tokens',
           f'{mutants} mutants caught', f'{others} other-exporter textures' if FULL else 'other exporters in full mode')
    print('PASS test_disc_textures_reference')
    return 0


def run_item(item):
    """One work item. A SystemExit (the exporter's refusals) is re-raised as
    an ordinary exception: raised as is inside a forked pool worker it
    would end the worker without a result and leave pool.map waiting."""
    kind, arg = item
    try:
        if kind == 'oracle':
            return kind, oracle_case(arg)
        if kind == 'part':
            return kind, export_part(arg)
        if kind == 'sources':
            return kind, check_sources()
        return kind, capture_hub_tokens()
    except SystemExit as e:
        raise RuntimeError(f'{item}: SystemExit: {e}') from None


if __name__ == '__main__':
    sys.exit(main())
