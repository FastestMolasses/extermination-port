#!/usr/bin/env python3
"""Export AREA11 placement18's original compact cell faces from local assets.

World directory4 resolves to chunk15/f12_id44+0x39800. The actor remains
published as class4/uid18 during its battery interaction; its cell is set2.
No invented mesh bounds or collision dimensions are used.
"""
import argparse
import struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def export_panel_cell(decomp,scene):
    overlay=(decomp/'extract/OVERLAY/AREA11.BIN').read_bytes()
    record=0x82a3c0-0x823500+18*40
    if overlay[:4]!=b'MWo3' or struct.unpack_from('<I',overlay,record+36)[0]!=0x159210:
        raise ValueError('original panel owner differs')
    uid=overlay[record+7];attr=overlay[record+8]
    if uid!=18 or attr!=0x46:raise ValueError('original panel cell identity differs')
    data=(decomp/'extract/chunk15/f12_id44.bin').read_bytes();base=0x39800
    count=struct.unpack_from('<I',data,base)[0]
    if count!=27:raise ValueError('original cell directory differs')
    cell=base+(struct.unpack_from('<I',data,base+4+uid*4)[0]&0x3fffffff)
    n=struct.unpack_from('<h',data,cell+24)[0]
    if n!=5:raise ValueError('original panel face count differs')
    records=[]
    for i in range(n):
        at=cell+28+i*28;kind,face=struct.unpack_from('<HB',data,at)
        if kind!=0x2000 or face not in (1,2,4,5,6):raise ValueError('unexpected panel primitive')
        records.append(struct.pack('<I',face)+data[at+4:at+28])
    out=scene/'props/panel_cell18.emcb';out.parent.mkdir(parents=True,exist_ok=True)
    out.write_bytes(struct.pack('<4s4I',b'EMCB',1,uid,attr,n)+data[cell:cell+24]+b''.join(records))
    print('original panel collision:',out,'faces',n)
    return out

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--decomp-root',type=Path,default=ROOT.parent/'Extermination')
    parser.add_argument('--scene',type=Path,default=ROOT/'assets/scene_snow')
    args=parser.parse_args();export_panel_cell(args.decomp_root,args.scene)
if __name__=='__main__':main()
