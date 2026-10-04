#!/usr/bin/env python3
"""AREA01 multi-door binding and shaft composition against original code.

The original door/overlay/script-start/math instructions execute; model,
animation, script-pump, collision publication, draw and fade are explicit
worker boundaries. No original bytes are distributed. Full mode covers all
recorded AREA01 frames and a larger phase/side/lock sweep.
"""
import ctypes as C
import itertools
import struct
import subprocess
import time

import export_area01_common as A
import reference_mode as mode
from test_area01_runtime_reference import Call, Host, View, Worker
from test_player_fall_reference import FallEE
from test_player_slide_reference import read_elf, bits, number, STACK_TOP

OUT=A.ROOT/'build/level2/doors'
MATH={0x1B1240:(1,2),0x1B1470:(0,1),0x11E2A8:(0,1),0x11DE90:(0,1)}
LEAVES={0x1B0EA0:(1,0),0x1C63E0:(2,0),0x1C64F0:(1,1),0x1C67E0:(2,2),
        0x1C68C0:(1,0),0x1B1B30:(1,3),0x1CAA00:(1,0),0x1AFC10:(1,0),
        0x182F90:(2,0),0x1BA1A0:(2,0),0x1BA1F0:(1,0),0x1AEDE0:(2,0),0x1B0C00:(1,0)}
VOID={0x1BBDA0,0x1BC150,0x1BC240,0x1BC300,0x1BC350,0x823580}


def build():
    OUT.mkdir(parents=True,exist_ok=True)
    lib=OUT/'door.dylib'
    sources=['tests/area01_door_bridge.c']+[f'src/game/{name}.c' for name in
        ('em_area01_door_live','em_door_original','em_door_transit','em_door_program',
         'em_script_door_fan','em_startup_load_gaps')]
    sources += [f'src/game/em_area01_{name}.c' for name in
        ('runtime','math_core','math_actor','math_owner','light_owner','overlay','overlay_826d40',
         'sys','exita','exitb','room','side')]
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off',
                    '-shared','-fPIC','-Isrc',*sources,'-lm','-o',str(lib)],cwd=A.ROOT,check=True)
    native=C.CDLL(str(lib))
    native.a01door_bind.argtypes=[C.POINTER(Host)]
    native.a01door_call.argtypes=[C.POINTER(Call)]
    native.a01door_direct.argtypes=[C.POINTER(Call)]
    native.a01door_fault.restype=C.c_uint32
    return native


def main():
    start=time.monotonic(); native=build(); elf=read_elf()
    caps=A.captures()
    if not mode.FULL:caps=[caps[0],caps[2],caps[-1]]
    total=workers=0
    for capture in caps:
        nodes=[0x7A5640+i*0x2F0 for i in range(400)
               if A.u32(capture.ram,0x7A5640+i*0x2F0+16) in (0x1BC350,0x823580)]
        assert len(nodes)==5
        assert capture.ram[0x100000:0x241000] == elf[0x300:0x141300]
        overlay=A.read_overlay()
        assert capture.ram[0x823500:0x828A00] == overlay[:0x5500]
        cases=[(A.u32(capture.ram,n+16),n,None) for n in nodes]
        if capture==caps[0]:
            states=[(0,0),(2,0),(3,0),(4,0)]+[(1,p) for p in range(8)]
            gates=(0,0x80,0x81,0xFF) if mode.FULL else (0,0x81)
            for n,(life,phase),gate,side in itertools.product(nodes,states,gates,(0,1)):
                cases.append((A.u32(capture.ram,n+16),n,(life,phase,gate,side)))
            # Direct shared leaves include a separate script block, negative
            # active flag, and ordinary versus inter-area destination rows.
            for fn,n,side in itertools.product((0x1B0F60,0x1BBDA0,0x1BBE40,0x1BC0E0,
                                                0x1BC150,0x1BC240,0x1BC290,0x1BC300),nodes,(0,1)):
                cases.append((fn,n,(0 if fn in (0x1B0F60,0x1BBDA0) else 1,0,0x81,side)))
        cases = mode.select(cases, 50, 0xA01D00,
            axes=(lambda x:x[0],lambda x:x[1],lambda x:x[2][0:2] if x[2] else None,
                  lambda x:x[2][2:] if x[2] else None),keep=lambda i,x:x[2] is None)
        for index,(entry,node,change) in enumerate(cases):
            original=FallEE(elf);original.mem[:]=capture.ram
            original.spad[:]=capture.spad
            if change:
                life,phase,gate,side=change
                original.save(node+4,life,1);original.save(node+5,phase,1)
                original.save(node+11,4 if index%3 else 0,1)
                original.save(node+0x2E,side,2)
                original.save(node+0x34,(3 if original.load(node+3,1)==0x15 else index%4),2)
                original.save(node+0x1FC,0x80 if index%2 else 0,1)
                original.save(0x8107D9,gate,1)
                original.save(0x810842,8 if index%2 else 0,1)
                original.save(0x8106B8,index%3,1)
                if entry==0x1BC150 and index%2:original.save(node+0x34,0x80,2)
                # Both sides of each placed door, using its actual yaw.
                original.save(0x810350,original.load(node+0xB0))
                z=struct.unpack('<f',original.read(node+0xB8,4))[0]
                original.save(0x810358,bits(z+(10 if side else -10)))
            done=(index%3)!=0; reject=(index%11)==0
            initial=bytes(original.mem); spad=bytes(original.spad)
            block=node+(0x230 if entry in (0x1BC0E0,0x1BC240,0x1BC290) and index%2 else 0x1F0)
            args=(node,block,index%2) if entry==0x1BBE40 else ((node,block) if entry in
                (0x1BC0E0,0x1BC240,0x1BC290) else ((node,0) if entry==0x1B0F60 else (node,)))
            expected=[]

            def leaf(o,fn,args,floats):
                if fn==0x1BA1A0:
                    old=o.hooks;o.hooks={}
                    try:o.nested(fn,args,floats)
                    finally:o.hooks=old
                elif fn==0x1B0EA0:
                    if reject:o.save(args[0]+4,3,1)
                    o.r[2]=int(reject)
                elif fn==0x1C64F0:
                    o.save(args[0]+0x38,bits(7.25));o.r[2]=0xFEDC
                elif fn in (0x1C63E0,0x1C67E0):o.save(args[0]+0x38,0)
                elif fn==0x1BA1F0:
                    o.save(args[0]+0x1FC,index&1,1);o.r[2]=int(done)
                elif fn==0x1B1B30:o.save(args[0]+1,index&255,1)
                elif fn==0x182F90:o.write(args[0]+0xA0,o.read(args[1],16))

            def original_leaf(fn):
                def run(o):
                    na,nf=LEAVES[fn]
                    a=tuple(int(o.r[4+i])&0xffffffff for i in range(na))
                    f=tuple(o.f[12+i]&0xffffffff for i in range(nf))
                    # At non-math boundaries, every door, shared program,
                    # player and request/scratch byte is compared exactly.
                    snapshot=[o.read(n,0x2F0) for n in nodes]
                    snapshot += [o.read(0x24DBC0,0x3C0),o.read(0x8102B0,0x600),o.read(0x700038A0,16)]
                    expected.append((fn,a,f,snapshot))
                    leaf(o,fn,a,f)
                return run
            original.hooks={fn:original_leaf(fn) for fn in LEAVES}
            original.call(entry,args)
            want=bytes(original.mem);want_spad=bytes(original.spad);want_ret=original.r[2]&0xffffffff
            ram=(C.c_uint8*len(initial)).from_buffer_copy(initial)
            scratch=(C.c_uint8*len(spad)).from_buffer_copy(spad)
            pos=0; error=[]
            helper=FallEE(elf)
            def view(_,a,n,write):
                if a+n<=len(ram):return C.addressof(ram)+a
                if 0x70000000<=a and a+n<=0x70004000:return C.addressof(scratch)+a-0x70000000
                return None
            def worker(_,ptr):
                nonlocal pos
                c=ptr.contents;fn=c.function
                if native.em_area01_door_handles(fn):return native.a01door_call(ptr)
                try:
                    if fn not in MATH and fn not in LEAVES:raise AssertionError(hex(fn))
                    a=tuple(c.a[i]&0xffffffff for i in range(c.na));f=tuple(c.f[:c.nf])
                    if fn in LEAVES:
                        assert pos<len(expected),(hex(entry),hex(fn),'extra')
                        want_fn,want_a,want_f,snapshot=expected[pos]
                        assert (fn,a,f)==(want_fn,want_a,want_f),(hex(entry),pos,hex(fn),a,hex(want_fn),want_a)
                        got=[bytes(ram[n:n+0x2F0]) for n in nodes]
                        got += [bytes(ram[0x24DBC0:0x24DF80]),bytes(ram[0x8102B0:0x8108B0]),bytes(scratch[0x38A0:0x38B0])]
                        for k,(x,y) in enumerate(zip(got,snapshot)):
                            if x!=y:
                                at=next(i for i,(l,r) in enumerate(zip(x,y)) if l!=r)
                                raise AssertionError((capture.name,hex(entry),index,pos,hex(fn),'boundary bytes',k,hex(at),x[at:at+8].hex(),y[at:at+8].hex()))
                        pos+=1
                    helper.mem[:]=bytes(ram);helper.spad[:]=bytes(scratch);helper.hooks={}
                    if fn in MATH:helper.call(fn,a,tuple(number(v) for v in f))
                    else:leaf(helper,fn,a,f)
                    C.memmove(ram,bytes(helper.mem),len(ram));C.memmove(scratch,bytes(helper.spad),len(scratch))
                    c.v0=int(helper.r[2])&((1<<64)-1);c.f0=helper.f[0]&0xffffffff
                    return 0
                except Exception as e:error.append(e);return -1
            callbacks=(View(view),Worker(worker))
            host=Host(None,*callbacks)
            assert native.a01door_bind(C.byref(host))==0
            call=Call(function=entry,sp=STACK_TOP,na=len(args));call.a[:len(args)]=args
            rc=native.a01door_call(C.byref(call))
            if error:raise error[0]
            assert rc==0,(hex(entry),index,hex(native.a01door_fault()))
            assert pos==len(expected)
            actual=bytes(ram);actual_spad=bytes(scratch)
            # Stack frames are private call ABI storage; compare game data.
            spans=[(0x241000,0x700000),(0x7A5640,0x7A5640+400*0x2F0),(0x810000,0x828A00)]
            for lo,hi in spans:
                if actual[lo:hi]!=want[lo:hi]:
                    at=next(i for i in range(lo,hi) if actual[i]!=want[i])
                    raise AssertionError((capture.name,hex(entry),index,hex(at),actual[at:at+12].hex(),want[at:at+12].hex()))
            assert actual_spad[0x38A0:0x38B0]==want_spad[0x38A0:0x38B0]
            if entry not in VOID:assert call.v0&0xffffffff==want_ret,(hex(entry),call.v0,want_ret)
            total+=1;workers+=pos
        print(f'  {capture.name}: {len(cases)} cases exact',flush=True)
    # Faults latch at the reached boundary. An unsupported function, an
    # absent record, a missing required argument and a rejected place
    # worker must not run subsequent workers or silently change area.
    bad_calls = []
    good_view = View(lambda _,a,n,w: C.addressof(ram)+a if a+n<=len(ram) else None)
    reject_worker = Worker(lambda _,p: bad_calls.append(p.contents.function) or -1)
    for fn,na,reader,want in ((0xDEAD,1,good_view,0xDEAD),
                              (0x1BC300,1,View(lambda *_:None),node),
                              (0x1BC0E0,1,good_view,0x1BC0E0),
                              (0x1BC300,1,good_view,0x1C68C0)):
        host = Host(None,reader,reject_worker)
        assert native.a01door_bind(C.byref(host))==0
        call=Call(function=fn,sp=STACK_TOP,na=na);call.a[0]=node
        assert native.a01door_direct(C.byref(call))==-1
        assert native.a01door_fault()==want,(hex(fn),hex(native.a01door_fault()),hex(want))
        before=len(bad_calls)
        assert native.a01door_direct(C.byref(call))==-1 and len(bad_calls)==before
    assert bad_calls==[0x1C68C0]
    print(f'PASS AREA01 doors: {len(caps)} captures,5 placed doors each,{total} cases,{workers} exact worker boundaries;4 fault cases; {time.monotonic()-start:.1f}s')


if __name__=='__main__':main()
