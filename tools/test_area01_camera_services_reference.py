#!/usr/bin/env python3
"""Canonical XYZ camera leaves against unhooked original instructions."""
import ctypes as C
from pathlib import Path
import random
import struct
import subprocess
from reference_mode import MODE,pick
from test_player_fall_reference import FallEE
from test_player_slide_reference import read_elf
ROOT=Path(__file__).resolve().parents[1]
BASE=0x8105D0
def F(x):return struct.unpack('<I',struct.pack('<f',x))[0]
def main():
    out=ROOT/'build/level2/camera-services';out.mkdir(parents=True,exist_ok=True)
    path=out/'bridge.dylib'
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off',
                    '-shared','-fPIC','-Isrc','tests/area01_camera_services_bridge.c',
                    'src/game/em_area01_camera_services.c','src/game/em_camera_follow_original.c',
                    'src/game/em_sdk_math_original.c','-lm','-o',str(path)],cwd=ROOT,check=True)
    n=C.CDLL(str(path));n.cvectors.restype=C.c_void_p;n.cresult.restype=C.c_uint64
    n.ccall.argtypes=[C.c_uint32]*5+[C.c_int]
    rng=random.Random(0x18C4B0);ee=FallEE(read_elf());count=0
    for i in range(pick(600,80)):
        for fn in (0x18C4B0,0x18C6A0):
            raw=struct.pack('<16I',*[F(rng.choice((-300.,-1.,-.01,0.,.01,.9,1.,40.,300.))) for _ in range(16)])
            ee.write(BASE,raw);C.memmove(n.cvectors(),raw,len(raw))
            # Exact and partial aliases use the actual single native span.
            src=BASE+rng.choice((0,4,8,16));dst=BASE+rng.choice((0,4,8,16,32))
            x=F(rng.choice((0.,.01,.2,1.,4.,18.)))
            y=F(rng.choice((0.,.01,.2,1.,4.)))
            args=(src,) if fn==0x18C4B0 else (src,dst)
            bits=(x,y) if fn==0x18C4B0 else (x,)
            ee.call(fn,args,tuple(struct.unpack('<f',struct.pack('<I',v))[0] for v in bits))
            assert n.ccall(fn,src,dst,x,y,0)==0
            assert C.string_at(n.cvectors(),64)==ee.read(BASE,64),(i,hex(fn),'memory')
            assert n.cresult()&0xFFFFFFFF==ee.r[2]&0xFFFFFFFF,(i,hex(fn),'result')
            count+=1
    raw=C.string_at(n.cvectors(),64)
    for fn in (0x18C4B0,0x18C6A0):
        for a,b,deny in ((BASE,BASE+16,1),(BASE+1,BASE+16,0),(BASE+60,BASE+16,0)):
            assert n.ccall(fn,a,b,F(.2),F(1),deny)<0
            assert C.string_at(n.cvectors(),64)==raw
    print(f'PASS {MODE}: {count} original camera XYZ calls including partial aliases; 6 refusal checks')
if __name__=='__main__':main()
