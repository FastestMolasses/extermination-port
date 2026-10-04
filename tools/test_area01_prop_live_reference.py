#!/usr/bin/env python3
"""Canonical placed-prop adapter versus original caller instructions.

Model, publication, draw and free are explicit worker boundaries. Their
effects exercise refreshed canonical metadata; this proves the adapter's
ordering and arguments, not the separate workers' game behavior.
"""
import ctypes as C
import random
import struct
import subprocess
from pathlib import Path
import reference_mode as mode
from test_area01_runtime_reference import Call,Host,View,Worker
from test_player_fall_reference import FallEE
from test_player_slide_reference import read_elf,STACK_TOP,RETURN

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/level2/prop'
NODE=0x7A5640
SIZE=0x2F0
METHOD=0x1CAA00
NEW_METHOD=0x1CB360

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    libpath=OUT/'prop.dylib'
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off',
                    '-shared','-fPIC','-Isrc','src/game/em_area01_prop_live.c',
                    'src/game/em_status_ui_leftovers.c','-o',str(libpath)],cwd=ROOT,check=True)
    lib=C.CDLL(str(libpath))
    lib.em_area01_prop_call.argtypes=[C.POINTER(Host),C.POINTER(Call),C.POINTER(C.c_uint32)]
    o=FallEE(read_elf());cases=boundaries=0
    states=range(256) if mode.FULL else (0,1,2,3,4,255)
    for state in states:
        for bound in (0,1,-1):
            for changed in (False,True):
                initial=bytearray(random.Random(state*19+bound).randbytes(SIZE))
                initial[4]=state
                struct.pack_into('<I',initial,0x4C,METHOD)
                o.mem[NODE:NODE+SIZE]=initial
                expected=[]
                def effect(fn,write):
                    if changed:
                        if fn==0x1B0FD0:write(4,b'\1')
                        if fn==0x1B17A0:write(0x4C,struct.pack('<I',NEW_METHOD))
                        write(0x50,struct.pack('<I',fn))
                def hook(fn):
                    def apply(ee):
                        expected.append((fn,ee.r[4],ee.r[29]))
                        effect(fn,lambda at,data:ee.mem.__setitem__(slice(NODE+at,NODE+at+len(data)),data))
                        ee.r[2]=bound & ((1<<64)-1) if fn==0x1B0FD0 else 0
                    return apply
                entries=(0x1B0FD0,0x1C6380,0x1B17A0,0x1AFC10,METHOD,NEW_METHOD)
                o.hooks={fn:hook(fn) for fn in entries}
                o.r[4]=NODE;o.r[29]=STACK_TOP;o.r[31]=RETURN;o.run(0x1C4820)
                buf=C.create_string_buffer(bytes(initial),SIZE);got=[]
                @View
                def view(_ctx,a,n,write):
                    if not write and NODE<=a and 0<n<=SIZE and a-NODE<=SIZE-n:
                        return C.addressof(buf)+a-NODE
                    return None
                @Worker
                def worker(_ctx,ptr):
                    c=ptr.contents
                    assert c.na==1 and c.nf==0
                    got.append((c.function,c.a[0],c.sp))
                    effect(c.function,lambda at,data:C.memmove(C.addressof(buf)+at,data,len(data)))
                    c.v0=bound & ((1<<64)-1) if c.function==0x1B0FD0 else 0
                    return 0
                host=Host(None,view,worker)
                call=Call(function=0x1C4820,sp=STACK_TOP,na=1);call.a[0]=NODE
                fault=C.c_uint32()
                assert lib.em_area01_prop_call(C.byref(host),C.byref(call),C.byref(fault))==0
                assert got==expected,(state,bound,changed,got,expected)
                assert buf.raw==o.mem[NODE:NODE+SIZE]
                cases+=1;boundaries+=len(got)
    @View
    def denied(*_):return None
    host.bytes=denied
    fault=C.c_uint32()
    assert lib.em_area01_prop_call(C.byref(host),C.byref(call),C.byref(fault))==-1 and fault.value==NODE
    host.bytes=view;buf[4]=b'\0'
    @Worker
    def failed(*_):return -1
    host.worker=failed;fault=C.c_uint32()
    assert lib.em_area01_prop_call(C.byref(host),C.byref(call),C.byref(fault))==-1 and fault.value==0x1B0FD0
    print(f'AREA01 prop: PASS {cases} original caller cases, {boundaries} exact worker boundaries, 2 refusals')

if __name__=='__main__':main()
