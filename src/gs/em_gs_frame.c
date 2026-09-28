/* em_gs_frame - binding helpers of the CPU GS model (em_gs_frame.h,
 * docs/GS_EXACT.md section 9). */
#include "gs/em_gs_frame.h"

#include <string.h>

enum {
    W_TEX0 = 1u << 0, W_CLAMP = 1u << 1, W_TEX1 = 1u << 2, W_ALPHA = 1u << 3, W_TEST = 1u << 4,
    W_COLCLAMP = 1u << 5, W_FRAME = 1u << 6, W_ZBUF = 1u << 7, W_XYOFFSET = 1u << 8,
    W_SCISSOR = 1u << 9, W_PRMODECONT = 1u << 10, W_DTHE = 1u << 11, W_FBA = 1u << 12,
    W_PABE = 1u << 13, W_TEXA = 1u << 14, W_SCANMSK = 1u << 15, W_FOGCOL = 1u << 16,
};

void em_gs_replay_init(EmGsReplay *r)
{
    memset(r, 0, sizeof *r);
}

static void put(EmGs *gs, EmGsReplay *r, uint32_t bit, uint64_t *slot, unsigned reg, uint64_t v)
{
    if ((r->written & bit) && *slot == v)
        return;
    *slot = v;
    r->written |= bit;
    em_gs_write(gs, reg, v);
}

void em_gs_replay_fogcol(EmGs *gs, EmGsReplay *r, uint64_t fogcol)
{
    put(gs, r, W_FOGCOL, &r->fogcol, EM_GS_FOGCOL, fogcol);
}

uint32_t em_gs_replay_prims(EmGs *gs, EmGsReplay *r, const EmGfxGsPrim *prims, const EmGfxGsEnv *envs,
                            uint32_t count)
{
    uint32_t refused0 = gs->refused_prims;
    for (uint32_t i = 0; i < count; i++) {
        const EmGfxGsPrim *p = &prims[i];
        if (envs) {
            const EmGfxGsEnv *e = &envs[i];
            if (e->set & EM_GFX_GS_ENV_FRAME) put(gs, r, W_FRAME, &r->frame, EM_GS_FRAME_1, e->frame);
            if (e->set & EM_GFX_GS_ENV_ZBUF) put(gs, r, W_ZBUF, &r->zbuf, EM_GS_ZBUF_1, e->zbuf);
            if (e->set & EM_GFX_GS_ENV_XYOFFSET) put(gs, r, W_XYOFFSET, &r->xyoffset, EM_GS_XYOFFSET_1, e->xyoffset);
            if (e->set & EM_GFX_GS_ENV_SCISSOR) put(gs, r, W_SCISSOR, &r->scissor, EM_GS_SCISSOR_1, e->scissor);
            if (e->set & EM_GFX_GS_ENV_PRMODECONT)
                put(gs, r, W_PRMODECONT, &r->prmodecont, EM_GS_PRMODECONT, e->prmodecont);
            if (e->set & EM_GFX_GS_ENV_DTHE) put(gs, r, W_DTHE, &r->dthe, EM_GS_DTHE, e->dthe);
            if (e->set & EM_GFX_GS_ENV_FBA) put(gs, r, W_FBA, &r->fba, EM_GS_FBA_1, e->fba);
            if (e->set & EM_GFX_GS_ENV_PABE) put(gs, r, W_PABE, &r->pabe, EM_GS_PABE, e->pabe);
            if (e->set & EM_GFX_GS_ENV_TEXA) put(gs, r, W_TEXA, &r->texa, EM_GS_TEXA, e->texa);
            if (e->set & EM_GFX_GS_ENV_SCANMSK) put(gs, r, W_SCANMSK, &r->scanmsk, EM_GS_SCANMSK, e->scanmsk);
        }
        if (p->set & EM_GFX_GS_TEX0) put(gs, r, W_TEX0, &r->tex0, EM_GS_TEX0_1, p->tex0);
        if (p->set & EM_GFX_GS_CLAMP) put(gs, r, W_CLAMP, &r->clamp, EM_GS_CLAMP_1, p->clamp);
        if (p->set & EM_GFX_GS_TEX1) put(gs, r, W_TEX1, &r->tex1, EM_GS_TEX1_1, p->tex1);
        if (p->set & EM_GFX_GS_ALPHA) put(gs, r, W_ALPHA, &r->alpha, EM_GS_ALPHA_1, p->alpha);
        if (p->set & EM_GFX_GS_TEST) put(gs, r, W_TEST, &r->test, EM_GS_TEST_1, p->test);
        if (p->set & EM_GFX_GS_COLCLAMP) put(gs, r, W_COLCLAMP, &r->colclamp, EM_GS_COLCLAMP, p->colclamp);
        em_gs_write(gs, EM_GS_PRIM, p->prim & 0x7FF);
        uint32_t n = p->count > 3 ? 3 : p->count;
        for (uint32_t k = 0; k < n; k++) {
            const EmGfxGsVertex *v = &p->v[k];
            em_gs_write(gs, EM_GS_RGBAQ, (uint64_t)v->rgba[0] | (uint64_t)v->rgba[1] << 8 | (uint64_t)v->rgba[2] << 16
                                         | (uint64_t)v->rgba[3] << 24 | (uint64_t)v->q << 32);
            em_gs_write(gs, EM_GS_ST, (uint64_t)v->s | (uint64_t)v->t << 32);
            em_gs_write(gs, EM_GS_UV, (uint64_t)(v->u & 0x3FFF) | (uint64_t)(v->v & 0x3FFF) << 16);
            if (v->has_f)
                em_gs_write(gs, EM_GS_XYZF2, (uint64_t)v->x | (uint64_t)v->y << 16 | (uint64_t)(v->z & 0xFFFFFF) << 32
                                             | (uint64_t)v->f << 56);
            else
                em_gs_write(gs, EM_GS_XYZ2, (uint64_t)v->x | (uint64_t)v->y << 16 | (uint64_t)v->z << 32);
        }
    }
    return gs->refused_prims - refused0;
}

void em_gs_read_frame_rgba(EmGs *gs, uint32_t fbp, uint32_t fbw, uint32_t w, uint32_t h, uint8_t *rgba)
{
    em_gs_flush(gs);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            uint32_t c = em_gs_read_pixel(gs, fbp * 32, fbw, EM_GS_PSMCT32, x, y);
            uint8_t *o = rgba + ((size_t)y * w + x) * 4;
            o[0] = (uint8_t)c; o[1] = (uint8_t)(c >> 8); o[2] = (uint8_t)(c >> 16); o[3] = (uint8_t)(c >> 24);
        }
}
