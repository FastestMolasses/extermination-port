#!/usr/bin/env python3
"""The live stage's feet copy/order against original 0015BCF0.

Runs the actual static helpers in em_player.c. The original stage/animation
services are observed boundaries: callbacks see retained A0 and working B0;
the later footstep sees retained A0 until the final feet publication.
"""
import ctypes as C
import json
import struct
import subprocess
import time
import export_area01_common as A
from test_player_fall_reference import FallEE

OUT = A.ROOT / 'build/level2-crashes/stage-position-reference'
PLAYER = 0x8102B0
SIZE = 0x320
BRIDGE = r'''
#include "game/em_player.c"
EmGameState g;
void psp_begin(const uint8_t *raw,const float *feet,uint8_t *out) {
    memcpy(live.a.bytes,raw,EM_PLAYER_ACTOR_SIZE);
    memcpy(g.pos,feet,12);
    stage_position_begin();
    memcpy(out,live.a.bytes,EM_PLAYER_ACTOR_SIZE);
}
void psp_finish(const uint8_t *raw,uint8_t *out) {
    memcpy(live.a.bytes,raw,EM_PLAYER_ACTOR_SIZE);
    stage_position_publish();
    memcpy(out,live.a.bytes,EM_PLAYER_ACTOR_SIZE);
}
'''

def main():
    started = time.monotonic()
    OUT.mkdir(parents=True, exist_ok=True)
    source, library = OUT / 'bridge.c', OUT / 'bridge.dylib'
    source.write_text(BRIDGE)
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off',
                    '-shared','-fPIC','-Isrc','-Wl,-dead_strip','-Wl,-undefined,dynamic_lookup',
                    '-Wl,-exported_symbol,_psp_*',str(source),'-o',str(library)],cwd=A.ROOT,check=True)
    lib = C.CDLL(str(library))
    lib.psp_begin.argtypes = [C.c_void_p,C.c_void_p,C.c_void_p]
    lib.psp_finish.argtypes = [C.c_void_p,C.c_void_p]
    elf, cases = A.read_elf(), 0
    for cap in A.captures():
        assert A.u32(cap.ram, PLAYER + 0xAC) == 0x3F800000
        for major,state in ((1,0),(1,1),(1,0x25),(2,0x16),(4,0),(5,1),(6,0)):
            for carry in ((0.,0.,0.),(31.25,-7.5,18.)):
                o = FallEE(elf,cap.ram,cap.spad)
                original = bytearray(cap.ram[PLAYER:PLAYER+SIZE])
                original[4:6] = bytes((major,state))
                original[0x31A:0x31C] = b'\0\xff'
                feet = tuple(x+y for x,y in zip(struct.unpack_from('<3f',original,0xA0),carry))
                struct.pack_into('<4f',original,0xA0,*feet,1.)
                o.write(PLAYER,original)
                raw = bytearray(original)
                raw[0xA0:0xB0] = bytes(16)  # stale mirror must never supply the scan
                out = C.create_string_buffer(SIZE)
                lib.psp_begin(C.create_string_buffer(bytes(raw)),C.create_string_buffer(struct.pack('<3f',*feet)),out)
                entered = []
                def stage(ee):
                    entered.append(ee.read(PLAYER,SIZE))
                    assert entered[-1] == out.raw, (cap.name,major,state,carry,'stage entry')
                    moved = (feet[0]+2.5,feet[1]+1.25,feet[2]-3.)
                    ee.write(PLAYER+0xB0,struct.pack('<4f',*moved,1.))
                o.hooks[0x15BA50] = stage
                observed = []
                def service(ee):
                    assert ee.read(PLAYER+0xA0,16) == struct.pack('<4f',*feet,1.)
                    observed.append(ee.read(PLAYER,SIZE))
                for fn in (0x1C6DA0,0x1C68C0,0x1C6960,0x15CF90,0x15CBA0,0x187350):
                    o.hooks[fn] = service
                o.hooks[0x11A070] = lambda ee: None
                o.save(0x275B40,PLAYER+0x110)
                o.call(0x15BCF0,(PLAYER,))
                assert len(entered)==1 and observed
                lib.psp_finish(C.create_string_buffer(observed[-1]),out)
                assert out.raw[0xA0:0xB0] == o.read(PLAYER+0xA0,16)
                assert out.raw[:0xA0] == observed[-1][:0xA0] and out.raw[0xB0:] == observed[-1][0xB0:]
                cases += 1
    report = dict(status='PASS',captures=len(A.captures()),cases=cases,
                  scope='stage entry, carried placement, locked states, callback/footstep order and final feet publication',
                  seconds=round(time.monotonic()-started,3))
    (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))

if __name__ == '__main__':
    main()
