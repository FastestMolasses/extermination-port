#!/usr/bin/env python3
"""Verify AREA01 reads the frame loop's actual counter and refuses writes.

The generated test-only seed writes the frame owner's storage. Production
uses only the new const accessor; this test compiles both actual owners.
"""
import ctypes as C
import json
import os
import random
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'build/level2-crashes/frame-counter-view'
FRAME = '''#include "game/em_frame.c"
void counter_seed(uint32_t value) { s_frame.counter = value; }
'''
VIEW = '''#include "game/em_area01_live.c"
EmGameState g;
uintptr_t counter_view(uint32_t address,uint32_t size,int write) {
    EmArea01Live live={.bound=1};
    return (uintptr_t)em_area01_live_bytes(&live,address,size,write);
}
'''
# No test case may reach another address owner. These link-only providers
# abort if the actual view routes any tested span away from the frame owner.
UNUSED_PROVIDERS = '''
em_area01_actor_view_bytes em_area01_collision_view_bytes
em_area01_collision_view_owns em_area01_model_slot_bytes
em_area01_player_view_bytes em_area01_scene_view em_area01_script_bytes
em_area01_state_bytes em_area11_boxes_slot_world
em_area11_interaction_host_scan_memory em_area11_roger_door_bank_rest
em_area11_roger_door_bank_word em_area11_roger_library_rest
em_area11_roger_library_word em_camera_live_bytes em_camera_live_scratch_bytes
em_collision_world_contact_bytes em_effects_live_d275C04
em_effects_live_scratch_3660 em_effects_live_window em_module_loader_memory
em_module_loader_state em_owner_draw_live_memory em_rcl_bytes em_rcl_bytes_mut
em_message_live_block player_pose_record_host
'''.split()


def original_reference():
    # Reuse the existing instruction oracle and actual math owner. Its full
    # duct-capture cases include low-byte zero/nonzero/0x80, neighboring-byte
    # poison, and the original read after a callee changes the counter byte.
    os.environ['EM_TEST_FULL'] = '1'
    sys.path.insert(0, str(ROOT/'tools'))
    import test_area01_math_reference as M
    M.LIB = M.build_native()
    M.ELF = M.read_elf()
    counts = M.route_cases(M.ELF, ['a01_s5_duct'], {0x1C02E0}, {0x1C02E0})
    assert counts['route'] and counts['perturb']
    st = M.STATS[0x1C02E0]
    report = dict(status='PASS', routine='001C02E0', capture='a01_s5_duct',
                  counts=counts, cases=st['cases'], boundaries=st['calls'],
                  branch_outcomes=len([o for o in st['outcomes']
                                       if 0x1C02E0 <= o[0] < 0x1C02E0+M.SPECS[0x1C02E0].size]),
                  scope='full perturbations including low-byte zero, nonzero, 0x80, neighboring byte poison, and read after callee write')
    (OUT/'owner-report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    frame, view, library = OUT/'frame.c', OUT/'view.c', OUT/'view.dylib'
    traps = OUT/'unused-providers.c'
    frame.write_text(FRAME); view.write_text(VIEW)
    traps.write_text('#include <stdlib.h>\n' + ''.join(
        f'void {name}(void) {{ abort(); }}\n' for name in UNUSED_PROVIDERS))
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-shared', '-fPIC', '-Isrc', '-Wl,-undefined,dynamic_lookup', '-Wl,-dead_strip',
                    '-Wl,-exported_symbol,_counter_seed', '-Wl,-exported_symbol,_counter_view',
                    '-Wl,-exported_symbol,_em_frame_counter', '-Wl,-exported_symbol,_em_frame_counter_storage',
                    str(frame), str(view), str(traps), '-o', str(library)], cwd=ROOT, check=True)
    native = C.CDLL(str(library))
    native.counter_seed.argtypes = [C.c_uint32]
    native.counter_view.argtypes = [C.c_uint32, C.c_uint32, C.c_int]
    native.counter_view.restype = C.c_size_t
    native.em_frame_counter.restype = C.c_uint32
    native.em_frame_counter_storage.restype = C.c_size_t
    address = native.em_frame_counter_storage()
    rng = random.Random(0x70003B64)
    cases = [0, 1, 0x7F, 0x80, 0xFF, 0x100, 0x101, 0xFFFFFFFF, 0] + [rng.getrandbits(32) for _ in range(256)]
    reads = refusals = 0
    for value in cases:
        native.counter_seed(value)
        assert native.em_frame_counter() == value
        assert native.em_frame_counter_storage() == address
        expected = struct.pack('<I', value)
        for start in range(4):
            for size in range(1, 5-start):
                ptr = native.counter_view(0x70003B64+start, size, 0)
                assert ptr == address+start and C.string_at(ptr, size) == expected[start:start+size]
                assert native.counter_view(0x70003B64+start, size, 1) == 0
                reads += 1; refusals += 1
        for start, size in ((-1, 2), (-1, 5), (1, 4), (3, 2), (0, 5), (0, 0)):
            assert native.counter_view(0x70003B64+start, size, 0) == 0
            refusals += 1
    report = dict(status='PASS', counter_values=len(cases), alias_reads=reads, refusals=refusals,
                  scope='actual frame storage and actual AREA01 view, byte/halfword/word reads, wrap boundaries, writes and crossing spans refused')
    (OUT/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))
    original_reference()


if __name__ == '__main__':
    main()
