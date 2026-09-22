#!/usr/bin/env python3
"""Compare the typed door controller against the owner's original EE code.

Model allocation, script workers, pose construction and device output are
explicit callbacks. The controller and its nested phase wrappers execute
their actual original instructions. No instruction bytes are distributed.
"""
import ctypes as C
import hashlib
import itertools
import json
from pathlib import Path
import random
import subprocess

from test_pickup_owner_reference import OwnerOracle, ACTOR, DRAW
from test_interaction_scan_reference import DECOMP, ELF_SHA
from test_point_light_reference import bits, signed

ROOT = Path(__file__).resolve().parents[1]


class DoorOracle(OwnerOracle):
    def plain(self, word):
        if word >> 26 == 0 and word & 63 in (60, 63):
            target, source = word >> 11 & 31, word >> 16 & 31
            shift = (word >> 6 & 31) + 32
            self.r[target] = ((self.r[source] << shift) & 0xffffffffffffffff
                if word & 63 == 60 else (signed(self.r[source], 64) >> shift) & 0xffffffffffffffff)
            return
        if word >> 26 == 32:  # lb: signed script animation flag
            base, target = word >> 21 & 31, word >> 16 & 31
            address = (self.r[base] + signed(word & 65535, 16)) & 0xffffffff
            self.r[target] = signed(self.load(address, 1), 8) & 0xffffffff
            return
        super().plain(word)


class Door(C.Structure):
    _fields_ = [(name, C.c_uint8) for name in (
        'status', 'visible', 'class_flags', 'subtype', 'lifecycle', 'phase', 'armed', 'freed')]
    _fields_ += [('animation_active', C.c_int8), ('animation_flags', C.c_int16),
                ('door_id', C.c_int16), ('side', C.c_int16), ('link_flags', C.c_uint16),
                ('origin', C.c_float * 3), ('initialized_scale', C.c_float * 3)]


CALL = C.CFUNCTYPE(C.c_int, C.c_void_p)
KICK = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int)
ADVANCE = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_int16))
START = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32)
PUBLISH = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_float))


class Hooks(C.Structure):
    _fields_ = [('context', C.c_void_p), ('initialize', CALL), ('kickoff', KICK),
                ('advance_animation', ADVANCE), ('script_tick', CALL),
                ('script_start', START), ('transition', CALL), ('reset_animation', CALL),
                ('place', CALL), ('publish', PUBLISH), ('draw', CALL), ('free', CALL)]


def compare(elf, lib, lifecycle, phase, armed, active, done, pending, subtype,
            unlocked, initialized=1, link=0x400, side=0x1203, y=184.8):
    original = DoorOracle(elf)
    door = Door(2, 73, 0x85, subtype, lifecycle, phase, armed, 0, active,
                -32768, 0x1234, side, link, (423, y, 290.3), (7, 8, 9))
    for offset, value in ((0, door.status), (1, door.visible), (2, door.class_flags),
                          (3, subtype), (4, lifecycle), (5, phase), (11, armed),
                          (0x1fc, active)):
        original.save(ACTOR + offset, value, 1)
    for offset, value in ((0x1fe, -32768), (0x34, door.door_id), (0x2e, side), (0x56, link)):
        original.save(ACTOR + offset, value, 2)
    for offset, values in ((0xb0, door.origin), (0x80, door.initialized_scale)):
        for axis, value in enumerate(values): original.save(ACTOR + offset + axis*4, bits(value))
    original.save(ACTOR + 0x4c, DRAW)
    original.save(0x810700, 11, 1)
    original.save(0x810841 + 11, 1 << (door.door_id & 31) if unlocked else 0, 1)
    # Use a representable persistent-bit index; all other cases preserve it.
    if subtype == 0x15:
        door.door_id = 3
        original.save(ACTOR + 0x34, 3, 2)
        original.save(0x810841 + 11, 8 if unlocked else 0, 1)
    original.save(0x8106b8, pending, 1)
    expected = []

    def initialize(o):
        expected.append(('initialize',))
        o.r[2] = not initialized
        if not initialized: o.save(ACTOR + 4, 3, 1)

    def seed(o):
        assert (o.r[4], o.r[5]) == (ACTOR, 0)

    def kickoff(o):
        assert o.r[4:6] == [ACTOR, ACTOR + 0x1f0]
        if armed & 4: expected.append(('kickoff', o.r[6]))
        o.r[2] = int(bool(armed & 4))

    def advance(o):
        assert (o.r[4], o.f[12]) == (ACTOR, bits(1))
        expected.append(('advance',))
        o.r[2] = 0xFEDC

    def pump(o):
        assert o.r[4] == ACTOR
        expected.append(('tick',))
        o.r[2] = done

    def start(o):
        assert o.r[4] == ACTOR + 0x1f0
        expected.append(('start', o.r[5]))

    def reset(o):
        assert (o.r[4], o.r[5], o.f[12], o.f[13]) == (ACTOR, 0, 0, 0)
        expected.append(('reset',))

    def publish(o):
        assert o.r[4] == ACTOR
        expected.append(('publish', tuple(o.f[12:15])))
        o.save(ACTOR + 1, 0, 1)

    original.calls.update({0x1b0ea0: initialize, 0x1c63e0: seed, 0x1bbe40: kickoff,
        0x1c64f0: advance, 0x1ba1f0: pump, 0x1ba1a0: start, 0x1c67e0: reset,
        0x1bc150: lambda o: expected.append(('transition',)),
        0x1c68c0: lambda o: expected.append(('place',)), 0x1b1b30: publish,
        DRAW: lambda o: expected.append(('draw',)),
        0x1afc10: lambda o: expected.append(('free',))})
    original.run(0x1bc350, (ACTOR,))
    actual = []

    def event(name):
        def callback(_): actual.append((name,)); return 1
        return CALL(callback)

    def native_advance(_, output):
        actual.append(('advance',)); output[0] = signed(0xfedc, 16); return 1

    hooks = Hooks(None, CALL(lambda _: actual.append(('initialize',)) or initialized),
        KICK(lambda _, mode: actual.append(('kickoff', mode)) or 1), ADVANCE(native_advance),
        CALL(lambda _: actual.append(('tick',)) or done),
        START(lambda _, address: actual.append(('start', address)) or 1),
        event('transition'), event('reset'), event('place'),
        PUBLISH(lambda _, point: actual.append(('publish', tuple(bits(point[i]) for i in range(3)))) or 0),
        event('draw'), event('free'))
    result = lib.em_door_original_tick(C.byref(door), unlocked, pending, C.byref(hooks))
    expected_state = tuple(original.load(ACTOR + offset, 1) for offset in (0, 1, 2, 3, 4, 5, 11))
    actual_state = (door.status, door.visible, door.class_flags, door.subtype,
                    door.lifecycle, door.phase, door.armed)
    expected_extra = tuple(signed(original.load(ACTOR + offset, 2), 16)
                           for offset in (0x1fe, 0x34, 0x2e))
    actual_extra = (door.animation_flags, door.door_id, door.side)
    assert (actual, actual_state, actual_extra, result) == (
        expected, expected_state, expected_extra, 0 if ('free',) in expected else 1), dict(
            case=(lifecycle, phase, armed, active, done, pending, subtype, unlocked),
            actual=actual, expected=expected, state=(actual_state, expected_state),
            extra=(actual_extra, expected_extra), result=result)
    assert tuple(bits(v) for v in door.initialized_scale) == tuple(
        original.load(ACTOR + 0x80 + i*4) for i in range(3))


def main():
    elf = (DECOMP/'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    build = ROOT/'build/door_original_reference'
    build.mkdir(parents=True, exist_ok=True)
    library = build/'door.dylib'
    subprocess.run(['cc', '-shared', '-fPIC', '-std=c11', '-O2', '-Wall', '-Wextra',
        '-Werror', '-ffp-contract=off', '-I', str(ROOT/'src'),
        str(ROOT/'src/game/em_door_original.c'), '-o', str(library)], check=True)
    lib = C.CDLL(str(library))
    lib.em_door_original_tick.argtypes = [C.POINTER(Door), C.c_int, C.c_uint8, C.POINTER(Hooks)]
    count = 0
    for case in itertools.product((1, 2, 3, 255), range(7), (0, 1, 4, 255),
            (0, 1, -128), (0, 1), (0, 2), (3, 0x15), (0, 1)):
        compare(elf, lib, *case)
        count += 1
    for initialized, link, side in itertools.product((0, 1), (0, 0x40, 0x80, 0xc0, 0xffff), (0, 127, 0x12ff)):
        compare(elf, lib, 0, 0, 0, 0, 0, 0, 3, 0, initialized, link, side)
        count += 1
    rng = random.Random(0x1bc300)
    for _ in range(256):
        compare(elf, lib, 1, 0, 0, 0, 0, 0, 3, 0, y=rng.uniform(-10000, 10000))
        count += 1
    (build/'result.json').write_text(json.dumps(dict(status='PASS', cases=count,
        original_functions=['001BBDA0', '001B0F60', '001BC350', '001BC0E0',
                            '001BC240', '001BC290', '001BC300'],
        limits='Model allocation, kickoff/script, transition and pose/device workers are explicit hooks.'),
        indent=2)+'\n')
    print(f'Original door controller: PASS {count} instruction/state/order cases')


if __name__ == '__main__': main()
