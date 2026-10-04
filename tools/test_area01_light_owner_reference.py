#!/usr/bin/env python3
"""Original-instruction check of the standalone AREA01 flicker-light owner.

The arrival's original RAM supplies the owner record and code. External
workers are recorded/stubbed on both sides; this test verifies owner logic,
call arguments/order, state before each call, final writes and float words.
No emulator is launched. EM_TEST_FULL=1 expands the synthetic boundary sweep.
"""
import ctypes as C
import itertools
import random
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode
from test_area01_math_reference import CALL, Mem
from test_coll_probe_reference import ProbeEE
from test_player_slide_reference import bits, read_elf

OUT = ROOT / 'build/area01/light_owner'
CAPTURE = ROOT.parent / 'Extermination/build/c10/exit/exit_01_movie_arrival'
PREDICATE, OWNER = 0x1C4FA0, 0x1C50B0
SP = 0x1F00000
FLAGS = (0x810C87, 0x81075D, 0x81076D, 0x810770, 0x810700, 0x810701)
WORKERS = {0x1028B8: (3, 0), 0x1028D0: (3, 0), 0x102900: (2, 1),
           0x122BB8: (0, 0), 0x1AFC10: (1, 0), 0x1C5050: (1, 1),
           0x1D80B0: (1, 0), 0x1F5490: (1, 0), 0x1F5F60: (4, 0)}


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    target = OUT / 'light_owner.dylib'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                    '-ffp-contract=off', '-O2', '-dynamiclib', '-Isrc',
                    'src/game/em_area01_light_owner.c', 'src/game/em_area01_math_core.c',
                    '-o', str(target)], cwd=ROOT, check=True)
    native = C.CDLL(str(target))
    native.em_area01_light_001C4FA0.argtypes = [C.POINTER(Mem), C.c_uint32, C.POINTER(C.c_uint32)]
    native.em_area01_light_001C50B0.argtypes = [C.POINTER(Mem), C.c_uint32, C.c_uint32]
    return native


def u32(mem, at):
    return struct.unpack_from('<I', mem, at)[0]


def word(mem, at, value):
    struct.pack_into('<I', mem, at, value & 0xFFFFFFFF)


class Pair:
    def __init__(self, elf, ram, spad, native):
        self.native = native
        records = [a for a in range(0x7A5640, 0x7A5640 + 256 * 0x2F0, 0x2F0)
                   if u32(ram, a + 0x10) == OWNER]
        assert len(records) == 1, 'arrival must provide exactly one light owner'
        self.self = records[0]
        self.record = ram[self.self:self.self + 0x2F0]
        self.ee = ProbeEE(elf, ram, spad)
        self.ram = bytearray(ram)
        self.spad = bytearray(spad)
        self.view = (C.c_uint8 * len(self.ram)).from_buffer(self.ram)
        self.sview = (C.c_uint8 * len(self.spad)).from_buffer(self.spad)
        self.trace = (C.c_uint32 * 512)()
        self.callback = CALL(self.worker)
        self.m = Mem(C.addressof(self.view), len(ram), C.addressof(self.sview), self.callback,
                     None, 0, 0, 0, self.trace, len(self.trace), 0)
        for fn in WORKERS:
            self.ee.hooks[fn] = lambda ee, fn=fn: self.original_worker(ee, fn)
        self.calls = []
        self.expected = []
        self.error = None

    def state(self, mem, fn=0, args=()):
        record = bytes(mem[self.self:self.self + 0x2F0])
        flags = tuple(mem[a] for a in FLAGS)
        vector = bytes(mem[args[2]:args[2] + 16]) if fn == 0x1F5F60 else None
        return record, flags, vector

    def stub(self, mem, fn, args):
        c = self.case
        if fn == 0x1F5490:
            if c.get('scribble'):
                mem[self.self + 0xD] = c.get('post_param', 1)
            return c.get('init', 1)
        if fn == 0x1C5050:
            if c.get('scribble'):
                word(mem, self.self + 0x80, bits(19.0))
            return c.get('handle', 7)
        if fn == 0x122BB8:
            if c.get('scribble'):
                word(mem, self.self + 0x8C, bits(0.0))
                word(mem, self.self + 0x80, bits(300.0))
            return c.get('random', 0x12345678)
        if fn in (0x1028B8, 0x1028D0):
            # Deliberate worker writes, not a substitute vector translation.
            word(mem, args[0], 0x41A00000 if fn == 0x1028B8 else 0x41200000)
        elif fn == 0x102900:
            for i in range(4):
                word(mem, args[0] + i * 4, 0x42000000 + i * 0x10000)
        elif fn == 0x1AFC10:
            mem[self.self] = 0
        return 0

    def original_worker(self, ee, fn):
        assert ee.r[29] == SP - 0x40, ('external worker stack', hex(fn), hex(ee.r[29]))
        na, nf = WORKERS[fn]
        args = tuple(ee.r[4 + i] & 0xFFFFFFFF for i in range(na))
        floats = tuple(ee.f[12 + i] for i in range(nf))
        self.expected.append((fn, args, floats, self.state(ee.mem, fn, args)))
        ee.r[2] = self.stub(ee.mem, fn, args)

    def worker(self, ctx, fn, a, na, f, nf, v0, f0):
        try:
            args, floats = tuple(a[i] for i in range(na)), tuple(f[i] for i in range(nf))
            now = (fn, args, floats, self.state(self.ram, fn, args))
            at = len(self.calls)
            assert at < len(self.expected), ('extra native call', hex(fn))
            want = self.expected[at]
            assert now[:3] == want[:3], ('call', at, now[:3], want[:3])
            assert now[3] == want[3], ('state at call', at, hex(fn))
            self.calls.append(now)
            v0[0] = self.stub(self.ram, fn, args)
            return 0
        except BaseException as error:
            self.error = error
            return -1

    def run(self, fn, case):
        self.case = case
        self.calls, self.expected, self.error = [], [], None
        self.m.fault_address = self.m.fault_code = self.m.stores = self.m.trace_len = 0
        for mem in (self.ram, self.ee.mem):
            mem[self.self:self.self + 0x2F0] = self.record
            mem[SP - 0x80:SP] = bytes(0x80)
            for a in FLAGS:
                mem[a] = 0
            mem[0x810700], mem[0x810701] = case.get('area', 1), case.get('sub', 0)
            mem[self.self + 3] = case.get('type', 0)
            mem[self.self + 4] = case.get('state', 0)
            mem[self.self + 0xD] = case.get('param', 0)
            for a in FLAGS[:4]:
                mem[a] = case.get('flag', 0)
            word(mem, self.self + 0x20, case.get('handle', 7))
            colors = case.get('colors', (bits(128.0), bits(1.0), bits(254.0), bits(32.0)))
            for i, value in enumerate(colors):
                word(mem, self.self + 0x80 + i * 4, value)
        self.ee.r[29] = SP
        writes = []
        save = self.ee.save

        def guarded(address, value, size=4):
            at = address & 0xFFFFFFFF
            assert (self.self <= at and at + size <= self.self + 0x2F0) or \
                   (SP - 0x40 <= at and at + size <= SP), ('unexpected original write', hex(at), size)
            if not SP - 0x40 <= at < SP - 0x10:
                writes.append((at, size))
            save(address, value, size)

        self.ee.save = guarded
        try:
            self.ee.call(fn, (self.self,))
        finally:
            del self.ee.save
        result = C.c_uint32(0xBAD0BAD0)
        rc = self.native.em_area01_light_001C4FA0(C.byref(self.m), self.self, C.byref(result)) if fn == PREDICATE \
            else self.native.em_area01_light_001C50B0(C.byref(self.m), self.self, SP)
        if self.error:
            raise AssertionError((case, self.error))
        assert rc == 0 and not self.m.fault_code, (case, rc, self.m.fault_code, hex(self.m.fault_address))
        assert len(self.calls) == len(self.expected), ('missing native call', case)
        assert self.state(self.ram) == self.state(self.ee.mem), ('final state', case)
        if fn == PREDICATE:
            assert result.value == self.ee.r[2], ('predicate result', case)
        native_writes = [(self.trace[i], self.trace[i + 1]) for i in range(0, self.m.trace_len, 2)]
        for a, size in set(writes + native_writes):
            assert self.ram[a:a + size] == self.ee.mem[a:a + size], ('final write', hex(a), case)
        return len(self.calls)


def fault_contract(pair):
    native, m = pair.native, pair.m
    p = pair.self
    result = C.c_uint32(0xBAD0BAD0)
    assert native.em_area01_light_001C50B0(None, p, SP) == -1
    assert native.em_area01_light_001C4FA0(None, p, C.byref(result)) == -1
    assert result.value == 0xBAD0BAD0
    m.fault_code, m.fault_address = 0, 0
    assert native.em_area01_light_001C4FA0(C.byref(m), p, None) == -1
    assert (m.fault_code, m.fault_address) == (1, PREDICATE)
    # Existing fault wins, and no record or result changes.
    before = bytes(pair.ram[p:p + 0x2F0])
    assert native.em_area01_light_001C50B0(C.byref(m), p, SP) == -1
    assert bytes(pair.ram[p:p + 0x2F0]) == before
    assert (m.fault_code, m.fault_address) == (1, PREDICATE)
    m.fault_code, m.fault_address = 0, 0
    assert native.em_area01_light_001C4FA0(C.byref(m), 0x60000000, C.byref(result)) == -1
    assert result.value == 0xBAD0BAD0 and m.fault_code == 4
    m.fault_code, m.fault_address = 0, 0
    pair.ram[p + 3] = pair.ram[p + 4] = 0
    before = bytes(pair.ram[p:p + 0x2F0])
    m.call = CALL()
    try:
        assert native.em_area01_light_001C50B0(C.byref(m), p, SP) == -1
        assert (m.fault_code, m.fault_address) == (1, 0x1F5490)
        assert bytes(pair.ram[p:p + 0x2F0]) == before
    finally:
        m.call = pair.callback
    return 6


def main():
    begin = time.time()
    if not (CAPTURE / 'eeMemory.bin').exists():
        print('SKIP: AREA01 arrival capture missing')
        return 0
    elf = read_elf()
    ram = (CAPTURE / 'eeMemory.bin').read_bytes()
    spad = (CAPTURE / 'scratchpad.bin').read_bytes()
    phoff = u32(elf, 28)
    kind, offset, base = struct.unpack_from('<3I', elf, phoff)
    assert kind == 1
    for fn, size in ((PREDICATE, 0xB0), (OWNER, 0x4D0)):
        start = fn - base + offset
        assert ram[fn:fn + size] == elf[start:start + size], ('original code mismatch', hex(fn))
    pair = Pair(elf, ram, spad, build())
    types = range(256) if reference_mode.FULL else (0, 1, 2, 4, 5, 6, 7, 8, 0xFF)
    predicates = [{'type': t, 'flag': f} for t, f in itertools.product(types, (0, 1, 0xFE, 0xFF))]
    cases = [{'state': 0, 'param': p, 'init': init, 'scribble': scribble}
             for p, init, scribble in itertools.product((0, 1, 0xFF), (1, 3, 0x103), (0, 1))]
    cases += [{'state': s, 'type': t, 'flag': flag} for s, t, flag in
              itertools.product((0, 1), (1, 5, 6, 7), (0, 1, 0xFF))]
    cases += [{'state': s, 'handle': h} for s, h in itertools.product((2, 3, 4, 0xFF), (0, 7, 0xFFFFFFFF))]
    cases += [{'state': 1, 'random': r, 'scribble': scribble} for r, scribble in
              itertools.product((0, 1, 0x7FFFFFFF, 0xFFFFFFFF, 0x80000000), (0, 1))]
    rng = random.Random(0x1C50B0)
    special = (0, 1, 0x007FFFFF, 0x80000000, 0x80000001, 0x3F800000, 0xC0000000,
               0x437F0000, 0x7F800000, 0x7FC00000, 0xFF800000, 0xFFC00000)
    for _ in range(reference_mode.pick(1200, 40)):
        cases.append({'state': 1, 'random': rng.getrandbits(32),
                      'colors': tuple(rng.choice(special) if rng.randrange(2) else rng.getrandbits(32)
                                      for _ in range(4))})
    # Dispatch boundary inputs exercise this same owner, not later-area routes.
    cases += [{'area': a, 'sub': s, 'param': p} for (a, s), p in itertools.product(
        ((1, 1), (6, 0), (6, 1), (7, 3), (8, 3), (15, 0), (0, 0)), (0, 1, 2))]
    calls = sum(pair.run(PREDICATE, c) for c in predicates)
    calls += sum(pair.run(OWNER, c) for c in cases)
    faults = fault_contract(pair)
    reference_mode.banner(f'{len(predicates)} predicate cases', f'{len(cases)} owner cases')
    print(f'PASS: {len(predicates) + len(cases)} cases, {calls} worker boundaries, '
          f'original calls/state/float words/final writes, {faults} fault checks '
          f'({time.time() - begin:.2f}s)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
