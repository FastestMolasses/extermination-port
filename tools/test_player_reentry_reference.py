#!/usr/bin/env python3
"""Original C440/C540 instruction oracle for stop-interruption metadata.

Call hooks retain call order and argument values. The translation and clip
lookup/arbiter callees are boundaries, not claimed implementations of them.
Original bytes are read from the user's pinned ELF and never embedded here.
"""
import ctypes as C
import struct
import json
import math
from pathlib import Path

ACTOR=0x600000
RETURN=0xBADF00D

def bits(x): return struct.unpack('<I',struct.pack('<f',x))[0]
def number(x): return struct.unpack('<f',struct.pack('<I',x&0xffffffff))[0]
def signed(x): return (x&0x7fffffff)-(x&0x80000000)

class Reentry(C.Structure):
    _fields_=[('phase',C.c_uint),('blend_left',C.c_uint),('frame',C.c_uint)]

class Original:
    def __init__(self,elf,gait,family,frames):
        self.elf=elf;self.mem={};self.r=[0]*32;self.f=[0]*32;self.calls=[]
        self.r[4]=ACTOR;self.r[5]=1;self.r[29]=0x70001000;self.r[31]=RETURN
        self.frames=frames;self.clip=100+family*4+gait-1
        self.put(ACTOR+0x23f,gait,1);self.put(ACTOR+0x235,family,1)
        self.put(ACTOR+0x40,0x500000);self.put(ACTOR+0x1f0,4,1)
        self.put(ACTOR+0x1f1,3,1)
    def put(self,a,v,n=4):
        for i in range(n):self.mem[a+i]=v>>(8*i)&255
    def get(self,a,n=4):
        if a not in self.mem and 0x100000<=a<0x275b00:
            return int.from_bytes(self.elf[a-0x100000+0x300:a-0x100000+0x300+n],'little')
        return sum(self.mem.get(a+i,0)<<(8*i) for i in range(n))
    def plain(self,w):
        r,f=self.r,self.f;op,rs,rt,rd=w>>26,w>>21&31,w>>16&31,w>>11&31
        im=w&65535;im=im if im<32768 else im-65536;a=(r[rs]+im)&0xffffffff
        if op==0:
            fn=w&63
            if fn==0:r[rd]=r[rt]<<(w>>6&31)&0xffffffff
            elif fn==3:r[rd]=signed(r[rt])>>(w>>6&31)&0xffffffff
            elif fn==60:r[rd]=r[rt]<<((w>>6&31)+32)&0xffffffffffffffff
            elif fn==63:
                v=r[rt]&0xffffffffffffffff
                if v&0x8000000000000000:v-=0x10000000000000000
                r[rd]=v>>((w>>6&31)+32)&0xffffffff
            elif fn==33:r[rd]=(r[rs]+r[rt])&0xffffffff
            else:raise AssertionError(('SPECIAL',fn))
        elif op==9:r[rt]=a
        elif op==15:r[rt]=(w&65535)<<16
        elif op==28 and w&63==0x28:
            assert r[rs]==0 or r[rt]==0 # this slice uses paddub only as a copy
            r[rd]=r[rs]|r[rt]
        elif op in (35,36,37,30):r[rt]=self.get(a,{35:4,36:1,37:2,30:16}[op])
        elif op in (40,43,31):self.put(a,r[rt],{40:1,43:4,31:16}[op])
        elif op==49:f[rt]=self.get(a)
        elif op==57:self.put(a,f[rt])
        elif op==17:
            fd,fn=w>>6&31,w&63
            if rs==4:f[rd]=r[rt]
            elif rs==20 and fn==32:f[fd]=bits(float(signed(f[rd])))
            elif rs==16 and fn==1:f[fd]=bits(number(f[rd])-number(f[rt]))
            else:raise AssertionError(('FPU',rs,fn))
        else:raise AssertionError(('opcode',op,hex(w)))
        r[0]=0
    def run(self,pc):
        for _ in range(180):
            if pc==RETURN:return
            assert 0x17c440<=pc<0x17c5c0,hex(pc)
            w=self.get(pc);op,rs,rt=w>>26,w>>21&31,w>>16&31
            im=w&65535;im=im if im<32768 else im-65536
            branch=None
            if op in (4,5):branch=pc+4+im*4 if (self.r[rs]==self.r[rt])==(op==4) else pc+8
            elif op==0 and w&63==8:branch=self.r[rs]
            elif op==3:
                callee=(w&0x3ffffff)<<2;self.r[31]=pc+8;self.plain(self.get(pc+4))
                args=self.r[4:8];self.calls.append((callee,args[:],[number(self.f[12]),number(self.f[13])]))
                if callee==0x178b90:
                    assert args[:2]==[ACTOR,1]
                elif callee==0x17b490:
                    assert args==[ACTOR,1,self.get(ACTOR+0x235,1),self.get(ACTOR+0x25c,1)]
                    self.r[2]=self.clip
                elif callee==0x1c61d0:
                    assert args[:2]==[0x500000,self.clip];self.r[2]=self.frames
                elif callee==0x1749f0:
                    assert args[:2]==[ACTOR,self.clip]
                else:raise AssertionError(('callee',hex(callee)))
                pc+=8;continue
            if branch is not None:self.plain(self.get(pc+4));pc=branch
            else:self.plain(w);pc+=4
        raise AssertionError('original reentry did not return')

def check_reentry(elf,native,Motor):
    native.em_player_reentry_begin.argtypes=[C.POINTER(Reentry),C.POINTER(Motor),C.c_uint,C.c_uint]
    native.em_player_reentry_tick.argtypes=[C.POINTER(Reentry),C.POINTER(Motor)]
    checks=0
    for gait in (2,3):
        for family in range(4):
            for frames in (120 if gait==2 else 45, 80, 180):
                original=Original(elf,gait,family,frames);original.run(0x17c440)
                m=Motor(0,0,1,0,4,3,3,gait,0);r=Reentry()
                assert native.em_player_reentry_begin(C.byref(r),C.byref(m),gait,frames)
                assert m.tier==original.get(ACTOR+0x25c,1)
                assert bits(m.speed)==original.get(ACTOR+0x38)
                assert m.mode==original.get(ACTOR+0x1f0,1) and m.substate==3
                request=original.calls[-1]
                assert [x[0] for x in original.calls]==[0x178b90,0x17b490,0x1c61d0,0x1749f0]
                assert request[2]==[float(r.blend_left),float(r.frame)]
                for tick in range(4):
                    native.em_player_reentry_tick(C.byref(r),C.byref(m))
                    assert r.phase==(2 if tick==3 else 1)
                original.r[4]=ACTOR;original.r[31]=RETURN;original.run(0x17c540)
                assert m.mode==original.get(ACTOR+0x1f0,1)
                assert m.substate==original.get(ACTOR+0x1f1,1)==0
                checks+=2
    live=Path(__file__).resolve().parents[2]/'Extermination/build/startup-reference/interrupted_run_poll.json'
    if live.exists():
        rows={r['frame']:r for r in json.loads(live.read_text())['rows']}
        first=next(f for f,r in rows.items() if r['state'][2]==0x63)
        m=Motor(0,.8,1,0,4,0,3,3,0);r=Reentry()
        assert native.em_player_reentry_begin(C.byref(r),C.byref(m),3,45)
        for tick in range(5):
            sample=rows[first+tick]
            assert sample['speed']==m.speed and sample['tier']==m.tier
            assert sample['animation_id']==2 and r.frame==27
            assert sample['state'][2]==(0 if tick==4 else 0x63)
            if tick<4:native.em_player_reentry_tick(C.byref(r),C.byref(m))
        old=rows[first-1]['base'];new=rows[first]['base']
        assert abs(math.hypot(new[0]-old[0],new[2]-old[2])-.6)<.0001
    print('player reentry original-instruction PASS',checks,'request/handoff cases; live interruption matched' if live.exists() else 'request/handoff cases')
