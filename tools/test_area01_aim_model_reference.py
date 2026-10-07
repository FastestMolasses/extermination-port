#!/usr/bin/env python3
"""Original 00183C40 over AREA01's production canonical target model views.

Captured records/slots are test inputs. The actual runtime target_regions,
actor-view borrow and model-slot resolver expose only owned storage; the
existing 00183C40 translation runs with the original 001026A0 leaf. Compare
ordered output stores/callee entry and all RAM. No actor image is mapped.
"""
import ctypes as C
import json
import subprocess
import time
import export_area01_common as A
from test_player_fall_reference import FallEE
import test_player_slide_reference as S

OUT = A.ROOT/'build/level2-crashes/aim-model-reference'
POOL, STRIDE, SLOTS, SLOT_SIZE = 0x7A5640, 0x2F0, 0x7D5840, 0x480*0xD0
OUTPUT = 0x1E00000
BRIDGE = r'''
#include "game/em_aim_fire_runtime.c"
#include "game/em_area01_live.h"
#include "game/em_area02_math.h"
static EmArea01Live live;
static EmActorPool pool;
static uint32_t current;
static uint8_t shared[56*4], header[EM_ACTOR_RECORD_SIZE];
static int shared_mode;
static void (*trace)(uint32_t,unsigned);
void pfr_trace_store(uint32_t a,unsigned n) { if(trace)trace(a,n); }
static int model_written(void *c,EmActor *a,uint16_t o,uint16_t n)
{ (void)c;(void)a;(void)o;(void)n;return 0; }
static int project(void *c,EmActor *a,EmArea01ActorSpan *out)
{
    (void)c;
    if(!shared_mode)return 0;
    out[0]=(EmArea01ActorSpan){0x110,shared_mode==2 ? 4 : (uint16_t)(4*a->bones),shared,shared_mode==2 ? 2 : 0};
    return 1;
}
const uint8_t *em_scene_bindings_target_model_bytes(uint32_t a,uint32_t n)
{ return em_area01_live_target_model_bytes(&live,a,n); }
int pfr_setup(uint8_t *ram,uint32_t node,int share,int active)
{
    memset(&live,0,sizeof live);memset(&pool,0,sizeof pool);
    R.pool=&pool;live.bound=1;live.host.pool=&pool;current=node;
    shared_mode=0;
    unsigned index=(node-EM_ACTOR_POOL_BASE)/EM_ACTOR_RECORD_SIZE;
    EmActor *a=&pool.records[index];
    a->allocated=1;a->self=a;a->status=ram[node];a->cls=ram[node+2];a->model=ram[node+3];
    a->bones=ram[node+9];if(a->bones>56)return -1;
    memcpy(a->pos,ram+node+0xB0,16);
    em_area01_actor_view_reset(&live.actors,&pool,project,NULL,NULL);live.actors.written=model_written;
    if(em_area01_actor_view_begin(&live.actors)<0)return -2;
    if(a->bones) {
        void *p=em_area01_actor_view_bytes(&live.actors,node+0x110,4*a->bones,1);
        if(!p)return -3;memcpy(p,ram+node+0x110,4*a->bones);
    }
    if(em_area01_actor_view_commit(&live.actors)<0)return -4;
    memcpy(shared,ram+node+0x110,sizeof shared);shared_mode=share;
    live.model.actor.world.slots=ram+EM_SLG_BONE_RECORDS;
    live.model.actor.world.slots_base=EM_SLG_BONE_RECORDS;
    live.model.actor.world.slots_size=EM_SLG_BONE_SLOTS*EM_SLG_BONE_SLOT_SIZE;
    if(active && em_area01_actor_view_begin(&live.actors)<0)return -5;
    return 0;
}
const void *pfr_map(uint32_t a,uint32_t n) { return em_area01_live_target_model_bytes(&live,a,n); }
void pfr_native(unsigned mode) {
    EmActor *a=&pool.records[(current-EM_ACTOR_POOL_BASE)/EM_ACTOR_RECORD_SIZE];
    if(mode==1)a->allocated=0;
    if(mode==2)a->self=NULL;
    if(mode==3)a->bones=57;
    if(mode==4)live.bound=0;
    if(mode==5)live.model.actor.world.slots_size=0;
    if(mode==6)shared_mode=2;
}
int pfr_fault(void) { return live.fault || live.actors.fault || live.model.fault; }
int pfr_run(uint8_t *ram,EmArea02MathWorker call,void(*observe)(uint32_t,unsigned),uint32_t *fault)
{
    EmPoseRegion model[2];unsigned count=0;
    if(target_regions(NULL,current,model,2,&count)<0)return -7;
    EmActor *a=&pool.records[(current-EM_ACTOR_POOL_BASE)/EM_ACTOR_RECORD_SIZE];
    em_actor_pool_record_image(&pool,a,header);
    EmArea02MathRegion regions[5]={{current,0x14,header},{current+0xB0,16,header+0xB0},
                                 {0x1E00000,16,ram+0x1E00000}};
    for(unsigned i=0;i<count;++i)regions[3+i]=(EmArea02MathRegion){model[i].address,model[i].size,model[i].bytes};
    EmArea02Math m={regions,3+count,call,NULL,0x7F0F0000,0,0,0};
    trace=observe;int r=em_area02_math_00183C40(&m,current,0x1E00000);trace=NULL;
    *fault=m.fault_address;return r;
}
'''
U32, U64 = C.c_uint32, C.c_uint64
class Call(C.Structure):
    _fields_=[('fn',U32),('sp',U32),('a',U64*4),('f',U32*4),('na',U32),('nf',U32),('v0',U64),('f0',U32)]
CALL=C.CFUNCTYPE(C.c_int,C.c_void_p,C.POINTER(Call))
TRACE=C.CFUNCTYPE(None,U32,C.c_uint)
class Oracle(FallEE):
    def save(self,a,v,size=4):
        super().save(a,v,size)
        if hasattr(self,'events') and OUTPUT<=a<OUTPUT+16:
            self.events.append(('store',a,size,self.read(a,size)))

def main():
    started=time.monotonic();OUT.mkdir(parents=True,exist_ok=True)
    source=OUT/'bridge.c';source.write_text(BRIDGE)
    library=OUT/'bridge.dylib'
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off','-shared','-fPIC','-Isrc',
                    '-Wl,-dead_strip','-Wl,-undefined,dynamic_lookup','-Wl,-exported_symbol,_pfr_*',
                    '-DEM_AREA02_MATH_STORE_TRACE=pfr_trace_store',str(source),
                    *[f'src/game/em_{s}.c' for s in ('area01_actor_view','actor_pool','area01_model_live','area01_live','area02_math')],
                    '-o',str(library)],cwd=A.ROOT,check=True)
    lib=C.CDLL(str(library));lib.pfr_setup.argtypes=[C.c_void_p,U32,C.c_int,C.c_int]
    lib.pfr_run.argtypes=[C.c_void_p,CALL,TRACE,C.POINTER(U32)]
    lib.pfr_map.argtypes=[U32,U32];lib.pfr_map.restype=C.c_void_p
    elf=A.read_elf();captures=A.captures();cases=events=refusals=0;identities=set()
    def run(cap,node,share,active,model=None):
        nonlocal cases,events
        e=Oracle(elf,cap.ram,cap.spad);e.load_elf(elf)
        if model is not None:e.save(node+3,model,1)
        e.write(OUTPUT,bytes([0xA5])*16)
        n=FallEE(elf,e.mem,e.spad);array=(C.c_uint8*len(n.mem)).from_buffer(n.mem)
        assert lib.pfr_setup(array,node,share,active)==0
        e.events=[]
        def original_leaf(x):
            args=tuple(x.r[4:7]);e.events.append(('call',0x1026A0,x.r[29],args))
            leaf=FallEE(elf,x.mem,x.spad);leaf.call(0x1026A0,args)
            x.write(OUTPUT,leaf.read(OUTPUT,16))
        e.hooks={0x1026A0:original_leaf};e.call(0x183C40,[node,OUTPUT])
        actual=[];errors=[];pending=[]
        def flush():
            if pending:
                a,size=pending.pop();actual.append(('store',a,size,n.read(a,size)))
        def worker(_,p):
            c=p.contents
            try:
                flush()
                assert (c.fn,c.na,c.nf)==(0x1026A0,3,0)
                args=tuple(c.a[:3]);actual.append(('call',c.fn,c.sp,args))
                assert actual==e.events[:len(actual)],(actual,e.events)
                matrix=lib.pfr_map(args[1],64)
                assert matrix and C.string_at(matrix,64)==n.read(args[1],64)
                n.call(c.fn,args);c.v0=n.r[2];c.f0=n.f[0];return 0
            except Exception as ex:errors.append(ex);return -1
        def observe(a,size):
            flush();pending.append((a,size))
        callbacks=CALL(worker),TRACE(observe);fault=U32()
        status=lib.pfr_run(array,*callbacks,C.byref(fault));flush()
        if errors:raise errors[0]
        assert status==0,(cap.name,hex(node),model,share,active,hex(fault.value))
        assert actual==e.events,(actual,e.events)
        assert n.mem==e.mem,(cap.name,hex(node),model,'RAM')
        assert not lib.pfr_fault()
        cases+=1;events+=len(actual)
        return array,n
    for cap in captures:
        for node in range(POOL,POOL+256*STRIDE,STRIDE):
            if cap.ram[node]&1 and cap.ram[node+2]&0x1F==2 and A.u32(cap.ram,node+0x14)==node and 0<cap.ram[node+9]<=56:
                identities.add((node,cap.ram[node+3]))
                for share,active in ((0,0),(0,1),(1,0),(1,1)):run(cap,node,share,active)
        # Exercise every original model branch with an actual held arena.
        node=next(a for a in range(POOL,POOL+256*STRIDE,STRIDE)
                  if cap.ram[a]&1 and cap.ram[a+2]&0x1F==2 and cap.ram[a+9]>=15 and A.u32(cap.ram,a+0x14)==a)
        for model in range(20):run(cap,node,0,0,model)
    cap=captures[0];node=next(a for a,m in identities if cap.ram[a]&1 and cap.ram[a+9]>=3)
    ram=bytearray(cap.ram);array=(C.c_uint8*len(ram)).from_buffer(ram)
    for share in (0,1):
        assert lib.pfr_setup(array,node,share,0)==0
        valid=lib.pfr_map(node+0x118,4);assert valid
        for addr,size in ((node+0x10F,4),(node+0x110,0),(node+0x110+4*ram[node+9],4),(SLOTS-1,4),(SLOTS+SLOT_SIZE-2,4)):
            assert not lib.pfr_map(addr,size),(hex(addr),size)
            assert not lib.pfr_fault();refusals+=1
        for mode in range(1,7):
            assert lib.pfr_setup(array,node,share,0)==0
            lib.pfr_native(mode)
            addr=SLOTS if mode==5 else node+0x110
            assert not lib.pfr_map(addr,4),(mode,share)
            assert not lib.pfr_fault();refusals+=1
    report=dict(status='PASS',captures=len(captures),cases=cases,ordered_events=events,
                actor_model_identities=len(identities),provider_refusals=refusals,seconds=round(time.monotonic()-started,3))
    (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
