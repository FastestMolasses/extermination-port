#!/usr/bin/env python3
"""Canonical pickup adapter versus original owners, with explicit workers.

Compare records, child, scripts, globals and scratch at every native-worker
boundary, then all game data. No original code or capture bytes are output.
"""
import ctypes as C
import itertools
import json
import struct
import subprocess
import time
import export_area01_common as A
import reference_mode as mode
from test_area01_runtime_reference import Call,Host,View,Worker
from test_player_fall_reference import FallEE
from test_player_slide_reference import read_elf,bits,STACK_TOP

OUT=A.ROOT/'build/level2/pickups'
LEAVES={0x1B0FD0:(1,0),0x1B1020:(4,0),0x1C6380:(1,0),0x1F1110:(2,0),
        0x1A2370:(2,0),0x1C5570:(4,0),0x1BA1A0:(2,0),0x1BA1F0:(1,0),
        0x1F1180:(1,0),0x1B17A0:(1,0),0x1CAA00:(1,0),0x1FBD50:(3,1),
        0x1B1190:(1,0),0x1AFC10:(1,0),0x1C40B0:(2,0)}
CHILD=0x7CDB00

def build():
    OUT.mkdir(parents=True,exist_ok=True)
    path=OUT/'pickup.dylib'
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off',
        '-shared','-fPIC','-Isrc','tests/area01_pickup_bridge.c',
        'src/game/em_area01_pickup_live.c','src/game/em_pickup_owner.c','src/game/em_pickup_items_original.c','-o',str(path)],cwd=A.ROOT,check=True)
    lib=C.CDLL(str(path));lib.ap_bind.argtypes=[C.POINTER(Host)];lib.ap_call.argtypes=[C.POINTER(Call)]
    lib.ap_fault.restype=C.c_uint32
    return lib

def main():
    start=time.monotonic();lib=build();elf=read_elf();caps=A.captures()
    caps=caps if mode.FULL else [caps[0]]
    total=boundaries=0
    for capture in caps:
        nodes=[0x7A5640+i*0x2F0 for i in range(400) if A.u32(capture.ram,0x7A5640+i*0x2F0+16) in (0x219550,0x15AFA0)]
        assert nodes
        cases=[(A.u32(capture.ram,n+16),n,None) for n in nodes]
        if capture==caps[0]:
            n=nodes[0]
            for entry,life,phase,subtype,armed in itertools.product((0x219550,0x15AFA0),(0,1,2,3),(0,1,2),(0,1,2),(0,4)):
                cases.append((entry,n,(life,phase,subtype,armed)))
            for sub,t in itertools.product((0,1,2),(0,3,0x1F,0x20,0xFF)):
                cases.append((0x1B6EA0,n,(1,0,sub,t)))
            for fn,life,phase in itertools.product((0x15AC00,0x15AE20),(0,1,2),(0,1)):
                cases.append((fn,n,(life,phase,1,4)))
        cases=mode.select(cases,44,0xA0117E,
            axes=(lambda x:x[0],lambda x:x[2][0] if x[2] else None,
                  lambda x:x[2][1:3] if x[2] else None),keep=lambda i,x:x[2] is None)
        for index,(entry,node,change) in enumerate(cases):
            o=FallEE(elf);o.mem[:]=capture.ram;o.spad[:]=capture.spad
            o.save(node+0x4C,0x1CAA00);o.save(node+0x2EC,CHILD);o.save(CHILD+4,2,1)
            if change:
                life,phase,sub,last=change
                o.save(node+4,life,1);o.save(node+5,phase,1);o.save(node+3,sub,1)
                o.save(node+11,last if entry!=0x1B6EA0 else 4,1)
                o.save(node+0x2E,last if entry==0x1B6EA0 else (0x28 if index%3==0 else 3),2)
                o.save(0x810C67,index%2,1);o.save(0x810700,0x10 if index%3==0 else 1,1)
                o.save(0x8104A0,0x2D if index%4==0 else 0,1);o.save(0x8104E6,index%5==0,1)
                o.save(0x810354,bits(10.));o.save(node+0xB4,bits((15.,20.,25.)[index%3]))
                o.save(0x70003B92,index%2,1)
            initial=bytes(o.mem);spad=bytes(o.spad);expected=[]
            spans=[(node,0x2F0),(CHILD,0x2F0),(0x2482C0,0x280),(0x266620,0x280),
                   (0x8102B0,0xB00),(0x700038A0,16),(0x70003B92,1)]
            reject=index%11==0;done=index%3!=0;visible=index%2
            def snapshot(r):return [r.read(a,n) for a,n in spans]
            def leaf(r,fn,args,floats):
                if fn in (0x1B0FD0,0x1B1020):
                    r.save(node+4,3 if reject else 1,1);r.r[2]=int(reject)
                elif fn==0x1C6380:r.save(node+0xD0,bits(2.))
                elif fn==0x1C5570:r.r[2]=CHILD
                elif fn==0x1BA1A0:
                    old=r.hooks;r.hooks={}
                    try:r.nested(fn,args)
                    finally:r.hooks=old
                elif fn==0x1BA1F0:
                    r.save(0x70003B92,index%2,1);r.r[2]=int(done)
                elif fn==0x1B17A0:r.save(node+1,visible,1);r.r[2]=visible
                elif fn==0x1AFC10:r.save(node,0,1);r.save(node+0x24,0x7A5640)
                elif fn==0x1C40B0:r.save(0x810C64+args[0],(r.load(0x810C64+args[0],1)+args[1])&255,1)
            def hook(fn):
                def run(r):
                    na,nf=LEAVES[fn];args=tuple(r.r[4+i]&0xFFFFFFFF for i in range(na));floats=tuple(r.f[12+i] for i in range(nf))
                    # BA1F0 uses the frame's current actor, not a meaningful
                    # a0 argument on every original owner call site.
                    if fn==0x1BA1F0:args=(node,)
                    expected.append((fn,args,floats,snapshot(r)));leaf(r,fn,args,floats)
                return run
            o.hooks={fn:hook(fn) for fn in LEAVES}
            args=(node,node+0x230) if entry==0x15AE20 else (node,)
            o.call(entry,args)
            want=bytes(o.mem);want_spad=bytes(o.spad)
            ram=(C.c_uint8*len(initial)).from_buffer_copy(initial);scratch=(C.c_uint8*len(spad)).from_buffer_copy(spad)
            helper=FallEE(elf);position=0;errors=[]
            def view(_,a,n,write):
                if a+n<=len(ram):return C.addressof(ram)+a
                if 0x70000000<=a and a+n<=0x70004000:return C.addressof(scratch)+a-0x70000000
                return None
            def worker(_,ptr):
                nonlocal position
                c=ptr.contents
                try:
                    fn=c.function;a=tuple(c.a[i]&0xFFFFFFFF for i in range(c.na));f=tuple(c.f[:c.nf])
                    assert position<len(expected),(hex(fn),'extra')
                    e=expected[position]
                    assert (fn,a,f)==e[:3],(index,hex(entry),position,(hex(fn),a,f),(hex(e[0]),e[1],e[2]))
                    helper.mem[:]=bytes(ram);helper.spad[:]=bytes(scratch)
                    have=snapshot(helper)
                    if have!=e[3]:
                        k=next(k for k in range(len(have)) if have[k]!=e[3][k]);at=next(i for i,(a,b) in enumerate(zip(have[k],e[3][k])) if a!=b)
                        raise AssertionError((index,hex(entry),hex(fn),'boundary',hex(spans[k][0]+at),have[k][at:at+8].hex(),e[3][k][at:at+8].hex()))
                    position+=1;leaf(helper,fn,a,f)
                    C.memmove(ram,bytes(helper.mem),len(ram));C.memmove(scratch,bytes(helper.spad),len(scratch))
                    c.v0=helper.r[2]&0xFFFFFFFF;return 0
                except Exception as error:errors.append(error);return -1
            callbacks=(View(view),Worker(worker));host=Host(None,*callbacks)
            assert lib.ap_bind(C.byref(host))==0
            c=Call(function=entry,sp=STACK_TOP,na=len(args));c.a[:len(args)]=args
            rc=lib.ap_call(C.byref(c))
            if errors:raise errors[0]
            assert rc==0,(index,hex(entry),hex(lib.ap_fault()))
            assert position==len(expected)
            for lo,hi in ((0x241000,0x700000),(0x7A5640,0x7D4640),(0x810000,0x828A00)):
                got=bytes(ram[lo:hi])
                if got!=want[lo:hi]:
                    at=next(i for i,(x,y) in enumerate(zip(got,want[lo:hi])) if x!=y)
                    raise AssertionError((index,hex(entry),'final',hex(lo+at),got[at:at+8].hex(),want[lo+at:lo+at+8].hex()))
            assert bytes(scratch)==want_spad
            if entry==0x1B6EA0:assert c.v0==1
            total+=1;boundaries+=position
    # Every failure latches and stops subsequent worker calls.
    calls=[]
    def reject(_,c):calls.append(c.contents.function);return -1
    good=View(lambda _,a,n,w:C.addressof(ram)+a if a+n<=len(ram) else None)
    for entry,na,reader,want in ((0xDEAD,1,good,0xDEAD),(0x219550,0,good,0x219550),
                                (0x219550,1,View(lambda *_:None),node),
                                (0x15AC00,1,good,0x1B0FD0)):
        ram[node+3]=1
        host=Host(None,reader,Worker(reject));assert lib.ap_bind(C.byref(host))==0
        c=Call(function=entry,na=na);c.a[0]=node
        assert lib.ap_call(C.byref(c))<0 and lib.ap_fault()==want,(hex(entry),hex(lib.ap_fault()),hex(want))
        before=len(calls);assert lib.ap_call(C.byref(c))<0 and len(calls)==before
    assert calls==[0x1B0FD0]
    report=dict(fault_cases=4,status='PASS',captures=len(caps),cases=total,worker_boundaries=boundaries,seconds=round(time.monotonic()-start,2))
    (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n');print('AREA01 pickups:',json.dumps(report,sort_keys=True))
if __name__=='__main__':main()
