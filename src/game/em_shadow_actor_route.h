/* em_shadow_actor_route.h - the player's blob shadow while it stands on an
 * actor (player +0x214 != 0): 0015BF90, 001F9100, 001F8D30
 * (docs/SHADOW_ACTOR_ROUTE.md).
 *
 * 0015C160 (the player post-step, em_shadow_original_route_0015C160) calls
 * 001DA6A0, the projected shadow, only while the player word +0x214 is 0.
 * When the player stands on an actor (the elevator car, a crate, the truck)
 * +0x214 names that actor and 0015C160 calls 0015BF90(player) instead.
 * 0015BF90 does not project the player's mesh. It places one textured quad
 * (a decal) on the surface under the player:
 *
 *   0015BF90(p)  the start point is (p+B0 x, min(foot y), p+B0 z): the
 *                lower of the two node records the words p+154 and p+158
 *                name (their float +C4). Normal play: a segment 100 units
 *                straight down through 0019A570 mask 6 (cells and grid);
 *                on a hit the decal goes at the hit point with the hit
 *                record's normal. With 0x70003B8D != 0 and p+1F0 == 0x41
 *                it goes one unit below the start point, normal +y, with
 *                no query. p+1F0 == 0x19 draws nothing.
 *   001F9100(owner, point, normal, size)
 *                001F8D30 with the facing D_0025DAF0, the colour
 *                D_0025DAE0, half extents (size, size) and fade range 30.
 *   001F8D30(owner, point, normal, facing, colour, half_w, half_h, range)
 *                builds the decal matrix (identity, turned about z by
 *                atan2(facing.z, facing.x), times the look-at rows of the
 *                normal, moved to the point), dims the colour by the height
 *                of the owner above the point (1 - |owner.y - point.y| /
 *                range, clamped to [0.1, 1]), transforms the four corners
 *                (+-half_w, +-half_h, 0), projects the first three through
 *                the camera 0x70003AC0 into 0x70003600..0x7000362C, and
 *                submits the quad through 001CE300 unless those three wind
 *                clockwise on screen.
 *
 * Translations of the original instructions (every routine is NEARMISS or
 * hand-written in the decomp, so the .s was read; the doc lists where the
 * readable C is wrong), not models of them.
 *
 * Reused translations, called directly (not re-translated here):
 *   001029C0 identity, 00102A60 z turn, 00102918 translate
 *            (em_owner_services_original.c),
 *   0011DF78 fabsf (em_sdk_math_original.c),
 *   00128250 float -> unsigned (em_stream_lanes_original.c).
 * Translated here (two SDK VU0 leaves no module exports in the EE float
 * model): 001026D0 (4x4 product) and 00102900 (vector times scalar), and the
 * quadword copy 00102948.
 *
 * Every other original callee is a worker (EmShadowActorRouteWorkers). A
 * missing worker, table, scratch word or player record is a fault: each
 * entry point checks everything it can reach and returns -1 before its first
 * write. A worker that returns a negative value, or a VU0 form the EE float
 * model refuses, is a fault too: the routine stops at once and returns -1,
 * leaving the writes made before it, as the original order leaves them.
 *
 * Arithmetic: every COP1 and VU0 macro operation goes through em_ee_float.h
 * (docs/EE_FLOAT_MODEL.md), on raw bit patterns.
 *
 * Oracle: tools/test_shadow_actor_route_reference.py executes the original
 * instructions of the three routines and their SDK leaves from the user's
 * pinned ELF (the four worker callees hooked and recorded) over synthetic
 * records and over the player of every route beat's captured RAM, and
 * compares the scratchpad words, the worker calls and their arguments. */
#ifndef EM_SHADOW_ACTOR_ROUTE_H
#define EM_SHADOW_ACTOR_ROUTE_H

#include <stddef.h>
#include <stdint.h>

#include "game/em_player_floor.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_SHADOW_ACTOR_ROUTE_ELF_SIZE 1532624u   /* SCUS_971.12 */

/* D_0025DAE0 and D_0025DAF0: the two data quadwords 001F9100 passes (raw
 * words from the user's ELF; only 001F9100 and its siblings 001F9140 /
 * 001F9180 / 001F91C0 read them, nothing writes them). */
typedef struct EmShadowActorRouteTables {
    uint32_t colour[4];   /* D_0025DAE0: r, g, b, a (floats) */
    uint32_t facing[4];   /* D_0025DAF0: the decal's in-plane direction */
} EmShadowActorRouteTables;

/* 0, or -1 when `elf` is not the 1532624-byte boot ELF. */
int em_shadow_actor_route_load_tables(const uint8_t *elf, size_t size,
                                      EmShadowActorRouteTables *out);

/* The scratchpad words the routines read and write, as raw words. The
 * binder points each at the ONE scratchpad image the other translations
 * share (0x700038A0 and 0x70003A20 are also EmPlayerLandScratch's;
 * 0x70003600.. is also EmEffectOriginalGlobals.spad3600's). */
typedef struct EmShadowActorRouteScratch {
    uint32_t *s38A0;        /* 0x700038A0..0x700038AC (4 words): the point */
    uint32_t *s38B0;        /* 0x700038B0..0x700038BC (4 words): the segment end, then the normal */
    uint32_t *s3A20;        /* 0x70003A20: the lower foot y */
    uint32_t *s3600;        /* 0x70003600..0x7000362C (12 words): P0, P1, P2 (x, y, z, depth; 12.4) */
    const uint8_t *s3B8D;   /* 0x70003B8D (read) */
    const uint32_t *s3AC0;  /* 0x70003AC0..0x70003AFC (16 words, read): the camera rows */
} EmShadowActorRouteScratch;

/* The original callees that are not translated here. Each returns 0, or a
 * negative value on a fault. */
typedef struct EmShadowActorRouteWorkers {
    void *context;
    /* The float at +0xC4 of the node record the player word at `slot`
     * (0x154 or 0x158) names; `word` is that player word. (+0x154 and
     * +0x158 are the node slots 17 and 18 of the table at player+0x110.) */
    int (*node_c4)(void *context, uint32_t slot, uint32_t word, uint32_t *value);
    /* 0019A570(from, to, mask, id) with from = 0x700038A0, to = 0x700038B0:
     * *result is its v0 (0 = no hit). On a hit (only then) the worker also
     * returns what 0015BF90 reads right after the call: point = the four
     * words at 0x700031B0 (the query writes x, y, z; the fourth word is
     * whatever the scratchpad held) and normal = the words +0x24, +0x28,
     * +0x2C of the record *0x700031D0 names. */
    int (*segment)(void *context, const uint32_t from[4], const uint32_t to[4], int mask, int id,
                   int32_t *result, uint32_t point[4], uint32_t normal[3]);
    /* 0011E620(y, x): atan2f, raw bits. */
    int (*atan2)(void *context, uint32_t y, uint32_t x, uint32_t *result);
    /* 001CD390(out, v): the look-at rows of v into the 4x4 `out` (it also
     * writes 0x70003600..0x7000363F, which 001F8D30 then overwrites up to
     * 0x7000362F). */
    int (*look_at)(void *context, uint32_t out[16], const uint32_t v[4]);
    /* 001CE300(tag, corners, tex0, rgba): the quad's GS packet (clip
     * 001CF470, packet 001CB5F0 / 001CB950 / 001CB900). corners are the
     * four world corners (x, y, z, w words each) in the order 001F8D30
     * builds them. */
    int (*submit)(void *context, int32_t tag, const uint32_t corners[16], uint64_t tex0,
                  uint32_t rgba);
} EmShadowActorRouteWorkers;

enum {
    EM_SHADOW_ACTOR_ROUTE_FAULT_NONE = 0,
    EM_SHADOW_ACTOR_ROUTE_FAULT_UNBOUND = 1,   /* a worker, table, scratch word or record is missing */
    EM_SHADOW_ACTOR_ROUTE_FAULT_WORKER = 2,    /* a worker returned a negative value */
    EM_SHADOW_ACTOR_ROUTE_FAULT_FLOAT = 3      /* the EE float model refused a VU0 form */
};

typedef struct EmShadowActorRouteFault {
    uint32_t address;   /* the original function or callee address */
    int32_t code;       /* EM_SHADOW_ACTOR_ROUTE_FAULT_* */
} EmShadowActorRouteFault;

typedef struct EmShadowActorRoute {
    const EmShadowActorRouteTables *tables;
    EmShadowActorRouteScratch scratch;
    const EmShadowActorRouteWorkers *workers;
    EmShadowActorRouteFault fault;   /* the first fault; a latched fault refuses every call */
} EmShadowActorRoute;

/* 0015BF90(player). Reads the player bytes +0xB0..+0xBF, +0x154, +0x158
 * and +0x1F0 (writes none). Returns 0, or -1 on a fault. */
int em_shadow_actor_route_0015BF90(EmShadowActorRoute *route, const EmPlayerLiveActor *player);

/* 001F9100(owner, point, normal, f12). `owner` is the a0 quadword (only
 * owner[1], the y, is read); point/normal are the a1/a2 quadwords; f12 is
 * the half extent, raw bits. Returns 0, or -1. */
int em_shadow_actor_route_001F9100(EmShadowActorRoute *route, const uint32_t owner[4],
                                   const uint32_t point[4], const uint32_t normal[4],
                                   uint32_t f12);

/* 001F8D30(owner, point, normal, facing, colour, f12, f13, f14): half_w,
 * half_h and the fade range as raw bits. Returns 0, or -1. */
int em_shadow_actor_route_001F8D30(EmShadowActorRoute *route, const uint32_t owner[4],
                                   const uint32_t point[4], const uint32_t normal[4],
                                   const uint32_t facing[4], const uint32_t colour[4],
                                   uint32_t f12, uint32_t f13, uint32_t f14);

#ifdef __cplusplus
}
#endif

#endif /* EM_SHADOW_ACTOR_ROUTE_H */
