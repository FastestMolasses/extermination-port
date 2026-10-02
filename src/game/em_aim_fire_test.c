#include "game/em_aim_fire_test.h"
#include "em_input.h"
#include "game/em_player.h"
#include "game/em_scene_bindings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct { unsigned tick, stance_seen, shot_seen; int key, fire; } test;

static void key(int code, int down)
{
    EmEvent e = {0};
    e.type = down ? EM_EVENT_KEY_DOWN : EM_EVENT_KEY_UP;
    e.key = code;
    em_input_handle_event(&e);
}

void em_aim_fire_test_begin(void)
{
    memset(&test, 0, sizeof test);
    const char *mode = getenv("EM_AIM_FIRE_TEST");
    /* r1 / r2: aim, fire, release; r1hold / r2hold: aim and release only
     * (the AIM captures aim_00 / aim_01: draw, hold, holster). */
    test.key = mode && strncmp(mode, "r2", 2) == 0 ? '3' : 'e';
    test.fire = !(mode && strstr(mode, "hold"));
    fprintf(stderr, "aim/fire test: begin %s; settle, aim, %srelease\n",
            test.key == '3' ? "R2" : "R1", test.fire ? "fire, " : "");
}

void em_aim_fire_test_before_frame(void)
{
    /* Let the original run-stop finish before drawing the weapon. These
     * are test inputs, not substitutions for any game state or timer. */
    if (test.tick == 60) key(test.key, 1);
    if (test.fire && test.tick == 100) key('l', 1);
    if (test.fire && test.tick == 102) key('l', 0);
    if (test.tick == 150) key(test.key, 0);
}

int em_aim_fire_test_after_frame(void)
{
    const EmPlayerLiveActor *a = player_states_actor();
    unsigned state = em_live_u8(a, 5), sub = em_live_u8(a, 7);
    test.stance_seen |= state >= 0x1D && state <= 0x20;
    test.shot_seen |= test.stance_seen && (sub == 10 || sub == 11 ||
                                        sub == 20 || sub == 21 || sub == 22 ||
                                        sub == 30 || sub == 31);
    if (test.tick >= 59 && test.tick <= 155)
        fprintf(stderr, "aim/fire sample: tick=%u state=%02x/%02x/%02x action=%02x "
                "latch=%u held=%04x pressed=%04x\n", test.tick, state,
                em_live_u8(a, 6), sub, em_live_u8(a, 0x1F0), em_live_u8(a, 0x274),
                em_scene_state()->d810E70, em_scene_state()->d810E74);
    if (++test.tick < 240) return 0;
    key(test.key, 0);
    key('l', 0);
    if (!test.stance_seen || (test.fire && !test.shot_seen) || (!test.fire && test.shot_seen) || state != 0) {
        fprintf(stderr, "aim/fire test: FAIL stance=%u shot=%u final=%02x\n",
                test.stance_seen, test.shot_seen, state);
        return -1;
    }
    fprintf(stderr, "aim/fire test: PASS input path; oracle/capture equality is checked separately\n");
    return 1;
}
