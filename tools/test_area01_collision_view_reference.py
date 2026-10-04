#!/usr/bin/env python3
"""Original collision instructions across the borrowed AREA01 boundary.
Captures are test inputs only; runtime borrows the existing collision world.
"""
import ctypes as C
import hashlib
import random
import struct
from pathlib import Path
import reference_mode as RM
import test_coll_segment_walkers_reference as Q
import test_coll_probe_reference as P
import test_area01_sys_reference as S
from test_actor_pool_reference import ELF_SHA256

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/level2/collision-view'


def build():
    Q.OUT=OUT
    Q.BRIDGE+=(ROOT/'tests/area01_collision_view_bridge.inc').read_text()
    Q.SOURCES+=['src/game/em_area01_collision_view.c','src/game/em_startup_load_gaps.c',
                'src/game/em_coll_move_original.c','src/game/em_aim_fire_sdk_memory.c',
                'src/game/em_owner_services_original.c','src/game/em_point_light.c','src/game/em_area01_sys.c']
    n=Q.build_native()
    n.cv_setup.argtypes=[C.c_void_p,C.POINTER(Q.BState),C.c_uint32,C.c_uint32,C.c_uint32,C.c_uint32]
    n.cv_input.argtypes=[C.c_uint32,C.c_void_p,C.c_uint32]
    n.cv_state.argtypes=[C.POINTER(Q.BState)]
    n.cv_bytes.argtypes=[C.c_uint32,C.c_uint32,C.c_int];n.cv_bytes.restype=C.c_void_p
    n.cv_cells.restype=C.c_void_p
    n.cv_fourth.restype=C.c_uint32
    n.cv_quad_w.restype=C.c_uint32
    n.cv_quad_w_seed.argtypes=[C.POINTER(C.c_uint32)]
    n.cv_fault_address.restype=C.c_uint32
    n.cv_owns.argtypes=[C.c_uint32,C.c_uint32]
    n.cv_call.argtypes=[C.c_uint32]*5+[C.POINTER(C.c_uint32)]
    n.cv_sys.argtypes=n.cv_call.argtypes
    return n


def main():
    n=build();elf=Q.read_elf();assert hashlib.sha256(elf).hexdigest()==ELF_SHA256
    Q.G['elf']=elf
    P.ROUTE=S.ROUTE
    # Directory extent is the actual exported AREA01 resource; the generic
    # AREA11 helper predates EE uncached-mirror directory words.
    directory=(ROOT/'assets/area01/area01_cells.bin').read_bytes()
    P.directory_size=lambda ram,base:len(directory)
    count=0;hits={};contracts=0
    rng=random.Random(0xA01C011)
    beats=[];outside=[]
    for capture in sorted(S.ROUTE.glob('a01_*/eeMemory.bin')):
        with capture.open('rb') as f:f.seek(0x810700);area=f.read(1)[0]
        (beats if area==1 else outside).append(capture.parent.name)
    assert outside==['a01_07_level_exit'] and len(beats)==15
    for beat in beats:
        w=Q.World(beat,elf)
        b=Q.native_world(n,w,ROOT/'assets/area01/area01.emcl')
        ee=Q.LockEE(elf,w.ram,w.spad)
        off=Q.u32(elf,Q.u32(elf,28)+4)
        for fn,size in ((0x19AB20,0x1E0),(0x19F730,0x718),(0x199C50,0x158),
                        (0x19B4C0,0x200),(0x19CF50,0x3E0),(0x1A06A0,0x46C),(0x1AF8E0,0x16C)):
            assert ee.mem[fn:fn+size]==elf[off+fn-0x100000:off+fn-0x100000+size]
        # The original reads static AREA01 hulls via EE uncached RAM mirror.
        original_read,original_write=ee.read,ee.write
        ee.read=lambda a,z=4:original_read(a-0x20000000 if 0x20000000<=a<0x22000000 else a,z)
        ee.write=lambda a,data:original_write(a-0x20000000 if 0x20000000<=a<0x22000000 else a,data)
        file=Q.u32(w.spad,0x31F8)
        # Test fixture keeps the captured immutable grid interval, not RAM in runtime.
        file_size=max(Q.u32(w.spad,0x3210+4*k)+w.node_count*2 for k in range(12))-file
        file_size=max(file_size,w.nodes+64*w.node_count-file,w.table+w.size-file)
        def setup(case):
            st=case.native(w)
            assert n.cv_setup(b,C.byref(st),file,file_size,w.table,case.words[0x700031BC][0])==0,(beat,hex(file),file_size,hex(w.table),n.cv_fault(),hex(n.cv_fault_address()))
            # Captured W lanes were previously absent from the XYZ-only
            # typed ABI. They are actual shared words, never zero defaults.
            n.cv_quad_w_seed((C.c_uint32*4)(*(Q.u32(w.spad,0x360C+16*i) for i in range(4))))
            return st
        def check(fn,args,case,stage=()):
            nonlocal count
            ee.spad[:]=w.spad;case.load(ee)
            st=setup(case)
            saved=[]
            for address,data in stage:
                if address<len(w.ram):saved.append((address,bytes(w.ram[address:address+len(data)])))
                assert n.cv_input(address,data,len(data))==0
                ee.write(address,data)
            # Ensure the original function's code is precisely the pinned ELF.
            Q.check_code(elf,ee.mem,beat)
            if fn==0x19AB20:
                phoff=Q.u32(elf,28);off=Q.u32(elf,phoff+4)
                assert ee.mem[fn:fn+0x1E0]==elf[off+fn-0x100000:off+fn-0x100000+0x1E0]
            ee.call(fn,args)
            result=C.c_uint32()
            dispatch=n.cv_sys if fn==0x19B4C0 else n.cv_call
            assert dispatch(fn,*(list(args)+[0]*(4-len(args))),C.byref(result))==0,(beat,hex(fn),args,n.cv_fault(),hex(n.cv_fault_address()))
            n.cv_state(C.byref(st))
            actual=Q.native_state(st);want=Q.ee_state(ee,w)
            assert actual==want,(beat,hex(fn),[(k,want[k],actual[k]) for k in want if want[k]!=actual[k]])
            if fn!=0x19F1A0:assert result.value==(ee.r[2]&0xFFFFFFFF),(beat,hex(fn),'return',result.value,ee.r[2]&0xFFFFFFFF)
            assert n.cv_fourth()==ee.load(0x700031BC)
            for i in range(4):assert n.cv_quad_w(i)==ee.load(0x7000360C+16*i),(beat,hex(fn),'preserved W',i)
            if fn==0x19AB20 and args[3]&0x80000000:
                assert w.ram[args[0]+0xB4:args[0]+0xB8]==ee.mem[args[0]+0xB4:args[0]+0xB8]
            for address,data in saved:w.ram[address:address+len(data)]=data
            count+=1;hits[(fn,result.value)]=hits.get((fn,result.value),0)+1
        pos=struct.unpack_from('<3f',w.ram,P.PLAYER+0xB0)
        trials=RM.pick(20,2)
        for trial in range(trials):
            p=(pos[0]+rng.uniform(-10,10),pos[1]+10,pos[2]+rng.uniform(-10,10))
            to=(p[0],p[1]-80,p[2]);a=P.ARGS
            case=Q.Case(w,rng,words={0x700031BC:0xD1230000+trial})
            stage=[(a,struct.pack('<4f',*p,0)+struct.pack('<4f',*to,0))]
            for fn,args in ((0x19A570,(a,a+16,6,0)),(0x19A910,(a,a+16,6)),(0x19B6C0,(a,a+16)),
                            (0x19F1A0,(a,0x3F))):check(fn,args,case,stage)
            actor=bytearray(w.ram[P.PLAYER:P.PLAYER+0xC0]);struct.pack_into('<I',actor,0x14,P.PLAYER)
            # Preserve an explicit native identity for the player, which is not a published class-4 owner.
            n.bridge_actor(b,P.PLAYER,actor[0],actor[2],actor[3],Q.u16(actor,0xE),0,actor[9],0,0)
            ground_stage=[(a,struct.pack('<4f',*p,0)+struct.pack('<4f',0,80,0,0)),(P.PLAYER,bytes(actor))]
            for mask in (0,2,4,6,0x80000006):check(0x19AB20,(P.PLAYER,a,a+16,mask),case,ground_stage)
            check(0x19B4C0,(P.PLAYER,a,a+16,6),case,ground_stage)
        # Leaf tests use actual captured prim records and grid nodes, with a
        # staged segment. No collision callee is replaced by a return stub.
        segment={0x70003190+4*k:Q.bits(pos[k]+(10 if k==1 else 0)) for k in range(3)}
        segment.update({0x700031A0+4*k:Q.bits(pos[k]-(80 if k==1 else 0)) for k in range(3)})
        case=Q.Case(w,rng,words=segment)
        for node in RM.select(list(range(w.node_count)),RM.pick(80,8),seed=123):
            check(0x19ED80,(0x70003190,w.nodes+64*node),case)
            plane,verts,_=P.node_ring(w,w.ram,node);centre=P.inside(rng,verts)
            start=[centre[k]+10*plane[k] for k in range(3)];end=[centre[k]-10*plane[k] for k in range(3)]
            check(0x19ED80,(0x70003190,w.nodes+64*node),Q.Case(w,rng,Q.seg_words(start,end)))
        floor=next(i for i in range(w.node_count) if P.node_ring(w,w.ram,i)[0][1]>.99 and P.node_ring(w,w.ram,i)[2]<0x51)
        plane,verts,_=P.node_ring(w,w.ram,floor);centre=P.inside(rng,verts)
        p=(centre[0],centre[1]-5,centre[2]);a=P.ARGS
        ground_stage=[(a,struct.pack('<4f',*p,0)+struct.pack('<4f',0,-10,0,0)),(P.PLAYER,bytes(actor))]
        check(0x19AB20,(P.PLAYER,a,a+16,4),case,ground_stage)
        prims=[]
        for uid in range(w.count):
            word=Q.u32(w.ram,w.table+4+4*uid);off=word&0x1FFFFFFF
            if not word:continue
            hull=w.table+off;at=hull+0x1C
            for _ in range(Q.s16(w.ram,hull+0x18)):
                h=Q.u16(w.ram,at);kind=h&0xF000
                size={0x8000:0x24 if h&0x800 else 0x14,0x4000:0x2C if h&0x800 else 0x18,
                      0x2000:0x1C,0x1000:(0x24+0x30*w.ram[at+2]) if h&0x800 else (0x14+0x18*w.ram[at+2])}.get(kind)
                if not size:break
                prims.append((at,{0x8000:0x1A5C30,0x4000:0x1A5C30,0x2000:0x1A50A0,0x1000:0x1A4030}[kind]));at+=size
        for at,fn in RM.select(prims,RM.pick(120,12),seed=31,axes=(lambda x:x[1],)):
            check(fn,(at,),case)
            origin=struct.unpack_from('<3f',w.ram,at+4)
            if fn==0x1A4030:
                normal=origin;verts=[struct.unpack_from('<3f',w.ram,at+0x14+12*j) for j in range(w.ram[at+2])]
                centre=P.inside(rng,verts)
                start=[centre[k]+10*normal[k] for k in range(3)];end=[centre[k]-10*normal[k] for k in range(3)]
            elif fn==0x1A50A0:
                face=w.ram[at+2]
                if not 1<=face<=6:continue
                extent=struct.unpack_from('<3f',w.ram,at+0x10);axis=(face-1)//2
                start=[origin[k]+.5*extent[k] for k in range(3)];end=list(start)
                start[axis]=origin[axis]+10;end[axis]=origin[axis]-10
            else:
                h=Q.u16(w.ram,at);half=struct.unpack_from('<f',w.ram,at+(0x10 if h&0x8000 else 0x14))[0]
                start=[origin[0],origin[1]+abs(half)+10,origin[2]];end=[origin[0],origin[1]-abs(half)-10,origin[2]]
            for start,end in ((start,end),(end,start)):
                check(fn,(at,),Q.Case(w,rng,Q.seg_words(start,end)))
        # Metadata is rederived by the existing original initializer, and
        # compared with an independent execution of 00199C50, including kept ranks.
        setup(case);assert n.cv_begin()==0
        ee.spad[:]=w.spad;ee.call(0x199C50,())
        for address,size in ((0x700031F8,0x48),(0x7000324C,2),(0x70003250,4)):
            ptr=n.cv_bytes(address,size,0);assert ptr and C.string_at(ptr,size)==ee.read(address,size)
            assert not n.cv_bytes(address,size,1);contracts+=2
        assert not n.cv_owns(0x28A9A0,4) and n.cv_owns(0x28AAB0,4) and n.cv_owns(0x28B01F,1);contracts+=1
        # Mutable directory aliases are the very same native bytes, including the uncached mirror.
        ptr=n.cv_bytes(w.table,16,1);mirror=n.cv_bytes(w.table+0x20000000,16,1)
        assert ptr==n.cv_cells()==mirror
        old=C.string_at(ptr,16);C.memmove(ptr+12,b'\x5a\xa5\x19\x01',4)
        assert C.string_at(mirror+12,4)==b'\x5a\xa5\x19\x01';C.memmove(ptr,old,16);contracts+=2
        # Sprite/beam views span adjacent QWs of the same native owner.
        quad=n.cv_bytes(0x70003600,64,1);assert quad
        for off in range(0,64,4):
            assert n.cv_bytes(0x70003600+off,64-off,1)==quad+off
            assert n.cv_bytes(0x70003600+off,4,0)==quad+off
        saved=C.string_at(quad,64);pattern=bytes(range(64));C.memmove(quad,pattern,64)
        for off in range(0,64,16):
            assert C.string_at(n.cv_bytes(0x70003600+off,16,0),16)==pattern[off:off+16]
        C.memmove(quad,saved,64)
        for at,size in ((0x700035FF,2),(0x7000363F,2),(0x70003600,65)):
            assert n.cv_owns(at,size) and not n.cv_bytes(at,size,1)
        contracts+=39
        # One quadword and its subranges remain coherent through commit.
        point=n.cv_bytes(0x700031B0,16,1);assert point and n.cv_bytes(0x700031BC,4,1)==point+12
        C.memmove(point+12,struct.pack('<I',0xCF00A501),4)
        assert n.cv_commit()==0 and n.cv_fourth()==0xCF00A501;contracts+=2
        for address,value in ((0x700031D0,0x700030B0),(0x700031D0,w.nodes+64),
                              (0x700031D4,P.PLAYER),(0x70003254,P.PLAYER)):
            setup(case);assert n.cv_begin()==0;p=n.cv_bytes(address,4,1);assert p
            C.memmove(p,struct.pack('<I',value),4);assert n.cv_commit()==0 and n.cv_begin()==0
            p=n.cv_bytes(address,4,0);assert p and C.string_at(p,4)==struct.pack('<I',value)
            assert n.cv_commit()==0;contracts+=1
        for address,value in ((0x700031D0,w.nodes+1),(0x700031D4,0xDEAD1234),(0x70003254,0xDEAD4321)):
            setup(case);assert n.cv_begin()==0;p=n.cv_bytes(address,4,1);assert p
            C.memmove(p,struct.pack('<I',value),4);assert n.cv_commit()<0 and n.cv_fault()==3;contracts+=1
        for mutate in (n.cv_invalidate,n.cv_native_point):
            setup(case);assert n.cv_begin()==0;mutate();assert n.cv_commit()<0 and n.cv_fault()==2;contracts+=1
        for address,size in ((0x700031CC,8),(0x7000324B,4),(w.table-1,8),(0x700031F8,0)):
            setup(case);assert n.cv_begin()==0;assert not n.cv_bytes(address,size,1) and not n.cv_bytes(address,size,0);contracts+=1
        setup(case);n.cv_move_seed();assert n.cv_begin()==0 and n.cv_commit()==0
        st=Q.BState();n.cv_state(C.byref(st));assert Q.native_state(st)==Q.native_state(case.native(w));contracts+=1
        # Original reset proves all six list bases, including class 7 AC30.
        def memset(e):
            a,z,size=e.arg(0),e.arg(1),e.arg(2)
            assert z==0 and size==0x2F0 and (a-0x7A5640)%0x2F0==0
            e.write(a,bytes(size));e.ret_int(a)
        ee.hooks[0x121A28]=memset;ee.call(0x1AF8E0,());del ee.hooks[0x121A28]
        n.cv_reset_lists();setup(case);assert n.cv_begin()==0
        for globals in (0x275BAC,0x275B9C,0x275B8C,0x275B7C,0x275B6C,0x275B5C):
            for address,size in ((globals,8),(globals+8,2),(globals+12,2)):
                ptr=n.cv_bytes(address,size,0);assert ptr and C.string_at(ptr,size)==ee.read(address,size)
                assert not n.cv_bytes(address,size,1);contracts+=1
        assert n.cv_commit()==0
        setup(case);assert n.cv_pool_contract()==0;contracts+=261
        n.bridge_free(b)
    for fn in (0x19ED80,0x1A4030,0x1A50A0,0x1A5C30):assert hits.get((fn,1),0)>0
    assert hits.get((0x19AB20,4),0)>0 and hits.get((0x19B4C0,4),0)>0
    print(f'PASS: {count} original collision calls on {len(beats)} AREA01 snapshots; {contracts} canonical aliases, pointer commits, metadata and refusal checks')
    print('Different-area end snapshot excluded:',outside)
    print('results:',{hex(fn)+':'+str(value):number for (fn,value),number in sorted(hits.items())})

if __name__=='__main__':main()
