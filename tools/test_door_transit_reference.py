#!/usr/bin/env python3
"""Execute the original 001BBE40 and 001BC150 and compare em_door_transit.

001BBE40 runs on the shared EE interpreter with COP1 on tools/ee_float_model.py
(FallEE, docs/EE_FLOAT_MODEL.md): the PCSX2 route capture 09_fence_door
reproduces the alignment point only under that model (the IEEE interpreter
this test used before gave a point one ULP off the capture). Its callees
001B1240, 001B1470, 0011E2A8 and 0011DE90 run as original instructions in the
same EE; the native math workers are answered by executing the same original
functions for the arguments the native passes (their own translations have
their own oracles). Compared: the result, the side latch, the patched
program words, the player's +0xC4, the 00182F90 point and the call order of
00182F90 / 001BA1A0 / 001BA1F0."""
import ctypes as C
import hashlib
import itertools
import json
from pathlib import Path
import random
import struct
import subprocess

import reference_mode
from test_door_original_reference import Door
from test_interaction_scan_reference import ELF_SHA
from test_player_fall_reference import FallEE
from test_player_slide_reference import bits, number

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent/'Extermination'
ACTOR, PLAYER, TABLE = 0x920000, 0x8102b0, 0x960000
SCRATCH = 0x940000


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
BEARING = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_float), C.c_float, C.c_float, C.POINTER(C.c_float))
UNARY = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_float, C.POINTER(C.c_float))


class Hooks(C.Structure):
    _fields_ = [('context', C.c_void_p), ('patch', PATCH), ('face', FACE),
                ('align', ALIGN), ('start', START), ('tick', TICK)]


class TransitMath(C.Structure):
    _fields_ = [('context', C.c_void_p), ('bearing', BEARING), ('wrap', UNARY),
                ('sine', UNARY), ('cosine', UNARY)]


class Destination(C.Structure):
    _fields_ = [(name, C.c_uint8) for name in ('area', 'sub_area', 'entry', 'kind')]


FADE = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.c_int)
EE = None
PROGRAM = None


def ee_float_call(entry, args=(), floats=()):
    """An original SDK / wrap routine on the EE model; f0 as float bits."""
    EE.hooks = {}
    EE.call(entry, args, floats)
    return EE.f[0] & 0xffffffff


def native_math(calls):
    def bearing(_, origin, x, z, result):
        EE.write(SCRATCH, struct.pack('<3f', origin[0], origin[1], origin[2]))
        value = ee_float_call(0x1b1240, (SCRATCH,), (x, z))
        calls.append(('001B1240', bits(x), bits(z), value))
        result[0] = number(value)
        return 0

    def unary(entry, name):
        def run(_, x, result):
            value = ee_float_call(entry, (), (x,))
            calls.append((name, bits(x), value))
            result[0] = number(value)
            return 0
        return UNARY(run)
    return TransitMath(None, BEARING(bearing), unary(0x1b1470, '001B1470'),
                       unary(0x11e2a8, '0011E2A8'), unary(0x11de90, '0011DE90'))


def kickoff(native, origin, yaw, player, locked, armed, side, done, link):
    ee = EE
    ee.write(0x24dbc0, PROGRAM)
    origin, player = (C.c_float*3)(*origin), (C.c_float*3)(*player)
    door = Door()
    door.armed, door.side, door.link_flags = armed, side, link
    door.origin = origin
    ee.write(ACTOR + 0xb0, bytes(origin))
    ee.write(PLAYER + 0xa0, bytes(player))
    ee.save(ACTOR + 0xc4, bits(yaw))
    ee.save(ACTOR + 11, armed, 1)
    ee.save(ACTOR + 0x2e, side, 2)
    ee.save(ACTOR + 0x56, link, 2)
    ee.save(PLAYER + 0xc4, bits(.713))
    sounds = (C.c_uint16*2)(0x401, 0x402)
    ee.write(0x24db80 + (link >> 8)*4, bytes(sounds))
    scripts = bytearray(ee.read(0x24dbc0, 0x3c0))
    expected = []

    def align(o):
        assert (o.r[4] & 0xffffffff, o.r[5] & 0xffffffff) == (PLAYER, 0x700038a0)
        expected.append(('align', o.load(PLAYER + 0xc4), o.read(0x700038a0, 16)))

    def start(o):
        assert o.r[4] & 0xffffffff == ACTOR + 0x1f0
        expected.append(('start', o.r[5] & 0xffffffff))

    def tick(o):
        assert o.r[4] & 0xffffffff == ACTOR
        expected.append(('tick',)); o.r[2] = done

    ee.hooks = {0x182f90: align, 0x1ba1a0: start, 0x1ba1f0: tick}
    ee.call(0x1bbe40, (ACTOR, ACTOR + 0x1f0, locked))
    want = (ee.r[2] & 0xffffffff, C.c_int16(ee.load(ACTOR + 0x2e, 2)).value, ee.load(PLAYER + 0xc4),
            ee.read(0x24dbc0, 0x3c0))
    actual, calls = [], []
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
    math = native_math(calls)
    result = native.em_door_transit_kickoff(C.byref(door), yaw, player, sounds, locked,
                                           C.byref(math), C.byref(hooks))
    got = (result, door.side, live_yaw[0], bytes(scripts))
    assert (got, actual) == (want, expected), dict(
        origin=list(origin), yaw=yaw, player=list(player), locked=locked, armed=armed,
        actual=actual, expected=expected, result=(result, want[0]), side=(door.side, want[1]),
        yaw_bits=(live_yaw[0], want[2]))
    return armed & 4 != 0


def destination(native, door_id, side, row):
    ee = EE
    ee.hooks = {}
    ee.save(ACTOR + 0x34, door_id & 0xffff, 2)
    ee.save(ACTOR + 0x2e, side, 2)
    ee.save(0x810700, 11, 1)
    ee.save(0x24e140 + 11*4, TABLE)
    ee.write(TABLE + (door_id & 127)*4, bytes(row))
    ee.write(0x8106b5, bytes((17, 19, 23, 29)))
    expected = []

    def fade(o, whole_area):
        assert o.r[4] & 0xffffffff == 4
        if not whole_area: assert o.r[5] & 0xffffffff == 0
        expected.append((whole_area, 4, ee.read(0x8106b5, 4)))

    ee.hooks = {0x1b0c00: lambda o: fade(o, 1), 0x1aede0: lambda o: fade(o, 0)}
    ee.call(0x1bc150, (ACTOR,))
    actual = []
    request = Destination(17, 19, 23, 29)
    hook = FADE(lambda _, whole, ticks: actual.append((whole, ticks, bytes(request))) or 1)
    result = native.em_door_transit_commit(C.byref(request), door_id, side,
                                          (C.c_uint8*4)(*row), hook, None)
    assert result == 1 and actual == expected
    assert bytes(request) == ee.read(0x8106b5, 4)


def main():
    global EE, PROGRAM
    elf = (DECOMP/'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    output = ROOT/'build/door_transit_reference'
    output.mkdir(parents=True, exist_ok=True)
    library = output/'transit.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
        '-shared', '-fPIC', '-Isrc', 'src/game/em_door_transit.c', '-lm',
        '-o', str(library)], cwd=ROOT, check=True)
    native = C.CDLL(str(library))
    native.em_door_transit_kickoff.argtypes = [C.POINTER(Door), C.c_float, C.POINTER(C.c_float),
        C.POINTER(C.c_uint16), C.c_int, C.POINTER(TransitMath), C.POINTER(Hooks)]
    native.em_door_transit_commit.argtypes = [C.POINTER(Destination), C.c_int16,
        C.c_uint16, C.POINTER(C.c_uint8), FADE, C.c_void_p]
    EE = FallEE(elf)
    PROGRAM = EE.read(0x24dbc0, 0x3c0)
    grid = list(itertools.product(
            (-3.1415927, -1.5707964, -.40142572, 0, 1.5707964, 3.1415927),
            (-10, 0, 10), (-10, 0, 10), (0, 1), (0, 4, 255)))
    grid = reference_mode.select(grid, 60, 0x1bbe40, axes=(lambda g: g[0], lambda g: g[3], lambda g: g[4]))
    count = armed_count = 0
    for yaw, x, z, locked, armed in grid:
        armed_count += kickoff(native, (0, 0, 0), yaw, (x, -21.7, z), locked, armed, 0x1234, 0, 0x400)
        count += 1
    rng = random.Random(0x1bbe40)
    for _ in range(reference_mode.pick(1000, 60)):
        origin = [rng.uniform(-500, 500) for _ in range(3)]
        player = [value + rng.uniform(-20, 20) for value in origin]
        armed_count += kickoff(native, origin, rng.uniform(-3.14, 3.14), player,
                rng.randrange(2), 4, rng.randrange(0x8000), rng.randrange(2), rng.randrange(8)*256)
        count += 1
    # Both sides of the canonical AREA11 source, using the actual EMDO pose,
    # and route beat 09's press stance (f306, the capture's player position):
    # the point must be the capture's f309 player position.
    metadata = (ROOT/'assets/scene_snow/door_original/source.emdo').read_bytes()
    origin = struct.unpack_from('<3f', metadata, 32)
    yaw = struct.unpack_from('<f', metadata, 48)[0]
    for dx, dz in itertools.product((-10, -5, 0, 5, 10), repeat=2):
        armed_count += kickoff(native, origin, yaw, (origin[0]+dx, origin[1], origin[2]+dz),
                0, 4, 0, 0, 0x400)
        count += 1
    rows = json.loads((DECOMP/'build/s87/route/09_fence_door/trace.json').read_text())['rows']
    kickoff(native, origin, yaw, rows[306]['pos'], 0, 4, 0, 0, 0x400)
    point = struct.unpack('<3f', EE.read(0x700038a0, 12))
    assert [round(v, 5) for v in point] == rows[309]['pos'], ('route 09 f309 alignment', point, rows[309]['pos'])
    count += 1
    commits = 0
    for door_id, side, row in itertools.product((0, 1, 127, 128, 255, 0x7f80, -1),
            range(4), ((2, 1, 0, 0), (7, 3, 0, 9), (7, 3, 1, 9), (255, 255, 255, 255))):
        destination(native, door_id, side, row)
        commits += 1
    reference_mode.banner(f'{count} kickoff cases ({armed_count} armed)', f'{commits} commit cases')
    report = dict(status='PASS', original_kickoff_and_sdk_cases=count,
        original_destination_cases=commits, route_09_alignment=[round(v, 5) for v in point],
        scope='Side latch, patch fields, facing/alignment on the EE float model, the first script pump '
              'and fade-before-request order.')
    (output/'result.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__': main()
