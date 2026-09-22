#!/usr/bin/env python3
"""Original instruction checks for the player pose host's narrow state events.

182F90 executes its original SDK vector callees. Idle-return callbacks execute
61020 with animation selection and unrelated physics as recorded boundaries.
No instruction bytes are embedded or loaded by the native game.
"""
import ctypes as C
import json
from pathlib import Path
import random
import struct
import subprocess
import tempfile

from test_interaction_scan_reference import ScanOracle, PLAYER, bits, number

ROOT = Path(__file__).resolve().parents[1]
TARGET = 0x940000

BRIDGE = r'''
#include "game/em_player_pose_host.c"
EmGameState g;
static EmTransitionFade fade;
const EmTransitionFade *em_frame_transition(void) { return &fade; }
void em_frame_request_quit(void) { abort(); }
void palette_apply_placement(float *palette, uint32_t count, const float p[3], float yaw) {
    (void)palette; (void)count; (void)p; (void)yaw;
}
int load(const char *path) {
    g.model.bone_count = 22;
    g.status.health = 100;
    return player_pose_load(path) && player_pose_opening_release();
}
void align_input(const float *feet, const float *hip, const float *euler) {
    memcpy(g.pos, feet, 12);
    memcpy(g.player_palette + 28, hip, 12);
    g.yaw = euler[1];
    player_pose_finish_palette();
}
int align_to(const float *target, float *out) {
    if (!player_pose_align(target)) return 0;
    memcpy(out, g.pos, 12);
    if (!player_pose_hip(out + 3) || !player_pose_script_euler(out + 6)) return 0;
    return 1;
}
void face_owned(int owned) { source.pose.acquired = owned; }
int face_to(float yaw, float *out) {
    if (!player_pose_face(yaw)) return 0;
    memcpy(out, g.pos, 12);
    return player_pose_hip(out + 3) && player_pose_script_euler(out + 6);
}
void cancel_entry(void) {
    em_player_pose_init(&source.pose, &source.bank, 1, 64);
    player_pose_entry_cancel();
}
int return_tick(unsigned flags) {
    source.pose.flags = flags;
    return player_pose_entry_return_tick();
}
void return_state(unsigned *out) {
    out[0] = source.idle_return ? source.idle_return : 1;
    out[1] = source.idle_count;
    player_pose_source(out + 2, (float *)(out + 3), NULL, (int *)(out + 4));
}
void use_start(unsigned clip, float frame) {
    em_player_pose_init(&source.pose, &source.bank, clip, frame);
    g.loco_upt = .3f; g.loco_mode = g.loco_tier = 2;
}
int use_reset(unsigned *out) {
    if (!player_pose_use_accepted()) return 0;
    out[0] = g.loco_mode; out[1] = g.loco_tier;
    memcpy(out + 2, &g.loco_upt, 4);
    player_pose_source(out + 3, (float *)(out + 4), NULL, (int *)(out + 5));
    return 1;
}
static unsigned poll_count;
static int accepted_use(void *context) {
    ++poll_count;
    return *(int *)context;
}
unsigned poll_gate(unsigned action, unsigned phase, unsigned fade_wait, int accepted) {
    source.idle_return = 0;
    source.idle_phase = action == 0 && phase == 1;
    g.loco_mode = action == 1;
    g.loco_entry_ticks = action == 0 && phase == 2;
    g.loco_reentry.phase = action == 1 && phase == 0x63 ? 1 : 0;
    if (action == 0 && phase >= 0x63) source.idle_return = phase;
    fade.substate = fade_wait;
    poll_count = 0;
    player_use_set_hook(accepted_use, &accepted);
    unsigned result = player_use_poll();
    player_use_set_hook(NULL, NULL);
    return poll_count * 2 + result;
}
'''


def vector(values):
    return (C.c_float * len(values))(*values)


def main():
    elf = (ROOT.parent / 'Extermination/config/SCUS_971.12').read_bytes()
    random_source = random.Random(0x182F90)
    report = {}
    with tempfile.TemporaryDirectory(prefix='player_pose_host_reference_') as folder:
        source = Path(folder) / 'bridge.c'
        source.write_text(BRIDGE)
        library = Path(folder) / 'host.dylib'
        subprocess.run(['cc', '-std=c11', '-O2', '-ffp-contract=off', '-fPIC', '-shared',
                        '-I' + str(ROOT / 'src'), str(source),
                        str(ROOT / 'src/game/em_player_pose.c'),
                        str(ROOT / 'src/game/em_pose_bank.c'),
                        str(ROOT / 'src/game/em_pose_transition.c'),
                        str(ROOT / 'src/game/em_player_foot_stop.c'),
                        str(ROOT / 'src/game/em_camera_rotation.c'), '-lm', '-o', str(library)], check=True)
        native = C.CDLL(str(library))
        native.load.argtypes = [C.c_char_p]
        native.align_input.argtypes = [C.POINTER(C.c_float)] * 3
        native.align_to.argtypes = [C.POINTER(C.c_float)] * 2
        native.face_to.argtypes = [C.c_float, C.POINTER(C.c_float)]
        native.return_state.argtypes = [C.POINTER(C.c_uint)]
        native.use_start.argtypes = [C.c_uint, C.c_float]
        native.use_reset.argtypes = [C.POINTER(C.c_uint)]
        assert native.load(str(ROOT / 'assets/player_channels.empc').encode())
        cases = []
        for _ in range(1200):
            feet = vector([random_source.uniform(-1024, 1024) for _ in range(3)])
            hip = vector([feet[i] + random_source.uniform(-16, 16) for i in range(3)])
            target = vector([feet[i] + random_source.uniform(-40, 40) for i in range(3)])
            cases.append((feet, hip, target, vector([0, random_source.uniform(-3.14, 3.14), 0])))
        for name in ('elevator/clip47_ee.bin', 'elevator/refusal/eeMemory.bin', 'playable_ee.bin'):
            data = (ROOT.parent / 'Extermination/build/startup-reference' / name).read_bytes()
            cases.append((vector(struct.unpack_from('<3f', data, 0x810350)),
                          vector(struct.unpack_from('<3f', data, 0x810360)),
                          vector([222, 230, 250]),
                          vector(struct.unpack_from('<3f', data, 0x810370))))
        for feet, hip, target, euler in cases:
            original = ScanOracle(elf)
            original.write(PLAYER + 0xA0, bytes(feet) + struct.pack('<f', 1))
            original.write(PLAYER + 0xB0, bytes(hip) + struct.pack('<f', 1))
            original.write(PLAYER + 0xC0, bytes(euler) + struct.pack('<f', 1))
            original.write(0x70003B40, bytes(hip) + struct.pack('<f', 1))
            original.write(TARGET, bytes(target) + struct.pack('<f', 1))
            original.run(0x182F90, (PLAYER, TARGET))
            native.align_input(feet, hip, euler)
            output = (C.c_float * 9)()
            assert native.align_to(target, output)
            expected = (original.read(PLAYER + 0xA0, 12) + original.read(PLAYER + 0xB0, 12) +
                        original.read(0x70003B50, 12))
            assert bytes(output) == expected, (list(feet), list(target), list(output), expected.hex())
            assert original.read(PLAYER + 0xB0, 12) == original.read(0x70003B40, 12)
        report['alignment_sdk_cases'] = len(cases)

        count = 0
        player_global = 0x8102B0  #1B9C10/sub8 writes the global player.
        for owned in (0, 1):
            native.face_owned(owned)
            for feet, hip, target, euler in cases[:200] + cases[-3:]:
                for face_first in (False, True):
                    original = ScanOracle(elf)
                    original.write(player_global + 0xA0, bytes(feet) + struct.pack('<f', 1))
                    original.write(player_global + 0xB0, bytes(hip) + struct.pack('<f', 1))
                    original.write(player_global + 0xC0, bytes(euler) + struct.pack('<f', 1))
                    original.write(0x70003B40, bytes(hip) + struct.pack('<f', 1))
                    original.write(0x70003B50, bytes(euler) + struct.pack('<f', 1))
                    original.write(TARGET, bytes(target) + struct.pack('<f', 1))
                    record = TARGET + 0x100
                    original.save(record + 8, 8)
                    original.save(record + 0x24, bits(-1.3037610054016113))
                    native.align_input(feet, hip, euler)
                    output = (C.c_float * 9)()
                    for face in ((True, False) if face_first else (False, True)):
                        if face:
                            original.run(0x1B9C10, (player_global, 0, record))
                            assert native.face_to(number(bits(-1.3037610054016113)), output)
                        else:
                            original.run(0x182F90, (player_global, TARGET))
                            assert native.align_to(target, output)
                        expected = (original.read(player_global + 0xA0, 12) +
                                    original.read(player_global + 0xB0, 12) +
                                    original.read(0x70003B50, 12))
                        assert bytes(output) == expected, (owned, face_first, face, list(output))
                        assert original.read(player_global + 0xB0, 12) == original.read(0x70003B40, 12)
                        count += 1
        native.face_owned(0)
        report['face_alignment_order_callbacks'] = count

        count = 0
        for held in range(17):
            original = ScanOracle(elf)
            original.save(PLAYER + 6, 2, 1)
            original.save(PLAYER + 0x200, 0)
            original.save(PLAYER + 0x240, 0)
            requests = []
            for address in (0x1607D0, 0x160220, 0x174AC0):
                original.calls[address] = lambda o: o.r.__setitem__(2, 0)
            for address in (0x1764E0, 0x175900, 0x1756E0, 0x1796C0):
                original.calls[address] = lambda o: None
            original.calls[0x174A50] = lambda o: requests.append(o.f[12])
            original.run(0x161020, (PLAYER,))
            native.cancel_entry()
            output = (C.c_uint * 5)()
            native.return_state(output)
            assert output[0] == original.load(PLAYER + 6, 1) == 0x63
            assert not requests and list(output)[2:] == [1, bits(56), 0]
            for flags in [0] + [0xFFFF8000] * held + [0]:
                original.save(PLAYER + 0x200, flags)
                original.run(0x161020, (PLAYER,))
                assert native.return_tick(flags)
                native.return_state(output)
                assert output[0] == original.load(PLAYER + 6, 1)
                if output[0] == 1:
                    assert output[1] == original.load(PLAYER + 0x28, 2) == 300
                count += 1
            assert requests == [bits(8)]
            assert not native.return_tick(0)
        report['aborted_entry_callbacks'] = count + 17

        output = (C.c_uint * 6)()
        for clip in (0, 1, 2, 3, 4, 5, 0x15D):
            original = ScanOracle(elf)
            requests = []
            original.calls[0x174A50] = lambda o: requests.append(o.f[12])
            original.run(0x1798D0, (PLAYER,))
            native.use_start(clip, 5)
            assert native.use_reset(output)
            assert list(output) == [0, 0, 0, 0, bits(75 if clip == 0 else 80), 0]
            assert requests == [bits(0)]
            assert all(original.load(PLAYER + offset, 1) == 0 for offset in (5, 6, 0x1F0, 0x25C))
        report['use_reset_cases'] = 7
        count = 0
        for action, phases in ((0, (0, 1, 2, 0x63, 0x64)), (1, (0, 1, 0x63))):
            for phase in phases:
                for fade_wait in (0, 1):
                    for accepted in (0, 1):
                        original = ScanOracle(elf)
                        original.save(PLAYER + 6, phase, 1)
                        original.save(PLAYER + 0x28, 300, 2)
                        original.save(PLAYER + 0x1F0, 1, 1)
                        original.save(0x28A9A0, fade_wait, 2)
                        calls = []
                        def use(o):
                            calls.append('Use')
                            o.r[2] = accepted
                        original.calls[0x160220] = use
                        for address in (0x1607D0, 0x174AC0):
                            original.calls[address] = lambda o: o.r.__setitem__(2, 0)
                        for address in (0x1764E0, 0x175900, 0x1756E0, 0x1796C0,
                                        0x174A50, 0x17BC40, 0x17C030, 0x178B90, 0x17C540):
                            original.calls[address] = lambda o: None
                        original.run(0x161020 if action == 0 else 0x1612D0, (PLAYER,))
                        actual = native.poll_gate(action, phase, fade_wait, accepted)
                        assert actual == len(calls) * 2 + int(bool(calls) and accepted), (
                            action, phase, fade_wait, accepted, actual, calls)
                        count += 1
        report['use_poll_state_gates'] = count
    report['scope'] = 'original state instructions and bounded VU arithmetic; cache matrix shifts are host adaptation'
    output = ROOT / 'build/player_pose_channels/host_reference.json'
    output.write_text(json.dumps(report, indent=2) + '\n')
    print('player pose host original reference PASS:', json.dumps(report))


if __name__ == '__main__':
    main()
