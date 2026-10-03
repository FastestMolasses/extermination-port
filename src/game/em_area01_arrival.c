/* em_area01_arrival.c - see em_area01_arrival.h. */
#include "game/em_area01_arrival.h"

#include <string.h>

static struct {
    unsigned count;
    uint32_t callback[EM_ACTOR_POOL_CAPACITY];
    uint32_t record[EM_ACTOR_POOL_CAPACITY];
} A;

int em_area01_arrival_bind(void *ctx, EmActor *actor, const EmActorRosterSpawned *spawned)
{
    (void)ctx;
    if (!actor || !spawned || A.count >= EM_ACTOR_POOL_CAPACITY)
        return -1;
    A.callback[A.count] = actor->callback;
    A.record[A.count] = spawned->record_address;
    ++A.count;
    return 0;
}

void em_area01_arrival_reset(void)
{
    memset(&A, 0, sizeof A);
}

unsigned em_area01_arrival_count(void)
{
    return A.count;
}

int em_area01_arrival_node(unsigned i, uint32_t *callback, uint32_t *record)
{
    if (i >= A.count || !callback || !record)
        return -1;
    *callback = A.callback[i];
    *record = A.record[i];
    return 0;
}
