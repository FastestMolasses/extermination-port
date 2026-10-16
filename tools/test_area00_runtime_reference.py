#!/usr/bin/env python3
"""Selected AREA00 runtime entry/adapter checks against original instructions.

This checks the dispatch boundary, not a gameplay frame. Twelve owner free
paths and the terminal op09 callback run the real AREA00 overlay through the
shared runtime. Free and sound workers are explicit compared boundaries.
Unknown AREA00 overlay entries must reach only the selected host and fault.
"""
import ctypes as C
import struct

import test_area01_runtime_reference as R
from test_player_fall_reference import FallEE
from test_player_slide_reference import read_elf, STACK_TOP

OWNERS = (0x823580, 0x823820, 0x824EA0, 0x825170, 0x825480, 0x825600,
          0x825920, 0x825C80, 0x8261E0, 0x8262D0, 0x8263C0, 0x8266A0)
NODE = 0x7B7C00


def shared_leaves(native, elf):
    """Original flag-list branches and the existing switch-marker thunk.

    Test-only synthetic lists isolate the exact pointer walk and signed-key
    gates. Live tables come from the immutable export and delivered overlay.
    """
    cases = []
    for key in (7, -7):
        for flags in (0, 0x20000000, 0x40000000, 0x60000000):
            for enable in (0, 0x100):
                cases.append((key, flags, enable, 0xB, False))
    cases += [(7, 0, 0, kind, False) for kind in (0xFF, 1)]
    cases += [(7, 0, 0, 0xB, True)]
    for key, flags, enable, kind, skip in cases:
        o = FallEE(elf)
        table, records, directory = 0x1C00000, 0x1D00000, 0x1B00000
        o.save(0x810700, 0, 1); o.save(0x810701, 1, 1)
        o.save(0x24D7C0, table, 4); o.save(table + 4, records, 4)
        o.save(0x70003250, directory, 4)
        o.save(records, kind, 2); o.save(records + 4, 123 if skip else key, 2)
        o.save(records + 6, 0x700, 2)
        if skip:
            o.save(records + 0x28, 0xB, 2); o.save(records + 0x2C, key, 2)
            o.save(records + 0x2E, 0x700, 2)
        o.save(records + (0x50 if skip else 0x28), 0xFF, 2)
        o.save(directory + 32, flags | 0x1234, 4)
        baseline, scratch = bytes(o.mem), bytes(o.spad)
        o.call(0x19C6F0, (key & 0xFFFFFFFF, enable))
        ram = (C.c_uint8 * len(baseline)).from_buffer_copy(baseline)
        spr = (C.c_uint8 * len(scratch)).from_buffer_copy(scratch)
        access, seen = [], []
        def view(_, address, size, write):
            access.append((address, size, write))
            for base, data in ((0, ram), (0x70000000, spr)):
                if size and base <= address and address + size <= base + len(data):
                    return C.addressof(data) + address - base
            return None
        v, w = R.View(view), R.Worker(lambda _, p: seen.append(p.contents.function) or -1)
        host = R.Host(None, v, w)
        assert native.a01rt_bind_area(C.byref(host), 0) == 0
        call = R.Call(function=0x19C6F0, sp=STACK_TOP, na=2)
        call.a[0] = key & ((1 << 64) - 1); call.a[1] = enable
        assert native.a01rt_call(C.byref(call)) == 0 and not seen
        assert call.v0 == o.r[2] and bytes(ram) == bytes(o.mem) and bytes(spr) == bytes(o.spad)
        assert [a for a in access if a[2]] == ([(directory + 32, 4, 1)] if call.v0 else [])
    # The canonical collision directory may be readable but refuse stores.
    # A refusal must preserve the word and latch its actual data address.
    C.memmove(ram, baseline, len(baseline))
    def readonly(_, address, size, write):
        return None if write else view(None, address, size, write)
    v = R.View(readonly); host = R.Host(None, v, w)
    assert native.a01rt_bind_area(C.byref(host), 0) == 0
    assert native.a01rt_call(C.byref(call)) == -1
    assert native.a01rt_fault() == directory + 32 and bytes(ram) == baseline
    assert native.a01rt_call(C.byref(call)) == -1
    # The original tail call forwards whole pointer register images and the
    # float bits, with no stack frame. The worker's entire result survives.
    args = (0xFFFFFFFFF00038A0, 0xFFFFFFFFF00038B0)
    o = FallEE(elf); expected = []
    def marker(e):
        expected.append((e.r[29], tuple(v & ((1 << 64) - 1) for v in e.r[4:6]), tuple(e.f[12:14])))
        e.r[2] = 0xFEDCBA9876543210
    o.hooks = {0x1F4D40: marker}
    o.call(0x1F4E20, args, (2.0,))
    seen = []
    def worker(_, p):
        c = p.contents
        seen.append((c.sp, tuple(c.a[:c.na]), tuple(c.f[:c.nf])))
        c.v0 = 0xFEDCBA9876543210
        return 0 if c.function == 0x1F4D40 else -1
    v, w = R.View(lambda *args: None), R.Worker(worker)
    host = R.Host(None, v, w)
    assert native.a01rt_bind_area(C.byref(host), 0) == 0
    call = R.Call(function=0x1F4E20, sp=STACK_TOP, na=2, nf=1)
    call.a[:2] = args; call.f[0] = 0x40000000
    assert native.a01rt_call(C.byref(call)) == 0
    assert seen == expected and call.v0 == o.r[2], (seen, expected, hex(call.v0), hex(o.r[2]))
    print(f'AREA00 shared runtime: PASS {len(cases)} original flag-list paths and marker tail-call boundary')


def main():
    native = R.build()
    native.a01rt_bind_area.argtypes = [C.POINTER(R.Host), C.c_uint]
    elf = read_elf()
    shared_leaves(native, elf)
    overlay = (R.ROOT.parent / 'Extermination/extract/OVERLAY/AREA00.BIN').read_bytes()
    assert struct.unpack_from('<3I', overlay) == (0x336F574D, 1, 0x823500)
    cases = [(fn, 3, 0) for fn in OWNERS]
    cases += [(0x8253E0, phase, step) for phase, step in ((0, 0), (1, 15), (1, 48), (2, 7))]
    boundaries = writes = 0
    for entry, phase, step in cases:
        o = FallEE(elf)
        o.write(0x823500, overlay)
        o.save(NODE + 4, phase, 1); o.save(NODE + 5, step, 1)
        baseline = bytes(o.mem)
        expected = []

        def hook(fn, count):
            def call(ee):
                expected.append((fn, tuple(ee.r[4+k] & 0xFFFFFFFFFFFFFFFF for k in range(count))))
                ee.r[2] = 0
            return call

        o.hooks = {0x1AFC10: hook(0x1AFC10, 1), 0x1FB9F0: hook(0x1FB9F0, 4)}
        args = (0x8102B0, NODE, 0) if entry == 0x8253E0 else (NODE,)
        o.call(entry, args)
        ram = (C.c_uint8 * len(baseline)).from_buffer_copy(baseline)
        spr = (C.c_uint8 * len(o.spad))()
        stack = (C.c_uint8 * 0x100000)()
        seen, access, errors = [], [], []

        def view(_, address, size, write):
            access.append((address, size, write))
            for base, data in ((0, ram), (0x70000000, spr), (0x7F000000, stack)):
                if size and base <= address and address + size <= base + len(data):
                    return C.addressof(data) + address - base
            return None

        def worker(_, pointer):
            try:
                c = pointer.contents
                assert len(seen) < len(expected), ('extra worker', hex(c.function))
                want = expected[len(seen)]
                actual = (c.function, tuple(c.a[:c.na]))
                assert actual == want and c.nf == 0, (actual, want)
                seen.append(actual); c.v0 = 0
                return 0
            except Exception as error:
                errors.append(repr(error)); return -1

        v, w = R.View(view), R.Worker(worker)
        host = R.Host(None, v, w)
        assert native.a01rt_bind_area(C.byref(host), 0) == 0
        call = R.Call(function=entry, sp=STACK_TOP, na=len(args))
        for k, arg in enumerate(args): call.a[k] = arg
        assert native.a01rt_call(C.byref(call)) == 0, (hex(entry), errors, hex(native.a01rt_fault()))
        assert not errors and seen == expected
        assert bytes(ram) == bytes(o.mem) and bytes(spr) == bytes(o.spad)
        if entry == 0x8253E0:
            assert call.v0 == o.r[2]
            if phase <= 1:
                assert (NODE + 5, 1, 1) in access
        assert (NODE + 4, 1, 0) in access
        boundaries += len(seen); writes += sum(a[2] for a in access)

    # This is an AREA01 owner at a valid overlapping numeric address.
    # Selecting AREA00 must not execute its body or touch its memory.
    access, seen = [], []
    v = R.View(lambda _, a, n, write: access.append((a, n, write)) or None)
    w = R.Worker(lambda _, p: seen.append(p.contents.function) or -1)
    host = R.Host(None, v, w)
    assert native.a01rt_bind_area(C.byref(host), 0) == 0
    call = R.Call(function=0x825350, sp=STACK_TOP, na=1); call.a[0] = NODE
    assert native.a01rt_call(C.byref(call)) == -1
    assert native.a01rt_fault() == 0x825350 and seen == [0x825350] and not access
    assert native.a01rt_call(C.byref(call)) == -1 and seen == [0x825350]
    assert native.a01rt_bind_area(C.byref(host), 2) == -1
    # A canonical read may succeed where a write must be refused. Refusal
    # cannot retry through a mutable legacy view or publish a partial store.
    record = (C.c_uint8 * 0x10)()
    access = []
    def read_only(_, address, size, write):
        access.append((address, size, write))
        if not write and NODE <= address and address + size <= NODE + len(record):
            return C.addressof(record) + address - NODE
        return None
    v = R.View(read_only); host = R.Host(None, v, w)
    assert native.a01rt_bind_area(C.byref(host), 0) == 0
    call = R.Call(function=0x8253E0, sp=STACK_TOP, na=3); call.a[1] = NODE
    assert native.a01rt_call(C.byref(call)) == -1
    assert native.a01rt_fault() == NODE + 4
    assert access == [(NODE + 4, 1, 0), (NODE + 4, 1, 1)] and bytes(record) == bytes(0x10)
    print(f'AREA00 selected runtime: PASS {len(cases)} original entry paths, {boundaries} compared workers, '
          f'{writes} canonical writes; unknown overlay, write refusal and fault latch checks (not gameplay parity)')


if __name__ == '__main__':
    main()
