#ifndef EM_SNOW_RUNTIME_H
#define EM_SNOW_RUNTIME_H
/* em_snow_runtime.h - the AREA11 weather actor 001E55F0 bound live
 * (docs/SNOW_PARTICLES.md): em_weather's controller over the node's own
 * state, 001E67C0's tiles (em_snow.c) with its fog programmer calls on the
 * render context, each tile's draw request 001CFAE0 / 001CFFE0 into the
 * context's channel 3 and 001E55F0's close of that list
 * (em_weather_packets). The frame close's 001E0D70 CALLs the list into the
 * chain page, whose consumer runs the snow program D_00233800 on it
 * (em_chain_page_live, docs/CHAIN_PAGE.md): the snow draws only from the
 * original packets. */
#include <stdint.h>

#include "game/em_weather.h"

void em_snow_runtime_clear(void);
/* The scene's `weather <flags> <config> [<texture>]` line: the exported
 * descriptor, lookup and row tables (tools/export_snow.py; the texture
 * token of an older manifest is not read: the page textures hold it).
 * Only the recovered snow branch (flags & 0x0E000070 == 0x10) loads. */
int em_snow_runtime_load(const char *scene_dir, const char *config, unsigned flags);
/* One 001E55F0 behaviour call of a weather actor whose own state (+0x1F0
 * block and +4 byte) is *weather (a fresh actor passes a zeroed EmWeather:
 * state 0 seeds). `transition` is D_008106B8 and `fade_state` the
 * D_0028A9A0 halfword, read by the original at the end of state 1 (== 2 and
 * == 2 -> state 3). Returns 1 when the call is state 2/3, where the original
 * frees the actor (001AFC10) and draws nothing: the caller frees its node;
 * 0 after a call that drew or did not draw; -1 on a fault (the render
 * context refused a fog programmer call or a packet write, or the SDK
 * VU0 scale 00102900 refused a form): the caller fail-stops. */
int em_snow_runtime_tick_actor(EmWeather *weather, const float eye[3], unsigned selector,
                               unsigned transition, unsigned fade_state);

/* The level smoke's view (the tick log's "snow"): the last list a drawing
 * call closed. */
typedef struct {
    uint32_t frame;          /* em_frame_counter() of that call            */
    uint32_t lists;          /* lists closed since the load                */
    uint32_t start;          /* the list's first packet (context +0x2520)  */
    uint32_t tiles;          /* 001CFFE0 requests in it                    */
    uint32_t p3_digest;      /* FNV-1a of tile 0's packet-3 data (0x100)   */
    uint32_t p3_uniform;     /* 1: every tile's packet 3 equals tile 0's   */
} EmSnowRuntimeLog;
void em_snow_runtime_log(EmSnowRuntimeLog *out);

#endif
