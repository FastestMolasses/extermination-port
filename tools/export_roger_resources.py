#!/usr/bin/env python3
"""Export original AREA11 Roger metadata, idle bank and mesh into ignored assets.

All channel values, scripts and textures come from the user's original inputs.
No animation interpolation is baked; the one mesh palette is its initial pose.
"""
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
DECOMP=ROOT.parent/'Extermination'


def main():
    sys.path.insert(0,str(DECOMP/'tools'))
    from export_opening_actors import OpeningClip,exact_mesh_sections
    import export_native as native
    data=(DECOMP/'extract/chunk15/f12_id44.bin').read_bytes()
    model_source=(DECOMP/'extract/chunk15/f18_id94.bin').read_bytes()
    overlay=(DECOMP/'extract/OVERLAY/AREA11.BIN').read_bytes()
    ram=(DECOMP/'build/startup-reference/playable_ee.bin').read_bytes()
    actor=0x7A8830;bank=0x10E000
    runtime_bank=struct.unpack_from('<I',ram,actor+0x40)[0]
    assert runtime_bank==struct.unpack_from('<I',ram,0x28a490+0x4a*4)[0]
    assert data[bank:bank+64]==ram[runtime_bank:runtime_bank+64]
    assert struct.unpack_from('<I',data,bank)[0]==9
    model_at=0x35000;size=struct.unpack_from('<I',model_source,model_at+12)[0]
    raw=model_source[model_at:model_at+size]
    model_pointer=struct.unpack_from('<I',ram,actor+0x44)[0]
    assert raw==ram[model_pointer:model_pointer+size]
    clips=[];rows=[];parents=None;initial=None
    for cid in range(9):
        header=bank+struct.unpack_from('<I',data,bank+4+4*cid)[0]
        clip=OpeningClip(data,header)
        bones,length,next_clip,blend=struct.unpack_from('<HHhh',data,header)
        assert bones==21 and next_clip==-1 and blend==0
        assert struct.unpack_from('<I',data,header+20)[0]==0
        if parents is None:parents=clip.parents
        assert parents==clip.parents
        if cid==8:initial=clip.palette()
        payload=bytearray(struct.pack('<HHhH',cid,length,next_clip,blend));keys=0
        for bone in range(bones):
            for channel in (clip.rotation[bone],clip.translation[bone],clip.scale[bone]):
                payload+=struct.pack('<I',len(channel.keys));keys+=len(channel.keys)
                for time,values,hold in channel.keys:
                    payload+=struct.pack('<HH4f',time,int(hold),*values,*((0.,)*(4-len(values))))
        clips.append(payload);rows.append(dict(id=cid,duration=length,next=next_clip,
                                               source_header=header,keys=keys))
    out=ROOT/'assets/scene_snow/roger';out.mkdir(parents=True,exist_ok=True)
    channels=struct.pack('<4sIII21i',b'EMPC',1,21,9,*parents)+b''.join(clips)
    (out/'channels.empc').write_bytes(channels)
    base=0x8283D0;length=0x800
    scripts=struct.pack('<4s4I',b'EMSC',1,base,base,length)+overlay[base-0x823500:base-0x823500+length]
    (out/'programs.emsc').write_bytes(scripts)
    polygon=overlay[0x82ab80-0x823500:0x82abc0-0x823500]
    (out/'trigger.empg').write_bytes(struct.pack('<4s3I',b'EMPG',1,0,4)+polygon)
    sections,textures=exact_mesh_sections(raw)
    gs=DECOMP/'build/startup-reference/opening_gs.bin'
    entries,texels=native.build_texture_blob(None,textures,p2s=gs)
    native.write_emdl(out/'roger.emdl',sections,[],parents,[initial],60.,entries,texels,
                      clips=[dict(id=8,first=0,count=1,fps=60.)])
    report=dict(model_source='chunk15/f18_id94.bin',model_offset=model_at,model_bytes=size,
        model_sha256=hashlib.sha256(raw).hexdigest(),runtime_model=f'{model_pointer:08X}',
        bank_source='chunk15/f12_id44.bin',bank_offset=bank,runtime_bank=f'{runtime_bank:08X}',
        clips=rows,trigger_source='0082AB80',trigger=struct.unpack('<16f',polygon),
        gs_sha256=hashlib.sha256(gs.read_bytes()).hexdigest(),
        files={str(p.relative_to(ROOT)):dict(size=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
               for p in sorted(out.iterdir())},
        boundaries=['face morph/blink state','owner placement rounding',
                    'bank96 encounter streams and scripted pose binding'])
    receipt=ROOT/'build/roger_reference';receipt.mkdir(parents=True,exist_ok=True)
    (receipt/'export.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Exported original Roger mesh, nine raw clips, four programs and polygon.')


if __name__=='__main__':main()
