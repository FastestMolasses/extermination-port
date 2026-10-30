/* EM_START_AT=<name>: the developer switch that starts a run at a start
 * point by loading its start-point save through the boot title's LOAD GAME
 * (docs/STARTUP.md "Developer switches", docs/SAVE_GAME.md section 8; the
 * user's decision of 2026-10-19, "Like LOAD GAME").
 *
 * Off by default. With a name, the boot's memory card is the start-point
 * card build/start_saves/<name>/ (tools/build_start_saves.py, make
 * start-saves), served read-only (em_memcard_set_read_only), and the
 * frontend (em_frontend.c) presses through the boot exactly as the title
 * load test does: the warning / Sony / Deep Space screens, START on the
 * attract movie, Down to LOAD GAME and its CROSS, and on the card screen
 * 00225AC0(0) / 00227300 slot 1 on port 0 and Yes. Everything that runs is
 * the original route: 001AC070 state 5 reads the slot (00228B60), state 4
 * hands the loaded game to 001ACEC0 with D_00275BE0 = 1, and the chain
 * enters the block's area at the save's load pose. The frames up to the
 * player's control in that area run unpaced (host pacing only; the frames
 * are the same); the pad is the player's again from the loaded game's
 * handoff.
 *
 * The names: area01 (AREA01's arrival from AREA11), area00 (AREA00's
 * arrival from AREA01), area01-revisit (AREA01 again, from AREA00), area02
 * (AREA02's arrival), and a route beat's start (its source state):
 * a02_01 .. a02_05 (discovery runs) and a01_03, a01_05, a01_s7, a01_07,
 * a00_01, a00_04, a01r_01 (tools/test_cutscene_skip.py's skips). */
#ifndef EM_START_AT_H
#define EM_START_AT_H

/* Reads EM_START_AT once. 0: off (unset or empty) or a start point whose
 * card passed every check; -1 refused (the reason on stderr): an unknown
 * name; a combination with EM_NEW_GAME=1 or a fixture that drives the pad
 * or chooses another route, input or card (EM_SKIP_STARTUP, any EM_*TEST
 * but EM_START_AT_TEST / EM_INPUT_TEST, any EM_CAPTURE*, EM_SCENE,
 * EM_TEST_MEMCARD_ROOT, EM_INPUT_RECORD / PLAY, the level smoke's and the
 * other scripts' variables: fixture_set in em_start_at.c); a card that is missing or not the builder's (start.json
 * missing or not this name's, a file missing, extra or not the SHA-256
 * start.json records, a symlink, slot2 not empty, the slot not one checked
 * 0x640-byte progress block of the name's area, or without the load pose
 * start.json records: a card from before the load pose, save2 round 4, is
 * stale; rebuild with make start-saves). */
int em_start_at_check(void);

/* The start point's name, or NULL when the switch is off (after
 * em_start_at_check returned 0). */
const char *em_start_at_name(void);
/* The card root (holding slot1 and slot2) em_memcard serves. */
const char *em_start_at_card_root(void);
/* 1 under EM_START_AT_TEST=1 (test instrumentation; its name ends in TEST,
 * so the run is headless): the frontend reports the block, the placement
 * and control as the title load test does, and quits 60 ticks after
 * control. EM_START_AT_DIR=<dir> (only with it) reads the card from
 * <dir>/<name> instead of build/start_saves/<name>. */
int em_start_at_report(void);
/* EM_START_AT_PAD=<file> (test input; docs/STARTUP.md): a recorded EMA1 pad
 * script, loaded by em_start_at_check, installed as the whole pad at control
 * (em_start_at_pad_control) and advanced once per frame
 * (em_start_at_pad_frame: 1 on the frame the script ends, then the pad is
 * neutral). Without the variable both do nothing. */
void em_start_at_pad_control(unsigned counter);
/* pos / fwd: the player's position and the camera's forward (with
 * EM_START_AT_PATH the stick steers toward the recorded path). */
int em_start_at_pad_frame(unsigned counter, const float pos[3], const float fwd[3]);
/* 1 on every 100th frame of a running pad script (its progress line). */
int em_start_at_pad_progress(unsigned counter);
/* The pad script's clock (its frame; it waits for a lagging player). */
int em_start_at_pad_clock(void);
/* EM_START_AT_MARKS=<f>,<f>,...: the script frames at which the frontend
 * reports the progress block (a chained beat's end, tools/
 * mid_route_beats.py). 1 on the frame the clock reaches the next mark. */
int em_start_at_pad_mark(void);

#endif
