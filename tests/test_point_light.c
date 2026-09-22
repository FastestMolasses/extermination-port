#include "game/em_point_light.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
    em_point_light_tick(&pool,0x0b00,midpoint,NULL);
    assert(random_calls == 0 && pool.pending_count == 0);
    assert(pool.active[0].matrix[0] == 42 && pool.active[0].angle[3] == .75f);
    assert(pool.active[0].color[3] == 384);
    em_point_light_tick(&pool,0x0b00,midpoint,NULL);
    assert(random_calls == 2 && pool.active[0].matrix[0] == 1);
    em_point_light_tick(&pool,0x0f00,midpoint,NULL);
    assert(random_calls == 2);
    float direction[4] = {0,0,0,0}, accumulated[4] = {0,0,0,0};
    em_point_light_fold(direction,accumulated,&pool,position);
    assert(direction[0] == 0 && direction[1] == 0 && direction[2] == 0);
    assert(isfinite(accumulated[0]) && accumulated[0] > 0);
    for (int i = 0; i < 32; ++i)
        assert(em_point_light_register(&pool,position,color,1,1,0) == i+1);
    assert(em_point_light_register(&pool,position,color,1,1,0) == -1);

    char path[] = "/tmp/em-point-light-XXXXXX";
    int descriptor = mkstemp(path);
    assert(descriptor >= 0);
    FILE *file = fdopen(descriptor,"wb");
    const uint32_t header[4] = {0x504c4d45,1,0x0b00,1}, type = 1;
    assert(fwrite(header,sizeof header,1,file) == 1);
    assert(fwrite(&type,sizeof type,1,file) == 1);
    assert(fwrite(position,sizeof position,1,file) == 1);
    assert(fwrite(color,sizeof color,1,file) == 1);
    assert(fclose(file) == 0);
    uint16_t key = 0;
    assert(em_point_light_load(&pool,&key,path));
    assert(key == 0x0b00 && pool.pending_count == 1 && pool.next_handle == 1);
    assert(memcmp(pool.pending[0].position,position,sizeof position) == 0);
    EmPointLightPool before = pool;
    file = fopen(path,"ab");
    assert(file && fputc(0,file) == 0 && fclose(file) == 0);
    assert(!em_point_light_load(&pool,&key,path));
    assert(memcmp(&pool,&before,sizeof pool) == 0);
    assert(unlink(path) == 0);
    assert(!em_point_light_load(&pool,&key,path));
    puts("point lights: staging, random gates, zero distance, capacity and asset validation PASS");
    return 0;
}
