#!/usr/bin/env python3
"""Check camera Euler rotation against the user's original SDK instructions.

The bounded instruction runner is validation-only. No original instruction
bytes are embedded in this tool or used by the native game.
"""
import ctypes as C
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import sys
from test_point_light_reference import Oracle as Base, bits, number, signed

ROOT = Path(__file__).resolve().parents[1]
MATRIX, EULER, VECTOR, RESULT = 0x900000, 0x901000, 0x902000, 0x903000


class Original(Base):
    def plain(self, word):
        op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
        address = (self.r[rs] + signed(word & 65535, 16)) & 0xffffffff
        if op == 63:
            self.save(address, self.r[rt], 8)
        elif op == 55:
            self.r[rt] = self.load(address, 8)
        elif op == 25:
            self.r[rt] = address
        else:
            super().plain(word)
        self.r[0] = 0

    def rotation(self, angles, distance):
        self.write(EULER, struct.pack('<4f', *angles, 0))
        self.write(VECTOR, struct.pack('<4f', 0, 0, distance, 1))
        self.run(0x1029C0, (MATRIX,))
        self.run(0x102C58, (MATRIX, MATRIX, EULER))
        self.run(0x1026A0, (RESULT, MATRIX, VECTOR))
        return self.read(MATRIX, 64), self.read(RESULT, 16)


def main():
    elf = (ROOT.parent/'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
    output = ROOT/'build/camera_rotation'
    output.mkdir(parents=True, exist_ok=True)
    library = output/('rotation.dylib' if sys.platform == 'darwin' else 'rotation.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
        '-ffp-contract=off', '-fPIC', '-dynamiclib' if sys.platform == 'darwin' else '-shared',
        '-Isrc', 'src/game/em_camera_rotation.c', '-lm', '-o', str(library)],
        cwd=ROOT, check=True)
    native = C.CDLL(str(library))
    native.em_camera_rotation_offset.argtypes = [C.POINTER(C.c_float), C.c_float,
        C.POINTER(C.c_float), C.POINTER(C.c_float)]
    random_source = random.Random(0x18CBD0)
    cases = [(0, 0, 0), (0, -1.303761, 0), (.2, .4, .6)]
    pi = number(bits(3.141592653589793))
    for axis in range(3):
        for value in (-pi, -pi/2, -0.001, 0, 0.001, pi/2, pi):
            angles = [0, 0, 0]
            angles[axis] = value
            cases.append(angles)
    cases.extend([random_source.uniform(-pi, pi) for _ in range(3)] for _ in range(300))
    count = 0
    for angles in cases:
        values = (C.c_float*3)(*angles)
        for distance in (-46.8, -20, -14):
            distance = number(bits(distance))
            matrix, offset = Original(elf).rotation(list(values), distance)
            native_matrix, native_offset = (C.c_float*16)(), (C.c_float*4)()
            assert native.em_camera_rotation_offset(values, distance, native_matrix, native_offset) == 1
            for label, actual, expected in [('matrix', bytes(native_matrix), matrix),
                                            ('offset', bytes(native_offset), offset)]:
                a, b = struct.unpack('<'+'I'*(len(actual)//4), actual), struct.unpack('<'+'I'*(len(expected)//4), expected)
                differences = [(i, hex(x), hex(y)) for i, (x, y) in enumerate(zip(a, b)) if x != y]
                assert not differences, (label, list(values), distance, differences)
            count += 1
    report = {'rotation_and_transform_cases': count, 'all_output_bytes_exact': True,
              'domain': 'finite normalized Euler angles and first-level camera distances',
              'boundaries': 'bounded VU arithmetic model; not general PS2 hardware equivalence'}
    (output/'result.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
