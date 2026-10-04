#!/usr/bin/env python3
"""Actor serialization/lifetime against original pool and worker instructions.

Synthetic byte mutations are test inputs, not runtime seeds. Shared-model
span fixtures test ownership/commit contracts; they do not replace or claim
to verify model allocation. One original movement helper runs through the
sparse math provider with its matrix-vector callee explicitly scripted.
"""
import ctypes as C
import hashlib
import random
import struct
import subprocess
from pathlib import Path

import reference_mode as mode
from test_actor_pool_reference import Original, ELF_SHA256, POOL, SIZE, COUNT
from test_player_fall_reference import FallEE

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/level2/actor-view'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    library = OUT / 'bridge.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                    '-Isrc', 'tests/area01_actor_view_bridge.c', 'src/game/em_area01_actor_view.c',
                    'src/game/em_actor_pool.c', 'src/game/em_area01_math_core.c',
                    'src/game/em_area01_math_actor.c', '-o', str(library)], cwd=ROOT, check=True)
    lib = C.CDLL(str(library))
    lib.av_alloc.argtypes = [C.c_uint8]; lib.av_alloc.restype = C.c_uint32
    lib.av_bytes.argtypes = [C.c_uint32, C.c_uint32, C.c_int]; lib.av_bytes.restype = C.c_void_p
    lib.av_image.restype = lib.av_matrix.restype = C.c_void_p
    lib.av_native_word.argtypes = lib.av_math_alias.argtypes = [C.c_uint32, C.c_uint32]
    elf = (ROOT.parent / 'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA256
    o = Original(elf)

    def reset():
        lib.av_reset(); o.reset()

    def image():
        return C.string_at(lib.av_image(), SIZE*COUNT)

    def compare(label):
        actual, expected = image(), o.image()
        assert actual == expected, (label, next((hex(POOL+i), a, b) for i, (a,b) in
                                               enumerate(zip(actual, expected)) if a != b))

    def poke(node, offset, data):
        assert lib.av_begin() == 0
        p = lib.av_bytes(node+offset, len(data), 1)
        assert p
        C.memmove(p, data, len(data))
        assert lib.av_commit() == 0
        for i, b in enumerate(data): o.poke(node, offset+i, b)

    reset(); compare('reset')
    rng = random.Random(0xA01)
    operations = mode.pick(600, 60)
    live = []
    # Pool-owned links and bone counts remain the real allocator's. Every
    # other byte is exercised, including gaps, scratch, and both +34 halves.
    excluded = set(range(0x10, 0x20)) | {9, 0xC}
    offsets = [i for i in range(SIZE) if i not in excluded]
    for step in range(operations):
        if not live or (len(live) < 12 and rng.randrange(3) == 0):
            cls = rng.choice((1, 2, 4, 9, 0xE2))
            address = o.alloc(cls); assert lib.av_alloc(cls) == address
            live.append(address)
        else:
            address = rng.choice(live)
        chosen = offsets if mode.FULL and step == 0 else rng.sample(offsets, min(32, len(offsets)))
        for at in chosen: poke(address, at, bytes([rng.randrange(256)]))
        poke(address, 0x10, struct.pack('<I', 0xA00000 + (step % 32)*4))
        if rng.randrange(3) == 0:
            assert lib.av_free(address) == 0; o.free(address); live.remove(address)
        compare(('pool/mutate/free', step))
    # Explicit slot reuse: unrepresented bytes survive, represented free
    # clears remain authoritative, and reset finally clears everything.
    reset(); a = o.alloc(4); assert lib.av_alloc(4) == a
    poke(a, 0x20, bytes(range(14))); poke(a, 0x34, bytes.fromhex('78563412'))
    poke(a, 0xD0, bytes(range(64))); poke(a, 0x1F0, bytes([0xA5])*0x100)
    o.free(a); assert lib.av_free(a) == 0; compare('free preserves unknown')
    assert o.alloc(9) == a == lib.av_alloc(9); compare('reuse')
    reset(); compare('reset clears unknown')

    # Sparse math mirrors share the same bytes; both halves of +34 commit.
    a = o.alloc(4); assert lib.av_alloc(4) == a
    assert lib.av_math_alias(a, 0xFEDCBA98) == 0
    o.save(a+0x34, 0xFEDCBA98); compare('math mirrors +34')
    # Real 001C39F0 original code with one explicit helper boundary. Mode
    # zero follows the straight movement path and writes canonical position.
    f = FallEE(elf)
    f.mem[POOL:POOL+SIZE*COUNT] = o.image()
    f.hooks[0x1026A0] = lambda ee: ee.write(0x70003610, struct.pack('<4f', 2,3,4,1))
    f.call(0x1C39F0, (a, a+0x1F0), (1.0,))
    assert lib.av_math_move(a) == 0
    assert image() == bytes(f.mem[POOL:POOL+SIZE*COUNT])

    # Canonical model projections and writable matrix are explicit commit
    # boundaries, with no native model/slot allocator in this adapter.
    reset(); a = lib.av_alloc(4); lib.av_model(a)
    assert lib.av_begin() == 0
    whole = lib.av_bytes(a, SIZE, 0); assert whole
    before = C.string_at(whole, SIZE)
    assert before[0x40:0x48] == bytes(range(0x80,0x88))
    matrix = lib.av_matrix()
    # A request crosses private +CC and shared +D0: one coherent projected
    # view, followed by commits to the two legitimate canonical owners.
    p = lib.av_bytes(a+0xCC, 8, 1); assert p
    C.memmove(p, b'abcdefgh', 8)
    assert C.string_at(matrix, 4) != b'efgh'
    assert lib.av_commit() == 0 and C.string_at(matrix, 4) == b'efgh'
    assert lib.av_begin() == 0
    assert C.string_at(lib.av_bytes(a+0xCC, 8, 0), 8) == b'abcdefgh'
    assert lib.av_commit() == 0
    # Committed projection survives the model owner's generation ending.
    assert lib.av_free(a) == 0 and lib.av_alloc(4) == a
    assert lib.av_begin() == 0
    assert C.string_at(lib.av_bytes(a+0x40, 8, 0), 8) == before[0x40:0x48]
    assert C.string_at(lib.av_bytes(a+0xD0, 4, 0), 4) == b'efgh'
    assert lib.av_commit() == 0

    fault_cases = 0
    def fresh():
        lib.av_reset(); a = lib.av_alloc(4); assert lib.av_begin() == 0
        assert lib.av_bytes(a, SIZE, 0)
        return a

    for at in (0x14, 0x18, 0x1C):
        a = fresh(); p = lib.av_bytes(a+at, 4, 0); C.memset(p, 0xA5, 4)
        assert lib.av_commit() == -1 and lib.av_fault() == 3; fault_cases += 1
    a = fresh(); assert not lib.av_bytes(a+0x13, 2, 1) and lib.av_fault() == 3; fault_cases += 1
    a = fresh(); assert lib.av_free(a) == 0 and lib.av_alloc(4) == a
    assert lib.av_commit() == -1 and lib.av_fault() == 2; fault_cases += 1
    a = fresh(); lib.av_native_word(a, 123)
    assert lib.av_commit() == -1 and lib.av_fault() == 1; fault_cases += 1
    a = fresh(); C.memmove(lib.av_bytes(a+0x10, 4, 1), struct.pack('<I', 0xDEADBEEF), 4)
    assert lib.av_commit() == -1 and lib.av_fault() == 5; fault_cases += 1
    for bad in (1,2,3):
        lib.av_reset(); a=lib.av_alloc(4); lib.av_model(a); lib.av_bad_projection(bad)
        assert lib.av_begin() == 0 and not lib.av_bytes(a, SIZE, 0)
        assert lib.av_fault() == 4; fault_cases += 1
    for at, n, write in ((0x43, 2, 1), (0x40, 4, 0), (0x110, 8, 0)):
        lib.av_reset(); a=lib.av_alloc(4); lib.av_model(a); assert lib.av_begin() == 0
        p=lib.av_bytes(a+at,n,write)
        if write: assert not p
        else:
            C.memset(p, 0, n); assert lib.av_commit() == -1
        assert lib.av_fault() == 4; fault_cases += 1
    for at,n in ((SIZE-1,2),(0,0),(0,0xFFFFFFFF)):
        a=fresh(); assert not lib.av_bytes(a+at,n,0) and lib.av_fault()==1; fault_cases+=1
    assert lib.av_begin() == -1 and not lib.av_bytes(a,4,0)  # latched
    mode.banner(f'{operations} original pool mutation/lifetime operations')
    print(f'area01 actor view: PASS full pool bytes after every operation; original sparse movement; '
          f'model projections/crossing spans/retained bytes; {fault_cases} fail-stop cases')


if __name__ == '__main__':
    main()
