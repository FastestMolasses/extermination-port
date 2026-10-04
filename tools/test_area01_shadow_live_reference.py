#!/usr/bin/env python3
"""AREA01 generic shadow hook with the sole shared shadow owner.
Receiver bytes come from the disc exporter; original instructions independently
check state matrices, passes and receiver order. No GPU is required here.
"""
import ctypes as C
import json
import struct
import time
import reference_mode as mode
import test_area01_model_draw_reference as D
import test_shadow_original_reference as S
import export_area01_shadow_receivers as E
from test_coll_move_reference import FloatEE

def check(lib,elf,ram,spr,node,force,face=0):
    ram=bytearray(ram);ctx=D.u32(ram,0x275670)
    if force:ram[ctx+0x2240:ctx+0x2280]=bytes(64)
    if face:ram[0x8106D5]=face
    memory=C.create_string_buffer(bytes(ram));scratch=C.create_string_buffer(spr)
    assert lib.as_seed(memory,scratch,node)==0
    if face:
        lib.as_actor_snapshot();ram=bytearray(memory.raw[:-1])
    assert (lib.as_face(node) if face else lib.as_call(node))==0
    plan=lib.as_plan().contents
    oracle=S.ShadowRam(elf,bytes(ram),spr)
    start=0x740000;oracle.save(ctx+0x10,start)
    oracle.run(S.BEGIN_CTX,[node,0x2F0,ram[node+9]])
    oracle.run(S.SHADOW,[node])
    end=int.from_bytes(oracle.read(ctx+0x10,4),'little')
    if face:
        # BA580 writes nothing before its shadow worker. The complete
        # shadow chain above supplies the plan oracle; the remainder runs
        # original BA580/face instructions with that worker as its boundary.
        original=FloatEE(elf,ram,spr)
        calls=[]
        original.hooks[0x1DA6A0]=lambda ee:calls.append(ee.r[4]&0xFFFFFFFF)
        original.hooks[0x122BB8]=lambda ee:ee.r.__setitem__(2,7)
        original.call(0x1BA580,(node,ram[node+0xD]))
        assert calls==[node]
        lib.as_actor_snapshot()
        got=memory.raw[:-1]
        for a,n in ((node,0x2F0),(0x8106D4,10),(0x7D5840,0x480*0xD0)):
            assert got[a:a+n]==bytes(original.mem[a:a+n]),('BA580 canonical fields',face,hex(a))
    assert bool(plan.drawn)==(end!=start)
    stats={}
    if plan.drawn:
        for field,address,count in (('view_817F20',0x817F20,16),('eye_817F60',0x817F60,4),
            ('light_817F70',0x817F70,4),('right_817F80',0x817F80,4),('up_817F90',0x817F90,4),
            ('far_817FA0',0x817FA0,4),('near_817FB0',0x817FB0,4),('cross_817FC0',0x817FC0,4),
            ('uv_24B0',ctx+0x24B0,16)):
            assert bytes(getattr(plan,field))==oracle.read(address,count*4),(field,hex(node),force)
        assert bytes(lib.as_state().contents.d817FF0)==oracle.read(0x817FF0,16)
        output=bytearray(ram)
        output[start:end]=oracle.read(start,end-start)
        units=S.walk(output,start,end)
        S.box_checks(output,units,plan,ram,stats)
        first=next(j for j,u in enumerate(units) if u[1]==5 and u[3]==S.RECEIVER_KERNEL)
        expected=[]
        for r in plan.receiver[:plan.receiver_count]:
            address=S.object_address(ram,r.id)+0x40
            expected.append((address,S.RECEIVER_KERNEL))
            if r.cls==2:expected.append((address,S.CLIP_KERNEL))
        assert S.receiver_sequence(units,first-3)==expected
        commands=(C.c_uint32*2048)();n=lib.as_commands(commands,len(commands))
        assert list(commands[:2*n:2])==[0,1,1,2,3]+[4]*plan.receiver_count+[5]
        assert lib.as_select(11,0)==-1 and lib.as_area()==1
    assert lib.as_select(99,0)==-1 and lib.as_area()==1
    lib.as_clear_passes()
    assert lib.as_select(11,0)==0 and lib.as_area()==11
    assert lib.as_select(1,0)==0 and lib.as_area()==1
    return int(plan.drawn),plan.receiver_count,end-start
def main():
    start=time.time();data,export=E.build()
    asset=D.ROOT/'assets/area01_shadow_receivers.emsr'
    assert asset.read_bytes()==data,'regenerate AREA01 shadow receivers'
    lib=D.build('tests/area01_shadow_bridge.c','as',('src/game/em_shadow_original.c',))
    lib.as_seed.argtypes=[C.c_void_p,C.c_void_p,C.c_uint32]
    lib.as_call.argtypes=[C.c_uint32];lib.as_face.argtypes=[C.c_uint32]
    lib.as_plan.restype=C.POINTER(S.Plan);lib.as_state.restype=C.POINTER(S.State)
    lib.as_commands.argtypes=[C.POINTER(C.c_uint32),C.c_uint32]
    elf=D.read_elf_bytes()
    paths=sorted((D.DECOMP/'build/s87/route_a01').glob('a01_*/eeMemory.bin'))
    paths=[p for p in paths if p.read_bytes()[0x810700:0x810702]==b'\1\0']
    paths=paths if mode.FULL else paths[:1]
    cases=drawn=receivers=packets=face_cases=0
    for path in paths:
        ram=path.read_bytes();spr=(path.parent/'scratchpad.bin').read_bytes()
        node=next(a for a in range(0x7A5640,0x7D4640,0x2F0) if ram[a] and D.u32(ram,a+0x10)==0x825350)
        for force in (False,True):
            d,r,p=check(lib,elf,ram,spr,node,force);cases+=1;drawn+=d;receivers+=r;packets+=p
        for face in ((1,2) if mode.FULL else (1,)):
            d,r,p=check(lib,elf,ram,spr,node,True,face);cases+=1;face_cases+=1
            drawn+=d;receivers+=r;packets+=p
    report=dict(status='PASS',mode='full' if mode.FULL else 'quick',captures=len(paths),cases=cases,
        face_shadow_sequences=face_cases,drawn=drawn,receiver_calls=receivers,original_packet_bytes=packets,export=export,seconds=round(time.time()-start,2))
    (D.OUT/'shadow_report.json').write_text(json.dumps(report,indent=2)+'\n')
    mode.banner(f'{cases} AREA01 shared-shadow cases')
    print('AREA01 shadow live:',json.dumps(report,sort_keys=True))
if __name__=='__main__':main()
