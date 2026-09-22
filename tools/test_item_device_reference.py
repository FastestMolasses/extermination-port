#!/usr/bin/env python3
"""Original185420 ordered lookup and184D20 battery eligibility oracle."""
import ctypes as C
import hashlib
import itertools
import json
from pathlib import Path
import random
import struct
import subprocess
import sys

from test_item_sdk_math_reference import Original, AtanMath, ELF_SHA

ROOT = Path(__file__).resolve().parents[1]
LIST, ACTOR, PLAYER, DESCRIPTOR = 0x980000, 0x981000, 0x8102B0, 0x990000


class Device(C.Structure):
    _fields_ = [('owner', C.c_void_p), ('status', C.c_uint8), ('flags', C.c_uint8),
                ('subtype', C.c_uint8), ('shape', C.c_uint8), ('armed', C.c_uint8),
                ('position', C.c_float * 3), ('yaw', C.c_float), ('parameters', C.c_float * 6)]


def compare(elf, native, math, devices, player, yaw, item=0x1B):
    original = Original(elf)
    original.save(0x275b64, len(devices), 2)
    original.save(0x275b5c, LIST)
    original.write(PLAYER + 0xa0, struct.pack('<3f', *player))
    original.write(PLAYER + 0xc4, struct.pack('<f', yaw))
    for i, device in enumerate(devices):
        actor, descriptor = ACTOR + i * 0x400, DESCRIPTOR + i * 0x40
        original.save(LIST + i * 4, actor)
        for offset, value in ((0, device.status), (2, device.flags), (3, device.subtype),
                              (8, device.shape), (11, device.armed)):
            original.save(actor + offset, value, 1)
        original.save(actor + 0x30, descriptor)
        original.write(actor + 0xb0, bytes(device.position))
        original.write(actor + 0xc4, struct.pack('<f', device.yaw))
        original.write(descriptor, bytes(device.parameters))
    original.run(0x185420, (item,))
    winner = original.r[2]
    expected = None if not winner else devices[(winner - ACTOR) // 0x400].owner
    actual = C.c_void_p()
    array = (Device * len(devices))(*devices)
    status = native.em_item_device_find(array, len(devices), item,
        (C.c_float * 3)(*player), yaw, C.byref(math), C.byref(actual))
    assert status == int(expected is not None) and actual.value == expected, {
        'item': item, 'player': list(player), 'yaw': yaw, 'status': status,
        'actual': actual.value, 'expected': expected,
        'devices': [(d.flags, d.subtype, d.shape, d.armed, list(d.position), d.yaw,
                     list(d.parameters)) for d in devices]}
    return actual.value


def main():
    elf = (ROOT.parent / 'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    output = ROOT / 'build/item_device_reference'
    output.mkdir(parents=True, exist_ok=True)
    library = output / ('devices.dylib' if sys.platform == 'darwin' else 'devices.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
        '-fPIC', '-dynamiclib' if sys.platform == 'darwin' else '-shared', '-Isrc',
        'src/game/em_item_device.c', 'src/game/em_item_sdk_math.c',
        'src/game/em_interaction_scan.c', '-lm', '-o', str(library)], cwd=ROOT, check=True)
    native = C.CDLL(str(library))
    native.em_item_device_find.argtypes = [C.POINTER(Device), C.c_size_t, C.c_uint,
        C.POINTER(C.c_float), C.c_float, C.POINTER(AtanMath), C.POINTER(C.c_void_p)]
    math = AtanMath.from_buffer_copy(elf[0x26c5d8 - 0x100000 + 0x300:][:76])
    rng = random.Random(0x185420)
    checks = 0
    for shape, flags, subtype, item in itertools.product(range(7), (0x84, 0x86, 0x8A, 4),
            (0x14, 0x22, 0x23, 0x24, 0x25, 0x26, 0x2C, 0x20), (0x1B, 0x1C, 0x1D)):
        device = Device(1, 1, flags, subtype, shape, 0, (C.c_float * 3)(100, 230, 200),
                        0, (C.c_float * 6)(14, 4, 200, 14, 4, 0))
        if shape in (1, 2):
            device.parameters[0:3] = [100, 230, 200]
        for player, yaw in (((100, 230, 200), 3.14159274), ((100, 230, 210), 0),
                            ((100, 235, 214.001), 0)):
            compare(elf, native, math, [device], player, yaw, item)
            checks += 1
    for _ in range(700):
        shape = rng.randrange(6)
        position = [rng.uniform(-20, 20), 230, rng.uniform(-20, 20)]
        parameters = position + [14, 4, rng.uniform(-3.14159, 3.14159)] if shape in (1, 2) else [14, 4, 0, 0, 0, 0]
        device = Device(1, rng.choice((0, 1, 2, 3)), 0x84, 0x24, shape,
                        rng.choice((0, 0, 1, 4, 5)), (C.c_float * 3)(*position),
                        rng.uniform(-3.14159, 3.14159), (C.c_float * 6)(*parameters))
        player = [position[0] + rng.uniform(-18, 18), 230 + rng.uniform(-23, 6),
                  position[2] + rng.uniform(-18, 18)]
        compare(elf, native, math, [device], player, rng.uniform(-3.14159, 3.14159))
        checks += 1
    # List order wins, independent of distance. Armed entries cannot win.
    devices = [Device(i + 1, 1, 0x84, 0x24, 5, 0, (C.c_float * 3)(0, 230, i), 0,
                       (C.c_float * 6)()) for i in range(4)]
    for order in itertools.permutations(range(4)):
        ordered = [devices[i] for i in order]
        assert compare(elf, native, math, ordered, (0, 230, 0), 0) == ordered[0].owner
        ordered[0].armed = 4
        assert compare(elf, native, math, ordered, (0, 230, 0), 0) == ordered[1].owner
        ordered[0].armed = 0
        checks += 2
    result = {'original_lookup_and_predicate_cases': checks, 'first_published_owner': 'PASS',
              'domain': 'battery1B..1D, originalclass4/6 shapes0..5 and default0',
              'boundaries': 'host must supply the actual frozen published list and descriptors'}
    (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
