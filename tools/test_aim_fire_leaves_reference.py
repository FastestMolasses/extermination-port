#!/usr/bin/env python3
"""Execute the original leaves; compare every result; 001839A0 / 001B1510
write no memory; 001AA7A0 writes only its entry's +0x36 halfword, and only
when it returns 1.

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
    # 001AA7A0(a0, e): the knife's reach (D_00275B40 = 0x690000, its first
    # word the node 0x6A0000; the entry at 0x6B0000). Random and boundary
    # offsets around the 25.0 / 45.0 limits, both signs, denormal / huge.
    reach=getattr(n,'em_aim_fire_001AA7A0')
    reach.argtypes=[C.POINTER(C.c_uint32),C.POINTER(C.c_uint32),C.POINTER(C.c_int)];reach.restype=C.c_uint32
    cases=[]
    edge=[0.0,-0.0,5.0,-5.0,4.9999995,5.0000005,3.5355339,45.0,-45.0,44.99999,45.00001,1e-39,3e38,-3e38]
    for _ in range(6000 if os.getenv('EM_TEST_FULL')=='1' else 600):
        pos=[rng.uniform(-400,400),rng.uniform(-50,250),rng.uniform(-400,400)]
        d=[rng.choice(edge+[rng.uniform(-8,8)]) for _ in range(3)]
        d[1]=rng.choice(edge+[rng.uniform(-60,10)])
        cases.append((pos,[pos[0]+d[0],pos[1]+d[1],pos[2]+d[2]]))
    ones=0
    for pos,node in cases:
        e.save(0x690000,0x6A0000,4)
        for k in range(3):
            e.save(0x6A00C0+4*k,bits(node[k]),4);e.save(0x6B00B0+4*k,bits(pos[k]),4)
        e.save(0x6B0036,0xBEEF,2)
        e.save(0x275B40,0x690000,4)
        e.r[4]=0;e.r[5]=0x6B0000;e.r[31]=RETURN
        e.run(0x1AA7A0)
        want=e.r[2]&0xFFFFFFFF
        stored=e.load(0x6B0036,2)
        hit=C.c_int(0)
        got=reach((C.c_uint32*3)(*[bits(x) for x in node]),(C.c_uint32*3)(*[bits(x) for x in pos]),C.byref(hit))
        assert got==want and (stored==1)==bool(hit.value) and stored in (1,0xBEEF),('001AA7A0',node,pos,got,want,stored)
        ones+=want
        total+=1
    assert 0<ones<len(cases),('001AA7A0 outcomes',ones,len(cases))
    lib.unlink()
    print(f'PASS aim/fire leaves: {total} original-instruction cases ({ones} 001AA7A0 reaches); '
          f'no stores or callees but 001AA7A0\'s +0x36')
if __name__=='__main__':main()
