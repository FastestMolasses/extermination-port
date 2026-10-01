#!/usr/bin/env python3
"""Original-instruction oracle for target acquisition and sight drawers.

Seven roots execute from the user's pinned ELF. Every direct store (address,
size, bytes, including same-value writes), each callee entry (arguments and
stack pointer), and defined results are compared. The native call runs over a
second complete memory image. All RAM and scratch bytes are compared at exit.
Only compiler register-save stack slots are excluded; 00185760's local tint is
compared. Callees are explicit scripted contracts, except SDK vector leaves,
which execute their original instructions identically on both sides. Scripted
callees are not claimed verified by this test. Capture cases start from AREA11
route RAM, with actors / globals adjusted to reach the otherwise absent states.
EM_TEST_FULL=1 expands cases; EM_AIM_FIRE_TARGET_SOURCE tests a source mutant.
"""
import ctypes as C
import math
import os
import random
import struct
import subprocess
import sys
import time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import reference_mode as RM
import test_player_slide_reference as S
from test_player_fall_reference import FallEE
import ee_float_model as M

OUT=ROOT/'build/aim-fire/target'
FUNCS={0x185A10:0x41C,0x185E30:0x26C,0x199220:0x544,0x1854E0:0x280,0x185760:0x2B0,
       0x183AC0:0xB8,0x183B80:0xBC}
SIG={0x102948:(2,0),0x103230:(2,1),0x1028B8:(3,0),0x1028D0:(3,0),0x1031E0:(2,0),
     0x102760:(2,0),0x102738:(2,0),0x1026A0:(3,0),0x183AC0:(1,0),0x183B80:(1,0),
     0x183C40:(2,0),0x19A570:(4,0),0x1B1240:(1,2),0x1B1470:(0,1),0x11DF78:(0,1),
     0x11E748:(0,1),0x1B0070:(0,0),0x122BB8:(0,0),0x1CD520:(5,3),0x1E2BA0:(3,1),
     0x1DD170:(5,0)}
REAL={0x102948,0x103230,0x1028B8,0x1028D0,0x1031E0,0x102760}
PLAYER,GUN,LIST,TARGET=0x8102B0,0x1E10000,0x1E20000,0x1E30000
A,B,D=0x700038A0,0x700038B0,0x700038D0
U32,U64,I,P=C.c_uint32,C.c_uint64,C.c_int,C.POINTER
class Call(C.Structure):
    _fields_=[('function',U32),('sp',U32),('a',U64*7),('f',U32*8),('na',U32),('nf',U32),('v0',U64),('f0',U32)]
MAP=C.CFUNCTYPE(C.c_void_p,C.c_void_p,U32,U32,I)
CALL=C.CFUNCTYPE(I,C.c_void_p,P(Call))
STORE=C.CFUNCTYPE(None,C.c_void_p,U32,U32)
class Host(C.Structure):
    _fields_=[('context',C.c_void_p),('map',MAP),('call',CALL),('store',STORE),('sp',U32),('fault',I),('fault_function',U32),('fault_address',U32)]

ELF=None
LIB=None
IMAGES={}
def build(source=None):
    OUT.mkdir(parents=True,exist_ok=True)
    source=source or os.environ.get('EM_AIM_FIRE_TARGET_SOURCE',str(ROOT/'src/game/em_aim_fire_target.c'))
    lib=OUT/(Path(source).stem+('.dylib' if sys.platform=='darwin' else '.so'))
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wpedantic','-ffp-contract=off',
                    '-shared','-fPIC','-Isrc',source,'-o',str(lib)],cwd=ROOT,check=True,capture_output=True)
    dll=C.CDLL(str(lib))
    for fn in FUNCS:
        args=[P(Host),U32]
        if fn==0x185E30:args+=[U32]
        if fn in (0x185A10,0x185E30,0x183AC0,0x183B80):args+=[P(U32)]
        getattr(dll,f'em_aim_fire_target_{fn:08X}').argtypes=args
    return dll

class Oracle(FallEE):
    def __init__(self,ram=None,spad=None):
        super().__init__(ELF,ram,spad)
        self.events=[];self.active=False;self.in_hook=False;self.branches=set();self.entry=0
    def save(self,a,v,size=4):
        super().save(a,v,size)
        if self.active and not self.in_hook and (a<0x7F000000 or
                self.entry==0x185760 and S.STACK_TOP-16<=a<S.STACK_TOP):
            self.events.append(('store',a,size,self.read(a,size)))
    def branch(self,w,pc):
        b=super().branch(w,pc)
        if b is not None and self.entry<=pc<self.entry+FUNCS[self.entry]:self.branches.add((pc,b[0]))
        return b

def nested(e,fn):
    saved=(list(e.r),list(e.rh),list(e.f),e.hi,e.lo,e.acc,e.cond,[list(v) for v in e.vf],list(e.vacc),e.q)
    hooks,e.hooks=e.hooks,{}
    e.r[31]=S.RETURN
    e.run(fn)
    v,f=e.r[2],e.f[0]
    e.r,e.rh,e.f,e.hi,e.lo,e.acc,e.cond,e.vf,e.vacc,e.q=saved
    e.hooks=hooks;e.r[2]=v;e.f[0]=f

def policy(e,fn,case,state):
    a=[int(e.r[4+i])&0xFFFFFFFF for i in range(5)]
    f=e.f[12:15]
    e.r[2]=0;e.f[0]=0
    if fn in REAL:nested(e,fn)
    elif fn==0x1B0070:e.r[2]=case.get('flags',0)
    elif fn in (0x183AC0,0x183B80):e.r[2]=case.get('eligible',1)
    elif fn==0x183C40:
        state['target']=a[0]
        e.write(a[1],e.read(a[0]+0xB0,16))
    elif fn==0x1B1240:e.f[0]=S.bits(case.get('angle',0.25))
    elif fn==0x1B1470:e.f[0]=f[0]
    elif fn==0x11DF78:e.f[0]=f[0]&0x7FFFFFFF
    elif fn==0x11E748:
        e.f[0]=M.vu_sqrt(f[0])
        if case.get('sqrt') is not None:e.f[0]=S.bits(case['sqrt'])
    elif fn==0x102738:e.f[0]=S.bits(case.get('dot',0.9))
    elif fn==0x1026A0:
        x,y,w=case.get('screen',(2048.,2048.,1.))
        for i,n in enumerate((x,y,4.,w)):e.save(a[0]+4*i,S.bits(n))
    elif fn==0x19A570:
        state['segments']=state.get('segments',0)+1
        if a[2]==6:e.r[2]=case.get('blocked',0)
        else:
            target=state.get('target',TARGET)
            hit=case.get('hit',1)
            if case['fn']==0x185A10 and state['segments']==1:hit=case.get('fast',0)
            e.r[2]=hit
            e.save(0x700031D4,0 if case.get('nullhit') else (target if not case.get('wrong',0) else target+0x400) if hit else 0)
            for i,n in enumerate(case.get('hitpoint',(12.,2.,8.,1.))):e.save(0x700031B0+4*i,S.bits(n))
    elif fn==0x122BB8:
        state['rng']=state.get('rng',0)+1
        e.r[2]=S.sx32((case.get('random',0x654321)*state['rng'])&0xFFFFFFFF)
    elif fn in (0x1DD170,0x1CD520,0x1E2BA0):pass
    else:raise AssertionError(hex(fn))
    # Deliberate contracts may change lock/aim fields at callee boundaries;
    # both sides receive the same changes and must reload where original does.
    if case.get('fx') and fn==0x1CD520:e.save(0x8106E0,0 if e.load(0x8106E0) else TARGET)
    if case.get('fx') and fn==0x11DF78:e.save(GUN+0x214,S.bits(0.37))

def prepare(case):
    ram,spad=IMAGES.get(case.get('capture'),(None,None))
    e=Oracle(ram,spad);e.entry=case['fn']
    if ram is not None:e.load_elf(ELF)
    e.write(GUN,bytes([0xA5])*0x320)
    if ram is None:e.write(PLAYER,bytes([0xA5])*0x320)
    e.save(PLAYER+0x20,GUN)
    for i,v in enumerate((1.25,-2.5,3.75,1.)):e.save(PLAYER+0xA0+4*i,S.bits(v))
    e.save(PLAYER+0xC4,S.bits(case.get('yaw',0.)))
    for i,v in enumerate((2.,3.,4.,1.)):e.save(GUN+0xA0+4*i,S.bits(v))
    for i,v in enumerate((0.,0.,1.,0.)):e.save(GUN+0xC0+4*i,S.bits(v))
    e.save(GUN+0x214,S.bits(case.get('cone',0.)))
    e.save(0x275B8C,LIST);e.save(0x275B94,case.get('count',1),2)
    e.save(0x810CA4,case.get('mode',1),1);e.save(0x8106C7,case.get('c7',0),1)
    e.save(0x275B00,case.get('cycle',30))
    e.save(0x8106E0,case.get('lock',TARGET));e.save(0x8106E4,TARGET+0x400);e.save(0x8106E8,0xABCDEFFF)
    for j in range(8):
        t=TARGET+0x400*j;e.write(t,bytes([0xB6])*0x320);e.save(LIST+4*j,t)
        e.save(t,case.get('active',1),1);e.save(t+2,case.get('class',2),1)
        e.save(t+3,case.get('model',3),1);e.save(t+0x34,case.get('hp',1),2)
        e.save(t+0x14,t+0x40);e.save(t+0x9F,case.get('9f',0),1);e.save(t+0xD,case.get('0d',0),1)
        pos=case.get('points',((12.,2.,8.),(2.,5.,5.),(15.,3.,8.),(4.,0.,5.),(200.,3.,8.),(3.,0.,6.),(1.,2.,4.),(7.,8.,9.)))[j]
        for i,v in enumerate((*pos,1.)):e.save(t+0xB0+4*i,S.bits(v))
    return e

def run_case(case):
    e=prepare(case)
    n=Oracle(e.mem,e.spad);n.stack[:]=e.stack;n.entry=case['fn']
    original_state={};native_state={}
    def hook(fn):
        def inner(x):
            na,nf=SIG[fn]
            x.events.append(('call',fn,x.r[29],tuple(v&0xFFFFFFFFFFFFFFFF for v in x.r[4:4+na]),tuple(x.f[12:12+nf])))
            x.in_hook=True
            policy(x,fn,case,original_state)
            x.in_hook=False
        return inner
    e.hooks={fn:hook(fn) for fn in SIG}
    if e.entry in e.hooks:del e.hooks[e.entry]
    e.active=True
    actor=TARGET if e.entry in (0x183AC0,0x183B80) else GUN if e.entry in (0x1854E0,0x185760) else PLAYER
    e.call(e.entry,[actor,TARGET] if e.entry==0x185E30 else [actor])
    native_events=[];errors=[];calls_seen=[0]
    arrays=[(C.c_uint8*len(buf)).from_buffer(buf) for buf in (n.mem,n.spad,n.stack)]
    def mapping(_,a,size,write):
        try:
            if a<0x2000000:b,i=0,a
            elif 0x70000000<=a<0x70004000:b,i=1,a-0x70000000
            elif 0x7F000000<=a<0x7F100000:b,i=2,a-0x7F000000
            else:return None
            if i+size>len(arrays[b]):return None
            return C.addressof(arrays[b])+i
        except Exception as ex:errors.append(ex);return None
    def observe(_,a,size):
        for at in range(a,a+size,8):
            count=min(8,a+size-at);native_events.append(('store',at,count,n.read(at,count)))
    def worker(_,p):
        c=p.contents
        try:
            fn=c.function;na,nf=SIG[fn]
            assert (c.na,c.nf)==(na,nf),(hex(fn),'signature')
            native_events.append(('call',fn,c.sp,tuple(c.a[:na]),tuple(c.f[:nf])))
            k=len(native_events)-1
            assert k<len(e.events) and native_events[k]==e.events[k],(case,'event',k,native_events[k],e.events[k] if k<len(e.events) else 'extra')
            # All preceding stores must agree before executing this callee.
            assert native_events==e.events[:len(native_events)],(case,'preceding stores',next((i for i,(a,b) in enumerate(zip(native_events,e.events)) if a!=b),None))
            calls_seen[0]+=1
            if calls_seen[0]==case.get('failat'):return -1
            n.r[29]=c.sp
            for i,v in enumerate(c.a[:na]):n.r[4+i]=v
            for i,v in enumerate(c.f[:nf]):n.f[12+i]=v
            policy(n,fn,case,native_state)
            c.v0=n.r[2];c.f0=n.f[0]
            return 0
        except Exception as ex:errors.append(ex);return -1
    callbacks=(MAP(mapping),CALL(worker),STORE(observe))
    h=Host(None,*callbacks,S.STACK_TOP,0,0,0);out=U32(0xDEADBEEF)
    args=[C.byref(h),actor]
    if e.entry==0x185E30:args.append(TARGET)
    hasout=e.entry in (0x185A10,0x185E30,0x183AC0,0x183B80)
    if hasout:args.append(C.byref(out))
    status=getattr(LIB,f'em_aim_fire_target_{e.entry:08X}')(*args)
    if errors:raise errors[0]
    if 'failat' in case:
        assert status==-1 and h.fault==3 and h.fault_function==e.entry,(case,'failure latch')
        assert native_events==e.events[:len(native_events)],(case,'failure prefix')
        assert out.value==0xDEADBEEF,(case,'failure result')
        before=list(native_events)
        assert getattr(LIB,f'em_aim_fire_target_{e.entry:08X}')(*args)==-1
        assert native_events==before,(case,'latched invocation had effects')
        return set(),len(native_events),calls_seen[0]
    assert status==0 and h.fault==0,(case,status,h.fault,hex(h.fault_address))
    assert native_events==e.events,(case,'final events',next((i for i,(a,b) in enumerate(zip(native_events,e.events)) if a!=b),None),len(native_events),len(e.events))
    assert n.mem==e.mem and n.spad==e.spad,(case,'whole memory')
    if hasout:assert out.value==(e.r[2]&0xFFFFFFFF),(case,'return',hex(out.value),hex(e.r[2]))
    return e.branches,len(e.events),sum(x[0]=='call' for x in e.events)

def cases():
    out=[]
    roots=list(FUNCS)[:5]
    for fn in roots:
        base={'fn':fn,'name':'baseline'};out.append(base)
        for field,values in {'count':[0,2,4,8],'active':[0,2,255],'eligible':[0], 'hp':[0,0xFFFF],
                             'angle':[1.5707964,1.5707965,1.0471976,1.0471977,-.25],
                             'dot':[.5,.50000006,.707099974,.7071,.81919998,.8192],
                             'flags':[0x80],'c7':[1],'fast':[1],'blocked':[1],'wrong':[1],'hit':[0],
                             'mode':[0,255],'cone':[-1.,.5,1.], 'lock':[0], 'cycle':[0,29,31],
                             'sqrt':[0.,19.999998,20.,35.,55.,110.,259.99997,260.,1000.],
                             'screen':[(2114.,2048.,1.),(2114.001,2048.,1.),(2048.,2078.,1.),(2048.,2078.001,1.),
                                       (2098.,2048.,1.),(2098.001,2048.,1.),(2048.,2048.,-1.),(2048.,2048.,0.)],
                             'random':[0,0xFFFFFFFF], 'fx':[1]}.items():
            for value in values:out.append(dict(base,**{field:value,'name':field+str(value)}))
    for fn in (0x183AC0,0x183B80):
        for model in range(0,22):
            for variant in range(4):out.append({'fn':fn,'name':'filter','model':model,'class':2 if variant<3 else 0xE2,'9f':variant,'0d':variant})
        for cls in (0,1,3,31,32,255):out.append({'fn':fn,'name':'class','class':cls})
    for fn in (0x1854E0,0x185760,0x185A10):
        out.append({'fn':fn,'name':'hit without actor','nullhit':1,'fast':1})
    out.append({'fn':0x185A10,'name':'difficulty flag','flags':0x80,'c7':1})
    out.append({'fn':0x185A10,'name':'fast dead target','fast':1,'hp':0})
    out.append({'fn':0x185A10,'name':'fast ineligible','fast':1,'eligible':0})
    out.append({'fn':0x185A10,'name':'fast occluded','fast':1,'blocked':1})
    out.append({'fn':0x185A10,'name':'easy radius edge','flags':0x80,'sqrt':54.5})
    out.append({'fn':0x199220,'name':'positive homogeneous sixteen','screen':(32768.,32768.,16.)})
    out.append({'fn':0x199220,'name':'manual x edge','mode':0,'screen':(2114.,2048.,1.)})
    out.append({'fn':0x199220,'name':'manual y edge','mode':0,'screen':(2048.,2078.,1.)})
    out.append({'fn':0x199220,'name':'manual outside x','mode':0,'screen':(2114.001,2048.,1.)})
    out.append({'fn':0x199220,'name':'manual outside y','mode':0,'screen':(2048.,2078.001,1.)})
    out.append({'fn':0x199220,'name':'middle insertion','count':3,
                'points':((10.,0.,0.),(30.,0.,0.),(20.,0.,0.))+((0.,0.,0.),)*5})
    rng=random.Random(97112)
    for k in range(200):
        out.append({'fn':roots[k%5],'name':'random','count':rng.randrange(9),'mode':rng.choice((0,1,255)),
                    'active':rng.choice((0,1,2)),'eligible':rng.randrange(2),'hp':rng.choice((0,1,2)),
                    'fast':rng.randrange(2),'hit':rng.randrange(2),'blocked':rng.randrange(2),'wrong':rng.randrange(2),
                    'angle':rng.uniform(-2,2),'yaw':rng.uniform(-1,1),'dot':rng.uniform(-1,1),
                    'cone':rng.uniform(-1,2),'lock':rng.choice((0,TARGET))})
    route=ROOT.parent/'Extermination/build/s87/route'
    for beat in sorted(route.glob('*')):
        if RM.in_scope_beat(beat.name) and (beat/'eeMemory.bin').exists():
            for fn in roots:out.append({'fn':fn,'name':'capture','capture':str(beat),'count':4,'mode':0})
    return out

def main():
    global ELF,LIB,IMAGES
    start=time.monotonic();ELF=S.read_elf();LIB=build()
    ee=FallEE(ELF)
    actual=set()
    for fn,size in FUNCS.items():
        for pc in range(fn,fn+size,4):
            w=ee.load(pc)
            if w>>26==3:actual.add((w&0x3FFFFFF)<<2)
    assert actual==set(SIG),(actual-set(SIG),set(SIG)-actual)
    all_cases=cases()
    selected=RM.select(all_cases,240,97112,axes=(lambda c:(c['fn'],c['name'] if c['name']!='capture' else 'capture'),),
                       keep=lambda i,c:c['name']=='baseline' or c['fn'] in (0x183AC0,0x183B80))
    RM.banner(RM.part(len(selected),len(all_cases),'target cases'))
    for c in selected:
        if 'capture' in c and c['capture'] not in IMAGES:
            p=Path(c['capture']);IMAGES[str(p)]=((p/'eeMemory.bin').read_bytes(),(p/'scratchpad.bin').read_bytes())
    rows=RM.parallel_map(run_case,selected)
    fault_cases=[]
    for c,row in zip(selected,rows):
        if c['name']=='baseline':
            fault_cases.extend(dict(c,failat=k) for k in range(1,row[2]+1))
    fault_rows=RM.parallel_map(run_case,fault_cases)
    for fn in FUNCS:
        for absent in ('map','call','latched'):
            e=prepare({'fn':fn});backing=(C.c_uint8*len(e.mem)).from_buffer(e.mem)
            def mp(_,a,n,w):return C.addressof(backing)+a if a+n<len(e.mem) else None
            h=Host(None,MAP() if absent=='map' else MAP(mp),CALL(),STORE(),S.STACK_TOP,7 if absent=='latched' else 0,0,0)
            answer=U32(0xDEADBEEF);args=[C.byref(h),PLAYER]
            if fn==0x185E30:args.append(TARGET)
            if fn in (0x185A10,0x185E30,0x183AC0,0x183B80):args.append(C.byref(answer))
            rc=getattr(LIB,f'em_aim_fire_target_{fn:08X}')(*args)
            if absent=='call' and fn in (0x183AC0,0x183B80):assert rc==0;continue
            assert rc==-1 and h.fault and answer.value==0xDEADBEEF,(hex(fn),absent,rc,h.fault)
    print(f'PASS: {len(fault_rows)} callee failure cuts and 19 missing/latched host refusals')
    branches=set().union(*(r[0] for r in rows))
    sites=set()
    for fn,size in FUNCS.items():
        for pc in range(fn,fn+size,4):
            w=ee.load(pc);op=w>>26;rs=w>>21&31;rt=w>>16&31
            if op in (4,20) and rs==0 and rt==0:continue
            if ee.branch(w,pc) is not None:sites.add(pc)
    gaps={(pc,b) for pc in sites for b in (False,True)}-branches
    assert not gaps, sorted(gaps)
    if os.environ.get('EM_AIM_FIRE_TARGET_GAPS'):print('gaps:',[(hex(pc),b) for pc,b in sorted(gaps)])
    print(f'PASS: {len(selected)} cases, {sum(r[1] for r in rows)} ordered stores/calls, {sum(r[2] for r in rows)} callee entries, {len(branches)} branch outcomes; {time.monotonic()-start:.2f}s')

def mutations():
    global ELF,LIB
    ELF=S.read_elf()
    source=(ROOT/'src/game/em_aim_fire_target.c').read_text()
    changes=[
      ('range', '#define RANGE 0x43820000u', '#define RANGE 0x43818000u'),
      ('homogeneous', '#define ONE 0x3F800000u', '#define ONE 0x40000000u'),
      ('stored byte', 'v >> (8 * i)', 'v >> (7 * i)'),
      ('loaded byte', 'p[i] << (8 * i)', 'p[i] << (7 * i)'),
      ('distance axis', 'word(r,from+8),word(r,to+8)', 'word(r,from+4),word(r,to+8)'),
      ('distance accumulation', 'em_ee_madd_bits(acc,z,z)', 'em_ee_add_bits(acc,z)'),
      ('angle heading', 'em_ee_sub_bits(f,word(r,actor+0xC4))', 'em_ee_add_bits(f,word(r,actor+0xC4))'),
      ('delta y', 'word(r,a+4),word(r,b+4)', 'word(r,a+4),word(r,b+8)'),
      ('ray mask', 'dest,1,0x20', 'dest,7,0x20'),
      ('ray identity', 'word(r,HIT_NODE) != target', 'word(r,HIT_NODE) == target'),
      ('world clear', 'segment(r,from,HIT,6,0) == 0', 'segment(r,from,HIT,6,0) != 0'),
      ('retain live bit', '(readn(r,target,1)&1) &&', '(readn(r,target,1)&2) &&'),
      ('retain hp', 'readn(r,target+0x34,2)) {', 'readn(r,target+0x35,2)) {'),
      ('retain range edge', 'em_ee_c_lt_bits(d,RANGE)) {', 'em_ee_c_le_bits(d,RANGE)) {'),
      ('retain dot edge', 'em_ee_c_le_bits(v,0x3F000000)', 'em_ee_c_lt_bits(v,0x3F000000)'),
      ('retain overshoot', 'scale(r,C,C,0x3F99999A)', 'scale(r,C,C,0x3F800000)'),
      ('retain result', 'answer = target;', 'answer = actor;'),
      ('easy radius', '? 0x425C0000 : 0x42DC0000', '? 0x42580000 : 0x42DC0000'),
      ('difficulty flag', '(flags&0x80)', '(flags&0x40)'),
      ('fast id', 'answer = word(r,target+0x14)', 'answer = target'),
      ('scan count', 'readn(r,0x275B94,2)', 'readn(r,0x275B95,1)'),
      ('scan list stride', 'list += 4; --count', 'list += 8; --count'),
      ('scan near threshold', '? 0x3F350481 : 0x3F51B717', '? 0x3F400000 : 0x3F51B717'),
      ('scan far threshold', '? 0x3F350481 : 0x3F51B717', '? 0x3F350481 : 0x3F700000'),
      ('scan yaw cone', 'yaw,0x3F860A92', 'yaw,0x3F900000'),
      ('screen scale', 'em_ee_mul_bits(0x3FC00000,y)', 'em_ee_mul_bits(0x3F800000,y)'),
      ('screen reject back', 'em_ee_c_lt_bits(t,0)', 'em_ee_c_le_bits(t,0x3F800000)'),
      ('screen radial cone', 'em_ee_mul_bits(0x425C0000,word', 'em_ee_mul_bits(0x42480000,word'),
      ('screen box x', 'em_ee_add_bits(0x42840000,', 'em_ee_add_bits(0x42820000,'),
      ('screen box y', 'em_ee_add_bits(0x42340000,', 'em_ee_add_bits(0x42300000,'),
      ('nearest shift', 'store(r,LOCK+8,word(r,LOCK+4))', 'store(r,LOCK+8,word(r,LOCK))'),
      ('middle insertion', 'em_ee_c_lt_bits(d,best[1])', 'em_ee_c_lt_bits(d,best[0])'),
      ('markers count', '== 1 ? 1 : 3', '== 1 ? 3 : 1'),
      ('sight hp mark', 'put(r,target+0xA,0x80,1)', 'put(r,target+0xA,0x40,1)'),
      ('sight latch', 'store(r,gun+0x210,1)', 'store(r,gun+0x210,0)'),
      ('sight near falloff', 'word(r,0x70003A24),0x41A00000', 'word(r,0x70003A24),0x41200000'),
      ('sight random channel', '((random>>15)&31)+0x40', '((random>>14)&31)+0x40'),
      ('laser cycle', '(word(r,0x275B00)+3)&31', '(word(r,0x275B00)+2)&31'),
      ('laser locked size', 'sprite(r,0x40A00000)', 'sprite(r,0x40400000)'),
      ('target special model', 'readn(r,actor+0x0D,1)==1', 'readn(r,actor+0x0D,1)!=1'),
    ]
    assert len(changes)==40
    folder=OUT/'mutations';folder.mkdir(parents=True,exist_ok=True)
    pool=[c for c in cases() if 'capture' not in c]
    pool.sort(key=lambda c:c['name']!='baseline')
    survivors=[]
    for i,(name,old,new) in enumerate(changes):
        assert old in source,(name,'missing edit')
        path=folder/f'mutant_{i:02d}.c';path.write_text(source.replace(old,new,1))
        LIB=build(str(path));killed=None
        for case in pool:
            try:run_case(case)
            except AssertionError:
                killed=case['name'];break
        if killed is None:survivors.append(name)
        print(f'{i+1:02d} {name}: '+('SURVIVED' if killed is None else f'killed by {killed}'),flush=True)
    (OUT/'mutation-results.txt').write_text(f'40 mutants; {40-len(survivors)} killed; survivors: {survivors}\n')
    assert not survivors,survivors
if __name__=='__main__':
    mutations() if '--mutations' in sys.argv else main()

