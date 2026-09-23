/* Status scene workers (see em_status_scene_original.h and
 * docs/STATUS_SCENE.md). Addresses in comments are original runtime
 * addresses; float constants are the original bit patterns. */
#include "game/em_status_scene_original.h"

#include <string.h>

#include "game/em_ee_float.h" /* EE COP1 arithmetic (docs/EE_FLOAT_MODEL.md) */

/* Original constants (bit patterns). */
#define K(b) em_ee_float(b)
#define F_PI 0x40490FDBu        /* pi */
#define F_TWO_PI 0x40C90FDBu    /* 2 pi */
#define F_16 0x41800000u        /* 16 */
#define F_1 0x3F800000u         /* 1 */
#define F_0_01 0x3C23D70Au      /* 0.01 */
#define F_1_3 0x3FA66666u       /* 1.3 */
#define F_35 0x420C0000u        /* 35 */
#define F_M80 0xC2A00000u       /* -80 */
#define F_M100 0xC2C80000u      /* -100 */
#define F_M30 0xC1F00000u       /* -30 */
#define F_M127 0xC2FE0000u      /* -127 */
#define F_40 0x42200000u        /* 40 */
#define F_7_4 0x40ECCCCDu       /* 7.4 */
#define F_2_4 0x4019999Au       /* 2.4 */

/* ------------------------------------------------------------------------
 * Fail-stop plumbing. */
static int fail(EmStatusSceneFault *fault, uint32_t address, int32_t code)
{
    fault->address = address;
    fault->code = code;
    return -1;
}

#define NEED(fn, addr)                                                                          \
    do {                                                                                        \
        if (!w->fn)                                                                             \
            return fail(fault, (addr), EM_STATUS_SCENE_FAULT_NULL_WORKER);                      \
    } while (0)
#define CALL(addr, expr)                                                                        \
    do {                                                                                        \
        if ((expr) < 0)                                                                         \
            return fail(fault, (addr), EM_STATUS_SCENE_FAULT_WORKER_FAILED);                    \
    } while (0)

static int latched(const EmStatusSceneWorkers *w, EmStatusSceneFault *fault)
{
    return !w || !fault || fault->code != EM_STATUS_SCENE_FAULT_NONE;
}

/* ------------------------------------------------------------------------
 * 0020E3A0 (byte-matched C): the 24-entry table over code + 2. */
int32_t em_status_scene_glyph_0020E3A0(int32_t code)
{
    switch (code) {
    case -2: return 0x30;
    case 0: return 0x32;
    case 1: return 0x33;
    case 2: return 0x34;
    case 3: return 0x35;
    case 4: return 0x36;
    case 5: return 0x31;
    case 6: return 0x37;
    case 7: return 0x38;
    case 8: return 0x39;
    case 9: return 0x3A;
    case 10: return 0x3B;
    case 11: return 0x3C;
    case 12: return 0x3D;
    case 16: return 0x40;
    case 21: return 0x6D;
    default: return 0x2F; /* also -1, 13, 14, 15, 17..20 */
    }
}

/* 0020E1E0(code). */
static int letter(int32_t code, uint32_t bank, const EmStatusSceneWorkers *w,
                  EmStatusSceneFault *fault)
{
    EmStatusSceneActor *actor = NULL;
    uint32_t model = 0, bones = 0;
    NEED(w_001AFF10, 0x001AFF10u);
    CALL(0x001AFF10u, w->w_001AFF10(w->ctx, &actor));
    if (!actor)
        return 0;
    actor->w10 = EM_STATUS_SCENE_CB_0020E460;
    NEED(w_001C6120, 0x001C6120u);
    CALL(0x001C6120u, w->w_001C6120(w->ctx, bank, code, &model));
    NEED(w_001CA6E0, 0x001CA6E0u);
    CALL(0x001CA6E0u, w->w_001CA6E0(w->ctx, actor, model));
    NEED(w_001C6150, 0x001C6150u);
    CALL(0x001C6150u, w->w_001C6150(w->ctx, actor->w44, &bones));
    actor->b0C = (uint8_t)bones; /* the low byte of the 001C6150 result */
    actor->b0D = (uint8_t)code;  /* the low byte of the code */
    return 1;
}

int em_status_scene_letter_0020E1E0(int32_t code, uint32_t bank_28A56C,
                                    const EmStatusSceneWorkers *w, EmStatusSceneFault *fault)
{
    if (latched(w, fault))
        return -1;
    return letter(code, bank_28A56C, w, fault);
}

/* 0020E250 (byte-matched C). */
int em_status_scene_letters_0020E250(const uint8_t ca[4], uint32_t bank_28A56C,
                                     const EmStatusSceneWorkers *w, EmStatusSceneFault *fault)
{
    int32_t codes[8];
    int n = 0;
    if (latched(w, fault))
        return -1;
    if (!ca)
        return fail(fault, 0x00810CA4u, EM_STATUS_SCENE_FAULT_NULL_WORKER);
    codes[n++] = -1;
    codes[n++] = 0x10;
    if (ca[0] == 2) {
        codes[n++] = 0xC;
    } else if (ca[0] == 0) {
        codes[n++] = 0xA;
        codes[n++] = ca[2];
    } else {
        codes[n++] = -2;
        if (ca[0] == 1) {
            codes[n++] = 0xB;
            codes[n++] = ca[2];
        } else {
            codes[n++] = ca[1];
            codes[n++] = ca[2];
            codes[n++] = ca[3];
        }
        if (ca[2] == 4)
            codes[n++] = 0x15;
    }
    for (int i = 0; i < n; ++i)
        if (letter(em_status_scene_glyph_0020E3A0(codes[i]), bank_28A56C, w, fault) < 0)
            return -1;
    return 0;
}

/* ------------------------------------------------------------------------
 * 0020E6F0: the menu player (Dennis) on the status screen. */
static int player_init(EmStatusSceneActor *a, const EmStatusScenePlayerGlobals *g,
                       const EmStatusSceneWorkers *w, EmStatusSceneFault *fault)
{
    uint32_t model, value;
    if (g->d8104E4 == 0)
        model = g->d810C60 == 1 ? g->d28A588 : g->d810C60 == 2 ? g->d28A58C : g->d28A57C;
    else if (g->d8104E4 == 1)
        model = g->d810C60 == 1 ? g->d28A588 : g->d810C60 == 2 ? g->d28A58C : g->d28A590;
    else
        model = g->d28A584;
    NEED(w_001CA6E0, 0x001CA6E0u);
    CALL(0x001CA6E0u, w->w_001CA6E0(w->ctx, a, model));
    NEED(w_001C6150, 0x001C6150u);
    CALL(0x001C6150u, w->w_001C6150(w->ctx, a->w44, &value));
    a->b0C = (uint8_t)value;
    for (unsigned i = 0; i < a->b0C; ++i) { /* +0xC is re-read on each pass */
        if (i >= EM_STATUS_SCENE_BONE_SLOTS)
            return fail(fault, 0x0020E6F0u, EM_STATUS_SCENE_FAULT_BAD_INDEX);
        NEED(w_001AF7C0, 0x001AF7C0u);
        CALL(0x001AF7C0u, w->w_001AF7C0(w->ctx, &value));
        a->w110[i] = value;
    }
    a->b09 = a->b0C;
    NEED(w_001CB5B0, 0x001CB5B0u);
    CALL(0x001CB5B0u, w->w_001CB5B0(w->ctx, a->b0C));
    a->w40 = g->d28A580;
    NEED(w_001C63E0, 0x001C63E0u);
    if (!em_ee_c_le(g->health, K(F_35))) {
        CALL(0x001C63E0u, w->w_001C63E0(w->ctx, a, 0x1C2));
        a->b0B = 0;
    } else {
        CALL(0x001C63E0u, w->w_001C63E0(w->ctx, a, 0xA));
        a->b0B = 1;
    }
    NEED(w_001CA5F0, 0x001CA5F0u);
    CALL(0x001CA5F0u, w->w_001CA5F0(w->ctx, a, 0xB));
    a->f80[0] = em_ee_mul(K(F_0_01), em_ee_mul(K(F_M80), g->infection));
    a->f80[1] = em_ee_mul(K(F_0_01), em_ee_mul(K(F_M100), g->infection));
    a->f80[2] = em_ee_mul(K(F_0_01), em_ee_mul(K(F_M30), g->infection));
    a->f80[3] = K(0);
    a->f38 = K(F_1);
    a->fC0[0] = K(0);
    a->fC0[1] = K(F_PI);
    a->fC0[2] = K(0);
    a->b04 = 1;
    a->fB0[0] = g->view[3];  /* D_0081061C */
    a->fB0[1] = g->view[7];  /* D_0081062C */
    a->fB0[2] = g->view[11]; /* D_0081063C */
    for (int k = 0; k < 3; ++k) /* 40 * column 2 */
        a->fB0[k] = em_ee_add(a->fB0[k], em_ee_mul(K(F_40), g->view[4 * k + 2]));
    for (int k = 0; k < 3; ++k) /* 7.4 * column 0 */
        a->fB0[k] = em_ee_add(a->fB0[k], em_ee_mul(K(F_7_4), g->view[4 * k + 0]));
    for (int k = 0; k < 3; ++k) /* 2.4 * column 1 */
        a->fB0[k] = em_ee_add(a->fB0[k], em_ee_mul(K(F_2_4), g->view[4 * k + 1]));
    return 1;
}

static int player_run(EmStatusSceneActor *a, const EmStatusScenePlayerGlobals *g,
                      const EmStatusSceneWorkers *w, EmStatusSceneFault *fault)
{
    if (a->b05 == 0) {
        a->f38 = em_ee_add(a->f38, K(F_0_01));
        if (!em_ee_c_lt(a->f38, K(F_1_3)))
            a->b05 = 1;
    } else {
        a->f38 = em_ee_sub(a->f38, K(F_0_01));
        if (em_ee_c_le(a->f38, K(F_1)))
            a->b05 = 0;
    }
    a->f80[0] = em_ee_mul(em_ee_mul(K(F_0_01), em_ee_mul(K(F_M80), g->infection)), a->f38);
    a->f80[1] = em_ee_mul(em_ee_mul(K(F_0_01), em_ee_mul(K(F_M100), g->infection)), a->f38);
    if (em_ee_c_lt(a->f80[1], K(F_M127)))
        a->f80[1] = K(F_M127);
    a->f80[2] = em_ee_mul(em_ee_mul(K(F_0_01), em_ee_mul(K(F_M30), g->infection)), a->f38);
    a->fC0[1] = em_ee_add(a->fC0[1], K(F_0_01));
    if (!em_ee_c_le(a->fC0[1], K(F_PI)))
        a->fC0[1] = em_ee_sub(a->fC0[1], K(F_TWO_PI));
    if (!em_ee_c_le(g->health, K(F_35)) && a->b0B == 1) {
        NEED(w_001C67E0, 0x001C67E0u);
        CALL(0x001C67E0u, w->w_001C67E0(w->ctx, a, 0x1C2, K(F_16), K(0)));
        a->b0B = 0;
    }
    NEED(w_001C64F0, 0x001C64F0u);
    CALL(0x001C64F0u, w->w_001C64F0(w->ctx, a, K(F_1)));
    NEED(w_0020EC80, 0x0020EC80u);
    CALL(0x0020EC80u, w->w_0020EC80(w->ctx, a));
    return 1;
}

int em_status_scene_player_0020E6F0(EmStatusSceneActor *actor, const EmStatusScenePlayerGlobals *g,
                                    const EmStatusSceneWorkers *w, EmStatusSceneFault *fault)
{
    if (latched(w, fault))
        return -1;
    if (!actor)
        return fail(fault, 0x0020E6F0u, EM_STATUS_SCENE_FAULT_NULL_WORKER);
    if (actor->b04 == 0 || actor->b04 == 1) {
        if (!g)
            return fail(fault, 0x00810858u, EM_STATUS_SCENE_FAULT_NULL_WORKER);
        return actor->b04 == 0 ? player_init(actor, g, w, fault) : player_run(actor, g, w, fault);
    }
    NEED(w_001AFF90, 0x001AFF90u); /* states 2, 3 and every other value */
    CALL(0x001AFF90u, w->w_001AFF90(w->ctx, actor));
    return 0;
}

/* ------------------------------------------------------------------------
 * 0020E460: the letter behaviour (NEARMISS C; translated from the .s). */
#define F_1_5 0x3FC00000u       /* 1.5 */
#define F_M18_4 0xC1933333u     /* -18.4 */

static int letter_init(EmStatusSceneActor *a, const EmStatusSceneLetterGlobals *g,
                       const EmStatusSceneWorkers *w, EmStatusSceneFault *fault)
{
    uint32_t value;
    if (g->d275BCC < (int32_t)a->b0C) { /* signed halfword D_00275BCC against the byte +0xC */
        a->b04 = 3;
        return 1;
    }
    unsigned i = 0;
    for (; i < a->b0C; ++i) { /* +0xC is re-read on each pass */
        if (i >= EM_STATUS_SCENE_BONE_SLOTS)
            return fail(fault, 0x0020E460u, EM_STATUS_SCENE_FAULT_BAD_INDEX);
        NEED(w_001AF7C0, 0x001AF7C0u);
        CALL(0x001AF7C0u, w->w_001AF7C0(w->ctx, &value));
        a->w110[i] = value;
    }
    a->b09 = a->b0C; /* the last +0xC value read */
    NEED(w_001CB5B0, 0x001CB5B0u);
    CALL(0x001CB5B0u, w->w_001CB5B0(w->ctx, a->b0C));
    NEED(w_001C62C0, 0x001C62C0u);
    CALL(0x001C62C0u, w->w_001C62C0(w->ctx, a));
    NEED(w_001CA5F0, 0x001CA5F0u);
    CALL(0x001CA5F0u, w->w_001CA5F0(w->ctx, a, 0xB));
    a->f80[0] = a->f80[1] = a->f80[2] = K(F_1_5);
    a->fC0[0] = K(0);
    a->fC0[1] = K(F_PI);
    a->fC0[2] = K(0);
    a->b04 = 1;
    /* +0xB0/B4/B8 = D_0081061C/2C/3C, then per axis: 40 * column 2,
     * -18.4 * column 0, 1.3 * column 1 (each mul.s then add.s). */
    a->fB0[0] = g->view[3];
    a->fB0[1] = g->view[7];
    a->fB0[2] = g->view[11];
    static const uint32_t factor[3] = {F_40, F_M18_4, F_1_3};
    static const int column[3] = {2, 0, 1};
    for (int t = 0; t < 3; ++t)
        for (int k = 0; k < 3; ++k)
            a->fB0[k] = em_ee_add(a->fB0[k], em_ee_mul(K(factor[t]), g->view[4 * k + column[t]]));
    return 1;
}

static int letter_run(EmStatusSceneActor *a, const EmStatusSceneLetterGlobals *g,
                      const EmStatusSceneWorkers *w, EmStatusSceneFault *fault)
{
    NEED(w_001C6380, 0x001C6380u);
    CALL(0x001C6380u, w->w_001C6380(w->ctx, a));
    if (em_status_scene_glyph_0020E3A0(0x15) == (int32_t)a->b0D) {
        if (!g->d275B40)
            return fail(fault, 0x00275B40u, EM_STATUS_SCENE_FAULT_NULL_WORKER);
        uint32_t row = g->d275B40[0];
        NEED(w_001026A0, 0x001026A0u);
        CALL(0x001026A0u, w->w_001026A0(w->ctx, row + 0xC0u, row + 0x90u,
                                         EM_STATUS_SCENE_D_0024A340));
    }
    NEED(w_001D2040, 0x001D2040u);
    CALL(0x001D2040u, w->w_001D2040(w->ctx, 0, 1));
    NEED(w_call, a->w4C);
    CALL(a->w4C, w->w_call(w->ctx, a, a->w4C));
    CALL(0x001D2040u, w->w_001D2040(w->ctx, 0, 0));
    return 1;
}

int em_status_scene_letter_0020E460(EmStatusSceneActor *actor, const EmStatusSceneLetterGlobals *g,
                                    const EmStatusSceneWorkers *w, EmStatusSceneFault *fault)
{
    if (latched(w, fault))
        return -1;
    if (!actor)
        return fail(fault, 0x0020E460u, EM_STATUS_SCENE_FAULT_NULL_WORKER);
    if (actor->b04 == 0 || actor->b04 == 1) {
        if (!g)
            return fail(fault, 0x00275BCCu, EM_STATUS_SCENE_FAULT_NULL_WORKER);
        return actor->b04 == 0 ? letter_init(actor, g, w, fault) : letter_run(actor, g, w, fault);
    }
    NEED(w_001AFF90, 0x001AFF90u); /* states 2, 3 and every other value */
    CALL(0x001AFF90u, w->w_001AFF90(w->ctx, actor));
    return 0;
}

/* ------------------------------------------------------------------------
 * 0020EC80 (byte-matched C): the menu player's object matrix, then its
 * bones (001C69A0) and draw. */
#define B_M1 0xBF800000u        /* -1.0 */

int em_status_scene_publish_0020EC80(EmStatusSceneActor *actor, uint8_t d8104E4,
                                     EmStatusSceneScratch *spr, const EmStatusSceneWorkers *w,
                                     EmStatusSceneFault *fault)
{
    const uint32_t m3400 = EM_STATUS_SCENE_SPR_3400, m3440 = EM_STATUS_SCENE_SPR_3440;
    if (latched(w, fault))
        return -1;
    if (!actor || !spr)
        return fail(fault, 0x0020EC80u, EM_STATUS_SCENE_FAULT_NULL_WORKER);
    NEED(w_001029C0, 0x001029C0u);
    NEED(w_00102B08, 0x00102B08u);
    NEED(w_00102BB0, 0x00102BB0u);
    NEED(w_00102A60, 0x00102A60u);
    NEED(w_001026D0, 0x001026D0u);
    CALL(0x001029C0u, w->w_001029C0(w->ctx, m3400));
    CALL(0x00102B08u, w->w_00102B08(w->ctx, m3400, m3400, em_ee_bits(actor->fC0[0])));
    CALL(0x00102BB0u, w->w_00102BB0(w->ctx, m3400, m3400, em_ee_bits(actor->fC0[1])));
    CALL(0x00102A60u, w->w_00102A60(w->ctx, m3400, m3400, em_ee_bits(actor->fC0[2])));
    CALL(0x001029C0u, w->w_001029C0(w->ctx, m3440));
    spr->s3440[0] = spr->s3440[5] = spr->s3440[10] = K(B_M1);
    CALL(0x001026D0u, w->w_001026D0(w->ctx, m3400, m3440, m3400));
    CALL(0x00102B08u, w->w_00102B08(w->ctx, m3400, m3400, F_PI));
    /* 001031E0(0x70003430, actor + 0xB0): three word copies. */
    spr->s3400[12] = actor->fB0[0];
    spr->s3400[13] = actor->fB0[1];
    spr->s3400[14] = actor->fB0[2];
    /* copy_qw4(0x700036A0, 0x70003400): a raw 64-byte copy. */
    memcpy(spr->s36A0, spr->s3400, sizeof spr->s36A0);
    NEED(w_001C69A0, 0x001C69A0u);
    CALL(0x001C69A0u, w->w_001C69A0(w->ctx, actor));
    if (d8104E4 == 1) {
        static const uint32_t position[4] = {0x404CCCCDu, 0xBFC00000u, 0xBF19999Au, 0x3F800000u};
        static const uint32_t colour[4] = {0x20, 0x70, 0x80, 0x80};
        memcpy(spr->s38A0, position, sizeof position);
        NEED(w_001026A0, 0x001026A0u);
        /* *(arg0 + 0x118) + 0x90: bone slot 2's world matrix. */
        CALL(0x001026A0u, w->w_001026A0(w->ctx, EM_STATUS_SCENE_SPR_38A0, actor->w110[2] + 0x90u,
                                         EM_STATUS_SCENE_SPR_38A0));
        memcpy(spr->s38B0, colour, sizeof colour);
        if (em_status_scene_glow_001F4BF0(EM_STATUS_SCENE_SPR_38A0, spr->s38B0, w, fault) < 0)
            return -1;
    }
    NEED(w_001D2040, 0x001D2040u);
    CALL(0x001D2040u, w->w_001D2040(w->ctx, 0, 1));
    NEED(w_call, actor->w4C);
    CALL(actor->w4C, w->w_call(w->ctx, actor, actor->w4C));
    CALL(0x001D2040u, w->w_001D2040(w->ctx, 0, 0));
    return 0;
}

/* ------------------------------------------------------------------------
 * 001F4BF0 (asm): k = (c3 + ((c3 * ((rand() >> 23) & 0xFF)) >> 8)) >> 1,
 * then channel n = (cn * k) >> 7 packed as c0 | c1 << 8 | c2 << 16 (32-bit
 * products and logical shifts), and 001CD520(0, 2, position, TEX0, rgb,
 * 3.0, 3.0, 1.5). */
int em_status_scene_glow_001F4BF0(uint32_t position, const uint32_t colour[4],
                                  const EmStatusSceneWorkers *w, EmStatusSceneFault *fault)
{
    int32_t r;
    if (latched(w, fault))
        return -1;
    if (!colour)
        return fail(fault, 0x001F4BF0u, EM_STATUS_SCENE_FAULT_NULL_WORKER);
    NEED(w_00122BB8, 0x00122BB8u);
    CALL(0x00122BB8u, w->w_00122BB8(w->ctx, &r));
    if (r < 0)
        return fail(fault, 0x00122BB8u, EM_STATUS_SCENE_FAULT_BAD_RESULT);
    uint32_t pulse = ((uint32_t)r >> 23) & 0xFFu;
    uint32_t k = (colour[3] + ((colour[3] * pulse) >> 8)) >> 1;
    uint32_t rgb = ((colour[2] * k) >> 7) << 16 | ((colour[1] * k) >> 7) << 8 |
                   (colour[0] * k) >> 7;
    NEED(w_001CD520, 0x001CD520u);
    CALL(0x001CD520u, w->w_001CD520(w->ctx, 0, 2, position, EM_STATUS_SCENE_TEX0_001F4BF0, rgb,
                                     K(0x40400000u), K(0x40400000u), K(F_1_5)));
    return 0;
}

/* ------------------------------------------------------------------------
 * The static actor pool D_0028B020 (001AFF10, 001AFF90, 001AF800,
 * 001AFEB0 and 001AFE60 are byte-matched C; 001B0000 matched). */
EmStatusSceneActor *em_status_scene_alloc_001AFF10(EmStatusScenePool *pool)
{
    if (!pool)
        return NULL;
    for (unsigned i = 0; i < EM_STATUS_SCENE_POOL_RECORDS; ++i) {
        EmStatusSceneActor *a = &pool->record[i];
        if (a->b00)
            continue;
        a->w14 = EM_STATUS_SCENE_D_0028B020 + i * EM_STATUS_SCENE_RECORD_SIZE;
        a->b00 = 2;
        for (int k = 0; k < 4; ++k)
            a->f60[k] = a->f80[k] = K(F_1);
        a->b9A = 0;
        a->b99 = 0;
        a->h94 = -1;
        return a;
    }
    return NULL;
}

int em_status_scene_bones_001AF800(EmStatusSceneActor *actor, const EmStatusSceneWorkers *w,
                                   EmStatusSceneFault *fault)
{
    if (latched(w, fault))
        return -1;
    if (!actor)
        return fail(fault, 0x001AF800u, EM_STATUS_SCENE_FAULT_NULL_WORKER);
    for (unsigned i = 0; i < actor->b09; ++i) { /* +9 is re-read on each pass */
        if (i >= EM_STATUS_SCENE_BONE_SLOTS)
            return fail(fault, 0x001AF800u, EM_STATUS_SCENE_FAULT_BAD_INDEX);
        NEED(w_001AF800_slot, 0x001AF800u);
        CALL(0x001AF800u, w->w_001AF800_slot(w->ctx, actor->w110[i]));
        actor->w110[i] = 0;
    }
    actor->b09 = 0;
    actor->b0C = 0;
    return 0;
}

int em_status_scene_free_001AFF90(EmStatusScenePool *pool, EmStatusSceneActor *actor,
                                  const EmStatusSceneWorkers *w, EmStatusSceneFault *fault)
{
    if (latched(w, fault))
        return -1;
    if (!pool || !actor)
        return fail(fault, 0x001AFF90u, EM_STATUS_SCENE_FAULT_NULL_WORKER);
    uint32_t self = actor->w14, offset = self - EM_STATUS_SCENE_D_0028B020;
    if (self < EM_STATUS_SCENE_D_0028B020 || offset % EM_STATUS_SCENE_RECORD_SIZE ||
        offset / EM_STATUS_SCENE_RECORD_SIZE >= EM_STATUS_SCENE_POOL_RECORDS)
        return fail(fault, self, EM_STATUS_SCENE_FAULT_BAD_INDEX);
    EmStatusSceneActor *s0 = &pool->record[offset / EM_STATUS_SCENE_RECORD_SIZE];
    actor->w14 = 0;
    if (em_status_scene_bones_001AF800(s0, w, fault) < 0)
        return -1;
    s0->b00 = s0->b01 = s0->b02 = s0->b03 = 0; /* four word stores +0x0..+0xF */
    s0->b04 = s0->b05 = s0->b06 = s0->b07 = 0;
    s0->b08 = s0->b09 = s0->b0A = s0->b0B = 0;
    s0->b0C = s0->b0D = s0->b0E = s0->b0F = 0;
    s0->h36 = 0;
    s0->b98 = 0;
    s0->w90 = 0;
    for (unsigned i = (0x1F0u - 0x110u) / 4u; i < EM_STATUS_SCENE_BONE_SLOTS; ++i)
        s0->w110[i] = 0; /* +0x1F0..+0x2EF */
    return 0;
}

int em_status_scene_release_001AFEB0(EmStatusScenePool *pool, const EmStatusSceneWorkers *w,
                                     EmStatusSceneFault *fault)
{
    if (latched(w, fault))
        return -1;
    if (!pool)
        return fail(fault, 0x001AFEB0u, EM_STATUS_SCENE_FAULT_NULL_WORKER);
    for (unsigned i = 0; i < EM_STATUS_SCENE_POOL_RECORDS; ++i)
        if (pool->record[i].b00 && em_status_scene_bones_001AF800(&pool->record[i], w, fault) < 0)
            return -1;
    return 0;
}

void em_status_scene_clear_001AFE60(EmStatusScenePool *pool)
{
    if (pool)
        memset(pool, 0, sizeof *pool);
}

int em_status_scene_walk_001B0000(EmStatusScenePool *pool, const EmStatusSceneWorkers *w,
                                  EmStatusSceneFault *fault)
{
    if (latched(w, fault))
        return -1;
    if (!pool)
        return fail(fault, 0x001B0000u, EM_STATUS_SCENE_FAULT_NULL_WORKER);
    for (unsigned i = 0; i < EM_STATUS_SCENE_POOL_RECORDS; ++i) {
        EmStatusSceneActor *a = &pool->record[i];
        if (!a->b00)
            continue;
        NEED(w_001CB590, 0x001CB590u);
        CALL(0x001CB590u, w->w_001CB590(w->ctx, a, (int32_t)EM_STATUS_SCENE_RECORD_SIZE, a->b09));
        /* D_00275B44 is the record 001CB590 just published. */
        NEED(w_call, a->w10);
        CALL(a->w10, w->w_call(w->ctx, a, a->w10));
    }
    return 0;
}

/* ------------------------------------------------------------------------
 * The module loader. user[k] is task record byte +8 + k. */
#define U_STATE 0   /* +0x08 */
#define U_STEP 1    /* +0x09 */
#define U_SUB 3     /* +0x0B */
#define U_MODULE 6  /* +0x0E */
#define U_KIND 7    /* +0x0F */
#define U_COUNT 12  /* +0x14 (u16) */
#define U_INDEX 14  /* +0x16 (u16) */

static uint16_t u16_at(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static void u16_put(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

/* A word of D_00289BC0 by original address (fault outside the buffer). */
static int header_word(const EmStatusSceneLoader *ld, uint32_t address, unsigned size,
                       uint32_t *value, EmStatusSceneFault *fault)
{
    uint32_t off = address - EM_STATUS_SCENE_D_00289BC0;
    if (address < EM_STATUS_SCENE_D_00289BC0 || off > sizeof ld->header - size)
        return fail(fault, address, EM_STATUS_SCENE_FAULT_BAD_INDEX);
    uint32_t v = 0;
    for (unsigned i = 0; i < size; ++i)
        v |= (uint32_t)ld->header[off + i] << (8 * i);
    *value = v;
    return 0;
}

#define HDR(address, size, out)                                                                 \
    do {                                                                                        \
        if (header_word(ld, (address), (size), (out), fault) < 0)                              \
            return -1;                                                                          \
    } while (0)

static int read_start(EmStatusSceneLoader *ld, uint32_t file, uint32_t buf, uint32_t offset,
                      uint32_t size, const EmStatusSceneWorkers *w, EmStatusSceneFault *fault)
{
    NEED(w_00200780, 0x00200780u);
    CALL(0x00200780u, w->w_00200780(w->ctx, file, buf, (int32_t)offset, (int32_t)size,
                                     buf == EM_STATUS_SCENE_D_00289BC0 ? ld->header : NULL));
    return 0;
}

static int poll(const EmStatusSceneWorkers *w, EmStatusSceneFault *fault, int32_t *status)
{
    NEED(w_00200730, 0x00200730u);
    CALL(0x00200730u, w->w_00200730(w->ctx, status));
    return 0;
}

static int section(uint32_t address, const EmStatusSceneWorkers *w, EmStatusSceneFault *fault)
{
    NEED(w_00200830, 0x00200830u);
    CALL(0x00200830u, w->w_00200830(w->ctx, address));
    return 0;
}

/* 001FF3F0 (translated from the .s): 1 when every chunk is in. */
static int chunks_001FF3F0(uint8_t *user, EmStatusSceneLoader *ld, const EmStatusSceneWorkers *w,
                           EmStatusSceneFault *fault, int *done)
{
    uint32_t v, v2;
    int32_t status;
    *done = 0;
    switch (user[U_SUB]) {
    case 0:
        ld->d275C70 = EM_STATUS_SCENE_D_00289BC0;
        HDR(EM_STATUS_SCENE_D_00289BC0 + 0xE, 2, &v);
        u16_put(user + U_COUNT, (uint16_t)v);
        if (!u16_at(user + U_COUNT)) {
            *done = 1; /* v0 still holds the state-1 compare constant 1 */
            return 0;
        }
        u16_put(user + U_INDEX, 0);
        user[U_SUB] = (uint8_t)(user[U_SUB] + 1);
        /* fall through */
    case 1: {
        uint32_t entry = ld->d275C70 + ((uint32_t)u16_at(user + U_INDEX) << 3);
        HDR(ld->d275C70 + 4, 4, &v);
        HDR(entry + 0x20, 4, &v2);
        uint32_t size;
        HDR(entry + 0x24, 4, &size);
        if (read_start(ld, EM_STATUS_SCENE_D_0028A488, ld->d275C74, v2 + v, size, w, fault) < 0)
            return -1;
        user[U_SUB] = (uint8_t)(user[U_SUB] + 1);
        return 0;
    }
    case 2:
        if (poll(w, fault, &status) < 0)
            return -1;
        if (status == 0)
            return 0;
        if (status == 1) {
            user[U_SUB] = (uint8_t)(user[U_SUB] + 1);
            u16_put(user + U_INDEX, (uint16_t)(u16_at(user + U_INDEX) + 1));
        } else {
            user[U_SUB] = (uint8_t)(user[U_SUB] - 1);
        }
        return 0;
    case 3:
        if (section(ld->d275C74, w, fault) < 0)
            return -1;
        u16_put(user + U_COUNT, (uint16_t)(u16_at(user + U_COUNT) - 1));
        if (!u16_at(user + U_COUNT)) {
            user[U_SUB] = 0;
            *done = 1;
            return 0;
        }
        user[U_SUB] = 1;
        return 0;
    default:
        return 0;
    }
}

/* 001FF830(module) (byte-matched C). */
static int bank_001FF830(uint8_t module, uint8_t *user, EmStatusSceneLoader *ld,
                         const EmStatusSceneWorkers *w, EmStatusSceneFault *fault)
{
    uint32_t a, b, c;
    int32_t status;
    switch (user[U_STEP]) {
    case 0:
        switch (module) {
        case 2:
        case 3:
            ld->d275C74 = ld->d28A738;
            user[U_KIND] = 0;
            break;
        case 1:
        case 0x27:
        case 0x28:
        case 0x29:
        case 0x37:
            ld->d275C74 = EM_STATUS_SCENE_FIXED_BUFFER;
            user[U_KIND] = 1;
            break;
        case 0x1D:
            ld->d275C74 = ld->d28A5A0;
            user[U_KIND] = 2;
            break;
        case 0x32:
        case 0x33:
        case 0x34:
        case 0x35:
            ld->d275C74 = ld->d28A744;
            user[U_KIND] = 3;
            break;
        case 0x36:
            ld->d275C74 = ld->d28A748;
            user[U_KIND] = 2;
            break;
        case 0x2A:
        case 0x2B:
            ld->d275C74 = ld->spad3B90 == 0 ? EM_STATUS_SCENE_FIXED_BUFFER : ld->d28A748;
            user[U_KIND] = 1;
            break;
        default:
            ld->d275C74 = ld->d28A748;
            user[U_KIND] = 1;
            break;
        }
        user[U_STEP] = (uint8_t)(user[U_STEP] + 1);
        return read_start(ld, EM_STATUS_SCENE_D_0028A480, EM_STATUS_SCENE_D_00289BC0,
                          (uint32_t)module << 11, 0x800, w, fault);
    case 1:
    case 4:
        if (poll(w, fault, &status) < 0)
            return -1;
        if (status == 1)
            user[U_STEP] = (uint8_t)(user[U_STEP] + 1);
        else if (status != 0)
            user[U_STEP] = user[U_STEP] == 1 ? 0 : (uint8_t)(user[U_STEP] - 1);
        return 0;
    case 2: {
        int done;
        if (chunks_001FF3F0(user, ld, w, fault, &done) < 0)
            return -1;
        if (done)
            user[U_STEP] = (uint8_t)(user[U_STEP] + 1);
        return 0;
    }
    case 3:
        user[U_STEP] = (uint8_t)(user[U_STEP] + 1);
        HDR(ld->d275C70 + 0x14, 4, &a);
        HDR(ld->d275C70 + 4, 4, &b);
        HDR(ld->d275C70 + 8, 4, &c);
        return read_start(ld, EM_STATUS_SCENE_D_0028A488, ld->d275C74, b + a, c - a, w, fault);
    case 5: {
        user[U_STEP] = 7;
        HDR(ld->d275C70 + 8, 4, &c);
        HDR(ld->d275C70 + 0x14, 4, &a);
        uint32_t end = ld->d275C74 + (c - a);
        if (user[U_KIND] == 0) {
            ld->d28A73C = end;
        } else if (user[U_KIND] == 2) {
            ld->d28A744 = end;
            ld->d28A748 = ld->d28A744;
        } else if (user[U_KIND] == 3) {
            user[U_STEP] = 6;
        }
        return 0;
    }
    case 6: {
        uint32_t result = 0;
        NEED(w_001FB370, 0x001FB370u);
        CALL(0x001FB370u, w->w_001FB370(w->ctx, ld->d275C74, &result));
        if (result) {
            ld->d28A748 = result;
            user[U_STEP] = (uint8_t)(user[U_STEP] + 1);
        }
        return 0;
    }
    case 7: {
        uint32_t base = ld->d275C74, n, h, count, offset = 0;
        HDR(ld->d275C70 + 0x10, 4, &n);
        HDR(ld->d275C70 + 0xE, 2, &h);
        for (uint32_t i = 0; i < n; ++i) {
            if (section(base + offset, w, fault) < 0)
                return -1;
            HDR(ld->d275C70 + ((h + i) << 3) + 0x24, 4, &a);
            offset += a;
        }
        HDR(ld->d275C70 + 0x1C, 4, &count);
        for (uint32_t k = 0; k < count; ++k) {
            uint32_t e;
            HDR(ld->d275C70 + (h + n) * 8 + 0x20 + 4 * k, 4, &e);
            if ((e >> 24) >= EM_STATUS_SCENE_RELOC_WORDS)
                return fail(fault, 0x0028A490u + 4 * (e >> 24), EM_STATUS_SCENE_FAULT_BAD_INDEX);
            ld->d28A490[e >> 24] = (e & 0xFFFFFFu) + base;
        }
        user[U_STATE] = 0x63;
        user[U_STEP] = 0;
        return 0;
    }
    default:
        return 0;
    }
}

int32_t em_status_scene_bank_001FEF70(uint8_t ca4, uint8_t ca6)
{
    if (ca4 == 2)
        return 0x35;
    if (ca6 == 1)
        return 0x32;
    if ((unsigned)(ca6 - 2) < 2)
        return 0x33;
    if (ca6 == 4)
        return 0x34;
    return -1;
}

void em_status_scene_loader_request_001FF080(uint8_t *slot_state, uint8_t user[24],
                                             uint8_t state, uint8_t module)
{
    *slot_state = 1;         /* 001AB740(2, 001FF0D0) */
    memset(user, 0, 16);     /* +8..+0x17 */
    user[U_STATE] = state;   /* D_0028A798 */
    user[U_MODULE] = module; /* D_0028A79E */
}

int em_status_scene_loader_001FF0D0(uint8_t *slot_state, uint8_t user[24], EmStatusSceneLoader *ld,
                                    const EmStatusSceneWorkers *w, EmStatusSceneFault *fault)
{
    if (latched(w, fault))
        return -1;
    if (!slot_state || !user || !ld)
        return fail(fault, 0x0028A790u, EM_STATUS_SCENE_FAULT_NULL_WORKER);
    if (ld->d282157 != 0)
        return 0;
    switch (user[U_STATE]) {
    case 0:
        return bank_001FF830(user[U_MODULE], user, ld, w, fault) < 0 ? -1 : 0;
    case 1: {
        NEED(w_001FFCD0, 0x001FFCD0u);
        CALL(0x001FFCD0u, w->w_001FFCD0(w->ctx, user));
        if (user[U_STATE] == 0x63) {
            int32_t r = em_status_scene_bank_001FEF70(ld->d810CA4, ld->d810CA6);
            if (r != -1) {
                user[U_MODULE] = (uint8_t)r;
                memset(user, 0, 5); /* +8..+0xC */
            }
        }
        return 0;
    }
    case 2:
        NEED(w_00200360, 0x00200360u);
        CALL(0x00200360u, w->w_00200360(w->ctx, user));
        return 0;
    case 0x63:
        ld->d275BD8 = 0;
        *slot_state = 0; /* 001AB7D0 */
        return 0;
    default:
        return 0;
    }
}
