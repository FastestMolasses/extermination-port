#!/usr/bin/env python3
"""The status pages' GS packet builders against the original, byte for byte.

docs/STATUS_FIELD.md section 5 (step 2), src/game/em_ui_packet_original.c.
Inputs (user-local, never copied here, nothing of them embedded): the
pinned ELF and the decomp's fork captures of the status pages
(../Extermination/build/status_field_capture/<beat>/f<T>/, decomp
build/status_field_capture/capture.py; STATUS_FIELD.md section 4).

A. The original instructions. The EE interpreter of
   test_player_slide_reference.py (the glyph tests' MMI subset) executes
   00207D00, 00207D90, 00207E40, 00207F80, 00208040 (with 001281C0),
   001CC8A0 (with 001CCE80), 001CBC20 and 001CC3B0 over the 01_battery f0300
   EE image, the slot cursor pointed into a filled scratch area. The native
   builder runs over a copy of the same bytes: every byte of the area (the
   fill the original leaves included) and the cursor must be equal, and the
   original may store only into the area, the cursor word and its stack.
   Argument sets: edges and random words; fonts and offsets of the capture
   (*D_0028A490 / *D_0028A494). Then the native fail-stop: a missing cursor
   word, a packet outside memory or over its cursor, a glyph outside the
   font, a missing worker, and the latch, each writing nothing.
B. The captured chains. Each frame's slot-1 chain (the status page's whole
   2D layer: the 0020A7A0 background, the page sprites, the text uploads
   and flushes) is decoded into builder calls, packet by packet (an
   unknown packet fails), and rebuilt by the native builders into the same
   addresses twice, over two fills: the bytes both rebuilds write must equal
   the captured bytes, every cursor must land on the next captured packet,
   and the bytes neither writes are listed by class (the DMA tags' unsent
   words, the REGLIST pads, the tall uploads' unfilled texels). The chain's
   open and close (001D1AE0's 001D1F20 / 001D2040 / 001D1FF0, step V's
   REF and 001D1F80(1, 0, 7); em_load_veil_particles' translations, tested
   by their own oracles) are checked against their formulas.
   Then the captured list with the rebuilt chain in place (its unwritten
   bytes left as the fill) runs through the CPU GS model as
   test_status_field_reference does: the field must equal the displayed
   field, and local memory may differ from the original's only in the
   glyph strip's pages (the unfilled texels the passes never sample).

Quick (default, about 10 s): A sampled, B at every frame, its raster step at
01_battery f0300. EM_TEST_FULL=1: every A case and the raster step at every
frame.
"""
from __future__ import annotations

import ctypes as C
import hashlib
import json
import random
import struct
import subprocess
import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from reference_mode import FULL, MODE, select  # noqa: E402
from test_player_slide_reference import s32, sx32, RETURN  # noqa: E402
from test_message_draw_reference import DrawEE  # noqa: E402
import test_status_field_reference as sf  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
CAP = sf.CAP
OUT = ROOT / 'build/ui_packet_reference'
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
SOURCES = ['tests/ui_packet_reference_bridge.c', 'src/game/em_ui_packet_original.c',
           'src/game/em_message_glyph_original.c', 'src/game/em_ui_font_raw.c', 'src/game/em_item_sdk_math.c',
           'src/game/em_interaction_scan.c']

B_7D00, B_7D90, B_7E40, B_7F80, B_8040, B_CC8A0, B_CBC20, B_CC3B0, B_CBA50, B_1D66A0 = range(10)
ENTRY = {B_7D00: 0x207D00, B_7D90: 0x207D90, B_7E40: 0x207E40, B_7F80: 0x207F80, B_8040: 0x208040,
         B_CC8A0: 0x1CC8A0, B_CBC20: 0x1CBC20, B_CC3B0: 0x1CC3B0, B_CBA50: 0x1CBA50, B_1D66A0: 0x1D66A0}
NAME = {B_7D00: '00207D00', B_7D90: '00207D90', B_7E40: '00207E40', B_7F80: '00207F80', B_8040: '00208040',
        B_CC8A0: '001CC8A0', B_CBC20: '001CBC20', B_CC3B0: '001CC3B0', B_CBA50: '001CBA50',
        B_1D66A0: '001D66A0'}
# 0020AC70's fans at rest (every ring slot zero magnitude): pos {x + 1792,
# y + 1824, 28, 28} for the base (x, y) its caller passes, the colour 16
# (float_to_int(16 (1 + 0))). The bases: the ITEM root's 0020F2A0 (248, 208)
# and SPR4's selector 002125B0 (212, 170: its stores of 0x43540000 and
# 0x432A0000 to D_700038A0 before the call; STATUS_FIELD.md 3h). By the
# centre vertex's fixed-point XY (16 x the pixel).
FAN_REST = {(16 * int(x), 16 * int(y)): tuple(struct.unpack('<I', struct.pack('<f', v))[0] for v in (x, y, 28.0, 28.0))
            for x, y in ((2040.0, 2032.0), (2004.0, 1994.0))}
FAN_REST_CENTRE = (16 * 2040, 16 * 2032)
FAN_REST_A1 = FAN_REST[FAN_REST_CENTRE]
FAN_REST_A2, FAN_A3 = (16, 16, 16, 0), (0, 0, 0, 0)   # A3: D_00273580, sixteen zero bytes of the ELF
EXECUTED = ((0x207D00, 0x84), (0x207D90, 0xAC), (0x207E40, 0x134), (0x207F80, 0xB8), (0x208040, 0x160),
            (0x1CC8A0, 0x254), (0x1CCE80, 0xEC), (0x1CBC20, 0x1F0), (0x1CC3B0, 0x4E8), (0x1281C0, 0x8C),
            (0x1CBA50, 0x1C4), (0x1232E0, 0x138), (0x1CCB00, 0x8))
FLOAT_TO_INT = 0x1281C0
CONTEXT_PTR, GS_BLOCKS_PTR = 0x275670, 0x275674
NIBBLES, FONT_TALL_PTR, FONT_SMALL_PTR = 0x26E350, 0x28A490, 0x28A494
FONT_TALL_SIZE = FONT_SMALL_SIZE = 0x3000     # the two resources' spacing in the captures
HEAD, SPRITE, STRIP, SMALL = 0x2511C0, 0x2510C0, 0x251140, 0x250FC0
PACKET, AREA = 0x1F40000, 0x400               # scratch packet area of the oracle's image
AREA_TEXT, FLUSH_AT = 0x6000, 0x5000           # 001CBA50's area; a flush cursor other than slot 1's
VEC, STYLE, TEXT = 0x1F48000, 0x1F48100, 0x1F48200
TEXT_AREA = 0x100
FONT_EXPORT = ROOT / 'assets/font.emfn'        # the user's own font export (tools/export_font.py)
UFR_TALL, UFR_SMALL = 409 * 30, 256 * 32       # em_ui_font_raw's resource sizes
CH1_BASE, CH1_STRIDE = 0x0028F700 + 0xC9000, 0x95760   # 001D1AE0's +0x14 buffers
FRAMES = [f for f in sf.FRAMES if f not in sf.RASTER_ONLY]   # its decode reads fans at rest only
RASTER_QUICK = [('01_battery', 300), ('slot12_status_root_pad8080_bg3467', 40)]
STRIP_PAGES = (0x1B00 * 256, 0x1B00 * 256 + 0x8000)    # GS block 0x1B00, PSMT4 512 x 128 texels

FAILS: list[str] = []


def fail(msg: str) -> None:
    FAILS.append(msg)
    print(f'  FAIL {msg}')


TO_INT = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.POINTER(C.c_int32))


def float_to_int(bits: int) -> int | None:
    """001281C0 over a finite binary32 inside int32: truncation toward zero
    (the fans' coordinates; None outside that domain)."""
    v = struct.unpack('<f', struct.pack('<I', bits & 0xFFFFFFFF))[0]
    if v != v or not -2.0 ** 31 <= v < 2.0 ** 31:
        return None
    return int(v)


def _fan_to_int(ctx, bits, out):
    v = float_to_int(bits)
    if v is None:
        return 0
    out[0] = v
    return 1


FAN_TO_INT = TO_INT(_fan_to_int)


def build() -> C.CDLL:
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / ('libuipacket.dylib' if sys.platform == 'darwin' else 'libuipacket.so')
    cmd = ['cc', '-std=c11', '-O2', '-ffp-contract=off', '-Wall', '-Wextra', '-Werror', '-fPIC',
           '-dynamiclib' if sys.platform == 'darwin' else '-shared', '-Isrc', *SOURCES, '-o', str(lib)]
    digest = hashlib.sha256(' '.join(cmd).encode())
    for path in SOURCES + ['src/game/em_ui_packet_original.h', 'src/game/em_message_glyph_original.h',
                           'src/game/em_message_draw_original.h', 'src/game/em_ui_font_raw.h',
                           'src/game/em_ee_float.h', 'src/game/em_item_sdk_math.h']:
        digest.update(path.encode() + b'\0' + (ROOT / path).read_bytes())
    stamp = lib.with_suffix('.sha256')
    if not lib.exists() or not stamp.exists() or stamp.read_text() != digest.hexdigest():
        subprocess.run(cmd, cwd=ROOT, check=True)
        stamp.write_text(digest.hexdigest())
    n = C.CDLL(str(lib))
    P = C.POINTER
    n.uipb_call.argtypes = [P(C.c_uint32), P(C.c_uint32), P(C.c_void_p), C.c_uint32, C.c_uint32, C.c_uint32,
                            C.c_void_p, C.c_void_p, C.c_uint32, C.c_void_p, C.c_uint32, TO_INT, C.c_int,
                            P(C.c_int64), C.c_char_p, C.c_uint32]
    n.uipb_call.restype = C.c_int
    n.uipb_latch.argtypes = [C.c_void_p, C.c_uint32, C.c_uint32, C.c_void_p]
    n.uipb_latch.restype = C.c_int
    n.uipb_fonts.argtypes = [C.c_char_p, C.c_void_p, C.c_void_p, C.c_void_p, C.c_char_p, C.c_uint32]
    n.uipb_fonts.restype = C.c_int
    n.uipb_fan.argtypes = [P(C.c_uint32), P(C.c_uint32), P(C.c_void_p), C.c_uint32, C.c_uint32, TO_INT, C.c_int32,
                           P(C.c_uint32), P(C.c_uint32), P(C.c_uint32), P(C.c_uint32), C.c_char_p, C.c_uint32]
    n.uipb_fan.restype = C.c_int
    n.uipb_fan_sdk.argtypes = [P(C.c_uint32), P(C.c_uint32), P(C.c_void_p), C.c_uint32, C.c_uint32, TO_INT,
                               C.c_int32, P(C.c_uint32), P(C.c_uint32), P(C.c_uint32), C.c_uint32, C.c_char_p,
                               C.c_uint32]
    n.uipb_fan_sdk.restype = C.c_int
    return n


class Native:
    """The native builders over (address, bytearray) regions."""

    def __init__(self, lib, ram: bytes, context: int, blocks: int, to_int=None):
        self.lib, self.context, self.blocks = lib, context, blocks
        nib = ram[NIBBLES:NIBBLES + 32]
        self.nib = C.create_string_buffer(nib, 32)
        tall = struct.unpack_from('<I', ram, FONT_TALL_PTR)[0] & 0x1FFFFFF
        small = struct.unpack_from('<I', ram, FONT_SMALL_PTR)[0] & 0x1FFFFFF
        self.tall = C.create_string_buffer(bytes(ram[tall:tall + FONT_TALL_SIZE]), FONT_TALL_SIZE)
        self.small = C.create_string_buffer(bytes(ram[small:small + FONT_SMALL_SIZE]), FONT_SMALL_SIZE)
        self.to_int = TO_INT(to_int if to_int else (lambda ctx, b, out: 0))
        self.fault = C.create_string_buffer(128)

    def call(self, which: int, args, regions, fonts=True, nibbles=True) -> tuple[int, str]:
        keep = [(C.c_char * len(b)).from_buffer(b) for _, b in regions]
        addr = (C.c_uint32 * len(regions))(*[a for a, _ in regions])
        size = (C.c_uint32 * len(regions))(*[len(b) for _, b in regions])
        ptrs = (C.c_void_p * len(regions))(*[C.addressof(k) for k in keep])
        if which == B_1D66A0:
            # (slot, a1 x4, a2 x4, a3 x4, f12 bits): the live workers' sin / cos (em_item_sdk_math)
            vec = [(C.c_uint32 * 4)(*args[1 + 4 * i:5 + 4 * i]) for i in range(3)]
            rc = self.lib.uipb_fan_sdk(addr, size, ptrs, len(regions), self.context, FAN_TO_INT, args[0], *vec,
                                       args[13], self.fault, 128)
            del keep, ptrs
            return rc, self.fault.value.decode()
        a = (C.c_int64 * 16)(*[int(v) - (1 << 64) if int(v) >= 1 << 63 else int(v) for v in args])
        rc = self.lib.uipb_call(addr, size, ptrs, len(regions), self.context, self.blocks,
                                self.nib if nibbles else None, self.tall if fonts else None, FONT_TALL_SIZE,
                                self.small if fonts else None, FONT_SMALL_SIZE, self.to_int, which, a, self.fault,
                                128)
        del keep, ptrs
        return rc, self.fault.value.decode()


# ---------------------------------------------------------------- A: original
class Original:
    def __init__(self, elf: bytes, ram: bytes):
        self.ee = ee = DrawEE(elf, ram)
        self.context = ee.load(CONTEXT_PTR)
        self.blocks = ee.load(GS_BLOCKS_PTR)
        self.stores: set[int] = set()
        save = ee.save

        def guarded(address, value, size=4):
            address &= 0xFFFFFFFF
            if not 0x7F000000 <= address < 0x7F100000:
                self.stores.update(range(address, address + size))
            save(address, value, size)
        ee.save = guarded

    def to_int(self, bits: int) -> int:
        ee = self.ee
        saved = (list(ee.r), list(ee.rh), list(ee.f))
        ee.f[12] = bits & 0xFFFFFFFF
        ee.r[29] = 0x7F0F0000
        ee.r[31] = RETURN
        ee.run(FLOAT_TO_INT)
        value = s32(ee.r[2])
        ee.r, ee.rh, ee.f = saved
        return value

    def run(self, which: int, regs: list[int]) -> None:
        ee = self.ee
        for i in range(32):
            ee.r[i], ee.rh[i] = 0, 0
        ee.r[28], ee.r[29], ee.r[31] = 0x27D370, 0x7F0F0000, RETURN
        for i, v in enumerate(regs):
            ee.r[4 + i] = v & 0xFFFFFFFFFFFFFFFF
        ee.run(ENTRY[which])


def u64(v: int) -> int:
    return v & 0xFFFFFFFFFFFFFFFF


def rnd32(r: random.Random) -> int:
    return s32(r.getrandbits(32))


def style_bytes(r: random.Random) -> bytes:
    return bytes(r.getrandbits(8) for _ in range(8))


def oracle_cases(r: random.Random) -> list[tuple]:
    """(which, slot, cursor offset, args, style bytes or None, edge): the
    edge cases (every one runs in quick mode too), then random words."""
    coords = [0, 1, -1, 0x7000, 0x7900, 0x8AE0, 0xFFFF, 0x10000, 0x7FFFFFFF, -0x80000000]
    cases = []

    def add(edge, case):
        cases.append((*case, edge))
    for slot in (1, 0, 2, 3):
        for mode in (0, 1, 2, 3, 4, -1, 0x7FFFFFFF):
            add(True, (B_7D00, slot, 0, (mode,), None))
    for c in coords:
        add(True, (B_7D90, 1, 0, (c, 0x790, 0x8FF, c), None))
        add(True, (B_7E40, 1, 0, (c, 0x7B30, 8, 0x10, 0x80808080, 0x2000AC8D61322000 + (c & 0xFF)), None))
        add(True, (B_7F80, 1, 0, (c, 0x7B30, c ^ 0x100, 0x7F00, 0x40808080), None))
    for tw in range(16):
        for th in (0, 7, 15):
            add(True, (B_7E40, 1, 0x10, (0x7100, 0x7900, 0x100, 0x80, 0x40808080,
                                            (tw << 26) | (th << 30) | 0x1322000), None))
    for _ in range(300):
        slot = r.choice((1, 1, 1, 0, 2, 3))
        off = r.randrange(0, 0x40, 16)
        add(False, (B_7D90, slot, off, tuple(rnd32(r) for _ in range(4)), None))
        add(False, (B_7E40, slot, off, (*(rnd32(r) for _ in range(4)), r.getrandbits(32), r.getrandbits(64)), None))
        add(False, (B_7F80, slot, off, (*(rnd32(r) for _ in range(4)), r.getrandbits(32)), None))
    floats = [0.0, -0.0, 0.5, -0.5, 1.0, 1792.0, 1936.5, -1.5, 32767.9, 65535.0, 2.0 ** 31, -2.0 ** 31, 1e30, -1e30]
    for _ in range(150):
        vs = [struct.unpack('<I', struct.pack('<f', r.choice(floats) if r.random() < 0.3 else
                                                  r.uniform(-70000.0, 70000.0)))[0] for _ in range(6)]
        add(False, (B_8040, r.choice((1, 1, 3)), r.randrange(0, 0x40, 16), (*vs, r.getrandbits(64)), None))
    for mode in (1, 0):
        for g in (0, 1, 0x21, 0x41, 0x5A, 0x89, 0xFF):
            add(True, (B_CC8A0, 1, 0, (mode, 0x10 * g, 0x14 * (g & 3), g * (30 if mode else 32)), None))
    for mode in (2, -1, 0x100):
        add(True, (B_CC8A0, 1, 0, (mode, 5, 5, 30), None))
    for _ in range(120):
        mode = r.choice((0, 1))
        limit = (FONT_TALL_SIZE - 30) if mode else (FONT_SMALL_SIZE - 32)
        add(False, (B_CC8A0, 1, r.randrange(0, 0x40, 16), (mode, rnd32(r), rnd32(r), r.randrange(0, limit)), None))
    for st in (None, bytes([0x60, 0x60, 0x60, 0, 0x80, 0, 0, 0]), bytes([0x10, 0x20, 0x30, 0x40, 0x7F, 1, 0, 5]),
               bytes([0xFF] * 7 + [0x10]), bytes([0, 0, 0, 0x84, 0, 0, 0, 0x0F])):
        add(True, (B_CBC20, 1, 0, (0x7C2, 0x7E8, 0x60, 0x14, 0x61, 0x10), st))
        add(True, (B_CC3B0, 1, 0, (0x700, 0x790, 0x55, 0x14, 0x54, 0x14), st))
    for _ in range(200):
        st = None if r.random() < 0.2 else style_bytes(r)
        add(False, (B_CBC20, r.choice((1, 1, 2)), r.randrange(0, 0x40, 16), tuple(rnd32(r) for _ in range(6)), st))
        add(False, (B_CC3B0, r.choice((1, 1, 2)), r.randrange(0, 0x40, 16), tuple(rnd32(r) for _ in range(6)), st))
    printable = bytes(range(0x20, 0x7F))
    texts = [b'', b'0', b'$', b'A$b', b'\x01\x1f ', b'\x80\xff\xa0\x87', b'00000', b'BATT. 6',
             bytes(printable[i % len(printable)] for i in range(31)), bytes(printable[:32]),
             bytes(printable[:33]), bytes(printable[i % 95] for i in range(64)),
             bytes(printable[i % 95] for i in range(65)), bytes(printable[i % 95] for i in range(70))]
    for i, t in enumerate(texts):
        st = None if i % 3 == 0 else bytes([0x60, 0x60, 0x60, 0, 0x80, 0, 0, i & 0x1F])
        add(True, (B_CBA50, 1, 0, (2012, 2066, 16, 16, t), st))
    for slot in (0, 2):
        add(True, (B_CBA50, slot, 0, (0x7C0, 0x7F0, 12, 12, bytes(printable[:40])), None))
    for adv, h in ((0, 0), (-3, -5), (0x7FFFFFFF, 1), (12, 16), (-0x80000000, 0x7FFFFFFF)):
        add(True, (B_CBA50, 1, 0, (-1, 0x7FFFFFFF, adv, h, bytes(printable[:34])), style_bytes(r)))
    for _ in range(60):
        t = bytes(r.randrange(1, 256) for _ in range(r.randrange(0, 71)))
        add(False, (B_CBA50, r.choice((1, 1, 1, 0, 2)), r.randrange(0, 0x40, 16),
                    (rnd32(r), rnd32(r), r.choice((12, 16, rnd32(r))), r.choice((12, 16, rnd32(r))), t),
                    None if r.random() < 0.3 else style_bytes(r)))
    return cases


def run_cba50(o: 'Original', n: 'Native', case) -> str | None:
    """001CBA50: its uploads on slot 1's cursor, its flushes on the slot's
    (the same cursor for slot 1, else one at FLUSH_AT); the whole area, both
    cursors and the stores compared."""
    _, slot, off, (x, y, adv, h, text), st = case[:5]
    ee = o.ee
    c1 = (o.context + 0x14) & 0xFFFFFFFF
    cs = (o.context + 0x10 + 4 * slot) & 0xFFFFFFFF
    saved = {c: ee.load(c) for c in (c1, cs)}
    fill = bytes([0xA5]) * AREA_TEXT
    ee.write(PACKET, fill)
    ee.save(c1, PACKET + off)
    if cs != c1:
        ee.save(cs, PACKET + FLUSH_AT)
    ee.write(TEXT, text + b'\0' + bytes(TEXT_AREA - len(text) - 1))
    if st is not None:
        ee.write(STYLE, st)
    o.stores.clear()
    o.run(B_CBA50, [sx32(slot), sx32(x), sx32(y), sx32(adv), sx32(h), TEXT, STYLE if st is not None else 0])
    want = ee.read(PACKET, AREA_TEXT)
    want_c = {c: ee.load(c) for c in (c1, cs)}
    stray = sorted(a for a in o.stores if not (PACKET <= a < PACKET + AREA_TEXT or c1 <= a < c1 + 4 or
                                               cs <= a < cs + 4))
    for c, v in saved.items():
        ee.save(c, v)
    area = bytearray(fill)
    b1 = bytearray(struct.pack('<I', PACKET + off))
    regions = [(c1, b1), (PACKET, area), (TEXT, bytearray(text + b'\0' + bytes(TEXT_AREA - len(text) - 1)))]
    bs = b1
    if cs != c1:
        bs = bytearray(struct.pack('<I', PACKET + FLUSH_AT))
        regions.insert(1, (cs, bs))
    rc, why = n.call(B_CBA50, [slot, x, y, adv, h, TEXT, *style_args(st)], regions)
    if stray:
        return f'the original stores outside the packets: {[hex(a) for a in stray[:4]]}'
    if rc != 0:
        return f'native refused: {why}'
    if bytes(area) != want:
        i = next(i for i in range(AREA_TEXT) if area[i] != want[i])
        return f'byte {PACKET + i:#x}: native {area[i]:#04x}, original {want[i]:#04x}'
    if struct.unpack('<I', b1)[0] != want_c[c1] or struct.unpack('<I', bs)[0] != want_c[cs]:
        return f'cursors {struct.unpack("<I", b1)[0]:#x} / {struct.unpack("<I", bs)[0]:#x}, original ' \
               f'{want_c[c1]:#x} / {want_c[cs]:#x}'
    return None


def style_args(st: bytes | None) -> list[int]:
    if st is None:
        return [0, 0, 0, 0, 0]
    return [1, s32(struct.unpack_from('<I', st, 0)[0]), st[4], st[5], st[7]]


def run_oracle(o: Original, n: Native, case) -> str | None:
    which, slot, off, args, st = case[:5]
    if which == B_CBA50:
        return run_cba50(o, n, case)
    ee = o.ee
    if which == B_CC8A0:
        slot = 1
    cell = (o.context + 0x10 + 4 * slot) & 0xFFFFFFFF
    saved_cell = ee.load(cell)
    fill = bytes([0xA5]) * AREA
    ee.write(PACKET, fill)
    ee.save(cell, PACKET + off)
    o.stores.clear()
    if which == B_7D00:
        regs, native = [slot, args[0]], [slot, args[0]]
    elif which in (B_7D90, B_7F80):
        regs = [slot, *args]
        native = [slot, *args]
    elif which == B_7E40:
        regs = [slot, *args]
        native = [slot, *args]
    elif which == B_8040:
        for i in range(3):
            ee.save(VEC + 0x10 * i, args[2 * i])
            ee.save(VEC + 0x10 * i + 8, args[2 * i + 1])
        regs = [slot, VEC, VEC + 0x10, VEC + 0x20, args[6]]
        native = [slot, *args[:6], args[6] & 0xFFFFFFFF]
    elif which == B_CC8A0:
        regs, native = list(args), list(args)
    else:
        if st is not None:
            ee.write(STYLE, st)
        regs = [slot, *args, STYLE if st is not None else 0]
        native = [slot, *args, *style_args(st)]
    regs = [sx32(v) if i != 6 or which not in (B_7E40,) else u64(v) for i, v in enumerate(regs)]
    if which == B_8040:
        regs[4] = u64(args[6])
    o.run(which, regs)
    want_area = ee.read(PACKET, AREA)
    want_cell = ee.load(cell)
    stray = sorted(a for a in o.stores if not (PACKET <= a < PACKET + AREA or cell <= a < cell + 4 or
                                               (which == B_8040 and VEC <= a < VEC + 0x30)))
    ee.save(cell, saved_cell)
    area, cbuf = bytearray(fill), bytearray(struct.pack('<I', PACKET + off))
    rc, why = n.call(which, native, [(cell, cbuf), (PACKET, area)])
    if stray:
        return f'the original stores outside the packet: {[hex(a) for a in stray[:4]]}'
    if rc != 0:
        return f'native refused: {why}'
    if bytes(area) != want_area:
        i = next(i for i in range(AREA) if area[i] != want_area[i])
        return f'byte +{i - off:#x} of the packet: native {area[i]:#04x}, original {want_area[i]:#04x}'
    if struct.unpack('<I', cbuf)[0] != want_cell:
        return f'cursor {struct.unpack("<I", cbuf)[0]:#x}, original {want_cell:#x}'
    return None


def part_a(elf: bytes, ram: bytes, lib) -> dict:
    o = Original(elf, ram)
    n = Native(lib, ram, o.context, o.blocks)

    def to_int(ctx, bits, out):
        out[0] = o.to_int(bits)
        return 1
    n.to_int = TO_INT(to_int)
    cases = oracle_cases(random.Random(0x207E40))
    cases = select(cases, 600, 0x207D00, axes=(lambda c: c[0], lambda c: c[1]),
                   keep=lambda i, c: c[5])
    counts: dict[str, int] = {}
    for case in cases:
        bad = run_oracle(o, n, case)
        counts[NAME[case[0]]] = counts.get(NAME[case[0]], 0) + 1
        if bad:
            fail(f'A {NAME[case[0]]} slot {case[1]} args {[hex(a) if isinstance(a, int) else a[:12] for a in case[3]]}: '
                 f'{bad}')
            if len(FAILS) > 20:
                break
    print(f'  A: {len(cases)} cases equal to the original, every byte and the cursor: {counts}')
    return {'cases': counts, 'fail_stop': fail_stop(n, o.context)}


def fail_stop(n: Native, context: int) -> int:
    """Native refusals: each returns -1 and writes nothing."""
    checks = 0
    cell = context + 0x14
    fill = bytes([0xA5]) * AREA

    def expect(which, args, regions, label, **kw):
        nonlocal checks
        before = [bytes(b) for _, b in regions]
        rc, why = n.call(which, args, regions, **kw)
        checks += 1
        if rc != -1 or not why:
            fail(f'fail-stop {label}: returned {rc} ({why!r})')
        if [bytes(b) for _, b in regions] != before:
            fail(f'fail-stop {label}: bytes written')
    area = bytearray(fill)
    good = lambda: [(cell, bytearray(struct.pack('<I', PACKET))), (PACKET, area)]
    for which, args in ((B_7D00, [1, 0]), (B_7D90, [1, 0, 0, 0, 0]), (B_7E40, [1, 0, 0, 1, 1, 0, 0]),
                        (B_7F80, [1, 0, 0, 1, 1, 0]), (B_CC8A0, [1, 0, 0, 0]), (B_CBC20, [1] + [0] * 11),
                        (B_CC3B0, [1] + [0] * 11)):
        expect(which, args, [(PACKET, area)], f'{NAME[which]} without its cursor word')
        expect(which, args, [(cell, bytearray(struct.pack('<I', PACKET + AREA - 8))), (PACKET, area)],
               f'{NAME[which]} packet past memory')
        expect(which, args, [(cell, bytearray(struct.pack('<I', cell - 8))), (cell - 8, bytearray(0x400))],
               f'{NAME[which]} packet over its cursor')
    worker = n.to_int
    n.to_int = TO_INT(lambda ctx, b, out: 0)
    expect(B_8040, [1, 0, 0, 0, 0, 0, 0, 0], good(), '00208040 failing 001281C0')
    n.to_int = worker
    expect(B_CC8A0, [1, 0, 0, FONT_TALL_SIZE - 29], good(), '001CC8A0 glyph past the tall font')
    expect(B_CC8A0, [0, 0, 0, FONT_SMALL_SIZE - 31], good(), '001CC8A0 glyph past the small font')
    expect(B_CC8A0, [1, 0, 0, -1], good(), '001CC8A0 negative glyph offset')
    expect(B_CC8A0, [1, 0, 0, 0], good(), '001CC8A0 without fonts', fonts=False)
    expect(B_CC8A0, [0, 0, 0, 0], good(), '001CC8A0 without D_0026E350', nibbles=False)
    # 001CBA50: the whole run is checked before its first store
    text = lambda t: (TEXT, bytearray(t))
    expect(B_CBA50, [1, 0, 0, 16, 16, TEXT, 0, 0, 0, 0, 0], good() + [text(b'AB')], '001CBA50 text without its NUL')
    expect(B_CBA50, [1, 0, 0, 16, 16, TEXT, 0, 0, 0, 0, 0],
           [(cell, bytearray(struct.pack('<I', PACKET + AREA - 0x200))), (PACKET, area), text(b'AB\0')],
           '001CBA50 run past memory (its first upload fits)')
    expect(B_CBA50, [1, 0, 0, 16, 16, TEXT, 0, 0, 0, 0, 0], good() + [text(b'A\0')], '001CBA50 without fonts',
           fonts=False)
    expect(B_CBA50, [1, 0, 0, 16, 16, TEXT, 0, 0, 0, 0, 0], good() + [text(b'A\0')],
           '001CBA50 without D_0026E350', nibbles=False)
    expect(B_CBA50, [1, 0, 0, 16, 16, 0, 0, 0, 0, 0, 0], good(), '001CBA50 without its text')
    expect(B_CBA50, [0, 0, 0, 16, 16, TEXT, 0, 0, 0, 0, 0],
           [(cell, bytearray(struct.pack('<I', PACKET))), (PACKET, area), text(b'A\0')],
           '001CBA50 flush cursor (slot 0) missing, the uploads\' present')
    cbuf, area = bytearray(struct.pack('<I', PACKET)), bytearray(fill)
    a = (C.c_char * AREA).from_buffer(area)
    c4 = (C.c_char * 4).from_buffer(cbuf)
    rc = n.lib.uipb_latch(C.addressof(a), PACKET, context, C.addressof(c4))
    del a, c4
    checks += 1
    if rc != 1 or bytes(area) != fill:
        fail(f'fail-stop latch: {rc}, the later call wrote {sum(x != 0xA5 for x in area)} bytes')
    print(f'  A: {checks} native fail-stop checks')
    return checks


# ---------------------------------------------------------------- B: captures
def q(buf: bytes, at: int) -> int:
    return struct.unpack_from('<Q', buf, at)[0]


def lo32(v: int) -> int:
    return v & 0xFFFFFFFF


def sra(v: int, n: int) -> int:
    return s32(v) >> n


def find_chain(ram: bytes, list_start: int) -> tuple[int, int]:
    """The slot-1 chain the list NEXTs into: (first tag, after its NEXT out)."""
    t = list_start
    for _ in range(64):
        lo = q(ram, t)
        qwc, tid, addr = lo & 0xFFFF, (lo >> 28) & 7, (lo >> 32) & 0x7FFFFFFF
        for s in (0, 1):
            base = CH1_BASE + s * CH1_STRIDE
            if tid == 2 and base <= addr < base + CH1_STRIDE:
                u = addr
                while True:
                    lo2 = q(ram, u)
                    qwc2, tid2 = lo2 & 0xFFFF, (lo2 >> 28) & 7
                    if tid2 == 1:
                        u += 16 + qwc2 * 16
                    elif tid2 == 3:
                        u += 16
                    elif tid2 == 2:
                        return addr, u + 16
                    else:
                        raise RuntimeError(f'tag {tid2} in the slot-1 chain at {u:#x}')
        t = {1: t + 16 + qwc * 16, 2: addr, 3: t + 16}.get(tid)
        if t is None:
            break
    raise RuntimeError('no slot-1 chain in the list')


def ref_at(ram, at):
    lo = q(ram, at)
    return (lo >> 28) & 7, lo & 0xFFFF, (lo >> 32) & 0x7FFFFFFF


def decode(ram: bytes, start: int, end: int, blocks: int, font_tall: bytes, font_small: bytes, inv: dict,
           skewed: bool = False, fan_angles: list[int] | None = None):
    """The chain as [(address, kind, args)]: 'tag' entries for the open and
    close (their 8 written bytes), builder calls for the rest. A fan
    (001D66A0) decodes at rest only (FAN_REST), its f12 the k-th of
    `fan_angles` (the ring's angles; default 0.0, the port's neutral stick).
    skewed: the skewed glyph strips and the fans not at rest are accepted by
    their shape alone (args None)."""
    out, at = [], start
    modes = {0x6A0: 3, 0x7A0: 1, 0x820: 2, 0x720: 0}
    while at < end:
        tid, qwc, addr = ref_at(ram, at)
        nxt = ram[at + 0x10:at + 0x20]
        if tid == 3 and qwc == 8 and addr - blocks in modes:
            out.append((at, B_7D00, (1, modes[addr - blocks])))
            at += 0x10
        elif tid == 3 and addr == HEAD and qwc == 3:
            t2, q2, a2 = ref_at(ram, at + 0x10)
            t3, q3, _ = ref_at(ram, at + 0x20)
            if t2 == 3 and q2 == 8 and a2 == SMALL and t3 == 1 and q3 == 5:
                p = at + 0x20
                rgba, v40, v48, v58 = lo32(q(ram, p + 0x38)), lo32(q(ram, p + 0x40)), lo32(q(ram, p + 0x48)), lo32(q(ram, p + 0x58))
                a = (v40 >> 16) & 0xF
                x, y = (v40 & 0xFFFF) >> 4, sra(v40, 20)
                w, hh = ((v58 & 0xFFFF) >> 4) - x, sra(v58, 20) - y
                st = [0, 0, 0, 0, 0] if rgba == 0x80808080 and a == 0 else [1, s32(rgba), rgba >> 24, 0, a]
                out.append((at, B_CBC20, (1, x, y, w, 2 * hh, (v48 & 0xFFFF) >> 4, sra(v48, 20), *st)))
                at += 0x80
            elif t2 == 3 and q2 == 8 and a2 == SPRITE and t3 == 1 and q3 == 0x15:
                fill_pass, outline = at + 0x40 + 4 * 0x40, lo32(q(ram, at + 0x40 + 0x10))
                fill = lo32(q(ram, fill_pass + 0x10))
                uv1, x0, x1 = lo32(q(ram, fill_pass + 0x28)), lo32(q(ram, fill_pass + 0x20)), lo32(q(ram, fill_pass + 0x30))
                a = (x0 >> 16) & 0xF
                x, y = (x0 & 0xFFFF) >> 4, sra(x0, 20)
                w, hh = ((x1 & 0xFFFF) >> 4) - x, sra(x1, 20) - y
                st = ([0, 0, 0, 0, 0] if outline == 0x80100505 and fill == 0x80808080 and a == 0 else
                      [1, s32(fill), outline >> 24, 0, a])
                out.append((at, B_CC3B0, (1, x, y, (uv1 & 0xFFFF) >> 4, sra(uv1, 20), w, 2 * hh, *st)))
                at += 0x180
            elif t2 == 3 and q2 == 8 and a2 == STRIP and t3 == 1 and q3 == 0x1F and skewed:
                # 001CC3B0's skewed strip (style flag != 0): its shape only (the
                # five passes' GIF tags and register lists); its arguments are
                # not decoded (part A proves the builder against the original)
                for i in range(5):
                    gt, rg = q(ram, at + 0x40 + 0x60 * i), q(ram, at + 0x48 + 0x60 * i)
                    if gt != 0x9400000000008001 or rg != 0x434343431:
                        raise RuntimeError(f'skewed glyph strip pass {i} at {at:#x}: {gt:#x} {rg:#x}')
                out.append((at, B_CC3B0, None))
                at += 0x220
            else:
                raise RuntimeError(f'glyph flush shape not decoded at {at:#x} ({t2}, {q2}, {a2:#x}, {t3}, {q3})')
        elif tid == 1 and qwc == 7 and q(ram, at + 0x20) == 0xA400000000008001:
            tex0, rgba = q(ram, at + 0x40), lo32(q(ram, at + 0x50))
            xyz1, xyz2 = lo32(q(ram, at + 0x58)), lo32(q(ram, at + 0x70))
            x, y = xyz1 & 0xFFFF, xyz2 >> 16
            w, h = (((xyz2 & 0xFFFF) - x) & 0xFFFF) >> 4, (((xyz1 >> 16) - y) & 0xFFFF) >> 3
            out.append((at, B_7E40, (1, x, y, w, h, rgba, tex0)))
            at += 0x80
        elif tid == 1 and qwc == 0x86 and q(ram, at + 0x20) == 0x4026400000008021 and q(ram, at + 0x28) == 0x4141:
            # 001D66A0 (0020AC70's fan): the inputs are recovered at rest only
            a2, a3 = struct.unpack_from('<4I', ram, at + 0x30), struct.unpack_from('<4I', ram, at + 0x50)
            centre = struct.unpack_from('<4I', ram, at + 0x40)
            for i in range(33):
                p = at + 0x30 + 0x40 * i
                if (struct.unpack_from('<4I', ram, p) != a2 or struct.unpack_from('<4I', ram, p + 0x10) != centre
                        or struct.unpack_from('<4I', ram, p + 0x20) != a3
                        or struct.unpack_from('<2I', ram, p + 0x38) != (0xFFFFFF, 0)):
                    raise RuntimeError(f'fan at {at:#x}: pair {i} is not 001D66A0\'s')
            k = sum(1 for _, w, _ in out if w == B_1D66A0)
            if centre[:2] not in FAN_REST or centre[2:] != (0xFFFFFF, 0) or a2 != FAN_REST_A2 or a3 != FAN_A3:
                if not skewed:
                    raise RuntimeError(f'fan at {at:#x}: not at rest (centre {centre[:2]}, colour {a2}): its '
                                       f'inputs are not recoverable from the packet')
                out.append((at, B_1D66A0, None))   # by shape only (the stick moved)
            else:
                angle = fan_angles[k] if fan_angles else 0
                out.append((at, B_1D66A0, (1, *FAN_REST[centre[:2]], *a2, *a3, angle)))
            at += 0x870
        elif (tid == 1 and qwc == 3 and q(ram, at + 0x20) == 0x1000000000008001 and q(ram, at + 0x28) == 0xE
              and q(ram, at + 0x38) == 0x40):
            # 00207D90 (SCISSOR_1 by A+D; DATABASE's 00213CC0): its packed
            # word is rebuilt from the decoded corners, so a corner the
            # 16-bit fields cannot hold (a negative one) refuses
            packed = q(ram, at + 0x30)
            x0, x1, y0, y1 = packed & 0xFFFF, (packed >> 16) & 0xFFFF, (packed >> 32) & 0xFFFF, packed >> 48
            if x0 | x1 << 16 | y0 << 32 | y1 << 48 != packed or ram[at + 0x10:at + 0x1C] != bytes(12) \
                    or lo32(q(ram, at + 0x18) >> 32) != 0x50000002:
                raise RuntimeError(f'scissor at {at:#x}: not 00207D90\'s packet')
            out.append((at, B_7D90, (1, x0, y0, x1, y1)))
            at += 0x40
        elif tid == 1 and qwc == 4 and q(ram, at + 0x20) == 0x4400000000008001:
            d40, d48 = lo32(q(ram, at + 0x40)), lo32(q(ram, at + 0x48))
            out.append((at, B_7F80, (1, d40 & 0xFFFF, d40 >> 16, d48 & 0xFFFF, d48 >> 16, lo32(q(ram, at + 0x38)))))
            at += 0x50
        elif tid == 1 and qwc in (0xF, 0x27) and q(ram, at + 0x20) == 0x1000000000000004:
            tall = qwc == 0x27
            trxpos = q(ram, at + 0x40)
            x, y = (trxpos >> 32) & 0xFFFF, trxpos >> 48
            d = at + 0x80
            if tall:
                hw = [(d + 0x20 * i + o) for i in range(10) for o in (0, 2, 4, 0x10, 0x12, 0x14)]
            else:
                hw = [d + 2 * i for i in range(0x40)]
            nibs = [inv.get(struct.unpack_from('<H', ram, h)[0]) for h in hw]
            if None in nibs:
                raise RuntimeError(f'upload at {at:#x}: a texel halfword outside D_0026E350')
            data = bytes(nibs[2 * i] | nibs[2 * i + 1] << 4 for i in range(len(nibs) // 2))
            font, step = (font_tall, 30) if tall else (font_small, 32)
            hits = [o for o in range(0, len(font) - len(data) + 1, step) if font[o:o + len(data)] == data]
            if not hits:
                raise RuntimeError(f'upload at {at:#x}: its texels are no glyph of the font')
            out.append((at, B_CC8A0, (1 if tall else 0, x, y, hits[0])))
            at += 0x280 if tall else 0x100
        else:
            raise RuntimeError(f'packet not decoded at {at:#x}: tag {tid} qwc {qwc} addr {addr:#x} {nxt.hex()}')
    return out


def tag_bytes(buf: bytearray, at: int, base: int, raw: bytes) -> None:
    """The 7 bytes a tag head store writes (+0, +1, +3..+7)."""
    o = at - base
    buf[o:o + 2] = raw[0:2]
    buf[o + 3:o + 8] = raw[3:8]


def part_b(lib, beat: str, T: int, raster: bool, rlib) -> dict:
    d = CAP / beat / f'f{T:04d}'
    info = json.loads((d / 'capture.json').read_text())
    ram = (d / 'ee_V_T1.bin').read_bytes()
    start_list = int(info['full']['list_start'], 16)
    context = struct.unpack_from('<I', ram, CONTEXT_PTR)[0]
    blocks = struct.unpack_from('<I', ram, GS_BLOCKS_PTR)[0]
    n = Native(lib, ram, context, blocks)
    nibs = struct.unpack_from('<16H', ram, NIBBLES)
    inv = {v: i for i, v in enumerate(nibs)}
    first, end = find_chain(ram, start_list)
    tall = struct.unpack_from('<I', ram, FONT_TALL_PTR)[0] & 0x1FFFFFF
    small = struct.unpack_from('<I', ram, FONT_SMALL_PTR)[0] & 0x1FFFFFF
    where = f'{beat} f{T:04d}'
    # the open: 001D1F20(1) (bank A of slot k), 001D2040(1, 0), 001D1FF0(1, 1)
    opens = [ref_at(ram, first + 0x10 * i) for i in range(3)]
    k = (opens[0][2] - blocks - 0x20) // 0x190
    want_open = [(3, 0x19, blocks + 0x20 + 0x190 * k), (3, 4, blocks + 0x5A0), (3, 4, blocks + 0x4E0)]
    if k not in (0, 1) or opens != want_open:
        fail(f'{where}: the chain open {opens} is not 001D1AE0\'s')
        return {}
    # the close: REF of GS block +0 (1 quadword), 001D1F80(1, 0, 7), NEXT to the list
    closes = [ref_at(ram, end - 0x30 + 0x10 * i) for i in range(3)]
    if closes[0] != (3, 1, blocks) or closes[1] != (3, 9, blocks + 0xF90) or closes[2][0] != 2:
        fail(f'{where}: the chain close {closes} is not step V\'s')
        return {}
    # the fans' f12: the trail ring D_00821300 after the frame's 0020AC70 (the
    # angles do not decay; at rest the magnitudes are 0 before and after)
    ring = (d / 'ee_V_T.bin').read_bytes()[0x821300:0x821400]
    angles = [struct.unpack_from('<I', ring, 16 * k + 12)[0] for k in range(16)]
    try:
        calls = decode(ram, first + 0x30, end - 0x30, blocks, ram[tall:tall + FONT_TALL_SIZE],
                       ram[small:small + FONT_SMALL_SIZE], inv, fan_angles=angles)
    except RuntimeError as e:
        fail(f'{where}: {e}')
        return {}
    cap = ram[first:end]
    cell = context + 0x14
    builds = []
    for fill in (0xA5, 0x5A):
        buf = bytearray([fill]) * (end - first)
        for i in range(3):
            tag_bytes(buf, first + 0x10 * i, first, ram[first + 0x10 * i:first + 0x10 * i + 8])
            tag_bytes(buf, end - 0x30 + 0x10 * i, first, ram[end - 0x30 + 0x10 * i:end - 0x30 + 0x10 * i + 8])
        cbuf = bytearray(struct.pack('<I', first + 0x30))
        for idx, (at, which, args) in enumerate(calls):
            if struct.unpack('<I', cbuf)[0] != at:
                fail(f'{where}: before call {idx} ({NAME[which]}) the cursor is {struct.unpack("<I", cbuf)[0]:#x}, '
                     f'the captured packet at {at:#x}')
                return {}
            rc, why = n.call(which, args, [(cell, cbuf), (first, buf)])
            if rc != 0:
                fail(f'{where}: call {idx} ({NAME[which]} {args}) refused: {why}')
                return {}
        if struct.unpack('<I', cbuf)[0] != end - 0x30:
            fail(f'{where}: the last cursor {struct.unpack("<I", cbuf)[0]:#x} is not the close at {end - 0x30:#x}')
            return {}
        builds.append(buf)
    a, b = np.frombuffer(bytes(builds[0]), np.uint8), np.frombuffer(bytes(builds[1]), np.uint8)
    c = np.frombuffer(cap, np.uint8)
    written = a == b
    off = int((written & (a != c)).sum())
    if off:
        i = int(np.nonzero(written & (a != c))[0][0])
        fail(f'{where}: {off} written bytes differ from the capture, the first at {first + i:#x}')
    # the unwritten bytes by class
    classes = {'dma_tag_unsent': 0, 'reglist_pad': 0, 'tall_texels': 0}
    unwritten = set(int(i) for i in np.nonzero(~written)[0])
    expect = set()
    tags = [first + 0x10 * i for i in range(3)] + [end - 0x30 + 0x10 * i for i in range(3)]
    for at, which, args in calls:
        if which == B_CC3B0:
            tags += [at, at + 0x10, at + 0x20]
            for i in range(5):
                p = at + 0x40 + 0x40 * i + 0x38 - first
                expect.update(range(p, p + 8))
                classes['reglist_pad'] += 8
        elif which == B_CBC20:
            tags += [at, at + 0x10, at + 0x20]
        else:
            tags.append(at)
        if which == B_CC8A0 and args[0] == 1:
            d0 = at + 0x80 - first
            filled = set(d0 + 0x20 * i + o + j for i in range(10) for o in (0, 2, 4, 0x10, 0x12, 0x14) for j in (0, 1))
            gap = set(range(d0, d0 + 0x200)) - filled
            expect.update(gap)
            classes['tall_texels'] += len(gap)
    for t in tags:
        o = t - first
        expect.update([o + 2, *range(o + 8, o + 16)])
        classes['dma_tag_unsent'] += 9
    if unwritten != expect:
        extra, missing = sorted(unwritten - expect), sorted(expect - unwritten)
        fail(f'{where}: unwritten bytes outside the listed classes {[hex(first + x) for x in extra[:4]]}, '
             f'listed but written {[hex(first + x) for x in missing[:4]]}')
    kinds: dict[str, int] = {}
    for _, which, args in calls:
        key = NAME[which] + (f' mode {args[0]}' if which == B_CC8A0 else '')
        kinds[key] = kinds.get(key, 0) + 1
    line = (f'  B {where}: slot-1 chain {first:#x}..{end:#x}, {len(calls)} calls {kinds}; written bytes equal '
            f'({int(written.sum())}), unwritten {classes}')
    print(line)
    res = {'calls': kinds, 'written': int(written.sum()), 'unwritten': classes}
    if raster:
        res['raster'] = raster_check(rlib, d, info, ram, first, bytes(builds[0]), where)
    return res


def raster_check(rlib, d: Path, info: dict, ram: bytes, first: int, chain: bytes, where: str) -> dict:
    """The captured list with the rebuilt chain (fill in its unwritten bytes)
    through the CPU GS model, as test_status_field_reference."""
    full = info['full']
    slot, start = full['slot_9C'], int(full['list_start'], 16)
    ram_t = (d / 'ee_V_T.bin').read_bytes()
    rebuilt = bytearray(ram)
    rebuilt[first:first + len(chain)] = chain
    prev = sf.direct(sf.walk(ram_t, sf.LIST_BASE + ((slot ^ 1) << 14)))
    cur = sf.direct(sf.walk(bytes(rebuilt), start))
    regs = sf.state_writes(prev)
    frame = sf.state_writes(cur).get(0x4C)
    fbp = frame & 0x1FF
    before, after = (d / 'vram_before.bin').read_bytes(), (d / 'vram_after.bin').read_bytes()
    want = np.frombuffer(after, '<u4')
    words = sf.field_words(rlib, fbp, 512, 224)
    disp = np.frombuffer((d / 'displayed_field_T2.rgba').read_bytes(), '<u4')
    mem = bytearray(before)
    r = sf.replay(rlib, mem, regs, cur, 0)
    got = np.frombuffer(bytes(mem), '<u4')
    diff = np.nonzero(got != want)[0] * 4
    outside = int(((diff < STRIP_PAGES[0]) | (diff >= STRIP_PAGES[1])).sum())
    field_ok = bool((got[words] == disp).all())
    print(f'  B {where} raster: {r["prims"]} primitives, field equals the displayed field: {field_ok}; local memory '
          f'words off {len(diff)} (outside the glyph strip pages: {outside})')
    if not field_ok:
        fail(f'{where}: the field drawn from the rebuilt chain is not the displayed field')
    if outside or r['refused'] or r['span_faults'] or r['consumed'] != len(cur):
        fail(f'{where}: rebuilt list: {outside} words off outside the strip pages, {r["refused"]} refused, '
             f'{r["span_faults"]} span faults')
    return {'field_equal': field_ok, 'words_off_in_strip_pages': int(len(diff)), 'outside': outside}


def fonts_check(lib, ram: bytes, elf: bytes, text: int) -> dict:
    """em_ui_font_raw: both resources rebuilt from the font export equal the
    capture's every byte; em_ufr_nibbles equals the ELF's D_0026E350."""
    if not FONT_EXPORT.exists():
        fail(f'no font export at {FONT_EXPORT} (the decomp\'s tools/export_font.py)')
        return {}
    tall, small = C.create_string_buffer(UFR_TALL), C.create_string_buffer(UFR_SMALL)
    nib, why = (C.c_uint16 * 16)(), C.create_string_buffer(128)
    rc = lib.uipb_fonts(str(FONT_EXPORT).encode(), tall, small, nib, why, 128)
    if rc != 0:
        fail(f'em_ufr_load refused the font export: {why.value.decode()}')
        return {}
    t = struct.unpack_from('<I', ram, FONT_TALL_PTR)[0] & 0x1FFFFFF
    sm = struct.unpack_from('<I', ram, FONT_SMALL_PTR)[0] & 0x1FFFFFF
    out = {}
    for name, got, want in (('tall', tall.raw, ram[t:t + UFR_TALL]), ('small', small.raw, ram[sm:sm + UFR_SMALL])):
        off = sum(a != b for a, b in zip(got, want))
        if off:
            fail(f'the {name} font rebuilt from the export differs from the capture\'s in {off} bytes')
        out[name] = len(got) - off
    if bytes(nib) != elf[NIBBLES + text:NIBBLES + text + 32]:
        fail('em_ufr_nibbles differs from the ELF\'s D_0026E350')
    print(f'  fonts: the export rebuilt to the resources\' bytes, every byte equal (tall {out.get("tall")}, '
          f'small {out.get("small")}); D_0026E350 equal to em_ufr_nibbles')
    return out


def main() -> int:
    t0 = time.monotonic()
    missing = [p for p in [CAP / b / f'f{T:04d}' / 'capture.json' for b, T in FRAMES] if not p.exists()]
    if missing:
        print(f'test_ui_packet_reference: REFUSED (no capture at {missing[0]}; decomp '
              f'build/status_field_capture/capture.py records them)')
        return 2
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    if hashlib.sha256(elf).hexdigest() != ELF_SHA256:
        print('test_ui_packet_reference: REFUSED (wrong original executable)')
        return 2
    lib = build()
    rlib = sf.build()
    ram = (CAP / '01_battery/f0300/ee_V_T1.bin').read_bytes()
    text = 0x300 - 0x100000
    for address, size in EXECUTED:
        if ram[address:address + size] != elf[address + text:address + text + size]:
            fail(f'the capture\'s {address:08X} differs from the pinned ELF')
    if ram[NIBBLES:NIBBLES + 32] != elf[NIBBLES + text:NIBBLES + text + 32]:
        fail('the capture\'s D_0026E350 differs from the pinned ELF')
    print(f'mode {MODE}: A the builders against the original instructions, B {len(FRAMES)} captured chains')
    report = {'mode': MODE, 'fonts': fonts_check(lib, ram, elf, text), 'A': part_a(elf, ram, lib), 'B': {}}
    raster = FRAMES if FULL else RASTER_QUICK
    for beat, T in FRAMES:
        report['B'][f'{beat}/f{T:04d}'] = part_b(lib, beat, T, (beat, T) in raster, rlib)
    dt = time.monotonic() - t0
    (OUT / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    if FAILS:
        print(f'test_ui_packet_reference: FAIL ({len(FAILS)} failures; {dt:.1f} s)')
        return 1
    print(f'test_ui_packet_reference: PASS (the builders byte-equal to the original, {len(FRAMES)} captured slot-1 '
          f'chains rebuilt byte for byte, {len(raster)} drawn to the displayed field; {dt:.1f} s)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
