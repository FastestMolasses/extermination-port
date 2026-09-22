/* Original door kickoff geometry/script parameters (001BBE40) and destination
 * commit (001BC150). These workers do not invent room loading or lock UI. */
#ifndef EM_DOOR_TRANSIT_H
#define EM_DOOR_TRANSIT_H

#include "game/em_door_original.h"
#include "game/em_interaction_scan.h"

typedef struct {
    uint32_t script_entry;
    uint16_t side, player_clip, door_clip, sound;
    float wait_ticks, player_yaw, position[4];
    int locked;
} EmDoorTransitPlan;

/* Pure geometry/patch selection for a finite normalized owner yaw. */
int em_door_transit_prepare(EmDoorTransitPlan *, const float origin[3], float yaw,
    const float player_position[3], const uint16_t sounds[2], int locked,
    const EmInteractionMath *math);

typedef struct {
    void *context;
    /* Patch DC14/DC54/DC8C/DC58 for an ordinary door, or DCD4/DD14
     * for a locked door. Other record bytes must survive unchanged. */
    int (*patch)(void *, const EmDoorTransitPlan *);
    int (*face_player)(void *, float yaw);
    int (*align_player)(void *, const float position[4]);
    int (*script_start)(void *, uint32_t entry);
    int (*script_tick)(void *); /*0 waiting,1 finished,-1 host fault*/
} EmDoorTransitHooks;

/* BBE40's actual ordering, including its first script pump. Return0 when
 * bit2 is clear,1 kicked off,-1 missing/failed worker. The first pump's
 * finished result does not change the kickoff return value. */
int em_door_transit_kickoff(EmDoorOriginal *, float yaw,
    const float player_position[3], const uint16_t sounds[2], int locked,
    const EmInteractionMath *, const EmDoorTransitHooks *);

typedef struct {
    uint8_t area, sub_area, entry, kind; /*D8106B5..B8*/
} EmDoorDestination;

/* record is the actual four-byte row selected by area and (door_id&7F).
 * Same-area requests preserve area/sub-area. The fade runs before any
 * request-byte mutation, as in BC150. whole_area selects B0C00(4), else
 * AEDE0(4,0). Return1 accepted,-1 invalid/failed required worker. */
int em_door_transit_commit(EmDoorDestination *, int16_t door_id, uint16_t side,
    const uint8_t record[4], int (*fade)(void *, int whole_area, int ticks), void *);

#endif
