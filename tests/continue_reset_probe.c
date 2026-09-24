/* Native side of tools/test_continue_reset_reference.py: applies the
 * port's func_001AF2C0 mirror (game_state_new_game, em_game_internal.h)
 * to a dirtied state and exposes the result as engine-shaped values.
 * Header-only: no gameplay module is linked. */
#include "game/em_game_internal.h"
#include "game/em_scene_state.h"
#include "game/em_director_original.h"

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
    uint32_t director_step;   /* D_00810813                     */
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
    /* D_0081084C (WP-4), D_00810CC3 (WP-6) and D_00810813 (HK) are canonical
     * D2 progress bytes: 001AF2C0's memset reaches them through
     * em_scene_progress_reset_001AF2C0 (called by em_pickup_reset in the
     * game). */
    static EmSceneState scene;
    uint8_t *power = em_scene_progress_at(&scene, 0x0081084Cu, 1);
    uint8_t *key0 = em_scene_progress_at(&scene, 0x00810CC3u, 1);
    uint8_t *step = em_scene_progress_at(&scene, 0x00810813u, 1);
    *power = 0x80;
    *key0 = 1;
    *step = 0x20;

    game_state_new_game(&g);
    em_scene_progress_reset_001AF2C0(&scene);

    memcpy(&out->health_bits, &g.status.health, 4);
    memcpy(&out->infection_bits, &g.status.infection, 4);
    out->mag = g.status.mag;
    out->reserve = (uint16_t)g.status.reserve;
    out->battery = g.status.battery;
    out->battery_max = g.status.battery_max;
    out->opening_complete = g.opening_complete;
    out->event_39 = g.opening_event_39;
    out->key_item_zero = *key0;
    out->director_step = *step;
    out->terminal_powered = (uint32_t)(*power >> 7);
}

/* em_director_original_001C4760_scene (the live 001C4760 binding) over a
 * scene whose D_00810CC3[a0], D_008106B0 and D_008106B1 are seeded:
 * out = {returned 0, D_00810CC3[a0], B0, B1}. */
void key_add_probe(int32_t a0, int32_t a1, uint8_t key, uint8_t b0, uint8_t b1, uint8_t out[4])
{
    static EmSceneState scene;
    memset(&scene, 0, sizeof scene);
    uint8_t *byte = em_scene_progress_at(&scene, 0x00810CC3u + (uint32_t)a0, 1);
    if (byte) *byte = key;
    scene.req[EM_SCENE_REQ_B0] = b0;
    scene.req[EM_SCENE_REQ_B1] = b1;
    out[0] = (uint8_t)(em_director_original_001C4760_scene(&scene, a0, a1) == 0);
    out[1] = byte ? *byte : 0;
    out[2] = scene.req[EM_SCENE_REQ_B0];
    out[3] = scene.req[EM_SCENE_REQ_B1];
}
