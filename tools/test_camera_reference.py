#!/usr/bin/env python3
"""Compare the native opening camera to a local paused original EE snapshot.

The snapshot must be taken after the original camera tick, while its mode is3.
Only comparison metadata is printed. No original game data is embedded here.
"""
import argparse
import ctypes as C
import json
from pathlib import Path
import struct
import subprocess
import sys


class Track(C.Structure):
    _fields_ = [('samples', C.POINTER(C.c_float)), ('sample_count', C.c_uint32),
                ('duration', C.c_float)]


class Frame(C.Structure):
    _fields_ = [('eye', C.c_float * 3), ('target', C.c_float * 3),
                ('roll', C.c_float), ('fov', C.c_float), ('cut', C.c_int)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ee', type=Path, required=True)
    parser.add_argument('--track', type=Path, required=True)
    parser.add_argument('--sample-time', type=float,
                        help='default: original camera cursor minus its last 0.5 advance')
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    build = root / 'build/camera_reference'
    build.mkdir(parents=True, exist_ok=True)
    library = build / ('camera.dylib' if sys.platform == 'darwin' else 'camera.so')
    # Exercise the same native matrix helper used by camera_commit_view.
    wrapper = build / 'view_reference.c'
    wrapper.write_text('''#include "em_math.h"
void reference_view(float *out, const float *eye, const float *target,
                    const float *up, float push) {
    float f[3], p[3];
    for (int i=0;i<3;i++) f[i]=target[i]-eye[i];
    float length=sqrtf(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]);
    for (int i=0;i<3;i++) { f[i]/=length; p[i]=eye[i]+push*f[i]; }
    em_mat4_lookat_gs(out,p,f,up);
}
''')
    subprocess.run(['cc', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC',
                    '-dynamiclib' if sys.platform == 'darwin' else '-shared',
                    '-Isrc', 'src/game/em_cinematic_camera.c', str(wrapper), '-lm',
                    '-o', str(library)], cwd=root, check=True)
    raw = args.ee.read_bytes()
    if len(raw) != 32 * 1024 * 1024 or raw[0x8101E4] != 3:
        raise SystemExit('Expected a 32MiB EE snapshot with camera mode3')
    sample_time = args.sample_time
    if sample_time is None:
        sample_time = struct.unpack_from('<f', raw, 0x810254)[0] - .5
    lib = C.CDLL(str(library))
    lib.em_cinematic_camera_load.argtypes = [C.POINTER(Track), C.c_char_p]
    lib.em_cinematic_camera_sample.argtypes = [C.POINTER(Track), C.c_float,
                                              C.POINTER(Frame)]
    lib.em_cinematic_camera_free.argtypes = [C.POINTER(Track)]
    track, frame = Track(), Frame()
    if lib.em_cinematic_camera_load(C.byref(track), str(args.track).encode()):
        raise SystemExit('Could not load camera track')
    try:
        if lib.em_cinematic_camera_sample(C.byref(track), sample_time, C.byref(frame)) != 1:
            raise SystemExit('Snapshot time is outside the active track')
        actual = struct.pack('<6f', *frame.eye, *frame.target)
        expected = raw[0x8105D0:0x8105DC] + raw[0x8105E0:0x8105EC]
        result = {'sample_time': sample_time, 'samples': track.sample_count,
                  'eye_target_bytes_match': actual == expected}
        floats = C.c_float * 16
        vector = C.c_float * 3
        up = vector(*struct.unpack_from('<3f', raw, 0x8105F0))
        original_view = struct.unpack_from('<16f', raw, 0x810610)
        # The captured GS view maps to the native view by negating its
        # Y/Z rows, including translation. The X row retains its sign.
        wanted = [-x if i % 4 in (1, 2) else x for i, x in enumerate(original_view)]
        lib.reference_view.argtypes = [C.POINTER(C.c_float)]*4 + [C.c_float]
        view, pushed = floats(), floats()
        lib.reference_view(view, frame.eye, frame.target, up, 0)
        lib.reference_view(pushed, frame.eye, frame.target, up, 4)
        result['cinematic_view_max_error'] = max(abs(a-b) for a,b in zip(view,wanted))
        result['incorrect_gameplay_push_max_error'] = max(abs(a-b) for a,b in zip(pushed,wanted))
        (build / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps(result))
        if actual != expected:
            raise SystemExit('Native camera coordinates differ from original runtime')
        if result['cinematic_view_max_error'] > .0002:
            raise SystemExit('Native cinematic view differs from original runtime')
        if result['incorrect_gameplay_push_max_error'] < 3.9:
            raise SystemExit('Snapshot does not distinguish the cinematic eye from the gameplay push')
    finally:
        lib.em_cinematic_camera_free(C.byref(track))


if __name__ == '__main__':
    main()
