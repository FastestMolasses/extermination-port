#!/usr/bin/env python3
"""Execute the original 00176180 (the player's class-2 hull shove) and
compare src/game/em_player_hull_shove.c.

The user's pinned ELF supplies every instruction; none are embedded here.
The bounded interpreter of tools/test_player_probe_reference.py (Probe)
runs, unmodified: 00176180 with the SDK 00102738, 00102760, 00102948 and
001028B8. Hooked boundaries (scripted per case, recorded, compared): the
0019AFE0 sweep (it writes the case's hit record into the scratchpad result
block) and 0011E748 (a host sqrt on both sides; its result is unused).

Each case stages the actor bytes (+0xB0 position with a w word, +4, +5,
+0x1F0), D_00281B50 (the first hit's delta with a w word), the target and
0x700031F0, and compares the actor's +0xB0 / +0xB4 / +0xB8, 0x700031F0 and
the sweep call (its from / to vectors and mask). Cases: +0x1F0 0x3C (no
shove) and others, +4 / +5 at and around (1, 0x1D), zero, tiny and large
deltas, sweeps that miss and hit with their deltas.

Default run: 600 cases (about 2 s); EM_TEST_FULL=1: 6,000.
"""
import ctypes as C
import random
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from test_player_floor_reference import ProbeHit, LIBC, DECOMP, cached_build  # noqa: E402
from test_player_probe_reference import (Probe, ProbeActor, ProbeScene, SWEEP_FN, MATH1_FN, HULL_SHOVE,  # noqa: E402
                                         load)
from player_callback_oracle import ELF_SHA256  # noqa: E402
from test_point_light_reference import bits, number  # noqa: E402
import reference_mode  # noqa: E402
import hashlib  # noqa: E402

BASE = 0x680000           # the actor (test_player_floor_reference ACTOR)
TARGET = 0x6D0200         # the target vector (00176390's a2)
D_00281B50 = 0x281B50
CARRY = 0x700031F0


class ShoveWorkers(C.Structure):
    _fields_ = [('context', C.c_void_p), ('sweep', SWEEP_FN), ('sqrt', MATH1_FN),
                ('carry31F0', C.POINTER(C.c_int32))]


class Oracle(Probe):
    def __init__(self, elf):
        super().__init__(elf, None, BASE)
        del self.calls[HULL_SHOVE]       # executed, not a boundary


def build_native():
    out = ROOT / 'build/player_hull_shove_reference'
    out.mkdir(parents=True, exist_ok=True)
    lib = out / ('shove.dylib' if sys.platform == 'darwin' else 'shove.so')
    cached_build(lib, ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared',
                       '-fPIC', '-Isrc', 'src/game/em_player_hull_shove.c', 'src/game/em_effect_original.c',
                       '-lm', '-o', str(lib)])
    native = C.CDLL(str(lib))
    native.em_player_00176180.argtypes = [C.POINTER(ProbeActor), C.POINTER(C.c_float), C.POINTER(C.c_float),
                                          C.POINTER(ShoveWorkers)]
    return native


def f(v):
    return number(bits(v))


def case_values(rng):
    actor = ProbeActor()
    for i in range(3):
        actor.position[i] = f(rng.uniform(-300, 300))
    actor.major = rng.choice([1, 1, 1, 2, 0])
    actor.state = rng.choice([0x1D, 0x1D, 0x1C, 0x1E, 1, 0x9D])
    actor.mode = rng.choice([0x3C, 0, 1, 0x36, 0x3D, 0x3B, rng.randrange(256)])
    pick = rng.random()
    if pick < 0.1:
        delta = [0.0, 0.0, 0.0]
    elif pick < 0.25:
        delta = [rng.choice([0.0, rng.uniform(-1e-3, 1e-3)]) for _ in range(3)]
    else:
        delta = [rng.uniform(-3, 3) for _ in range(3)]
    delta = [f(v) for v in delta]
    target = [f(actor.position[0] + rng.uniform(-5, 5)), f(rng.uniform(-300, 300)),
              f(actor.position[2] + rng.uniform(-5, 5))]
    hit = ProbeHit()
    hit.kind = rng.choice([0, 0, 1, 2, 4])
    hit.node = rng.choice([0x2000, 0x4000, 0x8000, 0x1000])
    for i in range(3):
        hit.point[i] = f(rng.uniform(-300, 300))
        hit.delta[i] = f(rng.choice([0.0, rng.uniform(-0.5, 0.5)]))
    words = (rng.choice([0, 0x3F800000, rng.getrandbits(32) & 0x3FFFFFFF]),
             rng.choice([0, 0x3F800000, rng.getrandbits(32) & 0x3FFFFFFF]))
    carry = rng.choice([0, 0, 5])
    return actor, delta, target, hit, words, carry


def run_case(elf, native, values):
    actor, delta, target, hit, (w_pos, w_delta), carry = values
    oracle = Oracle(elf)
    load(oracle, BASE, actor, ProbeScene(0xB, 1, 0))
    oracle.save(BASE + 0xBC, w_pos)
    for i in range(3):
        oracle.save(D_00281B50 + 4 * i, bits(delta[i]))
        oracle.save(TARGET + 4 * i, bits(target[i]))
    oracle.save(D_00281B50 + 12, w_delta)
    oracle.save(TARGET + 12, bits(1.0))
    oracle.save(CARRY, carry)
    oracle.probes = [hit]
    oracle.call(HULL_SHOVE, BASE, 1, TARGET)

    log = []
    queue = [hit]

    def sweep(_, start, end, mask, out):
        log.append(('sweep', tuple(bits(start[i]) for i in range(3)), tuple(bits(end[i]) for i in range(3)), mask))
        h = queue.pop(0)
        C.memmove(out, C.byref(h), C.sizeof(ProbeHit))
        return h.kind
    sweep_fn = SWEEP_FN(sweep)
    sqrt_fn = MATH1_FN(lambda _, x: LIBC.sqrtf(x))
    cell = C.c_int32(carry)
    workers = ShoveWorkers(None, sweep_fn, sqrt_fn, C.pointer(cell))
    r = native.em_player_00176180(C.byref(actor), (C.c_float * 3)(*delta), (C.c_float * 3)(*target),
                                  C.byref(workers))
    assert r == 0, ('native fault', r)
    assert log == oracle.log, ('sweep call', oracle.log, log)
    for i in range(3):
        assert bits(actor.position[i]) == oracle.load(BASE + 0xB0 + 4 * i), ('position', i, values)
    assert cell.value == oracle.load(CARRY), ('0x700031F0', cell.value, oracle.load(CARRY))
    return bool(oracle.log), cell.value != carry, oracle.log and hit.kind != 0


def main():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA256, 'wrong original executable'
    native = build_native()
    rng = random.Random(0x176180)
    total = 6000 if reference_mode.FULL else 600
    shoved = carried = swept_hit = 0
    for n in range(total):
        values = case_values(rng)
        if n < 8:      # the carry store's gate, explicitly
            values[0].mode, values[0].major, values[0].state = 0, (1, 1, 2, 0, 1, 1, 1, 1)[n], \
                (0x1D, 0x1C, 0x1D, 0x1D, 0x1D, 0x1E, 0x9D, 0x1D)[n]
        s, c, h = run_case(elf, native, values)
        shoved += s; carried += c; swept_hit += bool(h)
    assert shoved and carried and swept_hit and shoved < total, (shoved, carried, swept_hit)
    print(f'player hull shove (00176180) original-instruction PASS: {total} cases, {shoved} shoves '
          f'({swept_hit} with a sweep hit), {carried} carry-flag stores')
    reference_mode.banner(reference_mode.part(total, 6000, 'cases'))


if __name__ == '__main__':
    main()
