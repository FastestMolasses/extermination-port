#!/usr/bin/env python3
"""Compare snow C with the original particle VU instruction slices.

No original program/data is embedded. The bounded VM decodes only operations
used by the owner's original 00233828 generator, including delayed MAC flags
and division. Floating results use binary32 truncation; exceptional hardware
arithmetic is outside this fixture. RANDU follows Sony VU User Manual v6.0,
sections1.1.2 and4(RINIT/RNEXT/RXOR), independently verified with captured VU
scratch particle outputs. No emulator implementation source was used.
"""
from pathlib import Path
import struct, math
ROOT=Path(__file__).resolve().parents[1]
ELF=b''
def bits(x):return struct.unpack('<I',struct.pack('<f',x))[0]
def flt(x):return struct.unpack('<f',struct.pack('<I',x&0xffffffff))[0]
ROUND='trunc'
def fp(x):
 b=bits(x);r=flt(b)
 if ROUND=='trunc' and abs(r)>abs(x):r=flt(b-1)
 return r
def signed(x,b=32):return (x&((1<<b)-1))-(1<<b) if x&(1<<(b-1)) else x&((1<<b)-1)
class VU:
 def __init__(self,dmem):
  self.mem=bytearray(dmem);self.v=[[0]*4 for _ in range(32)];self.v[0][3]=bits(1)
  self.vi=[0]*16;self.acc=[0.]*4;self.q=0.;self.r=0;self.i=0.;self.cycle=0;self.pending=[];self.mac=0;self.cf=0;self.kicks=[];self.clips=[]
 def read(self,a):return list(struct.unpack_from('<4I',self.mem,a*16))
 def write(self,a,x):struct.pack_into('<4I',self.mem,a*16,*x)
 def run(self,pc,stop):
  # The ELF stores two MPG fragments, separated by one header qword's
  # second half. Logical VU branch offsets do not include that gap.
  def instruction_index(address):return (address-0x233828-(8 if address>=0x234030 else 0))//8
  def address(index):return 0x233828+index*8+(8 if index>=256 else 0)
  branch=None
  for _ in range(100000):
   if pc==stop:return
   lo,up=struct.unpack_from('<II',ELF,pc-0x100000+0x300)
   for t,k,value in self.pending[:]:
    if t<=self.cycle:setattr(self,k,value);self.pending.remove((t,k,value))
   nextpc=branch if branch is not None else address(instruction_index(pc)+1);branch=None
   op=up&63;fs=up>>11&31;ft=up>>16&31;fd=up>>6&31;mask=up>>21&15
   if up&0x7fffffff!=0x2ff:
    x=list(map(flt,self.v[fs]));y=list(map(flt,self.v[ft]));res=[0.]*4;dest=fd;flag=True;accwrite=False;intwrite=False
    if op<0x1c:
     bc=op&3
     for c in range(4):
      if op<4:res[c]=fp(x[c]+y[bc])
      elif op<8:res[c]=fp(x[c]-y[bc])
      elif op<12:res[c]=fp(self.acc[c]+fp(x[c]*y[bc]))
      elif op<16:res[c]=fp(self.acc[c]-fp(x[c]*y[bc]))
      elif op<20:res[c]=max(x[c],y[bc])
      elif op<24:res[c]=min(x[c],y[bc])
      else:res[c]=fp(x[c]*y[bc])
     if 16<=op<24:flag=False
    elif op in (0x1c,0x1e,0x1f,0x20,0x22,0x23,0x28,0x2c):
     for c in range(4):
      if op==0x1c:res[c]=fp(x[c]*self.q)
      elif op==0x1e:res[c]=fp(x[c]*self.i)
      elif op==0x1f:res[c]=min(x[c],self.i)
      elif op==0x20:res[c]=fp(x[c]+self.q)
      elif op==0x22:res[c]=fp(x[c]+self.i)
      elif op==0x23:res[c]=fp(self.acc[c]+fp(x[c]*self.i))
      elif op==0x28:res[c]=fp(x[c]+y[c])
      elif op==0x2c:res[c]=fp(x[c]-y[c])
     if op==0x1f:flag=False
    elif op>=0x3c:
     sub=fd;bc=op&3;dest=ft
     if sub==5 and op==0x3d:
      flag=False;intwrite=True;res=[int(flt(k)*16)&0xffffffff for k in self.v[fs]]
     elif sub in (4,5):
      flag=False;intwrite=sub==5
      res=[int(flt(k))&0xffffffff if intwrite else fp(float(signed(k))) for k in self.v[fs]]
     elif sub==7 and op==0x3d:flag=False;res=list(map(abs,x))
     elif sub in (0,2,6):
      accwrite=True
      for c in range(4):
       if sub==0:res[c]=fp(x[c]+y[bc])
       elif sub==2:res[c]=fp(self.acc[c]+fp(x[c]*y[bc]))
       else:res[c]=fp(x[c]*y[bc])
     elif sub==9 and op==0x3e:accwrite=True;res=[fp(z-self.i) for z in x]
     elif sub==7 and op==0x3f:
      flag=False;mask=0;clip=0;boundary=abs(y[3])
      self.clips.append(list(self.v[fs]))
      for c in range(3):
       if x[c]>boundary:clip|=1<<(c*2)
       if x[c]<-boundary:clip|=1<<(c*2+1)
      self.pending.append((self.cycle+4,'cf',((self.cf<<6)|clip)&0xffffff))
     else:raise Exception(('upper special',hex(pc),hex(up),sub,op))
    else:raise Exception(('upper',hex(pc),hex(up)))
    mac=0
    for c in range(4):
     if mask&(8>>c):
      if accwrite:self.acc[c]=res[c]
      else:self.v[dest][c]=res[c] if intwrite else bits(res[c])
      if flag:
       if res[c]<0:mac|=0x80>>c
       if res[c]==0:mac|=8>>c
    if flag:self.pending.append((self.cycle+4,'mac',mac))
   if up>>31:self.i=flt(lo)
   elif lo!=0x8000033c and not getattr(self,'color_only',False):
    op=lo>>25;it=lo>>16&31;iss=lo>>11&31;dest=lo>>6&31;imm=signed(lo&2047,11)
    mask=lo>>21&15
    if op==0:self.v[it]=self.read((self.vi[iss]+imm)&1023)
    elif op==1:
     a=(self.vi[it]+imm)&1023;v=self.read(a)
     for c in range(4):
      if mask&(8>>c):v[c]=self.v[iss][c]
     self.write(a,v)
    elif op==4:
     v=self.read((self.vi[iss]+imm)&1023)
     for c in range(4):
      if mask&(8>>c):self.vi[it]=v[c]&65535
    elif op==5:
     a=(self.vi[iss]+imm)&1023;v=self.read(a)
     for c in range(4):
      if mask&(8>>c):v[c]=self.vi[it]
     self.write(a,v)
    elif op in (8,9):
     imm=(lo&2047)|((lo>>21&15)<<11)
     self.vi[it]=(self.vi[iss]+(imm if op==8 else -imm))&65535
    elif op==0x1a:self.vi[it]=self.mac&self.vi[iss]
    elif op in (0x20,0x21):
     if op==0x21:self.vi[it]=instruction_index(pc)+2
     branch=address(instruction_index(pc)+1+imm)
    elif op==0x24:branch=address(self.vi[iss])
    elif op in (0x28,0x29,0x2d,0x2e):
     take={0x28:self.vi[iss]==self.vi[it],0x29:self.vi[iss]!=self.vi[it],0x2d:signed(self.vi[iss],16)>0,0x2e:signed(self.vi[iss],16)<=0}[op]
     if take:branch=address(instruction_index(pc)+1+imm)
    elif op==0x12:self.vi[1]=int(bool(self.cf&(lo&0xffffff)))
    elif op==0x40:
     fn=lo&0x7ff
     if lo&63==0x34:self.vi[dest]=self.vi[iss]&self.vi[it]
     elif lo&63==0x35:self.vi[dest]=self.vi[iss]|self.vi[it]
     elif fn==0x3fc:self.vi[it]=self.v[iss][lo>>21&3]&65535
     elif fn==0x33c:
      for c in range(4):
       if mask&(8>>c):self.v[it][c]=self.v[iss][c]
     elif fn==0x3fd:
      for c in range(4):
       if mask&(8>>c):self.v[it][c]=signed(self.vi[iss],16)&0xffffffff
     elif fn==0x6fc:
      self.kicks.append(self.vi[iss])
      if getattr(self,'stop_on_kick',False):return
     elif fn==0x3bc:
      x=flt(self.v[iss][lo>>21&3]);y=flt(self.v[it][lo>>23&3]);self.pending.append((self.cycle+7,'q',fp(x/y)))
     elif fn==0x43e:self.r=self.v[iss][lo>>21&3]&0x7fffff
     elif fn==0x43f:self.r^=self.v[iss][lo>>21&3]&0x7fffff
     elif fn==0x43c:
      self.r=((self.r<<1)^((self.r>>22^self.r>>4)&1))&0x7fffff
      for c in range(4):
       if mask&(8>>c):self.v[it][c]=0x3f800000|self.r
     else:raise Exception(('lower special',hex(pc),hex(lo),hex(fn)))
    else:raise Exception(('lower',hex(pc),hex(lo),hex(op)))
   self.vi[0]=0;self.v[0]=[0,0,0,bits(1)];pc=nextpc;self.cycle+=1
  raise Exception('runaway')


import argparse, ctypes as C, random, subprocess, tempfile, json
class Particle(C.Structure):
 _fields_=[('position',C.c_float*4),('color',C.c_float*4),('half_size',C.c_float*4),('source_index',C.c_uint32)]

def main():
 global ELF
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--decomp-root',type=Path,default=ROOT.parent/'Extermination')
 p.add_argument('--reference-vu',type=Path,help='optional original VU1 data dump for independent runtime comparison')
 p.add_argument('--reference-micro',type=Path,help='matching original VU1 instruction dump; confirms the captured effect shares this generator')
 args=p.parse_args();ELF=(args.decomp_root/'config/SCUS_971.12').read_bytes()
 assert ELF[:6]==b'\x7fELF\x01\x01'
 lookup=struct.unpack_from('<80f',ELF,0x2342BC-0x100000+0x300)
 descriptor=ELF[0x255170-0x100000+0x300:0x255200-0x100000+0x300]
 cases=[];rng=random.Random(0x233828)
 for flags in range(32):
  for count in [1,2,7,20,28]:
   for phase in [-0.25,0.0,0.5,1.0,1.00001,1.99,49.0,51.0]:
    desc=bytearray(descriptor);struct.pack_into('<4I',desc,128,count,bits(0.3),flags,2)
    struct.pack_into('<4f',desc,32,43.25,100.5,32.0,128.)
    struct.pack_into('<4f',desc,48,87.5,16.0,63.25,32.)
    struct.pack_into('<4f',desc,80,0.2,0.8,0.1,0.0)
    params=[phase,rng.random(),1.e-6,rng.random()]
    matrix=[1.,0.,0.,0.,0.,0.9829571843,0.1838346422,0.,0.,-0.1838346422,0.9829571843,0.,300.,254.901535,201.627594,1.]
    cases.append((desc,params,matrix))
 with tempfile.TemporaryDirectory(prefix='em-snow-') as tmp:
  lib=Path(tmp)/'snow.dylib'
  subprocess.run(['cc','-std=c11','-O2','-ffp-contract=off','-shared','-fPIC','-I'+str(ROOT/'src'),str(ROOT/'src/game/em_snow_particles.c'),'-o',str(lib)],check=True)
  api=C.CDLL(str(lib));generate=api.em_snow_particles_generate
  F=C.POINTER(C.c_float);generate.argtypes=[F,F,F,F,C.POINTER(Particle),C.c_uint];generate.restype=C.c_int
  shade=api.em_snow_particles_color;shade.argtypes=[F,C.c_float,F,C.POINTER(C.c_uint32)]
  count_checked=0;runtime_particles=0;runtime_float_bytes=0
  def native(desc,params,matrix):
   out=(Particle*256)();d=(C.c_float*36).from_buffer_copy(desc)
   n=generate(d,(C.c_float*80)(*lookup),(C.c_float*4)(*params),(C.c_float*16)(*matrix),out,256)
   assert n>=0,n
   return out,n
  for desc,params,matrix in cases:
   mem=bytearray(16384)
   for i,x in enumerate(lookup):struct.pack_into('<f',mem,i*16,x)
   mem[0x500:0x590]=desc;struct.pack_into('<4f',mem,0x590,*params);struct.pack_into('<16f',mem,0x5a0,*matrix)
   oracle=VU(mem);oracle.run(0x233828,0x233fc0);out,n=native(desc,params,matrix)
   assert n==oracle.vi[8],('count',params,n,oracle.vi[8])
   for i in range(n):
    for field,offset in [('position',0),('color',2),('half_size',3)]:
     expected=struct.pack('<4I',*oracle.read(0x88+4*i+offset));actual=bytes(getattr(out[i],field))
     assert actual==expected,(field,count_checked,i,list(getattr(out[i],field)),struct.unpack('<4f',expected),params,struct.unpack_from('<4I',desc,128))
    count_checked+=1
  if args.reference_vu:
   assert args.reference_micro, '--reference-vu requires --reference-micro provenance'
   micro=args.reference_micro.read_bytes()
   start=0x233828-0x100000+0x300;size=0x233fa0-0x233828
   assert micro[:size]==ELF[start:start+size], 'captured VU uses a different generator'
   mem=args.reference_vu.read_bytes();assert len(mem)==16384
   assert all(mem[i*16:i*16+4]==struct.pack('<f',lookup[i]) for i in range(80))
   desc=mem[0x500:0x590];params=struct.unpack_from('<4f',mem,0x590);matrix=struct.unpack_from('<16f',mem,0x5a0)
   out,n=native(desc,params,matrix);oracle=VU(mem);oracle.run(0x233828,0x233fc0)
   while oracle.read(0x62)[0]:
    oracle.vi[9]=(0x233fc0-0x233828)//8;oracle.run(0x233910,0x233fc0)
   last=oracle.vi[8]
   for i in range(last):
    for field,offset in [('position',0),('color',2),('half_size',3)]:
     expected=mem[(0x88+4*i+offset)*16:(0x89+4*i+offset)*16]
     actual=bytes(getattr(out[n-last+i],field));vm=struct.pack('<4I',*oracle.read(0x88+4*i+offset))
     assert actual==expected==vm,('runtime',field,i,list(getattr(out[n-last+i],field)),struct.unpack('<4f',expected))
     runtime_float_bytes+=16
    runtime_particles+=1
  color_cases=0
  for depth in [0.01,1.,20.,49.999,50.,51.,100.,250.,300.,400.]:
   for _ in range(100):
    color=[rng.random()*255 for _ in range(4)]
    fog=[255.,2048.,151.11111450195312,-0.49707603454589844]
    vm=VU(bytearray(16384));vm.color_only=True
    vm.v[8][3]=bits(depth);vm.v[15][3]=bits(depth);vm.v[28]=list(map(bits,fog));vm.v[14]=list(map(bits,color));vm.q=1.
    vm.run(0x234110,0x234170)
    vm.run(0x234200,0x234208);vm.run(0x234220,0x234228);vm.run(0x234240,0x234248)
    expected=vm.v[5];actual=(C.c_uint32*4)()
    shade((C.c_float*4)(*color),depth,(C.c_float*4)(*fog),actual)
    assert list(actual)==expected,('projectioncolor',depth,list(actual),expected)
    color_cases+=1
  print(json.dumps({'status':'PASS','original_instruction_cases':len(cases),'particle_records_compared':count_checked,'original_runtime_particles':runtime_particles,'original_runtime_float_bytes_equal':runtime_float_bytes,'projected_color_cases':color_cases}))
if __name__=='__main__':main()
