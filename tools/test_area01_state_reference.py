#!/usr/bin/env python3
"""AREA01 init over original instructions and canonical loader allocations.

The original 001E7780 runs its AREA01 dispatch into runtime 00823A50;
all non-stack stores and their order, six globals, every overlay data/BSS
byte, and untouched guard bytes are compared. No worker is stubbed on
these paths. A separate 002009E0 check runs the original memset, with
only FlushCache treated as a host boundary, against the existing loader.
Capture bytes seed state tests only; production reads loaded disc bytes.
"""
import ctypes as C
import hashlib
import struct
import subprocess
from pathlib import Path

import reference_mode as mode
from test_module_loader_reference import LoaderEE
from test_player_slide_reference import ELF_SHA256

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
OUT = ROOT / 'build/level2/area-state'
BASE, DATA, BSS, SIZE, GLOBALS = 0x823500, 0x828A00, 0x82CD00, 0x14AC80, 0x275C18
ENTRY, INIT = 0x1E7780, 0x823A50


class Oracle(LoaderEE):
    def __init__(self, elf, ram, spad):
        super().__init__(elf, ram, spad)
        self.ordered = []

    def save(self, address, value, size=4):
        if not 0x7F000000 <= address < 0x7F100000:
            self.ordered.append((address, size, value & ((1 << (8 * size)) - 1)))
        super().save(address, value, size)


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    target = OUT / 'bridge.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-shared', '-fPIC', '-DEM_AREA01_STATE_STORE_TRACE', '-Isrc',
                    'tests/area01_state_bridge.c', 'src/game/em_area01_state.c',
                    'src/game/em_status_scene_original.c', 'src/game/em_task.c',
                    '-o', str(target)], cwd=ROOT, check=True)
    lib = C.CDLL(str(target))
    lib.area_state_test_seed.argtypes = [C.c_char_p, C.c_uint32, C.c_char_p, C.c_char_p]
    for fn in ('area_state_test_view', 'area_state_test_loader'):
        getattr(lib, fn).argtypes = [C.c_uint32, C.c_uint32]
        getattr(lib, fn).restype = C.c_void_p
    lib.area_state_test_init.argtypes = [C.c_int, C.c_uint8, C.c_uint8]
    lib.area_state_test_fault.restype = C.c_uint32
    return lib


def main():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA256
    overlay = (DECOMP / 'extract/OVERLAY/AREA01.BIN').read_bytes()
    h = struct.unpack_from('<8I', overlay)
    assert h == (0x336F574D, 2, BASE, 0x54C0, 0x4300, SIZE, BSS, BSS)
    assert len(overlay) == 0x40 + h[3] + h[4] == 0x9800
    emsc = (ROOT / 'assets/area01/overlay_data.emsc').read_bytes()
    assert emsc[:20] == struct.pack('<5I', 0x43534D45, 1, DATA, DATA, 0x4300)
    assert emsc[20:] == overlay[DATA-BASE:]
    capdirs = [DECOMP / 'build/s87/route/15_level_exit']
    capdirs += sorted((DECOMP / 'build/s87/route_a01').glob('a01_*'))
    captures = []
    for cap in capdirs:
        ram = (cap / 'eeMemory.bin').read_bytes()
        if ram[BASE:BASE+8] != overlay[:8]:
            continue  # a01_07 already crossed into AREA00
        spad = (cap / 'scratchpad.bin').read_bytes()
        assert ram[INIT:INIT+64] == overlay[INIT-BASE:INIT-BASE+64], cap.name
        # 001E7780 ends immediately before 001E7C60. Loader clear and memset
        # are checked too because the zero-initialization proof executes them.
        for lo, hi in ((ENTRY, 0x1E7C60), (0x2009E0, 0x200A40), (0x121A28, 0x121BC0)):
            assert ram[lo:hi] == elf[lo-0x100000+0x300:hi-0x100000+0x300], cap.name
        captures.append((cap.name, ram, spad))
    assert captures
    lib = build()
    def native(address, size):
        pointer = lib.area_state_test_loader(address, size)
        assert pointer, ('missing loader span', hex(address), size)
        return C.string_at(pointer, size)

    guard = bytes(range(32))
    cases = [(i, direct, sub, poison) for i in range(len(captures))
             for direct, sub in ((1, 0), (0, 0), (0, 1), (0, 2), (0, 255))
             for poison in (False, True)]
    if mode.FULL:
        cases += [(0, 0, sub, True) for sub in range(256)]
    compared = 0
    for ci, direct, sub, poison in cases:
        name, ram, spad = captures[ci]
        o = Oracle(elf, ram, spad)
        # The resource image is the disc's loaded image. Captured BSS and
        # globals deliberately remain nonzero, to catch a rebuild-wide wipe.
        o.write(BASE, overlay)
        bss = ram[BSS:BSS+SIZE]
        globals_ = ram[GLOBALS:GLOBALS+24]
        if poison:
            bss = bytes(x ^ 0xA5 for x in bss)
            globals_ = bytes(x ^ 0x5A for x in globals_)
        o.write(BSS, bss + guard)
        o.write(GLOBALS, globals_)
        o.save(0x810700, 1, 1); o.save(0x810701, sub, 1)
        o.ordered.clear()
        assert lib.area_state_test_seed(overlay, len(overlay), bss + guard, globals_) == 0
        o.call(INIT if direct else ENTRY)
        assert lib.area_state_test_init(direct, 1, sub) == 0, (name, direct, sub)
        log = (C.c_uint32 * 64)()
        n = lib.area_state_test_stores(log)
        assert n <= 32
        actual = [(log[2*i], 4, log[2*i+1]) for i in range(n)]
        assert actual == o.ordered, (name, direct, sub, 'ordered stores', actual, o.ordered)
        assert native(BASE, len(overlay)) == o.read(BASE, len(overlay))
        for address, size in ((GLOBALS, 24), (DATA, 0x4300), (BSS, SIZE)):
            p = lib.area_state_test_view(address, size)
            assert p and C.string_at(p, size) == o.read(address, size), (name, hex(address))
            if address != GLOBALS:
                assert p == lib.area_state_test_loader(address, size), ('alias', hex(address))
            compared += size
        assert native(BSS+SIZE, 32) == guard
        # No original write can be hidden outside the three compared spans.
        assert all((GLOBALS <= a and a+n <= GLOBALS+24) or (BSS <= a and a+n <= BSS+SIZE)
                   for a, n, _ in o.ordered)

    # The original loader zeroes the entire BSS on a module load, before
    # 001D0660 / 001E7780. Exercise real 002009E0 and its memset, not a
    # capture assumption or a replacement clear in the new state provider.
    name, ram, spad = captures[0]
    o = Oracle(elf, ram, spad)
    poison = bytes([0xA5]) * SIZE + guard
    o.write(BASE, overlay); o.write(BSS, poison)
    flushes = []
    o.hooks[0x10BAA0] = lambda ee: flushes.append(ee.r[4])
    assert lib.area_state_test_seed(overlay, len(overlay), poison, bytes([0x5A])*24) == 0
    o.call(0x2009E0, (BASE, len(overlay)))
    assert flushes == [2], flushes
    assert lib.area_state_test_clear() == 0
    assert native(BASE, len(overlay)) == overlay
    # Adjacent original addresses need not share a host allocation. Compare
    # the whole BSS and guard separately; never dereference a refused span.
    assert native(BSS, SIZE) == o.read(BSS, SIZE)
    assert native(BSS+SIZE, 32) == o.read(BSS+SIZE, 32)
    assert o.read(BSS, SIZE) == bytes(SIZE) and o.read(BSS+SIZE, 32) == guard

    # Shared alias, moved loader allocation, rebuild persistence, and bounds.
    p = lib.area_state_test_view(BSS, SIZE)
    assert p
    C.memset(p, 0xA7, SIZE)
    assert lib.area_state_test_move_bss() == 0
    q = lib.area_state_test_view(BSS, SIZE)
    assert q and q != p and q == lib.area_state_test_loader(BSS, SIZE)
    assert C.string_at(q, SIZE) == bytes([0xA7])*SIZE
    assert lib.area_state_test_init(0, 1, 0) == 0
    expected = bytearray([0xA7])*SIZE
    expected[0x54:0x5C] = bytes(8)
    assert C.string_at(q, SIZE) == expected
    data = lib.area_state_test_view(DATA, 0x4300)
    assert data
    C.memset(data+0x123, 0xC9, 1)
    assert lib.area_state_test_init(0, 1, 1) == 0
    assert native(DATA+0x123, 1) == b'\xC9'
    for a, n in ((BSS-1, 2), (BSS+SIZE, 1), (BSS, SIZE+1), (GLOBALS-1, 2),
                 (GLOBALS+24, 1), (DATA, 0), (0xFFFFFFFF, 2), (BASE, 0x20)):
        assert not lib.area_state_test_view(a, n), (hex(a), n)
    assert lib.area_state_test_fault() == 0
    # Missing BSS during a dispatch preserves the twelve preceding original
    # stores, then fails at the first record store; nothing follows it.
    lib.area_state_test_drop_bss()
    assert not lib.area_state_test_view(BSS, 4)
    assert lib.area_state_test_init(0, 1, 0) == -1
    assert lib.area_state_test_fault() == BSS+0x54
    log = (C.c_uint32 * 64)()
    assert lib.area_state_test_stores(log) == 12
    assert lib.area_state_test_init(1, 1, 0) == -1
    assert lib.area_state_test_stores(log) == 0
    assert lib.area_state_test_seed(overlay, len(overlay), poison, bytes(24)) == 0
    lib.area_state_test_drop_bss()
    assert lib.area_state_test_rebind() == -1 and lib.area_state_test_fault() == BSS
    assert lib.area_state_test_init(1, 1, 0) == -1
    assert not lib.area_state_test_view(GLOBALS, 24)
    # Replaced module header and unsupported area are fail-stop too.
    assert lib.area_state_test_seed(overlay, len(overlay), poison, bytes(24)) == 0
    header_word = lib.area_state_test_loader(BASE+4, 4)
    assert header_word
    C.memset(header_word, 0, 4)
    assert not lib.area_state_test_view(GLOBALS, 24)
    assert lib.area_state_test_init(0, 1, 0) == -1
    assert lib.area_state_test_seed(overlay, len(overlay), poison, bytes(24)) == 0
    assert lib.area_state_test_init(0, 11, 0) == -1
    assert lib.area_state_test_fault() == ENTRY
    for offset in range(0, 0x20, 4):
        bad = bytearray(overlay)
        bad[offset] ^= 1
        assert lib.area_state_test_seed(bytes(bad), len(bad), poison, bytes(24)) == -1
        assert lib.area_state_test_fault() == BASE
    assert lib.area_state_test_seed(overlay, len(overlay), poison, bytes(24)) == 0
    lib.area_state_test_shorten_data()
    assert lib.area_state_test_rebind() == -1 and lib.area_state_test_fault() == DATA
    lib.area_state_test_shutdown()
    mode.banner(f'{len(captures)} captures', f'{len(cases)} init cases')
    print(f'area01 state: PASS ordered original stores; {compared:,} compared bytes; '
          f'original BSS clear {SIZE:,} bytes; alias/lifetime/bounds/fault checks')


if __name__ == '__main__':
    main()
