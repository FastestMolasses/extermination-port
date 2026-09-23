/* Fail-stop contract of em_roger_actor_original under ASan/UBSan.
 *
 * Behaviour is proven by tools/test_roger_actor_original_reference.py
 * against the original instructions; this test only pins what the oracle
 * cannot see from outside: NULL workers and views, addresses outside the
 * views, the bone bound, the lifecycle precondition and the latched fault.
 * The world here is synthetic bookkeeping (slot addresses, one model record
 * with a bone count), not original data. */
#include "game/em_roger_actor_original.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) \
    do { if (!(cond)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); } } while (0)

enum { SLOTS = 80, STACK_BASE = 0x1000, SLOT_BASE = 0x10000, MODEL = 0x40000 };

typedef struct {
    uint8_t slots[SLOTS * EM_ROGER_ACTOR_SLOT_BYTES];
    uint32_t stack[SLOTS];
    int16_t bcc;
    uint32_t bd0;
    uint32_t table[0x100];
    uint8_t d758, d788, d700, activity[EM_ROGER_ACTOR_ACTIVITY_COUNT];
    uint32_t b40[4], spad[4];
    uint8_t model[16];
    int calls;
} Fixture;

static const uint8_t *resource(void *ctx, uint32_t address, uint32_t size)
{
    Fixture *f = ctx;
    return address == MODEL && size <= sizeof f->model ? f->model : NULL;
}

static int ok_clip(void *ctx, EmRogerActorRecord *a, int32_t clip) { (void)a; (void)clip; ++((Fixture *)ctx)->calls; return 0; }
static int ok_count(void *ctx, uint8_t n) { (void)n; ++((Fixture *)ctx)->calls; return 0; }
static int ok_spawn(void *ctx, uint32_t o, int32_t k) { (void)o; (void)k; ++((Fixture *)ctx)->calls; return 0; }
static int ok_actor(void *ctx, EmRogerActorRecord *a) { (void)a; ++((Fixture *)ctx)->calls; return 0; }
static int bad_actor(void *ctx, EmRogerActorRecord *a) { (void)ctx; (void)a; return -1; }
static int ok_bind(void *ctx, EmRogerActorRecord *a, uint32_t a1, int32_t a2, int32_t a3, int32_t *r)
{
    (void)a; (void)a1; (void)a2; (void)a3;
    ++((Fixture *)ctx)->calls;
    *r = 0;
    return 0;
}
static int odd_bind(void *ctx, EmRogerActorRecord *a, uint32_t a1, int32_t a2, int32_t a3, int32_t *r)
{
    (void)ctx; (void)a; (void)a1; (void)a2; (void)a3;
    *r = 2;
    return 0;
}

static void setup(Fixture *f, EmRogerActor *s, uint8_t bones)
{
    memset(f, 0, sizeof *f);
    for (int i = 0; i < SLOTS; ++i) f->stack[i] = SLOT_BASE + (uint32_t)i * EM_ROGER_ACTOR_SLOT_BYTES;
    f->bcc = SLOTS;
    f->bd0 = STACK_BASE;
    f->table[0x47] = MODEL;
    f->model[8] = bones;
    f->b40[0] = SLOT_BASE;
    memset(s, 0, sizeof *s);
    EmRogerActorWorld *w = &s->world;
    w->d00275BCC = &f->bcc;
    w->d00275BD0 = &f->bd0;
    w->slot_stack = f->stack;
    w->slot_stack_base = STACK_BASE;
    w->slot_stack_words = SLOTS;
    w->slots = f->slots;
    w->slots_base = SLOT_BASE;
    w->slots_size = sizeof f->slots;
    w->d0028A490 = f->table;
    w->d0028A490_count = 0x100;
    w->d00810758 = &f->d758;
    w->d00810788 = &f->d788;
    w->d00810700 = &f->d700;
    w->d008106D4 = f->activity;
    w->d00275B40 = f->b40;
    w->d00275B40_count = 4;
    w->spad3600 = f->spad;
    w->resource = resource;
    w->resource_ctx = f;
    EmRogerActorWorkers *k = &s->workers;
    k->ctx = f;
    k->w_001C63E0 = ok_clip;
    k->w_001CB5B0 = ok_count;
    k->w_001F0120 = ok_spawn;
    k->w_001DA6A0 = ok_actor;
    k->w_001BA7F0 = ok_actor;
    k->w_001D0720 = ok_actor;
    k->w_001B1020 = ok_bind;
    k->w_draw = ok_actor;
    k->w_001AFC10 = ok_actor;
}

static EmRogerActorRecord roger(void)
{
    EmRogerActorRecord r;
    memset(&r, 0, sizeof r);
    r.address = 0x7A8830;
    r.w14 = 0x7A8830;
    r.kind = 0x47;
    r.cls = 0x8A;
    return r;
}

int main(void)
{
    static Fixture f;
    EmRogerActor s;

    /* The whole init path runs on consistent views, with no fault. */
    setup(&f, &s, 21);
    EmRogerActorRecord r = roger();
    CHECK(em_roger_actor_008237E0_init(&s, &r) == 0 && s.fault.code == 0);
    CHECK(r.lifecycle == 1 && r.status == 1 && r.bones_held == 21 && r.face != 0);

    /* Wrong lifecycle: fault, nothing written. */
    setup(&f, &s, 21);
    r = roger();
    r.lifecycle = 1;
    EmRogerActorRecord before = r;
    CHECK(em_roger_actor_008237E0_init(&s, &r) == -1 && s.fault.code == EM_ROGER_ACTOR_FAULT_BAD_INDEX);
    CHECK(memcmp(&r, &before, sizeof r) == 0);

    /* Each unconditional init worker missing: fault before any write. */
    for (int which = 0; which < 3; ++which) {
        setup(&f, &s, 21);
        if (which == 0) s.workers.w_001C63E0 = NULL;
        if (which == 1) s.workers.w_001CB5B0 = NULL;
        if (which == 2) s.workers.w_001F0120 = NULL;
        r = roger();
        before = r;
        CHECK(em_roger_actor_008237E0_init(&s, &r) == -1);
        CHECK(s.fault.code == EM_ROGER_ACTOR_FAULT_NULL_WORKER);
        CHECK(memcmp(&r, &before, sizeof r) == 0 && f.bcc == SLOTS && f.bd0 == STACK_BASE && f.calls == 0);
        /* Latched: the next call refuses without touching anything. */
        CHECK(em_roger_actor_008237E0_init(&s, &r) == -1 && memcmp(&r, &before, sizeof r) == 0);
    }

    /* A bone count past the modelled +0x110 words faults before any write. */
    setup(&f, &s, EM_ROGER_ACTOR_MAX_BONES + 1);
    r = roger();
    before = r;
    CHECK(em_roger_actor_001B10B0(&s, &r, 0x47, -1) == -1 && s.fault.code == EM_ROGER_ACTOR_FAULT_BAD_INDEX);
    CHECK(memcmp(&r, &before, sizeof r) == 0 && f.bcc == SLOTS);

    /* A slot stack shorter than the pops faults before any write. */
    setup(&f, &s, 21);
    s.world.slot_stack_words = 10;
    r = roger();
    before = r;
    CHECK(em_roger_actor_001B10B0(&s, &r, 0x47, -1) == -1 && s.fault.code == EM_ROGER_ACTOR_FAULT_BAD_INDEX);
    CHECK(memcmp(&r, &before, sizeof r) == 0 && f.bcc == SLOTS);

    /* An unknown resource address faults (001C6150). */
    setup(&f, &s, 21);
    f.table[0x47] = MODEL + 4;
    r = roger();
    CHECK(em_roger_actor_001B10B0(&s, &r, 0x47, -1) == -1 && s.fault.address == 0x001C6150u);

    /* Table index outside the view. */
    setup(&f, &s, 21);
    r = roger();
    CHECK(em_roger_actor_001B10B0(&s, &r, 0x100, -1) == -1 && s.fault.address == 0x0028A490u);

    /* 001AF890 of an address outside the slot view. */
    setup(&f, &s, 21);
    CHECK(em_roger_actor_001AF890(&s, SLOT_BASE - 0x10) == -1 && s.fault.code == EM_ROGER_ACTOR_FAULT_BAD_INDEX);
    CHECK(f.bcc == SLOTS && f.bd0 == STACK_BASE);

    /* Face update with a face attached but no face slot in view: fault
     * before 001DA6A0 runs and before the activity byte is consumed. */
    setup(&f, &s, 21);
    r = roger();
    r.face_active = 1;
    r.face = 0x9999;
    f.activity[1] = 1;
    CHECK(em_roger_actor_001BA580(&s, &r, 0x47) == -1 && f.calls == 0 && f.activity[1] == 1);

    /* Face update: a failing 001DA6A0 faults (WORKER_FAILED). */
    setup(&f, &s, 21);
    s.workers.w_001DA6A0 = bad_actor;
    r = roger();
    r.face_active = 1;
    r.face = SLOT_BASE;
    CHECK(em_roger_actor_001BA580(&s, &r, 0x47) == -1 && s.fault.code == EM_ROGER_ACTOR_FAULT_WORKER_FAILED);

    /* Equipment: a parent view that is not +0x18, and a 001B1020 result
     * outside {0, 1}. */
    setup(&f, &s, 21);
    EmRogerActorRecord e = roger(), p = roger();
    e.address = 0x7A8B20;
    e.kind = 0x6B;
    e.parent = 0x7A8830;
    p.address = 0x7A8540;
    CHECK(em_roger_actor_001C5C90(&s, &e, &p) == -1 && s.fault.code == EM_ROGER_ACTOR_FAULT_BAD_RESULT);
    setup(&f, &s, 21);
    s.workers.w_001B1020 = odd_bind;
    p.address = 0x7A8830;
    CHECK(em_roger_actor_001C5C90(&s, &e, &p) == -1 && s.fault.code == EM_ROGER_ACTOR_FAULT_BAD_RESULT);

    /* Equipment state 1 with the draw worker missing while the parent is
     * drawn: fault before any record or scratch write. */
    setup(&f, &s, 21);
    s.workers.w_draw = NULL;
    e.lifecycle = 1;
    p.lifecycle = 1;
    p.bones_held = 21;
    p.drawn = 1;
    p.bone[1] = SLOT_BASE + EM_ROGER_ACTOR_SLOT_BYTES;
    before = e;
    CHECK(em_roger_actor_001C5C90(&s, &e, &p) == -1 && s.fault.code == EM_ROGER_ACTOR_FAULT_NULL_WORKER);
    CHECK(memcmp(&e, &before, sizeof e) == 0 && f.spad[0] == 0 && f.spad[1] == 0);

    /* Equipment state 1, parent drawn, +0x4C not 001CAA00: w_draw stands
     * for 001CAA00 only, so fault (BAD_RESULT) before any write or call. */
    setup(&f, &s, 21);
    e.draw = EM_ROGER_ACTOR_DRAW_001CAA00 + 4u;
    before = e;
    CHECK(em_roger_actor_001C5C90(&s, &e, &p) == -1 && s.fault.code == EM_ROGER_ACTOR_FAULT_BAD_RESULT);
    CHECK(s.fault.address == EM_ROGER_ACTOR_DRAW_001CAA00 + 4u && f.calls == 0);
    CHECK(memcmp(&e, &before, sizeof e) == 0 && f.spad[0] == 0 && f.spad[1] == 0);

    /* NULL state / record. */
    CHECK(em_roger_actor_001BA540(NULL, &r) == -1);
    setup(&f, &s, 21);
    CHECK(em_roger_actor_001BA540(&s, NULL) == -1 && s.fault.code == EM_ROGER_ACTOR_FAULT_NULL_WORKER);

    puts("roger actor original: fail-stop contract PASS");
    return 0;
}
