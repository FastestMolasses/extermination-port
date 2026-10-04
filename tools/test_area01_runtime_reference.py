#!/usr/bin/env python3
"""Cross-module AREA01 composition against original instructions.

Model/pose allocation is an explicit scripted boundary here (the separate
model oracle proves that adapter). Identity/copy leaves execute original
code on both sides. No capture bytes or original instructions are exported.
"""
import ctypes as C
import json
import random
import struct
import subprocess
import time
from pathlib import Path

import reference_mode as mode
from test_player_fall_reference import FallEE
from test_player_slide_reference import read_elf, STACK_TOP

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/level2/runtime'
MASK = (1 << 64) - 1
NODE = 0x1E00000

class Call(C.Structure):
    _fields_ = [('function', C.c_uint32), ('sp', C.c_uint32), ('a', C.c_uint64 * 8),
                ('f', C.c_uint32 * 8), ('na', C.c_uint32), ('nf', C.c_uint32),
                ('v0', C.c_uint64), ('f0', C.c_uint32)]

View = C.CFUNCTYPE(C.c_void_p, C.c_void_p, C.c_uint32, C.c_uint32, C.c_int)
Worker = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(Call))
class Host(C.Structure):
    _fields_ = [('ctx', C.c_void_p), ('bytes', View), ('worker', Worker)]


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / 'runtime.dylib'
    sources = ['tests/area01_runtime_bridge.c'] + [f'src/game/em_area01_{s}.c' for s in
        ('runtime', 'math_core', 'math_actor', 'math_owner', 'light_owner', 'overlay',
         'overlay_826d40', 'sys', 'exita', 'exitb', 'room', 'side')]
    sources.append('src/game/em_stream_lanes_original.c')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-shared', '-fPIC', '-Isrc', *sources, '-lm', '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    native.a01rt_bind.argtypes = [C.POINTER(Host)]
    native.a01rt_call.argtypes = [C.POINTER(Call)]
    native.a01rt_fault.restype = C.c_uint32
    native.a01rt_stream_worker.argtypes = [C.POINTER(Call)]
    return native


def sampler_cases(native, elf):
    """Runtime -> sole ROOM sampler -> actual native stream conversion owner.

    The oracle executes the original entry and all callees without hooks.
    Captured bytes initialize test fixtures only; the live binding reads the
    ELF-exported immutable rows and its existing allocated slot instead.
    """
    from export_area01_common import captures, read_elf as pinned_elf, u32
    from export_effect_tables import BLOCKS, elf_block
    windows = ((0x24FD50, 0x9F4), (0x275638, 0x14))
    source = pinned_elf()
    for span in windows:
        assert span in BLOCKS, ('missing sampler boot export', span)
    total = boundaries = compared = 0

    def run(original, slot, step, label):
        nonlocal total, boundaries
        baseline, spad_start = bytes(original.mem), bytes(original.spad)
        original.call(0x1D0D60, (slot,), (step,))
        ram = (C.c_uint8 * len(baseline)).from_buffer_copy(baseline)
        spr = (C.c_uint8 * len(spad_start)).from_buffer_copy(spad_start)
        seen, errors = [], []
        def resolve(_, address, size, write):
            for base, data in ((0, ram), (0x70000000, spr)):
                if size and address >= base and address + size <= base + len(data):
                    return C.addressof(data) + address - base
            return None
        def worker(_, cp):
            try:
                c = cp.contents
                assert c.sp == STACK_TOP - 0x60, hex(c.sp)
                assert native.a01rt_stream_worker(cp) == 0
                seen.append((c.function, c.f[0], c.v0))
                return 0
            except Exception as error:
                errors.append(repr(error)); return -1
        view, work = View(resolve), Worker(worker)
        host = Host(None, view, work)
        assert native.a01rt_bind(C.byref(host)) == 0
        c = Call(function=0x1D0D60, sp=STACK_TOP, na=1, nf=1)
        c.a[0] = slot; c.f[0] = struct.unpack('<I', struct.pack('<f', step))[0]
        assert native.a01rt_call(C.byref(c)) == 0, (label, errors, hex(native.a01rt_fault()))
        assert not errors and len(seen) in (3, 4), (label, seen, errors)
        assert c.v0 == original.r[2], (label, 'return')
        assert bytes(ram) == bytes(original.mem), (label, 'RAM differs')
        assert bytes(spr) == bytes(original.spad), (label, 'scratchpad differs')
        boundaries += len(seen); total += 1

    recorded = captures()
    for capture in recorded:
        for address, size in windows:
            assert capture.ram[address:address + size] == elf_block(source, address, size), (capture.name, hex(address))
            compared += size
        actor = 0x7A93F0
        assert u32(capture.ram, actor + 0x10) == 0x1C02E0
        assert capture.ram[actor + 8] == 0 and capture.ram[actor + 2] & 31 == 2
        slot = u32(capture.ram, actor + 0x90)
        assert u32(capture.ram, slot) == 0x24FD50
        original = FallEE(elf); original.mem[:] = capture.ram
        original.spad[:] = capture.spad
        run(original, slot, 1.0, capture.name)
    capture_count = len(recorded)
    del recorded

    # Same real 91-row boot table: both loop branches, time before/at/past
    # the end, wrap more than once, and fractional interpolation.
    cases = [(flag, t, step) for flag in (0, 1, 0x80)
             for t in (0., .5, 89., 89.5, 90., 90.5, 91., 181.5, 273.)
             for step in (0., .5, 1., 2., 91.)]
    chosen = mode.select(cases, 18, 0xD0D60,
                         axes=(lambda x: x[0], lambda x: x[2]),
                         keep=lambda i, x: x[1] in (90., 91.) and x[2] == 1.)
    for flag, t, step in chosen:
        original = FallEE(elf); slot = NODE
        original.write(slot, bytes([0xA5]) * 0xD0)
        original.write(slot, struct.pack('<Iff', 0x24FD50, 91., t))
        original.save(slot + 0xC, flag, 1)
        run(original, slot, step, (flag, t, step))
    return total, boundaries, capture_count, compared


def main():
    start = time.monotonic()
    native = build()
    elf = read_elf()
    rng = random.Random(0xA01004)
    count = mode.pick(320, 32)
    boundaries = 0
    # math -> EXITB and SYS -> EXITA (including direct EXITA subcalls).
    roots = (0x1BB860, 0x1BB520, 0x128C10, 0x128AB0)
    policies = {0x1B0FD0: (1, 0), 0x1B10B0: (3, 0), 0x1029C0: (1, 0),
                0x102948: (2, 0), 0x1C63E0: (2, 0)}
    for index in range(count):
        entry = roots[index % len(roots)]
        original = FallEE(elf)
        initial = bytes(rng.randrange(256) for _ in range(0x400))
        original.write(NODE, initial)
        original.save(NODE + 4, 0, 2)
        original.save(NODE + 0xD, rng.randrange(256), 1)
        original.save(0x810788, rng.choice((0, 0xFF)), 1)
        original.save(0x81070A, rng.randrange(3), 1)
        original.save(0x28A4C8, rng.getrandbits(32))
        baseline, spad_start = bytes(original.mem), bytes(original.spad)
        scripted_result = rng.choice((0, 0, 0, 1, 0xFFFFFFFF))
        log = []

        def action(ee, fn):
            if fn in (0x1029C0, 0x102948):
                saved = ee.hooks
                ee.hooks = {k: v for k, v in saved.items() if k != fn}
                try:
                    v0, f0 = ee.nested(fn)
                finally:
                    ee.hooks = saved
                ee.r[2], ee.f[0] = v0, f0
            elif fn in (0x1B0FD0, 0x1B10B0):
                # Explicit boundary effects, deliberately unlike a full model
                # allocator: caller reload order and sign-extension are tested.
                ee.save(NODE + 4, 3 if scripted_result else 1, 1)
                ee.save(NODE + 0x44, 0x123450)
                ee.r[2] = (scripted_result if scripted_result < 0x80000000
                           else scripted_result - (1 << 32)) & MASK
            elif fn == 0x1C63E0:
                ee.save(NODE + 0x110, 0x7D5840)
            else:
                raise AssertionError(hex(fn))

        def hook(fn):
            def run(ee):
                na, nf = policies[fn]
                before = ee.read(NODE, 0x400)
                args = tuple(ee.r[4+i] & MASK for i in range(na))
                sp = ee.r[29]
                action(ee, fn)
                log.append((fn, args, sp, before, ee.read(NODE, 0x400)))
            return run
        original.hooks = {fn: hook(fn) for fn in policies}
        args = (NODE, NODE + 0x1F0) if entry == 0x128AB0 else (NODE,)
        original.call(entry, args)

        ram = (C.c_uint8 * len(baseline)).from_buffer_copy(baseline)
        spr = (C.c_uint8 * len(spad_start)).from_buffer_copy(spad_start)
        stack = (C.c_uint8 * 0x100000)()
        leaves = FallEE(elf)
        leaves.mem = memoryview(ram).cast('B')
        leaves.spad = memoryview(spr).cast('B')
        leaves.stack = memoryview(stack).cast('B')
        errors, seen = [], []
        def resolve(_, address, size, write):
            for base, data in ((0, ram), (0x70000000, spr), (0x7F000000, stack)):
                if address >= base and size and address + size <= base + len(data):
                    return C.addressof(data) + address - base
            return None
        def worker(_, cp):
            try:
                c = cp.contents
                k = len(seen)
                assert k < len(log), ('extra worker', hex(c.function))
                fn, expected, sp, before, after = log[k]
                assert c.function == fn
                assert tuple(c.a[:len(expected)]) == expected
                assert C.string_at(C.addressof(ram) + NODE, 0x400) == before
                # SYS/EXITA expose exact original stack offsets. Math's API
                # has no frame-stack parameter; no compared callee touches it.
                if entry != 0x1BB860: assert c.sp == sp, (hex(entry), hex(c.sp), hex(sp))
                for i in range(c.na): leaves.r[4+i] = c.a[i]
                for i in range(c.nf): leaves.f[12+i] = c.f[i]
                leaves.r[29] = c.sp
                action(leaves, fn)
                c.v0, c.f0 = leaves.r[2], leaves.f[0]
                assert C.string_at(C.addressof(ram) + NODE, 0x400) == after
                seen.append(fn)
                return 0
            except Exception as error:
                errors.append(repr(error)); return -1
        view, work = View(resolve), Worker(worker)
        host = Host(None, view, work)
        assert native.a01rt_bind(C.byref(host)) == 0
        call = Call(function=entry, sp=STACK_TOP, na=len(args))
        for i, arg in enumerate(args): call.a[i] = arg
        rc = native.a01rt_call(C.byref(call))
        assert rc == 0, (index, hex(entry), errors, hex(native.a01rt_fault()))
        assert not errors and len(seen) == len(log)
        assert bytes(ram) == bytes(original.mem), (index, 'RAM differs')
        assert bytes(spr) == bytes(original.spad), (index, 'scratchpad differs')
        if entry in (0x1BB520, 0x128AB0): assert call.v0 == original.r[2]
        boundaries += len(seen)

    # Duct camera: new room dispatch with unhooked original SDK/vector
    # callees on both sides. Covers each room state and its action-11 exit.
    camera_cases=0
    for state in (0,1,2):
        for action in (0x11,0x12):
            original=FallEE(elf)
            original.write(0x8101E0,bytes(0xD0));original.write(0x8102B0,bytes(0x320))
            original.save(0x8101E1,state,1);original.save(0x8104E0,action)
            original.write(0x810350,struct.pack('<4f',-35.,2.,71.,1.))
            original.write(0x810370,struct.pack('<4f',.1,.5,-.2,0.))
            original.write(0x8105D0,struct.pack('<8f',11.,12.,13.,1.,-5.,4.,27.,1.))
            original.save(0x26C5D0,0xFFFFFFFF)
            baseline,spad_start=bytes(original.mem),bytes(original.spad)
            original.call(0x198D90,(0x8101E0,0x8102B0))
            ram=(C.c_uint8*len(baseline)).from_buffer_copy(baseline)
            spr=(C.c_uint8*len(spad_start)).from_buffer_copy(spad_start)
            leaves=FallEE(elf);leaves.mem=memoryview(ram).cast('B');leaves.spad=memoryview(spr).cast('B')
            errors=[];seen=[]
            def resolve_camera(_,address,size,write):
                for base,data in ((0,ram),(0x70000000,spr)):
                    if size and address>=base and address+size<=base+len(data):
                        return C.addressof(data)+address-base
                return None
            def camera_worker(_,cp):
                try:
                    c=cp.contents;seen.append(c.function)
                    assert c.sp==STACK_TOP-0x30,(hex(c.function),hex(c.sp))
                    for k in range(c.na):leaves.r[4+k]=c.a[k]
                    for k in range(c.nf):leaves.f[12+k]=c.f[k]
                    leaves.r[29]=c.sp;c.v0,c.f0=leaves.nested(c.function)
                    return 0
                except Exception as error:errors.append(repr(error));return -1
            view,work=View(resolve_camera),Worker(camera_worker);host=Host(None,view,work)
            assert native.a01rt_bind(C.byref(host))==0
            c=Call(function=0x198D90,sp=STACK_TOP,na=2);c.a[0]=0x8101E0;c.a[1]=0x8102B0
            assert native.a01rt_call(C.byref(c))==0,(state,action,errors,hex(native.a01rt_fault()))
            assert not errors and bytes(ram)==bytes(original.mem) and bytes(spr)==bytes(original.spad)
            assert (0x18C4B0 in seen)==(state==1) and (0x18C6A0 in seen)==(state==1)
            camera_cases+=1;boundaries+=len(seen)

    sampler_count, sampler_workers, sampler_captures, sampler_bytes = sampler_cases(native, elf)
    boundaries += sampler_workers

    # First failure is sticky and cannot be bypassed by another module.
    faults = 0
    for fn, na, nf, address in ((0xDEADC0DE, 0, 0, 0xDEADC0DE),
                               (0x128AB0, 1, 0, 0x128AB0),
                               (0x1B0D80, 1, 0, 0xFFFFFFF4),
                               (0x1D0D60, 0, 1, 0x1D0D60),
                               (0x1D0D60, 1, 0, 0x1D0D60)):
        empty = View(lambda *args: None)
        refused = Worker(lambda *args: -1)
        host = Host(None, empty, refused)
        assert native.a01rt_bind(C.byref(host)) == 0
        call = Call(function=fn, na=na, nf=nf)
        call.a[0] = 0xFFFFFF40
        assert native.a01rt_call(C.byref(call)) == -1
        assert native.a01rt_fault() == address, (hex(fn), hex(native.a01rt_fault()), hex(address))
        call.function = 0x1E7CB0
        assert native.a01rt_call(C.byref(call)) == -1 and native.a01rt_fault() == address
        faults += 1
    result = dict(cases=count, camera_room_cases=camera_cases, worker_boundaries=boundaries, fault_cases=faults,
                  sampler_cases=sampler_count, sampler_workers=sampler_workers,
                  sampler_captures=sampler_captures, sampler_export_bytes=sampler_bytes,
                  seconds=round(time.monotonic()-start, 3))
    (OUT / ('full.json' if mode.FULL else 'quick.json')).write_text(json.dumps(result, indent=2)+'\n')
    print('AREA01 runtime: PASS', result)

if __name__ == '__main__': main()
