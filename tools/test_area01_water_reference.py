#!/usr/bin/env python3
"""Water contact/runtime and ripple adapter against the original instructions.

Captured RAM is test input only. Effect allocation and audio submission are
explicit boundaries; ripple, conversion and scratch copy execute natively.
The existing effect and render oracles verify the boundary owners separately.
"""
import ctypes as C
import struct

import reference_mode as mode
import test_area01_runtime_reference as T
import test_area01_flame_services_reference as F
import test_area01_render_reference as R
from test_player_slide_reference import read_elf, STACK_TOP

PLAYER = 0x8102B0


def main():
    runtime, render, elf = T.build(), F.build(), read_elf()
    from export_area01_water_effects import EXTRA
    from export_effect_tables import elf_block
    assert render.fs_load() == 0
    for address, size in EXTRA:
        window = render.fs_window(address, size)
        assert window and C.string_at(window, size) == elf_block(elf, address, size)
    render.fs_unload()
    render.fs_splash.argtypes = [C.c_void_p, C.c_void_p, C.c_uint32, C.c_uint32, C.c_int32]
    cases = ripples = handlers = 0
    beats = mode.select([b for b in R.BEATS if not b.startswith('a01_07')], 3, 0x187DE0)
    beats = list(dict.fromkeys(['a01_02_shaft_landing', *beats]))
    for beat in beats:
        base, spad = R.image(beat)
        for address, size in ((0x187DE0, 0xB4), (0x1E8B90, 0x2EC), (0x1EB7F0, 0x190),
                              (0x1EC270, 0x180), (0x1281C0, 0x10), (0x1031E0, 0x20)):
            offset = address - 0x100000 + 0x300
            assert base[address:address + size] == elf[offset:offset + size]
        for depth in (1, 2):
            ram, spr = bytearray(base), bytearray(spad)
            ram[PLAYER + 0x23C] = depth
            # Inside the real first-visit water rectangle. The two depth
            # values exercise both first-contact sounds, independent of a
            # captured endpoint having left the water already.
            spr[0x31B0:0x31BC] = struct.pack('<3f', 5, -27.92, -995)
            expected, actual, errors = [], [], []
            e, f = F.oracle(elf, ram, spr), F.Fixture(ram, spr)

            def effect(o):
                args = tuple(o.r[i] & 0xFFFFFFFF for i in (4, 5, 6))
                expected.append((0x1EFD90, args, bytes(o.spad[0x38B0:0x38C0]),
                                 bytes(o.mem[PLAYER + 0xC0:PLAYER + 0xD0])))

            def sound(o):
                expected.append((0x1FB9F0, tuple(o.r[i] & 0xFFFFFFFF for i in (4, 5, 6, 7))))

            e.hooks = {0x1EFD90: effect, 0x1FB9F0: sound}
            R.oracle_call(e, 0x187DE0, (PLAYER,))

            @T.Worker
            def worker(_, cp):
                try:
                    c = cp.contents
                    assert c.sp == STACK_TOP - 0x20, hex(c.sp)
                    if c.function == 0x1031E0:
                        return render.fs_sdk(C.byref(f.host), cp)
                    if c.function == 0x1E8B90:
                        fault = C.c_uint32()
                        return render.fs_call(C.byref(f.host), cp, C.byref(fault))
                    args = tuple(c.a[i] & 0xFFFFFFFF for i in range(c.na))
                    if c.function == 0x1EFD90:
                        actual.append((c.function, args, bytes(f.spr[0x38B0:0x38C0]),
                                       bytes(f.ram[PLAYER + 0xC0:PLAYER + 0xD0])))
                    elif c.function == 0x1FB9F0:
                        actual.append((c.function, args))
                    else:
                        raise AssertionError(hex(c.function))
                    return 0
                except Exception as error:
                    errors.append(repr(error))
                    return -1

            f.host.worker = worker
            assert runtime.a01rt_bind(C.byref(f.host)) == 0
            c = T.Call(function=0x187DE0, sp=STACK_TOP, na=1)
            c.a[0] = PLAYER
            assert runtime.a01rt_call(C.byref(c)) == 0, (beat, errors, hex(runtime.a01rt_fault()))
            assert not errors and actual == expected, (beat, actual, expected, errors)
            F.compare(f, e, (beat, depth, 'contact'))
            assert any(e.mem[i] != ram[i] for i in range(0x82CD00, 0x836D60)), 'ripple made no grid writes'
            cases += 1
        for position in ((5, -27.92, -995), (-30, -27.92, -980), (32, -27.92, -1021.5)):
            for speed in (0, .03, .3, 1.0):
                ram, spr = bytearray(base), bytearray(spad)
                ram[PLAYER + 0xB0:PLAYER + 0xBC] = struct.pack('<3f', *position)
                e, f = F.oracle(elf, ram, spr), F.Fixture(ram, spr)
                R.oracle_call(e, 0x1E8B90, (PLAYER + 0xB0,), (R.F(speed),))
                rc, _, fault = f.call(render, 0x1E8B90, (PLAYER + 0xB0,), (R.F(speed),))
                assert rc == 0, hex(fault)
                F.compare(f, e, (beat, position, speed, 'ripple'))
                ripples += 1
        for node in R.pool(base, {0x1EA240})[:mode.pick(8, 2)]:
            ram, spr = bytearray(base), bytearray(spad)
            F.put(ram, 0x275C34, node + 0x1F0)
            F.put(ram, 0x811CC0 + 0x18, 0x300000)
            ram[0x300000:0x310000] = bytes(0x10000)
            ram[0x7635C0:0x76B5C0] = bytes(0x8000)
            e, f = F.oracle(elf, ram, spr), F.Fixture(ram, spr)
            for handler in (0x1EC270, 0x1EB7F0, 0x1ECB00):
                e, f = F.oracle(elf, ram, spr), F.Fixture(ram, spr)
                R.oracle_call(e, handler, (node + 0xD0, 0x2000))
                assert render.fs_splash(f.ram, f.spr, node, handler, 0x2000) == 0
                F.compare(f, e, (beat, node, hex(handler), 'live splash handler'))
                handlers += 1
    # A missing required canonical window must remain a failure.
    f = F.Fixture(base, spad)
    f.deny = (0x275C20, 4, 0)
    assert f.call(render, 0x1E8B90, (PLAYER + 0xB0,), (R.F(.3),))[0] < 0
    assert handlers, 'no recorded effect nodes tested'
    print(f'PASS water: {cases} original contact chains, {ripples} ripple adapters, '
          f'{handlers} live effect packet chains, missing-grid refusal')


if __name__ == '__main__':
    main()
