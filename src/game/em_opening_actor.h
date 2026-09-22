#ifndef EM_OPENING_ACTOR_H
#define EM_OPENING_ACTOR_H

#include "em_gfx.h"
#include <stdint.h>

/* Original AREA11 opening: bank98 clip1 player, clip2 Roger, and model6B
 * attached to Roger bone1. Each exported sample is one 60Hz engine tick
 * (0.5 animation frame); palettes already contain world translation.
 * half_tick zero selects source0.5, after the original bind-frame advance. */
#define EM_OPENING_ACTOR_COUNT 3u

/* Returns one only when every required mesh and pose track loaded. */
int em_opening_actor_init(EmGfx *gfx, const char *scene_dir);
void em_opening_actor_shutdown(EmGfx *gfx);
/* Called by the original clip-bind command, including opening replay with
 * already loaded resources. Does not reset the shared game RNG. */
void em_opening_actor_begin(void);
/* Advance both original face state machines exactly once per ordinary
 * opening tick. talk_mask is the 001FD950 speaker0/1 mailbox state. */
int em_opening_actor_tick(uint32_t half_tick, unsigned talk_mask);

/* The caller supplies the actor light rig before drawing this record.
 * No interpolation or looping: completion holds the final original pose.
 * Returned pointers remain owned by this module until shutdown. */
int em_opening_actor_record(unsigned index, uint32_t half_tick,
                           EmGfxMesh **mesh, const float **palette,
                           uint32_t *bone_count);

/* Convenience draw for callers that already supplied their lighting. */
void em_opening_actor_draw(EmGfx *gfx, const float *viewproj,
                          uint32_t half_tick);

#endif
