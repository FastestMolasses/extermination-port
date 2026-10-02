/* The AREA11 security gun 0x825940 as one owner, over original record
 * memory (docs/SECURITY_GUN.md; oracle
 * tools/test_security_gun_rest_reference.py).
 *
 * What this module adds to em_security_gun (which stays the one
 * translation of lifecycles 0, 0x64, 2, 3 and "other", of the cable
 * 0x827490 and of the manager 0x823CE0):
 *   0x825940 lifecycle 4 (runtime 0x825B74..0x826190): the idle patrol
 *            (head sway, gun sway, sight line, target pick-up);
 *   0x825940 lifecycle 1 (runtime 0x826190..0x826D60): the aim and fire
 *            (turn toward the target, pitch, shot, hit effects);
 *   0x826F30 the sight probe (two collision probes from the gun bone, the
 *            sight sprite and beam); its return value picks the aim result;
 *   0x827400 the shot node (a class-0xC pool node at the gun muzzle);
 *   001B1190 the persistent taken-bit set the cable calls (byte-matched
 *            decomp C; before this lane it had no verified translation);
 *   0021AAC0 the behaviour of the cable's hit effect node (NEARMISS decomp
 *            C; translated from the listing);
 *   0021A500 the behaviour of the strip nodes 0021AAC0 spawns (NEARMISS;
 *            translated from the listing);
 *   001EFEB0 the spawn 0021AAC0 calls (byte-matched decomp C).
 * Addresses are original runtime addresses: splat names each overlay
 * function 0x40 lower (its vram is the MWo3 header address), so 0x826F30 is
 * listed as ..._00826EF0 plus its fall-through ..._00826F30, and 0x827400
 * as ..._008273C0 plus ..._00827400. The jal instructions of 0x825940
 * encode the runtime addresses 0x826F30 and 0x827400. The decomp now holds
 * byte-identical C for 0x825940, 0x826F30 and 0x827400
 * (src/overlays/AREA11/func_overlay_AREA11_00825900.c, 00826EF0.c,
 * 008273C0.c), which the translations agree with.
 *
 * em_gun_rest_tick runs one call of 0x825940. Lifecycles 1 and 4
 * are translated here; every other lifecycle delegates to
 * em_gun_tick (em_security_gun.c) through an adapter that
 * builds its record/child views from this module's memory and writes them
 * back before every worker call, so the whole function has one entry.
 *
 * Memory. Every byte the function reads or writes is reached through
 * EmGunRestMem by its original address: the gun record, its child (the
 * 0x7A node at +0x220), the bone records the D_00275B40 slots and the +0x11C
 * words name, the target record at +0x204, the hit record the probe leaves
 * in 0x700031D4, the scratchpad ranges 0x70003190..0x700031DB,
 * 0x70003600..0x7000361F, 0x70003680..0x70003687, 0x700038A0..0x700038DF,
 * 0x70003910..0x7000391F, 0x70003A20..0x70003A23 and 0x70003B68..0x70003B6B,
 * the player position D_00810360, the event flag D_00810788
 * (D_00810758[0x30]), D_00275B40 and the overlay data quads 0x82A730 /
 * 0x82A740 (docs/SECURITY_GUN.md 5.1 lists the record offsets). A NULL answer
 * faults at that address.
 *
 * Freed nodes. A call that frees the node (001AFC10) returns 0; the caller
 * must not tick that node again. Unlike em_gun_tick, this entry
 * keeps no use-after-free latch (the original has none either). The two stack locals of 0x826F30 (the gun-tip point and the
 * 0x82A730 quad) and the one of 0x827400 are host locals here.
 *
 * Workers. Every original callee outside the three overlay functions is a
 * worker named by its address. Actor arguments are original record
 * addresses; vector and matrix arguments are host pointers to the 16- or
 * 64-byte original-layout bytes (resolved through EmGunRestMem, or the
 * helper's host locals); float arguments and results are bit patterns.
 * A NULL worker or a negative worker result faults (fail-stop) with
 * EM_GUN_FAULT_NULL / EM_GUN_FAULT_WORKER_FAILED at the callee's address;
 * a latched fault makes every later call return -1. Float arithmetic is
 * the EE model of em_ee_float.h on bit patterns. */
#ifndef EM_SECURITY_GUN_REST_H
#define EM_SECURITY_GUN_REST_H

#include <stdint.h>

#include "game/em_security_gun.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_GUN_REST_SIGHT 0x00826F30u   /* the sight probe */
#define EM_GUN_REST_SHOT 0x00827400u    /* the shot node */
#define EM_GUN_REST_SHOT_HANDLER 0x001F5040u /* shot node +0x10 */
#define EM_GUN_REST_SIGHT_QUAD 0x0082A730u   /* overlay data quad (0x826F30) */
#define EM_GUN_REST_SHOT_QUAD 0x0082A740u    /* overlay data quad (0x827400) */

typedef struct EmGunRestMem {
    void *ctx;
    /* The `size` bytes at `address` for reading / for writing (the same
     * storage). NULL when the address is not mapped (the module faults). */
    const uint8_t *(*load)(void *ctx, uint32_t address, uint32_t size);
    uint8_t *(*store)(void *ctx, uint32_t address, uint32_t size);
} EmGunRestMem;

typedef struct EmGunRestWorkers {
    void *ctx;
    /* Owner services (actor = record address). */
    int (*w_001B0FD0)(void *ctx, uint32_t actor, int32_t *result); /* as em_gun_tick: minus +0x04 */
    int (*w_001C6380)(void *ctx, uint32_t actor);
    int (*w_001A2370)(void *ctx, uint32_t actor, const uint8_t *matrix);
    int (*w_001B17A0)(void *ctx, uint32_t actor);
    int (*w_draw_4C)(void *ctx, uint32_t actor, uint32_t callback); /* jalr *(actor+0x4C) */
    int (*w_001AFC10)(void *ctx, uint32_t actor);
    int (*w_001AFA90)(void *ctx, uint8_t cls, uint32_t *node);      /* node address or 0 */
    int (*w_001FBD50)(void *ctx, uint32_t actor, int32_t cue, int32_t a2, uint32_t range);
    int (*w_00122BB8)(void *ctx, int32_t *value);                   /* rand */
    /* SDK float leaves: f0 = f(f12). */
    int (*w_0011E2A8)(void *ctx, uint32_t x, uint32_t *result);     /* sinf */
    int (*w_0011DF78)(void *ctx, uint32_t x, uint32_t *result);     /* fabsf */
    int (*w_0011DBB8)(void *ctx, uint32_t x, uint32_t *result);     /* atanf */
    int (*w_0011E748)(void *ctx, uint32_t x, uint32_t *result);     /* sqrtf */
    int (*w_0011E520)(void *ctx, uint32_t x, uint32_t *result);     /* asinf */
    int (*w_001B1470)(void *ctx, uint32_t x, uint32_t *result);     /* angle wrap */
    /* SDK vector leaves over 16-byte quads / 64-byte matrices. */
    int (*w_00102948)(void *ctx, uint8_t *dst, const uint8_t *src);            /* quad copy */
    int (*w_00102958)(void *ctx, uint8_t *dst, const uint8_t *src);            /* matrix copy */
    int (*w_001026A0)(void *ctx, uint8_t *dst, const uint8_t *m, const uint8_t *v); /* m * v */
    int (*w_001028B8)(void *ctx, uint8_t *dst, const uint8_t *a, const uint8_t *b); /* a + b */
    int (*w_001028D0)(void *ctx, uint8_t *dst, const uint8_t *a, const uint8_t *b); /* a - b */
    int (*w_00102738)(void *ctx, const uint8_t *a, const uint8_t *b, uint32_t *f0); /* dot */
    int (*w_00102760)(void *ctx, uint8_t *dst, const uint8_t *src);            /* normalize */
    int (*w_001031E0)(void *ctx, uint8_t *dst, const uint8_t *src);
    /* Collision probes; they leave their result in scratchpad
     * 0x70003190..0x700031DB, which the module reads through EmGunRestMem. */
    int (*w_0019AA80)(void *ctx, const uint8_t *from, const uint8_t *to, int32_t a2,
                      int32_t *result);
    int (*w_0019A570)(void *ctx, const uint8_t *from, const uint8_t *to, int32_t a2, int32_t a3,
                      int32_t *result);
    int (*w_0019B6C0)(void *ctx, const uint8_t *a0, const uint8_t *a1, int32_t *result);
    /* Effects and the sight draw. */
    int (*w_001EFD90)(void *ctx, uint32_t id, const uint8_t *point, const uint8_t *at,
                      uint32_t *node);
    int (*w_001CD520)(void *ctx, int32_t a0, int32_t a1, const uint8_t *point, uint64_t a3,
                      uint32_t t0, uint32_t f12, uint32_t f13, uint32_t f14);
    int (*w_001E2BA0)(void *ctx, const uint8_t *a0, const uint8_t *a1, const uint8_t *a2,
                      uint32_t f12);
    /* 0021AAC0 (the cable's hit effect node) only. */
    int (*w_001029C0)(void *ctx, uint8_t *m);                                  /* identity */
    int (*w_00102B08)(void *ctx, uint8_t *dst, const uint8_t *src, uint32_t angle);
    int (*w_00102918)(void *ctx, uint8_t *dst, const uint8_t *src, const uint8_t *v);
    int (*w_001CCF70)(void *ctx, const uint8_t *p, int32_t *key);             /* depth key */
    int (*w_001CFA60)(void *ctx, uint8_t *block, const uint8_t *m, uint32_t f12, uint32_t f13);
    int (*w_001CFBE0)(void *ctx, int32_t key, int32_t kind, uint32_t table, const uint8_t *block,
                      int32_t t0);
    int (*w_001EFEB0)(void *ctx, uint32_t id, const uint8_t *m, uint32_t *node);
    /* 0021A500 (the 0x8000003B strip node 0021AAC0 spawns) only. */
    int (*w_001CE860)(void *ctx, int32_t a0, int32_t a1, const uint8_t *points,
                      const uint8_t *colour, uint32_t f12, int32_t t0, uint64_t t1);
    int (*w_00103230)(void *ctx, uint8_t *dst, const uint8_t *src, uint32_t scale);
    int (*w_001CFB50)(void *ctx, uint8_t *block, int32_t a1, const uint8_t *m, uint32_t f12,
                      uint32_t f13, uint32_t f14, uint32_t f15, uint32_t f16);
    int (*w_001281C0)(void *ctx, uint32_t x, int32_t *value);                 /* float_to_int */
    /* 001EFEB0 (translated here) only: the effect allocator. */
    int (*w_001EF9D0)(void *ctx, uint32_t id, const uint8_t *pos, uint32_t f12, uint32_t *node);
} EmGunRestWorkers;

/* One call of 0x825940 on the record at `actor`. Returns 1 while allocated,
 * 0 after the 001AFC10 free, -1 on a fault (latched in `fault`). */
int em_gun_rest_tick(uint32_t actor, const EmGunRestMem *mem,
                              const EmGunRestWorkers *w, EmGunFault *fault);

/* 0021AAC0: the behaviour (+0x10) of the effect node the cable's hit
 * spawns (001EFE00(0x80000045, cable) -> 001EF9D0: record 0x45 of the
 * global effect table names this callback). One call on the node at `node`;
 * its +0x24 is the cable. Lifecycle +0x04: 0 seeds six random phases and
 * places the node at the cable's +0xD0 matrix, then runs 1; 1 counts
 * +0x288 (e +0x98) up: from 0x1F, every 6th tick adds one of up to six
 * rising points (8.0 apart) and every point draws two 001CFBE0 packets;
 * below 0x3C, every 10th tick spawns 001EFEB0(0x8000003B) with +5 = 0,
 * +0x1F0 = 0xC, +0x1F4 = 48.0, +0x1F8 = 0.5; at 0x3C it sets the cable's
 * +0x04 = 3 (the cable frees itself on its next tick); at >= 0x79 its own
 * +0x04 = 3; 2 and 3 free the node (001AFC10); other values return.
 * Returns 1 while allocated, 0 after the free, -1 on a fault. */
int em_gun_rest_0021AAC0(uint32_t node, const EmGunRestMem *mem, const EmGunRestWorkers *w,
                         EmGunFault *fault);

/* 0021A500: the behaviour of the 0x8000003B nodes 0021AAC0 spawns (record
 * 0x3B of the global effect table names this callback). Word +0x1F0 is the
 * point count n (0021AAC0 sets 12), +0x1F4 the length, +0x1F8 the width.
 * Lifecycle 0 seeds n - 2 random lateral offsets (2.5 * sinf(2 pi rand)),
 * a colour seed and +0x1F4 /= n - 1, then runs 1; 1 builds n points into
 * D_00821400 through the node's +0xD0 matrix, draws them (001CE860) with a
 * grey fading as +0x200 grows toward 1.0 by (1 - x) / 8 + 0.0001 per tick;
 * with +0x05 == 0 it ends (+0x04 = 3) once +0x200 passes 1.0; with +0x05
 * == 1 it also draws two end sprites per tick until +0x204 passes 1.1.
 * 2 and 3 free. Returns 1 while allocated, 0 after the free, -1. */
int em_gun_rest_0021A500(uint32_t node, const EmGunRestMem *mem, const EmGunRestWorkers *w,
                         EmGunFault *fault);

/* 001EFEB0(id, m): node = 001EF9D0(id, m + 0x30, 1.0); when non-zero,
 * 00102958(node + 0xD0, m). `m` is the 64-byte matrix (host pointer);
 * *node receives the node address. Returns 0 or -1. */
int em_gun_rest_001EFEB0(uint32_t id, const uint8_t *m, const EmGunRestMem *mem,
                         const EmGunRestWorkers *w, uint32_t *node, EmGunFault *fault);

/* 001B1190(a0): with (a0 & 0xFF) != 0, the word at D_00810860 +
 * (D_00810700 << 5) + ((a0 & 0xFF) >> 5) * 4 gets bit (a0 & 0x1F) set.
 * Only `load` (D_00810700, the word) and `store` (the word) are used.
 * Returns 0, or -1 with a fault at the unmapped address. */
int em_gun_rest_001B1190(int32_t a0, const EmGunRestMem *mem, EmGunFault *fault);

#ifdef __cplusplus
}
#endif

#endif /* EM_SECURITY_GUN_REST_H */
