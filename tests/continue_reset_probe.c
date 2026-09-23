/* Native side of tools/test_continue_reset_reference.py: applies the
 * port's func_001AF2C0 mirror (game_state_new_game, em_game_internal.h)
 * to a dirtied state and exposes the result as engine-shaped values.
 * Header-only: no gameplay module is linked. */
#include "game/em_game_internal.h"

EmGameState g;

typedef struct {
    uint32_t health_bits;     /* D_00810858 (float)            */
    uint32_t infection_bits;  /* D_0081085C                     */
    uint32_t mag;             /* D_00810C62                     */
    uint32_t reserve;         /* D_00810CB4                     */
    uint32_t battery;         /* D_00810CB2 >> 1 (display units) */
    uint32_t battery_max;     /* D_00810CB7 >> 1 (display units) */
    uint32_t opening_complete;/* D_00810811                     */
    uint32_t event_39;        /* D_00810791                     */
    uint32_t key_item_zero;   /* D_00810CC3                     */
    uint32_t cine_step;       /* D_00810813                     */
    uint32_t terminal_powered;/* D_0081084C bit 7               */
} ContinueResetProbe;

void continue_reset_probe(ContinueResetProbe *out)
{
    /* The state a death in AREA11 leaves behind (after the opening). */
    memset(&g, 0, sizeof g);
    g.status = (EmPlayerStatus){ .health = 0.0f, .health_max = 100.0f,
        .infection = 60.0f, .mag = 4, .mag_max = 30, .reserve = 120,
        .battery = 4, .battery_max = 6 };
    g.opening_complete = 0xFF;
    g.opening_event_39 = 0xFF;
    g.opening_key_item_zero = 1;
    g.cine_step = 0x20;
    g.terminal_powered = 1;

    game_state_new_game(&g);

    memcpy(&out->health_bits, &g.status.health, 4);
    memcpy(&out->infection_bits, &g.status.infection, 4);
    out->mag = g.status.mag;
    out->reserve = (uint16_t)g.status.reserve;
    out->battery = g.status.battery;
    out->battery_max = g.status.battery_max;
    out->opening_complete = g.opening_complete;
    out->event_39 = g.opening_event_39;
    out->key_item_zero = g.opening_key_item_zero;
    out->cine_step = g.cine_step;
    out->terminal_powered = (uint32_t)g.terminal_powered;
}
