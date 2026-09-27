#include "game/em_random.h"

#include <stdio.h>
#include <stdlib.h>
#if defined(__APPLE__) || defined(__linux__)
#include <dlfcn.h>
#include <execinfo.h>
#define EM_RANDOM_CAN_TRACE 1
#endif

/* Cold-boot state: the ELF's initialized SDK word at (*D_0024295C)+0x58
 * (0x002426C8) is 1. The only seeding call, func_00122BA8(0x45), is in
 * anim_frame_top_a (0x001ACA20) state 4 sub 0, i.e. attract-demo start;
 * no overlay calls it. Cold boot -> New Game therefore starts from 1 (the
 * C7 rand() capture read 1 at the NEW GAME commit and saw no srand). The
 * call order against the original's is audited by tools/rand_order.py
 * (docs/RAND_ORDER.md). */
static uint32_t random_state = 1;

uint32_t em_random_step(uint32_t *state)
{
    /* Unsigned arithmetic reproduces the original MULT/ADDIU wraparound
     * without the signed-overflow undefined behavior of the decompiled C. */
    *state = *state * UINT32_C(0x41C64E6D) + UINT32_C(0x3039);
    return *state & UINT32_C(0x7FFFFFFF);
}

void em_random_seed(uint32_t seed)
{
    random_state = seed;
}

/* EM_RAND_TRACE=<path> (test instrumentation; it never changes behaviour):
 * one line per call, "counter state r1 .. r6" in hex: the main-loop counter
 * (0x70003B64, from the clock main.c registers), the state word before
 * the call and the native return addresses of the six frames above this
 * function, relative to the image base. tools/rand_order.py resolves them
 * to the translated functions and those to the original callers. */
static uint32_t (*s_clock)(void);

void em_random_trace_clock(uint32_t (*counter)(void))
{
    s_clock = counter;
}

#ifdef EM_RANDOM_CAN_TRACE
static FILE *trace_file(void)
{
    static int checked;
    static FILE *file;
    if (!checked) {
        checked = 1;
        const char *path = getenv("EM_RAND_TRACE");
        if (path && path[0]) {
            file = fopen(path, "w");
            if (!file) fprintf(stderr, "em_random: EM_RAND_TRACE=%s cannot be opened\n", path);
        }
    }
    return file;
}

static void trace_call(FILE *file, void *const *frames, int count)
{
    Dl_info info;
    uintptr_t base = 0;
    if (dladdr((const void *)&em_random_step, &info) && info.dli_fbase)
        base = (uintptr_t)info.dli_fbase;
    fprintf(file, "%x %08x", s_clock ? (unsigned)s_clock() : 0u, (unsigned)random_state);
    for (int i = 1; i < count; ++i)
        fprintf(file, " %lx", (unsigned long)((uintptr_t)frames[i] - base));
    fputc('\n', file);
}
#endif

uint32_t em_random_next(void)
{
#ifdef EM_RANDOM_CAN_TRACE
    FILE *file = trace_file();
    if (file) {
        void *frames[7];
        trace_call(file, frames, backtrace(frames, 7));
    }
#endif
    return em_random_step(&random_state);
}
