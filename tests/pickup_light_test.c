/* Original pickup-effect lifecycle, color range, and parent transform.
 * Include the module to exercise its private color kernel with controlled
 * RNG input; model/GPU boundaries are small deterministic test doubles. */
#include <assert.h>
#include "../src/game/em_pickup.c"

static int draws, random_calls;
static float drawn_palette[32], drawn_tint[4];
static uint32_t random_value;

uint32_t em_random_next(void) { ++random_calls; return random_value; }
void em_game_set_battery(int on) { (void)on; }
void em_game_player_interact_anim(int clip) { (void)clip; }

int em_model_load(EmModel *m, const char *path)
{
    (void)path;
    memset(m,0,sizeof *m);
    m->bone_count=2;
    return 0;
}
void em_model_free(EmModel *m) { memset(m,0,sizeof *m); }
void em_model_palette_at(const EmModel *m, uint32_t clip, double time,
                         float *out)
{
    (void)clip; (void)time;
    memset(out,0,m->bone_count*16*sizeof(float));
    for (uint32_t i=0;i<m->bone_count;++i)
        for (unsigned j=0;j<4;++j) out[i*16+j*5]=1.0f;
}
EmGfxMesh *em_gfx_mesh_create(EmGfx *gfx, const float *verts, uint32_t count,
                              const uint32_t *indices, uint32_t index_count,
                              const EmGfxTexDesc *texs, uint32_t tex_count,
                              const uint8_t *texels, uint32_t flags)
{
    (void)gfx; (void)verts; (void)count; (void)indices; (void)index_count;
    (void)texs; (void)tex_count; (void)texels; (void)flags;
    return (EmGfxMesh *)(uintptr_t)2;
}
void em_gfx_mesh_destroy(EmGfx *gfx, EmGfxMesh *mesh)
{ (void)gfx; (void)mesh; }
void em_gfx_draw_skinned_additive(EmGfx *gfx, EmGfxMesh *mesh,
                                  const float *viewproj, const float *palette,
                                  uint32_t count, const float rgba[4])
{
    (void)gfx; (void)mesh; (void)viewproj;
    assert(count==2);
    ++draws;
    memcpy(drawn_palette,palette,sizeof drawn_palette);
    memcpy(drawn_tint,rgba,sizeof drawn_tint);
}

int main(void)
{
    const float green[4]={0,1,0,.25f};
    float tint[4];
    /* Original state4: reverse the saved SDK LCG 19..14 calls to recover
     * these six inputs. Every result is the original actor+84 float,
     * including four values ordinary host rounding computes differently. */
    const uint32_t random_fixture[6]={0x406c517e,0x746746df,0x770b4f2c,
                                      0x09f84df5,0x66f8078a,0x48a371fb};
    const float delta_fixture[6]={0.20989990234375f,25.996994018554688f,
        27.30706787109375f,-26.803977966308594f,19.332199096679688f,
        4.2854766845703125f};
    for (unsigned i=0;i<6;++i) {
        float delta[3];
        em_effect_delta(random_fixture[i],green,delta);
        assert(delta[0]==-127.0f && delta[1]==delta_fixture[i] && delta[2]==-127.0f);
    }
    em_effect_color(0,green,tint);
    assert(tint[0]==1.0f/128 && tint[1]==96.0f/128 && tint[2]==1.0f/128);
    em_effect_color(0x40000000,green,tint);
    assert(tint[1]==1.0f);
    em_effect_color(0x7fffffff,green,tint);
    assert(tint[1]==159.0f/128 && tint[3]==1.0f);
    const float full[4]={1,1,1,1};
    em_effect_color(0x7fffffff,full,tint);
    assert(tint[0]==254.0f/128); /* largest 31-bit RNG input truncates below 1 */

    EmGfx *gfx=(EmGfx *)(uintptr_t)1;
    const float pos[3]={211.6f,229.9f,227.2f};
    const float viewproj[16]={0};
    EmFrameInput input={0};
    em_pickup_reset();
    assert(em_pickup_light_add(gfx,"scene",0xb01,"light",green)==-1);
    assert(em_pickup_add(gfx,"scene",0x1b,pos,-PICKUP_PI/2,
                         0xb01,"props/item_72.emdl",0)==0);
    assert(em_pickup_light_add(gfx,"scene",0xb01,"light",green)==0);
    assert(em_pickup_light_add(gfx,"scene",0xb01,"light",green)==-1);
    em_pickup_lights_draw(gfx,viewproj);
    assert(draws==0);
    em_pickup_update(pos,0,&input,0);
    em_pickup_lights_draw(gfx,viewproj);
    assert(draws==0 && random_calls==0); /* original initialization frame */
    random_value=0x40000000;
    em_pickup_update(pos,0,&input,0);
    em_pickup_lights_draw(gfx,viewproj);
    assert(draws==1 && random_calls==1 && drawn_tint[1]==1.0f);
    assert(drawn_palette[12]==pos[0] && drawn_palette[13]==pos[1]);
    assert(drawn_palette[14]==pos[2] && fabsf(drawn_palette[2]-1)<1e-6f);
    assert(memcmp(drawn_palette,drawn_palette+16,16*sizeof(float))==0);
    s.p[0].armed=4;
    s.p[0].take_t=1;
    em_pickup_update(pos,0,&input,0);
    em_pickup_lights_draw(gfx,viewproj);
    assert(draws==1 && random_calls==1); /* owner took: no stale effect */
    /* AREA11 UID0B01 is original item1B. 001C40B0 adds 12 internal
     * half-units, not merely an item count or ammunition. */
    assert(em_pickup_item_count(0x1b)==1);
    assert(em_pickup_battery_charge()==12 && em_pickup_battery_capacity()==12);
    em_pickup_scene_clear(gfx);
    assert(em_pickup_battery_charge()==12); /* global inventory persists */
    assert(em_pickup_light_add(gfx,"scene",0xb01,"light",green)==-2);
    em_pickup_lights_draw(gfx,viewproj);
    assert(draws==1);
    em_pickup_battery_set_charge(2);
    inventory_add(0x1c,1);
    assert(em_pickup_battery_charge()==36 && em_pickup_battery_capacity()==36);
    em_pickup_battery_set_charge(5);
    inventory_add(0x1b,1);
    assert(em_pickup_battery_charge()==17 && em_pickup_battery_capacity()==36);
    inventory_add(0x1d,1);
    assert(em_pickup_battery_charge()==48 && em_pickup_battery_capacity()==48);
    g.count[0x1b]=255;
    inventory_add(0x1b,1);
    assert(em_pickup_item_count(0x1b)==0); /* original byte store wraps */
    assert(em_pickup_battery_charge()==48 && em_pickup_ammo_take()==0);
    em_pickup_battery_set_charge(-1);
    assert(em_pickup_battery_charge()==0);
    em_pickup_battery_set_charge(100);
    assert(em_pickup_battery_charge()==48);
    em_pickup_reset();
    assert(em_pickup_battery_charge()==0 && em_pickup_battery_capacity()==0);
    puts("pickup indicators and original battery inventory: PASS");
    return 0;
}
