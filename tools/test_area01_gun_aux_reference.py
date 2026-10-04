#!/usr/bin/env python3
"""Auxiliary gun owner composition: real matrix/SDK leaves, bounded services.

8282F0's collision and render calls, 8287C0 allocation, and E520 exception
services are explicit paired boundaries. Compare all game RAM/scratch and
named stack locals, plus memory and arguments at every service boundary.
"""
import ctypes as C
import itertools
import json
import math
import random
import struct
import subprocess
import time
import export_area01_common as A
import reference_mode as mode
from test_area01_runtime_reference import Call,Host,View,Worker
from test_player_fall_reference import FallEE
from test_player_slide_reference import STACK_TOP,bits,flt

OUT=A.ROOT/'build/level2/gun-aux'
LEAVES={0x1026A0:(3,0),0x1028B8:(3,0),0x1028D0:(3,0),0x102738:(2,0),
        0x102948:(2,0),0x102958:(2,0),0x1031E0:(2,0),0x19AA80:(3,0),0x19A570:(4,0),
        0x122BB8:(0,0),0x1CD520:(5,3),0x1E2BA0:(3,1),0x1AFA90:(1,0),
        0x11DF78:(0,1),0x11CB90:(0,1),0x11E080:(0,1),0x128350:(0,1),
        0x11DB90:(1,0),0x11FD78:(0,0),0x127758:(1,0)}
NATIVE_SDK={0x11DF78,0x11CB90,0x11E080}
REAL={0x1026A0,0x1028B8,0x1028D0,0x102738,0x102948,0x102958,0x1031E0}|NATIVE_SDK
NODE=0x7A5640;OTHER=0x7A5930;CHILD=0x7C0000;ERRNO=0x1E00000

def build():
    OUT.mkdir(parents=True,exist_ok=True);path=OUT/'aux.dylib'
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off',
        '-shared','-fPIC','-Isrc','tests/area01_gun_aux_bridge.c','src/game/em_area01_gun_aux.c',
        'src/game/em_area01_revisit.c','src/game/em_area02_misc.c','src/game/em_sdk_math_original.c',
        '-o',str(path)],cwd=A.ROOT,check=True)
    lib=C.CDLL(str(path));lib.ag_call.argtypes=[C.POINTER(Host),C.POINTER(Call),C.POINTER(C.c_uint32),C.c_int]
    return lib

def main():
    start=time.monotonic();lib=build();elf=A.read_elf();caps=A.captures();caps=caps if mode.FULL else [caps[0]]
    rng=random.Random(0x8287C0);total=boundaries=spawned=probe=asin=0
    for capture in caps:
        ov=A.read_overlay();text_size=A.u32(ov,12)
        assert capture.ram[0x823540:0x823540+text_size]==ov[0x40:0x40+text_size]
        cases=[(0x8282F0,(world,entity,kind,far,state,count)) for world,entity,kind,far,state,count in
               itertools.product((0,1),(0,1),(0,1,2),(0,1),(1,4),(12,13))]
        cases+= [(0x8287C0,(refuse,)) for refuse in (0,1)]
        if capture==caps[0]:
            values=[0.,-0.,1.,-1.,.000000001,.2,-.2,.5,-.5,.95,-.95,1.2,-1.2]
            values += [rng.uniform(-1.5,1.5) for _ in range(mode.pick(100,0))]
            cases += [(0x11E520,(x,m,sdk)) for x,m,sdk in itertools.product(values,(-1,0,1,2),(0,1))]
        cases=mode.select(cases,45,0x8287C0,axes=(lambda x:x[0],),keep=lambda i,x:x[0]==0x8287C0)
        for entry,choice in cases:
            o=FallEE(elf,capture.ram,capture.spad)
            o.stack[:]=b'\xA5'*len(o.stack)
            o.save(NODE+4,4,1);o.save(NODE+0x204,0);o.save(NODE+0x200,13)
            o.write(NODE+0xD0,struct.pack('<16f',1,0,0,0,0,1,0,0,0,0,1,0,2,3,4,1))
            # This exact slot is only a supplied existing owner view here.
            o.save(0x275B40,NODE+0x110);o.save(NODE+0x11C,NODE+0x40)
            sdk=choice[2] if entry==0x11E520 else 0
            if entry==0x8282F0:
                world,entity,kind,far,state,count=choice
                o.save(NODE+4,state,1);o.save(NODE+0x200,count)
                o.save(OTHER+3,0x11 if far else 3,1)
                o.write(0x810360,struct.pack('<4f',0,0,0,1))
            if entry==0x11E520:o.save(0x26C5D0,choice[1])
            args=(NODE,NODE+0xD0) if entry==0x8282F0 else ((NODE+0xD0,) if entry==0x8287C0 else ())
            floats=(choice[0],) if entry==0x11E520 else ()
            local_lo=STACK_TOP-(0x20 if entry==0x8282F0 else 0x10) if entry!=0x11E520 else STACK_TOP-0x60
            local_n=0x24 if entry==0x11E520 else STACK_TOP-local_lo
            spans=[(NODE,0x2F0),(CHILD,0x2F0),(0x70003000,0x1000),(local_lo,local_n),(ERRNO,4)]
            initial=bytes(o.mem);spad=bytes(o.spad);stack=bytes(o.stack);expected=[]
            def snapshot(ee):return tuple(ee.read(a,n) for a,n in spans)
            def action(ee,fn):
                if fn in REAL:
                    old=ee.hooks;ee.hooks={}
                    try:v,f=ee.nested(fn)
                    finally:ee.hooks=old
                    ee.r[2]=v;ee.f[0]=f
                elif fn in (0x19AA80,0x19A570):
                    result=world if fn==0x19AA80 else entity
                    ee.r[2]=result
                    ee.write(0x700031B0,struct.pack('<4f',200. if far else 5.,3.,4.,1.))
                    ee.save(0x700031D8,kind);ee.save(0x700031D4,OTHER);ee.save(0x700031D0,NODE)
                elif fn==0x122BB8:ee.r[2]=0x6ABC1234
                elif fn==0x1AFA90:ee.r[2]=0 if choice[0] else CHILD
                elif fn in (0x1CD520,0x1E2BA0):ee.r[2]=37
                elif fn==0x128350:ee.r[2]=struct.unpack('<Q',struct.pack('<d',flt(ee.f[12])))[0]
                elif fn==0x11DB90:
                    ee.save(ee.r[4]+0x20,73);ee.write(ee.r[4]+0x18,struct.pack('<d',.75));ee.r[2]=1
                elif fn==0x11FD78:ee.r[2]=ERRNO
                elif fn==0x127758:ee.f[0]=bits(struct.unpack('<d',struct.pack('<Q',ee.r[4]))[0])
                else:raise AssertionError(hex(fn))
            def hook(fn):
                def go(ee):
                    na,nf=LEAVES[fn]
                    if not(sdk and fn in NATIVE_SDK):
                        a=tuple(ee.r[4+i]&((1<<64)-1) for i in range(na));f=tuple(ee.f[12:12+nf])
                        expected.append((fn,a,f,ee.r[29],snapshot(ee)))
                    action(ee,fn)
                return go
            o.hooks={fn:hook(fn) for fn in LEAVES}
            o.call(entry,args,floats);want=bytes(o.mem);want_spad=bytes(o.spad)
            ram=(C.c_uint8*len(initial)).from_buffer_copy(initial)
            scratch=(C.c_uint8*len(spad)).from_buffer_copy(spad)
            stk=(C.c_uint8*len(stack)).from_buffer_copy(stack)
            helper=FallEE(elf);position=0;errors=[]
            def view(_,a,n,w):
                if a+n<=len(ram):return C.addressof(ram)+a
                if 0x70000000<=a and a+n<=0x70004000:return C.addressof(scratch)+a-0x70000000
                if 0x7F000000<=a and a+n<=0x7F100000:return C.addressof(stk)+a-0x7F000000
                return None
            def worker(_,ptr):
                nonlocal position
                c=ptr.contents
                try:
                    e=expected[position];a=tuple(c.a[:c.na]);f=tuple(c.f[:c.nf])
                    assert (c.function,a,f,c.sp)==e[:4],(hex(entry),choice,position,hex(c.function),a,f,hex(c.sp),e[:4])
                    helper.mem[:]=bytes(ram);helper.spad[:]=bytes(scratch);helper.stack[:]=bytes(stk)
                    assert snapshot(helper)==e[4],(hex(entry),choice,hex(c.function),'boundary memory')
                    helper.r[29]=c.sp
                    for i,x in enumerate(a):helper.r[4+i]=x
                    for i,x in enumerate(f):helper.f[12+i]=x
                    action(helper,c.function)
                    C.memmove(ram,bytes(helper.mem),len(ram));C.memmove(scratch,bytes(helper.spad),len(scratch))
                    C.memmove(stk,bytes(helper.stack),len(stk))
                    c.v0=helper.r[2]&((1<<64)-1);c.f0=helper.f[0];position+=1;return 0
                except Exception as error:errors.append(error);return -1
            callbacks=(View(view),Worker(worker));host=Host(None,*callbacks)
            c=Call(function=entry,sp=STACK_TOP,na=len(args),nf=len(floats));c.a[:len(args)]=args
            if floats:c.f[0]=bits(floats[0])
            fault=C.c_uint32();rc=lib.ag_call(C.byref(host),C.byref(c),C.byref(fault),sdk)
            if errors:raise errors[0]
            assert rc==0,(hex(entry),choice,hex(fault.value))
            assert position==len(expected)
            got=bytes(ram)
            assert got==want,(hex(entry),choice,next((hex(i),x,y) for i,(x,y) in enumerate(zip(got,want)) if x!=y))
            assert bytes(scratch)==want_spad,(hex(entry),choice,'scratch')
            assert bytes(stk[local_lo-0x7F000000:local_lo-0x7F000000+local_n])==o.read(local_lo,local_n)
            if entry==0x8282F0:assert c.v0==o.r[2]&((1<<64)-1)
            if entry==0x11E520:assert c.f0==o.f[0],(choice,hex(c.f0),hex(o.f[0]))
            total+=1;boundaries+=position;spawned+=entry==0x8287C0;probe+=entry==0x8282F0;asin+=entry==0x11E520
    # Missing views/workers must stop at their reached boundary and latch.
    fault_cases=0
    for entry,missing,wanted in ((0x8287C0,0,0x1AFA90),(0x8282F0,0,0x1026A0),
                                  (0x11E520,0x26C5D0,0x26C5D0),(0xDEAD,0,0xDEAD)):
        seen=[]
        def reject(_,p):seen.append(p.contents.function);return -1
        def guarded(_,a,n,w):return None if missing and a<=missing<a+n else view(_,a,n,w)
        callbacks=(View(guarded),Worker(reject));host=Host(None,*callbacks)
        c=Call(function=entry,sp=STACK_TOP,na=2 if entry==0x8282F0 else (1 if entry==0x8287C0 else 0),nf=int(entry==0x11E520))
        c.a[0]=NODE;c.a[1]=NODE+0xD0;c.f[0]=bits(.2)
        fault=C.c_uint32();assert lib.ag_call(C.byref(host),C.byref(c),C.byref(fault),0)<0
        assert fault.value==wanted,(hex(entry),hex(fault.value),hex(wanted))
        before=len(seen);assert lib.ag_call(C.byref(host),C.byref(c),C.byref(fault),0)<0 and len(seen)==before
        fault_cases+=1
    report=dict(status='PASS',captures=len(caps),cases=total,probe_cases=probe,spawn_cases=spawned,asin_cases=asin,fault_cases=fault_cases,
                worker_boundaries=boundaries,seconds=round(time.monotonic()-start,2))
    (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n');print('AREA01 gun aux:',json.dumps(report,sort_keys=True))
if __name__=='__main__':main()
