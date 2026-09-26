#include "game/em_area_script.h"

#include <string.h>

#include "game/em_cinematic_playback.h"
#include "game/em_effect_color.h"
#include "game/em_interaction_cinematic.h"
#include "game/em_interaction_frame.h"
#include "game/em_pose_math.h"

/* Handler results use 001BA1F0's numbering (EmScriptCommandResult):
 * 0 stay, 1 advance, 2 advance and continue, 3 abort. FAIL is a fault. */
#define FAIL EM_SCRIPT_UNSUPPORTED

typedef struct {
    EmAreaScript *h;
    const EmAreaScriptWorld *w;
    const EmAreaScriptWorkers *k;
    void *ctx;
    EmScript *st;
    unsigned char *rec;
} Op;

static EmScriptCommandResult fault(Op *o, uint32_t address)
{
    if (!o->h->faulted) {
        o->h->faulted = 1;
        o->h->fault_address = address;
        o->h->fault_pc = o->st->pc;
    }
    return FAIL;
}

/* A reached NULL pointer or worker, or a negative worker result. */
#define NEED(ptr, address) do { if (!(ptr)) return fault(o, (address)); } while (0)
#define CALL(address, expr) do { if ((expr) < 0) return fault(o, (address)); } while (0)
#define WORKER(name, address) do { if (!o->k->name) return fault(o, (address)); } while (0)

static uint32_t u32(const unsigned char *p, unsigned offset) { return em_script_u32(p, offset); }
static int32_t s32(const unsigned char *p, unsigned offset) { return (int32_t)em_script_u32(p, offset); }
static float f32(const unsigned char *p, unsigned offset) { return em_script_f32(p, offset); }
static int16_t s16(const unsigned char *p, unsigned offset)
{
    return (int16_t)(uint16_t)(p[offset] | p[offset + 1] << 8);
}
static void put32(unsigned char *p, unsigned offset, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) p[offset + i] = (unsigned char)(value >> (8 * i));
}
static void putf(unsigned char *p, unsigned offset, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, 4);
    put32(p, offset, bits);
}
/* Record vectors are four little-endian floats (the host is little-endian,
 * as the exported images already assume). */
static float *rv(unsigned char *rec, unsigned offset) { return (float *)(void *)(rec + offset); }

static uint8_t phase(const Op *o) { return (uint8_t)o->st->phase; }
static void set_phase(Op *o, uint8_t value) { o->st->phase = (o->st->phase & ~0xFF) | value; }

/* 00102948 (lq/sq): four-lane copy. */
static void quad(float *dst, const float *src) { memmove(dst, src, 16); }
/* 001028D0 (VU0 vsub.xyzw): dst = a - b, four lanes, truncating. */
static void vsub(float *dst, const float *a, const float *b)
{
    float out[4];
    for (unsigned i = 0; i < 4; ++i) out[i] = em_effect_float32((double)a[i] - b[i]);
    memcpy(dst, out, 16);
}

/* 3B91 has one storage; EmScript.skip_request is the interpreter's view. */
static int skip_set(Op *o, uint8_t value)
{
    if (!o->w->spad3B91) return 0;
    *o->w->spad3B91 = value;
    o->st->skip_request = value;
    return 1;
}
#define SKIP_SET(value) do { if (!skip_set(o, (value))) return fault(o, 0x70003B91u); } while (0)

/* (1 + sin(pi*t - pi/2)) / 2, the sine ease of 001B8FC0 kind 1 and
 * 001B94F0 kinds 6/7 (FPU: mul, sub, SDK sine, add, div). */
static int ease(Op *o, float t, float *out)
{
    float s;
    if (!o->k->w_0011E2A8) return 0;
    float x = pose_sub(pose_mul(3.14159274101257324f, t), 1.57079637050628662f);
    if (o->k->w_0011E2A8(o->ctx, x, &s) < 0) return 0;
    *out = pose_div(pose_add(1.0f, s), 2.0f);
    return 1;
}

/* ---- 001B81D0: bind the player's cinematic face ---- */
static EmScriptCommandResult face_attach(Op *o)
{
    int idx;
    NEED(o->w->d81078F, 0x0081078Fu);
    if (*o->w->d81078F == 1) idx = 0x18;
    else {
        NEED(o->w->p2FF, 0x008103AFu);
        switch (*o->w->p2FF) {
        case 0x3B: idx = 0x18; break;
        case 0x3E: idx = 0x19; break;
        case 0x3F: idx = 0x1A; break;
        case 0x40: idx = 0x1B; break;
        default: idx = -1; break;
        }
    }
    if (idx != -1) {
        uint32_t bank;
        int32_t result;
        WORKER(r_0028A490, EM_AREA_SCRIPT_D_0028A490);
        CALL(EM_AREA_SCRIPT_D_0028A490,
             o->k->r_0028A490(o->ctx, EM_AREA_SCRIPT_D_0028A490 + 4u * (uint32_t)idx, &bank));
        WORKER(w_001CA700, 0x001CA700u);
        CALL(0x001CA700u, o->k->w_001CA700(o->ctx, EM_AREA_SCRIPT_D_008102B0, bank, 7, &result));
        if (result != 0) {
            WORKER(w_001D06D0, 0x001D06D0u);
            CALL(0x001D06D0u, o->k->w_001D06D0(o->ctx, EM_AREA_SCRIPT_D_008102B0, 1));
            NEED(o->w->spad3B8F, 0x70003B8Fu);
            *o->w->spad3B8F = 2;
        }
    }
    return EM_SCRIPT_ADVANCE; /* not a handler result; callers ignore it */
}

/* ---- 001B82D0 (ftab_0024D880[7]) ----
 * Subcommands 0/2/4/13 are em_interaction_frame and 9..12 are
 * em_interaction_cinematic (the verified translations); this host only
 * adapts them to the canonical storage and the typed workers. Subcommands
 * 1/3/7/8 are 0/2 plus, at the point the original raises them (after the
 * skip byte, before 001D2610), 3B91 = 1 (1/3/8) and 001B81D0 (7/8); the
 * adapter adds exactly those at the module's scope-zero event. Subcommand 5
 * is the event-flag store followed by subcommand 4. Subcommand 6 has no
 * native module and is translated below. */
typedef struct {
    Op *o;
    EmInteractionFrame frame;
    int skip_at_scope, face_at_scope;
    EmScriptCommandResult failure;
} FrameAdapter;

/* The canonical storage the frame modules use; a missing one faults at
 * its own address. */
static int frame_ready(Op *o)
{
    const EmAreaScriptWorld *w = o->w;
    const struct { const void *p; uint32_t address; } need[] = {
        {w->spad3B92, 0x70003B92u}, {w->spad3B8D, 0x70003B8Du}, {w->spad3B8F, 0x70003B8Fu},
        {w->spad3B84, 0x70003B84u}, {w->spad3B91, 0x70003B91u}, {w->d8101E1, 0x008101E1u},
        {w->d8101E2, 0x008101E2u}, {w->d8101E3, 0x008101E3u}, {w->d8101E4, 0x008101E4u},
        {w->d8101E6, 0x008101E6u}, {w->d8106EF, 0x008106EFu}, {w->d8106F3, 0x008106F3u},
        {w->d8106D4, 0x008106D4u}, {w->d2821B4, 0x002821B4u}, {w->d8105F0, 0x008105F0u},
    };
    for (size_t i = 0; i < sizeof need / sizeof need[0]; ++i)
        if (!need[i].p) {
            fault(o, need[i].address);
            return 0;
        }
    return 1;
}

static int frame_load(FrameAdapter *a)
{
    const EmAreaScriptWorld *w = a->o->w;
    if (!frame_ready(a->o)) return 0;
    EmInteractionFrame *f = &a->frame;
    f->selector = *w->spad3B8D;
    f->player_ready = *w->spad3B8F;
    f->ready = *w->spad3B92;
    f->camera_phase = *w->d8101E1;
    f->camera_state = *w->d8101E2;
    f->camera_swing = *w->d8101E3;
    f->camera_top = *w->d8101E4;
    f->camera_mode = *w->d8101E6;
    f->recovery_lock = *w->d8106EF;
    f->auxiliary = *w->d8106F3;
    memcpy(f->activity, w->d8106D4, sizeof f->activity);
    f->counter = *w->spad3B84;
    f->message_phase = *w->d2821B4;
    memcpy(f->up, w->d8105F0, sizeof f->up);
    a->o->st->skip_request = *w->spad3B91;
    return 1;
}

/* Publish the module's writes to the canonical storage. The projection
 * zoom has no canonical byte here: 001D25F0/001D2610 carry it. */
static void frame_store(FrameAdapter *a)
{
    const EmAreaScriptWorld *w = a->o->w;
    const EmInteractionFrame *f = &a->frame;
    *w->spad3B8D = f->selector;
    *w->spad3B8F = f->player_ready;
    *w->spad3B92 = f->ready;
    *w->d8101E1 = f->camera_phase;
    *w->d8101E2 = f->camera_state;
    *w->d8101E3 = f->camera_swing;
    *w->d8101E4 = f->camera_top;
    *w->d8101E6 = f->camera_mode;
    *w->d8106EF = f->recovery_lock;
    *w->d8106F3 = f->auxiliary;
    memcpy(w->d8106D4, f->activity, sizeof f->activity);
    *w->spad3B84 = f->counter;
    *w->d2821B4 = f->message_phase;
    memcpy(w->d8105F0, f->up, sizeof f->up);
    *w->spad3B91 = a->o->st->skip_request;
}

/* Every worker sees the canonical storage as the original callee would:
 * publish before the call, reload after it. */
static int frame_call(FrameAdapter *a, uint32_t address, int result)
{
    if (result < 0) {
        fault(a->o, address);
        return 0;
    }
    return frame_load(a);
}
#define FRAME_CALL(address, expr) \
    (frame_store(a), frame_call(a, (address), (a->o->k->expr)))

static int scope_zero(FrameAdapter *a)
{
    Op *o = a->o;
    if (a->skip_at_scope) o->st->skip_request = 1;
    if (a->face_at_scope) {
        frame_store(a);
        if (face_attach(o) == FAIL || !frame_load(a)) return 0;
    }
    if (!o->k->w_001D2610) { fault(o, 0x001D2610u); return 0; }
    return FRAME_CALL(0x001D2610u, w_001D2610(o->ctx, 0.0f));
}

static int frame_emit(void *context, EmInteractionFrameEvent event)
{
    FrameAdapter *a = context;
    Op *o = a->o;
    const EmAreaScriptWorkers *k = o->k;
    switch (event) {
    case EM_INTERACTION_BARS_ENTER:
        if (!k->w_001AEB60) break;
        return FRAME_CALL(0x001AEB60u, w_001AEB60(o->ctx, 4));
    case EM_INTERACTION_SCOPE_ZOOM_ZERO:
        return scope_zero(a);
    case EM_INTERACTION_BARS_LEAVE:
        if (!k->w_001AEBA0) break;
        return FRAME_CALL(0x001AEBA0u, w_001AEBA0(o->ctx, 4));
    case EM_INTERACTION_RELEASE_SKELETON:
        if (!k->w_001CA770) break;
        return FRAME_CALL(0x001CA770u, w_001CA770(o->ctx, EM_AREA_SCRIPT_D_008102B0));
    case EM_INTERACTION_ZOOM_DEFAULT:
        if (!k->w_001D25F0) break;
        return FRAME_CALL(0x001D25F0u, w_001D25F0(o->ctx, 480.0f));
    case EM_INTERACTION_RESUME_MUSIC:
        if (!k->w_001FAE70) break;
        return FRAME_CALL(0x001FAE70u, w_001FAE70(o->ctx, 0));
    case EM_INTERACTION_FADE_IN:
        if (!k->w_001AEE10) break;
        return FRAME_CALL(0x001AEE10u, w_001AEE10(o->ctx, 4, 0));
    }
    {
        static const uint32_t missing[] = {0x001AEB60u, 0x001D2610u, 0x001AEBA0u, 0x001CA770u,
                                           0x001D25F0u, 0x001FAE70u, 0x001AEE10u};
        fault(o, (unsigned)event < 7 ? missing[event] : 0x001B82D0u);
    }
    return 0;
}

static int cinematic_emit(void *context, EmInteractionCinematicEvent event, int32_t argument)
{
    FrameAdapter *a = context;
    Op *o = a->o;
    const EmAreaScriptWorkers *k = o->k;
    switch (event) {
    case EM_CINEMATIC_FADE_OUT_FOUR:
        if (!k->w_001AEDE0) { fault(o, 0x001AEDE0u); return 0; }
        return FRAME_CALL(0x001AEDE0u, w_001AEDE0(o->ctx, 4, 0));
    case EM_CINEMATIC_REQUEST_STREAM:
        if (!k->w_001FD4C0) { fault(o, 0x001FD4C0u); return 0; }
        return FRAME_CALL(0x001FD4C0u, w_001FD4C0(o->ctx, argument));
    case EM_CINEMATIC_MUTE_CHANNEL:
        if (!k->w_00119828) { fault(o, 0x00119828u); return 0; }
        return FRAME_CALL(0x00119828u, w_00119828(o->ctx, argument, 0, 0));
    case EM_CINEMATIC_BARS_IMMEDIATE:
        if (!k->w_001AEB60) { fault(o, 0x001AEB60u); return 0; }
        return FRAME_CALL(0x001AEB60u, w_001AEB60(o->ctx, 0xFF));
    case EM_CINEMATIC_ATTACH_PLAYER:
        frame_store(a);
        return face_attach(o) != FAIL && frame_load(a);
    case EM_CINEMATIC_SCOPE_ZERO:
        if (!k->w_001D2610) { fault(o, 0x001D2610u); return 0; }
        return FRAME_CALL(0x001D2610u, w_001D2610(o->ctx, 0.0f));
    case EM_CINEMATIC_FADE_IN_SIXTEEN:
        if (!k->w_001AEE10) { fault(o, 0x001AEE10u); return 0; }
        return FRAME_CALL(0x001AEE10u, w_001AEE10(o->ctx, 0x10, 0));
    }
    fault(o, 0x001B82D0u);
    return 0;
}

/* 001B82D0 sub6: story byte, then the sub4 teardown without its
 * camera-mode-3 branch (translated; no native module covers it). */
static EmScriptCommandResult leave_story(Op *o)
{
    const EmAreaScriptWorld *w = o->w;
    NEED(w->d8107D8, 0x008107D8u);
    if (u32(o->rec, 0x14) > 0xFF) return fault(o, 0x008107D8u);
    w->d8107D8[u32(o->rec, 0x14)] = o->rec[0x18];
    NEED(w->spad3B8D, 0x70003B8Du);
    if (*w->spad3B8D == 0) return EM_SCRIPT_ADVANCE;
    NEED(w->d8106F3, 0x008106F3u); NEED(w->d8101E4, 0x008101E4u);
    NEED(w->d8106EF, 0x008106EFu); NEED(w->d8101E1, 0x008101E1u);
    *w->d8106F3 = 0;
    *w->d8101E4 = 0;
    *w->d8101E1 = 0;
    *w->d8106EF = 0x50;
    WORKER(w_001AEBA0, 0x001AEBA0u);
    CALL(0x001AEBA0u, o->k->w_001AEBA0(o->ctx, 4));
    NEED(w->spad3B8F, 0x70003B8Fu); NEED(w->d2821B4, 0x002821B4u);
    uint8_t mode = *w->spad3B8F;
    *w->d2821B4 = 2;
    if (mode == 2) {
        WORKER(w_001CA770, 0x001CA770u);
        CALL(0x001CA770u, o->k->w_001CA770(o->ctx, EM_AREA_SCRIPT_D_008102B0));
        *w->spad3B8F = 1;
    }
    WORKER(w_001D25F0, 0x001D25F0u);
    CALL(0x001D25F0u, o->k->w_001D25F0(o->ctx, 480.0f));
    NEED(w->d8105F0, 0x008105F0u);
    w->d8105F0[0] = 0.0f;
    w->d8105F0[1] = -1.0f;
    w->d8105F0[2] = 0.0f;
    w->d8105F0[3] = 1.0f;
    NEED(w->spad3B91, 0x70003B91u); NEED(w->spad3B92, 0x70003B92u);
    int aborted = *w->spad3B91 == 2 && o->st->skip_phase == 2;
    if (aborted) {
        WORKER(w_001FAE70, 0x001FAE70u);
        CALL(0x001FAE70u, o->k->w_001FAE70(o->ctx, 0));
        WORKER(w_001AEE10, 0x001AEE10u);
        CALL(0x001AEE10u, o->k->w_001AEE10(o->ctx, 4, 0));
    }
    o->st->skip_phase = 0;
    *w->spad3B8D = 0;
    *w->spad3B92 = 0;
    SKIP_SET(0);
    return aborted ? EM_SCRIPT_ABORT : EM_SCRIPT_ADVANCE;
}

static EmScriptCommandResult op07(Op *o)
{
    int32_t sub = s32(o->rec, 8);
    int immediate = u32(o->rec, 0x14) != 0;
    FrameAdapter a = {o, {0}, 0, 0, EM_SCRIPT_WAIT};
    unsigned module_sub;
    EmScriptCommandResult result;
    switch (sub) {
    case 0: case 1: case 13: module_sub = sub == 13 ? 13 : 0; break;
    case 2: case 3: case 7: case 8: module_sub = 2; break;
    case 4: case 5: module_sub = 4; break;
    case 9: case 10: case 11: case 12: module_sub = (unsigned)sub; break;
    case 6: return leave_story(o);
    default: return EM_SCRIPT_WAIT;
    }
    a.skip_at_scope = sub == 1 || sub == 3 || sub == 8;
    a.face_at_scope = sub == 7 || sub == 8;
    if (sub == 5) {
        NEED(o->w->d810758, 0x00810758u);
        if (u32(o->rec, 0x14) > 0xFF) return fault(o, 0x00810758u);
        o->w->d810758[u32(o->rec, 0x14)] = 0xFF;
    }
    if (!frame_load(&a)) return FAIL;
    if (module_sub >= 9 && module_sub <= 12) {
        EmInteractionCinematicInputs input;
        NEED(o->w->d28A9A0, 0x0028A9A0u); NEED(o->w->d8106F4, 0x008106F4u);
        input.fade_phase = *o->w->d28A9A0;
        input.stream_ready = *o->w->d8106F4;
        result = em_interaction_cinematic_command(&a.frame, o->st, module_sub, immediate,
                                                  s32(o->rec, 0x18), &input, cinematic_emit, &a);
    } else {
        result = em_interaction_frame_command(&a.frame, o->st, module_sub, immediate,
                                              frame_emit, &a);
    }
    if (o->h->faulted) return FAIL;
    if (result == EM_SCRIPT_UNSUPPORTED) return fault(o, 0x001B82D0u);
    frame_store(&a);
    return result;
}

/* 001B8FC0 (ftab_0024D880[0]): camera move. arg0 = owner, arg1 = script. */
static EmScriptCommandResult op00(Op *o)
{
    const EmAreaScriptWorld *w = o->w;
    int32_t kind = s32(o->rec, 8);
    NEED(w->d8105D0, 0x008105D0u); NEED(w->d8105E0, 0x008105E0u);
    switch (phase(o)) {
    case 0:
        switch (kind) {
        case 0: case 3:
            NEED(w->cam_10, 0x008101F0u); NEED(w->cam_20, 0x00810200u);
            quad(w->d8105D0, rv(o->rec, 0x20));
            quad(w->d8105E0, rv(o->rec, 0x30));
            quad(w->cam_10, rv(o->rec, 0x20));
            quad(w->cam_20, rv(o->rec, 0x30));
            if (kind == 3) return EM_SCRIPT_ADVANCE;
            break;
        case 1: case 5:
            quad(o->h->st_10, w->d8105D0);
            quad(o->h->st_20, w->d8105E0);
            break;
        case 4: case 7:
            NEED(w->cam_10, 0x008101F0u); NEED(w->cam_20, 0x00810200u);
            NEED(w->p0B0, 0x00810360u);
            quad(w->d8105D0, rv(o->rec, 0x20));
            quad(w->d8105E0, w->p0B0);
            quad(w->cam_10, rv(o->rec, 0x20));
            quad(w->cam_20, w->p0B0);
            break;
        case 9: case 10:
            NEED(w->cam_10, 0x008101F0u); NEED(w->cam_20, 0x00810200u);
            NEED(w->p0B0, 0x00810360u);
            quad(w->d8105D0, rv(o->rec, 0x20));
            quad(w->d8105E0, w->p0B0);
            w->d8105E0[1] = pose_add(w->d8105E0[1], 6.0f);
            quad(w->cam_10, w->d8105D0);
            quad(w->cam_20, w->d8105E0);
            break;
        case 6: {
            uint32_t table, track;
            float head;
            NEED(w->cam_70, 0x00810250u); NEED(w->cam_74, 0x00810254u);
            NEED(w->cam_78, 0x00810258u); NEED(w->d8101E4, 0x008101E4u);
            NEED(w->cam_6E, 0x0081024Eu);
            WORKER(r_0028A490, EM_AREA_SCRIPT_D_0028A490);
            CALL(EM_AREA_SCRIPT_D_0028A490, o->k->r_0028A490(o->ctx,
                 EM_AREA_SCRIPT_D_0028A490 + 4u * u32(o->rec, 0x1C), &table));
            WORKER(w_001C6120, 0x001C6120u);
            CALL(0x001C6120u, o->k->w_001C6120(o->ctx, table, s32(o->rec, 0x14), &track));
            *w->cam_70 = track;
            *w->cam_74 = 0.0f;
            WORKER(r_track_head, 0x001C6120u);
            CALL(0x001C6120u, o->k->r_track_head(o->ctx, track, &head));
            *w->cam_78 = head;
            *w->d8101E4 = 3;
            *w->cam_6E = s16(o->rec, 0x18);
            WORKER(w_0022EC30, 0x0022EC30u);
            CALL(0x0022EC30u, o->k->w_0022EC30(o->ctx, EM_AREA_SCRIPT_D_008101E0));
            return EM_SCRIPT_ADVANCE;
        }
        case 2:
            break;
        default:
            /* Kind 8 and out-of-range kinds are not admitted
             * (docs/AREA_SCRIPT.md): 8 needs 0018C6A0/0018C4B0. */
            return fault(o, 0x001B8FC0u);
        }
        put32(o->rec, 0x10, 0);
        set_phase(o, (uint8_t)(phase(o) + 1));
        break;
    case 1:
        if (kind == 8) return fault(o, 0x001B8FC0u);
        if (!(f32(o->rec, 0x10) < f32(o->rec, 0x0C))) {
            WORKER(w_001DD980, 0x001DD980u);
            CALL(0x001DD980u, o->k->w_001DD980(o->ctx, w->d8105D0, w->d8105E0));
            return EM_SCRIPT_ADVANCE;
        }
        putf(o->rec, 0x10, pose_add(f32(o->rec, 0x10), 1.0f));
        switch (kind) {
        case 7:
            NEED(w->cam_20, 0x00810200u); NEED(w->p0B0, 0x00810360u);
            quad(w->d8105E0, w->p0B0);
            quad(w->cam_20, w->p0B0);
            break;
        case 10:
            NEED(w->cam_20, 0x00810200u); NEED(w->p0B0, 0x00810360u);
            quad(w->d8105E0, w->p0B0);
            w->d8105E0[1] = pose_add(w->d8105E0[1], 6.0f);
            quad(w->cam_20, w->d8105E0);
            break;
        case 1: case 5: {
            float t = pose_div(f32(o->rec, 0x10), f32(o->rec, 0x0C));
            NEED(w->cam_10, 0x008101F0u); NEED(w->cam_20, 0x00810200u);
            if (kind == 1 && !ease(o, t, &t)) return fault(o, 0x0011E2A8u);
            vsub(w->d8105D0, rv(o->rec, 0x20), o->h->st_10);
            vsub(w->d8105E0, rv(o->rec, 0x30), o->h->st_20);
            for (unsigned i = 0; i < 3; ++i)
                w->d8105D0[i] = pose_add(o->h->st_10[i], pose_mul(w->d8105D0[i], t));
            for (unsigned i = 0; i < 3; ++i)
                w->d8105E0[i] = pose_add(o->h->st_20[i], pose_mul(w->d8105E0[i], t));
            quad(w->cam_10, w->d8105D0);
            quad(w->cam_20, w->d8105E0);
            break;
        }
        default:
            break;
        }
        break;
    default:
        break;
    }
    WORKER(w_001DD980, 0x001DD980u);
    CALL(0x001DD980u, o->k->w_001DD980(o->ctx, w->d8105D0, w->d8105E0));
    return EM_SCRIPT_WAIT;
}

/* 001B94F0 (ftab_0024D880[1]): owner/player placement and moves. */
static EmScriptCommandResult op01(Op *o)
{
    const EmAreaScriptWorld *w = o->w;
    int32_t kind = s32(o->rec, 8);
    switch (kind) {
    case 0:
        NEED(w->s0B0, 0x1B94F000u);
        quad(w->s0B0, rv(o->rec, 0x30));
        return EM_SCRIPT_ADVANCE;
    case 1:
        WORKER(w_00182F90, 0x00182F90u);
        CALL(0x00182F90u, o->k->w_00182F90(o->ctx, EM_AREA_SCRIPT_D_008102B0, rv(o->rec, 0x30)));
        return EM_SCRIPT_ADVANCE;
    case 2: case 4: case 6: case 7:
        NEED(w->s0B0, 0x1B94F000u);
        if (phase(o) == 0) {
            set_phase(o, (uint8_t)(phase(o) + 1));
            quad(rv(o->rec, 0x20), w->s0B0);
            putf(o->rec, 0x10, 0.0f);
        } else if (phase(o) != 1) {
            return EM_SCRIPT_WAIT;
        }
        {
            if (!(f32(o->rec, 0x10) < f32(o->rec, 0x0C))) return EM_SCRIPT_ADVANCE;
            putf(o->rec, 0x10, pose_add(f32(o->rec, 0x10), 1.0f));
            float r = pose_div(f32(o->rec, 0x10), f32(o->rec, 0x0C));
            if ((kind == 6 || kind == 7) && !ease(o, r, &r)) return fault(o, 0x0011E2A8u);
            vsub(w->s0B0, rv(o->rec, 0x30), rv(o->rec, 0x20));
            for (unsigned i = 0; i < 3; ++i)
                w->s0B0[i] = pose_add(f32(o->rec, 0x20 + 4 * i), pose_mul(w->s0B0[i], r));
            if (kind == 4 || kind == 7) {
                NEED(w->d8105E0, 0x008105E0u);
                quad(w->d8105E0, w->s0B0);
            }
        }
        return EM_SCRIPT_WAIT;
    case 3: case 5: {
        NEED(w->p0A0, 0x00810350u); NEED(w->p0C0, 0x00810370u);
        NEED(w->p1F2, 0x008104A2u); NEED(w->p25C, 0x0081050Cu);
        NEED(w->p1F8, 0x008104A8u); NEED(w->d24D8F0, 0x0024D8F0u);
        uint8_t p = phase(o);
        if (p == 0) {
            uint32_t index = u32(o->rec, 0x14);
            if (index >= w->d24D8F0_count) return fault(o, 0x0024D8F0u);
            quad(rv(o->rec, 0x20), w->p0A0);
            set_phase(o, (uint8_t)(phase(o) + 1));
            putf(o->rec, 0x10, 0.0f);
            *w->p1F2 = w->d24D8F0[index];
            *w->p25C = o->rec[0x14];
            *w->p1F8 = 4.0f;
            return EM_SCRIPT_WAIT;
        }
        if (p == 1) {
            float t, u;
            WORKER(w_001B1240, 0x001B1240u);
            CALL(0x001B1240u, o->k->w_001B1240(o->ctx, w->p0A0, f32(o->rec, 0x30),
                                               f32(o->rec, 0x38), &t));
            WORKER(w_001B12B0, 0x001B12B0u);
            CALL(0x001B12B0u, o->k->w_001B12B0(o->ctx, t, w->p0C0[1], 0.0698131695389747620f, &u));
            w->p0C0[1] = u;
            if (u != t) return EM_SCRIPT_WAIT;
            set_phase(o, (uint8_t)(phase(o) + 1));
            p = 2;
        }
        if (p != 2) return EM_SCRIPT_WAIT;
        if (!(f32(o->rec, 0x10) < f32(o->rec, 0x0C))) {
            *w->p1F2 = w->d24D8F0[0];
            *w->p25C = 0;
            *w->p1F8 = 4.0f;
            return EM_SCRIPT_ADVANCE;
        }
        putf(o->rec, 0x10, pose_add(f32(o->rec, 0x10), 1.0f));
        float r = pose_div(f32(o->rec, 0x10), f32(o->rec, 0x0C));
        NEED(w->spad3600, 0x70003600u);
        vsub(o->h->st_10, rv(o->rec, 0x30), rv(o->rec, 0x20));
        for (unsigned i = 0; i < 3; ++i)
            w->spad3600[i] = pose_add(f32(o->rec, 0x20 + 4 * i), pose_mul(o->h->st_10[i], r));
        WORKER(w_00182F90, 0x00182F90u);
        CALL(0x00182F90u, o->k->w_00182F90(o->ctx, EM_AREA_SCRIPT_D_008102B0, w->spad3600));
        if (kind == 5) {
            NEED(w->d8105E0, 0x008105E0u); NEED(w->p0B0, 0x00810360u);
            quad(w->d8105E0, w->p0B0);
        }
        return EM_SCRIPT_WAIT;
    }
    case 9:
        /* 001B6F80(rec+0x20, rec+0x34): player yaw, then 00182F90. */
        NEED(w->p0C0, 0x00810370u);
        w->p0C0[1] = f32(o->rec, 0x34);
        WORKER(w_00182F90, 0x00182F90u);
        CALL(0x00182F90u, o->k->w_00182F90(o->ctx, EM_AREA_SCRIPT_D_008102B0, rv(o->rec, 0x20)));
        return EM_SCRIPT_ADVANCE;
    case 10:
        NEED(w->s0B0, 0x1B94F000u); NEED(w->s0C0, 0x1B94F000u);
        quad(w->s0B0, rv(o->rec, 0x20));
        quad(w->s0C0, rv(o->rec, 0x30));
        return EM_SCRIPT_ADVANCE;
    default:
        /* Kind 8 (orbit, 0011DE90) is not admitted. */
        return fault(o, 0x001B94F0u);
    }
}

/* 001B9BA0 (ftab_0024D880[2]): countdown. */
static EmScriptCommandResult op02(Op *o)
{
    if (phase(o) == 0) {
        put32(o->rec, 0x10, u32(o->rec, 0x0C));
        set_phase(o, (uint8_t)(phase(o) + 1));
    } else if (f32(o->rec, 0x10) <= 0.0f) {
        return EM_SCRIPT_ADVANCE;
    } else {
        putf(o->rec, 0x10, pose_sub(f32(o->rec, 0x10), 1.0f));
    }
    return EM_SCRIPT_WAIT;
}

/* 001B9C10 (ftab_0024D880[4]): owner / player Euler components. */
static EmScriptCommandResult op04(Op *o)
{
    const EmAreaScriptWorld *w = o->w;
    uint32_t sub = u32(o->rec, 8);
    switch (sub) {
    case 0: case 1: case 2:
        NEED(w->s0C0, 0x1B9C1000u);
        w->s0C0[sub] = f32(o->rec, 0x20 + 4 * sub);
        w->s0C0[3] = 1.0f;
        break;
    case 3:
        NEED(w->s0C0, 0x1B9C1000u);
        for (unsigned i = 0; i < 3; ++i) w->s0C0[i] = f32(o->rec, 0x20 + 4 * i);
        w->s0C0[3] = 1.0f;
        break;
    case 7: case 8: case 9:
        NEED(w->p0C0, 0x00810370u);
        w->p0C0[sub - 7] = f32(o->rec, 4 * sub + 4);
        w->p0C0[3] = 1.0f;
        break;
    case 10:
        NEED(w->p0C0, 0x00810370u);
        for (unsigned i = 0; i < 3; ++i) w->p0C0[i] = f32(o->rec, 0x20 + 4 * i);
        w->p0C0[3] = 1.0f;
        break;
    default:
        break;
    }
    return EM_SCRIPT_ADVANCE;
}

/* 001BA080 (ftab_0024D880[6]): event flags / counters. */
static EmScriptCommandResult op06(Op *o)
{
    const EmAreaScriptWorld *w = o->w;
    uint32_t slot = u32(o->rec, 0x14);
    /* The original indexes with the whole word; the canonical arrays are
     * the 256-byte progress ranges, so a larger index is not admitted. */
    if (slot > 0xFF) return fault(o, 0x001BA080u);
    switch (u32(o->rec, 8)) {
    case 0: NEED(w->d810758, 0x00810758u); w->d810758[slot] = 1; break;
    case 1: NEED(w->d810758, 0x00810758u); w->d810758[slot] = 0xFF; break;
    case 2:
        NEED(w->d8107D8, 0x008107D8u);
        if (w->d8107D8[slot] == 0) return EM_SCRIPT_WAIT;
        break;
    case 3: NEED(w->d8107D8, 0x008107D8u); w->d8107D8[slot] = o->rec[0x18]; break;
    case 4:
        NEED(w->d8107D8, 0x008107D8u);
        if ((uint32_t)w->d8107D8[slot] != u32(o->rec, 0x18)) return EM_SCRIPT_WAIT;
        break;
    case 5: NEED(w->d8107D8, 0x008107D8u); w->d8107D8[slot]++; break;
    case 6: NEED(w->d8107D8, 0x008107D8u); w->d8107D8[slot]--; break;
    default: break;
    }
    return EM_SCRIPT_ADVANCE;
}

/* 001B99F0 (ftab_0024D880[9]): tail call of the record's callback. */
static EmScriptCommandResult op09(Op *o)
{
    int32_t result;
    WORKER(c_record, 0x001B99F0u);
    CALL(u32(o->rec, 4), o->k->c_record(o->ctx, u32(o->rec, 4), o->h, o->rec, &result));
    return (EmScriptCommandResult)result;
}

/* 001B9A00 (ftab_0024D880[10]): player animation fields. */
static EmScriptCommandResult op0A(Op *o)
{
    const EmAreaScriptWorld *w = o->w;
    uint32_t bank;
    switch (u32(o->rec, 8)) {
    case 4:
    case 1:
        NEED(w->p040, 0x008102F0u); NEED(w->p2F3, 0x008105A3u);
        NEED(w->p1F2, 0x008104A2u); NEED(w->p1F4, 0x008104A4u);
        WORKER(r_0028A490, EM_AREA_SCRIPT_D_0028A490);
        CALL(EM_AREA_SCRIPT_D_0028A490, o->k->r_0028A490(o->ctx,
             EM_AREA_SCRIPT_D_0028A490 + 4u * u32(o->rec, 0x1C), &bank));
        *w->p040 = bank;
        if (u32(o->rec, 8) == 4) {
            *w->p2F3 = 3;
            goto clip;
        }
        NEED(w->p200, 0x008104B0u);
        *w->p1F2 = s16(o->rec, 0x14);
        *w->p2F3 = 1;
        *w->p1F4 = f32(o->rec, 0x0C);
        *w->p200 = 0;
        break;
    case 0:
    clip:
        NEED(w->p1F2, 0x008104A2u); NEED(w->p1F4, 0x008104A4u);
        NEED(w->p1F8, 0x008104A8u);
        *w->p1F2 = s16(o->rec, 0x14);
        *w->p1F8 = f32(o->rec, 0x0C);
        *w->p1F4 = 1.0f;
        break;
    case 2:
        NEED(w->p1F2, 0x008104A2u); NEED(w->p20C, 0x008104BCu);
        NEED(w->p2F3, 0x008105A3u); NEED(w->p040, 0x008102F0u);
        WORKER(r_0028A490, EM_AREA_SCRIPT_D_0028A580);
        CALL(EM_AREA_SCRIPT_D_0028A580, o->k->r_0028A490(o->ctx, EM_AREA_SCRIPT_D_0028A580, &bank));
        *w->p1F2 = 0;
        *w->p20C = -1;
        *w->p2F3 = 3;
        *w->p040 = bank;
        break;
    case 3:
        NEED(w->p200, 0x008104B0u);
        return (*w->p200 & 0x1000) ? EM_SCRIPT_ADVANCE : EM_SCRIPT_WAIT;
    case 5: {
        float bone[4];
        WORKER(r_player_bone_C0, 0x008103C4u);
        CALL(0x008103C4u, o->k->r_player_bone_C0(o->ctx, bone));
        quad(rv(o->rec, 0x30), bone);
        putf(o->rec, 0x34, pose_sub(f32(o->rec, 0x34), 11.0f));
        WORKER(w_00182F90, 0x00182F90u);
        CALL(0x00182F90u, o->k->w_00182F90(o->ctx, EM_AREA_SCRIPT_D_008102B0, rv(o->rec, 0x30)));
        return EM_SCRIPT_ADVANCE;
    }
    case 6:
        NEED(w->p0A0, 0x00810350u); NEED(w->p0B0, 0x00810360u);
        w->p0A0[0] = w->p0B0[0];
        memset(&w->p0A0[1], 0, 4);
        w->p0A0[2] = w->p0B0[2];
        break;
    case 7:
        NEED(w->p1F2, 0x008104A2u); NEED(w->p1F4, 0x008104A4u);
        NEED(w->p1F8, 0x008104A8u);
        *w->p1F2 = s16(o->rec, 0x14);
        *w->p1F8 = f32(o->rec, 0x0C);
        *w->p1F4 = f32(o->rec, 0x10);
        break;
    case 8:
        /* 001798D0 (player re-init) is not admitted. */
        return fault(o, 0x001798D0u);
    default:
        break;
    }
    return EM_SCRIPT_ADVANCE;
}

/* 001B8020 (ftab_0024D880[11]): owner animation. Subs 6, 0 and 4 are
 * admitted (byte-matched src/func_001B8020.c): sub 6 plays 001FBD50(owner,
 * rec +0x18, 0, 300.0) and falls into sub 0, anim_clip_init(owner, (short)
 * rec +0x14, rec +0x0C, 0.0) with st +0x0E = 0 (census L18: the ordinary
 * door program's 0x24DC40); sub 4 binds D_0028A490[rec +0x1C] at the owner's
 * +0x40 first and passes 0.0 for both floats. The other subs fault. */
static EmScriptCommandResult op0B(Op *o)
{
    const EmAreaScriptWorld *w = o->w;
    uint32_t bank;
    switch (u32(o->rec, 8)) {
    case 6:
        WORKER(w_001FBD50, 0x001FBD50u);
        CALL(0x001FBD50u, o->k->w_001FBD50(o->ctx, w->self, s32(o->rec, 0x18), 0, 300.0f));
        /* fall through */
    case 0:
        WORKER(w_001C67E0, 0x001C67E0u);
        CALL(0x001C67E0u, o->k->w_001C67E0(o->ctx, w->self, s16(o->rec, 0x14), f32(o->rec, 0x0C), 0.0f));
        o->h->st_0E = 0;
        return EM_SCRIPT_ADVANCE;
    case 4:
        NEED(w->s040, 0x1B802000u);
        WORKER(r_0028A490, EM_AREA_SCRIPT_D_0028A490);
        CALL(EM_AREA_SCRIPT_D_0028A490, o->k->r_0028A490(o->ctx,
             EM_AREA_SCRIPT_D_0028A490 + 4u * u32(o->rec, 0x1C), &bank));
        *w->s040 = bank;
        WORKER(w_001C67E0, 0x001C67E0u);
        CALL(0x001C67E0u, o->k->w_001C67E0(o->ctx, w->self, s16(o->rec, 0x14), 0.0f, 0.0f));
        o->h->st_0E = 0;
        return EM_SCRIPT_ADVANCE;
    default:
        return fault(o, 0x001B8020u);
    }
}

/* 001B7D60 (ftab_0024D880[12]): message request, through the service. */
static EmScriptCommandResult op0C(Op *o)
{
    int32_t result;
    uint8_t handshake = phase(o);
    WORKER(w_001B7D60, 0x001B7D60u);
    CALL(0x001B7D60u, o->k->w_001B7D60(o->ctx, &handshake, o->rec, &result));
    set_phase(o, handshake);
    return (EmScriptCommandResult)result;
}

/* em_cinematic_playback's restore events are 001B0250, 0021B9A0(0,0,0)
 * and 001D2830(2,0). */
static int playback_emit(void *context, EmCinematicPlaybackEvent event,
                         const EmCinematicPlayback *playback)
{
    Op *o = context;
    (void)playback;
    switch (event) {
    case EM_CINEMATIC_CAMERA_RESTORE_ROOM:
        if (!o->k->w_001B0250) { fault(o, 0x001B0250u); return 0; }
        if (o->k->w_001B0250(o->ctx) < 0) { fault(o, 0x001B0250u); return 0; }
        return 1;
    case EM_CINEMATIC_CAMERA_EFFECT_OFF:
        if (!o->k->w_0021B9A0) { fault(o, 0x0021B9A0u); return 0; }
        if (o->k->w_0021B9A0(o->ctx, 0, 0.0f, 0.0f) < 0) { fault(o, 0x0021B9A0u); return 0; }
        return 1;
    case EM_CINEMATIC_CAMERA_FLAG_OFF:
        if (!o->k->w_001D2830) { fault(o, 0x001D2830u); return 0; }
        if (o->k->w_001D2830(o->ctx, 2, 0) < 0) { fault(o, 0x001D2830u); return 0; }
        return 1;
    default:
        fault(o, 0x001B7B30u);
        return 0;
    }
}

/* 001B7B30 (ftab_0024D880[13]): camera restore / retarget / waits. */
static EmScriptCommandResult op0D(Op *o)
{
    const EmAreaScriptWorld *w = o->w;
    float offset;
    switch (u32(o->rec, 8)) {
    case 0: {
        /* em_cinematic_playback_wait is the verified 001B7B30/sub0: its
         * time and track duration are the camera's +0x74 cursor and +0x78
         * head (both written by op00 kind 6). It stores the 480 zoom and the
         * up vector itself; 001D25F0 carries the zoom here, after its three
         * restore events, as in the original. */
        EmCinematicCamera head = {0};
        EmCinematicPlayback playback = {0};
        NEED(w->cam_74, 0x00810254u); NEED(w->cam_78, 0x00810258u);
        head.duration = *w->cam_78;
        playback.track = &head;
        playback.time = *w->cam_74;
        int done = em_cinematic_playback_wait(&playback, playback_emit, o);
        if (o->h->faulted) return FAIL;
        if (done < 0) return fault(o, 0x001B7B30u);
        if (!done) return EM_SCRIPT_WAIT;
        WORKER(w_001D25F0, 0x001D25F0u);
        CALL(0x001D25F0u, o->k->w_001D25F0(o->ctx, playback.zoom));
        NEED(w->d8105F0, 0x008105F0u);
        memcpy(w->d8105F0, playback.up, 16);
        return EM_SCRIPT_ADVANCE;
    }
    case 7:
        NEED(w->cam_74, 0x00810254u);
        return *w->cam_74 >= f32(o->rec, 0x0C) ? EM_SCRIPT_ADVANCE : EM_SCRIPT_WAIT;
    case 1:
        WORKER(w_001B0460, 0x001B0460u);
        CALL(0x001B0460u, o->k->w_001B0460(o->ctx, 1));
        return EM_SCRIPT_ADVANCE;
    case 2: case 3:
        NEED(w->cam_0C, 0x008101ECu);
        offset = *w->cam_0C;
        goto retarget;
    case 4:
        offset = -14.0f;
        goto retarget;
    case 5:
        offset = -20.0f;
    retarget:
        WORKER(w_0018CBD0, 0x0018CBD0u);
        CALL(0x0018CBD0u, o->k->w_0018CBD0(o->ctx, EM_AREA_SCRIPT_D_008101E0,
                                            EM_AREA_SCRIPT_D_008102B0, offset));
        WORKER(w_0018D7B0, 0x0018D7B0u);
        CALL(0x0018D7B0u, o->k->w_0018D7B0(o->ctx, EM_AREA_SCRIPT_D_008101E0, 5));
        CALL(0x0018D7B0u, o->k->w_0018D7B0(o->ctx, EM_AREA_SCRIPT_D_008101E0, 1));
        NEED(w->cam_A0, 0x00810280u);
        *w->cam_A0 = 0x78;
        return EM_SCRIPT_ADVANCE;
    case 6:
        NEED(w->cam_10, 0x008101F0u); NEED(w->cam_20, 0x00810200u);
        NEED(w->cam_50, 0x00810230u); NEED(w->cam_54, 0x00810234u);
        NEED(w->cam_A0, 0x00810280u); NEED(w->p0A0, 0x00810350u);
        NEED(w->d8105D0, 0x008105D0u); NEED(w->d8105E0, 0x008105E0u);
        quad(w->cam_10, w->d8105D0);
        quad(w->cam_20, w->d8105E0);
        *w->cam_54 = pose_add(100.0f, w->p0A0[1]);
        *w->cam_50 = w->p0A0[1];
        *w->cam_A0 = 0x78;
        return EM_SCRIPT_ADVANCE;
    case 8:
        NEED(w->d8101E4, 0x008101E4u);
        *w->d8101E4 = 1;
        return EM_SCRIPT_ADVANCE;
    default:
        return EM_SCRIPT_ADVANCE;
    }
}

/* 001B7A30 (ftab_0024D880[15]): stream stop / cue handshake / resume. */
static EmScriptCommandResult op0F(Op *o)
{
    const EmAreaScriptWorld *w = o->w;
    uint8_t v = phase(o);
    switch (v) {
    case 0:
        WORKER(w_001FBC50, 0x001FBC50u);
        CALL(0x001FBC50u, o->k->w_001FBC50(o->ctx));
        WORKER(w_001FABB0, 0x001FABB0u);
        CALL(0x001FABB0u, o->k->w_001FABB0(o->ctx));
        set_phase(o, (uint8_t)(phase(o) + 1));
        break;
    case 1:
        NEED(w->d282157, 0x00282157u);
        if (*w->d282157 == 0) set_phase(o, (uint8_t)(v + 1));
        break;
    case 2:
        NEED(w->d275C78, 0x00275C78u); NEED(w->d821058, 0x00821058u);
        *w->d275C78 = o->rec[0x14];
        *w->d821058 = 1;
        set_phase(o, (uint8_t)(phase(o) + 1));
        break;
    case 3:
        NEED(w->d821058, 0x00821058u);
        if (*w->d821058 == 0) set_phase(o, (uint8_t)(v + 1));
        break;
    case 4:
        if (u32(o->rec, 8) == 0) {
            WORKER(w_001FAE70, 0x001FAE70u);
            CALL(0x001FAE70u, o->k->w_001FAE70(o->ctx, 1));
        }
        return EM_SCRIPT_ADVANCE;
    default:
        break;
    }
    return EM_SCRIPT_WAIT;
}

/* 001B7840 (ftab_0024D880[16]): transition fades and waits. */
static EmScriptCommandResult op10(Op *o)
{
    const EmAreaScriptWorld *w = o->w;
    uint32_t sub = u32(o->rec, 8);
    int16_t handle = (int16_t)u32(o->rec, 0x14);
    switch (sub) {
    case 0: case 6:
        WORKER(w_001AEE10, 0x001AEE10u);
        CALL(0x001AEE10u, o->k->w_001AEE10(o->ctx, handle, sub == 6));
        break;
    case 1: case 7:
        WORKER(w_001AEDE0, 0x001AEDE0u);
        CALL(0x001AEDE0u, o->k->w_001AEDE0(o->ctx, handle, sub == 7));
        break;
    case 2: case 8:
        WORKER(w_001AED80, 0x001AED80u);
        CALL(0x001AED80u, o->k->w_001AED80(o->ctx, sub == 8));
        break;
    case 3: case 9:
        switch (phase(o)) {
        case 0:
            WORKER(w_001AEDB0, 0x001AEDB0u);
            CALL(0x001AEDB0u, o->k->w_001AEDB0(o->ctx, sub == 9));
            set_phase(o, (uint8_t)(phase(o) + 1));
            putf(o->rec, 0x10, 0.0f);
            return EM_SCRIPT_WAIT;
        case 1:
            if (f32(o->rec, 0x10) < f32(o->rec, 0x0C)) {
                putf(o->rec, 0x10, pose_add(f32(o->rec, 0x10), 1.0f));
                return EM_SCRIPT_WAIT;
            }
            break;
        default:
            break;
        }
        break;
    case 4:
        NEED(w->d28A9A0, 0x0028A9A0u);
        if (*w->d28A9A0 != 0) return EM_SCRIPT_WAIT;
        break;
    case 5:
        NEED(w->d28A9A0, 0x0028A9A0u);
        if (*w->d28A9A0 != 2) return EM_SCRIPT_WAIT;
        break;
    default:
        break;
    }
    return EM_SCRIPT_ADVANCE;
}

/* 001B6FA0 (ftab_0024D880[21]): owner turns and talks to the player. */
static EmScriptCommandResult op15(Op *o)
{
    const EmAreaScriptWorld *w = o->w;
    float f20, f21, v;
    int32_t hit;
    NEED(w->s0B0, 0x1B6FA000u); NEED(w->s0C0, 0x1B6FA000u);
    NEED(w->p0A0, 0x00810350u); NEED(w->p0B0, 0x00810360u); NEED(w->p0C0, 0x00810370u);
    NEED(w->p1F2, 0x008104A2u); NEED(w->p1F4, 0x008104A4u); NEED(w->p1F8, 0x008104A8u);
    NEED(w->p200, 0x008104B0u);
    WORKER(w_001C67E0, 0x001C67E0u);
    switch (phase(o)) {
    case 0:
        putf(o->rec, 0x10, w->s0C0[1]);
        WORKER(w_001B1380, 0x001B1380u);
        CALL(0x001B1380u, o->k->w_001B1380(o->ctx, w->p0A0, w->s0B0, w->s0C0[1], &hit));
        CALL(0x001C67E0u, o->k->w_001C67E0(o->ctx, w->self,
             s16(o->rec, hit ? 0x18 : 0x14), 20.0f, 0.0f));
        CALL(0x001B1380u, o->k->w_001B1380(o->ctx, w->s0B0, w->p0A0, w->p0C0[1], &hit));
        *w->p1F2 = hit ? 0x156 : 0x155;
        *w->p1F8 = 20.0f;
        *w->p1F4 = 1.0f;
        set_phase(o, 1);
        break;
    case 1:
        WORKER(w_001B1240, 0x001B1240u); WORKER(w_001B12B0, 0x001B12B0u);
        if (f32(o->rec, 0x24) == 0.0f) {
            f20 = w->s0C0[1];
        } else {
            CALL(0x001B1240u, o->k->w_001B1240(o->ctx, w->s0B0, w->p0B0[0], w->p0B0[2], &f20));
            CALL(0x001B12B0u, o->k->w_001B12B0(o->ctx, f20, w->s0C0[1], f32(o->rec, 0x24), &v));
            w->s0C0[1] = v;
        }
        if (f32(o->rec, 0x34) == 0.0f) {
            f21 = w->p0C0[1];
        } else {
            CALL(0x001B1240u, o->k->w_001B1240(o->ctx, w->p0B0, w->s0B0[0], w->s0B0[2], &f21));
            CALL(0x001B12B0u, o->k->w_001B12B0(o->ctx, f21, w->p0C0[1], f32(o->rec, 0x34), &v));
            w->p0C0[1] = v;
        }
        if (w->p0C0[1] == f21) {
            *w->p1F2 = 0x164;
            *w->p1F8 = 30.0f;
        }
        if (w->s0C0[1] == f20 && w->p0C0[1] == f21) {
            NEED(w->d2821B0, 0x002821B0u); NEED(w->d2821B4, 0x002821B4u);
            NEED(w->d2821B8, 0x002821B8u); NEED(w->d2821BC, 0x002821BCu);
            *w->p1F2 = 0x166;
            *w->p1F8 = 30.0f;
            CALL(0x001C67E0u, o->k->w_001C67E0(o->ctx, w->self, s16(o->rec, 0x1C), 30.0f, 0.0f));
            *w->d2821B0 = 2;
            *w->d2821B4 = 1;
            *w->d2821B8 = u32(o->rec, 8);
            *w->d2821BC = 0;
            set_phase(o, 2);
        }
        break;
    case 2:
        NEED(w->d2821B0, 0x002821B0u); NEED(w->d2821B4, 0x002821B4u);
        if (*w->d2821B4 != 2 && *w->d2821B0 != 0) break;
        *w->p1F2 = 0x165;
        *w->p1F8 = 30.0f;
        WORKER(w_001B1470, 0x001B1470u);
        CALL(0x001B1470u, o->k->w_001B1470(o->ctx, pose_sub(w->s0C0[1], f32(o->rec, 0x10)), &v));
        CALL(0x001C67E0u, o->k->w_001C67E0(o->ctx, w->self,
             s16(o->rec, v < 0.0f ? 0x18 : 0x14), 20.0f, 0.0f));
        set_phase(o, 3);
        break;
    case 3:
        if (f32(o->rec, 0x24) == 0.0f) {
            w->s0C0[1] = f32(o->rec, 0x10);
        } else {
            WORKER(w_001B12B0, 0x001B12B0u);
            CALL(0x001B12B0u, o->k->w_001B12B0(o->ctx, f32(o->rec, 0x10), w->s0C0[1],
                                               f32(o->rec, 0x24), &v));
            w->s0C0[1] = v;
        }
        if (w->s0C0[1] == f32(o->rec, 0x10)) {
            CALL(0x001C67E0u, o->k->w_001C67E0(o->ctx, w->self, s16(o->rec, 4), 20.0f, 0.0f));
            if (*w->p200 & 0x1000) return EM_SCRIPT_ADVANCE;
            set_phase(o, 4);
        }
        break;
    case 4:
        if (*w->p200 & 0x1000) return EM_SCRIPT_ADVANCE;
        break;
    default:
        break;
    }
    return EM_SCRIPT_WAIT;
}

/* 001B6E40 (ftab_0024D880[22]): take the scripted frame. */
static EmScriptCommandResult op16(Op *o)
{
    int32_t result;
    NEED(o->w->spad3B8D, 0x70003B8Du);
    if (*o->w->spad3B8D != 0) return EM_SCRIPT_WAIT;
    WORKER(w_00182BF0, 0x00182BF0u);
    CALL(0x00182BF0u, o->k->w_00182BF0(o->ctx, EM_AREA_SCRIPT_D_008102B0, &result));
    if (result != 0) return EM_SCRIPT_WAIT;
    *o->w->spad3B8D = 3;
    return EM_SCRIPT_ADVANCE;
}

/* 001B6BF0 (ftab_0024D880[24]): skip landing. Unskipped it only marks the
 * skip byte 3 and continues. */
static EmScriptCommandResult op18(Op *o)
{
    const EmAreaScriptWorld *w = o->w;
    switch (phase(o)) {
    case 0:
        if (o->st->skip_phase != 2) {
            o->st->skip_phase = 3;
            return EM_SCRIPT_CONTINUE;
        }
        WORKER(w_001B0C00, 0x001B0C00u);
        CALL(0x001B0C00u, o->k->w_001B0C00(o->ctx, 8));
        set_phase(o, (uint8_t)(phase(o) + 1));
        WORKER(w_001B6250, 0x001B6250u);
        CALL(0x001B6250u, o->k->w_001B6250(o->ctx, EM_AREA_SCRIPT_D_00810E40));
        return EM_SCRIPT_WAIT;
    case 1: {
        uint32_t bank;
        NEED(w->d28A9A0, 0x0028A9A0u);
        if (*w->d28A9A0 != 2) return EM_SCRIPT_WAIT;
        NEED(w->d2821B4, 0x002821B4u); NEED(w->p1F2, 0x008104A2u);
        NEED(w->p20C, 0x008104BCu); NEED(w->p2F3, 0x008105A3u);
        NEED(w->p040, 0x008102F0u); NEED(w->d8101E4, 0x008101E4u);
        WORKER(w_001FBC50, 0x001FBC50u);
        CALL(0x001FBC50u, o->k->w_001FBC50(o->ctx));
        WORKER(w_001FABB0, 0x001FABB0u);
        CALL(0x001FABB0u, o->k->w_001FABB0(o->ctx));
        *w->d2821B4 = 2;
        set_phase(o, (uint8_t)(phase(o) + 1));
        WORKER(r_0028A490, EM_AREA_SCRIPT_D_0028A580);
        CALL(EM_AREA_SCRIPT_D_0028A580, o->k->r_0028A490(o->ctx, EM_AREA_SCRIPT_D_0028A580, &bank));
        *w->p1F2 = 0;
        *w->p20C = -1;
        *w->p2F3 = 3;
        *w->p040 = bank;
        if (*w->d8101E4 == 3) {
            WORKER(w_001B0250, 0x001B0250u);
            CALL(0x001B0250u, o->k->w_001B0250(o->ctx));
            WORKER(w_0021B9A0, 0x0021B9A0u);
            CALL(0x0021B9A0u, o->k->w_0021B9A0(o->ctx, 0, 0.0f, 0.0f));
            WORKER(w_001D2830, 0x001D2830u);
            CALL(0x001D2830u, o->k->w_001D2830(o->ctx, 2, 0));
            WORKER(w_001D25F0, 0x001D25F0u);
            CALL(0x001D25F0u, o->k->w_001D25F0(o->ctx, 480.0f));
            NEED(w->d8105F0, 0x008105F0u);
            w->d8105F0[0] = 0.0f;
            w->d8105F0[2] = 0.0f;
            w->d8105F0[1] = -1.0f;
            w->d8105F0[3] = 1.0f;
        }
        *w->d8101E4 = 2;
        WORKER(w_001B6250, 0x001B6250u);
        CALL(0x001B6250u, o->k->w_001B6250(o->ctx, EM_AREA_SCRIPT_D_00810E40));
        return EM_SCRIPT_ADVANCE;
    }
    default:
        return EM_SCRIPT_WAIT;
    }
}

/* ---- 0011E2A8 (SDK sinf) ----
 * The original scalar FPU steps in order; add.s/sub.s with the guard-bit
 * model, mul.s truncating (em_pose_math.h). */
static float bits_float(uint32_t value)
{
    float f;
    memcpy(&f, &value, 4);
    return f;
}

static uint32_t float_bits(float value)
{
    uint32_t u;
    memcpy(&u, &value, 4);
    return u;
}

/* 0011D770(x, tail, reduced). Below 0x32000000 it returns x when cvt.w.s
 * of x is zero. */
static float sdk_sin_kernel(float x, float tail, int reduced)
{
    if ((float_bits(x) & 0x7FFFFFFFu) <= 0x31FFFFFFu && (int32_t)x == 0) return x;
    float z = pose_mul(x, x);
    float v = pose_mul(z, x);
    float r = pose_add(pose_mul(z, bits_float(0x2F2EC9D3u)), bits_float(0xB2D72F34u));
    r = pose_add(pose_mul(z, r), bits_float(0x3638EF1Bu));
    r = pose_add(pose_mul(z, r), bits_float(0xB9500D01u));
    r = pose_add(pose_mul(z, r), bits_float(0x3C088889u));
    if (!reduced)
        return pose_add(x, pose_mul(v, pose_add(pose_mul(z, r), bits_float(0xBE2AAAABu))));
    float c = pose_sub(pose_mul(tail, 0.5f), pose_mul(v, r));
    c = pose_sub(pose_mul(z, c), tail);
    c = pose_sub(c, pose_mul(v, bits_float(0xBE2AAAABu)));
    return pose_sub(x, c);
}

/* 0011CCC8(x, tail). Below 0x32000000 it returns 1 when cvt.w.s of x is
 * zero. */
static float sdk_cos_kernel(float x, float tail)
{
    uint32_t absolute = float_bits(x) & 0x7FFFFFFFu;
    if (absolute <= 0x31FFFFFFu && (int32_t)x == 0) return 1.0f;
    float z = pose_mul(x, x);
    float r = pose_add(pose_mul(z, bits_float(0xAD47D74Eu)), bits_float(0x310F74F6u));
    r = pose_add(pose_mul(z, r), bits_float(0xB493F27Cu));
    r = pose_add(pose_mul(z, r), bits_float(0x37D00D01u));
    r = pose_add(pose_mul(z, r), bits_float(0xBAB60B61u));
    r = pose_mul(z, pose_add(pose_mul(z, r), bits_float(0x3D2AAAABu)));
    if (absolute <= 0x3E999999u) {
        float c = pose_sub(pose_mul(z, r), pose_mul(x, tail));
        return pose_sub(1.0f, pose_sub(pose_mul(z, 0.5f), c));
    }
    float q = bits_float(absolute > 0x3F480000u ? 0x3E900000u : absolute - 0x01000000u);
    float c = pose_sub(pose_mul(z, r), pose_mul(x, tail));
    return pose_sub(pose_sub(1.0f, q), pose_sub(pose_sub(pose_mul(z, 0.5f), q), c));
}

int em_area_script_sin_0011E2A8(float x, float *result)
{
    uint32_t absolute = float_bits(x) & 0x7FFFFFFFu;
    float z, high, low;
    if (!result) return -1;
    if (absolute <= 0x3F490FD8u) {
        *result = sdk_sin_kernel(x, 0.0f, 0);
        return 0;
    }
    /* 0011C7B0, pi/4 < |x| <= 0x4016CBE3: one pi/2 step (n = +-1). The
     * larger-argument branches are not translated. */
    if (absolute > 0x4016CBE3u) return -1;
    if ((int32_t)float_bits(x) > 0) {
        z = pose_sub(x, bits_float(0x3FC90F80u));
        if ((absolute & 0xFFFFFFF0u) != 0x3FC90FD0u) {
            high = pose_sub(z, bits_float(0x37354443u));
            low = pose_sub(pose_sub(z, high), bits_float(0x37354443u));
        } else {
            z = pose_sub(z, bits_float(0x37354400u));
            high = pose_sub(z, bits_float(0x2E85A308u));
            low = pose_sub(pose_sub(z, high), bits_float(0x2E85A308u));
        }
        *result = sdk_cos_kernel(high, low);           /* n & 3 == 1 */
    } else {
        z = pose_add(x, bits_float(0x3FC90F80u));
        if ((absolute & 0xFFFFFFF0u) != 0x3FC90FD0u) {
            high = pose_add(z, bits_float(0x37354443u));
            low = pose_add(pose_sub(z, high), bits_float(0x37354443u));
        } else {
            z = pose_add(z, bits_float(0x37354400u));
            high = pose_add(z, bits_float(0x2E85A308u));
            low = pose_add(pose_sub(z, high), bits_float(0x2E85A308u));
        }
        *result = -sdk_cos_kernel(high, low);          /* n & 3 == 3 */
    }
    return 0;
}

int em_area_script_w_0011E2A8(void *ctx, float a0, float *result)
{
    (void)ctx;
    return em_area_script_sin_0011E2A8(a0, result);
}

static EmScriptCommandResult execute(void *context, EmScript *script, unsigned char *record)
{
    EmAreaScript *h = context;
    Op o = {h, h->world, h->workers, h->workers->ctx, script, record};
    if (h->faulted) return FAIL;
    switch (u32(record, 0) & 0xFFF) {
    case 0x00: return op00(&o);
    case 0x01: return op01(&o);
    case 0x02: return op02(&o);
    case 0x04: return op04(&o);
    case 0x06: return op06(&o);
    case 0x07: return op07(&o);
    case 0x09: return op09(&o);
    case 0x0A: return op0A(&o);
    case 0x0B: return op0B(&o);
    case 0x0C: return op0C(&o);
    case 0x0D: return op0D(&o);
    case 0x0F: return op0F(&o);
    case 0x10: return op10(&o);
    case 0x15: return op15(&o);
    case 0x16: return op16(&o);
    case 0x18: return op18(&o);
    default:
        /* Opcodes 03, 05, 08, 0E, 11..14, 17, 19, 1A: not used by the
         * AREA11 scripts this host admits (docs/AREA_SCRIPT.md). */
        return fault(&o, 0x0024D880u + 4u * (u32(record, 0) & 0xFFF));
    }
}

static unsigned char *resolve(void *context, uint32_t address)
{
    EmAreaScript *h = context;
    return em_script_image_read(h->image, address, EM_SCRIPT_RECORD_SIZE);
}

void em_area_script_init(EmAreaScript *h, EmScriptImage *image, const EmAreaScriptWorld *world,
                         const EmAreaScriptWorkers *workers)
{
    memset(h, 0, sizeof *h);
    h->image = image;
    h->world = world;
    h->workers = workers;
}

int em_area_script_start(EmAreaScript *h, uint32_t entry)
{
    if (!h || !h->image || !em_script_image_read(h->image, entry, EM_SCRIPT_RECORD_SIZE))
        return -1;
    /* 001BA1A0 writes +0x00, +0x04, +0x08 and the byte +0x0C only; it does
     * not store 3B91 (em_script_start's view reset is refreshed per tick). */
    em_script_start(&h->script, entry);
    return 0;
}

int em_area_script_tick(EmAreaScript *h)
{
    if (!h || !h->world || !h->workers || !h->image) return -1;
    if (h->faulted) return -1;
    if (h->script.active <= 0) return 1;
    if (!h->world->spad3B91) {
        h->faulted = 1;
        h->fault_address = 0x70003B91u;
        h->fault_pc = h->script.pc;
        return -1;
    }
    h->script.skip_request = *h->world->spad3B91;
    EmScriptResult result = em_script_tick(&h->script, resolve, execute, h);
    switch (result) {
    case EM_SCRIPT_YIELDED: return 0;
    case EM_SCRIPT_FINISHED: return 1;
    case EM_SCRIPT_ABORTED: return 3;
    default:
        if (!h->faulted) {
            h->faulted = 1;
            h->fault_address = 0x001BA1F0u;
            h->fault_pc = h->script.pc;
        }
        return -1;
    }
}
