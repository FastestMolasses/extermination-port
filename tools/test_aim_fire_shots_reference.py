#!/usr/bin/env python3
"""Instruction oracle for equipment fire/impact routines. Each native write,
call-entry memory image, callee arguments/results and integer return is checked.
Original workers are scripted boundaries, not claimed translations. No emulator.
"""
import ctypes as C
from pathlib import Path
import os,random,struct,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
import reference_mode as mode
import test_player_slide_reference as shared
from test_player_fall_reference import FallEE
OUT=ROOT/'build/aim-fire/shots';SOURCE=ROOT/'src/game/em_aim_fire_shots.c'
ENTRIES=(0x1861C0,0x187CC0,0x1869A0,0x1872C0,0x1860A0,0x186A60)
SIZES=(0x7D8,0x100,0xB8,0x94,0x114,0x860)
GUN,TARGET,TARGET2,HIT,SLOT,NODE,TABLE=0x680000,0x690000,0x691000,0x692000,0x693000,0x694000,0x695000
REGIONS=((GUN,0x320),(TARGET,0x200),(TARGET2,0x200),(HIT,0x40),(SLOT,0x200),(NODE,0x100),(TABLE,0x20),
 (0x275B40,4),(0x8102B0,0x320),(0x8106E0,12),(0x81070B,1),(0x810CA4,1),
 (0x700031B0,0x40),(0x70003620,0x20),(0x700037A0,0x170),(0x70003A20,0x20))
# (integer argument count, float argument count, return category)
SIG={0x102718:(3,0,'v'),0x102738:(2,0,'f'),0x102760:(2,0,'v'),0x1028B8:(3,0,'v'),
 0x1028D0:(3,0,'v'),0x102900:(2,1,'v'),0x102948:(2,0,'v'),0x103230:(2,1,'v'),0x1031E0:(2,0,'v'),
 0x11E748:(0,1,'f'),0x1839A0:(1,0,'i'),0x183C40:(2,0,'v'),0x1860A0:(2,0,'i'),
 0x19A570:(4,0,'i'),0x1AFA90:(1,0,'i'),0x1B41F0:(6,0,'i'),0x1EFD90:(3,0,'v'),
 0x15D2F0:(0,0,'i'),0x1F4F40:(1,0,'i'),0x102958:(2,0,'v'),0x1026A0:(3,0,'v'),
 0x19B6C0:(2,0,'i'),0x1E8B90:(1,1,'v'),0x102918:(3,0,'v'),0x11DE90:(0,1,'f'),
 0x11E2A8:(0,1,'f'),0x122BB8:(0,0,'i'),0x1B1470:(0,1,'f'),0x1CD390:(2,0,'v')}
VP=C.c_void_p;U=C.c_uint32;I=C.c_int;P=C.POINTER
class Call(C.Structure):_fields_=[('a',U*6),('f',U*4),('v0',U),('f0',U)]
MAP=C.CFUNCTYPE(VP,VP,U,C.c_size_t,I);CALL=C.CFUNCTYPE(I,VP,U,P(Call))
class Bus(C.Structure):_fields_=[('context',VP),('map',MAP),('call',CALL)]
def build(src=SOURCE,tag='native'):
 OUT.mkdir(parents=True,exist_ok=True);lib=OUT/(tag+('.dylib' if sys.platform=='darwin' else '.so'))
 subprocess.run(['cc','-O2','-std=c11','-Wall','-Wextra','-Werror','-Wpedantic','-shared','-fPIC','-Isrc',str(src),'-o',str(lib)],cwd=ROOT,check=True,capture_output=True)
 native=C.CDLL(str(lib));native.em_aim_fire_shots_run.argtypes=[P(Bus),U,U,U,P(I)];native.em_aim_fire_shots_run.restype=I;return native

def F(x):return struct.unpack('<I',struct.pack('<f',x))[0]
def put(buf,o,n,v):buf[o:o+n]=(v&((1<<(8*n))-1)).to_bytes(n,'little')
def events(a,n,v):return [('write',a+i,(v>>(8*i))&255) for i in range(n)]
def snap(read):return tuple(read(a,n) for a,n in REGIONS)
def case(seed,kind=None):
 r=random.Random(seed);kind=seed%6 if kind is None else kind;data={a:bytearray(r.randbytes(n)) for a,n in REGIONS}
 def setv(a,n,v):
  for base,buf in data.items():
   if base<=a and a+n<=base+len(buf):put(buf,a-base,n,v);return
  raise AssertionError(hex(a))
 if seed%3==0:data[GUN]=bytearray(CAPTURE)
 for target in (TARGET,TARGET2):
  setv(target,1,r.choice((0,1,2)));setv(target+2,1,r.choice((2,0x22,0xE2,0,4)))
 setv(HIT+0x1A,1,r.choice((0,2,3,4,5,8,0x5A,0x5B,0x5C)));setv(HIT+0x1C,4,r.getrandbits(32))
 setv(0x275B40,4,TABLE);setv(TABLE,4,NODE)
 setv(0x8104E0,4,r.choice((12,0x29,0,1)));setv(0x810CA4,1,r.choice((0,1,2,0xFF)))
 setv(0x8102B0+0x2F0,1,r.choice((0,1,2,3)));setv(0x810525,1,r.choice((0,3,4)))
 for a in (0x8106E0,0x8106E4,0x8106E8):setv(a,4,r.choice((0,TARGET,TARGET2)))
 setv(0x700031D0,4,HIT);setv(0x700031D4,4,r.choice((0,TARGET,TARGET2)))
 return seed,kind,data

def effect(seed,index,entry,args,floats=()):
 r=random.Random(f'{seed}:{index}:{entry}')
 ret=0;fret=0;writes=[]
 if entry in (0x1AFA90,0x1F4F40):ret=r.choice((0,SLOT,SLOT))
 elif entry==0x15D2F0:ret=r.choice((0,1,2,0x82,0x83))
 elif entry==0x1839A0:ret=r.choice((0x101,0x201,0x300,0x105,0xFFFF,0x8000))
 elif entry in (0x19A570,0x19B6C0):
  ret=r.choice((0,1,2,3));writes.extend(((0x700031D0,4,HIT),(0x700031D4,4,r.choice((0,TARGET,TARGET2)))))
 elif entry in (0x1860A0,0x1B41F0):ret=r.choice((0,1))
 elif entry==0x122BB8:ret=r.getrandbits(31)
 if SIG[entry][2]=='f':fret=r.choice(tuple(F(x) for x in (0,1,4,4.5,19,20,49,50,70,80,100)))
 # Script every vector/matrix output without using the implementation under test.
 if entry in (0x102718,0x102760,0x1028B8,0x1028D0,0x102900,0x102948,0x103230,0x1031E0,0x102958,0x1026A0,0x102918,0x1CD390):
  size=64 if entry in (0x102958,0x102918,0x1CD390) else 16
  for off in range(0,size,4):writes.append((args[0]+off,4,F(r.choice((0,1,4,20,50,100)))))
 elif entry==0x183C40:
  for off in range(0,16,4):writes.append((args[1]+off,4,F(r.choice((0,1,4,20,50,100)))))
 # Observable mutations across callbacks exercise reloads and local retention.
 if r.random()<.2:writes.append((HIT+0x1A,1,r.choice((0,2,5,8,0x5A,0x5B,0x5C))))
 if r.random()<.1:writes.append((r.choice((TARGET,TARGET2)),1,r.choice((0,1,2))))
 if r.random()<.1:writes.append((0x70003A20,4,F(r.choice((1,5,20,70)))))
 if r.random()<.1:writes.append((0x70003A24,4,F(r.choice((1,5,20,70)))))
 if r.random()<.1:writes.append((0x70003A28,4,F(r.choice((1,5,20,70)))))
 if seed in (6000,6001):
  writes=[w for w in writes if w[0] not in (TARGET,TARGET2,0x70003A20,0x70003A24,0x70003A28)]
  if entry==0x19A570:ret=3 if seed==6000 else 1
  if seed==6000 and entry==0x102738:fret=F(1 if args[0]==0x700038D0 else 4.25)
  if entry==0x11E748:fret=floats[0] if seed==6000 else F(69.5)
 if seed==6002:
  if entry==0x19A570:ret=1
  if entry==0x1CD390:writes.append((0x70003A38,4,F(2)))
 if seed==6003:
  writes=[w for w in writes if w[0] not in (HIT+0x1A,TARGET)]
  if entry==0x19A570:ret=2;writes.append((0x700031D4,4,TARGET))
  if entry==0x1839A0:ret=0x101
 return ret,fret,writes
class Cut(Exception):pass
class Oracle(FallEE):
 def __init__(self,elf):
  super().__init__(elf);self.active=False;self.events=[];self.cover=set();self.hooks={e:self.hook(e) for e in SIG}
 def save(self,a,v,n=4):
  if self.active and not 0x7F000000<=a<0x7F100000:
   assert any(b<=a and a+n<=b+sz for b,sz in REGIONS),('stray',hex(a),n)
   self.events.extend(events(a,n,v))
  super().save(a,v,n)
 def write(self,a,data):
  if self.active:
   for i,v in enumerate(data):self.save(a+i,v,1)
  else:super().write(a,data)
 def branch(self,w,pc):
  x=super().branch(w,pc)
  if x is not None and any(e<=pc<e+n for e,n in zip(ENTRIES,SIZES)):self.cover.add((pc,x[0]))
  return x
 def hook(self,entry):
  def run(ee):
   ni,nf,ret=SIG[entry];args=tuple(ee.arg(i) for i in range(ni));floats=tuple(ee.f[12+i]&0xFFFFFFFF for i in range(nf))
   self.events.append(('call',entry,args,floats,snap(self.read)))
   idx=self.count;self.count+=1
   if idx==self.cut:raise Cut()
   v,f,w=effect(self.seed,idx,entry,args,floats)
   for a,n,x in w:self.save(a,x,n)
   ee.ret_int(v);ee.f[0]=f
   if ret!='v':self.events.append(('return',entry,f if ret=='f' else v))
  return run
 def run_case(self,c,cut=None):
  self.seed,kind,data=c;self.active=False;self.r=[0]*32;self.rh=[0]*32;self.f=[0]*32;self.r[28]=0x27D370;self.r[29]=shared.STACK_TOP
  for a,buf in data.items():self.write(a,buf)
  self.events=[];self.count=0;self.cut=cut;self.active=True
  # A translated entry may also be a hooked worker of another root.
  entry=ENTRIES[kind];hook=self.hooks.pop(entry,None)
  try:self.call(entry,(GUN,GUN+16));status=0
  except Cut:status=-1
  finally:
   if hook:self.hooks[entry]=hook
  self.active=False
  result=shared.s32(self.r[2]) if kind!=1 and status==0 else 0x12345
  return status,result,snap(self.read),self.events
class Native:
 def __init__(self,lib,c,cut=None):
  self.lib=lib;self.seed,self.kind,data=c;self.cut=cut;self.count=0;self.pending=None;self.events=[];self.error=None
  self.buffers={a:(C.c_uint8*len(buf)).from_buffer_copy(buf) for a,buf in data.items()}
  self.mapfn=MAP(self.map);self.callfn=CALL(self.call);self.bus=Bus(None,self.mapfn,self.callfn)
 def read(self,a,n):
  for base,buf in self.buffers.items():
   if base<=a and a+n<=base+len(buf):return bytes(buf[a-base:a-base+n])
  raise AssertionError(('unmapped',hex(a),n))
 def flush(self):
  if self.pending:
   a,n=self.pending;self.pending=None;v=int.from_bytes(self.read(a,n),'little');self.events.extend(events(a,n,v))
 def map(self,_,a,n,write):
  try:
   self.flush()
   for base,buf in self.buffers.items():
    if base<=a and a+n<=base+len(buf):
     if write:self.pending=(a,n)
     return C.addressof(buf)+a-base
   raise AssertionError(('map missing',hex(a),n,write))
  except BaseException as e:self.error=e;return None
 def write(self,a,n,v):
  for base,buf in self.buffers.items():
   if base<=a and a+n<=base+len(buf):
    for i in range(n):buf[a-base+i]=(v>>(8*i))&255
    self.events.extend(events(a,n,v));return
  raise AssertionError(('write missing',hex(a),n))
 def call(self,_,entry,frame):
  try:
   self.flush();ni,nf,ret=SIG[entry];c=frame.contents;args=tuple(c.a[:ni]);floats=tuple(c.f[:nf])
   self.events.append(('call',entry,args,floats,snap(self.read)));idx=self.count;self.count+=1
   if idx==self.cut:return -1
   v,f,w=effect(self.seed,idx,entry,args,floats)
   for a,n,x in w:self.write(a,n,x)
   c.v0=v;c.f0=f
   if ret!='v':self.events.append(('return',entry,f if ret=='f' else v))
   return 0
  except BaseException as e:self.error=e;return -1
 def run(self):
  result=I(0x12345);status=self.lib.em_aim_fire_shots_run(C.byref(self.bus),ENTRIES[self.kind],GUN,GUN+16,C.byref(result));self.flush()
  if self.error:raise AssertionError(self.error)
  return status,result.value,snap(self.read),self.events

def compare(c,lib,oracle,cut=None):
 want=oracle.run_case(c,cut);got=Native(lib,c,cut).run()
 if got!=want:
  for i,(a,b) in enumerate(zip(got[3],want[3])):
   if a!=b:
    d=(a[:4],b[:4])
    if a[0]==b[0]=='call' and a[:4]==b[:4]:d=[(hex(base+j),x,y) for (base,_),ba,bb in zip(REGIONS,a[4],b[4]) for j,(x,y) in enumerate(zip(ba,bb)) if x!=y]
    raise AssertionError(('seed/kind/cut/event',c[0],c[1],cut,i,d))
  raise AssertionError(('seed/kind/cut',c[0],c[1],cut,'status/result/events',got[:2],want[:2],len(got[3]),len(want[3])))
 return oracle.count

def init():
 global ELF,CAPTURE,LIB
 ELF=shared.read_elf();CAPTURE=(shared.DECOMP/'build/startup-reference/playable_ee.bin').read_bytes()[0x8102B0:0x8102B0+0x320];LIB=build()
 o=FallEE(ELF);targets=set()
 for e,n in zip(ENTRIES,SIZES):
  for pc in range(e,e+n,4):
   w=o.load(pc)
   if w>>26==3:targets.add((w&0x3FFFFFF)<<2)
 assert targets==set(SIG),('callee inventory',targets^set(SIG))

def batch(items):
 o=Oracle(ELF);count=0
 for c in items:count+=compare(c,LIB,o)
 return len(items),count,o.cover

def mutations(selected):
 pairs=[
  ('if(!hit){*result=0;', 'if(hit){*result=0;'),
  ('type==0x5A','type==0x59'),
  ('0x80000026,0x70003620','0x8000002C,0x70003620'),
  ('r==2 || r==0x82','r==2 || r==0x83'),
  ('B(0x810525)==3','B(0x810525)==4'),
  ('(crouch?4:1):(crouch?3:0)','(crouch?3:1):(crouch?4:0)'),
  ('W(A,0x3E4CCCCD)','W(A,0x3E4CCCCC)'),
  ('W(fx+0x10C,ONE)','W(fx+0x10C,0)'),
  ('grenade?0x18B3E0:0x18AF50','grenade?0x18AF50:0x18B3E0'),
  ('U(p+3,3)','U(p+3,2)'),
  ('grenade?0x70:0xC0','grenade?0x74:0xC0'),
  ('G(0x8104E0)==12 &&','G(0x8104E0)==0x29 &&'),
  ('type-2u<3','type-2u<2'),
  ('reaction==0x101','reaction==0x100'),
  ('reaction=0x201','reaction=0x200'),
  ('reaction==0x300','reaction==0x301'),
  ('mode==12 || mode==0x29','mode==12 || mode==0x28'),
  ('if(selection==2)','if(selection==3)'),
  ('target=G(0x8106E8)','target=G(0x8106E4)'),
  ('0x43820000','0x43800000'),
  ('CF(0x102900,N,N,0x40A00000)','CF(0x102900,N,N,0x40800000)'),
  ('actor+0xA0,A,7,0x20','actor+0xA0,A,6,0x20'),
  ('if(hit==1) {','if(hit==2) {'),
  ('(B(secondary+2)&0x1F)==2','(B(secondary+2)&0x1F)==3'),
  ('actor+0xC0,parm,0,5','actor+0xC0,parm,0,4'),
  ('else if(secondary) reaction=0x100','else reaction=0x100'),
  ('0x40900000))','0x40800000))'),
  ('if(B(secondary)) {','if(!B(secondary)) {'),
  ('H(secondary+0x36,5)','H(secondary+0x36,4)'),
  ('W(slot+0x10,0x18ABA0)','W(slot+0x10,0x18AF50)'),
  ('0x42C80000','0x42C60000'),
  ('em_ee_c_lt_bits(value,0x428C0000)','em_ee_c_lt_bits(value,0x428A0000)'),
  ('em_ee_mul_bits(0x41A00000,em_ee_div_bits','em_ee_mul_bits(0x41980000,em_ee_div_bits'),
  ('i<10','i<9'),
  ('if(em_ee_c_le_bits(0x41A00000,spread))','if(!em_ee_c_le_bits(0x41A00000,spread))'),
  ('minr=0x40800000','minr=0x40400000'),
  ('0x3F20D97C','0x3F20D97D'),
  ('?0x2A:em_ee_c_lt_bits','?0x29:em_ee_c_lt_bits'),
  ('?0x23:0x19','?0x22:0x19'),
  ('U(slot+13,i?2:1)','U(slot+13,i?1:2)'),
 ]
 assert len(pairs)==40
 source=SOURCE.read_text();oracle=Oracle(ELF);expected=[oracle.run_case(c) for c in selected]
 killed=[];survivors=[]
 for idx,(old,new) in enumerate(pairs):
  assert old in source,(idx,old)
  path=OUT/f'mutant-{idx:02d}.c';path.write_text(source.replace(old,new,1));lib=build(path,f'mutant-{idx:02d}')
  for c,want in zip(selected,expected):
   if Native(lib,c).run()!=want:killed.append((idx,c[0],old,new));break
  else:survivors.append((idx,old,new))
 import json
 (OUT/'mutation-results.json').write_text(json.dumps({'mutants':40,'killed':killed,'survivors':survivors},indent=2)+'\n')
 print(f'Mutations: {len(killed)}/40 killed; survivors {survivors}')
 assert not survivors,'Kill or explain each survivor before completion'

def cases():
 allcases=[case(i) for i in range(6000)]
 chosen=mode.select(allcases,600,97112,axes=(lambda c:c[1],))
 if not mode.FULL:
  ids={c[0] for c in chosen}
  chosen += [case(seed) for seed in (198,264,2526,2832,3150) if seed not in ids]
 for seed,kind in ((6000,0),(6001,5),(6002,5),(6003,0)):
  c=case(seed,kind)
  if seed==6000:
   put(c[2][0x8102B0],0x8104E0-0x8102B0,4,12);c[2][0x810CA4][0]=1
   put(c[2][0x8106E0],0,4,TARGET);c[2][TARGET][0]=1;c[2][TARGET][2]=2
  if seed==6003:c[2][0x810CA4][0]=0xFF;c[2][HIT][0x1A]=8;c[2][TARGET][0]=1
  chosen.append(c)
 return chosen

def main():
 start=time.monotonic();init();selected=cases()
 if '--mutations' in sys.argv:return mutations(selected)
 results=mode.parallel_map(batch,[selected[i:i+30] for i in range(0,len(selected),30)])
 count=sum(x[0] for x in results);calls=sum(x[1] for x in results);coverage=set().union(*(x[2] for x in results))
 o=Oracle(ELF);faults=0
 for c in selected[:24]:
  total=compare(c,LIB,o)
  for cut in sorted({0,total//2,total-1}):
   if cut>=0:compare(c,LIB,o,cut);faults+=1
 missing=0
 for c in selected[:6]:
  for name in ('map','call'):
   n=Native(LIB,c);before=snap(n.read)
   setattr(n.bus,name,MAP() if name=='map' else CALL())
   assert n.run()==(-1,0x12345,before,[])
   missing+=1
 sites=set()
 for entry,size in zip(ENTRIES,SIZES):
  for pc in range(entry,entry+size,4):
   w=o.load(pc)
   if w>>26 in (4,20) and (w>>21&31)==(w>>16&31)==0:continue
   if o.branch(w,pc) is not None:sites.add(pc)
 covered={(p,t) for p,t in coverage if p in sites}
 absent=sorted({(p,t) for p in sites for t in (False,True)}-covered)
 mode.banner(mode.part(count,6004,'shot cases'))
 print(f'PASS {count} cases; {calls} worker calls; {faults} failure cuts; {missing} missing-bus checks; {len(covered)}/{2*len(sites)} conditional branch outcomes; {time.monotonic()-start:.2f}s')
 assert not absent,('Missing outcomes',[(hex(p),t) for p,t in absent])
if __name__=='__main__':main()
