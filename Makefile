# Extermination native port — build.
#
# Clean-room: no third-party libraries, no build-system dependencies beyond
# make + the platform's own compiler/frameworks. Per-platform source + flags
# are selected from `uname`. Cross-compilation targets get their own rules as
# the Windows/Linux backends are implemented.

UNAME := $(shell uname)

BIN     := build/extermination
CFLAGS  := -O2 -Wall -Wextra -Isrc
COMMON  := src/main.c src/em_model.c src/em_input.c \
           src/game/em_fade.c src/game/em_startup.c src/game/em_frontend.c src/game/em_startup_audio.c \
           src/game/em_task.c src/game/em_frame.c src/game/em_game.c src/game/em_player_frame.c src/game/em_render_frame.c src/game/em_game_selftest.c src/game/em_props.c src/game/em_scene.c src/game/em_camera.c src/game/em_player_damage.c src/game/em_player.c src/game/em_player_heading.c src/game/em_player_motor.c src/game/em_player_reversal.c src/game/em_director.c src/game/em_area11_flow.c src/game/em_script.c src/game/em_area11_opening.c \
           src/game/em_opening_runtime.c src/game/em_cinematic_camera.c src/game/em_random.c src/game/em_opening_control_test.c src/game/em_level_smoke_test.c \
           src/game/em_opening_actor.c src/game/em_opening_face.c src/game/em_opening_media.c src/game/em_lighting.c \
           src/game/em_point_light.c \
           src/game/em_area11_effect.c src/game/em_area11_effect_runtime.c \
           src/game/em_weather.c src/game/em_snow.c src/game/em_snow_particles.c src/game/em_snow_projection.c src/game/em_snow_runtime.c \
           src/game/em_collision.c src/game/em_door.c src/game/em_door_candidate.c src/game/em_door_original.c src/game/em_door_original_runtime.c src/game/em_door_transit.c src/game/em_door_program.c src/game/em_bgm.c \
           src/game/em_sfx.c src/game/em_sfx_bank.c src/game/em_pickup.c src/game/em_pickup_owner.c src/game/em_pickup_program.c src/game/em_pickup_motion.c src/game/em_pickup_items_original.c src/game/em_roger.c src/game/em_roger_assets.c \
           src/game/em_roger_runtime.c src/game/em_face_model.c src/game/em_player_face_host.c \
           src/game/em_examine.c src/game/em_truck.c src/game/em_panel.c src/game/em_panel_program.c src/game/em_panel_runtime.c src/game/em_battery_ui.c src/game/em_camera_retarget.c \
           src/game/em_camera_probe.c src/game/em_camera_rotation.c src/game/em_interaction_frame.c src/game/em_interaction_animation.c \
           src/game/em_interaction_alignment.c src/game/em_interaction_projection.c src/game/em_area11_interaction_host.c \
           src/game/em_interaction_runtime.c src/game/em_interaction_cinematic.c src/game/em_interaction_scan.c src/game/em_interaction_scene.c src/game/em_status_frame.c \
           src/game/em_status_page.c src/game/em_item_root.c src/game/em_item_ui.c \
           src/game/em_item_trail.c src/game/em_item_sdk_math.c src/game/em_item_device.c \
           src/game/em_item_geometry.c src/game/em_status_hub.c src/game/em_status_draw.c src/game/em_status_hub_ui.c \
           src/game/em_status_runtime.c src/game/em_status_background.c src/game/em_status_background_draw.c \
           src/game/em_sdk_math_original.c src/game/em_status_scene_original.c src/game/em_status_models.c \
           src/game/em_owner_services_original.c \
           src/game/em_message_service.c src/game/em_message_draw_original.c src/game/em_message_glyph_original.c \
           src/game/em_message_live.c \
           src/game/em_pose_bank.c src/game/em_pose_transition.c src/game/em_player_pose.c src/game/em_player_pose_host.c \
           src/game/em_player_foot_stop.c src/game/em_player_floor.c \
           src/game/em_player_stage_workers.c src/game/em_player_stage_live.c \
           src/game/em_elevator.c src/game/em_elevator_program.c src/game/em_elevator_runtime.c \
           src/game/em_hud.c src/game/em_weapon.c src/game/em_enemy.c \
           src/game/em_scene_bindings.c src/game/em_scene_task.c src/game/em_scene_frame.c \
           src/game/em_scene_classify.c src/game/em_frame_trace.c \
           src/game/em_actor_pool.c src/game/em_actor_roster.c src/game/em_area11_bindings.c \
           src/game/em_spawn_table.c src/game/em_load_veil.c src/game/em_manager_008257A0.c \
           src/game/em_director_original.c

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

# Unit test for the weapon fire sub-state machine (the engine ladder-
# clip cadence + queue window, the laser hide window, the L3 top-up
# gate, dry-mag auto reload, the persistent flashlight preference
# toggle, and the input-event-API mash leg): links em_weapon.c +
# em_input.c (the real pad model) — every other module is stubbed in
# the test, so it runs headless on any host.
test-weapon: tests/weapon_fire_test.c src/game/em_weapon.c src/em_input.c
	@mkdir -p build
	$(CC) $(CFLAGS) tests/weapon_fire_test.c src/game/em_weapon.c \
	    src/em_input.c -o build/weapon_fire_test
	./build/weapon_fire_test

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
.PHONY: test-area11-flow
test-area11-flow: tests/area11_flow_test.c src/game/em_area11_flow.c src/game/em_area11_flow.h
	@mkdir -p build
	$(CC) $(CFLAGS) -ffp-contract=off tests/area11_flow_test.c src/game/em_area11_flow.c -lm -o build/area11_flow_test
	build/area11_flow_test

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

.PHONY: test-cinematic-camera
test-cinematic-camera: tests/cinematic_camera_test.c src/game/em_cinematic_camera.c src/game/em_cinematic_camera.h
	@mkdir -p build
	$(CC) $(CFLAGS) -ffp-contract=off tests/cinematic_camera_test.c src/game/em_cinematic_camera.c -lm -o build/cinematic_camera_test
	build/cinematic_camera_test

.PHONY: test-opening-actor test-script-reference
OPENING_ACTOR_TEST_SRC := tests/opening_actor_test.c src/game/em_opening_actor.c \
    src/game/em_opening_face.c src/game/em_random.c src/em_model.c
test-opening-actor: $(OPENING_ACTOR_TEST_SRC)
	@mkdir -p build
	$(CC) $(CFLAGS) $(OPENING_ACTOR_TEST_SRC) -lm -o build/opening_actor_test
	build/opening_actor_test

.PHONY: test-opening-face-reference
test-opening-face-reference:
	python3 tools/test_opening_face_reference.py

test-script-reference:
	python3 tools/test_script_reference.py

.PHONY: test-opening-media test-bgm-ticks
test-opening-media: tests/opening_media_test.c src/game/em_opening_media.c
	@mkdir -p build
	$(CC) $(CFLAGS) tests/opening_media_test.c src/game/em_opening_media.c -lm -o build/opening_media_test
	build/opening_media_test

test-bgm-ticks: tests/bgm_tick_test.c src/game/em_bgm.c
	@mkdir -p build
	$(CC) $(CFLAGS) tests/bgm_tick_test.c src/game/em_bgm.c -lm -o build/bgm_tick_test
	build/bgm_tick_test

OPENING_TEST_SRC := tests/opening_runtime_test.c src/game/em_opening_runtime.c \
    src/game/em_area11_opening.c src/game/em_script.c src/game/em_cinematic_camera.c \
    src/game/em_opening_actor.c src/game/em_opening_face.c src/em_model.c src/game/em_opening_media.c \
    src/game/em_bgm.c src/game/em_random.c src/game/em_fade.c \
    src/game/em_scene_frame.c src/game/em_scene_classify.c src/game/em_status_frame.c \
    src/game/em_message_live.c src/game/em_message_service.c src/game/em_message_draw_original.c \
    src/game/em_message_glyph_original.c src/game/em_director_original.c
.PHONY: test-opening-runtime
test-opening-runtime: $(OPENING_TEST_SRC)
	@mkdir -p build
	$(CC) $(CFLAGS) -ffp-contract=off $(OPENING_TEST_SRC) -lm -o build/opening_runtime_test
	build/opening_runtime_test

.PHONY: test-pickup-lights
test-pickup-lights: tests/pickup_light_test.c src/game/em_pickup.c src/game/em_pickup.h
	@mkdir -p build
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/pickup_light_test.c $(PICKUP_ORIGINAL_TEST_SRC) -o build/pickup_light_test
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

.PHONY: test-roger-assets
test-roger-assets: tests/roger_assets_test.c src/game/em_roger_assets.c
	@mkdir -p build
	$(CC) -std=c11 -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/roger_assets_test.c src/game/em_roger_assets.c src/em_model.c src/game/em_pose_bank.c src/game/em_pose_transition.c src/game/em_player_pose.c src/game/em_script.c -o build/roger_assets_test
	build/roger_assets_test

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

# S12b: the room move. 0018AB00 against the executed original, and a headless
# New Game whose AREA11 door commits a room move at first control
# (EM_ROOM_MOVE_TEST): its ticks (EM_AREA_CHANGE_LOG) are compared with the
# original route capture 09_fence_door and 001AD010 is replayed through the
# executed original.
.PHONY: test-room-move-reference
test-room-move-reference: $(BIN)
	mkdir -p build/room_move_reference
	EM_UNCAPPED=1 EM_STARTUP_TEST=newgame-control EM_ROOM_MOVE_TEST=1 \
	    EM_AREA_CHANGE_LOG=build/room_move_reference/ticks.jsonl $(BIN) > build/room_move_reference/run.log 2>&1
	grep -q "newgame control test: PASS" build/room_move_reference/run.log
	grep -q "room move test: PASS" build/room_move_reference/run.log
	python3 tools/test_room_move_reference.py --log build/room_move_reference/ticks.jsonl

# S13: the live first-level smoke. A headless New Game walks the route of
# docs/FIRST_LEVEL_ROUTE.md phase by phase (EM_LEVEL_SMOKE_UNTIL, default
# the whole route); live phases must pass in process and against the
# original captures (tools/test_level_smoke.py), later phases report
# NOT-LIVE with the step they wait on (docs/LEVEL_SMOKE.md).
.PHONY: test-level-smoke
test-level-smoke: $(BIN)
	mkdir -p build/level_smoke
	EM_UNCAPPED=1 EM_STARTUP_TEST=newgame-level EM_AREA_CHANGE_LOG=build/level_smoke/ticks.jsonl \
	    $(BIN) > build/level_smoke/run.log 2>&1 || (grep "level smoke" build/level_smoke/run.log; false)
	grep "level smoke:" build/level_smoke/run.log
	python3 tools/test_level_smoke.py --log build/level_smoke/ticks.jsonl --run-log build/level_smoke/run.log

.PHONY: test-message-service
test-message-service:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/message_service_test.c src/game/em_message_service.c -o build/message_service_test && ./build/message_service_test

.PHONY: test-message-service-reference
test-message-service-reference:
	python3 tools/test_message_service_reference.py

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
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc -ffp-contract=off tests/crate_drum_original_test.c src/game/em_crate_original.c src/game/em_drum_original.c src/game/em_item_sdk_math.c src/game/em_interaction_scan.c src/game/em_item_trail.c -lm -o build/crate_drum_original_test && ./build/crate_drum_original_test
	python3 tools/test_crate_original_reference.py
	python3 tools/test_drum_original_reference.py

.PHONY: test-player-reversal-reference
test-player-reversal-reference:
	python3 tools/test_player_reversal_reference.py

.PHONY: test-player-reversal-host
test-player-reversal-host:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/player_reversal_host_test.c src/game/em_player.c src/game/em_player_floor.c src/game/em_player_reversal.c src/game/em_player_motor.c src/game/em_player_heading.c -lm -o build/player_reversal_host_test && ./build/player_reversal_host_test

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
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/player_states_host_test.c src/game/em_player.c src/game/em_player_floor.c src/game/em_player_reversal.c src/game/em_player_motor.c src/game/em_player_heading.c src/game/em_player_slide.c src/game/em_player_climb.c src/game/em_actor_collision.c src/game/em_collision.c src/game/em_actor_pool.c src/game/em_player_reaction.c -lm -o build/player_states_host_test && ./build/player_states_host_test

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

.PHONY: test-camera-leftovers-reference
test-camera-leftovers-reference:
	python3 tools/test_camera_leftovers_reference.py

.PHONY: test-camera-leftovers
test-camera-leftovers:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -Isrc tests/camera_leftovers_test.c src/game/em_camera_leftovers.c src/game/em_camera_leftovers_solver.c src/game/em_camera_follow_original.c src/game/em_sdk_math_original.c -lm -o build/camera_leftovers_test && ./build/camera_leftovers_test

.PHONY: test-script-door-fan
test-script-door-fan:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -ffp-contract=off -Isrc tests/script_door_fan_test.c src/game/em_script_door_fan.c src/game/em_script_door_fan_husk.c -lm -o build/script_door_fan_test && ./build/script_door_fan_test

.PHONY: test-script-door-fan-reference
test-script-door-fan-reference:
	python3 tools/test_script_door_fan_reference.py

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

.PHONY: test-ee-float-header
test-ee-float-header:
	python3 tools/test_ee_float_header.py

.PHONY: test-level-material-reference
test-level-material-reference:
	python3 tools/test_level_material_reference.py

.PHONY: test-area-script
test-area-script:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -ffp-contract=off -Isrc tests/area_script_test.c src/game/em_area_script.c src/game/em_script.c src/game/em_message_service.c src/game/em_interaction_frame.c src/game/em_interaction_cinematic.c src/game/em_cinematic_playback.c src/game/em_cinematic_camera.c src/game/em_camera_rotation.c src/game/em_item_sdk_math.c src/game/em_interaction_scan.c src/game/em_item_trail.c src/game/em_fan_original.c -lm -o build/area_script_test && ./build/area_script_test assets/scene_snow/roger/programs.emsc assets/scene_snow/elevator.emsc assets/scene_snow/panel/scripts.emsc ../Extermination/extract/OVERLAY/AREA11.BIN

.PHONY: test-area-script-reference
test-area-script-reference:
	python3 tools/test_area_script_reference.py

.PHONY: test-player-climb-reference
test-player-climb-reference:
	python3 tools/test_player_climb_reference.py

.PHONY: test-player-slide-reference
test-player-slide-reference:
	python3 tools/test_player_slide_reference.py

.PHONY: test-background-reference
test-background-reference:
	python3 tools/test_background_reference.py

.PHONY: test-director-original
test-director-original:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc -ffp-contract=off tests/director_original_test.c src/game/em_director_original.c -lm -o build/director_original_test && ./build/director_original_test ../Extermination/extract/OVERLAY/AREA11.BIN ../Extermination/config/SCUS_971.12
	python3 tools/test_director_original_reference.py

.PHONY: test-actor-collision
test-actor-collision:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc -ffp-contract=off tests/actor_collision_test.c src/game/em_actor_collision.c src/game/em_collision.c src/game/em_actor_pool.c -lm -o build/actor_collision_test && ./build/actor_collision_test

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

.PHONY: test-roger-runtime
test-roger-runtime:
	@mkdir -p build
	python3 tools/test_roger_encounter_reference.py
	$(CC) -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/roger_runtime_test.c src/game/em_roger_runtime.c src/game/em_roger_assets.c src/game/em_roger.c src/game/em_face_model.c src/game/em_opening_face.c src/game/em_interaction_scene.c src/game/em_interaction_scan.c src/em_model.c src/game/em_pose_bank.c src/game/em_pose_transition.c src/game/em_player_pose.c src/game/em_script.c -o build/roger_runtime_test
	build/roger_runtime_test

test-pickup-original: tests/pickup_original_test.c src/game/em_pickup.c $(PICKUP_ORIGINAL_TEST_SRC)
	@mkdir -p build/pickup_owner_reference
	$(CC) -std=c11 -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/pickup_original_test.c $(PICKUP_ORIGINAL_TEST_SRC) -o build/pickup_original_test
	build/pickup_original_test

.PHONY: test-panel-reference test-panel-interaction
test-panel-reference:
	python3 tools/test_panel_reference.py

.PHONY: test-battery-reference test-battery-ui-reference test-panel-program
test-battery-reference:
	python3 tools/test_battery_reference.py

test-battery-ui-reference:
	python3 tools/test_battery_ui_reference.py

.PHONY: test-camera-retarget-reference
test-camera-retarget-reference:
	python3 tools/test_camera_retarget_reference.py

.PHONY: test-camera-probe-reference test-camera-interaction-fixture test-interaction-frame-reference test-interaction-animation-reference test-collision-faces-reference
test-camera-probe-reference:
	python3 tools/test_camera_probe_reference.py

test-camera-interaction-fixture:
	python3 tools/test_camera_interaction_fixture.py

.PHONY: test-camera-rotation-reference
.PHONY: test-camera-commit-reference test-interaction-recovery-reference
test-camera-commit-reference:
	python3 tools/test_camera_commit_reference.py

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

.PHONY: test-player-pose-host
test-player-pose-host:
	@mkdir -p build/player_pose_channels
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -Isrc \
	    tests/player_pose_host_test.c src/game/em_player_pose_host.c src/game/em_player_pose.c \
	    src/game/em_pose_bank.c src/game/em_pose_transition.c src/game/em_fade.c \
	    src/game/em_player_foot_stop.c src/game/em_camera_rotation.c -lm \
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

test-status-scene-original:
	@mkdir -p build/status_scene_reference
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -Isrc tests/status_scene_original_test.c src/game/em_status_scene_original.c -lm -o build/status_scene_reference/status_scene_original_test
	build/status_scene_reference/status_scene_original_test

test-status-models:
	@mkdir -p build/status_models
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -Isrc tests/status_models_test.c src/game/em_status_models.c src/game/em_status_scene_original.c src/game/em_owner_services_original.c src/game/em_player_pose.c src/game/em_pose_bank.c src/game/em_pose_transition.c src/em_model.c src/game/em_random.c -lm -o build/status_models/status_models_test
	build/status_models/status_models_test assets/status_models ../Extermination/build/startup-reference/status-hub/eeMemory.bin

.PHONY: test-battery-pickup-reference test-item-device-reference
test-battery-pickup-reference:
	python3 tools/test_battery_pickup_reference.py

test-item-device-reference:
	python3 tools/test_item_device_reference.py

test-status-runtime:
	@mkdir -p build/status_page_reference
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -Wl,-dead_strip -Isrc tests/status_runtime_test.c src/game/em_status_hub.c src/game/em_status_runtime.c src/game/em_status_frame.c src/game/em_status_page.c src/game/em_item_root.c src/game/em_item_ui.c src/game/em_item_trail.c src/game/em_battery_ui.c src/game/em_panel.c src/game/em_status_hub_ui.c src/game/em_status_draw.c src/game/em_item_geometry.c src/game/em_item_sdk_math.c -lm -o build/status_page_reference/status_runtime_test
	build/status_page_reference/status_runtime_test assets/scene_snow/panel/battery.emba assets/scene_snow/panel/item_root.emir

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
	    src/game/em_message_glyph_original.c src/game/em_script.c src/em_model.c -lm -o build/panel_runtime_test
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

.PHONY: test-snow-particles-reference test-snow-particles
test-area11-effect-reference:
	python3 tools/test_area11_effect_reference.py

AREA11_EFFECT_TEST_SRC := tests/area11_effect_runtime_test.c src/game/em_area11_effect.c src/game/em_area11_effect_runtime.c src/game/em_random.c src/game/em_snow_particles.c src/game/em_snow_projection.c
.PHONY: test-area11-effect-runtime test-area11-effect-reference
test-area11-effect-runtime: $(AREA11_EFFECT_TEST_SRC)
	@mkdir -p build
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -Isrc $(AREA11_EFFECT_TEST_SRC) -lm -o build/area11_effect_runtime_test
	./build/area11_effect_runtime_test

test-snow-particles-reference:
	python3 tools/test_snow_particles_reference.py

.PHONY: test-snow-tiles-reference
test-snow-tiles-reference:
	python3 tools/test_snow_tiles_reference.py

.PHONY: test-snow-projection-reference
test-snow-projection-reference:
	python3 tools/test_snow_projection_reference.py

.PHONY: test-lighting-reference test-lighting
.PHONY: test-point-light-reference test-point-light
test-point-light-reference:
	python3 tools/test_point_light_reference.py

test-point-light: tests/test_point_light.c src/game/em_point_light.c src/game/em_point_light.h
	@mkdir -p build
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -Isrc tests/test_point_light.c src/game/em_point_light.c -lm -o build/test_point_light
	./build/test_point_light

test-lighting-reference:
	python3 tools/audit_opening_lighting.py

test-lighting: tests/test_lighting.c src/game/em_lighting.c src/game/em_lighting.h
	@mkdir -p build
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -Isrc tests/test_lighting.c src/game/em_lighting.c -lm -o build/test_lighting
	build/test_lighting

test-snow-particles:
	@mkdir -p build
	$(CC) -std=c11 -O1 -g -ffp-contract=off -fsanitize=address,undefined -I src tests/test_snow_particles.c src/game/em_snow_particles.c -lm -o build/test_snow_particles
	build/test_snow_particles ../Extermination/config/SCUS_971.12

SNOW_RUNTIME_TEST_SRC := tests/snow_runtime_test.c src/game/em_snow_runtime.c \
    src/game/em_weather.c src/game/em_snow.c src/game/em_snow_particles.c src/game/em_snow_projection.c src/game/em_random.c
.PHONY: test-snow-runtime
test-snow-runtime: $(SNOW_RUNTIME_TEST_SRC)
	@mkdir -p build
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -Isrc $(SNOW_RUNTIME_TEST_SRC) -lm -o build/snow_runtime_test
	build/snow_runtime_test

.PHONY: test-props-indicators
test-props-indicators: tests/props_indicator_test.c src/game/em_props.c src/game/em_props.h
	@mkdir -p build
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/props_indicator_test.c -o build/props_indicator_test
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

# Original door transit and shared-player program.
.PHONY: test-door-transit
test-door-transit:
	python3 tools/test_door_transit_reference.py

.PHONY: test-door-program
test-door-program:
	python3 tools/test_door_program_reference.py

.PHONY: test-door-program-runtime
test-door-program-runtime:
	python3 tools/test_door_program_runtime.py

.PHONY: test-player-face-host
test-player-face-host:
	python3 tools/test_player_face_host.py
