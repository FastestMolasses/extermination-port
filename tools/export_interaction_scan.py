#!/usr/bin/env python3
"""Export initial AREA11 use-owner metadata and original SDK math coefficients.

Disc-derived outputs stay in ignored assets/. Runtime captures validate
source-to-owner associations, not a universal actor allocation address.
The static source-record address is the stable native identity token.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
SHA='ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--decomp',type=Path,default=ROOT.parent/'Extermination')
    parser.add_argument('--ee',type=Path)
    parser.add_argument('--out',type=Path,default=ROOT/'assets/scene_snow/interaction.emis')
    args=parser.parse_args()
    sys.path.insert(0,str(args.decomp/'tools'))
    from export_level import BootElf,AreaMem,defer_records,FN_PICKUPS
    elf=BootElf(args.decomp/'config/SCUS_971.12')
    assert hashlib.sha256(elf.data).hexdigest()==SHA
    overlay_path=args.decomp/'extract/OVERLAY/AREA11.BIN'
    overlay=overlay_path.read_bytes();mem=AreaMem(elf,overlay_path)
    ram=(args.ee or args.decomp/'build/startup-reference/playable_ee.bin').read_bytes()
    assert len(ram)==0x2000000 and ram[0x810700:0x810702]==b'\x0b\0'
    assert ram[0x827b10:0x828050]==overlay[0x4610:0x4b50]
    def u32(a):return struct.unpack_from('<I',ram,a)[0]
    source_pickups=[r for r in defer_records(mem,11,0) if r['fn'] in FN_PICKUPS]
    assert [r['puid'] for r in source_pickups]==[1,4,5,6,7,8,9]
    ptr=u32(0x275bc0);rank=0;owners=[]
    while ptr:
        callback=u32(ptr+16)
        if callback in (*FN_PICKUPS,0x159210,0x827b10,0x1bc350,0x8237e0):
            role=0 if callback in FN_PICKUPS else {
                0x159210:1,0x827b10:2,0x1bc350:3,0x8237e0:4}[callback]
            if role==0:
                source=next(r for r in source_pickups if r['puid']==ram[ptr+0x9a])
                source_id=source['vaddr']
                assert callback==source['fn'] and ram[ptr+2]==source['cls']
                assert ram[ptr+3]==source['model']
                assert ram[ptr+0xb0:ptr+0xbc]==struct.pack('<3f',*source['pos'])
                assert ram[ptr+0xc0:ptr+0xcc]==struct.pack('<3f',*source['rot'])
                descriptor_address=0x275878 if callback==0x219550 else 0x275488
                size=2
            else:
                # Actual40-byte record begins with packed actor metadata;
                # its behavior is the LAST word. The preceding word belongs
                # to the previous record and must not be named a constructor.
                source_id={1:0x82a690,2:0x82a6b8,3:0x82a3c0,4:0x82a500}[role]
                record=mem.read(source_id,40)
                assert struct.unpack_from('<I',record,36)[0]==callback
                assert record[0]==ram[ptr+2] and record[2]==ram[ptr+3]
                assert record[12:36]==ram[ptr+0xb0:ptr+0xbc]+ram[ptr+0xc0:ptr+0xcc]
                descriptor_address={1:0x275480,2:0x82ab10,3:0x2755f0,4:0x828bd0}[role]
                size=6 if role==2 else 3 if role in (1,4) else 2
            assert u32(ptr+0x30)==descriptor_address
            descriptor=bytearray(mem.read(descriptor_address,size*4))
            if role==2:
                # Original827B10 initializer patches only this descriptor's Y.
                descriptor[4:8]=struct.pack('<f',190 if ram[0x81083a] else 230)
            assert descriptor==ram[descriptor_address:descriptor_address+size*4]
            descriptor+=bytes(24-len(descriptor))
            uid=(11<<8)|ram[ptr+0x9a]
            item_type=struct.unpack_from('<H',ram,ptr+0x2e)[0]
            owner=dict(source_id=source_id,role=role,publication_rank=rank,
                callback=callback,item_type=item_type,uid=uid,status=ram[ptr],
                class_flags=ram[ptr+2],subtype=ram[ptr+3],selector=ram[ptr+8],
                descriptor=struct.unpack('<6f',descriptor),
                position=struct.unpack_from('<3f',ram,ptr+0xb0),
                angles=struct.unpack_from('<3f',ram,ptr+0xc0),
                reference_owner=ptr,descriptor_address=descriptor_address)
            owners.append(owner)
        ptr=u32(ptr+0x1c);rank+=1
    assert len(owners)==11 and [r['role'] for r in owners]==[0]*7+[3,4,1,2]
    math=elf.read(0x26c5d8,76)
    records=bytearray()
    for r in owners:
        records+=struct.pack('<5IH4BHI',r['source_id'],r['role'],r['publication_rank'],
            r['callback'],r['item_type'],r['uid'],r['status'],r['class_flags'],
            r['subtype'],r['selector'],0,0)
        records+=struct.pack('<12f',*r['descriptor'],*r['position'],*r['angles'])
    assert len(records)==80*len(owners)
    blob=struct.pack('<4s4I',b'EMIS',1,0xb00,len(owners),80)+math+records
    args.out.parent.mkdir(parents=True,exist_ok=True);args.out.write_bytes(blob)
    metadata=dict(version=1,elf_sha256=SHA,overlay_sha256=hashlib.sha256(overlay).hexdigest(),
        reference_sha256=hashlib.sha256(ram).hexdigest(),output_sha256=hashlib.sha256(blob).hexdigest(),
        math_address=0x26c5d8,math_floats=19,record_stride=80,owners=owners,
        note='reference_owner is evidence only; source_id is the stable native owner token.')
    out=ROOT/'build/interaction_scan_reference';out.mkdir(parents=True,exist_ok=True)
    (out/'export.json').write_text(json.dumps(metadata,indent=2)+'\n')
    print(f'Exported {len(owners)} AREA11 interaction owners + original SDK coefficients to {args.out}')


if __name__=='__main__':main()
