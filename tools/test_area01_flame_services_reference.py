#!/usr/bin/env python3
"""Flame service adapters versus original code; actual effects/packet owners.

Captured renderer inputs are fixtures only. The flame caller initializes its
two newly mapped matrices from poison, using the original/native SDK copies.
"""
import ctypes as C
from pathlib import Path
import random
import struct
import subprocess
import reference_mode as mode
from test_area01_runtime_reference import Call,Host,View,Worker
import test_area01_render_reference as R
from test_player_slide_reference import read_elf,STACK_TOP

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/level2/flame-services'
def u32(b,a):return struct.unpack_from('<I',b,a)[0]
def put(b,a,v):struct.pack_into('<I',b,a,v&0xFFFFFFFF)
def build():
    OUT.mkdir(parents=True,exist_ok=True);target=OUT/'bridge.dylib'
    sources=['tests/area01_flame_services_bridge.c','tests/area01_glow_runtime_bridge.c',
             'src/game/em_aim_fire_render_live.c','src/game/em_aim_fire_reticle.c',
             'src/game/em_player_equipment_sprite.c','src/game/em_area00_hud.c','src/game/em_area02_misc.c',
             'src/game/em_level8_port_fx.c','src/game/em_status_scene_original.c','src/game/em_area01_flame_services.c',
             'src/game/em_area01_render_gs.c','src/game/em_area01_render_hud.c','src/game/em_weather_packets.c',
             'src/game/em_area01_sys.c','src/game/em_aim_fire_sdk_memory.c','src/game/em_camera_commit_original.c','src/game/em_sdk_math_original.c',
             'src/game/em_coll_probe_original.c','src/game/em_owner_services_original.c',
             'src/game/em_stream_lanes_original.c','src/game/em_effect_original.c',
             'src/game/em_effect_manager.c',
             'src/game/em_effect_kinds.c','src/game/em_head_sprite_original.c',
             'src/game/em_packet_chain_original.c','src/game/em_actor_pool.c',
             'src/game/em_status_ui_leftovers.c']
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off',
                    '-shared','-fPIC','-Isrc','-Wl,-dead_strip','-Wl,-exported_symbol,_fs_*',
                    *sources,'-lm','-o',str(target)],cwd=ROOT,check=True)
    n=C.CDLL(str(target))
    for fn in ('fs_call','fs_packet'):getattr(n,fn).argtypes=[C.POINTER(Host),C.POINTER(Call),C.POINTER(C.c_uint32)]
    n.fs_seed.argtypes=[C.c_void_p,C.c_void_p];n.fs_publish.argtypes=[C.c_void_p]
    n.fs_window.argtypes=[C.c_uint32,C.c_uint32];n.fs_window.restype=C.c_void_p
    n.fs_sys.argtypes=[C.POINTER(Host),C.c_uint32,C.POINTER(C.c_uint32)]
    n.fs_sdk.argtypes=[C.POINTER(Host),C.POINTER(Call)]
    return n

class Fixture:
    def __init__(self,ram,spr):
        self.ram=(C.c_uint8*len(ram)).from_buffer_copy(ram)
        self.spr=(C.c_uint8*len(spr)).from_buffer_copy(spr)
        self.stack=(C.c_uint8*0x100000)()
        self.access=[];self.deny=None
        @View
        def view(_,a,n,w):
            self.access.append((a,n,w))
            if self.deny==(a,n,w):return None
            # Exact new matrices: no joined span or captured initialization.
            for lo,hi in ((0x700036E0,0x70003720),(0x70003720,0x70003760)):
                if a<hi and a+n>lo and not(lo<=a and a+n<=hi):return None
            if a>=0x70000000 and a+n<=0x70004000:return C.addressof(self.spr)+a-0x70000000
            if a>=0x7F000000 and a+n<=0x7F100000:return C.addressof(self.stack)+a-0x7F000000
            if a+n<=len(self.ram):return C.addressof(self.ram)+a
            return None
        self.view=view;self.host=Host(None,view,Worker())
    def call(self,n,fn,args=(),floats=(),packet=False):
        c=Call();c.function=fn;c.sp=STACK_TOP;c.na=len(args);c.nf=len(floats)
        for i,v in enumerate(args):c.a[i]=v&((1<<64)-1)
        for i,v in enumerate(floats):c.f[i]=v
        fault=C.c_uint32()
        rc=(n.fs_packet if packet else n.fs_call)(C.byref(self.host),C.byref(c),C.byref(fault))
        return rc,c,fault.value
def oracle(elf,ram,spr):return R.A01EE(elf,ram=ram,spad=spr)
def native_oracle(elf,f):
    e=oracle(elf,b'',b'');e.mem=f.ram;e.spad=f.spr;e.stack=f.stack;return e
def execute(e,fn):
    saved=e.r[31];hook=e.hooks.pop(fn,None);e.r[31]=R.RETURN
    try:e.run(fn)
    finally:
        e.r[31]=saved
        if hook is not None:e.hooks[fn]=hook
def compare(f,e,label):
    assert bytes(f.spr)==bytes(e.spad),(label,'scratch',R.first_differences(bytes(f.spr),bytes(e.spad),0x70000000))
    assert bytes(f.ram)==bytes(e.mem),(label,'ram',R.first_differences(bytes(f.ram),bytes(e.mem)))
def main():
    n=build();elf=read_elf();rng=random.Random(0x1CFAE0);count=0
    from export_effect_tables import BLOCKS,elf_block
    assert n.fs_overflow()==0
    assert n.fs_load()==0
    for address,size in BLOCKS:
        p=n.fs_window(address,size);assert p,(hex(address),size,'missing exported window')
        assert C.string_at(p,size)==elf_block(elf,address,size)
    n.fs_unload()
    beat=R.BEATS[0];base,spr0=R.image(beat)
    # Scalar-tail and QW alias layouts; preserved destination +58..+5F.
    for i in range(mode.pick(400,60)):
        ram=bytearray(base);spr=bytearray(spr0);dst=0x900000
        src=dst+rng.choice((-0x80,-0x30,-0x10,0,0x10,0x40,0x80))
        ram[dst-0x100:dst+0x100]=rng.randbytes(0x200)
        f=Fixture(ram,spr);e=oracle(elf,ram,spr)
        args=(dst,rng.choice((-1,0,1,2)),src);floats=tuple(rng.getrandbits(32) for _ in range(4))
        R.oracle_call(e,0x1CFAE0,args,floats)
        rc,_,fault=f.call(n,0x1CFAE0,args,floats);assert rc==0,(i,hex(fault))
        compare(f,e,('CFAE0',i,args));count+=1
    # Actual recorded flame locations and all three variants; run projection
    # and transform builder with existing original packet/chain owner bodies.
    for beat in mode.select(R.BEATS,4,0x1E3D90):
        base,spr0=R.image(beat)
        for node in R.pool(base,{0x1E3D90}):
            ram=bytearray(base);spr=bytearray(spr0)
            put(ram,0x811CC0+0x18,0x300000);ram[0x300000:0x310000]=bytes(0x10000)
            ram[0x7635C0:0x76B5C0]=bytes(0x8000)
            # Actual first initialization rebuilds matrices; poison bytes are
            # deliberately unrelated to the snapshot's scratch contents.
            spr[0x36E0:0x3760]=bytes([0xA5])*0x80
            e=oracle(elf,ram,spr);f=Fixture(ram,spr)
            for at in (0x700036A0,0x700036E0,0x70003720):
                e.call(0x102958,(at,node+0xD0))
                C.memmove(C.addressof(f.spr)+at-0x70000000,bytes(f.ram[node+0xD0:node+0x110]),64)
            # 3750 is the third matrix's translation after original copies.
            args=(0x70003750,0x30)
            want=R.oracle_call(e,0x1CD070,args);rc,c,fault=f.call(n,0x1CD070,args)
            assert rc==0 and c.v0&0xFFFFFFFF==want,(beat,hex(node),hex(fault))
            compare(f,e,('CD070',beat,node));count+=1
            if want==0xFFFFFF:continue
            variant=ram[node+0xD];assert variant in (0,1,2)
            pair=0x253CC0+variant*8
            floats=(u32(ram,pair),u32(ram,pair+4),R.F(320),R.F(320))
            result=R.oracle_call(e,0x1CD2B0,(),floats);rc,c,fault=f.call(n,0x1CD2B0,(),floats)
            assert rc==0 and c.f0==result,(beat,hex(node),hex(fault))
            compare(f,e,('CD2B0',beat,node));count+=1
            rows=(0x253CE0,0x253E90,0x254040)[variant]
            for layer in range(3):
                args=(0x900000,0,0x700036A0+layer*0x40);floats=(R.F(.2),R.F(.7),R.F(.8),R.F(.4))
                R.oracle_call(e,0x1CFAE0,args,floats);assert f.call(n,0x1CFAE0,args,floats)[0]==0
                n.fs_seed(f.ram,f.spr)
                args=(want,6 if layer==2 else 1,rows+0x90*layer,0x900000,0)
                R.oracle_call(e,0x1CFBE0,args)
                rc,_,fault=f.call(n,0x1CFBE0,args,packet=True);assert rc==0,(beat,hex(node),hex(fault))
                n.fs_publish(f.ram);compare(f,e,('CFBE0',beat,node,layer));count+=2
    # Run the actual placement's state-0 owner, using actual native SDK,
    # projection, builder and packet owners. RAND executes original code on
    # each side; sound and final draw publication are explicit boundaries.
    callers=0
    for beat in mode.select(R.BEATS,2,0x3D90):
        base,spr0=R.image(beat)
        for node in R.pool(base,{0x1E3D90}):
            ram=bytearray(base);spr=bytearray(spr0);ram[node+4]=0
            spr[0x36E0:0x3760]=bytes([0xA5])*0x80
            put(ram,0x811CC0+0x18,0x300000);ram[0x300000:0x310000]=bytes(0x10000)
            ram[0x7635C0:0x76B5C0]=bytes(0x8000)
            f=Fixture(ram,spr);e=oracle(elf,ram,spr);native=native_oracle(elf,f)
            external={0x1FC3C0,0x1B17A0}
            for fn in external:e.hooks[fn]=lambda ee:None
            R.oracle_call(e,0x1E3D90,(node,))
            failures=[]
            @Worker
            def worker(_,p):
                c=p.contents
                try:
                    if c.function in external:return 0
                    if c.function==0x122BB8:
                        R.oracle_call(native,c.function,());c.v0=native.r[2];return 0
                    if c.function in (0x102958,0x1029C0,0x102918):return n.fs_sdk(C.byref(f.host),p)
                    fault=C.c_uint32()
                    if c.function==0x1CFBE0:
                        n.fs_seed(f.ram,f.spr);rc=n.fs_packet(C.byref(f.host),p,C.byref(fault));n.fs_publish(f.ram)
                    else:rc=n.fs_call(C.byref(f.host),p,C.byref(fault))
                    assert rc==0,(hex(c.function),hex(fault.value));return 0
                except BaseException as ex:failures.append(ex);return -1
            f.host.worker=worker;fault=C.c_uint32()
            assert n.fs_sys(C.byref(f.host),node,C.byref(fault))==0,(beat,hex(node),hex(fault.value),failures)
            compare(f,e,('flame state0',beat,node));callers+=1
    # Existing GS/HUD owners through the new argument/stack forwarders.
    # These callees execute original instructions as explicit boundaries;
    # their native implementations are separately proved by their owners.
    forwarders=0
    specs={0x122BB8:(0,0),0x1C7900:(4,0),0x1C6120:(2,0),0x1D3990:(1,0),
           0x1CB760:(3,0),0x1CB5F0:(3,0),0x1CB6B0:(4,0),0x1CB950:(3,0)}
    for beat in mode.select(R.BEATS,4,0x1F4A10):
        base,spr0=R.image(beat)
        nodes=R.pool(base,{0x158BD0,0x158D30})[:1]
        hud=R.pool(base,{0x15A2C0})[:mode.pick(8,1)]
        cases=[(0x1F4A10,(node+0xD0,0x700038B0)) for node in nodes]
        cases += [(0x1E9E60,(node,struct.unpack_from('<H',base,node+0xE)[0])) for node in hud]
        for fn,args in cases:
            ram=bytearray(base);spr=bytearray(spr0)
            struct.pack_into('<4I',spr,0x38B0,0,128,0,128)
            put(ram,0x811CC0+0x1C,0x320000);ram[0x320000:0x330000]=bytes(0x10000)
            ram[0x7635C0:0x76B5C0]=bytes(0x8000)
            f=Fixture(ram,spr);e=oracle(elf,ram,spr);native=native_oracle(elf,f)
            expected=[];actual=[];failures=[]
            def original_hook(entry):
                def hook(ee):
                    na,nf=specs[entry]
                    if ee.r[29]==STACK_TOP-(0x90 if fn==0x1F4A10 else 0xC0):expected.append((entry,ee.r[29],tuple(ee.r[4+i]&((1<<64)-1) for i in range(na)),
                                     ee.read(ee.r[5],16) if entry==0x1C7900 else b''))
                    execute(ee,entry)
                return hook
            for entry in specs:e.hooks[entry]=original_hook(entry)
            R.oracle_call(e,fn,args)
            @Worker
            def worker(_,p):
                c=p.contents
                try:
                    actual.append((c.function,c.sp,tuple(c.a[:c.na]),
                                   bytes(f.stack[int(c.a[1])-0x7F000000:int(c.a[1])-0x7F000000+16])
                                   if c.function==0x1C7900 else b''))
                    na,nf=specs[c.function];assert (c.na,c.nf)==(na,nf)
                    native.r[29]=c.sp;native.r[31]=R.RETURN
                    for j in range(c.na):native.r[4+j]=c.a[j]
                    for j in range(c.nf):native.f[12+j]=c.f[j]
                    native.run(c.function)
                    c.v0=native.r[2]&((1<<64)-1);c.f0=native.f[0];return 0
                except BaseException as ex:failures.append(ex);return -1
            f.host.worker=worker
            rc,_,fault=f.call(n,fn,args)
            assert rc==0,(beat,hex(fn),hex(fault),failures)
            assert actual==expected,(beat,hex(fn),'forwarded boundary',actual,expected)
            compare(f,e,('GS/HUD forwarder',beat,fn,args));forwarders+=1
    # Same-value writes still request permission and stop at the refused
    # original access. Earlier stores must remain, including context failure.
    ram=bytearray(base);spr=bytearray(spr0);dst=0x900000;src=0x900100
    floats=(R.F(.2),R.F(.3),R.F(.4),R.F(.5));order=[0x44,0x4C,0x48,0x50,0x54]
    checks=0
    for deny in [(dst+o,4,1) for o in order]+[(0x275670,4,0),(dst+0x40,4,1),(src+16,16,0),(dst+32,16,1)]:
        f=Fixture(ram,spr);f.deny=deny
        rc,_,fault=f.call(n,0x1CFAE0,(dst,0,src),floats)
        assert rc<0 and fault==deny[0] and f.access[-1]==deny
        e=oracle(elf,ram,spr);load,save=e.load,e.save
        class Refused(Exception):pass
        def read(a,n=4):
            if a==deny[0] and 0==deny[2] and n<=deny[1]:raise Refused()
            return load(a,n)
        def write(a,v,n=4):
            if a==deny[0] and 1==deny[2] and n<=deny[1]:raise Refused()
            save(a,v,n)
        e.load=read;e.save=write
        try:R.oracle_call(e,0x1CFAE0,(dst,0,src),floats)
        except Refused:pass
        else:raise AssertionError(('original did not reach refusal',deny))
        compare(f,e,('original fault prefix',deny))
        checks+=1
    print(f'PASS {mode.MODE}: {count} original projection/transform/packet calls; {callers} complete state-0 flame bodies; {forwarders} GS/HUD forwarded bodies; {checks} original refusal prefixes; {len(BLOCKS)} exact resource windows and over-capacity refusal; exact disjoint flame matrices')
if __name__=='__main__':main()
