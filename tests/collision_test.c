/* Collision-family regression fixture. --dump-gates is consumed by the
 * original-ELF oracle in tools/test_collision_reference.py. */
#include "game/em_collision.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static float vertices[] = {-1,-1,0, 1,-1,0, 1,1,0, -1,1,0};
static uint16_t indices[] = {0,1,2,3};
static float edges[] = {0,-1,0, 1,0,0, 0,1,0, -1,0,0};
static EmCollPoly polygon = {.plane={0,0,1,0}, .vcount=4, .set=EM_COLL_SET_GRID};
static unsigned char present;
static const int query_ids[] = {-32768,-1,0,1,2,3,31,32767};

int main(int argc, char **argv)
{
    const int dump = argc == 2 && strcmp(argv[1], "--dump-gates") == 0;
    EmCollision world = {.vert_count=4, .poly_count=1, .index_count=4,
        .verts=vertices, .polys=&polygon, .indices=indices, .edge_n=edges,
        .blob=&present};
    const float from[] = {0,0,1}, to[] = {0,0,-1};
    unsigned checks = 0;
    for (unsigned attr=0; attr<256; ++attr) {
        polygon.attr = (uint8_t)attr;
        for (unsigned i=0; i<sizeof query_ids/sizeof query_ids[0]; ++i) {
            int id = query_ids[i];
            int expected = attr < 0x5a && attr != 0x50 &&
                (attr != 0x51 || id == 0) && (attr != 0x52 || id == 2) &&
                (attr != 0x53 || id != -1);
            EmCollHit hit = {0};
            int result = em_collision_segment_query(&world, from, to,
                                                     EM_COLL_SET_GRID, id, &hit);
            assert(!!result == expected);
            if (result) {
                assert(result == EM_COLL_SET_GRID && hit.attr == attr);
                assert(hit.surf_class == EM_SURF_WALL);
                assert(hit.point[2] == 0 && hit.delta[2] == 1);
            }
            if (dump) printf("segment %u %d %d\n", attr, id, !!result);
            ++checks;
        }
        int camera = !!em_collision_camera_query(&world, from, to,
                                                  EM_COLL_SET_GRID, NULL);
        assert(camera == (attr < 0x51 || attr >= 0x54));
        if (dump) printf("camera %u -1 %d\n", attr, camera);
        ++checks;

        float position[] = {0,0,1};
        EmCollHit hit = {0};
        int movement = !!em_collision_move_probe(&world, position, to,
                                                 EM_COLL_SET_GRID | EM_COLL_SLIDE,
                                                 &hit);
        int expected_move = attr < 0x5a && attr != 0x52;
        assert(movement == expected_move);
        assert(fabsf(position[2] - (movement ? 0.0f : -1.0f)) < 0.000001f);
        if (dump) printf("movement %u 0 %d\n", attr, movement);
        ++checks;
    }
    polygon.attr = 0x50;
    assert(!em_collision_camera_query(&world, from, to, 0, NULL));
    assert(!em_collision_camera_query(&world, to, from, EM_COLL_SET_GRID, NULL));
    /* Published actor cells belong to set2. Their lifetime and nearest
     * result must survive composition with the pre-existing grid set. */
    EmCollBoxFace face={.face=5,.origin={-1,-1,.5f},.extent={2,2,0}};
    EmCollCell cell={.uid=18,.attr=0x46,.face_count=1,.faces=&face};
    EmCollHit compact;
    polygon.attr=0;
    assert(em_collision_cell_bind(&world,&cell));
    assert(em_collision_cell_bind(&world,&cell) && world.actor_cell_count==1);
    assert(em_collision_segment_query(&world,from,to,6,0,&compact)==2);
    assert(compact.poly==-19 && compact.attr==0x46 && compact.point[2]==.5f);
    assert(em_collision_camera_query(&world,from,to,6,&compact)==2);
    assert(!em_collision_camera_query(&world,from,to,1,&compact));
    assert(em_collision_camera_query(&world,from,to,4,&compact)==4);
    float edge_from[]={0,-1,1},edge_to[]={0,-1,-1};
    assert(!em_collision_segment_query(&world,edge_from,edge_to,2,0,&compact));
    assert(em_collision_move_probe(&world,edge_from,edge_to,2,&compact)==2);
    em_collision_cell_unbind(&world,18);
    assert(world.actor_cell_count==0);
    assert(em_collision_segment_query(&world,from,to,6,0,&compact)==4);
    if (!dump) printf("collision test: %u gate cases, mask and backface: PASS\n", checks);
    return 0;
}
