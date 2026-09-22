#!/usr/bin/env python3
"""Run the actual-resource door/shared-player fixture under ASan and UBSan."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    output = ROOT/'build/door_program_reference'
    output.mkdir(parents=True, exist_ok=True)
    executable = output/'runtime_test'
    sources = ['em_door_original', 'em_door_original_runtime', 'em_door_transit',
        'em_door_program', 'em_script', 'em_pose_bank', 'em_pose_transition',
        'em_player_pose', 'em_interaction_animation', 'em_interaction_runtime',
        'em_interaction_frame', 'em_camera_rotation', 'em_interaction_scene',
        'em_interaction_scan', 'em_item_sdk_math']
    subprocess.run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
        '-ffp-contract=off', '-fsanitize=address,undefined', '-Isrc',
        'tests/door_program_test.c', *[f'src/game/{name}.c' for name in sources],
        'src/em_model.c', '-lm', '-o', str(executable)], cwd=ROOT, check=True)
    result = subprocess.run([str(executable)], cwd=ROOT, check=True,
                             text=True, stdout=subprocess.PIPE)
    (output/'runtime.log').write_text(result.stdout)
    print(result.stdout, end='')


if __name__ == '__main__': main()
