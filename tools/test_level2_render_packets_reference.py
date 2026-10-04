#!/usr/bin/env python3
"""Run AREA01's live render-context packet composition against original code.

Uses canonical em_rcl storage and all real packet/depth workers. No actor
binder, VU1 presentation, or emulator process is involved. EM_TEST_FULL=1
runs every AREA01 sub-0 route snapshot; quick keeps arrival and three route
milestones plus the NPC's visible dynamic table.
"""
import ctypes as C
import os
import struct
import sys
import time
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode as RM
os.environ.setdefault('EM_TEST_JOBS', '4')
import test_render_context_live_reference as R
import test_area01_render_reference as A
from test_player_slide_reference import read_elf, RETURN

OUT = ROOT / 'build/area01/render_packets'
AREA = ROOT.parent / 'Extermination/build/s87/route_a01'
ARRIVAL = ROOT.parent / 'Extermination/build/s87/route/15_level_exit'
TRACE = (0x1D5BD0, 0x1D5A70, 0x1D4FC0, 0x1D5170, 0x1CAAC0, 0x1CB760)


def emsc(name):
    b = (ROOT / 'assets/area01/level' / (name + '.emsc')).read_bytes()
    magic, version, base, entry, size = struct.unpack_from('<4s4I', b)
    assert magic == b'EMSC' and version == 1 and base == entry and len(b) == 20 + size
    return base, b[20:]


class Pair(R.Pair):
    def __init__(self, capture, dynamic, dynbase):
        super().__init__(str(capture), False)
        ram = (capture / 'eeMemory.bin').read_bytes()
        assert ram[BANK_BASE:BANK_BASE + len(BANK)] == BANK, capture.name
        assert ram[dynbase:dynbase + len(dynamic)] == dynamic, capture.name
        assert struct.unpack_from('<I', ram, 0x28A5A4)[0] == dynbase
        A.check_code(ram, capture.name, A.A01EE(R.ELF))
        # The loader owns these bytes. The adapter borrows the actual pointer
        # word and table; it must not expose them as writable RCL storage.
        self.table_word = (C.c_uint8 * 4).from_buffer_copy(ram[0x28A5A4:0x28A5A8])
        self.dynamic = (C.c_uint8 * len(dynamic)).from_buffer_copy(dynamic)
        n = R.NATIVE
        assert n.em_rcl_dynamic_world_bind(self.table_word, self.dynamic, len(dynamic)) == 0
        assert not n.em_rcl_bytes_mut(0x28A5A4, 4)
        assert not n.em_rcl_bytes_mut(dynbase, len(dynamic))
        assert n.em_rcl_poke(0x28A5A4, bytes(4), 4) == -1
        assert n.em_rcl_poke(dynbase, bytes(4), 4) == -1
        self.dynamic[0] ^= 1
        assert n.em_rcl_bytes(dynbase, 1)[0] == self.dynamic[0], 'table must be a borrowed view'
        self.dynamic[0] ^= 1
        self.counts, self.clips = Counter(), Counter()
        for fn in TRACE:
            self.ee.hooks[fn] = self.traced(fn)

    def traced(self, fn):
        def call(ee):
            self.counts[fn] += 1
            ret = ee.r[31]
            del ee.hooks[fn]
            try:
                ee.r[31] = RETURN
                ee.run(fn)
            finally:
                ee.hooks[fn] = call
                ee.r[31] = ret
            if fn == 0x1D5A70:
                self.clips[ee.r[2] & 0xFFFFFFFF] += 1
        return call

    def step(self, label, entry, native, ints=(), floats=()):
        super().step(label, entry, native, ints, floats)
        assert bytes(self.dynamic) == DYNAMIC, (self.beat, 'borrowed table was modified')
        assert C.string_at(R.NATIVE.em_rcl_bytes(DYN_BASE, len(DYNAMIC)), len(DYNAMIC)) == DYNAMIC


def initialize():
    global BANK_BASE, BANK, DYN_BASE, DYNAMIC
    R.OUT = OUT
    R.KickEE = A.A01EE  # same original core plus the static pass's VCLIP
    R.OWNED = R.OWNED + ((0x70003400, 0x80),)
    R.ELF = read_elf()
    R.NATIVE = R.build_native()
    R.LEAF = R.FRH.Leaf(R.ELF)
    R.E80, R.E88 = (C.c_uint8 * 2)(), (C.c_uint8 * 2)()
    n = R.NATIVE
    n.em_rcl_static_world_bank.argtypes = [C.c_uint32, C.c_char_p, C.c_uint32]
    n.em_rcl_dynamic_world_bind.argtypes = [C.c_void_p, C.c_void_p, C.c_uint32]
    n.em_rcl_001C1D00.argtypes = [C.c_uint32]
    n.em_rcl_bytes_mut.argtypes = [C.c_uint32, C.c_uint32]
    n.em_rcl_bytes_mut.restype = C.c_void_p
    assert n.em_rcl_init(str(R.EXPORT).encode(), R.E80) == 0
    # The global upload block comes from its existing boot-derived export;
    # AREA01's actual static bank then replaces the AREA11 bank.
    assert n.em_rcl_static_world_load(str(R.STATIC_WORLD).encode()) == 0
    BANK_BASE, BANK = emsc('static_bank')
    DYN_BASE, DYNAMIC = emsc('dynamic_objects')
    assert n.em_rcl_static_world_bank(BANK_BASE, BANK, len(BANK)) == 0


def run_capture(path):
    steps = 0
    p = Pair(path, DYNAMIC, DYN_BASE)
    n = R.NATIVE
    p.step('area flags', 0x1C1DC0, n.em_rcl_001C1DC0)
    steps += 1
    for frame in range(RM.pick(4, 2)):
        slot = frame & 1
        p.poke_external(0x810E80, struct.pack('<H', slot))
        p.step('frame begin', 0x1D1AE0, lambda: n.em_rcl_001D1AE0(slot), ints=(slot,))
        p.step('frame head', 0x1D1C50, n.em_rcl_001D1C50)
        p.step('static and dynamic pass', 0x1C1D00, lambda: n.em_rcl_001C1D00(0x8101D0), ints=(0x8101D0,))
        p.step('page splice', 0x1D1EA0, lambda: n.em_rcl_001D1EA0(1), ints=(1,))
        steps += 4
    if path == ARRIVAL:
        # Exercise the real partial-clip arm even if endpoints happen
        # to leave every triangle wholly inside/outside. This matrix
        # maps y/16 to clip x and keeps w=1: the captured table has
        # triangle vertices at y=10 and y=22. Only the dynamic pass
        # reads this context matrix; its depth projection stays real.
        p.step('partial begin', 0x1D1AE0, lambda: n.em_rcl_001D1AE0(0), ints=(0,))
        p.step('partial head', 0x1D1C50, n.em_rcl_001D1C50)
        m = [0.0] * 16
        m[4], m[15] = 0.0625, 1.0
        data = struct.pack('<16f', *m)
        p.ee.write(0x811CC0 + 0x2240, data)
        assert n.em_rcl_poke(0x811CC0 + 0x2240, data, len(data)) == 0
        p.step('partial clip pass', 0x1C1D00, lambda: n.em_rcl_001C1D00(0x8101D0), ints=(0x8101D0,))
        p.step('partial page splice', 0x1D1EA0, lambda: n.em_rcl_001D1EA0(1), ints=(1,))
        steps += 4
    print(f'{path.name}: PASS {dict((hex(k), v) for k, v in p.counts.items())} '
          f'clip results {dict(p.clips)}', flush=True)
    return steps, p.counts, p.clips


def main():
    start = time.time()
    initialize()
    paths = [ARRIVAL] + sorted(p for p in AREA.glob('a01_*')
                               if (p / 'eeMemory.bin').exists()
                               and (p / 'eeMemory.bin').read_bytes()[0x810700:0x810702] == b'\x01\x00')
    picked = RM.select(paths, 5, 0x1D5BD0, keep=lambda i, p: p == ARRIVAL or p.name in
                       ('a01_00_train_room', 'a01_01_tunnel', 'a01_02_shaft_landing', 'a01_05_npc_bridge_talk'))
    steps, calls, clips = 0, Counter(), Counter()
    for count, subcalls, subclips in RM.parallel_map(run_capture, picked):
        steps += count
        calls.update(subcalls)
        clips.update(subclips)
    assert calls[0x1D4FC0] and calls[0x1D5170] and calls[0x1CAAC0] and set(clips) == {0, 1, 0xFF}, (calls, clips)
    # Rebinding an area invalidates the borrowed loader window. It must not
    # silently use AREA01 geometry in the next area's packet pass.
    last = Pair(picked[-1], DYNAMIC, DYN_BASE)
    assert R.NATIVE.em_rcl_bind(last._views, len(R.EXTERNAL), C.byref(last.workers)) == 0
    assert not R.NATIVE.em_rcl_bytes(0x28A5A4, 4)
    assert not R.NATIVE.em_rcl_bytes(DYN_BASE, len(DYNAMIC))
    assert R.NATIVE.em_rcl_dynamic_world_bind(None, last.dynamic, len(DYNAMIC)) == -1
    assert R.NATIVE.em_rcl_dynamic_world_bind(last.table_word, None, len(DYNAMIC)) == -1
    assert R.NATIVE.em_rcl_dynamic_world_bind(last.table_word, last.dynamic, 0) == -1
    # State 0 initializes and then runs the static pass too. Its first
    # reached dynamic pass must report the missing table.
    assert R.NATIVE.em_rcl_001C1D00(0x8101D0) == -1
    assert R.NATIVE.em_rcl_fault() == 0x28A5A4
    assert R.NATIVE.em_rcl_dynamic_world_bind(last.table_word, last.dynamic, len(DYNAMIC)) == -1
    RM.banner(RM.part(len(picked), len(paths), 'AREA01 snapshots'))
    print(f'PASS: {steps} composed entries, all owned bytes after every entry; '
          f'{sum(calls.values())} original dynamic/depth calls, clips {dict(clips)}; '
          f'borrowed read-only views, area invalidation, missing-table fail-stop ({time.time()-start:.1f}s)')


if __name__ == '__main__':
    main()
