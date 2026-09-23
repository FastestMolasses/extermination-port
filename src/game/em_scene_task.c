#include "game/em_scene_task.h"

#include <stddef.h>
#include <string.h>

/* Task byte at ORIGINAL record offset `off` (+8..+0x1F), see em_scene_state.h. */
#define TB(user, off) ((user)[(off) - EM_SCENE_TASK_FIRST])

/* One worker call through the S1 protocol (em_scene_workers.h): fault when
 * latched or NULL, trace, call, latch a negative result. FIELD is the
 * EmSceneWorkers member; ARGS is its parenthesised argument list, ctx first;
 * T0..T3 are the a0..a3 the original call site sets up. Evaluates to -1 on a
 * fault, else the worker's result. */
#define ST_CALL(s, w, caller, callee, field, t0, t1, t2, t3, args)                              \
    (em_scene_worker_enter((s), (w), (caller), (callee), (w) != NULL && (w)->field != NULL,     \
                           (uint32_t)(t0), (uint32_t)(t1), (uint32_t)(t2), (uint32_t)(t3)) < 0 \
         ? -1                                                                                   \
         : em_scene_worker_leave((s), (callee), (w)->field args))

#define ST_CALL0(s, w, caller, callee, field) \
    ST_CALL(s, w, caller, callee, field, 0, 0, 0, 0, ((w)->ctx))

/* Entry guard shared by the cores: fail-stop, and the task record must exist. */
static int core_begin(EmSceneState *s, const uint8_t *user, uint32_t address)
{
    if (em_scene_faulted(s))
        return -1;
    if (!user)
        return em_scene_fault(s, address, EM_SCENE_FAULT_BAD_INDEX);
    return 0;
}

/* A call from one core to another core (or to 00121A28, done in place):
 * not a worker, so never a NULL fault, but reported to the trace so the
 * trace keeps the original jal order. */
static int core_call(EmSceneState *s, const EmSceneWorkers *w, uint32_t caller, uint32_t callee,
                     uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (em_scene_faulted(s))
        return -1;
    if (w && w->trace)
        w->trace(w->ctx, caller, callee, a0, a1, a2, 0);
    return 0;
}

/* D_0028A9A0 (lh; transition substate), owned outside the coordinator. */
static int read_0028A9A0(EmSceneState *s, const EmSceneWorkers *w, int16_t *out)
{
    if (em_scene_reader_ready(s, w, 0x0028A9A0u, w != NULL && w->r_0028A9A0 != NULL) < 0)
        return -1;
    *out = w->r_0028A9A0(w->ctx);
    return 0;
}

/* D_00282157 (lb; audio busy), owned outside the coordinator. */
static int read_00282157(EmSceneState *s, const EmSceneWorkers *w, uint8_t *out)
{
    if (em_scene_reader_ready(s, w, 0x00282157u, w != NULL && w->r_00282157 != NULL) < 0)
        return -1;
    *out = w->r_00282157(w->ctx);
    return 0;
}

static int store_result(EmSceneState *s, uint32_t address, int result)
{
    return em_scene_worker_leave(s, address, result) < 0 ? -1 : 0;
}

static uint16_t task_u16(const uint8_t *user, unsigned off)
{
    return em_scene_task_u16(user, off, NULL);
}

/* ------------------------------------------------------------ 001AFCF0 */

/* 001AFCF0 (byte-matched), called by 0x1AE040 states 0 and 4. Store order is
 * the .s order: 3B84 (sh), 3B93, 3B8C, 3B8D, 3B8E, 3B8F, 3B91, 3B92 (3B90 is
 * not cleared), the word 0x70003258 (jal delay slot), then
 * 00121A28(D_008106B0, 0, 0x48) and 001FC9B0(). */
int em_sf_001AFCF0(EmSceneState *s, const EmSceneWorkers *w)
{
    enum { F = 0x001AFCF0u };
    if (em_scene_faulted(s))
        return -1;
    s->spad3B84 = 0;
    s->spad3B93 = 0;
    s->spad3B8C = 0;
    s->spad3B8D = 0;
    s->spad3B8E = 0;
    s->spad3B8F = 0;
    s->spad3B91 = 0;
    s->spad3B92 = 0;
    s->spad3258 = 0;
    if (core_call(s, w, F, 0x00121A28u, EM_SCENE_REQ_BASE, 0, EM_SCENE_REQ_SIZE) < 0)
        return -1;
    memset(s->req, 0, EM_SCENE_REQ_SIZE); /* 00121A28 = memset over the owned block */
    return ST_CALL0(s, w, F, 0x001FC9B0u, w_001FC9B0) < 0 ? -1 : 0;
}

/* ------------------------------------------------------------ 001AD140 */

/* 001AD140 (byte-matched): +8=3, +9=2, +A=+B=0, then 001FC9B0, 001FBC50,
 * 001FABB0. The next chain tick enters 001AD4E0 (game-over screen). */
int em_sf_001AD140(EmSceneState *s, uint8_t *user, const EmSceneWorkers *w)
{
    enum { F = 0x001AD140u };
    if (core_begin(s, user, F) < 0)
        return -1;
    TB(user, 0x08) = 3;
    TB(user, 0x09) = 2;
    TB(user, 0x0A) = 0;
    TB(user, 0x0B) = 0;
    if (ST_CALL0(s, w, F, 0x001FC9B0u, w_001FC9B0) < 0)
        return -1;
    if (ST_CALL0(s, w, F, 0x001FBC50u, w_001FBC50) < 0)
        return -1;
    return ST_CALL0(s, w, F, 0x001FABB0u, w_001FABB0) < 0 ? -1 : 0;
}

/* ------------------------------------------------------------ 001AD010 */

/* 001AD010 (NEARMISS C; semantics checked against the splat .s). */
int em_sf_001AD010(EmSceneState *s, uint8_t *user, const EmSceneWorkers *w)
{
    enum { F = 0x001AD010u };
    if (core_begin(s, user, F) < 0)
        return -1;
    /* Room move: 702 = B7, +B = 4. B8 is not cleared here. */
    if (s->req[EM_SCENE_REQ_B8] == 2) {
        s->d810702 = s->req[EM_SCENE_REQ_B7];
        TB(user, 0x0B) = 4;
        return 0;
    }
    /* 3B93 request (lbu, then a redundant andi 0xFF before the == 2 test). */
    uint8_t st = s->spad3B93;
    if (st != 0) {
        if (st == 2 && ST_CALL0(s, w, F, 0x001FABB0u, w_001FABB0) < 0)
            return -1;
        TB(user, 0x09) = 3;
        TB(user, 0x0A) = 0;
        TB(user, 0x0B) = 0;
        return 0;
    }
    /* Area change. The .s loads B5, B7, B6 first and stores 700, 702 before
     * the B6 test. */
    uint8_t b5 = s->req[EM_SCENE_REQ_B5];
    uint8_t b7 = s->req[EM_SCENE_REQ_B7];
    uint8_t b6 = s->req[EM_SCENE_REQ_B6];
    s->d810700 = b5;
    s->d810702 = b7;
    if (b6 == 0xFF) {
        /* D_00810730[B5 & 0xFF] & 0x7F. An index past the owned 0x20 bytes
         * would read D_00810750.. in the original: fault instead. */
        const uint8_t *entry = em_scene_d810730_at(s, b5);
        if (!entry)
            return em_scene_fault(s, F, EM_SCENE_FAULT_BAD_INDEX);
        s->d810701 = (uint8_t)(*entry & 0x7F);
    } else {
        s->d810701 = b6;
    }
    TB(user, 0x09) = 5;
    TB(user, 0x0A) = 0;
    TB(user, 0x0B) = 0;
    /* 001FBC50 is declared void; a0 = B7 and a1 = B5 are still in the
     * registers at the jal (trace only). */
    if (ST_CALL(s, w, F, 0x001FBC50u, w_001FBC50, b7, b5, 0, 0, ((w)->ctx)) < 0)
        return -1;
    return ST_CALL0(s, w, F, 0x001FABB0u, w_001FABB0) < 0 ? -1 : 0;
}

/* ------------------------------------------------------------ 001ADF00 */

/* 001ADF00 (byte-matched): D_00810D38 = 0 (sw), 3B93 = 0, 001D2880,
 * 001D1EF0, 001AEBA0(0xFF), D_00275BDC = 1, 001AB790(001AC070), which
 * replaces the running task. */
int em_sf_001ADF00(EmSceneState *s, const EmSceneWorkers *w)
{
    enum { F = 0x001ADF00u };
    if (em_scene_faulted(s))
        return -1;
    if (em_scene_reader_ready(s, w, 0x00810D38u, w != NULL && w->s_00810D38 != NULL) < 0 ||
        store_result(s, 0x00810D38u, w->s_00810D38(w->ctx, 0)) < 0)
        return -1;
    s->spad3B93 = 0;
    if (ST_CALL0(s, w, F, 0x001D2880u, w_001D2880) < 0)
        return -1;
    if (ST_CALL0(s, w, F, 0x001D1EF0u, w_001D1EF0) < 0)
        return -1;
    if (ST_CALL(s, w, F, 0x001AEBA0u, w_001AEBA0, 0xFF, 0, 0, 0, ((w)->ctx, 0xFF)) < 0)
        return -1;
    s->d275BDC = 1;
    return ST_CALL(s, w, F, 0x001AB790u, w_001AB790, EM_SCENE_FN_001AC070, 0, 0, 0,
                   ((w)->ctx, EM_SCENE_FN_001AC070)) < 0
               ? -1
               : 0;
}

/* ------------------------------------------------------------ 001AD4E0 */

/* 001ABF90 packet 001AD4E0 pushes in steps 3 and 4 (four 64-bit values). */
static int push_packet_001ABF90(EmSceneState *s, const EmSceneWorkers *w)
{
    enum { F = 0x001AD4E0u };
    static const uint64_t packet[4] = {
        0x2005C00621322A00ull, 0x2005C08621322A40ull,
        0x2005C20621322C00ull, 0x2005C28621322C40ull,
    };
    return ST_CALL(s, w, F, 0x001ABF90u, w_001ABF90, packet[0], packet[1], packet[2], packet[3],
                   ((w)->ctx, packet[0], packet[1], packet[2], packet[3]));
}

/* 001AD4E0 (byte-matched): game-over screen, steps on +A; other +A values
 * do nothing. */
int em_sf_001AD4E0(EmSceneState *s, uint8_t *user, const EmSceneWorkers *w)
{
    enum { F = 0x001AD4E0u };
    int16_t fade;
    if (core_begin(s, user, F) < 0)
        return -1;
    switch (TB(user, 0x0A)) {
    case 0:
        if (ST_CALL0(s, w, F, 0x001D2880u, w_001D2880) < 0)
            return -1;
        if (ST_CALL0(s, w, F, 0x001D1EF0u, w_001D1EF0) < 0)
            return -1;
        TB(user, 0x0A) = (uint8_t)(TB(user, 0x0A) + 1);
        em_scene_task_set_u16(user, EM_SCENE_TASK_18, 0xF0);
        return ST_CALL(s, w, F, 0x001AEDB0u, w_001AEDB0, 0, 0, 0, 0, ((w)->ctx, 0)) < 0 ? -1 : 0;
    case 1:
        if (ST_CALL0(s, w, F, 0x001D1EF0u, w_001D1EF0) < 0)
            return -1;
        s->d275BD8 = 1;
        if (ST_CALL(s, w, F, 0x001FF080u, w_001FF080, 0, 0x27, 0, 0, ((w)->ctx, 0, 0x27)) < 0)
            return -1;
        TB(user, 0x0A) = (uint8_t)(TB(user, 0x0A) + 1);
        return 0;
    case 2:
        if (s->d275BD8 != 0)
            return 0;
        if (ST_CALL(s, w, F, 0x001AEE10u, w_001AEE10, 4, 0, 0, 0, ((w)->ctx, 4, 0)) < 0)
            return -1;
        TB(user, 0x0A) = (uint8_t)(TB(user, 0x0A) + 1);
        if (ST_CALL(s, w, F, 0x001FA790u, w_001FA790, 0, 0x1B, 0, 0, ((w)->ctx, 0, 0x1B)) < 0)
            return -1;
        return ST_CALL(s, w, F, 0x001D2830u, w_001D2830, 3, 1, 0, 0, ((w)->ctx, 3, 1)) < 0 ? -1 : 0;
    case 3: {
        if (push_packet_001ABF90(s, w) < 0)
            return -1;
        /* u16 countdown at +0x18, decremented only while nonzero. */
        uint16_t count = task_u16(user, EM_SCENE_TASK_18);
        if (count != 0)
            em_scene_task_set_u16(user, EM_SCENE_TASK_18, (uint16_t)(count - 1));
        if (read_0028A9A0(s, w, &fade) < 0)
            return -1;
        if (fade != 0)
            return 0;
        if (task_u16(user, EM_SCENE_TASK_18) != 0 && (s->d810E74 & 0x40) == 0) /* CROSS */
            return 0;
        if (ST_CALL(s, w, F, 0x001AEDE0u, w_001AEDE0, 4, 0, 0, 0, ((w)->ctx, 4, 0)) < 0)
            return -1;
        TB(user, 0x0A) = (uint8_t)(TB(user, 0x0A) + 1);
        return 0;
    }
    case 4:
        if (push_packet_001ABF90(s, w) < 0)
            return -1;
        if (read_0028A9A0(s, w, &fade) < 0)
            return -1;
        if (fade != 2)
            return 0;
        if (ST_CALL0(s, w, F, 0x001FAB50u, w_001FAB50) < 0)
            return -1;
        TB(user, 0x09) = 4;
        TB(user, 0x0A) = 0;
        return 0;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------ 001ADF50 */

/* 001ADF50 (byte-matched): area load, at least 3 ticks. Returns 4 when done,
 * else 0; other +A values do nothing. */
int em_sf_001ADF50(EmSceneState *s, uint8_t *user, const EmSceneWorkers *w)
{
    enum { F = 0x001ADF50u };
    int r;
    if (core_begin(s, user, F) < 0)
        return -1;
    switch (TB(user, 0x0A)) {
    case 0:
        if (ST_CALL(s, w, F, 0x001AED80u, w_001AED80, 0, 0, 0, 0, ((w)->ctx, 0)) < 0)
            return -1;
        TB(user, 0x0A) = (uint8_t)(TB(user, 0x0A) + 1);
        s->d275BD8 = 1;
        if (ST_CALL(s, w, F, 0x001FF080u, w_001FF080, 1, 0, 0, 0, ((w)->ctx, 1, 0)) < 0)
            return -1;
        return ST_CALL0(s, w, F, 0x0021B180u, w_0021B180) < 0 ? -1 : 0;
    case 1:
        /* 0021B550's result is ignored here; BD8 is read after it returns. */
        if (ST_CALL0(s, w, F, 0x0021B550u, w_0021B550) < 0)
            return -1;
        if (s->d275BD8 != 0)
            return 0;
        if (ST_CALL0(s, w, F, 0x0021B840u, w_0021B840) < 0)
            return -1;
        TB(user, 0x0A) = (uint8_t)(TB(user, 0x0A) + 1);
        return 0;
    case 2:
        r = ST_CALL0(s, w, F, 0x0021B550u, w_0021B550);
        if (r <= 0)
            return r < 0 ? -1 : 0;
        if (ST_CALL(s, w, F, 0x001D2830u, w_001D2830, 3, 1, 0, 0, ((w)->ctx, 3, 1)) < 0)
            return -1;
        if (ST_CALL(s, w, F, 0x001AEDB0u, w_001AEDB0, 0, 0, 0, 0, ((w)->ctx, 0)) < 0)
            return -1;
        return 4;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------ 001AD360 */

/* 001AD360 step 4 (also reached from step 3 by falling through the case). */
static int bringup_step4_001AD360(EmSceneState *s, uint8_t *user, const EmSceneWorkers *w)
{
    enum { F = 0x001AD360u };
    uint8_t *entry;
    if (ST_CALL(s, w, F, 0x001D2830u, w_001D2830, 3, 1, 0, 0, ((w)->ctx, 3, 1)) < 0)
        return -1;
    s->d810700 = 0x0B;
    s->d810701 = 0;
    s->d810702 = 0;
    /* D_00810730[D_00810700] = D_00810701, both reloaded (lbu). */
    entry = em_scene_d810730_at(s, s->d810700);
    if (!entry)
        return em_scene_fault(s, F, EM_SCENE_FAULT_BAD_INDEX);
    *entry = s->d810701;
    TB(user, 0x0A) = (uint8_t)(TB(user, 0x0A) + 1);
    return 0;
}

/* 001AD360 (NEARMISS C; the .s jump table 0x26DCD0 has 6 entries, +A >= 6
 * returns 0). New Game bring-up; returns 4 at step 5, else 0. */
int em_sf_001AD360(EmSceneState *s, uint8_t *user, const EmSceneWorkers *w)
{
    enum { F = 0x001AD360u };
    uint8_t busy;
    if (core_begin(s, user, F) < 0)
        return -1;
    switch (TB(user, 0x0A)) {
    case 0:
        if (ST_CALL0(s, w, F, 0x001D1EF0u, w_001D1EF0) < 0)
            return -1;
        if (ST_CALL0(s, w, F, 0x001FABB0u, w_001FABB0) < 0)
            return -1;
        TB(user, 0x0A) = (uint8_t)(TB(user, 0x0A) + 1);
        return 0;
    case 1:
        if (ST_CALL0(s, w, F, 0x001D1EF0u, w_001D1EF0) < 0)
            return -1;
        if (read_00282157(s, w, &busy) < 0)
            return -1;
        if (busy != 0)
            return 0;
        /* .s store order: D_00275C78 = 0, then D_00821058 = 1 (intro movie). */
        if (em_scene_reader_ready(s, w, 0x00275C78u, w != NULL && w->s_00275C78 != NULL) < 0 ||
            store_result(s, 0x00275C78u, w->s_00275C78(w->ctx, 0)) < 0)
            return -1;
        if (em_scene_reader_ready(s, w, 0x00821058u, w != NULL && w->s_00821058 != NULL) < 0 ||
            store_result(s, 0x00821058u, w->s_00821058(w->ctx, 1)) < 0)
            return -1;
        TB(user, 0x0A) = (uint8_t)(TB(user, 0x0A) + 1);
        return 0;
    case 2:
        if (ST_CALL0(s, w, F, 0x001D1EF0u, w_001D1EF0) < 0)
            return -1;
        TB(user, 0x0A) = (uint8_t)(TB(user, 0x0A) + 1);
        return 0;
    case 3:
        /* Step 3 falls into step 4, so +A advances twice in this tick. */
        TB(user, 0x0A) = (uint8_t)(TB(user, 0x0A) + 1);
        em_scene_task_set_u16(user, EM_SCENE_TASK_18, 0);
        TB(user, 0x10) = 0;
        return bringup_step4_001AD360(s, user, w);
    case 4:
        return bringup_step4_001AD360(s, user, w);
    case 5:
        if (ST_CALL0(s, w, F, 0x001D1EF0u, w_001D1EF0) < 0)
            return -1;
        return 4;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------ 001AD250 */

/* 001AD250 (byte-matched): dispatch on +9 through the 6-entry jump table at
 * 0x26DCB0; +9 >= 6 does nothing. */
int em_sf_001AD250(EmSceneState *s, uint8_t *user, const EmSceneWorkers *w)
{
    enum { F = 0x001AD250u };
    int r;
    if (core_begin(s, user, F) < 0)
        return -1;
    switch (TB(user, 0x09)) {
    case 0:
        if (core_call(s, w, F, 0x001AD360u, 0, 0, 0) < 0)
            return -1;
        r = em_sf_001AD360(s, user, w);
        if (r <= 0)
            return r < 0 ? -1 : 0;
        TB(user, 0x09) = 5;
        TB(user, 0x0A) = 0;
        TB(user, 0x0B) = 0;
        return ST_CALL(s, w, F, 0x001AEDB0u, w_001AEDB0, 0, 0, 0, 0, ((w)->ctx, 0)) < 0 ? -1 : 0;
    case 1:
        /* 001AD4D0 tail-jumps to 0x1AE040 (the frame machine). */
        return ST_CALL0(s, w, F, 0x001AD4D0u, w_001AD4D0) < 0 ? -1 : 0;
    case 2:
        if (core_call(s, w, F, 0x001AD4E0u, 0, 0, 0) < 0)
            return -1;
        return em_sf_001AD4E0(s, user, w) < 0 ? -1 : 0;
    case 3:
        return ST_CALL0(s, w, F, 0x001AD740u, w_001AD740) < 0 ? -1 : 0;
    case 4:
        if (core_call(s, w, F, 0x001ADF00u, 0, 0, 0) < 0)
            return -1;
        return em_sf_001ADF00(s, w) < 0 ? -1 : 0;
    case 5:
        if (core_call(s, w, F, 0x001ADF50u, 0, 0, 0) < 0)
            return -1;
        r = em_sf_001ADF50(s, user, w);
        if (r <= 0)
            return r < 0 ? -1 : 0;
        TB(user, 0x09) = 1;
        TB(user, 0x0A) = 0;
        TB(user, 0x0B) = 0;
        return 0;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------ 001ACEC0 */

/* 001ACEC0 (NEARMISS C; semantics checked against the splat .s): the slot-0
 * game task. Writes spad 3B90 = 2 every tick, then dispatches on +8;
 * +8 >= 4 does nothing. */
int em_sf_001ACEC0(EmSceneState *s, uint8_t *user, const EmSceneWorkers *w)
{
    enum { F = 0x001ACEC0u };
    int r;
    if (core_begin(s, user, F) < 0)
        return -1;
    s->spad3B90 = 2;
    switch (TB(user, 0x08)) {
    case 0:
        r = ST_CALL0(s, w, F, 0x001AD1A0u, w_001AD1A0);
        if (r < 0)
            return -1;
        if (r != 0) {
            TB(user, 0x08) = s->d275BE0 == 0 ? 1 : 2;
            TB(user, 0x09) = 0;
            TB(user, 0x0A) = 0;
        }
        return ST_CALL0(s, w, F, 0x001D1EF0u, w_001D1EF0) < 0 ? -1 : 0;
    case 1:
        r = ST_CALL0(s, w, F, 0x001AD230u, w_001AD230);
        if (r <= 0)
            return r < 0 ? -1 : 0;
        TB(user, 0x08) = 3;
        TB(user, 0x09) = 0;
        TB(user, 0x0A) = 0;
        TB(user, 0x0B) = 0;
        return 0;
    case 2:
        TB(user, 0x08) = 3;
        TB(user, 0x09) = 5;
        TB(user, 0x0A) = 0;
        TB(user, 0x0B) = 0;
        return 0;
    case 3:
        if (core_call(s, w, F, 0x001AD250u, 0, 0, 0) < 0)
            return -1;
        return em_sf_001AD250(s, user, w) < 0 ? -1 : 0;
    default:
        return 0;
    }
}
