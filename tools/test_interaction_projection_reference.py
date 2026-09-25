#!/usr/bin/env python3
"""Check 001DD980 (em_interaction_projection_001DD980) with the original SDK
square-root body: the two float registers and the address it hands its tail
call 001DD950 must equal the original's, bit for bit. 001DD950 itself is the
render context's (em_render_context_001DD950, test_render_context_reference;
bound live through em_rcl_001DD950, test_render_context_live_reference)."""
import ctypes as C
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess

from test_item_sdk_math_reference import Original
from test_interaction_scan_reference import ELF_SHA

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
EYE, TARGET = 0x8105D0, 0x8105E0


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
    native = C.CDLL(str(library)).em_interaction_projection_001DD980
    native.argtypes = [C.POINTER(C.c_float), C.POINTER(C.c_float), C.POINTER(C.c_uint32)]
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
        tail = []

        def capture(o):
            tail.append((o.r[4] & 0xFFFFFFFF, o.f[12] & 0xFFFFFFFF, o.f[13] & 0xFFFFFFFF))
        original.calls[0x1DD950] = capture
        original.run(0x1DD980, (EYE, TARGET))
        assert len(tail) == 1 and tail[0][0] == TARGET, (index, tail)
        result = (C.c_uint32 * 2)()
        assert native(eye, target, result) == 1
        assert (result[0], result[1]) == tail[0][1:], (index, [hex(v) for v in result],
                                                       [hex(v) for v in tail[0][1:]])
    report = {'original_DD980_cases': len(cases), 'saved_camera_pairs': len(captured),
              'scope': '001DD980 up to its 001DD950(&D_008105E0, f12, f13) call (the SDK arithmetic); '
                       '001DD950 is the render context\'s'}
    (folder / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print('interaction projection original reference PASS:', json.dumps(report))


if __name__ == '__main__':
    main()
