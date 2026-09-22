#!/usr/bin/env python3
"""Compare native BATTERY decor submissions to captured original GIF packets.

The comparison uses the original confirmation framebuffer's EE packet
buffers, not hand-authored expected positions or another copy of C layout.
Text metrics/background animation are outside this specific draw oracle.
"""
from pathlib import Path
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def main():
    output=ROOT/'build/battery_reference';output.mkdir(parents=True,exist_ok=True)
    binary=output/'battery_ui_test'
    subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
        '-fsanitize=address,undefined','-fno-omit-frame-pointer','-Isrc',
        'tests/battery_ui_test.c','src/game/em_battery_ui.c','src/game/em_panel.c',
        '-lm','-o',str(binary)],cwd=ROOT,check=True)
    asset=ROOT/'assets/scene_snow/panel/battery.emba'
    result=subprocess.run([str(binary),str(asset)],capture_output=True,text=True,check=True)
    d=asset.read_bytes();entries=[]
    for i in range(struct.unpack_from('<I',d,16)[0]):
        _,u,v,w,h,_,token=struct.unpack_from('<6IQ',d,32+i*32)
        entries.append((u,v,u+w,v+h,token))
    ram=(ROOT.parent/'Extermination/build/startup-reference/panel/eeMemory.bin').read_bytes()
    found=set();p=0;tag=struct.pack('<Q',0xA400000000008001)
    def xy(q):return q&65535,q>>16&65535
    while (p:=ram.find(tag,p))>=0:
        if p%16==0 and struct.unpack_from('<Q',ram,p+8)[0]==0x8413413680:
            token=struct.unpack_from('<Q',ram,p+32)[0]
            color=struct.unpack_from('<I',ram,p+48)[0]
            a,b=xy(struct.unpack_from('<Q',ram,p+56)[0]),xy(struct.unpack_from('<Q',ram,p+80)[0])
            found.add((token,color,min(a[0],b[0]),min(a[1],b[1]),max(a[0],b[0]),max(a[1],b[1])))
        p+=8
    p=0;rects=set();tag=struct.pack('<Q',0x4400000000008001)
    while (p:=ram.find(tag,p))>=0:
        if p%16==0 and struct.unpack_from('<Q',ram,p+8)[0]==0x4410:
            color=struct.unpack_from('<I',ram,p+24)[0]
            a,b=xy(struct.unpack_from('<Q',ram,p+32)[0]),xy(struct.unpack_from('<Q',ram,p+40)[0])
            rects.add((0,color,*a,*b))
        p+=8
    count=0
    for line in result.stdout.splitlines():
        if not line.startswith('Q '):continue
        x,y,w,h,u,v,u1,v1,*rgba=map(float,line.split()[1:])
        tokens=[e[4] for e in entries if e[:4]==(u,v,u1,v1)]
        assert len(tokens)==1
        color=sum(round(c*128)<<(i*8) for i,c in enumerate(rgba))
        key=(tokens[0],color,round((x+1792)*16),round((y/2+1936)*16),
             round((x+w+1792)*16),round(((y+h)/2+1936)*16))
        assert key in (found if tokens[0] else rects),('Native quad not present in original packet buffers',key)
        count+=1
    assert count==29
    print(f'Original BATTERY GS packets: all {count} native decor/cursor quads match TEX0, RGBA and XY exactly; ASan/UBSan page flow PASS')


if __name__=='__main__':main()
