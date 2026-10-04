#!/usr/bin/env python3
"""Player/scene byte boundaries over their canonical storage. Tests original
00187EC0 and 001A8840 with its real fabs/sound callees on AREA01 snapshots;
synthetic mutations separately check ownership, lifetime and refusal.
"""
import ctypes as C
import hashlib
import random
import struct
import subprocess
from pathlib import Path
import reference_mode as RM
import area01_reference_view as AV
import test_area01_sys_reference as S
from test_player_fall_reference import FallEE, nested_bits
from test_actor_pool_reference import ELF_SHA256

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/level2/player-view'
PLAYER=0x8102B0;SIZE=0x320;OTHER=0x1100000


def main():
    OUT.mkdir(parents=True,exist_ok=True);path=OUT/'bridge.dylib'
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC','-Isrc',
                    'tests/area01_player_view_bridge.c','src/game/em_area01_player_view.c',
                    'src/game/em_area01_scene_view.c','src/game/em_area01_math_core.c',
                    'src/game/em_area01_math_player.c','-o',str(path)],cwd=ROOT,check=True)
    lib=C.CDLL(str(path))
    for name in ('pv_actor','pv_owners','pv_other'):getattr(lib,name).restype=C.c_void_p
    lib.pv_memory.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_int];lib.pv_memory.restype=C.c_void_p
    lib.sv_bytes.argtypes=[C.c_uint32,C.c_uint32];lib.sv_bytes.restype=C.c_void_p
    lib.pv_reset.argtypes=[C.c_void_p]
    elf=(ROOT.parent/'Extermination/config/SCUS_971.12').read_bytes();assert hashlib.sha256(elf).hexdigest()==ELF_SHA256
    native=S.build_native();callback=AV.VIEW(lib.pv_memory)
    helper=FallEE(elf);calls=[]
    def worker(ctx,ptr):
        c=ptr.contents;calls.append(c.fn)
        assert lib.pv_commit()==0
        if c.fn==0x11DF78:
            _,c.f0=nested_bits(helper,c.fn,fregs=[c.f[0]])
        elif c.fn==0x187EC0:
            assert lib.pv_begin()==0 and lib.pv_sound(c.a[0],c.a[1])==0 and lib.pv_commit()==0
        else:raise AssertionError(hex(c.fn))
        assert lib.pv_begin()==0
        return 0
    worker_cb=S.WORKER(worker)
    rng=random.Random(0xA015);case_count=0
    for beat in S.BEATS:
        ram,spad=S.image(beat)
        for trial in range(RM.pick(16,1)):
            raw=bytearray(ram[PLAYER:PLAYER+SIZE])
            # Original 0015BCF0's homogeneous outputs; external canonical
            # owners intentionally differ from the raw in-stage fixture later.
            struct.pack_into('<I',raw,0xAC,0x3F800000);struct.pack_into('<I',raw,0xBC,0x3F800000)
            raw[0]=1;raw[0x234]=0
            lib.pv_reset(bytes(raw))
            other=bytearray(0x400);other[0xB]=1;other[0x56]=trial&255
            struct.pack_into('<I',other,0x30,OTHER+0x300)
            position=struct.unpack_from('<3f',raw,0xA0)
            for k,value in enumerate(position):struct.pack_into('<f',other,0xB0+4*k,value+(20 if trial%3==1 else 0))
            struct.pack_into('<3f',other,0x300,2,2,2)
            C.memmove(lib.pv_other(),bytes(other),len(other))
            o=FallEE(elf);o.mem[:]=ram;o.spad[:]=spad
            o.mem[PLAYER:PLAYER+SIZE]=raw;o.mem[OTHER:OTHER+len(other)]=other;o.mem[0x810707]=0
            o.write(0x70003B86,struct.pack('<H',0x1234));o.call(0x1A8840,(PLAYER,OTHER))
            state=S.Sys(None,0,worker_cb,None,0x7F080000,0,0,0,C.cast(callback,C.c_void_p).value)
            calls.clear();assert lib.pv_begin()==0
            assert native.em_area01_sys_001A8840(C.byref(state),PLAYER,OTHER)==0,(beat,trial,state.fault)
            assert lib.pv_commit()==0 and lib.pv_begin()==0
            actual=C.string_at(lib.pv_actor(),SIZE)
            assert actual==bytes(o.mem[PLAYER:PLAYER+SIZE]),(beat,trial,next((hex(i),a,b) for i,(a,b) in enumerate(zip(actual,o.mem[PLAYER:PLAYER+SIZE])) if a!=b))
            assert C.string_at(lib.pv_other(),len(other))==bytes(o.mem[OTHER:OTHER+len(other)])
            assert lib.pv_span()==int.from_bytes(o.read(0x70003B86,2),'little')
            assert lib.pv_commit()==0
            # Real byte-store leaf through the same sparse provider.
            a,b=rng.getrandbits(32),rng.getrandbits(32);o.call(0x187EC0,(a,b))
            assert lib.pv_begin()==0 and lib.pv_sound(a,b)==0
            assert C.string_at(lib.pv_actor(),SIZE)==bytes(o.mem[PLAYER:PLAYER+SIZE])
            assert lib.pv_commit()==0;case_count+=1
    raw=bytes((i*73+17)&255 for i in range(SIZE))
    def reset():lib.pv_reset(raw)
    def view(at,n=4,write=1):return lib.pv_memory(None,PLAYER+at,n,write)
    # Saved in-stage representation is restored; legitimate ordinary bytes
    # and high bits of +235 survive, external owners receive changed fields.
    reset();saved=C.string_at(lib.pv_actor(),SIZE);assert lib.pv_begin()==0
    assert view(0x9C,8,1)==lib.pv_actor()+0x9C
    C.memmove(view(0x9C,8),b'abcdefgh',8)
    C.memmove(view(0xC4),struct.pack('<I',0x3F000000),4)
    C.memmove(view(0x220,16),struct.pack('<4I',1,2,3,4),16)
    C.memmove(view(0x20E,2),b'\x34\x12',2)
    C.memmove(view(0x234,2),b'\x02\xA5',2)
    assert lib.pv_commit()==0
    after=C.string_at(lib.pv_actor(),SIZE);assert after[0x9C:0xA0]==b'abcd'
    assert after[0xA0:0xC0]==saved[0xA0:0xC0] and after[0x235]&0xFE==0xA4
    assert lib.pv_begin()==0
    assert C.string_at(view(0xA0,4,0),4)==b'efgh'
    assert C.string_at(view(0x220,16,0),16)==struct.pack('<4I',1,2,3,4)
    assert C.string_at(view(0x234,2,0),2)==b'\x02\xA5'
    assert lib.pv_commit()==0
    faults=0
    for at in (9,0xC,0x10,0x14,0x20,0x40,0x44,0x4C,0xAC,0xB0,0xD0,0x110,0x214,0x308):
        reset();assert lib.pv_begin()==0;assert not view(at,1,1) and lib.pv_fault()==3;faults+=1
        reset();assert lib.pv_begin()==0;p=view(at,1,0);C.c_uint8.from_address(p).value^=1
        assert lib.pv_commit()==-1 and lib.pv_fault()==3;faults+=1
    for kind in range(6):
        reset();assert lib.pv_begin()==0;lib.pv_stale(kind)
        assert lib.pv_commit()==-1 and lib.pv_fault()==2;faults+=1
    for at,n in ((0,0),(SIZE-1,2),(0,0xFFFFFFFF)):
        reset();assert lib.pv_begin()==0;assert not view(at,n,0) and lib.pv_fault()==1;faults+=1
    reset();assert not view(0,4,0);faults+=1
    reset();assert lib.pv_begin()==0 and lib.pv_begin()==-1;faults+=1
    # In-stage borrowing must read the actual intermediate record, even
    # when external placement/hip owners intentionally disagree. No
    # snapshot, publication, homogeneous rewrite or restoration is allowed.
    reset();lib.pv_stale(1);lib.pv_stale(2)
    owners_before=C.string_at(lib.pv_owners(),lib.pv_owner_size())
    assert lib.pv_borrow_begin()==0
    assert view(0,SIZE,0)==lib.pv_actor()
    assert C.string_at(view(0,SIZE,0),SIZE)==raw
    assert lib.pv_commit()==0 and C.string_at(lib.pv_actor(),SIZE)==raw
    assert C.string_at(lib.pv_owners(),lib.pv_owner_size())==owners_before
    # A legitimate native change BETWEEN borrows is visible on re-entry.
    C.c_uint32.from_address(lib.pv_actor()+0xA0).value=0x43210000
    assert lib.pv_borrow_begin()==0
    assert C.string_at(view(0xA0,4,0),4)==struct.pack('<I',0x43210000)
    assert lib.pv_commit()==0
    for at,n in ((0,1),(0xA0,12),(0x220,16),(SIZE-1,1)):
        reset();assert lib.pv_borrow_begin()==0
        assert not view(at,n,1) and lib.pv_fault()==3;faults+=1
    reset();assert lib.pv_borrow_begin()==0
    C.c_uint8.from_address(view(0xA0,1,0)).value^=1
    assert lib.pv_commit()==-1 and lib.pv_fault()==3;faults+=1
    reset();assert lib.pv_borrow_begin()==0;lib.pv_stale(4)
    assert lib.pv_commit()==-1 and lib.pv_fault()==2;faults+=1
    reset();assert lib.pv_borrow_begin()==0 and lib.pv_begin()==-1;faults+=1
    reset();assert lib.pv_begin()==0 and lib.pv_borrow_begin()==-1;faults+=1
    # Canonical scene pointer aliases and exact ownership boundaries.
    reset();lib.sv_native()
    assert C.string_at(lib.sv_bytes(0x70003B68,4),4)==b'\x78\x56\x34\x12'
    assert C.string_at(lib.sv_bytes(0x810E70,2),2)==b'\xDC\xFE'
    C.c_uint8.from_address(lib.sv_bytes(0x810701,1)).value=5
    C.c_uint8.from_address(lib.sv_bytes(0x70003B8D,1)).value=3
    assert lib.sv_check()==1
    for a,n in ((0x810700,4),(0x81072F,2),(0x81074F,2),(0x810750,5),(0x810E70,3),
                (0x8101E4,1),(0x810811,1),(0x70003B84,4),(0xFFFFFFFF,2),(0x810700,0)):
        assert not lib.sv_bytes(a,n),(hex(a),n)
    p=lib.sv_bytes(0x8106B0,0x48);assert p and lib.sv_bytes(0x8106C8,4)==p+0x18
    print(f'area01 player/scene view: PASS ({RM.MODE}: {case_count} captured original contact + sound cases; {faults} refusal/lifetime cases; alias/owner restoration checks)')


if __name__=='__main__':main()
