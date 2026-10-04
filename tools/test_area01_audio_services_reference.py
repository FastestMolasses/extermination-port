#!/usr/bin/env python3
"""Exact positional gain/submit adapter versus original instructions.

Original math, pan, SDK and gain calls execute. Only 001FB9F0 voice submit is
an explicit boundary returning handle27 on both sides. Runtime has no test
RAM dependency; this fixture supplies sparse canonical owner buffers.
"""
import ctypes as C
import hashlib
from pathlib import Path
import random
import struct
import subprocess

from reference_mode import MODE, pick
from test_player_misc_workers_reference import MiscEE, F
from test_actor_pool_reference import ELF_SHA256

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/level2/audio-services'

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    sources=['tests/area01_audio_services_bridge.c','src/game/em_area01_audio_services.c',
             'src/game/em_player_misc_workers.c','src/game/em_player_stage_workers.c','src/game/em_random.c',
             'src/game/em_script_host_workers.c','src/game/em_script.c','src/game/em_sdk_math_original.c',
             'src/game/em_owner_services_original.c','src/game/em_effect_original.c',
             'src/game/em_coll_probe_original.c','src/game/em_actor_collision.c',
             'src/game/em_collision.c','src/game/em_actor_pool.c','src/game/em_sfx_bank.c']
    lib=OUT/'bridge.dylib'
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC','-ffp-contract=off',
                    '-Isrc',*sources,'-lm','-o',str(lib)],cwd=ROOT,check=True)
    n=C.CDLL(str(lib));n.au_init.argtypes=[C.c_void_p,C.c_uint32]
    n.au_bytes.argtypes=[C.c_uint32,C.c_uint32];n.au_bytes.restype=C.c_void_p
    n.au_result.restype=C.c_uint64;n.au_submits.restype=C.POINTER(C.c_uint32)
    n.au_call.argtypes=[C.c_uint32]*6
    n.au_loop_state.argtypes=[C.POINTER(C.c_int32),C.POINTER(C.c_int32),C.c_int32,C.c_int32,C.c_int32]
    n.au_requested.restype=C.POINTER(C.c_int32)
    n.au_gain.restype=C.c_int32;n.au_start_gain.restype=C.c_int32
    elf=(ROOT.parent/'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest()==ELF_SHA256
    buf=C.create_string_buffer(elf);assert n.au_init(buf,len(elf))==0
    ee=MiscEE(elf)
    rng=random.Random(0x1FBF50)
    regions=((0x680000,0x320),(0x810360,16),(0x8105D0,16),(0x81027C,4),
             (0x28215B,1),(0x6F0000,8),(0x70003400,64),(0x70003600,32))
    def store(a,data):
        p=n.au_bytes(a,len(data));assert p,(hex(a),len(data));C.memmove(p,data,len(data));ee.write(a,data)
    def words(a,values):store(a,struct.pack('<%dI'%len(values),*values))
    def native(a,size):
        p=n.au_bytes(a,size);assert p;return C.string_at(p,size)
    completed=0
    for case in range(pick(300,40)):
        for fn in (0x1FBF50,0x1FBD50,0x1B1380):
            for a,size in regions:store(a,rng.randbytes(size))
            source=(rng.choice((-300.,-18.,0.,9.,18.,50.,299.,300.)),rng.choice((-3.,0.,20.)),rng.choice((-60.,0.,10.,30.)),1.)
            words(0x6800B0,list(map(F,source)));words(0x810360,[0,0,0,F(1)])
            words(0x8105D0,list(map(F,(13.,4.,17.,1.))))
            yaw=F(rng.choice((-3.14,-1.57,0.,.4,1.57,3.14)));words(0x81027C,[yaw])
            store(0x28215B,bytes([case%3]));words(0x6F0000,[0xDEADBEEF,0xABCDEF12])
            flat=rng.choice((0,1,0x100,0xFFFFFFFF));radius=F(rng.choice((0.,18.,300.,800.)))
            scale=F(rng.choice((1.,1024.,4096.)))
            log=[]
            def submit(e):
                log.append(tuple(e.arg(i)&0xFFFFFFFF for i in range(4)));e.ret_int(27)
            ee.hooks={0x1FB9F0:submit};ee.save(0x26C5D0,0xFFFFFFFF)
            right=0x6F0000 if case%7==0 else 0x6F0004
            if fn==0x1FBF50:args=(0x680000,0x6F0000,right,flat);floats=(radius,scale)
            elif fn==0x1FBD50:args=(0x680000,0x301,flat);floats=(radius,)
            else:args=(0x6800B0,0x8105D0);floats=(yaw,)
            # FallEE takes float values; these chosen finite binary32 words
            # round-trip exactly and all executed arithmetic uses EE model.
            ee.call(fn,args,tuple(struct.unpack('<f',struct.pack('<I',v))[0] for v in floats))
            assert n.au_call(fn,flat,radius,scale,0x6F0000,right)==0,(case,hex(fn))
            assert n.au_result()&0xFFFFFFFF==ee.r[2]&0xFFFFFFFF,(case,hex(fn),'v0')
            for a,size in regions:
                expected=bytes(ee.read(a,size));actual=native(a,size)
                assert actual==expected,(case,hex(fn),hex(a),next((i,x,y) for i,(x,y) in enumerate(zip(actual,expected)) if x!=y))
            calls=n.au_submits();assert calls[0]==len(log)
            if log:assert tuple(calls[i] for i in range(1,5))==log[0]
            completed+=1
    # Missing writable scratch and aliases which the typed boundary cannot
    # preserve must refuse before a gain or submit runs.
    before=[native(a,size) for a,size in regions]
    n.au_deny(1);assert n.au_call(0x1FBF50,0,F(300),F(4096),0x6F0000,0x6F0004)<0;n.au_deny(0)
    assert before==[native(a,size) for a,size in regions]
    for address in (0x6800B0,0x206800B0,0x306800B0,0x810360,0x70003400,0x70003600,0x6F0001):
        assert n.au_call(0x1FBF50,0,F(300),F(4096),address,0x6F0004)<0
        assert before==[native(a,size) for a,size in regions]
    loop_completed=0
    for case in range(pick(1000,160)):
        for a,size in regions:store(a,rng.randbytes(size))
        words(0x6800B0,list(map(F,(rng.choice((0.,20.,500.)),0.,10.,1.))))
        words(0x810360,[0,0,0,F(1)]);words(0x8105D0,list(map(F,(13.,4.,17.,1.))))
        words(0x81027C,[F(.4)]);store(0x28215B,bytes([case%3]))
        handle=(-1,0,7,47)[case%4];sid=0x44E;status=2 if case%3 else 0
        requested=[rng.choice((-1,sid,sid+1)) for _ in range(48)];snapshot=requested[:]
        if handle>=0:snapshot[handle]=(-1,sid,sid+1)[case%3]
        frame=rng.randrange(-10000,10000);ordinal=rng.randrange(-100,100)
        if case%2:frame-=(frame+ordinal)%10
        release=case%7==0;radius=F(300.);scale=F((1.,512.,4096.)[case%3])
        words(0x6F0000,[handle&0xFFFFFFFF,0xABCDEF12])
        for i in range(48):ee.save(0x281B70+4*i,requested[i]&0xFFFFFFFF);ee.save(0x281C30+4*i,snapshot[i]&0xFFFFFFFF)
        ee.save(0x70003B68,frame&0xFFFFFFFF);ee.save(0x70003B8A,ordinal&0xFFFF,2);ee.save(0x26C5D0,0xFFFFFFFF)
        req=(C.c_int32*48)(*requested);snap=(C.c_int32*48)(*snapshot)
        n.au_loop_state(req,snap,frame,ordinal,status)
        events=[]
        def status_call(e):
            assert e.arg(0)==1;e.ret_int(status)
        def start_call(e):
            args=tuple(e.arg(i)&0xFFFFFFFF for i in range(4));assert args[:2]==(sid,0x1000)
            events.append(('start',*args));e.ret_int(-1 if status==2 else 0)
        def request_call(e):events.append(('request',*(e.arg(i)&0xFFFFFFFF for i in range(3))))
        def stop_call(e):events.append(('stop',e.arg(0)&0xFFFFFFFF))
        ee.hooks={0x119890:status_call,0x1FB9F0:start_call,0x11A218:request_call,0x11A070:stop_call}
        fn=0x1FC520 if release else 0x1FC3C0
        ee.call(fn,(0x6F0000,) if release else (0x680000,0x6F0000,sid),() if release else (300.,struct.unpack('<f',struct.pack('<I',scale))[0]))
        assert n.au_call(fn,sid,radius,scale,0,0)==0,(case,events)
        for a,size in regions:
            expected=bytes(ee.read(a,size));actual=native(a,size)
            assert actual==expected,(case,hex(fn),hex(a),next((i,x,y) for i,(x,y) in enumerate(zip(actual,expected)) if x!=y))
        assert [x&0xFFFFFFFF for x in n.au_requested()[:48]]==[ee.load(0x281B70+4*i) for i in range(48)]
        for event in events:
            if event[0]=='stop':assert n.au_track(event[1])==(4 if status==2 else 0)
            elif event[0]=='request':
                assert tuple(n.au_gain(event[1],side)&0xFFFFFFFF for side in (0,1))==event[2:]
            elif event[0]=='start' and status!=2:
                assert n.au_track(0)==2
                assert tuple(n.au_start_gain(0,side)&0xFFFFFFFF for side in (0,1))==event[3:]
        loop_completed+=1
    # On a live update the original stores an unchanged handle. A refused
    # canonical write must fault after the request, without clearing tables.
    for deny in (0,1,2):
        words(0x6800B0,list(map(F,(0.,0.,0.,1.))));words(0x810360,[0,0,0,F(1)])
        words(0x6F0000,[7,0xABCDEF12]);req=(C.c_int32*48)(*([-1]*48));snap=(C.c_int32*48)(*([-1]*48));req[7]=snap[7]=0x44E
        n.au_loop_state(req,snap,10,0,2);n.au_deny(deny)
        rc=n.au_call(0x1FC3C0,0x44E,F(300),F(512),0,0);n.au_deny(0)
        assert rc==(-1 if deny else 0)
        assert native(0x6F0000,4)==struct.pack('<I',7) and n.au_requested()[7]==0x44E
        assert n.au_stores()==(0 if deny==1 else 1)
    print(f'PASS {MODE}: {completed} original audio/gain/pan chains + {loop_completed} loop/release chains; actual canonical SFX table/voice owner, exact scratch/output/submit comparisons; missing/alias/store refusal')

if __name__=='__main__':main()
