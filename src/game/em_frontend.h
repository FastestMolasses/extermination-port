/* Native resource/rendering adapter for the recovered startup control flow. */
#ifndef EM_FRONTEND_H
#define EM_FRONTEND_H

#include <stddef.h>
#include <stdint.h>

void em_frontend_install(void);
/* EM_NEW_GAME=1 (em_new_game_switch.h): the movie service without the
 * startup flow, then the title's New Game handoff (em_game_install_new). */
void em_frontend_install_new_game(void);
void em_frontend_shutdown(void);
/* 001ADF00's 001AB790(001AC070) after a death: the title flow replaces the
 * game task in slot 0, from 001AC070 state 0 (D_00275BDC set: the menu with
 * the cursor on its second entry). 0, or -1. */
int em_frontend_install_001AC070(void);
/* 1 while the title flow holds slot 0 and its menu took input on the last
 * tick (*cursor = its cursor); test driver use (the level smoke). */
int em_frontend_title_menu(unsigned *cursor);
/* The title flow's 001AC070 state (+8: em_startup's major) and, in state 2,
 * 001AC480's sub-state (+9); 0 / 0 when the flow does not hold slot 0.
 * Test driver use (the level smoke's dmg_load). */
void em_frontend_title_state(unsigned *state, unsigned *sub);
int em_frontend_failed(void);
/* What the game reads of the frontend after New Game (the movie service of
 * 001AD360 step 1): {installed, the stored selector D_00275C78 (-1 none),
 * a movie open, failed}. tools/test_new_game_switch.py compares it. */
void em_frontend_service_state(int32_t out[4]);
/* The frontend's own host state (the startup flow, its screens and movie
 * player), which the game reads only through the service above; the test
 * leaves it out of the comparison. */
const void *em_frontend_host_state(size_t *size);

/* The game's movie requests: 001AD360 step 1 (S12a) stores the movie
 * selector D_00275C78 = 0 and then D_00821058 = 1; op0F (001B7A30) of Roger's
 * departure script 0x828A10 stores D_00275C78 = 1 (its record's +0x14) and
 * D_00821058 = 1. The main loop's blocking movie driver 00203350 serves the
 * request at step M (em_frame.c). The native frontend owns the movie player,
 * so the stores land here. Selectors 0 (E900.PSS, assets/startup/intro.mov)
 * and 1 (E001.PSS, assets/movies/e001.mov) are exported; any other selector,
 * or a D_00821058 value other than 1, is refused (-1, fail-stop).
 * em_frontend_movie_selector reads D_00275C78 back (-1 before any store). */
int em_frontend_movie_select(uint8_t selector); /* D_00275C78 */
int em_frontend_movie_request(uint8_t value);   /* D_00821058 */
int em_frontend_movie_selector(void);

#endif
