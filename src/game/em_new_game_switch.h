/* EM_NEW_GAME=1: the developer switch that starts New Game straight into the
 * AREA11 opening (docs/STARTUP.md "Developer switches";
 * docs/LAUNCHER_OPTIONS.md "Not launcher options").
 *
 * It is not a launcher option and not a test variable (its name does not
 * end in TEST, so the window stays; EM_HEADLESS=1 still makes the run
 * headless). The startup frontend (the warning / Sony / Deep Space screens,
 * the attract movie and the title menu, 001AB7E0..001AC070) does not run;
 * the run enters the title's New Game exactly as 001AC070 state 4 does
 * (D_00275BE0 = 0, 001AB790(001ACEC0): em_game_install_new), and the intro
 * movie that 001AD360 step 1 requests is skipped through the original
 * START skip path (002036E0's held & 0x800 test, at the decoder's
 * completed-picture gate). Everything else is the New Game route.
 *
 * What the frontend runs before New Game and what the switch does instead
 * is proved by tools/test_new_game_switch.py (make test-new-game-switch):
 * the port's whole static state, the module loader and the IOP at the
 * opening's first frame, against the frontend route. */
#ifndef EM_NEW_GAME_SWITCH_H
#define EM_NEW_GAME_SWITCH_H

/* 1 when EM_NEW_GAME=1 is set. */
int em_new_game_switch_requested(void);

/* Refuses (-1, with the reason on stderr) a switch that cannot start the
 * original New Game route: a value other than 0/1, a combination with
 * EM_SKIP_STARTUP (the old debug fixture), or with an EM_STARTUP_TEST
 * fixture that tests the frontend itself. The New Game fixtures (newgame,
 * newgame-control, newgame-skip, newgame-level) run after the handoff, so
 * they combine with the switch; only their title part does not run. */
int em_new_game_switch_check(void);

/* EM_NEW_GAME_STATE_TEST=<file> (test instrumentation; the name ends in
 * TEST, so the run is headless): at the first world frame after New Game
 * (the opening's first frame, before any of its work), write the state
 * image and quit. Called at the head of both world-frame variants. */
void em_new_game_state_test_before_frame(void);
/* 1 when the state test was requested and could not write its image. */
int em_new_game_state_test_failed(void);

#endif
