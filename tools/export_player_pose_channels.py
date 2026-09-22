#!/usr/bin/env python3
"""Export decoded original node keys for the first-level player clip subset.

EMPC retains key times, hold flags, unnormalized quaternions, translations,
scales and hierarchy. It contains no baked matrices or guessed bind pose.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
ROOT=Path(__file__).resolve().parents[1]
CLIPS=(0,1,2,3,4,5,0x47,0x15c,0x15d)

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--decomp',type=Path,default=ROOT.parent/'Extermination')
    ap.add_argument('--output',type=Path,default=ROOT/'assets/player_channels.empc')
    args=ap.parse_args();sys.path.insert(0,str(args.decomp/'tools'))
    from export_opening_actors import OpeningClip
    data=(args.decomp/'extract/chunk28/f01_id3c.bin').read_bytes()
    clips=[];parents=None;report=[]
    for cid in CLIPS:
        at=struct.unpack_from('<I',data,4+cid*4)[0];clip=OpeningClip(data,at)
        bones,length,next_clip,blend=struct.unpack_from('<HHhh',data,at)
        assert bones==21 and next_clip in (-1,-2) and blend==0
        assert struct.unpack_from('<I',data,at+20)[0]==0,'event-bearing clips need a separate event binding'
        if parents is None:parents=clip.parents
        assert clip.parents==parents
        payload=bytearray(struct.pack('<HHhH',cid,length,next_clip,blend));keys=0
        for bone in range(bones):
            for channel in (clip.rotation[bone],clip.translation[bone],clip.scale[bone]):
                payload+=struct.pack('<I',len(channel.keys));keys+=len(channel.keys)
                for time,values,hold in channel.keys:
                    values=(*values,)+(0.0,)*(4-len(values))
                    payload+=struct.pack('<HH4f',time,int(hold),*values)
        clips.append(payload);report.append({'clip':cid,'frames':length,'next':next_clip,'keys':keys,'source_header':at})
    payload=struct.pack('<4sIII',b'EMPC',1,len(parents),len(clips))+struct.pack('<21i',*parents)+b''.join(clips)
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_bytes(payload)
    out=ROOT/'build/player_pose_channels';out.mkdir(parents=True,exist_ok=True)
    (out/'export.json').write_text(json.dumps({'source_sha256':hashlib.sha256(data).hexdigest(),
        'output_sha256':hashlib.sha256(payload).hexdigest(),'bytes':len(payload),'clips':report},indent=2)+'\n')
    print('original player channel export:',len(clips),'clips,',len(payload),'bytes ->',args.output)
if __name__=='__main__':main()
