#!/usr/bin/env python3
"""AREA01 target +34 through the actual runtime canonical field provider.

Runs 00185A10/00185E30 against original instructions using the existing
ordered-call/store oracle. Target health reads use em_aim_fire_runtime's
pool_field; the rest of each native/original case uses the same RAM contract.
"""
import ctypes as C
import json
import struct
import subprocess
import time
import export_area01_common as A
import test_aim_fire_target_reference as T

OUT = A.ROOT / 'build/level2-crashes/aim-target-pool'
BRIDGE = r'''
#include "game/em_aim_fire_runtime.c"
static EmActorPool pool;
float *em_area11_boxes_owner_world(EmActor *a) { (void)a;return NULL; }
uint32_t em_actor_pool_address(const EmActorPool *p,const EmActor *a) {
    return a ? EM_ACTOR_POOL_BASE+(uint32_t)(a-p->records)*EM_ACTOR_RECORD_SIZE : 0;
}
void pfr_set(unsigned i,uint32_t health,uint16_t upper) {
    R.pool=&pool; pool.records[i].allocated=1;
    pool.records[i].w34=health; pool.records[i].h36=upper;
}
void *pfr_map(unsigned i,unsigned offset,unsigned size,int write) {
    return pool_field(EM_ACTOR_POOL_BASE+i*EM_ACTOR_RECORD_SIZE+offset,size,write);
}
'''


def main():
    start = time.monotonic()
    OUT.mkdir(parents=True, exist_ok=True)
    source, library = OUT / 'bridge.c', OUT / 'bridge.dylib'
    source.write_text(BRIDGE)
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off',
                    '-shared','-fPIC','-Isrc','-Wl,-dead_strip','-Wl,-undefined,dynamic_lookup',
                    '-Wl,-exported_symbol,_pfr_*',str(source),'-o',str(library)],cwd=A.ROOT,check=True)
    bridge = C.CDLL(str(library))
    bridge.pfr_set.argtypes = [C.c_uint,C.c_uint32,C.c_uint16]
    bridge.pfr_map.argtypes = [C.c_uint,C.c_uint,C.c_uint,C.c_int]
    bridge.pfr_map.restype = C.c_void_p
    bridge.pfr_set(0,0x12345678,0xABCD)
    assert C.string_at(bridge.pfr_map(0,0x34,2,0),2) == b'\x78\x56'
    assert C.string_at(bridge.pfr_map(0,0x36,2,0),2) == b'\xCD\xAB'
    for offset,size in ((0x33,2),(0x34,3),(0x34,4),(0x35,2),(0x36,3)):
        assert bridge.pfr_map(0,offset,size,0) is None, (offset,size)
    C.memmove(bridge.pfr_map(0,0x34,2,1),b'\x21\x43',2)
    assert C.string_at(bridge.pfr_map(0,0x36,2,0),2) == b'\xCD\xAB'
    T.ELF, T.LIB = A.read_elf(), T.build()
    old_prepare, old_map = T.prepare, T.MAP
    def prepare(case):
        ee = old_prepare(case)
        for i in range(8):
            health = ee.load(T.TARGET+i*0x400+0x34,2)
            bridge.pfr_set(i,0xCAFE0000|health,0xABCD)
        return ee
    def mapping(callback):
        def mapped(context,address,size,write):
            delta = address-T.TARGET
            if 0 <= delta < 8*0x400 and 0x34 <= delta%0x400 < 0x38:
                return bridge.pfr_map(delta//0x400,delta%0x400,size,write)
            return callback(context,address,size,write)
        return old_map(mapped)
    T.prepare, T.MAP = prepare, mapping
    total = events = 0
    for cap in A.captures():
        T.IMAGES[cap.name] = (cap.ram,cap.spad)
        for fn in (0x185A10,0x185E30):
            for hp in (0,1,0x7FFF,0xFFFF):
                for count in (1,8):
                    result = T.run_case(dict(fn=fn,name='canonical target halfword',
                                            capture=cap.name,hp=hp,count=count))
                    total += 1; events += result[1]
    report = dict(status='PASS',cases=total,captures=len(A.captures()),ordered_events=events,
                  refusal_spans=5,seconds=round(time.monotonic()-start,3))
    (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))

if __name__ == '__main__':
    main()
