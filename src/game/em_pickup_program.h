#ifndef EM_PICKUP_PROGRAM_H
#define EM_PICKUP_PROGRAM_H
#include "game/em_script.h"

typedef struct {
    void *context;
    EmScriptCommandResult (*frame)(void *, EmScript *, const unsigned char *);
    /* Original1B7F90/sub1 and1B8FC0/sub8: -1 failure,0 waiting,1 done. */
    int (*turn)(void *, float step);
    int (*camera)(void *, EmScript *);
    int (*animation)(void *, uint16_t clip, float rate, float blend);
    int (*animation_done)(void *);
    int (*take)(void *);
} EmPickupProgramHooks;

typedef struct {
    EmScriptImage image;
    EmScript script;
    EmPickupProgramHooks hooks;
    int failed;
} EmPickupProgram;

int em_pickup_program_load(EmPickupProgram *, const char *path, uint32_t callback,
                            const EmPickupProgramHooks *);
void em_pickup_program_free(EmPickupProgram *);
int em_pickup_program_start(EmPickupProgram *, uint32_t entry, uint16_t clip);
/* -1 required worker/program failure,0 yielded,1 complete. */
int em_pickup_program_tick(EmPickupProgram *);
#endif
