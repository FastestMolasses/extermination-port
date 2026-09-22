#include "game/em_area11_flow.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void verify_milestones(void)
{
    for (unsigned step = 0; step < 256; ++step) {
        int expected = -1;
        if (step == 0 || step == 1) expected = 0;
        if (step == 16 || step == 17) expected = 1;
        if (step == 32) expected = 2;
        assert(em_area11_beat_for_step((uint8_t)step) == expected);
    }
    assert(em_area11_step_after_beat(0) == 16);
    assert(em_area11_step_after_beat(1) == 32);
    assert(em_area11_step_after_beat(2) == 255);
}

static void verify_height_and_polygon(void)
{
    /* Asset-free synthetic polygons deliberately include a slanted edge. */
    EmArea11Triggers triggers = {0};
    const float polygon[4][2] = {{0,2},{10,4},{10,0},{2,0}};
    for (int beat = 0; beat < 3; ++beat)
        memcpy(triggers.polygon[beat], polygon, sizeof polygon);
    float p[3] = {5,270,2};
    assert(em_area11_trigger_contains(&triggers,0,p));
    p[1] = 260; assert(em_area11_trigger_contains(&triggers,0,p));
    p[1] = 280; assert(em_area11_trigger_contains(&triggers,0,p));
    p[1] = nextafterf(260,-INFINITY);
    assert(!em_area11_trigger_contains(&triggers,0,p));
    p[1] = nextafterf(280,INFINITY);
    assert(!em_area11_trigger_contains(&triggers,0,p));
    p[1] = 275; assert(em_area11_trigger_contains(&triggers,1,p));
    p[1] = nextafterf(275,-INFINITY);
    assert(!em_area11_trigger_contains(&triggers,1,p));
    p[1] = 300; assert(em_area11_trigger_contains(&triggers,1,p));
    p[1] = 285; assert(em_area11_trigger_contains(&triggers,2,p));
    p[1] = nextafterf(285,-INFINITY);
    assert(!em_area11_trigger_contains(&triggers,2,p));
    p[1] = 300; assert(em_area11_trigger_contains(&triggers,2,p));
    p[0] = 1; p[2] = 3; /* Inside bounding box, outside the polygon. */
    assert(!em_area11_trigger_contains(&triggers,1,p));
    p[0] = -1; p[2] = 2;
    assert(!em_area11_trigger_contains(&triggers,1,p));
    assert(!em_area11_trigger_contains(&triggers,-1,p));
    assert(!em_area11_trigger_contains(&triggers,3,p));
}

static void verify_original_export(const char *path)
{
    EmArea11Triggers triggers;
    assert(em_area11_triggers_load(&triggers,path));
    float p[3] = {250.8f,229.9f,209}; /* New Game spawn is not a trigger. */
    for (int beat = 0; beat < 3; ++beat)
        assert(!em_area11_trigger_contains(&triggers,beat,p));
    p[0]=350; p[1]=270; p[2]=240;
    assert(em_area11_trigger_contains(&triggers,0,p));
    p[0]=480; p[1]=280; p[2]=284;
    assert(em_area11_trigger_contains(&triggers,1,p));
    p[1]=274;
    assert(!em_area11_trigger_contains(&triggers,1,p));
    p[0]=454; p[1]=280; p[2]=291; /* Old AABB incorrectly accepted this. */
    assert(!em_area11_trigger_contains(&triggers,1,p));
    p[0]=420; p[1]=290; p[2]=190;
    assert(em_area11_trigger_contains(&triggers,2,p));
    p[1]=284;
    assert(!em_area11_trigger_contains(&triggers,2,p));
}

int main(int argc, char **argv)
{
    verify_milestones();
    verify_height_and_polygon();
    if (argc == 2) verify_original_export(argv[1]);
    puts("area11_flow_test: PASS");
    return 0;
}
