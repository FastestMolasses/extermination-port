/* Headless task-table contract checks. Expected transitions come from
 * 001AB650/001AB6A0/001AB740/001AB790, including the original four-word
 * clear and later-slot registration during dispatch. No OS or assets. */
#include "game/em_task.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static EmTask *slots[EM_TASK_SLOTS];
static int trace[16];
static int trace_count;

static void record(int slot)
{
    assert(em_task_current() == slots[slot]);
    assert(slots[slot]->state == EM_TASK_STATE_RUN);
    assert(trace_count < (int)(sizeof trace / sizeof trace[0]));
    trace[trace_count++] = slot;
}

static void first(void)  { record(0); }
static void second(void) { record(1); }
static void third(void)  { record(2); }

static void check_reset(const EmTask *task)
{
    for (int i = 0; i < EM_TASK_RESET_BYTES; ++i)
        assert(task->user[i] == 0);
    for (int i = EM_TASK_RESET_BYTES; i < EM_TASK_USER_BYTES; ++i)
        assert(task->user[i] == (unsigned char)(0x80 + i));
}

static void replacement(void)
{
    record(0);
    check_reset(slots[0]);
}

static void replace_self(void)
{
    record(0);
    for (int i = 0; i < EM_TASK_USER_BYTES; ++i)
        slots[0]->user[i] = (unsigned char)(0x80 + i);
    assert(em_task_replace_current(replacement) == slots[0]);
    assert(em_task_current() == slots[0]);
    assert(slots[0]->state == EM_TASK_STATE_START);
    assert(slots[0]->fn == replacement);
    check_reset(slots[0]);
}

static void register_later(void)
{
    record(0);
    slots[2] = em_task_register(2, third);
    slots[0]->state = EM_TASK_STATE_FREE;
}

static void register_earlier(void)
{
    record(2);
    slots[0] = em_task_register(0, first);
    slots[2]->state = EM_TASK_STATE_FREE;
}

static void reset(void)
{
    em_task_init();
    trace_count = 0;
    assert(em_task_current() == NULL);
}

int main(void)
{
    reset();
    assert(em_task_register(-1, first) == NULL);
    assert(em_task_register(EM_TASK_SLOTS, first) == NULL);
    assert(em_task_replace_current(first) == NULL);
    em_task_dispatch();
    assert(trace_count == 0);

    /* Re-registering preserves the final eight bytes; initializing the
     * entire table later clears them, as original 001AB650 does. */
    slots[0] = em_task_register(0, first);
    for (int i = 0; i < EM_TASK_USER_BYTES; ++i)
        slots[0]->user[i] = (unsigned char)(0x80 + i);
    assert(em_task_register(0, replacement) == slots[0]);
    assert(slots[0]->state == EM_TASK_STATE_START);
    assert(slots[0]->fn == replacement);
    check_reset(slots[0]);
    em_task_dispatch();
    assert(trace_count == 1);
    reset();
    assert(slots[0]->state == EM_TASK_STATE_FREE);
    assert(slots[0]->fn == NULL);
    for (int i = 0; i < EM_TASK_USER_BYTES; ++i)
        assert(slots[0]->user[i] == 0);

    /* START and WAKE both become RUN before callback; RUN is repeatable,
     * FREE and unrecognized state 3 do not dispatch. */
    slots[0] = em_task_register(0, first);
    slots[1] = em_task_register(1, second);
    slots[2] = em_task_register(2, third);
    slots[1]->state = EM_TASK_STATE_WAKE;
    slots[2]->state = EM_TASK_STATE_RUN;
    em_task_dispatch();
    assert(trace_count == 3);
    assert(trace[0] == 0 && trace[1] == 1 && trace[2] == 2);
    assert(em_task_current() == NULL);
    slots[0]->state = EM_TASK_STATE_FREE;
    slots[1]->state = 3;
    em_task_dispatch();
    assert(trace_count == 4 && trace[3] == 2);

    /* Replacing the currently running function arms it for the next
     * dispatch, without executing it again in the same pass. */
    reset();
    slots[0] = em_task_register(0, replace_self);
    em_task_dispatch();
    assert(trace_count == 1);
    assert(slots[0]->state == EM_TASK_STATE_START);
    assert(em_task_current() == NULL);
    em_task_dispatch();
    assert(trace_count == 2 && trace[1] == 0);
    assert(slots[0]->state == EM_TASK_STATE_RUN);

    /* A future slot can run this frame; a passed slot waits one frame.
     * This distinguishes the original sequential dispatch from a
     * snapshot/queued scheduler. */
    reset();
    slots[0] = em_task_register(0, register_later);
    em_task_dispatch();
    assert(trace_count == 2 && trace[0] == 0 && trace[1] == 2);
    assert(slots[0]->state == EM_TASK_STATE_FREE);
    reset();
    slots[2] = em_task_register(2, register_earlier);
    em_task_dispatch();
    assert(trace_count == 1 && trace[0] == 2);
    assert(slots[0]->state == EM_TASK_STATE_START);
    em_task_dispatch();
    assert(trace_count == 2 && trace[1] == 0);
    assert(em_task_current() == NULL);

    puts("task_test: PASS");
    return 0;
}
