#!/usr/bin/env python3
"""Shared live C7900 channels 0/3 and actual F4A10 worker boundaries.

Every original instruction below C7900 and D3990 executes. Test-only RCL
views borrow recorded inputs; no fixture or oracle is linked into the game.
"""
import ctypes as C
import json
from pathlib import Path
import struct
import subprocess
import time
import reference_mode as mode
import test_area01_render_reference as R
import test_object_unit_reference as unit
from test_area01_runtime_reference import Call
from test_face_attach_reference import SOURCES
from test_player_slide_reference import read_elf
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/level2/matrix-service'
def u32(b,a):return struct.unpack_from('<I',b,a)[0]
def put(b,a,v):struct.pack_into('<I',b,a,v&0xFFFFFFFF)
def build():
    OUT.mkdir(parents=True,exist_ok=True);target=OUT/'bridge.dylib'
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off',
        '-shared','-fPIC','-Isrc','-Wl,-dead_strip','-Wl,-exported_symbol,_mx_*',
        'tests/area01_matrix_service_bridge.c','src/game/em_area01_matrix_service.c',
        'src/game/em_object_unit.c',*SOURCES,'-o',str(target)],cwd=ROOT,check=True)
    n=C.CDLL(str(target));n.mx_seed.argtypes=[C.c_void_p]*3
    n.mx_call.argtypes=[C.POINTER(Call),C.POINTER(C.c_uint32)]
    n.mx_deny.argtypes=[C.c_uint32];n.mx_model.argtypes=[C.c_uint32]
    n.mx_finish.argtypes=[C.c_uint32,C.POINTER(C.c_uint32)]
    n.mx_begin.argtypes=[C.POINTER(C.c_uint32),C.POINTER(C.c_uint32)]
    n.mx_page.argtypes=[C.c_uint32]
    n.mx_triangles.argtypes=[C.c_uint32,C.POINTER(unit.Triangle),C.c_uint32]
    return n
def execute(e,fn):
    saved=e.r[31];hook=e.hooks.pop(fn,None);e.r[31]=R.RETURN
    try:e.run(fn)
    finally:
        e.r[31]=saved
        if hook is not None:e.hooks[fn]=hook
class Native:
    def __init__(self,n,e):
        self.n=n
        self.ram=C.create_string_buffer(bytes(e.mem));self.spr=C.create_string_buffer(bytes(e.spad))
        self.stack=C.create_string_buffer(bytes(e.stack));n.mx_seed(self.ram,self.spr,self.stack)
    def call(self,args):
        c=Call();c.function=0x1C7900;c.na=4
        for i,v in enumerate(args):c.a[i]=v&((1<<64)-1)
        fault=C.c_uint32();rc=self.n.mx_call(C.byref(c),C.byref(fault))
        assert rc==0,('matrix refusal',tuple(map(hex,args)),hex(fault.value))
    def compare(self,e,label):
        self.n.mx_snapshot()
        got=self.ram.raw[:-1];want=bytes(e.mem)
        assert got==want,(label,'ram',R.first_differences(got,want))
        got=self.spr.raw[:-1];want=bytes(e.spad)
        assert got==want,(label,'scratch',R.first_differences(got,want,0x70000000))
def prepared(elf,base,spr):
    ram=bytearray(base)
    for at,value in ((0x811CD0,0x310000),(0x811CDC,0x320000)):put(ram,at,value)
    ram[0x310000:0x330000]=bytes([0xAD])*0x20000
    return R.A01EE(elf,ram=ram,spad=spr)
def main():
    start=time.time();n=build();elf=read_elf();direct=boundaries=callers=contracts=0
    unit.ELF=unit.vc.ELF=unit.tod.ELF=elf
    unit.ABE['value']=0x40
    vu_cases=triangles=0;page_image=None
    beats=R.BEATS if mode.FULL else [R.BEATS[0]]
    for beat in beats:
        base,spr=R.image(beat)
        nodes=R.pool(base,{0x158BD0,0x158D30})
        assert nodes,beat
        for node in nodes:
            # The actual original caller supplies its own local vector and
            # stack address. Compare each complete native callee at that
            # boundary, then let the original finish its remaining chain.
            for vector in ((0,128,0,128),(24,96,32,64)) if mode.FULL else ((0,128,0,128),):
                e=prepared(elf,base,spr)
                struct.pack_into('<4I',e.spad,0x38B0,*vector)
                for at in (0x275B44,0x275B48):put(e.mem,at,node)
                native=[]
                def matrix(ee):
                    args=tuple(ee.r[4+i]&0xFFFFFFFF for i in range(4))
                    assert args[0]==node+0xD0 and args[2:]==(0x3F5,3)
                    native.append(Native(n,ee));native[-1].call(args)
                    execute(ee,0x1C7900);native[-1].compare(ee,('F4A10 C7900',beat,node))
                def model(ee):
                    assert native and n.mx_model(ee.r[4]&0xFFFFFFFF)==0
                    execute(ee,0x1D3990);native[-1].compare(ee,('F4A10 D3990',beat,node))
                e.hooks[0x1C7900]=matrix;e.hooks[0x1D3990]=model
                R.oracle_call(e,0x1F4A10,(node+0xD0,0x700038B0))
                assert len(native)==1
                callers+=1;boundaries+=2
                # Final original F4A10 RET/CB760 work is independently
                # checked by the flame adapter oracle. Register those exact
                # canonical bytes, then compare the cached unit's triangles
                # with the original VU1 kernel. Registration must write no
                # RAM, packet, chain, cursor or scratch bytes.
                page=Native(n,e);fault=C.c_uint32();cursor=C.c_uint32()
                assert n.mx_begin(C.byref(cursor),C.byref(fault))==0
                assert cursor.value==u32(e.mem,0x811CDC)
                assert n.mx_finish(0x320000,C.byref(fault))==0,('register',beat,hex(fault.value))
                assert n.mx_page(0x320000)==1;page.compare(e,('registration writes',beat))
                size=cursor.value-0x320000-16
                want,_=unit.oracle(bytes(e.mem),0x320000,size)
                out=(unit.Triangle*unit.MAX_TRIS)()
                count=n.mx_triangles(0x320000,out,unit.MAX_TRIS)
                assert count>=0 and unit.ntris(out,count)==want,('F4A10 VU1',beat,node,count,len(want))
                vu_cases+=1;triangles+=count;page_image=e
            # Both channels, all original lighting modes, and interleaved
            # channel-0 open-unit bookkeeping followed by channel 3.
            for light in ((-1,0,1,2,3,4,5,6,7) if mode.FULL else (0,1,4)):
                e=prepared(elf,base,spr);put(e.mem,0x811CC0+0x246C,light)
                struct.pack_into('<4f',e.spad,0x38B0,.25,.75,.125,0)
                native=Native(n,e)
                for channel in (0,3):
                    args=(node+0xD0,0x700038B0,0x3F5,channel)
                    native.call(args);R.oracle_call(e,0x1C7900,args)
                    native.compare(e,('direct',beat,node,light,channel));direct+=1
                    assert n.mx_open()==1 and n.mx_open_start()==0x310000
    # Refused argument domains and each borrowed input fail before writes.
    e=prepared(elf,base,spr);native=Native(n,e)
    c=Call();c.function=0x1C7900;c.na=4
    c.a[0]=node+0xD0;c.a[1]=0x700038B0;c.a[2]=0x3F5;c.a[3]=3
    for field,value,want in [('na',3,0x1C7900),('nf',1,0x1C7900)]:
        old=getattr(c,field);setattr(c,field,value);fault=C.c_uint32()
        assert n.mx_call(C.byref(c),C.byref(fault))<0 and fault.value==want
        setattr(c,field,old);native.compare(e,('contract',field));contracts+=1
    for channel in (-1,1,2,4):
        c.a[3]=channel&0xFFFFFFFF;fault=C.c_uint32()
        assert n.mx_call(C.byref(c),C.byref(fault))<0 and fault.value==0x1C7900
        native.compare(e,('channel',channel));contracts+=1
    c.a[3]=3
    for address in (node+0xD0,0x700038B0):
        n.mx_deny(address);fault=C.c_uint32()
        assert n.mx_call(C.byref(c),C.byref(fault))<0 and fault.value==address
        native.compare(e,('borrow',address));contracts+=1
    n.mx_deny(0);fault=C.c_uint32(0x123456)
    assert n.mx_call(C.byref(c),C.byref(fault))<0 and fault.value==0x123456;contracts+=1
    c.function=0x1C7904;fault=C.c_uint32()
    assert n.mx_call(C.byref(c),C.byref(fault))==1 and fault.value==0;contracts+=1
    page_contracts=0
    end=u32(page_image.mem,0x811CDC)
    # Exact RET validation and malformed-unit refusal before publication.
    for offset,value in ((end-16,1),(end-13,0x10),(end-12,1),(0x320000,4)):
        e=R.A01EE(elf,ram=bytes(page_image.mem),spad=bytes(page_image.spad))
        e.mem[offset]=value;native=Native(n,e);fault=C.c_uint32()
        assert n.mx_finish(0x320000,C.byref(fault))<0 and fault.value==0x320000
        assert n.mx_page(0x320000)==0;native.compare(e,('bad packet',offset));page_contracts+=1
    native=Native(n,page_image)
    for i in range(16):
        fault=C.c_uint32();assert n.mx_finish(0x320000,C.byref(fault))==0
    fault=C.c_uint32();assert n.mx_finish(0x320000,C.byref(fault))<0 and fault.value==0x320000
    native.compare(page_image,'cache capacity');page_contracts+=1
    result=dict(status='PASS',mode=mode.MODE,captures=len(beats),direct_calls=direct,
                actual_caller_cases=callers,original_boundaries=boundaries,contract_checks=contracts,
                vu1_cases=vu_cases,vu1_triangles=triangles,page_contract_checks=page_contracts,
                seconds=round(time.time()-start,2))
    (OUT/('full.json' if mode.FULL else 'quick.json')).write_text(json.dumps(result,indent=2)+'\n')
    print('AREA01 matrix service:',json.dumps(result,sort_keys=True))
if __name__=='__main__':main()
