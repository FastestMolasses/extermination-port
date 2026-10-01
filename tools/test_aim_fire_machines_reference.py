#!/usr/bin/env python3
"""Six fire machines against original ELF instructions, with scripted callees.
Every non-stack write byte (including same-value writes), call-entry memory,
callee arguments and used results are checked in order. The originals return
void; native return is solely fail-stop status. float_to_int and copy_qw4 run
as original code. Test inputs come from the user's captured player and ELF.
EM_TEST_FULL=1 expands the fixed-seed sweep; --mutations runs one 40-mutant set.
"""
import ctypes as C
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import reference_mode as mode
import test_player_slide_reference as shared
from test_player_fall_reference import FallEE
from test_player_weapon_states_b_reference import LiveActor

OUT=ROOT/'build/aim-fire/machines'
MASK=0xFFFFFFFF
ACTOR,LINK,LINK2,NODE,TABLE=0x680000,0x690000,0x690100,0x6A0000,0x6B0000
ENTRIES=(0x170A60,0x171320,0x171670,0x171B00,0x171E90,0x1723D0)
SIZES=(0x8B4,0x344,0x484,0x388,0x53C,0x48C)
CALLS={0x1607D0:'action',0x17AF70:'steer',0x17B300:'reload',0x17B420:'reload4',
       0x1FBD50:'sound',0x1FB9F0:'empty_sound',0x11A070:'stop_sound',0x17A130:'matrix',
       0x1281C0:'to_int',0x102958:'copy'}
GLOBALS=(('aim_active',0x8106E0,4),('fire_mode',0x810C61,1),('magazine',0x810C62,1),
 ('reserve',0x810CB4,2),('ammo1',0x810CAA,2),('ammo3',0x810CA8,2),('ammo4',0x810CAE,2),
 ('ammo5',0x810CB0,2),('remote',0x810CB6,1),('pressed',0x810E74,2),
 ('fire_mask',0x70003B78,2),('remote_mask',0x70003B74,2))
REGIONS=((ACTOR,0x320),(LINK,0x40),(LINK2,0x40),(NODE,0xD0))+tuple((a,n) for _,a,n in GLOBALS)
P=C.POINTER; I=C.c_int; U=C.c_uint32; V=C.c_void_p; A=P(LiveActor)
TYPES={1:C.c_uint8,2:C.c_uint16,4:C.c_uint32}
class Scene(C.Structure):
    _fields_=[(name,P(TYPES[n])) for name,_,n in GLOBALS]
FN={'action':C.CFUNCTYPE(I,V,A,P(I)), 'steer':C.CFUNCTYPE(I,V,A),
    'reload':C.CFUNCTYPE(I,V,A,I,P(I)), 'reload4':C.CFUNCTYPE(I,V,P(I)),
    'sound':C.CFUNCTYPE(I,V,A,I,P(I)), 'empty_sound':C.CFUNCTYPE(I,V),
    'stop_sound':C.CFUNCTYPE(I,V,I),'matrix':C.CFUNCTYPE(I,V,A),
    'bone':C.CFUNCTYPE(I,V,C.c_uint,P(U)),'link20':C.CFUNCTYPE(I,V,U,P(P(C.c_uint8))),
    'to_int':C.CFUNCTYPE(I,V,U,P(C.c_int32))}
class Workers(C.Structure):
    _fields_=[('context',V),('scene',P(Scene))]+[(name,t) for name,t in FN.items()]
TRACE=C.CFUNCTYPE(None,V,U,C.c_uint)
SOURCE=ROOT/'src/game/em_aim_fire_machines.c'

def build(source=None,tag='native'):
    OUT.mkdir(parents=True,exist_ok=True)
    shim=OUT/'observe.c'
    shim.write_text('''#include <stdint.h>
void (*em_af_observer)(const void *,uint32_t,unsigned);
void em_aim_fire_observe(const void *p,uint32_t v,unsigned n) { if(em_af_observer) em_af_observer(p,v,n); }
''')
    lib=OUT/(tag+('.dylib' if sys.platform=='darwin' else '.so'))
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wpedantic',
      '-shared','-fPIC','-DEM_AIM_FIRE_OBSERVE','-Isrc',str(source or SOURCE),str(shim),'-o',str(lib)],
      cwd=ROOT,check=True,capture_output=True)
    x=C.CDLL(str(lib))
    for i,e in enumerate(ENTRIES):
        f=getattr(x,f'em_aim_fire_{e:08X}'); f.argtypes=[P(Workers),A]+([I] if i==0 else []);f.restype=I
    return x

def fbits(f): return struct.unpack('<I',struct.pack('<f',f))[0]
def put(buf,o,n,v): buf[o:o+n]=(v&((1<<(8*n))-1)).to_bytes(n,'little')
def effects(seed,count,name):
    rng=random.Random(f'{seed}:{count}:{name}')
    result=rng.choice((0,0,1,7,-1)) if name in ('action','reload','reload4') else rng.choice((0,9,-1,0x17F))
    writes=[]
    if name in ('action','matrix','reload','reload4','steer','sound','stop_sound','empty_sound'):
        for off,sz,vals in ((6,1,(1,2,3,255)),(7,1,(0,1,2,10,11,12,13,21,22,23,30,31,32)),
          (0x1F0,1,(0x31,0x32,0x33,0x34,0x35)),(0x274,1,(0,1)),(0x275,1,(0,1,4,5)),
          (0x2F2,1,(0,1)),(0x20,4,(LINK,LINK2)),(0x28,2,(0,1,2,3,5,6,0xFFFF)),
          (0x276,2,(0,1,8,10,12,30,0x7FFF)),(0x2F4,4,(fbits(6),fbits(12),fbits(30)))):
            if rng.random()<.12: writes.append((ACTOR+off,sz,rng.choice(vals)))
        if rng.random()<.2:
            _,addr,sz=rng.choice(GLOBALS[1:]); writes.append((addr,sz,rng.choice((0,1,2,0xFFFF))))
        if name=='matrix': writes.append((NODE+0x90+4*rng.randrange(16),4,rng.getrandbits(32)))
    return result,writes

def case(seed,kind=None,st=None):
    r=random.Random(seed); kind=r.randrange(6) if kind is None else kind
    actor=bytearray(CAPTURE if seed%3==0 else r.randbytes(0x320))
    states=((0,1,10,11,20,21,22,23,30,31,32,255),(0,10,11,1,255),
      (0,1,2,10,11,12,255),(0,1,10,11,12,255),(0,1,2,10,11,12,255),(0,10,11,12,13,255))
    put(actor,7,1,r.choice(states[kind]) if st is None else st)
    put(actor,0x20,4,LINK)
    for o,v in ((0x1F0,r.choice((0x31,0x32,0x33,0x34,0x35))),
      (0x274,r.randrange(2)),(0x275,r.randrange(6)),(0x2F2,r.randrange(2)),
      (0x31B,r.choice((0xFF,0,1,0x80,0xFE))),(0x31A,r.randrange(2))):put(actor,o,1,v)
    for o,v in ((0x28,r.choice((0,1,2,3,4,5,6,7,0xFFFF,0x7FFF,0x8000))),
      (0x276,r.choice((0,1,4,8,9,10,11,12,14,24,30,0xFFFF,0x7FFF,0x8000))),
      (0x2A,r.randrange(2)),(0x31C,r.choice((0x5DD,0,0x5DC)))):put(actor,o,2,v)
    put(actor,0x2F4,4,r.choice((fbits(0),fbits(6),fbits(12),fbits(12)-1,fbits(30),fbits(-1),0x7FC00000,0x7F800000)))
    data={ACTOR:actor,LINK:bytearray(r.randbytes(0x40)),LINK2:bytearray(r.randbytes(0x40)),NODE:bytearray(r.randbytes(0xD0))}
    for name,addr,sz in GLOBALS:
        val=r.choice((0,1,2,3,0xFF))
        if name in ('pressed','fire_mask','remote_mask'):val=r.choice((0,0x20,0x200,0x220,0xFFFF))
        data[addr]=bytearray((val&((1<<(sz*8))-1)).to_bytes(sz,'little'))
    return seed,kind,r.randrange(2),data

def snapshot(read):return tuple(read(a,n) for a,n in REGIONS)
def write_events(a,n,v):return [('write',a+i,(v>>(8*i))&255) for i in range(n)]
class Oracle(FallEE):
    def __init__(self,elf):
        super().__init__(elf);self.events=[];self.cover=set();self.active=False;self.cut=None
        self.hooks={a:self.hook(n) for a,n in CALLS.items()}
    def branch(self,w,pc):
        x=super().branch(w,pc)
        if x is not None and any(a<=pc<a+n for a,n in zip(ENTRIES,SIZES)): self.cover.add((pc,x[0]))
        return x
    def save(self,a,v,n=4):
        if self.active and not 0x7F000000<=a<0x7F100000:
            assert any(b<=a and a+n<=b+s for b,s in REGIONS),('stray write',hex(a),n)
            self.events.extend(write_events(a,n,v))
        super().save(a,v,n)
    def hook(self,name):
        def run(ee):
            if name=='to_int':args=(ee.f[12]&MASK,)
            elif name=='copy':args=(ee.arg(0),ee.arg(1))
            elif name=='reload':args=(ee.arg(0),shared.s32(ee.arg(1)))
            elif name=='sound':args=(ee.arg(0),shared.s32(ee.arg(1)),ee.arg(2),ee.f[12]&MASK)
            elif name=='stop_sound':args=(shared.s32(ee.arg(0)),)
            elif name=='empty_sound':args=tuple(ee.arg(i) for i in range(4))
            elif name in ('action','matrix','steer'):args=(ee.arg(0),)
            else:args=()
            self.events.append(('call',name,args,snapshot(self.read)))
            if name=='copy':
                saved=self.hooks.pop(0x102958)
                try:self.nested(0x102958,args)
                finally:self.hooks[0x102958]=saved
                return
            if name=='to_int':
                saved=self.hooks.pop(0x1281C0)
                try: result,_=self.nested(0x1281C0,(),(shared.number(args[0]),))
                finally:self.hooks[0x1281C0]=saved
                self.ret_int(result); self.events.append(('return',name,shared.s32(result)))
                return
            idx=self.calls;self.calls+=1
            if idx==self.cut:raise Cut()
            result,writes=effects(self.seed,idx,name)
            for a,n,v in writes:self.save(a,v,n)
            self.ret_int(result)
            if name in ('action','reload','reload4','sound'):self.events.append(('return',name,result))
        return run
    def run_case(self,c,cut=None):
        self.seed,kind,arg,data=c;self.active=False
        self.r=[0]*32;self.rh=[0]*32;self.f=[0]*32;self.r[28]=0x27D370;self.r[29]=shared.STACK_TOP
        for a,buf in data.items():self.write(a,buf)
        self.save(0x275B40,TABLE);self.save(TABLE+16,NODE)
        self.events=[];self.calls=0;self.cut=cut;self.active=True
        try:self.call(ENTRIES[kind],(ACTOR,arg));status=0
        except Cut:status=-1
        self.active=False
        return status,snapshot(self.read),self.events
    def write(self,a,data):
        if self.active:
            for i,v in enumerate(data):self.save(a+i,v,1)
        else:super().write(a,data)
class Cut(Exception):pass

class Native:
    def __init__(self,lib,c,cut=None,missing=None):
        self.lib=lib;self.seed,self.kind,self.arg,data=c;self.cut=cut;self.count=0;self.events=[];self.error=None
        self.actor=LiveActor();C.memmove(self.actor.bytes,bytes(data[ACTOR]),0x320)
        self.buffers={ACTOR:self.actor.bytes}
        for a,buf in data.items():
            if a!=ACTOR:self.buffers[a]=(C.c_uint8*len(buf)).from_buffer_copy(buf)
        self.ranges=[(C.addressof(buf),len(buf),a) for a,buf in self.buffers.items()]
        scene_args=[C.cast(self.buffers[a],P(TYPES[n])) if name!=missing else None for name,a,n in GLOBALS]
        self.scene=Scene(*scene_args)
        self.keep=[FN[name](self.worker(name)) if name!=missing else FN[name]() for name in FN]
        self.workers=Workers(None,C.pointer(self.scene) if missing!='scene' else None,*self.keep)
        self.observe=TRACE(self.trace); C.c_void_p.in_dll(lib,'em_af_observer').value=C.cast(self.observe,V).value
    def read(self,a,n):
        for base,buf in self.buffers.items():
            if base<=a and a+n<=base+len(buf):return bytes(buf[a-base:a-base+n])
        raise AssertionError(('native unmapped',hex(a),n))
    def trace(self,p,v,n):
        for base,size,original in self.ranges:
            if base<=p and p+n<=base+size:self.events.extend(write_events(original+p-base,n,v));return
        self.error=('native stray write',p,n)
    def write(self,a,n,v):
        for base,buf in self.buffers.items():
            if base<=a and a+n<=base+len(buf):
                for i in range(n):buf[a-base+i]=(v>>(8*i))&255
                self.events.extend(write_events(a,n,v));return
        raise AssertionError(('unmapped write',hex(a)))
    def worker(self,name):
        def run(*args):
            try:return self.work(name,*args)
            except BaseException as e:self.error=e;return -1
        return run
    def work(self,name,*args):
        # The +0x2E event halfword of the linked record (em_aim_fire_machines.h).
        if name=='link20':args[2][0]=C.cast(C.c_void_p(C.addressof(self.buffers[args[1]])+0x2E),P(C.c_uint8));return 0
        if name=='bone':
            assert args[1]==4
            if (self.actor.bytes[0x1F0] in (0x32,0x35) or self.actor.bytes[0x275]==4 or self.actor.bytes[0x2F2]):
                self.events.append(('call','copy',(ACTOR+0x2A0,NODE+0x90),snapshot(self.read)))
            for i in range(16):args[2][i]=int.from_bytes(self.read(NODE+0x90+i*4,4),'little')
            return 0
        if name=='to_int':actual=(args[1],)
        elif name=='reload':actual=(ACTOR,args[2])
        elif name=='sound':actual=(ACTOR,args[2],0,fbits(300))
        elif name=='stop_sound':actual=(args[1],)
        elif name=='empty_sound':actual=(0x169,0x1000,0x1000,0x1000)
        elif name in ('action','matrix','steer'):actual=(ACTOR,)
        else:actual=()
        self.events.append(('call',name,actual,snapshot(self.read)))
        if name=='to_int':
            # Numeric result is obtained from the original helper; only machine use is under test.
            value=conversion(actual[0]);args[2][0]=value;self.events.append(('return',name,value));return 0
        idx=self.count;self.count+=1
        if idx==self.cut:return -1
        result,writes=effects(self.seed,idx,name)
        for a,n,v in writes:self.write(a,n,v)
        if name in ('action','reload','reload4','sound'):
            args[-1][0]=result;self.events.append(('return',name,result))
        return 0
    def run(self):
        args=[C.byref(self.workers),C.byref(self.actor)]+([self.arg] if self.kind==0 else [])
        result=getattr(self.lib,f'em_aim_fire_{ENTRIES[self.kind]:08X}')(*args)
        if self.error:raise AssertionError(self.error)
        return result,snapshot(self.read),self.events

def compare(c,lib,oracle,cut=None):
    expected=oracle.run_case(c,cut)
    actual=Native(lib,c,cut).run()
    if actual!=expected:
        for i,(a,b) in enumerate(zip(actual[2],expected[2])):
            if a!=b:
                details=(a[:3],b[:3])
                if a[0]==b[0]=='call' and a[:3]==b[:3]:
                    details=[(hex(base+j),va,vb) for (base,_),xa,xb in zip(REGIONS,a[3],b[3]) for j,(va,vb) in enumerate(zip(xa,xb)) if va!=vb]
                raise AssertionError(('seed/kind/state/cut',c[0],c[1],c[3][ACTOR][7],cut,'event',i,details))
        raise AssertionError(('seed/kind/state/cut',c[0],c[1],c[3][ACTOR][7],cut,'status/count',actual[0],expected[0],len(actual[2]),len(expected[2])))
    return oracle.calls

def batch(items):
    o=Oracle(ELF);calls=0
    for c in items:calls+=compare(c,LIB,o)
    return len(items),calls,o.cover

def conversion(bits):
    if bits not in CONVERT:
        helper=FallEE(ELF);helper.r[28]=0x27D370;helper.r[29]=shared.STACK_TOP
        helper.call(0x1281C0,(),(shared.number(bits),));CONVERT[bits]=shared.s32(helper.r[2])
    return CONVERT[bits]

def init():
    global ELF,CAPTURE,CONVERT,LIB
    ELF=shared.read_elf();CAPTURE=(shared.DECOMP/'build/startup-reference/playable_ee.bin').read_bytes()[0x8102B0:0x8102B0+0x320]
    LIB=build(); CONVERT={}
    # Every float input in generated cases and scripted callbacks.
    values={fbits(x) for x in (0,6,12,30,-1)}|{fbits(12)-1,0x7FC00000,0x7F800000}
    o=FallEE(ELF)
    for v in values:
        o.r[28]=0x27D370;o.r[29]=shared.STACK_TOP;o.call(0x1281C0,(),(shared.number(v),));CONVERT[v]=shared.s32(o.r[2])
    targets=set()
    for entry,size in zip(ENTRIES,SIZES):
        for pc in range(entry,entry+size,4):
            w=o.load(pc)
            if w>>26==3:targets.add((w&0x3FFFFFF)<<2)
    assert targets==set(CALLS),('callee inventory',targets^set(CALLS))
    return LIB

def cases():
    allcases=[case(i) for i in range(18000)]
    chosen=mode.select(allcases,1800,701,axes=(lambda c:c[1],lambda c:(c[1],c[3][ACTOR][7])))
    # Keep every original conditional branch outcome in quick mode.
    if not mode.FULL:
        ids={c[0] for c in chosen}
        chosen += [case(seed) for seed in (173,466,530,565,1077,1378,1705,1810,2096,2156,4052,7042,8155,15009) if seed not in ids]
    for seed in range(18000,18032):
        c=case(seed,0,0)
        c[3][ACTOR][0x274]=1
        c[3][0x810C62][0]=0
        c[3][0x810C61][0]=0
        chosen.append(c)
    # The two mutation survivors exposed exact cadence and short-burst boundaries.
    for st in (11,22):
        c=case(18032,0,st);a=c[3][ACTOR]
        put(a,0x276,2,2 if st==11 else 10);put(a,0x2F4,4,fbits(12))
        put(a,0x28,2,1);put(a,0x2A,2,0)
        c[3][0x810C62][0]=3 if st==11 else 0
        put(c[3][0x810E74],0,2,0x20);put(c[3][0x70003B78],0,2,0x20)
        chosen.append(c)
    return chosen

def mutations(selected):
    """One fixed, bounded mutation set; each mutant differs at exactly one site."""
    pairs=[
      ('!arg && *s->aim_active','arg && *s->aim_active'),
      ('ph(a,0x2E,1); TRY(w->action','ph(a,0x2E,0); TRY(w->action'),
      ('pb(a,0x2F2,1);','pb(a,0x2F2,0);'),
      ('if (b(a,0x274)) {','if (!b(a,0x274)) {'),
      ('ph(a,0x2E,0);','ph(a,0x2E,1);'),
      ('!*s->fire_mode?10:*s->fire_mode==1?20:30','!*s->fire_mode?20:*s->fire_mode==1?10:30'),
      ('sound(w,a,0x169)','sound(w,a,0x168)'),
      ('*s->pressed & 0x200','*s->pressed & 0x100'),
      ('a,2,&r)); if (!r) reload_state','a,1,&r)); if (!r) reload_state'),
      ('st==23 &&','st==32 &&'),
      ('ph(a,0x2A,0);','ph(a,0x2A,1);'),
      ('h(a,0x276)+2','h(a,0x276)+1'),
      ('(uint32_t)limit-8','(uint32_t)limit-7'),
      ('*s->pressed & *s->fire_mask','*s->pressed | *s->fire_mask'),
      ('if (count>=limit)','if (count>limit)'),
      ('if (!*s->magazine)','if (*s->magazine)'),
      ('pb(a,0x274,1); pb(a,7,b(a,7)-1)','pb(a,0x274,1); pb(a,7,b(a,7)+1)'),
      ('if (h(a,0x28)<2)','if (h(a,0x28)<3)'),
      ('pw(a,0x2F4,0x41400000)','pw(a,0x2F4,0x41000000)'),
      ('if (h(a,0x28)>=3)','if (h(a,0x28)>3)'),
      ('if (h(a,0x28)<3)','if (h(a,0x28)<2)'),
      ('*w->scene->magazine-1','*w->scene->magazine+1'),
      ('*w->scene->reserve-1','*w->scene->reserve+1'),
      ('? 0x164 : 0x165','? 0x165 : 0x164'),
      ('b(a,0x1F0)==0x33;','b(a,0x1F0)==0x32;'),
      ('b(a,0x275)==4','b(a,0x275)==5'),
      ('if (kind==2) pb(a,7,b(a,7)+1);','if (kind==3) pb(a,7,b(a,7)+1);'),
      ('if (r) pb(a,7,st==2?0:1);','if (!r) pb(a,7,st==2?0:1);'),
      ('h(a,0x276)+1','h(a,0x276)+2'),
      ('if (h(a,0x28)>=6)','if (h(a,0x28)>6)'),
      ('ph(a,0x28,4);','ph(a,0x28,3);'),
      ('if (!count) {','if (count) {'),
      ('h(a,0x28)%3','h(a,0x28)%2'),
      ('pb(a,0x2F1,0xFF)','pb(a,0x2F1,0)'),
      ('int handle=(int8_t)b(a,0x31B)','int handle=(uint8_t)b(a,0x31B)'),
      ('em_live_u16(a,0x31C)==0x5DD','em_live_u16(a,0x31C)==0x5DC'),
      ('*s->remote==1 &&','*s->remote==2 &&'),
      ('byte(s->remote,2)','byte(s->remote,1)'),
      ('h(a,0x276)>=11','h(a,0x276)>=10'),
      ('half(s->ammo5,*s->ammo5-1)','half(s->ammo5,*s->ammo5+1)'),
    ]
    assert len(pairs)==40
    source=SOURCE.read_text();oracle=Oracle(ELF)
    expected=[oracle.run_case(c) for c in selected]
    survivors=[];killed=[]
    for idx,(old,new) in enumerate(pairs):
        assert old in source,(idx,old)
        path=OUT/f'mutant-{idx:02d}.c';path.write_text(source.replace(old,new,1))
        lib=build(path,f'mutant-{idx:02d}')
        for c,want in zip(selected,expected):
            if Native(lib,c).run()!=want:
                killed.append((idx,c[0],old,new));break
        else:survivors.append((idx,old,new))
    receipt={'mutants':40,'killed':killed,'survivors':survivors}
    import json
    (OUT/'mutation-results.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(f'Mutations: {len(killed)}/40 killed; survivors {survivors}')
    assert not survivors,'Each survivor must be killed or explained before completion'

def main():
    start=time.monotonic();init();selected=cases()
    if '--mutations' in sys.argv:return mutations(selected)
    results=mode.parallel_map(batch,[selected[i:i+100] for i in range(0,len(selected),100)])
    count=sum(x[0] for x in results);calls=sum(x[1] for x in results);coverage=set().union(*(x[2] for x in results))
    o=Oracle(ELF);faults=0
    for c in selected[:90]:
        for cut in range(compare(c,LIB,o)):
            compare(c,LIB,o,cut);faults+=1
    missing=0
    for kind in range(6):
        c=case(27,kind,10)
        needs=(('aim_active','fire_mode','magazine','reserve','pressed','fire_mask','steer','reload','empty_sound','to_int'),
          ('ammo1','to_int'),('ammo1','to_int'),('ammo3','to_int'),('ammo4','reload4','stop_sound'),
          ('ammo5','remote','pressed','remote_mask','to_int'))
        for field in ('scene','action','sound','matrix','bone','link20')+needs[kind]:
            n=Native(LIB,c,missing=field);before=snapshot(n.read);got=n.run()
            assert got==(-1,before,[]),(kind,field);missing+=1
    branchsites=set()
    for entry,size in zip(ENTRIES,SIZES):
        for pc in range(entry,entry+size,4):
            word=o.load(pc);op=word>>26;rs=word>>21&31;rt=word>>16&31
            if op in (4,20) and rs==rt==0:continue
            if o.branch(word,pc) is not None:branchsites.add(pc)
    covered={(p,t) for p,t in coverage if p in branchsites}
    missing_outcomes=sorted({(p,t) for p in branchsites for t in (False,True)}-covered)
    mode.banner(mode.part(count,18034,'machine cases'))
    print(f'PASS {count} cases; {calls} worker calls; {faults} fault cuts; {missing} missing-worker checks; {len(covered)}/{2*len(branchsites)} branch outcomes; {time.monotonic()-start:.2f}s')
    assert not missing_outcomes,('uncovered branch outcomes',[(hex(p),t) for p,t in missing_outcomes])

if __name__=='__main__':main()
