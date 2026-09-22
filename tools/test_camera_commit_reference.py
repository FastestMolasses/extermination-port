#!/usr/bin/env python3
"""Validate original0018C0D0's argument/mode offset gate and native commits.

The original scalar control flow executes from the user's ELF. SDK vector,
normalization, matrix and angle routines are explicit boundaries. Exact-axis
camera vectors isolate the offset gate from normalization/rounding questions.
The native test calls the actual em_camera.c public commit functions.
"""
import ctypes as C
import hashlib
import itertools
import json
import math
from pathlib import Path
import struct
import subprocess
import tempfile

from test_interaction_scan_reference import ScanOracle, ELF_SHA, bits, number

ROOT = Path(__file__).resolve().parents[1]
CAMERA = 0x8101E0
EYE, TARGET = (32.0, 16.0, 8.0), (32.0, 16.0, 24.0)

BRIDGE = r'''
#include "game/em_camera.h"
EmGameState g;
const float kLocoTierSpeed[4] = {0};
void em_frame_request_quit(void) { abort(); }
void probe(unsigned top, unsigned mode, int argument, unsigned route, float *view) {
    memset(&g, 0, sizeof g);
    g.cam.top_mode = top;
    g.cam.mode = mode;
    const float eye[3] = {32, 16, 8}, target[3] = {32, 16, 24};
    memcpy(g.cam.eye, eye, sizeof eye);
    memcpy(g.cam.eye_des, eye, sizeof eye);
    memcpy(g.cam.tgt, target, sizeof target);
    memcpy(g.cam.tgt_des, target, sizeof target);
    g.cam.up[1] = -1;
    g.cam.zoom = 480;
    if (route == 1) camera_commit(&g.cam);
    else if (route == 2) camera_commit_cinematic(&g.cam);
    else camera_commit_original(&g.cam, argument);
    memcpy(view, g.cam.view, 16 * sizeof(float));
}
'''


def original_eye(elf, top, mode, argument):
    original = ScanOracle(elf)
    original.save(CAMERA + 4, top, 1)
    original.save(CAMERA + 6, mode, 1)
    for address, values in ((0x8105D0, EYE), (0x8105E0, TARGET),
                            (CAMERA + 0x10, EYE), (CAMERA + 0x20, TARGET)):
        original.write(address, struct.pack('<4f', *values, 1))
    captured = []

    def subtract(o):
        left = struct.unpack('<4f', o.read(o.r[5], 16))
        right = struct.unpack('<4f', o.read(o.r[6], 16))
        o.write(o.r[4], struct.pack('<4f', *(a - b for a, b in zip(left, right))))

    def normalize(o):
        assert struct.unpack('<3f', o.read(o.r[5], 12)) == (0, 0, 16)
        o.write(o.r[4], struct.pack('<4f', 0, 0, 1, 1))

    def copy(o, size):
        o.write(o.r[4], o.read(o.r[5], size))

    original.calls[0x1028D0] = subtract
    original.calls[0x102760] = normalize
    original.calls[0x1031E0] = lambda o: copy(o, 12)
    original.calls[0x102948] = lambda o: copy(o, 16)
    original.calls[0x11E748] = lambda o: o.f.__setitem__(0, bits(math.sqrt(number(o.f[12]))))
    original.calls[0x11DF78] = lambda o: o.f.__setitem__(0, o.f[12] & 0x7FFFFFFF)
    original.calls[0x102CD0] = lambda o: captured.append(o.read(o.r[5], 12))
    for address in (0x102798, 0x11E620, 0x1B1240):
        original.calls[address] = lambda o: o.f.__setitem__(0, 0)
    original.run(0x18C0D0, (CAMERA, argument & 0xFFFFFFFF))
    assert len(captured) == 1
    return struct.unpack('<3f', captured[0])


def main():
    elf = (ROOT.parent / 'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    cases = set(itertools.product((0, 1, 2, 3, 255), (0, 1, 2, 3, 10, 255), (0, 1, 2, -1)))
    cases.update((top, mode, argument) for top in range(256)
                 for mode, argument in ((0, 0), (1, 0), (10, 1)))
    cases.update((top, mode, argument) for mode in range(256)
                 for top, argument in ((0, 0), (3, 0), (3, 1)))
    with tempfile.TemporaryDirectory(prefix='camera_commit_reference_') as temporary:
        wrapper = Path(temporary) / 'bridge.c'
        wrapper.write_text(BRIDGE)
        library = Path(temporary) / 'commit.dylib'
        subprocess.run(['cc', '-std=c11', '-O2', '-ffp-contract=off', '-fPIC',
                        '-shared', '-Wl,-dead_strip', '-Wl,-exported_symbol,_probe', '-Isrc',
                        'src/game/em_camera.c', str(wrapper), '-lm', '-o', str(library)],
                       cwd=ROOT, check=True)
        native = C.CDLL(str(library))
        native.probe.argtypes = [C.c_uint, C.c_uint, C.c_int, C.c_uint, C.POINTER(C.c_float)]
        for top, mode, argument in sorted(cases):
            expected = original_eye(elf, top, mode, argument)
            for route in (0, 1 if argument == 1 else 2 if argument == 0 else 0):
                view = (C.c_float * 16)()
                native.probe(top, mode, argument, route, view)
                # For forward+Z/up-Y, native view rows are(-X,+Y,-Z).
                actual = (view[12], -view[13], view[14])
                assert actual == expected, (top, mode, argument, route, actual, expected)
    result = {'original_gate_cases': len(cases), 'native_public_wrappers': 'PASS',
              'status_argument1_top3': 'push4 except mode0xA uses minus1',
              'scope': 'offset gate and actual native view; exact-axis helper boundaries'}
    output = ROOT / 'build/camera_commit_reference'
    output.mkdir(parents=True, exist_ok=True)
    (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print('camera commit original reference PASS:', json.dumps(result))


if __name__ == '__main__':
    main()
