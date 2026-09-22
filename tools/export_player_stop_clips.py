#!/usr/bin/env python3
"""Append original stop clips4/5 while preserving all existing player data.

The existing first clip supplies the original export's shared origin. Its
entire regenerated palette must match before the new poses can be appended.
The original mesh, equipment, texture data and existing clip bytes are copied.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--decomp',type=Path,default=ROOT.parent/'Extermination')
    parser.add_argument('--player',type=Path,default=ROOT/'assets/player.emdl')
    args=parser.parse_args()
    data=args.player.read_bytes()
    magic,bones,verts,indices,frames,fps,textures,flags,nclips=struct.unpack_from('<4sIIIIfIII',data)
    assert magic==b'EMD3' and bones==22 and flags==0
    parent_end=36+bones*4
    clips_at=parent_end+textures*16
    mesh_at=clips_at+nclips*16
    palette_at=mesh_at+verts*40+indices*4
    texture_at=palette_at+frames*bones*64
    clips=[struct.unpack_from('<IIIf',data,clips_at+16*i) for i in range(nclips)]
    present={c[0] for c in clips}
    assert not ({4,5}&present), 'Stop clips already exist; refusing duplicate or partial append'
    sys.path.insert(0,str(args.decomp/'tools'))
    spec=importlib.util.spec_from_file_location('original_native_export',args.decomp/'tools/export_native.py')
    exporter=importlib.util.module_from_spec(spec);spec.loader.exec_module(exporter)
    parents,poses,newclips,_=exporter.bake_id74_clips(args.decomp/'extract/chunk28/f01_id3c.bin',[clips[0][0],4,5])
    expected_parents=struct.pack('<22i',*[p if 0<=p<21 else -1 for p in parents],-1)
    assert data[36:parent_end]==expected_parents,'Rig parent mismatch'
    baked=bytearray()
    for pose in poses:
        for matrix in [*pose,exporter.mat_identity()]:
            for column in matrix:baked+=struct.pack('<4f',*column)
    first=newclips[0]
    assert first['count']==clips[0][2]
    firstbytes=first['count']*bones*64
    original_first=palette_at+clips[0][1]*bones*64
    assert bytes(baked[:firstbytes])==data[original_first:original_first+firstbytes], 'Existing origin/pose bake differs; do not mix rigs or transforms'
    assert [c['count'] for c in newclips[1:]]==[20,10], 'Original stop clip length mismatch'
    extra=baked[firstbytes:]
    header=bytearray(data[:36]);struct.pack_into('<I',header,16,frames+30);struct.pack_into('<I',header,32,nclips+2)
    table=b''.join(struct.pack('<IIIf',c['id'],frames+c['first']-first['count'],c['count'],c['fps']) for c in newclips[1:])
    result=bytes(header)+data[36:mesh_at]+table+data[mesh_at:texture_at]+extra+data[texture_at:]
    outdir=ROOT/'build/player_stop_export';outdir.mkdir(parents=True,exist_ok=True)
    backup=outdir/'player_before_stop.emdl'
    if not backup.exists():backup.write_bytes(data)
    geometry=data[mesh_at:palette_at];textures_blob=data[texture_at:]
    new_mesh_at=mesh_at+32;new_palette_at=palette_at+32;new_texture_at=texture_at+32+len(extra)
    assert result[new_mesh_at:new_palette_at]==geometry
    assert result[new_texture_at:]==textures_blob
    assert result[new_palette_at:new_palette_at+frames*bones*64]==data[palette_at:texture_at]
    report={'before_sha256':hashlib.sha256(data).hexdigest(),'after_sha256':hashlib.sha256(result).hexdigest(),
            'preserved_existing_clips':nclips,'preserved_existing_frames':frames,'added_clips':[4,5],'added_frames':30,
            'geometry_sha256':hashlib.sha256(geometry).hexdigest(),'texture_blob_sha256':hashlib.sha256(textures_blob).hexdigest(),
            'first_clip_pose_bytes_verified':firstbytes,'added_palette_bytes':len(extra)}
    args.player.write_bytes(result)
    (outdir/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Appended original stop clips; all existing geometry, textures and animation bytes preserved:',json.dumps(report))


if __name__=='__main__':main()
