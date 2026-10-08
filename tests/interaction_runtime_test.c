#include "game/em_interaction_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The shared owner token and the scripted frame over a player record. The
 * player stage (0015B130's admission, 00183090 + 001C64F0 over the record,
 * 0015B530's 00182DF0) is this fixture's boundary: it is executed against
 * the original by test_player_stage_workers_reference and in play by the
 * level smoke. Here the fixture writes what the stage would (3B8F, +200). */
typedef struct {
    int cameras, events, records;
    uint8_t record[0x320];
    int unbound;
} Host;

static int frame_event(void *context, EmInteractionFrameEvent event)
{
    Host *h = context;
    (void)event;
    h->events++;
    return 1;
}
static int camera(void *context)
{
    Host *h = context;
    h->cameras++;
    return 1;
}
static uint8_t *player_record(void *context)
{
    Host *h = context;
    h->records++;
    return h->unbound ? NULL : h->record;
}
static void word(unsigned char *record, unsigned offset, unsigned value)
{
    for (unsigned i = 0; i < 4; i++)
        record[offset + i] = (unsigned char)(value >> (8 * i));
}
static uint32_t u32(const uint8_t *p, unsigned at)
{
    uint32_t v;
    memcpy(&v, p + at, 4);
    return v;
}
static uint16_t u16(const uint8_t *p, unsigned at)
{
    uint16_t v;
    memcpy(&v, p + at, 2);
    return v;
}

int main(void)
{
    EmInteractionFrame frame = {0};
    Host host = {0};
    EmInteractionRuntimeHooks hooks = {&host, frame_event, camera, player_record};
    EmInteractionRuntime runtime;
    int panel = 0, elevator = 0;
    assert(em_interaction_runtime_init(&runtime, &frame, &hooks));
    assert(em_interaction_runtime_claim(&runtime, &panel));
    assert(frame.selector == 3 && !em_interaction_runtime_claim(&runtime, &elevator));
    /* A foreign token neither requests nor faults. */
    assert(!em_interaction_runtime_animation_start(&runtime, &elevator, 0x47, 1, 1));
    assert(em_interaction_runtime_animation_done(&runtime, &elevator) == -1);
    assert(!runtime.failed && em_interaction_runtime_owner(&runtime) == &panel);
    unsigned char record[64] = {0};
    word(record, 0, 7);
    word(record, 8, 2);
    EmScript script = {0};
    assert(em_interaction_runtime_frame(&runtime, &panel, &script, record) == EM_SCRIPT_WAIT);
    assert(frame.selector == 2 && frame.camera_top == 1 && script.phase == 1);
    assert(em_interaction_runtime_camera_owned(&runtime));
    /* The frame waits for the player stage's admission (00182D70's 3B8F). */
    assert(em_interaction_runtime_frame(&runtime, &panel, &script, record) == EM_SCRIPT_WAIT);
    frame.player_ready = 1;
    assert(em_interaction_runtime_frame(&runtime, &panel, &script, record) == EM_SCRIPT_ADVANCE);
    assert(frame.ready == 1 && script.skip_phase == 1);
    assert(em_interaction_runtime_camera_retarget(&runtime, &panel));
    assert(host.cameras == 1);

    /* 001B9A00 sub 0: +1F2 = clip, +1F8 = the command's blend, +1F4 = 1.0;
     * nothing else of the record changes. */
    memset(host.record, 0xA5, sizeof host.record);
    uint8_t before[0x320];
    memcpy(before, host.record, sizeof before);
    assert(em_interaction_runtime_animation_start(&runtime, &panel, 0x15C, 1, 0));
    assert(u16(host.record, 0x1F2) == 0x15C && u32(host.record, 0x1F8) == 0 &&
           u32(host.record, 0x1F4) == 0x3F800000u);
    for (unsigned i = 0; i < sizeof before; ++i)
        if (i < 0x1F2 || i >= 0x1FC) assert(host.record[i] == before[i]);
    /* 001B9A00 sub 3 reads +200 & 0x1000 (the stage's 001C64F0 result). */
    word(host.record, 0x200, 0);
    assert(em_interaction_runtime_animation_done(&runtime, &panel) == 0);
    word(host.record, 0x200, 0x8000);
    assert(em_interaction_runtime_animation_done(&runtime, &panel) == 0);
    word(host.record, 0x200, 0x1000);
    assert(em_interaction_runtime_animation_done(&runtime, &panel) == 1);
    word(host.record, 0x200, 0x3000);
    assert(em_interaction_runtime_animation_done(&runtime, &panel) == 1);
    assert(em_interaction_runtime_animation_start(&runtime, &panel, 0x47, 1, 1));
    assert(u16(host.record, 0x1F2) == 0x47 && u32(host.record, 0x1F8) == 0x3F800000u);

    /* The frame's close clears the selector; the token holds until the
     * stage's 00182DF0 ends it, and no other owner can claim before. */
    word(record, 8, 4);
    assert(em_interaction_runtime_frame(&runtime, &panel, &script, record) == EM_SCRIPT_ADVANCE);
    assert(!frame.selector && frame.player_ready == 1 && !frame.ready);
    assert(em_interaction_runtime_owner(&runtime) == &panel);
    assert(!em_interaction_runtime_claim(&runtime, &elevator));
    frame.player_ready = 0;
    assert(em_interaction_runtime_release(&runtime));
    assert(!em_interaction_runtime_owner(&runtime) && !em_interaction_runtime_release(&runtime));
    assert(em_interaction_runtime_animation_done(&runtime, &panel) == -1);

    /* Faults: a request before the player is taken, a rate sub 0 does not
     * write, and an unbound record each latch; a latched runtime refuses
     * everything. */
    assert(em_interaction_runtime_claim(&runtime, &elevator));
    assert(!em_interaction_runtime_animation_start(&runtime, &elevator, 0x47, 1, 1) && runtime.failed);
    assert(!em_interaction_runtime_owns(&runtime, &elevator) && !em_interaction_runtime_release(&runtime));
    memset(&frame, 0, sizeof frame);
    assert(em_interaction_runtime_init(&runtime, &frame, &hooks));
    assert(em_interaction_runtime_claim(&runtime, &elevator));
    frame.player_ready = 1;
    assert(!em_interaction_runtime_animation_start(&runtime, &elevator, 0x47, 0.5f, 1) && runtime.failed);
    memset(&frame, 0, sizeof frame);
    assert(em_interaction_runtime_init(&runtime, &frame, &hooks));
    assert(em_interaction_runtime_claim(&runtime, &elevator));
    frame.player_ready = 1;
    host.unbound = 1;
    assert(!em_interaction_runtime_animation_start(&runtime, &elevator, 0x47, 1, 1) && runtime.failed);
    memset(&frame, 0, sizeof frame);
    assert(em_interaction_runtime_init(&runtime, &frame, &hooks));
    assert(em_interaction_runtime_claim(&runtime, &elevator));
    assert(em_interaction_runtime_animation_done(&runtime, &elevator) == -1 && runtime.failed);
    host.unbound = 0;

    /* A script owner whose op07 opened the frame claims without writing it. */
    memset(&frame, 0, sizeof frame);
    assert(em_interaction_runtime_init(&runtime, &frame, &hooks));
    assert(!em_interaction_runtime_claim_scripted(&runtime, &panel));   /* no selector */
    frame.selector = 2;
    assert(em_interaction_runtime_claim_scripted(&runtime, &panel) && frame.selector == 2);
    assert(!em_interaction_runtime_claim_scripted(&runtime, &elevator));
    assert(em_interaction_runtime_release(&runtime) && !runtime.owner);
    puts("Shared interaction token, frame, 001B9A00 sub 0 / sub 3 on the player record, release "
         "and faults: PASS");
    return 0;
}
