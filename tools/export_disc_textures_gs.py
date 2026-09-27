#!/usr/bin/env python3
"""export_disc_textures_gs.py - the first level's resident GS textures,
rebuilt from the user's own disc instead of a PCSX2 capture.

Library for tools/export_disc_textures.py and
tools/test_disc_textures_reference.py (docs/DISC_TEXTURES.md). Nothing here
is original data: every byte is read from the user's disc image (INDEX.IDX /
DATA.DAT, located in the ISO or a copy of the DATA/ directory) and checked
against the user's extract (../Extermination/extract/).

Every texture the first level samples from GS local memory is put there by
one routine, 00200830: it hands a buffer to 00101F08 on DMA channel 1 (VIF1),
so the buffer is a VIF1 source chain whose DIRECT data are GIF packets that
set BITBLTBUF / TRXPOS / TRXREG / TRXDIR and send the IMAGE data
(host-to-local, PSMCT32). The buffers it is given, in the order the New Game
route gives them (docs/DISC_TEXTURES.md section 2 names each caller):

  1. boot, 001AB7E0 step 3: 001FF1E0(0x1B) - module 0x1B's upload sections
     (its descriptor's B sections, in its resident region);
  2. New Game, 001AD1A0: 00200830(D_0028A564) - resource slot 0x35, the
     first file of module 0x1B's resident region (the same packet again);
  3. the area load 001FFCD0 (area D_00810700 = 11 -> INDEX.IDX sector 15):
     state 4 runs 001FF590(0xAB, 1), which reads each A section
     (descriptor entries hdr[0x0C] .. hdr[0x0C] + hdr[0x0E] - 1) and
     uploads it; state 7 uploads the B sections of the resident region and
     then calls 00200890;
  4. 00200890: the player texture packet, resource slot 8 / 9 / 0xA / 0xB /
     0xC chosen by D_00810707 and D_00810C60 (module 3's resident files,
     loaded by 001AD1A0's 001FF080(0, 3));
  5. a status page: 001FF830(module) - 001FF3F0 uploads the module's A
     sections (entries 0 .. hdr[0x0E] - 1) and state 7 its B sections;
  6. leaving a page: 0020CDC0's 00200970(1) - slot 0x35 again, then
     00200890.

The replay of one buffer (the VIF1 chain, the DIRECT GIF data and the
PSMCT32 IMAGE writes) is the decomp's export_level._bg_section_chain /
_bg_gs_upload, the model docs/AREA01_ASSETS.md already proved byte-exact
against the AREA01 captures. It refuses anything it does not model.

Runs natively on arm64 macOS (pure Python).
"""
from __future__ import annotations

import importlib.util
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
EXTRACT = DECOMP / 'extract'
ISO_PATH = DECOMP / 'Extermination-rebuilt.iso'
ELF_PATH = DECOMP / 'config/SCUS_971.12'
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'

LOCALMEM = 4 << 20
FREEZE_HEAD, FREEZE_TAIL = 425, 84         # gs_vram.read_localmem's layout

LIBRARY_MODULE = 0x1B                      # 001AB7E0 step 3: 001FF1E0(0x1B)
LIBRARY_SLOT = 0x35                        # D_0028A564 = D_0028A490[0x35]
PLAYER_TEXTURE_MODULE = 3                  # 001AD1A0: 001FF080(0, 3)
FIRST_LEVEL_AREA = 11                      # D_00810700 in every route capture


def _load(name: str, path: Path):
    mod = sys.modules.get(name)
    if mod is None:
        spec = importlib.util.spec_from_file_location(name, path)
        mod = importlib.util.module_from_spec(spec)
        sys.modules[name] = mod
        spec.loader.exec_module(mod)
    return mod


def export_level():
    """The decomp's export_level (disc reader and the upload replay)."""
    return _load('_disc_textures_export_level', DECOMP / 'tools/export_level.py')


def u32(b, a):
    return struct.unpack_from('<I', b, a)[0]


# ---------------------------------------------------------------------------
# The disc
# ---------------------------------------------------------------------------

class Disc:
    """INDEX.IDX / DATA.DAT of the user's disc: an ISO-9660 image (--iso) or
    a mounted disc / a copy of its DATA/ directory (--disc)."""

    def __init__(self, iso: Path | None = None, disc: Path | None = None):
        el = export_level()
        if disc is not None:
            self.image = el.BackgroundDisc(str(disc), None)
        else:
            iso = Path(iso) if iso is not None else ISO_PATH
            if not iso.exists():
                raise SystemExit(f'{iso}: missing; pass --iso <your disc image> or --disc <DATA dir>')
            self.image = el.BackgroundDisc(None, str(iso))
        self._desc = {}

    def descriptor(self, sector: int) -> bytes:
        if sector not in self._desc:
            self._desc[sector] = self.image.descriptor(sector)
        return self._desc[sector]

    def read(self, offset: int, size: int) -> bytes:
        return self.image.read(self.image.data, offset, size)


class Block:
    """One descriptor block (the 0x100 top block of an INDEX.IDX sector or a
    0x70 nested block): the fields the loaders read."""

    def __init__(self, raw: bytes, label: str):
        self.raw, self.label = raw, label
        self.offset, self.size = u32(raw, 4), u32(raw, 8)
        self.first, self.count = struct.unpack_from('<2H', raw, 0x0C)
        self.b_count, self.resident = u32(raw, 0x10), u32(raw, 0x14)
        self.files = u32(raw, 0x1C)

    def entry(self, k: int):
        return struct.unpack_from('<2I', self.raw, 0x20 + 8 * k)

    def slot_entries(self, table_index: int):
        """[(slot, resident offset)] of the pointer table starting at
        descriptor entry `table_index` (u24 offset, u8 slot per word)."""
        out = []
        for i in range(self.files):
            w = u32(self.raw, 0x20 + 8 * table_index + 4 * i)
            out.append((w >> 24, w & 0xFFFFFF))
        return out


def top_block(disc: Disc, sector: int) -> Block:
    return Block(disc.descriptor(sector)[:0x100], f'sector {sector}')


def nested_block(disc: Disc, sector: int, sub: int) -> Block:
    raw = disc.descriptor(sector)
    return Block(raw[0x100 + 0x70 * sub:0x100 + 0x70 * (sub + 1)], f'sector {sector} nested {sub}')


# Each loader's rule for which descriptor entries are upload sections
# (docs/DISC_TEXTURES.md section 2). A section is (label, region offset,
# size); the bytes are DATA.DAT[block offset + region offset, + size).

def module_sections(block: Block) -> list:
    """001FF1E0 / 001FF830 (+ its 001FF3F0): the A sections are entries
    0 .. hdr[0x0E] - 1 (read into one buffer each, uploaded, discarded);
    the B sections are the next hdr[0x10] entries' sizes, consecutive from
    the start of the resident region (region offset hdr[0x14])."""
    out = []
    for k in range(block.count):
        off, size = block.entry(k)
        out.append((f'{block.label} A{k}', off, size))
    pos = block.resident
    for i in range(block.b_count):
        size = block.entry(block.count + i)[1]
        out.append((f'{block.label} B{i}', pos, size))
        pos += size
    return out


def area_sections(block: Block) -> list:
    """001FFCD0: state 4's 001FF590(tag, 1) uploads entries hdr[0x0C] ..
    hdr[0x0C] + hdr[0x0E] - 1 (entry 0 is the sound bank that
    001FF590(tag, 0) hands to 001FB370); state 7 / 11 upload the next
    hdr[0x10] entries' sizes consecutively from the resident region."""
    out = []
    for k in range(block.first, block.first + block.count):
        off, size = block.entry(k)
        out.append((f'{block.label} A{k}', off, size))
    pos = block.resident
    for i in range(block.b_count):
        size = block.entry(block.first + block.count + i)[1]
        out.append((f'{block.label} B{i}', pos, size))
        pos += size
    return out


def module_slot(disc: Disc, block: Block, slot: int, table_index: int):
    """(label, region offset, size) of resource `slot` of a module whose
    resident region the loader relocates (D_0028A490[slot] = base + the
    entry's offset; base holds region offset hdr[0x14]). The size runs to
    the next greater entry offset or the end of the resident region (the
    loaders keep no sizes: the extent is only the reader's bound; the test
    checks it against the original's relocated pointers and cursors)."""
    entries = block.slot_entries(table_index)
    for s, off in entries:
        if s == slot:
            end = min((o for _s, o in entries if o > off), default=block.size - block.resident)
            return (f'{block.label} slot {slot:#x}', block.resident + off, end - off)
    raise SystemExit(f'{block.label}: no resource slot {slot:#x}')


def module_table_index(block: Block, loader: str) -> int:
    """The descriptor entry the pointer table starts at: after the A and B
    entries (001FF1E0 / 001FF830: hdr[0x0E] + hdr[0x10]; 001FFCD0:
    hdr[0x0C] + hdr[0x0E] + hdr[0x10])."""
    if loader == 'area':
        return block.first + block.count + block.b_count
    return block.count + block.b_count


def player_texture_slot(mode: int, costume: int) -> int:
    """00200890: the player texture packet D_0028A490[slot] it uploads,
    by D_00810707 (mode) and D_00810C60 (costume)."""
    mode &= 0xFF
    costume &= 0xFF
    if mode == 0:
        return 0x0A if costume == 2 else 0x0B if costume == 1 else 0x08
    if mode == 1:
        return 0x0A if costume == 2 else 0x0B if costume == 1 else 0x0C
    return 0x09


# ---------------------------------------------------------------------------
# The GS image
# ---------------------------------------------------------------------------

class GSImage:
    """4 MB of GS local memory with the uploads replayed so far."""

    def __init__(self):
        self.lm = bytearray(LOCALMEM)
        self.covered = set()          # 256-byte GS blocks a replay wrote
        self.steps = []               # (caller, source label, [transfers])

    def copy(self) -> 'GSImage':
        other = GSImage()
        other.lm = bytearray(self.lm)
        other.covered = set(self.covered)
        other.steps = list(self.steps)
        return other

    def upload(self, caller: str, label: str, buf: bytes):
        """00200830(buf): the VIF1 chain at the start of `buf`."""
        el = export_level()
        log = []
        el._bg_gs_upload(el._bg_section_chain(buf), self.lm, log)
        for dbp, dbw, dx, dy, w, h in log:
            if dx or dy or w % 64 or h % 32 or dbw != w // 64:
                raise SystemExit(f'{label}: transfer {dbp:#x} {w}x{h}+{dx}+{dy} dbw {dbw}: '
                                 'not a whole-page sheet (coverage is not modelled)')
            self.covered.update(range(dbp, dbp + (w // 64) * (h // 32) * 32))
        self.steps.append((caller, label, log))
        return log

    def freeze(self) -> bytes:
        """The image in the freeze-blob layout gs_vram.read_localmem reads
        (zero header and trailer: only the local memory is meaningful)."""
        return bytes(FREEZE_HEAD) + bytes(self.lm) + bytes(FREEZE_TAIL)


class FirstLevel:
    """The upload sequence of docs/DISC_TEXTURES.md section 2 over the
    user's disc, with every uploaded buffer checked against the extract."""

    def __init__(self, disc: Disc, extract: Path = EXTRACT, check_extract: bool = True):
        self.disc, self.extract, self.check = disc, Path(extract), check_extract
        self.sources = []             # (caller, label, DATA.DAT offset, size, extract file(s))

    # -- the disc bytes of a section and where they sit in the extract -----
    def _bytes(self, caller: str, block: Block, chunk: str, label: str, off: int, size: int) -> bytes:
        data = self.disc.read(block.offset + off, size)
        where = self._extract_span(chunk, off, size, data) if self.check else '(not checked)'
        self.sources.append((caller, label, block.offset + off, size, where))
        return data

    def _extract_span(self, chunk: str, off: int, size: int, data: bytes) -> str:
        """The extract files of `chunk` tile its region in file order
        (tools/extract_data.py); the section is the slice [off, off + size)
        of their concatenation. Returns the file names it spans."""
        directory = self.extract / chunk
        files = sorted(directory.glob('f*_id*.bin'), key=lambda p: int(p.name[1:3]))
        if not files:
            raise SystemExit(f'{directory}: no extracted files')
        pos, names, got = 0, [], bytearray()
        for p in files:
            n = p.stat().st_size
            if pos + n > off and pos < off + size:
                b = p.read_bytes()
                lo, hi = max(off, pos) - pos, min(off + size, pos + n) - pos
                got += b[lo:hi]
                names.append(f'{chunk}/{p.name}' + ('' if (lo, hi) == (0, n) else f'[{lo:#x}:{hi:#x}]'))
            pos += n
        if bytes(got) != data:
            raise SystemExit(f'{chunk} [{off:#x}, +{size:#x}): the extract differs from DATA.DAT')
        return ' + '.join(names)

    # -- the steps ----------------------------------------------------------
    def library(self, gs: GSImage, caller='001AB7E0 step 3: 001FF1E0(0x1B)'):
        block = top_block(self.disc, LIBRARY_MODULE)
        for label, off, size in module_sections(block):
            gs.upload(caller, label, self._bytes(caller, block, f'chunk{LIBRARY_MODULE:02d}', label, off, size))

    def library_slot(self, gs: GSImage, caller: str):
        block = top_block(self.disc, LIBRARY_MODULE)
        label, off, size = module_slot(self.disc, block, LIBRARY_SLOT,
                                       module_table_index(block, 'module'))
        gs.upload(caller, label, self._bytes(caller, block, f'chunk{LIBRARY_MODULE:02d}', label, off, size))

    def player_texture(self, gs: GSImage, mode: int, costume: int, caller: str):
        block = top_block(self.disc, PLAYER_TEXTURE_MODULE)
        slot = player_texture_slot(mode, costume)
        label, off, size = module_slot(self.disc, block, slot, module_table_index(block, 'module'))
        gs.upload(caller, label, self._bytes(caller, block, f'chunk{PLAYER_TEXTURE_MODULE:02d}', label, off, size))

    def area(self, gs: GSImage, area: int, sub: int, mode: int, costume: int):
        sector = area + 4
        top = top_block(self.disc, sector)
        blocks = [(top, f'chunk{sector:02d}')]
        if u32(self.disc.descriptor(sector), 0x18):
            blocks.append((nested_block(self.disc, sector, sub), f'chunk{sector:02d}.n{sub}'))
        for block, chunk in blocks:
            for label, off, size in area_sections(block):
                caller = '001FFCD0 state 4: 001FF590(tag, 1)' if ' A' in label else '001FFCD0 state 7/11'
                gs.upload(caller, label, self._bytes(caller, block, chunk, label, off, size))
            if block is top:
                self.player_texture(gs, mode, costume, '001FFCD0 state 7: 00200890')

    def world(self, area: int = FIRST_LEVEL_AREA, sub: int = 0, mode: int = 0, costume: int = 0) -> GSImage:
        """GS memory in the area after a New Game (no page open)."""
        gs = GSImage()
        self.library(gs)
        self.library_slot(gs, '001AD1A0: 00200830(D_0028A564)')
        self.area(gs, area, sub, mode, costume)
        return gs

    def page(self, gs: GSImage, module: int) -> GSImage:
        """001FF830(module): 001FF3F0's A sections, then state 7's B."""
        out = gs.copy()
        block = top_block(self.disc, module)
        for label, off, size in module_sections(block):
            caller = f'001FF830({module:#x}) ' + ('001FF3F0' if ' A' in label else 'state 7')
            out.upload(caller, label, self._bytes(caller, block, f'chunk{module:02d}', label, off, size))
        return out

    def page_close(self, gs: GSImage, mode: int = 0, costume: int = 0) -> GSImage:
        """0020CDC0's 00200970(1): slot 0x35, then 00200890."""
        out = gs.copy()
        self.library_slot(out, '00200970(1): 00200830(D_0028A564)')
        self.player_texture(out, mode, costume, '00200970(1): 00200890')
        return out


# ---------------------------------------------------------------------------
# Residency: does a decode read only replayed blocks?
# ---------------------------------------------------------------------------

def reads_only_covered(gs: GSImage, decode) -> bool:
    """decode(localmem) -> bytes. True when the result is the same with every
    block no replay wrote filled with 0x00 and with 0xA5 (so the texture's
    texels and CLUT lie wholly in uploaded blocks). No block table is
    assumed: the decoder itself shows which blocks it reads."""
    fills = []
    for fill in (0x00, 0xA5):
        lm = bytearray(gs.lm)
        blank = bytes([fill]) * 256
        for b in range(LOCALMEM // 256):
            if b not in gs.covered:
                lm[b * 256:b * 256 + 256] = blank
        fills.append(decode(bytes(lm)))
    return fills[0] == fills[1] == decode(bytes(gs.lm))
