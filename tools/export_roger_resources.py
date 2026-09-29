#!/usr/bin/env python3
"""Export original AREA11 Roger metadata, idle bank and mesh into ignored assets.

All channel values, scripts and textures come from the user's own disc
files: the bank and the model from the extract, the programs and the
trigger polygon from the AREA11 overlay. The texels come from the first
level's GS memory rebuilt from the disc (tools/export_disc_textures_gs.py
FirstLevel.world(); docs/DISC_TEXTURES.md), or from --gs FILE (a GS freeze
blob); every texture must read only GS blocks a disc upload writes. No
PCSX2 capture is needed.

With --verify-ram FILE (optional; default the first-control capture
../Extermination/build/startup-reference/playable_ee.bin when it exists,
--no-verify skips it) the captured Roger record 0x7A8830 is checked: its
+0x40 bank is D_0028A490[0x4A] and holds the bank's bytes, its +0x44 model
holds the model's bytes.

No animation interpolation is baked; the one mesh palette is its initial pose.

Usage (port root): python3 tools/export_roger_resources.py [--iso FILE | --disc DIR] [--gs FILE]
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
DECOMP=ROOT.parent/'Extermination'
ACTOR, BANK, MODEL_AT = 0x7A8830, 0x10E000, 0x35000


def verify_ram(ram, data, raw):
    """The captured Roger record's bank and model (see the docstring)."""
    runtime_bank=struct.unpack_from('<I',ram,ACTOR+0x40)[0]
    assert runtime_bank==struct.unpack_from('<I',ram,0x28a490+0x4a*4)[0]
    assert data[BANK:BANK+64]==ram[runtime_bank:runtime_bank+64]
    model_pointer=struct.unpack_from('<I',ram,ACTOR+0x44)[0]
    assert raw==ram[model_pointer:model_pointer+len(raw)]
    return dict(runtime_bank=f'{runtime_bank:08X}',runtime_model=f'{model_pointer:08X}')


def main():
    ap=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--iso',type=Path,help='the disc image (default ../Extermination/Extermination-rebuilt.iso)')
    ap.add_argument('--disc',type=Path,help='a mounted disc or a copy of its DATA/ directory')
    ap.add_argument('--gs',type=Path,help='a GS freeze blob to take the texels from instead of the disc')
    ap.add_argument('--verify-ram',type=Path,help='optional: a captured AREA11 EE RAM image')
    ap.add_argument('--no-verify',action='store_true')
    ap.add_argument('--out',type=Path,default=ROOT/'assets/scene_snow/roger')
    args=ap.parse_args()
    sys.path.insert(0,str(DECOMP/'tools'))
    sys.path.insert(0,str(ROOT/'tools'))
    from export_opening_actors import OpeningClip,exact_mesh_sections
    import export_native as native
    import export_disc_textures_gs as G
    data=(DECOMP/'extract/chunk15/f12_id44.bin').read_bytes()
    model_source=(DECOMP/'extract/chunk15/f18_id94.bin').read_bytes()
    overlay=(DECOMP/'extract/OVERLAY/AREA11.BIN').read_bytes()
    assert struct.unpack_from('<I',data,BANK)[0]==9
    size=struct.unpack_from('<I',model_source,MODEL_AT+12)[0]
    raw=model_source[MODEL_AT:MODEL_AT+size]
    clips=[];rows=[];parents=None;initial=None
    for cid in range(9):
        header=BANK+struct.unpack_from('<I',data,BANK+4+4*cid)[0]
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
    out=args.out;out.mkdir(parents=True,exist_ok=True)
    channels=struct.pack('<4sIII21i',b'EMPC',1,21,9,*parents)+b''.join(clips)
    (out/'channels.empc').write_bytes(channels)
    base=0x8283D0;length=0x800
    scripts=struct.pack('<4s4I',b'EMSC',1,base,base,length)+overlay[base-0x823500:base-0x823500+length]
    (out/'programs.emsc').write_bytes(scripts)
    polygon=overlay[0x82ab80-0x823500:0x82abc0-0x823500]
    (out/'trigger.empg').write_bytes(struct.pack('<4s3I',b'EMPG',1,0,4)+polygon)
    sections,textures=exact_mesh_sections(raw)
    if args.gs:
        gs=args.gs
    else:
        world=G.first_level_world(args.iso,args.disc)
        G.require_resident(world,textures,'Roger model 0x47')
        gs=G.first_level_freeze(ROOT/'build/disc_textures/first_level_gs.bin',world)
    entries,texels=native.build_texture_blob(None,textures,p2s=gs)
    native.write_emdl(out/'roger.emdl',sections,[],parents,[initial],60.,entries,texels,
                      clips=[dict(id=8,first=0,count=1,fps=60.)])
    ram_path=args.verify_ram or DECOMP/'build/startup-reference/playable_ee.bin'
    capture=None
    if not args.no_verify and ram_path.is_file():
        capture=dict(ram=str(ram_path),**verify_ram(ram_path.read_bytes(),data,raw))
    report=dict(model_source='chunk15/f18_id94.bin',model_offset=MODEL_AT,model_bytes=size,
        model_sha256=hashlib.sha256(raw).hexdigest(),
        bank_source='chunk15/f12_id44.bin',bank_offset=BANK,capture_check=capture,
        clips=rows,trigger_source='0082AB80',trigger=struct.unpack('<16f',polygon),
        texels=str(gs),gs_sha256=hashlib.sha256(Path(gs).read_bytes()).hexdigest(),
        files={p.name:dict(size=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
               for p in sorted(out.iterdir()) if p.name in ('channels.empc','programs.emsc','trigger.empg','roger.emdl')},
        boundaries=['face morph/blink state','owner placement rounding',
                    'bank96 encounter streams and scripted pose binding'])
    receipt=ROOT/'build/roger_reference';receipt.mkdir(parents=True,exist_ok=True)
    (receipt/'export.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Exported original Roger mesh, nine raw clips, four programs and polygon'
          +(' (checked against the capture).' if capture else '.'))


if __name__=='__main__':main()
