#include "game/em_area01_runtime.h"
#include "game/em_area01_math_actor.h"
#include "game/em_area01_math_owner.h"
#include "game/em_area01_light_owner.h"
#include "game/em_ee_float.h"
#include <string.h>

static int fail(EmArea01Runtime *r, uint32_t address)
{
    if (r && !r->fault) { r->fault = 1; r->fault_address = address; }
    return -1;
}

static uint8_t *bytes(void *ctx, uint32_t address, uint32_t size, int write)
{
    EmArea01Runtime *r = ctx;
    if (r->fault || !size || (uint64_t)address + size > UINT64_C(0x100000000)) return NULL;
    return r->host.bytes(r->host.ctx, address, size, write);
}

static int math_call(void *ctx, uint32_t function, const uint32_t *a, unsigned na,
                     const uint32_t *f, unsigned nf, uint32_t *v0, uint32_t *f0)
{
    EmArea01Runtime *r = ctx;
    if (na > 8 || nf > 8) return fail(r, function);
    EmArea01Call c = {.function = function, .sp = r->sp, .na = na, .nf = nf};
    for (unsigned i = 0; i < na; ++i) c.a[i] = (uint64_t)(int64_t)(int32_t)a[i];
    if (nf) memcpy(c.f, f, nf * sizeof *f);
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0) { if (v0) *v0 = (uint32_t)c.v0; if (f0) *f0 = c.f0; }
    return rc;
}

#define CALL_ADAPTER(name, Type) \
static int name(void *ctx, Type *in) \
{ \
    EmArea01Call c = {.function = in->fn, .sp = in->sp, .na = in->na, .nf = in->nf}; \
    if (in->na > 8 || in->nf > sizeof in->f / sizeof in->f[0]) return fail(ctx, in->fn); \
    memcpy(c.a, in->a, in->na * sizeof in->a[0]); \
    memcpy(c.f, in->f, in->nf * sizeof in->f[0]); \
    int rc = em_area01_runtime_call(ctx, &c); \
    if (rc == 0) { in->v0 = c.v0; in->f0 = c.f0; } \
    return rc; \
}
CALL_ADAPTER(sys_call, EmArea01SysCall)
CALL_ADAPTER(exita_call, EmArea01ExitaCall)
CALL_ADAPTER(exitb_call, EmArea01ExitbCall)
CALL_ADAPTER(room_call, EmArea01RoomCall)
CALL_ADAPTER(side_call, EmArea01SideCall)
#undef CALL_ADAPTER

#include "game/em_area01_runtime_overlay.inc"

static int overlay_callback(void *ctx, uint32_t function, uint32_t actor)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = function, .sp = r->sp, .a = {actor}, .na = 1};
    return em_area01_runtime_call(r, &c);
}

int em_area01_runtime_bind(EmArea01Runtime *r, const EmArea01RuntimeHost *host)
{
    if (!r || !host || !host->bytes || !host->worker) return -1;
    memset(r, 0, sizeof *r);
    r->host = *host;
    r->math.ctx = r; r->math.view = bytes; r->math.call = math_call;
    r->overlay.ctx = r; r->overlay.view = bytes;
    overlay_workers(r); r->overlay.w_callback = overlay_callback;
    r->sys.ctx = r; r->sys.view = bytes; r->sys.call = sys_call;
    r->exita.ctx = r; r->exita.view = bytes; r->exita.call = exita_call;
    r->exitb.ctx = r; r->exitb.view = bytes; r->exitb.call = exitb_call;
    r->room.ctx = r; r->room.view = bytes; r->room.call = room_call;
    r->side.ctx = r; r->side.view = bytes; r->side.call = side_call;
    return 0;
}

static int dispatch(EmArea01Runtime *r, EmArea01Call *c)
{
    int rc;
    int32_t i32 = 0;
    uint32_t u32 = 0;
    uint64_t u64 = 0;
    switch (c->function) {
#include "game/em_area01_runtime_dispatch.inc"
    case 0x00198D90u:
        if (c->na < 2) return fail(r, c->function);
        rc = em_area01_room_00198D90(&r->room, c->a[0], c->a[1]);
        break;
    case 0x001D0D60u:
        if (c->na < 1 || c->nf < 1) return fail(r, c->function);
        rc = em_area01_room_001D0D60(&r->room, (uint32_t)c->a[0], c->f[0], &i32);
        if (rc == 0) c->v0 = (uint64_t)(int64_t)i32;
        break;
    case 0x001EFE00u:
        if(c->na<2)return fail(r,c->function);
        rc=em_area01_side_001EFE00(&r->side,(int32_t)c->a[0],(uint32_t)c->a[1],&i32);
        if(rc==0)c->v0=(uint64_t)(int64_t)i32;
        break;
#define OVERLAY(addr) case 0x##addr##u: \
    if (c->na < 1) return fail(r, c->function); \
    rc = em_area01_ovl_##addr(&r->overlay, (uint32_t)c->a[0], &r->overlay_fault); break
    OVERLAY(00823580);
    OVERLAY(00825350);
    OVERLAY(008254B0);
    OVERLAY(00825590);
    OVERLAY(00825670);
    OVERLAY(00825740);
    OVERLAY(008261A0);
    OVERLAY(00826200);
    OVERLAY(00826440);
    OVERLAY(008267C0);
    OVERLAY(00826CF0);
    OVERLAY(00826D40);
    OVERLAY(00828850);
#undef OVERLAY
    case 0x00825130u:
    case 0x00825240u:
        if (c->na < 3) return fail(r, c->function);
        rc = c->function == 0x00825130u
            ? em_area01_ovl_00825130(&r->overlay, c->a[0], c->a[1], c->a[2], &i32, &r->overlay_fault)
            : em_area01_ovl_00825240(&r->overlay, c->a[0], c->a[1], c->a[2], &i32, &r->overlay_fault);
        if (rc == 0) c->v0 = (uint64_t)(int64_t)i32;
        break;
    case 0x001C4FA0u:
        if (c->na < 1) return fail(r, c->function);
        rc = em_area01_light_001C4FA0(&r->math, c->a[0], &u32);
        if (rc == 0) c->v0 = (uint64_t)(int64_t)(int32_t)u32;
        break;
    case 0x001C50B0u:
        if (c->na < 1) return fail(r, c->function);
        r->sp = c->sp - 0x40u;
        rc = em_area01_light_001C50B0(&r->math, c->a[0], c->sp);
        break;
    default:
        return r->host.worker(r->host.ctx, c);
    }
    return rc;
}

int em_area01_runtime_call(EmArea01Runtime *r, EmArea01Call *c)
{
    if (!r || !c || r->fault) return -1;
    if (!r->host.bytes || !r->host.worker || c->na > 8 || c->nf > 8) return fail(r, c->function);
    uint32_t saved_sp = r->sp, ss = r->sys.sp, sa = r->exita.sp, sb = r->exitb.sp;
    uint32_t sr = r->room.sp, sd = r->side.sp;
    r->sp = r->sys.sp = r->exita.sp = r->exitb.sp = r->room.sp = r->side.sp = c->sp;
    int rc = dispatch(r, c);
    r->sp = saved_sp; r->sys.sp = ss; r->exita.sp = sa; r->exitb.sp = sb;
    r->room.sp = sr;
    r->side.sp = sd;
    if (rc < 0) {
        uint32_t address = c->function;
        if (r->math.fault_code) address = r->math.fault_address;
        else if (r->overlay_fault.code) address = r->overlay_fault.address;
        else if (r->sys.fault) address = r->sys.fault_address;
        else if (r->exita.fault) address = r->exita.fault_address;
        else if (r->exitb.fault) address = r->exitb.fault_address;
        else if (r->room.fault) address = r->room.fault_address;
        else if (r->side.fault) address = r->side.fault_address;
        return fail(r, address);
    }
    return r->fault ? -1 : 0;
}
