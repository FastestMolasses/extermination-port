#!/usr/bin/env python3
"""Execute the original AREA11 security gun 0x825940 (every lifecycle, with
its overlay helpers 0x826F30 and 0x827400) and 001B1190, and compare the
native em_security_gun_rest (docs/SECURITY_GUN.md).

The oracle is the shared FallEE core (COP1 through tools/ee_float_model.py)
over copy-on-write views of the captured AREA11 RAM; the overlay code runs
from that RAM (asserted equal to the user's extract/OVERLAY/AREA11.BIN).
Callees:
- the SDK float leaves (0011E2A8, 0011DF78, 0011DBB8, 0011E748, 0011E520,
  001B1470) and the SDK vector leaves (00102948, 00102958, 001026A0,
  001028B8, 001028D0, 00102738, 00102760, 001031E0) execute as original
  instructions nested inside the call; their inputs and outputs are logged;
- every other callee (owner services, rand, the pool alloc, sound, the
  collision probes, effects, the sight draw) is a hook that logs its
  arguments and answers from a per-case script; a probe hook also writes a
  scripted result block into scratchpad 0x70003190..0x700031DB.
The native module then runs over its own copy of the same memory; its
workers must be called in the same order with the same arguments (vector
arguments compared by address and content) and replay the logged outputs.
Compared: the result, the ordered calls, every byte either side wrote (final
values), and the same byte set at every callee entry (memory checked at
each call). Every conditional branch of 0x825940, 0x826F30 and 0x827400
must be observed both ways.

Captures: every AREA11 RAM image is ticked from its captured gun; the
C7 per-call rand capture's lifecycle-0 draw (return address 0x8259F0) must
reproduce the captured timer +0x28, and the native setup over that draw
the captured bone-3 +0x78 word and child +0xA0 quad.

Quick mode (default) runs a covering sample; EM_TEST_FULL=1 runs every
generated case. At most 4 worker processes. No original instruction bytes,
disassembly or game data are written by this file.
"""
import ctypes as C
import json
import os
import random
import struct
import subprocess
import sys
from pathlib import Path

import numpy as np

os.environ.setdefault('EM_TEST_JOBS', '4')
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from reference_mode import FULL, banner, parallel_map, pick  # noqa: E402
import test_script_door_fan_reference as sdf  # noqa: E402
import test_player_slide_reference as shared  # noqa: E402
from test_player_fall_reference import nested_bits  # noqa: E402
from test_player_slide_reference import bits, sx32  # noqa: E402

MASK = 0xFFFFFFFF
MASK64 = (1 << 64) - 1
DECOMP = ROOT.parent / 'Extermination'
OUT = ROOT / 'build' / 'security_gun_rest'
RAND_CAPTURE = DECOMP / 'build/s87/c7cap/rng/newgame/rand.jsonl'

GUN, CHILD_NODE = 0x7A6AD0, 0x7ADD60      # the captured AREA11 nodes
PLAYER, CABLE = 0x8102B0, 0x7A6DC0
FACE = 0x01D60000                               # scratch collision face (test data)
SHOT_NODE = 0x01D61000                          # scratch pool node (test data)
RANGES = {'gun': (0x825940, 0x826F2C), 'sight': (0x826F30, 0x8273FC),
          'shot': (0x827400, 0x827484), '0021AAC0': (0x21AAC0, 0x21AE84),
          '0021A500': (0x21A500, 0x21AABC), '001EFEB0': (0x1EFEB0, 0x1EFF0C)}
SETUP_COPY = (0x8259AC, 0x825B74)   # em_gun_tick inlines this range's two 00102948 copies

MATH = {0x11E2A8: 'w_0011E2A8', 0x11DF78: 'w_0011DF78', 0x11DBB8: 'w_0011DBB8',
        0x11E748: 'w_0011E748', 0x11E520: 'w_0011E520', 0x1B1470: 'w_001B1470'}
LEAVES = {0x102948: ('w_00102948', ('d16', 's16')), 0x102958: ('w_00102958', ('d64', 's64')),
          0x1026A0: ('w_001026A0', ('d16', 's64', 's16')),
          0x1028B8: ('w_001028B8', ('d16', 's16', 's16')),
          0x1028D0: ('w_001028D0', ('d16', 's16', 's16')),
          0x102738: ('w_00102738', ('s16', 's16')), 0x102760: ('w_00102760', ('d16', 's16')),
          0x1031E0: ('w_001031E0', ('d16', 's16')),
          0x1029C0: ('w_001029C0', ('d64',)), 0x102B08: ('w_00102B08', ('d64', 's64', 'f')),
          0x102918: ('w_00102918', ('d64', 's64', 's16')),
          0x103230: ('w_00103230', ('d16', 's16', 'f'))}
FX_NODE, FX_CHILD = 0x01D63000, 0x01D64000      # scratch pool nodes (test data)


def s32(v): v &= MASK; return v - 0x100000000 if v & 0x80000000 else v
def f32b(x): return struct.unpack('<I', struct.pack('<f', x))[0]


# ======================================================================
# Native library
# ======================================================================

V, U8, U32, I32, U64 = C.c_void_p, C.c_uint8, C.c_uint32, C.c_int32, C.c_uint64
P32, PI32, PB = C.POINTER(C.c_uint32), C.POINTER(C.c_int32), C.c_void_p
F = C.CFUNCTYPE
MEMFN = F(C.c_void_p, V, U32, U32)
MATHFN = F(C.c_int, V, U32, P32)
WORKERS = [
    ('w_001B0FD0', F(C.c_int, V, U32, PI32)), ('w_001C6380', F(C.c_int, V, U32)),
    ('w_001A2370', F(C.c_int, V, U32, PB)), ('w_001B17A0', F(C.c_int, V, U32)),
    ('w_draw_4C', F(C.c_int, V, U32, U32)), ('w_001AFC10', F(C.c_int, V, U32)),
    ('w_001AFA90', F(C.c_int, V, U8, P32)), ('w_001FBD50', F(C.c_int, V, U32, I32, I32, U32)),
    ('w_00122BB8', F(C.c_int, V, PI32)),
    ('w_0011E2A8', MATHFN), ('w_0011DF78', MATHFN), ('w_0011DBB8', MATHFN),
    ('w_0011E748', MATHFN), ('w_0011E520', MATHFN), ('w_001B1470', MATHFN),
    ('w_00102948', F(C.c_int, V, PB, PB)), ('w_00102958', F(C.c_int, V, PB, PB)),
    ('w_001026A0', F(C.c_int, V, PB, PB, PB)), ('w_001028B8', F(C.c_int, V, PB, PB, PB)),
    ('w_001028D0', F(C.c_int, V, PB, PB, PB)), ('w_00102738', F(C.c_int, V, PB, PB, P32)),
    ('w_00102760', F(C.c_int, V, PB, PB)), ('w_001031E0', F(C.c_int, V, PB, PB)),
    ('w_0019AA80', F(C.c_int, V, PB, PB, I32, PI32)),
    ('w_0019A570', F(C.c_int, V, PB, PB, I32, I32, PI32)),
    ('w_0019B6C0', F(C.c_int, V, PB, PB, PI32)),
    ('w_001EFD90', F(C.c_int, V, U32, PB, PB, P32)),
    ('w_001CD520', F(C.c_int, V, I32, I32, PB, U64, U32, U32, U32, U32)),
    ('w_001E2BA0', F(C.c_int, V, PB, PB, PB, U32)),
    ('w_001029C0', F(C.c_int, V, PB)), ('w_00102B08', F(C.c_int, V, PB, PB, U32)),
    ('w_00102918', F(C.c_int, V, PB, PB, PB)), ('w_001CCF70', F(C.c_int, V, PB, PI32)),
    ('w_001CFA60', F(C.c_int, V, PB, PB, U32, U32)),
    ('w_001CFBE0', F(C.c_int, V, I32, I32, U32, PB, I32)),
    ('w_001EFEB0', F(C.c_int, V, U32, PB, P32)),
    ('w_001CE860', F(C.c_int, V, I32, I32, PB, PB, U32, I32, U64)),
    ('w_00103230', F(C.c_int, V, PB, PB, U32)),
    ('w_001CFB50', F(C.c_int, V, PB, I32, PB, U32, U32, U32, U32, U32)),
    ('w_001281C0', F(C.c_int, V, U32, PI32)),
    ('w_001EF9D0', F(C.c_int, V, U32, PB, U32, P32)),
]


class Mem(C.Structure):
    _fields_ = [('ctx', V), ('load', MEMFN), ('store', MEMFN)]


class Workers(C.Structure):
    _fields_ = [('ctx', V)] + WORKERS


class Fault(C.Structure):
    _fields_ = [('address', C.c_uint32), ('code', C.c_int32)]


LAYOUT = r'''
#include <stddef.h>
#include <stdio.h>
#include "game/em_security_gun_rest.h"
int main(void) {
  printf("%zu %zu %zu %zu %zu\n", sizeof(EmGunRestMem), sizeof(EmGunRestWorkers),
         offsetof(EmGunRestWorkers, w_001CD520), offsetof(EmGunRestWorkers, w_001EF9D0),
         sizeof(EmGunFault));
  return 0; }
'''


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / 'security_gun_rest.dylib'
    # EM_GUN_REST_MODULE (the mutation check) names another em_security_gun_rest.c.
    sources = [os.environ.get('EM_GUN_REST_MODULE', 'src/game/em_security_gun_rest.c'),
               'src/game/em_security_gun.c']
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc', *sources, '-o', str(lib)],
                   cwd=ROOT, check=True)
    probe = OUT / 'layout.c'
    probe.write_text(LAYOUT)
    subprocess.run(['cc', '-std=c11', '-Isrc', str(probe), '-o', str(OUT / 'layout')], cwd=ROOT,
                   check=True)
    got = [int(x) for x in subprocess.run([str(OUT / 'layout')], capture_output=True, text=True,
                                          check=True).stdout.split()]
    mine = [C.sizeof(Mem), C.sizeof(Workers), Workers.w_001CD520.offset,
            Workers.w_001EF9D0.offset, C.sizeof(Fault)]
    assert got == mine, ('ctypes layout differs from C', got, mine)
    native = C.CDLL(str(lib))
    native.em_gun_rest_tick.argtypes = [U32, C.POINTER(Mem), C.POINTER(Workers),
                                                 C.POINTER(Fault)]
    native.em_gun_rest_001B1190.argtypes = [I32, C.POINTER(Mem), C.POINTER(Fault)]
    native.em_gun_rest_0021AAC0.argtypes = [U32, C.POINTER(Mem), C.POINTER(Workers),
                                            C.POINTER(Fault)]
    native.em_gun_rest_0021A500.argtypes = [U32, C.POINTER(Mem), C.POINTER(Workers),
                                            C.POINTER(Fault)]
    native.em_gun_rest_001EFEB0.argtypes = [U32, PB, C.POINTER(Mem), C.POINTER(Workers), P32,
                                            C.POINTER(Fault)]
    return native


CONTEXT = {}


# ======================================================================
# Per-case script and the two sides
# ======================================================================

class Script:
    """Answers for the hooked callees, by name and call index (shared by
    every tick of a run; `tick` is the current tick)."""
    def __init__(self, **answers):
        self.a = answers
        self.n = {}
        self.tick = 0

    def next(self, name, default=0):
        n = self.n.get(name, 0)
        self.n[name] = n + 1
        if name not in self.a: return default
        v = self.a[name]
        if callable(v): return v(n, self.tick)
        if isinstance(v, (list, tuple)): return v[n] if n < len(v) else v[-1]
        return v


def is_local(a): return 0x7F000000 <= (a & MASK) < 0x7F100000


class OracleSide:
    def __init__(self, image, seeds, script, keys):
        self.o = o = sdf.Oracle(image)
        for address, value, size in seeds: o.seed(address, value, size)
        self.script, self.keys = script, keys
        self.log = []
        self.callee = set()
        self.node = None
        self.install()

    def snap(self):
        if self.keys is None: return None
        return bytes(self.o.load(k, 1) for k in self.keys)

    def content(self, a, n):
        return bytes(self.o.load(a + i, 1) for i in range(n))

    def tok(self, a):
        a &= MASK
        return 'local' if is_local(a) else a

    def emit(self, key, out=None):
        self.log.append([key, out, self.snap()])

    def out(self, value):
        self.log[-1][1] = value

    def through(self, e, address, args, fregs):
        """Run the original instructions of a hooked leaf (hooks of other
        leaves it calls pass through too); returns (v0, f0) and sets them."""
        hooked = e.hooks.pop(address)
        self.depth += 1
        try:
            r2, f0 = nested_bits(e, address, args, fregs)
        finally:
            self.depth -= 1
            e.hooks[address] = hooked
        e.r[2], e.f[0] = r2, f0
        return r2, f0

    def install(self):
        o = self.o
        self.depth = 0

        def actor(e):
            assert e.r[4] & MASK == self.node, ('actor argument', hex(e.r[4] & MASK))
            return self.node

        def service(name):
            def fn(e): self.emit((name, actor(e)))
            return fn

        def b0fd0(e):
            self.emit(('w_001B0FD0', actor(e)))
            v = self.script.next('w_001B0FD0')
            if v == 0: e.save(self.node + 4, (e.load(self.node + 4, 1) + 1) & 255, 1)
            elif v == 1: e.save(self.node + 4, 3, 1)
            self.out(v)
            e.r[2] = v

        def a2370(e):
            self.emit(('w_001A2370', actor(e), self.tok(e.r[5]), self.content(e.r[5], 64)))

        def alloc(e):
            self.emit(('w_001AFA90', e.r[4] & 0xFF))
            v = self.script.next('w_001AFA90')
            self.out(v)
            e.r[2] = v

        def sound(e):
            self.emit(('w_001FBD50', actor(e), s32(e.r[5]), s32(e.r[6]), e.f[12] & MASK))

        def rand(e):
            self.emit(('w_00122BB8',))
            v = self.script.next('w_00122BB8') & MASK
            self.out(v)
            e.r[2] = sx32(v)

        def math(address, name):
            def fn(e):
                if self.depth:
                    return self.through(e, address, (), (e.f[12] & MASK,))
                x = e.f[12] & MASK
                snap = self.snap()
                value = self.through(e, address, (), (x,))[1] & MASK
                self.log.append([(name, x), value, snap])
            return fn

        def leaf(address, name, kinds):
            def fn(e):
                pk = [k for k in kinds if k != 'f']
                args = [e.r[4 + i] & MASK for i in range(len(pk))]
                fregs = (e.f[12] & MASK,) if 'f' in kinds else ()
                ra = e.r[31] & MASK
                if self.depth or (SETUP_COPY[0] <= ra < SETUP_COPY[1] and address == 0x102948):
                    return self.through(e, address, tuple(args), fregs)
                snap = self.snap()
                ins = tuple((self.tok(a), self.content(a, int(k[1:])) if k[0] == 's' else None)
                            for a, k in zip(args, pk)) + tuple(('f', x) for x in fregs)
                r2, f0 = self.through(e, address, tuple(args), fregs)
                outs = tuple(self.content(a, int(k[1:])) if k[0] == 'd' else None
                             for a, k in zip(args, pk))
                self.log.append([(name, ins), (outs, f0 & MASK if address == 0x102738 else None),
                                 snap])
            return fn

        def probe(name, nargs):
            def fn(e):
                args = [e.r[4 + i] & MASK for i in range(2)]
                ints = tuple(s32(e.r[6 + i]) for i in range(nargs - 2))
                self.emit((name, tuple((self.tok(a), self.content(a, 16)) for a in args), ints))
                result, block = self.script.next(name, (0, {}))
                for address, (value, size) in sorted(block.items()):
                    e.save(address, value, size, track=False)
                    self.callee.update(range(address, address + size))
                self.out((result, tuple(sorted(block.items()))))
                e.r[2] = sx32(result)
            return fn

        def efd90(e):
            self.emit(('w_001EFD90', e.r[4] & MASK, self.tok(e.r[5]), self.content(e.r[5], 16),
                       self.tok(e.r[6]), self.content(e.r[6], 16)))
            v = self.script.next('w_001EFD90', 0)
            self.out(v)
            e.r[2] = v

        def cd520(e):
            self.emit(('w_001CD520', s32(e.r[4]), s32(e.r[5]), self.tok(e.r[6]),
                       self.content(e.r[6], 16), e.r[7] & MASK64, e.r[8] & MASK,
                       e.f[12] & MASK, e.f[13] & MASK, e.f[14] & MASK))

        def e2ba0(e):
            self.emit(('w_001E2BA0',) + tuple(x for i in range(3) for x in
                                               (self.tok(e.r[4 + i]), self.content(e.r[4 + i], 16)))
                      + (e.f[12] & MASK,))

        def cf70(e):
            self.emit(('w_001CCF70', self.tok(e.r[4]), self.content(e.r[4], 16)))
            v = self.script.next('w_001CCF70', 0)
            self.out(v)
            e.r[2] = sx32(v)

        def cfa60(e):
            self.emit(('w_001CFA60', self.tok(e.r[4]), self.tok(e.r[5]), self.content(e.r[5], 64),
                       e.f[12] & MASK, e.f[13] & MASK))
            block = self.script.next('w_001CFA60', bytes(0x60))
            for i, byte in enumerate(block): e.save((e.r[4] & MASK) + i, byte, 1, track=False)
            self.out(block)

        def cfbe0(e):
            self.emit(('w_001CFBE0', s32(e.r[4]), s32(e.r[5]), e.r[6] & MASK, self.tok(e.r[7]),
                       self.content(e.r[7], 0x60), s32(e.r[8])))

        def efeb0(e):
            self.emit(('w_001EFEB0', e.r[4] & MASK, self.tok(e.r[5]), self.content(e.r[5], 64)))
            v = self.script.next('w_001EFEB0', 0)
            self.out(v)
            e.r[2] = v

        def ce860(e):
            count = s32(e.r[8])
            self.emit(('w_001CE860', s32(e.r[4]), s32(e.r[5]), self.tok(e.r[6]),
                       self.content(e.r[6], 16 * (count if count > 0 else 1)), self.tok(e.r[7]),
                       self.content(e.r[7], 16), e.f[12] & MASK, count, e.r[9] & MASK64))

        def cfb50(e):
            self.emit(('w_001CFB50', self.tok(e.r[4]), s32(e.r[5]), self.tok(e.r[6]),
                       self.content(e.r[6], 64)) + tuple(e.f[12 + i] & MASK for i in range(5)))
            block = self.script.next('w_001CFB50', bytes(0x60))
            for i, byte in enumerate(block): e.save((e.r[4] & MASK) + i, byte, 1, track=False)
            self.out(block)

        def f2i(e):
            x = e.f[12] & MASK
            snap = self.snap()
            r2 = self.through(e, 0x1281C0, (), (x,))[0]
            self.log.append([('w_001281C0', x), s32(r2), snap])

        def ef9d0(e):
            self.emit(('w_001EF9D0', e.r[4] & MASK, self.tok(e.r[5]), self.content(e.r[5], 16),
                       e.f[12] & MASK))
            v = self.script.next('w_001EF9D0', 0)
            self.out(v)
            e.r[2] = v

        table = {0x1CE860: ce860, 0x1CFB50: cfb50, 0x1281C0: f2i, 0x1EF9D0: ef9d0,
                 0x1CCF70: cf70, 0x1CFA60: cfa60, 0x1CFBE0: cfbe0, 0x1EFEB0: efeb0,
                 0x1B0FD0: b0fd0, 0x1C6380: service('w_001C6380'),
                 0x1B17A0: service('w_001B17A0'), 0x1AFC10: service('w_001AFC10'),
                 0x1A2370: a2370, 0x1AFA90: alloc, 0x1FBD50: sound, 0x122BB8: rand,
                 0x19AA80: probe('w_0019AA80', 3), 0x19A570: probe('w_0019A570', 4),
                 0x19B6C0: probe('w_0019B6C0', 2), 0x1EFD90: efd90, 0x1CD520: cd520,
                 0x1E2BA0: e2ba0}
        for address, name in MATH.items(): table[address] = math(address, name)
        for address, (name, kinds) in LEAVES.items(): table[address] = leaf(address, name, kinds)
        for address, fn in table.items(): sdf.hook(o, address, fn)

    def tick(self, node, entry=0x825940):
        self.node = node
        if entry != 0x825940:
            sdf.run_original(self.o, entry, (node,))
            return
        draw = self.o.load(node + 0x4C)

        def fn(e):
            self.emit(('w_draw_4C', e.r[4] & MASK, draw))
        sdf.hook(self.o, draw, fn)
        sdf.run_original(self.o, 0x825940, (node,))
        del self.o.hooks[draw]

    def written(self):
        return set(self.o.written) | self.callee

    def value(self, k): return self.o.load(k, 1)


class NativeSide:
    def __init__(self, image, seeds, keys, ologs):
        self.ram = np.frombuffer(image, dtype=np.uint8).copy()
        self.spad = np.zeros(0x4000, dtype=np.uint8)
        self.rbase, self.sbase = self.ram.ctypes.data, self.spad.ctypes.data
        for address, value, size in seeds:
            for i in range(size): self.put(address + i, (value >> (8 * i)) & 255)
        self.keys, self.olog = keys, ologs
        self.log = []
        self.stores, self.callee = set(), set()
        self.error = None
        self.keep = []
        self.mem = Mem(None, MEMFN(self.load), MEMFN(self.store))
        self.workers = self.make_workers()

    # memory
    def put(self, k, b):
        if 0x70000000 <= k < 0x70004000: self.spad[k - 0x70000000] = b
        else: self.ram[k] = b

    def value(self, k):
        return int(self.spad[k - 0x70000000]) if 0x70000000 <= k < 0x70004000 else int(self.ram[k])

    def ptr(self, a, n):
        if 0x70000000 <= a and a + n <= 0x70004000: return self.sbase + a - 0x70000000
        if a + n <= 0x2000000: return self.rbase + a
        return None

    def load(self, _, a, n): return self.ptr(a, n)

    def store(self, _, a, n):
        p = self.ptr(a, n)
        if p is not None: self.stores.update(range(a, a + n))
        return p

    def tok(self, p):
        if self.rbase <= p < self.rbase + 0x2000000: return p - self.rbase
        if self.sbase <= p < self.sbase + 0x4000: return p - self.sbase + 0x70000000
        return 'local'

    def snap(self):
        if self.keys is None: return None
        return bytes(self.value(k) for k in self.keys)

    # workers: each call must equal the oracle's next logged call
    def expect(self, entry):
        i = len(self.log)
        self.log.append(entry)
        assert i < len(self.olog), ('native made an extra call', entry[0])
        want = self.olog[i]
        assert want[0] == entry, ('call differs', i, entry, want[0])
        assert want[2] == self.snap(), ('memory differs at callee entry', i, entry[0])
        return want[1]

    def make_workers(self):
        def guard(fn):
            def run(*args):
                if self.error is not None: return -1
                try:
                    return fn(*args)
                except BaseException as error:  # noqa: BLE001
                    self.error = error
                    return -1
            return run

        def content(p, n): return C.string_at(p, n)

        def service(name):
            def fn(_, a): self.expect((name, a)); return 0
            return fn

        def b0fd0(_, a, out):
            out[0] = self.expect(('w_001B0FD0', a)); return 0

        def a2370(_, a, m):
            self.expect(('w_001A2370', a, self.tok(m), content(m, 64))); return 0

        def draw(_, a, cb): self.expect(('w_draw_4C', a, cb)); return 0

        def alloc(_, cls, out):
            out[0] = self.expect(('w_001AFA90', cls)); return 0

        def sound(_, a, cue, a2, f12):
            self.expect(('w_001FBD50', a, cue, a2, f12)); return 0

        def rand(_, out):
            out[0] = s32(self.expect(('w_00122BB8',))); return 0

        def math(name):
            def fn(_, x, out):
                out[0] = self.expect((name, x))
                return 0
            return fn

        def leaf(name, kinds):
            def fn(_, *ptrs):
                result = None
                if name == 'w_00102738': ptrs, result = ptrs[:2], ptrs[2]
                pk = [k for k in kinds if k != 'f']
                fl = tuple(('f', x) for x in ptrs[len(pk):])
                ptrs = ptrs[:len(pk)]
                ins = tuple((self.tok(p), content(p, int(k[1:])) if k[0] == 's' else None)
                            for p, k in zip(ptrs, pk)) + fl
                outs, f0 = self.expect((name, ins))
                for p, out in zip(ptrs, outs):
                    if out is not None: C.memmove(p, out, len(out))
                if result is not None: result[0] = f0
                return 0
            return fn

        def probe(name, nints):
            def fn(_, a, b, *rest):
                ints, out = rest[:nints], rest[nints]
                result, block = self.expect((name, ((self.tok(a), content(a, 16)),
                                                    (self.tok(b), content(b, 16))), tuple(ints)))
                for address, (value, size) in block:
                    for k in range(size): self.put(address + k, (value >> (8 * k)) & 255)
                    self.callee.update(range(address, address + size))
                out[0] = result
                return 0
            return fn

        def efd90(_, fx, p, at, out):
            out[0] = self.expect(('w_001EFD90', fx, self.tok(p), content(p, 16), self.tok(at),
                                  content(at, 16)))
            return 0

        def cd520(_, a0, a1, p, a3, t0, f12, f13, f14):
            # t0 reaches the callee sign-extended from its 32-bit value.
            self.expect(('w_001CD520', a0, a1, self.tok(p), content(p, 16), a3, t0, f12, f13,
                         f14))
            return 0

        def e2ba0(_, a, b, c, f12):
            self.expect(('w_001E2BA0', self.tok(a), content(a, 16), self.tok(b), content(b, 16),
                         self.tok(c), content(c, 16), f12))
            return 0

        def cf70(_, p, out):
            out[0] = s32(self.expect(('w_001CCF70', self.tok(p), content(p, 16)))); return 0

        def cfa60(_, block, m, f12, f13):
            out = self.expect(('w_001CFA60', self.tok(block), self.tok(m), content(m, 64), f12, f13))
            C.memmove(block, out, len(out))
            return 0

        def cfbe0(_, key, kind, table, block, t0):
            self.expect(('w_001CFBE0', key, kind, table, self.tok(block), content(block, 0x60), t0))
            return 0

        def efeb0(_, fx, m, out):
            out[0] = self.expect(('w_001EFEB0', fx, self.tok(m), content(m, 64))); return 0

        def ce860(_, a0, a1, pts, colour, f12, t0, t1):
            self.expect(('w_001CE860', a0, a1, self.tok(pts),
                         content(pts, 16 * (t0 if t0 > 0 else 1)), self.tok(colour),
                         content(colour, 16), f12, t0, t1))
            return 0

        def cfb50(_, block, a1, m, f12, f13, f14, f15, f16):
            out = self.expect(('w_001CFB50', self.tok(block), a1, self.tok(m), content(m, 64),
                               f12, f13, f14, f15, f16))
            C.memmove(block, out, len(out))
            return 0

        def f2i(_, x, out):
            out[0] = self.expect(('w_001281C0', x)); return 0

        def ef9d0(_, fx, pos, f12, out):
            out[0] = self.expect(('w_001EF9D0', fx, self.tok(pos), content(pos, 16), f12)); return 0

        table = {'w_001CE860': ce860, 'w_001CFB50': cfb50, 'w_001281C0': f2i, 'w_001EF9D0': ef9d0,
                 'w_001CCF70': cf70, 'w_001CFA60': cfa60, 'w_001CFBE0': cfbe0,
                 'w_001EFEB0': efeb0,
                 'w_001B0FD0': b0fd0, 'w_001C6380': service('w_001C6380'),
                 'w_001A2370': a2370, 'w_001B17A0': service('w_001B17A0'), 'w_draw_4C': draw,
                 'w_001AFC10': service('w_001AFC10'), 'w_001AFA90': alloc,
                 'w_001FBD50': sound, 'w_00122BB8': rand,
                 'w_0019AA80': probe('w_0019AA80', 1), 'w_0019A570': probe('w_0019A570', 2),
                 'w_0019B6C0': probe('w_0019B6C0', 0), 'w_001EFD90': efd90,
                 'w_001CD520': cd520, 'w_001E2BA0': e2ba0}
        for name in MATH.values(): table[name] = math(name)
        for name, kinds in LEAVES.values(): table[name] = leaf(name, kinds)
        fns = []
        for name, kind in WORKERS:
            f = kind(guard(table[name]))
            self.keep.append(f)
            fns.append(f)
        return Workers(None, *fns)

    def tick(self, node, entry=0x825940):
        fault = Fault()
        lib = CONTEXT['lib']
        fn = {0x825940: lib.em_gun_rest_tick, 0x21AAC0: lib.em_gun_rest_0021AAC0,
              0x21A500: lib.em_gun_rest_0021A500}[entry]
        got = fn(node, C.byref(self.mem), C.byref(self.workers), C.byref(fault))
        if self.error is not None: raise self.error
        return got, fault

    def written(self): return self.stores | self.callee


# ======================================================================
# A run: ticks of one node on both sides, compared
# ======================================================================

def run_pair(image, seeds, answers, ticks, node=GUN, keys=None, between=None,
             entry=0x825940):
    script_o = Script(**answers)
    o = OracleSide(image, seeds, script_o, keys)
    per_tick = []
    for t in range(ticks):
        script_o.tick = t
        if between: between(t, o, None)
        start = len(o.log)
        o.tick(node, entry)
        per_tick.append((start, len(o.log), o.o.load(node + 4, 1)))
        if any(e[0][0] == 'w_001AFC10' for e in o.log[start:]): break
    n = NativeSide(image, seeds, keys, o.log)
    results = []
    for t, (start, end, lifecycle) in enumerate(per_tick):
        if between: between(t, None, n)
        got, fault = n.tick(node, entry)
        assert fault.code == 0, ('native fault', t, hex(fault.address), fault.code)
        assert len(n.log) == end, ('native call count', t, len(n.log), end)
        freed = any(e[0][0] == 'w_001AFC10' for e in o.log[start:end])
        assert got == (0 if freed else 1), ('result', t, got)
        results.append('freed' if freed else lifecycle)
    return o, n, results


def compare_run(image, seeds, answers, ticks, label, node=GUN, between=None,
                entry=0x825940, sink=None):
    """Pass 1 finds every byte either side writes; pass 2 checks those bytes
    at every callee entry and at the end. `sink` (a list) receives the
    oracle's call log and a final-value reader."""
    try:
        o, n, _ = run_pair(image, seeds, answers, ticks, node, None, between, entry)
        keys = sorted(o.written() | n.written())
        o, n, results = run_pair(image, seeds, answers, ticks, node, keys, between, entry)
        for k in keys:
            assert o.value(k) == n.value(k), ('final byte differs', hex(k), o.value(k), n.value(k))
    except AssertionError as error:
        raise AssertionError((label,) + tuple(error.args)) from None
    if sink is not None: sink.extend([o.log, lambda a, size: o.o.load(a, size)])
    return results, o.o.outcomes, o.o.fetched, [e[0][0] for e in o.log]


# ======================================================================
# Cases
# ======================================================================

def w32(image, address): return int.from_bytes(image[address:address + 4], 'little')


def slots(image):
    return [w32(image, GUN + 0x110 + 4 * i) for i in range(4)]


def fill(rng, lifecycle, image, aim=None):
    """A random gun state over the captured node, with the D_00275B40
    slots set as the pool walk sets them (node + 0x110)."""
    bone2, bone3 = slots(image)[2], slots(image)[3]
    ch = rng.choice
    seeds = [(0x275B40, GUN + 0x110, 4), (GUN + 4, lifecycle, 1),
             (GUN + 0x220, CHILD_NODE, 4),
             (GUN + 0x28, ch([0, 1, 2, 3, 4, 0x33, 0x7FFF, 0x8000, 0xFFFB, 100]), 2),
             (GUN + 0x2A, ch([0, 1, 3, 4, 0x12C, 0xFFFF]), 2),
             (GUN + 0x36, ch([0, 0, 1]), 2),
             (GUN + 0x1F4, bits(ch([0.05, -0.05, 0.0, 1.0, -1.0])), 4),
             (GUN + 0x1F8, bits(ch([0.02, 0.3, -0.1])), 4),
             (GUN + 0x1FC, bits(ch([0.0, 3.1, 3.14159, -3.0, 2.95, -0.5])), 4),
             (GUN + 0x200, ch([0, 1, 12, 13, 14, 0xD]), 4),
             (GUN + 0x204, ch([0, PLAYER]), 4),
             (GUN + 0x208, ch([0, 0, -3 & MASK, 1, 2, 0x1E, 0x1F, 0x20, 0x40]), 4),
             (GUN + 0x20C, ch([1, 2, 50, 0]), 4),
             (GUN + 0x210, bits(ch([0.01, -0.01, 0.5, -0.5])), 4),
             (GUN + 0x214, ch([1, 2, 50, 0]), 4),
             (GUN + 0x218, bits(ch([0.02, -0.02, 0.2])), 4),
             (GUN + 0x224, ch([0, 1]), 4),
             (GUN + 0xC4, bits(ch([0.0, -1.5707964, 2.5])), 4),
             (bone2 + 0x74, bits(ch([0.0, 1.1, -1.1, 1.13, -1.13, 1.2, -1.2])), 4),
             (bone3 + 0x78, bits(ch([-1.1, -1.2, -0.6, -0.5, -0.52, -0.9])), 4),
             (0x70003B68, ch([0, 7, 0x40]), 4),
             (PLAYER + 3, ch([0, 0x0F, 0x10, 0x13, 0x14]), 1),
             (PLAYER + 0, ch([1, 3]), 1),
             (FACE + 0x1A, ch([0x5C, 0x5B, 0x5A, 5, 3]), 1),
             (FACE + 0x24, bits(0.0), 4), (FACE + 0x28, bits(1.0), 4), (FACE + 0x2C, bits(0.0), 4)]
    # The aim geometry: bone 2's position and forward row against the player.
    px, py, pz = 250.0, 240.0, 209.0          # the seeded player position below
    kind = aim or ch(['left', 'right', 'ahead', 'ahead-x0', 'ahead-neg', 'random', 'random'])
    bx, bz = px + rng.uniform(-40, 40), pz + rng.uniform(-40, 40)
    if kind == 'ahead-x0': bx = px
    if kind == 'ahead-neg': bx = px + 30
    dx, dz = px - bx, pz - bz
    ln = (dx * dx + dz * dz) ** 0.5 or 1.0
    nx, nz = dx / ln, dz / ln
    fx, fz = {'left': (-nx, -nz), 'right': (nx, nz),
              'random': (rng.uniform(-1, 1), rng.uniform(-1, 1))}.get(kind, (nz, -nx))
    # The player at whole coordinates, so a probe point 100 units away is at
    # exactly the sight's 10000.0 squared-distance limit.
    seeds += [(0x810360, bits(250.0), 4), (0x810364, bits(240.0), 4), (0x810368, bits(209.0), 4)]
    seeds += [(bone2 + 0xC0, bits(bx), 4), (bone2 + 0xC4, bits(py - rng.uniform(-10, 30)), 4),
              (bone2 + 0xC8, bits(bz), 4), (bone2 + 0xB0, bits(fx), 4), (bone2 + 0xB8, bits(fz), 4)]
    return seeds


def probe_block(rng, near=None, kind=None, hit=None):
    ch = rng.choice
    near = ch([True, False, 'edge']) if near is None else near
    point = (250.0 + {True: 5.0, False: 150.0, 'edge': 100.0}[near], 240.0, 209.0, 1.0)
    block = {}
    for base, vec in ((0x700031B0, point), (0x70003190, (1.0, 2.0, 3.0, 1.0)),
                      (0x700031A0, (4.0, 2.5, 3.5, 1.0))):
        for i, v in enumerate(vec): block[base + 4 * i] = (bits(v), 4)
    block[0x700031D0] = (FACE, 4)
    block[0x700031D4] = (ch([PLAYER, CABLE]) if hit is None else hit, 4)
    block[0x700031D8] = (ch([0, 1, 1, 2]) if kind is None else kind, 4)
    return block


def probe_answer(rng, v):
    if isinstance(v, tuple) and v and v[0] == 'probe':
        _, result, kind, hit = v
        return (result, probe_block(rng, True, kind, hit))
    return v


def unit_answers(rng):
    ch = rng.choice
    return dict(w_00122BB8=[rng.randrange(0, 0x80000000) for _ in range(4)],
                w_0019AA80=[(ch([0, 1]), probe_block(rng))],
                w_0019A570=[(ch([0, 1, 1]), probe_block(rng))],
                w_0019B6C0=[(ch([0, 1]), probe_block(rng))],
                w_001AFA90=[ch([0, SHOT_NODE])], w_001EFD90=[ch([0, 0x01D62000])])


def unit_cases():
    out = []
    for i in range(pick(1600, 260)):
        lifecycle = (1, 4)[i % 2]
        idle = i % 4 < 2      # half the cases start outside the alert block
        out.append(('unit', f'lifecycle {lifecycle} #{i}', lifecycle, i,
                    [(GUN + 0x208, 0, 4)] if idle else [], {}))
    # The aim's three heading arms, the snap on both sides of x = 0.
    for k, aim in enumerate(('left', 'right', 'ahead', 'ahead-x0', 'ahead-neg', 'random') * 3):
        out.append(('unit', f'aim {aim} #{k}', 1, 30_000 + k,
                    [('aim', aim, 0), (GUN + 0x208, 0, 4)], {}))
    # The alert gun sway wrapping past pi, in both lifecycles.
    for lifecycle in (1, 4):
        out.append(('unit', f'lifecycle {lifecycle} alert sway wraps', lifecycle, 20_000 + lifecycle,
                    [(GUN + 0x208, 0x40, 4), (GUN + 0x214, 5, 4),
                     (GUN + 0x1FC, bits(3.1), 4), (GUN + 0x218, bits(0.2), 4)], {}))
    # The shot and what it hits: +0x200 reaches 14 with the target in sight.
    n = 0
    for kind, surface, world_hit, hit, byte0 in (
            (1, 3, 0, PLAYER, 1), (1, 3, 0, PLAYER, 3), (1, 3, 0, CABLE, 1),
            (0, 0x5C, 1, PLAYER, 1), (0, 0x5B, 1, PLAYER, 1), (2, 0x5A, 1, PLAYER, 1),
            (0, 3, 1, PLAYER, 1), (0, 5, 0, PLAYER, 1), (2, 3, 0, PLAYER, 1)):
        for alloc in (0, SHOT_NODE):
            seeds = [(GUN + 0x208, 0, 4), (GUN + 0x200, 13, 4),
                     (GUN + 0x204, PLAYER, 4), (PLAYER + 3, 0, 1), (PLAYER + 0, byte0, 1),
                     (FACE + 0x1A, surface, 1)]
            answers = dict(w_0019A570=[('probe', 1, kind, hit)], w_0019B6C0=[('probe', world_hit,
                                                                               0, hit)],
                           w_001AFA90=[alloc])
            out.append(('unit', f'fire kind {kind} surface {surface:#x} world {world_hit} '
                        f'hit {hit:#x} byte0 {byte0} alloc {alloc:#x}', 1, 10_000 + n, seeds,
                        answers))
            n += 1
    return out


def unit_case(case):
    _, label, lifecycle, i, overrides, answer_overrides = case
    image = CONTEXT['base']
    rng = random.Random(0x825940 + i)
    aim = [v for a, v, size in overrides if a == 'aim']
    overrides = [x for x in overrides if x[0] != 'aim']
    seeds = fill(rng, lifecycle, image, aim[0] if aim else None)
    answers = unit_answers(rng)
    replaced = {a for a, _, _ in overrides}
    seeds = [x for x in seeds if x[0] not in replaced] + list(overrides)
    for name, values in answer_overrides.items():
        answers[name] = [probe_answer(rng, v) for v in values]
    results, oc, fe, names = compare_run(image, seeds, answers, 1, label)
    return label, results, oc, fe, names


# Pinned exact-threshold cases of lifecycle 1's aim (review round): each
# comparison of the aim meets its constant with equality, and the case
# proves from the oracle's own log that it did.
K_STEP, K_NEG_STEP = 0x3C3EA2F1, 0xBC3EA2F1
K_YAW_MIN, K_YAW_MAX, K_PITCH_MAX = 0xBF91361E, 0x3F91361E, 0xBF060A92


def _one_ulp_search(target, op):
    """The float bits c near `target` with op(c) == target exactly."""
    import ee_float_model as ef
    base = op[1](target)
    for delta in range(0, 64):
        for c in (base + delta, base - delta):
            if op[0](c) == target: return c
    raise AssertionError(('no exact operand', hex(target)))


def boundary_cases():
    import ee_float_model as ef
    bone2, bone3 = slots(CONTEXT['base'])[2], slots(CONTEXT['base'])[3]
    # The target at (250, 240, 209); bone 2 four units short on x, level on
    # z, so the normalized flat direction is exactly (1, 0, 0, 0) and
    # d = -(1 * bone2 +0xB0) - 0 * bone2 +0xB8 is exactly -(bone2 +0xB0).
    geometry = [(bone2 + 0xC0, bits(246.0), 4), (bone2 + 0xC8, bits(209.0), 4),
                (bone2 + 0xB8, 0, 4), (bone2 + 0x74, 0, 4)]
    rise_to_max = _one_ulp_search(K_PITCH_MAX, (lambda c: ef.ee_add(c, K_STEP),
                                                lambda t: ef.ee_sub(t, K_STEP)))
    fall_to_min = _one_ulp_search(K_YAW_MIN, (lambda c: ef.ee_sub(c, K_STEP),
                                              lambda t: ef.ee_add(t, K_STEP)))
    out = []
    # d == -STEP: not below -STEP, so the snap arm (0x826544), not a turn.
    out.append(('heading d == -STEP', 'd', K_NEG_STEP,
                geometry + [(bone2 + 0xB0, K_STEP, 4), (bone2 + 0xC4, bits(240.0), 4)]))
    # d == +STEP: not above +STEP, so the snap arm, not a turn.
    out.append(('heading d == +STEP', 'd', K_STEP,
                geometry + [(bone2 + 0xB0, K_NEG_STEP, 4), (bone2 + 0xC4, bits(240.0), 4)]))
    # Dead zone with x == 0 (no snap): bone 2 +0x74 meets each yaw clamp
    # exactly and must not be clamped (+0x2A keeps its value).
    for name, value in (('min', K_YAW_MIN), ('max', K_YAW_MAX)):
        out.append((f'bone 2 +0x74 == yaw {name}', 'yaw', value,
                    [(bone2 + 0xC0, bits(250.0), 4), (bone2 + 0xC8, bits(205.0), 4),
                     (bone2 + 0xB0, bits(1.0), 4), (bone2 + 0xB8, 0, 4),
                     (bone2 + 0xC4, bits(240.0), 4), (bone2 + 0x74, value, 4)]))
    # The pitch step lands exactly on each pitch clamp: level target (pitch
    # 0) rises onto -0.5236; a steep drop (dy/len = -5) falls onto -1.1344.
    out.append(('bone 3 +0x78 rises onto pitch max', 'pitch', (rise_to_max, K_PITCH_MAX),
                geometry + [(bone2 + 0xB0, bits(0.5), 4), (bone2 + 0xC4, bits(240.0), 4),
                            (bone3 + 0x78, rise_to_max, 4)]))
    out.append(('bone 3 +0x78 falls onto pitch min', 'pitch', (fall_to_min, K_YAW_MIN),
                geometry + [(bone2 + 0xB0, bits(0.5), 4), (bone2 + 0xC4, bits(260.0), 4),
                            (bone3 + 0x78, fall_to_min, 4)]))
    return out


def boundary_case(case):
    import ee_float_model as ef
    label, kind, want, overrides = case
    image = CONTEXT['base']
    rng = random.Random(0x826544)
    seeds = fill(rng, 1, image, 'ahead')
    overrides = overrides + [(GUN + 0x208, 0, 4), (GUN + 0x204, PLAYER, 4),
                             (GUN + 0x2A, 0x12C, 2), (GUN + 0x200, 0, 4)]
    replaced = {a for a, _, _ in overrides}
    seeds = [x for x in seeds if x[0] not in replaced] + overrides
    answers = unit_answers(rng)
    # Both sight probes miss, so the sight cannot reset +0x2A to 300 and hide
    # a clamp's cost of four: +0x2A ends at 300 - 1 without a clamp.
    answers.update(w_0019AA80=[(0, {})], w_0019A570=[(0, {})])
    sink = []
    results, oc, fe, names = compare_run(image, seeds, answers, 1, label, sink=sink)
    log, load = sink
    bone2 = slots(image)[2]
    norm = [e for e in log if e[0][0] == 'w_00102760'][0]
    v = [int.from_bytes(norm[1][0][0][i:i + 4], 'little') for i in range(0, 16, 4)]
    fx = int.from_bytes(bytes(load(bone2 + 0xB0 + i, 1) for i in range(4)), 'little')
    fz = int.from_bytes(bytes(load(bone2 + 0xB8 + i, 1) for i in range(4)), 'little')
    d = ef.ee_madd(ef.ee_mul(v[0], ef.ee_neg(fx)), v[2], ef.ee_neg(fz))
    x_abs = [e for e in log if e[0][0] == 'w_0011DF78']
    if kind == 'd':
        assert d == want, (label, 'heading value not at the threshold', hex(d))
        assert x_abs, (label, 'the snap arm (0011DF78) did not run')
    elif kind == 'yaw':
        assert d == 0 or d == 0x80000000, (label, 'not in the dead zone', hex(d))
        assert x_abs and not ef.ee_c_lt(0x3A83126F, x_abs[0][1] & 0x7FFFFFFF), (label, 'snapped')
        assert load(bone2 + 0x74, 4) == want, (label, 'yaw moved')
        assert load(GUN + 0x2A, 2) == 0x12C - 1, (label, '+0x2A', load(GUN + 0x2A, 2))
    else:
        start, landed = want
        pitch = [e for e in log if e[0][0] == 'w_0011DBB8'][-1][1]
        lo = ef.ee_sub(start, K_STEP)
        rising = not ef.ee_c_le(pitch, lo)
        assert rising == (landed == K_PITCH_MAX), (label, 'wrong pitch arm', hex(pitch))
        if not rising:
            assert ef.ee_c_lt(pitch, ef.ee_add(K_STEP, start)), (label, 'dead arm', hex(pitch))
        assert load(slots(image)[3] + 0x78, 4) == landed, (label, 'pitch did not land exactly')
        assert load(GUN + 0x2A, 2) == 0x12C - 1, (label, '+0x2A', load(GUN + 0x2A, 2))
    return label, results, oc, fe, names


# Multi-tick lockstep runs.
def seen_script(first_seen, lose_at=10 ** 9, near=True):
    """Probes that see nothing until `first_seen`, then the player (kind 1,
    player byte +3 0x10), until `lose_at`."""
    def aa80(n, tick):
        rng = random.Random(n)
        return (0, probe_block(rng, near, 1, PLAYER))

    def a570(n, tick):
        rng = random.Random(1000 + n)
        seen = first_seen <= tick < lose_at
        return (1 if seen else 0, probe_block(rng, near, 1 if seen else 0, PLAYER))
    return dict(w_0019AA80=aa80, w_0019A570=a570,
                w_0019B6C0=lambda n, t: (n & 1, probe_block(random.Random(n), True, 0, PLAYER)),
                w_00122BB8=lambda n, t: (0x2468ACE1 * (n + 3)) & 0x7FFFFFFF,
                w_001AFA90=lambda n, t: SHOT_NODE if n % 3 else 0,
                w_001EFD90=lambda n, t: 0)


def multi_cases():
    base = CONTEXT['base']
    rng = random.Random(7)
    common = [s for s in fill(rng, 4, base) if s[0] not in
              (GUN + 0x28, GUN + 0x2A, GUN + 0x36, GUN + 0x200,
               GUN + 0x204, GUN + 0x208, PLAYER + 3, PLAYER + 0)]
    bone2, bone3 = slots(base)[2], slots(base)[3]
    common = [s for s in common if s[0] not in (bone2 + 0x74, bone3 + 0x78)]
    common += [(PLAYER + 3, 0, 1), (PLAYER + 0, 1, 1), (GUN + 0x36, 0, 2),
               (bone2 + 0x74, bits(0.3), 4), (bone3 + 0x78, bits(-0.8), 4)]
    runs = [
        ('patrol, sees the player, aims and fires', 4,
         [(GUN + 0x28, 10, 2), (GUN + 0x2A, 0, 2), (GUN + 0x200, 1, 4),
          (GUN + 0x204, 0, 4), (GUN + 0x208, 0, 4)], seen_script(4), 90),
        ('aim, loses the player, back to patrol', 1,
         [(GUN + 0x28, 3, 2), (GUN + 0x2A, 9, 2), (GUN + 0x200, 0, 4),
          (GUN + 0x204, PLAYER, 4), (GUN + 0x208, 0, 4)], seen_script(10 ** 9), 14),
        ('alert countdown through the ramp', 4,
         [(GUN + 0x28, 10, 2), (GUN + 0x2A, 0, 2), (GUN + 0x200, 1, 4),
          (GUN + 0x204, 0, 4), (GUN + 0x208, 0x26, 4)], seen_script(10 ** 9), 42),
    ]
    return [('multi', label, lc, common + extra, script, ticks)
            for label, lc, extra, script, ticks in runs]


def multi_case(index):
    _, label, lifecycle, seeds, answers, ticks = multi_cases()[index]
    image = CONTEXT['base']
    seeds = [s for s in seeds if s[0] != GUN + 4] + [(GUN + 4, lifecycle, 1)]
    results, oc, fe, names = compare_run(image, seeds, answers, ticks, label)
    return label, results, oc, fe, names


# The delegated lifecycles over the adapter.
def delegate_cases():
    out = []
    for name in CONTEXT['images']:
        out.append(('capture', name))
    for lifecycle in (0, 2, 3, 0x64, 0x63, 0xFF):
        for flag in (0, 1, 0xFF):
            out.append(('delegate', lifecycle, flag, 0, CHILD_NODE, -1.5, 3, 1))
    for fd0 in (0, 1):
        for child in (0, CHILD_NODE):
            out.append(('delegate', 0, 0, fd0, child, 0.0, 0, 0))
    for angle in (-1.0, -1.5, -2.0, 'exact'):
        for w21c in (0, 3):
            for w224 in (0, 1):
                out.append(('delegate', 2, 0xFF, 0, CHILD_NODE, angle, w21c, w224))
    return out


def delegate_case(case):
    if case[0] == 'capture':
        image = CONTEXT['images'][case[1]]
        assert w32(image, GUN + 0x10) == 0x825940, case
        results, oc, fe, names = compare_run(image, [], {}, 1, f'capture {case[1]}')
        return case[1], results, oc, fe, names
    _, lifecycle, flag, fd0, child, angle, w21c, w224 = case
    image = CONTEXT['base']
    seeds = [(0x275B40, GUN + 0x110, 4), (GUN + 4, lifecycle, 1),
             (0x810788, flag, 1),
             (GUN + 0x1FC, 0xBFC90FDB if angle == 'exact' else bits(angle), 4),
             (GUN + 0x1F8, bits(0.25), 4),
             (GUN + 0x21C, w21c, 4), (GUN + 0x224, w224, 4),
             (GUN + 0x220, CHILD_NODE, 4)]
    answers = dict(w_001B0FD0=fd0, w_00122BB8=0x794BDF32, w_001AFA90=child)
    label = (f'delegate lifecycle {lifecycle:#x} flag {flag:#x} fd0 {fd0} child {child:#x} '
             f'angle {angle} n {w21c} w224 {w224}')
    results, oc, fe, names = compare_run(image, seeds, answers, 3, label)
    return label, results, oc, fe, names


# The cable's hit effect node 0021AAC0.
def fx_cases():
    out = []
    for st in (0, 1, 2, 3, 5):
        counts = (0x1D, 0x1E, 0x1F, 0x24, 0x25, 0x2A, 0x32, 0x3A, 0x3B, 0x3C, 0x77, 0x78, 0x79) \
            if st == 1 else (0,)
        for count in counts:
            for points in ((0, 1, 5, 6) if st == 1 else (0,)):
                for child in ((0, FX_CHILD) if st == 1 and count in (0x1E, 0x32) else (FX_CHILD,)):
                    out.append(('fx', st, count, points, child, 1))
    out.append(('fx', 0, 0, 0, FX_CHILD, pick(125, 125)))   # spawn to free, in lockstep
    return out


def fx_case(case):
    _, st, count, points, child, ticks = case
    image = CONTEXT['base']
    rng = random.Random(st * 1000 + count * 10 + points)
    e = FX_NODE + 0x1F0
    seeds = [(FX_NODE + 4, st, 1), (FX_NODE + 0x24, CABLE, 4), (e + 0x98, count, 4),
             (e + 0x94, points, 4), (e + 0x90, bits(-8.0 * points), 4)]
    for i in range(6):
        seeds += [(e + 16 * i + j * 4, bits(rng.uniform(-50, 300)), 4) for j in range(3)]
        seeds += [(e + 0x60 + 4 * i, bits(rng.uniform(0, 1)), 4),
                  (e + 0x78 + 4 * i, bits(rng.uniform(0, 1)), 4)]
    for j in range(4): seeds.append((FX_NODE + 0xB0 + 4 * j, bits(rng.uniform(-9, 9)), 4))
    answers = dict(w_00122BB8=lambda n, t: (0x41C64E6D * (n + 1) + 0x3039) & 0x7FFFFFFF,
                   w_001CCF70=lambda n, t: n * 3 + 1,
                   w_001CFA60=lambda n, t: bytes((n * 7 + i) & 255 for i in range(0x60)),
                   w_001EFEB0=[child] * 16)
    label = f'0021AAC0 st {st} count {count:#x} points {points} child {child:#x} x{ticks}'
    results, oc, fe, names = compare_run(image, seeds, answers, ticks, label, FX_NODE,
                                         entry=0x21AAC0)
    return label, results, oc, fe, names


# The strip nodes 0021A500 (spawned by 0021AAC0 with +5 0, n 12, 48.0, 0.5).
def strip_cases():
    out = []
    for st in (0, 1, 2, 3, 7):
        for flag5 in ((0, 1, 2) if st in (0, 1) else (0,)):
            for n in ((0, 1, 2, 3, 12) if st == 0 else (12,)):
                for grow, spent in (((0.0, 0.0), (0.99, 1.05), (1.0, 1.08), (0.999, 1.1))
                                    if st == 1 else ((0.0, 0.0),)):
                    out.append(('strip', st, flag5, n, grow, spent, 1))
    out.append(('strip', 0, 0, 12, 0.0, 0.0, 80))   # as 0021AAC0 spawns it, to its end
    out.append(('strip', 0, 1, 12, 0.0, 0.0, 40))
    return out


def strip_case(case):
    _, st, flag5, n, grow, spent, ticks = case
    image = CONTEXT['base']
    rng = random.Random(n * 100 + st * 10 + flag5)
    e = FX_CHILD + 0x1F0
    seeds = [(FX_CHILD + 4, st, 1), (FX_CHILD + 5, flag5, 1), (e, n, 4),
             (e + 4, bits(48.0), 4), (e + 8, bits(0.5), 4), (e + 0xC, rng.randrange(1 << 32), 4),
             (e + 0x10, bits(grow), 4), (e + 0x14, bits(spent), 4)]
    for i in range(24): seeds.append((e + 0x18 + 4 * i, bits(rng.uniform(-2.5, 2.5)), 4))
    for i in range(16): seeds.append((FX_CHILD + 0xD0 + 4 * i, bits(rng.uniform(-2, 2)), 4))
    answers = dict(w_00122BB8=lambda k, t: (0x2545F491 * (k + 7)) & 0x7FFFFFFF,
                   w_001CCF70=lambda k, t: k + 5,
                   w_001CFB50=lambda k, t: bytes((k * 11 + i * 3) & 255 for i in range(0x60)))
    label = f'0021A500 st {st} +5 {flag5} n {n} grow {grow} spent {spent} x{ticks}'
    results, oc, fe, names = compare_run(image, seeds, answers, ticks, label, FX_CHILD,
                                         entry=0x21A500)
    return label, results, oc, fe, names


def check_001EFEB0():
    """001EFEB0(id, 0x700036A0) as 0021AAC0 calls it, 001EF9D0 answering 0 or
    a node; the original runs from the ELF, the native gets the same answer."""
    image = CONTEXT['base']
    outcomes, count = set(), 0
    for node in (0, FX_CHILD):
        m = [bits(random.Random(node + i).uniform(-9, 9)) for i in range(16)]
        seeds = [(0x700036A0 + 4 * i, v, 4) for i, v in enumerate(m)]
        o = OracleSide(image, seeds, Script(w_001EF9D0=node), None)
        o.node = None
        del o.o.hooks[0x1EFEB0]          # the function under test runs as original code
        sdf.run_original(o.o, 0x1EFEB0, (0x8000003B, 0x700036A0))
        n = NativeSide(image, seeds, None, o.log)
        out, fault = C.c_uint32(), Fault()
        got = CONTEXT['lib'].em_gun_rest_001EFEB0(0x8000003B, n.sbase + 0x36A0, C.byref(n.mem),
                                                  C.byref(n.workers), C.byref(out), C.byref(fault))
        if n.error is not None: raise n.error
        assert got == 0 and fault.code == 0 and out.value == (o.o.r[2] & MASK), ('001EFEB0', node)
        assert len(n.log) == len(o.log), ('001EFEB0 calls', node)
        for k in o.written() | n.written():
            assert o.value(k) == n.value(k), ('001EFEB0 byte', node, hex(k))
        outcomes |= o.o.outcomes
        count += 1
    return count, outcomes


# ======================================================================
# Native fail-stop
# ======================================================================

def check_fail_stop():
    """A NULL worker, a failing worker and an unmapped address fault at the
    original address; a latched fault stops every later call."""
    image = CONTEXT['base']
    rng = random.Random(3)
    seeds = [x for x in fill(rng, 4, image) if x[0] != GUN + 0x208] + [(GUN + 0x208, 0x40, 4)]
    lib = CONTEXT['lib']
    checks = 0

    def run(mutate, want_address, want_code):
        nonlocal checks
        n = NativeSide(image, seeds, None, [])
        n.olog = None
        mutate(n)
        fault = Fault()
        got = lib.em_gun_rest_tick(GUN, C.byref(n.mem), C.byref(n.workers),
                                            C.byref(fault))
        assert got == -1 and (fault.address, fault.code) == (want_address, want_code), (
            'fail-stop', hex(fault.address), fault.code, hex(want_address), want_code)
        calls = len(n.log)
        assert lib.em_gun_rest_tick(GUN, C.byref(n.mem), C.byref(n.workers),
                                             C.byref(fault)) == -1 and len(n.log) == calls
        checks += 2

    def quiet(n):                       # every worker answers 0 without an oracle log
        n.expect = lambda entry: 0 if entry[0] not in ('w_001CFA60', 'w_001CFB50') else bytes(0x60)

    def null_worker(n):
        quiet(n); n.workers.w_001C6380 = dict(WORKERS)['w_001C6380']()

    def failing(n):
        quiet(n); f = dict(WORKERS)['w_001B17A0'](lambda *a: -1); n.keep.append(f)
        n.workers.w_001B17A0 = f

    def unmapped(n):
        quiet(n)
        base = n.load

        def load(ctx, a, size):
            return None if a == 0x00275B40 else base(ctx, a, size)
        n.mem.load = MEMFN(load); n.keep.append(n.mem.load)
    run(null_worker, 0x1C6380, 1)
    run(failing, 0x1B17A0, 2)
    run(unmapped, 0x275B40, 1)
    return checks


# ======================================================================
# Captures: the lifecycle-0 rand draw and its results
# ======================================================================

def check_rand_capture():
    rows = [json.loads(line) for line in RAND_CAPTURE.open()]
    draws = [r for r in rows if r.get('ra') == '8259f0']
    assert len(draws) == 1, ('the gun draws once in the New Game capture', len(draws))
    value = int(draws[0]['r'], 16)
    t = value >> 16
    timer = 300 + ((300 * t) >> 15)
    # Its place: the first world frame after the area entry, before 0x8235F0's draw.
    frame = draws[0]['f']
    same = [r for r in rows if r['f'] == frame and 'ra' in r and 'mark' not in r]
    assert [r['ra'] for r in same][:5] == ['1d7d44', '1d7dd4', '1f112c', '8259f0', '8236b4'], same[:5]
    for name, image in CONTEXT['images'].items():
        assert int.from_bytes(image[GUN + 0x28:GUN + 0x2A], 'little') == timer, name
    # The native setup over that draw reproduces the captured words.
    image = CONTEXT['base']
    bone3 = slots(image)[3]
    seeds = [(0x275B40, GUN + 0x110, 4), (GUN + 4, 0, 1), (0x810788, 0, 1),
             (GUN + 0x28, 0, 2), (bone3 + 0x78, 0x12345678, 4)]
    for off in range(0xA0, 0xB0, 4): seeds.append((CHILD_NODE + off, 0x55555555, 4))
    answers = dict(w_001B0FD0=0, w_00122BB8=value, w_001AFA90=CHILD_NODE)
    o, n, _ = run_pair(image, seeds, answers, 1)
    for k, want in ((GUN + 0x28, timer), (bone3 + 0x78, w32(image, bone3 + 0x78))):
        size = 2 if k == GUN + 0x28 else 4
        got = sum(n.value(k + i) << (8 * i) for i in range(size))
        assert got == want, ('capture word', hex(k), hex(got), hex(want))
    for off in range(0xA0, 0xB0, 4):
        got = sum(n.value(CHILD_NODE + off + i) << (8 * i) for i in range(4))
        assert got == w32(image, CHILD_NODE + off), ('child +0xA0 quad', off)
    return value, timer


# ======================================================================
# The fan's player hit and its consumer 0021C440
# ======================================================================

FAN_R2 = 0x7A7690          # fan record [2] (+0x2E == 1), the one with the box


def check_fan_hit_chain():
    """The original fan 0x827630 hits the player (fast arm, player in the
    band); the player record it leaves goes through tools/
    test_player_stage_workers_reference.run_case('reaction'), which executes
    the original 0021C440 and compares the native em_player_stage_reaction
    field for field and call for call."""
    import test_player_stage_workers_reference as psw
    psw.LANE = OUT / 'stage_workers'          # this lane's private build
    psw.ELF = CONTEXT['elf']
    lib = psw.build_native(name='gun_rest_stage_workers')
    psw.check_clip_rates(CONTEXT['elf'], lib, [])   # loads the clip-rate table (psw.RATES)
    outcomes = []
    names = [n for n in CONTEXT['images'] if pick(True, n in ('playable',) or '14_roger' in n)]
    for name in names:
        image = CONTEXT['images'][name]
        assert w32(image, FAN_R2 + 0x10) == 0x827630 and image[FAN_R2 + 0x2E] == 1, name
        for health, z in ((100.0, 160.0), (4.0, 166.0), (5.0, 158.0)):
            o = sdf.Oracle(image)
            for address, value, size in (
                    (FAN_R2 + 4, 1, 1), (FAN_R2 + 5, 2, 1), (FAN_R2 + 0x28, 30, 2),
                    (FAN_R2 + 0x38, bits(0.2), 4), (0x8106B8, 0, 1), (PLAYER + 0, 1, 1),
                    (PLAYER + 0x0F, 0, 1), (PLAYER + 0x220, bits(health), 4),
                    (PLAYER + 0x224, 0, 4), (PLAYER + 0xA0, bits(330.0), 4),
                    (PLAYER + 0xA4, bits(300.0), 4), (PLAYER + 0xA8, bits(z), 4)):
                o.seed(address, value, size)
            log = []
            for address in (0x1C6380, 0x1B17A0, 0x1CAA00, 0x1FBD50, 0x1B0C60, 0x1AFC10):
                sdf.hook(o, address, lambda e, a=address: log.append(a))
            sdf.run_original(o, 0x827630, (FAN_R2,))
            hit = [o.load(PLAYER + k, 1) for k in (0, 0xF)] + [o.load(PLAYER + 0x224)] + \
                [o.load(PLAYER + 0x70 + 4 * i) for i in range(4)]
            assert hit == [3, 6, 0x40A00000, 0, 0, 0x3F800000, 0x3F800000], (name, 'fan hit', hit)
            assert 0x1B0C60 not in log, (name, 'the band past Z 156 is the hit, not the exit')
            # The consumer, over the RAM the fan left.
            ram = bytearray(image)
            for address in sorted(o.over):
                if address < len(ram): ram[address] = o.over[address]
            ram = bytes(ram)
            raw = bytearray(ram[PLAYER:PLAYER + 0x320])
            scene = dict(area=ram[psw.D_AREA], d8106F1=ram[psw.D_6F1], spad3B8F=0)
            glob = dict(d8106C8=s32(struct.unpack_from('<I', ram, psw.D_6C8)[0]),
                        d810701=ram[psw.D_701], d810770=ram[psw.D_770], d81083C=ram[psw.D_83C],
                        d810707=ram[psw.D_707], d810C7E=ram[psw.D_C7E])
            case = psw.Case(raw, ram=ram, base=PLAYER, scene=scene, globals_=glob)
            case.label = (name, 'fan hit', health)
            case.step, case.blend = 0, psw.F(8.0)
            psw.run_case(CONTEXT['elf'], lib, 'reaction', case)
            oc = case.oracle(CONTEXT['elf'])
            oc.run(psw.ROUTINES['reaction'][0], (PLAYER,), ())
            record = case.oracle_state(oc)[0]
            outcomes.append((name, health, record[4], record[5], record[0xF],
                             struct.unpack_from('<f', bytes(record[0x220:0x224]))[0]))
    return outcomes


# ======================================================================
# 001B1190
# ======================================================================

def check_001B1190():
    image = CONTEXT['base']
    count = 0
    for area in (0, 0xB, 0xFF):
        for a0 in (0, 1, 0x1F, 0x20, 0x50, 0xFF, 0x100, 0x150, -1, 0x7FFFFFE1):
            seeds = [(0x810700, area, 1)]
            o = sdf.Oracle(image)
            for a, v, s in seeds: o.seed(a, v, s)
            sdf.run_original(o, 0x1B1190, (a0,))
            n = NativeSide(image, seeds, None, [])
            fault = Fault()
            assert CONTEXT['lib'].em_gun_rest_001B1190(a0, C.byref(n.mem), C.byref(fault)) == 0
            assert fault.code == 0
            keys = set(o.written) | n.stores
            for k in keys:
                assert o.load(k, 1) == n.value(k), ('001B1190', area, a0, hex(k))
            count += 1
    return count


# ======================================================================
# Main
# ======================================================================

# Conditional branches that cannot go both ways, with the reason.
BRANCH_EXCEPTIONS = {
    # Aim pitch (runtime 0x8267CC): reached only with the pitch <= bone 3
    # +0x78 - STEP; its store-the-pitch arm needs the pitch not below +0x78 +
    # STEP as well, i.e. STEP absorbed by rounding (|+0x78| >= 2**20 * STEP)
    # with the pitch equal to it, but the pitch is an atanf result in
    # [-pi/2, pi/2].
    0x8267CC: 'pitch store arm needs STEP absorbed by rounding at |pitch| <= pi/2',
}


def branch_report(outcomes):
    missing = []
    base = CONTEXT['base']
    for name, (start, end) in RANGES.items():
        for pc in range(start, end, 4):
            word = int.from_bytes(base[pc:pc + 4], 'little')
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            conditional = (op in (4, 5, 20, 21) and not (op == 4 and rs == rt)) or \
                op in (6, 7, 22, 23) or op == 1 or (op == 17 and rs == 8)
            if not conditional: continue
            seen = {taken for p, taken in outcomes if p == pc}
            if seen != {True, False} and pc not in BRANCH_EXCEPTIONS:
                missing.append((name, hex(pc), sorted(seen)))
    return missing


def main():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    import hashlib
    assert hashlib.sha256(elf).hexdigest() == sdf.ELF_SHA, 'unexpected boot ELF'
    sdf.CONTEXT['elf'] = elf
    CONTEXT['elf'] = elf
    images = sdf.load_images()
    images.pop('opening', None)
    data = sdf.AREA11_BIN.read_bytes()
    # Route beat 15's snapshot is the AREA01 arrival (another overlay).
    images = {k: v for k, v in images.items()
              if v[0x825940:0x825980] == data[0x825940 - sdf.ARENA:0x825980 - sdf.ARENA]}
    CONTEXT['images'] = images
    CONTEXT['base'] = images['playable']
    sdf.CONTEXT['images'], sdf.CONTEXT['base'] = images, images['playable']
    data = sdf.AREA11_BIN.read_bytes()
    for name, (start, end) in RANGES.items():
        want = data[start - sdf.ARENA:end - sdf.ARENA] if start >= sdf.ARENA else \
            elf[start - 0x100000 + 0x300:end - 0x100000 + 0x300]
        for image_name, image in images.items():
            assert image[start:end] == want, ('captured code differs from the user\'s file', name,
                                              image_name)
    CONTEXT['lib'] = build()

    outcomes, fetched, calls = set(), set(), {}
    counts = {}

    def gather(results):
        for label, res, oc, fe, names in results:
            outcomes.update(oc); fetched.update(fe)
            for nm in names: calls[nm] = calls.get(nm, 0) + 1
    units = unit_cases()
    gather(parallel_map(unit_case, units))
    counts['unit ticks'] = len(units)
    pinned = boundary_cases()
    gather(parallel_map(boundary_case, pinned))
    counts['pinned threshold cases'] = len(pinned)
    multi = multi_cases()
    mres = parallel_map(multi_case, list(range(len(multi))))
    gather(mres)
    counts['lockstep ticks'] = sum(len(r[1]) for r in mres)
    deleg = delegate_cases()
    gather(parallel_map(delegate_case, deleg))
    counts['delegated/capture runs'] = len(deleg)
    fx = fx_cases()
    fres = parallel_map(fx_case, fx)
    gather(fres)
    counts['0021AAC0 runs'] = len(fx)
    print('  0021AAC0 spawn-to-free:', fres[-1][1][:3], '...', fres[-1][1][-3:],
          f'({len(fres[-1][1])} ticks)')
    strips = strip_cases()
    sres = parallel_map(strip_case, strips)
    gather(sres)
    counts['0021A500 runs'] = len(strips)
    print('  0021A500 as spawned, to its end:', sres[-2][1][-3:], f'({len(sres[-2][1])} ticks)')
    n_eb0, oc_eb0 = check_001EFEB0()
    outcomes |= oc_eb0
    counts['001EFEB0 cases'] = n_eb0
    value, timer = check_rand_capture()
    counts['001B1190 cases'] = check_001B1190()
    counts['native fail-stop checks'] = check_fail_stop()
    chain = check_fan_hit_chain()
    counts['fan hit -> 0021C440 chains'] = len(chain)
    for name, health, s4, s5, code, left in chain[:3]:
        print(f'  fan hit ({name}, health {health}): 0021C440 leaves +4 {s4} +5 {s5:#x} '
              f'+F {code:#x} health {left}')
    missing = branch_report(outcomes)
    assert not missing, ('conditional branches not observed both ways', missing)
    for label, res, *_ in mres:
        print(f'  lockstep "{label}": lifecycles {res}')
    print('callees exercised:', ', '.join(f'{k} {v}' for k, v in sorted(calls.items())))
    print(f'rand capture: 0x8259F0 drew {value:#010x} -> +0x28 = {timer} in every image')
    print(', '.join(f'{k} {v}' for k, v in counts.items()))
    print('branch coverage: every conditional branch of 0x825940, 0x826F30, 0x827400, '
          '0021AAC0, 0021A500 and 001EFEB0 observed both ways')
    banner(f'{len(units)} unit ticks', f'{len(multi)} lockstep runs', f'{len(deleg)} delegated runs',
           f'{len(images)} RAM images')
    print('security gun rest translations match the original instructions')


if __name__ == '__main__':
    main()
