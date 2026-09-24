#!/usr/bin/env python3
"""Export the SDK float-math tables (D_0026C170..D_0026C658) and the soft-float
errno data (D_0024295C and the word it points at) from the user's own ELF.

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
The live readers are src/game/em_status_background_draw.c (the 0020A7A0 sine)
and src/game/em_collision_world.c (the collision world's SDK context).

Second output: assets/sdk_soft_float.emsf (ignored), the .data the soft-float
worker 0011FD78 serves to the atan2f/sqrtf wrappers' domain-error tails
(src/game/em_sdk_soft_float.c, docs/SDK_SOFT_FLOAT.md):
  'EMSF', u32 version 1, then two (u32 address, u32 word) pairs:
  D_0024295C and its initial word (the errno cell pointer), then the cell
  that word names and the cell's initial word.
The live reader is src/game/em_collision_world.c
(em_sdk_soft_float_load_export). tools/test_sdk_soft_float_reference.py
checks the file against the ELF and the captured RAM.
Only address ranges, sizes and the pointer are printed.
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
ELF_FILESZ = 0x175B00                 # the LOAD segment's file size (CLAUDE.md target identity)
D_ERRNO_PTR = 0x24295C                # read by 0011FD78 on every call
SOFT_VERSION = 1


def elf_word(elf, address):
    assert ELF_VADDR <= address and address + 4 <= ELF_VADDR + ELF_FILESZ, hex(address)
    return struct.unpack_from('<I', elf, address - ELF_VADDR + ELF_OFFSET)[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--decomp', type=Path, default=ROOT.parent / 'Extermination')
    parser.add_argument('--out', type=Path, default=ROOT / 'assets/sdk_math_tables.emsm')
    parser.add_argument('--soft-out', type=Path, default=ROOT / 'assets/sdk_soft_float.emsf')
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
    # D_0024295C and the errno cell it names; both are initialised .data
    # inside the LOAD segment's file image.
    pointer = elf_word(elf, D_ERRNO_PTR)
    cell = elf_word(elf, pointer)
    args.soft_out.parent.mkdir(parents=True, exist_ok=True)
    args.soft_out.write_bytes(struct.pack('<4s5I', b'EMSF', SOFT_VERSION, D_ERRNO_PTR, pointer, pointer, cell))
    print(f'sdk soft float: D_{D_ERRNO_PTR:08X} -> errno cell 0x{pointer:08X} -> {args.soft_out}')


if __name__ == '__main__':
    main()
