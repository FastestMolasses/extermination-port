#!/usr/bin/env python3
"""Original FCA10/FDB80/FD790/FD950 timing for global panel message18.

The original timing table is read directly from the user's ELF. Glyph width,
text drawing and audio teardown are explicit call boundaries. No authored
message duration or input-dismissal rule is used by the native worker.
"""
import ctypes as C
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from test_status_frame_reference import Original as Base, Status
from test_interaction_animation_reference import signed

ROOT=Path(__file__).resolve().parents[1]
RETURN=0xBADF00D

class Original(Base):
    def __init__(self,elf,delay,first_line=0x18):
        super().__init__(elf,Status(1,0,0,0,0,0),0)
        self.put(0x2821B0,2);self.put(0x2821B4,1)
        self.put(0x2821B8,0x80000000|first_line);self.put(0x2821BC,delay)
        self.draw=-1;self.cleanup=0

    def plain(self,word):
        op,rs,rt,rd=word>>26,word>>21&31,word>>16&31,word>>11&31
        imm=signed(word&65535,16);address=(self.r[rs]+imm)&0xFFFFFFFF
        if op==0 and word&63 in (4,35,36,38,39,42,45,56,62):
            fn=word&63
            if fn==4:self.r[rd]=self.r[rt]<<(self.r[rs]&31)&0xFFFFFFFF
            elif fn==35:self.r[rd]=(self.r[rs]-self.r[rt])&0xFFFFFFFF
            elif fn==36:self.r[rd]=self.r[rs]&self.r[rt]
            elif fn==38:self.r[rd]=self.r[rs]^self.r[rt]
            elif fn==39:self.r[rd]=~(self.r[rs]|self.r[rt])&0xFFFFFFFF
            elif fn==42:self.r[rd]=int(signed(self.r[rs])<signed(self.r[rt]))
            elif fn==45:self.r[rd]=(self.r[rs]+self.r[rt])&0xFFFFFFFFFFFFFFFF
            elif fn==56:self.r[rd]=self.r[rt]<<(word>>6&31)&0xFFFFFFFFFFFFFFFF
            else:self.r[rd]=(self.r[rt]&0xFFFFFFFFFFFFFFFF)>>((word>>6&31)+32)
        elif op==10:self.r[rt]=int(signed(self.r[rs])<imm)
        elif op==12:self.r[rt]=self.r[rs]&(word&65535)
        elif op==25:self.r[rt]=(self.r[rs]+imm)&0xFFFFFFFFFFFFFFFF
        elif op==33:self.r[rt]=signed(self.get(address,2),16)&0xFFFFFFFF
        elif op==55:self.r[rt]=self.get(address,8)
        elif op in (41,63):self.put(address,self.r[rt],2 if op==41 else 8)
        else:super().plain(word)
        self.r[0]=0

    def tick(self,busy155,busy156):
        self.put(0x282155,busy155,1);self.put(0x282156,busy156,1)
        self.r[31]=RETURN;pc=0x1FCA10;self.draw=-1;self.cleanup=0
        for _ in range(4000):
            if pc==RETURN:return self.get(0x2821B4),self.draw,self.cleanup
            if pc in (0x1FE480,0x1FE530,0x1CC170,0x1FE070,0x1FAB80,0x121A28):
                if pc==0x1FE480:self.r[2]=0x950000
                elif pc==0x1FE530:self.r[2]=0x950000
                elif pc==0x1CC170:self.r[2]=10
                elif pc==0x1FE070:
                    self.draw=self.r[5]&0x7FFFFFFF
                    assert self.r[4]==self.get(0x28A4E8)
                    assert self.r[6:8]==[251,194]
                elif pc==0x1FAB80:self.cleanup+=1
                else:
                    assert self.r[4:7]==[0x2821B0,0,0x9C]
                    for address in range(0x2821B0,0x28224C):self.put(address,0,1)
                pc=self.r[31]&0xFFFFFFFF;continue
            assert (0x1FCA10<=pc<0x1FCB90 or 0x1FDB80<=pc<0x1FDDB0 or
                    0x1FD790<=pc<0x1FDB80 or 0x1FC9B0<=pc<0x1FCA10),hex(pc)
            word=self.get(pc);op,rs,rt=word>>26,word>>21&31,word>>16&31
            target=None
            if op in (1,4,5,6,7,20,21):
                if op==1:
                    assert rt in (0,1)
                    taken=(signed(self.r[rs])<0)==(rt==0)
                elif op in (4,5,20,21):taken=(self.r[rs]==self.r[rt])==(op in (4,20))
                elif op==6:taken=signed(self.r[rs])<=0
                else:taken=signed(self.r[rs])>0
                target=pc+4+signed(word&65535,16)*4 if taken else pc+8
                if op<20 or taken:self.plain(self.get(pc+4))
                pc=target;continue
            if op in (2,3):
                if op==3:self.r[31]=pc+8
                target=(word&0x3FFFFFF)<<2
            elif op==0 and word&63==8:target=self.r[rs]&0xFFFFFFFF
            if target is not None:self.plain(self.get(pc+4));pc=target
            else:self.plain(word);pc+=4
        raise AssertionError('Original global message failed to return')

BRIDGE=r'''
#include "game/em_panel_message.h"
static EmPanelMessage message;
int load(const char *path) {return em_panel_message_load(&message,path);}
void close_message(void) {em_panel_message_free(&message);}
int start(unsigned token,unsigned delay) {return em_panel_message_start(&message,token,delay);}
void tick(int a,int b,int *result) {
    result[2]=em_panel_message_tick(&message,a,b);
    result[0]=message.phase;
    const EmOpeningLine *line=em_opening_dialogue_line(&message.dialogue);
    result[1]=line ? line->line : -1;
}
'''

def main():
    assert sys.platform=='darwin','Host harness uses the macOS dead-strip linker'
    elf=(ROOT.parent/'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest()=='ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
    out=ROOT/'build/panel_message_reference';out.mkdir(parents=True,exist_ok=True)
    (out/'bridge.c').write_text(BRIDGE)
    library=out/'message.dylib'
    subprocess.run(['cc','-dynamiclib','-Wl,-undefined,dynamic_lookup','-Wl,-dead_strip',
        *[f'-Wl,-exported_symbol,_{name}' for name in ('load','close_message','start','tick')],
        '-O1','-g','-Wall','-Wextra','-Werror','-Isrc',str(out/'bridge.c'),
        'src/game/em_panel_message.c','src/game/em_opening_media.c','-lm','-o',str(library)],cwd=ROOT,check=True)
    native=C.CDLL(str(library));native.load.argtypes=[C.c_char_p]
    native.tick.argtypes=[C.c_int,C.c_int,C.POINTER(C.c_int)]
    count=0
    for first_line,asset in ((0x18,'panel/terminal.emod'),(0x1A,'elevator_refusal.emod')):
        for delay in (0,1,30):
            for busy_kind in (0,1,2):
                assert native.load(str(ROOT/'assets/scene_snow'/asset).encode())==1
                assert native.start(0x80000000|(first_line^2),delay)==0
                assert native.start(0x80000000|first_line,delay)==1
                original=Original(elf,delay,first_line);values=(C.c_int*3)();visible=0
                for frame in range(delay+165):
                    busy=frame<delay+155
                    a=int(busy and busy_kind==1);b=int(busy and busy_kind==2)
                    expected=original.tick(a,b);native.tick(a,b,values)
                    assert tuple(values)==expected,(first_line,delay,busy_kind,frame,tuple(values),expected)
                    visible+=values[1]==first_line;count+=1
                assert visible==149
                native.close_message()
    report={'original_worker_callbacks':count,'global_lines18_and1A_visible_draws_each':149,
            'delay_and_both_stream_gates':'PASS','glyph_rendering':'explicit boundary'}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))

if __name__=='__main__':main()
