/* em_player_weapon_states_b.c - the player states +5 = 0x20, 0x21 and 0x22
 * (see em_player_weapon_states_b.h).
 *
 * Read from the original instructions (build/asm of the decomp). 00173000,
 * 001735C0 and 00173E60 are byte-matched C; 00173DD0 is NEARMISS C and was
 * translated from its instructions. Three places where the instructions say
 * more than the C seems to:
 *   - the hit-confirm test "handle != -1" compares the zero-extended +302
 *     byte with -1 (001737BC, 001739EC, 00173BF8, 00173FE8). It is never
 *     equal, so 0011A070 is always called with the byte, 0xFF included;
 *   - 001735C0's impact branches store the damage halfword with registers
 *     loaded before the phase switch (3 at 00173670 / 0017360C, 5 at
 *     00173BB0); the values are the C's;
 *   - 00173000 +6 = 0x63 stores the atan2 result at 0x70003A20 and reloads
 *     it (0017334C / 00173354) before adding pi/2.
 * Every address in a comment is the original instruction translated there.
 * tools/test_player_weapon_states_b_reference.py executes the original
 * instructions and compares every actor byte, target byte, global word and
 * worker call. */
#include "game/em_player_weapon_states_b.h"
#include "game/em_ee_float.h"

#include <stddef.h>

#define FAULT(expr) do { if ((expr) < 0) return -1; } while (0)

/* Float constants as the instructions build them (lui/ori). */
#define F_ZERO      UINT32_C(0x00000000)
#define F_ONE       UINT32_C(0x3F800000)
#define F_HALF      UINT32_C(0x3F000000)
#define F_FOUR      UINT32_C(0x40800000)
#define F_EIGHT     UINT32_C(0x41000000)
#define F_HALF_PI   UINT32_C(0x3FC90FDB)
#define F_PI        UINT32_C(0x40490FDB)
#define F_180       UINT32_C(0x43340000)
#define F_TURN      UINT32_C(0x3D32B8C3)   /* 0.043633234 (2.5 degrees) */
#define F_MINUS_0_2 UINT32_C(0xBE4CCCCD)

/* The melee tables, indexed by the row byte +236 (0 or 1: 001764E0 and the
 * hang set 1, 00179680 clears it). A row above 1 faults instead of reading
 * the next table. */
#define MELEE_ROWS 2
/* D_00248690: the three combo clips (6-byte rows). */
static const int16_t kComboClip[MELEE_ROWS][3] = {
    { 0x10B, 0x10C, 0x10D }, { 0x1BD, 0x1BE, 0x1BF },
};
/* D_002486A0: per hit, the impact and release clock thresholds (0x18-byte
 * rows): hit 1 impact (A0) / release (A4), hit 2 (A8 / AC), hit 3 (B0 / B4). */
static const uint32_t kComboGate[MELEE_ROWS][6] = {
    { UINT32_C(0x41C00000), UINT32_C(0x41A00000), UINT32_C(0x41D00000),     /* 24 20 26 */
      UINT32_C(0x41700000), UINT32_C(0x42240000), UINT32_C(0x41F00000) },   /* 15 41 30 */
    { UINT32_C(0x41C80000), UINT32_C(0x41A00000), UINT32_C(0x41E00000),     /* 25 20 28 */
      UINT32_C(0x41800000), UINT32_C(0x42240000), UINT32_C(0x42080000) },   /* 16 41 34 */
};
/* D_002486D0 / D_002486D4: the chain thresholds of hits 1 and 2 (0xC-byte
 * rows; the third word is not read here). */
static const uint32_t kComboChain[MELEE_ROWS][2] = {
    { UINT32_C(0x41980000), UINT32_C(0x41980000) },                         /* 19 19 */
    { UINT32_C(0x41980000), UINT32_C(0x41980000) },
};
/* D_002754A8: the heavy stab clip. */
static const int16_t kStabClip[MELEE_ROWS] = { 0x10E, 0x1C0 };
/* D_00248700 / D_00248704: the heavy stab's impact and release thresholds. */
static const uint32_t kStabGate[MELEE_ROWS][2] = {
    { UINT32_C(0x422C0000), UINT32_C(0x41E80000) },                         /* 43 29 */
    { UINT32_C(0x422C0000), UINT32_C(0x41E80000) },
};
/* D_002486F0: 00173DD0's turn rate in degrees by gait +23F (0..3; a gait
 * above 3 faults instead of reading past the stack copy). */
static const uint32_t kStabTurn[4] = {
    UINT32_C(0x00000000), UINT32_C(0x3F000000), UINT32_C(0x3F800000), UINT32_C(0x40000000),
};
static uint32_t w32(const EmPlayerLiveActor *a, unsigned at) { return em_live_u32(a, at); }
static void put32(EmPlayerLiveActor *a, unsigned at, uint32_t v) { em_live_set_u32(a, at, v); }
static uint16_t h16(const EmPlayerLiveActor *a, unsigned at) { return em_live_u16(a, at); }
static void put16(EmPlayerLiveActor *a, unsigned at, unsigned v) { em_live_set_u16(a, at, (uint16_t)v); }
static uint8_t b8(const EmPlayerLiveActor *a, unsigned at) { return em_live_u8(a, at); }
static void put8(EmPlayerLiveActor *a, unsigned at, unsigned v) { em_live_set_u8(a, at, (uint8_t)v); }

/* The row byte +236 at a table read: -1 (a fault) above 1. */
#define ROW(a, row) do { (row) = b8((a), 0x236); if ((row) >= MELEE_ROWS) return -1; } while (0)

/* ---- bound checks ------------------------------------------------------- */

static int melee_bound(const EmPlayerWeaponBWorkers *w)
{
    return w && w->request && w->sound && w->stop_sound && w->heading && w->translate &&
           w->probes && w->floor && w->fall_check && w->reentry && w->handoff && w->link18;
}

int em_player_weapon_b_bound_state20(const EmPlayerWeaponBWorkers *w)
{
    return w && w->scene && w->scene->d8106E0 && w->scene->d810CA4 && w->scene->spad3A20 &&
           w->request && w->sound && w->approach && w->wrap && w->atan2 && w->link20 &&
           w->bone && w->clip_id && w->skeleton && w->matrix && w->reload && w->draw &&
           w->reload_wait && w->pose && w->acquire && w->fire_00170A60 && w->fire_00171320 &&
           w->fire_00171670 && w->fire_00171B00 && w->fire_00171E90 && w->fire_001723D0;
}

int em_player_weapon_b_bound_state21(const EmPlayerWeaponBWorkers *w)
{
    return melee_bound(w) && w->scene && w->scene->pad_pressed && w->scene->spad3B78;
}

int em_player_weapon_b_bound_state22(const EmPlayerWeaponBWorkers *w)
{
    return melee_bound(w) && w->approach;
}

/* ---- shared pieces ------------------------------------------------------ */

static int request(const EmPlayerWeaponBWorkers *w, EmPlayerLiveActor *a, int clip,
                   uint32_t blend)
{
    return w->request(w->context, a, clip, 0, blend);
}

/* A load of the +18 word and the record it addresses. */
static int target(const EmPlayerWeaponBWorkers *w, const EmPlayerLiveActor *a, uint8_t **record)
{
    *record = NULL;
    FAULT(w->link18(w->context, w32(a, 0x18), record));
    return *record ? 0 : -1;
}

/* The target's event byte +0 (1 = swing live, 2 = release). */
static int target_event(const EmPlayerWeaponBWorkers *w, const EmPlayerLiveActor *a, uint8_t value)
{
    uint8_t *record;
    FAULT(target(w, a, &record));
    record[0] = value;
    return 0;
}

/* The impact: event 1, the damage halfword +36 (each through its own load
 * of +18), the positional sound whose handle's low byte goes to +302, and
 * the step-effect marker +25E. */
static int impact(const EmPlayerWeaponBWorkers *w, EmPlayerLiveActor *a, unsigned damage,
                  int sound, unsigned marker)
{
    uint8_t *record;
    FAULT(target_event(w, a, 1));
    FAULT(target(w, a, &record));
    record[0x36] = (uint8_t)damage;
    record[0x37] = (uint8_t)(damage >> 8);
    int handle = 0;
    FAULT(w->sound(w->context, a, sound, &handle));
    put8(a, 0x302, (unsigned)handle);
    put8(a, 0x25E, marker);
    return 0;
}

/* The hit confirm: the target's +A byte. When it is set, +6 = 0x50, and the
 * swing sound stops: 0011A070(+302) always runs (the zero-extended byte is
 * never -1), then +302 = 0xFF. *hit says which way it went. */
static int hit_confirm(const EmPlayerWeaponBWorkers *w, EmPlayerLiveActor *a, int *hit)
{
    uint8_t *record;
    FAULT(target(w, a, &record));
    *hit = record[0xA] != 0;
    if (!*hit) return 0;
    put8(a, 6, 0x50);
    FAULT(w->stop_sound(w->context, b8(a, 0x302)));
    put8(a, 0x302, 0xFF);
    return 0;
}

/* The chain press: D_00810E74 & 0x70003B78 sets +2E. */
static void chain_press(const EmPlayerWeaponBWorkers *w, EmPlayerLiveActor *a)
{
    if (*w->scene->pad_pressed & *w->scene->spad3B78) put16(a, 0x2E, 1);
}

static int gate(const EmPlayerLiveActor *a, uint32_t threshold)
{
    return em_ee_c_le_bits(w32(a, 0x3C), threshold);
}

/* +6 = 0x50 / 0x51 (the hit-confirm recover), 0x52, 0x63 and 0x64: shared
 * by 001735C0 (00173C78..00173D84) and 00173E60 (00174070..0017417C).
 * *handled is 0 for any other +6. */
static int melee_exit(const EmPlayerWeaponBWorkers *w, EmPlayerLiveActor *a, uint8_t state,
                      int *handled)
{
    *handled = 1;
    int ignored = 0;
    switch (state) {
    case 0x50:
        put8(a, 6, state + 1);
        put16(a, 0x28, 4);
        /* fall through */
    case 0x51: {
        int16_t count = (int16_t)h16(a, 0x28);
        put16(a, 0x28, (uint16_t)(count - 1));
        if (count == 0) {
            put8(a, 6, b8(a, 6) + 1);
            return request(w, a, b8(a, 0x236) == 0 ? 0x10F : 0x1C1, F_FOUR);
        }
        put32(a, 0x204, 0);
        return 0;
    }
    case 0x52:
        if (w32(a, 0x200) & 0x1000) put8(a, 6, 0x63);
        return 0;
    case 0x63:
        FAULT(w->heading(w->context, a, 1, &ignored));
        if (b8(a, 0x23F) >= 2) {
            put8(a, 6, b8(a, 6) + 1);
            return w->reentry(w->context, a, 0);
        }
        put8(a, 0x25C, 0);
        return w->handoff(w->context, a);
    case 0x64:
        FAULT(w->heading(w->context, a, 1, &ignored));
        FAULT(w->translate(w->context, a, 0));
        if (!(w32(a, 0x200) & 0x8000)) return w->handoff(w->context, a);
        return 0;
    default:
        *handled = 0;
        return 0;
    }
}

/* The tail of both melee routines: 001764E0, +B4 += -0.2, 00175900(p, 1),
 * 001796C0 (00173D88.. / 00174180..). */
static int melee_tail(const EmPlayerWeaponBWorkers *w, EmPlayerLiveActor *a)
{
    int ignored = 0;
    FAULT(w->probes(w->context, a));
    put32(a, 0xB4, em_ee_add_bits(w32(a, 0xB4), F_MINUS_0_2));
    FAULT(w->floor(w->context, a, 1, &ignored));
    return w->fall_check(w->context, a);
}

/* ---- 001735C0: the light combo ------------------------------------------ */

/* Hit 1 (+6 = 1), by +7. */
static int combo_hit1(const EmPlayerWeaponBWorkers *w, EmPlayerLiveActor *a)
{
    uint8_t sub = b8(a, 7);
    unsigned row = 0;
    int hit = 0;
    switch (sub) {
    case 0:                                                            /* 001736A8 */
        put8(a, 7, sub + 1);
        ROW(a, row);
        FAULT(request(w, a, kComboClip[row][0], w32(a, 0x1FC)));
        put16(a, 0x2E, 0);                                             /* 001736E0 */
        return 0;
    case 1:                                                            /* 001736E4 */
        if (!(w32(a, 0x200) & 0x8000)) put8(a, 7, sub + 1);
        return 0;
    case 2:                                                            /* 00173700 */
        ROW(a, row);
        if (gate(a, kComboGate[row][0])) {
            put8(a, 7, sub + 1);
            FAULT(impact(w, a, 3, 0x17D, 0x81));
        }
        chain_press(w, a);                                             /* 00173774 */
        return 0;
    case 3:                                                            /* 0017379C */
        FAULT(hit_confirm(w, a, &hit));
        if (hit) return 0;
        chain_press(w, a);                                             /* 001737D8 */
        ROW(a, row);
        if (gate(a, kComboGate[row][1])) FAULT(target_event(w, a, 2)); /* 00173820 */
        ROW(a, row);
        if (gate(a, kComboChain[row][0])) {                            /* 00173860 */
            if (h16(a, 0x2E) != 0) {
                put8(a, 6, b8(a, 6) + 1);
                put8(a, 7, 0);
                FAULT(request(w, a, kComboClip[row][1], F_ONE));
                return target_event(w, a, 2);                          /* 001738C0 */
            }
            put8(a, 7, b8(a, 7) + 1);                                  /* 001738D0 */
        }
        return 0;
    case 4:                                                            /* 001738E0 */
        if (w32(a, 0x200) & 0x1000) put8(a, 6, 0x63);
        return 0;
    default:
        return 0;
    }
}

/* Hit 2 (+6 = 2), by +7. */
static int combo_hit2(const EmPlayerWeaponBWorkers *w, EmPlayerLiveActor *a)
{
    uint8_t sub = b8(a, 7);
    unsigned row = 0;
    int hit = 0;
    switch (sub) {
    case 0:                                                            /* 00173930 */
        put8(a, 7, sub + 1);
        put16(a, 0x2E, 0);
        return 0;
    case 1:                                                            /* 0017393C */
        ROW(a, row);
        if (gate(a, kComboGate[row][2])) {
            put8(a, 7, sub + 1);
            FAULT(impact(w, a, 3, 0x17E, 0x82));
        }
        chain_press(w, a);                                             /* 001739A8 */
        return 0;
    case 2:                                                            /* 001739D0 */
        FAULT(hit_confirm(w, a, &hit));
        if (hit) return 0;
        chain_press(w, a);                                             /* 00173A08 */
        ROW(a, row);
        if (gate(a, kComboChain[row][1])) {                            /* 00173A50 */
            if (h16(a, 0x2E) != 0) {
                put8(a, 6, b8(a, 6) + 1);
                put8(a, 7, 0);
                FAULT(request(w, a, kComboClip[row][2], F_ONE));
                return target_event(w, a, 2);                          /* 00173AB0 */
            }
            put8(a, 7, b8(a, 7) + 1);                                  /* 00173AC0 */
        }
        return 0;
    case 3:                                                            /* 00173AD0 */
        if (w32(a, 0x200) & 0x1000) {
            put8(a, 6, 0x63);
        } else {
            ROW(a, row);
            if (gate(a, kComboGate[row][3])) FAULT(target_event(w, a, 2)); /* 00173B0C */
        }
        return 0;
    default:
        return 0;
    }
}

/* Hit 3 (+6 = 3), by +7. */
static int combo_hit3(const EmPlayerWeaponBWorkers *w, EmPlayerLiveActor *a)
{
    uint8_t sub = b8(a, 7);
    unsigned row = 0;
    int hit = 0;
    switch (sub) {
    case 0:                                                            /* 00173B60 */
        put8(a, 7, sub + 1);
        return 0;
    case 1:                                                            /* 00173B68 */
        ROW(a, row);
        if (gate(a, kComboGate[row][4])) {
            put8(a, 7, sub + 1);
            FAULT(impact(w, a, 5, 0x17F, 0x82));
        }
        return 0;
    case 2:                                                            /* 00173BDC */
        FAULT(hit_confirm(w, a, &hit));
        if (hit) return 0;
        ROW(a, row);
        if (gate(a, kComboGate[row][5])) {                             /* 00173C38 */
            put8(a, 7, sub + 1);
            FAULT(target_event(w, a, 2));
        }
        return 0;
    case 3:                                                            /* 00173C5C */
        if (w32(a, 0x200) & 0x1000) put8(a, 6, 0x63);
        return 0;
    default:
        return 0;
    }
}

static int combo(const EmPlayerWeaponBWorkers *w, EmPlayerLiveActor *a)
{
    uint8_t state = b8(a, 6);
    int handled = 0;
    FAULT(melee_exit(w, a, state, &handled));
    if (!handled && state <= 3) {
        if (state == 0) {                                              /* 00173644 */
            uint8_t *record;
            put8(a, 6, state + 1);
            put8(a, 7, 0);
            put32(a, 0x38, 0);
            FAULT(target(w, a, &record));
            record[0xA] = 0;
            put8(a, 0x302, 0xFF);
            state = 1;                                                 /* falls into 1 */
        }
        if (state == 1) FAULT(combo_hit1(w, a));
        else if (state == 2) FAULT(combo_hit2(w, a));
        else FAULT(combo_hit3(w, a));
    }
    return melee_tail(w, a);
}

/* ---- 00173DD0 / 00173E60: the heavy stab -------------------------------- */

int em_player_weapon_b_00173DD0(const EmPlayerWeaponBWorkers *w, EmPlayerLiveActor *a)
{
    int turning = 0;
    FAULT(w->heading(w->context, a, 2, &turning));                     /* 00173DF4 */
    if (turning == 0) return 0;
    uint8_t gait = b8(a, 0x23F);                                       /* 00173E10 */
    if (gait >= 4) return -1;
    uint32_t rate = em_ee_div_bits(em_ee_mul_bits(F_PI, kStabTurn[gait]), F_180); /* 00173E30 */
    uint32_t yaw = 0;
    FAULT(w->approach(w->context, w32(a, 0x218), w32(a, 0xC4), rate, &yaw));
    put32(a, 0xC4, yaw);                                               /* 00173E48 */
    return 0;
}

static int stab(const EmPlayerWeaponBWorkers *w, EmPlayerLiveActor *a)
{
    uint8_t state = b8(a, 6);
    int handled = 0, hit = 0;
    uint8_t *record;
    FAULT(melee_exit(w, a, state, &handled));
    if (!handled) {
        switch (state) {
        case 0:                                                        /* 00173EF0 */
            put8(a, 6, state + 1);
            put8(a, 7, 0);
            put32(a, 0x38, 0);
            if (b8(a, 0x236) >= MELEE_ROWS) return -1;
            FAULT(request(w, a, kStabClip[b8(a, 0x236)], w32(a, 0x1FC)));
            FAULT(target(w, a, &record));                              /* 00173F1C */
            record[0xA] = 0;
            put8(a, 0x302, 0xFF);
            break;
        case 1:                                                        /* 00173F30 */
            if (!(w32(a, 0x200) & 0x8000)) put8(a, 6, state + 1);
            break;
        case 2:                                                        /* 00173F4C */
            FAULT(em_player_weapon_b_00173DD0(w, a));
            if (b8(a, 0x236) >= MELEE_ROWS) return -1;
            if (gate(a, kStabGate[b8(a, 0x236)][0])) {
                put8(a, 6, b8(a, 6) + 1);                              /* 00173F98 */
                FAULT(target_event(w, a, 1));
                FAULT(target(w, a, &record));
                record[0x36] = 0xF;
                record[0x37] = 0;
                int handle = 0;
                FAULT(w->sound(w->context, a, 0x17F, &handle));
                put8(a, 0x302, (unsigned)handle);
                put8(a, 0x25E, 0x83);
            }
            break;
        case 3:                                                        /* 00173FCC */
            FAULT(hit_confirm(w, a, &hit));
            if (hit) break;
            FAULT(em_player_weapon_b_00173DD0(w, a));                  /* 00174004 */
            if (b8(a, 0x236) >= MELEE_ROWS) return -1;
            if (gate(a, kStabGate[b8(a, 0x236)][1])) {
                put8(a, 6, b8(a, 6) + 1);
                FAULT(target_event(w, a, 2));                          /* 00174048 */
            }
            break;
        case 4:                                                        /* 00174054 */
            if (w32(a, 0x200) & 0x1000) put8(a, 6, 0x63);
            break;
        default:
            break;
        }
    }
    return melee_tail(w, a);
}

/* ---- 00173000: the R2 aiming stance ------------------------------------- */

/* The aim-pose clip from D_00248B88 (+5 0x1D / 0x1E) or D_00248C68 by +275,
 * requested with blend 0.0 (0017314C.. / 001733B4..). */
static int stance_pose(const EmPlayerWeaponBWorkers *w, EmPlayerLiveActor *a)
{
    uint8_t mode = b8(a, 5);
    uint32_t table = (mode == 0x1D || mode == 0x1E) ? EM_PLAYER_WEAPON_B_CLIPS_1D1E
                                                    : EM_PLAYER_WEAPON_B_CLIPS_OTHER;
    int16_t clip = 0;
    FAULT(w->clip_id(w->context, table, b8(a, 0x275), &clip));
    return request(w, a, clip, F_ZERO);
}

/* copy_qw4(p + 2A0, node 4 + 90): the node's 64-byte matrix. */
static int copy_node_matrix(const EmPlayerWeaponBWorkers *w, EmPlayerLiveActor *a)
{
    uint32_t matrix[16];
    FAULT(w->bone(w->context, 4, matrix));
    for (unsigned i = 0; i < 16; ++i) put32(a, 0x2A0 + 4 * i, matrix[i]);
    return 0;
}

static int stance(const EmPlayerWeaponBWorkers *w, EmPlayerLiveActor *a)
{
    const EmPlayerWeaponBScene *s = w->scene;
    *s->d8106E0 = 0;                                                   /* 00173014 */
    uint8_t state = b8(a, 6);
    switch (state) {
    case 0:                                                            /* 00173084 */
        FAULT(w->reload(w->context, a, 0));
        if (b8(a, 0x317) == 0) {
            put8(a, 6, b8(a, 6) + 1);
            put32(a, 0x278, F_HALF);
            put8(a, 0x2F2, 0);
            put16(a, 0x2E, 0);
            put8(a, 0x275, 0);
            FAULT(request(w, a, 0x188, F_ONE));
        } else {                                                       /* 001730D8 */
            put8(a, 6, b8(a, 6) + 2);
            FAULT(w->draw(w->context, a, 0));
        }
        put32(a, 0x27C, F_HALF);                                       /* 001730F4 */
        put8(a, 7, 0);
        put8(a, 0x302, 0);
        put16(a, 0x276, 0);
        put8(a, 0x2F0, 0);
        put8(a, 0x274, 0);
        return 0;
    case 1:                                                            /* 00173110 */
        if (w32(a, 0x200) & 0x1000) {
            put8(a, 6, state + 1);
            FAULT(w->draw(w->context, a, 0));
            FAULT(stance_pose(w, a));
        }
        FAULT(w->skeleton(w->context, a));                             /* 001731A8 */
        return copy_node_matrix(w, a);
    case 2: {                                                          /* 001731D0 */
        put16(a, 0x94, 7);
        put8(a, 0x302, 0);
        FAULT(w->pose(w->context, a));
        uint8_t option = *s->d810CA4;                                  /* 001731E0 */
        if (option == 0) {
            if (b8(a, 0x274) != 0) {
                put8(a, 0x2F0, b8(a, 0x2F0) + 1);
                if (b8(a, 0x2F0) >= 3) put8(a, 0x2F0, 0);              /* 00173208 */
            }
            FAULT(w->acquire(w->context, a));
        } else if (option == 1) {
            FAULT(w->acquire(w->context, a));
        }
        switch (b8(a, 0x275)) {                                        /* jtbl_0026D6C0 */
        case 0: return w->fire_00170A60(w->context, a, 1);
        case 1: return w->fire_00171320(w->context, a);
        case 2: return w->fire_00171670(w->context, a);
        case 3: return w->fire_00171B00(w->context, a);
        case 4: return w->fire_00171E90(w->context, a);
        case 5: return w->fire_001723D0(w->context, a);
        default: return 0;
        }
    }
    case 3:                                                            /* 001732D0 */
        FAULT(w->reload_wait(w->context, a));
        if (b8(a, 0x1F0) == 0x33) put8(a, 1, 0);
        return 0;
    case 0x63: {                                                       /* 001732F4 */
        put8(a, 6, state + 1);
        put16(a, 0x28, 8);
        put32(a, 0x26C, em_ee_div_bits(em_ee_sub_bits(F_HALF, w32(a, 0x27C)), F_EIGHT));
        put32(a, 0x270, em_ee_div_bits(em_ee_sub_bits(F_HALF, w32(a, 0x278)), F_EIGHT));
        uint32_t c0 = 0, c8 = 0, angle = 0, heading = 0;
        FAULT(w->link20(w->context, w32(a, 0x20), &c0, &c8));          /* 00173334 */
        FAULT(w->atan2(w->context, em_ee_neg_bits(c8), c0, &angle));
        *s->spad3A20 = angle;                                          /* 0017334C */
        FAULT(w->wrap(w->context, em_ee_add_bits(F_HALF_PI, *s->spad3A20), &heading));
        put32(a, 0x218, heading);                                      /* 0017336C */
    }
        /* fall through */
    case 0x64: {                                                       /* 00173370 */
        int16_t count = (int16_t)h16(a, 0x28);
        put16(a, 0x28, (uint16_t)(count - 1));
        if (count == 0) {
            put8(a, 6, b8(a, 6) + 1);
            put32(a, 0x27C, F_HALF);
            put32(a, 0x278, F_HALF);
            return stance_pose(w, a);
        }
        put32(a, 0x27C, em_ee_add_bits(w32(a, 0x27C), w32(a, 0x26C)));  /* 00173420 */
        put32(a, 0x278, em_ee_add_bits(w32(a, 0x278), w32(a, 0x270)));
        if (b8(a, 0x1F0) == 0x33) return w->matrix(w->context, a);    /* 00173510 */
        FAULT(w->matrix(w->context, a));                               /* 00173444 */
        uint8_t action = b8(a, 0x1F0);
        if (action == 0x32 || action == 0x35 || b8(a, 0x275) == 4 || b8(a, 0x2F2) != 0)
            return copy_node_matrix(w, a);
        uint32_t node[16];                                             /* 001734DC */
        FAULT(w->bone(w->context, 4, node));
        put32(a, 0x2D0, node[12]);
        put32(a, 0x2D4, node[13]);
        put32(a, 0x2D8, node[14]);
        return 0;
    }
    case 0x65:                                                         /* 00173524 */
        put8(a, 6, state + 1);
        put16(a, 0x276, 0);
        FAULT(request(w, a, 0x189, F_ONE));
        {
            int ignored = 0;
            FAULT(w->sound(w->context, a, 0x163, &ignored));
        }
        put8(a, 0x317, 0);
        return 0;
    case 0x66:                                                         /* 00173560 */
        if (w32(a, 0x200) & 0x1000) {
            put8(a, 5, 0x14);
            put8(a, 6, 0);
            put8(a, 0x1F0, 0x26);
        } else {
            uint32_t yaw = 0;
            FAULT(w->approach(w->context, w32(a, 0x218), w32(a, 0xC4), F_TURN, &yaw));
            put32(a, 0xC4, yaw);                                       /* 001735A0 */
        }
        return 0;
    default:
        return 0;
    }
}

/* ---- the state callbacks ------------------------------------------------ */

int em_player_weapon_b_state20(void *workers, EmPlayerLiveActor *actor)
{
    const EmPlayerWeaponBWorkers *w = workers;
    if (!actor || !em_player_weapon_b_bound_state20(w)) return -1;
    return stance(w, actor) < 0 ? -1 : 0;
}

int em_player_weapon_b_state21(void *workers, EmPlayerLiveActor *actor)
{
    const EmPlayerWeaponBWorkers *w = workers;
    if (!actor || !em_player_weapon_b_bound_state21(w)) return -1;
    return combo(w, actor) < 0 ? -1 : 0;
}

int em_player_weapon_b_state22(void *workers, EmPlayerLiveActor *actor)
{
    const EmPlayerWeaponBWorkers *w = workers;
    if (!actor || !em_player_weapon_b_bound_state22(w)) return -1;
    return stab(w, actor) < 0 ? -1 : 0;
}
