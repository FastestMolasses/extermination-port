/* The pickup light's colour (001F54E0 = em_effect_kinds_001F54E0, then the
 * 001D8C30 mode-1 GS conversion), its draw at the owner's transform, and the
 * battery inventory. The child's behaviour (init frame, stop) is
 * em_indicator_child (tests/indicator_child_test.c and
 * tools/test_census_unverified_reference.py). Model/GPU boundaries are
 * small deterministic test doubles. */
#include <assert.h>
#include "../src/game/em_pickup.c"
#include "game/em_effect_kinds.h"

static int draws, random_calls;
static float drawn_palette[32], drawn_tint[4];
static uint32_t random_value;

uint32_t em_random_next(void) { ++random_calls; return random_value; }

/* 001F54E0 over a child whose +0x80 is `colour`, with the RNG value `r`;
 * delta = the +0x80..+0x88 it leaves, tint = the GS colour / 128. */
static int w_rand(void *ctx, int32_t *v0) { *v0 = (int32_t)*(uint32_t *)ctx; return 0; }
static int w_draw(void *ctx, uint32_t fn, void *obj) { (void)ctx; (void)obj; return fn == 0x001CACB0u ? 0 : -1; }
static void colour_001F54E0(uint32_t r, const float colour[4], float delta[3], float tint[4])
{
    const EmEffectKindsWorkers w = {.ctx = &r, .w_00122BB8 = w_rand, .w_indirect = w_draw};
    EmEffectKinds k = {.workers = &w};
    float c80[4];
    memcpy(c80, colour, sizeof c80);
    assert(em_effect_kinds_001F54E0(&k, c80, c80, 0x001CACB0u, c80) == 0);
    if (delta) memcpy(delta, c80, 3 * sizeof(float));
    if (tint) em_effect_color_gs(c80, tint);
}

/* 001C40B0 over em_pickup's canonical item block (the resolver the
 * owner adapter uses). */
static void inventory_add(int type, int n)
{
    assert(em_pickup_items_001C40B0(items_resolve, NULL, type, n) == 0);
}
/* The D2 progress region (taken bits, CA4..CA7); the game owns it in
 * em_scene_bindings.c. */
EmSceneState *em_scene_state(void) { static EmSceneState state; return &state; }

int em_model_load(EmModel *m, const char *path)
{
    (void)path;
    memset(m,0,sizeof *m);
    m->bone_count=2;
    return 0;
}
void em_model_free(EmModel *m) { memset(m,0,sizeof *m); }
int em_model_clip_index(const EmModel *m, uint32_t clip)
{
    for (uint32_t i=0; i<m->clip_count; ++i) if (m->clips[i].id == clip) return (int)i;
    return -1;
}
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
        colour_001F54E0(random_fixture[i],green,delta,NULL);
        assert(delta[0]==-127.0f && delta[1]==delta_fixture[i] && delta[2]==-127.0f);
    }
    colour_001F54E0(0,green,NULL,tint);
    assert(tint[0]==1.0f/128 && tint[1]==96.0f/128 && tint[2]==1.0f/128);
    colour_001F54E0(0x40000000,green,NULL,tint);
    assert(tint[1]==1.0f);
    colour_001F54E0(0x7fffffff,green,NULL,tint);
    assert(tint[1]==159.0f/128 && tint[3]==1.0f);
    const float full[4]={1,1,1,1};
    colour_001F54E0(0x7fffffff,full,NULL,tint);
    assert(tint[0]==254.0f/128); /* largest 31-bit RNG input truncates below 1 */

    EmGfx *gfx=(EmGfx *)(uintptr_t)1;
    const float pos[3]={211.6f,229.9f,227.2f};
    const float viewproj[16]={0};
    em_pickup_reset();
    assert(em_pickup_light_add(gfx,"scene",0xb01,"light")==-1);
    assert(em_pickup_add(gfx,"scene",0x1b,pos,-PICKUP_PI/2,
                         0xb01,"props/item_72.emdl",0)==0);
    s.p[0].source_id=0x2A;   /* the owner's EMIS record (the host binds it) */
    assert(em_pickup_light_add(gfx,"scene",0xb01,"light")==0);
    assert(em_pickup_light_add(gfx,"scene",0xb01,"light")==-1);
    em_pickup_lights_draw(gfx,viewproj);
    assert(draws==0);          /* nothing submitted: no draw */
    float c80[4];
    colour_001F54E0(0x40000000,green,c80,NULL);
    c80[3]=1.0f;
    assert(em_pickup_light_submit(0x2B,c80)==-1);   /* no light of that owner */
    assert(em_pickup_light_submit(0x2A,c80)==0);
    em_pickup_lights_draw(gfx,viewproj);
    assert(draws==1 && drawn_tint[1]==1.0f);
    assert(drawn_palette[12]==pos[0] && drawn_palette[13]==pos[1]);
    assert(drawn_palette[14]==pos[2] && fabsf(drawn_palette[2]-1)<1e-6f);
    assert(memcmp(drawn_palette,drawn_palette+16,16*sizeof(float))==0);
    em_pickup_lights_draw(gfx,viewproj);
    assert(draws==1);          /* one draw per submitted 001F54E0 */
    /* The owner's FREE (00219550 state 3's 001AFC10) and its taken bit. */
    s.p[0].used=0;
    taken_set(0xb01);
    assert(em_pickup_light_submit(0x2A,c80)==-1);   /* owner gone: no stale draw */
    em_pickup_lights_draw(gfx,viewproj);
    assert(draws==1);
    assert(random_calls==0);   /* the RNG is the child's 001F54E0 worker's, not em_pickup's */
    /* AREA11 UID0B01 is original item1B. 001C40B0 adds 12 internal
     * half-units, not merely an item count or ammunition. */
    inventory_add(0x1b,1);
    assert(em_pickup_item_count(0x1b)==1);
    assert(em_pickup_battery_charge()==12 && em_pickup_battery_capacity()==12);
    em_pickup_scene_clear(gfx);
    assert(em_pickup_battery_charge()==12); /* global inventory persists */
    assert(em_pickup_light_add(gfx,"scene",0xb01,"light")==-2);
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
    item_store(0x00810C7Fu,255);
    inventory_add(0x1b,1);
    assert(em_pickup_item_count(0x1b)==0); /* original byte store wraps */
    assert(em_pickup_battery_charge()==48);
    em_pickup_battery_set_charge(-1);
    assert(em_pickup_battery_charge()==0);
    em_pickup_battery_set_charge(100);
    assert(em_pickup_battery_charge()==48);
    em_pickup_reset();
    assert(em_pickup_battery_charge()==0 && em_pickup_battery_capacity()==0);
    /* 001AF2C0's seeds (tools/test_continue_reset_reference.py checks
     * every em_pickup field against the executed original). */
    assert(em_pickup_item_count(0)==1 && em_pickup_item_count(5)==1 &&
           em_pickup_item_count(7)==1 && em_pickup_item_count(0x17)==1 &&
           em_pickup_item_count(0x10)==2 && em_pickup_mag_packs()==2);
    /* 001C40B0 case 0x10 writes em_weapon's D_00810C62/D_00810CB4 directly
     * (unbound: the take faults instead of queueing rounds). */
    assert(em_pickup_items_001C40B0(items_resolve, NULL, 0x10, 1) == -1);
    uint8_t mag=0; int16_t reserve=60;
    em_pickup_set_weapon_ammo(&mag,&reserve);
    inventory_add(0x10,1);
    assert(mag==30 && reserve==90 && em_pickup_mag_packs()==4 && em_pickup_item_count(0x10)==4);
    /* The map/key bytes overlap the counts, as in the original. */
    inventory_add(0x5C,1);
    assert(em_pickup_maps()[8]==1);

    /* 00827630 state-0 init pose (AREA11 fan pair). Record 1 (+0x2E 0,
     * yaw -pi) gets rot.z +pi/4, record 2 (+0x2E 1, yaw 0) -pi/4, and
     * build_trs_matrix applies rot.z about world Z after the yaw (the
     * +0xD0 matrices of both actors in the AREA11 captures). */
    em_pickup_scene_clear(gfx);
    const float fan1[3]={329.3f,309.3f,159.8f}, fan2[3]={329.3f,309.3f,160.8f};
    const float r=0.70710677f;
    int f1=em_pickup_add(gfx,"scene",0x13,fan1,-PICKUP_PI,0,"props/area_item_13.emdl",1);
    int f2=em_pickup_add(gfx,"scene",0x13,fan2,0,0,"props/area_item_13.emdl",1);
    assert(f1==0 && f2==1);
    assert(em_pickup_owner_init_pose(f1,0x827630,0)==0);
    assert(em_pickup_owner_init_pose(f2,0x827630,1)==0);
    assert(em_pickup_owner_init_pose(f2,0x827490,1)==-1); /* not translated */
    assert(em_pickup_owner_init_pose(7,0x827630,0)==-1);
    const float *p1=s.p[f1].palette, *p2=s.p[f2].palette;
    assert(fabsf(p1[0]+r)<1e-6f && fabsf(p1[1]+r)<1e-6f && fabsf(p1[2])<1e-6f);
    assert(fabsf(p1[4]+r)<1e-6f && fabsf(p1[5]-r)<1e-6f && fabsf(p1[10]+1)<1e-6f);
    assert(fabsf(p2[0]-r)<1e-6f && fabsf(p2[1]+r)<1e-6f && fabsf(p2[2])<1e-6f);
    assert(fabsf(p2[4]-r)<1e-6f && fabsf(p2[5]-r)<1e-6f && fabsf(p2[10]-1)<1e-6f);
    assert(p1[12]==fan1[0] && p1[13]==fan1[1] && p1[14]==fan1[2]);
    puts("pickup indicators and original battery inventory: PASS");
    return 0;
}
