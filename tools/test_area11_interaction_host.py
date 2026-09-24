#!/usr/bin/env python3
"""Build and run the actual-asset AREA11 interaction host sanitizer fixture."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
MODULES = (
    'em_area11_interaction_host em_interaction_alignment em_interaction_projection '
    'em_interaction_scene em_interaction_scan em_interaction_runtime em_interaction_frame '
    'em_interaction_animation em_player_pose_host em_player_pose em_pose_bank em_pose_transition '
    'em_player_foot_stop em_camera em_camera_rotation em_camera_probe em_camera_retarget '
    'em_collision em_panel_runtime em_panel_program em_panel em_panel_message '
    'em_elevator_runtime em_elevator_program em_elevator em_status_runtime em_status_frame '
    'em_status_page em_item_root em_item_ui em_item_trail em_item_sdk_math em_item_device '
    'em_battery_ui em_status_hub em_status_hub_ui em_status_draw em_status_models em_status_scene_original '
    'em_owner_services_original em_item_geometry em_pickup em_pickup_items_original em_pickup_owner em_pickup_program em_pickup_motion em_opening_media '
    'em_script em_frame em_fade em_random em_task em_player_face_host em_face_model em_opening_face'
).split()


def main():
    output = ROOT / 'build/area11_interaction_host'
    output.mkdir(parents=True, exist_ok=True)
    executable = output / 'host_test'
    subprocess.run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-ffp-contract=off', '-fsanitize=address,undefined', '-Wl,-dead_strip',
                    '-Isrc', 'tests/area11_interaction_host_test.c',
                    *(f'src/game/{module}.c' for module in MODULES),
                    'src/em_model.c', 'src/em_input.c', '-lm', '-o', str(executable)], cwd=ROOT, check=True)
    result = subprocess.run([str(executable)], cwd=ROOT, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (output / 'result.log').write_text(result.stdout)
    print(result.stdout, end='')
    result.check_returncode()


if __name__ == '__main__':
    main()
