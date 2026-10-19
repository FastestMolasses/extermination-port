#!/usr/bin/env python3
"""Opt-in AREA00 crate roots and production lazy registry versus original code.

The registry reads the new ELF windows and borrowed overlay data. Other crate
workers retain the existing oracle's explicit scripted boundaries; this does
not certify native drawing, collision, sound or a complete AREA00 frame.
"""
from __future__ import annotations

import ctypes as C
import json
import os
from types import SimpleNamespace

import export_area00_common as A
import export_area00_runtime_tables as X
import test_area01_crate_registry_reference as R

OUT = A.ROOT / 'build/levels/area00-crate-registry-reference'


class Fixture:
    def __init__(self, ram, windows):
        self.regions = [(a, a + len(raw), C.create_string_buffer(raw)) for a, raw in windows]
        self.regions.append((0x826F80, 0x82D480, C.create_string_buffer(ram[0x826F80:0x82D480])))
        self.access, self.deny = [], None

        @R.VIEW
        def view(_, address, size, write):
            self.access.append((address, size, write))
            if write or address == self.deny:
                return None
            for start, end, data in self.regions:
                if start <= address and address + size <= end:
                    return C.addressof(data) + address - start
            return None
        self.view = view

    def bind(self, native):
        native.cr_registry_bind(self.view, None)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    elf = (A.DECOMP / 'config/SCUS_971.12').read_bytes()
    windows = X.blocks(elf)
    X.verify_windows(windows, X.captures(A.DECOMP))
    R.OUT = OUT / 'native'
    native = R.build()
    adapter = SimpleNamespace(em_crate_original_tick=native.cr_registry_tick)
    stats = dict(ticks=0, calls=0, children=0)
    pairs = A.all_captures()
    full = os.environ.get('EM_TEST_FULL') == '1'
    if not full:
        pairs = [next(pair for pair in pairs if pair[1] == sub) for sub in A.SUBS]
    selected = 0
    for capture, sub in pairs:
        ram = capture.ram
        for slot in range(256):
            address = 0x7A5640 + slot * 0x2F0
            if not ram[address] or R.u32(ram, address + 0x10) != 0x1551B0:
                continue
            world = R.T.World(elf, ram, address, None, 0, sub)
            crate = R.T.crate_from(world.o, address)
            if crate.link < 0 or not crate.placement & 1:
                continue
            selected += 1
            for busy in (False, True):
                world = R.T.World(elf, ram, address, None, 0, sub)
                crate = R.T.crate_from(world.o, address)
                crate.state = 0
                fixture = Fixture(ram, windows)
                fixture.bind(native)
                fixed = dict(allocate=[int(busy)], probe=[(4, 0, 0.0)], place=[list(crate.world)])
                R.T.tick_compare(adapter, world, crate, 17, fixed, stats,
                                 tag='AREA00 borrowed crate registry')
                R.T.check_code(elf, ram, world.o.pcs)
                assert not native.cr_registry_fault(), 'production registry fault'
                roots = [(0x24A850, 2, 0), (0x24D820, 4, 0)]
                assert fixture.access[:2] == ([] if busy else roots), 'unexpected lazy root reads'
                if not busy:
                    for denied in (0x24A850, 0x24D820):
                        blocked = Fixture(ram, windows)
                        blocked.deny = denied
                        blocked.bind(native)
                        assert not native.cr_registry_group(0, crate.link)
                        assert native.cr_registry_fault() == denied
                        before = list(blocked.access)
                        assert not native.cr_registry_group(0, crate.link)
                        assert blocked.access == before, 'registry fault was not sticky'
    assert selected, 'no captured AREA00 crate uses the registry'
    stats.update(result='passed', selected_owners=selected, captures=len(pairs), full=full)
    (OUT / 'receipt.json').write_text(json.dumps(stats, indent=2) + '\n')
    print(f'PASS AREA00 crate registry: {stats["ticks"]} original owner ticks, '
          f'{stats["calls"]} worker calls; lazy roots and sticky refusals')


if __name__ == '__main__':
    main()
