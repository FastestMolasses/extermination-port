#!/usr/bin/env python3
"""Execute original B6F00, angle wrap and SDK matrix transform locally.

Only the final182F90 application is intercepted; its position-mirror behavior
has a separate original-instruction test. No original instructions are embedded.
"""
import ctypes as C
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess

from test_interaction_scan_reference import ScanOracle, ELF_SHA, bits

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
OWNER, POINT = 0x900000, 0x910000


def vector(values):
    return (C.c_float * len(values))(*values)


def main():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    folder = ROOT / 'build/interaction_alignment'
    folder.mkdir(parents=True, exist_ok=True)
    library = folder / 'alignment.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc',
                    'src/game/em_interaction_alignment.c', '-o', str(library)],
                   cwd=ROOT, check=True)
    native = C.CDLL(str(library)).em_interaction_alignment
    pointer = C.POINTER(C.c_float)
    native.argtypes = [pointer, C.c_float, pointer, C.c_float, C.c_float, pointer, pointer]
    native.restype = C.c_int
    cases = []
    captures = ('panel/root/eeMemory.bin', 'elevator/clip47_ee.bin',
                'elevator/refusal/eeMemory.bin')
    for name in captures:
        data = (DECOMP / 'build/startup-reference' / name).read_bytes()
        matrix = struct.unpack_from('<16f', data, 0x7AA590 + 0xD0)
        yaw = struct.unpack_from('<f', data, 0x7AA590 + 0xC4)[0]
        ground = struct.unpack_from('<f', data, 0x810354)[0]
        cases.append((matrix, yaw, (.3, 0, 9, 1), 3.1415927, ground))
    identity = [float(i // 4 == i % 4) for i in range(16)]
    for yaw in (-6.2831855, -3.1415927, -0.0, 0, 3.1415927, 6.2831855):
        for offset in (-6.2831855, -3.1415927, -0.0, 0, 3.1415927, 6.2831855):
            cases.append((identity, yaw, (.3, 0, 9, 1), offset, 230))
    rng = random.Random(0x1B6F00)
    for _ in range(1600):
        matrix = [rng.uniform(-2, 2) for _ in range(16)]
        matrix[3] = matrix[7] = matrix[11] = 0
        matrix[15] = 1
        matrix[12:15] = [rng.uniform(-500, 500) for _ in range(3)]
        cases.append((matrix, rng.uniform(-3.1415926, 3.1415926),
                      [rng.uniform(-20, 20) for _ in range(3)] + [1],
                      rng.choice((-3.1415927, 0, 3.1415927)), rng.uniform(-100, 500)))
    for index, (matrix, yaw, point, offset, ground) in enumerate(cases):
        matrix, point = vector(matrix), vector(point)
        original = ScanOracle(elf)
        original.write(OWNER + 0xD0, bytes(matrix))
        original.save(OWNER + 0xC4, bits(yaw))
        original.write(POINT, bytes(point))
        original.save(0x810354, bits(ground))
        outputs = []

        def apply_position(machine):
            assert machine.r[4] == 0x8102B0
            outputs.append(machine.read(machine.r[5], 16) + machine.read(0x810374, 4))

        original.calls[0x182F90] = apply_position
        original.run(0x1B6F00, (OWNER, POINT), (offset,))
        target, facing = vector([0] * 4), C.c_float()
        assert native(matrix, yaw, point, offset, ground, target, C.byref(facing)) == 1
        actual = bytes(target) + bytes(facing)
        assert len(outputs) == 1 and actual == outputs[0], (index, actual.hex(), outputs)
    assert native(None, 0, vector([0, 0, 0, 1]), 0, 0, vector([0] * 4), C.byref(C.c_float())) == 0
    report = {'original_B6F00_SDK_cases': len(cases), 'captured_panel_matrices': len(captures),
              'scope': 'yaw and homogeneous target exact;182F90 application tested separately'}
    (folder / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print('interaction alignment original reference PASS:', json.dumps(report))


if __name__ == '__main__':
    main()
