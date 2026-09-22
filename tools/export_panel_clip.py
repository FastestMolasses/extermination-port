#!/usr/bin/env python3
"""Append the original panel clip15C with a captured-palette proof.

Uses the original stateful channel cursor and unnormalized quaternion
blend already recovered for opening actors, at rate1.0. Original root
channel0 has no translation; actor+A0/world placement stays fixed while
node1/actor+B0 supplies the animated hip. No locomotion recentering,
matrix interpolation, or fabricated player movement is baked here.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--decomp',type=Path,default=ROOT.parent/'Extermination')
    ap.add_argument('--player',type=Path,default=ROOT/'assets/player.emdl')
    args=ap.parse_args();sys.path.insert(0,str(args.decomp/'tools'))
    from export_opening_actors import OpeningClip,matrix_multiply,original_palette
    from export_native import mat_identity
    bank=(args.decomp/'extract/chunk28/f01_id3c.bin').read_bytes()
    ram=(args.decomp/'build/startup-reference/panel/animation_ee.bin').read_bytes()
    actor=0x8102B0
    assert struct.unpack_from('<I',ram,actor+0x40)[0]==0xD689C0
    assert ram[0xD689C0:0xD689C0+len(bank)]==bank,'Original animation bank mismatch'
    header=struct.unpack_from('<I',bank,4+348*4)[0]
    clip=OpeningClip(bank,header)
    assert (clip.bones,clip.length)==(21,121)
    assert struct.unpack_from('<h',bank,header+4)[0]==-2
    assert struct.unpack_from('<I',bank,header+0x14)[0]==0
    assert struct.unpack_from('<H',ram,actor+0x20C)[0]==348
    owner=[struct.unpack_from('<4f',ram,actor+0xD0+c*16) for c in range(4)]
    assert tuple(owner[3][:3])==struct.unpack_from('<3f',ram,actor+0xA0)
    expected=original_palette(ram,actor,21)
    frames=[];errors=[]
    for frame in range(121):
        assert clip.translation[0].value==(0.0,0.0,0.0),'Unexpected root-motion channel'
        palette=clip.palette();frames.append(palette)
        world=[matrix_multiply(owner,m) for m in palette]
        errors.append(max(abs(a-b) for original,matrix in zip(expected,world)
                          for a,b in zip(original,[v for col in matrix for v in col])))
        hold=False
        for r,t,s in zip(clip.rotation,clip.translation,clip.scale):
            r.advance(1.0);t.advance(1.0);flag=s.advance(1.0)
            if flag is not None:hold=flag
        if hold:
            for r,t,s in zip(clip.rotation,clip.translation,clip.scale):
                r.reciprocal=0.0;t.velocity=s.velocity=(0.0,0.0,0.0)
    nearest=min(range(121),key=errors.__getitem__)
    assert nearest==30 and errors[nearest]<=0.0001,('Original palette comparison failed',nearest,errors[nearest])

    data=args.player.read_bytes()
    magic,bones,verts,indices,nframes,fps,textures,flags,nclips=struct.unpack_from('<4sIIIIfIII',data)
    assert magic==b'EMD3' and bones==22 and flags==0
    assert struct.unpack_from('<22i',data,36)==tuple(clip.parents)+(-1,)
    clips_at=36+bones*4+textures*16
    mesh_at=clips_at+nclips*16
    palette_at=mesh_at+verts*40+indices*4
    texture_at=palette_at+nframes*bones*64
    old_clips=[struct.unpack_from('<IIIf',data,clips_at+i*16) for i in range(nclips)]
    added=b''.join(struct.pack('<16f',*[v for col in matrix for v in col])
                    for frame in frames for matrix in [*frame,mat_identity()])
    existing=[c for c in old_clips if c[0]==348]
    if existing:
        assert len(existing)==1 and existing[0][2:]==(121,60.0)
        first=palette_at+existing[0][1]*bones*64
        assert data[first:first+len(added)]==added,'Existing15C differs; refusing overwrite'
        print(f'Panel clip15C already present and matches original palette proof (max error{errors[nearest]:.9g})')
        return
    output=ROOT/'build/panel_clip_export';output.mkdir(parents=True,exist_ok=True)
    backup=output/'player_before_panel.emdl'
    if not backup.exists():backup.write_bytes(data)
    new_header=bytearray(data[:36])
    struct.pack_into('<I',new_header,16,nframes+121);struct.pack_into('<I',new_header,32,nclips+1)
    entry=struct.pack('<IIIf',348,nframes,121,60.0)
    result=bytes(new_header)+data[36:mesh_at]+entry+data[mesh_at:texture_at]+added+data[texture_at:]
    assert result[mesh_at+16:palette_at+16]==data[mesh_at:palette_at]
    assert result[palette_at+16:texture_at+16]==data[palette_at:texture_at]
    assert result[texture_at+16+len(added):]==data[texture_at:]
    report={'before_sha256':hashlib.sha256(data).hexdigest(),
            'after_sha256':hashlib.sha256(result).hexdigest(),
            'original_capture_frame':nearest,'max_matrix_error':errors[nearest],
            'added_clip':348,'frames':121,'source_bank_header':header,
            'preserved_clips':nclips,'preserved_frames':nframes,
            'mesh_sha256':hashlib.sha256(data[mesh_at:palette_at]).hexdigest(),
            'texture_sha256':hashlib.sha256(data[texture_at:]).hexdigest(),
            'root_motion':'zero; actor placement preserved'}
    args.player.write_bytes(result)
    (output/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'Appended panel clip15C:121 frames,21 original bones; captured frame30 max matrix error{errors[nearest]:.9g}; all existing mesh/texture/clip bytes preserved')


if __name__=='__main__':main()
