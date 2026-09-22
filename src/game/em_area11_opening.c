#include "game/em_area11_opening.h"

#include <string.h>

void em_area11_opening_init(EmArea11Opening *opening, uint32_t script_entry)
{
    memset(opening,0,sizeof *opening);
    opening->entry=script_entry;
}

EmScriptResult em_area11_opening_tick(EmArea11Opening *opening,
        int resources_ready, uint8_t event_39, EmScriptResolve resolve,
        EmScriptExecute execute, EmOpeningNotify notify, void *context)
{
    if (!resolve || !execute || !notify) return EM_SCRIPT_FAULT;
    if (opening->actor_state == 0) {
        if (resources_ready) opening->actor_state=1;
        return EM_SCRIPT_YIELDED;
    }
    if (event_39 == 0xFF || opening->substate == 2) return EM_SCRIPT_FINISHED;
    if (opening->substate == 0) {
        em_script_start(&opening->script,opening->entry);
        notify(context,EM_OPENING_STOP_STREAM);
        opening->substate=1;
        return EM_SCRIPT_YIELDED;
    }
    EmScriptResult result=em_script_tick(&opening->script,resolve,execute,context);
    if (result == EM_SCRIPT_FAULT || result == EM_SCRIPT_YIELDED) return result;
    /* 0x00823F28..0x00823F68. The controller's final side effects run
     * for either normal script completion or its nonzero abort result. */
    notify(context,EM_OPENING_STOP_CHILD_ACTORS);
    notify(context,EM_OPENING_EVENT_B9_COMPLETE);
    notify(context,EM_OPENING_ADD_KEY_ITEM_ZERO);
    notify(context,EM_OPENING_RESUME_MUSIC);
    opening->substate=2;
    notify(context,EM_OPENING_FADE_IN_FOUR);
    return EM_SCRIPT_FINISHED;
}
