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

static EmTask *task_arm(EmTask *t, EmTaskFn fn)
{
    t->state  = EM_TASK_STATE_START;
    t->fn     = fn;
    /* Both 001AB740 and 001AB790 clear four words, original +8..+0x17.
     * The last eight bytes of task-private storage survive replacement. */
    memset(t->user, 0, EM_TASK_RESET_BYTES);
    return t;
}

EmTask *em_task_register(int slot, EmTaskFn fn)
{
    if (slot < 0 || slot >= EM_TASK_SLOTS) return NULL;
    return task_arm(&s_table[slot], fn);
}

EmTask *em_task_replace_current(EmTaskFn fn)
{
    if (!s_current) return NULL;
    return task_arm(s_current, fn);
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
