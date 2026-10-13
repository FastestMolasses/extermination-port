#!/usr/bin/env python3
"""AREA00 script resource selection and narrow workers against the original.

Resource fixtures use the original load-time module. Original instructions
prove 824E40's stores/clip boundary and the existing 1B7700 owner through
its canonical adapter. These are binding proofs, not native route parity.
"""
import ctypes as C
import itertools
import os
from pathlib import Path
import subprocess
import time

import export_area00_common as A0
import reference_root as refs
from reference_mode import FULL, banner
from test_area01_runtime_reference import Host, Call, View, Worker
from test_player_fall_reference import FallEE
from test_player_slide_reference import read_elf, STACK_TOP

ROOT = Path(__file__).resolve().parents[1]


def build_and_check_resources(out):
    common = ['src/game/em_area01_script_live.c', 'src/game/em_script.c', 'src/game/em_spawn_table.c']
    for name in ('area01', 'area00'):
        binary = out / name
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
            '-Isrc', f'tests/{name}_script_binding_test.c', *common, '-o', str(binary)], cwd=ROOT, check=True)
        args = (['assets/area01_boot_scripts', 'assets/area01/door_destinations.emsp'] if name == 'area01'
                else [str(A0.OVERLAY_PATH)])
        subprocess.run([str(binary), *args], cwd=ROOT, check=True,
                       env=dict(os.environ, UBSAN_OPTIONS='halt_on_error=1'))
    sources = ('em_area01_script_workers', 'em_area00_low', 'em_area02_misc', 'em_area01_revisit', 'em_area01_math_core')
    lib = out / 'workers.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared', '-fPIC',
        '-Isrc', *[f'src/game/{s}.c' for s in sources], '-o', str(lib)], cwd=ROOT, check=True)
    n = C.CDLL(str(lib))
    n.em_area01_script_worker_call_area.argtypes = [C.POINTER(Host), C.POINTER(Call), C.POINTER(C.c_uint32), C.c_uint]
    n.em_area01_script_worker_handles_area.argtypes = [C.c_uint, C.c_uint]
    return n


def main():
    started = time.monotonic()
    out = ROOT / 'build/level3/script-binding'
    out.mkdir(parents=True, exist_ok=True)
    n = build_and_check_resources(out)
    overlay = A0.OVERLAY_PATH.read_bytes()
    assert A0.C.sha(overlay) == A0.OVERLAY_SHA256, 'original AREA00 module digest differs'
    assert n.em_area01_script_worker_handles_area(0x824E40, 0) == 1
    assert n.em_area01_script_worker_handles_area(0x824E40, 1) == 0
    assert n.em_area01_script_worker_handles_area(0x825910, 0) == 0
    assert n.em_area01_script_worker_handles_area(0x826950, 0) == 0
    assert n.em_area01_script_worker_handles_area(0x1B7700, 2) == 0
    elf = read_elf()
    folder = refs.capture('s87/route_a01/a01_07_level_exit')
    initial, scratch = folder.joinpath('eeMemory.bin').read_bytes(), folder.joinpath('scratchpad.bin').read_bytes()
    node, block, record = 0x7A8250, 0x7A8440, 0x828FA0
    spans = ((node, 0x300), (record, 64), (0x8106CE, 2), (0x275BD8, 1))
    cases = [(0x824E40, 0, 0, value) for value in (0, 1, 0xDEADBEEF, 0xFFFFFFFF)]
    modes = (0, 1, 2, 3, 0xFFFFFFFF) if FULL else (0, 1, 2, 3)
    cases += [(0x1B7700, phase, busy, mode) for phase, busy, mode in itertools.product(range(4), (0, 1), modes)]
    total = boundaries = failures = 0
    for fn, phase, busy, value in cases:
        o = FallEE(elf);o.mem[:] = initial;o.spad[:] = scratch
        o.write(0x823500, overlay)
        o.save(block + 4, phase, 1);o.save(0x275BD8, busy, 1)
        o.save(record + 8, value);o.save(record + 0x14, 0x87, 1)
        o.save(block + 0xD0, value);o.save(block + 0xD8, value);o.save(block + 0xEC, value)
        before = bytes(o.mem)
        expected = []

        def snapshot(memory):
            return tuple(memory.read(a, size) for a, size in spans)

        def clip(memory):
            args = tuple(memory.r[4 + i] & 0xFFFFFFFF for i in range(2))
            floats = tuple(memory.f[12 + i] for i in range(2))
            expected.append((0x1C67E0, args, floats, memory.r[29] & 0xFFFFFFFF, snapshot(memory)))
            memory.save(block + 0xD0, 0xABCD1234)
            memory.r[2] = 0x1234

        o.hooks = {0x1C67E0: clip}
        o.call(fn, (node, block, record))
        want, result = snapshot(o), o.r[2] & 0xFFFFFFFF
        ram = (C.c_uint8 * len(before)).from_buffer_copy(before)
        actual = []
        deny = None
        reject_worker = False

        @View
        def view(_, address, size, write):
            if deny is not None and address <= deny < address + size:
                return None
            if any(a <= address and address + size <= a + length for a, length in spans):
                return C.addressof(ram) + address
            return None

        def current():
            return tuple(bytes(ram[a:a + size]) for a, size in spans)

        @Worker
        def worker(_, ptr):
            c = ptr.contents
            actual.append((c.function, tuple(c.a[i] & 0xFFFFFFFF for i in range(c.na)),
                           tuple(c.f[:c.nf]), c.sp, current()))
            if c.function != 0x1C67E0:
                return -1
            if reject_worker:
                return -1
            C.memmove(C.addressof(ram) + block + 0xD0, bytes.fromhex('3412cdab'), 4)
            c.v0 = 0x1234
            return 0

        host = Host(None, view, worker)
        call = Call(function=fn, sp=STACK_TOP, na=3);call.a[:3] = (node, block, record)
        fault = C.c_uint32()
        rc = n.em_area01_script_worker_call_area(C.byref(host), C.byref(call), C.byref(fault), 0)
        assert rc == 0, (hex(fn), phase, busy, value, 'fault', hex(fault.value))
        assert actual == expected, (hex(fn), phase, 'original worker boundary',
                                    [x[:4] for x in actual], [x[:4] for x in expected])
        assert current() == want, (hex(fn), phase, busy, value, 'original bytes')
        assert call.v0 & 0xFFFFFFFF == result, (hex(fn), phase, busy, value, 'return')
        total += 1;boundaries += len(expected)
        if fn == 0x824E40 and value == 0:
            for deny, reject_worker, address in ((block + 0xD8, False, block + 0xD8),
                                                 (None, True, 0x1C67E0)):
                C.memmove(ram, before, len(before));actual.clear();call.v0 = 0xCAFE;fault.value = 0
                rc = n.em_area01_script_worker_call_area(C.byref(host), C.byref(call), C.byref(fault), 0)
                assert rc < 0 and fault.value == address and call.v0 == 0xCAFE, ('missing arrival worker/view', rc, fault.value)
                assert bytes(ram[block + 0xD0:block + 0xD0 + 4]) == before[block + 0xD0:block + 0xD0 + 4], \
                    'arrival callback wrote its final halfword after failure'
                failures += 1
        if fn == 0x1B7700 and phase == 1 and busy == 0 and value == 1:
            C.memmove(ram, before, len(before));deny = 0x8106CE;call.v0 = 0xCAFE;fault.value = 0
            rc = n.em_area01_script_worker_call_area(C.byref(host), C.byref(call), C.byref(fault), 0)
            assert rc < 0 and fault.value == deny and call.v0 == 0xCAFE, 'loader request accepted a missing canonical byte'
            failures += 1
    banner(f'{total} AREA00 script worker cases', f'{boundaries} exact original clip boundaries',
           f'{failures} missing worker/view rejections', 'AREA01/AREA00 resource fixtures (ASan/UBSan)',
           f'{time.monotonic() - started:.1f}s')


if __name__ == '__main__':
    main()
