#include "game/em_area01_runtime.h"
#include "game/em_area01_state.h"
#include "game/em_area01_math_actor.h"
#include "game/em_area01_math_owner.h"
#include "game/em_area01_math_player.h"
#include "game/em_area01_light_owner.h"
#include "game/em_area00_world.h"
#include "game/em_area00_fx.h"
#include "game/em_script_door_fan.h"
#include "game/em_startup_load_gaps.h"
#include "game/em_area02_misc.h"
#include "game/em_area02_math.h"
#include "game/em_ee_float.h"
#include <stdio.h>
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

/* em_area02_math's boot routines (docs/AREA02_MATH.md: the node
 * behaviours, use tests, script callbacks and draw methods the revisit and
 * AREA02 run), over this runtime's bytes; every callee re-enters the runtime.
 * Dispatched for AREA02 (area 2; dispatch_math02): AREA00 / AREA01 keep
 * their existing live paths, except 001C8140 / 001C7EB0, the revisit's
 * placement spawn, which area 1 runs through spawn02 too. */
static uint8_t *math02_view(void *ctx, uint32_t address, uint32_t size)
{
    return bytes(ctx, address, size, 0);
}
static uint8_t *misc_view(void *ctx, uint32_t address, uint32_t size, int write)
{
    return bytes(ctx, address, size, write);
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
/* 001C8140(bank, index, behaviour) or 001C7EB0(node) through em_area02_math
 * at the call's sp; *v0 = 001C8140's register. 0, or -1 with *fault. */
static int spawn02(EmArea01Runtime *r, uint32_t sp, uint32_t fn, uint32_t a0, uint32_t a1, uint32_t a2,
                   uint64_t *v0, uint32_t *fault)
{
    EmArea02Math m;
    memset(&m, 0, sizeof m);
    m.call = math02_call;
    m.ctx = r;
    m.sp = sp;
    m.view = math02_view;
    int x = fn == 0x001C8140u ? em_area02_math_001C8140(&m, a0, a1, a2, v0) : em_area02_math_001C7EB0(&m, a0);
    if (x < 0 && fault) *fault = m.fault_address ? m.fault_address : fn;
    return x < 0 ? -1 : 0;
}

#include "game/em_area01_runtime_overlay.inc"
#include "game/em_area00_runtime_overlay.inc"
#include "game/em_area01_runtime_revisit.inc"
#include "game/em_area02_runtime_overlay.inc"
#include "game/em_area04_runtime_overlay.inc"
/* The middle of the route (AREA22, AREA06): the standalone level ports'
 * typed hooks into this runtime (tools/gen_mid_route_bridge.py). */
#include "game/em_area04_port.h"
#include "game/em_area06_port.h"
#include "game/em_area22_port.h"
#include "game/em_level8_port.h"
#include "game/em_level9_port.h"
#include "game/em_area13_route.h"
#include "game/em_level10_port.h"
#include "game/em_level11_port.h"
#include "game/em_level12_port.h"
#include "game/em_level13_port.h"
#include "game/em_level14_port.h"
#include "game/em_level15_creatures.h"
#include "game/em_area15_route.h"
#include "game/em_area03_route.h"
#include "game/em_area03_arrival.h"
#include "game/em_area19_route.h"
#include "game/em_level15_fountain.h"
#include "game/em_level15_motion.h"
#include "game/em_level15_perception.h"
#include "game/em_level15_support.h"
#include "game/em_pose_host_workers.h"
#include "game/em_aim_fire_binding.h"
#include "game/em_aim_fire_runtime.h"
#include "game/em_mid_route_bridge.inc"
#include "game/em_area01_runtime_area13.inc"

/* Boot routines of AREA22 / AREA06 that only the level ports translate:
 * 001C1A80 (the model-0x52 owner; em_area04_port, NEARMISS C checked
 * against the instructions, test_area04_port_reference over the AREA22
 * captures); 00183010 (its push of the player) and 0012D940 (the
 * 0012A5D0 bugs' +5 = 10 / 11 sub-state), em_area22_port's byte-matched C
 * (test_area22_port_reference). Returns 1 when it handled the call. */
/* AREA06's overlay owners and the boot routines em_area06_port translates
 * (byte-matched or NEARMISS C checked against the instructions,
 * test_area06_port_reference over the AREA06 captures; docs/AREA06_PORT.md).
 * The entries that keep a frame take the caller's sp (the original's at
 * entry; [sp - 0x100, sp) is the composition's stack); nested hooks run
 * below that frame. */
static int dispatch_a06(EmArea01Runtime *r, EmArea01Call *c, int *rc)
{
    const uint32_t fn = c->function, a0 = (uint32_t)c->a[0], a1 = (uint32_t)c->a[1];
    unsigned need_a = 1, need_f = 0;
    switch (fn) {
    case 0x00823580u: case 0x008240E0u: case 0x00824340u: case 0x00823B50u: case 0x00824560u:
    case 0x00219870u: case 0x00219F50u: case 0x0021A180u: case 0x0021AE90u: break;
    case 0x008242C0u: case 0x0021A440u: case 0x001EBE10u: case 0x001ECA20u: case 0x00823B10u: need_a = 2; break;
    case 0x001EA210u: need_a = 0; need_f = 1; break;
    default: return 0;
    }
    if (c->na < need_a || c->nf < need_f || c->sp < 0x100u) { *rc = fail(r, fn); return 1; }
    EmArea06PortHooks h;
    m06_hooks(r, &h);
    EmArea06PortFault f = {0, 0};
    const uint32_t sp = c->sp, saved = r->sp;
    r->sp = sp - 0x100u;
    int32_t out = 0;
    int x;
    switch (fn) {
    case 0x00823580u: x = em_area06_port_00823580(&h, a0, &f); break;
    case 0x008240E0u: x = em_area06_port_008240E0(&h, a0, &f); break;
    case 0x00824340u: x = em_area06_port_00824340(&h, a0, &f); break;
    case 0x00823B50u: x = em_area06_port_00823B50(&h, a0, sp, &f); break;
    case 0x00823B10u: x = em_area06_port_00823B10(&h, a0, a1, &f); break;
    case 0x00824560u: x = em_area06_port_00824560(&h, a0, sp, &f); break;
    case 0x00219870u: x = em_area06_port_00219870(&h, a0, sp, &f); break;
    case 0x0021AE90u: x = em_area06_port_0021AE90(&h, a0, &f); break;
    case 0x00219F50u: x = em_area06_port_00219F50(&h, a0, &f); break;
    case 0x0021A180u: x = em_area06_port_0021A180(&h, a0, sp, &out, &f); break;
    case 0x008242C0u: x = em_area06_port_008242C0(&h, a0, a1, &out, &f); break;
    case 0x0021A440u: x = em_area06_port_0021A440(&h, a0, a1, &f); break;
    case 0x001EBE10u: x = em_area06_port_001EBE10(&h, a0, a1, &f); break;
    case 0x001ECA20u: x = em_area06_port_001ECA20(&h, a0, a1, &f); break;
    default: x = em_area06_port_001EA210(&h, em_ee_float(c->f[0]), &f); break;
    }
    r->sp = saved;
    if (x < 0) {
        fprintf(stderr, "em_area01: em_area06_port %08X fault code %d at %08X\n", (unsigned)fn, (int)f.code,
                (unsigned)f.address);
        *rc = fail(r, f.address ? f.address : fn);
        return 1;
    }
    c->v0 = (uint64_t)(int64_t)out;
    *rc = 0;
    return 1;
}

/* The infection node 001F8350 (0x80000048): a boot owner, in whatever area
 * the player turns (0021C190's spawn), em_area06_port's translation. */
static int dispatch_infection(EmArea01Runtime *r, EmArea01Call *c)
{
    if (c->na < 1 || c->nf || c->sp < 0x100u) return fail(r, c->function);
    EmArea06PortHooks h;
    m06_hooks(r, &h);
    EmArea06PortFault f = {0, 0};
    const uint32_t sp = c->sp, saved = r->sp;
    r->sp = sp - 0x100u;
    const int x = em_area06_port_001F8350(&h, (uint32_t)c->a[0], sp, &f);
    r->sp = saved;
    if (x < 0) {
        fprintf(stderr, "em_area01: em_area06_port 001F8350 fault code %d at %08X\n", (int)f.code,
                (unsigned)f.address);
        return fail(r, f.address ? f.address : c->function);
    }
    return 0;
}

static int dispatch_mid(EmArea01Runtime *r, EmArea01Call *c, int *rc)
{
    /* em_area06_port's boot routines serve any area that spawns them (AREA15
     * sub 1's, AREA03's and AREA13's 00219870 records); its overlay owners only
     * AREA06. AREA13's own owners come first. */
    if (r->area == 0x0D && dispatch_a13(r, c, rc))
        return 1;
    if ((r->area == 6 || c->function < 0x00823500u) && dispatch_a06(r, c, rc))
        return 1;
    if (c->function == 0x00183010u || c->function == 0x0012D940u) {
        if (c->na < 2 || c->nf) { *rc = fail(r, c->function); return 1; }
        EmArea22PortHooks h;
        m22_hooks(r, &h);
        EmArea22PortFault f = {0, 0};
        const int x = c->function == 0x00183010u
            ? em_area22_port_00183010(&h, (uint32_t)c->a[0], (uint32_t)c->a[1], &f)
            : em_area22_port_0012D940(&h, (uint32_t)c->a[0], (uint32_t)c->a[1], &f);
        *rc = x < 0 ? fail(r, f.address ? f.address : c->function) : 0;
        return 1;
    }
    if (c->function == 0x001C1A80u) {
        if (c->na < 1 || c->nf) { *rc = fail(r, c->function); return 1; }
        EmArea04PortHooks h;
        m04_hooks(r, &h);
        EmArea04PortFault f = {0, 0};
        *rc = em_area04_port_001C1A80(&h, (uint32_t)c->a[0], &f) < 0 ? fail(r, f.address ? f.address : c->function)
                                                                     : 0;
        return 1;
    }
    return 0;
}

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
    /* the areas em_area01_state binds: AREA00..02, AREA04, AREA06, AREA13, AREA15, AREA03, AREA19, AREA22 */
    if (!r || !host || !host->bytes || !host->worker || !(area <= 2 || area == 4 || area == 6 || area == 0x0D ||
                                                     area == 0x16 || area == 0x0F || area == 3 || area == 0x13))
        return -1;
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
    overlay04_workers(r); r->overlay04.w_callback = overlay_callback;
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

/* AREA04's overlay entries (em_area04_port: the 21 owners and callees the
 * AREA04 census found; docs/AREA04_PORT.md) and the boot owner 001C1A80.
 * Each takes its original arguments; 0x824320 and 0x824490 take the
 * caller's sp. Nested hooks get the entry's sp less the deepest chain of
 * original frames below it (0x823700 0x20 -> 0x823580 0x40; 0x8246B0 0x20
 * -> 0x824830 / 0x824930 0x30; 0x824320 0x20 -> 0x824490 0x70; read from
 * each function's first instruction). Any other overlay address goes to
 * the host's workers (an unbound one is the fail-stop). */
static int dispatch_overlay04(EmArea01Runtime *r, EmArea01Call *c)
{
    static const struct { uint32_t fn, frame; } frames[] = {
        {0x00823580u, 0x40u}, {0x00823700u, 0x60u}, {0x00823B40u, 0x10u}, {0x00823B90u, 0x30u},
        {0x00823EE0u, 0x20u}, {0x00824100u, 0x20u}, {0x008241F0u, 0x20u}, {0x00824320u, 0x90u},
        {0x00824490u, 0x70u}, {0x008246B0u, 0x50u}, {0x00824830u, 0x30u}, {0x00824930u, 0x30u},
        {0x00824DC0u, 0x20u}, {0x00825510u, 0x30u}, {0x00825880u, 0x30u}, {0x00825B00u, 0x20u},
        {0x00825D60u, 0x20u}, {0x00825DF0u, 0x30u}, {0x008260C0u, 0x20u}, {0x00824A40u, 0x20u},
        {0x00824A90u, 0xC0u}};
    uint32_t frame = 0xFFFFFFFFu;
    for (unsigned i = 0; i < sizeof frames / sizeof frames[0]; ++i)
        if (frames[i].fn == c->function) frame = frames[i].frame;
    if (c->function == 0x001C1A80u) frame = 0;
    if (frame == 0xFFFFFFFFu) return r->host.worker(r->host.ctx, c);
    const unsigned na = c->function == 0x00823B40u ? 0u : c->function == 0x00823580u ? 2u :
                        c->function == 0x00824A90u ? 3u : 1u;
    if (c->na < na || c->nf || c->sp < frame) return fail(r, c->function);
    r->sp = c->sp - frame;
    const EmArea04PortHooks *h = &r->overlay04;
    EmArea04PortFault *f = &r->overlay04_fault;
    const uint32_t a0 = (uint32_t)c->a[0];
    int32_t result = 0;
    int rc;
    switch (c->function) {
    case 0x00823700u: return em_area04_port_00823700(h, a0, f);
    case 0x00823B90u: return em_area04_port_00823B90(h, a0, f);
    case 0x00823EE0u: return em_area04_port_00823EE0(h, a0, f);
    case 0x00824100u: return em_area04_port_00824100(h, a0, f);
    case 0x008241F0u: return em_area04_port_008241F0(h, a0, f);
    case 0x00824320u: return em_area04_port_00824320(h, a0, c->sp, f);
    case 0x00824490u: return em_area04_port_00824490(h, a0, c->sp, f);
    case 0x008246B0u: return em_area04_port_008246B0(h, a0, f);
    case 0x00824830u: return em_area04_port_00824830(h, a0, f);
    case 0x00824930u: return em_area04_port_00824930(h, a0, f);
    case 0x00824DC0u: return em_area04_port_00824DC0(h, a0, f);
    case 0x00825510u: return em_area04_port_00825510(h, a0, f);
    case 0x00825880u: return em_area04_port_00825880(h, a0, f);
    case 0x00825B00u: return em_area04_port_00825B00(h, a0, f);
    case 0x00825D60u: return em_area04_port_00825D60(h, a0, f);
    case 0x00825DF0u: return em_area04_port_00825DF0(h, a0, f);
    case 0x008260C0u: return em_area04_port_008260C0(h, a0, f);
    case 0x001C1A80u: return em_area04_port_001C1A80(h, a0, f);
    case 0x00823580u: rc = em_area04_port_00823580(h, a0, (uint32_t)c->a[1], &result, f); break;
    case 0x00824A40u: rc = em_area04_port_00824A40(h, c->sp, &result, f); break;
    case 0x00824A90u:
        rc = em_area04_port_00824A90(h, a0, (uint32_t)c->a[1], (uint32_t)c->a[2], c->sp, &result, f);
        break;
    default:          rc = em_area04_port_00823B40(h, &result, f); break;
    }
    if (rc == 0) c->v0 = (uint64_t)(int64_t)result;
    return rc;
}

/* The eighth level's entries (em_level8_port: the a04b census's new
 * functions; docs/LEVEL8_PORT.md), bound in AREA04 through the generated
 * hooks m08_hooks (em_mid_route_bridge.inc). Each takes its original
 * arguments; the entries with stack locals take the caller's sp. Nested
 * hooks get the entry's sp less the entry's own frame (read from its first
 * instruction) and EIGHTH_NEST below it, deeper than any chain of original
 * frames these entries call each other through (the deepest, 001A7BA0's
 * 0x290 under 001A7B80), so no callee frame overlaps a local the module
 * writes. Returns 0 when the address is not one of them. */
enum { EIGHTH_NEST = 0x400u };
static int dispatch_level8(EmArea01Runtime *r, EmArea01Call *c, int *out)
{
    static const struct { uint32_t fn, frame; unsigned na, nf; } entries[] = {
        {0x001BBD20u, 0x00u, 2, 0}, {0x001BC560u, 0x00u, 0, 0}, {0x001BC6D0u, 0x00u, 2, 0},
        {0x001BC740u, 0x40u, 2, 0}, {0x001BC860u, 0x40u, 2, 0}, {0x001BD180u, 0x00u, 1, 0},
        {0x001BD270u, 0x00u, 1, 0}, {0x001BD9F0u, 0x40u, 1, 0}, {0x001BDE60u, 0x30u, 1, 0},
        {0x0012E3A0u, 0x30u, 1, 0}, {0x0012E560u, 0x30u, 2, 0}, {0x0012E840u, 0x30u, 2, 0},
        {0x0012EB60u, 0x30u, 2, 0}, {0x0012F100u, 0x30u, 2, 0}, {0x0012FC10u, 0x30u, 2, 0},
        {0x00131ED0u, 0x00u, 1, 0}, {0x00131F90u, 0x60u, 2, 0}, {0x00132490u, 0x60u, 2, 0},
        {0x001328D0u, 0x50u, 2, 0}, {0x00132FB0u, 0x50u, 2, 0}, {0x001333F0u, 0x30u, 2, 0},
        {0x00133640u, 0x40u, 2, 0}, {0x00133A20u, 0x40u, 2, 0}, {0x00133DB0u, 0x00u, 2, 0},
        {0x0011E0A8u, 0x00u, 1, 1}, {0x0021BE40u, 0x20u, 1, 0}, {0x001B4810u, 0x40u, 1, 0},
        {0x001A7B80u, 0x00u, 1, 0}, {0x001A7BA0u, 0x290u, 4, 0}, {0x001B1560u, 0x30u, 2, 1},
        {0x001B2B10u, 0x40u, 3, 0}, {0x001B2BF0u, 0x60u, 3, 1}, {0x001B30E0u, 0x80u, 2, 0},
        {0x001B32F0u, 0x50u, 2, 1}, {0x001B3440u, 0x50u, 3, 1}, {0x001B3580u, 0x30u, 2, 0},
        {0x001B55E0u, 0x30u, 2, 0}, {0x001EAD70u, 0x30u, 2, 0}, {0x001ED100u, 0x30u, 2, 0},
        {0x001F91C0u, 0xD0u, 1, 0}, {0x00823920u, 0x10u, 1, 0}, {0x008239A0u, 0x30u, 1, 0},
        {0x00823A90u, 0x10u, 1, 0}, {0x008245F0u, 0x20u, 1, 0}, {0x001284E0u, 0x50u, 4, 0}};
    unsigned k = 0, n = sizeof entries / sizeof entries[0];
    while (k < n && entries[k].fn != c->function) ++k;
    if (k == n) return 0;
    const uint32_t frame = entries[k].frame;
    if (c->na < entries[k].na || c->nf < entries[k].nf || c->sp < frame + EIGHTH_NEST) {
        *out = fail(r, c->function);
        return 1;
    }
    r->sp = c->sp - frame - EIGHTH_NEST;
    EmLevel8PortHooks hooks;
    m08_hooks(r, &hooks);                /* em_mid_route_bridge.inc's generated hooks */
    const EmLevel8PortHooks *h = &hooks;
    EmLevel8PortFault fault = {0, 0}, *f = &fault;
    const uint32_t a0 = (uint32_t)c->a[0], a1 = (uint32_t)c->a[1], a2 = (uint32_t)c->a[2],
                   a3 = (uint32_t)c->a[3];
    float f12;
    memcpy(&f12, &c->f[0], 4);
    int32_t i = 0;
    float x = 0;
    int rc, has_i = 0, has_f = 0;
    switch (c->function) {
    case 0x001BBD20u: rc = em_level8_port_001BBD20(h, a0, (int32_t)a1, &i, f); has_i = 1; break;
    case 0x001BC560u: rc = em_level8_port_001BC560(h, &i, f); has_i = 1; break;
    case 0x001BC6D0u: rc = em_level8_port_001BC6D0(h, a0, a1, &i, f); has_i = 1; break;
    case 0x001BC740u: rc = em_level8_port_001BC740(h, a0, a1, &i, f); has_i = 1; break;
    case 0x001BC860u: rc = em_level8_port_001BC860(h, a0, a1, &i, f); has_i = 1; break;
    case 0x001BD180u: rc = em_level8_port_001BD180(h, a0, &i, f); has_i = 1; break;
    case 0x001BD270u: rc = em_level8_port_001BD270(h, a0, &i, f); has_i = 1; break;
    case 0x001BD9F0u: rc = em_level8_port_001BD9F0(h, a0, f); break;
    case 0x001BDE60u: rc = em_level8_port_001BDE60(h, a0, f); break;
    case 0x0012E3A0u: rc = em_level8_port_0012E3A0(h, a0, c->sp, f); break;
    case 0x0012E560u: rc = em_level8_port_0012E560(h, a0, a1, f); break;
    case 0x0012E840u: rc = em_level8_port_0012E840(h, a0, a1, c->sp, f); break;
    case 0x0012EB60u: rc = em_level8_port_0012EB60(h, a0, a1, c->sp, f); break;
    case 0x0012F100u: rc = em_level8_port_0012F100(h, a0, a1, c->sp, f); break;
    case 0x0012FC10u: rc = em_level8_port_0012FC10(h, a0, a1, c->sp, f); break;
    case 0x00131ED0u: rc = em_level8_port_00131ED0(h, a0, f); break;
    case 0x00131F90u: rc = em_level8_port_00131F90(h, a0, a1, c->sp, f); break;
    case 0x00132490u: rc = em_level8_port_00132490(h, a0, a1, c->sp, f); break;
    case 0x001328D0u: rc = em_level8_port_001328D0(h, a0, a1, c->sp, f); break;
    case 0x00132FB0u: rc = em_level8_port_00132FB0(h, a0, a1, c->sp, &i, f); has_i = 1; break;
    case 0x001333F0u: rc = em_level8_port_001333F0(h, a0, a1, &i, f); has_i = 1; break;
    case 0x00133640u: rc = em_level8_port_00133640(h, a0, a1, &i, f); has_i = 1; break;
    case 0x00133A20u: rc = em_level8_port_00133A20(h, a0, a1, &i, f); has_i = 1; break;
    case 0x00133DB0u: rc = em_level8_port_00133DB0(h, a0, a1, f); break;
    case 0x0011E0A8u: rc = em_level8_port_0011E0A8(h, a0, f12, &x, f); has_f = 1; break;
    case 0x0021BE40u: rc = em_level8_port_0021BE40(h, a0, &i, f); has_i = 1; break;
    case 0x001B4810u: rc = em_level8_port_001B4810(h, a0, f); break;
    case 0x001A7B80u: rc = em_level8_port_001A7B80(h, a0, c->sp, &i, f); has_i = 1; break;
    case 0x001A7BA0u: rc = em_level8_port_001A7BA0(h, a0, a1, a2, a3, c->sp, &i, f); has_i = 1; break;
    case 0x001B1560u: rc = em_level8_port_001B1560(h, a0, a1, f12, &i, f); has_i = 1; break;
    case 0x001B2B10u: rc = em_level8_port_001B2B10(h, a0, a1, a2, f); break;
    case 0x001B2BF0u: rc = em_level8_port_001B2BF0(h, a0, a1, a2, f12, c->sp, &i, f); has_i = 1; break;
    case 0x001B30E0u: rc = em_level8_port_001B30E0(h, a0, a1, &i, f); has_i = 1; break;
    case 0x001B32F0u: rc = em_level8_port_001B32F0(h, a0, a1, f12, c->sp, &i, f); has_i = 1; break;
    case 0x001B3440u: rc = em_level8_port_001B3440(h, a0, a1, a2, f12, c->sp, &i, f); has_i = 1; break;
    case 0x001B3580u: rc = em_level8_port_001B3580(h, a0, a1, &x, f); has_f = 1; break;
    case 0x001B55E0u: rc = em_level8_port_001B55E0(h, a0, (int32_t)a1, f); break;
    case 0x001EAD70u: rc = em_level8_port_001EAD70(h, (int32_t)a0, (int32_t)a1, f); break;
    case 0x001ED100u: rc = em_level8_port_001ED100(h, (int32_t)a0, (int32_t)a1, f); break;
    case 0x001F91C0u: rc = em_level8_port_001F91C0(h, a0, c->sp, f); break;
    case 0x00823920u: rc = em_level8_port_00823920(h, a0, f); break;
    case 0x008239A0u: rc = em_level8_port_008239A0(h, a0, f); break;
    case 0x00823A90u: rc = em_level8_port_00823A90(h, a0, f); break;
    case 0x001284E0u: rc = em_level8_port_001284E0(h, a0, a1, (int32_t)a2, a3, &i, f); has_i = 1; break;
    default:          rc = em_level8_port_008245F0(h, a0, f); break;
    }
    if (rc < 0) {
        fprintf(stderr, "em_area01: em_level8_port %08X fault code %d at %08X\n", (unsigned)c->function,
                (int)fault.code, (unsigned)fault.address);
        *out = fail(r, fault.address ? fault.address : c->function);
        return 1;
    }
    if (has_i) c->v0 = (uint64_t)(int64_t)i;
    if (has_f) memcpy(&c->f0, &x, 4);
    *out = 0;
    return 1;
}

static int dispatch_math02(EmArea01Runtime *r, EmArea01Call *c, int *rc)
{
    static const struct { uint32_t fn; unsigned na; } entries[] = {
        {0x001575E0u, 1}, {0x00157860u, 3}, {0x00157B30u, 2}, {0x00157F30u, 1}, {0x00158050u, 1}, {0x001580C0u, 1},
        {0x001582E0u, 1},
        {0x00158EC0u, 1}, {0x00159210u, 1}, {0x00159620u, 1}, {0x00159970u, 1}, {0x00159E70u, 1}, {0x00183C40u, 2},
        {0x001B18F0u, 3}, {0x001BC960u, 1}, {0x001BD560u, 1}, {0x001BDFC0u, 1}, {0x001C48C0u, 1},
        {0x001C4960u, 1}, {0x001C4AF0u, 1}, {0x001C4CB0u, 1}, {0x001C7EB0u, 1}, {0x001C8140u, 3}};
    /* The draw methods 001CAE40 / 001CAF60 / 001CB4F0 / 001CB580 are the
     * draw host's (em_area01_model_draw -> em_owner_draw_live), not this
     * dispatch: their callees are the render context's. */
    unsigned na = 99;
    for (unsigned i = 0; i < sizeof entries / sizeof entries[0]; ++i)
        if (entries[i].fn == c->function) na = entries[i].na;
    if (na == 99) return 0;
    if (c->na < na || c->nf) { *rc = fail(r, c->function); return 1; }
    if (c->function == 0x001C7EB0u || c->function == 0x001C8140u) {
        uint32_t f = 0;
        *rc = spawn02(r, c->sp, c->function, (uint32_t)c->a[0], (uint32_t)c->a[1], (uint32_t)c->a[2], &c->v0, &f);
        if (*rc < 0) *rc = fail(r, f);
        return 1;
    }
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
    case 0x00157860u: x = em_area02_math_00157860(&m, a0, a1, a2, &out); break;
    case 0x00157B30u: x = em_area02_math_00157B30(&m, a0, a1, &out); break;
    case 0x00157F30u: x = em_area02_math_00157F30(&m, a0, &out); break;
    case 0x00158050u: x = em_area02_math_00158050(&m, a0, &out); break;
    case 0x001580C0u: x = em_area02_math_001580C0(&m, a0, &out); break;
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
    default:          x = em_area02_math_001C4CB0(&m, a0); break;
    }
    if (x < 0) { *rc = fail(r, m.fault_address ? m.fault_address : c->function); return 1; }
    c->v0 = (uint64_t)(int64_t)out;
    *rc = 0;
    return 1;
}

#include "game/em_area01_runtime_boss.inc"
#include "game/em_area01_runtime_a19.inc"
#include "game/em_area01_runtime_area15.inc"
#include "game/em_area01_runtime_later.inc"

int em_area01_runtime_later(EmArea01Runtime *r, EmArea01Call *c, int *rc)
{
    return r && c && rc && !r->fault && dispatch_later(r, c, rc);
}

/* The areas whose overlay owners are their own dispatch's (the later areas
 * in the shared composition); any other overlay address there goes to the
 * host's workers (fail-stop). */
static int mid_host(unsigned area)
{
    return area == 0x16 || area == 6 || area == 0x0D || area == 0x0F || area == 3 || area == 0x13;
}

static int dispatch(EmArea01Runtime *r, EmArea01Call *c)
{
    if (r->area == 0 && c->function >= 0x00823500u)
        return dispatch_overlay00(r, c);
    if (r->area == 2 && c->function >= 0x00823500u)
        return dispatch_overlay02(r, c);
    int rc;
    if (r->area == 0x0F && c->function == 0x001CB2B0u) {
        /* 0x826850's +0xD 0xD method (kind 12): 001CB200's class-6 draw on
         * channel 3 (the screen-copy pass 001D4440) has no live chain page
         * yet. Nothing but the picture depends on it: its packet, its
         * colour words D_00251C20..2B and its scratch matrices. Open item,
         * docs/AREA15_PORT.md. */
        static int reported;
        if (!reported) fprintf(stderr, "em_area01: 001CB2B0: the class-6 draw of AREA15 sub 1's [3] is not drawn\n");
        reported = 1;
        return 0;
    }
    /* 001CB1F0 / 001CB130 / 001CB060: the draw methods of AREA19's tendril
     * field and worms (0x26E310 kinds 10, 9, 8). Their VU1 programs (0x0023D4D0
     * and 001D3F60's) have no translation yet: not drawn (an open item of
     * docs/LEVEL3_BINDING.md, lane J1). */
    if (r->area == 0x13 && (c->function == 0x001CB1F0u || c->function == 0x001CB130u || c->function == 0x001CB060u)) {
        static int noted;
        if (!noted++) fprintf(stderr, "em_area01: %08X: its VU1 program is not translated, the node is not drawn\n",
                              (unsigned)c->function);
        return 0;
    }
    if (r->area == 0x0F && dispatch_a15(r, c, &rc))
        return rc;
    if (r->area == 3 && dispatch_a03(r, c, &rc))
        return rc;
    if (r->area == 0x13 && c->function >= 0x00823500u)
        return dispatch_area19(r, c);
    /* The creature family's boot entries and the script actors serve AREA13,
     * AREA15, AREA03 and AREA19 too (their overlay addresses went to their own
     * dispatch or reach the host's fail-stop below). */
    const int later = (r->area == 0x0D || r->area == 0x0F || r->area == 3 || r->area == 0x13) &&
                      c->function < 0x00823500u;
    const int creatures = r->area == 4 || later;
    if (creatures && dispatch_level8(r, c, &rc))
        return rc;
    if (creatures && dispatch_boss(r, c, &rc))
        return rc;
    if (r->area == 4 && (c->function >= 0x00823500u || c->function == 0x001C1A80u))
        return dispatch_overlay04(r, c);
    if (creatures && c->function == 0x001BAC00u)
        return rv_001BAC00(r, c);          /* AREA04's arrival script op14 */
    if (creatures && c->function == 0x001BB0E0u)
        return rv_001BB0E0(r, c);          /* the script actors it places */
    if (creatures && c->function == 0x001F9660u)
        return rv_001F9660(r, c);          /* their command 6's sound copy (event [3]'s script, a04b_03) */
    /* AREA22's overlay holds no owner (its one function is the area init
     * 0x823580, em_area01_state's); AREA06's bound owners are dispatch_a06's.
     * Any other overlay address in either goes to the host's workers
     * (fail-stop), never to another area's overlay at the same address. */
    if (c->function == 0x0012B850u) {
        /* 0012B850(self, sub) (byte-matched C): the sub-state a bug 001284E0
         * spawned runs (0012A5D0's +5 = 8): em_level8_port's. */
        if (c->na < 2 || c->nf) return fail(r, c->function);
        EmLevel8PortHooks h;
        m08_hooks(r, &h);
        EmLevel8PortFault f = {0, 0};
        if (em_level8_port_0012B850(&h, (uint32_t)c->a[0], (uint32_t)c->a[1], &f) < 0)
            return fail(r, f.address ? f.address : c->function);
        return 0;
    }
    if (c->function == 0x001BFF90u) {
        /* 001BFF90(obj, p, clip) (byte-matched C): the clip set once, which
         * 001BFFD0 calls on AREA01's way back: em_level8_port's. */
        if (c->na < 3 || c->nf) return fail(r, c->function);
        EmLevel8PortHooks h;
        m08_hooks(r, &h);
        EmLevel8PortFault f = {0, 0};
        if (em_level8_port_001BFF90(&h, (uint32_t)c->a[0], (uint32_t)c->a[1], (int32_t)c->a[2], &f) < 0)
            return fail(r, f.address ? f.address : c->function);
        return 0;
    }
    if (c->function == 0x001BF6B0u || c->function == 0x001BE6C0u) {
        /* 001BF6B0 (byte-matched C): the behaviour AREA01's 001C02E0 stores
         * once D_00810845 bit 5 is set (the AREA06 event; the upper floor's
         * way back, a01v), and 001BE6C0, the companion its 001BEAC0 spawns:
         * em_level8_port's translations through the generated hooks
         * (test_level8_port_reference; docs/LEVEL8_PORT.md). */
        if (c->na < 1 || c->nf) return fail(r, c->function);
        EmLevel8PortHooks h;
        m08_hooks(r, &h);
        EmLevel8PortFault f = {0, 0};
        const int x = c->function == 0x001BF6B0u ? em_level8_port_001BF6B0(&h, (uint32_t)c->a[0], &f)
                                                 : em_level8_port_001BE6C0(&h, (uint32_t)c->a[0], &f);
        if (x < 0) {
            fprintf(stderr, "em_area01: em_level8_port %08X fault code %d at %08X\n", (unsigned)c->function,
                    (int)f.code, (unsigned)f.address);
            return fail(r, f.address ? f.address : c->function);
        }
        return 0;
    }
    if (c->function == 0x001F8350u)
        return dispatch_infection(r, c);
    if ((mid_host(r->area) || (r->area == 4 && c->function == 0x0012D940u)) && dispatch_mid(r, c, &rc))
        return rc;
    if (mid_host(r->area) && c->function >= 0x00823500u)
        return r->host.worker(r->host.ctx, c);
    if (dispatch_revisit(r, c, &rc))
        return rc;
    if ((r->area == 2 || creatures) && c->function == 0x001F4E40u) {
        /* 001F4E40(pos, colour, rate; f12): em_area02_misc's translation
         * over this runtime's bytes (0x70003B68 and the scratch blocks). */
        if (c->na < 3 || c->nf < 1) return fail(r, c->function);
        EmArea02Misc m;
        memset(&m, 0, sizeof m);
        m.call = rv_misc_call;
        m.ctx = r;
        m.sp = c->sp;
        if (em_area02_misc_view_001F4E40(&m, misc_view, (uint32_t)c->a[0], (uint32_t)c->a[1], (uint32_t)c->a[2],
                                         c->f[0]) < 0)
            return fail(r, m.fault_address ? m.fault_address : c->function);
        return 0;
    }
    if ((r->area == 2 || r->area == 4 || r->area == 6 || r->area == 0x0D || r->area == 0x0F || r->area == 3 ||
         r->area == 0x13) &&
        c->function == 0x001E4CE0u) {
        /* The burst emitter node 001E4CE0 (spawned by the car's 001EFD20,
         * by a drum's in AREA04 and by AREA06's g[11] 00219870):
         * em_area02_misc's translation over this runtime's bytes. */
        if (c->na < 1 || c->nf) return fail(r, c->function);
        EmArea02Misc m;
        memset(&m, 0, sizeof m);
        m.call = rv_misc_call;
        m.ctx = r;
        m.sp = c->sp;
        if (em_area02_misc_view_001E4CE0(&m, misc_view, (uint32_t)c->a[0]) < 0)
            return fail(r, m.fault_address ? m.fault_address : c->function);
        return 0;
    }
    if (r->area == 2 && c->function == 0x001AA700u) {
        /* 001AA700(car): em_area02_misc's translation over this runtime's
         * bytes (D_00275B7C / D_00275B84 and the records they name). */
        if (c->na < 1 || c->nf) return fail(r, c->function);
        EmArea02Misc m;
        memset(&m, 0, sizeof m);
        m.call = rv_misc_call;
        m.ctx = r;
        m.sp = c->sp;
        if (em_area02_misc_view_001AA700(&m, misc_view, (uint32_t)c->a[0]) < 0)
            return fail(r, m.fault_address ? m.fault_address : c->function);
        return 0;
    }
    /* em_area02_math's boot routines (the panels, the bed, the lift, the
     * light owners 001C48C0..): boot code, so AREA04's, AREA06's, AREA13's
     * and AREA22's too. */
    if ((r->area == 2 || r->area == 4 || mid_host(r->area)) && dispatch_math02(r, c, &rc))
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
    case 0x001FB0B0u: {
        /* 001FB0B0(cue) (byte-matched C): D_00810D38 = cue; 001FAE70(1). */
        if (c->na != 1 || c->nf) return fail(r, c->function);
        uint8_t *cue = bytes(r, 0x00810D38u, 4, 1);
        if (!cue) return fail(r, 0x00810D38u);
        const int32_t v = (int32_t)c->a[0];
        memcpy(cue, &v, 4);
        EmArea01Call music = {.function = 0x001FAE70u, .sp = c->sp, .na = 1, .nf = 0, .a = {1}};
        rc = em_area01_runtime_call(r, &music);
        break;
    }
    case 0x0019A310u: {
        /* 0019A310(out) (byte-matched C; em_player_slope_angle's float
         * operations): the slope of the stage record's normal. AREA04's
         * creature family (a04b_03). */
        if (!creatures) return r->host.worker(r->host.ctx, c);
        if (c->na != 1 || c->nf) return fail(r, c->function);
        const uint8_t *word = bytes(r, 0x700031D0u, 4, 0);
        if (!word) return fail(r, 0x700031D0u);
        uint32_t rec, n[3];
        memcpy(&rec, word, 4);
        c->v0 = 0;
        if (!rec) { rc = 0; break; }
        const uint8_t *normal = bytes(r, rec + 0x24u, 12, 0);
        if (!normal) return fail(r, rec + 0x24u);
        memcpy(n, normal, 12);
        for (unsigned k = 0; k < 3; ++k) {
            uint8_t *w = bytes(r, 0x70003680u + 4u * k, 4, 1);
            if (!w) return fail(r, 0x70003680u + 4u * k);
            memcpy(w, &n[k], 4);
        }
        EmArea01Call root = {.function = 0x0011E748u, .sp = c->sp, .na = 0, .nf = 1};
        root.f[0] = em_ee_madd_bits(em_ee_mula_bits(n[0], n[0]), n[2], n[2]);
        rc = em_area01_runtime_call(r, &root);
        if (rc < 0) break;
        uint32_t ratio = 0x7F7FC99Eu;       /* 3.4e38 below 0.0001 */
        if (!(em_ee_float(root.f0) < 1.0e-4f)) {
            EmArea01Call fabs_call = {.function = 0x0011DF78u, .sp = c->sp, .na = 0, .nf = 1};
            fabs_call.f[0] = n[1];
            rc = em_area01_runtime_call(r, &fabs_call);
            if (rc < 0) break;
            ratio = em_ee_div_bits(fabs_call.f0, root.f0);
        }
        for (unsigned k = 0; k < 2; ++k) {
            uint8_t *w = bytes(r, 0x70003680u + 4u * k, 4, 1);
            if (!w) return fail(r, 0x70003680u + 4u * k);
            memcpy(w, k ? &ratio : &root.f0, 4);
        }
        EmArea01Call atan_call = {.function = 0x0011DBB8u, .sp = c->sp, .na = 0, .nf = 1};
        atan_call.f[0] = ratio;
        rc = em_area01_runtime_call(r, &atan_call);
        if (rc < 0) break;
        uint32_t angle = em_ee_sub_bits(0x3FC90FDBu, atan_call.f0);
        if (em_ee_float(ratio) < 0.0f) angle ^= 0x80000000u;
        uint8_t *out = bytes(r, (uint32_t)c->a[0], 4, 1);
        if (!out) return fail(r, (uint32_t)c->a[0]);
        memcpy(out, &angle, 4);
        c->v0 = 1;
        break;
    }
    case 0x001B15D0u: {
        /* 001B15D0(a, b) (byte-matched C, mwcc 2.3.3): 001028D0(D_70003600,
         * a, b), then 0011E748 of x*x + y*y + z*z over 0x70003600 (mul.s,
         * mul.s, adda.s, madd.s); the result in f0. AREA04's creature
         * 0012E3A0 measures with it (a04b_03). */
        if (!creatures) return r->host.worker(r->host.ctx, c);   /* elsewhere: the host's */
        if (c->na != 2 || c->nf) return fail(r, c->function);
        EmArea01Call sub = {.function = 0x001028D0u, .sp = c->sp, .na = 3, .nf = 0};
        sub.a[0] = 0x70003600u;
        sub.a[1] = c->a[0];
        sub.a[2] = c->a[1];
        rc = em_area01_runtime_call(r, &sub);
        if (rc < 0) break;
        const uint8_t *v = bytes(r, 0x70003600u, 12, 0);
        if (!v) return fail(r, 0x70003600u);
        uint32_t x, y, z;
        memcpy(&x, v, 4); memcpy(&y, v + 4, 4); memcpy(&z, v + 8, 4);
        const uint32_t acc = em_ee_adda_bits(em_ee_mul_bits(x, x), em_ee_mul_bits(y, y));
        EmArea01Call root = {.function = 0x0011E748u, .sp = c->sp, .na = 0, .nf = 1};
        root.f[0] = em_ee_madd_bits(acc, z, z);
        rc = em_area01_runtime_call(r, &root);
        if (rc == 0) c->f0 = root.f0;
        break;
    }
    case 0x001B6F80u: {
        /* 001B6F80(point; f12 yaw) (byte-matched C): D_00810374 (the
         * player's +0xC4) = yaw, then 00182F90(D_008102B0, point). */
        if (c->na != 1 || c->nf != 1) return fail(r, c->function);
        uint8_t *yaw = bytes(r, 0x00810374u, 4, 1);
        if (!yaw) return fail(r, 0x00810374u);
        memcpy(yaw, &c->f[0], 4);
        EmArea01Call align = {.function = 0x00182F90u, .sp = c->sp, .na = 2, .nf = 0};
        align.a[0] = 0x008102B0u;
        align.a[1] = (uint64_t)(int64_t)(int32_t)(uint32_t)c->a[0];
        rc = em_area01_runtime_call(r, &align);
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
    case 0x001BB400u: case 0x001BB7C0u: case 0x001BB7F0u: case 0x001BB310u:
        if (c->na < 1) return fail(r, c->function);
        if (c->function == 0x001BB310u)
            rc = em_area01_room_001BB310(&r->room, (uint32_t)c->a[0], &i32);
        else if (c->function == 0x001BB400u)
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
        else if (r->overlay04_fault.code) address = r->overlay04_fault.address;
        return fail(r, address);
    }
    return r->fault ? -1 : 0;
}
