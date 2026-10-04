#!/usr/bin/env python3
"""Existing-handler composition and AREA01 826950 against original instructions."""
import ctypes as C
import itertools
import subprocess
import time
import export_area01_common as A
from reference_mode import FULL,banner
from test_area01_runtime_reference import Host,Call,View,Worker
from test_player_fall_reference import FallEE
from test_player_slide_reference import read_elf,bits,number,STACK_TOP

LEAVES={0x102948:(2,0),0x1028D0:(3,0),0x1B1240:(1,2),0x1B12B0:(0,3),
        0x1FB9F0:(4,0),0x182F90:(2,0),0x1FD4C0:(1,0),0x119828:(3,0)}
EXTERNAL={0x1FB9F0,0x1FD4C0,0x119828}

def main():
    start=time.monotonic();out=A.ROOT/'build/level2/script_workers';out.mkdir(parents=True,exist_ok=True)
    lib=out/'workers.dylib'
    sources=['em_area01_script_workers','em_area00_low','em_area02_misc','em_area01_revisit','em_area01_math_core']
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off','-shared','-fPIC','-Isrc',
                    *['src/game/'+s+'.c' for s in sources],'-o',str(lib)],cwd=A.ROOT,check=True)
    n=C.CDLL(str(lib));n.em_area01_script_worker_call.argtypes=[C.POINTER(Host),C.POINTER(Call),C.POINTER(C.c_uint32)]
    elf=read_elf();caps=A.captures();caps=caps if FULL else [caps[0],caps[2],caps[-1]]
    total=boundaries=0
    for cap in caps:
        cases=[(0x1B7670,mode,flag,0) for mode,flag in itertools.product((0,1,2,0xFFFFFFFF),(0,1,2))]
        cases += [(0x1B6AE0,phase,wait,t) for phase,wait,t in itertools.product((0,1,2,3),(0,1),(59.,60.,61.))]
        cases += [(0x825910,0,0,value) for value in (504.,505.,506.)]
        cases += [(0x826950,phase,0,t) for phase,t in itertools.product((0,1,2,3),(0.,19.,20.,39.,40.,54.,55.,80.))]
        if not FULL:cases=cases[::3]+[(0x826950,2,0,t) for t in (20.,40.,55.)]
        node,block,record=0x7ABD10,0x7ABF00,0x82B650
        spans=[(node,0x2F0),(record,64),(0x8101E0,0x710)]
        def snapshot(o):return tuple(o.read(a,size) for a,size in spans)+(o.read(0x70003600,16),o.read(0x70003B80,32))
        for fn,phase,flag,t in cases:
            o=FallEE(elf);o.mem[:]=cap.ram;o.spad[:]=cap.spad
            o.write(record,A.read_overlay()[record-0x823500:record-0x823500+64])
            o.save(block+4,phase,1);o.save(0x70003B91,flag,1);o.save(0x8106F4,flag,1)
            o.save(record+0x10,bits(t));o.save(record+0xC,bits(80.));o.save(node+0x3C,bits(t))
            if fn==0x1B7670:o.save(record+8,phase)
            if fn==0x1B6AE0:o.save(record+8,flag)
            before,spad=bytes(o.mem),bytes(o.spad);expected=[]
            def leaf(o,fn,args,floats):
                if fn in EXTERNAL:o.r[2]=0;return
                old=o.hooks;o.hooks={}
                try:o.r[2],o.f[0]=o.nested(fn,args,tuple(number(v) for v in floats))
                finally:o.hooks=old
            def hook(fn):
                def call(o):
                    na,nf=LEAVES[fn];args=tuple(o.r[4+i]&0xFFFFFFFF for i in range(na));floats=tuple(o.f[12+i] for i in range(nf))
                    expected.append((fn,args,floats,o.r[29]&0xFFFFFFFF,snapshot(o)));leaf(o,fn,args,floats)
                return call
            o.hooks={fn:hook(fn) for fn in LEAVES};o.call(fn,(node,block,record))
            want=snapshot(o);ret=o.r[2]&0xFFFFFFFF
            ram=(C.c_uint8*len(before)).from_buffer_copy(before);sp=(C.c_uint8*len(spad)).from_buffer_copy(spad)
            helper=FallEE(elf);pos=0;errors=[]
            @View
            def view(_,a,size,write):
                if a+size<=len(ram):return C.addressof(ram)+a
                if 0x70000000<=a and a+size<=0x70004000:return C.addressof(sp)+a-0x70000000
                return None
            def current():return tuple(bytes(ram[a:a+size]) for a,size in spans)+(bytes(sp[0x3600:0x3610]),bytes(sp[0x3B80:0x3BA0]))
            @Worker
            def worker(_,ptr):
                nonlocal pos
                c=ptr.contents
                try:
                    actual=(c.function,tuple(c.a[i]&0xFFFFFFFF for i in range(c.na)),tuple(c.f[:c.nf]),c.sp,current())
                    assert pos<len(expected) and actual==expected[pos],(cap.name,hex(fn),phase,t,pos,actual[:4],expected[pos][:4] if pos<len(expected) else 'extra')
                    pos+=1;helper.mem[:]=bytes(ram);helper.spad[:]=bytes(sp);helper.hooks={}
                    if c.function in EXTERNAL:helper.r[2]=0
                    else:helper.call(c.function,actual[1],tuple(number(v) for v in actual[2]))
                    C.memmove(ram,bytes(helper.mem),len(ram));C.memmove(sp,bytes(helper.spad),len(sp))
                    c.v0=helper.r[2]&((1<<64)-1);c.f0=helper.f[0]&0xFFFFFFFF;return 0
                except Exception as e:errors.append(e);return -1
            host=Host(None,view,worker);call=Call(function=fn,sp=STACK_TOP,na=3);call.a[:3]=(node,block,record);fault=C.c_uint32()
            rc=n.em_area01_script_worker_call(C.byref(host),C.byref(call),C.byref(fault))
            if errors:raise errors[0]
            assert rc==0 and pos==len(expected),(cap.name,hex(fn),hex(fault.value),rc,pos,len(expected))
            assert current()==want,(cap.name,hex(fn),phase,t,'final bytes')
            assert call.v0&0xFFFFFFFF==ret,(cap.name,hex(fn),phase,t,'return')
            total+=1;boundaries+=pos
        print(cap.name,len(cases),'exact cases',flush=True)
    banner(f'{total} script worker cases',f'{boundaries} exact original worker boundaries',f'{time.monotonic()-start:.1f}s')
if __name__=='__main__':main()
