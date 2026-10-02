#!/usr/bin/env python3
"""Export the player's weapon clip rows/sound/tint constants from the user's ELF
(EMAF v4: also the R2 aim camera's eye offset D_002754E8..D_002754F3, read by 00198440).

Native Python, no dependencies. Output is disc-derived and must stay ignored.
For an isolated checkout with assets symlinked, use --output
build/aim-fire/tables.emaf and EM_AIM_FIRE_TABLES=build/aim-fire/tables.emaf.
"""
import argparse
import hashlib
from pathlib import Path
import struct

ROOT=Path(__file__).resolve().parents[1]
BASE,SIZE=0x248680,0x680
SPANS=((BASE,SIZE),(0x2533D0,0xC0),(0x253720,0x20),(0x266930,0x1B0),(0x2754E8,0xC))
SHA='ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf',type=Path,default=ROOT.parent/'Extermination/config/SCUS_971.12')
    parser.add_argument('--output',type=Path,default=ROOT/'assets/aim_fire_tables.emaf')
    args=parser.parse_args()
    elf=args.elf.read_bytes()
    if hashlib.sha256(elf).hexdigest()!=SHA: raise SystemExit('Wrong original ELF hash')
    start=BASE-0x100000+0x300
    data=elf[start:start+SIZE]
    if len(data)!=SIZE: raise SystemExit('ELF table span is truncated')
    for bank in (0x248B70,0x248C50):
        for i in range(6):
            row=struct.unpack_from('<I',data,bank-BASE+4*i)[0]
            if not BASE<=row<=BASE+SIZE-18: raise SystemExit('Clip row leaves export span')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    header=struct.pack('<4sI',b'EMAF',4)+b''.join(struct.pack('<II',*span) for span in SPANS)
    payload=b''.join(elf[a-0x100000+0x300:a-0x100000+0x300+n] for a,n in SPANS)
    args.output.write_bytes(header+payload)
    print(f'Exported {len(payload)} table bytes to {args.output}; keep this generated file ignored')

if __name__=='__main__': main()
