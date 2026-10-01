#!/usr/bin/env python3
"""Shared inputs of the AREA13 asset exporters (docs/AREA13_ASSETS.md).

Two targets, each the area and sub a recorded ninth-level capture loaded
(NINTH_LEVEL_ROUTE.md):
  * 'area13': AREA13 (area 0x0D) sub 0, OVERLAY/AREA13.BIN = MWo3 id 10 at
    0x823500. Captures: the AREA13 arrival, the end of beat a04b_04
    (../Extermination/build/s87/route_a04b/a04b_04_lift/, area bytes 0D 00
    00), and the a13 beats a13_00 .. a13_04 (build/s87/route_a13/); a13_05
    ends in AREA19 and is that target's capture;
  * 'area19': AREA19 (area 0x13) sub 0, OVERLAY/AREA19.BIN = MWo3 id 0x10.
    One capture: the end of a13_05 (area bytes 13 00 09, the foot of
    AREA19's entry-9 ladder). Nothing past that arrival is covered.

The AREA01 exporters' machinery is reused by import, unchanged, the way the
AREA00 .. AREA06 exporters do: export_area01_common (LoadedImage,
compare_load_map, _files, _extract_files, iso_descriptor, Capture, emsc,
static_reader) and, through it, everything the AREA01 level / table / sound
exporters and the AREA01 checker compute. Those modules read their area
constants as attributes of export_area01_common at call time, so
`configure(target)` points them at the target; nothing in them is edited.

The two load layouts (INDEX.IDX sector D_00810700 + 4, read by 001FFCD0;
NEARMISS C src/func_001FFCD0.c):
  * AREA13 (sector 17) is FLAT, like AREA22: no nested block (+0x18 = 0),
    upload sections in the top descriptor (+0x0C first = 1, +0x0E count =
    1: section 0 the sound container, one group-A section the GS texel
    upload) and a resident offset +0x14. export_area22_common.
    build_load_map_flat models it by 001FFCD0 state 5 and labels every row
    by relocation id (AREA22_ASSETS.md finding 8: the file list is
    resident-relative; the extracted chunk17/fII_idXX.bin names read it as
    block offsets). It is imported and used unchanged;
  * AREA19 (sector 23) is a layout no earlier lane met: a top descriptor
    WITH a group-A section (+0x0C = 0, +0x0E = 1: the GS texel upload) and a
    resident offset, AND two nested blocks (one per sub) whose section 0 is
    the sound container (+0x0C = 1, +0x0E = 0) with their own resident
    offset. export_area01_common.build_load_map refuses a top block with
    sections, and the flat builder refuses nested blocks, so
    `build_load_map_both` composes the loader's two rules: state 5 streams
    the top block's +0x14 .. +8 to D_0028A73C and state 7 relocates the top
    list as D_0028A490[id] = D_0028A73C + offset; state 9 streams the nested
    block's +0x14 .. +8 to D_0028A740 and state 11 relocates the nested list
    as D_0028A740 + offset. Rows are labelled `chunk23/idXX` and
    `chunk23.n<sub>/idXX` by those readings; each names the extracted file
    and offset its bytes come from. The second cursor is read from the
    capture (the capture's D_0028A740 is 0x2B40 above D_0028A73C + the top
    resident length that state 5 stores; something between states 7 and 9
    moves it, as in the AREA06 captures; not traced here).

Nothing here is original data: every byte is read from the user's own files
(the pinned boot ELF, the pinned overlays, the extracted chunk17 / chunk23
files under ../Extermination/extract/, the disc image for the descriptors
and the GS upload replay), and every check compares them with the recorded
captures. A process may configure both targets in turn: configure() sets
every export_area01_common attribute either target reads, and the level /
table / sound modules re-install their own hooks per call.
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area01_common as C  # noqa: E402

_NESTED_BUILD_LOAD_MAP = C.build_load_map           # AREA01's builder, kept for configure()
if getattr(_NESTED_BUILD_LOAD_MAP, '__module__', None) != C.__name__:
    raise ImportError('export_area13_common must be imported before another area replaces build_load_map')
import export_area06_common  # noqa: E402,F401  (imported while AREA01's builder is installed: the AREA06
#                                    level module, whose derive_scaled_hull is reused, requires it)
import export_area22_common as A22  # noqa: E402  (build_load_map_flat)

ROOT, DECOMP = C.ROOT, C.DECOMP
OUT = ROOT / 'assets/area13'
SCRATCH = ROOT / 'build/area13/assets'
ROUTE_A04B = DECOMP / 'build/s87/route_a04b'
ROUTE_A13 = DECOMP / 'build/s87/route_a13'
ARRIVAL = ROUTE_A04B / 'a04b_04_lift'
D_0028A490 = 0x28A490                         # the relocation targets, D_0028A490[id]


class Target:
    """One (area, sub) the a13 captures load."""

    def __init__(self, name, area, sub, overlay, overlay_size, overlay_id, sha256, chunk, nested, out, label):
        self.name, self.area, self.sub = name, area, sub
        self.overlay_path = C.EXTRACT / 'OVERLAY' / overlay
        self.overlay_size, self.overlay_id, self.overlay_sha256 = overlay_size, overlay_id, sha256
        self.chunk, self.nested, self.out, self.label = chunk, nested, out, label

    @property
    def scratch(self):
        return SCRATCH / self.name

    def __repr__(self):
        return f'Target({self.name})'


# SHA-256 of the user's extract/OVERLAY/AREA13.BIN and AREA19.BIN (hashes,
# not disc data): the overlay references are as fixed as the pinned ELF
AREA13_OVERLAY_SHA256 = 'c2b7e7ecdf8764db8cb2aec01a2846791e1466c99acf5be6bc0095ec9db197e9'
AREA19_OVERLAY_SHA256 = '75a2a056049a853a2ba675f9922b36d80f0bbb6de03a6d7c47b1958c20c175e9'
AREA13 = Target('area13', 0x0D, 0, 'AREA13.BIN', 0xAD80, 10, AREA13_OVERLAY_SHA256,
                'chunk17', False, OUT / 'sub0', 'AREA13')
AREA19 = Target('area19', 0x13, 0, 'AREA19.BIN', 0xC380, 0x10, AREA19_OVERLAY_SHA256,
                'chunk23', True, OUT / 'area19/sub0', 'AREA19')
TARGETS = {t.name: t for t in (AREA13, AREA19)}
_CURRENT = [AREA13]


def current():
    return _CURRENT[0]


def read_overlay(target=None):
    t = target or current()
    configure(t)
    ov = C.read_overlay()
    if C.sha(ov) != t.overlay_sha256:
        raise SystemExit(f'{t.overlay_path}: not the pinned {t.label} overlay')
    return ov


def descriptor(capture, check_iso=True):
    """(sector bytes, rambuf note): INDEX.IDX sector D_00810700 + 4 (top at
    +0, nested blocks at +0x100 + 0x70 * sub) from the disc image, checked
    equal to the capture's D_00289BC0 buffer whenever that buffer still
    holds the sector; without a disc image, the buffer."""
    ram = capture.ram
    area = ram[C.D_00810700]
    rbuf = ram[C.D_00289BC0:C.D_00289BC0 + 0x800]
    disc = C.iso_descriptor(area + 4) if check_iso else None
    if disc is None:
        return rbuf, 'used (no disc image)'
    if C.u32(rbuf, 0) == area + 4:
        nested = C.u32(disc, 0x18)
        span = C.NESTED_BASE + C.NESTED_SIZE * nested if nested else C.DESCRIPTOR_SIZE
        if rbuf[:span] != disc[:span]:
            raise SystemExit(f'{capture.name}: RAM descriptor differs from INDEX.IDX sector {area + 4}')
        return disc, 'equal to the disc sector'
    return disc, f'reused (sector word {C.u32(rbuf, 0)})'


def _files(d):
    first, count = C.u16(d, 0x0C), C.u16(d, 0x0E)
    return C._files(d, 0x20 + 8 * (first + count), C.u32(d, 0x1C))


def _sections(d):
    first, count = C.u16(d, 0x0C), C.u16(d, 0x0E)
    return [(C.u32(d, 0x20 + 8 * k), C.u32(d, 0x24 + 8 * k)) for k in range(first + count)]


def _resident_rows(desc, chunk, base, capture):
    """[(address, path, file offset, size, label)] of one block's resident
    region (+0x14 .. +8) at `base`, one or more rows per relocation id
    (resident-relative offsets), and [(id, address, size)]."""
    resident, size = C.u32(desc, 0x14), C.u32(desc, 8)
    if C.u32(desc, 0x10) != 0:
        raise SystemExit(f'{capture.name}: group-B sections in {chunk}; not modelled')
    entries = _files(desc)
    length = size - resident
    offs = [o for _i, o in entries]
    if not entries or offs[0] != 0 or offs != sorted(set(offs)) or offs[-1] >= length:
        raise SystemExit(f'{capture.name}: the {chunk} relocation list does not tile the resident region')
    sources = C._extract_files(chunk, entries, size)      # byte sources (block-offset cuts)
    out, ids = [], []
    for i, (ident, off) in enumerate(entries):
        end = offs[i + 1] if i + 1 < len(offs) else length
        ids.append((ident, base + off, end - off))
        lo, hi = resident + off, resident + end
        for path, foff, span in sources:
            a, b = max(lo, foff), min(hi, foff + span)
            if a < b:
                out.append((base + a - resident, path, a - foff, b - a, f'{chunk}/id{ident:02x}'))
    return out, ids, length


def build_load_map_both(capture, check_iso=True):
    """[(address, path, file offset, size, label)] of the AREA19-shaped load
    (a top block with upload sections and a resident offset, plus the
    capture's nested block): the top resident region at D_0028A73C (001FFCD0
    state 5; D_0028A740 >= its end), the nested resident region at
    D_0028A740 (state 9). Labels are relocation ids of each list."""
    ram = capture.ram
    sector, rambuf = descriptor(capture, check_iso)
    area, sub = ram[C.D_00810700], ram[C.D_00810700 + 1]
    if C.u32(sector, 0) != area + 4:
        raise SystemExit(f'{capture.name}: descriptor sector word {C.u32(sector, 0)}, not {area + 4}')
    if C.u32(sector, 0x18) <= sub:
        raise SystemExit(f'{capture.name}: sub {sub} outside the nested count {C.u32(sector, 0x18)}')
    top = sector[:C.DESCRIPTOR_SIZE]
    nested = sector[C.NESTED_BASE + C.NESTED_SIZE * sub:C.NESTED_BASE + C.NESTED_SIZE * (sub + 1)]
    chunk = f'chunk{area + 4:02d}'
    nchunk = f'{chunk}.n{sub}'
    tbase, nbase = C.u32(ram, C.D_0028A73C), C.u32(ram, C.D_0028A740)
    trows, tids, tlen = _resident_rows(top, chunk, tbase, capture)
    if nbase < tbase + tlen:
        raise SystemExit(f'{capture.name}: D_0028A740 {nbase:#x} below the top resident end {tbase + tlen:#x}')
    nrows, nids, nlen = _resident_rows(nested, nchunk, nbase, capture)
    return trows + nrows, dict(
        rambuf=rambuf, chunk=chunk, nested=nchunk, resident=C.u32(top, 0x14), nested_resident=C.u32(nested, 0x14),
        top_cursor=tbase, nested_cursor=nbase, top_length=tlen, nested_length=nlen,
        cursor_gap=nbase - (tbase + tlen),
        ids=[(f'id{i:02x}', a, n) for i, a, n in tids], nested_ids=[(f'id{i:02x}', a, n) for i, a, n in nids],
        sections=_sections(top), nested_sections=_sections(nested))


def build_load_map(capture, check_iso=True):
    """The current target's builder (configure() installs it as
    export_area01_common.build_load_map)."""
    if current().nested:
        return build_load_map_both(capture, check_iso)
    return A22.build_load_map_flat(capture, check_iso)


def relocation_lists(capture, check_iso=True):
    """{'top': [(id, offset)], 'nested': [(id, offset)]} of the capture's
    descriptor (nested empty for a flat one)."""
    sector, _note = descriptor(capture, check_iso)
    sub = capture.ram[C.D_00810700 + 1]
    top = sector[:C.DESCRIPTOR_SIZE]
    out = dict(top=_files(top), nested=[])
    if C.u32(top, 0x18):
        out['nested'] = _files(sector[C.NESTED_BASE + C.NESTED_SIZE * sub:C.NESTED_BASE + C.NESTED_SIZE * (sub + 1)])
    return out


def relocation_problems(capture, check_iso=True):
    """Every word D_0028A490[id] of the lists equals its formula in the
    capture: top ids D_0028A73C + offset, nested ids D_0028A740 + offset
    (001FFCD0 states 7 and 11). Returns (problems, {id: address})."""
    lists = relocation_lists(capture, check_iso)
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


def configure(target='area13'):
    """Point export_area01_common (and so every AREA01 exporter and checker
    function that reads it) at `target`. Returns the module."""
    t = TARGETS[target] if isinstance(target, str) else target
    _CURRENT[0] = t
    C.OVERLAY_PATH = t.overlay_path
    C.OVERLAY_SIZE, C.OVERLAY_ID = t.overlay_size, t.overlay_id
    C.AREA, C.SUB = t.area, t.sub
    C.OUT = t.out
    C.SCRATCH = t.scratch
    C.ROUTE_A01 = ROUTE_A13
    C.ARRIVAL = ARRIVAL if t is AREA13 else ROUTE_A13 / 'a13_05_shaft'
    C.captures = lambda limit=None: captures(t, limit)
    C.build_load_map = build_load_map
    return C


def capture_paths():
    """The AREA13 arrival first, then the a13 beats in order."""
    return [ARRIVAL] + sorted(p for p in ROUTE_A13.glob('a13_*') if (p / 'eeMemory.bin').exists())


def all_captures(target):
    """[Capture] of every capture of `target`: its area byte and sub, with its
    overlay resident."""
    t = TARGETS[target] if isinstance(target, str) else target
    out = []
    for p in capture_paths():
        if not (p / 'eeMemory.bin').exists():
            continue
        c = C.Capture(p)
        if c.area[0] != t.area:
            continue
        if c.ram[C.OVERLAY_ARENA:C.OVERLAY_ARENA + 4] != b'MWo3' or \
                C.u32(c.ram, C.OVERLAY_ARENA + 4) != t.overlay_id:
            raise SystemExit(f'{p}: area {t.area:#x} without the {t.label} overlay resident')
        if c.area[1] != t.sub:
            raise SystemExit(f'{p}: {t.label} capture with D_00810701 = {c.area[1]}; only sub {t.sub} is modelled')
        out.append(c)
    return out


def excluded_captures(target):
    """[(name, area bytes)] of the capture folders that end in another area."""
    t = TARGETS[target] if isinstance(target, str) else target
    out = []
    for p in capture_paths():
        if (p / 'eeMemory.bin').exists():
            ram = (p / 'eeMemory.bin').read_bytes()
            if ram[C.D_00810700] != t.area:
                out.append((p.name, tuple(ram[C.D_00810700:C.D_00810700 + 3])))
    return out


def captures(target, limit=None):
    out = all_captures(target)
    if limit:
        out = out[:limit]
    if not out:
        raise SystemExit(f'no {target} capture found')
    return out


def loaded_sub_proof(caps):
    """AREA19 only: per capture, the differing 16-byte rows of each nested
    block's load map (built with the capture's own cursors) against its RAM;
    the capture's own sub must be the smaller by far. {name: {sub: rows}}."""
    out = {}
    for cap in caps:
        sector, _note = descriptor(cap)
        rows = {}
        for s in range(C.u32(sector, 0x18)):
            ram = bytearray(cap.ram)
            ram[C.D_00810700 + 1] = s
            proxy = C.Capture.__new__(C.Capture)
            proxy.path, proxy.name, proxy.ram, proxy.spad, proxy.gs = cap.path, cap.name, bytes(ram), cap.spad, cap.gs
            lmap, _info = build_load_map_both(proxy)
            rows[s] = sum(r['differing_rows'] for r in C.compare_load_map(C.LoadedImage(lmap), proxy))
        out[cap.name] = rows
    return out


configure(AREA13)
