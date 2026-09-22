#!/usr/bin/env python3
"""Original NPC bind/placement words and all bank96/Roger2 channel keys."""
import ctypes as C
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
from test_roger_reference import RogerOracle,ROOT,DECOMP,ELF_SHA,ACTOR
from test_pose_bank_reference import Bank,State
from test_pose_transition_reference import Original,A,OUT,bits

def main():
    out=ROOT/'build/roger_reference';out.mkdir(parents=True,exist_ok=True)
    bridge=out/'encounter_bridge.c'
    bridge.write_text('''#include "game/em_roger_runtime.c"
static EmPoseBank ordinary,encounter;
EmGfxMesh *em_gfx_mesh_create(EmGfx*g,const float*v,uint32_t vc,const uint32_t*i,uint32_t ic,const EmGfxTexDesc*t,uint32_t tc,const uint8_t*p,uint32_t f)
{(void)g;(void)v;(void)vc;(void)i;(void)ic;(void)t;(void)tc;(void)p;(void)f;return NULL;}
void em_gfx_mesh_destroy(EmGfx*g,EmGfxMesh*m){(void)g;(void)m;}
int em_gfx_mesh_update_positions(EmGfx*g,EmGfxMesh*m,const float*p,uint32_t c)
{(void)g;(void)m;(void)p;(void)c;return 0;}
int prepare(void) {
 return em_pose_bank_load(&ordinary,"assets/scene_snow/roger/channels.empc") &&
        em_pose_bank_load(&encounter,"assets/scene_snow/roger/encounter_roger.empc");
}
void cleanup(void){em_pose_bank_free(&ordinary);em_pose_bank_free(&encounter);}
int local_command(unsigned char *record,float *placement,unsigned *state) {
 EmRogerRuntime r={0};r.assets.animation=ordinary;r.encounter=encounter;
 r.ready=1;r.bank=0x4A;r.owner.animation_result=0xCAFE;
 const float position[3]={1,2,3},angles[3]={.1f,.7f,.2f};
 if(!em_roger_runtime_set_placement(&r,position,angles)||!em_player_pose_init(&r.pose,&r.assets.animation,8,0))return -9;
 int result=execute(&r,&r.script,record);
 memcpy(placement,r.position,12);memcpy(placement+3,r.angles,12);
 state[0]=r.bank;state[1]=r.owner.animation_result;
 state[2]=r.pose.playback.clip->id;state[3]=(unsigned)r.pose.playback.remaining;
 return result;
}
''')
    library=out/'encounter.dylib'
    sources=['em_roger_assets','em_roger','em_face_model','em_opening_face',
             'em_interaction_scan','em_pose_bank','em_pose_transition','em_player_pose','em_script']
    subprocess.run(['cc','-std=c11','-O2','-ffp-contract=off','-shared','-fPIC','-Isrc',str(bridge),
        *['src/game/'+s+'.c' for s in sources],'src/em_model.c','-o',str(library)],cwd=ROOT,check=True)
    native=C.CDLL(str(library));native.prepare.restype=C.c_int
    assert native.prepare()
    native.local_command.argtypes=[C.c_void_p,C.POINTER(C.c_float),C.POINTER(C.c_uint)]
    elf=(DECOMP/'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest()==ELF_SHA
    rng=random.Random(0x828590);cases=0
    for sub in (0,10):
        for _ in range(300):
            values=[rng.uniform(-500,500) for _ in range(8)]
            record=bytearray(struct.pack('<8I8f',1,0,sub,0,0,0,0,0,*values))
            original=RogerOracle(elf);st=0x940000;cfg=0x950000
            original.write(ACTOR+0xB0,struct.pack('<4f',1,2,3,0))
            original.write(ACTOR+0xC0,struct.pack('<4f',.1,.7,.2,0));original.write(cfg,record)
            original.run(0x1B94F0,(ACTOR,st,cfg))
            placement=(C.c_float*6)();state=(C.c_uint*4)()
            assert native.local_command((C.c_ubyte*64).from_buffer(record),placement,state)==original.r[2]
            expected=[original.load(ACTOR+0xB0+i*4) for i in range(3)]+[
                original.load(ACTOR+0xC0+i*4) for i in range(3)]
            assert [bits(v) for v in placement]==expected
            assert list(state)==[0x4A,0xCAFE,8,180]
            cases+=1
    for bank,clip in [(0x4A,i) for i in range(9)]+[(0x96,2)]:
        original=RogerOracle(elf);st=ACTOR+0x1F0;cfg=0x950000;calls=[]
        record=bytearray(struct.pack('<16I',11,0,4,0,0,clip,0,bank,*([0]*8)))
        original.write(cfg,record);original.save(st+0xE,0xCAFE,2)
        original.save(0x28A490+4*bank,0xA00000+bank*0x1000)
        original.calls[0x1C67E0]=lambda o:calls.append((o.r[4],o.r[5],o.f[12],o.f[13]))
        original.run(0x1B8020,(ACTOR,st,cfg))
        placement=(C.c_float*6)();state=(C.c_uint*4)()
        assert native.local_command((C.c_ubyte*64).from_buffer(record),placement,state)==original.r[2]==1
        assert calls==[(ACTOR,clip,0,0)]
        assert list(state)[:3]==[bank,original.load(st+0xE,2),clip]
        assert original.load(ACTOR+0x40)==0xA00000+bank*0x1000
        cases+=1
    native.cleanup()
    bank=Bank();native.em_pose_bank_load.argtypes=[C.POINTER(Bank),C.c_char_p]
    native.em_pose_bank_free.argtypes=[C.POINTER(Bank)]
    native.em_pose_playback_begin.argtypes=[C.POINTER(State),C.POINTER(Bank),C.c_uint,C.c_float]
    native.em_pose_playback_advance.argtypes=[C.POINTER(State),C.c_float,C.c_int]
    assert native.em_pose_bank_load(C.byref(bank),b'assets/scene_snow/roger/encounter_roger.empc')
    source=(DECOMP/'extract/chunk15/f12_id44.bin').read_bytes();header=0x555E0
    decoder=Original(elf);decoded=0;clip=bank.clips[0]
    for bone in range(21):
        for kind in range(3):
            table=header+struct.unpack_from('<I',source,header+8+4*kind)[0]
            record=table+struct.unpack_from('<I',source,table+4*bone)[0]
            track=clip.tracks[bone][kind]
            for index in range(track.count-1):
                raw=source[record+12*index:record+12*index+12]
                for offset,value in enumerate(raw):decoder.put(A+offset,value,1)
                decoder.run(0x1C84D0 if kind==0 else 0x1C85D0,(A,OUT))
                width=4 if kind==0 else 3
                assert [bits(track.keys[index].value[k]) for k in range(width)]==decoder.floats(OUT,width)
                flags,time=struct.unpack_from('<HH',raw,8)
                assert (track.keys[index].time,track.keys[index].hold)==(time,bool(flags&0x8000))
                decoded+=1
    state=State();assert native.em_pose_playback_begin(C.byref(state),C.byref(bank),2,0)
    for _ in range(1382):assert native.em_pose_playback_advance(C.byref(state),.5,0)
    assert state.remaining==1 and state.flags&0x1000
    native.em_pose_bank_free(C.byref(bank))
    report=dict(elf_sha256=ELF_SHA,local_command_cases=cases,decoded_encounter_keys=decoded,
        boundaries=['placement compares XYZ/Euler; unused fourth copy components not represented',
                    'NPC bind compares original initializer arguments; sampler covered separately',
                    'half-rate end smoke is structural coverage; no live encounter node snapshot'])
    (out/'encounter_reference.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'Original Roger encounter: {cases} placement/NPC-bind cases, {decoded} decoded keys PASS')

if __name__=='__main__':main()
