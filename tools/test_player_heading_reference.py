#!/usr/bin/env python3
"""Execute the original stick-heading slice and compare the native helper.

Reads the user's original ELF locally; prints only comparison metadata.
The EE integer/FPU instructions at 00174B7C..00174C68 run unmodified.
The three SDK helper calls use host cosf/atan2f and the recovered wrap rule;
this proves the coordinate convention and operation order, not bit-identical
replacement of the original SDK's transcendental functions.
"""
import ctypes as C
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
ELF=ROOT.parent/'Extermination/config/SCUS_971.12'
ELF_SHA256='ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
START,END=0x174B7C,0x174C68
ACTOR=0x800000
LIB=C.CDLL(None)
LIB.cosf.argtypes=[C.c_float]; LIB.cosf.restype=C.c_float
LIB.atan2f.argtypes=[C.c_float,C.c_float]; LIB.atan2f.restype=C.c_float


def bits(value): return struct.unpack('<I',struct.pack('<f',value))[0]
def number(value): return struct.unpack('<f',struct.pack('<I',value&0xffffffff))[0]
def f32(value): return number(bits(value))
PI=f32(math.pi)
TWO_PI=f32(2*math.pi)


def rtz(value):
    rounded=f32(value)
    if abs(rounded)>abs(value): return number(bits(rounded)-1)
    return rounded


def wrap(value):
    while value>PI: value=rtz(value-TWO_PI)
    while value<=-PI: value=rtz(value+TWO_PI)
    return value


def oracle(elf,x,y,camera):
    registers=[0]*32; registers[17]=ACTOR
    fp=[0.0]*32
    memory={0x810E64:x,0x810E65:y,0x8106A0:bits(camera)}
    def instruction(pc): return struct.unpack_from('<I',elf,pc-0x100000+0x300)[0]
    def plain(word):
        op,rs,rt,rd=word>>26,word>>21&31,word>>16&31,word>>11&31
        imm=word&65535
        signed=imm if imm<32768 else imm-65536
        address=(registers[rs]+signed)&0xffffffff
        if op==0:
            fn=word&63
            if fn==2: registers[rd]=registers[rt]>>(word>>6&31)
            elif fn==0 and word==0: pass
            else: raise AssertionError(('special',fn))
        elif op==15: registers[rt]=imm<<16
        elif op==13: registers[rt]=registers[rs]|imm
        elif op==36: registers[rt]=memory[address]&255
        elif op==49: fp[rt]=number(memory[address])
        elif op==57: memory[address]=bits(fp[rt])
        elif op==17:
            fs,fd,fn=rd,word>>6&31,word&63
            if rs==4: fp[fs]=number(registers[rt])
            elif rs==20 and fn==32:
                raw=bits(fp[fs]); fp[fd]=float(raw if raw<0x80000000 else raw-0x100000000)
            elif rs==16:
                if fn==0: fp[fd]=rtz(fp[fs]+fp[rt])
                elif fn==2: fp[fd]=rtz(fp[fs]*fp[rt])
                elif fn==3: fp[fd]=rtz(fp[fs]/fp[rt])
                elif fn==6: fp[fd]=fp[fs]
                elif fn==7: fp[fd]=-fp[fs]
                else: raise AssertionError(('float',fn))
            else: raise AssertionError(('cop1',rs,fn))
        else: raise AssertionError(('opcode',op))
    pc=START
    for _ in range(200):
        if pc==END: return fp[0]
        word=instruction(pc); op=word>>26
        if op==3:
            target=(word&0x3ffffff)<<2
            plain(instruction(pc+4))
            if target==0x11DE90: fp[0]=LIB.cosf(fp[12])
            elif target==0x11E620: fp[0]=LIB.atan2f(fp[12],fp[13])
            elif target==0x1B1470: fp[0]=wrap(fp[12])
            else: raise AssertionError(('callee',hex(target)))
            pc+=8
        elif op in (1,4):
            rs,rt=word>>21&31,word>>16&31
            imm=word&65535; imm=imm if imm<32768 else imm-65536
            taken=registers[rs]>=0x80000000 if op==1 else registers[rs]==registers[rt]
            plain(instruction(pc+4))
            pc=pc+4+imm*4 if taken else pc+8
        else:
            plain(word); pc+=4
    raise AssertionError('Original heading slice did not terminate')


def main():
    elf=ELF.read_bytes()
    assert hashlib.sha256(elf).hexdigest()==ELF_SHA256,'Wrong original executable'
    output=ROOT/'build/player_heading_reference'
    output.mkdir(parents=True,exist_ok=True)
    library=output/('heading.dylib' if sys.platform=='darwin' else 'heading.so')
    subprocess.run(['cc','-O2','-Wall','-Wextra','-Werror','-fPIC',
                    '-dynamiclib' if sys.platform=='darwin' else '-shared',
                    '-Isrc','src/game/em_player_heading.c','-lm','-o',str(library)],cwd=ROOT,check=True)
    native=C.CDLL(str(library))
    native.em_player_stick_heading.argtypes=[C.c_uint8,C.c_uint8,C.c_float,C.c_float]
    native.em_player_stick_heading.restype=C.c_float
    axes=[0,1,32,64,80,96,112,127,128,129,144,160,176,192,224,254,255]
    forwards=[(f32(math.sin(yaw)),f32(math.cos(yaw)))
              for yaw in (-3,-2.4,-1.57,-.7,0,.4,1.57,2.7,3.14)]
    # Captured original first-control basis, frame4083. The scalar D8106A0
    # agrees with atan2(-forward.z,forward.x) to float precision.
    forwards.append((-.49190166592598,.7406790256500244))
    count=0; maximum=0.0
    for fx,fz in forwards:
        camera=LIB.atan2f(-fz,fx)
        for x in axes:
            for y in axes:
                if math.hypot(x-128,y-128)<=48: continue  # Original gait0 returns first.
                expected=oracle(elf,x,y,camera)
                actual=native.em_player_stick_heading(x,y,fx,fz)
                error=abs(math.remainder(actual-expected,2*math.pi))
                maximum=max(maximum,error); count+=1
                assert error<0.000003,(x,y,fx,fz,expected,actual,error)
    fx,fz=forwards[-1]
    heading=native.em_player_stick_heading(128,0,fx,fz)
    azimuth=LIB.atan2f(fx,fz)
    assert abs(heading-azimuth)<0.000001
    # The previous native formula added a quarter turn for this input.
    previous=LIB.atan2f(LIB.cosf(0),-LIB.cosf(PI*.5))+azimuth
    assert abs(math.remainder(previous-heading,2*math.pi)-math.pi/2)<0.000001
    result={'cases':count,'max_heading_error_radians':maximum,
            'opening_up_heading':heading,'opening_camera_azimuth':azimuth,
            'previous_up_error_radians':math.remainder(previous-heading,2*math.pi)}
    (output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print('player heading original-slice PASS',json.dumps(result))


if __name__=='__main__': main()
