#!/usr/bin/env python3
"""Execute original B910/C030 foot-stop callbacks and their SDK math callees.

The source skeleton evaluator is an explicit input boundary: test cases supply
already evaluated foot-node positions. Clip lookup and initialization calls are
recorded; original arithmetic, branching, vector rotation and stop ticking run
from the user's ELF. The native game never uses these instruction bytes.
"""
import ctypes as C
import json
from pathlib import Path
import random
import struct
import subprocess

from test_interaction_scan_reference import ScanOracle, PLAYER, bits, number, signed

ROOT = Path(__file__).resolve().parents[1]
NODE17, NODE18 = 0x950000, 0x951000


class Original(ScanOracle):
    def plain(self, word):
        op, rt, rd, shift, fn = word >> 26, word >> 16 & 31, word >> 11 & 31, word >> 6 & 31, word & 63
        if op == 0 and fn == 60:
            self.r[rd] = self.r[rt] << (shift + 32) & 0xFFFFFFFFFFFFFFFF
        elif op == 0 and fn == 63:
            self.r[rd] = signed(self.r[rt], 64) >> (shift + 32) & 0xFFFFFFFF
        else:
            super().plain(word)
        self.r[0] = 0


class Stop(C.Structure):
    _fields_ = [('step_x', C.c_float), ('step_z', C.c_float), ('remaining', C.c_float),
                ('tier', C.c_uint), ('active', C.c_int)]


def vector(values):
    return (C.c_float * len(values))(*values)


def main():
    elf = (ROOT.parent / 'Extermination/config/SCUS_971.12').read_bytes()
    folder = ROOT / 'build/player_foot_stop'
    folder.mkdir(parents=True, exist_ok=True)
    library = folder / 'foot_stop.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-fPIC', '-shared', '-Isrc', 'src/game/em_player_foot_stop.c',
                    'src/game/em_camera_rotation.c', '-lm', '-o', str(library)], cwd=ROOT, check=True)
    native = C.CDLL(str(library))
    native.em_player_foot_stop_begin.argtypes = [C.POINTER(Stop), C.c_uint, C.c_float] + [C.POINTER(C.c_float)] * 4
    native.em_player_foot_stop_tick.argtypes = [C.POINTER(Stop), C.c_uint, C.POINTER(C.c_float), C.POINTER(C.c_float)]
    rng = random.Random(0x17B910)
    entries = []
    for tier in (1, 2):
        # 0 and (0, 1): 0017B910 has no lower bound on the clock (walk clamps
        # the halved negative residual to 1, jog uses 10).
        for clock in (0, 1e-6, .001, .25, .5, .999, .9999999, 1, 1.5, 2, 3, 23.999, 24, 24.001, 57.999, 58,
                      58.001, 119):
            if tier == 2 and clock > 45: continue
            entries.append((tier, clock, [250.8, 229.9, 209], [249, 230, 209.2],
                            [252.4, 230, 211.3], [0, .6108653, 0]))
    for _ in range(600):
        tier = rng.choice((1, 2))
        position = [rng.uniform(-1024, 1024) for _ in range(3)]
        feet = [[position[i] + rng.uniform(-8, 8) for i in range(3)] for _ in range(2)]
        entries.append((tier, rng.uniform(0, 120 if tier == 1 else 45), position,
                        *feet, [rng.uniform(-3.14, 3.14) for _ in range(3)]))
    callbacks = 0
    for tier, clock, position, foot17, foot18, euler in entries:
        position, foot17, foot18, euler = map(vector, (position, foot17, foot18, euler))
        clock = number(bits(clock))
        original = Original(elf)
        original.save(PLAYER + 0x25C, tier, 1)
        original.save(PLAYER + 0x3C, bits(clock))
        original.write(PLAYER + 0xB0, bytes(position) + struct.pack('<f', 1))
        original.write(PLAYER + 0xC0, bytes(euler) + struct.pack('<f', 1))
        original.save(0x275B40, PLAYER + 0x110)
        original.save(PLAYER + 0x110 + 17 * 4, NODE17)
        original.save(PLAYER + 0x110 + 18 * 4, NODE18)
        original.write(NODE17 + 0xC0, bytes(foot17))
        original.write(NODE18 + 0xC0, bytes(foot18))
        original.calls[0x1C6DA0] = lambda o: None
        original.calls[0x17B490] = lambda o: o.r.__setitem__(2, tier if o.r[5] == 1 else 4)
        original.calls[0x1C61D0] = lambda o: o.r.__setitem__(2, 120 if tier == 1 else 45)
        requests = []
        original.calls[0x1749A0] = lambda o: requests.append((o.r[5], o.r[6], o.f[12]))
        original.run(0x17B910, (PLAYER,))
        stop = Stop()
        assert native.em_player_foot_stop_begin(C.byref(stop), tier, clock, foot17, foot18, position, euler)
        expected = original.read(PLAYER + 0x260, 12)
        assert bytes(stop)[:12] == expected, (tier, clock, bytes(stop)[:12].hex(), expected.hex())
        assert original.load(PLAYER + 0x1F0, 1) == 5
        assert requests == ([(4, 0, bits(10))] if tier == 2 else [])
        for frame in range(80):
            flags = 0x1000 if frame > 35 else 0
            original.save(PLAYER + 0x200, flags)
            original.save(PLAYER + 0x204, bits(1))
            original.run(0x17C030, (PLAYER,))
            rate = C.c_float(1)
            active = native.em_player_foot_stop_tick(C.byref(stop), flags, position, C.byref(rate))
            assert active == int(original.load(PLAYER + 0x1F0, 1) == 5)
            assert bytes(position) == original.read(PLAYER + 0xB0, 12)
            assert bits(rate.value) == original.load(PLAYER + 0x204)
            assert bits(stop.remaining) == original.load(PLAYER + 0x268)
            callbacks += 1
            if not active:
                assert original.load(PLAYER + 0x25E, 1) == (0x81 if tier == 1 else 0x82)
                break
        else:
            raise AssertionError('foot stop did not finish')
    below_one = sum(1 for entry in entries if entry[1] < 1)
    assert below_one >= 14, below_one
    report = {'entry_cases': len(entries), 'clock_below_one_cases': below_one, 'mode5_callbacks': callbacks,
              'all_scalar_words_exact': True,
              'boundaries': ['Original evaluated foot nodes are caller inputs.',
                             'Native whole-matrix/channel pose accuracy is a separate measured boundary.',
                             'Finite reference arithmetic model, not universal PS2 hardware equivalence.']}
    (folder / 'reference.json').write_text(json.dumps(report, indent=2) + '\n')
    print('player foot-stop original reference PASS:', json.dumps(report))


if __name__ == '__main__':
    main()
