/* Test-only: project captured fields into the existing typed status owner.
 * Production receives these channels from its animation evaluator. */
#include "game/em_status_models.c"

static uint32_t sp69_word(const uint8_t *p)
{ uint32_t w;memcpy(&w,p,4);return w; }
int sp69_call(uint8_t *ram,uint8_t *scratch,uint32_t actor,int public_helper)
{
    unsigned count=ram[actor+0xC];
    if(count>EM_OWNER_SERVICES_MAX_BONES || count>EM_POSE_NODE_MAX)return -1;
    EmStatusModels *m=calloc(1,sizeof *m);if(!m)return -1;
    EmStatusSceneActor *a=&m->pool.record[0];
    memcpy(a,ram+actor,sizeof *a);m->bank.bone_count=(uint16_t)count;m->pose_valid[0]=1;
    memcpy(m->spr.s3400,scratch+0x3400,64);memcpy(m->spr.s3440,scratch+0x3440,64);
    memcpy(m->pose_rest,scratch+0x3480,64);memcpy(m->pose_quat,scratch+0x3600,16);
    memcpy(m->pose_products,scratch+0x3760,44);
    EmStatusModelsNode nodes[EM_OWNER_SERVICES_MAX_BONES];
    float world[EM_OWNER_SERVICES_MAX_BONES][16];
    for(unsigned i=0;i<count;++i) {
        uint32_t at=sp69_word(ram+actor+0x110+4*i);if(at>0x2000000u-0xD0u){free(m);return -1;}
        const uint8_t *raw=ram+at;EmOwnerBone *bone=&m->slot[i];
        EmPoseChannels *channels=&m->pose[0].channels[i];
        memcpy(channels->translation,raw,12);memcpy(channels->scale,raw+0x18,12);
        uint32_t left[4],right[4],blended[4];memcpy(left,raw+0x30,16);memcpy(right,raw+0x40,16);
        em_pose_host_001CA0A0(blended,left,right,sp69_word(raw+0x50));
        memcpy(channels->rotation,blended,16);
        memcpy(&bone->parent,raw+0x64,2);memcpy(bone->rot,raw+0x70,12);
        memcpy(bone->trans,raw+0x7C,12);memcpy(bone->scale,raw+0x88,6);
        memcpy(bone->world,raw+0x90,64);a->w110[i]=SLOT_BASE+SLOT_SIZE*i;m->slot_used[i]=1;
        memcpy(nodes[i].translation,channels->translation,12);memcpy(nodes[i].scale,channels->scale,12);
        memcpy(nodes[i].rotation,channels->rotation,16);nodes[i].parent=bone->parent;
        memcpy(nodes[i].rest_rot,bone->rot,12);memcpy(nodes[i].rest_trans,bone->trans,12);
        memcpy(nodes[i].rest_scale,bone->scale,6);
    }
    int rc=public_helper ? em_status_models_pose_001C69A0(m->spr.s3400,a->f60,nodes,count,world) :
                           w_001C69A0(m,a);
    if(rc==0)for(unsigned i=0;i<count;++i) {
        uint32_t at=sp69_word(ram+actor+0x110+4*i);
        memcpy(ram+at+0x90,public_helper ? world[i] : m->slot[i].world,64);
    }
    if(!public_helper) {
        memcpy(scratch+0x3400,m->spr.s3400,64);memcpy(scratch+0x3440,m->spr.s3440,64);
        memcpy(scratch+0x3480,m->pose_rest,64);memcpy(scratch+0x3600,m->pose_quat,16);
        memcpy(scratch+0x3760,m->pose_products,44);
    }
    free(m);return rc;
}

int sp69_contract(void)
{
    float object[16],scale[4]={1,1,1,1},world[1][16];
    EmStatusModelsNode node={0};
    if(em_owner_services_identity_001029C0(object)<0)return -1;
    node.rotation[3]=1;node.scale[0]=node.scale[1]=node.scale[2]=1;
    node.rest_scale[0]=node.rest_scale[1]=node.rest_scale[2]=4096;
    for(unsigned kind=0;kind<3;++kind) {
        memset(world,0xA5,sizeof world);uint8_t before[sizeof world];memcpy(before,world,sizeof world);
        node.parent=kind==0 ? -2 : 1;
        unsigned count=kind==2 ? EM_OWNER_SERVICES_MAX_BONES+1u : 1u;
        if(em_status_models_pose_001C69A0(object,scale,&node,count,world)>=0 ||
           memcmp(world,before,sizeof world))return -1;
    }
    return 0;
}
