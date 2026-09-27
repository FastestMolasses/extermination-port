#!/usr/bin/env python3
"""Shared inputs of the AREA01 sub-0 asset exporters (docs/AREA01_ASSETS.md).

Nothing here is original data: every byte is read from the user's own files
(the pinned boot ELF config/SCUS_971.12, the AREA01 overlay and the extracted
disc files under ../Extermination/extract/, the disc image for the GS upload
replay) and every check compares them with the recorded AREA01 captures
(../Extermination/build/s87/route_a01/<beat>/ and the AREA01 arrival
../Extermination/build/s87/route/15_level_exit/).

The load map is derived from the loader's own descriptors and cursors, not
from file-role labels:
  * the top area descriptor (INDEX.IDX sector D_00810700 + 4, read into
    D_00289BC0) and the nested descriptor at +0x100 + 0x70 * sub (the
    capture's D_00810701), taken from the disc image and checked equal to
    D_00289BC0 in every capture whose buffer still holds them;
  * a descriptor's file list (id << 24 | block offset) names the extracted
    files extract/chunkSS[.nN]/fII_idXX.bin in list order;
  * the top block (resident offset 0) lies whole at the first load cursor
    D_0028A73C; a nested block's resident region (+0x14 .. block size) lies at
    the second cursor D_0028A740.
Every mapped byte is compared with every capture (compare_load_map).
"""
from __future__ import annotations

import hashlib
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
ELF_PATH = DECOMP / 'config/SCUS_971.12'
OVERLAY_PATH = DECOMP / 'extract/OVERLAY/AREA01.BIN'
EXTRACT = DECOMP / 'extract'
ISO_PATH = DECOMP / 'Extermination-rebuilt.iso'
OUT = ROOT / 'assets/area01'
SCRATCH = ROOT / 'build/area01/assets'

ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
ELF_VADDR, ELF_OFFSET, ELF_FILESZ = 0x100000, 0x300, 0x175B00
OVERLAY_ARENA, OVERLAY_SIZE, OVERLAY_ID = 0x823500, 0x9800, 2
AREA, SUB = 1, 0

D_00810700 = 0x810700
D_00289BC0 = 0x289BC0          # the area descriptor block (INDEX.IDX sector)
D_0028A73C, D_0028A740 = 0x28A73C, 0x28A740   # load cursors (top, nested)
DESCRIPTOR_SIZE, NESTED_BASE, NESTED_SIZE = 0x100, 0x100, 0x70

ROUTE_A01 = DECOMP / 'build/s87/route_a01'
ARRIVAL = DECOMP / 'build/s87/route/15_level_exit'


def u32(b, a):
    return struct.unpack_from('<I', b, a)[0]


def s32(b, a):
    return struct.unpack_from('<i', b, a)[0]


def u16(b, a):
    return struct.unpack_from('<H', b, a)[0]


def s16(b, a):
    return struct.unpack_from('<h', b, a)[0]


def read_elf() -> bytes:
    elf = ELF_PATH.read_bytes()
    if hashlib.sha256(elf).hexdigest() != ELF_SHA256:
        raise SystemExit(f'{ELF_PATH}: not the pinned SCUS-97112 boot ELF')
    return elf


def read_overlay() -> bytes:
    ov = OVERLAY_PATH.read_bytes()
    if len(ov) != OVERLAY_SIZE or ov[:4] != b'MWo3' or u32(ov, 4) != OVERLAY_ID \
            or u32(ov, 8) != OVERLAY_ARENA:
        raise SystemExit(f'{OVERLAY_PATH}: not the original AREA01 overlay (MWo3 id 2 at 0x823500)')
    return ov


def static_reader(elf: bytes, overlay: bytes):
    """read(address, size) over the ELF load segment and the AREA01 overlay
    (loaded whole at its header address, as the captures show)."""
    def read(address: int, size: int) -> bytes:
        if ELF_VADDR <= address and address + size <= ELF_VADDR + ELF_FILESZ:
            at = address - ELF_VADDR + ELF_OFFSET
            return elf[at:at + size]
        if OVERLAY_ARENA <= address and address + size <= OVERLAY_ARENA + OVERLAY_SIZE:
            at = address - OVERLAY_ARENA
            return overlay[at:at + size]
        raise ValueError(f'address {address:#x} outside the ELF and the AREA01 overlay')
    return read


class Capture:
    """One recorded original frame set (end-of-beat images)."""

    def __init__(self, path: Path):
        self.path, self.name = path, path.name
        self.ram = (path / 'eeMemory.bin').read_bytes()
        spad = path / 'scratchpad.bin'
        self.spad = spad.read_bytes() if spad.exists() else None
        gs = path / 'gs.bin'
        self.gs = gs if gs.exists() else None

    @property
    def area(self):
        return tuple(self.ram[D_00810700:D_00810700 + 3])


def captures(limit=None):
    """Every recorded AREA01 sub-0 capture: the route beats a01_00..06 and
    side beats a01_s0..s3 (a01_07 ends in AREA00 and is excluded by its area
    bytes) and the AREA01 arrival of the first level's exit (beat 15)."""
    paths = sorted(p for p in ROUTE_A01.glob('a01_*') if (p / 'eeMemory.bin').exists())
    if (ARRIVAL / 'eeMemory.bin').exists():
        paths.append(ARRIVAL)
    out = []
    for p in paths:
        c = Capture(p)
        if c.area[:2] != (AREA, SUB):
            continue
        if c.ram[OVERLAY_ARENA:OVERLAY_ARENA + 4] != b'MWo3' or u32(c.ram, OVERLAY_ARENA + 4) != OVERLAY_ID:
            raise SystemExit(f'{p}: area 1 without the AREA01 overlay resident')
        out.append(c)
        if limit and len(out) >= limit:
            break
    if not out:
        raise SystemExit('no AREA01 sub-0 capture found under ' + str(ROUTE_A01))
    return out


# ---------------------------------------------------------------------------
# Load map


def _files(desc: bytes, first_entry: int, count: int):
    """[(id, block offset)] of a descriptor's file list."""
    out = []
    for i in range(count):
        w = u32(desc, first_entry + 4 * i)
        out.append((w >> 24, w & 0xFFFFFF))
    return out


def _extract_files(chunk: str, entries, block_size: int):
    """(path, block offset, size) per file entry; the extracted file must be
    fII_idXX.bin with the entry's index and id, and at least as long as the
    span to the next entry (extracted files are sector padded)."""
    out = []
    for i, (ident, off) in enumerate(entries):
        end = entries[i + 1][1] if i + 1 < len(entries) else block_size
        path = EXTRACT / chunk / f'f{i:02d}_id{ident:02x}.bin'
        if not path.exists():
            raise SystemExit(f'{path}: missing (the descriptor names file {i} id {ident:#x})')
        if path.stat().st_size < end - off:
            raise SystemExit(f'{path}: shorter than its descriptor span {end - off:#x}')
        out.append((path, off, end - off))
    return out


def iso_descriptor(sector: int) -> bytes | None:
    """The INDEX.IDX sector of the user's disc image (None without it)."""
    if not ISO_PATH.exists():
        return None
    import importlib.util
    import sys
    name = '_a01_export_level'
    mod = sys.modules.get(name)
    if mod is None:
        spec = importlib.util.spec_from_file_location(name, DECOMP / 'tools/export_level.py')
        mod = importlib.util.module_from_spec(spec)
        sys.modules[name] = mod
        spec.loader.exec_module(mod)
    return mod.BackgroundDisc(None, str(ISO_PATH)).descriptor(sector)


def build_load_map(capture: Capture, check_iso=True):
    """[(address, path, file offset, size, label)] of the AREA01 sub-0 data
    the loader left resident. The descriptors come from the disc image's
    INDEX.IDX sector (D_00810700 + 4) when it is present, else from the
    capture's D_00289BC0 buffer; the cursors always from the capture. The
    buffer is reused by later reads (a later read in beat a01_s1 leaves
    sector 36 there), so a capture whose buffer holds the area descriptor
    must equal the disc sector; `rambuf` in the info says which it was."""
    ram = capture.ram
    area, sub = ram[D_00810700], ram[D_00810700 + 1]
    rtop = ram[D_00289BC0:D_00289BC0 + DESCRIPTOR_SIZE]
    rnested = ram[D_00289BC0 + NESTED_BASE + NESTED_SIZE * sub:
                  D_00289BC0 + NESTED_BASE + NESTED_SIZE * (sub + 1)]
    disc = iso_descriptor(area + 4) if check_iso else None
    if disc is not None:
        top = disc[:DESCRIPTOR_SIZE]
        nested = disc[NESTED_BASE + NESTED_SIZE * sub:NESTED_BASE + NESTED_SIZE * (sub + 1)]
        if u32(rtop, 0) == area + 4:
            if (rtop, rnested) != (top, nested):
                raise SystemExit(f'{capture.name}: RAM descriptors differ from INDEX.IDX sector {area + 4}')
            rambuf = 'equal to the disc sector'
        else:
            rambuf = f'reused (sector word {u32(rtop, 0)})'
    else:
        top, nested, rambuf = rtop, rnested, 'used (no disc image)'
    sector = u32(top, 0)
    if sector != area + 4:
        raise SystemExit(f'{capture.name}: descriptor sector word {sector}, not {area + 4}')
    chunk = f'chunk{sector:02d}'
    if u32(top, 0x18) <= sub:
        raise SystemExit(f'{capture.name}: sub {sub} outside the nested count')
    out = []
    # Top block: nothing resident-offset, the whole block at cursor 0.
    tfirst, tcount = u16(top, 0x0C), u16(top, 0x0E)
    if u32(top, 0x14) != 0 or tcount != 0:
        raise SystemExit('top descriptor carries upload sections; not modelled')
    tfiles = _files(top, 0x20 + 8 * (tfirst + tcount), u32(top, 0x1C))
    base = u32(ram, D_0028A73C)
    for path, off, size in _extract_files(chunk, tfiles, u32(top, 8)):
        out.append((base + off, path, 0, size, f'{chunk}/{path.name}'))
    # Nested block: the resident region at cursor 1.
    nchunk = f'{chunk}.n{sub}'
    nfirst, ncount = u16(nested, 0x0C), u16(nested, 0x0E)
    resident, nsize = u32(nested, 0x14), u32(nested, 8)
    nfiles = _files(nested, 0x20 + 8 * (nfirst + ncount), u32(nested, 0x1C))
    base = u32(ram, D_0028A740)
    for path, off, size in _extract_files(nchunk, nfiles, nsize):
        lo, hi = max(off, resident), off + size
        if lo >= hi:
            continue
        out.append((base + lo - resident, path, lo - off, hi - lo, f'{nchunk}/{path.name}'))
    return out, dict(rambuf=rambuf, chunk=chunk, nested=nchunk, resident=resident, top_cursor=u32(ram, D_0028A73C),
                     nested_cursor=u32(ram, D_0028A740),
                     sections=[(u32(nested, 0x20 + 8 * k), u32(nested, 0x24 + 8 * k))
                               for k in range(nfirst + ncount)])


class LoadedImage:
    """The mapped bytes by original address, read from the extracted files."""

    def __init__(self, load_map):
        self.map = sorted(load_map)
        self.cache = {}

    def _data(self, path):
        d = self.cache.get(path)
        if d is None:
            d = self.cache[path] = path.read_bytes()
        return d

    def locate(self, address):
        for a, path, off, size, label in self.map:
            if a <= address < a + size:
                return a, path, off, size, label
        return None

    def read(self, address, size):
        out = bytearray()
        while size:
            hit = self.locate(address)
            if hit is None:
                raise ValueError(f'{address:#x} is not in the AREA01 load map')
            a, path, off, n, _label = hit
            take = min(size, a + n - address)
            d = self._data(path)
            out += d[off + address - a:off + address - a + take]
            address += take
            size -= take
        return bytes(out)

    def span(self):
        return self.map[0][0], self.map[-1][0] + self.map[-1][3]


def compare_load_map(image: LoadedImage, capture: Capture, allow=()):
    """Per mapped file: (label, bytes compared, differing 16-byte rows).
    `allow` = [(lo, hi)] address ranges whose differences are expected and
    reported separately (run-time state the caller checks itself). The
    allowance is per byte: a differing row is unexpected when any of its
    differing bytes lies outside every allowed range, so a range that ends
    inside a row excuses only its own bytes of that row."""
    rows = []
    for a, path, off, size, label in image.map:
        d = image._data(path)[off:off + size]
        r = capture.ram[a:a + size]
        bad = [a + k for k in range(0, size, 16) if d[k:k + 16] != r[k:k + 16]]
        unexpected = [x for x in bad
                      if any(d[x - a + j] != r[x - a + j] and not any(lo <= x + j < hi for lo, hi in allow)
                             for j in range(min(16, a + size - x)))]
        rows.append(dict(file=label, address=a, bytes=size, differing_rows=len(bad),
                         unexpected_rows=len(unexpected), first_unexpected=unexpected[:4]))
    return rows


def emsc(base: int, data: bytes, entry: int | None = None) -> bytes:
    """EMSC v1 address window (em_script_image_load's format:
    "EMSC", u32 1, base, entry, length, then the bytes)."""
    return struct.pack('<4s4I', b'EMSC', 1, base, base if entry is None else entry, len(data)) + data


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()
