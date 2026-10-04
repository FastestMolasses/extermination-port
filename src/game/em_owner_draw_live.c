/* em_owner_draw_live.c - see em_owner_draw_live.h and docs/OWNER_DRAW.md. */
#include "game/em_owner_draw_live.h"
#include "game/em_world_textures_live.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/em_actor_light_001D89D0.h"
#include "game/em_area01_math_owner.h"
#include "game/em_anim_runtime_rest.h"
#include "game/em_face_attach.h"
#include "game/em_frame.h"
#include "game/em_game_internal.h"
#include "game/em_object_unit.h"
#include "game/em_area11_roger.h"
#include "game/em_packet_chain_original.h"
#include "game/em_render_context_live.h"

/* Original addresses of the canonical storage this module reads through the
 * render context (em_render_context_live.h lists its ranges). */
#define D_00810610 0x00810610u   /* the view matrix (the live camera's pool) */
#define D_00810700 0x00810700u   /* area / sub-area bytes */
#define D_00251C50 0x00251C50u   /* the room rig table: 45 x 0x78 */
#define D_00253170 0x00253170u   /* 001D8340's fold seed */
#define D_00275674 0x00275674u   /* the GS block (arena) address word */
#define D_00275688 0x00275688u   /* the rig pointer word */
#define D_00250FB0 0x00250FB0u   /* 001CB3C0's attachment offset (3 words, render_context.emrc) */
#define SPR_3AC0 0x70003AC0u     /* the view-projection 001D1C50 copies */
#define CTX EM_RCL_CONTEXT
#define ARENA_END 0x007635C0u    /* the packet arena ends where the chain table starts */
#define POINT_LIGHT_WORDS (32u * 32u)
#define EM_OWNER_DRAW_LIVE_PAGE_UNITS 16u

static struct {
    /* D_00817BC0, the rig record 001D8130 loads and 001D8340 / 001D8690
     * read. Only 001D89D0's chain reads or writes it. */
    uint32_t rig[EM_ACTOR_LIGHT_RIG_WORDS];
    const uint32_t *points;               /* context +0x220 (the point-light slots), per draw */
    EmActorLight light;
    EmOwnerDraw draw;
    EmOwnerServices services;
    EmOwnerServicesScratch spr;
    EmOwnerServicesChannel channel;
    uint32_t cursor_address;              /* the channel's original address at `channel.cursor` base */
    uint8_t *cursor_base;
    const uint32_t *owner_rgb;
    /* The frame's units, and the calls' log (this frame's, the last drawn
     * frame's). */
    EmObjectUnitPieces units[EM_OWNER_DRAW_LIVE_UNITS];
    uint32_t unit_count;
    EmOwnerDrawLiveLog log[EM_OWNER_DRAW_LIVE_UNITS], last[EM_OWNER_DRAW_LIVE_UNITS];
    uint32_t log_count, last_count;
    uint32_t unit_frame;
    /* The frame's units drawn so far (em_owner_draw_live_flush_walk), and
     * the first unit the post-step 0015C160 built (post_frame's). */
    uint32_t drawn;
    uint32_t post_first, post_frame;
    const EmWorldModels *bank;
    /* 001CB3C0 (the +0x90 attachment): the anim rest whose region table
     * serves its EE-address reads (the call's regions), and the face
     * attachment over this module's views. */
    EmAnimRest rest;
    EmFaceAttach face;
    uint32_t record;                      /* the owner's record address (001CB3C0 a0) */
    const EmOwnerDrawLiveRegion *regions;
    unsigned region_count;
    /* The attached calls' samples (this frame's, the last drawn frame's). */
    EmOwnerDrawLiveSample sample[EM_OWNER_DRAW_LIVE_SAMPLES], last_sample[EM_OWNER_DRAW_LIVE_SAMPLES];
    uint32_t sample_count, last_sample_count;
    /* 001CABA0: the four channel slots (only channel 3, context +0x1C, is
     * opened), and this frame's class-2 units by address. */
    EmOwnerServicesChannel channels[4];
    uint32_t ch3_address;
    uint8_t *ch3_base;
    EmObjectUnitPieces page_units[EM_OWNER_DRAW_LIVE_PAGE_UNITS];
    uint32_t page_address[EM_OWNER_DRAW_LIVE_PAGE_UNITS];
    uint32_t page_count, page_frame;
    const EmOwnerServicesOwner *page_owner;
    uint32_t page_record;
    EmOwnerDrawLivePageLog page_log[EM_OWNER_DRAW_LIVE_PAGE_LOG];
    uint32_t page_log_count;
    /* 001F3E30's mode-0 draw: the library models it bound (Roger export),
     * the open unit's start (001C7900's) and the token's bytes. */
    EmWorldModels library;
    uint32_t open_start, open_frame;
    int open;
    EmOwnerDrawLiveRegion token_region;
} L;

static int report(uint32_t address, const char *what)
{
    fprintf(stderr, "em_owner_draw_live: %s (%08X)\n", what, (unsigned)address);
    return -1;
}

static const uint32_t *words(uint32_t address, uint32_t size)
{
    return (const uint32_t *)(const void *)em_rcl_bytes(address, size);
}

static uint32_t *words_mut(uint32_t address, uint32_t size)
{
    return (uint32_t *)(void *)em_rcl_bytes_mut(address, size);
}

/* ------------------------------------------------ the channel cursor */

/* The context's channel-0 cursor word (+0x10) as a host channel; the words
 * between the cursor and the arena's end are the channel's room. */
static int channel_open(void)
{
    uint32_t *cursor = words_mut(CTX + 0x10u, 4);
    if (!cursor || *cursor >= ARENA_END) return -1;
    uint8_t *p = em_rcl_bytes_mut(*cursor, ARENA_END - *cursor);
    if (!p) return -1;
    L.cursor_address = *cursor;
    L.cursor_base = p;
    L.channel.cursor = p;
    L.channel.end = p + (ARENA_END - *cursor);
    return 0;
}

static uint32_t channel_address(void)
{
    return L.cursor_address + (uint32_t)(L.channel.cursor - L.cursor_base);
}

static int channel_store(void)
{
    uint32_t *cursor = words_mut(CTX + 0x10u, 4);
    if (!cursor) return -1;
    *cursor = channel_address();
    return 0;
}

/* ------------------------------------------------ the workers */

static int w_001CA7B0(void *ctx, const float position[3], uint32_t radius, int32_t *flags)
{
    (void)ctx;
    uint32_t p[4] = {0, 0, 0, 0};
    memcpy(p, position, 12);
    return em_owner_draw_001CA7B0(&L.draw, p, radius, flags);
}

static int w_001D8C20(void *ctx, int32_t mode)
{
    (void)ctx;
    uint32_t *m = words_mut(CTX + 0x246Cu, 4);
    if (!m) return -1;
    *m = (uint32_t)mode;
    return 0;
}

static int w_owner_rgb(void *ctx, const EmOwnerServicesOwner *owner, uint32_t rgb[4])
{
    (void)ctx;
    (void)owner;
    if (!L.owner_rgb) return -1;
    memcpy(rgb, L.owner_rgb, 16);
    return 0;
}

static int w_001D1F80(void *ctx, int32_t a0, int32_t a1, int32_t a2)
{
    (void)ctx;
    /* The veil module writes at the context's cursor word: hand it over and
     * take it back. */
    if (channel_store() < 0) return -1;
    if (em_rcl_001D1F80(a0, a1, a2) < 0) return -1;
    const uint32_t *cursor = words(CTX + 0x10u, 4);
    if (!cursor || *cursor < L.cursor_address || *cursor > ARENA_END) return -1;
    L.channel.cursor = L.cursor_base + (*cursor - L.cursor_address);
    return 0;
}

static int w_001CA940(void *ctx, int32_t flags, const EmOwnerModel *model)
{
    (void)ctx;
    return em_owner_draw_001CA940(&L.draw, flags, model);
}

/* The face attachment's 001D1F80(0, 1, 0): the same cursor handover. */
static int face_1F80(void *ctx, int32_t a0, int32_t a1, int32_t a2)
{
    return w_001D1F80(ctx, a0, a1, a2);
}

/* The face attachment's 001D2910: the live render context's. */
static int face_2910(void *ctx, int32_t a0, uint32_t *result)
{
    (void)ctx;
    return em_rcl_001D2910(a0, result);
}

/* 001CB3C0(owner): em_face_attach_001CB3C0 over this module's channel,
 * scratch, owner-draw and light views (the rig record of 001D89D0), with
 * the call's regions as the EE bytes it reads by address (the record's
 * +0x80..+0x97, the attachment slot, the face resource), D_00250FB0 from
 * the render context's .data and the owner's node array as D_00275B40. */
static int face_bind(EmOwnerServicesOwner *owner)
{
    const uint32_t *offset = words(D_00250FB0, 12);
    if (!offset) return report(D_00250FB0, "D_00250FB0 is not in the render context's .data");
    if (!L.regions || L.region_count > EM_POSE_REGION_MAX)
        return report(0x001CB3C0u, "an owner with +0x90 != 0 drawn without the attachment's regions");
    EmAnimRest *r = &L.rest;
    memset(r, 0, sizeof *r);
    for (unsigned i = 0; i < L.region_count; ++i)
        r->world.region[i] = (EmPoseRegion){L.regions[i].address, L.regions[i].size,
                                            (uint8_t *)(uintptr_t)L.regions[i].bytes, 0};
    r->world.region_count = L.region_count;
    r->world.channel = &L.channel;
    r->world.channel_count = 1;
    r->world.scratch = &L.spr;
    r->workers.ctx = &L.face;
    r->workers.w_001D88B0 = em_face_attach_w_001D88B0;
    EmFaceAttach *f = &L.face;
    memset(f, 0, sizeof *f);
    f->world.anim = r;
    f->world.draw = &L.draw.world;
    f->world.light = &L.light;
    f->world.context_address = CTX;
    f->world.d00250FB0 = offset;
    f->world.d00275B40 = owner->bone;
    f->world.d00275B40_count = owner->bone_count;
    f->workers.w_001D1F80 = face_1F80;
    f->workers.w_001D2910 = face_2910;
    return 0;
}

static int w_001CB3C0(void *ctx, EmOwnerServicesOwner *owner)
{
    (void)ctx;
    if (face_bind(owner) < 0) return -1;
    EmFaceAttach *f = &L.face;
    EmAnimRest *r = &L.rest;
    if (em_face_attach_001CB3C0(f, L.record) < 0) {
        fprintf(stderr, "em_owner_draw_live: 001CB3C0 faulted: %08X/%d (anim rest %08X/%d, light %08X/%d)\n",
                (unsigned)f->fault.address, (int)f->fault.code, (unsigned)r->fault.address, (int)r->fault.code,
                (unsigned)L.light.fault.address, (int)L.light.fault.code);
        return -1;
    }
    return 0;
}

/* Every view, re-pointed before each draw (the render context's storage is
 * fixed, but its bind can come after this module's first use). */
static int bind_views(const EmWorldModels *bank)
{
    const uint32_t *view = words(D_00810610, 64);
    const uint32_t *planes = words(CTX + 0x2410u, 64);
    const uint32_t *c0c = words(CTX + 0x0Cu, 4), *c9c = words(CTX + 0x9Cu, 4);
    const uint32_t *arena = words(D_00275674, 4);
    uint32_t *c50 = words_mut(CTX + 0x50u, 16);
    uint32_t *mode = words_mut(CTX + 0x246Cu, 4);
    uint32_t *rigword = words_mut(D_00275688, 4);
    const uint32_t *c2380 = words(CTX + 0x2380u, 64);
    const uint32_t *table = words(D_00251C50, EM_ACTOR_LIGHT_TABLE_ENTRIES * 0x78u);
    const uint32_t *seed = words(D_00253170, 16);
    const uint8_t *area = em_rcl_bytes(D_00810700, 2);
    const uint32_t *vp = words(SPR_3AC0, 64);
    if (!view || !planes || !c0c || !c9c || !arena || !c50 || !mode || !rigword || !c2380 || !table ||
        !seed || !area || !vp)
        return report(0x00275670u, "the render context is not loaded and bound (a view is missing)");
    /* The point-light slots are the context's own +0x220 (the one pool,
     * em_rcl_point_lights), loaded with the area's lights. */
    L.points = words(CTX + 0x220u, POINT_LIGHT_WORDS * 4u);
    if (!g.point_lights_loaded || !L.points)
        return report(0x001D8340u, "the point-light pool (context +0x220) is not loaded");

    EmOwnerDrawWorld *d = &L.draw.world;
    d->d00810610 = view;
    d->ctx_2410 = planes;
    d->ctx_0C = c0c;
    d->ctx_9C = c9c;
    d->d00275674 = arena;
    d->channel = &L.channel;
    d->channel_count = 1;
    d->ctx_50 = c50;
    d->ctx_50_count = 4;
    d->models = bank;

    EmActorLightWorld *w = &L.light.world;
    w->d00275688 = rigword;
    w->d00817BC0 = L.rig;
    w->ctx_246C = (const int32_t *)(const void *)mode;
    w->ctx_000C = c0c;
    w->ctx_0220 = L.points;
    w->ctx_2380 = c2380;
    w->d00810700 = area;
    w->d00251C50 = table;
    w->d00253170 = seed;
    w->d00810610 = view;

    memcpy(L.spr.s3AC0, vp, 64);
    return 0;
}

/* ------------------------------------------------ the unit */

/* A REF target: the model bank, then the call's attachment regions (the
 * face resource's blocks, 001D3E40's REF to face + 0x40), then the render
 * context's storage. */
static const uint8_t *resolve(void *ctx, uint32_t address, uint32_t bytes)
{
    const uint8_t *p = em_world_models_bytes(ctx, address, bytes);
    if (p) return p;
    for (unsigned i = 0; i < L.region_count && L.regions; ++i) {
        const EmOwnerDrawLiveRegion *g = &L.regions[i];
        if (g->bytes && bytes <= g->size && address >= g->address && address - g->address <= g->size - bytes)
            return g->bytes + (address - g->address);
    }
    return em_rcl_bytes(address, bytes);
}

static uint8_t *morph_bytes(void *ctx, uint32_t address, uint32_t size, int write)
{
    (void)ctx;
    return write ? NULL : (uint8_t *)(uintptr_t)resolve(NULL, address, size);
}

static int morph_worker(void *ctx, uint32_t function, const uint32_t *a, unsigned na,
                          const uint32_t *f, unsigned nf, uint32_t *v0, uint32_t *f0)
{
    EmOwnerServicesOwner *owner = ctx;
    (void)f; (void)nf; (void)f0; *v0 = 0;
    switch (function) {
    case 0x001C7420u:
        return na == 3 && a[0] == L.record
                   ? em_owner_services_001C7420(&L.services, owner, a[1], a[2], NULL) : -1;
    case 0x001CB2C0u:
        return na == 3 ? em_anim_rest_001CB2C0(&L.rest, a[0], a[1], a[2]) : -1;
    case 0x001D1F80u:
        return na == 3 ? w_001D1F80(NULL, a[0], a[1], a[2]) : -1;
    case 0x001D3F50u:
        return na == 1 ? em_face_attach_001D3F50(&L.face, a[0]) : -1;
    default: return -1;
    }
}

int em_owner_draw_live_001CB360(EmOwnerServicesOwner *owner, const uint32_t rgb[4], uint32_t record,
                                const EmOwnerDrawLiveRegion *regions, unsigned region_count)
{
    if (!owner || !rgb || !regions || !region_count || region_count > EM_OWNER_DRAW_LIVE_REGIONS)
        return report(0x001CB360u, "missing morph-model views");
    if (em_rcl_fault() || bind_views(NULL) < 0 || channel_open() < 0) return -1;
    const uint32_t frame = em_frame_counter();
    if (L.unit_frame != frame) {
        L.unit_frame = frame; L.unit_count = L.log_count = L.sample_count = L.drawn = 0;
    }
    if (L.unit_count >= EM_OWNER_DRAW_LIVE_UNITS) return report(0x001CB360u, "unit storage is full");
    EmActorLightBinding binding = {0};
    binding.light = &L.light; binding.w_owner_rgb = w_owner_rgb;
    L.owner_rgb = rgb; L.record = record; L.regions = regions; L.region_count = region_count;
    memset(&L.light.fault, 0, sizeof L.light.fault);
    memset(&L.draw.fault, 0, sizeof L.draw.fault);
    memset(&L.services, 0, sizeof L.services);
    L.services.world.d00275B40 = owner->bone;
    L.services.world.d00275B40_count = owner->bone_count;
    L.services.world.scratch = &L.spr;
    L.services.world.channel = &L.channel; L.services.world.channel_count = 1;
    L.services.workers.ctx = &binding;
    L.services.workers.w_001D89D0 = em_actor_light_w_001D89D0;
    const uint8_t *start = L.channel.cursor;
    EmA01Math math = {0}; math.ctx = owner; math.view = morph_bytes; math.call = morph_worker;
    int rc = face_bind(owner);
    if (!rc) rc = em_area01_math_001CB360(&math, record);
    if (!rc) rc = channel_store();
    if (!rc) {
        const char *why = NULL;
        EmObjectUnitPieces *unit = &L.units[L.unit_count];
        uint32_t size = (uint32_t)(L.channel.cursor - start);
        rc = em_object_unit_parse_one(start, size, resolve, NULL, unit, &why);
        if (!rc && (unit->bytes != size || unit->unit.program != EM_GFX_OBJECT_FACE)) rc = -1;
        if (!rc) ++L.unit_count;
        else fprintf(stderr, "em_owner_draw_live: 001CB360 unit refused: %s\n", why ? why : "unit shape");
    }
    L.owner_rgb = NULL; L.regions = NULL; L.region_count = 0;
    return rc < 0 ? report(0x001CB360u, "morph-model draw faulted") : 0;
}

static uint32_t fnv(uint32_t h, const uint32_t *w, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i)
        for (unsigned b = 0; b < 4u; ++b) h = (h ^ ((w[i] >> (8u * b)) & 0xFFu)) * 16777619u;
    return h;
}

/* 001D8C20(mode), then 001D89D0(owner, 0x70003400, 0x70003440, owner +
 * 0x80) over the same views (see the header). */
int em_owner_draw_live_light(int32_t mode, const EmOwnerServicesOwner *owner, const uint32_t rgb[4],
                             float a[16], float b[16])
{
    if (!owner || !rgb || !a || !b) return report(0x001D89D0u, "NULL argument");
    if (em_rcl_fault()) return report(em_rcl_fault(), "the render context has faulted");
    if (bind_views(NULL) < 0) return -1;
    if (w_001D8C20(NULL, mode) < 0) return report(0x001D8C20u, "the context +0x246C view is missing");
    static EmActorLightBinding binding;
    binding.light = &L.light;
    binding.ctx = NULL;
    binding.w_owner_rgb = w_owner_rgb;
    L.owner_rgb = rgb;
    memset(&L.light.fault, 0, sizeof L.light.fault);
    const int rc = em_actor_light_w_001D89D0(&binding, owner, a, b);
    L.owner_rgb = NULL;
    if (rc < 0)
        return report(L.light.fault.address, "001D89D0 faulted");
    return 0;
}

/* 001D8C20(mode) alone (001CB4F0's mode reset after its draw). */
int em_owner_draw_live_light_mode(int32_t mode)
{
    if (em_rcl_fault()) return report(em_rcl_fault(), "the render context has faulted");
    if (w_001D8C20(NULL, mode) < 0) return report(0x001D8C20u, "the context +0x246C view is missing");
    return 0;
}

int em_owner_draw_live_001CAA00(const EmWorldModels *bank, EmOwnerServicesOwner *owner,
                                const uint32_t rgb[4], uint32_t record)
{
    return em_owner_draw_live_001CAA00_attached(bank, owner, rgb, record, NULL, 0);
}

/* The bytes of the call's region holding [address, address + size), or
 * NULL. */
static const uint8_t *region_bytes(const EmOwnerDrawLiveRegion *regions, unsigned count, uint32_t address,
                                   uint32_t size)
{
    for (unsigned i = 0; i < count; ++i) {
        const EmOwnerDrawLiveRegion *g = &regions[i];
        if (g->bytes && size <= g->size && address >= g->address && address - g->address <= g->size - size)
            return g->bytes + (address - g->address);
    }
    return NULL;
}

/* An attached call's inputs, before 001CAA00 runs (see the header). */
static EmOwnerDrawLiveSample *sample_inputs(const EmOwnerServicesOwner *owner, uint32_t record,
                                            const EmOwnerDrawLiveRegion *regions, unsigned count)
{
    if (L.sample_count >= EM_OWNER_DRAW_LIVE_SAMPLES) return NULL;
    EmOwnerDrawLiveSample *m = &L.sample[L.sample_count];
    memset(m, 0, sizeof *m);
    m->frame = em_frame_counter();
    m->record = record;
    for (unsigned i = 0; i < count; ++i)
        if (regions[i].address == record && regions[i].bytes) {
            m->record_size = regions[i].size < EM_OWNER_DRAW_LIVE_SAMPLE_RECORD ? regions[i].size
                                                                               : EM_OWNER_DRAW_LIVE_SAMPLE_RECORD;
            memcpy(m->record_bytes, regions[i].bytes, m->record_size);
        }
    m->node_count = owner->bone_count;
    for (uint32_t k = 0; k < owner->bone_count && k < EM_OWNER_SERVICES_MAX_BONES; ++k)
        if (owner->bone[k]) memcpy(m->nodes[k], owner->bone[k]->world, 64);
    const uint8_t *slot = owner->attachment ? region_bytes(regions, count, owner->attachment, 0xD0) : NULL;
    if (slot) {
        m->slot_address = owner->attachment;
        memcpy(m->slot, slot, sizeof m->slot);
    }
    const EmOwnerDrawWorld *d = &L.draw.world;
    memcpy(m->view_810610, d->d00810610, 64);
    memcpy(m->planes_2410, d->ctx_2410, 64);
    memcpy(m->vp_3AC0, L.spr.s3AC0, 64);
    m->ctx_0C = *d->ctx_0C;
    m->ctx_9C = *d->ctx_9C;
    memcpy(m->rig, L.rig, sizeof m->rig);
    m->rig_word = *L.light.world.d00275688;
    memcpy(m->area, L.light.world.d00810700, 2);
    ++L.sample_count;
    return m;
}

static void log_face(EmOwnerDrawLiveLog *log, const EmObjectUnitPieces *p)
{
    uint32_t h = fnv(2166136261u, p->color, 16);
    h = fnv(h, p->nodes, 32);
    log->face = fnv(h, p->weights, 8);
    log->face_bytes = p->bytes;
}

int em_owner_draw_live_001CAA00_attached(const EmWorldModels *bank, EmOwnerServicesOwner *owner,
                                         const uint32_t rgb[4], uint32_t record,
                                         const EmOwnerDrawLiveRegion *regions, unsigned region_count)
{
    if (!bank || !owner || !rgb) return report(0x001CAA00u, "NULL argument");
    if (region_count > EM_OWNER_DRAW_LIVE_REGIONS || (region_count && !regions))
        return report(0x001CB3C0u, "more attachment regions than EM_OWNER_DRAW_LIVE_REGIONS");
    if (em_rcl_fault()) return report(em_rcl_fault(), "the render context has faulted");
    if (bind_views(bank) < 0) return -1;
    if (channel_open() < 0) return report(0x00811CD0u, "the channel-0 cursor is outside the packet arena");
    const uint32_t frame = em_frame_counter();
    if (L.unit_frame != frame) {
        L.unit_frame = frame;
        L.unit_count = 0;
        L.log_count = 0;
        L.sample_count = 0;
        L.drawn = 0;
    }
    if (L.log_count >= EM_OWNER_DRAW_LIVE_UNITS)
        return report(0x001CAA00u, "more 001CAA00 calls in one frame than EM_OWNER_DRAW_LIVE_UNITS");
    EmOwnerDrawLiveLog *log = &L.log[L.log_count++];
    memset(log, 0, sizeof *log);
    log->record = record;
    {
        const float *point = owner->pos;          /* 001CAA00's position */
        if (owner->pose_bone != 0xFF && owner->pose_bone < owner->bone_count && owner->bone[owner->pose_bone])
            point = owner->bone[owner->pose_bone]->world + 12;
        memcpy(log->point, point, sizeof log->point);
        log->pose = 2166136261u;
        for (uint32_t k = 0; k < owner->bone_count && k < EM_OWNER_SERVICES_MAX_BONES; ++k)
            if (owner->bone[k])
                log->pose = fnv(log->pose, (const uint32_t *)(const void *)owner->bone[k]->world, 16);
    }

    static EmActorLightBinding binding;
    binding.light = &L.light;
    binding.ctx = NULL;
    binding.w_owner_rgb = w_owner_rgb;
    L.owner_rgb = rgb;
    memset(&L.light.fault, 0, sizeof L.light.fault);
    memset(&L.draw.fault, 0, sizeof L.draw.fault);

    EmOwnerServices *s = &L.services;
    memset(s, 0, sizeof *s);
    s->world.d00275B40 = owner->bone;              /* 001CB590: the owner's own +0x110 */
    s->world.d00275B40_count = owner->bone_count;
    s->world.scratch = &L.spr;
    s->world.channel = &L.channel;
    s->world.channel_count = 1;
    s->workers.ctx = &binding;
    s->workers.w_001CA7B0 = w_001CA7B0;
    s->workers.w_001D8C20 = w_001D8C20;
    s->workers.w_001D89D0 = em_actor_light_w_001D89D0;
    s->workers.w_001D1F80 = w_001D1F80;
    s->workers.w_001CA940 = w_001CA940;
    s->workers.w_001CB3C0 = w_001CB3C0;

    const uint32_t start = channel_address();
    uint8_t *const unit = L.channel.cursor;
    L.record = record;
    L.regions = regions;
    L.region_count = region_count;
    EmOwnerDrawLiveSample *sample = region_count ? sample_inputs(owner, record, regions, region_count) : NULL;
    const int rc = em_owner_services_001CAA00(s, owner);
    L.owner_rgb = NULL;
    if (rc < 0 || s->fault.code) {
        L.regions = NULL;
        L.region_count = 0;
        fprintf(stderr, "em_owner_draw_live: 001CAA00 faulted: services %08X/%d, draw %08X/%d, light %08X/%d\n",
                (unsigned)s->fault.address, (int)s->fault.code, (unsigned)L.draw.fault.address,
                (int)L.draw.fault.code, (unsigned)L.light.fault.address, (int)L.light.fault.code);
        return -1;
    }
    if (channel_store() < 0) {
        L.regions = NULL;
        L.region_count = 0;
        return report(0x00811CD0u, "the channel-0 cursor could not be stored");
    }
    const uint32_t used = channel_address() - start;
    if (sample) {
        sample->unit_address = start;
        sample->unit_bytes = used < EM_OWNER_DRAW_LIVE_SAMPLE_UNIT ? used : EM_OWNER_DRAW_LIVE_SAMPLE_UNIT;
        memcpy(sample->unit, unit, sample->unit_bytes);
    }
    if (!used) {                                   /* 001CA7B0 culled it, no attachment */
        L.regions = NULL;
        L.region_count = 0;
        return owner->attachment ? report(0x001CB3C0u, "an owner with +0x90 != 0 appended no face unit") : 0;
    }
    /* The bytes are the owner's unit (unless 001CA7B0 culled it), then
     * 001CB3C0's face unit while +0x90 != 0 (appended even for a culled
     * body). Each is parsed in build order and kept for the flush. */
    EmObjectUnitPieces *p = NULL;
    uint32_t at = 0;
    unsigned parsed = 0;
    while (at < used) {
        if (L.unit_count >= EM_OWNER_DRAW_LIVE_UNITS) {
            L.regions = NULL;
            L.region_count = 0;
            return report(0x001CAA00u, "more drawn units in one frame than EM_OWNER_DRAW_LIVE_UNITS");
        }
        const char *why = NULL;
        EmObjectUnitPieces *q = &L.units[L.unit_count];
        const int one = em_object_unit_parse_one(unit + at, used - at, resolve, (void *)bank, q, &why);
        const int face = !one && q->unit.program == EM_GFX_OBJECT_FACE;
        /* The owner unit comes first and never after a face; the face unit
         * only with +0x90 != 0, at most once. */
        if (!one && ((face && !owner->attachment) || (!face && parsed) || (face && at + q->bytes != used))) {
            why = "the owner's bytes are not [owner unit][001CB3C0's face unit]";
        }
        if (one || why) {
            fprintf(stderr, "em_owner_draw_live: the unit at %08X is refused: %s\n", (unsigned)(start + at),
                    why ? why : "(no reason)");
            L.regions = NULL;
            L.region_count = 0;
            return -1;
        }
        if (face) {
            log_face(log, q);
            if (sample) sample->face_bytes = q->bytes;
        } else {
            p = q;
        }
        ++L.unit_count;
        ++parsed;
        at += q->bytes;
    }
    L.regions = NULL;
    L.region_count = 0;
    if (owner->attachment && !log->face_bytes)       /* 001CB3C0 always appends its unit */
        return report(0x001CB3C0u, "an owner with +0x90 != 0 appended no face unit");
    L.bank = bank;
    log->bytes = used - log->face_bytes;
    if (!p) return 0;                              /* the body was culled: the face alone */
    log->clip = p->unit.clip;
    const uint32_t basis = 2166136261u;
    log->points = fnv(basis, L.points, POINT_LIGHT_WORDS);
    log->colour = fnv(basis, p->color, 16);
    log->light = log->position = log->light_rig = basis;
    for (uint32_t k = 0; k < p->unit.node_count; ++k) {
        log->position = fnv(log->position, p->nodes + 32u * k, 16);
        log->light = fnv(log->light, p->nodes + 32u * k + 16u, 16);
        for (uint32_t r = 0; r < 4u; ++r)       /* lanes y, z: the rig slots 1 and 2 */
            log->light_rig = fnv(log->light_rig, p->nodes + 32u * k + 16u + 4u * r + 1u, 2);
    }
    return 0;
}

/* ------------------------------------------------ 001CABA0 (channel 3) */

/* The context's channel-3 cursor word (+0x1C) as host channel 3. */
static int ch3_open(void)
{
    memset(L.channels, 0, sizeof L.channels);
    uint32_t *cursor = words_mut(CTX + 0x1Cu, 4);
    if (!cursor || *cursor >= ARENA_END) return -1;
    uint8_t *p = em_rcl_bytes_mut(*cursor, ARENA_END - *cursor);
    if (!p) return -1;
    L.ch3_address = *cursor;
    L.ch3_base = p;
    L.channels[3].cursor = p;
    L.channels[3].end = p + (ARENA_END - *cursor);
    return 0;
}

static uint32_t ch3_address(const uint8_t *at) { return L.ch3_address + (uint32_t)(at - L.ch3_base); }

static int ch3_store(void)
{
    uint32_t *cursor = words_mut(CTX + 0x1Cu, 4);
    if (!cursor) return -1;
    *cursor = ch3_address(L.channels[3].cursor);
    return 0;
}

/* 001D1F80(3, a1, a2): the veil module writes at context +0x1C. */
static int ch3_1F80(void *ctx, int32_t a0, int32_t a1, int32_t a2)
{
    (void)ctx;
    if (a0 != 3 || ch3_store() < 0) return -1;
    if (em_rcl_001D1F80(a0, a1, a2) < 0) return -1;
    const uint32_t *cursor = words(CTX + 0x1Cu, 4);
    if (!cursor || *cursor < L.ch3_address || *cursor > ARENA_END) return -1;
    L.channels[3].cursor = L.ch3_base + (*cursor - L.ch3_address);
    return 0;
}

static int w_001D3990(void *ctx, const EmOwnerModel *model)
{
    (void)ctx;
    const EmWorldModel *m = L.bank ? em_world_models_of(L.bank, model) : NULL;
    if (!m) return report(0x001D3990u, "001D3990 of a model outside the owner's bank");
    return em_owner_draw_001D3900(&L.draw, 3, m->address, m->w04, ch3_1F80, NULL);
}

static int w_001D3D90(void *ctx, const EmOwnerModel *model)
{
    (void)ctx;
    const EmWorldModel *m = L.bank ? em_world_models_of(L.bank, model) : NULL;
    if (!m) return report(0x001D3D90u, "001D3D90 of a model outside the owner's bank");
    return em_owner_draw_001D3CF0(&L.draw, 3, m->address, m->w04, ch3_1F80, NULL);
}

int em_owner_draw_live_001D3990_at(uint32_t model, uint32_t w04)
{
    if (em_rcl_fault() || ch3_open()<0) return -1;
    /* This submit needs no actor, animation, rig, or parsed model bank. */
    EmOwnerDraw draw={0};
    draw.world.ctx_0C=words(CTX+0x0Cu,4);
    draw.world.ctx_9C=words(CTX+0x9Cu,4);
    draw.world.d00275674=words(D_00275674,4);
    draw.world.ctx_50=words_mut(CTX+0x50u,16);
    draw.world.ctx_50_count=4;
    draw.world.channel=L.channels; draw.world.channel_count=4;
    if (em_owner_draw_001D3900(&draw,3,model,w04,ch3_1F80,NULL)<0)
        return report(draw.fault.address,"001D3990 raw-model submit faulted");
    return ch3_store();
}

/* Parse into the next slot, but publish its address only after the caller's
 * original work succeeds. This cache owns no original packet bytes. */
static int page_parse(uint32_t start, uint32_t end, EmObjectUnitResolve resolver, void *ctx, int light)
{
    if (!resolver || end<start || end-start<16)
        return report(start,"raw-model page unit has missing inputs");
    uint32_t size=end-start;
    const uint8_t *unit=em_rcl_bytes(start,size);
    uint16_t qwc=1; uint32_t target=1;
    if (unit) { memcpy(&qwc,unit+size-16,2);memcpy(&target,unit+size-12,4); }
    if (!unit || unit[size-13]!=0x60u || qwc || target)
        return report(start,"raw-model unit does not end in RET");
    uint32_t frame=em_frame_counter();
    if (L.page_frame!=frame) { L.page_frame=frame; L.page_count=0; L.page_log_count=0; }
    if (L.page_count>=EM_OWNER_DRAW_LIVE_PAGE_UNITS)
        return report(start,"raw-model page unit cache is full");
    const char *why=NULL;
    EmObjectUnitPieces *q=&L.page_units[L.page_count];
    int rc=light ? em_object_unit_parse_light(unit,size-16,resolver,ctx,q,&why)
                 : em_object_unit_parse(unit,size-16,resolver,ctx,q,&why);
    if (rc<0 || q->unit.gs_class!=2u) {
        fprintf(stderr,"em_owner_draw_live: raw-model unit at %08X refused: %s\n",(unsigned)start,
                why ? why : "not a class-2 unit");
        return -1;
    }
    return 0;
}

int em_owner_draw_live_page_register(uint32_t start, uint32_t end,
                                      EmObjectUnitResolve resolver, void *ctx)
{
    if (em_rcl_fault() || page_parse(start,end,resolver,ctx,0)<0) return -1;
    L.page_address[L.page_count++]=start;
    return 0;
}

int em_owner_draw_live_001CAAC0_packet(const uint32_t position[4], uint32_t start,
                                      EmObjectUnitResolve resolver, void *ctx)
{
    const uint32_t *cursor=words(CTX+0x1Cu,4), *vp=words(SPR_3AC0,64);
    EmPacketChain *pc=em_rcl_packet_chain();
    if (!position || !cursor || !vp || !pc)
        return report(0x001CAAC0u,"raw-model page insertion has missing inputs");
    if (page_parse(start,*cursor,resolver,ctx,1)<0) return -1;
    EmOwnerServicesScratch scratch={0}; memcpy(scratch.s3AC0,vp,64);
    EmAnimRest rest={0};
    rest.world.region[0]=(EmPoseRegion){0x7F000000u,16,(uint8_t *)(const void *)position,0};
    rest.world.region_count=1; rest.world.scratch=&scratch;
    rest.workers.ctx=pc; rest.workers.w_001CB760=em_packet_chain_w_001CB760;
    if (em_anim_rest_001CAAC0(&rest,0x7F000000u,start,NULL)<0)
        return report(rest.fault.address,"raw-model depth insertion faulted");
    L.page_address[L.page_count++]=start;
    return 0;
}

/* 001CAAC0(owner + 0xB0, node): em_anim_rest_001CAAC0 over the owner's
 * +0xB0 quadword and the scratchpad view-projection, its 001CB760 the
 * render context's packet chain (the page D_007635C0). */
static int w_001CAAC0(void *ctx, const EmOwnerServicesOwner *owner, const uint8_t *node)
{
    (void)ctx;
    EmPacketChain *pc = em_rcl_packet_chain();
    if (!pc || !owner || L.page_owner != owner) return -1;
    EmAnimRest *r = &L.rest;
    memset(r, 0, sizeof *r);
    r->world.region[0] = (EmPoseRegion){L.page_record + 0xB0u, 16, (uint8_t *)(uintptr_t)owner->pos, 0};
    r->world.region_count = 1;
    r->world.scratch = &L.spr;
    r->workers.ctx = pc;
    r->workers.w_001CB760 = em_packet_chain_w_001CB760;
    int32_t key = 0;
    if (em_anim_rest_001CAAC0(r, L.page_record + 0xB0u, ch3_address(node), &key) < 0)
        return report(r->fault.address, "001CAAC0 faulted");
    return 0;
}

int em_owner_draw_live_001CABA0(const EmWorldModels *bank, EmOwnerServicesOwner *owner,
                                const uint32_t rgb[4], uint32_t record)
{
    if (!bank || !owner || !rgb) return report(0x001CABA0u, "NULL argument");
    if (em_rcl_fault()) return report(em_rcl_fault(), "the render context has faulted");
    if (bind_views(bank) < 0) return -1;
    if (ch3_open() < 0) return report(0x00811CDCu, "the channel-3 cursor is outside the packet arena");
    const uint32_t frame = em_frame_counter();
    if (L.page_frame != frame) {
        L.page_frame = frame;
        L.page_count = 0;
        L.page_log_count = 0;
    }
    if (L.page_log_count >= EM_OWNER_DRAW_LIVE_PAGE_LOG)
        return report(0x001CABA0u, "more 001CABA0 calls in one frame than EM_OWNER_DRAW_LIVE_PAGE_LOG");
    EmOwnerDrawLivePageLog *log = &L.page_log[L.page_log_count++];
    memset(log, 0, sizeof *log);
    log->record = record;
    memcpy(log->rgb, rgb, sizeof log->rgb);
    if (L.page_count >= EM_OWNER_DRAW_LIVE_PAGE_UNITS)
        return report(0x001CABA0u, "more 001CABA0 units in one frame than EM_OWNER_DRAW_LIVE_PAGE_UNITS");
    L.bank = bank;
    EmOwnerDrawWorld *d = &L.draw.world;
    d->channel = L.channels;
    d->channel_count = 4;
    static EmActorLightBinding binding;
    binding.light = &L.light;
    binding.ctx = NULL;
    binding.w_owner_rgb = w_owner_rgb;
    L.owner_rgb = rgb;
    memset(&L.light.fault, 0, sizeof L.light.fault);
    memset(&L.draw.fault, 0, sizeof L.draw.fault);
    EmOwnerServices *s = &L.services;
    memset(s, 0, sizeof *s);
    s->world.d00275B40 = owner->bone;              /* 001CB590: the owner's own +0x110 */
    s->world.d00275B40_count = owner->bone_count;
    s->world.scratch = &L.spr;
    s->world.channel = L.channels;
    s->world.channel_count = 4;
    s->workers.ctx = &binding;
    s->workers.w_001CA7B0 = w_001CA7B0;
    s->workers.w_001D8C20 = w_001D8C20;
    s->workers.w_001D89D0 = em_actor_light_w_001D89D0;
    s->workers.w_001D3990 = w_001D3990;
    s->workers.w_001D3D90 = w_001D3D90;
    s->workers.w_001CAAC0 = w_001CAAC0;
    L.page_owner = owner;
    L.page_record = record;
    const uint32_t start = L.ch3_address;
    uint8_t *const unit = L.channels[3].cursor;
    const int rc = em_owner_services_001CABA0(s, owner, owner->model);
    L.owner_rgb = NULL;
    L.page_owner = NULL;
    d->channel = &L.channel;                       /* 001CAA00's single channel 0 */
    d->channel_count = 1;
    if (rc < 0 || s->fault.code) {
        fprintf(stderr, "em_owner_draw_live: 001CABA0 faulted: services %08X/%d, draw %08X/%d, light %08X/%d\n",
                (unsigned)s->fault.address, (int)s->fault.code, (unsigned)L.draw.fault.address,
                (int)L.draw.fault.code, (unsigned)L.light.fault.address, (int)L.light.fault.code);
        return -1;
    }
    if (ch3_store() < 0) return report(0x00811CDCu, "the channel-3 cursor could not be stored");
    const uint32_t used = ch3_address(L.channels[3].cursor) - start;
    log->bytes = used;
    if (!used) return 0;                           /* 001CA7B0 culled it */
    /* [the class-2 unit][the RET tag]: the page CALLs the unit's start. */
    if (used < 0x10u || unit[used - 0x10u + 3u] != 0x60u)
        return report(start, "001CABA0's bytes do not end in its RET tag");
    const char *why = NULL;
    EmObjectUnitPieces *q = &L.page_units[L.page_count];
    if (em_object_unit_parse(unit, used - 0x10u, resolve, (void *)bank, q, &why) < 0 ||
        q->unit.gs_class != 2u) {
        fprintf(stderr, "em_owner_draw_live: the 001CABA0 unit at %08X is refused: %s\n", (unsigned)start,
                why ? why : "not a class-2 unit");
        return -1;
    }
    L.page_address[L.page_count++] = start;
    log->clip = q->unit.clip;
    const uint32_t basis = 2166136261u;
    log->colour = fnv(basis, q->color, 16);
    log->light = log->position = basis;
    for (uint32_t k = 0; k < q->unit.node_count; ++k) {
        log->position = fnv(log->position, q->nodes + 32u * k, 16);
        log->light = fnv(log->light, q->nodes + 32u * k + 16u, 16);
    }
    return 0;
}

/* ------------------------------------------------ 001F3E30's mode-0 draw */

int em_owner_draw_live_001CA7B0(const uint32_t position[4], uint32_t radius, int32_t *flags)
{
    if (!position || !flags) return report(0x001CA7B0u, "NULL argument");
    if (em_rcl_fault()) return report(em_rcl_fault(), "the render context has faulted");
    if (bind_views(NULL) < 0) return -1;
    memset(&L.draw.fault, 0, sizeof L.draw.fault);
    return em_owner_draw_001CA7B0(&L.draw, position, radius, flags) < 0
               ? report(L.draw.fault.address, "001CA7B0 faulted")
               : 0;
}

int em_owner_draw_live_001C7900(const uint32_t m[16], uint32_t token, const uint8_t token_bytes[16], int32_t vuaddr,
                                int32_t chan)
{
    if (!m || !token_bytes || (chan != 0 && chan != 3))
        return report(0x001C7900u, "001C7900 outside channels 0/3, or NULL");
    if (em_rcl_fault()) return report(em_rcl_fault(), "the render context has faulted");
    if (bind_views(NULL) < 0) return -1;
    if ((chan == 3 ? ch3_open() : channel_open()) < 0)
        return report(CTX + 0x10u + 4u * (uint32_t)chan, "the upload cursor is outside the packet arena");
    const uint32_t frame = em_frame_counter();
    if (chan == 0 && L.unit_frame != frame) {
        L.unit_frame = frame;
        L.unit_count = 0;
        L.log_count = 0;
        L.sample_count = 0;
        L.drawn = 0;
    }
    /* em_anim_rest_001C7900 with em_face_attach's 001D88B0 (as 001CB3C0's
     * 001C7900): the token's 16 bytes are its only EE read. */
    if (chan == 0) L.token_region = (EmOwnerDrawLiveRegion){token, 16, token_bytes};
    EmAnimRest *r = &L.rest;
    memset(r, 0, sizeof *r);
    r->world.region[0] = (EmPoseRegion){token, 16, (uint8_t *)(uintptr_t)token_bytes, 0};
    r->world.region_count = 1;
    r->world.channel = chan == 3 ? L.channels : &L.channel;
    r->world.channel_count = chan == 3 ? 4 : 1;
    r->world.scratch = &L.spr;
    r->workers.ctx = &L.face;
    r->workers.w_001D88B0 = em_face_attach_w_001D88B0;
    EmFaceAttach *f = &L.face;
    memset(f, 0, sizeof *f);
    f->world.anim = r;
    f->world.draw = &L.draw.world;
    f->world.light = &L.light;
    f->world.context_address = CTX;
    memset(&L.light.fault, 0, sizeof L.light.fault);
    uint8_t *first = NULL;
    const uint32_t start = chan == 3 ? ch3_address(L.channels[3].cursor) : channel_address();
    if (em_anim_rest_001C7900(r, m, token, vuaddr, chan, &first) < 0 || f->fault.code) {
        fprintf(stderr, "em_owner_draw_live: 001C7900 faulted: %08X/%d (light %08X/%d, face %08X/%d)\n",
                (unsigned)r->fault.address, (int)r->fault.code, (unsigned)L.light.fault.address,
                (int)L.light.fault.code, (unsigned)f->fault.address, (int)f->fault.code);
        return -1;
    }
    if ((chan == 3 ? ch3_store() : channel_store()) < 0)
        return report(CTX + 0x10u + 4u * (uint32_t)chan, "the upload cursor could not be stored");
    if (chan == 0) {
        L.open = 1;
        L.open_start = start;
        L.open_frame = frame;
    }
    return 0;
}

int em_owner_draw_live_001CA940_library(int32_t flags, uint32_t model)
{
    if (!L.open || L.open_frame != em_frame_counter())
        return report(0x001CA940u, "001CA940 without its 001C7900 in the same frame");
    L.open = 0;
    if (bind_views(&L.library) < 0) return -1;
    /* The model (header, blocks, skeleton records) from the Roger export,
     * at its library address. */
    const uint8_t *m = em_area11_roger_resource(model, 0x40);
    if (!m) return report(model, "001CA940 of a model outside the Roger export");
    const uint32_t bones = (uint32_t)m[8] | (uint32_t)m[9] << 8 | (uint32_t)m[10] << 16 | (uint32_t)m[11] << 24;
    const uint32_t skeleton = (uint32_t)m[12] | (uint32_t)m[13] << 8 | (uint32_t)m[14] << 16 | (uint32_t)m[15] << 24;
    if (bones > 0xFFu || skeleton > 0x01000000u) return report(model, "not a block model");
    const EmWorldModel *entry = NULL;
    const uint8_t *all = em_area11_roger_resource(model, skeleton + 0x50u * bones);
    if (!all || em_world_models_add(&L.library, model, all, skeleton + 0x50u * bones, &entry) < 0)
        return report(model, "the library model does not parse");
    if (channel_open() < 0) return report(0x00811CD0u, "the channel-0 cursor is outside the packet arena");
    memset(&L.draw.fault, 0, sizeof L.draw.fault);
    if (em_owner_draw_001CA940_at(&L.draw, flags, entry->address, entry->w04) < 0)
        return report(L.draw.fault.address, "001CA940 faulted");
    if (channel_store() < 0) return report(0x00811CD0u, "the channel-0 cursor could not be stored");
    const uint32_t used = channel_address() - L.open_start;
    if (L.unit_count >= EM_OWNER_DRAW_LIVE_UNITS)
        return report(0x001CA940u, "more drawn units in one frame than EM_OWNER_DRAW_LIVE_UNITS");
    const uint8_t *unit = em_rcl_bytes(L.open_start, used);
    const char *why = NULL;
    EmObjectUnitPieces *q = &L.units[L.unit_count];
    if (!unit || em_object_unit_parse_inherit(unit, used, resolve, (void *)&L.library, q, &why) < 0) {
        fprintf(stderr, "em_owner_draw_live: the 001F3E30 unit at %08X is refused: %s\n", (unsigned)L.open_start,
                why ? why : "unreadable");
        return -1;
    }
    L.bank = &L.library;
    ++L.unit_count;
    return 0;
}

const EmObjectUnitPieces *em_owner_draw_live_page_unit(uint32_t address)
{
    if (L.page_frame != em_frame_counter()) return NULL;
    for (uint32_t i = 0; i < L.page_count; ++i)
        if (L.page_address[i] == address) return &L.page_units[i];
    return NULL;
}

/* ------------------------------------------------ the frame's draw */

static int load_textures(EmGfx *gfx)
{
    return em_world_textures_live_ensure(gfx);
}

int em_owner_draw_live_textures(EmGfx *gfx)
{
    if (!gfx) return report(0, "no graphics device");
    return load_textures(gfx);
}

void em_owner_draw_live_post_step(void)
{
    const uint32_t frame = em_frame_counter();
    L.post_frame = frame;
    L.post_first = L.unit_frame == frame ? L.unit_count : 0;
}

/* Draw the kept units [L.drawn, end) of this frame in build order. */
static int draw_units(EmGfx *gfx, uint32_t end)
{
    const uint32_t frame = em_frame_counter();
    if (L.unit_frame != frame) {                   /* units of an earlier frame are never drawn */
        L.unit_count = 0;
        L.log_count = 0;
        L.sample_count = 0;
        L.drawn = 0;
    }
    if (end > L.unit_count) end = L.unit_count;
    int rc = 0;
    if (L.drawn < end) {
        if (!gfx) rc = report(0, "no graphics device");
        else if (load_textures(gfx) < 0) rc = -1;
        for (uint32_t i = L.drawn; !rc && i < end; ++i)
            if (em_gfx_object_unit(gfx, &L.units[i].unit) < 0)
                rc = report(L.units[i].model_address, "em_gfx_object_unit refused a unit (reason above)");
        L.drawn = end;
    }
    return rc;
}

int em_owner_draw_live_flush_walk(EmGfx *gfx)
{
    const uint32_t frame = em_frame_counter();
    return draw_units(gfx, L.post_frame == frame && L.unit_frame == frame ? L.post_first : UINT32_MAX);
}

int em_owner_draw_live_flush(EmGfx *gfx)
{
    const int rc = draw_units(gfx, UINT32_MAX);
    memcpy(L.last, L.log, sizeof L.log[0] * L.log_count);
    L.last_count = L.log_count;
    L.log_count = 0;
    memcpy(L.last_sample, L.sample, sizeof L.sample[0] * L.sample_count);
    L.last_sample_count = L.sample_count;
    L.sample_count = 0;
    L.unit_count = 0;
    L.drawn = 0;
    return rc;
}

uint32_t em_owner_draw_live_count(void)
{
    return L.unit_frame == em_frame_counter() ? L.unit_count : 0;
}

const EmObjectUnitPieces *em_owner_draw_live_unit(uint32_t i)
{
    return i < em_owner_draw_live_count() ? &L.units[i] : NULL;
}

int em_owner_draw_live_log(EmOwnerDrawLiveLog *out, int capacity)
{
    int n = 0;
    for (uint32_t i = 0; i < L.last_count && n < capacity; ++i) out[n++] = L.last[i];
    return n;
}

int em_owner_draw_live_page_log(EmOwnerDrawLivePageLog *out, int capacity, uint32_t *frame)
{
    if (frame) *frame = L.page_frame;
    int n = 0;
    for (uint32_t i = 0; i < L.page_log_count && n < capacity; ++i) out[n++] = L.page_log[i];
    return n;
}

int em_owner_draw_live_samples(const EmOwnerDrawLiveSample **out)
{
    if (out) *out = L.last_sample;
    return (int)L.last_sample_count;
}

uint8_t *em_owner_draw_live_memory(uint32_t address, uint32_t size)
{
    if (!size) return NULL;
    _Static_assert(offsetof(EmOwnerServicesScratch, s3440) == 0x40, "scratch B offset");
    _Static_assert(offsetof(EmOwnerServicesScratch, s3480) == 0x80, "scratch C offset");
    if (address >= 0x70003400u && address - 0x70003400u < 0xC0u &&
        size <= 0xC0u - (address - 0x70003400u))
        return (uint8_t *)(void *)L.spr.s3400 + address - 0x70003400u;
    if (address >= 0x00817BC0u && address - 0x00817BC0u < sizeof L.rig &&
        size <= sizeof L.rig - (address - 0x00817BC0u))
        return (uint8_t *)(void *)L.rig + address - 0x00817BC0u;
    return NULL;
}

void em_owner_draw_live_reset(void)
{
    L.sample_count = L.last_sample_count = 0;
    L.unit_count = 0;
    L.log_count = L.last_count = 0;
    L.unit_frame = 0;
    L.drawn = 0;
    L.post_frame = L.post_first = 0;
    L.bank = NULL;
}
