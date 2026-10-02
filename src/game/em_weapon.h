/* em_weapon.h - the rifle's ammo and mode bytes: their one storage.
 *
 * The player's aiming and firing run on their originals in AREA11
 * (docs/AIM_FIRE.md): the stance machines 0x1D..0x22, the fire machines,
 * the gun node 00188630 / 0018A6B0, the shots, the lamp, the knife and the
 * aim camera, composed by em_aim_fire_live. This module only holds the
 * global bytes those originals (and the HUD, the pickups and the status
 * pages) read and write:
 *
 *   D_00810C61  the fire mode (0 single, 1 burst, 2 automatic)
 *   D_00810C62  the rounds in the magazine
 *   D_00810CB4  the reserve (the total, halfword)
 *   D_00810D3C  the gun light switch (0017A970's)
 *
 * The port's own stance / fire stand-in that lived here (its firing loop,
 * gun tick, lamp gate, laser and muzzle-flash drawing and the camera
 * stand-in it fed) was retired on 2026-10-02 (chain step AIMLIVE, fix
 * round): AIM_FIRE.md section 10. */
#ifndef EM_WEAPON_H
#define EM_WEAPON_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fire modes, D_00810C61's values. */
enum {
    EM_WPN_MODE_SEMI  = 0,
    EM_WPN_MODE_BURST = 1,
    EM_WPN_MODE_AUTO  = 2
};

/* The scene's ammo state: magazine and reserve; the fire mode and the light
 * switch become 0. */
void em_weapon_reset(uint8_t mag, int16_t reserve);

/* The values, for the HUD. */
uint8_t em_weapon_mag(void);
int16_t em_weapon_reserve(void);
uint8_t em_weapon_fire_mode(void);

/* The storage itself (the originals write it through these): D_00810C62,
 * D_00810C61, D_00810D3C, D_00810CB4. */
uint8_t *em_weapon_mag_byte(void);
uint8_t *em_weapon_fire_mode_byte(void);
uint8_t *em_weapon_flashlight_byte(void);
int16_t *em_weapon_reserve_word(void);

/* A fixture's fire-mode select (the status page's SELECTOR writes the byte
 * through em_weapon_fire_mode_byte). Values above 2 are ignored. */
void em_weapon_set_fire_mode(uint8_t mode);

#ifdef __cplusplus
}
#endif

#endif /* EM_WEAPON_H */
