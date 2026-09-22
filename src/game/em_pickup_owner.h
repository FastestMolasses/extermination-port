/* Original post-initialization pickup actors: 0015AFA0/0015AE20 and
 * 00219550. Model initialization and script workers are explicit inputs. */
#ifndef EM_PICKUP_OWNER_H
#define EM_PICKUP_OWNER_H

#include <stdint.h>

typedef struct {
    uint32_t callback;
    uint16_t item_type;
    uint8_t uid, status, class_flags, subtype;
    uint8_t lifecycle, phase, armed, child_status, has_child, freed;
} EmPickupOwner;

typedef enum {
    EM_PICKUP_OWNER_AURA,
    EM_PICKUP_OWNER_PUBLISH,
    EM_PICKUP_OWNER_DRAW,
    EM_PICKUP_OWNER_TAKE_SOUND,
    EM_PICKUP_OWNER_PERSIST,
    EM_PICKUP_OWNER_STOP_CHILD,
    EM_PICKUP_OWNER_FREE
} EmPickupOwnerEvent;

typedef struct {
    void *context;
    /* clip is zero for the short program; otherwise patch its op0A
     * record before starting. A start does not tick the program. */
    int (*script_start)(void *, uint32_t entry, uint16_t clip);
    /* -1 missing/failed worker,0 waiting,1 finished. */
    int (*script_tick)(void *);
    /* PUBLISH returns actual visibility0/1; other events require1.
     * TAKE_SOUND is cue0194 through FBD50(owner,cue,0,300).
     * PERSIST receives the original one-byte UID, including zero. */
    int (*event)(void *, EmPickupOwnerEvent, uint32_t argument);
} EmPickupOwnerHooks;

/* One ordinary actor update. Return1 while allocated,0 after free,
 * -1 unsupported callback/uninitialized state/failed required worker.
 * All finite float thresholds follow the original two separate ADD.S. */
int em_pickup_owner_tick(EmPickupOwner *owner, float item_y, float player_y,
                         uint8_t player_action, uint8_t no_grab,
                         uint8_t scripted_frame, const EmPickupOwnerHooks *hooks);

typedef struct { uint8_t kind, index; } EmPickupStatusRequest;
/* 001B6EA0 and001C4720/4760/47A0. Item inventory mutation is the required
 * 001C40B0 worker; maps/keys are distinct wrapping byte arrays.
 * A low key index leaves any existing status request unchanged. */
int em_pickup_owner_take(const EmPickupOwner *owner, uint8_t maps[256],
    uint8_t keys[256], EmPickupStatusRequest *request,
    int (*add_item)(void *, uint16_t type, int amount), void *context);

#endif
