#!/usr/bin/env python3
"""Sparse canonical shared services against original boot instructions."""
import ctypes as C
import hashlib
from pathlib import Path
import random
import struct
import subprocess
from reference_mode import MODE,pick
from test_player_misc_workers_reference import MiscEE,F
from test_actor_pool_reference import ELF_SHA256
from test_actor_collision_reference import LIST_BASE,LIST_LIVE,LIST_CAP
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/level2/shared-services'
def main():
    OUT.mkdir(parents=True,exist_ok=True)
    sources=['tests/area01_shared_services_bridge.c','src/game/em_area01_shared_services.c',
             'src/game/em_area01_scene_view.c','src/game/em_panel.c','src/game/em_director_original.c','src/game/em_actor_roster.c',
             'src/game/em_player_misc_workers.c','src/game/em_player_stage_workers.c','src/game/em_random.c',
             'src/game/em_script_host_workers.c','src/game/em_script.c','src/game/em_sdk_math_original.c',
             'src/game/em_owner_services_original.c','src/game/em_effect_original.c',
             'src/game/em_coll_probe_original.c','src/game/em_actor_collision.c',
             'src/game/em_collision.c','src/game/em_actor_pool.c']
    lib=OUT/'bridge.dylib'
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC','-ffp-contract=off',
                    '-Isrc',*sources,'-lm','-o',str(lib)],cwd=ROOT,check=True)
    n=C.CDLL(str(lib));n.ss_init.argtypes=[C.c_void_p,C.c_uint32]
    n.ss_bytes.argtypes=[C.c_uint32,C.c_uint32];n.ss_bytes.restype=C.c_void_p
    n.ss_result.restype=C.c_uint64;n.ss_call.argtypes=[C.c_uint32]*5
    n.ss_list_entry.restype=C.c_uint32;n.ss_stores.restype=C.POINTER(C.c_uint32)
    elf=(ROOT.parent/'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest()==ELF_SHA256
    buf=C.create_string_buffer(elf);assert n.ss_init(buf,len(elf))==0
    ee=MiscEE(elf);rng=random.Random(0x182BF0)
    def store(a,data):
        p=n.ss_bytes(a,len(data));assert p,(hex(a),len(data));C.memmove(p,data,len(data));ee.write(a,data)
    def word(a,v):store(a,struct.pack('<I',v&0xFFFFFFFF))
    def native(a,size):
        p=n.ss_bytes(a,size);assert p;return C.string_at(p,size)
    def call(fn,args):
        ee.call(fn,args)
        assert n.ss_call(fn,*(list(args)+[0]*4)[:4])==0,(hex(fn),args)
        assert n.ss_result()&0xFFFFFFFF==ee.r[2]&0xFFFFFFFF,(hex(fn),args,n.ss_result(),ee.r[2])
    count=0
    # Low-byte IDs including sign extended and large input words; canonical
    # progress row, no snapshot RAM seed in runtime.
    for i in range(pick(2304,320)):
        area=i%24;store(0x810700,bytes([area]));store(0x810860+area*32,rng.randbytes(32))
        arg=[0,1,31,32,127,128,255,256,0xFFFFFFFF,0x80000080][i%10] if i<100 else rng.getrandbits(32)
        before=native(0x810860+area*32,32);call(0x1B11E0,(arg,))
        assert native(0x810860+area*32,32)==before;count+=1
    print(f'PASS {MODE}: B11E0 {count} original query cases',flush=True)
    for i in range(pick(512,96)):
        k=i%40;store(0x810CC3+k,bytes([rng.randrange(256)]));store(0x8106B0,rng.randbytes(2))
        call(0x1C4760,(k,rng.getrandbits(32)))
        assert native(0x810CC3+k,1)==ee.read(0x810CC3+k,1)
        assert native(0x8106B0,2)==ee.read(0x8106B0,2);count+=1
    for i in range(pick(2048,256)):
        store(0x8102B0,rng.randbytes(0x320))
        store(0x8102BF,bytes([0x63 if i%7==0 else 1]))
        store(0x8104A0,bytes([i%64]));word(0x8104D0,F(0 if i%13==0 else 100))
        word(0x8104D4,F(0 if i%2 else 2));word(0x8104DC,F(0 if i%3 else 3))
        for a in (0x8106BC,0x81083C,0x8106F1):store(a,bytes([rng.randrange(3)==0]))
        call(0x182BF0,(0x8102B0,))
        for a,size in ((0x8102B0,0x320),(0x8106BC,1),(0x81083C,1),(0x8106F1,1)):
            assert native(a,size)==ee.read(a,size),(i,hex(a))
        count+=1
    # Existing predicate owner calls the store boundary even when zero/state1
    # is already present. Refusing each exact store preserves only its prefix.
    expected=[0x8104DC,0x8104D4,0x8102B0]
    for deny in expected+[0]:
        store(0x8102B0,bytes(0x320));store(0x8102B0,b'\x01')
        word(0x8104D0,F(100));word(0x8104DC,F(2));word(0x8104D4,0)
        for a in (0x8106BC,0x81083C,0x8106F1):store(a,b'\0')
        n.ss_deny(deny);rc=n.ss_call(0x182BF0,0x8102B0,0,0,0);n.ss_deny(0)
        stop=expected.index(deny)+1 if deny else 3
        assert list(n.ss_stores()[:n.ss_store_count()])==expected[:stop]
        assert rc==(-1 if deny else 0)
        assert native(0x8104DC,4)==struct.pack('<I',F(2) if deny==expected[0] else 0)
        assert native(0x8104D4,4)==bytes(4) and native(0x8102B0,1)==b'\x01'
        count+=1
    # AREA01 XZ/XY/YZ quads, both windings and points off every plane.
    for i in range(pick(900,120)):
        mode=i%3;axes=(1,2) if mode==2 else (0,1) if mode==1 else (0,2)
        points=[]
        for x,z in ((-10.,-10.),(-10.,10.),(10.,10.),(10.,-10.)):
            v=[0.,0.,0.,1.];v[axes[0]]=x;v[axes[1]]=z;points.append(v)
        if i%2:points.reverse()
        poly=sum(points,[]);point=[rng.choice((-11.,-10.,-9.,0.,9.,10.,11.)) for _ in range(3)]
        store(0x680000,struct.pack('<3f',*point));store(0x680020,struct.pack('<16f',*poly))
        ee.save(0x26C5D0,0xFFFFFFFF);call(0x1B1EA0,(mode,0x680000,0x680020,4));count+=1
    # Publish real pool nodes through the existing list owner, with original
    # counters/address words and saturation. No synthetic result boundary.
    for function in (0x1B1B70,0x1B1DE0):
        n.ss_lists_reset()
        for k,(ptr,counter) in LIST_LIVE.items():ee.save(ptr,LIST_BASE[k]);ee.save(counter,0,2)
        for i in range(pick(512,160)):
            node=0x7A5640+(i%128)*0x2F0;cls=(1,2,4,7,13,0x8A,0xA4,3)[i%8]
            ee.save(node+2,cls,1);ee.save(node+0x14,node)
            ee.call(function,(node,));assert n.ss_publish_function(function,i%128,cls)==0
            for k,(ptr,counter) in LIST_LIVE.items():
                live=ee.load(counter,2);assert n.ss_list_count(k)==live,(i,k,live,n.ss_list_count(k))
                for j in range(live):assert n.ss_list_entry(k,j)==ee.load(LIST_BASE[k]-4*(j+1)),(i,k,j)
            count+=1
    # 00157F60: all model bytes, wrapping cost byte, owner/request aliasing,
    # exact ordered accesses and every missing-view prefix. The same scalar
    # owner also serves the existing AREA11 typed power panel wrapper.
    n.ss_accesses.restype=C.POINTER(C.c_uint32)
    regions=((0x680000,0x100),(0x8106B0,0x48))
    class Cut(Exception):pass
    class TerminalEE(MiscEE):
        def access(self,a,size,write):
            if hasattr(self,'events') and any(base<=a<base+length for base,length in regions):
                self.events.append((a,size,write))
                if len(self.events)==self.cut:raise Cut()
        def load(self,a,nbytes=4):
            self.access(a,nbytes,0);return super().load(a,nbytes)
        def save(self,a,v,nbytes=4):
            self.access(a,nbytes,1);return super().save(a,v,nbytes)
    te=TerminalEE(elf)
    def terminal_case(actor,mode,cost,cut=0):
        te.events=[];te.cut=0
        for base,length in regions:
            data=rng.randbytes(length);C.memmove(n.ss_bytes(base,length),data,length);te.write(base,data)
        for address,data in ((actor+3,bytes([mode])),(actor+0x34,bytes([cost])),
                             (actor+0x14,struct.pack('<I',rng.getrandbits(32)))):
            C.memmove(n.ss_bytes(address,len(data)),data,len(data));te.write(address,data)
        te.events=[];te.cut=cut
        try:
            te.call(0x157F60,(actor,));expected=0
        except Cut:expected=-1
        n.ss_deny_access(cut)
        rc=n.ss_call(0x157F60,actor,0xDEADBEEF,0xABCD0123,0)
        n.ss_deny_access(0)
        accesses=[tuple(n.ss_accesses()[3*i:3*i+3]) for i in range(n.ss_access_count())]
        assert rc==expected,(mode,cost,cut,rc,expected)
        assert accesses==te.events,(mode,cost,cut,accesses,te.events)
        for base,length in regions:
            assert native(base,length)==te.read(base,length),(mode,cost,cut,hex(base))
        if not cut:assert n.ss_result()==te.r[2]==1
        return len(accesses)
    terminal_cases=0
    costs=(0,1,0x7F,0x80,0xFE,0xFF) if MODE=='full' else (0,0xFF)
    for mode in range(256):
        for cost in costs:
            terminal_case(0x680000,mode,cost);terminal_cases+=1
    for actor in (0x8106B0,0x8106BC,0x8106C0):
        for mode in (0x24,0x2C,0x37,0x38):
            terminal_case(actor,mode,0x80);terminal_cases+=1
    cuts=0
    for mode in (0x24,0x2C,0x37,0x38):
        length=terminal_case(0x680000,mode,0x80)
        for cut in range(1,length+1):
            terminal_case(0x680000,mode,0x80,cut);cuts+=1
    print(f'PASS {MODE}: terminal request00157F60 {terminal_cases} original cases and {cuts} exact missing-access prefixes')
    count+=terminal_cases+cuts
    print(f'PASS {MODE}: {count} original shared-service cases, canonical scene/player/list mutations and SDK polygon calls')
if __name__=='__main__':main()
