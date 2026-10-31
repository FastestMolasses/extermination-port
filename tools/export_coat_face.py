#!/usr/bin/env python3
"""Add the player's coat face to the retained resources (lane M1).

001B81D0 picks D_0028A490 row 0x19 while the player's +0x2FF is 0x3E, the
coat (em_area_script.c face_attach): extract/chunk03/f17_id19.bin. A script
that holds the player's face in the coat (AREA13's [44], script 0x82B090) reads
it by address through 001CB3C0. The export's own table word says where it
lies. Run after export_later_library.py; the region is added once.

Output: assets/scene_snow/roger/resources.emrs, disc-derived and ignored.
Usage (port root; native arm64 macOS): python3 tools/export_coat_face.py
"""
import struct
from pathlib import Path

import export_area00_library as L
import export_area02_library as L2
import export_roger_banks as B

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'assets/scene_snow/roger/resources.emrs'
ROW = 0x19


def main():
    if any(p.is_symlink() for p in (OUT, *OUT.parents)):
        raise SystemExit(f'{OUT} is a symlink: unlink it first')
    data = OUT.read_bytes()
    header, regions = L.parse(data)
    address = struct.unpack_from('<I', header, 32 + 4 * ROW)[0]
    face = (B.DECOMP / 'extract/chunk03/f17_id19.bin').read_bytes()
    assert len(face) == 0x4C800, len(face)
    if any(a <= address and address + len(face) <= a + len(r) for a, _w, _f, r in regions):
        print('coat face: already present')
        return 0
    assert not any(a < address + len(face) and address < a + len(r) for a, _w, _f, r in regions)
    # row 0x18's region ends where this one starts: one region (the pose
    # host's table holds twelve, EM_POSE_REGION_MAX)
    for i, (a, w, f, r) in enumerate(regions):
        if a + len(r) == address and (w, f) == (0, 0):
            regions[i] = (a, 0, 0, r + face)
            break
    else:
        regions.append((address, 0, 0, face))
    if len(regions) > 16:
        raise SystemExit(f'{len(regions)} regions: the provider holds 16')
    OUT.write_bytes(L2.rebuild(header[:16] + len(regions).to_bytes(4, 'little') + header[20:], regions))
    print(f'coat face: {len(face):#x} bytes at {address:#x}, {len(regions)} regions')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
