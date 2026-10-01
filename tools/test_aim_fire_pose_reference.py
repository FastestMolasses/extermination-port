#!/usr/bin/env python3
"""Original-instruction proof of the armed pose quartet; no original bytes embedded.

Checks every non-ABI store, call arguments and memory at each callee entry,
final memory and the slot lookup return. Scripted callees deliberately change
fields the callers must reread. Exhaustive sampling is EM_TEST_FULL=1.
"""
import ctypes as C
import os
import random
import struct
import subprocess
import sys
from pathlib import Path

import reference_mode as mode
from test_player_fall_reference import FallEE
from test_player_slide_reference import read_elf, RETURN, bits, sx32

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/aim-fire/pose'
U = C.c_uint32
MAP = C.CFUNCTYPE(C.c_void_p, C.c_void_p, U, C.c_size_t, C.c_int)
CALL = C.CFUNCTYPE(C.c_int, C.c_void_p, U, C.POINTER(U), C.POINTER(U), C.POINTER(U))

class Host(C.Structure):
    _fields_ = [('context', C.c_void_p), ('map', MAP), ('call', CALL), ('fault', U)]

SIZES = {0x17A130: 0x6C8, 0x17A0B0: 0x74, 0x179BC0: 0xD4, 0x179CA0: 0x80}
CALLEES = {0x17A0B0:(2,0), 0x179BC0:(3,0), 0x179CA0:(3,1),
           0x102760:(2,0), 0x1749F0:(2,2), 0x1C6DA0:(1,0), 0x102958:(2,0)}
P = 0x680000
SPANS = [(P,0x400), (0x248B70,0x200), (0x275B40,4), (0x286340,0x3800),
         (0x690000,0x2000), (0x70003A20,4)]

class Oracle(FallEE):
    def __init__(self, elf):
        super().__init__(elf)
        self.stores = []
        self.tracking = False
        self.cover = set()
    def save(self, address, value, size=4):
        if self.tracking and not 0x7F000000 <= address < 0x7F100000:
            self.stores.append((address,size,value & ((1 << (8*size))-1)))
        super().save(address,value,size)
    def branch(self, word, pc):
        b = super().branch(word,pc)
        if b is not None and any(s <= pc < s+n for s,n in SIZES.items()):
            self.cover.add((pc,bool(b[0])))
        return b

class Native:
    def __init__(self, e):
        self.mem = bytearray(e.mem)
        self.spad = bytearray(e.spad)
        self.buffers = [(C.c_ubyte*len(b)).from_buffer(b) for b in (self.mem,self.spad)]
        self.pending = None
        self.stores = []
        self.error = None
        self.denied = None
        self.call_count = 0
    def where(self, at):
        return (self.spad,at-0x70000000,1) if at>=0x70000000 else (self.mem,at,0)
    def read(self,at,n):
        b,i,_ = self.where(at)
        return bytes(b[i:i+n])
    def write(self,at,data):
        b,i,_ = self.where(at)
        b[i:i+len(data)] = data
    def flush(self):
        if self.pending:
            at,n = self.pending
            self.stores.append((at,n,int.from_bytes(self.read(at,n),'little')))
            self.pending = None
    def mapping(self,ctx,at,n,write):
        self.flush()
        if self.denied is not None and at <= self.denied < at+n: return None
        b,i,k = self.where(at)
        if i<0 or i+n>len(b):
            self.error = ('bad map',hex(at),n)
            return None
        if write: self.pending = (at,n)
        return C.addressof(self.buffers[k])+i


def snap(m): return tuple(m.read(a,n) for a,n in SPANS)
def load(m,a,n=4): return int.from_bytes(m.read(a,n),'little')
def put(m,a,v,n=4): m.write(a,(v & ((1<<(8*n))-1)).to_bytes(n,'little'))

def script(m, pc, args, floats, index, seed, mutate):
    rng = random.Random(seed*1009+index)
    def fill(at,n): m.write(at,rng.randbytes(n))
    if pc == 0x17A0B0:
        if mutate:
            put(m,P+0x276,rng.randrange(0x10000),2)
            put(m,P+0x278,rng.choice([bits(0),bits(.25),bits(.75),bits(1)]))
        return (0xFFFF8000 + rng.randrange(0x10000)) & 0xFFFFFFFF
    if pc == 0x179BC0:
        fill(args[2],0xC0)
        if mutate:
            put(m,P+0xC,rng.randrange(4),1)
            put(m,P+0x278,rng.choice([bits(0),bits(.25),bits(.5),bits(.75),bits(1)]))
            put(m,P+0x27C,rng.choice([bits(-1),bits(.25),bits(.75),bits(2)]))
    elif pc == 0x179CA0:
        fill(args[0],64)
        if mutate:
            put(m,0x70003A20,bits(rng.choice([0,.125,.25,.5])))
            put(m,P+0xC,rng.randrange(4),1)
    elif pc == 0x102760:
        fill(args[0],12)
        if mutate:
            put(m,0x690000+4*(index%3),0x690100+0x200*(index%3))
    elif pc == 0x1749F0:
        if mutate: put(m,P+0xC,rng.randrange(4),1)
        put(m,P+0x20C,args[1],2)
    elif pc == 0x1C6DA0:
        for i in range(3): fill(0x690190+0x200*i,64)
        if mutate: put(m,P+0xC,rng.randrange(4),1)
    elif pc == 0x102958:
        m.write(args[0],m.read(args[1],64))
    else: raise AssertionError(hex(pc))
    return 0


def case(spec):
    entry,seed,x,y,n,mutate,alias = spec
    rng = random.Random(seed)
    e = Oracle(ELF)
    for at,size in SPANS: e.write(at,rng.randbytes(size))
    if seed >= 100000:
        e.write(P,CAPTURE_RECORDS[(seed-100000)//10])
    put(e,P+5,rng.choice([0,0x1D,0x1E,0x1F,0x20]),1)
    put(e,P+0x275,rng.randrange(6),1)
    put(e,P+0x1F0,rng.choice([0x31,0x32,0x34,0x35]),1)
    put(e,P+0xC,n,1)
    put(e,P+0x278,x);put(e,P+0x27C,y)
    put(e,0x275B40,0x690000)
    for i in range(3): put(e,0x690000+4*i,0x690100+0x200*i)
    for bank,table in enumerate((0x248B70,0x248C50)):
        for i in range(6): put(e,table+4*i,0x690800+bank*0x100+32*i)
    a,b,c,f = P,0,0,0
    if entry == 0x17A0B0: b=seed%9
    elif entry == 0x179BC0: b=seed & 0xFFFF; c=0x286340
    elif entry == 0x179CA0:
        a,b,c,f = 0x286340,0x287140,0x287F40,x
        if alias==1: a=b
        if alias==2: a=c
        if alias==3: a=b+4
    native = Native(e)
    events=[]
    def hook(pc):
        def run(q):
            ac,fc=CALLEES[pc]
            args=tuple(v & 0xFFFFFFFF for v in q.r[4:4+ac])
            floats=tuple(q.f[12:12+fc])
            events.append((pc,args,floats,snap(q),list(q.stores)))
            q.tracking=False
            q.r[2]=sx32(script(q,pc,args,floats,len(events),seed,mutate))
            q.tracking=True
        return run
    e.hooks = {pc:hook(pc) for pc in CALLEES if pc!=entry}
    e.r[4:7]=[sx32(a),sx32(b),sx32(c)]; e.f[12]=f; e.r[31]=RETURN
    e.tracking=True
    e.run(entry)
    def call(ctx,pc,args,floats,result):
        native.flush()
        try:
            ac,fc=CALLEES[pc]
            argv=tuple(args[i] for i in range(ac)); fv=tuple(floats[i] for i in range(fc))
            event=(pc,argv,fv,snap(native),list(native.stores))
            assert native.call_count<len(events), ('extra call',hex(pc))
            expected=events[native.call_count]
            assert event==expected, ('callee boundary',entry,seed,native.call_count,
                                      hex(pc),argv,fv,expected[:3],
                                      'store mismatch' if event[4]!=expected[4] else 'memory/args')
            native.call_count+=1
            result[0]=script(native,pc,argv,fv,native.call_count,seed,mutate)
            return 0
        except BaseException as err:
            native.error=err
            return -1
    callbacks=(MAP(native.mapping),CALL(call))
    h=Host(None,*callbacks,0)
    result=U(0xAABBCCDD)
    status=LIB.em_aim_fire_pose_run(C.byref(h),entry,a,b,c,f,C.byref(result))
    native.flush()
    assert native.error is None, native.error
    assert status==0 and h.fault==0,(spec,status,hex(h.fault))
    assert native.call_count==len(events),(spec,'missing calls')
    assert native.stores==e.stores,(spec,'store sequence',native.stores,e.stores)
    assert snap(native)==snap(e),(spec,'final memory')
    if entry==0x17A0B0: assert result.value==e.r[2]&0xFFFFFFFF
    # Reached missing memory and worker cuts must latch a fault.
    native.denied=a+5 if entry==0x17A0B0 else None
    if entry==0x17A0B0:
        h.fault=0
        assert LIB.em_aim_fire_pose_run(C.byref(h),entry,a,b,c,f,C.byref(result))==-1
        assert h.fault==a+5
    return len(events),len(e.stores),e.cover


def main():
    global ELF,LIB,CAPTURE_RECORDS
    ELF=read_elf()
    route=ROOT.parent/"Extermination/build/s87/route"
    paths=sorted(p for p in route.glob("*/eeMemory.bin") if mode.in_scope_beat(p.parent.name))
    CAPTURE_RECORDS=[]
    for path in paths:
        with path.open("rb") as capture:
            capture.seek(0x8102B0)
            CAPTURE_RECORDS.append(capture.read(0x320))
    assert len(CAPTURE_RECORDS)==15, "15 AREA11 captured player records required"
    OUT.mkdir(parents=True,exist_ok=True)
    src=Path(os.environ.get('EM_AIM_FIRE_POSE_SOURCE',ROOT/'src/game/em_aim_fire_pose.c'))
    dylib=OUT/'pose.so'
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wpedantic','-shared','-fPIC',
                    '-Isrc',str(src),'-o',str(dylib)],cwd=ROOT,check=True)
    LIB=C.CDLL(str(dylib))
    LIB.em_aim_fire_pose_run.argtypes=[C.POINTER(Host),U,U,U,U,U,C.POINTER(U)]
    LIB.em_aim_fire_pose_run.restype=C.c_int
    e=Oracle(ELF)
    found=set()
    conditionals=set()
    for at,size in SIZES.items():
        for pc in range(at,at+size,4):
            w=e.load(pc)
            if w>>26==3: found.add((w&0x3FFFFFF)<<2)
            op,rs,rt=w>>26,(w>>21)&31,(w>>16)&31
            if e.branch(w,pc) is not None and not (op in (4,20) and rs==rt):
                conditionals.add(pc)
    assert found==set(CALLEES),(found,set(CALLEES))
    values=[0x80000000,0,bits(-1),bits(.25),bits(.5),bits(.75),bits(1),bits(2),
            0x3EFFFFFF,0x3F000001,0x3F7FFFFF,0x3F800001,0x7FC00000,0xFF800000]
    allcases=[]
    for i,x in enumerate(values):
        for j,y in enumerate(values):
            allcases.append((0x17A130,i*100+j,x,y,(i+j)%4,(i+j)%3==0,0))
    for i in range(180):
        allcases.append((0x17A0B0,i,0,0,0,False,0))
        allcases.append((0x179BC0,i,0,0,i%4,i%2,0))
    for i in range(1024):
        r=random.Random(i+700)
        x=r.getrandbits(32) if i>len(values) else values[i%len(values)]
        allcases.append((0x179CA0,i,x,0,0,False,i%4))
    for i in range(len(CAPTURE_RECORDS)):
        for j in range(3):
            allcases.append((0x17A130,100000+10*i+j,values[3+j],values[3+j],j+1,False,0))
    chosen=mode.select(allcases,305,8711,axes=(lambda c:c[0],lambda c:(c[0],c[4],c[5],c[6])),
                       keep=lambda i,c:(c[0]==0x17A130 and i<196) or c[1]>=100000)
    mode.banner(mode.part(len(chosen),len(allcases),'pose cases'))
    results=mode.parallel_map(case,chosen)
    calls=sum(x[0] for x in results); stores=sum(x[1] for x in results)
    outcomes=set().union(*(x[2] for x in results))
    gaps={(pc,taken) for pc in conditionals for taken in (False,True)}-outcomes
    assert not gaps, ('uncovered conditional outcomes',[(hex(pc),taken) for pc,taken in sorted(gaps)])
    print(f'captured player records: {len(CAPTURE_RECORDS)} (45 designed stance cases)')
    print(f'PASS aim/fire pose: {len(results)} cases, {calls} callee boundaries, '
          f'{stores} ordered stores, {len(outcomes)} branch outcomes; '
          f'all {len(conditionals)} conditional sites exercised both ways')

if __name__=='__main__': main()
