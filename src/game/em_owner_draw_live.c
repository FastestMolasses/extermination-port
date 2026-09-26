/* em_owner_draw_live.c - see em_owner_draw_live.h and docs/OWNER_DRAW.md. */
#include "game/em_owner_draw_live.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/em_actor_light_001D89D0.h"
#include "game/em_frame.h"
#include "game/em_game_internal.h"
#include "game/em_object_unit.h"
#include "game/em_render_context_live.h"

/* Original addresses of the canonical storage this module reads through the
 * render context (em_render_context_live.h lists its ranges). */
#define D_00810610 0x00810610u   /* the view matrix (the live camera's pool) */
#define D_00810700 0x00810700u   /* area / sub-area bytes */
#define D_00251C50 0x00251C50u   /* the room rig table: 45 x 0x78 */
#define D_00253170 0x00253170u   /* 001D8340's fold seed */
#define D_00275674 0x00275674u   /* the GS block (arena) address word */
#define D_00275688 0x00275688u   /* the rig pointer word */
#define SPR_3AC0 0x70003AC0u     /* the view-projection 001D1C50 copies */
#define CTX EM_RCL_CONTEXT
#define ARENA_END 0x007635C0u    /* the packet arena ends where the chain table starts */
#define POINT_LIGHT_WORDS (32u * 32u)

static struct {
    /* D_00817BC0, the rig record 001D8130 loads and 001D8340 / 001D8690
     * read. Only 001D89D0's chain reads or writes it. */
    uint32_t rig[EM_ACTOR_LIGHT_RIG_WORDS];
    uint32_t points[POINT_LIGHT_WORDS];   /* context +0x220, per draw */
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
    const EmWorldModels *bank;
    int textures_loaded, textures_tried;
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

/* 001CB3C0: the +0x90 attachment (a face unit, 001D3F50 / 001D3E40) has no
 * EE-side translation yet. */
static int w_001CB3C0(void *ctx, EmOwnerServicesOwner *owner)
{
    (void)ctx;
    (void)owner;
    return report(0x001CB3C0u, "the +0x90 attachment draw 001CB3C0 is not bound");
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
    if (!g.point_lights_loaded)
        return report(0x001D8340u, "the point-light pool (context +0x220) is not loaded");
    memcpy(L.points, g.point_lights.active, sizeof L.points);
    _Static_assert(sizeof g.point_lights.active == sizeof L.points, "EmPointLight is the 0x80-byte slot");

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

static const uint8_t *resolve(void *ctx, uint32_t address, uint32_t bytes)
{
    const EmWorldModels *bank = ctx;
    if (bank && address >= bank->table_address && bytes <= bank->span_size &&
        address - bank->table_address <= bank->span_size - bytes)
        return bank->span + (address - bank->table_address);
    return em_rcl_bytes(address, bytes);
}

static uint32_t fnv(uint32_t h, const uint32_t *w, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i)
        for (unsigned b = 0; b < 4u; ++b) h = (h ^ ((w[i] >> (8u * b)) & 0xFFu)) * 16777619u;
    return h;
}

int em_owner_draw_live_001CAA00(const EmWorldModels *bank, EmOwnerServicesOwner *owner,
                                const uint32_t rgb[4], uint32_t record)
{
    if (!bank || !owner || !rgb) return report(0x001CAA00u, "NULL argument");
    if (em_rcl_fault()) return report(em_rcl_fault(), "the render context has faulted");
    if (bind_views(bank) < 0) return -1;
    if (channel_open() < 0) return report(0x00811CD0u, "the channel-0 cursor is outside the packet arena");
    const uint32_t frame = em_frame_counter();
    if (L.unit_frame != frame) {
        L.unit_frame = frame;
        L.unit_count = 0;
        L.log_count = 0;
    }
    if (L.log_count >= EM_OWNER_DRAW_LIVE_UNITS)
        return report(0x001CAA00u, "more 001CAA00 calls in one frame than EM_OWNER_DRAW_LIVE_UNITS");
    EmOwnerDrawLiveLog *log = &L.log[L.log_count++];
    memset(log, 0, sizeof *log);
    log->record = record;

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
    const int rc = em_owner_services_001CAA00(s, owner);
    L.owner_rgb = NULL;
    if (rc < 0 || s->fault.code) {
        fprintf(stderr, "em_owner_draw_live: 001CAA00 faulted: services %08X/%d, draw %08X/%d, light %08X/%d\n",
                (unsigned)s->fault.address, (int)s->fault.code, (unsigned)L.draw.fault.address,
                (int)L.draw.fault.code, (unsigned)L.light.fault.address, (int)L.light.fault.code);
        return -1;
    }
    if (channel_store() < 0) return report(0x00811CD0u, "the channel-0 cursor could not be stored");
    const uint32_t used = channel_address() - start;
    if (!used) return 0;                           /* 001CA7B0 culled it */
    if (L.unit_count >= EM_OWNER_DRAW_LIVE_UNITS)
        return report(0x001CAA00u, "more drawn units in one frame than EM_OWNER_DRAW_LIVE_UNITS");
    const char *why = NULL;
    EmObjectUnitPieces *p = &L.units[L.unit_count];
    if (em_object_unit_parse(unit, used, resolve, (void *)bank, p, &why) < 0) {
        fprintf(stderr, "em_owner_draw_live: the unit at %08X is refused: %s\n", (unsigned)start, why);
        return -1;
    }
    L.bank = bank;
    ++L.unit_count;
    log->bytes = used;
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

/* ------------------------------------------------ the frame's draw */

static int load_textures(EmGfx *gfx)
{
    if (L.textures_loaded) return 0;
    if (L.textures_tried) return -1;
    L.textures_tried = 1;
    FILE *f = fopen(EM_OWNER_DRAW_LIVE_TEXTURES, "rb");
    if (!f) return report(0, "no " EM_OWNER_DRAW_LIVE_TEXTURES " (run tools/export_object_textures.py)");
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = size > 0x10 ? malloc((size_t)size) : NULL;
    const int ok = data && fread(data, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    uint32_t head[4];
    if (ok) memcpy(head, data, sizeof head);
    int bad = !ok || memcmp(data, "EMOT", 4) != 0 || head[1] != 1u ||
              (uint64_t)0x10 + (uint64_t)24 * head[2] > (uint64_t)size;
    for (uint32_t i = 0; !bad && i < head[2]; ++i) {
        const uint8_t *e = data + 0x10 + 24u * i;
        uint64_t tex0;
        uint32_t w, h, at;
        memcpy(&tex0, e, 8);
        memcpy(&w, e + 8, 4);
        memcpy(&h, e + 12, 4);
        memcpy(&at, e + 16, 4);
        if (w > 1024u || h > 1024u || (uint64_t)at + 4ull * w * h > (uint64_t)size ||
            em_gfx_object_texture(gfx, tex0, data + at, w, h) < 0)
            bad = 1;
    }
    free(data);
    if (bad) return report(0, EM_OWNER_DRAW_LIVE_TEXTURES " is not a valid object texture export");
    L.textures_loaded = 1;
    return 0;
}

int em_owner_draw_live_flush(EmGfx *gfx)
{
    const uint32_t frame = em_frame_counter();
    if (L.unit_frame != frame) {                   /* units of an earlier frame are never drawn */
        L.unit_count = 0;
        L.log_count = 0;
    }
    memcpy(L.last, L.log, sizeof L.log[0] * L.log_count);
    L.last_count = L.log_count;
    L.log_count = 0;
    int rc = 0;
    if (L.unit_count) {
        if (!gfx) rc = report(0, "no graphics device");
        else if (load_textures(gfx) < 0) rc = -1;
        for (uint32_t i = 0; !rc && i < L.unit_count; ++i)
            if (em_gfx_object_unit(gfx, &L.units[i].unit) < 0)
                rc = report(L.units[i].model_address, "em_gfx_object_unit refused a unit (reason above)");
    }
    L.unit_count = 0;
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

void em_owner_draw_live_reset(void)
{
    L.unit_count = 0;
    L.log_count = L.last_count = 0;
    L.unit_frame = 0;
    L.bank = NULL;
}
