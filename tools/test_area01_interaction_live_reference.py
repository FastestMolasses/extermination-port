#!/usr/bin/env python3
"""AREA01 canonical Use candidates and scan against the original instructions.

Ray queries are explicit worker boundaries. Full RAM/scratch changes, worker
arguments, score at the boundary, return and winner/claim ordering are checked.
"""
import ctypes as C
import json
import math
from pathlib import Path
import struct
import subprocess
import time
from test_coll_move_reference import FloatEE
from test_module_loader_reference import read_elf_bytes
import reference_mode as mode
import reference_root as refs  # noqa: E402  (EM_REFERENCE: fork by default)
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/level2/interaction'
DECOMP=ROOT.parent/'Extermination'
PLAYER=0x900000

def u32(b,a):return struct.unpack_from('<I',b,a)[0]
def build():
    OUT.mkdir(parents=True,exist_ok=True)
    target=OUT/'bridge.dylib'
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off',
        '-shared','-fPIC','-Isrc','-Wl,-dead_strip','-Wl,-exported_symbol,_ai_*',
        'tests/area01_interaction_bridge.c','src/game/em_area01_interaction_live.c',
        'src/game/em_interaction_scan.c','src/game/em_door_candidate.c',
        'src/game/em_roger.c','src/game/em_item_sdk_math.c','-o',str(target)],cwd=ROOT,check=True)
    lib=C.CDLL(str(target));lib.ai_seed.argtypes=[C.c_void_p,C.c_void_p,C.c_int]+[C.c_uint32]*3
    lib.ai_call.argtypes=[C.c_uint32]*3+[C.POINTER(C.c_uint32)]
    lib.ai_query.argtypes=[C.c_uint];lib.ai_query.restype=C.c_void_p
    return lib

def check(lib,elf,ram,spr,node,action=0,hit=(0,0,0,0),distance=1.,scan=False,gates=(0,0,0),scan_nodes=None):
    ram=bytearray(ram);spr=bytearray(spr)
    selector=ram[node+8];desc=u32(ram,node+0x30)
    pos=struct.unpack_from('<3f',ram,desc if selector==1 else node+0xB0)
    point=(pos[0],pos[1],pos[2]-distance)
    struct.pack_into('<4f',ram,PLAYER+0xA0,*point,2. if action==0x2D else 1.)
    yaw=struct.unpack_from('<f',ram,node+0xC4)[0]+math.pi
    while yaw>math.pi:yaw-=2*math.pi
    struct.pack_into('<f',ram,PLAYER+0xC4,yaw);ram[PLAYER+0x1F0]=action
    struct.pack_into('<4f',ram,0x8105E0,*pos,1.)
    struct.pack_into('<f',spr,0x3B98,91.25)
    spr[0x3B8D]=gates[0];struct.pack_into('<h',ram,0x28A9A0,gates[1]);ram[0x8106EF]=gates[2]
    if scan:
        selected=scan_nodes if scan_nodes is not None else [node]
        struct.pack_into('<I',ram,0x275B5C,0x940000);struct.pack_into('<h',ram,0x275B64,len(selected))
        for i,a in enumerate(selected):
            ram[a]=1;ram[a+2]|=0x80;ram[a+11]=0
            struct.pack_into('<I',ram,0x940000+4*i,a)
    native=C.create_string_buffer(bytes(ram));scratch=C.create_string_buffer(bytes(spr))
    assert lib.ai_seed(native,scratch,*hit)==0
    o=FloatEE(elf,ram,spr);queries=[]
    def query(ee):
        a,b,c=(ee.r[k]&0xFFFFFFFF for k in (4,5,6))
        def read(a,n):return bytes(ee.spad[a-0x70000000:a-0x70000000+n]) if a>=0x70000000 else bytes(ee.mem[a:a+n])
        queries.append(struct.pack('<3I',a,b,c)+read(a,16)+read(b,16)+read(0x70003B98,4))
        ee.r[2]=hit[0]
        struct.pack_into('<I',ee.spad,0x31D0,0x950000);struct.pack_into('<H',ee.mem,0x95001A,hit[1])
        struct.pack_into('<II',ee.spad,0x31D4,hit[3],hit[2])
    o.hooks[0x19A910]=query
    fn=0x184BA0 if scan else 0x183EF0
    o.call(fn,(PLAYER,node))
    out=C.c_uint32();rc=lib.ai_call(fn,PLAYER,node,C.byref(out))
    assert rc==0,(hex(node),hex(u32(ram,node+16)),action,hex(lib.ai_fault()))
    assert out.value==o.r[2]&0xFFFFFFFF,(hex(node),'result',action,out.value,o.r[2])
    assert lib.ai_queries()==len(queries)
    for i,q in enumerate(queries):assert C.string_at(lib.ai_query(i),48)==q,('query',hex(node),action,i)
    want=bytes(o.mem);got=native.raw[:-1]
    assert got==want,('RAM',hex(node),action,next((hex(i),x,y) for i,(x,y) in enumerate(zip(got,want)) if x!=y))
    want=bytes(o.spad);got=scratch.raw[:-1]
    assert got==want,('scratch',hex(node),action,next((hex(i),x,y) for i,(x,y) in enumerate(zip(got,want)) if x!=y))
    assert lib.ai_claims()==int(scan and out.value!=0)
    return len(queries),lib.ai_claims()

def main():
    start=time.time();lib=build();elf=read_elf_bytes()
    paths=sorted((refs.capture('s87/route_a01')).glob('a01_*/eeMemory.bin'))
    if not mode.FULL:paths=[paths[0]]
    # AREA02 (the revisit's door 16 arrival and the a02 route): the bed
    # 00159620 (selector 5), the 0x2C panel 00159210 (its line of sight),
    # and the selector-2 owners where a capture holds them.
    a02=[refs.capture('s87/route_a01r')/'a01r_03_door16/eeMemory.bin']
    a02+=sorted(refs.capture('s87/route_a02').glob('a02_*/eeMemory.bin'))
    paths+=[a for a in a02 if a.exists()]
    cases=queries=claims=0;forms=set()
    for path in paths:
        ram=path.read_bytes();spr=(path.parent/'scratchpad.bin').read_bytes();seen=set();nodes=[]
        for node in range(0x7A5640,0x7D4640,0x2F0):
            form=(ram[node+2]&31,ram[node+3],ram[node+8],u32(ram,node+16))
            if ram[node] and ram[node+2]&0x80 and u32(ram,node+0x30) and form not in seen:
                seen.add(form);forms.add(form);nodes.append(node)
        for node in nodes:
            options=[(0,(0,0,0,0),1.,False),(0,(0,0,0,0),1.,True)]
            special=ram[node+8] in (2,5) or (ram[node+2]&31==4 and ram[node+3]==0x2C)
            if mode.FULL or special:options += [(0,(0,0,0,0),40.,False),(0,(1,0x2800,2,node),1.,True),
                                                (0,(1,0x0800,0,0),1.,False),(0,(1,0x0100,0,0),1.,False)]
            if ram[node+2]&31==7:
                options += [(0x2D,(0,0,0,0),2.,False),(0x2D,(1,0x2000,0,0),2.,False)]
            for action,hit,distance,scan in options:
                q,c=check(lib,elf,ram,spr,node,action,hit,distance,scan)
                cases+=1;queries+=q;claims+=c
        if nodes:
            for selected in (nodes,list(reversed(nodes)),[]):
                q,c=check(lib,elf,ram,spr,nodes[0],scan=True,scan_nodes=selected)
                cases+=1;queries+=q;claims+=c
            for gates in ((1,0,0),(0,1,0),(0,0,1)):
                q,c=check(lib,elf,ram,spr,nodes[0],scan=True,gates=gates)
                cases+=1;queries+=q;claims+=c
    # Selector 2 (no captured owner holds it): the bed of the first AREA02
    # capture with +8 = 2 and a descriptor (its x, y + 1, z, 3.0, 2.0) in
    # zero RAM, both gates passing and failing.
    path=next(a for a in paths if 'a01r_03' in str(a) or 'a02_' in str(a))
    ram=bytearray(path.read_bytes());spr=(path.parent/'scratchpad.bin').read_bytes();bed=0x7A5F10
    assert u32(ram,bed+16)==0x159620,('the bed node',hex(u32(ram,bed+16)))
    x,y,z=struct.unpack_from('<3f',ram,bed+0xB0);ram[bed+8]=2
    struct.pack_into('<I',ram,bed+0x30,0x1E00000);struct.pack_into('<5f',ram,0x1E00000,x,y+1.,z,3.,2.)
    for distance in (1.,2.9,3.1,40.):
        for scan in (False,True):
            q,c=check(lib,elf,bytes(ram),spr,bed,0,(0,0,0,0),distance,scan)
            cases+=1;queries+=q;claims+=c
    forms.add((ram[bed+2]&31,ram[bed+3],2,0x159620))
    special=sorted({f'{f[0]}/{f[1]:x}/{f[2]}' for f in forms if f[2] in (2,5) or (f[0]==4 and f[1]==0x2C)})
    assert any(f.endswith('/5') for f in special) and any('/2c/' in f for f in special),('AREA02 forms',special)
    report=dict(status='PASS',mode='full' if mode.FULL else 'quick',captures=len(paths),special_forms=special,
        forms=len(forms),cases=cases,ray_boundaries=queries,claim_boundaries=claims,seconds=round(time.time()-start,2))
    (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    mode.banner(f'{cases} AREA01 candidate/scan cases');print(json.dumps(report,sort_keys=True))
if __name__=='__main__':main()
