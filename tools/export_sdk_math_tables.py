#!/usr/bin/env python3
"""Export the SDK float-math tables (D_0026C170..D_0026C658) from the user's own ELF.

The boot ELF's sin/cos/tan/atan/sqrt (0011E2A8 and its callees, translated in
src/game/em_sdk_math_original.c, docs/SDK_MATH_ORIGINAL.md) read their
constants from this .data window: D_0026C170 (-0.0f), the ipio2 table
D_0026C178, npio2_hw D_0026C490, init_jk D_0026C538, PIo2 D_0026C548, the
tangent T D_0026C598, the atan hi/lo/aT D_0026C5D8/5E8/5F8 and the double
D_0026C650. The asset holds exactly that window, address-mapped, so the
port's loader rebuilds the same EmSdkMathTables that
em_sdk_math_original_load_tables reads from the ELF.

Output: assets/sdk_math_tables.emsm (ignored):
  'EMSM', u32 version 1, u32 base address 0x0026C170, u32 size 0x4E8,
  then the window's bytes.
The live reader is src/game/em_status_background_draw.c (the 0020A7A0 sine).
Only the address range and size are printed.
"""
import argparse
import hashlib
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
ELF_VADDR, ELF_OFFSET = 0x100000, 0x300
BASE, END = 0x26C170, 0x26C658
VERSION = 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--decomp', type=Path, default=ROOT.parent / 'Extermination')
    parser.add_argument('--out', type=Path, default=ROOT / 'assets/sdk_math_tables.emsm')
    args = parser.parse_args()
    elf = (args.decomp / 'config/SCUS_971.12').read_bytes()
    if hashlib.sha256(elf).hexdigest() != ELF_SHA256:
        raise SystemExit('unexpected boot ELF (SCUS-97112 VER 1.00 is the pinned target)')
    start = BASE - ELF_VADDR + ELF_OFFSET
    window = elf[start:start + (END - BASE)]
    assert len(window) == END - BASE
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(struct.pack('<4s3I', b'EMSM', VERSION, BASE, END - BASE) + window)
    print(f'sdk math tables: 0x{BASE:08X}..0x{END:08X} ({END - BASE} bytes) -> {args.out}')


if __name__ == '__main__':
    main()
