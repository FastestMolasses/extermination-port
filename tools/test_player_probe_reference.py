#!/usr/bin/env python3
"""Execute the original radial wall probes and compare em_player_floor.c.

WP-15 P16. The user's pinned ELF supplies every instruction and table; none
are embedded here. The bounded interpreter (test_player_floor_reference Floor,
built on test_player_reversal_reference Reversal) runs, unmodified:

  001764E0  radial probes          00176390  probe response
  00176BE0  slide response         001762E0  area-2 target gate
  00176C80  crawl-space test       001760C0  column probe set-up
  001756E0  clearance release      0019A310  slope angle
  and the SDK vector routines 001029C0/00102BB0/001029E8/00102918/001026A0,
  001B1470.

Hooked boundaries, scripted per case and recorded (never simulated):
0019AD00 / 0019AFE0 / 0019AB20 probes (each writes the case's hit record into
the original scratchpad result block), 00176180 hull shove, 00174A50 pose
request, and the SDK transcendental calls 0011E748 sqrt / 0011DBB8 atan /
0011DF78 fabs (host models on both sides).

Cases: random synthetic actors, and the captured first-control actor bytes
(collision_run_poll.json) as the starting state with the probe results that
reproduce each frame's captured obstruction byte.
"""
import ctypes as C
import hashlib
import json
import random
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from test_player_floor_reference import (Floor, ProbeHit, LIBC, ACTOR, NODE, ENTITY, LINK,  # noqa: E402
                                         DECOMP, REFERENCE)
from test_player_reversal_reference import ELF_SHA256  # noqa: E402
import ee_float_model as M  # noqa: E402
from test_point_light_reference import bits, number, fp  # noqa: E402

PROBES, RESPONSE, CRAWL, RELEASE = 0x1764E0, 0x176390, 0x176C80, 0x1756E0
MOVE, SWEEP, COLUMN_PROBE = 0x19AD00, 0x19AFE0, 0x19AB20
HULL_SHOVE, POSE = 0x176180, 0x174A50
SQRT, ATAN, FABS = 0x11E748, 0x11DBB8, 0x11DF78


class ProbeActor(C.Structure):
    _fields_ = [('position', C.c_float * 3), ('yaw', C.c_float), ('speed', C.c_float),
                ('major', C.c_uint8), ('state', C.c_uint8), ('mode', C.c_uint8),
                ('variant', C.c_uint8), ('row', C.c_uint8), ('special', C.c_uint8),
                ('obstruction', C.c_uint8), ('contact', C.c_uint8), ('link', C.c_uint8),
                ('link_type', C.c_uint8)]


class ProbeScene(C.Structure):
    _fields_ = [('area', C.c_uint8), ('inherited_s1', C.c_uint8),
                ('previous_obstruction', C.c_uint8)]


MOVE_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_float), C.POINTER(C.c_float), C.c_uint,
                      C.POINTER(ProbeHit))
SWEEP_FN = MOVE_FN
COLUMN_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_float), C.c_float, C.POINTER(ProbeHit))
SHOVE_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_float))
GATE_FN = C.CFUNCTYPE(C.c_int, C.c_void_p)
POSE_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_float)
MATH1_FN = C.CFUNCTYPE(C.c_float, C.c_void_p, C.c_float)


class ProbeWorkers(C.Structure):
    _fields_ = [('context', C.c_void_p), ('move', MOVE_FN), ('sweep', SWEEP_FN),
                ('column', COLUMN_FN), ('hull_shove', SHOVE_FN), ('target_shove', GATE_FN),
                ('pose', POSE_FN), ('sqrt', MATH1_FN), ('atan', MATH1_FN)]


FIELDS = (('yaw', 0xC4, 4, True), ('speed', 0x38, 4, True), ('major', 4, 1, False),
          ('state', 5, 1, False), ('mode', 0x1F0, 1, False), ('variant', 0x1F1, 1, False),
          ('row', 0x235, 1, False), ('special', 0x236, 1, False), ('obstruction', 0x314, 1, False),
          ('contact', 0xA, 1, False))


class Probe(Floor):
    def __init__(self, elf, ram=None, base=ACTOR):
        super().__init__(elf, ram)
        self.base = base
        self.calls = {
            MOVE: lambda o: o.scripted('move', (o.load(o.base + 0xB0), o.load(o.base + 0xB8)),
                                       o.vector(o.r[5]), o.r[6]),
            SWEEP: lambda o: o.scripted('sweep', o.vector(o.r[5]), o.vector(o.r[6]), o.r[7]),
            COLUMN_PROBE: lambda o: o.scripted('column', o.vector(o.r[5]), o.load(o.r[6] + 4), o.r[7]),
            HULL_SHOVE: lambda o: o.record_call('hull_shove', o.vector(o.r[6])),
            POSE: lambda o: o.record_call('pose', o.f[12]),
            SQRT: lambda o: o.ret_float(LIBC.sqrtf(number(o.f[12]))),
            ATAN: lambda o: o.ret_float(LIBC.atanf(number(o.f[12]))),
            FABS: lambda o: o.ret_float(abs(number(o.f[12]))),
        }

    def plain(self, word):
        op, rs, rt, rd, fn = word >> 26, word >> 21 & 31, word >> 16 & 31, word >> 11 & 31, word & 63
        if op == 0 and fn in (4, 6):   # sllv / srlv
            amount = self.r[rs] & 31
            value = self.r[rt] & 0xffffffff
            self.r[rd] = (value << amount) & 0xffffffff if fn == 4 else value >> amount
            self.r[0] = 0
            return
        super().plain(word)

    def scripted(self, name, *entry):
        self.log.append((name,) + entry)
        self.probe('scripted')


def build_native():
    out = ROOT / 'build/player_probe_reference'
    out.mkdir(parents=True, exist_ok=True)
    lib = out / ('floor.dylib' if sys.platform == 'darwin' else 'floor.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-shared', '-fPIC', '-Isrc', 'src/game/em_player_floor.c', '-lm', '-o', str(lib)],
                   cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    native.em_player_wall_probes.argtypes = [C.POINTER(ProbeActor), C.POINTER(ProbeScene),
                                             C.POINTER(ProbeWorkers)]
    native.em_player_clearance_release.argtypes = [C.POINTER(ProbeActor), C.POINTER(ProbeScene),
                                                   C.POINTER(ProbeWorkers)]
    native.em_player_crawl_ahead.argtypes = [C.POINTER(ProbeActor), C.POINTER(ProbeWorkers)]
    native.em_player_sdk_wrap.argtypes = [C.c_float]; native.em_player_sdk_wrap.restype = C.c_float
    native.em_player_sdk_lane_point.argtypes = [C.c_float, C.c_float, C.POINTER(C.c_float),
                                                C.POINTER(C.c_float), C.POINTER(C.c_float)]
    return out, native


class NativeProbes:
    def __init__(self, hits):
        self.queue = list(hits)
        self.log = []
        self.target_shoves = 0
        self.workers = ProbeWorkers(None, MOVE_FN(self.move), SWEEP_FN(self.sweep),
                                    COLUMN_FN(self.column), SHOVE_FN(self.hull_shove),
                                    GATE_FN(self.target_shove), POSE_FN(self.pose),
                                    MATH1_FN(lambda _, x: LIBC.sqrtf(x)),
                                    MATH1_FN(lambda _, x: LIBC.atanf(x)))

    @staticmethod
    def vec(v): return tuple(bits(v[i]) for i in range(3))

    def take(self, out):
        hit = self.queue.pop(0)
        C.memmove(out, C.byref(hit), C.sizeof(ProbeHit))
        return hit.kind

    def move(self, _, position, target, mask, out):
        self.log.append(('move', (bits(position[0]), bits(position[2])), self.vec(target), mask))
        return self.take(out)

    def sweep(self, _, start, end, mask, out):
        self.log.append(('sweep', self.vec(start), self.vec(end), mask))
        return self.take(out)

    def column(self, _, at, height, out):
        # 001760C0's add.s (at.y + height), in the measured EE model.
        start = (bits(at[0]), M.ee_add(bits(at[1]), bits(height)), bits(at[2]))
        self.log.append(('column', start, bits(height), 6))
        return self.take(out)

    def hull_shove(self, _, target):
        self.log.append(('hull_shove', self.vec(target)))
        return 0

    def target_shove(self, _):
        self.target_shoves += 1
        return 0

    def pose(self, _, blend):
        self.log.append(('pose', bits(blend)))
        return 0


def load(oracle, base, actor, scene):
    for name, offset, size, is_float in FIELDS:
        value = getattr(actor, name)
        oracle.save(base + offset, bits(value) if is_float else value, size)
    for i in range(3):
        oracle.save(base + 0xB0 + 4 * i, bits(actor.position[i]))
    oracle.save(base, 1, 1)               # byte0 bit0 (001762E0's shove gate)
    oracle.save(base + 0x214, LINK if actor.link else 0)
    oracle.save(LINK + 3, actor.link_type, 1)
    oracle.save(0x810700, scene.area, 1)
    oracle.save(0x275B04, scene.previous_obstruction, 1)
    oracle.r[17] = scene.inherited_s1


def compare(oracle, base, actor, scene, native_log, label):
    expected = oracle.log
    assert expected == native_log.log, (label, expected, native_log.log)
    for name, offset, size, is_float in FIELDS:
        raw = oracle.load(base + offset, size)
        value = getattr(actor, name)
        got = bits(value) if is_float else value
        assert got == raw, (label, name, hex(got), hex(raw))
    for i in range(3):
        assert bits(actor.position[i]) == oracle.load(base + 0xB0 + 4 * i), (label, 'position', i)
    shoved = oracle.load(base, 1) == 2
    assert shoved == (native_log.target_shoves > 0), (label, 'target shove')


def run_probes(elf, native, actor, scene, hits, ram=None, base=ACTOR, entry=PROBES):
    oracle = Probe(elf, ram, base)
    load(oracle, base, actor, scene)
    oracle.probes = list(hits)
    oracle.call(entry, base)
    log = NativeProbes(hits)
    if entry == PROBES:
        assert native.em_player_wall_probes(C.byref(actor), C.byref(scene), C.byref(log.workers)) == 0
        assert scene.previous_obstruction == oracle.load(0x275B04, 1), 'D_00275B00[4]'
    else:
        answer = native.em_player_clearance_release(C.byref(actor), C.byref(scene), C.byref(log.workers))
        assert answer == oracle.r[2], (answer, oracle.r[2])
    compare(oracle, base, actor, scene, log, (hex(entry),))
    assert len(oracle.probes) == len(log.queue), ('consumed hits', len(oracle.probes), len(log.queue))
    return oracle, log


def random_hit(rng, kind=None):
    hit = ProbeHit()
    hit.kind = rng.choice([0, 0, 0, 2, 4, 4, 1]) if kind is None else kind
    hit.node = rng.choice([0x2000, 0x2000, 0x2003, 0x1000, 0x0800, 0x4000, 0x8000, 0x2005,
                           rng.randrange(65536)])
    hit.entity = rng.choice([0, 0, 1])
    hit.entity_flags = rng.choice([2, 4, 0x22, 0xE4, 5, rng.randrange(256)])
    hit.entity_type = rng.choice([2, 0x54, 6, rng.randrange(256)])
    ny = rng.choice([0.0, 0.1, 0.3, 0.35, 0.37, 0.5, -0.3, rng.uniform(-1, 1)])
    hit.normal[0], hit.normal[1], hit.normal[2] = rng.uniform(-1, 1), ny, rng.uniform(-1, 1)
    for i in range(3):
        hit.point[i] = rng.uniform(-300, 300)
        hit.delta[i] = rng.choice([0.0, rng.uniform(-0.5, 0.5)])
    return hit


def random_actor(rng):
    actor = ProbeActor()
    for i in range(3):
        actor.position[i] = rng.uniform(-300, 300)
    actor.yaw = rng.choice([rng.uniform(-3.2, 3.2), 0.0, 0.610865295, -3.1415927, 3.1415927])
    actor.speed = rng.choice([0.0, 0.3, 0.8, -0.1])
    actor.major = rng.choice([1, 1, 1, 2, 4])
    actor.state = rng.choice([1, 1, 0, 0x1E, 0x1D, rng.randrange(40)])
    actor.mode = rng.choice([0, 1, 2, 0x36, 0x37, 0x3E, 0x38, rng.randrange(256)])
    actor.variant = rng.choice([0, 1, 3])
    actor.row = rng.randrange(4)
    actor.special = rng.choice([0, 0, 0, 1])
    actor.obstruction = rng.randrange(256)
    actor.contact = rng.choice([0, 1, 0x81])
    actor.link = rng.choice([0, 1])
    actor.link_type = rng.choice([6, 2, rng.randrange(256)])
    if not actor.link: actor.contact &= 0x7F
    return actor


def main():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA256, 'wrong original executable'
    out, native = build_native()
    rng = random.Random(0x1764E0)
    result = {}

    # 1. SDK wrap and lane points against the original routines.
    lanes = 0
    for case in range(3000):
        yaw = rng.choice([rng.uniform(-10, 10), 0.0, 3.1415927, -3.1415927, 1.5707964, -0.0,
                          number(bits(3.1415927) + 1)])
        oracle = Probe(elf); oracle.f[12] = bits(yaw); oracle.call(0x1B1470)
        assert bits(native.em_player_sdk_wrap(yaw)) == oracle.f[0], yaw
        offset = rng.choice([0.0, 0.7853982, -0.7853982, 1.5707964, 3.1415927, rng.uniform(-3, 3)])
        position = [rng.uniform(-400, 400) for _ in range(3)]
        local = [0.0, rng.choice([0.05, 4.01, 18.0, 0.0]), rng.choice([4.5, 2.0, 10.0, 0.0]), 1.0]
        yaw, offset = number(bits(yaw)), number(bits(offset))
        position = [number(bits(v)) for v in position]; local = [number(bits(v)) for v in local]
        oracle = Probe(elf)
        # The caller's add.s of yaw and offset, in the measured EE model (as
        # em_player_sdk_lane_point computes it through em_ee_float.h).
        wrapped_sum = M.ee_add(bits(yaw), bits(offset))
        oracle.f[12] = wrapped_sum; oracle.call(0x1B1470); angle = oracle.f[0]
        oracle.call(0x1029C0, 0x700036A0)
        oracle.f[12] = angle; oracle.call(0x102BB0, 0x700036A0, 0x700036A0)
        for i in range(3): oracle.save(0x6D0000 + 4 * i, bits(position[i]))
        oracle.call(0x102918, 0x700036A0, 0x700036A0, 0x6D0000)
        for i in range(4): oracle.save(0x6D0100 + 4 * i, bits(local[i]))
        oracle.call(0x1026A0, 0x700038A0, 0x700036A0, 0x6D0100)
        got = (C.c_float * 4)()
        native.em_player_sdk_lane_point(yaw, offset, (C.c_float * 3)(*position),
                                        (C.c_float * 4)(*local), got)
        for i in range(4):
            assert bits(got[i]) == oracle.load(0x700038A0 + 4 * i), (case, i, yaw, offset)
        lanes += 1
    result['sdk_lane_cases'] = lanes

    # 2. 001764E0 over random states and scripted probe results.
    counts = {}
    for case in range(2500):
        actor = random_actor(rng)
        scene = ProbeScene(rng.choice([0xB, 0xB, 2, 0x12]), rng.choice([1, 1, 4, 5, 0x51, 0]),
                           rng.randrange(256))
        hits = [random_hit(rng) for _ in range(80)]
        oracle, log = run_probes(elf, native, actor, scene, hits)
        for entry in log.log:
            counts[entry[0]] = counts.get(entry[0], 0) + 1
        if log.target_shoves: counts['target_shove'] = counts.get('target_shove', 0) + 1
    for kind in ('move', 'sweep', 'column', 'hull_shove', 'target_shove'):
        assert counts.get(kind, 0) > 20, (kind, counts)
    result['wall_probe_cases'] = 2500; result['wall_probe_calls'] = counts

    # 3. 001756E0.
    counts = {}
    for case in range(2500):
        actor = random_actor(rng)
        if case % 3 == 0:
            actor.special = 1
        if case % 2 == 0:
            actor.major, actor.state = 1, 0
        if case % 7 == 0:
            actor.contact = 0x81; actor.link = 1; actor.link_type = 6
            actor.position[1] = rng.choice([165.0, 164.99, 200.0])
        scene = ProbeScene(rng.choice([0xB, 0x12, 0x12]), 1, 0)
        hits = [random_hit(rng) for _ in range(10)]
        oracle, log = run_probes(elf, native, actor, scene, hits, entry=RELEASE)
        for entry in log.log:
            counts[entry[0]] = counts.get(entry[0], 0) + 1
    assert counts.get('pose', 0) > 20 and counts.get('column', 0) > 100, counts
    result['clearance_release_cases'] = 2500; result['clearance_release_calls'] = counts

    # 4. Captured first-control actor bytes: every frame's state, with the
    # obstruction lanes that frame recorded answered by a wall hit (zero
    # delta) and every other probe clear. The native lane order and the
    # original's must agree on which lanes set +314.
    capture = json.loads((REFERENCE / 'collision_run_poll.json').read_text())
    finals = {}
    for row in capture['rows']:
        finals[row['frame']] = bytes.fromhex(row['actor_hex'])
    captured = 0
    for frame, raw in sorted(finals.items()):
        base = 0x8102B0
        ram = bytearray(0x900000); ram[base:base + len(raw)] = raw
        actor = ProbeActor()
        for name, offset, size, is_float in FIELDS:
            value = int.from_bytes(raw[offset:offset + size], 'little')
            setattr(actor, name, number(value) if is_float else value)
        for i in range(3):
            actor.position[i] = struct.unpack_from('<f', raw, 0xA0 + 4 * i)[0]
        actor.link = 0
        lanes = raw[0x314]
        scene = ProbeScene(0xB, 1, 0)
        probe_oracle = Probe(elf, bytes(ram), base); load(probe_oracle, base, actor, scene)
        # Each chest sweep of a recorded lane hits a wall at its end; every
        # other probe is clear. Build the answer list by running the original
        # with a responder, then replay it against both sides.
        answers = []

        def responder(oracle_self, name):
            index = sum(1 for e in oracle_self.log if e[0] == 'sweep') - 1
            hit = ProbeHit()
            if name == 'sweep' and lanes >> index & 1:
                hit.kind = 4; hit.node = 0x2000
                for i in range(3): hit.point[i] = number(oracle_self.log[-1][2][i])
            answers.append(hit)
            return hit
        probe_oracle.probes = []
        original_probe = probe_oracle.probe
        def probe(name, o=probe_oracle):
            o.probes = [responder(o, o.log[-1][0])]
            original_probe(name)
        probe_oracle.probe = probe
        probe_oracle.call(PROBES, base)
        oracle, log = run_probes(elf, native, actor, scene, answers, ram=bytes(ram), base=base)
        assert actor.obstruction == lanes, (frame, actor.obstruction, lanes)
        captured += 1
    result['captured_frames'] = captured

    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print('player probe original-instruction PASS', json.dumps(result))


if __name__ == '__main__':
    main()
