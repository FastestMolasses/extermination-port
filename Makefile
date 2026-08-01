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
           src/game/em_task.c src/game/em_frame.c src/game/em_game.c src/game/em_props.c src/game/em_scene.c src/game/em_camera.c src/game/em_player_damage.c src/game/em_player.c src/game/em_director.c \
           src/game/em_collision.c src/game/em_door.c src/game/em_bgm.c \
           src/game/em_sfx.c src/game/em_pickup.c \
           src/game/em_examine.c src/game/em_truck.c \
           src/game/em_hud.c src/game/em_weapon.c src/game/em_enemy.c

# ---------------------------------------------------------------- macOS
ifeq ($(UNAME),Darwin)
CC        := clang
SRC       := $(COMMON) \
             src/platform/mac/em_platform_mac.m \
             src/gfx/metal/em_gfx_metal.m \
             src/audio/mac/em_audio_mac.c
FRAMEWORKS := -framework Cocoa -framework Metal -framework QuartzCore \
              -framework AudioToolbox
LDFLAGS   := $(FRAMEWORKS)
endif

# ---------------------------------------------------------------- Linux
ifeq ($(UNAME),Linux)
CC        := cc
SRC       := $(COMMON) \
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

$(BIN): $(SRC)
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
