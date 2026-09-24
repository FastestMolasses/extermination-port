#!/usr/bin/env python3
"""Compare the actual native panel camera hook with the local original capture.

Requires the ignored original animation snapshot and AREA11 collision export.
This is a captured-scene regression, not exhaustive DD20/geometry equivalence.
The probe's queries run over the collision world of the captured scene: the
original's own cell directory and published class-4 list, read from the
capture (census L06b/L08; tests/camera_interaction_fixture.c).
"""
import ctypes as C
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]


def main():
    assert sys.platform=='darwin','This captured-scene host harness uses the macOS linker'
    capture=ROOT.parent/'Extermination/build/startup-reference/panel/animation_ee.bin'
    world=ROOT/'assets/scene_snow/snow.emcl'
    assert capture.is_file() and world.is_file(),'Generate the local reference assets first'
    output=ROOT/'build/camera_interaction';output.mkdir(parents=True,exist_ok=True)
    library=output/'fixture.dylib'
    subprocess.run(['cc','-dynamiclib','-Wl,-undefined,dynamic_lookup','-Wl,-dead_strip',
        '-Wl,-exported_symbol,_test_retarget','-Wl,-exported_symbol,_test_refusal','-O1','-g','-Isrc',
        'tests/camera_interaction_fixture.c','src/game/em_camera.c',
        'src/game/em_camera_probe.c','src/game/em_camera_retarget.c','src/game/em_camera_rotation.c',
        'src/game/em_collision.c','src/game/em_collision_world.c','src/game/em_actor_collision.c',
        'src/game/em_actor_pool.c','src/game/em_coll_probe_original.c','src/game/em_coll_segment_walkers.c',
        'src/game/em_coll_list_passes.c','src/game/em_coll_list_passes_walkers.c',
        'src/game/em_sdk_math_original.c','src/game/em_sdk_soft_float.c','src/game/em_effect_original.c',
        '-lm','-o',str(library)],cwd=ROOT,check=True)
    native=C.CDLL(str(library))
    native.test_retarget.argtypes=[C.c_char_p,C.c_char_p,C.c_char_p,C.POINTER(C.c_float)]
    result=(C.c_float*12)()
    # The captured scene's own cell directory is copied here (build/, ignored).
    cells=output/'panel_cells.bin'
    assert native.test_retarget(str(capture).encode(),str(world).encode(),str(cells).encode(),result)==1
    ram=capture.read_bytes();camera=0x8101E0
    expected=[struct.unpack_from('<f',ram,camera+offset+i*4)[0]
              for offset in (0x10,0x20) for i in range(3)]
    expected.extend(struct.unpack_from('<f',ram,camera+offset)[0] for offset in (0x50,0x54))
    expected.extend((ram[camera+7],struct.unpack_from('<H',ram,camera+0x5A)[0],
                     ram[camera+0x6D],struct.unpack_from('<f',ram,camera+0x60)[0]))
    assert list(result[:6])==expected[:6],('eye/target',list(result),expected)
    assert list(result[8:])==expected[8:],('probe state',list(result),expected)
    bound_error=max(abs(result[i]-expected[i]) for i in (6,7))
    assert bound_error<=0.00006103515625,('bounds',list(result),expected)
    report={'eye_and_target_exact':True,'hit_probe_ground_and_overhead_exact':True,
            'max_bound_error':bound_error,'rotation_domain':'zero Euler AREA11 panel',
            'general_DD20_or_collision_equivalence':False}
    (output/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print('captured original panel camera fixture PASS',json.dumps(report))
    refusal=ROOT.parent/'Extermination/build/startup-reference/elevator/refusal/eeMemory.bin'
    assert refusal.is_file(),'Capture original refusal in fresh slot13 first'
    native.test_refusal.argtypes=[C.c_char_p,C.c_char_p,C.c_char_p,C.POINTER(C.c_float)]
    cells=output/'refusal_cells.bin'
    assert native.test_refusal(str(refusal).encode(),str(world).encode(),str(cells).encode(),result)==1
    ram=refusal.read_bytes()
    expected=[struct.unpack_from('<f',ram,camera+offset+i*4)[0]
              for offset in (0x10,0x20) for i in range(3)]
    expected.extend(struct.unpack_from('<f',ram,camera+offset)[0] for offset in (0x50,0x54))
    expected.extend((ram[camera+7],struct.unpack_from('<H',ram,camera+0x5A)[0],
                     ram[camera+0x6D],struct.unpack_from('<f',ram,camera+0x60)[0]))
    assert list(result[:6])==expected[:6],('refusal eye/target',list(result),expected)
    assert list(result[8:11])==expected[8:11],('refusal probe flags',list(result),expected)
    bound_error=max(abs(result[i]-expected[i]) for i in (6,7))
    assert bound_error<=0.00006103515625,('refusal bounds',list(result),expected)
    # The overhead point is 0019A910's hit (0018D330's ceiling probe) over
    # the captured collision world: since census L06b the translated walkers
    # (em_coll_segment_walkers) supply it, and it is exact (the port's own
    # walker, before, was one float ULP off here).
    overhead_error=abs(result[11]-expected[11])
    assert overhead_error==0,('refusal overhead',list(result),expected)
    report={'eye_and_target_exact':True,'hit_probe_and_ground_flags_exact':True,
            'max_bound_error':bound_error,'overhead_error':overhead_error,'distance':-20,
            'current_and_preset_distance':struct.unpack_from('<f',ram,camera+0xC)[0],
            'original_rotation':struct.unpack_from('<3f',ram,camera+0x30),
            'general_DD20_or_collision_equivalence':False}
    (output/'refusal.json').write_text(json.dumps(report,indent=2)+'\n')
    print('captured original elevator refusal camera fixture PASS',json.dumps(report))


if __name__=='__main__':main()
