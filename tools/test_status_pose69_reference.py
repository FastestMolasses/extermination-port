#!/usr/bin/env python3
"""The typed status C69A0 bridge against the complete original routine.

The fixture obtains its preblended channel through the existing native
nlerp owner. The oracle executes every original instruction and callee.
Both start with the same user-owned capture bytes; no math is scripted.
"""
import ctypes as C
import json
from pathlib import Path
import struct
import subprocess
import time
import reference_mode as mode
from test_coll_move_reference import FloatEE
from test_module_loader_reference import read_elf_bytes
from test_pose_host_workers_reference import SOURCES as POSE_SOURCES

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/level2/status-pose69'
def word(b,a):return struct.unpack_from('<I',b,a)[0]
def main():
    start=time.monotonic();OUT.mkdir(parents=True,exist_ok=True)
    target=OUT/'bridge.dylib'
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off',
        '-shared','-fPIC','-Isrc','-Wl,-dead_strip','-Wl,-exported_symbol,_sp69_*',
        'tests/status_pose69_bridge.c','src/game/em_area01_math_core.c',
        'src/game/em_area01_math_actor.c',*POSE_SOURCES,'-o',str(target)],cwd=ROOT,check=True)
    lib=C.CDLL(str(target));lib.sp69_call.argtypes=[C.c_void_p,C.c_void_p,C.c_uint32,C.c_int]
    elf=read_elf_bytes();ref=ROOT.parent/'Extermination/build/startup-reference/status-hub'
    ram=(ref/'eeMemory.bin').read_bytes();spr=(ref/'scratchpad.bin').read_bytes();actor=0x28B020
    bones=[word(ram,actor+0x110+4*i) for i in range(ram[actor+12])]
    shapes=['capture','zero','one','scales','saturation','self','forward']
    shapes += [(sign,t) for sign in (-1,1) for t in (-1.,0.,.5,1.,2.)]
    if mode.FULL:shapes += [('sweep',i) for i in range(128)]
    cases=compatibility=0
    for shape in shapes:
        b=bytearray(ram)
        if shape=='zero':b[actor+12]=0
        elif shape=='one':b[actor+12]=1
        elif shape=='self':struct.pack_into('<h',b,bones[-1]+0x64,len(bones)-1)
        elif shape=='forward':struct.pack_into('<h',b,bones[0]+0x64,1)
        elif shape=='scales':
            struct.pack_into('<3f',b,actor+0x60,1.25,-.75,.5)
            for i,at in enumerate(bones):
                struct.pack_into('<3f',b,at+0x18,.75,2.,1.25)
                struct.pack_into('<3f',b,at+0x70,.5,-1.25,2.5)
                struct.pack_into('<3f',b,at+0x7C,i+.25,-i-1.5,3.75)
                struct.pack_into('<3h',b,at+0x88,-32768,4096,32767)
        elif shape=='saturation':
            struct.pack_into('<3I',b,actor+0x60,0x7F800000,0xFF7FFFFF,0x7FC00001)
            for at in bones:
                struct.pack_into('<3I',b,at+0x7C,0x7F800000,0xFFC00001,0xFF7FFFFF)
        elif isinstance(shape,tuple):
            sign,t=shape
            if sign=='sweep':
                sign=(-1,1)[t&1];t=(t-32)/16
            for i,at in enumerate(bones):
                struct.pack_into('<4f',b,at+0x30,.25,-.5,.75,1.)
                struct.pack_into('<4f',b,at+0x40,sign*.5,sign*-.25,sign*.625,sign*1.25)
                struct.pack_into('<f',b,at+0x50,t)
        native=C.create_string_buffer(bytes(b));scratch=C.create_string_buffer(spr)
        o=FloatEE(elf,bytes(b),spr);o.call(0x1C69A0,(actor,))
        assert lib.sp69_call(native,scratch,actor,0)==0,shape
        assert native.raw[:-1]==bytes(o.mem),(shape,'RAM',next((hex(i),x,y) for i,(x,y) in enumerate(zip(native.raw[:-1],o.mem)) if x!=y))
        assert scratch.raw[:-1]==bytes(o.spad),(shape,'scratch',next((hex(i),x,y) for i,(x,y) in enumerate(zip(scratch.raw[:-1],o.spad)) if x!=y))
        cases+=1
        # The older public convenience API defines no input world matrices.
        # Its forward/self-parent starting matrices remain zero as before.
        if shape in ('capture','self','forward','zero'):
            for at in bones:b[at+0x90:at+0xD0]=bytes(64)
            native=C.create_string_buffer(bytes(b));scratch=C.create_string_buffer(spr)
            o=FloatEE(elf,bytes(b),spr);o.call(0x1C69A0,(actor,))
            assert lib.sp69_call(native,scratch,actor,1)==0
            assert native.raw[:-1]==bytes(o.mem),(shape,'public helper')
            assert scratch.raw[:-1]==spr,'public helper changed caller scratch'
            compatibility+=1
    assert lib.sp69_contract()==0,'public helper failure changed output'
    report=dict(status='PASS',mode='full' if mode.FULL else 'quick',original_cases=cases,
                compatibility_cases=compatibility,failure_contracts=3,seconds=round(time.monotonic()-start,2))
    (OUT/('full.json' if mode.FULL else 'quick.json')).write_text(json.dumps(report,indent=2)+'\n')
    mode.banner(f'{cases} original C69A0 typed-status cases')
    print('status C69A0:',json.dumps(report,sort_keys=True))
if __name__=='__main__':main()
