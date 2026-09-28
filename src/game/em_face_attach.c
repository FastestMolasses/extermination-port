/* em_face_attach.c - the +0x90 attachment draw 001CB3C0 and the face-unit
 * submit 001D3F50 / 001D3E40 (see em_face_attach.h, docs/FACE_ATTACH.md).
 *
 * Read from the original instructions (the decomp's build/asm): 001CB3C0 is
 * byte-matched C, 001D3F50 a two-instruction thunk, 001D3E40 NEARMISS C
 * whose .s was followed. vif_append_ref_tag and the tag writer are
 * em_owner_draw_original's, 001D2910 is a worker (the live render
 * context's em_render_context_001D2910). Every address in a comment is the
 * original instruction translated there. Comments describe what the
 * original computes; they never reproduce its instruction stream. */
#include "game/em_face_attach.h"

#include "game/em_ee_float.h"
#include "game/em_frame_render_heads.h"
#include "game/em_sdk_vu0.h"

#include <stddef.h>
#include <string.h>

typedef uint32_t u32;

#define SPR_A UINT32_C(0x70003400)   /* 001C7900's 001D88B0 a1 */
#define SPR_B UINT32_C(0x70003440)   /* 001C7900's 001D88B0 a2 */
#define D_00275670 UINT32_C(0x00275670)
#define D_00275688 UINT32_C(0x00275688)

/* ======================================================================
 * Faults
 * ==================================================================== */

static int latched(const EmFaceAttach *s) { return s->fault.code != EM_FACE_ATTACH_FAULT_NONE; }

static int fault(EmFaceAttach *s, u32 address, int32_t code)
{
    if (!latched(s)) {
        s->fault.address = address;
        s->fault.code = code;
    }
    return -1;
}

/* ======================================================================
 * Raw access
 * ==================================================================== */

static u32 rd32(const uint8_t *p)
{
    return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

/* The host bytes of the EE range [address, address + len) in the anim
 * rest's region table (the one 001CB2C0 reads through), or NULL. */
static uint8_t *map(const EmFaceAttach *s, u32 address, u32 len)
{
    const EmAnimRestWorld *w = &s->world.anim->world;
    for (unsigned i = 0; i < w->region_count && i < EM_POSE_REGION_MAX; ++i) {
        const EmPoseRegion *g = &w->region[i];
        if (!g->bytes || len > g->size) continue;
        u32 off = address - g->address;
        if (address < g->address || off > g->size - len) continue;
        return g->bytes + off;
    }
    return NULL;
}

static void trace(const EmFaceAttach *s, u32 callee, u32 a0, u32 a1, u32 a2, u32 a3, u32 t0)
{
    if (!s->workers.trace) return;
    static const u32 none[16];
    const u32 args[5] = {a0, a1, a2, a3, t0};
    s->workers.trace(s->workers.ctx, callee, args, s->frame ? s->frame : none);
}

/* ======================================================================
 * 001D3E40 / 001D3F50: the face unit's submit
 * ==================================================================== */

/* 001D2910(0) through the bound worker (the live render context's
 * em_render_context_001D2910): 0 while context +0x0C bit 0 is clear. */
static int query_2910(EmFaceAttach *s, u32 *result)
{
    if (!s->workers.w_001D2910) return fault(s, 0x001D2910u, EM_FACE_ATTACH_FAULT_NULL);
    if (s->workers.w_001D2910(s->workers.ctx, 0, result) < 0)
        return fault(s, 0x001D2910u, EM_FACE_ATTACH_FAULT_WORKER);
    return 0;
}

/* Every view 001D3E40 reaches and its whole run of bytes, before a write.
 * The face +0x04 word is only checked to be mapped here; submit reads it
 * where the original does (0x001D3F08, after the tags before it). The room
 * is sized by a 001D2910(0) query (it only reads context +0x0C; the traced
 * call at 0x001D3EB8 is the original's). */
static int submit_ready(EmFaceAttach *s, int32_t chan, u32 face, u32 fn)
{
    if (!s->world.anim) return fault(s, fn, EM_FACE_ATTACH_FAULT_NULL);
    const EmOwnerDrawWorld *d = s->world.draw;
    if (!d || !d->ctx_9C || !d->d00275674 || !d->channel || !d->ctx_50)
        return fault(s, 0x00275670u, EM_FACE_ATTACH_FAULT_NULL);
    if (chan < 0 || (u32)chan >= d->channel_count || (u32)chan >= d->ctx_50_count)
        return fault(s, fn, EM_FACE_ATTACH_FAULT_BAD_INDEX);
    if (!map(s, face + 4u, 4)) return fault(s, 0x001D3F08u, EM_FACE_ATTACH_FAULT_BAD_INDEX);
    u32 flag = 0;
    if (query_2910(s, &flag) < 0) return -1;
    const EmOwnerServicesChannel *c = &d->channel[chan];
    u32 bytes = flag ? 0x40u : 0x50u;
    if (!c->cursor || !c->end || c->cursor > c->end || (size_t)(c->end - c->cursor) < bytes)
        return fault(s, fn, EM_FACE_ATTACH_FAULT_BAD_INDEX);
    return 0;
}

static int submit(EmFaceAttach *s, int32_t chan, u32 face)
{
    if (submit_ready(s, chan, face, 0x001D3E40u) < 0) return -1;
    const EmOwnerDrawWorld *d = s->world.draw;
    EmOwnerServicesChannel *c = &d->channel[chan];

    /* vif_append_ref_tag(chan, 0x0023C480) (0x001D3E64): REF 1 qw to
     * *D_00275674; context +0x50 + 4 chan = the target; CALL (qwc 0) to it
     * (em_owner_draw_original's translation, the one bound owner). */
    trace(s, 0x001D2090u, (u32)chan, EM_FACE_ATTACH_KERNEL, 0, 0, 0);
    em_owner_draw_vif_append_ref_tag(d, chan, EM_FACE_ATTACH_KERNEL);

    /* REF 8 qw to D_00816440 + (context +0x9C) << 7 (0x001D3E80..0x001D3EBC). */
    em_owner_draw_tag(c, 0x30, 8u, EM_FACE_ATTACH_SKIN_RECORD + (*d->ctx_9C << 7));

    /* 001D2910(0) (0x001D3EB8): while it returns 0, REF 2 qw to
     * D_002514B0. */
    trace(s, 0x001D2910u, 0, 0, 0, 0, 0);
    u32 flag = 0;
    if (query_2910(s, &flag) < 0) return -1;
    if (flag == 0) em_owner_draw_tag(c, 0x30, 2u, EM_FACE_ATTACH_D_002514B0);

    /* REF (the halfword store of face +0x04) qw to face + 0x40
     * (0x001D3F04..0x001D3F34). +0x04 is read here, after the tags above,
     * as the original does; submit_ready checked it is mapped. */
    const u32 w04 = rd32(map(s, face + 4u, 4));
    em_owner_draw_tag(c, 0x30, w04 & 0xFFFFu, face + 0x40u);
    return 0;
}

int em_face_attach_001D3E40(EmFaceAttach *s, int32_t chan, uint32_t face)
{
    if (!s) return -1;
    if (latched(s)) return -1;
    return submit(s, chan, face);
}

int em_face_attach_001D3F50(EmFaceAttach *s, uint32_t face)
{
    if (!s) return -1;
    if (latched(s)) return -1;
    trace(s, 0x001D3E40u, 0, face, 0, 0, 0);                     /* the tail jump, a0 = 0 */
    return submit(s, 0, face);
}

/* ======================================================================
 * 001D88B0 through em_frh_001D88B0 (the EmAnimRest worker)
 * ==================================================================== */

typedef struct {
    EmFaceAttach *s;
    u32 *a, *b;          /* SPR 0x70003400 / 0x70003440 host words */
    const u32 *token;    /* the 16 bytes at `token` */
    u32 token_address;
} Light;

static int host_matrix(const Light *l, u32 address, u32 **out)
{
    if (address == SPR_A) *out = l->a;
    else if (address == SPR_B) *out = l->b;
    else return -1;
    return 0;
}

/* 001D8130(a0 = 0x20, a1 = position): reads no argument. */
static int w_8130(void *ctx, int32_t a0, uint32_t a1)
{
    Light *l = ctx;
    trace(l->s, 0x001D8130u, (u32)a0, a1, 0, 0, 0);
    return em_actor_light_001D8130(l->s->world.light) < 0 ? -1 : 0;
}

/* 001D8340(owner = 0, A, B, flag = 0x20, point): owner 0 returns before the
 * fold, the only reader of the point; A and B are not read. */
static int w_8340(void *ctx, int32_t a0, uint32_t a1, uint32_t a2, int32_t a3, uint32_t t0)
{
    Light *l = ctx;
    trace(l->s, 0x001D8340u, (u32)a0, a1, a2, (u32)a3, t0);
    if (a0 != 0) return -1;
    return em_actor_light_001D8340(l->s->world.light, NULL, (u32)a3, NULL) < 0 ? -1 : 0;
}

/* 001D8690(A, B, rgb, 0x20). */
static int w_8690(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, int32_t a3)
{
    Light *l = ctx;
    trace(l->s, 0x001D8690u, a0, a1, a2, (u32)a3, 0);
    u32 *a = NULL, *b = NULL;
    if (host_matrix(l, a0, &a) < 0 || host_matrix(l, a1, &b) < 0 || a2 != l->token_address) return -1;
    return em_actor_light_001D8690(l->s->world.light, a, b, l->token) < 0 ? -1 : 0;
}

int em_face_attach_w_001D88B0(void *ctx, const uint32_t position[4], float a[16], float b[16],
                              uint32_t token)
{
    EmFaceAttach *s = ctx;
    if (!s || !a || !b) return -1;
    if (latched(s)) return -1;
    (void)position;   /* forwarded by the original to 001D8130 / 001D8340, neither reads it here */
    trace(s, 0x001D88B0u, 0, SPR_A, SPR_B, token, 0);
    EmActorLight *light = s->world.light;
    if (!light || !light->world.ctx_246C || !light->world.d00275688)
        return fault(s, 0x001D88B0u, EM_FACE_ATTACH_FAULT_NULL);
    uint8_t *rgb = map(s, token, 16);
    if (!rgb) return fault(s, 0x001D88B0u, EM_FACE_ATTACH_FAULT_BAD_INDEX);

    u32 words[4], context = s->world.context_address;
    for (int k = 0; k < 4; ++k) words[k] = rd32(rgb + 4 * k);
    Light l = {s, (u32 *)(void *)a, (u32 *)(void *)b, words, token};
    EmFrhView views[7];
    uint32_t n = 0;
    views[n++] = (EmFrhView){D_00275670, 4, (uint8_t *)&context, 0};
    views[n++] = (EmFrhView){D_00275688, 4, (uint8_t *)light->world.d00275688, 1};
    views[n++] = (EmFrhView){context + 0x246Cu, 4, (uint8_t *)(uintptr_t)light->world.ctx_246C, 0};
    if (light->world.ctx_2380)
        views[n++] = (EmFrhView){context + 0x2380u, 64, (uint8_t *)(uintptr_t)light->world.ctx_2380, 0};
    views[n++] = (EmFrhView){SPR_A, 64, (uint8_t *)a, 1};
    views[n++] = (EmFrhView){SPR_B, 64, (uint8_t *)b, 1};
    views[n++] = (EmFrhView){token, 16, rgb, 0};
    EmFrh h;
    memset(&h, 0, sizeof h);
    h.views = views;
    h.view_count = n;
    h.workers.ctx = &l;
    h.workers.w_001D8130 = w_8130;
    h.workers.w_001D8340 = w_8340;
    h.workers.w_001D8690 = w_8690;
    /* a0: the original's stack address of the matrix row 3; em_frh only
     * forwards it (to 001D8130 and 001D8340), so 0 stands for it. */
    if (em_frh_001D88B0(&h, 0, SPR_A, SPR_B, token) < 0) return fault(s, 0x001D88B0u, EM_FACE_ATTACH_FAULT_WORKER);
    return 0;
}

/* ======================================================================
 * 001CB3C0: the attachment draw
 * ==================================================================== */

/* Views 001D88B0's path needs for the mode it will read (so a view fault
 * leaves nothing written). */
static int light_ready(const EmFaceAttach *s)
{
    const EmActorLight *l = s->world.light;
    if (!l) return 0;
    const EmActorLightWorld *w = &l->world;
    if (!w->ctx_246C || !w->d00275688 || !w->d00817BC0) return 0;
    int32_t mode = *w->ctx_246C;
    if (mode == 1 || mode == 3) return 1;
    if (mode == 4 || mode == 5 || mode == 6) return w->ctx_2380 != NULL;
    /* the rig path: 001D8130's lookup, 001D8340's camera fill */
    if (!w->ctx_000C || !w->d00251C50 || !w->d00810610) return 0;
    return (*w->ctx_000C & 0x100u) || w->d00810700;
}

int em_face_attach_001CB3C0(EmFaceAttach *s, uint32_t owner)
{
    if (!s) return -1;
    if (latched(s)) return -1;
    const EmFaceAttachWorld *w = &s->world;
    EmAnimRest *r = w->anim;
    if (!r || !r->world.channel || !r->world.scratch) return fault(s, 0x001CB3C0u, EM_FACE_ATTACH_FAULT_NULL);
    if (r->workers.w_001D88B0 != em_face_attach_w_001D88B0 || r->workers.ctx != (void *)s)
        return fault(s, 0x001D88B0u, EM_FACE_ATTACH_FAULT_NULL);
    if (!w->draw || w->draw->channel != r->world.channel) return fault(s, 0x00275670u, EM_FACE_ATTACH_FAULT_NULL);
    if (!w->d00250FB0) return fault(s, 0x00250FB0u, EM_FACE_ATTACH_FAULT_NULL);
    if (!w->d00275B40) return fault(s, 0x00275B40u, EM_FACE_ATTACH_FAULT_NULL);
    if (!s->workers.w_001D1F80) return fault(s, 0x001D1F80u, EM_FACE_ATTACH_FAULT_NULL);
    if (!light_ready(s)) return fault(s, 0x001D88B0u, EM_FACE_ATTACH_FAULT_NULL);

    /* The record bytes read: +0x80..+0x8F (001D88B0's token), +0x90, +0x94. */
    const uint8_t *rec = map(s, owner + 0x80u, 0x18);
    if (!rec) return fault(s, 0x001CB3D0u, EM_FACE_ATTACH_FAULT_BAD_INDEX);
    const u32 p = rd32(rec + 0x10);                              /* 0x001CB3D0: *(owner + 0x90) */
    const int16_t bone = (int16_t)(rec[0x14] | rec[0x15] << 8);  /* 0x001CB410: the signed halfword +0x94 */
    if (bone < 0 || (u32)bone >= w->d00275B40_count || !w->d00275B40[bone])
        return fault(s, 0x00275B40u, EM_FACE_ATTACH_FAULT_BAD_INDEX);
    /* The attachment slot's two weight quadwords (001CB2C0's 00102948 loads
     * ignore the low four bits) and +0x60 (0x001CB460). */
    if (!map(s, (p + 0x40u) & ~15u, 16) || !map(s, (p + 0x50u) & ~15u, 16))
        return fault(s, 0x00102948u, EM_FACE_ATTACH_FAULT_BAD_INDEX);
    /* Slot +0x60 is checked here but read where the original reads it,
     * after 001D1F80 (0x001CB460, the delay slot of the 001D3F50 call). The
     * face +0x04 check below uses the entry value; if a callee rewrote +0x60,
     * 001D3E40's own check refuses an unmapped face before its first tag. */
    const uint8_t *pm = map(s, p + 0x60u, 4);
    if (!pm) return fault(s, 0x001CB460u, EM_FACE_ATTACH_FAULT_BAD_INDEX);
    const u32 face_at_entry = rd32(pm);
    const EmOwnerDrawWorld *d = w->draw;
    if (!d->ctx_9C || !d->d00275674 || !d->ctx_50) return fault(s, 0x00275670u, EM_FACE_ATTACH_FAULT_NULL);
    if (d->channel_count < 1 || d->ctx_50_count < 1 || r->world.channel_count < 1)
        return fault(s, 0x001CB3C0u, EM_FACE_ATTACH_FAULT_BAD_INDEX);
    if (!map(s, face_at_entry + 4u, 4)) return fault(s, 0x001D3F08u, EM_FACE_ATTACH_FAULT_BAD_INDEX);
    u32 flag = 0;
    if (query_2910(s, &flag) < 0) return -1;     /* sizes the unit (see submit_ready) */
    const EmOwnerServicesChannel *c = &d->channel[0];
    const u32 bytes = flag ? EM_FACE_ATTACH_UNIT_BYTES : EM_FACE_ATTACH_UNIT_BYTES_REF2;
    if (!c->cursor || !c->end || c->cursor > c->end || (size_t)(c->end - c->cursor) < bytes)
        return fault(s, 0x001CB3C0u, EM_FACE_ATTACH_FAULT_BAD_INDEX);

    /* 001029C0(local) (0x001CB3D8): the identity. */
    union { float f[16]; u32 u[16]; } m;
    memset(&m, 0, sizeof m);
    s->frame = m.u;
    trace(s, 0x001029C0u, 0, 0, 0, 0, 0);
    if (em_owner_services_identity_001029C0(m.f) != EM_EE_FLOAT_OK) {
        s->frame = NULL;
        return fault(s, 0x001029C0u, EM_FACE_ATTACH_FAULT_WORKER);
    }
    /* Row 3 xyz = D_00250FB0 / B4 / B8, raw words (0x001CB3E4..0x001CB40C). */
    m.u[12] = w->d00250FB0[0];
    m.u[13] = w->d00250FB0[1];
    m.u[14] = w->d00250FB0[2];

    /* 001026D0(local, D_00275B40[+0x94] + 0x90, local) (0x001CB420): the
     * node's world matrix rows first, then each local row through them. */
    u32 node[16];
    memcpy(node, w->d00275B40[bone]->world, sizeof node);
    trace(s, 0x001026D0u, 0, 0, 0, 0, 0);
    if (em_sdk_vu0_001026D0(m.u, node, m.u) != EM_EE_FLOAT_OK) {
        s->frame = NULL;
        return fault(s, 0x001026D0u, EM_FACE_ATTACH_FAULT_WORKER);
    }

    /* 001C7900(local, owner + 0x80, 0x3F5, 0) (0x001CB434). */
    trace(s, 0x001C7900u, 0, owner + 0x80u, EM_FACE_ATTACH_COLOR_VU, 0, 0);
    uint8_t *first = NULL;
    if (em_anim_rest_001C7900(r, m.u, owner + 0x80u, EM_FACE_ATTACH_COLOR_VU, 0, &first) < 0) {
        s->frame = NULL;
        return fault(s, 0x001C7900u, EM_FACE_ATTACH_FAULT_WORKER);
    }

    /* 001CB2C0(owner, 0x3F3, 0) (0x001CB444). */
    trace(s, 0x001CB2C0u, owner, EM_FACE_ATTACH_WEIGHTS_VU, 0, 0, 0);
    if (em_anim_rest_001CB2C0(r, owner, EM_FACE_ATTACH_WEIGHTS_VU, 0) < 0) {
        s->frame = NULL;
        return fault(s, 0x001CB2C0u, EM_FACE_ATTACH_FAULT_WORKER);
    }

    /* 001D1F80(0, 1, 0) (0x001CB454). */
    trace(s, 0x001D1F80u, 0, 1, 0, 0, 0);
    if (s->workers.w_001D1F80(s->workers.ctx, 0, 1, 0) < 0) {
        s->frame = NULL;
        return fault(s, 0x001D1F80u, EM_FACE_ATTACH_FAULT_WORKER);
    }

    /* 001D3F50(*(p + 0x60)) (0x001CB45C): p as read on entry, +0x60 read
     * now (0x001CB460). */
    const u32 face = rd32(pm);
    trace(s, 0x001D3F50u, face, 0, 0, 0, 0);
    int rc = em_face_attach_001D3F50(s, face);
    s->frame = NULL;
    return rc;
}
