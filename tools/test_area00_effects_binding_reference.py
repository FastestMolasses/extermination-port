#!/usr/bin/env python3
"""AREA00 marker/puff adapters through the existing canonical effects owner.

Original instructions run the marker selection and sprite helper, with
random, depth-range and final sprite emission as compared boundaries. The
transform and depth entries execute without hooks. Same bounded cases in
quick/full mode; this is a binding proof, not an AREA00 gameplay claim.
"""
import json
import os
import shlex
import struct
import subprocess
import sys

import test_area01_effects_services_reference as S
from test_player_fall_reference import FallEE
from test_player_slide_reference import read_elf
from test_effect_kinds_reference import KEE

OUT = S.ROOT / 'build/level3/effects-binding'
POS, COLOUR, MATRIX, BLOCK, CTX = 0x1E00000, 0x1E00010, 0x1E00020, 0x1E00080, 0x811CC0


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    exe = OUT / 'contract'
    sources = ['tests/area00_effects_binding_test.c', *S.SOURCES[1:], 'src/game/em_effect_manager.c']
    flags = ['-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
             '-fsanitize=address,undefined', '-ffp-contract=off', '-ffunction-sections',
             '-fdata-sections', '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections', '-Isrc']
    subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + flags + sources + ['-lm', '-o', str(exe)],
                   cwd=S.ROOT, check=True)
    env = dict(os.environ, UBSAN_OPTIONS='halt_on_error=1')
    subprocess.run([str(exe)], cwd=S.ROOT, env=env, check=True)
    elf = read_elf()
    cases = [(0x1F5940, k) for k in (1, 2, 3, 4, 5, 6, 7, 8, 9, 0xFFFFFFFF)]
    cases += [(fn, 0) for fn in (0x1F4D40, 0x1CCF70, 0x1CFA60)]
    boundaries = 0
    for fn, kind in cases:
        seed = FallEE(elf)
        ee = KEE(elf, seed.mem, seed.spad)
        ee.store(0x275670, CTX, 4)
        identity = struct.pack('<16f', 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
        for at in (CTX + 0x2240, CTX + 0x22C0, 0x70003A40, 0x70003AC0): ee.write(at, identity)
        ee.write(CTX + 0xA0, struct.pack('<4f', 255, 0, 255, 0))
        ee.write(POS, struct.pack('<4f', 0, 0, .5, 1))
        ee.write(COLOUR, struct.pack('<4I', 128, 64, 32, 127))
        ee.write(MATRIX, struct.pack('<16f', 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 2, 3, .5, 1))
        ee.write(BLOCK, b'\xA5' * 0x60)
        expected = []
        def rand(e):
            expected.append([0x122BB8, 0x12345678]); e.r[2] = 0x12345678
        def ranges(e):
            expected.append([0x21B9A0, e.r[4] & 0xFFFFFFFF, *e.f[12:14]])
        def sprite(e):
            expected.append([0x1CD520, e.r[4] & 0xFFFFFFFF, e.r[5] & 0xFFFFFFFF,
                             *struct.unpack_from('<4I', e.ram, e.r[6] & 0xFFFFFFFF),
                             e.r[7] & 0xFFFFFFFF, (e.r[7] >> 32) & 0xFFFFFFFF,
                             e.r[8] & 0xFFFFFFFF, *e.f[12:15]])
        ee.stubs = {0x122BB8: rand, 0x21B9A0: ranges, 0x1CD520: sprite}
        args, floats = (POS, COLOUR), (0x40000000, 0x40B00000)
        if fn == 0x1F5940: args, floats = (kind, POS, 0), ()
        if fn == 0x1CCF70: args, floats = (POS,), ()
        if fn == 0x1CFA60: args = (BLOCK, MATRIX)
        ee.call(fn, args, floats)
        native = json.loads(subprocess.check_output([str(exe), f'{fn:x}', f'{kind:x}'],
                                                    cwd=S.ROOT, env=env, text=True))
        assert native['events'] == expected, (hex(fn), kind, native['events'], expected)
        assert bytes.fromhex(native['block']) == bytes(ee.ram[BLOCK:BLOCK+0x60]), (hex(fn), 'transform bytes')
        if fn == 0x1CCF70:
            assert native['v0'] == ee.r[2] and native['depth'] == ee.load(0x275C04)
        boundaries += len(expected)
    print(f'PASS AREA00 effects binding: {len(cases)} original marker/depth/transform cases, '
          f'{boundaries} compared worker boundaries, ASan/UBSan ownership and fault contracts')


if __name__ == '__main__':
    main()
