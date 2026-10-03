/* em_indicator_bind_live.h - the indicator children's model and bone-slot
 * bind (001C2360 / 001C22A0) and their placement (001C6380), bound live
 * (docs/CENSUS_UNVERIFIED.md "001C5680 and 001C5760").
 *
 * This module adds no behaviour of its own. It keeps each child node's
 * bytes the translations read or write beside its pool record (+0x44 the
 * model handle, +0x4C the draw method, the +0x110 slot words and the
 * slots' typed views, +0xD0 the node matrix) and binds:
 *   001C2360 / 001C22A0  em_rvr_001C2360 / em_rvr_001C22A0
 *                        (em_render_verify_rest), over D_0028A56C (the Roger
 *                        export's table and models 0x73..0x75, 0x7A:
 *                        em_area11_roger_001C6120) or *D_0028A59C (the world
 *                        model bank: em_area11_boxes_world_001C6120, the
 *                        terminal's model 0x10), with its workers:
 *     001CA5E0(self, model, 2)  +0x44 = model, then 001CA5F0 kind 2:
 *                               +0x4C = 001CACB0 (decomp 001CA5E0 /
 *                               001CA5F0; no other kind is reached)
 *     001C6150(model)           the model's +0x08 bone count
 *     001AF780                  the one bone-slot stack (em_roger_actor on
 *                               em_area11_boxes_slot_world)
 *     001CB5B0                  nothing to do: D_00275B40 is the node's
 *                               own +0x110 slots (the walk's 001CB590)
 *     001C62C0                  em_owner_services_001C62C0
 *   001C6380              em_owner_services_001C6380 over the child's
 *                         +0xB0 / +0xC0 / +0x60 and its slots
 *   001AF800              em_roger_actor_001AF800 over the child's +0x09,
 *                         +0x0C and held words (the pool's free of a child
 *                         with +0x09 != 0; its own loop, not 001AF890)
 *
 * and the child's +0x4C draw 001CACB0 -> 001CABA0(child, +0x44)
 * (em_owner_draw_live_001CABA0: channel 3, lighting mode 1, the class-2
 * unit the page D_007635C0 CALLs at its depth; OWNER_DRAW.md section 11),
 * over the child's record bytes, its slots and the model's bank (the
 * library models join this module's table-less bank at their addresses).
 *
 * Fail-stop: the first fault is latched (em_indicator_bind_live_fault());
 * every later entry returns -1. */
#ifndef EM_INDICATOR_BIND_LIVE_H
#define EM_INDICATOR_BIND_LIVE_H

#include <stdint.h>

#include "game/em_actor_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

/* At each area build, after the pool reset and the boxes' 001AF710 (the
 * Roger export must be loaded: D_0028A56C). 0, or -1. */
int em_indicator_bind_live_attach(EmActorPool *pool);
uint32_t em_indicator_bind_live_fault(void);

/* 001C2360 (fn 0x001C2360) or 001C22A0 (fn 0x001C22A0) over the child:
 * *result = the original's return (0 bound, 1 the bone cap refused). Writes
 * the child's +0x09 / +0x0C (EmActor bones / u0A[2]). 0, or -1. */
int em_indicator_bind_live_bind(EmActor *child, uint32_t fn, int32_t *result);
/* 001C6380 over the child (its model, slots, +0xB0, +0xC0, +0x60). */
int em_indicator_bind_live_place(EmActor *child);
/* 00102958 copy_qw4 of `matrix` into slot k's +0x90 of a bound child (the
 * terminal's 0x827E6C copy of its node matrix into its child). 0, or -1
 * (not a bound child, or no slot k). */
int em_indicator_bind_live_set_node(EmActor *child, unsigned k, const float matrix[16]);
/* Slot k's +0x90 of a bound child (the stand-in draws of the children's
 * +0x4C read their own node matrices). 0, or -1. */
int em_indicator_bind_live_node(const EmActor *child, unsigned k, float matrix[16]);
/* The child's +0x4C, 001CACB0 (001F54E0's indirect call, the child's +0x80
 * colour words just stored): 001CABA0(child, +0x44). 0, or -1 (latched). */
int em_indicator_bind_live_draw(EmActor *child);
/* 001AF800 for an indicator child: 1 handled, 0 not a bound child, -1. */
int em_indicator_bind_live_001AF800(EmActor *child);

/* The tick log's view of one bound child. */
typedef struct {
    uint32_t address;     /* the pool record */
    uint8_t bones, count; /* +0x09, +0x0C */
    uint32_t model;       /* +0x44 */
    uint32_t method;      /* +0x4C */
    uint32_t slot[4];     /* +0x110.. (0 past +0x09) */
    float world[16];      /* the first slot's +0x90 (the placed node) */
} EmIndicatorBindRecord;
int em_indicator_bind_live_record(const EmActor *child, EmIndicatorBindRecord *out);

#ifdef __cplusplus
}
#endif

#endif /* EM_INDICATOR_BIND_LIVE_H */
