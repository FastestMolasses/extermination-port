#include "game/em_point_light.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t random_calls;
static uint32_t midpoint(void *unused)
{
    (void)unused;
    ++random_calls;
    return 0x40000000;
}

int main(void)
{
    EmPointLightPool pool = {0};
    const float position[4] = {4,5,6,1}, color[4] = {2,1,.5f,3};
    pool.active[0].matrix[0] = 42;
    pool.active[0].angle[3] = .75f;
    em_point_light_reset(&pool);
    assert(em_point_light_register(&pool,position,color,1,1,0) == 0);
    assert(em_point_light_tick(&pool,0x0b00,midpoint,NULL) == 0);
    assert(random_calls == 0 && pool.pending_count == 0);
    assert(pool.active[0].matrix[0] == 42 && pool.active[0].angle[3] == .75f);
    assert(pool.active[0].color[3] == 384);
    assert(em_point_light_tick(&pool,0x0b00,midpoint,NULL) == 0);
    assert(random_calls == 2 && pool.active[0].matrix[0] == 1);
    assert(em_point_light_tick(&pool,0x0f00,midpoint,NULL) == 0);
    assert(random_calls == 2);
    float direction[4] = {0,0,0,0}, accumulated[4] = {0,0,0,0};
    em_point_light_fold(direction,accumulated,&pool,position);
    assert(direction[0] == 0 && direction[1] == 0 && direction[2] == 0);
    assert(isfinite(accumulated[0]) && accumulated[0] > 0);
    for (int i = 0; i < 32; ++i)
        assert(em_point_light_register(&pool,position,color,1,1,0) == i+1);
    assert(em_point_light_register(&pool,position,color,1,1,0) == -1);

    /* 001D7BB0's field writes after a full pool: the counters restart, so
     * the area entry's next registration (001F6640 -> 001D7FA0, the room
     * lists; em_effects_live_room_lights) takes handle 0 again. */
    em_point_light_reset(&pool);
    assert(pool.next_handle == 0 && pool.pending_count == 0);
    assert(em_point_light_register(&pool,position,color,1,1,0) == 0);
    assert(pool.pending_count == 1 && pool.next_handle == 1);
    assert(memcmp(pool.pending[0].position,position,sizeof position) == 0);
    puts("point lights: staging, random gates, zero distance, capacity and reset PASS");
    return 0;
}
