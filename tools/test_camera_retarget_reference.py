#!/usr/bin/env python3
"""Compare the 0018CBD0 scalar seed with original EE instructions.

Reads the user's local original ELF; no original code or assets embedded.
Transform construction is a helper boundary: supplied rotated offsets are
returned at 001026A0. sqrt uses host sqrtf in both runs. The finite scalar
EE operations truncate toward zero; accumulator operations follow the bounded
model also used by the weather oracle. This does not validate rotation,
physical EE rounding corner cases, or camera collision styles 5/1.
"""
from __future__ import annotations
import ctypes as C
import hashlib
import json
import math
from pathlib import Path
import random
import struct
import subprocess
import sys
from test_battery_reference import signed
from test_weather_reference import bits,number,truncate

ROOT=Path(__file__).resolve().parents[1]
ELF=ROOT.parent/'Extermination/config/SCUS_971.12'
ELF_HASH='ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
LIB=C.CDLL(None)
LIB.sqrtf.argtypes=[C.c_float];LIB.sqrtf.restype=C.c_float


def oracle(elf,position,offset,distance,preset):
 mem={};r=[0]*32;fp=[0]*32;cond=False;acc=0;RET=0xbadf00d
 def get(a,n=4):
  def byte(at):
   if at in mem:return mem[at]
   if 0x100000<=at<0x275b00:return elf[at-0x100000+0x300]
   return 0
  return sum(byte(a+i)<<(8*i) for i in range(n))
 def put(a,v,n=4):
  for i in range(n):mem[a+i]=v>>(8*i)&255

 def plain(w):
  nonlocal acc,cond
  op,rs,rt,rd=w>>26,w>>21&31,w>>16&31,w>>11&31
  imm=signed(w&65535,16);a=(r[rs]+imm)&0xffffffff
  if op==0:
   fn=w&63
   if fn==0:r[rd]=r[rt]<<(w>>6&31)&0xffffffff
   elif fn in (33,45):r[rd]=(r[rs]+r[rt])&0xffffffff
   elif fn==37:r[rd]=r[rs]|r[rt]
   else:raise AssertionError(('SPECIAL',fn))
  elif op==9:r[rt]=a
  elif op==13:r[rt]=r[rs]|(w&65535)
  elif op==15:r[rt]=(w&65535)<<16
  elif op==28 and w&63==40:r[rd]=r[rs]|r[rt]
  elif op in (30,35,55):r[rt]=get(a,{30:16,35:4,55:8}[op])
  elif op in (31,43,63):put(a,r[rt],{31:16,43:4,63:8}[op])
  elif op==49:fp[rt]=get(a)
  elif op==57:
   put(a,fp[rt])
  elif op==17:
   fs,fd,fn=rd,w>>6&31,w&63
   if rs==4:fp[fs]=r[rt]&0xffffffff
   elif rs==16:
    x,y=number(fp[fs]),number(fp[rt])
    if fn==0:fp[fd]=bits(truncate(x+y))
    elif fn==1:fp[fd]=bits(truncate(x-y))
    elif fn==2:fp[fd]=bits(truncate(x*y))
    elif fn==3:fp[fd]=bits(truncate(x/y))
    elif fn==6:fp[fd]=fp[fs]
    elif fn==7:fp[fd]=fp[fs]^0x80000000
    elif fn==24:acc=truncate(x+y)
    elif fn==26:acc=truncate(x*y)
    elif fn==28:fp[fd]=bits(truncate(acc+x*y))
    elif fn==50:cond=x==y
    elif fn==52:cond=x<y
    elif fn==54:cond=x<=y
    else:raise AssertionError(('FPU',fn))
   else:raise AssertionError(('COP1',rs))
  else:raise AssertionError(('OP',op))
  r[0]=0
 for i,value in enumerate((*position,1.0)):put(0x8102B0+0xA0+i*4,bits(value))
 put(0x8101E0+0x64,bits(preset))
 r[4],r[5],r[29],r[31]=0x8101e0,0x8102b0,0x600000,RET
 fp[12]=bits(distance);pc=0x18cbd0
 for n in range(1000):
  if pc==RET:break
  assert 0x18CBD0<=pc<0x18CE54,hex(pc)
  w=get(pc);op=w>>26;rs=w>>21&31;rt=w>>16&31
  if op==3:
   target=(w&0x3ffffff)<<2;plain(get(pc+4))
   if target==0x102948:put(r[4],get(r[5],16),16)
   elif target==0x1029c0:
    for i in range(16):put(r[4]+i*4,bits(1.0 if i%5==0 else 0.0))
   elif target==0x102c58:
    assert all(number(get(r[6]+i*4))==0 for i in range(3))
   elif target==0x1026a0:
    for i,value in enumerate((*offset,1.0)):put(r[4]+i*4,bits(value))
   elif target==0x1028d0:
    for i in range(4):put(r[4]+i*4,bits(truncate(number(get(r[5]+i*4))-number(get(r[6]+i*4)))))
   elif target==0x11e748:fp[0]=bits(LIB.sqrtf(number(fp[12])))
   elif target==0x11df78:fp[0]=fp[12]&0x7fffffff
   else:raise Exception(('CALL',hex(target)))
   pc+=8
  elif op in (4,5,20,21) or (op==17 and rs==8):
   if op==17:taken=cond if rt&1 else not cond;likely=bool(rt&2)
   else:taken=(r[rs]==r[rt])==(op in (4,20));likely=op>=20
   if not likely or taken:plain(get(pc+4))
   pc=pc+4+signed(w&65535,16)*4 if taken else pc+8
  elif op==0 and w&63==8:
   target=r[rs];plain(get(pc+4));pc=target
  elif op==2:plain(get(pc+4));pc=(w&0x3ffffff)<<2
  else:plain(w);pc+=4
 else:raise Exception('loop')
 return tuple(get(0x8101E0+off+i*4) for off in (0x10,0x20) for i in range(3))


def main():
    elf=ELF.read_bytes()
    assert hashlib.sha256(elf).hexdigest()==ELF_HASH,'Wrong original executable'
    output=ROOT/'build/camera_retarget_reference';output.mkdir(parents=True,exist_ok=True)
    library=output/('seed.dylib' if sys.platform=='darwin' else 'seed.so')
    subprocess.run(['cc','-O2','-Wall','-Wextra','-Werror','-fPIC',
                    '-dynamiclib' if sys.platform=='darwin' else '-shared',
                    '-Isrc','src/game/em_camera_retarget.c','-lm','-o',str(library)],
                    cwd=ROOT,check=True)
    native=C.CDLL(str(library));vec=C.c_float*3
    native.em_camera_retarget_seed.argtypes=[C.POINTER(C.c_float),C.POINTER(C.c_float),
                C.c_float,C.c_float,C.POINTER(C.c_float),C.POINTER(C.c_float)]
    cases=[]
    for preset in (-46.8,-31.2):
        for distance in (-70,-46.8,-31.2,-10,0,10,31.2,46.8,70):
            for horizontal in (0,1,5,10,11,19,20,21,30,31.2,46.8,80):
                for position in ((0,0,0),(239.7,229.89044189453125,223.8),(-90,-28,-112)):
                    cases.append((position,(0,7,-horizontal),distance,preset))
    rng=random.Random(0x18CBD0)
    for _ in range(300):
        cases.append((tuple(rng.uniform(-500,500) for _ in range(3)),
                      tuple(rng.uniform(-60,60) for _ in range(3)),
                      rng.uniform(-80,80),rng.choice((-46.8,-31.2))))
    for index,(position,offset,distance,preset) in enumerate(cases):
        position,offset=vec(*position),vec(*offset)
        distance,preset=number(bits(distance)),number(bits(preset))
        expected=oracle(elf,position,offset,distance,preset)
        eye,target=vec(),vec()
        native.em_camera_retarget_seed(position,offset,distance,preset,eye,target)
        actual=tuple(bits(value) for value in (*eye,*target))
        assert expected==actual,(index,position[:],offset[:],distance,preset,
                                 [number(v) for v in expected],[number(v) for v in actual])
    capture=ROOT.parent/'Extermination/build/startup-reference/panel/animation_ee.bin'
    captured=False
    if capture.is_file():
        ram=capture.read_bytes()
        position=struct.unpack_from('<3f',ram,0x810350)
        distance,preset=struct.unpack_from('<f',ram,0x8101EC)[0],struct.unpack_from('<f',ram,0x810244)[0]
        assert struct.unpack_from('<3f',ram,0x810210)==(0,0,0),'Capture is not zero rotation'
        expected=oracle(elf,position,(0,0,distance),distance,preset)
        actual=tuple(struct.unpack_from('<I',ram,0x8101E0+off+i*4)[0]
                     for off in (0x10,0x20) for i in range(3))
        assert actual==expected,([number(v) for v in actual],[number(v) for v in expected])
        captured=True
    result={'cases':len(cases),'scalar_float_bit_matches':len(cases),
            'original_panel_capture_matches':captured,
            'helper_boundaries':['rotation and vector transform','host sqrtf'],
            'collision_styles_verified':False}
    (output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print('camera retarget original-slice PASS',json.dumps(result))


if __name__=='__main__':main()
