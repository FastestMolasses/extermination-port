#!/usr/bin/env python3
"""Check bank96 camera samples and player key decode against original code."""
import ctypes as C
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from test_camera_reference import Track, Frame
from test_item_sdk_math_reference import Original
from test_interaction_scan_reference import ELF_SHA
from test_pose_bank_reference import Bank
from test_pose_transition_reference import Original as Decoder, A, OUT
from test_point_light_reference import bits

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
TABLE, OUTPUT = 0x1400000, 0x1300000


def main():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    data = (DECOMP / 'extract/chunk15/f12_id44.bin').read_bytes()
    folder = ROOT / 'build/roger_cinematic'
    folder.mkdir(parents=True, exist_ok=True)
    library = folder / 'camera_player.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc',
                    'src/game/em_cinematic_camera.c', 'src/game/em_pose_bank.c',
                    'src/game/em_pose_transition.c', '-o', str(library)], cwd=ROOT, check=True)
    native = C.CDLL(str(library))
    native.em_cinematic_camera_load.argtypes = [C.POINTER(Track), C.c_char_p]
    native.em_cinematic_camera_free.argtypes = [C.POINTER(Track)]
    native.em_cinematic_camera_sample.argtypes = [C.POINTER(Track), C.c_float, C.POINTER(Frame)]
    native.em_pose_bank_load.argtypes = [C.POINTER(Bank), C.c_char_p]
    native.em_pose_bank_free.argtypes = [C.POINTER(Bank)]
    track, bank = Track(), Bank()
    resources = ROOT / 'assets/scene_snow/roger'
    assert native.em_cinematic_camera_load(C.byref(track), str(resources / 'encounter_camera.emcc').encode()) == 0
    assert native.em_pose_bank_load(C.byref(bank), str(resources / 'encounter_player.empc').encode())
    assert (track.duration, track.sample_count, bank.bone_count, bank.clip_count) == (691, 692, 21, 1)
    camera = 0x41000 + (struct.unpack_from('<I', data, 0x41004)[0] & ~3)
    duration = int(track.duration)
    times = [-1.] + [i * .5 for i in range(2*duration)] + [float(duration), duration+1.]
    camera_cases = 0
    for time in times:
        original = Original(elf)
        original.write(TABLE, data[camera:camera+16+32*track.sample_count])
        original.save(0x8106F3, 9, 1)
        original.save(0x275BFC, 17)
        original.run(0x1C7C00, (OUTPUT, TABLE), (time,))
        frame = Frame()
        result = native.em_cinematic_camera_sample(C.byref(track), time, C.byref(frame))
        assert result == original.r[2], time
        assert bytes(frame)[:32] == original.read(OUTPUT, 32), time
        if result:
            assert frame.cut == original.load(0x8106F3, 1), time
            assert original.load(0x275BFC) == (32 if frame.cut else 17), time
        camera_cases += 1
    header = 0x41000 + (struct.unpack_from('<I', data, 0x41008)[0] & ~3)
    clip = bank.clips[0]
    assert (clip.id, clip.duration, clip.next, clip.blend) == (1, 691, -2, 0)
    decoder = Decoder(elf)
    decoded = 0
    for bone in range(21):
        for kind in range(3):
            table = header + struct.unpack_from('<I', data, header+8+4*kind)[0]
            record = table + struct.unpack_from('<I', data, table+4*bone)[0]
            channel = clip.tracks[bone][kind]
            for index in range(channel.count-1):
                raw = data[record+12*index:record+12*index+12]
                for offset, value in enumerate(raw):
                    decoder.put(A+offset, value, 1)
                decoder.run(0x1C84D0 if kind == 0 else 0x1C85D0, (A, OUT))
                width = 4 if kind == 0 else 3
                assert [bits(channel.keys[index].value[k]) for k in range(width)] == decoder.floats(OUT, width)
                flags, time = struct.unpack_from('<HH', raw, 8)
                assert (channel.keys[index].time, channel.keys[index].hold) == (time, bool(flags & 0x8000))
                decoded += 1
    native.em_cinematic_camera_free(C.byref(track))
    native.em_pose_bank_free(C.byref(bank))
    report = {'original_camera_samples': camera_cases, 'original_player_decoded_keys': decoded,
              'scope': 'camera arithmetic/cut writes and key decoding; actor bind and playback timing separate'}
    (folder / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print('Roger cinematic original reference PASS:', json.dumps(report))


if __name__ == '__main__':
    main()
