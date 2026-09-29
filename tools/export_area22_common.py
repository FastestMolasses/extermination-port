#!/usr/bin/env python3
"""Shared inputs of the AREA22 asset exporters (docs/AREA22_ASSETS.md).

AREA22 reuses the AREA01 exporters' machinery by import, unchanged, the way
the AREA00, AREA02 and AREA04 exporters do: export_area01_common (the load
map, LoadedImage, compare_load_map, the EMSC writer) and, through it,
everything the AREA01 level / table / sound exporters and the AREA01 checker
compute. Those modules read their area constants as attributes of
export_area01_common at call time, so `configure()` points them at AREA22
(area 0x16, sub 0, OVERLAY/AREA22.BIN = MWo3 id 0x13 at 0x823500) and at the
AREA22 captures; nothing in those files is edited.

What is AREA22's own here: its level block has NO nested sub-block.
INDEX.IDX sector 26 (D_00810700 + 4) is a single descriptor whose nested
count (+0x18) is 0 and which, unlike the top descriptors of AREA01, AREA00,
AREA02 and AREA04, carries upload sections and a resident offset itself
(+0x0C/+0x0E: one group-A section after the first entry; +0x14 = the
resident offset). export_area01_common.build_load_map refuses such a top
descriptor ("not modelled"), so `build_load_map_flat` models it by the
loader's own rule, 001FFCD0 state 5 (NEARMISS C, src/func_001FFCD0.c): the
first block's bytes from descriptor +0x14 to +8 (the block size) stream to
the first cursor D_0028A73C, and D_0028A740 = D_0028A73C + that length.
`configure()` installs it as export_area01_common.build_load_map, so every
reused function that builds a load map gets the flat one. A process must
not mix AREA22 work with another area's.

Nothing here is original data: every byte is read from the user's own files
(the pinned boot ELF, the AREA22 overlay and the extracted chunk26 files
under ../Extermination/extract/, the disc image for the descriptor and the
GS upload replay), and every check compares them with the recorded AREA22
captures (SIXTH_LEVEL_ROUTE.md):
  * the AREA22 arrival: the end of beat a04_05
    (../Extermination/build/s87/route_a04/a04_05_progression_exit/, area
    bytes 16 00 00, overlay id 0x13 resident);
  * the a22 beats a22_00, a22_01 and the side beats a22_s0 .. a22_s2
    (build/s87/route_a22/); a22_02 ends in AREA01 and a22_s3 in AREA04, and
    both are excluded by their area byte.
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area01_common as C  # noqa: E402

ROOT, DECOMP = C.ROOT, C.DECOMP
OVERLAY_PATH = C.EXTRACT / 'OVERLAY/AREA22.BIN'
OVERLAY_SIZE, OVERLAY_ID = 0x900, 0x13         # 2,304 bytes, MWo3 id 0x13 at 0x823500
AREA = 0x16
SUBS = (0,)                                    # the descriptor has no nested block
OUT = ROOT / 'assets/area22'
SCRATCH = ROOT / 'build/area22/assets'
ROUTE_A04 = DECOMP / 'build/s87/route_a04'
ROUTE_A22 = DECOMP / 'build/s87/route_a22'
ARRIVAL = ROUTE_A04 / 'a04_05_progression_exit'
CHUNK = 'chunk26'

# SHA-256 of the user's extract/OVERLAY/AREA22.BIN (a hash, not disc data):
# the overlay reference is as fixed as the pinned ELF
OVERLAY_SHA256 = 'd78f9681364dac52408795feb67db6f13ee160ad5a31def04b938fbacf273038'

_NESTED_BUILD_LOAD_MAP = C.build_load_map


def read_overlay():
    ov = C.read_overlay()
    if C.sha(ov) != OVERLAY_SHA256:
        raise SystemExit(f'{OVERLAY_PATH}: not the pinned AREA22 overlay')
    return ov


def sub_out(sub=0):
    return OUT / f'sub{sub}'


def descriptor(capture, check_iso=True):
    """(descriptor, rambuf note): INDEX.IDX sector D_00810700 + 4 from the disc
    image, checked equal to the capture's D_00289BC0 buffer whenever that
    buffer still holds it (a later read in a22_s0 leaves another sector
    there); without a disc image, the buffer."""
    ram = capture.ram
    area = ram[C.D_00810700]
    rtop = ram[C.D_00289BC0:C.D_00289BC0 + C.DESCRIPTOR_SIZE]
    disc = C.iso_descriptor(area + 4) if check_iso else None
    if disc is None:
        return rtop, 'used (no disc image)'
    top = disc[:C.DESCRIPTOR_SIZE]
    if C.u32(rtop, 0) == area + 4:
        if rtop != top:
            raise SystemExit(f'{capture.name}: RAM descriptor differs from INDEX.IDX sector {area + 4}')
        return top, 'equal to the disc sector'
    return top, f'reused (sector word {C.u32(rtop, 0)})'


def build_load_map_flat(capture, check_iso=True):
    """[(address, path, file offset, size, label)] of the AREA22 data the
    loader left resident, by 001FFCD0 state 5: the block's bytes from the
    descriptor's resident offset (+0x14) to its size (+8) lie at the first
    cursor D_0028A73C. The descriptor must have no nested block (+0x18 = 0)
    and no group-B section (+0x10 = 0); its group-A sections (+0x0C first,
    +0x0E count) are uploads (the sound container and the GS texel upload),
    not resident. The second cursor must be D_0028A73C + the resident length
    (state 5 sets it so).

    Labels. The descriptor's file list (+0x1C entries after the section
    table, id << 24 | offset) is the list 001FFCD0 state 7 relocates as
    D_0028A490[id] = D_0028A73C + offset, so its offsets are RESIDENT-
    relative: id i spans resident offsets [offset_i, offset_i+1) (the last
    one up to the resident length), i.e. block offsets +0x14 later. Every
    row is labelled `chunk26/idXX` by that reading (relocation_problems
    checks the seven words in every capture). The extracted files
    chunk26/fII_idXX.bin are only the byte source: the decomp's
    extract_data.py cut them at the same offsets read as BLOCK offsets, so
    their names are shifted by 0xD9800 and a row names the extracted file
    and offset its bytes come from, not the id they belong to. The extracted
    files are contiguous slices of the block, so the bytes are unaffected."""
    ram = capture.ram
    top, rambuf = descriptor(capture, check_iso)
    area = ram[C.D_00810700]
    sector = C.u32(top, 0)
    if sector != area + 4:
        raise SystemExit(f'{capture.name}: descriptor sector word {sector}, not {area + 4}')
    if C.u32(top, 0x18) != 0:
        raise SystemExit(f'{capture.name}: the descriptor has nested blocks; not the flat AREA22 layout')
    if ram[C.D_00810700 + 1] != 0:
        raise SystemExit(f'{capture.name}: D_00810701 = {ram[C.D_00810700 + 1]} with no nested block')
    if C.u32(top, 0x10) != 0:
        raise SystemExit(f'{capture.name}: group-B sections in the descriptor; not modelled')
    chunk = f'chunk{sector:02d}'
    first, count = C.u16(top, 0x0C), C.u16(top, 0x0E)
    resident, size = C.u32(top, 0x14), C.u32(top, 8)
    entries = C._files(top, 0x20 + 8 * (first + count), C.u32(top, 0x1C))
    base = C.u32(ram, C.D_0028A73C)
    if C.u32(ram, C.D_0028A740) != base + size - resident:
        raise SystemExit(f'{capture.name}: D_0028A740 {C.u32(ram, C.D_0028A740):#x} != D_0028A73C + '
                         f'{size - resident:#x}')
    length = size - resident
    offs = [o for _i, o in entries]
    if not entries or offs[0] != 0 or offs != sorted(set(offs)) or offs[-1] >= length:
        raise SystemExit(f'{capture.name}: the relocation list does not tile the resident region')
    sources = C._extract_files(chunk, entries, size)      # byte sources (block-offset cuts)
    out, ids = [], []
    for i, (ident, off) in enumerate(entries):
        end = offs[i + 1] if i + 1 < len(offs) else length
        ids.append((ident, base + off, end - off))
        lo, hi = resident + off, resident + end            # block offsets of id i
        for path, foff, span in sources:
            a, b = max(lo, foff), min(hi, foff + span)
            if a < b:
                out.append((base + a - resident, path, a - foff, b - a, f'{chunk}/id{ident:02x}'))
    return out, dict(rambuf=rambuf, chunk=chunk, nested=None, resident=resident, top_cursor=base,
                     nested_cursor=C.u32(ram, C.D_0028A740),
                     ids=[(f'id{i:02x}', a, n) for i, a, n in ids],
                     sections=[(C.u32(top, 0x20 + 8 * k), C.u32(top, 0x24 + 8 * k)) for k in range(first + count)],
                     upload_sections=[(C.u32(top, 0x20 + 8 * k), C.u32(top, 0x24 + 8 * k))
                                      for k in range(first, first + count)])


def top_files():
    """(chunk, [(path, block offset, span)], [(offset, size)] of every
    section entry) of the AREA22 descriptor, read from the disc image. The
    (path, block offset, span) triples are the extracted files as the
    decomp's extract_data.py cut them (the list offsets read as block
    offsets): contiguous byte sources of the block, NOT the ids' extents
    (build_load_map_flat)."""
    top = C.iso_descriptor(AREA + 4)
    if top is None:
        raise SystemExit('the disc image is needed for the AREA22 descriptor')
    if C.u32(top, 0) != AREA + 4 or C.u32(top, 0x18) != 0:
        raise SystemExit('INDEX.IDX sector 26 is not the flat AREA22 descriptor')
    first, count = C.u16(top, 0x0C), C.u16(top, 0x0E)
    files = C._files(top, 0x20 + 8 * (first + count), C.u32(top, 0x1C))
    sections = [(C.u32(top, 0x20 + 8 * k), C.u32(top, 0x24 + 8 * k)) for k in range(first + count)]
    return CHUNK, C._extract_files(CHUNK, files, C.u32(top, 8)), sections


def configure(sub=0):
    """Point export_area01_common (and so every AREA01 exporter and checker
    function that reads it) at AREA22. Returns the module."""
    if sub != 0:
        raise SystemExit('AREA22 has one sub-state (no nested block)')
    C.OVERLAY_PATH = OVERLAY_PATH
    C.OVERLAY_SIZE, C.OVERLAY_ID = OVERLAY_SIZE, OVERLAY_ID
    C.AREA, C.SUB = AREA, 0
    C.OUT = sub_out(0)
    C.SCRATCH = SCRATCH / 'sub0'
    C.ROUTE_A01 = ROUTE_A22
    C.ARRIVAL = ARRIVAL
    C.captures = lambda limit=None: captures(limit)
    C.build_load_map = build_load_map_flat
    return C


def capture_paths():
    """The arrival first, then the a22 beats in order."""
    return [ARRIVAL] + sorted(p for p in ROUTE_A22.glob('a22_*') if (p / 'eeMemory.bin').exists())


def all_captures():
    """[Capture] of every AREA22 capture: area byte 0x16 with the AREA22
    overlay (MWo3 id 0x13) resident. Every one must be sub 0."""
    out = []
    for p in capture_paths():
        if not (p / 'eeMemory.bin').exists():
            continue
        c = C.Capture(p)
        if c.area[0] != AREA:
            continue
        if c.ram[C.OVERLAY_ARENA:C.OVERLAY_ARENA + 4] != b'MWo3' or C.u32(c.ram, C.OVERLAY_ARENA + 4) != OVERLAY_ID:
            raise SystemExit(f'{p}: area 0x16 without the AREA22 overlay resident')
        if c.area[1] != 0:
            raise SystemExit(f'{p}: AREA22 capture with D_00810701 = {c.area[1]}')
        out.append(c)
    return out


def excluded_captures():
    """[(name, area bytes)] of the route_a22 captures that end in another area."""
    out = []
    for p in capture_paths():
        if (p / 'eeMemory.bin').exists():
            ram = (p / 'eeMemory.bin').read_bytes()
            if ram[C.D_00810700] != AREA:
                out.append((p.name, tuple(ram[C.D_00810700:C.D_00810700 + 3])))
    return out


def captures(limit=None):
    out = all_captures()
    if limit:
        out = out[:limit]
    if not out:
        raise SystemExit('no AREA22 capture found')
    return out
