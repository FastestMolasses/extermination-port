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
#include "game/em_ee_float.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EPS_FACING 1e-5f   /* 0xB727C5AC / 0x3727C5AC in the walkers */
#define MOVE_PROBE_PAD 0.01f  /* func_0019AD00 f12 = 0x3C23D70A */
#define FLT_MAX_EE 3.40282347e38f /* 0x7F7FFFFF: EE overflow result */

/* Finite binary32 arithmetic in the original compact-face routines uses
 * R5900 round-toward-zero after each operation. */
static float face_float(double value)
{
    float result=(float)value;
    if ((value>0 && result>value) || (value<0 && result<value))
        result=nextafterf(result,0);
    return result;
}

int em_collision_box_face(const EmCollBoxFace *box, const float start[3],
                          const float end[3], int movement, EmCollHit *hit)
{
    unsigned face=box->face;
    if (!face || face>6 || (movement && (face==3 || face==4))) return 0;
    float delta[3], lo[3], hi[3], relative[3];
    for (unsigned k=0;k<3;++k) {
        delta[k]=face_float((double)end[k]-start[k]);
        if (delta[k]<0 ? face==2*k+2 : face==2*k+1) return 0;
        float other=face_float((double)box->origin[k]+box->extent[k]);
        lo[k]=box->extent[k]>0 ? box->origin[k] : other;
        hi[k]=box->extent[k]>0 ? other : box->origin[k];
        relative[k]=face_float((double)box->origin[k]-start[k]);
    }
    unsigned axis=(face-1)/2;
    float remaining=face_float((double)lo[axis]-end[axis]);
    if (!(face_float((double)relative[axis]*remaining)<0)) return 0;
    float t=face_float((double)relative[axis]/delta[axis]);
    float point[3];
    for (unsigned k=0;k<3;++k) {
        if (k==axis) point[k]=box->origin[k];
        else if (movement && k==1) {
            point[k]=end[k];
            if (point[k]<lo[k] || point[k]>hi[k]) return 0;
        } else {
            float offset=movement
                ? face_float((double)face_float((double)delta[k]*relative[axis])/delta[axis])
                : face_float((double)delta[k]*t);
            point[k]=face_float((double)start[k]+offset);
            if (!(point[k]>lo[k] && point[k]<hi[k])) return 0;
        }
    }
    if (hit) {
        memset(hit,0,sizeof *hit);
        memcpy(hit->point,point,sizeof point);
        hit->normal[axis]=(face&1) ? 1.0f : -1.0f;
        hit->surf_class=axis==1 ? ((face&1) ? EM_SURF_FLOOR : EM_SURF_CEIL) : EM_SURF_WALL;
        hit->kind=EM_COLL_SET_CELLS;
    }
    return 1;
}

int em_collision_cell_load(EmCollCell *cell, const char *path)
{
    FILE *file=fopen(path,"rb");
    if (!file) return -1;
    uint32_t header[5];
    EmCollCell next={0};
    if (fread(header,sizeof header,1,file)!=1 || memcmp(header,"EMCB",4) ||
        header[1]!=1 || header[2]>255 || header[3]>255 || !header[4] ||
        header[4]>256) goto fail;
    next.uid=header[2];next.attr=header[3];next.face_count=header[4];
    if (fread(next.bbox,sizeof next.bbox,1,file)!=1) goto fail;
    for (unsigned k=0;k<6;++k) if (!isfinite(next.bbox[k])) goto fail;
    for (unsigned k=0;k<3;++k) if (next.bbox[k]>next.bbox[k+3]) goto fail;
    next.faces=malloc(next.face_count*sizeof *next.faces);
    if (!next.faces || fread(next.faces,sizeof *next.faces,next.face_count,file)!=next.face_count ||
        fgetc(file)!=EOF) goto fail;
    for (unsigned i=0;i<next.face_count;++i) {
        const EmCollBoxFace *face=next.faces+i;
        if (!face->face || face->face>6) goto fail;
        for (unsigned k=0;k<3;++k)
            if (!isfinite(face->origin[k]) || !isfinite(face->extent[k])) goto fail;
    }
    fclose(file);em_collision_cell_free(cell);*cell=next;return 0;
fail:
    fclose(file);free(next.faces);return -1;
}

void em_collision_cell_free(EmCollCell *cell)
{
    free(cell->faces);memset(cell,0,sizeof *cell);
}

int em_collision_cell_bind(EmCollision *c, const EmCollCell *cell)
{
    if (!cell || !cell->faces || !cell->face_count) return 0;
    for (unsigned i=0;i<c->actor_cell_count;++i)
        if (c->actor_cells[i]->uid==cell->uid) { c->actor_cells[i]=cell;return 1; }
    if (c->actor_cell_count==32) return 0;
    c->actor_cells[c->actor_cell_count++]=cell;return 1;
}

void em_collision_cell_unbind(EmCollision *c, unsigned uid)
{
    for (unsigned i=0;i<c->actor_cell_count;++i) if (c->actor_cells[i]->uid==uid) {
        memmove(c->actor_cells+i,c->actor_cells+i+1,
            (c->actor_cell_count-i-1)*sizeof *c->actor_cells);
        --c->actor_cell_count;return;
    }
}

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

/* Surface classification — func_001A4030's tail. CONFIRMED (audit)
 * against decomp Extermination/src/func_001A4030.c: the ratio is
 * ny^2/(nx^2+nz^2), tested `< 0.49029058f` then `<= 3.0f`, with the
 * sign of the plane normal's y selecting the family:
 *   ny <  0:  wall 0x2000 / down-slope 0x800  / ceiling 0x8000
 *   ny >= 0:  wall 0x2000 / slope      0x1000 / floor   0x4000
 * All five constants match EM_SURF_* exactly. The threshold literal is
 * 0.49029058f in the recovered C (was truncated to 0.49029f here).
 * Staged at SPR 0x700030CA on the PS2. */
static uint16_t surf_classify(const float n[3])
{
    float h = n[0] * n[0] + n[2] * n[2];
    float r = (h > 0.0f) ? (n[1] * n[1]) / h : 1e9f;
    if (r < 0.49029058f) return EM_SURF_WALL;
    if (n[1] >= 0.0f) return (r <= 3.0f) ? EM_SURF_SLOPE : EM_SURF_FLOOR;
    return (r <= 3.0f) ? EM_SURF_STEEPDN : EM_SURF_CEIL;
}

typedef enum {
    QUERY_SEGMENT,       /* 0019A570 -> 001A0B10 / 0019D330 */
    QUERY_MOVEMENT,      /* 0019AD00 -> 0019FE50 / 0019CB60 */
    QUERY_CAMERA        /* 0019A910 -> 001A1390 / 0019D770 */
} QueryFamily;

/* The original walkers deliberately use different surface gates. Verified
 * against the raw branch blocks, not the former erroneous 0019D330 C:
 * segment rejects 0x50 and >=0x5A; movement rejects >=0x5A but admits
 * 0x50; camera rejects only 0x51..0x53 and does not use the actor ID.
 * The shared ID tests below apply to segment/movement only.
 * EMCL v1 loses cell-record attributes (exports them as zero), so this
 * fixes grid gates; complete cell-record filtering needs richer assets. */
static int attr_passes(uint8_t attr, int id, QueryFamily family)
{
    if (family == QUERY_CAMERA) return attr < 0x51 || attr >= 0x54;
    if (attr >= 0x5A) return 0;
    if (attr < 0x50) return 1;
    switch (attr) {
        case 0x50: return family == QUERY_MOVEMENT;
        case 0x51: return id == 0;
        case 0x52: return id == 2;
        case 0x53: return id != -1;
        default:   return 1;          /* 0x54..0x59 */
    }
}

/* The published class4 actor list is the cell world's second pass
 * (0019FE50/001A0B10/001A1390). Cell18's owner keeps its authored world
 * box throughout the battery interaction; no model-space transform is
 * applied. Polygon cells and other compact primitive types remain on
 * their existing paths until their asset/lifecycle bindings are recovered. */
static int actor_cell_walk(const EmCollision *c, int id, QueryFamily family,
                           const float start[3], float end[3], EmCollHit *hit)
{
    int found=0;
    for (unsigned i=0;i<c->actor_cell_count;++i) {
        const EmCollCell *cell=c->actor_cells[i];
        if (!attr_passes((uint8_t)cell->attr,id,family)) continue;
        for (unsigned j=0;j<cell->face_count;++j) {
            EmCollHit next;
            if (!em_collision_box_face(cell->faces+j,start,end,
                                       family==QUERY_MOVEMENT,&next)) continue;
            memcpy(end,next.point,12);
            next.poly=-(int)cell->uid-1;
            next.attr=(uint8_t)cell->attr;
            if (hit) *hit=next;
            found=1;
        }
    }
    return found;
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
static int set_walk(const EmCollision *c, uint8_t set, int id, QueryFamily family,
                    const float start[3], float end[3])
{
    int best = -1;
    for (uint32_t i = 0; i < c->poly_count; i++) {
        const EmCollPoly *p = &c->polys[i];
        if (p->set != set) continue;
        if (!attr_passes(p->attr, id, family)) continue;
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
    /* Grid hits carry the node's authored class (EM_COLL_FLAG_NODE_CLASS);
     * an EMCL without it keeps the older normal-derived class. */
    if (p->set == EM_COLL_SET_GRID && (c->flags & EM_COLL_FLAG_NODE_CLASS))
        hit->surf_class = (uint16_t)(p->pad << 8);
    else
        hit->surf_class = surf_classify(p->plane);
}

static int segment_query(const EmCollision *c, const float from[3],
                         const float to[3], unsigned mask, int id,
                         QueryFamily family, EmCollHit *hit)
{
    if (!c || !c->blob) return 0;
    float end[3] = { to[0], to[1], to[2] };
    int kind = 0, poly = -1, r;

    /* Set order is the hub's: hulls (bit 0 — no native objects yet),
     * cells (bit 1), grid (bit 2); each set only beats the previous by
     * hitting nearer, because the segment end stays clamped. */
    if (mask & EM_COLL_SET_CELLS) {
        if ((r = set_walk(c, EM_COLL_SET_CELLS, id, family, from, end)) >= 0) {
            kind = EM_COLL_SET_CELLS;
            poly = r;
        }
        if (actor_cell_walk(c,id,family,from,end,hit)) {
            kind=EM_COLL_SET_CELLS;
            poly=-1;
        }
    }
    if (mask & EM_COLL_SET_GRID) {
        if ((r = set_walk(c, EM_COLL_SET_GRID, id, family, from, end)) >= 0) {
            kind = EM_COLL_SET_GRID;
            poly = r;
        }
    }
    if (!kind) return 0;
    if (poly>=0) stage_hit(c, hit, poly, kind, end);
    if (hit) {
        hit->delta[0] = end[0] - to[0];
        hit->delta[1] = end[1] - to[1];
        hit->delta[2] = end[2] - to[2];
    }
    return kind;
}

int em_collision_segment_query(const EmCollision *c, const float from[3],
                               const float to[3], unsigned mask, int id,
                               EmCollHit *hit)
{
    return segment_query(c, from, to, mask, id, QUERY_SEGMENT, hit);
}

int em_collision_camera_query(const EmCollision *c, const float from[3],
                              const float to[3], unsigned mask, EmCollHit *hit)
{
    return segment_query(c, from, to, mask, EM_COLL_ID_NONE, QUERY_CAMERA, hit);
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
        if ((r = set_walk(c, EM_COLL_SET_CELLS, 0, QUERY_MOVEMENT, start, end)) >= 0) {
            kind = EM_COLL_SET_CELLS;
            poly = r;
        }
        if (actor_cell_walk(c,0,QUERY_MOVEMENT,start,end,hit)) {
            kind=EM_COLL_SET_CELLS;
            poly=-1;
        }
    }
    if (mask & EM_COLL_SET_GRID) {
        if ((r = set_walk(c, EM_COLL_SET_GRID, 0, QUERY_MOVEMENT, start, end)) >= 0) {
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
        if (poly>=0) stage_hit(c, hit, poly, kind, end);
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

/* --- Moving walkable surfaces (footprint-AABB + velocity carry) -------
 *
 * INVESTIGATION_first_level_area11.md §11.4 / truck convergence block
 * 0x00825014. A per-frame registry of axis-aligned walkable footprints
 * with a per-frame velocity; the player's carry is a DIRECT add of the
 * matched surface's velocity to its position (no smoothing), mirroring the
 * PS2 D_70003.._38A8 add into D_00810358. The registry stands in for the
 * engine's zone table D_70003250[(uid>>8)&0xFF]; it is rebuilt each frame
 * (clear -> actors register -> player carry resolves).
 *
 * Engine-global like the PS2 scratch zone table (a fixed array, not bound
 * to a collision world). Dormant until an actor registers — the TRUCK
 * actor that does so is a separate, later task. */

#define EM_MOVING_MAX 8   /* fixed slots; AREA-11 needs exactly one (truck) */

typedef struct {
    float minX, maxX, minZ, maxZ;
    float top_y;
    float vel[3];
} EmMovingSurface;

static EmMovingSurface s_moving[EM_MOVING_MAX];
static int             s_moving_n;

void em_collision_moving_clear(void)
{
    s_moving_n = 0;
}

int em_collision_moving_register(float minX, float maxX,
                                 float minZ, float maxZ, float top_y,
                                 const float vel[3])
{
    if (s_moving_n >= EM_MOVING_MAX) return -1;
    EmMovingSurface *m = &s_moving[s_moving_n];
    /* normalize the footprint so the inclusive interval test below is
     * order-independent (the truck authors min<max, but be defensive) */
    m->minX = minX < maxX ? minX : maxX;
    m->maxX = minX < maxX ? maxX : minX;
    m->minZ = minZ < maxZ ? minZ : maxZ;
    m->maxZ = minZ < maxZ ? maxZ : minZ;
    m->top_y  = top_y;
    m->vel[0] = vel[0];
    m->vel[1] = vel[1];
    m->vel[2] = vel[2];
    return s_moving_n++;
}

int em_collision_moving_carry(float pos[3])
{
    /* §11.4 step 1: footprint test on the player X/Z (D_00810360 /
     * D_00810368) against [minX,maxX] x [minZ,maxZ]. PORT addition: gate
     * to the Y band [top_y - EM_MOVING_Y_BELOW, top_y + EM_MOVING_Y_ABOVE]
     * so a free-falling player only rides a surface they actually stand on
     * (the PS2 truck owns the sole walkable top at its height, so it skips
     * the Y test). On multiple matches, take the highest top_y at/below the
     * player — the surface the player is resting on, not one overhead. */
    int   best = -1;
    float best_y = -1e30f;
    for (int i = 0; i < s_moving_n; i++) {
        const EmMovingSurface *m = &s_moving[i];
        if (pos[0] < m->minX || pos[0] > m->maxX) continue;
        if (pos[2] < m->minZ || pos[2] > m->maxZ) continue;
        if (pos[1] < m->top_y - EM_MOVING_Y_BELOW) continue;
        if (pos[1] > m->top_y + EM_MOVING_Y_ABOVE) continue;
        if (m->top_y > best_y) { best_y = m->top_y; best = i; }
    }
    if (best < 0) return 0;

    /* §11.4 step 2: read the player position, ADD the surface velocity
     * directly, write it back. No smoothing — the truck's accelerating
     * fall is carried verbatim, which is the fail/death mechanic. */
    const EmMovingSurface *m = &s_moving[best];
    pos[0] += m->vel[0];
    pos[1] += m->vel[1];
    pos[2] += m->vel[2];
    return 1;   /* "player is riding" (PS2 D_70003.._31F0 = 1) */
}

/* --- EM_CARRY_TEST=1 harness (headless, OS-free) ------------------------
 *
 * clear -> register a footprint with vel (0,-0.6667,0) at top_y -> a point
 * inside the footprint at the right Y rides (carry==1, Y drops by 0.6667);
 * the same point moved outside the footprint does not ride (carry==0, Y
 * holds); a point far above top_y does not ride. -0.6667 is the truck's
 * terminal fall velocity from §11.x. */
static int carry_check(int cond, const char *what, int *fail)
{
    if (cond) return 1;
    (*fail)++;
    printf("carry test: CHECK FAILED — %s\n", what);
    return 0;
}

int em_collision_moving_selftest(void)
{
    int fail = 0;
    const float top_y = 100.0f;
    const float vel[3] = { 0.0f, -0.6667f, 0.0f };

    em_collision_moving_clear();
    int slot = em_collision_moving_register(10.0f, 20.0f,   /* X span */
                                            30.0f, 40.0f,   /* Z span */
                                            top_y, vel);
    carry_check(slot == 0, "first registration takes slot 0", &fail);

    /* inside the footprint, standing on top_y -> rides, Y drops by 0.6667 */
    float p_in[3] = { 15.0f, top_y, 35.0f };
    int r_in = em_collision_moving_carry(p_in);
    carry_check(r_in == 1, "point inside footprint at top_y rides", &fail);
    carry_check(fabsf(p_in[1] - (top_y - 0.6667f)) < 1e-4f,
                "rider Y dropped by the surface vel (0.6667)", &fail);
    carry_check(p_in[0] == 15.0f && p_in[2] == 35.0f,
                "rider X/Z unchanged (vel x/z = 0)", &fail);

    /* outside the footprint -> no ride, Y holds */
    float p_out[3] = { 100.0f, top_y, 35.0f };
    int r_out = em_collision_moving_carry(p_out);
    carry_check(r_out == 0, "point outside footprint does not ride", &fail);
    carry_check(p_out[1] == top_y, "non-rider Y held", &fail);

    /* inside X/Z but far above top_y (out of the Y band) -> no ride */
    float p_high[3] = { 15.0f, top_y + 100.0f, 35.0f };
    int r_high = em_collision_moving_carry(p_high);
    carry_check(r_high == 0, "point far above top_y does not ride", &fail);
    carry_check(p_high[1] == top_y + 100.0f, "wrong-Y point held", &fail);

    printf("carry test: %s\n", fail == 0 ? "PASS" : "FAIL");
    fflush(stdout);
    return fail;
}

/* --- Static blocking AABBs (the gated path-blocker primitive) ----------
 *
 * INVESTIGATION_area11_grate.md §5 — the AREA-11 GRATE's closed collision
 * hull. A per-frame registry of world-axis-aligned solid boxes; the player
 * wall-solve pushes the player OUT of any box they penetrated, AFTER the
 * static EMCL wall solve. Unlike the single-sided EMCL polygon world, an
 * AABB is a solid VOLUME: resolve along the axis of least horizontal
 * penetration so the player cannot pass into it (the grate is a wall).
 *
 * Engine-global like the moving-surface table (a fixed scratch array, not
 * bound to a collision world). Dormant until an actor registers — the
 * GRATE actor (em_game.c) registers its closed hull each frame ONLY while
 * the area is not powered, so once powered the registry is empty and the
 * push-out is a no-op (movement identical to before). */

#define EM_BLOCKER_MAX 8   /* fixed slots; AREA-11 needs exactly one (grate) */

static EmBlockerAabb s_blocker[EM_BLOCKER_MAX];
static int           s_blocker_n;

void em_collision_blocker_clear(void)
{
    s_blocker_n = 0;
}

int em_collision_blocker_register(const EmBlockerAabb *aabb)
{
    if (!aabb || s_blocker_n >= EM_BLOCKER_MAX) return -1;
    EmBlockerAabb *d = &s_blocker[s_blocker_n];
    /* normalize so the overlap test below is order-independent */
    d->minX = aabb->minX < aabb->maxX ? aabb->minX : aabb->maxX;
    d->maxX = aabb->minX < aabb->maxX ? aabb->maxX : aabb->minX;
    d->minZ = aabb->minZ < aabb->maxZ ? aabb->minZ : aabb->maxZ;
    d->maxZ = aabb->minZ < aabb->maxZ ? aabb->maxZ : aabb->minZ;
    d->minY = aabb->minY < aabb->maxY ? aabb->minY : aabb->maxY;
    d->maxY = aabb->minY < aabb->maxY ? aabb->maxY : aabb->minY;
    return s_blocker_n++;
}

int em_collision_blocker_probe(float pos[3], float radius)
{
    int pushed = 0;
    for (int i = 0; i < s_blocker_n; i++) {
        const EmBlockerAabb *a = &s_blocker[i];

        /* Vertical gate: the player body band must overlap the blocker's
         * Y span (a blocker the player is above/below does not eject). */
        float body_lo = pos[1];
        float body_hi = pos[1] + EM_BLOCKER_BODY_H;
        if (body_hi < a->minY || body_lo > a->maxY) continue;

        /* Horizontal overlap of the player cylinder (footprint expanded by
         * `radius`) with the box footprint. The player penetrates when its
         * centre lies within the box grown by radius on each side. */
        float exMinX = a->minX - radius, exMaxX = a->maxX + radius;
        float exMinZ = a->minZ - radius, exMaxZ = a->maxZ + radius;
        if (pos[0] <= exMinX || pos[0] >= exMaxX) continue;
        if (pos[2] <= exMinZ || pos[2] >= exMaxZ) continue;

        /* Penetration depth to each of the four expanded faces; eject
         * along the axis (and direction) of LEAST penetration — the
         * standard AABB minimum-translation push-out. */
        float pXlo = pos[0] - exMinX;   /* push toward -X exits here */
        float pXhi = exMaxX - pos[0];   /* push toward +X */
        float pZlo = pos[2] - exMinZ;   /* push toward -Z */
        float pZhi = exMaxZ - pos[2];   /* push toward +Z */

        float minX = pXlo < pXhi ? pXlo : pXhi;
        float minZ = pZlo < pZhi ? pZlo : pZhi;
        if (minX <= minZ) {
            pos[0] = (pXlo < pXhi) ? exMinX : exMaxX;
        } else {
            pos[2] = (pZlo < pZhi) ? exMinZ : exMaxZ;
        }
        pushed++;
    }
    return pushed;
}

/* --- EM_BLOCKER_TEST=1 harness (headless, OS-free) ---------------------
 *
 * clear -> register one AABB -> a point INSIDE it is pushed out to the
 * nearer face (least-penetration axis), Y untouched; a point OUTSIDE is
 * unaffected; a point inside X/Z but ABOVE the blocker (out of the body
 * band) is unaffected; with an empty registry no point moves. */
int em_collision_blocker_selftest(void)
{
    int fail = 0;
    em_collision_blocker_clear();

    /* a box centred at (240,245,232.8)-ish, narrow in Z (the grate) */
    EmBlockerAabb box = { 236.0f, 244.0f,   /* X */
                          231.0f, 234.0f,   /* Z (narrow — push exits in Z) */
                          240.0f, 250.0f }; /* Y */
    int slot = em_collision_blocker_register(&box);
    carry_check(slot == 0, "first blocker takes slot 0", &fail);

    /* a point well inside, nearer the -Z face: pushed out in -Z, X/Y held */
    float p_in[3] = { 240.0f, 245.0f, 232.0f };
    int n_in = em_collision_blocker_probe(p_in, 1.0f);
    carry_check(n_in == 1, "point inside the blocker is pushed once", &fail);
    carry_check(p_in[2] <= 231.0f - 1.0f + 1e-4f,
                "pushed out past the -Z face (radius-expanded)", &fail);
    carry_check(p_in[0] == 240.0f, "blocker push leaves X untouched (Z axis won)",
                &fail);
    carry_check(p_in[1] == 245.0f, "blocker push never moves Y", &fail);

    /* a point fully outside the footprint: untouched */
    float p_out[3] = { 300.0f, 245.0f, 232.0f };
    int n_out = em_collision_blocker_probe(p_out, 1.0f);
    carry_check(n_out == 0, "point outside footprint is not pushed", &fail);
    carry_check(p_out[0] == 300.0f && p_out[2] == 232.0f,
                "outside point held", &fail);

    /* a point inside X/Z but ABOVE the body band: untouched */
    float p_high[3] = { 240.0f, 300.0f, 232.0f };
    int n_high = em_collision_blocker_probe(p_high, 1.0f);
    carry_check(n_high == 0, "point above the blocker body band is not pushed",
                &fail);
    carry_check(p_high[2] == 232.0f, "above-band point held", &fail);

    /* empty registry: nothing moves */
    em_collision_blocker_clear();
    float p_clr[3] = { 240.0f, 245.0f, 232.0f };
    int n_clr = em_collision_blocker_probe(p_clr, 1.0f);
    carry_check(n_clr == 0, "empty registry pushes nothing", &fail);
    carry_check(p_clr[2] == 232.0f, "empty-registry point held", &fail);

    printf("blocker test: %s\n", fail == 0 ? "PASS" : "FAIL");
    fflush(stdout);
    return fail;
}

/* EM_CARRY_TEST=1 / EM_BLOCKER_TEST=1 hook. Self-contained so it needs no
 * wiring in the reserved dispatcher files: a load-time constructor checks
 * the env vars and runs the headless self-test(s), then exits with the
 * pass/fail code. (GCC/Clang __attribute__((constructor)) — same family the
 * rest of the port builds with; if a stricter toolchain lacks it, call the
 * selftest functions directly from a test driver instead.) */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor))
static void em_carry_test_ctor(void)
{
    const char *e = getenv("EM_CARRY_TEST");
    if (e && e[0] == '1') {
        exit(em_collision_moving_selftest() == 0 ? 0 : 1);
    }
    e = getenv("EM_BLOCKER_TEST");
    if (e && e[0] == '1') {
        exit(em_collision_blocker_selftest() == 0 ? 0 : 1);
    }
}
#endif

/* ---- func_0019BC40 column table ------------------------------------------ */

static float column_sqrt(const EmCollColumnMath *m, float x)
{
    return m && m->sqrt ? m->sqrt(m->context, x) : sqrtf(x);
}

static float column_atan(const EmCollColumnMath *m, float x)
{
    return m && m->atan ? m->atan(m->context, x) : atanf(x);
}

/* The SDK VU0 leaves 0019F330 calls, over em_ee_float.h's VU0 macro model
 * (the forms em_coll_probe_original's walkers use): 001028D0 (four-lane
 * difference), 001028B8 (four-lane sum), 00102738 (the three-lane dot:
 * a product, then the y and z lanes summed into x) and 00103230 (three
 * lanes scaled by one scalar). Each returns 0, or -1 when a form is
 * refused. */
static int col_vu(em_vu_op op, unsigned dest, int bc, const float fs[4], const float ft[4],
                  float dst[4])
{
    uint32_t a[4], b[4], d[4];
    memcpy(a, fs, sizeof a);
    memcpy(b, ft, sizeof b);
    memcpy(d, dst, sizeof d);
    if (em_vu_vec_bits(op, dest, bc, a, b, 0, NULL, d) != EM_EE_FLOAT_OK) return -1;
    memcpy(dst, d, sizeof d);
    return 0;
}
static int col_sub(float out[4], const float a[4], const float b[4])
{
    float r[4] = { 0 };
    if (col_vu(EM_VU_SUB, 0xF, EM_VU_NO_BC, a, b, r)) return -1;
    memcpy(out, r, sizeof r);
    return 0;
}
static int col_add(float out[4], const float a[4], const float b[4])
{
    float r[4] = { 0 };
    if (col_vu(EM_VU_ADD, 0xF, EM_VU_NO_BC, a, b, r)) return -1;
    memcpy(out, r, sizeof r);
    return 0;
}
static int col_dot(float *out, const float a[4], const float b[4])
{
    float v[4];
    memcpy(v, b, sizeof v);
    if (col_vu(EM_VU_MUL, 0xE, EM_VU_NO_BC, a, v, v)) return -1;
    if (col_vu(EM_VU_ADDBC, 0x8, 1, v, v, v)) return -1;
    if (col_vu(EM_VU_ADDBC, 0x8, 2, v, v, v)) return -1;
    *out = v[0];
    return 0;
}
static int col_scale(float out[4], const float v[4], float t)
{
    float r[4], q[4] = { t, t, t, t };
    memcpy(r, v, sizeof r);
    if (col_vu(EM_VU_MULBC, 0xE, 0, r, q, r)) return -1;
    memcpy(out, r, sizeof r);
    return 0;
}

/* 001A5760 for a type-0x2000 face: out[0]/out[2] top crossing, out[1]/out[3]
 * bottom crossing, extra[0]/extra[1] the 0x7000319C / 0x700031AC values.
 * The bounds are EE add.s (em_ee_float.h). */
static int column_face(const EmCollBoxFace *f, float x, float z, float out[4], float extra[2])
{
    if (f->face != 3 && f->face != 4) return 0;
    float xlo, xhi, zlo, zhi;
    if (em_ee_c_lt(f->extent[0], 0.0f)) { xhi = f->origin[0]; xlo = em_ee_add(xhi, f->extent[0]); }
    else { xlo = f->origin[0]; xhi = em_ee_add(xlo, f->extent[0]); }
    if (em_ee_c_lt(f->extent[2], 0.0f)) { zhi = f->origin[2]; zlo = em_ee_add(zhi, f->extent[2]); }
    else { zlo = f->origin[2]; zhi = em_ee_add(zlo, f->extent[2]); }
    if (em_ee_c_le(x, xlo) || !em_ee_c_lt(x, xhi)) return 0;
    if (em_ee_c_le(z, zlo) || !em_ee_c_lt(z, zhi)) return 0;
    union { uint32_t u; float f; } big = { 0x7F7FC99Eu }, small = { 0x322BCC77u },
                                    negbig = { 0xFF7FC99Eu }, negsmall = { 0xB22BCC77u };
    if (f->face == 3) {
        out[0] = f->origin[1]; out[1] = big.f; out[2] = 1.0f; out[3] = 0.0f;
        extra[0] = small.f;
    } else {
        out[0] = negbig.f; out[1] = f->origin[1]; out[2] = 0.0f; out[3] = -1.0f;
        extra[1] = negsmall.f;
    }
    return 1;
}

/* 0019F330(pos, pos + (0,1,0), q, node): q[1] the crossing height, q[3] the
 * signed slope complement. 1 / 0, or -1 when a VU form is refused. COP1
 * operations are em_ee_float.h's; the VU0 leaves col_*. */
static int column_node(const EmCollision *c, const EmCollPoly *p, const float a[3],
                       const EmCollColumnMath *m, float q[4])
{
    /* 0019BC40 passes v1 = pos and v2 = pos with y + 1.0 (add.s); 001028D0
     * takes the difference, so d.y is (y + 1) - y in EE arithmetic. */
    const float v1[4] = { a[0], a[1], a[2], 0.0f };
    const float v2[4] = { a[0], em_ee_add(a[1], 1.0f), a[2], 0.0f };
    const float n[4] = { p->plane[0], p->plane[1], p->plane[2], 0.0f };
    float d[4], along, nq, hit[4];
    if (col_sub(d, v2, v1) || col_dot(&along, d, n) || col_dot(&nq, n, v1)) return -1;
    float t = em_ee_div(em_ee_sub(p->plane[3], nq), along);         /* sub.s, div.s */
    if (col_scale(hit, d, t) || col_add(hit, v1, hit)) return -1;   /* 00103230, 001028B8 */
    for (unsigned k = 0; k < p->vcount; ++k) {
        const float *v = c->verts + 3u * c->indices[p->first + k];
        const float *e = c->edge_n + 3u * (p->first + k);
        const float vv[4] = { v[0], v[1], v[2], 0.0f }, ee[4] = { e[0], e[1], e[2], 0.0f };
        float rel[4], dot;
        if (col_sub(rel, hit, vv) || col_dot(&dot, rel, ee)) return -1;
        if (!em_ee_c_le(dot, em_ee_float(0x3727C5ACu))) return 0;    /* +1e-5 */
    }
    for (int k = 0; k < 3; ++k) q[k] = hit[k];
    float h = column_sqrt(m, em_ee_madd(em_ee_mula(n[0], n[0]), n[2], n[2]));  /* mula.s, madd.s */
    union { uint32_t u; float f; } big = { 0x7F7FC99Eu };
    float ratio = em_ee_c_lt(h, em_ee_float(0x38D1B717u)) ? big.f           /* 1e-4 */
                : em_ee_div(em_ee_float(em_ee_bits(n[1]) & 0x7FFFFFFFu), h); /* 0011DF78, div.s */
    float angle = em_ee_sub(em_ee_float(0x3FC90FDBu), column_atan(m, ratio));  /* pi/2 - atan */
    q[3] = em_ee_c_lt(n[1], 0.0f) ? em_ee_neg(angle) : angle;
    return 1;
}

int em_collision_column_box_face(const EmCollBoxFace *face, float x, float z,
                                 float out[4], float extra[2])
{
    return face && out && extra ? column_face(face, x, z, out, extra) : 0;
}

/* 0019BC40 pass 1 over EmCollCell owners (type-0x2000 faces only). */
int em_collision_column_table(const EmCollision *c, const EmCollColumnOwner *owners,
                              unsigned owner_count, const float pos[3],
                              const EmCollColumnMath *math, EmCollColumn *out)
{
    EmCollColumnSeed seed;
    int n = 0;
    memset(&seed, 0, sizeof seed);
    for (unsigned i = 0; i < owner_count; ++i) {
        const EmCollColumnOwner *o = owners + i;
        if (!o->alive || o->owner_class != 4 || o->uid == 0xFF || !o->cell) continue;
        const EmCollCell *cell = o->cell;
        if (pos[0] < cell->bbox[0] || !(pos[0] <= cell->bbox[3])) continue;
        if (pos[2] < cell->bbox[2] || !(pos[2] <= cell->bbox[5])) continue;
        float spare[2] = { 0.0f, 0.0f };
        for (unsigned j = 0; j < cell->face_count; ++j) {
            float cross[4];
            if (!column_face(cell->faces + j, pos[0], pos[2], cross, spare)) continue;
            if (!(n < EM_COLL_COLUMN_MAX)) break;
            if (cross[0] > -3.4e37f) {
                seed.height[n] = cross[0]; seed.owner[n] = (int)i; seed.kind[n] = o->kind54;
                seed.flags[n] = 0x8000; seed.aux[n] = spare[0];
                if (!(cross[2] <= 0.0f)) seed.flags[n] |= 1;
                ++n;
            }
            if (!(n < EM_COLL_COLUMN_MAX)) break;
            if (cross[1] < 3.4e37f) {
                seed.height[n] = cross[1]; seed.owner[n] = (int)i; seed.kind[n] = o->kind54;
                seed.flags[n] = 0x8000; seed.aux[n] = spare[1];
                if (!(cross[3] <= 0.0f)) seed.flags[n] |= 1;
                ++n;
            }
        }
    }
    seed.count = n;
    int count = em_collision_column_finish(c, &seed, pos, math, out);
    return count < 0 ? 0 : count;
}

/* 0019BC40 pass 2 (grid), the selection sort, the close-pair cull and the
 * compaction, over pass-1 candidates the caller collected. */
int em_collision_column_finish(const EmCollision *c, const EmCollColumnSeed *seed,
                               const float pos[3], const EmCollColumnMath *math,
                               EmCollColumn *out)
{
    short order[EM_COLL_COLUMN_MAX];
    uint16_t flags[EM_COLL_COLUMN_MAX];
    float dist[EM_COLL_COLUMN_MAX], extra[EM_COLL_COLUMN_MAX];
    int owner[EM_COLL_COLUMN_MAX], poly[EM_COLL_COLUMN_MAX];
    uint8_t kind[EM_COLL_COLUMN_MAX];
    memset(out, 0, sizeof *out);
    if (!seed || seed->count < 0 || seed->count > EM_COLL_COLUMN_MAX) return -1;
    int n = seed->count;
    for (int i = 0; i < n; ++i) {
        order[i] = (short)i; dist[i] = seed->height[i]; extra[i] = seed->aux[i];
        flags[i] = seed->flags[i]; owner[i] = seed->owner[i]; poly[i] = -1; kind[i] = seed->kind[i];
        if (!(flags[i] & 0x8000) || owner[i] < 0) return -1;
    }
    if (c && c->blob) {
        for (uint32_t i = 0; i < c->poly_count; ++i) {
            const EmCollPoly *p = &c->polys[i];
            if (p->set != EM_COLL_SET_GRID) continue;
            if (fabsf(p->plane[1]) < 0.001f) continue;              /* 0011DF78 */
            if (p->attr >= 0x50) continue;
            float q[4];
            int crossed = column_node(c, p, pos, math, q);
            if (crossed < 0) return -1;
            if (!crossed) continue;
            if (!(n < EM_COLL_COLUMN_MAX)) break;
            dist[n] = q[1]; owner[n] = -1; poly[n] = (int)i; order[n] = (short)n; kind[n] = 0;
            flags[n] = 0x4000;
            if (!(q[3] <= 0.0f)) flags[n] |= 1;
            extra[n] = q[3];
            ++n;
        }
    }
    for (int i = 0; i < n; ++i) {                                   /* selection sort */
        int k = i;
        for (int j = i + 1; j < n; ++j)
            if (!(dist[order[k]] <= dist[order[j]])) k = j;
        if (k != i) { short t = order[i]; order[i] = order[k]; order[k] = t; }
    }
    if (n <= 0) return 0;
    for (int i = 0; i < n - 1; ++i) {
        /* sub.s then 0011DF78 (the sign bit cleared). */
        float gap = em_ee_sub(dist[order[i]], dist[order[i + 1]]);
        if (em_ee_c_lt(em_ee_float(em_ee_bits(gap) & 0x7FFFFFFFu), 3.0f)) {
            for (int side = 1; side >= 0; --side) {
                int b = order[i + side];
                if (flags[b] & 0x8000) flags[b] |= 0x80;
                else if (poly[b] >= 0 && c->polys[poly[b]].attr < 0x32) flags[b] |= 0x80;
            }
        } else {
            flags[order[i]] &= (uint16_t)~0x80;
            flags[order[i + 1]] &= (uint16_t)~0x80;
        }
    }
    if (!(flags[order[0]] & 1)) flags[order[0]] &= (uint16_t)~0x80;
    if (flags[order[n - 1]] & 1) flags[order[n - 1]] &= (uint16_t)~0x80;
    int count = 0;
    for (int i = 0; i < n; ++i) {
        int b = order[i];
        if (flags[b] & 0x80) continue;
        out->flags[count] = flags[b];
        out->height[count] = dist[b];
        out->aux[count] = extra[b];
        out->owner[count] = owner[b];
        out->poly[count] = poly[b];
        if (owner[b] >= 0) out->object_kind[count] = kind[b];
        else out->object_node[count] = (int16_t)(c->polys[poly[b]].attr |
                                                 (c->polys[poly[b]].pad << 8));
        ++count;
    }
    out->count = count;
    return count;
}

