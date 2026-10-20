#!/usr/bin/env python3
"""Reached F4CC0 glow path (and the 001F4A00 tail call) over the actual borrowed sprite/packet owners.
Original 159B90/158590 produce the first reached arguments from captured
state; complete F4CC0/F4BF0/CD520 bodies run on both sides. Only RNG is an
original-code boundary on the native side. RAM/scratch and nested calls
are compared, including actual native fog programming and packet writes.
"""
import ctypes as C
import struct
import random
import reference_mode as mode
import test_area01_flame_services_reference as F
from test_area01_runtime_reference import Call,Worker
import reference_root as refs  # noqa: E402  (EM_REFERENCE: fork by default)
R=F.R
SPECS={0x21B9A0:(1,2),0x1F4BF0:(2,0),0x122BB8:(0,0),0x1CD520:(5,3)}

def seed(elf,beat):
    ram,spr=R.image(beat)
    for address,size in ((0x159B90,0x2D4),(0x158590,0x278),(0x1F4CC0,0x78),
                         (0x1F4BF0,0xD0),(0x1CD520,0x420)):
        offset=address-0x100000+0x300
        assert ram[address:address+size]==elf[offset:offset+size],(beat,hex(address))
    nodes=R.pool(ram,{0x159B90})
    assert len(nodes)==1,(beat,nodes)
    e=F.oracle(elf,ram,spr)
    class Reached(Exception):pass
    got=[]
    def boundary(x):
        got.append((tuple(x.r[4:6]),x.r[29],bytes(x.mem),bytes(x.spad)))
        raise Reached()
    e.hooks[0x1F4CC0]=boundary
    try:R.oracle_call(e,0x159B90,(nodes[0],))
    except Reached:pass
    assert got,(beat,'original idle did not reach glow')
    args,sp,ram,spr=got[0]
    assert args==(0x700038A0,0x700038B0)
    return ram,spr,args,sp

def one(n,elf,ram,spr,args,sp,label,deny=None,fn=0x1F4CC0):
    ram=bytearray(ram);spr=bytearray(spr)
    F.put(ram,0x811CC0+0x18,0x300000);F.put(ram,0x811CC0+0x1C,0x300000)
    ram[0x300000:0x310000]=bytes(0x10000);ram[0x7635C0:0x76B5C0]=bytes(0x8000)
    f=F.Fixture(ram,spr);e=F.oracle(elf,ram,spr);native=F.native_oracle(elf,f)
    expected=[];actual=[];errors=[]
    def original_hook(fn):
        def cb(x):
            na,nf=SPECS[fn]
            expected.append((fn,x.r[29],tuple(v&((1<<64)-1) for v in x.r[4:4+na]),tuple(x.f[12:12+nf])))
            F.execute(x,fn)
        return cb
    # 001F4A00 is the frameless tail call to 001F4BF0: its glow body is the
    # callee itself, so only the glow's own nested calls are boundaries.
    for hooked in SPECS:
        if fn==0x1F4CC0 or hooked not in (0x1F4BF0,0x21B9A0):e.hooks[hooked]=original_hook(hooked)
    e.r[29]=sp;e.r[31]=R.RETURN;e.r[4],e.r[5]=args
    refused=False
    class Refused(Exception):pass
    if deny:
        original_load,original_save=e.load,e.save
        def overlaps(a,z):return a<deny[0]+deny[1] and a+z>deny[0]
        def load(a,z=4):
            if not deny[2] and overlaps(a,z):raise Refused()
            return original_load(a,z)
        def save(a,value,z=4):
            if deny[2] and overlaps(a,z):raise Refused()
            return original_save(a,value,z)
        e.load=load;e.save=save
    try:e.run(fn)
    except Refused:refused=True
    if deny:assert refused,(label,'original did not reach refusal')
    f.deny=deny
    n.fs_seed(f.ram,f.spr);n.fs_glow_reset()
    @Worker
    def worker(_,ptr):
        c=ptr.contents
        try:
            na,nf=SPECS[c.function];assert (na,nf)==(c.na,c.nf)
            actual.append((c.function,c.sp,tuple(c.a[:na]),tuple(c.f[:nf])))
            if c.function==0x21B9A0:
                rc=n.fs_fog(C.c_int(c.a[0]),c.f[0],c.f[1]);n.fs_publish(f.ram);return rc
            if c.function==0x122BB8:
                native.r[29]=c.sp;native.r[31]=R.RETURN;native.run(c.function)
                c.v0=native.r[2]&((1<<64)-1);return 0
            fault=C.c_uint32()
            rc=n.fs_call(C.byref(f.host),ptr,C.byref(fault));n.fs_publish(f.ram)
            if not deny:assert rc==0,(hex(c.function),hex(fault.value))
            return rc
        except BaseException as ex:errors.append(ex);return -1
    f.host.worker=worker
    c=Call();c.function=fn;c.sp=sp;c.na=2;c.a[0],c.a[1]=args
    fault=C.c_uint32();rc=n.fs_call(C.byref(f.host),C.byref(c),C.byref(fault));n.fs_publish(f.ram)
    assert not errors,(label,errors)
    assert (rc<0)==bool(deny),(label,rc,hex(fault.value))
    assert actual==expected,(label,actual,expected)
    F.compare(f,e,label)
    if deny:
        if deny[0]!=args[1]:
            assert n.fs_glow_fault()==deny[0]
            # A second direct sprite entry sees the same fault before mapping.
            before=len(f.access);bad=Call();bad.function=0x1CD520;bad.na=5;bad.nf=3
            assert n.fs_call(C.byref(f.host),C.byref(bad),C.byref(fault))<0
            assert len(f.access)==before
        return 0,len(actual)
    emitted=F.u32(f.ram,0x811CC0+0x18)!=0x300000
    assert n.fs_glow_valid()==emitted,(label,emitted,n.fs_glow_valid())
    assert any(a==0x70003600 and z==32 and w==1 for a,z,w in f.access)
    return emitted,len(actual)

def main():
    mode.banner('AREA01 borrowed idle glow')
    n=F.build();elf=F.read_elf();n.fs_fog.argtypes=[C.c_int,C.c_uint32,C.c_uint32]
    count=packets=calls=0
    # Every capture, including the unchanged arrival image, contributes its
    # actual reached 159B90 -> 158590 boundary (no forced owner state).
    arrival=refs.capture('s87/route/15_level_exit')
    R.IMAGES['arrival']=((arrival/'eeMemory.bin').read_bytes(),(arrival/'scratchpad.bin').read_bytes())
    for beat in ['arrival']+R.BEATS:
        if R.image(beat)[0][0x810700]!=1:continue
        ram,spr,args,sp=seed(elf,beat)
        emitted,ncalls=one(n,elf,ram,spr,args,sp,beat)
        count+=1;packets+=emitted;calls+=ncalls
    # Positive controls guarantee both clipped and emitted sprite branches,
    # plus nonzero and wrap-prone original channel arithmetic.
    rng=random.Random(0x1F4CC0)
    for k in range(mode.pick(80,12)):
        r=bytearray(ram);s=bytearray(spr)
        identity=struct.pack('<16f',1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1)
        r[0x811CC0+0x2240:0x811CC0+0x2280]=identity
        s[0x3A40:0x3A80]=identity;s[0x3AC0:0x3B00]=identity
        struct.pack_into('<4f',s,0x38A0,0 if k%2==0 else 20,0,.5,1)
        struct.pack_into('<4I',s,0x38B0,*(rng.getrandbits(32) if k%3==0 else rng.randrange(256) for _ in range(4)))
        emitted,ncalls=one(n,elf,r,s,args,sp,('controlled clip',k))
        assert emitted==(k%2==0)
        count+=1;packets+=emitted;calls+=ncalls
    # 001F4A00 (00128C10's glow through 001288D0): the tail call runs the
    # glow on the caller's stack with 001288D0's two colour sets (byte
    # +0xE1 zero / non-zero) over every capture and the controlled clips.
    tails=0
    for beat in ['arrival']+R.BEATS:
        if R.image(beat)[0][0x810700]!=1:continue
        ram0,spr0,args0,sp0=seed(elf,beat)
        for words in ((0x20,0x70,0x80,0x80),(0x80,0x50,0x30,0x80)):
            s1=bytearray(spr0);struct.pack_into('<4I',s1,0x38B0,*words)
            emitted,ncalls=one(n,elf,ram0,s1,args0,sp0,('tail',beat,words),fn=0x1F4A00)
            count+=1;packets+=emitted;calls+=ncalls;tails+=1
    for k in range(mode.pick(16,4)):
        r1=bytearray(ram);s1=bytearray(spr)
        identity=struct.pack('<16f',1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1)
        r1[0x811CC0+0x2240:0x811CC0+0x2280]=identity
        s1[0x3A40:0x3A80]=identity;s1[0x3AC0:0x3B00]=identity
        struct.pack_into('<4f',s1,0x38A0,0 if k%2==0 else 20,0,.5,1)
        struct.pack_into('<4I',s1,0x38B0,*((0x20,0x70,0x80,0x80) if k%4<2 else (0x80,0x50,0x30,0x80)))
        emitted,ncalls=one(n,elf,r1,s1,args,sp,('tail clip',k),fn=0x1F4A00)
        assert emitted==(k%2==0)
        count+=1;packets+=emitted;calls+=ncalls;tails+=1
    # Original stopped at the actual denied read/store, preserving prior
    # fog programming and RNG changes; no restoration of canonical writes.
    struct.pack_into('<4f',s,0x38A0,0,0,.5,1)
    for deny in ((0x700038B0,16,0),(0x700038A0,16,0),(0x70003600,32,1)):
        one(n,elf,r,s,args,sp,('refusal',deny),deny)
        one(n,elf,r,s,args,sp,('tail refusal',deny),deny,fn=0x1F4A00)
    assert packets
    print(f'PASS {mode.MODE}: {count} complete original F4CC0/F4BF0/CD520 bodies, '
          f'{calls} ordered nested calls, {packets} emitted sprite chains ({tails} through the 001F4A00 tail call), exact RAM/scratch and shared fog validity; 6 original refusal prefixes')
if __name__=='__main__':main()
