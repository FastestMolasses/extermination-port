#!/usr/bin/env python3
"""Extend the retained library export with the first boss fight's models.

Run export_area04_library.py first. The SHOTGUN UNIT's shell casing
(001F4010 kind 7, drawn by 001F3E30 with library model 0x1A) and the
creature's death pieces (001F4840's 001F3E30 calls, models 0x2A..0x2E) lie
just after retained spans; each model below is delivered by growing the retained
read-only span that ends before it (the region count, the provider's limit
of 16, is unchanged), with the file's bytes up to the model's end. The span's
address stays and every earlier byte stays.

Output is private disc-derived data (assets/scene_snow/roger/resources.emrs),
never repository content. Runs natively on arm64 macOS (pure Python).
"""
import argparse
from pathlib import Path

import export_area00_library as L
import export_area02_library as E2
import export_roger_banks as B

ROOT = Path(__file__).resolve().parents[1]
MODELS = (0x1A, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E)
MAX_GAP = 0x1000


def extend(previous, library):
    header, regions = L.parse(previous)
    base = B.u32(header, 32 + 4 * B.GLOBAL_TABLE_INDEX)
    for model in MODELS:
        offset = B.u32(library, 4 + 4 * model) & ~3
        end = offset + B.block_model_size(library, offset)
        address = base + offset
        if any(a <= address and base + end <= a + len(raw) for a, _, _, raw in regions):
            continue
        before = [i for i, r in enumerate(regions) if r[0] < address <= r[0] + len(r[3]) + MAX_GAP and not r[1]]
        if len(before) != 1:
            raise ValueError(f'no retained span ends before model {model:#x}')
        i = before[0]
        a, writable, retained, old = regions[i]
        start = a - base
        if old != library[start:start + len(old)]:
            raise ValueError('the retained span differs from the library file')
        regions[i] = (a, writable, retained, library[start:end])
    return E2.rebuild(header, regions)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--out', type=Path, default=ROOT / 'assets/scene_snow/roger/resources.emrs')
    output = parser.parse_args().out.absolute()
    previous = output.read_bytes()
    if output.is_symlink():
        output.unlink()
    library = (B.DECOMP / 'extract/chunk27/f01_id37.bin').read_bytes()
    result = extend(previous, library)
    output.write_bytes(result)
    print(f'boss library: {len(result) - len(previous)} bytes added')


if __name__ == '__main__':
    main()
