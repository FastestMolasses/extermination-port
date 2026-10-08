/* em_glue_original.c - see em_glue_original.h. */
#include "game/em_glue_original.h"

#include <stddef.h>
#include <string.h>

#include "game/em_ee_float.h"

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* ====================================================================== */
/* 0015CF90                                                               */
/* ====================================================================== */

int em_glue_0015CF90(const uint8_t *record, const EmGlueVitals *v)
{
    if (!record || !v || !v->d810706 || !v->d810707 || !v->d810858 || !v->d81085C || !v->d8106B9)
        return -1;
    *v->d810706 = record[0x235];
    *v->d810707 = record[0x234];
    memcpy(v->d810858, record + 0x220, 4);
    memcpy(v->d81085C, record + 0x228, 4);
    /* +0x220 <= 0.0 on the EE compare (a denormal reads as zero), then the
     * latch only while it is clear. */
    if (em_ee_c_le_bits(rd32(record + 0x220), 0u) && *v->d8106B9 == 0)
        *v->d8106B9 = 1;
    return 0;
}

/* ====================================================================== */
/* 001FC280                                                               */
/* ====================================================================== */

static int load32(const EmGlueAmbientWorkers *w, uint32_t address, uint32_t *out)
{
    const uint8_t *p = w->load(w->ctx, address, 4);
    if (!p) return -1;
    *out = rd32(p);
    return 0;
}

int em_glue_001FC280(const EmGlueAmbientWorkers *w, EmGlueAmbient *s, uint8_t d810700,
                     uint8_t d810701, uint8_t d810702, uint8_t d810788)
{
    if (!w || !s || !w->load || !w->w_0011A070 || !w->w_001FB9F0 || !w->w_00119828)
        return -1;
    /* D_0024D650[area][room] + entry * 0x30, then its +0x20 word. */
    uint32_t rooms, entries, word;
    if (load32(w, EM_GLUE_SPAWN_TABLE + 4u * d810700, &rooms) < 0 ||
        load32(w, rooms + 4u * d810701, &entries) < 0 ||
        load32(w, entries + EM_GLUE_SPAWN_STRIDE * d810702 + 0x20u, &word) < 0)
        return -1;
    int32_t lo = (int32_t)(word & 0xFFFFu);
    int32_t id = (int32_t)(int16_t)(uint16_t)(word >> 16);   /* the high half, arithmetic */
    if (d810700 == EM_GLUE_AMBIENT_AREA && d810788 == 0xFF)
        id = EM_GLUE_AMBIENT_FORCED;
    if (s->d282160 != id) {
        if (s->d282160 != -1 && w->w_0011A070(w->ctx, s->d282164) < 0)
            return -1;
        s->d282160 = id;
        if (id != -1) {
            s->d282170 = 0x1000;
            s->d28216C = 0x1000;
            s->d282168 = 0x1000;
            int32_t handle;
            if (w->w_001FB9F0(w->ctx, s->d282160, s->d282170, s->d282168, s->d28216C, &handle) < 0)
                return -1;
            s->d282164 = handle;
        }
    }
    if (w->w_00119828(w->ctx, 0, lo, lo) < 0 || w->w_00119828(w->ctx, 1, lo, lo) < 0)
        return -1;
    return 0;
}
