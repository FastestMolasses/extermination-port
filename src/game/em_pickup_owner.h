/* Original pickup actors: 0015AFA0/0015AE20 and 00219550, their state 0
 * (0015AC00, 00219550's) and their later states. Model initialization and
 * script workers are explicit inputs. Oracle:
 * tools/test_pickup_owner_reference.py. */
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

/* The item owners' state 0 over the record bytes it reads or writes
 * (0015AFA0's 0015AC00, byte-matched decomp src/func_0015AC00.c; 00219550's
 * state 0, NEARMISS src/func_00219550.c, the .s followed). The model binds,
 * 001C6380, the aura's 001F1110 and 001A2370 are workers on the record the
 * caller owns; +0x04 is the bind workers' (001B0FD0 / 001B1020 add 1). The
 * +0x30 store (0015AC00: &D_00275488; 00219550: the word at D_00275878 or
 * D_00275880 by D_00810700) is not modelled: no port code reads it. */
typedef struct {
    uint8_t status;      /* +0x00: 1 once bound */
    uint8_t subtype;     /* +0x03 */
    uint8_t mode;        /* +0x08: 3 once bound */
    uint8_t model;       /* +0x0D: the model id (0015AC00's scale switch) */
    uint16_t flags2;     /* +0x2E: 00219550 rewrites 3 -> 0x14 (+0x03 == 0, item 3 held) */
    float scale[3];      /* +0x60, +0x64, +0x68 (0015AC00) */
    float color[3];      /* +0x80, +0x84, +0x88 (0015AC00, (+0x03 & 0xF) == 1) */
} EmPickupState0;

typedef struct {
    void *context;
    /* 001B0FD0(self): *result its v0 (0 bound, 1 refused) */
    int (*bind_001B0FD0)(void *context, int32_t *result);
    /* 001B1020(self, a1, a2, a3): *result its v0 */
    int (*bind_001B1020)(void *context, uint32_t a1, int32_t a2, int32_t a3, int32_t *result);
    /* 001C6380(self) */
    int (*place_001C6380)(void *context);
    /* 001F1110(self, variant): 0015AC00's aura */
    int (*aura_001F1110)(void *context, int16_t variant);
    /* 001A2370(self, self + 0xD0): 00219550's cell */
    int (*cell_001A2370)(void *context);
} EmPickupState0Hooks;

/* 0015AC00(self): 0 bound (placed, +0x00 = 1, +0x08 = 3, 001F1110 run), 1
 * the bind refused (the scale and colour stores stay; nothing after the
 * bind), -1 a missing or failed worker. */
int em_pickup_owner_0015AC00(EmPickupState0 *r, const EmPickupState0Hooks *h);
/* 00219550 state 0 up to its 001C5570 child spawn (which the caller runs
 * next): `item3` = D_00810C64[3]. 0 done, 1 the 001B0FD0 bind refused (the
 * owner stays in state 0), -1. 001B1020's result is not read. */
int em_pickup_owner_00219550_state0(EmPickupState0 *r, uint8_t item3, const EmPickupState0Hooks *h);

typedef struct { uint8_t kind, index; } EmPickupStatusRequest;
/* 001B6EA0 and001C4720/4760/47A0. Item inventory mutation is the required
 * 001C40B0 worker; maps/keys are distinct wrapping byte arrays.
 * A low key index leaves any existing status request unchanged. */
int em_pickup_owner_take(const EmPickupOwner *owner, uint8_t maps[256],
    uint8_t keys[256], EmPickupStatusRequest *request,
    int (*add_item)(void *, uint16_t type, int amount), void *context);

#endif
