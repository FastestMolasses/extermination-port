#!/usr/bin/env python3
"""Execute original001B82D0 sub0/sub2/sub4/sub13 against the shared interaction core.

Reads original instructions from the user's ELF. Fade, projection, audio and
skeleton calls are intercepted and compared in order. The original activity
clear helper runs as instructions. This proves command state/call order, not
the external player/status worker or the rendering of those side effects.
"""
import ctypes as C
import hashlib
import itertools
import json
from pathlib import Path
import struct
import subprocess
import sys
from test_battery_reference import signed

ROOT=Path(__file__).resolve().parents[1]
STATE,RECORD,RETURN=0x900000,0x901000,0xbadf00d
class Frame(C.Structure):
    _fields_=[(n,C.c_uint8) for n in ('selector','player_ready','ready',
        'camera_phase','camera_state','camera_swing','camera_top','camera_mode',
        'recovery_lock','auxiliary')]+[('activity',C.c_uint8*12),('counter',C.c_uint16),
        ('message_phase',C.c_int32),('zoom',C.c_float),('up',C.c_float*4)]
class Script(C.Structure):
    _fields_=[('active',C.c_int32),('phase',C.c_int32),('pc',C.c_uint32),
              ('skip_phase',C.c_int8),('skip_request',C.c_uint8)]
FIELDS={'selector':(0x70003B8D,1),'player_ready':(0x70003B8F,1),
        'ready':(0x70003B92,1),'camera_phase':(0x8101E1,1),'camera_state':(0x8101E2,1),
        'camera_swing':(0x8101E3,1),'camera_top':(0x8101E4,1),'camera_mode':(0x8101E6,1),
        'recovery_lock':(0x8106EF,1),'auxiliary':(0x8106F3,1),'counter':(0x70003B84,2),
        'message_phase':(0x2821B4,4)}
def bits(v):return struct.unpack('<I',struct.pack('<f',v))[0]
def number(v):return struct.unpack('<f',struct.pack('<I',v))[0]


def oracle(elf,initial,phase,skip_phase,skip_request,sub,immediate):
    memory={};r=[0]*32;fp=[0]*32;events=[];zoom=initial.zoom
    def put(a,n,size=4):
        for i in range(size):memory[a+i]=n>>(8*i)&255
    def get(a,size=4):
        def byte(at):
            if at in memory:return memory[at]
            return elf[at-0x100000+0x300] if 0x100000<=at<0x275b00 else 0
        return sum(byte(a+i)<<(8*i) for i in range(size))
    for name,(a,n) in FIELDS.items():put(a,getattr(initial,name),n)
    for i,v in enumerate(initial.activity):put(0x8106D4+i,v,1)
    for i,v in enumerate(initial.up):put(0x8105F0+4*i,bits(v))
    put(STATE+4,phase);put(STATE+12,skip_phase,1);put(0x70003B91,skip_request,1)
    put(RECORD+8,sub);put(RECORD+20,immediate)
    r[4],r[5],r[6],r[29],r[31]=0,STATE,RECORD,0x600000,RETURN
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
        elif op==17 and rs==4:fp[rd]=r[rt]&0xffffffff
        elif op==49:fp[rt]=get(a)
        elif op==57:put(a,fp[rt])
        else:raise AssertionError(('opcode',op))
        r[0]=0
    pc=0x1B82D0
    calls={0x1AEB60:0,0x1D2610:1,0x1AEBA0:2,0x1CA770:3,
           0x1D25F0:4,0x1FAE70:5,0x1AEE10:6}
    for _ in range(1200):
        if pc==RETURN:
            result=Frame.from_buffer_copy(initial)
            for name,(a,n) in FIELDS.items():setattr(result,name,get(a,n))
            for i in range(12):result.activity[i]=get(0x8106D4+i,1)
            for i in range(4):result.up[i]=number(get(0x8105F0+4*i))
            result.zoom=zoom
            return result,get(STATE+4),signed(get(STATE+12,1),8),get(0x70003B91,1),r[2],events
        if pc in calls:
            event=calls[pc];events.append(event)
            if event in (0,2,6):assert r[4]==4
            if event==1:assert fp[12]==0
            if event==3:assert r[4]==0x8102B0
            if event==4:assert number(fp[12])==480;zoom=480
            if event==5:assert r[4]==0
            if event==6:assert r[5]==0
            pc=r[31]&0xffffffff;continue
        assert 0x1B82D0<=pc<0x1B8AB0 or 0x1BA510<=pc<0x1BA550,hex(pc)
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
    raise AssertionError('Original frame command did not return')


def main():
    elf=(ROOT.parent/'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest()=='ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
    out=ROOT/'build/interaction_frame_reference';out.mkdir(parents=True,exist_ok=True)
    library=out/('frame.dylib' if sys.platform=='darwin' else 'frame.so')
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-fPIC',
        '-dynamiclib' if sys.platform=='darwin' else '-shared','-Isrc',
        'src/game/em_interaction_frame.c','-o',str(library)],cwd=ROOT,check=True)
    lib=C.CDLL(str(library));callback=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_int)
    lib.em_interaction_frame_command.argtypes=[C.POINTER(Frame),C.POINTER(Script),
                                              C.c_uint,C.c_int,callback,C.c_void_p]
    count=0
    for sub,phase,selector,ready,player,mode,skip,skip_phase,immediate in itertools.product(
            (0,2,4,13),(0,1,2,0x123400,0x123401),(0,1,2,3),(0,1),(0,1,2),(0,3),(0,2),(0,1,2),(0,1)):
        state=Frame(selector,player,ready,4,5,6,7,mode,8,9,
                    (C.c_uint8*12)(*range(12)),321,12,470,(C.c_float*4)(1,2,3,4))
        expected=oracle(elf,state,phase,skip_phase,skip,sub,immediate)
        script=Script(1,phase,0,skip_phase,skip);events=[]
        @callback
        def emit(_,event):events.append(event);return 1
        result=lib.em_interaction_frame_command(C.byref(state),C.byref(script),sub,
                                              immediate,emit,None)
        wanted,expected_phase,expected_skip_phase,expected_skip,result0,events0=expected
        assert bytes(state)==bytes(wanted),(count,sub,'state')
        assert (script.phase,script.skip_phase,script.skip_request,result,events)==(
                expected_phase,expected_skip_phase,expected_skip,result0,events0),(
                count,sub,phase,selector,ready,player,mode,skip,skip_phase,immediate,
                result,events,result0,events0)
        count+=1
    @callback
    def reject(_,event):return 0
    state=Frame();script=Script(1,0,0,0,0)
    assert lib.em_interaction_frame_command(C.byref(state),C.byref(script),2,0,reject,None)==-1
    result={'cases':count,'state_and_ordered_call_matches':count,
            'external_player_and_status_workers_verified':False}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print('interaction frame original001B82D0 PASS',json.dumps(result))


if __name__=='__main__':main()
