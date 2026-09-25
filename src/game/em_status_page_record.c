/* em_status_page_record.c - 002149F0 (the BATTERY page) and 0020CD80 over
 * the original status records. See em_status_page_record.h and
 * docs/STATUS_PAGE_RECORD.md. Followed against the splat .s of 002149F0
 * (the NEARMISS C matches it except for register allocation). */
#include "game/em_status_page_record.h"

#define FN_PAGE 0x002149F0u

typedef struct {
    const EmSprWorkers *w;
    const EmSprRecords *r;
    EmSprFault *fault;
} Ctx;

static int fail(Ctx *c, uint32_t address, int32_t code)
{
    if (c->fault && c->fault->code == EM_SPR_FAULT_NONE) {
        c->fault->address = address;
        c->fault->code = code;
    }
    return -1;
}

/* ---- little-endian original fields ---------------------------------- */

static int32_t s16_at(const uint8_t *p)
{
    return (int16_t)(uint16_t)(p[0] | (unsigned)p[1] << 8);
}

static void put16(uint8_t *p, int32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)((uint32_t)v >> 8);
}

static uint32_t u32_at(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* D_00810CB2 as the halfword (lh) and as its low byte (lbu). */
static int32_t charge_h(const Ctx *c) { return s16_at(c->r->d810CB2); }
static uint8_t charge_b(const Ctx *c) { return c->r->d810CB2[0]; }
static void charge_set(const Ctx *c, int32_t v) { put16(c->r->d810CB2, v); }
static uint32_t pressed(const Ctx *c)
{
    return (uint32_t)c->r->d810E74[0] | (uint32_t)c->r->d810E74[1] << 8;
}

/* ---- workers --------------------------------------------------------- */

#define CALL(c, address, expr)                                                                  \
    do {                                                                                        \
        if ((expr) < 0) return fail((c), (address), EM_SPR_FAULT_WORKER_FAILED);                \
    } while (0)

/* The ring byte +0x50 + index: faults when it lies outside the page. */
static int ring_at(Ctx *c, uint32_t index, uint8_t **at)
{
    if (0x50u + index >= c->r->page_size) return fail(c, FN_PAGE, EM_SPR_FAULT_BAD_INPUT);
    *at = c->r->page + 0x50 + index;
    return 0;
}

static int owner_cost(Ctx *c, uint32_t owner, int32_t *cost)
{
    if (!owner) return fail(c, FN_PAGE, EM_SPR_FAULT_BAD_INPUT);
    CALL(c, FN_PAGE, c->w->owner_read(c->w->context, owner, 0x34, 2, cost));
    return 0;
}

/* The owner's completion: +0xA = 1, +0xB = 5 (after D_008106C5 = 0xFF),
 * then the mode byte 0x70003B8D = 3. */
static int owner_done(Ctx *c, uint32_t owner)
{
    if (!owner) return fail(c, FN_PAGE, EM_SPR_FAULT_BAD_INPUT);
    CALL(c, FN_PAGE, c->w->owner_write(c->w->context, owner, 0xA, 1));
    CALL(c, FN_PAGE, c->w->owner_write(c->w->context, owner, 0xB, 5));
    *c->r->spad3B8D = 3;
    return 0;
}

/* The message line of a cost confirmation: 4 -> 0xA, 6 -> 0xC, 16 -> 0xE,
 * 24 -> 0x10, anything else (2 included) -> 8. */
static uint8_t cost_line(int32_t cost)
{
    switch (cost) {
    case 4: return 0xA;
    case 6: return 0xC;
    case 16: return 0xE;
    case 24: return 0x10;
    default: return 8;
    }
}

/* 0020A7A0, 0020AE40(flags 2), 0020B210(list_flags), 0020B0D0: the four
 * page draws states 3..8 share (state 1 and 2 interleave their own). */
static int draws(Ctx *c, int32_t list_flags)
{
    const EmSprWorkers *w = c->w;
    uint8_t *p = c->r->page;
    int32_t result = 0;
    CALL(c, 0x0020A7A0u, w->background(w->context, EM_SPR_BACKGROUND_TEX0));
    CALL(c, 0x0020AE40u, w->frame(w->context, p, EM_SPR_FRAME_TABLE, 2));
    CALL(c, 0x0020B210u, w->list(w->context, p, EM_SPR_ROW_TABLE, EM_SPR_LIST_GLYPH, list_flags,
                                 &result));
    CALL(c, 0x0020B0D0u, w->arrows(w->context, p, EM_SPR_FRAME_TABLE));
    return 0;
}

/* ---- state 0: the request ------------------------------------------- */

/* Returns 1 when the request branch finished the call, 0 to fall into
 * state 1, -1 on a fault. */
static int state0(Ctx *c)
{
    const EmSprRecords *r = c->r;
    uint8_t *p = r->page;
    uint8_t *slot;
    p[0x17] = 0;
    p[0x19] = 0;
    p[0x18] = 0;
    p[0x1A] = 0;
    *r->d2821B0 = 4;
    *r->d2821B4 = 0;
    *r->d282240 = 3;
    put16(p + 0x1E, 0x1B);
    /* the highest owned battery kind is the only row */
    int kind = r->d810C7F[2] ? 2 : r->d810C7F[1] ? 1 : r->d810C7F[0] ? 0 : -1;
    if (kind >= 0) {
        uint8_t n = p[0x18];
        p[0x18] = (uint8_t)(n + 1);
        if (ring_at(c, n, &slot) < 0) return -1;
        *slot = (uint8_t)kind;
    }
    if (*r->d8106B0 == 0) {
        p[5] = (uint8_t)(p[5] + 1);
        return 0;
    }
    if (*r->d8106B0 == 6) {
        /* the panel's direct confirmation request */
        int32_t cost;
        p[0x12] = charge_b(c);
        p[0x13] = 8;
        uint32_t owner = u32_at(r->d8106D0);
        put32(p + 0x30, owner);
        if (owner_cost(c, owner, &cost) < 0) return -1;
        if (charge_h(c) < (int32_t)((uint32_t)cost << 1)) {
            p[5] = 5;
            p[6] = 0xF0;
        } else {
            p[5] = 4;
            p[6] = 1;
        }
        return 1;
    }
    uint8_t kind_byte = *r->d8106B1;
    if (kind_byte & 0x80) {
        /* a cost confirmation for the owner D_008106D0 */
        int32_t cost;
        uint32_t owner = u32_at(r->d8106D0);
        put32(p + 0x30, owner);
        *r->d8106B0 = 0;
        p[5] = 4;
        p[6] = 1;
        p[0x12] = charge_b(c);
        if (owner_cost(c, owner, &cost) < 0) return -1;
        p[0x13] = cost_line(cost);
        return 1;
    }
    if (kind_byte & 0x40) {
        /* the recharge path for the owner D_008106D0 */
        put32(p + 0x30, u32_at(r->d8106D0));
        *r->d8106B0 = 0;
        if (charge_h(c) == *r->d810CB7) {
            p[5] = 5;
            p[6] = 0x78;
        } else {
            p[5] = 4;
            p[6] = 1;
        }
        p[0x13] = 6;
        return 1;
    }
    /* the acquisition of battery item D_008106B1 */
    p[0x1B] = (uint8_t)(kind_byte - 0x1B);
    uint8_t rows = p[0x18];
    for (uint32_t i = 0; i < rows; i++) {
        if (ring_at(c, i, &slot) < 0) return -1;
        if (*slot == p[0x1B]) {
            p[0x17] = (uint8_t)i;
            *r->d282240 = 4;
            break;
        }
    }
    int32_t capacity = p[0x1B] == 0 ? 0xC : p[0x1B] == 1 ? 0x24 : 0x30;
    charge_set(c, capacity);
    *r->d810CB7 = (uint8_t)capacity;
    *r->d8106B0 = 0;
    p[5] = 3;
    p[6] = 0xF0;
    return 1;
}

/* ---- state 1: the list ---------------------------------------------- */

static int state1(Ctx *c)
{
    const EmSprWorkers *w = c->w;
    const EmSprRecords *r = c->r;
    uint8_t *p = r->page;
    int32_t result = 0;
    if (pressed(c) & 0x20) {
        /* back to the ITEM page */
        CALL(c, 0x0020CD60u, w->cue_back(w->context));
        *r->d2821B4 = 2;
        p[1] = 3;
        p[2] = 0;
        p[3] = 0;
        p[4] = 0;
        p[5] = 0;
        return 0;
    }
    CALL(c, 0x0020A7A0u, w->background(w->context, EM_SPR_BACKGROUND_TEX0));
    CALL(c, 0x0020AE40u, w->frame(w->context, p, EM_SPR_FRAME_TABLE, 2));
    CALL(c, 0x0020B210u, w->list(w->context, p, EM_SPR_ROW_TABLE, EM_SPR_LIST_GLYPH, 2, &result));
    if (result != 0) {
        put16(p + 0x1C, 0);
        p[5] = (uint8_t)(p[5] + 1);
        CALL(c, 0x0020BBE0u, w->list_refill(w->context, p, p[0x1A]));
    } else if (p[0x18] != 0 && (pressed(c) & 0x40)) {
        uint8_t *slot;
        uint32_t owner = 0;
        if (ring_at(c, p[0x17], &slot) < 0) return -1;
        CALL(c, 0x00185420u, w->find_device(w->context, (int32_t)*slot + 0x1B, &owner));
        put32(p + 0x30, owner);
        owner = u32_at(p + 0x30);
        if (owner != 0) {
            int32_t type, cost;
            CALL(c, 0x0020CD40u, w->cue_accept(w->context));
            owner = u32_at(p + 0x30);
            CALL(c, FN_PAGE, w->owner_read(w->context, owner, 3, 1, &type));
            if (type == 0x2C) {
                if (charge_h(c) == *r->d810CB7) {
                    p[5] = 5;
                    p[6] = 0x78;
                } else {
                    p[5] = 4;
                    p[6] = 1;
                }
                p[0x13] = 6;
            } else {
                p[5] = 4;
                p[6] = 1;
                owner = u32_at(p + 0x30);
                p[0x12] = charge_b(c);
                if (owner_cost(c, owner, &cost) < 0) return -1;
                p[0x13] = cost_line(cost);
            }
        } else {
            CALL(c, 0x0020CD80u, w->cue_refuse(w->context));
            *r->d2821B4 = 0;
            p[5] = 8;
            p[6] = 0xF0;
        }
    }
    CALL(c, 0x0020B0D0u, w->arrows(w->context, p, EM_SPR_FRAME_TABLE));
    return 0;
}

/* ---- states 2..8 ---------------------------------------------------- */

static int state2(Ctx *c)
{
    const EmSprWorkers *w = c->w;
    uint8_t *p = c->r->page;
    int32_t result = 0;
    CALL(c, 0x0020A7A0u, w->background(w->context, EM_SPR_BACKGROUND_TEX0));
    CALL(c, 0x0020AE40u, w->frame(w->context, p, EM_SPR_FRAME_TABLE, 2));
    CALL(c, 0x0020BC50u,
         w->list_scroll(w->context, p, EM_SPR_ROW_TABLE, EM_SPR_LIST_GLYPH, 2, &result));
    if (result != 0) p[5] = (uint8_t)(p[5] - 1);
    CALL(c, 0x0020B0D0u, w->arrows(w->context, p, EM_SPR_FRAME_TABLE));
    return 0;
}

/* The "no target" line 0x19, 240 frames or until 0x60. */
static int state8(Ctx *c)
{
    const EmSprRecords *r = c->r;
    uint8_t *p = r->page;
    *r->d2821B4 = 1;
    *r->d282240 = 5;
    *r->d2821B8 = 0x19;
    if (draws(c, 0x602) < 0) return -1;
    if (!(pressed(c) & 0x60)) {
        p[6] = (uint8_t)(p[6] - 1);
        if (p[6] != 0) return 0;
    }
    if (pressed(c) & 0x60) CALL(c, 0x0020CD60u, c->w->cue_back(c->w->context));
    *r->d2821B4 = 0;
    *r->d8106B0 = 0;
    *r->d282240 = 3;
    p[5] = 1;
    return 0;
}

/* The acquisition notice: 240 frames or input 0x5060. */
static int state3(Ctx *c)
{
    const EmSprRecords *r = c->r;
    uint8_t *p = r->page;
    if (draws(c, 0x402) < 0) return -1;
    p[6] = (uint8_t)(p[6] - 1);
    if (p[6] != 0 && !(pressed(c) & 0x5060)) return 0;
    if (pressed(c) & 0x5060) CALL(c, 0x0020CD60u, c->w->cue_back(c->w->context));
    *r->d8106B0 = 0;
    p[5] = 1;
    *r->d282240 = 3;
    return 0;
}

/* The Yes/No confirmation (+6: 1 = No, 0 = Yes). */
static int state4(Ctx *c)
{
    const EmSprWorkers *w = c->w;
    const EmSprRecords *r = c->r;
    uint8_t *p = r->page;
    if (draws(c, 0x402) < 0) return -1;
    *r->d2821B4 = 1;
    *r->d282240 = 5;
    *r->d2821B8 = p[0x13];
    CALL(c, 0x001FCF10u, w->message_line(w->context));
    CALL(c, 0x00207D00u, w->blend(w->context, 1, 3));
    uint32_t ev = pressed(c);
    if (ev & 0x8000) {
        if (p[6] != 0) {
            p[6] = (uint8_t)(p[6] - 1);
            CALL(c, 0x0020CDA0u, w->cue_cursor(w->context));
        }
    } else if (ev & 0x2000) {
        if (p[6] == 0) {
            p[6] = (uint8_t)(p[6] + 1);
            CALL(c, 0x0020CDA0u, w->cue_cursor(w->context));
        }
    }
    CALL(c, 0x0020CCB0u, w->marker(w->context, p));
    ev = pressed(c);
    if (ev & 0x40) {
        if (p[6] != 0) {
            /* No */
            *r->d2821B4 = 0;
            *r->d282240 = 3;
            p[5] = 1;
            CALL(c, 0x0020CD60u, w->cue_back(w->context));
            if (*r->d8106B0 == 6) *r->d8106C5 = 0xFF;
            return 0;
        }
        if (p[0x13] == 6) {
            /* Yes to a recharge */
            p[0x12] = charge_b(c);
            p[5] = 7;
            p[6] = 0;
            put16(p + 0x3C, 3);
        } else {
            int32_t cost;
            if (owner_cost(c, u32_at(p + 0x30), &cost) < 0) return -1;
            if (charge_h(c) < (int32_t)((uint32_t)cost << 1)) {
                p[5] = 5;
                p[6] = 0xF0;
                CALL(c, 0x0020CD60u, w->cue_back(w->context));
                return 0;
            }
            p[5] = 6;
            put16(p + 0x3C, 1);
        }
        if (*r->d8106B0 != 6) {
            *r->d8106B0 = 1;
        } else {
            /* the panel's direct request leaves the page (status phase 6) */
            *r->d2821B4 = 0;
            p[1] = 6;
            p[2] = 0;
            p[3] = 0;
            p[4] = 0;
            p[5] = 0;
        }
        CALL(c, 0x0020CD40u, w->cue_accept(w->context));
        return 0;
    }
    if (*r->d8106B0 == 6) {
        if (ev & 0x830) {
            *r->d2821B4 = 0;
            CALL(c, 0x0020CD60u, w->cue_back(w->context));
            *r->d8106C5 = 0xFF;
        }
        return 0;
    }
    if (ev & 0x20) {
        *r->d2821B4 = 0;
        *r->d282240 = 3;
        p[5] = 1;
        CALL(c, 0x0020CD60u, w->cue_back(w->context));
    }
    return 0;
}

/* The result line +0x13 + 1: released on 0x870 / 0x60 or the timer. */
static int state5(Ctx *c)
{
    const EmSprRecords *r = c->r;
    uint8_t *p = r->page;
    if (draws(c, 0x402) < 0) return -1;
    uint8_t request = *r->d8106B0;
    *r->d2821B4 = 1;
    *r->d282240 = 5;
    *r->d2821B8 = p[0x13] + 1;
    if (request == 6) {
        if (pressed(c) & 0x870) {
            *r->d2821B4 = 0;
            *r->d8106C5 = 0xFF;
            CALL(c, 0x0020CD60u, c->w->cue_back(c->w->context));
            return 0;
        }
        p[6] = (uint8_t)(p[6] - 1);
        if (p[6] == 0) {
            *r->d2821B4 = 0;
            *r->d8106C5 = 0xFF;
        }
        return 0;
    }
    if (pressed(c) & 0x60) {
        *r->d2821B4 = 0;
        *r->d282240 = 3;
        p[5] = 1;
        CALL(c, 0x0020CD60u, c->w->cue_back(c->w->context));
        return 0;
    }
    p[6] = (uint8_t)(p[6] - 1);
    if (p[6] == 0) {
        *r->d2821B4 = 0;
        *r->d282240 = 3;
        p[5] = 1;
    }
    return 0;
}

/* The discharge: every 30 frames 2 half-units (sound 6) until the charge
 * reaches +0x12 - 2 * cost; 0x870 finishes at once. */
static int state6(Ctx *c)
{
    const EmSprRecords *r = c->r;
    uint8_t *p = r->page;
    int32_t cost;
    if (draws(c, 0x402) < 0) return -1;
    *r->d2821B4 = 0;
    int32_t t = (int16_t)(uint16_t)(s16_at(p + 0x3C) - 1);
    put16(p + 0x3C, t);
    uint32_t owner = u32_at(p + 0x30);
    if (t == 0) {
        put16(p + 0x3C, 0x1E);
        if (owner_cost(c, owner, &cost) < 0) return -1;
        if (charge_h(c) == (int32_t)(p[0x12] - ((uint32_t)cost << 1))) {
            *r->d8106C5 = 0xFF;
            return owner_done(c, owner);
        }
        charge_set(c, charge_h(c) - 2);
        CALL(c, 0x001FB9F0u, c->w->sound(c->w->context, 6, 0x1000, 0x1000, 0x1000));
    }
    if (pressed(c) & 0x870) {
        if (owner_cost(c, owner, &cost) < 0) return -1;
        charge_set(c, (int32_t)(p[0x12] - ((uint32_t)cost << 1)));
        CALL(c, 0x0020CD40u, c->w->cue_accept(c->w->context));
        *r->d8106C5 = 0xFF;
        return owner_done(c, owner);
    }
    return 0;
}

/* The recharge: the mirror of state 6, up to the capacity D_00810CB7. */
static int state7(Ctx *c)
{
    const EmSprRecords *r = c->r;
    uint8_t *p = r->page;
    if (draws(c, 0x402) < 0) return -1;
    put16(p + 0x3C, s16_at(p + 0x3C) - 1);
    int32_t charge = charge_h(c);
    if (*r->d810CB7 == charge) {
        p[6] = 1;
    } else if (s16_at(p + 0x3C) == 0) {
        charge_set(c, charge + 2);
        CALL(c, 0x001FB9F0u, c->w->sound(c->w->context, 6, 0x1000, 0x1000, 0x1000));
    }
    *r->d2821B4 = 0;
    if (p[6] == 1 || (pressed(c) & 0x870)) {
        uint32_t owner = u32_at(p + 0x30);
        charge_set(c, *r->d810CB7);
        CALL(c, 0x0020CD40u, c->w->cue_accept(c->w->context));
        *r->d8106C5 = 0xFF;
        return owner_done(c, owner);
    }
    if (s16_at(p + 0x3C) == 0) put16(p + 0x3C, 0x14);
    return 0;
}

/* ---- entry ------------------------------------------------------------ */

int em_spr_002149F0(const EmSprWorkers *w, const EmSprRecords *r, EmSprFault *fault)
{
    Ctx c = {w, r, fault};
    if (fault) {
        fault->address = 0;
        fault->code = EM_SPR_FAULT_NONE;
    }
    if (!w || !r || !w->background || !w->frame || !w->list || !w->arrows || !w->list_refill ||
        !w->list_scroll || !w->marker || !w->cue_accept || !w->cue_back || !w->cue_refuse ||
        !w->cue_cursor || !w->message_line || !w->blend || !w->sound || !w->find_device ||
        !w->owner_read || !w->owner_write || !r->page || !r->d8106B0 || !r->d8106B1 ||
        !r->d8106C5 || !r->d8106D0 || !r->d810C7F || !r->d810CB2 || !r->d810CB7 ||
        !r->d810E74 || !r->d2821B0 || !r->d2821B4 || !r->d2821B8 || !r->d282240 ||
        !r->spad3B8D)
        return fail(&c, FN_PAGE, EM_SPR_FAULT_NULL);
    if (r->page_size < EM_SPR_PAGE_SIZE) return fail(&c, FN_PAGE, EM_SPR_FAULT_BAD_INPUT);
    switch (r->page[5]) {
    case 0: {
        int done = state0(&c);
        if (done != 0) return done < 0 ? -1 : 0;
        return state1(&c);
    }
    case 1: return state1(&c);
    case 2: return state2(&c);
    case 3: return state3(&c);
    case 4: return state4(&c);
    case 5: return state5(&c);
    case 6: return state6(&c);
    case 7: return state7(&c);
    case 8: return state8(&c);
    default: return 0; /* the jump table's range check: nothing happens */
    }
}

int em_spr_0020CD80(const EmSprWorkers *w)
{
    if (!w || !w->sound) return -1;
    return w->sound(w->context, 2, 0x1000, 0x1000, 0x1000) < 0 ? -1 : 0;
}
