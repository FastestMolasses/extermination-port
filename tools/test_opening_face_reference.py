#!/usr/bin/env python3
"""Compare readable face C with instructions in the owner's original ELF.

This bounded EE interpreter executes only 001D0720, supplies an identical
sequence of RNG return values to both implementations, and compares every
state byte and RNG call count. Floating instructions use host IEEE float32;
this is an instruction/branch oracle, not an EE rounding emulator. No
original instructions, face geometry, or captured memory are embedded here.
"""
from __future__ import annotations
import argparse
import ctypes as C
from pathlib import Path
import random
import struct
import subprocess
import tempfile
import sys

ROOT=Path(__file__).resolve().parents[1]
ENTRY,END=0x1D0720,0x1D0C68
ACTOR,FACE,RETURN=0x600000,0x610000,0xBADF00D

def signed(v,bits=32):
    v &= (1<<bits)-1
    return v-(1<<bits) if v>>(bits-1) else v

def float_bits(f): return struct.unpack('<I',struct.pack('<f',f))[0]
def as_float(v): return struct.unpack('<f',struct.pack('<I',v))[0]

class Face(C.Structure):
    _fields_=[('weight',C.c_float*8),('blink_state',C.c_int32),
              ('blink_wait',C.c_int32),('expression_state',C.c_int32),
              ('expression_wait',C.c_int32),('talking',C.c_uint8),
              ('speed',C.c_uint8),('reserved',C.c_uint8*2),
              ('mouth_wait',C.c_int32),('current_shape',C.c_int32),
              ('previous_shape',C.c_int32),('target',C.c_float*6)]

def oracle(elf,initial,draws):
    mem={}; regs=[0]*32; fpr=[0]*32; condition=False; hi=0; calls=0
    def save(a,v,n=4):
        for i in range(n): mem[a+i]=(v>>(8*i))&255
    def load(a,n=4): return sum(mem.get(a+i,0)<<(8*i) for i in range(n))
    def fetch(pc):
        assert ENTRY<=pc<END,hex(pc)
        return struct.unpack_from('<I',elf,pc-0x100000+0x300)[0]
    for i,v in enumerate(initial[:32]):save(FACE+0x40+i,v,1)
    for i,v in enumerate(initial[32:]):save(FACE+0x70+i,v,1)
    save(ACTOR+0x90,FACE)
    # Read the constants from the original file, not from native C.
    for a in range(0x2513B0,0x2513D0,4):
        save(a,struct.unpack_from('<I',elf,a-0x100000+0x300)[0])
    regs[4],regs[29],regs[31]=ACTOR,0x700000,RETURN
    def plain(w):
        nonlocal hi,condition
        op,rs,rt,rd=w>>26,w>>21&31,w>>16&31,w>>11&31
        imm=signed(w&65535,16); a=(regs[rs]+imm)&0xFFFFFFFF
        if op==0:
            fn=w&63
            if fn==0:regs[rd]=(regs[rt]<<(w>>6&31))&0xFFFFFFFF
            elif fn==3:regs[rd]=(signed(regs[rt])>>(w>>6&31))&0xFFFFFFFF
            elif fn==16:regs[rd]=hi
            elif fn==26:
                x,y=signed(regs[rs]),signed(regs[rt]); assert y
                hi=(x-int(x/y)*y)&0xFFFFFFFF
            elif fn==33:regs[rd]=(regs[rs]+regs[rt])&0xFFFFFFFF
            elif fn==35:regs[rd]=(regs[rs]-regs[rt])&0xFFFFFFFF
            else:raise AssertionError(('SPECIAL',fn))
        elif op==9:regs[rt]=a
        elif op==10:regs[rt]=int(signed(regs[rs])<imm)
        elif op==12:regs[rt]=regs[rs]&(w&65535)
        elif op==13:regs[rt]=regs[rs]|(w&65535)
        elif op==15:regs[rt]=(w&65535)<<16
        elif op==28 and w&63==40:
            regs[rd]=sum((((regs[rs]>>(8*i)&255)+(regs[rt]>>(8*i)&255))&255)<<(8*i) for i in range(16))
        elif op in (30,35,36):regs[rt]=load(a,{30:16,35:4,36:1}[op])
        elif op in (31,43):save(a,regs[rt],{31:16,43:4}[op])
        elif op==49:fpr[rt]=load(a)
        elif op==57:save(a,fpr[rt])
        elif op==17:
            ft,fs,fd,fn=rt,rd,w>>6&31,w&63
            if rs==4:fpr[fs]=regs[rt]&0xFFFFFFFF
            elif rs==20 and fn==32:fpr[fd]=float_bits(float(signed(fpr[fs])))
            elif rs==16:
                x,y=as_float(fpr[fs]),as_float(fpr[ft])
                if fn==0:fpr[fd]=float_bits(x+y)
                elif fn==1:fpr[fd]=float_bits(x-y)
                elif fn==2:fpr[fd]=float_bits(x*y)
                elif fn==3:fpr[fd]=float_bits(x/y)
                elif fn==7:fpr[fd]=fpr[fs]^0x80000000
                elif fn==52:condition=x<y
                elif fn==54:condition=x<=y
                else:raise AssertionError(('FPU',fn))
            else:raise AssertionError(('COP1',rs,fn))
        else:raise AssertionError(('opcode',op))
        regs[0]=0
    pc=ENTRY
    for _ in range(2000):
        if pc==RETURN:
            result=bytes(load(FACE+0x40+i,1) for i in range(32))
            result+=bytes(load(FACE+0x70+i,1) for i in range(56))
            return result,calls
        w=fetch(pc);op=w>>26;rs=w>>21&31;rt=w>>16&31
        offset=signed(w&65535,16)*4;branch=None
        if op==3:
            target=(w&0x3FFFFFF)<<2
            assert target==0x122BB8,hex(target)
            plain(fetch(pc+4));regs[2]=draws[calls];calls+=1;pc+=8;continue
        if op in (4,5):
            take=(regs[rs]==regs[rt])==(op==4)
            branch=pc+4+offset if take else pc+8
        elif op==7:branch=pc+4+offset if signed(regs[rs])>0 else pc+8
        elif op==1 and rt==1:branch=pc+4+offset if signed(regs[rs])>=0 else pc+8
        elif op==17 and rs==8:branch=pc+4+offset if condition==bool(rt&1) else pc+8
        elif op==0 and w&63==8:branch=regs[rs]
        if branch is not None:plain(fetch(pc+4));pc=branch
        else:plain(w);pc+=4
    raise AssertionError('face instruction oracle did not return')

def morph_oracle(elf,record,weights):
    """Execute original VU blend/load words; upper reads precede lower writes."""
    vectors=[[0.0]*4 for _ in range(32)];vectors[0][3]=1.0
    vectors[3][:3]=struct.unpack_from('<3f',record,48)
    vectors[16]=list(weights[:4]);vectors[27]=list(weights[4:])
    acc=[0.0]*4
    def fp(x):return as_float(float_bits(x))
    for pc in range(0x23C558,0x23C5B8,8):
        lower,upper=struct.unpack_from('<II',elf,pc-0x100000+0x300)
        op,fs,ft,fd=upper&63,upper>>11&31,upper>>16&31,upper>>6&31
        if upper!=0x2FF:
            product=[fp(vectors[fs][c]*vectors[ft][op&3]) for c in range(3)]
            if op>=0x3C and fd==6:acc[:3]=product
            elif op>=0x3C and fd==2:acc[:3]=[fp(acc[c]+product[c]) for c in range(3)]
            elif op&~3==8:vectors[fd][:3]=[fp(acc[c]+product[c]) for c in range(3)]
            else:raise AssertionError(('VU upper',hex(pc),hex(upper)))
        if lower!=0x8000033C:
            assert lower>>25==0,('VU lower',hex(lower))
            target=lower>>16&31;offset=signed(lower&2047,11)*16
            # Loads from vi10 fetch matrix slots into vf28..31; these are
            # outside this position blend. vi14 is the vertex record base.
            if lower>>11&31==14:
                for c in range(4):
                    if lower&(1<<(24-c)):
                        vectors[target][c]=struct.unpack_from('<f',record,offset+4*c)[0]
    return vectors[3][:3]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--decomp-root',type=Path,default=ROOT.parent/'Extermination')
    p.add_argument('--reference-ee',type=Path)
    p.add_argument('--morph-assets',action='store_true',help='also verify every original face record and all seven delta channels')
    args=p.parse_args();elf=(args.decomp_root/'config/SCUS_971.12').read_bytes()
    assert elf[:6]==b'\x7fELF\x01\x01'
    rng=random.Random(0x1D0720);cases=[]
    for blink in range(5):
        for expression in range(6):
            for talk in (0,1):
                for speed in (0,1,2,3):
                    for timer in (0,1,2):
                        f=Face();f.blink_state=blink;f.expression_state=expression
                        f.talking=talk;f.speed=speed
                        f.blink_wait=f.expression_wait=f.mouth_wait=timer
                        f.current_shape=rng.randrange(5);f.previous_shape=rng.randrange(5)
                        for i in range(8):f.weight[i]=rng.choice([0,0.01,0.05,0.1,0.5,0.94,0.95,1])
                        for i in range(6):f.target[i]=rng.random()
                        cases.append(bytes(f))
    if args.reference_ee:
        ram=args.reference_ee.read_bytes()
        for actor in (0x8102B0,0x7A96E0):
            face=struct.unpack_from('<I',ram,actor+0x90)[0]
            cases.append(ram[face+0x40:face+0x60]+ram[face+0x70:face+0xA8])
    callback=C.CFUNCTYPE(C.c_uint32,C.c_void_p)
    with tempfile.TemporaryDirectory(prefix='em_face_oracle_') as tmp:
        lib=Path(tmp)/'face.so'
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-O2',
                        '-ffp-contract=off','-shared','-fPIC','-I'+str(ROOT/'src'),
                        str(ROOT/'src/game/em_opening_face.c'),'-o',str(lib)],check=True)
        native=C.CDLL(str(lib));native.em_opening_face_tick.argtypes=[C.POINTER(Face),callback,C.c_void_p]
        comparisons=0;draw_count=0
        for number,initial in enumerate(cases):
            # Repeated service exercises retained targets, timer expiry, and
            # transitions after the initial branch-boundary cases.
            f=Face.from_buffer_copy(initial)
            for tick in range(16):
                draws=[rng.randrange(0x80000000) for _ in range(16)]
                expected,count=oracle(elf,bytes(f),draws);called=[]
                def rand(_):called.append(1);return draws[len(called)-1]
                native.em_opening_face_tick(C.byref(f),callback(rand),None)
                assert count==len(called),(number,tick,'RNG count',count,len(called))
                assert bytes(f)==expected,(number,tick,'state mismatch',bytes(f).hex(),expected.hex())
                comparisons+=1;draw_count+=count
        native.em_opening_face_talk.argtypes=[C.POINTER(Face),C.c_uint8]
        native.em_opening_face_talk(C.byref(f),0)
        assert not f.talking and not any(f.target)
        native.em_opening_face_reset.argtypes=[C.POINTER(Face)]
        before=bytes(f);native.em_opening_face_reset(C.byref(f))
        assert bytes(f)[:32]==before[:32]
        assert not f.blink_state and not f.expression_state and not f.mouth_wait
        assert not f.current_shape and not f.previous_shape and not any(f.target)
        morph_cases=0
        if args.morph_assets:
            sys.path.insert(0,str(args.decomp_root/'tools'))
            import export_opening_faces as export
            fp=C.POINTER(C.c_float)
            native.em_opening_face_position.argtypes=[fp,fp,fp,fp]
            for name,source,offset,address,actor,resource in export.FACES:
                data=(args.decomp_root/'extract'/source).read_bytes()
                size=struct.unpack_from('<I',data,offset+12)[0]
                for block in export.records(data[offset:offset+size]):
                    for record in block:
                        base=(C.c_float*3)(*struct.unpack_from('<3f',record,48))
                        delta=(C.c_float*21)(*(x for i in range(7) for x in struct.unpack_from('<3f',record,64+16*i)))
                        for index in range(8):
                            weights=[float(i==index) if index<7 else rng.random() for i in range(8)]
                            w=(C.c_float*8)(*weights);out=(C.c_float*3)()
                            native.em_opening_face_position(out,base,delta,w)
                            expected=morph_oracle(elf,record,list(w))
                            assert bytes(out)==struct.pack('<3f',*expected),(name,index,'morph mismatch')
                            morph_cases+=1
    print(f'opening face reference: PASS {comparisons} full-state comparisons, {draw_count} identical RNG calls')
    if morph_cases:print(f'opening face morph: PASS {morph_cases} original VU blend comparisons')

if __name__=='__main__':main()
