#!/usr/bin/env python3
"""Door/terminal service bindings and slider dispatch against original code.

Fade/stream, script ticks and final player placement are explicit native
service boundaries. All calls compare their ordered arguments, stack and
canonical bytes; final RAM and scratch are compared in full.
"""
import ctypes as C
import json
import struct
import subprocess
import time
import export_area01_common as A
import reference_mode as mode
import test_area01_runtime_reference as runtime
from test_player_fall_reference import FallEE
from test_player_slide_reference import STACK_TOP, bits

OUT = A.ROOT / 'build/level2-crashes/transition-reference'


def main():
    start = time.monotonic()
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / 'services.dylib'
    sources = ['em_area01_transition_services', 'em_interaction_alignment', 'em_script_door_fan',
               'em_script_host_workers', 'em_player_stage_workers', 'em_sdk_math_original', 'em_script']
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-shared', '-fPIC', '-Isrc', *['src/game/' + s + '.c' for s in sources],
                    '-o', str(path)], cwd=A.ROOT, check=True)
    lib = C.CDLL(str(path))
    lib.em_area01_transition_call.argtypes = [C.POINTER(runtime.Host), C.POINTER(runtime.Call), C.POINTER(C.c_uint32)]
    rt = runtime.build()
    elf = A.read_elf()
    captures = A.captures()
    if not mode.FULL:
        captures = [next(c for c in captures if c.path.name == 'a01_s4_east_room')]
    total = boundaries = 0
    for cap in captures:
        slider = next(a for a in range(0x7A5640, 0x7D4640, 0x2F0) if A.u32(cap.ram, a + 0x10) == 0x1BB860)
        terminal = 0x7AD490
        cases = [(0x1BBD60, (slider, 0x24D980), (), None)]
        cases += [(0x1B0C00, (d,), (), None) for d in (-1, 0, 4, 8, 32767)]
        cases += [(0x1B6F00, (terminal, 0x700038A0), (3.1415927,), None)]
        cases += [(fn, (slider,), (), variant) for fn in (0x1BB400, 0x1BB7C0, 0x1BB7F0)
                  for variant in ((0, 0), (1, 1), (8, 0), (0x3E, 0))]
        for fn, args, floats, variant in cases:
            original = FallEE(elf, cap.ram, cap.spad)
            original.write(0x700038A0, struct.pack('<4f', 0, 0, 8.8, 1))
            if variant:
                kind, busy = variant
                original.save(slider + 3, kind, 1)
                original.save(0x8106B8, busy, 1)
                original.save(0x275B40, slider + 0x110)
            baseline, spad_start = bytes(original.mem), bytes(original.spad)
            calls = []
            def hook(entry, n):
                def call(ee):
                    calls.append((entry, tuple(ee.r[4:4+n]), ee.r[29], bytes(ee.mem), bytes(ee.spad)))
                    if entry == 0x1BA1F0:
                        ee.r[2] = 0xFFFFFFFFFFFFFFFF if variant[1] else 0
                return call
            original.hooks = {entry: hook(entry, n) for entry, n in
                              ((0x1AEDE0, 2), (0x1FAD70, 3), (0x182F90, 2), (0x1BA1F0, 1))}
            original.call(fn, args, floats)
            ram = (C.c_uint8 * len(baseline)).from_buffer_copy(baseline)
            spr = (C.c_uint8 * len(spad_start)).from_buffer_copy(spad_start)
            position, errors = 0, []
            def view(_, address, size, write):
                for base, buf in ((0, ram), (0x70000000, spr)):
                    if size and address >= base and address + size <= base + len(buf):
                        return C.addressof(buf) + address - base
                return None
            def worker(_, cp):
                nonlocal position
                try:
                    c = cp.contents
                    want = calls[position]
                    assert (c.function, tuple(c.a[:c.na]), c.sp) == want[:3]
                    assert bytes(ram) == want[3] and bytes(spr) == want[4]
                    if c.function == 0x1BA1F0:
                        c.v0 = 0xFFFFFFFFFFFFFFFF if variant[1] else 0
                    position += 1
                    return 0
                except Exception as error:
                    errors.append(repr(error))
                    return -1
            callbacks = runtime.View(view), runtime.Worker(worker)
            host = runtime.Host(None, *callbacks)
            c = runtime.Call(function=fn, sp=STACK_TOP, na=len(args), nf=len(floats))
            for i, arg in enumerate(args): c.a[i] = arg & ((1 << 64) - 1)
            for i, value in enumerate(floats): c.f[i] = bits(value)
            fault = C.c_uint32()
            if fn in (0x1BB400, 0x1BB7C0, 0x1BB7F0):
                assert rt.a01rt_bind(C.byref(host)) == 0
                rc = rt.a01rt_call(C.byref(c))
                fault.value = rt.a01rt_fault()
                assert c.v0 == original.r[2]
            else:
                rc = lib.em_area01_transition_call(C.byref(host), C.byref(c), C.byref(fault))
            assert rc == 0, (cap.name, hex(fn), variant, errors, hex(fault.value))
            assert position == len(calls)
            assert bytes(ram) == bytes(original.mem), (cap.name, hex(fn), variant, 'RAM')
            assert bytes(spr) == bytes(original.spad), (cap.name, hex(fn), variant, 'scratch')
            total += 1
            boundaries += position
    refusals = 0
    for fn, args, floats, missing in ((0x1BBD60, (slider, 0x24D980), (), slider + 0x56),
                                     (0x1BBD60, (slider, 0x24D980), (), 0x24D998),
                                     (0x1B6F00, (terminal, 0x700038A0), (3.1415927,), 0x700038A0),
                                     (0x1B0C00, (4,), (), 0)):
        def denied(_, address, size, write):
            if missing and address <= missing < address + size:
                return None
            return view(None, address, size, write)
        refused = []
        def fail_worker(_, cp):
            refused.append(cp.contents.function)
            return -1
        callbacks = runtime.View(denied), runtime.Worker(fail_worker)
        host = runtime.Host(None, *callbacks)
        c = runtime.Call(function=fn, sp=STACK_TOP, na=len(args), nf=len(floats))
        c.a[:len(args)] = args
        if floats: c.f[0] = bits(floats[0])
        fault = C.c_uint32()
        assert lib.em_area01_transition_call(C.byref(host), C.byref(c), C.byref(fault)) < 0
        assert fault.value == (missing or 0x1AEDE0), (hex(fn), hex(fault.value))
        assert refused == ([] if missing else [0x1AEDE0])
        refusals += 1
    report = dict(status='PASS', captures=len(captures), cases=total, boundaries=boundaries, refusals=refusals,
                  seconds=round(time.monotonic() - start, 3))
    (OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
