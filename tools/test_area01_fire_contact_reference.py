#!/usr/bin/env python3
"""AREA01 fire contact runtime binding versus original contact instructions.

The actual SIDE owner and native 0021BB00 predicate run in the composition.
Effect allocation is an explicit boundary; test_effect_original_reference
proves its complete light/sound chain separately. Captures are fixtures only.
"""
import ctypes as C
import json
from pathlib import Path

from export_area01_common import captures, read_elf, u32
from test_area01_runtime_reference import Call, Host, View, Worker, build
from test_player_fall_reference import FallEE
from test_player_slide_reference import STACK_TOP
from test_player_stage_workers_reference import build_native

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/level2-crashes/fire-contact-reference'
PLAYER, OWNER, EFFECT = 0x8102B0, 0x7A96E0, 0x1E00000
MASK = (1 << 64) - 1


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    native, gate = build(), build_native()
    gate.em_player_0021BB00.argtypes = [C.c_void_p]
    elf = read_elf()
    records = captures()
    count = boundaries = spawned = refused = 0

    def run(capture, mode=None, allocation=EFFECT, state=1, minor=2, deny=0):
        nonlocal count, boundaries, spawned, refused
        original = FallEE(elf)
        original.mem[:] = capture.ram
        original.spad[:] = capture.spad
        assert u32(capture.ram, OWNER + 0x34) == 0x1E3D20
        if mode is not None:
            original.save(PLAYER, state, 1)
            original.save(PLAYER + 0x1F0, mode, 1)
            original.save(PLAYER + 0xD, minor, 1)
            original.save(PLAYER + 4, 1, 1)
            original.save(PLAYER + 5, 8, 1)
            original.save(PLAYER + 6, minor, 1)
        original.write(EFFECT, bytes([0xA5]) * 0x2F0)
        baseline, scratch = bytes(original.mem), bytes(original.spad)
        expected = []
        policies = {0x21BB00: (1, 0), 0x102948: (2, 0), 0x1EF9D0: (2, 1)}

        def hook(fn):
            def call(e):
                na, nf = policies[fn]
                args, floats = tuple(e.r[4:4+na]), tuple(e.f[12:12+nf])
                point = e.read(e.r[5], 16) if fn == 0x1EF9D0 else None
                expected.append((fn, args, floats, e.r[29], point))
                if fn == 0x1EF9D0:
                    e.r[2] = allocation
                else:
                    del e.hooks[fn]
                    try:
                        e.r[2], e.f[0] = e.nested(fn)
                    finally:
                        e.hooks[fn] = call
            return call
        original.hooks = {fn: hook(fn) for fn in policies}
        original.call(0x1E3D20, (OWNER, PLAYER))

        ram = (C.c_uint8 * len(baseline)).from_buffer_copy(baseline)
        spr = (C.c_uint8 * len(scratch)).from_buffer_copy(scratch)
        stack = (C.c_uint8 * 0x100000)()
        leaves = FallEE(elf)
        leaves.mem = memoryview(ram).cast('B')
        leaves.spad = memoryview(spr).cast('B')
        leaves.stack = memoryview(stack).cast('B')
        seen, errors = [], []

        def resolve(_, address, size, write):
            for base, data in ((0, ram), (0x70000000, spr), (0x7F000000, stack)):
                if size and base <= address and address+size <= base+len(data):
                    return C.addressof(data)+address-base
            return None

        def worker(_, cp):
            try:
                c = cp.contents
                assert len(seen) < len(expected)
                fn, args, floats, sp, point = expected[len(seen)]
                assert (c.function, tuple(c.a[:c.na]), tuple(c.f[:c.nf]), c.sp) == (fn, args, floats, sp)
                seen.append(fn)
                if fn == deny:
                    return -1
                if fn == 0x21BB00:
                    c.v0 = gate.em_player_0021BB00(resolve(None, PLAYER, 0x320, 0))
                elif fn == 0x1EF9D0:
                    assert C.string_at(resolve(None, c.a[1], 16, 0), 16) == point
                    c.v0 = allocation
                else:
                    for i in range(c.na): leaves.r[4+i] = c.a[i]
                    leaves.r[29] = c.sp
                    c.v0, c.f0 = leaves.nested(fn)
                return 0
            except Exception as error:
                errors.append(repr(error))
                return -1

        view, work = View(resolve), Worker(worker)
        host = Host(None, view, work)
        assert native.a01rt_bind(C.byref(host)) == 0
        call = Call(function=0x1E3D20, sp=STACK_TOP, na=2)
        call.a[0], call.a[1] = OWNER, PLAYER
        result = native.a01rt_call(C.byref(call))
        assert not errors, (capture.name, mode, errors)
        if deny:
            assert result == -1 and native.a01rt_fault() == deny
            assert bytes(ram[PLAYER:PLAYER+0x320]) == baseline[PLAYER:PLAYER+0x320]
            assert bytes(ram[OWNER:OWNER+0x2F0]) == baseline[OWNER:OWNER+0x2F0]
            assert bytes(ram[EFFECT:EFFECT+0x2F0]) == baseline[EFFECT:EFFECT+0x2F0]
            refused += 1
            return
        assert result == 0 and len(seen) == len(expected), (capture.name, mode, hex(native.a01rt_fault()))
        assert bytes(ram) == bytes(original.mem), (capture.name, mode, 'RAM')
        assert bytes(spr) == bytes(original.spad), (capture.name, mode, 'scratch')
        spawned += 0x1EF9D0 in seen and allocation != 0
        boundaries += len(seen)
        count += 1

    for record in records:
        run(record)
    first = records[0]
    for mode in (0, 8, 9, 10, 0xF, 0x11, 0x14, 0x1D, 0x2C, 0x2E, 0x31, 0xFF):
        for minor in (0, 2, 4):
            for allocation in (0, EFFECT):
                run(first, mode, allocation, minor=minor)
    for state in (0, 2, 3, 4, 0xFF):
        run(first, 0, state=state)
    for deny in policies_for_refusal():
        run(first, 0, deny=deny)
    assert spawned and boundaries and refused == 3
    result = dict(cases=count, worker_boundaries=boundaries, spawned=spawned,
                  refusal_cases=refused, captures=len(records))
    (OUT/'result.json').write_text(json.dumps(result, indent=2)+'\n')
    print('AREA01 fire contact runtime original reference PASS', result)


def policies_for_refusal():
    return (0x21BB00, 0x102948, 0x1EF9D0)


if __name__ == '__main__':
    main()
