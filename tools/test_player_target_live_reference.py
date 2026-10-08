#!/usr/bin/env python3
"""Use target adapter against original helpers over AREA01 captures.

The original 001AA410, 001AA2A0 and SDK sqrt execute without hooks. Captured
pool fields are projected by the actual native adapter; designed nearby
positions additionally exercise radius/height boundaries absent on the route.
"""
import ctypes as C
import itertools
import json
import struct
import subprocess
import time
import export_area01_common as A
import reference_mode as mode
from test_player_fall_reference import FallEE
from test_player_slide_reference import bits, flt

OUT = A.ROOT / 'build/level2-crashes/target-reference'
PLAYER = 0x8102B0


def main():
    start = time.monotonic()
    OUT.mkdir(parents=True, exist_ok=True)
    library = OUT / 'target.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-shared', '-fPIC', '-Isrc', 'tests/player_target_live_bridge.c',
                    'src/game/em_player_target_live.c', 'src/game/em_level14_port_boot.c',
                    'src/game/em_sdk_math_original.c', '-o', str(library)], cwd=A.ROOT, check=True)
    lib = C.CDLL(str(library))
    U = C.c_uint32
    lib.pt_call.argtypes = [C.c_void_p, C.c_void_p, U, U, C.POINTER(U)]
    elf = A.read_elf()
    captures = A.captures()
    if not mode.FULL:
        captures = [next(c for c in captures if c.path.name == 'a01_s0_npc_first_talk')]
    count = recorded = 0
    forms = set()
    for cap in captures:
        for entry, size in ((0x1AA410, 0xC8), (0x1AA2A0, 0x164)):
            assert cap.ram[entry:entry + size] == elf[entry - 0x100000 + 0x300:entry - 0x100000 + 0x300 + size]
        listed = A.u32(cap.ram, 0x275B8C)
        n = struct.unpack_from('<h', cap.ram, 0x275B94)[0]
        actors = [A.u32(cap.ram, listed + i * 4) for i in range(n)]
        cases = [(address, None) for address in actors]
        # A real captured node is retained; only the stated fields are designed.
        node = actors[0] if actors else 0x7A8540
        variants = itertools.product((0, 1, 2, 4, 5, 6, 7, 8, 12, 255),
                                     (-13.01, -13., -8.01, -8., 0., 10., 10.01, 30., 30.01),
                                     (19.99, 20., 20.01, 30., 35., 35.01))
        chosen = list(variants)
        if not mode.FULL:
            chosen = mode.select(chosen, 65, 0x1AA2A0, axes=(lambda v: v[0], lambda v: v[1]))
        cases += [(node, variant) for variant in chosen]
        for address, variant in cases:
            original = FallEE(elf, cap.ram, cap.spad)
            original.save(0x26C5D0, 1)
            if variant:
                kind, height, distance = variant
                original.save(address + 2, 2, 1)
                original.save(address + 3, kind, 1)
                original.save(address + 0x34, 0xA5A58001)
                original.write(PLAYER + 0xA0, struct.pack('<3f', 0, 0, 0))
                original.write(address + 0xB0, struct.pack('<3f', distance, height, 0))
            player = original.read(PLAYER, 0x320)
            actor = original.read(address, 0x2F0)
            original.call(0x1AA410, (address,))
            radius = original.f[0]
            original.call(0x1AA2A0, (PLAYER, address), (flt(radius),))
            out = (U * 8)()
            assert lib.pt_call(player, actor, address, radius, out) == 0
            field34 = struct.unpack_from('<h', actor, 0x34)[0] if actor[2] & 31 == 2 else 0
            expected = (actor[2], actor[3], field34 & 0xFFFFFFFF,
                        A.u32(actor, 0xB0), A.u32(actor, 0xB8), radius,
                        original.load(0x70003A20), original.r[2])
            assert tuple(out) == expected, (cap.path.name, hex(address), variant, tuple(out), expected)
            assert original.read(PLAYER, 0x320) == player and original.read(address, 0x2F0) == actor
            count += 1
            recorded += variant is None
            forms.add((actor[2], actor[3], field34))
    report = dict(status='PASS', captures=len(captures), cases=count, captured_list_entries=recorded,
                  owner_forms=len(forms), seconds=round(time.monotonic() - start, 3))
    (OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
