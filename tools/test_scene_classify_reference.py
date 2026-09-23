#!/usr/bin/env python3
"""Compare em_sf_001AE7E0 with the original classifier 0x1AE7E0 (WP-3 step S1).

Executes the original ELF instructions at 0x1AE7E0 in the base Oracle
(tools/test_point_light_reference.py), extended here with lhu, over the design's
full input product (SCENE_COORDINATOR_DESIGN.md section 6, row S1):

  B8{0,1,2} x B9{0,1} x CE{0,1,2} x C5{0,1} x B0{0,1} x D_0028A9A0{0..3}
  x 3B8D{0,1,2,4} x E74{0,0x10,0x40,0x100,0x800,0x810,0x900,0xFFFF}
  x E50{0,4,7} x B3{0,1}                                   = 55,296 cases

each once over a zero background and once over a deterministic noise
background (every other request byte, the neighbouring input halfwords, the
neighbouring scratchpad bytes and D_0028A9A2.. are noise, so a wrong offset or
load width shows up). Plus boundary cases (every E50 value, every E74 bit,
signed D_0028A9A0 extremes, 0x80/0xFF request bytes).

Required: identical result, and identical bytes written. The original performs
no store (asserted), and the native classifier leaves the whole EmSceneState
unchanged (asserted by memcmp in the shim).

The lhu extension is validated two ways before it is trusted: directly, on
synthetic halfwords with the sign bit set (lhu zero-extends, lh sign-extends),
and by a third witness, the byte-matched decomp C
(Extermination/src/func_001AE7E0.c) compiled natively, which must agree with
the oracle on every case.

Lead decision Q1 is tested separately: em_scene_classify_q1 must equal the
original executed with E74 & ~0x100, report SELECT exactly when E74 & 0x100,
and leave the canonical state unchanged.
"""
import ctypes as C
import hashlib
import itertools
import json
from pathlib import Path
import random
import subprocess
import sys

from test_point_light_reference import Oracle

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent/'Extermination'
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
ENTRY = 0x1AE7E0
REQ, REQ_SIZE = 0x8106B0, 0x48
E50, E70, E74 = 0x810E50, 0x810E70, 0x810E74
FADE = 0x28A9A0
SPAD = 0x70003B8C  # 3B8C..3B93
INPUT_REQ = {'B0': 0x00, 'B3': 0x03, 'B8': 0x08, 'B9': 0x09, 'C5': 0x15, 'CE': 0x1E}

SHIM = r'''
#include <string.h>
#include "game/em_scene_classify.h"

static void fill(EmSceneState *s, uint32_t seed)
{
    unsigned char *p = (unsigned char *)s;
    for (size_t i = 0; i < sizeof *s; ++i) {
        seed = seed * 1103515245u + 12345u;
        p[i] = (unsigned char)(seed >> 16);
    }
}

int scene_classify_shim(const uint8_t *req, const uint8_t *spad, uint16_t e74, uint16_t e70,
                        uint8_t e50, int16_t fade, uint32_t seed, int q1, int *unchanged,
                        int *withheld)
{
    EmSceneState s, before;
    fill(&s, seed);
    memcpy(s.req, req, sizeof s.req);
    s.spad3B8C = spad[0]; s.spad3B8D = spad[1]; s.spad3B8E = spad[2]; s.spad3B8F = spad[3];
    s.spad3B90 = spad[4]; s.spad3B91 = spad[5]; s.spad3B92 = spad[6]; s.spad3B93 = spad[7];
    s.d810E74 = e74; s.d810E70 = e70; s.d810E50 = e50;
    memcpy(&before, &s, sizeof s);
    int result = q1 ? em_scene_classify_q1(&s, fade, withheld) : em_sf_001AE7E0(&s, fade);
    *unchanged = memcmp(&before, &s, sizeof s) == 0;
    return result;
}
'''

# Third witness: the byte-matched decomp C, compiled natively.
DECOMP_SHIM = r'''
unsigned char D_008106B0, D_008106B3, D_008106B8, D_008106B9, D_008106C5, D_008106CE;
unsigned char D_00810E50, D_70003B8D;
unsigned short D_00810E74;
short D_0028A9A0;
int func_001AE7E0(void);

int decomp_classify(const unsigned char *req, unsigned char sel, unsigned short e74,
                    unsigned char e50, short fade)
{
    D_008106B0 = req[0x00]; D_008106B3 = req[0x03]; D_008106B8 = req[0x08];
    D_008106B9 = req[0x09]; D_008106C5 = req[0x15]; D_008106CE = req[0x1E];
    D_70003B8D = sel; D_00810E74 = e74; D_00810E50 = e50; D_0028A9A0 = fade;
    return func_001AE7E0();
}
'''


class ClassifierOracle(Oracle):
    """Base Oracle plus lhu (opcode 37); records every store after setup."""

    def __init__(self, elf):
        self.recording = False
        self.stores = []
        super().__init__(elf)

    def save(self, address, value, size=4):
        if self.recording:
            self.stores.append((address, size))
        super().save(address, value, size)

    def plain(self, word):
        if word >> 26 == 37:  # lhu: zero-extended halfword load
            rs, rt = word >> 21 & 31, word >> 16 & 31
            imm = word & 0xFFFF
            imm = imm - 0x10000 if imm & 0x8000 else imm
            if rt:
                self.r[rt] = self.load((self.r[rs] + imm) & 0xFFFFFFFF, 2)
            return
        super().plain(word)


def validate_lhu(elf):
    """lhu zero-extends and lh sign-extends, at negative and positive offsets."""
    checked = 0
    for value in (0x0000, 0x0001, 0x7FFF, 0x8000, 0x8001, 0xFFFF, 0x1234, 0xFEFF):
        for offset in (-2, 0, 6):
            for op, expected in ((37, value), (33, value - 0x10000 if value & 0x8000 else value)):
                o = ClassifierOracle(elf)
                o.save(0x500100 + offset, value, 2)
                o.save(0x500100 + offset + 2, 0xA5A5, 2)  # a neighbour must not leak
                o.save(0x500000, (op << 26) | (4 << 21) | (2 << 16) | (offset & 0xFFFF))
                o.save(0x500004, 0x03E00008)  # jr ra
                o.save(0x500008, 0)           # nop
                o.run(0x500000, [0x500100])
                assert o.r[2] == expected & 0xFFFFFFFF, (op, hex(value), offset, hex(o.r[2]))
                checked += 1
    return checked


def noise_background(rng):
    return {
        'req': bytearray(rng.randrange(256) for _ in range(REQ_SIZE)),
        'spad': bytearray(rng.randrange(256) for _ in range(8)),
        'e70': rng.randrange(0x10000),
        'around': {a: rng.randrange(256) for a in
                   [*range(0x810E40, 0x810E80), *range(0x28A990, 0x28A9B0),
                    *range(0x70003B80, 0x70003BA0), *range(0x8106A0, 0x8106B0),
                    *range(0x8106F8, 0x810700)]},
        'seed': rng.randrange(1 << 32),
    }


def zero_background():
    return {'req': bytearray(REQ_SIZE), 'spad': bytearray(8), 'e70': 0, 'around': {}, 'seed': 0}


def original(elf, bg, case):
    o = ClassifierOracle(elf)
    for address, value in bg['around'].items():
        o.save(address, value, 1)
    req = bytearray(bg['req'])
    for name, offset in INPUT_REQ.items():
        req[offset] = case[name]
    o.write(REQ, req)
    spad = bytearray(bg['spad']); spad[1] = case['sel']
    o.write(SPAD, spad)
    o.save(E74, case['e74'], 2)
    o.save(E70, bg['e70'], 2)
    o.save(E50, case['e50'], 1)
    o.save(FADE, case['fade'] & 0xFFFF, 2)
    o.recording = True
    o.run(ENTRY)
    assert not o.stores, ('original classifier stored', [(hex(a), n) for a, n in o.stores])
    result = o.r[2]
    assert result in (0, 1, 2, 3), result
    return result, bytes(req), bytes(spad)


def main():
    elf = (DECOMP/'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA256
    out = ROOT/'build/scene_classify_reference'; out.mkdir(parents=True, exist_ok=True)
    ext = 'dylib' if sys.platform == 'darwin' else 'so'
    link = '-dynamiclib' if sys.platform == 'darwin' else '-shared'
    (out/'shim.c').write_text(SHIM)
    (out/'decomp_shim.c').write_text(DECOMP_SHIM)
    native_lib, decomp_lib = out/f'scene_classify.{ext}', out/f'decomp_classify.{ext}'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC', link, '-Isrc',
                    'src/game/em_scene_classify.c', str(out/'shim.c'), '-o', str(native_lib)],
                   cwd=ROOT, check=True)
    subprocess.run(['cc', '-std=c11', '-O2', '-fPIC', link, str(out/'decomp_shim.c'),
                    str(DECOMP/'src/func_001AE7E0.c'), '-o', str(decomp_lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(native_lib))
    native.scene_classify_shim.argtypes = [C.c_char_p, C.c_char_p, C.c_uint16, C.c_uint16,
                                           C.c_uint8, C.c_int16, C.c_uint32, C.c_int,
                                           C.POINTER(C.c_int), C.POINTER(C.c_int)]
    decomp = C.CDLL(str(decomp_lib))
    decomp.decomp_classify.argtypes = [C.c_char_p, C.c_uint8, C.c_uint16, C.c_uint8, C.c_int16]

    lhu_checks = validate_lhu(elf)

    def run_native(req, spad, e74, bg, case, q1):
        unchanged, withheld = C.c_int(-1), C.c_int(-1)
        result = native.scene_classify_shim(req, spad, e74, bg['e70'], case['e50'], case['fade'],
                                            bg['seed'], q1, C.byref(unchanged), C.byref(withheld))
        assert unchanged.value == 1, ('native classifier modified EmSceneState', case)
        return result, withheld.value

    product = [dict(zip(('B8', 'B9', 'CE', 'C5', 'B0', 'fade', 'sel', 'e74', 'e50', 'B3'), values))
               for values in itertools.product(
                   (0, 1, 2), (0, 1), (0, 1, 2), (0, 1), (0, 1), (0, 1, 2, 3), (0, 1, 2, 4),
                   (0, 0x10, 0x40, 0x100, 0x800, 0x810, 0x900, 0xFFFF), (0, 4, 7), (0, 1))]
    assert len(product) == 55296
    boundary = []
    base = dict(B8=0, B9=0, CE=0, C5=0, B0=0, fade=0, sel=0, e74=0, e50=4, B3=0)
    for e50 in range(256):
        for e74 in (0, 0x100, 0x800, 0x10, 0x8000, 0x0001):
            for b3 in (0, 1):
                boundary.append(dict(base, e50=e50, e74=e74, B3=b3))
    for bit in range(16):
        for b3 in (0, 1):
            for e50 in (4, 5):
                boundary.append(dict(base, e74=1 << bit, B3=b3, e50=e50))
    for fade in (-32768, -1, 1, 2, 3, 4, 0x7FFF):
        for e74 in (0, 0x100, 0x800):
            boundary.append(dict(base, fade=fade, e74=e74))
            boundary.append(dict(base, fade=fade, e74=e74, CE=1))
    for sel in (1, 0x80, 0xFF):
        boundary.append(dict(base, sel=sel, e74=0x800))
    for name in INPUT_REQ:
        for value in (0x80, 0xFF):
            boundary.append(dict(base, e74=0x810, **{name: value}))

    rng = random.Random(0x1AE7E0)
    backgrounds = [('zero', zero_background()), ('noise', noise_background(rng))]
    counts = {}
    results = {0: 0, 1: 0, 2: 0, 3: 0}
    q1_checks = q1_changed = 0
    for label, bg in backgrounds:
        cases = 0
        for case in product + boundary:
            expected, req, spad = original(elf, bg, case)
            got, _ = run_native(req, spad, case['e74'], bg, case, 0)
            assert got == expected, (label, case, got, expected)
            witness = decomp.decomp_classify(req, case['sel'], case['e74'], case['e50'], case['fade'])
            assert witness == expected, ('decomp C disagrees with the oracle', label, case, witness, expected)
            # Q1: equal to the ORIGINAL executed on E74 without SELECT.
            masked = case['e74'] & ~0x100 & 0xFFFF
            q1_expected = expected if masked == case['e74'] else \
                original(elf, bg, dict(case, e74=masked))[0]
            q1_got, withheld = run_native(req, spad, case['e74'], bg, case, 1)
            assert q1_got == q1_expected, ('Q1', label, case, q1_got, q1_expected)
            assert withheld == int(bool(case['e74'] & 0x100)), ('Q1 withheld flag', case, withheld)
            q1_changed += q1_got != expected
            q1_checks += 1
            if label == 'zero':
                results[expected] += 1
            cases += 1
        counts[label] = cases

    # Q1 withholds only SELECT: the original returns 1 for SELECT alone
    # (0x1AE040 state 2 -> 0022A650); under Q1 the same input returns 0.
    solo = dict(base, e74=0x100)
    assert original(elf, zero_background(), solo)[0] == 1
    assert run_native(bytes(REQ_SIZE), bytes(8), 0x100, zero_background(), solo, 1)[0] == 0
    # E50 != 4 is not masked by Q1.
    e50_case = dict(base, e50=7)
    assert run_native(bytes(REQ_SIZE), bytes(8), 0, zero_background(), e50_case, 1)[0] == 1

    report = {
        'status': 'PASS',
        'original_entry': '0x1AE7E0',
        'design_product_cases': len(product),
        'boundary_cases': len(boundary),
        'cases_per_background': counts,
        'original_result_histogram_zero_background': results,
        'identical_results': True,
        'original_stores': 0,
        'native_state_unchanged': True,
        'decomp_c_witness_agrees': True,
        'oracle_lhu_extension_checks': lhu_checks,
        'q1_checks': q1_checks,
        'q1_cases_differing_from_original': q1_changed,
        'original_elf_sha256': ELF_SHA256,
    }
    (out/'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
