#!/usr/bin/env python3
"""Export the area-script walk clip table D_0024D8F0 from the user's ELF.

001B94F0 (op01 of the area-script interpreter, kinds 3, 5 and 8: a scripted
walk of the player) stores D_0024D8F0[record +0x14] into the player's +0x1F2
when the walk starts and D_0024D8F0[0] when it ends (the decomp's
src/func_001B94F0.c). The table is nine halfwords of the ELF's data; the
port's interpreter (em_area_script.c op01) reads it through this export
(EMWC v1, little endian: 'EMWC', u32 1, u32 base 0x24D8F0, u32 count 9, then
the count halfwords; em_area11_script_host.c walk_clips).
Roger's departure script 0x828A10 walks the player with kind 3 (route beat
15, docs/FIRST_LEVEL_EXIT.md).

Input: the user's pinned boot ELF (../Extermination/config/SCUS_971.12,
SHA-256 checked). Output (ignored, disc-derived): assets/script_walk_clips.emwc.
Only the output's size and SHA-256 are printed.
"""
import hashlib
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
BASE, COUNT = 0x24D8F0, 9      # D_0024D8F0[0..8] (the extent the interpreter admits)


def main():
    elf = (ROOT.parent / 'Extermination/config/SCUS_971.12').read_bytes()
    if hashlib.sha256(elf).hexdigest() != ELF_SHA256:
        raise SystemExit('export_script_walk_clips: not the pinned boot ELF')
    offset = BASE - 0x100000 + 0x300
    table = elf[offset:offset + 2 * COUNT]
    out = ROOT / 'assets/script_walk_clips.emwc'
    out.parent.mkdir(parents=True, exist_ok=True)
    blob = struct.pack('<4s3I', b'EMWC', 1, BASE, COUNT) + table
    out.write_bytes(blob)
    print(f'script walk clips: {len(blob)} bytes -> {out} (sha256 {hashlib.sha256(blob).hexdigest()[:16]})')


if __name__ == '__main__':
    main()
