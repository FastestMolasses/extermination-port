#!/usr/bin/env python3
"""Original instruction checks for the player pose host's narrow state events.

182F90 executes its original SDK vector callees. Idle-return callbacks execute
61020 with animation selection and unrelated physics as recorded boundaries.
The legacy stand-in re-seed executes 182DF0's nonzero-2F3 branch with the
channel initializer 1C63E0 and model lookup 1C6150 as recorded boundaries.
The foot-stop begin during an active pose blend executes 0017C030 mode 3 and
its 0017B910 solve (skeleton evaluation, clip lookups and the clip request are
recorded boundaries; the evaluated feet and transition clock are the native
host's). No instruction bytes are embedded or loaded by the native game.
"""
import ctypes as C
import json
from pathlib import Path
import random
import struct
import subprocess
import tempfile

from test_interaction_scan_reference import ScanOracle, PLAYER, bits, number
from test_player_foot_stop_reference import Original as FootOriginal, NODE17, NODE18
from test_point_light_reference import signed

ROOT = Path(__file__).resolve().parents[1]
TARGET = 0x940000
# The player's one pose owner and the translations it reaches (Makefile
# PLAYER_RECORD_POSE_SRC).
RECORD_POSE = ('em_player_record_pose', 'em_pose_host_workers', 'em_player_stage_workers',
               'em_player_floor', 'em_player_reaction', 'em_player_fall',
               'em_owner_services_original', 'em_stream_lanes_original')

BRIDGE = r'''
#include "game/em_player_pose_host.c"
EmGameState g;
static EmTransitionFade fade;
const EmTransitionFade *em_frame_transition(void) { return &fade; }
void em_frame_request_quit(void) { abort(); }
void palette_apply_placement(float *palette, uint32_t count, const float p[3], float yaw) {
    (void)palette; (void)count; (void)p; (void)yaw;
}
static EmPlayerLiveActor actor;
static uint8_t d8106F3, d8106F1, d810707;
static EmPlayerStageScene stage_scene = { .d8106F1 = &d8106F1 };
static EmPlayerStageGlobals stage_globals = { .d810707 = &d810707 };
int load(const char *bank, const char *row0) {
    g.model.bone_count = 22;
    g.status.health = 100;
    return player_pose_load(bank, row0) && player_pose_attach(&actor, &d8106F3, &stage_scene, &stage_globals) &&
           player_pose_opening_release();
}
/* The record at `clip` / `frame` with no transition: anim_clip_arbiter with
 * no blend (+20C = clip, +3C = frames - frame). */
static int seed(unsigned clip, float frame) {
    int result;
    source.flags = 0;
    return em_player_record_pose_arbiter(&source.record, (int)clip, 0, frame, &result) == 0;
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
void face_owned(int owned) { source.acquired = owned; }
int face_to(float yaw, float *out) {
    if (!player_pose_face(yaw)) return 0;
    memcpy(out, g.pos, 12);
    return player_pose_hip(out + 3) && player_pose_script_euler(out + 6);
}
void cancel_entry(void) {
    seed(1, 64);
    player_pose_entry_cancel();
}
int return_tick(unsigned flags) {
    source.flags = flags;
    return player_pose_entry_return_tick();
}
void return_state(unsigned *out) {
    out[0] = source.idle_return ? source.idle_return : 1;
    out[1] = source.idle_count;
    player_pose_source(out + 2, (float *)(out + 3), NULL, (int *)(out + 4));
}
void use_start(unsigned clip, float frame) {
    seed(clip, frame);
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
int legacy_cycle(float health, unsigned *out) {
    seed(2, 10); /* a jog source */
    g.status.health = health;
    g.loco_mode = 1; g.loco_tier = 2;
    player_pose_legacy_hold("oracle stand-in");
    int released = player_pose_legacy_release();
    memset(out, 0, 7 * sizeof *out);
    out[0] = released;
    player_pose_source(out + 1, (float *)(out + 2), NULL, (int *)(out + 3));
    out[4] = g.loco_mode; out[5] = source.idle_phase; out[6] = source.idle_count;
    g.status.health = 100;
    player_pose_legacy_release();
    return released;
}
int bank_has(unsigned clip) {
    int32_t frames;
    return bank_clip(clip) && em_player_record_pose_frames(&source.record, (int)clip, &frames) == 0 &&
           frames > 0;
}
/* Seed from_clip, request to_clip with a blend, advance `ticks` ordinary
 * rate-1 callbacks, then run the mode-3 foot-stop begin at `pos` / `yaw`.
 * out: the feet 17/18 its skeleton evaluation left (node +C0), the clock and
 * transition flag it read, then step x/z, stop remaining, clip after,
 * transition flag/clock after. */
int foot_blend(unsigned tier, unsigned from_clip, float from_frame, unsigned to_clip,
               float to_frame, unsigned blend, unsigned ticks, const float *pos,
               float yaw, float *out) {
    int result;
    player_pose_legacy_release();
    if (!seed(from_clip, from_frame) ||
        em_player_record_pose_arbiter(&source.record, (int)to_clip, (float)blend, to_frame, &result) < 0)
        return -1;
    for (unsigned i = 0; i < ticks; ++i)
        if (!record_advance(1)) return -1;
    out[6] = current_remaining();
    out[7] = (float)in_transition();
    memcpy(g.pos, pos, 12);
    g.yaw = yaw;
    g.loco_tier = tier;
    g.loco_mode = 3;
    source.foot_stop.active = 0;
    result = player_pose_foot_stop_begin();
    memcpy(out, source.record.nodes + EM_POSE_NODE_BYTES * 17 + 0xC0, 12);
    memcpy(out + 3, source.record.nodes + EM_POSE_NODE_BYTES * 18 + 0xC0, 12);
    out[8] = source.foot_stop.step_x;
    out[9] = source.foot_stop.step_z;
    out[10] = source.foot_stop.remaining;
    out[11] = (float)current_clip();
    out[12] = (float)in_transition();
    out[13] = current_remaining();
    source.foot_stop.active = source.foot_display = 0;
    g.loco_mode = g.loco_tier = 0;
    return result;
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
                        str(ROOT / 'src/game/em_camera_rotation.c'),
                        str(ROOT / 'src/game/em_effect_original.c'),
                        *[str(ROOT / 'src/game' / (name + '.c')) for name in RECORD_POSE],
                        '-lm', '-o', str(library)], check=True)
        native = C.CDLL(str(library))
        native.load.argtypes = [C.c_char_p, C.c_char_p]
        native.align_input.argtypes = [C.POINTER(C.c_float)] * 3
        native.align_to.argtypes = [C.POINTER(C.c_float)] * 2
        native.face_to.argtypes = [C.c_float, C.POINTER(C.c_float)]
        native.return_state.argtypes = [C.POINTER(C.c_uint)]
        native.use_start.argtypes = [C.c_uint, C.c_float]
        native.use_reset.argtypes = [C.POINTER(C.c_uint)]
        assert native.load(str(ROOT / 'assets/player_clips_full.bank').encode(),
                           str(ROOT / 'assets/player_clip_row0.emch').encode())
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

        native.legacy_cycle.argtypes = [C.c_float, C.POINTER(C.c_uint)]
        for row in (0, 1):
            original = ScanOracle(elf)
            original.save(PLAYER + 0x2F3, 1, 1)
            original.save(PLAYER + 0x235, row, 1)
            original.save(PLAYER + 4, 3, 1)
            original.save(PLAYER + 5, 0x1D, 1)
            original.save(PLAYER + 0x1F0, 0x31, 1)
            seeded = []
            original.calls[0x1C6150] = lambda o: o.r.__setitem__(2, 21)
            original.calls[0x1C63E0] = lambda o: seeded.append(signed(o.r[5] & 0xFFFF, 16))
            original.run(0x182DF0, (PLAYER,))
            assert len(seeded) == 1
            assert [original.load(PLAYER + offset, 1) for offset in (4, 5, 6, 0x1F0, 0x2F3)] == \
                [1, 0, 0, 0, 0]
            output = (C.c_uint * 7)()
            if row == 0:
                assert native.legacy_cycle(100.0, output) == 1
                assert list(output) == [1, seeded[0], bits(80), 0, 0, 0, 300], list(output)
            else:
                # Row1's default is in the bank, but the host holds: the +235
                # low-health latch that selects row 1 is not ported.
                assert seeded[0] == 0x0A and native.bank_has(seeded[0])
                assert native.legacy_cycle(35.0, output) == 0 and output[1] == 0
            report['legacy_reseed_row%d_clip' % row] = seeded[0]
        native.foot_blend.argtypes = [C.c_uint, C.c_uint, C.c_float, C.c_uint, C.c_float,
                                      C.c_uint, C.c_uint, C.POINTER(C.c_float), C.c_float,
                                      C.POINTER(C.c_float)]
        blend_cases = []
        # (tier, from clip/frame, to clip/frame, blend ticks, ticks advanced)
        for tier in (1, 2):
            for ticks in range(0, 8):
                blend_cases.append((tier, 0, 20.0, tier, 64.0 if tier == 1 else 10.0, 8, ticks))
            blend_cases.append((tier, 2 if tier == 1 else 1, 12.0, tier, 30.0, 4, 1))
            blend_cases.append((tier, 3, 5.0, tier, 7.0, 12, 5))
        for _ in range(40):
            tier = random_source.choice((1, 2))
            blend = random_source.choice((4, 6, 8, 10, 12, 16))
            blend_cases.append((tier, random_source.choice((0, 1, 2, 3)),
                                float(random_source.randrange(0, 40)), tier,
                                float(random_source.randrange(0, 40)), blend,
                                random_source.randrange(0, blend - 1)))
        count = 0
        for tier, from_clip, from_frame, to_clip, to_frame, blend, ticks in blend_cases:
            position = vector([random_source.uniform(-512, 512) for _ in range(3)])
            yaw = number(bits(random_source.uniform(-3.14, 3.14)))
            out = (C.c_float * 14)()
            result = native.foot_blend(tier, from_clip, from_frame, to_clip, to_frame, blend,
                                       ticks, position, yaw, out)
            assert result == 1 and out[7] == 1, (tier, from_clip, to_clip, ticks, result)
            original = FootOriginal(elf)
            original.save(PLAYER + 0x1F0, 3, 1)             # 0017C030 mode 3
            original.save(PLAYER + 0x25C, tier, 1)
            original.save(PLAYER + 0x2C, 0x8000, 2)          # transition bit set
            original.save(PLAYER + 0x200, 0xFFFF8000)        # blend flags
            original.save(PLAYER + 0x3C, bits(out[6]))       # transition clock
            original.write(PLAYER + 0xB0, bytes(position) + struct.pack('<f', 1))
            original.write(PLAYER + 0xC0, struct.pack('<4f', 0, yaw, 0, 1))
            original.save(0x275B40, PLAYER + 0x110)
            original.save(PLAYER + 0x110 + 17 * 4, NODE17)
            original.save(PLAYER + 0x110 + 18 * 4, NODE18)
            original.write(NODE17 + 0xC0, bytes(out)[0:12])
            original.write(NODE18 + 0xC0, bytes(out)[12:24])
            original.calls[0x1C6DA0] = lambda o: None
            original.calls[0x17B490] = lambda o, t=tier: o.r.__setitem__(
                2, t if o.r[5] == 1 else 4)
            original.calls[0x1C61D0] = lambda o, t=tier: o.r.__setitem__(
                2, 120 if t == 1 else 45)
            requests = []
            original.calls[0x1749A0] = lambda o: requests.append((o.r[5], o.r[6], o.f[12]))
            original.run(0x17C030, (PLAYER,))
            assert original.load(PLAYER + 0x1F0, 1) == 5
            assert bytes(out)[32:44] == original.read(PLAYER + 0x260, 12), (
                tier, ticks, list(out))
            assert requests == ([(4, 0, bits(10))] if tier == 2 else [])
            if tier == 2:
                assert out[11] == 4 and out[12] == 1 and out[13] == 10
            else:
                assert out[11] == 1
            count += 1
        report['foot_stop_during_blend_cases'] = count

    report['scope'] = 'original state instructions and bounded VU arithmetic; cache matrix shifts are host adaptation'
    output = ROOT / 'build/player_pose_channels/host_reference.json'
    output.write_text(json.dumps(report, indent=2) + '\n')
    print('player pose host original reference PASS:', json.dumps(report))


if __name__ == '__main__':
    main()
