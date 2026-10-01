#!/usr/bin/env python3
"""Composition check for live aim draw bindings against original instructions.

The fixture exposes a single canonical RCL context/arena backed by the native
RAM image. Sprite, beam, reticle and packet-chain bodies are the existing
verified owners. All external SDK/LCG workers execute original instructions
on both copies; text drawing is an explicit boundary contract. Compares each
external worker's arguments and all previously written RAM/scratch bytes at
that boundary, and complete RAM/scratch at every root exit. Checks vf23 handoff
and defined sprite returns. The pure-owner suites check ordered direct writes
and their complete call interfaces; this composition suite does not repeat
those store observers or claim every ambient VU register is shared live.
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
import test_aim_fire_target_reference as T
import test_aim_fire_reticle_reference as R
import test_area00_hud_reference as H
OUT=ROOT/'build/aim-fire/render'
CTX=0x811CC0
POINT,END,COLOUR=0x1D20000,0x1D20010,0x1D20020
SIG={0x1028D0:(3,0),0x11E748:(0,1),0x102870:(2,1),0x102948:(2,0),0x122BB8:(0,0),
     0x11E2A8:(0,1),0x11DF78:(0,1),0x1281C0:(0,1),0x1028B8:(3,0),0x1C5FB0:(3,0),0x1CBA50:(7,0)}
MAP=C.CFUNCTYPE(C.c_void_p,C.c_void_p,C.c_uint32,C.c_size_t,C.c_int)
class Host(C.Structure):
    _fields_=[('context',C.c_void_p),('map',MAP),('call',T.CALL),('vu',C.POINTER(R.Vu)),('valid',C.POINTER(C.c_int)),('fault_function',C.c_uint32),('fault_address',C.c_uint32)]
ELF=None;LIB=None;IMAGES={}
def build():
    OUT.mkdir(parents=True,exist_ok=True);path=OUT/('render.dylib' if sys.platform=='darwin' else 'render.so')
    sources=['tools/aim_fire_render_fixture.c','src/game/em_aim_fire_render_live.c','src/game/em_aim_fire_reticle.c',
             'src/game/em_player_equipment_sprite.c','src/game/em_area00_hud.c','src/game/em_packet_chain_original.c','src/game/em_status_ui_leftovers.c']
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wpedantic','-ffp-contract=off','-shared','-fPIC','-Isrc',*sources,'-o',str(path)],cwd=ROOT,check=True,capture_output=True)
    lib=C.CDLL(str(path));lib.em_aim_fire_render_live_call.argtypes=[C.POINTER(Host),C.POINTER(T.Call)]
    lib.em_aim_fire_render_fixture_bind.argtypes=[C.c_void_p,C.c_void_p]
    return lib
class Oracle(H.HudEE):
    def __init__(self,ram=None,spad=None):
        super().__init__(ELF,ram,spad);self.events=[];self.inside=False;self.writes=set()
    def save(self,a,value,size=4):
        super().save(a,value,size)
        if not self.inside and (a<0x2000000 or 0x70000000<=a<0x70004000):
            self.writes.update(range(a,a+size))
    def snapshot(self):
        return tuple((a,self.read(a,1)) for a in sorted(self.writes))

def policy(e,fn):
    if fn==0x1C5FB0:
        e.save(0x8111D0,0x31323334);e.save(0x8111D4,0);e.r[2]=0x8111D0
    elif fn==0x1CBA50:
        pass # explicit text boundary; its draw body is outside this adapter
    else:T.nested(e,fn)

def prepare(case):
    ram,spad=IMAGES.get(case.get('capture'),(None,None));e=Oracle(ram,spad)
    e.save(0x275670,CTX);e.save(0x275674,0x814220)
    for o,a in ((0x10,0x500000),(0x14,0x510000),(0x18,0x520000),(0x1C,0x530000)):e.save(CTX+o,a)
    e.mem[0x7635C0:0x76B5C0]=bytes(0x8000)
    identity=[1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.]
    clip=identity[:];clip[15]=case.get('clipw',4.)
    screen=identity[:];screen[12]=2048.;screen[13]=2048.;screen[15]=case.get('screenw',2.)
    for j in range(4):
        for i,v in enumerate(clip):e.save(CTX+0x2240+j*64+i*4,S.bits(v))
    for a,m in ((0x70003A40,identity),(0x70003AC0,screen)):
        for i,v in enumerate(m):e.save(a+4*i,S.bits(v))
    for i,v in enumerate((255.,2048.,220.,-2.)):e.save(CTX+0xA0+4*i,S.bits(v))
    for a,p in ((POINT,case.get('point',(0.,0.,1.,1.))),(END,case.get('end',(1.,.5,1.,1.))),
                (COLOUR,case.get('colour',(1.,.25,.5,.8)))):
        for i,v in enumerate(p):e.save(a+4*i,S.bits(v))
    e.save(0x26F590,case.get('seed',1234))
    rng=random.Random(case.get('seed',1234))
    for i in range(1,32):e.vf[i]=[S.bits(rng.uniform(-1,1)) for _ in range(4)]
    e.vf[23]=[S.bits(v) for v in case.get('ambient',(255.,0.,64.,-.25))]
    e.vacc=[S.bits(.125)]*4;e.q=S.bits(.75);e.clip=0xA5C4D3;e.writes.clear();return e

def args_for(fn,case):
    if fn==0x1CD370:return [case.get('matrix',0)],[]
    if fn==0x1CB5F0:return [0x7635C0,0x4000,6],[]
    if fn==0x1CB6B0:return [0x7635C0,0x4000,9,0x266930],[]
    if fn==0x1CB900:return [0x7635C0,0x4000,2],[]
    if fn==0x1CD520:return [case.get('bucket',0),case.get('mode',1),POINT,0x6200000000008002,case.get('rgba',0xAA4FBBCC)],[S.bits(v) for v in (3.,5.,case.get('zbias',0.))]
    if fn==0x1E2BA0:return [POINT,END,COLOUR],[S.bits(260.)]
    return [case.get('style',1),POINT,case.get('kind',0),case.get('rgba',0xAA4FBBCC),37],[]

def run_case(case):
    e=prepare(case);n=Oracle(e.mem,e.spad);n.stack[:]=e.stack;n.set_vu(e.vu_state())
    def hook(fn):
        def cb(x):
            na,nf=SIG[fn]
            x.events.append((fn,x.r[29],tuple(y&0xFFFFFFFFFFFFFFFF for y in x.r[4:4+na]),tuple(x.f[12:12+nf]),x.snapshot()))
            x.inside=True;policy(x,fn);x.inside=False
        return cb
    e.hooks={fn:hook(fn) for fn in SIG}
    arrays=[(C.c_uint8*len(b)).from_buffer(b) for b in (n.mem,n.spad,n.stack)]
    def mp(_,a,z,w):
        # RCL-owned ranges must be obtained from the canonical provider.
        if (0x28F700<=a<0x76B5C0 or CTX<=a<0x817240 or 0x275670<=a<0x2756A0 or
            0x70003A40<=a<0x70003B40):raise AssertionError(('unexpected RCL fallback',hex(a),z))
        if a<0x2000000:b,i=0,a
        elif 0x70000000<=a<0x70004000:b,i=1,a-0x70000000
        elif 0x7F000000<=a<0x7F100000:b,i=2,a-0x7F000000
        else:return None
        return C.addressof(arrays[b])+i if i+z<=len(arrays[b]) else None
    v=R.Vu();R.set_vu(v,n.vu_state());valid=C.c_int(case.get('valid',1));errors=[];events=[]
    def worker(_,ptr):
        c=ptr.contents
        try:
            na,nf=SIG[c.function];assert (na,nf)==(c.na,c.nf)
            ref=e.events[len(events)]
            got=(c.function,c.sp,tuple(c.a[:na]),tuple(c.f[:nf]),tuple((a,n.read(a,1)) for a,_ in ref[4]))
            assert got==ref,(case,'boundary',len(events),got[:4],ref[:4],next(((hex(a),x,y) for (a,x),(_,y) in zip(got[4],ref[4]) if x!=y),None))
            events.append(got)
            n.r[29]=c.sp
            for i,a in enumerate(c.a[:na]):n.r[4+i]=a
            for i,f in enumerate(c.f[:nf]):n.f[12+i]=f
            policy(n,c.function);c.v0=n.r[2];c.f0=n.f[0];return 0
        except Exception as ex:errors.append(ex);return -1
    callbacks=MAP(mp),T.CALL(worker);host=Host(None,*callbacks,C.pointer(v),C.pointer(valid),0,0)
    LIB.em_aim_fire_render_fixture_bind(C.addressof(arrays[0]),C.addressof(arrays[1]))
    totalwrites=0
    for fn in case.get('sequence',[case['fn']]):
        args,floats=args_for(fn,case);e.r[29]=S.STACK_TOP
        for i,a in enumerate(args):e.r[4+i]=a&0xFFFFFFFFFFFFFFFF
        for i,f in enumerate(floats):e.f[12+i]=f
        e.r[31]=S.RETURN;e.run(fn);call=T.Call();call.function=fn;call.sp=S.STACK_TOP;call.na=len(args);call.nf=len(floats)
        for i,a in enumerate(args):call.a[i]=a&0xFFFFFFFFFFFFFFFF
        for i,f in enumerate(floats):call.f[i]=f
        status=LIB.em_aim_fire_render_live_call(C.byref(host),C.byref(call))
        if errors:raise errors[0]
        assert status==0 and host.fault_function==0,(case,hex(fn),status,hex(host.fault_function),hex(host.fault_address))
        assert n.mem==e.mem,(case,hex(fn),'RAM',next((hex(i) for i,(a,b) in enumerate(zip(n.mem,e.mem)) if a!=b),None))
        assert n.spad==e.spad,(case,hex(fn),'scratch',next((hex(i) for i,(a,b) in enumerate(zip(n.spad,e.spad)) if a!=b),None))
        assert tuple(v.vf[23])==tuple(e.vf[23]),(case,'vf23 handoff')
        if fn in (0x1CD520,0x1CD370,0x1CB5F0):assert call.v0==e.r[2],(case,'sprite return',call.v0,e.r[2])
        totalwrites+=len(e.writes)
    assert len(events)==len(e.events)
    return len(events),totalwrites

def cases():
    out=[{'fn':fn} for fn in (0x1CD370,0x1CB5F0,0x1CB6B0,0x1CB900)]
    for mode in range(5):
        for point in ((0.,0.,1.,1.),(5.,0.,1.,1.),(-5.,0.,1.,1.),(0.,0.,-1.,1.)):
            out.append({'fn':0x1CD520,'mode':mode,'point':point})
    for rgba in (0,0xFFFFFFFF,0x80000000):out.append({'fn':0x1CD520,'rgba':rgba})
    for point,end in (((0.,0.,1.,1.),(1.,.5,1.,1.)),((-5.,0.,1.,1.),(5.,0.,1.,1.)),((5.,0.,1.,1.),(7.,0.,1.,1.)),((1.,1.,1.,1.),(1.,1.,1.,1.))):
        out.append({'fn':0x1E2BA0,'point':point,'end':end})
    for style in range(4):
        for kind in (0,1,2):out.append({'fn':0x1DD170,'style':style,'kind':kind})
    out.extend([{'fn':0x1DD170,'point':(5.,0.,1.,1.)},
                {'fn':0x1DD170,'sequence':[0x1CD520,0x1DD170],'valid':0},
                {'fn':0x1DD170,'sequence':[0x1E2BA0,0x1DD170],'valid':0}])
    rng=random.Random(97112)
    for i in range(40):out.append({'fn':(0x1CD520,0x1E2BA0,0x1DD170)[i%3],'seed':i,'mode':i%5,'kind':i%3,'point':tuple(rng.uniform(-5,5) for _ in range(3))+(1.,),'end':tuple(rng.uniform(-5,5) for _ in range(3))+(1.,)})
    return out

def refusal_cases():
    e=prepare({});arrays=[(C.c_uint8*len(b)).from_buffer(b) for b in(e.mem,e.spad)]
    LIB.em_aim_fire_render_fixture_bind(C.addressof(arrays[0]),C.addressof(arrays[1]))
    count=0
    for absent in ('host','callframe','map','vu','valid','ambient','latched','unsupported','zero-function'):
        v=R.Vu();valid=C.c_int(0 if absent=='ambient' else 1)
        host=Host(None,MAP(lambda *_:None),T.CALL(),C.pointer(v),C.pointer(valid),0,0)
        call=T.Call();call.function=0x1DD170;call.sp=S.STACK_TOP
        if absent=='map':host.map=MAP()
        if absent=='vu':host.vu=C.POINTER(R.Vu)()
        if absent=='valid':host.valid=C.POINTER(C.c_int)()
        if absent=='latched':host.fault_function=0xDEAD
        if absent=='unsupported':call.function=0xBAD
        if absent=='zero-function':call.function=0
        before=bytes(e.mem)+bytes(e.spad)
        assert LIB.em_aim_fire_render_live_call(None if absent=='host' else C.byref(host),None if absent=='callframe' else C.byref(call))==-1,absent
        assert before==bytes(e.mem)+bytes(e.spad),absent
        if absent not in ('host','callframe'):assert host.fault_function,absent
        count+=1
    return count

def main():
    global ELF,LIB
    start=time.monotonic();ELF=S.read_elf();LIB=build();pool=cases()
    selected=RM.select(pool,46,97112,axes=(lambda c:(c['fn'],tuple(c.get('sequence',[])),c.get('mode'),c.get('kind')),))
    RM.banner(RM.part(len(selected),len(pool),'render binding cases'))
    rows=RM.parallel_map(run_case,selected);refusals=refusal_cases()
    print(f'PASS: {len(rows)} cases, {sum(x[0] for x in rows)} external worker boundaries, {sum(x[1] for x in rows)} observed byte addresses at root exits; {refusals} fail-stop checks; {time.monotonic()-start:.2f}s')
if __name__=='__main__':main()
