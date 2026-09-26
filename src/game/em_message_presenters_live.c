/* em_message_presenters_live.c - see em_message_presenters_live.h. */
#include "game/em_message_presenters_live.h"

#include <stdio.h>
#include <string.h>

#include "game/em_census_standins.h"
#include "game/em_message_live.h"
#include "game/em_message_presenter_rest.h"
#include "game/em_sfx.h"

/* The cue bank's address in all 18 captures (a heap load address;
 * docs/MESSAGE_PRESENTER_REST.md finding 7). */
#define CUE_BANK_ADDRESS 0x011739C0u

static struct {
    int bound;
    EmMprData data;
    EmCsPresenters presenters;
    EmMprLineDraw walker;
} P;

/* 001FD0E0's 001FC9B0: the service's reset (its block is the one the
 * presenter was given). */
static int w_reset(void *context)
{
    (void)context;
    return em_message_live_reset() == 0;
}

/* 001FDDB0's gate sound 001FB9F0(id, 0x1000, 0x1000, 0x1000): the
 * non-positional submit, em_sfx_play (the binding of the status cues).
 * Other request words have no live binding. */
static int w_sound(void *context, int32_t id, int32_t a1, int32_t a2, int32_t a3)
{
    (void)context;
    if (id < 0 || a1 != 0x1000 || a2 != 0x1000 || a3 != 0x1000) return -1;
    em_sfx_play((unsigned)id);
    return 0;
}

int em_message_presenters_live_install(const char *path)
{
    em_message_presenters_live_shutdown();
    EmMessageDraw *draw = em_message_live_draw();
    EmMessageBlock *block = em_message_live_block();
    if (!draw || !block || !em_mpr_data_load(&P.data, path, CUE_BANK_ADDRESS)) {
        fprintf(stderr, "message presenters: %s missing or malformed (or no message service); "
                        "the first mode-3 / mode-4 request faults (run "
                        "tools/export_message_data.py)\n", path ? path : "(null)");
        return 0;
    }
    EmCsMode3Workers mode3 = {&P.walker, w_reset, em_mpr_worker_line_draw};
    if (!em_mpr_line_draw_init(&P.walker, draw, &P.data.cs, NULL, w_sound) ||
        !em_cs_presenters_init(&P.presenters, draw, &P.data.cs, &mode3,
                               (int32_t *)(void *)block->presenter_78)) {
        em_mpr_data_free(&P.data);
        fprintf(stderr, "message presenters: bind failed\n");
        return 0;
    }
    const EmMessageLivePresenters hooks = {&P.presenters, em_cs_worker_mode3_present,
                                           em_cs_worker_help_draw, em_cs_worker_record_setup,
                                           em_cs_worker_record_draw};
    em_message_live_set_presenters(&hooks);
    P.bound = 1;
    return 1;
}

void em_message_presenters_live_shutdown(void)
{
    if (P.bound) {
        em_message_live_set_presenters(NULL);
        em_mpr_data_free(&P.data);
    }
    memset(&P, 0, sizeof P);
}
