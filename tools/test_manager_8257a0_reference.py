#!/usr/bin/env python3
"""AREA11 record 13 (overlay manager 0x008257A0): native vs original (S12a).

Executes the original behaviour from the user's AREA11 overlay (loaded at its
arena 0x823500: the opening capture holds the function's bytes there) with
the main-ELF 001BA1C0 it calls, over the node byte +4 (every value 0..255), +5, the event byte
D_00810758[0x3C] (D_00810794) and D_00810788, and compares with
src/game/em_manager_008257A0.c: the node bytes +0/+4 afterwards and the call
to 001AFC10(self) and the state-1 calls (001B1EA0 over the area 0x82ACA0,
001BA1A0 of the script 0x829E80, 001BA1F0, 001DFE40, 001B17A0) in order with
their arguments, over every result of 001B1EA0 and 001BA1F0, and the
D_00810814 store. Ground truth: the decomp's byte-identical C
(src/overlays/AREA11/func_overlay_AREA11_00825760.c).
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
ENTRY = 0x8257A0
NODE = 0x7A96E0           # record 13's node in the captures
FREE = 0x1AFC10
CALLEES = (FREE, 0x1B1EA0, 0x1BA1A0, 0x1BA1F0, 0x1DFE40, 0x1B17A0)
D814 = 0x810814


class ManagerOracle(OwnerOracle):
    def __init__(self, elf, overlay, inside, result):
        super().__init__(elf)
        self.write(ARENA, overlay)
        self.events = []
        returns = {0x1B1EA0: inside, 0x1BA1F0: result}

        def hook(callee):
            def call(o):
                args = {0x1B1EA0: 4, 0x1BA1A0: 2, 0x1DFE40: 0}.get(callee, 1)
                o.events.append((callee, *[o.r[4 + i] & 0xFFFFFFFF for i in range(args)]))
                o.r[2] = returns.get(callee, 0)
            return call
        for callee in CALLEES:
            self.calls[callee] = hook(callee)


SHIM = r'''
#include "game/em_manager_008257A0.h"
#include <stdint.h>
#define SELF 0x7A96E0u
static uint32_t ev[64]; static int nev;
static int32_t g_inside, g_result;
static uint8_t g_814;
static void put(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, int n)
{ uint32_t v[5] = {a, b, c, d, e}; for (int i = 0; i < 5; ++i) if (nev < 64) ev[nev++] = i < n ? v[i] : 0xFFFFFFFFu; }
static int w_free(void *c) { (void)c; put(0x1AFC10, SELF, 0, 0, 0, 2); return 0; }
static int w_1EA0(void *c, int32_t *r) { (void)c; put(0x1B1EA0, 0, 0x810350, 0x82ACA0, 4, 5); *r = g_inside; return 0; }
static int w_A1A0(void *c, uint32_t e) { (void)c; put(0x1BA1A0, SELF + 0x1F0, e, 0, 0, 3); return 0; }
static int w_A1F0(void *c, int32_t *r) { (void)c; put(0x1BA1F0, SELF, 0, 0, 0, 2); *r = g_result; return 0; }
static int w_FE40(void *c) { (void)c; put(0x1DFE40, 0, 0, 0, 0, 1); return 0; }
static int s_814(void *c, uint8_t v) { (void)c; g_814 = v; return 0; }
static int w_17A0(void *c) { (void)c; put(0x1B17A0, SELF, 0, 0, 0, 2); return 0; }
int manager_shim(unsigned char *b00, unsigned char *b04, unsigned char *b05, unsigned char e794, unsigned char e788,
                 int32_t inside, int32_t result, unsigned char *d814, uint32_t *out, int *count, unsigned *fault)
{
    EmManager8257A0 m = { *b00, *b04, e794, e788, *b05 };
    EmManager8257A0Workers w = { 0, w_free, w_1EA0, w_A1A0, w_A1F0, w_FE40, s_814, w_17A0 };
    nev = 0; g_inside = inside; g_result = result; g_814 = *d814; *fault = 0;
    int rc = em_manager_008257A0_tick(&m, &w, fault);
    *b00 = m.b00; *b04 = m.b04; *b05 = m.b05; *d814 = g_814;
    for (int i = 0; i < nev; ++i) out[i] = ev[i];
    *count = nev;
    return rc;
}
'''


def build_native():
    out = ROOT / 'build/manager_8257a0_reference'
    out.mkdir(parents=True, exist_ok=True)
    source = out / 'shim.c'
    source.write_text(SHIM)
    lib = out / ('shim.dylib' if sys.platform == 'darwin' else 'shim.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC', '-Isrc',
                    str(source), 'src/game/em_manager_008257A0.c', '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    u8p = C.POINTER(C.c_ubyte)
    native.manager_shim.argtypes = [u8p, u8p, u8p, C.c_ubyte, C.c_ubyte, C.c_int32, C.c_int32, u8p,
                                    C.POINTER(C.c_uint32), C.POINTER(C.c_int), C.POINTER(C.c_uint)]
    return native


def native_events(out, count):
    words = [out[i] for i in range(count)]
    events = []
    for i in range(0, len(words), 5):
        events.append(tuple(w for w in words[i:i + 5] if w != 0xFFFFFFFF))
    return events


def main():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA256
    overlay = (DECOMP / 'extract/OVERLAY/AREA11.BIN').read_bytes()
    assert len(overlay) == SIZE and overlay[:4] == b'MWo3' and struct.unpack_from('<I', overlay, 8)[0] == ARENA, \
        'not the original AREA11 overlay'
    ram = (DECOMP / 'build/startup-reference/opening_ee.bin').read_bytes()
    code = slice(ENTRY - ARENA, ENTRY - ARENA + 0x154)   # the function (splat size 0x154)
    assert ram[ARENA + code.start:ARENA + code.stop] == overlay[code], \
        'the capture does not hold this overlay function at its arena address'
    native = build_native()

    def product(states):
        # state 1 reads +5, 001B1EA0 and 001BA1F0; the others read the event bytes
        return [c for c in itertools.product(states, (0, 1, 2), (0, 1, 0xFE, 0xFF), (0, 1, 0xFF), (0, 1),
                                             (0, 1), (0, 1, 3))
                if c[0] == 1 or (c[5] == 0 and c[6] == 0)]
    total = len(product(range(256)))
    cases = product(range(256)) if reference_mode.FULL else product([0, 1, 2, 3, 4, 5, 0x7F, 0x80, 0xFF])
    counts = {'free': 0, 'arm': 0, 'state 1': 0, 'return': 0}
    for b04, b05, e794, e788, b00, inside, result in cases:
        o = ManagerOracle(elf, overlay, inside, result)
        o.save(NODE, b00, 1)
        o.save(NODE + 4, b04, 1)
        o.save(NODE + 5, b05, 1)
        o.save(0x810758 + 0x3C, e794, 1)
        o.save(0x810788, e788, 1)
        o.save(D814, 0x5A, 1)
        o.run(ENTRY, [NODE])
        n00, n04, n05, n814 = C.c_ubyte(b00), C.c_ubyte(b04), C.c_ubyte(b05), C.c_ubyte(0x5A)
        out, count, fault = (C.c_uint32 * 64)(), C.c_int(), C.c_uint()
        rc = native.manager_shim(C.byref(n00), C.byref(n04), C.byref(n05), e794, e788, inside, result,
                                 C.byref(n814), out, C.byref(count), C.byref(fault))
        label = dict(b04=b04, b05=b05, e794=e794, e788=e788, b00=b00, inside=inside, result=result)
        assert rc == 0, (label, 'native faulted', hex(fault.value))
        assert native_events(out, count.value) == o.events, (label, 'calls', native_events(out, count.value),
                                                             o.events)
        got = (n00.value, n04.value, n05.value, n814.value)
        want = (o.load(NODE, 1), o.load(NODE + 4, 1), o.load(NODE + 5, 1), o.load(D814, 1))
        assert got == want, (label, 'bytes (+0, +4, +5, D_00810814)', got, want)
        key = 'free' if any(e[0] == FREE for e in o.events) else 'state 1' if b04 == 1 else \
              'arm' if got[:2] != (b00, b04) else 'return'
        counts[key] += 1
    reference_mode.banner(reference_mode.part(len(cases), total, 'manager cases'))
    print(f'manager 008257A0 reference: PASS ({counts["free"]} self-frees, {counts["arm"]} state-0 arms, '
          f'{counts["state 1"]} state-1 calls, {counts["return"]} returns, identical to the executed overlay)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
