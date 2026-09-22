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
           src/game/em_task.c src/game/em_frame.c src/game/em_game.c src/game/em_props.c src/game/em_scene.c src/game/em_camera.c src/game/em_player_damage.c src/game/em_player.c src/game/em_player_heading.c src/game/em_player_motor.c src/game/em_director.c src/game/em_area11_flow.c src/game/em_script.c src/game/em_area11_opening.c \
           src/game/em_opening_runtime.c src/game/em_cinematic_camera.c src/game/em_random.c src/game/em_opening_control_test.c \
           src/game/em_opening_actor.c src/game/em_opening_face.c src/game/em_opening_media.c src/game/em_lighting.c \
           src/game/em_point_light.c \
           src/game/em_area11_effect.c src/game/em_area11_effect_runtime.c \
           src/game/em_weather.c src/game/em_snow.c src/game/em_snow_particles.c src/game/em_snow_projection.c src/game/em_snow_runtime.c \
           src/game/em_collision.c src/game/em_door.c src/game/em_bgm.c \
           src/game/em_sfx.c src/game/em_pickup.c \
           src/game/em_examine.c src/game/em_truck.c src/game/em_panel.c src/game/em_panel_program.c src/game/em_battery_ui.c src/game/em_camera_retarget.c \
           src/game/em_camera_probe.c src/game/em_camera_rotation.c src/game/em_interaction_frame.c src/game/em_interaction_animation.c \
           src/game/em_interaction_runtime.c src/game/em_status_frame.c src/game/em_panel_message.c \
           src/game/em_elevator.c src/game/em_elevator_program.c src/game/em_elevator_runtime.c \
           src/game/em_hud.c src/game/em_weapon.c src/game/em_enemy.c

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
    src/game/em_bgm.c src/game/em_random.c src/game/em_fade.c
.PHONY: test-opening-runtime
test-opening-runtime: $(OPENING_TEST_SRC)
	@mkdir -p build
	$(CC) $(CFLAGS) -ffp-contract=off $(OPENING_TEST_SRC) -lm -o build/opening_runtime_test
	build/opening_runtime_test

.PHONY: test-pickup-lights
test-pickup-lights: tests/pickup_light_test.c src/game/em_pickup.c src/game/em_pickup.h
	@mkdir -p build
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/pickup_light_test.c -o build/pickup_light_test
	build/pickup_light_test

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
test-camera-rotation-reference:
	python3 tools/test_camera_rotation_reference.py

test-interaction-frame-reference:
	python3 tools/test_interaction_frame_reference.py

test-interaction-animation-reference:
	python3 tools/test_interaction_animation_reference.py

.PHONY: test-interaction-runtime
test-interaction-runtime:
	@mkdir -p build
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -Isrc tests/interaction_runtime_test.c src/game/em_interaction_runtime.c src/game/em_interaction_frame.c src/game/em_interaction_animation.c src/game/em_script.c src/em_model.c -lm -o build/interaction_runtime_test
	build/interaction_runtime_test

.PHONY: test-status-frame-reference
test-status-frame-reference:
	python3 tools/test_status_frame_reference.py

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
