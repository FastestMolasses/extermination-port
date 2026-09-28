/* em_page_draw.c - see em_page_draw.h. */
#include "game/em_page_draw.h"

#include <stdlib.h>
#include <string.h>

#include "game/em_hud.h"
#include "game/em_item_geometry.h"
#include "game/em_status_background.h"

enum { CALLS = 2048, ARENA = 4096, ATLAS_W = 1024, ATLAS_H = 2048, TEXTURES = 128, ARCS = 16 };
enum { K_BLEND = 1, K_SPRITE, K_SCISSOR, K_RECT, K_TEXT, K_BACKGROUND, K_TRIANGLE, K_ARC, K_FLAT,
       K_MODELS };

typedef struct {
    uint8_t kind, mode, proportional;
    int32_t xy[6];
    uint32_t rgba, text;
    uint64_t tex0;
} Call;

typedef struct {
    uint64_t tex0;
    uint32_t u, v, w, h;
} Texture;

struct EmPageDraw {
    const EmGsTexture *gs;
    Call calls[CALLS];
    unsigned count, arena_used, mode;
    char arena[ARENA];
    float arcs[ARCS][24];
    unsigned arc_count;
    int refused, uploaded, dirty;
    uint8_t *atlas;
    Texture textures[TEXTURES];
    unsigned texture_count, shelf_x, shelf_y, shelf_h;
    uint32_t generation;
    EmPageDrawModels models;
    void *models_ctx;
};

EmPageDraw *em_page_draw_create(const EmGsTexture *gs)
{
    if (!gs)
        return NULL;
    EmPageDraw *d = calloc(1, sizeof *d);
    if (!d || !(d->atlas = calloc((size_t)ATLAS_W * ATLAS_H, 4))) {
        free(d);
        return NULL;
    }
    d->gs = gs;
    d->generation = em_gs_texture_generation(gs) - 1;
    return d;
}

void em_page_draw_free(EmPageDraw *d)
{
    if (!d)
        return;
    em_page_draw_deactivate(d);
    free(d->atlas);
    free(d);
}

void em_page_draw_begin(EmPageDraw *d)
{
    if (!d)
        return;
    d->count = d->arena_used = d->arc_count = 0;
    d->refused = 0;
    d->mode = 0;
}

static Call *append(EmPageDraw *d, unsigned kind)
{
    if (!d || d->refused || d->count >= CALLS) {
        if (d)
            d->refused = 1;
        return NULL;
    }
    Call *c = &d->calls[d->count++];
    memset(c, 0, sizeof *c);
    c->kind = (uint8_t)kind;
    c->mode = (uint8_t)d->mode;
    return c;
}

static int refuse(EmPageDraw *d)
{
    if (d)
        d->refused = 1;
    return 0;
}

int em_page_draw_blend(EmPageDraw *d, int32_t slot, int32_t mode)
{
    if (!d || slot != 1 || mode < 0 || mode > 3)
        return refuse(d);
    d->mode = (unsigned)mode;
    return append(d, K_BLEND) != NULL;
}

int em_page_draw_sprite(EmPageDraw *d, int32_t slot, int32_t x, int32_t y, int32_t w, int32_t h,
                        uint32_t rgba, uint64_t tex0)
{
    if (!d || slot != 1 || !em_gs_texture_size(tex0, NULL, NULL))
        return refuse(d);
    Call *c = append(d, K_SPRITE);
    if (!c)
        return 0;
    c->xy[0] = x;
    c->xy[1] = y;
    c->xy[2] = w;
    c->xy[3] = h;
    c->rgba = rgba;
    c->tex0 = tex0;
    return 1;
}

int em_page_draw_scissor(EmPageDraw *d, int32_t slot, int32_t x0, int32_t y0, int32_t x1,
                         int32_t y1)
{
    if (!d || slot != 1)
        return refuse(d);
    Call *c = append(d, K_SCISSOR);
    if (!c)
        return 0;
    /* SCISSOR_1's fields are 11 bits wide. */
    c->xy[0] = x0 & 0x7FF;
    c->xy[1] = y0 & 0x7FF;
    c->xy[2] = x1 & 0x7FF;
    c->xy[3] = y1 & 0x7FF;
    return 1;
}

int em_page_draw_rectangle(EmPageDraw *d, int32_t slot, int32_t x0, int32_t y0, int32_t x1,
                           int32_t y1, uint32_t rgba)
{
    if (!d || slot != 1)
        return refuse(d);
    Call *c = append(d, K_RECT);
    if (!c)
        return 0;
    c->xy[0] = x0;
    c->xy[1] = y0;
    c->xy[2] = x1;
    c->xy[3] = y1;
    c->rgba = rgba;
    return 1;
}

/* The glyph cells the original 001CBA50 / 001CC1E0 calls pass (the status
 * hub's mapping, em_status_hub_ui.c / em_battery_ui.c). */
static int text_style(int proportional, int32_t w, int32_t h, EmHudTextStyle *style)
{
    if (proportional) {
        *style = EM_HUD_TEXT_TALL;
        return w == 10 && h == 20;
    }
    if (w == 12 && h == 12)
        *style = EM_HUD_TEXT_NUM12;
    else if (w == 16 && h == 16)
        *style = EM_HUD_TEXT_NUM16;
    else
        return 0;
    return 1;
}

int em_page_draw_text(EmPageDraw *d, int proportional, int32_t x, int32_t y, int32_t w, int32_t h,
                      const char *text, uint32_t rgb)
{
    EmHudTextStyle unused;
    const size_t length = text ? strlen(text) + 1 : 0;
    if (!d || !text || !text_style(proportional, w, h, &unused) || length > ARENA - d->arena_used)
        return refuse(d);
    Call *c = append(d, K_TEXT);
    if (!c)
        return 0;
    c->proportional = (uint8_t)(proportional != 0);
    c->xy[0] = x;
    c->xy[1] = y;
    c->xy[2] = w;
    c->xy[3] = h;
    c->rgba = rgb & 0xFFFFFFu;
    c->text = d->arena_used;
    memcpy(d->arena + d->arena_used, text, length);
    d->arena_used += (unsigned)length;
    return 1;
}

int em_page_draw_background(EmPageDraw *d, uint64_t tex0)
{
    if (!d || !em_gs_texture_size(tex0, NULL, NULL))
        return refuse(d);
    Call *c = append(d, K_BACKGROUND);
    if (!c)
        return 0;
    c->tex0 = tex0;
    d->mode = 0; /* 0020A7A0 leaves 00207D00(1, 0) */
    return 1;
}

int em_page_draw_triangle(EmPageDraw *d, const int32_t xy[3][2], unsigned intensity)
{
    if (!d || !xy || intensity > 255)
        return refuse(d);
    Call *c = append(d, K_TRIANGLE);
    if (!c)
        return 0;
    for (unsigned i = 0; i < 3; ++i) {
        c->xy[2 * i] = xy[i][0];
        c->xy[2 * i + 1] = xy[i][1];
    }
    c->rgba = intensity;
    return 1;
}

int em_page_draw_flat(EmPageDraw *d, int32_t slot, const int32_t xy[3][2], uint32_t rgba)
{
    if (!d || slot != 1 || !xy)
        return refuse(d);
    Call *c = append(d, K_FLAT);
    if (!c)
        return 0;
    for (unsigned i = 0; i < 3; ++i) {
        c->xy[2 * i] = xy[i][0];
        c->xy[2 * i + 1] = xy[i][1];
    }
    c->rgba = rgba;
    return 1;
}

int em_page_draw_models(EmPageDraw *d)
{
    return append(d, K_MODELS) != NULL;
}

void em_page_draw_set_models(EmPageDraw *d, EmPageDrawModels models, void *ctx)
{
    if (!d)
        return;
    d->models = models;
    d->models_ctx = ctx;
}

int em_page_draw_arc(EmPageDraw *d, const float descriptor[24])
{
    if (!d || !descriptor || d->arc_count >= ARCS)
        return refuse(d);
    Call *c = append(d, K_ARC);
    if (!c)
        return 0;
    memcpy(d->arcs[d->arc_count], descriptor, sizeof d->arcs[0]);
    c->xy[0] = (int32_t)d->arc_count++;
    return 1;
}

unsigned em_page_draw_count(const EmPageDraw *d)
{
    return d ? d->count : 0;
}

void em_page_draw_deactivate(EmPageDraw *d)
{
    if (d && d->uploaded) {
        em_hud_decor_invalidate();
        d->uploaded = 0;
    }
}

/* The atlas: texels (0..2, 0..2) are white (the untextured carrier, sampled
 * at (1.5, 1.5)), textures are
 * shelf-packed below it and rebuilt whenever the GS memory changed. */
static void atlas_reset(EmPageDraw *d)
{
    memset(d->atlas, 0, (size_t)ATLAS_W * ATLAS_H * 4);
    memset(d->atlas, 255, 4 * 3);
    memset(d->atlas + ATLAS_W * 4, 255, 4 * 3);
    memset(d->atlas + ATLAS_W * 8, 255, 4 * 3);
    d->texture_count = 0;
    d->shelf_x = 3;
    d->shelf_y = 0;
    d->shelf_h = 3;
    d->dirty = 1;
    d->generation = em_gs_texture_generation(d->gs);
}

/* Each texture sits in the atlas with a one-texel border that repeats its
 * edge texels, so the renderer's bilinear sampler at a sprite's edge reads
 * the texture's own edge instead of a neighbour (the atlas is the port's
 * packing, not GS memory). That is the original's sampling of a page
 * sprite: 00207E40's packet sets CLAMP_1 = 5 (clamp in both directions)
 * for the sprite and 0 after it, and every 00207D00 blend block sets
 * TEX1_1 = 0x60 (bilinear magnification and minification; the blocks at
 * D_00275674 + 0x6A0 / 0x720 / 0x7A0 / 0x820 in the status-hub capture).
 * The GS's exact bilinear weights are the renderer's. */
static const Texture *texture(EmPageDraw *d, uint64_t tex0)
{
    for (unsigned i = 0; i < d->texture_count; ++i)
        if (d->textures[i].tex0 == tex0)
            return &d->textures[i];
    uint32_t w, h;
    if (d->texture_count >= TEXTURES || !em_gs_texture_size(tex0, &w, &h) || w + 2 > ATLAS_W)
        return NULL;
    if (d->shelf_x + w + 2 > ATLAS_W) {
        d->shelf_y += d->shelf_h;
        d->shelf_x = 0;
        d->shelf_h = 0;
    }
    if (d->shelf_y + h + 2 > ATLAS_H)
        return NULL;
    uint8_t *pixels = malloc((size_t)w * h * 4);
    if (!pixels || !em_gs_texture_decode(d->gs, tex0, pixels, (size_t)w * h * 4)) {
        free(pixels);
        return NULL;
    }
    for (uint32_t row = 0; row < h + 2; ++row) {
        const uint32_t sy = row == 0 ? 0 : row > h ? h - 1 : row - 1;
        uint8_t *out = d->atlas + (((size_t)d->shelf_y + row) * ATLAS_W + d->shelf_x) * 4;
        const uint8_t *in = pixels + (size_t)sy * w * 4;
        memcpy(out, in, 4);
        memcpy(out + 4, in, (size_t)w * 4);
        memcpy(out + 4 + (size_t)w * 4, in + (size_t)(w - 1) * 4, 4);
    }
    free(pixels);
    Texture *t = &d->textures[d->texture_count++];
    *t = (Texture){tex0, d->shelf_x + 1, d->shelf_y + 1, w, h};
    d->shelf_x += w + 2;
    if (h + 2 > d->shelf_h)
        d->shelf_h = h + 2;
    d->dirty = 1;
    return t;
}

/* GS modulate uses 128 as unity; untextured RGB is the raw 0..255 value. */
static void source_color(uint32_t rgba, int textured, float out[4])
{
    for (unsigned channel = 0; channel < 3; ++channel)
        out[channel] = (float)((rgba >> (8 * channel)) & 255) / (textured ? 128.0f : 255.0f);
    out[3] = (float)(rgba >> 24) / 128.0f;
}

typedef struct {
    float x0, y0, x1, y1; /* canvas units (512 x 448), half-open */
} Clip;

/* A quad clipped to the scissor, its UVs cut in proportion. */
static void quad(EmGfx *gfx, const Clip *clip, float x, float y, float w, float h, float u0,
                 float v0, float u1, float v1, const float color[4], unsigned mode)
{
    float x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (w <= 0 || h <= 0)
        return;
    const float cx0 = x0 < clip->x0 ? clip->x0 : x0, cx1 = x1 > clip->x1 ? clip->x1 : x1;
    const float cy0 = y0 < clip->y0 ? clip->y0 : y0, cy1 = y1 > clip->y1 ? clip->y1 : y1;
    if (cx0 >= cx1 || cy0 >= cy1)
        return;
    const float du = (u1 - u0) / w, dv = (v1 - v0) / h;
    em_gfx_overlay_sprite_blend(gfx, cx0, cy0, cx1 - cx0, cy1 - cy0, u0 + (cx0 - x0) * du,
                                v0 + (cy0 - y0) * dv, u0 + (cx1 - x0) * du, v0 + (cy1 - y0) * dv,
                                color, (EmGfxOverlayBlend)mode);
}

/* A flat triangle clipped to the scissor window (Sutherland-Hodgman over
 * its four edges), drawn as a fan. */
static int flat(EmGfx *gfx, const Clip *clip, const float in[3][2], const float color[4],
                unsigned mode)
{
    float poly[2][16][2];
    unsigned n = 3, cur = 0;
    memcpy(poly[0], in, sizeof(float) * 6);
    for (unsigned edge = 0; edge < 4 && n; ++edge) {
        const unsigned axis = edge & 1;
        const float bound = edge == 0 ? clip->x0 : edge == 1 ? clip->y0 : edge == 2 ? clip->x1 : clip->y1;
        const int keep_above = edge < 2;
        unsigned out = 0;
        for (unsigned i = 0; i < n; ++i) {
            const float *a = poly[cur][i], *b = poly[cur][(i + 1) % n];
            const int ina = keep_above ? a[axis] >= bound : a[axis] <= bound;
            const int inb = keep_above ? b[axis] >= bound : b[axis] <= bound;
            if (ina && out < 16) {
                poly[cur ^ 1][out][0] = a[0];
                poly[cur ^ 1][out++][1] = a[1];
            }
            if (ina != inb && out < 16) {
                const float t = (bound - a[axis]) / (b[axis] - a[axis]);
                poly[cur ^ 1][out][axis] = bound;
                poly[cur ^ 1][out++][axis ^ 1] = a[axis ^ 1] + t * (b[axis ^ 1] - a[axis ^ 1]);
            }
        }
        n = out;
        cur ^= 1;
    }
    const float rgba[3][4] = {{color[0], color[1], color[2], color[3]},
                              {color[0], color[1], color[2], color[3]},
                              {color[0], color[1], color[2], color[3]}};
    for (unsigned i = 2; i < n; ++i) {
        const float xy[3][2] = {{poly[cur][0][0], poly[cur][0][1]},
                                {poly[cur][i - 1][0], poly[cur][i - 1][1]},
                                {poly[cur][i][0], poly[cur][i][1]}};
        if (!em_gfx_overlay_triangle(gfx, xy, rgba, 1.5f, 1.5f, (EmGfxOverlayBlend)mode))
            return 0;
    }
    return 1;
}

int em_page_draw_render(EmPageDraw *d, EmGfx *gfx)
{
    if (!d || !gfx || d->refused)
        return 0;
    if (d->generation != em_gs_texture_generation(d->gs) || !d->texture_count)
        atlas_reset(d);
    /* Decode every texture of the frame first: one upload. */
    for (unsigned i = 0; i < d->count; ++i)
        if ((d->calls[i].kind == K_SPRITE || d->calls[i].kind == K_BACKGROUND) &&
            !texture(d, d->calls[i].tex0)) {
            /* The atlas filled: rebuild it from this frame's textures. */
            atlas_reset(d);
            for (unsigned j = 0; j < d->count; ++j)
                if ((d->calls[j].kind == K_SPRITE || d->calls[j].kind == K_BACKGROUND) &&
                    !texture(d, d->calls[j].tex0))
                    return refuse(d);
            break;
        }
    if (d->dirty || !d->uploaded) {
        if (!em_gfx_overlay_texture_set(gfx, EM_GFX_OVERLAY_TEX_UI, d->atlas, ATLAS_W, ATLAS_H))
            return 0;
        em_hud_decor_invalidate();
        d->uploaded = 1;
        d->dirty = 0;
    }
    em_gfx_overlay_canvas(gfx, EM_GFX_STATUS_W, EM_GFX_STATUS_H);
    Clip clip = {0, 0, 512, 448};
    int ok = 1;
    for (unsigned i = 0; ok && i < d->count; ++i) {
        const Call *c = &d->calls[i];
        float color[4];
        switch (c->kind) {
        case K_BLEND:
            break;
        case K_SCISSOR:
            clip = (Clip){(float)c->xy[0], (float)c->xy[1] * 2, (float)c->xy[2] + 1,
                          ((float)c->xy[3] + 1) * 2};
            break;
        case K_SPRITE: {
            const Texture *t = texture(d, c->tex0);
            if (!t)
                return refuse(d);
            source_color(c->rgba, 1, color);
            quad(gfx, &clip, c->xy[0] / 16.0f - 1792, (c->xy[1] / 16.0f - 1936) * 2,
                 (float)c->xy[2], (float)c->xy[3], (float)t->u, (float)t->v, (float)(t->u + t->w),
                 (float)(t->v + t->h), color, c->mode);
            break;
        }
        case K_RECT:
            source_color(c->rgba, 0, color);
            quad(gfx, &clip, c->xy[0] / 16.0f - 1792, (c->xy[1] / 16.0f - 1936) * 2,
                 (c->xy[2] - c->xy[0]) / 16.0f, (c->xy[3] - c->xy[1]) / 8.0f, 1.5f, 1.5f, 1.5f,
                 1.5f, color, c->mode);
            break;
        case K_TEXT: {
            EmHudTextStyle style;
            if (!text_style(c->proportional, c->xy[2], c->xy[3], &style))
                return refuse(d);
            em_hud_text_color(gfx, (float)c->xy[0] - 1792, ((float)c->xy[1] - 1936) * 2,
                              d->arena + c->text, style, c->rgba);
            break;
        }
        case K_BACKGROUND: {
            const Texture *t = texture(d, c->tex0);
            if (!t)
                return refuse(d);
            em_status_background_frame(gfx);
            ok = em_status_background_render(gfx, (float)t->u, (float)t->v, (float)t->w,
                                             (float)t->h) == 1;
            break;
        }
        case K_ARC: {
            EmItemVertex vertices[512];
            size_t n;
            if (!em_item_geometry_arc(d->arcs[c->xy[0]], vertices, 512, &n))
                return refuse(d);
            for (size_t v = 2; ok && v < n; ++v) {
                float xy[3][2], rgba[3][4];
                for (unsigned corner = 0; corner < 3; ++corner) {
                    const EmItemVertex *e = &vertices[v - 2 + corner];
                    xy[corner][0] = e->x / 16.0f - 1792;
                    xy[corner][1] = (e->y / 16.0f - 1936) * 2;
                    source_color(e->rgba, 0, rgba[corner]);
                }
                ok = em_gfx_overlay_triangle(gfx, xy, rgba, 1.5f, 1.5f, (EmGfxOverlayBlend)c->mode) != 0;
            }
            break;
        }
        case K_TRIANGLE: {
            float xy[3][2];
            for (unsigned v = 0; v < 3; ++v) {
                xy[v][0] = c->xy[2 * v] / 16.0f - 1792;
                xy[v][1] = (c->xy[2 * v + 1] / 16.0f - 1936) * 2;
            }
            const float k = c->rgba / 255.0f;
            const float rgba[3][4] = {{k, k, k, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};
            ok = em_gfx_overlay_triangle(gfx, xy, rgba, 1.5f, 1.5f, EM_GFX_UI_ADD) != 0;
            break;
        }
        case K_FLAT: {
            /* 00208040's XYZF2: X the low halfword, Y the high one (GS 12.4). */
            float xy[3][2];
            for (unsigned v = 0; v < 3; ++v) {
                xy[v][0] = (float)(c->xy[2 * v] & 0xFFFF) / 16.0f - 1792;
                xy[v][1] = ((float)(c->xy[2 * v + 1] & 0xFFFF) / 16.0f - 1936) * 2;
            }
            source_color(c->rgba, 0, color);
            ok = flat(gfx, &clip, xy, color, c->mode);
            break;
        }
        case K_MODELS: {
            /* 001B0000's model draws share the page's GS list: the 2D layer
             * so far is drawn first, the models go over it inside the
             * SCISSOR_1 window, the later calls over the models. */
            if (!d->models)
                return refuse(d);
            em_gfx_overlay_decor_flush(gfx);
            const float rect[4] = {clip.x0, clip.y0, clip.x1, clip.y1};
            ok = d->models(d->models_ctx, gfx, rect) == 1;
            em_gfx_overlay_canvas(gfx, EM_GFX_STATUS_W, EM_GFX_STATUS_H);
            break;
        }
        default:
            ok = 0;
            break;
        }
    }
    em_gfx_overlay_canvas(gfx, EM_GFX_OVERLAY_W, EM_GFX_OVERLAY_H);
    return ok ? 1 : refuse(d);
}
