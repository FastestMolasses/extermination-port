#!/usr/bin/env python3
"""Compare original001AE040 status branches with the native outer driver.

The page worker, sound workers and rendering calls are ordered boundaries.
This proves scheduling/state transitions, not their UI/audio implementations.
"""
import ctypes as C
import hashlib
import itertools
import json
from pathlib import Path
import subprocess
import sys

from test_player_reentry_reference import Original as Base, signed

ROOT=Path(__file__).resolve().parents[1]
TASK,RETURN=0x900000,0xBADF00D
FIELDS=[('phase',TASK+0xB),('step',TASK+0xC),('task_flag11',TASK+0x11),
        ('control_mode',0x8106C4),('recovery_lock',0x8106EF),('audio_busy',0x282157)]
class Status(C.Structure):
    _fields_=[(name,C.c_uint8) for name,_ in FIELDS]
CALLS={0x20E060:(0,()),0x1FBC50:(1,()),0x1FABB0:(2,()),
       0x1D1C50:(5,()),0x1D2830:(6,(3,1)),0x1E0CC0:(7,(0,)),
       0x1AEDB0:(8,(0,)),0x1D1EA0:(9,(0,)),0x1D1EF0:(10,()),
       0x18C0D0:(11,(0x8101E0,1)),0x1FAE70:(12,(1,)),0x1AEE40:(13,(32,))}

class Original(Base):
    def __init__(self,elf,state,page_result):
        super().__init__(elf,0,0,0)
        self.r[28]=0x27D370
        self.r[31]=RETURN
        self.put(0x70003B6C,TASK)
        for name,address in FIELDS:self.put(address,getattr(state,name),1)
        self.page_result=page_result

    def plain(self,word):
        op,rs,rt,rd=word>>26,word>>21&31,word>>16&31,word>>11&31
        if op==0 and word&63 in (2,37,43):
            fn=word&63
            if fn==2:self.r[rd]=(self.r[rt]&0xFFFFFFFF)>>(word>>6&31)
            elif fn==37:self.r[rd]=self.r[rs]|self.r[rt]
            else:self.r[rd]=int((self.r[rs]&0xFFFFFFFF)<(self.r[rt]&0xFFFFFFFF))
        elif op==11:
            imm=word&65535;imm=imm-65536 if imm>=32768 else imm
            self.r[rt]=int((self.r[rs]&0xFFFFFFFF)<(imm&0xFFFFFFFF))
        elif op==13:self.r[rt]=self.r[rs]|(word&65535)
        elif op==32:
            imm=word&65535;imm=imm-65536 if imm>=32768 else imm
            value=self.get((self.r[rs]+imm)&0xFFFFFFFF,1)
            self.r[rt]=(value-256 if value>=128 else value)&0xFFFFFFFF
        else:super().plain(word)
        self.r[0]=0

    def run(self):
        pc=0x1AE040
        for _ in range(1200):
            if pc==RETURN:
                return Status(*(self.get(a,1) for _,a in FIELDS)),self.calls
            if pc==0x1AE7E0:
                self.r[2]=2;pc=self.r[31]&0xFFFFFFFF;continue
            if pc==0x20CDC0:
                self.calls.append(14);self.r[2]=self.page_result
                pc=self.r[31]&0xFFFFFFFF;continue
            if pc==0x119828:
                args=tuple(self.r[4:7]);assert args in ((0,16383,16383),(1,16383,16383)),args
                self.calls.append(3+args[0]);pc=self.r[31]&0xFFFFFFFF;continue
            if pc in CALLS:
                event,args=CALLS[pc]
                assert tuple(self.r[4:4+len(args)])==args,(hex(pc),self.r[4:8],args)
                self.calls.append(event)
                if event==2:self.put(0x282157,0,1)
                pc=self.r[31]&0xFFFFFFFF;continue
            assert 0x1AE040<=pc<0x1AE5E0,hex(pc)
            word=self.get(pc);op,rs,rt=word>>26,word>>21&31,word>>16&31
            imm=word&65535;imm=imm-65536 if imm>=32768 else imm
            target=None
            if op in (1,4,5,6,7,20,21):
                if op==1:
                    assert rt in (0,1)
                    taken=(signed(self.r[rs])<0)==(rt==0)
                elif op in (4,5,20,21):taken=(self.r[rs]==self.r[rt])==(op in (4,20))
                elif op==6:taken=signed(self.r[rs])<=0
                else:taken=signed(self.r[rs])>0
                target=pc+4+imm*4 if taken else pc+8
                if op<20 or taken:self.plain(self.get(pc+4))
                pc=target;continue
            if op in (2,3):
                if op==3:self.r[31]=pc+8
                target=(word&0x3FFFFFF)<<2
            elif op==0 and word&63==8:target=self.r[rs]&0xFFFFFFFF
            if target is not None:
                self.plain(self.get(pc+4));pc=target
            else:self.plain(word);pc+=4
        raise AssertionError('Original status branch failed to return')

def main():
    elf=(ROOT.parent/'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest()=='ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
    out=ROOT/'build/status_frame_reference';out.mkdir(parents=True,exist_ok=True)
    library=out/('status.dylib' if sys.platform=='darwin' else 'status.so')
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-fPIC',
        '-dynamiclib' if sys.platform=='darwin' else '-shared','-Isrc',
        'src/game/em_status_frame.c','-o',str(library)],cwd=ROOT,check=True)
    native=C.CDLL(str(library));Emit=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_int)
    Page=C.CFUNCTYPE(C.c_int,C.c_void_p)
    native.em_status_frame_enter.argtypes=[C.POINTER(Status),Emit,C.c_void_p]
    native.em_status_frame_tick.argtypes=[C.POINTER(Status),Emit,Page,C.c_void_p]
    checks=0
    for phase,step,flag,control,recovery,busy,result in itertools.product(
        (1,3,5),(0,1,2),(0,17),(0,1),(0,70,80),(0,1),(0,1)):
        initial=Status(phase,step,flag,control,recovery,busy)
        expected,events=Original(elf,initial,result).run()
        got=Status.from_buffer_copy(initial);actual=[]
        emit=Emit(lambda _,event:(actual.append(event),1)[1])
        page=Page(lambda _:(actual.append(14),result)[1])
        if phase==1:ret=native.em_status_frame_enter(C.byref(got),emit,None)
        else:ret=native.em_status_frame_tick(C.byref(got),emit,page,None)
        assert bytes(got)==bytes(expected),(tuple(getattr(initial,n) for n,_ in FIELDS),bytes(got),bytes(expected))
        assert actual==events,(phase,step,actual,events)
        assert ret==int(phase==5)
        checks+=1
    # Negative worker completion is a fault; it cannot publish phase5.
    state=Status(3,1,0,1,0,0);actual=[]
    emit=Emit(lambda _,event:(actual.append(event),1)[1]);page=Page(lambda _:-1)
    assert native.em_status_frame_tick(C.byref(state),emit,page,None)==-1
    assert state.phase==3 and actual==[5,6]
    report={'cases':checks,'state_and_ordered_calls':'PASS',
            'negative_page_result':'fault','boundary':'UI/audio/render workers'}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))

if __name__=='__main__':main()
