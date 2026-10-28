/* Oracle fixtures only; no runtime or asset data. */
#include "game/em_sfx_bank.h"
#include <stdint.h>
#include <string.h>

static struct { EmSfxDriver driver; } s;
static uint32_t commands[48][4];
static unsigned command_count;

static void command(void *ctx, int cmd, int voice, uint32_t a, uint32_t b)
{
    (void)ctx;
    if (command_count < 48) {
        uint32_t *p = commands[command_count++];
        p[0] = (uint32_t)cmd; p[1] = (uint32_t)voice; p[2] = a; p[3] = b;
    }
}

static void queue_push(void *ctx, int cmd, int voice, uint32_t a, uint32_t b)
{
    command(ctx, cmd, voice, a, b);
}

/* Generated from the protected port sources, read-only. Both actual
 * forwarding bodies execute before the canonical driver routine. */
int em_sfx_stop_track(int track, int hard);
#include "scene-stop.inc"

/* Voice rows: state, owner, release, kind. Only these original fields
 * change in 0011A070. Hardware command application stays deferred. */
int shared_stop(int handle, uint16_t *tracks, uint16_t *voices,
                uint64_t *key_off, uint32_t *out, uint32_t *count)
{
    memset(&s.driver, 0, sizeof s.driver);
    s.driver.deferred = 1;
    s.driver.key_off = *key_off;
    command_count = 0;
    for (int i = 0; i < 48; ++i) {
        s.driver.tracks[i].allocated = (uint8_t)tracks[i];
        s.driver.voices[i].state = voices[4*i];
        s.driver.voices[i].owner = voices[4*i+1];
        s.driver.voices[i].release = voices[4*i+2];
        s.driver.voices[i].kind = voices[4*i+3];
    }
    int rc = ambient_0011A070(NULL, handle);
    for (int i = 0; i < 48; ++i) {
        tracks[i] = s.driver.tracks[i].allocated;
        voices[4*i] = s.driver.voices[i].state;
        voices[4*i+1] = s.driver.voices[i].owner;
        voices[4*i+2] = s.driver.voices[i].release;
        voices[4*i+3] = s.driver.voices[i].kind;
    }
    *key_off = s.driver.key_off;
    *count = command_count;
    memcpy(out, commands, command_count * sizeof commands[0]);
    return rc;
}

static void stop(void *ctx, int track)
{
    (void)ctx;
    command(0, 0x11a070, track, 0, 0);
}

void shared_release(int32_t *handle, int32_t *requested,
                    uint32_t *out, uint32_t *count)
{
    EmSfxLoopOps ops = {0};
    ops.stop = stop;
    command_count = 0;
    em_sfx_service_release(&ops, requested, handle);
    *count = command_count;
    memcpy(out, commands, command_count * sizeof commands[0]);
}
