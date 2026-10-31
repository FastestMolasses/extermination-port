#!/usr/bin/env python3
"""Original 001E7780 / 0x8250C0 against AREA19 loader-owned state.

em_area01_state selects AREA19 (area byte 0x13): its overlay views are the
module loader's delivered data and BSS, the six boot globals the shared
owner's, and 001E7780's keys 0x1300 / 0x1301 run the area init 0x8250C0
(em_area01_state_008250C0). This runs the ORIGINAL 001E7780 (area 0x13, subs
0..3 and 0xFF) and 0x8250C0 directly over AREA19 fork captures of both subs
and compares the six globals, the overlay data and the whole BSS, with
poisoned and as-captured starting bytes. Also: the header checks and a wrong
area. No capture bytes or original instructions are exported.
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
OUT = ROOT / 'build/level19/area19-state'
BASE, DATA, DATA_SIZE, BSS, SIZE, GLOBALS = 0x823500, 0x829E00, 0x5A80, 0x82F880, 0x15ED80, 0x275C18
INIT = 0x8250C0
CAPTURES = ('levels-recordings-20261018-10/s87/route_a19/a19_00_duct',
            'levels-recordings-20261018-13/s87/route_a19b/a19b_02_ladder1023',
            'levels-recordings-20261018-17/s87/route_a19c/a19c_07_door52',
            'levels-recordings-20261018-19/s87/route_a19d/a19d_05_valve37')


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    target = OUT / 'bridge.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC', '-Isrc',
                    'tests/area19_state_bridge.c', 'src/game/em_area01_state.c', 'src/game/em_area00_state.c',
                    'src/game/em_area02_state.c', 'src/game/em_area02_overlay.c', 'src/game/em_area04_state.c',
                    'src/game/em_area22_port.c', 'src/game/em_area06_port.c', 'src/game/em_area06_port_node.c',
                    'src/game/em_area06_port_overlay.c', 'src/game/em_area06_port_strip.c', 'src/game/em_level10_port_boot.c',
                    'src/game/em_status_scene_original.c', 'src/game/em_task.c',
                    '-o', str(target)], cwd=ROOT, check=True)
    lib = C.CDLL(str(target))
    lib.area19_state_test_seed.argtypes = [C.c_char_p, C.c_uint32, C.c_char_p, C.c_char_p]
    lib.area19_state_test_view.argtypes = [C.c_uint32, C.c_uint32]
    lib.area19_state_test_view.restype = C.c_void_p
    lib.area19_state_test_dispatch.argtypes = [C.c_uint8, C.c_uint8]
    lib.area19_state_test_fault.restype = C.c_uint32
    return lib


def main():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA256
    overlay = (DECOMP / 'extract/OVERLAY/AREA19.BIN').read_bytes()
    assert struct.unpack_from('<8I', overlay) == (0x336F574D, 0x10, BASE, 0x68C0, DATA_SIZE, SIZE, BSS, BSS)
    captures = []
    for rel in CAPTURES:
        cap = DECOMP / 'build/fork_refs' / rel
        ram = (cap / 'eeMemory.bin').read_bytes()
        assert ram[BASE:BASE + 8] == overlay[:8], cap.name
        captures.append((cap.name, ram, (cap / 'scratchpad.bin').read_bytes()))
    selected = captures if mode.FULL else captures[:1] + captures[-1:]
    mode.banner(mode.part(len(selected), len(captures), 'AREA19 captures'))
    lib = build()
    compared = cases = 0
    for name, ram, spad in selected:
        for direct, sub, poison in [(d, s, p) for d, s in ((1, 0), (0, 0), (0, 1), (0, 2), (0, 3), (0, 255))
                                    for p in (False, True)]:
            o = Oracle(elf, ram, spad)
            o.write(BASE, overlay)
            bss, globals_ = ram[BSS:BSS + SIZE], ram[GLOBALS:GLOBALS + 24]
            if poison:
                bss = bytes(x ^ 0xA5 for x in bss)
                globals_ = bytes(x ^ 0x5A for x in globals_)
            o.write(BSS, bss)
            o.write(GLOBALS, globals_)
            o.save(0x810700, 0x13, 1); o.save(0x810701, sub, 1)
            assert lib.area19_state_test_seed(overlay, len(overlay), bss, globals_) == 0, name
            o.call(INIT if direct else 0x1E7780)
            rc = lib.area19_state_test_init() if direct else lib.area19_state_test_dispatch(0x13, sub)
            assert rc == 0, (name, direct, sub, hex(lib.area19_state_test_fault()))
            for address, size in ((GLOBALS, 24), (DATA, DATA_SIZE), (BSS, SIZE)):
                p = lib.area19_state_test_view(address, size)
                assert p and C.string_at(p, size) == o.read(address, size), (name, direct, sub, hex(address))
                compared += size
            cases += 1
    # Refusals: a changed header word, the wrong area.
    bss = bytes(SIZE)
    for offset in range(0, 0x20, 4):
        bad = bytearray(overlay)
        bad[offset] ^= 1
        assert lib.area19_state_test_seed(bytes(bad), len(bad), bss, bytes(24)) == -1
        assert lib.area19_state_test_fault() == BASE
    assert lib.area19_state_test_seed(overlay, len(overlay), bss, bytes(24)) == 0
    assert lib.area19_state_test_dispatch(1, 0) == -1 and lib.area19_state_test_fault() == 0x1E7780
    print(f'area19 state: PASS {cases} original 001E7780 / 0x8250C0 runs, {compared:,} compared bytes, '
          '8 header refusals, wrong-area refusal')


if __name__ == '__main__':
    main()
