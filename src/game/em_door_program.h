/* 001BBE40's patch of the ELF's ordinary-door program (0x24DBC0..0x24DF80,
 * entry 0x24DE40; the locked program 0x24DEC0). The program itself runs on
 * the AREA11 script host (em_area11_script_host: 001BA1A0 / 001BA1F0 with the
 * ftab_0024D880 handlers of em_area_script; census L18). */
#ifndef EM_DOOR_PROGRAM_H
#define EM_DOOR_PROGRAM_H

#include "game/em_door_transit.h"
#include "game/em_script.h"

/* The ordinary door's stores: 0x24DC14 = the player clip, 0x24DC54 = the
 * door clip, 0x24DC58 = the sound word 001BBD60 writes (plan->sound) and
 * 0x24DC8C = the wait's float bits. Every other byte of the image survives.
 * 1, or 0 for an image other than 0x24DBC0..0x24DF80, a locked plan or a
 * plan whose words are not the ordinary program's. */
int em_door_program_patch(EmScriptImage *image, const EmDoorTransitPlan *plan);

#endif
