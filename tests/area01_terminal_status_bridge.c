#include "game/em_area01_terminal_status.h"
#include <assert.h>
#include <limits.h>

static EmSceneState scene;
static EmActorPool pool;
static EmArea01ActorView actors;

int terminal_status_reset(const uint8_t *before, uint8_t *after)
{
    memset(&scene, 0xA5, sizeof scene);
    memcpy(scene.d810040, before, sizeof scene.d810040);
    int rc = em_area01_terminal_reset(&scene);
    memcpy(after, scene.d810040, sizeof scene.d810040);
    /* No neighboring canonical state changed. */
    const uint8_t *bytes = (const uint8_t *)&scene;
    for (unsigned i = 0; i < sizeof scene; ++i)
        if (i < offsetof(EmSceneState, d810040) ||
            i >= offsetof(EmSceneState, d810040) + sizeof scene.d810040)
            assert(bytes[i] == 0xA5);
    return rc;
}

int terminal_status_contract(void)
{
    memset(&scene, 0xA5, sizeof scene);
    uint8_t *block = em_area01_scene_view(&scene, 0x00810040u, 0xD4u);
    assert(block == scene.d810040);
    for (unsigned i = 0; i < 0xD4; ++i)
        assert(em_area01_scene_view(&scene, 0x00810040u+i, 0xD4u-i) == block+i);
    const uint32_t bad[][2] = {{0x81003F,1},{0x81003F,2},{0x810040,0},{0x810040,0xD5},
                               {0x810113,2},{0x810114,1},{0xFFFFFFF0u,32}};
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; ++i)
        assert(!em_area01_scene_view(&scene,bad[i][0],bad[i][1]));
    memset(scene.req, 0, sizeof scene.req);
    em_scene_progress_reset_001AF2C0(&scene);
    assert(block == scene.d810040 && block[0] == 0xA5 && block[0xD3] == 0xA5);
    assert(em_area01_terminal_reset(&scene) == 0);
    for (unsigned i = 0; i < 0xD4; ++i) assert(block[i] == 0);
    assert(em_area01_terminal_reset(NULL) < 0);
    EmArea01RoomCall c = {.fn=0x121A28,.na=3,.a={0x810040,0,0xD4}};
    for (unsigned cut = 0; cut < 5; ++cut) {
        EmArea01RoomCall badcall = c;
        if (cut == 0) badcall.fn++;
        if (cut == 1) badcall.a[0]--;
        if (cut == 2) badcall.a[1]++;
        if (cut == 3) badcall.a[2]++;
        if (cut == 4) badcall.nf++;
        memset(block,0xA5,0xD4);
        assert(em_area01_terminal_reset_worker(&scene,&badcall) < 0);
        for (unsigned i=0;i<0xD4;++i) assert(block[i]==0xA5);
    }
    memset(&scene,0,sizeof scene);
    em_actor_pool_reset_001AF8E0(&pool);
    em_area01_actor_view_reset(&actors,&pool,NULL,NULL,NULL);
    EmActor *a = em_actor_pool_alloc_001AFA90(&pool,&scene,4);
    assert(a);
    uint32_t owner=em_actor_pool_address(&pool,a);
    int32_t value;
    for (unsigned type=0;type<256;++type) {
        a->model=(uint8_t)type;
        int rc=em_area01_terminal_owner_read(&actors,owner,3,1,&value);
        assert(type == 0x38 ? rc == 0 && value == 0x38 : rc < 0);
    }
    a->model=0x38;
    const int16_t costs[]={INT16_MIN,-1,0,1,2,24,INT16_MAX};
    for (unsigned i=0;i<sizeof costs/sizeof *costs;++i) {
        a->w34=(uint16_t)costs[i];
        assert(em_area01_terminal_owner_read(&actors,owner,0x34,2,&value)==0 && value==costs[i]);
    }
    assert(em_area01_terminal_owner_read(&actors,owner+1,3,1,&value)<0);
    assert(em_area01_terminal_owner_read(&actors,owner,0x34,1,&value)<0);
    assert(em_area01_terminal_owner_read(&actors,owner,0xA,1,&value)<0);
    assert(em_area01_terminal_owner_read(&actors,owner,3,1,NULL)<0);
    assert(em_area01_actor_view_begin(&actors)==0);
    assert(em_area01_terminal_owner_read(&actors,owner,3,1,&value)<0);
    assert(em_area01_actor_view_commit(&actors)==0);
    assert(em_actor_pool_free_001AFC10(&pool,&scene,a)==0);
    assert(em_area01_terminal_owner_read(&actors,owner,3,1,&value)<0);
    a=em_actor_pool_alloc_001AFA90(&pool,&scene,4);
    assert(a && em_actor_pool_address(&pool,a)==owner);
    a->model=0x38;a->w34=6;
    assert(em_area01_terminal_owner_read(&actors,owner,0x34,2,&value)==0 && value==6);
    em_actor_pool_reset_001AF8E0(&pool);
    assert(em_area01_terminal_owner_read(&actors,owner,3,1,&value)<0);
    return 0;
}
#ifdef EM_TERMINAL_STATUS_CONTRACT
int main(void) { return terminal_status_contract(); }
#endif
