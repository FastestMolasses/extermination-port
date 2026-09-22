/* Original ordinary-door program24DE40 and cleanup24DBC0. */
#ifndef EM_DOOR_PROGRAM_H
#define EM_DOOR_PROGRAM_H

#include "game/em_door_transit.h"
#include "game/em_script.h"

typedef struct {
    void *context;
    EmScriptCommandResult (*frame)(void *, EmScript *, const unsigned char *);
    int (*camera_chase)(void *); /*B7B30/sub5: CBD0(-20), D7B0(5/1), timer120*/
    int (*player_animation)(void *, uint16_t clip, float rate, float blend);
    int (*object_animation)(void *, uint16_t clip, float blend, float start);
    int (*sound)(void *, uint32_t cue, float radius); /*FBD50(owner,cue,0,300)*/
} EmDoorProgramHooks;

typedef struct {
    EmScriptImage image;
    EmScript script;
    EmDoorOriginal *owner;
    EmDoorProgramHooks hooks;
    int failed;
} EmDoorProgram;

int em_door_program_load(EmDoorProgram *, const char *path,
    EmDoorOriginal *, const EmDoorProgramHooks *);
void em_door_program_free(EmDoorProgram *);
/* These functions are the typed BBE40 patch/start/pump bindings. */
int em_door_program_patch(EmDoorProgram *, const EmDoorTransitPlan *);
int em_door_program_start(EmDoorProgram *, uint32_t entry);
int em_door_program_tick(EmDoorProgram *); /*-1 fault,0 yielded,1 finished*/
/* Bounded command worker; unsupported locked-door commands fail explicitly. */
EmScriptCommandResult em_door_program_command(EmDoorProgram *, EmScript *, unsigned char record[64]);

#endif
