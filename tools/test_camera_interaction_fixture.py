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
camera actions 9, 10, 11 and 14 (em_area00_low's 00198CE0 / 00198F10,
em_area01_room's 00198D90 with the 001D2830(3, 1) after it, and
em_camera_aim's 00198AF0 through the live binder) for every sub-state 0..3
and the player codes that keep and end each action; the original 0018BC20
runs over the same capture with the same inputs (AREA11's D_00810700 = 0xB,
D_00810701 = 0, 0x70003B50 = the player's +C0..+CC as the live camera
publishes it) with every callee its own original instructions. The whole
camera block and D_008105D0..D_008105EF must be equal byte for byte.

The hand-offs: the actions move the camera on by themselves (00198CE0
stores +6 = 0xA on code 0x11, 00198D90 0xB on 0x12, 00198F10 and 00198AF0
0). Multi-frame chains run the live dispatch and the original 0018BC20
frame after frame from the same start, the player's code changing between
frames: 9 -> 10 -> 11 -> 0 and 14 -> 0. Every frame the block, the pool and
the render context's flags word (context +0x0C, set by 001D2830(3, 1)) must
be equal byte for byte, and the original's own action sequence is asserted
so the chain really passes through every action.

EM_TEST_FULL=1 also rebuilds the fixture from mutated copies of the binder
(em_camera_live.c: action 10 unbound, action 14 unbound, action 9 bound to
11's routine, 001D2830's flag dropped) and of em_camera_aim.c (00198AF0's
sub-state 2 with the NEARMISS C's `and`): each must fail a check.
"""
import ctypes as C
import json
import os
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
           # Camera actions 9 / 11 (00198CE0 / 00198F10) and 10 (00198D90), their
           # one owners (section 6).
           'src/game/em_area00_low.c', 'src/game/em_area01_room.c',
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
ACTIONS = ((9, (0x10, 0x11), (0,)), (10, (0x11, 0x12), (0,)), (11, (0x12, 0x10), (0,)),
           (14, (0x28, 0x12), (0, 1, 2)))
# (start action, +1F1, the player's code per frame, the original's action +6
# after each frame): the hand-offs 9 -> 10 -> 11 -> 0 and 14 -> 0.
CHAINS = ((9, 0, (0x10, 0x10, 0x11, 0x11, 0x11, 0x12, 0x12, 0x12, 0x10),
           (9, 9, 10, 10, 10, 11, 11, 11, 0)),
          # 14: sub-state 1 waits on the code (and +1F1 == 1 moves it to 2,
          # which waits on the code too); another code moves it to 3, which
          # seats the camera and clears +6.
          (14, 0, (0x28, 0x28, 0x12, 0x12), (14, 14, 14, 0)),
          (14, 1, (0x28, 0x28, 0x12, 0x12), (14, 14, 14, 0)))
CHAIN_FRAME = 0xD0 + 0xD4 + 4
RCL_FLAGS = 0x811CC0 + 0x0C      # the render context (D_00275670) +0x0C


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


def build(library, sources):
    subprocess.run(['cc', '-dynamiclib', '-Wl,-undefined,dynamic_lookup', '-Wl,-dead_strip',
                    '-Wl,-exported_symbol,_test_retarget', '-Wl,-exported_symbol,_test_refusal',
                    '-Wl,-exported_symbol,_test_dispatch', '-Wl,-exported_symbol,_test_chain', '-O1', '-g',
                    '-ffp-contract=off', '-Isrc', *sources, '-lm', '-o', str(library)], cwd=ROOT, check=True)
    native = C.CDLL(str(library))
    native.test_dispatch.argtypes = [C.c_char_p, C.c_char_p, C.c_char_p, C.c_int, C.c_int, C.c_int, C.c_int,
                                     C.POINTER(C.c_uint8)]
    native.test_chain.argtypes = [C.c_char_p, C.c_char_p, C.c_char_p, C.c_int, C.c_int, C.c_int,
                                  C.POINTER(C.c_int), C.POINTER(C.c_uint8)]
    return native


def check_chains(native, capture, scratchpad, world, cells, what):
    """The hand-offs frame by frame: the live dispatch and the original
    0018BC20 from the same start, the player's code changing per frame."""
    sys.path.insert(0, str(ROOT / 'tools'))
    from test_player_fall_reference import FallEE
    import test_player_slide_reference as S
    elf = S.read_elf()
    ram = capture.read_bytes()
    spad = scratchpad.read_bytes() if scratchpad and scratchpad.is_file() else None
    player = 0x8102B0
    frames = 0
    for action, sub, codes, sequence in CHAINS:
        n = len(codes)
        out = (C.c_uint8 * (n * CHAIN_FRAME))()
        ran = native.test_chain(str(capture).encode(), str(world).encode(), str(cells).encode(),
                                action, sub, n, (C.c_int * n)(*codes), out)
        e = FallEE(elf, ram, spad)
        e.save(CAM + 1, 0, 1)
        e.save(CAM + 5, 0, 1)
        e.save(CAM + 6, action, 1)
        e.save(player + 0x1F1, sub, 1)
        e.save(player + 0xAC, 0x3F800000)
        e.save(player + 0xBC, 0x3F800000)
        e.save(0x810700, 0x0B, 1)
        e.save(0x810701, 0, 1)
        e.write(0x70003B50, e.read(player + 0xC0, 16))
        actions = []
        for i, code in enumerate(codes):
            e.save(player + 0x230, code)
            e.call(0x18BC20, (CAM, player))
            want = e.read(CAM, 0xD0) + e.read(POOL, 0x20)
            actions.append(want[6])
            assert ran > i, (what, 'chain from action', action, 'the live dispatch faulted at frame', i,
                             'where the original runs action', actions)
            frame = bytes(out)[i * CHAIN_FRAME:(i + 1) * CHAIN_FRAME]
            got = frame[:0xD0] + frame[0xD0:0xF0]
            assert got == want, (what, 'chain from action', action, 'frame', i, hex(code),
                                 [hex(j) for j in range(len(want)) if got[j] != want[j]][:8])
            flags = frame[0xD0 + 0xD4:]
            assert flags == e.read(RCL_FLAGS, 4), (what, 'chain from action', action, 'frame', i,
                                                    'render context flags', flags.hex(),
                                                    e.read(RCL_FLAGS, 4).hex())
            frames += 1
        if sequence is not None:
            assert tuple(actions) == sequence, (what, 'the original\'s hand-offs', actions)
        assert actions[-1] == 0, (what, 'chain from action', action, 'does not end in action 0', actions)
    return frames


# Mutated copies of a source (path, the original text, its replacement).
MUTANTS = (
    ('action 10 unbound', 'src/game/em_camera_live.c',
     '    C.lw.w_00198D90 = lw_00198D90;\n', ''),
    ('action 14 unbound', 'src/game/em_camera_live.c',
     '    C.lw.w_00198AF0 = lw_00198AF0;\n', ''),
    ('action 9 runs 11', 'src/game/em_camera_live.c',
     '    return low_run(0x00198CE0u);', '    return low_run(0x00198F10u);'),
    ('001D2830 flag dropped', 'src/game/em_camera_live.c',
     '    return em_rcl_001D2830(a0, a1) < 0 ? fail(0x001D2830u) : 0;',
     '    return em_rcl_001D2830(a0, 0) < 0 ? fail(0x001D2830u) : 0;'),
    ('00198AF0 sub-state 2 as the C reads it', 'src/game/em_camera_aim.c',
     '        if (w32(r, pl + 0x230) != 0x28 || u8(r, pl + 0x1F1) == 2)',
     '        if (w32(r, pl + 0x230) == 0x28 && u8(r, pl + 0x1F1) == 2)'),
)


def check_mutants(capture, world, output):
    caught = 0
    for name, path, old, new in MUTANTS:
        text = (ROOT / path).read_text()
        assert text.count(old) == 1, ('mutant site not found', name)
        folder = output / ('mutant_%d' % caught)
        folder.mkdir(parents=True, exist_ok=True)
        copy = folder / Path(path).name
        copy.write_text(text.replace(old, new))
        sources = [str(copy) if s == path else s for s in SOURCES]
        native = build(folder / 'fixture.dylib', sources)
        try:
            check_dispatch(native, capture, capture.parent / 'animation_scratchpad.bin', world,
                           output / 'panel_cells.bin', 'mutant')
            check_chains(native, capture, capture.parent / 'animation_scratchpad.bin', world,
                         output / 'panel_cells.bin', 'mutant')
        except AssertionError:
            caught += 1
            print('mutant caught: %s' % name)
            continue
        raise AssertionError(('mutant survived', name))
    return caught


def main():
    assert sys.platform == 'darwin', 'This captured-scene host harness uses the macOS linker'
    capture = ROOT.parent / 'Extermination/build/startup-reference/panel/animation_ee.bin'
    refusal = ROOT.parent / 'Extermination/build/startup-reference/elevator/refusal/eeMemory.bin'
    world = ROOT / 'assets/scene_snow/snow.emcl'
    assert capture.is_file() and world.is_file(), 'Generate the local reference assets first'
    assert refusal.is_file(), 'Capture original refusal in fresh slot13 first'
    output = ROOT / 'build/camera_interaction'
    output.mkdir(parents=True, exist_ok=True)
    native = build(output / 'fixture.dylib', SOURCES)
    for fn in (native.test_retarget, native.test_refusal):
        fn.argtypes = [C.c_char_p, C.c_char_p, C.c_char_p, C.POINTER(C.c_uint8)]
    # The captured scene's own cell directory is copied here (build/, ignored).
    report = check(native.test_retarget, capture, world, output / 'panel_cells.bin', 'panel')
    (output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print('captured original panel camera fixture PASS', json.dumps(report))
    report = check(native.test_refusal, refusal, world, output / 'refusal_cells.bin', 'refusal')
    (output / 'refusal.json').write_text(json.dumps(report, indent=2) + '\n')
    print('captured original elevator refusal camera fixture PASS', json.dumps(report))
    runs = check_dispatch(native, capture, capture.parent / 'animation_scratchpad.bin', world,
                          output / 'panel_cells.bin', 'panel')
    runs += check_dispatch(native, refusal, None, world, output / 'refusal_cells.bin', 'refusal')
    print('camera actions 9 / 10 / 11 / 14 through the live dispatch vs the original 0018BC20: PASS %d runs '
          '(block and eye / target byte for byte)' % runs)
    frames = check_chains(native, capture, capture.parent / 'animation_scratchpad.bin', world,
                          output / 'panel_cells.bin', 'panel')
    frames += check_chains(native, refusal, None, world, output / 'refusal_cells.bin', 'refusal')
    print('camera hand-offs 9 -> 10 -> 11 -> 0 and 14 -> 0 frame by frame vs the original 0018BC20: PASS '
          '%d frames (block, eye / target and the render context flags byte for byte)' % frames)
    if os.environ.get('EM_TEST_FULL') == '1':
        caught = check_mutants(capture, world, output)
        print('binder / 00198AF0 mutants: all %d caught' % caught)
    else:
        print('mode quick: mutants skipped (EM_TEST_FULL=1 rebuilds and runs them)')


if __name__ == '__main__':
    main()
