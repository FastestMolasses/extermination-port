#!/usr/bin/env python3
"""Canonical terminal reset/borrow contracts and original 00225A00+00121A28.

The existing status-page/record oracles separately prove cold entry, decline,
and drawing. This checks the actual binding; it never accepts a save or card I/O.
"""
import ctypes as C
import json
import subprocess

import export_area01_common as A
from test_startup_load_gaps_reference import GapEE

OUT = A.ROOT / 'build/level2-crashes/terminal-status-reference'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    unit, library = OUT/'contract', OUT/'bridge.dylib'
    sources = ['tests/area01_terminal_status_bridge.c', 'src/game/em_area01_scene_view.c',
               'src/game/em_area01_room.c', 'src/game/em_area01_actor_view.c', 'src/game/em_actor_pool.c']
    common = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-Isrc']
    subprocess.run(common + ['-fsanitize=address,undefined', '-DEM_TERMINAL_STATUS_CONTRACT'] +
                   sources + ['-o', str(unit)], cwd=A.ROOT, check=True)
    subprocess.run([str(unit)], check=True)
    subprocess.run(common + ['-O2', '-shared', '-fPIC'] + sources + ['-o',str(library)],
                   cwd=A.ROOT, check=True)
    native = C.CDLL(str(library))
    native.terminal_status_reset.argtypes = [C.c_void_p,C.c_void_p]
    elf, captures, cases = A.read_elf(), A.captures(), 0
    for capture in captures:
        for pattern in (capture.ram[0x810040:0x810114], bytes([0xA5])*0xD4, bytes(range(0xD4))):
            oracle = GapEE(elf, bytearray(capture.ram), bytearray(capture.spad))
            oracle.mem[0x810040:0x810114] = pattern
            oracle.call(0x225A00, ())
            assert oracle.written == set(range(0x810040,0x810114))
            before, after = C.create_string_buffer(pattern), C.create_string_buffer(0xD4)
            assert native.terminal_status_reset(before,after) == 0
            assert after.raw == oracle.read(0x810040,0xD4) == bytes(0xD4)
            cases += 1
    report = dict(status='PASS', captures=len(captures), original_reset_cases=cases,
                  contracts='ASan/UBSan exact alias/bounds/lifetime, 256 actor types, signed costs, live/free/reuse, active-view refusal',
                  scope='00225A00 existing owner + actual memset and canonical terminal read boundary; no accepted save/card I/O')
    (OUT/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print('AREA01 terminal status:', json.dumps(report))


if __name__ == '__main__':
    main()
