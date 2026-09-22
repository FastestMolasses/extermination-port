#!/usr/bin/env python3
"""Compare elevator command bindings with original EE leaf handlers.

Player placement and camera publication are explicit external boundaries.
The original dispatch, direct stores and two-call camera timing execute from
the user's pinned ELF. No original instruction bytes are embedded here.
"""
import ctypes as C
import hashlib
import itertools
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
RETURN, ACTOR, STATE, RECORD = 0xbadf00d, 0x900000, 0x901000, 0x902000
FIELDS = (0x810350,0x810354,0x810358,0x810374,0x81037c,
          0x8105d0,0x8105d4,0x8105d8,0x8105e0,0x8105e4,0x8105e8,
          0x8104a2,0x8104a4,0x8104a8)
INITIAL = (11,12,13,2,0,-1,-2,-3,4,5,6,99,4,7)

def signed(value, width=32):
    value &= (1 << width)-1
    return value-(1 << width) if value >> (width-1) else value

def bits(value): return struct.unpack('<I',struct.pack('<f',value))[0]
def number(value): return struct.unpack('<f',struct.pack('<I',value & 0xffffffff))[0]

def original(elf, entry, record, phase, flags):
    memory = {}; r = [0]*32; f = [0]*32; condition = False
    calls = [0,0,0] # alignment, camera publication, vector copies
    def put(address, value, size=4):
        for i in range(size): memory[address+i] = value >> (i*8) & 255
    def get(address, size=4):
        def byte(at):
            if at in memory: return memory[at]
            return elf[at-0x100000+0x300] if 0x100000 <= at < 0x275b00 else 0
        return sum(byte(address+i) << (i*8) for i in range(size))
    for i, (address, value) in enumerate(zip(FIELDS, INITIAL)):
        put(address, value if i==11 else bits(value), 2 if i==11 else 4)
    for i, value in enumerate(record): put(RECORD+i,value,1)
    put(STATE+4,phase,1); put(0x8104b0,flags)
    r[4:7] = [ACTOR,STATE,RECORD]; r[29] = 0x70001000; r[31] = RETURN
    def plain(word):
        nonlocal condition
        op,rs,rt,rd = word>>26,word>>21&31,word>>16&31,word>>11&31
        immediate = signed(word&65535,16); address = (r[rs]+immediate)&0xffffffff
        if op==0:
            fn=word&63
            if fn==0: r[rd]=r[rt]<<(word>>6&31)&0xffffffff
            elif fn==10:
                if r[rt]==0: r[rd]=r[rs]
            elif fn in (33,45): r[rd]=(r[rs]+r[rt])&0xffffffff
            elif fn==36: r[rd]=r[rs]&r[rt]
            elif fn==37: r[rd]=r[rs]|r[rt]
            elif fn==43: r[rd]=int(r[rs]<r[rt])
            else: raise AssertionError(('SPECIAL',fn))
        elif op in (9,25): r[rt]=address
        elif op==11: r[rt]=int(r[rs]<(immediate&0xffffffff))
        elif op==12: r[rt]=r[rs]&(word&65535)
        elif op==13: r[rt]=r[rs]|(word&65535)
        elif op==15: r[rt]=(word&65535)<<16
        elif op==28 and word&63==40:
            assert r[rs]==0 or r[rt]==0
            r[rd]=r[rs]|r[rt]
        elif op in (30,32,33,35,36,37,55):
            size={30:16,32:1,33:2,35:4,36:1,37:2,55:8}[op]
            value=get(address,size)
            r[rt]=signed(value,size*8)&0xffffffff if op in (32,33) else value
        elif op in (31,40,41,43,63): put(address,r[rt],{31:16,40:1,41:2,43:4,63:8}[op])
        elif op==49: f[rt]=get(address)
        elif op==57: put(address,f[rt])
        elif op==17:
            if rs==4: f[rd]=r[rt]
            elif rs==16 and word&63 in (52,60): condition=number(f[rd])<number(f[rt])
            else: raise AssertionError(('COP1',rs,word&63))
        else: raise AssertionError(('opcode',op,hex(word)))
        r[0]=0
    pc=entry
    for _ in range(400):
        if pc==RETURN:
            return [r[2],get(STATE+4,1),*(get(a,2 if i==11 else 4) for i,a in enumerate(FIELDS)),*calls[:2]]
        if pc in (0x102948,0x182f90,0x1dd980):
            if pc==0x102948:
                put(r[4],get(r[5],16),16); calls[2]+=1
            elif pc==0x182f90:
                assert r[4]==0x8102b0
                put(0x810350,get(r[5],12),12); calls[0]+=1
            else:
                assert r[4:6]==[0x8105d0,0x8105e0]
                calls[1]+=1
            pc=r[31]&0xffffffff; continue
        assert 0x1b8fc0<=pc<0x1b9cf0,hex(pc)
        word=get(pc); op,rs,rt=word>>26,word>>21&31,word>>16&31
        target=None
        if op in (4,5,20,21) or (op==17 and rs==8):
            if op==17: taken=condition==bool(rt&1); likely=bool(rt&2)
            else: taken=(r[rs]==r[rt])==(op in (4,20)); likely=op>=20
            target=pc+4+signed(word&65535,16)*4 if taken else pc+8
            if taken or not likely: plain(get(pc+4))
            pc=target; continue
        if op in (2,3):
            if op==3: r[31]=pc+8
            target=(word&0x3ffffff)<<2
        elif op==0 and word&63 in (8,9):
            target=r[rs]&0xffffffff
            if word&63==9: r[word>>11&31]=pc+8
        if target is not None: plain(get(pc+4)); pc=target
        else: plain(word); pc+=4
    raise AssertionError('Original command did not return')

# This bridge invokes the actual native adapter with observable host boundaries.
BRIDGE = r'''
#include "game/em_elevator_program.c"
static uint32_t result[18];
static int align(void *p,const float v[3]){(void)p;memcpy(result+2,v,12);result[16]++;return1;}
static int face(void *p,float yaw){(void)p;memcpy(result+5,&yaw,4);float one=1;memcpy(result+6,&one,4);return1;}
static int cut(void *p,const float e[3],const float t[3]){(void)p;memcpy(result+7,e,12);memcpy(result+10,t,12);return1;}
static int publish(void *p){(void)p;result[17]++;return1;}
static int animation(void *p,uint16_t clip,float rate,float blend){(void)p;result[13]=clip;memcpy(result+14,&rate,4);memcpy(result+15,&blend,4);return1;}
static int done(void *p){return (*(unsigned *)p&0x1000)!=0;}
void probe(unsigned char *record,unsigned phase,unsigned flags,uint32_t *out){
  const float init[14]={11,12,13,2,0,-1,-2,-3,4,5,6,99,4,7};
  memset(result,0,sizeof result);memcpy(result+2,init,sizeof init);result[13]=99;
  EmElevatorProgram p={0};p.hooks.context=&flags;p.hooks.align_player=align;p.hooks.face_player=face;
  p.hooks.camera_set=cut;p.hooks.camera_publish=publish;p.hooks.animation_start=animation;p.hooks.animation_done=done;
  EmScript s={.phase=(int)phase};result[0]=(uint32_t)execute(&p,&s,record);result[1]=(uint32_t)s.phase;
  memcpy(out,result,sizeof result);
}
'''.replace('return1','return 1')

def main():
    output=ROOT/'build/elevator_reference'; output.mkdir(parents=True,exist_ok=True)
    source=output/'command_bridge.c'; source.write_text(BRIDGE)
    library=output/('commands.dylib' if sys.platform=='darwin' else 'commands.so')
    subprocess.run(['cc','-std=c11','-O1','-Wall','-Wextra','-Werror','-fPIC',
        '-dynamiclib' if sys.platform=='darwin' else '-shared','-Isrc',str(source),
        'src/game/em_script.c','-o',str(library)],cwd=ROOT,check=True)
    native=C.CDLL(str(library));native.probe.argtypes=[C.c_void_p,C.c_uint,C.c_uint,C.c_void_p]
    elf=(ROOT.parent/'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest()=='ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
    image=(ROOT/'assets/scene_snow/elevator.emsc').read_bytes()[20:]
    entries={1:0x1b94f0,2:0x1b9c10,3:0x1b8fc0,4:0x1b9a00,5:0x1b9a00}
    count=0
    for index,phase,flags in itertools.product(entries,(0,1),(0,0x1000)):
        record=image[index*64:(index+1)*64]
        expected=original(elf,entries[index],record,phase,flags)
        actual=(C.c_uint32*18)(); copy=C.create_string_buffer(record)
        native.probe(copy,phase,flags,actual)
        assert list(actual)==expected,dict(index=index,phase=phase,flags=flags,actual=list(actual),expected=expected)
        count+=1
    print(f'Original elevator alignment/yaw/camera/clip command bindings: {count} direct-state and call-order cases PASS')
    (output/'commands_validation.json').write_text(json.dumps({'cases':count,'handlers':list(entries.values()),
        'boundaries':['00182F90 player placement','001DD980 camera publication']},indent=2)+'\n')

if __name__=='__main__': main()
