#include "game/em_area01_runtime.h"
#include "game/em_area01_math_actor.h"
#include "game/em_area01_math_owner.h"
#include "game/em_area01_math_player.h"
#include "game/em_area01_light_owner.h"
#include "game/em_area00_world.h"
#include "game/em_area00_fx.h"
#include "game/em_area01_revisit_spawn.h"
#include "game/em_script_door_fan.h"
#include "game/em_startup_load_gaps.h"
#include "game/em_area02_misc.h"
#include "game/em_area02_math.h"
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
#include "game/em_area00_runtime_overlay.inc"
#include "game/em_area01_runtime_revisit.inc"
#include "game/em_area02_runtime_overlay.inc"

/* 001F4E20 is already translated by the AREA00 FX owner. Its only
 * callee keeps both address registers and adds the original second float. */
static int switch_marker(void *ctx, uint32_t fn, uint32_t sp, const EmArea00FxRegs *in,
                         uint64_t *v0, uint32_t *f0)
{
    if (fn != 0x001F4D40u || in->imask != ((1u << 4) | (1u << 5)) ||
        in->fmask != ((1u << 12) | (1u << 13))) return fail(ctx, fn);
    EmArea01Call c = {.function = fn, .sp = sp, .a = {in->r[4], in->r[5]},
                     .f = {in->f[12], in->f[13]}, .na = 2, .nf = 2};
    int rc = em_area01_runtime_call(ctx, &c);
    if (rc == 0) { if (v0) *v0 = c.v0; if (f0) *f0 = c.f0; }
    return rc;
}

static int overlay_callback(void *ctx, uint32_t function, uint32_t actor)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = function, .sp = r->sp, .a = {actor}, .na = 1};
    return em_area01_runtime_call(r, &c);
}

int em_area01_runtime_bind(EmArea01Runtime *r, const EmArea01RuntimeHost *host)
{
    return em_area01_runtime_bind_area(r, host, 1);
}

int em_area01_runtime_bind_area(EmArea01Runtime *r, const EmArea01RuntimeHost *host, unsigned area)
{
    if (!r || !host || !host->bytes || !host->worker || area > 2) return -1;
    memset(r, 0, sizeof *r);
    r->host = *host;
    r->area = area;
    r->math.ctx = r; r->math.view = bytes; r->math.call = math_call;
    r->overlay.ctx = r; r->overlay.view = bytes;
    overlay_workers(r); r->overlay.w_callback = overlay_callback;
    r->overlay00.ctx = r; r->overlay00.view = bytes;
    overlay00_workers(r); r->overlay00.w_callback = overlay_callback;
    revisit_workers(r);
    overlay02_workers(r); r->overlay02.w_callback = overlay_callback;
    r->sys.ctx = r; r->sys.view = bytes; r->sys.call = sys_call;
    r->exita.ctx = r; r->exita.view = bytes; r->exita.call = exita_call;
    r->exitb.ctx = r; r->exitb.view = bytes; r->exitb.call = exitb_call;
    r->room.ctx = r; r->room.view = bytes; r->room.call = room_call;
    r->side.ctx = r; r->side.view = bytes; r->side.call = side_call;
    return 0;
}

/* Overlay addresses overlap. Resolve AREA00 before the AREA01 switch,
 * including its generated math entries. Unknown AREA00 entries may only
 * reach the selected host's explicit workers, never an AREA01 overlay. */
static int dispatch_overlay00(EmArea01Runtime *r, EmArea01Call *c)
{
    switch (c->function) {
#define OVERLAY00(addr) case 0x##addr##u: \
    if (c->na < 1 || c->nf) return fail(r, c->function); \
    return em_area00_ovl_##addr(&r->overlay00, (uint32_t)c->a[0], &r->overlay00_fault)
    OVERLAY00(00823580);
    OVERLAY00(00824EA0);
    OVERLAY00(00825170);
    OVERLAY00(00825480);
    OVERLAY00(00825920);
    OVERLAY00(00825C80);
    OVERLAY00(008261E0);
    OVERLAY00(008262D0);
    OVERLAY00(008263C0);
    OVERLAY00(008266A0);
#undef OVERLAY00
    case 0x00823820u:
        if (c->na < 1 || c->nf || c->sp < 0xE0u) return fail(r, c->function);
        r->sp = c->sp - 0xE0u;
        return em_area00_ovl_00823820(&r->overlay00, (uint32_t)c->a[0], c->sp, &r->overlay00_fault);
    case 0x00825600u:
        if (c->na < 1 || c->nf || c->sp < 0x50u) return fail(r, c->function);
        r->sp = c->sp - 0x50u;
        return em_area00_ovl_00825600(&r->overlay00, (uint32_t)c->a[0], c->sp, &r->overlay00_fault);
    case 0x008253E0u: {
        if (c->na < 3 || c->nf) return fail(r, c->function);
        int32_t result = 0;
        int rc = em_area00_ovl_008253E0(&r->overlay00, (uint32_t)c->a[0], (uint32_t)c->a[1],
                                       (uint32_t)c->a[2], &result, &r->overlay00_fault);
        if (rc == 0) c->v0 = (uint64_t)(int64_t)result;
        return rc;
    }
    default:
        return r->host.worker(r->host.ctx, c);
    }
}

/* AREA02's overlay entries the route reaches (docs/AREA02_PRETRANS.md).
 * Each takes its original arguments; the entries with stack locals take
 * the caller's sp. Nested hooks get the entry's sp less the deepest chain
 * of original frames below it (0x823930 0x10 -> 0x824020 0x20 -> 0x8242F0
 * 0x40 -> 0x824CD0 0x60 -> 0x824C40 0x20; read from each function's first
 * instruction), so no callee frame overlaps a local the module writes.
 * 0x825520 (reached only through 0x825100 with D_0081083F == 2, not on the
 * route) and any other overlay address go to the host's workers. */
static int dispatch_overlay02(EmArea01Runtime *r, EmArea01Call *c)
{
    static const struct { uint32_t fn, frame; } frames[] = {
        {0x00823580u, 0x30u}, {0x00823900u, 0}, {0x00823930u, 0x110u}, {0x00823980u, 0x60u},
        {0x00823D70u, 0x20u}, {0x00824020u, 0x100u}, {0x008242F0u, 0xC0u}, {0x00824800u, 0x40u},
        {0x00824910u, 0x60u}, {0x00824AC0u, 0x40u}, {0x00824C40u, 0x20u}, {0x00824CD0u, 0x80u},
        {0x00824D50u, 0x30u}, {0x00824FA0u, 0x30u}, {0x00825100u, 0x30u}};
    uint32_t frame = 0xFFFFFFFFu;
    for (unsigned i = 0; i < sizeof frames / sizeof frames[0]; ++i)
        if (frames[i].fn == c->function) frame = frames[i].frame;
    if (frame == 0xFFFFFFFFu) return r->host.worker(r->host.ctx, c);
    const unsigned na = c->function == 0x00823900u ? 0u : 1u;
    if (c->na < na || c->nf || c->sp < frame) return fail(r, c->function);
    r->sp = c->sp - frame;
    const EmArea02OvlHooks *h = &r->overlay02;
    EmArea02OvlFault *f = &r->overlay02_fault;
    const uint32_t a0 = (uint32_t)c->a[0];
    int32_t result = 0;
    int rc;
    switch (c->function) {
    case 0x00823580u: return em_area02_ovl_00823580(h, a0, f);
    case 0x00823900u: return em_area02_ovl_00823900(h, f);
    case 0x00823930u: return em_area02_ovl_00823930(h, a0, c->sp, f);
    case 0x00823980u: return em_area02_ovl_00823980(h, a0, f);
    case 0x00823D70u: return em_area02_ovl_00823D70(h, a0, f);
    case 0x00824020u: return em_area02_ovl_00824020(h, a0, c->sp, f);
    case 0x00824910u: return em_area02_ovl_00824910(h, a0, c->sp, f);
    case 0x00824AC0u: return em_area02_ovl_00824AC0(h, a0, f);
    case 0x00824C40u: return em_area02_ovl_00824C40(h, a0, f);
    case 0x00824CD0u: return em_area02_ovl_00824CD0(h, a0, c->sp, f);
    case 0x00824FA0u: return em_area02_ovl_00824FA0(h, a0, f);
    case 0x00825100u: return em_area02_ovl_00825100(h, a0, f);
    case 0x008242F0u: rc = em_area02_ovl_008242F0(h, a0, c->sp, &result, f); break;
    case 0x00824800u: rc = em_area02_ovl_00824800(h, a0, &result, f); break;
    default:          rc = em_area02_ovl_00824D50(h, a0, &result, f); break;
    }
    if (rc == 0) c->v0 = (uint64_t)(int64_t)result;
    return rc;
}

/* em_area02_math's boot routines (docs/AREA02_MATH.md: the node
 * behaviours, use tests, script callbacks and draw methods the revisit and
 * AREA02 run), over this runtime's bytes; every callee re-enters the runtime.
 * Dispatched for AREA02 only (area 2): AREA00 / AREA01 keep their existing
 * live paths. Not 001C7EB0 / 001C8140 (the revisit's spawn module; the
 * duplicate pair is to be merged). Returns 1
 * when `function` is one of them (*rc set). */
static uint8_t *math02_view(void *ctx, uint32_t address, uint32_t size)
{
    return bytes(ctx, address, size, 0);
}
static int math02_call(void *ctx, EmArea02MathCall *in)
{
    EmArea01Call c = {.function = in->fn, .sp = in->sp, .na = in->na, .nf = in->nf};
    if (in->na > 4 || in->nf > 4) return fail(ctx, in->fn);
    memcpy(c.a, in->a, sizeof in->a);
    memcpy(c.f, in->f, sizeof in->f);
    int rc = em_area01_runtime_call(ctx, &c);
    if (rc == 0) { in->v0 = c.v0; in->f0 = c.f0; }
    return rc;
}
static int dispatch_math02(EmArea01Runtime *r, EmArea01Call *c, int *rc)
{
    static const struct { uint32_t fn; unsigned na; } entries[] = {
        {0x001575E0u, 1}, {0x00157B30u, 2}, {0x00157F30u, 1}, {0x00158050u, 1}, {0x001582E0u, 1},
        {0x00158EC0u, 1}, {0x00159210u, 1}, {0x00159620u, 1}, {0x00159970u, 1}, {0x00159E70u, 1}, {0x00183C40u, 2},
        {0x001B18F0u, 3}, {0x001BC960u, 1}, {0x001BD560u, 1}, {0x001BDFC0u, 1}, {0x001C48C0u, 1},
        {0x001C4960u, 1}, {0x001C4AF0u, 1}, {0x001C4CB0u, 1}, {0x001CAE40u, 2}, {0x001CAF60u, 1},
        {0x001CB4F0u, 2}, {0x001CB580u, 1}};
    unsigned na = 99;
    for (unsigned i = 0; i < sizeof entries / sizeof entries[0]; ++i)
        if (entries[i].fn == c->function) na = entries[i].na;
    if (na == 99) return 0;
    if (c->na < na || c->nf) { *rc = fail(r, c->function); return 1; }
    EmArea02Math m;
    memset(&m, 0, sizeof m);
    m.call = math02_call;
    m.ctx = r;
    m.sp = c->sp;
    m.view = math02_view;
    const uint32_t a0 = (uint32_t)c->a[0], a1 = (uint32_t)c->a[1], a2 = (uint32_t)c->a[2];
    int32_t out = 0;
    int x;
    switch (c->function) {
    case 0x001575E0u: x = em_area02_math_001575E0(&m, a0, &out); break;
    case 0x00157B30u: x = em_area02_math_00157B30(&m, a0, a1, &out); break;
    case 0x00157F30u: x = em_area02_math_00157F30(&m, a0, &out); break;
    case 0x00158050u: x = em_area02_math_00158050(&m, a0, &out); break;
    case 0x001582E0u: x = em_area02_math_001582E0(&m, a0); break;
    case 0x00158EC0u: x = em_area02_math_00158EC0(&m, a0); break;
    case 0x00159210u: x = em_area02_math_00159210(&m, a0); break;
    case 0x00159620u: x = em_area02_math_00159620(&m, a0); break;
    case 0x00159970u: x = em_area02_math_00159970(&m, a0); break;
    case 0x00159E70u: x = em_area02_math_00159E70(&m, a0); break;
    case 0x00183C40u: x = em_area02_math_00183C40(&m, a0, a1); break;
    case 0x001B18F0u: x = em_area02_math_001B18F0(&m, a0, a1, a2, &out); break;
    case 0x001BC960u: x = em_area02_math_001BC960(&m, a0); break;
    case 0x001BD560u: x = em_area02_math_001BD560(&m, a0); break;
    case 0x001BDFC0u: x = em_area02_math_001BDFC0(&m, a0); break;
    case 0x001C48C0u: x = em_area02_math_001C48C0(&m, a0); break;
    case 0x001C4960u: x = em_area02_math_001C4960(&m, a0); break;
    case 0x001C4AF0u: x = em_area02_math_001C4AF0(&m, a0); break;
    case 0x001C4CB0u: x = em_area02_math_001C4CB0(&m, a0); break;
    case 0x001CAE40u: x = em_area02_math_001CAE40(&m, a0, a1); break;
    case 0x001CAF60u: x = em_area02_math_001CAF60(&m, a0); break;
    case 0x001CB4F0u: x = em_area02_math_001CB4F0(&m, a0, a1); break;
    default:          x = em_area02_math_001CB580(&m, a0); break;
    }
    if (x < 0) { *rc = fail(r, m.fault_address ? m.fault_address : c->function); return 1; }
    c->v0 = (uint64_t)(int64_t)out;
    *rc = 0;
    return 1;
}

static int dispatch(EmArea01Runtime *r, EmArea01Call *c)
{
    if (r->area == 0 && c->function >= 0x00823500u)
        return dispatch_overlay00(r, c);
    if (r->area == 2 && c->function >= 0x00823500u)
        return dispatch_overlay02(r, c);
    int rc;
    if (dispatch_revisit(r, c, &rc))
        return rc;
    if (r->area == 2 && dispatch_math02(r, c, &rc))
        return rc;
    int32_t i32 = 0;
    uint32_t u32 = 0;
    uint64_t u64 = 0;
    switch (c->function) {
#include "game/em_area01_runtime_dispatch.inc"
    case 0x0019C6F0u: {
        if (c->na != 2 || c->nf) return fail(r, c->function);
        EmArea00World owner = {0};
        rc = em_area00_world_view_0019C6F0(&owner, bytes, r, (uint32_t)c->a[0],
                                           (uint32_t)c->a[1], &i32);
        if (rc < 0) return fail(r, owner.fault_address ? owner.fault_address : c->function);
        c->v0 = (uint64_t)(int64_t)i32;
        break;
    }
    case 0x001F4E20u: {
        if (c->na != 2 || c->nf != 1) return fail(r, c->function);
        EmArea00Fx owner = {.call = switch_marker, .ctx = r, .sp = c->sp};
        rc = em_area00_fx_001F4E20(&owner, c->a[0], c->a[1], c->f[0], &c->v0);
        if (rc < 0) return fail(r, owner.core.fault.address ? owner.core.fault.address : c->function);
        break;
    }
    case 0x00188610u:
        if (c->na != 1 || c->nf) return fail(r, c->function);
        rc = em_area01_room_00188610(&r->room, (uint32_t)c->a[0], &i32);
        if (rc == 0) c->v0 = (uint64_t)(int64_t)i32;
        break;
    case 0x00187DE0u:
        if (c->na != 1 || c->nf || c->sp < 0x20u) return fail(r, c->function);
        r->sp = c->sp - 0x20u;
        rc = em_area01_math_00187DE0(&r->math, (uint32_t)c->a[0]);
        break;
    case 0x00198D90u:
        if (c->na < 2) return fail(r, c->function);
        rc = em_area01_room_00198D90(&r->room, c->a[0], c->a[1]);
        break;
    case 0x001BB400u: case 0x001BB7C0u: case 0x001BB7F0u:
        if (c->na < 1) return fail(r, c->function);
        if (c->function == 0x001BB400u)
            rc = em_area01_room_001BB400(&r->room, (uint32_t)c->a[0], &i32);
        else if (c->function == 0x001BB7C0u)
            rc = em_area01_room_001BB7C0(&r->room, (uint32_t)c->a[0], &i32);
        else
            rc = em_area01_room_001BB7F0(&r->room, (uint32_t)c->a[0], &i32);
        if (rc == 0) c->v0 = (uint64_t)(int64_t)i32;
        break;
    case 0x001D0D60u:
        if (c->na < 1 || c->nf < 1) return fail(r, c->function);
        rc = em_area01_room_001D0D60(&r->room, (uint32_t)c->a[0], c->f[0], &i32);
        if (rc == 0) c->v0 = (uint64_t)(int64_t)i32;
        break;
    case 0x001E3D20u:
        if(c->na<2)return fail(r,c->function);
        rc=em_area01_side_001E3D20(&r->side,(uint32_t)c->a[0],(uint32_t)c->a[1]);
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
        else if (r->overlay00_fault.code) address = r->overlay00_fault.address;
        else if (r->overlay_fault.code) address = r->overlay_fault.address;
        else if (r->sys.fault) address = r->sys.fault_address;
        else if (r->exita.fault) address = r->exita.fault_address;
        else if (r->exitb.fault) address = r->exitb.fault_address;
        else if (r->room.fault) address = r->room.fault_address;
        else if (r->side.fault) address = r->side.fault_address;
        else if (r->revisit_fault.code) address = r->revisit_fault.address;
        else if (r->ret_fault.code) address = r->ret_fault.address;
        else if (r->overlay02_fault.code) address = r->overlay02_fault.address;
        return fail(r, address);
    }
    return r->fault ? -1 : 0;
}
