#!/usr/bin/env python3
"""vu1_vm.py - a bounded VU1 machine over the ORIGINAL microcode words.

Written for the snow generator's reference test (the particle program of
D_00233800, docs/SNOW_PARTICLES.md), which the chain page's snow program
translation and its reference test (tools/test_chain_page_reference.py)
superseded; it stays the shared VU1 machine of the shadow, actor-lighting,
AREA11 fog and opening-lighting tools. It decodes the operations those
programs use, including delayed MAC flags and division. Floating results use
binary32 truncation; exceptional hardware arithmetic is outside it. RANDU
follows Sony VU User Manual v6.0, sections 1.1.2 and 4 (RINIT/RNEXT/RXOR).
No original program or data is embedded; no emulator source was used.
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
 def __init__(self,dmem,program_start=0x233828):
  self.program_start=program_start
  self.mem=bytearray(dmem);self.v=[[0]*4 for _ in range(32)];self.v[0][3]=bits(1)
  self.vi=[0]*16;self.acc=[0.]*4;self.q=0.;self.r=0;self.i=0.;self.cycle=0;self.pending=[];self.mac=0;self.cf=0;self.kicks=[];self.clips=[]
 def read(self,a):return list(struct.unpack_from('<4I',self.mem,a*16))
 def write(self,a,x):struct.pack_into('<4I',self.mem,a*16,*x)
 def run(self,pc,stop):
  # The ELF stores two MPG fragments, separated by one header qword's
  # second half. Logical VU branch offsets do not include that gap.
  def instruction_index(address):return (address-self.program_start-(8 if address>=self.program_start+0x808 else 0))//8
  def address(index):return self.program_start+index*8+(8 if index>=256 else 0)
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
