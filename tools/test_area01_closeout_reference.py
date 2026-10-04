#!/usr/bin/env python3
"""Actual collision-world close-out and existing SYS pairs vs original code.

Captures are isolated test fixtures. No captured state enters gameplay.
"""
import ctypes as C
import json
import random
import struct
import subprocess
import time
from pathlib import Path

import reference_mode as RM
from export_area01_common import captures, read_elf, u32
from test_area01_math_reference import A01Base
from test_coll_list_passes_reference import HOOK_ORDER, PUBLISHED, check_code
from test_player_fall_reference import nested_bits

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/level2/closeout'
PLAYER=0x8102B0
PAIR=(0x1A8840,0x1A9E00)


def build():
    OUT.mkdir(parents=True,exist_ok=True)
    sources=['collision_world','actor_collision','collision','actor_pool',
             'coll_probe_original','effect_original','coll_list_passes_walkers',
             'coll_list_passes','coll_segment_walkers','coll_move_original',
             'coll_grid_hull','sdk_math_original','sdk_soft_float','interaction_scan',
             'area01_sys','area01_math_core','area01_math_player','area01_collision_view',
             'startup_load_gaps']
    path=OUT/'closeout.dylib'
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror',
                    '-ffp-contract=off','-shared','-fPIC','-Isrc',
                    'tests/area01_closeout_bridge.c',
                    *[f'src/game/em_{s}.c' for s in sources],'-lm','-o',str(path)],
                   cwd=ROOT,check=True)
    n=C.CDLL(str(path));n.sg_load.argtypes=[C.c_char_p,C.c_char_p]
    n.cg_setup.argtypes=[C.c_void_p,C.c_void_p]
    n.cg_run.argtypes=[C.POINTER(C.c_uint32)]
    n.cg_calls.restype=C.POINTER(C.c_uint32)
    n.cg_refuse.argtypes=[C.c_uint32,C.c_int]
    n.em_collision_world_contact_bytes.argtypes=[C.c_uint32,C.c_uint32]
    n.em_collision_world_contact_bytes.restype=C.c_void_p
    return n


def main():
    start=time.monotonic();n=build();elf=read_elf();records=captures()
    rng=random.Random(0xA8840);runs=contacts=pushes=contracts=0;calls_seen={fn:0 for fn in PAIR}
    emcl=str(ROOT/'assets/area01/area01.emcl').encode()
    cells=str(ROOT/'assets/area01/area01_cells.bin').encode()

    def setup(ram,spad):
        assert n.sg_load(emcl,cells)==0
        buf=(C.c_uint8*len(ram)).from_buffer(ram)
        sbuf=(C.c_uint8*len(spad)).from_buffer(spad)
        assert n.cg_setup(buf,sbuf)==0
        return buf,sbuf

    def compare(capture,edit=None,only=None):
        nonlocal runs,contacts,pushes
        ram=bytearray(capture.ram);spad=bytearray(capture.spad)
        if edit:edit(ram,spad)
        if only is not None:
            for live,(pc,pn) in PUBLISHED.items():
                if live!=only:struct.pack_into('<H',ram,pn,0)
            if only==0x275B90:
                # Retain the first real class-2 owner; the two-owner AA140
                # branch is separately guarded until its AA000 is bound.
                struct.pack_into('<H',ram,0x275B94,1)
        for live,(pc,pn) in PUBLISHED.items():
            ram[live:live+4]=ram[pc:pc+4];ram[live+8:live+10]=ram[pn:pn+2]
        before=bytes(ram);o=A01Base(elf,ram,spad);want=[]
        def pair(e,fn):
            args=tuple(e.r[4:6]);want.append((fn,*args))
            del e.hooks[fn]
            try:e.r[2],e.f[0]=nested_bits(e,fn,args)
            finally:e.hooks[fn]=lambda ee,f=fn:pair(ee,f)
        for fn in PAIR:o.hooks[fn]=lambda e,f=fn:pair(e,f)
        for fn in HOOK_ORDER:o.call(fn,(PLAYER,) if fn in (0x1A9F60,0x1A8BE0) else ())
        buffers=setup(ram,spad);fault=C.c_uint32()
        assert n.cg_run(C.byref(fault))==0,(capture.name,edit,hex(fault.value))
        got=[tuple(n.cg_calls()[3*i+k] for k in range(3)) for i in range(n.cg_count())]
        assert got==want,(capture.name,'pair order',got,want)
        assert bytes(ram)==bytes(o.mem),(capture.name,'RAM',next(hex(i) for i,(a,b) in enumerate(zip(ram,o.mem)) if a!=b))
        for k in range(2):assert n.cg_span(k)==o.load(0x70003B86+2*k,2),(capture.name,'span',k,n.cg_span(k),o.load(0x70003B86+2*k,2))
        # The native close-out also performs the original list publication.
        for i,(_,pn) in enumerate(PUBLISHED.values()):assert n.cg_published(i)==struct.unpack_from('<h',before,pn)[0]
        for fn,_,a in got:
            calls_seen[fn]+=1
            contacts+=fn==PAIR[0] and ram[a+0xA]!=before[a+0xA]
            pushes+=fn==PAIR[1] and ram[a+0xB0:a+0xBC]!=before[a+0xB0:a+0xBC]
        runs+=1
        return buffers,ram,spad

    n.em_collision_world_unload();assert n.cg_bind(1)==-1;contracts+=1
    for capture in records:
        check_code(elf,capture.ram,capture.name)
        # s6's later AA140 reaches a separate, still unbound AA000. This
        # fixture isolates its actual captured class-2 list, retaining the
        # complete nine-pass sequence and the caught 1A9F60->1A9E00 calls.
        compare(capture,only=0x275B90 if capture.name=='a01_s6_bridge_blocked' else None)
        p=n.em_collision_world_contact_bytes(0x275490,8)
        assert p and C.string_at(p,8)==capture.ram[0x275490:0x275498];contracts+=1

    c=records[0];entry=u32(c.ram,u32(c.ram,0x275B9C))
    assert c.ram[entry+3]==3
    def contact(trial):
        def edit(ram,spad):
            # Keep two real class-D entries; exact first-pair contact must
            # clear 3B86 so the second entry is not visited.
            cur=u32(ram,0x275B9C);struct.pack_into('<H',ram,0x275BA4,2)
            struct.pack_into('<II',ram,cur,entry,entry)
            ram[entry]=1;ram[entry+3]=3;ram[entry+0xA]=0;ram[entry+0xB]=1
            ram[entry+0xD]=trial%3;ram[entry+0x56]=trial&255;ram[PLAYER]=1
            ram[0x810707]=1 if trial%4==1 else 0
            x,y,z=struct.unpack_from('<3f',ram,entry+0xB0)
            if trial%4==2:x+=10000
            struct.pack_into('<3f',ram,PLAYER+0xA0,x,y,z)
            struct.pack_into('<h',ram,0x28A9A0,0);spad[0x3B8D]=0
        return edit
    for i in range(RM.pick(96,12)):compare(c,contact(i),0x275BA0)

    c=next(x for x in records if x.name=='a01_s6_bridge_blocked')
    entry=u32(c.ram,u32(c.ram,0x275B8C))
    assert c.ram[entry+2]&31==2 and c.ram[entry+3]==0
    def push(trial):
        def edit(ram,spad):
            ram[entry]=1;ram[entry+3]=trial%5==1;ram[entry+0xB]=0
            ram[PLAYER]=4 if trial%4==1 else 1
            x,y,z=struct.unpack_from('<3f',ram,entry+0xB0)
            struct.pack_into('<3f',ram,PLAYER+0xA0,x+rng.choice((.5,2,1000)),
                             y+(1000 if trial%6==1 else 0),z+1)
            struct.pack_into('<h',ram,0x28A9A0,0);spad[0x3B8D]=0
        return edit
    for i in range(RM.pick(128,12)):compare(c,push(i),0x275B90)

    # Binding failure leaves the existing contract active; absent/refused
    # workers and records retain their exact pass call-site fail-stops.
    c=records[0]
    entry=u32(c.ram,u32(c.ram,0x275B9C))
    for mode,refuse,worker,expected in ((0,0,0,0x1A8BE0),(1,entry,0,0x1A8BE0),
                                      (1,0,1,0x1A8C94)):
        ram=bytearray(c.ram);spad=bytearray(c.spad);buffers=setup(ram,spad)
        assert n.cg_bind(2)==-1 and n.cg_bind(3)==-1;contracts+=2
        assert n.cg_bind(mode)==0;n.cg_refuse(refuse,worker);fault=C.c_uint32()
        assert n.cg_run(C.byref(fault))==-1
        # Unbound record providers have no generic captured owner projection.
        assert fault.value==expected,(mode,refuse,worker,hex(fault.value),hex(expected));contracts+=1
    n.em_collision_world_unload();assert n.cg_bind(1)==-1;contracts+=1
    for a,size in ((0x275490,0),(0x27548F,1),(0x275498,1),(0x275497,2),(0x275490,0xFFFFFFFF)):
        assert not n.em_collision_world_contact_bytes(a,size);contracts+=1
    out={'runs':runs,'pair_calls':{f'{k:08X}':v for k,v in calls_seen.items()},
         'contacts':contacts,'pushes':pushes,'contracts':contracts,
         'captures':len(records),'seconds':round(time.monotonic()-start,3)}
    assert contacts and pushes
    (OUT/('full.json' if RM.FULL else 'quick.json')).write_text(json.dumps(out,indent=2)+'\n')
    print('AREA01 close-out original reference PASS',json.dumps(out))


if __name__=='__main__':main()
