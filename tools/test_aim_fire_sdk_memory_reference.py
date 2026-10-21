#!/usr/bin/env python3
"""Pinned-instruction proof of the binding's copy/matrix/vector memory adapter.

Compares ordered reads and stores byte-for-byte (same-value writes included),
all final memory, defined f0 and refusal cuts. Arithmetic is provided by the
existing verified owners; no substitute Python behavior model is used.
Optional --targeted-mutations checks eight added entries' alignment once each;
the earlier seven-entry sweep is not repeated.
"""
import ctypes as C
import json
import random
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import test_player_slide_reference as S
import test_aim_fire_target_reference as T

BASE=0x680000
SIZE=0x200
MAP=C.CFUNCTYPE(C.c_void_p,C.c_void_p,C.c_uint32,C.c_size_t,C.c_int)
ENTRIES=(0x102948,0x102958,0x1031E0,0x1029C0,0x102918,0x102B08,0x102BB0,0x102798)
VECTORS=(0x1026A0,0x102760,0x102718,0x102738,0x1028D0,0x1028B8,0x103230,0x102900)
OUT=ROOT/'build/aim-fire/sdk-memory'

class Missing(Exception):
    pass

class Oracle(S.EE):
    def __init__(self,elf):
        super().__init__(elf)
        self.events=[]
        self.missing=None
    def mmi(self,word,pc):
        # 00102798's word interleaves (the slide core has no PEXTLW/PEXTUW):
        # rd = rs.w(k+1) rt.w(k+1) rs.wk rt.wk, high to low, k 0 or 2.
        rs,rt,rd=word>>21&31,word>>16&31,word>>11&31
        fn,sub=word&63,word>>6&31
        if fn in (0x08,0x28) and sub==0x12:
            full=lambda n:(self.r[n]&(1<<64)-1)|(self.rh[n]<<64)
            a,b=full(rs),full(rt)
            w=lambda v,i:v>>(32*i)&0xFFFFFFFF
            k=0 if fn==0x08 else 2
            v=w(b,k)|w(a,k)<<32|w(b,k+1)<<64|w(a,k+1)<<96
            if rd:self.r[rd]=v&(1<<64)-1;self.rh[rd]=v>>64
            return
        super().mmi(word,pc)
    def check(self,a,n):
        if self.missing is not None and a<self.missing+16 and a+n>self.missing:
            raise Missing()
    def load(self,a,n=4):
        self.check(a,n)
        value=super().load(a,n)
        if BASE<=a<BASE+SIZE:
            self.events.extend(('r',a+i,v) for i,v in enumerate(value.to_bytes(n,'little')))
        return value
    def save(self,a,v,n=4):
        self.check(a,n)
        super().save(a,v,n)
        if BASE<=a<BASE+SIZE:
            self.events.extend(('w',a+i,z) for i,z in enumerate(self.read(a,n)))

def build(source=None,path=None):
    OUT.mkdir(parents=True,exist_ok=True)
    path=path or OUT/('sdk-memory.dylib' if sys.platform=='darwin' else 'sdk-memory.so')
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off',
        '-shared','-fPIC','-Isrc',str(source or 'src/game/em_aim_fire_sdk_memory.c'),
        'src/game/em_owner_services_original.c','src/game/em_effect_original.c',
        'src/game/em_point_light.c','src/game/em_coll_probe_original.c',
        'src/game/em_actor_collision.c','src/game/em_actor_pool.c','src/game/em_collision.c',
        'src/game/em_camera_commit_original.c','src/game/em_sdk_math_original.c',
        '-lm','-o',str(path)],cwd=ROOT,check=True,capture_output=True)
    lib=C.CDLL(str(path))
    lib.em_aim_fire_sdk_memory_call.argtypes=[C.c_void_p,MAP,C.POINTER(T.Call)]
    lib.em_aim_fire_sdk_memory_call.restype=C.c_int
    return lib

def run(lib,e,fn,offset,seed,missing=None,source_offset=0,third_offset=0):
    rng=random.Random(seed)
    data=bytearray().join(S.bits(rng.uniform(-10,10)).to_bytes(4,'little') for _ in range(SIZE//4))
    e.write(BASE,data);e.events=[];e.missing=missing
    a,b,c=BASE+0x80+offset,BASE+0x80+source_offset,BASE+0xD0
    if seed&1:c=a+16  # translation input aliases a destination row
    c+=third_offset
    angle=S.bits((-.75,0.,.75,-1.5707963267948966)[seed%4])
    e.r[4:7]=[a,b,c];e.r[31]=S.RETURN;e.f[12]=angle
    expected=0
    try:e.run(fn)
    except Missing:expected=-1
    n=bytearray(data);array=(C.c_uint8*len(n)).from_buffer(n)
    events=[];pending=[];errors=[]
    def flush():
        if pending:
            start,size=pending.pop()
            events.extend(('w',start+i,z) for i,z in enumerate(n[start-BASE:start-BASE+size]))
    def mapping(_,address,size,write):
        flush()
        if missing is not None and address<missing+16 and address+size>missing:return None
        if address<BASE or address+size>BASE+SIZE:
            errors.append(('unexpected map',hex(address),size));return None
        if write:pending.append((address,size))
        else:events.extend(('r',address+i,z) for i,z in enumerate(n[address-BASE:address-BASE+size]))
        return C.addressof(array)+address-BASE
    cb=MAP(mapping);frame=T.Call();frame.function=fn;frame.a[0:3]=[a,b,c];frame.f[0]=angle
    status=lib.em_aim_fire_sdk_memory_call(None,cb,C.byref(frame));flush()
    context=(hex(fn),offset,seed,hex(missing) if missing else None,source_offset,third_offset)
    assert not errors,(context,errors)
    assert status==expected,(context,'status',status,expected)
    assert events==e.events,(context,'events',next(((i,x,y) for i,(x,y) in enumerate(zip(events,e.events)) if x!=y),None),len(events),len(e.events))
    assert bytes(n)==e.read(BASE,SIZE),(context,'final memory')
    if fn==0x102738 and expected==0:assert frame.f0==e.f[0],(context,'return',hex(frame.f0),hex(e.f[0]))
    return len(events)

SPAD3600=0x70003600

def look_at(lib,e,v,seed,out_offset=0):
    """001CD390(out, v) against the original instructions: the final out
    matrix (BASE region) and the sixteen scratchpad words 0x70003600..3F
    it builds the rows in, after poisoned starts. v.xyz are the words
    given (bit patterns, signed zeros included)."""
    rng=random.Random(seed)
    data=bytearray(rng.getrandbits(8) for _ in range(SIZE))
    spad=bytearray(rng.getrandbits(8) for _ in range(64))
    a,b=BASE+0x80+out_offset,BASE+0x140
    data[b-BASE:b-BASE+12]=b''.join(w.to_bytes(4,'little') for w in v)
    e.write(BASE,data);e.write(SPAD3600,spad);e.events=[];e.missing=None
    e.r[4:6]=[a,b];e.r[31]=S.RETURN
    e.run(0x1CD390)
    n=bytearray(data);array=(C.c_uint8*len(n)).from_buffer(n)
    sp=bytearray(spad);sarray=(C.c_uint8*64).from_buffer(sp)
    errors=[]
    def mapping(_,address,size,write):
        if BASE<=address and address+size<=BASE+SIZE:return C.addressof(array)+address-BASE
        if SPAD3600<=address and address+size<=SPAD3600+64:return C.addressof(sarray)+address-SPAD3600
        errors.append(('unexpected map',hex(address),size));return None
    cb=MAP(mapping);frame=T.Call();frame.function=0x1CD390;frame.a[0:2]=[a,b]
    status=lib.em_aim_fire_sdk_memory_call(None,cb,C.byref(frame))
    context=('001CD390',[hex(w) for w in v],seed)
    assert not errors and status==0,(context,errors,status)
    assert bytes(n)==e.read(BASE,SIZE),(context,'final memory')
    assert bytes(sp)==e.read(SPAD3600,64),(context,'scratchpad 0x70003600')

def look_at_cases(lib,e):
    zero,neg=0,0x80000000
    special=[(zero,zero,zero),(zero,S.bits(2.5),zero),(neg,S.bits(-1.0),zero),(zero,S.bits(3.0),neg),
             (neg,zero,neg),(S.bits(1e-30),S.bits(4.0),zero),(zero,S.bits(4.0),S.bits(-2.0)),
             (S.bits(1.0),zero,zero)]
    count=0
    for i,v in enumerate(special):
        look_at(lib,e,v,i);count+=1
    rng=random.Random(0x1CD390)
    for i in range(64):
        v=tuple(S.bits(rng.uniform(-500,500)) for _ in range(3))
        look_at(lib,e,v,100+i);count+=1
    # Refusals: an unaligned out, an out or a v inside 0x70003600..3F.
    for a,b in ((BASE+0x84,BASE+0x140),(SPAD3600,BASE+0x140),(BASE+0x80,SPAD3600+0x30)):
        frame=T.Call();frame.function=0x1CD390;frame.a[0:2]=[a,b]
        cb=MAP(lambda *_:None)
        assert lib.em_aim_fire_sdk_memory_call(None,cb,C.byref(frame))==-1,('refusal',hex(a),hex(b))
        count+=1
    return count

def targeted_mutations(elf):
    source=(ROOT/'src/game/em_aim_fire_sdk_memory.c').read_text()
    receipt=[]
    with tempfile.TemporaryDirectory(prefix='vector-alignment-',dir=OUT) as folder:
        folder=Path(folder)
        for fn in VECTORS:
            marker='    case 0x1028D0: case 0x1028B8:' if fn in (0x1028D0,0x1028B8) else f'    case 0x{fn:X}:'
            pos=source.index('\n',source.index(marker))+1
            end=source.index('\n',pos)
            line=source[pos:end]
            assert '&=~15u' in line,(hex(fn),line)
            mutation=f'        if(f->function!=0x{fn:X}) {{ {line.strip()} }}'
            changed=source[:pos]+mutation+source[end:]
            path=folder/f'vector-{fn:X}.c';path.write_text(changed)
            lib=build(path,folder/f'vector-{fn:X}.dylib')
            try:run(lib,Oracle(elf),fn,3,2,source_offset=15,third_offset=7)
            except AssertionError as error:
                receipt.append({'entry':f'{fn:08X}','mutation':'remove this entry quad-address masks','killed':True,'witness':str(error)})
            else:raise AssertionError(('surviving targeted alignment mutant',hex(fn)))
    (OUT/'vector-alignment-mutations.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(f'PASS: {len(receipt)} of {len(VECTORS)} targeted vector alignment mutants killed; no broad sweep rerun')

def main():
    started=time.monotonic();lib=build();e=Oracle(S.read_elf());count=events=cuts=0
    for fn in ENTRIES+VECTORS:
        offsets=(-8,-4,0,4,8,16) if fn==0x1031E0 else (-64,-16,-13,0,3,16,19,32,64)
        for seed in range(4):
            for offset in offsets:
                events+=run(lib,e,fn,offset,seed);count+=1
        if fn in (0x102948,0x102958,0x102B08,0x102BB0,0x102798)+VECTORS:
            for offset in (3,15):
                events+=run(lib,e,fn,64,2,source_offset=offset);count+=1
        if fn in (0x1026A0,0x102718,0x1028D0,0x1028B8):
            for offset in (1,15):
                events+=run(lib,e,fn,64,2,third_offset=offset);count+=1
        # Missing first/late source and destination rows, with disjoint ranges.
        for absent in (BASE+0x80,BASE+0xA0,BASE+0xE0,BASE+0x100):
            events+=run(lib,e,fn,96,2,absent);cuts+=1
    looks=look_at_cases(lib,e)
    print(f'PASS: {count} copy/matrix/vector overlap cases across {len(ENTRIES)+len(VECTORS)} entries; {cuts} missing-view cases; {events} ordered access bytes; 001CD390: {looks} look-at cases (out and 0x70003600..3F, 3 refusals); {time.monotonic()-started:.2f}s')
    if '--targeted-mutations' in sys.argv:targeted_mutations(S.read_elf())

if __name__=='__main__':main()
