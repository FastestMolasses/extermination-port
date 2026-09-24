#!/usr/bin/env python3
"""Execute the original footstep draws against the native shared stream.

SI-02/AM-17: footstep_rand5 must be 00179B90 over the shared func_00122BB8
stream (em_random_next). The original 00182430 mapper runs from the user's
ELF with its two 00179B90 draws executed; func_00122BB8 receives the same
values the native em_random.c stream produces, and 001FBD50 is a recorded
boundary. No instruction bytes are embedded in or loaded by the native game.

Retired with census L01: the flinch-entry part (the port's copy of 0021D800
case 0 and 0021D1A0 in em_player_damage.c, reached only from its deleted
copy of 0021C440). The original stage now enters the +4 = 2 reaction states
itself (em_player_stage_workers.c; the states are
em_player_reaction.c's, verified by test_player_reaction_reference.py).
"""
import ctypes as C
import json
import re
from pathlib import Path
import subprocess
import tempfile

from test_interaction_scan_reference import ScanOracle, PLAYER

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

    output = ROOT / 'build/player_pose_channels/random_reference.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + '\n')
    print('player random original reference PASS:', json.dumps(report))


if __name__ == '__main__':
    main()
