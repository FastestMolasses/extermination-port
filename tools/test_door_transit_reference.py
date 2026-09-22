#!/usr/bin/env python3
"""Execute original BBE40/BC150 and their complete finite SDK geometry paths."""
import ctypes as C
import hashlib
import itertools
import json
from pathlib import Path
import random
import struct
import subprocess

from test_door_original_reference import Door
from test_item_sdk_math_reference import Original
from test_interaction_pickup_reference import Math
from test_interaction_scan_reference import ELF_SHA
from test_point_light_reference import bits

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent/'Extermination'
ACTOR, PLAYER, TABLE = 0x920000, 0x8102b0, 0x960000


class Plan(C.Structure):
    _fields_ = [('script_entry', C.c_uint32), ('side', C.c_uint16),
                ('player_clip', C.c_uint16), ('door_clip', C.c_uint16),
                ('sound', C.c_uint16), ('wait_ticks', C.c_float),
                ('player_yaw', C.c_float), ('position', C.c_float*4), ('locked', C.c_int)]


PATCH = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(Plan))
FACE = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_float)
ALIGN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_float))
START = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32)
TICK = C.CFUNCTYPE(C.c_int, C.c_void_p)


class Hooks(C.Structure):
    _fields_ = [('context', C.c_void_p), ('patch', PATCH), ('face', FACE),
                ('align', ALIGN), ('start', START), ('tick', TICK)]


class Destination(C.Structure):
    _fields_ = [(name, C.c_uint8) for name in ('area', 'sub_area', 'entry', 'kind')]


FADE = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.c_int)


def kickoff(elf, native, math, origin, yaw, player, locked, armed, side, done, link):
    original = Original(elf)
    origin, player = (C.c_float*3)(*origin), (C.c_float*3)(*player)
    door = Door()
    door.armed, door.side, door.link_flags = armed, side, link
    door.origin = origin
    original.write(ACTOR + 0xb0, bytes(origin))
    original.write(PLAYER + 0xa0, bytes(player))
    original.save(ACTOR + 0xc4, bits(yaw))
    original.save(ACTOR + 11, armed, 1)
    original.save(ACTOR + 0x2e, side, 2)
    original.save(ACTOR + 0x56, link, 2)
    original.save(PLAYER + 0xc4, bits(.713))
    sounds = (C.c_uint16*2)(0x401, 0x402)
    original.write(0x24db80 + (link >> 8)*4, bytes(sounds))
    scripts = bytearray(original.read(0x24dbc0, 0x3c0))
    expected = []

    def align(o):
        assert o.r[4:6] == [PLAYER, 0x700038a0]
        expected.append(('align', o.load(PLAYER + 0xc4), o.read(o.r[5], 16)))

    def start(o):
        assert o.r[4] == ACTOR + 0x1f0
        expected.append(('start', o.r[5]))

    def tick(o):
        assert o.r[4] == ACTOR
        expected.append(('tick',)); o.r[2] = done

    original.calls.update({0x182f90: align, 0x1ba1a0: start, 0x1ba1f0: tick})
    original.run(0x1bbe40, (ACTOR, ACTOR + 0x1f0, locked))
    actual = []
    live_yaw = [bits(.713)]

    def patch(_, pointer):
        plan = pointer.contents
        assert door.side == plan.side
        def write(address, value): struct.pack_into('<I', scripts, address - 0x24dbc0, value)
        if plan.locked:
            write(0x24dcd4, plan.player_clip); write(0x24dd14, plan.door_clip)
        else:
            write(0x24dc14, plan.player_clip); write(0x24dc54, plan.door_clip)
            write(0x24dc8c, bits(plan.wait_ticks)); write(0x24dc58, plan.sound)
        return 1

    def face(_, value): live_yaw[0] = bits(value); return 1
    def native_align(_, point):
        actual.append(('align', live_yaw[0], bytes((C.c_float*4)(*[point[i] for i in range(4)]))))
        return 1
    hooks = Hooks(None, PATCH(patch), FACE(face), ALIGN(native_align),
        START(lambda _, entry: actual.append(('start', entry)) or 1),
        TICK(lambda _: actual.append(('tick',)) or done))
    result = native.em_door_transit_kickoff(C.byref(door), yaw, player, sounds, locked,
                                           C.byref(math), C.byref(hooks))
    assert (result, actual, door.side, live_yaw[0], bytes(scripts)) == (
        original.r[2], expected, C.c_int16(original.load(ACTOR + 0x2e, 2)).value,
        original.load(PLAYER + 0xc4), original.read(0x24dbc0, 0x3c0)), dict(
            origin=list(origin), yaw=yaw, player=list(player), locked=locked, armed=armed,
            actual=actual, expected=expected, result=result,
            side=(door.side, original.load(ACTOR + 0x2e, 2)))


def destination(elf, native, door_id, side, row):
    original = Original(elf)
    original.save(ACTOR + 0x34, door_id, 2)
    original.save(ACTOR + 0x2e, side, 2)
    original.save(0x810700, 11, 1)
    original.save(0x24e140 + 11*4, TABLE)
    original.write(TABLE + (door_id & 127)*4, bytes(row))
    original.write(0x8106b5, bytes((17, 19, 23, 29)))
    expected = []

    def fade(o, whole_area):
        assert o.r[4] == 4
        if not whole_area: assert o.r[5] == 0
        expected.append((whole_area, 4, original.read(0x8106b5, 4)))

    original.calls[0x1b0c00] = lambda o: fade(o, 1)
    original.calls[0x1aede0] = lambda o: fade(o, 0)
    original.run(0x1bc150, (ACTOR,))
    actual = []
    request = Destination(17, 19, 23, 29)
    hook = FADE(lambda _, whole, ticks: actual.append((whole, ticks, bytes(request))) or 1)
    result = native.em_door_transit_commit(C.byref(request), door_id, side,
                                          (C.c_uint8*4)(*row), hook, None)
    assert result == 1 and actual == expected
    assert bytes(request) == original.read(0x8106b5, 4)


def main():
    elf = (DECOMP/'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    output = ROOT/'build/door_transit_reference'
    output.mkdir(parents=True, exist_ok=True)
    library = output/'transit.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
        '-shared', '-fPIC', '-Isrc', 'src/game/em_door_transit.c',
        'src/game/em_interaction_scan.c', 'src/game/em_item_sdk_math.c', '-lm',
        '-o', str(library)], cwd=ROOT, check=True)
    native = C.CDLL(str(library))
    native.em_door_transit_kickoff.argtypes = [C.POINTER(Door), C.c_float, C.POINTER(C.c_float),
        C.POINTER(C.c_uint16), C.c_int, C.POINTER(Math), C.POINTER(Hooks)]
    native.em_door_transit_commit.argtypes = [C.POINTER(Destination), C.c_int16,
        C.c_uint16, C.POINTER(C.c_uint8), FADE, C.c_void_p]
    offset = 0x26c5d8 - 0x100000 + 0x300
    math = Math.from_buffer_copy(elf[offset:offset + 76])
    count = 0
    for yaw, x, z, locked, armed in itertools.product(
            (-3.1415927, -1.5707964, -.40142572, 0, 1.5707964, 3.1415927),
            (-10, 0, 10), (-10, 0, 10), (0, 1), (0, 4, 255)):
        kickoff(elf, native, math, (0, 0, 0), yaw, (x, -21.7, z), locked, armed, 0x1234, 0, 0x400)
        count += 1
    rng = random.Random(0x1bbe40)
    for _ in range(1000):
        origin = [rng.uniform(-500, 500) for _ in range(3)]
        player = [value + rng.uniform(-20, 20) for value in origin]
        kickoff(elf, native, math, origin, rng.uniform(-3.14, 3.14), player,
                rng.randrange(2), 4, rng.randrange(0x8000), rng.randrange(2), rng.randrange(8)*256)
        count += 1
    # Both sides of the canonical AREA11 source, using the actual EMDO pose.
    metadata = (ROOT/'assets/scene_snow/door_original/source.emdo').read_bytes()
    origin = struct.unpack_from('<3f', metadata, 32)
    yaw = struct.unpack_from('<f', metadata, 48)[0]
    for dx, dz in itertools.product((-10, -5, 0, 5, 10), repeat=2):
        kickoff(elf, native, math, origin, yaw, (origin[0]+dx, origin[1], origin[2]+dz),
                0, 4, 0, 0, 0x400)
        count += 1
    commits = 0
    for door_id, side, row in itertools.product((0, 1, 127, 128, 255, 0x7f80, -1),
            range(4), ((2, 1, 0, 0), (7, 3, 0, 9), (7, 3, 1, 9), (255, 255, 255, 255))):
        destination(elf, native, door_id, side, row)
        commits += 1
    report = dict(status='PASS', original_kickoff_and_sdk_cases=count,
        original_destination_cases=commits,
        scope='Side latch, patch fields, facing/alignment, first script pump and fade-before-request order. '
              'Animation/frame/camera/script commands and actual room loading remain real host workers.')
    (output/'result.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__': main()
