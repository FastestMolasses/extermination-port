#!/usr/bin/env python3
"""Original 001E7780 and the area inits against AREA15's and AREA03's state.

em_area01_state selects AREA15 (area 0x0F) and AREA03: their overlay views are
the module loader's delivered data and BSS, the six boot globals the shared
owner's, and 001E7780's keys 0xF00 / 0xF01 and 0x300 / 0x301 run the init at
0x823580. This runs the ORIGINAL 001E7780 (subs 0..3 and 0xFF) over the fork
captures of each area and compares the six globals, the overlay data and the
whole BSS, with poisoned and as-captured starting bytes; then the header checks
and a wrong area. No capture bytes or original instructions are exported.
"""
import ctypes as C
import hashlib
import struct
import subprocess
from pathlib import Path

import reference_mode as mode
from test_area01_state_reference import Oracle
from test_player_slide_reference import ELF_SHA256

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
OUT = ROOT / 'build/level15/later-state'
BASE, GLOBALS = 0x823500, 0x275C18
AREAS = {
    0x0F: ('AREA15', 0x0C, 0x3440, 0x826980, 0x3580, 0x829F00, 0x140C00,
           ('levels-recordings-20261018-19/s87/route_a15b/a15b_00_event',
            'levels-recordings-20261018-19/s87/route_a15b/a15b_02_scripts',
            'levels-recordings-20261018-19/s87/route_a15/a15_01_door51')),
    0x03: ('AREA03', 0x04, 0x33C0, 0x826900, 0x2800, 0x829100, 0x140C00,
           ('levels-recordings-20261018-26/s87/route_a19e/a19e_07_door25',
            'levels-recordings-20261018-26/s87/route_a03/a03_00_elevator')),
    0x08: ('AREA08', 0x08, 0x1BC0, 0x825100, 0x2E00, 0x827F00, 0x140C00,
           ('levels-recordings-20261030-1/s87/route_a08/a08_01_magazine',
            'levels-recordings-20261030-1/s87/route_a08/a08_03_slider',
            'levels-recordings-20261030-1/s87/route_a08/a08_05_terminal')),
    0x07: ('AREA07', 0x07, 0x2640, 0x825B80, 0x2500, 0x828080, 0x140C00,
           ('levels-recordings-20261030-1/s87/route_a07/a07_00_switch',)),
}


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    target = OUT / 'bridge.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC', '-Isrc',
                    'tests/later_state_bridge.c', 'src/game/em_area01_state.c', 'src/game/em_area00_state.c',
                    'src/game/em_area02_state.c', 'src/game/em_area02_overlay.c', 'src/game/em_area04_state.c',
                    'src/game/em_area22_port.c', 'src/game/em_area06_port.c', 'src/game/em_area06_port_node.c',
                    'src/game/em_area06_port_overlay.c', 'src/game/em_area06_port_strip.c', 'src/game/em_level10_port_boot.c',
                    'src/game/em_status_scene_original.c', 'src/game/em_task.c',
                    '-o', str(target)], cwd=ROOT, check=True)
    lib = C.CDLL(str(target))
    lib.later_state_test_seed.argtypes = [C.c_uint, C.c_char_p, C.c_uint32, C.c_char_p, C.c_uint32, C.c_uint32,
                                          C.c_char_p]
    lib.later_state_test_view.argtypes = [C.c_uint32, C.c_uint32]
    lib.later_state_test_view.restype = C.c_void_p
    lib.later_state_test_dispatch.argtypes = [C.c_uint8, C.c_uint8]
    lib.later_state_test_fault.restype = C.c_uint32
    return lib


def main():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA256
    lib = build()
    compared = cases = 0
    for area, (name, ident, text, data, data_size, bss_at, bss_size, caps) in AREAS.items():
        overlay = (DECOMP / f'extract/OVERLAY/{name}.BIN').read_bytes()
        assert struct.unpack_from('<8I', overlay) == (0x336F574D, ident, BASE, text, data_size, bss_size, bss_at,
                                                      bss_at)
        assert BASE + 0x40 + text == data and data + data_size == bss_at
        captures = []
        for rel in caps:
            cap = DECOMP / 'build/fork_refs' / rel
            ram = (cap / 'eeMemory.bin').read_bytes()
            if ram[BASE:BASE + 8] == overlay[:8]:
                captures.append((cap.name, ram, (cap / 'scratchpad.bin').read_bytes()))
        assert captures, name
        selected = captures if mode.FULL else captures[:1]
        mode.banner(mode.part(len(selected), len(captures), f'{name} captures'))
        for cap, ram, spad in selected:
            for sub, poison in [(s, p) for s in (0, 1, 2, 3, 4, 5, 6, 7, 255) for p in (False, True)]:
                o = Oracle(elf, ram, spad)
                o.write(BASE, overlay)
                bss, globals_ = ram[bss_at:bss_at + bss_size], ram[GLOBALS:GLOBALS + 24]
                if poison:
                    bss = bytes(x ^ 0xA5 for x in bss)
                    globals_ = bytes(x ^ 0x5A for x in globals_)
                o.write(bss_at, bss)
                o.write(GLOBALS, globals_)
                o.save(0x810700, area, 1)
                o.save(0x810701, sub, 1)
                assert lib.later_state_test_seed(area, overlay, len(overlay), bss, bss_at, bss_size, globals_) == 0
                o.call(0x1E7780)
                rc = lib.later_state_test_dispatch(area, sub)
                assert rc == 0, (cap, sub, hex(lib.later_state_test_fault()))
                for address, size in ((GLOBALS, 24), (data, data_size), (bss_at, bss_size)):
                    p = lib.later_state_test_view(address, size)
                    assert p and C.string_at(p, size) == o.read(address, size), (cap, sub, hex(address))
                    compared += size
                cases += 1
        bss = bytes(bss_size)
        for offset in range(0, 0x20, 4):
            bad = bytearray(overlay)
            bad[offset] ^= 1
            assert lib.later_state_test_seed(area, bytes(bad), len(bad), bss, bss_at, bss_size, bytes(24)) == -1
            assert lib.later_state_test_fault() == BASE
        assert lib.later_state_test_seed(area, overlay, len(overlay), bss, bss_at, bss_size, bytes(24)) == 0
        assert lib.later_state_test_dispatch(1, 0) == -1 and lib.later_state_test_fault() == 0x1E7780
    print(f'AREA15 / AREA03 state: PASS {cases} original 001E7780 runs, {compared:,} compared bytes, '
          '16 header refusals, two wrong-area refusals')


if __name__ == '__main__':
    main()
