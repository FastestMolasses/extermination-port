#!/usr/bin/env python3
"""Compare both bank96 actors and camera with disposable original state15."""
import ctypes as C
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from test_pose_bank_reference import Bank, State
from test_pose_transition_reference import bits

ROOT = Path(__file__).resolve().parents[1]


def main():
    capture = ROOT.parent / 'Extermination/build/startup-reference/roger-encounter/eeMemory.bin'
    ram = capture.read_bytes()
    folder = ROOT / 'build/roger_cinematic'
    folder.mkdir(parents=True, exist_ok=True)
    bridge = folder / 'capture_bridge.c'
    bridge.write_text('''#include "game/em_player_pose.h"
int palette(const EmPoseBank *bank,unsigned clip,unsigned ticks,float *out) {
    EmPlayerPose pose;
    if(!em_player_pose_init(&pose,bank,clip,0))return 0;
    for(unsigned i=0;i<ticks;++i)if(!em_player_pose_advance(&pose,.5f,0))return 0;
    return em_player_pose_palette(&pose,out,22);
}
''')
    library = folder / 'capture.dylib'
    subprocess.run(['cc', '-shared', '-fPIC', '-std=c11', '-O2', '-ffp-contract=off', '-Isrc',
                    str(bridge), 'src/game/em_player_pose.c', 'src/game/em_pose_bank.c',
                    'src/game/em_pose_transition.c', '-o', str(library)], cwd=ROOT, check=True)
    native = C.CDLL(str(library))
    native.em_pose_bank_load.argtypes = [C.POINTER(Bank), C.c_char_p]
    native.em_pose_bank_free.argtypes = [C.POINTER(Bank)]
    native.em_pose_playback_begin.argtypes = [C.POINTER(State), C.POINTER(Bank), C.c_uint, C.c_float]
    native.em_pose_playback_advance.argtypes = [C.POINTER(State), C.c_float, C.c_int]
    native.palette.argtypes = [C.POINTER(Bank), C.c_uint, C.c_uint, C.POINTER(C.c_float)]
    rows = []
    for name, actor, clip in [('player', 0x8102B0, 1), ('roger', 0x7A8830, 2)]:
        bank, state = Bank(), State()
        resource = ROOT / f'assets/scene_snow/roger/encounter_{name}.empc'
        assert native.em_pose_bank_load(C.byref(bank), str(resource).encode())
        assert struct.unpack_from('<H', ram, actor+0x2C)[0] == clip
        assert struct.unpack_from('<I', ram, actor+0x40)[0] == struct.unpack_from('<I', ram, 0x28A490+0x96*4)[0]
        remaining = struct.unpack_from('<f', ram, actor+0x3C)[0]
        ticks = (691-remaining)*2
        assert ticks == int(ticks) and ticks >= 0
        assert native.em_pose_playback_begin(C.byref(state), C.byref(bank), clip, 0)
        for _ in range(int(ticks)):
            assert native.em_pose_playback_advance(C.byref(state), .5, 0)
        assert state.remaining == remaining
        checks = 0
        for bone in range(21):
            node = struct.unpack_from('<I', ram, actor+0x110+4*bone)[0]
            rotation, translation, scale = state.nodes[bone]
            values = {}
            for cursor, offset in ((translation, 0), (scale, 24)):
                for component in range(3):
                    values[offset+4*component] = bits(cursor.value[component])
                    values[offset+12+4*component] = bits(cursor.velocity[component])
            keys = state.clip.contents.tracks[bone][0].keys
            for component in range(4):
                values[48+4*component] = bits(keys[rotation.index-1].value[component])
                values[64+4*component] = bits(keys[rotation.index].value[component])
            values.update({80: bits(rotation.fraction), 84: bits(rotation.reciprocal),
                           88: bits(translation.remaining), 92: bits(scale.remaining),
                           96: bits(rotation.remaining)})
            for offset, value in values.items():
                assert value == struct.unpack_from('<I', ram, node+offset)[0], (name, bone, offset)
                checks += 1
            assert (rotation.index, translation.index, scale.index) == struct.unpack_from('<3H', ram, node+0x66)
            assert struct.unpack_from('<6f3h', ram, node+0x70) == (0, 0, 0, 0, 0, 0, 4096, 4096, 4096)
        # Both source owner matrices are identity: Dennis uses C6960, while
        # Roger's script zeros placement before his ordinary C68C0 builder.
        identity = tuple(float(i % 5 == 0) for i in range(16))
        assert struct.unpack_from('<16f', ram, actor+0xD0) == identity
        world = (C.c_float*(22*16))()
        assert native.palette(C.byref(bank), clip, int(ticks), world)
        maximum = 0
        for bone in range(21):
            node = struct.unpack_from('<I', ram, actor+0x110+4*bone)[0]
            expected = struct.unpack_from('<16f', ram, node+0x90)
            maximum = max(maximum, max(abs(world[bone*16+i]-value) for i, value in enumerate(expected)))
        assert maximum < .0002, (name, maximum)
        assert struct.unpack_from('<h', ram, actor+0x94)[0] == 7
        face = struct.unpack_from('<I', ram, actor+0x90)[0]
        assert face and ram[face+0x81] == 1
        rows.append({'actor': name, 'source_frame': ticks/2, 'exact_channel_floats': checks,
                     'exact_cursor_indices': 63, 'world_matrix_max_error': maximum,
                     'face_attached': True})
        native.em_pose_bank_free(C.byref(bank))
    camera = subprocess.run(['python3', 'tools/test_camera_reference.py', '--ee', str(capture),
        '--track', 'assets/scene_snow/roger/encounter_camera.emcc'], cwd=ROOT, check=True,
        text=True, stdout=subprocess.PIPE)
    report = {'capture_sha256': hashlib.sha256(ram).hexdigest(), 'actors': rows,
              'camera': json.loads(camera.stdout),
              'scope': 'original runtime capture after position-seeded trigger; movement, face RNG and rasterization separate'}
    (folder / 'capture.json').write_text(json.dumps(report, indent=2)+'\n')
    print('Original Roger encounter capture PASS:', json.dumps(report))


if __name__ == '__main__': main()
