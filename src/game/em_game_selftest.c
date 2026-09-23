/* em_game_selftest.c — retired legacy gameplay self-tests.
 *
 * The env-gated EM_MOVE/DOOR/TRANSIT/SLIDER/LOCKED/WEAPON/ENEMY/PAUSE/
 * PICKUP/EXAMINE/SFX/CAMREGION/AIM/MELEE/DEATH/CINE_TEST scripts and the
 * EM_CAPTURE_AIM/DOOR/SUPPLY/EXAMINE/RISE/ORIENT/WALK/LOCKED scripted
 * captures exercised office/drawbridge scenes and legacy mechanics outside
 * the first level; many encoded fabricated behaviour. They were retired on
 * 2026-09-23 at the user's request (CLAUDE.md, "Tests"). First-level
 * coverage lives in the original-instruction reference tests (make test-*),
 * EM_STARTUP_TEST=newgame-control and the level smoke test.
 *
 * The empty hook stays until the scene coordinator chain releases em_game.c;
 * then its call site and the old flag parsing are deleted too. */
#include "game/em_game.h"
#include "game/em_game_internal.h"

void em_game_selftest_pre_frame(void) {}
