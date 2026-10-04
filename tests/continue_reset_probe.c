/* Native side of tools/test_continue_reset_reference.py: applies the
 * port's func_001AF2C0 mirror (game_state_new_game, em_game_internal.h)
 * to a dirtied state and exposes the result as engine-shaped values.
 * Header-only: no gameplay module is linked. */
#include "game/em_game_internal.h"
#include "game/em_scene_state.h"
#include "game/em_director_original.h"
#include <assert.h>

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
    uint32_t mechanism_done;  /* D_00810766                     */
    uint32_t area01_locks;    /* D_00810842                     */
    uint32_t mechanism_gate;  /* D_00810845                     */
    uint32_t npc_done;        /* D_00810759                     */
    uint32_t npc_gate;        /* D_0081075A                     */
    uint32_t light_gate;      /* D_0081075D                     */
    uint32_t bridge_gate;     /* D_00810760                     */
    uint32_t area01_story;    /* D_008107D9                     */
    uint32_t bridge_counter;  /* D_008107E0                     */
} ContinueResetProbe;

void continue_reset_probe(ContinueResetProbe *out)
{
    /* The state a death in AREA11 leaves behind (after the opening). */
    memset(&g, 0, sizeof g);
    g.status = (EmPlayerStatus){ .health = 0.0f, .health_max = 100.0f,
        .infection = 60.0f, .mag = 4, .mag_max = 30, .reserve = 120,
        .battery = 4, .battery_max = 6 };
    g.opening_complete = 0xFF;
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
    uint8_t *event39 = em_scene_progress_at(&scene, 0x00810791u, 1); /* D2 since census L22 */
    *event39 = 0xFF;
    uint8_t *done = em_scene_progress_at(&scene, 0x00810766u, 1);
    uint8_t *locks = em_scene_progress_at(&scene, 0x00810842u, 1);
    uint8_t *gate = em_scene_progress_at(&scene, 0x00810845u, 1);
    assert(done && locks && gate);
    *done = 0xFF; *locks = 0xA5; *gate = 0x28;
    static const uint32_t area01_addresses[] = {
        0x00810759u, 0x0081075Au, 0x0081075Du, 0x00810760u, 0x008107D9u, 0x008107E0u
    };
    static const uint8_t area01_dirty[] = {0xFF, 1, 0xFF, 0xFF, 0x81, 0xE0};
    uint8_t *area01[6];
    for (unsigned i = 0; i < 6; ++i) {
        area01[i] = em_scene_progress_at(&scene, area01_addresses[i], 1);
        assert(area01[i]);
        *area01[i] = area01_dirty[i];
    }
    /* Adjacent migrated bytes share one span; reserved gaps still refuse. */
    assert(em_scene_progress_at(&scene, 0x00810758u, 3));
    assert(em_scene_progress_at(&scene, 0x0081075Du, 4));
    assert(em_scene_progress_at(&scene, 0x008107D8u, 2));
    assert(!em_scene_progress_at(&scene, 0x00810757u, 2));
    assert(!em_scene_progress_at(&scene, 0x0081075Au, 2));
    assert(!em_scene_progress_at(&scene, 0x0081075Cu, 2));
    assert(!em_scene_progress_at(&scene, 0x00810760u, 2));
    assert(!em_scene_progress_at(&scene, 0x008107D7u, 2));
    assert(!em_scene_progress_at(&scene, 0x008107D9u, 2));
    assert(!em_scene_progress_at(&scene, 0x008107DFu, 2));
    assert(!em_scene_progress_at(&scene, 0x008107E0u, 2));
    assert(!em_scene_progress_at(&scene, 0x00810758u, 6));
    /* Neighboring reserved bytes cannot be read through a wider request. */
    assert(!em_scene_progress_at(&scene, 0x00810765u, 2));
    assert(!em_scene_progress_at(&scene, 0x00810766u, 2));
    assert(!em_scene_progress_at(&scene, 0x00810841u, 2));
    assert(!em_scene_progress_at(&scene, 0x00810842u, 2));
    assert(!em_scene_progress_at(&scene, 0x00810844u, 2));
    assert(!em_scene_progress_at(&scene, 0x00810845u, 2));

    game_state_new_game(&g);
    em_scene_progress_reset_001AF2C0(&scene);

    memcpy(&out->health_bits, &g.status.health, 4);
    memcpy(&out->infection_bits, &g.status.infection, 4);
    out->mag = g.status.mag;
    out->reserve = (uint16_t)g.status.reserve;
    out->battery = g.status.battery;
    out->battery_max = g.status.battery_max;
    out->opening_complete = g.opening_complete;
    out->event_39 = *event39;
    out->key_item_zero = *key0;
    out->director_step = *step;
    out->terminal_powered = (uint32_t)(*power >> 7);
    out->mechanism_done = *done;
    out->area01_locks = *locks;
    out->mechanism_gate = *gate;
    out->npc_done = *area01[0];
    out->npc_gate = *area01[1];
    out->light_gate = *area01[2];
    out->bridge_gate = *area01[3];
    out->area01_story = *area01[4];
    out->bridge_counter = *area01[5];
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
