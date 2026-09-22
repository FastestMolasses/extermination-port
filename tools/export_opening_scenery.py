#!/usr/bin/env python3
"""Restore AREA11's canopy placement from the original overlay record.

The opening manager is also the persistent canopy prop: 00823E80 initializes
its per-area model through001B0FD0/001C6380 and keeps drawing it after the
opening script completes. This updates only that generated scene-manifest
entry. No guessed positions or forced-state capture coordinates are used.
"""
from __future__ import annotations
import argparse
import json
import math
from pathlib import Path
import struct

ROOT=Path(__file__).resolve().parents[1]
TABLE=0x0082A3C0
BEHAVIOR=0x00823E80
MODEL='props/area_parachute.emdl'


def canopy_placement(overlay: bytes) -> dict:
    if len(overlay)!=0x7800 or overlay[:4]!=b'MWo3':
        raise ValueError('Expected original SCUS-97112 AREA11 overlay')
    arena=struct.unpack_from('<I',overlay,8)[0]
    if arena!=0x00823500:
        raise ValueError('Unexpected AREA11 arena')
    found=[]
    for i in range(256):
        off=TABLE-arena+i*40
        if off+40>len(overlay):
            raise ValueError('Placement table ran outside overlay')
        sc,model,flags,param,uid,kind,link=struct.unpack_from('<HBBHHHH',overlay,off)
        if sc==0xff:break
        behavior=struct.unpack_from('<I',overlay,off+36)[0]
        if behavior!=BEHAVIOR:continue
        xyz=struct.unpack_from('<3f',overlay,off+12)
        rot=struct.unpack_from('<3f',overlay,off+24)
        if (sc!=4 or model!=0 or param!=0x11 or rot[0]!=0 or rot[2]!=0
                or not all(math.isfinite(x) for x in (*xyz,*rot))):
            raise ValueError('Unsupported canopy model or transform')
        found.append(dict(record=TABLE+i*40,behavior=behavior,model=param,
                          position=xyz,yaw=rot[1]))
    else:raise ValueError('Unterminated placement table')
    if len(found)!=1:raise ValueError('Expected one original canopy owner')
    return found[0]


def update_manifest(path: Path, record: dict) -> None:
    lines=path.read_text().splitlines()
    matches=[i for i,line in enumerate(lines)
             if line.startswith('pickup ') and MODEL in line.split()]
    if len(matches)!=1:raise ValueError('Expected one generated canopy manifest entry')
    xyz=record['position'];yaw=record['yaw']
    lines[matches[0]]='pickup 0x00 '+' '.join(format(x,'.9g') for x in (*xyz,yaw))+f' 0 {MODEL} prop'
    # Replace the stale explanatory block if present, retaining surrounding
    # scene entries and their unrelated local annotations.
    begin=next((i for i,line in enumerate(lines[:matches[0]]) if line.startswith('# PARACHUTE CANOPY')),None)
    if begin is not None:
        lines[begin:matches[0]]=[
            '# PARACHUTE CANOPY: original AREA11 placement record10, owner00823E80.',
            '# The owner binds per-area model11 and keeps drawing after its opening',
            '# script finishes. Position/yaw come from overlay record0082A550 and',
            '# match the cold-boot original actor matrix. Regenerate this line with',
            '# tools/export_opening_scenery.py; the prior forced-state pose was wrong.']
    path.write_text('\n'.join(lines)+'\n')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--overlay',type=Path,default=ROOT.parent/'Extermination/extract/OVERLAY/AREA11.BIN')
    parser.add_argument('--manifest',type=Path,default=ROOT/'assets/scene_snow/scene.txt')
    args=parser.parse_args()
    record=canopy_placement(args.overlay.read_bytes())
    update_manifest(args.manifest,record)
    print(json.dumps(record,indent=2))

if __name__=='__main__':main()
