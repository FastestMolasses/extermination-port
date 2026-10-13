#!/usr/bin/env python3
"""Original 00824E00 against the AREA00 initializer over loader-owned bytes.

Compare every store in order, all six globals and the complete overlay and
BSS. The test does not replace or claim the common 001E7780 dispatcher.
"""
import ctypes as C
import hashlib
import struct
import subprocess
from pathlib import Path

import reference_mode as mode
import reference_root as refs
from test_area01_state_reference import Oracle
from test_player_slide_reference import ELF_SHA256

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
OUT = ROOT / 'build/level3/area-state'
BASE, DATA, BSS, SIZE, GLOBALS = 0x823500, 0x826F80, 0x82D480, 0x295900, 0x275C18
INIT = 0x824E00


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    target = OUT / 'bridge.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-shared', '-fPIC', '-DEM_AREA00_STATE_STORE_TRACE', '-Isrc',
                    'tests/area00_state_bridge.c', 'src/game/em_area00_state.c',
                    'src/game/em_status_scene_original.c', 'src/game/em_task.c',
                    '-o', str(target)], cwd=ROOT, check=True)
    lib = C.CDLL(str(target))
    lib.area00_state_test_seed.argtypes = [C.c_char_p, C.c_uint32, C.c_char_p, C.c_char_p]
    for name in ('area00_state_test_view', 'area00_state_test_loader'):
        getattr(lib, name).argtypes = [C.c_uint32, C.c_uint32]
        getattr(lib, name).restype = C.c_void_p
    lib.area00_state_test_globals.restype = C.c_void_p
    lib.area00_state_test_fault.restype = C.c_uint32
    return lib


def main():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA256
    overlay = (DECOMP / 'extract/OVERLAY/AREA00.BIN').read_bytes()
    assert struct.unpack_from('<8I', overlay) == (0x336F574D, 1, BASE, 0x3A40, 0x6500, SIZE, BSS, BSS)
    assert len(overlay) == 0x9F80
    capdirs = sorted(refs.capture('s87/route_a00').glob('a00_*'))
    captures = []
    for cap in capdirs:
        ram = (cap / 'eeMemory.bin').read_bytes()
        if ram[BASE:BASE+8] != overlay[:8]:
            continue  # progression exit and side return already loaded AREA01
        assert ram[INIT:INIT+64] == overlay[INIT-BASE:INIT-BASE+64], cap.name
        captures.append((cap.name, ram, (cap / 'scratchpad.bin').read_bytes()))
    assert captures
    selected = captures if mode.FULL else captures[:1] + captures[-1:]
    lib = build()
    guard = bytes(range(32))
    compared = 0
    cases = 0

    def loader(address, size):
        p = lib.area00_state_test_loader(address, size)
        assert p, ('missing loader allocation', hex(address), size)
        return C.string_at(p, size)

    for name, ram, spad in selected:
        for poison in (False, True):
            o = Oracle(elf, ram, spad)
            o.write(BASE, overlay)
            bss = ram[BSS:BSS+SIZE]
            globals_ = ram[GLOBALS:GLOBALS+24]
            if poison:
                bss = bytes(x ^ 0xA5 for x in bss)
                globals_ = bytes(x ^ 0x5A for x in globals_)
            o.write(BSS, bss + guard)
            o.write(GLOBALS, globals_)
            o.ordered.clear()
            assert lib.area00_state_test_seed(overlay, len(overlay), bss + guard, globals_) == 0
            # The shared storage is aliased directly, including host writes
            # made after binding. No second six-global owner is installed.
            assert lib.area00_state_test_view(GLOBALS, 24) == lib.area00_state_test_globals()
            o.call(INIT)
            assert lib.area00_state_test_init() == 0, name
            log = (C.c_uint32 * 16)()
            n = lib.area00_state_test_stores(log)
            actual = [(log[2*i], 4, log[2*i+1]) for i in range(n)]
            assert n == 6 and actual == o.ordered, (name, actual, o.ordered)
            assert loader(BASE, len(overlay)) == o.read(BASE, len(overlay))
            for address, size in ((GLOBALS, 24), (DATA, 0x6500), (BSS, SIZE)):
                p = lib.area00_state_test_view(address, size)
                assert p and C.string_at(p, size) == o.read(address, size), (name, hex(address))
                if address != GLOBALS:
                    assert p == lib.area00_state_test_loader(address, size), ('alias', hex(address))
                compared += size
            assert loader(BSS+SIZE, 32) == guard
            assert all(GLOBALS <= a and a + n <= GLOBALS + 24 for a, n, _ in o.ordered)
            cases += 1

    p = lib.area00_state_test_view(BSS, SIZE)
    assert p
    C.memset(p, 0xA7, SIZE)
    assert lib.area00_state_test_move_bss() == 0
    q = lib.area00_state_test_view(BSS, SIZE)
    assert q and q != p and q == lib.area00_state_test_loader(BSS, SIZE)
    assert C.string_at(q, SIZE) == bytes([0xA7]) * SIZE
    assert lib.area00_state_test_init() == 0
    assert C.string_at(q, SIZE) == bytes([0xA7]) * SIZE
    for a, n in ((BSS-1, 2), (BSS+SIZE, 1), (BSS, SIZE+1), (GLOBALS-1, 2),
                 (GLOBALS+24, 1), (DATA, 0), (0xFFFFFFFF, 2), (BASE, 0x20)):
        assert not lib.area00_state_test_view(a, n), (hex(a), n)
    assert lib.area00_state_test_fault() == 0
    lib.area00_state_test_drop_bss()
    assert not lib.area00_state_test_view(BSS, 4)
    assert lib.area00_state_test_rebind() == -1
    assert lib.area00_state_test_fault() == BSS
    assert lib.area00_state_test_init() == -1
    log = (C.c_uint32 * 16)()
    assert lib.area00_state_test_stores(log) == 0
    assert not lib.area00_state_test_view(GLOBALS, 24)

    bss = bytes(SIZE) + guard
    for offset in range(0, 0x20, 4):
        bad = bytearray(overlay)
        bad[offset] ^= 1
        assert lib.area00_state_test_seed(bytes(bad), len(bad), bss, bytes(24)) == -1
        assert lib.area00_state_test_fault() == BASE
    assert lib.area00_state_test_seed(overlay, len(overlay), bss, bytes(24)) == 0
    lib.area00_state_test_shorten_data()
    assert lib.area00_state_test_rebind() == -1
    assert lib.area00_state_test_fault() == DATA
    assert lib.area00_state_test_seed(overlay, len(overlay), bss, bytes(24)) == 0
    header = lib.area00_state_test_loader(BASE+4, 4)
    C.memset(header, 0, 4)
    assert not lib.area00_state_test_view(GLOBALS, 24)
    assert lib.area00_state_test_init() == -1
    assert lib.area00_state_test_fault() == GLOBALS+20
    lib.area00_state_test_shutdown()
    mode.banner(f'{len(selected)} of {len(captures)} captures', f'{cases} initializer cases')
    print(f'area00 state: PASS ordered original stores; {compared:,} compared bytes; '
          'shared globals / loader aliases / lifetime / bounds / fault checks')


if __name__ == '__main__':
    main()
