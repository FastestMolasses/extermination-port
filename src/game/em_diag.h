/* em_diag.h - the diagnostic digest switch (docs/PERFORMANCE.md, Q1).
 *
 * The live draw modules keep FNV-1a / CRC-32 digests of what they drew
 * (the chain page, the static world, the owner units, the effects' lanes,
 * the background grid, the load veil, the snow packets, the dynamic
 * table). Their only reader is the scene tick log (em_scene_bindings.c,
 * EM_AREA_CHANGE_LOG), which the level smokes, the fb2 harness and the
 * reference tests open and tools/test_level_smoke.py checks. They never
 * change what is drawn or what the game does, so normal play skips them.
 *
 * On when EM_AREA_CHANGE_LOG names a file, or EM_DIAG_DIGESTS=1 (a
 * developer variable: the digests without the log, to measure their cost).
 * Read once per translation unit, on the game thread. */
#ifndef EM_DIAG_H
#define EM_DIAG_H

#include <stdlib.h>

static inline int em_diag_digests(void)
{
    static int on = -1;
    if (on < 0) {
        const char *log = getenv("EM_AREA_CHANGE_LOG");
        const char *force = getenv("EM_DIAG_DIGESTS");
        on = (log && log[0]) || (force && force[0] == '1');
    }
    return on;
}

#endif
