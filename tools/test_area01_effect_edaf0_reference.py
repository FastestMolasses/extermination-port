#!/usr/bin/env python3
"""The live effect handler 001EDAF0 against the original.

001EDAF0 is byte-matched decomp C (src/func_001EDAF0.c). The AREA01
revisit's event reaches it from the live effect pass (decomp census: first
run a01r_01 f1682; em_effects_live.c handler_001EDAF0, through
em_area02_misc's translation). It rewrites +0x20..+0x3C of the three
descriptors D_00257510 / D_002575A0 / D_00257630, steps the work block's LCG
(+0x1F4) and eases its +0x1F8 toward 0.02, and submits each descriptor
through 001CFB50 / 001CFBE0.

This executes the original 001EDAF0 and every transform and packet callee
over retained AREA01 revisit fork captures (each effect node's matrix, with
the work block's accumulator +0x244, seed +0x1F4 and ease word +0x1F8 set to
fixed variants) and compares the production adapter's whole RAM and
scratchpad result (the rewritten descriptors included). The captures are
test input only. Also checks the installed export carries D_00257510 ..
D_002576BF equal to the pinned ELF, and two refusals (no descriptors; not a
node record).
"""
import ctypes as C

import reference_mode as mode
import test_area01_flame_services_reference as F
import test_area01_render_reference as R
from export_effect_tables import elf_block
from test_player_slide_reference import read_elf

AREA01R = R.DECOMP / 'build/fork_refs/s87/route_a01r'
BEATS = sorted(p.name for p in AREA01R.iterdir()
               if p.name.startswith('a01r_') and (p / 'eeMemory.bin').exists()) if AREA01R.exists() else []
HANDLER = 0x1EDAF0
WINDOW = (0x257510, 0x1B0)
# (accumulator +0x244, seed +0x1F4, ease word +0x1F8)
VARIANTS = ((0.0, 0, 0.5), (0.25, 0x12345678, 0.02), (2.0, 0xFFFFFFFF, 0.01), (0.875, 7, 1.0))


def image(beat):
    return ((AREA01R / beat / 'eeMemory.bin').read_bytes(), (AREA01R / beat / 'scratchpad.bin').read_bytes())


def main():
    elf, native = read_elf(), F.build()
    native.fs_splash.argtypes = [C.c_void_p, C.c_void_p, C.c_uint32, C.c_uint32, C.c_int32]
    native.fs_splash_deny_descriptors.argtypes = [C.c_int]
    assert native.fs_load() == 0
    pointer = native.fs_window(*WINDOW)
    assert pointer and C.string_at(pointer, WINDOW[1]) == elf_block(elf, *WINDOW), \
        'export lacks 001EDAF0\'s descriptors (export_area00_runtime_tables.py)'
    native.fs_unload()
    usable = [b for b in BEATS if R.pool(image(b)[0], {0x1EA240, 0x1E3D90})]
    assert usable, 'no retained AREA01 revisit capture with an effect transform'
    beats = mode.select(usable, 2, HANDLER, keep=lambda i, _b: i == 0)
    mode.banner(mode.part(len(beats), len(usable), 'AREA01 revisit captures'))
    cases = 0
    for beat in beats:
        base, scratch = image(beat)
        # The handler's code is the ELF's; its descriptors are not (the game
        # rewrites their +0x20.. words), so they are taken from the capture.
        offset = HANDLER - 0x100000 + 0x300
        assert base[HANDLER:HANDLER + 844] == elf[offset:offset + 844], (beat, 'handler code')
        for node in R.pool(base, {0x1EA240, 0x1E3D90})[:3]:
            for (accumulator, seed, ease), depth in ((v, d) for v in VARIANTS for d in (0x2000, 0x80)):
                ram, spr = bytearray(base), bytearray(scratch)
                # 001EA240 (byte-matched) sets both before the handler.
                F.put(ram, 0x275C30, node)
                F.put(ram, 0x275C34, node + 0x1F0)
                F.put(ram, 0x811CC0 + 0x18, 0x300000)
                ram[0x300000:0x310000] = bytes(0x10000)
                ram[0x7635C0:0x76B5C0] = bytes(0x8000)
                F.put(ram, node + 0x244, R.F(accumulator))
                F.put(ram, node + 0x1F4, seed)
                F.put(ram, node + 0x1F8, R.F(ease))
                expected, fixture = F.oracle(elf, ram, spr), F.Fixture(ram, spr)
                R.oracle_call(expected, HANDLER, (node + 0xD0, depth))
                assert native.fs_splash(fixture.ram, fixture.spr, node, HANDLER, depth) == 0, (beat, hex(node))
                F.compare(fixture, expected, (beat, hex(node), accumulator, seed, ease, depth, '001EDAF0'))
                assert F.u32(expected.mem, 0x811CC0 + 0x18) > 0x300000, 'no packets written'
                cases += 1
    base, scratch = image(beats[0])
    node = R.pool(base, {0x1EA240, 0x1E3D90})[0]
    fixture = F.Fixture(bytearray(base), bytearray(scratch))
    native.fs_splash_deny_descriptors(1)
    assert native.fs_splash(fixture.ram, fixture.spr, node, HANDLER, 0x2000) < 0
    native.fs_splash_deny_descriptors(0)
    assert native.fs_splash(fixture.ram, fixture.spr, node + 1, HANDLER, 0x2000) < 0
    print(f'PASS effect handler 001EDAF0: {cases} production effect packet chains against the original '
          f'instructions, {len(beats)} AREA01 revisit captures, 2 refusals')


if __name__ == '__main__':
    main()
