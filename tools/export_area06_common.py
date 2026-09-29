#!/usr/bin/env python3
"""Shared inputs of the AREA06 asset exporters (docs/AREA06_ASSETS.md).

AREA06 reuses the AREA01 exporters' machinery by import, unchanged, the way
the AREA00, AREA02, AREA04 and AREA22 exporters do: export_area01_common (the
load map from the loader's own descriptors and cursors, LoadedImage,
compare_load_map, the EMSC writer) and, through it, everything the AREA01
level / table / sound exporters and the AREA01 checker compute. Those
modules read their area constants as attributes of export_area01_common at
call time, so `configure(sub)` points them at AREA06 (area 6, the given sub,
OVERLAY/AREA06.BIN = MWo3 id 6 at 0x823500) and at the AREA06 captures;
nothing in those files is edited.

AREA06's level block is NESTED (INDEX.IDX sector 10: a top block with one
file, id 0x41, and two nested blocks, one per sub), like AREA01 / AREA00 /
AREA02 / AREA04 and unlike AREA22. What is AREA06's own here is the
labelling (AREA22_ASSETS.md finding 8, "the resident-offset label rule"):
a nested descriptor's file list (id << 24 | offset) is the list 001FFCD0
state 7 relocates as D_0028A490[id] = D_0028A740 + offset, so its offsets
are RESIDENT-relative (they count from the nested resident offset +0x14),
while the decomp's tools/extract_data.py cut chunk10.nN/fII_idXX.bin at the
same offsets read as BLOCK offsets. `build_load_map_ids` therefore keeps
export_area01_common.build_load_map's rows and bytes (the extracted files are
contiguous slices of the block, so the bytes are unaffected) and relabels
the nested rows by relocation id (`chunk10.n<sub>/idXX`), splitting a row
where an id boundary falls inside an extracted file; each row still names
the extracted file and offset its bytes are read from. The top block's
resident offset is 0, so its one file keeps its extracted name (the id and
the name agree). `configure()` installs the id-labelled builder as
export_area01_common.build_load_map, so every reused function gets it. A
process must not mix AREA06 work with another area's.

Nothing here is original data: every byte is read from the user's own files
(the pinned boot ELF, the AREA06 overlay and the extracted chunk10 files
under ../Extermination/extract/, the disc image for the descriptors and the
GS upload replay), and every check compares them with the recorded AREA06
captures (SEVENTH_LEVEL_ROUTE.md):
  * the AREA06 arrival: the end of beat a01u_02
    (../Extermination/build/s87/route_a01u/a01u_02_progression_exit/, area
    bytes 06 00 00, overlay id 6 resident);
  * the a06 beats a06_00 .. a06_05 and the side beat a06_s1
    (build/s87/route_a06/); a06_s0 ends in AREA01 and is excluded by its area
    byte.

Which sub each capture loaded is measured, not assumed: its D_00810701
selects the nested block, and `loaded_sub_proof` compares the whole resident
load map of each sub with the capture's RAM. Every AREA06 capture is sub 0
(door [2] is a room move inside the sub); sub 1 (chunk10.n1) is not loaded by
any capture and is not exported.
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area01_common as C  # noqa: E402

ROOT, DECOMP = C.ROOT, C.DECOMP
OVERLAY_PATH = C.EXTRACT / 'OVERLAY/AREA06.BIN'
OVERLAY_SIZE, OVERLAY_ID = 0x5A00, 6          # 23,040 bytes, MWo3 id 6 at 0x823500
AREA = 6
SUBS = (0,)                                   # sub 1 is not loaded by any capture
ALL_SUBS = (0, 1)                             # the nested blocks of the descriptor
OUT = ROOT / 'assets/area06'
SCRATCH = ROOT / 'build/area06/assets'
ROUTE_A01U = DECOMP / 'build/s87/route_a01u'
ROUTE_A06 = DECOMP / 'build/s87/route_a06'
ARRIVAL = ROUTE_A01U / 'a01u_02_progression_exit'
D_0028A490 = 0x28A490                         # the relocation targets, D_0028A490[id]

# SHA-256 of the user's extract/OVERLAY/AREA06.BIN (a hash, not disc data):
# the overlay reference is as fixed as the pinned ELF
OVERLAY_SHA256 = 'eb5bd82c38ad27084dd67bd8ed5b9f42830b6f13d57c1d1fe04f73097dadc997'

_BLOCK_BUILD_LOAD_MAP = C.build_load_map      # AREA01's builder (block-offset labels)
if getattr(_BLOCK_BUILD_LOAD_MAP, '__module__', None) != C.__name__:
    # another area's configure() replaced it before this import (AREA22's
    # flat builder): the id relabelling needs AREA01's nested builder
    raise ImportError('export_area06_common must be imported before another area replaces build_load_map')


def read_overlay():
    ov = C.read_overlay()
    if C.sha(ov) != OVERLAY_SHA256:
        raise SystemExit(f'{OVERLAY_PATH}: not the pinned AREA06 overlay')
    return ov


def sub_out(sub):
    return OUT / f'sub{sub}'


def descriptors(capture, check_iso=True):
    """(top, nested) descriptors of the capture's area and sub: the disc
    image's INDEX.IDX sector when present (build_load_map already checked
    the RAM buffer against it), else the capture's D_00289BC0 buffer."""
    ram = capture.ram
    area, sub = ram[C.D_00810700], ram[C.D_00810700 + 1]
    disc = C.iso_descriptor(area + 4) if check_iso else None
    src = disc if disc is not None else ram[C.D_00289BC0:C.D_00289BC0 + C.NESTED_BASE + C.NESTED_SIZE * 8]
    top = src[:C.DESCRIPTOR_SIZE]
    nested = src[C.NESTED_BASE + C.NESTED_SIZE * sub:C.NESTED_BASE + C.NESTED_SIZE * (sub + 1)]
    return top, nested


def relocation_lists(top, nested):
    """{'top': [(id, offset)], 'nested': [(id, offset)]}: the file lists of
    the two descriptors (+0x1C entries after each one's section table)."""
    def lst(d):
        first, count = C.u16(d, 0x0C), C.u16(d, 0x0E)
        return C._files(d, 0x20 + 8 * (first + count), C.u32(d, 0x1C))
    return dict(top=lst(top), nested=lst(nested))


def build_load_map_ids(capture, check_iso=True):
    """[(address, path, file offset, size, label)] of the AREA06 data the
    loader left resident: export_area01_common's (AREA01) builder for the
    rows and bytes, with every nested row relabelled by relocation id.

    Nested id i spans resident offsets [offset_i, offset_i+1) (the last one
    up to the resident length, block size - +0x14), i.e. addresses
    D_0028A740 + offset; a row of the block builder that crosses an id
    boundary is split there. Rows keep their extracted source path and file
    offset. The top block (resident offset 0, the whole block at
    D_0028A73C) keeps the builder's labels, which then name its ids
    correctly; the builder refuses a top block with upload sections or a
    resident offset."""
    rows, info = _BLOCK_BUILD_LOAD_MAP(capture, check_iso)
    top, nested = descriptors(capture, check_iso)
    if C.u32(top, 0x14) != 0:
        raise SystemExit(f'{capture.name}: the top block has a resident offset; not modelled')
    lists = relocation_lists(top, nested)
    resident, size = C.u32(nested, 0x14), C.u32(nested, 8)
    length = size - resident
    base = C.u32(capture.ram, C.D_0028A740)
    offs = [o for _i, o in lists['nested']]
    if not offs or offs[0] != 0 or offs != sorted(set(offs)) or offs[-1] >= length:
        raise SystemExit(f'{capture.name}: the nested relocation list does not tile the resident region')
    bounds = [(ident, base + o, base + (offs[k + 1] if k + 1 < len(offs) else length))
              for k, (ident, o) in enumerate(lists['nested'])]
    nchunk = info['nested']
    out = []
    for a, path, off, n, label in rows:
        if not label.startswith(nchunk + '/'):
            out.append((a, path, off, n, label))
            continue
        for ident, lo, hi in bounds:
            x, y = max(a, lo), min(a + n, hi)
            if x < y:
                out.append((x, path, off + x - a, y - x, f'{nchunk}/id{ident:02x}'))
    covered = sum(r[3] for r in out if r[4].startswith(nchunk + '/'))
    if covered != length or sum(r[3] for r in rows if r[4].startswith(nchunk + '/')) != length:
        raise SystemExit(f'{capture.name}: the nested rows cover {covered:#x} bytes, not the resident '
                         f'length {length:#x}')
    info = dict(info, ids=[(f'id{i:02x}', lo, hi - lo) for i, lo, hi in bounds],
                top_ids=[(f'id{i:02x}', C.u32(capture.ram, C.D_0028A73C) + o) for i, o in lists['top']],
                resident_length=length)
    return out, info


def relocation_problems(capture, check_iso=True):
    """Every word D_0028A490[id] of the two lists equals its formula in the
    capture: top ids D_0028A73C + offset, nested ids D_0028A740 + offset
    (001FFCD0 state 7). Returns (problems, {id: address})."""
    top, nested = descriptors(capture, check_iso)
    lists = relocation_lists(top, nested)
    out, where = [], {}
    for which, cursor in (('top', C.D_0028A73C), ('nested', C.D_0028A740)):
        base = C.u32(capture.ram, cursor)
        for ident, off in lists[which]:
            if ident in where:
                out.append(f'{capture.name}: id {ident:#x} relocated by both lists')
            where[ident] = base + off
            got = C.u32(capture.ram, D_0028A490 + 4 * ident)
            if got != base + off:
                out.append(f'{capture.name}: D_0028A490[{ident:#x}] = {got:#x}, not the {which} cursor + {off:#x}')
    return out, where


def configure(sub):
    """Point export_area01_common (and so every AREA01 exporter and checker
    function that reads it) at AREA06 sub `sub`. Returns the module."""
    C.OVERLAY_PATH = OVERLAY_PATH
    C.OVERLAY_SIZE, C.OVERLAY_ID = OVERLAY_SIZE, OVERLAY_ID
    C.AREA, C.SUB = AREA, sub
    C.OUT = sub_out(sub)
    C.SCRATCH = SCRATCH / f'sub{sub}'
    C.ROUTE_A01 = ROUTE_A06
    C.ARRIVAL = ARRIVAL
    C.captures = lambda limit=None: captures(sub, limit)
    C.build_load_map = build_load_map_ids
    return C


def capture_paths():
    """The arrival first, then the a06 beats in order."""
    return [ARRIVAL] + sorted(p for p in ROUTE_A06.glob('a06_*') if (p / 'eeMemory.bin').exists())


def all_captures():
    """[(Capture, sub)] of every AREA06 capture. A capture belongs to AREA06
    when its area byte is 6 and the AREA06 overlay (MWo3 id 6) is resident;
    its sub is D_00810701."""
    out = []
    for p in capture_paths():
        if not (p / 'eeMemory.bin').exists():
            continue
        c = C.Capture(p)
        if c.area[0] != AREA:
            continue
        if c.ram[C.OVERLAY_ARENA:C.OVERLAY_ARENA + 4] != b'MWo3' or C.u32(c.ram, C.OVERLAY_ARENA + 4) != OVERLAY_ID:
            raise SystemExit(f'{p}: area 6 without the AREA06 overlay resident')
        out.append((c, c.area[1]))
    return out


def excluded_captures():
    """[(name, area bytes)] of the capture folders that end in another area."""
    out = []
    for p in capture_paths():
        if (p / 'eeMemory.bin').exists():
            ram = (p / 'eeMemory.bin').read_bytes()
            if ram[C.D_00810700] != AREA:
                out.append((p.name, tuple(ram[C.D_00810700:C.D_00810700 + 3])))
    return out


def captures(sub, limit=None):
    out = [c for c, s in all_captures() if s == sub]
    if limit:
        out = out[:limit]
    if not out:
        raise SystemExit(f'no AREA06 sub-{sub} capture found')
    return out


def loaded_sub_proof(pairs):
    """For every capture, the number of differing 16-byte rows of each
    nested block's load map (built with the capture's own cursors) against
    its RAM: the capture's own sub must be the smaller by far. Returns
    {name: {sub: rows}}."""
    out = {}
    for cap, _sub in pairs:
        rows = {}
        for s in ALL_SUBS:
            ram = bytearray(cap.ram)
            ram[0x810701] = s
            proxy = C.Capture.__new__(C.Capture)
            proxy.path, proxy.name, proxy.ram, proxy.spad, proxy.gs = cap.path, cap.name, bytes(ram), cap.spad, cap.gs
            lmap, _info = C.build_load_map(proxy)
            rows[s] = sum(r['differing_rows'] for r in C.compare_load_map(C.LoadedImage(lmap), proxy))
        out[cap.name] = rows
    return out
