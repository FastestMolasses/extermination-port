#!/usr/bin/env python3
"""Original pickup facing and camera-settle instruction oracle."""
import ctypes as C
import hashlib
import itertools
import json
from pathlib import Path
import random
import struct
import subprocess
from test_pickup_owner_reference import OwnerOracle, ROOT, DECOMP, ELF_SHA, ACTOR
from test_interaction_pickup_reference import Math
from test_interaction_frame_reference import Script
from test_point_light_reference import bits

RECORD = 0x940000


def turn(elf, native, math, position, yaw, owner, step):
    oracle = OwnerOracle(elf)
    oracle.write(0x810350, struct.pack('<3f', *position))
    oracle.save(0x810374, bits(yaw))
    oracle.write(ACTOR+0xb0, struct.pack('<3f', *owner))
    oracle.save(RECORD+8, 1); oracle.save(RECORD+0x24, bits(step))
    oracle.run(0x1b7f90, (ACTOR, ACTOR+0x1f0, RECORD))
    actual_yaw = C.c_float(yaw)
    result = native.em_pickup_turn(C.byref(math), (C.c_float*3)(*position),
        C.byref(actual_yaw), (C.c_float*3)(*owner), step)
    assert (result,bits(actual_yaw.value)) == (oracle.r[2],oracle.load(0x810374)), dict(
        position=position, yaw=yaw, owner=owner, step=step,
        actual=(result,hex(bits(actual_yaw.value))), expected=(oracle.r[2],hex(oracle.load(0x810374))))


def camera(elf, native, owner, target, phase):
    oracle = OwnerOracle(elf)
    oracle.write(ACTOR+0xb0, struct.pack('<4f', *owner, 1.))
    oracle.write(0x8105e0, struct.pack('<4f', *target, 1.))
    oracle.save(ACTOR+0x1f4, phase)
    oracle.save(RECORD+8, 8); oracle.save(RECORD+0x10, bits(35.))
    calls=[]
    def publish(r):
        assert (r.r[4],r.r[5]) == (0x8105d0,0x8105e0)
        calls.append(r.read(0x8105e0,12))
    oracle.calls[0x1dd980] = publish
    oracle.run(0x1b8fc0, (ACTOR,ACTOR+0x1f0,RECORD))
    script = Script(1,phase,0,0,0); actual = (C.c_float*3)(*target)
    result = native.em_pickup_camera_settle(C.byref(script),(C.c_float*3)(*owner),actual)
    assert len(calls) == 1 and bytes(actual) == calls[0], dict(owner=owner,target=target,
        phase=phase,actual=list(actual),expected=[struct.unpack('<3f',call) for call in calls])
    assert (result,script.phase) == (oracle.r[2],oracle.load(ACTOR+0x1f4)), dict(
        owner=owner,target=target,phase=phase,result=result,expected=oracle.r[2])


def main():
    elf=(DECOMP/'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest()==ELF_SHA
    out=ROOT/'build/pickup_owner_reference';out.mkdir(parents=True,exist_ok=True)
    library=out/'motion.dylib'
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC',
        '-ffp-contract=off','-Isrc','src/game/em_pickup_motion.c','src/game/em_interaction_scan.c',
        '-o',str(library)],cwd=ROOT,check=True)
    native=C.CDLL(str(library))
    native.em_pickup_turn.argtypes=[C.POINTER(Math),C.POINTER(C.c_float),C.POINTER(C.c_float),
        C.POINTER(C.c_float),C.c_float]
    native.em_pickup_camera_settle.argtypes=[C.POINTER(Script),C.POINTER(C.c_float),C.POINTER(C.c_float)]
    math=Math.from_buffer_copy(elf[0x26c5d8-0x100000+0x300:0x26c5d8-0x100000+0x300+76])
    turning=settling=0
    for dx,dz,yaw,step in itertools.product((-10.,-.001,0.,.001,10.),
        (-10.,-.001,0.,.001,10.),(-7.,-3.1415927,-.18,0.,.18,3.1415927,7.),(0.,.18,3.)):
        if dx==dz==0:continue # SDK zero-vector error hook is a separate boundary.
        turn(elf,native,math,(0.,229.9,0.),yaw,(dx,239.9,dz),step);turning+=1
    for dx,dy,dz,phase in itertools.product((-30.,-1.00001,-1.,0.,1.,1.00001,30.),
        (-30.,-1.00001,-1.,0.,1.,1.00001,30.),(-1.,0.,1.),(0,1,2,0x101)):
        camera(elf,native,(211.6,229.9,227.2),(211.6+dx,229.9+dy,227.2+dz),phase);settling+=1
    rng=random.Random(0x1b7f90)
    for _ in range(600):
        position=[rng.uniform(-400,400) for _ in range(3)]
        owner=[rng.uniform(-400,400) for _ in range(3)]
        turn(elf,native,math,position,rng.uniform(-7,7),owner,.18);turning+=1
        camera(elf,native,owner,position,1);settling+=1
    report=dict(elf_sha256=ELF_SHA,turn_cases=turning,camera_cases=settling,
        boundaries=['SDK zero-vector error hooks','DD980 publication','world camera frame order'])
    (out/'motion.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'Original pickup motion: {turning} turn, {settling} camera-settle cases PASS')


if __name__=='__main__':main()
