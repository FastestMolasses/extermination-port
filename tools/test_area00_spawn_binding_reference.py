#!/usr/bin/env python3
"""Direct AREA00 deferred spawn binding, with the original 001B6660 oracle.

Reuses the existing pool/roster oracle and compares the whole pool,
progress, list links and exact return. The production scene adapter is
also extracted and checked with the real actor-view transaction boundary.
"""
import ctypes as C
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import time

import export_area00_common as A0
import test_actor_census_reference as T
from reference_mode import FULL, banner

ROOT = Path(__file__).resolve().parents[1]


class Group(C.Structure):
    _fields_ = [('address', C.c_uint32), ('count', C.c_uint32), ('records', C.c_void_p)]


def check_live(out):
    source = (ROOT / 'src/game/em_scene_bindings.c').read_text()
    pieces = []
    for name in ('condition_canonical', 'area00_bind_deferred', 'area_spawn_deferred', 'area01_missing_worker'):
        start = source.index(f'static int {name}(')
        end = source.index('\n}', start) + 2
        pieces.append(source[start:end])
    (out / 'scene_spawn_under_test.inc').write_text('\n\n'.join(pieces) + '\n')
    binary = out / 'scene-spawn'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
        '-Isrc', '-I' + str(out), 'tests/area00_spawn_binding_test.c', 'src/game/em_actor_pool.c',
        'src/game/em_actor_roster.c', 'src/game/em_area01_actor_view.c', '-o', str(binary)], cwd=ROOT, check=True)
    subprocess.run([str(binary), str(A0.OVERLAY_PATH)], cwd=ROOT, check=True,
                   env=dict(os.environ, UBSAN_OPTIONS='halt_on_error=1'))


def record(original, condition=0, cls=7, key=1, flags=0):
    result = bytearray(original)
    struct.pack_into('<hHH', result, 0, condition, key, cls)
    struct.pack_into('<H', result, 8, flags)
    return bytes(result)


def main():
    started = time.monotonic()
    out = ROOT / 'build/level3/spawn-binding'
    out.mkdir(parents=True, exist_ok=True)
    check_live(out)
    lib = T.load_native()
    fn = lib.em_actor_roster_spawn_001B6660
    fn.restype = C.c_int
    fn.argtypes = [C.POINTER(Group), T.vp, T.vp, T.vp, T.Bind, T.vp, T.vp, C.POINTER(T.vp)]
    elf = (A0.DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == T.ELF_SHA256
    module = A0.OVERLAY_PATH.read_bytes()
    assert hashlib.sha256(module).hexdigest() == A0.OVERLAY_SHA256
    cab = module[0x827A30 - 0x823500:0x827A5C - 0x823500]
    ram = (A0.ARRIVAL / 'eeMemory.bin').read_bytes()
    captured = ram[T.PROGRESS_BASE:T.PROGRESS_END]
    zero = bytes(len(captured))
    ordinary = record(cab)
    skipped = record(cab, condition=2)
    cases = [
        ('original cab, captured progress', [cab], captured, 0),
        ('empty', [], zero, 0),
        ('two successful allocations', [ordinary, ordinary], zero, 0),
        ('last allocation fails', [ordinary, ordinary], zero, 255),
        ('last condition skips', [ordinary, skipped], bytes([255]) * len(zero), 0),
        ('all allocations fail', [ordinary, ordinary], zero, 256),
        ('reserve refuses final allocation', [ordinary, record(cab, cls=12)], zero, 248),
    ]
    for condition in range(1, 7):
        for value in (0, 1, 255):
            for flags in ((0, 0x80) if condition == 6 else (0,)):
                cases.append((f'condition {condition}, progress {value}, flags {flags}',
                              [record(cab, condition, 7, 1, flags)], bytes([value]) * len(zero), 0))
    if FULL:
        for condition in (0, 7, -2, 0x7FFF):
            for cls in (2, 0x22, 0x82, 0x102, 0xFF02):
                cases.append((f'extended condition {condition}, class {cls}',
                              [record(cab, condition, cls)], zero, 0))
    total = spawned = failed = 0
    for name, records, progress, prefill in cases:
        o, n = T.RosterOracle(elf), T.Native(lib)
        try:
            o.byte(T.D_700, 0); o.byte(T.D_701, 0); o.byte(T.D_702, 0)
            o.set_progress(progress); n.set_scene(0, 0, 0); n.set_progress(progress)
            o.call(T.RESET); n.reset()
            for _ in range(prefill):
                assert o.call(T.ALLOC, [7]) == n.alloc(7)
            address = T.SYN_BASE + 0x1000
            data = b''.join(records)
            for i, value in enumerate(data + b'\xff\xff'):
                o.save(address + i, value, 1)
            expected = o.call(0x1B6660, [address])
            storage = C.create_string_buffer(data or b'\0')
            group = Group(address, len(records), C.addressof(storage))
            result = T.vp(0xCAFE)
            assert fn(C.byref(group), n.pool, n.scene, n.progress_ptr, T.NO_BIND,
                      None, n.log, C.byref(result)) == 0, (name, n.fault())
            actual = n.addr(result.value) if result.value else 0
            assert actual == expected, (name, 'return', hex(actual), hex(expected))
            T.compare(o, n, n.entries(), name)
            total += 1; spawned += len(n.entries()); failed += n.counters()[1]
        finally:
            n.close()
    n = T.Native(lib)
    try:
        n.reset(); n.set_scene(0, 0, 0); n.set_progress(zero)
        storage = C.create_string_buffer(ordinary)
        group = Group(T.SYN_BASE, 1, C.addressof(storage))
        result = T.vp(0xCAFE)
        reject = T.Bind(lambda *_: -1)
        assert fn(C.byref(group), n.pool, n.scene, n.progress_ptr, reject,
                  None, n.log, C.byref(result)) < 0
        assert result.value == 0xCAFE and n.fault()[0] == T.FAULT_WORKER_FAILED
    finally:
        n.close()
    banner(f'{total} original deferred-spawn cases', f'{spawned} spawned records',
           f'{failed} original allocation failures', 'whole pool/progress/list/return equality',
           'live module and actor-view fixture (ASan/UBSan)', f'{time.monotonic() - started:.1f}s')


if __name__ == '__main__':
    main()
