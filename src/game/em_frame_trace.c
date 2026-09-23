/* Native frame-order trace recorder (WP-3 step S6). Format and rules:
 * em_frame_trace.h and SCENE_COORDINATOR_DESIGN.md section 3.3. */
#include "game/em_frame_trace.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static void latch(EmFrameTrace *t, const char *what)
{
    if (t->error)
        return;
    t->error = 1;
    fprintf(stderr, "em_frame_trace: %s (tick %u); recording stopped\n", what,
            (unsigned)t->counter);
    if (t->fp) {
        /* `what` is a fixed ASCII message from this file (no escaping needed). */
        fprintf(t->fp, "{\"error\":\"%s\",\"counter\":%u}\n", what, (unsigned)t->counter);
        fflush(t->fp);
    }
    t->in_tick = 0;
}

static int reserve(EmFrameTrace *t, size_t extra)
{
    if (t->len + extra + 1 <= t->cap)
        return 0;
    size_t cap = t->cap ? t->cap : 4096;
    while (cap < t->len + extra + 1)
        cap *= 2;
    char *p = realloc(t->events, cap);
    if (!p) {
        latch(t, "out of memory");
        return -1;
    }
    t->events = p;
    t->cap = cap;
    return 0;
}

static void append(EmFrameTrace *t, const char *fmt, ...)
{
    if (t->error)
        return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) {
        latch(t, "format failure");
        return;
    }
    if (reserve(t, (size_t)n) < 0)
        return;
    va_start(ap, fmt);
    vsnprintf(t->events + t->len, t->cap - t->len, fmt, ap);
    va_end(ap);
    t->len += (size_t)n;
}

static void append_string(EmFrameTrace *t, const char *s)
{
    if (!s) {
        append(t, "null");
        return;
    }
    append(t, "\"");
    for (const unsigned char *p = (const unsigned char *)s; *p && !t->error; ++p) {
        if (*p == '"' || *p == '\\')
            append(t, "\\%c", *p);
        else if (*p < 0x20)
            append(t, "\\u%04x", *p);
        else
            append(t, "%c", *p);
    }
    append(t, "\"");
}

/* Every event-level entry point requires an open tick. */
static int event_ready(EmFrameTrace *t, const char *what)
{
    if (!t || t->error)
        return 0;
    if (!t->in_tick) {
        latch(t, what);
        return 0;
    }
    return 1;
}

static void event_separator(EmFrameTrace *t)
{
    if (t->nevents++)
        append(t, ",");
}

int em_frame_trace_open(EmFrameTrace *t, const char *path)
{
    memset(t, 0, sizeof *t);
    t->fp = fopen(path, "w");
    if (!t->fp) {
        fprintf(stderr, "em_frame_trace: cannot open %s\n", path);
        t->error = 1;
        return -1;
    }
    return 0;
}

int em_frame_trace_close(EmFrameTrace *t)
{
    if (!t)
        return -1;
    if (t->in_tick)
        latch(t, "tick still open at close");
    int result = t->error ? -1 : 0;
    if (t->fp && fclose(t->fp) != 0)
        result = -1;
    t->fp = NULL;
    free(t->events);
    t->events = NULL;
    t->len = t->cap = 0;
    return result;
}

int em_frame_trace_failed(const EmFrameTrace *t)
{
    return !t || t->error;
}

void em_frame_trace_tick_begin(EmFrameTrace *t, uint32_t counter)
{
    if (!t || t->error)
        return;
    if (t->in_tick) {
        latch(t, "tick begun while a tick is open");
        return;
    }
    t->in_tick = 1;
    t->counter = counter;
    t->have_state = 0;
    t->have_classifier = 0;
    t->len = 0;
    t->nevents = 0;
    if (reserve(t, 0) == 0)
        t->events[0] = '\0';
}

void em_frame_trace_frame_state(EmFrameTrace *t, const uint8_t task[5], uint8_t selector,
                                int32_t fade)
{
    if (!event_ready(t, "frame state outside a tick"))
        return;
    if (t->have_state) {
        latch(t, "second frame state in one tick");
        return;
    }
    t->have_state = 1;
    memcpy(t->task, task, sizeof t->task);
    t->selector = selector;
    t->fade = fade;
}

void em_frame_trace_classifier(EmFrameTrace *t, int32_t result)
{
    if (!event_ready(t, "classifier outside a tick"))
        return;
    if (t->have_classifier) {
        latch(t, "second classifier result in one tick");
        return;
    }
    t->have_classifier = 1;
    t->classifier = result;
}

void em_frame_trace_call(EmFrameTrace *t, uint32_t caller, uint32_t callee, uint32_t a0,
                         uint32_t a1, uint32_t a2, uint32_t a3)
{
    if (!event_ready(t, "call outside a tick"))
        return;
    event_separator(t);
    append(t, "{\"fn\":\"%08X\",\"target\":\"%08X\",\"args\":[%u,%u,%u,%u]}", (unsigned)caller,
           (unsigned)callee, (unsigned)a0, (unsigned)a1, (unsigned)a2, (unsigned)a3);
}

void em_frame_trace_node(EmFrameTrace *t, uint32_t callback, uint8_t cls, const char *record,
                         const char *binding)
{
    if (!event_ready(t, "walk node outside a tick"))
        return;
    event_separator(t);
    /* The walker is 001AFD70; per ticked node it calls 001CB590 and then the
     * behaviour *(+0x10) (Extermination/src/func_001AFD70.c). One event
     * stands for that pair. */
    append(t, "{\"fn\":\"001AFD70\",\"op\":\"jalr\",\"callback\":\"%08X\",\"class\":%u,\"record\":",
           (unsigned)callback, (unsigned)cls);
    append_string(t, record);
    append(t, ",\"binding\":");
    append_string(t, binding);
    append(t, "}");
}

int em_frame_trace_tick_end(EmFrameTrace *t)
{
    if (!t || t->error)
        return -1;
    if (!t->in_tick) {
        latch(t, "tick ended without a tick open");
        return -1;
    }
    FILE *fp = t->fp;
    int ok = fprintf(fp, "{\"counter\":%u,", (unsigned)t->counter) >= 0;
    if (t->have_state)
        ok = ok && fprintf(fp, "\"task\":[%u,%u,%u,%u,%u],", t->task[0], t->task[1], t->task[2],
                           t->task[3], t->task[4]) >= 0;
    else
        ok = ok && fputs("\"task\":null,", fp) >= 0;
    if (t->have_classifier)
        ok = ok && fprintf(fp, "\"classifier\":%d,", (int)t->classifier) >= 0;
    else
        ok = ok && fputs("\"classifier\":null,", fp) >= 0;
    if (t->have_state)
        ok = ok && fprintf(fp, "\"selector\":%u,\"fade\":%d,", t->selector, (int)t->fade) >= 0;
    else
        ok = ok && fputs("\"selector\":null,\"fade\":null,", fp) >= 0;
    ok = ok && fprintf(fp, "\"events\":[%s]}\n", t->events ? t->events : "") >= 0;
    ok = ok && fflush(fp) == 0;
    t->in_tick = 0;
    if (!ok) {
        latch(t, "write failure");
        return -1;
    }
    ++t->ticks;
    return 0;
}

/* ------------------------------------------------ EM_FRAME_TRACE recorder */

static EmFrameTrace s_env;
static int s_env_state; /* 0 unchecked, 1 open, -1 disabled or failed */

static void env_close(void)
{
    if (s_env_state == 1) {
        if (em_frame_trace_close(&s_env) != 0)
            fprintf(stderr, "em_frame_trace: EM_FRAME_TRACE output is incomplete\n");
        s_env_state = -1;
    }
}

EmFrameTrace *em_frame_trace_env(void)
{
    if (s_env_state == 0) {
        const char *path = getenv("EM_FRAME_TRACE");
        s_env_state = -1;
        if (path && *path && em_frame_trace_open(&s_env, path) == 0) {
            s_env_state = 1;
            atexit(env_close);
        }
    }
    return s_env_state == 1 ? &s_env : NULL;
}

void em_frame_trace_env_hook(void *ctx, uint32_t caller, uint32_t callee, uint32_t a0,
                             uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)ctx;
    EmFrameTrace *t = em_frame_trace_env();
    if (t)
        em_frame_trace_call(t, caller, callee, a0, a1, a2, a3);
}
