#!/usr/bin/env python3
"""Validate exported Roger keys and playback against two original RAM captures."""
import ctypes as C
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
from test_pose_bank_reference import Bank,State
from test_pose_transition_reference import Original,A,OUT,bits

ROOT=Path(__file__).resolve().parents[1]
DECOMP=ROOT.parent/'Extermination'


def main():
    out=ROOT/'build/roger_reference';out.mkdir(parents=True,exist_ok=True)
    bridge=out/'pose_bridge.c'
    bridge.write_text('''#include "game/em_player_pose.h"
int roger_palette(const EmPoseBank *bank,unsigned ticks,float *out) {
    EmPlayerPose pose;
    if(!em_player_pose_init(&pose,bank,8,0))return 0;
    for(unsigned i=0;i<ticks;++i)if(!em_player_pose_advance(&pose,1,0))return 0;
    return em_player_pose_palette(&pose,out,22);
}
''')
    library=out/'pose.dylib'
    subprocess.run(['cc','-shared','-fPIC','-std=c11','-O2','-ffp-contract=off','-Isrc',
        str(bridge),'src/game/em_player_pose.c','src/game/em_pose_bank.c',
        'src/game/em_pose_transition.c','-o',str(library)],cwd=ROOT,check=True)
    native=C.CDLL(str(library.resolve()));bank=Bank()
    native.em_pose_bank_load.argtypes=[C.POINTER(Bank),C.c_char_p]
    native.em_pose_bank_free.argtypes=[C.POINTER(Bank)]
    native.em_pose_playback_begin.argtypes=[C.POINTER(State),C.POINTER(Bank),C.c_uint,C.c_float]
    native.em_pose_playback_advance.argtypes=[C.POINTER(State),C.c_float,C.c_int]
    native.roger_palette.argtypes=[C.POINTER(Bank),C.c_uint,C.POINTER(C.c_float)]
    assert native.em_pose_bank_load(C.byref(bank),str(ROOT/'assets/scene_snow/roger/channels.empc').encode())
    assert (bank.bone_count,bank.clip_count)==(21,9)
    source=(DECOMP/'extract/chunk15/f12_id44.bin').read_bytes();base=0x10e000
    elf=(DECOMP/'config/SCUS_971.12').read_bytes();decoder=Original(elf);decoded=0
    for ci in range(bank.clip_count):
        clip=bank.clips[ci];header=base+struct.unpack_from('<I',source,base+4+4*clip.id)[0]
        for bone in range(bank.bone_count):
            for kind in range(3):
                table=header+struct.unpack_from('<I',source,header+8+4*kind)[0]
                record=table+struct.unpack_from('<I',source,table+4*bone)[0]
                track=clip.tracks[bone][kind]
                for index in range(track.count-1):
                    raw=source[record+12*index:record+12*index+12]
                    for offset,value in enumerate(raw):decoder.put(A+offset,value,1)
                    decoder.run(0x1c84d0 if kind==0 else 0x1c85d0,(A,OUT))
                    width=4 if kind==0 else 3
                    assert [bits(track.keys[index].value[k]) for k in range(width)]==decoder.floats(OUT,width)
                    flags,time=struct.unpack_from('<HH',raw,8)
                    assert (track.keys[index].time,track.keys[index].hold)==(time,bool(flags&0x8000))
                    decoded+=1
    sys.path.insert(0,str(DECOMP/'tools'))
    from export_opening_actors import matrix_multiply
    captures=[]
    for name in ('opening_ee.bin','playable_ee.bin'):
        ram=(DECOMP/'build/startup-reference'/name).read_bytes();actor=0x7a8830
        remaining=struct.unpack_from('<f',ram,actor+0x3c)[0]
        clip=struct.unpack_from('<H',ram,actor+0x2c)[0]
        state=State();assert native.em_pose_playback_begin(C.byref(state),C.byref(bank),clip,0)
        frame=state.clip.contents.duration-remaining
        assert frame==int(frame)
        for _ in range(int(frame)):assert native.em_pose_playback_advance(C.byref(state),1,0)
        count=0
        for bone in range(21):
            node=struct.unpack_from('<I',ram,actor+0x110+4*bone)[0]
            rotation,translation,scale=state.nodes[bone];actual={}
            for cursor,start in ((translation,0),(scale,24)):
                for component in range(3):
                    actual[start+component*4]=bits(cursor.value[component])
                    actual[start+12+component*4]=bits(cursor.velocity[component])
            keys=state.clip.contents.tracks[bone][0].keys
            for component in range(4):
                actual[48+component*4]=bits(keys[rotation.index-1].value[component])
                actual[64+component*4]=bits(keys[rotation.index].value[component])
            actual.update({80:bits(rotation.fraction),84:bits(rotation.reciprocal),
                88:bits(translation.remaining),92:bits(scale.remaining),96:bits(rotation.remaining)})
            for offset,value in actual.items():
                assert value==struct.unpack_from('<I',ram,node+offset)[0],(name,bone,offset)
                count+=1
            assert (rotation.index,translation.index,scale.index)==struct.unpack_from('<3H',ram,node+0x66)
            assert struct.unpack_from('<6f3h',ram,node+0x70)==(0,0,0,0,0,0,4096,4096,4096)
        palette=(C.c_float*(22*16))();assert native.roger_palette(C.byref(bank),int(frame),palette)
        owner=[struct.unpack_from('<4f',ram,actor+0xd0+16*i) for i in range(4)]
        error=0
        for bone in range(21):
            node=struct.unpack_from('<I',ram,actor+0x110+4*bone)[0]
            local=[list(palette)[bone*16+4*i:bone*16+4*i+4] for i in range(4)]
            world=[v for column in matrix_multiply(owner,local) for v in column]
            expected=struct.unpack_from('<16f',ram,node+0x90)
            error=max(error,max(abs(a-b) for a,b in zip(world,expected)))
        assert error<.0002,(name,error)
        captures.append(dict(name=name,sha256=hashlib.sha256(ram).hexdigest(),clip=clip,
            frame=frame,exact_node_float_comparisons=count,exact_cursor_indices=63,
            native_hierarchy_world_matrix_max_error=error))
    native.em_pose_bank_free(C.byref(bank))
    result=dict(decoded_original_keys=decoded,captures=captures,
        boundaries=['Uses captured original owner matrix for world-placement comparison',
                    'Facial morphs and any later nonidentity node adjustments are not covered'])
    (out/'pose_capture.json').write_text(json.dumps(result,indent=2)+'\n')
    print(f'Original Roger pose: {decoded} raw keys, two captured525-float/63-index states PASS')


if __name__=='__main__':main()
