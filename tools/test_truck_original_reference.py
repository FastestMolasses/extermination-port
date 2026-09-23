#!/usr/bin/env python3
"""Execute the original AREA11 truck (0x823FF0) and camera trigger (0x8251E0).

The user's own ELF and AREA11.BIN are executed by the bounded MIPS/VU0
oracle; the native em_truck_original module must produce the same owner
fields, the same canonical bytes and the same ordered worker calls. The
oracle also asserts that the original writes nothing outside the modeled
fields. No original instruction bytes or data are stored in this file.

Worker boundaries (intercepted in the oracle, hooks in the native module):
1B0FD0 model bind, 1C6380 placement matrix, 102958 into the bone-0 matrix
(pose), 1A2370 hull, 1B1B70 publish, +0x4C draw, 1B1E20 rumble, 1EFD20
effect, 1FBD50 sound, 1AFC10 free, 1BA1A0/1BA1F0 script. The SDK rotations
102B08/102A60/1029E8 and 102958 copies between the actor's own matrices
run as original instructions.

Arithmetic: EE add.s/sub.s use the single-guard-bit model (the PCSX2
capture rejects plain truncation on the arm tick); mul.s and VU0 truncate;
div.s rounds to nearest.

Original PCSX2 validation (docs/TRUCK_ORIGINAL.md):
  --capture truck|trigger   drive the original game from save state 04 with
                            ../Extermination/tools/pcsx2_session.py (run with
                            the decomp .venv python; needs zstandard) and
                            record into ../Extermination/build/s87/truck/
  --compare-capture [DIR]   replay those recordings through the native module
                            and require every recorded field and call
"""
import argparse
import ctypes as C
import hashlib
import itertools
import json
from pathlib import Path
import random
import struct
import subprocess
import sys
import time

from test_pickup_owner_reference import OwnerOracle, ROOT, DECOMP, ELF_SHA
from test_point_light_reference import bits, number, signed
from test_pose_transition_reference import add as ee_add

ACTOR, DRAW, BONE, TABLE, GROUND = 0x910000, 0x990000, 0x9a0000, 0x9b0000, 0x9c0000
POINTER = 0x9d0000
OVERLAY_BASE, TRUCK, TRIGGER = 0x823500, 0x823FF0, 0x8251E0
TRUCK_ACTOR = 0x7A9FB0          # pool node #24 in the playable capture
STACK_LOW, STACK_HIGH = 0x700000-0x100, 0x700000
TEMPORARIES = set(range(0x700038A0, 0x700038B0)) | set(range(0x70003A20, 0x70003A24))
STORY, PHASE, BYTE_0A, GROUND_POINTER = 0x810792, 0x8102B5, 0x8102BA, 0x8104C4
PLAYER_A0, PLAYER_B0, CARRY = 0x810350, 0x810360, 0x700031F0


class TruckOracle(OwnerOracle):
    """Adds div/mfhi (the 0x823FF0 arm) and mult/mflo (validation only).
    EE add.s/sub.s use the single-guard-bit model of
    test_pose_transition_reference (as the fan oracle does); the PCSX2
    capture of this set piece rejects plain truncation on the arm tick.
    VU0 macro arithmetic stays truncating."""
    hi = lo = 0

    def plain(self, word):
        op, fn = word >> 26, word & 63
        rs, rt, rd = word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        if op == 17 and rs == 16 and fn in (0, 1):
            a, b = number(self.f[rd]), number(self.f[rt])
            self.f[word >> 6 & 31] = bits(ee_add(a, b if fn == 0 else -b))
        elif op == 0 and fn == 26:
            a, b = signed(self.r[rs]), signed(self.r[rt])
            assert b != 0
            quotient = abs(a)//abs(b)*(1 if (a < 0) == (b < 0) else -1)
            self.lo, self.hi = quotient & 0xffffffff, (a-quotient*b) & 0xffffffff
        elif op == 0 and fn == 24:
            product = signed(self.r[rs])*signed(self.r[rt])
            self.lo, self.hi = product & 0xffffffff, (product >> 32) & 0xffffffff
            if rd: self.r[rd] = self.lo
        elif op == 0 and fn == 16: self.r[rd] = self.hi
        elif op == 0 and fn == 18: self.r[rd] = self.lo
        else: super().plain(word)
        self.r[0] = 0


def validate_divide(elf):
    """The extension on byte-matched 001F8880: a/6 by mult/mfhi, a%6 by div/mfhi."""
    cases = 0
    for a in list(range(-40, 41))+[0x7fffffff, -0x80000000, 123456789, -987654321]:
        o = TruckOracle(elf); o.save(0x275b40, 0x9e0000)
        quotient = abs(a)//6*(1 if a >= 0 else -1)  # C99 truncating division
        o.save(0x9e0000+quotient*4 & 0xffffffff, 0x1000)
        o.run(0x1F8880, (a & 0xffffffff,))
        assert o.r[2] == (0x1000+((a-quotient*6) << 5)) & 0xffffffff, (a, hex(o.r[2]))
        cases += 1
    return cases


# ---------------------------------------------------------------- native side
class Truck(C.Structure):
    _fields_ = [('state', C.c_uint8), ('freed', C.c_uint8), ('frame', C.c_int16),
                ('position', C.c_float*3), ('rotation_x', C.c_float),
                ('matrix', C.c_float*16), ('rest_matrix', C.c_float*16),
                ('jitter_z', C.c_float), ('jitter_x', C.c_float), ('rest_y', C.c_float),
                ('rest_rotation_x', C.c_float), ('shake', C.c_int32), ('velocity', C.c_float*3)]


class Trigger(C.Structure):
    _fields_ = [('state', C.c_uint8), ('armed', C.c_uint8), ('freed', C.c_uint8)]


class World(C.Structure):
    _fields_ = [('story', C.POINTER(C.c_uint8)), ('player_phase', C.POINTER(C.c_uint8)),
                ('player_0a', C.POINTER(C.c_uint8)), ('ground_kind', C.POINTER(C.c_uint8)),
                ('player_a0', C.POINTER(C.c_float)), ('player_b0', C.POINTER(C.c_float)),
                ('carry', C.POINTER(C.c_int32))]


VOIDP = C.CFUNCTYPE(C.c_int, C.c_void_p)
BIND = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_int))
MATRIX_OUT = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_float))
MATRIX_IN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_float))
RUMBLE = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int)
EFFECT = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.POINTER(C.c_float))
SOUND = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint16, C.c_float)
START = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32)
POLL = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_int))


class Hooks(C.Structure):
    _fields_ = [('context', C.c_void_p), ('model_bind', BIND), ('placement_matrix', MATRIX_OUT),
                ('pose', MATRIX_IN), ('hull', MATRIX_IN), ('hull_bounds', MATRIX_OUT),
                ('publish', VOIDP), ('draw', VOIDP), ('rumble', RUMBLE), ('effect', EFFECT),
                ('sound', SOUND), ('free_owner', VOIDP)]


class TriggerHooks(C.Structure):
    _fields_ = [('context', C.c_void_p), ('script_start', START), ('script_tick', POLL),
                ('free_owner', VOIDP)]


def words(values): return [bits(v) for v in values]


class NativeWorld:
    """Canonical storage the native pointers name."""
    def __init__(self, story, phase, byte_0a, kind, a0, b0, carry=0):
        self.story, self.phase = C.c_uint8(story), C.c_uint8(phase)
        self.byte_0a = C.c_uint8(byte_0a)
        self.kind = None if kind is None else C.c_uint8(kind)
        self.a0, self.b0 = (C.c_float*3)(*a0), (C.c_float*3)(*b0)
        self.carry = C.c_int32(carry)
        self.world = World(C.pointer(self.story), C.pointer(self.phase), C.pointer(self.byte_0a),
                           C.pointer(self.kind) if self.kind is not None else None,
                           self.a0, self.b0, C.pointer(self.carry))


class NativeTruck:
    def __init__(self, lib, placement=None, bounds=(0,)*6, pending=0):
        self.lib, self.events = lib, []
        self.placement, self.bounds, self.pending = placement, bounds, pending
        record = self.events.append
        def bind(_, out): out[0] = self.pending; record(('bind',)); return 1
        def place(_, m):
            for i, v in enumerate(self.placement): m[i] = number(v)
            record(('placement',)); return 1
        def bounds_fn(_, out):
            for i, v in enumerate(self.bounds): out[i] = v
            return 1
        self.hooks = Hooks(None, BIND(bind), MATRIX_OUT(place),
            MATRIX_IN(lambda _, m: record(('pose', words(m[:16]))) or 1),
            MATRIX_IN(lambda _, m: record(('hull', words(m[:16]))) or 1),
            MATRIX_OUT(bounds_fn),
            VOIDP(lambda _: record(('publish',)) or 1), VOIDP(lambda _: record(('draw',)) or 1),
            RUMBLE(lambda _, e: record(('rumble', e)) or 1),
            EFFECT(lambda _, i, p: record(('effect', i, words(p[:4]))) or 1),
            SOUND(lambda _, i, r: record(('sound', i, bits(r))) or 1),
            VOIDP(lambda _: record(('free',)) or 1))

    def tick(self, truck, world):
        del self.events[:]
        return self.lib.em_truck_original_tick(C.byref(truck), C.byref(world.world), C.byref(self.hooks))


# ---------------------------------------------------------------- oracle side
def oracle(elf, overlay):
    o = TruckOracle(elf)
    o.write(OVERLAY_BASE, overlay)
    return o


def truck_calls(o, events, placement, pending):
    record = events.append
    def copy(r):
        for i in range(64): r.save(r.r[4]+i, r.load(r.r[5]+i, 1), 1)
        if r.r[4] == BONE+0x90: record(('pose', [r.load(r.r[5]+4*i) for i in range(16)]))
    def bind(r):
        assert r.r[4] == ACTOR; record(('bind',)); r.r[2] = pending
        if not pending: r.save(ACTOR+4, r.load(ACTOR+4, 1)+1 & 255, 1)
    def place(r):
        assert r.r[4] == ACTOR; record(('placement',))
        for i, v in enumerate(placement): r.save(ACTOR+0xD0+4*i, v)
    def hull(r):
        assert (r.r[4], r.r[5]) == (ACTOR, ACTOR+0xD0)
        record(('hull', [r.load(ACTOR+0xD0+4*i) for i in range(16)]))
    def rumble(r):
        assert r.r[5] == 0; record(('rumble', r.r[4]))
    def effect(r):
        assert r.r[5] == 0x700038A0; record(('effect', r.r[4], [r.load(r.r[5]+4*i) for i in range(4)]))
    def sound(r):
        assert (r.r[4], r.r[6]) == (ACTOR, 0); record(('sound', r.r[5], r.f[12]))
    def expect(name): return lambda r: (r.r[4] == ACTOR or (_ for _ in ()).throw(
        AssertionError((name, hex(r.r[4]))))) and record((name,))
    o.calls.update({0x102958: copy, 0x1B0FD0: bind, 0x1C6380: place, 0x1A2370: hull,
                    0x1B1B70: expect('publish'), DRAW: expect('draw'), 0x1B1E20: rumble,
                    0x1EFD20: effect, 0x1FBD50: sound, 0x1AFC10: expect('free')})


FIELDS = {'state': (4, 1), 'frame': (0x28, 2), 'rotation_x': (0xC0, 4), 'jitter_z': (0x2DC, 4),
          'jitter_x': (0x2E0, 4), 'rest_y': (0x2E4, 4), 'rest_rotation_x': (0x2E8, 4),
          'shake': (0x2EC, 4)}
MODELED = set()
for _offset, _size in list(FIELDS.values())+[(0xB0, 12), (0xD0, 64), (0x1F0, 64)]:
    MODELED |= set(range(ACTOR+_offset, ACTOR+_offset+_size))


def seed_truck(o, truck, raw):
    """Load an actor image, then overwrite every modeled field from the native owner."""
    o.write(ACTOR, raw)
    o.save(ACTOR+0x4C, DRAW)
    o.save(ACTOR+4, truck.state, 1)
    o.save(ACTOR+0x28, truck.frame & 0xffff, 2)
    for i in range(3): o.save(ACTOR+0xB0+4*i, bits(truck.position[i]))
    o.save(ACTOR+0xC0, bits(truck.rotation_x))
    for i in range(16):
        o.save(ACTOR+0xD0+4*i, bits(truck.matrix[i]))
        o.save(ACTOR+0x1F0+4*i, bits(truck.rest_matrix[i]))
    for name in ('jitter_z', 'jitter_x', 'rest_y', 'rest_rotation_x'):
        o.save(ACTOR+FIELDS[name][0], bits(getattr(truck, name)))
    o.save(ACTOR+0x2EC, truck.shake & 0xffffffff)


def seed_world(o, world, bounds):
    o.save(STORY, world.story.value, 1); o.save(PHASE, world.phase.value, 1)
    o.save(BYTE_0A, world.byte_0a.value, 1)
    if world.kind is None: o.save(GROUND_POINTER, 0)
    else: o.save(GROUND_POINTER, GROUND); o.save(GROUND+0xD, world.kind.value, 1)
    for i in range(3):
        o.save(PLAYER_A0+4*i, bits(world.a0[i])); o.save(PLAYER_B0+4*i, bits(world.b0[i]))
    o.save(CARRY, world.carry.value & 0xffffffff)
    o.save(0x275B40, POINTER); o.save(POINTER, BONE)
    uid = o.load(ACTOR+0xE, 2) >> 8 & 0xFF
    o.save(0x70003250, TABLE); o.save(TABLE+uid*4+4, 0x100)
    for i, v in enumerate(bounds): o.save(TABLE+0x100+4*i, bits(v))


def truck_step(elf, overlay, native, truck, world, raw, placement=(0,)*16, bounds=(0,)*6,
               pending=0, label=''):
    """One original call and one native call from the same state; compare all."""
    o = oracle(elf, overlay)
    seed_truck(o, truck, raw); seed_world(o, world, bounds)
    before = dict(o.mem)
    expected = []
    truck_calls(o, expected, placement, pending)
    state0 = truck.state
    o.run(TRUCK, (ACTOR,))
    native.placement, native.bounds, native.pending = placement, bounds, pending
    result = native.tick(truck, world)
    actual = list(native.events)
    freed = ('free',) in expected
    exp = {name: o.load(ACTOR+off, size) for name, (off, size) in FIELDS.items()}
    exp['frame'] = signed(exp['frame'], 16); exp['shake'] = signed(exp['shake'])
    got = {'state': truck.state, 'frame': truck.frame, 'shake': truck.shake,
           **{n: bits(getattr(truck, n)) for n in ('rotation_x', 'jitter_z', 'jitter_x',
                                                    'rest_y', 'rest_rotation_x')}}
    exp['position'] = [o.load(ACTOR+0xB0+4*i) for i in range(3)]
    got['position'] = words(truck.position)
    exp['matrix'] = [o.load(ACTOR+0xD0+4*i) for i in range(16)]
    got['matrix'] = words(truck.matrix)
    exp['rest'] = [o.load(ACTOR+0x1F0+4*i) for i in range(16)]
    got['rest'] = words(truck.rest_matrix)
    exp['story'], got['story'] = o.load(STORY, 1), world.story.value
    exp['player_a0'] = [o.load(PLAYER_A0+4*i) for i in range(3)]
    got['player_a0'] = words(world.a0)
    exp['carry'], got['carry'] = o.load(CARRY), world.carry.value & 0xffffffff
    if state0 == 1:
        exp['velocity'] = [o.load(0x700038A0+4*i) for i in range(3)]
        got['velocity'] = words(truck.velocity)
    if freed:  # 1AFC10 is a worker: the original fields are the pool's to clear
        exp = {'freed': 1}; got = {'freed': truck.freed}
    assert (actual, got, result) == (expected, exp, 0 if freed else 1), dict(
        label=label, state=state0, actual=actual, expected=expected,
        diff={k: (got.get(k), exp.get(k)) for k in set(got) | set(exp) if got.get(k) != exp.get(k)})
    # Nothing outside the modeled fields may change.
    allowed = MODELED | TEMPORARIES | set(range(BONE+0x90, BONE+0xD0)) | set(range(STORY, STORY+1)) \
        | set(range(PLAYER_A0+8, PLAYER_A0+12)) | set(range(CARRY, CARRY+4))
    for address, value in o.mem.items():
        if before.get(address, None) != value and not STACK_LOW <= address < STACK_HIGH:
            assert address in allowed, (label, hex(address))
    return expected


def trigger_case(elf, overlay, lib, state, story, phase, x, z, done):
    o = oracle(elf, overlay)
    o.save(ACTOR+4, state, 1); o.save(ACTOR+0xB, 0x5A, 1)
    o.save(STORY, story, 1); o.save(PHASE, phase, 1)
    o.save(PLAYER_A0, bits(x)); o.save(PLAYER_A0+8, bits(z))
    expected = []
    def start(r):
        assert r.r[4] == ACTOR+0x1F0; expected.append(('start', r.r[5]))
    def poll(r):
        assert r.r[4] == ACTOR; expected.append(('poll',)); r.r[2] = done
    o.calls.update({0x1BA1A0: start, 0x1BA1F0: poll, 0x1AFC10: lambda r: expected.append(('free',))})
    before = dict(o.mem)
    o.run(TRIGGER, (ACTOR,))
    for address, value in o.mem.items():
        if before.get(address) != value and not STACK_LOW <= address < STACK_HIGH:
            assert address in (ACTOR+4, ACTOR+0xB, STORY), hex(address)
    actual = []
    def poll_native(_, out): actual.append(('poll',)); out[0] = done; return 1
    hooks = TriggerHooks(None, START(lambda _, e: actual.append(('start', e)) or 1), POLL(poll_native),
                         VOIDP(lambda _: actual.append(('free',)) or 1))
    world = NativeWorld(story, phase, 0, None, (x, 0, z), (0, 0, 0))
    trigger = Trigger(state, 0x5A, 0)
    result = lib.em_truck_trigger_tick(C.byref(trigger), C.byref(world.world), C.byref(hooks))
    freed = ('free',) in expected
    exp = (expected, o.load(STORY, 1), 0 if freed else 1) + (
        (1,) if freed else (o.load(ACTOR+4, 1), o.load(ACTOR+0xB, 1)))
    got = (actual, world.story.value, result) + (
        (trigger.freed,) if freed else (trigger.state, trigger.armed))
    assert got == exp, dict(state=state, story=story, phase=phase, x=x, z=z, done=done, got=got, exp=exp)


def rotation_case(elf, lib, which, angle, matrix):
    o = TruckOracle(elf); src, dst = 0x9f0000, 0x9f0100
    for i, v in enumerate(matrix): o.save(src+4*i, bits(v))
    o.run(0x102B08 if which == 'x' else 0x102A60, (dst, src), (angle,))
    native_src = (C.c_float*16)(*matrix); native_dst = (C.c_float*16)()
    (lib.em_truck_rotate_x if which == 'x' else lib.em_truck_rotate_z)(native_dst, native_src, C.c_float(angle))
    assert words(native_dst) == [o.load(dst+4*i) for i in range(16)], (which, angle)


def placement_from(raw):
    return [struct.unpack_from('<I', raw, 0xD0+4*i)[0] for i in range(16)]


def native_from(raw):
    t = Truck()
    t.state = raw[4]; t.frame = struct.unpack_from('<h', raw, 0x28)[0]
    t.position[:] = struct.unpack_from('<3f', raw, 0xB0)
    t.rotation_x = struct.unpack_from('<f', raw, 0xC0)[0]
    t.matrix[:] = struct.unpack_from('<16f', raw, 0xD0)
    t.rest_matrix[:] = struct.unpack_from('<16f', raw, 0x1F0)
    t.jitter_z, t.jitter_x, t.rest_y, t.rest_rotation_x = struct.unpack_from('<4f', raw, 0x2DC)
    t.shake = struct.unpack_from('<i', raw, 0x2EC)[0]
    return t


def timeline(elf, overlay, native, raw, bounds, world_at, ticks, label):
    """Run the wedged capture forward; world_at(i, story) -> NativeWorld."""
    truck = native_from(raw); log = []; story = 0
    for i in range(ticks):
        world = world_at(i, story)
        events = truck_step(elf, overlay, native, truck, world, raw, bounds=bounds, label=(label, i))
        log.append(dict(tick=i, state=truck.state, frame=truck.frame, shake=truck.shake,
                        position=list(truck.position), story=world.story.value,
                        events=[e[0] if e[0] not in ('effect', 'sound', 'rumble') else list(e[:2])
                                for e in events if e[0] not in ('pose', 'hull', 'publish', 'draw')]))
        story = world.story.value
    return log


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--capture', choices=('truck', 'trigger'),
                        help='drive PCSX2 (run with the decomp .venv python) and record')
    parser.add_argument('--frames', type=int, default=260)
    parser.add_argument('--compare-capture', type=Path, nargs='?', const=CAPTURE_DIR)
    args = parser.parse_args()
    if args.capture:
        return capture(CAPTURE_DIR, args.capture, args.frames)
    elf = (DECOMP/'config/SCUS_971.12').read_bytes()
    overlay = (DECOMP/'extract/OVERLAY/AREA11.BIN').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    playable = (DECOMP/'build/startup-reference/playable_ee.bin').read_bytes()
    raw = playable[TRUCK_ACTOR:TRUCK_ACTOR+0x300]
    assert raw[2] == 4 and raw[4] == 4 and raw[0xD] == 9, 'playable capture: truck node #24 moved'
    out = ROOT/'build/truck_original_reference'; out.mkdir(parents=True, exist_ok=True)
    library = out/'truck.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                    '-ffp-contract=off', '-Isrc', 'src/game/em_truck_original.c', '-o', str(library)],
                   cwd=ROOT, check=True)
    lib = C.CDLL(str(library))
    lib.em_truck_original_tick.argtypes = [C.POINTER(Truck), C.POINTER(World), C.POINTER(Hooks)]
    lib.em_truck_trigger_tick.argtypes = [C.POINTER(Trigger), C.POINTER(World), C.POINTER(TriggerHooks)]
    lib.em_truck_trigger_bands.argtypes = [C.c_float, C.c_float]
    for name in ('em_truck_rotate_x', 'em_truck_rotate_z'):
        getattr(lib, name).argtypes = [C.POINTER(C.c_float), C.POINTER(C.c_float), C.c_float]
    report = dict(elf_sha256=ELF_SHA, overlay_sha256=hashlib.sha256(overlay).hexdigest())

    report['divide_extension_cases'] = validate_divide(elf)

    rng = random.Random(0x823FF0)
    rotations = 0
    angles = [0.0, -0.0, 0.0005, 0.0015, 0.0025, -0.002, -0.003, 1.0, -1.0, 1.5707963, -1.5707963]
    rest = struct.unpack_from('<16f', raw, 0x1F0)
    for which, angle in itertools.product('xz', angles+[rng.uniform(-1.6, 1.6) for _ in range(40)]):
        for matrix in (rest, [rng.uniform(-400, 400) for _ in range(16)]):
            rotation_case(elf, lib, which, angle, matrix); rotations += 1
    report['rotation_cases'] = rotations

    # Trigger: every state, story, phase and band edge.
    edges_x = [311.99997, 312.0, 312.00003, 318.99997, 319.0, 319.00003, 335.99997, 336.0, 325.0, 300.0]
    edges_z = [389.99997, 390.0, 390.00003, 412.99997, 413.0, 413.00003, 426.99997, 427.0, 420.0, 400.0, 380.0]
    triggers = 0
    for state, story, phase, done in itertools.product((0, 1, 2, 3, 4, 5, 255), (0, 1, 0xFF),
                                                       (0, 1, 2, 255), (0, 1)):
        for x, z in ((325.0, 420.0), (330.0, 400.0), (250.8, 209.0)):
            trigger_case(elf, overlay, lib, state, story, phase, x, z, done); triggers += 1
    for x, z in itertools.product(edges_x, edges_z):
        trigger_case(elf, overlay, lib, 4, 0, 0, x, z, 0); triggers += 1
        assert lib.em_truck_trigger_bands(x, z) in (0, 1)
    for _ in range(300):
        trigger_case(elf, overlay, lib, 4, 0, rng.choice((0, 1, 2)), rng.uniform(305, 340),
                     rng.uniform(385, 432), 0); triggers += 1
    report['trigger_cases'] = triggers

    native = NativeTruck(lib)
    placement = placement_from(raw)
    bounds = (370.0, 150.0, 380.0, 392.0, 180.0, 402.0)
    inside, outside = (380.8, 175.0, 391.1), (250.8, 240.8, 209.0)

    # State 0 init, both story branches and the pending bind.
    inits = 0
    for story, pending in itertools.product((0, 1, 0xFE, 0xFF), (0, 1)):
        truck = native_from(raw); truck.state = 0; truck.shake = 77
        world = NativeWorld(story, 0, 1, 9, inside, inside)
        truck_step(elf, overlay, native, truck, world, raw, placement, bounds, pending, ('init', story))
        inits += 1
    # Rest, free and unlisted states.
    for state in (2, 3, 5, 6, 255):
        truck = native_from(raw); truck.state = state
        truck_step(elf, overlay, native, truck, NativeWorld(0, 0, 1, 9, inside, inside), raw,
                   bounds=bounds, label=('state', state)); inits += 1
    report['state_cases'] = inits

    # Full set piece: standing on the truck throughout, footprint inside.
    # Tick 0 arms (shake 1), ticks 1..45 shake, tick 46 ends the shake and
    # enters state 1; fall beat f runs on tick 47+f; f 119 rests (tick 166).
    def standing(i, story):
        return NativeWorld(story, 0, 1, 9, inside, inside)
    full = timeline(elf, overlay, native, raw, bounds, standing, 175, 'standing')
    report['standing_timeline'] = summary = timeline_summary(full)
    assert summary['arm_tick'] == 0 and summary['fall_tick'] == 46 and summary['rest_tick'] == 166, summary
    assert summary['sounds'] == [[47+8, 0x454], [47+110, 0x455]], summary
    assert summary['rumbles'] == [[0, 0], [47+14, 2], [47+87, 2]], summary
    assert summary['effects'] == 32 and summary['final_story'] == 0xFF, summary

    # Leave right after arming: no later rumbles, no carry.
    def leave(i, story):
        return NativeWorld(story, 0, 1, 9 if i == 0 else None, outside, outside)
    summary = timeline_summary(timeline(elf, overlay, native, raw, bounds, leave, 170, 'leave'))
    assert summary['rumbles'] == [[0, 0]] and summary['effects'] == 32, summary
    report['leave_timeline'] = summary

    # Not armed: byte 0A clear, wrong ground kind, no ground.
    for kind, byte_0a in ((9, 0), (8, 1), (None, 1)):
        def idle(i, story, kind=kind, byte_0a=byte_0a):
            return NativeWorld(story, 0, byte_0a, kind, inside, inside)
        log = timeline(elf, overlay, native, raw, bounds, idle, 12, ('idle', kind, byte_0a))
        assert all(entry['state'] == 4 and entry['shake'] == 0 for entry in log)

    # Randomized worlds, including a wrapped fall counter and a negative shake.
    fuzz = 0
    for trial in range(40):
        truck = native_from(raw)
        truck.state = rng.choice((1, 4))
        truck.frame = rng.choice((0, 7, 8, 13, 14, 86, 87, 109, 110, 118, 119, 32767, -3))
        truck.shake = rng.choice((0, -2, 1, 3, 15, 16, 20, 45, 46))
        for _ in range(12):
            box = [rng.uniform(370, 390) for _ in range(3)] + [rng.uniform(380, 400) for _ in range(3)]
            player = (rng.uniform(365, 405), rng.uniform(150, 200), rng.uniform(375, 405))
            world = NativeWorld(rng.choice((0, 1, 0xFF)), 0, rng.choice((0, 1)),
                                rng.choice((None, 9, 3)), player, player)
            truck_step(elf, overlay, native, truck, world, raw, bounds=box, label=('fuzz', trial))
            fuzz += 1
    report['fuzz_ticks'] = fuzz

    if args.compare_capture:
        report['capture'] = compare_capture(lib, native, args.compare_capture)
        report['trigger_capture'] = compare_trigger_capture(lib, args.compare_capture)

    report['boundaries'] = ['1B0FD0 model bind', '1C6380 placement TRS', '102958 bone-0 pose',
        '1A2370 hull rebuild and its AABB', '1B1B70 list publish', '+0x4C draw',
        '1B1E20 rumble', '1EFD20 effect', '1FBD50 sound', '1AFC10 free',
        '1BA1A0/1BA1F0 camera script 0x8292C0']
    report['source_sha256'] = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in (ROOT/'src/game/em_truck_original.c', ROOT/'src/game/em_truck_original.h', Path(__file__))}
    (out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(f"Original truck: {report['divide_extension_cases']} divide-extension, {rotations} rotation, "
          f"{triggers} trigger, {inits} init/state, {fuzz} fuzz cases PASS")
    s = report['standing_timeline']
    print(f"Original truck timeline: arm tick {s['arm_tick']}, fall tick {s['fall_tick']}, "
          f"rest tick {s['rest_tick']}, {s['effects']} effects, sounds {s['sounds']}, "
          f"rumbles {s['rumbles']} PASS")
    if args.compare_capture:
        c, t = report['capture'], report['trigger_capture']
        print(f"Original PCSX2 truck capture: {c['frames']} frames, {c['events']} worker calls "
              f"({c['effect_positions']} effect positions) match PASS")
        print(f"Original PCSX2 trigger capture: {t['frames']} frames (script start frame "
              f"{t['script_start_frame']}, {t['polls']} polls, freed frame {t['freed_frame']}) match PASS")


def timeline_summary(log):
    arm = next(e['tick'] for e in log if e['shake'] > 0)
    fall = next(e['tick'] for e in log if e['state'] == 1)
    rest = next(e['tick'] for e in log if e['state'] == 2)
    events = [(e['tick'], x) for e in log for x in e['events']]
    return dict(arm_tick=arm, fall_tick=fall, rest_tick=rest,
                sounds=[[t, x[1]] for t, x in events if x[0] == 'sound'],
                rumbles=[[t, x[1]] for t, x in events if x[0] == 'rumble'],
                effects=sum(1 for _, x in events if x[0] == 'effect'),
                final_story=log[-1]['story'], final_position=log[-1]['position'])


# ---------------------------------------------------------------- PCSX2
CAPTURE_DIR = DECOMP/'build/s87/truck'
SITE_KINDS = {0x1EFD20: 'effect', 0x1FBD50: 'sound', 0x1B1E20: 'rumble'}


def call_sites(overlay, start, end):
    """Worker call sites inside one overlay routine, found from its jal words."""
    sites = {}
    for address in range(start, end, 4):
        word = struct.unpack_from('<I', overlay, address-OVERLAY_BASE)[0]
        if word >> 26 == 3 and (word & 0x3ffffff)*4 in SITE_KINDS:
            sites[address] = SITE_KINDS[(word & 0x3ffffff)*4]
    return sites


def capture(directory, mode, frames):
    """Drive the original game (save state 04, first control) through the set
    piece and record, at the truck's own entry and exit, every input and
    output field, plus each effect/sound/rumble call site as it executes."""
    sys.path.insert(0, str(DECOMP/'tools'))
    from pcsx2_session import OriginalSession, SSTATES, LOOP_TOP, FRAME_COUNTER
    overlay = (DECOMP/'extract/OVERLAY/AREA11.BIN').read_bytes()
    sites = call_sites(overlay, TRUCK, TRIGGER)
    directory.mkdir(parents=True, exist_ok=True)
    state = SSTATES/'SCUS-97112 (0AE679AF).04.p2s'
    trigger_actor = 0x7AA2A0
    out = dict(mode=mode, source_state=state.name, sites={hex(k): v for k, v in sites.items()}, frames=[])
    with OriginalSession(state, log_dir=directory/'logs') as s:
        def pause_pc():
            s.debug.call({'cmd': 'resume'})
            while True:
                status = s.debug.call({'cmd': 'status'})
                data = status.get('data', status)
                if data.get('paused'): return int(data['pc'], 16)
                time.sleep(0.001)
        def hexread(address, size): return s.read(address, size).hex()
        def inputs():
            ground = s.u32(GROUND_POINTER)
            table = s.u32(0x70003250)
            uid = (s.u32(TRUCK_ACTOR+0xC) >> 16) >> 8 & 0xFF
            hull = table + s.u32(table+uid*4+4)
            return dict(truck=hexread(TRUCK_ACTOR, 0x300), story=s.read(0x810790, 4)[2],
                        player=hexread(0x8102B0, 0x220), ground=ground,
                        kind=s.read(ground+0xC, 4)[1] if ground else None,
                        bounds=list(s.read_f32(hull, 6)), carry=s.u32(CARRY))
        def outputs():
            return dict(truck=hexread(TRUCK_ACTOR, 0x300), story=s.read(0x810790, 4)[2],
                        player_a0=hexread(PLAYER_A0, 12), carry=s.u32(CARRY),
                        scratch=hexread(0x700038A0, 16))
        s.step(2)
        out['initial'] = dict(trigger=hexread(trigger_actor, 0x300), story=s.read(0x810790, 4)[2])
        if mode == 'truck':
            position = (375.0, 200.0, 391.0)
        else:
            position = (325.0, 235.0, 420.0)
        s.write(PLAYER_A0, struct.pack('<4f', *position, 1.0))
        s.write(PLAYER_B0, struct.pack('<4f', position[0], position[1]+10.9, position[2], 1.0))
        out['teleport'] = position
        breakpoints = [TRUCK, TRIGGER] + sorted(sites)
        for address in breakpoints:
            s.debug.call({'cmd': 'set_breakpoint', 'address': address, 'description': 'truck capture'})
        try:
            for index in range(frames):
                counter = s.u32(FRAME_COUNTER)
                record = dict(index=index, events=[])
                while True:
                    pc = pause_pc()
                    if pc == TRUCK: record['in'] = inputs()
                    elif pc in sites:
                        record['events'].append([sites[pc], hex(pc), hexread(0x700038A0, 12)])
                    elif pc == TRIGGER:
                        record['out'] = outputs()
                        record['trigger_in'] = dict(trigger=hexread(trigger_actor, 0x20),
                                                    player=hexread(0x8102B0, 0xB0), story=s.read(0x810790, 4)[2])
                    elif pc == LOOP_TOP: break
                    else: raise RuntimeError(f'unexpected pause at {pc:#x}')
                if 'in' in record and 'out' not in record: record['out'] = outputs()
                record['end'] = dict(trigger=hexread(trigger_actor, 0x300 if 'trigger_in' in record else 0x20),
                                     story=s.read(0x810790, 4)[2], truck=hexread(TRUCK_ACTOR, 0x30))
                if s.u32(FRAME_COUNTER) != counter+1: raise RuntimeError('frame step skipped')
                out['frames'].append(record)
        finally:
            for address in breakpoints:
                s.debug.call({'cmd': 'remove_breakpoint', 'address': address})
        out['snapshot'] = s.snapshot(directory/f'{mode}_final')
    (directory/f'{mode}_capture.json').write_text(json.dumps(out)+'\n')
    print(f'captured {len(out["frames"])} {mode} frames into {directory}')


def compare_trigger_capture(lib, directory):
    """Replay the trigger capture: band entry, the running camera script
    (the worker answers done on the tick the original left state 1) and the
    free. Script execution itself belongs to the script host."""
    data = json.loads((directory/'trigger_capture.json').read_text())
    frames = [f for f in data['frames'] if 'trigger_in' in f]
    first = bytes.fromhex(frames[0]['trigger_in']['trigger'])
    trigger = Trigger(first[4], first[0xB], 0)
    starts, polls, result = [], [0], 1
    for record in frames:
        before = bytes.fromhex(record['trigger_in']['trigger'])
        player = bytes.fromhex(record['trigger_in']['player'])
        end = bytes.fromhex(record['end']['trigger'])
        assert (trigger.state, trigger.armed) == (before[4], before[0xB]), record['index']
        done = int(before[4] == 1 and end[4] == 3)
        def poll(_, out): polls[0] += 1; out[0] = done; return 1
        hooks = TriggerHooks(None, START(lambda _, e: starts.append((record['index'], e)) or 1), POLL(poll),
                             VOIDP(lambda _: 1))
        world = NativeWorld(record['trigger_in']['story'], player[5], player[0xA], None,
                            struct.unpack_from('<3f', player, 0xA0), (0, 0, 0))
        result = lib.em_truck_trigger_tick(C.byref(trigger), C.byref(world.world), C.byref(hooks))
        assert world.story.value == record['end']['story'], record['index']
        if end[4] == 0 and not any(end[:0xC]):   # 1AFC10 cleared the node
            assert result == 0 and trigger.freed, record['index']
            break
        assert result == 1 and (trigger.state, trigger.armed) == (end[4], end[0xB]), record['index']
        if (record['index'], EM_CAMERA) in starts:
            assert struct.unpack_from('<I', end, 0x1F8)[0] == EM_CAMERA
    assert [e for _, e in starts] == [EM_CAMERA] and trigger.freed
    return dict(frames=len(frames), script_start_frame=starts[0][0], polls=polls[0],
                freed_frame=record['index'])


EM_CAMERA = 0x8292C0


def compare_capture(lib, native, directory):
    """Replay the recorded inputs through the native module (one continuous
    native state, seeded once) and require every recorded output."""
    data = json.loads((directory/'truck_capture.json').read_text())
    frames = [f for f in data['frames'] if 'in' in f]
    truck = native_from(bytes.fromhex(frames[0]['in']['truck']))
    checked = events_checked = 0
    for record in frames:
        raw_in = bytes.fromhex(record['in']['truck'])
        player = bytes.fromhex(record['in']['player'])
        a0 = struct.unpack_from('<3f', player, 0xA0); b0 = struct.unpack_from('<3f', player, 0xB0)
        world = NativeWorld(record['in']['story'], player[5], player[0xA], record['in']['kind'],
                            a0, b0, record['in']['carry'])
        native.placement, native.bounds, native.pending = None, record['in']['bounds'], 0
        state0 = truck.state
        assert native.tick(truck, world) == 1
        raw = bytes.fromhex(record['out']['truck'])
        expected = dict(state=raw[4], frame=struct.unpack_from('<h', raw, 0x28)[0],
                        shake=struct.unpack_from('<i', raw, 0x2EC)[0],
                        position=list(struct.unpack_from('<3I', raw, 0xB0)),
                        matrix=list(struct.unpack_from('<16I', raw, 0xD0)),
                        rest=list(struct.unpack_from('<16I', raw, 0x1F0)),
                        tail=list(struct.unpack_from('<4I', raw, 0x2DC)),
                        story=record['out']['story'], carry=record['out']['carry'],
                        a0=list(struct.unpack_from('<3I', bytes.fromhex(record['out']['player_a0']))))
        got = dict(state=truck.state, frame=truck.frame, shake=truck.shake,
                   position=words(truck.position), matrix=words(truck.matrix),
                   rest=words(truck.rest_matrix),
                   tail=words((truck.jitter_z, truck.jitter_x, truck.rest_y, truck.rest_rotation_x)),
                   story=world.story.value, carry=world.carry.value & 0xffffffff, a0=words(world.a0))
        if state0 == 1:
            expected['velocity'] = list(struct.unpack_from('<3I', bytes.fromhex(record['out']['scratch'])))
            got['velocity'] = words(truck.velocity)
        assert got == expected, dict(index=record['index'], diff={k: (got[k], expected[k])
                                     for k in got if got[k] != expected[k]})
        mine = [(e[0], e[1] if e[0] != 'effect' else None) for e in native.events
                if e[0] in ('effect', 'sound', 'rumble')]
        theirs = [(kind, None) for kind, _, _ in record['events']]
        assert [m[0] for m in mine] == [t[0] for t in theirs], (record['index'], mine, theirs)
        for event, (kind, _, scratch) in zip([e for e in native.events if e[0] == 'effect'],
                                              [e for e in record['events'] if e[0] == 'effect']):
            assert event[2][:3] == list(struct.unpack('<3I', bytes.fromhex(scratch))), record['index']
            events_checked += 1
        checked += 1
    return dict(frames=checked, effect_positions=events_checked,
                final_state=truck.state, final_story=frames[-1]['out']['story'],
                events=sum(len(f['events']) for f in frames))


if __name__ == '__main__':
    main()
