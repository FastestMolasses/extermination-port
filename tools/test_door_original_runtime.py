#!/usr/bin/env python3
"""Validate canonical door assets, original node keys, channels and saved pose."""
import ctypes as C
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from test_pose_bank_reference import Bank, State
from test_pose_transition_reference import Original, A, OUT, bits
from test_interaction_scan_reference import ELF_SHA

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent/'Extermination'
SOURCES = ['src/game/em_door_original.c', 'src/game/em_door_original_runtime.c',
           'src/game/em_pose_bank.c', 'src/game/em_pose_transition.c',
           'src/game/em_camera_rotation.c', 'src/game/em_interaction_scene.c',
           'src/game/em_interaction_scan.c', 'src/em_model.c']


def main():
    build = ROOT/'build/door_original_reference'
    build.mkdir(parents=True, exist_ok=True)
    executable = build/'runtime_test'
    command = ['cc', '-std=c11', '-O1', '-g', '-ffp-contract=off', '-Wall', '-Wextra',
               '-Werror', '-Isrc']
    subprocess.run(command + ['-fsanitize=address,undefined',
        'tests/door_original_test.c', *SOURCES, '-lm', '-o', str(executable)], cwd=ROOT, check=True)
    subprocess.run([str(executable)], cwd=ROOT, check=True)
    bridge = build/'bridge.c'
    bridge.write_text('''#include "game/em_door_original_runtime.h"
static EmDoorOriginalRuntime door;
static EmInteractionScene scene;
static int draw(void *context) { (void)context; return 1; }
static int publish(void *context, const float *point) { (void)context; (void)point; return 1; }
int load(void) {
    EmDoorOriginalRuntimeHooks hooks = {.publish=publish, .draw=draw};
    return em_interaction_scene_load(&scene,"assets/scene_snow/interaction.emis") &&
        em_door_original_runtime_load(&door,"assets/scene_snow",
            em_interaction_scene_role(&scene,EM_INTERACTION_DOOR),&hooks);
}
const EmPoseBank *bank(void) { return &door.bank; }
const EmPosePlayback *state(void) { return &door.playback; }
const float *palette(void) { return door.palette; }
const float *matrix(void) { return door.matrix; }
void unload(void) { em_door_original_runtime_free(&door); }
''')
    library = build/'runtime.dylib'
    subprocess.run(command + ['-shared', '-fPIC', str(bridge), *SOURCES, '-lm',
                   '-o', str(library)], cwd=ROOT, check=True)
    native = C.CDLL(str(library))
    native.bank.restype = C.POINTER(Bank)
    native.state.restype = C.POINTER(State)
    native.palette.restype = native.matrix.restype = C.POINTER(C.c_float)
    assert native.load()
    bank, state = native.bank().contents, native.state().contents
    elf = (DECOMP/'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    source = (DECOMP/'extract/chunk27/f02_id39.bin').read_bytes()
    decoder = Original(elf)
    decoded = 0
    for ci in range(bank.clip_count):
        clip = bank.clips[ci]
        header = struct.unpack_from('<I', source, 4 + clip.id*4)[0] & ~3
        for bone in range(bank.bone_count):
            for kind in range(3):
                table = header + struct.unpack_from('<I', source, header + 8 + kind*4)[0]
                first = table + struct.unpack_from('<I', source, table + bone*4)[0]
                track = clip.tracks[bone][kind]
                for index in range(track.count - 1):
                    raw = source[first + 12*index:first + 12*index + 12]
                    for offset, value in enumerate(raw): decoder.put(A + offset, value, 1)
                    decoder.run(0x1c84d0 if kind == 0 else 0x1c85d0, (A, OUT))
                    width = 4 if kind == 0 else 3
                    assert [bits(track.keys[index].value[k]) for k in range(width)] == decoder.floats(OUT, width)
                    decoded += 1
    ram = (DECOMP/'build/startup-reference/playable_ee.bin').read_bytes()
    actor = 0x7a70b0
    u32 = lambda address: struct.unpack_from('<I', ram, address)[0]
    assert u32(actor + 16) == 0x1bc350 and state.remaining == 150
    comparisons = 0
    for bone in range(2):
        node = u32(actor + 0x110 + bone*4)
        rotation, translation, scale = state.nodes[bone]
        actual = {}
        for cursor, first in ((translation, 0), (scale, 24)):
            for component in range(3):
                actual[first + component*4] = bits(cursor.value[component])
                actual[first + 12 + component*4] = bits(cursor.velocity[component])
        keys = state.clip.contents.tracks[bone][0].keys
        for component in range(4):
            actual[48 + component*4] = bits(keys[rotation.index - 1].value[component])
            actual[64 + component*4] = bits(keys[rotation.index].value[component])
        actual.update({80: bits(rotation.fraction), 84: bits(rotation.reciprocal),
            88: bits(translation.remaining), 92: bits(scale.remaining), 96: bits(rotation.remaining)})
        for offset, value in actual.items():
            assert value == u32(node + offset), (bone, offset, hex(value), hex(u32(node + offset)))
            comparisons += 1
        assert (rotation.index, translation.index, scale.index) == struct.unpack_from('<3H', ram, node + 0x66)
        # C9940's optional node Euler/translation/fixed scales are identity.
        assert struct.unpack_from('<6f3h', ram, node + 0x70) == (0, 0, 0, 0, 0, 0, 4096, 4096, 4096)
    expected = [value for bone in range(2) for value in struct.unpack_from(
        '<16f', ram, u32(actor + 0x110 + bone*4) + 0x90)]
    palette = [native.palette()[i] for i in range(32)]
    error = max(abs(a - b) for a, b in zip(palette, expected))
    different = sum(bits(a) != bits(b) for a, b in zip(palette, expected))
    assert error < .0001, (error, different)
    assert [bits(native.matrix()[i]) for i in range(16)] == [u32(actor + 0xd0 + i*4) for i in range(16)]
    native.unload()
    report = dict(status='PASS', decoded_original_keys=decoded, captured_channel_floats=comparisons,
        captured_channel_differences=0, owner_matrix_exact_words=16,
        palette_words=32, palette_different_words=different, palette_max_error=error,
        limits='Initial clip0 channels and placement validated against original first-control RAM. '
               'Door transit scripts and output devices remain explicit worker boundaries.')
    (build/'runtime.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__': main()
