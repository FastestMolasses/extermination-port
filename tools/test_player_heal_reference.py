#!/usr/bin/env python3
"""em_player_0015C700 (em_player_stage_workers.c) against the ORIGINAL
0015C700 and its callee 0015C7C0 (both byte-matched C), which HEALING
002160B0 calls at the end of its count-up (docs/STATUS_PAGES.md).

The original runs in the EE interpreter over the user's ELF (its .data
holds the clip pairs D_00248A00..08 and D_002754C0..DA the translation
reads through its table worker) with the player record at a free address,
D_00810858 (health) and D_008106C8 (001B0070's word, run as original code)
set per case; 001749A0 is hooked and its (clip, flags, blend) recorded.
Every case compares the record's 0x320 bytes and the 001749A0 calls.

Cases: health {0, 35.0, the next float above 35, 36, 60, 100, -0.0},
+0x235 {0, 1, 2, 3, 0xFF}, the live clip +0x20C = every hurt and normal
clip of the pairs and three others, D_008106C8 bit 2 clear and set. The
default run takes every hurt clip with a sample of the rest.
"""
import ctypes as C
import itertools
import os
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from test_player_slide_reference import EE, RETURN, read_elf  # noqa: E402

RECORD, SIZE = 0x01A00000, 0x320
PAIRS = [(0x248A02, 0x248A00), (0x248A06, 0x248A04), (0x2754C2, 0x2754C0), (0x2754C6, 0x2754C4),
         (0x2754CA, 0x2754C8), (0x2754CE, 0x2754CC), (0x2754D2, 0x2754D0), (0x2754D6, 0x2754D4),
         (0x2754DA, 0x2754D8)]


def fbits(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def half(elf, address):
    return struct.unpack_from('<h', elf, address - 0x100000 + 0x300)[0]


class Actor(C.Structure):
    _fields_ = [('bytes', C.c_uint8 * SIZE), ('link_owner', C.c_void_p), ('link_prev', C.c_void_p),
                ('link_flags', C.c_uint8), ('link_type', C.c_uint8)]


def library():
    out = ROOT / 'build/player_heal_reference'
    out.mkdir(parents=True, exist_ok=True)
    lib = out / ('heal.dylib' if sys.platform == 'darwin' else 'heal.so')
    flags = ['-dynamiclib', '-undefined', 'dynamic_lookup'] if sys.platform == 'darwin' else \
        ['-shared', '-Wl,--unresolved-symbols=ignore-all']
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC', '-ffp-contract=off',
                    *flags, '-Isrc', 'src/game/em_player_stage_workers.c', '-o', str(lib)], cwd=ROOT, check=True)
    return C.CDLL(str(lib))


def main():
    elf = read_elf()
    n = library()
    Table = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.POINTER(C.c_int16))
    Request = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(Actor), C.c_int, C.c_int, C.c_float)
    n.em_player_0015C700.argtypes = [C.POINTER(Actor), C.c_uint32, C.c_uint32, Table, C.c_void_p,
                                     Request, C.c_void_p]
    table = Table(lambda _, address, out: (out.__setitem__(0, half(elf, address)), 0)[1])
    clips = sorted({half(elf, a) for pair in PAIRS for a in pair} | {half(elf, 0x248A08), 0, -1, 0x7FFF})
    hurt = {half(elf, h) for h, _ in PAIRS}
    healths = [0.0, 35.0, struct.unpack('<f', struct.pack('<I', fbits(35.0) + 1))[0], 36.0, 60.0, 100.0, -0.0]
    cases = list(itertools.product(healths, (0, 1, 2, 3, 0xFF), clips, (0, 4, 0xFFFFFFFF)))
    if os.environ.get('EM_TEST_FULL') != '1':
        cases = [c for c in cases if c[2] in hurt and c[1] in (1, 3)] + cases[::97]
    requests_total = 0
    for health, low, clip, c8 in cases:
        record = bytearray(os.urandom(SIZE))
        record[0x235] = low
        struct.pack_into('<h', record, 0x20C, clip)
        ee = EE(elf)
        ee.write(RECORD, bytes(record))
        ee.save(0x810858, fbits(health), 4)
        ee.save(0x8106C8, c8, 4)
        calls = []

        def request(e):
            calls.append((e.r[5] & 0xFFFFFFFF, e.r[6] & 0xFFFFFFFF, e.f[12] & 0xFFFFFFFF))
            e.r[2] = 0
        ee.hooks[0x1749A0] = request
        ee.r[4], ee.r[31] = RECORD, RETURN
        ee.run(0x15C700)
        expected = ee.read(RECORD, SIZE)
        actor = Actor()
        C.memmove(actor.bytes, bytes(record), SIZE)
        got = []
        req = Request(lambda _, a, c, f, b: (got.append((c & 0xFFFFFFFF, f & 0xFFFFFFFF, fbits(b))), 0)[1])
        rc = n.em_player_0015C700(C.byref(actor), fbits(health), c8, table, None, req, None)
        assert rc == 0, (health, low, clip, c8)
        assert bytes(actor.bytes) == expected, (health, low, clip, c8, 'record differs')
        assert got == calls, (health, low, clip, c8, got, calls)
        requests_total += len(calls)
    print(f'mode {"full" if os.environ.get("EM_TEST_FULL") == "1" else "quick"}: {len(cases)} cases, '
          f'{requests_total} 001749A0 requests equal to the original\'s')
    print('player heal reference: 0015C700 / 0015C7C0 match the original instructions PASS')


if __name__ == '__main__':
    main()
