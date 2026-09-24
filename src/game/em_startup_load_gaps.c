/* em_startup_load_gaps.c - the title / New Game / load / opening originals
 * of census lane L34 (see em_startup_load_gaps.h, docs/STARTUP_LOAD_GAPS.md).
 *
 * Read from the original instructions: the byte-matched decomp C for
 * 001AB4E0, 001AB590, 001AC070, 001AF5C0, 001AF690, 001AF710, 001AFCA0,
 * 001B0F60, 001B57E0, 001B62A0 and the overlay C of 00823780; the split
 * listing for the asm-word units 001AF470, 001B5F40 and 001BB0E0 and the
 * NEARMISS 00199C50. Every original address a branch or store comes from is
 * cited beside it. The sound routines are in em_startup_load_gaps_sound.c. */
#include "game/em_startup_load_gaps.h"

#include <string.h>

#define CALL(expr) do { if ((expr) < 0) return -1; } while (0)

static uint16_t rd16(const uint8_t *p, unsigned at) { return (uint16_t)(p[at] | p[at + 1] << 8); }
static uint32_t rd32(const uint8_t *p, unsigned at)
{
    return (uint32_t)p[at] | (uint32_t)p[at + 1] << 8 | (uint32_t)p[at + 2] << 16 |
           (uint32_t)p[at + 3] << 24;
}
static void wr16(uint8_t *p, unsigned at, uint32_t v) { p[at] = (uint8_t)v; p[at + 1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, unsigned at, uint32_t v)
{
    p[at] = (uint8_t)v; p[at + 1] = (uint8_t)(v >> 8);
    p[at + 2] = (uint8_t)(v >> 16); p[at + 3] = (uint8_t)(v >> 24);
}

#define ONE_F UINT32_C(0x3F800000)   /* 1.0f */

/* ====================================================================== */
/* 001AB4E0                                                               */
/* ====================================================================== */

/* Both environments get 001002E0(env, psm 0, 512, 224, dx, (short)(dy << 1)),
 * then the FBP field (bits 0..8 of the 64-bit DISPFB word at env +0x10) is
 * set to 0 for D_00810EA0 and 0x38 for D_00810EC8. */
int em_slg_001AB4E0(const EmSlgDisplayWorkers *w,
                    uint8_t env[2][EM_SLG_DISPENV_SIZE], int32_t dx, int32_t dy)
{
    if (!w || !w->w_001002E0 || !env) return -1;
    int32_t y = (int16_t)(uint16_t)((uint32_t)dy << 1);           /* 001AB4E0: (short)(arg1 << 1) */
    CALL(w->w_001002E0(w->ctx, env[0], 0, 0x200, 0xE0, dx, y));
    CALL(w->w_001002E0(w->ctx, env[1], 0, 0x200, 0xE0, dx, y));
    for (int i = 0; i < 2; i++) {
        uint8_t *f = env[i] + EM_SLG_DISPFB_OFFSET;               /* D_00810EB0 / D_00810ED8 */
        uint64_t v = (uint64_t)rd32(f, 0) | (uint64_t)rd32(f, 4) << 32;
        v = (v & ~(uint64_t)0x1FF) | (i ? 0x38u : 0u);
        wr32(f, 0, (uint32_t)v);
        wr32(f, 4, (uint32_t)(v >> 32));
    }
    return 0;
}

/* ====================================================================== */
/* 001AB590                                                               */
/* ====================================================================== */

void em_slg_001AB590(uint8_t chcr_byte0[3])
{
    for (int i = 0; i < 3; i++)                  /* D0, D1, D2 in that order */
        if (chcr_byte0[i] & 0x30)                /* ASP (bits 5:4) nonzero */
            chcr_byte0[i] &= (uint8_t)~0x30;
}

/* ====================================================================== */
/* 001AC070                                                               */
/* ====================================================================== */

#define R(k) user[(k) - 8]

static int flow_leave_edges(uint8_t *user) { R(9) = 0; R(0xA) = 0; return 0; }

int em_slg_001AC070(const EmSlgFlowWorkers *w, EmSlgFlowGlobals *g, uint8_t *user)
{
    if (!w || !g || !user) return -1;
    int32_t r;
    /* The workers the current state can reach, checked before the 3B90 store. */
    switch (R(8)) {
    case 0: if (!w->w_001AEDB0 || !w->w_001D1EF0) return -1; break;
    case 1: if (!w->w_001AC3B0) return -1; break;
    case 2: if (!w->w_001AC480 || !w->w_00225A00) return -1; break;
    case 3: if (!w->w_001ACA20 || !w->w_001FBC50 || !w->w_001D2880 || !w->w_001AEDB0) return -1; break;
    case 4: if (!w->w_001AB790) return -1; break;
    case 5: if (!w->w_00225AC0 || !w->w_001AF150) return -1; break;
    case 6: if (!w->w_00200A40) return -1; break;
    default: break;
    }
    if (R(8) != 4 && !w->w_001D2830) return -1;

    g->s3B90 = 0;                                                  /* spad 0x70003B90 */
    switch (R(8)) {                                                /* jtbl_0026DC90 */
    case 0:
        g->d275BD4 = 0;
        CALL(w->w_001AEDB0(w->ctx, 0));
        CALL(w->w_001D1EF0(w->ctx));
        if (g->d275BDC == 0) { R(8) = 1; R(0xE) = 1; }
        else { R(8) = 2; R(0xE) = 3; }
        break;
    case 1:
        CALL(w->w_001AC3B0(w->ctx, &r));
        if (r != 0) { R(8) = 2; flow_leave_edges(user); }
        break;
    case 2:
        CALL(w->w_001AC480(w->ctx, &r));
        if (r == 3) {
            if (R(0xE) == 1) { R(8) = 3; R(0xE) = 3; }
            else { R(8) = 1; R(0xE) = 1; }
            flow_leave_edges(user);
        } else if (r == 1) {
            uint8_t f = R(0xF);                                    /* the latched selector */
            if (f == 0) { R(8) = 4; g->d275BE0 = 0; }
            else if (f == 1) {
                CALL(w->w_00225A00(w->ctx));
                R(8) = 5; g->d275BE0 = 1;
            } else { R(8) = 6; R(0xC) = 0; }
            flow_leave_edges(user);
        }
        break;
    case 3:
        CALL(w->w_001ACA20(w->ctx, &r));
        if ((uint32_t)r - 2u < 2u) {                               /* 2 or 3: leave */
            g->d275BD4 = (int32_t)((uint32_t)g->d275BD4 + 1u);     /* wraps as the EE add does */
            if (g->d275BD4 > 2) g->d275BD4 = 0;
            CALL(w->w_001FBC50(w->ctx));
            CALL(w->w_001D2880(w->ctx));
            CALL(w->w_001AEDB0(w->ctx, 0));
            R(8) = 2; flow_leave_edges(user);
            g->d275BE0 = 0;
        }
        break;
    case 4:                                                        /* handoff, no tail */
        return w->w_001AB790(w->ctx, EM_SLG_FN_001ACEC0) < 0 ? -1 : 0;
    case 5:
        CALL(w->w_00225AC0(w->ctx, 0, &r));
        if (r == 1) { R(8) = 2; flow_leave_edges(user); }
        else if (r == 2) {
            CALL(w->w_001AF150(w->ctx));
            g->d275BE0 = 1;
            R(8) = 4; flow_leave_edges(user);
        }
        break;
    case 6:
        CALL(w->w_00200A40(w->ctx, &r));
        if (r != 0) R(8) = 2;
        break;
    default:
        break;
    }
    return w->w_001D2830(w->ctx, 3, 1) < 0 ? -1 : 0;
}

#undef R

/* ====================================================================== */
/* 001AF470                                                               */
/* ====================================================================== */

/* The three assignments (halfwords at spad 0x3B74, 76, 78, 7A, 7C, 7E, 80, 82). */
void em_slg_001AF470(uint16_t map[8], int32_t config)
{
    static const uint16_t table[3][8] = {
        { 0x80, 0x40, 0x20, 0x10, 0x08, 0x02, 0x04, 0x01 },   /* config 0 */
        { 0x20, 0x40, 0x80, 0x10, 0x08, 0x02, 0x04, 0x01 },   /* config 1 */
        { 0x80, 0x20, 0x40, 0x10, 0x02, 0x08, 0x04, 0x01 },   /* config 2 */
    };
    unsigned c = (uint32_t)config & 0xFF;                           /* 001AF470: a0 & 0xFF */
    if (c > 2) return;
    memcpy(map, table[c], sizeof table[c]);
}

/* ====================================================================== */
/* 001AFCA0 and its leaves                                                */
/* ====================================================================== */

static int state0_bound(const EmSlgState0Workers *w, const EmSlgState0 *s)
{
    return w && s && s->player && s->status && s->d81060C && s->bones && s->bones->records &&
           s->s31F4 && w->w_001D8BF0;
}

/* 001AF5C0: memset the player record, then the named fields, then
 * 001D8BF0(player, 1). */
int em_slg_001AF5C0(const EmSlgState0Workers *w, EmSlgState0 *s)
{
    if (!state0_bound(w, s)) return -1;
    uint8_t *p = s->player;
    memset(p, 0, EM_SLG_PLAYER_SIZE);                              /* 00121A28(D_008102B0, 0, 0x320) */
    wr32(p, 0x14, s->player_self);                                 /* D_008102C4 = D_008102B0 */
    p[0x02] = 0;                                                   /* D_008102B2 */
    for (unsigned at = 0x60; at <= 0x6C; at += 4) wr32(p, at, ONE_F);   /* D_00810310..1C */
    for (unsigned at = 0x80; at <= 0x8C; at += 4) wr32(p, at, ONE_F);   /* D_00810330..3C */
    wr32(p, 0x70, 0);                                              /* D_00810320 */
    wr32(p, 0x74, 0);                                              /* D_00810324 */
    wr32(p, 0x78, ONE_F);                                          /* D_00810328 */
    wr32(p, 0x7C, ONE_F);                                          /* D_0081032C */
    wr16(p, 0x94, 0xFFFF);                                         /* D_00810344 = -1 */
    wr16(p, 0x96, 0x3D);                                           /* D_00810346 */
    return w->w_001D8BF0(w->ctx, p, 1) < 0 ? -1 : 0;
}

/* 001AF690: D_008101E0 (0xD0), D_008101D0 (0x10), D_00810130 (0xA0), then
 * D_0081060C = 1.0f. */
void em_slg_001AF690(EmSlgState0 *s)
{
    memset(s->status + 0xB0, 0, 0xD0);
    memset(s->status + 0xA0, 0, 0x10);
    memset(s->status + 0x00, 0, 0xA0);
    *s->d81060C = ONE_F;
}

/* 001AF710: zero 0x480 records of 0xD0 bytes, record each one's address in
 * D_007D4640[], then D_00275BD0 = D_007D4640 and D_00275BCC = 0x480. */
void em_slg_001AF710(EmSlgBoneSlots *b)
{
    for (uint32_t i = 0; i < EM_SLG_BONE_SLOTS; i++) {
        memset(b->records + i * EM_SLG_BONE_SLOT_SIZE, 0, EM_SLG_BONE_SLOT_SIZE);   /* 13 zero quadwords */
        b->slot[i] = EM_SLG_BONE_RECORDS + i * EM_SLG_BONE_SLOT_SIZE;
    }
    b->head = EM_SLG_BONE_ARRAY;                                   /* D_00275BD0 = D_007D4640 */
    b->count = 0x480;                                              /* D_00275BCC */
}

int em_slg_001AFCA0(const EmSlgState0Workers *w, EmSlgState0 *s)
{
    if (!state0_bound(w, s) || !w->w_001AF8E0 || !w->w_001D0660) return -1;
    CALL(em_slg_001AF5C0(w, s));
    em_slg_001AF690(s);
    em_slg_001AF710(s->bones);
    CALL(w->w_001AF8E0(w->ctx));
    CALL(w->w_001D0660(w->ctx));
    *s->s31F4 = 0;                                                 /* spad 0x700031F4 */
    return 0;
}

/* ====================================================================== */
/* 001B0F60                                                               */
/* ====================================================================== */

int em_slg_001B0F60(const EmSlgNodeStartWorkers *w, uint8_t *node, int32_t n,
                    uint32_t d28A574, int32_t *ret)
{
    if (!w || !node || !ret || !w->w_001B0EA0 || !w->w_001C63E0) return -1;
    int32_t r;
    CALL(w->w_001B0EA0(w->ctx, node, &r));
    if (r != 0) { *ret = 1; return 0; }
    wr32(node, 0x40, d28A574);                                     /* node +0x40 = D_0028A574 */
    CALL(w->w_001C63E0(w->ctx, node, (int16_t)n));                 /* (short)n */
    node[4] = (uint8_t)(node[4] + 1);
    *ret = 0;
    return 0;
}

/* ====================================================================== */
/* 001B57E0 / 001B5F40 / 001B62A0                                          */
/* ====================================================================== */

/* 001B62A0 (byte-matched): +0x16 = 0, u16 +0x28 = 0, +0x18 = 0, +0x19 = 0. */
void em_slg_001B62A0(uint8_t *pad)
{
    pad[0x16] = 0;
    wr16(pad, 0x28, 0);
    pad[0x18] = 0;
    pad[0x19] = 0;
}

static void pad_reset(uint8_t *pad)          /* the three phase bytes, then 001B62A0 */
{
    pad[0x10] = 0;
    pad[0x12] = 0;
    pad[0x11] = 0;
    em_slg_001B62A0(pad);
}

/* 001B5F40(out, pad). Pad block: +4 port, +8 slot, +0xC libpad state word,
 * +0x10 phase, +0x11 actuator flag, +0x12, +0x14 mode id (u16), +0x1E the
 * actuator bytes, +0x2A the mode id seen by the read. Returns 0, or what
 * 001B5940 returned when phase 4 reads the pad. */
int em_slg_001B5F40(const EmSlgPadWorkers *w, uint8_t *out, uint8_t *pad, int32_t *ret)
{
    if (!w || !out || !pad || !ret || !w->w_00110B80 || !w->w_00110E58 || !w->w_00110F60 ||
        !w->w_001110B0 || !w->w_001B5940) return -1;
    int32_t port = (int32_t)rd32(pad, 4), slot = (int32_t)rd32(pad, 8), v;
    *ret = 0;
    CALL(w->w_00110B80(w->ctx, port, slot, &v));
    wr32(pad, 0xC, (uint32_t)v);                                   /* 001B5F64 */
    if (rd32(pad, 0xC) == 0) pad_reset(pad);                       /* 001B5F6C: disconnected */

    int32_t state = (int32_t)rd32(pad, 0xC);
    switch (pad[0x10]) {
    case 4:                                                        /* 001B6134: read */
        wr16(pad, 0x2A, rd16(pad, 0x14));
        if (state == 6) return w->w_001B5940(w->ctx, out, pad, 1, ret) < 0 ? -1 : 0;
        if (state == 2) return w->w_001B5940(w->ctx, out, pad, 0, ret) < 0 ? -1 : 0;
        if (state == 7) pad_reset(pad);                            /* 001B618C */
        return 0;
    case 2:                                                        /* 001B6108 */
        if (state == 5 || state == 7) return 0;
        pad[0x10] = 4;
        pad[0x12] = 1;
        return 0;
    case 1:                                                        /* 001B60E0 */
        if (state == 5 || state == 7) return 0;
        pad[0x10] = 0;
        pad[0x11] = 1;
        return 0;
    case 0:                                                        /* 001B5FC0 */
        break;
    default:
        return 0;
    }
    if (state != 6 && state != 2) return 0;                        /* 001B5FC8 / 001B5FD0 */
    port = (int32_t)rd32(pad, 4); slot = (int32_t)rd32(pad, 8);
    int32_t id;
    CALL(w->w_00110E58(w->ctx, port, slot, 1, 0, &id));
    if (id == 0) return 0;                                         /* 001B5FF0 */
    port = (int32_t)rd32(pad, 4); slot = (int32_t)rd32(pad, 8);
    CALL(w->w_00110E58(w->ctx, port, slot, 2, 0, &v));
    if (v > 0) id = v;                                             /* 001B600C */
    wr16(pad, 0x14, (uint32_t)id);
    uint32_t mode = rd16(pad, 0x14);
    port = (int32_t)rd32(pad, 4); slot = (int32_t)rd32(pad, 8);
    if (mode == 4) {                                               /* 001B6024 */
        CALL(w->w_00110E58(w->ctx, port, slot, 4, -1, &v));
        if (v == 0) { pad[0x10] = 4; return 0; }                   /* 001B6040 */
        port = (int32_t)rd32(pad, 4); slot = (int32_t)rd32(pad, 8);
        CALL(w->w_00110F60(w->ctx, port, slot, 1, 3, &v));
        if (v == 1) pad[0x10] = 1;
        return 0;
    }
    if (mode != 7) return 0;                                       /* 001B607C */
    if (pad[0x11] == 0) {                                          /* 001B6084 */
        CALL(w->w_00110F60(w->ctx, port, slot, 1, 3, &v));
        if (v == 1) pad[0x10] = 1;
        return 0;
    }
    CALL(w->w_001110B0(w->ctx, port, slot, pad + 0x1E, &v));      /* 001B60B8 */
    if (v == 1) pad[0x10] = 2;
    return 0;
}

/* 001B57E0: 001B5F40(&D_00810E70, &D_00810E40); on 0 clear the six
 * halfwords D_00810E70..7A and recentre D_00810E64/65 to 0x80. */
int em_slg_001B57E0(const EmSlgPadWorkers *w, uint8_t block[EM_SLG_PAD_SIZE])
{
    if (!block) return -1;
    int32_t r;
    CALL(em_slg_001B5F40(w, block + EM_SLG_PAD_OUT, block, &r));
    if (r == 0) {
        for (unsigned at = 0x30; at <= 0x3A; at += 2) wr16(block, at, 0);
        block[0x24] = 0x80;
        block[0x25] = 0x80;
    }
    return 0;
}

/* ====================================================================== */
/* 001BB0E0                                                               */
/* ====================================================================== */

int em_slg_001BB0E0(const EmSlgScriptActorWorkers *w, const EmSlgScriptActor *a)
{
    if (!w || !a || !a->node || !a->entry || !a->owner || !w->w_001BAD40 || !w->w_001C5C90 ||
        !w->w_001C68C0 || !w->w_001BA580 || !w->w_001C64F0 || !w->w_001F9660 ||
        !w->w_001BA540 || !w->w_001AFC10 || !w->w_method_4C) return -1;
    uint8_t *node = a->node;
    const uint8_t *e = a->entry;
    int32_t r;
    switch (node[4]) {
    case 3:                                                        /* 001BB2E8 */
        return w->w_001AFC10(w->ctx, a) < 0 ? -1 : 0;
    case 2:                                                        /* 001BB2B8 */
        node[4] = 3;
        if ((int16_t)rd16(e, 4) == 0x270D) return 0;
        if ((int16_t)rd16(e, 0xA) != 0) return 0;
        return w->w_001BA540(w->ctx, a) < 0 ? -1 : 0;
    case 0:                                                        /* 001BB138 */
        CALL(w->w_001BAD40(w->ctx, a, &r));
        if (r != 0) return 0;
        node[4] = 1;
        /* fall through */
    case 1:                                                        /* 001BB150 */
        break;
    default:
        return 0;
    }
    if (rd16(a->owner, 0x2E) & (1u << (rd16(node, 0x2E) & 31))) { /* the owner's done mask */
        node[4] = 2;
        return 0;
    }
    int32_t key = (int16_t)rd16(e, 4);
    if (key == 0x270D || key == 0x270C) return 0;                  /* 001BB17C / 001BB188 */
    uint32_t dt = rd32(e, 0xC);
    switch ((int16_t)rd16(e, 0xA)) {
    case 6:                                                        /* 001BB248 */
        CALL(w->w_001C64F0(w->ctx, a, dt));
        CALL(w->w_001C68C0(w->ctx, a));
        CALL(w->w_001F9660(w->ctx, a, key));
        break;
    case 0:                                                        /* 001BB210 */
        CALL(w->w_001BA580(w->ctx, a, key));
        CALL(w->w_001C64F0(w->ctx, a, dt));
        CALL(w->w_001C68C0(w->ctx, a));
        break;
    case 3:
        return 0;
    case 4:                                                        /* 001BB1F0 */
        CALL(w->w_001C68C0(w->ctx, a));
        return w->w_method_4C(w->ctx, a) < 0 ? -1 : 0;
    case 5:                                                        /* 001BB1DC */
        return w->w_001C5C90(w->ctx, a) < 0 ? -1 : 0;
    default:                                                       /* 001BB288 */
        CALL(w->w_001C64F0(w->ctx, a, dt));
        CALL(w->w_001C68C0(w->ctx, a));
        break;
    }
    node[1] = 1;
    return w->w_method_4C(w->ctx, a) < 0 ? -1 : 0;
}

/* ====================================================================== */
/* 008237C0                                                               */
/* ====================================================================== */

void em_slg_008237C0(EmSlgOverlayInit *g)
{
    g->d275C28 = 0x20;
    g->d275C1C = EM_SLG_AREA11_RECORDS;
    g->d275C2C = 0;
    g->d275C24 = 0;
}

/* ====================================================================== */
/* 00199C50                                                               */
/* ====================================================================== */

static int coll_word(const EmSlgCollFile *f, uint32_t at, uint32_t *v)
{
    if (at + 4 > f->size) return -1;
    *v = rd32(f->bytes, at);
    return 0;
}

int em_slg_00199C50(const EmSlgCollFile *f, uint32_t spad[EM_SLG_COLL_SPAD_WORDS])
{
    if (!f || !f->bytes || !spad || f->size < 0x28) return -1;
    uint32_t base = f->address, h[6];
    static const uint8_t field[6] = { 0x00, 0x08, 0x10, 0x20, 0x18, 0x1C };
    for (int i = 0; i < 6; i++) CALL(coll_word(f, field[i], &h[i]));
    int32_t cell = (int16_t)rd16(f->bytes, 0x24);
#define S(addr) spad[((addr) - EM_SLG_COLL_SPAD_BASE) / 4]
    S(0x700031F8) = base;
    S(0x700031FC) = base + h[0];
    S(0x70003200) = base + h[1];
    S(0x70003204) = base + h[2];
    S(0x70003208) = base + h[3];
    S(0x7000320C) = (uint32_t)cell;
    uint32_t step = (uint32_t)cell << 1;                           /* *0x7000320C * 2 */
    S(0x70003210) = base + h[4];
    for (uint32_t a = 0x70003214; a <= 0x70003224; a += 4)          /* 00199CDC: 5 entries */
        S(a) = S(a - 4) + step;
    uint32_t last = S(0x70003224);
    for (int j = 0; j < 6; j++)                                    /* 00199D30 / 00199D64 */
        S(0x70003228 + 4 * j) = h[5] == 0xC ? last + (uint32_t)(j + 1) * step : 0;
    S(0x70003250) = f->d28A5A8;
    uint32_t low = (uint16_t)f->d28A5A8_value;                     /* sh at 0x7000324C */
    S(0x7000324C) = (S(0x7000324C) & 0xFFFF0000u) | low;
#undef S
    return 0;
}
