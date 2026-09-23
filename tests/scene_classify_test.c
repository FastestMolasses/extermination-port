/* WP-3 S1 unit test: EmSceneState accessors and fault latch, the worker call
 * protocol (NULL worker = fault, fail-stop, trace hook), the 001AE7E0
 * translation's decision order, and the lead-decision Q1 input helper.
 * The exhaustive original comparison is tools/test_scene_classify_reference.py;
 * the classifier cases here restate the byte-matched order of
 * Extermination/src/func_001AE7E0.c.
 * Build: cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc
 *        tests/scene_classify_test.c src/game/em_scene_classify.c */
#include "game/em_scene_classify.h"
#include "game/em_scene_workers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x)                                                                     \
    do {                                                                             \
        if (!(x)) {                                                                  \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x);    \
            exit(1);                                                                 \
        }                                                                            \
    } while (0)

static int trace_calls;
static uint32_t trace_last[6];

static void trace(void *ctx, uint32_t caller, uint32_t callee, uint32_t a0, uint32_t a1,
                  uint32_t a2, uint32_t a3)
{
    CHECK(ctx == &trace_calls);
    ++trace_calls;
    trace_last[0] = caller; trace_last[1] = callee;
    trace_last[2] = a0; trace_last[3] = a1; trace_last[4] = a2; trace_last[5] = a3;
}

static int worker_ok(void *ctx) { (void)ctx; return 0; }

static EmSceneState idle(void)
{
    EmSceneState s;
    memset(&s, 0, sizeof s);
    s.d810E50 = 4;
    return s;
}

static void test_layout_and_accessors(void)
{
    EmSceneState s = idle();
    static const struct { EmSceneReqByte b; uint32_t address; } names[] = {
        {EM_SCENE_REQ_B0, 0x8106B0}, {EM_SCENE_REQ_B1, 0x8106B1}, {EM_SCENE_REQ_B3, 0x8106B3},
        {EM_SCENE_REQ_B5, 0x8106B5}, {EM_SCENE_REQ_B6, 0x8106B6}, {EM_SCENE_REQ_B7, 0x8106B7},
        {EM_SCENE_REQ_B8, 0x8106B8}, {EM_SCENE_REQ_B9, 0x8106B9}, {EM_SCENE_REQ_C4, 0x8106C4},
        {EM_SCENE_REQ_C5, 0x8106C5}, {EM_SCENE_REQ_C6, 0x8106C6}, {EM_SCENE_REQ_C7, 0x8106C7},
        {EM_SCENE_REQ_CE, 0x8106CE}, {EM_SCENE_REQ_CF, 0x8106CF}, {EM_SCENE_REQ_D5, 0x8106D5},
        {EM_SCENE_REQ_EF, 0x8106EF}, {EM_SCENE_REQ_F3, 0x8106F3}, {EM_SCENE_REQ_F5, 0x8106F5},
    };
    CHECK(sizeof s.req == 0x48); /* 001AFCF0 memset length */
    CHECK(sizeof s.d810730 == 0x20);
    for (size_t i = 0; i < sizeof names / sizeof names[0]; ++i) {
        em_scene_req_set(&s, names[i].b, (uint8_t)(0x40 + i));
        CHECK(em_scene_req_at(&s, names[i].address) == &s.req[names[i].b]);
        CHECK(em_scene_req_get(&s, names[i].b) == 0x40 + i);
    }
    CHECK(em_scene_req_at(&s, 0x8106AF) == NULL);
    CHECK(em_scene_req_at(&s, 0x8106B0) == &s.req[0]);
    CHECK(em_scene_req_at(&s, 0x8106F7) == &s.req[0x47]);
    CHECK(em_scene_req_at(&s, 0x8106F8) == NULL);
    CHECK(em_scene_d810730_at(&s, 0x1F) == &s.d810730[0x1F]);
    CHECK(em_scene_d810730_at(&s, 0x20) == NULL);
    CHECK(em_scene_d810730_at(&s, 0xFF) == NULL);

    uint8_t user[24];
    memset(user, 0, sizeof user);
    CHECK(em_scene_task_byte(user, 0x07) == NULL);
    CHECK(em_scene_task_byte(user, EM_SCENE_TASK_08) == &user[0]);
    CHECK(em_scene_task_byte(user, EM_SCENE_TASK_0B) == &user[3]);
    CHECK(em_scene_task_byte(user, EM_SCENE_TASK_11) == &user[9]);
    CHECK(em_scene_task_byte(user, 0x1F) == &user[23]);
    CHECK(em_scene_task_byte(user, 0x20) == NULL);
    CHECK(em_scene_task_byte(NULL, 0x08) == NULL);
    int ok = -1;
    CHECK(em_scene_task_set_u16(user, EM_SCENE_TASK_18, 0x00F0) == 0);
    CHECK(user[0x10] == 0xF0 && user[0x11] == 0x00); /* little-endian, as on the EE */
    CHECK(em_scene_task_u16(user, EM_SCENE_TASK_18, &ok) == 0x00F0 && ok == 1);
    CHECK(em_scene_task_set_u16(user, 0x19, 1) == -1);
    CHECK(em_scene_task_set_u16(user, 0x20, 1) == -1);
    CHECK(em_scene_task_set_u16(user, 0x1E, 0xBEEF) == 0 && user[22] == 0xEF && user[23] == 0xBE);
    CHECK(em_scene_task_u16(user, 0x19, &ok) == 0 && ok == 0);
    CHECK(em_scene_task_u16(user, 0x06, &ok) == 0 && ok == 0);
}

static void test_faults_and_workers(void)
{
    EmSceneState s = idle();
    CHECK(!em_scene_faulted(&s));
    CHECK(em_scene_fault(&s, 0x1AD740, EM_SCENE_FAULT_NULL_WORKER) == -1);
    CHECK(em_scene_fault(&s, 0x22A650, EM_SCENE_FAULT_WORKER_FAILED) == -1);
    CHECK(s.fault.address == 0x1AD740 && s.fault.code == EM_SCENE_FAULT_NULL_WORKER);

    EmSceneWorkers w;
    memset(&w, 0, sizeof w);
    w.ctx = &trace_calls;
    w.trace = trace;
    w.w_001FBC50 = worker_ok;

    /* NULL table and NULL worker both fault at the callee. */
    s = idle();
    CHECK(em_scene_worker_enter(&s, NULL, 0x1AE040, 0x1FBC50, 1, 0, 0, 0, 0) == -1);
    CHECK(s.fault.address == 0x1FBC50 && s.fault.code == EM_SCENE_FAULT_NULL_WORKER);
    s = idle();
    CHECK(em_scene_worker_enter(&s, &w, 0x1AE040, 0x22A650, w.w_0022A650 != NULL, 0, 0, 0, 0) == -1);
    CHECK(s.fault.address == 0x22A650 && s.fault.code == EM_SCENE_FAULT_NULL_WORKER);
    /* Fail-stop: after a fault even a present worker is not entered or traced. */
    trace_calls = 0;
    CHECK(em_scene_worker_enter(&s, &w, 0x1AE040, 0x1FBC50, w.w_001FBC50 != NULL, 0, 0, 0, 0) == -1);
    CHECK(trace_calls == 0 && s.fault.address == 0x22A650);

    /* A present worker is traced with caller, callee and the call-site args. */
    s = idle();
    CHECK(em_scene_worker_enter(&s, &w, 0x1AE040, 0x1FB9F0, 1, 0xC, 0x1000, 0x1000, 0x1000) == 0);
    CHECK(trace_calls == 1 && trace_last[0] == 0x1AE040 && trace_last[1] == 0x1FB9F0 &&
          trace_last[2] == 0xC && trace_last[5] == 0x1000);
    CHECK(em_scene_worker_leave(&s, 0x1FB9F0, 0) == 0);
    CHECK(em_scene_worker_leave(&s, 0x22A650, 3) == 3);
    CHECK(!em_scene_faulted(&s));
    CHECK(em_scene_worker_leave(&s, 0x20CDC0, -1) == -1);
    CHECK(s.fault.address == 0x20CDC0 && s.fault.code == EM_SCENE_FAULT_WORKER_FAILED);

    /* The trace hook is optional. */
    s = idle();
    w.trace = NULL;
    CHECK(em_scene_worker_enter(&s, &w, 0x1AE040, 0x1FBC50, 1, 0, 0, 0, 0) == 0);

    /* Readers: NULL faults at the data address; not traced. */
    s = idle();
    CHECK(em_scene_reader_ready(&s, &w, 0x28A9A0, w.r_0028A9A0 != NULL) == -1);
    CHECK(s.fault.address == 0x28A9A0 && s.fault.code == EM_SCENE_FAULT_NULL_WORKER);
}

static int classify(EmSceneState s, int16_t fade)
{
    EmSceneState before = s;
    int r = em_sf_001AE7E0(&s, fade);
    CHECK(memcmp(&before, &s, sizeof s) == 0);
    return r;
}

static void test_classifier_order(void)
{
    EmSceneState s = idle();
    CHECK(classify(s, 0) == 0);
    s.d810E74 = 0x800; CHECK(classify(s, 0) == 2); /* START */
    s.d810E74 = 0x10; CHECK(classify(s, 0) == 2);  /* TRIANGLE */
    s.d810E74 = 0x40; CHECK(classify(s, 0) == 0);  /* CROSS is not tested */
    s.d810E74 = 0x100; CHECK(classify(s, 0) == 1); /* SELECT */
    s.d810E74 = 0x900; CHECK(classify(s, 0) == 1); /* SELECT before START */
    /* E50 != 4 -> 1, tested before B3. */
    s = idle(); s.d810E50 = 7; s.req[EM_SCENE_REQ_B3] = 1; CHECK(classify(s, 0) == 1);
    /* B3 blocks START/TRIANGLE but not SELECT. */
    s = idle(); s.req[EM_SCENE_REQ_B3] = 1; s.d810E74 = 0x810; CHECK(classify(s, 0) == 0);
    s.d810E74 = 0x100; CHECK(classify(s, 0) == 1);
    /* Fade and selector block the input arms... */
    s = idle(); s.d810E74 = 0x900; CHECK(classify(s, 2) == 0); CHECK(classify(s, -1) == 0);
    s.spad3B8D = 2; CHECK(classify(s, 0) == 0);
    /* ...but not CE (3) or C5/B0 (2): design section 1 correction of A3. */
    s = idle(); s.spad3B8D = 4; s.req[EM_SCENE_REQ_CE] = 1; CHECK(classify(s, 3) == 3);
    s = idle(); s.spad3B8D = 1; s.req[EM_SCENE_REQ_C5] = 1; CHECK(classify(s, 2) == 2);
    s = idle(); s.req[EM_SCENE_REQ_B0] = 1; s.req[EM_SCENE_REQ_CE] = 2; CHECK(classify(s, 0) == 3);
    /* B8 and B9 win over everything. */
    s = idle(); s.req[EM_SCENE_REQ_CE] = 1; s.req[EM_SCENE_REQ_B8] = 2; CHECK(classify(s, 0) == 0);
    s = idle(); s.req[EM_SCENE_REQ_B0] = 1; s.req[EM_SCENE_REQ_B9] = 1; CHECK(classify(s, 0) == 0);
    /* Held word E70 is not an input. */
    s = idle(); s.d810E70 = 0xFFFF; CHECK(classify(s, 0) == 0);
}

static void test_q1(void)
{
    CHECK(em_scene_q1_classifier_e74(0x0100) == 0x0000);
    CHECK(em_scene_q1_classifier_e74(0x0900) == 0x0800);
    CHECK(em_scene_q1_classifier_e74(0xFFFF) == 0xFEFF);
    CHECK(em_scene_q1_classifier_e74(0x0850) == 0x0850);
    CHECK(strcmp(EM_SCENE_Q1_UNPORTED_MESSAGE, "unported: 0022A650 (SELECT)") == 0);

    EmSceneState s = idle();
    int withheld = -1;
    s.d810E74 = 0x100;
    EmSceneState before = s;
    CHECK(em_scene_classify_q1(&s, 0, &withheld) == 0 && withheld == 1);
    CHECK(memcmp(&before, &s, sizeof s) == 0 && s.d810E74 == 0x100); /* canonical kept */
    s.d810E74 = 0x900;
    CHECK(em_scene_classify_q1(&s, 0, &withheld) == 2 && withheld == 1);
    CHECK(em_sf_001AE7E0(&s, 0) == 1); /* the faithful classifier is unchanged */
    s.d810E74 = 0x800;
    CHECK(em_scene_classify_q1(&s, 0, &withheld) == 2 && withheld == 0);
    CHECK(em_scene_classify_q1(&s, 0, NULL) == 2);
    s = idle(); s.d810E50 = 0; /* E50 != 4 is not withheld */
    CHECK(em_scene_classify_q1(&s, 0, &withheld) == 1 && withheld == 0);
}

int main(void)
{
    test_layout_and_accessors();
    test_faults_and_workers();
    test_classifier_order();
    test_q1();
    printf("scene_classify_test: PASS (state accessors, fault latch, worker protocol, "
           "001AE7E0 order, Q1 helper)\n");
    return 0;
}
