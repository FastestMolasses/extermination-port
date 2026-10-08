#!/usr/bin/env python3
"""export_gs_memory.py - the GS local memory of the library upload, from the
user's own disc, for the Original profile's GS frame (src/gs/em_gs_world.h,
docs/GS_EXACT.md section 9).

The CPU GS model draws the world frame in GS local memory, so it samples the
textures where the original's uploads put them (docs/DISC_TEXTURES.md
section 2). The area load's uploads (its A sections at 001FFCD0 state 4 and
the player texture packet 00200890 at state 7) reach the model at run time,
through the loader's area consumer, from the bytes the port's loader
delivers. The library module 0x1B is uploaded by the boot (001AB7E0 step 3,
001FF1E0(0x1B)) and again by the New Game's 001AD1A0
(00200830(D_0028A564)), neither of which the port runs: this image is that
library, replayed by tools/export_disc_textures_gs.py (FirstLevel.library()
then library_slot()), the same bytes every texture exporter decodes and test
B of tools/test_disc_textures_reference.py proves equal to the route
captures' uploaded blocks.

Output (disc-derived: git-ignored assets/ only):
  assets/gs_library.emgm

EMGM v1 (little endian):
  +0x00  'EMGM'
  +0x04  u32 version (1)
  +0x08  u32 run count
  +0x0C  u32 0
  then per run of consecutive uploaded 256-byte GS blocks:
         u32 first block, u32 block count, count * 256 bytes of local memory
Blocks no upload wrote are not in the file (the runtime refuses a texture
that reads one).

Usage (port root; macOS arm64, pure Python, about 1 s):
  python3 tools/export_gs_memory.py [--iso FILE | --disc DIR] [--extract DIR] [--out FILE]
"""
from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import export_disc_textures_gs as G  # noqa: E402

OUT = ROOT / 'assets' / 'gs_library.emgm'
MAGIC, VERSION = b'EMGM', 1


def runs(covered):
    blocks = sorted(covered)
    out = []
    for b in blocks:
        if out and out[-1][0] + out[-1][1] == b:
            out[-1][1] += 1
        else:
            out.append([b, 1])
    return out


def emgm(world: G.GSImage) -> bytes:
    rs = runs(world.covered)
    parts = [MAGIC, struct.pack('<III', VERSION, len(rs), 0)]
    for first, count in rs:
        parts.append(struct.pack('<II', first, count))
        parts.append(bytes(world.lm[first * 256:(first + count) * 256]))
    return b''.join(parts)


def library(fl: G.FirstLevel) -> G.GSImage:
    """The boot's library upload, then 001AD1A0's slot 0x35 (the same
    sheet again)."""
    gs = G.GSImage()
    fl.library(gs)
    fl.library_slot(gs, '001AD1A0: 00200830(D_0028A564)')
    return gs


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--iso', type=Path, default=None)
    ap.add_argument('--disc', type=Path, default=None)
    ap.add_argument('--extract', type=Path, default=G.EXTRACT)
    ap.add_argument('--out', type=Path, default=OUT)
    a = ap.parse_args()
    fl = G.FirstLevel(G.Disc(a.iso, a.disc), a.extract)
    world = library(fl)
    data = emgm(world)
    a.out.parent.mkdir(parents=True, exist_ok=True)
    staged = a.out.with_name(a.out.name + '.tmp')
    staged.write_bytes(data)
    staged.replace(a.out)
    print(f'gs memory: {len(world.covered)} blocks in {len(runs(world.covered))} runs -> {a.out} '
          f'({len(data)} bytes, sha256 {hashlib.sha256(data).hexdigest()[:16]})')


if __name__ == '__main__':
    main()
