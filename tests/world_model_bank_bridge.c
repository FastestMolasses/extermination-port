/* Narrow area-reset test bridge. Including the adapter exposes its private
 * views to assertions without adding test-only entry points to the game. */
#include "game/em_area11_boxes.c"

static EmActor test_actor;
static EmActorPool test_pool;

/* The bank-selector cases use world models only. A library-provider call
 * would cross the test's boundary and must stop, never fabricate a model. */
int em_area11_roger_001C6120(uint32_t bank, uint32_t id, uint32_t *handle)
{ (void)bank; (void)id; (void)handle; abort(); }
const uint8_t *em_area11_roger_resource(uint32_t address, uint32_t size)
{ (void)address; (void)size; abort(); }

void bank_test_reset(void) { em_area11_boxes_reset(); }
int bank_test_bind(const char *path, uint32_t word)
{ return em_area11_boxes_bind_world_bank(path, word); }
const EmWorldModels *bank_test_view(void) { return em_area11_boxes_world_models(); }
int bank_test_lookup(uint32_t word, uint32_t id, uint32_t *handle)
{ return em_area11_boxes_world_001C6120(word, id, handle); }
const EmOwnerServicesOwner *bank_test_owner(uint32_t id, int door)
{
    memset(&test_actor, 0, sizeof test_actor);
    test_actor.allocated = 1;
    test_actor.generation = 1;
    test_actor.param = (uint8_t)id;
    test_actor.pos[3] = 1;
    for (unsigned i = 0; i < 4; ++i) test_actor.f60[i] = 1;
    int32_t ret = -1;
    if (door) {
        test_actor.callback = DOOR_CALLBACK;
        if (em_area11_boxes_door_001B0EA0(&test_actor, &ret) < 0 || ret) return NULL;
    } else if (em_area11_boxes_owner_001B0FD0(&test_actor, &test_pool, &ret) < 0 || ret) return NULL;
    return &box_for(&test_actor)->view;
}
uint32_t bank_test_owner_method(void) { return box_for(&test_actor)->method; }
int bank_test_slot_count(void) { return S.bones.count; }
int bank_test_seed_library(uint32_t handle, const uint8_t *bytes, uint32_t size, uint32_t word)
{
    S.library_word = word;
    return em_world_models_add(&S.library, handle, bytes, size, NULL);
}
const void *bank_test_library(void) { return &S.library; }
uint32_t bank_test_library_word(void) { return S.library_word; }
int bank_test_fields(uint32_t *anim, uint32_t *model, uint32_t *method)
{ return em_area11_boxes_owner_fields(&test_actor, anim, model, method); }
