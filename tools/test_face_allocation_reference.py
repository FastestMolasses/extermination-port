#!/usr/bin/env python3
"""Original player face allocation/reset/free, using captured pool state."""
import ctypes as C
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess

from test_item_sdk_math_reference import Original
from test_interaction_scan_reference import ELF_SHA
from test_opening_face_reference import Face

ROOT = Path(__file__).resolve().parents[1]
PLAYER = 0x8102B0


class Captured(Original):
    def __init__(self, elf, ram):
        self.ram = ram
        super().__init__(elf)

    def load(self, address, size=4):
        return sum(self.mem.get(address+i, self.ram[address+i] if address+i < len(self.ram)
                                else 0) << (8*i) for i in range(size))


def state(original, face):
    return original.read(face+0x40, 32)+original.read(face+0x70, C.sizeof(Face)-32)


def main():
    decomp = ROOT.parent / 'Extermination'
    elf = (decomp / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    out = ROOT / 'build/face_allocation_reference'
    out.mkdir(parents=True, exist_ok=True)
    library = out / 'face.dylib'
    subprocess.run(['cc', '-shared', '-fPIC', '-std=c11', '-O2', '-ffp-contract=off',
                    '-Isrc', 'src/game/em_opening_face.c', '-o', str(library)], cwd=ROOT, check=True)
    native = C.CDLL(str(library))
    native.em_opening_face_init.argtypes = [C.POINTER(Face), C.c_uint8]
    native.em_opening_face_reset.argtypes = [C.POINTER(Face)]
    captures = []
    for name in ('playable_ee.bin', 'status-hub/eeMemory.bin'):
        ram = (decomp / 'build/startup-reference' / name).read_bytes()
        original = Captured(elf, ram)
        assert original.load(PLAYER+0x90) == 0 and original.load(PLAYER+0x2FF, 1) == 0x3B
        original.save(0x70003B8F, 1, 1)
        original.run(0x1B81D0, (PLAYER,))
        face = original.load(PLAYER+0x90)
        assert face and ram[face:face+0xD0] == bytes(0xD0)
        assert original.load(PLAYER+0x94, 2) == 7
        assert original.load(face+0x60) == original.load(0x28A490+0x18*4)
        assert original.load(0x70003B8F, 1) == 2
        actual = Face()
        native.em_opening_face_init(C.byref(actual), 1)
        assert bytes(actual) == state(original, face)
        original.run(0x1CA770, (PLAYER,))
        assert original.read(face, 0xD0) == bytes(0xD0)
        assert original.load(PLAYER+0x90) == 0 and original.load(PLAYER+0x94, 2) == 65535
        captures.append({'capture': name, 'sha256': hashlib.sha256(ram).hexdigest(),
                         'selected_block_was_zero': True, 'face_state_bytes': C.sizeof(Face),
                         'release_zero_bytes': 208})
    rng = random.Random(0x1CA700)
    ram = (decomp / 'build/startup-reference/playable_ee.bin').read_bytes()
    for _ in range(256):
        original = Captured(elf, ram)
        original.run(0x1B81D0, (PLAYER,))
        face = original.load(PLAYER+0x90)
        actual = Face.from_buffer_copy(bytes(rng.randrange(256) for _ in range(C.sizeof(Face))))
        original.write(face+0x40, bytes(actual)[:32])
        original.write(face+0x70, bytes(actual)[32:])
        old_available = original.load(0x275BCC, 2)
        original.run(0x1B81D0, (PLAYER,))
        native.em_opening_face_reset(C.byref(actual))
        actual.speed = 1
        assert original.load(PLAYER+0x90) == face and original.load(0x275BCC, 2) == old_available
        assert bytes(actual) == state(original, face)
    report = {'captures': captures, 'existing_face_reset_cases': 256,
              'scope': 'full allocator/face initializer/free instructions; later RNG order and morph rounding separate'}
    (out / 'result.json').write_text(json.dumps(report, indent=2)+'\n')
    print('Original face allocation PASS:', json.dumps(report))


if __name__ == '__main__': main()
