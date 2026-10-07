#!/usr/bin/env python3
"""Validate expanded local stream export against the disc and AREA01 RAM.

Every exported sector is byte-compared with the user's disc. Original
AREA01 message and stream tables are compared in all captures, and every
referenced nonempty cue must have all of its sectors in the export.
"""
import argparse
import json
import struct
import time
from pathlib import Path
import export_area01_common as A
import export_streams as E


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--export',type=Path,default=A.ROOT/'assets/streams/streams.emst')
    parser.add_argument('--previous',type=Path)
    args = parser.parse_args()
    start = time.monotonic()
    blob = args.export.read_bytes()
    assert blob[:8] == struct.pack('<4sI',b'EMST',1)
    music_lsn,voice_lsn,_,_,nm,nv,ne = struct.unpack_from('<7I',blob,8)
    assert (nm,nv) == (E.MUSIC_ROWS,E.VOICE_ROWS)
    table_end = E.HEADER+16*(nm+nv)
    extents = [struct.unpack_from('<4I',blob,table_end+16*i) for i in range(ne)]
    def sector(data,records,lsn):
        for first,count,offset,_ in records:
            if first <= lsn < first+count:
                return data[offset+2048*(lsn-first):offset+2048*(lsn-first+1)]
        raise AssertionError(f'missing sector {lsn}')
    total = 0
    with A.ISO_PATH.open('rb') as disc:
        for first,count,offset,_ in extents:
            disc.seek(first*2048)
            assert blob[offset:offset+count*2048] == disc.read(count*2048)
            total += count
    prior = 0
    if args.previous:
        old = args.previous.read_bytes()
        old_count = struct.unpack_from('<I',old,32)[0]
        records = [struct.unpack_from('<4I',old,table_end+16*i) for i in range(old_count)]
        for first,count,_,_ in records:
            for lsn in range(first,first+count):
                assert sector(old,records,lsn) == sector(blob,extents,lsn)
                prior += 1
    elf = E.Elf(A.ELF_PATH)
    music,voice = E.cue_list(elf)
    for kind,cues,base,index in (('music',music,music_lsn,0),('voice',voice,voice_lsn,nm)):
        for cue in cues:
            lsn,begin,size,_ = struct.unpack_from('<4i',blob,E.HEADER+16*(index+cue))
            assert begin == 2048*lsn and size>0,(kind,cue)
            for offset in range((size+2047)//2048):
                sector(blob,extents,base+lsn+offset)
    caps = A.captures()
    for cap in caps:
        assert cap.ram[0x26F060:0x26F060+184*8] == elf.read(0x26F060,184*8)
        for i in range(512):
            row = elf.read(E.STREAM_TABLE+16*i,16)
            if struct.unpack_from('<i',row)[0] == -1:break
            assert cap.ram[E.STREAM_TABLE+16*i:E.STREAM_TABLE+16*i+16] == row
    report = dict(status='PASS',captures=len(caps),music_cues=len(music),voice_cues=len(voice),
                  disc_sectors=total,retained_prior_sectors=prior,extents=ne,
                  seconds=round(time.monotonic()-start,3))
    out=A.ROOT/'build/level2-crashes/stream-export-reference';out.mkdir(parents=True,exist_ok=True)
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))

if __name__ == '__main__':main()
