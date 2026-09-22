#!/usr/bin/env python3
"""Compare the actual readable D930 C source with every original menu table."""
import ctypes as C
import hashlib
import itertools
import json
from pathlib import Path
import struct
import subprocess
import sys

from test_item_sdk_math_reference import Original, ELF_SHA
from test_point_light_reference import bits, number

ROOT = Path(__file__).resolve().parents[1]


def main():
    decomp = ROOT.parent / 'Extermination'
    elf = (decomp / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    output = ROOT / 'build/menu_hover_source_reference'
    output.mkdir(parents=True, exist_ok=True)
    shim = output / 'workers.c'
    shim.write_text('''#include <stdint.h>
#include <string.h>
float D_700038A0, D_700038A8, D_700038AC;
static unsigned sounds;
void test_input(float magnitude, float angle) {
    D_700038A8 = magnitude; D_700038AC = angle; sounds = 0;
}
unsigned test_sounds(void) { return sounds; }
void func_001B62C0(float *out) { (void)out; }
uint64_t func_00128350(float value) {
    double promoted = value; uint64_t encoded;
    memcpy(&encoded, &promoted, 8); return encoded;
}
int func_00100130(uint64_t left, uint64_t right) {
    double a, b; memcpy(&a, &left, 8); memcpy(&b, &right, 8);
    return a >= b;
}
void func_001FB9F0(int cue, int a, int b, int c) {
    if (cue == 5 && a == 4096 && b == 4096 && c == 4096) ++sounds;
    else sounds = 100;
}
''')
    library = output / ('hover.dylib' if sys.platform == 'darwin' else 'hover.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC',
        '-dynamiclib' if sys.platform == 'darwin' else '-shared',
        str(decomp / 'src/func_0020D930.c'), str(shim), '-o', str(library)], check=True)
    native = C.CDLL(str(library))
    native.test_input.argtypes = [C.c_float, C.c_float]
    native.func_0020D930.argtypes = [C.POINTER(C.c_uint8), C.c_int]
    angles = {0, bits(-3.14159274), bits(3.14159274)}
    for boundary in (-2.7576203, -2.670354, -2.3561945, -2.0071287, -1.5707964,
                     -0.7853982, -0.5235988, -0.41887903, 0.36651915, 0.5235988,
                     0.7853982, 1.5707964, 2.0071287, 2.3561945, 2.7576203):
        angles.update(bits(boundary) + d for d in (-1, 0, 1))
    checks = 0
    for selector, previous, magnitude, encoded in itertools.product((-1, 0, 1, 2, 3, 255),
            range(7), (0, number(bits(.8) - 1), number(bits(.8)), 1), sorted(angles)):
        angle = number(encoded)
        original = Original(elf)
        original.save(0x960011, previous, 1)
        original.calls[0x1b62c0] = lambda o: o.write(o.r[4], struct.pack('<4f', 0, 0, magnitude, angle))
        sound_calls = []
        original.calls[0x1fb9f0] = lambda o: sound_calls.append(tuple(o.r[4:8]))
        original.run(0x20d930, (0x960000, selector & 0xffffffffffffffff))
        state = (C.c_uint8 * 32)()
        state[17] = previous
        native.test_input(magnitude, angle)
        native.func_0020D930(state, selector)
        assert state[17] == original.load(0x960011, 1), (selector, previous, magnitude, angle,
                                                       state[17], original.load(0x960011, 1))
        assert all(call == (5, 4096, 4096, 4096) for call in sound_calls)
        assert native.test_sounds() == len(sound_calls)
        checks += 1
    result = {'readable_source': 'Extermination/src/func_0020D930.c',
              'all_table_state_and_sound_cases': checks, 'result': 'PASS',
              'boundaries': 'stick sampling supplied identically; original soft-double bodies execute'}
    (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
