#!/usr/bin/env python3
"""Aura adapter uses canonical actor bytes, checked at RNG/draw boundaries."""
import ctypes as C
import itertools
import json
import struct
import time
import ee_float_model as fm
import reference_mode as mode
import test_pickup_items_reference as P
from test_area01_pickup_live_reference import build,OUT
from test_area01_runtime_reference import Call,Host,View,Worker

DRAW=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_uint32,C.c_uint32,C.c_uint32,C.c_uint32)
NODE=0x7A5640

def main():
    start=time.monotonic();lib=build();lib.ap_draw.argtypes=[DRAW,C.c_void_p]
    elf=(P.DECOMP/'config/SCUS_971.12').read_bytes()
    matrices=[(-1,0,0,0,0,1,0,0,0,0,-1,0,231.1,200.9,428.,1),
              (1,0,0,0,0,1,0,0,0,0,1,0,231.1,200.9,428.,1)]
    eye=struct.pack('<4f',268.2,258.47372,182.8,1.)
    cases=[(0x1F1180,state,timer,angle,variant,index,area,matrix)
           for state,timer,angle,variant,index,area,matrix in itertools.product(
               (0,1,2),(49.,0.,.5,.95,1.0000001),(0.,180.),(0,1,2,4,5),(0,3),(1,2,11),range(2))]
    cases += [(0x1F1110,3,49.,180.,v,3,1,0) for v in (0,1,2,4,5)]
    cases=mode.select(cases,40,0xA01A0A,axes=(lambda x:x[0],lambda x:x[1],lambda x:x[4],lambda x:x[2]))
    boundaries=drawn=0
    for index,(entry,state,timer,angle,variant,idx,area,matrix) in enumerate(cases):
        record=bytearray(0x2F0);struct.pack_into('<16f',record,0xD0,*matrices[matrix])
        struct.pack_into('<IIhhi',record,0x2D0,P.fbits(angle),P.fbits(timer),variant,idx,state)
        o=P.Oracle(elf);o.put(NODE,record);o.put(0x8105D0,eye);o.put(0x810700,bytes([area]))
        for r,mark in P.MARK.items():o.put(r+0x18,struct.pack('<I',mark))
        expected=[];draw={};queue=[(0,119,120,0x7FFFFFFF)[index%4],7]
        def rand(r):
            expected.append(('rand',r.read(NODE,0x2F0)));r.set32(2,queue.pop(0))
        def sine(r):draw['sine']=r.f[12];r.f[0]=P.fbits(.5)
        def matrix_vec(r):draw['mark']=r.load(r.g(6)&P.M32)
        def glint(r):
            rec=next(k for k,v in P.MARK.items() if v==draw['mark'])
            expected.append(('draw',rec,draw['sine'],r.f[12],r.read(NODE,0x2F0)))
        o.calls.update({0x122BB8:rand,0x11E2A8:sine,0x1281C0:lambda r:r.set32(2,0),0x1026A0:matrix_vec,0x1F0A60:glint})
        args=(NODE,variant) if entry==0x1F1110 else (NODE,);o.run(entry,args)
        want=o.read(NODE,0x2F0)
        buf=C.create_string_buffer(bytes(record),len(record));camera=C.create_string_buffer(eye,16);area_buf=C.c_uint8(area)
        position=0;errors=[];queue=[(0,119,120,0x7FFFFFFF)[index%4],7]
        def view(_,a,n,write):
            if NODE<=a and a+n<=NODE+len(record):return C.addressof(buf)+a-NODE
            if 0x8105D0<=a and a+n<=0x8105E0:return C.addressof(camera)+a-0x8105D0
            if a==0x810700 and n==1:return C.addressof(area_buf)
            return None
        def worker(_,ptr):
            nonlocal position
            try:
                assert ptr.contents.function==0x122BB8
                assert expected[position]==('rand',buf.raw),(index,'random boundary')
                position+=1;ptr.contents.v0=queue.pop(0);return 0
            except Exception as e:errors.append(e);return -1
        def native_draw(_,node,rec,a,t):
            nonlocal position
            try:
                assert node==NODE
                have=('draw',rec,fm.ee_mul(P.PI,t),fm.ee_div(fm.ee_mul(P.PI,a),P.F180),buf.raw)
                assert have==expected[position],(index,'draw boundary',have[:4],expected[position][:4])
                position+=1;return 0
            except Exception as e:errors.append(e);return -1
        callbacks=(View(view),Worker(worker),DRAW(native_draw));host=Host(None,*callbacks[:2])
        assert lib.ap_bind(C.byref(host))==0;lib.ap_draw(callbacks[2],None)
        c=Call(function=entry,na=len(args));c.a[:len(args)]=args
        rc=lib.ap_call(C.byref(c))
        if errors:raise errors[0]
        assert rc==0,(index,hex(entry),hex(lib.ap_fault()))
        assert buf.raw==want,(index,'record bytes')
        assert position==len(expected)
        boundaries+=position;drawn+=sum(x[0]=='draw' for x in expected)
    report=dict(status='PASS',cases=len(cases),worker_boundaries=boundaries,draw_boundaries=drawn,seconds=round(time.monotonic()-start,2))
    (OUT/'aura_report.json').write_text(json.dumps(report,indent=2)+'\n');print('AREA01 pickup aura:',json.dumps(report,sort_keys=True))
if __name__=='__main__':main()
