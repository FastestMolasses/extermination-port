#include "game/em_door_original_runtime.h"
#include "game/em_effect_color.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    EmDoorOriginalRuntime door;
    EmInteractionScene scene;
    unsigned publications, draws;
    int visibility;
} Fixture;

static int publish(void *context, const float position[3])
{
    Fixture *f = context;
    const float expected[3] = {423, em_effect_float32(184.8000030517578 + 10),
                               290.29998779296875};
    assert(!memcmp(position, expected, sizeof expected));
    ++f->publications;
    return f->visibility;
}

static int draw(void *context)
{
    Fixture *f = context;
    ++f->draws;
    return 1;
}

int main(void)
{
    Fixture f = {0};
    assert(em_interaction_scene_load(&f.scene, "assets/scene_snow/interaction.emis"));
    EmInteractionSceneOwner *source = em_interaction_scene_role(&f.scene, EM_INTERACTION_DOOR);
    EmDoorOriginalRuntimeHooks hooks = {.context = &f, .publish = publish, .draw = draw};
    assert(em_door_original_runtime_load(&f.door, "assets/scene_snow", source, &hooks));
    assert(f.door.owner.lifecycle == 1 && f.door.owner.status == 1 && !f.door.owner.phase);
    assert(f.door.owner.door_id == 0 && f.door.owner.side == 0);
    assert(f.door.model.bone_count == 3 && f.door.model.flags == 0);
    assert(f.door.destination[0] == 2 && f.door.destination[1] == 1);
    assert(f.door.sounds[0] == 0x401 && f.door.sounds[1] == 0x402);
    assert(!f.publications && !f.draws);
    assert(em_interaction_scene_bind(&f.scene, f.door.source_id, &f.door,
        &f.door.owner.status, &f.door.owner.class_flags, &f.door.owner.armed));
    float initial[48];
    memcpy(initial, f.door.palette, sizeof initial);
    for (unsigned i = 0; i < 240; ++i) {
        f.visibility = i & 1;
        assert(em_door_original_runtime_tick(&f.door, 0) == 1);
        assert(f.door.playback.remaining == 150 && !f.door.owner.animation_flags);
        assert(!memcmp(initial, f.door.palette, sizeof initial));
        assert(f.door.owner.visible == f.visibility);
    }
    assert(f.publications == 240 && f.draws == 240);
    assert(em_door_original_runtime_arm(&f.door));
    assert(em_door_original_runtime_tick(&f.door, 0) == -1);
    assert(f.door.failed && strstr(f.door.error, "BBE40"));
    assert(f.door.owner.phase == 0 && f.door.owner.armed == 4);
    assert(f.publications == 240 && f.draws == 240);
    /* A missing transit worker preserves the actual armed owner as a fault;
     * it never invents a lock message or reports a completed interaction. */
    source->native_owner = NULL;
    source->live_status = source->live_class_flags = source->live_armed = NULL;
    em_door_original_runtime_free(&f.door);
    assert(!f.door.loaded && !f.door.model.verts && !f.door.bank.clips);
    assert(em_door_original_runtime_load(&f.door, "assets/scene_snow", source, &hooks));
    f.door.owner.lifecycle = 2;
    assert(em_door_original_runtime_tick(&f.door, 0) == 0);
    assert(f.door.owner.freed && !f.door.owner.status && !(f.door.owner.class_flags & 0x80));
    em_door_original_runtime_free(&f.door);
    source->position[0] += 1;
    assert(!em_door_original_runtime_load(&f.door, "assets/scene_snow", source, &hooks));
    assert(!f.door.loaded && !f.door.model.verts && !f.door.bank.clips);
    puts("Original door runtime: PASS 240 passive callbacks, canonical binding, missing-worker fault and teardown");
    return 0;
}
