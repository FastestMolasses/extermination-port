/* Typed adapter for original AREA11 elevator programs. The scene binds
 * real player, camera, frame, message and motion operations. An absent
 * binding is a fault; script completion never substitutes for that work. */
#ifndef EM_ELEVATOR_PROGRAM_H
#define EM_ELEVATOR_PROGRAM_H

#include "game/em_elevator.h"
#include "game/em_script.h"

typedef struct {
    void *context;
    EmScriptCommandResult (*frame)(void *, EmScript *, const unsigned char *);
    int (*align_player)(void *, const float position[3]); /*00182F90*/
    int (*face_player)(void *, float yaw); /*001B9C10 sub8*/
    int (*camera_set)(void *, const float eye[3], const float target[3]);
    int (*camera_publish)(void *); /*001DD980*/
    int (*camera_chase)(void *); /*001B7B30 sub5*/
    int (*animation_start)(void *, uint16_t clip, float rate, float blend);
    int (*animation_done)(void *); /*player+200 bit1000; negative host fault*/
    int (*message_start)(void *, uint32_t token, uint32_t delay);
    int (*message_done)(void *); /*0 waiting,1 complete,negative host fault*/
    /* Original00828050 uses the current command phase; the host carries
     * all original position mirrors and rebuilds the actor pose. */
    EmScriptCommandResult (*move)(void *, EmScript *);
} EmElevatorProgramHooks;

typedef struct {
    EmScriptImage image;
    EmScript script;
    EmElevator *owner;
    EmElevatorProgramHooks hooks;
    int failed;
} EmElevatorProgram;

int em_elevator_program_load(EmElevatorProgram *, const char *path,
                              EmElevator *, const EmElevatorProgramHooks *);
void em_elevator_program_free(EmElevatorProgram *);
int em_elevator_program_start(EmElevatorProgram *, uint32_t entry);
/*0 yielded,1 completed,-1 missing/unsupported binding.*/
int em_elevator_program_tick(EmElevatorProgram *);

#endif
