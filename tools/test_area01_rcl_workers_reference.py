#!/usr/bin/env python3
"""AREA01 packet workers against original instructions and the water caller.

The native side uses the actual em_render_context_live storage. Captures are
test inputs only. Water's RNG is an explicit scripted boundary on both sides;
quadword-copy leaves execute their exact copy contract in the native harness.
Every packet worker executes its real original body and existing native owner.
"""
import ctypes as C
import json
import struct
import subprocess
import time
from pathlib import Path

import reference_mode as mode
import export_area01_common as AC
import test_render_context_live_reference as RL
from test_anim_runtime_rest_reference import EE
from test_area01_runtime_reference import Call, Host, View, Worker
from test_player_slide_reference import read_elf, RETURN, STACK_TOP

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/level2/rcl-workers'
CTX,TABLE,CURSOR=0x811CC0,0x7635C0,0x480000
MASK=(1<<64)-1
SPECS={0x1CB5F0:3,0x1CB950:3,0x1CB6B0:4,0x1CB760:3,0x1D2E00:1,0x1D2DE0:2}
RETURNED={0x1CB5F0,0x1CB950,0x1D2E00}
COMPARE=((CURSOR,0x20000),(TABLE,0x8000),(CTX,0x2540))

def u32(b,a):return struct.unpack_from('<I',b,a)[0]
def put(b,a,v):struct.pack_into('<I',b,a,v&0xFFFFFFFF)
def sx(v):return (v if v<0x80000000 else v-(1<<32))&MASK

def build():
    OUT.mkdir(parents=True,exist_ok=True)
    path=OUT/'rcl-workers.dylib'
    sources=[f'src/game/{s}.c' for s in RL.SOURCES]+[
        'src/game/em_area01_rcl_workers.c','src/game/em_shadow_decal_original.c',
        'src/game/em_area01_sys.c','tests/area01_rcl_workers_bridge.c']
    subprocess.run(['cc','-std=c11','-O1','-Wall','-Wextra','-Werror','-ffp-contract=off',
                    '-shared','-fPIC','-Isrc',*dict.fromkeys(sources),'-o',str(path)],cwd=ROOT,check=True)
    n=C.CDLL(str(path))
    n.em_rcl_init.argtypes=[C.c_char_p,C.c_void_p]
    n.em_rcl_poke.argtypes=[C.c_uint32,C.c_void_p,C.c_uint32]
    for name in ('em_rcl_bytes','em_rcl_bytes_mut'):
        getattr(n,name).argtypes=[C.c_uint32,C.c_uint32]
        getattr(n,name).restype=C.c_void_p
    n.em_rcl_packet_chain.restype=C.c_void_p
    n.em_packet_chain_clear_fault.argtypes=[C.c_void_p]
    n.em_area01_rcl_workers_call.argtypes=[C.POINTER(Call),C.POINTER(C.c_uint32)]
    n.ar_water.argtypes=[C.POINTER(Host),C.c_uint32,C.c_uint32,C.POINTER(C.c_uint32)]
    n._field=(C.c_uint8*2)()
    # Before load, no alternate packet owner can be substituted.
    c=Call(function=0x1CB5F0,na=3);c.a[:3]=(TABLE,0x1000,3);fault=C.c_uint32()
    assert n.em_area01_rcl_workers_call(C.byref(c),C.byref(fault))<0 and fault.value==c.function
    assert n.em_rcl_init(str(RL.EXPORT).encode(),n._field)==0
    return n

def seed(n,ram,spr):
    n.em_packet_chain_clear_fault(n.em_rcl_packet_chain())
    for a,size in RL.OWNED:
        data=bytes(spr[a-0x70000000:a-0x70000000+size] if a>=0x70000000 else ram[a:a+size])
        assert len(data)==size and n.em_rcl_poke(a,data,size)==0,hex(a)

def snapshot(n):
    return tuple(C.string_at(n.em_rcl_bytes(a,size),size) for a,size in COMPARE)

def base(path):
    ram=bytearray((path/'eeMemory.bin').read_bytes())
    spr=bytearray((path/'scratchpad.bin').read_bytes())
    put(ram,CTX+0x18,CURSOR)
    ram[TABLE:TABLE+0x8000]=bytes(0x8000)
    ram[CURSOR:CURSOR+0x20000]=bytes([0xA5])*0x20000
    return ram,spr

def execute(o,fn,args):
    for i,v in enumerate(args):o.r[4+i]=v&MASK
    o.r[29]=STACK_TOP;o.r[31]=RETURN;o.run(fn)
    return o.r[2]&MASK

def direct(n,elf,path):
    ram,spr=base(path);seed(n,ram,spr);o=EE(elf,ram=ram,spad=spr)
    keys=(-0x80000000,-1,0,0xFFF,0x1000,0x1FFF,0xFFB001,0xFFF000,0x7FFFFFFF)
    cases=[]
    for i,key in enumerate(keys):
        cases.extend([(0x1CB5F0,(TABLE,key,(-2,-1,0,1,3,5,9,0x62)[i%8])),
                      (0x1CB950,(TABLE,key,0x20048BA199422040^(i<<56))),
                      (0x1CB6B0,(TABLE,key,8,0xABCDEF008140E0)),
                      (0x1CB760,(TABLE,key,0xFFFFFFFFA0234B00))])
    for slot in range(8):
        cases.extend([(0x1D2DE0,(slot,0x80000000|(slot*0x10203))), (0x1D2E00,(slot,))])
    for fn,args in cases:
        args=tuple(sx(v&0xFFFFFFFF) if v<0 or v<=0xFFFFFFFF else v for v in args)
        expected=execute(o,fn,args)
        call=Call(function=fn,na=len(args),v0=0x123456789ABCDEF0)
        call.a[:len(args)]=args;fault=C.c_uint32()
        assert n.em_area01_rcl_workers_call(C.byref(call),C.byref(fault))==0,(hex(fn),hex(fault.value))
        if fn in RETURNED:assert call.v0==expected,(hex(fn),hex(call.v0),hex(expected))
        if fn==0x1CB5F0 and 0<int(args[2])<0x100:
            data=bytes((i*17+len(cases))&255 for i in range(int(args[2])*16))
            p=n.em_rcl_bytes_mut(call.v0,len(data));assert p
            C.memmove(p,data,len(data));o.write(call.v0,data)
        assert snapshot(n)==tuple(o.read(a,size) for a,size in COMPARE),hex(fn)
    return len(cases)

def water(n,elf,path,slot):
    ram,spr=base(path)
    nodes=[a for a in range(0x7A5640,0x7D4640,0x2F0) if ram[a] and u32(ram,a+0x10)==0x1E7D20]
    assert len(nodes)==1,(path.name,nodes)
    node=nodes[0];ram[node+4]=1;put(ram,CTX+0x2528,slot)
    seed(n,ram,spr);o=EE(elf,ram=ram,spad=spr)
    expected=[];rng=[0x1020304,0x5060708,0x33445566];ri=0
    def random(ee):
        nonlocal ri
        ee.r[2]=rng[ri%len(rng)];ri+=1
    def original_worker(fn):
        def run(ee):
            args=tuple(ee.r[4+i]&MASK for i in range(SPECS[fn]))
            before=tuple(ee.read(a,size) for a,size in COMPARE)
            sp=ee.r[29];saved=ee.r[31];hooks=ee.hooks
            ee.hooks={k:v for k,v in hooks.items() if k not in SPECS};ee.r[31]=RETURN
            try:ee.run(fn)
            finally:ee.hooks=hooks;ee.r[31]=saved
            after=tuple(ee.read(a,size) for a,size in COMPARE)
            expected.append((fn,args,sp,before,after,ee.r[2]&MASK))
        return run
    o.hooks={fn:original_worker(fn) for fn in SPECS};o.hooks[0x122BB8]=random
    execute(o,0x1E7D20,(node,))
    native=(C.c_uint8*len(ram)).from_buffer_copy(ram)
    scratch=(C.c_uint8*len(spr)).from_buffer_copy(spr)
    stack=(C.c_uint8*0x100000)();seen=[];errors=[];nr=0
    @View
    def view(_ctx,a,size,write):
        for start,buf in ((0,native),(0x70000000,scratch),(0x7F000000,stack)):
            if size and start<=a and size<=len(buf) and a-start<=len(buf)-size:return C.addressof(buf)+a-start
        return None
    def address(a,size,write):
        return (n.em_rcl_bytes_mut(a,size) if write else n.em_rcl_bytes(a,size)) or view(None,a,size,write)
    @Worker
    def worker(_ctx,cp):
        nonlocal nr
        c=cp.contents
        try:
            if c.function in (0x102948,0x102958):
                size=16 if c.function==0x102948 else 64
                dst,src=address(c.a[0],size,1),address(c.a[1],size,0)
                assert dst and src;C.memmove(dst,src,size);return 0
            if c.function==0x122BB8:
                c.v0=rng[nr%len(rng)];nr+=1;return 0
            i=len(seen);e=expected[i]
            assert (c.function,tuple(c.a[:c.na]),c.sp)==e[:3],(i,hex(c.function),e[:3])
            assert snapshot(n)==e[3],('before',i,hex(c.function))
            fault=C.c_uint32();rc=n.em_area01_rcl_workers_call(cp,C.byref(fault))
            assert rc==0,(i,hex(c.function),hex(fault.value))
            assert snapshot(n)==e[4],('after',i,hex(c.function))
            if c.function in RETURNED:assert c.v0==e[5],(i,'return')
            seen.append(c.function);return 0
        except Exception as error:errors.append(repr(error));return -1
    h=Host(None,view,worker);fault=C.c_uint32()
    rc=n.ar_water(C.byref(h),node,STACK_TOP,C.byref(fault))
    assert rc==0 and not errors,(path.name,slot,hex(fault.value),errors)
    assert len(seen)==len(expected) and nr==ri
    # Every byte outside the RCL-owned ranges is still in the test actor/loader
    # image; overlay the actual canonical RCL views only for final comparison.
    got=bytearray(native);ss=bytearray(scratch)
    for a,size in RL.OWNED:
        data=C.string_at(n.em_rcl_bytes(a,size),size)
        if a>=0x70000000:ss[a-0x70000000:a-0x70000000+size]=data
        else:got[a:a+size]=data
    assert got==o.mem and ss==o.spad,(path.name,slot,'full memory')
    assert seen.count(0x1CB5F0)==32 and seen.count(0x1CB950)==1
    return len(seen)

def faults(n,path):
    ram,spr=base(path);seed(n,ram,spr);before=snapshot(n);count=1
    # Missing/unexpected argument lanes and unsupported slot address domains.
    for fn,na in SPECS.items():
        for wrong in (na-1,na+1):
            c=Call(function=fn,na=wrong);f=C.c_uint32()
            assert n.em_area01_rcl_workers_call(C.byref(c),C.byref(f))<0 and f.value==fn
            assert snapshot(n)==before;count+=1
    for fn in (0x1D2E00,0x1D2DE0):
        for slot in (-1,8,0x7FFFFFFF):
            c=Call(function=fn,na=SPECS[fn]);c.a[0]=slot&MASK;f=C.c_uint32()
            assert n.em_area01_rcl_workers_call(C.byref(c),C.byref(f))<0 and f.value==fn
            assert snapshot(n)==before;count+=1
    c=Call(function=0x1CB760,na=3,nf=1);f=C.c_uint32()
    assert n.em_area01_rcl_workers_call(C.byref(c),C.byref(f))<0;count+=1
    # An unmapped append latches the exact packet address, with no stores.
    p=n.em_rcl_bytes_mut(CTX+0x18,4);C.memmove(p,struct.pack('<I',0x1FFFFF0),4)
    before=snapshot(n);c=Call(function=0x1CB5F0,na=3);c.a[:3]=(TABLE,0x1000,3);f=C.c_uint32()
    assert n.em_area01_rcl_workers_call(C.byref(c),C.byref(f))<0 and f.value==0x20000F0
    assert snapshot(n)==before;count+=1
    c.function=0x1CB950
    assert n.em_area01_rcl_workers_call(C.byref(c),C.byref(f))<0 and f.value==0x20000F0
    assert snapshot(n)==before;count+=1
    n.em_packet_chain_clear_fault(n.em_rcl_packet_chain())
    c=Call(function=0xDEADBEEF);f=C.c_uint32()
    assert n.em_area01_rcl_workers_call(C.byref(c),C.byref(f))==1 and f.value==0;count+=1
    return count

def main():
    start=time.monotonic();n=build();elf=read_elf()
    paths=[AC.ARRIVAL]+sorted(p for p in AC.ROUTE_A01.iterdir()
        if (p/'eeMemory.bin').exists() and (p/'scratchpad.bin').exists()
        and (p/'eeMemory.bin').read_bytes()[0x810700]==1)
    paths=paths if mode.FULL else paths[:1]
    calls=sum(direct(n,elf,p) for p in paths)
    boundaries=0
    for p in paths:
        for slot in ((0,1) if mode.FULL else (0,)):
            boundaries+=water(n,elf,p,slot)
    fault_count=faults(n,paths[0])
    report=dict(mode='full' if mode.FULL else 'quick',captures=len(paths),direct_calls=calls,
                water_cases=len(paths)*(2 if mode.FULL else 1),water_boundaries=boundaries,
                refusal_contract_cases=fault_count,seconds=round(time.monotonic()-start,2))
    (OUT/('full.json' if mode.FULL else 'quick.json')).write_text(json.dumps(report,indent=2)+'\n')
    print('AREA01 RCL workers: PASS',json.dumps(report))

if __name__=='__main__':main()
