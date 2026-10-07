#!/usr/bin/env python3
"""AREA01 bug-hit selection and the effect-07 production packet binding.

The six existing class-2/model-0 records and their hull descriptors come
from the fifteen pre-exit first-visit captures. Hit positions/directions are fixture
inputs; they do not establish a native gameplay hit. The shared hit owner
is checked at every external call and store boundary against original
001B41F0. Allocation/audio are explicit boundaries. The separate effect
test executes original 001ED7A0 and all transform/packet callees, comparing
the production adapter's entire RAM and scratchpad result.
"""
import ctypes as C
import json
import struct

import test_area00_world_reference as W
import test_area01_flame_services_reference as F
import test_area01_render_reference as R
from export_area01_water_effects import EXTRA
from export_effect_tables import elf_block
from test_player_slide_reference import read_elf

BUGS = tuple(0x7A8250 + i * 0x2F0 for i in range(6))
ROWS = (0x257360, 0x2573F0, 0x257480)


def main():
    elf, native = read_elf(), F.build()
    W.ELF, W.NATIVE = elf, W.build_native()
    W.BRANCH_PCS = W.branch_pcs(W.ExEE(elf).mem)
    native.fs_splash.argtypes = [C.c_void_p, C.c_void_p, C.c_uint32, C.c_uint32, C.c_int32]
    native.fs_splash_deny_descriptors.argtypes = [C.c_int]
    assert native.fs_load() == 0
    for address, size in EXTRA:
        pointer = native.fs_window(address, size)
        assert pointer and C.string_at(pointer, size) == elf_block(elf, address, size)
    native.fs_unload()
    target_cases = packet_cases = calls = 0
    chain_bytes = None
    inventory = []
    beats = [b for b in R.BEATS if not b.startswith('a01_07')]
    for beat in beats:
        base, scratch = R.image(beat)
        for address, size in ((0x1B41F0, 0x61C), (0x1ED7A0, 0x310)):
            offset = address - 0x100000 + 0x300
            assert base[address:address + size] == elf[offset:offset + size]
        # The original 001A6440 combines each face's attribute byte with
        # actor+5D and FE. Check the actual chain in every capture.
        chain = bytes(base[0x11349C0:0x1134C1C])
        if chain_bytes is None:
            chain_bytes = chain
        assert chain == chain_bytes, (beat, 'bug hull changed')
        assert struct.unpack_from('<I', chain)[0] == 5
        for offset in (4, 0x7C, 0xF4, 0x16C, 0x1E4):
            assert chain[offset:offset + 3] == bytes((0, 1, 0))
        W.IMAGES.clear()
        W.IMAGES[beat] = (base, scratch)
        for actor in BUGS:
            assert base[actor] == 1 and base[actor + 2] & 0x1F == 2 and base[actor + 3] == 0
            assert F.u32(base, actor + 0x10) == 0x128C10
            assert F.u32(base, actor + 0x58) == 0x11349C0
            assert base[actor + 0x5D] == 1
            assert all(chain[o + 1] & base[actor + 0x5D] & 0xFE == 0
                       for o in (4, 0x7C, 0xF4, 0x16C, 0x1E4))
            # A valid hit input at the target position. The actual shot's
            # normal/direction is a caller input, not inferred from a replay.
            writes = [(0x700038B0, bytes(base[actor + 0xA0:actor + 0xB0])),
                      (0x700038C0, struct.pack('<4f', 0, 0, -1, 0))]
            case = W.case(f'{beat} bug {actor:08X}', 0x1B41F0,
                          (actor, 0x700038B0, 0x700038C0, 0, 0, 5), beat=beat, writes=writes)
            ram, spr = W.prepared(case)
            original, log, _, _, _ = W.oracle_run(case, ram, spr)
            effects = [row[2] for row in log if row[0] == '0x1efd90']
            sounds = [row[2][1] for row in log if row[0] == '0x1fc580']
            assert len(effects) == 1 and effects[0][0] & 0xFFFFFFFF == 0x80000007, (case['name'], effects)
            assert sounds and sounds[0] in (0x15A, 0x15B), (case['name'], sounds)
            assert original.mem[actor] == 3
            assert struct.unpack_from('<H', original.mem, actor + 0x36)[0] == 5
            result = W.run_case(case)
            assert result is not None
            calls += result[1]
            target_cases += 1
        nodes = R.pool(base, {0x1EA240, 0x1E3D90})[:2]
        assert nodes, (beat, 'no captured effect transform')
        for node in nodes:
            for variant in range(2):
                ram, spr = bytearray(base), bytearray(scratch)
                F.put(ram, 0x275C34, node + 0x1F0)
                F.put(ram, 0x811CC0 + 0x18, 0x300000)
                ram[0x300000:0x310000] = bytes(0x10000)
                ram[0x7635C0:0x76B5C0] = bytes(0x8000)
                F.put(ram, node + 0x1F4, (0, 0xFFFFFFFF)[variant])
                F.put(ram, node + 0x1F8, R.F((0.0, 0.125)[variant]))
                F.put(ram, node + 0x244, R.F((0.25, 0.875)[variant]))
                # The worker must initialize these fields, independently of
                # a capture's previous effect handler and table state.
                for row in ROWS:
                    ram[row + 0x20:row + 0x40] = bytes((0xA5 ^ variant,)) * 0x20
                expected, fixture = F.oracle(elf, ram, spr), F.Fixture(ram, spr)
                R.oracle_call(expected, 0x1ED7A0, (node + 0xD0, 0x2000))
                assert native.fs_splash(fixture.ram, fixture.spr, node, 0x1ED7A0, 0x2000) == 0
                F.compare(fixture, expected, (beat, hex(node), variant, 'effect 07'))
                assert F.u32(expected.mem, 0x811CC0 + 0x18) > 0x300000, 'no packets written'
                packet_cases += 1
        inventory.append({'capture': beat, 'bug_records': len(BUGS), 'hull_flags': 0,
                          'effect': '07', 'sound_ids': ['15A', '15B']})
    fixture = F.Fixture(base, scratch)
    native.fs_splash_deny_descriptors(1)
    assert native.fs_splash(fixture.ram, fixture.spr, nodes[0], 0x1ED7A0, 0x2000) < 0
    native.fs_splash_deny_descriptors(0)
    assert native.fs_splash(fixture.ram, fixture.spr, nodes[0] + 1, 0x1ED7A0, 0x2000) < 0
    report = {'captures': len(beats), 'target_hit_cases': target_cases,
              'ordered_call_boundaries': calls, 'actual_effect_packet_cases': packet_cases,
              'refusals': 2, 'native_gameplay_hit_observed': False, 'inventory': inventory}
    output = F.ROOT / 'build/level2-crashes/target-hit-reference/report.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS target hit: {target_cases} original bug-hit cases, {calls} ordered call '
          f'boundaries, {packet_cases} production effect packet chains, 2 refusals, '
          f'{len(beats)} captures')


if __name__ == '__main__':
    main()
