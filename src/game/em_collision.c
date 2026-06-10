/* em_collision.c — EMCL loader + the engine's collision queries, native.
 *
 * The polygon test mirrors the PS2 walkers instruction-for-instruction
 * where it matters (epsilons, comparison directions, facing rule):
 *
 *   func_0019ED80 (grid node test) and func_001A4030 (cell n-gon test)
 *   are the same algorithm over the same data shape:
 *     1. dir = end - start; require dot(dir, n) <= -1e-5 (0xB727C5AC):
 *        FRONT-FACING only — motion along or away from a wall passes
 *        through, which is what lets an actor slide on a plane it is
 *        standing against.
 *     2. t = (plane_d - dot(n, start)) / dot(dir, n); hit = start + dir*t.
 *     3. per-axis interval check: hit within [min,max](start_k, end_k)
 *        inclusive on each axis (this also bounds t to [0,1]).
 *     4. convex inside test: for every ring vertex v_k with outward edge
 *        normal e_k, require dot(hit - v_k, e_k) <= +1e-5 (0x3727C5AC).
 *     5. on accept the segment END is clamped to the hit (so later polys
 *        must beat it) and the result block is staged.
 *
 * The engine's rank-table acceleration index (func_0019F1A0 binary
 * searches over the 6 sorted s16 tables) is a pure prune; the native
 * walker tests the poly list directly (205 polys for the office world)
 * with the identical accept logic, so results match the PS2's.
 *
 * Conditional surfaces (func_0019D330, attr 0x50..0x59 vs the query id
 * staged at SPR 0x7000324E) are honored for the grid set exactly as the
 * PS2 dispatcher orders them.
 */
#include "game/em_collision.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EPS_FACING 1e-5f   /* 0xB727C5AC / 0x3727C5AC in the walkers */
#define MOVE_PROBE_PAD 0.01f  /* func_0019AD00 f12 = 0x3C23D70A */

int em_collision_load(EmCollision *c, const char *path)
{
    memset(c, 0, sizeof *c);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0x30) { fclose(f); return -1; }
    uint8_t *blob = malloc((size_t)sz);
    if (!blob || fread(blob, 1, (size_t)sz, f) != (size_t)sz) {
        free(blob);
        fclose(f);
        return -1;
    }
    fclose(f);

    uint32_t hdr[6];
    memcpy(hdr, blob, sizeof hdr);
    if (memcmp(blob, "EMCL", 4) != 0 || hdr[1] != 1) { free(blob); return -1; }
    c->vert_count  = hdr[2];
    c->poly_count  = hdr[3];
    c->index_count = hdr[4];
    c->flags       = hdr[5];
    memcpy(c->bbox, blob + 0x18, sizeof c->bbox);

    size_t off = 0x30;
    size_t need = off + (size_t)c->vert_count * 12
                + (size_t)c->poly_count * sizeof(EmCollPoly)
                + ((size_t)c->index_count * 2 + 3 & ~(size_t)3)
                + (size_t)c->index_count * 12;
    if ((size_t)sz < need || c->poly_count == 0) { free(blob); return -1; }

    c->verts = (float *)(blob + off);
    off += (size_t)c->vert_count * 12;
    c->polys = (EmCollPoly *)(blob + off);
    off += (size_t)c->poly_count * sizeof(EmCollPoly);
    c->indices = (uint16_t *)(blob + off);
    off += (size_t)c->index_count * 2;
    off = (off + 3) & ~(size_t)3;
    c->edge_n = (float *)(blob + off);
    c->blob = blob;
    return 0;
}

void em_collision_free(EmCollision *c)
{
    free(c->blob);
    memset(c, 0, sizeof *c);
}

/* Surface classification — func_001A4030's tail: ratio = ny^2/(nx^2+nz^2)
 * against 0.49029 (0x3EFB075E) and 3.0, sign of ny selects the up/down
 * family. Staged at SPR 0x700030CA on the PS2. */
static uint16_t surf_classify(const float n[3])
{
    float h = n[0] * n[0] + n[2] * n[2];
    float r = (h > 0.0f) ? (n[1] * n[1]) / h : 1e9f;
    if (r < 0.49029f) return EM_SURF_WALL;
    if (n[1] >= 0.0f) return (r <= 3.0f) ? EM_SURF_SLOPE : EM_SURF_FLOOR;
    return (r <= 3.0f) ? EM_SURF_STEEPDN : EM_SURF_CEIL;
}

/* Conditional-surface gate — func_0019D330's attr 0x50..0x59 dispatch
 * against the query id (SPR 0x7000324E). Returns 0 = skip this poly. */
static int attr_passes(uint8_t attr, int id)
{
    if (attr < 0x50 || attr > 0x59) return 1;
    switch (attr) {
        case 0x50: return 0;
        case 0x51: return id == 0;
        case 0x52: return id == 2;
        case 0x53: return id != -1;
        default:   return 1;       /* 0x54..0x59 */
    }
}

/* One poly vs the (clamped) segment — func_0019ED80 / func_001A4030.
 * On accept clamps end[] to the hit and returns 1. */
static int poly_test(const EmCollision *c, const EmCollPoly *p,
                     const float start[3], float end[3])
{
    const float *n = p->plane;
    float dir[3] = { end[0] - start[0], end[1] - start[1],
                     end[2] - start[2] };
    float dn = dir[0] * n[0] + dir[1] * n[1] + dir[2] * n[2];
    if (!(dn <= -EPS_FACING)) return 0;           /* front-facing only */

    float t = (n[3] - (start[0] * n[0] + start[1] * n[1] + start[2] * n[2]))
              / dn;
    float hit[3] = { start[0] + dir[0] * t, start[1] + dir[1] * t,
                     start[2] + dir[2] * t };

    for (int k = 0; k < 3; k++) {                 /* per-axis interval */
        float lo = start[k], hi = end[k];
        if (lo > hi) { float tmp = lo; lo = hi; hi = tmp; }
        if (hit[k] < lo || hit[k] > hi) return 0;
    }

    const uint16_t *idx = c->indices + p->first;
    const float    *en  = c->edge_n + (size_t)p->first * 3;
    for (uint32_t k = 0; k < p->vcount; k++) {    /* convex inside test */
        const float *v = c->verts + (size_t)idx[k] * 3;
        float d = (hit[0] - v[0]) * en[k * 3 + 0]
                + (hit[1] - v[1]) * en[k * 3 + 1]
                + (hit[2] - v[2]) * en[k * 3 + 2];
        if (d > EPS_FACING) return 0;
    }

    end[0] = hit[0];                              /* clamp the segment */
    end[1] = hit[1];
    end[2] = hit[2];
    return 1;
}

/* Walk one collision set over the clamped segment. Returns the index of
 * the (new) nearest-hit poly or -1. Mirrors the per-set walkers; the
 * conditional-surface gate applies where func_0019D330 applies it. */
static int set_walk(const EmCollision *c, uint8_t set, int id,
                    const float start[3], float end[3])
{
    int best = -1;
    for (uint32_t i = 0; i < c->poly_count; i++) {
        const EmCollPoly *p = &c->polys[i];
        if (p->set != set) continue;
        if (!attr_passes(p->attr, id)) continue;
        if (poly_test(c, p, start, end)) best = (int)i;
    }
    return best;
}

static void stage_hit(const EmCollision *c, EmCollHit *hit, int poly,
                      int kind, const float point[3])
{
    if (!hit) return;
    const EmCollPoly *p = &c->polys[poly];
    memcpy(hit->point, point, 12);
    memcpy(hit->normal, p->plane, 12);
    hit->kind       = kind;
    hit->poly       = poly;
    hit->attr       = p->attr;
    hit->surf_class = surf_classify(p->plane);
}

int em_collision_segment_query(const EmCollision *c, const float from[3],
                               const float to[3], unsigned mask, int id,
                               EmCollHit *hit)
{
    if (!c || !c->blob) return 0;
    float end[3] = { to[0], to[1], to[2] };
    int kind = 0, poly = -1, r;

    /* Set order is the hub's: hulls (bit 0 — no native objects yet),
     * cells (bit 1), grid (bit 2); each set only beats the previous by
     * hitting nearer, because the segment end stays clamped. */
    if (mask & EM_COLL_SET_CELLS) {
        if ((r = set_walk(c, EM_COLL_SET_CELLS, id, from, end)) >= 0) {
            kind = EM_COLL_SET_CELLS;
            poly = r;
        }
    }
    if (mask & EM_COLL_SET_GRID) {
        if ((r = set_walk(c, EM_COLL_SET_GRID, id, from, end)) >= 0) {
            kind = EM_COLL_SET_GRID;
            poly = r;
        }
    }
    if (!kind) return 0;
    stage_hit(c, hit, poly, kind, end);
    if (hit) {
        hit->delta[0] = end[0] - to[0];
        hit->delta[1] = end[1] - to[1];
        hit->delta[2] = end[2] - to[2];
    }
    return kind;
}

int em_collision_move_probe(const EmCollision *c, float pos[3],
                            const float target[3], unsigned mask,
                            EmCollHit *hit)
{
    if (!c || !c->blob) return 0;

    /* func_0019AD00: start = (actor.x, target.y, actor.z); end = target
     * extended 0.01 past, along the normalized direction. */
    float start[3] = { pos[0], target[1], pos[2] };
    float dir[3] = { target[0] - start[0], target[1] - start[1],
                     target[2] - start[2] };
    float len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    float end[3] = { target[0], target[1], target[2] };
    if (len > 0.0f) {
        end[0] += dir[0] / len * MOVE_PROBE_PAD;
        end[1] += dir[1] / len * MOVE_PROBE_PAD;
        end[2] += dir[2] / len * MOVE_PROBE_PAD;
    }

    int kind = 0, poly = -1, r;
    if (mask & EM_COLL_SET_CELLS) {
        if ((r = set_walk(c, EM_COLL_SET_CELLS, 0, start, end)) >= 0) {
            kind = EM_COLL_SET_CELLS;
            poly = r;
        }
    }
    if (mask & EM_COLL_SET_GRID) {
        if ((r = set_walk(c, EM_COLL_SET_GRID, 0, start, end)) >= 0) {
            kind = EM_COLL_SET_GRID;
            poly = r;
        }
    }

    if (kind) {
        /* delta = hit - target (SPR 0x700031C0); with bit31 the engine
         * adds it to the velocity-integrated actor x/z — natively pos is
         * pre-move, so pos = target + delta lands on the same point. */
        float delta[3] = { end[0] - target[0], end[1] - target[1],
                           end[2] - target[2] };
        stage_hit(c, hit, poly, kind, end);
        if (hit) memcpy(hit->delta, delta, 12);
        if (mask & EM_COLL_SLIDE) {
            pos[0] = target[0] + delta[0];
            pos[2] = target[2] + delta[2];
        }
    } else if (mask & EM_COLL_SLIDE) {
        pos[0] = target[0];
        pos[2] = target[2];
    }
    return kind;
}
