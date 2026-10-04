#!/usr/bin/env python3
"""AREA01 camera timelines against original instructions and delivered tracks.

Only published camera, fade/cue and render-context services are explicit
boundaries. The original sampler, tangent and rotation execute unmodified.
"""
import ctypes as C
import struct
import subprocess
import time
import export_area01_common as A
from reference_mode import FULL,banner
from test_area01_runtime_reference import Host,Call,View,Worker
from test_player_fall_reference import FallEE
from test_player_slide_reference import read_elf,bits,STACK_TOP

CAM=0x8101E0
SERVICES={0x21BAB0:(0,0),0x1AEDE0:(2,0),0x1AEE10:(2,0),0x1B1E20:(2,0),
          0x1B0250:(0,0),0x21B9A0:(1,2),0x1D2830:(2,0),0x1DD980:(2,0),0x1D25F0:(0,1)}
SPANS=((CAM,0x90),(0x8105D0,0x30),(0x8106F3,1),(0x8234C0,32),
       (0x275BFC,4),(0x275C98,8),(0x70003400,64),(0x70003600,16))
STAMP=0xFEDCBA9876543210

def main():
    start=time.monotonic();out=A.ROOT/'build/level2/timeline';out.mkdir(parents=True,exist_ok=True)
    lib=out/'timeline.dylib'
    sources=['em_area01_timeline','em_cinematic_playback','em_cinematic_camera','em_camera_rotation',
             'em_owner_services_original','em_effect_original','em_stream_lanes_original']
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off','-shared','-fPIC','-Isrc',
                    *['src/game/'+s+'.c' for s in sources],'-lm','-o',str(lib)],cwd=A.ROOT,check=True)
    n=C.CDLL(str(lib));n.em_area01_timeline_call.argtypes=[C.POINTER(Host),C.POINTER(Call),C.POINTER(C.c_uint32)]
    elf=read_elf();read=A.static_reader(elf,A.read_overlay());caps=A.captures()
    caps=caps if FULL else [caps[0],caps[2],caps[-1]]
    total=boundaries=0;visited=set()
    for ci,cap in enumerate(caps):
      for scene,row,clip in ((2,0x96,2),(35,0x98,0)):
        o=FallEE(elf);o.mem[:]=cap.ram;o.spad[:]=cap.spad
        for a,size in ((0x26AAE0,32),(0x26AC80,32),(0x26C598,52)):o.write(a,read(a,size))
        bank=o.load(0x28A490+4*row);track=bank+(o.load(bank+4+4*clip)&~3)
        duration=struct.unpack('<f',o.read(track,4))[0]
        assert 1<=duration<65536 and duration==int(duration)
        o.save(CAM+0x6E,scene,2);o.save(CAM+0x70,track);o.save(CAM+0x78,bits(duration))
        ram=(C.c_uint8*len(o.mem)).from_buffer_copy(o.mem);sp=(C.c_uint8*len(o.spad)).from_buffer_copy(o.spad)
        @View
        def view(_,a,size,write):
            if a+size<=len(ram):return C.addressof(ram)+a
            if 0x70000000<=a and a+size<=0x70004000:return C.addressof(sp)+a-0x70000000
            return None
        def current():
            return tuple(bytes(ram[a:a+size]) if a<0x70000000 else bytes(sp[a-0x70000000:a-0x70000000+size]) for a,size in SPANS)
        def snap():return tuple(o.read(a,size) for a,size in SPANS)
        expected=[];position=0;errors=[]
        def hook(fn):
            def call(o):
                na,nf=SERVICES[fn]
                expected.append((fn,tuple(o.r[4+i]&0xFFFFFFFF for i in range(na)),tuple(o.f[12:12+nf]),o.r[29]&0xFFFFFFFF,snap()))
                if fn==0x21BAB0:o.r[2]=STAMP
            return call
        o.hooks={fn:hook(fn) for fn in SERVICES}
        @Worker
        def worker(_,ptr):
            nonlocal position
            c=ptr.contents
            try:
                actual=(c.function,tuple(c.a[i]&0xFFFFFFFF for i in range(c.na)),tuple(c.f[:c.nf]),c.sp,current())
                assert position<len(expected),(cap.name,scene,'extra call',actual[:4])
                want=expected[position]
                assert actual[:4]==want[:4],(cap.name,scene,t,position,actual[:4],want[:4])
                assert actual[4]==want[4],(cap.name,scene,t,position,hex(c.function),'boundary bytes',
                    [(hex(SPANS[i][0]),x.hex(),y.hex()) for i,(x,y) in enumerate(zip(actual[4],want[4])) if x!=y])
                position+=1;visited.add(c.function)
                if c.function==0x21BAB0:c.v0=STAMP
                return 0
            except Exception as e:errors.append(e);return -1
        host=Host(None,view,worker)
        if FULL and ci==0:times=[i*.5 for i in range(int(duration*2)+1)]
        else:
            # All event timestamps, the scene35 cue, track cuts, endpoint,
            # and uniformly spaced original camera samples in every capture.
            points={0.,.5,1.,49.5,50.,50.5,duration-.5,duration,duration+1}
            points.update(i*duration/48 for i in range(49))
            for a,size in ((0x26AAE0,32),(0x26AC80,32)):
                for value in struct.unpack('<'+'f'*(size//4),read(a,size)):
                    if 0<value<duration:points.update((max(0,value-.5),value,value+.5))
            for i in range(int(duration)):
                if struct.unpack_from('<f',cap.ram,track+16+32*i+28)[0]<0:points.update((float(i),i+.5))
            times=sorted(points)
        sequence=[(0x22EC30,0.)]+[(0x22EEF0,t) for t in times]
        for fn,t in sequence:
            o.save(CAM+0x74,bits(t));struct.pack_into('<I',ram,CAM+0x74,bits(t))
            expected.clear();position=0;errors.clear()
            o.call(fn,(CAM,));want=snap()
            call=Call(function=fn,sp=STACK_TOP,na=1);call.a[0]=CAM;fault=C.c_uint32()
            rc=n.em_area01_timeline_call(C.byref(host),C.byref(call),C.byref(fault))
            if errors:raise errors[0]
            assert rc==0 and position==len(expected),(cap.name,scene,t,rc,hex(fault.value),position,len(expected))
            assert current()==want,(cap.name,scene,t,'final bytes',
                [(hex(SPANS[i][0]),x.hex(),y.hex()) for i,(x,y) in enumerate(zip(current(),want)) if x!=y])
            total+=1;boundaries+=position
        print(cap.name,'scene',scene,len(sequence),'exact ticks',flush=True)
    assert visited==set(SERVICES),(visited,set(SERVICES)-visited)
    @Worker
    def reject(_,ptr):return -1
    rejected_host=Host(None,view,reject)
    def rejected(fn,want):
        call=Call(function=fn,sp=STACK_TOP,na=1);call.a[0]=CAM;fault=C.c_uint32()
        assert n.em_area01_timeline_call(C.byref(rejected_host),C.byref(call),C.byref(fault))<0
        assert fault.value==want,(hex(fault.value),hex(want))
    struct.pack_into('<h',ram,CAM+0x6E,1);rejected(0x22EC30,CAM+0x6E)
    struct.pack_into('<h',ram,CAM+0x6E,35);rejected(0x22EC30,0x21BAB0)
    struct.pack_into('<I',ram,CAM+0x74,bits(0.))
    struct.pack_into('<I',ram,CAM+0x7C,0x26AC10);rejected(0x22EEF0,CAM+0x7C)
    struct.pack_into('<I',ram,CAM+0x7C,0)
    struct.pack_into('<I',ram,CAM+0x80,0x26AC70);rejected(0x22EEF0,0x26AC70)
    struct.pack_into('<I',ram,CAM+0x80,0)
    struct.pack_into('<I',ram,CAM+0x84,0x26AAC0);rejected(0x22EEF0,0x26AAC0)
    struct.pack_into('<I',ram,CAM+0x84,0)
    struct.pack_into('<I',ram,CAM+0x78,bits(duration+1));rejected(0x22EEF0,track)
    banner(f'{total} AREA01 timeline ticks',f'{boundaries} exact service boundaries','6 fail-stop checks',f'{time.monotonic()-start:.1f}s')
if __name__=='__main__':main()
