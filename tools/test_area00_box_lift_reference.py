#!/usr/bin/env python3
"""The live box break lift handler 001EBC30 (effect subtype 8) against the original.

001EBC30 is NEARMISS decomp C (src/func_001EBC30.c). AREA00's a00_09 reaches
it from the live effect pass when the barricade crate (model 0x50) breaks
(em_effects_live.c handler_001EBC30, through em_area00_hud's translation).
This executes the original 001EBC30 and every transform and packet callee
over retained AREA00 fork captures (each effect node's matrix, with the work
block's accumulator +0x244 and fraction +0x24C set to fixed variants and the
node's +0x38 word 0 and 1, both 001CFBE0 sources) and compares the
production adapter's whole RAM and scratchpad result (the lift matrix
0x70003400 included). The captures are test input only; no retained capture
holds a subtype-8 node, so this does not establish a native gameplay break.
Also checks the installed export carries D_00256280..D_0025639F equal to the
pinned ELF, and two refusals (no descriptors; not a node record).
"""
import ctypes as C
import struct

import reference_mode as mode
import test_area01_flame_services_reference as F
import test_area01_render_reference as R
from export_area00_runtime_tables import BOX_BREAK_WINDOWS
from export_effect_tables import elf_block
from test_player_slide_reference import read_elf

AREA00 = R.DECOMP / 'build/fork_refs/s87/route_a00'
BEATS = sorted(p.name for p in AREA00.iterdir()
               if p.name.startswith('a00_') and (p / 'eeMemory.bin').exists()) if AREA00.exists() else []
HANDLER = 0x1EBC30
VARIANTS = ((0.0, 0.0), (0.25, 0.5), (0.875, 0.999), (2.0, 0.0625))


def image(beat):
    return ((AREA00 / beat / 'eeMemory.bin').read_bytes(), (AREA00 / beat / 'scratchpad.bin').read_bytes())


def main():
    elf, native = read_elf(), F.build()
    native.fs_splash.argtypes = [C.c_void_p, C.c_void_p, C.c_uint32, C.c_uint32, C.c_int32]
    native.fs_splash_deny_descriptors.argtypes = [C.c_int]
    assert native.fs_load() == 0
    for address, size in BOX_BREAK_WINDOWS:
        pointer = native.fs_window(address, size)
        assert pointer and C.string_at(pointer, size) == elf_block(elf, address, size), 'export lacks the box break lift sources'
    native.fs_unload()
    usable = []
    for beat in BEATS:
        base, _ = image(beat)
        if R.pool(base, {0x1EA240, 0x1E3D90}):
            usable.append(beat)
    assert usable, 'no retained AREA00 capture with an effect transform'
    beats = mode.select(usable, 2, HANDLER, keep=lambda i, _b: i == 0)
    mode.banner(mode.part(len(beats), len(usable), 'AREA00 captures'))
    cases = 0
    for beat in beats:
        base, scratch = image(beat)
        for address, size in ((HANDLER, 0xE4), *BOX_BREAK_WINDOWS):
            offset = address - 0x100000 + 0x300
            assert base[address:address + size] == elf[offset:offset + size], (beat, hex(address))
        nodes = R.pool(base, {0x1EA240, 0x1E3D90})[:3]
        for node in nodes:
            for (accumulator, fraction), live, depth in ((v, l, d) for v in VARIANTS for l in (0, 1)
                                                          for d in (0x2000, 0x80)):
                    ram, spr = bytearray(base), bytearray(scratch)
                    F.put(ram, node + 0x38, live)
                    # 001EA240 (byte-matched) sets both before the handler.
                    F.put(ram, 0x275C30, node)
                    F.put(ram, 0x275C34, node + 0x1F0)
                    F.put(ram, 0x811CC0 + 0x18, 0x300000)
                    ram[0x300000:0x310000] = bytes(0x10000)
                    ram[0x7635C0:0x76B5C0] = bytes(0x8000)
                    F.put(ram, node + 0x244, R.F(accumulator))
                    F.put(ram, node + 0x24C, R.F(fraction))
                    expected, fixture = F.oracle(elf, ram, spr), F.Fixture(ram, spr)
                    R.oracle_call(expected, HANDLER, (node + 0xD0, depth))
                    assert native.fs_splash(fixture.ram, fixture.spr, node, HANDLER, depth) == 0, (beat, hex(node))
                    F.compare(fixture, expected, (beat, hex(node), accumulator, fraction, depth, 'box lift'))
                    assert F.u32(expected.mem, 0x811CC0 + 0x18) > 0x300000, 'no packets written'
                    cases += 1
    base, scratch = image(beats[0])
    node = R.pool(base, {0x1EA240, 0x1E3D90})[0]
    fixture = F.Fixture(bytearray(base), bytearray(scratch))
    native.fs_splash_deny_descriptors(1)
    assert native.fs_splash(fixture.ram, fixture.spr, node, HANDLER, 0x2000) < 0
    native.fs_splash_deny_descriptors(0)
    assert native.fs_splash(fixture.ram, fixture.spr, node + 1, HANDLER, 0x2000) < 0
    print(f'PASS box break lift 001EBC30: {cases} production effect packet chains against the original '
          f'instructions, {len(beats)} AREA00 captures, 2 refusals')


if __name__ == '__main__':
    main()
