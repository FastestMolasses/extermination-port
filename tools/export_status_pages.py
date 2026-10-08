#!/usr/bin/env python3
"""Export what the status pages (docs/STATUS_PAGES.md) read at run time
from the user's own ELF and disc, into ignored assets/status_pages/:

  status_pages.emsp
    - the original .data windows the page translations read by address
      (their tables, text styles and TEX0 rows), copied from the ELF;
    - the first level's GS local memory after a New Game in AREA11 (the
      upload sequence of docs/DISC_TEXTURES.md section 2, replayed from the
      disc by tools/export_disc_textures_gs.py), with the set of 256-byte
      blocks an upload wrote;
    - for every status page module and the options' screen modules 0x2A /
      0x2B (001FF830(module): 001FF3F0's A sections,
      then state 7's B sections) and for 0020CDC0's 00200970(1) restore
      (library slot 0x35, then the player texture packet 00200890), the GS
      blocks that step writes and their bytes. The port applies them in the
      order the original runs them, so the page textures are decoded from
      the GS memory the original would hold (em_gs_texture.c).

Every upload is a whole-page PSMCT32 sheet (GSImage.upload refuses any
other), so a step's blocks are fully determined by the step alone.

Runs natively on arm64 macOS (pure Python, about 10 s).

  python3 tools/export_status_pages.py [--iso FILE | --disc DIR] [--extract DIR] [--assets DIR]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import export_disc_textures_gs as G  # noqa: E402

# The .data windows (original addresses, [start, end)). D_002655A0..0x265600
# (0020A7A0's live background state, em_status_background's) is left out on
# purpose: no page reads it, and a stale ELF copy must never be read.
DATA_WINDOWS = [
    (0x00248A00, 0x00248A10),   # 0015C7C0's clip pairs D_00248A00..08 (HEALING's 0015C700)
    (0x002754C0, 0x002754E0),   # and D_002754C0..DA
    (0x00265500, 0x002655A0),   # text styles D_00265510 / 518 / 520 ...
    (0x00265600, 0x00266700),   # MAP rows, page frame and row tables, D_002659C0, part pages
    (0x0026A980, 0x0026AA00),   # 00210F30's marker corners D_0026A990 / 9A0 / 9B0
    (0x00275860, 0x00275880),   # 002134C0's text style word D_00275870
    # the options screen and its card screen (docs/OPTIONS.md)
    (0x00264CB0, 0x00264CC8),   # 001FC770's config D_00264CB0 for 001FCBD0 / 001FCE30
    (0x00264D30, 0x00264DB0),   # 001FCBD0's 128-byte line template D_00264D30
    (0x00264EE0, 0x00264FA4),   # 00202D10's icon rows and their pointers D_00264F98
    (0x002672C0, 0x00267308),   # the row colours D_002672C0 and the actions D_002672E0
    (0x0026C658, 0x0026C760),   # the ctype table 001FCBD0 reads (D_0026C659)
    (0x0026EC50, 0x0026EC60),   # 001FCBD0's token set D_0026EC50
    (0x00273320, 0x00273330),   # 00201F70's labels D_00273320 / 28
    (0x00275828, 0x00275830),   # D_00275828's boot value (the binder's storage starts from it)
]
# The boot bank module 0 (001FF1E0(0) loads its resident region at 0xB00000
# and sets D_0028A490[slot] = 0xB00000 + the slot's entry offset,
# docs/DISC_TEXTURES.md section 2): the DATABASE helper 001FCF30 reads the
# record container *D_0028A49C (slot 3) by address.
BOOT_MODULE, BOOT_BASE, RECORD_SLOT, HELP_SLOT = 0, 0x00B00000, 3, 2
# The page modules the first level can load (0020CDC0 phase 3 and the ITEM
# root / SPR4 child loads), docs/STATUS_PAGES.md section 1.
MODULES = [0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31,
           0x2A, 0x2B]   # the card screen's (00225AC0) and the options' row screens' (0022A590)
RESTORE = 0xFF          # the 00200970(1) step's id in the file
D_0028A490 = 0x0028A490 # the resource-slot word table 001FF830 state 7 fills
ELF_BASE, ELF_OFFSET = 0x00100000, 0x300


def step_blocks(before: G.GSImage, after: G.GSImage) -> list[int]:
    """The GS blocks the steps after `before` wrote (whole-page sheets)."""
    blocks = set()
    for _, _, log in after.steps[len(before.steps):]:
        for dbp, dbw, dx, dy, w, h in log:
            blocks.update(range(dbp, dbp + (w // 64) * (h // 32) * 32))
    return sorted(blocks)


def runs(blocks: list[int]):
    out, start, prev = [], None, None
    for b in blocks:
        if start is None:
            start = prev = b
        elif b == prev + 1:
            prev = b
        else:
            out.append((start, prev - start + 1))
            start = prev = b
    if start is not None:
        out.append((start, prev - start + 1))
    return out


def patch(image: G.GSImage, blocks: list[int]) -> bytes:
    out = bytearray()
    rs = runs(blocks)
    out += struct.pack('<I', len(rs))
    for first, count in rs:
        out += struct.pack('<II', first, count)
        out += image.lm[first * 256:(first + count) * 256]
    return bytes(out)


def build(disc: G.Disc, extract: Path, assets: Path) -> dict:
    elf = G.ELF_PATH.read_bytes()
    if hashlib.sha256(elf).hexdigest() != G.ELF_SHA256:
        raise SystemExit(f'{G.ELF_PATH}: not the pinned SCUS_971.12')
    fl = G.FirstLevel(disc, extract)
    world = fl.world()
    windows = [(start, elf[start - ELF_BASE + ELF_OFFSET:end - ELF_BASE + ELF_OFFSET])
               for start, end in DATA_WINDOWS]
    boot = G.top_block(disc, BOOT_MODULE)
    table = G.module_table_index(boot, 'module')
    offsets = dict(boot.slot_entries(table))
    label, region, size = G.module_slot(disc, boot, RECORD_SLOT, table)
    container = BOOT_BASE + offsets[RECORD_SLOT]
    windows.append((0x0028A49C, struct.pack('<I', container)))
    windows.append((container, disc.read(boot.offset + region, size)))
    # the help container *D_0028A498 (slot 2), whose groups 7 and 8 the
    # options screen and its card screen draw (001FCBD0 / 001FCE30)
    label, region, size = G.module_slot(disc, boot, HELP_SLOT, table)
    help_container = BOOT_BASE + offsets[HELP_SLOT]
    windows.append((0x0028A498, struct.pack('<I', help_container)))
    windows.append((help_container, disc.read(boot.offset + region, size)))
    body = bytearray()
    body += struct.pack('<I', len(windows))
    for start, data in windows:
        body += struct.pack('<II', start, len(data)) + data
    covered = bytearray(G.LOCALMEM // 256 // 8)
    for b in world.covered:
        covered[b >> 3] |= 1 << (b & 7)
    body += bytes(world.lm) + bytes(covered)
    steps = []
    for module in MODULES:
        after = fl.page(world, module)
        blocks = step_blocks(world, after)
        steps.append((module, patch(after, blocks), len(blocks)))
    after = fl.page_close(world)
    blocks = step_blocks(world, after)
    steps.append((RESTORE, patch(after, blocks), len(blocks)))
    body += struct.pack('<I', len(steps))
    for ident, data, _ in steps:
        body += struct.pack('<II', ident, len(data)) + data
    # 001FF830 state 0's default case: the destination is D_0028A748.
    import export_module_loader as eml
    destination = eml.AREA11_SEEDS[6]
    relocations = []
    for module in MODULES:
        block = G.top_block(disc, module)
        if block.files:
            for slot, offset in block.slot_entries(G.module_table_index(block, 'module')):
                relocations.append((module, D_0028A490 + 4 * slot, destination + offset))
    body += struct.pack('<I', len(relocations))
    for module, address, value in relocations:
        body += struct.pack('<III', module, address, value)
    header = struct.pack('<4sII', b'EMSP', 2, len(body))
    out = assets / 'status_pages'
    out.mkdir(parents=True, exist_ok=True)
    path = out / 'status_pages.emsp'
    path.write_bytes(header + bytes(body))
    report = {'elf_sha256': G.ELF_SHA256,
              'data_windows': [[hex(s), hex(s + len(d))] for s, d in windows],
              'world_blocks': len(world.covered),
              'steps': {('restore 00200970(1)' if i == RESTORE else hex(i)): n for i, _, n in steps},
              'relocations': [[hex(m), hex(a), hex(v)] for m, a, v in relocations],
              'buffers': [dict(caller=c, source=l, data_dat_offset=hex(o), size=hex(s))
                          for c, l, o, s, _ in fl.sources],
              'output': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
    (out / 'status_pages_source.json').write_text(json.dumps(report, indent=1) + '\n')
    return report


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--iso', type=Path, help='the disc image (default ../Extermination/Extermination-rebuilt.iso)')
    ap.add_argument('--disc', type=Path, help='a mounted disc or a copy of its DATA/ directory')
    ap.add_argument('--extract', type=Path, default=G.EXTRACT)
    ap.add_argument('--assets', type=Path, default=ROOT / 'assets')
    args = ap.parse_args(argv)
    report = build(G.Disc(args.iso, args.disc), args.extract, args.assets)
    print(json.dumps({'output': report['output'], 'sha256': report['sha256'][:16],
                      'world_blocks': report['world_blocks'], 'steps': report['steps']}))
    return 0


if __name__ == '__main__':
    sys.exit(main())
