#!/usr/bin/env python3
"""Compare the panel owner against its original EE instructions.

Runs original00159210 and00157860 with controlled external script results.
It checks owner bytes, script selection, immediate-tick order, message token
and child lifetime. Camera/graphics/audio/script handlers are boundaries;
this does not claim to validate their implementations or menu rendering.
Reads only the user's local executable; reports comparison metadata.
"""
import ctypes as C
import hashlib
import itertools
from pathlib import Path
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
ELF=ROOT.parent/'Extermination/config/SCUS_971.12'
SHA='ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
ACTOR,CHILD=0x900000,0x901000
RETURN,VIRTUAL=0xbadf00d,0xbad0000


def signed(value,bits=32):
    value&=(1<<bits)-1
    return value-(1<<bits) if value>>(bits-1) else value


def oracle(elf,phase,armed,charged,has_battery,done,child):
    mem={}; regs=[0]*32; fp=[0]*32; events=[]
    def load(address,size=4):
        def byte(a):
            if a in mem: return mem[a]
            offset=a-0x100000+0x300
            return elf[offset] if 0x100000<=a<0x300000 and offset<len(elf) else 0
        return sum(byte(address+i)<<(i*8) for i in range(size))
    def save(address,value,size=4):
        for i in range(size): mem[address+i]=(value>>(i*8))&255
    for a in range(ACTOR,ACTOR+0x400): mem[a]=0
    save(ACTOR,1,1);save(ACTOR+3,0x24,1);save(ACTOR+4,1,1)
    save(ACTOR+5,phase,1);save(ACTOR+10,charged,1);save(ACTOR+11,armed,1)
    save(ACTOR+0x20,CHILD if child else 0);save(ACTOR+0x4c,VIRTUAL)
    save(CHILD+4,0,1);save(0x810c7f,has_battery,1)
    regs[4],regs[29],regs[31]=ACTOR,0x2000000,RETURN
    def plain(word):
        op,rs,rt,rd=word>>26,word>>21&31,word>>16&31,word>>11&31
        imm=signed(word&65535,16); address=(regs[rs]+imm)&0xffffffff
        if op==0:
            fn=word&63
            if fn==0:regs[rd]=(regs[rt]<<(word>>6&31))&0xffffffff
            elif fn==4:regs[rd]=(regs[rt]<<(regs[rs]&31))&0xffffffff
            elif fn in (33,45):regs[rd]=(regs[rs]+regs[rt])&0xffffffff
            elif fn==36:regs[rd]=regs[rs]&regs[rt]
            elif fn==37:regs[rd]=regs[rs]|regs[rt]
            elif fn==43:regs[rd]=int((regs[rs]&0xffffffff)<(regs[rt]&0xffffffff))
            else:raise AssertionError(('special',hex(word),fn))
        elif op==9:regs[rt]=address
        elif op==11:regs[rt]=int((regs[rs]&0xffffffff)<(imm&0xffffffff))
        elif op==12:regs[rt]=regs[rs]&(word&65535)
        elif op==13:regs[rt]=regs[rs]|(word&65535)
        elif op==15:regs[rt]=(word&65535)<<16
        elif op==17 and rs==4:fp[rd]=regs[rt]
        elif op==28 and word&63==40:
            assert regs[rs]==0 or regs[rt]==0,'Only original register-copy PADDUB expected'
            regs[rd]=regs[rs]|regs[rt]
        elif op in (30,32,33,35,36,37):
            size={30:16,32:1,33:2,35:4,36:1,37:2}[op]
            value=load(address,size)
            regs[rt]=signed(value,size*8)&0xffffffff if op in (32,33) else value
        elif op in (31,40,41,43):save(address,regs[rt],{31:16,40:1,41:2,43:4}[op])
        else:raise AssertionError(('opcode',op,hex(word)))
        regs[0]=0
    pc=0x159210
    for _ in range(2000):
        if pc==RETURN:
            return ([load(ACTOR+i,1) for i in (0,5,10,11)]+
                    [int(load(CHILD+4,1)!=3 and child)],events)
        if pc in (0x1b6f00,0x1ba1a0,0x1ba1f0,0x1b17a0,VIRTUAL):
            if pc==0x1b6f00:events.append(('align',))
            elif pc==0x1ba1a0:
                address=regs[5]&0xffffffff
                token=load({0x246f20:0x246fb4,0x2477a0:0x247834}.get(address,0))
                events.append(('start',address,token))
            elif pc==0x1ba1f0:events.append(('tick',));regs[2]=done
            pc=regs[31]&0xffffffff
            continue
        word=load(pc);op=word>>26;rs=word>>21&31;rt=word>>16&31
        if op in (4,5,20,21):
            taken=regs[rs]==regs[rt] if op in (4,20) else regs[rs]!=regs[rt]
            target=pc+4+signed(word&65535,16)*4 if taken else pc+8
            if op<20 or taken:plain(load(pc+4))
            pc=target
        elif op in (2,3):
            if op==3:regs[31]=pc+8
            target=(word&0x3ffffff)<<2
            plain(load(pc+4));pc=target
        elif op==0 and word&63 in (8,9):
            target=regs[rs]&0xffffffff
            if word&63==9:regs[word>>11&31]=pc+8
            plain(load(pc+4));pc=target
        else:plain(word);pc+=4
    raise AssertionError(('original owner failed to return',hex(pc)))


class Panel(C.Structure):
    _fields_=[(name,C.c_uint8) for name in ('status','phase','charged','armed','child')]+[('cost',C.c_uint16)]
ALIGN=C.CFUNCTYPE(None,C.c_void_p)
START=C.CFUNCTYPE(None,C.c_void_p,C.c_uint32,C.c_uint32)
TICK=C.CFUNCTYPE(C.c_int,C.c_void_p)
STOP=C.CFUNCTYPE(None,C.c_void_p)
class Hooks(C.Structure):
    _fields_=[('context',C.c_void_p),('align',ALIGN),('start',START),('tick',TICK),('stop',STOP)]


def main():
    elf=ELF.read_bytes();assert hashlib.sha256(elf).hexdigest()==SHA
    output=ROOT/'build/panel_reference';output.mkdir(parents=True,exist_ok=True)
    library=output/('panel.dylib' if sys.platform=='darwin' else 'panel.so')
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-fPIC',
        '-dynamiclib' if sys.platform=='darwin' else '-shared','-Isrc',
        'src/game/em_panel.c','-lm','-o',str(library)],cwd=ROOT,check=True)
    native=C.CDLL(str(library))
    native.em_panel_tick.argtypes=[C.POINTER(Panel),C.c_int,C.POINTER(Hooks)]
    count=0
    for phase,armed,charged,battery,done,child in itertools.product(
            range(7),(0,1,4,5),(0,1),(0,1),(0,1),(0,1)):
        panel=Panel(1,phase,charged,armed,child,2);events=[]
        def tick(_):events.append(('tick',));return done
        hooks=Hooks(None,ALIGN(lambda _:events.append(('align',))),
            START(lambda _,address,token:events.append(('start',address,token))),
            TICK(tick),STOP(lambda _:None))
        assert native.em_panel_tick(C.byref(panel),battery,C.byref(hooks))==0
        expected=oracle(elf,phase,armed,charged,battery,done,child)
        actual=([panel.status,panel.phase,panel.charged,panel.armed,panel.child],events)
        assert actual==expected,dict(case=(phase,armed,charged,battery,done,child),actual=actual,expected=expected)
        count+=1
    print(f'Original00159210 +00157860 panel owner: {count} state/call-order cases PASS')


if __name__=='__main__':main()
