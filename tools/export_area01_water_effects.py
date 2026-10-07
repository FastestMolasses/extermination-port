#!/usr/bin/env python3
"""Export existing effect windows plus AREA01 bullet-impact sources.

Original 001EB7F0 and 001ECB00 each read two 0x90-byte descriptors. The protected
base exporter stays unchanged. Only ignored local assets are produced; a
worktree symlink is replaced, never followed for writing.
"""
import argparse
import hashlib
from pathlib import Path

from export_effect_tables import BLOCKS, DECOMP, ELF_SHA256, ROOT, elf_block, serialize, verify

EXTRA = ((0x00255E90, 0x120), (0x00256EE0, 0x120))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, default=DECOMP/'config/SCUS_971.12')
    parser.add_argument('--out', type=Path, default=ROOT/'assets/effect_tables.emet')
    args = parser.parse_args()
    source = args.elf.read_bytes()
    if hashlib.sha256(source).hexdigest() != ELF_SHA256:
        raise SystemExit('not the pinned SCUS-97112 boot ELF')
    blocks = [(a, elf_block(source, a, n)) for a, n in (*BLOCKS, *EXTRA)]
    captures = sorted((DECOMP/'build/s87/route_a01').glob('*/eeMemory.bin'))
    if not captures:
        raise SystemExit('AREA01 reference captures are required')
    # Several base windows contain writable effect tables after startup.
    # Their initialization still comes from the pinned ELF; compare only
    # this extension's immutable source descriptors to every AREA01 beat.
    extra = [(a, elf_block(source, a, n)) for a, n in EXTRA]
    compared = sum(verify(extra, capture.read_bytes(), capture) for capture in captures)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    if args.out.is_symlink():
        args.out.unlink()
    args.out.write_bytes(serialize(blocks))
    print(f'wrote {args.out}: {len(blocks)} windows; {compared} new equal bytes in {len(captures)} captures')


if __name__ == '__main__':
    main()
