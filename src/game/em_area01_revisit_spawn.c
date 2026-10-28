/* em_area01_revisit_spawn.c - 001C8140 and 001C7EB0 (see the header). */
#include "game/em_area01_revisit_spawn.h"
#include "game/em_ee_float.h"
#include <string.h>

typedef struct {
    const EmArea01RuntimeHost *h;
    uint32_t sp, fault;
} Spawn;

static int stop(Spawn *s, uint32_t address)
{
    if (!s->fault) s->fault = address ? address : 1u;
    return -1;
}

static uint8_t *at(Spawn *s, uint32_t address, uint32_t size, int write)
{
    if (s->fault) return NULL;
    uint8_t *p = s->h->bytes(s->h->ctx, address, size, write);
    if (!p) stop(s, address);
    return p;
}

static uint32_t lw(Spawn *s, uint32_t a) { uint32_t v = 0; const uint8_t *p = at(s, a, 4, 0); if (p) memcpy(&v, p, 4); return v; }
static uint32_t lhu(Spawn *s, uint32_t a) { uint16_t v = 0; const uint8_t *p = at(s, a, 2, 0); if (p) memcpy(&v, p, 2); return v; }
static uint32_t lbu(Spawn *s, uint32_t a) { const uint8_t *p = at(s, a, 1, 0); return p ? *p : 0; }
static void sw(Spawn *s, uint32_t a, uint32_t v) { uint8_t *p = at(s, a, 4, 1); if (p) memcpy(p, &v, 4); }
static void sh(Spawn *s, uint32_t a, uint32_t v) { uint16_t h = (uint16_t)v; uint8_t *p = at(s, a, 2, 1); if (p) memcpy(p, &h, 2); }
static void sb(Spawn *s, uint32_t a, uint32_t v) { uint8_t *p = at(s, a, 1, 1); if (p) *p = (uint8_t)v; }

static uint64_t reg(uint32_t v) { return (uint64_t)(int64_t)(int32_t)v; }

/* One callee by original address: integer arguments, then at most one float. */
static int call(Spawn *s, uint32_t fn, unsigned na, uint64_t a0, uint64_t a1, int has_f, uint32_t f12, uint64_t *v0)
{
    if (s->fault) return -1;
    EmArea01Call c = {.function = fn, .sp = s->sp, .na = na, .nf = has_f ? 1u : 0u};
    c.a[0] = a0;
    c.a[1] = a1;
    c.f[0] = f12;
    if (s->h->worker(s->h->ctx, &c) < 0) return stop(s, fn);
    if (v0) *v0 = c.v0;
    return 0;
}

static int done(Spawn *s, uint32_t *fault)
{
    if (s->fault) { if (fault) *fault = s->fault; return -1; }
    return 0;
}

int em_area01_revisit_spawn_001C8140(const EmArea01RuntimeHost *h, uint32_t sp, uint32_t bank, int16_t index,
                                     uint32_t behaviour, uint64_t *v0, uint32_t *fault)
{
    if (!h || !h->bytes || !h->worker || !v0 || sp < 0x40u) return -1;
    Spawn s = {h, sp - 0x40u, 0};
    uint64_t node = 0;
    if (call(&s, 0x001AFA90u, 1, 0xC, 0, 0, 0, &node) < 0) return done(&s, fault);
    if (node != 0) {
        const uint32_t n = (uint32_t)node;
        sb(&s, n + 3u, 0xC);
        sb(&s, n + 0xDu, 0x63);
        sh(&s, n + 0x94u, (uint32_t)(uint16_t)index);
        sw(&s, n + 0x40u, bank);
        sw(&s, n + 0x10u, 0x001C7EB0u);
        sw(&s, n + 0x24u, 0);
        sw(&s, n + 0x2E8u, 0x3F000000u);
        sw(&s, n + 0x2ECu, behaviour);
    }
    if (done(&s, fault) < 0) return -1;
    *v0 = node;
    return 0;
}

/* The node's +0x2EC behaviour, read where the original reads it. */
static int behaviour(Spawn *s, uint32_t self)
{
    const uint32_t cb = lw(s, self + 0x1F0u + 0xFCu);
    if (s->fault || !cb) return 0;
    return call(s, cb, 1, reg(self), 0, 0, 0, NULL);
}

int em_area01_revisit_spawn_001C7EB0(const EmArea01RuntimeHost *h, uint32_t sp, uint32_t self, uint32_t *fault)
{
    if (!h || !h->bytes || !h->worker || sp < 0x50u) return -1;
    Spawn s = {h, sp - 0x50u, 0};
    const uint32_t tail = self + 0x1F0u;
    const uint32_t rec = lw(&s, self + 0x24u);
    uint32_t i = 0; /* the bone index in state 0, anim_advance_time's result in state 1 */
    switch (lbu(&s, self + 4u)) {
    case 0: {
        sb(&s, self + 0xCu, 1);
        if ((int32_t)(int16_t)lhu(&s, 0x00275BCCu) < (int32_t)lbu(&s, self + 0xCu)) {
            sb(&s, self + 4u, 3);
            break;
        }
        uint32_t cur = self;
        while (!s.fault && i < lbu(&s, self + 0xCu)) {
            uint64_t slot = 0;
            if (call(&s, 0x001AF780u, 0, 0, 0, 0, 0, &slot) < 0) break;
            sw(&s, cur + 0x110u, (uint32_t)slot);
            cur += 4u;
            i++;
        }
        sb(&s, self + 9u, lbu(&s, self + 0xCu));
        call(&s, 0x001CB5B0u, 1, lbu(&s, self + 0xCu), 0, 0, 0, NULL);  /* anim_bone_array_setup */
        behaviour(&s, self);
        call(&s, 0x001C63E0u, 2, reg(self), reg((uint32_t)(int32_t)(int16_t)lhu(&s, self + 0x94u)), 0, 0,
             NULL);                                                        /* bone_init_default_2 */
        sw(&s, tail + 0xF4u, 1);
        sb(&s, self + 4u, 1);
        break;
    }
    case 1: {
        if (rec != 0 && (lhu(&s, rec + 0x2Eu) & (1u << (lhu(&s, self + 0x2Eu) & 31u)))) {
            sb(&s, self + 4u, 3);
            break;
        }
        sw(&s, tail + 0xF4u, lw(&s, tail + 0xF4u) - 1u);
        if ((int32_t)lw(&s, tail + 0xF4u) < 0) {
            uint64_t r = 0;
            if (call(&s, 0x001C64F0u, 1, reg(self), 0, 1, lw(&s, tail + 0xF8u), &r) == 0) /* anim_advance_time */
                i = (uint32_t)r;
        }
        static const uint32_t rest[8] = {0, 0, 0, 0x3F800000u, 0, 0, 0, 0x3F800000u};
        for (unsigned k = 0; k < 8; ++k) sw(&s, self + 0xB0u + 4u * k, rest[k]);
        call(&s, 0x001C68C0u, 1, reg(self), 0, 0, 0, NULL);
        for (unsigned k = 0; k < 3; ++k)
            sw(&s, self + 0xB0u + 4u * k, lw(&s, lw(&s, lw(&s, 0x00275B40u)) + 0xC0u + 4u * k));
        sw(&s, self + 0xBCu, 0x3F800000u);
        {
            const uint32_t q = lw(&s, lw(&s, 0x00275B40u)) + 0x90u;
            uint8_t rows[64];
            const uint8_t *p = at(&s, q, 64, 0);
            if (p) {
                memcpy(rows, p, 64);
                uint8_t *d = at(&s, self + 0xD0u, 64, 1);
                if (d) memcpy(d, rows, 64);
            }
        }
        const uint32_t flags = i & 0x3000u;
        sw(&s, self + 0x10Cu, 0x3F800000u);
        if (flags) {
            sb(&s, self + 4u, 3);
            behaviour(&s, self);
        } else {
            const uint32_t src = lw(&s, lw(&s, 0x00275B40u));
            if (em_ee_c_lt_bits(lw(&s, src + 0x18u), 0x3F666666u) &&
                em_ee_c_lt_bits(lw(&s, src + 0x1Cu), 0x3F666666u) &&
                em_ee_c_lt_bits(lw(&s, src + 0x20u), 0x3F666666u))
                break;
            behaviour(&s, self);
        }
        break;
    }
    case 2: case 3:
        call(&s, 0x001AFC10u, 1, reg(self), 0, 0, 0, NULL);
        break;
    default:
        break;
    }
    return done(&s, fault);
}
