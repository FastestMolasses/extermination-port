/* em_player_major2.c - the player's +4 = 2 states of the FLOOR closure and
 * their entry routines (see em_player_major2.h, docs/PLAYER_MAJOR2.md).
 *
 * Sources read:
 *   - byte-matched decomp C (ground truth): 0021E830, 00222580, 00222AD0,
 *     002230A0, 00225570 (mwcc 2.3.3 files), 00181D70, 001823E0;
 *   - the original instructions (build/asm) for the NEARMISS 00221FC0 and for
 *     the asm-word / hybrid-asm 00181110, 00181180 and 002255C0.
 * tools/test_player_major2_reference.py executes all of them against this
 * file. Addresses in the comments are the original instructions. */
#include "game/em_player_major2.h"
#include "game/em_ee_float.h"

#include <stddef.h>
#include <string.h>

#define CALL(expression) do { if ((expression) < 0) return -1; } while (0)

typedef EmPlayerMajor2Workers Workers;

static uint8_t u8(const EmPlayerLiveActor *a, unsigned at) { return em_live_u8(a, at); }
static void set8(EmPlayerLiveActor *a, unsigned at, unsigned v) { em_live_set_u8(a, at, (uint8_t)v); }
static uint32_t u32(const EmPlayerLiveActor *a, unsigned at) { return em_live_u32(a, at); }
static void set32(EmPlayerLiveActor *a, unsigned at, uint32_t v) { em_live_set_u32(a, at, v); }

/* The equal / less-or-equal / less-than tests of a record float against an
 * immediate, as the EE's float compares (DAZ, sign-keeping saturation). */
static int f_eq(const EmPlayerLiveActor *a, unsigned at, float k)
{
    return em_ee_c_eq_bits(u32(a, at), em_ee_bits(k));
}
static int f_le(const EmPlayerLiveActor *a, unsigned at, float k)
{
    return em_ee_c_le_bits(u32(a, at), em_ee_bits(k));
}
static int f_lt(const EmPlayerLiveActor *a, unsigned at, float k)
{
    return em_ee_c_lt_bits(u32(a, at), em_ee_bits(k));
}

static int context_of(void *context, EmPlayerLiveActor *a, const Workers **w,
                      EmPlayerMajor2Scene **scene)
{
    const EmPlayerMajor2 *m = context;
    if (!m || !m->workers || !m->scene || !m->scene->d8106F1 || !m->scene->d810707 || !a)
        return -1;
    *w = m->workers;
    *scene = m->scene;
    return 0;
}

/* The shared state-0 prologue of 00221FC0 / 00222580 / 00222AD0 / 002230A0
 * up to the knock-down test: the +224 and +22C hit cues. */
static int hit_cues(EmPlayerLiveActor *a, const Workers *w)
{
    void *c = w->context;
    if (!f_eq(a, 0x224, 0.0f)) {               /* float +224 != 0.0 */
        CALL(w->sound(c, a, 0x152, 0, 300.0f));
        CALL(w->w0021C350(c, a));
    }
    if (!f_eq(a, 0x22C, 0.0f)) {               /* float +22C != 0.0 */
        CALL(w->sound(c, a, 0x153, 0, 300.0f));
        CALL(w->w0021C270(c, a));
    }
    return 0;
}

/* The shared root-motion sub-state (00221FC0 0xC, 00222580/00222AD0 0xB,
 * 002230A0 0xC) after its 0x1000 test:
 *   +38 = node+8 - +21C; +21C = node+8; 00178B90(p, 1);
 *   +2EC = node+4 - +2E4; +2E4 = node+4; +B4 = +B4 + +2EC; 00175900(p, 1).
 * node is *(*D_00275B40); each word is loaded where the original loads it. */
static int root_motion(EmPlayerLiveActor *a, const Workers *w)
{
    void *c = w->context;
    uint32_t node;
    int ignored;
    CALL(w->root_node(c, 8, &node));
    set32(a, 0x38, em_ee_sub_bits(node, u32(a, 0x21C)));     /* +38 = node8 - +21C */
    CALL(w->root_node(c, 8, &node));
    set32(a, 0x21C, node);
    CALL(w->translate(c, a, 1));
    CALL(w->root_node(c, 4, &node));
    set32(a, 0x2EC, em_ee_sub_bits(node, u32(a, 0x2E4)));    /* +2EC = node4 - +2E4 */
    CALL(w->root_node(c, 4, &node));
    set32(a, 0x2E4, node);
    set32(a, 0xB4, em_ee_add_bits(u32(a, 0xB4), u32(a, 0x2EC)));   /* +B4 = +B4 + +2EC */
    CALL(w->floor(c, a, 1, &ignored));
    return 0;
}

/* The shared landing sub-state (00221FC0 0xD, 00222580/00222AD0 0xC,
 * 002230A0 0xD): 00179880, then 00175900(p, 1); on a floor, 00182870(p, 1),
 * 001FBD50(0x156) and either the 2/3 reset (+F 0x63 or +234 1) or the next
 * sub-state with clip 0x2A and +1F0 0x40. `second_sound` is 00221FC0's
 * second 001FBD50(0x156) after +1F0 = 0x40; `surface5d` is the +23A 0x5D
 * test when no floor was found (absent in 00222580). */
static int landing(EmPlayerLiveActor *a, const Workers *w, int second_sound, int surface5d)
{
    void *c = w->context;
    int floor;
    CALL(w->drop(c, a));
    CALL(w->floor(c, a, 1, &floor));
    if (floor != 0) {
        CALL(w->land_sound(c, a, 1));
        CALL(w->sound(c, a, 0x156, 0, 300.0f));
        if (u8(a, 0xF) == 0x63 || u8(a, 0x234) == 1) {
            set8(a, 4, 2);
            set8(a, 5, 3);
            set8(a, 6, 0);
            set8(a, 0x1F0, 0x3F);
            return 0;
        }
        set8(a, 6, u8(a, 6) + 1);
        CALL(w->request(c, a, 0x2A, 0, 1.0f));
        set8(a, 0x1F0, 0x40);
        if (second_sound) CALL(w->sound(c, a, 0x156, 0, 300.0f));
        return 0;
    }
    if (surface5d && u8(a, 0x23A) == 0x5D) CALL(w->w0021D250(c, a, 0));
    return 0;
}

/* ---- 0021E830: +4 2 / +5 3 ------------------------------------------- */

int em_player_major2_0021E830(void *context, EmPlayerLiveActor *a)
{
    const Workers *w;
    EmPlayerMajor2Scene *scene;
    if (context_of(context, a, &w, &scene) < 0) return -1;
    if (!w->request || !w->w001EFE00 || !w->cue || !w->sound || !w->w0015C1F0 ||
        !w->w0021E650 || !w->w0021D2E0 || !w->drop || !w->floor)
        return -1;
    void *c = w->context;
    int ignored;
    uint8_t st = u8(a, 6);
    switch (st) {
    case 0:
        set8(a, 6, st + 1);
        set8(a, 7, 0);
        CALL(w->request(c, a, 0x1C4, 0, 0.0f));
        CALL(w->w001EFE00(c, 0x80000051u, a));
        CALL(w->cue(c, 0, 0xC0, 5, 1));
        CALL(w->sound(c, a, 0x146, 0, 300.0f));
        CALL(w->sound(c, a, 0x151, 0, 300.0f));
        set32(a, 0x2EC, 0);
        break;
    case 1:
        if (f_le(a, 0x3C, 160.0f)) {
            set8(a, 6, st + 1);
            set8(a, 0x234, 2);
            *scene->d810707 = 2;
            CALL(w->w0015C1F0(c, a));
        }
        CALL(w->w0021E650(c, a));
        break;
    case 2:
        if (u32(a, 0x200) & 0x1000) {
            set8(a, 6, st + 1);
            set8(a, 7, 0);
            CALL(w->cue(c, 1, 0xEE, 0x3C, 1));
        } else {
            CALL(w->w0021E650(c, a));
        }
        break;
    case 3:
        CALL(w->w0021D2E0(c, a, 0x78, 1));
        break;
    }
    /* Every sub-state, the others included: 00179880(p, p+2EC), 00175900(p, 1). */
    CALL(w->drop(c, a));
    CALL(w->floor(c, a, 1, &ignored));
    return 0;
}

/* ---- 00221FC0: +4 2 / +5 4 (read from the instructions; NEARMISS C) ---- */

int em_player_major2_00221FC0(void *context, EmPlayerLiveActor *a)
{
    const Workers *w;
    EmPlayerMajor2Scene *scene;
    if (context_of(context, a, &w, &scene) < 0) return -1;
    if (!w->cue || !w->sound || !w->w0021C350 || !w->w0021C270 || !w->w0021C200 ||
        !w->request || !w->w0017FC80 || !w->root_node || !w->translate || !w->floor ||
        !w->drop || !w->land_sound || !w->w0021D250 || !w->w0021D490 || !w->w0021D2E0 ||
        !w->w0021C120 || !w->w0021C190)
        return -1;
    void *c = w->context;
    uint8_t st = u8(a, 6);
    switch (st) {
    case 0: {                                          /* 0022206C */
        CALL(w->cue(c, 0, 0xC0, 5, 1));
        set8(a, 6, u8(a, 6) + 1);
        set8(a, 7, 0);
        int alt = u8(a, 0x302);                        /* $s0 */
        if (u8(a, 0xF) & 2) {
            alt = 1;
            set8(a, 0xF, 0);
        }
        CALL(hit_cues(a, w));
        if (f_le(a, 0x220, 0.0f)) {                    /* 00222118 */
            CALL(w->w0021C200(c, a));
            set8(a, 6, 0xA);
            return 0;
        }
        if (!f_lt(a, 0x228, 100.0f) && *scene->d8106F1 != 0) {   /* 00222148 */
            set8(a, 6, 0x14);
            alt = 0;
        }
        int clip;                                      /* 00222184 */
        if (u8(a, 0x2F1) == 0) clip = alt == 0 ? 0x102 : 0x104;
        else clip = alt == 0 ? 0x103 : 0x105;
        CALL(w->request(c, a, clip, 0, 8.0f));
        set32(a, 0xB4, u32(a, 0x294));                 /* 0022221C: +B4 = +294 (float copy) */
        return 0;
    }
    case 1:                                            /* 00222224 */
        if (u32(a, 0x200) & 0x1000) {
            set8(a, 4, 1);
            set8(a, 5, 0xC);
            set8(a, 6, 0);
            em_live_set_u16(a, 0x20E, 0x3C);
            CALL(w->w0017FC80(c, a, 16.0f));
        }
        return 0;
    case 0xA:                                          /* 00222264 */
        set8(a, 6, st + 1);
        CALL(w->request(c, a, 0x106, 0, 8.0f));
        set32(a, 0x21C, 0);
        set32(a, 0x2E4, 0);
        return 0;
    case 0xB:                                          /* 00222288 */
        if (!(u32(a, 0x200) & 0x8000)) set8(a, 6, st + 1);
        return 0;
    case 0xC:                                          /* 002222A4 */
        if (u32(a, 0x200) & 0x1000) {
            set8(a, 6, st + 1);
            return 0;
        }
        return root_motion(a, w);
    case 0xD:                                          /* 00222340 */
        return landing(a, w, 1, 1);
    case 0xE:                                          /* 00222430: 20.0 */
        if (f_le(a, 0x3C, 20.0f)) {
            set8(a, 6, st + 1);
            CALL(w->w0021D490(c, a));
        }
        return 0;
    case 0xF:                                          /* 00222464 */
        if (u32(a, 0x200) & 0x1000) {
            set8(a, 6, st + 1);
            set8(a, 7, 0);
            CALL(w->cue(c, 1, 0xEE, 0x3C, 1));
        }
        return 0;
    case 0x10:                                         /* 002224A0 */
        CALL(w->w0021D2E0(c, a, 0x78, 0));
        return 0;
    case 0x14:                                         /* 002224B0: 21.0 */
        if (f_le(a, 0x3C, 21.0f)) {
            set8(a, 6, st + 1);
            CALL(w->w0021C120(c, a));
        }
        return 0;
    case 0x15: {                                       /* 002224E4 */
        int done;
        CALL(w->w0021C190(c, a, &done));
        if (done != 0) set8(a, 6, u8(a, 6) + 1);
        else set32(a, 0x204, 0x3DCCCCCDu);             /* 0.1 */
        return 0;
    }
    case 0x16:                                         /* 00222514 */
        if (u32(a, 0x200) & 0x1000) {
            set8(a, 4, 1);
            set8(a, 5, 0xC);
            set8(a, 6, 0);
            em_live_set_u16(a, 0x20E, 0x3C);
            CALL(w->w0017FC80(c, a, 16.0f));
            return 0;
        }
        set32(a, 0x204, 0x3E800000u);                  /* 0.25 */
        return 0;
    }
    return 0;
}

/* ---- 00222580: +4 2 / +5 5 --------------------------------------------- */

int em_player_major2_00222580(void *context, EmPlayerLiveActor *a)
{
    const Workers *w;
    EmPlayerMajor2Scene *scene;
    if (context_of(context, a, &w, &scene) < 0) return -1;
    if (!w->cue || !w->sound || !w->w0021C350 || !w->w0021C270 || !w->request ||
        !w->w0017FF80 || !w->root_node || !w->translate || !w->floor || !w->drop ||
        !w->land_sound || !w->w0021D490 || !w->w0021D2E0 || !w->w0021C120 || !w->w0021C190)
        return -1;
    void *c = w->context;
    uint8_t st = u8(a, 6);
    switch (st) {
    case 0: {
        CALL(w->cue(c, 0, 0xC0, 5, 1));
        set8(a, 6, u8(a, 6) + 1);
        set8(a, 7, 0);
        int alt = u8(a, 0x302);
        if (u8(a, 0xF) & 2) {
            alt = 1;
            set8(a, 0xF, 0);
        }
        CALL(hit_cues(a, w));
        if (f_le(a, 0x220, 0.0f)) {                    /* no 0021C200 here */
            set8(a, 6, 0xA);
            return 0;
        }
        if (!f_lt(a, 0x228, 100.0f) && *scene->d8106F1 != 0) {
            set8(a, 6, 0x14);
            alt = 0;
        }
        int clip;
        if (u8(a, 0x2F1) == 0) clip = alt == 0 ? 0xB5 : 0xB7;
        else clip = alt == 0 ? 0xB6 : 0xB8;
        CALL(w->request(c, a, clip, 0, 8.0f));
        set32(a, 0xB4, u32(a, 0x294));
        return 0;
    }
    case 1:
        if (u32(a, 0x200) & 0x1000) {
            set8(a, 4, 1);
            set8(a, 5, 0xE);
            set8(a, 6, 0);
            em_live_set_u16(a, 0x20E, 0x3C);
            CALL(w->w0017FF80(c, a, 16.0f));
        }
        return 0;
    case 0xA:
        set8(a, 6, st + 1);
        CALL(w->request(c, a, 0xB9, 0, 1.0f));
        set32(a, 0x21C, 0);
        set32(a, 0x2E4, 0);
        return 0;
    case 0xB:
        if (u32(a, 0x200) & 0x1000) {
            set8(a, 6, st + 1);
            return 0;
        }
        return root_motion(a, w);
    case 0xC:
        return landing(a, w, 0, 0);
    case 0xD:
        if (f_le(a, 0x3C, 20.0f)) {
            set8(a, 6, st + 1);
            CALL(w->w0021D490(c, a));
        }
        return 0;
    case 0xE:
        if (u32(a, 0x200) & 0x1000) {
            set8(a, 6, st + 1);
            set8(a, 7, 0);
            CALL(w->cue(c, 1, 0xEE, 0x3C, 1));
        }
        return 0;
    case 0xF:
        CALL(w->w0021D2E0(c, a, 0x78, 0));
        return 0;
    case 0x14:
        if (f_le(a, 0x3C, 8.0f)) {
            set8(a, 6, st + 1);
            CALL(w->w0021C120(c, a));
        }
        return 0;
    case 0x15: {
        int done;
        CALL(w->w0021C190(c, a, &done));
        if (done != 0) set8(a, 6, u8(a, 6) + 1);
        else set32(a, 0x204, 0x3DCCCCCDu);
        return 0;
    }
    case 0x16:
        if (u32(a, 0x200) & 0x1000) {
            set8(a, 4, 1);
            set8(a, 5, 0xE);
            set8(a, 6, 0);
            em_live_set_u16(a, 0x20E, 0x3C);
            CALL(w->w0017FF80(c, a, 16.0f));
            return 0;
        }
        set32(a, 0x204, 0x3E800000u);
        return 0;
    }
    return 0;
}

/* ---- 00222AD0: +4 2 / +5 6 --------------------------------------------- */

/* Sub-states 1 and 0x16 on 0x1000: 1/9 (+1F0 0x10) when +302 is 9, else
 * 1/0x18 (+1F0 0x2C); then 001749A0(p, 00188550(p), 0, 16.0), +20E = 0x3C. */
static int exit_00222AD0(EmPlayerLiveActor *a, const Workers *w)
{
    void *c = w->context;
    set8(a, 4, 1);
    if (u8(a, 0x302) == 9) {
        set8(a, 5, 9);
        set8(a, 6, 0);
        set8(a, 0x1F0, 0x10);
    } else {
        set8(a, 5, 0x18);
        set8(a, 6, 0);
        set8(a, 0x1F0, 0x2C);
    }
    int clip;
    CALL(w->w00188550(c, a, &clip));
    CALL(w->request(c, a, clip, 0, 16.0f));
    em_live_set_u16(a, 0x20E, 0x3C);
    return 0;
}

int em_player_major2_00222AD0(void *context, EmPlayerLiveActor *a)
{
    const Workers *w;
    EmPlayerMajor2Scene *scene;
    if (context_of(context, a, &w, &scene) < 0) return -1;
    if (!w->cue || !w->random || !w->sound || !w->w0021C350 || !w->w0021C270 ||
        !w->w0021C200 || !w->request || !w->w00188550 || !w->root_node || !w->translate ||
        !w->floor || !w->drop || !w->land_sound || !w->w0021D250 || !w->w0021D490 ||
        !w->w0021D2E0 || !w->w0021C120 || !w->w0021C190)
        return -1;
    void *c = w->context;
    uint8_t st = u8(a, 6);
    switch (st) {
    case 0: {
        CALL(w->cue(c, 0, 0xC0, 5, 1));
        set8(a, 6, u8(a, 6) + 1);
        set8(a, 7, 0);
        uint32_t value;
        CALL(w->random(c, &value));
        int alt = (int)(value & 1);
        if (u8(a, 0xF) & 2) {
            alt = 1;
            set8(a, 0xF, 0);
        }
        CALL(hit_cues(a, w));
        if (f_le(a, 0x220, 0.0f)) {
            CALL(w->w0021C200(c, a));
            set8(a, 6, 0xA);
            return 0;
        }
        if (!f_lt(a, 0x228, 100.0f) && *scene->d8106F1 != 0) {
            set8(a, 6, 0x14);
            alt = 0;
        }
        int clip;
        if (u8(a, 0x234) == 0) clip = alt == 0 ? 0x8F : 0x90;
        else clip = 0x1C8;
        CALL(w->request(c, a, clip, 0, 1.0f));
        return 0;
    }
    case 1:
        if (u32(a, 0x200) & 0x1000) return exit_00222AD0(a, w);
        return 0;
    case 0xA:
        set8(a, 6, st + 1);
        CALL(w->request(c, a, 0x91, 0, 1.0f));
        set32(a, 0x21C, 0);
        set32(a, 0x2E4, 0);
        return 0;
    case 0xB:
        if (u32(a, 0x200) & 0x1000) {
            set8(a, 6, st + 1);
            return 0;
        }
        return root_motion(a, w);
    case 0xC:
        return landing(a, w, 0, 1);
    case 0xD:
        if (f_le(a, 0x3C, 20.0f)) {
            set8(a, 6, st + 1);
            CALL(w->w0021D490(c, a));
        }
        return 0;
    case 0xE:
        if (u32(a, 0x200) & 0x1000) {
            set8(a, 6, st + 1);
            set8(a, 7, 0);
            CALL(w->cue(c, 1, 0xEE, 0x3C, 1));
        }
        return 0;
    case 0xF:
        CALL(w->w0021D2E0(c, a, 0x78, 0));
        return 0;
    case 0x14:
        if (f_le(a, 0x3C, 35.0f)) {
            set8(a, 6, st + 1);
            CALL(w->w0021C120(c, a));
        }
        return 0;
    case 0x15: {
        int done;
        CALL(w->w0021C190(c, a, &done));
        if (done != 0) set8(a, 6, u8(a, 6) + 1);
        else set32(a, 0x204, 0x3DCCCCCDu);
        return 0;
    }
    case 0x16:
        if (u32(a, 0x200) & 0x1000) return exit_00222AD0(a, w);
        set32(a, 0x204, 0x3E800000u);
        return 0;
    }
    return 0;
}

/* ---- 002230A0: +4 2 / +5 7 --------------------------------------------- */

int em_player_major2_002230A0(void *context, EmPlayerLiveActor *a)
{
    const Workers *w;
    EmPlayerMajor2Scene *scene;
    if (context_of(context, a, &w, &scene) < 0) return -1;
    if (!w->cue || !w->sound || !w->w0021C350 || !w->w0021C270 || !w->request ||
        !w->w001885B0 || !w->root_node || !w->translate || !w->floor || !w->drop ||
        !w->land_sound || !w->w0021D250 || !w->w0021D490 || !w->w0021D2E0 ||
        !w->w0021C120 || !w->w0021C190)
        return -1;
    void *c = w->context;
    uint8_t st = u8(a, 6);
    switch (st) {
    case 0: {
        CALL(w->cue(c, 0, 0xC0, 5, 1));
        set8(a, 6, u8(a, 6) + 1);
        set8(a, 7, 0);
        uint8_t sub = u8(a, 0x2F1);
        int pose = sub == 0 ? 0 : sub == 1 ? 1 : 2;
        if (u8(a, 0xF) & 2) {
            if (pose == 0) pose = 1;
            set8(a, 0xF, 0);
        }
        CALL(hit_cues(a, w));
        if (f_le(a, 0x220, 0.0f)) {                    /* no 0021C200 here */
            set8(a, 6, 0xA);
            return 0;
        }
        if (!f_lt(a, 0x228, 100.0f) && *scene->d8106F1 != 0) {
            set8(a, 6, 0x14);
            pose = 0;
        }
        int clip;
        if (pose == 0) clip = u8(a, 0x234) == 0 ? 0xDD : 0x1C9;
        else clip = pose == 1 ? 0xDF : 0xE0;
        CALL(w->request(c, a, clip, 0, 8.0f));
        set32(a, 0xB0, u32(a, 0x290));
        set32(a, 0xB8, u32(a, 0x298));
        return 0;
    }
    case 1:
        if (u32(a, 0x200) & 0x1000) {
            set8(a, 4, 1);
            if (scene->d275B14 == 0x1E) {
                set8(a, 5, 0x12);
                set8(a, 6, 0);
            } else if (scene->d275B14 == 0x36) {
                set8(a, 5, 0x12);
                set8(a, 6, 0x28);
            } else {
                set8(a, 5, 0x10);
                set8(a, 6, 0);
            }
            em_live_set_u16(a, 0x20E, 0x3C);
            set8(a, 0x2F1, 0);
            int clip;
            CALL(w->w001885B0(c, a, &clip));
            CALL(w->request(c, a, clip, 0, 16.0f));
        }
        return 0;
    case 0xA:
        set8(a, 6, st + 1);
        CALL(w->request(c, a, 0xDE, 0, 8.0f));
        set32(a, 0x21C, 0);
        set32(a, 0x2E4, 0);
        return 0;
    case 0xB:
        if (!(u32(a, 0x200) & 0x8000)) set8(a, 6, st + 1);
        return 0;
    case 0xC:
        if (u32(a, 0x200) & 0x1000) {
            set8(a, 6, st + 1);
            return 0;
        }
        return root_motion(a, w);
    case 0xD:
        return landing(a, w, 0, 1);
    case 0xE:
        if (f_le(a, 0x3C, 20.0f)) {
            set8(a, 6, st + 1);
            CALL(w->w0021D490(c, a));
        }
        return 0;
    case 0xF:
        if (u32(a, 0x200) & 0x1000) {
            set8(a, 6, st + 1);
            set8(a, 7, 0);
            CALL(w->cue(c, 1, 0xEE, 0x3C, 1));
        }
        return 0;
    case 0x10:
        CALL(w->w0021D2E0(c, a, 0x78, 0));
        return 0;
    case 0x14:
        if (f_le(a, 0x3C, 8.0f)) {
            set8(a, 6, st + 1);
            CALL(w->w0021C120(c, a));
        }
        return 0;
    case 0x15: {
        int done;
        CALL(w->w0021C190(c, a, &done));
        if (done != 0) set8(a, 6, u8(a, 6) + 1);
        else set32(a, 0x204, 0x3DCCCCCDu);
        return 0;
    }
    case 0x16:
        if (u32(a, 0x200) & 0x1000) {
            set8(a, 4, 1);
            set8(a, 6, 0);
            set8(a, 5, scene->d275B14 == 0x1E ? 0x12 : 0x10);
            em_live_set_u16(a, 0x20E, 0x3C);
            int clip;
            CALL(w->w001885B0(c, a, &clip));
            CALL(w->request(c, a, clip, 0, 16.0f));
            return 0;
        }
        set32(a, 0x204, 0x3E800000u);
        return 0;
    }
    return 0;
}

/* ---- 00225570: +4 2 / +5 0x16 ------------------------------------------ */

int em_player_major2_00225570(void *context, EmPlayerLiveActor *a)
{
    const Workers *w;
    EmPlayerMajor2Scene *scene;
    if (context_of(context, a, &w, &scene) < 0) return -1;
    if (!w->w0021D2E0) return -1;
    switch (u8(a, 6)) {
    case 0:
        set8(a, 6, u8(a, 6) + 1);
        set8(a, 7, 0);
        /* fall through */
    case 1:
        CALL(w->w0021D2E0(w->context, a, 0x78, 0));
        break;
    }
    return 0;
}

/* ---- 002255C0: +4 2 / +5 0x19 (read from the instructions) ------------- */

int em_player_major2_002255C0(void *context, EmPlayerLiveActor *a)
{
    const Workers *w;
    EmPlayerMajor2Scene *scene;
    if (context_of(context, a, &w, &scene) < 0) return -1;
    if (!w->sound || !w->w0021C350 || !w->w0021D490 || !w->cue || !w->w0021D2E0) return -1;
    void *c = w->context;
    switch (u8(a, 6)) {
    case 0:                                            /* 002255F8 */
        if (!f_eq(a, 0x224, 0.0f)) {
            CALL(w->sound(c, a, 0x146, 0, 300.0f));
            CALL(w->w0021C350(c, a));
        }
        if (f_le(a, 0x220, 0.0f)) {                    /* 00225630 */
            set8(a, 6, u8(a, 6) + 1);
            set8(a, 7, 0);
            CALL(w->sound(c, a, 0x156, 0, 300.0f));
            em_live_set_u16(a, 0x28, 0x10);
        }
        return 0;
    case 1: {                                          /* 00225680 */
        int16_t count = (int16_t)em_live_u16(a, 0x28);
        em_live_set_u16(a, 0x28, (uint16_t)(count - 1));
        if (count != 0) return 0;
        set8(a, 6, u8(a, 6) + 1);
        CALL(w->w0021D490(c, a));
        CALL(w->cue(c, 1, 0xEE, 0x3C, 1));
        return 0;
    }
    case 2:                                            /* 002256BC */
        CALL(w->w0021D2E0(c, a, 0x78, 0));
        return 0;
    }
    return 0;
}

/* ---- Entry routines ----------------------------------------------------- */

/* 00181110 / 00181180 (asm words): unless +224 == 0 and +22C == 0 and
 * (+F & 2) == 0, set +4 = 2, +5 = state, +6 = 0, +302 = a1 and return 1. */
static int enter_with_step(EmPlayerLiveActor *a, int a1, uint8_t state)
{
    if (!a) return -1;
    if (em_ee_c_eq_bits(u32(a, 0x224), 0) && em_ee_c_eq_bits(u32(a, 0x22C), 0) &&
        (u8(a, 0xF) & 2) == 0)
        return 0;
    set8(a, 4, 2);
    set8(a, 5, state);
    set8(a, 6, 0);
    set8(a, 0x302, (unsigned)a1 & 0xFF);
    return 1;
}

int em_player_major2_00181110(EmPlayerLiveActor *a, int a1) { return enter_with_step(a, a1, 4); }
int em_player_major2_00181180(EmPlayerLiveActor *a, int a1) { return enter_with_step(a, a1, 5); }

/* 00181D70: the same guard; D_00275B14 = 0x34 (+5 0x10), 0x36 (+6 >= 0x28)
 * or 0x1E; +4 = 2, +5 = 7, +6 = 0; return 1. */
int em_player_major2_00181D70(EmPlayerLiveActor *a, EmPlayerMajor2Scene *scene)
{
    if (!a || !scene) return -1;
    if (em_ee_c_eq_bits(u32(a, 0x224), 0) && em_ee_c_eq_bits(u32(a, 0x22C), 0) &&
        (u8(a, 0xF) & 2) == 0)
        return 0;
    if (u8(a, 5) == 0x10) scene->d275B14 = 0x34;
    else if (u8(a, 6) >= 0x28) scene->d275B14 = 0x36;
    else scene->d275B14 = 0x1E;
    set8(a, 4, 2);
    set8(a, 5, 7);
    set8(a, 6, 0);
    return 1;
}

/* 001823E0: +224 != 0 sets +4 = 2, +5 = 0x19, +6 = 0 and returns 1. */
int em_player_major2_001823E0(EmPlayerLiveActor *a)
{
    if (!a) return -1;
    if (em_ee_c_eq_bits(u32(a, 0x224), 0)) return 0;
    set8(a, 4, 2);
    set8(a, 5, 0x19);
    set8(a, 6, 0);
    return 1;
}
