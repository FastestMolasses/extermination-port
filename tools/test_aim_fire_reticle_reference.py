#!/usr/bin/env python3
"""Original ELF oracle for 001DD170 / 001DD2F0 / 001DD600.

Checks every ordered direct store, every worker entry (including complete VU0
state), all final RAM/scratch, stack locals and VU0 state. Register-save stack
is excluded. Packet tables are read only from local ELF/capture bytes. Float
conversion calls run the original leaf; text and nested-draw boundaries have
explicit contracts. No emulator or renderer is launched. EM_TEST_FULL expands
random cases; --mutations runs the fixed bounded 40-mutant set.
"""
import ctypes as C
import os
import random
import subprocess
import sys
import time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import reference_mode as RM
import test_player_slide_reference as S
from test_area00_hud_reference import HudEE
import test_aim_fire_target_reference as T

OUT=ROOT/'build/aim-fire/reticle'
FUNCS={0x1DD170:0x180,0x1DD2F0:0x30C,0x1DD600:0x1A4}
SIG={0x1DD2F0:(3,0),0x1DD600:(3,0),0x1C5FB0:(3,0),0x1CBA50:(7,0),0x1281C0:(0,1)}
CONTEXT,PACKET,POINT,SCREEN=0x1D00000,0x1D10000,0x1D20000,0x70003600
U32,U64,I,P=C.c_uint32,C.c_uint64,C.c_int,C.POINTER
class Vu(C.Structure):
    _fields_=[('vf',(U32*4)*32),('acc',U32*4),('q',U32),('clip',U32)]
class Call(C.Structure):
    _fields_=[('function',U32),('sp',U32),('a',U64*7),('f12',U32),('na',U32),('nf',U32),('v0',U64)]
MAP=C.CFUNCTYPE(C.c_void_p,C.c_void_p,U32,U32,I)
CALL=C.CFUNCTYPE(I,C.c_void_p,P(Call))
STORE=C.CFUNCTYPE(None,C.c_void_p,U32,U32)
class Host(C.Structure):
    _fields_=[('context',C.c_void_p),('map',MAP),('call',CALL),('store',STORE),('sp',U32),('fault',I),('fault_function',U32),('fault_address',U32),('vu',P(Vu))]
ELF=None;LIB=None;IMAGES={}

def build(source=None):
    OUT.mkdir(parents=True,exist_ok=True)
    source=source or os.environ.get('EM_AIM_FIRE_RETICLE_SOURCE',str(ROOT/'src/game/em_aim_fire_reticle.c'))
    lib=OUT/(Path(source).stem+('.dylib' if sys.platform=='darwin' else '.so'))
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wpedantic','-ffp-contract=off','-shared',
                    '-fPIC','-Isrc',source,'-o',str(lib)],cwd=ROOT,check=True,capture_output=True)
    dll=C.CDLL(str(lib))
    for fn in FUNCS:
        getattr(dll,f'em_aim_fire_reticle_{fn:08X}').argtypes=[P(Host)]+[U32]*(5 if fn==0x1DD170 else 3)
    return dll

def vu_tuple(v):return (tuple(tuple(row) for row in v.vf),tuple(v.acc),v.q,v.clip)
def set_vu(v,state):
    vf,acc,q,clip=state
    for i,row in enumerate(vf):
        for j,n in enumerate(row):v.vf[i][j]=n
    for i,n in enumerate(acc):v.acc[i]=n
    v.q=q;v.clip=clip

class Oracle(HudEE):
    def __init__(self,ram=None,spad=None):
        super().__init__(ELF,ram,spad);self.events=[];self.active=False;self.inside=False;self.entry=0;self.branches=set()
    def local(self,a):
        sp=S.STACK_TOP
        return (sp-0x20<=a<sp if self.entry==0x1DD170 else
                sp-0x80<=a<sp-8 if self.entry==0x1DD2F0 else sp-0x40<=a<sp)
    def save(self,a,value,size=4):
        super().save(a,value,size)
        if self.active and not self.inside and (a<0x7F000000 or self.local(a)):
            self.events.append(('store',a,size,self.read(a,size)))
    def branch(self,w,pc):
        b=super().branch(w,pc)
        if b is not None and self.entry<=pc<self.entry+FUNCS[self.entry]:self.branches.add((pc,b[0]))
        return b

def policy(e,fn,case):
    if fn==0x1281C0:
        T.nested(e,fn)
        if case.get('fx'):
            e.save(SCREEN,(e.load(SCREEN)+13)&0xFFFFFFFF)
            e.save(SCREEN+4,(e.load(SCREEN+4)-17)&0xFFFFFFFF)
            e.save(0x2533D0,e.load(0x2533D0)^0x80000000)
    elif fn==0x1C5FB0:e.r[2]=0x1D30000
    else:e.r[2]=0

def prepare(case):
    ram,spad=IMAGES.get(case.get('capture'),(None,None));e=Oracle(ram,spad);e.entry=case['fn']
    if ram is not None:e.load_elf(ELF)
    e.save(0x275670,CONTEXT)
    for i in range(4):e.save(CONTEXT+0x10+4*i,PACKET+0x1000*i)
    e.write(PACKET,bytes([0xA5])*0x4000)
    for i,n in enumerate(case.get('point',(.25,-.5,.125,1.))):e.save(POINT+4*i,S.bits(n))
    for i,n in enumerate(case.get('screen',(32768,32768,0x1234,0x789))):e.save(SCREEN+4*i,n)
    clip=case.get('clipmatrix',((1.,0.,0.,0.),(0.,1.,0.,0.),(0.,0.,1.,0.),(0.,0.,0.,1.)))
    view=case.get('viewmatrix',((100.,0.,0.,0.),(0.,100.,0.,0.),(0.,0.,50.,0.),(2048.,2048.,100.,1.)))
    for row in range(4):
        for col in range(4):
            e.save(CONTEXT+0x2240+16*row+4*col,S.bits(clip[row][col]))
            e.save(0x70003AC0+16*row+4*col,S.bits(view[row][col]))
    rng=random.Random(case.get('seed',17))
    for i in range(1,32):e.vf[i]=[S.bits(rng.uniform(-2,2)) for _ in range(4)]
    e.vf[23]=[S.bits(v) for v in case.get('fog',(255.,0.,128.,-2.))]
    e.vacc=[S.bits(rng.uniform(-1,1)) for _ in range(4)];e.q=S.bits(.75);e.clip=0xABCDEF
    return e

def run_case(case):
    e=prepare(case);n=Oracle(e.mem,e.spad);n.stack[:]=e.stack;n.set_vu(e.vu_state());n.entry=e.entry
    def hook(fn):
        def cb(x):
            na,nf=SIG[fn]
            x.events.append(('call',fn,x.r[29],tuple(y&0xFFFFFFFFFFFFFFFF for y in x.r[4:4+na]),
                             tuple(x.f[12:12+nf]),x.vu_state()))
            x.inside=True;policy(x,fn,case);x.inside=False
        return cb
    e.hooks={fn:hook(fn) for fn in SIG if fn!=e.entry}
    args=[case.get('style',1),POINT,case.get('kind',0),case.get('rgba',0x80808080),case.get('value',37)] if e.entry==0x1DD170 else [case.get('style',1),SCREEN,case.get('rgba',0x80808080)]
    e.active=True;e.call(e.entry,args)
    arrays=[(C.c_uint8*len(b)).from_buffer(b) for b in (n.mem,n.spad,n.stack)]
    v=Vu();set_vu(v,n.vu_state());events=[];errors=[];called=[0]
    def mp(_,a,size,w):
        if a<0x2000000:b,i=0,a
        elif 0x70000000<=a<0x70004000:b,i=1,a-0x70000000
        elif 0x7F000000<=a<0x7F100000:b,i=2,a-0x7F000000
        else:return None
        return C.addressof(arrays[b])+i if i+size<=len(arrays[b]) else None
    def st(_,a,size):
        for at in range(a,a+size,8):
            count=min(8,a+size-at);events.append(('store',at,count,n.read(at,count)))
    def cb(_,p):
        c=p.contents
        try:
            na,nf=SIG[c.function];assert (c.na,c.nf)==(na,nf)
            events.append(('call',c.function,c.sp,tuple(c.a[:na]),(c.f12,) if nf else (),vu_tuple(v)))
            assert events==e.events[:len(events)],(case,'event',next((i for i,(x,y) in enumerate(zip(events,e.events)) if x!=y),None),events[-1],e.events[len(events)-1])
            called[0]+=1
            if called[0]==case.get('failat'):return -1
            n.r[29]=c.sp
            for i,a in enumerate(c.a[:na]):n.r[4+i]=a
            if nf:n.f[12]=c.f12
            n.set_vu(vu_tuple(v));policy(n,c.function,case);set_vu(v,n.vu_state());c.v0=n.r[2];return 0
        except Exception as ex:errors.append(ex);return -1
    callbacks=MAP(mp),CALL(cb),STORE(st)
    h=Host(None,*callbacks,S.STACK_TOP,0,0,0,C.pointer(v))
    status=getattr(LIB,f'em_aim_fire_reticle_{e.entry:08X}')(C.byref(h),*args)
    if errors:raise errors[0]
    if 'failat' in case:
        assert status==-1 and h.fault==3 and events==e.events[:len(events)]
        previous=list(events)
        assert getattr(LIB,f'em_aim_fire_reticle_{e.entry:08X}')(C.byref(h),*args)==-1 and events==previous
        return set(),len(events),called[0]
    assert status==0 and h.fault==0,(case,'native failure',status,h.fault,hex(h.fault_address))
    assert events==e.events,(case,'final events',next((i for i,(x,y) in enumerate(zip(events,e.events)) if x!=y),None),len(events),len(e.events))
    assert n.mem==e.mem and n.spad==e.spad,(case,'final memory')
    assert vu_tuple(v)==e.vu_state(),(case,'final VU')
    return e.branches,len(events),called[0]

def cases():
    out=[]
    for fn in FUNCS:
        out.append({'fn':fn,'name':'baseline'})
        for key,vals in {'rgba':[0,0xFFFFFFFF,0x7FFFFFFF,0xA5001234],'style':[0,2,3],
                         'screen':[(0,0,0,0),(0xFFFF,0x8000,0,0),(0xFFFFFFFF,0xFFFFFFFE,0,0),(0x7FFFFFFF,0x80000000,0,0)],
                         'fx':[1]}.items():
            for v in vals:out.append({'fn':fn,'name':key+str(v),key:v})
    for kind in (0,1,2,0xFFFFFFFF):
        for axis in range(3):
            for value in (-1.00000012,-1.,0.,1.,1.00000012):
                point=[0.,0.,0.,1.];point[axis]=value
                out.append({'fn':0x1DD170,'name':f'clip {kind} {axis} {value}','kind':kind,'point':point})
    for fog in ((255.,0.,0.,0.),(255.,0.,-1.,-1.),(255.,0.,1000.,1000.),(255.,0.,128.,-.01)):
        out.append({'fn':0x1DD170,'name':'fog'+str(fog),'kind':1,'fog':fog})
    out.append({'fn':0x1DD170,'name':'projected w two','viewmatrix':((100.,0.,0.,0.),(0.,100.,0.,0.),(0.,0.,50.,0.),(2048.,2048.,100.,2.))})
    rng=random.Random(97112)
    for i in range(160):
        out.append({'fn':list(FUNCS)[i%3],'name':'random','seed':i,'rgba':rng.getrandbits(32),'kind':rng.randrange(3),
                    'screen':tuple(rng.getrandbits(32) for _ in range(4)),
                    'point':tuple(rng.uniform(-1.2,1.2) for _ in range(3))+(1.,),
                    'fog':tuple(rng.uniform(-300,300) for _ in range(4))})
    route=ROOT.parent/'Extermination/build/s87/route'
    for beat in sorted(route.glob('*')):
        if RM.in_scope_beat(beat.name) and (beat/'eeMemory.bin').exists():
            for fn in FUNCS:out.append({'fn':fn,'name':'capture','capture':str(beat),'kind':1})
    return out

def main():
    global ELF,LIB,IMAGES
    start=time.monotonic();ELF=S.read_elf();LIB=build();all_cases=cases()
    selected=RM.select(all_cases,140,97112,axes=(lambda c:(c['fn'],c['name']),))
    RM.banner(RM.part(len(selected),len(all_cases),'reticle cases'))
    for c in selected:
        if 'capture'in c and c['capture'] not in IMAGES:
            p=Path(c['capture']);IMAGES[str(p)]=((p/'eeMemory.bin').read_bytes(),(p/'scratchpad.bin').read_bytes())
    ee=HudEE(ELF);targets=set();sites=set()
    for fn,size in FUNCS.items():
        for pc in range(fn,fn+size,4):
            w=ee.load(pc);op=w>>26;rs=w>>21&31;rt=w>>16&31
            if op==3:targets.add((w&0x3FFFFFF)<<2)
            if op in (4,20) and rs==0 and rt==0:continue
            if ee.branch(w,pc) is not None:sites.add(pc)
    assert targets==set(SIG)
    rows=RM.parallel_map(run_case,selected)
    faults=[]
    for c,row in zip(selected,rows):
        if c['name']=='baseline' or c['name'].startswith('fog'):
            faults.extend(dict(c,failat=k) for k in range(1,row[2]+1))
    RM.parallel_map(run_case,faults)
    branches=set().union(*(r[0] for r in rows));gaps={(p,b) for p in sites for b in (False,True)}-branches
    assert not gaps,[(hex(p),b) for p,b in sorted(gaps)]
    print(f'PASS: {len(selected)} cases, {sum(r[1] for r in rows)} ordered stores/calls, {sum(r[2] for r in rows)} callee entries, {len(branches)} branch outcomes, {len(faults)} failure cuts; {time.monotonic()-start:.2f}s')

def mutations():
    global ELF,LIB
    ELF=S.read_elf();source=(ROOT/'src/game/em_aim_fire_reticle.c').read_text()
    changes=[
      ('clip matrix offset','+0x2240','+0x2280'),
      ('clip point register','quad_read(r,position,v->vf[1])','quad_read(r,position,v->vf[2])'),
      ('clip transform row','transform(r,24)','transform(r,25)'),
      ('clip equality','(x&0x7FFFFFFF)>w','(x&0x7FFFFFFF)>=w'),
      ('clip sign','x>>31 ? 2u : 1u','x>>31 ? 1u : 2u'),
      ('clip history','v->clip<<6','v->clip<<5'),
      ('clip rejection','if(flags)return 0','if(!flags)return 0'),
      ('view rows','0x70003AC0+16*i','0x70003AC0+16*((i+1)%4)'),
      ('perspective numerator','em_vu_div_bits(0x3F800000,','em_vu_div_bits(0x40000000,'),
      ('project z','EM_VU_MULQ,14,','EM_VU_MULQ,12,'),
      ('fog accumulator','EM_VU_MULABC,1,2,','EM_VU_MULABC,1,3,'),
      ('fog slope','EM_VU_MADDBC,1,3,','EM_VU_MADDBC,1,2,'),
      ('fog cap','v->vf[2][3],v->vf[23][0]','v->vf[2][3],0x3F800000'),
      ('fog floor','em_vu_max_bits(v->vf[2][3],0)','em_vu_max_bits(v->vf[2][3],0x3F800000)'),
      ('screen quantization','em_vu_ftoi4_bits(v->vf[2][i])','em_vu_ftoi0_bits(v->vf[2][i])'),
      ('screen destination','quad_write(r,0x70003600,','quad_write(r,0x70003610,'),
      ('kind switch','if(kind==0)','if(kind==2)'),
      ('marker style','a[7]={sx(style),0x70003600','a[7]={0,0x70003600'),
      ('text rgb','rgba&0xFFFFFF','rgba&0xFFFF'),
      ('text alpha','rgba>>24,1','rgba>>16,1'),
      ('text value','a[7]={sx(value),4,0}','a[7]={sx(value+1),4,0}'),
      ('text x offset','word(r,0x70003600)+0xC0','word(r,0x70003600)+0x80'),
      ('text y coordinate','y=word(r,0x70003604)','y=word(r,0x70003600)'),
      ('text width','a[3]=8;a[4]=8','a[3]=16;a[4]=8'),
      ('packet slot','4*style+0x10','4*style+0x14'),
      ('packet tag byte','block+3,0x10,1','block+3,0x20,1'),
      ('packet qwc','block,count,2','block,count+1,2'),
      ('packet advance','block+advance,4','block+advance+16,4'),
      ('packet direct','0x50000000u+count-1','0x50000000u+count'),
      ('packet colour widen','block+0x38,rgba,8','block+0x38,sx(rgba),8'),
      ('x scale','em_ee_mul_bits(0x3F4CCCCD,','em_ee_mul_bits(0x3F000000,'),
      ('y scale','em_ee_mul_bits(0x3F000000,word','em_ee_mul_bits(0x3F4CCCCD,word'),
      ('screen origin x','word(r,screen)+q','word(r,screen+4)+q'),
      ('screen origin y','word(r,screen+4)+q','word(r,screen)+q'),
      ('coordinate packing','word(r,0x70003624)<<16','word(r,0x70003624)<<15'),
      ('coordinate sign','sx(xy)|UINT64_C','(uint64_t)xy|UINT64_C'),
      ('long template length','0x2533D0,80','0x2533D0,72'),
      ('first vertices','r->sp+0x70,10,8','r->sp+0x70,9,8'),
      ('second vertices','r->sp+0xC0,5,6','r->sp+0xC0,4,6'),
      ('square vertices','r->sp+0x50,8,7','r->sp+0x50,7,7'),
    ]
    assert len(changes)==40
    folder=OUT/'mutations';folder.mkdir(parents=True,exist_ok=True)
    pool=[c for c in cases() if 'capture' not in c];pool.sort(key=lambda c:c['name']!='baseline')
    survivors=[]
    for i,(name,old,new) in enumerate(changes):
        assert old in source,(name,'missing edit')
        path=folder/f'mutant_{i:02d}.c';path.write_text(source.replace(old,new,1));LIB=build(str(path));killed=None
        for case in pool:
            try:run_case(case)
            except AssertionError:killed=case['name'];break
        if killed is None:survivors.append(name)
        print(f'{i+1:02d} {name}: '+('SURVIVED' if killed is None else f'killed by {killed}'),flush=True)
    (OUT/'mutation-results.txt').write_text(f'40 mutants; {40-len(survivors)} killed; survivors: {survivors}\n')
    assert not survivors,survivors
if __name__=='__main__':mutations() if '--mutations' in sys.argv else main()

