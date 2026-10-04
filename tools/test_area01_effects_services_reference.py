#!/usr/bin/env python3
"""Effects service/storage contracts plus original spawn+pool instructions.

Synthetic no-light/no-sound table isolates the adapter and actual allocator;
the existing effect_original oracle separately proves the complete spawn
chain over recorded snapshots. No captured RAM is a runtime dependency.
"""
import hashlib
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys

from reference_mode import MODE
from test_effect_original_reference import EE, ELF_SHA, TABLE
from test_actor_pool_reference import POOL, SIZE, COUNT

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/level2/effects-services'
SOURCES = ['tests/area01_effects_services_test.c', 'src/game/em_area01_effects_services.c',
           'src/game/em_area01_actor_view.c', 'src/game/em_effect_original.c',
           'src/game/em_effect_kinds.c', 'src/game/em_head_sprite_original.c',
           'src/game/em_packet_chain_original.c', 'src/game/em_actor_pool.c',
           'src/game/em_status_ui_leftovers.c']


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    exe = OUT / 'contract'
    flags = ['-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
             '-fsanitize=address,undefined', '-ffp-contract=off', '-ffunction-sections',
             '-fdata-sections', '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections', '-Isrc']
    subprocess.run(shlex.split(os.environ.get('CC', 'cc'))+flags+SOURCES+['-lm', '-o', str(exe)],
                   cwd=ROOT, check=True)
    env = dict(os.environ, UBSAN_OPTIONS='halt_on_error=1')
    subprocess.run([str(exe)], cwd=ROOT, env=env, check=True)
    elf = (ROOT.parent/'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    vectors = struct.pack('<8I', 0x3F800000, 0x40000000, 0x40400000, 0x40800000,
                          0x3E800000, 0x3F000000, 0xBF800000, 0x3F800000)
    # Every case runs actual reset, allocation/list linking, spawner and SDK
    # copy instructions. Memset is the sole explicit original call boundary.
    cases = 0
    for function in (0x1EFD90, 0x1EFD20, 0x1EF9D0):
        for callback in (0, 0x21AAC0, 0x1EA240):
            ee = EE(elf, bytearray(0x2000000), bytearray(0x4000))
            def memset(e):
                assert e.u32(5) == 0 and e.u32(6) == SIZE
                assert POOL <= e.u32(4) < POOL+SIZE*COUNT
                e.write(e.u32(4), bytes(SIZE)); e.set32(2, e.u32(4))
            ee.stubs[0x121A28] = memset
            ee.call(0x1AF8E0)
            ee.store(0x259C70, TABLE)
            ee.store(TABLE, 0x0C, 1); ee.store(TABLE+8, 7, 1)
            ee.store(TABLE+0xC, callback); ee.store(TABLE+0x24, 0xFFFFFFFF)
            ee.write(0x1E00000, vectors)
            args = (0x80000000, 0x1E00000, 0x1E00010) if function == 0x1EFD90 else (0x80000000, 0x1E00000)
            ee.call(function, args, (0x3F800000,) if function == 0x1EF9D0 else ())
            native = subprocess.check_output([str(exe), f'{function:x}', f'{callback:x}'],
                                             cwd=ROOT, env=env, text=True).split()
            assert int(native[0], 16) == ee.u32(2), (hex(function), hex(callback), native[0], hex(ee.u32(2)))
            actual, expected = bytes.fromhex(native[1]), bytes(ee.ram[POOL:POOL+SIZE])
            assert actual == expected, (hex(function), hex(callback), next((hex(i),a,b)
                                          for i,(a,b) in enumerate(zip(actual,expected)) if a != b))
            cases += 1
    print(f'PASS {MODE}: {cases} original spawn/reset/allocator chains, ASan/UBSan canonical ownership contracts (same cases in both modes)')


if __name__ == '__main__':
    main()
