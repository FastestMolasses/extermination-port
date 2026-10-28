/* em_weapon.c - the rifle's ammo and mode bytes (em_weapon.h). Their one
 * storage is the progress block D_00810700 (em_scene_state.h, since save2
 * round 2): the named members d810C61 / C62 / CB4 / D3C. */
#include "game/em_weapon.h"

#include "game/em_scene_bindings.h"

static EmSceneState *w(void) { return em_scene_state(); }

void em_weapon_reset(uint8_t mag, int16_t reserve)
{
    w()->d810C62 = mag;
    w()->d810CB4 = reserve;
    w()->d810C61 = EM_WPN_MODE_SEMI;
    w()->d810D3C = 0;
}

uint8_t em_weapon_mag(void) { return w()->d810C62; }
int16_t em_weapon_reserve(void) { return w()->d810CB4; }
uint8_t em_weapon_fire_mode(void) { return w()->d810C61; }
uint8_t *em_weapon_mag_byte(void) { return &w()->d810C62; }
uint8_t *em_weapon_fire_mode_byte(void) { return &w()->d810C61; }
uint8_t *em_weapon_flashlight_byte(void) { return &w()->d810D3C; }
int16_t *em_weapon_reserve_word(void) { return &w()->d810CB4; }

void em_weapon_set_fire_mode(uint8_t mode)
{
    if (mode <= EM_WPN_MODE_AUTO) w()->d810C61 = mode;
}
