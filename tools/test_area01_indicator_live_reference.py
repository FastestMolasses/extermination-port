#!/usr/bin/env python3
"""Canonical indicator child/bind/slot composition versus original code.

The only normal boundaries are RNG and final CACB0 draw (whose actual
translation has its separate AREA01 draw oracle). Original binds, default
bones, placement, colour, pool free and bone release execute in full.
Parent writes go through the actor and slot resolvers between callbacks.
Compare complete RAM and scratch; the private interpreter call stack is
outside those images. No captures or instruction bytes enter native code.
"""
import ctypes as C
import json
import struct
import subprocess
import time
import reference_mode as mode
from test_area01_model_live_reference import ROOT,DECOMP,POSE_SOURCES,u32
from test_module_loader_reference import read_elf_bytes
from test_coll_move_reference import FloatEE

OUT=ROOT/'build/level2/indicator'

def build():
    OUT.mkdir(parents=True,exist_ok=True)
    sources=['tests/area01_indicator_bridge.c','src/game/em_area01_indicator_live.c',
             'src/game/em_indicator_child.c','src/game/em_effect_kinds.c',
             'src/game/em_area01_model_live.c','src/game/em_area01_actor_view.c',
             'src/game/em_actor_pool.c','src/game/em_area01_math_core.c','src/game/em_area01_math_owner.c',
             'src/game/em_area01_math_actor.c',
             'src/game/em_owner_draw_original.c','src/game/em_roger_actor_original.c',
             'src/game/em_startup_load_gaps.c','src/game/em_opening_face.c',
             'src/game/em_render_verify_rest.c','src/game/em_anim_runtime_rest.c',*POSE_SOURCES]
    path=OUT/'indicator.dylib'
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off',
        '-shared','-fPIC','-Isrc','-Wl,-dead_strip','-Wl,-exported_symbol,_ai_*',
        *dict.fromkeys(sources),'-o',str(path)],cwd=ROOT,check=True)
    lib=C.CDLL(str(path));lib.ai_seed.argtypes=[C.c_void_p,C.c_void_p,C.c_uint32,C.c_int]
    lib.ai_step.argtypes=[C.c_uint32]*3;lib.ai_write.argtypes=[C.c_uint32,C.c_void_p,C.c_uint32]
    lib.ai_cap.argtypes=[C.c_int16];lib.ai_color.restype=lib.ai_matrix.restype=C.POINTER(C.c_uint32)
    return lib

def main():
    start=time.monotonic();lib=build();elf=read_elf_bytes()
    paths=sorted((DECOMP/'build/s87/route_a01').glob('a01_*/eeMemory.bin'))
    paths=[p for p in paths if p.read_bytes()[0x810700:0x810702]==b'\1\0']
    paths=paths if mode.FULL else [paths[0]]
    cases=steps=draws=rngs=parent_writes=0
    for path in paths:
        original=path.read_bytes();spad=(path.parent/'scratchpad.bin').read_bytes()
        node=0x7A5640
        for entry,kind in [(0x1C5680,k) for k in (0x73,0x74,0x75,0x7A)]+[(0x1C5760,k) for k in (0,4,21)]:
            for cap in ((None,0,-1) if mode.FULL else (None,0)):
                ram=bytearray(original)
                # Exact fields written by manual 826D40 child spawn, plus
                # deterministic parent values for placement and colour.
                ram[node+4]=ram[node+9]=ram[node+0xC]=0;ram[node+2]=0xC;ram[node+0xD]=kind
                ram[node+0xA]=int(kind%2==0)
                struct.pack_into('<I',ram,node+0x10,entry)
                struct.pack_into('<4f',ram,node+0xA0,0.,1.,0.,.25)
                struct.pack_into('<4f',ram,node+0x60,1.,1.,1.,1.)
                struct.pack_into('<4f',ram,node+0xB0,3.,5.,7.,1.)
                struct.pack_into('<4f',ram,node+0xC0,.1,.2,.3,1.)
                native=C.create_string_buffer(bytes(ram));scratch=C.create_string_buffer(spad)
                assert lib.ai_seed(native,scratch,node,0)==0
                if cap is not None:lib.ai_cap(cap)
                lib.ai_snapshot();o=FloatEE(elf,native.raw[:-1],scratch.raw[:-1])
                expected_draw=[];random=0
                def rnd(ee):ee.r[2]=random
                def draw(ee):
                    a=ee.r[4];slot=ee.load(a+0x110,4)
                    expected_draw.append((bytes(ee.mem[a+0x80:a+0x90]),bytes(ee.mem[slot+0x90:slot+0xD0])))
                    ee.save(a+1,1,1)
                o.hooks={0x122BB8:rnd,0x1CACB0:draw}
                sequence=[None] if cap is not None else [None,0,0x40000000,0x7FFFFFFF,'free']
                for phase in sequence:
                    if isinstance(phase,int):
                        random=phase
                        # The parent updates both colour and one raw bone
                        # matrix through +110, exactly where 826D40 writes.
                        color=struct.pack('<4f',1.,phase/2147483648.,.125,.25)
                        assert lib.ai_write(node+0xA0,color,16)==0;o.mem[node+0xA0:node+0xB0]=color
                        slot=u32(o.mem,node+0x110)
                        matrix=struct.pack('<16f',*[float(k+phase%7) for k in range(16)])
                        assert lib.ai_write(slot+0x90,matrix,64)==0;o.mem[slot+0x90:slot+0xD0]=matrix
                        parent_writes+=2
                    elif phase=='free':
                        assert lib.ai_write(node+4,b'\x02',1)==0;o.save(node+4,2,1)
                    o.call(entry,(node,))
                    assert lib.ai_step(entry,node,random)==0,(hex(entry),kind,phase,hex(lib.ai_fault()))
                    lib.ai_snapshot();got=native.raw[:-1];want=bytes(o.mem)
                    assert got==want,(hex(entry),kind,cap,phase,next((hex(i),x,y) for i,(x,y) in enumerate(zip(got,want)) if x!=y))
                    assert scratch.raw[:-1]==bytes(o.spad),(hex(entry),kind,'scratch')
                    assert lib.ai_draws()==len(expected_draw)
                    if expected_draw:
                        assert C.string_at(lib.ai_color(),16)==expected_draw[-1][0]
                        assert C.string_at(lib.ai_matrix(),64)==expected_draw[-1][1]
                    steps+=1
                assert lib.ai_rngs()==len(expected_draw)
                draws+=lib.ai_draws();rngs+=lib.ai_rngs();cases+=1
    fault_cases=0
    for entry,state in ((0x1C5680,0),(0x1C5760,1),(0x1C5680,2)):
        ram=bytearray(original);ram[node+4]=state
        native=C.create_string_buffer(bytes(ram));scratch=C.create_string_buffer(spad)
        assert lib.ai_seed(native,scratch,node,0)==0;lib.ai_reject()
        assert lib.ai_step(entry,node,7)<0 and lib.ai_fault()
        first=lib.ai_fault();assert lib.ai_step(entry,node,7)<0 and lib.ai_fault()==first
        fault_cases+=1
    report=dict(status='PASS',mode='full' if mode.FULL else 'quick',captures=len(paths),
                cases=cases,steps=steps,draw_boundaries=draws,random_boundaries=rngs,
                parent_writes=parent_writes,fault_cases=fault_cases,seconds=round(time.monotonic()-start,2))
    (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print('AREA01 indicators:',json.dumps(report,sort_keys=True))
if __name__=='__main__':main()
