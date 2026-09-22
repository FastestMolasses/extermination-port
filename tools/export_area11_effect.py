#!/usr/bin/env python3
"""Export AREA11 owner008235F0's original placement/particles/texture.

Outputs are generated locally from the user's disc and reference state, and
belong only in ignored assets/build directories. Existing model files are
untouched. This exporter replaces the old guessed steam manifest record.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--decomp-root',type=Path,default=ROOT.parent/'Extermination')
    parser.add_argument('--ee',type=Path,required=True)
    parser.add_argument('--gs',type=Path,required=True)
    parser.add_argument('--vu',type=Path,required=True)
    parser.add_argument('--out',type=Path,default=ROOT/'assets/scene_snow')
    args=parser.parse_args()
    sys.path.insert(0,str(args.decomp_root/'tools'))
    import export_native as native
    import export_level as level
    original=args.decomp_root/'config/SCUS_971.12'
    elf=original.read_bytes()
    assert hashlib.sha256(elf).hexdigest()=='ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
    overlay=(args.decomp_root/'extract/OVERLAY/AREA11.BIN').read_bytes()
    ram=args.ee.read_bytes(); vu=args.vu.read_bytes()
    assert len(ram)==32*1024*1024 and len(vu)==16384
    assert ram[0x810700:0x810702]==b'\x0b\0'
    assert overlay[0xf0:0x2c0]==ram[0x8235f0:0x8237c0], 'runtime overlay mapping'
    descriptor=overlay[0x4e40:0x4ed0]
    assert descriptor==ram[0x828340:0x8283d0]==vu[0x500:0x590]
    assert struct.unpack_from('<4I',descriptor,128)[::2]==(80,9)
    assert struct.unpack_from('<I',descriptor,140)[0]==2
    record=overlay[0x6fd4:0x6ffc]
    assert struct.unpack_from('<I',record)[0]==0x1551b0
    assert struct.unpack_from('<I',record,4)[0]==0x1000d
    position=record[16:28]; rotation=record[28:40]
    assert rotation==bytes(12), 'unrecovered nonzero placement rotation'
    assert position==ram[0x7a85f0:0x7a85fc]
    assert struct.unpack_from('<I',ram,0x7a8550)[0]==0x8235f0
    matrix=struct.pack('<12f',1,0,0,0,0,1,0,0,0,0,1,0)+position+struct.pack('<f',1)
    assert matrix==ram[0x7a8610:0x7a8650]==vu[0x5a0:0x5e0]
    offset=0x2342bc-0x100000+0x300
    lookup=elf[offset:offset+320]
    assert lookup==b''.join(vu[i*16:i*16+4] for i in range(80))
    rig_index,matched,rig=level.lightrig_read(level.BootElf(original),11,0)
    assert matched
    fog=struct.pack('<2f',*rig['fog'][:2])
    key=struct.unpack_from('<Q',descriptor,112)[0]&native.TEX0_KEY_MASK
    texture=native.tex0_fields(key); texture['key']=key
    entries,texels=native.build_texture_blob(None,[texture],p2s=args.gs)
    entry=entries[0]
    assert 1<entry['w']<=256 and 1<entry['h']<=256
    data=struct.pack('<4s3I',b'EMEF',1,0x0b00,80)+position+descriptor+lookup+fog
    texture_data=struct.pack('<4s3I',b'EMTX',1,entry['w'],entry['h'])+texels
    args.out.mkdir(parents=True,exist_ok=True)
    (args.out/'area11_effect.emef').write_bytes(data)
    (args.out/'area11_effect.emtx').write_bytes(texture_data)
    manifest=args.out/'scene.txt'
    if manifest.exists():
        lines=manifest.read_text().splitlines()
        lines=[line for line in lines if not line.startswith(('steam ','area11effect ','# Legacy steam audio/FX',
                '# Original AREA11 runtime owner008235F0, placement record7.'))]
        lines+=['# Original AREA11 runtime owner008235F0, placement record7.',
                'area11effect area11_effect.emef area11_effect.emtx']
        manifest.write_text('\n'.join(lines)+'\n')
    report={'original_elf_sha256':hashlib.sha256(elf).hexdigest(),
            'original_overlay_sha256':hashlib.sha256(overlay).hexdigest(),
            'runtime_owner':0x8235f0,'runtime_descriptor':0x828340,
            'placement_file_offset':0x6fd4,'placement':struct.unpack('<3f',position),
            'lookup_address':0x2342bc,'rig_index':rig_index,'fog_near_far':rig['fog'][:2],
            'texture':texture,'texture_width':entry['w'],'texture_height':entry['h'],
            'config_sha256':hashlib.sha256(data).hexdigest(),
            'texture_sha256':hashlib.sha256(texture_data).hexdigest()}
    output=ROOT/'build/area11_effect_reference';output.mkdir(parents=True,exist_ok=True)
    (output/'export.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'Exported AREA11 effect:80 particles, {entry["w"]}x{entry["h"]} texture')


if __name__=='__main__': main()
