/* em_script_host_workers.c - the script host's callees and the AREA11
 * script loader (see em_script_host_workers.h, docs/SCRIPT_HOST_WORKERS.md).
 *
 * Read from the original instructions (the decomp's build/asm listing):
 * 00182BF0 is NEARMISS C, 001B12B0 and 001B1380 are asm-word files, and
 * 001B1240 / 001B6250 / 001B0C00 were checked against their listings. Every
 * address in a comment is the original instruction translated there. Float
 * operations and compares go through em_ee_float.h on bit patterns. */
#include "game/em_script_host_workers.h"
#include "game/em_ee_float.h"
#include "game/em_player_stage_workers.h"

#include <string.h>

#define F_ZERO UINT32_C(0x00000000)

static int fail(EmScriptHostWorkers *h, uint32_t address)
{
    if (h && h->fault_address == 0) h->fault_address = address;
    return -1;
}

#define NEED(ptr, address) do { if (!(ptr)) return fail(h, (address)); } while (0)
#define CALL(address, expr) do { if ((expr) < 0) return fail(h, (address)); } while (0)

/* ---- 001B1470 (reused) over the translated domain ------------------------ */

/* em_player_001B1470 is the 001B1470 translation; the bound keeps its loop
 * finite (em_script_host_workers.h, "001B1470 domain"). */
static int wrap(EmScriptHostWorkers *h, uint32_t x, uint32_t *out)
{
    const uint32_t magnitude = x & UINT32_C(0x7FFFFFFF);
    if (magnitude >= EM_SCRIPT_HOST_WRAP_LIMIT) return fail(h, 0x001B1470u);
    *out = em_player_001B1470(x);
    return 0;
}

/* ---- 0011E620 (reused) --------------------------------------------------- */

static int atan2_0011E620(EmScriptHostWorkers *h, uint32_t y, uint32_t x, uint32_t *out)
{
    const EmScriptHostWorkersWorld *w = &h->world;
    float result = 0.0f;
    uint32_t fault = 0;
    if (!w->sdk_tables) return fail(h, 0x0011E620u);
    if (em_sdk_math_original_0011E620(w->sdk_tables, w->sdk_world, w->sdk_workers,
                                      em_ee_float(y), em_ee_float(x), &result, &fault) < 0)
        return fail(h, fault ? fault : 0x0011E620u);
    *out = em_ee_bits(result);
    return 0;
}

/* ---- 00182BF0: op16 frame predicate --------------------------------------- */

int em_script_host_00182BF0(EmScriptHostWorkers *h, uint32_t actor, int32_t *result)
{
    if (!h) return -1;
    const EmScriptHostWorkersWorld *w = &h->world;
    NEED(result, 0x00182BF0u);
    NEED(w->player, 0x00182BF0u);
    if (actor != w->player_address) return fail(h, 0x00182BF0u);
    NEED(w->d8106BC, 0x00182C00u);
    NEED(w->d81083C, 0x00182C10u);
    NEED(w->d8106F1, 0x00182C90u);
    EmPlayerLiveActor *a = w->player;

    if (*w->d8106BC != 0) {                                        /* 00182C04 */
        if (*w->d81083C != 0) { *result = 1; return 0; }           /* 00182C14 */
        *w->d8106BC = 0;                                           /* 00182C24 */
    }
    if (em_ee_c_le_bits(em_live_u32(a, 0x220), F_ZERO)) {          /* 00182C40 / 00182C48 */
        *result = 1; return 0;
    }
    if (em_live_u8(a, 0xF) == 0x63) { *result = 1; return 0; }     /* 00182C58 */
    if (*w->d81083C != 0) {                                        /* 00182C74 */
        *w->d8106BC = 1;                                           /* 00182C88 */
        *result = 1; return 0;
    }
    if (*w->d8106F1 != 0) { *result = 1; return 0; }               /* 00182C94 */
    if (em_player_0021BB00(a) != 0) { *result = 1; return 0; }     /* 00182C9C / 00182CA4 */
    const unsigned mode = em_live_u8(a, 0x1F0);                    /* 00182CAC */
    if (mode == 0x3C || mode == 0x3D || mode - 0xBu < 3u) {        /* 00182CB4 / 00182CC0 / 00182CD0 */
        *result = 1; return 0;
    }
    if (!em_ee_c_eq_bits(em_live_u32(a, 0x22C), F_ZERO) ||        /* 00182CF0 / 00182CF8 */
        !em_ee_c_eq_bits(em_live_u32(a, 0x224), F_ZERO)) {         /* 00182D04 / 00182D0C */
        em_live_set_u32(a, 0x22C, 0);                              /* 00182D14 */
        em_live_set_u32(a, 0x224, 0);                              /* 00182D1C */
        em_live_set_u8(a, 0x0, 1);                                 /* 00182D20 */
    }
    *result = 0;                                                   /* 00182D10 / 00182D24 */
    return 0;
}

/* ---- 001B1240: bearing ---------------------------------------------------- */

int em_script_host_001B1240(EmScriptHostWorkers *h, const uint32_t object[3], uint32_t x,
                            uint32_t z, uint32_t *result)
{
    if (!h) return -1;
    NEED(object, 0x001B1240u);
    NEED(result, 0x001B1240u);
    const uint32_t dx = em_ee_sub_bits(x, object[0]);              /* 001B1250 */
    const uint32_t dz = em_ee_sub_bits(z, object[2]);              /* 001B1258 */
    uint32_t angle;
    if (atan2_0011E620(h, dx, dz, &angle) < 0) return -1;         /* 001B1254 */
    return wrap(h, angle, result);                                 /* 001B125C */
}

/* ---- 001B1380: side test --------------------------------------------------- */

int em_script_host_001B1380(EmScriptHostWorkers *h, const uint32_t from[3], const uint32_t to[3],
                            uint32_t yaw, int32_t *result)
{
    if (!h) return -1;
    NEED(from, 0x001B1380u);
    NEED(to, 0x001B1380u);
    NEED(result, 0x001B1380u);
    const uint32_t dz = em_ee_sub_bits(from[2], to[2]);            /* 001B13A0 */
    const uint32_t dx = em_ee_sub_bits(from[0], to[0]);            /* 001B13A8 */
    uint32_t angle, relative;
    if (atan2_0011E620(h, dx, dz, &angle) < 0) return -1;         /* 001B13A4 */
    if (wrap(h, em_ee_sub_bits(angle, yaw), &relative) < 0)        /* 001B13AC / 001B13B0 */
        return -1;
    *result = em_ee_c_lt_bits(relative, F_ZERO) ? 0 : 1;           /* 001B13BC / 001B13C4 */
    return 0;
}

/* ---- 001B12B0: turn toward ------------------------------------------------- */

int em_script_host_001B12B0(EmScriptHostWorkers *h, uint32_t target, uint32_t current,
                            uint32_t step, uint32_t *result)
{
    if (!result) return fail(h, 0x001B12B0u);
    uint32_t difference;
    if (wrap(h, em_ee_sub_bits(target, current), &difference) < 0)  /* 001B12C0 / 001B12D0 */
        return -1;
    if (em_ee_c_eq_bits(F_ZERO, difference))                        /* 001B12E0 / 001B12E8 */
        return wrap(h, current, result);                            /* 001B12F0 */
    if (em_ee_c_le_bits(difference, F_ZERO)) {                      /* 001B1300 / 001B1308 */
        const uint32_t away = em_ee_neg_bits(difference);           /* 001B130C (likely slot) */
        if (em_ee_c_le_bits(away, step)) {                          /* 001B1338 / 001B1340 */
            *result = target;                                       /* 001B1344 */
            return 0;
        }
        return wrap(h, em_ee_sub_bits(current, step), result);      /* 001B134C / 001B135C */
    }
    if (em_ee_c_le_bits(difference, step)) {                        /* 001B1310 / 001B1318 */
        *result = target;                                           /* 001B131C */
        return 0;
    }
    return wrap(h, em_ee_add_bits(current, step), result);          /* 001B1324 / 001B135C */
}

/* ---- 001B6250: pad actuator stop ------------------------------------------- */

static uint32_t block_word(const uint8_t *block, unsigned at)
{
    uint32_t value;
    memcpy(&value, block + at, 4);
    return value;
}

int em_script_host_001B6250(EmScriptHostWorkers *h, uint32_t address)
{
    if (!h) return -1;
    const EmScriptHostWorkersWorld *w = &h->world;
    NEED(w->d810E40, 0x001B6250u);
    if (address != w->pad_address) return fail(h, 0x001B6250u);
    NEED(h->callees.w_00111018, 0x00111018u);
    uint8_t *block = w->d810E40;
    if (block[0x12] == 0) return 0;                                /* 001B6258 / 001B625C */
    if (block[0x16] == 0) return 0;                                /* 001B6264 / 001B6268 */
    block[0x16] = 0;                                               /* 001B6270 */
    block[0x28] = 0; block[0x29] = 0;                              /* 001B6274 (halfword) */
    block[0x18] = 0;                                               /* 001B6278 */
    block[0x19] = 0;                                               /* 001B627C */
    const int32_t port = (int32_t)block_word(block, 0x4);          /* 001B6280 */
    const int32_t slot = (int32_t)block_word(block, 0x8);          /* 001B6284 */
    CALL(0x00111018u, h->callees.w_00111018(h->callees.ctx, port, slot, block + 0x18)); /* 001B6288 */
    return 0;
}

/* ---- 001B0C00: fade-out and stream fades ---------------------------------- */

int em_script_host_001B0C00(EmScriptHostWorkers *h, int32_t a0)
{
    if (!h) return -1;
    const EmScriptHostWorkersCallees *k = &h->callees;
    NEED(k->w_001AEDE0, 0x001AEDE0u);
    NEED(k->w_001FAD70, 0x001FAD70u);
    CALL(0x001AEDE0u, k->w_001AEDE0(k->ctx, a0, 0));               /* 001B0C10 */
    CALL(0x001FAD70u, k->w_001FAD70(k->ctx, 0, a0, 1));            /* 001B0C20 */
    CALL(0x001FAD70u, k->w_001FAD70(k->ctx, 1, a0, 1));            /* 001B0C30 */
    CALL(0x001FAD70u, k->w_001FAD70(k->ctx, 2, a0, 1));            /* 001B0C40 */
    return 0;
}

/* ---- 001B0460: camera re-seat ------------------------------------------------ */

#define F_ONE UINT32_C(0x3F800000)

/* A word of the boot ELF's loaded image at vram `address`. */
static int elf_word(const EmScriptHostWorkersWorld *w, uint32_t address, uint32_t *out)
{
    if (address < 0x00100000u || address > 0x00100000u + 0x175B00u - 4u || (address & 3u))
        return -1;
    if (!w->elf) return w->read_word ? w->read_word(w->read_ctx, address, out) : -1;
    const size_t at = (size_t)(address - 0x00100000u) + 0x300u;
    if (at + 4u > w->elf_size) return -1;
    memcpy(out, w->elf + at, 4);
    return 0;
}

static void qcopy(float *dst, const float *src) { memmove(dst, src, 16); }   /* 00102948 */
static uint32_t fword(const float *p, int i) { uint32_t v; memcpy(&v, p + i, 4); return v; }
static void set_fword(float *p, int i, uint32_t v) { memcpy(p + i, &v, 4); }

int em_script_host_001B0460(EmScriptHostWorkers *h, int32_t a0)
{
    if (!h) return -1;
    const EmScriptHostWorkersWorld *w = &h->world;
    const EmScriptHostWorkersCallees *k = &h->callees;
    if (!w->elf && !w->read_word) return fail(h, 0x0024D650u);
    NEED(w->d810700, 0x00810700u); NEED(w->d810701, 0x00810701u); NEED(w->d810702, 0x00810702u);
    NEED(w->d8101E1, 0x008101E1u); NEED(w->d8101E2, 0x008101E2u); NEED(w->d8101E3, 0x008101E3u);
    NEED(w->d8101E5, 0x008101E5u); NEED(w->d8101E6, 0x008101E6u); NEED(w->d8101E7, 0x008101E7u);
    NEED(w->d8101E8, 0x008101E8u); NEED(w->d8101EC, 0x008101ECu);
    NEED(w->cam_10, 0x008101F0u); NEED(w->cam_20, 0x00810200u);
    NEED(w->d810244, 0x00810244u); NEED(w->d8106C8, 0x008106C8u); NEED(w->d8106CD, 0x008106CDu);
    NEED(w->d275BE0, 0x00275BE0u); NEED(w->d8106BE, 0x008106BEu); NEED(w->d8104E0, 0x008104E0u);
    NEED(w->d810350, 0x00810350u); NEED(w->d810370, 0x00810370u);
    NEED(w->d8105D0, 0x008105D0u); NEED(w->d8105E0, 0x008105E0u);
    NEED(w->spad3400, 0x70003400u); NEED(w->spad3600, 0x70003600u);
    NEED(k->w_001B0250, 0x001B0250u); NEED(k->w_001B0B50, 0x001B0B50u);
    NEED(k->w_001B0080, 0x001B0080u); NEED(k->w_0018C0D0, 0x0018C0D0u);
    NEED(k->w_001DD980, 0x001DD980u); NEED(k->w_001029C0, 0x001029C0u);
    NEED(k->w_00102C58, 0x00102C58u); NEED(k->w_001026A0, 0x001026A0u);
    NEED(k->w_001028B8, 0x001028B8u);

    /* The room's camera record: D_0024D650[area][room] + index * 0x30. */
    uint32_t rooms, table;
    if (elf_word(w, 0x0024D650u + 4u * *w->d810700, &rooms) < 0)      /* 001B04A0 */
        return fail(h, 0x001B04A0u);
    if (elf_word(w, rooms + 4u * *w->d810701, &table) < 0)             /* 001B04BC */
        return fail(h, 0x001B04BCu);
    const uint32_t record = table + (uint32_t)*w->d810702 * 0x30u;    /* 001B04C8 */
    uint32_t distance, flags;
    if (elf_word(w, record + 0x18u, &distance) < 0) return fail(h, 0x001B04F8u);
    if (elf_word(w, record + 0x10u, &flags) < 0) return fail(h, 0x001B0524u);

    CALL(0x001B0250u, k->w_001B0250(k->ctx));                         /* 001B04C4 */
    CALL(0x001B0B50u, k->w_001B0B50(k->ctx));                         /* 001B04CC */
    *w->d8101E8 = 0;                                                  /* 001B04D8 */
    *w->d8101E7 = 0;                                                  /* 001B04E0 */
    *w->d8106CD = (uint8_t)(uint32_t)(*w->d8106C8 >> 16);             /* 001B04E8..001B04F4 */
    memcpy(w->d810244, &distance, 4);                                 /* 001B0500 */
    memcpy(w->d8101EC, &distance, 4);                                 /* 001B0508 */
    *w->d8101E1 = 0;                                                  /* 001B0510 */
    *w->d8101E2 = 0;                                                  /* 001B0518 */
    *w->d8101E3 = 0;                                                  /* 001B0520 */
    const uint8_t low = (uint8_t)flags;                               /* 001B0524 (lbu) */
    *w->d8101E5 = (low & 0x80u) ? 1 : 0;                              /* 001B052C..001B0540 */
    *w->d8101E6 = (uint8_t)(low & 0x7Fu);                             /* 001B0548 */
    if (*w->d275BE0 == 1) {                                           /* 001B0554 */
        *w->d8101E6 = 0;                                              /* 001B055C */
        *w->d275BE0 = 0;                                              /* 001B0560 */
    }
    if (*w->d8101E5 == 1) {                                           /* 001B0564 / 001B056C */
        const int32_t pose = *w->d8104E0;                             /* 001B0580 */
        if (a0 != 0 && (pose == 0x10 || pose == 0x12)) {              /* 001B0574 / 001B0588 / 001B0594 */
            *w->d8101E6 = pose == 0x10 ? 9 : 0xB;                     /* 001B05A0..001B05B8 */
            *w->d8101E1 = 0;                                          /* 001B05C0 */
        } else {
            /* 001B05C4: D_0024A8D0 + (flags >> 8) * 12 (arithmetic shift). */
            const uint32_t spot = 0x0024A8D0u + (uint32_t)(((int32_t)flags >> 8) * 12);
            uint32_t xyz[3];
            for (int i = 0; i < 3; ++i)
                if (elf_word(w, spot + 4u * (uint32_t)i, &xyz[i]) < 0)
                    return fail(h, 0x001B05ECu);
            set_fword(w->cam_10, 0, xyz[0]);                          /* 001B05F8 */
            set_fword(w->cam_10, 1, xyz[1]);                          /* 001B0600 */
            set_fword(w->cam_10, 2, xyz[2]);                          /* 001B0608 */
            set_fword(w->cam_10, 3, F_ONE);                           /* 001B0610 */
            qcopy(w->cam_20, w->d810350);                             /* 001B060C: 00102948 */
            set_fword(w->cam_20, 1, em_ee_add_bits(fword(w->cam_20, 1),
                                                   UINT32_C(0x41700000)));   /* 001B0628 / 001B0634: +15 */
            qcopy(w->d8105E0, w->cam_20);                             /* 001B0630 */
            qcopy(w->d8105D0, w->cam_10);                             /* 001B0640 */
        }
    } else if (*w->d8101E6 == 0xA) {                                  /* 001B0650 / 001B0658 */
        *w->d8106BE = 2;                                              /* 001B0668 */
        qcopy(w->cam_10, w->d810350);                                 /* 001B0674 */
        set_fword(w->cam_10, 1, em_ee_add_bits(fword(w->cam_10, 1),
                                               UINT32_C(0x40400000)));       /* 001B0690 / 001B0698: +3 */
        CALL(0x001029C0u, k->w_001029C0(k->ctx, w->spad3400));        /* 001B0694 */
        CALL(0x00102C58u, k->w_00102C58(k->ctx, w->spad3400, w->spad3400,
                                        w->d810370));                 /* 001B06B0 */
        set_fword(w->spad3600, 0, 0);                                 /* 001B06BC */
        set_fword(w->spad3600, 1, 0);                                 /* 001B06C4 */
        set_fword(w->spad3600, 2, UINT32_C(0x40A00000));              /* 001B06D0: 5.0 */
        set_fword(w->spad3600, 3, 0);                                 /* 001B06F0 */
        CALL(0x001026A0u, k->w_001026A0(k->ctx, w->cam_20, w->spad3400,
                                        w->spad3600));                /* 001B06EC */
        CALL(0x001028B8u, k->w_001028B8(k->ctx, w->cam_20, w->cam_20, w->cam_10)); /* 001B06FC */
        qcopy(w->d8105E0, w->cam_20);                                 /* 001B070C */
        qcopy(w->d8105D0, w->cam_10);                                 /* 001B071C */
    } else {
        CALL(0x001B0080u, k->w_001B0080(k->ctx, 0x008101E0u, 2.0f));  /* 001B0734 */
        uint32_t kind;
        if (elf_word(w, record + 0x14u, &kind) < 0) return fail(h, 0x001B073Cu);
        if (kind == 5) {                                              /* 001B0744 */
            *w->d8101E6 = 0xD;                                        /* 001B0754 */
        } else if (kind == 4 && *w->d810700 == 0x13) {                /* 001B075C / 001B0770 */
            *w->d8101E6 = 0xF;                                        /* 001B077C */
        }
    }
    CALL(0x0018C0D0u, k->w_0018C0D0(k->ctx, 0x008101E0u, 1));        /* 001B0784 */
    CALL(0x001DD980u, k->w_001DD980(k->ctx, w->d8105D0, w->d8105E0)); /* 001B0798 */
    return 0;
}

/* ---- Worker adapters -------------------------------------------------------- */

int em_script_host_w_00182BF0(void *ctx, uint32_t actor, int32_t *result)
{
    return em_script_host_00182BF0(ctx, actor, result);
}

int em_script_host_w_001B1240(void *ctx, const float object[4], float x, float z, float *result)
{
    EmScriptHostWorkers *h = ctx;
    uint32_t o[3], r;
    if (!h) return -1;
    if (!object || !result) return fail(h, 0x001B1240u);
    for (int i = 0; i < 3; ++i) o[i] = em_ee_bits(object[i]);
    if (em_script_host_001B1240(h, o, em_ee_bits(x), em_ee_bits(z), &r) < 0) return -1;
    *result = em_ee_float(r);
    return 0;
}

int em_script_host_w_001B12B0(void *ctx, float target, float current, float step, float *result)
{
    uint32_t r;
    if (!result) return fail(ctx, 0x001B12B0u);
    if (em_script_host_001B12B0(ctx, em_ee_bits(target), em_ee_bits(current), em_ee_bits(step),
                                &r) < 0)
        return -1;
    *result = em_ee_float(r);
    return 0;
}

int em_script_host_w_001B1380(void *ctx, const float from[4], const float to[4], float yaw,
                              int32_t *result)
{
    EmScriptHostWorkers *h = ctx;
    uint32_t f[3], t[3];
    if (!h) return -1;
    if (!from || !to) return fail(h, 0x001B1380u);
    for (int i = 0; i < 3; ++i) {
        f[i] = em_ee_bits(from[i]);
        t[i] = em_ee_bits(to[i]);
    }
    return em_script_host_001B1380(h, f, t, em_ee_bits(yaw), result);
}

int em_script_host_w_001B0C00(void *ctx, int a0)
{
    return em_script_host_001B0C00(ctx, (int32_t)a0);
}

int em_script_host_w_001B6250(void *ctx, uint32_t address)
{
    return em_script_host_001B6250(ctx, address);
}

int em_script_host_w_001B0460(void *ctx, int a0)
{
    return em_script_host_001B0460(ctx, (int32_t)a0);
}

int em_script_host_owner_001B6250(void *ctx)
{
    return em_script_host_001B6250(ctx, EM_SCRIPT_HOST_D_00810E40);
}

int em_script_host_approach(void *ctx, uint32_t target, uint32_t current, uint32_t step,
                            uint32_t *out)
{
    (void)ctx;
    return em_script_host_001B12B0(NULL, target, current, step, out);
}

/* ---- AREA11 overlay scripts --------------------------------------------------- */

static const uint32_t kEntries[] = {
    0x008292C0u,   /* truck camera preview (trigger 008251E0, 0x825388) */
    0x008294C0u,   /* director beat 0 (008253F0, 0x8255A0) */
    0x00829A40u,   /* director beat 1 (0x825688) */
    0x00829CC0u,   /* director beat 2 (0x825758) */
    0x00829E80u,   /* director beat 3 (0x825880) */
    0x0082A750u,   /* elevator powered (00827B10, 0x827CF0) */
    0x0082A990u,   /* elevator refusal (0x827D10) */
    0x008283D0u,   /* Roger encounter (00823910, 0x823A7C) */
    0x00828990u,   /* Roger alternate (0x823984) */
    0x00828810u,   /* Roger armed talk (00823B70, 0x823BB4) */
    0x00828A10u,   /* Roger departure (00823C40, 0x823C74) */
};

static const struct { uint32_t base, end; } kRanges[EM_AREA11_IMAGE_COUNT] = {
    {EM_AREA11_SCRIPTS_BASE, EM_AREA11_SCRIPTS_END},
    {EM_AREA11_ELEVATOR_BASE, EM_AREA11_ELEVATOR_END},
    {EM_AREA11_ROGER_BASE, EM_AREA11_ROGER_END},
};

const uint32_t *em_area11_scripts_entries(size_t *count)
{
    if (count) *count = sizeof kEntries / sizeof kEntries[0];
    return kEntries;
}

static int image_holds(const EmScriptImage *image, uint32_t address)
{
    return image->bytes && address >= image->base && address - image->base < image->length;
}

EmScriptImage *em_area11_scripts_image(EmArea11Scripts *s, uint32_t entry)
{
    if (!s) return NULL;
    for (int i = 0; i < EM_AREA11_IMAGE_COUNT; ++i)
        if (image_holds(&s->image[i], entry)) return &s->image[i];
    return NULL;
}

/* A chain from `entry` reaches a stop record (flag 0x80000000) inside the
 * image, following jump records (flag 0x40000000, target at +4), within 64
 * records. The flags are the ones em_script_tick reads (001BA1F0). */
static int chain_ends(EmScriptImage *image, uint32_t entry)
{
    uint32_t pc = entry;
    for (int n = 0; n < 64; ++n) {
        if ((pc - image->base) % EM_SCRIPT_RECORD_SIZE != 0) return 0;
        const unsigned char *record = em_script_image_read(image, pc, EM_SCRIPT_RECORD_SIZE);
        if (!record) return 0;
        const uint32_t flags = em_script_u32(record, 0);
        if (flags & UINT32_C(0x80000000)) return 1;
        pc = (flags & UINT32_C(0x40000000)) ? em_script_u32(record, 4) : pc + EM_SCRIPT_RECORD_SIZE;
    }
    return 0;
}

static int load_exact(EmScriptImage *image, const char *path, uint32_t base, uint32_t end)
{
    if (!em_script_image_load(image, path)) return -1;
    if (image->base != base || image->entry != base || image->length != end - base) {
        em_script_image_free(image);
        return -1;
    }
    return 0;
}

void em_area11_scripts_free(EmArea11Scripts *s)
{
    if (!s) return;
    for (int i = 0; i < EM_AREA11_IMAGE_COUNT; ++i) em_script_image_free(&s->image[i]);
    memset(s, 0, sizeof *s);
}

int em_area11_scripts_load(EmArea11Scripts *out, const char *scripts_path,
                           const char *quads_path, const char *elevator_path,
                           const char *roger_path)
{
    if (!out || !scripts_path || !quads_path) return -1;
    EmArea11Scripts s;
    memset(&s, 0, sizeof s);
    const char *paths[EM_AREA11_IMAGE_COUNT] = {scripts_path, elevator_path, roger_path};
    for (int i = 0; i < EM_AREA11_IMAGE_COUNT; ++i) {
        if (!paths[i]) continue;
        if (load_exact(&s.image[i], paths[i], kRanges[i].base, kRanges[i].end) < 0) {
            em_area11_scripts_free(&s);
            return -1;
        }
    }
    EmScriptImage quads;
    memset(&quads, 0, sizeof quads);
    if (load_exact(&quads, quads_path, EM_AREA11_QUADS_BASE, EM_AREA11_QUADS_END) < 0) {
        em_area11_scripts_free(&s);
        return -1;
    }
    for (int q = 0; q < 3; ++q)
        for (int v = 0; v < 4; ++v)
            for (int c = 0; c < 4; ++c)
                s.quad[q][v][c] = em_script_f32(quads.bytes, (unsigned)(q * 0x40 + v * 0x10 + c * 4));
    em_script_image_free(&quads);
    s.quads_loaded = 1;
    for (size_t i = 0; i < sizeof kEntries / sizeof kEntries[0]; ++i) {
        const uint32_t entry = kEntries[i];
        int range = -1;
        for (int r = 0; r < EM_AREA11_IMAGE_COUNT; ++r)
            if (entry >= kRanges[r].base && entry < kRanges[r].end) range = r;
        if (range < 0 || !s.image[range].bytes) continue;   /* its image was not asked for */
        if (!chain_ends(&s.image[range], entry)) {
            em_area11_scripts_free(&s);
            return -1;
        }
    }
    *out = s;
    return 0;
}

int em_area11_scripts_director_quads(const EmArea11Scripts *s, const float (*quad[3])[4])
{
    if (!s || !quad || !s->quads_loaded) return -1;
    for (int q = 0; q < 3; ++q) quad[q] = (const float (*)[4])s->quad[q];
    return 0;
}
