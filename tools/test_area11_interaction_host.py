#!/usr/bin/env python3
"""Build and run the actual-asset AREA11 interaction host sanitizer fixture."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
MODULES = (
    'em_area11_interaction_host em_interaction_alignment em_interaction_projection '
    'em_interaction_scene em_interaction_scan em_interaction_runtime em_interaction_frame '
    'em_interaction_animation em_player_pose_host em_player_pose em_pose_bank em_pose_transition '
    'em_player_foot_stop em_camera em_camera_rotation em_camera_retarget '
    # The live camera (census L13..L16): the panel script's retarget and the
    # commit run on it (docs/CAMERA_LIVE.md).
    'em_camera_live em_camera_commit_original em_camera_follow_original em_camera_area11_specials '
    # The aim camera (camera actions 1 / 2 / 5, docs/CAMERA_LIVE.md section 7)
    # and the translations it calls.
    'em_camera_aim em_aim_fire_sdk_memory em_area22_port '
    'em_camera_leftovers em_camera_leftovers_solver em_census_standins em_render_verify_rest '
    'em_script_host_workers em_script_door_fan em_director_original em_player_closure_10_12_19 '
    'em_collision em_panel_runtime em_panel_program em_panel em_message_live em_message_service '
    'em_message_draw_original em_message_glyph_original '
    'em_elevator_runtime em_elevator_program em_elevator em_status_runtime em_status_frame '
    'em_status_page em_item_root em_menu_hover em_gs_texture em_page_draw em_status_pages_live em_status_pages_helpers em_status_pages_item em_status_pages_spr4 em_status_pages_parts em_area01_ui_pages em_item_ui em_item_trail em_item_sdk_math em_item_device '
    'em_battery_ui em_battery_page_live em_status_page_record em_message_presenters_live '
    'em_message_presenter_rest em_status_hub em_status_hub_ui em_status_draw em_status_models em_status_scene_original em_module_loader '
    'em_owner_services_original em_item_geometry em_pickup em_pickup_items_original em_pickup_owner em_pickup_program em_pickup_motion '
    'em_script em_frame em_fade em_random em_task em_face_slot em_roger_actor_original em_opening_face '
    'em_collision_world em_actor_collision em_actor_pool em_coll_probe_original em_coll_grid_hull em_coll_segment_walkers '
    'em_coll_list_passes em_coll_list_passes_walkers em_sdk_math_original em_sdk_soft_float em_effect_original '
    # The player's one pose owner (docs/PLAYER_CLIPS.md section 6).
    'em_player_record_pose em_pose_host_workers em_player_stage_workers em_player_floor '
    # 00182DF0's row lookup 0017B490 (em_loco_0017B490) in the release fixture.
    'em_locomotion_display em_anim_runtime_rest em_player_ladder_climb '
    'em_player_reaction em_player_fall em_stream_lanes_original '
    # The render context (census L32 / L30, docs/RENDER_CONTEXT.md section 8).
    'em_render_context_live em_gs_blocks_original em_frame_kick em_frame_render_heads em_render_context em_packet_chain_original '
    'em_status_ui_leftovers em_load_veil_particles em_actor_light_001D89D0 em_player_equipment '
    'em_owner_draw_live em_owner_draw_original em_object_unit em_point_light em_face_attach '
    # 001C69A0 for the status models' pose (em_status_models).
    'em_area01_math_core em_area01_math_actor'
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
