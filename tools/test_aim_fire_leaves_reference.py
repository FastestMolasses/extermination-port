#!/usr/bin/env python3
"""Execute both original leaves; compare every result, assert no memory writes.

All type bytes and finite angle boundaries are checked. The original repeated
subtraction has no finite-time exit for huge positive values; the native keeps
that behavior, and the harness does not execute those nonterminating inputs.
"""
import ctypes as C
import os
import random
import subprocess
from pathlib import Path
from test_player_fall_reference import FallEE
from test_player_slide_reference import read_elf, RETURN, bits

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/aim-fire/leaves'
class Oracle(FallEE):
    def save(self,*args,**kwargs):
        if getattr(self,'tracking',False): raise AssertionError('leaf wrote memory')
        super().save(*args,**kwargs)

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    source=Path(os.getenv('EM_AIM_FIRE_LEAVES_SOURCE',ROOT/'src/game/em_aim_fire_leaves.c'))
    lib=OUT/('leaves-'+str(os.getpid())+'.dylib')
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC','-Isrc',str(source),'-o',str(lib)],cwd=ROOT,check=True)
    n=C.CDLL(str(lib))
    for name in ('em_aim_fire_001839A0','em_aim_fire_001B1510'):
        f=getattr(n,name); f.argtypes=[C.c_uint32]; f.restype=C.c_uint32
    e=Oracle(read_elf()); total=0
    for kind in range(256):
        e.save(0x680003,kind,1);e.r[4]=0x680000;e.r[31]=RETURN;e.tracking=True
        e.run(0x1839A0); e.tracking=False
        assert n.em_aim_fire_001839A0(kind)==e.r[2]&0xFFFFFFFF,('kind',kind)
        total+=1
    values={0,0x80000000,1,0x80000001,0x7FFFFF,0x807FFFFF,0xFF800000,0xFF7FFFFF}
    for i in range(-16,17):
        b=bits(i*6.2831855)
        values.update((b,(b+1)&0xFFFFFFFF,(b-1)&0xFFFFFFFF))
    values.discard(0xFFFFFFFF)
    values.discard(0x7FFFFFFF)
    rng=random.Random(8391510)
    values.update(bits(rng.uniform(-200,200)) for _ in range(1000 if os.getenv('EM_TEST_FULL')=='1' else 100))
    for value in sorted(values):
        e.f[12]=value;e.r[31]=RETURN;e.tracking=True;e.run(0x1B1510);e.tracking=False
        assert n.em_aim_fire_001B1510(value)==e.f[0],('angle',hex(value))
        total+=1
    lib.unlink()
    print(f'PASS aim/fire leaves: {total} original-instruction cases; no stores or callees')
if __name__=='__main__':main()
