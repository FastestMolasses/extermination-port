/* The live first-level smoke (SCENE_COORDINATOR_DESIGN.md step S13;
 * docs/LEVEL_SMOKE.md). Optional end-to-end instrumentation, inactive in
 * normal gameplay.
 *
 * EM_STARTUP_TEST=newgame-level runs the real startup, New Game and AREA11
 * opening headless, then walks the first level's route phase by phase in
 * the order of docs/FIRST_LEVEL_ROUTE.md section 3, up to and including
 * EM_LEVEL_SMOKE_UNTIL (default: the last phase). It drives only pad input,
 * never positions or state. A phase whose original owners are not live in
 * the port yet reports NOT-LIVE with the step that makes it live, and the
 * phases after it are not run (the route is sequential). */
#ifndef EM_LEVEL_SMOKE_TEST_H
#define EM_LEVEL_SMOKE_TEST_H

/* From em_game_install_new, beside em_opening_control_test_begin. */
void em_level_smoke_test_begin(void);
/* At the end of every world frame and status frame (frame_close_out, the
 * 001D1EA0 position), beside em_opening_control_test_after_frame. */
void em_level_smoke_test_after_frame(void);
int em_level_smoke_test_active(void);
int em_level_smoke_test_failed(void);

#endif
