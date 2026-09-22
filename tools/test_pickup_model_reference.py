#!/usr/bin/env python3
"""Verify the box's authored skeleton against an immutable original RAM capture.

The optional --ee input is an already extracted original EE Memory.bin;
this never communicates with an emulator or changes a saved state.
"""
import argparse
import json
from pathlib import Path
import struct
import sys

from export_pickup_lights import load_tool, owner_rest_mesh

ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--decomp-root',type=Path,default=ROOT.parent/'Extermination')
    parser.add_argument('--ee',type=Path)
    args=parser.parse_args()
    root=args.decomp_root.resolve()
    sys.path.insert(0,str(root/'tools'))
    props=load_tool(root/'tools/export_props.py','_pickup_model_reference')
    library=(root/'extract/chunk27/f01_id37.bin').read_bytes()
    sections,textures,parents,frames,offset=owner_rest_mesh(props,library)
    end=props.table_entry_offset(library,0,0x73)
    ram=(args.ee or root/'build/startup-reference/playable_ee.bin').read_bytes()
    def u32(address):return struct.unpack_from('<I',ram,address)[0]
    actor=u32(0x275bc0);seen=set();count=0;error=0.0
    while actor:
        if actor in seen or not 0<actor<len(ram)-0x300:
            raise ValueError('Invalid original actor list')
        seen.add(actor)
        if u32(actor+0x10)==0x219550 and ram[actor+0xd]==0x72:
            model=u32(actor+0x44)
            assert ram[model:model+end-offset]==library[offset:end]
            assert ram[actor+0xc]==3
            world=tuple(struct.unpack_from('<4f',ram,actor+0xd0+i*16)
                        for i in range(4))
            for i,local in enumerate(frames[0]):
                node=u32(actor+0x110+i*4)
                actual=struct.unpack_from('<16f',ram,node+0x90)
                expected=[v for col in props.en.mat_mul(world,local) for v in col]
                error=max(error,max(abs(a-b)for a,b in zip(actual,expected)))
            count+=1
        actor=u32(actor+0x1c)
    assert count==6,('Expected six original closed model72 owners',count)
    # Local-to-world shape is verified; EE hierarchical float truncation
    # differs slightly from the host's precomposed static palette.
    assert error<0.00008,error
    native=(ROOT/'assets/scene_snow/props/item_72.emdl').read_bytes()
    magic,bones,vertices,indices,frame_count,fps,tex_count,flags,clips=struct.unpack_from('<4s4If3I',native)
    assert (magic,bones,vertices,indices,frame_count,flags,clips)==(
        b'EMD3',4,len(sections[0][0]),660,1,0,1)
    assert struct.unpack_from('<4i',native,36)==(-1,0,1,-1)
    start=36+bones*4+tex_count*16+clips*16
    slots={struct.unpack_from('<I',native,start+i*40+32)[0] for i in range(vertices)}
    assert slots=={1,2},'Original node-local geometry must retain both slots'
    print(json.dumps(dict(owners=count,source_model_bytes=end-offset,
        original_parents=parents,vertices=vertices,triangles=indices//3,
        maximum_world_matrix_error=error,status='PASS'),indent=2))


if __name__=='__main__':main()
