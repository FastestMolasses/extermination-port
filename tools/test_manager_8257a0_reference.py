#!/usr/bin/env python3
"""AREA11 record 13 (overlay manager 0x008257A0): native vs original (S12a).

Executes the original behaviour from the user's AREA11 overlay (loaded at its
arena 0x823500: the opening capture holds the function's bytes there) with
the main-ELF 001BA1C0 it calls, over the node byte +4 (every value 0..255), +5, the event byte
D_00810758[0x3C] (D_00810794) and D_00810788, and compares with
src/game/em_manager_008257A0.c: the node bytes +0/+4 afterwards and the call
to 001AFC10(self). The state-1 script arm (001B1EA0, 001BA1A0, 001BA1F0,
001DFE40, 001B17A0) is not translated: wherever the original enters it (one
of those stubs is reached), the native must fault instead, and nowhere else.
Only addresses and values are printed; no original bytes are embedded.
"""
import ctypes as C
import hashlib
import itertools
from pathlib import Path
import struct
import subprocess
import sys

from test_point_light_reference import Oracle
import reference_mode

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
ARENA, SIZE = 0x823500, 0x7800
ENTRY = 0x8257A0
NODE = 0x7A96E0           # record 13's node in the captures
FREE = 0x1AFC10
SCRIPT = (0x1B1EA0, 0x1BA1A0, 0x1BA1F0, 0x1DFE40, 0x1B17A0)


class ManagerOracle(Oracle):
    def __init__(self, elf, overlay):
        super().__init__(elf)
        self.write(ARENA, overlay)
        self.events = []
        for callee in (FREE,) + SCRIPT:
            self.calls[callee] = lambda o, c=callee: o.events.append((c, o.r[4] & 0xFFFFFFFF))


SHIM = r'''
#include "game/em_manager_008257A0.h"
static int freed;
static int w_free(void *ctx) { (void)ctx; ++freed; return 0; }
int manager_shim(unsigned char *b00, unsigned char *b04, unsigned char e794, unsigned char e788,
                 int *nfree, unsigned *fault)
{
    EmManager8257A0 m = { *b00, *b04, e794, e788 };
    EmManager8257A0Workers w = { 0, w_free };
    freed = 0; *fault = 0;
    int rc = em_manager_008257A0_tick(&m, &w, fault);
    *b00 = m.b00; *b04 = m.b04; *nfree = freed;
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
    native.manager_shim.argtypes = [C.POINTER(C.c_ubyte), C.POINTER(C.c_ubyte), C.c_ubyte, C.c_ubyte,
                                    C.POINTER(C.c_int), C.POINTER(C.c_uint)]
    return native


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
    states = list(range(256)) if reference_mode.FULL else [0, 1, 2, 3, 4, 5, 0x7F, 0x80, 0xFF]
    cases = list(itertools.product(states, (0, 1), (0, 1, 0xFE, 0xFF), (0, 1, 0xFF), (0, 1)))
    counts = {'free': 0, 'arm': 0, 'script-fault': 0, 'return': 0}
    for b04, b05, e794, e788, b00 in cases:
        o = ManagerOracle(elf, overlay)
        o.save(NODE, b00, 1)
        o.save(NODE + 4, b04, 1)
        o.save(NODE + 5, b05, 1)
        o.save(0x810758 + 0x3C, e794, 1)
        o.save(0x810788, e788, 1)
        o.run(ENTRY, [NODE])
        n00, n04 = C.c_ubyte(b00), C.c_ubyte(b04)
        nfree, fault = C.c_int(), C.c_uint()
        rc = native.manager_shim(C.byref(n00), C.byref(n04), e794, e788, C.byref(nfree), C.byref(fault))
        label = (b04, b05, e794, e788, b00)
        script = [e for e in o.events if e[0] in SCRIPT]
        if script:
            assert rc < 0 and fault.value == ENTRY, (label, 'original entered the script arm; native did not fault')
            counts['script-fault'] += 1
            continue
        assert rc == 0, (label, 'native faulted', hex(fault.value))
        frees = [e for e in o.events if e[0] == FREE]
        assert all(a == NODE for _, a in frees), (label, 'free argument')
        assert len(frees) == nfree.value, (label, 'free calls', len(frees), nfree.value)
        assert o.load(NODE, 1) == n00.value and o.load(NODE + 4, 1) == n04.value, \
            (label, 'node bytes', o.load(NODE, 1), n00.value, o.load(NODE + 4, 1), n04.value)
        counts['free' if frees else 'arm' if n04.value != b04 or n00.value != b00 else 'return'] += 1
    reference_mode.banner(reference_mode.part(len(cases), 256 * 2 * 4 * 3 * 2, 'manager cases'))
    print(f'manager 008257A0 reference: PASS ({counts["free"]} self-frees, {counts["arm"]} state-0 arms, '
          f'{counts["return"]} returns, {counts["script-fault"]} script-arm faults identical to the '
          f'executed overlay)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
