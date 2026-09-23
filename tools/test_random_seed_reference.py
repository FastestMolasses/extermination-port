#!/usr/bin/env python3
"""Check the native cold-boot SDK random state against the owner's ELF.

1. Executes the original rand leaf 00122BB8 on the ELF's own initialized
   memory (the state word at (*D_0024295C)+0x58) and compares the stream
   with a freshly loaded em_random.c, with no seeding call.
2. Scans the ELF and every local AREA overlay for calls/jumps to the
   seeding leaf 00122BA8: the only one must be inside anim_frame_top_a
   (0x001ACA20..0x001ACE70, attract-demo start), so cold boot and New Game
   never reseed.
No original instruction bytes or data are embedded in or printed by it.
"""
import ctypes as C
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile

from test_interaction_scan_reference import ELF_SHA
from test_input_block_reference import PadOracle

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
RAND, SRAND = 0x122BB8, 0x122BA8
TOP_A = (0x1ACA20, 0x1ACE70)


def main():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    # Loadable image: file 0x300.. -> vram 0x100000 (CLAUDE.md target identity).
    text = elf[0x300:0x300 + 0x175B00]
    calls = []
    for op in (3, 2):                       # jal, j
        word = struct.pack('<I', op << 26 | SRAND >> 2)
        start = 0
        while (index := text.find(word, start)) >= 0:
            if index % 4 == 0:
                calls.append(0x100000 + index)
            start = index + 1
    assert len(calls) == 1 and TOP_A[0] <= calls[0] < TOP_A[1], [hex(c) for c in calls]
    overlays = sorted((DECOMP / 'extract/OVERLAY').glob('AREA*.BIN'))
    assert overlays, 'extract the disc OVERLAY directory first'
    for path in overlays:
        data = path.read_bytes()
        for op in (3, 2):
            assert struct.pack('<I', op << 26 | SRAND >> 2) not in data, path.name

    with tempfile.TemporaryDirectory(prefix='em_random_seed_') as tmp:
        library = Path(tmp) / 'random.so'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-shared',
                        '-fPIC', '-I' + str(ROOT / 'src'),
                        str(ROOT / 'src/game/em_random.c'), '-o', str(library)],
                       check=True)
        native = C.CDLL(str(library))
        native.em_random_next.restype = C.c_uint32
        oracle = PadOracle(elf)
        count = 4096
        for _ in range(count):
            oracle.run(RAND)
            expected = oracle.r[2] & 0xFFFFFFFF
            actual = native.em_random_next()
            assert actual == expected, (actual, expected)
    print(f'random cold-boot reference: PASS ({count} outputs from the ELF-initialized '
          f'state; 1 seeding call, in anim_frame_top_a; {len(overlays)} overlays clear)')


if __name__ == '__main__':
    main()
