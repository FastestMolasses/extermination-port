/* Test-only visibility into camera typed-view boundaries. Actual camera,
 * pose SDK and aim owners run; synthetic sentinels are not runtime seeds. */
#include "../src/game/em_camera_live.c"
#include "game/em_aim_fire_runtime.h"
#include "game/em_coll_segment_walkers.h"
#include "game/em_pose_host_workers.h"
#include "game/em_aim_fire_sdk_memory.h"
#include <assert.h>

static EmSceneState test_scene;
EmGameState g;
EmSceneState *em_scene_state(void) { return &test_scene; }
static EmCollProbeState test_probe;
static EmCollSegment test_segment;
const EmCollSegment *em_collision_world_segment(void) { return &test_segment; }
const EmCollisionWorldOwners *em_collision_world_owners(void) { return NULL; }
int em_coll_segment_hit(const EmCollSegment *seg,EmCollSegmentHit *hit)
{ (void)seg;memset(hit,0,sizeof *hit);return 0; }
/* Explicit worker-contract fixture: independent original collision oracle
 * verifies the collision body. Here a nested native caller changes its
 * shared XYZ fields, which the camera must not overwrite with a stale view. */
int em_coll_segment_0019A910(const EmCollSegment *seg,const float from[3],const float to[3],unsigned mask)
{
    (void)from;(void)to;(void)mask;
    seg->face->box_min[0]=7;seg->face->rel[2]=9;return 0;
}
static unsigned callback_calls;
static uint32_t expected_fn,expected_a0,expected_a1;
static int callback_result;
static int camera_area_worker(void *ctx,uint32_t fn,uint32_t a0,uint32_t a1)
{
    (void)ctx;
    assert(fn==expected_fn && a0==expected_a0 && a1==expected_a1);
    assert(g.cam.sub_state==C.cam.rec.bytes[1]);
    assert(g.cam.eye[0]==em_ee_float(C.pool[P_EYE]));
    C.cam.rec.bytes[1]=7;C.cam.rec.bytes[5]=1;
    C.pool[P_EYE]=0x42100000;S3600[0]=0x43210000;
    /* The composite's normal suspend publishes its canonical stores. */
    if (callback_result >= 0) view_store();
    callback_calls++;return callback_result;
}

static void *sdk_scratch(void *ctx,uint32_t a,size_t n,int write)
{
    (void)ctx;(void)write;
    return n<=UINT32_MAX ? em_camera_live_scratch_bytes(a,(uint32_t)n) : NULL;
}
/* Binary test-only transport: each case supplies the six original vectors.
 * The actual SDK memory adapter executes against the actual alias provider. */
static int sdk_cases(void)
{
    uint32_t input[29],output[26],a0[4],b0[4];
    uint32_t *c0=em_aim_fire_runtime_scratch_38C0();
    assert(em_camera_live_scratch_38_bind(a0,b0,c0)==0);
    while(fread(input,sizeof input,1,stdin)==1) {
        memcpy(a0,input+5,16);memcpy(b0,input+9,16);memcpy(c0,input+13,64);
        EmAimFireTargetCall c={0};c.function=input[0];c.na=3;c.nf=1;
        for(unsigned i=0;i<3;++i)c.a[i]=input[i+1];c.f[0]=input[4];
        output[0]=(uint32_t)em_aim_fire_sdk_memory_call(NULL,sdk_scratch,&c);output[1]=c.f0;
        memcpy(output+2,a0,16);memcpy(output+6,b0,16);memcpy(output+10,c0,64);
        if(fwrite(output,sizeof output,1,stdout)!=1)return 1;
    }
    assert(em_camera_live_scratch_38_bind(NULL,NULL,NULL)==0);
    return ferror(stdin) ? 1 : 0;
}

/* The room camera seat's eye rows D_0024A8D0 (001B0460 / 001B0300 mode 1):
 * stdin carries the ELF's 0x180 bytes; the actual export through the
 * camera's accessor and 001B0460's reader must give them word for word,
 * and only them (other addresses go to the room's spawn-table reader). */
static unsigned room_reads;
static int room_word(void *ctx,uint32_t a,uint32_t *out)
{ (void)ctx;assert(a<EYE_BASE || a>=EYE_END);room_reads++;*out=a;return 0; }
static int eye_cases(void)
{
    uint8_t want[EYE_END-EYE_BASE];
    if(fread(want,1,sizeof want,stdin)!=sizeof want || fgetc(stdin)!=EOF)return 1;
    const EmCameraLiveRoom room={NULL,room_word,NULL,NULL,NULL,NULL};
    s_room=&room;
    for(uint32_t a=0;a<sizeof want;a+=4) {
        const uint8_t *p=em_camera_live_eye_rows(EYE_BASE+a,4);uint32_t w;
        assert(p && !memcmp(p,want+a,4));
        assert(room_read(NULL,EYE_BASE+a,&w)==0 && !memcmp(&w,want+a,4));
    }
    for(uint32_t r=0;r<sizeof want/12;++r)
        assert(!memcmp(em_camera_live_eye_rows(EYE_BASE+12*r,12),want+12*r,12));
    assert(room_reads==0);
    uint32_t w;assert(room_read(NULL,0x0024D654u,&w)==0 && w==0x0024D654u && room_reads==1);
    assert(room_read(NULL,EYE_BASE-4,&w)==0 && room_reads==2);
    assert(!em_camera_live_eye_rows(EYE_BASE-4,4) && !em_camera_live_eye_rows(EYE_BASE-4,8));
    assert(!em_camera_live_eye_rows(EYE_END-8,12) && !em_camera_live_eye_rows(EYE_END,4));
    assert(!em_camera_live_eye_rows(EYE_BASE,0));
    s_room=NULL;
    printf("PASS eye rows: %zu words through the accessor and 001B0460's reader, 2 delegated reads, 5 refusals\n",
           sizeof want/4);
    return 0;
}

int main(int argc,char **argv)
{
    if(argc==2 && !strcmp(argv[1],"--sdk"))return sdk_cases();
    if(argc==2 && !strcmp(argv[1],"--eye"))return eye_cases();
    uint32_t matrix[16];EmCollSegmentFaceScratch face;
    _Static_assert(offsetof(EmCollSegmentFaceScratch,box_max)==16,"quad layout");
    _Static_assert(offsetof(EmCollSegmentFaceScratch,delta)==32,"quad layout");
    _Static_assert(offsetof(EmCollSegmentFaceScratch,rel)==48,"quad layout");
    for(unsigned i=0;i<16;++i)matrix[i]=0xA0100000+i;
    uint32_t initial[16];for(unsigned i=0;i<16;++i)initial[i]=0xB0100000+i;
    memcpy(&face,initial,sizeof initial);
    test_segment.state=&test_probe;test_segment.face=&face;
    assert(em_camera_live_scratch_bind(matrix,(uint32_t *)(void *)face.box_min,
           (uint32_t *)(void *)face.box_max,(uint32_t *)(void *)face.rel)==0);
    assert(em_aim_fire_runtime_scratch_3600_bind((uint32_t *)(void *)face.box_min)==0);
    assert(em_camera_live_scratch_bytes(0x70003400,64)==(uint8_t *)matrix);
    assert(em_camera_live_scratch_bytes(0x70003600,16)==(uint8_t *)face.box_min);
    assert(em_camera_live_scratch_bytes(0x70003610,16)==(uint8_t *)face.box_max);
    assert(em_camera_live_scratch_bytes(0x70003630,16)==(uint8_t *)face.rel);
    assert(!em_camera_live_scratch_bytes(0x70003608,16));
    assert(em_aim_fire_runtime_scratch_3600()==(uint32_t *)(void *)face.box_min);
    const uint8_t packed[10]={1,2,3,4,5,6,7,8,9,10};uint32_t quaternion[4];
    /* The actual decoder writes all four canonical lanes before a camera
     * view observes them, including the previously omitted W word. */
    em_pose_host_001C84D0(packed,quaternion,(uint32_t *)(void *)face.box_min);
    assert(!memcmp(quaternion,face.box_min,16));
    follow_load();assert(!memcmp(C.fs.s3600,face.box_min,16));
    C.fs.s3600[3]=0x3F800000;C.fs.s3400[1]=0x11223344;follow_store();
    assert(((uint32_t *)(void *)face.box_min)[3]==0x3F800000 && matrix[1]==0x11223344);
    specials_load();assert(!memcmp(C.sps.s3630,face.rel,16));
    C.sps.s3630[3]=0xABCDEF12;specials_store();
    assert(((uint32_t *)(void *)face.rel)[3]==0xABCDEF12);
    follow_load();EmCameraFollowHit hit;uint32_t point[4]={0,0,0,0};
    assert(fw_segment(NULL,point,point,0,&hit)==0);follow_store();
    assert(face.box_min[0]==7 && face.rel[2]==9);
    face.box_min[0]=1;face.rel[2]=2;specials_load();int32_t result;
    assert(sp_0019A910(NULL,point,point,0,&result)==0);specials_store();
    assert(face.box_min[0]==7 && face.rel[2]==9);
    C.host.area_worker=camera_area_worker;
    expected_fn=0x001B0300;expected_a0=expected_a1=0;
    C.cam.rec.bytes[1]=3;C.pool[P_EYE]=0x3F800000;
    assert(lw_001B0300(NULL)==0 && C.cam.rec.bytes[1]==7 && C.cam.rec.bytes[5]==1);
    assert(C.pool[P_EYE]==0x42100000);
    specials_load();C.sps.s3600[0]=0x11111111;
    assert(sp_001B0300(NULL)==0);specials_store();
    assert(S3600[0]==0x43210000);
    expected_fn=0x00198D90;expected_a0=CAM_BASE;expected_a1=0x008102B0;
    assert(lw_00198D90(NULL,&C.cam.rec,&C.player)==0);
    assert(lw_00198D90(NULL,NULL,&C.player)<0 && callback_calls==3);
    C.fault=0;expected_fn=0x001D2830;expected_a0=3;expected_a1=1;
    assert(lw_001D2830(NULL,3,1)==0 && callback_calls==4);
    callback_result=-1;C.cam.rec.bytes[1]=3;C.cam.rec.bytes[5]=0;
    C.pool[P_EYE]=0x3F800000;S3600[0]=0x11111111;
    assert(lw_001D2830(NULL,3,1)<0 && C.fault==0x001D2830 && callback_calls==5);
    /* Callback wrote canonical bytes, then refused before publishing. The
     * failure must preserve those writes rather than reload stale g.cam. */
    assert(g.cam.sub_state==3 && g.cam.table_sel==0 && g.cam.eye[0]==1);
    assert(C.cam.rec.bytes[1]==7 && C.cam.rec.bytes[5]==1);
    assert(C.pool[P_EYE]==0x42100000 && S3600[0]==0x43210000);
    C.fault=0;C.host.area_worker=NULL;
    assert(lw_001B0300(NULL)<0 && C.fault==0x001B0300 && callback_calls==5);
    assert(em_camera_live_scratch_bind(NULL,NULL,NULL,NULL)==0);
    assert(em_aim_fire_runtime_scratch_3600_bind(NULL)==0);
    assert(!memcmp(em_camera_live_scratch_bytes(0x70003600,16),face.box_min,16));
    assert(!memcmp(em_camera_live_scratch_bytes(0x70003630,16),face.rel,16));
    assert(!memcmp(em_aim_fire_runtime_scratch_3600(),&face,64));
    assert(em_camera_live_scratch_bytes(0x70003600,16)!=(uint8_t *)face.box_min);
    assert(em_aim_fire_runtime_scratch_3600()!=(uint32_t *)(void *)face.box_min);
    assert(em_camera_live_scratch_bind(matrix,NULL,NULL,NULL)<0);
    uint32_t a0[4],b0[4],*c0=em_aim_fire_runtime_scratch_38C0();
    for(unsigned i=0;i<4;++i){a0[i]=0xAABB0000u+i;b0[i]=0xCCDD0000u+i;}
    for(unsigned i=0;i<16;++i)c0[i]=0xEEFF0000u+i;
    memset(C.scratch.w,0xCD,0x60);
    C.lworld.scratch=&C.scratch;C.cworld.scratch=&C.scratch;
    assert(em_camera_live_scratch_38_bind(a0,b0,c0)==0);
    assert(em_camera_live_scratch_38_bind(a0,b0,c0)==0);
    assert(em_camera_live_scratch_38_bind(b0,a0,c0)<0);
    assert(em_camera_live_scratch_38_bind(a0,NULL,c0)<0);
    assert(em_camera_live_scratch_bytes(0x700038A0,16)==(uint8_t *)a0);
    assert(em_camera_live_scratch_bytes(0x700038B0,16)==(uint8_t *)b0);
    assert(em_camera_live_scratch_bytes(0x700038C0,64)==(uint8_t *)c0);
    assert(em_camera_live_scratch_bytes(0x700038E0,32)==(uint8_t *)(c0+8));
    assert(em_camera_live_scratch_bytes(0x700038F0,16)==(uint8_t *)(c0+12));
    assert(!em_camera_live_scratch_bytes(0x700038AF,2));
    assert(!em_camera_live_scratch_bytes(0x700038B0,32));
    assert(!em_camera_live_scratch_bytes(0x700038F0,17));
    assert(!em_camera_live_scratch_bytes(0x700038E0,0));
    assert(!em_camera_live_scratch_bytes(0x700038E0,UINT32_MAX));
    assert(aim_map(NULL,0x700038E0,16,1)==(uint8_t *)(c0+8));
    assert(!aim_map(NULL,0x700038AF,2,1));
    follow_load();assert(!memcmp(C.fs.s38A0,a0,16) && !memcmp(C.fs.s38C0,c0,16));
    C.fs.s38A0[1]=0x12345678;C.fs.s38C0[3]=0x89ABCDEF;follow_store();
    assert(a0[1]==0x12345678 && c0[3]==0x89ABCDEF);
    specials_load();C.sps.s38A0[2]=0x98765432;specials_store();assert(a0[2]==0x98765432);
    assert(em_camleft_spad_view(C.lworld.scratch,C.lworld.scratch_aliases,0x700038F4)==c0+13);
    assert(C.lworld.scratch_aliases==C.cworld.scratch_aliases);
    assert(C.scratch.w[0]==0xCDCDCDCD); /* Inactive camera bytes never shadow the owners. */
    assert(em_camera_live_scratch_38_bind(NULL,NULL,NULL)==0);
    assert(!C.lworld.scratch_aliases && !C.cworld.scratch_aliases);
    assert(!memcmp(C.scratch.w,a0,16) && !memcmp(C.scratch.w+4,b0,16) && !memcmp(C.scratch.w+8,c0,64));
    assert(em_camera_live_scratch_bytes(0x700038A0,0x60)==(uint8_t *)C.scratch.w);
    puts("PASS scratch aliases: pose/camera/collision/aim, 38A0..38FF shared ownership, bounds, typed views, detach preservation and camera forwarding");
    return 0;
}
