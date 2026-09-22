#!/usr/bin/env python3
"""Validate original-key resource and stateful channels against saved original RAM."""
import ctypes as C
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from test_pose_transition_reference import Pose,bits,Original,A,OUT
ROOT=Path(__file__).resolve().parents[1]
class Key(C.Structure):_fields_=[('time',C.c_uint16),('hold',C.c_uint16),('value',C.c_float*4)]
class Track(C.Structure):_fields_=[('count',C.c_uint32),('keys',C.POINTER(Key))]
class Clip(C.Structure):_fields_=[('id',C.c_uint16),('duration',C.c_uint16),('next',C.c_int16),('blend',C.c_uint16),('tracks',(Track*3)*64)]
class Bank(C.Structure):_fields_=[('bone_count',C.c_uint),('clip_count',C.c_uint),('parents',C.c_int32*64),('clips',C.POINTER(Clip))]
class Cursor(C.Structure):_fields_=[('index',C.c_uint),('remaining',C.c_float),('reciprocal',C.c_float),('fraction',C.c_float),('value',C.c_float*3),('velocity',C.c_float*3)]
class State(C.Structure):_fields_=[('bank',C.POINTER(Bank)),('clip',C.POINTER(Clip)),('remaining',C.c_float),('flags',C.c_uint),('nodes',(Cursor*3)*64)]

def captured_channels(native, bank, relative_path):
    ram = (ROOT.parent / 'Extermination/build/startup-reference' / relative_path).read_bytes()
    actor = 0x8102B0
    clip = struct.unpack_from('<H', ram, actor + 0x20C)[0]
    remaining = struct.unpack_from('<f', ram, actor + 0x3C)[0]
    state = State()
    assert native.em_pose_playback_begin(C.byref(state), C.byref(bank), clip, 0)
    frame = state.clip.contents.duration - remaining
    assert frame == int(frame) and frame >= 0
    for _ in range(int(frame)):
        assert native.em_pose_playback_advance(C.byref(state), 1, 0)
    differences = []
    count = 0
    for bone in range(bank.bone_count):
        node = struct.unpack_from('<I', ram, actor + 0x110 + 4 * bone)[0]
        rotation, translation, scale = state.nodes[bone]
        actual = {}
        for cursor, start in ((translation, 0), (scale, 24)):
            for component in range(3):
                actual[start + component * 4] = bits(cursor.value[component])
                actual[start + 12 + component * 4] = bits(cursor.velocity[component])
        keys = state.clip.contents.tracks[bone][0].keys
        for component in range(4):
            actual[48 + component * 4] = bits(keys[rotation.index - 1].value[component])
            actual[64 + component * 4] = bits(keys[rotation.index].value[component])
        actual.update({80: bits(rotation.fraction), 84: bits(rotation.reciprocal),
                       88: bits(translation.remaining), 92: bits(scale.remaining),
                       96: bits(rotation.remaining)})
        for offset, value in actual.items():
            expected = struct.unpack_from('<I', ram, node + offset)[0]
            count += 1
            if value != expected:
                differences.append((bone, offset, hex(value), hex(expected)))
        assert (rotation.index, translation.index, scale.index) == struct.unpack_from('<3H', ram, node + 0x66)
    result = {'capture': relative_path, 'clip': clip, 'frame': frame,
              'float_comparisons': count, 'different': len(differences), 'examples': differences[:25]}
    assert not differences, result
    from export_opening_actors import matrix_multiply
    palette = (C.c_float * (22 * 16))()
    assert native.captured_palette(C.byref(bank), clip, int(frame), palette)
    owner = [struct.unpack_from('<4f', ram, actor + 0xD0 + i * 16) for i in range(4)]
    matrix_error = 0
    for bone in range(21):
        node = struct.unpack_from('<I', ram, actor + 0x110 + 4 * bone)[0]
        assert struct.unpack_from('<6f3h', ram, node + 0x70) == (0, 0, 0, 0, 0, 0, 4096, 4096, 4096)
        local = [list(palette)[bone * 16 + i * 4:bone * 16 + i * 4 + 4] for i in range(4)]
        world = [v for column in matrix_multiply(owner, local) for v in column]
        expected = struct.unpack_from('<16f', ram, node + 0x90)
        matrix_error = max(matrix_error, max(abs(a - b) for a, b in zip(world, expected)))
    result['native_hierarchy_matrix_max_error'] = matrix_error
    assert matrix_error < .0002, result
    return result

def main():
    reference=ROOT.parent/'Extermination/build/startup-reference/panel/eeMemory.bin'
    ram=reference.read_bytes();actor=0x8102b0
    assert struct.unpack_from('<H',ram,actor+0x20c)[0]==0
    assert struct.unpack_from('<f',ram,actor+0x3c)[0]==75
    with tempfile.TemporaryDirectory(prefix='em_pose_bank_') as folder:
        library=Path(folder)/'bank.dylib'
        bridge=Path(folder)/'bridge.c'
        bridge.write_text('''
#include "game/em_player_pose.h"
int captured_palette(const EmPoseBank *bank, unsigned clip, unsigned ticks, float *out) {
    EmPlayerPose pose;
    if (!em_player_pose_init(&pose, bank, clip, 0)) return 0;
    for (unsigned i = 0; i < ticks; ++i)
        if (!em_player_pose_advance(&pose, 1, 0)) return 0;
    return em_player_pose_palette(&pose, out, 22);
}
''')
        subprocess.run(['cc','-shared','-fPIC','-std=c11','-O2','-ffp-contract=off','-I'+str(ROOT/'src'),
            str(bridge),str(ROOT/'src/game/em_player_pose.c'),
            str(ROOT/'src/game/em_pose_bank.c'),str(ROOT/'src/game/em_pose_transition.c'),'-lm','-o',str(library)],check=True)
        native=C.CDLL(str(library));bank=Bank();state=State()
        native.em_pose_bank_load.argtypes=[C.POINTER(Bank),C.c_char_p]
        native.em_pose_playback_begin.argtypes=[C.POINTER(State),C.POINTER(Bank),C.c_uint,C.c_float]
        native.em_pose_playback_advance.argtypes=[C.POINTER(State),C.c_float,C.c_int]
        native.em_pose_playback_channels.argtypes=[C.POINTER(State),C.POINTER(Pose)]
        native.em_pose_bank_free.argtypes=[C.POINTER(Bank)]
        native.captured_palette.argtypes=[C.POINTER(Bank),C.c_uint,C.c_uint,C.POINTER(C.c_float)]
        assert native.em_pose_bank_load(C.byref(bank),str(ROOT/'assets/player_channels.empc').encode())
        assert (bank.bone_count,bank.clip_count)==(21,12)
        elf=(ROOT.parent/'Extermination/config/SCUS_971.12').read_bytes()
        source=(ROOT.parent/'Extermination/extract/chunk28/f01_id3c.bin').read_bytes()
        decoder=Original(elf);decoded=0
        for ci in range(bank.clip_count):
            clip=bank.clips[ci];header=struct.unpack_from('<I',source,4+clip.id*4)[0]
            for bone in range(bank.bone_count):
                for kind in range(3):
                    table=header+struct.unpack_from('<I',source,header+8+4*kind)[0]
                    record=table+struct.unpack_from('<I',source,table+4*bone)[0]
                    track=clip.tracks[bone][kind]
                    for ki in range(track.count-1):
                        raw=source[record+12*ki:record+12*ki+12]
                        for offset,value in enumerate(raw):decoder.put(A+offset,value,1)
                        decoder.run(0x1c84d0 if kind==0 else 0x1c85d0,(A,OUT))
                        width=4 if kind==0 else 3
                        assert [bits(track.keys[ki].value[k]) for k in range(width)]==decoder.floats(OUT,width)
                        flags,time=struct.unpack_from('<HH',raw,8)
                        assert track.keys[ki].time==time and track.keys[ki].hold==bool(flags&0x8000)
                        decoded+=1
        assert native.em_pose_playback_begin(C.byref(state),C.byref(bank),0,0)
        for _ in range(5):assert native.em_pose_playback_advance(C.byref(state),1,0)
        checks=0;errors=[]
        def check(bone,label,actual,address):
            nonlocal checks
            expected=struct.unpack_from('<I',ram,address)[0];checks+=1
            if actual!=expected:errors.append((bone,label,hex(actual),hex(expected)))
        for bone in range(21):
            node=struct.unpack_from('<I',ram,actor+0x110+4*bone)[0]
            rot,trans,scale=state.nodes[bone]
            for label,cursor,offset in [('translation',trans,0),('scale',scale,24)]:
                for k in range(3):
                    check(bone,label,bits(cursor.value[k]),node+offset+k*4)
                    check(bone,label+'_velocity',bits(cursor.velocity[k]),node+offset+12+k*4)
            track=state.clip.contents.tracks[bone][0]
            for k in range(4):
                check(bone,'quatA',bits(track.keys[rot.index-1].value[k]),node+48+k*4)
                check(bone,'quatB',bits(track.keys[rot.index].value[k]),node+64+k*4)
            for label,v,offset in [('fraction',rot.fraction,80),('reciprocal',rot.reciprocal,84),('translation_time',trans.remaining,88),('scale_time',scale.remaining,92),('rotation_time',rot.remaining,96)]:check(bone,label,bits(v),node+offset)
            assert (rot.index,trans.index,scale.index)==struct.unpack_from('<3H',ram,node+0x66)
        report={'decoded_original_keys':decoded,'float_comparisons':checks,'different':len(errors),'examples':errors[:30]}
        out=ROOT/'build/player_pose_channels';out.mkdir(exist_ok=True)
        (out/'captured_idle.json').write_text(json.dumps(report,indent=2)+'\n')
        assert not errors,report
        # Channel agreement is exact. Hierarchy/owner composition here uses
        # the established finite host matrix path, so report its measured
        # world-matrix error separately rather than claiming an EE VU oracle.
        sys.path.insert(0,str(ROOT.parent/'Extermination/tools'))
        from export_opening_actors import matrix_multiply
        from export_native import mat_identity
        native.em_pose_channels_matrix.argtypes=[C.POINTER(C.c_float),C.POINTER(Pose)]
        poses=(Pose*64)();assert native.em_pose_playback_channels(C.byref(state),poses)
        owner=[struct.unpack_from('<4f',ram,actor+0xd0+i*16) for i in range(4)]
        matrices=[];matrix_error=0
        for bone in range(21):
            node=struct.unpack_from('<I',ram,actor+0x110+4*bone)[0]
            assert struct.unpack_from('<6f3h',ram,node+0x70)==(0,0,0,0,0,0,4096,4096,4096)
            out=(C.c_float*16)();native.em_pose_channels_matrix(out,C.byref(poses[bone]))
            local=mat_identity() if bone==0 else [list(out)[i*4:i*4+4] for i in range(4)]
            parent=bank.parents[bone]
            world=matrix_multiply(owner if parent<0 else matrices[parent],local)
            matrices.append(world)
            expected=struct.unpack_from('<16f',ram,node+0x90)
            matrix_error=max(matrix_error,max(abs(a-b) for a,b in zip(expected,[v for col in world for v in col])))
        assert matrix_error<.0001,matrix_error
        report['matrix_max_error']=matrix_error
        report['additional_captures']=[captured_channels(native,bank,name) for name in
            ('panel/eeMemory.bin','playable_ee.bin','panel/animation_ee.bin','handoff_ee.bin')]
        (out_dir:=ROOT/'build/player_pose_channels').mkdir(exist_ok=True)
        (out_dir/'captured_idle.json').write_text(json.dumps(report,indent=2)+'\n')
        # Exercise loop and terminal metadata through every exported clip;
        # this is structural coverage, not another original-state proof.
        poses=(Pose*64)()
        for index in range(bank.clip_count):
            clip=bank.clips[index]
            assert native.em_pose_playback_begin(C.byref(state),C.byref(bank),clip.id,0)
            for frame in range(clip.duration*2+2):
                assert native.em_pose_playback_channels(C.byref(state),poses)
                assert native.em_pose_playback_advance(C.byref(state),1,0)
            assert bool(state.flags&0x1000)==(clip.next==-2)
        loop_count = bank.clip_count
        native.em_pose_bank_free(C.byref(bank))
        print(f'pose bank captured original PASS:{decoded} decoded keys,2100 channel floats,'
              f'252 key cursors;{loop_count} clip loop/end paths')
        print('native hierarchy capture matrix errors:',[(r['clip'],r['frame'],r['native_hierarchy_matrix_max_error']) for r in report['additional_captures']])
if __name__=='__main__':main()
