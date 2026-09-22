/* Automatic opening controller: original AREA11 actor at 0x00823E80.
 * The script is triggered after actor resources initialize, with no
 * proximity or button test. This is separate from the three traversal
 * events handled by em_director. */
#ifndef EM_AREA11_OPENING_H
#define EM_AREA11_OPENING_H

#include "game/em_script.h"

typedef enum {
    EM_OPENING_STOP_STREAM,
    EM_OPENING_STOP_CHILD_ACTORS,
    EM_OPENING_EVENT_B9_COMPLETE,
    EM_OPENING_ADD_KEY_ITEM_ZERO,
    EM_OPENING_RESUME_MUSIC,
    EM_OPENING_FADE_IN_FOUR
} EmOpeningEvent;

typedef void (*EmOpeningNotify)(void *context, EmOpeningEvent event);

typedef struct {
    EmScript script;
    uint32_t entry;
    uint8_t actor_state;
    uint8_t substate;
} EmArea11Opening;

void em_area11_opening_init(EmArea11Opening *opening, uint32_t script_entry);
/* resources_ready corresponds to successful func_001B0FD0 initialization.
 * event_39 is D_00810791 (not the unrelated 0xB9 completion flag).
 * resolve/execute/notify are required host services. A missing script
 * handler faults; it never reports that the opening has completed. */
EmScriptResult em_area11_opening_tick(EmArea11Opening *opening,
        int resources_ready, uint8_t event_39, EmScriptResolve resolve,
        EmScriptExecute execute, EmOpeningNotify notify, void *context);

#endif
