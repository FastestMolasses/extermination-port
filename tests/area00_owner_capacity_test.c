/* Diagnose the current protected owner's capacity without modifying it. */
#include "game/em_area11_boxes.c"
#include <assert.h>

int main(void)
{
    assert(BOX_MAX > 1 && BOX_MAX <= EM_ACTOR_POOL_CAPACITY);
    EmActor actors[BOX_MAX + 1];
    memset(actors, 0, sizeof actors);
    for (unsigned i = 0; i <= BOX_MAX; ++i) {
        actors[i].generation = 1;
        actors[i].callback = DRUM_CALLBACK;
        actors[i].status = 1;
    }
    for (unsigned i = 0; i < BOX_MAX; ++i) {
        Box *view = box_for(&actors[i]);
        assert(view && view == box_for(&actors[i]));
    }
    assert(!box_for(&actors[BOX_MAX]));
    S.box[0].freed = 1;
    assert(box_for(&actors[BOX_MAX]) == &S.box[0]);
    assert(!box_for(&actors[0]));
    actors[1].generation++;
    assert(box_for(&actors[0]) == &S.box[1]);
    assert(!box_for(&actors[1]));
    printf("{\"box_capacity\":%u,\"actor_capacity\":%u,\"view_bytes\":%zu,"
           "\"extra_owner_refused\":true,\"freed_and_generation_reuse\":true}\n",
           BOX_MAX, EM_ACTOR_POOL_CAPACITY, sizeof(Box));
    return 0;
}
