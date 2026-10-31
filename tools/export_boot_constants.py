#!/usr/bin/env python3
"""Export the boot data every AREA19 recording holds unchanged from the ELF.

The later levels' owners read many small boot tables (rows of per-area
registries, records of the kind tables). tools/export_area00_runtime_tables.py
lists a window for each table an owner was found to read, capped at 128 by
EMET. This export is the area's fallback behind those windows: the runs of at
least MIN_RUN bytes of the boot image's data that equal the ELF in every fork
snapshot of the area's route groups, so a read outside them still faults
(em_boot_constants.c). A byte that any recorded state changed is left out.

Output: assets/boot_constants.embc, disc-derived and ignored:
  'EMBC', u32 version 1, u32 runs, u32 0, runs x {u32 address, u32 size}, bytes.

Usage (port root; native arm64 macOS; about 2 s): python3 tools/export_boot_constants.py
"""
import hashlib
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
LO, HI = 0x23D000, 0x275B00            # the end of the text to the file's end
MIN_RUN = 16
GROUPS = ('levels-recordings-20261018-10/s87/route_a19', 'levels-recordings-20261018-13/s87/route_a19b',
          'levels-recordings-20261018-14/s87/route_a19c', 'levels-recordings-20261018-17/s87/route_a19c',
          'levels-recordings-20261018-19/s87/route_a19d',
          # AREA15 and AREA03 (lane K1)
          'levels-recordings-20261018-19/s87/route_a15', 'levels-recordings-20261018-19/s87/route_a15b',
          'levels-recordings-20261018-25/s87/route_a15b', 'levels-recordings-20261018-26/s87/route_a03',
          # AREA08 and AREA07 (lane P1)
          'levels-recordings-20261030-1/s87/route_a08', 'levels-recordings-20261030-1/s87/route_a07',
          # AREA03 subs 1 and 2, AREA04 sub 1 (lane N1)
          'levels-recordings-20261030-1/s87/route_a03b', 'levels-recordings-20261030-1/s87/route_a03c',
          'levels-recordings-20261030-1/s87/route_a04c', 'levels-recordings-20261031-1/s87/route_a03d',
          'levels-recordings-20261031-1/s87/route_a04d')


def main():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA256
    base = elf[LO - 0x100000 + 0x300:HI - 0x100000 + 0x300]
    paths = [p for g in GROUPS for p in sorted((DECOMP / 'build/fork_refs' / g).glob('*/eeMemory.bin'))]
    assert len(paths) >= 35, len(paths)
    same = int.from_bytes(b'\xFF' * len(base), 'little')
    want = int.from_bytes(base, 'little')
    for p in paths:
        with p.open('rb') as f:
            f.seek(LO)
            got = int.from_bytes(f.read(HI - LO), 'little')
        x = got ^ want                 # a byte with any bit set differs
        for shift in (1, 2, 4):
            x |= x >> shift
        mask = int.from_bytes(b'\x01' * len(base), 'little')
        x &= mask
        same &= ~(x * 0xFF)
    flags = same.to_bytes(len(base), 'little')
    runs, i = [], 0
    while i < len(flags):
        if flags[i]:
            j = i
            while j < len(flags) and flags[j]:
                j += 1
            if j - i >= MIN_RUN:
                runs.append((i, j - i))
            i = j
        else:
            i += 1
    out = ROOT / 'assets/boot_constants.embc'
    if out.is_symlink():
        raise SystemExit(f'{out} is a symlink: unlink it first')
    blob = struct.pack('<4sIII', b'EMBC', 1, len(runs), 0)
    blob += b''.join(struct.pack('<II', LO + a, n) for a, n in runs)
    blob += b''.join(base[a:a + n] for a, n in runs)
    out.write_bytes(blob)
    print(f'boot constants: {len(runs)} runs, {sum(n for _, n in runs):,} of {len(base):,} bytes over '
          f'{len(paths)} snapshots -> {out.relative_to(ROOT)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
