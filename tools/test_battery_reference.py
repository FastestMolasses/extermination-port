#!/usr/bin/env python3
"""Execute original002149F0 to check the isolated panel battery core.

Graphics and sound submission are explicit intercepted boundaries. The
original state machine, jump table, comparisons, timers and stores execute
from the user's ELF. No original executable words are embedded here.
"""
import ctypes as C
import hashlib
import itertools
from pathlib import Path
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
ACTOR,PAGE,RETURN=0x900000,0x901000,0xbadf00d

def signed(n,b=32):
    n&=(1<<b)-1
    return n-(1<<b) if n>>(b-1) else n


def oracle(elf,phase,selection,timer,initial,charge,buttons):
    memory={};r=[0]*32;sounds=[]
    def put(a,n,size=4):
        for i in range(size):memory[a+i]=n>>(8*i)&255
    def get(a,size=4):
        def byte(at):
            if at in memory:return memory[at]
            return elf[at-0x100000+0x300] if 0x100000<=at<0x275b00 else 0
        return sum(byte(a+i)<<(8*i) for i in range(size))
    put(PAGE+5,phase,1);put(PAGE+6,timer if phase==5 else selection,1)
    put(PAGE+0x12,initial,1);put(PAGE+0x13,8,1);put(PAGE+0x30,ACTOR)
    put(PAGE+0x3c,timer,2);put(ACTOR+0x34,2,2);put(0x810cb2,charge,2)
    put(0x810e74,buttons,2);put(0x810cb7,12,1)
    r[4],r[29],r[31]=PAGE,0x2000000,RETURN
    def plain(w):
        op,rs,rt,rd=w>>26,w>>21&31,w>>16&31,w>>11&31
        imm=signed(w&65535,16);a=(r[rs]+imm)&0xffffffff
        if op==0:
            fn=w&63
            if fn==0:r[rd]=r[rt]<<(w>>6&31)&0xffffffff
            elif fn==2:r[rd]=(r[rt]&0xffffffff)>>(w>>6&31)
            elif fn==3:r[rd]=signed(r[rt])>>(w>>6&31)&0xffffffff
            elif fn==56:r[rd]=r[rt]<<(w>>6&31)&0xffffffffffffffff
            elif fn==60:r[rd]=r[rt]<<((w>>6&31)+32)&0xffffffffffffffff
            elif fn==62:r[rd]=(r[rt]&0xffffffffffffffff)>>((w>>6&31)+32)
            elif fn==63:r[rd]=signed(r[rt],64)>>((w>>6&31)+32)&0xffffffffffffffff
            elif fn in (33,45):r[rd]=(r[rs]+r[rt])&0xffffffff
            elif fn==35:r[rd]=(r[rs]-r[rt])&0xffffffff
            elif fn==36:r[rd]=r[rs]&r[rt]
            elif fn==37:r[rd]=r[rs]|r[rt]
            elif fn==42:r[rd]=int(signed(r[rs])<signed(r[rt]))
            elif fn==43:r[rd]=int((r[rs]&0xffffffff)<(r[rt]&0xffffffff))
            else:raise AssertionError(('SPECIAL',hex(w)))
        elif op in (9,25):r[rt]=a
        elif op==10:r[rt]=int(signed(r[rs])<imm)
        elif op==11:r[rt]=int((r[rs]&0xffffffff)<(imm&0xffffffff))
        elif op==12:r[rt]=r[rs]&(w&65535)
        elif op==13:r[rt]=r[rs]|(w&65535)
        elif op==15:r[rt]=(w&65535)<<16
        elif op==28 and w&63==40:
            assert r[rs]==0 or r[rt]==0
            r[rd]=r[rs]|r[rt]
        elif op in (30,32,33,35,36,37,55):
            size={30:16,32:1,33:2,35:4,36:1,37:2,55:8}[op]
            n=get(a,size);r[rt]=signed(n,size*8)&0xffffffff if op in (32,33) else n
        elif op in (31,40,41,43,63):put(a,r[rt],{31:16,40:1,41:2,43:4,63:8}[op])
        else:raise AssertionError(('opcode',op,hex(w)))
        r[0]=0
    pc=0x2149f0
    ignored={0x20a7a0,0x20ae40,0x20b210,0x20b0d0,0x1fcf10,0x207d00,0x20ccb0}
    sound={0x20cd40:0,0x20cd60:1,0x20cda0:4}
    for _ in range(3000):
        if pc==RETURN:
            return {'phase':get(PAGE+5,1),'selection':get(PAGE+6,1),
                    'timer':signed(get(PAGE+0x3c,2),16),'charge':signed(get(0x810cb2,2),16),
                    'charged':get(ACTOR+10,1),'armed':get(ACTOR+11,1),
                    'finished':get(0x8106c5,1)!=0,'sounds':sounds}
        if pc in ignored or pc in sound or pc==0x1fb9f0:
            if pc in sound:sounds.append(sound[pc])
            elif pc==0x1fb9f0:sounds.append(r[4])
            r[2]=0;pc=r[31]&0xffffffff;continue
        assert 0x2149f0<=pc<0x215870,hex(pc)
        w=get(pc);op=w>>26;rs=w>>21&31;rt=w>>16&31
        if op in (1,4,5,6,7,20,21):
            if op==1:
                assert rt in (0,1)
                taken=signed(r[rs])>=0 if rt else signed(r[rs])<0
            elif op in (4,20):taken=r[rs]==r[rt]
            elif op in (5,21):taken=r[rs]!=r[rt]
            elif op==6:taken=signed(r[rs])<=0
            else:taken=signed(r[rs])>0
            target=pc+4+signed(w&65535,16)*4 if taken else pc+8
            if op<20 or taken:plain(get(pc+4))
            pc=target
        elif op in (2,3):
            if op==3:r[31]=pc+8
            plain(get(pc+4));pc=(w&0x3ffffff)<<2
        elif op==0 and w&63 in (8,9):
            target=r[rs]&0xffffffff
            if w&63==9:r[w>>11&31]=pc+8
            plain(get(pc+4));pc=target
        else:plain(w);pc+=4
    raise AssertionError('Original page did not return')


class Panel(C.Structure):
    _fields_=[(n,C.c_uint8) for n in ('status','phase','charged','armed','child')]+[('cost',C.c_uint16)]
class Menu(C.Structure):
    _fields_=[('phase',C.c_int),('no',C.c_uint8),('initial',C.c_int),('timer',C.c_int)]


def main():
    elf=(ROOT.parent/'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest()=='ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
    out=ROOT/'build/battery_reference';out.mkdir(parents=True,exist_ok=True)
    library=out/('battery.dylib' if sys.platform=='darwin' else 'battery.so')
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-fPIC',
        '-dynamiclib' if sys.platform=='darwin' else '-shared','-Isrc',
        'src/game/em_panel.c','-lm','-o',str(library)],cwd=ROOT,check=True)
    lib=C.CDLL(str(library));lib.em_panel_battery_step.argtypes=[C.POINTER(Panel),C.POINTER(Menu),C.c_uint,C.POINTER(C.c_int)]
    count=0
    for phase,no,timer,charge,buttons in itertools.product((4,5,6),(0,1),(1,2,30,240),(2,4,8,10,12),
            (0,0x20,0x40,0x8000,0x2000,0xa000,0x8040,0x2040,0x870)):
        if phase==6 and charge<8:continue
        expected=oracle(elf,phase,no,timer,12,charge,buttons)
        p=Panel(1,6,0,0,1,2);m=Menu({4:1,5:2,6:3}[phase],no,12,timer);c=C.c_int(charge)
        events=lib.em_panel_battery_step(C.byref(p),C.byref(m),buttons,C.byref(c))
        actual_phase={0:1,1:4,2:5,3:6,4:6}[m.phase]
        sounds=[]
        if events&1:sounds.append(4)
        # A fast finish can emit both unit and acceptance in original order.
        if events&8:sounds.append(6)
        if events&2:sounds.append(0)
        if events&4:sounds.append(1)
        assert (actual_phase,c.value,p.charged,p.armed,bool(events&16),sounds)==(
            expected['phase'],expected['charge'],expected['charged'],expected['armed'],expected['finished'],expected['sounds']), (phase,no,timer,charge,buttons,events,expected)
        if actual_phase==4:assert m.no==expected['selection']
        if actual_phase==5:assert m.timer==expected['selection']
        if actual_phase==6:assert m.timer==expected['timer']
        count+=1
    print(f'Original002149F0 battery confirmation/discharge: {count} cases PASS')


if __name__=='__main__':main()
