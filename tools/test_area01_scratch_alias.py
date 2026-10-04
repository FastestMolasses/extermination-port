#!/usr/bin/env python3
"""ASan/UBSan actual scratch-owner alias and nested-call contract test."""
import os
import random
from pathlib import Path
import shlex
import subprocess
import struct
import sys
ROOT=Path(__file__).resolve().parents[1]
def main():
    out=ROOT/'build/level2/scratch-alias';out.mkdir(parents=True,exist_ok=True)
    flags=['-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
           '-ffunction-sections','-fdata-sections',
           '-Wl,-dead_strip' if sys.platform=='darwin' else '-Wl,--gc-sections','-Isrc']
    sources=['tests/area01_scratch_alias_test.c','src/game/em_aim_fire_runtime.c',
             'src/game/em_camera_leftovers.c','src/game/em_pose_host_workers.c',
             'src/game/em_aim_fire_sdk_memory.c','src/game/em_owner_services_original.c',
             'src/game/em_effect_original.c','src/game/em_point_light.c',
             'src/game/em_coll_probe_original.c','src/game/em_actor_collision.c',
             'src/game/em_actor_pool.c','src/game/em_collision.c']
    exe=out/'contract'
    subprocess.run(shlex.split(os.environ.get('CC','cc'))+flags+sources+['-o',str(exe)],cwd=ROOT,check=True)
    subprocess.run([str(exe)],cwd=ROOT,env=dict(os.environ,UBSAN_OPTIONS='halt_on_error=1'),check=True)
    # Original vector instructions address the same split A0/B0/C0..FF owners.
    # Includes the C02E0 E0/F0 subtraction, in-place normalization and dot.
    from test_player_slide_reference import EE, read_elf, bits, RETURN
    from reference_mode import pick, MODE
    e=EE(read_elf());rng=random.Random(0x700038E0);cases=[];payload=bytearray()
    funcs=(0x1028D0,0x1028B8,0x102760,0x102738,0x102948)
    vectors=tuple(0x700038A0+16*i for i in range(6))
    for i in range(pick(2000,320)):
        fn=funcs[i%len(funcs)];a,b,c=(rng.choice(vectors) for _ in range(3))
        words=[bits(rng.uniform(-100,100)) for _ in range(24)]
        if i%4==0:a,b,c=0x700038E0,0x700038B0,0x700038A0
        if fn==0x102760 and i%2:b=a
        data=struct.pack('<24I',*words);e.write(0x700038A0,data)
        e.r[4:7]=[a,b,c];e.r[31]=RETURN;e.run(fn)
        expected=e.read(0x700038A0,96);f0=e.f[0] if fn==0x102738 else 0
        cases.append((fn,expected,f0));payload+=struct.pack('<29I',fn,a,b,c,0,*words)
    result=subprocess.run([str(exe),'--sdk'],input=payload,stdout=subprocess.PIPE,cwd=ROOT,
                          env=dict(os.environ,UBSAN_OPTIONS='halt_on_error=1'),check=True).stdout
    assert len(result)==len(cases)*104
    for i,(fn,expected,f0) in enumerate(cases):
        rc,actual_f0=struct.unpack_from('<2I',result,i*104)
        assert rc==0 and result[i*104+8:(i+1)*104]==expected,(i,hex(fn),'scratch')
        assert actual_f0==f0,(i,hex(fn),'f0')
    print(f'PASS scratch38 SDK ({MODE}): {len(cases)} original instruction calls, full 96 bytes and dot return')
if __name__=='__main__':main()
