#!/usr/bin/env python3
"""Light adapter with real SDK/pool/packet/depth owners versus original code.

Only model allocation/placement are scripted boundaries; their independent
canonical adapter oracle proves their bodies. All light registration, release,
matrix/colour math, GS references and page insertion run real native owners.
Captured memory is an oracle fixture, never a live seed or an asset export.
"""
import ctypes as C
import json
import struct
import subprocess
import time
from pathlib import Path
import reference_mode as mode
from test_area01_runtime_reference import Call, Host, View, Worker
from test_anim_runtime_rest_reference import EE
from test_face_attach_reference import SOURCES
from test_player_slide_reference import read_elf, RETURN, STACK_TOP
import test_object_unit_reference as unit

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/level2/light-live'
DECOMP=ROOT.parent/'Extermination'
ARRIVAL=DECOMP/'build/c10/exit/exit_01_movie_arrival'
NODE=0x7B0F50
CTX=0x811CC0
DL=0x740000
MASK=(1<<64)-1
SPECS={0x1C22A0:(1,0),0x1C6380:(1,0),0x102948:(2,0),0x1D7FA0:(3,2),
       0x1029C0:(1,0),0x102C58:(3,0),0x102918:(3,0),0x1028B8:(3,0),
       0x1026D0:(3,0),0x1D3990:(1,0),0x1CAAC0:(3,0)}
class Service(C.Structure):
    _fields_=[('function',C.c_uint32),('argument',C.c_uint32),('word',C.c_uint32),
              ('position',C.c_float*4),('color',C.c_float*4),('multiplier',C.c_uint32),('adder',C.c_uint32)]
def u32(b,a):return struct.unpack_from('<I',b,a)[0]
def put(b,a,v):struct.pack_into('<I',b,a,v&0xFFFFFFFF)
def build():
    OUT.mkdir(parents=True,exist_ok=True)
    target=OUT/'light.dylib'
    sources=['tests/area01_light_live_bridge.c','src/game/em_area01_light_live.c',
             'src/game/em_area00_fx_exit.c','src/game/em_area00_world.c',
             'src/game/em_owner_draw_live.c','src/game/em_object_unit.c',
             'src/game/em_point_light.c','src/game/em_packet_chain_original.c',
             'src/game/em_aim_fire_sdk_memory.c','src/game/em_camera_commit_original.c','src/game/em_sdk_math_original.c','src/game/em_effect_original.c',
             'src/game/em_coll_probe_original.c',*SOURCES]
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off',
                    '-shared','-fPIC','-Isrc','-Wl,-dead_strip','-Wl,-exported_symbol,_al_*',
                    *dict.fromkeys(sources),'-o',str(target)],cwd=ROOT,check=True)
    lib=C.CDLL(str(target))
    lib.al_seed.argtypes=[C.c_void_p,C.c_void_p]
    lib.al_call.argtypes=[C.POINTER(Host),C.POINTER(Call),C.POINTER(C.c_uint32)]
    lib.al_prepare.argtypes=[C.POINTER(Host),C.POINTER(Call),C.POINTER(Service)]
    lib.al_invoke.argtypes=[C.POINTER(Host),C.POINTER(Service),C.POINTER(Call)]
    lib.al_sdk.argtypes=[C.POINTER(Host),C.POINTER(Call)]
    lib.al_triangles.argtypes=[C.c_uint32,C.POINTER(unit.Triangle),C.c_uint32]
    return lib

def run(lib,elf,path,entry,variant):
    ram=bytearray((path/'eeMemory.bin').read_bytes());spr=(path/'scratchpad.bin').read_bytes()
    put(ram,CTX+0x1C,DL);put(ram,CTX+0x18,DL-0x2000)
    ram[0x7635C0:0x76B5C0]=bytes(0x8000)
    put(ram,CTX+0x9C,variant&1)
    put(ram,CTX+0xC,(u32(ram,CTX+0xC)&~1)|((variant>>1)&1))
    table=u32(ram,0x28A59C)
    model=table+(u32(ram,table+4+4*ram[NODE+0xD])&~3)
    put(ram,NODE+0x44,model)
    struct.pack_into('<4f',ram,NODE+0x80,1.,.75,.25,.1)
    struct.pack_into('<4f',ram,NODE+0xB0,*unit.tod.camera_point(ram),1.)
    struct.pack_into('<4f',ram,NODE+0xC0,.1,.2,.3,0.)
    # Slots are crafted only for registration/release boundary coverage.
    put(ram,CTX+0x210,0xFFFFFFFD if variant&2 else 17)
    for i in range(32):put(ram,CTX+0x220+128*i+12,0xFFFFFFFF)
    occupied=32 if variant==3 else 2
    for i in range(occupied):put(ram,CTX+0x220+128*i+12,i+3)
    native=(C.c_uint8*len(ram)).from_buffer_copy(ram)
    scratch=(C.c_uint8*len(spr)).from_buffer_copy(spr)
    stack=(C.c_uint8*0x100000)()
    lib.al_seed(native,scratch)
    o=EE(elf,ram=ram,spad=spr);expected=[]
    refusal=variant&1
    def signature(ee,fn):
        na,nf=SPECS[fn]
        return fn,ee.r[29],tuple(ee.r[4+i]&MASK for i in range(na)),tuple(ee.f[12+i] for i in range(nf))
    def snapshot(ee):
        return (ee.read(NODE,0x2F0),ee.read(CTX,0x2540),ee.read(DL-0x2000,0x2400),
                ee.read(0x7635C0,0x8000),ee.read(0x70003400,0x80),
                ee.read(STACK_TOP-0x10,16) if entry!=0x1F5490 else b'')
    def original_hook(fn):
        def call(ee):
            # Inline quadword copies do not cross the F5F60 host boundary.
            frame={0x1F5490:0x20,0x1C5050:0x40,0x1F5F60:0xA0,0x1D80B0:0x10}[entry]
            log=ee.r[29]==STACK_TOP-frame and not (fn==0x102948 and entry==0x1F5F60)
            if log:expected.append((signature(ee,fn),snapshot(ee)))
            if fn in (0x1C22A0,0x1C6380):
                ee.save(NODE+0x44,model);ee.save(NODE+0xD,variant,1)
                ee.r[2]=refusal if fn==0x1C22A0 else 0
                return
            save=ee.r[31];del ee.hooks[fn];ee.r[31]=RETURN
            try:ee.run(fn)
            finally:ee.hooks[fn]=call;ee.r[31]=save
        return call
    o.hooks={fn:original_hook(fn) for fn in SPECS}
    args=(NODE+0xB0,NODE+0xC0,NODE+0x80,model) if entry==0x1F5F60 else (NODE,)
    floats=(0x3F99999A,) if entry==0x1C5050 else ()
    if entry==0x1D80B0:args=(4 if variant&1 else 0xFFFFFFFE,)
    for i,a in enumerate(args):o.r[4+i]=a
    for i,f in enumerate(floats):o.f[12+i]=f
    o.r[29]=STACK_TOP;o.r[31]=RETURN;o.run(entry)
    seen=[];errors=[];active=True
    @View
    def view(_ctx,a,n,write):
        if not active and (NODE<=a<NODE+0x2F0 or 0x7F000000<=a<0x7F100000):
            errors.append('serialized actor/stack borrowed across native boundary');return None
        for base,buf in ((0,native),(0x70000000,scratch),(0x7F000000,stack)):
            if n and base<=a and n<=len(buf) and a-base<=len(buf)-n:return C.addressof(buf)+a-base
        return None
    def native_snapshot():
        return (C.string_at(C.addressof(native)+NODE,0x2F0),C.string_at(C.addressof(native)+CTX,0x2540),
                C.string_at(C.addressof(native)+DL-0x2000,0x2400),C.string_at(C.addressof(native)+0x7635C0,0x8000),
                C.string_at(C.addressof(scratch)+0x3400,0x80),
                C.string_at(C.addressof(stack)+0xEFFF0,16) if entry!=0x1F5490 else b'')
    @Worker
    def worker(_ctx,cp):
        nonlocal active
        try:
            c=cp.contents
            if entry!=0x1D80B0:
                signature=(c.function,c.sp,tuple(c.a[:c.na]),tuple(c.f[:c.nf]))
                want,state=expected[len(seen)]
                assert signature==want,(signature,want)
                assert native_snapshot()==state,('worker-entry state',hex(c.function),
                    [i for i,(x,y) in enumerate(zip(native_snapshot(),state)) if x!=y])
                seen.append(signature)
            if c.function in (0x1C22A0,0x1C6380):
                put(native,NODE+0x44,model);native[NODE+0xD]=variant
                c.v0=refusal if c.function==0x1C22A0 else 0;return 0
            service=Service();rc=lib.al_prepare(C.byref(host),cp,C.byref(service))
            if rc==0:
                active=False
                try:return lib.al_invoke(C.byref(host),C.byref(service),cp)
                finally:active=True
            assert rc==1,('prepare',hex(c.function),rc)
            return lib.al_sdk(C.byref(host),cp)
        except BaseException as error:errors.append(repr(error));return -1
    host=Host(None,view,worker)
    c=Call(function=entry,sp=STACK_TOP,na=len(args),nf=len(floats))
    for i,a in enumerate(args):c.a[i]=a
    for i,f in enumerate(floats):c.f[i]=f
    fault=C.c_uint32()
    rc=worker(None,C.pointer(c)) if entry==0x1D80B0 else lib.al_call(C.byref(host),C.byref(c),C.byref(fault))
    assert rc==0,(path.name,hex(entry),variant,errors,hex(fault.value))
    assert not errors and len(seen)==len(expected),(errors,len(seen),len(expected))
    got=bytes(native);want=bytes(o.mem)
    assert got==want,(path.name,hex(entry),variant,next((hex(i),x,y) for i,(x,y) in enumerate(zip(got,want)) if x!=y))
    assert bytes(scratch)==bytes(o.spad),(path.name,hex(entry),'scratch')
    if entry in (0x1F5490,0x1C5050):assert c.v0==o.r[2]&MASK,(hex(entry),hex(c.v0),hex(o.r[2]))
    triangles=0
    if entry==0x1F5F60:
        assert lib.al_page(DL)==1
        size=u32(got,CTX+0x1C)-DL-16
        assert lib.al_parse(DL,size,0)==-1,'default parser must remain strict'
        for offset in (0x10,0x14,0x18,0x1C,0x70,0x74,0x78,0x7C):
            saved=native[DL+offset];native[DL+offset]^=1
            assert lib.al_parse(DL,size,1)==-1,('altered light header accepted',hex(offset))
            native[DL+offset]=saved
        unit.ABE['value']=0x40
        want,_=unit.oracle(got,DL,size)
        out=(unit.Triangle*unit.MAX_TRIS)()
        triangles=lib.al_triangles(DL,out,unit.MAX_TRIS)
        assert triangles>=0 and unit.ntris(out,triangles)==want,('light VU triangles',triangles,len(want))
    return len(seen),u32(got,CTX+0x1C)-DL if entry==0x1F5F60 else 0,triangles

def contracts(lib):
    accesses=[];calls=[]
    @View
    def denied(_ctx,a,n,w):accesses.append((a,n,w));return None
    @Worker
    def refused(_ctx,cp):calls.append(cp.contents.function);return -1
    host=Host(None,denied,refused);cases=0
    for fn,na,nf,sp,where in ((0xDEADC0DE,1,0,STACK_TOP,0xDEADC0DE),
                             (0x1F5490,0,0,STACK_TOP,0x1F5490),
                             (0x1F5F60,4,0,0x90,0x1F5F60),
                             (0x1C5050,1,1,STACK_TOP,STACK_TOP-4),
                             (0x1F5F60,4,0,STACK_TOP,0x275670),
                             (0x1F5490,1,0,STACK_TOP,0x1C22A0)):
        accesses.clear();calls.clear();c=Call(function=fn,na=na,nf=nf,sp=sp);c.a[0]=NODE
        fault=C.c_uint32()
        assert lib.al_call(C.byref(host),C.byref(c),C.byref(fault))==-1 and fault.value==where
        before=list(accesses),list(calls);c.function=0x1F5490;c.na=1;c.nf=0;c.sp=STACK_TOP
        assert lib.al_call(C.byref(host),C.byref(c),C.byref(fault))==-1 and fault.value==where
        assert (accesses,calls)==before,'latched call had side effects'
        cases+=1
    for fn,na,nf,args in ((0x1D7FA0,3,2,(NODE,NODE+0x80,2)),
                          (0x1D7FA0,3,2,(NODE+1,NODE+0x80,2)),
                          (0x1D7FA0,2,2,(NODE,NODE+0x80,2)),
                          (0x1D3990,1,0,(NODE,)),
                          (0x1CAAC0,3,0,(NODE,DL,CTX+16)),
                          (0x1CAAC0,3,0,(NODE,DL,CTX)),
                          (0x1D80B0,1,1,(1,))):
        c=Call(function=fn,na=na,nf=nf);s=Service()
        for i,a in enumerate(args):c.a[i]=a
        assert lib.al_prepare(C.byref(host),C.byref(c),C.byref(s))==-1
        cases+=1
    c=Call(function=0xDEADC0DE);s=Service()
    assert lib.al_prepare(C.byref(host),C.byref(c),C.byref(s))==1
    s.function=0x1D7FA0
    assert lib.al_invoke(C.byref(host),C.byref(s),C.byref(c))==-1
    return cases+2

def main():
    started=time.monotonic();lib=build();elf=read_elf()
    unit.ELF=unit.vc.ELF=unit.tod.ELF=elf
    paths=[ARRIVAL]+sorted(p.parent for p in (DECOMP/'build/s87/route_a01').glob('a01_*/eeMemory.bin')
                          if p.read_bytes()[0x810700:0x810702]==b'\1\0')
    paths=paths if mode.FULL else paths[:1]
    cases=boundaries=packets=triangles=0
    for path in paths:
        for fn in (0x1F5490,0x1C5050,0x1F5F60,0x1D80B0):
            for variant in range(4):
                n,b,t=run(lib,elf,path,fn,variant);cases+=1;boundaries+=n;packets+=b;triangles+=t
    refusals=contracts(lib)
    result=dict(status='PASS',mode=mode.MODE,captures=len(paths),cases=cases,
                worker_boundaries=boundaries,packet_bytes=packets,vu_triangles=triangles,
                refusal_contracts=refusals,parser_refusals=len(paths)*4*9,
                seconds=round(time.monotonic()-started,2))
    (OUT/(mode.MODE+'.json')).write_text(json.dumps(result,indent=2)+'\n')
    print('AREA01 light live:',json.dumps(result,sort_keys=True))
if __name__=='__main__':main()
