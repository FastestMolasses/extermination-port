#!/usr/bin/env python3
"""Check DD980 and DD950 with the original SDK square-root body."""
import ctypes as C
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess

from test_item_sdk_math_reference import Original
from test_interaction_scan_reference import ELF_SHA
from test_point_light_reference import CONTEXT

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
EYE, TARGET = 0x8105D0, 0x8105E0


class Projection(C.Structure):
    _fields_ = [('center', C.c_float * 4), ('scale', C.c_float), ('distance', C.c_float)]


def main():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    folder = ROOT / 'build/interaction_projection'
    folder.mkdir(parents=True, exist_ok=True)
    library = folder / 'projection.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc',
                    'src/game/em_interaction_projection.c', 'src/game/em_item_sdk_math.c',
                    'src/game/em_interaction_scan.c', '-o', str(library)], cwd=ROOT, check=True)
    native = C.CDLL(str(library)).em_interaction_projection_publish
    native.argtypes = [C.POINTER(Projection), C.POINTER(C.c_float), C.POINTER(C.c_float)]
    native.restype = C.c_int
    cases = [([0, 0, 0], [0, 0, 0]), ([1, 2, 3], [1, 2, 3])]
    captured = []
    for path in ('panel/root/eeMemory.bin', 'elevator/clip47_ee.bin',
                 'elevator/completed_ee.bin', 'elevator/refusal/eeMemory.bin'):
        data = (DECOMP / 'build/startup-reference' / path).read_bytes()
        pair = (struct.unpack_from('<3f', data, EYE), struct.unpack_from('<3f', data, TARGET))
        cases.append(pair)
        captured.append(path)
    rng = random.Random(0x1DD980)
    for _ in range(1600):
        eye = [rng.uniform(-600, 600) for _ in range(3)]
        target = [v + rng.uniform(-60, 60) for v in eye]
        cases.append((eye, target))
    for index, (eye, target) in enumerate(cases):
        eye, target = (C.c_float * 3)(*eye), (C.c_float * 3)(*target)
        original = Original(elf)
        original.write(EYE, bytes(eye) + struct.pack('<f', 1))
        original.write(TARGET, bytes(target) + struct.pack('<f', 1))
        original.run(0x1DD980, (EYE, TARGET))
        expected = original.read(CONTEXT + 0x2450, C.sizeof(Projection))
        result = Projection()
        assert native(C.byref(result), eye, target) == 1
        assert bytes(result) == expected, (index, bytes(result).hex(), expected.hex())
    report = {'original_DD980_DD950_cases': len(cases), 'saved_camera_pairs': len(captured),
              'render_context_bytes_each': C.sizeof(Projection), 'scope': 'setter and SDK arithmetic; downstream depth rendering separate'}
    (folder / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print('interaction projection original reference PASS:', json.dumps(report))


if __name__ == '__main__':
    main()
