#!/usr/bin/env python3
"""Original bug hit through production world composition and actor views.

No complete actor image is mapped to the native owner. The runtime's actual
enumerator exposes header/h36; SDK 00102948 writes the canonical f60 field
through its actual narrow map. Original RNG and wrap execute as explicit
callee boundaries; effect allocation/audio are recorded separately from
their independently checked production effect/packet bindings.
"""
import ctypes as C
import hashlib
import json
import struct
import subprocess
from pathlib import Path

import reference_mode as mode
import test_area00_world_reference as W
import test_area01_flame_services_reference as F
import test_area01_render_reference as R
from test_area01_target_hit_reference import BUGS
from test_player_slide_reference import read_elf

OUT=F.ROOT/'build/level2-crashes/target-hit-reference'
class Call(C.Structure):
    _fields_=[('function',C.c_uint32),('sp',C.c_uint32),('a',C.c_uint64*7),
              ('f',C.c_uint32*8),('na',C.c_uint32),('nf',C.c_uint32),
              ('v0',C.c_uint64),('f0',C.c_uint32)]
WORKER=C.CFUNCTYPE(C.c_int,C.POINTER(Call))


def build():
    OUT.mkdir(parents=True,exist_ok=True)
    sources=['tests/area01_target_hit_live_bridge.c']
    sources += ['src/game/em_'+s+'.c' for s in (
        'aim_fire_world_live','area00_world','actor_pool','actor_collision','aim_fire_sdk_memory',
        'aim_fire_leaves','area02_math','area02_misc','area01_side','area00_fx_exit',
        'area00_fx_spawn','area00_fx_gs','area00_fx_debris','area00_fx_trail',
        'area01_ui_effect','area01_render_gs','coll_probe_original',
        'effect_original','owner_services_original','camera_commit_original',
        'area00_low','sfx_bank','sfx')]
    library=OUT/'live.dylib'
    # Rebuild only when a source, any src header or this script is newer.
    deps=[F.ROOT/x for x in sources]+list((F.ROOT/'src').rglob('*.h'))+[Path(__file__)]
    if not library.exists() or max(d.stat().st_mtime for d in deps)>library.stat().st_mtime:
        subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off',
                        '-shared','-fPIC','-Isrc','-Wl,-dead_strip','-Wl,-exported_symbol,_ah_*',
                        *sources,'-lm','-o',str(library)],cwd=F.ROOT,check=True)
    lib=C.CDLL(str(library))
    lib.ah_run.argtypes=[C.c_void_p,C.c_void_p,C.c_uint32,WORKER,C.c_void_p]
    lib.ah_sdk.argtypes=[C.POINTER(Call)]
    lib.ah_sound.argtypes=[C.c_void_p,C.c_void_p,C.c_uint32,C.c_uint32,C.c_int,
                          C.c_float,C.c_float,C.c_int]
    return lib


def digest(ram,spr):
    return hashlib.sha256(ram).digest(),hashlib.sha256(spr).digest()


def main():
    lib,elf=build(),read_elf();cases=calls=sounds=real_gains=0
    every=[b for b in R.BEATS if not b.startswith('a01_07')]
    # Quick mode samples captures (first, last, fixed seed); every bug and
    # cue case runs in each. EM_TEST_FULL=1 runs every capture.
    beats=mode.select(every,4,0x1FC580,keep=lambda i,_b:i in (0,len(every)-1))
    mode.banner(mode.part(len(beats),len(every),'pre-exit captures'))
    for beat in beats:
        base,scratch=R.image(beat)
        for actor in BUGS:
            ram,spr=bytearray(base),bytearray(scratch)
            spr[0x38B0:0x38C0]=ram[actor+0xB0:actor+0xC0]
            spr[0x38C0:0x38D0]=bytes.fromhex('0000000000000000000080bf00000000')
            expected=F.oracle(elf,ram,spr);boundaries=[]
            def hook(fn):
                def run(e):
                    policy,na,nf=W.CALLEES[fn]
                    boundaries.append((fn,e.r[29],tuple(e.r[4:4+na]),tuple(e.f[12:12+nf]),digest(e.mem,e.spad)))
                    if policy==W.R:F.execute(e,fn)
                    else:e.r[2]=0;e.f[0]=0
                return run
            expected.hooks={fn:hook(fn) for fn in W.CALLEES}
            R.oracle_call(expected,0x1B41F0,(actor,0x700038B0,0x700038C0,0,0,5))
            fixture=F.Fixture(ram,spr);original=F.native_oracle(elf,fixture)
            errors=[];index=0
            @WORKER
            def worker(p):
                nonlocal index
                c=p.contents
                try:
                    record=(c.function,c.sp,tuple(c.a[:c.na]),tuple(c.f[:c.nf]),digest(fixture.ram,fixture.spr))
                    assert record==boundaries[index],(beat,hex(actor),index,record[:4],boundaries[index][:4],record[4]==boundaries[index][4])
                    index+=1
                    if c.function==0x102948:
                        assert lib.ah_sdk(p)==0
                    elif W.CALLEES[c.function][0]==W.R:
                        R.oracle_call(original,c.function,tuple(c.a[:c.na]),tuple(c.f[:c.nf]))
                        c.v0=original.r[2];c.f0=original.f[0]
                    else:c.v0=0;c.f0=0
                    return 0
                except Exception as error:errors.append(error);return -1
            fault=(C.c_uint32*3)()
            rc=lib.ah_run(fixture.ram,fixture.spr,actor,worker,fault)
            if errors:raise errors[0]
            assert rc==0,(beat,hex(actor),[hex(v) for v in fault],index)
            assert index==len(boundaries) and fault[2]==1
            F.compare(fixture,expected,(beat,hex(actor),'canonical hit'))
            cases+=1;calls+=index
        # Original FC580 queue policy, through the actual runtime adapter.
        # FBF50 is an explicit boundary: the existing gain solver receives
        # the canonical target position and radius; its exact request-word
        # conversion is linked natively. Vary gain, pending cue and range.
        actor=BUGS[0]
        for sound in (0x15A,0x15B):
            for timer,old_gain,result,left,right in ((0,0,1,1,.5),(4,0,1,.5,.25),
                                                   (4,4096,1,.5,.25),(4,0,0,1,1)):
                ram,spr=bytearray(base),bytearray(scratch)
                struct.pack_into('<4i',ram,0x281F60,timer,0x15B,old_gain,old_gain)
                expected=F.oracle(elf,ram,spr);seen=[]
                def gain(e):
                    assert tuple(e.r[4:8])==(actor,e.r[29]+0x38,e.r[29]+0x3C,0)
                    assert tuple(e.f[12:14])==(R.F(300),R.F(4096))
                    seen.append(1);e.r[2]=result
                    if result:
                        e.write(e.r[5],struct.pack('<i',int(left*4096)))
                        e.write(e.r[6],struct.pack('<i',int(right*4096)))
                expected.hooks={0x1FBF50:gain}
                # ah_sound is called from B41F0's 0x80-byte frame.
                expected.r[29]=0x7F0EFF80
                expected.r[4]=actor;expected.r[5]=sound;expected.r[31]=R.RETURN
                expected.run(0x1FC580)
                fixture=F.Fixture(ram,spr)
                assert lib.ah_sound(fixture.ram,fixture.spr,actor,sound,result,left,right,0)==1
                assert seen==[1]
                F.compare(fixture,expected,(beat,hex(sound),timer,old_gain,result,'cue'))
                sounds+=1
        # Also execute the original full gain solver and compare its cue
        # request words to this same adapter using the actual shared native
        # gain solver. Its pre-existing scratch/math implementation remains
        # a separate owner; this asserts the observable delayed cue output.
        for actor in BUGS:
            ram,spr=bytearray(base),bytearray(scratch)
            struct.pack_into('<4i',ram,0x281F60,0,-1,0,0)
            assert ram[0x28215B]==0, (beat,'stereo fixture required')
            expected=F.oracle(elf,ram,spr)
            R.oracle_call(expected,0x1FC580,(actor,0x15A))
            fixture=F.Fixture(ram,spr)
            assert lib.ah_sound(fixture.ram,fixture.spr,actor,0x15A,2,0,0,0)==1
            want=bytes(expected.mem[0x281F30:0x281FD0])
            actual=bytes(fixture.ram[0x281F30:0x281FD0])
            assert actual==want,(beat,hex(actor),'native gain cue output',
                                 actual[0x30:0x40].hex(),want[0x30:0x40].hex())
            real_gains+=1
    fixture=F.Fixture(base,scratch)
    for deny in (1,2):
        assert lib.ah_sound(fixture.ram,fixture.spr,BUGS[0],0x15A,1,1,1,deny)<0
    report=dict(status='PASS',mode=mode.MODE,captures=len(beats),cases=cases,ordered_call_boundaries=calls,
                sound_queue_cases=sounds,sound_refusals=2,
                actual_native_gain_cue_cases=real_gains,
                full_ram_and_scratchpad=True,native_gameplay_hit_observed=False)
    (OUT/'live-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))


if __name__=='__main__':main()
