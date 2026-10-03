#!/usr/bin/env python3
"""The AREA11 opening controller 0x823E80: native vs the original instructions.

Executes the original function from the user's AREA11 overlay (loaded at its
arena 0x823500, the code checked against the opening capture) with every
callee hooked as a recorded call (001B0FD0, 001C6380, 001BA1C0, 001BA1A0,
001FABB0, 001BA1F0, 001C4760, 001FAE70, 001AEE10, 001B1B70, the +0x4C
method and 001AFC10), over the record bytes +0x00 / +0x04 / +0x05 / +0x2E,
the callee results (001B0FD0's refusal, 001BA1C0's flag, 001BA1F0's script
result) and every +0x04 value, and compares src/game/em_area11_opening.c:
the calls in order with their arguments, the record bytes afterwards and
D_00810811. Ground truth: the decomp's byte-identical C
(src/overlays/AREA11/func_overlay_AREA11_00823E40.c).
Only addresses and values are printed; no original bytes are embedded.
"""
import ctypes as C
import hashlib
import itertools
from pathlib import Path
import struct
import subprocess
import sys

from test_pickup_owner_reference import OwnerOracle
import reference_mode

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
ARENA, SIZE = 0x823500, 0x7800
ENTRY, LENGTH = 0x823E80, 0x168
NODE = 0x7A8E10           # the controller's record in the captures
METHOD = 0x990000         # the record's +0x4C (a hooked address)
D811 = 0x810811
CALLEES = {0x1B0FD0: '001B0FD0', 0x1C6380: '001C6380', 0x1BA1C0: '001BA1C0', 0x1BA1A0: '001BA1A0',
           0x1FABB0: '001FABB0', 0x1BA1F0: '001BA1F0', 0x1C4760: '001C4760', 0x1FAE70: '001FAE70',
           0x1AEE10: '001AEE10', 0x1B1B70: '001B1B70', METHOD: '+0x4C', 0x1AFC10: '001AFC10'}
ARGS = {'001B0FD0': 1, '001C6380': 1, '001BA1C0': 2, '001BA1A0': 2, '001FABB0': 0, '001BA1F0': 1,
        '001C4760': 2, '001FAE70': 1, '001AEE10': 2, '001B1B70': 1, '+0x4C': 1, '001AFC10': 1}

SHIM = r'''
#include "game/em_area11_opening.h"
#include <stdint.h>
typedef struct { uint32_t name, a0, a1; } Ev;
static Ev ev[32]; static int nev;
static int32_t g_refused, g_flag, g_result;
static uint8_t g_811;
#define SELF 0x7A8E10u
static void put(uint32_t n, uint32_t a0, uint32_t a1) { if (nev < 32) { ev[nev].name = n; ev[nev].a0 = a0; ev[nev].a1 = a1; } ++nev; }
static int w_1BA1C0(void *c, uint32_t a1, int32_t *r) { (void)c; put(0x1BA1C0, SELF, a1); *r = g_flag; return 0; }
static int w_1BA1A0(void *c, uint32_t e) { (void)c; put(0x1BA1A0, SELF + 0x1F0, e); return 0; }
static int w_1BA1F0(void *c, int32_t *r) { (void)c; put(0x1BA1F0, SELF, 0); *r = g_result; return 0; }
static int w_1FABB0(void *c) { (void)c; put(0x1FABB0, 0, 0); return 0; }
static int s_811(void *c, uint8_t v) { (void)c; g_811 = v; return 0; }
static int w_1C4760(void *c, int32_t a0, int32_t a1) { (void)c; put(0x1C4760, (uint32_t)a0, (uint32_t)a1); return 0; }
static int w_1FAE70(void *c, int32_t a0) { (void)c; put(0x1FAE70, (uint32_t)a0, 0); return 0; }
static int w_1AEE10(void *c, int16_t a0, uint8_t a1) { (void)c; put(0x1AEE10, (uint32_t)(int32_t)a0, a1); return 0; }
static int w_1B0FD0(void *c, int32_t *r) { (void)c; put(0x1B0FD0, SELF, 0); *r = g_refused; return 0; }
static int w_1C6380(void *c) { (void)c; put(0x1C6380, SELF, 0); return 0; }
static int w_1B1B70(void *c) { (void)c; put(0x1B1B70, SELF, 0); return 0; }
static int m_4C(void *c) { (void)c; put(0x990000, SELF, 0); return 0; }
static int w_1AFC10(void *c) { (void)c; put(0x1AFC10, SELF, 0); return 0; }
int opening_shim(uint8_t *b00, uint8_t *b04, uint8_t *b05, uint16_t *h2E, uint8_t *d811,
                 int32_t refused, int32_t flag, int32_t result, uint32_t *out, int *count, uint32_t *fault)
{
    EmArea11Opening o = { *b00, *b04, *b05, *h2E };
    const EmArea11OpeningWorkers w = { 0, w_1BA1C0, w_1BA1A0, w_1BA1F0, w_1FABB0, s_811, w_1C4760,
                                       w_1FAE70, w_1AEE10, w_1B0FD0, w_1C6380, w_1B1B70, m_4C, w_1AFC10 };
    nev = 0; g_refused = refused; g_flag = flag; g_result = result; g_811 = *d811;
    int rc = em_area11_opening_tick(&o, &w, fault);
    *b00 = o.b00; *b04 = o.b04; *b05 = o.b05; *h2E = o.h2E; *d811 = g_811;
    for (int i = 0; i < nev && i < 32; ++i) { out[3*i] = ev[i].name; out[3*i+1] = ev[i].a0; out[3*i+2] = ev[i].a1; }
    *count = nev;
    return rc;
}
'''


def build_native():
    out = ROOT / 'build/area11_opening_reference'
    out.mkdir(parents=True, exist_ok=True)
    source = out / 'shim.c'
    source.write_text(SHIM)
    lib = out / ('shim.dylib' if sys.platform == 'darwin' else 'shim.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC', '-Isrc',
                    str(source), 'src/game/em_area11_opening.c', '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    u8p, u16p = C.POINTER(C.c_ubyte), C.POINTER(C.c_uint16)
    native.opening_shim.argtypes = [u8p, u8p, u8p, u16p, u8p, C.c_int32, C.c_int32, C.c_int32,
                                    C.POINTER(C.c_uint32), C.POINTER(C.c_int), C.POINTER(C.c_uint32)]
    return native


def original_case(elf, overlay, b00, b04, b05, h2E, refused, flag, result):
    o = OwnerOracle(elf)
    o.write(ARENA, overlay)
    o.save(NODE, b00, 1)
    o.save(NODE + 4, b04, 1)
    o.save(NODE + 5, b05, 1)
    o.save(NODE + 0x2E, h2E, 2)
    o.save(NODE + 0x14, NODE)
    o.save(NODE + 0x4C, METHOD)
    o.save(D811, 0x5A, 1)
    events = []

    def hook(address):
        name = CALLEES[address]

        def call(vm):
            args = [vm.r[4] & 0xFFFFFFFF, vm.r[5] & 0xFFFFFFFF][:ARGS[name]]
            if name == '001AEE10':
                args = [args[0] & 0xFFFF if args[0] & 0xFFFF < 0x8000 else (args[0] & 0xFFFF) - 0x10000, args[1] & 0xFF]
                args[0] &= 0xFFFFFFFF
            events.append((address, *args, *([0] * (2 - len(args)))))
            vm.r[2] = {'001B0FD0': refused, '001BA1C0': flag, '001BA1F0': result}.get(name, 0) & 0xFFFFFFFF
        return call

    for address in CALLEES:
        o.calls[address] = hook(address)
    o.run(ENTRY, [NODE])
    record = (o.load(NODE, 1), o.load(NODE + 4, 1), o.load(NODE + 5, 1), o.load(NODE + 0x2E, 2), o.load(D811, 1))
    return events, record


def native_case(native, b00, b04, b05, h2E, refused, flag, result):
    nb00, nb04, nb05, nh2E, n811 = C.c_ubyte(b00), C.c_ubyte(b04), C.c_ubyte(b05), C.c_uint16(h2E), C.c_ubyte(0x5A)
    out, count, fault = (C.c_uint32 * 96)(), C.c_int(), C.c_uint32()
    rc = native.opening_shim(C.byref(nb00), C.byref(nb04), C.byref(nb05), C.byref(nh2E), C.byref(n811),
                             refused, flag, result, out, C.byref(count), C.byref(fault))
    assert rc == 0 and count.value <= 32, ('native faulted', hex(fault.value))
    events = [tuple(out[3 * i:3 * i + 3]) for i in range(count.value)]
    return events, (nb00.value, nb04.value, nb05.value, nh2E.value, n811.value)


def main():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA256
    overlay = (DECOMP / 'extract/OVERLAY/AREA11.BIN').read_bytes()
    assert len(overlay) == SIZE and overlay[:4] == b'MWo3' and struct.unpack_from('<I', overlay, 8)[0] == ARENA, \
        'not the original AREA11 overlay'
    ram = (DECOMP / 'build/startup-reference/opening_ee.bin').read_bytes()
    code = slice(ENTRY - ARENA, ENTRY - ARENA + LENGTH)
    assert ram[ARENA + code.start:ARENA + code.stop] == overlay[code], \
        'the capture does not hold this overlay function at its arena address'
    native = build_native()
    def product(b04s):
        # the inputs a state reads: keep the product small where a state ignores them
        return [c for c in itertools.product(b04s, (0, 1, 2, 3), (0, 1), (0, 0x1234), (0, 1), (0, 1), (0, 1, 3))
                if (c[0] == 0 or c[5] == 0) and (c[0] == 1 or (c[2] == 0 and c[3] == 0 and c[6] == 0))]
    total = len(product(range(256)))
    cases = product(range(256)) if reference_mode.FULL else product([0, 1, 2, 3, 4, 5, 0x7F, 0xFF])
    counts = {}
    for b04, b05, b00, h2E, refused, flag, result in cases:
        expected = original_case(elf, overlay, b00, b04, b05, h2E, refused, flag, result)
        actual = native_case(native, b00, b04, b05, h2E, refused, flag, result)
        label = dict(b04=b04, b05=b05, b00=b00, h2E=h2E, refused=refused, flag=flag, result=result)
        assert actual[0] == expected[0], (label, 'calls', actual[0], expected[0])
        assert actual[1] == expected[1], (label, 'record (+0, +4, +5, +2E, D_00810811)', actual[1], expected[1])
        key = 'free' if any(e[0] == 0x1AFC10 for e in expected[0]) else \
              'state 0' if b04 == 0 else 'state 1' if b04 == 1 else 'return'
        counts[key] = counts.get(key, 0) + 1
    reference_mode.banner(reference_mode.part(len(cases), total, 'controller cases'))
    print('area11 opening 00823E80 reference: PASS (' +
          ', '.join(f'{v} {k}' for k, v in sorted(counts.items())) + ', identical to the executed overlay)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
