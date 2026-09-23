#!/usr/bin/env python3
"""Execute the original footstep and flinch draws against the native shared stream.

SI-02/AM-17: footstep_rand5 must be 00179B90 over the shared func_00122BB8
stream (em_random_next). The original 00182430 mapper runs from the user's
ELF with its two 00179B90 draws executed; func_00122BB8 receives the same
values the native em_random.c stream produces, and 001FBD50 is a recorded
boundary. No instruction bytes are embedded in or loaded by the native game.

The flinch entry 0021D800 case 0 runs with its callees 0021D1A0 (side test,
including the original SDK atan2/fabs/angle wrap) and 0021D600 executed;
func_00122BB8, the sound call 001FBD50, the clip request 001749A0 and the
rumble 001B61C0 are recorded boundaries. The native entry must draw once,
pick the same clip and play the same sounds. The side test is also checked
over arbitrary hit vectors, although the port's producers do not yet supply
one (the native entry uses the 001AF5C0 spawn value (0, 1)).
"""
import ctypes as C
import json
import math
import random
import re
from pathlib import Path
import subprocess
import tempfile

from test_interaction_scan_reference import ScanOracle, PLAYER
from test_item_sdk_math_reference import Original as SdkBase
from test_pose_transition_reference import add as guard_add
from test_point_light_reference import bits, number

ROOT = Path(__file__).resolve().parents[1]

BRIDGE = r'''
#include "game/em_player.h"
#include "game/em_random.h"
EmGameState g;
const float kLocoTierSpeed[4];
static unsigned played[8], played_count;
void em_sfx_play_at(unsigned id, const float pos[3], float radius) {
    (void)pos; (void)radius;
    if (played_count < 8) played[played_count] = id;
    ++played_count;
}
unsigned step(int tier, unsigned *out) {
    played_count = 0;
    footstep_play(tier);
    for (unsigned i = 0; i < played_count && i < 8; ++i) out[i] = played[i];
    return played_count;
}
'''


DAMAGE_BRIDGE = r'''
#include "game/em_player_damage.c"
EmGameState g;
static int armed;
static unsigned played[8], played_count, requested[4], requested_count;
int em_weapon_state(void) { return armed ? 1 : EM_WPN_HOLSTERED; }
void em_sfx_play_at(unsigned id, const float pos[3], float radius) {
    (void)pos; (void)radius;
    if (played_count < 8) played[played_count] = id;
    ++played_count;
}
int em_game_anim_request(unsigned clip, float rate) {
    (void)rate;
    if (requested_count < 4) requested[requested_count] = clip;
    ++requested_count;
    return 1;
}
unsigned side(float x, float z, float yaw) {
    const float hit[2] = { x, z };
    return player_flinch_side(hit, yaw);
}
/* out: clip, sound count, sounds[0..3], request count */
void flinch(int is_armed, int infected, int inf_hit, float yaw, unsigned *out) {
    memset(&g, 0, sizeof g);
    armed = is_armed;
    g.pd_infected = infected;
    g.pd_inf_hit = inf_hit;
    g.yaw = yaw;
    played_count = requested_count = 0;
    player_enter_flinch();
    out[0] = g.pd_clip;
    out[1] = played_count;
    for (unsigned i = 0; i < 4; ++i) out[2 + i] = i < played_count ? played[i] : 0;
    out[6] = requested_count;
}
'''


def build(folder, name, text, sources):
    source = Path(folder) / (name + '.c')
    source.write_text(text)
    library = Path(folder) / (name + '.dylib')
    # Unrelated callees get aborting stubs so a reached one fails loudly.
    command = ['cc', '-std=c11', '-O2', '-ffp-contract=off', '-fPIC', '-shared',
               '-I' + str(ROOT / 'src'), str(source)] + [str(ROOT / s) for s in sources] + [
               '-lm', '-o', str(library)]
    probe = subprocess.run(command, capture_output=True, text=True)
    missing = sorted(set(re.findall(r'"_(\w+)", referenced from', probe.stderr)))
    stubs = Path(folder) / (name + '_stubs.c')
    stubs.write_text('#include <stdlib.h>\n' + ''.join(
        'void %s(void);\nvoid %s(void) { abort(); }\n' % (m, m) for m in missing))
    subprocess.run(command + [str(stubs)], check=True)
    return C.CDLL(str(library))


class SdkOriginal(SdkBase):
    """add.s/sub.s use the captured-reference single-guard-bit model that
    em_pose_math.h pose_add/pose_sub implement (see
    test_pose_transition_reference.py); everything else is unchanged."""
    def plain(self, word):
        op, rs, fn = word >> 26, word >> 21 & 31, word & 63
        if op == 17 and rs == 16 and fn in (0, 1):
            a, b = number(self.f[word >> 11 & 31]), number(self.f[word >> 16 & 31])
            self.f[word >> 6 & 31] = bits(guard_add(a, b if fn == 0 else -b))
            self.r[0] = 0
        else:
            super().plain(word)


def original_flinch(elf, value, armed, infected, inf_hit, hit, yaw):
    original = SdkOriginal(elf)
    original.save(0x24295C, 0x950000)          # SDK atan2 error context
    original.save(PLAYER + 6, 0, 1)             # case 0: the entry callback
    original.save(PLAYER + 0x1F1, inf_hit, 1)
    original.save(PLAYER + 0x234, infected, 1)
    original.save(PLAYER + 0x236, armed, 1)
    original.save(PLAYER + 0x70, bits(hit[0]))
    original.save(PLAYER + 0x78, bits(hit[1]))
    original.save(PLAYER + 0xC4, bits(yaw))
    draws, sounds, clips, rumble = [], [], [], []

    def draw(o):
        draws.append(value)
        o.r[2] = value
    original.calls[0x122BB8] = draw
    original.calls[0x1FBD50] = lambda o: sounds.append(o.r[5] & 0xFFFF)
    original.calls[0x1749A0] = lambda o: clips.append(o.r[5] & 0xFFFF)
    original.calls[0x1B61C0] = lambda o: rumble.append(tuple(o.r[4:8]))
    original.run(0x21D800, (PLAYER,))
    assert len(draws) == 1 and len(clips) == 1 and rumble == [(0, 0xC0, 5, 1)]
    return clips[0], sounds


def original_side(elf, hit, yaw, host_atan2=None):
    original = SdkOriginal(elf)
    original.save(0x24295C, 0x950000)
    if host_atan2:
        # Declared boundary for the decision-edge sweep: the native side test
        # uses host atan2f, which can differ from SDK 0011E620 by one ulp.
        original.calls[0x11E620] = lambda o: o.f.__setitem__(
            0, bits(host_atan2(number(o.f[12]), number(o.f[13]))))
    original.save(PLAYER + 0x70, bits(hit[0]))
    original.save(PLAYER + 0x78, bits(hit[1]))
    original.save(PLAYER + 0xC4, bits(yaw))
    original.run(0x21D1A0, (PLAYER,))
    return original.r[2] & 0xFFFFFFFF


def stream(state, count):
    values = []
    for _ in range(count):
        state = (state * 0x41C64E6D + 0x3039) & 0xFFFFFFFF
        values.append(state & 0x7FFFFFFF)
    return values


def main():
    elf = (ROOT.parent / 'Extermination/config/SCUS_971.12').read_bytes()
    report = {}
    with tempfile.TemporaryDirectory(prefix='player_random_reference_') as folder:
        source = Path(folder) / 'bridge.c'
        source.write_text(BRIDGE)
        library = Path(folder) / 'player.dylib'
        # Only footstep_play/footstep_rand5 run. Unrelated em_player.c callees
        # get aborting stubs so a reached one fails loudly instead of linking.
        command = ['cc', '-std=c11', '-O2', '-ffp-contract=off', '-fPIC', '-shared',
                   '-I' + str(ROOT / 'src'), str(source),
                   str(ROOT / 'src/game/em_player.c'), str(ROOT / 'src/game/em_random.c'),
                   '-lm', '-o', str(library)]
        probe = subprocess.run(command, capture_output=True, text=True)
        missing = sorted(set(re.findall(r'"_(\w+)", referenced from', probe.stderr)))
        stubs = Path(folder) / 'stubs.c'
        stubs.write_text('#include <stdlib.h>\n' + ''.join(
            'void %s(void);\nvoid %s(void) { abort(); }\n' % (name, name) for name in missing))
        subprocess.run(command + [str(stubs)], check=True)
        native = C.CDLL(str(library))
        native.em_random_seed.argtypes = [C.c_uint32]
        native.footstep_rand5.restype = C.c_uint
        native.step.argtypes = [C.c_int, C.POINTER(C.c_uint)]
        native.step.restype = C.c_uint

        seed = 0x45
        native.em_random_seed(seed)
        values = stream(seed, 4096)
        for value in values:
            original = ScanOracle(elf, (value,))
            original.run(0x179B90)
            assert original.rng_calls == 1
            assert native.footstep_rand5() == original.r[2], hex(value)
        report['rand5_draws'] = len(values)

        state = 0x187350
        native.em_random_seed(state)
        count = 0
        for tier in (1, 2, 3) * 200:
            pair = stream(state, 2)
            state = (state * 0x41C64E6D + 0x3039) & 0xFFFFFFFF
            state = (state * 0x41C64E6D + 0x3039) & 0xFFFFFFFF
            original = ScanOracle(elf, pair)
            original.save(PLAYER + 0x23A, 0, 1)  # native attr 0 without collision
            requested = []
            original.calls[0x1FBD50] = lambda o: requested.append(o.r[5] & 0xFFFF)
            original.run(0x182430, (PLAYER, tier))
            assert original.rng_calls == 2
            output = (C.c_uint * 8)()
            assert native.step(tier, output) == 2
            assert list(output)[:2] == requested, (tier, [hex(v) for v in pair],
                                                   list(output)[:2], requested)
            count += 1
        report['footstep_mapper_steps'] = count

        damage = build(folder, 'damage', DAMAGE_BRIDGE,
                       ['src/game/em_random.c'])
        damage.em_random_seed.argtypes = [C.c_uint32]
        damage.side.argtypes = [C.c_float] * 3
        damage.side.restype = C.c_uint
        damage.flinch.argtypes = [C.c_int, C.c_int, C.c_int, C.c_float, C.POINTER(C.c_uint)]
        rng = random.Random(0x21D1A0)
        half = number(bits(math.pi / 2))
        sides = []
        for x, z in ((0, 1), (1, 0), (0, -1), (-1, 0), (0, 0), (0.6, 0.8)):
            for yaw in (0, half, -half, math.pi, -math.pi, 3.1415927, 1e-7, -1e-7):
                sides.append(((x, z), yaw))
        for _ in range(3000):
            angle = rng.uniform(-math.pi, math.pi)
            length = rng.choice((1.0, rng.uniform(0.01, 50)))
            sides.append(((math.sin(angle) * length, math.cos(angle) * length),
                          rng.uniform(-math.pi, math.pi)))
        for hit, yaw in sides:
            hit = [number(bits(v)) for v in hit]
            yaw = number(bits(yaw))
            assert damage.side(hit[0], hit[1], yaw) == original_side(elf, hit, yaw), (hit, yaw)
        report['flinch_side_cases_full_sdk'] = len(sides)
        # Yaw a few float steps either side of the +-pi/2 decision edges,
        # where one atan2 ulp can flip the result. The wrap/add/compare
        # arithmetic is checked exactly with atan2 as the host boundary;
        # SDK-versus-host atan2 disagreements are counted, not hidden.
        host = C.CDLL(None)
        host.atan2f.argtypes = [C.c_float, C.c_float]
        host.atan2f.restype = C.c_float
        edges = atan2_flips = 0
        for _ in range(150):
            angle = rng.uniform(-math.pi, math.pi)
            hit = [number(bits(v)) for v in (math.sin(angle), math.cos(angle))]
            for edge in (angle + math.pi / 2, angle - math.pi / 2):
                edge = math.remainder(edge, 2 * math.pi)
                for step in range(-3, 4):
                    yaw = number(bits(edge) + step)
                    native_side = damage.side(hit[0], hit[1], yaw)
                    assert native_side == original_side(elf, hit, yaw, host.atan2f), (hit, yaw)
                    atan2_flips += native_side != original_side(elf, hit, yaw)
                    edges += 1
        report['flinch_side_edge_cases_host_atan2'] = edges
        report['flinch_side_edge_sdk_atan2_flips'] = atan2_flips

        state = 0x21D800
        damage.em_random_seed(state)
        count = 0
        for case in range(400):
            armed, infected, inf_hit = case & 1, case >> 1 & 1, case >> 2 & 1
            yaw = number(bits(rng.uniform(-math.pi, math.pi)))
            value = stream(state, 1)[0]
            state = (state * 0x41C64E6D + 0x3039) & 0xFFFFFFFF
            clip, sounds = original_flinch(elf, value, armed, infected, inf_hit, (0.0, 1.0), yaw)
            output = (C.c_uint * 7)()
            damage.flinch(armed, infected, inf_hit, yaw, output)
            assert output[0] == clip and output[6] == 1, (case, list(output), clip)
            assert list(output)[2:2 + output[1]] == sounds and output[1] == len(sounds), (
                case, list(output), sounds)
            count += 1
        report['flinch_entries'] = count
    output = ROOT / 'build/player_pose_channels/random_reference.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + '\n')
    print('player random original reference PASS:', json.dumps(report))


if __name__ == '__main__':
    main()
