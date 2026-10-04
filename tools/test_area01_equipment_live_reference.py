#!/usr/bin/env python3
"""Generic C5C90 parent/model/slot/pool composition against original code."""
import ctypes as C
import itertools
import json
import struct
import time
import reference_mode as mode
import export_area01_common as A
import test_area01_model_live_reference as M
from test_area01_model_live_reference import u32
from test_module_loader_reference import read_elf_bytes
from test_coll_move_reference import FloatEE

def main():
    start=time.monotonic();M.OUT=A.ROOT/'build/level2/equipment';lib=M.build();elf=read_elf_bytes();caps=A.captures()
    caps=caps if mode.FULL else [caps[0]]
    lib.am_equipment_parent.argtypes=[C.c_uint32]
    lib.am_equipment_parent_raw.argtypes=[C.c_uint32,C.c_uint32]
    total=draws=frees=raw_parent_cases=0
    for capture in caps:
        ram0=capture.ram;spad=capture.spad
        child=next(a for a in range(0x7A5640,0x7D4640,0x2F0) if u32(ram0,a+16)==0x826CF0 and ram0[a+3]==1)
        parent=child-0x2F0
        shapes=list(itertools.product((0,1,2,3,4),(0,1,2),(0,1),(0,1)))
        shapes += [(1,0,1,1,k) for k in (0x47,0x4E,0x54,0x55,0x58,0x59,0x5A,0x5D,0x5E,0x6A,0x46,0x6B)]
        shapes += [(1,0,1,1,0x47,1)]
        shapes=mode.select(shapes,25,0x1C5C90,axes=(lambda x:x[0],lambda x:x[1]),keep=lambda i,x:len(x)>=5)
        for shape in shapes:
            life,plife,bones,visible,*kind=shape;ram=bytearray(ram0)
            struct.pack_into('<I',ram,0x275B40,(parent if len(kind)>1 else child)+0x110)
            ram[child+4]=life;ram[parent+4]=plife;ram[parent+9]=ram[parent+9] if bones else 0
            ram[parent+1]=visible
            if kind:ram[parent+0xD]=kind[0]
            if not life:ram[child+9]=ram[child+0xC]=0
            native=C.create_string_buffer(bytes(ram));scratch=C.create_string_buffer(spad)
            assert lib.am_seed(native,scratch,child,1)==0
            assert lib.am_equipment_parent(parent)==0
            o=FloatEE(elf,native.raw[:-1],scratch.raw[:-1]);calls=[]
            o.hooks[0x1CAA00]=lambda ee:calls.append((0x1CAA00,ee.r[4]))
            o.call(0x1C5C90,(child,))
            out=C.c_uint32();assert lib.am_call(0x1C5C90,child,0,0,0,0,C.byref(out))==0,(shape,hex(lib.am_fault()))
            lib.am_equipment_snapshot();got=native.raw[:-1];want=bytes(o.mem)
            assert got==want,(shape,next((hex(i),x,y) for i,(x,y) in enumerate(zip(got,want)) if x!=y))
            assert scratch.raw[:-1]==bytes(o.spad),(shape,'scratch')
            assert lib.am_external_calls()==len(calls)+(life in (2,3))
            total+=1;draws+=len(calls);frees+=life in (2,3)
        for value in (0x41F80000,0xC1580000):
            ram=bytearray(ram0);ram[child+4]=ram[parent+4]=ram[parent+1]=1
            struct.pack_into('<I',ram,0x275B40,child+0x110)
            native=C.create_string_buffer(bytes(ram));scratch=C.create_string_buffer(spad)
            assert lib.am_seed(native,scratch,child,1)==0
            assert lib.am_equipment_parent(parent)==0
            o=FloatEE(elf,native.raw[:-1],scratch.raw[:-1]);calls=[]
            o.hooks[0x1CAA00]=lambda ee:calls.append((0x1CAA00,ee.r[4]))
            o.save(u32(o.mem,parent+0x114)+0xC0,value)
            assert lib.am_equipment_parent_raw(parent,value)==0
            o.call(0x1C5C90,(child,))
            out=C.c_uint32();assert lib.am_call(0x1C5C90,child,0,0,0,0,C.byref(out))==0,hex(lib.am_fault())
            lib.am_equipment_snapshot();got=native.raw[:-1];want=bytes(o.mem)
            assert got==want,('raw parent matrix',next((hex(i),x,y) for i,(x,y) in enumerate(zip(got,want)) if x!=y))
            assert scratch.raw[:-1]==bytes(o.spad),'raw parent scratch'
            assert lib.am_external_calls()==len(calls)
            raw_parent_cases+=1;draws+=len(calls)
    report=dict(status='PASS',captures=len(caps),cases=total,draw_boundaries=draws,native_pool_frees=frees,
                raw_parent_matrix_cases=raw_parent_cases,
                seconds=round(time.monotonic()-start,2))
    out=M.OUT/'report.json';out.write_text(json.dumps(report,indent=2)+'\n')
    print('AREA01 equipment:',json.dumps(report,sort_keys=True))
if __name__=='__main__':main()
