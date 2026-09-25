/* AREA11 record 12, overlay director 0x008253F0. See the header and
 * docs/DIRECTOR_ORIGINAL.md. Every address cited is a runtime address. */
#include "game/em_director_original.h"

#include <math.h>
#include <string.h>

#include "game/em_pose_math.h"

#pragma STDC FP_CONTRACT OFF

static float f32(uint32_t bits)
{
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

static int fail(uint32_t *fault_address, uint32_t address)
{
    if (fault_address)
        *fault_address = address;
    return -1;
}

/* One beat's constants, read from its body. */
typedef struct {
    uint32_t body;        /* runtime entry */
    uint32_t y_low;       /* lower bound: Y < it rejects */
    uint32_t y_high;      /* upper bound: Y <= it passes (beat 0 only; 0 = none) */
    uint32_t quad;        /* 001B1EA0's third argument */
    uint32_t script;      /* 001BA1A0's second argument */
    uint8_t completion;   /* D_00810813 store */
    uint32_t y_read;      /* the load of D_00810354 (Y) */
    uint32_t step_store;  /* the byte store to D_00810813 */
} Beat;

static const Beat kBeats[3] = {
    /* 0x825500: 260.0 (0x43820000) at 0x825548, 280.0 (0x438C0000) at 0x825564. */
    {EM_DIRECTOR_ORIGINAL_BEAT0, 0x43820000u, 0x438C0000u, EM_DIRECTOR_ORIGINAL_QUAD0,
     EM_DIRECTOR_ORIGINAL_SCRIPT0, 0x10, 0x00825538u, 0x008255D0u},
    /* 0x825600: 275.0 (0x43898000) at 0x82564C. */
    {EM_DIRECTOR_ORIGINAL_BEAT1, 0x43898000u, 0, EM_DIRECTOR_ORIGINAL_QUAD1,
     EM_DIRECTOR_ORIGINAL_SCRIPT1, 0x20, 0x00825638u, 0x008256B4u},
    /* 0x8256D0: 285.0 (0x438E8000) at 0x82571C. */
    {EM_DIRECTOR_ORIGINAL_BEAT2, 0x438E8000u, 0, EM_DIRECTOR_ORIGINAL_QUAD2,
     EM_DIRECTOR_ORIGINAL_SCRIPT2, 0xFF, 0x00825708u, 0x00825784u},
};

int em_director_original_001C4760(const EmDirectorOriginalWorld *world, int32_t a0, int32_t a1)
{
    if (!world || !world->d810CC3 || a0 < 0)
        return -1;
    if (a0 >= 0x20 && (!world->d8106B0 || !world->d8106B1))
        return -1;
    world->d810CC3[a0] = (uint8_t)(world->d810CC3[a0] + a1);
    if (a0 >= 0x20) {
        *world->d8106B0 = 3;             /* stored first (volatile order in the decomp C) */
        *world->d8106B1 = (uint8_t)a0;
    }
    return 0;
}

int em_director_original_001C4760_scene(EmSceneState *scene, int32_t a0, int32_t a1)
{
    if (!scene || a0 < 0)
        return -1;
    EmDirectorOriginalWorld world;
    memset(&world, 0, sizeof world);
    world.d810CC3 = em_scene_progress_at(scene, 0x00810CC3u, (uint32_t)a0 + 1u);
    world.d8106B0 = em_scene_req_at(scene, 0x008106B0u);
    world.d8106B1 = em_scene_req_at(scene, 0x008106B1u);
    return em_director_original_001C4760(&world, a0, a1);
}

static uint32_t word(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

/* 0011DBB8, fdlibm atanf, from its instructions (finite x only). The
 * id >= 0 result is D_0026C5D8[id] - ((x * (s1 + s2) - D_0026C5E8[id]) - x)
 * (0x11DE24..0x11DE44). */
int em_director_original_0011DBB8(const EmDirectorAtanTables *t, float x, float *result)
{
    if (!t || !result || !isfinite(x))
        return -1;
    const uint32_t hx = word(x), ix = hx & 0x7FFFFFFFu;
    const float *aT = t->aT;
    if (ix > 0x507FFFFFu) {
        /* 0x11DC08: hx > 0 -> hi[3] + lo[3]; else -hi[3] - lo[3]. */
        *result = (int32_t)hx > 0 ? pose_add(t->hi[3], t->lo[3]) : pose_sub(-t->hi[3], t->lo[3]);
        return 0;
    }
    int id;
    float z;
    if (ix <= 0x3EDFFFFFu) {
        id = -1;
        /* 0x11DC60: 1.0e30 (0x7149F2CA); 1.0 < x + 1e30 returns x. */
        if (ix <= 0x30FFFFFFu && 1.0f < pose_add(x, f32(0x7149F2CAu))) {
            *result = x;
            return 0;
        }
    } else {
        x = fabsf(x);                                      /* 0011DF78 */
        if (ix <= 0x3F97FFFFu) {
            if (ix <= 0x3F2FFFFFu) {
                id = 0;                                    /* (x + x - 1) / (x + 2) */
                x = pose_div(pose_sub(pose_add(x, x), 1.0f), pose_add(x, 2.0f));
            } else {
                id = 1;                                    /* (x - 1) / (x + 1) */
                x = pose_div(pose_sub(x, 1.0f), pose_add(x, 1.0f));
            }
        } else if (ix <= 0x401BFFFFu) {
            id = 2;                                        /* (x - 1.5) / (x * 1.5 + 1) */
            x = pose_div(pose_sub(x, 1.5f), pose_add(pose_mul(x, 1.5f), 1.0f));
        } else {
            id = 3;                                        /* -1 / x */
            x = pose_div(-1.0f, x);
        }
    }
    z = pose_mul(x, x);                                    /* 0x11DD6C */
    const float w = pose_mul(z, z);
    /* 0x11DD80..0x11DDF8: two interleaved Horner chains. */
    float odd = pose_add(aT[8], pose_mul(w, aT[10]));
    float even = pose_add(aT[7], pose_mul(w, aT[9]));
    odd = pose_add(aT[6], pose_mul(w, odd));
    even = pose_add(aT[5], pose_mul(w, even));
    odd = pose_add(aT[4], pose_mul(w, odd));
    even = pose_add(aT[3], pose_mul(w, even));
    odd = pose_add(aT[2], pose_mul(w, odd));
    even = pose_add(aT[1], pose_mul(w, even));
    odd = pose_add(aT[0], pose_mul(w, odd));
    const float s2 = pose_mul(w, even);
    const float s1 = pose_mul(z, odd);
    const float sum = pose_mul(x, pose_add(s1, s2));
    if (id < 0) {
        *result = pose_sub(x, sum);                         /* 0x11DE08 */
        return 0;
    }
    float r = pose_sub(t->hi[id], pose_sub(pose_sub(sum, t->lo[id]), x));
    *result = (int32_t)hx < 0 ? -r : r;                    /* sign negate at 0x11DE48 */
    return 0;
}

/* 0011C4C8, fdlibm atan2f, from its instructions (finite arguments only;
 * its constants are folded: pi 0x40490FDA, pi/2 0x3FC90FDB). */
int em_director_original_0011C4C8(const EmDirectorAtanTables *t, float y, float x, float *result)
{
    if (!t || !result || !isfinite(x) || !isfinite(y))
        return -1;
    const uint32_t hx = word(x), hy = word(y);
    const uint32_t ix = hx & 0x7FFFFFFFu, iy = hy & 0x7FFFFFFFu;
    if (hx == 0x3F800000u)
        return em_director_original_0011DBB8(t, y, result);       /* x == 1.0 */
    const unsigned m = (hy >> 31) | ((hx >> 30) & 2u);
    if (iy == 0) {
        *result = m < 2 ? y : m == 2 ? f32(0x40490FDAu) : f32(0xC0490FDAu);
        return 0;
    }
    if (ix == 0) {
        *result = (int32_t)hy < 0 ? f32(0xBFC90FDBu) : f32(0x3FC90FDBu);
        return 0;
    }
    const int32_t k = ((int32_t)iy - (int32_t)ix) >> 23;
    float z;
    if (k > 60) {
        z = f32(0x3FC90FDCu);                              /* pi/2 + pi_lo/2, 0x11C6C8 */
    } else if ((int32_t)hx < 0 && k < -60) {
        z = 0.0f;
    } else if (em_director_original_0011DBB8(t, fabsf(pose_div(y, x)), &z) < 0) {
        return -1;
    }
    const float pi = f32(0x40490FDAu), pi_lo = f32(0x34222168u);
    switch (m) {
    case 0: *result = z; break;
    case 1: *result = f32(word(z) ^ 0x80000000u); break;
    case 2: *result = pose_sub(pi, pose_sub(z, pi_lo)); break;       /* 0x11C770 */
    default: *result = pose_sub(pose_sub(z, pi_lo), pi); break;      /* 0x11C798 */
    }
    return 0;
}

/* 0011E620(y, x): the kernel 0011C4C8 runs first; with D_0026C5D0 != -1 and
 * neither argument NaN, x == 0 && y == 0 returns 00127758(0.0) = +0. */
int em_director_original_0011E620(const EmDirectorAtanTables *t, float y, float x, float *result)
{
    float saved;
    if (!result || em_director_original_0011C4C8(t, y, x, &saved) < 0)
        return -1;
    *result = (x == 0.0f && y == 0.0f) ? 0.0f : saved;
    return 0;
}

int em_director_original_001B1EA0_bound(int32_t mode, const float *point, const float (*polygon)[4],
                                        int32_t count, EmDirectorAtan2 atan2, void *ctx,
                                        int32_t *result)
{
    if (!result)
        return -1;
    /* 0x1B1EC4..0x1B1ED0: count < 3 -> fewer than three vertices returns 0. */
    if (count < 3) {
        *result = 0;
        return 0;
    }
    if (mode == 1 || mode == 2)
        return -1;                        /* the X/Y and Y/Z sums: not translated */
    if (mode != 0) {
        /* 0x1B1F00: any other mode branches to the tail with total = 0. */
        *result = 0;
        return 0;
    }
    if (!point || !polygon || !atan2 || !isfinite(point[0]) || !isfinite(point[2]))
        return -1;
    float total = 0.0f;
    for (int32_t i = 0; i < count; ++i) {
        /* 0x1B1F18: next = i == count - 1 ? 0 : i + 1. */
        int32_t next = i == count - 1 ? 0 : i + 1;
        const float *cur = polygon[i], *nxt = polygon[next];
        if (!isfinite(cur[0]) || !isfinite(cur[2]) || !isfinite(nxt[0]) || !isfinite(nxt[2]))
            return -1;
        float bx = pose_sub(nxt[0], point[0]);   /* next.x - p.x */
        float ax = pose_sub(cur[0], point[0]);   /* cur.x - p.x */
        float bz = pose_sub(nxt[2], point[2]);   /* next.z - p.z */
        float az = pose_sub(cur[2], point[2]);   /* cur.z - p.z */
        /* cross = ACC(bz * ax) - bx * az, through the FPU accumulator. */
        float cross = pose_msub(pose_mul(bz, ax), bx, az);
        /* dot = ACC(bx * ax) + bz * az (issued in the call's delay slot). */
        float dot = pose_madd(pose_mul(bx, ax), bz, az);
        float angle;
        if (!isfinite(cross) || !isfinite(dot) || atan2(ctx, cross, dot, &angle) < 0)   /* call at 0x1B1F6C */
            return -1;
        total = pose_add(total, angle);           /* total += angle */
        if (!isfinite(total))
            return -1;
    }
    /* 0x1B2098 tail: pi = 0x40490FDB. */
    if (total < 0.0f)
        *result = total < f32(0xC0490FDBu);
    else
        *result = !(total <= f32(0x40490FDBu));
    return 0;
}

static int tables_atan2(void *ctx, float y, float x, float *result)
{
    return em_director_original_0011E620(ctx, y, x, result);
}

int em_director_original_001B1EA0(int32_t mode, const float *point, const float (*polygon)[4],
                                  int32_t count, const EmDirectorAtanTables *tables, int32_t *result)
{
    if (mode == 0 && count >= 3 && !tables)
        return -1;
    return em_director_original_001B1EA0_bound(mode, point, polygon, count, tables_atan2,
                                               (void *)(uintptr_t)tables, result);
}

int em_director_original_load_atan_tables(const uint8_t *elf, size_t size, EmDirectorAtanTables *out)
{
    /* SCUS_971.12: one PROGBITS section, file 0x300 = vram 0x00100000. */
    const size_t offset = 0x0026C5D8u - 0x00100000u + 0x300u;
    if (!elf || !out || size != 1532624u || memcmp(elf, "\x7F" "ELF", 4))
        return -1;
    float v[19];
    for (int i = 0; i < 19; ++i) {
        const uint8_t *b = elf + offset + (size_t)i * 4;
        v[i] = f32((uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24);
        if (!isfinite(v[i]))
            return -1;
    }
    memcpy(out->hi, v, sizeof out->hi);
    memcpy(out->lo, v + 4, sizeof out->lo);
    memcpy(out->aT, v + 8, sizeof out->aT);
    return 0;
}

int em_director_original_load_quads(const uint8_t *overlay, size_t size, float out[3][4][4])
{
    static const uint32_t quads[3] = {EM_DIRECTOR_ORIGINAL_QUAD0, EM_DIRECTOR_ORIGINAL_QUAD1,
                                      EM_DIRECTOR_ORIGINAL_QUAD2};
    if (!overlay || !out || size != EM_DIRECTOR_ORIGINAL_OVERLAY_SIZE || memcmp(overlay, "MWo3", 4))
        return -1;
    uint32_t load = (uint32_t)overlay[8] | (uint32_t)overlay[9] << 8 |
                    (uint32_t)overlay[10] << 16 | (uint32_t)overlay[11] << 24;
    if (load != EM_DIRECTOR_ORIGINAL_OVERLAY_BASE)
        return -1;
    float copy[3][4][4];
    for (int q = 0; q < 3; ++q)
        for (int v = 0; v < 4; ++v)
            for (int c = 0; c < 4; ++c) {
                const uint8_t *b = overlay + (quads[q] - load) + (uint32_t)(v * 16 + c * 4);
                uint32_t bits = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 |
                                (uint32_t)b[3] << 24;
                copy[q][v][c] = f32(bits);
                if ((c == 0 || c == 2) && !isfinite(copy[q][v][c]))
                    return -1;
            }
    memcpy(out, copy, sizeof copy);
    return 0;
}

/* One beat body (0x825500 / 0x825600 / 0x8256D0). */
static int beat(int index, const EmDirectorOriginalNode *node, const EmDirectorOriginalWorld *world,
                const EmDirectorOriginalWorkers *w, uint32_t *fault_address)
{
    const Beat *b = &kBeats[index];
    if (!node->b05)
        return fail(fault_address, b->body);
    uint8_t sub = *node->b05;
    if (sub == 1) {
        int32_t done = 0;
        if (!w->w_001BA1F0 || w->w_001BA1F0(w->ctx, node->self, &done) < 0)
            return fail(fault_address, 0x001BA1F0u);
        if (done == 0)
            return 1;
        if (!world->d810813)
            return fail(fault_address, b->step_store);
        *world->d810813 = b->completion;
        if (index == 0 && em_director_original_001C4760(world, 1, 1) < 0)
            return fail(fault_address, 0x001C4760u);
        *node->b05 = 0;
        return 1;
    }
    if (sub != 0)
        return 1;
    if (!world->d810350)
        return fail(fault_address, b->y_read);
    float y = world->d810350[1];
    if (y < f32(b->y_low))
        return 1;
    if (b->y_high && !(y <= f32(b->y_high)))
        return 1;
    const float (*quad)[4] = world->quad[index];
    if (!quad)
        return fail(fault_address, b->quad);
    int32_t inside = 0;
    /* 001B1EA0(0, &D_00810350, quad, 4). */
    if (em_director_original_001B1EA0(0, world->d810350, quad, 4, world->d26C5D8, &inside) < 0)
        return fail(fault_address, 0x001B1EA0u);
    if (!inside)
        return 1;
    if (!w->w_001BA1A0 ||
        w->w_001BA1A0(w->ctx, node->self + EM_DIRECTOR_ORIGINAL_SCRIPT_BLOCK, b->script) < 0)
        return fail(fault_address, 0x001BA1A0u);
    *node->b05 = 1;
    return 1;
}

int em_director_original_tick(const EmDirectorOriginalNode *node, const EmDirectorOriginalWorld *world,
                              const EmDirectorOriginalWorkers *workers, uint32_t *fault_address)
{
    if (!node || !node->b00 || !node->b04 || !world || !workers)
        return fail(fault_address, EM_DIRECTOR_ORIGINAL_OWNER);
    switch (*node->b04) {
    case 2:
    case 3:
        /* 0x8254E4 */
        if (!workers->w_001AFC10 || workers->w_001AFC10(workers->ctx, node->self) < 0)
            return fail(fault_address, 0x001AFC10u);
        return 0;
    case 1: {
        if (!world->d810813)
            return fail(fault_address, 0x00825468u);
        uint8_t step = *world->d810813;       /* read once, 0x825468 */
        if (step == 0x20)
            return beat(2, node, world, workers, fault_address);
        if (step == 0x10 || step == 0x11)
            return beat(1, node, world, workers, fault_address);
        if (step == 0 || step == 1)
            return beat(0, node, world, workers, fault_address);
        return 1;
    }
    case 0:
        /* 001BA1C0(self, 0x3B) at 0x825438: D_00810758[0x3B] == 0xFF. */
        if (!world->d810793)
            return fail(fault_address, 0x001BA1C0u);
        if (*world->d810793 == 0xFF) {
            *node->b04 = 3;
        } else {
            *node->b00 = 1;
            *node->b04 = 1;
        }
        return 1;
    default:
        return 1;
    }
}
