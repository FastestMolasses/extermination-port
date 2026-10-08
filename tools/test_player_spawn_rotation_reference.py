#!/usr/bin/env python3
"""Spawn rotation publication, including +CC, against original 001B07C0.

Uses the existing placement oracle and the actual static spawn_commit glue.
Camera code is not substituted or edited; its player +CC input is verified.
"""
import ctypes as C
import json
import subprocess
import time
import export_area01_common as A
import test_spawn_place_reference as T

OUT=A.ROOT/'build/level2-crashes/spawn-rotation-reference'
BRIDGE=r'''
#include "game/em_scene_bindings.c"
EmGameState g;
static EmPlayerLiveActor test_player;
EmPlayerLiveActor *player_states_actor_mut(void) { return &test_player; }
void em_pickup_equipment_read(uint8_t *a,uint8_t *b,uint8_t *c) { *a=*b=*c=0; }
void em_pickup_equipment_write(uint8_t a,uint8_t b,uint8_t c) { (void)a;(void)b;(void)c; }
void psr_commit(const uint8_t *rotation,uint8_t *out) {
    EmSpawnIo io={0};
    memset(&test_player,0xA5,sizeof test_player);
    memcpy(io.player.f0C0,rotation,16);
    spawn_commit(&io);
    memcpy(out,test_player.bytes+0xC0,16);
}
'''

def main():
    start=time.monotonic();OUT.mkdir(parents=True,exist_ok=True)
    source,library=OUT/'bridge.c',OUT/'bridge.dylib';source.write_text(BRIDGE)
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off',
                    '-shared','-fPIC','-Isrc','-Wl,-dead_strip','-Wl,-undefined,dynamic_lookup',
                    '-Wl,-exported_symbol,_psr_commit',str(source),'-o',str(library)],cwd=A.ROOT,check=True)
    bridge=C.CDLL(str(library));bridge.psr_commit.argtypes=[C.c_void_p,C.c_void_p]
    image,ranges=T.load_window(T.TABLE);native=T.build_native(image)
    captures=A.captures();elf=A.read_elf();total=0
    for cap in captures:
        snap=bytearray()
        for address,size in T.LAYOUT:
            if 0x70000000<=address<0x70004000:
                snap.extend((cap.spad or bytes(0x4000))[address-0x70000000:address-0x70000000+size])
            else:snap.extend(cap.ram[address:address+size])
        T.put(snap,T.P+0x1C,0,4);T.put(snap,T.P+0x304,0,4)
        T.put(snap,0x810700,1);T.put(snap,0x810701,0)
        for entry in range(5):
            for resume in (0,1):
                for arg0 in (0,1):
                    current=bytearray(snap)
                    T.put(current,0x810702,entry);T.put(current,0x275BE0,resume)
                    config=dict(arg0=arg0,effect=0)
                    original=T.run_original(elf,ranges,current,config)
                    out,events=T.run_native(native,current,config)
                    assert out.ret==0 and not original.first_outside
                    expected=original.snapshot()[T.OFFSET[T.P+0xC0]:T.OFFSET[T.P+0xC0]+16]
                    got=bytes(out.snap)[T.OFFSET[T.P+0xC0]:T.OFFSET[T.P+0xC0]+16]
                    assert got==expected
                    result=C.create_string_buffer(16)
                    bridge.psr_commit(C.create_string_buffer(got),result)
                    assert result.raw==expected and result.raw[12:]==b'\0\0\x80\x3f'
                    total+=1
    report=dict(status='PASS',captures=len(captures),cases=total,scope='four rotation lanes from ordinary and resume placement through actual live publication',seconds=round(time.monotonic()-start,3))
    (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))

if __name__=='__main__':main()
