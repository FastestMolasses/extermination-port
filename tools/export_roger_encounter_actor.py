#!/usr/bin/env python3
"""Export the original bank96/Roger2 raw channels without root stripping."""
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
DECOMP=ROOT.parent/'Extermination'

def main():
    sys.path.insert(0,str(DECOMP/'tools'))
    from export_opening_actors import OpeningClip
    data=(DECOMP/'extract/chunk15/f12_id44.bin').read_bytes()
    ram=(DECOMP/'build/startup-reference/opening_ee.bin').read_bytes()
    base=0x41000
    pointer=struct.unpack_from('<I',ram,0x28A490+4*0x96)[0]
    assert pointer==0x1434F40 and data[base:base+16]==ram[pointer:pointer+16]
    assert struct.unpack_from('<I',data,base)[0]==3
    header=base+struct.unpack_from('<I',data,base+12)[0]
    assert header==0x555E0
    assert struct.unpack_from('<HHhh',data,header)==(21,691,-2,0)
    assert struct.unpack_from('<I',data,header+20)[0]==0
    clip=OpeningClip(data,header)
    payload=bytearray(struct.pack('<4sIII21iHHhH',b'EMPC',1,21,1,*clip.parents,2,691,-2,0))
    keys=0
    for bone in range(21):
        for channel in (clip.rotation[bone],clip.translation[bone],clip.scale[bone]):
            payload+=struct.pack('<I',len(channel.keys));keys+=len(channel.keys)
            for time,values,hold in channel.keys:
                payload+=struct.pack('<HH4f',time,int(hold),*values,*((0.,)*(4-len(values))))
    path=ROOT/'assets/scene_snow/roger/encounter_roger.empc'
    path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(payload)
    report=dict(source='chunk15/f12_id44.bin',bank='96',bank_offset=base,
        runtime_bank=f'{pointer:08X}',clip=2,header=header,duration=691,keys=keys,
        bytes=len(payload),sha256=hashlib.sha256(payload).hexdigest(),
        boundaries=['No encounter live node snapshot yet','Original owner transform is separately zeroed by op01/sub10'])
    out=ROOT/'build/roger_reference';out.mkdir(parents=True,exist_ok=True)
    (out/'encounter_actor_export.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'Exported bank96 Roger2: {keys} raw keys, root channels retained.')

if __name__=='__main__':main()
