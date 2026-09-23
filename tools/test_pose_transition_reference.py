#!/usr/bin/env python3
"""Execute original quaternion/TRS/channel-update instructions against native C.

Original ELF bytes remain external. This isolates explicit per-node source and
sampled target channels; it does not claim that a matrix is a valid source pose.
Finite arithmetic uses the separately captured original-game reference model
(division nearest, add/sub one guard bit), not a universal PS2 hardware claim.
"""
import ctypes as C
import random
import struct
import subprocess
import tempfile
from pathlib import Path
from test_interaction_animation_reference import Original as Base, bits, number, signed

ROOT=Path(__file__).resolve().parents[1]
RETURN=0xBADF00D
PTR,NODE,A,B,OUT=0x600400,0x600800,0x601000,0x601100,0x601200

def rounded(value):
    r=number(bits(value))
    return number(bits(r)-(abs(r)>abs(value)))

def add(a,b):
    aa,bb=bits(a),bits(b);difference=((aa>>23)&255)-((bb>>23)&255)
    if difference:
        small=bb if difference>0 else aa;distance=abs(difference)
        small &= 0x80000000 if distance>=25 else (0xffffffff<<(distance-1))&0xffffffff
        if difference>0:b=number(small)
        else:a=number(small)
    return rounded(a+b)
class Original(Base):
    def __init__(self,elf):
        super().__init__(elf,b'');self.acc=0
    def plain(self,w):
        op,rs,rt,rd,fd,fn=w>>26,w>>21&31,w>>16&31,w>>11&31,w>>6&31,w&63
        if op==17 and rs==16 and fn in (0,1,2,3,24,25,26,28,29,30,31,7):
            a,b=number(self.f[rd]),number(self.f[rt])
            if fn==0:self.f[fd]=bits(add(a,b))
            elif fn==1:self.f[fd]=bits(add(a,-b))
            elif fn==2:self.f[fd]=bits(rounded(a*b))
            elif fn==3:self.f[fd]=bits(a/b)
            elif fn==24:self.acc=add(a,b)
            elif fn==25:self.acc=add(a,-b)
            elif fn==26:self.acc=rounded(a*b)
            elif fn==28:self.f[fd]=bits(add(self.acc,rounded(a*b)))
            elif fn==29:self.f[fd]=bits(add(self.acc,-rounded(a*b)))
            elif fn==30:self.acc=add(self.acc,rounded(a*b))
            elif fn==31:self.acc=add(self.acc,-rounded(a*b))
            else:self.f[fd]=bits(-a)
        elif op==0 and fn==4:self.r[rd]=self.r[rt]<<(self.r[rs]&31)&0xffffffff
        elif op==10:self.r[rt]=int(signed(self.r[rs])<signed(w&65535,16))
        else:super().plain(w)
        self.r[0]=0
    def run(self,entry,args=(),floats=()):
        for i,value in enumerate(args):self.r[4+i]=value
        for i,value in enumerate(floats):self.f[12+i]=bits(value)
        self.r[31]=RETURN;pc=entry
        for _ in range(16000):
            if pc==RETURN:return
            assert (0x1ca0a0<=pc<0x1ca420 or 0x1c84d0<=pc<0x1c8d50),hex(pc)
            w=self.get(pc);op,rs,rt=w>>26,w>>21&31,w>>16&31
            imm=signed(w&65535,16);branch=None;delay=True
            if op in (4,5,20,21):
                taken=(self.r[rs]==self.r[rt])==(op in (4,20))
                branch=pc+4+imm*4 if taken else pc+8;delay=taken or op not in (20,21)
            elif op==6:branch=pc+4+imm*4 if signed(self.r[rs])<=0 else pc+8
            elif op==7:branch=pc+4+imm*4 if signed(self.r[rs])>0 else pc+8
            elif op==17 and rs==8:
                taken=self.condition==bool(rt&1);branch=pc+4+imm*4 if taken else pc+8;delay=taken or not(rt&2)
            elif op==0 and w&63==8:branch=self.r[rs]
            if branch is not None:
                if delay:self.plain(self.get(pc+4))
                pc=branch
            else:self.plain(w);pc+=4
        raise AssertionError('original pose function did not return')
    def vector(self,address,values):
        for i,v in enumerate(values):self.put(address+i*4,bits(v))
    def floats(self,address,count):return [self.get(address+i*4) for i in range(count)]

class Pose(C.Structure):
    _fields_=[('translation',C.c_float*3),('scale',C.c_float*3),('rotation',C.c_float*4)]
class Transition(C.Structure):
    _fields_=[('count',C.c_uint),('remaining',C.c_float),('reciprocal',C.c_float),('fraction',C.c_float),('active',C.c_int),
              ('source',Pose*64),('target',Pose*64),('current',Pose*64),
              ('translation_velocity',(C.c_float*3)*64),('scale_velocity',(C.c_float*3)*64)]

from reference_mode import FULL, banner, part

def main():
    elf=(ROOT.parent/'Extermination/config/SCUS_971.12').read_bytes();rng=random.Random(0x1c8d50);checks=0
    with tempfile.TemporaryDirectory(prefix='em_pose_reference_') as folder:
        lib=Path(folder)/'pose.dylib'
        subprocess.run(['cc','-shared','-fPIC','-std=c11','-O2','-ffp-contract=off','-I'+str(ROOT/'src'),
            str(ROOT/'src/game/em_pose_transition.c'),'-lm','-o',str(lib)],check=True)
        native=C.CDLL(str(lib));native.em_pose_quaternion_blend.argtypes=[C.POINTER(C.c_float)]*3+[C.c_float]
        native.em_pose_transition_begin.argtypes=[C.POINTER(Transition),C.POINTER(Pose),C.POINTER(Pose),C.c_uint,C.c_uint]
        native.em_pose_transition_tick.argtypes=[C.POINTER(Transition)]
        native.em_pose_transition_step.argtypes=[C.POINTER(Transition),C.c_float]
        native.em_pose_channels_matrix.argtypes=[C.POINTER(C.c_float),C.POINTER(Pose)]
        cases_run=0
        for case in range(600):
            source=Pose();target=Pose()
            for p in (source,target):
                p.translation[:]=[rng.uniform(-500,500) for _ in range(3)]
                p.scale[:]=[rng.uniform(.1,2) for _ in range(3)]
                p.rotation[:]=[rng.uniform(-1,1) for _ in range(4)]
            # Quick: the first 120 poses of the same stream; the case index
            # cycles 8/16-tick durations and 1/.5/.75 steps (all six pairs).
            if not FULL and case>=120:continue
            cases_run+=1
            o=Original(elf);o.vector(A,source.rotation);o.vector(B,target.rotation)
            for fraction in (-.25,0,.125,.5,.875,1,1.25):
                out=(C.c_float*4)();native.em_pose_quaternion_blend(out,source.rotation,target.rotation,fraction)
                o.run(0x1ca0a0,(OUT,A,B),(fraction,))
                assert [bits(x) for x in out]==o.floats(OUT,4),(case,fraction,list(out),[number(x) for x in o.floats(OUT,4)])
                checks+=1
            # Original unscaled TRS, then scale each native basis column.
            one=Pose();C.memmove(C.byref(one),C.byref(source),C.sizeof(Pose));one.scale[:]=[1,1,1]
            matrix=(C.c_float*16)();native.em_pose_channels_matrix(matrix,C.byref(one))
            o.vector(A,source.rotation);o.vector(B,source.translation);o.run(0x1ca1c0,(OUT,A,B))
            assert [bits(x) for x in matrix]==o.floats(OUT,16),(case,'matrix')
            checks+=1
            duration=8 if case%2 else 16;s=Transition()
            assert native.em_pose_transition_begin(C.byref(s),C.byref(source),C.byref(target),1,duration)
            o.put(PTR,NODE);o.vector(NODE,source.translation);o.vector(NODE+24,source.scale)
            o.vector(NODE+48,source.rotation);o.vector(NODE+64,target.rotation)
            o.put(NODE+80,bits(0));o.put(NODE+84,bits(s.reciprocal))
            for offset in (88,92,96):o.put(NODE+offset,bits(duration))
            for src_values,target_values,offset,native_velocity in ((source.translation,target.translation,12,s.translation_velocity[0]),(source.scale,target.scale,36,s.scale_velocity[0])):
                o.vector(A,target_values);o.vector(B,src_values);o.run(0x1c86a0,(NODE+offset,A,B),(duration,))
                assert [bits(x) for x in native_velocity]==o.floats(NODE+offset,3)
                checks+=1
            step=(1,.5,.75)[case%3]
            tick=0
            while s.remaining>1:
                tick+=1
                assert native.em_pose_transition_step(C.byref(s),step)==1
                o.run(0x1c87c0,(PTR,1),(step,))
                assert [bits(x) for x in s.current[0].translation]==o.floats(NODE,3),(case,tick,'translation')
                assert [bits(x) for x in s.current[0].scale]==o.floats(NODE+24,3),(case,tick,'scale')
                assert bits(s.fraction)==o.get(NODE+80),(case,tick,'fraction')
                o.run(0x1ca0a0,(OUT,NODE+48,NODE+64),(number(o.get(NODE+80)),))
                assert [bits(x) for x in s.current[0].rotation]==o.floats(OUT,4)
                checks+=1
            assert native.em_pose_transition_step(C.byref(s),step)==0
            assert bytes(s.current[0])==bytes(target)
        banner(part(cases_run,600,'random pose pairs (every duration x step pair, 7 blend fractions each)'))
        print('pose transition original-instruction PASS',checks,'quaternion/TRS/velocity/channel cases;8/16 intervals, fractional steps and target reset')
if __name__=='__main__':main()
