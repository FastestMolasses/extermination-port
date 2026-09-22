#!/usr/bin/env python3
"""Execute original 0017BC40 locally and compare the native scalar motor.

The original ELF supplies instructions and tables, never embedded here.
Finite EE arithmetic truncates at each operation. The live opening reference
is optional and remains ignored: the default test exercises valid motor states.
"""
import ctypes as C
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[1]
ACTOR=0x600000
RETURN=0xBADF00D


def bits(v):return struct.unpack('<I',struct.pack('<f',v))[0]
def number(v):return struct.unpack('<f',struct.pack('<I',v&0xffffffff))[0]
def signed(v):return (v&0x7fffffff)-(v&0x80000000)
def rtz(v):
    f=number(bits(v))
    return number(bits(f)-1) if abs(f)>abs(v) else f


class Motor(C.Structure):
    _fields_=[('speed',C.c_float),('target',C.c_float),('rate',C.c_float),('blend',C.c_float),
              ('mode',C.c_uint8),('substate',C.c_uint8),('tier',C.c_uint8),('gait',C.c_uint8),('obstruction',C.c_uint8)]

class Stop(C.Structure):
    _fields_=[('phase',C.c_uint),('blend_left',C.c_uint),('frame',C.c_uint),('last_frame',C.c_uint)]

FIELDS={'speed':(0x38,4),'target':(0x240,4),'rate':(0x204,4),'blend':(0x208,4),
        'mode':(0x1f0,1),'substate':(0x1f1,1),'tier':(0x25c,1),'gait':(0x23f,1),'obstruction':(0x314,1)}


def oracle(elf,state):
    mem={};r=[0]*32;f=[0]*32;condition=False
    def save(a,v,n=4):
        for i in range(n):mem[a+i]=v>>(i*8)&255
    def load(a,n=4):
        if a not in mem and 0x100000<=a<0x275b00:
            return int.from_bytes(elf[a-0x100000+0x300:a-0x100000+0x300+n],'little')
        return sum(mem.get(a+i,0)<<(i*8) for i in range(n))
    for name,(off,n) in FIELDS.items():
        value=getattr(state,name);save(ACTOR+off,bits(value) if n==4 else value,n)
    r[4],r[31]=ACTOR,RETURN
    def plain(w):
        nonlocal condition
        op,rs,rt,rd=w>>26,w>>21&31,w>>16&31,w>>11&31
        imm=w&65535;imm=imm if imm<32768 else imm-65536
        a=(r[rs]+imm)&0xffffffff
        if op==0:
            fn=w&63
            if fn==0:r[rd]=r[rt]<<(w>>6&31)&0xffffffff
            elif fn==33:r[rd]=(r[rs]+r[rt])&0xffffffff
            else:raise AssertionError(('SPECIAL',fn))
        elif op==9:r[rt]=a
        elif op==10:r[rt]=int(signed(r[rs])<imm)
        elif op==11:r[rt]=int(r[rs]<(imm&0xffffffff))
        elif op==12:r[rt]=r[rs]&(w&65535)
        elif op==13:r[rt]=r[rs]|(w&65535)
        elif op==15:r[rt]=(w&65535)<<16
        elif op in (35,36):r[rt]=load(a,4 if op==35 else 1)
        elif op in (40,43):save(a,r[rt],1 if op==40 else 4)
        elif op==49:f[rt]=load(a)
        elif op==57:save(a,f[rt])
        elif op==17:
            fs,fd,fn=rd,w>>6&31,w&63
            if rs==4:f[fs]=r[rt]
            elif rs==16:
                x,y=number(f[fs]),number(f[rt])
                if fn==0:f[fd]=bits(rtz(x+y))
                elif fn==1:f[fd]=bits(rtz(x-y))
                elif fn==2:f[fd]=bits(rtz(x*y))
                elif fn==3:f[fd]=bits(rtz(x/y))
                elif fn==6:f[fd]=f[fs]
                elif fn==50:condition=x==y
                elif fn==52:condition=x<y
                elif fn==54:condition=x<=y
                else:raise AssertionError(('FPU',fn))
            else:raise AssertionError(('COP1',rs,fn))
        else:raise AssertionError(('opcode',op))
        r[0]=0
    pc=0x17bc40
    for _ in range(400):
        if pc==RETURN:
            result=Motor()
            for name,(off,n) in FIELDS.items():
                value=load(ACTOR+off,n);setattr(result,name,number(value) if n==4 else value)
            return result
        assert 0x17bc40<=pc<0x17c030,hex(pc)
        w=load(pc);op,rs,rt=w>>26,w>>21&31,w>>16&31
        imm=w&65535;imm=imm if imm<32768 else imm-65536
        branch=None
        if op in (4,5):branch=pc+4+imm*4 if (r[rs]==r[rt])==(op==4) else pc+8
        elif op==17 and rs==8:branch=pc+4+imm*4 if condition==bool(rt&1) else pc+8
        elif op==0 and w&63==8:branch=r[rs]
        if branch is not None:plain(load(pc+4));pc=branch
        else:plain(w);pc+=4
    raise AssertionError('Motor did not terminate')


def comparable(m):
    return {k:bits(getattr(m,k)) if n==4 else getattr(m,k) for k,(off,n) in FIELDS.items()}


def main():
    elf=(ROOT.parent/'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest()=='ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
    out=ROOT/'build/player_motor_reference';out.mkdir(parents=True,exist_ok=True)
    lib=out/'motor.so'
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off','-shared','-fPIC','-Isrc','src/game/em_player_motor.c','-lm','-o',str(lib)],cwd=ROOT,check=True)
    native=C.CDLL(str(lib));native.em_player_motor_tick.argtypes=[C.POINTER(Motor)]
    from test_player_reentry_reference import check_reentry
    check_reentry(elf,native,Motor)
    rng=random.Random(0x17bc40);checks=0
    for _ in range(12000):
        m=Motor();m.mode=rng.randrange(8);m.substate=rng.randrange(4);m.tier=rng.randrange(4)
        # Exclude unreachable table overflows, which the host defensively ignores.
        if (m.mode==1 and ((m.substate==1 and m.tier==3) or (m.substate==2 and m.tier==0))) or (m.mode==2 and m.tier==0):continue
        m.gait=rng.randrange(4);m.target=(0,.1,.3,.8)[m.gait];m.speed=rng.choice([0,.05,.1,.15,.2,.25,.3,.3625,.5,.8,rng.random()])
        m.rate=rng.choice([0,.75,1,1.5,2]);m.blend=rng.choice([0,.5,.875,1]);m.obstruction=rng.randrange(256)
        expected=oracle(elf,m);native.em_player_motor_tick(C.byref(m))
        assert comparable(m)==comparable(expected),(comparable(m),comparable(expected))
        checks+=1
    # A sustained run must preserve the boundary re-arm callbacks and EE .3 edge.
    m=Motor(0,.8,1,0,1,1,0,3,0);sequence=[]
    for tick in range(30):
        expected=oracle(elf,m);native.em_player_motor_tick(C.byref(m))
        assert comparable(m)==comparable(expected)
        sequence.append(m.speed)
    assert sequence[1]==sequence[2] and sequence[7]==sequence[8]
    assert bits(sequence[6])+1==bits(sequence[7])==bits(.3)
    stop=Stop();native.em_player_stop_begin(C.byref(stop),10)
    stop_phases=[]
    for tick in range(26):
        stop_phases.append(stop.phase)
        if tick in (0,6):assert stop.frame==4
        if tick in (11,12):assert stop.frame==9
        if tick==12:assert stop.phase==3
        if tick==13:assert stop.phase==4 and stop.blend_left==12
        if tick==25:assert stop.phase==0
        native.em_player_stop_tick(C.byref(stop))
    result={'raw_EE_comparisons':checks+30,'run_speed_first_30':sequence,'stop_phases':stop_phases}
    live=ROOT.parent/'Extermination/build/startup-reference/first_control_poll.json'
    if live.exists():
        capture=json.loads(live.read_text());rows={r['frame']:r for r in capture['rows']}
        first=next(f for f,r in rows.items() if r['speed']>0)
        reference=[rows[first+i]['speed'] for i in range(21)]
        assert sequence[:21]==reference,(sequence[:21],reference)
        result['live_run_speed_ticks']=21
        stop_first=next(f for f,r in rows.items() if r['animation_id']==5)
        for tick,phase in enumerate(stop_phases):
            sample=rows[stop_first+tick]
            assert sample['animation_id']==(5 if phase in (1,2,3) else 0)
            if phase==3:assert sample['state'][1:3]==[0,0]
            if phase==4:assert sample['state'][1:3]==[0,1]
            if phase in (1,2):assert sample['motor'][0]==4
        result['live_stop_callback_ticks']=26
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print('player motor original-instruction PASS',checks+30,'comparisons; live first-control speed sequence matched' if live.exists() else '')


if __name__=='__main__':main()
