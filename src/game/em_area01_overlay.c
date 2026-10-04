/* AREA01 overlay owners (see em_area01_overlay.h, docs/AREA01_OVERLAY.md).
 *
 * Each function below is a translation of the original AREA01 overlay code
 * at the named runtime address. Most follow the decomp's byte-identical C
 * (src/overlays/AREA01/func_overlay_AREA01_<runtime - 0x40>.c); 0x823580
 * was translated from the original code before its C recovery. Calls, their
 * arguments and the memory accesses between each two calls follow the
 * original: the test compares memory at every call entry and after the last
 * store, and the memory accesses between calls one for one, in order, by
 * address and size (docs/AREA01_OVERLAY.md, section 3). Where the original
 * loads two operands of one expression in a set order, the translation
 * loads them in separate statements in that order, because C leaves the
 * order of evaluation of function arguments and operands open.
 */
#include "em_area01_overlay_internal.h"

/* Original data the functions name. */
#define D_00810350 0x00810350u /* 8 floats: +0x10 / +0x18 are read as x / z */
#define D_008102B0 0x008102B0u /* player actor */
#define D_00810374 0x00810374u
#define D_008104A4 0x008104A4u
#define D_00810759 0x00810759u
#define D_0081075A 0x0081075Au
#define D_0081075E 0x0081075Eu
#define D_00810760 0x00810760u
#define D_00810784 0x00810784u
#define D_008107D9 0x008107D9u /* AREA01 story gate (D_008107D8[1]) */
#define D_008107E0 0x008107E0u
#define D_0028A5C4 0x0028A5C4u
#define S_70003B8D 0x70003B8Du /* scratchpad byte */

/* Overlay data (runtime addresses): script entry records and quad tables. */
#define A01_SCRIPT_829860 0x00829860u /* shaft door: locked program */
#define A01_SCRIPT_8298E0 0x008298E0u /* shaft door: first locked try */
#define A01_SCRIPT_829E60 0x00829E60u /* NPC, story byte 0 */
#define A01_SCRIPT_829FA0 0x00829FA0u /* NPC, story byte 0x80 */
#define A01_SCRIPT_82A660 0x0082A660u /* NPC, story byte 0x81 */
#define A01_DATA_82A7A0 0x0082A7A0u   /* NPC record +0x30 */
#define A01_SCRIPT_82A7B0 0x0082A7B0u /* placement [38] conversation */
#define A01_DATA_82A8F0 0x0082A8F0u   /* placement [38] record +0x30 */
#define A01_SCRIPT_82B0D0 0x0082B0D0u
#define A01_SCRIPT_82B4D0 0x0082B4D0u
#define A01_SCRIPT_82B590 0x0082B590u
#define A01_QUAD_82CC20 0x0082CC20u
#define A01_QUAD_82CC60 0x0082CC60u
#define A01_QUAD_82CCA0 0x0082CCA0u

#define F_20 0x41A00000u        /* 20.0f */
#define F_30 0x41F00000u        /* 30.0f */
#define F_40 0x42200000u        /* 40.0f */
#define F_300 0x43960000u       /* 300.0f */
#define F_ONE 0x3F800000u       /* 1.0f */

static inline float fb(uint32_t bits) { return em_ee_float(bits); }

static int a01_begin(A01Ovl *o, const EmArea01OvlHooks *h, EmArea01OvlFault *fault)
{
    if (!h || !fault) return -1;
    a01_open(o, h, fault);
    return a01_failed(o) ? -1 : 0;
}

static int a01_end(const A01Ovl *o) { return a01_failed(o) ? -1 : 0; }

/* ======================================================================
 * 0x823580 — shaft door owner (placement [12]); original code.
 * +0x04: 0 kickoff (001BBDA0, +0x00 = 1), 1 run, 2/3 free, other: nothing.
 * In state 1, +0x05 selects one of seven steps (other values run only the
 * common tail); the tail sets byte +0x0B of the node at +0x1C to 1 when the
 * story byte is 0x81 (else 0) and calls 001BC300.
 * ====================================================================== */
static void a01_shaft_door(A01Ovl *o, uint32_t self)
{
    uint32_t state = a01_u8(o, self + 4);
    uint32_t other = a01_u32(o, self + 0x1C); /* read before any callee */
    uint32_t block = self + 0x1F0;
    int32_t r = 0;

    if (state == 3 || state == 2) {
        (void)a01_c_001AFC10(o, self);
        return;
    }
    if (state == 0) {
        (void)a01_c_001BBDA0(o, self);
        a01_w8(o, self + 0, 1);
        return;
    }
    if (state != 1) return;

    switch (a01_u8(o, self + 5)) {
    case 0: /* door kickoff: open when the story byte is 0x81 */
        if (a01_u8(o, D_008107D9) == 0x81) {
            r = 0;
            (void)a01_c_001BBE40(o, self, block, 0, &r);
            if (r != 0) a01_w8(o, self + 5, 3);
        } else {
            r = 0;
            (void)a01_c_001BBE40(o, self, block, 1, &r);
            if (r != 0) a01_w8(o, self + 5, a01_u8(o, self + 5) + 1);
        }
        break;
    case 1: /* wait, then the locked-door program */
        r = 0;
        (void)a01_c_001BC0E0(o, self, block, &r);
        if (r != 0) {
            (void)a01_c_001BA1A0(o, block, A01_SCRIPT_829860);
            a01_w8(o, self + 5, a01_u8(o, self + 5) + 1);
        }
        break;
    case 2: /* wait; the first locked try raises the story byte to 0x80 */
        r = 0;
        (void)a01_c_001BC0E0(o, self, block, &r);
        if (r != 0) {
            uint32_t gate = a01_u8(o, D_008107D9);
            if (gate == 0 || gate == 0x80) {
                a01_w8(o, D_008107D9, 0x80);
                (void)a01_c_001BA1A0(o, block, A01_SCRIPT_8298E0);
                r = 0;
                (void)a01_c_001BA1F0(o, self, &r);
                a01_w8(o, self + 5, 6);
            } else {
                a01_w8(o, self + 0xB, 0);
                a01_w8(o, self + 5, 0);
            }
        }
        break;
    case 3:
        r = 0;
        (void)a01_c_001BC0E0(o, self, block, &r);
        if (r != 0) a01_w8(o, self + 5, a01_u8(o, self + 5) + 1);
        break;
    case 4:
        (void)a01_c_001BC240(o, self, block);
        a01_w8(o, self + 5, a01_u8(o, self + 5) + 1);
        break;
    case 5:
        r = 0;
        (void)a01_c_001BC290(o, self, block, &r);
        if (r != 0) a01_w8(o, self + 5, 0);
        break;
    case 6: /* run the started script until it ends */
        r = 0;
        (void)a01_c_001BA1F0(o, self, &r);
        if (r != 0) {
            a01_w8(o, self + 0xB, 0);
            a01_w8(o, self + 5, 0);
        }
        break;
    default:
        break;
    }
    a01_w8(o, other + 0xB, a01_u8(o, D_008107D9) == 0x81 ? 1 : 0);
    (void)a01_c_001BC300(o, self);
}

int em_area01_ovl_00823580(const EmArea01OvlHooks *h, uint32_t self, EmArea01OvlFault *fault)
{
    A01Ovl o;
    if (a01_begin(&o, h, fault)) return -1;
    a01_shaft_door(&o, self);
    return a01_end(&o);
}

/* ======================================================================
 * 0x825130 — op09 callback (decomp func_overlay_AREA01_008250F0).
 * Phase 0 picks clip 6 or 5 by 001B1380 and moves to phase 1; phase 1 turns
 * +0xC4 toward D_00810350 (+0x10, +0x18) at the record's rate (+0x0C) and
 * reports 1 once the turn reaches the goal.
 * ====================================================================== */
static int32_t a01_825130(A01Ovl *o, uint32_t self, uint32_t st, uint32_t prm)
{
    int32_t r = 0;
    float goal = 0.0f, turned = 0.0f;
    switch (a01_u8(o, st + 4)) {
    case 0:
        (void)a01_c_001B1380(o, D_00810350, self + 0xB0, a01_f32(o, self + 0xC4), &r);
        if (r != 0) {
            (void)a01_c_001C67E0(o, self, 6, fb(F_20), 0.0f);
        } else {
            (void)a01_c_001C67E0(o, self, 5, fb(F_20), 0.0f);
        }
        a01_w8(o, st + 4, 1);
        break;
    case 1: {
        /* Each argument is read in its own statement, in the original's
         * load order (C leaves the order of argument evaluation open). */
        float x = a01_f32(o, D_00810350 + 0x10);
        float z = a01_f32(o, D_00810350 + 0x18);
        float rate, yaw;
        (void)a01_c_001B1240(o, self + 0xB0, x, z, &goal);
        rate = a01_f32(o, prm + 0xC);
        yaw = a01_f32(o, self + 0xC4);
        (void)a01_c_001B12B0(o, goal, yaw, rate, &turned);
        a01_wf(o, self + 0xC4, turned);
        if (em_ee_c_eq(turned, goal)) return 1;
        break;
    }
    default:
        break;
    }
    return 0;
}

int em_area01_ovl_00825130(const EmArea01OvlHooks *h, uint32_t self, uint32_t block,
                           uint32_t record, int32_t *result, EmArea01OvlFault *fault)
{
    A01Ovl o;
    int32_t r;
    if (a01_begin(&o, h, fault)) return -1;
    r = a01_825130(&o, self, block, record);
    if (result && !a01_failed(&o)) *result = r; /* not written after a fault */
    return a01_end(&o);
}

/* ======================================================================
 * 0x825240 — op09 callback (decomp func_overlay_AREA01_00825200).
 * Phase 0 picks clip 5 (wrapped yaw - target > 0) or 6; phase 1 turns +0xC4
 * toward the record's yaw (+0x24) at its rate (+0x0C); on arrival it plays
 * the record's clip (+0x1C) and reports 1.
 * ====================================================================== */
static int32_t a01_825240(A01Ovl *o, uint32_t self, uint32_t st, uint32_t prm)
{
    float wrapped = 0.0f, turned = 0.0f;
    switch (a01_u8(o, st + 4)) {
    case 0: {
        /* the original loads the record's yaw before the actor's */
        float target = a01_f32(o, prm + 0x24);
        float yaw = a01_f32(o, self + 0xC4);
        (void)a01_c_001B1470(o, em_ee_sub(yaw, target), &wrapped);
        if (em_ee_c_lt(0.0f, wrapped)) {
            (void)a01_c_001C67E0(o, self, 5, fb(F_20), 0.0f);
        } else {
            (void)a01_c_001C67E0(o, self, 6, fb(F_20), 0.0f);
        }
        a01_w8(o, st + 4, 1);
        break;
    }
    case 1: {
        /* load order of the original: yaw, rate, then the record's yaw */
        float yaw = a01_f32(o, self + 0xC4);
        float rate = a01_f32(o, prm + 0xC);
        float target = a01_f32(o, prm + 0x24);
        (void)a01_c_001B12B0(o, target, yaw, rate, &turned);
        a01_wf(o, self + 0xC4, turned);
        if (em_ee_c_eq(turned, a01_f32(o, prm + 0x24))) {
            /* frame 0 comes from converting the integer 0 to float */
            (void)a01_c_001C67E0(o, self, a01_s16(o, prm + 0x1C), fb(F_20),
                                 em_ee_cvt_s_w(0));
            return 1;
        }
        break;
    }
    default:
        break;
    }
    return 0;
}

int em_area01_ovl_00825240(const EmArea01OvlHooks *h, uint32_t self, uint32_t block,
                           uint32_t record, int32_t *result, EmArea01OvlFault *fault)
{
    A01Ovl o;
    int32_t r;
    if (a01_begin(&o, h, fault)) return -1;
    r = a01_825240(&o, self, block, record);
    if (result && !a01_failed(&o)) *result = r; /* not written after a fault */
    return a01_end(&o);
}

/* ======================================================================
 * 0x8254B0 / 0x825590 / 0x825670 — the NPC's three talk state machines
 * (decomp func_overlay_AREA01_00825470 / 00825550 / 00825630), selected by
 * the story byte in 0x825350. +0x05: 0 face (copy +0xC4 to block +0x44,
 * clip 1 at blend 40), 1 wait for Use (+0x0B bit 2) and start the script,
 * 2 run the script until it ends.
 * ====================================================================== */
static void a01_talk_face(A01Ovl *o, uint32_t self)
{
    a01_w32(o, self + 0x1F0 + 0x44, a01_u32(o, self + 0xC4));
    (void)a01_c_001C67E0(o, self, 1, fb(F_40), 0.0f);
    a01_w8(o, self + 5, 1);
}

static void a01_talk_wait_use(A01Ovl *o, uint32_t self, uint32_t script)
{
    if (a01_u8(o, self + 0xB) & 4) {
        a01_w8(o, self + 5, 2);
        (void)a01_c_001BA1A0(o, self + 0x1F0, script);
    }
}

/* Script ended: back to waiting, clear the Use byte, clip 0 at blend 30. */
static void a01_talk_rewait(A01Ovl *o, uint32_t self)
{
    int32_t r = 0;
    (void)a01_c_001BA1F0(o, self, &r);
    if (r != 0) {
        a01_w8(o, self + 5, 1);
        a01_w8(o, self + 0xB, 0);
        (void)a01_c_001C67E0(o, self, 0, fb(F_30), 0.0f);
    }
}

static void a01_8254b0(A01Ovl *o, uint32_t self)
{
    switch (a01_u8(o, self + 5)) {
    case 0:
        a01_talk_face(o, self);
        a01_w16(o, self + 0x28, 0);
        break;
    case 1: a01_talk_wait_use(o, self, A01_SCRIPT_829E60); break;
    case 2: a01_talk_rewait(o, self); break;
    default: break;
    }
}

static void a01_825590(A01Ovl *o, uint32_t self)
{
    int32_t r = 0;
    switch (a01_u8(o, self + 5)) {
    case 0: a01_talk_face(o, self); break;
    case 1: a01_talk_wait_use(o, self, A01_SCRIPT_829FA0); break;
    case 2:
        (void)a01_c_001BA1F0(o, self, &r);
        if (r != 0) {
            a01_w8(o, D_00810759, 0xFF);
            r = 0;
            (void)a01_c_001C4760(o, 2, 1, &r);
            a01_w8(o, D_008107D9, 0x81);
            a01_w8(o, self + 5, 0);
            a01_w8(o, self + 6, 0);
            a01_w8(o, self + 0xB, 0);
        }
        break;
    default: break;
    }
}

static void a01_825670(A01Ovl *o, uint32_t self)
{
    switch (a01_u8(o, self + 5)) {
    case 0: a01_talk_face(o, self); break;
    case 1: a01_talk_wait_use(o, self, A01_SCRIPT_82A660); break;
    case 2: a01_talk_rewait(o, self); break;
    default: break;
    }
}

int em_area01_ovl_008254B0(const EmArea01OvlHooks *h, uint32_t self, EmArea01OvlFault *fault)
{
    A01Ovl o;
    if (a01_begin(&o, h, fault)) return -1;
    a01_8254b0(&o, self);
    return a01_end(&o);
}

int em_area01_ovl_00825590(const EmArea01OvlHooks *h, uint32_t self, EmArea01OvlFault *fault)
{
    A01Ovl o;
    if (a01_begin(&o, h, fault)) return -1;
    a01_825590(&o, self);
    return a01_end(&o);
}

int em_area01_ovl_00825670(const EmArea01OvlHooks *h, uint32_t self, EmArea01OvlFault *fault)
{
    A01Ovl o;
    if (a01_begin(&o, h, fault)) return -1;
    a01_825670(&o, self);
    return a01_end(&o);
}

/* ======================================================================
 * 0x825350 — the control-room NPC owner, placement [36]
 * (decomp func_overlay_AREA01_00825310).
 * +0x04: 0 set-up (or straight to teardown when D_0081075A != 0), 1 run the
 * talk machine picked by the story byte then animate/publish/draw, 2 idle,
 * 3 teardown and free.
 * ====================================================================== */
static void a01_npc(A01Ovl *o, uint32_t self)
{
    int32_t r = 0;
    switch (a01_u8(o, self + 4)) {
    case 0:
        if (a01_u8(o, D_0081075A) != 0) {
            a01_w8(o, self + 4, 3);
            break;
        }
        (void)a01_c_001B10B0(o, self, (int32_t)a01_u8(o, self + 0xD), 0x4A, &r);
        (void)a01_c_001C63E0(o, self, 1);
        (void)a01_c_001BA8E0(o, self, (int32_t)a01_u8(o, self + 0xD));
        a01_w8(o, self + 4, 1);
        a01_w8(o, self + 0, 1);
        a01_w32(o, self + 0x30, A01_DATA_82A7A0);
        a01_w32(o, self + 0x58, a01_u32(o, D_0028A5C4));
        break;
    case 1:
        switch (a01_u8(o, D_008107D9)) {
        case 0: a01_8254b0(o, self); break;
        case 0x80: a01_825590(o, self); break;
        case 0x81: a01_825670(o, self); break;
        default: break;
        }
        (void)a01_c_001BA580(o, self, (int32_t)a01_u8(o, self + 0xD));
        (void)a01_c_001C64F0(o, self, fb(F_ONE), &r);
        (void)a01_c_001C68C0(o, self);
        (void)a01_c_001B17A0(o, self, &r);
        a01_callback(o, self);
        break;
    case 2:
        break;
    case 3:
        (void)a01_c_001BA540(o, self);
        (void)a01_c_001AFC10(o, self);
        break;
    default:
        break;
    }
}

int em_area01_ovl_00825350(const EmArea01OvlHooks *h, uint32_t self, EmArea01OvlFault *fault)
{
    A01Ovl o;
    if (a01_begin(&o, h, fault)) return -1;
    a01_npc(&o, self);
    return a01_end(&o);
}

/* 0x825740 — placement [38]'s talk owner (decomp 00825700, byte-matched).
 * The setup runs before the story gates decide whether to retain the node.
 * Its active state starts script 0x82A7B0 on Use, then restores clip 1 when
 * the script ends. States 2 and 3 both tear down and free the node. */
static void a01_talk_owner(A01Ovl *o, uint32_t self)
{
    int32_t r = 0;
    switch (a01_u8(o, self + 4)) {
    case 0:
        (void)a01_c_001B10B0(o, self, (int32_t)a01_u8(o, self + 0xD), 0x4A, &r);
        (void)a01_c_001BA8E0(o, self, (int32_t)a01_u8(o, self + 0xD));
        (void)a01_c_001C63E0(o, self, 1);
        a01_w8(o, self, 1);
        a01_w16(o, self + 0x28, 0);
        a01_w32(o, self + 0x30, A01_DATA_82A8F0);
        a01_w32(o, self + 0x234, a01_u32(o, self + 0xC4));
        a01_w32(o, self + 0x58, a01_u32(o, D_0028A5C4));
        if (a01_u8(o, D_0081075A) == 0) {
            a01_w8(o, self + 4, 3);
            break;
        }
        (void)a01_c_001BA1C0(o, self, 6, &r);
        a01_w8(o, self + 4, r != 0 ? 3 : 1);
        break;
    case 1:
        switch (a01_u8(o, self + 5)) {
        case 0:
            if (a01_u8(o, self + 0xB) & 4) {
                a01_w8(o, self + 5, 1);
                (void)a01_c_001BA1A0(o, self + 0x1F0, A01_SCRIPT_82A7B0);
            }
            break;
        case 1:
            (void)a01_c_001BA1F0(o, self, &r);
            if (r != 0) {
                a01_w8(o, self + 5, 0);
                a01_w8(o, self + 0xB, 0);
                (void)a01_c_001C67E0(o, self, 1, fb(F_30), 0.0f);
            }
            break;
        default:
            break;
        }
        (void)a01_c_001BA580(o, self, (int32_t)a01_u8(o, self + 0xD));
        (void)a01_c_001C64F0(o, self, fb(F_ONE), &r);
        (void)a01_c_001C68C0(o, self);
        (void)a01_c_001B17A0(o, self, &r);
        a01_callback(o, self);
        break;
    case 2:
    case 3:
        (void)a01_c_001BA540(o, self);
        (void)a01_c_001AFC10(o, self);
        break;
    default:
        break;
    }
}

int em_area01_ovl_00825740(const EmArea01OvlHooks *h, uint32_t self, EmArea01OvlFault *fault)
{
    A01Ovl o;
    if (a01_begin(&o, h, fault)) return -1;
    a01_talk_owner(&o, self);
    return a01_end(&o);
}

/* The quad test shared by 0x826200 / 0x826440: the player inside quad
 * 0x82CC20 or 0x82CC60 sets header byte +0x01; either way the owner then
 * runs 001B1B70 and 001A2370(self, self + 0xD0). */
static void a01_bridge_tail(A01Ovl *o, uint32_t self)
{
    int32_t r = 0;
    int hit;
    (void)a01_c_001C6380(o, self);
    (void)a01_c_001B1EA0(o, 0, D_00810350, A01_QUAD_82CC20, 4, &r);
    hit = r == 1;
    if (!hit) {
        r = 0;
        (void)a01_c_001B1EA0(o, 0, D_00810350, A01_QUAD_82CC60, 4, &r);
        hit = r == 1;
    }
    if (hit) a01_w8(o, self + 1, 1);
    (void)a01_c_001B1B70(o, self);
    (void)a01_c_001A2370(o, self, self + 0xD0);
    a01_callback(o, self);
}

/* ======================================================================
 * 0x826200 — 0x8261A0's +0x0D == 2 half (decomp func_overlay_AREA01_008261C0).
 * +0x04: 0 pick +0xC0 from the story bytes 75E/760/784 and run 001B0FD0;
 * 1 script 0x82B4D0 with looping sound 0x8A9 once D_008107E0 != 0 and
 * D_00810760 != 0xFF, then the quad tail; 2/3 free.
 * ====================================================================== */
#define F_PI_18 0x3E32B8C3u /* 0.174532935f */
#define F_PI_3 0x3F860A92u  /* 1.04719758f */
#define F_MPI_3 0xBF860A92u /* -1.04719758f */

static void a01_826200(A01Ovl *o, uint32_t self)
{
    uint32_t block = self + 0x1F0, snd = self + 0x240;
    int32_t r = 0;
    switch (a01_u8(o, self + 4)) {
    case 0:
        if (a01_u8(o, D_0081075E) != 0) a01_w32(o, self + 0xC0, F_PI_18);
        else a01_w32(o, self + 0xC0, F_PI_3);
        if (a01_u8(o, D_00810760) != 0) a01_w32(o, self + 0xC0, F_PI_3);
        if (a01_u8(o, D_00810784) != 0) a01_w32(o, self + 0xC0, 0);
        (void)a01_c_001B0FD0(o, self, &r);
        break;
    case 1:
        switch (a01_u8(o, self + 5)) {
        case 0:
            if (a01_u8(o, D_00810760) != 0xFF && a01_u8(o, D_008107E0) != 0) {
                (void)a01_c_001BA1A0(o, block, A01_SCRIPT_82B4D0);
                a01_w8(o, self + 5, 1);
                a01_w32(o, snd, 0xFFFFFFFFu);
                a01_w16(o, self + 0x28, 0);
            }
            break;
        case 1:
            (void)a01_c_001FC3C0(o, self, snd, 0x8A9, fb(0x453B8000u) /* 3000 */,
                                 fb(0x45800000u) /* 4096 */);
            r = 0;
            (void)a01_c_001BA1F0(o, self, &r);
            if (r != 0) {
                a01_w8(o, self + 5, 2);
                (void)a01_c_001FC520(o, snd);
            }
            break;
        default:
            break;
        }
        a01_bridge_tail(o, self);
        break;
    case 2:
    case 3:
        (void)a01_c_001AFC10(o, self);
        break;
    default:
        break;
    }
}

/* ======================================================================
 * 0x826440 — 0x8261A0's +0x0D == 3 half (decomp func_overlay_AREA01_00826400).
 * +0x04: 0 pick +0xC0 from the story bytes and run 001B0FD0; 1 start script
 * 0x82B0D0 when the player stands in quad 0x82CC60 low enough (and 001BA1C0
 * bit 8 is clear, 0x70003B8D == 0, 00182BF0(player) == 0), then follow
 * D_008107E0 (0xE0: timed D_008104A4 ramp; 2: copy the +0x10/+0x18 point to
 * D_00810350 once) until the script ends; then the quad tail; 2/3 free.
 * ====================================================================== */
static void a01_826440(A01Ovl *o, uint32_t self)
{
    uint32_t block = self + 0x1F0, timer = self + 0x240;
    int32_t r = 0;
    switch (a01_u8(o, self + 4)) {
    case 0:
        if (a01_u8(o, D_0081075E) != 0) a01_w32(o, self + 0xC0, 0);
        else a01_w32(o, self + 0xC0, F_MPI_3);
        if (a01_u8(o, D_00810760) != 0) a01_w32(o, self + 0xC0, 0);
        if (a01_u8(o, D_00810784) != 0) a01_w32(o, self + 0xC0, 0);
        (void)a01_c_001B0FD0(o, self, &r);
        break;
    case 1:
        switch (a01_u8(o, self + 5)) {
        case 0: {
            int go;
            r = 0;
            (void)a01_c_001BA1C0(o, self, 8, &r);
            go = r == 0;
            if (go) go = a01_u8(o, S_70003B8D) == 0;
            if (go) {
                r = 0;
                (void)a01_c_001B1EA0(o, 0, D_00810350, A01_QUAD_82CC60, 4, &r);
                go = r == 1;
            }
            if (go) go = em_ee_c_le(a01_f32(o, D_00810350 + 4), 2.0f);
            if (go) {
                r = 0;
                (void)a01_c_00182BF0(o, D_008102B0, &r);
                go = r == 0;
            }
            if (go) {
                (void)a01_c_001BA1A0(o, block, A01_SCRIPT_82B0D0);
                a01_w8(o, D_008107E0, 1);
                a01_w8(o, self + 5, 1);
                a01_w32(o, D_00810374, 0);
            }
            break;
        }
        case 1:
            if (a01_u8(o, D_008107E0) == 0xE0) {
                switch (a01_u8(o, self + 6)) {
                case 0:
                    a01_w32(o, timer, 0);
                    a01_w8(o, self + 6, 1);
                    break;
                case 1:
                    a01_w32(o, timer, a01_u32(o, timer) + 1);
                    if (a01_s32(o, timer) == 0x3C) a01_w32(o, D_008104A4, 0x3E99999Au); /* 0.3f */
                    if (a01_s32(o, timer) == 0xC8) a01_w32(o, D_008104A4, 0x3F19999Au); /* 0.6f */
                    break;
                default:
                    break;
                }
            }
            if (a01_u8(o, D_008107E0) == 2) {
                switch (a01_u8(o, self + 7)) {
                case 0:
                    a01_w32(o, D_00810350 + 0, a01_u32(o, D_00810350 + 0x10));
                    a01_w32(o, D_00810350 + 4, 0);
                    a01_w32(o, D_00810350 + 8, a01_u32(o, D_00810350 + 0x18));
                    a01_w8(o, self + 7, 1);
                    break;
                default:
                    break;
                }
            }
            a01_w16(o, self + 0x2A, a01_u16(o, self + 0x2A) + 1);
            r = 0;
            (void)a01_c_001BA1F0(o, self, &r);
            if (r != 0) {
                a01_w8(o, D_008107E0, 0xFF);
                a01_w32(o, D_008104A4, F_ONE);
                a01_w8(o, self + 5, 2);
            }
            break;
        default:
            break;
        }
        a01_bridge_tail(o, self);
        break;
    case 2:
    case 3:
        (void)a01_c_001AFC10(o, self);
        break;
    default:
        break;
    }
}

int em_area01_ovl_00826200(const EmArea01OvlHooks *h, uint32_t self, EmArea01OvlFault *fault)
{
    A01Ovl o;
    if (a01_begin(&o, h, fault)) return -1;
    a01_826200(&o, self);
    return a01_end(&o);
}

int em_area01_ovl_00826440(const EmArea01OvlHooks *h, uint32_t self, EmArea01OvlFault *fault)
{
    A01Ovl o;
    if (a01_begin(&o, h, fault)) return -1;
    a01_826440(&o, self);
    return a01_end(&o);
}

/* ======================================================================
 * 0x8261A0 — placements [41]/[42] owner (decomp func_overlay_AREA01_00826160):
 * +0x00 = 1 while +0x04 == 0, then 0x826200 when +0x0D == 2 and 0x826440
 * when +0x0D == 3 (+0x0D re-read after the first call).
 * ====================================================================== */
int em_area01_ovl_008261A0(const EmArea01OvlHooks *h, uint32_t self, EmArea01OvlFault *fault)
{
    A01Ovl o;
    if (a01_begin(&o, h, fault)) return -1;
    if (a01_u8(&o, self + 4) == 0) a01_w8(&o, self + 0, 1);
    if (a01_u8(&o, self + 0xD) == 2) a01_826200(&o, self);
    if (a01_u8(&o, self + 0xD) == 3) a01_826440(&o, self);
    return a01_end(&o);
}

/* ======================================================================
 * 0x8267C0 — placement [45] owner (decomp func_overlay_AREA01_00826780).
 * +0x04: 0 001B0FD0 (on 0: 001C6380, +0x00 = 1); 1 while 001BA1C0 bit 15 is
 * clear and bit 7 is set: start script 0x82B590 once the player is in quad
 * 0x82CCA0 by both tests (0 and 2), run it to its end; then draw when
 * 001B17A0 != 0; 2/3 free.
 * ====================================================================== */
int em_area01_ovl_008267C0(const EmArea01OvlHooks *h, uint32_t self, EmArea01OvlFault *fault)
{
    A01Ovl o;
    int32_t r = 0;
    uint32_t block = self + 0x1F0;
    if (a01_begin(&o, h, fault)) return -1;
    switch (a01_u8(&o, self + 4)) {
    case 0:
        (void)a01_c_001B0FD0(&o, self, &r);
        if (r == 0) {
            (void)a01_c_001C6380(&o, self);
            a01_w8(&o, self + 0, 1);
        }
        break;
    case 1:
        r = 0;
        (void)a01_c_001BA1C0(&o, self, 0xF, &r);
        if (r == 0) {
            r = 0;
            (void)a01_c_001BA1C0(&o, self, 7, &r);
            if (r != 0) {
                switch (a01_u8(&o, self + 5)) {
                case 0: {
                    int go;
                    r = 0;
                    (void)a01_c_001B1EA0(&o, 0, D_00810350, A01_QUAD_82CCA0, 4, &r);
                    go = r != 0;
                    if (go) {
                        r = 0;
                        (void)a01_c_001B1EA0(&o, 2, D_00810350, A01_QUAD_82CCA0, 4, &r);
                        go = r != 0;
                    }
                    if (go) {
                        (void)a01_c_001BA1A0(&o, block, A01_SCRIPT_82B590);
                        a01_w8(&o, self + 5, 1);
                    }
                    break;
                }
                case 1:
                    r = 0;
                    (void)a01_c_001BA1F0(&o, self, &r);
                    if (r != 0) a01_w8(&o, self + 5, 2);
                    break;
                default:
                    break;
                }
            }
        }
        r = 0;
        (void)a01_c_001B17A0(&o, self, &r);
        if (r != 0) a01_callback(&o, self);
        break;
    case 2:
    case 3:
        (void)a01_c_001AFC10(&o, self);
        break;
    default:
        break;
    }
    return a01_end(&o);
}

/* ======================================================================
 * 0x826CF0 — placement [37] / sub-0 group owner (decomp
 * func_overlay_AREA01_00826CB0): header byte +0x03 == 1 runs 001C5C90,
 * anything else 001C4820.
 * ====================================================================== */
int em_area01_ovl_00826CF0(const EmArea01OvlHooks *h, uint32_t self, EmArea01OvlFault *fault)
{
    A01Ovl o;
    if (a01_begin(&o, h, fault)) return -1;
    if (a01_u8(&o, self + 3) == 1) (void)a01_c_001C5C90(&o, self);
    else (void)a01_c_001C4820(&o, self);
    return a01_end(&o);
}

/* ======================================================================
 * 0x826D40 — deferred group owner; em_area01_overlay_826d40.c.
 * ====================================================================== */
int em_area01_ovl_00826D40(const EmArea01OvlHooks *h, uint32_t self, EmArea01OvlFault *fault)
{
    A01Ovl o;
    if (a01_begin(&o, h, fault)) return -1;
    a01_ovl_826d40(&o, self);
    return a01_end(&o);
}

/* ======================================================================
 * 0x828850 — deferred group owner paired with 0x826D40 (decomp
 * func_overlay_AREA01_00828810). The partner record is the pointer at +0x18.
 * +0x04: 0 001B0FD0; on 0: +0x34 = 1, +0x00 = 1 and, when 001B11E0(+0x9A)
 * is set, partner +0x04 = 2 and own +0x04 = 3. 1: when +0x36 != 0: +0x00 = 2,
 * partner +0x04 = 2, partner +0x21C = 0x5A, then 001EFE00(0x80000045, self)
 * selects sound 0x426 and state 2 (else state 3); otherwise animate and
 * draw. 2: count +0x28 to 10 (sound 0x427 at 10), 001B1190(+0x9A), draw.
 * 3 and above: free.
 * ====================================================================== */
int em_area01_ovl_00828850(const EmArea01OvlHooks *h, uint32_t self, EmArea01OvlFault *fault)
{
    A01Ovl o;
    int32_t r = 0;
    if (a01_begin(&o, h, fault)) return -1;
    switch (a01_u8(&o, self + 4)) {
    case 0:
        (void)a01_c_001B0FD0(&o, self, &r);
        if (r == 0) {
            a01_w16(&o, self + 0x34, 1);
            a01_w8(&o, self + 0, 1);
            r = 0;
            (void)a01_c_001B11E0(&o, (int32_t)a01_u8(&o, self + 0x9A), &r);
            if (r != 0) {
                a01_w8(&o, a01_u32(&o, self + 0x18) + 4, 2);
                a01_w8(&o, self + 4, 3);
            }
        }
        break;
    case 1:
        if (a01_s16(&o, self + 0x36) != 0) {
            a01_w8(&o, self + 0, 2);
            a01_w8(&o, a01_u32(&o, self + 0x18) + 4, 2);
            a01_w32(&o, a01_u32(&o, self + 0x18) + 0x21C, 0x5A);
            r = 0;
            (void)a01_c_001EFE00(&o, (int32_t)0x80000045u, self, &r);
            if (r != 0) {
                (void)a01_c_001FBD50(&o, self, 0x426, 0, fb(F_300), &r);
                a01_w16(&o, self + 0x28, 0);
                a01_w8(&o, self + 4, 2);
            } else {
                a01_w8(&o, self + 4, 3);
            }
        } else {
            (void)a01_c_001C6380(&o, self);
            (void)a01_c_001B17A0(&o, self, &r);
            a01_callback(&o, self);
        }
        break;
    case 2: {
        /* one load for the test and the increment, then the stored value
         * is loaded again for the == 10 test (the original's loads) */
        int32_t count = a01_s16(&o, self + 0x28);
        if (count < 0xA) {
            a01_w16(&o, self + 0x28, (uint32_t)(count + 1));
            if (a01_s16(&o, self + 0x28) == 0xA)
                (void)a01_c_001FBD50(&o, self, 0x427, 0, fb(F_300), &r);
        }
        (void)a01_c_001B1190(&o, (int32_t)a01_u8(&o, self + 0x9A));
        (void)a01_c_001B17A0(&o, self, &r);
        a01_callback(&o, self);
        break;
    }
    default: /* 3 and anything else */
        (void)a01_c_001AFC10(&o, self);
        break;
    }
    return a01_end(&o);
}
