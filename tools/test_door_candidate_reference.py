#!/usr/bin/env python3
"""Compare the door branch and its SDK bodies with the original ELF."""
import ctypes as C
import hashlib
import itertools
import json
from pathlib import Path
import random
import struct
import subprocess

from test_item_sdk_math_reference import Original
from test_interaction_pickup_reference import Math, Player
from test_interaction_scan_reference import ELF_SHA
from test_point_light_reference import bits

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
ACTOR, PLAYER, DESCRIPTOR = 0x920000, 0x930000, 0x940000


def main():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    folder = ROOT / 'build/door_candidate_reference'
    folder.mkdir(parents=True, exist_ok=True)
    library = folder / 'door.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc',
                    'src/game/em_door_candidate.c', 'src/game/em_interaction_scan.c',
                    'src/game/em_item_sdk_math.c', '-o', str(library)], cwd=ROOT, check=True)
    native = C.CDLL(str(library)).em_door_candidate
    native.argtypes = [C.POINTER(C.c_float), C.POINTER(C.c_float), C.c_float,
                       C.c_uint8, C.POINTER(Player), C.POINTER(Math), C.POINTER(C.c_float)]
    offset = 0x26C5D8 - 0x100000 + 0x300
    math = Math.from_buffer_copy(elf[offset:offset + 76])
    cases = []
    for subtype, x, y, z, yaw, action in itertools.product(
        (3, 0x15, 4), (-15, -5, 0, 5, 10), (-8.00001, -8, 0, 8, 8.00001),
        (-.001, 0, .001), (-3.1415927, -.7853982, 0, .7853982, 3.1415927), (0, 0x2D)):
        cases.append((subtype, (0, 0, 0), 0, (x, y, z), yaw, action, (10, 8)))
    rng = random.Random(0x183EF005)
    for _ in range(1400):
        owner = [rng.uniform(-500, 500) for _ in range(3)]
        point = [v + rng.uniform(-15, 15) for v in owner]
        cases.append((rng.choice((3, 0x15, 4)), owner, rng.uniform(-3.14, 3.14),
                      point, rng.uniform(-3.14, 3.14), 0, (10, 8)))
    # Actual initial AREA11 door placement and descriptor, without using
    # the legacy native door's cached center or its independent scan.
    metadata = (ROOT / 'assets/scene_snow/interaction.emis').read_bytes()
    door = next(metadata[96+i*80:96+(i+1)*80] for i in range(11)
                if struct.unpack_from('<I', metadata, 96+i*80+4)[0] == 3)
    descriptor = struct.unpack_from('<2f', door, 32)
    owner = struct.unpack_from('<3f', door, 56)
    angle = struct.unpack_from('<f', door, 72)[0]
    for dx, dz, yaw in itertools.product((-10, -5, 0, 5, 10), (-10, 0, 10),
                                        (-3.1415927, -.7853982, 0, .7853982, 3.1415927)):
        cases.append((door[24], owner, angle, (owner[0]+dx, owner[1], owner[2]+dz),
                      yaw, 0, descriptor))
    for index, (subtype, owner, angle, point, yaw, action, desc) in enumerate(cases):
        owner, point, desc = (C.c_float * 3)(*owner), (C.c_float * 3)(*point), (C.c_float * 2)(*desc)
        original = Original(elf)
        original.save(ACTOR + 2, 0x85, 1)
        original.save(ACTOR + 3, subtype, 1)
        original.save(ACTOR + 8, 0, 1)
        original.save(ACTOR + 0x30, DESCRIPTOR)
        original.write(DESCRIPTOR, bytes(desc))
        original.write(ACTOR + 0xB0, bytes(owner))
        original.save(ACTOR + 0xC4, bits(angle))
        original.write(PLAYER + 0xA0, bytes(point))
        original.save(PLAYER + 0xC4, bits(yaw))
        original.save(PLAYER + 0x1F0, action, 1)
        original.save(0x70003B98, bits(-91.25))
        original.run(0x183EF0, (PLAYER, ACTOR))
        player = Player(point, yaw, action, (C.c_float * 3)())
        score = C.c_float(-91.25)
        result = native(desc, owner, angle, subtype, C.byref(player), C.byref(math), C.byref(score))
        expected = original.r[2], original.load(0x70003B98)
        assert (result, bits(score.value)) == expected, (index, cases[index], result, score.value, expected)
    report = {'original_door_and_sdk_cases': len(cases),
              'actual_AREA11_placement_cases': 75,
              'scope': 'selector0/class5 predicate and score; published-list and transit bindings separate'}
    (folder / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print('Original door candidate PASS:', json.dumps(report))


if __name__ == '__main__':
    main()
