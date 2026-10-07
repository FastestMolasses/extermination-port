#!/usr/bin/env python3
"""Canonical terminal pose storage and original 00159B90 copy-prefix proof.

The terminal's approach/script/audio/draw are explicit boundaries. The original
owner and 00102948 instructions execute; native copies use the production SDK
adapter through the actual scene view. No save serializer is exercised.
"""
import ctypes as C
import json
import struct
import subprocess

import export_area01_common as A
from test_area01_sys_reference import run_nested
from test_player_fall_reference import FallEE

OUT = A.ROOT / 'build/level2-crashes/terminal-progress-reference'
DEST, PLAYER = 0x810710, 0x8102B0


class Boundary(Exception):
    pass


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    bridge = 'tests/area01_terminal_progress_bridge.c'
    unit, library = OUT/'contract', OUT/'bridge.dylib'
    common = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Isrc']
    subprocess.run(common + ['-fsanitize=address,undefined', '-DEM_TERMINAL_CONTRACT_ONLY',
                              bridge, 'src/game/em_area01_scene_view.c', '-o', str(unit)],
                   cwd=A.ROOT, check=True)
    subprocess.run([str(unit)], check=True)
    subprocess.run(common + ['-O2', '-ffp-contract=off', '-shared', '-fPIC',
                              bridge,
                              'src/game/em_area01_scene_view.c', 'src/game/em_area01_sys.c',
                              'src/game/em_aim_fire_sdk_memory.c',
                              'src/game/em_owner_services_original.c', 'src/game/em_effect_original.c',
                              'src/game/em_point_light.c', 'src/game/em_coll_probe_original.c',
                              'src/game/em_actor_collision.c', 'src/game/em_actor_pool.c', 'src/game/em_collision.c',
                              'src/game/em_camera_commit_original.c', 'src/game/em_sdk_math_original.c',
                              '-lm', '-o', str(library)],
                   cwd=A.ROOT, check=True)
    native = C.CDLL(str(library))
    native.terminal_progress_prefix.argtypes = [C.c_void_p, C.c_void_p, C.c_uint32, C.c_int,
                                                C.c_void_p, C.c_void_p, C.c_void_p]
    elf, captures, cases, copies, cuts = A.read_elf(), A.captures(), 0, 0, 0
    for cap in captures:
        nodes = [0x7A5640 + 0x2F0*i for i in range(256)
                 if A.u32(cap.ram, 0x7A5640 + 0x2F0*i + 0x10) == 0x159B90]
        assert nodes, (cap.name, 'no original terminal owner')
        node = nodes[0]
        for branch in range(4):
            for pattern in range(3):
                ram, scratch = bytearray(cap.ram), bytearray(cap.spad)
                ram[node+4:node+6] = bytes((1, 0))
                ram[node+3], ram[node+0xB] = (0x38, 0 if branch == 0 else 4)
                ram[0x810C7F] = 0 if branch == 2 else 1
                struct.pack_into('<h', ram, node+0x34, 1)
                struct.pack_into('<h', ram, 0x810CB2, 5)
                if branch == 3:
                    ram[node+3], ram[node+0xB], ram[0x810C63] = 0x12, 5, 2
                    struct.pack_into('<h', ram, 0x810CB4, 61)
                if pattern:
                    first = bytes(range(16)) if pattern == 1 else struct.pack('<4I', 0, 0x80000000, 0x7F800000, 0x7FC12345)
                    second = first[::-1]
                    ram[PLAYER+0xA0:PLAYER+0xB0] = first
                    ram[PLAYER+0xC0:PLAYER+0xD0] = second
                ram[DEST:DEST+32] = bytes([0xA5])*32
                before = bytes(ram)
                oracle = FallEE(elf, ram, scratch)
                expected = []
                def boundary(fn, nargs, execute=False):
                    def call(o):
                        expected.append((fn, o.r[4] & 0xFFFFFFFF,
                                         o.r[5] & 0xFFFFFFFF if nargs > 1 else 0))
                        if execute:
                            run_nested(o, fn)
                        elif fn in (0x1FB9F0, 0x1026A0):
                            raise Boundary()
                        else:
                            o.r[2] = 0
                    return call
                for fn, nargs in ((0x1B6F00, 2), (0x1BA1A0, 2), (0x1BA1F0, 1),
                                   (0x1FB9F0, 4), (0x1026A0, 3)):
                    oracle.hooks[fn] = boundary(fn, nargs)
                oracle.hooks[0x102948] = boundary(0x102948, 2, True)
                try:
                    oracle.call(0x159B90, (node,))
                    raise AssertionError('expected explicit post-copy audio/draw boundary')
                except Boundary:
                    pass
                data = C.create_string_buffer(before)
                spr = C.create_string_buffer(bytes(scratch))
                out, calls, result = C.create_string_buffer(32), ((C.c_uint32*3)*32)(), (C.c_uint32*3)()
                rc = native.terminal_progress_prefix(data, spr, node, 0, out, calls, result)
                assert rc == -1 and result[1] == (0x1FB9F0 if branch else 0x1026A0), (cap.name, branch, pattern, rc, list(result), expected, [tuple(c) for c in calls[:result[0]]])
                assert [tuple(c) for c in calls[:result[0]]] == expected, (cap.name, branch, pattern)
                assert out.raw == oracle.read(DEST, 32), (cap.name, branch, pattern, 'canonical copy')
                assert data.raw[DEST:DEST+32] == bytes([0xA5])*32, 'flat RAM became a duplicate owner'
                expected_ram = bytearray(oracle.mem)
                expected_ram[DEST:DEST+32] = bytes([0xA5])*32
                assert data.raw[:len(before)] == expected_ram, (cap.name, branch, pattern, 'other RAM')
                assert spr.raw[:len(scratch)] == oracle.spad, (cap.name, branch, pattern, 'scratch')
                count = sum(row[0] == 0x102948 for row in expected)
                assert count == (2 if branch else 0)
                copies += count
                cases += 1
                if branch == 2 and pattern == 0:
                    # Removing the exact canonical window recreates the reached
                    # missing-copy failure, before audio and without fallback.
                    data, spr = C.create_string_buffer(before), C.create_string_buffer(bytes(scratch))
                    assert native.terminal_progress_prefix(data, spr, node, 1, out, calls, result) == -1
                    assert result[1] == 0 and result[2] == 0x102948
                    assert out.raw == bytes([0xA5])*32
                    cuts += 1
    report = dict(status='PASS', captures=len(captures), cases=cases, original_quadword_copies=copies, missing_window_refusals=cuts,
                  contracts='ASan/UBSan alias, bounds, neighboring owner, retained lifetime and original reset',
                  scope='00159B90 interaction-to-audio prefix; no serializer, saved-game load, UI or audio parity claim')
    (OUT/'result.json').write_text(json.dumps(report, indent=2)+'\n')
    print('AREA01 terminal canonical progress:', json.dumps(report))


if __name__ == '__main__':
    main()
