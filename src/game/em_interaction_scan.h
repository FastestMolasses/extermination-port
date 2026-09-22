/* Original 00184BA0 single-winner use scan and AREA11 elevator gate.
 * This module owns neither input dispatch nor the selected owner's script. */
#ifndef EM_INTERACTION_SCAN_H
#define EM_INTERACTION_SCAN_H

#include <stddef.h>
#include <stdint.h>

enum { EM_INTERACTION_CAPACITY = 32 };

typedef struct {
    uint8_t selector;       /* scratchpad3B8D: ordinary player control is0 */
    int16_t fade_wait;      /* 0028A9A0 */
    uint8_t inhibited;      /* 008106EF */
    float score;           /* scratchpad3B98, shared across candidate calls */
} EmInteractionScanState;

typedef struct {
    void *owner;           /* canonical owner, original walked actor+14 */
    uint8_t status;        /* owner+0, bit0 must be set */
    uint8_t class_flags;   /* owner+2, bit7 must be set */
    uint8_t *armed;        /* live owner+B; winner overwrites this with4 */
} EmInteractionCandidate;

/* Return0 rejects,2 immediately wins, and any other nonzero result
 * competes using *score. A predicate may modify score before rejecting,
 * or accept without modifying it (original selector5 does the latter).
 * Candidates must therefore be evaluated sequentially, in published order. */
typedef int (*EmInteractionPredicate)(void *context,
                                     const EmInteractionCandidate *candidate,
                                     float *score);

/* Invoke only on the configured Use edge, at the original player action
 * dispatch point. List order is 001B1DE0's reverse publication order from
 * the preceding frame; this helper does not sort or collect objects.
 * Returns1 when an owner is armed,0 when none wins,-1 for invalid host
 * bindings/count. winner_index is SIZE_MAX when no owner wins.
 * Gated calls retain score. An ungated scan resets score even when empty.
 * Original valid lists contain at most32 entries; invalid negative counts
 * are not reproduced as an unbounded native memory walk. */
int em_interaction_scan(EmInteractionScanState *state,
                        const EmInteractionCandidate *candidates, size_t count,
                        EmInteractionPredicate predicate, void *context,
                        size_t *winner_index);

/* The class4/selector1 branch of00183EF0 used by00827B10. descriptor is
 * owner+30's six original floats: pointXYZ, planar radius, height, yaw.
 * It is NOT the model origin. The owner initializer changes pointY with
 * the elevator's190/230 floor selection; power is checked by its script.
 * Outer active/interactive/armed gates belong to em_interaction_scan.
 * Finite authored coordinates and wrapped player yaw are required.
 * Score is written after the radius test, before height/facing tests. */
int em_interaction_elevator_candidate(const float descriptor[6],
                                      const float player[3], float player_yaw,
                                      uint8_t player_action, float *score);

/* Local exporter reads these19 original SDK coefficients at0026C5D8.
 * No generated approximation or platform atan2 is substituted. */
typedef struct { float atan_low[4], atan_high[4], atan_coefficients[11]; } EmInteractionMath;

/* Finite-input numerical0011C4C8/0011DBB8 path, with original SDK tables.
 * The0011E620 zero-vector wrapper's error callback/errno are not modeled;
 * its ordinary numerical(+0,+0) result is+0. */
float em_interaction_sdk_atan2(const EmInteractionMath *math,float y,float x);

typedef struct {
    uintptr_t identity;    /* stable canonical token used by ray hits */
    uint8_t class_flags, subtype, selector;
    uint32_t callback;
    float position[3], angles[3], descriptor[2];
} EmInteractionPickup;

typedef struct {
    float position[3], yaw;
    uint8_t action;
    float view_target[3]; /* 008105E0, only used by special action2D */
} EmInteractionPlayer;

typedef struct {
    int hit;
    uint16_t flags;
    uint32_t kind;
    uintptr_t owner;
} EmInteractionRayHit;

/* Return1 for a valid query,0 for an unavailable host service. mode is
 * the original0019A910 family value6. Its material/owner result matters. */
typedef int (*EmInteractionRaycast)(void *context,const float from[4],
                                   const float to[4],unsigned mode,
                                   EmInteractionRayHit *hit);

/* Original selector3/4 item path, including the action2D/class7 override.
 * Returns0/1/2 as183EF0, or -1 for missing/unsupported host bindings.
 * The host predicate must treat -1 as a fatal host error; do not forward
 * it to em_interaction_scan (the original scan treats any nonzero as a
 * candidate). Only finite authored geometry and wrapped yaw are supported. */
int em_interaction_pickup_candidate(const EmInteractionPickup *pickup,
                                    const EmInteractionPlayer *player,
                                    const EmInteractionMath *math,
                                    EmInteractionRaycast raycast,void *context,
                                    float *score);

/* Original001B1630 camera cone/range publication gate. Invoke during
 * owner update, before the camera update, using008105D0 and00810600.
 * This is independent of the later player's per-object eligibility. */
int em_interaction_visible(const float position[3],const float camera_anchor[3],
                            const float camera_forward[3]);

typedef struct {
    EmInteractionCandidate pending[EM_INTERACTION_CAPACITY];
    EmInteractionCandidate active[EM_INTERACTION_CAPACITY];
    size_t pending_count, active_count;
} EmInteractionList;

/* Push only when that owner's real update invokes B1B70 and bit80 is set.
 * Keep original active-walker order. Publication reverses the accepted
 * first32 entries, then clears pending. It runs at frame close-out. */
void em_interaction_list_push(EmInteractionList *list,
                              const EmInteractionCandidate *candidate);
void em_interaction_list_publish(EmInteractionList *list);

#endif
