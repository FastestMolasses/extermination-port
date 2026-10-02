/* em_aim_fire_marker.c - see em_aim_fire_marker.h. Every address in a
 * comment is the original instruction translated there. */
#include "game/em_aim_fire_marker.h"

#include "game/em_ee_float.h"

#define F_0    UINT32_C(0x00000000)
#define F_1    UINT32_C(0x3F800000)
#define F_PI   UINT32_C(0x40490FDB)
#define F_300  UINT32_C(0x43960000)
#define D_810374 UINT32_C(0x00810374)   /* the player's +0xC4 */
#define D_2754E0 UINT32_C(0x002754E0)

typedef struct {
    EmAimFireTarget *h;
    uint32_t sp;
} Run;

static void fault(Run *r, int code, uint32_t address)
{
    if (!r->h->fault) {
        r->h->fault = code;
        r->h->fault_function = 0x0018ABA0u;
        r->h->fault_address = address;
    }
}

static uint8_t *memory(Run *r, uint32_t address, uint32_t size, int write)
{
    if (r->h->fault) return NULL;
    if (!r->h->map) {
        fault(r, 1, address);
        return NULL;
    }
    uint8_t *p = r->h->map(r->h->context, address, size, write);
    if (!p) fault(r, 2, address);
    return p;
}

static uint32_t readn(Run *r, uint32_t a, unsigned n)
{
    const uint8_t *p = memory(r, a, n, 0);
    uint32_t v = 0;
    if (p)
        for (unsigned i = 0; i < n; ++i) v |= (uint32_t)p[i] << (8 * i);
    return v;
}

static void put(Run *r, uint32_t a, uint32_t v, unsigned n)
{
    uint8_t *p = memory(r, a, n, 1);
    if (!p) return;
    for (unsigned i = 0; i < n; ++i) p[i] = (uint8_t)(v >> (8 * i));
    if (r->h->store) r->h->store(r->h->context, a, n);
}

static uint64_t sx(uint32_t v) { return (uint64_t)(int64_t)(int32_t)v; }

static EmAimFireTargetCall call(Run *r, uint32_t fn, unsigned na, unsigned nf, uint32_t a0, uint32_t a1,
                                uint32_t a2, uint32_t a3, uint32_t f12)
{
    EmAimFireTargetCall c = {fn, r->sp, {sx(a0), sx(a1), sx(a2), sx(a3), 0, 0, 0},
                             {f12, 0, 0, 0, 0, 0, 0, 0}, na, nf, 0, 0};
    if (r->h->fault) return c;
    if (!r->h->call) fault(r, 1, fn);
    else if (r->h->call(r->h->context, &c) < 0) fault(r, 3, fn);
    return c;
}

/* 001FBD50(node, id, 0, 300.0): the ricochet; id + 1 when bit 12 of rand()
 * is clear (00122BB8). */
static void marker_ricochet(Run *r, uint32_t node, uint32_t id)
{
    uint32_t v = (uint32_t)call(r, 0x00122BB8u, 0, 0, 0, 0, 0, 0, 0).v0;
    if (r->h->fault) return;
    if (!(((int32_t)v >> 12) & 1)) ++id;
    (void)call(r, 0x001FBD50u, 3, 1, node, id, 0, 0, F_300);
}

int em_aim_fire_marker_0018ABA0(EmAimFireTarget *h, uint32_t node)
{
    if (!h || h->fault) return -1;
    Run run = {h, h->sp - 0x20}, *r = &run;
    uint32_t state = readn(r, node + 4, 1);                          /* 0018ABB0 */
    if (h->fault) return -1;
    switch (state) {
    case 0: {
        put(r, node + 0xA, 0, 1);                                    /* 0018ABE8 */
        uint32_t w = readn(r, node + 0xCC, 4);
        if (h->fault) return -1;
        if (em_ee_c_eq_bits(F_0, w)) {                               /* 0018ABF8 */
            put(r, node + 0xA0, 0, 4);
            uint32_t a = em_ee_add_bits(F_PI, readn(r, D_810374, 4)); /* 0018AC24 */
            a = call(r, 0x001B1470u, 0, 1, 0, 0, 0, 0, a).f0;
            put(r, node + 0xA4, a, 4);                               /* 0018AC28 */
            put(r, node + 0xA8, 0, 4);
            put(r, node + 0xAC, F_1, 4);                             /* 0018AC38 */
        } else {
            (void)call(r, 0x00102948u, 2, 0, node + 0xA0, node + 0xC0, 0, 0, 0);   /* 0018AC40 */
        }
        put(r, node + 0xAC, readn(r, node + 0xD, 1) == 2 ? F_0 : F_1, 4);       /* 0018AC48 */
        put(r, node + 4, readn(r, node + 4, 1) + 1, 1);              /* 0018AC7C */
        put(r, node + 0x36, 1, 2);
        put(r, node + 0x28, 4, 2);
        put(r, node + 0, 1, 1);
        put(r, node + 0x30, D_2754E0, 4);                            /* 0018AC90 */
        break;
    }
    case 1:
        if (readn(r, node + 5, 1) == 1 && readn(r, node + 0xD, 1) != 0) {   /* 0018AC94 */
            if (readn(r, node + 0xA, 1) != 0) {
                (void)call(r, 0x001EFD90u, 3, 0, 0x80000026u, node + 0xB0, node + 0xA0, 0, 0);
            } else {
                uint32_t flags = readn(r, node + 0x2E, 2);           /* 0018ACD4 */
                if (h->fault) return -1;
                if ((flags & 0x300) == 0) {
                    (void)call(r, 0x001EFD20u, 2, 0, 0x80000019u, node + 0xB0, 0, 0, 0);   /* 0018ACEC */
                    uint32_t again = readn(r, node + 0x2E, 2);       /* 0018ACF4 */
                    (void)call(r, 0x001F00A0u, 4, 0, 0x80000060u, node + 0xB0, node + 0xA0, again & 1, 0);
                    if (readn(r, node + 0xD, 1) != 2)                /* 0018AD10 */
                        marker_ricochet(r, node, (readn(r, node + 0x2E, 2) & 0x10) ? 0x18Au : 0x188u);
                } else if (flags & 0x100) {                          /* 0018ADE4 */
                    (void)call(r, 0x001F00A0u, 4, 0, 0x80000003u, node + 0xB0, node + 0xA0, flags & 1, 0);
                    if (readn(r, node + 0xD, 1) != 2) marker_ricochet(r, node, 0x188u);   /* 0018AE04 */
                } else {
                    (void)call(r, 0x001F00A0u, 4, 0, 0x80000060u, node + 0xB0, node + 0xA0, flags & 1, 0);
                    if (readn(r, node + 0xD, 1) != 2) marker_ricochet(r, node, 0x18Au);   /* 0018AE84 */
                }
            }
        }
        if (h->fault) return -1;
        put(r, node + 5, readn(r, node + 5, 1) + 1, 1);              /* 0018AEE4 */
        {
            int32_t t = (int16_t)readn(r, node + 0x28, 2);           /* 0018AEF0 */
            put(r, node + 0x28, (uint32_t)(t - 1), 2);
            if (h->fault) return -1;
            if (t == 0) {
                put(r, node + 0, 2, 1);                              /* 0018AF04 */
                put(r, node + 4, readn(r, node + 4, 1) + 1, 1);
            }
        }
        put(r, node + 0xA, 0, 1);                                    /* 0018AF1C */
        (void)call(r, 0x001B17A0u, 1, 0, node, 0, 0, 0, 0);
        break;
    case 2:
    case 3:
        (void)call(r, 0x001AFC10u, 1, 0, node, 0, 0, 0, 0);          /* 0018AF2C */
        break;
    default:
        break;
    }
    return h->fault ? -1 : 0;
}
