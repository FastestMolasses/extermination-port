/* em_player_weapon_states_a.c - the action machine 001607D0 and the stance
 * tops 0016FCF0 / 001703E0 / 001729A0 (see em_player_weapon_states_a.h).
 *
 * Read from the byte-matched decomp C (src/func_001607D0.c,
 * func_0016FCF0.c, func_001703E0.c, func_001729A0.c) and checked against
 * their instructions (build/asm) for the details the C leaves open: the
 * order of each store relative to the call whose delay slot holds it, the
 * bytes re-read after a call, and the operand order of every COP1 op.
 * Every address in a comment is the original instruction translated there.
 * tools/test_player_weapon_states_a_reference.py executes the original
 * instructions and compares every record byte, global, return value and
 * worker call. */
#include "game/em_player_weapon_states_a.h"
#include "game/em_ee_float.h"

#include <stddef.h>

#define FAULT(expr) do { if ((expr) < 0) return -1; } while (0)

/* Float constants as the instructions build them. */
#define F_HALF      UINT32_C(0x3F000000)   /* 0.5 */
#define F_4         UINT32_C(0x40800000)
#define F_8         UINT32_C(0x41000000)
#define F_20_5      UINT32_C(0x41A40000)
#define F_300       UINT32_C(0x43960000)
#define F_HALF_PI   UINT32_C(0x3FC90FDB)   /* 1.5707964 */
#define F_MINUS_0_2 UINT32_C(0xBE4CCCCD)
#define F_TURN_1D   UINT32_C(0x3DB2B8C3)   /* 0.08726647 (0016FCF0 state 0x66) */
#define F_TURN_1E1F UINT32_C(0x3D32B8C3)   /* 0.043633234 (001703E0 / 001729A0) */
#define F_RATE_015  UINT32_C(0x3C75C28F)   /* 0.015 */
#define F_RATE_010  UINT32_C(0x3C23D70A)   /* 0.01 */
#define F_RATE_025  UINT32_C(0x3CCCCCCD)   /* 0.025 */

static uint32_t w32(const EmPlayerLiveActor *a, unsigned at) { return em_live_u32(a, at); }
static void put32(EmPlayerLiveActor *a, unsigned at, uint32_t v) { em_live_set_u32(a, at, v); }
static uint8_t b8(const EmPlayerLiveActor *a, unsigned at) { return em_live_u8(a, at); }
static void put8(EmPlayerLiveActor *a, unsigned at, unsigned v) { em_live_set_u8(a, at, (uint8_t)v); }
static void put16(EmPlayerLiveActor *a, unsigned at, unsigned v) { em_live_set_u16(a, at, (uint16_t)v); }

/* ---- bound checks -------------------------------------------------------- */

static int action_bound(const EmPlayerWeaponWorkers *w)
{
    return w->w0016F5D0 && w->w0017A8B0 && w->w0017A970 && w->w0017AAD0 && w->w0017C370;
}

/* The workers every stance top reaches. */
static int stance_bound(const EmPlayerWeaponWorkers *w)
{
    return w->w0017B300 && w->w0016F530 && w->request && w->clip_id && w->skeleton &&
           w->matrix && w->bone && w->w0017ABA0 && w->w00170A60 && w->w00171320 &&
           w->w00171670 && w->w00171B00 && w->w00171E90 && w->w001723D0 && w->w0016F600 &&
           w->link20 && w->atan2 && w->wrap && w->approach && w->sound;
}

/* The exit states 0x6E / 0x6F and the standing tail (0016FCF0, 001703E0). */
static int tail_bound(const EmPlayerWeaponWorkers *w)
{
    return w->heading && w->reentry && w->handoff && w->translate && w->probes && w->floor &&
           w->fall_check;
}

static const EmPlayerWeaponWorkers *workers_of(const EmPlayerWeaponStates *s)
{
    return (s && s->scene && s->workers) ? s->workers : NULL;
}

int em_player_weapon_001607D0_bound(const EmPlayerWeaponStates *s)
{
    const EmPlayerWeaponWorkers *w = workers_of(s);
    return w && action_bound(w);
}

int em_player_weapon_0016FCF0_bound(const EmPlayerWeaponStates *s)
{
    const EmPlayerWeaponWorkers *w = workers_of(s);
    return w && stance_bound(w) && w->w00185A10 && w->w00185E30 && tail_bound(w);
}

int em_player_weapon_001703E0_bound(const EmPlayerWeaponStates *s)
{
    const EmPlayerWeaponWorkers *w = workers_of(s);
    return w && stance_bound(w) && w->w00199220 && tail_bound(w);
}

int em_player_weapon_001729A0_bound(const EmPlayerWeaponStates *s)
{
    const EmPlayerWeaponWorkers *w = workers_of(s);
    return w && stance_bound(w) && w->w00185A10 && w->w00185E30 && w->w00172860;
}

/* ---- 001607D0 ------------------------------------------------------------ */

/* The entries of +5 = 0x1D / 0x1E / 0x21 / 0x22 from +1F0 = 0 (without
 * 0017C370) and from +1F0 = 1..7 (with it, 0016097C..). */
static int action_enter(const EmPlayerWeaponWorkers *w, const EmPlayerWeaponScene *g,
                        EmPlayerLiveActor *a, int reset, int *result)
{
    if (g->d810E70 & g->spad3B7E) {                                     /* held 3B7E */
        if (b8(a, 0x236) != 0) return 0;
        if (reset) FAULT(w->w0017C370(w->context, a));
        put8(a, 5, 0x1E);
        put8(a, 6, 0);
        put8(a, 0x1F0, 0x32);
        put8(a, 0x1F1, 0);
        *result = 1;
        return 0;
    }
    if (g->d810E70 & g->spad3B7C) {                                     /* held 3B7C */
        if (b8(a, 0x236) != 0) return 0;
        if (reset) FAULT(w->w0017C370(w->context, a));
        put8(a, 5, 0x1D);
        put8(a, 6, 0);
        put8(a, 0x1F0, 0x31);
        put8(a, 0x1F1, 0);
        *result = 1;
        return 0;
    }
    if (g->d810E74 & g->spad3B78) {                                     /* pressed 3B78 */
        if (reset) FAULT(w->w0017C370(w->context, a));
        put8(a, 5, 0x21);
        put8(a, 6, 0);
        put8(a, 0x1F0, 0x36);
        *result = 1;
        return 0;
    }
    if (g->d810E74 & g->spad3B74) {                                     /* pressed 3B74 */
        if (reset) FAULT(w->w0017C370(w->context, a));
        put8(a, 5, 0x22);
        put8(a, 6, 0);
        put8(a, 0x1F0, 0x37);
        *result = 1;
        return 0;
    }
    return 0;
}

/* The forwarding every armed mode (0x31, 0x32, 0x34, 0x35) ends with. */
static int action_armed(const EmPlayerWeaponWorkers *w, const EmPlayerWeaponScene *g,
                        EmPlayerLiveActor *a, int *result)
{
    if (g->d810E74 & g->spad3B78) {                                     /* pressed 3B78 */
        if (g->d810C61 != 0) return 0;
        FAULT(w->w0017A8B0(w->context, a, 0, result));
        return 0;
    }
    if (g->d810E70 & g->spad3B78) {                                     /* held 3B78 */
        if (g->d810C61 == 0) return 0;
        FAULT(w->w0017A8B0(w->context, a, 0, result));
        return 0;
    }
    if (g->d810E74 & g->spad3B74) {                                     /* pressed 3B74 */
        FAULT(w->w0017A970(w->context, a, 0, result));
        return 0;
    }
    if (g->d810E70 & g->spad3B74) {                                     /* held 3B74 */
        FAULT(w->w0017A970(w->context, a, 1, result));
        return 0;
    }
    if (g->d810E74 & g->spad3B76) {                                     /* pressed 3B76 */
        int picked = 0;
        FAULT(w->w0017AAD0(w->context, a, &picked));
        *result = picked != 0;
    }
    return 0;
}

/* +6 = 0x63 and 0016F5D0: the stance exit (001607D0 cases 0x31/0x32/0x34/
 * 0x35 once their button is released). */
static int action_release(const EmPlayerWeaponWorkers *w, EmPlayerLiveActor *a, int *result)
{
    put8(a, 6, 0x63);
    FAULT(w->w0016F5D0(w->context, a));
    *result = 1;
    return 0;
}

int em_player_weapon_001607D0(void *context, EmPlayerLiveActor *a, int *result)
{
    const EmPlayerWeaponStates *s = context;
    if (!a || !result || !em_player_weapon_001607D0_bound(s)) return -1;
    const EmPlayerWeaponWorkers *w = s->workers;
    const EmPlayerWeaponScene *g = s->scene;
    const int r2 = (g->d810E70 & g->spad3B7E) != 0;   /* the words are read-only here */
    const int r1 = (g->d810E70 & g->spad3B7C) != 0;
    *result = 0;
    put32(a, 0x1FC, EM_EE_ONE);                                        /* 001607E0 */
    switch (b8(a, 0x1F0)) {                                            /* 001607E4 */
    case 0x00:
        return action_enter(w, g, a, 0, result);
    case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07:
        return action_enter(w, g, a, 1, result);
    case 0x31:
        if (r2) {
            put8(a, 5, 0x1E);
            put8(a, 0x1F0, 0x32);
            put8(a, 0x318, 1);
        } else if (!r1) {
            return action_release(w, a, result);
        }
        return action_armed(w, g, a, result);
    case 0x32:
        if (!r2) {
            if (r1) {
                put8(a, 5, 0x1D);
                put8(a, 0x1F0, 0x31);
                put8(a, 0x318, 1);
                return 0;
            }
            return action_release(w, a, result);
        }
        return action_armed(w, g, a, result);
    case 0x33:
        if (r1 || r2) return 0;
        *result = 1;
        return 0;
    case 0x27:
        if (r2) {
            put8(a, 5, 0x20);
            put8(a, 6, 0);
            put8(a, 0x1F0, 0x35);
            put8(a, 0x1F1, 0);
            *result = 1;
        } else if (r1) {
            put8(a, 5, 0x1F);
            put8(a, 6, 0);
            put8(a, 0x1F0, 0x34);
            put8(a, 0x1F1, 0);
            *result = 1;
        }
        return 0;
    case 0x34:
        if (r2) {
            put8(a, 5, 0x20);
            put8(a, 0x1F0, 0x35);
            put8(a, 0x318, 1);
            return 0;
        }
        if (!r1) return action_release(w, a, result);
        return action_armed(w, g, a, result);
    case 0x35:
        if (!r2) {
            if (r1) {
                put8(a, 5, 0x1F);
                put8(a, 0x1F0, 0x34);
                put8(a, 0x318, 1);
                return 0;
            }
            return action_release(w, a, result);
        }
        return action_armed(w, g, a, result);
    default:                                                           /* 0x36, 0x37, ... */
        return 0;
    }
}

/* ---- the stance tops: shared pieces --------------------------------------- */

typedef enum { TOP_1D, TOP_1E, TOP_1F } Top;

/* 001749A0(p, clip, 0, 0.0) with clip = D_00248B88[+275] for +5 = 0x1D /
 * 0x1E, else D_00248C68[+275] (0016FE34.. / 0017010C.. and the siblings). */
static int stance_clip(const EmPlayerWeaponWorkers *w, EmPlayerLiveActor *a)
{
    uint8_t character = b8(a, 5);
    uint32_t table = (character == 0x1D || character == 0x1E) ? EM_PLAYER_WEAPON_CLIPS_1D1E
                                                               : EM_PLAYER_WEAPON_CLIPS_OTHER;
    int16_t clip = 0;
    FAULT(w->clip_id(w->context, table, b8(a, 0x275), &clip));
    return w->request(w->context, a, clip, 0, 0.0f);
}

/* copy_qw4(p + 2A0, *(D_00275B40 + 0x10) + 0x90). */
static int copy_bone(const EmPlayerWeaponWorkers *w, EmPlayerLiveActor *a)
{
    uint32_t words[16];
    FAULT(w->bone(w->context, 4, words));
    for (unsigned i = 0; i < 16; ++i) put32(a, 0x2A0 + 4 * i, words[i]);
    return 0;
}

/* State 0: the stance entry (0016FD84.., 0017047C.., 00172A1C..). */
static int stance_enter(const EmPlayerWeaponStates *s, EmPlayerLiveActor *a, Top top)
{
    const EmPlayerWeaponWorkers *w = s->workers;
    FAULT(w->w0017B300(w->context, a, 0));
    if (b8(a, 0x317) == 0) {
        put8(a, 6, b8(a, 6) + 1);                                      /* re-read after 0017B300 */
        put32(a, 0x278, F_HALF);
        put8(a, 0x2F2, 0);
        put16(a, 0x2E, 0);
        put8(a, 0x275, 0);                                             /* the delay slot */
        if (top == TOP_1F) FAULT(w->request(w->context, a, 0x188, 0, em_ee_float(F_8)));
        else FAULT(w->request(w->context, a, 0x110, 0, em_ee_float(EM_EE_ONE)));
    } else {
        put8(a, 6, b8(a, 6) + 2);
        FAULT(w->w0016F530(w->context, a, 0));
    }
    put32(a, 0x27C, F_HALF);
    put8(a, 7, 0);
    if (top != TOP_1E) s->scene->d8106E0 = 0;                          /* 0016FE00, 00172A98 */
    put8(a, 0x302, 0);
    put16(a, 0x276, 0);
    if (top == TOP_1E) put8(a, 0x2F0, 0);                              /* 001704FC */
    put8(a, 0x274, 0);
    if (top == TOP_1F) {
        for (unsigned i = 0; i < 3; ++i)                               /* 001031E0(p+290, p+B0) */
            put32(a, 0x290 + 4 * i, w32(a, 0xB0 + 4 * i));
        put32(a, 0x294, em_ee_add_bits(w32(a, 0x294), F_20_5));        /* 00172AC4 */
        put32(a, 0x38, 0);
    }
    return 0;
}

/* State 1: wait for +200 & 0x1000, then the stance clip. 001703E0 runs the
 * skeleton and the bone copy every frame of the state (001705A0), the other
 * two only on the frame the bit is seen. */
static int stance_draw(const EmPlayerWeaponWorkers *w, EmPlayerLiveActor *a, uint8_t st,
                       Top top)
{
    int drawn = (w32(a, 0x200) & 0x1000) != 0;
    if (drawn) {
        put8(a, 6, st + 1);
        FAULT(w->w0016F530(w->context, a, 0));
        FAULT(stance_clip(w, a));
    }
    if (drawn || top == TOP_1E) {
        FAULT(w->skeleton(w->context, a));
        FAULT(copy_bone(w, a));
    }
    return 0;
}

/* D_008106E0 from 00185A10 (no target yet) or 00185E30 (keep / move the
 * lock), or 0 when `clear` (0016FEFC.., 00170018.., 00172BBC..). */
static int lock_update(const EmPlayerWeaponStates *s, EmPlayerLiveActor *a, int clear)
{
    const EmPlayerWeaponWorkers *w = s->workers;
    EmPlayerWeaponScene *g = s->scene;
    if (clear) {
        g->d8106E0 = 0;
        return 0;
    }
    uint32_t current = g->d8106E0, next = 0;
    if (current == 0) FAULT(w->w00185A10(w->context, a, current, &next));
    else FAULT(w->w00185E30(w->context, a, current, &next));
    g->d8106E0 = next;
    return 0;
}

/* +2F0 = (+2F0 + 1) as a byte, back to 0 from 3 (0016FF50.., 001705F0..). */
static void cycle_2F0(EmPlayerLiveActor *a)
{
    put8(a, 0x2F0, b8(a, 0x2F0) + 1);
    if (b8(a, 0x2F0) >= 3) put8(a, 0x2F0, 0);
}

/* The per-weapon handler by +275 (the jump tables at 0026D660 / 0026D680 /
 * 0026D6A0); +275 >= 6 calls nothing. */
static int stance_weapon(const EmPlayerWeaponWorkers *w, EmPlayerLiveActor *a, Top top)
{
    void *c = w->context;
    switch (b8(a, 0x275)) {
    case 0:
        return w->w00170A60(c, a, top == TOP_1E ? 1 : 0);
    case 1:
        FAULT(w->w00171320(c, a));
        return top == TOP_1F ? w->w00172860(c, a, em_ee_float(F_RATE_015)) : 0;
    case 2:
        FAULT(w->w00171670(c, a));
        return top == TOP_1F ? w->w00172860(c, a, em_ee_float(F_RATE_015)) : 0;
    case 3:
        FAULT(w->w00171B00(c, a));
        return top == TOP_1F ? w->w00172860(c, a, em_ee_float(F_RATE_010)) : 0;
    case 4:
        return w->w00171E90(c, a);
    case 5:
        FAULT(w->w001723D0(c, a));
        return top == TOP_1F ? w->w00172860(c, a, em_ee_float(F_RATE_025)) : 0;
    default:
        return 0;
    }
}

/* State 2: the stance itself. */
static int stance_hold(const EmPlayerWeaponStates *s, EmPlayerLiveActor *a, Top top)
{
    const EmPlayerWeaponWorkers *w = s->workers;
    if (top == TOP_1E) put16(a, 0x94, 7);                              /* 001705C8 */
    put8(a, 0x302, 0);                                                 /* the delay slot */
    FAULT(w->w0017ABA0(w->context, a));
    if (top == TOP_1E) {
        uint8_t equipped = s->scene->d810CA4;                          /* 001705D8 */
        if (equipped == 0) {
            if (b8(a, 0x274) != 0) cycle_2F0(a);
            FAULT(w->w00199220(w->context, a));
        } else if (equipped == 1) {
            FAULT(w->w00199220(w->context, a));
        }
    } else {
        FAULT(lock_update(s, a, b8(a, 0x302) != 0 || b8(a, 0x275) != 0));
        if (top == TOP_1D && s->scene->d810CA4 == 0 && b8(a, 0x274) != 0)  /* 0016FF38 */
            cycle_2F0(a);
    }
    return stance_weapon(w, a, top);
}

/* State 3: the holster (0016F600). */
static int stance_holster(const EmPlayerWeaponStates *s, EmPlayerLiveActor *a, Top top)
{
    const EmPlayerWeaponWorkers *w = s->workers;
    if (top == TOP_1E) {
        FAULT(w->w0016F600(w->context, a));
        if (b8(a, 0x1F0) == 0x33) put8(a, 1, 0);                       /* 001706E4 */
        return 0;
    }
    FAULT(lock_update(s, a, b8(a, 0x275) != 0));
    return w->w0016F600(w->context, a);
}

/* State 0x63: the blend back over `frames` ticks and the yaw toward the
 * object +20 points to (00170060.., 001706EC.., 00172D34..). */
static int stance_blend_start(const EmPlayerWeaponStates *s, EmPlayerLiveActor *a, uint8_t st,
                              unsigned frames, uint32_t divisor)
{
    const EmPlayerWeaponWorkers *w = s->workers;
    put8(a, 6, st + 1);
    put16(a, 0x28, frames);
    put32(a, 0x26C, em_ee_div_bits(em_ee_sub_bits(F_HALF, w32(a, 0x27C)), divisor));
    put32(a, 0x270, em_ee_div_bits(em_ee_sub_bits(F_HALF, w32(a, 0x278)), divisor));
    uint32_t c0 = 0, c8 = 0, angle = 0, yaw = 0;
    FAULT(w->link20(w->context, w32(a, 0x20), &c0, &c8));
    FAULT(w->atan2(w->context, em_ee_neg_bits(c8), c0, &angle));      /* 001700AC */
    s->scene->spad3A20 = angle;                                        /* 001700B8, reloaded */
    FAULT(w->wrap(w->context, em_ee_add_bits(F_HALF_PI, s->scene->spad3A20), &yaw));
    put32(a, 0x218, yaw);
    return 0;
}

/* State 0x64 (and 0x63 after its set-up): count +28 down, blending +27C /
 * +278 by +26C / +270; on the last tick the stance clip again. */
static int stance_blend(const EmPlayerWeaponWorkers *w, EmPlayerLiveActor *a)
{
    int16_t count = (int16_t)em_live_u16(a, 0x28);
    put16(a, 0x28, (uint16_t)(count - 1));
    if (count == 0) {
        put8(a, 6, b8(a, 6) + 1);
        put32(a, 0x27C, F_HALF);
        put32(a, 0x278, F_HALF);
        return stance_clip(w, a);
    }
    put32(a, 0x27C, em_ee_add_bits(w32(a, 0x27C), w32(a, 0x26C)));   /* 0017018C */
    put32(a, 0x278, em_ee_add_bits(w32(a, 0x278), w32(a, 0x270)));   /* 0017019C */
    if (b8(a, 0x1F0) == 0x33) return w->matrix(w->context, a);         /* 0017027C */
    FAULT(w->matrix(w->context, a));
    uint8_t mode = b8(a, 0x1F0);                                       /* re-read, 001701B8 */
    if (mode == 0x32 || mode == 0x35 || b8(a, 0x275) == 4 || b8(a, 0x2F2) != 0)
        return copy_bone(w, a);
    uint32_t words[16];
    FAULT(w->bone(w->context, 4, words));                              /* 00170248.. */
    put32(a, 0x2D0, words[12]);
    put32(a, 0x2D4, words[13]);
    put32(a, 0x2D8, words[14]);
    return 0;
}

/* State 0x65: the exit clip and sound (00170290.., 0017091C.., 00172F64..). */
static int stance_exit(const EmPlayerWeaponWorkers *w, EmPlayerLiveActor *a, uint8_t st, int clip)
{
    put8(a, 6, st + 1);
    put16(a, 0x276, 0);                                                /* the delay slot */
    FAULT(w->request(w->context, a, clip, 0, em_ee_float(EM_EE_ONE)));
    FAULT(w->sound(w->context, a, 0x163, 0, em_ee_float(F_300)));
    put8(a, 0x317, 0);
    return 0;
}

/* State 0x66 without its exit: turn +C4 toward +218. */
static int stance_turn(const EmPlayerWeaponWorkers *w, EmPlayerLiveActor *a, uint32_t rate)
{
    uint32_t yaw = 0;
    FAULT(w->approach(w->context, w32(a, 0x218), w32(a, 0xC4), rate, &yaw));
    put32(a, 0xC4, yaw);
    return 0;
}

/* States 0x6E / 0x6F: back to the walk (00170318.., 00170364..). */
static int stance_leave(const EmPlayerWeaponWorkers *w, EmPlayerLiveActor *a, uint8_t st)
{
    FAULT(w->heading(w->context, a, 1));
    if (st == 0x6F) {
        FAULT(w->translate(w->context, a, 0));
        if (!(w32(a, 0x200) & 0x8000)) return w->handoff(w->context, a);
        return 0;
    }
    if (b8(a, 0x23F) >= 2) {
        put8(a, 6, b8(a, 6) + 1);                                      /* re-read after 00174AC0 */
        return w->reentry(w->context, a, 0);
    }
    put8(a, 0x25C, 0);
    return w->handoff(w->context, a);
}

/* The standing tail (00170390.., 00170A0C..). */
static int stance_tail(const EmPlayerWeaponWorkers *w, EmPlayerLiveActor *a)
{
    int ignored = 0;
    FAULT(w->probes(w->context, a));
    put32(a, 0xB4, em_ee_add_bits(w32(a, 0xB4), F_MINUS_0_2));         /* 001703B0 */
    FAULT(w->floor(w->context, a, 1, &ignored));
    return w->fall_check(w->context, a);
}

/* ---- 0016FCF0 / 001703E0 / 001729A0 --------------------------------------- */

static int stance_top(const EmPlayerWeaponStates *s, EmPlayerLiveActor *a, Top top)
{
    const EmPlayerWeaponWorkers *w = s->workers;
    if (top == TOP_1E) s->scene->d8106E0 = 0;                          /* 001703F4 */
    uint8_t st = b8(a, 6);
    int tail = top != TOP_1F;
    switch (st) {
    case 0:
        FAULT(stance_enter(s, a, top));
        break;
    case 1:
        FAULT(stance_draw(w, a, st, top));
        break;
    case 2:
        FAULT(stance_hold(s, a, top));
        break;
    case 3:
        FAULT(stance_holster(s, a, top));
        break;
    case 0x63:
        if (top == TOP_1D) FAULT(stance_blend_start(s, a, st, 4, F_4));
        else FAULT(stance_blend_start(s, a, st, 8, F_8));
        FAULT(stance_blend(w, a));
        break;
    case 0x64:
        FAULT(stance_blend(w, a));
        break;
    case 0x65:
        FAULT(stance_exit(w, a, st, top == TOP_1F ? 0x189 : 0x111));
        break;
    case 0x66:
        if (top == TOP_1D) {
            if (em_ee_c_le_bits(w32(a, 0x3C), F_4)) put8(a, 6, 0x6E);  /* 001702DC */
            else FAULT(stance_turn(w, a, F_TURN_1D));
        } else if (w32(a, 0x200) & 0x1000) {
            if (top == TOP_1E) {
                put8(a, 6, 0x6E);
            } else {
                put8(a, 5, 0x14);                                      /* 00172FB4 */
                put8(a, 6, 0);
                put8(a, 0x1F0, 0x26);
            }
        } else {
            FAULT(stance_turn(w, a, F_TURN_1E1F));
        }
        break;
    case 0x6E:
    case 0x6F:
        if (top != TOP_1F) FAULT(stance_leave(w, a, st));
        break;
    default:
        break;
    }
    return tail ? stance_tail(w, a) : 0;
}

int em_player_weapon_state1D(void *context, EmPlayerLiveActor *a)
{
    const EmPlayerWeaponStates *s = context;
    if (!a || !em_player_weapon_0016FCF0_bound(s)) return -1;
    return stance_top(s, a, TOP_1D);
}

int em_player_weapon_state1E(void *context, EmPlayerLiveActor *a)
{
    const EmPlayerWeaponStates *s = context;
    if (!a || !em_player_weapon_001703E0_bound(s)) return -1;
    return stance_top(s, a, TOP_1E);
}

int em_player_weapon_state1F(void *context, EmPlayerLiveActor *a)
{
    const EmPlayerWeaponStates *s = context;
    if (!a || !em_player_weapon_001729A0_bound(s)) return -1;
    return stance_top(s, a, TOP_1F);
}
