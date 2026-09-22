#!/usr/bin/env python3
"""Original wrapper control flow for player pose acquisition and release.

The initializer remains an explicit ordered call boundary here. Separate pose
oracles cover its channel operations and captured idle channels. This test also
executes the original SDK float-to-integer routine used by source-frame reset.
"""
import ctypes as C
from pathlib import Path
import random
import subprocess
import tempfile

from test_pose_transition_reference import Original as Base, bits, number, signed
from test_interaction_animation_reference import Original as Clock, NODE

ROOT = Path(__file__).resolve().parents[1]
ACTOR, RETURN = 0x600000, 0xBADF00D
CLIPS = (0, 1, 2, 3, 4, 5, 0x40, 0x41, 0x42, 0x43, 0x45, 0x47, 0x15C, 0x15D)
LENGTHS = {0: 80, 1: 120, 2: 45, 3: 40, 4: 20, 5: 10, 0x40: 45, 0x41: 45, 0x42: 45,
           0x43: 150, 0x45: 150, 0x47: 200, 0x15C: 121, 0x15D: 180}


class Original(Base):
    def plain(self, word):
        op, rs, rt, rd = word >> 26, word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        address = (self.r[rs] + signed(word & 65535, 16)) & 0xFFFFFFFF
        fn = word & 63
        if op == 0 and fn in (6, 7, 38, 45):
            if fn == 6: self.r[rd] = (self.r[rt] & 0xFFFFFFFF) >> (self.r[rs] & 31)
            elif fn == 7: self.r[rd] = signed(self.r[rt]) >> (self.r[rs] & 31) & 0xFFFFFFFF
            elif fn == 38: self.r[rd] = self.r[rs] ^ self.r[rt]
            else: self.r[rd] = (self.r[rs] + self.r[rt]) & 0xFFFFFFFFFFFFFFFF
        elif op == 11: self.r[rt] = int((self.r[rs] & 0xFFFFFFFF) < (signed(word & 65535, 16) & 0xFFFFFFFF))
        elif op == 14: self.r[rt] = self.r[rs] ^ (word & 65535)
        elif op == 55: self.r[rt] = self.get(address, 8)
        elif op == 63: self.put(address, self.r[rt], 8)
        else: super().plain(word)
        self.r[0] = 0

    def run(self, entry, args=(ACTOR,), floats=()):
        for i, value in enumerate(args): self.r[4 + i] = value
        for i, value in enumerate(floats): self.f[12 + i] = bits(value)
        self.r[31] = RETURN
        self.calls = []
        pc = entry
        for _ in range(4000):
            if pc == RETURN: return self.r[2]
            if pc == 0x17B490:
                assert self.r[4] == ACTOR and self.r[6] == 0
                assert self.r[5] in (0, 1)
                self.r[2] = self.r[7] if self.r[5] == 1 else 0
                pc = self.r[31]
                continue
            if pc == 0x1C61D0:
                self.r[2] = LENGTHS[self.r[5] & 65535]
                pc = self.r[31]
                continue
            if pc in (0x179D20, 0x179FF0, 0x102958, 0x1C9D50):
                # Matrix generation has no clip-clock side effects. Its
                # displayed tier blend is not used as a source channel pose.
                pc = self.r[31]
                continue
            if pc == 0x182D40:
                self.r[2] = 0
                pc = self.r[31]
                continue
            if pc == 0x1C67E0:
                self.calls.append((self.r[5] & 65535, number(self.f[12]), number(self.f[13])))
                clip, blend, frame = self.calls[-1]
                self.put(ACTOR + 0x3C, bits(blend or LENGTHS[clip] - int(frame)))
                pc = self.r[31]
                continue
            assert (0x1749A0 <= pc < 0x174B20 or 0x182DF0 <= pc < 0x182F90 or
                    0x128250 <= pc < 0x128300 or 0x1278C0 <= pc < 0x127970 or
                    0x17B660 <= pc < 0x17B910), hex(pc)
            word = self.get(pc)
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            offset = signed(word & 65535, 16) * 4
            branch = None
            delay = True
            if op in (4, 5, 20, 21):
                taken = (self.r[rs] == self.r[rt]) == (op in (4, 20))
                branch = pc + 4 + offset if taken else pc + 8
                delay = taken or op < 20
            elif op == 1:
                assert rt in (0, 1)
                taken = signed(self.r[rs]) < 0 if rt == 0 else signed(self.r[rs]) >= 0
                branch = pc + 4 + offset if taken else pc + 8
            elif op == 7: branch = pc + 4 + offset if signed(self.r[rs]) > 0 else pc + 8
            elif op == 17 and rs == 8:
                branch = pc + 4 + offset if self.condition == bool(rt & 1) else pc + 8
            elif op in (2, 3):
                if op == 3: self.r[31] = pc + 8
                branch = (word & 0x3FFFFFF) << 2
            elif op == 0 and word & 63 == 8: branch = self.r[rs] & 0xFFFFFFFF
            if branch is not None:
                if delay: self.plain(self.get(pc + 4))
                pc = branch
            else:
                try: self.plain(word)
                except AssertionError as error: raise AssertionError(hex(pc), error) from error
                pc += 4
        raise AssertionError('Original player pose wrapper did not return')


BRIDGE = r'''
#include "game/em_player_pose.h"
#include <string.h>
static EmPoseBank bank;
static EmPlayerPose pose;
int load(const char *path) { return em_pose_bank_load(&bank, path); }
void close_bank(void) { em_pose_bank_free(&bank); }
int start(unsigned clip, float frame) { return em_player_pose_init(&pose, &bank, clip, frame); }
int acquire(void) { return em_player_pose_acquire(&pose); }
int release(void) { pose.acquired = 1; return em_player_pose_release(&pose); }
int select_clip(unsigned clip, unsigned blend, int force) {
    return em_player_pose_select(&pose, clip, 0, blend, force);
}
int gait(unsigned tier, unsigned substate, float blend) {
    return em_player_pose_gait_base(&pose, tier, substate, blend);
}
int advance(float rate) { return em_player_pose_advance(&pose, rate, 0); }
void state(unsigned *out) {
    out[0] = pose.playback.clip->id;
    out[1] = pose.transition.active;
    memcpy(out + 2, pose.transition.active ? &pose.transition.remaining : &pose.playback.remaining, 4);
    out[3] = pose.flags;
}
'''


def main():
    elf = (ROOT.parent / 'Extermination/config/SCUS_971.12').read_bytes()
    randomizer = random.Random(0x174A50)
    checks = 0
    with tempfile.TemporaryDirectory(prefix='player_pose_wrapper_') as folder:
        source = Path(folder) / 'bridge.c'
        source.write_text(BRIDGE)
        library = Path(folder) / 'pose.dylib'
        subprocess.run(['cc', '-shared', '-fPIC', '-std=c11', '-O2', '-ffp-contract=off',
                        '-I' + str(ROOT / 'src'), str(source),
                        str(ROOT / 'src/game/em_player_pose.c'),
                        str(ROOT / 'src/game/em_pose_bank.c'),
                        str(ROOT / 'src/game/em_pose_transition.c'), '-lm', '-o', str(library)], check=True)
        native = C.CDLL(str(library))
        native.load.argtypes = [C.c_char_p]
        native.start.argtypes = [C.c_uint, C.c_float]
        native.select_clip.argtypes = [C.c_uint, C.c_uint, C.c_int]
        native.gait.argtypes = [C.c_uint, C.c_uint, C.c_float]
        native.advance.argtypes = [C.c_float]
        native.state.argtypes = [C.POINTER(C.c_uint)]
        assert native.load(str(ROOT / 'assets/player_channels.empc').encode())
        output = (C.c_uint * 4)()
        for clip in CLIPS:
            for release in (False, True):
                original = Original(elf)
                original.put(ACTOR + 0x20C, clip, 2)
                original.put(ACTOR + 0x235, 0, 1)
                original.run(0x182DF0 if release else 0x174A50, floats=() if release else (8,))
                assert native.start(clip, 5)
                assert (native.release if release else native.acquire)()
                native.state(output)
                assert output[0] == original.get(ACTOR + 0x20C, 2) == 0
                if original.calls:
                    requested, blend, frame = original.calls[-1]
                    assert requested == 0 and frame == 0
                    assert output[1] == bool(blend)
                    assert output[2] == bits(blend or 80)
                else:
                    assert list(output) == [0, 0, bits(75), 0]
                checks += 1
        for _ in range(200):
            old, requested = randomizer.choice(CLIPS), randomizer.choice(CLIPS)
            force, blend = randomizer.randrange(2), randomizer.choice((0, 1, 8, 16))
            original = Original(elf)
            original.put(ACTOR + 0x20C, old, 2)
            original.run(0x1749A0, (ACTOR, requested, force), (blend,))
            assert native.start(old, 5)
            assert native.select_clip(requested, blend, force)
            native.state(output)
            assert output[0] == original.get(ACTOR + 0x20C, 2)
            assert bool(original.calls) == bool(force or old != requested)
            assert bool(output[1]) == bool(original.calls and blend)
            checks += 1
        source_bank = (ROOT.parent / 'Extermination/extract/chunk28/f01_id3c.bin').read_bytes()
        for clip in CLIPS:
            for blend in (0, 1, 8, 16):
                original = Clock(elf, source_bank)
                original.put(ACTOR + 0x20C, clip, 2)
                original.put(ACTOR + 0x2C, clip | (0x8000 if blend else 0), 2)
                original.put(ACTOR + 0x3C, bits(blend or LENGTHS[clip]))
                original.put(NODE + 0x8E, 0, 2)
                original.sample_frame = 0
                assert native.start(clip, 0)
                assert native.select_clip(clip, blend, 1)
                for _ in range(90):
                    rate = randomizer.choice((.25, .5, .75, 1, 1.5, 1.75, 2, 3.25))
                    flags = original.run(0x1C64F0, floats=(rate,)) & 0xFFFFFFFF
                    assert native.advance(rate)
                    native.state(output)
                    assert output[2] == original.get(ACTOR + 0x3C), (clip, blend, rate, list(output))
                    assert output[3] == flags, (clip, blend, rate, hex(output[3]), hex(flags))
                    assert bool(output[1]) == bool(original.get(ACTOR + 0x2C, 2) & 0x8000)
                    checks += 1
        for _ in range(1000):
            old = randomizer.randrange(4)
            tier = randomizer.randrange(1, 4)
            substate = randomizer.randrange(3)
            if substate == 1 and tier == 3: substate = 2
            blend = randomizer.choice((0, .125, .25, .5, .999, 1))
            frame = number(bits(randomizer.uniform(0, LENGTHS[old] - 1)))
            original = Original(elf)
            original.put(ACTOR + 0x20C, old, 2)
            original.put(ACTOR + 0x3C, bits(LENGTHS[old] - frame))
            original.put(ACTOR + 0xC, 1, 1)
            original.put(ACTOR + 0x235, 0, 1)
            original.put(ACTOR + 0x1F1, substate, 1)
            original.put(ACTOR + 0x25C, tier, 1)
            original.put(ACTOR + 0x208, bits(blend))
            assert native.start(old, frame)
            native.state(output)
            # Begin seeds remaining with the recovered add/sub model.
            original.put(ACTOR + 0x3C, output[2])
            original.run(0x17B660)
            assert native.gait(tier, substate, blend)
            native.state(output)
            assert output[0] == original.get(ACTOR + 0x20C, 2), (old, tier, substate, blend, frame)
            assert output[2] == original.get(ACTOR + 0x3C), (old, tier, substate, blend, frame, output[2], original.get(ACTOR + 0x3C))
            checks += 1
        native.close_bank()
    # Original SDK helper, including its decode callee, not a host math hook.
    original = Original(elf)
    for value in [0, .25, .5, .999, 1, 5.75, 27.9, 64.125, 65534.75] + [
            randomizer.uniform(0, 65535) for _ in range(1000)]:
        value = number(bits(value))
        assert original.run(0x128250, (), (value,)) == int(value)
        checks += 1
    print('player pose original wrapper PASS', checks,
          'conditional init/release/source-frame conversions')


if __name__ == '__main__':
    main()
