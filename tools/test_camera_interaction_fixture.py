#!/usr/bin/env python3
"""The scripted retarget (001B7B30 op0D sub 3 and the refusal's sub 5) on the
live camera against two local original captures (census L13..L16,
docs/CAMERA_LIVE.md).

Each capture's camera block, vector pool and player record go into the live
camera (em_camera_live.c); the capture's own seed Euler cam+30 then runs
through the port's 0018CBD0 seed and the live camera's translated
0018D7B0(5), 0018D7B0(1) and cam+A0 = 0x78 (camera_interaction_retarget_*).
The queries run over the collision world of the captured scene: the
original's own cell directory and published class-4 list, read from the
capture (tests/camera_interaction_fixture.c). Every word the retarget
writes must equal the capture byte for byte: the desired eye / target
+0x10 / +0x20, the seed +0x30, the bounds +0x50 / +0x54, the solver flags
+0x07 and surface +0x58 / +0x90, the prepass flags +0x5A, +0x6D and +0x60,
and the actual eye / target D_008105D0 / E0 (0018D7B0 style 1's copy).
A captured-scene regression, not exhaustive: the solvers' own oracles are
tools/test_camera_follow_original_reference.py and
tools/test_camera_leftovers_reference.py.
"""
import ctypes as C
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ['tests/camera_interaction_fixture.c', 'src/game/em_camera.c', 'src/game/em_camera_retarget.c',
           'src/game/em_camera_rotation.c', 'src/game/em_camera_live.c', 'src/game/em_camera_commit_original.c',
           'src/game/em_camera_follow_original.c', 'src/game/em_camera_area11_specials.c',
           'src/game/em_camera_leftovers.c', 'src/game/em_camera_leftovers_solver.c',
           'src/game/em_census_standins.c', 'src/game/em_render_verify_rest.c',
           'src/game/em_owner_services_original.c', 'src/game/em_message_draw_original.c',
           'src/game/em_script_host_workers.c', 'src/game/em_player_stage_workers.c', 'src/game/em_script.c',
           'src/game/em_script_door_fan.c', 'src/game/em_director_original.c',
           'src/game/em_player_closure_10_12_19.c', 'src/game/em_interaction_projection.c',
           'src/game/em_item_sdk_math.c',
           'src/game/em_collision.c', 'src/game/em_collision_world.c', 'src/game/em_actor_collision.c',
           'src/game/em_actor_pool.c', 'src/game/em_coll_probe_original.c', 'src/game/em_coll_grid_hull.c',
           'src/game/em_coll_segment_walkers.c', 'src/game/em_coll_list_passes.c',
           'src/game/em_coll_list_passes_walkers.c', 'src/game/em_sdk_math_original.c',
           'src/game/em_sdk_soft_float.c', 'src/game/em_effect_original.c']
CAM, POOL = 0x8101E0, 0x8105D0
# The camera block words the retarget writes (offset, size).
WRITTEN = ((0x07, 1), (0x10, 12), (0x20, 12), (0x30, 12), (0x50, 4), (0x54, 4), (0x58, 2), (0x5A, 2),
           (0x60, 4), (0x6D, 1), (0x90, 4))


def check(fn, capture, world, cells, what):
    out = (C.c_uint8 * (0xD0 + 0xD4))()
    assert fn(str(capture).encode(), str(world).encode(), str(cells).encode(), out) == 1, (what, 'retarget')
    ram = capture.read_bytes()
    got = bytes(out)
    for offset, size in WRITTEN:
        assert got[offset:offset + size] == ram[CAM + offset:CAM + offset + size], \
            (what, f'cam+{offset:#x}', got[offset:offset + size].hex(), ram[CAM + offset:CAM + offset + size].hex())
    for address in (0x8105D0, 0x8105E0):
        at = 0xD0 + address - POOL
        assert got[at:at + 12] == ram[address:address + 12], (what, hex(address))
    return {'camera_words_exact': [hex(o) for o, _ in WRITTEN], 'eye_and_target_exact': True}


def main():
    assert sys.platform == 'darwin', 'This captured-scene host harness uses the macOS linker'
    capture = ROOT.parent / 'Extermination/build/startup-reference/panel/animation_ee.bin'
    refusal = ROOT.parent / 'Extermination/build/startup-reference/elevator/refusal/eeMemory.bin'
    world = ROOT / 'assets/scene_snow/snow.emcl'
    assert capture.is_file() and world.is_file(), 'Generate the local reference assets first'
    assert refusal.is_file(), 'Capture original refusal in fresh slot13 first'
    output = ROOT / 'build/camera_interaction'
    output.mkdir(parents=True, exist_ok=True)
    library = output / 'fixture.dylib'
    subprocess.run(['cc', '-dynamiclib', '-Wl,-undefined,dynamic_lookup', '-Wl,-dead_strip',
                    '-Wl,-exported_symbol,_test_retarget', '-Wl,-exported_symbol,_test_refusal', '-O1', '-g',
                    '-ffp-contract=off', '-Isrc', *SOURCES, '-lm', '-o', str(library)], cwd=ROOT, check=True)
    native = C.CDLL(str(library))
    for fn in (native.test_retarget, native.test_refusal):
        fn.argtypes = [C.c_char_p, C.c_char_p, C.c_char_p, C.POINTER(C.c_uint8)]
    # The captured scene's own cell directory is copied here (build/, ignored).
    report = check(native.test_retarget, capture, world, output / 'panel_cells.bin', 'panel')
    (output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print('captured original panel camera fixture PASS', json.dumps(report))
    report = check(native.test_refusal, refusal, world, output / 'refusal_cells.bin', 'refusal')
    (output / 'refusal.json').write_text(json.dumps(report, indent=2) + '\n')
    print('captured original elevator refusal camera fixture PASS', json.dumps(report))


if __name__ == '__main__':
    main()
