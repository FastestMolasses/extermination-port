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

The action bindings (docs/CAMERA_LIVE.md section 6): on both captures, the
live camera's action dispatch 0018BC20 (em_camera_live_dispatch) runs
camera actions 9, 11 and 14 (em_area00_low's 00198CE0 / 00198F10 and
em_camera_aim's 00198AF0 through the live binder) for every sub-state 0..3
and the player codes that keep and end each action; the original 0018BC20
runs over the same capture with the same inputs (AREA11's D_00810700 = 0xB,
D_00810701 = 0, 0x70003B50 = the player's +C0..+CC as the live camera
publishes it) with every callee its own original instructions. The whole
camera block and D_008105D0..D_008105EF must be equal byte for byte.
"""
import ctypes as C
import json
import struct
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ['tests/camera_interaction_fixture.c', 'src/game/em_camera.c', 'src/game/em_camera_retarget.c',
           'src/game/em_camera_rotation.c', 'src/game/em_camera_live.c', 'src/game/em_camera_commit_original.c',
           'src/game/em_camera_follow_original.c', 'src/game/em_camera_area11_specials.c',
           # The aim camera (docs/CAMERA_LIVE.md section 7) and its callees.
           'src/game/em_camera_aim.c', 'src/game/em_aim_fire_sdk_memory.c', 'src/game/em_area22_port.c',
           # Camera actions 9 / 11 (00198CE0 / 00198F10, their one owner; section 6).
           'src/game/em_area00_low.c',
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
           'src/game/em_sdk_soft_float.c', 'src/game/em_effect_original.c',
           # The render context (census L32 / L30): the zoom the camera's view
           # publication reads, the 001DD950 store of its 001DD980 calls.
           'src/game/em_render_context_live.c', 'src/game/em_gs_blocks_original.c', 'src/game/em_frame_kick.c',
           'src/game/em_frame_render_heads.c',
           'src/game/em_render_context.c', 'src/game/em_packet_chain_original.c',
           'src/game/em_status_ui_leftovers.c', 'src/game/em_load_veil_particles.c',
           'src/game/em_actor_light_001D89D0.c', 'src/game/em_player_equipment.c']
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


# (action, the player codes that keep it / end it, +1F1 values)
ACTIONS = ((9, (0x10, 0x11), (0,)), (11, (0x12, 0x10), (0,)), (14, (0x28, 0x12), (0, 1, 2)))


def check_dispatch(native, capture, scratchpad, world, cells, what):
    sys.path.insert(0, str(ROOT / 'tools'))
    from test_player_fall_reference import FallEE
    import test_player_slide_reference as S
    elf = S.read_elf()
    ram = capture.read_bytes()
    spad = scratchpad.read_bytes() if scratchpad and scratchpad.is_file() else None
    player = 0x8102B0
    runs = 0
    for action, codes, subs in ACTIONS:
        for state in range(4):
            for code in codes:
                for sub in subs:
                    out = (C.c_uint8 * (0xD0 + 0xD4))()
                    ok = native.test_dispatch(str(capture).encode(), str(world).encode(), str(cells).encode(),
                                              action, state, code, sub, out)
                    assert ok == 1, (what, 'dispatch faulted', action, state, hex(code), sub)
                    e = FallEE(elf, ram, spad)
                    e.save(CAM + 1, state, 1)
                    e.save(CAM + 5, 0, 1)
                    e.save(CAM + 6, action, 1)
                    e.save(player + 0x230, code)
                    e.save(player + 0x1F1, sub, 1)
                    e.save(player + 0xAC, 0x3F800000)
                    e.save(player + 0xBC, 0x3F800000)
                    e.save(0x810700, 0x0B, 1)
                    e.save(0x810701, 0, 1)
                    e.write(0x70003B50, e.read(player + 0xC0, 16))
                    e.call(0x18BC20, (CAM, player))
                    got = bytes(out)
                    want = e.read(CAM, 0xD0) + e.read(POOL, 0x20)
                    assert got[:0xD0] + got[0xD0:0xF0] == want, (
                        what, 'action', action, state, hex(code), sub,
                        [hex(i) for i in range(len(want)) if (got[:0xD0] + got[0xD0:0xF0])[i] != want[i]][:8])
                    runs += 1
    return runs


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
                    '-Wl,-exported_symbol,_test_retarget', '-Wl,-exported_symbol,_test_refusal',
                    '-Wl,-exported_symbol,_test_dispatch', '-O1', '-g',
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
    native.test_dispatch.argtypes = [C.c_char_p, C.c_char_p, C.c_char_p, C.c_int, C.c_int, C.c_int, C.c_int,
                                     C.POINTER(C.c_uint8)]
    runs = check_dispatch(native, capture, capture.parent / 'animation_scratchpad.bin', world,
                          output / 'panel_cells.bin', 'panel')
    runs += check_dispatch(native, refusal, None, world, output / 'refusal_cells.bin', 'refusal')
    print('camera actions 9 / 11 / 14 through the live dispatch vs the original 0018BC20: PASS %d runs '
          '(block and eye / target byte for byte)' % runs)


if __name__ == '__main__':
    main()
