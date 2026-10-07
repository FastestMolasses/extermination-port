#!/usr/bin/env python3
"""AREA01's 00188610 runtime binding against the original instructions.

The live read-only player loan uses the in-stage +235 byte. This test gives
the runtime only that byte and the exported halfword table, refusing all
other spans and every write. No captured state is used by production code.
"""
import ctypes as C
import json
import struct
import time

import export_area01_common as A
import test_area01_runtime_reference as R
from test_player_fall_reference import FallEE
from test_player_slide_reference import read_elf, STACK_TOP


def main():
    started = time.monotonic()
    native = R.build()
    elf = read_elf()
    captures = A.captures()
    exported = (A.ROOT/'assets/area01_boot_scripts/timeline.emsp').read_bytes()
    magic, version, count, _ = struct.unpack_from('<4s3I', exported)
    assert (magic, version) == (b'EMSP', 1)
    offset = 16 + count*8
    table = None
    for i in range(count):
        address, size = struct.unpack_from('<2I', exported, 16+8*i)
        if address == 0x2754D8:
            assert size == 4
            table = exported[offset:offset+size]
        offset += size
    assert offset == len(exported) and table is not None
    total = 0
    for capture in captures:
        assert capture.ram[0x2754D8:0x2754DC] == table
        original = FallEE(elf)
        original.mem[:] = capture.ram
        flag = C.c_uint8()
        data = C.create_string_buffer(table, 4)
        errors = []
        def resolve(_, address, size, write):
            if write:
                errors.append(('write', address, size)); return None
            if (address, size) == (0x8102B0+0x235, 1):
                return C.addressof(flag)
            if size == 2 and address in (0x2754D8, 0x2754DA):
                return C.addressof(data)+address-0x2754D8
            errors.append(('read', address, size)); return None
        def worker(_, call):
            errors.append(('worker', call.contents.function)); return -1
        view, work = R.View(resolve), R.Worker(worker)
        host = R.Host(None, view, work)
        assert native.a01rt_bind(C.byref(host)) == 0
        for value in (0, 1, 2, 3, 0x7E, 0x7F, 0x80, 0x81, 0xFE, 0xFF):
            flag.value = value
            original.save(0x8102B0+0x235, value, 1)
            original.call(0x188610, (0x8102B0,))
            call = R.Call(function=0x188610, sp=STACK_TOP, na=1)
            call.a[0] = 0x8102B0
            assert native.a01rt_call(C.byref(call)) == 0, hex(native.a01rt_fault())
            assert not errors and call.v0 == original.r[2]
            assert flag.value == value and data.raw == table
            total += 1
    report = dict(status='PASS', captures=len(captures), cases=total,
                  scope='existing room owner through actual AREA01 runtime; exact exported table and strict read-only byte provider',
                  seconds=round(time.monotonic()-started, 3))
    out = A.ROOT/'build/level2-crashes/crawl-clip-reference'
    out.mkdir(parents=True, exist_ok=True)
    (out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
