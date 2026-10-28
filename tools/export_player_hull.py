#!/usr/bin/env python3
"""export_player_hull.py - the player's hull chain (D_0028A578[0], resource 0x3A).

0015C420 stores D_0028A578[0] at the player record's +0x58 (0xD1B9C0, the
resource table D_0028A490's entry 0x3A). 001A7280, the lock 0019AA80 runs
against the player record (the spat hazard 001E2E80 of AREA00's creatures),
walks that chain: a count word, then records { mask bytes x/y/z, bone slot,
u16 stride, s16 vertex count, normal, vertices, edge normals }
(em_coll_grid_hull.h).

Source (the user's own extracted disc; nothing is embedded here): the
resource file extract/chunk27/f03_id3a.bin (resource 0x3A), 0x800 bytes.

Checks (every run), per captured EE RAM image the developer has (default:
the decomp's AREA00 route captures):
  * D_0028A490[0x3A] == 0xD1B9C0 and the player's +0x58 == 0xD1B9C0;
  * the exported bytes equal RAM at that address, byte for byte.
The captures are only checked against, never exported.

Output (disc-derived: the worktree's private, git-ignored assets/ only):
  assets/player_hull.emom, little-endian:
    0x00 'EMOM', u32 version 1, u32 address, u32 size S, 4 x u32 0
    0x20 S original bytes

Runs natively on arm64 macOS (pure Python).

Usage (port root):
  python3 tools/export_player_hull.py
"""
from __future__ import annotations

import argparse
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
TABLE, INDEX, ADDRESS, SIZE = 0x0028A490, 0x3A, 0x00D1B9C0, 0x800
PLAYER = 0x008102B0


def verify(data, paths):
    count = 0
    for path in paths:
        ram = path.read_bytes()
        if struct.unpack_from('<I', ram, TABLE + 4 * INDEX)[0] != ADDRESS:
            raise ValueError(f'D_0028A490[0x3A] is not 0xD1B9C0 in {path}')
        if struct.unpack_from('<I', ram, PLAYER + 0x58)[0] != ADDRESS:
            raise ValueError(f'the player +0x58 is not 0xD1B9C0 in {path}')
        if ram[ADDRESS:ADDRESS + SIZE] != data:
            raise ValueError(f'the hull chain differs from RAM in {path}')
        count += 1
    if not count:
        raise ValueError('no capture to verify against')
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--source', type=Path, default=DECOMP / 'extract/chunk27/f03_id3a.bin')
    parser.add_argument('--verify-ram', type=Path, action='append')
    parser.add_argument('--out', type=Path, default=ROOT / 'assets/player_hull.emom')
    args = parser.parse_args()
    data = args.source.read_bytes()
    if len(data) != SIZE:
        raise ValueError(f'{args.source}: {len(data)} bytes, expected {SIZE}')
    count = struct.unpack_from('<I', data, 0)[0]
    if not 0 < count < 64:
        raise ValueError(f'implausible chain count {count}')
    paths = args.verify_ram or sorted((DECOMP / 'build/fork_refs/s87/route_a00').glob('*/eeMemory.bin'))
    checked = verify(data, paths)
    output = args.out.absolute()
    if output.is_symlink() or any(parent.is_symlink() for parent in output.parents):
        raise ValueError('Output must use the worktree\'s private directories')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(struct.pack('<4s3I4I', b'EMOM', 1, ADDRESS, SIZE, 0, 0, 0, 0) + data)
    print(f'Exported the player hull chain: {count} records, {SIZE} bytes at {ADDRESS:#x}; '
          f'{checked} captures match')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
