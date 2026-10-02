/* em_weapon.c - the rifle's ammo and mode bytes (em_weapon.h). */
#include "game/em_weapon.h"

static struct {
    uint8_t mag;        /* D_00810C62 */
    uint8_t fire_mode;  /* D_00810C61 */
    uint8_t light_on;   /* D_00810D3C */
    int16_t reserve;    /* D_00810CB4 */
} w;

void em_weapon_reset(uint8_t mag, int16_t reserve)
{
    w.mag = mag;
    w.reserve = reserve;
    w.fire_mode = EM_WPN_MODE_SEMI;
    w.light_on = 0;
}

uint8_t em_weapon_mag(void) { return w.mag; }
int16_t em_weapon_reserve(void) { return w.reserve; }
uint8_t em_weapon_fire_mode(void) { return w.fire_mode; }
uint8_t *em_weapon_mag_byte(void) { return &w.mag; }
uint8_t *em_weapon_fire_mode_byte(void) { return &w.fire_mode; }
uint8_t *em_weapon_flashlight_byte(void) { return &w.light_on; }
int16_t *em_weapon_reserve_word(void) { return &w.reserve; }

void em_weapon_set_fire_mode(uint8_t mode)
{
    if (mode <= EM_WPN_MODE_AUTO) w.fire_mode = mode;
}
