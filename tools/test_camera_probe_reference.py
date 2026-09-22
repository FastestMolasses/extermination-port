#!/usr/bin/env python3
"""Check original camera prepass and AREA11 bounds instructions.

The scalar instructions are read from the user's original ELF. Collision
responses and vector-normalize math are explicit boundaries; tests compare
state and ordered query endpoints under that bounded vector model. This is
not a proof of the existing collision walker or physical VU arithmetic.
"""
import ctypes as C
import hashlib
import itertools
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
from test_battery_reference import signed
from test_weather_reference import bits,number,truncate
ROOT=Path(__file__).resolve().parents[1]
CAM,PLAYER,RETURN=0x8101E0,0x8102B0,0xbadf00d
LIB=C.CDLL(None);LIB.sqrtf.argtypes=[C.c_float];LIB.sqrtf.restype=C.c_float
VEC=C.c_float*3
class Hit(C.Structure):
 _fields_=[('point',VEC),('normal',VEC),('delta',VEC),('kind',C.c_int),
           ('poly',C.c_int),('surf_class',C.c_uint16),('attr',C.c_uint8)]
class Probe(C.Structure):
 _fields_=[('flags',C.c_uint16),('ground78',C.c_uint8),('overhead_y',C.c_float)]
QUERY=C.CFUNCTYPE(C.c_int,C.c_void_p,C.POINTER(C.c_float),C.POINTER(C.c_float),C.c_int,C.POINTER(Hit))

def normalize(v):
 square=truncate(v[0]*v[0]);square=truncate(square+v[1]*v[1]);square=truncate(square+v[2]*v[2])
 if square==0:return v
 inverse=truncate(1.0/LIB.sqrtf(square))
 return [truncate(a*inverse) for a in v]


def oracle(elf,entry,eye,position,hip,responses,variant=2,low=-200):
 mem={};r=[0]*32;fp=[0]*32;cond=False;acc=0;calls=[]
 def get(a,n=4):
  def byte(at):
   if at in mem:return mem[at]
   if 0x100000<=at<0x275b00:return elf[at-0x100000+0x300]
   return 0
  return sum(byte(a+i)<<(8*i) for i in range(n))
 def put(a,v,n=4):
  for i in range(n):mem[a+i]=v>>(8*i)&255
 def vec(a):return [number(get(a+i*4)) for i in range(3)]
 def save(a,v):
  for i,x in enumerate(v):put(a+i*4,bits(x))
 save(CAM+0x10,eye);save(PLAYER+0xA0,position);save(PLAYER+0xB0,hip)
 put(CAM+0x5A,0x55,2);put(CAM+0x60,bits(500));put(CAM+0x6D,2,1)
 put(CAM+0x50,bits(low));put(CAM+0x54,bits(999));put(CAM+0x5C,bits(variant))
 put(0x810700,0x11,1)
 r[4],r[5],r[6],r[7],r[29],r[31]=CAM,PLAYER,5 if entry==0x18D330 else 6,6,0x600000,RETURN
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
  elif op==12:r[rt]=r[rs]&(w&65535)
  elif op==13:r[rt]=r[rs]|(w&65535)
  elif op==15:r[rt]=(w&65535)<<16
  elif op==28 and w&63==40:r[rd]=r[rs]|r[rt]
  elif op in (30,33,35,36,37,55):
   size={30:16,33:2,35:4,36:1,37:2,55:8}[op];value=get(a,size)
   r[rt]=signed(value,16)&0xffffffff if op==33 else value
  elif op in (31,40,41,43,63):put(a,r[rt],{31:16,40:1,41:2,43:4,63:8}[op])
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
 pc=entry
 for _ in range(1500):
  if pc==RETURN:break
  assert entry<=pc<(0x18D7B0 if entry==0x18D330 else 0x18DD18),hex(pc)
  w=get(pc);op=w>>26;rs=w>>21&31;rt=w>>16&31
  if op==3:
   target=(w&0x3ffffff)<<2;plain(get(pc+4))
   if target==0x102948:put(r[4],get(r[5],16),16)
   elif target in (0x1028D0,0x1028B8):
    a,b=vec(r[5]),vec(r[6]);sign=-1 if target==0x1028D0 else 1
    save(r[4],[truncate(a[i]+sign*b[i]) for i in range(3)])
   elif target==0x102760:save(r[4],normalize(vec(r[5])))
   elif target==0x103230:save(r[4],[truncate(v*number(fp[12])) for v in vec(r[5])])
   elif target in (0x19A910,0x19B7D0):
    ground=int(target==0x19B7D0)
    if not ground:assert r[6]==6
    calls.append((tuple(bits(v) for v in vec(r[4])),tuple(bits(v) for v in vec(r[5])),ground))
    result,cls,y=responses[len(calls)-1]
    r[2]=4 if result else 0
    if result:
     put(0x700031D0,0x640000);put(0x64001A,cls,2);put(0x700031B4,bits(y))
   else:raise AssertionError(('helper',hex(target)))
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
 else:raise AssertionError('Original camera probe did not return')
 return (get(CAM+0x5A,2),get(CAM+0x6D,1),get(CAM+0x60),get(CAM+0x50),get(CAM+0x54)),calls


def main():
 elf=(ROOT.parent/'Extermination/config/SCUS_971.12').read_bytes()
 assert hashlib.sha256(elf).hexdigest()=='ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
 out=ROOT/'build/camera_probe_reference';out.mkdir(parents=True,exist_ok=True)
 library=out/('probe.dylib' if sys.platform=='darwin' else 'probe.so')
 subprocess.run(['cc','-O2','-Wall','-Wextra','-Werror','-fPIC',
        '-dynamiclib' if sys.platform=='darwin' else '-shared','-Isrc',
        'src/game/em_camera_probe.c','-lm','-o',str(library)],cwd=ROOT,check=True)
 lib=C.CDLL(str(library));lib.em_camera_interaction_probe.argtypes=[C.POINTER(Probe),
        C.POINTER(C.c_float),C.POINTER(C.c_float),C.POINTER(C.c_float),QUERY,C.c_void_p]
 lib.em_camera_interaction_bounds11.argtypes=[C.POINTER(C.c_float),C.POINTER(C.c_float),
        C.POINTER(C.c_float),C.c_float,QUERY,C.c_void_p]
 positions=[((239.7,248.89044189,177),(239.7,229.89044189,223.8),(239.7,240.79,223.8)),
            ((15,30,-24),(0,0,0),(1,9,2)),((-80,-20,-75),(-90,-35,-40),(-88,-25,-40))]
 responses=[(0,0,0),(1,0x8000,40),(1,0x2000,22),(1,0x8800,400),(1,0x4000,10),(1,0xA000,44)]
 count=0;bound_count=0
 for (eye,position,hip),ground,overhead,lateral in itertools.product(positions,(0,1),responses,responses):
  eye,position,hip=VEC(*eye),VEC(*position),VEC(*hip)
  results=[(ground,0,0),overhead,lateral]
  expected,queries=oracle(elf,0x18D330,eye,position,hip,results)
  native_queries=[]
  @QUERY
  def query(_,a,b,g,out):
   native_queries.append((tuple(bits(a[i]) for i in range(3)),tuple(bits(b[i]) for i in range(3)),g))
   result,cls,y=results[len(native_queries)-1];out.contents.surf_class=cls;out.contents.point[1]=y
   return result
  probe=Probe(0x55,2,500)
  assert lib.em_camera_interaction_probe(C.byref(probe),eye,position,hip,query,None)==1
  assert (probe.flags,probe.ground78,bits(probe.overhead_y))==expected[:3],(count,'state')
  assert native_queries==queries,(count,'queries',native_queries,queries)
  count+=1
 for (eye,position,hip),floor,ceiling,variant,low in itertools.product(positions,responses,responses,(1,2),(-200,300)):
  eye,position,hip=VEC(*eye),VEC(*position),VEC(*hip);results=[floor,ceiling]
  expected,queries=oracle(elf,0x18D910,eye,position,hip,results,variant,low)
  native_queries=[]
  @QUERY
  def query(_,a,b,g,out):
   native_queries.append((tuple(bits(a[i]) for i in range(3)),tuple(bits(b[i]) for i in range(3)),g))
   result,cls,y=results[len(native_queries)-1];out.contents.surf_class=cls;out.contents.point[1]=y
   return result
  bounds=(C.c_float*2)(low,999)
  assert lib.em_camera_interaction_bounds11(bounds,eye,position,variant,query,None)==1
  assert tuple(bits(v) for v in bounds)==expected[3:],(bound_count,'bounds')
  assert native_queries==queries,(bound_count,'queries',native_queries,queries)
  bound_count+=1
 result={'prepass_cases':count,'bounds_cases':bound_count,'query_endpoints_bit_match':True,
         'boundaries':['collision replies','finite normalized-vector model'],'physical_VU_verified':False}
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
 print('camera probe original-instruction PASS',json.dumps(result))


if __name__=='__main__':main()
