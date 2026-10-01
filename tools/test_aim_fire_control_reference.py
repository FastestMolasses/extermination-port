#!/usr/bin/env python3
"""Original-instruction proof for the aim/fire control workers.

The pinned user ELF supplies code and tables; captured AREA11 player records
supply additional inputs. Hooked callees are scripted boundaries, never claims
about those callees. Every non-stack store (including repeated stores), every
callee argument and boundary memory image, final memory and defined integer
return are compared. Float instructions use the shared measured EE model.
"""
import ctypes as C
import json
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode as mode
from test_player_slide_reference import EE, read_elf, STACK_TOP, RETURN, sx32, s32

OUT = ROOT / 'build/aim-fire/control'
ENTRIES = {0x16F530: 0xA0, 0x16F5D0: 0x30, 0x16F600: 0x6EC, 0x172860: 0x138,
           0x17A8B0: 0xBC, 0x17A970: 0x15C, 0x17AAD0: 0xC8, 0x17ABA0: 0x3CC,
           0x17AF70: 0x388, 0x17B300: 0x118, 0x17B420: 0x3C, 0x17A800:0xA8, 0x1B5DC0:0x60}
# Actual arguments by original callee signature; unused registers aren't ABI.
SIG = {0x1C61D0:(2,0), 0x1FBD50:(3,1), 0x17A8B0:(2,0), 0x1B5DC0:(1,0),
       0x11E2A8:(0,1), 0x1B1470:(0,1), 0x11E620:(0,2), 0x1749A0:(3,1),
       0x17A130:(1,0), 0x102958:(2,0), 0x16F5D0:(1,0), 0x1C6DA0:(1,0),
       0x11DE90:(0,1), 0x1029C0:(1,0), 0x102B08:(2,1), 0x102BB0:(2,1),
       0x102918:(3,0), 0x1026A0:(3,0), 0x183C40:(2,0), 0x1B1240:(1,2),
       0x11E860:(1,0), 0x17A800:(2,0), 0x11E748:(0,1), 0x1B1510:(0,1), 0x102760:(2,0)}
RETURNING = {0x17A8B0, 0x17A970, 0x17AAD0, 0x17B300, 0x17B420, 0x1B5DC0}
ACTOR, LINK, NODETABLE, NODE = 0x680000, 0x690000, 0x6A0000, 0x6B0000
TMP = STACK_TOP - 0x10
MASK = 0xFFFFFFFF

def F(x): return struct.unpack('<I', struct.pack('<f', x))[0]
def in_code(pc): return any(a <= pc < a + n for a,n in ENTRIES.items())

def ranges(p):
    return [(p,0x320), (LINK,0x100), (NODETABLE,0x20), (NODE,0x100), (0x275B40,4),
            (0x810600,0x100), (0x810C00,0x300), (0x700036A0,0x40),
            (0x700038A0,0x30), (0x70003A20,16), (0x70003B74,12)]

def snapshot(mem,p): return tuple(mem.read(a,n) for a,n in ranges(p))

class OracleEE(EE):
    def __init__(self, elf):
        super().__init__(elf)
        self.events = None
        self.tables = bytes(self.mem[0x248680:0x248D00])
        self.outcomes = set()
    def branch(self,w,pc):
        b = super().branch(w,pc)
        if b is not None and in_code(pc): self.outcomes.add((pc,b[0]))
        return b
    def save(self,a,v,size=4):
        if self.events is not None and not 0x7F000000 <= a < 0x7F100000:
            assert any(lo <= a and a+size <= lo+n for lo,n in ranges(self.base)), ('uncompared write',hex(a),size)
            self.events.append(('write',a,size,v & ((1 << (8*size))-1)))
        super().save(a,v,size)

class Call(C.Structure):
    _fields_ = [('a',C.c_uint32*4),('f',C.c_uint32*4),('v0',C.c_uint32),('f0',C.c_uint32)]
MAP = C.CFUNCTYPE(C.c_void_p,C.c_void_p,C.c_uint32,C.c_size_t,C.c_int)
WORKER = C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_uint32,C.POINTER(Call))
class Bus(C.Structure):
    _fields_ = [('context',C.c_void_p),('map',MAP),('call',WORKER),('temporary',C.c_uint32)]

class Memory:
    def __init__(self, ee):
        self.mem, self.spad, self.stack = bytearray(ee.mem), bytearray(ee.spad), bytearray(ee.stack)
        self.arrays = [(C.c_ubyte*len(b)).from_buffer(b) for b in (self.mem,self.spad,self.stack)]
    def where(self,a):
        if 0x70000000 <= a < 0x70004000: return 1,a-0x70000000
        if 0x7F000000 <= a < 0x7F100000: return 2,a-0x7F000000
        if a < 0x2000000: return 0,a
        raise AssertionError(('native address',hex(a)))
    def read(self,a,n):
        i,k=self.where(a); return bytes((self.mem,self.spad,self.stack)[i][k:k+n])
    def save(self,a,v,n=4):
        i,k=self.where(a); (self.mem,self.spad,self.stack)[i][k:k+n]=(v & ((1<<(8*n))-1)).to_bytes(n,'little')
    def load(self,a,n=4): return int.from_bytes(self.read(a,n),'little')

# Scripts deliberately mutate fields read after the boundary. These writes are
# not implementations of the callees. Both sides receive exactly the same script.
def effects(case,index,entry):
    rng=random.Random(case['seed']*1009+index*31+entry)
    ret=rng.choice([0,1,-1,0x7FFFFFFF,-0x80000000,0x1000001])
    if entry==0x1B5DC0: ret=(case['seed']+index)%4
    if entry==0x11E860: ret=[-1,0,48,49,50,88,89,90,122,123,124,127,128][case['seed']%13]
    fret=rng.choice([F(0),F(.5),F(-.5),F(1),F(2),F(-2),F(.02),F(.020001)])
    if entry==0x11E748 and 'sqrt_result' in case: fret=case['sqrt_result']
    writes=[]
    if case['mutate']:
        writes += [(case['p']+0x275,rng.randrange(6),1), (case['p']+0x1F0,rng.choice([0x31,0x32,0x33,0x34,0x35,0xFF]),1),
                   (case['p']+5,rng.choice([0x1D,0x1E,0x1F,0x20,0x21]),1),
                   (case['p']+0x278,rng.choice([F(0),F(.3),F(.5),F(.7),F(1)]),4),
                   (case['p']+0x27C,rng.choice([F(0),F(.5),F(1)]),4),
                   (0x810CA6,rng.choice([0,1,2,3,4,255]),1), (0x810D3C,rng.randrange(2),1),
                   (0x810E64,rng.randrange(256),1), (0x810E65,rng.randrange(256),1)]
    if entry==0x183C40:
        writes += [(TMP+4*k,F(rng.uniform(-20,20)),4) for k in range(4)]
    if entry==0x102760:
        writes += [(0x700038B0+4*k,F(rng.uniform(-1,1)),4) for k in range(4)]
    if entry in (0x17A130,0x1C6DA0):
        writes += [(NODE+0x90+4*k,rng.getrandbits(32),4) for k in range(16)]
    return ret & MASK,fret,writes

def apply(mem, eff):
    for a,v,n in eff[2]: mem.save(a,v,n)

def setup(ee,case):
    p=case['p']; rng=random.Random(case['seed'])
    ee.write(0x248680,ee.tables)
    ee.r,ee.rh,ee.f=[0]*32,[0]*32,[0]*32
    ee.r[28],ee.r[29]=0x27D370,STACK_TOP
    for a,n in ranges(p):
        count=4 if a==0x70003A20 else n
        ee.write(a,bytes(rng.getrandbits(8) for _ in range(count))+bytes(n-count))
    if case.get('record'): ee.write(p,case['record'])
    ee.save(p+0x20,LINK); ee.save(0x275B40,NODETABLE); ee.save(NODETABLE+0x10,NODE)
    for off in (0xC0,0xC4,0xC8): ee.save(LINK+off,F(rng.uniform(-1,1)))
    ee.save(LINK+0x2E,rng.randrange(3),2)
    for off in (0xC0,0xC4,0x38,0x26C,0x270,0x278,0x27C,0x2E0,0x2E4):
        value=rng.choice([0,-.0,.3,.5,.7,.75,1,-1,.99999994,1.0000001])
        ee.save(p+off,F(value))
    if case['seed']%17==0:
        for off in (0xC0,0xC4,0x38,0x26C,0x270,0x278,0x27C): ee.save(p+off,rng.getrandbits(32))
    ee.save(p+5,rng.choice([0x1D,0x1E,0x1F,0x20,0,255]),1)
    ee.save(p+7,rng.choice([0,1,1,2,2,3,3,4,255]),1)
    ee.save(p+0x1F0,rng.choice([0x31,0x32,0x33,0x34,0x35,0,255]),1)
    ee.save(p+0x275,rng.randrange(6),1); ee.save(p+0x2F2,rng.randrange(2),1)
    ee.save(p+0x2E,rng.randrange(2),2); ee.save(p+0x28,rng.choice([-32768,-1,0,0,1,2,8]),2)
    ee.save(p+0x200,rng.choice([0,0x1000,0xFFFFFFFF]))
    for a in (0x810CA4,0x810CA6): ee.save(a,rng.choice([0,1,2,3,4,255]),1)
    for a in (0x810D3C,0x8106C7): ee.save(a,rng.choice([0,1,255]),1)
    ee.save(0x810C62,rng.choice([0,1,15,16,29,30,31,255]),1)
    ee.save(0x810CB4,rng.choice([-32768,-1,0,1,10,14,15,16,29,30,31,32767]),2)
    ee.save(0x810CAC,rng.choice([-32768,-1,0,1,100,32767]),2)
    ee.save(0x810E70,rng.randrange(4),2); ee.save(0x70003B7C,1,2); ee.save(0x70003B7E,2,2)
    for a,n,v in case.get('overrides',[]): ee.save(a if a>=0x100000 else p+a,v,n)
    if 'clip' in case: ee.save(ee.load(0x248B70+4*case['arg']),case['clip'],2)
    ee.base=p

class Native:
    def __init__(self,lib,ee,case,fail_at=None):
        self.mem=Memory(ee); self.case=case; self.events=[]; self.pending=None; self.calls=0; self.error=None
        self.fail_at=fail_at
        def mapping(_,a,n,writing):
            try:
                self.flush()
                i,k=self.mem.where(a)
                if writing: self.pending=(a,n)
                return C.addressof(self.mem.arrays[i])+k
            except BaseException as ex: self.error=ex; return None
        def worker(_,entry,ptr):
            try:
                self.flush(); f=ptr.contents; ni,nf=SIG[entry]
                self.events.append(('call',entry,tuple(f.a[:ni]),tuple(f.f[:nf]),snapshot(self.mem,case['p'])))
                at=self.calls; self.calls+=1
                if at==self.fail_at: return -37
                eff=effects(case,at,entry); apply(self.mem,eff)
                f.v0,f.f0=eff[:2]; return 0
            except BaseException as ex: self.error=ex; return -99
        self.keep=(MAP(mapping),WORKER(worker)); self.bus=Bus(None,*self.keep,TMP)
        self.lib=lib
    def flush(self):
        if self.pending:
            a,n=self.pending; self.pending=None
            self.events.append(('write',a,n,self.mem.load(a,n)))
    def run(self):
        result=C.c_int32(0x55555555); c=self.case
        a0=c['seed'] if c['entry']==0x1B5DC0 else c['p']
        status=self.lib.em_aim_fire_control_run(C.byref(self.bus),c['entry'],a0,c['arg'],c['farg'],C.byref(result))
        self.flush()
        if self.error: raise self.error
        return status,result.value

def original(ee,case,fail_at=None):
    setup(ee,case)
    events=[]; calls=[0]
    class Cut(Exception): pass
    def hook(entry):
        def run(e):
            ni,nf=SIG[entry]
            events.append(('call',entry,tuple(e.arg(i) for i in range(ni)),tuple(e.f[12:12+nf]),snapshot(e,case['p'])))
            at=calls[0]; calls[0]+=1
            if at==fail_at: raise Cut
            eff=effects(case,at,entry)
            e.events=None; apply(e,eff); e.events=events
            e.f[0]=eff[1]; e.ret_int(s32(eff[0]))
        return run
    ee.hooks={a:hook(a) for a in SIG if a != case['entry']}
    ee.events=events
    ee.r[4],ee.r[5],ee.f[12],ee.r[31]=case['seed'] if case['entry']==0x1B5DC0 else case['p'],sx32(case['arg']),case['farg'],RETURN
    try: ee.run(case['entry']); status=0
    except Cut: status=-37
    finally: ee.events=None
    return status,s32(ee.f[0] if case['entry']==0x17A800 else ee.r[2]),events,calls[0]

def compare(ee,lib,case,faults=False):
    setup(ee,case); native=Native(lib,ee,case)
    expected=original(ee,case)
    actual=native.run()
    assert actual[0]==expected[0], (case,'status',actual[0],expected[0])
    for i,(a,b) in enumerate(zip(native.events,expected[2])):
        if a!=b:
            if a[0]==b[0]=='call' and a[:4]==b[:4]:
                detail=[(hex(addr),next((k for k,(x,y) in enumerate(zip(a[4][j],b[4][j])) if x!=y),None)) for j,(addr,_) in enumerate(ranges(case['p'])) if a[4][j]!=b[4][j]]
                raise AssertionError((case,'boundary memory',i,hex(a[1]),detail))
            raise AssertionError((case,'event',i,a[:4],b[:4]))
    assert len(native.events)==len(expected[2]), (case,'events lengths',len(native.events),len(expected[2]))
    assert snapshot(native.mem,case['p'])==snapshot(ee,case['p']), (case,'final memory')
    if case['entry'] in RETURNING or case['entry']==0x17A800: assert actual[1]==expected[1], (case,'return',actual[1],expected[1])
    cuts=0
    if faults:
        for cut in range(expected[3]):
            setup(ee,case); broken=Native(lib,ee,case,cut)
            stopped=original(ee,case,cut); result=broken.run()
            assert result[0]==-37 and broken.events==stopped[2], (case,'fault stop',cut)
            cuts+=1
    return len(expected[2]),cuts

def build(source=None,name='control'):
    OUT.mkdir(parents=True,exist_ok=True)
    dest=OUT/(name+'.dylib')
    subprocess.run(['cc','-std=c11','-shared','-fPIC','-O2','-Wall','-Wextra','-Werror','-I',str(ROOT/'src/game'),
                    str(source or ROOT/'src/game/em_aim_fire_control.c'),'-o',str(dest)],check=True,capture_output=True)
    lib=C.CDLL(str(dest)); lib.em_aim_fire_control_run.argtypes=[C.POINTER(Bus),C.c_uint32,C.c_uint32,C.c_int32,C.c_uint32,C.POINTER(C.c_int32)]
    lib.em_aim_fire_control_run.restype=C.c_int
    return lib

def cases():
    allcases=[]
    for entry in ENTRIES:
        for seed in range(480):
            allcases.append(dict(entry=entry,seed=seed,p=ACTOR,arg=LINK if entry==0x17A800 else seed%6 if entry==0x17A8B0 else [-1,0,1,2,3,255][seed%6],
                                 farg=[F(.01),F(.015),F(.025),F(-.01)][seed%4],mutate=seed%2==1))
    route=ROOT.parent/'Extermination/build/s87/route'
    for path in sorted(route.glob('*/eeMemory.bin')):
        if not mode.in_scope_beat(path.parent.name): continue
        with path.open('rb') as stream: stream.seek(0x8102B0); record=stream.read(0x320)
        for i,entry in enumerate(ENTRIES):
            allcases.append(dict(entry=entry,seed=1000+i+int(path.parent.name[:2])*19,p=0x8102B0,arg=LINK if entry==0x17A800 else 0,farg=F(.015),mutate=True,record=record,beat=path.parent.name))
    allcases += [dict(entry=0x17A8B0,seed=11000,p=ACTOR,arg=0,farg=0,mutate=False,clip=-1,overrides=[(5,1,0x1D)]),
                 dict(entry=0x17AF70,seed=11001,p=ACTOR,arg=0,farg=0,mutate=False,sqrt_result=0x3CA3D70B,overrides=[(0x2F2,1,1)])]
    return allcases

COVER = {(0x16F600,n) for n in (87,29,83,19,48,55,124)} | {(0x17AAD0,n) for n in (22,25,61,73,184)} | {(0x17ABA0,73),(0x17B300,32)}

def select_cases(total):
    if mode.FULL: return total
    pool=[c for c in total if c['seed'] < 11000 and c['entry'] not in (0x17A800,0x1B5DC0)]
    chosen=mode.select(pool,650,0xA17F1,axes=(lambda c:c['entry'],),keep=lambda i,c:'beat' in c or c['seed']<16)
    return chosen+[c for c in total if ((c['entry'],c['seed']) in COVER or c['seed']>=11000 or (c['entry'] in (0x17A800,0x1B5DC0) and (c['seed']<40 or 'beat' in c))) and c not in chosen]

def main():
    t=time.monotonic(); elf=read_elf(); lib=build(); ee=OracleEE(elf)
    targets=set()
    for start,size in ENTRIES.items():
        for pc in range(start,start+size,4):
            word=ee.load(pc)
            if word>>26==3: targets.add((word&0x3FFFFFF)<<2)
    assert targets==set(SIG), ('callee inventory',targets^set(SIG))
    total=cases(); selected=select_cases(total)
    mode.banner(mode.part(len(selected),len(total),'control cases'))
    events=cuts=0; coverage=[]
    for case in selected:
        ee.outcomes=set()
        n,c=compare(ee,lib,case,case['seed']<8); events+=n; cuts+=c
        coverage.append((case,set(ee.outcomes)))
    ee.outcomes=set().union(*(o for _,o in coverage))
    assert lib.em_aim_fire_control_run(None,0x16F5D0,ACTOR,0,0,None)<0
    sites=set(); scan=EE(elf)
    for start,size in ENTRIES.items():
        for pc in range(start,start+size,4):
            w=ee.load(pc); op,rs,rt=w>>26,w>>21&31,w>>16&31
            if op in (4,20) and rs==rt: continue
            if scan.branch(w,pc) is not None: sites.add(pc)
    missing=sorted((pc,taken) for pc in sites for taken in (False,True) if (pc,taken) not in ee.outcomes)
    (OUT/('coverage-'+mode.MODE+'.json')).write_text(json.dumps({'cases':len(selected),'events':events,'cuts':cuts,'sites':len(sites),'missing':[(hex(pc),b) for pc,b in missing]},indent=2)+'\n')
    assert not missing, ('uncovered conditional outcomes',[(hex(pc),b) for pc,b in missing])
    if mode.FULL:
        mode.FULL=False
        quick=select_cases(total)
        mode.FULL=True
        key=lambda c:(c['entry'],c['seed'],c.get('beat'))
        keys={key(c) for c in quick}; seen=set().union(*(o for c,o in coverage if key(c) in keys))
        extra=[]
        while seen != ee.outcomes:
            c,o=max(coverage,key=lambda pair:len(pair[1]-seen))
            extra.append((hex(c['entry']),c['seed'])); seen |= o
        print('quick coverage additions:',extra)
    print(f'PASS: {len(selected)} cases; {events} exact stores/callee boundaries; {cuts} callee fault cuts; {len(ee.outcomes)} branch outcomes; {len(SIG)} callee signatures; {time.monotonic()-t:.2f}s',flush=True)

if __name__=='__main__': main()
