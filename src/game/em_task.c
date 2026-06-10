/* em_task.c — 3-slot frame-task table (see em_task.h for the PS2 mapping). */
#include "game/em_task.h"

#include <string.h>

static EmTask  s_table[EM_TASK_SLOTS];  /* the 0x0028A750 table, natively */
static EmTask *s_current;               /* scratchpad 0x70003B6C mirror */

void em_task_init(void)
{
    memset(s_table, 0, sizeof s_table);
    s_current = NULL;
}

EmTask *em_task_register(int slot, EmTaskFn fn)
{
    if (slot < 0 || slot >= EM_TASK_SLOTS) return NULL;
    EmTask *t = &s_table[slot];
    t->state  = EM_TASK_STATE_START;
    t->fn     = fn;
    memset(t->user, 0, sizeof t->user);
    return t;
}

void em_task_dispatch(void)
{
    for (int i = 0; i < EM_TASK_SLOTS; i++) {
        EmTask *t = &s_table[i];
        if (t->state == EM_TASK_STATE_START || t->state == EM_TASK_STATE_WAKE)
            t->state = EM_TASK_STATE_RUN;
        if (t->state != EM_TASK_STATE_RUN || !t->fn) continue;
        s_current = t;
        t->fn();          /* may re-register its own slot (boot -> game) */
        s_current = NULL;
    }
}

EmTask *em_task_current(void)
{
    return s_current;
}
