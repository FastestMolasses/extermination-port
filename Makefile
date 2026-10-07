# Extermination native port — build.
#
# Clean-room: no third-party libraries, no build-system dependencies beyond
# make + the platform's own compiler/frameworks. Per-platform source + flags
# are selected from `uname`. Cross-compilation targets get their own rules as
# the Windows/Linux backends are implemented.

UNAME := $(shell uname)

BIN     := build/extermination
CFLAGS  := -O2 -Wall -Wextra -Isrc
COMMON  := src/main.c src/em_model.c src/em_input.c src/em_settings.c \
           src/game/em_fade.c src/game/em_startup.c src/game/em_frontend.c src/game/em_startup_audio.c \
           src/game/em_task.c src/game/em_frame.c src/game/em_game.c src/game/em_player_frame.c src/game/em_render_frame.c src/game/em_game_selftest.c src/game/em_props.c src/game/em_scene.c src/game/em_camera.c src/game/em_player_damage.c src/game/em_player.c src/game/em_player_heading.c src/game/em_player_motor.c src/game/em_script.c src/game/em_area11_opening.c \
           src/game/em_opening_runtime.c src/game/em_cinematic_camera.c src/game/em_random.c src/game/em_opening_control_test.c src/game/em_aim_fire_test.c src/game/em_level_smoke_test.c src/game/em_new_game_switch.c \
           src/game/em_opening_face.c src/game/em_opening_media.c src/game/em_lighting.c \
           src/game/em_point_light.c \
           src/game/em_area11_effect.c src/game/em_area11_effect_runtime.c \
           src/game/em_weather.c src/game/em_snow.c src/game/em_weather_packets.c src/game/em_snow_runtime.c \
           src/game/em_collision.c src/game/em_actor_collision.c src/game/em_coll_probe_original.c src/game/em_coll_grid_hull.c src/game/em_coll_move_original.c \
           src/game/em_coll_segment_walkers.c src/game/em_coll_list_passes.c src/game/em_coll_list_passes_walkers.c \
           src/game/em_collision_world.c src/game/em_sdk_soft_float.c src/game/em_effect_original.c src/game/em_door.c src/game/em_door_candidate.c src/game/em_door_original.c src/game/em_door_original_runtime.c src/game/em_door_transit.c src/game/em_door_program.c src/game/em_area11_door.c src/game/em_bgm.c \
           src/game/em_sfx.c src/game/em_sfx_bank.c src/game/em_pickup.c src/game/em_pickup_owner.c src/game/em_pickup_program.c src/game/em_pickup_motion.c src/game/em_pickup_items_original.c src/game/em_roger.c \
           src/game/em_face_slot.c \
           src/game/em_examine.c src/game/em_panel.c src/game/em_panel_program.c src/game/em_panel_runtime.c src/game/em_battery_ui.c src/game/em_battery_page_live.c src/game/em_status_page_record.c src/game/em_camera_retarget.c \
           src/game/em_camera_rotation.c src/game/em_camera_live.c src/game/em_camera_commit_original.c \
           src/game/em_camera_follow_original.c src/game/em_camera_area11_specials.c src/game/em_camera_aim.c src/game/em_area22_port.c src/game/em_area00_low.c \
           src/game/em_camera_leftovers.c src/game/em_camera_leftovers_solver.c src/game/em_census_standins.c \
           src/game/em_script_door_fan.c src/game/em_interaction_frame.c src/game/em_interaction_animation.c \
           src/game/em_security_gun.c src/game/em_security_gun_rest.c src/game/em_fan_original.c \
           src/game/em_interaction_alignment.c src/game/em_interaction_projection.c src/game/em_area11_interaction_host.c \
           src/game/em_interaction_runtime.c src/game/em_interaction_cinematic.c src/game/em_interaction_scan.c src/game/em_interaction_scene.c src/game/em_status_frame.c \
           src/game/em_status_page.c src/game/em_item_root.c src/game/em_menu_hover.c src/game/em_item_ui.c \
           src/game/em_gs_texture.c src/game/em_page_draw.c src/game/em_status_pages_live.c \
           src/game/em_status_pages_helpers.c src/game/em_status_pages_item.c src/game/em_status_pages_spr4.c \
           src/game/em_status_pages_parts.c src/game/em_area01_ui_pages.c src/game/em_area01_ui_effect.c src/game/em_area01_render_gs.c \
           src/game/em_item_trail.c src/game/em_item_sdk_math.c src/game/em_item_device.c \
           src/game/em_item_geometry.c src/game/em_status_hub.c src/game/em_status_draw.c src/game/em_status_hub_ui.c \
           src/game/em_status_runtime.c src/game/em_status_background.c src/game/em_status_background_draw.c \
           src/game/em_sdk_math_original.c src/game/em_status_scene_original.c src/game/em_module_loader.c src/game/em_status_models.c \
           src/game/em_owner_services_original.c src/game/em_owner_draw_original.c src/game/em_object_unit.c src/game/em_owner_draw_live.c src/game/em_face_attach.c \
           src/game/em_indicator_child.c src/game/em_indicator_bind_live.c src/game/em_effect_kinds.c \
           src/game/em_packet_chain_original.c src/game/em_status_ui_leftovers.c src/game/em_area_title.c src/game/em_background_live.c \
           src/game/em_crate_original.c src/game/em_drum_original.c src/game/em_area11_boxes.c src/game/em_area11_roger.c \
           src/game/em_roger_actor_original.c \
           src/game/em_message_service.c src/game/em_message_draw_original.c src/game/em_message_glyph_original.c \
           src/game/em_message_live.c src/game/em_message_presenter_rest.c \
           src/game/em_message_presenters_live.c \
           src/game/em_pose_bank.c src/game/em_pose_transition.c src/game/em_player_pose.c src/game/em_player_pose_host.c \
           src/game/em_player_foot_stop.c src/game/em_player_floor.c \
           src/game/em_player_stage_workers.c src/game/em_player_stage_live.c \
           src/game/em_player_record_pose.c src/game/em_pose_host_workers.c \
           src/game/em_player_reaction.c src/game/em_player_fall.c src/game/em_stream_lanes_original.c \
           src/game/em_iop_stream.c src/game/em_stream_live.c src/game/em_ee_sound_lib.c src/game/em_sound_bank.c \
           src/game/em_player_closure_live.c src/game/em_player_hang.c src/game/em_player_recovery.c \
           src/game/em_player_ladder_climb.c src/game/em_player_ladder_entry.c \
           src/game/em_player_closure_0e_18.c src/game/em_player_closure_10_12_19.c \
           src/game/em_player_slide.c src/game/em_player_climb.c src/game/em_player_weapon_states_a.c \
           src/game/em_player_weapon_states_b.c src/game/em_player_major2.c \
           src/game/em_aim_fire_control.c src/game/em_aim_fire_machines.c src/game/em_aim_fire_pose.c \
           src/game/em_aim_fire_target.c src/game/em_aim_fire_shots.c src/game/em_aim_fire_reticle.c \
           src/game/em_aim_fire_tables.c src/game/em_aim_fire_leaves.c \
           src/game/em_aim_fire_live.c src/game/em_aim_fire_marker.c src/game/em_aim_fire_flash.c src/game/em_aim_fire_trail.c src/game/em_bone_burst.c src/game/em_effect_001F77B0.c src/game/em_aim_fire_binding.c \
           src/game/em_aim_fire_lamp.c \
           src/game/em_aim_fire_render_live.c src/game/em_aim_fire_world_live.c src/game/em_aim_fire_runtime.c \
           src/game/em_aim_fire_sdk_memory.c src/game/em_aim_fire_cable_live.c src/game/em_area06_port_strip.c \
           src/game/em_area00_hud.c src/game/em_area00_world.c src/game/em_area00_fx_exit.c src/game/em_area00_fx_spawn.c src/game/em_area00_fx_gs.c \
           src/game/em_area00_fx_debris.c src/game/em_area00_fx_trail.c src/game/em_area02_misc.c src/game/em_level8_port_fx.c \
           src/game/em_area01_side.c src/game/em_area02_math.c \
           src/game/em_player_running_jump.c src/game/em_player_use_dispatch.c \
           src/game/em_player_record_helpers.c src/game/em_player_heading_record.c \
           src/game/em_player_misc_workers.c src/game/em_script_host_workers.c \
           src/game/em_render_verify_rest.c src/game/em_locomotion_display.c \
           src/game/em_anim_runtime_rest.c src/game/em_startup_load_gaps.c src/game/em_startup_load_gaps_sound.c \
           src/game/em_elevator.c src/game/em_elevator_program.c src/game/em_elevator_runtime.c \
           src/game/em_hud.c src/game/em_weapon.c src/game/em_enemy.c \
           src/game/em_scene_bindings.c src/game/em_scene_task.c src/game/em_scene_frame.c \
           src/game/em_scene_classify.c src/game/em_frame_trace.c \
           src/game/em_actor_pool.c src/game/em_actor_roster.c src/game/em_area11_bindings.c \
           src/game/em_area01_arrival.c src/game/em_area01_state.c \
           src/game/em_area01_actor_view.c src/game/em_area01_player_view.c src/game/em_area01_scene_view.c \
           src/game/em_area01_model_live.c src/game/em_area01_runtime.c src/game/em_area01_live.c src/game/em_area01_script_live.c src/game/em_area01_script_workers.c src/game/em_area01_revisit.c src/game/em_area01_pickup_live.c src/game/em_area01_effects_services.c src/game/em_area01_timeline.c src/game/em_area01_audio_services.c \
           src/game/em_area01_model_draw.c src/game/em_area01_door_live.c \
           src/game/em_area01_collision_view.c src/game/em_area01_interaction_live.c src/game/em_area01_room.c src/game/em_area01_camera_services.c src/game/em_area01_indicator_live.c src/game/em_area01_prop_live.c src/game/em_area01_gun_aux.c src/game/em_area01_shared_services.c src/game/em_area01_light_live.c \
           src/game/em_area01_math_core.c src/game/em_area01_math_actor.c src/game/em_area01_math_owner.c src/game/em_area01_math_player.c \
           src/game/em_area01_light_owner.c src/game/em_area01_overlay.c src/game/em_area01_overlay_826d40.c \
           src/game/em_area01_sys.c src/game/em_area01_exita.c src/game/em_area01_exitb.c \
           src/game/em_world_textures_live.c src/game/em_area01_rcl_workers.c src/game/em_area01_flame_services.c src/game/em_area01_render_hud.c src/game/em_area01_matrix_service.c \
           src/game/em_spawn_table.c src/game/em_load_veil.c src/game/em_manager_008257A0.c \
           src/game/em_director_original.c \
           src/game/em_area_script.c src/game/em_cinematic_playback.c src/game/em_area11_script_host.c \
           src/game/em_truck_original.c src/game/em_pad_actuator.c \
           src/game/em_frame_render_heads.c src/game/em_render_context.c src/game/em_render_context_live.c src/game/em_area01_render_vif.c \
           src/game/em_replay.c \
           src/game/em_static_world.c src/game/em_static_world_compose.c src/game/em_static_world_draw.c \
           src/game/em_static_world_live.c \
           src/game/em_load_veil_particles.c src/game/em_actor_light_001D89D0.c src/game/em_player_equipment.c \
           src/game/em_effect_manager.c src/game/em_head_sprite_original.c src/game/em_player_equipment_sprite.c \
           src/game/em_effects_live.c src/game/em_equipment_live.c src/game/em_player_draw_live.c \
           src/game/em_shadow_original.c src/game/em_shadow_actor_route.c src/game/em_shadow_decal_original.c \
           src/game/em_shadow_live.c src/game/em_chain_page.c src/game/em_chain_page_live.c src/game/em_load_veil_live.c \
           src/game/em_gs_blocks_original.c src/game/em_frame_kick.c

# ---------------------------------------------------------------- macOS
ifeq ($(UNAME),Darwin)
CC        := clang
SRC       := $(COMMON) \
             src/platform/mac/em_platform_mac.m src/platform/mac/em_gamepad_mac.m \
             src/platform/mac/em_movie_mac.m \
             src/gfx/metal/em_gfx_metal.m \
             src/audio/mac/em_audio_mac.c
FRAMEWORKS := -framework Cocoa -framework GameController -framework Metal -framework QuartzCore \
              -framework AudioToolbox -framework AVFoundation -framework CoreMedia -framework CoreVideo
LDFLAGS   := $(FRAMEWORKS)
endif

# ---------------------------------------------------------------- Linux
ifeq ($(UNAME),Linux)
CC        := cc
SRC       := $(COMMON) \
             src/platform/em_movie_unavailable.c \
             src/platform/linux/em_platform_linux.c \
             src/gfx/vulkan/em_gfx_vk.c \
             src/audio/linux/em_audio_linux.c
LDFLAGS   := -lX11 -lvulkan -lm
endif

# Windows is built via its own toolchain (cl/clang-cl); see src/platform/win
# and src/gfx/d3d12. A nmake/msbuild project will be added when that backend
# is implemented.

.PHONY: all clean run test-input
all: $(BIN)

$(BIN): $(SRC) $(wildcard src/*.h src/game/*.h)
	@mkdir -p build
	$(CC) $(CFLAGS) $(SRC) $(LDFLAGS) -o $(BIN)

run: $(BIN)
	$(BIN)

# ---------------------------------------------------------------- iOS
# Cross-built on macOS with the iOS SDK (docs/IOS.md): the same game code,
# the UIKit layer (src/platform/ios), the RemoteIO audio backend
# (src/audio/ios) and the Apple code macOS already uses (GameController pad,
# AVFoundation movies, Metal renderer), compiled into a static library.
# tools/ios/build.sh links, bundles and signs it with
# tools/ios/Extermination.xcodeproj. main.c's main() is the engine bring-up;
# UIKit owns main() on iOS and runs it as em_port_main() on the game thread.
#   make ios-lib IOS_SDK=iphoneos | iphonesimulator
IOS_SDK    := iphoneos
IOS_MIN    := 17.0
IOS_DIR    := build/ios/$(IOS_SDK)
IOS_TRIPLE := arm64-apple-ios$(IOS_MIN)$(if $(filter iphonesimulator,$(IOS_SDK)),-simulator,)
IOS_CC     := xcrun --sdk $(IOS_SDK) clang -target $(IOS_TRIPLE)
IOS_SRC    := $(COMMON) \
              src/platform/ios/em_platform_ios.m \
              src/platform/mac/em_gamepad_mac.m src/platform/mac/em_movie_mac.m \
              src/gfx/metal/em_gfx_metal.m \
              src/audio/ios/em_audio_ios.m
IOS_OBJ    := $(IOS_SRC:%=$(IOS_DIR)/obj/%.o)
IOS_LIB    := $(IOS_DIR)/libextermination.a

.PHONY: ios-lib
ios-lib: $(IOS_LIB)

$(IOS_LIB): $(IOS_OBJ)
	@rm -f $@
	xcrun --sdk $(IOS_SDK) libtool -static -o $@ $(IOS_OBJ)

$(IOS_DIR)/obj/src/main.c.o: IOS_DEFS := -Dmain=em_port_main

$(IOS_DIR)/obj/%.o: %
	@mkdir -p $(@D)
	$(IOS_CC) $(CFLAGS) $(IOS_DEFS) -MMD -MP -c $< -o $@

-include $(wildcard $(IOS_OBJ:.o=.d))

# Unit test for the OS-free input model: links only em_input.c + the test,
# no platform/gfx/audio objects, so it runs headless on any host.
test-input: tests/input_test.c src/em_input.c
	@mkdir -p build
	$(CC) $(CFLAGS) tests/input_test.c src/em_input.c -o build/input_test
	./build/input_test

# OS-free task table checks: registration, promotion, dispatch ordering,
# current-task replacement, and the original's preserved trailing bytes.
.PHONY: test-task
test-task: tests/task_test.c src/game/em_task.c src/game/em_task.h
	@mkdir -p build
	$(CC) $(CFLAGS) tests/task_test.c src/game/em_task.c -o build/task_test
	./build/task_test

.PHONY: test-fade
test-fade: tests/fade_test.c src/game/em_fade.c src/game/em_fade.h \
           src/game/em_frame.c src/game/em_frame.h src/game/em_task.c src/em_input.c
	@mkdir -p build
	$(CC) $(CFLAGS) tests/fade_test.c src/game/em_fade.c src/game/em_frame.c \
	    src/game/em_task.c src/em_input.c -o build/fade_test
	./build/fade_test

clean:
	rm -rf build

.PHONY: test-startup test-movie-export test-startup-audio
test-startup: tests/startup_test.c src/game/em_startup.c src/game/em_startup.h src/game/em_fade.c src/game/em_fade.h
	@mkdir -p build
	$(CC) $(CFLAGS) tests/startup_test.c src/game/em_startup.c src/game/em_fade.c -o build/startup_test
	./build/startup_test

test-movie-export:
	python3 tests/movie_export_test.py

test-startup-audio: tests/startup_audio_test.c src/game/em_startup_audio.c src/game/em_startup_audio.h
	@mkdir -p build
	$(CC) $(CFLAGS) tests/startup_audio_test.c src/game/em_startup_audio.c -o build/startup_audio_test
	./build/startup_audio_test

# AREA11 event gates: no graphics/audio or disc data required.
# Distinct original segment, player-movement, and camera surface gates.
.PHONY: test-collision
test-collision: tests/collision_test.c src/game/em_collision.c src/game/em_collision.h
	@mkdir -p build
	$(CC) $(CFLAGS) tests/collision_test.c src/game/em_collision.c -lm -o build/collision_test
	build/collision_test

.PHONY: test-script test-area11-opening
test-script: tests/script_test.c src/game/em_script.c src/game/em_script.h
	@mkdir -p build
	$(CC) $(CFLAGS) tests/script_test.c src/game/em_script.c -o build/script_test
	build/script_test

test-area11-opening: tests/area11_opening_test.c src/game/em_area11_opening.c src/game/em_script.c
	@mkdir -p build
	$(CC) $(CFLAGS) tests/area11_opening_test.c src/game/em_area11_opening.c src/game/em_script.c -o build/area11_opening_test
	build/area11_opening_test

.PHONY: test-area11-opening-reference
test-area11-opening-reference:
	python3 tools/test_area11_opening_reference.py

.PHONY: test-cinematic-camera
test-cinematic-camera: tests/cinematic_camera_test.c src/game/em_cinematic_camera.c src/game/em_cinematic_camera.h
	@mkdir -p build
	$(CC) $(CFLAGS) -ffp-contract=off tests/cinematic_camera_test.c src/game/em_cinematic_camera.c -lm -o build/cinematic_camera_test
	build/cinematic_camera_test

.PHONY: test-script-reference

.PHONY: test-opening-face-reference
test-opening-face-reference:
	python3 tools/test_opening_face_reference.py

test-script-reference:
	python3 tools/test_script_reference.py

.PHONY: test-opening-media
test-opening-media: tests/opening_media_test.c src/game/em_opening_media.c
	@mkdir -p build
	$(CC) $(CFLAGS) tests/opening_media_test.c src/game/em_opening_media.c -lm -o build/opening_media_test
	build/opening_media_test

.PHONY: test-pickup-lights
test-pickup-lights: tests/pickup_light_test.c src/game/em_pickup.c src/game/em_pickup.h src/game/em_effect_kinds.c
	@mkdir -p build
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc -ffp-contract=off tests/pickup_light_test.c src/game/em_effect_kinds.c $(PICKUP_ORIGINAL_TEST_SRC) -o build/pickup_light_test
	build/pickup_light_test

PICKUP_ORIGINAL_TEST_SRC := src/game/em_pickup_items_original.c src/game/em_pickup_owner.c src/game/em_pickup_program.c src/game/em_script.c src/game/em_interaction_runtime.c src/game/em_interaction_frame.c src/game/em_interaction_animation.c
.PHONY: test-pickup-owner-reference test-pickup-original
test-pickup-owner-reference:
	python3 tools/test_pickup_owner_reference.py
	python3 tools/test_pickup_motion_reference.py

.PHONY: test-pickup-items-reference
test-pickup-items-reference:
	python3 tools/test_pickup_items_reference.py

.PHONY: test-roger-reference
test-roger-reference:
	python3 tools/test_roger_reference.py
	python3 tools/test_roger_pose_reference.py

.PHONY: test-door-original test-door-original-runtime
test-door-original:
	python3 tools/test_door_original_reference.py

test-door-original-runtime:
	python3 tools/test_door_original_runtime.py

.PHONY: test-cinematic-playback-reference
test-cinematic-playback-reference:
	python3 tools/test_cinematic_playback_reference.py

.PHONY: test-continue-reset-reference
test-continue-reset-reference:
	python3 tools/test_continue_reset_reference.py

.PHONY: test-area11-fog-reference
test-area11-fog-reference:
	python3 tools/test_area11_fog_reference.py

.PHONY: test-player-random-reference
test-player-random-reference:
	python3 tools/test_player_random_reference.py

.PHONY: test-input-block-reference
test-input-block-reference:
	python3 tools/test_input_block_reference.py

.PHONY: test-title-menu-reference
test-title-menu-reference:
	python3 tools/test_title_menu_reference.py

.PHONY: test-random-seed-reference
test-random-seed-reference:
	python3 tools/test_random_seed_reference.py

# The rand() call order against the decomp's C7 per-call capture (a headless
# New Game to first control + 30 ticks with EM_RAND_TRACE; about 10 s).
# docs/RAND_ORDER.md.
.PHONY: test-rand-order
test-rand-order: $(BIN)
	python3 tools/test_rand_order.py

.PHONY: test-frame-input
test-frame-input:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/frame_input_test.c src/game/em_frame.c src/game/em_fade.c src/game/em_task.c src/em_input.c -lm -o build/frame_input_test && ./build/frame_input_test

.PHONY: test-scene-classify
test-scene-classify:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/scene_classify_test.c src/game/em_scene_classify.c -o build/scene_classify_test && ./build/scene_classify_test

.PHONY: test-scene-no-shadow
test-scene-no-shadow:
	python3 tools/test_scene_no_shadow.py

.PHONY: test-scene-classify-reference
test-scene-classify-reference:
	python3 tools/test_scene_classify_reference.py

.PHONY: test-scene-frame-reference
test-scene-frame-reference:
	python3 tools/test_scene_frame_reference.py

.PHONY: test-scene-task-reference
test-scene-task-reference:
	python3 tools/test_scene_task_reference.py

.PHONY: test-actor-pool
test-actor-pool:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/actor_pool_test.c src/game/em_actor_pool.c -o build/actor_pool_test && ./build/actor_pool_test
	python3 tools/test_actor_pool_reference.py

.PHONY: test-frame-trace
test-frame-trace:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/frame_trace_test.c src/game/em_frame_trace.c -o build/frame_trace_test && ./build/frame_trace_test
	python3 tools/compare_frame_order.py --self-test

.PHONY: test-actor-census
test-actor-census:
	python3 tools/test_actor_census_reference.py

# S12a: spawn placement 001B07C0/001B0250 over the exported D_0024D650 window
# (tools/export_spawn_table.py), the record-13 manager 008257A0, and the area
# load: the veil 0021B180/0021B550/0021B840 plus a headless New Game run
# (first control, then an EM_AREA_CHANGE_TEST reload) whose chain ticks are
# replayed through the executed original.
.PHONY: test-spawn-place-reference
test-spawn-place-reference:
	python3 tools/test_spawn_place_reference.py

.PHONY: test-manager-8257a0-reference
test-manager-8257a0-reference:
	python3 tools/test_manager_8257a0_reference.py

.PHONY: test-area-load-reference
test-area-load-reference: $(BIN)
	mkdir -p build/area_load_reference
	EM_UNCAPPED=1 EM_STARTUP_TEST=newgame-control EM_AREA_CHANGE_TEST=1 \
	    EM_AREA_CHANGE_LOG=build/area_load_reference/ticks.jsonl $(BIN) > build/area_load_reference/run.log 2>&1
	grep -q "newgame control test: PASS" build/area_load_reference/run.log
	grep -q "area change test: PASS" build/area_load_reference/run.log
	python3 tools/test_area_load_reference.py --log build/area_load_reference/ticks.jsonl

# S12b: the room move's 0018AB00 against the executed original. The live room
# move (the fence door's commit, B8 = 2, state 4's re-place) is compared with
# route 09 by the level smoke's fence_door phase (census L18), which runs this
# script's sequence checks over its tick log.
.PHONY: test-room-move-reference
test-room-move-reference:
	python3 tools/test_room_move_reference.py

# S13: the live first-level smoke. A headless New Game walks the route of
# docs/FIRST_LEVEL_ROUTE.md phase by phase (EM_LEVEL_SMOKE_UNTIL, default
# the whole route); live phases must pass in process and against the
# original captures (tools/test_level_smoke.py), later phases report
# NOT-LIVE with the step they wait on (docs/LEVEL_SMOKE.md).
# Census L07: the live collision world's cells and class-4 list against the
# route captures 00 / 04 (runs the level smoke once with EM_COLL_WORLD_DUMP).
.PHONY: test-collision-world-capture
test-collision-world-capture: $(BIN)
	python3 tools/test_collision_world_capture.py

# The default run ends at battery (first_control, status, battery; about
# 15 s, docs/LEVEL_SMOKE.md "Adding a phase" rule 4). battery is the shortest
# supported end: a run ending at first_control or status fails
# check_render_context's 100-gameplay-tick minimum, and most of the 15 s is
# the New Game path every run plays to first control (1,393 of the run's
# 1,919 logged ticks).
# test-level-smoke-full (or EM_TEST_FULL=1) plays the whole live route
# through the level exit and the AREA01 arrival idle (a01_arrival, route 15
# f741..f801) with --require-through last (about 150 s), then the side
# runs 00 and 09 with side 1 (about 70 s more). Every run's checker requires
# each phase the run was asked to play to be checked live against its
# capture (--require-through: a NOT-LIVE, driven or unreached phase fails
# the target).
# The runs use the switches in the environment (src/em_settings.h): by
# default the Original profile, whose disc answers at host speed.
# test-level-smoke-ps2-drive plays the main route through roger with the
# PS2 disc-drive timing switch on (EM_PS2_DISC_DRIVE_TIMING=1,
# LAUNCHER_OPTIONS.md), where the voiced lines and the opening compare the
# capture's drive fields (about 120 s). The checker reads the mode from the
# run's "stream drive:" line (LEVEL_SMOKE.md "The stream drive's two modes").
LEVEL_SMOKE_UNTIL = $(if $(EM_TEST_FULL),,battery)

.PHONY: test-level-smoke
test-level-smoke: $(BIN)
	mkdir -p build/level_smoke
	rm -f build/level_smoke/status_pages.trace
	EM_UNCAPPED=1 EM_STARTUP_TEST=newgame-level EM_LEVEL_SMOKE_UNTIL=$${EM_LEVEL_SMOKE_UNTIL:-$(LEVEL_SMOKE_UNTIL)} \
	    EM_AREA_CHANGE_LOG=build/level_smoke/ticks.jsonl EM_RAND_TRACE=build/level_smoke/rand.trace \
	    EM_STATUS_PAGES_TRACE=build/level_smoke/status_pages.trace \
	    $(BIN) > build/level_smoke/run.log 2>&1 || (grep "level smoke" build/level_smoke/run.log; false)
	grep "level smoke:" build/level_smoke/run.log
	python3 tools/test_level_smoke.py --log build/level_smoke/ticks.jsonl --run-log build/level_smoke/run.log \
	    --rand-trace build/level_smoke/rand.trace --status-pages-trace build/level_smoke/status_pages.trace \
	    --require-through $${EM_LEVEL_SMOKE_UNTIL:-$(or $(LEVEL_SMOKE_UNTIL),last)}
	$(if $(EM_TEST_FULL),$(MAKE) test-level-smoke-side)

.PHONY: test-level-smoke-full
test-level-smoke-full: $(BIN)
	$(MAKE) test-level-smoke EM_TEST_FULL=1

.PHONY: test-level-smoke-ps2-drive
test-level-smoke-ps2-drive: $(BIN)
	EM_PS2_DISC_DRIVE_TIMING=1 EM_LEVEL_SMOKE_UNTIL=roger $(MAKE) test-level-smoke

# test-cutscene-skip: a cutscene skipped in the headless game, frame by
# frame against the original's skip captures (tools/test_cutscene_skip.py,
# AREA_SCRIPT.md "The skip path"): the opening by default (about 7 s);
# EM_TEST_FULL=1 adds a later opening press and the four skippable route
# scenes, each a level-smoke run through roger (about 2 min in parallel).
.PHONY: test-cutscene-skip
test-cutscene-skip: $(BIN)
	python3 tools/test_cutscene_skip.py

# test-new-game-switch: the developer switch EM_NEW_GAME=1 (STARTUP.md
# "Developer switches") starts the AREA11 opening from the title
# route's state: the whole static state, the module loader and the IOP at
# the opening's first frame against the title's New Game of the same field
# parity, and the switch's refusals (tools/test_new_game_switch.py; about
# 9 s). EM_TEST_FULL=1 adds
# newgame-control on both routes (about 17 s).
.PHONY: test-new-game-switch
test-new-game-switch: $(BIN)
	python3 tools/test_new_game_switch.py

# The side beats, each in its own run: 00 (from slot 04: first control, then
# the panel without the battery; about 14 s), the designed status_pages run
# (first control, the status phase, then every bound status page, MAP
# included, and the takes that open one, each page and MAP node call replayed
# through the original instructions from EM_STATUS_PAGES_TRACE; about 47 s)
# and 09 with the fence door's
# side 1 (the main line through truck_crossing, then the fence door from
# both sides; about 56 s). LEVEL_SMOKE.md. Each side run writes its own
# EM_RAND_TRACE and hands it to the checker, so the whole-run rand checks
# (rand order, sway, marker colour, head sprites) cover its ticks too.
.PHONY: test-level-smoke-side
test-level-smoke-side: $(BIN)
	mkdir -p build/level_smoke_side
	for side in panel_no_battery status_pages fence_door_side1; do \
	    rm -f build/level_smoke_side/status_pages.trace; \
	    EM_UNCAPPED=1 EM_STARTUP_TEST=newgame-level EM_LEVEL_SMOKE_UNTIL=$$side \
	        EM_AREA_CHANGE_LOG=build/level_smoke_side/ticks.jsonl EM_RAND_TRACE=build/level_smoke_side/rand.trace \
	        EM_STATUS_PAGES_TRACE=build/level_smoke_side/status_pages.trace \
	        $(BIN) > build/level_smoke_side/run.log 2>&1 || { grep "level smoke" build/level_smoke_side/run.log; exit 1; }; \
	    grep "level smoke:" build/level_smoke_side/run.log; \
	    python3 tools/test_level_smoke.py --log build/level_smoke_side/ticks.jsonl \
	        --run-log build/level_smoke_side/run.log --rand-trace build/level_smoke_side/rand.trace \
	        --status-pages-trace build/level_smoke_side/status_pages.trace \
	        --require-through $$side || exit 1; \
	done
	$(MAKE) test-level-smoke-aim
	$(MAKE) test-level-smoke-damage
	$(MAKE) test-level-smoke-branch

# The BRANCH side runs (audit 1b item 16; LEVEL_SMOKE.md "The BRANCH side
# runs"): the AREA11 branches the main route skips, each from the main-line
# phase its recording starts from: the optional pickups g0.1..g0.6 with
# their pages, the west-yard and plateau ladders both ways, the boxes r5 / r6
# broken by the light melee (r3's fall), the terminal's ride back up, the
# panel's No and Roger's talk after the encounter; side by side, each
# checked window by window against the decomp's BRANCH recordings (the
# whole player record and the status block: EM_LOG_AIM_RECORDS=1); about
# 9 min under a load average near 30 (2026-10-03); part of
# test-level-smoke-side and -full.
.PHONY: test-level-smoke-branch
test-level-smoke-branch: $(BIN)
	python3 tools/test_level_smoke_branch.py

# The DAMAGE side runs (docs/DAMAGE.md section 8; LEVEL_SMOKE.md "The DAMAGE
# side runs"): dmg_flame (from crevice_prompt: the flame's contacts, the
# low-health heartbeat, the death, the game over, the title after a death
# and the New Game to first control), dmg_crevice_fall (the landing hit)
# and dmg_pit_fall (from truck_preview: the pit floor's death and the game
# over), side by side, each checked window by window against the decomp's
# DAMAGE recordings; part of test-level-smoke-side and -full.
.PHONY: test-level-smoke-damage
test-level-smoke-damage: $(BIN)
	python3 tools/test_level_smoke_damage.py

# The aim/fire side runs (LEVEL_SMOKE.md "aim_r1_hold, aim_r2_hold" and "The
# AIM replays"; AIM_FIRE.md section 10.4): the main line through
# truck_crossing (about 5,550 ticks: the port has no state restore at route
# 08's end), then R1 (R2) held and released at the start of the AIM
# captures aim_00 / aim_01, and the replays aim_fire, aim_melee, aim_light,
# aim_world (aim_04, with the stick), aim_cable (aim_10 then aim_11) and
# aim_burst (aim_05: the status screen's SELECTOR picks the burst; EM_TEST_FULL=1
# adds aim_both, aim_reload, aim_reload_empty), each checked row for row
# against its capture, whole records included (the player record, the gun
# and knife nodes, the camera and the status block: EM_LOG_AIM_RECORDS=1).
# The runs side by side, then their checks: 8 min 57 s for the default
# seven, measured 2026-10-02 with the machine's load average near 50; part
# of test-level-smoke-side and -full.
.PHONY: test-level-smoke-aim
test-level-smoke-aim: $(BIN)
	python3 tools/test_level_smoke_aim.py

.PHONY: test-message-service
test-message-service:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/message_service_test.c src/game/em_message_service.c -o build/message_service_test && ./build/message_service_test

.PHONY: test-message-service-reference
test-message-service-reference:
	python3 tools/test_message_service_reference.py

.PHONY: test-message-area-reference
test-message-area-reference:
	python3 tools/test_message_area_reference.py

.PHONY: test-area01-light-owner-reference test-area01-math-views
test-area01-light-owner-reference:
	python3 tools/test_area01_light_owner_reference.py

test-area01-math-views:
	mkdir -p build/level2
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -Isrc tests/area01_math_views_test.c src/game/em_area01_math_core.c src/game/em_area01_math_owner.c -lm -o build/level2/area01_math_views_test
	./build/level2/area01_math_views_test

.PHONY: test-area01-state-reference test-level2-render-packets-reference test-world-model-bank-reference
test-area01-state-reference:
	python3 tools/test_area01_state_reference.py

test-level2-render-packets-reference:
	python3 tools/test_level2_render_packets_reference.py

test-world-model-bank-reference:
	python3 tools/test_world_model_bank_reference.py

.PHONY: test-fan-original
test-fan-original:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/fan_original_test.c src/game/em_fan_original.c -lm -o build/fan_original_test && ./build/fan_original_test

.PHONY: test-fan-original-reference
test-fan-original-reference:
	python3 tools/test_fan_original_reference.py

.PHONY: test-truck-original
test-truck-original:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc -ffp-contract=off tests/truck_original_test.c src/game/em_truck_original.c -lm -o build/truck_original_test && ./build/truck_original_test
	python3 tools/test_truck_original_reference.py

.PHONY: test-truck-original-capture
test-truck-original-capture:
	python3 tools/test_truck_original_reference.py --compare-capture

.PHONY: test-crate-drum-original
test-crate-drum-original:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc -ffp-contract=off tests/crate_drum_original_test.c src/game/em_crate_original.c src/game/em_owner_services_original.c src/game/em_effect_original.c src/game/em_drum_original.c src/game/em_item_sdk_math.c src/game/em_interaction_scan.c src/game/em_item_trail.c -lm -o build/crate_drum_original_test && ./build/crate_drum_original_test
	python3 tools/test_crate_original_reference.py
	python3 tools/test_drum_original_reference.py

# The first-control record against the original (census L12): the New Game
# control fixture with the run-stop interruption, then 56 callbacks of the
# player record compared with collision_run_poll.json (docs/FIRST_CONTROL.md).
.PHONY: test-first-control-reference
test-first-control-reference: $(BIN)
	mkdir -p build/player_pose_channels
	EM_UNCAPPED=1 EM_STARTUP_TEST=newgame-control EM_CONTROL_REENTRY_TEST=1 EM_CONTROL_TRACE=1 \
	    $(BIN) > build/player_pose_channels/live_reentry.log 2>&1 || \
	    (grep "newgame" build/player_pose_channels/live_reentry.log; false)
	python3 tools/test_player_pose_live_reference.py

.PHONY: test-player-loco-workers-reference
test-player-loco-workers-reference:
	python3 tools/test_player_loco_workers_reference.py

.PHONY: test-player-footstep-reference
test-player-footstep-reference:
	python3 tools/test_player_footstep_reference.py

.PHONY: test-player-floor-reference
test-player-floor-reference:
	python3 tools/test_player_floor_reference.py

.PHONY: test-player-probe-reference
test-player-probe-reference:
	python3 tools/test_player_probe_reference.py

.PHONY: test-shadow-original
test-shadow-original:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -ffp-contract=off -Isrc tests/shadow_original_test.c src/game/em_shadow_original.c -lm -o build/shadow_original_test && ./build/shadow_original_test

.PHONY: test-shadow-original-reference
test-shadow-original-reference:
	python3 tools/test_shadow_original_reference.py

.PHONY: test-player-states-host
test-player-states-host:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/player_states_host_test.c src/game/em_player.c src/game/em_player_floor.c src/game/em_player_motor.c src/game/em_player_heading.c src/game/em_player_slide.c src/game/em_player_climb.c src/game/em_actor_collision.c src/game/em_collision.c src/game/em_actor_pool.c src/game/em_player_reaction.c src/game/em_player_fall.c src/game/em_player_record_helpers.c src/game/em_script_host_workers.c src/game/em_script.c src/game/em_effect_original.c src/game/em_coll_probe_original.c src/game/em_owner_services_original.c src/game/em_player_stage_workers.c src/game/em_sdk_math_original.c -lm -o build/player_states_host_test && ./build/player_states_host_test

.PHONY: test-ee-float-model
test-ee-float-model:
	python3 tools/test_ee_float_model.py

.PHONY: test-player-stage-workers-reference
test-player-stage-workers-reference:
	python3 tools/test_player_stage_workers_reference.py

.PHONY: test-coll-probe-reference
test-coll-probe-reference:
	python3 tools/test_coll_probe_reference.py

.PHONY: test-coll-move-reference
test-coll-move-reference:
	python3 tools/test_coll_move_reference.py

.PHONY: test-player-fall-reference
test-player-fall-reference:
	python3 tools/test_player_fall_reference.py

.PHONY: test-player-major2-reference
test-player-major2-reference:
	python3 tools/test_player_major2_reference.py

.PHONY: test-player-reaction-reference
test-player-reaction-reference:
	python3 tools/test_player_reaction_reference.py

.PHONY: test-player-hang-reference
test-player-hang-reference:
	python3 tools/test_player_hang_reference.py

.PHONY: test-player-recovery-reference
test-player-recovery-reference:
	python3 tools/test_player_recovery_reference.py

.PHONY: test-player-recovery-route
test-player-recovery-route:
	EM_TEST_ROUTE=1 python3 tools/test_player_recovery_reference.py

.PHONY: test-owner-services
test-owner-services:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -ffp-contract=off -Isrc tests/owner_services_test.c src/game/em_owner_services_original.c -o build/owner_services_test && ./build/owner_services_test

.PHONY: test-owner-services-reference
test-owner-services-reference:
	python3 tools/test_owner_services_reference.py

.PHONY: test-effect-original
test-effect-original:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -ffp-contract=off -Isrc tests/effect_original_test.c src/game/em_effect_original.c -lm -o build/effect_original_test && ./build/effect_original_test

.PHONY: test-effect-original-reference
test-effect-original-reference:
	python3 tools/test_effect_original_reference.py

.PHONY: test-effect-001F77B0-reference
test-effect-001F77B0-reference:
	python3 tools/test_effect_001F77B0_reference.py

.PHONY: test-head-sprite-original
test-head-sprite-original:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -ffp-contract=off -Isrc tests/head_sprite_original_test.c src/game/em_head_sprite_original.c -lm -o build/head_sprite_original_test && ./build/head_sprite_original_test

.PHONY: test-head-sprite-reference
test-head-sprite-reference:
	python3 tools/test_head_sprite_reference.py

.PHONY: test-message-draw
test-message-draw:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/message_draw_test.c src/game/em_message_draw_original.c -o build/message_draw_test && ./build/message_draw_test

.PHONY: test-message-draw-reference
test-message-draw-reference:
	python3 tools/test_message_draw_reference.py

.PHONY: test-message-glyph
test-message-glyph:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/message_glyph_test.c src/game/em_message_glyph_original.c -o build/message_glyph_test && ./build/message_glyph_test

.PHONY: test-message-glyph-reference
test-message-glyph-reference:
	python3 tools/test_message_glyph_reference.py

.PHONY: test-message-capture
test-message-capture: $(BIN)
	python3 tools/test_message_capture.py

.PHONY: test-stream-lanes
test-stream-lanes:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -ffp-contract=off -Isrc tests/stream_lanes_test.c src/game/em_stream_lanes_original.c -o build/stream_lanes_test && ./build/stream_lanes_test
	python3 tools/test_stream_lanes_reference.py

.PHONY: test-player-weapon-states-a-reference
test-player-weapon-states-a-reference:
	python3 tools/test_player_weapon_states_a_reference.py

.PHONY: test-player-weapon-states-b-reference
test-player-weapon-states-b-reference:
	python3 tools/test_player_weapon_states_b_reference.py

.PHONY: test-player-ladder-entry-reference
test-player-ladder-entry-reference:
	python3 tools/test_player_ladder_entry_reference.py

.PHONY: test-player-ladder-climb-reference
test-player-ladder-climb-reference:
	python3 tools/test_player_ladder_climb_reference.py

.PHONY: test-player-running-jump-reference
test-player-running-jump-reference:
	python3 tools/test_player_running_jump_reference.py

.PHONY: test-pose-host-workers-reference
test-pose-host-workers-reference:
	python3 tools/test_pose_host_workers_reference.py

.PHONY: test-player-misc-workers-reference
test-player-misc-workers-reference:
	python3 tools/test_player_misc_workers_reference.py

.PHONY: test-load-veil-particles-reference
test-load-veil-particles-reference:
	python3 tools/test_load_veil_particles_reference.py

.PHONY: test-gs-blocks-reference
test-gs-blocks-reference:
	python3 tools/test_gs_blocks_reference.py

.PHONY: test-load-veil-gpu
test-load-veil-gpu:
	python3 tools/test_load_veil_gpu.py

.PHONY: test-load-veil-particles
test-load-veil-particles:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -ffp-contract=off -Isrc tests/load_veil_particles_test.c src/game/em_load_veil_particles.c -o build/load_veil_particles_test && ./build/load_veil_particles_test

.PHONY: test-camera-area11-specials-reference
test-camera-area11-specials-reference:
	python3 tools/test_camera_area11_specials_reference.py

.PHONY: test-player-closure-10-12-19-reference
test-player-closure-10-12-19-reference:
	python3 tools/test_player_closure_10_12_19_reference.py

.PHONY: test-roger-actor-original
test-roger-actor-original:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -ffp-contract=off -Isrc tests/roger_actor_original_test.c src/game/em_roger_actor_original.c -lm -o build/roger_actor_original_test && ./build/roger_actor_original_test

.PHONY: test-roger-actor-original-reference
test-roger-actor-original-reference:
	python3 tools/test_roger_actor_original_reference.py

.PHONY: test-player-closure-0e-18-reference
test-player-closure-0e-18-reference:
	python3 tools/test_player_closure_0e_18_reference.py

.PHONY: test-coll-segment-walkers-reference
test-coll-segment-walkers-reference:
	python3 tools/test_coll_segment_walkers_reference.py

.PHONY: test-script-host-workers-reference
test-script-host-workers-reference:
	python3 tools/test_script_host_workers_reference.py

.PHONY: test-script-host-workers
test-script-host-workers:
	@mkdir -p build/script_host_workers
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -Isrc tests/script_host_workers_test.c src/game/em_script_host_workers.c src/game/em_player_stage_workers.c src/game/em_sdk_math_original.c src/game/em_script.c -o build/script_host_workers/script_host_workers_test
	build/script_host_workers/script_host_workers_test build/script_host_workers

.PHONY: test-sdk-soft-float-reference
test-sdk-soft-float-reference:
	python3 tools/test_sdk_soft_float_reference.py

.PHONY: test-sdk-soft-float
test-sdk-soft-float:
	@mkdir -p build/sdk_soft_float
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=undefined -Isrc tests/sdk_soft_float_test.c src/game/em_sdk_soft_float.c src/game/em_sdk_math_original.c -lm -o build/sdk_soft_float/sdk_soft_float_test
	build/sdk_soft_float/sdk_soft_float_test ../Extermination/config/SCUS_971.12

.PHONY: test-shadow-actor-route-reference
test-shadow-actor-route-reference:
	python3 tools/test_shadow_actor_route_reference.py

.PHONY: test-camera-follow-original-reference
test-camera-follow-original-reference:
	python3 tools/test_camera_follow_original_reference.py

.PHONY: test-camera-follow-original
test-camera-follow-original:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -Isrc tests/camera_follow_original_test.c src/game/em_camera_follow_original.c src/game/em_sdk_math_original.c -lm -o build/camera_follow_original_test && ./build/camera_follow_original_test

.PHONY: test-effect-manager
test-effect-manager:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -ffp-contract=off -Isrc tests/effect_manager_test.c src/game/em_effect_manager.c src/game/em_effect_original.c src/game/em_owner_services_original.c -lm -o build/effect_manager_test && ./build/effect_manager_test

.PHONY: test-effect-manager-reference
test-effect-manager-reference:
	python3 tools/test_effect_manager_reference.py

.PHONY: test-effect-kinds
test-effect-kinds:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -ffp-contract=off -Isrc tests/effect_kinds_test.c src/game/em_effect_kinds.c -lm -o build/effect_kinds_test && ./build/effect_kinds_test

.PHONY: test-effect-kinds-reference
test-effect-kinds-reference:
	python3 tools/test_effect_kinds_reference.py

.PHONY: test-player-equipment-reference
test-player-equipment-reference:
	python3 tools/test_player_equipment_reference.py

.PHONY: test-frame-render-heads-reference
test-frame-render-heads-reference:
	python3 tools/test_frame_render_heads_reference.py

.PHONY: test-render-context-reference
test-render-context-reference:
	python3 tools/test_render_context_reference.py

.PHONY: test-render-context
test-render-context:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -ffp-contract=off -Isrc tests/render_context_test.c src/game/em_render_context.c -o build/render_context_test && ./build/render_context_test

# The static world and background channel (docs/STATIC_WORLD.md): 001C1D00's
# tree against the original instructions over the captures (the channel-3
# list and the channel-0 run rebuilt byte for byte), and the run drawn from
# its packets: the level kernel 00237180 and the clip kernel 00239C90 against
# the original VU1 microcode, kick for kick and triangle for triangle.
.PHONY: test-static-world-reference
test-static-world-reference:
	python3 tools/test_static_world_reference.py

.PHONY: test-static-world-draw-reference
test-static-world-draw-reference:
	python3 tools/test_static_world_draw_reference.py

# The VU lane rules on the host FPU (em_vu_host_lanes.h: round toward zero,
# flush-to-zero) against em_ee_float.h's integer model, bit for bit.
.PHONY: test-vu-host-lanes
test-vu-host-lanes:
	python3 tools/test_vu_host_lanes.py

# The Metal pixel path of em_gfx_gs_opaque over a captured static run
# against the GS pixel model (the original microcode's triangles).
.PHONY: test-static-world-gpu
test-static-world-gpu:
	python3 tools/test_static_world_gpu.py

.PHONY: test-render-context-live-reference
test-render-context-live-reference:
	python3 tools/test_render_context_live_reference.py

.PHONY: test-anim-runtime-rest-reference
test-anim-runtime-rest-reference:
	python3 tools/test_anim_runtime_rest_reference.py

.PHONY: test-status-ui-leftovers-reference
test-status-ui-leftovers-reference:
	python3 tools/test_status_ui_leftovers_reference.py

.PHONY: test-startup-load-gaps-reference
test-startup-load-gaps-reference:
	python3 tools/test_startup_load_gaps_reference.py

.PHONY: test-locomotion-display-reference
test-locomotion-display-reference:
	python3 tools/test_locomotion_display_reference.py

# The SDK VU0 leaves 001026D0 / 00102900 / 00102948 (em_sdk_vu0.h, their one
# translation) against the original instructions (docs/SDK_VU0.md).
.PHONY: test-sdk-vu0-reference
test-sdk-vu0-reference:
	python3 tools/test_sdk_vu0_reference.py

.PHONY: test-camera-leftovers-reference
test-camera-leftovers-reference:
	python3 tools/test_camera_leftovers_reference.py

.PHONY: test-camera-leftovers
test-camera-leftovers:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -Isrc tests/camera_leftovers_test.c src/game/em_camera_leftovers.c src/game/em_camera_leftovers_solver.c src/game/em_camera_follow_original.c src/game/em_sdk_math_original.c -lm -o build/camera_leftovers_test && ./build/camera_leftovers_test

.PHONY: test-script-door-fan
test-script-door-fan:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -ffp-contract=off -Isrc tests/script_door_fan_test.c src/game/em_script_door_fan.c src/game/em_security_gun.c -lm -o build/script_door_fan_test && ./build/script_door_fan_test

.PHONY: test-script-door-fan-reference
test-script-door-fan-reference:
	python3 tools/test_script_door_fan_reference.py

.PHONY: test-security-gun-rest-reference
test-security-gun-rest-reference:
	python3 tools/test_security_gun_rest_reference.py

.PHONY: test-render-verify-rest-reference
test-render-verify-rest-reference:
	python3 tools/test_render_verify_rest_reference.py

.PHONY: test-main-loop-and-gap-reference
test-main-loop-and-gap-reference:
	python3 tools/test_main_loop_and_gap_reference.py

.PHONY: test-player-use-dispatch-reference
test-player-use-dispatch-reference:
	python3 tools/test_player_use_dispatch_reference.py

.PHONY: test-coll-list-passes-reference
test-coll-list-passes-reference:
	python3 tools/test_coll_list_passes_reference.py

.PHONY: test-iop-stream
test-iop-stream:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -ffp-contract=off -Isrc tests/iop_stream_test.c src/game/em_iop_stream.c src/game/em_stream_lanes_original.c src/game/em_sfx_bank.c -o build/iop_stream_test && ./build/iop_stream_test
	python3 tools/test_iop_stream_reference.py

.PHONY: test-owner-draw
test-owner-draw:
	mkdir -p build/owner_draw && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -ffp-contract=off -Isrc tests/owner_draw_test.c src/game/em_owner_draw_original.c -o build/owner_draw/owner_draw_test && ./build/owner_draw/owner_draw_test

.PHONY: test-owner-draw-reference
test-owner-draw-reference:
	python3 tools/test_owner_draw_reference.py

# The object-unit path (docs/OWNER_DRAW.md P1/P2): the parser / runner
# fixture, and the captured owner units against the original VU1 microcode.
.PHONY: test-object-unit
test-object-unit:
	mkdir -p build/object_unit && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -ffp-contract=off -Isrc tests/object_unit_test.c src/game/em_object_unit.c -o build/object_unit/object_unit_test && ./build/object_unit/object_unit_test

.PHONY: test-object-unit-reference
test-object-unit-reference:
	python3 tools/test_object_unit_reference.py

# The Metal pixel path of em_gfx_object_unit over captured units, headless.
.PHONY: test-object-unit-gpu
test-object-unit-gpu:
	python3 tools/test_object_unit_gpu.py

.PHONY: export-object-textures
export-object-textures:
	python3 tools/export_object_textures.py

.PHONY: test-player-heading-record-reference
test-player-heading-record-reference:
	python3 tools/test_player_heading_record_reference.py

.PHONY: test-pose-chain-reference
test-pose-chain-reference:
	python3 tools/test_pose_chain_reference.py

# The player's one pose owner (docs/PLAYER_CLIPS.md section 6): the live
# module against the original over the captured player records.
.PHONY: test-player-record-pose-reference
test-player-record-pose-reference:
	python3 tools/test_player_record_pose_reference.py

.PHONY: test-actor-light-001d89d0-reference
test-actor-light-001d89d0-reference:
	python3 tools/test_actor_light_001d89d0_reference.py

.PHONY: test-vu1-object-clip-reference
test-vu1-object-clip-reference:
	python3 tools/test_vu1_object_clip_reference.py

.PHONY: test-vu1-object-kernel-reference
test-vu1-object-kernel-reference:
	python3 tools/test_vu1_object_kernel_reference.py

.PHONY: test-vu1-object-kernel-defects
test-vu1-object-kernel-defects:
	python3 tools/test_vu1_object_kernel_reference.py --defects

.PHONY: test-packet-chain-reference
test-packet-chain-reference:
	python3 tools/test_packet_chain_reference.py

.PHONY: test-census-unverified-reference
test-census-unverified-reference:
	python3 tools/test_census_unverified_reference.py

.PHONY: test-census-standins-reference
test-census-standins-reference:
	python3 tools/test_census_standins_reference.py

.PHONY: test-coll-grid-hull-reference
test-coll-grid-hull-reference:
	python3 tools/test_coll_grid_hull_reference.py

.PHONY: test-vu1-face-morph-reference
test-vu1-face-morph-reference:
	python3 tools/test_vu1_face_morph_reference.py

.PHONY: test-vu1-face-morph-defects
test-vu1-face-morph-defects:
	python3 tools/test_vu1_face_morph_reference.py --defects

.PHONY: test-shadow-decal-reference
test-shadow-decal-reference:
	python3 tools/test_shadow_decal_reference.py

.PHONY: test-chain-page-reference test-chain-page-gpu
test-chain-page-reference:
	python3 tools/test_chain_page_reference.py

test-chain-page-gpu:
	python3 tools/test_chain_page_gpu.py

# The GS fog rule (em_fog_gs_blend / em_fog_gs_blend7 and their shader
# copy) against the GS conformance captures' fogged pixels (decomp
# build/b16): constant F on the CPU and in Metal, the 8.7 weight of a
# Gouraud F on the CPU.
.PHONY: test-gs-fog-conformance
test-gs-fog-conformance:
	python3 tools/test_gs_fog_conformance.py

# The fb2 pixel harness (docs/GS_EXACT.md section 10): the port's
# Original-profile frame at the fb2 route points against the decomp's
# software-renderer fields, per pixel. Default: first control (about 17 s);
# EM_TEST_FULL=1: every point the level smoke aligns (about 4.5 min).
.PHONY: test-fb2-pixels
test-fb2-pixels: $(BIN)
	python3 tools/test_fb2_pixels.py

.PHONY: test-chain-page
test-chain-page:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc \
	    tests/chain_page_test.c src/game/em_chain_page.c -o build/chain_page_test && ./build/chain_page_test

.PHONY: test-status-page-record-reference
test-status-page-record-reference:
	python3 tools/test_status_page_record_reference.py

.PHONY: test-message-presenter-rest-reference
test-message-presenter-rest-reference:
	python3 tools/test_message_presenter_rest_reference.py

.PHONY: test-ee-float-header
test-ee-float-header:
	python3 tools/test_ee_float_header.py

.PHONY: test-level-material-reference
test-level-material-reference:
	python3 tools/test_level_material_reference.py

.PHONY: test-area-script
test-area-script:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -ffp-contract=off -Isrc tests/area_script_test.c src/game/em_area_script.c src/game/em_script.c src/game/em_message_service.c src/game/em_interaction_frame.c src/game/em_interaction_cinematic.c src/game/em_cinematic_playback.c src/game/em_cinematic_camera.c src/game/em_camera_rotation.c src/game/em_owner_services_original.c src/game/em_effect_original.c src/game/em_item_sdk_math.c src/game/em_interaction_scan.c src/game/em_item_trail.c src/game/em_fan_original.c src/game/em_sdk_math_original.c -lm -o build/area_script_test && ./build/area_script_test assets/scene_snow/roger/programs.emsc assets/scene_snow/elevator.emsc assets/scene_snow/panel/scripts.emsc ../Extermination/extract/OVERLAY/AREA11.BIN

.PHONY: test-area-script-reference
test-area-script-reference:
	python3 tools/test_area_script_reference.py

.PHONY: test-player-climb-reference
test-player-climb-reference:
	python3 tools/test_player_climb_reference.py

.PHONY: test-player-slide-reference
test-player-slide-reference:
	python3 tools/test_player_slide_reference.py

.PHONY: test-player-record-helpers-reference
test-player-record-helpers-reference:
	python3 tools/test_player_record_helpers_reference.py

.PHONY: test-background-reference
test-background-reference:
	python3 tools/test_background_reference.py

.PHONY: test-director-original
test-director-original:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc -ffp-contract=off tests/director_original_test.c src/game/em_director_original.c -lm -o build/director_original_test && ./build/director_original_test ../Extermination/extract/OVERLAY/AREA11.BIN ../Extermination/config/SCUS_971.12
	python3 tools/test_director_original_reference.py

.PHONY: test-actor-collision
test-actor-collision:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc -ffp-contract=off tests/actor_collision_test.c src/game/em_actor_collision.c src/game/em_collision.c src/game/em_actor_pool.c src/game/em_coll_probe_original.c src/game/em_effect_original.c -lm -o build/actor_collision_test && ./build/actor_collision_test

.PHONY: test-actor-collision-reference
test-actor-collision-reference:
	python3 tools/test_actor_collision_reference.py

.PHONY: test-roger-media-reference
test-roger-media-reference:
	python3 tools/test_roger_media_reference.py

.PHONY: test-roger-cinematic-reference
test-roger-cinematic-reference:
	python3 tools/test_roger_cinematic_reference.py

.PHONY: test-interaction-cinematic-reference
test-interaction-cinematic-reference:
	python3 tools/test_interaction_cinematic_reference.py

.PHONY: test-player-cinematic-reference
test-player-cinematic-reference:
	python3 tools/test_player_cinematic_reference.py

.PHONY: test-roger-encounter-capture test-face-allocation-reference
test-roger-encounter-capture:
	python3 tools/test_roger_encounter_capture.py

test-face-allocation-reference:
	python3 tools/test_face_allocation_reference.py

test-pickup-original: tests/pickup_original_test.c tests/pickup_light_test.c src/game/em_pickup.c src/game/em_effect_kinds.c $(PICKUP_ORIGINAL_TEST_SRC)
	@mkdir -p build/pickup_owner_reference
	$(CC) -std=c11 -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -ffp-contract=off -Isrc tests/pickup_original_test.c src/game/em_effect_kinds.c $(PICKUP_ORIGINAL_TEST_SRC) -o build/pickup_original_test
	build/pickup_original_test

.PHONY: test-panel-reference test-panel-interaction
test-panel-reference:
	python3 tools/test_panel_reference.py

.PHONY: test-panel-program
.PHONY: test-camera-retarget-reference
test-camera-retarget-reference:
	python3 tools/test_camera_retarget_reference.py

.PHONY: test-camera-interaction-fixture test-interaction-frame-reference test-interaction-animation-reference test-collision-faces-reference
test-camera-interaction-fixture:
	python3 tools/test_camera_interaction_fixture.py

.PHONY: test-camera-rotation-reference
.PHONY: test-camera-live-reference test-interaction-recovery-reference
# Census L13..L16: the camera commit 0018C0D0, 00102798 and 00193660 against
# the executed original with every callee (docs/CAMERA_LIVE.md section 4).
test-camera-live-reference:
	python3 tools/test_camera_live_reference.py

test-interaction-recovery-reference:
	python3 tools/test_interaction_recovery_reference.py

test-camera-rotation-reference:
	python3 tools/test_camera_rotation_reference.py

.PHONY: test-interaction-geometry-reference
test-interaction-geometry-reference:
	python3 tools/test_interaction_alignment_reference.py
	python3 tools/test_interaction_projection_reference.py

test-interaction-frame-reference:
	python3 tools/test_interaction_frame_reference.py

test-interaction-animation-reference:
	python3 tools/test_interaction_animation_reference.py

.PHONY: test-player-pose test-pose-reference
test-player-pose: tests/player_pose_test.c src/game/em_player_pose.c src/game/em_pose_bank.c \
                 src/game/em_pose_transition.c src/game/em_interaction_animation.c \
                 src/game/em_player_pose.h src/game/em_pose_bank.h src/game/em_pose_transition.h src/game/em_pose_math.h
	@mkdir -p build/player_pose_channels
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -Isrc \
	    tests/player_pose_test.c src/game/em_player_pose.c src/game/em_pose_bank.c \
	    src/game/em_pose_transition.c src/game/em_interaction_animation.c -lm \
	    -o build/player_pose_channels/player_pose_test
	./build/player_pose_channels/player_pose_test

test-pose-reference:
	python3 tools/test_pose_transition_reference.py
	python3 tools/test_pose_bank_reference.py
	python3 tools/test_player_pose_reference.py

# The player's one pose owner (em_player_record_pose over em_pose_host_workers)
# and the translations it reaches (docs/PLAYER_CLIPS.md section 6).
PLAYER_RECORD_POSE_SRC := src/game/em_player_record_pose.c src/game/em_pose_host_workers.c \
    src/game/em_player_stage_workers.c src/game/em_player_floor.c src/game/em_player_reaction.c \
    src/game/em_player_fall.c src/game/em_owner_services_original.c src/game/em_stream_lanes_original.c

.PHONY: test-player-pose-host
test-player-pose-host:
	@mkdir -p build/player_pose_channels
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -Isrc \
	    tests/player_pose_host_test.c src/game/em_player_pose_host.c src/game/em_player_pose.c \
	    src/game/em_locomotion_display.c src/game/em_anim_runtime_rest.c src/game/em_sdk_math_original.c \
	    src/game/em_player_ladder_climb.c src/game/em_player_major2.c \
	    src/game/em_pose_bank.c src/game/em_pose_transition.c src/game/em_fade.c \
	    src/game/em_player_foot_stop.c src/game/em_camera_rotation.c src/game/em_effect_original.c \
	    $(PLAYER_RECORD_POSE_SRC) -lm \
	    -o build/player_pose_channels/player_pose_host_test
	./build/player_pose_channels/player_pose_host_test

.PHONY: test-player-foot-stop-reference
test-player-foot-stop-reference:
	python3 tools/test_player_foot_stop_reference.py

.PHONY: test-interaction-runtime
test-interaction-runtime:
	@mkdir -p build
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -Isrc tests/interaction_runtime_test.c src/game/em_interaction_runtime.c src/game/em_interaction_frame.c src/game/em_interaction_animation.c src/game/em_script.c src/em_model.c -lm -o build/interaction_runtime_test
	build/interaction_runtime_test

.PHONY: test-interaction-scan-reference test-interaction-scan test-interaction-scene
test-interaction-scan-reference:
	python3 tools/test_interaction_scan_reference.py
	python3 tools/test_interaction_pickup_reference.py

test-interaction-scan: tests/interaction_scan_test.c src/game/em_interaction_scan.c src/game/em_interaction_scan.h
	@mkdir -p build/interaction_scan_reference
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -Isrc tests/interaction_scan_test.c src/game/em_interaction_scan.c -lm -o build/interaction_scan_reference/interaction_scan_test
	build/interaction_scan_reference/interaction_scan_test

.PHONY: test-door-candidate-reference
test-door-candidate-reference:
	python3 tools/test_door_candidate_reference.py

test-interaction-scene: tests/interaction_scene_test.c src/game/em_interaction_scene.c src/game/em_interaction_scan.c
	@mkdir -p build/interaction_scan_reference
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -Isrc tests/interaction_scene_test.c src/game/em_interaction_scene.c src/game/em_interaction_scan.c -lm -o build/interaction_scan_reference/interaction_scene_test
	build/interaction_scan_reference/interaction_scene_test assets/scene_snow/interaction.emis

.PHONY: test-status-frame-reference
test-status-frame-reference:
	python3 tools/test_status_frame_reference.py

.PHONY: test-status-page-reference test-item-root-reference test-item-ui-reference
test-status-page-reference:
	python3 tools/test_status_page_reference.py

# The status pages (docs/STATUS_PAGES.md) and the one 0020D930 owner
# (em_menu_hover) against the original instructions; the MAP page's model
# bank D_0028A570 (tools/export_status_map.py, the EMSP relocation) against
# the original 001C6120 / 001C6150.
.PHONY: test-status-pages-reference test-menu-hover-reference test-status-map-reference
test-status-pages-reference:
	python3 tools/test_status_pages_reference.py

test-status-map-reference:
	python3 tools/test_status_map_reference.py

test-menu-hover-reference:
	python3 tools/test_menu_hover_source_reference.py

# The status pages' GS memory and TEX0 decode (em_gs_texture) against the
# disc model and the decomp's decoder, and HEALING's player worker
# 0015C700 / 0015C7C0 against the original instructions.
.PHONY: test-gs-texture-reference test-player-heal-reference
test-gs-texture-reference:
	python3 tools/test_gs_texture_reference.py

test-player-heal-reference:
	python3 tools/test_player_heal_reference.py

test-item-root-reference:
	python3 tools/test_item_root_reference.py

test-item-ui-reference:
	python3 tools/test_item_ui_reference.py

.PHONY: test-item-trail-reference test-item-sdk-math-reference test-status-runtime
test-item-trail-reference:
	python3 tools/test_item_trail_reference.py

test-item-sdk-math-reference:
	python3 tools/test_item_sdk_math_reference.py

.PHONY: test-item-geometry-reference test-status-hub-reference
test-item-geometry-reference:
	python3 tools/test_item_geometry_reference.py

test-status-hub-reference:
	python3 tools/test_status_hub_reference.py

.PHONY: test-status-hub-ui-reference
test-status-hub-ui-reference:
	python3 tools/test_status_hub_ui_reference.py

.PHONY: test-status-draw-reference
test-status-draw-reference:
	python3 tools/test_status_draw_reference.py

.PHONY: test-status-background-reference
test-status-background-reference:
	python3 tools/test_status_background_reference.py

# The boot ELF's SDK float math (docs/SDK_MATH_ORIGINAL.md); its sinf is
# 0020A7A0's live sine (WP-5).
.PHONY: test-sdk-math-original-reference test-sdk-math-original
test-sdk-math-original-reference:
	python3 tools/test_sdk_math_original_reference.py

test-sdk-math-original:
	@mkdir -p build/sdk_math_original
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -Isrc tests/sdk_math_original_test.c src/game/em_sdk_math_original.c -lm -o build/sdk_math_original/sdk_math_original_test
	build/sdk_math_original/sdk_math_original_test ../Extermination/config/SCUS_971.12

# The status-screen owners (docs/STATUS_SCENE.md): the static pool
# D_0028B020, the hub's menu player and equipment letter models, and the
# module loader; and their live binding em_status_models over the
# status-hub capture (WP-5).
.PHONY: test-status-scene-reference test-status-scene-original test-status-models
test-status-scene-reference:
	python3 tools/test_status_scene_reference.py

# Every first-level asset from the user's disc (docs/DISC_TEXTURES.md): the
# textures rebuilt from the disc's GS uploads against the original loaders
# and the captures, and the other exporters run without a capture against
# the capture-derived files (the resource table D_0028A490, the weather bits,
# the door, Roger, the flame, the snow, the panel pages, the props, and
# interaction.emis / background.embg from the first world frame the original
# code builds from the disc (tools/export_disc_state.py); full mode also the
# hub, the Roger banks, the models, the sprite sheets, the light cone and
# the whole player.emdl). Both need the disc image.
.PHONY: test-disc-textures-reference test-disc-assets-reference
test-disc-textures-reference:
	python3 tools/test_disc_textures_reference.py

test-disc-assets-reference:
	python3 tools/test_disc_assets_reference.py

# The screen-module loader's disc/DMA layer and its live slot-2 binding
# (docs/MODULE_LOADER.md): the leaf routines and whole loads against the
# original instructions, the captured load rows, the ASan/UBSan driver.
.PHONY: test-module-loader-reference
test-module-loader-reference:
	python3 tools/test_module_loader_reference.py

# The sound-bank upload (docs/IOP_STREAM.md "The sound-bank transfer"): the
# EE sound library and 001FB370's whole chain against the original
# instructions over the title capture, and its end state against route 00.
.PHONY: test-sound-bank-reference
test-sound-bank-reference:
	python3 tools/test_sound_bank_reference.py

test-status-scene-original:
	@mkdir -p build/status_scene_reference
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -Isrc tests/status_scene_original_test.c src/game/em_status_scene_original.c -lm -o build/status_scene_reference/status_scene_original_test
	build/status_scene_reference/status_scene_original_test

test-status-models:
	@mkdir -p build/status_models
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -Isrc tests/status_models_test.c src/game/em_status_models.c src/game/em_status_scene_original.c src/game/em_owner_services_original.c src/game/em_actor_light_001D89D0.c src/game/em_frame_render_heads.c src/game/em_player_pose.c src/game/em_pose_bank.c src/game/em_pose_transition.c src/em_model.c src/game/em_random.c \
	    src/game/em_area01_math_core.c src/game/em_area01_math_actor.c src/game/em_pose_host_workers.c src/game/em_player_stage_workers.c src/game/em_player_floor.c src/game/em_player_reaction.c src/game/em_player_fall.c src/game/em_stream_lanes_original.c -lm -o build/status_models/status_models_test
	build/status_models/status_models_test assets/status_models ../Extermination/build/startup-reference/status-hub/eeMemory.bin

.PHONY: test-item-device-reference
test-item-device-reference:
	python3 tools/test_item_device_reference.py

test-status-runtime:
	@mkdir -p build/status_page_reference
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -Wl,-dead_strip -Isrc tests/status_runtime_test.c src/game/em_status_hub.c src/game/em_status_runtime.c src/game/em_status_frame.c src/game/em_status_page.c src/game/em_item_root.c src/game/em_menu_hover.c src/game/em_item_ui.c src/game/em_item_trail.c src/game/em_battery_ui.c src/game/em_panel.c src/game/em_status_hub_ui.c src/game/em_status_draw.c src/game/em_item_geometry.c src/game/em_item_sdk_math.c \
	    src/game/em_battery_page_live.c src/game/em_status_page_record.c src/game/em_status_ui_leftovers.c src/game/em_census_standins.c src/game/em_render_verify_rest.c src/game/em_player_stage_workers.c \
	    src/game/em_gs_texture.c src/game/em_page_draw.c src/game/em_status_pages_live.c src/game/em_status_pages_helpers.c \
	    src/game/em_status_pages_item.c src/game/em_status_pages_spr4.c src/game/em_status_pages_parts.c \
	    src/game/em_area01_ui_pages.c src/game/em_pickup_items_original.c \
	    src/game/em_status_scene_original.c src/game/em_module_loader.c src/game/em_task.c -lm -o build/status_page_reference/status_runtime_test
	build/status_page_reference/status_runtime_test assets/scene_snow/panel/battery.emba assets/scene_snow/panel/item_root.emir \
	    assets/scene_snow/panel/status_hub.emhs assets/scene_snow/panel/status_hub_atlas.emha \
	    assets/module_loader/modules.emml

.PHONY: test-panel-message-reference
test-panel-message-reference:
	python3 tools/test_panel_message_reference.py

.PHONY: test-overlay-blend
test-overlay-blend:
	python3 tools/test_overlay_blend.py

test-collision-faces-reference:
	python3 tools/test_collision_faces_reference.py

test-panel-program:
	@mkdir -p build
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -Isrc tests/panel_program_test.c src/game/em_panel_program.c src/game/em_script.c src/game/em_panel.c -lm -o build/panel_program_test
	build/panel_program_test assets/scene_snow/panel/scripts.emsc

.PHONY: test-camera-aim-reference
# The aim camera (camera actions 1 / 2 / 5 and what they own, em_camera_aim.c)
# against the original instructions over captured AREA11 RAM
# (docs/CAMERA_LIVE.md section 7). EM_TEST_FULL=1 runs every case.
test-camera-aim-reference:
	python3 tools/test_camera_aim_reference.py

.PHONY: test-aim-fire-marker-reference
# The round's impact marker 0018ABA0 (em_aim_fire_marker.c, bound in
# ordinary play; AIM_FIRE.md section 3) against the original instructions
# over captured AREA11 RAM.
test-aim-fire-marker-reference:
	python3 tools/test_aim_fire_marker_reference.py

.PHONY: test-area22-port-reference
# em_area22_port.c against the original instructions over recorded AREA22
# RAM. A later-level module, in the default set because the first level's
# aim camera binds its 001028E8 / 00183010 / 0018C850 / 0018C920 (the one
# owners; CAMERA_LIVE.md section 7). About 5 s.
test-area22-port-reference:
	python3 tools/test_area22_port_reference.py

.PHONY: test-area00-low-reference
# em_area00_low.c against the original instructions over recorded AREA00
# RAM. A later-level module, in the default set because the first level's
# aim beam binds its 00102870 (the one owner; AIM_FIRE.md section 7).
# About 4 s.
test-area00-low-reference:
	python3 tools/test_area00_low_reference.py

.PHONY: test-area11-interaction-host
test-area11-interaction-host:
	python3 tools/test_area11_interaction_host.py

.PHONY: test-panel-runtime
test-panel-runtime:
	@mkdir -p build
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -Wl,-dead_strip -Isrc \
	    tests/panel_runtime_test.c src/game/em_panel_runtime.c src/game/em_panel_program.c src/game/em_panel.c \
	    src/game/em_interaction_runtime.c src/game/em_interaction_animation.c src/game/em_interaction_frame.c \
	    src/game/em_player_pose.c src/game/em_pose_bank.c src/game/em_pose_transition.c \
	    src/game/em_message_live.c src/game/em_message_service.c src/game/em_message_draw_original.c \
	    src/game/em_message_glyph_original.c src/game/em_script.c src/em_model.c \
	    src/game/em_status_page_record.c -lm -o build/panel_runtime_test
	build/panel_runtime_test

.PHONY: test-elevator-reference
.PHONY: test-elevator-runtime
test-elevator-runtime:
	mkdir -p build
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -Isrc tests/elevator_runtime_test.c src/game/em_elevator_runtime.c src/game/em_elevator_program.c src/game/em_elevator.c src/game/em_interaction_runtime.c src/game/em_interaction_animation.c src/game/em_interaction_frame.c src/game/em_script.c src/em_model.c -lm -o build/elevator_runtime_test
	build/elevator_runtime_test assets/scene_snow/elevator.emsc assets/player.emdl

test-elevator-reference:
	python3 tools/test_elevator_reference.py
	python3 tools/test_elevator_commands_reference.py

.PHONY: test-elevator-program
test-elevator-program:
	@mkdir -p build
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -Isrc tests/elevator_program_test.c src/game/em_elevator_program.c src/game/em_elevator.c src/game/em_script.c -lm -o build/elevator_program_test
	build/elevator_program_test assets/scene_snow/elevator.emsc

test-panel-interaction: tests/panel_interaction_test.c src/game/em_panel.c src/game/em_panel.h
	@mkdir -p build
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -Isrc tests/panel_interaction_test.c src/game/em_panel.c -lm -o build/panel_interaction_test
	build/panel_interaction_test

.PHONY: test-weather-reference
test-weather-reference:
	python3 tools/test_weather_reference.py

.PHONY: test-area11-effect-reference
test-area11-effect-reference:
	python3 tools/test_area11_effect_reference.py

.PHONY: test-snow-tiles-reference
test-snow-tiles-reference:
	python3 tools/test_snow_tiles_reference.py

.PHONY: test-lighting-reference test-lighting
.PHONY: test-point-light-reference test-point-light
test-point-light-reference:
	python3 tools/test_point_light_reference.py

test-point-light: tests/test_point_light.c src/game/em_point_light.c src/game/em_point_light.h src/game/em_owner_services_original.c
	@mkdir -p build
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -Isrc tests/test_point_light.c src/game/em_point_light.c src/game/em_owner_services_original.c -lm -o build/test_point_light
	./build/test_point_light

# The skinned path's lighting (em_lighting_matrices / em_lighting_vertex
# over the bound 001D89D0's A and B, as em_status_models feeds the renderer)
# against the original kernel slice, and 001D8270 / 001D8690's one
# translation against the original instructions, over the first-control
# capture (docs/ACTOR_LIGHTING.md "Verification"; about 5 s).
.PHONY: test-actor-lighting-reference
test-actor-lighting-reference:
	python3 tools/test_actor_lighting_reference.py

test-lighting-reference:
	python3 tools/audit_opening_lighting.py

test-lighting: tests/test_lighting.c src/game/em_lighting.c src/game/em_lighting.h src/game/em_actor_light_001D89D0.c
	@mkdir -p build
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -Isrc tests/test_lighting.c src/game/em_lighting.c src/game/em_actor_light_001D89D0.c src/game/em_frame_render_heads.c src/game/em_owner_services_original.c -lm -o build/test_lighting
	build/test_lighting

.PHONY: test-indicator-child
test-indicator-child: tests/indicator_child_test.c src/game/em_indicator_child.c src/game/em_indicator_child.h
	@mkdir -p build
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -ffp-contract=off -Isrc tests/indicator_child_test.c src/game/em_indicator_child.c -o build/indicator_child_test
	build/indicator_child_test

.PHONY: test-props-indicators
test-props-indicators: tests/props_indicator_test.c src/game/em_props.c src/game/em_props.h src/game/em_effect_kinds.c
	@mkdir -p build
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc -ffp-contract=off tests/props_indicator_test.c src/game/em_effect_kinds.c -o build/props_indicator_test
	build/props_indicator_test

.PHONY: test-player-heading-reference
test-player-heading-reference:
	python3 tools/test_player_heading_reference.py

.PHONY: test-player-motor-reference
test-player-motor-reference:
	python3 tools/test_player_motor_reference.py

# Original AREA11 panel sound mapping and the scoped native dry mixer.
.PHONY: test-area11-sfx-reference test-area11-sfx
test-area11-sfx-reference:
	python3 tools/test_area11_sfx_reference.py

test-area11-sfx:
	python3 tools/test_area11_sfx_runtime.py

# Original door transit (001BBE40 on the EE float model, 001BC150).
.PHONY: test-door-transit
test-door-transit:
	python3 tools/test_door_transit_reference.py

.PHONY: test-face-slot-reference
test-face-slot-reference:
	python3 tools/test_face_slot_reference.py

.PHONY: test-face-attach-reference
test-face-attach-reference:
	python3 tools/test_face_attach_reference.py

.PHONY: test-aim-fire-control-reference
test-aim-fire-control-reference:
	python3 tools/test_aim_fire_control_reference.py

.PHONY: test-aim-fire-machines-reference
test-aim-fire-machines-reference:
	python3 tools/test_aim_fire_machines_reference.py

.PHONY: test-aim-fire-pose-reference
test-aim-fire-pose-reference:
	python3 tools/test_aim_fire_pose_reference.py

.PHONY: test-aim-fire-target-reference
test-aim-fire-target-reference:
	python3 tools/test_aim_fire_target_reference.py

# The gun lamp (em_aim_fire_lamp.c: 00187780, 00187690, 001D9530, 001D91A0,
# 001DA290, 001DA1E0, 001D4E20, 001D4EB0, 001D4B80, 001D4C30) against the
# original instructions over AREA11 route / AIM captures (AIM_FIRE.md
# section 10; about 5 s; EM_TEST_FULL=1 every case).
.PHONY: test-aim-fire-lamp-reference
test-aim-fire-lamp-reference:
	python3 tools/test_aim_fire_lamp_reference.py

.PHONY: test-aim-fire-shots-reference
test-aim-fire-shots-reference:
	python3 tools/test_aim_fire_shots_reference.py

.PHONY: test-aim-fire-reticle-reference
test-aim-fire-reticle-reference:
	python3 tools/test_aim_fire_reticle_reference.py

.PHONY: test-aim-fire-leaves-reference
test-aim-fire-leaves-reference:
	python3 tools/test_aim_fire_leaves_reference.py

.PHONY: test-aim-fire-tables
test-aim-fire-tables:
	python3 tools/test_aim_fire_tables.py

.PHONY: test-aim-fire-render-reference
test-aim-fire-render-reference:
	python3 tools/test_aim_fire_render_reference.py

.PHONY: test-aim-fire-live
test-aim-fire-live:
	mkdir -p build/aim-fire
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/aim_fire_live_test.c src/game/em_aim_fire_live.c src/game/em_aim_fire_lamp.c src/game/em_aim_fire_marker.c src/game/em_aim_fire_control.c src/game/em_aim_fire_pose.c src/game/em_aim_fire_target.c src/game/em_aim_fire_machines.c src/game/em_aim_fire_shots.c -lm -o build/aim-fire/live-test
	./build/aim-fire/live-test

.PHONY: test-aim-fire-world-live
test-aim-fire-world-live:
	mkdir -p build/aim-fire
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/aim_fire_world_live_test.c src/game/em_aim_fire_world_live.c src/game/em_aim_fire_leaves.c src/game/em_area02_math.c src/game/em_area02_misc.c src/game/em_area00_world.c src/game/em_area01_side.c src/game/em_area00_fx_exit.c src/game/em_area00_fx_spawn.c src/game/em_area00_fx_gs.c src/game/em_area00_fx_debris.c src/game/em_area00_fx_trail.c src/game/em_area01_ui_effect.c src/game/em_area01_render_gs.c -lm -o build/aim-fire/world-live-test
	./build/aim-fire/world-live-test

.PHONY: test-aim-fire-sdk-memory-reference
test-aim-fire-sdk-memory-reference:
	python3 tools/test_aim_fire_sdk_memory_reference.py

.PHONY: test-aim-fire-cable-live
test-aim-fire-cable-live:
	mkdir -p build/aim-fire
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -ffunction-sections -fdata-sections -Xlinker $(if $(filter Darwin,$(UNAME)),-dead_strip,--gc-sections) -Isrc tests/aim_fire_cable_live_test.c src/game/em_aim_fire_cable_live.c src/game/em_security_gun_rest.c src/game/em_security_gun.c src/game/em_owner_services_original.c src/game/em_effect_original.c src/game/em_coll_probe_original.c src/game/em_actor_collision.c src/game/em_area06_port_strip.c -lm -o build/aim-fire/cable-live-test
	./build/aim-fire/cable-live-test

.PHONY: test-aim-fire-effects-live
test-aim-fire-effects-live:
	python3 tools/test_aim_fire_effects_live.py

.PHONY: test-aim-fire-equipment-live
test-aim-fire-equipment-live:
	python3 tools/test_aim_fire_equipment_live.py

# AREA01 canonical adapters; exhaustive samples remain behind EM_TEST_FULL.
.PHONY: test-area01-actor-view-reference test-area01-memory-view-reference test-area01-player-view-reference test-area01-model-live-reference test-area01-model-draw-reference test-area01-runtime-reference test-area01-door-live-reference test-level2-dynamic-vu-reference test-world-textures-reference test-level2-floor-vu-reference
test-area01-actor-view-reference:
	python3 tools/test_area01_actor_view_reference.py
test-area01-memory-view-reference:
	python3 tools/test_area01_memory_view_reference.py
test-area01-player-view-reference:
	python3 tools/test_area01_player_view_reference.py
test-area01-model-live-reference:
	python3 tools/test_area01_model_live_reference.py
test-area01-model-draw-reference:
	python3 tools/test_area01_model_draw_reference.py
test-area01-runtime-reference:
	python3 tools/test_area01_runtime_reference.py
test-area01-door-live-reference:
	python3 tools/test_area01_door_live_reference.py
test-level2-dynamic-vu-reference:
	python3 tools/test_level2_dynamic_vu_reference.py
test-level2-floor-vu-reference:
	python3 tools/test_level2_floor_vu_reference.py
test-world-textures-reference:
	python3 tools/test_world_textures_reference.py

.PHONY: test-area01-collision-view-reference test-area01-shadow-live-reference test-area01-scratch-views
test-area01-collision-view-reference:
	python3 tools/test_area01_collision_view_reference.py
test-area01-shadow-live-reference:
	python3 tools/test_area01_shadow_live_reference.py
test-area01-scratch-views:
	python3 tools/test_area01_scratch_views.py

.PHONY: test-area01-pickup-live-reference test-area01-pickup-aura-reference test-area01-effects-services-reference test-area01-audio-services-reference test-area01-script-host-reference test-area01-script-workers-reference test-area01-timeline-reference
test-area01-pickup-live-reference:
	python3 tools/test_area01_pickup_live_reference.py
test-area01-pickup-aura-reference:
	python3 tools/test_area01_pickup_aura_reference.py
test-area01-effects-services-reference:
	python3 tools/test_area01_effects_services_reference.py
test-area01-audio-services-reference:
	python3 tools/test_area01_audio_services_reference.py
test-area01-script-host-reference:
	python3 tools/test_area01_script_host_reference.py
test-area01-script-workers-reference:
	python3 tools/test_area01_script_workers_reference.py
test-area01-timeline-reference:
	python3 tools/test_area01_timeline_reference.py

.PHONY: test-area01-interaction-live-reference
test-area01-interaction-live-reference:
	python3 tools/test_area01_interaction_live_reference.py

.PHONY: test-area01-scratch-alias
test-area01-scratch-alias:
	python3 tools/test_area01_scratch_alias.py

.PHONY: test-area01-camera-services-reference
test-area01-camera-services-reference:
	python3 tools/test_area01_camera_services_reference.py

.PHONY: test-area01-indicator-live-reference test-area01-smoke-harness
test-area01-indicator-live-reference:
	python3 tools/test_area01_indicator_live_reference.py
test-area01-smoke-harness:
	python3 tools/test_level_smoke_area01.py --verify-harness

.PHONY: test-area01-prop-live-reference
test-area01-prop-live-reference:
	python3 tools/test_area01_prop_live_reference.py

.PHONY: test-area01-gun-aux-reference
test-area01-gun-aux-reference:
	python3 tools/test_area01_gun_aux_reference.py

.PHONY: test-area01-shared-services-reference
test-area01-shared-services-reference:
	python3 tools/test_area01_shared_services_reference.py

.PHONY: test-area01-equipment-live-reference
test-area01-equipment-live-reference:
	python3 tools/test_area01_equipment_live_reference.py

.PHONY: test-area01-light-live-reference
test-area01-light-live-reference:
	python3 tools/test_area01_light_live_reference.py

.PHONY: test-area01-sfx-registry
test-area01-sfx-registry:
	python3 tools/test_area01_sfx_registry.py

.PHONY: test-status-pose69-reference
test-status-pose69-reference:
	python3 tools/test_status_pose69_reference.py

.PHONY: test-area01-rcl-workers test-area01-flame-services-reference
test-area01-rcl-workers:
	python3 tools/test_area01_rcl_workers_reference.py
test-area01-flame-services-reference:
	python3 tools/test_area01_flame_services_reference.py

.PHONY: test-area01-matrix-service
test-area01-matrix-service:
	python3 tools/test_area01_matrix_service_reference.py

.PHONY: test-area01-static-ground
test-area01-static-ground:
	python3 tools/test_area01_static_ground_reference.py

.PHONY: test-area01-crate-registry
test-area01-crate-registry:
	python3 tools/test_area01_crate_registry_reference.py

.PHONY: test-area01-matrix-scratch
test-area01-matrix-scratch:
	python3 tools/test_area01_matrix_scratch_reference.py

.PHONY: test-area01-glow-services-reference
test-area01-glow-services-reference:
	python3 tools/test_area01_glow_services_reference.py

.PHONY: test-area01-closeout
test-area01-closeout:
	python3 tools/test_area01_closeout_reference.py

.PHONY: test-area01-exploration test-area01-exploration-harness
AREA01_EXPLORE_ARGS ?= --case water
test-area01-exploration: $(BIN)
	python3 tools/test_area01_exploration.py $(AREA01_EXPLORE_ARGS)
test-area01-exploration-harness:
	python3 tools/test_area01_exploration_harness.py

.PHONY: test-area01-water-reference
test-area01-water-reference:
	python3 tools/test_area01_water_reference.py
