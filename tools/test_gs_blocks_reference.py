#!/usr/bin/env python3
"""The boot builder's GS register blocks (em_gs_blocks_original: 001D0F20's
banks A..G and header, 00101898, 00101630, 001008C0) against the original.

docs/RENDER_CONTEXT.md section 8.3, docs/LOAD_VEIL_PARTICLES.md section 3.

A. The ORIGINAL 001D0F20, executed from the user's pinned ELF over a route
   capture's RAM with the GS blocks cleared first, with 00101898 and every
   callee it reaches running (001002E0, 00100268, 00100610, 001006D8,
   00101630, 001008C0) and block_copy 00121870 running; only the calls that
   write no block are stubbed (0021B860, 001D2830, 001D25F0, 001DEDE0,
   0021B970, 0021BA80, 001CB5C0). It runs twice, over two different fills
   of its stack frame. The bytes that differ between the two runs must be
   exactly the stale bits of the read-back dwords of the four SDK fills
   (PRMODECONT, COLCLAMP and DTHE of each environment of bank A: 12 dwords,
   every bit but bit 0);
   every other byte of the 0x2220 must equal the native blocks.
B. Every route capture and the three startup-reference images: the native
   blocks equal the captured ones except (asserted, not skipped) the two
   XYOFFSET pairs of each bank-A environment, which equal the native value
   or step V's half-pixel form of it (Y + 8), bank B's FOGCOL data (the
   context's +0xB0 or 0), and the 12 read-back dwords (bit 0 equal).
C. The pieces alone, from a sample of arguments (halfword edges, negative
   sizes, ztest 0 / 1 / 2 / 3, both SDK mode dwords): 001008C0 and 00101630
   executed against the native functions, 0x60 / 0x80 bytes each, over a
   random block (the read-back words keep their other bits).

Default (~6 s): A once over one capture's RAM, B over every capture, C 40
cases each. EM_TEST_FULL=1: C 400 cases each.
"""
from __future__ import annotations

import ctypes as C
import hashlib
import random
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_effect_manager_reference as EMR  # noqa: E402
from reference_mode import FULL, banner, in_scope_beat, pick  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
ROUTE = DECOMP / 'build/s87/route'
REF = DECOMP / 'build/startup-reference'
OUT = ROOT / 'build/gs_blocks_reference'
ELF_SHA = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
BEATS = sorted(p.name for p in ROUTE.iterdir() if (p / 'eeMemory.bin').exists() and in_scope_beat(p.name))

BLOCKS, SIZE = 0x814220, 0x2220
CTX = 0x811CC0
D241010 = 0x241010
STACK = 0x01F80000
# Bank A: the environments at +0x20 and +0x1B0; XYOFFSET_1 at env + 0x40,
# XYOFFSET_2 at env + 0xC0 (block offsets); the three read-back words
# (PRMODECONT, COLCLAMP, DTHE) of each of the four SDK fills the two
# environments copy, at env + 0x60 / 0x70 / 0x80 and env + 0xE0 / 0xF0 / 0x100.
XYOFFSETS = (0x60, 0xE0, 0x1F0, 0x270)
STALE = tuple(env + o for env in (0x20, 0x1B0) for o in (0x60, 0x70, 0x80, 0xE0, 0xF0, 0x100))
FOGCOL = (0x360, 0x390)
STUBS = (0x21B860, 0x1D2830, 0x1D25F0, 0x1DEDE0, 0x21B970, 0x21BA80, 0x1CB5C0)

SHIM = r'''
#include "game/em_gs_blocks_original.h"
int shim_blocks(uint8_t *b, const uint8_t *d) { return em_gs_blocks_001D0F20(b, d); }
int shim_clear(uint8_t *c, int zt, int x, int y, int w, int h, unsigned r, unsigned g, unsigned b, unsigned a,
               unsigned z)
{ return em_gs_blocks_001008C0(c, zt, x, y, w, h, r, g, (uint8_t)b, (uint8_t)a, z); }
int shim_env2(uint8_t *e, int psm, int w, int h, int zt, int zp, const uint8_t *d)
{ return em_gs_blocks_00101630(e, psm, w, h, zt, zp, d); }
'''


def fail(msg):
    raise AssertionError(msg)


class Oracle(EMR.Oracle):
    """The shared interpreter plus SYNC (the SDK fills end with one; it has
    no effect on memory) and the 128-bit POR 00101898 zeroes its GIF tags
    with."""

    def execute(self, w, pc):
        op, fn = w >> 26, w & 63
        if op == 0 and fn == 0x0F:
            return
        if op == 0x1C and fn == 0x29 and (w >> 6 & 31) == 0x12:
            rs, rt, rd = w >> 21 & 31, w >> 16 & 31, w >> 11 & 31
            self.set128(rd, self.r[rs] | self.r[rt])
            return
        super().execute(w, pc)


def build_lib():
    OUT.mkdir(parents=True, exist_ok=True)
    src, lib_path = OUT / 'shim.c', OUT / 'gs_blocks.dylib'
    src.write_text(SHIM)
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                    '-I' + str(ROOT / 'src'), str(src), str(ROOT / 'src/game/em_gs_blocks_original.c'),
                    str(ROOT / 'src/game/em_load_veil_particles.c'), '-o', str(lib_path)], check=True)
    lib = C.CDLL(str(lib_path))
    lib.shim_blocks.argtypes = [C.c_char_p, C.c_char_p]
    lib.shim_clear.argtypes = [C.c_char_p] + [C.c_int] * 5 + [C.c_uint] * 5
    lib.shim_env2.argtypes = [C.c_char_p] + [C.c_int] * 5 + [C.c_char_p]
    return lib


def native_blocks(lib, d241010):
    buf = C.create_string_buffer(SIZE)
    if lib.shim_blocks(buf, d241010) != 0:
        fail('em_gs_blocks_001D0F20 refused')
    return buf.raw


def q64(b, off):
    return struct.unpack_from('<Q', b, off)[0]


def load_capture(path):
    ram = path.read_bytes()
    return ram


def captures():
    out = [(b, ROUTE / b / 'eeMemory.bin') for b in BEATS]
    out += [(n, REF / f'{n}_ee.bin') for n in ('opening', 'playable', 'handoff')]
    return [(n, p) for n, p in out if p.exists()]


# ------------------------------------------------------------ A. executed
def run_original(elf, ram_path, fill):
    ram = bytearray(ram_path.read_bytes())
    ram[BLOCKS:BLOCKS + SIZE] = bytes(SIZE)
    # 001D0F20's frame lies just below STACK; fill it (and a margin).
    ram[STACK - 0x1000:STACK] = bytes(fill)
    spad = bytearray(0x4000)
    e = Oracle(elf, ram, spad)
    for a in STUBS:
        e.stubs[a] = lambda ee: None
    e.run(0x1D0F20, stack=STACK)
    return bytes(e.ram[BLOCKS:BLOCKS + SIZE]), e.steps


def part_a(lib, elf, stats):
    name, path = captures()[0]
    d241010 = path.read_bytes()[D241010:D241010 + 8]
    native = native_blocks(lib, d241010)
    rng = random.Random(0x6B10)
    one, steps = run_original(elf, path, [rng.randrange(256) for _ in range(0x1000)])
    two, _ = run_original(elf, path, [rng.randrange(256) for _ in range(0x1000)])
    stale_bytes = {o + k for o in STALE for k in range(8)}
    for off in range(SIZE):
        if one[off] != two[off]:
            if off not in stale_bytes:
                fail(f'executed 001D0F20: byte +{off:#x} depends on the stack fill')
            if off in {o for o in STALE} and (one[off] ^ two[off]) & 1:
                fail(f'executed 001D0F20: bit 0 of +{off:#x} depends on the stack fill')
    stats['stale_seen'] = sum(one[o:o + 8] != two[o:o + 8] for o in STALE)
    if stats['stale_seen'] != len(STALE):
        fail('executed 001D0F20: a read-back word did not vary with the stack')
    for off in range(0, SIZE, 8):
        if off in STALE:
            if (q64(one, off) ^ q64(native, off)) & 1:
                fail(f'executed 001D0F20: bit 0 of +{off:#x} differs from the native blocks')
            continue
        if one[off:off + 8] != native[off:off + 8]:
            fail(f'executed 001D0F20 differs at +{off:#x}: original {q64(one, off):#018x} '
                 f'native {q64(native, off):#018x}')
    stats['executed'] = 1
    stats['steps'] = steps
    return native


# ------------------------------------------------------------ B. captures
def part_b(lib, stats):
    for name, path in captures():
        ram = path.read_bytes()
        native = native_blocks(lib, ram[D241010:D241010 + 8])
        got = ram[BLOCKS:BLOCKS + SIZE]
        fog = struct.unpack_from('<Q', ram, CTX + 0xB0)[0]
        for off in range(0, SIZE, 8):
            n, g = q64(native, off), q64(got, off)
            if off in XYOFFSETS:
                if g not in (n, n + (8 << 32)):
                    fail(f'{name}: XYOFFSET +{off:#x} {g:#x} is neither {n:#x} nor its half-pixel form')
                stats['xyoffset'] += 1
            elif off in FOGCOL:
                if g not in (fog & 0xFFFFFFFF, fog, 0):
                    fail(f'{name}: FOGCOL +{off:#x} {g:#x} is not the context +0xB0 ({fog:#x})')
            elif off in STALE:
                if (n ^ g) & 1:
                    fail(f'{name}: bit 0 of +{off:#x} differs')
            elif n != g:
                fail(f'{name}: +{off:#x} captured {g:#018x}, native {n:#018x}')
        stats['captures'] += 1


# ------------------------------------------------------------ C. the pieces
def part_c(lib, elf, stats):
    path = captures()[0][1]
    base_ram = path.read_bytes()
    rng = random.Random(0x1008)
    n = pick(400, 40)
    edges = (0, 1, 2, -1, 0x7FFF, -0x8000, 0x200, 0xE0, 0x100, 0x1FF)
    for case in range(n):
        ram = bytearray(base_ram)
        at = 0x1E00000
        junk = bytes(rng.randrange(256) for _ in range(0x80))
        ram[at:at + 0x80] = junk
        mode = rng.choice((b'\x01\x00\x02\x00\x01\x00\x1b\x00', b'\x00\x00\x00\x00\x00\x00\x00\x00',
                           bytes(rng.randrange(256) for _ in range(8))))
        ram[D241010:D241010 + 8] = mode
        pick_h = lambda: rng.choice(edges) if rng.random() < 0.5 else rng.randrange(-0x8000, 0x8000)
        if case % 2 == 0:
            args = [pick_h() for _ in range(5)]
            rgbaz = [rng.randrange(1 << 32), rng.randrange(1 << 32), rng.randrange(256), rng.randrange(256),
                     rng.randrange(1 << 32)]
            e = Oracle(elf, ram, bytearray(0x4000))
            sp = STACK - 0x100
            struct.pack_into('<QQQ', e.ram, sp - 0x100, 0, 0, 0)
            # 001008C0 reads its 9th..11th arguments at sp+0, +8, +0x10.
            struct.pack_into('<I', e.ram, sp, rgbaz[2])
            struct.pack_into('<I', e.ram, sp + 8, rgbaz[3])
            struct.pack_into('<I', e.ram, sp + 0x10, rgbaz[4])
            e.run(0x1008C0, args=(at, args[0] & 0xFFFFFFFF, args[1] & 0xFFFFFFFF, args[2] & 0xFFFFFFFF,
                                  args[3] & 0xFFFFFFFF, args[4] & 0xFFFFFFFF, rgbaz[0], rgbaz[1]), stack=sp)
            buf = C.create_string_buffer(junk, 0x80)
            if lib.shim_clear(buf, *args, rgbaz[0], rgbaz[1], rgbaz[2], rgbaz[3], rgbaz[4]) != 0:
                fail('001008C0 refused')
            if bytes(e.ram[at:at + 0x60]) != buf.raw[:0x60]:
                fail(f'001008C0 case {case} {args} differs')
            stats['clear'] += 1
        else:
            args = [pick_h() for _ in range(5)]
            e = Oracle(elf, ram, bytearray(0x4000))
            e.run(0x101630, args=(at, *(a & 0xFFFFFFFF for a in args)), stack=STACK)
            buf = C.create_string_buffer(junk, 0x80)
            if lib.shim_env2(buf, *args, mode) != 0:
                fail('00101630 refused')
            if bytes(e.ram[at:at + 0x80]) != buf.raw[:0x80]:
                fail(f'00101630 case {case} {args} mode {mode.hex()} differs')
            stats['env2'] += 1


def main():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    if hashlib.sha256(elf).hexdigest() != ELF_SHA:
        fail('boot ELF is not the pinned build')
    lib = build_lib()
    stats = {'captures': 0, 'xyoffset': 0, 'clear': 0, 'env2': 0}
    part_a(lib, elf, stats)
    part_b(lib, stats)
    part_c(lib, elf, stats)
    banner(f"{stats['captures']} captures", f"001008C0 {stats['clear']}", f"00101630 {stats['env2']}")
    print(f"gs blocks reference: PASS — the ORIGINAL 001D0F20 executed ({stats['steps']} instructions, "
          f"00101898 / 001006D8 / 00101630 / 001008C0 running) writes the native 0x2220 bytes, the "
          f"stack-fill dependence exactly the {stats['stale_seen']} read-back words' upper bits; "
          f"{stats['captures']} captures equal ({stats['xyoffset']} XYOFFSET words in the boot or step V form); "
          f"001008C0 {stats['clear']} and 00101630 {stats['env2']} cases executed equal")


if __name__ == '__main__':
    main()
