#!/usr/bin/env python3
"""Generic AREA01 record adapter plus the existing live owner draw, headless.
No draw, light, face, veil or packet worker is replaced with expected output.
The render-context memory provider is a test view over captured inputs.
"""
import ctypes as C
import json
from pathlib import Path
import struct
import subprocess
import time
import reference_mode as mode
from test_anim_runtime_rest_reference import EE
from test_face_attach_reference import SOURCES
from test_module_loader_reference import read_elf_bytes
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/level2/model-draw'
DECOMP=ROOT.parent/'Extermination'
def u32(b,a):return struct.unpack_from('<I',b,a)[0]
def build(fixture='tests/area01_model_draw_bridge.c',prefix='ad',extra=()):
    OUT.mkdir(parents=True,exist_ok=True)
    sources=[fixture,'src/game/em_area01_model_draw.c',
             'src/game/em_area01_model_live.c','src/game/em_area01_actor_view.c',
             'src/game/em_render_verify_rest.c',
             'src/game/em_actor_pool.c','src/game/em_roger_actor_original.c',
             'src/game/em_startup_load_gaps.c','src/game/em_opening_face.c',
             'src/game/em_object_unit.c','src/game/em_packet_chain_original.c',
             'src/game/em_area01_math_core.c','src/game/em_area01_math_owner.c',
             'src/game/em_area01_math_actor.c',*SOURCES,*extra]
    target=OUT/f'{prefix}_bridge.dylib'
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off',
        '-shared','-fPIC','-Isrc','-Wl,-dead_strip',f'-Wl,-exported_symbol,_{prefix}_*',
        *dict.fromkeys(sources),'-o',str(target)],cwd=ROOT,check=True)
    lib=C.CDLL(str(target))
    if prefix=='ad':
        lib.ad_seed.argtypes=[C.c_void_p,C.c_void_p,C.c_uint32]
        lib.ad_call.argtypes=[C.c_uint32,C.c_uint32]
        lib.ad_pose.argtypes=[C.c_uint32]
        lib.ad_failure_boundary.argtypes=[C.c_uint32,C.c_uint32,C.c_uint32,C.c_int]
    return lib
def check(lib,elf,ram,spad,node):
    ram=bytearray(ram)
    ctx=u32(ram,0x275670);assert ctx==0x811CC0
    struct.pack_into('<I',ram,ctx+0x10,0x740000)
    for a,v in ((0x275B48,node),(0x275B44,node),(0x275B40,node+0x110)):
        struct.pack_into('<I',ram,a,v)
    native=C.create_string_buffer(bytes(ram));scratch=C.create_string_buffer(spad)
    assert lib.ad_seed(native,scratch,node)==0
    lib.ad_snapshot();initial=native.raw[:-1]
    oracle=EE(elf,ram=initial,spad=spad)
    function=u32(ram,node+0x4C)
    cross=function==0x1CB360 or u32(ram,node+0x10)==0x825350
    if cross:
        oracle.invoke(0x1C68C0,(node,))
        assert lib.ad_pose(node)==0
    oracle.invoke(function,(node,))
    assert lib.ad_call(function,node)==0,(hex(node),hex(lib.ad_fault()),hex(u32(ram,node+0x44)),ram[node+0xC])
    if cross:
        oracle.invoke(0x1C68C0,(node,))
        assert lib.ad_pose(node)==0
    lib.ad_snapshot();got=native.raw[:-1];want=bytes(oracle.mem)
    assert got==want,(hex(node),next((hex(i),x,y) for i,(x,y) in enumerate(zip(got,want)) if x!=y))
    assert scratch.raw[:-1]==bytes(oracle.spad),(hex(node),'scratch',next((hex(i),x,y) for i,(x,y) in enumerate(zip(scratch.raw[:-1],oracle.spad)) if x!=y))
    return lib.ad_units(),u32(native.raw,ctx+0x10)-0x740000,native.raw[:-1],int(cross)
def main():
    start=time.time();lib=build();elf=read_elf_bytes()
    paths=sorted((DECOMP/'build/s87/route_a01').glob('a01_*/eeMemory.bin'))
    paths=[p for p in paths if p.read_bytes()[0x810700:0x810702]==b'\1\0']
    paths=paths if mode.FULL else [paths[0]]
    cases=units=packets=crosses=0; morph=[]
    for path in paths:
        ram=path.read_bytes();spad=(path.parent/'scratchpad.bin').read_bytes()
        nodes=[a for a in range(0x7A5640,0x7D4640,0x2F0) if ram[a] and ram[a+0xC] and u32(ram,a+0x4C) in (0x1CAA00,0x1CB360) and u32(ram,a+0x44)]
        nodes=nodes if mode.FULL else [a for a in nodes if u32(ram,a+0x90)]
        for node in nodes:
            u,p,image,cross=check(lib,elf,ram,spad,node);cases+=1;units+=u;packets+=p;crosses+=cross
            if u32(ram,node+0x4C)==0x1CB360:morph.append((image,p,ram[node+0xC]))
    import test_object_unit_reference as unit
    import test_vu1_object_clip_reference as clip
    import test_owner_draw_reference as draw
    unit.ELF=clip.ELF=draw.ELF=elf
    unit.OUT=OUT/'units';unit.LIB=unit.build_library()
    triangles=0
    for image,size,bones in morph:
        packet=image[0x740000:0x740000+size]
        want,blocks=unit.oracle(image,0x740000,size)
        n,out,objects,pieces,why=unit.native(image,packet)
        assert n>=0,why
        assert unit.ntris(out,n)==want,('morph VU1',n,len(want))
        assert pieces[0]==bones and pieces[1]==blocks[unit.FACE_KERNEL]
        triangles+=n
    failures=0
    for active in (0,1):
        for function,hole in ((0x1CAA00,u32(ram,node+0x44)),(0x1CAAA0,0)):
            memory=C.create_string_buffer(ram);scratch=C.create_string_buffer(spad)
            assert lib.ad_seed(memory,scratch,node)==0
            assert lib.ad_failure_boundary(node,function,hole,active)==0
            failures+=1
    result=dict(failure_boundaries=failures,status='PASS',mode='full' if mode.FULL else 'quick',captures=len(paths),cases=cases,
                units=units,packet_bytes=packets,morph_vu1_cases=len(morph),morph_triangles=triangles,
                pose_draw_pose_sequences=crosses,
                seconds=round(time.time()-start,2))
    (OUT/'report.json').write_text(json.dumps(result,indent=2)+'\n')
    mode.banner(f'{cases} generic record draw cases')
    print('AREA01 model draw:',json.dumps(result,sort_keys=True))
if __name__=='__main__':main()
