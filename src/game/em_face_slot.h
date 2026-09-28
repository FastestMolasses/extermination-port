/* em_face_slot.h - a record's face slot (+0x90 / +0x94) on the original
 * functions that attach, drive and release it, over the one 001AF710
 * bone-slot stack and arena (docs/FACE_ATTACH.md section 6.3).
 *
 * The player's face while a script holds it (001B81D0's 001CA700(player,
 * D_0028A490[row], 7) and 001D06D0(player, 1); 00183090's 001D0C70 under
 * 0x70003B8F == 2; 001FD950's 001D06E0(player, 0 / 1); 001B82D0 sub 4's
 * 001CA770(player)) runs here on the player record's bytes. Nothing is
 * translated in this module: each call loads the record's +0x90 word and
 * +0x94 halfword into an em_roger_actor_original typed view, runs the
 * translation, and stores them back:
 *   001CA700  em_roger_actor_001CA700 (001AF780's pop from the shared stack,
 *             slot +0x60 = the resource, 001D0690 on slot +0x70)
 *   001D06D0  em_roger_actor_001D06D0 (slot +0x81)
 *   001D06E0  em_roger_actor_001D06E0 (slot +0x80; +0x90..+0xA7 = 0 on 0)
 *   001CA770  em_roger_actor_001CA770 (001AF890's clear and push back,
 *             +0x90 = 0, +0x94 = -1)
 *   001D0C70  the tail call to 001D0720: em_opening_face_tick_slot over
 *             the slot +0x90 names, with the caller's 00122BB8
 * 001CB3C0 then draws the slot's face (em_owner_draw_live, em_face_attach).
 *
 * Fail-stop: a NULL view, a slot outside the arena (e.g. 001D0C70 /
 * 001D06D0 / 001D06E0 with +0x90 == 0, where the original would address
 * low memory) or a translation fault returns -1 (the translation's fault
 * stays latched in the EmRogerActor); nothing is stored back then. */
#ifndef EM_FACE_SLOT_H
#define EM_FACE_SLOT_H

#include <stdint.h>

#include "game/em_opening_face.h"
#include "game/em_roger_actor_original.h"

typedef struct {
    uint8_t *record;          /* the record's bytes (+0x90 word, +0x94 halfword) */
    uint32_t record_address;  /* its original address */
    EmRogerActor *actor;      /* its world: the one 001AF710 stack and slot arena */
    EmFaceRandom random;      /* 00122BB8 (001D0720's draws) */
    void *random_context;
} EmFaceSlot;

/* 001CA700(record, resource, a2): *result = 1 attached, 0 when no slot was
 * free (below 31). 0, or -1. */
int em_face_slot_001CA700(EmFaceSlot *s, uint32_t resource, int32_t a2, int32_t *result);
/* 001D06D0(record, a1) / 001D06E0(record, a1). 0, or -1. */
int em_face_slot_001D06D0(EmFaceSlot *s, uint32_t a1);
int em_face_slot_001D06E0(EmFaceSlot *s, uint32_t a1);
/* 001CA770(record). 0, or -1. */
int em_face_slot_001CA770(EmFaceSlot *s);
/* 001D0C70(record) = 001D0720(record). 0, or -1. */
int em_face_slot_001D0C70(EmFaceSlot *s);

#endif
