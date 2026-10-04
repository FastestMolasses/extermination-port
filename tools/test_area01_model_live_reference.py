#!/usr/bin/env python3
"""Generic AREA01 model workers against original code, on captured resources.

The native fixture exposes the actual actor-view adapter and the sole shared
slot arena. Its raw RAM input is test-only; the production source contract
is the canonical loader. Head/shadow services and rand are explicit recorded
boundaries; all model, allocation, pose, face-setup and release logic runs
existing translations. No original bytes are embedded or written to reports.
"""
import ctypes as C
import json
from pathlib import Path
import struct
import subprocess
import time
import reference_mode as mode
from test_coll_move_reference import FloatEE
from test_module_loader_reference import read_elf_bytes
from test_pose_host_workers_reference import SOURCES as POSE_SOURCES

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/level2/model-live'
DECOMP=ROOT.parent/'Extermination'
NODE=0x7B0390
SIZE=0x2F0

def u32(data,at):return struct.unpack_from('<I',data,at)[0]

def build():
    OUT.mkdir(parents=True,exist_ok=True)
    sources=['tests/area01_model_bridge.c','src/game/em_area01_model_live.c',
             'src/game/em_area01_actor_view.c','src/game/em_actor_pool.c',
             'src/game/em_area01_math_core.c','src/game/em_area01_math_owner.c',
             'src/game/em_area01_math_actor.c',
             'src/game/em_owner_draw_original.c','src/game/em_roger_actor_original.c',
             'src/game/em_startup_load_gaps.c','src/game/em_opening_face.c',
             'src/game/em_render_verify_rest.c','src/game/em_anim_runtime_rest.c',*POSE_SOURCES]
    target=OUT/'bridge.dylib'
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off',
                    '-shared','-fPIC','-Isrc','-Wl,-dead_strip','-Wl,-exported_symbol,_am_*',
                    *dict.fromkeys(sources),'-o',str(target)],cwd=ROOT,check=True)
    lib=C.CDLL(str(target))
    lib.am_seed.argtypes=[C.c_void_p,C.c_void_p,C.c_uint32,C.c_int]
    lib.am_call.argtypes=[C.c_uint32]*4+[C.c_float,C.c_float,C.POINTER(C.c_uint32)]
    lib.am_record.restype=C.c_void_p
    lib.am_bank.argtypes=[C.c_char_p,C.c_uint32]
    lib.am_failure_boundary.argtypes=[C.c_uint32,C.c_uint32,C.c_int]
    lib.am_fault.restype=C.c_uint32
    lib.am_special_bones.argtypes=[C.c_uint32]*5
    lib.am_release_pool.argtypes=[C.c_uint32,C.c_int]
    lib.am_box_bone_change.argtypes=[C.c_uint32,C.c_uint32]
    lib.am_pose69_call.argtypes=[C.c_uint32,C.c_int]
    lib.am_pose69_box.argtypes=[C.c_uint32,C.c_int]
    lib.am_pose69_box_prepare.argtypes=[C.c_uint32]
    lib.am_pose69_missing.argtypes=[C.c_uint]
    return lib

def check(lib,elf,ram,spad,node,fn,a1=0,a2=0,f12=0.,f13=0.,slots=True,alter=None,library_owner=False,active=False):
    ram=bytearray(ram)
    if alter:alter(ram)
    native=C.create_string_buffer(bytes(ram));scratch=C.create_string_buffer(spad)
    assert lib.am_seed(native,scratch,node,int(slots))==0
    if library_owner:lib.am_library_owner()
    if fn in (0x1B0FD0,0x1B0EA0):
        assert lib.am_bank(str(ROOT/'assets/area01/world_models.emwm').encode(),u32(ram,0x28A59C))==0
    lib.am_snapshot()
    initial=native.raw[:-1]
    o=FloatEE(elf,initial,spad)
    external=[];random=[]
    for address in (0x1F0120,0x1DA6A0,0x1BA7F0):
        o.hooks[address]=lambda ee, a=address:external.append((a,ee.r[4],ee.r[5]))
    def rnd(ee):random.append(7);ee.r[2]=7
    o.hooks[0x122BB8]=rnd
    if fn==0x1D0C80:
        o.call(fn,(node,a1))
        o.call(0x1D0D40,(node,a2,0x80000001,0x101))
    else:o.call(fn,(node,a1,a2),(f12,f13))
    out=C.c_uint32()
    if fn==0x1D0C80:rc=lib.am_special_bones(node,a1,a2,0x80000001,0x101)
    elif fn==0x1C69A0:rc=lib.am_pose69_call(node,int(active))
    else:rc=lib.am_call(fn,node,a1,a2,f12,f13,C.byref(out))
    assert rc==0,(hex(fn),hex(node),hex(lib.am_fault()))
    lib.am_snapshot()
    got=native.raw[:-1]
    want=bytes(o.mem)
    assert got==want,(hex(fn),hex(node),next((hex(i),x,y) for i,(x,y) in enumerate(zip(got,want)) if x!=y))
    assert scratch.raw[:-1]==bytes(o.spad),(hex(fn),'scratch',next((hex(i),x,y) for i,(x,y) in enumerate(zip(scratch.raw[:-1],o.spad)) if x!=y))
    assert lib.am_external_calls()==len(external)
    assert lib.am_random_calls()==len(random)
    if fn in (0x1B1020,0x1B10B0,0x1B0FD0,0x1B0EA0,0x1CA700,0x1C64F0):
        assert out.value==o.r[2]&0xffffffff,(hex(fn),'result',out.value,o.r[2])
    return len(random),len(external)

def pose69_cases(lib,elf,paths):
    """No math worker is scripted: the original C69A0 and all four leaves
    run from capture storage or an actual original actor's call boundary."""
    import test_area01_math_reference as math_oracle
    route=unit=typed=contracts=0;no_route=[]
    class Stop(Exception):pass
    for path in paths:
        ram=path.read_bytes();spad=(path.parent/'scratchpad.bin').read_bytes()
        def caught(fn,memory,scratch,regs,where):
            nonlocal route
            check(lib,elf,memory,bytes(scratch),regs[0][4]&0xffffffff,fn,active=True)
            route+=1
        catches=math_oracle.catch_route(elf,path.parent.name,math_oracle.ACTOR_OWNERS,
                  {0x1C69A0},caught,node_limit=None if mode.FULL else 1)
        if not catches:no_route.append(path.parent.name)
        nodes=[a for a in range(0x7A5640,0x7D4640,SIZE)
               if ram[a] and ram[a+12]>=2 and u32(ram,a+16) in (0x128C10,0x825350)]
        assert nodes,('no captured animated actor',path.parent.name)
        node=nodes[0]
        bones=[u32(ram,node+0x110+4*k) for k in range(ram[node+12])]
        def mutate(kind):
            def apply(b):
                if kind=='zero':b[node+12]=0
                elif kind=='one':b[node+12]=1
                elif kind=='negative parent':
                    struct.pack_into('<h',b,bones[-1]+0x64,-2)
                    struct.pack_into('<I',b,node+0x108,bones[0])
                elif kind=='self':struct.pack_into('<h',b,bones[-1]+0x64,len(bones)-1)
                elif kind=='forward':struct.pack_into('<h',b,bones[0]+0x64,1)
                elif kind=='scales':
                    struct.pack_into('<3f',b,node+0x60,1.25,.5,-.75)
                    for i,bone in enumerate(bones):
                        struct.pack_into('<3f',b,bone+0x18,.5,1.5,2.)
                        struct.pack_into('<3f',b,bone+0x70,.73,-1.25,2.2)
                        struct.pack_into('<3f',b,bone+0x7C,1.5+i,-2.25-i,3.75)
                        struct.pack_into('<3h',b,bone+0x88,-32768,4096,32767)
                elif kind=='saturation':
                    struct.pack_into('<3I',b,node+0x60,0x7F800000,0xFFC00001,0xFF7FFFFF)
                    for bone in bones:
                        struct.pack_into('<3I',b,bone+0x18,0x7F800000,0x7FC00001,0xFF7FFFFF)
                        struct.pack_into('<3I',b,bone+0x7C,0x7F800000,0xFFC00001,0x7F7FFFFF)
                else:
                    sign,t=kind
                    for bone in bones:
                        struct.pack_into('<4f',b,bone+0x30,.25,-.5,.75,1.)
                        struct.pack_into('<4f',b,bone+0x40,sign*.5,sign*-.25,sign*.625,sign*1.25)
                        struct.pack_into('<f',b,bone+0x50,t)
            return apply
        kinds=['zero','one','negative parent','self','forward','scales','saturation']
        kinds += [(sign,t) for sign in (-1,1) for t in (-.5,0.,.5,1.,1.5)]
        for i,kind in enumerate(kinds):
            check(lib,elf,ram,spad,node,0x1C69A0,alter=mutate(kind),active=bool(i&1))
            unit+=1
        for active in (0,1):
            native=C.create_string_buffer(ram);scratch=C.create_string_buffer(spad)
            assert lib.am_seed(native,scratch,node,1)==0
            assert lib.am_pose69_box_prepare(node)==0
            lib.am_snapshot();o=FloatEE(elf,native.raw[:-1],spad)
            o.save(bones[1]+0x78,0x3F400000)
            o.call(0x1C69A0,(node,))
            assert lib.am_pose69_box(node,active)==0,hex(lib.am_fault())
            lib.am_snapshot()
            assert native.raw[:-1]==bytes(o.mem),('C69A0 typed/raw slot continuity',
                next((hex(i),x,y) for i,(x,y) in enumerate(zip(native.raw[:-1],o.mem)) if x!=y))
            assert scratch.raw[:-1]==bytes(o.spad),'C69A0 typed/raw scratch'
            typed+=1
    # Each absent canonical window faults exactly when first needed. Compare
    # every byte with the original prefix through that same instruction or
    # leaf boundary, then call again to ensure the fault is sticky.
    for active in (0,1):
        for span,address,stop in ((0,0x70003400,0x1C69A0),(1,0x70003440,0x1CA1C0),
                                  (2,0x70003480,0x1029C0),(3,0x70003600,0x1CA0A0),
                                  (4,0x70003760,0x1CA1C0)):
            native=C.create_string_buffer(ram);scratch=C.create_string_buffer(spad)
            assert lib.am_seed(native,scratch,node,1)==0
            lib.am_snapshot();o=FloatEE(elf,native.raw[:-1],spad)
            def stop_here(ee):raise Stop()
            o.hooks[stop]=stop_here
            try:o.call(0x1C69A0,(node,))
            except Stop:pass
            else:raise AssertionError(('missing prefix stop',hex(stop)))
            assert lib.am_pose69_missing(span)==0
            assert lib.am_pose69_call(node,active)==-1 and lib.am_fault()==address,(span,hex(lib.am_fault()))
            lib.am_snapshot();before=native.raw,scratch.raw
            assert before[0][:-1]==bytes(o.mem) and before[1][:-1]==bytes(o.spad),('missing window prefix',hex(address))
            assert lib.am_pose69_call(node,active)==-1 and lib.am_fault()==address
            lib.am_snapshot();assert before==(native.raw,scratch.raw),'fault made later writes'
            contracts+=1
        # A missing rest span is never touched for a zero-bone model.
        data=bytearray(ram);data[node+12]=0
        native=C.create_string_buffer(bytes(data));scratch=C.create_string_buffer(spad)
        assert lib.am_seed(native,scratch,node,1)==0 and lib.am_pose69_missing(2)==0
        lib.am_snapshot();o=FloatEE(elf,native.raw[:-1],spad);o.call(0x1C69A0,(node,))
        assert lib.am_pose69_call(node,active)==0
        lib.am_snapshot();assert native.raw[:-1]==bytes(o.mem) and scratch.raw[:-1]==bytes(o.spad)
        contracts+=1
    assert route,'no original C69A0 actor call caught'
    return dict(route_calls=route,field_cases=unit,typed_continuity=typed,fault_contracts=contracts,
                captures_without_actor_call=no_route)

def main():
    start=time.time();lib=build();elf=read_elf_bytes()
    paths=sorted((DECOMP/'build/s87/route_a01').glob('a01_*/eeMemory.bin'))
    paths=[p for p in paths if p.read_bytes()[0x810700:0x810702]==b'\1\0']
    paths=paths if mode.FULL else [paths[0]]
    cases=0;random=external=0
    for path in paths:
        ram=path.read_bytes();spad=(path.parent/'scratchpad.bin').read_bytes()
        node=next(a for a in range(0x7A5640,0x7D4640,SIZE) if ram[a] and u32(ram,a+0x10)==0x825350)
        tests=[(node,0x1D0C80,u32(ram,0x28A490+0x20*4),u32(ram,0x28A490+0x21*4),0.,0.,False,None)]
        for kind in list(range(15))+[0xffffffff,0x80000000]:
            tests.append((node,0x1CA5E0,0x12345678,kind,0.,0.,True,None))
        for model,anim in ((0x47,0x4A),(0x20,0x21),(0x47,0xffffffff)):
            tests.append((node,0x1B10B0,model,anim,0.,0.,False,None))
        for fn in (0x1C6380,0x1C62C0,0x1C63E0,0x1C68C0,0x1C69A0,0x1C64F0,0x1CA6F0,0x1D06D0,0x1D06E0,0x1BA540,0x1CA770,0x1AF800):
            tests.append((node,fn,0,0,1.,0.,True,None))
        def no_bank(b):struct.pack_into('<I',b,node+0x40,0xFFFFFFFF)
        tests.append((node,0x1C68C0,0,0,0.,0.,True,no_bank))
        for clip,blend,frame in ((0,0.,0.),(1,20.,0.),(2,0.,3.)):
            tests.append((node,0x1C67E0,clip,0,blend,frame,True,None))
        def no_face(b):struct.pack_into('<Ih',b,node+0x90,0,-1)
        tests.extend([(node,0x1BA8E0,0x47,0,0.,0.,True,no_face),
                      (node,0x1CA700,u32(ram,0x28A490+0x4D*4),7,0.,0.,True,no_face),
                      (node,0x1BA580,0x47,0,0.,0.,True,None)])
        for id_ in (0,4,21):
            def fresh(b,id_=id_):b[node+9]=b[node+0xC]=b[node+4]=0;b[node+0xD]=id_
            for fn in (0x1B0FD0,0x1B0EA0):tests.append((node,fn,0,0,0.,0.,False,fresh))
        for model_id in (0,0x34,0x40,0x5B):
            def fresh_library(b):b[node+9]=b[node+0xC]=b[node+4]=0
            tests.append((node,0x1B1020,model_id,0xFFFFFFFF,0.,0.,False,fresh_library))
        for args in tests:
            r,e=check(lib,elf,ram,spad,*args);random+=r;external+=e;cases+=1
    box_mutation_cases=0
    gun=next(a for a in range(0x7A5640,0x7D4640,SIZE) if u32(ram,a+16)==0x826D40)
    for angle in (0x3F400000,0xBF542D5B):
        data=bytearray(ram);data[gun+9]=data[gun+0xC]=data[gun+4]=0
        native=C.create_string_buffer(bytes(data));scratch=C.create_string_buffer(spad)
        assert lib.am_seed(native,scratch,gun,0)==0
        assert lib.am_bank(str(ROOT/'assets/area01/world_models.emwm').encode(),u32(ram,0x28A59C))==0
        lib.am_snapshot();o=FloatEE(elf,native.raw[:-1],scratch.raw[:-1])
        o.call(0x1B0FD0,(gun,));o.save(o.load(gun+0x118,4)+0x78,angle)
        o.call(0x1C6380,(gun,))
        assert lib.am_box_bone_change(gun,angle)==0,hex(lib.am_fault())
        lib.am_snapshot();got=native.raw[:-1];want=bytes(o.mem)
        assert got==want,('box parent store',next((hex(i),x,y) for i,(x,y) in enumerate(zip(got,want)) if x!=y))
        assert scratch.raw[:-1]==bytes(o.spad)
        box_mutation_cases+=1
    release_cases=library_owner_cases=0
    for reuse in (0,1):
        native=C.create_string_buffer(ram);scratch=C.create_string_buffer(spad)
        assert lib.am_seed(native,scratch,node,1)==0
        lib.am_pool_snapshot()
        o=FloatEE(elf,native.raw[:-1],spad)
        o.call(0x1AFC10,(node,))
        if reuse:o.call(0x1AFA90,(7,))
        assert lib.am_release_pool(node,reuse)==0,('release',reuse,hex(lib.am_fault()))
        got=native.raw[:-1];want=bytes(o.mem)
        assert got==want,('release',reuse,next((hex(i),x,y) for i,(x,y) in enumerate(zip(got,want)) if x!=y))
        assert scratch.raw[:-1]==bytes(o.spad)
        release_cases+=1
    for id_ in (0x34,0x72):
        check(lib,elf,ram,spad,node,0x1B1020,id_,0xFFFFFFFF,slots=False,
              alter=fresh_library,library_owner=True)
        library_owner_cases+=1
    failure_boundaries=0
    library=u32(ram,0x28A56C)
    handle=library+(u32(ram,library+4)&~3)
    for active,hole in ((0,library+4),(1,library+4),(0,handle),(1,handle)):
        native=C.create_string_buffer(ram);scratch=C.create_string_buffer(spad)
        assert lib.am_seed(native,scratch,node,0)==0
        assert lib.am_failure_boundary(node,hole,active)==0
        failure_boundaries+=1
    pose69=pose69_cases(lib,elf,paths)
    report=dict(pose69=pose69,box_mutation_cases=box_mutation_cases,release_cases=release_cases,library_owner_cases=library_owner_cases,
                failure_boundaries=failure_boundaries,status='PASS',mode='full' if mode.FULL else 'quick',captures=len(paths),
                cases=cases,random_boundaries=random,external_boundaries=external,seconds=round(time.time()-start,2))
    (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    mode.banner(f'{cases} model/pose/face cases over {len(paths)} captures')
    print('AREA01 model live:',json.dumps(report,sort_keys=True))
if __name__=='__main__':main()
