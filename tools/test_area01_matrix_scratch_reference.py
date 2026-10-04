#!/usr/bin/env python3
"""Exact live 3000 matrix alias plus original actor/SDK matrix writers."""
import ctypes as C
import json
from pathlib import Path
import struct
import subprocess
import time
import reference_mode as mode
import test_area01_render_reference as R
import export_area01_common as A
import export_area11_roster as roster
import export_effect_tables as effects
from test_player_slide_reference import read_elf
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/level2/matrix-scratch'
def u32(b,a):return struct.unpack_from('<I',b,a)[0]
def build():
    OUT.mkdir(parents=True,exist_ok=True);target=OUT/'bridge.dylib'
    sources=['tests/area01_matrix_scratch_bridge.c','src/game/em_area01_live.c',
        'src/game/em_area01_sys.c','src/game/em_area01_math_core.c','src/game/em_area01_math_actor.c',
        'src/game/em_aim_fire_sdk_memory.c','src/game/em_owner_services_original.c',
        'src/game/em_effect_original.c','src/game/em_coll_probe_original.c']
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off',
        '-shared','-fPIC','-Isrc','-Wl,-dead_strip','-Wl,-exported_symbol,_ms_*',*sources,'-o',str(target)],
        cwd=ROOT,check=True)
    n=C.CDLL(str(target));n.ms_seed.argtypes=[C.c_void_p,C.c_void_p]
    n.ms_call.argtypes=[C.c_uint32,C.c_uint32];return n
class Boundary(Exception):pass
def main():
    start=time.time();n=build();elf=read_elf();cases=prefixes=captures=0
    # Three immutable source records, found by the original roster walk.
    boot=A.read_elf();read=A.static_reader(boot,A.read_overlay())
    _,_,placements=roster.walk_roster(read,1,0)
    rows=[]
    for record in placements:
        if u32(record,0x24)==0x15A2C0:
            row,link=struct.unpack_from('<hh',record,8);rows.append(row);assert link==0
    assert len(rows)==8 and set(rows)=={0,1,5}
    blob=(ROOT/'assets/effect_tables.emet').read_bytes()
    blocks={a:blob[o:o+size] for a,size,o,_ in
            (struct.unpack_from('<4I',blob,16+16*i) for i in range(u32(blob,8)))}
    configs=[(a,blocks[a]) for a in (0x248120,0x248184)]
    assert [len(b) for _,b in configs]==[40,20]
    for a,b in configs:assert b==effects.elf_block(boot,a,len(b))
    paths=[ROOT.parent/'Extermination/build/s87/route/15_level_exit']
    if mode.FULL:
        paths+=sorted(p.parent for p in (ROOT.parent/'Extermination/build/s87/route_a01').glob('a01_*/eeMemory.bin'))
    for path in paths:
        ram=path.joinpath('eeMemory.bin').read_bytes();spr=bytearray(path.joinpath('scratchpad.bin').read_bytes())
        if ram[0x810700:0x810702]!=b'\1\0':continue
        captures+=1
        for a,b in configs:assert ram[a:a+len(b)]==b
        spr[0x3000:0x3040]=bytes([0xA5])*64
        nodes=R.pool(ram,{0x128C10})
        for node in nodes if mode.FULL else nodes[:1]:
            for fn in (0x1C3BE0,0x1C3D60,0x128C10):
                e=R.A01EE(elf,ram=ram,spad=spr,journal=False)
                native=C.create_string_buffer(ram);scratch=C.create_string_buffer(bytes(spr))
                n.ms_seed(native,scratch)
                if fn==0x128C10:
                    def stop(ee):raise Boundary()
                    e.hooks[0x1B17A0]=stop
                    try:R.oracle_call(e,fn,(node,))
                    except Boundary:pass
                    else:continue # another room / paused owner does not reach matrix work
                    assert n.ms_call(fn,node)<0 and n.ms_stop()==0x1B17A0,(path.name,hex(node),hex(n.ms_stop()))
                    prefixes+=1
                else:
                    R.oracle_call(e,fn,(node,node+0x1F0))
                    assert n.ms_call(fn,node)==0,(path.name,hex(node),hex(fn),hex(n.ms_stop()))
                assert n.ms_snapshot()==0,'another matrix copy received stores'
                assert native.raw[:-1]==bytes(e.mem),(path.name,hex(node),hex(fn),'RAM',R.first_differences(native.raw[:-1],bytes(e.mem)))
                assert scratch.raw[:-1]==bytes(e.spad),(path.name,hex(node),hex(fn),'scratch',R.first_differences(scratch.raw[:-1],bytes(e.spad),0x70000000))
                cases+=1
    n.ms_seed(native,scratch);assert n.ms_contracts()==13
    # The production byte resolver must block overlapping requests before
    # any fallback and use the exact accessor exercised above.
    text=(ROOT/'src/game/em_area01_live.c').read_text()
    assert 'if(overlaps(a,n,0x70003000u,sizeof l->scratch_3000))\n        return em_area01_live_matrix_3000(l,a,n);' in text
    result=dict(status='PASS',mode=mode.MODE,captures=captures,cases=cases,
                field_placements=len(rows),config_rows=len(set(rows)),config_bytes=60,
                compared_config_bytes=60*captures,
                original_class2_prefixes=prefixes,alias_contracts=13,seconds=round(time.time()-start,2))
    (OUT/('full.json' if mode.FULL else 'quick.json')).write_text(json.dumps(result,indent=2)+'\n')
    print('AREA01 matrix scratch:',json.dumps(result,sort_keys=True))
if __name__=='__main__':main()
