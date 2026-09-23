#!/usr/bin/env python3
"""Original Roger scene1 camera driver, scalar projection and SDK rotation."""
import ctypes as C
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess

from test_camera_reference import Track
from test_item_sdk_math_reference import Original
from test_interaction_scan_reference import ELF_SHA
from test_point_light_reference import bits, number

ROOT = Path(__file__).resolve().parents[1]
CAMERA, TABLE = 0x8101E0, 0x1400000


class Projection(C.Structure):
    _fields_ = [('tangent', C.c_float*13)]


class Playback(C.Structure):
    _fields_ = [('track', C.POINTER(Track)), ('time', C.c_float),
                ('eye', C.c_float*4), ('target', C.c_float*4), ('up', C.c_float*4),
                ('zoom', C.c_float), ('cut_counter', C.c_uint32), ('auxiliary', C.c_uint8)]


Emit = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.POINTER(Playback))


def view(p):
    return (bytes(p.eye), bytes(p.target), bytes(p.up), bits(p.zoom), bits(p.time),
            p.cut_counter, p.auxiliary)


def original_view(o):
    return (o.read(0x8105D0, 16), o.read(0x8105E0, 16), o.read(0x8105F0, 16),
            o.load(0x70003B60), o.load(CAMERA+0x74), o.load(0x275BFC), o.load(0x8106F3, 1))


def main():
    elf = (ROOT.parent/'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    data = (ROOT.parent/'Extermination/extract/chunk15/f12_id44.bin').read_bytes()
    header = 0x41000+(struct.unpack_from('<I', data, 0x41004)[0] & ~3)
    out = ROOT/'build/cinematic_playback_reference'
    out.mkdir(parents=True, exist_ok=True)
    library = out/'camera.dylib'
    subprocess.run(['cc', '-shared', '-fPIC', '-std=c11', '-O2', '-Wall', '-Wextra',
        '-Werror', '-ffp-contract=off', '-Isrc', 'src/game/em_cinematic_playback.c',
        'src/game/em_cinematic_camera.c', 'src/game/em_camera_rotation.c', '-lm',
        '-o', str(library)], cwd=ROOT, check=True)
    native = C.CDLL(str(library))
    native.em_cinematic_camera_load.argtypes = [C.POINTER(Track), C.c_char_p]
    native.em_cinematic_camera_free.argtypes = [C.POINTER(Track)]
    native.em_cinematic_projection_load.argtypes = [C.POINTER(Projection), C.c_char_p]
    native.em_cinematic_projection_zoom.argtypes = [C.POINTER(Projection), C.c_float]
    native.em_cinematic_projection_zoom.restype = C.c_float
    native.em_cinematic_playback_start.argtypes = [C.POINTER(Playback), C.POINTER(Track), C.c_int]
    native.em_cinematic_playback_tick.argtypes = [C.POINTER(Playback), C.POINTER(Projection), Emit, C.c_void_p]
    native.em_cinematic_playback_wait.argtypes = [C.POINTER(Playback), Emit, C.c_void_p]
    track, projection = Track(), Projection()
    assets = ROOT/'assets/scene_snow/roger'
    assert native.em_cinematic_camera_load(C.byref(track), str(assets/'encounter_camera.emcc').encode()) == 0
    assert native.em_cinematic_projection_load(C.byref(projection), str(assets/'camera_projection.emcp').encode())
    assert bytes(projection) == elf[0x26C598-0x100000+0x300:0x26C598-0x100000+0x300+52]
    timeline = Original(elf)
    timeline.calls[0x21BAB0] = lambda o: o.r.__setitem__(2, 0x12345678)
    timeline.save(CAMERA+0x6E, 1, 2)
    timeline.run(0x22EC30, (CAMERA,))
    assert timeline.read(CAMERA+0x7C, 15) == bytes(15)
    rng = random.Random(0x22EEF0)
    times = [-1, 0, .5, 25, 25.5, 690.5, 691, 692]
    times += [i*.5 for i in range(1382)]
    times += [rng.uniform(0, 691) for _ in range(80)]
    cases = [(time, None, None) for time in times]
    cases += [(.25, roll, fov) for roll in (-180, -90, -1, 0, 1, 90, 180)
              for fov in (-100, -.1, .1, .5, 20, 45, 46, 100)]
    events = 0
    waits = 0
    captured = None
    for index, (time, roll, fov) in enumerate(cases):
        o = Original(elf)
        o.write(TABLE, data[header:header+16+32*track.sample_count])
        o.save(CAMERA+0x6E, 1, 2)
        o.save(CAMERA+0x70, TABLE)
        o.save(CAMERA+0x74, bits(time))
        o.save(CAMERA+0x78, bits(track.duration))
        p = Playback()
        original_sample = [track.samples[i] for i in range(16)]
        if roll is not None:
            # Synthetic projection boundaries retain the real driver and
            # camera format; original exported sample data stay untouched.
            for sample in range(2):
                o.save(TABLE+16+32*sample+24, bits(roll))
                o.save(TABLE+16+32*sample+28, bits(fov))
                track.samples[8*sample+6] = roll
                track.samples[8*sample+7] = fov
        p.eye[:] = (1, 2, 3, 7)
        p.target[:] = (4, 5, 6, 9)
        p.up[:] = (.25, -.75, .5, 3)
        p.zoom, p.cut_counter, p.auxiliary = 321, 17, 9
        assert native.em_cinematic_playback_start(C.byref(p), C.byref(track), 1)
        p.time = time
        for address, values in ((0x8105D0, p.eye), (0x8105E0, p.target), (0x8105F0, p.up)):
            o.write(address, bytes(values))
        o.save(0x70003B60, bits(p.zoom))
        o.save(0x275BFC, p.cut_counter)
        o.save(0x8106F3, p.auxiliary, 1)
        original_events, native_events = [], []
        for address, kind in [(0x1DD980, 0), (0x1B0250, 1), (0x21B9A0, 2), (0x1D2830, 3)]:
            o.calls[address] = lambda oracle, k=kind: original_events.append((k, original_view(oracle)))
        @Emit
        def emit(_context, kind, state):
            native_events.append((kind, view(state.contents)))
            return 1
        o.run(0x22EEF0, (CAMERA,))
        result = native.em_cinematic_playback_tick(C.byref(p), C.byref(projection), emit, None)
        assert result == int(0 <= time < 691), (index, time, result)
        assert original_events == native_events, ('event order/state', index, time)
        actual, expected = view(p), original_view(o)
        assert actual == expected, ('state', index, time,
            [(i, a.hex() if isinstance(a, bytes) else hex(a), b.hex() if isinstance(b, bytes) else hex(b))
             for i, (a, b) in enumerate(zip(actual, expected)) if a != b])
        events += len(original_events)
        if time == 25 and roll is None:
            ram = (ROOT.parent/'Extermination/build/startup-reference/roger-encounter/eeMemory.bin').read_bytes()
            world = struct.unpack_from('<I', ram, 0x275670)[0]
            assert bytes(p.up) == ram[0x8105F0:0x810600]
            assert bits(p.zoom) == struct.unpack_from('<I', ram, world+0x2468)[0]
            captured = {'camera_time': 25, 'up_bytes_exact': True,
                        'zoom_bytes_exact': True, 'zoom': p.zoom}
        if roll is None:
            original_events.clear()
            native_events.clear()
            o.save(0x1300000+8, 0)
            o.run(0x1B7B30, (0, 0, 0x1300000))
            result = native.em_cinematic_playback_wait(C.byref(p), emit, None)
            assert result == o.r[2] and original_events == native_events
            assert view(p) == original_view(o)
            waits += 1
            events += len(original_events)
        for i, value in enumerate(original_sample): track.samples[i] = value
    native.em_cinematic_camera_free(C.byref(track))
    raw = (assets/'camera_projection.emcp').read_bytes()
    invalid = [raw[:11], raw[:-1], raw+b'\0', b'BAD!'+raw[4:],
               raw[:8]+struct.pack('<I', 14)+raw[12:],
               raw[:12]+struct.pack('<I', 0x7FC00000)+raw[16:]]
    for i, value in enumerate(invalid): (out/f'bad_{i}.emcp').write_bytes(value)
    executable = out/'sanitizer_test'
    subprocess.run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
        '-ffp-contract=off', '-fsanitize=address,undefined', '-Isrc',
        'tests/cinematic_playback_test.c', 'src/game/em_cinematic_playback.c',
        'src/game/em_cinematic_camera.c', 'src/game/em_camera_rotation.c', '-lm',
        '-o', str(executable)], cwd=ROOT, check=True)
    result = subprocess.run([str(executable)], cwd=ROOT, check=True, text=True, stdout=subprocess.PIPE)
    (out/'sanitizer.log').write_text(result.stdout)
    report = {'original_driver_cases': len(cases), 'original_script_wait_cases': waits,
        'ordered_service_observations': events,
        'exact_state_bytes': True, 'original_capture': captured, 'sanitizer': 'PASS',
        'scope': 'scene1; full original sampler, tangent and rotation; projection publication and three end workers observed'}
    (out/'result.json').write_text(json.dumps(report, indent=2)+'\n')
    print('Original cinematic driver PASS:', json.dumps(report))


if __name__ == '__main__': main()
