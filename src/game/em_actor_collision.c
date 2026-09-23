/* em_actor_collision.c - original actor collision cells (see the header).
 *
 * Every routine below names the original function it translates; the
 * comparisons, their directions and the operation order follow the .s
 * (the NEARMISS readable C of 001A2370, 0019F730, 0019AB20, 0019BC40 and
 * 001A4030 was checked against it: 001A4030's C misplaces the edge-normal
 * pool and the staged normal, see docs/ACTOR_COLLISION.md). */
#include "game/em_actor_collision.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- EE single precision ------------------------------------------------- */

#define EE_MAX 3.40282347e38f   /* 0x7F7FFFFF */

static float ee(double value)
{
    if (value != value) return 0.0f;
    double magnitude = fabs(value);
    if (magnitude >= 3.4028234663852886e38) return value < 0 ? -EE_MAX : EE_MAX;
    if (magnitude == 0.0) return (float)value;
    if (magnitude < 1.1754943508222875e-38) return 0.0f;
    float result = (float)value;
    if (fabs((double)result) > magnitude) result = nextafterf(result, 0.0f);
    return result;
}

static float ee_add(float a, float b) { return ee((double)a + b); }
static float ee_sub(float a, float b) { return ee((double)a - b); }
static float ee_mul(float a, float b) { return ee((double)a * b); }
static float ee_div(float a, float b)
{
    if (b == 0.0f) return signbit(a) != signbit(b) ? -EE_MAX : EE_MAX;
    return ee((double)a / b);
}

/* 00102738: vmul.xyz, then x + y, then + z. */
static float vu_dot(const float a[3], const float b[3])
{
    return ee_add(ee_add(ee_mul(a[0], b[0]), ee_mul(a[1], b[1])), ee_mul(a[2], b[2]));
}

/* 001026A0(out, m, v): ACC = m0 * v.x, += m1 * v.y, += m2 * v.z, out = ACC + m3 * v.w. */
static void vu_apply(const float m[16], const float v[4], float out[4])
{
    float r[4];
    for (int lane = 0; lane < 4; ++lane) {
        float acc = ee_mul(m[lane], v[0]);
        acc = ee_add(acc, ee_mul(m[4 + lane], v[1]));
        acc = ee_add(acc, ee_mul(m[8 + lane], v[2]));
        r[lane] = ee_add(acc, ee_mul(m[12 + lane], v[3]));
    }
    memcpy(out, r, sizeof r);
}

/* ---- Original-layout byte access (little-endian image) ------------------- */

static float rd_f(const uint8_t *p, unsigned off) { float v; memcpy(&v, p + off, 4); return v; }
static void wr_f(uint8_t *p, unsigned off, float v) { memcpy(p + off, &v, 4); }
static uint32_t rd_u32(const uint8_t *p, unsigned off) { uint32_t v; memcpy(&v, p + off, 4); return v; }
static uint16_t rd_u16(const uint8_t *p, unsigned off) { uint16_t v; memcpy(&v, p + off, 2); return v; }
static int16_t rd_s16(const uint8_t *p, unsigned off) { int16_t v; memcpy(&v, p + off, 2); return v; }

/* Prim size by header, or 0 for an unknown type nibble (the walkers do not
 * advance over one). */
static uint32_t prim_size(const uint8_t *p)
{
    uint16_t h = rd_u16(p, 0);
    switch (h & 0xF000) {
    case 0x8000: return h & 0x800 ? 0x24 : 0x14;
    case 0x4000: return h & 0x800 ? 0x2C : 0x18;
    case 0x2000: return 0x1C;
    case 0x1000: return h & 0x800 ? 0x24u + 0x30u * p[2] : 0x14u + 0x18u * p[2];
    default: return 0;
    }
}

/* ---- The cell directory -------------------------------------------------- */

static int hull_valid(const uint8_t *bytes, uint32_t size, uint32_t offset)
{
    if (offset > size || size - offset < 0x1C) return 0;
    int16_t count = rd_s16(bytes, offset + 0x18);
    uint32_t at = offset + 0x1C;
    for (int16_t i = 0; i < count; ++i) {
        if (at > size || size - at < 4) return 0;
        uint32_t n = prim_size(bytes + at);
        if (!n) return 1;              /* the walkers stop advancing here */
        if (size - at < n) return 0;
        at += n;
    }
    return 1;
}

void em_actor_cells_free(EmActorCellTable *table)
{
    if (!table) return;
    free(table->bytes);
    memset(table, 0, sizeof *table);
}

int em_actor_cells_init(EmActorCellTable *table, const void *image, size_t size)
{
    if (!table) return -1;
    em_actor_cells_free(table);
    if (!image || size < 4 || size > 0x1000000) return -1;
    const uint8_t *src = image;
    uint32_t count = rd_u32(src, 0);
    if (count > 0x7FFF || 4u + 4u * count > size) return -1;
    for (uint32_t uid = 0; uid < count; ++uid) {
        uint32_t word = rd_u32(src, 4 + 4 * uid);
        if (!word) continue;
        /* Pass 1 masks the flag bits, pass 2 and 001A2370 add the raw word:
         * both views must name a whole hull. */
        if (!hull_valid(src, (uint32_t)size, word & 0x3FFFFFFFu)) return -1;
        if (!(word & 0x80000000u) && !hull_valid(src, (uint32_t)size, word)) return -1;
    }
    table->bytes = malloc(size);
    if (!table->bytes) return -1;
    memcpy(table->bytes, src, size);
    table->size = (uint32_t)size;
    table->count = (int16_t)count;
    return 0;
}

int em_actor_cells_load(EmActorCellTable *table, const char *path)
{
    if (!table || !path) return -1;
    FILE *file = fopen(path, "rb");
    if (!file) return -1;
    uint8_t *data = NULL;
    long size = -1;
    if (fseek(file, 0, SEEK_END) == 0) size = ftell(file);
    if (size > 0 && size <= 0x1000000 && fseek(file, 0, SEEK_SET) == 0) {
        data = malloc((size_t)size);
        if (data && fread(data, 1, (size_t)size, file) != (size_t)size) { free(data); data = NULL; }
    }
    fclose(file);
    if (!data) return -1;
    int result = em_actor_cells_init(table, data, (size_t)size);
    free(data);
    return result;
}

static uint32_t cell_word(const EmActorCellTable *table, unsigned uid)
{
    return rd_u32(table->bytes, 4 + 4 * uid);
}

const uint8_t *em_actor_cells_hull(const EmActorCellTable *table, unsigned uid)
{
    if (!table || !table->bytes || uid >= (unsigned)table->count) return NULL;
    uint32_t word = cell_word(table, uid);
    if (!word || word & 0x80000000u) return NULL;
    return table->bytes + word;
}

int em_actor_cells_bounds(const EmActorCellTable *table, unsigned uid, float out[6])
{
    const uint8_t *hull = em_actor_cells_hull(table, uid);
    if (!hull || !out) return 0;
    for (int k = 0; k < 6; ++k) out[k] = rd_f(hull, 4u * k);
    return 1;
}

/* 001A2370 ------------------------------------------------------------------ */

static void bound_min(float *slot, float value) { if (!(*slot <= value)) *slot = value; }
static void bound_max(float *slot, float value) { if (*slot < value) *slot = value; }

int em_actor_cells_retransform_001A2370(EmActorCellTable *table, uint16_t uid_halfword,
                                        const float matrix[16])
{
    if (!table || !matrix) return -1;
    unsigned uid = (uid_halfword >> 8) & 0xFF;
    if (uid == 0xFF || !table->bytes) return 1;
    /* The original reads the word before its count check (uid < 0xFF always
     * indexes the word array of a directory that has it). */
    if (4 + 4 * uid + 4 > table->size) return -1;
    uint32_t word = cell_word(table, uid);
    if (!word) return 1;
    if (!((int)uid < table->count)) return 1;
    if (word & 0x80000000u) return -1;          /* a raw pass-2 offset outside the image */
    uint8_t *hull = table->bytes + word;
    int16_t count = rd_s16(hull, 0x18);
    uint8_t *p = hull + 0x1C;
    if (!(rd_u16(p, 0) & 0x800)) return 1;
    float box[6];                                /* D_70003400 min xyz, D_70003410 max xyz */
    if (count != 0)
        for (int k = 0; k < 3; ++k) { box[k] = 3.4e38f; box[3 + k] = -3.4e38f; }
    for (int16_t index = 0; index < count; ++index) {
        uint16_t type = rd_u16(p, 0) & 0xF000;
        if (type == 0x8000) {
            /* Local centre +0x14 -> world centre +4 (w 1), radius +0x10. */
            float v[4] = { rd_f(p, 0x14), rd_f(p, 0x18), rd_f(p, 0x1C), 1.0f };
            vu_apply(matrix, v, v);
            for (int k = 0; k < 3; ++k) wr_f(p, 4 + 4 * k, v[k]);
            float r = rd_f(p, 0x10);
            for (int k = 0; k < 3; ++k) bound_min(&box[k], ee_sub(rd_f(p, 4 + 4 * k), r));
            for (int k = 0; k < 3; ++k) bound_max(&box[3 + k], ee_add(r, rd_f(p, 4 + 4 * k)));
            p += 0x24;
        } else if (type == 0x4000) {
            /* Local centre +0x18 -> world +4 (w 1); radius +0x10, half height +0x14. */
            float v[4] = { rd_f(p, 0x18), rd_f(p, 0x1C), rd_f(p, 0x20), 1.0f };
            vu_apply(matrix, v, v);
            for (int k = 0; k < 3; ++k) wr_f(p, 4 + 4 * k, v[k]);
            float x = rd_f(p, 4), y = rd_f(p, 8), z = rd_f(p, 0xC);
            float r = rd_f(p, 0x10), h = rd_f(p, 0x14);
            bound_min(&box[0], ee_sub(x, r));
            bound_min(&box[2], ee_sub(z, r));
            bound_min(&box[1], ee_sub(y, h));
            bound_max(&box[3], ee_add(x, r));
            bound_max(&box[5], ee_add(z, r));
            bound_max(&box[4], ee_add(y, h));
            p += 0x2C;
        } else if (type == 0x1000) {
            /* n points then n edge normals: world lanes at +0x14, the local
             * copy after them (axis at +0x14 + 0x18n, lanes 0x10 further). */
            unsigned n = p[2];
            const uint8_t *local = p + n * 0x18 + 0x14;
            float axis[4] = { rd_f(local, 0), rd_f(local, 4), rd_f(local, 8), 0.0f };
            vu_apply(matrix, axis, axis);
            for (int k = 0; k < 3; ++k) wr_f(p, 4 + 4 * k, axis[k]);
            for (unsigned k = 0; k < 2 * n; ++k) {
                float v[4] = { rd_f(local, 0x10 + 12 * k), rd_f(local, 0x14 + 12 * k),
                               rd_f(local, 0x18 + 12 * k), k < n ? 1.0f : 0.0f };
                vu_apply(matrix, v, v);
                for (int j = 0; j < 3; ++j) wr_f(p, 0x14 + 12 * k + 4 * j, v[j]);
                if (k < n) {
                    bound_min(&box[0], v[0]); bound_min(&box[1], v[1]); bound_min(&box[2], v[2]);
                    bound_max(&box[3], v[0]); bound_max(&box[4], v[1]); bound_max(&box[5], v[2]);
                }
            }
            /* d = axis . first point (00102738 with the w lanes zeroed). */
            float a[3] = { rd_f(p, 4), rd_f(p, 8), rd_f(p, 0xC) };
            float q[3] = { rd_f(p, 0x14), rd_f(p, 0x18), rd_f(p, 0x1C) };
            wr_f(p, 0x10, vu_dot(a, q));
            p += 0x24 + 0x30 * n;
        }
        /* Any other type: the index advances, the pointer stays. */
    }
    if (count != 0)
        for (int k = 0; k < 3; ++k) { wr_f(hull, 4 * k, box[k]); wr_f(hull, 0xC + 4 * k, box[3 + k]); }
    return 1;
}

/* ---- Class lists ---------------------------------------------------------- */

static const int16_t kListCap[EM_ACTOR_LIST_COUNT] = { 0x0C, 0x30, 0x40, 0x80, 0x40, 0x20 };

void em_actor_class_lists_reset(EmActorClassLists *lists)
{
    if (lists) memset(lists, 0, sizeof *lists);
}

static void list_push(EmActorClassList *list, int16_t cap, const EmActor *actor)
{
    if (list->live < cap) list->slot[list->live++] = actor->self;   /* a0[5] */
}

int em_actor_class_push4_001B1D20(EmActorClassLists *lists, const EmActor *actor)
{
    if (!lists || !actor) return -1;
    list_push(&lists->list[EM_ACTOR_LIST_CLASS4], kListCap[EM_ACTOR_LIST_CLASS4], actor);
    return 1;
}

int em_actor_class_publish_001B1B70(EmActorClassLists *lists, const EmActor *actor)
{
    if (!lists || !actor) return -1;
    if (actor->cls & 0x80)
        list_push(&lists->list[EM_ACTOR_LIST_FLAG80], kListCap[EM_ACTOR_LIST_FLAG80], actor);
    int which;
    switch (actor->cls & ~0xE0) {
    case 1: which = EM_ACTOR_LIST_CLASS1; break;
    case 0xA: case 2: which = EM_ACTOR_LIST_CLASS2; break;
    case 4: which = EM_ACTOR_LIST_CLASS4; break;
    case 7: which = EM_ACTOR_LIST_CLASS7; break;
    case 0xD: which = EM_ACTOR_LIST_CLASS_D; break;
    default: return 1;
    }
    list_push(&lists->list[which], kListCap[which], actor);
    return 1;
}

void em_actor_class_lists_swap_001AAD00(EmActorClassLists *lists)
{
    if (!lists) return;
    lists->count_b58 = 0;
    for (int i = 0; i < EM_ACTOR_LIST_COUNT; ++i) {
        lists->list[i].published = lists->list[i].live;
        lists->list[i].live = 0;
    }
}

const EmActor *em_actor_class_list_entry(const EmActorClassLists *lists, int which, int j)
{
    if (!lists || which < 0 || which >= EM_ACTOR_LIST_COUNT) return NULL;
    const EmActorClassList *list = &lists->list[which];
    if (j < 0 || j >= list->published) return NULL;
    return list->slot[list->published - 1 - j];
}

/* ---- The segment state of the walkers (0x70003190..0x700031D8) ----------- */

typedef struct {
    float start[3];          /* 0x70003190 */
    float end[3];            /* 0x700031A0 */
    float point[3];          /* 0x700031B0 */
    uint16_t record_class;   /* 0x700030CA: D_700030B0 +0x1A */
    float record_normal[3];  /* 0x700030D4: D_700030B0 +0x24 */
    const EmActor *entity;   /* 0x700031D4 */
    int16_t query_class;     /* 0x7000324E */
    const void *self;        /* 0x70003254 */
} Segment;

/* 001A44B0: the vertical test of a 0x8000 (sphere, half height = radius)
 * or 0x4000 (cylinder, half height +0x14) prim. */
static int prim_round_vertical(const uint8_t *p, Segment *s)
{
    const uint8_t *q = p + 4;
    float half = (rd_u16(p, 0) & 0x8000) ? rd_f(q, 0xC) : rd_f(q, 0x10);
    float lo, hi;
    int upward;
    if (s->start[1] <= s->end[1]) { upward = 1; lo = s->start[1]; hi = s->end[1]; }
    else { upward = 0; lo = s->end[1]; hi = s->start[1]; }
    float face = upward ? ee_sub(rd_f(q, 4), half) : ee_add(rd_f(q, 4), half);
    if (face <= lo) return 0;
    if (!(face < hi)) return 0;
    float dx = ee_sub(s->start[0], rd_f(q, 0));
    float dz = ee_sub(s->start[2], rd_f(q, 8));
    float r2 = ee_mul(rd_f(q, 0xC), rd_f(q, 0xC));
    float d2 = ee_add(ee_mul(dx, dx), ee_mul(dz, dz));          /* mula, madd */
    if (r2 < d2) return 0;
    s->point[0] = s->start[0];
    s->point[2] = s->start[2];
    s->record_normal[0] = 0.0f;
    s->record_normal[2] = 0.0f;
    if (upward) {
        s->record_class = 0x8000;
        s->record_normal[1] = -1.0f;
        s->point[1] = ee_sub(rd_f(q, 4), half);
    } else {
        s->record_class = 0x4000;
        s->record_normal[1] = 1.0f;
        s->point[1] = ee_add(rd_f(q, 4), half);
    }
    return 1;
}

/* 001A4650: the vertical test of a 0x2000 face (faces 3 and 4 only). */
static int prim_face_vertical(const uint8_t *p, Segment *s)
{
    const uint8_t *q = p + 4;
    unsigned face = p[2];
    if ((unsigned)(face - 3) >= 2) return 0;
    float lo, hi;
    if (s->start[1] < s->end[1]) {                /* upward: only the bottom face */
        if (face == 3) return 0;
        lo = s->start[1]; hi = s->end[1];
    } else {                                      /* downward: only the top face */
        if (face == 4) return 0;
        lo = s->end[1]; hi = s->start[1];
    }
    float ex = rd_f(q, 0xC), xlo, xhi;
    if (ex < 0.0f) { xhi = rd_f(q, 0); xlo = ee_add(xhi, ex); }
    else { xlo = rd_f(q, 0); xhi = ee_add(xlo, ex); }
    float ez = rd_f(q, 0x14), zlo, zhi;
    if (ez < 0.0f) { zhi = rd_f(q, 8); zlo = ee_add(zhi, ez); }
    else { zlo = rd_f(q, 8); zhi = ee_add(zlo, ez); }
    float x = s->start[0], z = s->start[2];
    if (x < xlo || !(x <= xhi)) return 0;
    if (z < zlo || !(z <= zhi)) return 0;
    float y = rd_f(q, 4);
    if (!(lo < y)) return 0;
    if (hi <= y) return 0;
    s->point[0] = x;
    s->point[1] = y;
    s->record_normal[0] = 0.0f;
    s->record_normal[2] = 0.0f;
    s->point[2] = z;
    if (face == 3) { s->record_class = 0x4000; s->record_normal[1] = 1.0f; }
    else { s->record_class = 0x8000; s->record_normal[1] = -1.0f; }
    return 1;
}

/* 001A4030: the convex n-gon segment test (both prim layouts: world
 * normal +4, d +0x10, points from +0x14, edge normals 12n further). */
static int prim_ngon(const uint8_t *p, Segment *s)
{
    const float qa[3] = { s->start[0], s->start[1], s->start[2] };
    const float qb[3] = { s->end[0], s->end[1], s->end[2] };
    const float n[3] = { rd_f(p, 4), rd_f(p, 8), rd_f(p, 0xC) };
    float dir[3];
    for (int k = 0; k < 3; ++k) dir[k] = ee_sub(qb[k], qa[k]);  /* 001028D0 */
    float d = rd_f(p, 0x10);
    float along = vu_dot(dir, n);
    if (!(along <= -1e-5f)) return 0;
    float t = ee_div(ee_sub(d, vu_dot(n, qa)), along);
    float hit[3];
    for (int k = 0; k < 3; ++k) hit[k] = ee_add(qa[k], ee_mul(dir[k], t)); /* 00103230, 001028B8 */
    for (int k = 0; k < 3; ++k) {
        if (!(qa[k] <= hit[k] || qb[k] <= hit[k])) return 0;
        if (qa[k] < hit[k] && qb[k] < hit[k]) return 0;
    }
    const uint8_t *vert = p + 0x14;
    const uint8_t *edge = p + 0x14 + 12u * p[2];
    for (unsigned k = 0; k < p[2]; ++k) {
        float rel[3], e[3];
        for (int j = 0; j < 3; ++j) {
            rel[j] = ee_sub(hit[j], rd_f(vert, 4 * j));
            e[j] = rd_f(edge, 4 * j);
        }
        if (!(vu_dot(rel, e) <= 1e-5f)) return 0;
        vert += 12;
        edge += 12;
    }
    memcpy(s->point, hit, sizeof hit);
    float sum = ee_add(ee_mul(n[0], n[0]), ee_mul(n[2], n[2]));   /* mula, madd */
    float ratio = ee_div(ee_mul(n[1], n[1]), sum);
    uint16_t cls;
    if (n[1] < 0.0f) cls = ratio < 0.49029058f ? 0x2000 : ratio <= 3.0f ? 0x0800 : 0x8000;
    else cls = ratio < 0.49029058f ? 0x2000 : ratio <= 3.0f ? 0x1000 : 0x4000;
    s->record_class = cls;
    memcpy(s->record_normal, n, sizeof n);
    return 1;
}

/* One prim of a vertical walk: the test result and the next prim. */
static int prim_vertical(const uint8_t **cursor, Segment *s, int pass2, int *hit)
{
    const uint8_t *p = *cursor;
    uint16_t header = rd_u16(p, 0);
    switch (header & 0xF000) {
    case 0x8000:
        if (pass2) *hit = prim_round_vertical(p, s);
        else *hit = 0;
        break;
    case 0x4000: *hit = prim_round_vertical(p, s); break;
    case 0x2000: *hit = prim_face_vertical(p, s); break;
    case 0x1000: *hit = prim_ngon(p, s); break;
    default: return 0;          /* no advance; the hit variable keeps its value */
    }
    *cursor = p + prim_size(p);
    return 0;
}

static int in_hull_box(const uint8_t *hull, const Segment *s, float lo, float hi)
{
    if (s->start[0] < rd_f(hull, 0) || !(s->start[0] <= rd_f(hull, 0xC))) return 0;
    if (s->start[2] < rd_f(hull, 8) || !(s->start[2] <= rd_f(hull, 0x14))) return 0;
    if (hi < rd_f(hull, 4) || !(lo <= rd_f(hull, 0x10))) return 0;
    return 1;
}

/* 0019F730. Returns 1 when nothing was hit, 0 on a hit, -1 on a fault. */
static int vertical_0019F730(const EmActorCollisionWorld *w, Segment *s)
{
    const EmActorCellTable *t = w->table;
    float lo, hi;
    if (s->start[1] <= s->end[1]) { lo = s->start[1]; hi = s->end[1]; }
    else { lo = s->end[1]; hi = s->start[1]; }
    int result = 1;
    for (int i = 0; i < t->count; ++i) {                    /* pass 1: static cells */
        uint32_t word = cell_word(t, (unsigned)i);
        if (!(word & 0x80000000u)) break;
        if (word & 0x40000000u) continue;
        if (!w->static_kind || (unsigned)i >= w->static_kind_count) return -1;
        int16_t kind = w->static_kind[i];                    /* 0x70003B88 */
        if (kind >= 0x5A) continue;
        if (kind == 0x51 && s->query_class != 0) continue;
        if (kind == 0x52 && s->query_class != 2) continue;
        if (kind == 0x53 && s->query_class == -1) continue;
        const uint8_t *hull = t->bytes + (word & 0x3FFFFFFFu);
        if (!in_hull_box(hull, s, lo, hi)) continue;
        const uint8_t *p = hull + 0x1C;
        int hit = 0;
        for (int16_t j = 0; j < rd_s16(hull, 0x18); ++j) {
            prim_vertical(&p, s, 0, &hit);
            if (hit) break;
        }
        if (!hit) continue;
        result = 0;
        s->end[1] = s->point[1];
        s->entity = NULL;
        s->record_class = (uint16_t)((s->record_class & 0xFF00) | (uint8_t)kind);
        if (!(s->start[1] <= s->end[1])) lo = s->end[1]; else hi = s->end[1];
    }
    const EmActorClassList *list = &w->lists->list[EM_ACTOR_LIST_CLASS4];
    for (int j = 0; j < list->published; ++j) {             /* pass 2: owner cells */
        const EmActor *a = list->slot[list->published - 1 - j];
        if (!a) return -1;
        if (a->status == 0) continue;
        if ((a->cls & 0x1F) != 4) continue;
        if (s->self == (const void *)a) continue;
        unsigned uid = (a->uid >> 8) & 0xFF;
        if (uid == 0xFF) continue;
        if (4 + 4 * uid + 4 > t->size) return -1;
        uint32_t word = cell_word(t, uid);
        if (!word) continue;
        if ((uint8_t)a->kind >= 0x51) continue;
        if (!((int)uid < t->count)) continue;
        if (word & 0x80000000u) return -1;
        const uint8_t *hull = t->bytes + word;
        if (!in_hull_box(hull, s, lo, hi)) continue;
        const uint8_t *p = hull + 0x1C;
        int hit = 0, found = 0;
        for (int16_t k = 0; k < rd_s16(hull, 0x18); ++k) {
            prim_vertical(&p, s, 1, &hit);
            if (!hit) continue;
            found = 1;
            result = 0;
            s->end[1] = s->point[1];
            s->entity = a;
            s->record_class = (uint16_t)((s->record_class & 0xFF00) | (uint8_t)a->kind);
        }
        if (found) {
            if (s->start[1] <= s->end[1]) hi = s->end[1];
            else lo = s->end[1];
        }
    }
    return result;
}

/* 0019AB20 ------------------------------------------------------------------ */

int em_actor_collision_ground_0019AB20(const EmActorCollisionWorld *world,
                                       const EmActorCollisionQuery *query,
                                       const float position[3], const float probe[3],
                                       uint32_t mask, EmActorCollisionHit *hit)
{
    if (!world || !query || !position || !probe || !hit) return -1;
    if ((mask & 2) && (!world->table || !world->table->bytes || !world->lists)) return -1;
    if ((mask & 4) && (!world->grid || !world->grid->blob)) return -1;
    if ((mask & 0x80000000u) && !query->feet_y) return -1;
    Segment s;
    memset(&s, 0, sizeof s);
    memcpy(s.start, position, sizeof s.start);
    memcpy(s.end, position, sizeof s.end);
    s.start[1] = ee_sub(s.start[1], probe[1]);
    float nudge = probe[1] < 0.0f ? 0.001f : -0.001f;         /* 0x3A83126F */
    s.start[1] = ee_add(s.start[1], nudge);
    s.query_class = query->cls & 0x1F;
    int kind = 0, record = EM_ACTOR_RECORD_NONE, poly = -1;
    if (mask & 2) {
        s.self = query->self;
        record = EM_ACTOR_RECORD_CELL;                          /* 0x700031D0 = D_700030B0 */
        int r = vertical_0019F730(world, &s);
        if (r < 0) return -1;
        if (r == 0) kind = 2;
    }
    float grid_start[3] = { 0.0f, 0.0f, 0.0f }, grid_end[3] = { 0.0f, 0.0f, 0.0f };
    if (mask & 4) {
        float point[3];
        int node;
        memcpy(grid_start, s.start, sizeof grid_start);
        memcpy(grid_end, s.end, sizeof grid_end);
        if (em_collision_grid_vertical(world->grid, s.start, s.end, s.query_class, point, &node)) {
            memcpy(s.point, point, sizeof point);
            record = EM_ACTOR_RECORD_GRID;
            poly = node;
            kind = 4;
        }
    }
    s.start[1] = ee_sub(s.start[1], nudge);
    memset(hit, 0, sizeof *hit);
    hit->poly = -1;
    hit->entity = s.entity;
    memcpy(hit->grid_start, grid_start, sizeof grid_start);
    memcpy(hit->grid_end, grid_end, sizeof grid_end);
    if (kind) {
        memcpy(s.end, position, sizeof s.end);
        for (int k = 0; k < 3; ++k) {
            hit->point[k] = s.point[k];
            hit->delta[k] = ee_sub(s.point[k], s.end[k]);
        }
        if (mask & 0x80000000u) *query->feet_y = ee_add(*query->feet_y, hit->delta[1]);
        hit->record = record;
        if (record == EM_ACTOR_RECORD_GRID) {
            const EmCollPoly *p = &world->grid->polys[poly];
            hit->poly = poly;
            hit->node_class_known = (world->grid->flags & EM_COLL_FLAG_NODE_CLASS) != 0;
            hit->node = (uint16_t)(p->attr | (hit->node_class_known ? p->pad << 8 : 0));
            memcpy(hit->normal, p->plane, sizeof hit->normal);
        } else {
            hit->node_class_known = 1;
            hit->node = s.record_class;
            memcpy(hit->normal, s.record_normal, sizeof hit->normal);
        }
    } else {
        hit->record = EM_ACTOR_RECORD_NONE;
    }
    hit->kind = kind;
    return kind;
}

/* 0019BC40 pass 1 ------------------------------------------------------------ */

/* 001A56A0(q, out, p): a 0x8000/0x4000 prim's column crossings. */
static int column_round(const uint8_t *p, float x, float z, float out[4], float extra[2])
{
    const uint8_t *c = p + 4;
    float half = (rd_u16(p, 0) & 0x8000) ? rd_f(c, 0xC) : rd_f(c, 0x10);
    float dz = ee_sub(z, rd_f(c, 8));
    float dx = ee_sub(x, rd_f(c, 0));
    float r2 = ee_mul(rd_f(c, 0xC), rd_f(c, 0xC));
    float d2 = ee_add(ee_mul(dx, dx), ee_mul(dz, dz));          /* mula, madd */
    if (r2 < d2) return 0;
    out[0] = ee_add(rd_f(c, 4), half);
    out[1] = ee_sub(rd_f(c, 4), half);
    out[2] = 1.0f;
    out[3] = -1.0f;
    union { uint32_t u; float f; } small = { 0x322BCC77u }, negsmall = { 0xB22BCC77u };
    extra[0] = small.f;
    extra[1] = negsmall.f;
    return 1;
}

/* 001A58B0(q, out, p): an n-gon's crossing of the vertical line (x, z). */
static int column_ngon(const uint8_t *p, float x, float z, const EmCollColumnMath *m,
                       float out[4], float extra[2])
{
    const float n[3] = { rd_f(p, 4), rd_f(p, 8), rd_f(p, 0xC) };
    const float pos[3] = { x, 0.0f, z };
    const float dir[3] = { 0.0f, 1.0f, 0.0f };
    float along = vu_dot(dir, n);
    float t = ee_div(ee_sub(rd_f(p, 0x10), vu_dot(n, pos)), along);
    float hit[3];
    for (int k = 0; k < 3; ++k) hit[k] = ee_add(pos[k], ee_mul(dir[k], t));
    const uint8_t *vert = p + 0x14;
    const uint8_t *edge = p + 0x14 + 12u * p[2];
    for (unsigned k = 0; k < p[2]; ++k) {
        float rel[3], e[3];
        for (int j = 0; j < 3; ++j) {
            rel[j] = ee_sub(hit[j], rd_f(vert, 4 * j));
            e[j] = rd_f(edge, 4 * j);
        }
        if (!(vu_dot(rel, e) <= 1e-5f)) return 0;
        vert += 12;
        edge += 12;
    }
    float square = ee_add(ee_mul(n[0], n[0]), ee_mul(n[2], n[2]));   /* mula, madd */
    float h = m->sqrt(m->context, square);                              /* 0011E748 */
    union { uint32_t u; float f; } big = { 0x7F7FC99Eu }, negbig = { 0xFF7FC99Eu };
    float ratio = h < 1e-4f ? big.f : ee_div(fabsf(n[1]), h);            /* 0011DF78 */
    float angle = ee_sub(1.57079637050628662f,
                         m->atan(m->context, ratio));                      /* 0011DBB8 */
    if (n[1] <= 0.0f) {
        out[0] = negbig.f; out[1] = hit[1]; out[2] = 0.0f; out[3] = -1.0f;
        extra[1] = n[1] < 0.0f ? -angle : angle;
    } else {
        out[0] = hit[1]; out[1] = big.f; out[2] = 1.0f; out[3] = 0.0f;
        extra[0] = n[1] < 0.0f ? -angle : angle;
    }
    return 1;
}

int em_actor_collision_column_0019BC40(const EmActorCollisionWorld *world,
                                       const float position[3],
                                       const EmCollColumnMath *math, EmCollColumn *out)
{
    if (!world || !world->table || !world->table->bytes || !world->lists || !position || !out)
        return -1;
    /* The SDK calls are original workers: no host stand-in is chosen here. */
    if (!math || !math->sqrt || !math->atan) return -1;
    const EmActorCellTable *t = world->table;
    const EmActorClassList *list = &world->lists->list[EM_ACTOR_LIST_CLASS4];
    EmCollColumnSeed seed;
    memset(&seed, 0, sizeof seed);
    float extra[2] = { 0.0f, 0.0f };       /* 0x7000319C / 0x700031AC */
    int n = 0;
    for (int j = 0; j < list->published; ++j) {
        const EmActor *a = list->slot[list->published - 1 - j];
        if (!a) return -1;
        if (a->status == 0) continue;
        if ((a->cls & 0x1F) != 4) continue;
        unsigned uid = a->uid >> 8;
        if (uid == 0xFF) continue;
        if (4 + 4 * uid + 4 > t->size) return -1;
        uint32_t word = cell_word(t, uid);
        if (!word) continue;
        /* No uid < count test here: a word past the offset array is read
         * as the original reads it, but must still name a whole hull. */
        if (word & 0x80000000u || (!((int)uid < t->count) && !hull_valid(t->bytes, t->size, word)))
            return -1;
        const uint8_t *hull = t->bytes + word;
        if (position[0] < rd_f(hull, 0) || !(position[0] <= rd_f(hull, 0xC))) continue;
        if (position[2] < rd_f(hull, 8) || !(position[2] <= rd_f(hull, 0x14))) continue;
        const uint8_t *p = hull + 0x1C;
        int hit = 0;
        float cross[4];
        for (int16_t k = 0; k < rd_s16(hull, 0x18); ++k) {
            uint16_t header = rd_u16(p, 0);
            switch (header & 0xF000) {
            case 0x8000: hit = 0; break;
            case 0x4000: hit = column_round(p, position[0], position[2], cross, extra); break;
            case 0x2000: {
                EmCollBoxFace face;
                face.face = p[2];
                for (int i = 0; i < 3; ++i) {
                    face.origin[i] = rd_f(p, 4 + 4 * i);
                    face.extent[i] = rd_f(p, 0x10 + 4 * i);
                }
                hit = em_collision_column_box_face(&face, position[0], position[2], cross, extra);
                break;
            }
            case 0x1000: hit = column_ngon(p, position[0], position[2], math, cross, extra); break;
            default:
                /* The pointer stays and the hit variable keeps its value;
                 * with no earlier value to keep the original reads a stale
                 * register, so this faults instead. */
                return -1;
            }
            p += prim_size(p);
            if (!hit) continue;
            if (!(n < EM_COLL_COLUMN_MAX)) break;
            if (cross[0] > -3.4e37f) {
                seed.height[n] = cross[0]; seed.owner[n] = j; seed.kind[n] = (uint8_t)a->kind;
                seed.flags[n] = 0x8000; seed.aux[n] = extra[0];
                if (!(cross[2] <= 0.0f)) seed.flags[n] |= 1;
                ++n;
            }
            if (!(n < EM_COLL_COLUMN_MAX)) break;
            if (cross[1] < 3.4e37f) {
                seed.height[n] = cross[1]; seed.owner[n] = j; seed.kind[n] = (uint8_t)a->kind;
                seed.flags[n] = 0x8000; seed.aux[n] = extra[1];
                if (!(cross[3] <= 0.0f)) seed.flags[n] |= 1;
                ++n;
            }
        }
    }
    seed.count = n;
    return em_collision_column_finish(world->grid, &seed, position, math, out);
}

/* ---- Worker adapters ------------------------------------------------------- */

int em_actor_collision_owner_hull(void *owner, const float matrix[16])
{
    EmActorCollisionOwner *o = owner;
    if (!o || !o->world || !o->world->table || !o->actor) return -1;
    return em_actor_cells_retransform_001A2370(o->world->table, o->actor->uid, matrix) == 1 ? 1 : -1;
}

int em_actor_collision_owner_hull_bounds(void *owner, float bounds[6])
{
    EmActorCollisionOwner *o = owner;
    if (!o || !o->world || !o->actor || !bounds) return -1;
    return em_actor_cells_bounds(o->world->table, (o->actor->uid >> 8) & 0xFF, bounds) ? 1 : -1;
}

int em_actor_collision_owner_publish(void *owner)
{
    EmActorCollisionOwner *o = owner;
    if (!o || !o->lists) return -1;
    return em_actor_class_publish_001B1B70(o->lists, o->actor);
}

int em_actor_collision_owner_contact(void *owner)
{
    EmActorCollisionOwner *o = owner;
    if (!o || !o->lists) return -1;
    return em_actor_class_push4_001B1D20(o->lists, o->actor);
}

int em_actor_collision_owner_probe(void *owner, float position[4], const float from[3], float dy,
                                   uint32_t mode, EmCrateProbe *out)
{
    EmActorCollisionOwner *o = owner;
    if (!o || !o->world || !o->actor || !position || !from || !out) return -1;
    EmActorCollisionQuery q = { o->actor->self, (uint8_t)(o->actor->cls & 0x1F), &position[1] };
    const float probe[3] = { 0.0f, dy, 0.0f };
    EmActorCollisionHit hit;
    int kind = em_actor_collision_ground_0019AB20(o->world, &q, from, probe, mode, &hit);
    if (kind < 0) return -1;
    uint32_t address = 0;
    if (hit.entity) {
        if (!o->pool || !(address = em_actor_pool_address(o->pool, hit.entity))) return -1;
    }
    out->result = kind;
    out->actor = (int32_t)address;
    return 1;
}

int em_actor_collision_player_ground(void *player, const float position[3], const float probe[3],
                                     unsigned mask, EmPlayerProbeHit *hit)
{
    EmActorCollisionPlayer *p = player;
    if (!p || !p->world || !hit) return -1;
    EmActorCollisionHit h;
    int kind = em_actor_collision_ground_0019AB20(p->world, &p->query, position, probe, mask, &h);
    if (kind < 0) return -1;
    if (kind && (!h.node_class_known || (h.node & 0xFF) == 0x35)) return -1;
    p->entity = h.entity;
    memset(hit, 0, sizeof *hit);
    hit->kind = kind;
    hit->node = kind ? h.node : 0;
    hit->entity = h.entity != NULL;
    if (h.entity) {
        hit->entity_flags = h.entity->cls;     /* *(0x700031D4) + 2 */
        hit->entity_type = h.entity->model;    /* *(0x700031D4) + 3 */
    }
    memcpy(hit->point, h.point, sizeof hit->point);
    memcpy(hit->delta, h.delta, sizeof hit->delta);
    memcpy(hit->normal, h.normal, sizeof hit->normal);
    return kind;
}
