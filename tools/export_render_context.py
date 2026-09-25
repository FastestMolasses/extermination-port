#!/usr/bin/env python3
"""export_render_context.py - the boot ELF data the live render context reads.

The one canonical render context (src/game/em_render_context_live.c,
docs/RENDER_CONTEXT.md section 8) addresses original memory. Its storage is
zero at start, like the original .bss, except for the .data words the bound
routines read before any of them writes them. This tool copies those from
the user's own pinned boot ELF (config/SCUS_971.12, file offset = address -
0x100000 + 0x300) into an ignored asset; nothing is embedded here.

Blocks (original address, bytes, what reads them):
  0x00241010   8       00100610's GS parameter words, read by 001D6E60
                       (inside 001D6B10's 001D6930, 001DDE10)
  0x00250F30   0x2240  D_00250F30 (001C1F50's 001E2270 colour), D_002513E0
                       (001D30A0 stores its K copy there) and the room table
                       D_00251C50 (45 entries of 0x78 bytes: 001D7B30 /
                       001D8FD0 read the area fog)
  0x0026E510   16      the quadword 001D6B10 hands 001D6930 (001DDE10)
  0x0026E850   16      the three ramp colour words 001DEDE0 copies (boot)
  0x00275670   0x30    D_00275670 (the context address), D_00275674 (the
                       GS block address), D_00275688, D_0027568C, D_00275690
                       and D_00275694 (001DDE10's eases)

--verify-ram (default: the opening capture and every AREA11 route capture
00..14) checks that each block equals captured RAM, except the words the
game writes at run time: the GS revision halfword 0x241016 (00100158 at
boot; 001D6E60 masks it out), D_002513E0 (001D30A0), D_00275688 (001D88B0)
and D_00275690 / D_00275694 (001DDE10).

Output (disc-derived: git-ignored assets/ only):
  assets/render_context.emrc, little-endian:
    0x00 'EMRC', u32 version 1, u32 block count, u32 0
    0x10 count x (u32 address, u32 size, u32 file offset, u32 0)
    then the block bytes at their offsets (16-aligned)

Runs natively on arm64 macOS (pure Python).

Usage (port root):
  python3 tools/export_render_context.py
"""
from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
BLOCKS = ((0x00241010, 0x8), (0x00250F30, 0x2240), (0x0026E510, 0x10), (0x0026E850, 0x10),
          (0x00275670, 0x30))
# Words the game writes at run time (address, bytes): not compared.
# 0x241016: 00100158 (the boot GS reset) stores the GS revision read from
# CSR there; 001D6E60 masks it out (D_00241010 & 0x0000FFFF0000FFFF).
RUNTIME = ((0x00241016, 2), (0x002513E0, 0x40), (0x00275688, 4), (0x00275690, 8))


def elf_block(elf: bytes, address: int, size: int) -> bytes:
    offset = address - 0x100000 + 0x300
    if offset < 0x300 or offset + size > 0x175E00:
        raise SystemExit(f'{address:#x}: outside the loadable section')
    return elf[offset:offset + size]


def serialize(blocks) -> bytes:
    head = struct.pack('<4s3I', b'EMRC', 1, len(blocks), 0)
    table, body = b'', b''
    offset = 0x10 + 0x10 * len(blocks)
    for address, data in blocks:
        at = offset + len(body)
        table += struct.pack('<4I', address, len(data), at, 0)
        body += data + bytes(-len(data) % 16)
    return head + table + body


def verify(blocks, ram: bytes) -> int:
    compared = 0
    for address, data in blocks:
        for i, b in enumerate(data):
            a = address + i
            if any(r <= a < r + n for r, n in RUNTIME):
                continue
            if ram[a] != b:
                raise SystemExit(f'{a:#x}: ELF {b:#04x}, captured {ram[a]:#04x}')
            compared += 1
    return compared


def default_captures():
    ref = DECOMP / 'build/startup-reference/opening_ee.bin'
    route = DECOMP / 'build/s87/route'
    return ([ref] if ref.exists() else []) + sorted(
        p for p in route.glob('*/eeMemory.bin') if not p.parent.name.startswith('15'))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--elf', type=Path, default=DECOMP / 'config/SCUS_971.12')
    ap.add_argument('--out', type=Path, default=ROOT / 'assets/render_context.emrc')
    ap.add_argument('--verify-ram', type=Path, action='append', default=None)
    ap.add_argument('--no-verify', action='store_true')
    args = ap.parse_args(argv)
    elf = args.elf.read_bytes()
    if hashlib.sha256(elf).hexdigest() != ELF_SHA256:
        raise SystemExit(f'{args.elf}: not the pinned SCUS-97112 boot ELF')
    blocks = [(a, elf_block(elf, a, n)) for a, n in BLOCKS]
    captures = [] if args.no_verify else (args.verify_ram or default_captures())
    compared = sum(verify(blocks, p.read_bytes()) for p in captures)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(serialize(blocks))
    print(f'wrote {args.out}: {len(blocks)} blocks, {sum(len(d) for _, d in blocks)} bytes; '
          f'{compared} bytes equal in {len(captures)} captures (run-time words excepted)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
