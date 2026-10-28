/* tests/status_field_reference_bridge.c - the C side of
 * tools/test_status_field_reference.py (docs/STATUS_FIELD.md):
 *
 *   sf_map32   word addresses of a PSMCT32 buffer (the model's own tables),
 *              for the test's decoding of local memory;
 *   sf_replay  register writes (A+D form), then a GIF byte stream, through
 *              the CPU GS model (em_gs_raster, strict mode as the Original
 *              profile runs it, or not strict for a diagnosis) over a local-memory image.
 *
 * Nothing here is disc-derived: the captures are read at run time from the
 * user's own build folder. */
#include "gs/em_gs_raster.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void sf_map32(uint32_t bp, uint32_t bw, uint32_t w, uint32_t h, uint32_t *out)
{
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) out[y * w + x] = em_gs_addr32(bp, bw, x, y, 0);
}

/* out: [0] GIF bytes consumed, [1] refusal bits, [2] refused primitives,
 * [3] span faults, [4] primitives drawn, [5] pixels drawn. */
int sf_replay(uint8_t *mem, const uint32_t *reg, const uint64_t *val, uint32_t n, const uint8_t *gif,
              uint64_t gif_bytes, int strict, uint64_t out[6], char *reason, uint32_t reason_cap)
{
    if (!mem || (n && (!reg || !val)) || (!gif && gif_bytes) || !out) return -1;
    EmGs g;
    em_gs_init(&g, mem);
    g.strict = strict;
    for (uint32_t i = 0; i < n; ++i) em_gs_write(&g, reg[i], val[i]);
    out[0] = gif_bytes ? em_gs_gif(&g, gif, (size_t)gif_bytes) : 0;
    em_gs_flush(&g);
    out[1] = g.refusals;
    out[2] = g.refused_prims;
    out[3] = g.span_faults;
    out[4] = g.drawn_prims;
    out[5] = g.drawn_pixels;
    if (reason && reason_cap) snprintf(reason, reason_cap, "%s", g.reason);
    em_gs_release(&g);
    return 0;
}
